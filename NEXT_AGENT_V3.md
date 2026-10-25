# Handoff V3: the two deferred items from the third-pass audit

Paste this whole file into a fresh session as the task. It is self-contained.

This is the **follow-up** to the third-pass audit of branch
`audio-pump-wallclock_gen4`. That pass produced six findings; five are now
fixed and committed. These two were deliberately deferred, because both are
changes to *behaviour* and the audit branch was a documentation-and-instrument
branch:

- **F3-A: the `midas.ini` writer for `tickhz`.** A file left holding a stale
  numeric `tickhz` under `maxfps=-1` is not rewritten, so the file and the
  running state disagree.
- **D5: the stall cap versus a deliberate hold.** `tickhz=hw` waits for up to 30
  fields and the credit path keeps at most 8, discarding the difference.

Work them in that order. They are independent; F3-A is small and low-risk, D5
is the real decision.

> **Reviewed before you start.** A fourth pass has since read the three commits
> that landed after the audit and signed the branch off (§8). It confirmed the
> audit's `tk`, `tickhz` and load-`f/s` findings, checked the clamp-report
> concurrency by hand, and found one gap the audit did not: the new
> `credit N of M` field is not documented anywhere a user will see it. Read §8
> before §2 — it tells you which of the claims below are load-bearing and which
> are cosmetic.

---

## 0. State you are inheriting

Branch `audio-pump-wallclock_gen4` now has **ten** commits on top of `3a1ab62`.
The nine that precede the one adding this file:

```
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

Unpushed. Baseline: build clean except the pre-existing
`reducing alignment of section __DATA,__common` linker warning, `ctest` **9/9**.

**Read the audit first**: `docs/port/audio-pump-audit-pass3.md`. It is the
evidence base for everything below, it records what was executed versus merely
read, and it contains a self-correction the author had to make — which is the
standard this repository expects. `docs/` is gitignored (`.gitignore:281`,
`/docs/*`), but several files under it are tracked anyway, the audit report
among them — `git ls-files docs/port/` lists it, so this reference resolves from
a fresh clone. If you add a new document there it will be ignored until you
force-add it.

---

## 1. Ground rules

1. Derive claims from the code and from execution, not from the documents. Both
   manuals, the design record and several comments on this branch have shipped
   false statements about behaviour, twice while "fixing" an earlier false one.
2. Do not present an inference as a measurement. The audit's §2 marks every
   unverified item; keep that habit.
3. `docs/` is gitignored but two files under it are tracked. `git add` on a path
   under `docs/port` prints an ignore warning even when the file is tracked —
   check `git status` rather than trusting the warning.
4. Guard the user's settings: `build_macos/midas.ini` is real. Back it up before
   any run, diff it after, restore it. Running the game must not touch
   `Data/Saves/`.
5. Do not touch the other five findings. They are fixed.

---

## 2. Task A — the `tickhz` writer (F3-A)

### What is wrong

`maxfps=-1` means hardware pacing. The loader handles that in
`port/src/video.c:1133` and again through
`videoCoupleTickToMaxFps` → `videoSetDisplayOption("tickhz", "hw")`
(`port/src/video.c:1063`).

`videoPersistDerivedTick` (`port/src/video.c:923`) is what writes `tickhz` back
to the file, and it bails out on exactly that case:

```c
if (s_CfgTickHz == -1 || !s_CfgTickSeenInIni || s_CfgTickHz == s_CfgTickAsWritten)
{
    return;
}
```

So a session that runs hw pacing leaves whatever numeric `tickhz` the file
already had. **Executed** (audit §4, `run10b`): a file with `maxfps=-1` and a
hand-written `tickhz=120`, launched into Dam, reached hardware pacing — the hitch
lines carried the pacer's field hold — and afterwards the file still read
`tickhz=120`. The user's own `midas.ini` is the same shape: `maxfps=-1`,
`tickhz=0`.

This matters because the loader resolves the pair differently from what the file
records, and the branch's own design record already called that out as a defect
before the manual asserted the opposite:

> `docs/port/tick-and-field-accounting.md` §11.3 — "The writer should never emit
> a pair the loader resolves differently from what was running."

`bbc5c99` wrote the manual to say the game rewrites it to `hw`; commit `52807f7`
corrected the manual to describe the current behaviour. **If you fix the
writer, you must correct that manual row again** — it currently says a stale
number "is ignored rather than an override", which is true today and would
become false.

### What to decide first

Two coherent designs. Pick one and say why in the commit message:

- **(a) Write `hw`.** When the resolved tick rate is `-1`, write the literal
  `hw`. The file then means what it says. `portIniWriteStrs` takes strings, so
  this is a value change, not a new API.
- **(b) Write nothing and drop the key.** Riskier: it changes what a hand-edited
  file looks like and the loader's `tickhz`-absent path is a different branch.

(a) is the smaller change and matches §11.3. Prefer it unless you find a reason
it cannot work.

### Traps

- **The `-2` sentinel.** `videoCoupleTickToMaxFps` sets `s_CfgTickHz = -2` on
  the leave-hw path so that "no `want` equals it" and the coupling always runs.
  `-1` is hw; do not treat `-2` as a value to write.
- **`s_CfgTickSeenInIni` / `s_CfgTickAsWritten`.** The writer's guard exists so a
  file with no `tickhz` line is not given one. Preserve that: a fresh install
  with `maxfps=-1` and no `tickhz` line should probably stay without one.
- **Leave-hw.** Coming back from `-1` to a number must rewrite the `hw` line,
  or the file keeps `hw` and the loader keeps pacing the console. Trace
  `videoRoomCacheAfterHw` and `s_CfgTickAsWritten = -1` through that path.
- **Suppression.** `portIniWriteStrs` no-ops under `GE_WINDOW` or
  `GE_NOINISAVE`. Any test must account for that.
- **`portIniWriteStrs` preserves the file byte for byte** apart from the keys it
  writes and any `;` comment lines, which it drops. Do not add comment lines to
  `midas.ini` from the code.

### How to verify

The branch's established pattern for anything arithmetic is a **pure function
plus a test**, because a running game is a bad oracle. Suggest extracting the
decision:

```c
/* what tickhz line the file should hold for a given resolved state */
const char *portTickIniValue(int tickhz /* -1 = hw, 0 = uncapped, else Hz */);
```

and testing it in `port/tests/tick_stats.c` (or a new target) against:
`-1 → "hw"`, `0 → "0"`, `60 → "60"`, `120 → "120"`.

Then verify the integration by hand, and **check that the file actually
changed** — the audit's `run8` was invalid precisely because a write and a
no-write looked identical:

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork/build_macos
cp midas.ini midas.ini.v3-backup
# force a visible difference: start from a numeric tickhz, resolve to hw
sed -i '' 's/^tickhz=.*/tickhz=120/' midas.ini
GE_FRAMES=900 GE_STAGE=dam ./ProjectMidas > /tmp/v3a.log 2>&1
grep -n '^maxfps\|^tickhz' midas.ini     # expect tickhz=hw
cp midas.ini.v3-backup midas.ini && rm midas.ini.v3-backup
```

Note `GE_STAGE=dam` needs ~900 pumps under a software renderer to reach the
stage — `GE_FRAMES=420` never got there.

---

## 3. Task B — D5, the stall cap versus a deliberate hold

### What is wrong

`tickhz=hw` prices each frame at the fields the console would have needed
(`portHwPaceFields`, `src/game/frametiming.c`, 1..30 after a 6-field cap on the
render model) and `src/boss.c:600` **holds the tick** for that many fields.

The credit path then clamps what it measured. `portTickMaxStep()` defaults to
**8 fields** (`src/game/frametiming.c`), and the clamp sits in both arms of
`waitForNextFrame` (`frametiming.c:1216` and `:1319`).

So for any frame the model prices above 8 fields, the loop waits the full N and
then keeps 8. **Executed** (audit §4, `run7` and the F1 verification run): in
Dam under hw pacing, hitch lines read `credit 8 of 53`, `8 of 55`, `8 of 58` —
42 to 56 fields of simulated time discarded per frame. Because the new line is
published at the clamp, this loss is now *visible*; before commit `cb98c39` the
same frames read "healthy".

The console has no stall cap: whatever fields elapsed are credited. The cap is a
port-only invention with a legitimate purpose — stop a 90-second window drag
from being simulated as one enormous step — but it cannot tell a deliberate
N-field hold from an unintended stall, so it truncates the hold it was given.

