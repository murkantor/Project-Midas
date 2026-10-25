# Ticks, fields, and the numbers in the corner

> **READ THIS FIRST.** This document was written when the block's first row showed
> `FPS` and `TPS`, and it is the reasoning that led to replacing `TPS`. **`TPS` is
> no longer displayed anywhere**: it was removed from the overlay and, in a later
> commit, from `Data/Logs/audio_pump.log` as well. The row now reads
> `fps`, `tk`, `f/s`, `f/t`, and §12 indexes those to the code. Everything below
> that discusses `TPS` is the *evidence for removing it*, kept because the next
> session should not have to re-derive it — not a description of the current
> interface. `g_PortStatTps` still exists as a global (it came in with the
> original import) but nothing reads it.

What `FPS` and `TPS` *used to* measure in the `cl_audio_pump` block and in
`Data/Logs/audio_pump.log`, traced from the CPU counter to the glyph. Written
after a `tickhz=hw` capture where `TPS` tracked `FPS` below 60 and the question
came up whether that is a bug, a warp, or the console.

Short answer: `TPS` is `min(sim tick rate, 60)` — it is *supposed* to fall with
`FPS`, the game clock does not fall with it, and the digit is mislabelled in
three places. Two genuine defects were found next to it: `TPS` goes negative for
one window after every stage load, and `tickhz=hw` waits for up to 30 fields per
frame but credits at most 8. The second one loses simulated time, and is the
only code path found here that can make the world appear to lag or skip.

Status: all of this is read-and-computed, not captured. No runtime trace and no
access to the footage. See §9 for the exact split.

---

## 1. The trace

One wall-clock interval, followed end to end. Every hop names the file and the
expression as written. In the files this branch kept editing, the symbol is the
anchor rather than a line number; stable files keep their line references.

**T0 — the clock.** `OS_CPU_COUNTER` = `OS_CLOCK_RATE * 3 / 4` = **46,875,000
ticks/s** (`include/PR/os.h:724-725`). `osGetCount()` is the only time source in
the chain; nothing below reads `SDL_GetTicks` or the wall clock.

**T1 — what a field is.** A 60 Hz video field is
`PORT_TICK_FIELD_COUNTS` = **781,250 counts** (`src/game/frametiming.c`,
`PORT_TICK_FIELD_COUNTS`; `937,500` under `REFRESH_PAL`). That is
`OS_CPU_COUNTER / 60`. Rare's own accounting used a *short* field, `775,875`
counts (`frametiming.c`, `PORT_TICK_FIELD_GATE`), which at
46.875 MHz is 16.552 ms → **60.42 fields/s**. The port keeps both constants and
uses them for different jobs; see §8.

**T2 — admission.** The main loop admits one sim tick when the retrace has
elapsed at least the tick interval (`src/boss.c:649-650`):

```c
if (mainTickElapsed < ((portTickHz() == 60) ? (u32) (MAIN_LOOP_TICK_INTERVAL)
                                            : portTickGateCounts()))
```

with `MAIN_LOOP_TICK_INTERVAL` = `390625 - 2688` = **387,937 counts**
(`boss.c:103-106`) at `tickhz=60`, and
`portTickGateCounts()` = `(781250 * 60 / hz) / 2` otherwise
(`frametiming.c`, `portTickGateCounts()`). Admitted ticks are counted in
`g_PortTicksAdmitted`
(`boss.c:660`).

**T3 — measuring.** The admitted tick calls `waitForNextFrame()`
(`boss.c:701`). It computes `elapsed = now - copy_of_osgetcount_value_1`,
**banks** it in `portTickFieldAccum`, and divides out whole fields
(`frametiming.c`, `waitForNextFrame`; the classic carry arm and the non-60 Hz
arm perform the same arithmetic):

```c
portTickFieldAccum += elapsed;
whole = (s32) (portTickFieldAccum / PORT_TICK_FIELD_COUNTS);
portTickFieldAccum -= (u32) whole * PORT_TICK_FIELD_COUNTS;
```

`whole` is the number of 60 Hz fields that elapsed during the *previous* frame.
The remainder stays banked, so no time is lost to rounding.

**T4 — publishing the field count.** `updateFrameCounters(whole)`
(`frametiming.c`, `updateFrameCounters`) does two different things with that one
number:

```c
speedgraphframes = deltaFrames;      /* fields */
portTickDelta    = (f32) deltaFrames; /* fields, as float */
```

`portTickDelta` is then overwritten in the non-60 Hz arm by the *measured
fractional* delta (`frametiming.c`, the at-rate arm of `waitForNextFrame`), which
is what raised rates integrate motion with.

**T5 — the engine clock.** `lvlTick()` copies it to the game's own variable
(`src/game/lv.c:1518`):

```c
g_ClockTimer = speedgraphframes;
```

**T6 — the counter the overlay divides.** Two lines later, `lv.c:1526`:

```c
if (g_ClockTimer > 0)
    g_GlobalTickCount += 1;
```

**`+= 1`, not `+= whole`, and the `if` is a port addition** (vanilla increments
unconditionally). This single line is why `TPS` is capped at 60: a tick that
absorbed three fields still counts as one tick.

**T7 — game time.** Separately, and correctly (`lv.c:1565`):

```c
g_GlobalTimer += g_ClockTimer;
```

Game time advances in **fields**, not in ticks. This is the line that keeps the
mission clock and every `g_ClockTimer`-scaled quantity at 60 units/s regardless
of what `TPS` reads.

**T8 — the stat window.** `gfx_sdl_update_title_stats()`
(`port/fast3d/gfx_sdl2.cpp`) runs on the render thread after every
`SDL_GL_SwapWindow`, and every ≥500 ms computes both digits from
*different* counters:

```c
int fps = (int)((stat_frames * 1000 + dt / 2) / dt);                        /* presented frames  */
int tps = (int)(((g_GlobalTickCount - stat_last_tick) * 1000 + dt / 2) / dt); /* clock-advancing ticks */
```

then publishes them to `g_PortStatFps` / `g_PortStatTps`
(`frametiming.c`, their definitions).

**T9 — the display.** Two consumers read those two globals, and nothing else
computes them. At the time of writing:

- `portDrawAudioPumpStats` → `sprintf(line[0], "FPS %dfps TPS %dtps", ...)`.
- `portAudioLogLine` → `"t=%ums FPS %d TPS %d ..."`.

Both formats have since changed: the block's first row is
`"%dfps %dtk %d.%df/s %d.%02df/t"` and the log line is
`"t=%ums FPS %d tk %d f/s %d.%d f/t %d.%02d ..."` — no `TPS` in either. See the
note at the top. Both still read the same globals, so the screen and the file
cannot disagree — that part of commit `3a1ab62` is sound.

