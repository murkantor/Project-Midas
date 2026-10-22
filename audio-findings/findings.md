# Audio in the PC port — findings

Companion to [`README.md`](README.md). Line references are to `port/src/` and
`src/` in the current tree.

Claims are tagged **Verified** (read in the tree or reproduced), **Likely**
(inference needing a machine to confirm), or **Open**.

---

## 1. The pipeline, and why the pump is the game's audio clock

The AL synthesizer runs CPU-side and emits an `Acmd` list that
`portAcmdRun()` interprets into a sample buffer. Three facts frame everything
below.

**Verified — the pump is the only driver.** `portAudioPump()` calls
`alAudioFrame()`; nothing else does. The N64 had a dedicated audio thread
running `amMain`, but on PC `amMain` is never scheduled — `osCreateThread`
(`port/src/libultra_shim.c:840`) only records state. The pump replaces the
thread with one call per video frame from `portPumpEvents()`
(`port/src/pcsched.c`, in the depth-0 duty block).

**Verified — the pump is the only thing that advances any AL client.**

```c
/* src/libultra/audio/synthesizer.c:189 */
client->samplesLeft += _timeToSamplesNoRound(drvr, (*client->handler)(client));
```

This is the sole call site of any AL client handler. The registered clients are
the SFX player (`sndPlayerVoiceHandler`, registered at `src/snd.c:403`) and the
three music sequencers (`src/libultra/audio/csplayer.c:121`,
`seqplayer.c:120`). So while `alAudioFrame` runs, the SFX event queue drains,
the sound-state list advances and music sequences; when it stops, all of it
stops.

**Verified — therefore device state must never gate the pump.** This single
consequence explains the first bug and constrains the fix for the second. The
port must keep the synthesizer running whether or not anything is listening.

---

## 2. Bug one: total silence on some machines (#19, #56)

### 2.1 Symptom

The game runs, renders, takes input, and produces no sound. Music and SFX both
at 100%. The only audio line in the log was:

```
ge: audio: global volumes - music 100%, sound 100%
```

which was read in the issue thread as "audio initialised fine". It is printed by
`src/game/options.c:4759` and says nothing about whether a device exists.

### 2.2 Evidence

The Linux reporter captured this while the game ran:

```
ge: snd: evtq FULL - 256 queued (254 due now) | pan 0 vol 0 play 18 decay 0
stop 0 end 0 other 238 | states 21 live, 0 voiced, 0 waiting for a voice
(0 of 16 voices) | heartbeat alive | coalesced 0 evicted 0 dropped 3328
ge: snd: event queue FULL - dropped event type 1024 (drop #3329)
```

Every field is diagnostic, and each was checked against the code rather than
assumed:

- **`256 queued`** is this build's exact queue capacity, not a round number:
  `MUSIC_SFX_SEQ_CONFIG_MAX_EVENTS (0x40)` × `MUSIC_POOL_SCALE` (default 2,
  `src/music.c:141`) × `MUSIC_SFX_EVENT_HEADROOM` (2, `src/music.c:110`) = 256.
  Composed at `src/music.c:908`. The census in `src/snd.c` prints **only on a
  drop**, so 256 means 100% full. **Verified.**
- **`254 due now`** — events are ready to run immediately. Nothing is running
  them.
- **`0 voiced`** and **`0 of 16 voices`** — `alSynAllocVoice` is only reached
  from `sndHandleEvent`, which only runs inside `alAudioFrame`. No voice was
  ever allocated, which is direct evidence the synthesizer never ran.
- **`heartbeat alive`** — the game thread is still posting `AL_SNDP_API_EVT`
  events. Only the consumer is gone.
- **`dropped 3328`**, later including `type 1024` = `AL_SNDP_DEACTIVATE_EVT`
  (`src/snd.h:44` region) — the queue never drains, so drops accumulate without
  bound.

This is not quiet audio. It is a stopped synthesizer.

### 2.3 Root cause

The original `portAudioPump` had two device-gated exits:

```c
if (s_AudioDead) return;                                        /* permanent */
if (SDL_GetQueuedAudioSize(dev) >= PORT_AUDIO_QUEUE_LOW) break; /* per-pump  */
```

`s_AudioDead` was set on **any** `SDL_OpenAudioDevice` failure and never
cleared. Nothing was printed on that path: no `SDL_GetError()`, no driver name,
no obtained format. The failure branches were exactly `s_AudioDead = 1;`
followed by a bare `return;` (**Verified**).

It was the only SDL subsystem in the port failing silently — `input_pc.c:2119`,
`video.c:5156` and `raphnet.c:289` all log their failures — and the Windows
build opens no console, so a mute system produced a log that looked healthy.

