#!/bin/bash
# Run the port's regression suite and print one line per test.
#
# The suite itself is ctest, which is fine for a terminal and noisy for a
# recording: its default output interleaves "Start 1: ..." lines with the
# results, and a viewer cannot tell what passed. This reformats it - one aligned
# line per test, a summary, an exit code - and by default then launches the game
# with the diagnostics on, which is the second half of the demo.
#
#   scripts/port/port_test.sh              # the whole suite, then the game
#   scripts/port/port_test.sh --audio      # only the audio/tick/layout four
#   scripts/port/port_test.sh --no-game    # tests only (CI, or a quick check)
#   scripts/port/port_test.sh --game-only  # just launch, no tests
#   scripts/port/port_test.sh -v           # windowed on display 0 for the game
#
# `make port_test` runs the whole suite, `make audio_test` the audio subset, and
# `make demo` runs the whole thing windowed for a recording. Do not add a second
# implementation: the targets and the demo must show the same thing.
#
# WHAT A VIEWER IS LOOKING AT
#
#   native_*      ROM-free engine tests: layout, collision, aiming, movement,
#                 prop collision, player. They drive shipped code without a ROM,
#                 a window or a GPU.
#   audio_pace    the audio pump's pacing policy: refill, wall-clock credit, the
#                 drain watchdog, the underrun ratchet, and the closed-loop
#                 simulation that is now modelled in the pump's real order.
#   boot_flags    the -n64_aa / -audio_pump / -audio_log argument parse.
#   tick_stats    the frame and tick arithmetic behind fps / tk / f/s / f/t.
#
# WHAT IT CANNOT TELL YOU
#
#   A green suite means the rules those pure functions implement still hold. It
#   says nothing about whether the game renders, whether a menu saved what it
#   showed, or whether a held frame felt right - those need a run, and that is
#   the second half of this script. This branch's recorded failure mode is a fix
#   verified on one shape of input and reported as the mechanism working; a
#   passing suite has never been sufficient evidence here.
#
# WHAT THE GAME HALF CHECKS, AND WHAT IT CANNOT
#
#   It prints the renderer the run actually got, because a sandboxed or non-GUI
#   shell gets `Apple Software Renderer`: that draws into the back buffer while
#   the window stays black (CLI.md 7.2), so "the pump block is visible" needs the
#   GPU line, not the capture. It also restores midas.ini after a -v run and
#   verifies the md5 of both midas.ini and Data/Saves/eeprom.bin, so the demo
#   cannot leave the user's real settings behind (the audio_run.sh discipline).
#   It cannot see the screen: if the window is black and the renderer line says
#   Software, the run had no GPU and that is not a rendering defect.

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build_macos"
GAME="$BUILD/ProjectMidas"
INI="$BUILD/midas.ini"

run_tests=1
run_game=1
all_tests=1
windowed=0
game_args="-audio_pump -audio_log"

while [ $# -gt 0 ]; do
    case "$1" in
        --no-game)   run_game=0 ;;
        --game-only) run_tests=0 ;;
        --audio)     all_tests=0 ;;
        --all)       all_tests=1 ;;
        -v)          windowed=1 ;;
        -h|--help)   sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
    shift
done

[ -d "$BUILD" ] || { echo "no $BUILD - configure and build first (see BUILD.md)" >&2; exit 2; }

fail=0

