# room_cache — the static room geometry cache

A reference for what `[display] room_cache` switches on, who owns it, and what
went wrong with it on 2026-10-25. Written to be read on its own; the symbols it
names are the authority, not this file.

---

## What it is

The N64 renderer is an interpreter: every frame the game builds a display list,
and the interpreter walks it, decodes each command and turns it into triangles.
Most of those triangles are background rooms, and a room's display lists are
immutable between load and unload.

`room_cache` records what a room's list produced the first time it ran, and
answers the same list from the recording afterwards. It exists because the Xbox
port was CPU-bound in `gfx_sp_tri1` / `gfx_run_dl` — most of a heavy frame was
the interpreter re-deriving triangles for geometry that had not changed.

Two files hold it:

- **`port/src/roc_hooks.c`** — the sim-thread side. Gates the emitters and
  queues invalidation messages.
- **`port/fast3d/gfx_roomcache.inl`** — the render-thread side, included by
  `gfx_pc.cpp`. This is the cache itself.

The split is not stylistic. With `render_thread` on, frame N's display list runs
on the render thread while the sim builds frame N+1, and only the thread running
the DL may touch the cache tables. So the game's calls become 8-byte commands
written into the DL (`G_PORTCACHE_EXT`) or queued operations that the render
thread applies at its next frame start.

## The three modes

- **0 — off.** Every frame goes through the interpreter. The emitters write
  nothing, so the DL carries no markers at all.
- **1 — CPU replay.** The first run of a registered room OPA list is rendered
  normally *and* recorded: the per-vertex float stream (uv, combiner inputs),
  each emitted vertex's model-space position, the draw state, and the per-draw
  constants. Later frames replay it — apply the cached state, transform the
  cached model-space positions by the **current** matrix, re-derive aspect,
  clip codes, fog and z remap exactly as `gfx_sp_vertex` / `gfx_sp_tri1` would,
  and hand `draw_triangles` the result.
- **2 — GPU retained.** At capture the room becomes an object-space vertex
  buffer in the backend (`retained_*`). Per frame the CPU applies each draw's
  state, hands the vertex program the room's MP matrix and fog constants, and
  issues one `DRAW_ARRAYS` per draw. No per-triangle work. Rooms the builder
  refuses — decal-pull draws, y-inverted captures, backend out of buffers —
  fall back to the interpreter.

Mode 1 costs what the interpreter would have. The 2026-09-11 A/B on Cradle
measured 59–62 µs per draw either way, which is why the comment in
`gfx_roomcache.inl` calls it "the skeleton of a GPU-side room path" rather than
a win. **Mode 2 is the one that removes the work.** Mode 1 is worth knowing
about only because it is a distinct code path that can be selected.

Mode 2 additionally caches **model parts** (characters, props). Mode 1 does not:
`portPartCacheEmit` returns early unless the mode is exactly 2. Two escape
hatches exist, read once at first use:

- `GE_ROCGUN=1` — re-enable gun-DL parts (segment 5). They are off by default
  because retained copies replayed stale geometry around pause, watch and
  weapon switches.
- `GE_ROCMODEL=0` — disable segment 4 (characters, props) parts.

## What is and is not cached

Cached: room **opaque** lists, and — in mode 2 only — model part lists.

Never cached:

- **XLU (translucent) room lists.** They must reach the deferred sorter.
- **Props, characters, weapons and effects**, except the mode-2 model parts
  above.
- **Any room whose list loads a matrix, draws a rect or sky triangle, or takes
  the deferred-XLU path during capture.** It is marked uncacheable and stays on
  the interpreter forever.
- **A room whose cached texture ids no longer match the texture cache**
  (eviction) falls back to the interpreter for that frame and is recaptured on
  the next.

Invalidation paths, all in `roc_hooks.c`:

- `portRoomCacheRoomFreed` — a room's list or vertex colours changed
  (`bg.c` `delete_room_data`, a light shot out in `lightfixture.c`).
- `portPartCacheVerticesFreed` — a vertex buffer was freed, replaced or edited
  (`vtxstore.c`, per-instance edits in `chr.c`).
- `portPartCacheStageLoaded` — a stage load voids the part table, because `memp`
  reuses the addresses the keys are made of. Stage `0x5A` is the front end and
  is excluded.
- `portRoomCacheSetMode` — the mode changed; the render side clears and
  switches.
- A changed list pointer at registration; the texture cache clear at a stage
  boundary.

## Who owns it: hardware pacing

**Hardware pacing owns `room_cache`.** The fill model under `tickhz=hw`
(`maxfps=-1`) prices a frame from the interpreter's triangle counts. A cached
room produces no interpreter triangles, so the model would be pricing an empty
frame. The invariant is therefore: *room_cache is 0 whenever hardware pacing is
active.*

It is enforced in three places:

- **The ini / console / menu setter** (`videoSetDisplayOption`). On
  `tickhz=hw` it forces 0, saves the previous value in `s_RoomCacheBeforeHw`, and
  logs `tickhz=hw - room_cache forced 0 (the fill model reads the interpreter's
  triangles)`.
- **Leaving hw pacing** (`videoRoomCacheAfterHw`). Restores the saved value and
  logs `hw pacing off - room_cache N back on`. An explicit `room_cache=0` is
  never put back — setting 0 on purpose clears `s_RoomCacheBeforeHw`.
