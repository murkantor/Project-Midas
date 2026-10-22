/**
 * portAudioPump render-pacing policy - ROM-free, device-free regression tests.
 *
 * Build and run:
 *     cmake -B build -DGE_PORT_TESTS=ON && cmake --build build --target audio_pace_test
 *     ctest --test-dir build -R audio_pace
 *
 * Or standalone, since the unit under test has no dependencies at all:
 *     cc -std=c99 -Iport/include port/tests/audio_pace.c port/src/audio_pace.c \
 *        -o audio_pace_test && ./audio_pace_test
 *
 * What this protects, and why it is worth a test at all:
 *
 * 1. The synthesizer must never be stopped by device state. alAudioFrame is the
 *    only caller of the AL client handlers, so stopping it freezes the SFX
 *    event queue, the sound-state list and all three music sequencers. That is
 *    the shape of the silent-system reports (#19, #56).
 * 2. A live device must remain the pacing authority, so the backlog - and with
 *    it the audible delay - cannot drift upward. This is the ~100 ms latency
 *    regression addressed after the first round of field reports.
 * 3. A healthy sink must never be mistaken for a dead one, and a dead one must
 *    be noticed quickly and then reopened.
 *
 * Figures are the real ones for the port at 22050 Hz stereo s16 (88200 B/s),
 * with the 147 Hz pump cadence seen in field logs ("tick120 pump147").
 */

#include <stdio.h>
#include <stdlib.h>

#include "audio_pace.h"

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #expr);                                                    \
            abort();                                                           \
        }                                                                      \
    } while (0)

/* ---- the real figures ---------------------------------------------------- */

#define RATE              22050
#define BYTES_PER_S       (RATE * 2 * 2)                /* stereo, s16      */
#define FIELD_SAMPLES     736                           /* audi.c NTSC      */
#define QUANTUM_SAMPLES   368                           /* half a field     */
#define QUANTUM_BYTES     (QUANTUM_SAMPLES * 4)         /* 1472 B           */
#define PUMP_HZ           147                           /* field logs       */
#define BYTES_PER_PUMP    (BYTES_PER_S / PUMP_HZ)       /* ~600 B           */
#define DEV_PERIOD_B      (PORT_AUDIO_DEV_SAMPLES * 4)  /* 1024 B           */

static void test_quantum(void)
{
    port_u32 q = portAudioQuantum(FIELD_SAMPLES, 1024, 2);

    /* Half a field, 16-aligned, so alAudioFrame sees a legal subframe count. */
    CHECK(q == QUANTUM_SAMPLES);
    CHECK((q % 16) == 0);
    /* Halving the quantum is the point: it halves the worst-case wait for a
     * newly triggered event's first rendered sample. */
    CHECK(q * 2 <= FIELD_SAMPLES);

    /* Degenerate arguments must not produce a zero or absurd length. */
    CHECK(portAudioQuantum(0, 1024, 2) == 368);             /* default field */
    CHECK(portAudioQuantum(FIELD_SAMPLES, 1024, 0) == 736);  /* div 0 -> 1    */
    CHECK(portAudioQuantum(10, 1024, 2) == 64);             /* floor         */
    CHECK(portAudioQuantum(999999, 1024, 1) == 1024);       /* cap           */
    CHECK(portAudioQuantum(999999, 0, 1) == 999984);        /* 0 = uncapped  */
}

/* A live device with a backlog at or above the watermark owes nothing. */
static void test_live_device_paces_to_watermark(void)
{
    PortAudioPace p;

    p.dev_live = 1;
    p.queued = PORT_AUDIO_QUEUE_LOW;
    p.owed = 0;
    p.frame = (port_u64) QUANTUM_SAMPLES * 1000;
    CHECK(!portAudioShouldRender(&p));

    p.queued = PORT_AUDIO_QUEUE_LOW - 1;
    CHECK(portAudioShouldRender(&p));

    /* Crucially: a large clock debt must NOT override a full-enough device
     * queue. If it did, the backlog would climb toward the cap and latency
     * with it - the bug this rule exists to prevent. */
    p.queued = PORT_AUDIO_QUEUE_LOW;
    p.owed = (port_u64) 100 * RATE * 1000;
    CHECK(!portAudioShouldRender(&p));
}

