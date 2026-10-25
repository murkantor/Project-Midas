# settings and save audit

The sweep `NEXT_LEVEL_AGENT.md` asked for: for every item in its §2, does the
saved state match the running state, and does it survive a round trip?

Method: read the owning code, then execute the case. Every verdict below says
which. Claims here are derived from this tree at the commits named in
"Fixed in this pass"; nothing is copied from `CLI.md` or the design records
without re-checking it, because this branch has shipped false statements in
both.

Guards held throughout: `build_macos/midas.ini` was backed up before each
experiment and restored after (`md5 eba8465ce4711eb86226c13d3eadce3e`), and
`Data/Saves/eeprom.bin` stayed at `3590c7df0edb0930f4e4248826e8ab64`.

---

## The headline: the forced values are an event, not an invariant

Classic Mode works by saving your values and forcing seven of them
(`portClassicSetLive`). That forcing happens **at the moment of the toggle** and
is never re-asserted. Anything that reaches those settings afterwards therefore
leaves the mode **on with non-classic values**, and nothing in the game notices.

Three entry points reach that state, and only one of them is a hand edit:

- **A hand-edited `n64_aa=1`** boots with the flag set and the parts wherever
  they were. This is the handoff's §0.2 finding; confirmed here (executed).
- **The console's `cl_n64_aa`** bypassed the swap entirely, in both directions
  (executed — the sharpest of the three, and new). **Fixed** — see §2.2 and
  "Fixed in this pass".
- **An env override applied when the mode is already on** — `GE_LOWRES=480
  -n64_aa` with `n64_aa=1` in the file leaves `low_res=480` under Classic Mode
  (executed), because the boot flag's `!s_CfgN64Aa` guard correctly declines to
  re-snapshot, so nothing re-forces 240.

The `!s_CfgN64Aa` guard is right — re-snapshotting would overwrite the player's
saved values with the forced ones. The defect is that *the forced set is
maintained by a single event*. Fixing it properly means deciding whether the
seven values are derived state (recomputed from `n64_aa` + the `classic_*`
parts) or stored state (written once and defended). That is a product decision,
so it is recorded here, not fixed.

---

## §2.1 The surface

### Preserve-on-write: correct, with one undocumented asymmetry (executed)

Inserted a `;` note, a `#` note and an unknown key `zz_unknown=7` into
`[display]`, then changed one setting through the console. After the save:

- `zz_unknown=7` **survives** (and the loader warns `unknown control "zz_unknown"`);
- `# hash note` **survives**;
- `; semicolon note` **is dropped**.

So a hand-edited unknown key is not destroyed — the worry the sweep raised, and
it does not happen. But the two comment styles are treated differently: the
loader skips both, the writer drops only `;`. `tools/midas_ini_help.txt` says the
file "has no comments", so dropping `;` is at least documented in spirit, while
`#` surviving is not documented at all. **Verdict: correct behaviour,
undocumented asymmetry.**

### Every save rewrites the whole table (executed)

One console change of `hud_scale` logged:

```
ge: midas.ini: saved 77 [display] option(s)
ge: midas.ini: saved 24 [input] option(s)
```

`portOptSave` writes every row it can read, section by section. The header
comment's "only those keys change" means *only those keys change value* — the
other lines are rewritten byte-identically. Correct, but worth stating plainly
because it means a single menu step touches 101 lines of the file.

### The four writers: the table is imprecise (executed)

The handoff lists four writers, one of which is "`port_console.c`: one
`[display]` key from a console `cl_` command". The console actually has **two**
paths:

- a key that is a Port Options row goes through `portOptSetIni` →
  `optSet` + `portOptSave`, i.e. the **whole table** (`conCmdSet`);
- a key with no row (`room_cache`) goes through `videoSetDisplayOption` and then
  writes **one** key (`conCmdClient`).

Both were observed. The distinction matters because only the first path gets the
`classicPartChanged` bookkeeping — the second cannot, since it is by definition
about rows the table does not own. **Verdict: contradicts** (descriptively, not
behaviourally).

### The three guards

- `portIniWriteStrs` caches `allowed` on first call from `GE_WINDOW` /
  `GE_NOINISAVE` (read). Once cached, changing the env mid-run cannot re-enable
  writes. Harmless, since these knobs are set before launch.
- `videoIniWritableCheck` uses `fopen(..., "a")`, which **creates** the file
  (read). It runs immediately after `videoLoadConfig()`, and on a fresh install
  that call already wrote the default `midas.ini` (`s_IniFresh`), so the create
  side effect is moot in the normal path. **Verdict: intended, and harmless —
  but only because of the call order, which nothing states.**
- The window saver has its own `changed` check and its own `create=0`, so a
  hand-edited file with no `[display]` section is left alone (read).

