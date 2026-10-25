# Audio-pump branch — third-pass audit

Branch `tick-field-accounting` (then `audio-pump-wallclock_gen4`), base
`3a1ab62`, tip `2f8a6d0` (7 commits).
Report-only pass: no tracked file was edited, staged or committed. The only
writes were `Data/Logs/`, `/tmp/`, and the `midas.ini` backup/restore cycle in
§5 of the handoff, which closed clean: the user's ini is byte-identical to how
it was found (`maxfps=-1`, `vsync=0`, `tickhz=0`, `room_cache=0`), and
`Data/Saves/eeprom.bin` has the same md5 (`3590c7df…`) before and after.

Build and suite: `cmake --build build_macos -j8` clean except the pre-existing
`__DATA,__common` alignment warning; `ctest` **9/9**.

Environment bound, and it matters: this shell gets an **Apple Software
Renderer** GL context. Front-end frames cost ~25 ms, Dam frames ~360 ms. Every
result below is conditioned on that; anything needing a fast renderer is marked
unverified rather than guessed.

**Resolution status (added after the fixes landed).** This report describes the
branch as it was audited. Two follow-up commits address it: `52807f7` corrects
F2, F4, F5 and F6 and the manual side of F3, and `cb98c39` fixes F1 by reporting
the stall cap at the clamp site (`credit N of M`) instead of inviting the `fld`
versus `model held` comparison. §6 records the post-fix numbers to beat. **F3's
code half and D5 were both open when this file was written and are now closed**
by `NEXT_AGENT_V3.md`'s Tasks A and B; §6 records both, including a wrong first
cut of the F3 fix. The findings below are left as written, including the
self-correction under F2, because the record of a wrong first conclusion is
worth more than a tidy one.

---

## 1. Findings

IDs are stable and are referenced from §4. By severity, the order is: **F1**
(major), **F3** (major), then **F2**, **F4**, **F5**, **F6** (all minor).

| ID | Severity | One line |
|----|----------|----------|
| F1 | major | `model held N` / `N fld` reads "healthy" while 63% of wall time is discarded |
| F3 | major | manual says `tickhz` is rewritten to `hw` under `maxfps=-1`; it is not |
| F2 | minor | the general rule behind `tk` (bounded by presents) is never stated |
| F4 | minor | "`f/s` reads 0 during a stage load" — it reads 0.8–1.4 |
| F5 | minor | stated healthy `f/s` band is narrower than the measured one (59.3–62.9) |
| F6 | minor | log's frame/tick columns are frozen, not measured, during a load |

### F1 — `model held N` vs `N fld` compares two different spans, and reads "healthy" while the sim loses two thirds of wall time

**Where claimed** — `port/src/hitchlog.c:150-159`:

> `/* The audit: under hw pacing the model asks for `model held N` fields and
> the sim is credited `fld`. Both cover the same span, the frame that just
> presented … `fld` short of N is simulated time thrown away (D5 in
> docs/port/tick-and-field-accounting.md), which is invisible at 60fps … */`

and the commit message of `6d02aa1`:

> `Equal is healthy; fld short of it is lost simulated time.`

**Where contradicted** — executed, `run7` and `run10b`, hw pacing in Dam:

```
ge: hitch: 367.6 ms frame (stage 33, tick60, render thread off) | render: gl 366.1 …
  | sim: longest 365.6 ms, pacer 0.0 ms, 1 tick(s) 8 fld model held 4 | draws 147
```

`fld` (8) is **greater** than `held` (4), which by the stated rule is healthy.
The same window's `audio_pump.log` line reads `fps 3 tk 3 f/s 22.1 f/t 8.00`:
367 ms of wall time is 22 fields at 60 Hz, and the sim was credited 8. Fourteen
fields of simulated time per frame were thrown away — 63% of wall time — and the
audit pair reports success. Across `run7`, of 240 logged hitch lines, 198 read
`8 fld model held 4`, 35 read `… held 3`, and 4 read `… held 2`. Exactly **2**
read `fld < held`, both by a single field (`8 fld model held 9`) on frames of
367 and 386 ms — i.e. 22–23 fields of wall time, of which 14 were discarded.
The documented failure condition therefore fired on 0.8% of lines and reported a
one-field shortfall while the real loss was fourteen.

The stage-load line shows the same inversion at the other extreme:

```
ge: hitch: 9190.3 ms frame (stage 33, …) | sim: … pacer 8736.3 ms,
  1 tick(s) 1 fld model held 2 (clock reset)
```