**The trace in one line:**

```
osGetCount ─▶ elapsed ─▶ /781250 ─▶ whole fields ─┬─▶ speedgraphframes ─▶ g_ClockTimer ─▶ g_GlobalTimer += (game time, 60/s)
                                                  │                    └─▶ if (>0) g_GlobalTickCount += 1 ─▶ TPS
                                                  └─▶ portTickDelta ─▶ g_GlobalTimerDelta (motion, tick rate)
                    presented frames ─────────────────────────────────────────────────────▶ FPS
```

`FPS` counts presents. `TPS` counts *ticks that advanced the clock by ≥ 1
field*. `g_GlobalTimer` counts fields. Three different quantities, one shared
word.

---

## 2. What the two digits mean

### 2.1 TPS = min(sim tick rate, 60)

Consequence of T6 plus the fact that any tick which banks at least one whole
field increments by exactly one:

- **Frames slower than 60 fps** → every tick absorbs ≥ 1 whole field, so every
  tick counts. `TPS == FPS`, exactly.
- **Ticks faster than 60 Hz** (`tickhz=120`, `144`, or `0` = uncapped) → most
  ticks absorb 0 fields (`g_ClockTimer == 0`), so only the ~60 ticks/s that
  complete a field count. `TPS == 60` while the sim really steps 120 or 144
  times.

Verified by simulating the T3 arithmetic (781,250 counts/field, integer counts,
one loop tick per frame — the same code with the same truncation):

```
mode / frame time            fps     TPS shown   fields/s   fields/tick
tickhz=60, 60.0 fps          60.0       60.0       60.00       1.00
tickhz=60, 50.0 fps          50.0       50.0       60.00       1.20
tickhz=60, 40.0 fps          40.0       40.0       60.00       1.50
tickhz=60, 33.3 fps          30.0       30.0       59.98*      2.00
tickhz=60, 20.0 fps          20.0       20.0       60.00       3.00
tickhz=120, 120 fps         120.0       60.0       59.99       0.50
tickhz=144, 144 fps         144.0       60.0       59.99       0.42
tickhz=0, ~533 fps          533.0       60.0       59.99       0.11
```

\* quantization: the simulation feeds whole counts, and 33.333 ms does not
divide evenly. Not drift.

The `fields/s` column is the point. It is 60.00 in every row: **game speed is
correct in all eight cases**, including the 20 fps one where `TPS` reads 20.

### 2.2 `fields/s` is the speed-correctness number

`TPS` conflates two things — how often the sim steps, and how much time each
step covers. `fields/s` (i.e. `Δg_GlobalTimer`) answers the only question that
matters for correctness: is simulated time advancing at wall-clock rate? It
should read ~60.0 in every healthy mode, console or port, at any tick rate.

"~60.0" is looser than it looks. The figure is integer fields over a window that
is never exactly 500 ms, so healthy readings scatter a few percent: measured
across four configurations on 2026-10-25 (60 Hz, `tickhz=120`, `maxfps 30`, and
hw pacing), the healthy range was **59.3 to 62.9**. Treat the last digit as
window jitter and only a sustained departure from that band as a fault — the
narrative below says "60.0" where it means "in that band".

At the time of writing that quantity was **not plumbed to any display.**
`g_GlobalTimer` is internal to `lv.c`, and both consumers only saw
`g_PortStatFps`/`Tps`. It is now the `f/s` field (§7).

### 2.3 Why they differ at all

`tickhz=hw` is not an accident of the port. It is the closest reproduction of
the console, and on the console `TPS == FPS` *by construction*: one `lvlTick` per
presented frame, and a frame that overruns a vblank is presented late rather than
dropped, so `g_ClockTimer` becomes 2 or 3 while `g_GlobalTickCount` advances by 1.
A 20 fps console view therefore showed `FPS 20 / TPS 20 / 3 fields per tick`,
with the mission clock still at 60 fields/s.

That is exactly what the capture showed. `TPS` tracking `FPS` below 60 is not a
symptom; it is the fidelity target being met.

---

## 3. The four modes

**`tickhz=60`** — the fallback when nothing is configured, and what `maxfps`
1–59 derives to. A default install does not run this: `s_CfgMaxFps` starts at 120
(`port/src/video.c`), so the coupling derives `tickhz=120` and writes it. One tick
per present, `MAIN_LOOP_TICK_INTERVAL`; `FPS` = presents, `TPS` = presents,
`fields/s` = 60 if the machine keeps up. Faithful to the console, and below
60 fps this *is* the console's slowdown.

**`tickhz=N` for N > 60** (120, 144, 165, …): sim steps N/s, presents N/s
(if the machine keeps up), `TPS` reads 60, `fields/s` reads 60, motion
integrates at N/s through `g_GlobalTimerDelta`. Discrete 60 Hz logic (AI,
animation, ammo, countdowns) stays at 60 via `g_ClockTimer > 0` guards. The
`TPS 60` digit here is the most misleading of the four cases, because the sim is
demonstrably *not* running at 60.

**`tickhz=hw`** (`midas.ini tickhz=hw`, or Max FPS = hardware; parsed by
`videoSetDisplayOption`, the `tickhz` key in `port/src/video.c`, which calls
`portTickSetHz(60)` +
`portTickSetHwPace(1)`): classic 60 Hz sim, but each frame is held for the number
of fields a linear RDP-workload model predicts (`portHwPredictMs()` in
`frametiming.c`; hold at `boss.c:599-606`). `FPS` = model's rate, `TPS` =
`FPS` below 60. This is the mode in the capture.

**`tickhz=0`** (uncapped): tick rate pinned to a 1000 Hz ceiling
(`frametiming.c`, `portTickGateCounts()`, which clamps uncapped to 1000 Hz),
`TPS` reads 60, `fields/s` 60.
The old reference sample in `tools/console_help.txt` (`FPS 533fps TPS 60tps`)
was this case; the manual now shows `738fps 738tk 60.0f/s 0.08f/t` for it.

---

## 4. Where the labels were wrong

The shipped manuals and the comments below all said something the code does
not do. All were documentation-only; none changed behaviour. **State as of the
last commit: two of the three wrong labels listed here are fixed, one is still
live.**

