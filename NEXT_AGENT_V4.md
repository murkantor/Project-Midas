# Handoff V4: make the timing tests honest, then give the two missing documents a home

Paste this whole file into a fresh session as the task. It is self-contained.

This follows `NEXT_AGENT_V3.md`, whose two tasks are now done: the `tickhz`
writer (F3-A) and the stall cap versus a deliberate hold (D5). Both closed, both
committed, both documented. What that work exposed is the subject of Task A
here, and what it left behind is Task B.

Work them **in this order**. Task A is code with a measurable pass/fail. Task B
is documentation that depends on Task A's outcome for one of its sections.

> **Read this before §2.** The single most useful thing in this handoff is that
> the branch's timing simulations model the pump in an order the pump does not
> use, which makes starvation *structurally impossible* to observe. That is
> measured, not argued: §2.1 has the two numbers. Everything else in Task A
> follows from it.

---

## 0. State you are inheriting

Branch `audio-pump-wallclock_gen4`, on top of `3a1ab62`, tip `4e731d9`. **Do
not trust a commit count written in this file.** Derive it:
`git rev-list --count 3a1ab62..HEAD`. Three handoffs in a row stated the wrong
total, and this branch's own session miscounted itself twice in one day; a number
written here is a snapshot, not a fact.

The list below stops at `af78f4a`; the commits after it are documentation-only
amendments to this brief, and `git log` is the authority for the tip.

```
af78f4a  docs: the counts and the cap claim the last commit left stale
f04d421  tests: keep the harnesses, and keep them where they survive
df881c9  audio: let the underrun ratchet reach the queue below 8.6 fps
ef16465  docs: the state of play, as the next session's entry point
17bf80b  frametiming: let the stall cap yield to a hardware-pacing hold (D5)
87af349  video: record the tick mode in midas.ini, hw included (F3-A)
dbed958  docs: the third-pass audit, its brief, and the sign-off on both
cb98c39  hitchlog: report the stall cap where it fires, not against the model's hold
52807f7  docs: correct the tick, load and window claims a third pass falsified
2f8a6d0  console: drop TPS from the pump log
bbc5c99  docs: correct the shipped manuals' frame and tick claims
9873f43  docs: the design record for the frame and tick numbers
6d02aa1  hitchlog: audit the hw field hold against the fields credited
85fa9f1  console: the block shows ticks, fields/s and f/t
97659f6  frametiming: publish fields credited per second, and stop the negative TPS
ca41f25  stats: the frame and tick rate maths, as tested pure functions
```

Unpushed — `origin/audio-pump-wallclock_gen4` is still at `3a1ab62`, so the whole
branch exists only here. §8 records the decision not to squash it; that is
settled and needs no action.

Baseline: build clean except the pre-existing
`reducing alignment of section __DATA,__common` linker warning, `ctest` **9/9**
(`native_layout`, `native_collision`, `native_aim`, `native_stan`,
`native_movement`, `native_prop_collision`, `native_player`, `audio_pace`,
`tick_stats`).

**Closed on this branch**, and not to be reopened: F1–F6 from the third-pass
audit, D4–D7, audit §8.2, F3-A, D5, and the pump ratchet defect found on
2026-10-25. Every one has a commit and a doc entry.

**The entry point is `docs/port/audio-pump-state-of-play.md`.** Read it first;
it is one page with the findings register and the remaining work. The two long
references are `docs/port/audio-pump-audit-pass3.md` (evidence) and
`docs/port/tick-and-field-accounting.md` (mechanism).

**Two live-run harnesses already exist — use them, do not rewrite them.**

```bash
scripts/port/audio_run.sh -t 50 -s dam -e GE_HITCHMS=1,GE_HWSCALE=3 \
    -w 'credit [0-9]* of' -n 5
scripts/port/audio_pump_log.py <captured audio_pump.log>
```

`audio_run.sh` is the time-bounded runner: it backs up and restores
`midas.ini`, records and re-checks `Data/Saves/eeprom.bin`'s md5, refuses to
start beside another copy of the game, stops as soon as `-w` matches `-n`
times, and reports which GL renderer produced the numbers. `audio_pump_log.py`
reads the captured pump log and reports starves, queue-vs-target and the
target's trend. Both carry the caveats in their headers; read those before
trusting a number that came out of them.

