/*
 * Classic Mode through the option table's seam.
 *
 * port/tests/classic_mode.c pins the RULE (what a transition writes). This file
 * pins the WIRING: that the shipped port_options.c applies the rule when
 * classic_mode changes, from every setter, exactly once, in the right order -
 * and that n64_aa does NOT.
 *
 * That seam is where the defects have been. The console bug was a missing call
 * (the rule was right the whole time), and the Phase 2 rewrite could reintroduce
 * it from the other side: a leftover explicit apply in portClassicToggle would
 * run the transition twice, leave the same VALUES, and silently replace the
 * player's snapshot with the forced one.
 *
 * So most of these cases assert WORK, not values: how many saves, how many
 * snapshots, and in what order the writes landed. See port_options_fake.h.
 *
 * PHASE 3 (the n64_aa split) is what the last section is for. Until the keys
 * were separated, "changing the bit does not force the seven" was not a state
 * the program could be in, so it could not be asserted. It is now the assertion
 * that holds the whole split together.
 *
 * WHAT THIS CANNOT TELL YOU
 *
 * That the player sees the right thing. Tab, the watch and the folder sheet each
 * render their own view and have no headless harness; what is asserted here is
 * the state they would all be reading.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>
#include "port_options.h"
#include "port_options_fake.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

/* Declared by port_options.c and used by the menus; not in the public header. */
s32 portOptFind(const char *key);
s32 portOptSetValue(s32 i, s32 v);
const char *portOptKey(s32 i);
void portClassicToggle(void);

#define NCLASSIC 7
static const int sClassic[NCLASSIC] = { 240, 2, 8, -1, 2, 1, 1 };
static const int sMine[NCLASSIC] = { 480, 1, 2, 60, 4, 0, 0 };
static const char *const sClassicKeys[NCLASSIC] = {
    "low_res", "filtering", "anisotropy", "maxfps", "msaa", "mipmaps", "filter_2d"
};

static void expect_classic_forced(void)
{
    int i;

    CHECK(fakeGet("classic_mode") == 1);
    for (i = 0; i < NCLASSIC; i++) {
        CHECK(fakeGet(sClassicKeys[i]) == sClassic[i]);
    }
}

static void expect_mine(void)
{
    int i;

    for (i = 0; i < NCLASSIC; i++) {
        CHECK(fakeGet(sClassicKeys[i]) == sMine[i]);
    }
}

static void expect_snapshot_is_mine(void)
{
    int i;

    CHECK(fakeSavedN() == NCLASSIC);
    for (i = 0; i < NCLASSIC; i++) {
        CHECK(fakeSaved(i) == sMine[i]);
    }
}

static void set_text(const char *key, const char *text)
{
    const s32 i = portOptFind(key);

    CHECK(i >= 0);
    CHECK(portOptSetIni(i, text) == 1);
}

/* ---- the table itself -------------------------------------------------- */

/* Neither key is the first row. An implementation that special-cased index 0
 * would pass a fake whose tables started there and fail in the game. */
static void the_mode_keys_are_not_row_zero(void)
{
    CHECK(portOptFind("classic_mode") > 0);
    CHECK(strcmp(portOptKey(portOptFind("classic_mode")), "classic_mode") == 0);

    CHECK(portOptFind("n64_aa") > 0);
    CHECK(strcmp(portOptKey(portOptFind("n64_aa")), "n64_aa") == 0);

    /* One row each, and different rows: a table holding the same key twice
     * would make portOptFind's answer depend on where the loop started. */
    CHECK(portOptFind("classic_mode") != portOptFind("n64_aa"));
}

/* ---- entering, by text ------------------------------------------------- */

static void text_on_forces_and_snapshots(void)
{
    fakeReset();
    set_text("classic_mode", "1");

    expect_classic_forced();
    expect_snapshot_is_mine();
}

/* Every spelling the parser accepts must run the preset. A guard written
 * against the TEXT ("0" only) rather than the VALUE passes a test that only
 * tries "1" and "0". */
static void all_true_spellings_transition(void)
{
    static const char *const yes[] = { "1", "on", "true", "yes" };
    static const char *const no[] = { "0", "off", "false", "no" };
    int i;

    for (i = 0; i < 4; i++) {
        fakeReset();
        set_text("classic_mode", yes[i]);
        expect_classic_forced();
        expect_snapshot_is_mine();

        fakeClassicOn();
        set_text("classic_mode", no[i]);
        CHECK(fakeGet("classic_mode") == 0);
        expect_mine();
    }
}

