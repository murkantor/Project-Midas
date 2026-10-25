# Handoff: split `n64_aa` into `classic_mode` + the AA bit

Paste this whole file into a fresh session as the task. It is self-contained.

You are doing **Phase 3** of `plan.md`: the last phase of the Classic Mode work.
Phases 0–2 are landed and verified. Phase 3 is the largest remaining change and
the one that needs the most care, because it renames a shipped ini key and moves
a dozen call sites that no headless test can see.

Read this file completely before running anything. It contains an inventory that
took a session of grep to build; re-deriving it is fine, trusting it blindly is
not — line numbers drift, symbol names are the authority.

---

## 1. What this is for

`[display] n64_aa` currently means **two different things at once**, and that
double duty is the root cause of the whole defect family this branch has been
chasing:

- **the VI AA bit** — the N64's edge blend and the resolved framebuffer. A render
  setting. Someone who wants N64 edges without 240p wants exactly this.
- **"Classic Mode is on"** — the preset flag that owns seven forced values, the
  pillars, the hitches, the composite filter, and the row-lock in the watch.

Because one key carries both, every entry point that sets the AA bit also
accidentally toggles a preset, and every entry point that toggles the preset
cannot turn on the AA bit without turning on everything else. Phases 0–2 made
the two meanings *consistent* with each other. Phase 3 makes them **separate**.

The destination, decided in `plan.md` §5:

- **`[display] classic_mode=0|1`** — the preset. Owns the forced values and every
  "is the mode on" question.
- **`[display] n64_aa=0|1`** — the VI AA bit only.
- **`[display] classic_n64aa=0|1`** — unchanged: whether the preset sets the bit
  when it turns on. This becomes the *only* link between the two keys.

The rejected alternative, recorded in `plan.md` §5 because it will occur to you:
keep `n64_aa` as the preset and add `classic_aa` as the bit. It needs no
migration, and it leaves a key named `n64_aa` doing preset duty forever — the
confusion the split exists to end. Do not take it without the user reopening the
decision.

---

## 2. State you are inheriting

