/*
 * ROM-free, device-free tests for the audio pump's pacing policy.
 *
 * port/src/audio_pace.c holds the decisions as pure functions, so these tests
 * drive the shipped code rather than a model of it. A test written against a
 * reimplementation only proves the test agrees with itself.
 *
 * Two of these cases exist because the first cut of the policy passed every
 * test it had and still starved a real device:
 *
 *   - test_simulation_holds_the_queue: a wall-clock pace
 *     plus a bounded queue bias looks right and cannot converge, because the
 *     queue level is then a constant of integration. Measured on hardware it
 *     sat at 1.2-3.6 KB against a 10 KB target.
 *   - test_simulation_at_high_frame_rate: rounding the refill DOWN to the
 *     synthesizer's 16-sample alignment loses a few samples per pump, which at
 *     300 fps drains the queue at ~11% faster than it is filled.
 *
 * Both were found by running the game and reading its audio log, not by
 * reasoning. Prefer a device-facing measurement over a convincing argument.
 *
 * Build standalone:
 *   cc -std=c99 -Iport/include port/tests/audio_pace.c port/src/audio_pace.c -o t
 *   ./t
 */

#include <stdio.h>
#include <stdlib.h>

#include "audio_pace.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

#define RATE        22050u
#define ALIGN       16u
#define MAX_FRAME   1024u
#define TARGET      3528u   /* 40 ms at 22050 Hz stereo s16 */
#define TARGET_MAX  10240u
#define TARGET_STEP 1024u
#define TARGET_EASE 256u
#define CATCHUP_MS  250u
#define DRAIN_MS    2000u
#define DEV_PERIOD_MS 12u   /* 256-sample device period */
#define BYTES_PER_SAMPLE 4u
#define CAP         16384u  /* fixed floor; the real cap scales with the interval */
#define MAX_INTERVAL_MS 1000u /* covers down to 1 fps */
#define PUMP_MS     17u     /* a 60 Hz-ish pump interval, for watchdog pacing */

static const PortAudioPaceCfg cfg = {
    RATE, ALIGN, MAX_FRAME, TARGET, TARGET_MAX, TARGET_STEP, TARGET_EASE,
    CATCHUP_MS, DRAIN_MS, DEV_PERIOD_MS, CAP, MAX_INTERVAL_MS
};

/* ---- refill: the device-paced path -------------------------------------- */

static void test_refill_is_aligned_and_clamped(void)
{
    unsigned q;

    /* At or above the mark: leave it to the device. */
    CHECK(portAudioPaceRefill(TARGET, TARGET, &cfg) == 0);
    CHECK(portAudioPaceRefill(TARGET + 4096, TARGET, &cfg) == 0);

    /* Never more than one frame, always aligned, and never 0 while a full
     * aligned unit is missing. Below that the device is effectively keeping
     * up, and calling alAudioFrame for a few samples is not worth the work. */
    for (q = 0; q < TARGET; q += 8) {
        unsigned n = portAudioPaceRefill(q, TARGET, &cfg);
        unsigned deficit = TARGET - q;

        CHECK(n <= MAX_FRAME);
        CHECK(n % ALIGN == 0);
        if (deficit >= ALIGN * BYTES_PER_SAMPLE) {
            CHECK(n > 0);
            /* Close to the deficit: within one aligned unit of rounding up. */
            CHECK(n * BYTES_PER_SAMPLE >= deficit);
            CHECK(n * BYTES_PER_SAMPLE <= deficit + ALIGN * BYTES_PER_SAMPLE);
        } else {
            CHECK(n == 0);
        }
    }

    /* A deep hole is capped at one frame rather than asked for all at once. */
    CHECK(portAudioPaceRefill(0, 16384u, &cfg) == MAX_FRAME);
    CHECK(portAudioPaceRefill(0, 16384u, &cfg) % ALIGN == 0);
}

static void test_refill_does_not_drain_when_pumping_fast(void)
{
    /* The alignment-rounding trap, isolated: pumping far faster than the
     * device drains must still fill, never systematically under-refill. */
    unsigned queued = TARGET;
    unsigned long long rendered = 0;
    unsigned i;
    unsigned n;

    for (i = 0; i < 1000; i++) {
        /* the device drains a trickle, less than one aligned unit */
        if (queued >= 8 * BYTES_PER_SAMPLE) {
            queued -= 8 * BYTES_PER_SAMPLE;
        }

        n = portAudioPaceRefill(queued, TARGET, &cfg);
        if (n != 0) {
            rendered += n;
            queued += n * BYTES_PER_SAMPLE;
            CHECK(queued <= TARGET + ALIGN * BYTES_PER_SAMPLE);
        }
    }

    /* Every drain was eventually made up: the queue did not walk downward. */
    CHECK(rendered > 0);
    CHECK(queued >= TARGET - ALIGN * BYTES_PER_SAMPLE);
}

/* ---- the wall clock: the no-device path --------------------------------- */

static void test_clock_holds_wall_time(void)
{
    PortAudioPaceClock c;
    unsigned long long rendered = 0;
    unsigned i;

    portAudioPaceClockReset(&c);

    /* 1 ms intervals: 22.05 samples of credit per call. A design that rounds
     * per call would either render nothing or double the rate. */
    for (i = 0; i < 1000; i++) {
        unsigned n = portAudioPaceFrame(&c, 1, 0, TARGET, 0, &cfg);

        CHECK(n % ALIGN == 0);
        CHECK(n <= MAX_FRAME);
        rendered += n;
    }

    CHECK(rendered > RATE - ALIGN);
    CHECK(rendered < RATE + ALIGN);
}

