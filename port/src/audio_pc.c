/**
 * PC-port audio backend — AUDIO PHASE 1b/1c.
 *
 * The real AL synthesizer (src/libultra/audio + Rare's libultrare/audio)
 * runs CPU-side and emits an Acmd list for the RSP audio microcode
 * (aspMain). On PC nothing executes that list, so this file provides:
 *
 *   1. portAcmdRun(): an interpreter for the aspMain command set, using
 *      the encodings straight from include/PR/abi.h (the same header the
 *      library emits through, same compiler — no byte-order concerns).
 *      DMEM is a 4KB scratch array; RAM addresses in commands are used as
 *      RAW HOST POINTERS (the library only ever emits aSegment(0,0), and
 *      the port's osVirtualToPhysical is the identity).
 *
 *   2. portAudioPump(): the frame driver replacing the N64 audio thread
 *      (amMain is never scheduled by pcsched). Called once per video
 *      frame from the main loop, it renders one frame per game field and
 *      keeps the SDL queue just above a small target:
 *      alAudioFrame() -> portAcmdRun() -> SDL_QueueAudio().
 *
 *   3. portAudioInit(): SDL audio device setup, invoked lazily by the
 *      first pump after amCreateAudioManager has run alInit/alSynNew.
 *      A failed open, or a device closed after it stalled, is retried
 *      every 2 s or more while the mixer runs on the wall clock.
 *
 * RAW16 wavetables are converted to native s16 when loading the bank;
 * ADPCM wavetables remain byte streams. A_LOADBUFF copies both unchanged.
 *
 * State blobs (ADPCM/resampler/envmixer state RAM the driver hands us via
 * command words) are only ever read/written by THIS interpreter, so their
 * internal layout is self-consistent native — except ADPCM loop state
 * (ALADPCMloop.state from the .ctl), which alBnkfNew already byteswapped
 * to native s16s. RSP state sizes (32/40/80 bytes) are respected.
 */

#include <SDL.h>

#include <ultra64.h>
#include <PR/libaudio.h>
#include <PR/abi.h>

/* ---- config ------------------------------------------------------------ */

#define PORT_AUDIO_RATE      22050  /* both GE banks; alSynConfig outputRate */
#define PORT_AUDIO_DEV_SAMPLES 256       /* device period, 11.6 ms */
#define PORT_AUDIO_QUEUE_MAX (16 * 1024)
#define PORT_AUDIO_TARGET    3584        /* lowest queue level the drift trim aims for (41 ms) */
#define PORT_AUDIO_TARGET_MAX (6 * 1024) /* raised this far by queues that ran dry */
#define PORT_AUDIO_TRIM_MS   500
#define PORT_AUDIO_FIELD_BYTES (PORT_AUDIO_RATE / PORT_AUDIO_FIELD_HZ * 4)
#define PORT_MAX_FRAME_SAMPLES 1024
#define PORT_AUDIO_RETRY_MS  2000   /* open again this long after a failure */
#define PORT_AUDIO_RETRY_MAX_MS 30000
#ifdef REFRESH_PAL
#define PORT_AUDIO_FIELD_HZ 50
#else
#define PORT_AUDIO_FIELD_HZ 60
#endif

extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
extern void portConLogf(const char *fmt, ...); /* port_console.c */

/* audi.c globals we drive directly (the amMain thread never runs) */
extern u32 g_FrameSize;

static SDL_AudioDeviceID s_AudioDev;
static u32 s_AudioRetryTick; /* SDL_GetTicks() of the next open attempt */
static u32 s_AudioFails;     /* failed opens in a row */
static u32 s_AudioReopens;   /* stalled devices closed for a reopen */
static s32 s_AudioSubsys;    /* SDL_InitSubSystem(SDL_INIT_AUDIO) holds a reference */
static s32 s_AudioLogOpen;   /* this open attempt logs its details */
static s32 s_ForceOneFrame; /* portAudioPumpForce: one frame, whatever the queue */

/* Output health, summarised in the log every ~10 s when any is nonzero. */
static u32 s_StatSkipped;  /* frames portAcmdValidate refused (played as silence) */
static u32 s_StatDry;      /* pumps that found the SDL queue already empty */
static u32 s_StatOverCap;  /* rendered frames dropped at PORT_AUDIO_QUEUE_MAX */
static u32 s_StatMaxGapMs; /* longest wait between two pumps */

/* ---- aspMain interpreter ------------------------------------------------ */

#define DMEM_SIZE 0x1000

static u8 s_Dmem[DMEM_SIZE + 64]; /* +64: slop for 8-byte-rounded loads */

/* A_SETBUFF state */
static u16 s_BufIn;
static u16 s_BufOut;
static u16 s_BufCount;
static u16 s_BufFlags;
/* A_SETBUFF(A_AUX) state for the envmixer: dmemin=MAIN_R, dmemout=AUX_L,
 * count=AUX_R (all DMEM addresses) */
static u16 s_MixMainR;
static u16 s_MixAuxL;
static u16 s_MixAuxR;

/* A_SETVOL pending params. GE's env_wip.c emits five aSetVolumes per
 * voice segment: L/R current vols (A_VOL), L/R target+rate (A_RATE,
 * rate = ratm.ratel signed 16.16 PER 8 SAMPLES from _getRate's LINEAR
 * (tgt-vol)/count*8), and dry/wet (A_AUX). Engine semantics ported from
 * the Perfect Dark port's sm64-lineage mixer (port/src/mixer.c). */
static s16 s_VolL, s_VolR;
static s16 s_TgtL, s_TgtR;
static s32 s_RateL, s_RateR;
static s16 s_Dry, s_Wet;

