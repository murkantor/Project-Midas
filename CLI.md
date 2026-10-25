# Starting Project Midas from a command line

How to launch the game, how to get hardware (N64) frame pacing, which command-line
arguments exist, and which environment variables are worth knowing.

Every claim here was read from the source, and the ones that could be checked
without running the game were checked. Nothing in this file is a guess: where a
statement is derived from code rather than observed, it says so. Where a variable
exists but is not useful to a player, it is not listed — see §7 for the full set.

---

## 1. Quick start

```bash
cd build_macos
./ProjectMidas
```

The binary looks for your cart image itself (see §4), and it looks **next to the
executable**, not in the current directory, so `cd`-ing away from `build_macos`
does not break it — but the `Data/`, `Mods/` and `*.txt` files it writes do live
beside the binary.

A build for another platform has the same name without the extension on Linux and
macOS, and `ProjectMidas.exe` on Windows. See `BUILD.md`.

---

## 2. Hardware pacing (`tickhz=hw`) — the three ways

`tickhz=hw` runs the classic 60 Hz simulation and holds each frame for the number
of video fields the N64 would have needed to draw it. It is the mode that
reproduces the console's frame rate, its slowdown in heavy views, and its stepped
motion. What it costs and why: `docs/port/n64-hw-pacing.md`.

### 2.1 The reliable way — the settings file

There is **no environment variable for this.** Set it in `midas.ini`, which lives
beside the executable:

```ini
[display]
maxfps=-1
```

Then launch normally:

```bash
cd build_macos && ./ProjectMidas
```

`maxfps=-1` is the menu's "Hardware" Max FPS and is equivalent to `tickhz=hw`
(`videoCoupleTickToMaxFps` in `port/src/video.c`: `maxfps=-1` calls the `tickhz`
setter with `"hw"` through the same path the ini uses). Writing

```ini
[display]
tickhz=hw
```

directly does the same thing and skips the derivation. Either line works; if both
are present and disagree, `maxfps` is the one the menu owns.

From a shell, without editing by hand:

```bash
cd build_macos
sed -i '' 's/^maxfps=.*/maxfps=-1/' midas.ini   # macOS/BSD
sed -i    's/^maxfps=.*/maxfps=-1/' midas.ini   # GNU sed (Linux)
./ProjectMidas
```

### 2.2 At runtime — the console