static void test_clock_at_field_rate_is_exact(void)
{
    PortAudioPaceClock c;
    unsigned long long rendered = 0;
    unsigned i;
    unsigned ms_acc = 0;

    portAudioPaceClockReset(&c);

    /* 60 Hz for 60 s, carrying the 16.667 ms field in integer ms. */
    for (i = 0; i < 3600; i++) {
        unsigned dt;

        ms_acc += 1000;
        dt = ms_acc / 60;
        ms_acc -= dt * 60;

        while (1) {
            unsigned n = portAudioPaceFrame(&c, dt, 0, TARGET, 0, &cfg);

            dt = 0;
            if (n == 0) {
                break;
            }
            rendered += n;
        }
    }

    /* 60 s of audio within two aligned frames: music cannot drag or run fast. */
    CHECK(rendered > (unsigned long long) RATE * 60 - 2 * ALIGN);
    CHECK(rendered < (unsigned long long) RATE * 60 + 2 * ALIGN);
}

static void test_clock_catchup_is_bounded(void)
{
    PortAudioPaceClock c;
    unsigned long long rendered = 0;
    unsigned n;

    portAudioPaceClockReset(&c);

    /* A 10 s stall must credit at most catchup_ms, or the pump would replay it.
     *
     * Note for anyone mutation-testing this function: the `dt_ms > catchup_ms`
     * clamp below and the `credit > catchup*rate` ceiling further down are
     * redundant on purpose - either one alone bounds the burst, so deleting
     * either single one leaves this test passing. Delete both and it fails.
     * The clamp is what a reader expects; the ceiling is the backstop that does
     * not depend on the caller passing any particular dt. */
    while ((n = portAudioPaceFrame(&c, rendered == 0 ? 10000u : 0u, 0, TARGET, 0, &cfg)) != 0) {
        rendered += n;
    }
    CHECK(rendered > 0);
    CHECK(rendered <= (unsigned long long) RATE * CATCHUP_MS / 1000u + MAX_FRAME);

    /* And it must not still owe the stall afterwards. */
    rendered = 0;
    while ((n = portAudioPaceFrame(&c, 0, 0, TARGET, 0, &cfg)) != 0) {
        rendered += n;
    }
    CHECK(rendered <= ALIGN);
}

static void test_clock_rests_while_a_device_paces(void)
{
    PortAudioPaceClock c;
    unsigned n;

    portAudioPaceClockReset(&c);

    /* Accumulate credit, then a device appears. The clock must be discarded, so
     * that a device failing later does not replay a phantom backlog. */
    n = portAudioPaceFrame(&c, 50, 0, TARGET, 0, &cfg);
    CHECK(n > 0);

    CHECK(portAudioPaceFrame(&c, 50, TARGET, TARGET, 1, &cfg) == 0);
    CHECK(c.credit == 0);

    /* Now the device dies: the clock starts from nothing, not from 50 ms. */
    n = portAudioPaceFrame(&c, 0, 0, TARGET, 0, &cfg);
    CHECK(n == 0);
}

/* ---- target ratchet ----------------------------------------------------- */

static void test_target_ratchets_and_eases(void)
{
    PortAudioPaceTarget t = {0, 0};
    unsigned v;
    int i;

    CHECK(portAudioPaceTargetStep(&t, 0, 0, &cfg) == TARGET);

    v = portAudioPaceTargetStep(&t, 1, 0, &cfg);
    CHECK(v == TARGET + TARGET_STEP);

    for (i = 0; i < 100; i++) {
        v = portAudioPaceTargetStep(&t, 1, 0, &cfg);
    }
    CHECK(v == TARGET_MAX);

    v = portAudioPaceTargetStep(&t, 0, 1, &cfg);
    CHECK(v == TARGET_MAX - TARGET_EASE);
    for (i = 0; i < 1000; i++) {
        v = portAudioPaceTargetStep(&t, 0, 1, &cfg);
    }
    CHECK(v == TARGET);
}

/* ---- drain watchdog ----------------------------------------------------- */

static void test_drain_watchdog(void)
{
    PortAudioPaceDrain d = {0, 0, 0, 0};
    unsigned deep = TARGET;
    unsigned shallower = TARGET - 1200u;

    CHECK(portAudioPaceDrainObserve(&d, deep, deep, 0, &cfg) == 0);

    /* A device consuming between pumps is never stalled, however long it runs:
     * this is the case a "did the byte count change?" detector got wrong. */
    {
        unsigned now = 0;
        int i;

        for (i = 0; i < 1000; i++) {
            now += 17;
            CHECK(portAudioPaceDrainObserve(&d, shallower, deep, now, &cfg) == 0);
        }
    }

    /* Frozen backlog for drain_ms is a stall. */
    {
        unsigned now = 100000;
        PortAudioPaceDrain s = {0, 0, 0, 0};

        CHECK(portAudioPaceDrainObserve(&s, deep, deep, now, &cfg) == 0);
        CHECK(portAudioPaceDrainObserve(&s, deep, deep, now + DRAIN_MS - 1, &cfg) == 0);
        CHECK(portAudioPaceDrainObserve(&s, deep, deep, now + DRAIN_MS, &cfg) == 1);

        /* It wakes up again as soon as it consumes. */
        CHECK(portAudioPaceDrainObserve(&s, shallower, deep, now + DRAIN_MS + 17, &cfg) == 0);
    }

    /* A queue pinned at empty is consuming everything, not stalled. */
    {
        PortAudioPaceDrain e = {0, 0, 0, 0};
        unsigned now = 0;
        int i;

        CHECK(portAudioPaceDrainObserve(&e, 0, 0, now, &cfg) == 0);
        for (i = 0; i < 1000; i++) {
            now += 17;
            CHECK(portAudioPaceDrainObserve(&e, 0, 0, now, &cfg) == 0);
        }
    }
}