Branch `diagnostic-launch-kit`. **Derive the tip and the counts, do not trust the
numbers below** — they were correct when written and the branch has moved since:

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
git log --oneline -1                    # the tip; it was a8e1fd3
git rev-list --count 6ef2530..HEAD      # 20 at the time of writing
git rev-list --count 3a1ab62..HEAD      # 49 at the time of writing
git log --oneline 6ef2530..HEAD         # read these; each message carries its evidence
```

Baseline, measured at the tip:

```bash
cmake --build build_macos -j8           # only the pre-existing __DATA,__common warning
./scripts/port/port_test.sh --no-game   # 13 passed, 0 failed
./scripts/port/test_the_tests.sh        # every mutant killed, baseline passes
```

Thirteen ctest targets: ten engine tests, plus `classic_mode`, `video_env`,
`port_options_n64aa`. The last three are the Classic Mode work.

### What landed, and why it matters to you

Read the commit messages — they are the evidence record, and several name
decisions you must not silently reverse:

- `5904317` **`port/src/classic_mode.c`** — the rule as pure functions
  (`portClassicPlan`, `portClassicMismatch`, `ClassicOp`). **Comitted before
  anything called it**, deliberately: the rule and its test, then the wiring.
- `87312d8` the adapter in `port_options.c` (`classicInputFill`,
  `classicPlanApply`, `portClassicMismatchParts`, `portClassicRepair`), the
  `cl_classic` verb, and the boot-time mismatch report.
- `b64ff76` `portOptSetIni` runs the transition for `n64_aa`.
- `248fe18` + `dba69cd` **`port/tests/port_options_n64aa.c`** — the seam test,
  now with a fake that records work (ordered write log, save and snapshot
  counts) rather than only values.
- `d54d2a0` **Phase 2: the choke point.** `optSet` in `port_options.c` applies
  the mode when `n64_aa` changes; the three per-surface copies were deleted;
  `portClassicSetLive` was **removed** rather than kept as an alias;
  `portClassicSetHold` suppresses the transition for surfaces that land a batch
  (the VIDEO sheet's Apply Changes, Reset All).
- `7e8d629` + `af6065a` **`port/src/video_env.c`** — the three GE_* overrides
  that can contradict a mode, as tested pure functions.

**The single most important structural fact:** the transition lives in `optSet`,
one choke point. Every surface that writes the table reaches it. Your job is to
rename *what* it watches, not to add another path.

---

## 3. The complete inventory

This is the work. Every item below is a place `n64_aa` appears in a semantic
role; the `read`-verified count is **14 `s_CfgN64Aa` references in `video.c`**
(plus 3 `portVideoN64AaOn`, its accessor), plus 12 `n64_aa` references in
`src/game/front.c`, plus 8 mentions in `port/src/port_watchpages.c` of which
**4 are code** (the rest are comments), plus the option table, the tests and the
docs.

Re-derive before you start:

```bash
grep -n 's_CfgN64Aa\|portVideoN64AaOn' port/src/video.c
grep -n 'n64_aa' port/src/port_options.c port/src/port_watchpages.c src/game/front.c
grep -rn 'n64_aa' port/tests/
```

### 3.1 `port/src/video.c` — the render state

Each site is one of three kinds. **You must decide which, per site, and write the
decision down.** The classifications below are a starting read, not gospel — the
commentary on each is my reasoning, and two are genuinely ambiguous.

| Site (symbol) | Current expression | Kind | Note |
| --- | --- | --- | --- |
| `s_CfgN64Aa` declaration | `static int s_CfgN64Aa = 0;` | state | Split into two statics. |
| `videoApplyAa` | `const int vi = s_CfgN64Aa;` | **BIT** | This is the VI resolve + edge blend. After the split it reads the new AA static and the code is unchanged — the meaning finally matches the name. |
| `videoRenderLines` | `if (s_CfgN64Aa && viy > 0)` | **AMBIGUOUS** | The comment says "Classic Mode also covers menus, rendering them at the VI's own line count". Is rendering menus at `viy` a property of the AA bit or of the preset? **Decide and justify.** My read: it belongs with the BIT, because the VI resolve is what produces the line count — but a player with the bit on and the preset off may not expect menu resolution to change. If in doubt, keep the current *observable* behaviour (preset on ⇒ true) rather than inventing a new one. |
| `videoClassicViOn` | `s_CfgN64Aa && portClassicPart(CLASSIC_PART_N64AA)` | **BIT × part** | Becomes `aa_bit && classic_n64aa_part` — but then the part gates a bit that is no longer owned by the preset, which may read as nonsense. Consider whether this helper survives at all; it currently feeds `floor_level` in `videoApplyAa`. |
| `portVideoN64AaOn` | `return s_CfgN64Aa;` | **PRESET** | The name lies. Rename to `portClassicModeOn` (or equivalent) and return the new preset static. Callers: the `-n64_aa` boot-flag log line and `portEnvTickClash`. |
| `portVideoOptionSet/Get` arms | `SDL_strcasecmp(key, "n64_aa")` | **both** | Two keys now: `n64_aa` (bit) and `classic_mode` (preset). Both need arms. This is where the preset's arm matters most — the choke point watches this key. |
| `videoMipmapMode` | `s_CfgN64Aa && portClassicPart(CLASSIC_PART_MIPMAPS)` | **PRESET** | Swap to the preset static. |
| `videoWriteDefaultIni` | `fprintf(f, "n64_aa=%d\n", …)` | **both** | Write both keys on a fresh install. |
| `videoBootClassicWanted` | `s_BootFlags.n64_aa && s_CfgN64Aa == 0` | **PRESET** | The `== 0` is the idempotence guard: `-n64_aa` must not re-enter a mode already on. |
| `GE_N64AA` env block | `s_CfgN64Aa = (e[0] != '0');` | **both** | Decision needed: does `GE_N64AA` keep setting the bit only, or the preset too? **Recommendation: keep it setting the BIT only**, and document that, because that is what its name says and the preset now has its own key. But this is a **behaviour change** for anyone using `GE_N64AA=1` to force Classic Mode — say so in `CLI.md` if you take it. Consider a new `GE_CLASSICMODE` alongside, following the existing naming. |
| `portEnvMipmapClash(s_CfgN64Aa, …)` | preset | **PRESET** | Swap to the preset static. |
| `portClassicPillars` | `s_CfgN64Aa && portClassicPart(CLASSIC_PART_PILLARS)` | **PRESET** | Swap. |
| the setter's default | `SDL_atoi(value) != 0` | both | The new preset key needs the same arm. |

### 3.2 `port/src/port_options.c` — the table and the choke point

- **`sOpts[]`** — `TOGGLE("n64_aa", "[display]", "Classic Mode\n", …)` at
  `port_options.c:202`. The row label says "Classic Mode" and the key says `n64_aa`: that is
  the conflation in one line. **Add a `classic_mode` row labelled "Classic Mode";
  relabel the `n64_aa` row "N64 AA".** Two rows now, in PORTOPT_CAT_DISPLAY.
  A comment above the `n64_aa` row already claims "on the console this sets the
  AA alone" — that comment is **currently false** and becomes true; verify it
  before quoting it.
- **the choke point**, `optSet` at `port_options.c:388`: `strcmp(key, "n64_aa") == 0`.
  → `strcmp(key, "classic_mode") == 0`. **This one line is the whole Phase 2
  mechanism moving to the new key.** Get it right and every surface follows.
- **`portClassicMismatchParts` / `portClassicRepair`** — unchanged logic, but
  they read `portOptFind("n64_aa")` in `classicInputFill`:
  `in->mode_on = (aa >= 0) ? (portOptGet(aa) != 0) : 0;` → read `classic_mode`.
- **`sClassicKeys[]`** — the seven forced values. `n64_aa` is **not** in it, and
  must not be: the manifest is 7, `classic_saved` holds 7, `CLASSIC_MODE_MAX` is
  16. Adding an eighth is a different change with an ini-format consequence.
- The `n64_aa` row's comment ("Shown as Classic Mode in the menus") moves to the
  new row.

### 3.3 `port/src/port_watchpages.c`

- row definition: `{ PWROW_OPT, "n64_aa", "Classic Mode\n", 0, 0 }` (~line 154)
  → `classic_mode`, and consider whether the watch also needs an "N64 AA" row.
- `portWatchRowLocked` (~953) — `const s32 aa = portOptFind("n64_aa");` used to
  decide whether classic rows are dimmed → `classic_mode`.
- `portWatchPageEntered` (~976) — the reconcile-on-entry path, same change.
- the `sPreset[]` table (~2605) — `{ "n64_aa", 1, 0 }`: the "Nintendo 64" preset
  sets the AA bit. After the split this should set **both** (`classic_mode` and
  `n64_aa`) or just the preset — decide, and note that this table is the old
  removed-preset machinery, partly vestigial (its own comment says so).

### 3.4 `src/game/front.c` — the VIDEO sheet

12 references. All are `portOptFind("n64_aa")` / row key comparisons, and all
follow the preset semantics:

- the sheet row (`{ SHROW_OPT, "n64_aa", "Classic Mode\n" }`, ~4719)
- `portSheetClassicShownOn` (~5508), `portSheetClassicRevert` (~5576),
  `portSheetClassicSnapshot` (~5595): the pending-pick machinery
- the pick handler (`strcmp(portOptKey(opt), "n64_aa") == 0`, ~5680) which calls
  `portSheetClassicPreset` / `portSheetClassicRevert`
- ~5238, an "is the mode on" check

**This is the surface with no headless test.** The sheet pends values and lands
them on Apply Changes, and its correctness depends on the row key matching the
choke point's. If you change the choke point's key and miss one of these, the
sheet will silently stop forcing values and every test will still pass.

### 3.5 Tests that name the key

- `port/tests/port_options_fake.c` — the fake's key table has `"n64_aa"`. It
  needs `"classic_mode"` too, and the fake must route both through
  `portVideoOptionSet` the way `video.c` does.
- `port/tests/port_options_n64aa.c` — every case. Consider renaming the file
  (`port_options_classic.c`) and the ctest target; if you rename, update
  `CMakeLists.txt` (registration is explicit, no glob) and
  `scripts/port/test_the_tests.sh` (which hardcodes the path).
- `port/tests/classic_mode.c` — comments mention `n64_aa`; the rule itself is
  key-agnostic and needs no change.
- `port/tests/boot_flags.c` — the **flag** `-n64_aa`, not the ini key. See §5.
- `scripts/port/test_the_tests.sh` — the mutant that swaps the choke point's key
  (`strncmp(key, "classic_", 8)`) becomes **wrong** after the rename: with the
  key renamed to `classic_mode`, that mutation starts *matching*. Fix the mutant
  to something meaningful (e.g. match `n64_aa` instead of `classic_mode`).

### 3.6 Documentation

- `tools/midas_ini_help.txt` — the shipped key table. Add `classic_mode`, correct
  the `n64_aa` description. It is copied verbatim by `CMakeLists.txt`, so a
  stale line ships.
- `CLI.md` §3.2 — the `-n64_aa` boot flag and its precedence notes.
- `SETTINGS.md` §3 — the player-facing Classic Mode section.
- `docs/port/settings-and-save-audit.md` — the findings register; add the Phase 3
  entry, do not rewrite earlier conclusions.
- `plan.md` §5 — mark Phase 3 landed when it is.
- `docs/port/room-cache.md` — mentions `n64_aa` as "the preset is on" in passing.

---

## 4. Migration

Old files must keep working. The rules:

- **`n64_aa=1` in an old file meant both.** So: `classic_mode=1` **and**
  `n64_aa=1`. Forcing the preset on preserves what the player saw.
- **`n64_aa=0` → `classic_mode=0`, `n64_aa=0`.**
- **A new file carries both keys.** A file with `classic_mode` present needs no
  migration.
- **`classic_saved` is unchanged** — still 7 fields, still append-only. It
  currently holds "the values before the preset forced its own", and none of
  those seven is the AA bit.

Run it **read-side, once**, in `videoLoadConfig`, with a marker key so it cannot
re-run and undo a player's deliberate `classic_mode=0`:

```
classic_split=1
```

There is an existing pattern to copy: the one-time `analog_*` bind upgrade in
`videoLoadConfig` uses a marker (`moveUpgraded`) and writes it back through
`videoKbmBindsSave()`. Follow its shape — including the guard that it runs
**only when the marker is absent**.

**Write-side:** the default-ini writer emits both keys. `portOptSave` writes
whatever is in `sOpts`, so adding the rows handles the table path.

**Downgrade is not supported and does not need to be**, but do not make it
destructive: a newer file read by an older build sees `n64_aa` (the bit) and an
unknown `classic_mode` (logged as `unknown control`, ignored). Say this in the
commit message so nobody later thinks it is an oversight.

---

## 5. The boot flag: keep, rename, or both

`-n64_aa` means "start in Classic Mode". After the split that name points at the
wrong key. Options, in order of my preference:

1. **`-classic` as the new spelling, `-n64_aa` kept as a silent alias.** Zero
   breakage, honest going forward. `portBootFlagIs` already treats `-`/`_` and
   case as equivalent; an alias is a second call, not a second parser.
2. **Keep `-n64_aa` only.** Cheapest, and the flag is documented as "Classic
   Mode" everywhere, so the docs stay coherent — but the name keeps lying.
3. **Rename outright.** Breaks every script and every message in `CLI.md`,
   `boot_flag_test.sh` and `SETTINGS.md`.

If you take 1, `port/tests/boot_flags.c` needs a case for both spellings, and
`CLI.md` §3.2 needs to say they are the same flag. If you take 2, the only work
is making sure nothing in the flag's path reads the ini key.

**Whatever you choose, `-n64_aa` must still turn the preset on** — that is its
documented and tested behaviour, and `videoBootClassicWanted`'s `== 0` guard is
what makes it idempotent.

---

## 6. Commit sequence

One subject per commit, in this order. Phases 0–2 kept each commit independently
revertible; hold that property.

1. **`video: classic_mode as its own key, and n64_aa as the AA bit`** — the two
   statics, the get/set arms, the default-ini writer, and the migration with its
   marker. Everything else still reads the old static; the build must be green.
2. **`options: move the preset to classic_mode`** — the `sOpts` row, the choke
   point's key, `classicInputFill`, and the fake + seam test updated together.
   This is the commit where a mistake is silent, so its test changes land with it.
3. **`front, watch: the sheet and the watch follow classic_mode`** — the 12
   `front.c` sites and the 4 code sites in `watchpages.c`.
4. **`video: the render-side consumers read the AA bit`** — `videoApplyAa`,
   `videoRenderLines`, `videoClassicViOn`, with the judgement calls from §3.1
   written into the code comments.
5. **`docs: classic_mode and n64_aa, and the migration`** — the ini help, CLI.md,
   SETTINGS.md, the audit, plan.md.
6. **`tests: ...`** — only if something in 1–5 could not carry its test with it;
   say why in the message.

**Exception, as at Phase 2:** a commit whose change *inverts a test assertion*
lands code and test together. Do not leave a red suite or a deleted assertion
between two commits.

---

## 7. Tests you must add

The existing three tests are the model: a **rule** test (pure functions), a
**seam** test (the shipped file against a fake), and a **mutation audit** that
proves both can fail. Phase 3 needs:

1. **Migration, as a pure function.** Extract the old-key→new-key decision into
   something testable (`port/include/` + `port/src/`, no SDL, no globals — the
   `video_env.c` shape) and test: `n64_aa=1` alone → both on; `n64_aa=0` alone →
   both off; `classic_mode` present → untouched; marker present → untouched.
   Register in `CMakeLists.txt` inside `if(GE_PORT_TESTS)`.
2. **The seam, extended.** `port_options_n64aa.c` must assert that changing
   **`classic_mode`** runs the transition and that changing **`n64_aa`** does
   **not**. That pair is the whole point of the split, and it is the assertion
   that would have been impossible before.
3. **Mutants.** Add to `scripts/port/test_the_tests.sh`: choke point on the wrong
   key (now the *other* key), migration that overwrites an explicit
   `classic_mode`, migration that runs when the marker is present, preset static
   left reading the AA static. Fix the now-inverted `classic_` mutant (§3.5).

The audit's own bar applies: **a test that cannot fail is not a test.** Run
`scripts/port/test_the_tests.sh` and read it, do not just check the exit code.

---

## 8. Verification

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cp build_macos/midas.ini /tmp/ini.before
cmake --build build_macos -j8
./scripts/port/port_test.sh --no-game        # expect 13 (or more) passed
./scripts/port/test_the_tests.sh             # every mutant killed
diff /tmp/ini.before build_macos/midas.ini   # must be empty
md5 -q build_macos/Data/Saves/eeprom.bin     # 3590c7df0edb0930f4e4248826e8ab64
```

