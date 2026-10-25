# The TPS lie

A short record of the one false number this port shipped to users: the `TPS` field
in the `cl_audio_pump` block and in `Data/Logs/audio_pump.log`.

`TPS` was labelled *ticks per second* and documented as *the simulation step
rate, set by Max FPS*. It was neither. It was `min(tick rate, 60)`, and below
60 fps it was simply the frame rate under another name. It also printed negative
numbers after every stage load.

Everything below is quoted from the tree or from git history, and every claim
carries the command that re-derives it. §7 has the commands in one block.

---

## 1. The lie in one sentence

`lv.c` increments the counter `TPS` divides **once per tick that advanced the
game clock by at least one field**, however many fields that tick absorbed — so
the value could never exceed 60, could never distinguish 60 Hz from 120 Hz, and
tracked `FPS` exactly whenever frames ran slower than one field.

The counter is `g_GlobalTickCount`, and the increment is `+= 1`, not `+= whole`
(`src/game/lv.c:1523-1526`):

```c
if (g_ClockTimer > 0)
{
    g_GlobalTickCount += 1;
```

A tick that absorbed three fields adds one. That is the whole mechanism, and
everything the label got wrong follows from it.

### The arithmetic, in full

With the field carry in `waitForNextFrame` (`src/game/frametiming.c`), each tick
is credited `whole` fields, and `g_ClockTimer` is that number (or 0 when paused
or controls-locked). Therefore:

- **Ticks per second that satisfy `g_ClockTimer > 0`** = min(tick rate, field
  rate).
- Fields arrive at ~60/s whenever the sim is keeping up, and a tick is only
  counted when at least one lands, so **the published rate is
  `min(tick rate, 60)`**.
- Below 60 fps every tick absorbs ≥1 field, so every tick counts and
  **TPS == FPS exactly**.

There is no reading of the code under which that counter is "simulation steps
per second".

---

## 2. Where the lie was told

Four places, quoted exactly as they shipped.

### 2.1 The manual — the one users read

`tools/console_help.txt`, as of commit `3a1ab62`:

```
TPS       Simulation steps per second. Normally 60, whatever FPS is: that is
          the tick rate, set by Max FPS in the menus. FPS well above TPS just
          means frames are cheap enough to draw more often than the game
          updates, which is normal and harmless.
```

Three false claims in two sentences:

- *"Simulation steps per second"* — false at any raised tick rate. At
  `tickhz=120` the sim steps 120 times a second and the digit still said 60.
- *"Normally 60, whatever FPS is"* — false below 60 fps. At 15 fps it read 15.
- *"that is the tick rate, set by Max FPS"* — false everywhere. It is
  `min(tick rate, 60)`, and Max FPS only reaches the tick rate through the
  coupling in `port/src/video.c`.

The last clause is the most damaging part: *"FPS well above TPS just means
frames are cheap enough to draw more often"* tells the reader that the one case
where the number is correct-by-accident is the case that needs explaining, and
invites them to dismiss the case where it is wrong.

### 2.2 The log header — the lie users attach to bug reports

`port/src/audio_pc.c`, legend written into every `audio_pump.log`, as of
`3a1ab62`:

```
#   FPS/TPS rendered frames and simulation ticks per second
```

Same claim — *simulation ticks per second* — in a file users are explicitly
asked to attach to reports. A wrong diagnostic label outlives the session that
produced it; that is why this one mattered more than a UI string.

### 2.3 The source comment — the lie told to the next developer

`port/fast3d/gfx_sdl2.cpp`, as of `3a1ab62`:

```c
/* Title-bar stats: rendered fps + engine FIELDS/sec (g_GlobalTickCount counts
 * video fields, not loop ticks - see lv.c) + the GE_TICKHZ configuration
 * and the actual pump rate. Updated twice a second; cheap enough to leave
```

A third name for the same counter: *engine FIELDS/sec*. It is fields/s only when
frames fit inside one field; in the hw-pacing capture it was ticks/s. Three
labels — ticks/s, fields/s, "simulation steps" — on one number, none of them
true in general.

### 2.4 The upstream comment — the cause, still live

`src/game/lv.c:1520-1522`, which is still in the tree today:

```c
/* GE_TICKHZ: g_GlobalTickCount must count 60Hz fields, not ticks,
 * because half- and third-rate work (chraction.c `& 1`, bondview2.c `%
 * 3`) keys off it. Always true at GE_TICKHZ=60. */
```

*"Always true at GE_TICKHZ=60"* is the root of the whole problem. It is true only
at ≥ 60 fps. Below 60, `g_GlobalTickCount` counts **ticks**, each worth 2–3
fields. The exact counterexample: at 15 fps, `g_ClockTimer` is 4 and
`g_GlobalTickCount` still advances by 1 — which is the measured row in §3.

This comment is **not** fixed. It is the only one of the four that still lies,
and it is recorded as a live item in §4 of
`docs/port/tick-and-field-accounting.md`.

---

## 3. The measurements that prove it

Three rows, each from a real run, with the screenshots in the repository root.
The `TPS` column is what a pre-fix build printed; the `tk` / `f/s` / `f/t`
columns are what the same window actually was.

**Vsync on, 60 Hz panel** (`vsync.png`):

```
60fps 60tk 60.0f/s 1.00f/t      ->  TPS 60    correct by accident
```

**Vsync off, uncapped** (`vsync_off.png`):

```
738fps 738tk 60.0f/s 0.08f/t    ->  TPS 60    sim stepped 738 times/s
```

**Heavy view, frame overruns a field** (`Screenshot 2026-10-25 at 02.50.32.png`):

```
15fps 15tk 59.8f/s 4.00f/t      ->  TPS 15    sim stepped 15 times/s
```

Read the third row carefully, because it is the one that started the
investigation. `59.8 f/s` says **game time is correct** — the world is not in
slow motion. `4.00 f/t` says each tick absorbed four fields. `TPS 15` says only
that 15 ticks finished a field, which is exactly `min(15, 60)`. A reader told
that TPS was "simulation steps per second" would conclude the sim was running at
a quarter speed. It was running at the right speed, stepping at the right rate
for a 15 fps view.

The second row is the mirror image: 738 sim steps a second, reported as 60.

---

## 4. The second lie: a negative tick rate

Independent of the label, the number could go **negative**.

`lvlStageLoad()` zeroes the counter (`src/game/lv.c:779`):

```c
g_GlobalTickCount = 0;
```

The stat pass's baseline is a `static` in another translation unit
(`port/fast3d/gfx_sdl2.cpp`, `stat_last_tick`) and was never rebaselined. The
first window after a load computed `0 - stat_last_tick`, publishing **−2× the
pre-load tick count**:

- 0.5 s of play before the load → `TPS -60`
- 5 s → `TPS -600`
- 20 s → `TPS -2400`

It lasted one ≤500 ms window and reached both the overlay and the log, so a
report could carry `TPS -600` as a permanent artifact of the load it happened
next to. A "ticks per second" field that prints −2400 undercuts every other
number in the block.

Fixed in the branch by re-seating the baseline
(`portStatsReseatTickBaseline`) and by snapshotting the counter once, since the
stat pass runs on the render thread when `render_thread=1`
(`port/fast3d/gfx_sdl2.cpp`).

---

## 5. What replaced it

The block's first row is now (`src/game/bondview2.c`,
`portDrawAudioPumpStats`):

```
"%dfps %dtk %d.%df/s %d.%02df/t"
```

Four numbers, each with one meaning:

- `fps` — frames presented per second.
- `tk` — simulation ticks per second, from `g_PortTickSerial`, which increments
  once per `lvlTick` call (sub-ticks and paused ticks included). It is the real
  tick rate the label always promised, with one qualification: a tick is taken
  only once the previous frame has been presented (`boss.c`'s `pendingGfx`
  guard), so `tk` follows the frame rate whenever drawing is the limit and will
  not show a raised tick rate the renderer cannot sustain. Measured, that is
  most of the time: every capture cited here reads `tk == fps`.
- `f/s` — fields credited to the sim per second. **60.0 means game speed is
  correct**, at any tick rate and any frame rate. This is the number that answers
  the question `TPS` was mistakenly believed to answer.
- `f/t` — fields per tick. 1.00 = one field per tick; above = frames overrunning
  a field; below = a raised tick rate sub-ticking; 0 = no game time credited in
  the window. `tk` can still read 60 beside a 0 here, because paused ticks are
  counted by `tk` and advance nothing.