### The decision

The candidate fix, and its counter-argument, are both written down at
`docs/port/tick-and-field-accounting.md` §5.2 ("D5 is DEFERRED"):

- **Fix:** `boss.c` just held the loop for N fields, so it *knows* the elapsed
  time was intended. Pass that expectation into `waitForNextFrame` and raise the
  cap to `max(8, N)` for that tick.
- **Counter-argument:** the cap exists because the caller cannot in general tell
  a hold from a window drag, and threading the expectation through adds a
  cross-module contract that can itself be wrong.

A third position worth considering: leave the clamp alone and make the *hold*
authoritative — if the model priced N fields, admit the tick with exactly N
rather than making the credit path re-derive it. That removes the disagreement
instead of raising a limit.

### Before you change anything

`docs/port/tick-and-field-accounting.md` §10 records that there are **three caps
on one quantity**:

- the render model's 6-field cap (`portHwPaceFields`),
- the 30-field total cap (same function),
- the 8-field credit cap (`portTickMaxStep`).

The open question is whether the render cap was fitted against footage that
already contained load hitches. Decide the three together or say explicitly
which you are leaving alone and why. Changing one in isolation is how this
quantity got three caps.

### How to verify

The `credit N of M` line is your instrument and it exists precisely for this:

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork/build_macos
cp midas.ini midas.ini.v3-backup
GE_FRAMES=900 GE_STAGE=dam GE_HITCHMS=1 ./ProjectMidas > /tmp/v3b.log 2>&1
grep -c 'credit [0-9]* of' /tmp/v3b.log          # expect 0 or near 0 after a fix
grep -o 'credit [0-9]* of [0-9]*' /tmp/v3b.log | head
cp midas.ini.v3-backup midas.ini && rm midas.ini.v3-backup
```

Baseline to beat, measured on a software renderer: 311 hitch lines, of which
241 carried `credit`, every one reporting a shortfall of 35 to 227 fields (most
commonly 45–50). The 70 without it were all frames under 133 ms, below the point
where the cap can fire. On healthy front-end 60 fps frames, 0 of 110 carried it.
A working fix should show either no `credit` lines at all, or `credit N of N`.
Full figures are in `docs/port/audio-pump-audit-pass3.md` §6.

**Two things not to break:**

- **Paused and controls-locked frames must still lose their fields.**
  `lv.c` zeroes `g_ClockTimer` for those, before `g_PortFieldsTicked` is
  advanced. Do not "fix" that — it is correct, and it is why `f/s` reads near
  zero while paused.
- **The cap must still stop a genuine stall.** A window drag is not a hold.
  Whatever you relax must be conditioned on hw pacing actually being in force
  (`portTickHwPace()`), which is false in menus, during demos and outside a
  stage.

---

## 4. Verification available in this repo

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cmake --build build_macos -j8
cd build_macos && ctest --output-on-failure        # baseline 9/9
```

Useful knobs, all env-only: `GE_FRAMES=n` (exit after n pumps), `GE_CONCMD="a;b"`
(run console lines once at the first rendered frame), `GE_STAGE=dam`,
`GE_TICKHZ=120`, `GE_TICKMAXSTEP=n`, `GE_HITCHMS=1` (log every frame),
`GE_HWDBG=1` (hw model tracing), `GE_NOINISAVE=1`.

**The renderer may be software.** In this environment the GL context was
`Apple Software Renderer`: front-end frames ~25 ms, Dam frames ~360 ms. That is
fine for the clamp (it makes D5 easy to trigger) and useless for anything about
frame rates above 60. Check the `GL: renderer:` line in your log before
concluding anything from an fps figure, and say which renderer a measurement
came from.

---

## 5. Also still open — smaller, and none of it code

These are not blockers for Tasks A and B. They are the rest of what the third
pass left, and each is a place where the tree says something it cannot support.
Take them in whatever order suits, but do not let them evaporate: five of the six
findings in the audit were sentences, and sentences are how this branch has gone
wrong twice.

### 5.1 `lv.c`'s comment still asserts something false

`src/game/lv.c:1520-1522`, immediately above the increment:

```c
/* GE_TICKHZ: g_GlobalTickCount must count 60Hz fields, not ticks,
 * because half- and third-rate work (chraction.c `& 1`, bondview2.c `%
 * 3`) keys off it. Always true at GE_TICKHZ=60. */
```