if [ "$run_tests" -eq 1 ]; then
    echo "Project Midas - port test suite"
    echo

    # Build, but never reconfigure: build_macos is the developer's own build
    # directory and a stray -D flag here would silently change it.
    #
    # Errors are fatal and printed. Warnings are NOT a signal from the compiler:
    # this build passes `-w`, which suppresses every C warning outright (checked
    # in build.ninja's FLAGS). A `#warning` directive produces nothing. So the
    # only warnings this can ever see are LINKER ones, and there is exactly one
    # known: the `__DATA,__common` alignment note. It is allow-listed by name so
    # that a new LINKER warning stands out, and the summary says so rather than
    # implying the compile is warning-free.
    if ! cmake --build "$BUILD" -j 8 > "$BUILD/.port_test_build.log" 2>&1; then
        echo "BUILD FAILED - errors:" >&2
        grep -E 'error:|Error [0-9]' "$BUILD/.port_test_build.log" | head -20 >&2 \
            || tail -20 "$BUILD/.port_test_build.log" >&2
        echo >&2
        echo "  full log: $BUILD/.port_test_build.log" >&2
        exit 1
    fi

    build_lines=$(wc -l < "$BUILD/.port_test_build.log" | tr -d ' ')
    known_warn='reducing alignment of section __DATA,__common'
    new_warnings=$(grep -E 'warning:' "$BUILD/.port_test_build.log" 2>/dev/null \
                   | grep -vcF "$known_warn" || true)
    known_seen=$(grep -cF "$known_warn" "$BUILD/.port_test_build.log" 2>/dev/null || true)

    if [ "${new_warnings:-0}" -gt 0 ]; then
        echo "  build: succeeded, ${new_warnings} NEW linker warning(s)"
        grep -E 'warning:' "$BUILD/.port_test_build.log" \
            | grep -vF "$known_warn" | head -10 | sed 's/^/    /'
    elif [ "${build_lines:-0}" -le 3 ]; then
        echo "  build: up to date, nothing recompiled"
    elif [ "${known_seen:-0}" -gt 0 ]; then
        echo "  build: succeeded (1 known linker warning: __DATA,__common alignment)"
    else
        echo "  build: succeeded"
    fi
    echo "        note: the build passes -w, so C warnings are suppressed entirely"
    echo
    if [ "$all_tests" -eq 1 ]; then
        filter=""
    else
        # The four the audio/tick work added or touches. Named, not a regex
        # over the whole list, so adding a test never silently widens the demo.
        filter="audio_pace|boot_flags|tick_stats|native_layout"
    fi

    if [ -n "$filter" ]; then
        raw=$(cd "$BUILD" && ctest -R "^($filter)$" --output-on-failure 2>&1)
    else
        raw=$(cd "$BUILD" && ctest --output-on-failure 2>&1)
    fi

    # ctest prints "1/10 Test  #8: audio_pace .......   Passed    0.17 sec" -
    # note the TWO spaces after "Test" for single-digit numbers, which is what a
    # first cut of this parser missed. Reformat those lines; everything else is
    # passed through only when something failed, so a red run still shows why.
    printf '%s\n' "$raw" | awk '
        /Test +#[0-9]+:/ {
            line = $0
            sub(/^.*Test +#[0-9]+: +/, "", line)      # drop "1/10 Test  #8: "
            gsub(/\*\*\*/, " ", line)                 # ***Failed -> Failed
            gsub(/\.{2,}/, " ", line)                 # the dot leader (2+ dots)
            gsub(/ +/, " ", line)                     # collapse padding
            status = "?"
            if (line ~ /Passed/)    { status = "ok"   }
            if (line ~ /Failed/)    { status = "FAIL" }
            if (line ~ /Timeout/)   { status = "FAIL" }
            if (line ~ /Not Run/)   { status = "FAIL" }
            # An abort (a failed CHECK) is "***Exception: Other" or
            # "Subprocess aborted***Exception:", which carries no "Failed".
            # Without this it printed "?" - the one status a viewer cannot read.
            if (line ~ /Exception/) { status = "FAIL" }

            name = line
            sub(/ .*/, "", name)
            secs = ""
            if (match(line, /[0-9]+\.[0-9]+ sec/)) { secs = substr(line, RSTART, RLENGTH) }
            sub(/ sec$/, "s", secs)
            printf "  %-22s %-4s %s\n", name, status, secs
            passed += (status == "ok")
            failed += (status != "ok")
            next
        }
        END {
            printf "\n  %d passed, %d failed\n", passed, failed
            exit (failed > 0)
        }
    ' || fail=1

    # ctest's failure text carries NO prefix (the "<n>: " form only appears with
    # -j), so it is found by excluding the scaffolding instead. Without this the
    # demo shows FAIL and no reason - a claim in the comments above that the code
    # would not have honoured.
    if [ "$fail" -ne 0 ]; then
        printf '%s\n' "$raw" | grep -vE \
            '^[[:space:]]*Start +[0-9]+:|Test +#[0-9]+:|^Test project|^[[:space:]]*[0-9]+% tests passed|^Total Test time|^The following tests FAILED:|^[[:space:]]*[0-9]+ - |^Errors while running|^$' \
            | head -40 | sed 's/^/    /'
        echo
    fi

    echo
    if [ "$fail" -ne 0 ]; then
        echo "RESULT: FAILED"
        exit 1
    fi
    echo "RESULT: PASSED"
    echo
fi

if [ "$run_game" -eq 0 ]; then
    exit $fail
fi

[ -x "$GAME" ] || { echo "no game at $GAME" >&2; exit 2; }

[ -f "$INI" ] || { echo "no midas.ini at $INI - run the game once" >&2; exit 2; }

