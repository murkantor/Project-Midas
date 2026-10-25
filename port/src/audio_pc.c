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
 *      frame from the main loop: alAudioFrame() -> portAcmdRun() ->
 *      SDL_QueueAudio().
 *
 *      PACING POLICY (see audio_pace.h for the tested rules, and
 *      docs/port/audio-pump-history.md for what came before):
 *
 *      - WALL TIME is the sole authority for how much to synthesise.
 *        alAudioFrame is the only thing that advances the game's audio
 *        clock (the sound player and the three music sequencers move in
 *        rendered samples), so audio time must track real time whatever
 *        the render loop, the tick rate or the device are doing. Keying
 *        this off the device's queue makes music drag on a slow sink;
 *        keying it off the simulation's field counter starves the sink
 *        when the game loop stalls, and every stall is a click.
 *      - THE QUEUE sets the backlog, and only reshapes delivery within
 *        the clock's budget: a few samples per frame of bounded bias
 *        toward a 40 ms target, charged against the clock so the
 *        long-run rate stays wall time. A frame is never dropped or
 *        duplicated to correct depth - dropping a rendered frame throws
 *        away an audible discontinuity, which is what 1.0.2b's trim did.
 *      - A STALLED DEVICE forfeits the pace. The clock keeps running and
 *        the samples are discarded, then it is reopened.
 *
 *   3. portAudioPumpForce(): one frame regardless of the clock, for
 *      music.c's track-stop spins, which wait on state that only moves
 *      inside alAudioFrame. It advances the sequencer and costs the
 *      clock, but queues only into a shallow queue: it is a scheduling
 *      device, not audio to be played.
 *
 *   4. portAudioInit(): SDL audio device setup, invoked lazily by the
 *      first pump after amCreateAudioManager has run alInit/alSynNew.
 *      A failed open, or a device closed after it stalled, is retried
 *      every 2 s or more while the mixer runs on the wall clock.
 *
 *      KNOWN LIMITATION, next change: the device is opened at an exact
 *      22050 Hz with allowed_changes = 0. Windows shared mode mixes at
 *      48000, so the sink is then resampled by the OS or by a fallback
 *      driver, and drift between our rate and the DAC's has nowhere to
 *      go but the queue. An SDL_AudioStream (22050 -> device rate) would
 *      absorb both. Scoped in docs/port/rate_prompt.md; do it as its own
 *      change so a crackle report can tell the two fixes apart.
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

#include <stdio.h>
#include <time.h>   /* the log's start stamp only; the pump itself never reads a clock */

#include <ultra64.h>
#include <PR/libaudio.h>
#include <PR/abi.h>

#include "audio_pace.h"
#include "port_audio_stats.h"

/* ---- config ------------------------------------------------------------ */

#define PORT_AUDIO_RATE      22050  /* both GE banks; alSynConfig outputRate */
#define PORT_AUDIO_DEV_SAMPLES 256       /* device period, 11.6 ms */
#define PORT_AUDIO_QUEUE_MAX (16 * 1024)
/* Queue depth held in steady state: the latency/safety trade, nothing else.
 * 40 ms is ~3.5 device periods of cushion - enough that a hitchy Windows
 * shared-mode mixer cannot starve it, small enough to stay inaudible on a
 * reload. Tighten only with a device that has proven it never underruns. */
#define PORT_AUDIO_TARGET_MS 40
#define PORT_AUDIO_TARGET    (PORT_AUDIO_RATE * 4 / 1000 * PORT_AUDIO_TARGET_MS)
#define PORT_AUDIO_TARGET_MAX (10 * 1024) /* raised this far by queues that ran dry */
#define PORT_AUDIO_TARGET_STEP 1024       /* per underrun, within the ceiling above */
#define PORT_AUDIO_TARGET_EASE 256        /* per quiet 10 s window, back toward TARGET */
#define PORT_AUDIO_CATCHUP_MS 250         /* wall time one pump may credit */
#define PORT_AUDIO_DEADBAND  512          /* queue error not worth correcting */
#define PORT_AUDIO_DRIFT_MAX 32           /* bias authority per frame, samples */
#define PORT_AUDIO_DRAIN_MS  2000         /* no consumption this long = stalled */
#define PORT_AUDIO_DEV_PERIOD_MS 12       /* cushion over one pump interval */
/* Longest pump interval the queue is asked to bridge. Single-digit frame rates
 * are real (classic/demo modes), so cover down to 4 fps; a longer gap is a
 * hitch, not a frame rate, and a hitch is allowed to click rather than demand a
 * backlog nobody would tolerate. */