These live in `scripts/port/` deliberately — see §7 (where a harness belongs).

`docs/` is gitignored (`.gitignore:281`, `/docs/*`) but six files under
`docs/port/` are tracked anyway — `git ls-files docs/port/` lists them. `git
add` on a path there prints an ignore warning even for a tracked file, and a
**new** file there needs `git add -f` or it will exist on one disk and nowhere
else. `build_macos/` is gitignored too (`.gitignore:274`), which is why nothing
investigative may live there. That is the same defect Task B is about, so do
not repeat it.

---

## 1. Ground rules

1. Derive claims from the code and from execution, not from the documents. Two
   manuals, a design record and several comments on this branch have shipped
   false statements — twice while "fixing" an earlier false one.
2. Do not present an inference as a measurement. Mark what you executed and
   what you only read.
3. Guard the user's settings: `build_macos/midas.ini` is real. Back it up
   (`cp build_macos/midas.ini /tmp/midas.ini.v4-backup`), diff it after every
   run, restore it. **Note:** the file is `maxfps=-1` + `tickhz=0`, and the
   current build rewrites that line to `tickhz=hw` on launch — expected and
   correct per F3-A, but it means "the file changed" is now the *normal* case
   and needs checking rather than assuming. Never touch `Data/Saves/`; check
   `eeprom.bin`'s md5 (`3590c7df0edb0930f4e4248826e8ab64`) before and after.
4. A test that passes is not a test that works. This branch has five recorded
   instances of a fix verified on one shape of input being reported as the
   mechanism working. Assume you are the sixth.
5. Nothing investigative may be thrown away. Any script that produced a number
   a commit message cites must be committed, in `port/tests/` if it asserts and
   in `scripts/port/` if a human reads its output. A scratch harness in
   `build_macos/` is gitignored and dies with the session — that happened once
   already on this branch, and the evidence for §2.1 below had to be
   reconstructed.

---

## 2. Task A — the timing simulations model a pump that does not exist

### 2.1 What is wrong, measured

`port/tests/audio_pace.c:435` defines `simulate()`, the closed-loop pump model
every audio case is built on. Each iteration it **refills to the target and
then** lets the device drain the interval:

```c
/* audio_pace.c:465 */   /* Refill toward the target, as the pump does with a live device. */
...                      /* ... refill loop, queued += n * BYTES_PER_SAMPLE ... */
/* audio_pace.c:514 */   /* The device plays for pump_ms at its own rate. */
```

The real pump does the opposite. `portAudioPump` (`port/src/audio_pc.c`,
`portAudioPump`) is called *after* the interval has elapsed; the device has
already drained the queue, and then the pump measures that interval and refills.
`queued` is read at the top of the pump, which is the post-drain depth.

Consequence: in `simulate()` the queue is always topped up to the current
interval's requirement *before* being drained, so it can never be empty when
the pump arrives. **Starvation is unobservable by construction, at any frame
rate and under any jitter.**

Measured, not inferred. One scenario — 1 fps, every fifth pump 940 ms instead
of 870 ms, 120 pumps, legacy `max(ratcheted, floor)` target in both arms:

```
refill-THEN-drain (simulate()'s order): dry=0
drain-THEN-refill (the game's order):   dry=24
```

The same policy, the same jitter, the same length. The only difference is the
order, and it is the difference between "no defect exists" and "it starves on
every spike".

Two second-order problems follow from the same blind spot:

- **The cadence is constant.** `simulate(unsigned fps, ...)` derives
  `pump_ms = ms_acc / fps` (`:462`), so a run at 60 fps has 17 ms every pump
  forever. A pump that measures the interval just elapsed and refills for it
  cannot starve on an even cadence even in the correct order. This is why the
  branch's ratchet defect (below) survived every passing case.
- **`ms_acc/fps` is not jitter.** For fps values that do not divide 1000 the
  intervals alternate by 1 ms (3 fps → 333/334). That is arithmetic, not
  coverage. Do not count it.

**What this already cost.** The previous session found and fixed a real defect
this way — commit `df881c9`: the queue target was `max(ratcheted, interval
floor)`, so below ~8.6 fps the interval floor won unconditionally and every
underrun the ratchet learned from was discarded. Measured 27 → 5 dry events
after the fix, and confirmed live (`target 1041 → 1053 → 1064 ms` after a dry).
That fix is correct and stays. The point is that **it was found by ear and
reasoning, not by the suite**, and the suite's silence was structural.

