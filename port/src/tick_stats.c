/*
 * Frame/tick statistics - implementation.
 *
 * Pure functions: no SDL, no engine headers, no globals, no clock reads. The
 * stat pass in port/fast3d/gfx_sdl2.cpp supplies counter deltas and publishes
 * the results; port/tests/tick_stats.c drives the same functions against
 * windows computed by hand.
 *
 * The one rule worth restating here: every division rounds, and the numerator
 * is widened to 64 bits before the multiply. `delta * scale * 1000` overflows
 * 32 bits as soon as a delta is large (a 10 s stall window, a debugger pause),
 * and a wrapped numerator reads as a plausible but wildly wrong rate.
 */

#include <stdio.h>

#include "tick_stats.h"

uint32_t portStatsRatePerSec(uint32_t delta, uint32_t dt_ms, uint32_t scale)
{
    if (dt_ms == 0 || scale == 0) {
        return 0;
    }

    /* delta per dt_ms -> per 1000 ms, scaled, rounded to nearest. */
    return (uint32_t) ((((uint64_t) delta * scale * 1000u) + dt_ms / 2u) / dt_ms);
}

uint32_t portStatsFieldsPerTickX100(uint32_t fields, uint32_t ticks)
{
    if (ticks == 0) {
        return 0; /* nothing admitted: no ratio to report */
    }

    return (uint32_t) ((((uint64_t) fields * 100u) + ticks / 2u) / ticks);
}

int32_t portStatsReseatTickBaseline(int32_t last, int32_t now)
{
    /* Signed comparison on purpose: the counter is signed in the engine, and a
     * reset makes `now` plainly less than `last`. */
    return (now < last) ? now : last;
}

uint32_t portStatsMeasuredFields(uint32_t elapsed_counts, uint32_t field_counts)
{
    if (field_counts == 0) {
        return 0;
    }

    return elapsed_counts / field_counts;
}

uint32_t portStatsCreditedFields(uint32_t measured, uint32_t cap_fields)
{
    if (cap_fields == 0 || measured <= cap_fields) {
        return measured;
    }

    return cap_fields;
}

uint32_t portStatsStepCap(uint32_t cap_fields, uint32_t hold_fields, int hw_pace, int cap_explicit)
{
    if (cap_fields == 0 || cap_explicit || !hw_pace) {
        return cap_fields; /* uncapped, or the cap is not ours to move */
    }

    return (hold_fields > cap_fields) ? hold_fields : cap_fields;
}

const char *portTickIniValue(int tickhz, char *buf, unsigned len)
{
    if (buf == NULL || len == 0) {
        return NULL;
    }
    if (tickhz == -1) {
        if (len < 3) {
            return NULL;
        }
        buf[0] = 'h';
        buf[1] = 'w';
        buf[2] = '\0';
        return buf;
    }
    if (tickhz < 0) {
        return NULL; /* -2: the coupling's "re-derive" sentinel, not a rate */
    }

    snprintf(buf, (size_t) len, "%d", tickhz);
    return buf;
}
