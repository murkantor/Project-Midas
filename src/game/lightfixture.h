#ifndef _LIGHTFIXTURE_H_
#define _LIGHTFIXTURE_H_

#include <ultra64.h>
#include <bondtypes.h>

typedef struct s_lightfixture {
    s16 room_index;
    s16 RESERVED;
    Gfx *ptr_start_pertinent_DL;
    Gfx *ptr_end_pertinent_DL;
} s_lightfixture;

struct s_darkened_light {
    u16 room_index;
    u16 vtx_index;
};

void lightFixtureEntryEnd(Gfx *param_1);
bool check_if_imageID_is_light(s32 imageID);
void lightFixtureEntryBegin(Gfx *DL);


void lightFixtureInitTables(void);
void clear_light_fixturetable_in_room(s32 room_index);
void redarken_lights_in_room(s32 room_index);
void lightFixtureBreak(Gfx * hit_gfx, u32 tri_type, s32 room_index);
s32 darkened_light_table_contains_vertex(Vtx * vertex, s32 room_index);

#endif