### 2.4 Why it only affected some machines

**Likely.** The port requests an exact device: 22050 Hz, `AUDIO_S16SYS`, stereo,
`allowed_changes = 0`. Almost no system has that natively; it works only because
the backend converts behind SDL — WASAPI `AUTOCONVERTPCM`, CoreAudio, ALSA
`plug`, PulseAudio/PipeWire resampling.

Where a 32-bit client cannot reach that layer — the Batocera report in #19 is
exactly "32-bit PipeWire can't find 64-bit SPA plugins" — or the endpoint
refuses the spec, the open fails and the game goes permanently and silently
mute. Machines where audio works never touch the failure path, which is why this
survived extensive testing before release.

This is inference from the reports plus the code. No affected machine was
available to produce the actual `SDL_OpenAudioDevice` error. The fix makes that
failure self-reporting, so the next report will confirm it directly.

### 2.5 The Windows report (#56) is probably a *different* bug

**Open.** `Carpo97`'s report does not match the above:

- No `ge: snd: evtq FULL` line anywhere in his log, which argues the pump was
  running.
- `sdlaudio.raw`, captured through SDL's `disk` driver, is entirely zeroes.

The most likely path is `portAcmdValidate` (`port/src/audio_pc.c`, in the
interpreter section): it returns 0 for a whole frame if any command has
`w1 < 0x01000000` or is odd, and `portAudioPump` then still queues `frameBuf` —
a static buffer that remains all-zero if that has happened since boot. That
would produce: music and SFX silent, game fine, no queue drops, player running,
all-zero capture.

Its only diagnostic goes through `ge_dbg_vec`, which is gated on
`GE_DUMP_BOND`, so it is invisible in a normal log.

**This is a hypothesis, not a diagnosis.** Confirming it needs #56's reporter to
rerun with `GE_DUMP_BOND=1` and `grep -i acmd`.

---

## 3. Bug two: ~100 ms of output latency

### 3.1 Symptom

Reported by a tester comparing side by side with real hardware:

> Notice where the weapon reload sound starts. On real hardware, it starts at
> the frame where the weapon starts to lower. But on this build, the sound
> starts after the weapon is off-screen.

Also visible in a release-era livestream, so it predates the silence fix —
**Verified** not a regression from it.

### 3.2 The three terms

At 22050 Hz stereo s16 the byte rate is 88200 B/s, so bytes convert directly to
milliseconds. **Verified** by reading the constants.

**Queue watermark.** `PORT_AUDIO_QUEUE_LOW` was `4 * 1024` = 4096 B = 46.4 ms.
The pump refills only while the queue is below it, so the queue always holds at
least that much audio. It is the floor of the steady-state backlog and therefore
a direct addition to latency.

**Render quantum.** One full `g_FrameSize`, derived at `src/audi.c:342-356`:

```c
fsize = (f32) ((alconf->outputRate << FRAMES_PER_FIELD_AS_POW2) / (f32)MAYBE_FRAME_RATE);
g_FrameSize = (u32) fsize;
if (g_FrameSize < fsize) g_FrameSize++;
if (g_FrameSize & 0xf) g_FrameSize = (g_FrameSize & ~0xf) + 0x10;   /* 16-align */
```

`(22050 << 1) / 60` = 735 → 736 samples = **33.4 ms**. One video field at
60 Hz. An event posted between pumps waits up to one of these for its first
rendered sample.

**Device period.** `want.samples = 512` = **23.2 ms**, the cushion the sound card
owns.

The queue oscillates in `[watermark, watermark + one quantum]` = 46–80 ms. Add
the device period: **63–103 ms**. Weapon lowering is roughly 100–150 ms at
60 Hz, which is why the reload sound lands right about where the weapon leaves
the frame.

### 3.3 One term is authentic and was left alone

**Verified.** The SFX player's own clock granularity is
`AL_USEC_PER_FRAME_30FPS` (33 ms), set at `src/snd.c:368`:

```c
g_sndPlayerPtr->frameTime = AL_USEC_PER_FRAME_30FPS;
```

A `PLAY` event posted between client wake-ups waits up to that long. Hardware
behaved the same way. This was **deliberately not changed** — do not "fix" it
while chasing the remaining latency, and do not attribute the reported lag to
it.

### 3.4 Two flaws found in the first fix

Both were introduced while fixing bug one, and both are worth recording because
they are the same error class as the original bug, inverted.

