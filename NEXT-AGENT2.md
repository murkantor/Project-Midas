# Handoff: audit the audio-pump change, independently

Paste this whole file into a fresh session as the task. It is self-contained.

You are auditing a seven-commit branch. **You are the third pass.** Two prior
passes — one implementation session and one documentation review — have already
corrected this branch twice, and both passes introduced errors while fixing
errors. One of them "fixed" a false statement in a shipped manual by replacing
it with the opposite false statement. Another shipped a document, inside the very
commit that made it false, whose central claim was that its subject had not
shipped yet.

That is why this task exists, and it is why the instructions below are shaped the
way they are: **do not trust anything the branch says about itself, including its
own claims to have been fixed.** Re-derive every claim from the code and from
execution. Where a comment says "corrected", treat that as a lead to investigate,
not as evidence of correctness.

You are report-only. Do not edit, stage, commit or delete anything outside the
scratch areas named in §5.

---

## 1. The repository

GoldenEye 007 N64 decompilation with a PC port layer, at
`/Users/jcorneli/Developer/DeepSeek/PM-fork`, branch `audio-pump-wallclock_gen4`.

**Build and test:**

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork
cmake --build build_macos -j8
cd build_macos && ctest --output-on-failure
```

The baseline is **9/9 passing**. `build_macos/` is gitignored. One linker warning
(`reducing alignment of section __DATA,__common`) appears on every commit
including the untouched base — it is pre-existing, not a regression.

**`docs/` is gitignored** (`.gitignore` line 281, `/docs/*`) but three files under
it are tracked via force-add. Consequences: `git status` will not show doc edits,
and `git diff` needs `git show <sha>:<path>` to see them per commit.

**The change under audit:**

```
2f8a6d0  console: drop TPS from the pump log
bbc5c99  docs: correct the shipped manuals' frame and tick claims
9873f43  docs: the design record for the frame and tick numbers
6d02aa1  hitchlog: audit the hw field hold against the fields credited
85fa9f1  console: the block shows ticks, fields/s and fields/tick
97659f6  frametiming: publish fields credited per second, and stop the negative TPS
ca41f25  stats: the frame and tick rate maths, as tested pure functions
```

Base is `origin/audio-pump-wallclock_gen4` = `3a1ab62`. Seven commits, unpushed.

**What the branch is about.** The `cl_audio_pump` overlay and
`Data/Logs/audio_pump.log` publish frame/tick rates. The branch replaces a `TPS`
field that was `min(tick rate, 60)` — and therefore misleading — with four
numbers (`fps`, `tk`, `f/s`, `f/t`), and rewrites the two shipped manuals, a
design record, and various comments to match.

---

## 2. Ground rules

1. **Report-only.** No edits to tracked files, no commits, no stashing, no
   rebasing. §5 permits writes in two scratch locations only.
2. **Blind first.** Do your own complete pass before consulting anything
   recorded by a previous session. If you find a previous session's notes or a
   handoff document, do not read it until your own findings are written down.
3. **Execution beats reading.** The two prior passes were read-only. Every claim
   of the form "measured", "observed", "live" in the branch is therefore
   unverified until you run it. §5 tells you how.
4. **A claim is false until the code agrees.** "The docs say X" is never evidence
   that X is true; the docs are the thing under test.
5. **Do not report things that are right.** Findings only.

---

## 3. Four audit directions

Run all four. They catch different classes of defect; the prior passes
essentially only ran the first.

### 3.1 Documentation → code

For each document, extract every checkable claim and falsify it against the code
at HEAD.

- `tools/console_help.txt` — the shipped manual for the console and the pump
  diagnostics. Highest priority: users read this.
- `tools/midas_ini_help.txt` — the shipped settings manual.
- `docs/port/tick-and-field-accounting.md` — the design record.
- `docs/port/tps-the-lie.md` — a record of the removed field.

Concentrate on: arithmetic and unit conversions; which counter backs which
displayed number; whether a value described as *displayed* is in fact only
computed; stated conditions for a value being zero; configuration semantics
(what `maxfps` values and `tickhz` modes actually derive to, including defaults);
and file:line citations — resolve every one and read the target line. A citation
landing on a brace, a blank line or an unrelated comment is a miss.

### 3.2 Code → documentation (omissions)

This direction finds what the first cannot: behaviour the documents do not
mention, or mention wrongly by omission.

Walk the pump and the stat pass yourself —
`port/src/audio_pc.c`, `port/src/bondview2.c` (`portDrawAudioPumpStats`),
`port/fast3d/gfx_sdl2.cpp` (the stat pass), `src/game/lv.c`,
`src/game/frametiming.c`, `port/src/hitchlog.c` — and for every published number
ask:

- What exactly does this count, and at what point in the frame?
- Is that the same thing the documentation says it counts?
- What states can make it read a surprising value that no document warns about?
- Is there a mode (Hardware pacing, uncapped, raised tick rate, paused,
  render-thread) in which the documented story is incomplete?

### 3.3 Commit message → diff → tree

All three must agree. This branch has produced commits whose message described
work their tree did the opposite of, and a document added by a commit that made
its central claim false.

For each of the seven commits:

- Does the diff match the message's stated scope — nothing shipped that the
  message does not mention, nothing described that is not in the diff?
- Are factual statements in the message true of the tree *at that commit*, not
  merely of the final tree? Read `git show <sha>:<path>`.
- Does the message describe a defect as fixed by an approach the diff does not
  use?
- Does the message reference a commit, symbol or state that does not exist at
  HEAD or is unreachable from it?

### 3.4 Execution

Run the game and read what it publishes. This is the part no prior pass did, and
it is the only way to settle the empirical claims. §5 gives the method and §6
the states to exercise.

---

## 4. What this branch has been wrong about before

Not a list of current defects — a list of the *shapes* to hunt. Each has already
happened at least once here.

- **A correction that inverts the error.** A false sentence replaced by its
  opposite, equally false. Happens when someone fixes a label from reading alone.
- **A number described as displayed that nothing displays.** Several globals are
  computed every window and read by nothing.
- **The wrong counter named for a number.** Two tick counters exist with subtly
  different semantics; several places name the one that is wrong here.
- **Zero conflated with "nothing happened".** A ratio can be zero because the
  numerator is zero while the denominator is healthy, which is a different
  statement from "nothing ran".
- **A doc describing its own uncommitted state.** Claims about what "the working
  tree" or "the tip of the branch" contains, written before or inside the commit
  that changes it.
- **A caveat that invents a limitation.** A comment asserting two measurements
  are incomparable when the code makes them comparable.
- **Pre-existing text left behind by a change that obsoleted it.** Grep for the
  removed concept; judge each surviving mention as legitimate history or a
  leftover lie.
- **Untracked material that never got updated.** The PR text and message drafts
  are what a reviewer will actually read.

---

## 5. Execution protocol

The game runs from `build_macos/` and reads `midas.ini` beside the executable.
That `midas.ini` is the user's real settings. **Protect it.**

**Before any run:**

```bash
cd /Users/jcorneli/Developer/DeepSeek/PM-fork/build_macos
cp midas.ini midas.ini.audit-backup
```

**Run** (headless-ish, bounded, with logging on):

```bash
rm -f Data/Logs/audio_pump.log
GE_FRAMES=400 GE_CONCMD="cl_audio_log 1" ./ProjectMidas > /tmp/audit-run.log 2>&1
grep '^t=' Data/Logs/audio_pump.log | head -6
```

`GE_FRAMES=n` exits after n frames. `GE_CONCMD` runs console lines once, `;`
separated. Useful knobs: `GE_TICKHZ` (wins over the ini), `GE_STAGE`,
`GE_HITCHMS`/`cl_hitchms`, and `cl_maxfps` / `cl_vsync` / `cl_tickhz` as console
settings.

**After every run:**

```bash
diff midas.ini midas.ini.audit-backup || echo "INI CHANGED - restore it"
cp midas.ini.audit-backup midas.ini
rm -f midas.ini.audit-backup
```

**Hard limits:**

- Do not touch `Data/Saves/`, `Data/Profiles/`, `Data/Weaponsets/` or
  `Data/MatchSettings/`. Running the game must not overwrite a save.
- Writes are permitted only to `Data/Logs/`, `/tmp/`, and the `midas.ini`
  backup/restore cycle above.
- Note that a console `cl_maxfps` / `cl_tickhz` **writes to `midas.ini`**, and
  setting `maxfps` may also rewrite `tickhz` through the coupling. This is the
  single most likely way to damage the user's settings — hence the backup.
- If a run produces no log lines, say so rather than inventing a reading. The
  log needs a few hundred frames; the first line lands at about `t=1000ms`.

---

## 6. States to exercise

For each state, first derive from the documents what they *predict* every
published field should read, write the prediction down, then run it and compare.
A mismatch is a finding — either the code or the document is wrong, and your job
is to say which and why.

1. **Default / Hardware pacing.** The user's ini is `maxfps=-1`, `vsync=0`,
   `tickhz=0`. Record what the fields read.
2. **A frame cap below 60.** `cl_maxfps 30`. The manual makes an explicit,
   checkable claim about how many ticks happen per second here and what `f/t`
   does. Test exactly that claim, including the fields-per-tick figure.
3. **A raised tick rate.** `GE_TICKHZ=120`. Both the manual and the design record
   describe what happens. Check which of them survives contact with a 60 Hz
   display, and whether the design record's mode description is qualified enough
   to be true.
4. **Paused or controls-locked.** If you can reach a paused state within a
   bounded run, record what `tk`, `f/s` and `f/t` read. If you cannot reach it,
   say so and treat the documented behaviour as unverified — do not guess.
5. **A stage load.** If reachable within a bounded run, capture the window during
   the load. The documents make claims about `f/s` and `f/t` during loads.
6. **The log itself.** Verify the legend in the emitted file matches the line it
   introduces, field by field and unit by unit. Verify no field the documents say
   was removed is still printed.

Also verify, mechanically rather than by eye:

- the `fprintf`/`sprintf` format strings and their argument lists agree in count
  and type (a drift here compiles silently);
- `f/s` × `f/t` reconciles with the tick rate in a real line;
- the second figure of every dual-unit field is the first converted at the right
  rate.

---

## 7. Staffing

Do your own complete pass first, unaided. Only then may you spawn help — and
only for breadth, never to replace your own judgement.

Suggested split if you use subagents: one per shipped manual, one for
§3.3 (message vs tree), one for §3.2 (code → docs omissions). Give each the
blind rule and the evidence standard from §8.

**Every subagent finding must be re-verified by you before it appears in your
report.** In this project a child's self-report has already arrived truncated
mid-finding, and has already contained a claim about a commit's contents that the
parent had to check against the tree. Treat child output as a lead, not a result.
If you cannot verify a child's finding yourself, report it as unverified and say
who claimed it.

Scope each subagent tightly; an unbounded one has previously consumed ~10M tokens
on this repository.

---

## 8. Evidence standard

Every finding needs all four:

- **Where claimed**: file and line, with the claim quoted exactly.
- **Where contradicted**: file and line, with the code quoted exactly.
- **Severity**: `critical` = a user is actively misled about behaviour they will
  act on. `major` = a developer would implement against the wrong model. `minor` =
  imprecision that changes no decision. `nit` = wording.
- **What is actually true**: one sentence.

Additional rules:

- A finding with only one half is not a finding. If you cannot produce both, list
  it under unverified instead.
- Distinguish what you executed from what you inferred. Say which.
- If the code is wrong but the document faithfully describes that wrongness, that
  is not a documentation defect — report it in its own section, because it is
  still worth knowing.
- Do not propose patches. Say what is wrong and what is true.

---

## 9. Output

1. **Findings**, ordered by severity, each in the §8 format.
2. **Unverified**, with the reason for each — including anything requiring
   hardware you do not have, a state you could not reach, or an artifact that
   does not exist.
3. **Code that looks wrong but is faithfully documented.**
4. **The execution record**: which states you ran, the exact commands, and the
   log lines you based conclusions on. Include the predictions you wrote down
   before running, and whether each held.
5. **A one-paragraph verdict**: is this branch safe to push as-is, and if not,
   what must change first.

Be blunt. A clean report that misses a defect is worse here than a harsh one that
finds it, because the whole point of a third pass is that the first two were
confident and wrong.