/* 4-tap windowed resample table (RSP aspMain; via sm64-port/PD mixer) */
static const s16 s_ResampleTable[64][4] = {
    {0x0c39, 0x66ad, 0x0d46, (s16)0xffdf}, {0x0b39, 0x6696, 0x0e5f, (s16)0xffd8},
    {0x0a44, 0x6669, 0x0f83, (s16)0xffd0}, {0x095a, 0x6626, 0x10b4, (s16)0xffc8},
    {0x087d, 0x65cd, 0x11f0, (s16)0xffbf}, {0x07ab, 0x655e, 0x1338, (s16)0xffb6},
    {0x06e4, 0x64d9, 0x148c, (s16)0xffac}, {0x0628, 0x643f, 0x15eb, (s16)0xffa1},
    {0x0577, 0x638f, 0x1756, (s16)0xff96}, {0x04d1, 0x62cb, 0x18cb, (s16)0xff8a},
    {0x0435, 0x61f3, 0x1a4c, (s16)0xff7e}, {0x03a4, 0x6106, 0x1bd7, (s16)0xff71},
    {0x031c, 0x6007, 0x1d6c, (s16)0xff64}, {0x029f, 0x5ef5, 0x1f0b, (s16)0xff56},
    {0x022a, 0x5dd0, 0x20b3, (s16)0xff48}, {0x01be, 0x5c9a, 0x2264, (s16)0xff3a},
    {0x015b, 0x5b53, 0x241e, (s16)0xff2c}, {0x0101, 0x59fc, 0x25e0, (s16)0xff1e},
    {0x00ae, 0x5896, 0x27a9, (s16)0xff10}, {0x0063, 0x5720, 0x297a, (s16)0xff02},
    {0x001f, 0x559d, 0x2b50, (s16)0xfef4}, {(s16)0xffe2, 0x540d, 0x2d2c, (s16)0xfee8},
    {(s16)0xffac, 0x5270, 0x2f0d, (s16)0xfedb}, {(s16)0xff7c, 0x50c7, 0x30f3, (s16)0xfed0},
    {(s16)0xff53, 0x4f14, 0x32dc, (s16)0xfec6}, {(s16)0xff2e, 0x4d57, 0x34c8, (s16)0xfebd},
    {(s16)0xff0f, 0x4b91, 0x36b6, (s16)0xfeb6}, {(s16)0xfef5, 0x49c2, 0x38a5, (s16)0xfeb0},
    {(s16)0xfedf, 0x47ed, 0x3a95, (s16)0xfeac}, {(s16)0xfece, 0x4611, 0x3c85, (s16)0xfeab},
    {(s16)0xfec0, 0x4430, 0x3e74, (s16)0xfeac}, {(s16)0xfeb6, 0x424a, 0x4060, (s16)0xfeaf},
    {(s16)0xfeaf, 0x4060, 0x424a, (s16)0xfeb6}, {(s16)0xfeac, 0x3e74, 0x4430, (s16)0xfec0},
    {(s16)0xfeab, 0x3c85, 0x4611, (s16)0xfece}, {(s16)0xfeac, 0x3a95, 0x47ed, (s16)0xfedf},
    {(s16)0xfeb0, 0x38a5, 0x49c2, (s16)0xfef5}, {(s16)0xfeb6, 0x36b6, 0x4b91, (s16)0xff0f},
    {(s16)0xfebd, 0x34c8, 0x4d57, (s16)0xff2e}, {(s16)0xfec6, 0x32dc, 0x4f14, (s16)0xff53},
    {(s16)0xfed0, 0x30f3, 0x50c7, (s16)0xff7c}, {(s16)0xfedb, 0x2f0d, 0x5270, (s16)0xffac},
    {(s16)0xfee8, 0x2d2c, 0x540d, (s16)0xffe2}, {(s16)0xfef4, 0x2b50, 0x559d, 0x001f},
    {(s16)0xff02, 0x297a, 0x5720, 0x0063}, {(s16)0xff10, 0x27a9, 0x5896, 0x00ae},
    {(s16)0xff1e, 0x25e0, 0x59fc, 0x0101}, {(s16)0xff2c, 0x241e, 0x5b53, 0x015b},
    {(s16)0xff3a, 0x2264, 0x5c9a, 0x01be}, {(s16)0xff48, 0x20b3, 0x5dd0, 0x022a},
    {(s16)0xff56, 0x1f0b, 0x5ef5, 0x029f}, {(s16)0xff64, 0x1d6c, 0x6007, 0x031c},
    {(s16)0xff71, 0x1bd7, 0x6106, 0x03a4}, {(s16)0xff7e, 0x1a4c, 0x61f3, 0x0435},
    {(s16)0xff8a, 0x18cb, 0x62cb, 0x04d1}, {(s16)0xff96, 0x1756, 0x638f, 0x0577},
    {(s16)0xffa1, 0x15eb, 0x643f, 0x0628}, {(s16)0xffac, 0x148c, 0x64d9, 0x06e4},
    {(s16)0xffb6, 0x1338, 0x655e, 0x07ab}, {(s16)0xffbf, 0x11f0, 0x65cd, 0x087d},
    {(s16)0xffc8, 0x10b4, 0x6626, 0x095a}, {(s16)0xffd0, 0x0f83, 0x6669, 0x0a44},
    {(s16)0xffd8, 0x0e5f, 0x6696, 0x0b39}, {(s16)0xffdf, 0x0d46, 0x66ad, 0x0c39}
};

/* A_LOADADPCM book (s16 coefficients, already native from alBnkfNew) */
static s16 s_Book[128];
/* A_SETLOOP loop-state pointer (ALADPCMloop.state, native s16[16]) */
static s16 *s_LoopState;

static s16 *dmem16(u32 byteaddr)
{
    return (s16 *)&s_Dmem[byteaddr & (DMEM_SIZE - 1)];
}

static s16 clamp16(s32 v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (s16)v;
}

/* envmixer state blob (fits the RSP's 80 bytes; PD-mixer layout) */
typedef struct {
    s32 t[2];
    s32 rate[2];
    s16 tgt[2];
    s16 voldry;
    s16 volwet;
} PortEnvState;

/**
 * EXACT aspMain ADPCM (semantics ported from the Perfect Dark port's
 * sm64-lineage mixer, scalar path). Two things my first cut got wrong,
 * both catastrophic:
 *  - the 16-sample state is written INTO THE OUTPUT REGION first (the
 *    decoded samples follow it) - load.c's lastsam/DMEMMove addressing
 *    is built around that convention, so omitting the prefix shifted
 *    every downstream read by 32 bytes;
 *  - the intra-group prediction uses the book row-1 cross terms on the
 *    UNCLAMPED residuals (matrix form), not a sequential clamped IIR.
 */
