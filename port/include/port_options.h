#ifndef PORT_OPTIONS_H
#define PORT_OPTIONS_H
#include <ultra64.h>
s32 portOptValueIni(s32 i, char *buf, s32 n);
s32 portOptSetIni(s32 i, const char *text);
s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n);
s32 portVideoIniSetKey(const char *key, const char *value);
s32 portCrosshairColor(void);
#endif
