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
 *      frame from the main loop, it renders samples on a WALL-CLOCK
 *      schedule at PORT_AUDIO_RATE - alAudioFrame() -> portAcmdRun() ->
 *      SDL_QueueAudio() - and lets the device decide only how much of
 *      that is buffered, never whether the renderer runs at all.
 *
 *   3. portAudioInit(): SDL audio device setup, invoked lazily by the
 *      first pump after amCreateAudioManager has run alInit/alSynNew.
 *      A failure is logged and retried, never fatal: the game's audio
 *      clock (item 2) has to keep running with no device attached.
 *
 * Why the render pace is the playback queue when a device exists, and wall
 * time when it does not: the synthesizer's clock IS the game's audio clock.
 * alAudioFrame is the only thing that ever calls the AL client handlers, so
 * the SFX player's event queue, the sound-state list and all three music
 * sequencers are advanced exactly when it runs (synthesizer.c:189). Gating the
 * pump on the device - "no device, don't run" or "queue full, don't run" -
 * freezes game audio state and the SFX event queue fills until it drops
 * thousands of events. The silent-system reports look exactly like that: a
 * queue pinned at capacity, nothing voiced, and a heartbeat still alive.
 *
 * So neither source of pacing is allowed to STOP the renderer. Which one
 * DRIVES it is a latency question:
 *   - a live device is the authority, because it consumes at the true DAC rate
 *     and pacing to its queue keeps the backlog (and therefore the delay
 *     between an event and the sample reaching a speaker) at that queue's
 *     depth. Pacing to the wall clock instead would let the backlog drift up
 *     to PORT_AUDIO_QUEUE_MAX on any device slightly slower than real time;
 *   - with no device, or one that stopped draining, the wall clock drives so
 *     game audio time still advances and the output is simply discarded.
 *
 * Output latency is then the sum of three buffers, and all three are sized
 * deliberately small here: the queue floor below (top-up watermark), one
 * render quantum, and the SDL device period. At 22050 Hz stereo that is
 * ~35-52 ms, against ~63-103 ms with the original 4 KB watermark and one
 * full 736-sample field per render.
 *
 * Honest limits (bring-up quality, refine later):
 *   - A_RESAMPLE is linear interpolation (RSP uses a windowed table);
 *     slight high-frequency rolloff difference, inaudible for bring-up.
 *   - A_POLEF is a pass-through copy: the reverb wet path plays untinted
 *     (section filters off). Reverb timing/feedback still applies.
 *   - RAW16 wavetables would need a byteswap at A_LOADBUFF that is not
 *     detectable at that layer; both GE banks are 100% ADPCM (verified),
 *     so this never triggers. The bnkf dump would show type != 0.
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
#include "audio_pace.h"

/* printf is declared by hand rather than pulling <stdio.h>: PR/os.h declares
 * sprintf itself, which collides with the hosted header's fortified macros on
 * macOS, and this TU includes ultra64.h before any libc header. Same reason
 * src/snd.c and port/src/printf_port.c declare it locally. */
extern int printf(const char *, ...);

/* ---- config ------------------------------------------------------------ */

#define PORT_AUDIO_RATE      22050  /* both GE banks; alSynConfig outputRate */
/* With no device present nothing is heard, so it costs nothing to stop
 * handing it samples. Kept low so the fallback rate matches the DAC's. */
/* One render quantum is HALF a video field (g_FrameSize / 2): 368 samples at
 * 22050 Hz = 16.7 ms instead of 33.4 ms. A sound triggered between pumps waits
 * up to one quantum for its first sample to be rendered, so halving it halves
 * that term. Two quanta per pump at 147 Hz still cover the 2944 samples a field
 * needs, so throughput is unchanged - only the granularity is finer. */
#define PORT_AUDIO_FRAME_DIV 2
#define PORT_AUDIO_RETRY_MS  2000   /* open-failure retry cadence (ms)       */
/* A process that stalled (debugger, long load) must not replay minutes of
 * audio back-to-back when it resumes; catch-up is bounded per pump. */
#define PORT_AUDIO_MAX_CATCHUP_MS 100
#define PORT_MAX_FRAME_SAMPLES 1024

extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);

/* audi.c globals we drive directly (the amMain thread never runs) */
extern u32 g_FrameSize;

static SDL_AudioDeviceID s_AudioDev;
static u32 s_AudioFailCount; /* consecutive failed opens, for the log rate limit */
static u32 s_AudioStallReopens; /* stall-triggered reopens, for the log rate limit */
static u32 s_DevRetryAtMs;   /* SDL_GetTicks() of the next open attempt        */
static s32 s_ForceOneFrame;  /* portAudioPumpForce: render exactly one frame   */

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
        u32 w1 = list[c].words.w1;
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
            }
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
        u32 w1 = list[c].words.w1;
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