---

## §2.2 The classic-mode snapshot

### The toggle and the watch: correct (read)

`portClassicToggle` (Tab) and the watch's `rowClassicSync` both call
`portClassicSetLive`, which snapshots on entry, forces the enabled parts, and
restores on exit. `rowClassicSync` guards the no-op case by comparing the value
before and after, so choosing ON while already on does not retake the snapshot.
`portClassicPartOn` decides per part, so a part switched off on the CLASSIC sheet
stays the player's.

**Not executed** — there is no headless way to press Tab or the watch row, and
the save-hold/key-injection knobs do not reach this UI. Recorded as read.

### The flag is idempotent and writes a correct snapshot (executed)

From `480/bilinear/2/60/4/0/0` with Classic off, `-n64_aa` wrote:

```
n64_aa=1  low_res=240  filtering=3point  anisotropy=8  maxfps=-1  tickhz=hw  msaa=2
mipmaps=1  filter_2d=1  classic_saved=480,1,2,60,4,0,0
```

A second `-n64_aa` run left the file **byte-identical**. **Verdict: correct.**

### The console bypassed the whole swap (executed) — DEFECT, FIXED

`cl_n64_aa` is a Port Options row, so it goes through `portOptSetIni` →
`optSet` → `classicPartChanged`. That handler only reacts when a **classic part
row** changes; it did nothing for `n64_aa` itself. Confirmed:

- `cl_n64_aa 0` from a Classic-on state changed exactly one line — `n64_aa=1` →
  `n64_aa=0`. `low_res=240`, `filtering=3point`, `anisotropy=8`, `maxfps=-1`,
  `tickhz=hw`, `msaa=2`, `mipmaps=1`, `filter_2d=1` all stayed, and
  `classic_saved` was not restored.
- `cl_n64_aa 1` from `480/bilinear/2/60/4/0/0` changed exactly one line —
  `n64_aa=0` → `n64_aa=1`. No snapshot was taken, and none of the seven values
  was forced.

So the console can produce both half-states: mode on with your own values, and
mode off with classic values forced. **Verdict: contradicts** the documented
meaning of the setting ("the classic_toggle key does this").

**Fixed** in `options: set n64_aa by text and Classic Mode's values follow`,
and re-executed against the same two cases: `cl_n64_aa 0` now restores all
seven and `cl_n64_aa 1` forces all seven and writes a snapshot, with a repeated
ON leaving the file byte-identical. The fix is at `portOptSetIni`, not in the
console, because it is the one setter whose callers do no syncing of their own
— which also closed the same hole for the Gameplay Options page.

Why it is not fixed here: the correct repair is to route `n64_aa` through
`portClassicSetLive` on **every** setter, not just the toggle. `portOptSetIni` is
shared by the console, the Port Options page and `front.c`'s Gameplay Options, so
that is a three-surface change needing its own review and its own test — not a
line to slip into an audit.

### A hand-edited `n64_aa=1` (executed, confirms §0.2)

With `n64_aa=1` and `low_res=480` in the file, a plain launch ran with
`low_res=480` — the flag set, the resolution not forced. Same root cause as
above.

### `portOptResetDefaults` (read, not executed)

It writes `n64_aa` raw first, before the other rows, "so its remembered values
can't come back over the defaults". Reading `portClassicSetLive`'s off-path,
bringing up the mode only restores where `portClassicPartOn(i)` is true and only
where a snapshot exists — so writing the flag first does prevent a restore from
overwriting the defaults. The comment holds. **Not exercised**, because Reset All
has no headless entry point.

---

## §2.3 The ordering hazards

### maxfps and tickhz: the invariant holds (executed)

Hand-edited a deliberate mismatch — `maxfps=60`, `tickhz=30` — and launched:

```
ge: video: tickhz follows maxfps - midas.ini 30 -> 60
ge: video: config: maxfps=60 ... tickhz=60 limiter=60
```

The file was corrected to `tickhz=60`, so the loader and the file agree after
the run. Changing `maxfps` live in the other direction produced
`tickhz follows maxfps - midas.ini hw -> 60`, i.e. the writer records the
resolved mode rather than leaving a stale number. **Verdict: correct**, and the
invariant `docs/port/tick-and-field-accounting.md` §11.3 states is the one the
writer keeps.

### room_cache is restored when hardware pacing is left (executed)

`cl_maxfps 30` from `maxfps=-1` logged `hw pacing off - room_cache 2 back on` and
the file kept `room_cache=2`. **Verdict: correct.**

### A crash in hardware pacing cannot strand room_cache (executed)