static void portAcmdAdpcm(u32 flags, void *stateAddr)
{
    const u8 *in = &s_Dmem[s_BufIn & (DMEM_SIZE - 1)];
    s16 *out = dmem16(s_BufOut);
    s32 nbytes = (s_BufCount + 31) & ~31;
    s16 *state = (s16 *)stateAddr;

    if (flags & A_INIT) {
        memset(out, 0, 16 * sizeof(s16));
    } else if (flags & A_LOOP) {
        memcpy(out, s_LoopState, 16 * sizeof(s16));
    } else {
        memcpy(out, state, 16 * sizeof(s16));
    }
    out += 16;

    while (nbytes > 0) {
        s32 shift = *in >> 4;
        s32 table_index = *in++ & 0xF;
        const s16 *tbl0 = &s_Book[table_index * 16];
        const s16 *tbl1 = &s_Book[table_index * 16 + 8];
        s32 i;

        for (i = 0; i < 2; i++) {
            s16 ins[8];
            s16 prev1 = out[-1];
            s16 prev2 = out[-2];
            s32 j, k;

            for (j = 0; j < 4; j++) {
                ins[j * 2] = (s16)((((*in >> 4) << 28) >> 28) << shift);
                ins[j * 2 + 1] = (s16)((((*in++ & 0xF) << 28) >> 28) << shift);
            }
            for (j = 0; j < 8; j++) {
                s32 acc = tbl0[j] * prev2 + tbl1[j] * prev1 + (ins[j] << 11);

                for (k = 0; k < j; k++) {
                    acc += tbl1[(j - k) - 1] * ins[k];
                }
                acc >>= 11;
                *out++ = clamp16(acc);
            }
        }
        nbytes -= 16 * (s32)sizeof(s16);
    }

    if (state != NULL) {
        memcpy(state, out - 16, 16 * sizeof(s16));
    }
}

/**
 * EXACT aspMain resampler (4-tap windowed table; semantics ported from
 * the PD port's sm64-lineage mixer, scalar path). State blob layout is
 * the RSP's own: state[0..3] = 4 carried input samples, state[4] =
 * pitch fraction, state[5] = rewind bytes, state[8..15] = 8 samples for
 * the rewind path. The 4-sample history is written BEFORE the input
 * pointer in DMEM, exactly like the microcode does.
 */
static void portAcmdResample(u32 flags, u32 pitch, void *stateAddr)
{
    s16 *state = (s16 *)stateAddr;
    s16 tmp[16];
    s16 *in_initial = dmem16(s_BufIn);
    s16 *in = in_initial;
    s16 *out = dmem16(s_BufOut);
    s32 nbytes = (s_BufCount + 15) & ~15;
    u32 pitch_accumulator;
    s32 i;

    if (flags & A_INIT) {
        memset(tmp, 0, 5 * sizeof(s16));
    } else {
        memcpy(tmp, state, 16 * sizeof(s16));
    }
    if (flags & 2) {
        memcpy(in - 8, tmp + 8, 8 * sizeof(s16));
        in -= tmp[5] / (s32)sizeof(s16);
    }
    in -= 4;
    pitch_accumulator = (u16)tmp[4];
    memcpy(in, tmp, 4 * sizeof(s16));

    do {
        for (i = 0; i < 8; i++) {
            const s16 *tbl = s_ResampleTable[pitch_accumulator * 64 >> 16];
            s32 sample = ((in[0] * tbl[0] + 0x4000) >> 15) +
                         ((in[1] * tbl[1] + 0x4000) >> 15) +
                         ((in[2] * tbl[2] + 0x4000) >> 15) +
                         ((in[3] * tbl[3] + 0x4000) >> 15);

            *out++ = clamp16(sample);
            pitch_accumulator += pitch << 1;
            in += pitch_accumulator >> 16;
            pitch_accumulator %= 0x10000;
        }
        nbytes -= 8 * (s32)sizeof(s16);
    } while (nbytes > 0);

    state[4] = (s16)pitch_accumulator;
    memcpy(state, in, 4 * sizeof(s16));
    i = (s32)((in - in_initial + 4) & 7);
    in -= i;
    if (i != 0) {
        i = -8 - i;
    }
    state[5] = (s16)i;
    memcpy(state + 8, in, 8 * sizeof(s16));
}

/**
 * EXACT envmixer (semantics ported from the PD port's sm64-lineage
 * mixer). GE's _getRate produces a LINEAR signed 16.16 volume delta per
 * 8 samples; the engine steps t += rate>>3 per sample and clamps on
 * target crossing. Gains are Q15 rounding multiplies; all four buses
 * accumulate with saturation.
 */
static void portAcmdEnvMixer(u32 flags, void *stateAddr)
{
    PortEnvState *st = (PortEnvState *)stateAddr;
    const s16 *in = dmem16(s_BufIn);
    s16 *dry[2];
    s16 *wet[2];
    s32 n = s_BufCount >> 1;
    s32 useAux = (flags & A_AUX) != 0;
    s32 t[2], rate[2], tgt[2];
    s16 voldry, volwet;
    s32 i, j;

    dry[0] = dmem16(s_BufOut);
    dry[1] = dmem16(s_MixMainR);
    wet[0] = dmem16(s_MixAuxL);
    wet[1] = dmem16(s_MixAuxR);

    if (flags & A_INIT) {
        t[0] = s_VolL << 16;
        t[1] = s_VolR << 16;
        rate[0] = s_RateL >> 3;
        rate[1] = s_RateR >> 3;
        tgt[0] = s_TgtL << 16;
        tgt[1] = s_TgtR << 16;
        voldry = s_Dry;
        volwet = s_Wet;
    } else {
        for (j = 0; j < 2; j++) {
            t[j] = st->t[j];
            rate[j] = st->rate[j];
            tgt[j] = st->tgt[j] << 16;
        }
        voldry = st->voldry;
        volwet = st->volwet;
    }

    for (i = 0; i < n; i++) {
        s16 vol[2];
        s16 gain;
        s32 samp = in[i];

        for (j = 0; j < 2; j++) {
            t[j] += rate[j];
            if ((rate[j] <= 0 && t[j] <= tgt[j]) || (rate[j] > 0 && t[j] >= tgt[j])) {
                t[j] = tgt[j];
                rate[j] = 0;
            }
            vol[j] = (s16)(t[j] >> 16);
        }

        for (j = 0; j < 2; j++) {
            gain = clamp16((vol[j] * voldry + 0x4000) >> 15);
            dry[j][i] = clamp16(dry[j][i] + ((samp * gain) >> 15));
            if (useAux) {
                gain = clamp16((vol[j] * volwet + 0x4000) >> 15);
                wet[j][i] = clamp16(wet[j][i] + ((samp * gain) >> 15));
            }
        }
    }

    for (j = 0; j < 2; j++) {
        st->t[j] = t[j];
        st->rate[j] = rate[j];
        st->tgt[j] = (s16)(tgt[j] >> 16);
    }
    st->voldry = voldry;
    st->volwet = volwet;
}

