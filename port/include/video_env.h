#ifndef PORT_VIDEO_ENV_H
#define PORT_VIDEO_ENV_H

/*
 * The video environment overrides that can contradict a mode, as pure functions.
 *
 * Three GE_* knobs interact with Classic Mode or with hardware pacing, and each
 * one used to decide inline inside videoInit - where nothing could test it, and
 * where two of the three shipped with a silent contradiction:
 *
 *   GE_TICKHZ    beats hw pacing's 60 while holds are still priced. Announced
 *                whenever the mode is on, which used to mean "only when the
 *                -n64_aa flag turned it on" and was silent for every other way
 *                in (n64_aa=1 in the file, a Tab press last session).
 *   GE_ROOMCACHE hw pacing owns room_cache; applied after the boot flags it used
 *                to switch the cache back ON under the fill model, silently.
 *   GE_HWMIPMAPS mipmaps belong to Classic Mode; =0 contradicted a mode that
 *                reported itself on, and nothing logged the mipmap mode at all.
 *
 * The DECISIONS live here. The logging and the assignment stay in video.c, so
 * these stay pure - no SDL, no getenv, no statics. port/tests/video_env.c drives
 * them; scripts/port/test_the_tests.sh checks the tests can fail.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GE_HWMIPMAPS is a tri-state: unset, or an explicit 0/1. */
#define PORT_ENV_UNSET (-1)

/* 1 = announce that GE_TICKHZ overrides hw pacing's tick rate while holds are
 * still priced. Depends on the RESULTING mode state, never on whether the
 * -n64_aa flag is what turned it on. */
int portEnvTickClash(int classic_on, int tick_env_set);

/* What [display] room_cache should end up as, after GE_ROOMCACHE is applied.
 *
 * hw pacing forces room_cache to 0 (the fill model reads the interpreter's
 * triangles), so a knob asking for anything else is refused LOUDLY rather than
 * silently re-enabling the one combination the loader refuses. An invalid value
 * is clamped to 0 without a refusal: that is a bad value, not a contradiction. */
typedef struct {
    int touched;   /* 0 = GE_ROOMCACHE unset; leave the ini's value alone */
    int value;     /* the room_cache to use when touched                    */
    int refused;   /* 1 = the env asked for nonzero under hw pacing         */
} PortEnvRoomCache;

PortEnvRoomCache portEnvRoomCache(int env_set, int env_val, int hw_pace);

/* 1 = announce that GE_HWMIPMAPS=0 contradicts Classic Mode being on.
 * `mipmap_env_on` is PORT_ENV_UNSET, 0 or 1; only an explicit 0 clashes, and
 * only when the mode owns mipmaps (`part_on`). */
int portEnvMipmapClash(int classic_on, int mipmap_part_on, int mipmap_env_on);

#ifdef __cplusplus
}
#endif

#endif /* PORT_VIDEO_ENV_H */