static void test_watchdog_measures_across_pumps_not_within_one(void)
{
    /* The contract above is explicit: consumption is measured from the PREVIOUS
     * pump's closing depth to THIS pump's opening depth, "not against the
     * reading just before it". The distinction is load-bearing whenever the
     * pump does not refill - a queue sitting above the target - because then a
     * pump closes where it opened. A within-pump comparison (closed != opened)
     * sees that as no consumption at all and declares a healthy, draining sink
     * dead once drain_ms has passed.
     *
     * So: byte-identical WITHIN every pump, strictly decreasing BETWEEN pumps.
     * The cross-pump rule tracks the device; a within-pump rule would stall.
     * The watchdog test above does not cover this, because its healthy sequence
     * refills (opened != closed) and a within-pump rule would read that refill
     * as consumption and pass. */
    PortAudioPaceDrain d = {0, 0, 0, 0};
    unsigned now = 0;
    unsigned q = 4096; /* above the target, so the pump has nothing to refill */
    unsigned pumps = cfg.drain_ms / PUMP_MS + 16u; /* derived: outlast drain_ms */
    unsigned i;

    CHECK(q > TARGET); /* genuinely above the mark, or the pump would refill */
    CHECK(portAudioPaceDrainObserve(&d, q, q, now, &cfg) == 0);
    for (i = 0; i < pumps; i++) {
        now += PUMP_MS;
        q -= 24; /* ~16% of a 22050 Hz stereo drain; the pump closed where it opened */
        CHECK(q > 0); /* never wrap: the case must stay the one described */
        CHECK(portAudioPaceDrainObserve(&d, q, q, now, &cfg) == 0);
    }
    /* The loop must have outlasted the threshold, or this test would pass with
     * a within-pump detector and prove nothing. Derived from cfg.drain_ms
     * rather than hard-coded, so retuning the config cannot silently gut it. */
    CHECK(now > cfg.drain_ms);

    /* And the contrast, so the case above cannot be satisfied by never
     * flagging a stall: a queue identical within AND across pumps is a frozen
     * device, and must be reported. */
    {
        PortAudioPaceDrain f = {0, 0, 0, 0};
        unsigned t = 0;

        CHECK(portAudioPaceDrainObserve(&f, q, q, t, &cfg) == 0);
        CHECK(portAudioPaceDrainObserve(&f, q, q, t + DRAIN_MS, &cfg) == 1);
    }
}

/* ---- configuration guards and saturation -------------------------------- */

static void test_refill_rejects_an_unusable_config(void)
{
    PortAudioPaceCfg z = cfg;

    /* frame_align == 0 would make the alignment modulo a division by zero, and
     * this is the case that actually pins the guard: on x86 it traps, on arm64
     * it silently yields 0. Reached on both the refill and the clock path. */
    z.frame_align = 0;
    CHECK(portAudioPaceRefill(0, TARGET, &z) == 0);

    /* max_frame < frame_align cannot express one aligned unit. Honest note:
     * now that the clamp rounds DOWN, this returns 0 even without the early
     * guard, so the assertion documents the contract rather than pinning that
     * branch. Kept because a caller reading this expects 0, and because the
     * guard makes the intent explicit at the top of the function. */
    z = cfg;
    z.max_frame = ALIGN - 1;
    CHECK(portAudioPaceRefill(0, TARGET, &z) == 0);

    /* The same guard on the clock path. */
    {
        PortAudioPaceClock c;
        PortAudioPaceCfg w = cfg;

        portAudioPaceClockReset(&c);
        w.frame_align = 0;
        CHECK(portAudioPaceFrame(&c, 17, 0, TARGET, 0, &w) == 0);
        CHECK(portAudioPaceFrame(&c, 17, 0, TARGET, 1, &w) == 0);
    }
}

static void test_refill_stays_aligned_when_the_clamp_binds(void)
{
    /* max_frame is not required to be a multiple of frame_align. When the
     * clamp binds, rounding UP to the alignment overshoots it, so the request
     * has to come back down to the largest aligned value that still fits.
     * Before this was pinned, max_frame=1000/align=16 asked for 1000 samples
     * (1000 % 16 == 8) - unaligned, and therefore not what alAudioFrame wants. */
    PortAudioPaceCfg odd = cfg;
    unsigned targets[4] = { 4096u, 8192u, 16384u, 65536u };
    unsigned i;

    odd.max_frame = 1000;
    for (i = 0; i < 4; i++) {
        unsigned n = portAudioPaceRefill(0, targets[i], &odd);

        CHECK(n > 0);
        CHECK(n % ALIGN == 0);
        CHECK(n <= odd.max_frame);
    }
}

static void test_geometry_saturates_instead_of_wrapping(void)
{
    PortAudioPaceCfg big = cfg;
    unsigned dt;
    unsigned prev = 0;

    /* Absurd inputs clamp to UINT_MAX rather than wrapping to a tiny backlog. */
    big.rate = 0xFFFFFFFFu;
    CHECK(portAudioPaceMinTarget(1000u, &big) == 0xFFFFFFFFu);
    CHECK(portAudioPaceCap(0xFFFFFFFFu, &cfg) == 0xFFFFFFFFu);

    /* And the interval floor never falls as the pump interval grows: a slower
     * frame rate can only mean more audio to bridge, never less. */
    for (dt = 0; dt <= 2 * MAX_INTERVAL_MS; dt += 7) {
        unsigned v = portAudioPaceMinTarget(dt, &cfg);

        CHECK(v >= prev);
        prev = v;
    }
    CHECK(prev == portAudioPaceMinTarget(MAX_INTERVAL_MS, &cfg));
}