/* bring-up (GE_DUMP_BOND): pre-validate every RAM address in the list;
 * returns 0 and photographs the neighbourhood on the first insane one.
 * Legit RAM lives >= 0x01000000 (exe image 0x10......, memp 0x2F......). */
static s32 portAcmdValidate(Acmd *list, s32 count)
{
    s32 c;

    for (c = 0; c < count; c++) {
        u32 w0 = list[c].words.w0;
        uintptr_t w1 = list[c].words.w1;
        u32 op = w0 >> 24;
        s32 bad = 0;

        switch (op) {
        case A_ADPCM:
        case A_RESAMPLE:
        case A_ENVMIXER:
        case A_LOADBUFF:
        case A_SAVEBUFF:
        case A_LOADADPCM:
        case A_SETLOOP:
        case A_POLEF:
            bad = (w1 < 0x01000000) || (w1 & 1);
            break;
        default:
            break;
        }

        if (bad) {
            static s32 shot;

            if (!shot) {
                f32 v[16];
                s32 k;
                s32 lo = (c >= 7) ? c - 7 : 0;

                shot = 1;
                v[0] = (f32)op;
                v[1] = (f32)c;
                for (k = 0; k < 7; k++) {
                    v[2 + k * 2] = (f32)list[lo + k].words.w0;
                    v[3 + k * 2] = (f32)list[lo + k].words.w1;
                }
                ge_dbg_vec("acmd INSANE op/idx/window7", v, 16);
                portConLogf("ge: audio: WARNING - audio frame skipped (silent): command %d op 0x%02X has address 0x%08X\n",
                            c, op, w1);
            }
            s_StatSkipped++;
            return 0;
        }
    }
    return 1;
}

void portAcmdRun(Acmd *list, s32 count)
{
    s32 c;

    if (!portAcmdValidate(list, count)) {
        return; /* skip the whole frame - diagnosis over crash */
    }

    for (c = 0; c < count; c++) {
        u32 w0 = list[c].words.w0;
        uintptr_t w1 = list[c].words.w1;
        u32 op = w0 >> 24;

        switch (op) {
        case A_SPNOOP:
        case A_SEGMENT: /* only ever aSegment(0,0); addresses are host ptrs */
            break;

        case A_CLEARBUFF: {
            u32 d = w0 & 0xFFFFFF;
            u32 n = w1;

            if (n > 0 && d < DMEM_SIZE) {
                if (d + n > DMEM_SIZE) {
                    n = DMEM_SIZE - d;
                }
                memset(&s_Dmem[d], 0, n);
            }
            break;
        }

        case A_SETBUFF:
            if (((w0 >> 16) & 0xFF) & A_AUX) {
                s_MixMainR = w0 & 0xFFFF;
                s_MixAuxL = w1 >> 16;
                s_MixAuxR = w1 & 0xFFFF;
            } else {
                s_BufFlags = (w0 >> 16) & 0xFF;
                s_BufIn = w0 & 0xFFFF;
                s_BufOut = w1 >> 16;
                s_BufCount = w1 & 0xFFFF;
            }
            break;

        case A_SETVOL: {
            u32 f = (w0 >> 16) & 0xFFFF;
            s16 vol = (s16)(w0 & 0xFFFF);
            s16 t = (s16)(w1 >> 16);
            s16 r = (s16)(w1 & 0xFFFF);

            if (f & A_AUX) {
                s_Dry = vol;
                s_Wet = r;
            } else if (f & A_VOL) {
                if (f & A_LEFT) {
                    s_VolL = vol;
                } else {
                    s_VolR = vol;
                }
            } else { /* A_RATE: target + LINEAR signed 16.16 delta / 8 samples */
                if (f & A_LEFT) {
                    s_TgtL = vol;
                    s_RateL = (s32)(((u32)(u16)t << 16) | (u16)r);
                } else {
                    s_TgtR = vol;
                    s_RateR = (s32)(((u32)(u16)t << 16) | (u16)r);
                }
            }
            break;
        }

        case A_LOADBUFF:
        case A_SAVEBUFF:
            if (w1 < 0x01000000) {
                /* bring-up (GE_DUMP_BOND): garbage RAM address emitted -
                 * photograph the command window once, then skip */
                static s32 warned2;

                if (!warned2) {
                    f32 v[14];
                    s32 k;
                    s32 lo = (c >= 6) ? c - 6 : 0;

                    warned2 = 1;
                    v[0] = (f32)op;
                    v[1] = (f32)c;
                    for (k = 0; k < 6; k++) {
                        v[2 + k * 2] = (f32)list[lo + k].words.w0;
                        v[3 + k * 2] = (f32)list[lo + k].words.w1;
                    }
                    ge_dbg_vec("acmd BAD LDSV op/idx/window", v, 14);
                }
                break;
            }
            if (op == A_LOADBUFF) {
                u32 n = (s_BufCount + 7) & ~7u;

                if (n > DMEM_SIZE) {
                    n = DMEM_SIZE;
                }
                memcpy(&s_Dmem[s_BufIn & (DMEM_SIZE - 1)], (void *)w1, n);
            } else {
                memcpy((void *)w1, &s_Dmem[s_BufOut & (DMEM_SIZE - 1)], s_BufCount);
            }
            break;

        case A_DMEMMOVE: {
            u32 din = w0 & 0xFFFFFF;
            u32 dout = w1 >> 16;
            u32 n = w1 & 0xFFFF;

            memmove(&s_Dmem[dout & (DMEM_SIZE - 1)], &s_Dmem[din & (DMEM_SIZE - 1)], n);
            break;
        }

        case A_LOADADPCM: {
            u32 n = w0 & 0xFFFFFF;

            if (n > sizeof(s_Book)) {
                n = sizeof(s_Book);
            }
            memcpy(s_Book, (void *)w1, n);
            break;
        }

        case A_SETLOOP:
            s_LoopState = (s16 *)w1;
            break;

        case A_ADPCM:
            /* legit state blobs live in the music heap (static .bss,
             * >= exe base 0x10000000) or memp arenas (0x2F......) - a
             * low "pointer" is emitted garbage */
            if (w1 < 0x01000000 || (w1 & 1)) {
                /* bring-up (GE_DUMP_BOND): a garbage state pointer got
                 * emitted - dump the command neighbourhood once and skip
                 * the decode instead of crashing */
                static s32 warned;

                if (!warned) {
                    f32 v[13];
                    s32 k;
                    s32 lo = (c >= 6) ? c - 6 : 0;

                    warned = 1;
                    v[0] = (f32)c;
                    for (k = 0; k < 6; k++) {
                        v[1 + k * 2] = (f32)list[lo + k].words.w0;
                        v[2 + k * 2] = (f32)list[lo + k].words.w1;
                    }
                    ge_dbg_vec("acmd BAD ADPCM idx/window-6w0w1", v, 13);
                }
                break;
            }
            portAcmdAdpcm((w0 >> 16) & 0xFF, (void *)w1);
            break;

        case A_RESAMPLE:
            portAcmdResample((w0 >> 16) & 0xFF, w0 & 0xFFFF, (void *)w1);
            break;

        case A_ENVMIXER:
            portAcmdEnvMixer((w0 >> 16) & 0xFF, (void *)w1);
            break;

        case A_MIXER: {
            s32 gain = (s16)(w0 & 0xFFFF);
            const s16 *min = dmem16(w1 >> 16);
            s16 *mout = dmem16(w1 & 0xFFFF);
            s32 n = ((s_BufCount + 15) & ~15) >> 1;
            s32 i;

            /* PD-mixer exact: gain -0x8000 runs a saturating subtract
             * pass FIRST, then the rounding Q15 multiply-accumulate */
            if (gain == -0x8000) {
                for (i = 0; i < n; i++) {
                    mout[i] = clamp16(mout[i] - min[i]);
                }
            }
            for (i = 0; i < n; i++) {
                mout[i] = clamp16(((mout[i] * 0x7fff + min[i] * gain) + 0x4000) >> 15);
            }
            break;
        }

        case A_INTERLEAVE: {
            const s16 *l = dmem16(w1 >> 16);
            const s16 *r = dmem16(w1 & 0xFFFF);
            s16 *o = dmem16(s_BufOut);
            s32 n = s_BufCount >> 1; /* per-channel sample count */
            s32 i;

            for (i = n - 1; i >= 0; i--) { /* backwards: out may alias in */
                s16 sl = l[i];
                s16 sr = r[i];

                o[i * 2] = sl;
                o[i * 2 + 1] = sr;
            }
            break;
        }

        case A_POLEF: {
            /* One-pole lowpass used in place by the reverb (reverb.c
             * _filterBuffer): y[n] = (fgain*x[n] + fc*y[n-1]) >> 14, with fc
             * read from the book area loaded by A_LOADADPCM. y[-1] carries
             * across subframes (A_INIT zeroes it); without this filter the
             * reverb sounds far too wet. */
            u32 fl = (w0 >> 16) & 0xFF;
            s32 fgain = (s16)(w0 & 0xFFFF);
            s16 *st = (s16 *)w1;
            s16 *buf = dmem16(s_BufIn);
            s32 n = s_BufCount >> 1;
            s32 fc = s_Book[8];
            s32 prev = (fl & A_INIT) ? 0 : st[0];
            s32 i;

            for (i = 0; i < n; i++) {
                prev = clamp16((fgain * buf[i] + fc * prev) >> 14);
                buf[i] = (s16)prev;
            }
            st[0] = (s16)prev;
            break;
        }

        default:
            break;
        }
    }
}