**The migration is the one thing that must be tested live**, because it touches
the player's real file:

```bash
# craft an OLD file: n64_aa=1, no classic_mode, no marker
cp build_macos/midas.ini /tmp/ini.keep
# ... edit it to the old shape ...
cd build_macos && ./ProjectMidas            # NOT GE_NOINISAVE: the write IS the test
grep -E '^(n64_aa|classic_mode|classic_split)=' midas.ini
#   expect n64_aa=1  classic_mode=1  classic_split=1
cp /tmp/ini.keep midas.ini                  # restore, verify md5
```

Then the same file **again**, with `classic_mode` hand-set to 0 and the marker
present: it must **not** be migrated back to 1. That is the whole reason the
marker exists.

**Live matrix for the surfaces no test can reach** (all with `GE_NOINISAVE=1`,
and remember that your `midas.ini` has `n64_aa=1` today, which makes some probes
silently no-ops — check the log line, not just the end state):

```bash
GE_NOINISAVE=1 ./ProjectMidas -n64_aa      # preset on, all seven forced
GE_NOINISAVE=1 ./ProjectMidas              # if the file has classic_mode=1, consistent
GE_NOINISAVE=1 GE_CONCMD="cl_classic" ./ProjectMidas
GE_NOINISAVE=1 GE_CONCMD="cl_classic fix" ./ProjectMidas
GE_NOINISAVE=1 GE_CONCMD="cl_n64_aa 1" ./ProjectMidas      # the BIT only: no forcing
GE_NOINISAVE=1 GE_CONCMD="cl_classic_mode 1" ./ProjectMidas # the preset: forcing
```

