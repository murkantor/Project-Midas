#!/usr/bin/env python3
"""Read Data/Logs/audio_pump.log and report whether the pump's queue starved.

The log line is written once a second by portAudioLogLine (port/src/audio_pc.c).
Capture it with `cl_audio_log 1` in the console, or GE_CONCMD="cl_audio_log 1".

    scripts/port/audio_pump_log.py                     # build_macos/Data/Logs/audio_pump.log
    scripts/port/audio_pump_log.py /tmp/run.log
    scripts/port/audio_pump_log.py run.log --all       # every line, not just changes

WHAT IT REPORTS, AND WHAT IT CANNOT TELL YOU

The log samples the queue once a second. The pump runs far more often than that,
so a starvation that opens and closes inside one second can be missed entirely
between two samples. `dry` is the pump's own count and is therefore the
authoritative starving signal - this script counts it, and treats the queue
depths as corroboration, not proof.

The pump interval is not in the log either. It can be estimated from the target
only while the ratchet is idle: the floor is one interval + PORT_AUDIO_DEV_PERIOD_MS
(12 ms), so interval ~= target_ms - 12. Once an underrun has been learned the
target carries extra headroom on top of that (portAudioPaceEffectiveTarget), and
the estimate becomes a lower bound. This script therefore reports the deficit
`que - target` and the trend of `target`, which are what the fix changed, and
never claims an interval it cannot measure.

Reading the output:

  dry>0        a pump found the sink empty and played silence. This is the
               defect signal; the queue columns cannot show a shorter gap.
  que << tgt   the pump is behind its own target - it is not refilling fast
               enough, or the cap is refusing frames (check `over`).
  tgt climbing the underrun ratchet is doing its job: each dry adds one
               PORT_AUDIO_TARGET_STEP and it is now reaching the queue. Before
               commit df881c9 this was masked below ~8.6 fps.
  tgt pinned   target == interval + 12 every line, never rising even after a
               dry: the ratchet is being discarded again - see
               docs/port/audio-pump-history.md section 9.
  over>0       frames were refused because they did not fit under `cap`.
  stall 1      the device has played nothing for 2 s; the pump has taken over
               the clock. A device-layer problem, not a pump one.
"""

import re
import sys

FIELDS = re.compile(
    r"t=(?P<t>\d+)ms FPS (?P<fps>\d+) tk (?P<tk>\d+) f/s (?P<fs>[\d.]+) f/t (?P<ft>[\d.]+)"
)
QUEUE = re.compile(r"que (?P<qbytes>\d+)/(?P<tbytes>\d+)B (?P<qms>\d+)/(?P<tms>\d+)ms cap (?P<cap>\d+)B")
HEALTH = re.compile(
    r"dry (?P<dry>\d+) over (?P<over>\d+) skip (?P<skip>\d+) frc (?P<frc>\d+) stall (?P<stall>\d+)"
)

DEVICE_PERIOD_MS = 12  # PORT_AUDIO_DEV_PERIOD_MS, port/src/audio_pc.c


def parse(path):
    rows = []
    for line in open(path, errors="replace"):
        if not line.startswith("t="):
            continue
        f, q, h = FIELDS.search(line), QUEUE.search(line), HEALTH.search(line)
        if not (f and q and h):
            continue
        rows.append(dict(
            t=int(f["t"]), fps=int(f["fps"]), fs=float(f["fs"]), ft=float(f["ft"]),
            que_ms=int(q["qms"]), tgt_ms=int(q["tms"]), cap_bytes=int(q["cap"]),
            dry=int(h["dry"]), over=int(h["over"]), skip=int(h["skip"]),
            stall=int(h["stall"]),
        ))
    return rows


