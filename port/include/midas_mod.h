/**
 * midas_mod.h - the native C mod API (port/src/port_mods.c is the host).
 *
 * A mod is a shared library in Data/Mods/<name>/ (<name>.so on Linux,
 * <name>.dll on Windows) next to a mod.ini, exporting
 *
 *     MIDAS_EXPORT s32 midas_mod_init(const MidasHost *host);
 *
 * which runs once at boot, before midas.ini is read, and returns 1 to stay
 * loaded. Everything a mod adds goes through `host`: hooks, options, binds,
 * console commands, strings, items. Beyond that a mod may call any non-static
 * game function or global with a plain `extern`; the executable exports them
 * all. Those raw symbols are the unstable tier: a mod using them is built for
 * one game build, which is what MIDAS_ABI guards.
 *
 * Bump MIDAS_ABI whenever MidasHost, a hook signature, or a struct a mod is
 * likely to touch (struct player, PropRecord, GunModelFileRecord...) changes.
 * The host refuses a mod built against another value.
 *
 * Mods are built with the game's own compiler flags (CMake midas_add_mod), so
 * struct layouts match. Windows: memory a display list points at must sit
 * at or above 0x10000000 (port_hostptr.h); mod DLLs are based there, and
 * host->alloc_high is always safe.
 */
#ifndef MIDAS_MOD_H
#define MIDAS_MOD_H

#include <ultra64.h>
#include <bondtypes.h>

#define MIDAS_ABI 1

#ifdef _WIN32
#define MIDAS_EXPORT __declspec(dllexport)
#else
#define MIDAS_EXPORT __attribute__((visibility("default")))
#endif

/* Hook points. The comment is the callback's signature. Chained hooks run in
 * priority order (lower first); "handled" hooks stop at the first nonzero. */
typedef enum
{
    /* void (void): solo or MP level start, after the fist is added */
    MIDAS_HOOK_LEVEL_START,
    /* void (coord3d *move, f32 walkx, f32 walkz): Bond's move offset for this
     * tick before collision; walkx/z is the walk animation's world velocity
     * per tick already in it */
    MIDAS_HOOK_PLAYER_MOVE,
    /* void (f32 dx, f32 dz): what collision let him move this tick (sway
     * excluded); runs every on-foot tick after the move */
    MIDAS_HOOK_PLAYER_MOVED,
    /* void (StandTile *floor, f32 *height, f32 *always_30): the stan prop
     * test window for a move, measured from `floor`; widen it to step over */
    MIDAS_HOOK_PLAYER_STEP_WINDOW,
    /* s32 (coord3d *next_pos): a move was refused by a tile edge. Return 1
     * after placing Bond yourself (tile + collision position) */
    MIDAS_HOOK_PLAYER_BLOCKED,
    /* f32 (f32 floor): the floor height under Bond (stanHeight); chained */
    MIDAS_HOOK_PLAYER_FLOOR,
    /* void (s32 use_stanHeight): bondviewUpdatePlayerY, after stanHeight is
     * set and before the fall integrates field_70 / field_7C */
    MIDAS_HOOK_PLAYER_Y,
    /* s32 (s32 hand, s32 item): the fire trigger; 1 = handled, no vanilla shot */
    MIDAS_HOOK_WEAPON_FIRE,
    /* s32 (s32 pressed): the aim button; 1 = this mod owns it this tick (no
     * aim mode). `pressed` = went down this tick */
    MIDAS_HOOK_WEAPON_AIM,
    /* Gfx *(Gfx *gdl): world pass, after the bullet impacts; chained */
    MIDAS_HOOK_RENDER_WORLD,
    /* Gfx *(Gfx *gdl): HUD pass, after the FPS counter; chained */
    MIDAS_HOOK_RENDER_HUD,
    MIDAS_HOOK_COUNT
} MidasHookId;

typedef struct MidasHost
{
    u32 abi;    /* MIDAS_ABI the game was built with */
    u32 size;   /* sizeof(MidasHost): fields are only ever appended */

    void (*log)(const char *fmt, ...);   /* "ge: <mod>: " is prefixed */

    s32 (*hook)(MidasHookId id, void *fn, s32 priority);

    /* Options on the Port Options sheet, the watch and the console, stored in
     * midas.ini [input]. The pointer is host-owned and never moves; read it
     * whenever. Registering a key twice returns the same pointer. */
    const s32 *(*option_bool)(const char *key, const char *label, s32 def);
    const s32 *(*option_int)(const char *key, const char *label, s32 def, s32 lo, s32 hi, s32 step);

    /* A bindable action (keyboard and pad rebind lists, ini key `name`).
     * Returns a handle, -1 when the slots are full. */
    s32 (*action)(const char *name, const char *label, const char *defkeys);
    /* the current player's latched press (taken), 0 while controls are not live */
    s32 (*action_pressed)(s32 handle);
    /* the current player's held level */
    s32 (*action_held)(s32 handle);

    /* Console command `name args...`; fn returns 0 to print the usage `help`. */
    s32 (*command)(const char *name, s32 (*fn)(const char *args), const char *help);

    /* A text id (langGet bank 45) for `text`, which must stay valid; Rare's
     * strings end in "\n". */
    u16 (*string)(const char *text);

    /* An ITEM_* no level places, for a mod weapon. Same name, same item;
     * -1 when none are left. */
    s32 (*claim_item)(const char *name);
    /* First-person textures for `item` from Data/Textures/Models/<dir>/ */
    void (*item_model_dir)(s32 item, const char *dir);

    /* A MIDAS_EXPORT symbol of another loaded mod, or NULL */
    void *(*symbol)(const char *mod, const char *name);

    /* Never freed; safe for display lists on every platform */
    void *(*alloc_high)(u32 size);
} MidasHost;

typedef s32 (*MidasModInit)(const MidasHost *host);

/* Once per mod, at file scope: the ABI it was built against. */
#define MIDAS_MOD_ABI_EXPORT MIDAS_EXPORT const u32 midas_mod_abi = MIDAS_ABI;

#endif
