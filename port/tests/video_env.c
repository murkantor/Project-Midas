/*
 * The video overrides that can contradict a mode, as a pure rule.
 *
 * These three decisions shipped inside videoInit and were verified only by
 * launching the game and reading the log. That is the same shape as the boot
 * flags, and the same failure mode: an announcement that never fires looks
 * exactly like a run that had nothing to announce.
 *
 * Build standalone:
 *   cc -std=c99 -Iport/include port/tests/video_env.c port/src/video_env.c -o t
 *   ./t
 *
 * WHAT THIS CANNOT TELL YOU
 *
 * That video.c calls these. A green run pins the rule; wiring it up is a fact
 * about that file. scripts/port/test_the_tests.sh covers whether these cases
 * can fail at all.
 */

#include <stdio.h>
#include <stdlib.h>

#include "video_env.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

/* ---- GE_TICKHZ vs Classic Mode ----------------------------------------- */

static void tick_clash_needs_both(void)
{
    CHECK(portEnvTickClash(1, 1) == 1);
    CHECK(portEnvTickClash(1, 0) == 0);
    CHECK(portEnvTickClash(0, 1) == 0);
    CHECK(portEnvTickClash(0, 0) == 0);
}

/* THE regression case for 0e5f786. The announcement used to be gated on the
 * -n64_aa boot flag being the thing that turned the mode on, so a session that
 * was already in Classic Mode (n64_aa=1 in the file, or a Tab press last time)
 * said nothing. `classic_on` alone must be enough - there is no flag argument
 * here precisely so that cannot come back. */
static void tick_clash_does_not_need_a_boot_flag(void)
{
    CHECK(portEnvTickClash(1, 1) == 1);   /* mode on, no flag: still announced */
}

/* Any nonzero means "set"; getenv's presence test is what feeds this. */
static void tick_clash_treats_any_value_as_set(void)
{
    CHECK(portEnvTickClash(1, 2) == 1);
    CHECK(portEnvTickClash(1, -1) == 1);
}

/* ---- GE_ROOMCACHE ------------------------------------------------------ */

static void room_cache_unset_leaves_it_alone(void)
{
    const PortEnvRoomCache r = portEnvRoomCache(0, 2, 0);

    CHECK(r.touched == 0);
    CHECK(r.refused == 0);
}

static void room_cache_applies_off_hw_pacing(void)
{
    PortEnvRoomCache r = portEnvRoomCache(1, 2, 0);

    CHECK(r.touched == 1);
    CHECK(r.value == 2);
    CHECK(r.refused == 0);

    r = portEnvRoomCache(1, 1, 0);
    CHECK(r.value == 1);
    CHECK(r.refused == 0);

    r = portEnvRoomCache(1, 0, 0);
    CHECK(r.value == 0);
    CHECK(r.refused == 0);
}

/* THE regression case for 89b3b50: under hw pacing a nonzero request must be
 * refused rather than silently re-enabling the cache the fill model cannot
 * price. */
static void room_cache_refuses_under_hw_pacing(void)
{
    PortEnvRoomCache r = portEnvRoomCache(1, 2, 1);

    CHECK(r.touched == 1);
    CHECK(r.refused == 1);
    CHECK(r.value == 0);            /* hw pacing wins */
}

/* Asking for 0 under hw pacing agrees with it: no refusal, and no misleading
 * "ignored" line for something that was never contradictory. */
static void room_cache_zero_under_hw_is_not_a_refusal(void)
{
    const PortEnvRoomCache r = portEnvRoomCache(1, 0, 1);

    CHECK(r.touched == 1);
    CHECK(r.refused == 0);
    CHECK(r.value == 0);
}

static void room_cache_clamps_invalid_values(void)
{
    PortEnvRoomCache r = portEnvRoomCache(1, 7, 0);

    CHECK(r.value == 0);            /* clamped */
    CHECK(r.refused == 0);          /* a bad value is not a contradiction */

    r = portEnvRoomCache(1, -3, 0);
    CHECK(r.value == 0);
    CHECK(r.refused == 0);

    /* Clamped BEFORE the hw check, so an invalid value under hw is not reported
     * as a refusal either. */
    r = portEnvRoomCache(1, 9, 1);
    CHECK(r.value == 0);
    CHECK(r.refused == 0);
}

/* ---- GE_HWMIPMAPS vs Classic Mode -------------------------------------- */

static void mipmap_clash_needs_the_mode_and_the_part(void)
{
    CHECK(portEnvMipmapClash(1, 1, 0) == 1);
    CHECK(portEnvMipmapClash(1, 1, 1) == 0);    /* agrees with the mode */
    CHECK(portEnvMipmapClash(1, 1, PORT_ENV_UNSET) == 0);
    CHECK(portEnvMipmapClash(1, 0, 0) == 0);     /* part off: the player's call */
    CHECK(portEnvMipmapClash(0, 1, 0) == 0);     /* mode off: plain A/B knob */
    CHECK(portEnvMipmapClash(0, 0, 0) == 0);
}

static void mipmap_unset_and_one_are_never_clashes(void)
{
    CHECK(portEnvMipmapClash(1, 1, PORT_ENV_UNSET) == 0);
    CHECK(portEnvMipmapClash(1, 1, 2) == 0);    /* not an explicit 0 */
}

int main(void)
{
    tick_clash_needs_both();
    tick_clash_does_not_need_a_boot_flag();
    tick_clash_treats_any_value_as_set();

    room_cache_unset_leaves_it_alone();
    room_cache_applies_off_hw_pacing();
    room_cache_refuses_under_hw_pacing();
    room_cache_zero_under_hw_is_not_a_refusal();
    room_cache_clamps_invalid_values();

    mipmap_clash_needs_the_mode_and_the_part();
    mipmap_unset_and_one_are_never_clashes();

    printf("video_env: all checks passed\n");
    return 0;
}