Hardware pacing can be entered without touching the file first. Press `` ` `` for
the console and enter:

```
cl_maxfps -1
```

(`cl_fps -1` is the same command; `fps` is an alias for `maxfps`.) The engine's
setting is named `maxfps`, so `cl_maxfps` is the correct form — `cl_maxfps 120`
is a 120 fps cap and a 120 Hz tick, `cl_maxfps -1` is hardware pacing, `cl_maxfps 0`
is uncapped.

The change is written back to `midas.ini`, so it survives the session.

### 2.3 From the environment, for scripted runs

The closest thing to "start in hw mode with a variable" is to run the console
command at the first rendered frame:

```bash
cd build_macos
GE_CONCMD="cl_maxfps -1" ./ProjectMidas
```

`GE_CONCMD` takes one or more console lines separated by `;` and runs them once,
without a keyboard. It fires at the **first rendered frame**, so the first frame
or two are not paced yet — from `conEnvHarness` in `port/src/port_console.c`
(read, not observed).

### 2.4 The trap: `GE_TICKHZ=hw` does not do this

`GE_TICKHZ` is parsed with `strtol`, and `strtol("hw")` is `0`. So

```bash
GE_TICKHZ=hw ./ProjectMidas     # WRONG
```

does not enable hardware pacing. It silently sets an **uncapped** tick rate, which
is a different mode with different behaviour. Nothing warns you: the log says
`ge: sim tick rate = 0 Hz UNCAPPED (from GE_TICKHZ)`, which is honest but easy to
miss.

`GE_TICKHZ` also cannot select hw pacing by any other spelling — negative values
clamp to 60. Use §2.1–§2.3. (`portTickHz`, `src/game/frametiming.c`; read, not
observed.)

### 2.5 What hw mode costs, before you turn it on

- **Entering some stages is slow on purpose.** The Dam stage load is priced at
  ~8.7 s and the game holds for all of it with nothing drawn — a white screen.
  That is fidelity, not a hang. See `docs/port/n64-hw-pacing.md` §5.
- **Audio latency rises when frames are slow.** The pump must bridge a whole frame
  interval, so at 15 fps that is ~67 ms of buffer, and single-digit frame rates
  buy a larger backlog deliberately rather than crackle.
- **It is only active inside a stage.** Menus, demos and the watch menu run
  without it by design, so "hw mode is on" and "a hold was applied" are different
  statements.

---

## 3. Command-line arguments

Only two flags exist, plus drag-and-drop. `src/` and `port/` were searched for
`argv` uses: `main.c` takes `argv[0]` for the root directory and hands the rest to
`romdataInit`, and nothing else reads the argument vector.

| Argument | Meaning |
| --- | --- |
| `--rom-file <path>` | Load this cart image. |
| `--rom-file=<path>` | The same, one argument. |
| `--lang j` / `--lang jp` | Japanese text. `--lang e` / `--lang en` selects English. |
| `<path>` (first argument, not starting with `-`) | Drag-and-drop onto the executable; treated as the cart image. |

```bash
./ProjectMidas --rom-file ~/carts/goldeneye.u.z64
./ProjectMidas --rom-file ~/carts/goldeneye.u.z64 --lang jp
```

There is **no** `--help` and no `--version`. An unrecognised flag is not an error —
it is ignored, and the ROM falls through to the next source in §4.

---

## 4. Where the cart image is found

Tried in this order; the first that loads wins (`romdataInit`,
`port/src/romdata.c`):

1. the first argument, if it does not start with `-` (drag-and-drop)
2. `--rom-file`
3. `GE_ROM=<path>`
4. `midas.ini` → `[data] rom=`
5. a scan of `Data/` beside the executable
6. a legacy `baserom.u.z64` next to the repository (development only)

Accepted formats and the ways to point it elsewhere without copying: see
`Data/README.txt` in a shipped build. If nothing loads, the log says which
sources were tried.

---

## 5. Environment variables worth knowing

These are ordinary environment variables, set on the command line for one run.
They are **not** `midas.ini` keys, and none of them is saved.

### 5.1 Running without a keyboard

| Variable | Effect |
| --- | --- |
| `GE_STAGE=<name>\|<number>` | Load a stage directly instead of the front end. Names: `dam`, `facility`, `runway`, `surface`, `bunker1`, `silo`, `frigate`, `surface2`, `bunker2`, `statue`, `archives`, `streets`, `depot`, `train`, `jungle`, `control`, `caverns`, `cradle`, `aztec`, `egypt`, `citadel`, `complex`, `temple`, `cuba` (case-insensitive), or a numeric level id. |
| `GE_FRAMES=<n>` | Run `n` pumps (frames), print a frame line, and exit 0. Unset or `0` runs forever. |
| `GE_SKIPINTRO=1` | Skip the intro sequence. |
| `GE_CONSOLE=1` | Open the console at startup. |
| `GE_CONCMD="<line>[;<line>…]"` | Run console lines once at the first rendered frame. |
| `GE_NOINPUT=1` | Ignore input devices. |
| `GE_WINDOW=<WxH>` | Override the window size, and make the run write nothing to `midas.ini` — the capture/harness switch. `GE_WINDOW=1280x720` for a specific size; any value at all suppresses ini writes. |

`GE_STAGE=dam GE_FRAMES=900` is a ~4 minute run on a software renderer, because
Dam frames cost ~890 ms there. Bounding by **time** and stopping on a log pattern
is usually cheaper than a fixed frame count — that is what
`scripts/port/audio_run.sh` automates.

### 5.2 Seeing what the timing is doing

| Variable | Effect |
| --- | --- |
| `GE_HITCHMS=<ms>` | Log every frame slower than this as `ge: hitch:` (`0` = off). `1` logs every frame. |
| `GE_HWDBG=1` | Print the hw-pacing model's price once a second, and the stage-load price. `2` prints every load as charged. |
| `GE_TICKDBG=1` | Once a second, print the spread of the measured per-tick delta. |
| `GE_TITLESTATS=1` | Frame/tick numbers in the window title. |
| `GE_CONCMD="cl_audio_log 1"` | Write `Data/Logs/audio_pump.log`, one line a second. |

### 5.3 The hardware-pacing model

Only meaningful with hw pacing on (§2). Documented at length in
`docs/port/n64-hw-pacing.md` §6.

| Variable | Effect |
| --- | --- |
| `GE_HWSCALE=<f>` | Multiply the RDP model's prediction. `1.0` = the fit. Above ~2 it saturates the model's own 6-field cap. |
| `GE_HWLOAD=0` | Turn load charges off. |
| `GE_HWLOAD_SCALE=<x>` | Scale the cart-DMA and inflate charges. |
| `GE_HWCAL=1` | Log the N64's recorded demo timing beside the port's estimate, for calibration. |

### 5.4 Tick rate and the stall cap

| Variable | Effect |
| --- | --- |
| `GE_TICKHZ=<hz>` | Simulation tick rate. `60` is classic, `120`/`144` raise it, `0` is uncapped (1000 Hz ceiling). **Not a way to get hw pacing** (§2.4). |
| `GE_TICKMAXSTEP=<fields>` | The stall cap: most fields one tick may be credited. Default `8`; `0` is uncapped. Setting it explicitly also stops the hw-pacing hold from raising the cap, which is the point — it is how that cap is measured. |
| `GE_NOTICKCARRY=1` | Restore the old round-and-drop tick behaviour, for A/B comparison. |
| `GE_NODELTASMOOTH=1` | Disable the fractional tick delta smoothing. |

### 5.5 Choosing a ROM and a language

| Variable | Effect |
| --- | --- |
| `GE_ROM=<path>` | Cart image path, step 3 in §4. |
| `GE_LANG=e` \| `jp` | Text language. Overrides `--lang` (`romdata.c` records that `GE_LANG` wins). |

### 5.6 Files, logs and housekeeping

| Variable | Effect |
| --- | --- |
| `GE_NOINISAVE=1` | Never write `midas.ini`. Use this for any run you do not want to leave traces in. |
| `GE_NODIRECTLOG=1` | Do not write the Windows direct log (`Data/Logs/ge_direct.log`). |
| `GE_RENDERTHREAD=1` | Render on a separate thread. |

---

## 6. The in-game console

Press `` ` ``. Useful for starting or checking a mode without relaunching:

```
cl_maxfps -1          hardware pacing (see §2.2)
cl_maxfps 120         120 fps cap and a 120 Hz tick
cl_maxfps 0           uncapped
cl_showfps 1          the FPS digits
cl_audio_pump 1       six rows of pump state
cl_audio_log 1        the same, once a second, to Data/Logs/audio_pump.log
cl_hitchms 1          log every frame as a hitch (0 = off)
list                  every cl_ setting with its current value
harness               every GE_* environment knob (§7)
help                  the above, from the game itself
```

`cl_<key>` reads or sets **any** `midas.ini` key by name, so `cl_tickhz`,
`cl_vsync` and the rest all work. Settings changed here are written back to the
file. The full legend, including how to read the numbers, ships as
`tools/console_help.txt` (installed as `console_help.txt` next to the binary).

---

## 7. The complete environment-variable list

There are **310** `GE_*` names in the tree; the large majority are single-purpose
diagnostics for one subsystem (texel tracing, XLU sorting, sky modelling, AI
probes, input capture) and are of no use to a player. Rather than reproduce them
here where they would rot, the game ships the list itself:

```
harness              lists every GE_* knob, marking the ones currently set
harness hw           lists only the ones matching "hw"
```

The table it prints is generated from the source into
`port/src/port_console_env.h` by `scripts/port/regen_console_env.py`. That file is
a snapshot, so it can lag the tree — it did until this branch regenerated it,
when it was 20 entries short and two of the missing ones (`GE_HWLOAD`,
`GE_HWLOAD_SCALE`) were hw-pacing knobs. `harness` is a convenience; a source
grep is the authority:

```bash
grep -rhoa '"GE_[A-Z0-9_]*"' port/src src port/fast3d | tr -d '"' | LC_ALL=C sort -u
```

Most knobs are read **once at startup**, which is why the console command only
reports them and cannot toggle them: set them in the environment before launching.

---

## 8. What a run writes

Beside the executable:

- `midas.ini` — settings; written on first run and whenever a setting changes.
  Delete it to restore defaults. **`GE_NOINISAVE=1` stops this.**
- `Data/Saves/eeprom.bin` — the save file.
- `Data/Logs/ge_direct.log` — the Windows run log (no console window there).
- `Data/Logs/audio_pump.log` — only while `cl_audio_log 1`.
- `Data/Logs/crash.log` — written if the game crashes.

Changing `maxfps` live, or running `cl_maxfps -1`, rewrites the `maxfps` and
`tickhz` lines. If you are testing and want your real settings untouched, back up
`midas.ini` first or set `GE_NOINISAVE=1`.

---

## 9. Provenance

Read from the source: the argument list and their parsing (`romdataInit`), the ROM
search order, every variable in §5, the console command set, and the
`maxfps=-1` → `tickhz=hw` path (`videoCoupleTickToMaxFps` → `videoSetDisplayOption`
→ `portTickSetHwPace(1)`).

Checked by execution, on 2026-10-25: the `GE_*` inventory counts in §7 (310 in
the tree, and the committed table regenerated to match), and the build and test
suite.

**Not executed:** no game run was made while writing this, because a launch writes
`midas.ini`. In particular the statement that `GE_CONCMD="cl_maxfps -1"` engages
hw pacing from the first rendered frame is derived from the code path, not
observed. If you want it verified, run it with `GE_NOINISAVE=1` and check the log
line for the resolved tick rate.