### 2.2 What to do

Three steps, in order. Do not skip to the third.

**Step 1 — make the order faithful.** `simulate()` (`:435`) still refills
*before* draining. Change it so each iteration drains the interval first,
measures the resulting depth, and then refills.

`simulate_jitter()` (`:758`) already has both orders: it takes a `refill_first`
switch, and `test_harness_order_decides_whether_a_starve_is_visible` drives the
two against each other. That case is the *evidence* that the order matters, not
a fix to `simulate()` — so the primary harness is still refill-first today, and
that is the thing to change. Having two harnesses with different physics is
itself a hazard: prefer to fold them into one once `simulate()` is faithful, or
state in a comment exactly why two exist.

Then run the suite. Expect the constant-interval cases to still pass — at a
constant interval the drain is covered by the target by construction, so
`dry == 0` should hold at 60 fps, at 300 fps, and at 8/5/2/1 fps alike.
**Verify that expectation rather than assuming it.** Any case that fails is the
interesting one: decide whether it exposes a real defect or an assertion tuned
to the old model, and say which in the commit message. Do not relax an assertion
to make it green.

**Step 2 — give the harness jitter.** A pump interval in a real session varies
frame to frame; that variance is the thing the queue has to absorb, and it is
absent from every case that existed before `simulate_jitter`. Add it to the
primary harness rather than as a side channel, with at least: a bounded ±jitter
at 60 fps (the ordinary case — must stay `dry == 0`), and the low-frame-rate
spike case (which must starve under the old rule and mostly not under the new
one).

**Step 3 — inventory every other timing simulation.** The claim "this is the
only one" is exactly the kind of claim this branch has been wrong about. Sweep
the tree and write the inventory down in the commit message:

- `port/tests/audio_pace.c` — `simulate()`, `simulate_jitter()`, and the manual
  loops in `test_simulation_survives_a_stall_between_pumps`,
  `test_clock_*`, `test_drain_watchdog*`.
- `port/tests/tick_stats.c` — pure arithmetic, no loop, so "interval" means the
  **stat window length**. The tests use a handful of fixed `dt` values (500,
  367, 1017, 479). Audit F5's whole finding was that the healthy `f/s` band is
  59.3–62.9 *because* windows are never exactly 500 ms — so a sweep over window
  lengths is cheap and pins a claim that currently rests on one measured run.
- `src/game/frametiming.c` — **there is no test target for this file at all.**
  `grep frametiming CMakeLists.txt port/tests/` returns nothing. `waitForNextFrame`,
  `portTickCreditCap`, `portHwPaceFields` and the field accumulator are reached
  only through a live game. That is the largest untested timing surface on the
  branch, and it is where D5 lived.

For `frametiming.c`, **decide explicitly and record the decision**: either
build a harness (it needs stubs for `osGetCount`, and the file has engine
includes and statics — this is real work, not an afternoon), or state why not
and what a live run can and cannot prove instead. Do not leave it as an
unmentioned gap; an unmentioned gap is how D5 survived three audits.

### 2.3 Traps

- **Do not "fix" the model by making it more forgiving.** The order change will
  make some cases newly exercise starvation; if a case starts failing, the
  question is whether the assertion or the policy is wrong, and the answer is
  never "raise the bound until it passes".
- **The A/B must step the ratchet identically.** `simulate_jitter` takes a
  `use_effective` flag and advances `portAudioPaceTargetStep` the same way in
  both arms, so the only variable is the combining rule. Keep that discipline
  in anything you add, or the comparison measures nothing.
- **Keep the harness pure.** No SDL, no device, no clock. That is what lets it
  run under `ctest` in 0.3 s and is the reason these decisions are testable at
  all.
- **`MAX_INTERVAL_MS` is 1000** (`audio_pc.c:117`), so the floor stops growing
  below 1 fps. A sweep that goes under 1 fps is testing the clamp, not the
  policy; say which you mean.
- **Do not model a cadence the game does not have.** The pump is called from
  the retrace-duty gate in `port/src/pcsched.c` and from forced-frame paths;
  one pump per frame is an approximation. If a new case depends on a cadence
  subtler than that, name the approximation in the comment.

