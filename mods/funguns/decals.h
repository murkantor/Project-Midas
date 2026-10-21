#ifndef FUNGUNS_DECALS_H
#define FUNGUNS_DECALS_H

#include <ultra64.h>
#include <bondtypes.h>

void funDecalClear(s32 i);
void funDecalSet(s32 i, coord3d *pos, coord3d *normal, s32 room, f32 halfw, f32 halfh, u8 r, u8 g, u8 b);
Gfx *funDecalsRender(Gfx *gdl);

#endif
