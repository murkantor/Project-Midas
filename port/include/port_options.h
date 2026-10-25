#ifndef PORT_OPTIONS_H
#define PORT_OPTIONS_H
#include <ultra64.h>
s32 portOptValueIni(s32 i, char *buf, s32 n);
s32 portOptSetIni(s32 i, const char *text);
s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n);
s32 portVideoIniSetKey(const char *key, const char *value);
s32 portCrosshairColor(void);

/* Classic Mode's mismatch and repair (port_options.c; the rule itself is in
 * classic_mode.h). Neither turns the mode on or off, and neither re-snapshots. */
s32 portClassicMismatchParts(s32 *idx, s32 max);
s32 portClassicRepair(void);

/* Suppress the n64_aa transition while a surface lands a batch of its own
 * values and will handle the mode itself (the VIDEO sheet's Apply Changes, Reset
 * All). Mirrors portOptSaveHold. Always pair a 1 with a 0. */
void portClassicSetHold(s32 on);
#endif
