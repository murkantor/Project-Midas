#ifndef _CHR_H_
#define _CHR_H_
#include <ultra64.h>
#include <bondgame.h>
#include <bondtypes.h>
#include "chrai.h"

#define EXPLOSION_ANIMATION_TABLE_LEN 8

struct StruckAnim;

// Per-body-part hits and deaths reaction descriptor.
struct ChrHitReaction
{
    s32 hitpart; // HITTARGET key
    s32 impactPuffCount;
    s32 unused_8;
    f32 impactPuffSize;
    s32 backImpactPuffCount; // Count for the impact puff(s) that appears behind the hit point
    s32 unused_14;
    f32 backImpactPuffSize;

    struct StruckAnim *deathAnims;
    s32 deathAnimCount;

    struct StruckAnim *flinchAnims;
    s32 flinchAnimCount;
};

// Animations for when characters are wounded or killed.
struct StruckAnim
{
    ModelAnimation *struck_anim;
    s32 flip; // Mirror flag selecting left/right variant
    f32 endframe;
    f32 speed;
    s32 knockback; // Enables characters to be pushed back when the gun's impact force > 0
    f32 thudframe1;
    f32 thudframe2;
};

struct explosion_death_animation
{
    s32 anonymous_0;
    s32 anonymous_1;
    f32 anonymous_2;
    f32 anonymous_3;
    f32 anonymous_4;
    f32 anonymous_5;
    f32 anonymous_6;
};

struct explosion_anim_group_info
{
    s8 *table;
    s32 count;
};

struct weapon_firing_animation_table
{
    union
    {
        s32 offset;
        struct ModelAnimation *anim;
    } anim;
    
    f32 unk04;
    f32 turn_angle_per_frame;
    f32 angle_offset;

    /**
     * Offset 0x10.
    */
    f32 start_frame;
    f32 end_frame;
    f32 shoot_start_frame;
    f32 shoot_end_frame;

    /**
     * Offset 0x20.
    */
    f32 recoil_start_frame;
    f32 recoil_end_frame;
    f32 aim_start_frame;
    f32 aim_end_frame;

    /**
     * Offset 0x30.
    */
    f32 max_up;
    f32 max_down;

    /**
     * Some kind of minimum. See chrlvUpdateAimendsideback.
     * Offset 0x38.
    */
    f32 max_left;

    /**
     * Some kind of maximum. See chrlvUpdateAimendsideback.
     * Offset 0x3c.
    */
    f32 max_right;

    /**
     * Offset 0x40.
    */
    f32 free_arm_frac_up;
    f32 free_arm_frac_down;
};

struct anim_group_info
{
    struct weapon_firing_animation_table (*table)[];
    s32 len;
};

extern s32 objectiveregisters1;
extern ChrRecord* g_ActiveChrs;
extern s32 g_ActiveChrsCount;

extern f32 g_AiAccuracyModifier;
extern f32 g_AiDamageModifier;
extern f32 g_AiHealthModifier;
extern f32 g_AiReactionSpeed;
extern s32 g_SeenBondRecentlyGuardCount;

extern struct ChrHitReaction g_HitReactionTable[];

extern struct StruckAnim death_left_foot[];
extern struct StruckAnim flinch_left_foot[];
extern struct StruckAnim death_left_leg[];
extern struct StruckAnim flinch_left_leg[];
extern struct StruckAnim death_left_thigh[];
extern struct StruckAnim flinch_left_thigh[];
extern struct StruckAnim death_right_foot[];
extern struct StruckAnim flinch_right_foot[];
extern struct StruckAnim death_right_leg[];
extern struct StruckAnim flinch_right_leg[];
extern struct StruckAnim death_right_thigh[];
extern struct StruckAnim flinch_right_thigh[];
extern struct StruckAnim death_pelvis[];
extern struct StruckAnim flinch_pelvis[];
extern struct StruckAnim death_head[];
extern struct StruckAnim flinch_head[];
extern struct StruckAnim death_left_hand[];
extern struct StruckAnim flinch_left_hand[];
extern struct StruckAnim death_left_arm[];
extern struct StruckAnim flinch_left_arm[];
extern struct StruckAnim death_left_shoulder[];
extern struct StruckAnim flinch_left_shoulder[];
extern struct StruckAnim death_right_hand[];
extern struct StruckAnim flinch_right_hand[];
extern struct StruckAnim death_right_arm[];
extern struct StruckAnim flinch_right_arm[];
extern struct StruckAnim death_right_shoulder[];
extern struct StruckAnim flinch_right_shoulder[];
extern struct StruckAnim death_chest[];
extern struct StruckAnim flinch_chest[];
extern struct StruckAnim death_gun[];
extern struct StruckAnim flinch_gun[];
extern struct StruckAnim death_stagger[];