/* ---- SDL device + frame pump ------------------------------------------- */

#if defined(__linux__) && defined(__i386__)
static s32 portAudioFileExists(const char *path)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");

    if (rw == NULL) {
        return 0;
    }
    SDL_RWclose(rw);
    return 1;
}

/**
 * A 32-bit libpipewire on a 64-bit distro can look for its plugins in the 64-bit
 * directory and fail to start (murkantor/Project-Midas#19). When the variables are
 * unset and a 32-bit plugin directory exists, point PipeWire at it.
 */
static void portAudioPipeWire32Dirs(void)
{
    static const char *const prefixes[] = { "/usr/lib32", "/lib32", "/usr/lib/i386-linux-gnu" };
    char path[256];
    u32 i;

    if (SDL_getenv("SPA_PLUGIN_DIR") != NULL) {
        return;
    }
    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        SDL_snprintf(path, sizeof(path), "%s/spa-0.2/support/libspa-support.so", prefixes[i]);
        if (!portAudioFileExists(path)) {
            continue;
        }
        SDL_snprintf(path, sizeof(path), "%s/spa-0.2", prefixes[i]);
        SDL_setenv("SPA_PLUGIN_DIR", path, 0);
        portConLogf("ge: audio: SPA_PLUGIN_DIR unset - using the 32-bit PipeWire plugins in %s\n", path);

        SDL_snprintf(path, sizeof(path), "%s/pipewire-0.3/libpipewire-module-protocol-native.so", prefixes[i]);
        if (SDL_getenv("PIPEWIRE_MODULE_DIR") == NULL && portAudioFileExists(path)) {
            SDL_snprintf(path, sizeof(path), "%s/pipewire-0.3", prefixes[i]);
            SDL_setenv("PIPEWIRE_MODULE_DIR", path, 0);
            portConLogf("ge: audio: PIPEWIRE_MODULE_DIR unset - using %s\n", path);
        }
        return;
    }
}
#endif

/**
 * Open the default output device. SDL only tries one driver per init, and a
 * driver that initialises can still fail to open (a 32-bit PipeWire without its
 * plugins), so every other real driver is tried before giving up.
 */