**Pacing conflict.** The silence fix made the pump render whenever the wall
clock owed a frame, *in addition to* the existing queue-driven catch-up. At
steady state the two cadences coincide — which is why a play test showed no
regression — but on a device consuming even slightly slower than real time, the
clock branch keeps rendering, the queue creeps toward `PORT_AUDIO_QUEUE_MAX`
(16 KB = **185.8 ms**), and discarded frames become audible glitches.

The original bug was device state *stopping* the synthesizer. This one was
device state failing to *stop* it when latency demanded.

**Stall detection that couldn't tell health from death.** The first detector
treated "queue byte count unchanged since the last pump" as a dead sink. That is
wrong at these rates: the pump runs every 6.8 ms (147 Hz, from field logs
reading `tick120 pump147`) while an SDL device period is 11.6 ms at 256 samples,
so a **healthy** queue is legitimately byte-identical across a pump or two. Real
consumption has to be measured, not change.

---

## 4. Code changes

### 4.1 `port/src/audio_pc.c` — device setup

**`portAudioReportFail()`** (new, ~line 690). Prints the failing operation,
`SDL_GetError()`, the current driver, the attempt number and the retry cadence:

```
ge: audio: SDL_OpenAudioDevice(22050 Hz, s16, stereo) failed (SDL: ...; driver none)
    - no output, attempt 1, retrying every 2000 ms
```

Rate-limited to the 1st and every 30th failure, so a permanently mute machine
cannot bury the rest of the log.

**`portAudioInit()`** (~line 710). Was `static void`; now `static s32` returning
success/failure. Logs the successful open too:

```
ge: audio: device open - driver pipewire, 22050 Hz, 2 ch, 512 samples, 4096 byte buffer
```

`s_AudioDead` is gone. The pump retries every `PORT_AUDIO_RETRY_MS` (2000 ms),
so a sink that appears or moves after boot is picked up: a headset plugged in, a
sound server restart, a 32-bit client stack that was not up yet.

The exact-format request was **deliberately not relaxed**. `allowed_changes`
stays 0 because `g_FrameSize` is derived from the synth rate
(`src/audi.c:344`), so the mixer is sized for 22050 Hz specifically. Accepting a
device rate needs an `SDL_AudioStream` in front — see §7.3.

### 4.2 `port/src/audio_pc.c` — render pacing

**Halved quantum.** `PORT_AUDIO_FRAME_DIV 2` gives a render chunk of half a
field: 368 samples, 16.7 ms, instead of 736 and 33.4 ms. Throughput is
unchanged — two quanta per pump at 147 Hz cover the 2944 samples a field needs —
only granularity improves, which halves the worst-case wait for a newly
triggered event's first sample.

**Exclusive pacing authority.** The rule is now explicit:

- **device draining** → refill while `queued < PORT_AUDIO_QUEUE_LOW`, then stop.
  The DAC sets the rate, so the backlog cannot drift upward.
- **no device, or a stalled one** → the wall clock paces, so game audio time
  still advances. Samples are simply discarded.

Neither branch can be *blocked* by the device. The clock's owed backlog is
forced to zero while a device is live, so handing over to the fallback starts
from zero instead of dumping a stale backlog.

The clock is kept in samples ×1000 so a 147 Hz pump's fractional share of a
736-sample frame cannot round to zero and stall the render, and catch-up is
clamped to `PORT_AUDIO_MAX_CATCHUP_MS` (100 ms) per pump so a debugger pause or
a long load cannot replay minutes of audio at once.

**Consumption-based stall detection.** `PortAudioDrain` tracks `at_exit` (queue
left at the end of the last pump) and `famished` (consecutive pumps with no
consumption). Consumption is `at_exit − queued_now`. The device stops being
trusted as pacing authority after `PORT_AUDIO_DRAIN_PUMPS` (8, ~54 ms) empty
pumps, and is closed and reopened only after `PORT_AUDIO_STALL_PUMPS` (120,
~0.8 s). The wide gap means a merely hesitant device falls back to clock pacing
long before it is torn down.

**`portAudioPumpForce` renders exactly one frame**, as its comment always
claimed. Previously `s_ForceOneFrame` bypassed the queue gate per iteration, so
a forced call could render up to 8 — and `music.c`'s stop-wait spins it up to
512 times (**Verified**).

### 4.3 `port/include/audio_pace.h`, `port/src/audio_pace.c` — new

The pacing decisions as pure functions: `portAudioDrainTick()`,
`portAudioShouldReopen()`, `portAudioShouldRender()`, `portAudioQuantum()`. No
SDL, no Ultra64 headers, no globals, no clock reads. `portAudioPump()` gathers
observations and obeys the verdict.

Constants live here:

```c
#define PORT_AUDIO_QUEUE_LOW     (2 * 1024)  /* watermark, 23.2 ms      */
#define PORT_AUDIO_QUEUE_MAX     (16 * 1024) /* ceiling, 185.8 ms       */
#define PORT_AUDIO_DRAIN_PUMPS   8           /* pacing fallback, ~54 ms */
#define PORT_AUDIO_STALL_PUMPS   120         /* close + reopen, ~0.8 s  */
#define PORT_AUDIO_DEV_SAMPLES   256         /* device period, 11.6 ms  */
```

It uses local `port_s32` / `port_u32` / `port_u64` typedefs rather than the
usual `s32`/`u32`, because `PR/ultratypes.h` gates its typedefs behind
`_LANGUAGE_C` and the unit must compile with no project headers at all. The
`port_` prefix avoids clashing when Ultra64 types are also in scope.

**Why extract rather than test `audio_pc.c` directly:** that file needs SDL and
the whole Ultra64 audio stack to compile, which makes a unit test heavy and
host-dependent. Extracting keeps the test at zero dependencies. It also means
the test exercises **the shipped policy**, not a reimplementation of it — see
§6.2 for why that distinction mattered in practice.

### 4.4 `port/tests/audio_pace.c` — new

Ten tests, ROM-free and device-free, following the `CHECK` macro convention.
Coverage:

- Render quantum: half a field, 16-aligned; degenerate arguments (0 field, 0
  divisor, too small, too large, uncapped).
- A live device paces to the watermark — including the case that matters most:
  a large clock debt must **not** override a full-enough device queue.
- No device → the wall clock paces, so the synthesizer keeps running.
- A stalled device falls back to the clock instead of freezing.
- Drain tracker: an empty queue counts as live (nothing exists to drain); a
  hesitation that recovers clears the streak; a dead sink is detected within
  `PORT_AUDIO_DRAIN_PUMPS` and reopened at `PORT_AUDIO_STALL_PUMPS`; the two
  thresholds are ordered so a hesitant device is never closed.
- A simulated pump loop against a device that drains in whole periods — the
  bursty pattern that broke the first detector — asserting the backlog never
  leaves the watermark band over ten seconds, and that render volume tracks
  consumption exactly. Rendering more would grow latency; less would starve it.
- Latency arithmetic asserted at ≤55 ms rather than eyeballed.

### 4.5 `CMakeLists.txt`

Appended at the end of the file, after `add_executable(ge ...)`, so it cannot
affect the game target:

```cmake
option(GE_PORT_TESTS "Build the ROM-free port unit tests" OFF)
```

plus an `add_custom_command` / `add_custom_target(port_tests)` pair that builds
the test binary with explicit `-std=c99 -Wall -Wextra -Iport/include`.

It does **not** use `add_executable` because the directory-wide
`add_compile_options(-m32 -msse2 ...)` near the top of the file cannot be
un-inherited, and this unit is portable C99 that should build on any host able
to run it — including 64-bit and ARM dev machines that cannot link the game.

`port/src/audio_pace.c` needs no CMake entry for the game build: it is picked up
by the existing `file(GLOB PORT_SRC ... port/src/*.c)`. `port/include` is
already on the global include path.

---

## 5. Why a build shipped with this in it

Worth recording, because it explains why "it works on my machine" was true for
so long.

The silence bug required a *failed device open*, which never happened on
machines where the audio backend could convert the requested format. The latency
bug was present everywhere but is only obvious in a side-by-side comparison
against hardware with a known-good reference; without one, ~100 ms reads as
"video game audio is a bit loose".

Neither is visible in a compile, a smoke test, or a log that doesn't already
know what to look for.

---

## 6. Verification

### 6.1 Verified

| Check | Method | Result |
| --- | --- | --- |
| Silence fix compiles | `clang -fsyntax-only` with the real 64-bit macOS build flags | clean |
| Silence fix works | play-tested by the maintainer and by the reporter (Arch, mission with gunfire) | **audio works** |
| `audio_pc.c` compiles | `-fsyntax-only`, same flags | clean |
| `audio_pace.c` compiles standalone | `cc -std=c99 -Iport/include -Wall -Wextra` | clean, no warnings |
| `audio_pace.c` has no project deps | builds with no SDL, no Ultra64 headers | confirmed |
| Tests pass standalone | `cc ... && ./audio_pace_test` | `audio pace tests passed` |
| Tests pass via CMake/ctest | `cmake -B verify -DGE_PORT_TESTS=ON`, build, `ctest -R audio_pace` | 1/1 passed, ~0.3 s |

### 6.2 Mutation testing

A suite that cannot fail is decoration, so three deliberate regressions were
introduced into `audio_pace.c` and each was confirmed to be caught:

