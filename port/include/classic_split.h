#ifndef PORT_CLASSIC_SPLIT_H
#define PORT_CLASSIC_SPLIT_H

#include <stddef.h>

/*
 * The one-time [display] n64_aa -> classic_mode split, as a pure decision.
 *
 * Before Plan Phase 3, `n64_aa` meant two things at once: the VI AA bit AND
 * "Classic Mode is on". An old file's `n64_aa=1` therefore has to become BOTH
 * `classic_mode=1` (the preset) and `n64_aa=1` (the bit), or the player boots
 * into a preset reported off with its values still forced - the half-state the
 * audit spent a phase removing.
 *
 * The decision is here, not in videoLoadConfig, for the reason video_env.c
 * exists: a migration rewrites the player's real midas.ini, and one that runs
 * twice would undo a deliberate `classic_mode=0`. That property is worth a test.
 *
 * The caller reads the file and passes what it saw. Nothing here opens a file,
 * reads the environment or touches a global. port/tests/classic_split.c drives
 * it; scripts/port/test_the_tests.sh checks that test can fail.
 *
 * The marker (`[display] classic_split=1`) is the once-only gate, the same
 * shape as the analog_* bind upgrade in videoLoadConfig. It is checked BEFORE
 * anything else: a file that carries it is never migrated again, whatever its
 * n64_aa says.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* [display] n64_aa as loaded: the AA bit. 0 when the line is absent. */
    int n64_aa;
    /* [display] classic_mode as loaded. Only meaningful when had_classic_mode. */
    int classic_mode;
    /* 1 = the file carried a classic_mode line. A file that has one needs no
     * migration: the two meanings were already separate when it was written. */
    int had_classic_mode;
    /* 1 = the file carried the classic_split marker. */
    int had_split_marker;
} PortClassicSplitIn;

typedef struct {
    /* The preset's value after the pass: the migrated one, or the file's own. */
    int classic_mode;
    /* 1 = the caller must write classic_mode and the marker to midas.ini. */
    int migrate;
} PortClassicSplitOut;

/*
 * Fill `out` from `in`. Every field is written.
 *
 * - neither classic_mode nor the marker present: migrate, and the preset takes
 *   the bit's old value (that is what the old key meant);
 * - classic_mode present: untouched, whatever n64_aa says - a player who wants
 *   the bit without the preset is exactly what the split is for;
 * - marker present without classic_mode: untouched too. The marker records that
 *   the pass ran; re-running it would resurrect a preset the player turned off.
 */
void portClassicSplit(const PortClassicSplitIn *in, PortClassicSplitOut *out);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CLASSIC_SPLIT_H */
