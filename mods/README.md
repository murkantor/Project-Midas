# Native C mods

A mod is a shared library the game loads at boot from `Data/Mods/<name>/`:

```
Data/Mods/bhop/
    bhop.so        (bhop.dll on Windows)
    mod.ini        order=10, enabled=1
```

The API is `port/include/midas_mod.h`. A mod exports two symbols:

```c
#include "midas_mod.h"

MIDAS_MOD_ABI_EXPORT

static const MidasHost *H;
static const s32 *s_on;

static void tick(f32 dx, f32 dz)
{
    if (*s_on) { /* g_CurrentPlayer, any game function via extern */ }
}

MIDAS_EXPORT s32 midas_mod_init(const MidasHost *host)
{
    H = host;
    s_on = H->option_bool("my_mod", "My Mod", 1);
    H->hook(MIDAS_HOOK_PLAYER_MOVED, (void *) tick, 0);
    return 1;
}
```

## What the host gives a mod

| Service | Notes |
|---|---|
| `hook(id, fn, prio)` | see `MidasHookId` for each signature; lower priority runs first |
| `option_bool` / `option_int` | Port Options sheet, the watch, the console, `midas.ini [input]`; returns a host-owned pointer |
| `action`, `action_pressed`, `action_held` | a rebindable keyboard/pad action (8 slots) |
| `command` | a console command |
| `string` | a text id for item names and the like (lang bank 45) |
| `claim_item`, `item_model_dir` | a free `ITEM_*` slot and its texture folder |
| `symbol(mod, name)` | another mod's `MIDAS_EXPORT` function |
| `alloc_high` | memory that is safe to point display lists at |
| `log` | `ge: <mod>: ...` in the log and console |

Everything else is plain C: the executable exports every global, so `extern`
any game function or variable. That half is tied to one game build.
`MIDAS_ABI` is bumped when a struct mods touch changes, and the loader refuses
a mismatch.

## Building

In this tree, add a folder with `mod.ini` and the sources, and one line to
`mods/CMakeLists.txt`:

```cmake
midas_add_mod(mymod mymod/mymod.c)
```

It builds with the game's own flags (32-bit, the same defines and includes, so
struct layouts match) into `build_pc/Data/Mods/mymod/`. The Windows
cross-build makes the `.dll` the same way. Mod DLLs are rebased above
`0x10000000`, and the loader refuses one below it (see `port_hostptr.h`).

Changing a mod means rebuilding the mod only, not the game. Mods load once
at boot and are never unloaded.

## Choosing which mods load

By default every mod whose `mod.ini` has `enabled=1` loads. To pick:

```ini
; midas.ini - kept between runs
[mods]
load=bhop,funguns
```

```sh
GE_MODS=bhop ./run.sh          # this run only; overrides midas.ini
GE_MODS=all ./run.sh           # every enabled mod, whatever midas.ini says
GE_MODS=none ./run.sh          # no mods (same as GE_NOMODS=1)
```

| Value | Loads |
|---|---|
| names, comma or space separated | exactly those, even if `enabled=0`, in `mod.ini` `order` |
| `all` or `*` | every mod with `enabled=1` |
| `none` or empty | nothing |

Names are folder names under `Data/Mods/`, case-insensitive. A listed name
with no folder is logged (`ge: mods: <name>: no Data/Mods/<name>/mod.ini`).
A mod that another one uses (funguns uses bhop's velocity) is optional:
without it the other carries on without that part. Mods load at start,
so a change takes effect on the next launch.

`GE_MODDBG=1` logs every registration. The `mods` console command shows the
selection, where it came from, and what loaded.

## Bundled mods

- `bhop`: jump (Space), ledge jumping, bunnyhopping, speedometer. Exports
  `bhop_get_velocity` / `bhop_set_velocity`.
- `funguns`: the Portal Gun (`give portalgun`). Uses bhop's velocity when it
  is loaded.

## Lua later

Lua fits on top of this rather than beside it: a `mods/lua` C mod that
embeds Lua and is the only thing the engine needs to know about.

- In `midas_mod_init` it creates a `lua_State`, scans `Data/Mods/*/main.lua`,
  and registers one C trampoline per hook. Each trampoline calls the scripts'
  `on_tick`, `on_weapon_fire`... in load order.
- `option_*`, `action*`, `command`, `string` are exposed as Lua functions that
  forward to `MidasHost`, so script options land on the same menus.
- Game state goes out as a small curated table/userdata layer (player position,
  velocity, health, inventory, spawn/give), not raw structs. That layer is the
  stable, sandboxed API for small tweaks (`CONTRIBUTING.md`'s "Lua mods"),
  while C mods stay the full-access, rebuild-per-release tier.
- Script states can be torn down and reloaded freely (nothing in the engine
  points into Lua memory), which gives the hot reload C mods don't have.

The engine itself needs no Lua-specific changes, only new hook ids when a
script needs a point C mods don't hook yet.