- **`src/game/lv.c`** (`g_GlobalTickCount += 1`) — *"must count 60Hz fields, not
  ticks ... Always true at GE_TICKHZ=60"*. **Still live.** True only at ≥ 60 fps.
  Below 60 it counts **ticks**, each worth 2–3 fields. The hw-pacing
  counterexample is exact: at 15 fps `g_ClockTimer` is 4 and `g_GlobalTickCount`
  still advances by 1.
- **`port/fast3d/gfx_sdl2.cpp`** — *"rendered fps + engine FIELDS/sec"*.
  **Fixed**: replaced by a WARNING block that says what the counter really is.
  `g_GlobalTickCount` is fields/s only when frames fit inside one field; in the
  hw capture it was ticks/s.
- **`tools/console_help.txt`** — *"TPS — Simulation steps per second. Normally 60,
  whatever FPS is"*. **Fixed, and then some**: the entry is gone, because `TPS`
  is gone from the overlay and the log. Wrong in both directions while it lasted:
  at `tickhz=120` the sim steps 120/s while the digit said 60, and below 60 fps
  the digit was neither 60 nor the tick rate but the frame rate. This was the one
  users read.
- **`port/include/port_audio_stats.h`** is right and worth preserving: the frame
  and tick rates are deliberately not copied into the pump snapshot, so the block
  cannot disagree with the numbers it displaces.

---

## 5. Two real defects

### 5.1 D4 — `TPS` prints a negative number for one window after every stage load

`lvlStageLoad()` zeroes the counter (`src/game/lv.c:779`):

```c
g_GlobalTickCount = 0;
```

The stat window's baseline is a `static` in another translation unit
(`gfx_sdl2.cpp`, the `stat_last_tick` static) and is never rebaselined.
The first window closing after the load computes `0 - stat_last_tick`, so the
published value is **-2 × the pre-load tick count**:

- 30 pre-load ticks (0.5 s of play) → `TPS -60`
- 300 pre-load ticks (5 s of play) → `TPS -600`
- 1200 pre-load ticks (20 s of play) → `TPS -2400`

Duration: one window, ≤ 500 ms. It reaches the overlay *and* `audio_pump.log`,
which samples at 1 Hz (`audio_pc.c`, `portAudioLogLine`) — so a report can carry
`TPS -600` as a permanent artifact of the load it happened next to.

This is a display bug that only became visible when the overlay started
publishing the value; the title bar reads the same `tps` local and had it too,
behind `GE_TITLESTATS`.

Minimal fix, at the point of publication (`gfx_sdl2.cpp`, the stat pass): if
`g_GlobalTickCount < stat_last_tick`, publish 0 (or skip the window) instead of a
negative. It is a reset, not a measurement.

### 5.2 D5 — `tickhz=hw` waits for up to 30 fields but credits at most 8

Two independent caps, in different files, disagreeing about the longest frame
the mode will produce:

- **The wait.** `portHwPaceFields()` prices the frame at
  `ceil(ms / 16.667)` and clamps **1..30 fields** (`frametiming.c`,
  `portHwPaceFields`), after a 6-field cap applied to the render model alone.
  `boss.c:600` then holds the tick for that many fields.
- **The credit.** The carry arm — the arm `tickhz=hw` uses, since
  `portTickHz() == 60` — clamps the measured elapsed time
  (`frametiming.c`, the stall cap in `waitForNextFrame`):

```c
if (portTickMaxStep() > 0 && elapsed > (u32) portTickMaxStep() * PORT_TICK_FIELD_COUNTS)
{
    elapsed = (u32) portTickMaxStep() * PORT_TICK_FIELD_COUNTS;
    portTickFieldAccum = 0;
}
```

`portTickMaxStep()` defaults to **8 fields** (`frametiming.c`,
`portTickMaxStep`), i.e. 133 ms. *(The clamp now asks
`portTickCreditCap()`, which is this number unless a hw-pacing hold was larger —
see "D5 is DECIDED" below. The snippet above is the defect as it was found.)*

So for any frame the model prices at N > 8 fields, the loop **waits** the full
N fields and then **discards** (N − 8) of them. At the model's own ceiling
(N = 30, 500 ms) that is 22 fields = **367 ms of simulated time thrown away per
frame**. The stage-load charges make N > 8 easy to reach: `portHwNoteLoad`
prices cart DMA plus inflate, and `portHwLoadStageLoaded` bills a whole stage
load's decode and setup time.

Why this is not "the same as the console": the console has no stall cap. Whatever
number of fields elapsed is the number credited, always. The cap is a port-only
invention with a legitimate purpose — stop a 90-second window drag from being
simulated as one enormous step — but it cannot tell a *deliberate* N-field hold
from an *unintended* stall, so it truncates the hold it was given.

Signature, if it is firing: hitches clustered at room transitions, weapon swaps,
and heavy views, where the world advances less than wall time did. Confirm with
`GE_HWDBG=1` (`frametiming.c`, `portHwPaceFields` and the load-charge path) and
look for `-> N fields` with
N > 8.

The same clamp sits on the non-60 Hz arm (`frametiming.c`, the at-rate arm of
`waitForNextFrame`), so at
`tickhz=120` a frame longer than 133 ms loses time identically — the hw mode is
just where it is easy to trigger on purpose.

#### D5, observed (macOS, 2026-10-25)

Confirmed live while implementing option C. A launch from a non-GUI shell got a
software GL context (`GL: renderer: Apple Software Renderer`), so frames cost
~466 ms. `cl_audio_log 1` then wrote, steadily:

```
t=16418ms FPS 2 TPS 2 tk 2 f/s 16.7 f/t 8.00 pmp 2/s rnd 2/s \
          que 43264/43217B 491/490ms cap 47313B \
          snd 21248/21079smp 964/956ms dry 0 ... dev 22050Hz
```

`f/t 8.00` is pinned to the cap exactly — not near it, on it — and
`f/s 16.7` is 28% of 60. Every tick is credited 8 fields and then loses the
remainder (~28 fields of wall time per 466 ms frame). The note that `f/t` is
integer-rounded (`fields*100/ticks`) is why it reads a clean 8.00.

The audio row is the interesting half: `snd 964/956ms` says the **mixer is
keeping real time** while `f/s 16.7` says the **world is running at 28%**. Music
is sequenced by the audio library (CSP/`ALSeqPlayer`, advanced from
`alAudioFrame` in `src/libultra/audio/synthesizer.c`), so the
music plays at full speed and the world does not. Anything the player perceives
as "the audio is stretched / the music is running away from the intro" is this
mismatch, not the pump. `que 491ms` against the 80 ms target is the pump
over-compensating for the `dry 2` starves at the start.

