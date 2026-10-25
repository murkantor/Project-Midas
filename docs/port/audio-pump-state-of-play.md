# The audio-pump branch: state of play

Branch `tick-field-accounting`, base `3a1ab62`. (It was `audio-pump-wallclock_gen4`
while this work was done; the name described its first subject rather than its
centre of mass. The `NEXT_AGENT*.md` handoffs still say the old name and are left
as written, because they are dated snapshots. The rename touched nothing but
these two headers. Note that the branch's *earlier* history was amended once,
before this pass: `tps-the-lie.md` cited two pre-rewrite SHAs that resolved to
nothing, corrected 2026-10-25.) Re-derive the commit count
with `git rev-list --count 3a1ab62..HEAD` — it is deliberately not written here,
because this branch has now miscounted itself five times, twice in one day.

This is the entry point for the next session: what the branch set out to do,
what is fixed, what is not, and which claims rest on measurement rather than
inference.

It does not replace the two long documents. It tells you which of them to open
and why.

- **`docs/port/audio-pump-audit-pass3.md`** — the execution audit. Findings
  F1–F6, the execution record with predictions written before each run, and the
  post-fix numbers now in §6. Read it for *evidence*.
- **`docs/port/tick-and-field-accounting.md`** — the design record. The trace,
  the four modes, the D-numbered defects, and §11's hw-pacing measurements.
  Read it for *mechanism*.

---

## The commits, in six phases

Read them as these units rather than serially. Every commit through `af78f4a`
builds clean and passes `ctest` 9/9 on its own — measured, not assumed (see
below).

**A. The four numbers** (`ca41f25`, `97659f6`, `85fa9f1`) — the tick and field
rate maths as pure functions, `fields/s` published, the block showing
`tk`/`f/s`/`f/t` instead of `TPS`.

**B. Documenting and instrumenting them** (`9873f43`, `bbc5c99`, `2f8a6d0`,
`6d02aa1`) — the design record, the manual claims, `TPS` dropped from the log,
and the hw field-hold audit in the hitch line.

**C. The audit and its corrections** (`52807f7`, `cb98c39`, `dbed958`) — a
third-pass execution audit falsified four of phase B's claims; these correct
them, replace the misleading `fld`/`model held` comparison with a clamp-site
measurement, and record the audit itself.

**D. The two deferred items** (`87af349`, `17bf80b`) — the `tickhz` writer
(F3-A) and the stall cap yielding to a hardware-pacing hold (D5).

**E. What that work exposed** (`ef16465`, `df881c9`, `f04d421`, `af78f4a`) —
the state-of-play page, the pump's underrun ratchet defect found while checking
an audio complaint, the harnesses made durable, and a documentation sweep.

**F. The pass after it** (`13fedc5`, `7b99f9c`, `e06105f`) — the audio
simulation put in the pump's order and given jitter (the largest test change on
the branch), the two missing documents resolved, and the branch renamed. Two of
these three carry code; do not read "documentation" into a phase tag.

Phase C is the interesting one for a reader: it contains the branch's own
retraction, and the commits in it that supersede earlier ones are the strongest
argument anyone could make for folding this history.

### Why the history is not folded — decided 2026-10-25

The question was whether to squash some of these commits before review. The
answer is no, and this section is the evidence for it so the question does not
have to be re-argued.

**Every commit stands on its own.** All of them were checked out in a separate
worktree and built and tested individually
(`cmake -DGE_PORT_TESTS=ON -DGE_NATIVE_64=ON -DGE_VIDEO=ON
-DSDL2_DIR=/opt/homebrew/lib/cmake/SDL2`): every one builds with zero errors and
passes `ctest` 9/9. There is no broken bisect step, so no mechanical reason to
fold.

**Folding would break the citation graph.** At the time of the decision the
tracked docs cited commit SHAs 66 times, and three more sat in code
(`port/tests/audio_pace.c` and `scripts/port/audio_pump_log.py` cite
`df881c9`). Those citations are this branch's evidence system — "the rule before
`df881c9`", "see `cb98c39`", "`52807f7` corrects F2, F4, F5 and F6". Rewriting
any commit changes its SHA and every descendant's, so folding one commit turns
dozens of references into dangling ones: the same defect as a handoff pointing
at an untracked file, which is what Task B of `NEXT_AGENT_V4.md` exists to fix.

