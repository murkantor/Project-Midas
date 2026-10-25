/*
 * video_env - the three video overrides that can contradict a mode.
 * See video_env.h for why each is a decision rather than a line in videoInit.
 */

#include "video_env.h"

int portEnvTickClash(int classic_on, int tick_env_set)
{
    /* The mode decides, not the flag that entered it. That is the whole point:
     * gating on the boot flag was silent for every other way of being in the
     * mode, which is the case a reader is least likely to have in mind. */
    return (classic_on != 0) && (tick_env_set != 0);
}

PortEnvRoomCache portEnvRoomCache(int env_set, int env_val, int hw_pace)
{
    PortEnvRoomCache r;

    r.touched = 0;
    r.value = 0;
    r.refused = 0;

    if (!env_set) {
        return r;
    }
    r.touched = 1;

    r.value = env_val;
    if (r.value < 0 || r.value > 2) {
        r.value = 0;   /* bad value: clamped, and not a contradiction */
    }
    if (hw_pace && r.value != 0) {
        /* hw pacing owns it. Refused, and the caller says so. */
        r.refused = 1;
        r.value = 0;
    }
    return r;
}

int portEnvMipmapClash(int classic_on, int mipmap_part_on, int mipmap_env_on)
{
    if (!classic_on || !mipmap_part_on) {
        return 0;   /* nothing is being contradicted */
    }
    /* Only an explicit 0 clashes. Unset leaves the mode's own answer alone, and
     * 1 agrees with it. */
    return (mipmap_env_on == 0) ? 1 : 0;
}
