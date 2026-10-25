/**
 * hitchlog.c - explains what a long frame was spent on.
 *
 * The 10-second stats line reports the worst frame in its window but not what
 * that frame was doing. Each frame whose present-to-present interval reaches
 * GE_HITCHMS (default 25 ms, 0 = off) prints one line splitting it up:
 *
 *   render  gl = gfx_run minus its waits, swap = SDL_GL_SwapWindow,
 *           lim = the max-fps limiter, idle = the rest of the interval
 *           (render thread waiting for the game's next frame)
 *   sim     longest stretch of game code between two top-level pumps (tick +
 *           display-list build + any loading), pacer = time the pump slept
 *           on its schedule, ticks = g_GlobalTickCount advance, fld = the
 *           fields credited to the sim (g_PortFieldsTicked). `credit N of M`
 *           appears when the stall cap fired: a tick measured M fields of
 *           elapsed time (GE_TICKMAXSTEP) and was credited N. Under tickhz=hw
 *           `pacer asked N` is the field hold the pacer last priced
 *           (g_PortHwFieldsHeld; read, never recomputed - see below).
 *
 *           `credit` and `pacer asked` are NOT baselines for each other. On a
 *           renderer slower than the hw model a frame spans far more wall time
 *           than either figure, so the old `fld` vs `model held` reading said
 *           "healthy" at exactly the moment the most simulated time was being
 *           dropped. `credit` is published by the clamp itself, which is the
 *           only place that knows both halves; its presence on a line is the
 *           signal that time was lost, and the shortfall is M - N.
 *   events  since the previous frame, count (total ms) of each timed kind:
 *           rooms (bgLoadRoomModelData), models (load_object_fill_header:
 *           file + ROM conversion + textures), tex (texLoad inflate),
 *           shaders (GL compile + link), uploads (texture decode + upload on
 *           a cache miss)
 *
 * How to read it:
 *  - big idle + big sim: the game thread (an event column usually says why)
 *  - big gl: the renderer (shaders/uploads say why)
 *  - big swap: the driver or compositor
 *  - big idle, short sim, big pacer: the pump overslept (OS scheduling)
 * With the render thread off, the frame runs inside the game's pump, so sim
 * includes gl.
 *
 * Counters are atomics bumped from both threads and reset by the render thread
 * once per frame. Output is limited to 10 lines a second.
 */
#include <stdio.h>
#include <stdlib.h>

#define HITCH_CATS 5
static const char *const sHitchName[HITCH_CATS] = { "rooms", "models", "tex", "shaders", "uploads" };

static unsigned int sHitchCount[HITCH_CATS];
static unsigned long long sHitchUs[HITCH_CATS];
static unsigned long long sHitchSimMaxUs;

/* cat: 0 rooms, 1 models, 2 tex, 3 shaders, 4 uploads */
void portHitchNote(int cat, unsigned long long us)
{
    if (cat < 0 || cat >= HITCH_CATS)
    {
        return;
    }
    __atomic_fetch_add(&sHitchCount[cat], 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&sHitchUs[cat], us, __ATOMIC_RELAXED);
}

