# The N64 frame-pacing model (`tickhz=hw`)

What `tickhz=hw` reproduces, what the model is made of, how its numbers were
fitted, and what it does *not* prove. Cited from `src/game/frametiming.c` (the
model header, the load-charge note, `portHwPaceFields`) and
`src/game/ramromreplay.c` (calibration from the previous frame's RDP workload).

The design record for the tick itself — what a field is, how many are credited,
`f/s` and `f/t` — is `docs/port/tick-and-field-accounting.md`. This document is
about the *cost model* that decides how long one frame of hw pacing lasts.

**Where the session measurements live, and why they did not move here.** The
design record's §11 ("hw pacing: what a real session turned up") is the
measurement record: D5 not firing at ordinary frame rates (§11.1), the Dam load
priced and held (§11.2), the `maxfps=-1` / `tickhz=0` pair (§11.3) and the
`GE_STAGE` limitation (§11.4). Three of those four are about the *tick and the
ini*, not the RDP model — §11.3 is the record of the `tickhz` writer, which a
reader chasing the ini would never look for under a document about the RDP. So
§11 stays where it is, and this document owns the model the field hold is built
from. Moving it would also renumber §11.1–§11.4, which are cited from
`audio-pump-state-of-play.md` and from the design record itself.

---

## 1. What the mode is

`tickhz=hw` runs the **classic 60 Hz simulation** and holds each frame for the
number of video fields the console would have needed to draw it. It is Rare's
frame pacing, not a raised tick rate: the sim steps at 60, the *present* is
late, and `g_ClockTimer` comes out as 2, 3, 4 … exactly as it did on hardware.
That is the mode that reproduces console slowdown instead of hiding it.

The hold is applied in `boss.c`, before the first frame of a stage load as well
as between frames, and `waitForNextFrame()` credits the fields elapsed — see
`docs/port/tick-and-field-accounting.md` §3 for the mode table and §5.2 for the
credit cap that used to truncate it.

**Where it is active:** `portTickHwPace()` — the flag from the ini, *and* not a
demo recording, *and* no ramrom replay in flight, *and* `portStageRunning()`,
*and* no watch menu. Menus, demos and the front-end deliberately run without it;
the comment there records that pricing menu frames made them run at 30 and
slowed their mouse and timers. (That comment says "the model's fixed cost alone
is two fields". The constant term is 13.17 ms, under one field, so the figure is
the fixed cost *plus* an ordinary menu draw — the arithmetic does not support
"alone", and this document does not repeat it.)
`GE_STAGE=` launches never reach `portStageRunning()` at the point the
stage-load hold would fire, so that path cannot be used to observe the hold
(§11.4).

---

## 2. The render model

`portHwPredictMs()` (`src/game/frametiming.c`) is linear in eight workload
counts plus a constant. The counts come from `struct PortHwLoad`
(`port/fast3d/gfx_api.h`), reconstructed from the same display list at 320×240 —
`g_PortHwLoadLast` is the last completed frame.

```
ms = c0 + c1*fill2/1e3 + c2*fill1/1e3 + c3*fillx/1e3 + c4*fillr/1e3
       + c5*loads + c6*lbytes/1e3 + c7*tris + c8*chrs
```

| term | coefficient | what it counts |
| --- | --- | --- |
| `c0` | 13.174978 ms | fixed cost of a frame |
| `fill2` | 0.027856 µs/px | triangle pixels drawn 2-cycle |
| `fill1` | 0.000000 | triangle pixels drawn 1-cycle — **fitted to zero** |
| `fillx` | 0.050997 µs/px | triangle pixels with `ZMODE_XLU`, counted *in addition* to fill1/fill2 (`gfx_pc.cpp`) |
| `fillr` | 0.049921 µs/px | rectangle (fill-rect) pixels |
| `loads` | 0.000000 ms/load | TMEM loads — **fitted to zero** |
| `lbytes` | 0.028149 µs/byte | TMEM load bytes |
| `tris` | 4.372 µs/tri (0.004372 **ms**) | triangles |
| `chrs` | 152.0 µs/char (0.152045 **ms**) | characters simulated this tick (`g_PortHwChrsTicked`) |

**Two unit conventions in one formula, which is a trap.** The fill and byte terms
carry a `* 0.001f` on the *count*, so their coefficients read directly as µs per
unit. The last two do not — `sHwModel[7] * tris` and `sHwModel[8] * chrs` are
multiplied by the raw count, so their coefficients are **milliseconds** per unit.
Read as µs they look 1000× too cheap, which is what a first draft of this table
did: 0.004372 µs/tri would make the whole triangle term 4.4 ns each and vanish.
Whenever a coefficient here is quoted, quote its unit with it.

Two zero coefficients are not missing terms: the fit decided 1-cycle triangle
pixels and TMEM load counts carry no weight beyond the other columns. Anything
reading the model must therefore not assume a non-zero cost per TMEM load or per
1-cycle pixel. `fillx` is a surcharge, not a fourth bucket: a translucent-z
triangle contributes to fill2/fill1 *and* to fillx.

**The caps, in the order they apply** (`portHwPaceFields`):

1. the render model is capped at **6 fields** (100 ms) *before* load charges;
2. the load charge for the frame is added on top;
3. the total is rounded **up** to whole fields (`ceil(ms / 16.667)`) and clamped
   to **1..30** fields — the 30-field ceiling is the model's own safety limit.

The 6-field cap bounds the *fit*, not the tick: the model was fitted against
hardware footage, and whether that footage already contained load hitches is
unknown.
Load charges are added after it precisely so a load hitch is not swallowed by
the render cap. `portHwPaceFields()` publishes the field it priced in
`g_PortHwFieldsHeld`, which the hitch log reads and which the credit cap now
yields to (§5.2 of the design record, D5).

---

## 3. Load charges

The console loads in-frame: a weapon swap, a guard's body or a room coming into
view DMAs compressed bytes off the cart and inflates them on the CPU, so that
frame runs long. `tickhz=hw` charges it too. Each load site notes the number of
ROM bytes it read (uncompressed reads charge the cart DMA alone: `compressed`
is 0 for those) — `ob.c` file loads and room bytes, `image.c` texture loads,
`objecthandler_2.c` model textures for private-pool loads (the hands, which the
N64 re-inflates on every swap) — and the next frame's hold adds the time.

Rates, fitted to the N64 bench ROM (867 file loads over every mission, per the
comment in `frametiming.c`):

- **cart DMA** — 174 µs per read **+ 0.2025 µs per byte**
- **inflate, during a stage load** — 714 µs **+ 1.254 µs per compressed byte**
- **inflate, in play** — 326 µs **+ 1.479 µs per compressed byte** (the other
  threads share the CPU then, so the per-byte rate is *higher* but the fixed cost
  lower)

A stage load additionally prices the **setup CPU work** it does, from counters
taken while `lvlStageLoad` runs (`portHwStageCpuUs`):

| counter | µs each |
| --- | --- |
| `MATCH_CALLS` / `MATCH_STEPS` | 70.47 / 0.7923 |
| `NEAR_CALLS` / `NEAR_TILES` / `NEAR_POINTS` | 222.17 / 11.686 / **19.025** |
| `WALK_CALLS` | 34.22 |
| `SETUPIDX_STEPS` / `CHRFND_STEPS` | 0.675 / 1.099 |
| `GUARDS` / `OBJECTS` / `TEXTURES` | 692.6 / 827.8 / 520.3 |
| cart bytes | 370.9 per KB |
| constant | 12,280 |

The sum becomes the stage's load time (`portHwLoadStageLoaded`), which `boss.c`
holds before the first frame — the 8.7-second Dam entry recorded in §11.2.

---

## 4. Provenance, and the open question

- The **render model** was fitted against **14 attract demos** (the comment names
  `hwfit.py`). That script is **not in this tree** (searched), so the coefficients
  cannot be re-derived from the repo; they are inherited, not reproducible here.
- The **load rates** were fitted to the N64 bench ROM's 867 file loads. The
  comment gives the fitted rates, not the data behind them, and no dataset for
  them is in the tree either.
- The **near-points coefficient is pathological and unproven.** 19.025 µs at the
  N64's 93.75 MHz CPU clock is **~1,784 cycles** for what should be a distance
  test of tens of cycles. In the one measured Dam load it is
  **221,600 points × 19.025 µs = 4,216 ms**, 67.7% of the 6,225 ms setup CPU
  total (§11.2). Two candidate explanations, neither tested: the term absorbs
  work that has its own counter (candidate matching runs only 462 of 695,526
  steps, so the coefficient looks pathological on that path), or it is pricing a
  cache miss on a large table. **Not proven; no access to the fit data.** Re-fit
  before trusting the stage-load total.

---

## 5. What the hold means, and what it does not

- **It is fidelity, not a hang.** The port finishes the real Dam load in a
  second or two and then waits out the remainder of the 8.7 s. That is what the
  console did.
- **The blank screen during a hold is white, and should be black** (user report,
  macOS). Nothing is drawn while the hold runs, so the window shows the last back
  buffer. Deferred twice; two separate questions — whether to clear before the
  hold, and whether a loading indication belongs there. §11.2.
- **`tickhz=hw` is not `portTickHwPace()`.** The ini mode can be set while the
  game is in a menu, a demo or outside a stage, and then no hold is applied.
  Every measurement of the model must say which of the two states it was in.
- **`GE_HWSCALE=3` saturates the render model against its own 6-field cap**, so a
  scaled run's holds above 8 fields are 6 render fields plus a load charge —
  which is why the D5 evidence landed on load frames (§5.2 of the design record).

**Not verified:** any of it against real hardware footage. The numbers in
`frametiming.c` were fitted elsewhere; this port has never compared its holds
with a captured console frame time. Windows and Linux were not run. The
stage-load figure is a single Dam measurement.

---

## 6. Knobs

All environment-only:

- **`GE_HWSCALE=<f>`** — multiplies the render-model prediction (1.0 = the fit).
  At the 3 used to exercise the D5 path it saturates the render model against its
  own 6-field cap, which is useful for forcing holds above 8 fields and useless
  for measuring the fit.
- **`GE_HWLOAD=0`** — turns load charges off entirely.
- **`GE_HWLOAD_SCALE=<x>`** — scales both the cart DMA and the inflate charge.
- **`GE_HWDBG=1`** — prints the priced frame once a second and the stage-load
  price at the end of a load. **`GE_HWDBG=2`** — prints every load as charged.

The hold itself is not separately knobbable: it is the model's output. The
*credit* cap that used to truncate it is (`GE_TICKMAXSTEP`, see D5).