**That last pair is the acceptance test for the entire phase.** They must
differ: `cl_n64_aa 1` turns on the AA bit and changes nothing else;
`cl_classic_mode 1` forces the seven.

Live runs must report their renderer (`grep 'GL: renderer'`); a sandboxed shell
gives `Apple Software Renderer` and a window that may not composite
(`CLI.md` §7.2).

---

## 9. Hazards, in the order they will bite

1. **`build_macos/midas.ini` is the user's real settings file.** Back it up
   before every experiment; restore and md5-check after. `eeprom.bin` must stay
   at `3590c7df0edb0930f4e4248826e8ab64`.
2. **`src/game/front.c` has no test.** 12 sites, all silent on failure. The
   sheet is the most likely place to leave the preset broken while the suite is
   green.
3. **The migration writes the player's file.** Test the marker both ways (runs
   once; does not run twice).
4. **`docs/` is gitignored** (`.gitignore:281`, `/docs/*`). A **new** file under
   `docs/port/` needs `git add -f`, and `git add docs/port/x.md` fails outright
   with "paths are ignored". Verify with `git ls-files docs/port/`, never with
   the `git add` warning.
5. **The choke point's key is one string comparison.** Miss it and nothing
   happens anywhere; get it and everything follows. It is the highest-leverage
   and most silent line in the change.