/* pcsched.c: one stretch of game code between two top-level pumps */
void portHitchSimSpan(unsigned long long us)
{
    unsigned long long cur = __atomic_load_n(&sHitchSimMaxUs, __ATOMIC_RELAXED);

    while (us > cur && !__atomic_compare_exchange_n(&sHitchSimMaxUs, &cur, us, 0,
                                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
    }
}

#ifdef GE_VIDEO /* the frame end lives in the renderer; headless only keeps the counters */
static int s_Us = -1;   /* the threshold in us: -1 = not read yet, 0 = off (console cl_hitchms) */

static int portHitchThresholdUs(void)
{
    if (s_Us < 0)
    {
        const char *e = getenv("GE_HITCHMS");

        s_Us = (e != NULL && *e != '\0') ? atoi(e) * 1000 : 25000;
        if (s_Us < 0)
        {
            s_Us = 0;
        }
        if (s_Us > 0)
        {
            printf("ge: hitchlog: frames of %d ms or more are logged as 'ge: hitch:' (GE_HITCHMS, 0 = off)\n", s_Us / 1000);
            fflush(stdout);
        }
    }
    return s_Us;
}

/** console cl_hitchms: the threshold in ms (0 = off) */
int portHitchMs(void)
{
    return portHitchThresholdUs() / 1000;
}

/** console cl_hitchms <ms>: set it live (0 = off). The render thread reads one int. */
void portHitchSetMs(int ms)
{
    (void) portHitchThresholdUs();   /* the env read and its banner happen first, once */
    s_Us = (ms > 0) ? ms * 1000 : 0;
}

/**
 * Render thread, once per presented frame (gfx_sdl2.cpp, after the swap).
 * interval = since the previous present; run/swap/lim = this frame's gfx_run
 * so far and its two waits; draws = this frame's draw batches.
 */
void portHitchFrame(unsigned long long interval_us, unsigned long long run_us,
                    unsigned long long swap_us, unsigned long long lim_us, unsigned int draws)
{
    extern unsigned long long g_PortPumpSleepUs;
    extern int g_GlobalTickCount;     /* s32 */
    extern int g_StageNum;            /* s32, boss.c: the running stage */
    extern int portTickHz(void);      /* s32 */
    extern int portRenderAsyncActive(void);
    extern unsigned int g_PortFieldsTicked; /* frametiming.c: fields credited to the sim */
    static unsigned long long s_LastSleep;
    static int s_LastTick, s_Primed;
    static unsigned int s_LastFields;
    static unsigned long long s_SecondUs;
    static int s_Lines, s_Dropped;
    const int threshold = portHitchThresholdUs();
    const unsigned long long sleep = g_PortPumpSleepUs;
    const int tick = g_GlobalTickCount;
    const unsigned long long simmax = __atomic_exchange_n(&sHitchSimMaxUs, 0ull, __ATOMIC_RELAXED);
    unsigned int n[HITCH_CATS];
    unsigned long long us[HITCH_CATS];
    int i;

    for (i = 0; i < HITCH_CATS; i++)
    {
        n[i] = __atomic_exchange_n(&sHitchCount[i], 0u, __ATOMIC_RELAXED);
        us[i] = __atomic_exchange_n(&sHitchUs[i], 0ull, __ATOMIC_RELAXED);
    }

    /* In hardware-paced mode (tickhz=hw) the pacer deliberately sleeps to match
     * the N64's frame time, so only time spent beyond that counts as a hitch. */
    extern int portTickHwPace(void);   /* frametiming.c */
    /* The field hold the pacer priced, READ ONLY. This used to call
     * portHwPaceFields(), which is wrong here: this function runs at present time
     * (the render thread when render_thread=1) and that call mutates the model and
     * consumes the pending load charge. */
    extern int g_PortHwFieldsHeld;     /* frametiming.c, written by the game thread */
    extern unsigned int g_PortTickClampSeq;      /* frametiming.c, the stall cap */
    extern unsigned int g_PortTickClampMeasured;
    extern unsigned int g_PortTickClampCredited;
    const unsigned long long slept = sleep - s_LastSleep;
    const unsigned long long judged = (portTickHwPace() && interval_us > slept) ? interval_us - slept : interval_us;
    const unsigned int dfld = __atomic_load_n(&g_PortFieldsTicked, __ATOMIC_RELAXED) - s_LastFields;
    char hw[64];

    /* Two independent facts about the frame that just presented, neither of
     * which is a baseline for the other:
     *
     *   `credit N of M`  the stall cap's own verdict, published by
     *                    waitForNextFrame where it clamps: a tick measured M
     *                    fields of elapsed time and was credited N. It prints
     *                    only on a frame where the cap fired, so its presence
     *                    is the signal, and M - N is the time not simulated.
     *   `pacer asked N`  the hold the hw model priced for this frame. It is
     *                    not a prediction of wall time: on a renderer slower
     *                    than the model the frame spans far more than N, which
     *                    is exactly when `credit` appears beside it.
     *
     * The pair belongs to one clamp only if the sequence is unchanged across
     * the two value loads; a render-thread reader can otherwise catch half of
     * one verdict and half of the next. */
    hw[0] = '\0';
    {
        static unsigned int s_LastClampSeq;
        const unsigned int seq = __atomic_load_n(&g_PortTickClampSeq, __ATOMIC_ACQUIRE);
        int n = 0;

        if (seq != s_LastClampSeq)
        {
            const unsigned int measured = __atomic_load_n(&g_PortTickClampMeasured, __ATOMIC_RELAXED);
            const unsigned int credited = __atomic_load_n(&g_PortTickClampCredited, __ATOMIC_RELAXED);

            if (seq == __atomic_load_n(&g_PortTickClampSeq, __ATOMIC_ACQUIRE) && credited < measured)
            {
                n = snprintf(hw, sizeof hw, " credit %u of %u", credited, measured);
            }
            s_LastClampSeq = seq;
        }
        if (portTickHwPace())
        {
            /* Relaxed load: this runs on the render thread when render_thread=1
             * and the writer is the game thread. It is loaded atomically to
             * match the store, since a torn int would print a nonsense field
             * count rather than a merely stale one.
             *
             * 0 means "no hold priced yet" (the model has not seen a frame),
             * NOT "the model asked for zero fields" - a frame is always at
             * least one field - so print nothing rather than a 0 a reader
             * would misread. */
            const int held = __atomic_load_n(&g_PortHwFieldsHeld, __ATOMIC_RELAXED);

            if (held > 0 && n >= 0 && n < (int) sizeof hw)
            {
                snprintf(hw + n, sizeof hw - (size_t) n, " pacer asked %d", held);
            }
        }
    }

    if (threshold > 0 && judged >= (unsigned long long) threshold && s_Primed)
    {
        if (s_Lines < 10)
        {
            const unsigned long long gl = run_us > swap_us + lim_us ? run_us - swap_us - lim_us : 0;
            const unsigned long long idle = interval_us > run_us ? interval_us - run_us : 0;
            char ev[256];
            int len = 0;

            ev[0] = '\0';
            for (i = 0; i < HITCH_CATS; i++)
            {
                if (n[i] != 0 && len < (int) sizeof(ev) - 48)
                {
                    len += snprintf(ev + len, sizeof(ev) - (size_t) len, " %s %u (%llu.%llu ms)",
                                    sHitchName[i], n[i], us[i] / 1000, (us[i] % 1000) / 100);
                }
            }
            printf("ge: hitch: %llu.%llu ms frame (stage %d, tick%d, render thread %s) | render: gl %llu.%llu swap %llu.%llu lim %llu.%llu idle %llu.%llu"
                   " | sim: longest %llu.%llu ms, pacer %llu.%llu ms, %d tick(s) %u fld%s%s | draws %u |%s%s\n",
                   interval_us / 1000, (interval_us % 1000) / 100, g_StageNum, portTickHz(),
                   portRenderAsyncActive() ? "on" : "off",
                   gl / 1000, (gl % 1000) / 100, swap_us / 1000, (swap_us % 1000) / 100,
                   lim_us / 1000, (lim_us % 1000) / 100, idle / 1000, (idle % 1000) / 100,
                   simmax / 1000, (simmax % 1000) / 100,
                   (sleep - s_LastSleep) / 1000, ((sleep - s_LastSleep) % 1000) / 100,
                   tick >= s_LastTick ? tick - s_LastTick : tick,
                   dfld,
                   hw,
                   tick >= s_LastTick ? "" : " (clock reset)", draws, len ? ev : " no timed events",
                   s_Dropped ? " (more hitches were dropped by the rate limit)" : "");
            fflush(stdout);
            s_Lines++;
            s_Dropped = 0;
        }
        else
        {
            s_Dropped++;
        }
    }
    s_SecondUs += interval_us;
    if (s_SecondUs >= 1000000ull)
    {
        s_SecondUs = 0;
        s_Lines = 0;
    }
    s_LastSleep = sleep;
    s_LastTick = tick;
    s_LastFields = __atomic_load_n(&g_PortFieldsTicked, __ATOMIC_RELAXED);
    s_Primed = 1;
}
#endif