This also settles the label question from §2: at 2 fps the old row read
`FPS 2fps TPS 2tps`, which looks like a broken tick counter. The new row reads
`2fps 2tk 17.0f/s 8.00f/t` and names the defect in one glance.

Note the renderer line: `tickhz=hw` was configured but `portTickHwPace()` was
false at stage 90 (`portStageRunning()` is false outside a stage), so no hold was
applied and the 466 ms was pure software rasterisation. The cap fired anyway —
it is not a hw-mode-only defect.

#### D5 is DECIDED and fixed (2026-10-25, NEXT_AGENT_V3.md Task B)

The clamp was confirmed and the loss was real. The section above is kept as it
was written, because it records the objection the decision had to answer.

**The decision.** Under hardware pacing the credit cap yields to the hold: a
tick is credited `max(cap, hold)` fields, so the N fields `boss.c` waited for
are the N fields the simulation receives. The policy is one pure function,
`portStatsStepCap` (`port/src/tick_stats.c`), with its whole table under
`ctest` in `port/tests/tick_stats.c`.

The counter-argument above — thread the expectation through and you add a
contract that can be wrong — is answered by *where* the hold is read rather
than by asserting it is safe. `boss.c` does not call into the credit path at
all. `portHwPaceFields()` already published the hold it priced
(`g_PortHwFieldsHeld`, added by `6d02aa1` for the hitch log), so the clamp
reads a number the pacer wrote. The data flow stays one-way: pacer publishes,
clamp reads. No new parameter, no new call site, nothing for `boss.c` to get
wrong.

What keeps the cap honest, and each of these is a test case:

- **Not a blank cheque.** The raise is only to the hold, and `portHwPaceFields`
  caps the hold at 30 fields. A 90-second window drag cannot become one
  90-second step.
- **Only under hw pacing** (`portTickHwPace()`, not the configured tick rate).
  A drag in a menu, in a demo, or outside a stage has no priced hold behind it
  and still clamps at 8. `portTickHwPace()` is already false for all three.
- **An explicit `GE_TICKMAXSTEP` is obeyed exactly.** It is how this cap is
  measured, so the mechanism must not override the experiment measuring it.
- **`0` still means uncapped**, never "cap of zero".

**The third position was not taken.** "Make the hold authoritative: admit N
without re-deriving it" is what this does *not* do, and the reason is the same
one that made the original cap necessary: the elapsed wall clock is still
measured and still compared. The hold only sets the ceiling; if the frame ran
longer than the hold, the excess is still dropped and still reported. A patch
that trusted the hold blindly would simulate time that never elapsed.

**The three caps (§10), decided together, explicitly:**

- **The 8-field credit cap** — raised to the hold under hw pacing. This is the
  one that was silently truncating deliberate holds.
- **The 6-field render cap** — **left alone.** It bounds the *fit*, not the
  tick: it was fitted against hardware footage and nothing here can improve on
  that fit. §10's open question is whether that footage contained load hitches;
  it remains open, and it is a question about the fit's accuracy, not about
  whether a priced hold may be credited. Load charges are added *after* this
  cap (`portHwPaceFields`), so a frame priced above 8 fields is always a load
  charge on top of a saturated render model — which is why the raise only ever
  fires on load frames in practice (measured below).
- **The 30-field total cap** — **left alone**, and it is now the bound on the
  raise too. It is a safety limit on the model's own output, not a credit rule.

**Verified by execution** (macOS, Apple Software Renderer, `GE_STAGE=dam`,
`GE_HWSCALE=3` to push priced holds above the cap, `cl_audio_log 1`, the user's
`midas.ini` restored afterwards):

```
   8 credit 8 of 52 pacer asked 6     <- hold under the cap: still clamps at 8
   6 credit 8 of 54 pacer asked 6     <-   (the 52 fields are the renderer)
   1 credit 12 of 57 pacer asked 12   <- hold over the cap: the hold is admitted
```

One frame in fifteen carried a hold above 8, and that frame is the one whose
credit changed from 8 to 12. `GE_HWSCALE=3` also shows *why* holds are rarely
above 8 in practice: scaling the render model 3x saturates it against its own
6-field cap, so a hold of 12 is 6 render fields plus a ~100 ms load charge. The
cap's raise therefore lands on load frames, which is exactly the population D5
was about.

**Not verified.** Real hardware. Every number above comes from a software
renderer where frames cost ~890 ms, so the renderer, not the model, sets the
frame length; the raise is exercised (the hold exceeds the cap and is admitted)
but the *feel* of a correct hw-pacing session at 30 fields is not reproduced
here. The 30-field boundary is covered by the unit test only. Windows and Linux
not run.

Fixed by the earlier commit: the hitch line now reports the clamp at the clamp
site —
`credit N of M`, e.g. `credit 8 of 53` — instead of inviting a comparison of
`fld` against the model's hold, which did not measure the loss (see the
third-pass audit, `docs/port/audio-pump-audit-pass3.md` F1). The model's figure
is renamed `pacer asked N` so it is not read as a baseline for the credit.
`tools/console_help.txt` now documents both fields for the first time
(`NEXT_AGENT_V3.md` §8.2): `credit` was the branch's only per-frame evidence of the loss and
had no shipped text at all.

### 5.3 Two defects in the fix itself, found by review

Both were in the first cut of the implementation of §7 option C, and both are
worth recording because they are the same *class* of error the change exists to
remove: a number that quietly means something other than its label.

**D6 — `f/s` read a healthy 60.0 during every pause.** `g_PortFieldsTicked` was
first advanced in `updateFrameCounters`, from `deltaFrames`. But
`updateFrameCounters` runs *before* `lvlTick` (`boss.c:701` then `:735`), and
`lvlTick` zeroes `g_ClockTimer` when `g_ControlsLockedFlag != 0` or
`checkGamePaused() != 0` (`lv.c:1502-1513`). Those fields are produced and then
discarded, so counting at the point of production made `f/s` report "game speed
correct" for a frozen simulation — a false healthy on the one number that is
supposed to be trustworthy. It is now advanced in `lv.c` beside
`g_GlobalTimer += g_ClockTimer`, so the two are the same sum by construction.
Consequence for readers: `f/s` now reads 0 while paused, which is honest, and
the manual says so.

**D7 — the D4 guard could still publish a negative TPS.** The reseat and the
subtraction each read `g_GlobalTickCount` live, and the stat pass runs on the
render thread when `render_thread=1` while the game thread writes the counter.
A stage-load reset landing between the two reads reproduces exactly the publish
D4 exists to prevent (1200 seen by the first read, 0 by the second: −2400). The
value is now snapshotted once into `tickNow`. All the stat pass's reads of
`g_GlobalTickCount`, `g_PortFieldsTicked` and `g_PortTickSerial` are relaxed
atomic loads for the same reason — they are independent samples, so relaxed is
sufficient, but a plain read beside an atomic write is a data race by the
letter of C11.