/* Longest pump interval the queue is asked to bridge. Single-digit frame rates
 * are real (classic and demo modes), so this covers down to 1 fps. Beyond that
 * the gap is a hitch, not a frame rate: audio cannot be synthesised for time
 * that has not been pumped, and a 1 s backlog is already the point where
 * latency stops being worth buying.
 *
 * THE COST, stated plainly: at N fps the pump runs N times a second, so each
 * pump must bridge 1000/N ms of audio, and that backlog IS the audio latency.
 * At 60 fps it is 17 ms and inaudible; at 2 fps it is 500 ms and the music runs
 * half a second behind. That is inherent to pumping from the render loop - the
 * only way out is a separate audio thread, which the AL library's shared state
 * makes a much larger change. Continuous audio at high latency beats crackle at
 * low latency, so that is the trade taken here. */
#define PORT_AUDIO_MAX_INTERVAL_MS 1000
#define PORT_AUDIO_ALIGN     16           /* alAudioFrame rounds to this */
#define PORT_MAX_FRAME_SAMPLES 1024
/* Enough render calls to fill the interval floor at the longest supported pump
 * interval, with margin: 1000 ms at 22050 Hz is 22050 samples, 22 calls of
 * 1024. A fixed 16 silently under-filled below ~1.5 fps. */
#define PORT_AUDIO_MAX_FRAMES_PER_PUMP 24
#define PORT_AUDIO_RETRY_MS  2000   /* open again this long after a failure */
#define PORT_AUDIO_RETRY_MAX_MS 30000

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

/* Live snapshot for the in-game overlay (console cl_audio_pump). The
 * instantaneous fields are refreshed on every pump; the rates are accumulated
 * and published once per PORT_AUDIO_STATS_WINDOW_MS. Everything here is
 * observation only - nothing in the pacing policy reads it. */
static s32 s_StatsShown;
/* The periodic log file (console cl_audio_log). One line a second is about
 * 175 bytes, so roughly 10 KB a minute: enough history for a crackle report
 * without filling a disk during a long session. Flushed per line so a crash
 * keeps the tail, which is when it is worth having. */
#define PORT_AUDIO_LOG_MS 1000
static s32 s_LogShown;
static FILE *s_LogFile;
static u32 s_LogTick;
static u32 s_LogStart;
static struct PortAudioPumpStats s_Stats;
static u32 s_StatWinTick;    /* start of the window being accumulated */
static u32 s_StatWinPumps;
static u32 s_StatWinBusy;      /* of those pumps, how many rendered samples */
static u32 s_StatWinRendered;
static u32 s_StatWinDry;
static u32 s_StatWinOverCap;
static u32 s_StatWinSkipped;
static u32 s_StatWinForced;
static s32 s_StatWinPrimed;  /* restart the window cleanly after being off */

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

/* Pacing state. The rules live in audio_pace.c as pure functions; this is
 * only the bookkeeping they need. */
static PortAudioPaceClock s_Clock;
static PortAudioPaceTarget s_TargetState;
static PortAudioPaceDrain s_Drain;
static const PortAudioPaceCfg s_PaceCfg = {
    PORT_AUDIO_RATE,
    PORT_AUDIO_ALIGN,
    PORT_MAX_FRAME_SAMPLES,
    PORT_AUDIO_TARGET,
    PORT_AUDIO_TARGET_MAX,
    PORT_AUDIO_TARGET_STEP,
    PORT_AUDIO_TARGET_EASE,
    PORT_AUDIO_CATCHUP_MS,
    PORT_AUDIO_DRAIN_MS,
    PORT_AUDIO_DEV_PERIOD_MS,
    PORT_AUDIO_QUEUE_MAX,
    PORT_AUDIO_MAX_INTERVAL_MS
};

static u32 s_ClockTick;      /* SDL_GetTicks() of the previous pump */
static s32 s_ClockPrimed;
static u32 s_QueuedAtOpen;   /* queue depth when this pump began */
static s32 s_Stalled;        /* the drain watchdog's current verdict */
static u32 s_StalledSince;   /* when it became stalled, for the reopen */
static s32 s_RanDry;         /* this pump found the queue empty */
static s32 s_SinkPrimed;     /* the current device has had audio queued once */
static u32 s_LastHealthTick;

