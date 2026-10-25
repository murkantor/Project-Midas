#!/bin/bash
# Eight-second live tests for the boot flags: -classic (alias -n64_aa),
# -audio_pump, -audio_log.
#
# Each case starts the real renderer (no headless build, no ROM-free stub) for
# eight seconds in the FRONT END - no GE_STAGE, so the legal screen, the logos
# and the file select are what runs. That is the point: the pump block used to be
# drawable only from maybe_mp_interface, which the menus never reach, so
# `cl_audio_pump 1` showed nothing until a stage had loaded.
#
#   scripts/port/boot_flag_test.sh              # all seven cases, 8 s each
#   scripts/port/boot_flag_test.sh -s eyeball_windowed     # just the eyes-on one
#   scripts/port/boot_flag_test.sh -s baseline,audio_pump
#   scripts/port/boot_flag_test.sh -o /tmp/bft # where the captures go
#   scripts/port/boot_flag_test.sh -t 12       # longer, for the eyeball cases
#
# The two eyeball_* cases run FIRST and are the ones a person watches: one
# windowed, one fullscreen, each with -audio_pump, so the block has to reach the
# real screen and not merely the back buffer. They are the two cases that cannot
# be asserted from a log - see the header note on what a capture does not prove.
#
# Options:
#   -t SECONDS   per-case wall-clock limit (default 8)
#   -s CASES     comma-separated subset (default all seven)
#   -o DIR       output directory (default captures/<date>_<time>_boot_flags/)
#   -v           run the ordinary cases windowed on display 0 as well
#   -O           do not open the capture folder when the run ends
#
# WHERE THE CAPTURES GO, AND WHY IT MATTERS. They land in a dated folder under
# captures/ at the repo root with a README.txt recording the GL renderer that
# produced them, and the folder is opened in the desktop's file manager when the
# run ends (open / explorer.exe / xdg-open). The reason is a real failure in this
# very session: a sandboxed shell gets `Apple Software Renderer`, which renders
# correctly into the back buffer and therefore produces convincing captures while
# the window on screen stays black. The developer saw nothing, the captures
# looked perfect, and the difference took a round of confused cross-examination
# to find. A folder a human can open, holding the renderer line, is what closes
# that gap - the observer compares their screen with what the harness saw.
#
# WHY DISPLAY MODE IS PER-CASE. The user's midas.ini runs
# `fullscreen=fullscreen` on window_display=1 at negative window_x/y, so a run
# with no override opens on the SECOND display and looks like a black screen
# from the primary one. GE_WINDOW does NOT override the display mode - it only
# sets the window SIZE and suppresses ini writes - so watching a run needs the
# mode changed in the file. Each case sets the mode it needs from a pristine
# copy of the ini; the original goes back byte for byte, checked by md5.
#
# Everything else (ini backup/restore, the eeprom.bin md5 guard, refusing to
# start beside another copy, the wall-clock bound) is audio_run.sh's, unchanged:
# one copy of the dangerous parts, not two.
#
# What each case asserts, and what it canNOT:
#   audio_log   the file appears and gains lines            (log, checkable)
#   n64_aa      Classic Mode reports applied, and the config line that follows
#               shows low_res=240 / maxfps=-1               (log, checkable)
#   skip        the log says five boot screens were skipped (log, checkable);
#               which screen it LANDS on is the capture's job
#   all         the same as n64_aa but through -classic, the flag's new spelling
#   audio_pump  the block is DRAWN                          (PIXELS ONLY)
#   eyeball_*   a capture exists, and the log agrees        (the SCREEN is yours)
# The pixel cases cannot be grepped: the block is geometry in a display list.
# A capture proves the back buffer had the block; it does NOT prove the window
# was on a display the observer could see - the game still swaps either way
# (gfx_sdl_swap_buffers_begin calls the capture first and SDL_GL_SwapWindow
# after), but "drawn" and "seen" are different claims. The eyeball_* cases exist
# to close that gap with a person, one windowed and one fullscreen.

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build_macos"
GAME="$BUILD/ProjectMidas"
INI="$BUILD/midas.ini"
AUDIO_LOG="$BUILD/Data/Logs/audio_pump.log"
RUN="$ROOT/scripts/port/audio_run.sh"