/* ---- closed-loop simulation -------------------------------------------- */

/*
 * ONE harness. Until 2026-10-25 there were two, with different physics: the
 * constant-interval one below refilled and then drained, and a second, jittered
 * one in the game's order. Two models of the same pump is a hazard by itself -
 * a case written against the wrong one proves nothing - so the cadence, the
 * jitter, the policy rule and the ORDER are all fields of one scenario now.
 *
 * THE ORDER IS THE POINT. portAudioPump() runs AFTER the interval has elapsed:
 * the device has already drained the queue, and the pump then measures that
 * depth and refills toward the target. So does this model. Refilling first tops
 * the queue up before it can be emptied, which makes starvation unobservable at
 * any frame rate under any jitter - measured, not argued: on the same scenario
 * and the same rule, refill-first gives dry=0 and the game's order gives dry=27
 * (see test_harness_order_decides_whether_a_starve_is_visible, which keeps the
 * wrong order as its own evidence). And a cadence that never varies cannot
 * starve even in the right order, because a pump that measures the interval it
 * just lost sizes the queue for it. Both blind spots are why the pump's real
 * ratchet defect survived every case in this file.
 */
typedef struct {
    unsigned min_bytes; /* the shallowest depth a pump OPENED with (post-drain) */
    unsigned max_bytes; /* the deepest depth a pump LEFT behind (post-refill) */
    unsigned end_bytes; /* the depth when the last pump closed */
    unsigned dry;       /* pumps that found the queue empty, after priming */
    long long rendered; /* samples synthesised */
    long long consumed; /* samples the device played */
} SimResult;

/* One closed-loop scenario. Every field is explicit: the cadence is the
 * variable these cases exist to vary, and a default that quietly picks one is
 * how the constant-interval blind spot got built. */
typedef struct {
    unsigned fps;         /* != 0: derive each interval from fps, as the constant
                           * cases always have (ms_acc, so 60 fps is 16/17/17 ms).
                           * 0: use base_ms. */
    unsigned base_ms;     /* constant pump interval when fps == 0 */
    unsigned jitter_pct;  /* per-pump +/- this percent of the interval; 0 = none */
    unsigned spike_every; /* every Nth pump takes spike_ms; 0 = none */
    unsigned spike_ms;
    unsigned seconds;
    unsigned start_bytes;
    unsigned cap;         /* extra floor for the cap; 0 = let the policy size it */
    long drift_ppm;       /* device rate error, parts per million */
    int use_effective;    /* 1: portAudioPaceEffectiveTarget - what the pump runs.
                           * 0: max(ratcheted, floor) - the rule before df881c9. */
    int refill_first;     /* 1: refill BEFORE the drain. NOT the pump's order;
                           * only for the order-sensitivity evidence. */
} SimScenario;

/* Refill toward `target_eff` one aligned frame at a time, bounded by `cap_eff`.
 * The device model's opening depth comes from the caller, so this only fills. */
static void simRefill(PortAudioPaceClock *c, unsigned *queued, unsigned pump_ms,
                      unsigned target_eff, unsigned cap_eff, SimResult *out)
{
    while (1) {
        unsigned n = portAudioPaceFrame(c, pump_ms, *queued, target_eff, 1, &cfg);
        unsigned bytes;

        if (n == 0) {
            break;
        }
        bytes = n * BYTES_PER_SAMPLE;
        if (*queued + bytes > cap_eff) {
            break; /* over the cap: rendered but not queued */
        }
        *queued += bytes;
        out->rendered += (long long) n;
    }
}

/* The device plays for `pump_ms` at its own rate, stopping when the queue runs
 * out - it played silence from then on, which is what the caller counts as dry.
 * Signed: a negative drift must subtract, not wrap. */
static void simDrain(unsigned *queued, unsigned pump_ms, long drift_ppm,
                     long long *consumed_acc, long long *consumed)
{
    *consumed_acc += (long long) pump_ms * (long long) RATE
                   + (long long) drift_ppm * (long long) pump_ms * (long long) RATE / 1000000;
    while (*consumed_acc >= 1000) {
        unsigned long long take = (unsigned long long) (*consumed_acc / 1000);

        *consumed_acc -= (long long) take * 1000;
        if (take > *queued / BYTES_PER_SAMPLE) {
            take = *queued / BYTES_PER_SAMPLE; /* cannot play what is not queued */
        }
        *queued -= (unsigned) (take * BYTES_PER_SAMPLE);
        *consumed += (long long) take;
    }
}

/* A bounded, deterministic re-draw of an interval: +/- pct percent, in whole ms,
 * from a hash of the pump index so a failure reproduces exactly.
 *
 * It is NOT a model of a frame-time distribution - real frame times have a tail
 * - it exercises the one property the queue has to absorb: the next interval is
 * not the one the last pump measured, and the pump does not find that out until
 * the device has already played. Bounded deliberately; an unbounded interval is
 * a hitch, not a frame rate, and no queue size covers one (audio-pump-history.md
 * section 9). */
static unsigned jitteredMs(unsigned base_ms, unsigned pct, unsigned i)
{
    unsigned span = base_ms * pct / 100u;
    unsigned h;
    int delta;

    if (span == 0) {
        return base_ms;
    }
    h = i * 2654435761u + 0x9e3779b9u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    delta = (int) (h % (2u * span + 1u)) - (int) span;

    return (unsigned) ((int) base_ms + delta);
}