### 2.4 How to verify

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cmake --build build_macos -j8                      # expect only the __DATA,__common warning
cd build_macos && ctest --output-on-failure        # baseline 9/9
```

Standalone, which is faster while iterating (the test file documents this):

```bash
cc -std=c99 -Iport/include port/tests/audio_pace.c port/src/audio_pace.c -o /tmp/ap && /tmp/ap
```

**The regression to demonstrate.** Before you change anything, reproduce the
orders that make Task A worth doing, and put the numbers in the commit message:

1. `simulate()`'s order + jitter → `dry == 0` (starvation invisible).
2. The game's order + jitter + the *old* `max(ratcheted, floor)` rule → starves.
3. The game's order + jitter + the *current* rule → mostly survives.

(1) and the contrast with (2) are already asserted by
`test_harness_order_decides_whether_a_starve_is_visible`, which is committed and
runs under `ctest` — so this is a case to keep, not to write. Re-derive the
numbers yourself for the commit message rather than quoting this document: the
measured values at 1 fps with every fifth pump at 940 ms were `dry=0` for
refill-first and `dry=24` for the game's order, under the old rule.

If (1) does not reproduce, you are looking at different code than this document
describes and should stop and say so.

### 2.5 Output

One commit. Message: what the model got wrong and the number that proves it,
what the new cases assert, which existing cases changed and why, the inventory
from step 3, and the explicit `frametiming.c` decision. If the `frametiming`
harness is built, that is a second commit.

---

## 3. Task B — scope (and, if it fits, write) the two missing documents

Three cited things do not exist, and one class of citation has gone stale.
This task is assembly, not research: the material is already written down. Do
**not** start from a blank page.

### 3.1 `docs/port/n64-hw-pacing.md` — cited three times, does not exist

Cited at `src/game/frametiming.c:598` (the hw model header),
`src/game/frametiming.c:658` (the load rates), and
`src/game/ramromreplay.c:510` (calibration from the previous frame's RDP
workload). The design record's §10 calls it "load-bearing for the hw model" and
says the record is not a substitute.

The material that belongs in it:

- **§11 of `tick-and-field-accounting.md`** (starts line 734): what a real
  session turned up — 11.1 D5 does not fire at ordinary frame rates, 11.2 the
  Dam load priced at 8.7 s and held for all of it, 11.3 the `maxfps=-1` +
  `tickhz=0` pair (now resolved, keep the analysis), 11.4 `GE_STAGE` does not
  exercise hw pacing. **Move it or reference it — decide which**, and if you
  move it leave a pointer so §11's incoming references still resolve.
- **The model itself**, from `frametiming.c`: the nine `sHwModel` coefficients
  and what each term is, the 6-field render cap, the 30-field total cap, and
  the load-rate constants (cart DMA 174 µs + 0.2025 µs/byte; inflate 714 µs +
  1.254 µs/byte in a stage load, 326 µs + 1.479 µs/byte in play), plus the
  knobs (`GE_HWSCALE`, `GE_HWLOAD`, `GE_HWLOAD_SCALE`, `GE_HWDBG`).
- **The fit's provenance and its open question**: fitted against 14 attract
  demos and the N64 bench ROM's 867 file loads; the near-points coefficient
  (19.025 µs, 1,780 cycles for a distance test) is 67.7% of setup CPU and looks
  pathological — recorded as *not proven*, with no access to the fit data.
- **What the hold does and does not mean**: the 8.7 s Dam hold is fidelity, the
  blank screen is white where black would read as loading (deferred, user
  report), and `portTickHwPace()` is false in menus, demos and outside a stage.

### 3.2 `docs/port/tick-rate-binding.md` — cited once, does not exist

Cited at `port/src/video.c:846`, for this claim: the raised-tick work only goes
up, so a 30 fps cap still gets a 60 Hz sim.

This one is smaller. The material: the four modes from
`tick-and-field-accounting.md` §3 (60 / N>60 / hw / 0), the `TPS = min(tick
rate, 60)` derivation and its table in §2.1, `fields/s` as the
speed-correctness number (§2.2), the coupling in `videoCoupleTickToMaxFps` and
its *never below 60* rule, `portTickGateCounts()`, and `g_PortFieldsTicked` /
`g_PortTickSerial` / `g_PortTicksAdmitted` and which of them is correct for
which question (§12).

**Scope decision, yours to make:** it may be better to fold this into
`tick-and-field-accounting.md` and change the one citation, rather than create a
second document that overlaps it. If you fold, say so in the commit and fix
`video.c:846` to point at the right anchor. Either answer is fine; a citation
that resolves is the requirement.

### 3.3 Citation drift — measured, from the last two commits

`port/src/video.c` moved by about +30 lines in `87af349`. Citations into it are
now wrong, and one describes code that no longer exists:

- `audio-pump-audit-pass3.md:182` cites `video.c:929-932` for
  `videoPersistDerivedTick`'s early return on `s_CfgTickHz == -1`. **That early
  return was deleted by F3-A.** The function is now at `:935`. This citation
  should not be renumbered; it should say the line is gone and point at the
  resolution recorded in §6 of the same file.
- `audio-pump-audit-pass3.md:312` cites `video.c:844` for the raised-tick
  comment → now `:846`.
- `tick-and-field-accounting.md:713` cites `video.c:844` → now `:846`.
- `tick-and-field-accounting.md:243` cites `video.c:1133-1172` for the `tickhz`
  parse → the parse is now at `:1162`.

**The decision to record:** line numbers in these docs rot with every nearby
edit, and this branch edits these files constantly. Cite the function or the
symbol and use line numbers only where they are the point. Pick a policy and
apply it to at least the citations you have already verified as stale, rather
than fixing four numbers that will rot again next commit.

### 3.4 What must stay marked unverified

Do not let a new document inherit a claim it cannot support. These stay
unverified and the text must say so:

- paused / controls-locked `f/s` near 0 with `tk` still 60 (no pause verb in the
  console; consistent with `lv.c` by reading only);
- sub-ticking, `f/t` below 1.00 (needs a renderer above 60 fps; this machine
  peaks near 38, lowest measured `f/t` anywhere is 1.03);
- vsync pinning `tk` at 60 on a real 60 Hz panel.

All three need hardware, not care.

### 3.5 Output

One commit for the documents. `git add -f` any new file under `docs/port/`, then
verify with `git ls-files docs/port/` — not with the `git add` warning, which
fires even for tracked files and is therefore useless as a signal.

---

## 4. Verification available in this repo

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cmake --build build_macos -j8
cd build_macos && ctest --output-on-failure        # baseline 9/9
```

