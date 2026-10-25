#!/bin/bash
# Mutation-check the Classic Mode tests: break the shipped code on purpose and
# confirm a test fails.
#
# A passing test proves nothing. This script asks the only question that
# matters about a test - CAN IT FAIL? - by mutating the shipped source and
# checking that something aborts. A mutant that no test kills is a test gap; a
# mutation that does not change the source at all is a broken check, and is
# reported as such rather than counted as a pass.
#
# Copies live in a scratch dir; the repo is never modified.
#
#   scripts/port/test_the_tests.sh
#
# Exit status: 0 = every expected verdict held, 1 = a gap or a no-op mutation.

set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
CC_BASE="-std=c99 -I$ROOT/include -I$ROOT/port/include"

CM="$ROOT/port/src/classic_mode.c"
PO="$ROOT/port/src/port_options.c"
TS="$ROOT/port/tests/classic_mode.c"
TP="$ROOT/port/tests/port_options_classic.c"
TF="$ROOT/port/tests/port_options_fake.c"

status=0

# mutate <src-file> <perl-expr> : apply into $W/<base> and fail loudly if the
# source is byte-identical afterwards (a silent no-op would fake a pass).
mutate() {
    local src="$1" expr="$2"
    cp "$src" "$W/mut.c"
    perl -0pi -e "$expr" "$W/mut.c"
    if cmp -s "$src" "$W/mut.c"; then
        printf '  NO-OP    %s\n' "$3"
        status=1
        return 1
    fi
    return 0
}

# check <label> <want: kill|survive>
check() {
    local label="$1" want="$2"
    local killed=0

    if [ "$(basename "$MUT_SRC")" = "classic_mode.c" ]; then
        cc $CC_BASE "$TS" "$W/mut.c" -o "$W/t" 2>"$W/cc.log" || {
            printf '  NOBUILD  %s\n' "$label"; status=1; return; }
        "$W/t" >/dev/null 2>&1 || killed=1
    else
        cc $CC_BASE "$TP" "$TF" "$W/mut.c" "$CM" -o "$W/t" 2>"$W/cc.log" || {
            printf '  NOBUILD  %s\n' "$label"; status=1; return; }
        "$W/t" >/dev/null 2>&1 || killed=1
    fi

    local got="survive"
    [ "$killed" -eq 1 ] && got="kill"

    if [ "$got" = "$want" ]; then
        if [ "$got" = "kill" ]; then
            printf '  ok       %-50s killed\n' "$label"
        else
            printf '  ok       %-50s survives (expected)\n' "$label"
        fi
    else
        if [ "$want" = "kill" ]; then
            printf '  GAP      %-50s SURVIVED - no test kills this\n' "$label"
        else
            printf '  BROKEN   %-50s the baseline itself fails\n' "$label"
        fi
        status=1
    fi
}

echo "mutation audit of the Classic Mode tests"
echo

echo "the rule (port/src/classic_mode.c)"
MUT_SRC="$CM"
mutate "$CM" 's/if \(in->op == CLASSIC_OP_TOGGLE_ON && in->mode_on\) \{/if (in->op == CLASSIC_OP_TOGGLE_ON) {/' "rule: drop mode_on from the snapshot test" && \
    check "rule: snapshot ignores mode_on" kill
mutate "$CM" 's/    memset\(out, 0, sizeof\(\*out\)\);\n//' "rule: drop memset" && \
    check "rule: plan reuse keeps stale fields" kill
mutate "$CM" 's/if \(in->parts_on == NULL \|\| in->classic == NULL \|\| in->current == NULL\) \{/if (in->parts_on == NULL) {/' "rule: trim NULL guard" && \
    check "rule: NULL classic/current not guarded" kill
mutate "$CM" 's/return \(n > CLASSIC_MODE_MAX\) \? CLASSIC_MODE_MAX : n;/return n;/' "rule: drop the buffer clamp" && \
    check "rule: n past the buffer bound" kill