/**
 * Report a failed open. Rate-limited so a machine that will never have audio
 * cannot bury the rest of the log, but the first failure has to say WHY: the
 * previous version returned in total silence, and on Windows the game opens
 * no console, so a mute system produced a log that looked completely healthy.
 */
static void portAudioReportFail(const char *what)
{
    const char *drv = SDL_GetCurrentAudioDriver();

    s_AudioFailCount++;
    if (s_AudioFailCount == 1 || (s_AudioFailCount % 30) == 0) {
        printf("ge: audio: %s failed (SDL: %s; driver %s) - no output, attempt %u, "
               "retrying every %d ms\n",
               what, SDL_GetError(), drv != NULL ? drv : "none",
               (unsigned) s_AudioFailCount, PORT_AUDIO_RETRY_MS);
    }
}

/**
 * Open the SDL audio device. Returns 1 on success, 0 on failure (the caller
 * retries). The exact 22050 Hz s16 stereo request with no allowed changes is
 * what the synthesizer's fixed frame size requires (audi.c derives
 * g_FrameSize from outputRate), so a refusal is reported rather than
 * silently accepted at a rate the mixer was never sized for.
 */
static s32 portAudioInit(void)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        portAudioReportFail("SDL_InitSubSystem(SDL_INIT_AUDIO)");
        return 0;
    }

    memset(&want, 0, sizeof(want));
    want.freq = PORT_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = PORT_AUDIO_DEV_SAMPLES;

    s_AudioDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_AudioDev == 0) {
        portAudioReportFail("SDL_OpenAudioDevice(22050 Hz, s16, stereo)");
        return 0;
    }
    SDL_PauseAudioDevice(s_AudioDev, 0);
    s_AudioFailCount = 0;

    {
        const char *drv = SDL_GetCurrentAudioDriver();

        printf("ge: audio: device open - driver %s, %d Hz, %d ch, %d samples, "
               "%d byte buffer\n",
               drv != NULL ? drv : "none", (s32) have.freq, (s32) have.channels,
               (s32) have.samples, (s32) have.size);
    }
    return 1;
}

/* ---- render pacing policy ---------------------------------------------- */
/*
 * portAudioDrainTick / portAudioShouldRender / portAudioQuantum live in
 * port/src/audio_pace.c, with their constants and rationale in
 * port/include/audio_pace.h. They are pure (no SDL, no globals, no clock) so
 * port/tests/audio_pace.c can drive the shipped policy directly under
 * GE_PORT_TESTS instead of testing a reimplementation of it.
 */