Neither was caught by the build or the tests, and D7 was reported as fixed in a
commit message before it was actually in the tree. Both were found by a
reviewer reading the code; §9's "verified" claims should be read with that in
mind — a green suite says nothing about a two-instruction race.

---

## 6. Is it the warp?

Ranked by mechanism. The footage was not available, so this is inference from
code, not observation.

**H1 — D5, lost fields during a hold.** The only path found that loses
simulated time. Matches a visible skip or lag around loads and busy views.
Testable in one run.

**H2 — 60 Hz gates keyed on tick parity, not field parity.** These all assume
one tick = one field: `chraction.c:5383` (`& 1`), `bondview2.c` (`% 3` twice),
`propobj.c:5325` (`& 7`), `:5331` (`& 0xF`), `:5342` (`% 40`),
`gunfire.c:3130` (`(g_GlobalTickCount - s->stamp) == 1`). At 2–3 fields per tick
these effects fire at 1/2 or 1/3 the wall-clock rate of a 60 fps view. **This is
fidelity, not a port bug** — Rare's own 20 fps views did exactly this — but it is
inherited deliberately by `tickhz=hw`, so oddities here are the console's.

**H3 — every port smoothing path is off in hw mode, correctly.**
`portSmoothOn()` returns 0 at `tickhz == 60` (`frametiming.c`, `portSmoothOn`),
so
`portAnimTicks`, `portLagStep`, `portTickFieldPhase` and `portRenderFieldPhase`
all collapse to whole-field steps. `portFastTickFixOn()` explicitly excludes hw
pacing (`frametiming.c`, `portFastTickFixOn`), so the fast-tick door and vehicle
collision fixes do not run. Coarse — not warped — but Bond at delta 3 is the vanilla tunnelling
case.

Note that the number that separates H1 from H2/H3 — **fields per tick** — was
the one number neither the overlay nor the log showed at the time; it is now
`f/t` in both.

---

## 7. Options

**A. Label only.** Rename `TPS` and correct the three texts in §4. Minutes, no
behavioural risk, stops the next session re-deriving this. Does not fix D4/D5 and
does not make the warp visible.

**B. Add a real field counter.** `g_PortFieldsTicked += whole` in
`updateFrameCounters` (`frametiming.c`, `updateFrameCounters`), published beside
`Fps`/`Tps`. Show
`FPS 60fps SIM 60.0f/s`. Fields/s is the number that means "game speed is
right". Real tick rate is already free: `g_PortTicksAdmitted` (`boss.c:57`,
`:660`) — it just needs a second baseline in the stat pass.

**C. The health triple (recommended).** One row that cannot mislead:

```
533fps 60tk 60.0f/s 1.00f/t
```

presented frames, admitted ticks, fields credited, fields per tick.
`f/t > 1` reads unambiguously as "frames are overrunning a field and game time
is being absorbed"; `f/s != 60.0` reads as "the sim is losing time", which is D5
and is invisible without it. Costs one `u32`, one field in the stat pass, and two
`sprintf` slots — and it makes A unnecessary, since the ambiguous name is gone.

> **Status: implemented** (2026-10-25).
>
> The arithmetic lives in `port/src/tick_stats.c` (`port/include/tick_stats.h`)
> as pure functions, following the `audio_pace` pattern, so it is tested without
> a window: **`port/tests/tick_stats.c`** covers the healthy 60 Hz window, a
> 120 Hz window (rate 120, `f/s` still 60.0, `f/t` 0.50), the stalled window the
> live run produced (8 fields, 1 tick, 479 ms → `f/s 16.7`, `f/t 8.00`), zero
> ticks, a zero-length window, rounding, and the D4 reset.
>
> Plumbing: `g_PortFieldsTicked` (relaxed atomic, advanced in `lv.c` beside
> `g_GlobalTimer += g_ClockTimer`, so it equals the fields the sim actually
> applied — see §5.3), `g_PortStatTicks`,
> `g_PortStatFieldsX10`, `g_PortStatFieldsPerTickX100` in `frametiming.c`/`.h`;
> the stat pass in `gfx_sdl2.cpp` snapshots `g_GlobalTickCount` once and
> rebaselines it through `portStatsReseatTickBaseline` (D4); the overlay row is
> `bondview2.c` `portDrawAudioPumpStats`; the log line and legend are
> `audio_pc.c`; the hitch line carries `N fld`, `credit N of M` when the stall
> cap fired (published at the clamp), and, under hw pacing, `pacer asked N` read
> from `g_PortHwFieldsHeld`.
>
> `tk` is **`g_PortTickSerial`** (incremented inside `lvlTick`), not boss.c's
> `g_PortTicksAdmitted`: the latter is incremented *before* the
> `pendingGfx < 2` backpressure guard, so under gfx backpressure it counts ticks
> that never reached the sim, which would make `f/t` read below 1.00 at
> `tickhz=60` and look like sub-ticking. `g_PortStatTps` came in with the original import, is still
> computed, and is documented at its definition; nothing reads it now that the
> row and the log carry `tk` and `f/s` instead.
>
> Verified: builds clean on macOS (no warnings in the touched files); ctest
> **9/9** with the new `tick_stats` target; live runs through `GE_CONCMD`
> reproduce `f/s 16.7 f/t 8.00` before and after the pure-function refactor, so
> behaviour is unchanged. (The exact figure moves in the last digit with the
> window length — 16.7 to 16.8 across runs; it is the pinned `f/t 8.00` that
> identifies the cap.) The overlay's glyph row was **not** captured: the only
> runs available were software-rendered at ~2 fps and the capture landed on the
> intro, where the HUD overlay does not draw. The row is a `sprintf` of the same
> four globals the verified log line prints, and the draw calls around it were
> not modified.

Add to C, if the hw model is to be trusted against footage: in `portTickHwPace()`
mode, log the model's field hold beside the fields actually credited, per hitch.
That single line answers "is our console model right" per frame, which is the
real question a `tickhz=hw` capture is asking — and it is also the cheapest
instrument for H1. **(Done: `credit N of M` / `pacer asked N` in the hitch line.
The first cut printed `N fld` beside `model held N` and invited a comparison of
those two; it read healthy at exactly the moment the most time was being
dropped, because on a renderer slower than the model the frame spans far more
wall time than either number. See `docs/port/audio-pump-audit-pass3.md` F1.)**