/* A value the toggle cannot take is refused, and a refusal must not run the
 * preset: forcing values with the flag unchanged is the audit's half-state,
 * produced by the fix itself. */
static void refused_set_does_not_transition(void)
{
    const s32 mode = portOptFind("classic_mode");

    fakeReset();
    CHECK(portOptSetIni(mode, "2") == 0);    /* a TOGGLE takes only on/off */

    CHECK(fakeGet("classic_mode") == 0);
    expect_mine();
    CHECK(fakeSnapshotCalls() == 0);
}

/* Same when the setter itself refuses (a live key refusing an out-of-range
 * value). optSet returns 0, portOptSetValue passes that on, and the preset must
 * not have run - forcing values with the flag unchanged is the audit's
 * half-state, produced by the fix itself. */
static void failed_setter_does_not_transition(void)
{
    const s32 mode = portOptFind("classic_mode");

    fakeReset();
    fakeRefuse("classic_mode");

    CHECK(portOptSetValue(mode, 1) == 0);   /* the refusal is reported */
    CHECK(fakeGet("classic_mode") == 0);    /* and the flag did not move */
    expect_mine();                          /* nor did anything else */
    CHECK(fakeSnapshotCalls() == 0);
}

/* ---- exactly once ------------------------------------------------------ */

/* One transition, one save. portOptSave writes once per non-empty section, and
 * the table has [display] and [input], so a single save is two writer calls.
 * A double apply - the leftover-explicit-call bug - is four. */
static void one_transition_is_one_save(void)
{
    fakeReset();
    fakeIniCallsReset();
    set_text("classic_mode", "1");
    CHECK(fakeIniCalls() == 2);

    fakeClassicOn();
    fakeIniCallsReset();
    set_text("classic_mode", "0");
    CHECK(fakeIniCalls() == 2);
}

/* One transition, one snapshot. This is the assertion a value-only test cannot
 * make: applying twice ends at the same values but takes the snapshot twice,
 * and the second one holds the FORCED values. */
static void one_transition_is_one_snapshot(void)
{
    fakeReset();
    set_text("classic_mode", "1");
    CHECK(fakeSnapshotCalls() == 1);

    /* And the snapshot it took is the player's, not the mode's. This is the
     * second half: a double apply would have left 240/2/8/... here. */
    expect_snapshot_is_mine();
}

/* A repeated ON is not a transition at all: no snapshot, and (because nothing
 * differs) no values moved. */
static void repeated_on_is_not_a_transition(void)
{
    fakeReset();
    set_text("classic_mode", "1");
    CHECK(fakeSnapshotCalls() == 1);

    set_text("classic_mode", "1");
    CHECK(fakeSnapshotCalls() == 1);         /* still one */
    expect_classic_forced();
    expect_snapshot_is_mine();
}

/* ---- ordering ---------------------------------------------------------- */

/* The flag must be written BEFORE the values it forces, or a failure between
 * the two leaves classic values under a mode reported off. The ordered log sees
 * this; comparing final values cannot. */
static void flag_is_written_before_the_values(void)
{
    const char *key = NULL;
    int val = 0;

    fakeReset();
    set_text("classic_mode", "1");

    CHECK(fakeWriteCount() >= 1 + NCLASSIC);
    CHECK(fakeWriteAt(0, &key, &val) == 1);
    CHECK(strcmp(key, "classic_mode") == 0);
    CHECK(val == 1);
}

/* Nothing may be SAVED before the values have landed: a save first would
 * persist the old values and the new ones would sit unsaved until the next
 * change. The save marker in the log is what makes this visible - a count of
 * saves cannot tell "saved after" from "saved before". */
static void nothing_is_saved_before_the_values_land(void)
{
    const char *key = NULL;
    int i;

    fakeReset();
    set_text("classic_mode", "1");

    for (i = 0; i < fakeWriteCount(); i++) {
        CHECK(fakeWriteAt(i, &key, NULL) == 1);
        CHECK(strcmp(key, FAKE_SAVE_MARK) != 0);   /* not yet */
        if (strcmp(key, "filter_2d") == 0) {
            break;                                  /* last value has landed */
        }
    }
    CHECK(fakeIniCalls() > 0);                      /* ...and then it is saved */
    CHECK(fakeWriteAt(fakeWriteCount() - 1, &key, NULL) == 1);
    CHECK(strcmp(key, FAKE_SAVE_MARK) == 0);
}

/* ---- the other setters ------------------------------------------------- */

/* The toggle is one set now: the choke point applies the mode. Before Phase 2
 * this test would have to expect an explicit call as well, and a regression
 * that restored it would double-apply without changing any value. */