Knobs, all env-only: `GE_FRAMES=n`, `GE_STAGE=dam`, `GE_HITCHMS=1` (log every
frame), `GE_HWSCALE=3` (price holds above the 8-field cap), `GE_HWDBG=1`,
`GE_TICKHZ=120`, `GE_TICKMAXSTEP=n`, `GE_NOINISAVE=1`,
`GE_CONCMD="cl_audio_log 1"` (writes `Data/Logs/audio_pump.log`).

**Runtime cost, measured.** The renderer here is `Apple Software Renderer`; Dam
frames cost ~890 ms. `GE_FRAMES=900 GE_STAGE=dam` is a ~4 minute run.
`GE_FRAMES=420` never reaches the stage; `GE_FRAMES=225` was measured reaching
only the menu (stage 90). The cheap shape is to start the run in the background,
poll the log for the lines you need, and stop it — waiting for twelve `credit`
lines reached stage 33 and finished in 30 s.

Say which renderer a number came from before concluding anything about frame
rates above 60.

---

## 5. Still open, and not this handoff's job unless you have time

- **`src/game/lv.c:1522`** — `g_GlobalTickCount` "must count 60Hz fields …
  Always true at GE_TICKHZ=60". Still false below 60 fps (`f/t 4.00` measured at
  15 fps). All three documents name it as the one deliberately-live mislabel.
  The fix is a parity decision (`chraction.c`, `bondview2.c`, `propobj.c`,
  `gunfire.c` key off it), not a comment edit. **Do not silently rewrite the
  sentence.**
- **Three hardware claims** (§3.4).
- **D4 under a live reset** — the negative-`TPS` guard is unit-tested and has
  never been seen in a real stage-load-inside-a-stat-window.
- **The hw hold's blank screen is white** (`tick-and-field-accounting.md`
  §11.2) — should be black. Deferred twice.
- **An unbounded audio spike still empties the queue** — a ratchet bounded at
  76 ms cannot cover a stage load. Recorded in `audio-pump-history.md` §9 as
  not fixed.

---

## 6. What the last pass learned, since it will apply to you