The forced 0 is memory-only: with `room_cache=2 tickhz=hw`, a run logs
`room_cache=0` and the file still says `room_cache=2` afterwards. **Verdict:
correct**, and better than the sweep feared — the setting was never saved off,
so there is nothing to strand.

---

## §2.4 The boot flags and the env overrides

Precedence is command line over environment, and that now holds for every knob
checked. See `docs/port/room-cache.md` for `GE_ROOMCACHE` in full.

- **`GE_N64AA=0 -n64_aa`** — flag wins, Classic Mode fully applied
  (`maxfps=-1`, `low_res=240`, `msaa=2`). Executed. Fixed earlier in `4ece424`;
  re-confirmed here.
- **`GE_LOWRES=720 -n64_aa`** with Classic off — `low_res=240`. Executed. The
  flag wins, as the code comment claims.
- **`GE_LOWRES=480` with Classic already on** — `low_res=480` under the mode.
  Executed. Not a precedence bug: the flag is a no-op by design, so nothing
  re-forces. This is the headline finding above, not a separate defect.
- **`GE_TICKHZ=120` with Classic Mode** — announced wherever the mode is on, not
  only when the flag turned it on. Fixed in this pass.
- **`GE_ROOMCACHE`** — moved before the flags, and refused under hardware
  pacing with a named log line. Fixed in this pass; see the doc.
- **`GE_HWMIPMAPS=0` with Classic Mode on** — now announced, because mipmaps are
  Classic Mode's and nothing else logged them. Fixed in this pass.
- **`GE_RENDERTHREAD`, `GE_SHADOWALPHA`** — read. Neither is owned by Classic
  Mode, so neither can produce a half-applied mode; no change made.

**A methodological note worth keeping.** A first attempt at the `GE_LOWRES` case
appeared to show the env var beating the flag. It did not: `n64_aa=1` was
already in the file, so the flag never ran. The same confounding produced a
false `GE_HWMIPMAPS` reading earlier. **Both false findings came from the user's
own ini being Classic-on**, which is a state three of this branch's
investigations have now tripped over. Any precedence test must set `n64_aa=0`
explicitly and check the `-n64_aa -> Classic Mode on (applied)` line before
believing the result.

---

## §2.5 What the menus write, and when

- **`sSaveHold`** — `portOptSaveHold(1)` sets a flag and arms an `atexit`
  flush; `portOptSave` returns early while it is set. So quitting mid-ramp
  **does** save (read: `portOptSaveFlushAtExit`). A **crash** mid-ramp does not,
  because `atexit` does not run. The ramp is a single analog edit on one sheet,
  so the exposure is one setting. **Verdict: correct for quit, silent loss on
  crash** — acceptable, undocumented.
- **The window saver** — `videoSaveWindowIni` is registered with `atexit` and
  writes `window_width/height`, `window_x/y` and `window_display` only if they
  changed. So a crash saves nothing, and a window moved to a disconnected display
  is restored off-screen. The user's own file has `window_x=-1568`,
  `window_display=1`, which is exactly that shape. **Verdict: documented
  behaviour, real hazard**; noted in `CLI.md` §7.1 for the display part but not
  for the crash part.
- **`videoIniWritableCheck`'s `fopen("a")`** — creates the file if absent (read).
  Covered under §2.1; moot because `videoLoadConfig` runs first.

---

## Documentation defects found, not fixed

Each is a one-line correction to a shipped file and belongs in its own change.

1. **`tools/midas_ini_help.txt` says `room_cache` defaults to 2.** In this build
   it is 0: `video.c` gives 2 only under `#ifdef GE_XBOX`, and `GE_XBOX` is not
   defined in `build_macos` (`grep -c GE_XBOX build_macos/build.ninja` → 0).
2. **`port/fast3d/gfx_roomcache.inl` says "default OFF everywhere since the
   2026-09-11 A/B",** while `video.c` gives the Xbox build 2. One of the two is
   stale (read, not resolved).
3. **Neither `CLI.md` nor the ini help states the precedence rule for
   `room_cache`** — that hardware pacing owns it and wins over
   `GE_ROOMCACHE`. Now stated in `docs/port/room-cache.md`, but that is a design
   record, not the user-facing help.