**The case for folding, stated honestly.** Four commits contain content a later
commit corrects: `bbc5c99` added a false `tickhz` claim that `52807f7` and then
`87af349` fixed; `6d02aa1` added the `fld` versus `model held` comparison that
`cb98c39` replaced because it reported the opposite of the truth; `17bf80b`
wrote "a here is the default cap of 8" that `af78f4a` corrected. A checkout of
those trees gives you a lying manual and a misleading instrument.

**Why that loses.** Nothing is *broken*: each tree compiles and its suite
passes, so a bisect is never misled about where a regression entered. The
project's own doctrine is that the record of a wrong first conclusion is worth
more than a tidy one — folding deletes exactly the evidence four passes
accumulated. And expecting no commit to supersede an earlier one is a fiction;
what has to be true is the tip.

**What was done instead.** The five-phase map above, so a reviewer reads five
units instead of seventeen. That is the reviewability folding would have bought,
at no cost to the citations.

**When to reopen this decision.** Only two conditions: the branch is going to be
squashed at merge anyway (then intermediate hygiene is moot and there is no work
to do), or a commit is found that does not build or whose suite fails (which
would already be a defect to fix, not a folding argument). "It would look
nicer" is not a condition — that is what the map is for.

---

## 1. The short version

The branch replaced a number that lied — `TPS`, which read `min(tick rate, 60)`
and tracked `FPS` — with four that could be trusted: `fps`, `tk`, `f/s`, `f/t`.
Three passes then audited that work, the audits found six findings and four
D-numbered defects, and every one of them is now closed.

The last two were held back deliberately — both were behaviour changes on a
branch that was documenting and instrumenting — and were finished on 2026-10-25
in commits `87af349` and `17bf80b`.

**State: build clean, `ctest` 9/9 (the only compiler output is the pre-existing
`__DATA,__common` alignment warning), no known false statement in a shipped
manual.** Five items remain open, and the most important thing about them is
which ones *cannot* be settled on this machine.

---

## 2. What changed in the last session

### `87af349` — the `tickhz` writer (audit F3)

**Was wrong.** A session running hardware pacing (`maxfps=-1`) left whatever
numeric `tickhz` the file already had. The writer returned early on the resolved
`-1`, and the live `maxfps=-1` path returned before calling the writer at all.
The file described a mode that had not run.

**Is true now.** The writer emits `hw`, via `portTickIniValue()`
(`port/src/tick_stats.c`), a pure function with `NULL` for the `-2` re-derive
sentinel so that state cannot reach a file. A file with no `tickhz` line still
gets none.

**The subtle half.** The `tickhz` handler used to record what the file said, but
`videoCoupleTickToMaxFps` calls that handler with `hw` *while the file is still
being read*, so the synthetic call could claim the file said `hw` when it said
`120`. That is why F3 stayed invisible for three passes. The ini parse loop now
records the file's own line, for that key only; the handler records nothing.

**A wrong first cut, kept in the record.** The first version used a
parse-in-progress flag set around the whole dispatch. It passed only because
this machine's `midas.ini` lists `maxfps` above `tickhz`; with the two lines
reversed the file was still left stale, because nothing gives `midas.ini` a line
order. Caught by the user reading the diff. Both documents record it — this is
the third instance on this branch of a fix verified on one shape of input.

**Verified by execution** — six cases, each from a backed-up `midas.ini`,
restored after (`md5` unchanged each time): `maxfps=-1` then `tickhz=120` → `hw`;
`tickhz=120` then `maxfps=-1` → `hw`; `tickhz=hw` then `maxfps=-1` → no write;
no `tickhz` line → still none; `tickhz=hw` + live `cl_maxfps 120` → `120`;
`tickhz=hw` then `maxfps=120`, cold launch → stays `hw` (pre-existing, and not a
violation: the file and the running mode agree).

### `17bf80b` — the stall cap versus a deliberate hold (D5)

**Was wrong.** `tickhz=hw` prices a frame at the fields the console would have
needed (`portHwPaceFields`, 1..30) and `boss.c` holds the tick for that many.
The credit path then clamped elapsed time at 8 fields. Every frame priced above
8 was waited for in full and credited 8 — the audit measured 45–50 fields of
simulated time discarded per frame in Dam.

**Is true now.** Under hardware pacing a tick is credited `max(cap, hold)`.
The policy is one pure function, `portStatsStepCap()`; `portTickCreditCap()` in
`frametiming.c` only gathers its inputs.

