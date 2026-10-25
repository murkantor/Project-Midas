# Handoff: audit the menu → settings → save path end to end

Paste this whole file into a fresh session as the task. It is self-contained.

The previous pass added three diagnostic boot flags and made the audio-pump
block draw in the front end. Both landed and are verified. What that work ran
into, repeatedly, is the thing this handoff is about: **the settings system has
several ways to hold a state the file does not describe, and no test covers
any of it.**

Three times in one session the question "what is actually saved, and does it
match what is running" could only be answered by reading a 2,000-line file and
launching the game. That is the debt.

---

## Task 0 — FIRST: verify the terminal demo end to end, it is what gets filmed

Do this before anything else in this file. It is small, it is self-contained, and
it is what the user wants to record.

**What it is for.** The user wants to post a video showing that the project's
tests run and pass from a plain terminal, **including a clean build**, and then
that the game launches with diagnostics on.

**The entry points exist** (added with this handoff — verify them, do not
rewrite them; one implementation only):

```bash
make port_test     # all ten tests, one line each, non-zero exit on failure
make audio_test    # the audio / tick / layout four
make demo          # tests, then the game with -audio_pump -audio_log, windowed
```

All three call `scripts/port/port_test.sh`; read its header before changing it.
`--no-game`, `--game-only`, `--audio`, `-v` are the flags. It never reconfigures
`build_macos` — that is the developer's own build directory.

**THE BUILD IS PART OF WHAT IS FILMED, AND MUST BE CHECKED FOR ERRORS.** The
script runs `cmake --build` first and reports it on its own line, before the
tests:

```
  build: succeeded (1 known linker warning: __DATA,__common alignment)
  build: succeeded
  build: succeeded, N NEW linker warning(s)   <- with the lines printed
  build: up to date, nothing recompiled
BUILD FAILED - errors:                        <- with the error lines, exit 1
```

**AND HERE IS THE LIMIT, MEASURED, NOT ASSUMED: the build passes `-w`.** Every C
warning is suppressed outright — the compile line in `build_macos/build.ninja`
ends in `-w`, and a deliberate `#warning "..."` directive produces NO output at
all. So:

- the only warnings this can ever report are **linker** ones, of which there is
exactly one known (`__DATA,__common` alignment, allow-listed by name);
- **"build succeeded" does not mean "no warnings exist"** — it means the compiler
was told not to tell you. The script prints a note saying so, and so should any
doc that quotes it.

That is why the new-warning test below must be done with a *linker* condition,
not a source warning. Adding `-Wall` would surface real findings and is worth
doing, but it is a separate scoped change with its own triage burden — do not
smuggle it into a demo commit.

Verify all four paths, not just the happy one:

1. **up to date** — run it twice.
2. **known warning** — `touch port/src/boot_flags.c` then run; that forces a
   relink and the `__DATA,__common` line appears, classified as known.
3. **new warning** — hard to trigger honestly under `-w`, which is the point.
   Verify the classification logic instead (feed it a log with one known and two
   unknown `warning:` lines) and say in the commit that it was not exercised
   end-to-end.
4. **build failure** — append a syntax error to a port source, run, confirm exit
   1 and the error lines, then `git checkout` the file and rebuild.

**It is for a camera, so the output is a deliverable, not a log.** One stable
line per test with its duration, a `N passed, N failed` line, `RESULT: PASSED` or
`RESULT: FAILED`, no ANSI colour, no emoji, nothing that wraps. A failing test
prints its own reason under the summary — verify that too, by making one fail on
purpose.

**THE ONE THAT WILL BITE: run it from a real terminal.** A sandboxed or non-GUI
shell gets `GL: renderer: Apple Software Renderer`, which renders into the back
buffer and leaves the window black while audio plays. Check with
`grep 'GL: renderer'` before filming. See `CLI.md` §7.2. On this machine the real
GPU line reads `Apple M5`.

**The window.** The user's `midas.ini` is borderless fullscreen on
`window_display=1` at negative coordinates, so a demo with no override opens on
the second monitor and looks like nothing happened. `-v` (and `make demo`, which
passes it) sets windowed 960x600 on display 0 for the run and restores the file
byte for byte, the same approach `boot_flag_test.sh` uses.