mutate "$CM" 's/if \(in->parts_on\[i\] && in->current\[i\] != in->classic\[i\]\) \{/if (in->parts_on[i]) {/' "rule: force unchanged parts too" && \
    check "rule: forces values already equal" kill
mutate "$CM" 's/            if \(in->parts_on\[i\] && in->current\[i\] != in->saved\[i\]\) \{/            if (in->current[i] != in->saved[i]) {/' "rule: restore disabled parts too" && \
    check "rule: restores a disabled part" kill
mutate "$CM" 's/        for \(i = 0; i < saved_n; i\+\+\) \{/        for (i = 0; i < n; i++) {/' "rule: ignore the snapshot length" && \
    check "rule: 4-field legacy snapshot" kill
mutate "$CM" 's/            if \(idx_out != NULL\) \{/            if (1) {/' "rule: mismatch without the NULL check" && \
    check "rule: mismatch wrote through NULL" kill

echo
echo "the video env overrides (port/src/video_env.c)"
VE="$ROOT/port/src/video_env.c"
TV="$ROOT/port/tests/video_env.c"
MUT_SRC="$VE"
run_ve() {
    local label="$1" want="$2"
    cc $CC_BASE "$TV" "$W/mut.c" -o "$W/t" 2>"$W/cc.log" || { printf '  NOBUILD  %s\n' "$label"; status=1; return; }
    if "$W/t" >/dev/null 2>&1; then
        if [ "$want" = "survive" ]; then printf '  ok       %-50s survives (expected)\n' "$label";
        else printf '  GAP      %-50s SURVIVED - no test kills this\n' "$label"; status=1; fi
    else
        if [ "$want" = "kill" ]; then printf '  ok       %-50s killed\n' "$label";
        else printf '  BROKEN   %-50s the baseline itself fails\n' "$label"; status=1; fi
    fi
}
ve_mutate() {
    cp "$VE" "$W/mut.c"
    perl -0pi -e "$1" "$W/mut.c"
    if cmp -s "$VE" "$W/mut.c"; then printf '  NO-OP    %s\n' "$2"; status=1; return 1; fi
}
ve_mutate 's/return \(classic_on != 0\) && \(tick_env_set != 0\);/return (tick_env_set != 0);/' "tick: ignore the mode" && \
    run_ve "tick clash announced with the mode off" kill
ve_mutate 's/return \(classic_on != 0\) && \(tick_env_set != 0\);/return (classic_on != 0);/' "tick: ignore the env" && \
    run_ve "tick clash announced with no GE_TICKHZ" kill
ve_mutate 's/    if \(hw_pace && r.value != 0\) \{/    if (0) {/' "roomcache: never refuse" && \
    run_ve "room_cache re-enabled under hw pacing" kill
ve_mutate 's/        r.refused = 1;\n        r.value = 0;/        r.refused = 1;\n        r.value = r.value;/' "roomcache: refuse but keep the value" && \
    run_ve "room_cache refused but still applied" kill
ve_mutate 's/    if \(r.value < 0 \|\| r.value > 2\) \{/    if (0) {/' "roomcache: no clamp" && \
    run_ve "room_cache takes an invalid value" kill
ve_mutate 's/    if \(!classic_on \|\| !mipmap_part_on\) \{/    if (!classic_on) {/' "mipmap: ignore the part" && \
    run_ve "mipmap clash with the part switched off" kill
ve_mutate 's/    return \(mipmap_env_on == 0\) \? 1 : 0;/    return (mipmap_env_on != 1) ? 1 : 0;/' "mipmap: unset counts as a clash" && \
    run_ve "mipmap clash announced when unset" kill
ve_mutate 's/    r.touched = 1;/    r.touched = 1;\n    if (1) { r.value = 2; return r; }/' "roomcache: ignore the value" && \
    run_ve "room_cache ignores what GE_ROOMCACHE said" kill
cp "$VE" "$W/mut.c"
run_ve "video_env" survive

