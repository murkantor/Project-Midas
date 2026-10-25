# Making the audio device rate-agnostic

Briefing for the next agent. This is deliberately a **separate change** from the
wall-clock/relevel pump fix that preceded it — read the "Why separately" section
before merging them.

## The problem

`port/src/audio_pc.c` opens the output device at one exact rate:

```c
want.freq     = 22050;              /* PORT_AUDIO_RATE: both GE banks        */
want.format   = AUDIO_S16SYS;
want.channels = 2;
want.samples  = 256;
dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);   /* allowed_changes = 0 */
```

22050 Hz is the N64's rate (`src/audi.c:20`, `OUTPUT_RATE 0x5622`), and it is
correct as the *synthesis* rate: the wavetables, the resampler ratios and the
sequencer's tempo are all relative to it. Changing it would change how the game
sounds.

The problem is that it is also being used as the *device* rate. On Linux/PipeWire
and macOS/CoreAudio that mostly works. On Windows it very often does not:

- Windows shared-mode mixing is normally 48000 Hz. A client asking for 22050 gets
  one of: WASAPI's automatic sample-rate conversion, an emulated DirectSound or
  WinMM device, or an outright failure that `portAudioOpenAnyDriver()` papers
  over by trying the next driver down the list.
- Whichever it is, there is now a resampler between us and the DAC that we did
  not write and cannot see. Two consequences:
  - **Drift.** Our 22050 and the DAC's 48000 are derived from different clocks.
    The pump's level controller absorbs this, but the buffer has to be deep
    enough to ride it out.
  - **Scheduling.** Windows shared mode mixes in quanta (typically 10 ms), so a
    256-sample (11.6 ms) device period is right at the edge of what the mixer
    will actually deliver. That is a classic source of periodic dropouts that
    sound exactly like "crackle and popping".

Issue #82 is a Windows 11 report with a USB DAC (Sound Blaster X4) and continuous
crackle from the intro. This change addresses the most likely environmental
cause. It was held back from the pacing fix so the two can be told apart.

## Why separately

The pacing fix changes *when* we render; this changes *where the samples go*.
Bundled together, a report of "still crackling" would be ambiguous. Keep this
independent and revertable.

It is also conditional evidence-first: the first thing to read on any #82-style
report is the device line the pump already logs —

```
ge: audio: opened the default device on driver 'X': N Hz, N ch, format 0x...., N-sample buffer (asked 22050 Hz, 2 ch, ...)
```

If `N` is 22050, the OS accepted the exact rate and this change is much less
likely to be the fix (look at pump starvation counters instead). If `N` is 48000
or the driver is `directsound`/`winmm` rather than `wasapi`, this is very likely
the fix. **Ask for that line before spending time here.**

## What to implement

### 1. Open the device at whatever it wants

```c
want.freq     = PORT_AUDIO_RATE;
want.format   = AUDIO_S16SYS;
want.channels = 2;
want.samples  = PORT_AUDIO_DEV_SAMPLES;
dev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                          SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
```

Then branch on `have.freq`:

- `have.freq == PORT_AUDIO_RATE` → today's behaviour, no stream. Do not pay for
  a resampler that is not needed.
- otherwise → create an `SDL_AudioStream` and feed it instead of the device.

Note `have.format` and `have.channels` may also differ once you allow changes;
either allow only frequency (above) or handle all three with
`SDL_ConvertAudio`. Allowing only frequency is the smaller, more reviewable
change and is enough for the Windows case — prefer it.

### 2. Route output through the stream

`portAudioPump()` currently does `SDL_QueueAudio(s_AudioDev, frameBuf, samples*4)`
in two places (the normal path and the forced-frame path). Replace both with a
single helper, e.g.:

```c
/* Returns bytes still owed to the device, or 0 when everything was taken. */
static u32 portAudioQueue(const s16 *buf, u32 samples)
{
    if (s_Stream == NULL) {
        SDL_QueueAudio(s_AudioDev, buf, samples * 4);
        return 0;
    }
    SDL_AudioStreamPut(s_Stream, buf, samples * 4);
    return (u32) SDL_AudioStreamAvailable(s_Stream);
}
```

Critical detail: **`SDL_GetQueuedAudioSize()` no longer means what the pump thinks
it means.** With a stream, the bytes sitting in the stream's internal buffer are
part of the backlog. The pacing policy is a level controller on backlog, so the
depth it is shown must be the sum:

```c
queued = SDL_GetQueuedAudioSize(s_AudioDev)
       + (s_Stream != NULL ? (u32) SDL_AudioStreamAvailable(s_Stream) : 0);
```

If you forget this, the controller sees only the device-side depth, believes it
is starving, and refills forever. This is the one place this change can quietly
break the previous fix — please add a comment saying so.

Use `SDL_AudioStreamGet`/`SDL_AudioStreamFlush` only if you decide to convert
eagerly. The simple, correct shape is: put at the game rate, let SDL's stream
pull through `SDL_QueueAudio`-equivalent, and sum the two depths for pacing.

### 3. Keep the target meaningful across rates

`PORT_AUDIO_TARGET_MS` (40 ms) is a *time* budget, so it should be converted with
the rate actually being synthesised (`PORT_AUDIO_RATE`), not the device rate. It
already is; do not "fix" it. `portAudioPaceMinTarget()` likewise works in bytes at
the game rate. A stream adds its own internal latency, so re-measure and, if the
sum is materially deeper than 40 ms, say so in the log line rather than silently
retuning.

### 4. Log it honestly

Extend the existing open log to state the conversion path, because this is the
line every future audio report hinges on:

```
ge: audio: opened the default device on driver 'wasapi': 48000 Hz, 2 ch, ... (asked 22050 Hz; resampling 22050 -> 48000)
```

### 5. Teardown

`portAudioClose()` must free the stream and NULL it, before closing the device.
It is called on stall-reopen, so a leak here is a slow one that only shows up on
repeated reopens.

## Verification

1. `cmake --build build_macos && ctest --test-dir build_macos` — `audio_pace`
   must stay green. It tests the policy, not the device, so it should be
   unaffected; if it needs changes, the level controller's contract changed and
   that deserves scrutiny.
2. Run the game with `GE_AUDIODBG=1` and confirm the one-second line:
   `queue ... -> ... of <target>` must sit near the target and not climb, and the
   10-second `ge: audio: last N s:` health line must be **absent** (it only
   prints when a counter is nonzero).
3. On a machine where the device is 48 kHz, confirm the log shows the resampling
   path and that the effective latency reported stays in the same ballpark.
4. Force the fallback: `SDL_AUDIODRIVER=directsound` and `=winmm`, and check it
   still opens and paces.
5. Update `docs/port/audio-pump-history.md` and the `KNOWN LIMITATION` comment in
   `port/src/audio_pc.c`'s header block, which both point here.

## Do not

- Do not change `PORT_AUDIO_RATE`. It is the synthesis rate and the game's tempo
  depends on it.
- Do not let the device rate drive the render rate. The device sets the pace
  through the queue level (see `docs/port/audio-pump-history.md`); swapping that
  for "render at `have.freq`" reintroduces the wall-clock/device confusion that
  took three rewrites to untangle.
- Do not drop or duplicate frames to correct depth. Size the frame instead.