**Acceptance:** the user runs one command from the repo root in their own
terminal, sees the build line, ten `ok` lines and a summary, and then the game
window opens with the pump block in the top-left corner. Nothing in `midas.ini`
or `Data/Saves/` may be left changed — same backup/restore and md5 discipline as
`scripts/port/audio_run.sh`.

---

## 0. State you are inheriting

Branch `diagnostic-launch-kit`, based on `tick-field-accounting`, which is based
on `3a1ab62`. See the topology below — the three-level shape matters.
**Do not trust a commit count written in this file** — derive it:
`git rev-list --count 3a1ab62..HEAD` (28 at the time of writing, and reading a
number instead of running that command is how this branch miscounted itself five
times).

Baseline, measured at the tip:

```
build   clean; only the pre-existing "reducing alignment of section __DATA,__common" warning
ctest   10/10   (native_layout, native_collision, native_aim, native_stan,
                 native_movement, native_prop_collision, native_player,
                 audio_pace, boot_flags, tick_stats)
```

**Push state — check it, do not trust this paragraph.** `origin/tick-field-accounting`
exists and is the base of this branch; whether it is level with the local ref
depends on who pushed last. Nothing has been through CI. At the time of writing:

```
git rev-parse --short origin/tick-field-accounting   # was ec848d4 (22 commits)
git log --oneline -1 origin/diagnostic-launch-kit     # did not exist yet
```

**THIS BRANCH IS THE SECOND OF TWO.** `NEXT_LEVEL_AGENT.md` lives here, not on
`tick-field-accounting`, because the session that wrote it also produced the
five commits above it. The topology:

```
3a1ab62 ── 23 commits ── tick-field-accounting (ef44204)   the tick accounting
                              └─ 5 commits ── diagnostic-launch-kit (HEAD)   the
                                              boot flags, harnesses, this file
```

Consequences worth knowing before you start:

- **The practical base for new work is this branch's tip**, not `3a1ab62`. The
  three-level chain above is still true transitively, but rebasing onto
  `3a1ab62` would lose the boot flags and the harnesses.
- **If `tick-field-accounting` is squash-merged, this branch's base disappears**
  and it needs a rebase. That is the branch's own reopen condition for the
  folding question, and it is not pre-solved.
- **`CLI.md` here is a superset of the one on `tick-field-accounting`.** The
  earlier branch describes `GE_WINDOW` as "the capture/harness switch" that
  overrides the window size — true, but it does not say the name misleads: it
  does **not** change the display mode. The clarification is §7.1 here, and it
  was left on this branch on purpose rather than back-ported, because the same
  paragraph carries the boot flags.
- Two reports sit untracked at the repo root: `PR_tick-field-accounting.md` and
  `PR_diagnostic-launch-kit.md`. They are paste sources, not diffs.

Read first, in this order:

1. **`CLI.md`** — the user-facing command line, the boot flags (§3.2), and two
   corrections that cost a session each: `GE_WINDOW` does not change the display
   mode (§7.1), and a sandboxed shell gets the software renderer and a black
   window while captures look perfect (§7.2).
2. **`docs/port/audio-pump-state-of-play.md`** — the entry point for the branch's
   earlier work, with the findings register.
3. **`port/src/port_options.c`**, `video.c`'s ini half, `front.c`'s sheets,
   `port_watchpages.c` — the sweep surface itself.

**Two harnesses exist. Use them, do not rewrite them.**

```bash
scripts/port/audio_run.sh          # time-bounded live run: ini backup/restore,
                                   # eeprom md5 guard, refuse-if-running, early stop
scripts/port/boot_flag_test.sh     # seven live cases, captures into a dated folder
                                   # under captures/, opens it when done
```

`audio_run.sh -g "ARGS"` passes game arguments. `boot_flag_test.sh -v` puts test
runs in a window on display 0, because the user's `midas.ini` is borderless
fullscreen on display 1 at negative coordinates.

### 0.1 What the last pass found, and what it cost

The boot-flag work was reviewed adversarially after it was committed, and the
review found six defects — five in the new code, one in the harness that was
supposed to catch them. All six are fixed in `4ece424`. Read that message: it is
the clearest single example of this branch's recurring failure, and it is the
reason §1 rule 5 exists.