echo
echo "the n64_aa -> classic_mode split (port/src/classic_split.c)"
CS="$ROOT/port/src/classic_split.c"
TCS="$ROOT/port/tests/classic_split.c"
MUT_SRC="$CS"
run_cs() {
    local label="$1" want="$2"
    cc $CC_BASE "$TCS" "$W/mut.c" -o "$W/t" 2>"$W/cc.log" || { printf '  NOBUILD  %s\n' "$label"; status=1; return; }
    if "$W/t" >/dev/null 2>&1; then
        if [ "$want" = "survive" ]; then printf '  ok       %-50s survives (expected)\n' "$label";
        else printf '  GAP      %-50s SURVIVED - no test kills this\n' "$label"; status=1; fi
    else
        if [ "$want" = "kill" ]; then printf '  ok       %-50s killed\n' "$label";
        else printf '  BROKEN   %-50s the baseline itself fails\n' "$label"; status=1; fi
    fi
}
cs_mutate() {
    cp "$CS" "$W/mut.c"
    perl -0pi -e "$1" "$W/mut.c"
    if cmp -s "$CS" "$W/mut.c"; then printf '  NO-OP    %s\n' "$2"; status=1; return 1; fi
}
# The marker is the whole point of the guard: without it the migration runs on
# every boot and a deliberate classic_mode=0 is resurrected.
cs_mutate 's/!in->had_classic_mode && !in->had_split_marker/!in->had_classic_mode/' "split: ignore the marker" && \
    run_cs "migration re-runs on a migrated file" kill
# classic_mode on its own must also stop it: a file that names the preset has
# already been split, whatever n64_aa says.
cs_mutate 's/!in->had_classic_mode && !in->had_split_marker/!in->had_split_marker/' "split: ignore classic_mode" && \
    run_cs "migration overwrites an explicit classic_mode" kill
# The mapping itself. A split that leaves the preset off upgrades a classic
# player into a half-state - the audit's defect, reintroduced by its own fix.
cs_mutate 's/out->classic_mode = out->migrate \? \(in->n64_aa != 0\) : \(in->classic_mode != 0\);/out->classic_mode = 0;/' "split: never migrate the preset" && \
    run_cs "old n64_aa=1 does not turn the preset on" kill
cs_mutate 's/out->classic_mode = out->migrate \? \(in->n64_aa != 0\) : \(in->classic_mode != 0\);/out->classic_mode = out->migrate ? 1 : (in->classic_mode != 0);/' "split: always migrate it on" && \
    run_cs "old n64_aa=0 still turns the preset on" kill
# A file with classic_mode already set keeps ITS value, not the bit's.
cs_mutate 's/out->classic_mode = out->migrate \? \(in->n64_aa != 0\) : \(in->classic_mode != 0\);/out->classic_mode = (in->n64_aa != 0);/' "split: derive from the bit" && \
    run_cs "classic_mode=1 n64_aa=0 loses the preset" kill
cp "$CS" "$W/mut.c"
run_cs "classic_split" survive

echo
echo "the boot flags (port/src/boot_flags.c)"
BF="$ROOT/port/src/boot_flags.c"
TBF="$ROOT/port/tests/boot_flags.c"
MUT_SRC="$BF"
run_bf() {
    local label="$1" want="$2"
    cc $CC_BASE "$TBF" "$W/mut.c" -o "$W/t" 2>"$W/cc.log" || { printf '  NOBUILD  %s\n' "$label"; status=1; return; }
    if "$W/t" >/dev/null 2>&1; then
        if [ "$want" = "survive" ]; then printf '  ok       %-50s survives (expected)\n' "$label";
        else printf '  GAP      %-50s SURVIVED - no test kills this\n' "$label"; status=1; fi
    else
        if [ "$want" = "kill" ]; then printf '  ok       %-50s killed\n' "$label";
        else printf '  BROKEN   %-50s the baseline itself fails\n' "$label"; status=1; fi
    fi
}
bf_mutate() {
    cp "$BF" "$W/mut.c"
    perl -0pi -e "$1" "$W/mut.c"
    if cmp -s "$BF" "$W/mut.c"; then printf '  NO-OP    %s\n' "$2"; status=1; return 1; fi
}
# The alias is the new name for a shipped flag, so losing it is a silent
# regression for every command that uses -classic.
bf_mutate 's/portBootFlagIs\(argv\[i\], "classic"\) \|\| //' "classic: drop the new spelling" && \
    run_bf "-classic is not a flag" kill
