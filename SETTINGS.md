# Settings

Where Project Midas keeps its settings, which ones change what, and how the ones
that look unrelated turn out to affect each other. The audio pump is the worked
example, because it is the clearest case of a setting you change for one reason
having a consequence you feel somewhere else.

---

## 1. Where settings live

`midas.ini`, in the same folder as the game. Plain `key=value` lines in three
sections:

- `[display]` — graphics, pacing, audio volumes, gameplay toggles
- `[input]` — mouse, controller and per-player device settings
- `[data]` — optional, only if you point the game at a cart image yourself

`midas_ini_help.txt` next to it explains every key.

**Binds are not here.** Keyboard and mouse binds live in
`Data/Devices/kbm.gedevice`, one file per device, so deleting `midas.ini` resets
your settings but keeps your controls.

### How they get saved

Change a setting in a menu and it is written immediately — one key changes, the
rest of the file is left alone. The console does the same: `cl_<key> <value>`,
where `<key>` is the ini key (`cl_maxfps 60`, `cl_music_volume 40`).

Two consequences worth knowing:

- **There is no Apply button for most things.** A setting you change is saved
  whether or not the session ends well. Only the folder's VIDEO sheet batches
  its changes behind Apply Changes.
- **Deleting `midas.ini` restores defaults.** The game writes a fresh one on
  next launch.

### What is deliberately *not* saved

Some console switches are diagnostics and reset on every launch, because a
diagnostic you turned on once while reading a bug report should not follow you
into the next session:

- `cl_audio_pump` — the pump block on screen
- `cl_audio_log` — the pump log file
- `cl_hitchms` — slow-frame logging

Their output is the point; the setting itself is not.

---

## 2. The audio pump, and why a graphics setting changes your sound

The N64 had a separate audio thread. On PC a **pump** replaces it: every frame,
the game synthesises however much audio the wall clock says has passed, and hands
it to the sound device.

That "wall clock" part matters. Audio time follows real time, not the frame
count — so music keeps its tempo when the frame rate collapses, instead of
slowing down with it. That is the right behaviour, and getting it wrong is what
made music drag in an older build.

### The catch: buffering is tied to how long a frame takes

The pump keeps a small queue of audio ahead of the device. Its minimum depth is
**one frame interval**: between two pumps the device drains a frame's worth of
audio, so a queue shallower than that would starve on every pump. At 60 fps that
is about 17 ms — unnoticeable. At 1 fps the floor is nearly a second of audio
(a little over 870 ms).

So the frame interval sets your audio latency, and **the frame interval is a
setting**:

| Setting | Effect on the pump |
| --- | --- |
| `maxfps` | The frame interval. A low cap means long frames, a deep audio queue and audible lag — even though the music still plays at the right tempo. |
| `tickhz` | `hw` (hardware pacing) holds each frame for as long as the console would have taken, which on a heavy scene is long. Same effect, and it is the setting Classic Mode turns on. |
| `render_thread` | Changes when the pump runs relative to drawing. Can add jitter to the interval, but does not change the floor. |
| `music_volume`, `sfx_volume` | Straight gain, 0–100%. Saved per profile, not per session. |
| `stereo_pan` | Off collapses stereo to mono. |
| `mute_unfocused` | Silences the game while its window has no focus. Does not stop the pump — audio time keeps running, so unmuting does not jump. |

### When it goes wrong anyway

A *steady* low frame rate is fine: the queue is deep and correct. What hurts is
an **uneven** one — the queue is sized for the interval it has seen, and a
sudden spike drains it. A real example, from a software-rendered run at 2 fps
with 420 ms frame spikes:

```
ge: audio: last 10 s: queue ran dry 2 times, 0 frames over the cap dropped,
    0 frames skipped as invalid | longest gap between pumps 730 ms
    | queue target 63 ms
```

`queue ran dry` is audible as a click. If you see it, find out why the frame
interval is swinging before adjusting anything audio-related — the pump is
usually reporting a rendering problem, not causing an audio one.

**A software renderer is the common cause.** If the window's renderer line says
`Apple Software Renderer` (or the equivalent elsewhere), the frame rate will be
unstable and this is the expected symptom, not a sound bug. `CLI.md` §7.2
explains how to tell.

---

## 3. Classic Mode: a preset that moves the settings above

Classic Mode (the `classic_mode` key, Tab by default, `-classic` on the command
line — `-n64_aa` is the same flag under its old name) is not one setting. It is a
**preset**: turning it on remembers what seven settings are, then forces its own
values; turning it off puts yours back.

The seven, in the order they are stored:

```
low_res  filtering  anisotropy  maxfps  msaa  mipmaps  filter_2d
```

`maxfps` being in that list is why this document has to mention Classic Mode at
all: turning it on switches the game to hardware pacing, which lengthens frames,
which deepens the audio queue. **Classic Mode therefore changes how your audio
feels**, without touching a single audio setting.

Individual parts can be switched off on the CLASSIC sheet, so you can have the
240p look without the pacing — or the pacing without the resolution. A part you
switch off is yours: the preset leaves it alone.

### Two keys, not one: `classic_mode` and `n64_aa`