| # | Defect | Why it happened |
| --- | --- | --- |
| 1 | The `ignoring argument` log reported the documented `--rom-file=…` and `--lang=…` forms as **unknown** | The log re-matched flag names itself instead of asking the parser; a hand-written list forgot the `=value` form. Now one function answers for both. |
| 2 | `GE_N64AA=0 -n64_aa` ran the whole Classic Mode apply and then cleared the flag, leaving **forced settings with the mode reported off** | The env override was read *after* the flag. Env now runs first — command line beats environment. |
| 3 | `GE_TICKHZ=120 -n64_aa` gave **a 120 Hz tick under a 60-field hold**, silently | `portTickHwPace()` does not look at the tick rate. Still permitted (both are diagnostics), now announced on the console. |
| 4 | The harness's `n64_aa` case **asserted the log sentence, not the resulting config** | The header claimed it checked `low_res=240`/`maxfps=-1`; it checked that a string was printed. It now parses the one `ge: video: config:` line. |
| 5 | The same assertion made the harness **fail on every run after the first** | `-n64_aa` persists `n64_aa=1`, so the second run correctly does nothing and logged nothing. Fixed by asserting the config line, which is idempotent. |
| 6 | `tr -d 'stats: '` **ate the `s` of `fps`** in every summary (`fps=60fp`) | `tr -d` deletes characters anywhere in the string. Replaced with `sed`. |

Also fixed: the harness had no `INT`/`TERM` trap, so a Ctrl-C mid-case could
leave a per-case display mode in `midas.ini` with the game still running.