# ...and the log must name the spelling typed, or a -classic launch reports
# itself as the old flag - the same defect as one that does nothing.
bf_mutate 's/            out->classic_spelling = argv\[i\];\n//' "classic: never record the spelling" && \
    run_bf "the log cannot name the spelling typed" kill
# A bare -skip means ALL the boot screens; a parser that defaulted to 0 there
# silently skips nothing, which reads exactly like a flag that was ignored.
bf_mutate 's/            if \(n >= 0\) \{\n                skip_n = n;/            if (1) {\n                skip_n = (n >= 0) ? n : 0;/' "skip: bare means none" && \
    run_bf "a bare -skip skips nothing" kill
# ...and a count must be a count: a word after -skip keeps the flag's bare
# meaning instead of becoming the number of screens to skip.
bf_mutate 's/        if \(a\[i\] < .0. \|\| a\[i\] > .9. \|\| i >= 3\) \{/        if (i >= 3) {/' "skip: any word is a count" && \
    run_bf "-skip banana becomes a count" kill
# 0 means off. A clamp that turned 0 into "all" would skip the boot sequence of
# every -skip 0 launch.
bf_mutate 's/    out->skip_screens = skip_all \? -1 : skip_n;/    out->skip_screens = (skip_all || skip_n == 0) ? -1 : skip_n;/' "skip: 0 means all" && \
    run_bf "-skip 0 skips everything" kill
# -stage with no value must not swallow a following flag: it would try to load a
# mission called "-audio_pump" and lose the flag.
bf_mutate "s/if \\(i \\+ 1 < argc && argv\\[i \\+ 1\\]\\[0\\] != '-'\\) \\{/if (i + 1 < argc) {/" "stage: swallow the next flag" && \
    run_bf "-stage eats a neighbouring flag" kill
cp "$BF" "$W/mut.c"
run_bf "boot_flags" survive

echo
echo "the seam (port/src/port_options.c)"
MUT_SRC="$PO"
# The choke point's key. After the Phase 3 split the preset IS classic_mode, so
# matching classic_* is now a SUPERSET of the real key rather than a different
# one, and proves nothing about the WRONG key. The wrong key that matters is the
# bit: a choke point firing on n64_aa is exactly the conflation the split exists
# to end - it would force the seven for anyone asking for N64 edges.
mutate "$PO" 's/strcmp\(key, "classic_mode"\) == 0/strcmp(key, "n64_aa") == 0/' "choke: wrong key test" && \
    check "choke point fires on the AA bit" kill
# ...and the wide version, a different bug: too MANY keys, not the wrong one. It
# fires on classic_n64aa and the other part rows too - the mutant that used to
# name this line, kept because the superset is still a bug.
mutate "$PO" 's/strcmp\(key, "classic_mode"\) == 0/strncmp(key, "classic_", 8) == 0/' "choke: key too wide" && \
    check "choke point fires on every classic_* row" kill
# The bit must not carry the preset. This is the mutant that undoes the whole
# split while leaving both keys in place: n64_aa is still its own key and still
# holds the bit, but setting it forces the seven as well.
mutate "$PO" 's/strcmp\(key, "classic_mode"\) == 0/(strcmp(key, "classic_mode") == 0 || strcmp(key, "n64_aa") == 0)/' "choke: the bit carries the preset" && \
    check "setting n64_aa forces the seven" kill
# ...and the link itself, in both directions of failure. Without it the preset
# turns on with the bit off, and every Classic Mode player silently loses the
# N64 edges on upgrade.
mutate "$PO" 's/    classicSyncAaBit\(v != 0\);/    (void) 0;/' "link: never move the bit" && \
    check "the preset leaves the AA bit behind" kill