seconds=8
cases="eyeball_windowed,eyeball_fullscreen,baseline,audio_pump,audio_log,n64_aa,skip,all"
outdir=""
windowed=0
open_dir=1   # show the folder when done; -O to suppress

while getopts "t:s:o:vO" opt; do
    case "$opt" in
        t) seconds=$OPTARG ;;
        s) cases=$OPTARG ;;
        o) outdir=$OPTARG ;;
        v) windowed=1 ;;
        O) open_dir=0 ;;
        *) sed -n '2,44p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done

[ -x "$GAME" ] || { echo "no game at $GAME - build first" >&2; exit 2; }
[ -f "$INI" ]  || { echo "no midas.ini at $INI" >&2; exit 2; }
[ -x "$RUN" ]  || { echo "missing $RUN" >&2; exit 2; }

# A dated, self-describing folder, so the captures outlive the terminal they were
# taken in. The point is that a HUMAN can open what the harness saw and compare
# it with what their own eyes saw - that comparison is the only thing that
# distinguishes "drawn" from "displayed", and it was the gap that took a whole
# round of confusion to find. Under captures/ at the repo root (gitignored by the
# directory listing below, never committed).
if [ -z "$outdir" ]; then
    stamp=$(date '+%Y-%m-%d_%H%M%S')
    outdir="$ROOT/captures/${stamp}_boot_flags"
fi
mkdir -p "$outdir" || { echo "cannot create $outdir" >&2; exit 2; }

# open_dir_default: hand the folder to the desktop's own file manager. Best
# effort - a headless or sandboxed shell has no `open`, and that must not fail
# the run.
show_dir() {
    case "$(uname -s)" in
        Darwin) command -v open >/dev/null 2>&1 && open "$1" >/dev/null 2>&1 ;;
        MINGW*|MSYS*|CYGWIN*) command -v explorer.exe >/dev/null 2>&1 && explorer.exe "$(cygpath -w "$1" 2>/dev/null || echo "$1")" >/dev/null 2>&1 ;;
        *)      command -v xdg-open >/dev/null 2>&1 && xdg-open "$1" >/dev/null 2>&1 ;;
    esac
    return 0
}

INI_BACKUP="${TMPDIR:-/tmp}/midas.ini.bootflag.$$"
cp "$INI" "$INI_BACKUP"
ORIG_MD5=$(md5 -q "$INI" 2>/dev/null || md5sum "$INI" | cut -d' ' -f1)

restore_ini() {
    cp "$INI_BACKUP" "$INI"
}
cleanup() {
    # Kill the game first: if it is still running it can rewrite midas.ini after
    # we restore it, and a per-case mode (windowed on display 0) would be left
    # behind. The EXIT trap alone did not cover a signal, which is how a
    # Ctrl-C during a case could leave a rewritten ini.
    [ -n "${game_pid:-}" ] && kill "$game_pid" 2>/dev/null
    pkill -f "$(basename "$GAME")" 2>/dev/null
    restore_ini
    rm -f "$INI_BACKUP"
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM HUP

# Per-case display mode, from the untouched ini each time.
#
# WHY THIS IS PER-CASE. The user's ini runs borderless fullscreen on
# window_display=1 at negative coordinates, so a run with no override opens on
# the SECOND display and looks like a black screen from the primary one. A
# global -v is not enough either, because the point of the eyeball cases is to
# check that the block reaches the SCREEN in each mode - windowed first, then
# fullscreen - and each needs its own mode.
#
#   as-is       exactly the user's file
#   windowed    960x600 on display 0, a corner a person can see
#   fullscreen  borderless, display 0 (position is ignored in this mode)
set_display() {
    case "$1" in
        as-is)
            cp "$INI_BACKUP" "$INI" ;;
        windowed)
            sed -e 's/^fullscreen=.*/fullscreen=windowed/' \
                -e 's/^window_x=.*/window_x=80/' \
                -e 's/^window_y=.*/window_y=80/' \
                -e 's/^window_width=.*/window_width=960/' \
                -e 's/^window_height=.*/window_height=600/' \
                -e 's/^window_display=.*/window_display=0/' \
                "$INI_BACKUP" > "$INI" ;;
        fullscreen)
            sed -e 's/^fullscreen=.*/fullscreen=fullscreen/' \
                -e 's/^window_display=.*/window_display=0/' \
                "$INI_BACKUP" > "$INI" ;;
        *)
            echo "internal: unknown display mode '$1'" >&2; return 1 ;;
    esac
}