/* One run of the scenario. What it does NOT model, so that no case mistakes
 * silence for coverage:
 *
 *   - the drain watchdog (`portAudioPaceDrainObserve`). It has its own cases
 *     below, and the pump's use of it does not move the queue.
 *   - the pump's per-pump render count (`PORT_AUDIO_MAX_FRAMES_PER_PUMP`): the
 *     refill loop here is unbounded, which is generous at single-digit frame
 *     rates and never binds at 60.
 *   - the real cadence. The pump is called from the retrace-duty gate in
 *     `port/src/pcsched.c` and from forced-frame paths, so *one pump per frame*
 *     is an approximation; every case here assumes exactly that, and none
 *     depends on anything subtler.
 */
static void simulateScenario(const SimScenario *s, SimResult *out)
{
    PortAudioPaceClock c;
    PortAudioPaceTarget t = {0, 0};
    long long consumed_acc = 0; /* samples x 1000 */
    unsigned queued = s->start_bytes;
    unsigned ms_acc = 0;
    unsigned pumps = (s->fps != 0) ? s->fps * s->seconds
                                  : (s->seconds * 1000u) / s->base_ms;
    unsigned i;
    int primed = 0;

    portAudioPaceClockReset(&c);
    out->min_bytes = 0xFFFFFFFFu;
    out->max_bytes = 0;
    out->end_bytes = queued;
    out->dry = 0;
    out->rendered = 0;
    out->consumed = 0;

    for (i = 0; i < pumps; i++) {
        unsigned pump_ms;
        unsigned floor_bytes;
        unsigned target_eff;
        unsigned cap_eff;
        unsigned opened;
        unsigned filled;
        unsigned ran_dry;

        if (s->fps != 0) {
            ms_acc += 1000u;
            pump_ms = ms_acc / s->fps;
            ms_acc -= pump_ms * s->fps;
        } else {
            pump_ms = s->base_ms;
        }
        if (s->jitter_pct != 0) {
            pump_ms = jitteredMs(pump_ms, s->jitter_pct, i);
        }
        if (s->spike_every != 0 && ((i + 1u) % s->spike_every) == 0) {
            pump_ms = s->spike_ms;
        }

        /* The target the pump steers toward, computed from the interval it just
         * measured - the same two rules portAudioPump() chooses between. */
        floor_bytes = portAudioPaceMinTarget(pump_ms, &cfg);
        target_eff = s->use_effective
                   ? portAudioPaceEffectiveTarget(floor_bytes, t.target, &cfg)
                   : ((floor_bytes > t.target) ? floor_bytes : t.target);
        cap_eff = portAudioPaceCap(target_eff, &cfg);
        if (s->cap > cap_eff) {
            cap_eff = s->cap;
        }

        if (s->refill_first) {
            /* The old harness's shape: top the queue up first, so it cannot be
             * empty when it is measured. Evidence only. */
            opened = queued;
        } else {
            /* The pump's shape: the interval has elapsed and the device has
             * already played, so this is the depth the pump measures. */
            simDrain(&queued, pump_ms, s->drift_ppm, &consumed_acc, &out->consumed);
            opened = queued;
        }

        ran_dry = (primed && opened == 0) ? 1u : 0u;
        if (ran_dry) {
            out->dry++;
        }
        /* One ratchet step per pump in BOTH orders, at the same point, so a
         * comparison never measures the stepping as well as the rule. */
        t.target = portAudioPaceTargetStep(&t, (int) ran_dry, 0, &cfg);

        simRefill(&c, &queued, pump_ms, target_eff, cap_eff, out);
        filled = queued;

        if (s->refill_first) {
            simDrain(&queued, pump_ms, s->drift_ppm, &consumed_acc, &out->consumed);
        }

        if (!primed) {
            /* The pump that first leaves audio in the queue is the cold start:
             * a freshly opened device IS empty, and the ratchet's own contract
             * says that is not an underrun. It primes the window rather than
             * contributing to it, the same way it is excluded from `dry`. */
            if (filled > 0) {
                primed = 1;
            }
        } else {
            if (opened < out->min_bytes) {
                out->min_bytes = opened;
            }
            if (filled > out->max_bytes) {
                out->max_bytes = filled;
            }
        }
        out->end_bytes = queued;
    }
}

/* Samples rendered minus samples consumed, less the queue's own growth: zero
 * when nothing was dropped and nothing was duplicated.
 *
 * Stating the balance is stronger than a tolerance on rendered-versus-consumed,
 * because the balance is the property that matters and the raw difference is not
 * - a run that ends with a deeper queue than it started has rendered more than
 * it played, and that is not a rate error. The old frame-sized slack hid exactly
 * that at low frame rates, where one target change is thousands of samples. */
static long long simUnaccounted(const SimResult *r, unsigned start_bytes)
{
    return (r->rendered - r->consumed)
         - (((long long) r->end_bytes - (long long) start_bytes) / (long long) BYTES_PER_SAMPLE);
}

/* The constant-interval cases below, as one call. Same cadence arithmetic as
 * before; what changed is that the depth they read is the depth the pump sees
 * rather than the depth it left. */
static void simulate(unsigned fps, long drift_ppm, unsigned seconds,
                     SimResult *out, unsigned start_bytes, unsigned cap)
{
    SimScenario s = {0};

    s.fps = fps;
    s.seconds = seconds;
    s.start_bytes = start_bytes;
    s.cap = cap;
    s.drift_ppm = drift_ppm;
    s.use_effective = 1;
    simulateScenario(&s, out);
}