| Mutant | Caught by |
| --- | --- |
| `portAudioShouldRender` always returns 1 (no watermark) | backlog-band test |
| Drain threshold effectively 1 (any gap = dead) | healthy-sink simulation |
| No-device branch returns 0 (**the original silence bug**) | clock-pacing test |

The third is the valuable one: the suite would have caught the bug that started
this investigation.

The earlier caution was justified in practice. A first attempt modelled the
policy in a throwaway script instead of testing the real functions; that model
had a variable-shadowing bug which made a healthy sink look permanently
stalled. Testing a reimplementation proves only that the test agrees with
itself. It also caught two arithmetic errors in the test itself — 736 is already
16-aligned, so a divisor of 1 yields 736, not 720; and the reopen threshold
trips one pump early because `famished` is incremented before the check.

### 6.3 Not verified

1. **No in-game run of the latency change.** The blocking item. Specifically
   untested: whether `PORT_AUDIO_DEV_SAMPLES = 256` underruns under load.
2. **No full build on any platform.** The development machine is arm64 macOS
   and the port is 32-bit x86; the game cannot be linked there. All compiles
   above are syntax-only or standalone units. CI is the first real build.
3. **The per-machine silence cause is inference** (§2.4), not an observed
   failure.
4. **#56 remains unexplained** (§2.5).
5. **The watch-text lead** (§7.1) was identified, not investigated.
6. **Latency figures are arithmetic, not measurements** — computed from buffer
   sizes and the byte rate, with no loopback or timing capture.
7. **Drain thresholds are reasoned, not tuned.** `PORT_AUDIO_DRAIN_PUMPS = 8`
   comes from comparing the pump interval (6.8 ms at 147 Hz) with the device
   period (11.6 ms at 256 samples) plus margin. It has not been exercised
   against a real device that hesitates.

---

## 7. Open leads and follow-ups

### 7.1 Watch text flashes too fast

**Open — a lead, not a finding.** Reported in the same testing session as the
latency bug: the watch text flashes faster than on N64.

The game keeps discrete 60 Hz cadence through `g_ClockTimer`'s `> 0` guards,
while `g_GlobalTimerDelta` carries the real-time float delta
(`src/game/lv.c:1530-1551`):

```c
#define PORT_TICK_DELTAF (portRunning ? portTickDelta : 0.0f)
...
g_GlobalTimer += g_ClockTimer;          /* integer 60 Hz step  */
g_GlobalTimerDelta = PORT_TICK_DELTAF;  /* float, true step    */
```

Anything that accumulates the **float** delta advances at the raised tick rate.
At `tickhz=120` that is **2× too fast**. `bondview2.c` has about a dozen such
accumulators, including `pause_transition_time` at `bondview2.c:3692` and
`:3708` — the watch transition, the right area for a flashing-text symptom.

To confirm: find the watch-text blink counter and check whether it accumulates
`g_GlobalTimerDelta` or `g_ClockTimer`. If the former, the fix is to use the
integer step (or divide by the tick-rate ratio) so the visual cadence stays
60 Hz while continuous motion keeps the float delta.

### 7.2 `portAcmdValidate`'s silent whole-frame bail-out

**Open — highest-priority correctness issue found.** In `port/src/audio_pc.c`,
before the interpreter loop:

- A frame is discarded entirely if any command has `w1 < 0x01000000` or is odd.
- The stale `frameBuf` contents are then queued anyway.
- The only diagnostic goes through `ge_dbg_vec`, which requires
  `GE_DUMP_BOND`.

So one bad `Acmd` can silence a run permanently with no visible trace in a
normal log. It should zero the frame and log unconditionally. This is the
leading suspect for #56 (§2.5).

### 7.3 Accept device rates instead of failing

Open the device with `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` and put an
`SDL_AudioStream` between the mixer and the device (22050 → device rate). That
removes the whole class of "this machine refused the exact format" failure,
which is the root of bug one. The request cannot simply be relaxed without the
stream, because `g_FrameSize` is sized from the synth rate.

### 7.4 Callback backend

`SDL_QueueAudio` puts a queue in the latency path. An SDL callback pulling from
a ring buffer would cut output latency to roughly one device period plus one
render chunk, removing the watermark term entirely. Only worth doing if ~50 ms
still reads as late in a side-by-side comparison.

### 7.5 Trim the queue back to target

The policy keeps the backlog inside the watermark band, but there is no trim
step. If a burst ever pushes the queue high, it stays there until the device
drains it naturally — adding fixed latency for the rest of the session. A small
backlog cap with a trim would bound worst-case latency after a hitch.