**The pattern, stated plainly**, because it is the thing to watch for in your own
work: every one of the six was visible by *reading the output a human would read*,
and none was caught by the build or the suite. Two of them (#4, #5) were in the
harness whose whole job was to catch that class of error.

### 0.2 One finding the last pass deliberately did NOT fix

**`n64_aa=1` in `midas.ini` alone does not force Classic Mode's parts.** Only the
toggle path (`portClassicToggle` → `portClassicSetLive`) saves the snapshot and
forces the seven values. A hand-edited `n64_aa=1` therefore boots with the AA flag
set and the parts at whatever they already were.

Confirmed by experiment: setting `n64_aa=1` by hand with `low_res=0` produced a
config line reading `low_res=0` — the AA on, the resolution not forced. It was
NOT the state `-n64_aa` leaves behind, which is consistent (`low_res=240`,
`maxfps=-1`, `msaa=2`, `classic_saved` intact).

It is pre-existing design, not introduced by the boot flags, and fixing it is a
product decision (should the ini be authoritative, or the snapshot?). It belongs
in the sweep below — **§2.2** — not in a boot-flag commit.

---

## 1. Ground rules

1. **Derive claims from code and execution, not from the docs.** This branch has
   shipped false statements in two manuals, a design record and several comments
   — twice while fixing an earlier false one.
2. **Do not present an inference as a measurement.** Mark what you executed and
   what you only read.
3. **Guard the user's settings.** `build_macos/midas.ini` is real:
   `cp build_macos/midas.ini /tmp/midas.ini.sweep-backup` before and diff after.
   Never touch `Data/Saves/`; check `eeprom.bin`'s md5
   (`3590c7df0edb0930f4e4248826e8ab64`) before and after.
4. **`GE_NOINISAVE=1` for anything exploratory.** It makes the whole save path a
   no-op, which is exactly what you want while testing the save path — and
   exactly why you must ALSO test without it.
5. **A test that passes is not a test that works.** This branch's recorded
   failure mode, five times over: *a fix verified on one shape of input, reported
   as the mechanism working.* Assume you are the sixth.
6. **Nothing investigative may be thrown away.** A script that produced a number
   a commit message cites must be committed: `port/tests/` if it asserts,
   `scripts/port/` if a human reads its output. `build_macos/` is gitignored
   (`.gitignore:274`) and a harness there dies with the session; `captures/` is
   gitignored too and is for evidence, not code.

---

## 2. The sweep

The question for every item: **does the saved state match the running state, and
does it survive a round trip?** Not "does the menu look right".

### 2.1 The surface

**One table drives the menus.** `sOpts[]` in `port/src/port_options.c` — 101
rows, each with a `section`, a label and a getter/setter pair. Every row is
reachable as `cl_<key>` in the console. Sections written: `[display]`,
`[input]`, plus whatever `sSections[]` lists.

**Four writers touch `midas.ini`:**

| Writer | Writes |
| --- | --- |
| `portOptSave` (`port_options.c`) | the whole table, section by section, through `portIniWriteStrs` |
| `video.c` | targeted single keys: `tickhz`, `maxfps`, `classic_saved`, window geometry |
| `input_pc.c` | `[input]` multi-player rows (`PM_COUNT`) |
| `port_console.c` | one `[display]` key from a console `cl_` command |

**Three guards, and they do not all guard the same thing:**
`portIniWriteStrs` caches `allowed` once on first call from `GE_WINDOW` /
`GE_NOINISAVE`; `videoIniWritableCheck` probes writability with `fopen(..., "a")`
(which *creates* the file); the window saver has its own check.

**The rewrite is preserve-on-write.** `videoIniWriteKeys` reads every line, and
writes back the ones it did not replace. Verified by reading, not execution — so
**test it**: put a comment and an unknown key in `midas.ini`, change one setting,
confirm both survive. If they do not, every hand-edited setting a user ever made
is destroyed on the next save, and nothing currently says so.

**Files other than `midas.ini`:** `Data/Devices/*.gedevice` (per-device, incl.
`kbm.gedevice` binds), `Data/Profiles/*.geprofile`, `*.gestats`,
`Data/WeaponSets/*.gewset`, `Data/MatchSettings/*.gemp`, `Data/Saves/eeprom.bin`.
Read-only for this sweep unless you find a save bug there.

### 2.2 The classic-mode snapshot, which is the sharpest edge

Classic Mode (`n64_aa`, toggled by Tab) works by **saving your current values and
forcing its own**, then restoring on the way out (`portClassicSetLive`,
`classic_saved`). It is a stateful swap with no transactional guarantee:

- toggling on saves the current values into `classic_saved`;
- toggling on **again** while already on would overwrite that snapshot — the
  `-n64_aa` boot flag is guarded against exactly this (`!s_CfgN64Aa`), but is
  every other entry point?
- changing a forced part *while* Classic Mode is on goes through
  `classicPartChanged`, which has its own save/restore rules;
- `portOptResetDefaults` deliberately writes `n64_aa` raw *first*, before the
  others, "so its remembered values can't come back over the defaults" — read
  that comment and then test whether it is true.

**Test the round trip:** record every display setting → turn Classic Mode on →
change one setting on the CLASSIC sheet → turn it off → compare. Then do it with
the game restarted in between, so the file round-trips too.

### 2.3 The two ordering hazards already on record

- **`maxfps` and `tickhz` are one knob in two lines.** F3-A made the writer
  record `hw` rather than leave a stale number. The coupling runs *after the
  whole file* on purpose. Check the invariant the design record states: the
  writer must never emit a pair the loader resolves differently from what was
  running. Then find an input order that breaks it.
- **`room_cache` is forced to 0 by hardware pacing** and restored when hw is
  left (`videoRoomCacheAfterHw`). Test that leaving hw restores it, and that a
  crash while in hw does not leave it off forever with no record.

### 2.4 The boot flags, and how they interact with env vars

The three flags are new (see `CLI.md` §3.2). Two conflicts are **confirmed by
reading and not yet fixed or documented**:

- **`GE_N64AA=0` defeats `-n64_aa` — and leaves a half-applied mode.**
  `videoApplyBootArgs()` runs at `video.c:4953`; `GE_N64AA` is applied at
  `video.c:~5019`, *after* it. So the flag turns Classic Mode on (which saves the
  snapshot and forces `low_res=240`, `maxfps=-1`, `msaa=2` …), and then
  `GE_N64AA=0` clears the flag while **the forced parts stay forced**. Result: a
  session running Classic Mode's settings with Classic Mode reported off, and a
  `midas.ini` that says `n64_aa=1`. Neither the precedence nor the half-state is
  documented.
- **`GE_TICKHZ` with `-n64_aa` gives a 120 Hz tick under hardware pacing.**
  `portTickHz()` (`frametiming.c`) reads `GE_TICKHZ` first, so it wins over the
  60 the hw mode set; `portTickHwPace()` does **not** check the tick rate, so the
  hold is still priced and the credit cap still yields to it. A deliberate hold
  at a raised tick is not a documented mode.

Decide for each: make the flag win, make the env var win and say so, or refuse
the combination loudly. Then write it down in `CLI.md` — a silent precedence is
the same defect class as `GE_TICKHZ=hw` meaning *uncapped*.

Also sweep the other env overrides for the same pattern: `GE_LOWRES` (applied at
~5035, after the flags), `GE_FORCEROOM`, `GE_HWSCALE`, and anything else that
writes through `s_Cfg*` at a later point in `videoInit`.

### 2.5 What the menus write, and when

- **`sSaveHold`** defers a write while an analog ramp is held. What happens if
  the game exits mid-hold — is the setting lost?
- The window saver writes `window_x/y`, `window_width/height`, `window_display`
  and `fullscreen` **at exit**. A crash mid-session therefore saves nothing, and
  a window moved to a disconnected display may be restored off-screen. The
  user's own file has `window_x=-1568`, which is exactly that shape.
- `videoIniWritableCheck` opens `midas.ini` in `"a"` mode to test writability. It
  *creates* the file if absent. Is that intended, and is it before or after the
  "no file yet" defaults path?

---

## 3. How to verify

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cmake --build build_macos -j8            # expect only the __DATA,__common warning
cd build_macos && ctest --output-on-failure        # baseline 10/10
```

Round-trip recipe for one setting, which is the shape most findings will take:

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cp build_macos/midas.ini /tmp/ini.before
GE_NOINISAVE=1 build_macos/ProjectMidas -audio_pump &   # watch what it THINKS
# change the setting in the menu, quit
diff /tmp/ini.before build_macos/midas.ini               # what it WROTE
```

Then repeat **without** `GE_NOINISAVE=1` and compare the two diffs: any
difference between "what the session ran" and "what it wrote" is a finding.

**A `ctest` target is the right home** if you can isolate a rule — the pattern
that worked for `portStatsStepCap` and `portBootFlagsParse` is: pull the decision
into a pure function in `port/src/`, give it a `port/tests/` file, register it
explicitly in `CMakeLists.txt` inside the `if(GE_PORT_TESTS)` block (there is no
glob). Candidate rules worth isolating: which values Classic Mode forces, and
whether a proposed write leaves the loader resolving the same mode.

**Live runs must say which renderer they used.** This machine's GL is
`Apple M5` from a normal terminal and `Apple Software Renderer` from a sandboxed
shell; the latter draws correctly into the back buffer but may not composite, so
a capture can look perfect while the screen is black. Check
`GL: renderer` in the log before concluding anything about what a run showed.

---

## 4. Known hazards, in the order they will bite

1. **`midas.ini` is the user's real settings file.** Every experiment restores
   it. The harnesses do this; anything you write by hand must too.
2. **`-n64_aa` writes the file.** It *is* the Tab path. Testing it changes the
   user's saved state unless `GE_NOINISAVE=1`.
3. **Classic Mode's swap is not transactional.** A partial failure (or a
   conflicting env var) leaves forced parts applied with the mode off.