static void test_simulation_holds_the_queue(void)
{
    SimResult r;

    simulate(60, 0, 30, &r, TARGET, CAP);

    CHECK(r.dry == 0);
    CHECK(r.min_bytes > TARGET / 2);
    CHECK(r.max_bytes < TARGET + MAX_FRAME * BYTES_PER_SAMPLE);
    /* Render rate equals the DAC's rate: audio time is the device's time. */
    CHECK(simUnaccounted(&r, TARGET) > -(long long) MAX_FRAME);
    CHECK(simUnaccounted(&r, TARGET) < (long long) MAX_FRAME);
}

static void test_simulation_refills_a_dry_start(void)
{
    SimResult r;

    /* A device that opens empty must be filled, not left starved. */
    simulate(60, 0, 10, &r, 0, CAP);

    /* Strictly: the first pump with a sink fills to the target, so from the
     * second pump on the depth the pump OPENS with is already above half the
     * target. (The first pump is excluded - it is the cold start, see
     * simulateScenario. Before the harness drained first, the minimum was
     * sampled after the refill and read as TARGET; that was a property of the
     * harness order, not of the pump.) The earlier `|| r.dry <= 1` escape let
     * this pass without that being true. */
    CHECK(r.min_bytes > TARGET / 2);
    CHECK(r.max_bytes >= TARGET);
    CHECK(r.consumed > 0);
}

static void test_simulation_absorbs_device_drift(void)
{
    SimResult r;
    const long drifts[4] = {100, -100, 2000, -2000};
    int i;

    /* Level control means the DAC sets the rate, so even a 0.2% crystal error
     * is absorbed: the queue holds and the sink never starves. This is the
     * property a wall-clock-budgeted design could NOT provide without letting
     * audio time drift. */
    for (i = 0; i < 4; i++) {
        simulate(60, drifts[i], 30, &r, TARGET, CAP);

        CHECK(r.dry == 0);
        CHECK(r.min_bytes > TARGET / 2);
        CHECK(r.max_bytes < TARGET + MAX_FRAME * BYTES_PER_SAMPLE);
        /* and the render rate followed the device, by construction */
        CHECK(simUnaccounted(&r, TARGET) > -(long long) MAX_FRAME);
        CHECK(simUnaccounted(&r, TARGET) < (long long) MAX_FRAME);
    }
}

static void test_simulation_at_low_frame_rate(void)
{
    SimResult r;

    /* 20 fps: 50 ms between pumps, three fields of audio each. */
    simulate(20, 0, 30, &r, TARGET, CAP);

    CHECK(r.dry == 0);
    /* What the pump OPENS with here is the interval floor's cushion, not the
     * target: at 50 ms the floor is 5468 B and the device has just drained
     * 4410 B of it, so ~1058 B - one device period - is left when the pump
     * arrives. The old `min_bytes > TARGET / 2` was true only because the
     * harness sampled the queue after refilling it first; in the pump's order
     * it would fail on a perfectly healthy run. The invariant that survives the
     * order change is stated instead: the cushion is at least one device period,
     * less at most one aligned unit of refill rounding. */
    CHECK(r.min_bytes >= DEV_PERIOD_MS * RATE * BYTES_PER_SAMPLE / 1000u
                        - ALIGN * BYTES_PER_SAMPLE);
    CHECK(r.max_bytes < r.min_bytes + portAudioPaceMinTarget(50u, &cfg));
    CHECK(simUnaccounted(&r, TARGET) > -(long long) MAX_FRAME);
    CHECK(simUnaccounted(&r, TARGET) < (long long) MAX_FRAME);
}

static void test_simulation_at_single_digit_frame_rate(void)
{
    SimResult r;

    /* Classic and demo modes can drop to single-digit fps. At N fps the pump
     * runs N times a second and must bridge 1000/N ms of audio, so the backlog
     * (and therefore the latency) equals the frame interval. The sink must still
     * never starve: a fixed 16 KB cap holds only 185 ms, which is already less
     * than one interval at 5 fps, so the cap has to scale. */
    static const unsigned rates[4] = { 8, 5, 2, 1 };
    unsigned k;

    for (k = 0; k < 4; k++) {
        unsigned fps = rates[k];
        unsigned long long need;

        simulate(fps, 0, 20, &r, TARGET, 0); /* 0 = let the policy size the cap */

        CHECK(r.dry == 0);
        CHECK(r.min_bytes > 0);
        /* one interval + one device period of audio, within a frame's slack */
        need = (unsigned long long) (1000u / fps + DEV_PERIOD_MS) * RATE * BYTES_PER_SAMPLE / 1000u;
        CHECK(r.max_bytes >= need - MAX_FRAME * BYTES_PER_SAMPLE);
        CHECK(simUnaccounted(&r, TARGET) > -(long long) MAX_FRAME);
        CHECK(simUnaccounted(&r, TARGET) < (long long) MAX_FRAME);
    }
}

static void test_interval_floor_and_cap_scale_together(void)
{
    unsigned fps;

    /* For every supported interval, the cap must be able to hold the floor. This
     * is the invariant that a fixed cap broke at 5 fps. */
    for (fps = 1; fps <= 120; fps++) {
        unsigned dt = 1000u / fps;
        unsigned floor_bytes = portAudioPaceMinTarget(dt, &cfg);
        unsigned cap = portAudioPaceCap(floor_bytes, &cfg);

        CHECK(cap >= floor_bytes);
        CHECK(cap >= CAP);
    }

    /* A pathological hitch is clamped, not obeyed. */
    CHECK(portAudioPaceMinTarget(60000u, &cfg) == portAudioPaceMinTarget(MAX_INTERVAL_MS, &cfg));
}

