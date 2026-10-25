# The audio pump: four designs in six days

Working note for issue #82 ("Audio popping/crackling and slow music on 1.02b").

This is the history of `port/src/audio_pc.c`'s frame driver — the thing that
replaces the N64 audio thread — why it was rewritten three times, what each
rewrite was trying to fix, and which of those designs is actually in the build
the reporter is running.

Read this before touching the pacing code. The file has been rewritten more
often than it has been tested, and the symptom in #82 (continuous choppiness
from the intro, plus music that sounds slow) is a pacing symptom, not a mixing
symptom.

---

## 0. Why a pump exists at all

On N64 the audio work is split across three parties:

- **The main thread** runs the AL *library*: `alSynNew`, the sound player, and
  the CSP music sequencers. These build a parameter graph.
- **The audio thread** (`amMain` in `src/audi.c`) calls `alAudioFrame()` once
  per field. That walks the AL clients, emits an `Acmd` display list for the
  RSP, and hands the samples to the AI DMA.
- **The RSP** (`aspMain` microcode) executes the list: ADPCM decode, resample,
  envmix, interleave, and writes s16 stereo into the output buffer.

On PC nothing calls `alAudioFrame` and nothing executes `aspMain`. So
`audio_pc.c` supplies both halves:

1. `portAcmdRun()` — an interpreter for the `aspMain` command set, using the
   `include/PR/abi.h` encodings directly (same headers the library emits
   through, so no byte-order questions).
2. `portAudioPump()` — the frame driver. This is the part with a history.

One property of the architecture drives every design decision below:

> `alAudioFrame()` is the **only** thing that advances game audio time. The SFX
> player's event queue, the sound-state list and the three music sequencers all
> advance inside it, and they advance in *rendered samples*
> (`src/libultra/audio/synthesizer.c:143-227`: `curSamples += nOut`, and client
> handler returns are converted with `_timeToSamplesNoRound`).

So the pump is not just "fill the sound card" — it is the game's audio clock.
Any policy that stops the pump freezes audio state; any policy that runs it too
fast burns the game's audio timeline ahead of real time. Every generation
below got one of those two wrong in a different way.

---

## 1. Timeline

| Date | Commit | Author | What it is |
| --- | --- | --- | --- |
| 2026-10-18 | `718a122` | Murkantor | **Gen 1**, initial public release. Queue-level driven. |
| 2026-10-21 | `7de0c0f` | jcorneli | **Gen 2**, "keep the synthesizer running without a sound device". Wall clock is the authority. |
| 2026-10-22 | `3b3b888` | jcorneli | **Gen 2.5**, latency work on branch `audio-latency`. Extractable pacing policy + tests. |
| 2026-10-23 | `beb698a` | Murkantor | Merge of PR #69 ("No Sound fix"). **Contains only `docs/port/abi-and-portability.md`.** |
| 2026-10-23 | `9062f9f` | Murkantor | **Gen 3**, "Project Midas 1.0.2b". Full pump rewrite. **This is what #82 runs.** |

The single most important line in that table is `beb698a`. PR #69 is described
in the changelog as fixing crackling and audio start-up, but the merge commit
carries no audio code: `port/src/audio_pc.c` in `beb698a` is byte-identical to
Gen 1 from `718a122`. The merge message says so directly:

> The audio changes are carried by 1.0.2b's reworked audio pump, which is based
> on this PR.

So Gen 2 was never in `main`. Gen 3 landed on top of Gen 1 **57 seconds after**
the merge commit (09:47:24 → 09:48:21 on 2026-10-23, same author): the rewrite
was already prepared, and the merge was bookkeeping for the docs file. In that
commit `port/src/audio_pc.c` went from 768 lines to 1211 (+490/−41 against
Gen 1). That is the answer to "I don't know in what way the fix got harvested"
— it was harvested as *ideas*, not as code, and the pacing authority was
inverted in the process.

---

## 2. Generation 1 — queue level decides (718a122)