def main(argv):
    path = argv[1] if len(argv) > 1 and not argv[1].startswith("-") else \
        "build_macos/Data/Logs/audio_pump.log"
    show_all = "--all" in argv

    try:
        rows = parse(path)
    except OSError as e:
        print(f"cannot read {path}: {e}", file=sys.stderr)
        return 1
    if not rows:
        print(f"{path}: no audio_pump lines found. Run with cl_audio_log 1.", file=sys.stderr)
        return 1

    print(f"{path}  ({len(rows)} lines, {rows[-1]['t'] / 1000:.1f}s)")
    print()
    print(f"{'t s':>6} {'fps':>4} {'f/s':>5} {'f/t':>5} {'que':>7} {'tgt':>7} "
          f"{'que-tgt':>8} {'dry':>4} {'over':>4} {'stall':>5}")
    prev = None
    for r in rows:
        deficit = r["que_ms"] - r["tgt_ms"]
        changed = (
            prev is None
            or r["dry"] != prev["dry"]
            or r["over"] != prev["over"]
            or r["stall"] != prev["stall"]
            or abs(deficit) > 20
            or r["fps"] != prev["fps"]
        )
        if show_all or changed:
            print(f"{r['t'] / 1000:>6.0f} {r['fps']:>4} {r['fs']:>5.1f} {r['ft']:>5.2f} "
                  f"{r['que_ms']:>6}ms {r['tgt_ms']:>6}ms {deficit:>+7}ms "
                  f"{r['dry']:>4} {r['over']:>4} {r['stall']:>5}")
        prev = r

    dry = sum(r["dry"] for r in rows)
    over = sum(r["over"] for r in rows)
    stall = max(r["stall"] for r in rows)
    worst = min(rows, key=lambda r: r["que_ms"] - r["tgt_ms"])
    targets = [r["tgt_ms"] for r in rows]

    print()
    print(f"dry (staved sinks): {dry}")
    print(f"over (frames refused at cap): {over}")
    print(f"stall (device silent 2 s+): {stall}")
    print(f"worst queue-vs-target: {worst['que_ms'] - worst['tgt_ms']:+}ms "
          f"at t={worst['t'] / 1000:.0f}s ({worst['que_ms']}ms queued, "
          f"{worst['tgt_ms']}ms target, {worst['fps']} fps)")
    print(f"target: {min(targets)}..{max(targets)}ms")

    # What the target did after a starve, reported as an observation only.
    #
    # Three attempts at a categorical verdict ("the ratchet worked" / "it is
    # masked") each produced a wrong answer on a real log while this script was
    # being written, because the log samples once a second with per-window
    # counters: a dry that lands on a frame-rate boundary is indistinguishable
    # from one in a steady state, and the interval floor moves with the frame
    # rate. So this prints the evidence and says plainly that the log cannot
    # settle the mechanism. The authority for that is the simulation in
    # port/tests/audio_pace.c, which controls the order and the cadence.
    dry_rows = [i for i, r in enumerate(rows) if r["dry"] > 0]
    if dry_rows:
        first_tgt = rows[dry_rows[0]]["tgt_ms"]
        after = [rows[i]["tgt_ms"] for i in range(dry_rows[0] + 1, len(rows))]
        print()
        print(f"lines with a starve: {len(dry_rows)}, first at t={rows[dry_rows[0]]['t'] / 1000:.0f}s "
              f"with a {first_tgt}ms target")
        if after:
            peak_after = max(after)
            print(f"highest target after that: {peak_after}ms")
            if peak_after > first_tgt:
                print("  -> higher afterwards: consistent with the ratchet adding headroom.")
                print("     Not proof - the interval floor also moves with the frame rate.")
            else:
                print("  -> no higher target afterwards. Consistent with the ratchet being")
                print("     discarded (commit df881c9's defect), but equally with a frame rate")
                print("     that settled lower. The 1 Hz sample cannot tell these apart.")
    print()
    print("The log cannot settle whether the ratchet reached the queue. That is")
    print("port/tests/audio_pace.c's job: it controls the cadence and the order, which")
    print("a 1 Hz sample of a live session cannot. This script reports what the sink did.")

    return 0 if dry == 0 else 2

if __name__ == "__main__":
    sys.exit(main(sys.argv))
