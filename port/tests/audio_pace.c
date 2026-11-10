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
 *   - test_simulation_holds_the_queue_with_bounded_refill: a wall-clock pace
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

typedef struct {
    unsigned min_bytes;
    unsigned max_bytes;
    unsigned dry;       /* pumps that found the queue empty, after priming */
    long long rendered; /* samples synthesised */
    long long consumed; /* samples the device played */
} SimResult;

/*
 * Pump at `fps` against a device that plays at (1 + ppm/1e6) * RATE for
 * `seconds` of wall time. The device model is the simplest honest one: whatever
 * is queued drains at its own rate, and an empty queue means it played silence.
 */
static void simulate(unsigned fps, long drift_ppm, unsigned seconds,
                     SimResult *out, unsigned start_bytes, unsigned cap)
{
    PortAudioPaceClock c;
    PortAudioPaceDrain d = {0, 0, 0, 0};
    long long consumed_acc = 0; /* samples x 1000 */
    unsigned queued;
    unsigned now_ms = 0;
    unsigned ms_acc = 0;
    unsigned i;
    unsigned steps = fps * seconds;
    int primed = 0;

    portAudioPaceClockReset(&c);
    out->min_bytes = 0xFFFFFFFFu;
    out->max_bytes = 0;
    out->dry = 0;
    out->rendered = 0;
    out->consumed = 0;
    queued = start_bytes;

    for (i = 0; i < steps; i++) {
        unsigned opened = queued;
        unsigned closed;
        unsigned pump_ms; /* the pump interval, for the device model */

        ms_acc += 1000;
        pump_ms = ms_acc / fps;
        ms_acc -= pump_ms * fps;

        /* Refill toward the target, as the pump does with a live device. The
         * effective target is never below one pump interval: a shallower one
         * starves every pump by construction. */
        {
            unsigned target_eff = TARGET;
            unsigned floor_bytes = portAudioPaceMinTarget(pump_ms, &cfg);
            unsigned cap_eff;

            if (floor_bytes > target_eff) {
                target_eff = floor_bytes;
            }
            cap_eff = portAudioPaceCap(target_eff, &cfg);
            if (cap != 0 && cap > cap_eff) {
                cap_eff = cap;
            }

            while (1) {
                unsigned n = portAudioPaceFrame(&c, pump_ms, queued, target_eff, 1, &cfg);
                unsigned bytes;

                if (n == 0) {
                    break;
                }
                bytes = n * BYTES_PER_SAMPLE;
                if (queued + bytes > cap_eff) {
                    break; /* over the cap: rendered but not queued */
                }
                queued += bytes;
                out->rendered += n;
            }
        }

        closed = queued;
        (void) portAudioPaceDrainObserve(&d, opened, closed, now_ms, &cfg);

        if (!primed && closed > 0) {
            primed = 1;
        } else if (primed && opened == 0) {
            out->dry++;
        }
        if (primed) {
            if (queued < out->min_bytes) {
                out->min_bytes = queued;
            }
            if (queued > out->max_bytes) {
                out->max_bytes = queued;
            }
        }

        /* The device plays for pump_ms at its own rate. Signed: a negative
         * drift must subtract, not wrap. */
        consumed_acc += (long long) pump_ms * (long long) RATE
                      + (long long) drift_ppm * (long long) pump_ms * (long long) RATE / 1000000;
        while (consumed_acc >= 1000) {
            unsigned long long take = (unsigned long long) (consumed_acc / 1000);

            consumed_acc -= (long long) take * 1000;
            if (take > queued / BYTES_PER_SAMPLE) {
                take = queued / BYTES_PER_SAMPLE;
            }
            queued -= (unsigned) (take * BYTES_PER_SAMPLE);
            out->consumed += (long long) take;
        }

        now_ms += pump_ms;
    }
}

static void test_simulation_holds_the_queue(void)
{
    SimResult r;

    simulate(60, 0, 30, &r, TARGET, CAP);

    CHECK(r.dry == 0);
    CHECK(r.min_bytes > TARGET / 2);
    CHECK(r.max_bytes < TARGET + MAX_FRAME * BYTES_PER_SAMPLE);
    /* Render rate equals the DAC's rate: audio time is the device's time. */
    CHECK(r.rendered > r.consumed - (long long) MAX_FRAME);
    CHECK(r.rendered < r.consumed + (long long) MAX_FRAME);
}

static void test_simulation_refills_a_dry_start(void)
{
    SimResult r;

    /* A device that opens empty must be filled, not left starved. */
    simulate(60, 0, 10, &r, 0, CAP);

    /* Strictly: the first pump with a sink fills to the target, so the depth is
     * already above half the target. (simulate() samples this AFTER the refill
     * and BEFORE the device drain, where the minimum settles at exactly TARGET;
     * post-drain it is TARGET - one drain interval, still above half.) The
     * earlier `|| r.dry <= 1` escape let this pass without that being true. */
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
        CHECK(r.rendered > r.consumed - (long long) MAX_FRAME);
        CHECK(r.rendered < r.consumed + (long long) MAX_FRAME);
    }
}

static void test_simulation_at_low_frame_rate(void)
{
    SimResult r;

    /* 20 fps: 50 ms between pumps, three fields of audio each. */
    simulate(20, 0, 30, &r, TARGET, CAP);

    CHECK(r.dry == 0);
    CHECK(r.min_bytes > TARGET / 2);
    CHECK(r.rendered > r.consumed - (long long) MAX_FRAME);
    CHECK(r.rendered < r.consumed + (long long) MAX_FRAME);
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
        CHECK(r.rendered > r.consumed - (long long) MAX_FRAME);
        CHECK(r.rendered < r.consumed + (long long) MAX_FRAME);
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
    CHECK(r.rendered > r.consumed - (long long) MAX_FRAME);
    CHECK(r.rendered < r.consumed + (long long) MAX_FRAME);
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

    printf("audio_pace: all checks passed\n");
    return 0;
}