6. **`classic_saved` is 7 fields and must stay 7.** Do not add the AA bit to it.
7. **`GE_N64AA`'s meaning may change** (§3.1). If it does, that is a
   user-visible behaviour change and needs a doc line, not just a commit.
8. **Repo conventions:** subjects are `area: lowercase clause`; the citation
   policy is symbol-or-section, line numbers only where the line is the point.

---

## 10. How this branch's last sessions went wrong, so you can avoid it

Recorded because it is the most useful thing in this file.

- **Two false findings came from the user's own `midas.ini` having `n64_aa=1`.**
  With the preset already on, `-n64_aa` is a no-op, so a probe appears to show
  the *env var* winning a precedence contest it never entered. **Always set the
  preset off explicitly and check the log line before believing a precedence
  result.** With Phase 3 this becomes easier (two keys) — do not waste it.
- **A test was written that passed for the wrong reason.** The first version of
  `classic_mode`'s "remove the memset" mutant appeared killed, but only because
  uninitialized stack happened to fail case one; the assertion that *claims* to
  test plan reuse did not. Mutation-test the tests and read *which* assertion
  fired.
- **A harness claim was overstated twice** — that `boot_flag_test.sh` runs
  windowed unconditionally (it does not), and that the demo's `-v` restored the
  ini with no failure path (it did not). Both were caught by reading the script
  before committing. Keep doing that.
- **The user's rule, which is a good one:** one subject per commit; a fix, a
  test, a doc, never mixed — except where splitting inverts an assertion, in
  which case code and test land together and the message says why.

---

## 11. Definition of done

- `classic_mode` and `n64_aa` are separate keys with separate meanings, and the
  acceptance pair in §8 behaves differently.
- An old `midas.ini` migrates once, and a migrated one is never touched again.
- Every one of the 14 `video.c` sites has a written decision about which concept
  it reads — in the code, not only in the commit message.
- `ctest` green (13+, with the new migration test), mutation audit green with the
  new mutants, `midas.ini` and `eeprom.bin` byte-identical to the baseline.
- `tools/midas_ini_help.txt`, `CLI.md`, `SETTINGS.md`, the audit and `plan.md`
  all describe the two keys.
- Anything you could not verify is named as unverified in the commit message.
  Phases 0–2 did that consistently; it is why their messages are still useful.