extern struct ChrHitReaction D_8002CAA0;
extern struct ChrHitReaction D_8002CACC;
extern struct ChrHitReaction D_8002CB24;
extern struct ChrHitReaction D_8002CB50;

extern struct weapon_firing_animation_table rifle_firing_animation_group1[];
extern struct weapon_firing_animation_table rifle_firing_animation_group2[];
extern struct weapon_firing_animation_table rifle_firing_animation_group5[];
extern struct weapon_firing_animation_table rifle_firing_animation_group3[];
extern struct weapon_firing_animation_table rifle_firing_animation_group4[];
extern struct weapon_firing_animation_table pistol_firing_animation_group1[];
extern struct weapon_firing_animation_table pistol_firing_animation_group2[];
extern struct weapon_firing_animation_table pistol_firing_animation_group3[];
extern struct weapon_firing_animation_table pistol_firing_animation_group6[];
extern struct weapon_firing_animation_table pistol_firing_animation_group4[];
extern struct weapon_firing_animation_table pistol_firing_animation_group5[];
extern struct weapon_firing_animation_table doubles_firing_animation_group1[];
extern struct weapon_firing_animation_table doubles_firing_animation_group2[];
extern struct weapon_firing_animation_table doubles_firing_animation_group3[];
extern struct weapon_firing_animation_table crouched_rifle_firing_animation_group1[];
extern struct weapon_firing_animation_table crouched_rifle_firing_animation_groupA[];
extern struct weapon_firing_animation_table crouched_rifle_firing_animation_group2[];
extern struct weapon_firing_animation_table crouched_rifle_firing_animation_group3[];
extern struct weapon_firing_animation_table crouched_pistol_firing_animation_group1[];
extern struct weapon_firing_animation_table crouched_pistol_firing_animation_group2[];
extern struct weapon_firing_animation_table crouched_pistol_firing_animation_group3[];
extern struct weapon_firing_animation_table crouched_doubles_firing_animation_group1[];
extern struct weapon_firing_animation_table crouched_doubles_firing_animation_group2[];
extern struct weapon_firing_animation_table crouched_doubles_firing_animation_group3[];


extern struct weapon_firing_animation_table D_80030078[];
extern struct weapon_firing_animation_table D_80030660[];

extern struct anim_group_info *ptr_rifle_firing_animation_groups[];
extern struct anim_group_info *ptr_pistol_firing_animation_groups[];
extern struct anim_group_info *ptr_doubles_firing_animation_groups[];
extern struct anim_group_info *ptr_crouched_rifle_firing_animation_groups[];
extern struct anim_group_info *ptr_crouched_pistol_firing_animation_groups[];
extern struct anim_group_info *ptr_crouched_doubles_firing_animation_groups[];

extern f32 animation_rate;
extern s32 D_8002C904;
extern s32 g_AnimationTablePointerCountRelated;
extern s32 D_8002C90C;
extern s32 D_8002C910;

extern s32 D_8002CC58;
extern s32 show_patrols_flag;
extern s32 player1_guardID;
extern ChrRecord *g_ChrSlots;
extern s32 g_NumChrSlots;
extern ModelRenderData D_8002CC6C;
extern coord3d D_8002CCAC;
extern rgba_u8 gBloodColour;

extern f32 D_80030984;
extern f32 D_80030988;
extern f32 D_8003098C;
extern f32 D_80030990;
extern f32 D_80030994;
extern f32 D_80030998;
extern f32 D_8003099C;
extern f32 D_800309A0;
extern f32 D_800309A4;

extern point2d D_800309A8;
extern point2d D_800309B0;
extern point2d D_800309B8;
extern point2d D_800309C0;
extern point2d D_800309C8;
extern point2d D_800309D0;
extern point2d D_800309D8;
extern point2d D_800309E0;
extern point2d D_800309E8;
extern point2d D_800309F0;


