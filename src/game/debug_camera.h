#ifndef _UNK_091080_H_
#define _UNK_091080_H_
#include <ultra64.h>

void debugFreeCamera(s8 joyX, s8 joyY, u16 joyBtns);
void setDebugCameraScale(f32 scale);
Gfx * sub_GAME_7F091580(Gfx *arg0);


void resetDebugCameraToPlayerPosition(void);
void initializeDebugCameraPosition(void);
void updateDebugCameraWorldPosition(void);

#endif