4. **`mipmaps` is a Port Options row whose value no longer drives anything.**
   `videoMipmapMode()` reads `n64_aa` and the `classic_mipmaps` part; `s_CfgMipmaps`
   is written by the ini and the row and read by nobody (`grep -n s_CfgMipmaps`
   → only the set and get arms, plus its own comment saying it is kept "for the
   classic_saved slot"). The comment is honest; the **menu row** is not — it
   steps a value that changes nothing, and nothing says so. Read, not executed:
   there is no observable signal for the mipmap mode in any log line, which is
   itself why this went unnoticed.

---

## Fixed in this pass

Five commits for the sweep's own findings, one subject each:

- `scripts: report aborted tests as FAIL, and guard the demo's exit state`
- `video: announce GE_TICKHZ under Classic Mode whenever the mode is on`
- `video: apply GE_ROOMCACHE before the boot flags, and never under hw pacing`
- `docs: what room_cache is, who owns it, and the env override that broke it`
- `video: say when GE_HWMIPMAPS=0 contradicts Classic Mode being on`

Then the Classic Mode repair, from `plan.md`, in its planned order:

- `tests: pin Classic Mode's forced values as a pure rule` — the rule itself
  (`port/src/classic_mode.c`) and the cases written from the four defects.
- `console: cl_classic, and the boot report for a half-applied Classic Mode` —
  a half-state is now named at boot, and `cl_classic [status|fix|on|off]`
  repairs it. `fix` never re-snapshots, which is asserted by turning the mode
  off afterwards and getting the original values back.
- `options: set n64_aa by text and Classic Mode's values follow` — the console
  defect below, closed.
- `tests: drive portOptSetIni, so the setter that lost the call is covered` —
  the wiring, not the rule. Links the shipped `port_options.c` against a fake
  option table; mutation-checked by disabling the call and by removing the
  "only on a real change" guard, each of which fails a different case.

Each of those was verified by execution; the commit messages carry the runs.

## Recorded, not fixed — the named defects

1. **The forced values are an event, not an invariant** (headline). Three entry
   points; the fix is a product decision. A half-state is now *reported* and
   *repairable* (`cl_classic fix`), which is option 4 from `plan.md`; making it
   unreachable is Phase 2 there.
2. **The `mipmaps` menu row is inert** (documentation list, item 4).
3. **A crash mid-save-hold or mid-session loses the ramp value and the window
   geometry** (§2.5). `atexit` is the mechanism and it cannot run on a crash.
4. **Restoring a window onto a disconnected display is silent** (§2.5).

---

## Phase 3 — the key split (this pass)

`plan.md` §5. `n64_aa` no longer means two things. This section is appended
rather than folded into the record above: the findings there were true when
written, and the point of keeping them is that a later reading can check what
the earlier one missed.

**What changed.** `[display] classic_mode` is the preset — the only key that
forces anything, and what every "is the mode on" question reads.
`[display] n64_aa` is the AA bit alone. `classic_n64aa` is the only link: on,
the preset sets the bit on entry and clears it on exit; off, it leaves the bit
to the player. Setting the bit never touches the preset, which is what makes
"N64 edges without 240p" reachable — the state the single key could not express,
and the reason some of the half-states above existed at all.

**The migration.** Old `n64_aa=1` → `classic_mode=1` and `n64_aa=1`; old
`n64_aa=0` → both 0; a file with `classic_mode` or the `classic_split` marker is
left alone. The decision is a pure function (`port/src/classic_split.c`) with a
test, because it rewrites the player's real file.

**Executed**, with the user's `midas.ini` backed up and restored byte-identical
afterwards (`eeprom.bin` unchanged at `3590c7df0edb0930f4e4248826e8ab64`):

| Run | Result |
| --- | --- |
| old file (`n64_aa=1`, no `classic_mode`) | `classic_mode=1 classic_split=1`; log: `n64_aa split - classic_mode=1 (the preset) beside n64_aa=1 (the AA bit)` |
| same file, `classic_mode` hand-set to 0 | stayed 0 — the marker held |
| `cl_n64_aa 1` from preset-off | `n64_aa=1`; the seven untouched; no snapshot; `classic_mode` still 0 |
| `cl_classic_mode 1` from the same file | the seven forced; `classic_saved=480,1,2,60,4,0,0`; `n64_aa=1` through the part |
| `cl_classic_mode 0` | the seven restored exactly; `n64_aa=0` |
| `-classic` / `-n64_aa`, preset off | both force the seven; each logs its own spelling |
| hand-edited `low_res=720` with the mode on | boot report names `low_res`; `cl_classic fix` repairs it without re-snapshotting |
| `GE_N64AA=1`, preset off | `low_res=480 maxfps=60 msaa=4` — the bit, and no forcing |

**Not executed, and named as such.** The VIDEO sheet and the in-game watch have
no headless harness and were not driven: their rows were changed and reasoned
about (the sheet also pends the bit, because Apply Changes lands the whole sheet
under the hold that suppresses the choke point), and the build plus the seam
tests are the evidence. This is the same gap the section above records for those
two surfaces — Phase 3 did not create it and did not close it.

**One behaviour change, deliberate.** `GE_N64AA` now sets the bit and no longer
forces Classic Mode; `-classic` is the flag that does that. `CLI.md` §3.2 says so.
