/**
 * port_mods.h - the engine's side of the mod host (port_mods.c): hook runners
 * for the call sites, and the registries the menus, binds and console read.
 * Mods themselves only see midas_mod.h.
 */
#ifndef PORT_MODS_H
#define PORT_MODS_H

#include <ultra64.h>

struct coord3d;
struct StandTile;

void portModsLoad(void);
s32 portModsIsIniKey(const char *key);   /* [mods] load= */

/* hook runners: a no-op loop when nothing is registered */
void portModsLevelStart(void);
void portModsPlayerMove(struct coord3d *move, f32 walkx, f32 walkz);
void portModsPlayerMoved(f32 dx, f32 dz);
void portModsPlayerStepWindow(struct StandTile *floor, f32 *height, f32 *always_30);
s32 portModsPlayerBlocked(struct coord3d *next_pos);
f32 portModsPlayerFloor(f32 floor);
void portModsPlayerY(s32 use_stanHeight);
s32 portModsWeaponFire(s32 hand, s32 item);
s32 portModsWeaponAim(s32 pressed);
Gfx *portModsRenderWorld(Gfx *gdl);
Gfx *portModsRenderHud(Gfx *gdl);

/* options (joy_port.c falls through to these) */
s32 portModsOptionCount(void);
const char *portModsOptionKey(s32 i);
const char *portModsOptionLabel(s32 i);
s32 portModsOptionIsInt(s32 i, s32 *lo, s32 *hi, s32 *step);
s32 portModsSetOption(const char *key, const char *value);
s32 portModsOptionGet(const char *key, s32 *out);
s32 portModsOptionSet(const char *key, s32 v);

/* actions: slot i is joy_port.c PORTACT_MOD_FIRST + i */
s32 portModsActionCount(void);
const char *portModsActionName(s32 i);
const char *portModsActionLabel(s32 i);
const char *portModsActionDefault(s32 i);

/* console: 1 = `cmd` was a mod command (it ran and printed) */
s32 portModsCommand(const char *cmd, const char *args);
void portModsCommandHelp(void (*println)(const char *line));
void portModsList(void (*println)(const char *line));

const char *portModsString(s32 slot);
const char *portModsItemModelDir(s32 item);

#endif