**The objection, and how it was answered.** §5.2 recorded the concern that
threading the hold through `boss.c` adds a cross-module contract that can itself
be wrong. The answer is *where the hold is read*: `boss.c` is not involved at
all. `portHwPaceFields()` already published the hold it priced
(`g_PortHwFieldsHeld`, added by `6d02aa1` for the hitch log), so the clamp reads
a number the pacer wrote. The flow stays one-way.

**What keeps the cap honest** — each clause is a test case in `stall_cap_hold`:
the raise is only *to* the hold, and the hold is itself capped at 30 fields; it
applies only under `portTickHwPace()` (false in menus, demos and outside a
stage); an explicit `GE_TICKMAXSTEP` is obeyed exactly, because that knob is how
the cap is measured; `0` still means uncapped.

**The third position was rejected.** "Make the hold authoritative" — admit N
without re-deriving it — was considered and dropped: the wall clock is still
measured and still compared, so a frame that ran longer than its hold still has
the excess dropped and reported. Trusting the hold blindly would simulate time
that never elapsed.

**The three caps (§10), decided together.** Only the **8-field credit cap**
moved. The **6-field render cap** is left alone — it bounds the fit, not the
tick, and the open question of whether that fit saw load hitches is about the
fit's accuracy, not about crediting. The **30-field total cap** is left alone
and is now the bound on this raise too.

**Verified by execution** (macOS, Apple Software Renderer, `GE_STAGE=dam`,
`GE_HWSCALE=3` to price holds above the cap):

```
   8 credit 8 of 52 pacer asked 6      hold under the cap: still clamps at 8
   6 credit 8 of 54 pacer asked 6
   1 credit 12 of 57 pacer asked 12    hold over the cap: the hold is admitted
```

One frame in fifteen carried a hold above 8, and that frame's credit moved from
8 to 12 — the change is confined to the frames it is about. `GE_HWSCALE=3` also
explains why holds above 8 are rare: scaling the render model 3× saturates it
against its own 6-field cap, so a 12-field hold is 6 render fields plus a
~100 ms load charge. **The raise lands on load frames, which is the population
D5 was about.**

### Also closed

- **`NEXT_AGENT_V3.md` §8.2** — `credit N of M` and `pacer asked N` now have shipped text:
  `tools/console_help.txt`, section THE HITCH LINE. They were the branch's only
  per-frame evidence of time loss and had no user-facing documentation at all.
- **`tools/midas_ini_help.txt`** — the `tickhz` row said a stale number "is
  ignored rather than an override", which F3's fix made false.

---

## 3. Findings register

| Item | Severity | Status |
|---|---|---|
| F1 `model held` vs `fld` reads healthy while 63% of wall time is lost | major | fixed, `cb98c39` |
| F3 manual says `tickhz` is rewritten to `hw`; it was not | major | fixed, `87af349` |
| F2 manual never states `tk` is bounded by the present rate | minor | fixed, `52807f7` |
| F4 "`f/s` reads 0 during a stage load" — it reads 0.8–1.4 | minor | fixed, `52807f7` |
| F5 stated healthy band narrower than measured (59.3–62.9) | minor | fixed, `52807f7` |
| F6 log's frame/tick columns frozen, not measured, during a hold | minor | fixed, `52807f7` |
| D4 negative `TPS` for one window after every stage load | — | fixed; unit-tested, never seen live |
| D5 stall cap truncates a deliberate hw hold | major | fixed, `17bf80b` |
| D6 `f/s` read a healthy 60.0 during every pause | — | fixed |
| D7 the D4 guard could still publish a negative | — | fixed |
| `NEXT_AGENT_V3.md` §8.2 `credit` undocumented where a user would see it | minor | fixed, `17bf80b` |
| Audio pump queue has almost no headroom above one frame | — | **reported, not fixed** |
| F3-A's first cut was order-dependent | — | caught before commit, recorded |

---

## 4. What remains

Ordered by what a reader should do next, not by severity.

### 4.1 Two cited documents did not exist — closed 2026-10-25

`src/game/frametiming.c` (the hw model header and the load-hitch note) and
`src/game/ramromreplay.c` cite `docs/port/n64-hw-pacing.md`.
`port/src/video.c` (the comment on `videoCoupleTickToMaxFps`) cited
`docs/port/tick-rate-binding.md`. Neither file existed.