4. **`docs/` is gitignored** (`.gitignore:281`, `/docs/*`), and eight files under
   `docs/port/` are tracked anyway. A **new** file there needs `git add -f` or it
   exists on one disk and nowhere else. Verify with `git ls-files docs/port/`,
   never with the `git add` warning — that fires for tracked files too and is
   useless as a signal.
5. **Repo conventions**: subjects are `area: lowercase clause`; the citation
   policy is symbol-or-section, line numbers only where the line is the point.

---

## 5. Where a harness belongs

- **`port/tests/`** — anything that asserts. Pure C, no SDL, no ROM, sub-second.
- **`scripts/port/`** — anything a human reads. Header must say what its output
  can and cannot tell you.
- **`captures/`** — evidence. Dated folders, gitignored. `boot_flag_test.sh` is
  the model: it writes a `README.txt` recording the renderer and opens the
  folder, because "the captures look right" and "I saw it on screen" are
  different claims and that gap cost a session.
- **`build_macos/`** — nothing, ever.

If a script cannot separate two explanations, it must print both and say so.

---

## 6. Deliverable

A findings register as `docs/port/settings-and-save-audit.md`: every item in §2
with a verdict (*correct / contradicts / undocumented*), the evidence, and
whether it was executed or only read. Fix what is cheap and safe — the env-var
precedence in §2.4 and the documentation are both — and leave the rest as
recorded, named defects rather than silent ones.

**Then, and only then**, commit. One commit per subject: a fix, a test, a doc.
This branch's history is read as five phases of related work; keep that property.
Do not squash anything, and do not rewrite a commit message to match a later
finding — the record of a wrong first conclusion is worth more than a tidy one.