Until the keys were split, `n64_aa` meant both "Classic Mode is on" and "the
N64's edge blending is on". They are separate now:

- **`classic_mode`** — the preset above, and the only key that forces anything.
- **`n64_aa`** — the AA bit on its own. Set it and nothing else changes: N64
  edge blending, at your resolution, with your filter and your frame rate. This
  is what "Classic Mode without the 240p" used to be unable to express.
- **`classic_n64aa`** — the one part that links them. On (the default), turning
  Classic Mode on sets the bit and turning it off clears it, which is what the
  old single key did. Off, the preset leaves your bit exactly as you set it.

Both are rows in the menus (VIDEO, and the in-game watch's video page). An old
`midas.ini` with `n64_aa=1` and no `classic_mode` is migrated once, on the next
launch: it becomes `classic_mode=1` **and** `n64_aa=1`, because that is what the
one key meant, and a marker (`classic_split=1`) stops the migration from ever
running twice — so turning the preset off later keeps it off.

### If the mode and its settings disagree

The preset is applied when you toggle it. A hand-edited `midas.ini`, a `GE_N64AA`
override, or an older file can therefore leave Classic Mode *on* while the
settings are not classic, and the game says so at startup:

```
ge: video: Classic Mode is on but 2 of its settings are not classic
           (low_res, maxfps) - `cl_classic fix` repairs it
```

In the console:

- `cl_classic` — on or off, and how many of its settings disagree
- `cl_classic fix` — force what should be forced, restore what should be
  restored. Does not remember anything, so it cannot overwrite your saved values
- `cl_classic on` / `cl_classic off` — enter or leave the mode, exactly as Tab does
- `cl_classic_mode 1` / `cl_n64_aa 1` — the same two settings by key. The first
  forces the seven; the second turns on edge blending and changes nothing else

---

## 4. The diagnostic toolkit, and which door to use

There are four ways to look at what the game is doing, and they answer different
questions. Picking the wrong one is how a session gets spent reading a number
that could not have shown the answer.

The tools themselves are documented elsewhere and are not repeated here — this is
the map, not the territory:

- the **console** commands, in `CLI.md` §6 and in `console_help.txt` next to the
  binary;
- the **boot flags** and the **environment variables**, in `CLI.md` §3.2 and §5;
- the **scripts** below, in their own headers, which say what their output can
  and cannot tell you.

### 4.1 Which question, which tool

```
Did a setting change take effect?        the console, cl_<key> with no value
Did it survive a restart?                quit, relaunch, cl_<key> again
Is Classic Mode consistent?              cl_classic
Is my audio actually starving?           cl_audio_pump 1 (on screen)
                                         cl_audio_log 1 + audio_pump_log.py (a file)
Is the frame loop the problem?           cl_hitchms 50, or the f/t and pump fields
Was the file written, and to what?       GE_NOINISAVE=1 for the session, then diff
Did a change break the game's rules?     make port_test
```

### 4.2 The scripts

All of these live in `scripts/port/` and print what they did. None of them touch
your settings without putting them back.

- **`port_test.sh`** — the regression suite, one line per test, then the game
  with diagnostics on. `make port_test` runs it without launching (CI, or a quick
  check); `make audio_test` runs the audio subset; `make demo` runs the whole
  thing windowed for a recording. This is the answer to "did my change break
  something the suite already knows about".
- **`audio_run.sh`** — a time-bounded live run that collects the pump, tick and
  hitch lines into one log, then checks that `midas.ini` and your save file came
  back byte-identical. The least effort way to produce a report worth reading.
  `-g "ARGS"` passes game arguments, so `-g "-audio_pump"` works.
- **`audio_pump_log.py`** — reads `Data/Logs/audio_pump.log` after a run and says
  whether the queue starved, without you reading the numbers yourself.
- **`boot_flag_test.sh`** — live tests for `-n64_aa`, `-audio_pump` and
  `-audio_log`, each in its own short run, with captures written to a dated
  folder. On a multi-monitor machine this is the reliable way to watch them: each
  case sets the display mode it needs from a pristine copy of your `midas.ini`
  and puts the original back byte for byte, because a run with no override opens
  fullscreen on the second display and looks like a black screen from the first.
  `-s eyeball_windowed` is the eyes-on case; `-v` runs the ordinary cases
  windowed as well.
- **`test_the_tests.sh`** — breaks the shipped Classic Mode code deliberately and
  checks that a test fails. It answers "are my tests real", which is a different
  question from "do they pass".

### 4.3 Two rules that keep a diagnostic honest

**Say which renderer you got.** A sandboxed or non-GUI shell falls back to a
software renderer. It draws correctly into captures — so a screenshot looks
perfect — while the window may never composite and the frame rate is far below
anything the machine can really do. Every timing reading from such a session is
meaningless. `CLI.md` §7.2 has the one-line check.

**`GE_NOINISAVE=1` when you are *testing* the save path.** It makes the whole
save path a no-op, which is exactly what you want while experimenting — and
exactly why you must also test without it. A setting verified only under
`GE_NOINISAVE=1` was never verified.