- **The env override** (`GE_ROOMCACHE`), since 2026-10-25 — see below.

**The forced 0 is never written to the file.** It exists in memory for the
session. Executed: with `room_cache=2 tickhz=hw` in `midas.ini`, a run logs
`room_cache=0` and the file still says `room_cache=2` afterwards. That answers
the obvious worry about a crash during hw pacing leaving the cache off forever —
it cannot, because it was never saved off.

## How to set it

- `midas.ini`, `[display] room_cache=0|1|2`
- `GE_ROOMCACHE=0|1|2` — env override, for A/B runs
- `cl_room_cache N` — the in-game console; writes the key like any other
  `cl_` setting
- **Not the Port Options menu.** There is no `sOpts` row for `room_cache`
  (checked: `grep -c room_cache port/src/port_options.c` is 0), so it cannot be
  changed from the menu, and Port Options' full-table save never writes it.
  A file only gets this key from the fresh-install default write or by hand.

## Defaults — and a documentation defect

Verified in `video.c`:

- **PC build: 0.** `#else static int s_CfgRoomCache = 0;` — "off by default on
  PC".
- **Xbox build: 2.** The `#ifdef GE_XBOX` arm.
- `GE_XBOX` is **not** defined in this build (`grep -c GE_XBOX
  build_macos/build.ninja` is 0).

**`tools/midas_ini_help.txt` claims the default is 2** ("room_cache | 2 | 2 =
keep room geometry on the GPU"). That is the Xbox default, not the PC one, and
the help file is hand-written and copied verbatim by `CMakeLists.txt`, so
nothing catches the drift. Recorded here rather than fixed: it is a one-line
correction to a shipped file and belongs in its own change.

**A second, smaller inconsistency, read not executed:** the header comment in
`gfx_roomcache.inl` says the cache is "default OFF everywhere since the
2026-09-11 A/B", while `video.c` gives the Xbox build 2. One of the two is out
of date. Not resolved here.

## The defect fixed on 2026-10-25

`GE_ROOMCACHE` was read at the end of `videoInit`, **after**
`videoApplyBootArgs`. The sequence was:

1. `-n64_aa` turns Classic Mode on.
2. Classic Mode sets `maxfps=-1`.
3. Hardware pacing arms and forces `room_cache` to 0.
4. `GE_ROOMCACHE=2` reads last and sets it back to 2.

(`-n64_aa` is spelled `-classic` now and the old name is kept as an alias, so
every command in this section still works as written. It sets the Classic Mode
preset — `[display] classic_mode` — and no longer the AA bit it is named after;
see `plan.md` §5.)

The run then had hardware pacing's fill model pricing frames against a cache
that had just been switched on, and nothing said so. Executed before the fix:
`GE_NOINISAVE=1 GE_N64AA=0 GE_ROOMCACHE=2 ./ProjectMidas -n64_aa` logged
`room_cache=2 tri_hoist=1` beside `config: maxfps=-1`.

Two changes, one commit (`video: apply GE_ROOMCACHE before the boot flags, and
never under hw pacing`):

- **The reorder.** `GE_ROOMCACHE` / `GE_TRIHOIST` moved up with the other env
  overrides, so the documented policy — env first, command line beats
  environment — is what the code does. `-n64_aa` now wins over a variable it
  never knew about.
- **The guard.** A nonzero `room_cache` is refused while `s_CfgTickHz == -1`,
  and the refusal names the line: `GE_ROOMCACHE=2 ignored - tickhz=hw forces
  room_cache 0 (the fill model reads the interpreter's triangles)`.

**The reorder alone was not enough, and only testing showed it.** A `tickhz=hw`
line in `midas.ini` arms hardware pacing *during the config load* — long before
any env var is read — so the env override landed after the forced 0 on that
path too. The guard is what actually holds the invariant, on both paths.

The console path (`cl_room_cache 2`) was already safe, because
`portVideoOptionSet` routes through `videoSetDisplayOption`, which carries the
forcing. Executed under hw pacing: `cl_room_cache 2` logs `tickhz=hw -
room_cache forced 0` and the knob has no effect. **The env path was the only
unguarded entry point** — which is the general lesson: an env override that
writes a `s_Cfg*` static directly bypasses every rule the setter enforces.

## Checking it

Off hardware pacing, the knob must bite:

```bash
GE_NOINISAVE=1 GE_ROOMCACHE=2 ./ProjectMidas        # tickhz=60, maxfps=60
#   ge: video: room_cache=2 tri_hoist=1
```

Under hardware pacing, it must be refused out loud:

```bash
GE_NOINISAVE=1 GE_ROOMCACHE=2 ./ProjectMidas        # tickhz=hw in the file
#   ge: video: GE_ROOMCACHE=2 ignored - tickhz=hw forces room_cache 0 ...
#   ge: video: room_cache=0 tri_hoist=1
```

And the restore must work — change `maxfps` from a real number to hardware
pacing and back inside one session:

```bash
GE_NOINISAVE=1 GE_CONCMD="cl_maxfps 30" ./ProjectMidas
#   ge: video: hw pacing off - room_cache 2 back on
```

`GE_TRIHOIST` is a separate knob that happens to live next to this one. It was
moved in the same commit and is unaffected by the ownership rule; verify with
`GE_NOINISAVE=1 GE_TRIHOIST=0` and the `room_cache=N tri_hoist=0` line.
