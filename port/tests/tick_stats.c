/*
 * The frame/tick statistics of the in-game block.
 *
 * Drives the shipped functions in port/src/tick_stats.c against windows
 * computed by hand, so the numbers a player reads in the corner are checked
 * without a window, a GPU or a ROM.
 *
 * The cases are chosen to pin the three things that were wrong or unverifiable
 * before: that fields/s means 60.0 whenever simulated time is keeping up, that
 * it falls when the sim loses time, and that a stage-load reset cannot publish
 * a negative rate.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tick_stats.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

/* A 500 ms window of a healthy 60 Hz sim: 30 fields, 30 ticks.
 * fields/s = 30 * 10000 / 500 = 600 (= 60.0), fields/tick = 100 (= 1.00). */
static void healthy_60hz(void)
{
    const uint32_t fields = 30, ticks = 30, dt = 500;

    CHECK(portStatsRatePerSec(fields, dt, 10) == 600);
    CHECK(portStatsFieldsPerTickX100(fields, ticks) == 100);
}

/* A 500 ms window at 120 Hz: one field per two ticks, so the tick rate is 120
 * while fields/s is still 60.0. This is the case the old TPS row got wrong: it
 * counted 60 clock-advancing ticks and reported "60 tps" for a 120 tick/s sim. */
static void healthy_120hz(void)
{
    const uint32_t fields = 30, ticks = 60, dt = 500;

    CHECK(portStatsRatePerSec(fields, dt, 10) == 600);
    CHECK(portStatsRatePerSec(ticks, dt, 1) == 120); /* the honest tick rate */
    CHECK(portStatsFieldsPerTickX100(fields, ticks) == 50); /* 0.50 f/t */
}

/* The live macOS capture: software rasterisation made frames cost ~466 ms, so a
 * window held one 60 Hz tick that was credited the GE_TICKMAXSTEP cap of 8
 * fields. Observed in audio_pump.log as `f/s 16.7 f/t 8.00` at dt=479 ms. This
 * is the reading that identified the sim losing time, so pin it. */
static void stalled_window(void)
{
    const uint32_t fields = 8, ticks = 1, dt = 479;

    CHECK(portStatsRatePerSec(fields, dt, 10) == 167);  /* 16.7 fields/s */
    CHECK(portStatsFieldsPerTickX100(fields, ticks) == 800); /* 8.00 f/t */
    /* And it is unmistakably short of the invariant. */
    CHECK(portStatsRatePerSec(fields, dt, 10) < 600);
}

/* A window with no admitted tick at all (the loop blocked loading): no ratio,
 * and no division by zero. */
static void no_ticks(void)
{
    CHECK(portStatsFieldsPerTickX100(0, 0) == 0);
    CHECK(portStatsFieldsPerTickX100(8, 0) == 0);
}

/* A zero-length window cannot happen on the wall clock, but the function must
 * not divide by it if it ever does. */
static void zero_window(void)
{
    CHECK(portStatsRatePerSec(30, 0, 10) == 0);
}

/* Rounding, not truncation: 22 fields over a 367 ms window is 59.945 fields/s,
 * which must read 599 (59.9) rather than 599.4's floor or a drifted 600. */
static void rounding(void)
{
    CHECK(portStatsRatePerSec(22, 367, 10) == 599);

    /* And a genuinely on-target window that is not exactly 500 ms: 61 fields in
     * 1017 ms is 59.98/s, which reads 600 rather than 599. */
    CHECK(portStatsRatePerSec(61, 1017, 10) == 600);
}

/* F5: the healthy band is a band, not a number. This pins where it comes from,
 * so the claim stops resting on four readings from one session.
 *
 * The rate is whole fields over a window of whole milliseconds. A healthy 60 Hz
 * sim credits 60 fields per second, so over a window of dt ms the nearest whole
 * count is round(dt*60/1000) - and 600 (60.0) is reached only where that product
 * lands on an integer. Sweeping every window length the stat pass can actually
 * close on (it publishes at >= 500 ms) gives the band below: arithmetic, with no
 * defect involved. */
