#ifndef _VIEWPORT_H_
#define _VIEWPORT_H_

#include <ultra64.h>

#include <PR/gbi.h>

Gfx *zbufClearCurrentPlayer(Gfx *gdl);
Gfx *zbufInit(Gfx *gdl);

void zbufDeallocate(void);


void zbufSetBuffer(intptr_t buffer, s32 width, s32 height);

#endif