#define PORT_AUDIO_REOPEN_MS 2000 /* stalled this much longer: close and reopen */

/**
 * True while the open device has consumed nothing for PORT_AUDIO_DRAIN_MS. A
 * stalled sink never drains the queue, so the pump's queue bias would stop
 * refilling it and the game's audio clock would stall behind it. The verdict
 * comes from audio_pace.c's consumption measurement; this only handles the
 * consequence: after PORT_AUDIO_REOPEN_MS more, close it and let the retry path
 * open whatever is there now.
 */
static s32 portAudioStalled(u32 now)
{
    u32 queued = SDL_GetQueuedAudioSize(s_AudioDev);

    if (!s_Stalled) {
        s_StalledSince = now;
    }
    if (s_Stalled && now - s_StalledSince >= PORT_AUDIO_REOPEN_MS) {
        s_AudioReopens++;
        if (portAudioLogDue(s_AudioReopens)) {
            portConLogf("ge: audio: closing the stalled device to reopen it (reopen %u)\n", s_AudioReopens);
        }
        portAudioClose();
        s_AudioRetryTick = now; /* the next pump opens a new one */
        s_Drain.primed = 0;
        s_QueuedAtOpen = 0;
        s_SinkPrimed = 0;
        s_Stalled = 0; /* the clock keeps running: there is no device now */
        return 1;
    }
    (void) queued;
    return s_Stalled;
}

/**
 * Count pumps that arrive to an empty queue (the device ran out and played
 * silence) and log the counters every ~10 s, so a crackle report says whether
 * the output starved. This is the first thing to read on a choppy-audio report:
 * a high dry count means the pump starved, and its absence means the fault is
 * below the pump (the device layer).
 */
static void portAudioHealth(void)
{
    static u32 lastPump;
    static s32 primed;
    u32 now = SDL_GetTicks();

    if (!primed) {
        primed = 1;
        lastPump = now;
        s_LastHealthTick = now;
        return;
    }
    if (now - lastPump > s_StatMaxGapMs) {
        s_StatMaxGapMs = now - lastPump;
    }
    lastPump = now;

    if (now - s_LastHealthTick < 10000) {
        return;
    }
    {
        const s32 had_trouble = (s_StatDry != 0 || s_StatOverCap != 0 || s_StatSkipped != 0);

        if (had_trouble) {
            portConLogf("ge: audio: last %u s: queue ran dry %u times, %u frames over the cap dropped,"
                        " %u frames skipped as invalid | longest gap between pumps %u ms | queue target %u ms\n",
                        (now - s_LastHealthTick) / 1000, s_StatDry, s_StatOverCap, s_StatSkipped,
                        s_StatMaxGapMs, s_TargetState.target * 1000 / (PORT_AUDIO_RATE * 4));
        }
        /* A clean window lets the ratcheted target ease back toward the floor. */
        s_TargetState.target = portAudioPaceTargetStep(&s_TargetState, 0, !had_trouble, &s_PaceCfg);
    }
    s_LastHealthTick = now;
    s_StatDry = 0;
    s_StatOverCap = 0;
    s_StatSkipped = 0;
    s_StatMaxGapMs = 0;
}

/* ---- live stats for the in-game overlay (console cl_audio_pump) ----- */

s32 portAudioPumpStatsShown(void)
{
    return s_StatsShown;
}

void portAudioPumpStatsSet(s32 on)
{
    s_StatsShown = (on != 0) ? 1 : 0;
}

void portAudioPumpStatsGet(struct PortAudioPumpStats *out)
{
    if (out != NULL) {
        *out = s_Stats;
    }
}

s32 portAudioPumpLogShown(void)
{
    return s_LogShown;
}

/* Opens the log and writes its legend. Returns 0 if the file could not be made,
 * so the console can say so instead of reporting a log that writes nowhere. */