/* With no device the wall clock paces, so game audio time keeps advancing. */
static void test_no_device_clock_paces(void)
{
    PortAudioPace p;

    p.dev_live = 0;
    p.queued = 0;
    p.owed = 0;
    p.frame = (port_u64) QUANTUM_SAMPLES * 1000;

    CHECK(!portAudioShouldRender(&p));      /* nothing owed yet  */
    p.owed = (port_u64) QUANTUM_SAMPLES * 1000;
    CHECK(portAudioShouldRender(&p));       /* exactly one frame */
    p.owed = (port_u64) QUANTUM_SAMPLES * 1000 - 1;
    CHECK(!portAudioShouldRender(&p));      /* one unit short    */

    /* A stale queue figure is irrelevant when there is no device. */
    p.queued = PORT_AUDIO_QUEUE_MAX;
    p.owed = (port_u64) QUANTUM_SAMPLES * 1000;
    CHECK(portAudioShouldRender(&p));
}

/* A dead sink hands pacing back to the clock instead of freezing it. */
static void test_stalled_device_falls_back_to_clock(void)
{
    PortAudioPace p;

    p.dev_live = 0;                 /* the tracker has given up on it */
    p.queued = PORT_AUDIO_QUEUE_LOW; /* still holding the watermark    */
    p.owed = 0;
    p.frame = (port_u64) QUANTUM_SAMPLES * 1000;

    CHECK(!portAudioShouldRender(&p));
    p.owed = (port_u64) QUANTUM_SAMPLES * 1000;
    CHECK(portAudioShouldRender(&p));
}

/* An empty queue is not evidence of a dead sink - nothing exists to drain. */
static void test_drain_tracker_empty_queue_is_live(void)
{
    PortAudioDrain d;
    int pump;

    d.at_exit = 0;
    d.famished = 0;
    for (pump = 0; pump < 400; pump++) {
        CHECK(portAudioDrainTick(&d, 0));
    }
    CHECK(d.famished == 0);
    CHECK(!portAudioShouldReopen(&d));
}

/*
 * A device that hesitates and then resumes must have its streak cleared, or a
 * healthy sink would eventually be declared dead just for being bursty.
 */
static void test_drain_tracker_recovers(void)
{
    PortAudioDrain d;
    int pump;

    d.at_exit = PORT_AUDIO_QUEUE_LOW;
    d.famished = 0;

    /* Four pumps with nothing consumed: still trusted, streak climbing. */
    for (pump = 0; pump < 4; pump++) {
        CHECK(portAudioDrainTick(&d, PORT_AUDIO_QUEUE_LOW));
    }
    CHECK(d.famished == 4);

    /* Then the device takes a period's worth. The streak must reset. */
    CHECK(portAudioDrainTick(&d, PORT_AUDIO_QUEUE_LOW - DEV_PERIOD_B));
    CHECK(d.famished == 0);

    /* And the tracker keeps trusting it afterwards. */
    d.at_exit = PORT_AUDIO_QUEUE_LOW - DEV_PERIOD_B;
    for (pump = 0; pump < 4; pump++) {
        CHECK(portAudioDrainTick(&d, PORT_AUDIO_QUEUE_LOW - DEV_PERIOD_B));
    }
    CHECK(d.famished == 4);
    CHECK(!portAudioShouldReopen(&d));
}

/* A sink that never drains must be detected, then eventually reopened. */
static void test_drain_tracker_dead(void)
{
    PortAudioDrain d;
    int pump;
    int wentDeadAt = -1;
    int reopenedAt = -1;

    d.at_exit = PORT_AUDIO_QUEUE_LOW;
    d.famished = 0;
    for (pump = 0; pump < 400; pump++) {
        /* nothing is ever consumed: the queue is exactly what we left */
        if (!portAudioDrainTick(&d, PORT_AUDIO_QUEUE_LOW) && wentDeadAt < 0) {
            wentDeadAt = pump;
        }
        if (portAudioShouldReopen(&d) && reopenedAt < 0) {
            reopenedAt = pump;
        }
    }

    /* Detection must be fast enough that the game's audio clock keeps up:
     * well under one video field at 60 Hz. */
    CHECK(wentDeadAt >= 0);
    CHECK(wentDeadAt < PORT_AUDIO_DRAIN_PUMPS);
    /* ...and the reopen must be slow enough not to thrash a blipping device.
     * famished is incremented before the check, so it trips one pump early. */
    CHECK(reopenedAt >= PORT_AUDIO_STALL_PUMPS - 1);
    CHECK(reopenedAt < PORT_AUDIO_STALL_PUMPS + 1);
    /* The reopen threshold is far beyond the detection threshold, so a device
     * that merely hesitates falls back to clock pacing long before it is
     * closed - the game must never wait on it. */
    CHECK(PORT_AUDIO_DRAIN_PUMPS < PORT_AUDIO_STALL_PUMPS);
}

/*
 * Simulated pump loop against a real device model.
 *
 * The device consumes in whole periods (it cannot take a fraction of one), so
 * at 147 Hz a healthy queue is byte-identical across roughly every second
 * pump. That is precisely the situation that must NOT be read as a dead sink,
 * and it is what this loop exists to exercise.
 */