static void test_simulation_at_high_frame_rate(void)
{
    SimResult r;

    /* 300 fps: the device drains less than one aligned frame per pump. Rounding
     * the refill down here drains the queue ~11% faster than it fills, which is
     * how this case failed. */
    simulate(300, 0, 30, &r, TARGET, CAP);

    CHECK(r.dry == 0);
    CHECK(r.min_bytes > TARGET / 2);
    CHECK(simUnaccounted(&r, TARGET) > -(long long) MAX_FRAME);
    CHECK(simUnaccounted(&r, TARGET) < (long long) MAX_FRAME);
}

static void test_simulation_survives_a_stall_between_pumps(void)
{
    PortAudioPaceClock c;
    unsigned queued = TARGET;
    unsigned long long rendered = 0;
    unsigned i;
    unsigned n;

    portAudioPaceClockReset(&c);

    /* A stall is not carried in dt when a device is draining - the pump simply
     * did not run, so the device emptied the queue. What must hold is that the
     * recovery is bounded: fill in frames, never ask for more than one at a
     * time, and never overshoot the target. */
    queued = 0;
    for (i = 0; i < 1000; i++) {
        n = portAudioPaceFrame(&c, 500, queued, TARGET, 1, &cfg);
        if (n == 0) {
            break;
        }
        CHECK(n <= MAX_FRAME);
        CHECK(n % ALIGN == 0);
        queued += n * BYTES_PER_SAMPLE;
        rendered += n;
    }
    CHECK(rendered > 0);
    CHECK(queued >= TARGET);
    CHECK(queued <= TARGET + ALIGN * BYTES_PER_SAMPLE);
}

/* ---- the ratchet under a cadence that varies ---------------------------- */

/*
 * These are the cases the constant-interval simulations above could never be:
 * the interval changes, so a pump meets a queue that was sized for the interval
 * before it. Both switches now live on SimScenario (use_effective, refill_first)
 * and drive the same core as everything above - one physics in this file, and it
 * is the pump's.
 *
 * The ratchet is stepped once per pump in both orders, at the same point, so a
 * comparison measures the rule or the order and never the stepping as well.
 */

/* The learned headroom must survive the interval floor.
 *
 * This is the arithmetic the pump got wrong: at 1 fps the floor is ~870 ms of
 * audio while the ratchet's whole ceiling is 116 ms, so max() discarded every
 * underrun it ever learned from. */
static void test_effective_target_adds_the_ratchet_on_top(void)
{
    const unsigned floor_1fps = portAudioPaceMinTarget(870u, &cfg);
    const unsigned floor_60fps = portAudioPaceMinTarget(17u, &cfg);

    /* No underruns learned: ordinary frame rates are unchanged. */
    CHECK(portAudioPaceEffectiveTarget(floor_60fps, TARGET, &cfg) == TARGET);
    CHECK(portAudioPaceEffectiveTarget(floor_1fps, TARGET, &cfg) == floor_1fps);

    /* Saturated ratchet: the headroom is ADDED where the floor dominates. */
    CHECK(portAudioPaceEffectiveTarget(floor_1fps, TARGET_MAX, &cfg)
          == floor_1fps + (TARGET_MAX - TARGET));
    CHECK(portAudioPaceEffectiveTarget(floor_1fps, TARGET_MAX, &cfg) > floor_1fps);

    /* And at an ordinary interval, where the configured target dominates, the
     * learned amount still applies rather than being swallowed by a max(). */
    CHECK(portAudioPaceEffectiveTarget(0, TARGET_MAX, &cfg) == TARGET_MAX);
    CHECK(portAudioPaceEffectiveTarget(0, TARGET, &cfg) == TARGET);

    /* Absurd inputs saturate rather than wrap. */
    CHECK(portAudioPaceEffectiveTarget(0xFFFFFFFFu, TARGET_MAX, &cfg) == 0xFFFFFFFFu);
}

/* The regression, end to end: at 1 fps with an occasional longer frame, the
 * queue used to starve on every single spike because nothing it learned could
 * reach the queue. With the headroom applied, the spikes eventually stop
 * starving it. */
static void test_jitter_at_low_frame_rate(void)
{
    SimScenario legacy = {0};
    SimScenario fixed = {0};
    SimResult lr, fr;
    const unsigned pumps = (120u * 1000u) / 870u; /* 137: 120 s of 870 ms pumps */

    /* 870 ms pumps, every fifth 940 ms: a 70 ms overrun, which is ordinary
     * frame-to-frame variance at this rate and well under the 116 ms the
     * ratchet is allowed to learn. The game's order, both arms. */
    legacy.base_ms = 870u;
    legacy.spike_every = 5u;
    legacy.spike_ms = 940u;
    legacy.seconds = 120u;
    legacy.start_bytes = TARGET;
    legacy.use_effective = 0;
    fixed = legacy;
    fixed.use_effective = 1;

    simulateScenario(&legacy, &lr);
    simulateScenario(&fixed, &fr);

    /* The defect: the ratchet is masked by the floor, so EVERY spike starves -
     * exactly one dry event per spike, which is why the count is the pump count
     * over the spike period rather than a loose lower bound. */
    CHECK(lr.dry == pumps / 5u);

    /* The fix: the first spikes teach it, then the headroom covers them. */
    CHECK(fr.dry > 0);        /* the first spike is still a surprise */
    CHECK(fr.dry < lr.dry);
    CHECK(fr.dry <= 8);

    /* Printed, not just asserted: the contrast IS the evidence, and a session
     * that cites these counts must be able to reproduce them from this binary.
     * The pump count is printed too, because 120 s of 870 ms pumps is 137 pumps
     * and not the 120 the older write-ups assumed. */
    printf("audio_pace: 1 fps, +70 ms every 5th pump, 120 s (%u pumps): "
           "max(ratcheted,floor) dry=%u, floor+learned dry=%u\n",
           pumps, lr.dry, fr.dry);

    /* And it is bounded: the queue does not grow without limit. */
    CHECK(fr.max_bytes <= portAudioPaceEffectiveTarget(
                             portAudioPaceMinTarget(940u, &cfg), TARGET_MAX, &cfg)
                         + MAX_FRAME * BYTES_PER_SAMPLE);
}