# A second window steals input focus from a live session; refuse like
# audio_run.sh and nat_test.sh.
if pgrep -x "$(basename "$GAME")" >/dev/null 2>&1; then
    echo "another ProjectMidas is already running - close it first" >&2
    exit 2
fi

md5_of() { md5 -q "$1" 2>/dev/null || md5sum "$1" | cut -d' ' -f1; }

SAVE="$BUILD/Data/Saves/eeprom.bin"

# The window. The user's midas.ini is borderless fullscreen on window_display=1
# at negative coordinates, so a run with no override opens on the second display
# and looks like nothing happened. -v sets a windowed mode on display 0 for the
# run and restores the file afterwards, the same trick boot_flag_test.sh uses.
#
# The backup is written to a FIXED path, not $TMPDIR/....$$, and is only removed
# once the file is back. A kill the trap cannot catch (SIGKILL, a closed
# terminal, an OBS crash) therefore leaves recoverable evidence instead of a
# silently windowed midas.ini: the next run repairs it before doing anything.
INI_BACKUP="$BUILD/.port_test_ini.bak"
restore_ini() {
    [ -f "$INI_BACKUP" ] && cp "$INI_BACKUP" "$INI"
    rm -f "$INI_BACKUP"
}
if [ -f "$INI_BACKUP" ]; then
    cp "$INI_BACKUP" "$INI"
    rm -f "$INI_BACKUP"
    echo "note: midas.ini restored from an interrupted -v run"
    echo
fi

INI_MD5=$(md5_of "$INI")
SAVE_MD5=$([ -f "$SAVE" ] && md5_of "$SAVE" || echo absent)
INI_CMP="${TMPDIR:-/tmp}/midas.ini.port_test.$$"
cp "$INI" "$INI_CMP"

if [ "$windowed" -eq 1 ]; then
    cp "$INI_CMP" "$INI_BACKUP"
    trap restore_ini EXIT INT TERM HUP
    sed -e 's/^fullscreen=.*/fullscreen=windowed/' \
        -e 's/^window_x=.*/window_x=80/' \
        -e 's/^window_y=.*/window_y=80/' \
        -e 's/^window_width=.*/window_width=960/' \
        -e 's/^window_height=.*/window_height=600/' \
        -e 's/^window_display=.*/window_display=0/' \
        "$INI_BACKUP" > "$INI"
fi

echo "launching: ProjectMidas $game_args"
echo "  the pump block is the six rows of small white text at the top left."
echo "  ctrl-C or quit the game to return to the terminal."
echo

# Tee, so the run's own output still scrolls past (it is half the evidence) and
# the renderer can be read back afterwards. A black window with working captures
# is the software-renderer failure in CLI.md 7.2; the renderer line is how you
# tell the two apart.
GAME_LOG="$BUILD/.port_test_game.log"
# shellcheck disable=SC2086  # game_args is a deliberate word-split argument list
( cd "$BUILD" && ./ProjectMidas $game_args ) 2>&1 | tee "$GAME_LOG"

renderer=$(grep -m1 'GL: renderer' "$GAME_LOG" 2>/dev/null | sed 's/.*GL: renderer: *//')
echo
echo "  renderer: ${renderer:-unknown}"
case "$renderer" in
    *Software*) echo "            <- software renderer: the window may stay black (CLI.md 7.2)" ;;
esac

# The acceptance claim is "nothing left changed", so check it rather than
# trusting the trap: a run that died hard restores nothing.
status=0
if [ "$windowed" -eq 1 ]; then
    restore_ini
    trap - EXIT INT TERM HUP
fi
now=$(md5_of "$INI")
if [ "$now" != "$INI_MD5" ]; then
    if [ "$windowed" -eq 1 ]; then
        echo "  !! midas.ini did NOT restore (now $now) - put the window keys back by hand" >&2
        status=1
    else
        # No -v, so nothing was restored and nothing should be: the game's own
        # exit-time writers (window geometry, tickhz normalisation) are allowed
        # to rewrite the file. Show what they changed rather than assume.
        echo "  note: the run rewrote midas.ini (see the keys below)"
        diff "$INI_CMP" "$INI" | sed -n '1,20p' | sed 's/^/    /'
    fi
fi
rm -f "$INI_CMP"
if [ -f "$SAVE" ] && [ "$(md5_of "$SAVE")" != "$SAVE_MD5" ]; then
    echo "  !! eeprom.bin CHANGED - the run touched the save file" >&2
    status=1
fi
exit $status