echo "== $seconds s per case, front end (no GE_STAGE)"
echo "   captures: $outdir"
[ "$windowed" -eq 1 ] && echo "   -v: ordinary cases run windowed on display 0"
echo

rm -f "$AUDIO_LOG" 2>/dev/null   # so an old log cannot look like this run's

fail=0
summary=""

for name in ${cases//,/ }; do
    case_dir="$outdir/$name"
    rm -rf "$case_dir"; mkdir -p "$case_dir"

    args=""
    want_log=0
    display="as-is"
    eyeball=0
    case_seconds=$seconds
    case "$name" in
        baseline)   args="" ;;
        audio_pump) args="-audio_pump" ;;
        audio_log)  args="-audio_log";  want_log=1 ;;
        n64_aa)     args="-n64_aa" ;;   # the flag's OLD spelling, kept as an alias
        # -skip: the boot screens go by, and the run lands on Select File. The
        # assertion is the log line that says so - the SCREEN it lands on is a
        # capture a person reads, since "is that the file select" is not a
        # question the log can answer.
        skip)       args="-skip" ;;
        # ...and "all" uses the NEW one, so one live run exercises each spelling:
        # both are asserted on the config line below, which is the only proof
        # that -classic reaches the same code path -n64_aa does.
        all)        args="-classic -audio_pump -audio_log"; want_log=1 ;;
        # The two eyes-on cases: same flags as audio_pump, but the display mode
        # is forced and the run is long enough for a person to look. Their
        # assertion is that a capture exists and the log agrees; whether the
        # block is ON THE SCREEN is the observer's call, and the script says so
        # rather than claiming it.
        eyeball_windowed)
            args="-audio_pump"; display="windowed"; eyeball=1 ;;
        eyeball_fullscreen)
            args="-audio_pump"; display="fullscreen"; eyeball=1 ;;
        *) echo "unknown case '$name'" >&2; fail=1; continue ;;
    esac
    if [ "$display" = "as-is" ] && [ "$windowed" -eq 1 ]; then
        display="windowed"
    fi
    # An eyeball run gets four seconds longer than the others: a person has to
    # notice the window, find it, and read six rows of small text, and the
    # startup alone eats several seconds at 4 fps under Classic Mode.
    [ "$eyeball" -eq 1 ] && case_seconds=$((seconds + 4))

    set_display "$display" || { fail=1; continue; }

    echo "-- $name ${args:+($args)}  [display: $display]"
    if [ "$eyeball" -eq 1 ]; then
        echo "   >>> LOOK AT THE SCREEN NOW - this window stays up for ${case_seconds}s."
        echo "       Expect: the Nintendo/Rare logos or legal screen, with six rows of"
        echo "       small white text at the very top-left (fps/tk/f-s/f-t, PMP/RND,"
        echo "       QUE/CAP, SND, DRY/OVER/SKIP/FRC, STALL/CLK/DEV)."
    fi
    # GE_SHOT in the case's own directory: the harness writes shot_NNNNN.bmp to
    # the CWD and would otherwise litter the repo root.
    #
    # The interval is 5, not 90, and that is measured rather than cautious:
    # Classic Mode costs this machine's SOFTWARE renderer ~200-490 ms a frame
    # (4x MSAA at 240 lines plus the VI resolve), so an 8 s run reaches ~16-32
    # frames AFTER a startup that itself takes several seconds. GE_SHOT=90
    # captured nothing on the first pass and the script read that as "did it
    # render?" - a harness fault that looked exactly like a product bug.
    ( cd "$case_dir" && "$RUN" -t "$case_seconds" -s "" -a 0 -g "$args" \
        -e "GE_SHOT=5" > run.out 2>&1 )
    rc=$?

    log=$(sed -n 's/^log: *//p' "$case_dir/run.out" | tail -1)
    shots=$(ls "$case_dir"/shot_*.bmp 2>/dev/null | wc -l | tr -d ' ')

    # Both numbers come from the ONE config line videoInit logs, so they describe
    # the state the renderer was created in - not some later log line that
    # mentions maxfps.
    cfg=$(grep -m1 'ge: video: config:' "$log" 2>/dev/null)
    lowres=$(printf '%s' "$cfg" | sed -n 's/.*low_res=\([0-9][0-9]*\).*/low_res=\1/p')
    mfps=$(printf '%s' "$cfg" | sed -n 's/.*maxfps=\(-\{0,1\}[0-9][0-9]*\).*/\1/p')
    # `sed`, not `tr -d 'stats: '`: tr deletes CHARACTERS anywhere in the string,
    # so it ate the 's' of "fps" and every summary read "60fp".
    fps=$(grep -o 'stats: [0-9]*fps' "$log" 2>/dev/null | tail -1 | sed 's/^stats: //; s/fps$//')
    loglines=0
    [ -f "$AUDIO_LOG" ] && loglines=$(grep -c '^t=' "$AUDIO_LOG" 2>/dev/null || true)

    # Expectations per case. The overlay is NOT among them - see the header.
    status="ok"
    if [ "$name" = "n64_aa" ] || [ "$name" = "all" ]; then
        # The header has always said this case checks low_res=240 / maxfps=-1.
        # Until it did, a Classic Mode that logged "applied" and forced nothing
        # passed - a comment claiming more coverage than the assertion.
        #
        # These two come from the config line, so they are the state the renderer
        # was actually created in, and they are IDEMPOTENT: a second run starts
        # already in Classic Mode (the flag writes classic_mode=1, and the
        # preset's classic_n64aa part writes n64_aa=1 beside it), the flag
        # correctly does nothing, and the config line still reads 240 / -1. The
        # first cut asserted the log SENTENCE instead and reported a false
        # failure on every run after the first - non-idempotent against the
        # product's own write.
        [ "$lowres" = "low_res=240" ] || { status="FAIL (${lowres:-no low_res} in the config line)"; }
        [ "$mfps" = "-1" ] || { status="FAIL (maxfps=${mfps:-none}, want -1)"; }
    fi
    if [ "$want_log" -eq 1 ]; then
        [ "${loglines:-0}" -ge 1 ] || { status="FAIL (no audio_pump.log lines)"; }
    else
        [ "${loglines:-0}" -eq 0 ] || { status="note (log lines present without -audio_log)"; }
    fi
    if [ "$name" = "skip" ]; then
        # "skipping 5 boot screens": the parse ran and the chain was spent. A
        # -skip that matched nothing would still render and still produce a
        # capture, so that line is the only evidence the flag did its job.
        grep -q 'skipping 5 boot screens' "$log" 2>/dev/null || status="FAIL (no '-skip: skipping 5' line)"
    fi
    [ "$shots" -ge 1 ] || { status="FAIL (no capture - did it render?)"; }
    [ "$rc" -eq 0 ] || status="note (harness exit $rc)"

    printf '   %-30s %s  shots=%s  %s %s  loglines=%s  fps=%s\n' \
        "$status" "" "$shots" "${lowres:-low_res=?}" \
        "maxfps=${mfps:-?}" "${loglines:-0}" "${fps:-?}"

    summary="${summary}${name}: ${status}; shots=${shots}; ${lowres:-low_res=?} maxfps=${mfps:-?}; loglines=${loglines}; display=${display}"$'\n'
    rm -f "$AUDIO_LOG" 2>/dev/null