"Always true at GE_TICKHZ=60" is true only at or above 60 fps. Below 60,
`g_ClockTimer` is 2–4 and `g_GlobalTickCount` still advances by 1, so it counts
*ticks*. Measured: at 15 fps `f/t` is 4.00.

All three documents name this as **the one deliberately-live mislabel**
(`tick-and-field-accounting.md` §4, `tps-the-lie.md` §2.4, and its status
board). They leave it because the fix is a decision about parity semantics —
`chraction.c`, `bondview2.c`, `propobj.c` and `gunfire.c` all key off this
counter as if one tick were one field — not a comment edit. **Do not silently
rewrite the sentence.** Either make the parity decision and change the
consumers, or add a line saying what the counter actually is. If you do the
latter, correct §4 and §2.4 to stop claiming it is unfixed.

### 5.2 Two cited documents do not exist

- `docs/port/n64-hw-pacing.md` — cited by `src/game/frametiming.c` (the hw model
  header) and `src/game/ramromreplay.c:510`.
- `docs/port/tick-rate-binding.md` — cited by `port/src/video.c:844`.

`tick-and-field-accounting.md` §10 calls both "load-bearing for the hw model and
the raised-tick design" and says it is not a substitute. Its §11 is written as
the evidence that belongs in the first one, so a good part of that file already
exists — it needs a home, not research.

This is the same defect as a handoff pointing at an untracked file: a reader
follows a reference into nothing. Note also that `docs/` is gitignored, so a new
file there needs the same force-add the other three got, or it will exist on the
author's disk and nowhere else.

### 5.3 Three doc claims remain unverified — they need hardware

From the audit §2, all stated as behaviour in the shipped manuals and none
reproduced in this environment:

- **Paused / controls-locked:** "`f/s` reads near 0 while paused, `tk` can still
  read 60 beside it." Consistent with `lv.c` (the serial increments before
  `g_ClockTimer` is zeroed) but never executed — the console has no pause verb.
- **Sub-ticking:** "`f/t` below 1.00 at a raised tick rate." Needs a renderer
  that beats 60 fps; the software renderer tops out near 38, and the lowest
  `f/t` measured anywhere was 1.03.
- **vsync pinning `tk` at 60:** needs a real 60 Hz panel where vsync, not the
  machine, is the limit.

A machine with a working GL driver settles all three in an afternoon. Until
then they are the manual's most load-bearing claims resting on inference.

### 5.4 D4's fix was never observed under a live reset

The negative-`TPS` guard (`portStatsReseatTickBaseline` plus the single `tickNow`
snapshot) is covered by unit test, and `tps-the-lie.md` reports 98 consecutive
live lines with no negative. But no run has produced a stage-load reset inside a
stat window with a non-zero pre-load count. The race the snapshot exists for is
reasoned, not seen. Worth an attempt with a long pre-load session and a load that
lands mid-window; say so in whichever document you correct.

---

## 6. Output expected

- One commit per task, message stating what is wrong, what is true, and how it
  was verified — including which claims were executed rather than read.
- If Task B changes the cap, `docs/port/tick-and-field-accounting.md` §5.2's
  DEFERRED section must be updated to say what was decided and why. Do not leave
  it claiming the item is deferred.
- If Task A changes the writer, `tools/midas_ini_help.txt`'s `tickhz` row must
  change with it. It currently documents the behaviour you are about to change.
- Whenever you correct a document, check the other documents that describe the
  same behaviour. `console_help.txt`, `midas_ini_help.txt`, the log legend in
  `audio_pc.c`, `tick-and-field-accounting.md` and `tps-the-lie.md` overlap on
  purpose and drift apart silently when one moves alone.
- If you correct a claim the audit made, say so in the audit (§6 is the place)
  rather than editing its findings. The audit is one session's work, not
  scripture: it has already had to retract a finding of its own, and that
  self-correction is the most useful thing in it.
- If you settle any of §5.3's three claims, move them from "unverified" to
  measured in `docs/port/audio-pump-audit-pass3.md` §2 and say what the
  measurement was, including which renderer produced it.

---

## 7. The two reference documents