# The read it makes its decision from, at the seam rather than in video.c: a
# mode_on taken from the bit reports a preset-on mode as off.
mutate "$PO" 's/    in->mode_on = \(mode >= 0\) \? \(portOptGet\(mode\) != 0\) : 0;/    in->mode_on = (portOptFind("n64_aa") >= 0) ? (portOptGet(portOptFind("n64_aa")) != 0) : 0;/' "mode_on: read the bit" && \
    check "is-the-mode-on reads the AA bit" kill
mutate "$PO" 's/\(v != 0\) != \(was != 0\)\)/1)/' "choke: always transition" && \
    check "choke point transitions on a same-value set" kill
mutate "$PO" 's/const s32 transitions = \(sClassicHold == 0\) && /const s32 transitions = /' "choke: ignore the hold" && \
    check "choke point ignores the hold" kill
mutate "$PO" 's/    in.op = CLASSIC_OP_FIX;/    in.op = CLASSIC_OP_TOGGLE_ON;/' "repair: re-snapshot" && \
    check "repair re-snapshots the forced values" kill
mutate "$PO" 's/    if \(!in.mode_on\)\n    \{\n        return 0;\n    \}/    return 0;/' "mismatch: always 0" && \
    check "mismatch always reports consistent" kill
mutate "$PO" 's/        const s32 room = \(max < n\) \? max : n;/        const s32 room = n;/' "mismatch: ignore max" && \
    check "mismatch overruns a short caller buffer" kill
mutate "$PO" 's/    in.mode_on = on \? 1 : 0;/    in.mode_on = on ? 0 : 1;/' "apply: invert mode_on" && \
    check "apply runs the wrong direction" kill
mutate "$PO" 's/        portVideoClassicSavedSet\(p->saved\);/        (void) 0;/' "apply: never snapshot" && \
    check "apply never stores the snapshot" kill
mutate "$PO" 's/    if \(!optSet\(o->key, v\)\)\n    \{\n        return 0;\n    \}\n    portOptSave\(\);/    portOptSave();\n    if (!optSet(o->key, v))\n    {\n        return 0;\n    }/' "setIni: save before set" && \
    check "portOptSetIni writes before applying" kill
mutate "$PO" 's/    classicPlanApply\(&plan\);\n    if \(plan.snapshot \|\| plan.write_n > 0\)\n    \{\n        portOptSave\(\);/    classicPlanApply(\&plan);\n    if (0)\n    {\n        portOptSave();/' "repair: never save" && \
    check "repair does not persist" kill

echo
echo "baseline"
MUT_SRC="$CM"
if cmp -s "$CM" "$CM"; then
    cc $CC_BASE "$TS" "$CM" -o "$W/b" 2>/dev/null && "$W/b" >/dev/null 2>&1 && \
        printf '  ok       %-50s passes unmutated\n' "classic_mode" || {
        printf '  BROKEN   the unmutated rule test fails\n'; status=1; }
fi
cc $CC_BASE "$TP" "$TF" "$PO" "$CM" -o "$W/b2" 2>/dev/null && "$W/b2" >/dev/null 2>&1 && \
    printf '  ok       %-50s passes unmutated\n' "port_options_classic" || {
    printf '  BROKEN   the unmutated seam test fails\n'; status=1; }
cc $CC_BASE "$TCS" "$CS" -o "$W/b3" 2>/dev/null && "$W/b3" >/dev/null 2>&1 && \
    printf '  ok       %-50s passes unmutated\n' "classic_split" || {
    printf '  BROKEN   the unmutated split test fails\n'; status=1; }
cc $CC_BASE "$TBF" "$BF" -o "$W/b4" 2>/dev/null && "$W/b4" >/dev/null 2>&1 && \
    printf '  ok       %-50s passes unmutated\n' "boot_flags" || {
    printf '  BROKEN   the unmutated boot flag test fails\n'; status=1; }

echo
if [ "$status" -eq 0 ]; then
    echo "RESULT: every mutant was killed, baseline passes"
else
    echo "RESULT: gaps found (see GAP / NO-OP / BROKEN above)"
fi
exit "$status"
