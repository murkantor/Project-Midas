/*
 * The one-time n64_aa -> classic_mode split, as a pure rule.
 *
 * The migration rewrites the player's real midas.ini, so the two things worth
 * pinning are the mapping and the guard: an old `n64_aa=1` must resurrect the
 * preset, and a file that already has an answer of its own must never be
 * touched again. Both were executed against a live file in the Phase 3
 * verification; this is what keeps them true without a launch.
 *
 * Build standalone:
 *   cc -std=c99 -Iport/include port/tests/classic_split.c port/src/classic_split.c -o t
 *   ./t
 *
 * WHAT THIS CANNOT TELL YOU
 *
 * That videoLoadConfig feeds it what the file said, or writes the result back.
 * Those are facts about that function, checked by launching against a crafted
 * ini (see the commit message for the Phase 3 migration).
 */

#include <stdio.h>
#include <stdlib.h>

#include "classic_split.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

/* A file with neither key nor marker: exactly what a pre-split midas.ini is. */
static PortClassicSplitIn old_file(int n64_aa)
{
    PortClassicSplitIn in;

    in.n64_aa = n64_aa;
    in.classic_mode = 0;
    in.had_classic_mode = 0;
    in.had_split_marker = 0;
    return in;
}

static PortClassicSplitOut run(PortClassicSplitIn in)
{
    PortClassicSplitOut out;

    out.classic_mode = -1;   /* a caller that forgets to write is caught below */
    out.migrate = -1;
    portClassicSplit(&in, &out);
    return out;
}

/* ---- the mapping ------------------------------------------------------- */

/* `n64_aa=1` used to mean both. Both is what must come back: the preset on
 * (the seven values, the pillars, the filter) AND the bit still set, or the
 * player's classic look loses its N64 edges on the upgrade. */
static void old_on_migrates_to_both(void)
{
    const PortClassicSplitOut o = run(old_file(1));

    CHECK(o.migrate == 1);
    CHECK(o.classic_mode == 1);
}

static void old_off_migrates_to_both_off(void)
{
    const PortClassicSplitOut o = run(old_file(0));

    CHECK(o.migrate == 1);
    CHECK(o.classic_mode == 0);
}

/* The input only ever holds 0/1 from the parser, but the rule normalizes: a
 * migration is not the place to propagate a stray 7 into a toggle. */
static void any_nonzero_is_on(void)
{
    const PortClassicSplitOut o = run(old_file(7));

    CHECK(o.migrate == 1);
    CHECK(o.classic_mode == 1);
}

/* ---- the guard --------------------------------------------------------- */

/* THE reason the marker exists. A player who turned the preset off after the
 * migration has `classic_mode=0` next to an untouched `n64_aa=1` (they want the
 * bit, not the preset). Re-running the migration would put the preset back on
 * and silently force seven settings they had dismissed. */
static void explicit_off_is_never_overwritten(void)
{
    PortClassicSplitIn in = old_file(1);

    in.classic_mode = 0;
    in.had_classic_mode = 1;

    CHECK(run(in).migrate == 0);
    CHECK(run(in).classic_mode == 0);
}

/* The same file one launch later: the marker is on disk, the classic_mode line
 * is there too. Still nothing to do. */
static void marker_and_classic_mode_are_quiet(void)
{
    PortClassicSplitIn in = old_file(1);

    in.classic_mode = 0;
    in.had_classic_mode = 1;
    in.had_split_marker = 1;

    CHECK(run(in).migrate == 0);
    CHECK(run(in).classic_mode == 0);
}

/* The marker alone is enough. This is the belt to classic_mode's braces: a
 * write that landed the marker but lost the classic_mode line (a hand edit, a
 * merge) must not be read as "an old file, migrate it". */
static void marker_alone_is_quiet(void)
{
    PortClassicSplitIn in = old_file(1);

    in.had_split_marker = 1;

    CHECK(run(in).migrate == 0);
    CHECK(run(in).classic_mode == 0);
}

/* A file that already carries classic_mode=1 keeps it, whatever the bit says.
 * `classic_mode=1 n64_aa=0` is a state the split deliberately makes reachable
 * (N64 edges off, preset on); a migration that "helpfully" synced them would
 * erase it on every boot. */
static void preset_on_with_bit_off_survives(void)
{
    PortClassicSplitIn in = old_file(0);

    in.classic_mode = 1;
    in.had_classic_mode = 1;

    CHECK(run(in).migrate == 0);
    CHECK(run(in).classic_mode == 1);
}

/* ---- every field is written ------------------------------------------- */

/* The struct is documented as fully written; a caller may pass a stack struct
 * it never cleared. An early return that left the fields alone would read as
 * garbage to that caller. */
static void all_fields_are_written(void)
{
    PortClassicSplitIn in;
    PortClassicSplitOut out;

    in.n64_aa = 0;
    in.classic_mode = 0;
    in.had_classic_mode = 0;
    in.had_split_marker = 0;
    out.classic_mode = 12345;
    out.migrate = 12345;

    portClassicSplit(&in, &out);
    CHECK(out.classic_mode == 0);   /* the sentinel is gone, not merely sane */
    CHECK(out.migrate == 1);
}

int main(void)
{
    old_on_migrates_to_both();
    old_off_migrates_to_both_off();
    any_nonzero_is_on();

    explicit_off_is_never_overwritten();
    marker_and_classic_mode_are_quiet();
    marker_alone_is_quiet();
    preset_on_with_bit_off_survives();

    all_fields_are_written();

    printf("classic_split: all checks passed\n");
    return 0;
}