- `docs/port/audio-pump-audit-pass3.md` — the third-pass audit. Findings,
  unverified items, the execution record with predictions verbatim, and §6's
  post-fix numbers. Force-added, so it is in the tree and not only on a disk.
- `docs/port/tick-and-field-accounting.md` — the design record. §5.2 has the
  DEFERRED D5 section you are inheriting; §10 has the three-caps question;
  §11 has the hw-pacing measurements that belong in the missing
  `n64-hw-pacing.md`.

---

## 8. Pass 4 — the sign-off review

A fourth pass, read-only, reviewed the three commits that landed after the
third-pass audit (`52807f7`, `cb98c39`, and the audit record itself). It was run
by the session that wrote the pass-3 brief, which is worth knowing: it had a
stake in the audit being right, and it is the same session whose own
documentation the audit had just falsified.

### 8.1 What was verified, and how

**Each commit builds and passes on its own.** `52807f7`, `cb98c39` and the audit
commit were checked out separately: `errors=0` and `100% tests passed` on all
three, `ctest` 9/9 at the tip. The only compiler output is the pre-existing
`__DATA,__common` alignment linker warning, present on the untouched base too.

**The clamp-report concurrency, by reading rather than running.** `cb98c39`
publishes `g_PortTickClampMeasured` / `Credited` behind `g_PortTickClampSeq`,
written on the game thread and read on the render thread. Checked:

- the sequence is bumped only where the clamp fires, and the reader's
  `s_LastClampSeq` starts at 0 to match the global's 0, so the first clamp is
  not missed;
- the pair is re-read and the sequence re-checked after both value loads, so a
  reader that catches half a verdict discards it rather than reporting a torn
  shortfall;
- the whole block runs *before* the hitch threshold test, so a suppressed line
  cannot leave a stale clamp for a later line to print;
- buffer bounds hold: `" credit %u of %u"` worst case 32 bytes plus
  `" pacer asked %d"` worst case 24 is 56, inside the 64-byte `hw[]`.

No race found. The release store on the sequence and the acquire loads on the
read side are the right pairing for this shape.

One bound worth knowing, not a defect: the report is *last clamp wins per
present*. If two ticks clamp between two frames, the line shows the later pair
and the earlier shortfall is not printed. In the measured hw-pacing run each
367 ms frame ran a single tick, so this did not bite — but a fifth pass looking
at sub-tick or raised-rate configurations should not read a `credit` line as a
complete account of the frame's loss.

**`videoPersistDerivedTick` returns before writing when the tick is `hw`** —
`port/src/video.c:929` tests `s_CfgTickHz == -1` first. F3 is correct, and the
manual's old claim really was false.

**The `tk`-tracks-`fps` rule is structural, up to a factor of two.** `src/boss.c`
admits a tick only when `pendingGfx < 2U` (`boss.c:693`): `pendingGfx` is
incremented on submit (`:862`) and decremented on `OS_SC_DONE_MSG` (`:877`), so
ticks are paced by *completed gfx tasks*, not by the loop and not by the
configured rate. The bound is therefore two ticks per completed task, not one —
which is the audit's own F2 correction, and this section originally over-stated
it as one. What matters for F2 either way: `tk` cannot outrun the present rate
by more than that factor, so it reports the renderer rather than the configured
rate whenever drawing is the limit.

Two caveats on the same point. With `render_thread=1` the present happens on the
render thread, so "one present per loop iteration" is loose even in the
one-tick case. And no measurement here observed the 2x: `tk == fps` in 114 of
115 windows across three sessions, and the user's captures read 60/60, 738/738,
15/15, 20/20, 832/832. F2 is correct — the manual's three causes are instances
of this one gate — but the *ceiling* is two per task in principle and one in
every observation we have.

One claim in the previous version of this section could not be checked and is
recorded here rather than deleted: it cited a pass-2 measurement of `FPS 61 tk
61` for `GE_TICKHZ=120` with vsync on. Pass 3's own attempt at that
configuration (`run5`) measured `fps 30 tk 30`, because the software renderer
bound first, so the audit lists the vsync case as **unverified** (§2). No
pass-2 report exists in the tree to check 61/61 against, so treat that figure as
unverified too. It does not affect F2, whose evidence is the 120 Hz case where
the loop was demonstrably running at 120 and `tk` read 38.