**Both citations resolve now.**

- **`docs/port/n64-hw-pacing.md` was written** — the model's nine coefficients
  and the two that fitted to zero, the 6-field render cap and the 30-field total
  cap, the cart-DMA and inflate rates, the stage-load CPU terms, the fit's
  provenance (and the fact that `hwfit.py` is not in the tree), and the open
  question about the near-points coefficient. It is assembly, not research: the
  material was in `frametiming.c` and in §11 of the design record.
- **`tick-rate-binding.md` was NOT written.** The binding is
  `tick-and-field-accounting.md` §13, and `video.c` now cites that section. A
  second document would have restated §2 and §3 and then drifted from them — the
  failure §5's documentation map is about.
- **§11 stayed in the design record**, by decision recorded in both documents:
  three of its four subsections are about the tick and the ini, not the RDP
  model, and moving them would renumber references that are already cited.

Reminder for the next new file here: `docs/` is gitignored, so it needs
`git add -f` or it will exist on one disk and nowhere else. Verify with
`git ls-files docs/port/`, not with the `git add` warning — that fires even for
tracked files and is therefore useless as a signal.

### 4.2 `lv.c`'s comment still asserts something false — needs a decision

`src/game/lv.c:1520-1522` says `g_GlobalTickCount` "must count 60Hz fields, not
ticks … Always true at GE_TICKHZ=60". It is true only at or above 60 fps. Below
60, `g_ClockTimer` is 2–4 and the counter advances by 1, so it counts *ticks*
(measured: `f/t 4.00` at 15 fps).

All three documents name this as **the one deliberately-live mislabel**. They
leave it because the fix is a decision about parity semantics —
`chraction.c`, `bondview2.c`, `propobj.c` and `gunfire.c` all key off this
counter as if one tick were one field. It is not a comment edit. Either make the
parity decision and change the consumers, or say plainly what the counter is and
correct §4 and `tps-the-lie.md` §2.4 to stop claiming it is unfixed.

### 4.3 Three claims need hardware, not care — blocked here

From the audit's §2, all stated as behaviour in the shipped manuals, none
reproduced on this machine:

- **Paused / controls-locked**: "`f/s` reads near 0 while paused, `tk` can still
  read 60 beside it." Consistent with `lv.c` (the serial increments before
  `g_ClockTimer` is zeroed) but never executed — the console has no pause verb.
- **Sub-ticking**: "`f/t` below 1.00 at a raised tick rate." Needs a renderer
  faster than 60 fps; this one tops out near 38, and the lowest `f/t` measured
  anywhere was 1.03.
- **vsync pinning `tk` at 60**: needs a real 60 Hz panel where vsync, not the
  machine, is the limit.

One machine with a working GL driver settles all three in an afternoon. Until
then these are the manual's most load-bearing claims resting on inference.

### 4.4 D4's fix was never observed under a live reset — blocked-ish

The negative-`TPS` guard is covered by unit test, and `tps-the-lie.md` reports
98 consecutive live lines with no negative. But no run has produced a stage-load
reset inside a stat window with a non-zero pre-load count. The race the snapshot
exists for is reasoned, not seen.

### 4.5 The hw hold's blank screen is white — small, deferred, real

While a stage load is held nothing is drawn, so the window shows the last back
buffer. White reads as a broken application where black would read as a loading
screen. Not investigated; not known to be macOS-only. Recorded in
`tick-and-field-accounting.md` §11.2.

### 4.6 The pump's underrun ratchet could not fire below ~8.6 fps — fixed

Found on 2026-10-25 while checking an audio complaint, then fixed here.

**Was wrong.** The queue target was `max(ratcheted, one interval + device
period)`. The interval floor is a frame-rate quantity (~870 ms at 1 fps); the
ratchet that learns from underruns is capped in absolute bytes (10 KB = 116 ms).
So below about 8.6 fps the floor won unconditionally and every underrun the pump
learned from was discarded on the next pump. The queue sat at exactly one
interval + 12 ms — a 12 ms tolerance for the next frame being slower, 1.4% of
the interval at 1 fps.

**Is true now.** `portAudioPaceEffectiveTarget()` returns
`max(configured, interval floor) + learned`, so the headroom lands on top of the
floor instead of competing with it. Ordinary frame rates are unchanged; the
raise is bounded by `TARGET_MAX - TARGET` (76 ms).

