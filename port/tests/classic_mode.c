/*
 * Classic Mode's forced values, as a pure rule.
 *
 * Drives the shipped functions in port/src/classic_mode.c against value sets
 * computed by hand, so the preset's semantics are pinned without a window, a
 * GPU, a ROM or a midas.ini. The cases below are written from the four defects
 * in docs/port/settings-and-save-audit.md, not from the code, so a rewrite that
 * keeps the bugs fails here.
 *
 * Build standalone:
 *   cc -std=c99 -Iport/include port/tests/classic_mode.c port/src/classic_mode.c -o t
 *   ./t
 *
 * WHAT THIS CANNOT TELL YOU
 *
 * That any surface calls it. The console, the sheet and the watch reach this
 * rule through their own appliers; whether they do is a fact about those files,
 * not about this function. A green run here means the rule is right, which is
 * exactly the part that has been wrong three different ways.
 */

#include <stdio.h>
#include <stdlib.h>

#include "classic_mode.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

/* The real table, in portClassicKey() order: low_res, filtering, anisotropy,
 * maxfps, msaa, mipmaps, filter_2d. Positional in the module; named here so the
 * assertions read like the game rather than like indices. */
enum { K_LOWRES = 0, K_FILTER, K_ANISO, K_MAXFPS, K_MSAA, K_MIPMAPS, K_FILTER2D };
#define NCLASSIC 7

static const int sClassic[NCLASSIC] = { 240, 2, 8, -1, 2, 1, 1 };

/* A non-classic baseline: what a player might have set on their own. */
static const int sMine[NCLASSIC] = { 480, 1, 2, 60, 4, 0, 0 };

static void allOn(int *parts)
{
    int i;

    for (i = 0; i < NCLASSIC; i++) {
        parts[i] = 1;
    }
}

static void plan(ClassicOp op, int mode_on, const int *parts, const int *cur,
                 const int *saved, int saved_n, ClassicPlan *out)
{
    ClassicInput in;

    in.op = op;
    in.mode_on = mode_on;
    in.parts_on = parts;
    in.classic = sClassic;
    in.current = cur;
    in.saved = saved;
    in.saved_n = saved_n;
    in.n = NCLASSIC;
    portClassicPlan(&in, out);
}

/* The value a plan writes to part `i`, or -999 when it does not touch it. */
static int written(const ClassicPlan *p, int i)
{
    int k;

    for (k = 0; k < p->write_n; k++) {
        if (p->write_idx[k] == i) {
            return p->write_val[k];
        }
    }
    return -999;
}

/* ---- 1. mismatch ------------------------------------------------------- */

static void mismatch_consistent(void)
{
    int parts[NCLASSIC];
    int idx[NCLASSIC];

    allOn(parts);
    CHECK(portClassicMismatch(parts, sClassic, sClassic, NCLASSIC, idx) == 0);
}

static void mismatch_each_part(void)
{
    int parts[NCLASSIC];
    int cur[NCLASSIC];
    int idx[NCLASSIC];
    int k;

    allOn(parts);
    /* Each part individually, because a check that only compares the first
     * would pass while six stayed wrong. */
    for (k = 0; k < NCLASSIC; k++) {
        int i;

        for (i = 0; i < NCLASSIC; i++) {
            cur[i] = sClassic[i];
        }
        cur[k] = sClassic[k] + 1;
        CHECK(portClassicMismatch(parts, sClassic, cur, NCLASSIC, idx) == 1);
        CHECK(idx[0] == k);
    }
}

static void mismatch_counts_and_names(void)
{
    int parts[NCLASSIC];
    int idx[NCLASSIC];
    int n;

    allOn(parts);
    /* The state the console defect produced: the mode on, the parts mine. */
    n = portClassicMismatch(parts, sClassic, sMine, NCLASSIC, idx);
    CHECK(n == NCLASSIC);
    CHECK(idx[0] == K_LOWRES);
    CHECK(idx[NCLASSIC - 1] == K_FILTER2D);
}

