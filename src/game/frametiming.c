#include <ultra64.h>
#include "frametiming.h"
#include "tick_stats.h"   /* the clamp verdict the hitch log reads */

// data
s32 lastFrameCounter = -1;
s32 currentFrameCounter = 0;

/**
 * Appears to be rendered framerate, or some kind of counter since the last frame update.
 */
s32 speedgraphframes = 1;

#if defined(BUGFIX_R1)
// EU address D_8004111C
f32 jpD_800484CC = 1.0f;

// EU address D_80041120
f32 jpD_800484D0 = 1.0f;
#endif

s32 previousFrameCounter = -1;
s32 halfFrameCounter = 0; // half of currentFrameCounter
s32 isFrameCounterOdd = 0; // is currentFrameCounter Odd
s32 halfMinusPreviousCounter = 0; // half - previousFrameCounter
u32 copy_of_osgetcount_value_0 = 0;
u32 copy_of_osgetcount_value_1 = 0;
s32 frameDelay = 1; //usually 1

#ifndef TARGET_N64
/* ---- GE_TICKHZ: sub-field simulation ticks -----------------------------
 *
 * GE_TICKHZ=120 runs the game loop twice per video field while keeping the
 * time UNIT at 1/60s: every constant in the game and the AI/setup data stays in
 * 60ths. Two values come out of each tick:
 *
 *   speedgraphframes / g_ClockTimer : WHOLE fields completed this tick
 *                                     (1,0,1,0 at 120Hz). Discrete logic
 *                                     (AI, actions, animation, ammo,
 *                                     countdowns) is already gated on
 *                                     `g_ClockTimer > 0`, so it still runs
 *                                     at a true 60Hz.
 *   portTickDelta                    : the real fractional field delta
 *                                     (0.5 at 120Hz). It feeds
 *                                     g_GlobalTimerDelta, so float-scaled
 *                                     motion (Bond movement, camera, fades)
 *                                     integrates at the raised rate.
 *
 * The delta is MEASURED, never assumed: hardcoding 0.5 would run the game at
 * half speed whenever the loop is throttled (boss.c's pendingGfx<2 backpressure
 * does that under load).
 *
 * portTickCadence keeps the last whole-field delta across 0-delta sub-ticks,
 * for the few sites that branch on the tick class instead of accumulating it
 * (gun.c's automatic-fire rate doubling).
 *
 * Off by default. Demo RECORDING forces classic 60Hz, because the recorder
 * stores speedgraphframes per block and 0 is its end-of-demo marker. Demo
 * PLAYBACK never reaches this code, so replay sync is unaffected.
 */
/* TRUE counts per video field (OS_CPU_COUNTER / refresh), not Rare's slightly short 775875.
 * The sub-field path carries its remainder, so a short field would compound
 * into a 0.69% fast clock. */
#ifdef REFRESH_PAL
#define PORT_TICK_FIELD_COUNTS 937500U
#else
#define PORT_TICK_FIELD_COUNTS 781250U
#endif

/* Rare's own field-minus-slack constants (waitForNextFrame's divisor). As an
 * admission gate these yield exactly 60 ticks/s however fast the pump runs -
 * used to hold the front-end at 60 while gameplay uses the configured rate. */
#ifdef REFRESH_PAL
#define PORT_TICK_FIELD_GATE 931050U
#else
#define PORT_TICK_FIELD_GATE 775875U
#endif

f32 portTickDelta = 1.0f;

/* GE_TICKMAXSTEP=n: most fields one measured tick may advance (stall cap,
 * see waitForNextFrame). Default 8; 0 = uncapped. */
static s32 portTickMaxStep(void)
{
    static s32 cap = -1;
    if (cap < 0)
    {
        extern char *getenv(const char *);
        extern int atoi(const char *);
        const char *e = getenv("GE_TICKMAXSTEP");
        cap = (e && *e) ? atoi(e) : 8;
        if (cap < 0) cap = 0;
    }
    return cap;
}

/* 1 when GE_TICKMAXSTEP set the cap: an experiment, obeyed exactly (see
 * portTickCreditCap). */
static s32 portTickMaxStepExplicit(void)
{
    extern char *getenv(const char *);
    const char *e = getenv("GE_TICKMAXSTEP");

    return (e != NULL && *e != '\0') ? 1 : 0;
}

/** The stall cap for the tick being measured, in fields (0 = uncapped).
 *
 * The 8-field cap cannot tell an unintended stall from a deliberate hold, so
 * it truncated the holds tickhz=hw had just waited for: boss.c held the loop
 * for the N fields portHwPaceFields() priced, then this cap kept 8 of them and
 * the difference was simulated time thrown away (the audit measured 45-50
 * fields per frame in Dam).
 *
 * Under hw pacing the hold IS the intended duration, so the cap admits it. The
 * decision itself is portStatsStepCap() (port/src/tick_stats.c), a pure
 * function with the policy table under ctest; this only gathers the three
 * inputs. Three of those matter:
 *
 *  - portTickHwPace(), not the configured tickhz: a window drag in a menu or a
 *    demo has no priced hold behind it and stays capped at 8.
 *  - g_PortHwFieldsHeld is the hold boss.c just waited for, published by
 *    portHwPaceFields() when it priced the frame. Reading it here is what
 *    keeps the contract one-way: the pacer publishes, the clamp reads, and no
 *    expectation is threaded back through boss.c (the objection recorded in
 *    docs/port/tick-and-field-accounting.md 5.2).
 *  - an explicit GE_TICKMAXSTEP wins, because it is how this cap is measured.
 *
 * Bounded by the hold, which portHwPaceFields() caps at 30 fields - so the
 * worst case admitted is the model's own ceiling, not an unbounded stall. */
static u32 portTickCreditCap(void)
{
    extern s32 g_PortHwFieldsHeld;   /* below: the hold the pacer published */
    const s32 cap = portTickMaxStep();

    if (cap == 0)
    {
        return 0; /* uncapped: portStatsStepCap keeps 0 as 0 */
    }

    return portStatsStepCap((u32) cap,
                            (u32) __atomic_load_n(&g_PortHwFieldsHeld, __ATOMIC_RELAXED),
                            (int) portTickHwPace(),
                            (int) portTickMaxStepExplicit());
}

/** Publish the stall cap's verdict for the frame just measured.
 *
 * Called from waitForNextFrame *before* it clamps, with the raw elapsed counts
 * and the cap that is about to be applied - the same one the caller uses, so
 * the reported credit cannot disagree with the credit taken. Only the game
 * thread runs this, so the read-modify-write of the sequence is single-writer;
 * the render thread only reads (port/src/hitchlog.c). */
static void portTickNoteClamp(u32 elapsed_counts, u32 cap)
{
    const u32 measured = portStatsMeasuredFields(elapsed_counts, PORT_TICK_FIELD_COUNTS);
    const u32 credited = portStatsCreditedFields(measured, cap);

    __atomic_store_n(&g_PortTickClampMeasured, measured, __ATOMIC_RELAXED);
    __atomic_store_n(&g_PortTickClampCredited, credited, __ATOMIC_RELAXED);
    __atomic_store_n(&g_PortTickClampSeq,
                     __atomic_load_n(&g_PortTickClampSeq, __ATOMIC_RELAXED) + 1u,
                     __ATOMIC_RELEASE);
}