Whatever is chosen, **D4 first**: a diagnostic file that can publish `TPS -600`
undercuts the rest of the file.

---

## 8. Constants and knobs, for reference

- **`OS_CPU_COUNTER`** — 46,875,000. `include/PR/os.h:725`.
- **`PORT_TICK_FIELD_COUNTS`** — 781,250 (NTSC), 937,500 (PAL). The *true*
  field, `OS_CPU_COUNTER / refresh`. Used for banking and credit.
  `frametiming.c`, `PORT_TICK_FIELD_COUNTS`.
- **`PORT_TICK_FIELD_GATE`** — 775,875 (NTSC), 931,050 (PAL). Rare's *short*
  field, 60.42 fields/s. Used only as the front-end 60 Hz hold and the intro
  deadline (`portTickFieldGateCounts`, `frametiming.c`; consumers
  `boss.c:621-623`, `title.c:674`). Faithful to vanilla's own rate; the 0.69%
  offset versus `PORT_TICK_FIELD_COUNTS` is inherited, not introduced.
- **`MAIN_LOOP_TICK_INTERVAL`** — 387,937 counts (`boss.c:106`), the
  `tickhz=60` admission threshold: half a field minus slack.
- **`portTickGateCounts()`** — `(781250 * 60 / hz) / 2`, the same half-field
  rule at any tick rate; a 1000 Hz ceiling stands in for uncapped.
  `frametiming.c`, `portTickGateCounts()`.
- **`portTickMaxStep()`** — default 8 fields (133 ms), `0` = uncapped.
  `GE_TICKMAXSTEP`. `frametiming.c`, `portTickMaxStep()`. Stall cap; see D5.
- **`portTickCreditCap()`** — the cap a tick is actually credited against:
  `portTickMaxStep()`, raised to the hw-pacing hold (`g_PortHwFieldsHeld`) when
  that is larger and hw pacing is in force; 0 stays 0; an explicit
  `GE_TICKMAXSTEP` wins. Policy in `portStatsStepCap()` (`tick_stats.c`).
  `frametiming.c`, `portTickCreditCap()`. See D5.
- **`portHwPaceFields()`** — 1..30 fields, render model capped at 6 before load
  charges. `frametiming.c`, `portHwPaceFields()`.
- **`portTickHz()`** — from `GE_TICKHZ` (wins) → `midas.ini tickhz` → 60 as the
  fallback. Forced to 60 during demo recording.
- **`g_ClockTimer`** — whole fields this tick (`lv.c:1518`). Discrete logic gates
  on `> 0`.
- **`g_GlobalTimerDelta`** — fractional fields, measured (`lv.c:1553`). Motion
  integrates on this.
- **`g_GlobalTickCount`** — clock-advancing ticks (`lv.c:1526`). Reset per stage
  load (`lv.c:779`).
- **`g_PortTicksAdmitted`** — admitted ticks, monotonic since boot
  (`boss.c:660`). Never displayed.
- **`g_GlobalTimer`** — fields, monotonic (`lv.c:1565`). The number that should
  be on screen.

---

## 9. Verified, and not

**Verified by reading, against the source:** the whole trace T0–T9; the cap
mismatch in D5 both sides; the reset-without-rebaseline in D4; the three
mislabels in §4; the four modes in §3.

**Verified by computation:** the §2.1 table, by re-implementing the T3 banking
arithmetic with integer counts; the D4 magnitudes and the D5 loss (N − 8 fields,
367 ms at N = 30) arithmetically; and, since the implementation, by
`port/tests/tick_stats.c` under ctest.

**Verified at runtime (2026-10-25, macOS):** D5 fired, and on the first run of
the new row. Details in §5.2. The window read `f/s 16.7 f/t 8.00` — `f/t` pinned
to the `GE_TICKMAXSTEP` cap exactly — while `snd 948/950smp` showed the mixer
keeping real time. That contrast is the mechanism behind "the music runs away
from the world": audio is sequenced in rendered samples, so it stays at wall
rate while the sim loses 28% of its time.

Note the conditions of that run, because they bound what it proves: the GL
context was `Apple Software Renderer` (launched from a non-GUI shell), frames
cost ~466 ms, and `tickhz=hw` was configured but **inactive** at that stage, so
no deliberate hold was involved. It demonstrates the cap is not hw-mode-only.

**Not verified:** the overlay's glyph row was not captured from my runs
(software-rendered at ~2 fps, and the capture landed on the intro where the HUD
overlay does not draw); it is covered instead by the user's own screenshots. No
`GE_HWDBG=1` per-frame trace of a hw-mode gameplay run. The D4 fix is covered by
unit test and by 98 consecutive live log lines with no negative value, but no
reset was observed mid-capture.

**Superseded:** the earlier note here said "whether D5 fires on a healthy machine
is still open". It has since been measured, and it does not. See §11.1.

## 10. Open questions

- `docs/port/n64-hw-pacing.md` now exists: the model, its nine coefficients,
  the load rates, the fit's provenance, and the open question about the
  near-points term. It was written from §11 plus `frametiming.c`; §11 stayed
  here, and that document's header records why. `docs/port/tick-rate-binding.md`
  was **not** written — the tick-rate binding is §13, because a second document
  would have restated §2 and §3 and then drifted from them. Both citations from
  `frametiming.c`, `ramromreplay.c` and `video.c` resolve now.
- The 6-field render cap versus the 30-field total cap in `portHwPaceFields()`
  versus the 8-field credit cap in `portTickMaxStep()` (all `frametiming.c`) is
  three caps for one quantity.
  Whether the render cap was fitted against footage that already contained
  load hitches is worth knowing before changing any of them.

  **Decided (Task B, 2026-10-25):** only the credit cap moved, and only under
  hardware pacing; the 6-field render cap and the 30-field total cap are left
  alone, with the reasons in §5.2. The render-cap question above is still open
  and is still about the quality of the fit — it is not a licence to credit
  fewer fields than the pacer waited for, which is what D5 was.