**Verified without a device.** All the existing simulations pumped at a
*constant* interval, which cannot starve — the pump sizes for the interval it
just measured. The new `simulate_jitter` drains the elapsed interval before the
refill, so a longer interval can starve it. At 1 fps with every fifth pump 70 ms
longer, over 120 s, both policies driven from the same ratchet:

```
legacy  max(ratcheted, floor):  27 dry events, peak queue  952 ms
now     floor + learned:         5 dry events, peak queue 1011 ms
```

**Verified live** (Apple Software Renderer, Dam, 1 fps): after a `dry` the
target climbs `1041 → 1053 → 1064 ms`, one 1024-byte step per pump; before, it
stayed pinned at `interval + 12`.

**Not fixed.** A spike larger than the whole learned ceiling — a stage load —
still empties the queue. One was observed at the transition into Dam. No bounded
ratchet covers an unbounded spike.

**Not verified.** Real hardware; all measurements are the software renderer, and
the audibility of a 12 ms vs 76 ms cushion is not something this machine can
tell you.

Details in `docs/port/audio-pump-history.md` §9.

### 4.7 Not verified anywhere

Windows and Linux. Every measurement on this branch is macOS, and most of it is
from an **Apple Software Renderer** context — front-end frames ~25 ms, Dam
frames ~360–890 ms. Say which renderer a number came from before drawing a
conclusion about frame rates above 60.

### 4.8 `src/game/frametiming.c` has no test target — decision: not this pass

`grep frametiming CMakeLists.txt port/tests/` returns nothing. `waitForNextFrame`,
`portTickCreditCap`, `portHwPaceFields` and the field accumulator are reached
only through a live game. That is the largest untested timing surface on the
branch, and it is where D5 lived. **Decided 2026-10-25: no harness in this pass,
with the reasons stated rather than left implicit.**

- **What is already tested:** the *policy* those functions obey.
  `portStatsStepCap`, `portStatsMeasuredFields` and `portStatsCreditedFields` are
  pure and under `ctest`, and D5's whole rule table lives there. The glue that
  gathers their inputs is not tested.
- **What a harness would need, measured:** an `osGetCount` stub (7 call sites),
  definitions for the engine symbols the file externs (42 `extern` declarations,
  including `g_PortHwChrsTicked` from `chr.c` and `g_PortHwLoadLast` from the gfx
  layer — `port/src/headless_stubs.c` already stubs the latter for another
  purpose), and the part that makes it real work: the file caches configuration
  in a dozen process-global statics fed by 30 `getenv` reads, so one process
  cannot exercise two configurations. That is one process per configuration,
  driven from a script — a different artifact from the nine sub-second tests.
- **What a live run *can* prove:** the composition. D5's evidence
  (`credit 8 of 52` → `credit 12 of 57`) is exactly the integration, and no unit
  test produces it.
- **What a live run *cannot* prove:** any single branch of that composition. On
  this machine a run measures the model, the renderer and the loop together and
  cannot say which set the frame time — every hw-pacing number here comes from an
  `Apple Software Renderer` context where the renderer, not the model, sets the
  frame length.

If a later pass builds it, the honest shape is: extract the accumulate-and-clamp
step of `waitForNextFrame` into `port/src/tick_stats.c` as a pure function (whole
fields, the banked remainder, and what the clamp does to the remainder), test
that, and leave the rest as glue.

---

## 5. Documentation map

These pages describe the same behaviour, overlap on purpose, and drift apart
when one moves alone. Touch one, check the rest:

- `tools/console_help.txt` — the in-game console and the log legend. Sections
  THE HITCH LINE, READING THE BLOCK, WHAT TO REPORT.
- `tools/midas_ini_help.txt` — every `midas.ini` key, including `tickhz`.
- `port/src/audio_pc.c` — the emitted log legend (must agree with
  `console_help.txt`).
- `docs/port/tick-and-field-accounting.md` — the design record.
- `docs/port/n64-hw-pacing.md` — the hw-pacing cost model: coefficients, load
  rates, provenance, and what a hold does and does not mean.
- `docs/port/tps-the-lie.md` — the original overlay argument; the historical
  record of why `TPS` went away.
- `docs/port/audio-pump-history.md` — the pump's generations and its own defect
  history, including §9 on the ratchet.
- `docs/port/audio-pump-audit-pass3.md` — the audit and its §6 post-fix numbers.

