#ifndef PORT_INPUT_H
#define PORT_INPUT_H
#include <ultra64.h>
s32 portInputDeviceCount(void);
s32 portInputDeviceIniKey(s32 dev, const char *name, char *buf, s32 len);
s32 portInputShowInput(void);
#endif