static s32 portAudioLogOpen(void)
{
    extern const char *portLogPath(const char *name); /* main.c: Data/Logs/<name> */

    if (s_LogFile != NULL) {
        return 1;
    }
    s_LogFile = fopen(portLogPath("audio_pump.log"), "w");
    if (s_LogFile == NULL) {
        portConLogf("ge: audio: cannot write Data/Logs/audio_pump.log; logging stays off\n");
        return 0;
    }
    fprintf(s_LogFile,
            "# Project Midas audio pump log (console: cl_audio_log 1)\n"
            "# One line per second. Fields:\n"
            "#   t      ms since logging started\n"
            "#   FPS    presented frames per second\n"
            "#   tk     simulation ticks per second - how many times the game\n"
            "#          ticked, including ticks that advanced no game time\n"
            "#          (paused, controls locked), which is why f/s is the number\n"
            "#          to read for game speed. A tick is taken only once the\n"
            "#          previous frame has been presented, so this tracks FPS\n"
            "#          whenever drawing is the limit: held below the configured\n"
            "#          rate by the display (vsync caps ticks at the refresh rate),\n"
            "#          by a renderer that cannot keep up, or deliberately by\n"
            "#          Hardware pacing, whose hold prices each frame at N fields.\n"
            "#   f/s    60Hz fields credited to the sim per second. 60.0 means\n"
            "#          simulated time is keeping up; a few percent either side is\n"
            "#          window jitter, not drift. Below that the sim is losing\n"
            "#          wall-clock time (a truncated stall, GE_TICKMAXSTEP). Near\n"
            "#          0 (under about 2.0) while paused, controls locked, or\n"
            "#          during a stage load.\n"
            "#   f/t    fields credited per tick, x100: 100 = 1.00 = one field per\n"
            "#          tick, above when a frame overran a field (the console's\n"
            "#          own pacing), below when a raised tick rate sub-ticks.\n"
            "#          0 = no game time credited in the window; read f/s. tk can\n"
            "#          read 60 beside a 0 here (paused ticks still tick).\n"
            "#   FPS, tk, f/s and f/t are sampled once per drawn frame, so they\n"
            "#   repeat their last value while nothing is drawn (a stage load, a\n"
            "#   long stall) - a still frame, not a measurement. The pump columns\n"
            "#   below keep updating throughout.\n"
            "#   pmp/s  pump calls per second. This is the GAME LOOP rate, not the\n"
            "#          frame rate: the pump runs once per loop pass and the loop\n"
            "#          passes far more often than it draws. It sits near 60/s only\n"
            "#          while the loop is blocked (a load, the intro, vsync stalling\n"
            "#          the swap) and can reach six figures in free-running play.\n"
            "#   rnd/s  of those calls, the ones that rendered any samples\n"
            "#   que    bytes queued / the target depth it is steered toward,\n"
            "#          each also given in ms of audio: that pair IS the latency\n"
            "#   cap    backlog ceiling in bytes\n"
            "#   snd    audio rendered / the wall time it was worth, as raw sample\n"
            "#          counts AND ms. The samples are the exact figures - at\n"
            "#          22050 Hz one sample is 0.045 ms, so ms alone rounds away a\n"
            "#          slow leak. The ms pair is for reading at a glance.\n"
            "#   dry    pumps that found the sink empty and played silence\n"
            "#   over   frames refused because they would not fit under cap\n"
            "#   skip   frames the RSP validator rejected (played as silence)\n"
            "#   frc    forced frames (music track-stop scheduling)\n"
            "#   stall  1 = the device has consumed nothing for 2 s\n"
            "#   clk    wall-clock credit, ms of audio; 0 when a device paces\n"
            "#   dev    device sample rate in Hz, 0 when no device is open\n");
    /* The start stamp is what makes this file correlatable: nothing else the
     * game logs carries a wall clock, so the session's absolute start plus the
     * relative t= on each line is what ties a line to a crash.log mtime, a
     * ge: stats line, or a user's "it happened around 21:40". t= stays
     * relative and monotonic, because a clock step must not reorder the run. */
    {
        const time_t now_wall = time(NULL);
        const struct tm *tmv = localtime(&now_wall);

        fprintf(s_LogFile, "# started %04d-%02d-%02d %02d:%02d:%02d (local);"
                           " t= below is ms since this instant\n",
                tmv ? tmv->tm_year + 1900 : 0, tmv ? tmv->tm_mon + 1 : 0,
                tmv ? tmv->tm_mday : 0, tmv ? tmv->tm_hour : 0,
                tmv ? tmv->tm_min : 0, tmv ? tmv->tm_sec : 0);
    }
    fflush(s_LogFile);
    return 1;
}