- `g_PortTicksAdmitted` was suggested here as the counter to publish for `tk`.
  That was wrong: it is incremented before the `pendingGfx < 2` backpressure
  guard, so it counts ticks that never reached `lvlTick`. `tk` publishes
  `g_PortTickSerial` instead. `g_PortTicksAdmitted` remains correct for its own
  purpose (title.c's per-admitted-tick stepping).

## 11. hw pacing: what a real session turned up

Measured 2026-10-25 from a user session (`midas.ini` `maxfps=-1`, `tickhz=0`,
`room_cache=0`; 98 log lines; screenshots at 832 fps uncapped and at 15 fps in a
level). This section is about the **hw model**, not the tick accounting of
§1–§5. It is the measurement record for `docs/port/n64-hw-pacing.md`, and it
stays here by decision: three of its four subsections are about the tick and the
ini rather than the RDP model, and moving them would renumber references this
document and `audio-pump-state-of-play.md` already cite. The model itself, and
its provenance, live in that document.

### 11.1 D5 does not fire at ordinary frame rates

The session held 15 fps for a long spell with `f/t 4.00` and `f/s 59.8–60.5`, and
832 fps for another with `f/t 0.07` and `f/s 60.0`. (Those narrow bands are that
session's readings, not the definition of healthy — §2.2 records a 59.3–62.9
range across four configurations, and the last digit is window jitter.) The
8-field credit cap is
reached only above 133 ms per frame (~7.5 fps), so it took a software-rendered
launch to trigger it at all. **On a machine that holds 15 fps, hw pacing is
time-correct.** Note `f/t 4.00` exactly: at a frame cost of four fields the cap
plays no part, so the pinning at `8.00` in §5.2 was the cap and nothing else.

### 11.2 A Dam stage load is priced at 8.7 seconds, and held for all of it

`boss.c:540` holds a stage load for the modelled N64 load time, blocking on
retrace messages with **no rendering**, before the first frame. Measured:

```
ge: hwpace: stage load cart+decode 2518.1 ms, setup cpu 6224.6 ms
            (match 462/695526, nearest 25/68875/221600, guards 36,
             objects 241, textures 275, cart 565 KB)
ge: hwpace: stage load 8742.8 ms
```

That is the white screen on entering Dam, and it is long enough to be mistaken
for a hang: the port finishes the real load in a second or two and then waits out
the remainder. It is fidelity rather than a defect, but nothing on screen says
so, and `-1` in `maxfps` is a menu option a player can pick without knowing it
costs 8.7 s per Dam entry.

**The hold itself is confirmed.** The user entered Dam from the menu in hw mode
and watched it: the game is loading, not hung, and the blank screen is the hold.
What is *not* confirmed is the `held to N ms` line for that path — the price is
measured twice (8742.8 and 8748.0 ms), but every trace I can produce comes from a
`GE_STAGE` launch, which §11.4 shows never reaches the hold.

**DEFERRED — the blank screen is white, and should be black** (user report,
macOS). While the hold runs nothing is drawn, so the window shows whatever the
last present left in the back buffer; white reads as a broken application where
black would read as a loading screen. Not investigated, and not known to be
macOS-only — the same path on Windows and Linux was not observed. Two separate
questions in it: whether the framebuffer should be cleared before the hold, and
whether a loading indication belongs there at all. Deferred deliberately: it is
a presentation defect in the hw hold, not in the tick accounting this document
covers, and the hold's duration is the thing that makes it visible.

**Two thirds of the setup CPU is one coefficient.** From `portHwStageCpuUs`:

- `PORTHW_NEAR_POINTS` — 221,600 steps at 19.025 µs = **4216 ms**, 67.7% of the
  6224.6 ms setup total
- `PORTHW_NEAR_TILES` — 68,875 at 11.686 µs = 805 ms, 12.9%
- `PORTHW_MATCH_STEPS` — 695,526 at 0.7923 µs = 551 ms, 8.9%
- `PORTHW_OBJECTS` — 241 at 827.8 µs = 200 ms, 3.2%
- `PORTHW_TEXTURES` — 275 at 520.3 µs = 143 ms, 2.3%
- `PORTHW_GUARDS` — 36 at 692.6 µs = 25 ms, 0.4%
- constant, cart KB and the rest — 286 ms, 4.6%

19.025 µs is ~1,780 cycles at 93.75 MHz for what should be a distance test of
tens of cycles. Either the term is absorbing work that has its own counter
(candidate matching runs only 462 of 695,526 steps, so the fit looks pathological
on that path) or it is pricing a cache miss on a large table. Worth re-fitting
before the total is trusted. **Not proven** — I have no access to the fit data.

### 11.3 `maxfps=-1` with `tickhz=0` re-arms hw pacing on every launch

The ini is written with a self-contradictory pair. Proven by setting
`room_cache=1` and relaunching: hw mode forces `room_cache=0`, and says so.

```
ge: video: tickhz=hw - room_cache forced 0 (the fill model reads the interpreter's triangles)
```

So a player who leaves the menu reporting "uncapped" exits with a file that
resolves back to hw pacing — and meets the 8.7 s Dam hold again next launch.
`videoCoupleTickToMaxFps` (`port/src/video.c`) normally keeps the pair in step,
so this state is reachable but not the intended steady state. The writer should
never emit a pair the loader resolves differently from what was running.

**RESOLVED (F3-A, this commit series).** The writer now records the mode:
`videoPersistDerivedTick` writes the literal `hw` when the resolved rate is -1,
through `portTickIniValue` (`port/src/tick_stats.c`, tested in
`port/tests/tick_stats.c`). Two things had to change besides the early return
that skipped every hw write. The live `maxfps=-1` path now persists at all — it
returned before calling the writer. And `s_CfgTickAsWritten` is now recorded
only from a line that came from the file (`s_CfgParsingIni`), because
`videoCoupleTickToMaxFps` calls the same `tickhz` setter *while the file is
still being read*, and that call used to mark the file as saying `hw` even when
it said `120`. Without that second fix the first one would have been invisible
exactly in this section's case.

Note what did *not* change: a file resolving to hw still re-enters hw pacing,
and that is now correct behaviour rather than a contradiction, because the file
says `hw`. The 8.7 s hold is therefore still what `maxfps=-1` costs; §11.2 is
unchanged.

The writer compares two things: the rate that is running, and the rate the file
says. Only the ini parse loop records the second one, for the `tickhz` key, and
it records the text's value before any clamp (`videoTickTextValue`,
`port/src/video.c`). The `tickhz` handler in `videoSetDisplayOption` records
nothing, because `videoCoupleTickToMaxFps` calls that handler with `hw` *while
the file is still being read* — a recording there would attribute to the file a
value the coupling synthesised.

That distinction was not cosmetic. The first cut of this fix used a "we are
parsing" flag set around the whole dispatch, which let the synthetic call
record `hw` as written; the writer then saw `-1 == -1` and skipped. It passed
the obvious test only because this machine's `midas.ini` happens to list
`maxfps` before `tickhz`, and it would have left the bug live for a file with
the two lines the other way round — a legal file, since nothing gives
`midas.ini` a line order. Recorded here because it is the same failure the
third-pass audit found twice: a fix verified on one shape of input, reported as
the mechanism working.

