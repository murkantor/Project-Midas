#ifndef PORT_TICK_STATS_H
#define PORT_TICK_STATS_H

/*
 * The frame/tick statistics of the in-game block, as pure functions.
 *
 * Nothing here reads a clock, touches SDL, or includes an engine header: the
 * stat pass in port/fast3d/gfx_sdl2.cpp gathers deltas over its 500 ms window
 * and these functions turn them into the published numbers. That is what makes
 * the arithmetic testable without a window, a GPU or a ROM - see
 * port/tests/tick_stats.c.
 *
 * WHY THESE EXIST
 *
 * The block used to show `FPS nfps TPS ntps`, and TPS was not the tick rate:
 * lv.c does `if (g_ClockTimer > 0) g_GlobalTickCount += 1`, so a tick that
 * absorbed three video fields still counted as one. Its rate is therefore
 * min(tick rate, 60) - it reads 60 at tickhz=120 and it tracks FPS whenever
 * frames overrun a field, which is exactly the reading that looked like a
 * broken counter in a tickhz=hw capture.
 *
 * Two numbers replace it, and both are ratios of monotonic counters so that a
 * stage load cannot disturb them:
 *
 *   fields/s   - 60 Hz fields credited to the sim per second, x10.
 *                600 = 60.0 = simulated time is keeping up. THE invariant: it
 *                holds at any tick rate and any frame rate the machine can
 *                sustain. Below 600 the game is in slow motion.
 *   fields/tick - fields absorbed per admitted tick, x100.
 *                100 = one field per tick (a normal 60 Hz frame). Above 100 a
 *                frame overran a field and the tick carried the difference,
 *                which is what the N64 did when it slowed down. Below 100 a
 *                raised tick rate is sub-ticking one field.
 *
 * Integer arithmetic throughout, rounded rather than truncated: a 500 ms window
 * at 60.0 fields/s is exactly 30 fields, but windows are not exactly 500 ms and
 * truncating turned "on target" into "one short" often enough to look like a
 * drift.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A rate per second from a counter delta, scaled by `scale` and rounded.
 *
 * delta is the counter's advance over dt_ms of wall time. The result is the
 * rate MULTIPLIED by scale: scale=1 gives whole units per second (120),
 * scale=10 gives tenths (600 for 60.0/s), scale=100 gives hundredths. Returns 0
 * for a zero-length window instead of dividing. */
uint32_t portStatsRatePerSec(uint32_t delta, uint32_t dt_ms, uint32_t scale);

/* Fields credited per tick, x100. `ticks` is the tick count in the same window
 * - pass g_PortTickSerial's delta (lv.c, inside lvlTick), NOT boss.c's
 * g_PortTicksAdmitted, which counts ticks admitted before the pendingGfx
 * backpressure guard and so would push f/t below 1.00 at tickhz=60. 0 ticks
 * (a window with no tick at all, e.g. the loop blocked on a load) yields 0
 * rather than a division. */
uint32_t portStatsFieldsPerTickX100(uint32_t fields, uint32_t ticks);

/* The stall cap's verdict, in whole 60Hz fields.
 *
 * `measured` is what a tick's elapsed time was worth: `elapsed_counts` is the
 * OS_CPU_COUNTER delta since the previous tick and `field_counts` is one field
 * in the same units (PORT_TICK_FIELD_COUNTS). The division truncates, the same
 * way waitForNextFrame turns counts into whole fields, so this is the count the
 * tick would have been credited had the cap not interfered.
 *
 * `credited` is what GE_TICKMAXSTEP will actually let it keep. cap_fields is
 * portTickMaxStep(), where 0 means uncapped - the same convention the cap
 * itself uses. Note the shape of the answer: credited == measured means the cap
 * did not fire, and credited < measured is time that will not be simulated.
 *
 * These exist because comparing the credited fields against the hw model's
 * *hold* measures nothing: on a renderer slower than the model the frame spans
 * far more wall time than either number, so the comparison read "healthy"
 * exactly when the most time was being dropped. The clamp site is the only
 * place that knows both halves. */
uint32_t portStatsMeasuredFields(uint32_t elapsed_counts, uint32_t field_counts);
uint32_t portStatsCreditedFields(uint32_t measured, uint32_t cap_fields);

/** The cap the credit path should use for one tick.
 *
 * `cap_fields` is portTickMaxStep() (0 = uncapped) and `cap_explicit` says
 * whether GE_TICKMAXSTEP set it. `hold_fields` is the deliberate hw-pacing hold
 * boss.c just applied, and `hw_pace` is portTickHwPace().
 *
 * The stall cap exists to stop an *unintended* stall (a window drag) from being
 * simulated as one enormous step. A hold is not that: the pacer priced the
 * frame at N fields and the loop waited for them, so those N are intended time
 * and the cap must not truncate the thing it was handed. Under hw pacing the
 * cap is therefore raised to the hold when the hold is larger.
 *
 * Three qualifications, all deliberate:
 *  - `cap_fields == 0` stays uncapped; 0 means "no cap", not "cap of zero".
 *  - an explicit GE_TICKMAXSTEP is obeyed exactly. It is how this cap is
 *    measured, so the mechanism must not override the experiment measuring it.
 *  - without hw pacing there is no priced hold to trust, so nothing moves. A
 *    window drag in any other mode is still capped.
 *
 * The raise is bounded by the caller's hold, which portHwPaceFields() itself
 * caps at 30 fields - the model's own ceiling, not a number invented here. */
uint32_t portStatsStepCap(uint32_t cap_fields, uint32_t hold_fields, int hw_pace, int cap_explicit);

/** The text midas.ini's `tickhz` line should hold for a resolved tick rate.
 *
 * -1 is hw pacing and is written as the literal "hw"; 0 means uncapped and any
 * positive value is the rate in Hz. The text is returned in `buf` (at least
 * PORT_TICK_INI_BUF bytes) and `buf` is returned, or NULL when there is no text
 * to write at all.
 *
 * The NULL case is the point: video.c keeps the resolved rate in an int and
 * uses -2 as a "re-derive this" sentinel inside videoCoupleTickToMaxFps. -2 is
 * a state, not a rate, and writing it would put a value in the file the loader
 * cannot resolve - the exact defect this function exists to prevent (a file
 * whose `tickhz` says something other than what was running).
 *
 * Lives here, beside the rest of the tick arithmetic, because it is the only
 * pure translation unit the tests can reach: port/tests/tick_stats.c checks the
 * text without a window, a GPU or a ROM. */
#define PORT_TICK_INI_BUF 12
const char *portTickIniValue(int tickhz, char *buf, unsigned len);

/** D4: the tick baseline to measure this window against.
 *
 * lvlStageLoad() zeroes g_GlobalTickCount (src/game/lv.c) and the stat pass
 * keeps its baseline in a static in another translation unit, so it could not
 * see the reset: the first window after a load computed `0 - last` and
 * published -2x the pre-load tick count (TPS -600 after five seconds of play,
 * for up to 500 ms) into both the overlay and audio_pump.log.
 *
 * A counter that went backwards is a reset, not a measurement, so the baseline
 * is re-seated to `now` and that window reads 0. A forward or equal move is
 * normal and keeps `last`. */
int32_t portStatsReseatTickBaseline(int32_t last, int32_t now);

#ifdef __cplusplus
}
#endif

#endif /* PORT_TICK_STATS_H */