void portAudioPumpLogSet(s32 on)
{
    on = (on != 0) ? 1 : 0;
    if (on == s_LogShown) {
        return;
    }
    if (on) {
        /* Only claim to be logging if the file really opened. */
        if (!portAudioLogOpen()) {
            return;
        }
        /* The file was truncated, so the timestamp starts again at zero. */
        s_LogStart = 0;
        s_LogTick = 0;
    } else if (s_LogFile != NULL) {
        fprintf(s_LogFile, "# logging stopped by cl_audio_log 0\n");
        fclose(s_LogFile);
        s_LogFile = NULL;
    }
    s_LogShown = on;
}

/* One line for the current window. Called from the pump; the stats it reads are
 * the same ones the overlay draws, so the file and the screen cannot disagree. */
static void portAudioLogLine(u32 now)
{
    const struct PortAudioPumpStats *st = &s_Stats;
    u32 pps, rps, snd_ms, need_ms, que_ms, target_ms;

    if (!s_LogShown || s_LogFile == NULL || st->rate == 0) {
        return;
    }
    if (s_LogStart == 0) {
        s_LogStart = now;
        s_LogTick = now;
        return; /* first line lands one interval after logging starts */
    }
    if (now - s_LogTick < PORT_AUDIO_LOG_MS) {
        return;
    }
    s_LogTick = now;

    pps = (st->window_ms != 0)
        ? (u32) (((unsigned long long) st->pumps * 1000u) / st->window_ms) : 0;
    rps = (st->window_ms != 0)
        ? (u32) (((unsigned long long) st->busy * 1000u) / st->window_ms) : 0;
    snd_ms = (u32) (((unsigned long long) st->rendered * 1000u + st->rate / 2u) / st->rate);
    need_ms = (u32) (((unsigned long long) st->needed * 1000u + st->rate / 2u) / st->rate);
    /* Bytes to ms of audio at this rate: 22050 stereo s16 is 88200 B/s, so
     * 3556 B is 40 ms. This pair is the latency a listener actually hears.
     * Rounded, not truncated: 3520 B is 39.9 ms and printing that as 39 beside
     * a 40 makes an on-target queue look short by a millisecond. */
    que_ms = (u32) (((unsigned long long) st->queue * 1000u + (unsigned long long) st->rate * 2u)
                    / ((unsigned long long) st->rate * 4u));
    target_ms = (u32) (((unsigned long long) st->target * 1000u + (unsigned long long) st->rate * 2u)
                    / ((unsigned long long) st->rate * 4u));

    {
        extern s32 g_PortStatFps; /* frametiming.c, the same numbers the block uses */
        extern s32 g_PortStatTicks;
        extern s32 g_PortStatFieldsX10;
        extern s32 g_PortStatFieldsPerTickX100;

        /* Both units on purpose. The screen shows one each, because a glance
         * needs one answer; this file is read by someone chasing a defect, so it
         * carries the exact sample counts as well. */
        fprintf(s_LogFile,
                "t=%ums FPS %d tk %d f/s %d.%d f/t %d.%02d "
                "pmp %u/s rnd %u/s"
                " que %u/%uB %u/%ums cap %uB"
                " snd %u/%usmp %u/%ums"
                " dry %u over %u skip %u frc %u stall %d clk %dms dev %uHz\n",
                now - s_LogStart, (int) g_PortStatFps,
                (int) g_PortStatTicks,
                (int) (g_PortStatFieldsX10 / 10), (int) (g_PortStatFieldsX10 % 10),
                (int) (g_PortStatFieldsPerTickX100 / 100),
                (int) (g_PortStatFieldsPerTickX100 % 100),
                pps, rps,
                st->queue, st->target, que_ms, target_ms, st->cap,
                st->rendered, st->needed, snd_ms, need_ms,
                st->dry, st->overcap, st->skipped, st->forced,
                (int) st->stalled, (int) (st->credit / (s32) st->rate),
                st->has_sink ? st->rate : 0u);
    }
    fflush(s_LogFile);
}

/**
 * Size of the next audio frame when the mixer must advance outside the paced
 * path (a forced frame). One field of output, aligned like the vanilla
 * g_FrameSize: 736 at 60 Hz, the same number the N64 audio thread used.
 */
