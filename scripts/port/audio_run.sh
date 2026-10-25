#!/bin/bash
# Time-bounded live run for audio / tick investigation.
#
# Every live run on this branch needs the same four things, and getting any of
# them wrong has already cost a session:
#
#   1. back up build_macos/midas.ini and restore it afterwards - it is the
#      user's real settings file. The current build rewrites tickhz=0 to
#      tickhz=hw on launch (F3-A), so "the file changed" is now expected and
#      needs checking rather than assuming.
#   2. leave Data/Saves/ alone. eeprom.bin's md5 is recorded and compared.
#   3. bound the run by WALL TIME, not by GE_FRAMES. A hw-pacing Dam run costs
#      ~890 ms per frame under the software renderer, so a frame count either
#      ends before the stage or runs for four minutes.
#   4. stop as soon as the evidence exists, rather than at a guessed count.
#
#   scripts/port/audio_run.sh                          # 60 s, audio log, Dam
#   scripts/port/audio_run.sh -t 30 -s dam -w 'credit [0-9]* of' -n 5
#   scripts/port/audio_run.sh -e GE_HWSCALE=3,GE_HITCHMS=1 -t 45
#
# Options:
#   -t SECONDS   wall-clock limit (default 60)
#   -s STAGE     GE_STAGE value (default dam; "" disables it and stays in menus)
#   -w REGEX     stop early once this regex matches the run log that many times
#   -n COUNT     how many matches -w needs (default 5)
#   -e K=V,..    extra environment for the game
#   -g "ARGS"    extra command-line arguments for the game (e.g. -n64_aa)
#   -a 0         do not enable cl_audio_log
#   -k           keep the ini as the run left it (default: restore it)
#
# Exit status: 0 = the pattern was found (or no pattern was asked for) and the
# ini/saves came back untouched; 1 = something to look at; 2 = usage/state error.

set -u

BUILD="$(cd "$(dirname "$0")/../.." && pwd)/build_macos"
GAME="$BUILD/ProjectMidas"
INI="$BUILD/midas.ini"
SAVE="$BUILD/Data/Saves/eeprom.bin"
LOG="${TMPDIR:-/tmp}/audio_run.$$.log"
AUDIO_LOG="$BUILD/Data/Logs/audio_pump.log"

seconds=60
stage=dam
want_re=""
want_n=5
extra_env=""
game_args=""
audio_log=1
keep_ini=0

while getopts "t:s:w:n:e:g:a:k" opt; do
    case "$opt" in
        t) seconds=$OPTARG ;;
        s) stage=$OPTARG ;;
        w) want_re=$OPTARG ;;
        n) want_n=$OPTARG ;;
        e) extra_env=$OPTARG ;;
        g) game_args=$OPTARG ;;
        a) audio_log=$OPTARG ;;
        k) keep_ini=1 ;;
        *) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done

[ -x "$GAME" ] || { echo "no game at $GAME - build first" >&2; exit 2; }
[ -f "$INI" ]  || { echo "no midas.ini at $INI" >&2; exit 2; }

BACKUP="${TMPDIR:-/tmp}/midas.ini.audio_run.$$"
cp "$INI" "$BACKUP"
INI_MD5=$(md5 -q "$INI" 2>/dev/null || md5sum "$INI" | cut -d' ' -f1)
SAVE_MD5_BEFORE=$([ -f "$SAVE" ] && (md5 -q "$SAVE" 2>/dev/null || md5sum "$SAVE" | cut -d' ' -f1) || echo "absent")

restore() {
    if [ "$keep_ini" -eq 0 ]; then
        cp "$BACKUP" "$INI"
    fi
    rm -f "$BACKUP"
}
trap restore EXIT

# A second window steals input focus from a live session; refuse like nat_test.sh.
if pgrep -f "$(basename "$GAME")" >/dev/null 2>&1; then
    echo "another ProjectMidas is already running - close it first" >&2
    exit 2
fi

concmd=""
[ "$audio_log" -ne 0 ] && concmd="cl_audio_log 1"