static void healthy_window_band(void)
{
    uint32_t dt;
    uint32_t lo = 0xFFFFFFFFu, hi = 0;

    for (dt = 500; dt <= 1000; dt++) {
        uint32_t fields = (dt * 60u + 500u) / 1000u; /* nearest whole field */
        uint32_t v = portStatsRatePerSec(fields, dt, 10);

        CHECK(v >= 591u && v <= 610u);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    /* Both ends are reached inside the sweep, so the band is arithmetic rather
     * than a tolerance chosen to fit: 59.1 is 30 fields over 508 ms, and 61.0 is
     * 32 over 525. */
    CHECK(lo == 591u);
    CHECK(hi == 610u);

    /* One field either side of a boundary - a window that straddles a field -
     * stays inside [58.0, 62.0], so a reading in the manual's measured 59.3-62.9
     * band still needs no fault to explain it. That band's top is two fields at
     * a 500 ms window, which is the ordinary case when the window starts and the
     * counters are sampled at different points. */
    for (dt = 500; dt <= 1000; dt++) {
        uint32_t base = (dt * 60u) / 1000u;

        CHECK(portStatsRatePerSec(base, dt, 10) >= 580u);
        CHECK(portStatsRatePerSec(base + 1u, dt, 10) <= 620u);
    }
}

/* D4: lvlStageLoad zeroes g_GlobalTickCount. The baseline must follow the reset
 * so the window reads 0 instead of -2x the pre-load count. */
static void stage_load_reset(void)
{
    /* 20 s of play, then the counter is zeroed by a stage load. */
    CHECK(portStatsReseatTickBaseline(1200, 0) == 0);
    /* The delta that follows is now 0, not -1200: no negative can be published. */
    CHECK((int32_t) (0 - portStatsReseatTickBaseline(1200, 0)) == 0);

    /* 5 s of play: the -600 case observed in the field. */
    CHECK(portStatsReseatTickBaseline(300, 0) == 0);

    /* A normal forward move keeps the baseline. */
    CHECK(portStatsReseatTickBaseline(1200, 1260) == 1200);
    /* Equal counts likewise (a window with no ticks). */
    CHECK(portStatsReseatTickBaseline(1200, 1200) == 1200);
}

/* Wrapping numbers must not produce a negative published rate. */
static void negative_never_published(void)
{
    int32_t baseline = 1200;

    baseline = portStatsReseatTickBaseline(baseline, 0);
    CHECK(baseline >= 0);
    CHECK((0 - baseline) == 0);
}

/* The stall cap's own verdict: the fields a tick measured against the fields it
 * was allowed to keep. The live case is a 367 ms frame under hw pacing, which
 * is 22 fields of wall time credited 8 - the reading the hitch log could not
 * express while it compared credits against the model's hold instead. */
static void stall_cap(void)
{
    const uint32_t field = 781250u;  /* PORT_TICK_FIELD_COUNTS, NTSC */
    const uint32_t ms367 = 17203125u; /* 367 ms at 46.875 MHz */

    CHECK(portStatsMeasuredFields(ms367, field) == 22);
    CHECK(portStatsCreditedFields(22, 8) == 8);

    /* credited == measured means the cap did not fire, so nothing is dropped */
    CHECK(portStatsCreditedFields(8, 8) == 8);
    CHECK(portStatsCreditedFields(4, 8) == 4);

    /* 0 = uncapped, the same convention portTickMaxStep() uses */
    CHECK(portStatsCreditedFields(22, 0) == 22);

    /* a frame shorter than a field credits nothing, and no division by zero */
    CHECK(portStatsMeasuredFields(700000u, field) == 0);
    CHECK(portStatsMeasuredFields(10, 0) == 0);

    /* ten seconds of stall must not wrap the arithmetic: 600 fields, 8 kept */
    CHECK(portStatsMeasuredFields(468750000u, field) == 600);
    CHECK(portStatsCreditedFields(600, 8) == 8);
}

/* The trailing text of midas.ini's tickhz line: "hw" for hw pacing, else the
 * rate. The one thing this pins that a reading of video.c would not: -1 is a
 * MODE and -2 is a state, and only the first belongs in the file. */
static void tick_ini_value(void)
{
    char buf[PORT_TICK_INI_BUF];

    /* the four cases the writer has to get right */
    CHECK(portTickIniValue(-1, buf, sizeof buf) != NULL);
    CHECK(strcmp(portTickIniValue(-1, buf, sizeof buf), "hw") == 0);
    CHECK(strcmp(portTickIniValue(0, buf, sizeof buf), "0") == 0);   /* uncapped */
    CHECK(strcmp(portTickIniValue(60, buf, sizeof buf), "60") == 0);
    CHECK(strcmp(portTickIniValue(120, buf, sizeof buf), "120") == 0);
    CHECK(strcmp(portTickIniValue(144, buf, sizeof buf), "144") == 0);

    /* -2 is videoCoupleTickToMaxFps's "no `want` equals it, re-derive" sentinel.
     * Writing it would leave a file the loader cannot resolve. */
    CHECK(portTickIniValue(-2, buf, sizeof buf) == NULL);
    CHECK(portTickIniValue(-3, buf, sizeof buf) == NULL);

    /* a buffer too small for the text yields nothing rather than a truncation */
    CHECK(portTickIniValue(-1, buf, 2) == NULL);
    CHECK(portTickIniValue(-1, NULL, sizeof buf) == NULL);
}

/* The cap one tick is credited against. Pins the policy the change exists for:
 * a hold larger than the cap is admitted under hw pacing, and nothing else
 * moves. The last two cases are the ones that keep the cap honest. */
static void stall_cap_hold(void)
{
    /* hw pacing, a 4-field hold under the 8-field cap: ordinary frames */
    CHECK(portStatsStepCap(8, 4, 1, 0) == 8);
    /* hw pacing, a 12-field hold (a load hitch): the hold is admitted */
    CHECK(portStatsStepCap(8, 12, 1, 0) == 12);
    /* the model's ceiling */
    CHECK(portStatsStepCap(8, 30, 1, 0) == 30);
    /* equal: no change either way */
    CHECK(portStatsStepCap(8, 8, 1, 0) == 8);

    /* NOT under hw pacing: a window drag in a menu or a demo has no priced
     * hold behind it and must still be capped. */
    CHECK(portStatsStepCap(8, 30, 0, 0) == 8);
    CHECK(portStatsStepCap(8, 30, 0, 1) == 8);

    /* an explicit GE_TICKMAXSTEP is the experiment measuring this cap */
    CHECK(portStatsStepCap(8, 30, 1, 1) == 8);
    CHECK(portStatsStepCap(4, 30, 1, 1) == 4);

    /* 0 = uncapped stays uncapped, never "cap of zero" */
    CHECK(portStatsStepCap(0, 30, 1, 0) == 0);
    CHECK(portStatsStepCap(0, 0, 1, 0) == 0);
}

int main(void)
{
    healthy_60hz();
    healthy_120hz();
    stalled_window();
    no_ticks();
    zero_window();
    rounding();
    healthy_window_band();
    stage_load_reset();
    negative_never_published();
    stall_cap();
    stall_cap_hold();
    tick_ini_value();

    printf("tick_stats: all checks passed\n");
    return 0;
}