done

restore_ini
rm -f "$INI_BACKUP"
trap - EXIT INT TERM HUP

now_md5=$(md5 -q "$INI" 2>/dev/null || md5sum "$INI" | cut -d' ' -f1)
echo
if [ "$now_md5" = "$ORIG_MD5" ]; then
    echo "ini: restored, unchanged ($ORIG_MD5)"
else
    echo "!! midas.ini did NOT restore (now $now_md5, was $ORIG_MD5)" >&2
    fail=1
fi

echo
echo "== what to look at"
echo "   Each case's captures are in $outdir/<case>/, shot_NNNNN.bmp, ~2 MB each."
echo "   The pump block is six rows of small white text at the very top-left:"
echo "     fps tk f/s f/t | pmp rnd | que/cap | snd | dry/over/skip/frc | stall/clk/dev"
echo "     eyeball_windowed    windowed: logos/legal screen WITH the block on screen"
echo "     eyeball_fullscreen  same, fullscreen on display 0"
echo "     baseline            NO block (the flags are what turns it on)"
echo "     audio_pump          the block, before any stage has loaded"
echo "     audio_log           no block, but Data/Logs/audio_pump.log gains lines"
echo "     n64_aa              no block, 240-line render, 4:3 pillars, maxfps=-1 (-n64_aa)"
echo "     skip                no block, and the run reaches Select File (-skip)"
echo "     all                 block present, Classic Mode applied via -classic, log writing"
echo "   Convert one to look at it:"
first_shot=$(ls "$outdir"/eyeball_windowed/shot_*.bmp 2>/dev/null | tail -1)
echo "     sips -s format png ${first_shot:-$outdir/audio_pump/shot_00005.bmp} --out /tmp/x.png"
echo
echo "   REMINDER: the eyeball_* results are the observer's call, not this"
echo "   script's. It only proves a capture was written and the log agrees."
echo
printf '%s' "$summary"