echo "== run: ${seconds}s ${stage:-(menus)}"
[ -n "$extra_env"    ] && echo "   env:  $extra_env"
[ -n "$game_args"    ] && echo "   args: $game_args"
[ -n "$concmd"       ] && echo "   cmd:  $concmd"
[ -n "$want_re"      ] && echo "   stop: $want_n x /$want_re/"
echo "   ini:  $INI_MD5"
echo "   save: $SAVE_MD5_BEFORE"

env_args=()
[ -n "$stage" ] && env_args+=("GE_STAGE=$stage")
# A frame count large enough never to bind before the time limit does.
env_args+=("GE_FRAMES=100000")
[ -n "$concmd" ] && env_args+=("GE_CONCMD=$concmd")
if [ -n "$extra_env" ]; then
    IFS=',' read -r -a pairs <<< "$extra_env"
    for p in "${pairs[@]}"; do env_args+=("$p"); done
fi

: > "$LOG"
# shellcheck disable=SC2086  # game_args is a deliberate word-split argument list
env "${env_args[@]}" "$GAME" $game_args > "$LOG" 2>&1 &
pid=$!
start=$(date +%s)
found=0
timeout_hit=0

while true; do
    sleep 1
    now=$(date +%s)
    elapsed=$((now - start))
    if ! kill -0 "$pid" 2>/dev/null; then
        break   # exited on its own (crash, or GE_FRAMES bound)
    fi
    if [ -n "$want_re" ]; then
        n=$(grep -Ec "$want_re" "$LOG" 2>/dev/null || true)
        if [ "${n:-0}" -ge "$want_n" ]; then
            found=1
            echo "== evidence reached ($n matches) after ${elapsed}s - stopping"
            kill "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            break
        fi
    fi
    if [ "$elapsed" -ge "$seconds" ]; then
        timeout_hit=1
        echo "== time limit ${seconds}s - stopping"
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
        break
    fi
done

elapsed=$(( $(date +%s) - start ))

renderer=$(grep -m1 'GL: renderer' "$LOG" 2>/dev/null | sed 's/.*GL: renderer: *//')
stages=$(grep -o 'stage [0-9]*' "$LOG" 2>/dev/null | sort -u | tr '\n' ' ')
hitches=$(grep -c 'ge: hitch:' "$LOG" 2>/dev/null || true)
credits=$(grep -c 'credit [0-9]* of' "$LOG" 2>/dev/null || true)
dries=$(grep -c 'dry [1-9]' "$LOG" 2>/dev/null || true)

echo
echo "ran ${elapsed}s"
echo "renderer: ${renderer:-unknown}   <- numbers above 60 fps are meaningless on a software renderer"
echo "stages:   ${stages:-none}"
echo "hitches:  ${hitches:-0}   credit lines: ${credits:-0}   audio-log lines with dry>0: ${dries:-0}"
echo "log:      $LOG"
[ -f "$AUDIO_LOG" ] && { cp "$AUDIO_LOG" "$LOG.audio"; echo "audio:    $LOG.audio  (scripts/port/audio_pump_log.py $LOG.audio)"; }

status=0
restore   # explicit, so the checks below see the restored file
trap - EXIT

if [ "$keep_ini" -eq 0 ]; then
    now_md5=$(md5 -q "$INI" 2>/dev/null || md5sum "$INI" | cut -d' ' -f1)
    if [ "$now_md5" != "$INI_MD5" ]; then
        echo "!! midas.ini did NOT restore (now $now_md5) - put it back by hand" >&2
        status=1
    else
        echo "ini: restored, unchanged"
    fi
fi
if [ -f "$SAVE" ]; then
    save_now=$(md5 -q "$SAVE" 2>/dev/null || md5sum "$SAVE" | cut -d' ' -f1)
    if [ "$save_now" != "$SAVE_MD5_BEFORE" ]; then
        echo "!! eeprom.bin CHANGED - the run touched the save file" >&2
        status=1
    fi
fi

[ -n "$want_re" ] && [ "$found" -eq 0 ] && { echo "pattern never reached" >&2; status=1; }
[ "$timeout_hit" -eq 1 ] && [ -n "$want_re" ] && [ "$found" -eq 0 ] && status=1

exit $status