static void mismatch_ignores_disabled_parts(void)
{
    int parts[NCLASSIC];
    int idx[NCLASSIC];

    /* The player switched every part off on the CLASSIC sheet: nothing is
     * forced, so a non-classic value is not a mismatch. */
    {
        int i;

        for (i = 0; i < NCLASSIC; i++) {
            parts[i] = 0;
        }
    }
    CHECK(portClassicMismatch(parts, sClassic, sMine, NCLASSIC, idx) == 0);

    /* ...and one part switched back on reports only that part. */
    parts[K_ANISO] = 1;
    CHECK(portClassicMismatch(parts, sClassic, sMine, NCLASSIC, idx) == 1);
    CHECK(idx[0] == K_ANISO);
}

/* ---- 2. entering the mode ---------------------------------------------- */

static void toggle_on_snapshots_then_forces(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;
    int i;

    allOn(parts);
    plan(CLASSIC_OP_TOGGLE_ON, 1, parts, sMine, NULL, 0, &p);

    CHECK(p.snapshot == 1);
    CHECK(p.saved_n == NCLASSIC);
    for (i = 0; i < NCLASSIC; i++) {
        CHECK(p.saved[i] == sMine[i]);     /* remembered: the player's values */
        CHECK(written(&p, i) == sClassic[i]);
    }
    CHECK(p.write_n == NCLASSIC);
}