static SDL_AudioDeviceID portAudioOpenAnyDriver(SDL_AudioSpec *want, SDL_AudioSpec *have)
{
    SDL_AudioDeviceID dev = 0;
    const char *first = NULL;
    s32 i;

#if defined(__linux__) && defined(__i386__)
    static s32 s_PipeWireChecked;

    if (!s_PipeWireChecked) {
        s_PipeWireChecked = 1; /* before the first open only: retries reuse it */
        portAudioPipeWire32Dirs();
    }
#endif
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        s_AudioSubsys = 1;
        first = SDL_GetCurrentAudioDriver();
        dev = SDL_OpenAudioDevice(NULL, 0, want, have, 0);
        if (dev != 0) {
            return dev;
        }
        if (s_AudioLogOpen) {
            portConLogf("ge: audio: driver '%s' could not open a device: %s\n", first ? first : "?", SDL_GetError());
        }
    } else if (s_AudioLogOpen) {
        portConLogf("ge: audio: SDL audio init failed: %s\n", SDL_GetError());
    }

    for (i = 0; i < SDL_GetNumAudioDrivers() && dev == 0; i++) {
        const char *name = SDL_GetAudioDriver(i);

        /* disk writes a file and dummy plays nothing: neither is a fallback */
        if (name == NULL || (first != NULL && SDL_strcmp(name, first) == 0) ||
            SDL_strcmp(name, "disk") == 0 || SDL_strcmp(name, "dummy") == 0) {
            continue;
        }
        if (SDL_AudioInit(name) != 0) {
            if (s_AudioLogOpen) {
                portConLogf("ge: audio: driver '%s' unavailable: %s\n", name, SDL_GetError());
            }
            continue;
        }
        dev = SDL_OpenAudioDevice(NULL, 0, want, have, 0);
        if (dev == 0 && s_AudioLogOpen) {
            portConLogf("ge: audio: driver '%s' could not open a device: %s\n", name, SDL_GetError());
        }
    }
    return dev;
}

/* Log the first of a run of failures, then every 30th. */
static s32 portAudioLogDue(u32 n)
{
    return n <= 1 || n % 30 == 0;
}

/**
 * Close the device and shut the audio driver down, so the next open starts from
 * a clean driver (a restarted sound server needs a new connection).
 */
static void portAudioClose(void)
{
    if (s_AudioDev != 0) {
        SDL_CloseAudioDevice(s_AudioDev);
        s_AudioDev = 0;
    }
    if (s_AudioSubsys) {
        s_AudioSubsys = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    SDL_AudioQuit(); /* a fallback driver is not reference counted */
}

/* Returns 1 when a device is open; on failure the pump retries later. */
static s32 portAudioInit(void)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    const char *drv;
    u32 start = SDL_GetTicks();
    u32 wait;
    s32 n, i;

    memset(&want, 0, sizeof(want));
    memset(&have, 0, sizeof(have));
    want.freq = PORT_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = PORT_AUDIO_DEV_SAMPLES;

    s_AudioLogOpen = s_AudioFails != 0 ? portAudioLogDue(s_AudioFails + 1) : portAudioLogDue(s_AudioReopens);
    s_AudioDev = portAudioOpenAnyDriver(&want, &have);
    if (s_AudioDev == 0) {
        portAudioClose();
        /* A driver teardown can stall the frame (ALSA's takes ~100 ms), so a
         * slow attempt is spaced out to cost at most 1% of the time. */
        wait = (SDL_GetTicks() - start) * 100;
        if (wait < PORT_AUDIO_RETRY_MS) {
            wait = PORT_AUDIO_RETRY_MS;
        }
        if (wait > PORT_AUDIO_RETRY_MAX_MS) {
            wait = PORT_AUDIO_RETRY_MAX_MS;
        }
        s_AudioRetryTick = SDL_GetTicks() + wait;
        s_AudioFails++;
        if (s_AudioLogOpen) {
            portConLogf("ge: audio: WARNING - no audio device could be opened (attempt %u, %u ms), the game is silent;"
                        " retrying every %u ms\n", s_AudioFails, SDL_GetTicks() - start, wait);
        }
        return 0;
    }
    SDL_PauseAudioDevice(s_AudioDev, 0);
    if (s_AudioFails != 0) {
        portConLogf("ge: audio: a device opened after %u failed attempts\n", s_AudioFails);
        s_AudioFails = 0;
        s_AudioLogOpen = 1; /* name the driver that recovered */
    }
    if (!s_AudioLogOpen) {
        return 1;
    }

    /* always-on: a silent-game report needs the backend that was chosen */
    drv = SDL_GetCurrentAudioDriver();
    portConLogf("ge: audio: opened the default device on driver '%s': %d Hz, %d ch, format 0x%04X, %d-sample buffer"
                " (asked %d Hz, 2 ch, 0x%04X, %d)\n",
                drv ? drv : "?", have.freq, (s32)have.channels, (u32)have.format, (s32)have.samples,
                PORT_AUDIO_RATE, (u32)AUDIO_S16SYS, PORT_AUDIO_DEV_SAMPLES);
    n = SDL_GetNumAudioDevices(0);
    for (i = 0; i < n && i < 8; i++) {
        const char *dname = SDL_GetAudioDeviceName(i, 0);

        portConLogf("ge: audio: output device %d of %d: '%s'\n", i + 1, n, dname ? dname : "?");
    }
    return 1;
}

/* Wall-clock pacing, in samples x 1000, for frames no device is draining. */
static u32 s_ClockTick;
static s32 s_ClockPrimed;
static s32 s_ClockCredit;

/* Queue level the last pump left, and when the device last consumed any. */
static u32 s_QueuedAfterPump;
static u32 s_DrainTick;
static s32 s_DrainPrimed;
static s32 s_Stalled;

/* Drift trim: the lowest queue level the pumps saw in this PORT_AUDIO_TRIM_MS
 * window, and the field frames still to skip for the last one. */
static u32 s_Target = PORT_AUDIO_TARGET;
static u32 s_Trough;
static u32 s_TroughTick;
static s32 s_TroughPrimed;
static s32 s_TrimFrames;

#define PORT_AUDIO_CLOCK_CAP   (250 * PORT_AUDIO_RATE) /* 250 ms of catch-up */
#define PORT_AUDIO_STALL_MS    2000
#define PORT_AUDIO_REOPEN_MS   2000 /* stalled this much longer: close and reopen */

static s32 portAudioClockDue(u32 samples)
{
    u32 now = SDL_GetTicks();
    u32 dt;

    if (!s_ClockPrimed) {
        s_ClockPrimed = 1;
        s_ClockTick = now;
        s_ClockCredit = 0;
    }
    dt = now - s_ClockTick;
    s_ClockTick = now;
    if (dt > 250) {
        dt = 250; /* a load stall must not turn into a burst of frames */
    }
    s_ClockCredit += (s32)dt * PORT_AUDIO_RATE;
    if (s_ClockCredit > PORT_AUDIO_CLOCK_CAP) {
        s_ClockCredit = PORT_AUDIO_CLOCK_CAP;
    }
    return s_ClockCredit >= (s32)samples * 1000;
}