Counts and commit numbers in these pages rot on every commit. Prefer "the
commits that did X" to a total; where a count is load-bearing it is stated once,
here, and should be derived from `git rev-list --count 3a1ab62..HEAD` rather
than copied forward.

**Citation policy, adopted 2026-10-25.** Line numbers in these pages rot with
every nearby edit, and this branch edits `video.c` and `frametiming.c`
constantly — four citations into `video.c` were stale within two commits, and one
of them described a branch that no longer existed. So: cite the **symbol or the
section**, and use a line number only where the line *is* the point (a
constant's definition, a branch that was deleted). When a cited line moves, point
at the symbol instead of renumbering — a renumbered citation is right until the
next commit, and a symbol citation fails loudly when it goes wrong.

The branch's own history says **every pass fixed real defects and introduced at
least one of its own**. Anyone doing the next pass should write their findings
down before editing anything, and should assume the same of themselves.

---

## 6. How to verify

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cmake --build build_macos -j8
cd build_macos && ctest --output-on-failure        # expect 9/9
```

Knobs, all env-only:

- `GE_FRAMES=n` — exit after n pumps. **A hw-pacing Dam run needs a time bound,
  not a frame bound:** `GE_FRAMES=900 GE_STAGE=dam` is a ~4 minute run on the
  software renderer, the audit found 420 never reaches the stage, and 225 was
  measured here reaching only the menu (stage 90).
- `GE_STAGE=dam`, `GE_HITCHMS=1` (log every frame), `GE_HWSCALE=3` (price holds
  above the 8-field cap), `GE_HWDBG=1`, `GE_TICKHZ=120`, `GE_TICKMAXSTEP=n`,
  `GE_NOINISAVE=1`.
- `GE_CONCMD="cl_audio_log 1"` writes `Data/Logs/audio_pump.log`.

Two rules for any run: **back up `build_macos/midas.ini` and diff it after** —
it is the user's real file, and `maxfps=-1` + `tickhz=0` will be rewritten to
`tickhz=hw` by the current build. And **do not touch `Data/Saves/`**; check
`eeprom.bin`'s md5 before and after.

A cheaper shape than a fixed `GE_FRAMES`: start the run in the background, poll
the log for the lines you need, and stop it. The expensive part is every frame
spent inside Dam (~890 ms), not the pumps needed to get there — a wait for
twelve `credit` lines reached the stage and finished in 30 seconds.

**Both of those are scripts now, not advice.** Use them:

```bash
scripts/port/audio_run.sh -t 50 -s dam -e GE_HITCHMS=1,GE_HWSCALE=3 \
    -w 'credit [0-9]* of' -n 5
scripts/port/audio_pump_log.py <captured audio_pump.log>
```

`audio_run.sh` does the backup/restore, the eeprom md5 check, the
refuse-if-running check, the early stop and the renderer line. `audio_pump_log.py`
reads the pump log and reports starves, queue-vs-target and the target trend,
with the limits of a 1 Hz sample stated in its header.

### Where investigative code goes

- **`port/tests/`** — anything that *asserts*. Registered explicitly in
  `CMakeLists.txt` (no glob), pure C, sub-second. Nine files, nine tests.
- **`scripts/port/`** — anything whose output a *human reads*. Probes, drivers,
  analysers. `nat_test.sh` is the model.
- **`build_macos/` — nothing.** `.gitignore:274` ignores it, so a harness there
  dies with the session. That happened once on this branch and a measurement had
  to be redone.

The test: if it decides pass/fail, it belongs in `port/tests/`; if it prints
numbers for interpretation, in `scripts/port/`, and its header must say what it
can and cannot tell you.

---

## 7. The lesson worth carrying

Four passes, alternating method, each finding what the previous could not see:

- **Reading catches structure** — a wrong general rule, a counter named wrongly,
  a document lying about its own commit state.
- **Execution catches sentences about measured quantities** — `model held`
  versus `fld` reading "healthy" while two thirds of wall time was discarded;
  `f/s` reading 0.8 rather than 0; a healthy band of 59.3–62.9 where the docs
  said 60.0.

Neither alone has yet been enough on this branch, and the failure mode is
specific and repeatable: **a fix verified on one shape of input, reported as the
mechanism working.** `run8` was invalid because a write and a no-write looked
identical. The F3-A first cut passed because the test file happened to list its
two lines in the helpful order. The next pass's most likely defect is the same
one.