# A README in the folder, because the folder is the deliverable: it is what a
# person opens when the terminal output is long gone. It records which renderer
# produced these pixels, which is the single fact most likely to be missing when
# someone compares them with their own screen.
renderer=$(sed -n 's/^renderer: *//p' "$outdir"/*/run.out 2>/dev/null | sed 's/  *<-.*//' | sort -u | head -1)
{
    echo "boot_flag_test run - $(date '+%Y-%m-%d %H:%M:%S')"
    echo
    echo "renderer: ${renderer:-unknown}"
    echo "  'Apple Software Renderer' means this run had NO GPU context (typically a"
    echo "  sandboxed or non-GUI shell). It draws correctly into the back buffer, so"
    echo "  the captures look right, but the window may not composite on screen."
    echo "  A launch from a normal terminal or by double-click gets the real GPU."
    echo
    echo "Cases, in order:"
    printf '%s' "$summary" | sed 's/^/  /'
    echo
    echo "Each <case>/ holds shot_NNNNN.bmp (the game's back buffer, 24-bit BMP)"
    echo "and run.out (that case's log). shot_NNNNN is the frame number, so a"
    echo "later capture is a later moment."
    echo
    echo "The pump block is six rows of small white text at the very top-left."
} > "$outdir/README.txt"

if [ "$open_dir" -eq 1 ]; then
    show_dir "$outdir"
fi
exit $fail