**Why** — `fld` is the `g_PortFieldsTicked` delta since the previous *present*
(`portHitchFrame`, `hitchlog.c`), which is clamped by `GE_TICKMAXSTEP` = 8 fields
(the stall cap in `waitForNextFrame`; it asks `portTickCreditCap()` now, see D5).
`model held` is the render model's price
for drawing work (`portHwPredictMs`, read in `portHwPaceFields`), published when
the pacer prices a
frame. When the renderer is the bottleneck, the frame's wall span is far longer
than either number. The two are comparable only when the machine presents at
the modelled rate — exactly the case where the audit is not needed.

> **The line numbers this paragraph used to give were wrong on arrival.** It
> cited `frametiming.c:1178-1183` for the cap and `:962-970` for the model, which
> matched the tree the audit examined (`2f8a6d0`) and stopped matching when
> `cb98c39` added 41 lines above them. Replaced with symbols, per the citation
> policy in `audio-pump-state-of-play.md` §5.

**What is actually true** — `model held` is a floor on how long the pacer
waited, not a prediction of wall time; comparing it to the credit delta measures
nothing unless wall time and the model agree, and when they do not, the pair
reads most favourably at the moment the sim is losing the most time.

**Severity: major.** This is the branch's own new instrument, added by `6d02aa1`
for the express purpose of exposing D5, and in the measured regime it reports
D5 as healthy. A developer chasing the hw-pacing time loss will be told there is
none.

### F2 — the manual never says `tk` is bounded by the present rate; the reader is left with three special cases and no general rule

**Where claimed** — `tools/console_help.txt:176-185`:

> `TK  Simulation ticks per second. Max FPS sets the rate it aims for, but three
> quite different things hold it lower. A display that cannot present that
> often: with vsync on, a 60 Hz panel allows 60 ticks a second, so the default
> Max FPS of 120 still reads 60tk and nothing is wrong. A machine that cannot
> keep up: with vsync off, tk below the rate you set means that. Or Hardware
> pacing doing its job …`

**Where contradicted** — `src/boss.c:693`:

```c
if (g_MainStageNum < 0 && pendingGfx < 2U)
{
    …
    lvlTick();          /* boss.c:735 */
```

The tick is admitted only when fewer than two gfx tasks are in flight, so the
tick rate is gated on *completed presents*, not on the loop or the configured
rate. Executed, `GE_TICKHZ=120` with vsync off (`run4`): `pmp 120/s` — the loop
came round 120 times a second — while the log read

```
t=2000ms FPS 38 tk 38 f/s 59.2 f/t 1.55 …
```

`tk` was 38, not 120.

**What is actually true** — the manual's "machine that cannot keep up" clause is
*correct* for this observation: the machine could not render fast enough, and
`tk` followed. The three listed causes are not wrong; they are three instances of
one gate. The omission is the general rule — **`tk` is bounded by the present
rate, so it equals `fps` whenever the renderer is what binds** — which is the
sentence a reader needs, because it makes the other two clauses legible as the
same phenomenon. It also matters that the machine that "cannot keep up" is the
*renderer*, not the simulation: `f/s` in the same window read 59.2, so game time
was correct while `tk` was a third of the configured rate.

Evidence that `tk` and `fps` are all but the same number: 114 of 115 windows
across `run3`, `run8` and `run10b` had `tk == fps` (the exception is a
window-boundary pair, `FPS 38 tk 37`); the user's cited screenshots read
`60fps 60tk`, `738fps 738tk`, `15fps 15tk`, `20fps 20tk`, `832fps 832tk`. The
user's own earlier session log is the informative counter-example —
`fps 65 tk 61` and `fps 61 tk 61` — where presents exceeded ticks, consistent
with ticks being capped near 60 while presents ran slightly ahead.