`TPS` is redundant against the new pair — it is effectively `min(tk, fields/s)`, which is
`tk` when each tick clears a field and the field rate otherwise — so it adds no
information. It was removed from the overlay in `85fa9f1` and from the log line
and its legend in `2f8a6d0` (`port/src/audio_pc.c`, `portAudioLogLine`).

> **These two SHAs were wrong until 2026-10-25.** They read `ecfbe07` and
> `853163b`, which are pre-rewrite twins of those commits — same subjects, same
> file sets, different trees — left behind when the branch's early history was
> amended, and reachable from nothing on it. A reader following the citation got
> "unknown revision". Corrected to the commits that are actually in the branch.

Nothing in the branch prints it any more. At the tip of the branch the log line
reads:

```
t=%ums FPS %d tk %d f/s %d.%d f/t %d.%02d ...
```

and the legend has no `TPS` entry. `g_PortStatTps` is still computed and still
unread (§6).

---

## 6. What survives, and why

**`g_PortStatTps` still exists and is still computed.** It arrived with the
original decompilation import — `git log -S g_PortStatTps` bottoms out at
`718a122 "Project Midas"` — so deleting it is not this change's business, and it
is documented at its definition (`src/game/frametiming.c`) and in
`src/game/frametiming.h` as *computed but read by nothing*. Nothing depends on
it; it is not a label anyone sees.

**The `lv.c` comment in §2.4 still lies** and is the one live item. It is honest
about what it wants (`g_GlobalTickCount` is used as a field parity counter by
`chraction.c`, `bondview2.c`, `propobj.c`, `gunfire.c`), but the sentence
"Always true at GE_TICKHZ=60" is false below 60 fps. Left in place rather than
silently rewritten because the fix is a decision about parity semantics, not a
comment edit — see `docs/port/tick-and-field-accounting.md` §4 and §6 (H2).

---

## 7. Verifying all of this

```bash
# the mechanism: += 1, guarded by the clock
sed -n '1520,1527p' src/game/lv.c

# the old manual claim, exactly as shipped
git show 3a1ab62:tools/console_help.txt | sed -n '137,140p'

# the old log legend and log line
git show 3a1ab62:port/src/audio_pc.c | grep -n -A2 'FPS/TPS\|t=%ums FPS'

# the old overlay format
git show 3a1ab62:src/game/bondview2.c | grep -n 'FPS %dfps'

# the old source comment
git show 3a1ab62:port/fast3d/gfx_sdl2.cpp | grep -n -B1 -A2 'FIELDS/sec'

# the symbol's provenance
git log --oneline -S g_PortStatTps | tail -3

# the measured rows, against the row format
ls vsync.png vsync_off.png "Screenshot 2026-10-25 at 02.50.32.png"
grep -n 'fps %dtk' src/game/bondview2.c

# the arithmetic, as tested
cd build_macos && ctest -R tick_stats --output-on-failure
```

The screenshots are untracked and are the user's; they are cited, not committed.

---

## 8. Status board

- **Manual entry** — gone, replaced by `FPS` / `TK` / `f/s` / `f/t` field
  documentation. Fixed.
- **Log legend and log line** — legend rewritten, `TPS` removed from the line,
  in `2f8a6d0`. Fixed.
- **`gfx_sdl2.cpp` comment** — replaced by a WARNING block that says what the
  counter is. Fixed.
- **`lv.c` comment** — still live, still false below 60 fps. Known, recorded,
  deliberately not rewritten.
- **Negative `TPS`** — fixed by baseline reseat and a single snapshot; covered by
  the `tick_stats` unit test.
- **`g_PortStatTps` global** — survives from the original import, computed,
  unread, documented as such.

The lessons worth carrying, since the same shape recurs: a diagnostic label is a
claim about semantics and needs the same test as the code; three names for one
number means at least two are wrong; and a rate derived from a counter that is
reset elsewhere needs an explicit rebaseline, not a `static`.

---

This file lives under `docs/`, which is gitignored (`.gitignore`, `/docs/*`).
Staging it, if wanted, needs `git add -f docs/port/tps-the-lie.md`.