extern u32 num_bodies;
extern u32 num_male_heads;
extern u32 num_female_heads;
extern s32 list_of_bodies[];
extern s32 random_male_heads[];
extern s32 random_female_heads[];
extern u32 current_random_body;
extern u32 current_random_male_head;
extern u32 current_random_female_head;

extern s32 female_guard_yelp_counter;
extern s16 female_guard_yelps[];
extern s32 male_guard_yelp_counter;
extern s16 male_guard_yelps[];

extern coord3d D_80030A44;
extern s16 metal_ricochet_SFX[3];

extern struct explosion_anim_group_info explosion_animation_table[];
extern struct explosion_death_animation D_8002E648[];

void        sub_GAME_7F022EE0(s32 param_1);
void        setanimationdebugflag(s32 param_1);
void        chrpropCleanupForRemoval(PropRecord* prop);
void        chrDetectRooms(ChrRecord *);
void        chrSetMoving(ChrRecord *guard,s32 param_2);
f32         getAnimationRate(void);
void        setAnimationRate(f32);
PropRecord *init_GUARDdata_with_set_values(PropRecord *, Model *, coord3d *, f32 arg2, StandTile * arg3, AIRecord *arg4);
PropRecord *chrAllocate(struct Model * arg0, coord3d * arg1, f32 arg2, StandTile * arg3, AIRecord *arg4);
void        chrSetHiddenToRandom(ChrRecord *arg0);
void        chrRemoved7F022E1C(f32 arg0);
void        chrDecrementAnimationTablePointerCount(void);
void        chrIncrementAnimationTablePointerCount(void);
void        chrToggleD_8002C90C(void);
void        chrCheckGuardsHeardSound(f32 arg0);
ChrRecord  *chrFindByLiteralId(s32 index);
PropRecord *chrGetEquippedWeaponProp(ChrRecord *arg0, GUNHAND arg1);
PropRecord *chrGetEquippedWeaponPropWithCheck(ChrRecord *ChrRecord, GUNHAND arg1);
void        chrUpdateCollisionBounds(PropRecord *arg0, struct rect4f **arg1, s32 *arg2, f32 *y_out, f32 *ground);
void        chrGetChrWidthHeight(PropRecord *arg0, f32 *width, f32 *height, f32 *always_20);
f32         chrGetChrGround(PropRecord *arg0);
void        chrDropItems(struct ChrRecord *arg0);
s32         get_numguards(void);
Gfx        *chrRenderProp(PropRecord *arg0, Gfx *arg1, s32 arg2);

void        chrAddHealth(ChrRecord *chr, f32 health);
void        chrSetMaxDamage(ChrRecord *chr, f32 maxdamage);
s32         propIsOfCdType(PropRecord* prop, s32 cdtypes);
s32         chrGetOnscreenRenderBounds(PropRecord *arg0, struct coord3d *arg1, struct coord2d *arg2, struct coord2d *arg3);
void        chrHandleBulletHit(struct ShotData *shot, struct BulletHit *bhit);
void        propExecuteTickOperation(PropRecord *prop, TICKOP op);

//tentative signature
s32         sub_GAME_7F01FC10(Model *, coord3d *, coord3d *, f32 *);
void        chrCreateBloodStain(Model *arg0, s32 arg1, ModelNode *arg2, struct coord3d *arg3);
void        chrpropAddBulletHit(struct ShotData *shotdata, PropRecord *prop, f32 dist, s32 hitpart, ModelNode *node, struct HitThing *hitthing, s32 room, ModelNode *unk44, Model *model, bool countsAsPenetration, s32 blocksFurtherHits);
void        chrTestHit(PropRecord *prop, ShotData *shotdata);
void        sub_GAME_7F03E134(PropRecord* p);

#ifdef BUGFIX_R1
bool chrCanUseDKModeScaling(s32 bodynum, s32 headnum);
#endif

Vertex *get_ptr_allocated_block_for_vertices(s32 count);


void cleanupGuardData(void);
s32 chrGetNumFree(void);
void set_show_patrols_flag(s32 flag);
s32 get_show_patrols_flag(void);
void chrpropDeregisterRoom(PropRecord* prop, s16 room);
void removed_debug_roomblocks_feature(void);
s32 propDoorGetCdTypes(PropRecord* prop);
bool doorIsPadlockFree(DoorRecord* door);
void chrpropDetach(PropRecord* prop);
s32 sub_GAME_7F03DB70(s32* roomids1, s32* roomids2);


s32 chrTick(PropRecord *prop);
#endif