static void run_healthy_device(int pumps, port_u32 *peak_out, int *renders_out)
{
    PortAudioDrain d;
    port_u32 queued = 0;
    port_u32 acc = 0;
    port_u32 peak = 0;
    int renders = 0;
    int pump;
    const port_u64 frame = (port_u64) QUANTUM_SAMPLES * 1000;

    d.at_exit = 0;
    d.famished = 0;

    for (pump = 0; pump < pumps; pump++) {
        PortAudioPace p;
        int guard;

        /* --- the device drains, in whole periods ------------------------ */
        acc += BYTES_PER_PUMP;
        while (acc >= DEV_PERIOD_B && queued >= DEV_PERIOD_B) {
            queued -= DEV_PERIOD_B;
            acc -= DEV_PERIOD_B;
        }

        /* --- the pump observes, before rendering anything ---------------- */
        if (!portAudioDrainTick(&d, queued)) {
            fprintf(stderr, "healthy sink misread as dead at pump %d "
                            "(queue %u, famished %u)\n",
                    pump, (unsigned) queued, (unsigned) d.famished);
            abort();
        }

        /* --- render loop ------------------------------------------------- */
        for (guard = 0; guard < 8; guard++) {
            p.dev_live = 1;
            p.queued = queued;
            p.owed = 0; /* a live device is the authority: clock is idle */
            p.frame = frame;
            if (!portAudioShouldRender(&p)) {
                break;
            }
            renders++;
            if (queued < PORT_AUDIO_QUEUE_MAX) {
                queued += QUANTUM_BYTES;
            }
        }

        /* --- leave the queue for the next observation -------------------- */
        d.at_exit = queued;
        if (queued > peak) {
            peak = queued;
        }
    }

    *peak_out = peak;
    *renders_out = renders;
}

static void test_drain_tracker_healthy(void)
{
    port_u32 peak;
    int renders;

    run_healthy_device(PUMP_HZ, &peak, &renders);
    CHECK(renders > 0);
    /* A healthy bursty sink never approaches the reopen threshold. */
    CHECK(peak <= PORT_AUDIO_QUEUE_LOW + QUANTUM_BYTES);
}

/*
 * The latency regression test. Ten seconds of a healthy sink: the backlog must
 * never escape the watermark band. If pacing ever hands authority to the clock
 * while a device is live, the queue climbs toward the cap and this fails -
 * which is exactly the ~100 ms lag reported after the first fix.
 */
static void test_backlog_stays_in_band(void)
{
    const int pumps = PUMP_HZ * 10;
    port_u32 peak;
    int renders;
    /* Bytes consumed over the window, divided by bytes per quantum: the pump
     * must render exactly as much audio as the device took, no more. */
    int expected = (int) ((long long) pumps * BYTES_PER_PUMP / QUANTUM_BYTES);

    run_healthy_device(pumps, &peak, &renders);

    /* Band: never above the watermark plus one quantum. */
    CHECK(peak <= PORT_AUDIO_QUEUE_LOW + QUANTUM_BYTES);
    /* And audio must actually have been rendered throughout - the synthesizer
     * running is the other half of the contract. */
    CHECK(renders > pumps / 4);
    /* Render volume tracks consumption, within the drain accumulator's
     * remainder. If the pump rendered more than the device drains, the backlog
     * would grow; less, and it would starve. */
    CHECK(renders >= expected - 2);
    CHECK(renders <= expected + 2);
}

/*
 * Latency arithmetic, asserted rather than eyeballed. Output delay is the sum
 * of the queue floor, one render quantum and the device period.
 */
static void test_latency_budget(void)
{
    port_u32 quantum_ms = QUANTUM_SAMPLES * 1000 / RATE;
    port_u32 low_ms = PORT_AUDIO_QUEUE_LOW * 1000 / BYTES_PER_S;
    port_u32 dev_ms = PORT_AUDIO_DEV_SAMPLES * 1000 / RATE;
    port_u32 total = quantum_ms + low_ms + dev_ms;

    /* ~17 + 23 + 12 = ~52 ms at the worst edge, against ~103 ms before. */
    CHECK(quantum_ms <= 17);
    CHECK(low_ms <= 24);
    CHECK(dev_ms <= 12);
    CHECK(total <= 55);
}

int main(void)
{
    test_quantum();
    test_live_device_paces_to_watermark();
    test_no_device_clock_paces();
    test_stalled_device_falls_back_to_clock();
    test_drain_tracker_empty_queue_is_live();
    test_drain_tracker_recovers();
    test_drain_tracker_dead();
    test_drain_tracker_healthy();
    test_backlog_stays_in_band();
    test_latency_budget();

    puts("audio pace tests passed");
    return 0;
}