/* The ordinary case: 60 fps, with the interval redrawn by +/-10% every pump.
 *
 * This is the cadence an ordinary session has, and it must stay dry. The case
 * exists to prove the faithful harness does not invent starvation at ordinary
 * frame rates - the 40 ms target spans two and a bit pump intervals here, so
 * the variance to absorb is a fraction of the cushion - and to keep the
 * generator's bound honest: "bounded jitter" must not be an assumption about a
 * hash. */
static void test_jitter_at_60fps_is_absorbed(void)
{
    SimScenario s = {0};
    SimResult r;
    unsigned i;

    for (i = 0; i < 1000u; i++) {
        unsigned v = jitteredMs(60u, 10u, i);

        CHECK(v >= 54u && v <= 66u); /* 60 ms +/- 10%, whole ms */
    }

    s.fps = 60u;
    s.jitter_pct = 10u;
    s.seconds = 60u;
    s.start_bytes = TARGET;
    s.cap = CAP;
    s.use_effective = 1;
    simulateScenario(&s, &r);

    CHECK(r.dry == 0);
    /* And the case has teeth: the level really moved, so dry == 0 is a property
     * of the policy and not of a queue nothing touched. */
    CHECK(r.min_bytes < TARGET);
    CHECK(r.max_bytes > TARGET);

    printf("audio_pace: 60 fps, +/-10%% jitter, 60 s: dry=%u, queue %u..%u B (target %u)\n",
           r.dry, r.min_bytes, r.max_bytes, TARGET);
}

/* The harness's ORDER is itself a claim, and for three passes it was the wrong
 * one.
 *
 * simulate() refills to the target and then lets the device drain, so the queue
 * is always topped up before it can be emptied: starvation is unobservable at
 * any frame rate, under any jitter, with any target rule. The pump does the
 * opposite - it is called after the interval has elapsed and the device has
 * already played - so the depth it measures is the post-drain one.
 *
 * Both arms below use the SAME policy and the same jitter; only the order
 * differs. That is the whole finding, and it is why the ratchet defect survived
 * every test that existed.
 *
 * Keep this case even after every harness pumps in the game's order: it is the
 * evidence for why the order matters, and it fails loudly if someone reintroduces
 * refill-first as an optimisation. */
static void test_harness_order_decides_whether_a_starve_is_visible(void)
{
    SimScenario refillFirst = {0};
    SimScenario gameOrder = {0};
    SimResult rf, go;

    /* 1 fps, every fifth pump 70 ms longer, and the pre-df881c9 rule in BOTH
     * arms: that rule loses on every spike by construction, so the only
     * variable left between the two runs is the order. */
    refillFirst.base_ms = 870u;
    refillFirst.spike_every = 5u;
    refillFirst.spike_ms = 940u;
    refillFirst.seconds = 120u;
    refillFirst.start_bytes = TARGET;
    refillFirst.use_effective = 0;
    refillFirst.refill_first = 1;   /* the harness's old shape */
    gameOrder = refillFirst;
    gameOrder.refill_first = 0;     /* the pump's shape */

    simulateScenario(&refillFirst, &rf);
    simulateScenario(&gameOrder, &go);

    /* Topped up before it can empty: the loss is invisible. */
    CHECK(rf.dry == 0);
    /* The real order sees it, and the pre-fix rule loses on every spike. */
    CHECK(go.dry >= 20);

    /* Same policy, same jitter, same length - the order alone is the difference. */
    CHECK(go.dry > rf.dry);

    printf("audio_pace: same rule and jitter, order only: refill-first dry=%u, "
           "game-order dry=%u\n", rf.dry, go.dry);
}

int main(void)
{
    test_refill_is_aligned_and_clamped();
    test_refill_does_not_drain_when_pumping_fast();

    test_clock_holds_wall_time();
    test_clock_at_field_rate_is_exact();
    test_clock_catchup_is_bounded();
    test_clock_rests_while_a_device_paces();

    test_target_ratchets_and_eases();
    test_drain_watchdog();
    test_watchdog_measures_across_pumps_not_within_one();

    test_refill_rejects_an_unusable_config();
    test_refill_stays_aligned_when_the_clamp_binds();
    test_geometry_saturates_instead_of_wrapping();

    test_simulation_holds_the_queue();
    test_simulation_refills_a_dry_start();
    test_simulation_absorbs_device_drift();
    test_simulation_at_low_frame_rate();
    test_simulation_at_single_digit_frame_rate();
    test_interval_floor_and_cap_scale_together();
    test_simulation_at_high_frame_rate();
    test_simulation_survives_a_stall_between_pumps();
    test_effective_target_adds_the_ratchet_on_top();
    test_jitter_at_low_frame_rate();
    test_jitter_at_60fps_is_absorbed();
    test_harness_order_decides_whether_a_starve_is_visible();

    printf("audio_pace: all checks passed\n");
    return 0;
}