static void toggle_applies_once(void)
{
    fakeReset();
    portClassicToggle();

    CHECK(fakeGet("classic_mode") == 1);
    expect_classic_forced();
    CHECK(fakeSnapshotCalls() == 1);
    expect_snapshot_is_mine();

    portClassicToggle();
    CHECK(fakeGet("classic_mode") == 0);
    expect_mine();
}

static void other_keys_do_not_transition(void)
{
    fakeReset();
    set_text("hud_scale", "0");

    CHECK(fakeGet("hud_scale") == 0);
    CHECK(fakeSnapshotCalls() == 0);
    expect_mine();
}

/* A PART row is not the preset, even though its name starts with classic_.
 * Setting one while the mode is on forces or restores that single setting via
 * classicPartChanged; it must not run a whole transition, which would
 * re-snapshot the player's values and overwrite the ones the mode saved. */
static void part_rows_do_not_transition(void)
{
    fakeReset();
    set_text("classic_pillars", "1");

    CHECK(fakeGet("classic_pillars") == 1);
    CHECK(fakeGet("classic_mode") == 0);
    CHECK(fakeSnapshotCalls() == 0);
    expect_mine();
}

/* An [input] row must still go through the same choke point. A refactor that
 * special-cased the display bridge would break every controller setting in the
 * console and leave the display rows perfectly fine. */
static void input_rows_still_set(void)
{
    fakeReset();
    fakeAcceptInput("no_aim_crouch");

    CHECK(portOptSetIni(portOptFind("no_aim_crouch"), "1") == 1);
    CHECK(fakeGet("no_aim_crouch") == 1);
}

/* ---- the hold ---------------------------------------------------------- */

static void the_hold_suppresses_the_transition(void)
{
    fakeReset();
    portClassicSetHold(1);
    set_text("classic_mode", "1");

    CHECK(fakeGet("classic_mode") == 1);
    expect_mine();                       /* the sheet lands its own values */
    CHECK(fakeSnapshotCalls() == 0);

    portClassicSetHold(0);

    fakeReset();                         /* a leaked hold would disable the mode */
    set_text("classic_mode", "1");
    expect_classic_forced();
}

/* ---- the split: two keys, one direction (plan.md Phase 3) --------------- */

/* THE acceptance assertion of the whole phase. Asking for N64 edges used to
 * turn on the preset as a side effect, because the two were one key. If this
 * ever fails, the split has been undone and a player can no longer have the bit
 * without the 240p, the forced values and the pillars. */
static void the_bit_does_not_run_the_preset(void)
{
    fakeReset();
    set_text("n64_aa", "1");

    CHECK(fakeGet("n64_aa") == 1);         /* the bit is on... */
    CHECK(fakeGet("classic_mode") == 0);   /* ...and the preset is not */
    expect_mine();                         /* nothing was forced */
    CHECK(fakeSnapshotCalls() == 0);       /* and nothing was remembered */
    CHECK(fakeIniCalls() > 0);             /* still persisted, though */
}

/* The other direction: the preset moves, and the bit follows it, because its
 * classic_n64aa part is the one thing that couples them. Without this the split
 * would silently drop N64 edges for every Classic Mode player. */
static void the_preset_moves_the_bit(void)
{
    fakeReset();
    set_text("classic_mode", "1");
    CHECK(fakeGet("n64_aa") == 1);

    fakeClassicOn();
    set_text("classic_mode", "0");
    CHECK(fakeGet("n64_aa") == 0);
}

/* ...but only because the part says so. A player who turned classic_n64aa off
 * has said the preset must not own the bit; the bit they chose survives. */
static void the_part_off_leaves_the_bit_alone(void)
{
    fakeReset();
    fakeClassicAaPart(0);
    set_text("n64_aa", "1");

    set_text("classic_mode", "1");         /* the preset comes on */
    expect_classic_forced();
    CHECK(fakeGet("n64_aa") == 1);         /* untouched by the preset */

    set_text("classic_mode", "0");         /* and off again */
    expect_mine();
    CHECK(fakeGet("n64_aa") == 1);         /* still the player's choice */
}

/* The two keys are independent settings, so preset-on with the bit off must
 * survive a same-value set of the preset: nothing re-derives one from the other
 * behind the player's back. (The menus can express it - the VIDEO sheet's two
 * rows - and videoApplyAa reads the bit alone.) */