/**
 * Called once per video frame from the main loop (pcsched). Replaces the N64
 * audio thread.
 *
 * Pacing is decided by portAudioShouldRender() above; this function only
 * gathers the observations and acts on the verdict. alAudioFrame is the only
 * thing that advances the SFX player, the sound-state list and the three music
 * sequencers (synthesizer.c:189), so it must run on schedule whether or not a
 * device is there to hear it.
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
    /* Audio owed to the wall clock, in samples x1000, so a 147 Hz pump's
     * fractional share of a frame cannot round to zero and stop the render.
     * Only meaningful when the clock is the pacing authority (no device, or a
     * stalled one); it is held at zero while a live device drives. */
    static u64 s_ClockOwed;
    static u32 s_ClockLastMs;
    static s32 s_ClockPrimed;
    static PortAudioDrain s_Drain;
    s32 devLive = 0;    /* device exists and its queue is draining */
    s32 devStalled = 0; /* device exists but its queue is stuck */
    u32 samples = portAudioQuantum(g_FrameSize, PORT_MAX_FRAME_SAMPLES, PORT_AUDIO_FRAME_DIV);
    u64 frame = (u64) samples * 1000;
    s32 guard;

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

    /* Device setup is a side quest: try, record the failure, try again later.
     * The sink can appear or move under us at any point - a headset plugged
     * in, PipeWire restarted, a 32-bit client stack that was not up at boot. */
    if (s_AudioDev == 0 && (s32) (SDL_GetTicks() - s_DevRetryAtMs) >= 0) {
        if (!portAudioInit()) {
            s_DevRetryAtMs = SDL_GetTicks() + PORT_AUDIO_RETRY_MS;
        }
    }

    /* Is the device consuming? A sink that opened and then went quiet holds a
     * queue that never drains, so it is no longer a valid pacing authority.
     * portAudioDrainTick() measures real consumption (bytes in at the end of
     * the last pump, minus bytes out now) rather than "the number changed",
     * because at 147 Hz the pump runs far more often than an SDL period and a
     * healthy queue legitimately sits still for a pump or two between drains. */
    if (s_AudioDev != 0) {
        u32 queued = SDL_GetQueuedAudioSize(s_AudioDev);

        devLive = portAudioDrainTick(&s_Drain, queued);
        if (!devLive) {
            devStalled = 1;
            if (s_Drain.famished >= PORT_AUDIO_STALL_PUMPS) {
                /* Same rate limit as a failed open: a sink that opens and
                 * never consumes would otherwise log once a second. */
                s_AudioStallReopens++;
                if (s_AudioStallReopens == 1 || (s_AudioStallReopens % 30) == 0) {
                    printf("ge: audio: device stalled (%u bytes queued, no drain for %u pumps) - "
                           "reopening (stall #%u)\n",
                           (unsigned) queued, (unsigned) s_Drain.famished,
                           (unsigned) s_AudioStallReopens);
                }
                SDL_CloseAudioDevice(s_AudioDev);
                s_AudioDev = 0;
                s_AudioFailCount = 0;
                s_DevRetryAtMs = 0;
                s_Drain.at_exit = 0;
                s_Drain.famished = 0;
                devLive = 0;
                devStalled = 0;
            }
        }
    }

    /* Advance the wall clock. Still tracked while a device is live - the
     * elapsed time is real either way - but the owed backlog is only carried
     * when the clock is actually pacing, so handing over from a live device to
     * the fallback starts from zero instead of dumping a stale backlog. */
    {
        u32 now = SDL_GetTicks();
        u32 elapsed;

        if (!s_ClockPrimed) {
            s_ClockPrimed = 1;
            s_ClockLastMs = now;
            elapsed = 0;
        } else {
            elapsed = now - s_ClockLastMs; /* u32 delta: exact across the wrap */
            s_ClockLastMs = now;
        }
        if (elapsed > PORT_AUDIO_MAX_CATCHUP_MS) {
            elapsed = PORT_AUDIO_MAX_CATCHUP_MS;
        }
        if (devLive) {
            s_ClockOwed = 0; /* the device is the authority */
        } else {
            s_ClockOwed += (u64) elapsed * PORT_AUDIO_RATE;
        }
    }

    for (guard = 0; guard < 8; guard++) {
        PortAudioPace pace;
        s32 cmdLen = 0;
        u32 queued;

        /* Gather the observation, ask the policy, obey it. No branch here can
         * be blocked by the device: at worst it hands pacing to the clock. */
        queued = (s_AudioDev != 0) ? SDL_GetQueuedAudioSize(s_AudioDev) : 0;
        pace.dev_live = devLive;
        pace.queued = queued;
        pace.owed = s_ClockOwed;
        pace.frame = frame;
        if (!s_ForceOneFrame && !portAudioShouldRender(&pace)) {
            break;
        }

        alAudioFrame(cmdList, &cmdLen, frameBuf, (s32)samples);
        if (cmdLen <= 0) {
            break; /* no clients / nothing to render */
        }
        portAcmdRun(cmdList, cmdLen);
        if (s_ClockOwed > frame) {
            s_ClockOwed -= frame;
        } else {
            s_ClockOwed = 0;
        }

        { /* bring-up (GE_DUMP_BOND): digital proof of synthesis - report
           * the first frame that contains a nonzero sample */
            static s32 heard;

            if (!heard) {
                u32 i;

                for (i = 0; i < samples * 2; i++) {
                    if (frameBuf[i] != 0) {
                        f32 v[2];

                        heard = 1;
                        v[0] = (f32)frameBuf[i];
                        v[1] = (f32)cmdLen;
                        ge_dbg_vec("audio: FIRST NONZERO sample/cmds", v, 2);
                        break;
                    }
                }
            }
        }

        /* Only a draining device gets fed. A stalled one is already pinned at
         * or above the watermark, so queueing would just deepen a backlog
         * nobody is consuming; it is closed and reopened by the stall path
         * above within ~1 s. */
        if (s_AudioDev != 0 && !devStalled && queued < PORT_AUDIO_QUEUE_MAX) {
            SDL_QueueAudio(s_AudioDev, frameBuf, samples * 4);
        }

        if (s_ForceOneFrame) {
            break; /* portAudioPumpForce renders exactly one frame */
        }
    }

    /* Record what was left for the device to chew on, so the next pump can
     * measure real consumption instead of guessing from a changed byte count. */
    if (s_AudioDev != 0) {
        s_Drain.at_exit = SDL_GetQueuedAudioSize(s_AudioDev);
    }

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
