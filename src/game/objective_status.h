#ifndef _OBJECTIVE_STATUS_H_
#define _OBJECTIVE_STATUS_H_
#include <ultra64.h>
#include <bondtypes.h>

extern struct objective_entry * objective_ptrs[OBJECTIVES_MAX];
extern  OBJECTIVESTATUS objectiveStatuses[OBJECTIVES_MAX]; //This is an array of 10 OBJECTIVESTATUS,

extern u32 *ptr_last_tag_entry_type16;
extern struct watchMenuObjectiveText *ptr_last_briefing_setup_entry_type23;
extern struct criteria_roomentered *ptr_last_enter_room_subobject_entry_type20;
extern struct criteria_deposit *ptr_last_deposit_in_room_subobject_entry_type21;
extern struct criteria_picture *ptr_last_photo_obj_in_room_subobject_entry_type1E;

extern s32 objective_count;
extern s32 objective_status_display_disabled;

void             objectiveTakePictureHandler(void);
bool             objectiveIsAllComplete(void);
TagObjectRecord *sub_GAME_7F057080(s32 TagID);
s32              objectiveGetCount(void);
ObjectRecord *   objFindByTagId(s32 TagID);
OBJECTIVESTATUS  get_status_of_objective(s32 objectiveNum);
void             sub_GAME_7F057C14(coord3d *coord, Mtxf *mtx);
void objectivestatusCheckDeposit(s32 weaponnum, s32 roomid);
void sub_GAME_7F057D88(f32 *arg0, f32 *arg1, f32 arg2);
void display_objective_status_text_on_status_change(void);
u8 * get_ptr_text_for_watch_breifing_page(WATCH_BRIEFING_PAGE page);

u8 * get_text_for_objective(int objectiveIndex);


void cleanupObjectives(void);
void objectivestatusCheckRoomEntered(s32 roomid);
s32 get_difficulty_for_objective(s32 objectiveIndex);

#endif