static void toggle_on_snapshots_even_when_already_classic(void)
{
    int parts[NCLASSIC];
    int saved[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    /* Nothing to write, because nothing differs - but the snapshot must still
     * happen: turning the mode off then has to restore these values, and
     * "restore nothing" would leave the mode off with no memory of them. */
    plan(CLASSIC_OP_TOGGLE_ON, 1, parts, sClassic, NULL, 0, &p);
    CHECK(p.snapshot == 1);
    CHECK(p.saved_n == NCLASSIC);
    CHECK(p.write_n == 0);

    /* And with a snapshot already on the inputs, the plan is the same: whether
     * to overwrite an existing snapshot is the CALLER's decision (the !n64_aa
     * guard), not this function's. */
    plan(CLASSIC_OP_TOGGLE_ON, 1, parts, sClassic, saved, NCLASSIC, &p);
    CHECK(p.snapshot == 1);
    CHECK(p.write_n == 0);
}

/* The op and mode_on can disagree only if a caller is confused, and then the
 * answer must be "do not remember": a TOGGLE_ON that is not entering the mode
 * would overwrite the player's snapshot with the forced values. This mutant
 * (dropping `&& in->mode_on`) survived the first version of this test. */
static void toggle_on_with_mode_off_does_not_snapshot(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;
    int i;

    allOn(parts);
    plan(CLASSIC_OP_TOGGLE_ON, 0, parts, sMine, NULL, 0, &p);
    CHECK(p.snapshot == 0);
    CHECK(p.saved_n == 0);
    /* mode_on is 0, so it restores rather than forces; with no saved values
     * there is nothing to restore. */
    CHECK(p.write_n == 0);

    /* With a snapshot present, the restore path applies and the snapshot is
     * still not retaken. */
    plan(CLASSIC_OP_TOGGLE_ON, 0, parts, sClassic, sMine, NCLASSIC, &p);
    CHECK(p.snapshot == 0);
    CHECK(p.write_n == NCLASSIC);
    for (i = 0; i < NCLASSIC; i++) {
        CHECK(written(&p, i) == sMine[i]);
    }
}

/* FIX never snapshots, in either direction - asserted for the OFF direction
 * too, which the first version only did for ON. */
static void fix_never_snapshots_in_either_direction(void)
{
    int parts[NCLASSIC];
    int saved[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    plan(CLASSIC_OP_FIX, 0, parts, sClassic, sMine, NCLASSIC, &p);
    CHECK(p.snapshot == 0);

    plan(CLASSIC_OP_FIX, 1, parts, sMine, saved, NCLASSIC, &p);
    CHECK(p.snapshot == 0);
}

/* A reused ClassicPlan must not carry state over. Without the memset in
 * portClassicPlan a second call into the same struct keeps the first call's
 * snapshot flag, and the applier writes a stale snapshot over classic_saved -
 * the player-value-destroying bug this whole rule exists to prevent. */
static void plan_reuse_clears_previous_state(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    plan(CLASSIC_OP_TOGGLE_ON, 1, parts, sMine, NULL, 0, &p);
    CHECK(p.snapshot == 1);
    CHECK(p.write_n == NCLASSIC);

    /* A FIX into the SAME struct must not inherit either field. */
    plan(CLASSIC_OP_FIX, 0, parts, sClassic, sMine, NCLASSIC, &p);
    CHECK(p.snapshot == 0);
    CHECK(p.saved_n == 0);

    plan(CLASSIC_OP_FIX, 1, parts, sClassic, NULL, 0, &p);
    CHECK(p.write_n == 0);
}

/* A NULL field array with n > 0 must be refused, not dereferenced. Each of the
 * three NULL checks is removed one at a time by the mutation audit; all three
 * need a case. */
static void null_fields_are_refused(void)
{
    ClassicInput in;
    ClassicPlan p;
    int parts[NCLASSIC];
    int cur[NCLASSIC];

    allOn(parts);
    in.op = CLASSIC_OP_TOGGLE_ON;
    in.mode_on = 1;
    in.saved = NULL;
    in.saved_n = 0;
    in.classic = sClassic;
    in.current = cur;
    in.parts_on = parts;
    in.n = NCLASSIC;

    in.parts_on = NULL;
    portClassicPlan(&in, &p);
    CHECK(p.write_n == 0);
    CHECK(p.snapshot == 0);

    in.parts_on = parts;
    in.classic = NULL;
    portClassicPlan(&in, &p);
    CHECK(p.write_n == 0);
    CHECK(p.snapshot == 0);

    in.classic = sClassic;
    in.current = NULL;
    portClassicPlan(&in, &p);
    CHECK(p.write_n == 0);
    CHECK(p.snapshot == 0);
}

/* The buffer bound, not just the n bound: saved_n past CLASSIC_MODE_MAX must not
 * walk the plan's fixed arrays. */
static void saved_n_beyond_the_buffer_is_clamped(void)
{
    ClassicInput in;
    ClassicPlan p;
    int parts[NCLASSIC];
    int cur[NCLASSIC];
    int i;

    allOn(parts);
    for (i = 0; i < NCLASSIC; i++) {
        cur[i] = sClassic[i];
    }
    in.op = CLASSIC_OP_TOGGLE_OFF;
    in.mode_on = 0;
    in.parts_on = parts;
    in.classic = sClassic;
    in.current = cur;
    in.saved = sMine;
    in.saved_n = CLASSIC_MODE_MAX + 8;   /* past the buffer AND past n */
    in.n = NCLASSIC;
    portClassicPlan(&in, &p);
    CHECK(p.write_n == NCLASSIC);
}

/* n longer than the buffer must be clamped too, or the loops run off the end. */
static void n_beyond_the_buffer_is_clamped(void)
{
    ClassicInput in;
    ClassicPlan p;
    int parts[CLASSIC_MODE_MAX];
    int cur[CLASSIC_MODE_MAX];
    int cls[CLASSIC_MODE_MAX];
    int i;

    for (i = 0; i < CLASSIC_MODE_MAX; i++) {
        parts[i] = 1;
        cur[i] = sMine[i % NCLASSIC];
        cls[i] = sClassic[i % NCLASSIC];
    }
    in.op = CLASSIC_OP_TOGGLE_ON;
    in.mode_on = 1;
    in.parts_on = parts;
    in.classic = cls;
    in.current = cur;
    in.saved = NULL;
    in.saved_n = 0;
    in.n = CLASSIC_MODE_MAX + 64;
    portClassicPlan(&in, &p);
    CHECK(p.saved_n == CLASSIC_MODE_MAX);
    CHECK(p.write_n == CLASSIC_MODE_MAX);
}

static void toggle_on_respects_disabled_parts(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    parts[K_MSAA] = 0;
    plan(CLASSIC_OP_TOGGLE_ON, 1, parts, sMine, NULL, 0, &p);

    CHECK(p.snapshot == 1);
    CHECK(p.saved_n == NCLASSIC);            /* remembered regardless */
    CHECK(written(&p, K_MSAA) == -999);      /* but not forced */
    CHECK(written(&p, K_LOWRES) == 240);
    CHECK(p.write_n == NCLASSIC - 1);
}

/* ---- 3. leaving the mode ----------------------------------------------- */

static void toggle_off_restores(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;
    int i;

    allOn(parts);
    plan(CLASSIC_OP_TOGGLE_OFF, 0, parts, sClassic, sMine, NCLASSIC, &p);

    CHECK(p.snapshot == 0);
    CHECK(p.write_n == NCLASSIC);
    for (i = 0; i < NCLASSIC; i++) {
        CHECK(written(&p, i) == sMine[i]);
    }
}

static void toggle_off_with_short_snapshot(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    /* An ini from before the table grew: four fields saved. Only those four go
     * back, and the rest keep whatever the mode left them. */
    plan(CLASSIC_OP_TOGGLE_OFF, 0, parts, sClassic, sMine, 4, &p);
    CHECK(p.write_n == 4);
    CHECK(written(&p, K_LOWRES) == 480);
    CHECK(written(&p, K_MAXFPS) == 60);
    CHECK(written(&p, K_MSAA) == -999);      /* beyond the saved four */
    CHECK(written(&p, K_FILTER2D) == -999);
}

static void toggle_off_with_no_snapshot(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    /* Hand-edited n64_aa=1 with no classic_saved: leaving the mode has nothing
     * to restore, and must not invent values. */
    plan(CLASSIC_OP_TOGGLE_OFF, 0, parts, sClassic, NULL, 0, &p);
    CHECK(p.snapshot == 0);
    CHECK(p.write_n == 0);
}

static void toggle_off_skips_disabled_parts(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    parts[K_ANISO] = 0;
    plan(CLASSIC_OP_TOGGLE_OFF, 0, parts, sClassic, sMine, NCLASSIC, &p);
    CHECK(written(&p, K_ANISO) == -999);     /* the player's own setting stays */
    CHECK(written(&p, K_LOWRES) == 480);
}

/* ---- 4. FIX: repair without remembering -------------------------------- */

static void fix_on_forces_without_snapshoting(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    /* The half-state the audit found: n64_aa=1 with the parts left alone.
     * Repairing it must NOT re-snapshot - the real pre-mode values are already
     * in classic_saved, and overwriting them with the forced ones would make
     * turning the mode off restore the mode's own settings. */
    plan(CLASSIC_OP_FIX, 1, parts, sMine, NULL, 0, &p);
    CHECK(p.snapshot == 0);
    CHECK(p.write_n == NCLASSIC);
    CHECK(written(&p, K_LOWRES) == 240);

    /* Already consistent: a true no-op, as a repair should be. */
    plan(CLASSIC_OP_FIX, 1, parts, sClassic, NULL, 0, &p);
    CHECK(p.snapshot == 0);
    CHECK(p.write_n == 0);
}

static void fix_off_restores(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;

    allOn(parts);
    plan(CLASSIC_OP_FIX, 0, parts, sClassic, sMine, NCLASSIC, &p);
    CHECK(p.snapshot == 0);
    CHECK(p.write_n == NCLASSIC);
    CHECK(written(&p, K_LOWRES) == 480);
}

/* ---- 5. edges ---------------------------------------------------------- */

static void no_parts_does_nothing(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;
    int i;

    for (i = 0; i < NCLASSIC; i++) {
        parts[i] = 0;
    }
    plan(CLASSIC_OP_TOGGLE_ON, 1, parts, sMine, NULL, 0, &p);
    CHECK(p.write_n == 0);

    plan(CLASSIC_OP_TOGGLE_OFF, 0, parts, sClassic, sMine, NCLASSIC, &p);
    CHECK(p.write_n == 0);
}

static void degenerate_counts_are_safe(void)
{
    ClassicInput in;
    ClassicPlan p;
    int parts[NCLASSIC];
    int idx[NCLASSIC];

    allOn(parts);
    in.op = CLASSIC_OP_TOGGLE_ON;
    in.mode_on = 1;
    in.parts_on = parts;
    in.classic = sClassic;
    in.current = sMine;
    in.saved = NULL;
    in.saved_n = 0;
    in.n = 0;
    portClassicPlan(&in, &p);
    CHECK(p.write_n == 0);
    CHECK(p.snapshot == 0);                  /* no parts: nothing to remember */

    /* NULL inputs must not crash: the module is called from surfaces whose
     * tables can be absent in a stripped build. Scribble over `p` first, so this
     * proves the call CLEARED it rather than that it was already clear. */
    p.snapshot = 1;
    p.saved_n = 99;
    p.write_n = 99;
    portClassicPlan(NULL, &p);
    CHECK(p.write_n == 0);
    CHECK(p.snapshot == 0);
    CHECK(p.saved_n == 0);

    CHECK(portClassicMismatch(NULL, sClassic, sMine, NCLASSIC, idx) == 0);
    /* idx_out is optional - the console and the boot report both count first and
     * name second - so a NULL buffer must count without writing. */
    CHECK(portClassicMismatch(parts, sClassic, sMine, NCLASSIC, NULL) == NCLASSIC);
    CHECK(portClassicMismatch(parts, sClassic, sMine, 0, idx) == 0);

    /* saved_n longer than n cannot walk off the end of the arrays. */
    in.n = NCLASSIC;
    in.current = sClassic;      /* the mode's values, to be given back */
    in.saved = sMine;
    in.saved_n = NCLASSIC + 5;
    in.op = CLASSIC_OP_TOGGLE_OFF;
    in.mode_on = 0;
    portClassicPlan(&in, &p);
    CHECK(p.write_n == NCLASSIC);   /* clamped to n, not read past it */
}

static void writes_are_in_part_order(void)
{
    int parts[NCLASSIC];
    ClassicPlan p;
    int k;

    allOn(parts);
    parts[K_FILTER] = 0;
    plan(CLASSIC_OP_TOGGLE_ON, 1, parts, sMine, NULL, 0, &p);
    /* The applier lands them in this order; a shuffled plan would make the
     * sheet's apply order-dependent, which is the class of bug Phase 2 has to
     * avoid introducing. */
    for (k = 1; k < p.write_n; k++) {
        CHECK(p.write_idx[k] > p.write_idx[k - 1]);
    }
}

int main(void)
{
    mismatch_consistent();
    mismatch_each_part();
    mismatch_counts_and_names();
    mismatch_ignores_disabled_parts();

    toggle_on_snapshots_then_forces();
    toggle_on_snapshots_even_when_already_classic();
    toggle_on_with_mode_off_does_not_snapshot();
    fix_never_snapshots_in_either_direction();
    plan_reuse_clears_previous_state();
    null_fields_are_refused();
    saved_n_beyond_the_buffer_is_clamped();
    n_beyond_the_buffer_is_clamped();
    toggle_on_respects_disabled_parts();

    toggle_off_restores();
    toggle_off_with_short_snapshot();
    toggle_off_with_no_snapshot();
    toggle_off_skips_disabled_parts();

    fix_on_forces_without_snapshoting();
    fix_off_restores();

    no_parts_does_nothing();
    degenerate_counts_are_safe();
    writes_are_in_part_order();

    printf("classic_mode: all checks passed\n");
    return 0;
}