```c
#define PORT_AUDIO_QUEUE_LOW (4 * 1024)
#define PORT_AUDIO_QUEUE_MAX (16 * 1024)
want.samples = 512;

for (guard = 0; guard < 8; guard++) {
    u32 samples = g_FrameSize;              /* 736 */
    if (SDL_GetQueuedAudioSize(s_AudioDev) >= PORT_AUDIO_QUEUE_LOW) break;
    alAudioFrame(cmdList, &cmdLen, frameBuf, samples);
    portAcmdRun(cmdList, cmdLen);
    SDL_QueueAudio(s_AudioDev, frameBuf, samples * 4);
}
```

**Policy:** refill with whole 736-sample frames (33.4 ms each) until the SDL
queue holds at least 4 KB (46.4 ms).

**Why it was wrong:** two independent failures.

- *Latency.* The queue floor is a direct addition to the delay between an
  event and a speaker. 4 KB queue + one render quantum + a 512-sample device
  period = 63–103 ms. That is roughly the length of a weapon-lowering
  animation, which is why the reload sound landed after the weapon left the
  screen on hardware comparison.
- *Burst-following.* Because refills are triggered by the device's pull bursts,
  the moment a new sound's first sample gets rendered follows the device's
  consumption pattern, so identical events did not start at identical times.
  That is the "uneven gunfire" complaint (upstream issue #51).
- *Silence on a dead sink.* If the device opened but never consumed, the queue
  pinned at the cap and the pump stopped — which froze the game's audio clock.
  That is the "no sound" and "SFX queue saturated, thousands of events dropped"
  family of reports.

Gen 1 also opened exactly one device with no driver fallback and no retry, so a
single `SDL_OpenAudioDevice` failure meant permanent silence.

---

## 3. Generation 2 — wall clock is the authority (7de0c0f)

```c
/* Advance the audio clock by however much wall time has passed. */
s_ClockOwed += (u64) elapsed * PORT_AUDIO_RATE;   /* capped at 100 ms/pump */

/* Render when the clock owes a whole frame, or when the queue is running low. */
if (s_ClockOwed < frame && !(s_AudioDev && queued < PORT_AUDIO_QUEUE_LOW)) break;
```

**Policy:** the wall clock is the pacing authority. A live DAC is only allowed
to *add* catch-up refills as a refinement. Neither branch can be blocked by the
device: with no device, or a stalled one, the mixer still runs and the output is
discarded, so game audio time keeps advancing.

**What it fixed:** the silence family. Device state could no longer stop the
synthesizer, and a stalled sink was detected (`queue >= MAX` and byte-identical
for 120 pumps) and reopened.

**What it kept:** the 4 KB / 736-sample / 512-device budget, so it fixed
stability and left the latency untouched. It also kept the burst-following
behaviour, since queue-low catch-up still fired on the device's schedule.

**What it got wrong:** nothing in steady state, but the two authorities
overlap. If a device consumes even slightly slower than 22050, the clock branch
keeps rendering and the queue creeps to the 16 KB cap, where frames are
rendered and thrown away — an audible glitch each time.

---

## 4. Generation 2.5 — latency, and a policy you can test (3b3b888, branch `audio-latency`)

Not in `main`. Worth reading anyway, because it is the only generation that
made its rules testable.

Changes:

- Queue watermark 4096 → 2048 B (23.2 ms), render quantum halved to 368 samples
  (16.7 ms, throughput unchanged at two quanta per pump), device period
  512 → 256 (11.6 ms). Worst-case edge ~52 ms, down from ~103 ms.
- **Exclusive** pacing authority: device draining → top up to the watermark and
  stop; no device or stalled → wall clock paces. No overlap, so the backlog
  cannot drift up to the cap.
- Stall detection measures *consumption* (bytes left last pump minus bytes
  present now) instead of change, because at 147 Hz pump rate and 11.6 ms device
  periods a healthy queue is legitimately byte-identical across pumps.
- The decisions moved into `port/src/audio_pace.c` + `port/include/audio_pace.h`
  as pure functions (no SDL, no Ultra64 headers, no globals, no clock reads),
  exercised by `port/tests/audio_pace.c` under `ctest -R audio_pace`.

That last point is the actual contribution of this branch. A pacing policy is a
handful of comparisons, and every generation so far has shipped those
comparisons untested.

---

## 5. Generation 3 — the game field is the authority (9062f9f, shipped as 1.0.2b)

```c
/* One audio frame per game field. */
fields = currentFrameCounter - s_LastField;      /* 0..16, else 0 */
due = fields;

for (guard = 0; guard < 20; guard++) {
    if (SDL_GetQueuedAudioSize(s_AudioDev) < s_Target / 2) {
        /* below the floor: an extra frame ahead of the field frames */
    } else if (due > 0) {
        due--;
        if (s_TrimFrames > 0) { s_TrimFrames--; continue; }  /* drop this frame */
    } else break;
    samples = portAudioFrameSamples();           /* 367.5 avg, multiple of 16 */
    ...
}
```

Config: `PORT_AUDIO_TARGET 3584` B (40.6 ms), `TARGET_MAX 6144`, `TRIM_MS 500`,
`PORT_AUDIO_DEV_SAMPLES 256`, `QUEUE_MAX 16 KB`, PAL-aware `FIELD_HZ`.

**Policy:** the number of `alAudioFrame()` calls is tied to the *simulation*
field counter (`src/game/frametiming.c:964`, advanced by `updateFrameCounters`),
not to the wall clock and not to the device. The device is a top-up trigger
(`queue < target/2`) and the clock is only a fallback for a missing or stalled
device. A trough tracker over a 500 ms window computes how many field frames the
queue sat above target, and **skips that many whole frames**.

**What it fixed, genuinely:**

- Event-start timing no longer follows the device's pull pattern (#51), because
  frames are delivered on a fixed field cadence.
- The SFX player's onset quantisation is 16.7 ms rather than 33.4 ms, since
  `snd.c:368` uses `AL_USEC_PER_FRAME_30FPS` per handler call and the handler
  is invoked at frame boundaries. This is a real improvement over Gen 2.
- Device handling got much better: multi-driver fallback
  (`portAudioOpenAnyDriver`), 32-bit PipeWire plugin dirs, retry with backoff
  from 2 s to 30 s, stalled-device reopen, and a health log every 10 s.

**What it lost against Gen 2:** the wall clock is no longer the authority, so
audio delivery is now coupled to the *simulation* loop. If the game loop stalls
— asset decode, stage load, the intro swirl, a window drag, alt-tab, a long
frame — no audio is delivered for that period, the device drains and plays
silence, and playback resumes with a discontinuity. That is a click. Repeat it
often enough and it is continuous crackle plus music that runs behind real time.

**And one structural regression:** the trough trim fixes an over-full queue by
**dropping whole rendered frames** (up to 16.7 ms of audio each, discarded after
being synthesised). The N64 never did this. `amHandleFrameMessage`
(`src/audi.c:529`) sized each frame from the AI's remaining buffer instead:

```c
info->frameSamples = ((g_FrameSize - (osAiGetLength() >> 2)) + 16 + EXTRA_SAMPLES) & ~0xf;
```

which is a proportional controller: frame *length* absorbs the error, so the
correction is sub-frame and inaudible, clamped by `g_MinFrameSize` /
`g_MaxFrameSize`. The port has no equivalent: `portAudioPump()` calls `alAudioFrame()` directly
and never enters `amHandleFrameMessage`, and `port/src/libultra_shim.c:1216`
stubs `osAiGetLength()` to 0, so the servo would have nothing to read even if it
were called. Every port generation has therefore reinvented pacing at frame
granularity instead of sample granularity.

---

## 6. Comparison

| | Gen 1 | Gen 2 | Gen 2.5 | Gen 3 (shipped) | Gen 4 (this fix) |
| --- | --- | --- | --- | --- | --- |
| Render trigger | queue < 4 KB | clock owes a frame, or queue < 4 KB | device-paced, clock if no device | game field counter, or queue < target/2 | **refill toward target; clock only with no device** |
| Frame length | 736 (33.4 ms) | 736 (33.4 ms) | 368 (16.7 ms) | 367.5 avg, per field | deficit, aligned, capped at 1024 |
| Queue target | 46.4 ms | 46.4 ms | 23.2 ms | 40.6 ms (rises to 69.7 ms when dry) | 40 ms (rises to 116 ms when dry) |
| Device period | 23.2 ms | 23.2 ms | 11.6 ms | 11.6 ms | 11.6 ms |
| Rough latency | 63–103 ms | 63–103 ms | 35–52 ms | 52–69 ms | ~40–52 ms |
| Freezes audio time on a dead sink | yes | no | no | no | no |
| Coupled to simulation stalls | no | no | no | **yes** | no |
| Absorbs device clock drift | no | no | partly | yes (by dropping frames) | yes (by refill depth) |
| Over-full queue handled by | n/a (cap) | discard rendered frame | n/a (exclusive authority) | **discard whole field frame** | never over-fills |
| Multi-driver / retry / reopen | no | retry + reopen | retry + reopen | retry + backoff + all drivers | same as Gen 3 |
| Pacing rules tested | no | no | yes (`audio_pace.c`) | no | **yes (`audio_pace.c`, 23 cases)** |
| In `main` | yes | no | no | yes (1.0.2b) | this branch |

---

## 7. The design space, stated once

Every generation has been an answer to one question: *what is allowed to decide
whether the mixer runs this iteration?* Only four answers exist, and the port has
now tried all of them:

1. **The queue** (Gen 1). Simple, self-limiting, and wrong on latency and on
   event-start timing, because it inherits the device's pull pattern.
2. **The wall clock** (Gen 2, Gen 4 fallback). Correct for audio time when there
   is nothing to listen to; cannot by itself hold a queue depth (see the
   constant-of-integration trap above).
3. **The device** (Gen 2.5, Gen 4 primary). Correct for latency, backlog and
   drift; useless when the sink is absent or wedged, hence the clock fallback.
4. **The simulation clock** (Gen 3). Correct for frame-locking events, but it
   makes audio delivery hostage to the game loop's worst frame.

The useful framing is that these are not four candidates for one decision. They
are four *different* decisions that the code keeps collapsing into one:

- **Audio time** — how fast the game's audio clock advances — is, on the N64,
  the DAC's own rate: `amHandleFrameMessage` sized each frame as "refill what
  drained", so the render rate *was* the device's consumption rate. The wall
  clock is the correct stand-in only when there is no device.
- **Buffer safety** — never let the queue hit zero, never let it grow without
  bound — belongs to the queue, and is a level controller on it.
- **Latency** — how much backlog is allowed to sit there — is a number, not a
  policy: the queue floor.
- **Delivery granularity** — how coarse the correction is — should be *samples
  within a frame*, not *frames dropped*.

Gen 2.5 got the scheduling of that split right and the numbers half-right. Gen 3
moved the first decision to the simulation clock, which is the one thing the
first three generations agreed should not happen.

### The tempting wrong answer, for the record

The fifth design (Gen 4, `audio-pump-wallclock`) first tried to make the wall
clock the sole authority *and* steer the queue depth from the same budget: credit
wall time, then apply a bounded bias toward a target depth, charging the bias
against the clock. It compiles, it passed fifteen unit tests including a
closed-loop simulated device, and it starves on real hardware.

The reason is arithmetic, not a bug: if total rendered is forced to equal wall
time, and the device also consumes wall time, then the queue level is a constant
of integration. A bounded bias can change *when* samples arrive but never the
running total, so the depth never converges to the target. Measured with
`GE_AUDIODBG=1` on CoreAudio: queue cycling 1216-3584 bytes against a 10240 target
that it had ratcheted to, 24 dry pumps in the first 10 s window.

The fix was to stop treating the two duties as one decision, which is what the
N64 code did all along:

- **A live device sets the rate.** Refill toward the target depth. Render rate =
  device rate, queue stable, drift absorbed. This is `amHandleFrameMessage`.
- **The wall clock sets the rate only when there is no usable device**, and those
  samples are discarded.

After the change the same measurement reads: queue steady at 3556 bytes against a
3520 target, and no health line at all — zero dry pumps, zero over-cap drops, zero
skipped frames.

Two further traps found the same way, both invisible to the tests that existed:

- Rounding the refill **down** to the 16-sample alignment loses a few samples on
  every pump. At 300 fps that is ~11% short and the queue drains regardless of the
  target. Round up; the "at or above the mark" early return is the hysteresis.
- A queue target **below one pump interval** starves by construction: between two
  pumps the device drains that interval, and refilling stops at the target. At
  20 fps a 40 ms target guaranteed a dry pump every pump. The effective target is
  `one interval + a device period + whatever the ratchet has learned`.

  *(That last term was originally `max(target, one interval + a device period)`.
  See §9: the maximum made the ratchet inert below ~8.6 fps.)*

Both now have tests named after them.

---

## 8. What this means for #82

The reporter's symptom set is:

- crackle/pop continuously, from the intro on — not only while firing;
- music that "feels slowed down";
- Windows 11, Ryzen 7 7800X3D, RTX 4090, Sound Blaster X4 (USB);
- it does not reproduce on the maintainer's macOS machine.

"From the intro on" rules out anything event-driven (gunfire cadence, sound
pool sizing, per-weapon volume ramps) — those cannot affect the Rareware logo.
It also rules out the mixing path: a wrong ADPCM decode or a bad resample table
would sound wrong, not choppy. What is left is a *delivery* problem: the device
is being starved, or is being fed at the wrong rate, or is discarding frames.

Candidate mechanisms, in the order worth checking:

1. **Device opened at a rate the OS does not really run.** `want.freq = 22050`
   with `allowed_changes = 0`. On Windows 11 the shared-mode mix format is
   almost always 48000 Hz, so this either fails over to a fallback driver
   (DirectSound/WinMM, both of which route through the Windows mixer with their
   own emulation) or is opened with WASAPI's automatic conversion. A 256-sample
   (11.6 ms) buffer underneath that layer is well inside the range where mixer
   scheduling shows up as periodic dropouts. The log line
   `ge: audio: opened the default device on driver 'X': N Hz, ...` settles this
   in one line.
2. **Queue starvation from simulation stalls** (Gen 3's coupling). Every stalled
   frame is a silence gap and a click; `s_StatDry` and `s_StatMaxGapMs`
   (`longest gap between pumps N ms`) count it.
3. **Trim dropping whole field frames.** Each drop is 16.7 ms of synthesised
   audio thrown away mid-stream, i.e. a step discontinuity. If the device is
   consuming slower than the pump renders (a real possibility when the OS is
   resampling), the trim enters a steady skip pattern — which is exactly
   "continuous crackle" plus "music slightly behind".
4. **Force-pump bursts at track transitions.** `portCSPWaitStopped`
   (`src/music.c:44`) calls `portAudioPumpForce()` up to 512 times while a track
   stops. Each call queues a full frame regardless of pacing. The queue slams
   into the 16 KB cap, the excess is discarded, and the trim then skips frames
   to get back to target. The intro is precisely where tracks stop and start, so
   this is a candidate specifically for *intro-onset* choppiness.

These are not mutually exclusive, and 1 and 3 reinforce each other.

### Diagnostics worth having before changing a line

Everything needed is nearly in place. What to ask the reporter for, or to add:

- `Data/Logs/ge_direct.log`, the first `ge: audio:` block: driver name, `have`
  rate/channels/format/samples, and the device list.
- The periodic health line — `queue ran dry N times, N frames over the cap
  dropped, N frames skipped as invalid | longest gap between pumps N ms | queue
  target N ms`. If it never appears, the pump is not reporting starvation and
  the problem is below it (device layer). If it appears with a high dry count,
  it is starvation.
- Two one-line experiments that need no rebuild: force the driver
  (`SDL_AUDIODRIVER=wasapi`, then `directsound`), and drop `maxfps` to 60 to see
  whether the crackle tracks the tick rate.
- Set the X4 to 48000 Hz in Windows sound settings, or test with the
  motherboard output, to separate "USB DAC dislikes 22050" from "the port's
  pacing is fragile".

### Fix directions, in order

1. ~~**Stop dropping rendered frames.**~~ **DONE (this branch).** The level
   controller refills toward a target and never discards a rendered frame, and a
   correction is a few samples of frame length, never a whole field.
2. ~~**Give the queue a real safety net.**~~ **DONE (this branch).** The target
   ratchets up on an underrun and eases back only in a clean 10 s window, and the
   effective target is floored at one pump interval plus a device period, with
   the learned headroom ADDED to that floor (see §9).
3. ~~**Land the tested policy.**~~ **DONE (this branch).** `audio_pace.c` and
   `port/tests/audio_pace.c` are in the build behind `GE_PORT_TESTS=ON`, with
   cases named after the two bugs found on hardware.
4. **Make the device layer rate-agnostic.** Open with
   `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` and insert an `SDL_AudioStream`
   (22050 → device rate), so the sink stops depending on the OS resampler and on
   whichever fallback driver won. Full briefing in `docs/port/rate_prompt.md`;
   originally scoped in `docs/port/pr-silent-audio.md:170`. Deliberately separate
   from this change so a crackle report can tell the two apart.
5. **Give forced frames a real pacing rule.** `portAudioPumpForce()` currently
   renders a field frame that is queued only when the queue is shallow, so a
   track-stop burst advances the sequencer's musical time without delivering it.
   Bounded by the stop, and pre-existing, but it is the last known hole: the fix
   is to make `music.c`'s spins wall-clock paced rather than iteration-counted
   (`MUSIC_WAIT_STOPPED`, `portMusicKillAll`, up to 512 and 128 spins).
6. **Expose a latency/stability knob** (`midas.ini`, e.g. `audio_buffer_ms`) so
   a USB-DAC user can trade 40 ms for 80 ms today while the real cause is found.
   This is a workaround, and it should be labelled as one.
7. **A real X11/Wayland or WASAPI-exclusive path** would sidestep shared-mode
   mixer quanta entirely, but it is a much larger change than #82 justifies.

---

## 9. The ratchet was inert below ~8.6 fps

Found 2026-10-25 while chasing a report of music hitching more on a slow
renderer, not by a test. It is worth recording because it is §7's own trap in a
later fix, and because every test that existed still passed.

**What was wrong.** §7 records that a target below one pump interval starves by
construction, and fixes it with a floor: `max(target, one interval + a device
period)`. Separately, `portAudioPaceTargetStep` ratchets the target up by 1024
bytes per underrun, to a ceiling of `TARGET_MAX` = 10 KB = **116 ms**, easing
back only in a clean window.

The two are in different units of thought. The floor is a *frame-rate* quantity
and reaches ~870 ms at 1 fps; the ratchet is an *absolute* byte budget of
116 ms. So `max(ratcheted, floor)` meant that at any frame rate below about
8.6 fps the floor won unconditionally and **every underrun the pump learned from
was discarded on the next pump.** The safety net in §8's item 2 could not deploy
where it was needed.

The practical effect: the queue sat at exactly one interval plus the 12 ms device
period, i.e. a 12 ms tolerance for the next frame being slower. At 1 fps
(870 ms intervals) that is 1.4% of headroom, and ordinary frame-to-frame
variance walks through it.

**What is true now.** `portAudioPaceEffectiveTarget` (`audio_pace.c`) returns
`max(configured, interval floor) + learned`, so the ratchet's headroom lands on
top of the floor instead of competing with it. Ordinary frame rates are
unchanged — there the configured target is the base and the learned term is what
moves — and single-digit rates get the net §8 promised, bounded by
`TARGET_MAX - TARGET` (76 ms).

**Measured, without a device.** The simulations in `port/tests/audio_pace.c` all
pumped at a *constant* interval, which is why none of them saw this: a pump that
measures the elapsed interval and refills for it cannot starve on an even
cadence. The new `simulate_jitter` drains the elapsed interval **before** the
refill, so an interval longer than the one before it can starve the queue. At
1 fps with every fifth pump 70 ms longer, over 120 s, driving both policies from
the same ratchet:

```
legacy  max(ratcheted, floor):  27 dry events, peak queue  952 ms
now     floor + learned:         5 dry events, peak queue 1011 ms
```

Five, not zero, because the ratchet learns 11.6 ms per underrun and the first few
spikes are surprises. That is the mechanism working as designed rather than
suppressed.

**Measured live** (Apple Software Renderer, Dam, 1 fps, `cl_audio_log 1`): after a
`dry` at t=43.5 s the target climbed `1041 → 1053 → 1064 ms`, one 1024-byte step
per pump. Under the old rule it stayed pinned at `interval + 12` indefinitely.

**Cost, stated plainly.** ~59 ms of extra latency at 1 fps, growing to the 76 ms
cap. §7's own note and `tools/console_help.txt` prefer a backlog to a gap at
single-digit frame rates; this spends latency to buy continuity, and only where
the frame rate is already too low for the difference to be heard.

**Not fixed.** A spike larger than the whole learned ceiling (a stage load, a
compile hitch) still empties the queue; one was observed at the transition into
Dam. No bounded ratchet can cover an unbounded spike, and covering it would mean
holding an unbounded backlog.

---

## Appendix — line references (dated; they have drifted)

**Read these as of the generation each section describes, not as of the current
tree.** Every line number below was correct when written and most are now
hundreds of lines short, because `port/src/audio_pc.c` has been edited since by
this branch and the branches before it. A spot check at `ef8a6c6`: the pacing
config is at 79-117, `portAcmdValidate` at 465, `portAudioHealth` at 1006,
`portAudioFieldSamples` at 1243 and `portAudioPump` at 1258. The symbols are the
stable reference; the numbers are kept because they locate the generation.

- `port/src/audio_pc.c:79-104` — pacing config (`RATE`, `TARGET_MS`,
  `TARGET`, `TARGET_MAX`, `CATCHUP_MS`, `DRAIN_MS`, `DEV_PERIOD_MS`).
- `port/src/audio_pc.c:376-411` — `portAcmdValidate`, the whole-frame skip that
  plays silence on a bad address.
- `port/src/audio_pc.c:923-950` — `portAudioStalled`, reopen of a dead sink.
- `port/src/audio_pc.c:954-985` — `portAudioHealth`, the counters quoted above.
- `port/src/audio_pc.c:998-1008` — `portAudioFieldSamples`, forced-frame size.
- `port/src/audio_pc.c:1013-1280` — `portAudioPump`.
- `port/src/audio_pc.c:1080` — `portAudioPaceMinTarget(dt)`, the pump-interval
  floor that stops a shallow target from starving.
- `port/src/audio_pc.c:1283-1290` — `portAudioPumpForce`, and the `GE_AUDIODBG`
  one-second trace just above it.
- `port/src/audio_pace.c` — the policy: `portAudioPaceRefill` (device-paced),
  `portAudioPaceFrame` (both paths), `portAudioPaceMinTarget`, the target ratchet
  and the drain watchdog. `port/include/audio_pace.h` documents why.
- `port/tests/audio_pace.c` — 23 cases, including the two hardware-only bugs,
  single-digit frame rates down to 1 fps.
- `src/audi.c:336-360` — `amCreateAudioManager`, `g_FrameSize = 736`.
- `src/audi.c:529` — the vanilla buffer-feedback frame sizing that the port lost.
- `src/libultra/audio/synthesizer.c:143-227` — `alAudioFrame`, the audio clock.
- `src/libultra/audio/seqplayer.c:326` — sequencer time advance.
- `src/snd.c:368` — `AL_USEC_PER_FRAME_30FPS`, the SFX onset quantisation.
- `src/game/frametiming.c:964-1012` — `updateFrameCounters`, Gen 3's pacing input.
- `src/music.c:44-58` — `portCSPWaitStopped`, the force-pump spin.
- `port/src/libultra_shim.c:1216` — `osAiGetLength()` stubbed to 0.
- `port/src/pcsched.c:327-365, 1004-1032` — the retrace-duty gate that calls the
  pump once per field.