static void the_preset_stays_on_with_the_bit_off(void)
{
    fakeClassicOn();
    fakeSet("n64_aa", 0);                  /* N64 edges off, preset on */

    set_text("classic_mode", "1");         /* a same-value set: no transition */
    CHECK(fakeGet("classic_mode") == 1);
    CHECK(fakeGet("n64_aa") == 0);
    CHECK(fakeSnapshotCalls() == 0);
}

/* The mismatch report counts the seven. The bit is NOT one of them: it is not in
 * classic_saved, and preset-on with the bit off is a legitimate state, not
 * drift. A repair must therefore leave the player's bit alone. */
static void the_bit_is_not_a_mismatch(void)
{
    s32 idx[8];

    fakeClassicOn();
    fakeSet("n64_aa", 0);

    /* the bit alone is not drift... */
    CHECK(portClassicMismatchParts(idx, 8) == 0);
    CHECK(portClassicRepair() == 0);
    CHECK(fakeGet("n64_aa") == 0);

    /* ...but the report still asks the PRESET whether the mode is on: a drifted
     * value must be named even though the bit is off. Reading the bit here would
     * call a preset-on mode "off" and report real drift as consistent - the
     * whole reason mode_on moved to classic_mode. */
    fakeSet("low_res", 720);
    CHECK(portClassicMismatchParts(idx, 8) == 1);
    CHECK(idx[0] == 0);
    CHECK(portClassicRepair() == 1);
    CHECK(fakeGet("n64_aa") == 0);         /* and the repair left the bit alone */
}

/* ---- the repair and report path (plan.md Phase 0) ---------------------- */

/* The half-state the audit found: mode on, one setting drifted. Reporting it
 * must name it, and repairing it must force it WITHOUT touching the snapshot -
 * re-snapshotting here would overwrite the player's originals with the forced
 * values, and a later "off" would then restore the mode's own settings. */
static void repair_forces_and_remembers_nothing(void)
{
    s32 idx[8];

    fakeClassicOn();
    fakeSet("low_res", 720);            /* drifted */

    CHECK(portClassicMismatchParts(idx, 8) == 1);
    CHECK(idx[0] == 0);                 /* PART index: low_res is part 0 */

    CHECK(portClassicRepair() == 1);    /* one value written */
    CHECK(fakeGet("low_res") == 240);
    expect_snapshot_is_mine();          /* the PLAYER's values, not 240/2/8/... */
    CHECK(fakeSnapshotCalls() == 0);    /* and no snapshot was taken at all */
    CHECK(fakeIniCalls() > 0);          /* and the repair is persisted */
}

/* A short buffer must not be overrun: the count still reports the truth. */
static void mismatch_respects_the_caller_buffer(void)
{
    s32 idx[2];

    fakeClassicOn();
    fakeSet("low_res", 720);
    fakeSet("msaa", 8);
    fakeSet("mipmaps", 0);

    CHECK(portClassicMismatchParts(idx, 2) == 3);   /* true count */
    CHECK(idx[0] == 0);            /* part 0 = low_res; only 2 slots filled */
    CHECK(idx[1] == 4);            /* part 4 = msaa */

    /* A NULL buffer is legal: count only. */
    CHECK(portClassicMismatchParts(NULL, 0) == 3);
}

/* Consistent and off are both "nothing wrong". */
static void mismatch_is_zero_when_consistent_or_off(void)
{
    s32 idx[8];

    fakeClassicOn();
    CHECK(portClassicMismatchParts(idx, 8) == 0);   /* on, all classic */

    fakeReset();
    fakeSet("low_res", 720);
    CHECK(portClassicMismatchParts(idx, 8) == 0);   /* off: nothing is forced */
}

int main(void)
{
    the_mode_keys_are_not_row_zero();

    text_on_forces_and_snapshots();
    all_true_spellings_transition();
    refused_set_does_not_transition();
    failed_setter_does_not_transition();

    one_transition_is_one_save();
    one_transition_is_one_snapshot();
    repeated_on_is_not_a_transition();

    flag_is_written_before_the_values();
    nothing_is_saved_before_the_values_land();

    toggle_applies_once();
    other_keys_do_not_transition();
    part_rows_do_not_transition();
    input_rows_still_set();

    the_hold_suppresses_the_transition();

    the_bit_does_not_run_the_preset();
    the_preset_moves_the_bit();
    the_part_off_leaves_the_bit_alone();
    the_preset_stays_on_with_the_bit_off();
    the_bit_is_not_a_mismatch();

    repair_forces_and_remembers_nothing();
    mismatch_respects_the_caller_buffer();
    mismatch_is_zero_when_consistent_or_off();

    printf("port_options_classic: all checks passed\n");
    return 0;
}