Executed 2026-10-25 (`build_macos`, Apple Software Renderer, `GE_FRAMES=90`,
ini restored from a backup after each case):

- `maxfps=-1` then `tickhz=120` → `tickhz=hw`, logged `midas.ini 120 -> hw`
- `tickhz=120` then `maxfps=-1` → `tickhz=hw`, logged `midas.ini 120 -> hw`
- `tickhz=hw` then `maxfps=-1` → no write (correct: file and running state agree)
- no `tickhz` line with `maxfps=-1` → still none added
- `tickhz=hw` with a live `cl_maxfps 120` → `maxfps=120`, `tickhz=120`
- `tickhz=hw` then `maxfps=120`, launched cold → stays hw, nothing written.
  This one is pre-existing and not a violation of the invariant: the running
  mode *is* hw and the file says `hw`, so they agree. It does mean a hand-written
  `tickhz=hw` outranks `maxfps` across a launch, which is worth knowing and not
  worth changing here — the player leaves hw by changing Max FPS live, and that
  path writes the number (previous case).

### 11.4 `GE_STAGE` direct launch does not exercise hw pacing

`portTickHwPace()` requires `portStageRunning()`, i.e.
`current_menu == MENU_RUN_STAGE` (`src/game/front.c:18713`). At the end of
`lvlStageLoad` that menu is not reached yet, so in a `GE_STAGE=dam` launch the
hold at `boss.c:540` never fires: `portHwTakeStageLoadMs()` returned 0 and the
`held to` line never printed, while the 8742.8 ms price was still computed and
logged. Debug path only, but it means the path cannot be used to reproduce or
measure the hold.

---

## 12. Where the numbers live now

For the next session, so none of this has to be re-derived:

- **Overlay row 1** — `portDrawAudioPumpStats`, `src/game/bondview2.c`:
  `%dfps %dtk %d.%df/s %d.%02df/t`.
- **Block rows 2–6** — unchanged; the snapshot is
  `port/include/port_audio_stats.h`.
- **Log line and legend** — `portAudioLogLine`, `port/src/audio_pc.c`. Carries
  `FPS`, `tk`, `f/s`, `f/t`; `TPS` is gone from both the line and the legend.
- **The stats** — `gfx_sdl_update_title_stats`, `port/fast3d/gfx_sdl2.cpp`,
  publishing `g_PortStatFps`, `g_PortStatTicks`, `g_PortStatFieldsX10`,
  `g_PortStatFieldsPerTickX100`. It still computes `g_PortStatTps`, which
  nothing reads.
- **The arithmetic** — `port/src/tick_stats.c`, tested by
  `port/tests/tick_stats.c` (ctest target `tick_stats`).
- **The counters** — `g_PortFieldsTicked` (defined in `frametiming.c`, relaxed
  atomic; advanced in `lv.c` beside `g_GlobalTimer += g_ClockTimer`),
  `g_PortTickSerial` (`lv.c`, inside `lvlTick`), `g_PortTicksAdmitted`
  (`boss.c`, before the backpressure guard — correct for stepping, wrong for
  rates).
- **The hw audit** — `g_PortHwFieldsHeld` (`frametiming.c`, stored by
  `portHwPaceFields`) read by `portHitchFrame` (`port/src/hitchlog.c`), which
  prints `N fld`, `credit N of M` when the stall cap fired, and, when priced,
  `pacer asked N`.

---

## 13. The tick rate follows the frame cap, upward only

Cited from the comment on `videoCoupleTickToMaxFps` in `port/src/video.c`: **the
raised-tick work only goes up, so a 30 fps cap still gets a 60 Hz sim.** That
comment used to cite `docs/port/tick-rate-binding.md`, which was never written.
This section is the binding. It summarises what §2, §3 and §12 establish rather
than restating them in a second document, because two documents describing one
behaviour is how both of them go stale.

**The rule.** One knob, two rates. `videoCoupleTickToMaxFps` derives the tick
rate from `maxfps`: uncapped fps gives an uncapped tick (1000 Hz ceiling), a
120 fps cap gives a 120 Hz tick, and the result is written back to the ini
(F3-A, §11.3). It will not do two things:

- **Take the tick below 60.** The authored rate is 60 and the raised tick is
  only ever a raise, so a 30 fps cap still gets a 60 Hz sim: 30 presents a
  second, two fields per tick, `f/t 2.00`, and `f/s` still 60.0. That is the
  console at 30 fps, not a degradation.
- **Override `tickhz=hw`.** Hardware pacing is the console's own frame pacing
  (§3, and `docs/port/n64-hw-pacing.md`); a derived number may not replace it
  silently.

**Which number means what.** The four modes are §3. In all four, the number
that says "the game is running at the right speed" is `f/s`, not the tick rate:
`tickhz=120` steps the sim 120 times a second while `f/s` stays 60.0 (§2.2).
`TPS` was the one that conflated them — `min(tick rate, 60)`, because `lv.c`
increments `g_GlobalTickCount` once per tick that advanced the clock by a whole
field (§2.1) — so it tracked FPS below 60 and read 60 above it. The overlay and
the log publish `tk` and `f/t` instead; §12 says where each lives.

**The admission gate.** `portTickGateCounts()` (§8) admits one tick per half
field at the configured rate, so `tk` is *bounded by the present rate*: on a
machine presenting 38 times a second, `tk` reads 38 at `tickhz=120` (the
third-pass audit's F2). Configured tick rate and achieved tick rate are
different numbers, and only the second is displayed.

**Which counter answers which question** (§12):

- `g_PortFieldsTicked` — fields the simulation actually applied, advanced in
  `lv.c` beside `g_GlobalTimer += g_ClockTimer`. The only one that measures
  **speed**. Monotonic across a stage load, which is why it can carry a rate
  through one.
- `g_PortTickSerial` — ticks that reached `lvlTick`. This is `tk`.
- `g_PortTicksAdmitted` — admitted ticks, incremented *before* the
  `pendingGfx < 2` backpressure guard, so it counts ticks that never reached the
  sim. Right for `title.c`'s per-admitted-tick stepping, wrong for any rate.

**Still unverified, and both need hardware:** sub-ticking (`f/t` below 1.00 needs
a renderer faster than 60 fps; this machine peaks near 38 and the lowest `f/t`
measured anywhere is 1.03), and vsync pinning `tk` at 60 on a real 60 Hz panel.
