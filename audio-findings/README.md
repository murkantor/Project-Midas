# Audio in the PC port — overview

**Status:** investigation notes with fixes applied, not maintainer documentation.

**Scope:** two audio defects — total silence on some machines, and ~100 ms of
output latency. Details, evidence and line references are in
[`findings.md`](findings.md).

**Date:** 2026-10-22.

Every claim is tagged:

- **Verified** — read directly in the tree, or reproduced locally.
- **Likely** — an inference from code and reports that needs a machine to
  confirm.
- **Open** — unresolved, or needs a decision.

No in-game testing was possible on the development machine (the port builds
32-bit x86; the dev machine is arm64 macOS). Play-testing quoted here was done
by the people who filed the reports and by the maintainer on a native 64-bit
macOS build. Anything not directly observed is marked.

## The one architectural fact to know first

`portAudioPump()` (`port/src/audio_pc.c`) is the only caller of
`alAudioFrame()`, and `alAudioFrame()` is the only caller of every AL client
handler:

```c
/* src/libultra/audio/synthesizer.c:189 */
client->samplesLeft += _timeToSamplesNoRound(drvr, (*client->handler)(client));
```

Those handlers are the SFX player and the three music sequencers. So the pump
**is the game's audio clock**: while it runs, the SFX event queue drains, the
sound-state list advances and music plays. The N64 ran this on a dedicated
audio thread; the port replaced it with one call per video frame.

Consequence, and the source of the first bug: stopping the pump is not "mute
output". It freezes the whole audio subsystem, which then fills its event queue
and drops thousands of events.

## The two bugs

| | Silence on some machines | ~100 ms output latency |
| --- | --- | --- |
| Reported as | #19 (Linux/Batocera), #56 (Windows 11) | "reload sound starts after the weapon is off-screen" |
| Cause | pump stopped permanently when the audio device failed to open, or stopped consuming | three audio buffers sized generously for a bring-up build |
| Effect | synthesizer never runs; queue saturates; nothing plays | every sound arrives ~100 ms late |
| Status | **Verified fixed** — play-tested by the reporter | **Fixed and unit-tested; not yet play-tested** |

`#56` is probably a **different** bug from `#19` — its log shows no queue
starvation at all. See `findings.md` §2.5. Do not treat one as explained by the
other.

## Latency, in numbers

At 22050 Hz stereo s16 the byte rate is 88200 B/s, so buffer sizes convert
straight to milliseconds:

| Term | Before | After |
| --- | --- | --- |
| Queue watermark (`PORT_AUDIO_QUEUE_LOW`) | 4096 B = 46.4 ms | 2048 B = 23.2 ms |
| One render quantum | 736 samples = 33.4 ms | 368 samples = 16.7 ms |
| Device period (`want.samples`) | 512 = 23.2 ms | 256 = 11.6 ms |
| **Worst-case total** | **~103 ms** | **~52 ms** |

These are arithmetic from the buffer sizes, not loopback measurements. The
reporter's subjective "about 100 ms" agrees with the before column.

## What changed

Three files touched, three added:

- `port/src/audio_pc.c` — device setup logs instead of failing silently and is
  retried; render pacing is explicit and cannot be blocked by the device; all
  three latency terms reduced.
- `port/include/audio_pace.h`, `port/src/audio_pace.c` — **new.** The pacing
  decisions, as pure functions with no SDL, no Ultra64 headers and no globals.
- `port/tests/audio_pace.c` — **new.** Ten tests over that policy.
- `CMakeLists.txt` — new `GE_PORT_TESTS` option, off by default.

## Running the tests

```sh
cmake -B build -DGE_PORT_TESTS=ON
cmake --build build --target port_tests
ctest --test-dir build -R audio_pace
```

The unit has no dependencies, so it also runs standalone — useful on a host that
cannot link the 32-bit game at all:

```sh
cc -std=c99 -Iport/include port/tests/audio_pace.c port/src/audio_pace.c \
   -o audio_pace_test && ./audio_pace_test
```

Passes in ~0.3 s. Needs no cart, display or sound device.

## What still needs doing

1. **Play-test the latency change.** The device period halved, which trades
   underrun headroom for responsiveness. Listen for crackle on a map load and
   during a busy firefight. If it crackles, set `PORT_AUDIO_DEV_SAMPLES` back to
   512 — that one constant costs ~12 ms and keeps the rest of the gain.
2. **`portAcmdValidate`'s silent whole-frame bail-out** — leading suspect for
   #56, and it can discard every frame of a run without saying so.
   See `findings.md` §7.2.
3. **Watch text flashing too fast** — identified, not investigated.
   See `findings.md` §7.1.

## Known cosmetic defect

`port/src/audio_pc.c` line 85 carries an orphaned comment left over from an
edit, describing a constant that no longer exists:

```c
/* With no device present nothing is heard, so it costs nothing to stop
 * handing it samples. Kept low so the fallback rate matches the DAC's. */
```

No behavioural effect. It should be deleted before this ships, because it
implies a knob that isn't there.

## Files

- [`findings.md`](findings.md) — evidence, root causes, exact figures, code
  changes, verification, open leads.