static void portAudioClockSpend(u32 samples)
{
    s_ClockCredit -= (s32)samples * 1000;
    if (s_ClockCredit < -PORT_AUDIO_CLOCK_CAP) {
        s_ClockCredit = -PORT_AUDIO_CLOCK_CAP; /* forced frames ran ahead */
    }
}

/**
 * True while the open device has consumed nothing for PORT_AUDIO_STALL_MS. Its
 * queue then never drops below the top-up mark, so without this the synthesizer
 * would stop and the sound player's event queue would fill. A device stalled for
 * PORT_AUDIO_REOPEN_MS more is closed, and the pump's retry opens a new one.
 */
static s32 portAudioStalled(void)
{
    u32 q = SDL_GetQueuedAudioSize(s_AudioDev);
    u32 now = SDL_GetTicks();

    if (!s_DrainPrimed || q < s_QueuedAfterPump || q == 0) {
        s_DrainPrimed = 1;
        s_DrainTick = now;
        if (s_Stalled) {
            s_Stalled = 0;
            portConLogf("ge: audio: the device is consuming audio again\n");
        }
    } else if (!s_Stalled && now - s_DrainTick > PORT_AUDIO_STALL_MS) {
        s_Stalled = 1;
        s_ClockPrimed = 0;
        if (portAudioLogDue(s_AudioReopens + 1)) {
            portConLogf("ge: audio: WARNING - the device has played nothing for %d ms (%u bytes queued);"
                        " running the mixer without it\n", PORT_AUDIO_STALL_MS, q);
        }
    } else if (s_Stalled && now - s_DrainTick > PORT_AUDIO_STALL_MS + PORT_AUDIO_REOPEN_MS) {
        s_AudioReopens++;
        if (portAudioLogDue(s_AudioReopens)) {
            portConLogf("ge: audio: closing the stalled device to reopen it (reopen %u)\n", s_AudioReopens);
        }
        portAudioClose();
        s_AudioRetryTick = now; /* the next pump opens a new one */
        s_DrainPrimed = 0;
        s_QueuedAfterPump = 0;
        s_Stalled = 0; /* the clock keeps running: there is no device now */
        return 1;
    }
    return s_Stalled;
}

/**
 * Count pumps that arrive to an empty queue (the device ran out and played
 * silence) and log the counters every ~10 s, so a crackle report says whether
 * the output starved.
 */
static void portAudioHealth(s32 byClock)
{
    static u32 lastPump;
    static u32 lastLog;
    static s32 primed;
    u32 now = SDL_GetTicks();

    if (!primed) {
        primed = 1;
        lastPump = now;
        lastLog = now;
        return;
    }
    if (now - lastPump > s_StatMaxGapMs) {
        s_StatMaxGapMs = now - lastPump;
    }
    /* a device that was just opened starts empty, which is not a dry queue */
    if (!byClock && s_QueuedAfterPump != 0 && SDL_GetQueuedAudioSize(s_AudioDev) == 0) {
        s_StatDry++;
        /* ran dry with the pumps on time: keep more queued on this machine */
        if (now - lastPump < 100 && s_Target < PORT_AUDIO_TARGET_MAX) {
            s_Target += 1024;
        }
    }
    lastPump = now;
    if (now - lastLog >= 10000) {
        if (s_StatDry != 0 || s_StatOverCap != 0 || s_StatSkipped != 0) {
            portConLogf("ge: audio: last %u s: queue ran dry %u times, %u frames over the cap dropped,"
                        " %u frames skipped as invalid | longest gap between pumps %u ms | queue target %u ms\n",
                        (now - lastLog) / 1000, s_StatDry, s_StatOverCap, s_StatSkipped, s_StatMaxGapMs,
                        s_Target * 1000 / (PORT_AUDIO_RATE * 4));
        } else if (s_Target > PORT_AUDIO_TARGET) {
            s_Target -= 256; /* a quiet window: ease back toward the base target */
        }
        lastLog = now;
        s_StatDry = 0;
        s_StatOverCap = 0;
        s_StatSkipped = 0;
        s_StatMaxGapMs = 0;
    }
}

/**
 * Size of the next audio frame: multiples of 16 samples averaging exactly one
 * field of output (367.5 at 60 Hz), so the paced frames keep step with the game.
 */
static u32 portAudioFrameSamples(void)
{
    static s32 s_Debt; /* samples owed, in 1/PORT_AUDIO_FIELD_HZ units */
    s32 size;

    s_Debt += PORT_AUDIO_RATE;
    size = ((s_Debt / PORT_AUDIO_FIELD_HZ) + 8) & ~15;
    s_Debt -= size * PORT_AUDIO_FIELD_HZ;

    return (u32) size;
}

/**
 * Track the lowest queue level the pumps see and, once per window, skip as many
 * field frames as it stayed above the target. Whatever the drift between the
 * device and game clocks, the queue then settles just above the target.
 */
static void portAudioTrim(u32 q)
{
    u32 now = SDL_GetTicks();

    if (!s_TroughPrimed) {
        s_TroughPrimed = 1;
        s_TroughTick = now;
        s_Trough = q;
    }
    if (q < s_Trough) {
        s_Trough = q;
    }
    if (now - s_TroughTick >= PORT_AUDIO_TRIM_MS) {
        if (s_Trough >= s_Target + PORT_AUDIO_FIELD_BYTES) {
            s_TrimFrames = (s32)((s_Trough - s_Target) / PORT_AUDIO_FIELD_BYTES);
        }
        s_TroughPrimed = 0;
    }
}

/**
 * Called once per video frame from the main loop (pcsched). Replaces the
 * N64 audio thread: builds and executes as many audio frames as needed to
 * keep the SDL queue ahead of the DAC.
 */