### 8.2 The one gap

**`credit N of M` and `pacer asked N` are documented nowhere a user will see
them.** The hitch line's format lives only in `hitchlog.c`'s header comment and
its inline comments. `tools/console_help.txt` has no hitch section at all — the
string "hitch" appears in it exactly once, in an unrelated sentence about `f/s`
under `f/t`. `cl_hitchms` is documented only in the in-game `help` text
(`port/src/port_console.c`), not in the shipped manual.

This is minor as a defect and not a blocker: nothing states anything false. But
it is the branch's own instrument. `credit N of M` is the only *per-frame*
evidence of D5 time loss — `f/s` below 60 shows the same loss only in aggregate,
after it has happened — and the person most likely to need it, someone
attaching a log to a bug report, has no shipped text telling them what it means
or that its presence is the signal. If §2 (D5) or §3's Task B changes the cap,
the field's meaning changes with it, and it should be documented in
`console_help.txt` in the same commit.

### 8.3 How this branch was verified, end to end

Four passes, alternating method, each finding what the previous could not see.
That alternation is the reason the branch is in its current state rather than in
the state pass 2 left it.

- **Pass 1 — implementation.** Built the four numbers and corrected the manuals
  from reading. Verified by a handful of live runs. Shipped a false
  `maxfps 30` sentence while "fixing" an earlier version of the same sentence,
  and shipped `tps-the-lie.md` inside the commit that made its central claim
  false.
- **Pass 2 — documentation review.** Reversed the direction: the documents became
  the specification and the code the evidence. Read-only, so it could catch
  structure — a wrong general rule, a counter named wrongly, a document lying
  about its own commit state — and could catch nothing empirical. Its own report
  arrived truncated and it verified no measurement at all. **"Twelve findings,
  all fixed" is inherited, not checked**: no pass-2 report exists in the tree to
  count against, so treat the number as the pass-2 session's own account.
- **Pass 3 — execution audit.** Blind, execution-first, and required to write
  predictions before each run. Six findings, five of which no amount of reading
  would have produced: `model held` versus `fld` reporting "healthy" while 63%
  of wall time was discarded, `f/s` reading 0.8-1.4 during a load rather than 0,
  a healthy band of 59.3-62.9 where the docs said 60.0, and log columns that
  freeze during a hold. It also retracted a finding it had first rated major from
  reading alone — the same failure it had been sent to find.
- **Pass 4 — this review.** Read-only again, checking pass 3's claims against
  the code and against pass 2's own prose. Confirmed three of pass 3's findings
  and found the documentation gap in §8.2.

The pattern worth carrying: **reading catches structure, execution catches
sentences about measured quantities, and neither alone has yet been enough on
this branch.** Every pass fixed real defects and introduced at least one of its
own. Anyone doing a fifth pass should assume the same of themselves and write
their findings down before they edit anything.

### 8.4 What this review did not verify

- The Dam / hw-pacing measurements behind F1 and §6 of the audit: the 311 hitch
  lines, the 241 carrying `credit`, the 35-227 field shortfalls. Not re-run here;
  taken from the audit's execution record, which states its renderer.
- F5's measured healthy band (59.3-62.9). Not independently reproduced; this
  environment's renderer is the same software one, so a reproduction would test
  the same conditions rather than new ones.
- §5.3's three claims that need a GPU. Still unverified, as the audit says.
- D4's fix under a live stage-load reset. Still unexercised, as the audit says.

Those are inherited, not discharged. A fifth pass that can run on real hardware
should treat them as its first order of business.

### 8.5 Sign-off

The branch is **safe to push** on the evidence available here, with one
qualification: the `credit` field ships undocumented (§8.2). The numerical core
is tested; the two remaining code items (F3-A, D5) are correctly identified as
open and are not claimed as done anywhere in the tree.

This section first read "nothing in the review found a false statement". That
was itself false: §0 of this same file said the audit report was untracked while
the commit carrying it had force-added it, and §8.1 above restated the audit's
retracted one-tick-per-present claim as structural. Both are corrected in this
version. The sign-off therefore rests on: no failing test, no broken commit, no
race found in the clamp report — not on the review having been error-free.

What would change this verdict: a hardware run that contradicts the audit's
band or its load readings, or a fifth pass that finds the pattern above has held
again.