static u32 portAudioFieldSamples(void)
{
    u32 samples = g_FrameSize != 0 ? g_FrameSize : 368;

    if (samples > PORT_MAX_FRAME_SAMPLES) {
        samples = PORT_MAX_FRAME_SAMPLES;
    }
    return samples;
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
    u32 now;
    u32 dt;
    u32 queued;
    s32 has_sink;
    s32 by_clock;
    s32 guard;
    u32 target;
    u32 queue_max;
    u32 rendered = 0;
    u32 span_ms = 0;
    s32 dry_now = 0;      /* this pump starved the sink (overlay stats) */
    s32 over_cap_now = 0; /* this pump refused a frame at the cap */
    s32 skipped_now = 0;  /* this pump played a rejected frame as silence */

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
    now = SDL_GetTicks();
    if (s_AudioDev == 0 && (s32)(now - s_AudioRetryTick) >= 0) {
        portAudioInit();
        now = SDL_GetTicks();
    }

    /* How long since the previous pump. This is used two ways and it is worth
     * keeping them apart, because conflating them is what broke 1.0.2b:
     *
     *   - With a live device it only sets the FLOOR on the target (a queue
     *     shallower than one pump interval starves by construction). The rate
     *     itself comes from the device, via the queue deficit.
     *   - With no usable device it sets the rate, so game audio time still
     *     tracks real time while the samples are discarded.
     *
     * 1.0.2b instead took the RENDER COUNT from the simulation's field counter,
     * which starved the sink whenever the game loop stalled and made the music
     * drag. Do not reintroduce that. */
    if (!s_ClockPrimed) {
        s_ClockPrimed = 1;
        s_ClockTick = now;
        portAudioPaceClockReset(&s_Clock);
    }
    dt = now - s_ClockTick;
    s_ClockTick = now;
    span_ms = dt;

    has_sink = s_AudioDev != 0;
    queued = has_sink ? SDL_GetQueuedAudioSize(s_AudioDev) : 0;
    s_QueuedAtOpen = queued;

    /* A target below one pump interval cannot work: between two pumps the device
     * drains dt of audio and the refill stops at the target, so a shallower
     * queue starves every pump. Measured at 20 fps with a 40 ms target, the sink
     * ran dry every single pump. The effective target is therefore the interval
     * floor plus the headroom the underrun ratchet has learned - NOT the larger
     * of the two, which made the ratchet inert below ~8.6 fps (audio_pace.h,
     * portAudioPaceEffectiveTarget). */
    target = portAudioPaceEffectiveTarget(portAudioPaceMinTarget(dt, &s_PaceCfg),
                                          s_TargetState.target, &s_PaceCfg);

    /* And the cap has to follow: at 5 fps one interval (200 ms) is already more
     * than a fixed 16 KB cap (185 ms), so a single-digit frame rate would starve
     * no matter how the target is set. Single-digit fps is real in classic and
     * demo modes. */
    queue_max = portAudioPaceCap(target, &s_PaceCfg);

    /* A device that has stopped consuming cannot set the pace, so the clock
     * takes over and the samples are discarded until it returns. This uses the
     * previous pump's verdict: the watchdog needs an interval to measure. */
    by_clock = !has_sink || s_Stalled;
    if (!s_ForceOneFrame) {
        portAudioHealth();

        /* Ran dry with a live device: the sink played silence, so keep more
         * queued on this machine. The target ratchets up here and eases back
         * in portAudioHealth's quiet window. A device that has just been opened
         * starts empty, which is not an underrun - only a sink that has carried
         * audio before can prove it starved. */
        if (queued > 0) {
            s_SinkPrimed = 1;
        }
        s_RanDry = has_sink && !by_clock && queued == 0 && s_SinkPrimed;
        dry_now = s_RanDry;
        if (s_RanDry) {
            s_StatDry++;
        }
        s_TargetState.target = portAudioPaceTargetStep(&s_TargetState, s_RanDry, 0, &s_PaceCfg);
    }

    for (guard = 0; guard < PORT_AUDIO_MAX_FRAMES_PER_PUMP; guard++) {
        s32 cmdLen = 0;
        u32 samples;

        if (s_ForceOneFrame) {
            /* A forced frame exists to advance the sequencer, not to deliver
             * this many samples: music.c's track-stop spins wait on
             * alCSPGetState(), which only moves inside alAudioFrame. */
            samples = portAudioFieldSamples();
        } else {
            samples = portAudioPaceFrame(&s_Clock, dt, queued, target,
                                         has_sink && !by_clock, &s_PaceCfg);
            dt = 0; /* the pump's interval is credited once */
            if (samples == 0) {
                break;
            }
        }

        /* Refuse BEFORE synthesising. alAudioFrame advances the game's audio
         * clock, so rendering a frame we cannot queue is not free: it both
         * burns the time and leaves the clock ahead of what was delivered.
         * Checked here rather than after the call, which is where it used to be
         * and where it could not prevent the very drop it described. */
        if (has_sink && !by_clock && !s_ForceOneFrame &&
            queued + samples * 4 > queue_max) {
            s_StatOverCap++;
            over_cap_now = 1;
            break;
        }

        alAudioFrame(cmdList, &cmdLen, frameBuf, (s32)samples);
        rendered += samples;
        if (cmdLen <= 0) {
            break; /* no clients / nothing to render */
        }
        {
            u32 skipped = s_StatSkipped;

            portAcmdRun(cmdList, cmdLen);
            if (s_StatSkipped != skipped) {
                /* nothing was written: play silence, not the previous frame again */
                memset(frameBuf, 0, samples * 4);
                skipped_now = 1;
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

        if (s_ForceOneFrame) {
            /* Deliver a forced frame only into a queue that is genuinely
             * shallow. A fast track-stop spin used to queue seconds of audio
             * into a 186 ms buffer: the excess was dropped, and the trim then
             * skipped field frames to recover - the intro's crackle.
             *
             * A forced frame does NOT charge the clock. Charging it was tried
             * and measured starved the sink: the title screen forces ~100
             * frames a minute during music kills, and paying audio time for
             * samples that are then discarded leaves the clock in debt, so the
             * normal pumps stop rendering until it is repaid and the queue runs
             * dry (21 dry pumps and a 116 ms ratcheted target in a 10 s sample).
             *
             * KNOWN LIMITATION: not charging means these frames advance the
             * sequencer's musical time without delivering it when the queue is
             * not shallow, so a burst can push the playing tracks slightly ahead
             * of wall time. 1.0.2b is not a defence here: it queued forced
             * frames whenever the queue was below its cap, so it had the
             * opposite problem (flooding, then dropping at the cap). Both are
             * bounded by the stop itself; bounding it properly means making
             * music.c's spins wall-clock paced, which is its own change. */
            if (has_sink && !by_clock && queued < target / 2 &&
                queued + samples * 4 <= queue_max) {
                SDL_QueueAudio(s_AudioDev, frameBuf, samples * 4);
                queued += samples * 4;
            }
            break;
        }

        if (has_sink && !by_clock) {
            SDL_QueueAudio(s_AudioDev, frameBuf, samples * 4);
            queued += samples * 4;
        }
        /* With no usable sink the frame is rendered and discarded: its clock
         * cost was already charged by portAudioPaceFrame, so game audio time
         * still advances. */
    }

    /* Watchdog: has the device consumed anything between the last pump closing
     * and this one opening? That interval is the only one in which just the
     * device ran, so a decrease there is a clean measurement. */
    if (has_sink) {
        const s32 was_stalled = s_Stalled;

        s_Stalled = portAudioPaceDrainObserve(&s_Drain, s_QueuedAtOpen, queued, now, &s_PaceCfg);
        if (s_Stalled && !was_stalled) {
            s_StalledSince = now;
            if (portAudioLogDue(s_AudioReopens + 1)) {
                portConLogf("ge: audio: WARNING - the device has played nothing for %d ms (%u bytes queued);"
                            " running the mixer without it\n", PORT_AUDIO_DRAIN_MS, queued);
            }
        } else if (!s_Stalled && was_stalled) {
            portConLogf("ge: audio: the device is consuming audio again\n");
        }
        (void) portAudioStalled(now); /* reopen it if it stays dead */
    }

    /* Console cl_audio_pump: keep the overlay's snapshot current. The
     * instantaneous fields are written every pump so the block moves with the
     * game; the rates are published once a window so they are readable rather
     * than flickering. Both are observation only - nothing below feeds back
     * into the pacing decisions above. */
    s_Stats.queue = queued;
    s_Stats.target = target;
    s_Stats.cap = queue_max;
    s_Stats.rate = PORT_AUDIO_RATE;
    s_Stats.stalled = s_Stalled;
    s_Stats.has_sink = has_sink;
    s_Stats.credit = (s32) s_Clock.credit;

    /* Either consumer needs the window accumulated: the overlay to draw the
     * rates, the log to write them. Tying this to the overlay alone made
     * `cl_audio_log 1` on its own write a file full of zeroes. */
    if (s_StatsShown || s_LogShown) {
        if (!s_StatWinPrimed) {
            s_StatWinPrimed = 1;
            s_StatWinTick = now;
            s_StatWinPumps = 0;
            s_StatWinBusy = 0;
            s_StatWinRendered = 0;
            s_StatWinDry = 0;
            s_StatWinOverCap = 0;
            s_StatWinSkipped = 0;
            s_StatWinForced = 0;
        }
        s_StatWinPumps++;
        /* portAudioPump runs once per message-queue pass, and those passes are
         * far more frequent than drawn frames; most calls find the queue
         * already at target and render nothing. Counting the ones that did is
         * what makes the call rate interpretable instead of just large. */
        if (rendered != 0) {
            s_StatWinBusy++;
        }
        s_StatWinRendered += rendered;
        s_StatWinDry += (u32) dry_now;
        s_StatWinOverCap += (u32) over_cap_now;
        s_StatWinSkipped += (u32) skipped_now;
        s_StatWinForced += (u32) s_ForceOneFrame;

        if (now - s_StatWinTick >= PORT_AUDIO_STATS_WINDOW_MS) {
            const u32 window = now - s_StatWinTick;

            s_Stats.pumps = s_StatWinPumps;
            s_Stats.busy = s_StatWinBusy;
            s_Stats.window_ms = window;
            s_Stats.rendered = s_StatWinRendered;
            /* what that wall time was worth in samples, at the output rate */
            s_Stats.needed = (u32) (((unsigned long long) window * PORT_AUDIO_RATE) / 1000u);
            s_Stats.dry = s_StatWinDry;
            s_Stats.overcap = s_StatWinOverCap;
            s_Stats.skipped = s_StatWinSkipped;
            s_Stats.forced = s_StatWinForced;
            s_Stats.valid = 1;
            s_StatWinTick = now;
            s_StatWinPumps = 0;
            s_StatWinBusy = 0;
            s_StatWinRendered = 0;
            s_StatWinDry = 0;
            s_StatWinOverCap = 0;
            s_StatWinSkipped = 0;
            s_StatWinForced = 0;
        }
    } else {
        /* Re-enabling restarts from a clean window rather than reporting the
         * gap since it was last on. */
        s_StatWinPrimed = 0;
    }

    /* console cl_audio_log: the same numbers to Data/Logs/audio_pump.log. Kept
     * independent of the overlay, so a report can carry the history without
     * anyone having watched the corner of the screen. */
    portAudioLogLine(now);

    /* GE_AUDIODBG=1: one line a second with the numbers that decide the pace,
     * so a starvation report can be read instead of guessed at. */
    {
        extern char *getenv(const char *);
        static s32 dbg = -1;
        static u32 dbg_tick;
        static u32 dbg_pumps;
        static u32 dbg_rendered;
        static u32 dbg_span;

        if (dbg < 0) {
            dbg = (getenv("GE_AUDIODBG") != NULL);
        }
        if (dbg) {
            dbg_pumps++;
            dbg_rendered += rendered;
            dbg_span += span_ms;
            if (now - dbg_tick >= 1000) {
                portConLogf("ge: audio dbg: %u pumps in %u ms, rendered %u samples (need %u),"
                            " queue %u -> %u of %u, clock %lld, stalled %d, forced %d\n",
                            dbg_pumps, dbg_span, dbg_rendered, (u32)((dbg_span * PORT_AUDIO_RATE) / 1000),
                            s_QueuedAtOpen, queued, target, (long long) s_Clock.credit,
                            s_Stalled, s_ForceOneFrame);
                dbg_tick = now;
                dbg_pumps = 0;
                dbg_rendered = 0;
                dbg_span = 0;
            }
        }
    }

    s_InPump = 0;
}

/**
 * Render exactly ONE audio frame, whatever the clock says. music.c's track-stop
 * waits spin on alCSPGetState(), which only changes when the sequencer client
 * runs inside alAudioFrame - on N64 the audio thread did that concurrently; on
 * PC the waiting thread must drive it.
 *
 * The frame advances the sequencer and costs the clock, but it is queued only
 * when the queue is shallow: a forced frame is a scheduling device, not audio
 * to be played. Output still respects the queue cap.
 */
void portAudioPumpForce(void)
{
    extern void portAudioPump(void);

    s_ForceOneFrame = 1;
    portAudioPump();
    s_ForceOneFrame = 0;
}