Four passes on this branch, alternating method, each finding what the previous
could not see: reading catches **structure**, execution catches **sentences
about measured quantities**, and neither alone has been enough.

The specific failure that keeps recurring, now five times: **a fix verified on
one shape of input, reported as the mechanism working.**

- `run8` was invalid because a write and a no-write looked identical.
- The F3-A first cut passed because this machine's `midas.ini` lists `maxfps`
  above `tickhz`; reversed, it was still broken.
- The F3-A first cut was caught by the *user* reading the diff, not by a test.
- The ratchet defect survived every passing case because every one of them pumped
  at a constant interval.
- And the harness itself models the pump in the wrong order, which is why it
  could not have caught it even with jitter.

Task A is the repair of that last one. Treat the inventory in §2.2 step 3 as
the deliverable, not the two new test cases — the point is that the *next*
timing defect gets caught by the suite instead of by the user's ears.

Write your findings down before you edit anything, and assume you will
introduce one of your own.

---

## 7. Where a harness belongs

There are two kinds of investigative artifact and they go in two places. This
is a convention, not a preference: picking wrong is how a session's evidence
disappears.

**`port/tests/`** — anything that *asserts*. It is registered with `ctest` in
`CMakeLists.txt` (each file is listed explicitly inside the `if(GE_PORT_TESTS)`
block, there is no glob), must be pure C with no SDL, no device and no ROM, and
must run in well under a second. Current contents: nine files, nine tests.

**`scripts/port/`** — anything a *human reads the output of*. Probes, live-run
drivers, log analysers. Python and shell already live here
(`model_probe.py`, `bg_probe.py`, `nat_test.sh`, `compile_sweep.sh`), and
`nat_test.sh` is the model to copy: a driver that launches the game, walks it
through a scenario, and documents in its own header how to read what comes back.

**`build_macos/` is not a place for anything.** `.gitignore:274` ignores the
whole directory. A harness written there works for exactly one session and then
is gone, taking its numbers with it. If you find yourself typing a path into
`build_macos/`, stop.

**The test of which one you need:** does the artifact decide pass/fail? If yes,
`port/tests/` — and it should fail loudly when the defect returns. If it prints
numbers for a person to interpret, `scripts/port/` — and its header must say
what its output can and cannot tell you.

The last session got this wrong twice: an order-sensitivity harness was written
in `build_macos/` and deleted, so the measurement behind §2.1 had to be redone;
and a log analyser went through three separate wrong verdicts before it was
reduced to reporting facts and stating its own limits. Both are now in the tree —
`port/tests/audio_pace.c`'s
`test_harness_order_decides_whether_a_starve_is_visible` and
`scripts/port/audio_pump_log.py` — and the analyser's header records the three
wrong answers, because the next person to "improve" it should know they were
already tried.

One rule for the analyser class specifically: **if a script cannot separate two
explanations, it must print both and say so.** `audio_pump_log.py` samples the
queue at 1 Hz and cannot distinguish "the ratchet added headroom" from "the
frame rate settled lower"; it says that, and points at the simulation that can.
A script that guesses is worse than no script.

---

## 8. History folding — decided: do not fold

**A decision record, not a task. There is nothing to do here** — do not spend
time on it, and do not re-litigate it.

Decided 2026-10-25, before any push. The history stays as it is: no squashing, no
reordering, no rewriting past commit messages. Commits made from here are new
work on the tip, not amendments to what came before.

Two facts, in case a reader goes looking:

- **Every commit builds and passes on its own.** Each was checked out in a
  separate worktree and built and tested individually; all build clean and pass
  `ctest` 9/9. There is no broken bisect step.
- **Folding would break the citation graph.** The docs cite commit SHAs dozens of
  times ("the rule before `df881c9`", "see `cb98c39`", "`52807f7` corrects F2"),
  and rewriting a commit changes its SHA and every descendant's. That is exactly
  the defect Task B exists to fix: a reference into nothing.

The full reasoning — including the honest case for folding and why it loses — is
in `docs/port/audio-pump-state-of-play.md`, under "Why the history is not
folded". The five-phase commit map there is the substitute for a tidier history.

**Reopen only if** the branch will be squashed at merge anyway (then
intermediate hygiene is moot), or a commit is found that does not build or whose
suite fails (a defect to fix, not a folding argument). "It would look nicer" is
not a reason.