void portAudioPump(void)
{
    /* audi.c's RSP budget is MAX_ACMD_SIZE = 3000 Acmds per frame; a
     * music-dense frame really emits >1024 (the undersized first cut
     * overflowed into frameBuf - the stable float-bits-as-pointer
     * crashes). Margin on top of the N64 budget, x4 for the GE_SNDSCALE
     * pools (music.c: up to 4x the voices -> up to 4x the per-frame Acmds). */
    static Acmd cmdList[3072 * 4];
    static s16 frameBuf[PORT_MAX_FRAME_SAMPLES * 2];
    static s32 s_InPump;
    s32 guard;
    s32 byClock;
    s32 due = 0;

    /* REENTRANCY GUARD: portPumpEvents fires from inside blocking
     * osRecvMesg, and the AL client handlers (sndp, the CSP sequencers)
     * and romCopy paths can hit osRecvMesg mid-frame - without this the
     * pump re-enters alAudioFrame while the param pool and filter chains
     * are mid-mutation (the polymorphic garbage-pointer crashes when
     * music got busy). */
    if (s_InPump) {
        return;
    }
    s_InPump = 1;
    if (alGlobals == NULL || alGlobals->drvr.head == NULL) {
        s_InPump = 0;
        return; /* synthesizer not up yet (or no clients registered) */
    }
    /* A failed or closed device is retried every PORT_AUDIO_RETRY_MS or more,
     * so a sink that appears later (a headset, a restarted sound server) is used. */
    if (s_AudioDev == 0 && (s32)(SDL_GetTicks() - s_AudioRetryTick) >= 0) {
        portAudioInit();
    }

    /* No device, or one that stopped draining: the sound player and the
     * sequencers only advance inside alAudioFrame, so keep it running anyway. */
    byClock = s_AudioDev == 0 || portAudioStalled();
    if (!s_ForceOneFrame) {
        portAudioHealth(byClock);
    }

    if (!s_ForceOneFrame) {
        /* One audio frame per game field, keeping the N64 audio thread's lock
         * to the retrace at half its frame length. Topping up by queue level
         * alone made each sound's start follow the device's pull bursts
         * (murkantor/Project-Midas#51). */
        extern s32 currentFrameCounter;
        static s32 s_LastField;
        static s32 s_FieldPrimed;
        s32 fields;

        if (!s_FieldPrimed) {
            s_FieldPrimed = 1;
            s_LastField = currentFrameCounter;
        }
        fields = currentFrameCounter - s_LastField;
        s_LastField = currentFrameCounter;
        if (fields < 0 || fields > 16) {
            fields = 0; /* long stall: the queue floor below refills */
        }
        due = fields;
        if (byClock) {
            s_TroughPrimed = 0;
            s_TrimFrames = 0;
        } else {
            portAudioTrim(SDL_GetQueuedAudioSize(s_AudioDev));
        }
    }

    for (guard = 0; guard < 20; guard++) {
        s32 cmdLen = 0;
        u32 samples;

        if (byClock) {
            samples = g_FrameSize != 0 ? g_FrameSize : 368;
            if (samples > PORT_MAX_FRAME_SAMPLES) {
                samples = PORT_MAX_FRAME_SAMPLES;
            }
            if (!s_ForceOneFrame && !portAudioClockDue(samples)) {
                break;
            }
        } else {
            if (s_ForceOneFrame) {
                /* render regardless of the queue */
            } else if (SDL_GetQueuedAudioSize(s_AudioDev) < s_Target / 2) {
                /* below the floor: an extra frame ahead of the field frames */
            } else if (due > 0) {
                due--;
                if (s_TrimFrames > 0) {
                    s_TrimFrames--;
                    continue; /* the queue sat above the target: drop this one */
                }
            } else {
                break;
            }
            samples = g_FrameSize != 0 ? portAudioFrameSamples() : 368;
            if (samples > PORT_MAX_FRAME_SAMPLES) {
                samples = PORT_MAX_FRAME_SAMPLES;
            }
        }

        alAudioFrame(cmdList, &cmdLen, frameBuf, (s32)samples);
        if (cmdLen <= 0) {
            break; /* no clients / nothing to render */
        }
        {
            u32 skipped = s_StatSkipped;

            portAcmdRun(cmdList, cmdLen);
            if (s_StatSkipped != skipped) {
                /* nothing was written: play silence, not the previous frame again */
                memset(frameBuf, 0, samples * 4);
            }
        }

        { /* always-on: digital proof of synthesis - report the first frame
           * that contains a nonzero sample */
            static s32 heard;
            static u32 frames;

            frames++;
            if (!heard) {
                u32 i;

                for (i = 0; i < samples * 2; i++) {
                    if (frameBuf[i] != 0) {
                        heard = 1;
                        portConLogf("ge: audio: first nonzero sample %d after %u mixer frames (%d cmds)\n",
                                    (s32)frameBuf[i], frames, cmdLen);
                        break;
                    }
                }
            }
        }

        { /* [display] mute_unfocused: silence at the output, so the game's
           * own volumes and the saved ones are never touched */
            extern s32 portVideoMuteUnfocused(void);
            extern s32 portInputWindowFocused(void);
            static s32 s_WasMuted;
            const s32 mute = portVideoMuteUnfocused() && !portInputWindowFocused();

            if (mute) {
                memset(frameBuf, 0, samples * 4);
            }
            if (mute != s_WasMuted) {
                s_WasMuted = mute;
                portConLogf("ge: audio: %s\n", mute ? "muted (window lost focus)" : "unmuted");
            }
        }

        if (byClock) {
            portAudioClockSpend(samples);
        } else if (SDL_GetQueuedAudioSize(s_AudioDev) < PORT_AUDIO_QUEUE_MAX) {
            SDL_QueueAudio(s_AudioDev, frameBuf, samples * 4);
        } else {
            s_StatOverCap++;
        }
        if (s_ForceOneFrame) {
            break;
        }
    }

    s_QueuedAfterPump = s_AudioDev != 0 ? SDL_GetQueuedAudioSize(s_AudioDev) : 0;
    s_InPump = 0;
}

/**
 * Render exactly ONE audio frame regardless of queue level. music.c's
 * track-stop waits spin on alCSPGetState(), which only changes when the
 * sequencer client runs inside alAudioFrame - on N64 the audio thread
 * did that concurrently; on PC the waiting thread must drive it. Output
 * still respects the queue cap (over-cap frames render but don't queue).
 */
void portAudioPumpForce(void)
{
    extern void portAudioPump(void);

    s_ForceOneFrame = 1;
    portAudioPump();
    s_ForceOneFrame = 0;
}