**Severity: minor.** The manual does not state anything false here, and it warns
the reader off the real trap ("it is not a measure of game speed - read f/s for
that"). What is missing is the rule that subsumes its three cases.

> **Self-correction, recorded deliberately.** My first pass wrote this up as
> "`tk` cannot exceed the presented frame rate" and rated it **major**. Both were
> wrong. `pendingGfx` is tested `< 2U`, so two ticks per present are structurally
> allowed — up to 2× is possible, and I never observed even 1× exceeded. And the
> manual's "machine that cannot keep up" does describe what I measured. I am
> leaving the correction visible rather than silently editing, because this is
> precisely the failure mode the handoff warned about (a correction that inverts
> an error, made from reading instead of execution) and because the first two
> passes on this branch made it twice. The rule I now believe, with the evidence
> above: `tk ≈ fps`, bounded by presents, up to 2 ticks per present in principle.

### F3 — the manual says the game rewrites `tickhz` to `hw` when `maxfps` is -1; executed, it leaves the stale number

**Where claimed** — `tools/midas_ini_help.txt:139-141`:

> `tickhz | (derived) | Worked out from maxfps and rewritten by the game; `hw`
> when maxfps is -1. Do not edit; change maxfps`

**Where contradicted** — `run10b`. The ini was set to `maxfps=-1` with
`tickhz=120` hand-written, so a rewrite would be visible. The session resolved to
hardware pacing — the hitch lines carry `model held N`, which only prints when
`portTickHwPace()` is true — and afterwards `midas.ini` still read:

```
69:maxfps=-1
71:tickhz=120
```

`videoPersistDerivedTick` (`port/src/video.c`) used to return early when
`s_CfgTickHz == -1`, so the `hw` form was never written by this path. **That
early return is gone** (F3-A); the function now writes `hw` through
`portTickIniValue`, and the resolution is recorded in
`docs/port/tick-and-field-accounting.md` §11.3 and in
`audio-pump-state-of-play.md` §2. The user's
own file is the same shape (`maxfps=-1`, `tickhz=0`), which is precisely the
state §11.3 records as live:

> `So a player who leaves the menu reporting "uncapped" exits with a file that
> resolves back to hw pacing … The writer should never emit a pair the loader
> resolves differently from what was running.`

**What is actually true** — a session running `tickhz=hw` leaves a numeric
`tickhz` in `midas.ini`; the value the loader resolves is not the value the file
records.

**Severity: major.** This is a shipped manual instructing the player what the
file will contain. §11.3 said the *code* was wrong; `bbc5c99` then wrote the
manual to assert the opposite of what the code does.

### F4 — "`f/s` reads 0 during a stage load" is false; it reads 0.8–1.4

**Where claimed** — `tools/console_help.txt:193-197`:

> `It also reads 0 during a stage load, and for one window after a frame so long
> that no tick landed inside the half-second window`

and the emitted legend, `port/src/audio_pc.c:1095-1098`:

> `#   f/s … 0.0 while paused, controls locked, or during a stage load.`

**Where contradicted** — two stage loads captured, no zero in either. Run 10b,
at the Dam hold:

```
t=12388ms FPS 1 tk 1 f/s 0.8 f/t 1.17 pmp 28/s rnd 28/s …
```

Run 7, same point: `f/s 1.4`, then `f/s 21.8` in the next window. Nor does a
9.19 s frame produce 0 — that is the `12388ms` line above, and its window
contains a tick.

**What is actually true** — during a stage load `f/s` reads a small nonzero value
(0.8–1.4), because a tick on one side of the hold lands inside the window.

**Severity: minor.** The false direction is "reads 0", which the manual frames
as correct-not-a-fault, so it does not raise a false alarm. It does make the
manual's own diagnostic rule ("If it reads 0 while you are playing … that IS a
fault") unactionable, since the load case it names never produces the value.

### F5 — the stated healthy band for `f/s` is narrower than the quantity's own quantisation

**Where claimed** — `docs/port/tick-and-field-accounting.md:630`:

> `The session held 15 fps for a long spell with `f/t 4.00` and `f/s 59.8–60.5``

and `tools/console_help.txt:186-188`: "60.0 means the game is running at the
right speed".

**Where contradicted** — front-end `run8`, healthy sim (every window `tk == fps`,
`f/s` internally consistent with `f/t`), `f/s` values across its 11 windows:

```
59.3  61.0  60.0  59.7  62.9  59.6  59.3  60.7  61.0  59.8  59.9
```

Four of the eleven sit above 60.5, one as high as **62.9** (`fps 61 tk 61
f/t 1.03` — 61 fields/s of tick rate with one field per tick, so 62.9 is
measurement scatter, not a fast sim). `run3`, which matched the manual exactly on
every other field, also produced `f/s 62.0` and `f/s 62.6` once its frame rate
wandered. Nothing was wrong in any of them.

**What is actually true** — with integer field credit over a ~500 ms window that
is never exactly 500 ms, `f/s` scatters roughly ±5% around 60; the measured
healthy range here is **59.3–62.9**. A reading in the low sixties is ordinary.
The branch's own arithmetic rounds rather than truncates
(`portStatsRatePerSec`, `port/src/tick_stats.c`), which is what lets readings
exceed 60.

**Severity: minor.** A user or developer applying "60.0 or it is broken" — which
is close to how both the manual and the design record phrase it — will raise
false alarms on a working build.

### F6 — the log's frame/tick fields are not measured during a stage load; the pump keeps writing lines against a window that has not closed

**Where claimed** — `tools/console_help.txt:171-173`:

> `Field by field. Everything except QUE, CAP, STALL, CLK and DEV is measured
> over the last half second, so it settles rather than flickering`

**Where contradicted** — the stat pass that produces `FPS`, `tk`, `f/s` and
`f/t` runs only from `gfx_sdl_swap_buffers_begin` (`port/fast3d/gfx_sdl2.cpp:1099`),
i.e. once per present. During the 8.7 s hw stage hold nothing is presented
(`boss.c:540-545`, documented in §11.2), so those four fields are frozen at
their pre-load values while `portAudioLogLine` keeps emitting lines with live
pump numbers. The manual's single sentence covers them as if they shared the
pump's window, and the new "two caveats" paragraph (`console_help.txt:99-104`)
names only SND, PMP and RND as window-skewed.

**What is actually true** — during a load (and any render stall longer than
500 ms) the frame/tick columns of the log are a stale snapshot, not a
measurement; only the pump columns are live.

**Severity: minor.** It does not mislead about health, but it makes the log's
frame columns untrustworthy for exactly the long stall a report is likely to be
about.

---

## 2. Unverified

- **Paused / controls-locked state (handoff §6.4).** Not reachable. The console
  exposes no pause command (`port/src/port_console.c` has no `pause` verb) and I
  would not drive a menu by synthesised input. The documented "f/s reads 0 while
  paused, `tk` can still read 60 beside it" is therefore **unverified by
  execution**; it is consistent with `lv.c:1502-1513` zeroing `g_ClockTimer`
  while `g_PortTickSerial++` at `lv.c:1499` runs first, but that is reading, not
  measurement.
- **Sub-ticking (`f/t` below 1.00) at a raised tick rate.** Not producible here.
  It needs the renderer to present faster than 60/s; the software renderer tops
  out at ~38 fps. The lowest `f/t` I measured anywhere in my own runs was
  **1.03**, so the sub-1.00 regime was never entered. The claim is corroborated
  only by the user's untracked screenshots (`738fps … 0.08f/t`), which show
  `tk == fps` there too (see F2).
- **vsync holding `tk` at 60 on a 60 Hz panel.** Run 5 set `cl_vsync 1` with
  `tickhz=120` and measured `fps 30 tk 30` — the machine, not the panel, was the
  limit, so the manual's vsync sentence was neither confirmed nor refuted.
- **The D4 negative-TPS fix under a live reset.** No run produced a stage load
  inside a stat window with a non-zero pre-load count in the window (the
  load-window line read `FPS 1 tk 1`, not a negative). The unit test covers the
  arithmetic; the race the fix exists for was not observed. The design record
  says the same.
- **`docs/port/n64-hw-pacing.md` and `docs/port/tick-rate-binding.md`.** Cited by
  `frametiming.c` (the hw model and its load rates) and by `video.c`
  (`videoCoupleTickToMaxFps`'s comment). Both were absent at the time of this
  audit and are resolved now: `n64-hw-pacing.md` exists, and the tick-rate
  binding was folded into `tick-and-field-accounting.md` §13 rather than given a
  second overlapping document.
- **Windows/Linux behaviour.** macOS only, as the handoff anticipated.

---

## 3. Code that looks wrong but is faithfully documented

- **`g_PortStatTps` is computed every 500 ms and read by nothing**
  (`gfx_sdl2.cpp:1001`, declared `frametiming.c:122`). The definition comment,
  `frametiming.h:33` and `tps-the-lie.md` §6 all say so plainly. Accurate; only
  the cost of a dead store.
- **The `lv.c:1520-1522` comment — "Always true at GE_TICKHZ=60" — is false at or
  below 60 fps.** All three documents flag it as the one known-live mislabel
  (`tick-and-field-accounting.md` §4, `tps-the-lie.md` §2.4/§6). The code is
  wrong in the way the docs say it is wrong.
- **`GE_TICKMAXSTEP` truncating a deliberate hw hold (D5).** Code defect, and
  §5.2 of the design record describes it exactly as it behaves — measured here at
  8 credited against 22 elapsed per frame. Finding F1 is separate: it is the
  *instrument* that misreports D5, not the D5 description. **Fixed** — under hw
  pacing the cap now yields to the hold; see §6, which is where the resolution
  and the numbers are recorded.

---

## 4. Execution record

All commands run from `/Users/jcorneli/Developer/DeepSeek/PM-fork/build_macos`,
after `cp midas.ini midas.ini.audit-backup`; the backup was diffed after every
run and restored from before the next.

Run IDs below are the shell artefact names (`/tmp/audit-runN.log`), which are
also what the predictions in the appendix were written against.

| Run | State | Command | Result |
|-----|-------|---------|--------|
| `run1` | ini defaults (`maxfps=-1`, `vsync=0`, `tickhz=0`), title screen | `GE_FRAMES=400 GE_CONCMD="cl_audio_log 1" ./ProjectMidas` | 9 log lines; ini unchanged |
| `run2` | hw + `cl_maxfps 30` + `GE_STAGE=dam` | `GE_FRAMES=760 GE_STAGE=dam GE_CONCMD="cl_audio_log 1;cl_maxfps 30"` | **contaminated, discarded**: outlived its 44 s foreground window, the user switched context mid-run. Kept only as the source of the 3 fps / dry-pump observation |
| `run3` | `cl_maxfps 30`, title screen | `GE_FRAMES=400 GE_CONCMD="cl_audio_log 1;cl_maxfps 30"` | 13 lines, all `FPS 30 tk 30 f/s 60.0 f/t 2.00` |
| `run4` | `GE_TICKHZ=120`, vsync off | `GE_FRAMES=1600 GE_TICKHZ=120 …` | `FPS 38 tk 38 f/s 59.2–61.4 f/t 1.55`; `pmp 120/s` |
| `run5` | `cl_maxfps 120; cl_vsync 1` | `GE_FRAMES=700 GE_CONCMD="cl_audio_log 1;cl_maxfps 120;cl_vsync 1"` | `fps 30 tk 30 f/s 60.0 f/t 2.00` |
| `run6`, `run6b` | hw pacing, Dam, short | `GE_FRAMES=80` / `260 GE_STAGE=dam GE_HITCHMS=1 GE_HWDBG=1` | never reached the stage; discarded |
| `run7` | hw pacing, Dam, `GE_FRAMES=900` | `GE_FRAMES=900 GE_STAGE=dam GE_HITCHMS=1 GE_CONCMD="cl_audio_log 1"` | 91 log lines, 240 hitch lines; `8 fld model held 4` ×198 |
| `run8` | live `cl_maxfps -1`, title screen | `GE_FRAMES=500 GE_CONCMD="cl_audio_log 1;cl_maxfps -1"` | 11 lines; **not a valid hw test** — `portTickHwPace()` is false outside a stage, and the file's values were unchanged before and after, so a write was indistinguishable from no write |
| `run9` | in-stage attempt, `maxfps=-1` + `tickhz=120` | `GE_FRAMES=420 GE_STAGE=dam …` | never reached the stage (420 pumps gave ~125 presents); discarded |
| `run10b` | in-stage redo, same ini | `GE_FRAMES=900 GE_STAGE=dam GE_HITCHMS=1 GE_CONCMD="cl_audio_log 1"` | reached stage 33, `model held` present, **ini still `tickhz=120` afterwards** |

### Predictions, and whether they held

Written down before each run; reproduced verbatim in the appendix. Four of them
**failed**, and every failure is a finding.

| Run | Predicted | Observed | Held? |
|-----|-----------|----------|-------|
| `run3` — `cl_maxfps 30` | fps 30, tk 30, f/s 60.0, f/t 2.00 | `30 30 60.0 2.00`, all 13 lines | **yes**, exactly |
| `run4` — `GE_TICKHZ=120` | tk ~120, fps ~40, f/s 60.0, f/t ~0.50 | tk **38**, fps 38, f/s 57.7–62.0, f/t **1.55** | **no** → F2 |
| `run5` — `maxfps 120; vsync 1` | fps ~60, tk ~60, f/t ~1.00 | fps 30, tk 30, f/t 2.00 (machine-limited) | shape only |
| `run7` — hw Dam, audit pair | `fld` 8, `held` > 8, i.e. `fld < held` | `8 fld model held 4`, i.e. `fld > held` | **no** → F1 |
| `run7` — hw Dam, load window | no `model held`, f/s 0 or near 0 | the hold carried `model held 2`; f/s 0.8/1.4, never 0 | **no** → F4 |
| `run10b` — `maxfps=-1` + `tickhz=120` | file rewritten to `tickhz=hw` | file left at `tickhz=120` | **no** → F3 |

`run4` is the important one. I predicted the sim would step 120 times a second
while the renderer managed ~40 frames, i.e. `f/t ≈ 0.50`. It stepped 38 times.
That is not a small error in the model: it is the observation that made me look
at the `pendingGfx < 2` gate and notice that `tk` is paced by presents. The
manual survives that (`run4` is its "machine that cannot keep up" case); what it
does not say is that this is the *general* rule rather than one of three special
cases. A reading-only pass had me rate that gap major, and execution is what
demoted it — see the self-correction note under F2.

Notable raw lines:

```
# state 2 — the manual's explicit claim, exact
t=1001ms FPS 30 tk 30 f/s 60.0 f/t 2.00 pmp 30/s rnd 30/s que 4544/4544B …

# state 3 — loop at 120/s, tick at 38/s (F2)
t=2000ms FPS 38 tk 38 f/s 59.2 f/t 1.55 pmp 120/s rnd 94/s …

# hw pacing in Dam — the pair reads healthy while time is lost (F1)
ge: hitch: 367.6 ms frame (stage 33, tick60, render thread off) | render: gl 366.1 …
  | sim: longest 365.6 ms, pacer 0.0 ms, 1 tick(s) 8 fld model held 4
t=13545ms FPS 3 tk 3 f/s 20.8 f/t 8.00 pmp 2/s rnd 2/s …

# stage-load hold, and the non-zero f/s (F4)
ge: hwpace: stage load 8747.3 ms
ge: hwpace: stage load took 16 ms here, held to 8751 ms
ge: hitch: 9190.3 ms frame (stage 33 …) | sim: … pacer 8736.3 ms, 1 tick(s) 1 fld model held 2 (clock reset)
t=12388ms FPS 1 tk 1 f/s 0.8 f/t 1.17 pmp 28/s rnd 28/s …
```

Formatting was checked mechanically rather than by eye: each `fprintf`/`sprintf`
in `audio_pc.c:1191`, `bondview2.c:12717` and `gfx_sdl2.cpp:1009` has a matching
argument count and type, and `f/s × f/t` reconciles with `tk` on every sampled
line (e.g. `60.0 / 1.55 = 38.7` against `tk 38`; `20.8 / 8.00 = 2.6` against
`tk 3`). No removed field is still printed: `TPS` appears in no format string,
no legend and no title bar at `HEAD`.

One honest note on the crackle the user heard: these runs open the real audio
device. In Dam at 3 fps the pump cannot keep the queue fed — 26 of 91 log lines
carried dry pumps (30 events) and 46 of 91 rendered short of the wall time they
were worth. That is the audit's software-renderer frame rate, not the player's
setup, and the branch's own §5.2 measured the same thing.

---

## 5. Verdict

> **This verdict is a dated snapshot and has since been discharged.** It was
> written against `2f8a6d0`. Both blockers it names were fixed — F1 by `cb98c39`,
> F3 by `87af349` — and §6 records the post-fix numbers. Read this section for the
> reasoning, not for the branch's current state. Kept verbatim because a verdict
> that gets rewritten once it comes true is not a verdict.

**Not safe to push as-is — two majors block it, and they are both documentation of
behaviour rather than arithmetic.** The branch's numerical core is sound: the
pure functions are tested, `maxfps 30` reproduces the manual's explicit claim
exactly (`30fps 30tk 60.0f/s 2.00f/t`), the D4 reseat is correctly snapshotted,
and `f/s` correctly reads near-60 in every healthy configuration I could build —
including `tickhz=120`, where it held 59.2–62.0 while the tick rate followed the
renderer. Nothing here suggests the *numbers* are wrong.

The blocker is that the branch's two new user-facing claims about `hw` pacing
and `tickhz` are both false as shipped. `model held N` against `N fld` reports
the sim as healthy at the moment it discards 63% of wall time (F1), which defeats
the one instrument `6d02aa1` exists to provide and would mislead exactly the
person the audit was built for. And the settings manual tells the player that
`tickhz` is rewritten to `hw` under `maxfps=-1`, which `run10b` shows it does not
do (F3) — on a branch that already had this recorded as a live defect in §11.3 of
its own design record before the manual asserted the opposite.

F2, F4, F5 and F6 are minor but they are the same genus: a checkable sentence
that execution falsified. Before pushing, fix or withdraw the `model held`/`fld`
comparison, correct the `tickhz` row, and correct the zero-`f/s` and 60.0-band
claims; then re-read every remaining sentence about a measured quantity and
prefer "what I ran" over "what the code implies." That is the specific debt this
branch carries: of the six findings, five were invisible to reading and obvious
on the first run that reached the state in question — and the one I first rated
major from reading alone (F2) turned out to be wrong, which is the same failure
the previous two passes committed. Execution is not the slow way here; it is the
only way that produced a finding I had to retract.

---

## 6. Post-fix verification (added after the fixes landed)

Two follow-up commits address the findings. The numbers below are what a future
session should treat as the baseline to beat — they are otherwise recorded only
in commit messages, and a commit message is not a place anyone looks twice.

**`52807f7` — documentation (F2, F4, F5, F6, and the manual half of F3).**
No behaviour change, so nothing to measure beyond the suite: builds clean,
`ctest` 9/9. The regenerated log legend was read back from a live
`Data/Logs/audio_pump.log` and its line length is unchanged at 174–175 bytes,
which is what keeps the manual's "roughly 10 KB a minute" true.

**`cb98c39` — the stall-cap report (F1).** The hitch line now carries
`credit N of M`, published by `waitForNextFrame` where it clamps.

Measured in the same hw-pacing Dam configuration as `run7`:

- 311 hitch lines; **241 carry `credit`**, and every one of those reports a
  shortfall — 35 to 227 fields per frame, most commonly 45–50.
- The **70 without it are all frames under 133 ms** (max 89.3 ms), i.e. below
  the point where the cap can fire. So the field appears exactly on the frames
  long enough to lose time, and on no others. That is a stronger statement than
  "no false positives": the absent case is correct too.
- On healthy front-end 60 fps frames: **0 of 110** hitch lines carry it.
- `ctest` 9/9, with `tick_stats` grown by the `stall_cap` case (a 367 ms frame
  is 22 measured fields credited 8; cap 0 means uncapped; a sub-field frame
  credits nothing without dividing by zero; a ten-second stall does not wrap).

The before/after is the point: the same windows that read `8 fld model held 4`
— healthy by the old rule — now read `credit 8 of 53`.

**What is still open.** The unverified items in §2. The two design documents
that were cited but did not exist are resolved since this audit was written —
see the last bullet of §2. D5 is no longer among them — see below.

### D5 — decided and fixed (`NEXT_AGENT_V3.md` Task B)

D5 said `tickhz=hw` waits for up to 30 fields and credits at most 8. Under
hardware pacing the credit cap now yields to the hold the pacer waited for:
`portStatsStepCap` (`port/src/tick_stats.c`), applied by
`portTickCreditCap()` in `frametiming.c`. The hold is read from
`g_PortHwFieldsHeld` — the number `portHwPaceFields()` already published — so
`boss.c` gains no parameter and the pacer/clamp contract stays one-way. An
explicit `GE_TICKMAXSTEP` still wins, `0` still means uncapped, and without
`portTickHwPace()` nothing moves, so a window drag is still capped.

Of the three caps on this quantity (§10), only the credit cap moved. The
6-field render cap and the 30-field total cap are left alone, with reasons in
`docs/port/tick-and-field-accounting.md` §5.2.

Measured (macOS, Apple Software Renderer, `GE_STAGE=dam`, `GE_HWSCALE=3` to
price holds above the cap, `cl_audio_log 1`, user's `midas.ini` restored):

```
   8 credit 8 of 52 pacer asked 6     hold under the cap: still clamps at 8
   6 credit 8 of 54 pacer asked 6
   1 credit 12 of 57 pacer asked 12   hold over the cap: the hold is admitted
```

That single `12 of 57` is the fix working: the same frame read `8 of 57`
before. The 14 frames with a 6-field hold clamp at 8 exactly as they should,
because 6 < 8 and the other 46-48 fields are the software renderer, not a
hold. `ctest` 9/9, with `tick_stats` grown by the `stall_cap_hold` policy table
(each clause above is a case).

**Not verified:** real hardware. The raise is exercised here, but the feel of a
correct hw session at the model's 30-field ceiling is not; the 30-field
boundary is covered by the unit test only. Audit §2's hardware items stay
unverified.

Also closed here: the gap `NEXT_AGENT_V3.md` §8.2 recorded. `credit N of M` and
`pacer asked N` now have a shipped explanation — `tools/console_help.txt`, section
THE HITCH LINE, including which of the two to read before blaming the cap.

### F3's code half — resolved (`NEXT_AGENT_V3.md` Task A)

F3 said the manual claimed the game rewrites `tickhz` to `hw` under `maxfps=-1`
and that it does not. The code is now what the manual said the code was — with
the manual corrected to the new behaviour, not the old claim restored
(`tools/midas_ini_help.txt`).

The writer (`videoPersistDerivedTick`, `port/src/video.c`) now writes `hw` when
the running rate is hw, and the live `maxfps=-1` path persists at all — it used
to return before calling the writer. The text comes from `portTickIniValue`
(`port/src/tick_stats.c`), a pure function with four cases under `ctest`
(`tick_stats`: `-1 -> "hw"`, `0 -> "0"`, `60 -> "60"`, `120 -> "120"`, and
`-2 ->` nothing, since `-2` is the coupling's re-derive sentinel and not a rate).

A first cut of this fix was **order-dependent and silently still broken for a
file listing `tickhz` above `maxfps`**; it is recorded rather than removed from
the history because it is the third instance on this branch of a fix verified
on one shape of input. The mechanism is in
`docs/port/tick-and-field-accounting.md` §11.3, with the six executed cases.

Verified: builds clean apart from the pre-existing `__DATA,__common` warning;
`ctest` 9/9; the six ini cases in §11.3 executed from `build_macos` with the
user's `midas.ini` restored (md5 unchanged) after each.

---

## Appendix — predictions, verbatim

Written before each run, unedited. Their titles use my plan-time numbering, which
predates the `runN` artefact names in §4: **plan-run 4 = `run4`**, **plan-run 5
= `run5`**, **plan-run 6 = `run7`**, **plan-run 8 = `run8`**, **plan-run 9 =
`run10b`**.

### audio-pump-audit-pass3-pred-run4.txt

```
PREDICTIONS BEFORE RUN 4 (state 3: GE_TICKHZ=120, title screen, ini maxfps=-1 vsync=0)

Docs under test:
  console_help.txt TK row: "Max FPS sets the rate it aims for ... with vsync on,
  a 60 Hz panel allows 60 ticks a second"; and the UNITS/mode story that a raised
  tick rate sub-ticks one field.
  design record §3, tickhz=N for N>60: "sim steps N/s, presents N/s (if the
  machine keeps up), TPS reads 60, fields/s reads 60".
  gfx_sdl2 comment: "tick 120 pump120, fps120 f/s 60, f/t 0.50 -> working as designed".

Predictions (software renderer: frames ~25 ms in the front end, render_thread=-1
auto turns ON because tickhz != 60):
  tk   ~120   (tick rate is the configured 120, loop paced at 120)
  fps  ~40    (the software renderer cannot present 120; the machine is the limit)
  f/s  ~60.0  (each tick absorbs half a field on average)
  f/t  ~0.50
```

### audio-pump-audit-pass3-pred-run5.txt

```
PREDICTIONS BEFORE RUN 5 (state 3b: raised tick rate against a 60 Hz display, vsync on)

Docs under test:
  console_help.txt TK row: "A display that cannot present that often: with vsync
  on, a 60 Hz panel allows 60 ticks a second, so the default Max FPS of 120
  still reads 60tk and nothing is wrong."
  audio_pump.log legend, tk entry: "Held below the configured rate by the
  display (vsync caps ticks at the refresh rate)".

Setup: ini otherwise as the user left it (maxfps=-1 -> hw, vsync=0).
GE_CONCMD="cl_audio_log 1;cl_maxfps 120;cl_vsync 1" makes tickhz 120 and vsync 1.

Predictions:
  fps ~60, tk ~60, f/s ~60.0, f/t ~1.00, pmp ~120/s
```

### audio-pump-audit-pass3-pred-run6.txt

```
PREDICTIONS BEFORE RUN 6 (hw pacing in a stage: `model held` / `N fld` audit pair)

Setup: default ini (maxfps=-1, tickhz=0 -> hw pacing armed), GE_STAGE=dam,
GE_FRAMES=80, GE_HITCHMS=1, cl_audio_log 1, GE_HWDBG=1.

Docs under test:
  6d02aa1 message: "`fld` is the fields credited to the sim since the previous
  frame (g_PortFieldsTicked), `model held` is what the pacer asked for. Equal is
  healthy; fld short of it is lost simulated time."
  hitchlog.c: "Both cover the same span, the frame that just presented".
  design record D5: any frame priced above 8 fields is credited 8.

Predictions:
  - hitch lines in-stage carry " N tick(s) M fld model held K".
  - The software renderer prices frames well above 8 fields, so M == 8 while
    K > 8, i.e. the audit line shows the D5 loss.
  - During the stage load: no `model held` (the hold is gated on portStageRunning,
    design record 11.4), and f/s is 0 or near 0 for a window.
```

### audio-pump-audit-pass3-pred-run8.txt

```
PREDICTIONS BEFORE RUN 8 (manual's tickhz row: what the game writes for maxfps -1)

Docs under test:
  tools/midas_ini_help.txt, tickhz row: "Worked out from maxfps and rewritten by
  the game; `hw` when maxfps is -1."

Setup: start from the user's ini (maxfps=-1, tickhz=0, vsync=0).
GE_CONCMD="cl_audio_log 1;cl_maxfps -1" forces a live maxfps change so the
coupling and the writer both run with s_CfgTickLoadDone set.

Predictions:
  - The game rewrites midas.ini with maxfps=-1 and tickhz=hw (the manual's claim).
  - f/s settles at 60.0 once the front end is up.
```

### audio-pump-audit-pass3-pred-run9.txt

```
PREDICTIONS BEFORE RUN 9 (in-stage, live maxfps -1 — redo of run 8)

Why run 8 was not a proper test:
  It ran on the title screen, where portTickHwPace() is false (portStageRunning()
  requires current_menu == MENU_RUN_STAGE), so nothing about hw pacing could be
  observed. And the file's maxfps/tickhz values were the same before and after,
  so "wrote tickhz=0" and "wrote nothing" looked identical.

Setup, so that a write is visible:
  midas.ini hand-set to maxfps=120, tickhz=120 (a coherent raised-rate state,
  i.e. NOT hw). Launch GE_STAGE=dam. At the first rendered frame run
  `cl_maxfps -1`, which is the menu's "hardware" choice.
  Restored from the audit backup at the end.

Claim under test (tools/midas_ini_help.txt, tickhz row):
  "Worked out from maxfps and rewritten by the game; `hw` when maxfps is -1."

Predictions:
  - console prints "tickhz=hw - room_cache forced 0" when the stage begins
    (room_cache=1 in the file, design record 11.3).
  - in-stage hitch lines carry `model held N`, so hw pacing really engaged.
  - midas.ini afterwards records maxfps=-1 and tickhz=hw  <- the manual's claim.
  - f/s stays near 60.0 in stage; f/t tracks the hold.
```