/* GE_NOTICKCARRY=1: restore the old classic (tickhz=60) behaviour -
 * round elapsed to the nearest field and drop the remainder - for A/B
 * comparison against the carry version below. */
static s32 portTickCarryOff(void)
{
    static s32 off = -1;
    if (off < 0)
    {
        extern char *getenv(const char *);
        const char *e = getenv("GE_NOTICKCARRY");
        off = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return off;
}
s32 portTickCadence = 1;

/* Written by gfx_sdl2's stat pass; read by the show_fps digits and the
 * cl_audio_pump block. */
s32 g_PortStatFps = 0;
/* Computed because the stat pass has always published it, but nothing READS it
 * any more: the overlay and the log now carry g_PortStatTicks (the real tick
 * rate) and g_PortStatFieldsX10 (game speed) instead. Its value is
 * min(tick rate, 60), because lv.c increments g_GlobalTickCount once per tick
 * that advanced the clock by >= 1 field, so it cannot distinguish 60 Hz from
 * 120 Hz and it tracks fps when frames overrun a field. Kept rather than
 * deleted because it belongs to the original import's set of stat globals. */
s32 g_PortStatTps = 0;
s32 g_PortStatTicks = 0;
s32 g_PortStatFieldsX10 = 0;
s32 g_PortStatFieldsPerTickX100 = 0;

/* Monotonic since boot: the fields credited to the sim. LvlStageLoad zeroes
 * g_GlobalTickCount (lv.c), so the tick counter cannot be used to measure a
 * rate across a load - this one is never reset. See the stat pass in
 * gfx_sdl2.cpp and docs/port/tick-and-field-accounting.md. */
u32 g_PortFieldsTicked = 0;

/* The stall cap's last verdict, for the hitch log (port/src/hitchlog.c).
 *
 * Published HERE, at the clamp, because this is the only place that knows both
 * what a tick measured and what it was allowed to keep. The hitch line used to
 * invite a reader to compare the credited fields against the hw model's hold,
 * which measures nothing: when the renderer is slower than the model the frame
 * spans far more wall time than either number, so that comparison read
 * "healthy" at exactly the moment the most simulated time was being dropped.
 *
 * ClampMeasured/ClampCredited are only meaningful together, so a reader takes
 * ClampSeq before and after: unchanged means the pair belongs to one clamp.
 * Relaxed loads are enough for the values themselves (they are stored before
 * the seq bump, and the reader's re-check discards a torn pair); the seq bump
 * is a release store so the two values are visible before it is. */
u32 g_PortTickClampSeq = 0;
u32 g_PortTickClampMeasured = 0;
u32 g_PortTickClampCredited = 0;

/* Counts of real time credited to the field accumulator but not yet worth a
 * whole field. THE carry - see waitForNextFrame. */
static u32 portTickFieldAccum = 0;
static s32 portTickCfgHz = -1;      /* midas.ini [display] tickhz; -1 = unset.
                                     * Not 0, because tickhz=0 is a real value meaning uncapped. */
static s32 portTickHzCache = -1;

/**
 * midas.ini hook (video.c); the GE_TICKHZ env var still wins for quick A/B runs.
 * The ini setting exists because the env var only reaches the game when the
 * launcher was started from a shell that had it set.
 */
void portTickSetHz(s32 hz)
{
    portTickCfgHz = hz;
    portTickHzCache = -1;           /* re-resolve: config load may follow first use */
}

/**
 * Configured simulation rate in Hz: 60 = classic (bit-identical vanilla
 * path), 0 = uncapped, anything else = that literal rate.
 *
 * Any rate works (144 and 165 as well as 120 and 240) because the measurement
 * carries raw clock counts rather than whole sub-steps. That matters because
 * fps equals tick rate here, so a 144Hz panel needs a 144Hz tick.
 */
s32 portTickHz(void)
{
    extern char *getenv(const char *);
    extern long strtol(const char *, char **, int);
    extern s32 g_ramromRecordFlag;

    if (portTickHzCache < 0)
    {
        extern int printf(const char *, ...);
        const char *env = getenv("GE_TICKHZ");
        s32 hz = (env != NULL) ? (s32) strtol(env, NULL, 0)
               : (portTickCfgHz >= 0) ? portTickCfgHz
               : 60;

        if (hz < 0)
        {
            hz = 60;
        }
        if (hz > 2000)
        {
            hz = 2000;
        }

        portTickHzCache = hz;

        /* Log the resolved rate: "60 fps | 60 tps" looks the same whether the
         * setting never arrived or something downstream pinned the loop. */
        printf("ge: sim tick rate = %d Hz%s (from %s)\n",
               hz, (hz == 0) ? " UNCAPPED" : "",
               (getenv("GE_TICKHZ") != NULL) ? "GE_TICKHZ"
             : (portTickCfgHz >= 0) ? "midas.ini tickhz" : "default");
    }

    /* The recorder stores speedgraphframes per block and a 0 there is the
     * end-of-demo terminator, so recording always runs classic. */
    if (g_ramromRecordFlag != 0)
    {
        return 60;
    }

    return portTickHzCache;
}

/**
 * boss.c's tick admission gate, in count units: half a tick period, which
 * is the same ratio vanilla uses (half a field against a 60Hz retrace).
 * 0 = uncapped, admit every retrace.
 */
/**
 * Admission gate that produces exactly 60 ticks/s at any pump rate.
 *
 * The front end (legal screen, Nintendo and Rare logos, the eye/gunbarrel
 * intro, the GoldenEye logo, file select and its watch) is driven entirely by
 * raw per-tick counters in title.c (`gunbarrelTimer++` etc.) with no time
 * delta, so at a raised tick rate it all runs too fast.
 *
 * Rather than delta-scale the boot sequence, hold the whole front end to 60.
 * Gameplay, the only place a higher rate helps, keeps the configured rate.
 */
u32 portTickFieldGateCounts(void)
{
    return PORT_TICK_FIELD_GATE;
}

/**
 * REAL-TIME PACING for work that lives in a display-list builder.
 *
 * A `Gfx *` function runs once per PRESENTED FRAME, so anything in one that
 * counts its own calls ("every 2nd call", `g_TitleX += XINC`) really runs at
 * the frame rate, which is only 60/s while presents are tied to the 60Hz tick.
 * The blood animations and the gunbarrel intro both worked this way.
 *
 * Returns 1 when `counts` have elapsed since the last time it said so.
 * PORT_TICK_FIELD_GATE is one 60Hz field, so that period gives 60 steps/s and
 * twice it gives 30.
 *
 * It ADVANCES the deadline instead of resetting it to now: resetting drops the
 * remainder every frame and makes the rate come out low. A backlog of 4 periods
 * resyncs instead of sprinting through it.
 *
 * Unsigned throughout, so the difference stays correct when osGetCount wraps
 * (about every 92 seconds). A stamp of 0 means "not started", so a sequence's
 * first step happens immediately.
 */
/**
 * FRONT-END TICK PACING: keeps the gunbarrel intro in sync with its music (the
 * intro is forced to 30 ticks/s once the barrel appears).
 *
 * boss.c's admission gate measures time since the last admitted tick, so a held
 * rate rounds to whole pump periods: 30Hz is exact at a 60 or 180Hz pump but
 * comes out around 28-29Hz at 144 or 165Hz. title.c's counters step once per
 * tick while the music runs in real time, so the barrel walk drifted behind the
 * music. An advancing deadline (as in portPaceDue) averages exactly instead.
 *
 * portTickIntroPaceDue(fields): returns 1 when a held-rate tick is due; the
 * deadline advances by exactly `fields` 60Hz fields each time, so 2 gives 30
 * ticks/s and 1 gives 60 at any pump rate. One stamp serves both rates, so the
 * 60 -> 30 switch at barrel entry stays continuous. Call
 * portTickIntroPaceReset() when no hold applies, so the next sequence starts on
 * its first pump instead of inheriting a stale deadline.
 */
static u32 sIntroPaceStamp;

s32 portTickIntroPaceDue(s32 fields)
{
    return portPaceDue(&sIntroPaceStamp, (u32) fields * PORT_TICK_FIELD_GATE);
}

/**
 * BLOOD-DRIP DIVISOR: slows only the dripping blood in the barrel intro
 * (title.c die_blood, mode 5); the sway, walk and durations stay at the 30Hz
 * intro lock. 1 = 30fps drip, 2 = 15fps (the default, which matches the
 * original), up to 8. GE_INTROBLOOD overrides it.
 */
s32 portGunbarrelBloodDiv(void)
{
    static s32 d = -1;
    if (d < 0)
    {
        extern char *getenv(const char *);
        extern int atoi(const char *);
        const char *e = getenv("GE_INTROBLOOD");
        d = (e != NULL && e[0] != '\0') ? atoi(e) : 2; /* DEFAULT 2 = half-speed drip (matches the
                                                        * original) */
        if (d < 1) d = 1;
        if (d > 8) d = 8;
    }
    return d;
}

void portTickIntroPaceReset(void)
{
    sIntroPaceStamp = 0;
}

s32 portPaceDue(u32 *stamp, u32 counts)
{
    u32 now = osGetCount();

    if (*stamp == 0)
    {
        *stamp = (now != 0) ? now : 1;
        return 1;
    }
    if ((u32) (now - *stamp) < counts)
    {
        return 0;
    }
    *stamp += counts;
    if ((u32) (now - *stamp) >= counts * 4U)
    {
        *stamp = (now != 0) ? now : 1;
    }
    return 1;
}

u32 portTickGateCounts(void)
{
    s32 hz = portTickHz();

    if (hz <= 0)
    {
        /* "Uncapped" gets a 1000Hz ceiling rather than a zero gate: zero left
         * the loop completely unbounded and could overflow the scheduler
         * queues. 1000Hz is far beyond any frame rate the port reaches, so it
         * is still uncapped in practice. */
        hz = 1000;
    }

    return (PORT_TICK_FIELD_COUNTS * 60u / (u32) hz) / 2u;
}

/* ---- Sub-field SMOOTHING ----------------------------------------------
 *
 * A raised tick rate makes delta-integrated motion smooth, but some per-frame
 * work is written in WHOLE FIELDS and only advances when g_ClockTimer > 0:
 *
 *   - animation: modelTickAnim(model, g_ClockTimer) for every chr, every
 *     animated object and Bond's head/walk model;
 *   - the `for (i = 0; i < g_ClockTimer; i++) x = x*d + in` lag filters
 *     that smooth the camera (head position, stair height follow, landing
 *     bounce, tank turret) and a few object rates (door speed, autogun
 *     spin-down).
 *
 * At 120Hz that is a clean every-other-frame hold, but at 144/165 the field
 * pattern is uneven (1,0,1,0,0,1,...), which shows up as jitter in moving
 * objects, camera pans and stair climbing.
 *
 * The cure is to let those sites advance by the true fractional delta:
 *   portAnimTicks()  - fractional animation ticks for this tick
 *   portLagStep()    - the lag filter advanced by `delta` fields in one
 *                      closed-form step (gunfire.c's gunCrossLagStep rule)
 *
 * Both reduce EXACTLY to vanilla whenever the delta is a whole number of
 * fields (tickhz=60, demo playback, paused), so no separate gate is needed.
 * GE_NOSMOOTH=1 restores field-stepped behaviour at a raised rate for A/B.
 */
s32 portSmoothOn(void)
{
    extern char *getenv(const char *);
    static s32 cache = -1;

    if (cache < 0)
    {
        const char *env = getenv("GE_NOSMOOTH");
        cache = (env != NULL && env[0] != '\0' && env[0] != '0') ? 0 : 1;
    }

    if (!cache || portTickHz() == 60)
    {
        return 0;
    }

    /* Demo playback replays integer field deltas; keep it on the vanilla
     * loops so replay stays bit-identical at every tickhz. */
    {
        extern s32 get_is_ramrom_flag(void);
        return get_is_ramrom_flag() == 0;
    }
}

/**
 * Animation ticks for THIS tick. g_GlobalTimerDelta when smoothing (it
 * equals g_ClockTimer whenever the delta is whole, so vanilla and demo
 * playback are untouched), else the integer field count.
 */
f32 portAnimTicks(void)
{
    extern s32 g_ClockTimer;
    extern f32 g_GlobalTimerDelta;

    if (portSmoothOn())
    {
        return g_GlobalTimerDelta;
    }

    return (f32) g_ClockTimer;
}

/**
 * Sub-field PHASE for this tick: the fraction of the current field that has
 * elapsed, taken from the remainder waitForNextFrame banks (exact against
 * g_ClockTimer). Used by output-side smoothing such as gunfire.c's hand pose,
 * and only meaningful under portSmoothOn().
 */
f32 portTickFieldPhase(void)
{
    return (f32) portTickFieldAccum / (f32) PORT_TICK_FIELD_COUNTS;
}

/**
 * Phase for drawing a field-stepped particle part of the way to its next
 * step: 0 unless smoothing is on, and held while paused so nothing wobbles.
 */
f32 portRenderFieldPhase(void)
{
    extern f32 g_GlobalTimerDelta;
    static f32 held;

    if (!portSmoothOn())
    {
        return 0.0f;
    }

    if (g_GlobalTimerDelta > 0.0f)
    {
        held = portTickFieldPhase();
    }

    return held;
}

/**
 * Advance the first-order lag `x = x*d + in` by `delta` FIELDS in one step.
 *
 *   d'   = d^delta
 *   gain = (1 - d^delta) / (1 - d)
 *
 * For whole-number deltas this takes the original loop, so a whole-field tick
 * is bit-for-bit vanilla. The gain term is required: it keeps the steady state
 * at in/(1-d).
 */
void portLagStep(f32 *x, f32 d, f32 in, f32 delta)
{
    extern float powf(float, float);
    s32 n = (s32) delta;

    if (delta <= 0.0f)
    {
        return;
    }

    if ((f32) n == delta && n <= 16)
    {
        while (n-- > 0)
        {
            *x = (*x * d) + in;
        }
        return;
    }

    if ((1.0f - d) <= 0.0f)
    {
        return;
    }

    {
        f32 dn = powf(d, delta);
        f32 gain = (1.0f - dn) / (1.0f - d);

        *x = (*x * dn) + (in * gain);
    }
}

/**
 * Pure decay `x *= d` per field, advanced by `delta` fields.
 */
void portDecayStep(f32 *x, f32 d, f32 delta)
{
    portLagStep(x, d, 0.0f, delta);
}
#endif


/**
 * Stores the current OS count in the two global variables.
 */
void store_osgetcount(void)
{
    copy_of_osgetcount_value_1 = osGetCount();
    copy_of_osgetcount_value_0 = copy_of_osgetcount_value_1;
}


/**
 * Updates the timing-related counters and frame information based on the given argument.
 *
 * @param deltaFrames The number of frames to add to the current frame counter.
 */
#ifndef TARGET_N64
extern u32 g_PortHwChrsTicked; /* chr.c */
u32 g_PortHwChrsLast;          /* the previous tick's count (GE HW pacing / GE_HWCAL) */

/* ---- GE "HW" tick rate: the console's frame pacing --------------------------
 * tickhz=hw runs the classic 60 Hz sim but holds each frame for the number of
 * video fields the N64 would have needed to render it, reproducing the
 * console's frame rate, its slowdown in heavy views and its stepped motion.
 *
 * The frame time comes from a linear model of the RDP workload (rebuilt from
 * the same display list, gfx_api.h PortHwLoad) plus the number of characters
 * simulated, fitted against fields recorded by the attract demos on real
 * hardware (docs/port/n64-hw-pacing.md, "The render model"). GE_HWSCALE=<f>
 * scales the prediction (1.0 = the fit); GE_HWDBG=1 prints it once a second. */
#include "../../port/fast3d/gfx_api.h"
#include "romdata.h"
extern s32 get_is_ramrom_flag(void);
extern s32 g_ramromRecordFlag;
static s32 portTickHwPaceFlag;
void portTickSetHwPace(s32 on)
{
    portTickHwPaceFlag = on;
}
s32 portTickHwPace(void)
{
    /* Demo playback keeps its recorded pacing and recording stays classic;
     * hardware pacing applies only in a stage, outside the watch menu. The
     * model's fixed cost alone is two fields, which made menus run at 30 and
     * slowed their mouse and timers. */
    extern s32 portStageRunning(void);
    extern s32 portWatchMenuUp(void);

    return portTickHwPaceFlag && g_ramromRecordFlag == 0 && get_is_ramrom_flag() == 0
        && portStageRunning() && !portWatchMenuUp();
}

/* Port-only collision fixes for rates faster than the N64's 20-30 fps (doors,
 * vehicles): on for a tick under two fields, never under Hardware pacing or in a demo. */
s32 portFastTickFixOn(void)
{
    extern f32 g_GlobalTimerDelta;
    extern s32 g_ramromPlayBackFlag;
    extern s32 get_recording_ramrom_flag(void);

    return g_GlobalTimerDelta > 0.0f && g_GlobalTimerDelta < 2.0f && !portTickHwPaceFlag
        && get_is_ramrom_flag() == 0 && g_ramromPlayBackFlag == 0
        && get_recording_ramrom_flag() == 0 && g_ramromRecordFlag == 0;
}
/* ms = c0 + c1*fill2/1e3 + c2*fill1/1e3 + c3*fillx/1e3 + c4*fillr/1e3 + c5*loads + c6*lbytes/1e3 + c7*tris + c8*chrs */
static const f32 sHwModel[9] = { 13.174978f, 0.027856f, 0.000000f, 0.050997f, 0.049921f, 0.000000f, 0.028149f, 0.004372f, 0.152045f }; /* fitted against 14 attract demos
                                                                                                                                        * (hwfit.py) */
f32 portHwPredictMs(const struct PortHwLoad *h, u32 chrs)
{
    return sHwModel[0] + sHwModel[1] * (f32) h->fill2 * 0.001f + sHwModel[2] * (f32) h->fill1 * 0.001f
         + sHwModel[3] * (f32) h->fillx * 0.001f + sHwModel[4] * (f32) h->fillr * 0.001f
         + sHwModel[5] * (f32) h->loads + sHwModel[6] * (f32) h->lbytes * 0.001f
         + sHwModel[7] * (f32) h->tris + sHwModel[8] * (f32) chrs;
}
/* ---- load hitches ---------------------------------------------------------
 * The console loads in-frame: a weapon swap, a guard's body or a room coming
 * into view each DMA compressed bytes off the cart and inflate them on the CPU,
 * so that frame runs long. tickhz=hw charges that cost on the port too:
 * every load notes its COMPRESSED size, and the next frame hold adds the time
 * on top of the render model.
 *
 * Load sites: ob.c file loads (portHwNoteFile) and room bytes, image.c texture
 * loads, and objecthandler_2.c model textures for private-pool loads (the
 * hands, which the N64 re-inflates on every swap). A stage load's own
 * charges become its load time instead (portHwLoadStageLoaded; boss.c holds
 * it before the first frame).
 *
 * The rates are fitted to a real console (the N64 bench ROM, 867 file loads
 * over every mission, docs/port/n64-hw-pacing.md, "Load charges"):
 *   cart DMA   174 us per read + 0.2025 us per byte
 *   inflate    714 us + 1.254 us per compressed byte during a stage load,
 *              326 us + 1.479 us per compressed byte in play (the other
 *              threads share the CPU then)
 *   GE_HWLOAD_SCALE=<x> scales both (default 1); GE_HWLOAD=0 turns it off;
 *   GE_HWDBG=1 prints every hitch. */
static f32 sHwLoadMs;
static s32 sHwLoadSuppress;
static f32 sHwLoadScale = -1.0f;
static s32 sHwLoadDbg; /* GE_HWDBG=2: every load, as it is charged */
static s32 sHwInStageLoad; /* between portHwLoadStageBegin and portHwLoadStageLoaded */
static u32 sHwStageRomBytes; /* cart bytes read during it */
extern s32 portClassicHitches(void);   /* video.c: the CLASSIC sheet's Load Hitches row */
static void portHwLoadInit(void)
{
    extern char *getenv(const char *);
    extern double atof(const char *);

    if (sHwLoadScale < 0.0f)
    {
        const char *e = getenv("GE_HWLOAD");
        if (e != NULL && e[0] == '0')
        {
            sHwLoadScale = 0.0f;
        }
        else
        {
            e = getenv("GE_HWLOAD_SCALE");
            sHwLoadScale = (e != NULL) ? (f32) atof(e) : 1.0f;
            if (sHwLoadScale < 0.0f) sHwLoadScale = 0.0f;
        }
        e = getenv("GE_HWDBG");
        sHwLoadDbg = (e != NULL && e[0] == '2');
    }
}
/* the file itself is always charged; only texLoad's per-texture notes yield to
 * a private-pool model load, which charges its textures from the header */
static void portHwChargeLoad(u32 romBytes, s32 compressed, s32 yieldToModel)
{
    portHwLoadInit();
    if ((yieldToModel && sHwLoadSuppress > 0) || !portTickHwPace() || !portClassicHitches())
    {
        return;
    }
    {
        extern int printf(const char *, ...);
        f32 us = 174.0f + 0.2025f * (f32) romBytes;
        f32 ms;

        if (compressed)
        {
            us += sHwInStageLoad ? 714.0f + 1.254f * (f32) romBytes : 326.0f + 1.479f * (f32) romBytes;
        }
        ms = us * 0.001f * sHwLoadScale;
        if (romBytes == 0)
        {
            return;
        }
        if (sHwInStageLoad)
        {
            sHwStageRomBytes += romBytes;
        }

        sHwLoadMs += ms;
        if (sHwLoadDbg && romBytes != 0)
        {
            printf("ge: hwload: %u bytes%s -> %.2f ms\n", romBytes, compressed ? " (1172)" : "", ms);
        }
    }
}
void portHwNoteLoad(u32 romBytes, s32 compressed)
{
    portHwChargeLoad(romBytes, compressed, 1);
}
/* a file-table load: its ROM span, compressed or not, from the cart's own directory */
void portHwNoteFile(s32 index)
{
    const struct romfile *f;

    portHwLoadInit();
    if (!portTickHwPace() || !romdataLoaded())
    {
        return;
    }
    f = romdataFile(index);
    if (f != NULL && f->size != 0)
    {
        if (sHwLoadDbg)
        {
            extern int printf(const char *, ...);
            printf("ge: hwload: file %s\n", f->name);
        }
        portHwChargeLoad(f->size, f->compressed, 0);
    }
}
/* a private-pool model load notes its textures itself; texLoad stays quiet meanwhile */
void portHwLoadSuppress(s32 on)
{
    sHwLoadSuppress += on ? 1 : -1;
}

/* ---- texture decode and stage setup (bench v2 fit) -------------------------
 * Most of a console stage load is not the cart: it is decoding textures (Rare's
 * own non-zlib formats, ~1-30 ms each) and the setup code matching every pad to
 * a stand tile. Both are charged from what the port's identical code actually
 * does, so a mod's own textures and setups cost what they would on a console:
 *
 *   texture: the decoder notes image 0's format/size/method/colours
 *            (portHwTexInfo) and texLoad prices it (portHwTexDecoded), fit to
 *            10562 texture records, per-stage error within +-9%
 *   setup:   calls and steps of the tile matchers, counted in g_PortHwWork
 *            and priced at the end of the stage load, plus a remainder fit
 *            per guard, object, texture and cart KB (rms 48 ms per stage)
 * The per-texture cost is remembered by number for a private-pool model
 * load (the hands), which charges its textures from the header. */
u32 g_PortHwWork[PORTHW_WORK_COUNT];
static u8  sTexInfoFmt, sTexInfoMethod, sTexInfoImages;
static s32 sTexInfoW, sTexInfoH, sTexInfoCols;
static f32 sTexCostUs[4096]; /* 0 = never decoded */
void portHwTexInfo(s32 format, s32 width, s32 height, s32 method, s32 numcolours, s32 numimages)
{
    sTexInfoFmt = (u8) format;
    sTexInfoW = width;
    sTexInfoH = height;
    sTexInfoMethod = (u8) method;
    sTexInfoCols = numcolours;
    sTexInfoImages = (u8) numimages;
}
/* decode time on the console, us (method 0xff = zlib) */
static f32 portHwTexDecodeUs(s32 zlib, s32 method, s32 format, s32 w, s32 h, s32 cols, s32 images, s32 lod, s32 explicitlods)
{
    static const u8 channels[13] = { 4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1 };
    static const u16 chansize[13] = { 0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100, 0x10, 0x100, 0x10, 0x100, 0x10 };
    const f32 ch = (format >= 0 && format < 13) ? (f32) channels[format] : 1.0f;
    const f32 chs = (format >= 0 && format < 13) ? (f32) chansize[format] * ch : 0.0f;
    const f32 px = (f32) w * (f32) h;
    f32 pxT = 0.0f, quarter = 1.0f, pxMip = 0.0f;
    f32 img = (images < 1) ? 1.0f : (f32) images;
    f32 us;
    s32 k;

    for (k = 0; k < (s32) img; k++, quarter *= 0.25f)
    {
        pxT += px * quarter; /* every explicit image is a quarter of the last */
    }
    if (!explicitlods && lod >= 2)
    {
        for (k = 1, quarter = 0.25f; k < lod; k++, quarter *= 0.25f)
        {
            pxMip += px * quarter; /* mip levels the decoder shrinks itself */
        }
    }
    if (zlib)
    {
        return 705.2f + 431.7f * img + 0.12989f * img * chs + 0.695f * pxT
               + 0.036446f * pxMip * (f32) cols + 0.34475f * pxMip + 1.0073f * (f32) cols;
    }
    switch (method)
    {
        case 2: /* HUFFMAN */
            us = 1.5286f * pxT * ch;
            break;
        case 3: /* HUFFMANPERHCHANNEL */
            us = 2.0763f * pxT * ch;
            break;
        case 4: /* RLE */
            us = 242.77f + 296.54f * img + 0.88427f * img * chs + 0.066697f * pxT + 0.28819f * pxT * ch;
            break;
        case 5: /* LOOKUP */
            us = 2.1592f * img * chs + 0.22146f * pxT * ch;
            break;
        case 6: /* HUFFMANLOOKUP */
            us = 421.47f + 10.639f * img * chs + 0.47622f * pxT + 0.83235f * pxT * ch;
            break;
        case 7: /* RLELOOKUP */
            us = 423.22f * img + 0.39231f * pxT + 8.5952f * (f32) cols * img;
            break;
        case 8: /* HUFFMANBLUR */
        case 9: /* RLEBLUR: no vanilla texture uses it; priced as HUFFMANBLUR */
            us = 5422.0f + 26.111f * img * chs + 1.1202f * pxT + 0.57923f * pxT * ch;
            break;
        default: /* UNCOMPRESSED: no vanilla texture uses it; priced as a LOOKUP's copy */
            us = 0.22146f * pxT * ch;
            break;
    }
    return us;
}
/* texLoad, after the decoder ran: price the decode, remember it, charge it */
void portHwTexDecoded(s32 texnum, s32 zlib, s32 lod, s32 explicitlods)
{
    const s32 method = zlib ? 0xff : sTexInfoMethod;
    /* the lookup methods' colour count is the decoder's; the others leave it stale */
    const s32 cols = (zlib || method == 5 || method == 6 || method == 7) ? sTexInfoCols : 0;
    const f32 us = portHwTexDecodeUs(zlib, method, sTexInfoFmt, sTexInfoW, sTexInfoH, cols, sTexInfoImages, lod, explicitlods);

    if (texnum >= 0 && texnum < (s32) (sizeof(sTexCostUs) / sizeof(sTexCostUs[0])))
    {
        sTexCostUs[texnum] = us;
    }
    if (sHwInStageLoad)
    {
        g_PortHwWork[PORTHW_TEXTURES]++;
    }
    portHwLoadInit();
    if (sHwLoadSuppress > 0 || !portTickHwPace() || !portClassicHitches())
    {
        return;
    }
    sHwLoadMs += us * 0.001f * sHwLoadScale;
    if (sHwLoadDbg)
    {
        extern int printf(const char *, ...);
        printf("ge: hwload: texture %d method %d %dx%d -> %.2f ms decode\n", texnum, method, sTexInfoW, sTexInfoH,
               us * 0.001f);
    }
}
/* load_bg_file: the console reads the bg file's first 0x40 bytes, then its
 * uncompressed front matter up to room 1's point table; the port uses the
 * linked or served copy in place, so price the two reads from the cart's own
 * header (z64 order) */
void portHwNoteBgHeader(s32 bgidx)
{
    const struct romfile *f;
    const u8 *rom;
    u32 romsize, list, pt;

    if (!portTickHwPace() || !romdataLoaded())
    {
        return;
    }
    f = romdataFile(bgidx);
    rom = romdataRom(&romsize);
    if (f == NULL || rom == NULL || f->romofs + 0x40 > romsize)
    {
        return;
    }
    portHwChargeLoad(0x40, 0, 0);
    list = ((u32) rom[f->romofs + 4] << 24 | (u32) rom[f->romofs + 5] << 16 | (u32) rom[f->romofs + 6] << 8
            | rom[f->romofs + 7]) & 0x00ffffffu;
    if (f->romofs + list + 28 > romsize || list + 28 > f->size)
    {
        return;
    }
    pt = ((u32) rom[f->romofs + list + 24] << 24 | (u32) rom[f->romofs + list + 25] << 16
          | (u32) rom[f->romofs + list + 26] << 8 | rom[f->romofs + list + 27]) & 0x00ffffffu;
    if (pt != 0 && pt <= f->size)
    {
        portHwChargeLoad((((pt - 1) | 0xf) + 1), 0, 0);
    }
}
/* a private-pool model load: one texture by number, cart read + its decode as
 * last priced (or ~5.6 us per cart byte, the console median, if never seen) */
void portHwNoteTexture(s32 texnum, u32 romBytes)
{
    f32 us = 0.0f;

    portHwChargeLoad(romBytes, 0, 0);
    if (texnum >= 0 && texnum < (s32) (sizeof(sTexCostUs) / sizeof(sTexCostUs[0])))
    {
        us = sTexCostUs[texnum];
    }
    if (us <= 0.0f)
    {
        us = 5.57f * (f32) romBytes;
    }
    portHwLoadInit();
    if (!portTickHwPace() || !portClassicHitches())
    {
        return;
    }
    sHwLoadMs += us * 0.001f * sHwLoadScale;
}
/* setup code: one propdef in proplvreset2's main loop */
void portHwNotePropdef(s32 type)
{
    if (!sHwInStageLoad)
    {
        return;
    }
    if (type == 9 /* PROPDEF_GUARD */)
    {
        g_PortHwWork[PORTHW_GUARDS]++;
    }
    else if (type == 1 || type == 3 || type == 4 || type == 5 || type == 6 || type == 7 || type == 8 || type == 10
             || type == 11 || type == 12 || type == 13 || type == 17 || type == 20 || type == 21 || type == 36
             || type == 39 || type == 40 || type == 41 || type == 42 || type == 43 || type == 45 || type == 47)
    {
        g_PortHwWork[PORTHW_OBJECTS]++; /* the PROPDEF types that make an object */
    }
}
/* the stage load's CPU work, us, from the counters */
static f32 portHwStageCpuUs(void)
{
    const u32 *w = g_PortHwWork;

    return 70.47f * (f32) w[PORTHW_MATCH_CALLS] + 0.7923f * (f32) w[PORTHW_MATCH_STEPS]
           + 222.17f * (f32) w[PORTHW_NEAR_CALLS] + 11.686f * (f32) w[PORTHW_NEAR_TILES]
           + 19.025f * (f32) w[PORTHW_NEAR_POINTS]
           + 34.22f * (f32) w[PORTHW_WALK_CALLS]
           + 0.675f * (f32) w[PORTHW_SETUPIDX_STEPS] + 1.099f * (f32) w[PORTHW_CHRFIND_STEPS]
           + 12280.0f + 692.6f * (f32) w[PORTHW_GUARDS] + 827.8f * (f32) w[PORTHW_OBJECTS]
           + 520.3f * (f32) w[PORTHW_TEXTURES] + 370.9f * (f32) sHwStageRomBytes * (1.0f / 1024.0f);
}
/* end of lvlStageLoad: what the load charged becomes the stage's load time,
 * held by boss.c before the first frame, not a hitch on it */
static f32 sHwStageLoadMs;
/* boss.c, just before lvlStageLoad: loads from here on are the stage load's */
void portHwLoadStageBegin(void)
{
    s32 i;

    sHwInStageLoad = 1;
    sHwStageRomBytes = 0;
    for (i = 0; i < PORTHW_WORK_COUNT; i++)
    {
        g_PortHwWork[i] = 0;
    }
}
void portHwLoadStageLoaded(void)
{
    extern char *getenv(const char *);

    sHwInStageLoad = 0;
    portHwLoadInit();
    if (portTickHwPace() && portClassicHitches())
    {
        const f32 cpu = portHwStageCpuUs() * 0.001f * sHwLoadScale;

        if (getenv("GE_HWDBG") != NULL)
        {
            extern int printf(const char *, ...);
            printf("ge: hwpace: stage load cart+decode %.1f ms, setup cpu %.1f ms (match %u/%u, nearest %u/%u/%u, "
                   "guards %u, objects %u, textures %u, cart %u KB)\n",
                   sHwLoadMs, cpu, g_PortHwWork[PORTHW_MATCH_CALLS], g_PortHwWork[PORTHW_MATCH_STEPS],
                   g_PortHwWork[PORTHW_NEAR_CALLS], g_PortHwWork[PORTHW_NEAR_TILES], g_PortHwWork[PORTHW_NEAR_POINTS],
                   g_PortHwWork[PORTHW_GUARDS], g_PortHwWork[PORTHW_OBJECTS], g_PortHwWork[PORTHW_TEXTURES],
                   sHwStageRomBytes >> 10);
        }
        sHwLoadMs += cpu;
    }
    sHwStageLoadMs = sHwLoadMs;
    sHwLoadMs = 0.0f;
    if (sHwStageLoadMs > 0.0f && getenv("GE_HWDBG") != NULL)
    {
        extern int printf(const char *, ...);
        printf("ge: hwpace: stage load %.1f ms\n", sHwStageLoadMs);
    }
}
f32 portHwTakeStageLoadMs(void)
{
    const f32 ms = sHwStageLoadMs;
    sHwStageLoadMs = 0.0f;
    return portTickHwPace() ? ms : 0.0f;
}
static f32 portHwTakeLoadMs(void)
{
    const f32 ms = sHwLoadMs;
    sHwLoadMs = 0.0f;
    return ms;
}

/* The field hold most recently priced, published for the hitch log to READ.
 *
 * hitchlog.c runs at present time, which is the RENDER thread when
 * render_thread=1, so it must not call portHwPaceFields(): that mutates statics
 * (lastFrame, lastFields) and CONSUMES the pending load charge via
 * portHwTakeLoadMs, which would both race the game thread and swallow a hitch
 * the pacer still needed. It is written and read with relaxed atomics for that
 * reason; a plain int would be a data race, and a torn one would print a
 * nonsense field count rather than a merely stale one. */
s32 g_PortHwFieldsHeld = 0;
s32 portHwPaceFields(void)
{
    extern char *getenv(const char *);
    extern double atof(const char *);
    extern int printf(const char *, ...);
    static u32 lastFrame = 0xffffffffu;
    static s32 lastFields = 1;
    static f32 scale = -1.0f;
    static s32 dbg = -1;
    static u32 dbgCount, dbgSumFields, dbgLastPrint;

    if (scale < 0.0f)
    {
        const char *e = getenv("GE_HWSCALE");
        scale = (e != NULL) ? (f32) atof(e) : 1.0f;
        if (scale <= 0.0f) scale = 1.0f;
        dbg = getenv("GE_HWDBG") != NULL;
    }
    if (g_PortHwLoadLast.frame != lastFrame)
    {
        f32 ms = portHwPredictMs(&g_PortHwLoadLast, g_PortHwChrsLast) * scale;
        const f32 loadMs = portHwTakeLoadMs();
        s32 fields;
        if (ms > 6.0f * (1000.0f / 60.0f)) ms = 6.0f * (1000.0f / 60.0f); /* the render model's 6-field cap */
        ms += loadMs; /* cart loads stall on top of it (portHwNoteLoad) */
        fields = (s32) ((ms + (1000.0f / 60.0f) - 0.001f) / (1000.0f / 60.0f)); /* ceil(ms / field) */
        if (fields < 1) fields = 1;
        if (fields > 30) fields = 30;
        if (dbg && loadMs > 0.0f)
        {
            printf("ge: hwpace: load hitch %.1f ms -> frame %.1f ms, %d fields\n", loadMs, ms, fields);
        }
        lastFrame = g_PortHwLoadLast.frame;
        lastFields = fields;
        /* published for the hitch log, which reads it from the render thread */
        __atomic_store_n(&g_PortHwFieldsHeld, fields, __ATOMIC_RELAXED);
        if (dbg)
        {
            dbgCount++; dbgSumFields += (u32) fields;
            if (dbgCount - dbgLastPrint >= 60u)
            {
                printf("ge: hwpace: %.1f ms -> %d fields (mean %.2f over %u frames) | f2 %u f1 %u fx %u fr %u loads %u KB %u tris %u chrs %u\n",
                        ms, fields, (f32) dbgSumFields / (f32) dbgCount, dbgCount, g_PortHwLoadLast.fill2, g_PortHwLoadLast.fill1,
                        g_PortHwLoadLast.fillx, g_PortHwLoadLast.fillr, g_PortHwLoadLast.loads, g_PortHwLoadLast.lbytes >> 10,
                        g_PortHwLoadLast.tris, g_PortHwChrsLast);
                dbgLastPrint = dbgCount;
            }
        }
    }
    return lastFields;
}
#endif
void updateFrameCounters(s32 deltaFrames)
{
#ifndef TARGET_N64
    g_PortHwChrsLast = g_PortHwChrsTicked;
    g_PortHwChrsTicked = 0;
#endif
#ifndef TARGET_N64
    /* GE_FIXEDTICK=1 (capture harness): advance the sim exactly one field per
     * rendered frame, so two runs of the same scene stay frame-for-frame
     * comparable for pixel diffs. The game runs in slow motion when rendering
     * can't keep up; never on by default. */
    {
        extern char *getenv(const char *);
        static s32 fixedtick = -1;

        if (fixedtick < 0)
        {
            fixedtick = (getenv("GE_FIXEDTICK") != NULL);
        }
        if (fixedtick && deltaFrames > 0)
        {
            deltaFrames = 1;
        }
    }
#endif

    copy_of_osgetcount_value_0 = (s32) copy_of_osgetcount_value_1;
    copy_of_osgetcount_value_1 = osGetCount();

    lastFrameCounter = currentFrameCounter;
    currentFrameCounter = (s32) (currentFrameCounter + deltaFrames);
    speedgraphframes = deltaFrames;
#ifndef TARGET_N64
    /* g_PortFieldsTicked (the stat pass's f/s counter) is NOT advanced here.
     * deltaFrames is the fields PRODUCED by waitForNextFrame's carry, but what
     * the simulation is actually credited with is g_ClockTimer after lvlTick's
     * pause and controls-lock checks, which zero it. Counting here made f/s
     * read a healthy 60.0 during every pause, because this runs before lvlTick
     * (boss.c) can decide the tick did not run. It is advanced in lv.c beside
     * `g_GlobalTimer += g_ClockTimer`, so the two are equal by construction. */
#endif

    #ifdef BUGFIX_R1
    jpD_800484CC = (f32) deltaFrames;
    #ifdef REFRESH_PAL
    jpD_800484D0 = (jpD_800484CC * 60.0f) / 50.0f;
    #else
    jpD_800484D0 = (f32) jpD_800484CC;
    #endif
    #endif

    previousFrameCounter = (s32) halfFrameCounter;
    halfFrameCounter = (s32) (currentFrameCounter / 2);
    isFrameCounterOdd = (s32) (currentFrameCounter & 1);
    halfMinusPreviousCounter = (s32) (halfFrameCounter - previousFrameCounter);

#ifndef TARGET_N64
    /* Default: the float delta IS the integer delta. waitForNextFrame
     * overwrites it immediately after this call when sub-ticking; demo
     * playback (ramromreplay.c) calls straight in here and keeps the
     * vanilla whole-field value. */
    portTickDelta = (f32) deltaFrames;

    if (deltaFrames > 0)
    {
        portTickCadence = deltaFrames;
    }
#endif
}


#ifndef TARGET_N64
/**
 * DELTA SMOOTHING for the sub-field path.
 *
 * The per-tick delta is measured, which keeps game speed exact but passes
 * scheduling noise straight into the sim. With vsync the frames are presented
 * at even intervals while the ticks behind them were measured whenever the
 * blocking swap returned (at a vsync-locked 120Hz the raw delta can swing
 * 0.22..0.85 fields around 0.5). Evenly presented uneven steps look like judder
 * in the camera and in moving guards at once.
 *
 * Fix: give the sim a low-pass estimate of the tick period and BANK the
 * difference against wall time, paying it back slowly so the long run stays
 * exact (the bank is capped at half a period; beyond that it is repaid at
 * once). A tick more than 75% of a period away from the estimate is a real
 * hitch or rate change: it passes through untouched and quickly re-seats the
 * estimate, so stalls are not smeared into lag.
 *
 * Only the float delta is smoothed; the integer field carry (g_ClockTimer)
 * stays on real elapsed time so the discrete 60Hz cadence never drifts.
 * GE_NODELTASMOOTH=1 (or GE_NOSMOOTH=1) restores the raw delta.
 */
static f32 portTickSmoothElapsed(f32 measured)
{
    extern char *getenv(const char *);
    static s32 on = -1;
    static f32 ema = 0.0f;      /* estimated tick period, counts */
    static f32 bank = 0.0f;     /* wall time owed to the sim, counts (+ = sim behind) */
    f32 used;
    f32 diff;

    if (on < 0)
    {
        const char *e = getenv("GE_NODELTASMOOTH");
        const char *e2 = getenv("GE_NOSMOOTH");
        on = ((e != NULL && e[0] != '\0' && e[0] != '0')
           || (e2 != NULL && e2[0] != '\0' && e2[0] != '0')) ? 0 : 1;
    }

    if (!on || measured <= 0.0f)
    {
        return measured;
    }

    if (ema <= 0.0f)
    {
        ema = measured;
    }

    diff = measured - ema;

    if (diff > 0.75f * ema || diff < -0.75f * ema)
    {
        /* Hitch or rate change (more than 75% off the estimate): pass it
         * through and re-seat the estimate quickly. Keep the bank rather than
         * zeroing it, because zeroing threw away real time on every hitch and
         * caused a load-dependent speed wobble below 60fps. */
        ema += diff * 0.25f;
        return measured;
    }

    ema += diff * (1.0f / 16.0f);
    used = ema;
    bank += measured - used;

    if (bank > 0.5f * ema || bank < -0.5f * ema)
    {
        used += bank;           /* drifted half a period: repay now */
        bank = 0.0f;

        if (used < 0.5f * ema)
        {
            /* never hand out a near-zero tick after a hitch; keep owing */
            bank = used - 0.5f * ema;
            used = 0.5f * ema;
        }
    }
    else
    {
        f32 b = bank * (1.0f / 8.0f);

        used += b;
        bank -= b;
    }

    return (used > 0.0f) ? used : measured;
}
#endif

/**
 * Waits until the appropriate time has passed before updating the frame counters.
 * This function effectively controls the frame rate by waiting for the next tick.
 */
void waitForNextFrame(void) //maybe WaitForTick
{
  u32 nextFrameTime; //next frame time?

#ifndef TARGET_N64
  if (portTickHz() != 60)
  {
      /* Any tick rate other than 60: from one elapsed-time reading, derive
       * the fractional delta (portTickDelta) and the whole fields done, with
       * the remainder banked in portTickFieldAccum so no time is lost. At
       * 60Hz this path is skipped so classic stays identical to vanilla. */
      u32 now = osGetCount();
      u32 elapsed = now - copy_of_osgetcount_value_1;
      s32 whole;
      u32 cap = portTickCreditCap();

      frameDelay = 1;

      /* Stall cap: a tick advances at most `cap` fields and the rest of a stall
       * is dropped, so a long hitch such as a window drag is not simulated as
       * one huge step. Report the drop here, where both halves are still
       * known. `cap` is portTickCreditCap(): still 8 unless a hw-pacing hold
       * was larger, in which case the hold is admitted. */
      if (cap > 0 && elapsed > cap * PORT_TICK_FIELD_COUNTS)
      {
          portTickNoteClamp(elapsed, cap);
          elapsed = cap * PORT_TICK_FIELD_COUNTS;
          portTickFieldAccum = 0;
      }

      portTickFieldAccum += elapsed;
      whole = (s32) (portTickFieldAccum / PORT_TICK_FIELD_COUNTS);
      portTickFieldAccum -= (u32) whole * PORT_TICK_FIELD_COUNTS;

      updateFrameCounters(whole);

      /* updateFrameCounters re-read the clock; use the reading this tick
       * was actually measured against, so the time between the two reads
       * lands in the NEXT tick instead of being lost. */
      copy_of_osgetcount_value_1 = now;
      portTickDelta = portTickSmoothElapsed((f32) elapsed) / (f32) PORT_TICK_FIELD_COUNTS;

      { /* GE_TICKDBG=1: once a second, print the spread of the measured
         * per-tick delta (raw) and of the smoothed delta the sim gets (used),
         * in fields. */
          extern char *getenv(const char *);
          extern int printf(const char *, ...);
          extern float sqrtf(float);
          static s32 dbg = -1;
          static u32 acc_counts;
          static s32 n;
          static f32 rmn, rmx, rsum, rsq, umn, umx, usum, usq;

          if (dbg < 0)
          {
              dbg = (getenv("GE_TICKDBG") != NULL);
              rmn = umn = 1e9f;
          }

          if (dbg)
          {
              f32 r = (f32) elapsed / (f32) PORT_TICK_FIELD_COUNTS;
              f32 u = portTickDelta;

              if (r < rmn) rmn = r;
              if (r > rmx) rmx = r;
              if (u < umn) umn = u;
              if (u > umx) umx = u;
              rsum += r; rsq += r * r;
              usum += u; usq += u * u;
              n++;
              acc_counts += elapsed;

              if (acc_counts >= PORT_TICK_FIELD_COUNTS * 60u)
              {
                  f32 rmean = rsum / (f32) n;
                  f32 rvar = (rsq / (f32) n) - (rmean * rmean);
                  f32 umean = usum / (f32) n;
                  f32 uvar = (usq / (f32) n) - (umean * umean);

                  printf("ge: tickdbg: ticks=%d (%.1f Hz) raw mean=%.4f min=%.4f max=%.4f sd=%.4f | used mean=%.4f min=%.4f max=%.4f sd=%.4f\n",
                         n, 60.0f / rmean,
                         rmean, rmn, rmx, (rvar > 0.0f) ? sqrtf(rvar) : 0.0f,
                         umean, umn, umx, (uvar > 0.0f) ? sqrtf(uvar) : 0.0f);
                  acc_counts = 0;
                  n = 0;
                  rmn = umn = 1e9f;
                  rmx = umx = 0.0f;
                  rsum = rsq = usum = usq = 0.0f;
              }
          }
      }

      /* Never hand out a zero measured delta: many sites divide by
       * g_GlobalTimerDelta unguarded, and two clock reads in the same
       * microsecond give zero elapsed. 1e-4 fields is unobservable; the
       * intentional 0.0f that lv.c writes when paused is vanilla and not
       * affected. */
      if (portTickDelta < 1e-4f)
      {
          portTickDelta = 1e-4f;
      }
      return;
  }
#endif

#ifndef TARGET_N64
  /* Classic 60Hz path below 60fps: credit whole fields but carry the
   * leftover fraction to the next tick, so game speed matches real time
   * instead of drifting with frame rate. The spin below keeps every tick at
   * least 1 field, so demo recording never writes its 0 end marker. */
  if (portTickCarryOff() == 0)
  {
      u32 now;
      u32 elapsed;
      s32 whole;
      u32 cap = portTickCreditCap();

      do {
          now = osGetCount();
          elapsed = now - copy_of_osgetcount_value_1;
      } while (portTickFieldAccum + elapsed < PORT_TICK_FIELD_COUNTS * (u32) frameDelay);

      frameDelay = 1;

      /* Stall cap - same policy and knob as the general path above, and the
       * same report. */
      if (cap > 0 && elapsed > cap * PORT_TICK_FIELD_COUNTS)
      {
          portTickNoteClamp(elapsed, cap);
          elapsed = cap * PORT_TICK_FIELD_COUNTS;
          portTickFieldAccum = 0;
      }

      portTickFieldAccum += elapsed;
      whole = (s32) (portTickFieldAccum / PORT_TICK_FIELD_COUNTS);
      portTickFieldAccum -= (u32) whole * PORT_TICK_FIELD_COUNTS;

      updateFrameCounters(whole);

      /* Keep the reading this tick was measured against (see the general
       * path): the time between the spin's read and updateFrameCounters'
       * re-read belongs to the NEXT tick, not nowhere. */
      copy_of_osgetcount_value_1 = now;
      return;
  }
#endif

  do {
    #ifdef REFRESH_PAL
    nextFrameTime = ((osGetCount() - copy_of_osgetcount_value_1) + 465525) / 931050; 
    #else
    nextFrameTime = ((osGetCount() - copy_of_osgetcount_value_1) + 387937) / 775875; //current time + 1/5
    #endif
  } while (nextFrameTime < frameDelay);

  frameDelay = 1;
#ifndef TARGET_N64
  /* tickhz=60 arm of the stall cap above (same policy, same knob). */
  if (portTickMaxStep() > 0 && nextFrameTime > (u32) portTickMaxStep())
  {
      nextFrameTime = (u32) portTickMaxStep();
  }
#endif
  updateFrameCounters(nextFrameTime);
}


void setFrameDelay(s32 arg0) {
    #ifdef LEFTOVERDEBUG
    frameDelay = arg0;
    #endif
}

#ifdef VERSION_EU
void eu_sub_7f0c00a4(void)
{
  
}
#endif




