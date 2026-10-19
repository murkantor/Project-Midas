#include <ultra64.h>
#include "game/bgfog.h"
#include "game/vtxstore.h"
#ifdef GE_NATIVE_64
#include "romfile_model64.h"
#endif
#include <PR/gbi.h>
#include <bondgame.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include <random.h>
#include <snd.h>
#include <math.h>
#include "bondaicommands.h"
#include "bg.h"
#include "cheat.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "chrobjdata.h"
#include "debugmenu_handler.h"
#include "dyn.h"
#include "glass.h"
#include "file2.h"
#include "propobj.h"
#include "explosion.h"
#include "file.h"
#include "gun.h"
#include "initanitable.h"
#include "joy.h"
#include "lv.h"
#include "language.h"
#include "matrixmath.h"
#include "objecthandler.h"
#include "player.h"
#ifndef TARGET_N64
#include <stdio.h> /* GE_INTRODBG diag in chrRenderProp */
#endif
#include "propobj.h"
#include "stan.h"
#include "model.h"
#include "tex.h"

#ifndef TARGET_N64
/* Sub-field smoothing (frametiming.c): chr animation advances by the TRUE
 * fractional delta instead of whole fields, so guards no longer step on
 * the uneven 60-in-144 field pattern. Identical whenever the delta is a
 * whole number (tickhz=60, demo playback). */
#include "frametiming.h"
typedef f32 chranimticks_t;
#define chrModelTickAnim(m, t, u) modelTickAnimF(m, t, u)
#else
typedef s32 chranimticks_t;
#define chrModelTickAnim(m, t, u) modelTickAnim(m, t, u)
#endif

#ifdef VERSION_EU
#define GROUND_SMOOTH_FACTOR 0.118799984f /* 0x3DF34D68 (PAL-scaled 0.1) */
#define FALLSPEED_DECAY      0.8812f      /* 0x3F619653 (PAL-scaled 0.9) */
#else
#define GROUND_SMOOTH_FACTOR 0.100000024f /* 0x3DCCCCD0 */
#define FALLSPEED_DECAY      0.9f         /* 0x3F666666 */
#endif

#ifndef TARGET_N64
/* PORT: validate the hat attach chain before chrTick's hat render walks it,
 * since a garbage attachedto_objinst node crashed MP. Reports the first stray
 * link via the model-audit channel (port/src/model_audit.c) and returns nonzero
 * to skip the hat for this chr. */
static int portChrHatChainLooksBad(ChrRecord *chr)
{
    extern int portPtrLooksBad(const void *);
    extern void portModelAuditReport(const char *, const void *, const void *, const void *);
    ObjectRecord *hatobj;
    Model *hatmodel;

    if (portPtrLooksBad(chr->handle_positiondata_hat)) {
        portModelAuditReport("chrTick:hat-prop", chr->handle_positiondata_hat, chr, NULL);
        return 1;
    }
    hatobj = chr->handle_positiondata_hat->obj;
    if (portPtrLooksBad(hatobj)) {
        portModelAuditReport("chrTick:hat-obj", hatobj, chr, chr->handle_positiondata_hat);
        return 1;
    }
    hatmodel = hatobj->model;
    if (portPtrLooksBad(hatmodel)) {
        portModelAuditReport("chrTick:hat-model", hatmodel, chr, hatobj);
        return 1;
    }
    if (hatmodel->attachedto_objinst != NULL && portPtrLooksBad(hatmodel->attachedto_objinst)) {
        portModelAuditReport("chrTick:hat-attach-node", hatmodel->attachedto_objinst, chr, hatmodel);
        return 1;
    }
    return 0;
}
#endif

// forward declarations

void chrUpdateAimProperties( ChrRecord *arg0);
void chrUpdateAnim( ChrRecord *chr, chranimticks_t tickamount);
void sub_GAME_7F057D44(f32 *arg0, f32 *arg1, f32 arg2);
f32  get_007_health_mod(void);

// end forward declarations

// data
f32 animation_rate = 0;
s32 D_8002C904 = 0;

/**
 * Address 0x8002C908.
 */
s32 g_AnimationTablePointerCountRelated = 0;
s32 D_8002C90C = 0;
s32 D_8002C910 = 0;

/*
 * D:8002C914
 * Per-body-part hit reaction table, indexed by HITTARGET.
 * Legend: iN/iSz = impact puff count/size, bN/bSz = back puff count/size,
 *         u8/u14 = unused, dN/fN = death/flinch anim counts (filled at init by initWeaponAnimGroups).
 */
/*                       part,                       iN,  u8,  iSz,   bN, u14,  bSz,  deathAnims,           dN, flinchAnims,          fN */
struct ChrHitReaction g_HitReactionTable[] = {
/* HIT_NULL_PART      */ {0                        , 0,   0,   0,     0,  0,    0,    NULL,                 0,  NULL,                  0},
/* HIT_LEFT_FOOT      */ {1                        , 1,   0,   17.0,  3,  0,    34.0, death_left_foot,      0,  flinch_left_foot,      0},
/* HIT_LEFT_LEG       */ {2                        , 1,   0,   17.0,  3,  0,    39.0, death_left_leg,       0,  flinch_left_leg,       0},
/* HIT_LEFT_THIGH     */ {3                        , 1,   0,   21.0,  3,  0,    43.0, death_left_thigh,     0,  flinch_left_thigh,     0},
/* HIT_RIGHT_FOOT     */ {4                        , 1,   0,   17.0,  3,  0,    34.0, death_right_foot,     0,  flinch_right_foot,     0},
/* HIT_RIGHT_LEG      */ {5                        , 1,   0,   17.0,  3,  0,    39.0, death_right_leg,      0,  flinch_right_leg,      0},
/* HIT_RIGHT_THIGH    */ {6                        , 1,   0,   21.0,  3,  0,    43.0, death_right_thigh,    0,  flinch_right_thigh,    0},
/* HIT_PELVIS         */ {7                        , 1,   0,   21.0,  3,  0,    52.0, death_pelvis,         0,  flinch_pelvis,         0},
/* HIT_HEAD           */ {8                        , 1,   0,   21.0,  3,  0,    43.0, death_head,           0,  flinch_head,           0},
/* HIT_LEFT_HAND      */ {9                        , 1,   0,   17.0,  3,  0,    34.0, death_left_hand,      0,  flinch_left_hand,      0},
/* HIT_LEFT_ARM       */ {0xA                      , 1,   0,   17.0,  3,  0,    43.0, death_left_arm,       0,  flinch_left_arm,       0},
/* HIT_LEFT_SHOULDER  */ {0xB                      , 1,   0,   21.0,  3,  0,    52.0, death_left_shoulder,  0,  flinch_left_shoulder,  0},
/* HIT_RIGHT_HAND     */ {0xC                      , 1,   0,   17.0,  3,  0,    34.0, death_right_hand,     0,  flinch_right_hand,     0},
/* HIT_RIGHT_ARM      */ {0xD                      , 1,   0,   17.0,  3,  0,    43.0, death_right_arm,      0,  flinch_right_arm,      0},
/* HIT_RIGHT_SHOULDER */ {0xE                      , 1,   0,   21.0,  3,  0,    52.0, death_right_shoulder, 0,  flinch_right_shoulder, 0},
/* HIT_CHEST          */ {0xF                      , 1,   0,   26.0,  3,  0,    60.0, death_chest,          0,  flinch_chest,          0},
/* HIT_GUN            */ {0x64                     , 1,   0,   26.0,  0,  0,    0.0,  death_gun,            0,  flinch_gun,            0},
/* HIT_HAT            */ {0x6E                     , 1,   0,   21.0,  0,  0,    0.0,  NULL,                 0,  NULL,                  0},
/* terminator         */ {0xFFFFFFFF               , 0,   0,   0.0,   0,  0,    0.0,  NULL,                 0,  NULL,                  0},
};

s32 D_8002CC58 = 0; // Set to 0 but otherwise never used.
s32 show_patrols_flag = FALSE;
s32 player1_guardID = 5000;
ChrRecord *g_ChrSlots = 0;
s32 g_NumChrSlots = 0;

ModelRenderData D_8002CC6C         = {NULL,
                                      TRUE,
                                      0x00000003,
                                      NULL,
                                      NULL,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      {0, 0, 0, 0},
                                      {0, 0, 0, 0},
                                      CULLMODE_BOTH};

coord3d D_8002CCAC = {0, 0, 0};

rgba_u8 gBloodColour = { 0x5a, 0, 0, 0};

/**
 * Address 0x8002CCBC.
*/

ModelRenderData D_8002CCBC         = {NULL,
                                      TRUE,
                                      0x00000003,
                                      NULL,
                                      NULL,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      {0, 0, 0, 0},
                                      {0, 0, 0, 0},
                                      CULLMODE_BOTH};

s32 D_8002CCFC = 0; // Unused.
u32 num_bodies = 0;
u32 num_male_heads = 0;
u32 num_female_heads = 0;

s32 list_of_bodies[] = {
    BODY_Jungle_Commando,BODY_St_Petersburg_Guard,BODY_Russian_Soldier,BODY_Russian_Infantry,
    BODY_Janus_Special_Forces,BODY_Brosnan_Tuxedo,BODY_Boris,BODY_Ourumov,
    BODY_Trevelyan_Janus,BODY_Valentin_,BODY_Xenia,BODY_Baron_Samedi,
    BODY_Jaws,BODY_Mayday,BODY_Oddjob,BODY_Natalya_Skirt,
    BODY_Janus_Marine,BODY_Russian_Commandant,BODY_Siberian_Guard_1_Mishkin,BODY_Naval_Officer,
    BODY_Siberian_Special_Forces,BODY_Special_Operations_Uniform,BODY_Formal_Wear,BODY_Jungle_Fatigues,
    BODY_Unused_Female,BODY_Rosika,BODY_Scientist_2_Female,BODY_Civilian_1_Female,
    BODY_Unused_Male_1,BODY_Unused_Male_2,BODY_Civilian_4,BODY_Civilian_2,
    BODY_Civilian_3,BODY_Scientist_1_Male,BODY_Brosnan_Tuxedo,BODY_Brosnan_Tuxedo,
    BODY_Brosnan_Tuxedo,BODY_Helicopter_Pilot,BODY_Siberian_Guard_2,BODY_Arctic_Commando,
    BODY_Moonraker_Elite_1_Male,BODY_Moonraker_Elite_2_Female,-1,
};

s32 random_male_heads[] = {
    HEAD_Male_Jim,HEAD_Male_Chris,HEAD_Male_Lee,HEAD_Male_Graeme,HEAD_Male_Steve_H,
    HEAD_Male_Neil,HEAD_Male_Robin,HEAD_Male_Des,HEAD_Male_Grant,HEAD_Male_Dave_Dr_Doak,
    HEAD_Male_Karl,HEAD_Male_Alan,HEAD_Male_Pete,HEAD_Male_Martin,HEAD_Male_Mark,
    HEAD_Male_Duncan,HEAD_Male_Shaun,HEAD_Male_Dwayne,HEAD_Male_B,HEAD_Male_Steve_Ellis,
    HEAD_Male_Joel,HEAD_Male_Scott,HEAD_Male_Joe_Altered,HEAD_Male_Ken,HEAD_Male_Joe,
    -1
};

s32 random_female_heads[] = {
    HEAD_Female_Sally,HEAD_Female_Marion_Rosika,HEAD_Female_Mandy,HEAD_Female_Vivien, -1
};

u32 current_random_body = 0;
u32 current_random_male_head = 0;
u32 current_random_female_head = 0;

/*
* Enemy accuracy modifier.
* Set on level load.
* One of the values that can be set with the 007 slider.
*/
f32 g_AiAccuracyModifier = 1.0f;

/*
* Enemy damage modifier.
* Set on level load.
* One of the values that can be set with the 007 slider.
*/
f32 g_AiDamageModifier = 1.0f;

f32 g_AiHealthModifier = 1.0f;

/*
* Enemy reaction speed modifier.
* Set on level load.
* One of the values that can be set with the 007 slider.
*/
f32 g_AiReactionSpeed = 1.0f;

/**
 * Count of number of guards that have recently seen bond:
 *     (guard->lastseetarget60 > 0) && (g_GlobalTimer - guard->lastseetarget60 < 120)
 * Updated every tick.
 * Address 0x8002CE50.
*/
s32 g_SeenBondRecentlyGuardCount = 0;

struct StruckAnim death_left_foot[] = {
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 0, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 0, 23.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_left_leg[] = {
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 0, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 0, 23.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_left_thigh[] = {
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 1, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 1, 23.0, -1.0 },
    { PTR_ANIM_death_left_leg, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_right_foot[] = {
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 0, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 0, 26.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_right_leg[] = {
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 0, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 0, 26.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_right_thigh[] = {
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 1, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 1, 26.0, -1.0 },
    { PTR_ANIM_death_left_leg, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, 0.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_pelvis[] = {
    { PTR_ANIM_death_forward_face_down, 0, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_face_down, 1, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_spin_face_up, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_forward_spin_face_up, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 0, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 1, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_hard, 0, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_forward_face_down_hard, 1, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_fetal_position_right, 0, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_right, 1, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 0, -1.0, 0.5, 0, 28.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 1, -1.0, 0.5, 0, 28.0, -1.0 },
    { PTR_ANIM_death_genitalia, 0, -1.0, 0.5, 0, 79.0, 415.0 },
    { PTR_ANIM_death_genitalia, 1, -1.0, 0.5, 0, 79.0, 415.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_head[] = {
    { PTR_ANIM_death_forward_face_down, 0, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_face_down, 1, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_spin_face_up, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_forward_spin_face_up, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 0, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 1, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 1, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 1, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 1, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 1, 26.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_hard, 0, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_forward_face_down_hard, 1, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_forward_face_down_soft, 0, -1.0, 0.5, 0, 94.0, 66.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, -1.0, 0.5, 0, 94.0, 66.0 },
    { PTR_ANIM_death_fetal_position_right, 0, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_right, 1, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 0, -1.0, 0.5, 0, 28.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 1, -1.0, 0.5, 0, 28.0, -1.0 },
    { PTR_ANIM_death_neck, 0, -1.0, 0.5, 0, 87.0, 203.0 },
    { PTR_ANIM_death_neck, 1, -1.0, 0.5, 0, 87.0, 203.0 },
    { PTR_ANIM_death_head, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_head, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_left_hand[] = {
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 0, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 0, 23.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_left_arm[] = {
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 0, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 0, 23.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_left_shoulder[] = {
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 1, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 1, 23.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_right_hand[] = {
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 0, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 0, 26.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_right_arm[] = {
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 0, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 0, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 0, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 0, 26.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_right_shoulder[] = {
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 1, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 1, 26.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_chest[] = {
    { PTR_ANIM_death_forward_face_down, 0, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_face_down, 1, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_spin_face_up, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_forward_spin_face_up, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 0, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 1, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 0, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_right, 1, -1.0, 0.5, 1, 25.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 0, -1.0, 0.5, 1, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_right, 1, -1.0, 0.5, 1, 23.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 0, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_down_left, 1, -1.0, 0.5, 1, 27.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 0, -1.0, 0.5, 1, 26.0, -1.0 },
    { PTR_ANIM_death_backward_spin_face_up_left, 1, -1.0, 0.5, 1, 26.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_hard, 0, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_forward_face_down_hard, 1, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_forward_face_down_soft, 0, -1.0, 0.5, 0, 94.0, 66.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, -1.0, 0.5, 0, 94.0, 66.0 },
    { PTR_ANIM_death_fetal_position_right, 0, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_right, 1, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 0, -1.0, 0.5, 0, 28.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 1, -1.0, 0.5, 0, 28.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_gun[] = {
    { PTR_ANIM_death_forward_face_down, 0, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_face_down, 1, -1.0, 0.5, 0, 55.0, 39.0 },
    { PTR_ANIM_death_forward_spin_face_up, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_forward_spin_face_up, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 0, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up1, 1, -1.0, 0.5, 1, 29.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_hard, 0, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_forward_face_down_hard, 1, -1.0, 0.5, 0, 97.0, 64.0 },
    { PTR_ANIM_death_forward_face_down_soft, 0, -1.0, 0.5, 0, 94.0, 66.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, -1.0, 0.5, 0, 94.0, 66.0 },
    { PTR_ANIM_death_fetal_position_right, 0, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_right, 1, -1.0, 0.5, 0, 31.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 0, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_fetal_position_left, 1, -1.0, 0.5, 0, 36.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 0, -1.0, 0.5, 0, 28.0, -1.0 },
    { PTR_ANIM_death_backward_fall_face_up2, 1, -1.0, 0.5, 0, 28.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim death_stagger[] = {
    { PTR_ANIM_death_stagger_back_to_wall, 0, -1.0, 0.5, 0, 67.0, 54.0 },
    { PTR_ANIM_death_stagger_back_to_wall, 1, -1.0, 0.5, 0, 67.0, 54.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_left_foot[] = {
    { PTR_ANIM_hit_left_leg, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_right_leg, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_left_leg[] = {
    { PTR_ANIM_hit_left_leg, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_right_leg, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_left_thigh[] = {
    { PTR_ANIM_hit_left_leg, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_right_leg, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_left_leg, 1, 20.0, 0.40000001, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_right_foot[] = {
    { PTR_ANIM_hit_right_leg, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_left_leg, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_right_leg[] = {
    { PTR_ANIM_hit_right_leg, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_left_leg, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_right_thigh[] = {
    { PTR_ANIM_hit_right_leg, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_left_leg, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_left_leg, 0, 20.0, 0.40000001, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_pelvis[] = {
    { PTR_ANIM_death_genitalia, 0, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_genitalia, 1, 30.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_soft, 0, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 0, 15.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 1, 15.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_fetal_position_right, 0, 10.0, 0.25, 0, -1.0, -1.0 },
    { PTR_ANIM_death_fetal_position_right, 1, 10.0, 0.25, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_head[] = {
    { PTR_ANIM_death_neck, 0, 15.0, 0.5, 0, 87.0, 203.0 },
    { PTR_ANIM_death_neck, 1, 15.0, 0.5, 0, 87.0, 203.0 },
    { PTR_ANIM_death_forward_face_down_soft, 0, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 0, 15.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 1, 15.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_left_hand[] = {
    { PTR_ANIM_hit_left_hand, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_right_hand, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_left_arm[] = {
    { PTR_ANIM_hit_left_arm, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_right_arm, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_left_shoulder[] = {
    { PTR_ANIM_hit_left_shoulder, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_right_shoulder, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_soft, 0, 20.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_right_hand[] = {
    { PTR_ANIM_hit_right_hand, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_left_hand, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_right_arm[] = {
    { PTR_ANIM_hit_right_arm, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_left_arm, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_right_shoulder[] = {
    { PTR_ANIM_hit_right_shoulder, 0, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_hit_left_shoulder, 1, -1.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, 20.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_chest[] = {
    { PTR_ANIM_death_forward_face_down_soft, 0, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 0, 15.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 1, 15.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};

struct StruckAnim flinch_gun[] = {
    { PTR_ANIM_death_forward_face_down_soft, 0, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down_soft, 1, 20.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 0, 15.0, 0.5, 0, -1.0, -1.0 },
    { PTR_ANIM_death_forward_face_down, 1, 15.0, 0.5, 0, -1.0, -1.0 },
    {0, 0, -1.0, 0.5, 0, -1.0, -1.0}
};



struct explosion_death_animation D_8002E648[] = {
    { PTR_ANIM_death_explosion_forward, 0, 0.5, 9.0, 18.0, 29.0, -1.0 },
    { PTR_ANIM_death_explosion_forward, 1, 0.5, 9.0, 18.0, 29.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_face_down, 0, 0.5, 11.0, 19.0, 31.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_face_down, 1, 0.5, 11.0, 19.0, 31.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_roll, 0, 0.5, 6.0, 20.0, 27.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_roll, 1, 0.5, 6.0, 20.0, 27.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right2, 0, 0.5, 29.0, 36.0, 48.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right2, 1, 0.5, 29.0, 36.0, 48.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right2_alt, 0, 0.5, 29.0, 38.0, 49.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right2_alt, 1, 0.5, 29.0, 38.0, 49.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right3, 0, 0.5, 19.0, 30.0, 42.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right3, 1, 0.5, 19.0, 30.0, 42.0, -1.0 },
    { PTR_ANIM_death_explosion_left1, 0, 0.5, 9.0, 21.0, 29.0, 55.0 },
    { PTR_ANIM_death_explosion_left1, 1, 0.5, 9.0, 21.0, 29.0, 55.0 },
    { PTR_ANIM_death_explosion_right, 0, 0.5, 6.0, 18.0, 27.0, -1.0 },
    { PTR_ANIM_death_explosion_right, 1, 0.5, 6.0, 18.0, 27.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right1, 0, 0.5, 6.0, 19.0, 29.0, -1.0 },
    { PTR_ANIM_death_explosion_forward_right1, 1, 0.5, 6.0, 19.0, 29.0, -1.0 },
    { PTR_ANIM_death_explosion_back_left, 0, 0.5, 8.0, 14.0, 25.0, -1.0 },
    { PTR_ANIM_death_explosion_back_left, 1, 0.5, 8.0, 14.0, 25.0, -1.0 },
    { PTR_ANIM_death_explosion_back1, 0, 0.5, 8.0, 19.0, 25.0, -1.0 },
    { PTR_ANIM_death_explosion_back1, 1, 0.5, 8.0, 19.0, 25.0, -1.0 },
    { PTR_ANIM_death_explosion_back2, 0, 0.5, 12.0, 21.0, 29.0, -1.0 },
    { PTR_ANIM_death_explosion_back2, 1, 0.5, 12.0, 21.0, 29.0, -1.0 },
    { PTR_ANIM_death_explosion_left2, 0, 0.5, 22.0, 30.0, 41.0, -1.0 },
    { PTR_ANIM_death_explosion_left2, 1, 0.5, 22.0, 30.0, 41.0, -1.0 },
    {0, 0, 0.5, 0.0, 0.0, 0.0, -1.0},
};

s8 expl_forward[] = {0x0, 0x01, 0x02, 0x03, 0x4, 0x05, 0x00, 0x00};
s8 expl_f_left[] = {0x7, 0x09, 0x0B, 0x00};
s8 expl_f_right[] = {0x06, 0x08, 0x0A, 0x00};
s8 expl_left[] = {0x0C, 0x0F, 0x11, 0x00};
s8 expl_right[] = {0x0D, 0x0E, 0x10, 0x00};
s8 expl_back[] = {0x14, 0x15, 0x16, 0x17};
s8 expl_b_right[] = {0x12, 0x18, 0x00, 0x00};
s8 expl_b_left[] = {0x13, 0x19, 0x00, 0x00};

struct explosion_anim_group_info explosion_animation_table[EXPLOSION_ANIMATION_TABLE_LEN] = {
    {expl_forward, 6},
    {expl_f_left, 3},
    {expl_left, 3},
    {expl_b_right, 2},
    {expl_back, 4},
    {expl_b_left, 2},
    {expl_right, 3},
    {expl_f_right, 3}
};

struct weapon_firing_animation_table rifle_firing_animation_group1[] = {
    { PTR_ANIM_fire_standing_fast, 28.0, 0, 0, 0, -1.0, 23.0, 54.0, -1.0, -1.0, 18.0, 54.0, 0.87266463, -0.52359879, 1.0471976, -0.34906587, 1.6, 1.8 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_rifle_firing_animation_group1 = { &rifle_firing_animation_group1, -1 };

struct weapon_firing_animation_table rifle_firing_animation_group2[] = {
    { PTR_ANIM_fire_standing, 37.0, 0, 0, 0, -1.0, 30.0, 81.0, -1.0, -1.0, 25.0, 81.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 1.6, 1.75 },
    { PTR_ANIM_fire_hip, 27.0, 0, 0, 0, -1.0, 22.0, 61.0, -1.0, -1.0, 17.0, 61.0, 0.87266463, -0.2617994, 0.69813174, -0.69813174, 2.0, 1.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_rifle_firing_animation_group2 = { &rifle_firing_animation_group2, -1 };

struct weapon_firing_animation_table rifle_firing_animation_group5[] = {
    { PTR_ANIM_fire_standing, 37.0, 0, 0, 0, -1.0, 30.0, 81.0, -1.0, -1.0, 25.0, 81.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 1.6, 1.75 },
    { PTR_ANIM_fire_hip, 27.0, 0, 0, 0, -1.0, 22.0, 61.0, -1.0, -1.0, 17.0, 61.0, 0.87266463, -0.2617994, 0.69813174, -0.69813174, 2.0, 1.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_rifle_firing_animation_group5 = { &rifle_firing_animation_group5, -1 };

struct weapon_firing_animation_table rifle_firing_animation_group3[] = {
    { PTR_ANIM_fire_shoulder_left, 19.0, 0, 1.5707964, 0, -1.0, 19.0, 61.0, -1.0, -1.0, 14.0, 61.0, 0.87266463, -0.34906587, 0.43633232, -1.0471976, 2.5, 2.5 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_rifle_firing_animation_group3 = { &rifle_firing_animation_group3, -1 };

struct weapon_firing_animation_table rifle_firing_animation_group4[] = {
    { PTR_ANIM_fire_turn_right2, 27.0, 0, 0, 0, -1.0, 39.0, 74.0, -1.0, -1.0, 34.0, 74.0, 0.87266463, -0.69813174, 0.78539819, -0.69813174, 1.5, 1.5 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_rifle_firing_animation_group4 = { &rifle_firing_animation_group4, -1 };

struct anim_group_info *ptr_rifle_firing_animation_groups[] = {
    &ptr_rifle_firing_animation_group1,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group2,
    &ptr_rifle_firing_animation_group3,
    &ptr_rifle_firing_animation_group3,
    &ptr_rifle_firing_animation_group3,
    &ptr_rifle_firing_animation_group3,
    &ptr_rifle_firing_animation_group3,
    &ptr_rifle_firing_animation_group3,
    &ptr_rifle_firing_animation_group4,
    &ptr_rifle_firing_animation_group4,
    &ptr_rifle_firing_animation_group4,
    &ptr_rifle_firing_animation_group4,
    &ptr_rifle_firing_animation_group4,
    &ptr_rifle_firing_animation_group4,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group5,
    &ptr_rifle_firing_animation_group1
};

struct weapon_firing_animation_table pistol_firing_animation_group1[] = {
    { PTR_ANIM_fire_standing_one_handed_weapon, 26.0, 0, 0, 12.0, 140.0, 58.0, 92.0, 60.0, 79.0, 20.0, 120.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 0.0, 0.0 },
    { PTR_ANIM_fire_hip_one_handed_weapon_fast, 0.0, 0, 0, 17.0, 100.0, 25.0, 87.0, 30.0, 55.0, 20.0, 93.0, 0.87266463, -0.69813174, 0.69813174, -1.0471976, 0.0, 0.0 },
    { PTR_ANIM_fire_hip_one_handed_weapon_slow, 0.0, 0, 0, 12.0, 64.0, 19.0, 51.0, 24.0, 46.0, 14.0, 58.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_hip_forward_one_handed_weapon, 22.0, 0, 0, 4.0, 69.0, 22.0, 49.0, 22.0, 33.0, 8.0, 58.0, 0.87266463, -0.69813174, 0.43633232, -0.78539819, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_pistol_firing_animation_group1 = { &pistol_firing_animation_group1, -1 };

struct weapon_firing_animation_table pistol_firing_animation_group2[] = {
    { PTR_ANIM_fire_standing_one_handed_weapon, 26.0, 0, 0, 12.0, 140.0, 58.0, 92.0, 60.0, 79.0, 20.0, 120.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 0.0, 0.0 },
    { PTR_ANIM_fire_hip_forward_one_handed_weapon, 22.0, 0, 0, 4.0, 69.0, 22.0, 49.0, 22.0, 33.0, 8.0, 58.0, 0.87266463, -0.69813174, 0.43633232, -0.78539819, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_pistol_firing_animation_group2 = { &pistol_firing_animation_group2, -1 };

struct weapon_firing_animation_table pistol_firing_animation_group3[] = {
    { PTR_ANIM_fire_standing_one_handed_weapon, 26.0, 0, 0, 12.0, 140.0, 58.0, 92.0, 60.0, 79.0, 20.0, 120.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 0.0, 0.0 },
    { PTR_ANIM_fire_hip_forward_one_handed_weapon, 22.0, 0, 0, 4.0, 69.0, 22.0, 49.0, 22.0, 33.0, 8.0, 58.0, 0.87266463, -0.69813174, 0.43633232, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_standing_left_one_handed_weapon_slow, 0.0, 0, 1.5707964, 7.0, 130.0, 45.0, 93.0, 56.0, 73.0, 26.0, 107.0, 0.87266463, -0.69813174, 0.34906587, -0.52359879, 0.0, 0.0 },
    { PTR_ANIM_fire_standing_left_one_handed_weapon_fast, 15.0, 0, 1.5707964, 5.0, 76.0, 20.0, 31.0, 31.0, 38.0, 15.0, 49.0, 0.87266463, -0.69813174, 0.52359879, -1.0471976, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_pistol_firing_animation_group3 = { &pistol_firing_animation_group3, -1 };

struct weapon_firing_animation_table pistol_firing_animation_group6[] = {
    { PTR_ANIM_fire_standing_one_handed_weapon, 26.0, 0, 0, 12.0, 140.0, 58.0, 92.0, 60.0, 79.0, 20.0, 120.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 0.0, 0.0 },
    { PTR_ANIM_fire_hip_forward_one_handed_weapon, 22.0, 0, 0, 4.0, 69.0, 22.0, 49.0, 22.0, 33.0, 8.0, 58.0, 0.87266463, -0.69813174, 0.43633232, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_standing_right_one_handed_weapon, 0.0, 0, 4.712389, 7.0, 139.0, 54.0, 105.0, 61.0, 88.0, 26.0, 120.0, 0.87266463, -0.69813174, 0.69813174, -0.61086529, 0.0, 0.0 },
    { PTR_ANIM_fire_step_right_one_handed_weapon, 19.0, 0, 4.712389, 4.0, 79.0, 21.0, 50.0, 26.0, 42.0, 10.0, 64.0, 0.87266463, -0.69813174, 0.69813174, -0.61086529, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_pistol_firing_animation_group6 = { &pistol_firing_animation_group6, -1 };

struct weapon_firing_animation_table pistol_firing_animation_group4[] = {
    { PTR_ANIM_fire_standing_left_one_handed_weapon_fast, 19.0, 0, 1.5707964, 5.0, 76.0, 20.0, 31.0, 31.0, 38.0, 15.0, 49.0, 0.87266463, -0.69813174, 0.52359879, -1.0471976, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_pistol_firing_animation_group4 = { &pistol_firing_animation_group4, -1 };

struct weapon_firing_animation_table pistol_firing_animation_group5[] = {
    { PTR_ANIM_fire_step_right_one_handed_weapon, 19.0, 0, 4.712389, 4.0, 79.0, 21.0, 50.0, 26.0, 42.0, 10.0, 64.0, 0.87266463, -0.69813174, 0.69813174, -0.61086529, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_pistol_firing_animation_group5 = { &pistol_firing_animation_group5, -1 };

struct anim_group_info *ptr_pistol_firing_animation_groups[] = {
    &ptr_pistol_firing_animation_group1,
    &ptr_pistol_firing_animation_group1,
    &ptr_pistol_firing_animation_group2,
    &ptr_pistol_firing_animation_group2,
    &ptr_pistol_firing_animation_group2,
    &ptr_pistol_firing_animation_group3,
    &ptr_pistol_firing_animation_group3,
    &ptr_pistol_firing_animation_group3,
    &ptr_pistol_firing_animation_group3,
    &ptr_pistol_firing_animation_group3,
    &ptr_pistol_firing_animation_group4,
    &ptr_pistol_firing_animation_group4,
    &ptr_pistol_firing_animation_group4,
    &ptr_pistol_firing_animation_group4,
    &ptr_pistol_firing_animation_group4,
    &ptr_pistol_firing_animation_group4,
    &ptr_pistol_firing_animation_group5,
    &ptr_pistol_firing_animation_group5,
    &ptr_pistol_firing_animation_group5,
    &ptr_pistol_firing_animation_group5,
    &ptr_pistol_firing_animation_group5,
    &ptr_pistol_firing_animation_group5,
    &ptr_pistol_firing_animation_group6,
    &ptr_pistol_firing_animation_group6,
    &ptr_pistol_firing_animation_group6,
    &ptr_pistol_firing_animation_group6,
    &ptr_pistol_firing_animation_group6,
    &ptr_pistol_firing_animation_group2,
    &ptr_pistol_firing_animation_group2,
    &ptr_pistol_firing_animation_group2,
    &ptr_pistol_firing_animation_group1,
    &ptr_pistol_firing_animation_group1
};

struct weapon_firing_animation_table doubles_firing_animation_group1[] = {
    { PTR_ANIM_fire_standing_dual_wield, 26.0, 0, 0, 7.0, 92.0, 28.0, 68.0, -1.0, -1.0, 11.0, 73.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_doubles_firing_animation_group1 = { &doubles_firing_animation_group1, -1 };

struct weapon_firing_animation_table doubles_firing_animation_group2[] = {
    { PTR_ANIM_fire_standing_dual_wield_left, 26.0, 0, 1.5707964, 9.0, 112.0, 38.0, 87.0, -1.0, -1.0, 19.0, 98.0, 0.87266463, -0.69813174, 0.43633232, -0.43633232, 0.0, 0.0 },
    { PTR_ANIM_fire_standing_dual_wield_hands_crossed_left, 25.0, 0, 1.5707964, 10.0, 112.0, 32.0, 86.0, -1.0, -1.0, 19.0, 97.0, 0.87266463, -0.69813174, 0.43633232, -0.43633232, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_doubles_firing_animation_group2 = { &doubles_firing_animation_group2, -1 };

struct weapon_firing_animation_table doubles_firing_animation_group3[] = {
    { PTR_ANIM_fire_standing_dual_wield_right, 39.0, 0, 4.712389, 22.0, 127.0, 44.0, 102.0, -1.0, -1.0, 28.0, 112.0, 0.87266463, -0.69813174, 0.43633232, -0.43633232, 0.0, 0.0 },
    { PTR_ANIM_fire_standing_dual_wield_hands_crossed_right, 39.0, 0, 4.712389, 23.0, 130.0, 46.0, 100.0, -1.0, -1.0, 30.0, 110.0, 0.87266463, -0.69813174, 0.43633232, -0.43633232, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_doubles_firing_animation_group3 = { &doubles_firing_animation_group3, -1 };

struct anim_group_info *ptr_doubles_firing_animation_groups[] = {
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group2,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group3,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1,
    &ptr_doubles_firing_animation_group1
};

struct weapon_firing_animation_table crouched_rifle_firing_animation_group1[] = {
    { PTR_ANIM_fire_kneel_right_leg, 27.0, 0, 0, 0, -1.0, 35.0, 75.0, -1.0, -1.0, 31.0, 75.0, 0.87266463, -0.69813174, 0.90757126, -0.69813174, 1.5, 1.5 },
#ifndef TARGET_N64
    /* PORT: on N64 this group has no terminator, so initResolveAnimGroupTable
     * runs into the adjacent crouched_rifle_firing_animation_groupA and stops
     * at its terminator (effective length 2). We spell out that effective table
     * explicitly because GCC's layout makes the implicit walk unsound. */
    { PTR_ANIM_fire_kneel_left_leg, 24.0, 0, 0, 0, -1.0, 46.0, 98.0, -1.0, -1.0, 41.0, 98.0, 0.87266463, -0.52359879, 1.134464, -0.69813174, 1.6, 1.6 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
#endif
};

struct weapon_firing_animation_table crouched_rifle_firing_animation_groupA[] = {
    { PTR_ANIM_fire_kneel_left_leg, 24.0, 0, 0, 0, -1.0, 46.0, 98.0, -1.0, -1.0, 41.0, 98.0, 0.87266463, -0.52359879, 1.134464, -0.69813174, 1.6, 1.6 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_rifle_firing_animation_group1 = { &crouched_rifle_firing_animation_group1, -1 };

struct weapon_firing_animation_table crouched_rifle_firing_animation_group2[] = {
    { PTR_ANIM_fire_kneel_left, 26.0, 0, 0, 0, -1.0, 34.0, 87.0, -1.0, -1.0, 29.0, 87.0, 0.87266463, -0.52359879, 0.69813174, -0.95993108, 1.6, 2.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_rifle_firing_animation_group2 = { &crouched_rifle_firing_animation_group2, -1 };

struct weapon_firing_animation_table crouched_rifle_firing_animation_group3[] = {
    { PTR_ANIM_fire_kneel_right, 28.0, 0, 0, 0, -1.0, 36.0, 88.0, -1.0, -1.0, 31.0, 88.0, 0.87266463, -0.69813174, 0.87266463, -0.43633232, 1.6, 1.5 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_rifle_firing_animation_group3 = { &crouched_rifle_firing_animation_group3, -1 };

struct anim_group_info *ptr_crouched_rifle_firing_animation_groups[] = {
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group2,
    &ptr_crouched_rifle_firing_animation_group2,
    &ptr_crouched_rifle_firing_animation_group2,
    &ptr_crouched_rifle_firing_animation_group2,
    &ptr_crouched_rifle_firing_animation_group2,
    &ptr_crouched_rifle_firing_animation_group2,
    &ptr_crouched_rifle_firing_animation_group3,
    &ptr_crouched_rifle_firing_animation_group3,
    &ptr_crouched_rifle_firing_animation_group3,
    &ptr_crouched_rifle_firing_animation_group3,
    &ptr_crouched_rifle_firing_animation_group3,
    &ptr_crouched_rifle_firing_animation_group3,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1,
    &ptr_crouched_rifle_firing_animation_group1
};

struct weapon_firing_animation_table crouched_pistol_firing_animation_group1[] = {
    { PTR_ANIM_fire_kneel_forward_one_handed_weapon_slow, 25.0, 0, 0, 12.0, 132.0, 55.0, 87.0, 67.0, 87.0, 26.0, 111.0, 0.87266463, -0.69813174, 0.61086529, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_kneel_forward_one_handed_weapon_fast, 26.0, 0, 0, 8.0, 89.0, 31.0, 63.0, 41.0, 51.0, 21.0, 80.0, 0.87266463, -0.69813174, 0.34906587, -1.134464, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_pistol_firing_animation_group1 = { &crouched_pistol_firing_animation_group1, -1 };

struct weapon_firing_animation_table crouched_pistol_firing_animation_group2[] = {
    { PTR_ANIM_fire_kneel_left_one_handed_weapon_slow, 47.0, 0, 1.5707964, 7.0, 128.0, 33.0, 86.0,47.0, 74.0, 23.0, 106.0, 0.87266463, -0.52359879, 0.52359879, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_kneel_left_one_handed_weapon_fast, 18.0, 0, 1.5707964, 7.0, 78.0, 28.0, 52.0, 35.0, 45.0, 15.0, 66.0, 0.87266463, -0.087266468, 0.69813174, -0.78539819, 1.5, 1.0 },
    { PTR_ANIM_fire_kneel_left_one_handed_weapon, 20.0, 0, 1.5707964, 13.0, 92.0, 37.0, 67.0, 42.0, 55.0, 25.0, 84.0, 0.87266463, -0.52359879, 0.34906587, -0.69813174, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_pistol_firing_animation_group2 = { &crouched_pistol_firing_animation_group2, -1 };

struct weapon_firing_animation_table crouched_pistol_firing_animation_group3[] = {
    { PTR_ANIM_fire_kneel_right_one_handed_weapon_slow, 28.0, 0, 4.712389, 15.0, 124.0, 38.0, 97.0, 60.0, 84.0, 20.0, 106.0, 0.87266463, -0.69813174, 0.52359879, -0.87266463, 0.0, 0.0 },
    { PTR_ANIM_fire_kneel_right_one_handed_weapon_fast, 23.0, 0, 4.712389, 0, 85.0, 32.0, 38.0, 38.0, 60.0, 14.0, 71.0, 0.87266463, -0.69813174, 0.61086529, -0.95993108, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_pistol_firing_animation_group3 = { &crouched_pistol_firing_animation_group3, -1 };

struct anim_group_info *ptr_crouched_pistol_firing_animation_groups[] = {
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group2,
    &ptr_crouched_pistol_firing_animation_group2,
    &ptr_crouched_pistol_firing_animation_group2,
    &ptr_crouched_pistol_firing_animation_group2,
    &ptr_crouched_pistol_firing_animation_group2,
    &ptr_crouched_pistol_firing_animation_group2,
    &ptr_crouched_pistol_firing_animation_group3,
    &ptr_crouched_pistol_firing_animation_group3,
    &ptr_crouched_pistol_firing_animation_group3,
    &ptr_crouched_pistol_firing_animation_group3,
    &ptr_crouched_pistol_firing_animation_group3,
    &ptr_crouched_pistol_firing_animation_group3,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1,
    &ptr_crouched_pistol_firing_animation_group1
};

struct weapon_firing_animation_table crouched_doubles_firing_animation_group1[] = {
    { PTR_ANIM_fire_kneel_dual_wield, 22.0, 0, 0, 10.0, 111.0, 34.0, 87.0, -1.0, -1.0, 17.0, 104.0, 0.87266463, -0.69813174, 0.61086529, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_kneel_dual_wield_hands_crossed, 25.0, 0, 0, 9.0, 92.0, 33.0, 62.0, -1.0, -1.0, 18.0, 69.0, 0.87266463, -0.69813174, 0.61086529, -0.78539819, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_doubles_firing_animation_group1 = { &crouched_doubles_firing_animation_group1, -1 };

struct weapon_firing_animation_table crouched_doubles_firing_animation_group2[] = {
    { PTR_ANIM_fire_kneel_dual_wield_left, 28.0, 0, 1.5707964, 15.0, 108.0, 34.0, 73.0, -1.0, -1.0, 17.0, 93.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_kneel_dual_wield_hands_crossed_left, 19.0, 0, 1.5707964, 3.0, 95.0, 30.0, 64.0, -1.0, -1.0, 14.0, 71.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 1.5, 1.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_doubles_firing_animation_group2 = { &crouched_doubles_firing_animation_group2, -1 };

struct weapon_firing_animation_table crouched_doubles_firing_animation_group3[] = {
    { PTR_ANIM_fire_kneel_dual_wield_right, 31.0, 0, 4.712389, 14.0, 111.0, 40.0, 83.0,-1.0, -1.0, 21.0, 94.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 0.0, 0.0 },
    { PTR_ANIM_fire_kneel_dual_wield_hands_crossed_right, 26.0, 0, 4.712389, 7.0, 89.0, 34.0, 60.0, -1.0, -1.0, 20.0, 68.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 0.0, 0.0 },
    {0, 0.0, 0, 0, 0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct anim_group_info ptr_crouched_doubles_firing_animation_group3 = { &crouched_doubles_firing_animation_group3, -1 };

struct anim_group_info* ptr_crouched_doubles_firing_animation_groups[] = {
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group2,
    &ptr_crouched_doubles_firing_animation_group2,
    &ptr_crouched_doubles_firing_animation_group2,
    &ptr_crouched_doubles_firing_animation_group2,
    &ptr_crouched_doubles_firing_animation_group2,
    &ptr_crouched_doubles_firing_animation_group2,
    &ptr_crouched_doubles_firing_animation_group3,
    &ptr_crouched_doubles_firing_animation_group3,
    &ptr_crouched_doubles_firing_animation_group3,
    &ptr_crouched_doubles_firing_animation_group3,
    &ptr_crouched_doubles_firing_animation_group3,
    &ptr_crouched_doubles_firing_animation_group3,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1,
    &ptr_crouched_doubles_firing_animation_group1
};

struct weapon_firing_animation_table D_80030078[] = {
    // sizeof(struct weapon_firing_animation_table) = 0x48 = 72.
    // address 0xD_80030078. Index 0, = D_80030078 + 0.
    { PTR_ANIM_fire_roll_left, 76.0, 0.0, 0.0, 20.0, -1.0, 98.0, 161.0, -1.0, -1.0, 93.0, 161.0, 0.87266463, -0.52359879, 0.69813174, -0.69813174, 1.7, 2.0 },

    // address 0x800300C0. Index 1, = D_80030078 + 72.
    { PTR_ANIM_fire_roll_right1, 58.0, 0.0, 0.0, 10.0, -1.0, 77.0, 104.0, -1.0, -1.0, 72.0, 104.0, 0.87266463, -0.34906587, 0.61086529, -0.69813174, 1.55, 1.5 },

    // address 0x80030108. Index 2, = D_80030078 + 144.
    { PTR_ANIM_fire_roll_left_fast, 61.0, 0.0, 0.0, 10.0, -1.0, 83.0, 128.0, -1.0, -1.0, 78.0, 128.0, 0.87266463, -0.52359879, 0.87266463, -0.52359879, 1.2, 1.3 },

    // address 0x80030150. Index 3, = D_80030078 + 216.
    { PTR_ANIM_fire_roll_right2, 63.0, 0.0, 0.0, 10.0, -1.0, 73.0, 114.0, -1.0, -1.0, 68.0, 114.0, 0.87266463, -0.52359879, 0.61086529, -0.61086529, 1.65, 1.5 },

    // address 0x80030198. Index 4, = D_80030078 + 288.
    { PTR_ANIM_fire_roll_left, 76.0, 0.0, 0.0, 20.0, 76.0, 98.0, 161.0, -1.0, -1.0, 93.0, 161.0, 0.87266463, -0.52359879, 0.69813174, -0.69813174, 1.7, 2.0 },

    // address 0x800301E0. Index 5, = D_80030078 + 360.
    { PTR_ANIM_fire_roll_right1, 58.0, 0.0, 0.0, 10.0, 63.0, 77.0, 104.0, -1.0, -1.0, 72.0, 104.0, 0.87266463, -0.34906587, 0.61086529, -0.69813174, 1.55, 1.5 },

    // address 0x80030228. Index 6, = D_80030078 + 432.
    { PTR_ANIM_fire_roll_left_fast, 61.0, 0.0, 0.0, 10.0, 56.0, 83.0, 128.0, -1.0, -1.0, 78.0, 128.0, 0.87266463, -0.52359879, 0.87266463, -0.52359879, 1.2, 1.3 },

    // address 0x80030270. Index 7, = D_80030078 + 504.
    { PTR_ANIM_fire_roll_right2, 63.0, 0.0, 0.0, 10.0, 50.0, 73.0, 114.0, -1.0, -1.0, 68.0, 114.0, 0.87266463, -0.52359879, 0.61086529, -0.61086529, 1.65, 1.5 },

    // address 0x800302B8. Index 8, = D_80030078 + 576.
    { PTR_ANIM_fire_hip_one_handed_weapon_slow, 0.0, 0.0, 0.0, 7.0, 64.0, 19.0, 51.0, 24.0, 46.0, 14.0, 58.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 0.0, 0.0 },

    // address 0x80030300. Index 9, = D_80030078 + 648.
    { PTR_ANIM_fire_standing_left_one_handed_weapon_fast, 0.0, 0.0, 1.5707964, 14.0, 76.0, 26.0, 31.0, 31.0, 38.0, 15.0, 49.0, 0.87266463, -0.69813174, 0.52359879, -1.0471976, 0.0, 0.0 },

    // address 0x80030348. Index 10, = D_80030078 + 720.
    { PTR_ANIM_fire_kneel_forward_one_handed_weapon_fast, 26.0, 0.0, 0.0, 25.0, 89.0, 41.0, 63.0, 41.0, 51.0, 21.0, 80.0, 0.87266463, -0.69813174, 0.34906587, -1.134464, 0.0, 0.0 },

    // address 0x80030390. Index 11, = D_80030078 + 792.
    { PTR_ANIM_fire_kneel_left_one_handed_weapon_fast, 18.0, 0.0, 1.5707964, 11.0, 78.0, 33.0, 52.0, 35.0, 45.0, 15.0, 66.0, 0.87266463, -0.087266468, 0.69813174, -0.78539819, 1.5, 1.0 },

    // address 0x800303D8. Index 12, = D_80030078 + 864.
    { PTR_ANIM_fire_standing_dual_wield, 26.0, 0.0, 0.0, 7.0, 92.0, 28.0, 68.0, -1.0, -1.0, 11.0, 73.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 0.0, 0.0 },

    // address 0x80030420. Index 13, = D_80030078 + 936.
    { PTR_ANIM_fire_standing_dual_wield_left, 26.0, 0.0, 1.5707964, 9.0, 112.0, 38.0, 87.0, -1.0, -1.0, 19.0, 98.0, 0.87266463, -0.69813174, 0.43633232, -0.43633232, 0.0, 0.0 },

    // address 0x80030468. Index 14, = D_80030078 + 1008.
    { PTR_ANIM_fire_kneel_dual_wield, 22.0, 0.0, 0.0, 10.0, 111.0, 34.0, 87.0, -1.0, -1.0, 17.0, 104.0, 0.87266463, -0.69813174, 0.61086529, -0.78539819, 0.0, 0.0 },

    // address 0x800304B0. Index 15, = D_80030078 + 1080.
    { PTR_ANIM_fire_kneel_dual_wield_left, 28.0, 0.0, 1.5707964, 15.0, 108.0, 34.0, 73.0, -1.0, -1.0, 17.0, 93.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 0.0, 0.0 },

    // address 0x800304F8. Index 16, = D_80030078 + 1152.
    { PTR_ANIM_fire_standing_dual_wield, 26.0, 0.0, 0.0, 7.0, 92.0, 28.0, 68.0, -1.0, -1.0, 11.0, 73.0, 0.87266463, -0.69813174, 0.69813174, -0.69813174, 0.0, 0.0 },

    // address 0x80030540. Index 17, = D_80030078 + 1224.
    { PTR_ANIM_fire_standing_dual_wield_hands_crossed_left, 25.0, 0.0, 1.5707964, 10.0, 112.0, 32.0, 86.0, -1.0, -1.0, 19.0, 97.0, 0.87266463, -0.69813174, 0.43633232, -0.43633232, 0.0, 0.0 },

    // address 0x80030588. Index 18, = D_80030078 + 1296.
    { PTR_ANIM_fire_kneel_dual_wield_hands_crossed, 25.0, 0.0, 0.0, 9.0, 92.0, 33.0, 62.0, -1.0, -1.0, 18.0, 69.0, 0.87266463, -0.69813174, 0.61086529, -0.78539819, 0.0, 0.0 },

    // address 0x800305D0. Index 19, = D_80030078 + 1368.
    { PTR_ANIM_fire_kneel_dual_wield_hands_crossed_left, 19.0, 0.0, 1.5707964, 3.0, 95.0, 30.0, 64.0, -1.0, -1.0, 14.0, 71.0, 0.87266463, -0.69813174, 0.52359879, -0.78539819, 1.5, 1.0 },

    // address 0x80030618. Index 20, = D_80030078 + 1440.
    {0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

struct weapon_firing_animation_table D_80030660[] = {
    // address 0x80030660. Index 0, = D_80030660 + 0.
    { PTR_ANIM_fire_walking, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 1.4, 1.3 },

    // address 0x800306A8. Index 1, = D_80030660 + 72.
    { PTR_ANIM_fire_running, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 1.1, 1.2 },

    // address 0x800306F0. Index 2, = D_80030660 + 144.
    { PTR_ANIM_aim_walking_one_handed_weapon, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x80030738. Index 3, = D_80030660 + 216.
    { PTR_ANIM_aim_running_one_handed_weapon, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x80030780. Index 4, = D_80030660 + 288.
    { PTR_ANIM_fire_walking_dual_wield, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x800307C8. Index 5, = D_80030660 + 360.
    { PTR_ANIM_fire_running_dual_wield, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x80030810. Index 6, = D_80030660 + 432.
    { PTR_ANIM_fire_walking_dual_wield_hands_crossed, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x80030858. Index 7, = D_80030660 + 504.
    { PTR_ANIM_fire_running_dual_wield_hands_crossed, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x800308A0. Index 8, = D_80030660 + 576.
    { PTR_ANIM_aim_running_left_one_handed_weapon, 0.0, 0.0, 1.5707964, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x800308E8. Index 9, = D_80030660 + 648.
    { PTR_ANIM_aim_running_right_one_handed_weapon, 0.0, 0.0, 4.712389, 0.0, -1.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.87266463, -0.52359879, 0.52359879, -0.52359879, 0.0, 0.0 },

    // address 0x80030930. Index 10, = D_80030660 + 720.
    {0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
};

s32 objectiveregisters1 = 0;

/**
 * List of characters updated every tick.
 *
 * Address 0x8003097C.
*/
ChrRecord* g_ActiveChrs = 0;

/**
 * Number of items in g_ActiveChrs.
 *
 * Adress 0x0x80030980.
*/
s32 g_ActiveChrsCount = 0;

/**
 * Default factor in chrlvModelScaleAnimationRelated.
 * Address 0x80030984.
*/
f32 D_80030984 = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by ANIM_DATA_running.
 * Address 0x80030988.
*/
f32 D_80030988 = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by ANIM_DATA_sprinting.
 * Address 0x8003098C.
*/
f32 D_8003098C = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by ANIM_DATA_walking_unarmed.
 * Address 0x80030990.
*/
f32 D_80030990 = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by ANIM_DATA_running_one_handed_weapon.
 * Address 0x80030994.
*/
f32 D_80030994 = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by first ANIM_DATA_sprinting_one_handed_weapon.
 * Address 0x80030998.
*/
f32 D_80030998 = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by ANIM_DATA_walking_female.
 * Address 0x8003099C.
*/
f32 D_8003099C = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by ANIM_DATA_running_female.
 * Address 0x800309A0.
*/
f32 D_800309A0 = 0;

/**
 * Scale factor in chrlvModelScaleAnimationRelated, used by duplicate ANIM_DATA_sprinting_one_handed_weapon.
 * Address 0x800309A4.
*/
f32 D_800309A4 = 0;

point2d D_800309A8 = {0, 0};
//s32 D_800309AC = 0;
point2d D_800309B0 = {0, 0};
//s32 D_800309B4 = 0;

/**
 * Default firing state, left and right hand.
 * Address 0x800309B8.
*/
point2d D_800309B8 = {0, 0};

/**
 * Default firing state when crouched, left and right hand.
 * Address 0x800309C0.
*/
point2d D_800309C0 = {0, 0};

point2d D_800309C8 = {0, 0};
point2d D_800309D0 = {0, 0};
point2d D_800309D8 = {0, 0};

point2d D_800309E0 = {0, 0};

point2d D_800309E8 = {0, 0};


s32 get_numguards(void)
{
  return g_NumChrSlots;
}


Vertex *get_ptr_allocated_block_for_vertices(s32 param_1)
{
  return dynAllocate(param_1 * sizeof(Vertex));
}


void set_show_patrols_flag(s32 flag)
{
  show_patrols_flag = flag;
}


s32 get_show_patrols_flag(void)
{
  return show_patrols_flag;
}


/**
 * Unreferenced.
 *
 * Address 0x7F01F574.
 */
f32 chrUnusedYPositionRelated(PropRecord *arg0)
{
    if (arg0->stan != 0)
    {
        if (sub_GAME_7F0B20D0(&arg0->stan, arg0->pos.x, arg0->pos.z, 1.0f) < 0)
        {
            return stanGetPositionYValue(arg0->stan, arg0->pos.x, arg0->pos.z);
        }

        return 0.0f;
    }

    return 0.0f;
}


void chrSetMoving(ChrRecord *self, bool unset)
{
    if (unset)
    {
        self->hidden &= ~CHRHIDDEN_MOVING;
    }
    else
    {
        self->hidden |= CHRHIDDEN_MOVING;
    }
    return;
}


StandTile *sub_GAME_7F01F614(ChrRecord *guard, StandTile *stan, coord3d *src, coord3d *dst, s32 updateLastMoveOk)
{
    StandTile *ret;
    StandTile *tile;
    f32 height;
    f32 always_20;
    f32 width;
    coord3d edgeA;
    coord3d edgeB;
    coord3d delta;
    coord3d dir;
    f32 tmp;
    s32 hasprojection;
    coord3d newpos;

    ret = NULL;
    tile = stan;

    chrGetChrWidthHeight(guard->prop, &width, &height, &always_20);
    chrSetMoving(guard, 0);
    stanResetHits();

    if (stanTestLineUnobstructed(&tile, src->x, src->z, dst->x, dst->z, 0x1f, height, always_20, 0.0f, 1.0f))
    {
        if (stanTestVolume(&tile, dst->x, dst->z, width, 0x1f, height, always_20) < 0)
        {
            if (updateLastMoveOk)
            {
                if (&newpos);
                guard->invalidmove = 0;
                guard->lastmoveok60 = g_GlobalTimer;
            }

            ret = tile;
            goto done;
        }
    }

    hasprojection = 0;

    if (getCollisionEdge_maybe(&edgeA, &edgeB))
    {
        delta.x = dst->x - src->x;
        delta.z = dst->z - src->z;

        if ((edgeA.x != edgeB.x) || (edgeA.z != edgeB.z))
        {
            hasprojection = 1;

            dir.x = edgeB.x - edgeA.x;
            dir.z = edgeB.z - edgeA.z;

            tmp = 1.0f / sqrtf((dir.z * dir.z) + (dir.x * dir.x));

            dir.x *= tmp;
            dir.z *= tmp;

            tmp = (dir.z * delta.z) + (delta.x * dir.x);

            newpos.x = (dir.x * tmp) + src->x;
            newpos.z = (dir.z * tmp) + src->z;

            if (&dir);

            tile = stan;
        }

        if (hasprojection)
        {
            if (stanTestLineUnobstructed(&tile, src->x, src->z, newpos.x, newpos.z, 0x1f, height, always_20, 0.0f, 1.0f))
            {
                if (stanTestVolume(&tile, newpos.x, newpos.z, width, 0x1f, height, always_20) < 0)
                {
                    dst->x = newpos.x;
                    dst->z = newpos.z;
                    guard->invalidmove = 2;
                    ret = tile;
                    goto done;
                }
            }
        }

        tmp = width * width;

        dir.x = edgeA.x - dst->x;
        dir.z = edgeA.z - dst->z;

        if (((dir.z * dir.z) + (dir.x * dir.x)) <= tmp)
        {
            if ((edgeA.x == src->x) && (edgeA.z == src->z))
            {
                goto done;
            }

            dir.x = -(edgeA.z - src->z);
            dir.z = edgeA.x - src->x;

            tmp = 1.0f / sqrtf((dir.z * dir.z) + (dir.x * dir.x));

            dir.x *= tmp;
            dir.z *= tmp;

            tmp = (dir.z * delta.z) + (delta.x * dir.x);

            newpos.x = (dir.x * tmp) + src->x;
            newpos.z = (dir.z * tmp) + src->z;

            tile = stan;

            if (stanTestLineUnobstructed(&tile, src->x, src->z, newpos.x, newpos.z, 0x1f, height, always_20, 0.0f, 1.0f))
            {
                if (stanTestVolume(&tile, newpos.x, newpos.z, width, 0x1f, height, always_20) < 0)
                {
                    dst->x = newpos.x;
                    dst->z = newpos.z;
                    guard->invalidmove = 2;
                    ret = tile;
                    goto done;
                }
            }
        }
        else
        {
            dir.x = edgeB.x - dst->x;
            dir.z = edgeB.z - dst->z;

            if (((dir.z * dir.z) + (dir.x * dir.x)) <= tmp)
            {
                if ((edgeB.x == src->x) && (edgeB.z == src->z))
                {
                    goto done;
                }

                dir.x = -(edgeB.z - src->z);
                dir.z = edgeB.x - src->x;

                tmp = 1.0f / sqrtf((dir.z * dir.z) + (dir.x * dir.x));

                dir.x *= tmp;
                dir.z *= tmp;

                tmp = (dir.z * delta.z) + (delta.x * dir.x);

                newpos.x = (dir.x * tmp) + src->x;
                newpos.z = (dir.z * tmp) + src->z;

                tile = stan;

                if (stanTestLineUnobstructed(&tile, src->x, src->z, newpos.x, newpos.z, 0x1f, height, always_20, 0.0f, 1.0f))
                {
                    if (stanTestVolume(&tile, newpos.x, newpos.z, width, 0x1f, height, always_20) < 0)
                    {
                        dst->x = newpos.x;
                        dst->z = newpos.z;
                        guard->invalidmove = 2;
                        ret = tile;
                    }
                }
            }
        }
    }

done:
    chrSetMoving(guard, 1);

    if (ret == NULL)
    {
        guard->invalidmove = 1;
    }

    return ret;
}


s32 sub_GAME_7F01FC10(Model *model, coord3d *src, coord3d *dst, f32 *ground_y)
{
    ChrRecord *chr;
    s32 moved;
    f32 ground;
    coord3d *groundpos;
    f32 tmp;
    s32 i;
    StandTile *tile;
    union ModelRwData *rwdata;

    chr = model->chr;
    moved = 0;
    ground = 0.0f;
    groundpos = src;

    if (chr->prop->stan != NULL)
    {
        if ((chr->actiontype == ACT_DIE) && (chr->act_die.timeextra > ground))
        {
#ifdef BUGFIX_R1
            tmp = ((model->playspeed * g_JP_GlobalTimerDelta) * (chr->act_die.timeextra - chr->act_die.elapseextra)) / chr->act_die.timeextra;
#else
            tmp = ((model->playspeed * g_GlobalTimerDelta) * (chr->act_die.timeextra - chr->act_die.elapseextra)) / chr->act_die.timeextra;
#endif

            dst->x += chr->act_die.extraspeed.x * tmp;
            dst->y += chr->act_die.extraspeed.y * tmp;
            dst->z += chr->act_die.extraspeed.z * tmp;

#ifdef BUGFIX_R1
            chr->act_die.elapseextra += g_JP_GlobalTimerDelta * model->playspeed;
#else
            chr->act_die.elapseextra += g_GlobalTimerDelta * model->playspeed;
#endif

            if (chr->act_die.timeextra < chr->act_die.elapseextra)
            {
                chr->act_die.timeextra = 0.0f;
            }
        }

        dst->x += chr->fallspeed.x * g_GlobalTimerDelta;
        dst->z += chr->fallspeed.z * g_GlobalTimerDelta;

        tile = sub_GAME_7F01F614(chr, chr->prop->stan, src, dst, 1);

        if (tile != NULL)
        {
            chr->prop->stan = tile;
            groundpos = dst;
            moved = 1;
        }

        if (!(chr->chrflags & CHRFLAG_LOCK_Y_POS))
        {
            ground = stanGetPositionYValue(chr->prop->stan, groundpos->x, groundpos->z);
            chr->ground = ground;

            if (chr->chrflags & CHRFLAG_INIT)
            {
                rwdata = modelGetNodeRwData(model, model->obj->RootNode);

                chr->chrflags &= ~1;
                chr->manground = chr->ground;
                chr->sumground = chr->ground / GROUND_SMOOTH_FACTOR;

                rwdata->Header.unk34.y = rwdata->Header.unk24.y;
            }
            else
            {
                if ((chr->fallspeed.y != 0.0f) || (chr->manground > chr->ground))
                {
                    sub_GAME_7F057D44(&chr->manground, &chr->fallspeed.y, g_GlobalTimerDelta);

                    if (chr->manground <= chr->ground)
                    {
                        chr->manground = chr->ground;
                        chr->sumground = chr->ground / GROUND_SMOOTH_FACTOR;
                        chr->fallspeed.y = 0.0f;
                    }
                }

                if (chr->manground <= chr->ground)
                {
                    i = 0;

#ifndef TARGET_N64
                    if (portSmoothOn())
                    {
                        /* ground-height follow advanced by the true delta,
                         * so a guard's Y tracks as smoothly as its XZ now
                         * that its animation ticks fractionally */
                        f32 dt = portAnimTicks();

                        tmp = FALLSPEED_DECAY;
                        portLagStep(&chr->sumground, tmp, chr->ground, dt);
                        portDecayStep(&chr->fallspeed.x, tmp, dt);
                        portDecayStep(&chr->fallspeed.z, tmp, dt);
                    }
                    else
#endif
                    if (g_ClockTimer > 0)
                    {
                        tmp = FALLSPEED_DECAY;

                        do
                        {
                            chr->sumground = (chr->sumground * tmp) + chr->ground;
                            chr->fallspeed.x *= tmp;
                            chr->fallspeed.z *= tmp;
                            i++;
                        }
                        while (i < g_ClockTimer);
                    }

                    tmp = 0.1f;
                    chr->manground = chr->sumground * GROUND_SMOOTH_FACTOR;

                    if (chr->fallspeed.x < tmp)
                    {
                        if (-0.1f < chr->fallspeed.x)
                        {
                            if (chr->fallspeed.z < tmp)
                            {
                                if (-0.1f < chr->fallspeed.z)
                                {
                                    chr->fallspeed.z = 0.0f;
                                    chr->fallspeed.x = 0.0f;
                                }
                            }
                        }
                    }
                }
            }

            dst->y += chr->manground - ground;
        }
        else
        {
            ground = chr->ground;
        }
    }

    *ground_y = ground;

    if (!moved)
    {
        dst->x = src->x;
        dst->z = src->z;
    }

    return 1;
}

#undef GROUND_SMOOTH_FACTOR
#undef FALLSPEED_DECAY


s32 chrGetNumFree(void)
{
    s32 count = 0;
    s32 i;

    for (i = 0; i < g_NumChrSlots; i++)
    {
        if (g_ChrSlots[i].model == 0)
        {
            count++;
        }
    }

    return count;
}


void chrSetMaxDamage(ChrRecord *chr, f32 maxdamage)
{
    chr->maxdamage = (get_007_health_mod() * maxdamage);
}


f32 chrGetMaxDamage(ChrRecord *chr)
{
    return chr->maxdamage;
}


void chrAddHealth(ChrRecord *chr, f32 health)
{
    chr->damage -= (health * get_007_health_mod());
}


f32 chrGetArmor(ChrRecord *chr)
{
    if (chr->damage < 0)
    {
        return -chr->damage;
    }

    return 0;
}


PropRecord *init_GUARDdata_with_set_values(PropRecord *arg0, Model *arg1, struct coord3d *arg2, f32 arg3, StandTile *arg4, AIRecord *arg5)
{
    ChrRecord *var_s0;
    s32 var_v0;

    var_s0 = NULL;
    var_v0 = 0;

    for (var_v0 = 0; var_v0 < g_NumChrSlots; var_v0++)
    {
        if (g_ChrSlots[var_v0].model == NULL)
        {
            var_s0 = &g_ChrSlots[var_v0];
            break;
        }
    }

    arg0->type = PROP_TYPE_CHR;
    arg0->chr = var_s0;
    arg0->pos.f[0] = arg2->f[0];
    arg0->pos.f[1] = arg2->f[1];
    arg0->pos.f[2] = arg2->f[2];
    arg0->stan = arg4;

    sub_GAME_7F06FF5C(arg1, (intptr_t) sub_GAME_7F01FC10);

    arg1->unk00 = 0xA;
    arg1->chr = var_s0;

    setsuboffset(arg1, arg2);
    setsubroty(arg1, arg3);

    #if defined(VERSION_EU)
    modelSetAnimPlaySpeed(arg1, animation_rate * 1.2f, 0.0f);
    #else
    modelSetAnimPlaySpeed(arg1, animation_rate, 0.0f);
    #endif

    var_s0->chrnum = (s16) player1_guardID;
    player1_guardID += 1;
    var_s0->headnum = 0;
    var_s0->bodynum = 0;
    var_s0->prop = arg0;
    var_s0->model = arg1;
    var_s0->field_20 = NULL;
    var_s0->numarghs = 0;
    var_s0->lastwalk60 = 0;
    var_s0->invalidmove = 0;
    var_s0->lastmoveok60 = g_GlobalTimer;
    var_s0->lastseetarget60 = 0;
    var_s0->lastknowntargetpos.f[0] = 0.0f;
    var_s0->lastknowntargetpos.f[1] = 0.0f;
    var_s0->lastknowntargetpos.f[2] = 0.0f;
    var_s0->targetTile = NULL;
    var_s0->seen_bond_time = 0;
    var_s0->lastheartarget60 = 0;
    var_s0->numclosearghs = 0;
    var_s0->shotbondsum = 0.0f;
    var_s0->damage = 0.0f;
    var_s0->visionrange = 250.0f;
    var_s0->hearingscale = 1.0f;

    var_s0->maxdamage = get_007_health_mod() * 4.0f;
    set_color_shading_from_tile(arg0, &var_s0->nextcol);

    var_s0->shadecol.rgba[0] = var_s0->nextcol.rgba[0];
    var_s0->shadecol.rgba[1] = var_s0->nextcol.rgba[1];
    var_s0->shadecol.rgba[2] = var_s0->nextcol.rgba[2];
    var_s0->shadecol.rgba[3] = var_s0->nextcol.rgba[3];
    var_s0->fadealpha = 0xFF;
    var_s0->field_160[0].ptr_SEbuffer1 = NULL;
    var_s0->field_160[0].ptr_SEbuffer2 = NULL;
    var_s0->field_160[1].ptr_SEbuffer1 = NULL;
    var_s0->field_160[1].ptr_SEbuffer2 = NULL;
    var_s0->field_178[0] = 0;
    var_s0->field_178[1] = 0;
    var_s0->chrflags = CHRFLAG_INIT;
    var_s0->hidden = CHRHIDDEN_NONE;
    var_s0->sumground = 0.0f;
    var_s0->manground = 0.0f;
    var_s0->ground = 0.0f;
    var_s0->fallspeed.f[0] = 0.0f;
    var_s0->fallspeed.f[1] = 0.0f;
    var_s0->fallspeed.f[2] = 0.0f;
    var_s0->prevpos.f[0] = arg2->f[0];
    var_s0->prevpos.f[1] = arg2->f[1];
    var_s0->prevpos.f[2] = arg2->f[2];
    var_s0->actiontype = 0;
    var_s0->sleep = 0;
    var_s0->ailist = (AIRecord *) arg5;
    var_s0->aioffset = 0;
    var_s0->aireturnlist = -1;
    var_s0->morale = 0;
    var_s0->alertness = 0;
    var_s0->flags2 = 0;
    var_s0->random = 0;
    var_s0->timer60 = 0;
    var_s0->padpreset1 = -1;
    var_s0->chrseeshot = -1;
    var_s0->chrseedie = -1;
    var_s0->chrpreset1 = -1;
    var_s0->beams[0].unk00 = -1;
    var_s0->beams[1].unk00 = -1;
    var_s0->firecount[0] = 0;
    var_s0->firecount[1] = 0;
    var_s0->grenadeprob = 0;
    var_s0->accuracyrating = 0;
    var_s0->speedrating = 0;
    var_s0->arghrating = 0;
    var_s0->flinchcnt = -1;
    var_s0->aimuplshoulder = 0.0f;
    var_s0->aimuprshoulder = 0.0f;
    var_s0->aimupback = 0.0f;
    var_s0->aimsideback = 0.0f;
    var_s0->aimendlshoulder = 0.0f;
    var_s0->aimendrshoulder = 0.0f;
    var_s0->aimendback = 0.0f;
    var_s0->aimendsideback = 0.0f;
    var_s0->aimendcount = 0;
    var_s0->weapons_held[0] = NULL;
    var_s0->weapons_held[1] = NULL;
    var_s0->handle_positiondata_hat = NULL;
    var_s0->chrwidth = 20.0f;
    var_s0->chrheight = 185.0f;

    sub_GAME_7F01FC10(arg1, &arg0->pos, &arg0->pos, &var_s0->ground);
    chrDetectRooms(var_s0);

    return arg0;
}


/**
 * Address 0x7F0203B8.
 */
PropRecord * chrAllocate( Model * arg0, coord3d * arg1, f32 arg2,  StandTile * arg3, AIRecord *arg4)
{
    PropRecord * ret;
    s32 phi_a0;

    ret = chrpropAllocate();

    if (ret != 0)
    {
        ret = init_GUARDdata_with_set_values(ret, arg0, arg1, arg2, arg3, arg4);
    }

    return ret;
}


/**
 * Address: 7F020414.
 */
void chrpropCleanupForRemoval(PropRecord *prop)
{
    ChrRecord *chr;
    Model *model;
    PropRecord *child;
    PropRecord *prev;
    ObjectRecord *obj;

    chr = prop->chr;
    model = chr->model;

    if (chr->field_160[0].ptr_SEbuffer1 != NULL && sndGetPlayingState(chr->field_160[0].ptr_SEbuffer1) != 0) {
        sndDeactivate(chr->field_160[0].ptr_SEbuffer1);
    }
    if (chr->field_160[0].ptr_SEbuffer2 != NULL && sndGetPlayingState(chr->field_160[0].ptr_SEbuffer2) != 0) {
        sndDeactivate(chr->field_160[0].ptr_SEbuffer2);
    }
    if (chr->field_160[1].ptr_SEbuffer1 != NULL && sndGetPlayingState(chr->field_160[1].ptr_SEbuffer1) != 0) {
        sndDeactivate(chr->field_160[1].ptr_SEbuffer1);
    }
    if (chr->field_160[1].ptr_SEbuffer2 != NULL && sndGetPlayingState(chr->field_160[1].ptr_SEbuffer2) != 0) {
        sndDeactivate(chr->field_160[1].ptr_SEbuffer2);
    }

    sub_GAME_7F050DE8(model);
    chrpropDeregisterRooms(prop);

    child = prop->child;

    if (child != NULL) {
        do {
            obj = (ObjectRecord *)child->obj;
            prev = child->prev;
            objDetach(child);
            objFreePermanently(obj, TRUE);
            child = prev;
        } while (child != NULL);
    }

    clear_aircraft_model_obj(model);

    chr->model = NULL;
    chr->chrnum = -1;

    if (chr->field_20 != NULL) {
        sub_GAME_7F06B248(chr->field_20);
    }
}


/**
 * Address 0x7F020540 (VERSION_US, VERSION_JP).
 * Address 0x7F0203B4 (VERSION_EU).
 */
void setAnimationRate(f32 arg0)
{
    s32 i;

    animation_rate = arg0;

    for (i=0; i<g_NumChrSlots; i++)
    {
        if (g_ChrSlots[i].model != NULL)
        {
#if defined(REFRESH_PAL)
/* should reference D_80047E4C (1.2f) */
            modelSetAnimPlaySpeed(g_ChrSlots[i].model, animation_rate * 1.2f, 600.0f);
#else
            modelSetAnimPlaySpeed(g_ChrSlots[i].model, animation_rate, 600.0f);
#endif
        }
    }
}


f32 getAnimationRate(void)
{
  return animation_rate;
}


/**
 * Address 0x7F0205F0 (all versions).
 */
void chrUpdateAimProperties( ChrRecord *self)
{
    f32 mult;

    if (self->aimendcount >= 2)
    {
#if defined(BUGFIX_R1)
        mult = g_JP_GlobalTimerDelta / (f32) self->aimendcount;
#else
        mult = g_GlobalTimerDelta / (f32) self->aimendcount;
#endif

        if (mult > 1.0f)
        {
            mult = 1.0f;
        }

        self->aimuplshoulder += ((self->aimendlshoulder - self->aimuplshoulder) * mult);
        self->aimuprshoulder += ((self->aimendrshoulder - self->aimuprshoulder) * mult);
        self->aimupback += ((self->aimendback - self->aimupback) * mult);
        self->aimsideback += ((self->aimendsideback - self->aimsideback) * mult);
        self->aimendcount -= g_ClockTimer;

        return;
    }

    self->aimuplshoulder = self->aimendlshoulder;
    self->aimuprshoulder = self->aimendrshoulder;
    self->aimupback = self->aimendback;
    self->aimsideback = self->aimendsideback;
}


/**
 * Address 0x7F0206D4.
 */
void chrSetHiddenToRandom(ChrRecord *self)
{
    ChrRecord *temp_a0;
    u32 rand;

    if ((s32) self->flinchcnt < 0)
    {
        self->flinchcnt = 1;
        self->hidden &= ~CHRHIDDEN_RAND_FLINCH_MASK;

        // roll for bits 12,13.
        // rand -> value
        // 2 => nothing
        // 0 => set bit 12
        // 1 => set bit 13
        rand = randomGetNext() % 3U;

        if (rand == 0)
        {
            self->hidden |= CHRHIDDEN_RAND_FLINCH_1;
        }
        else if (rand == 1)
        {
            self->hidden |= CHRHIDDEN_RAND_FLINCH_2;
        }

        // roll for bits 14,15.
        // rand -> value
        // 2 => nothing
        // 0 => set bit 14
        // 1 => set bit 15
        rand = randomGetNext() % 3U;

        if (rand == 0)
        {
            self->hidden |= CHRHIDDEN_RAND_FLINCH_4;
        }
        else if (rand == 1)
        {
            self->hidden |= CHRHIDDEN_RAND_FLINCH_8;
        }
    }
}


/**
 * Address: 7F020794
 *
 * Flinch animation envelope. Maps a character's flinchcnt timer to a normalized
 * intensity that goes 0->1 then falls 1->0, used to drive the flinch animation.
 *
 * Flinches happen when a character has body armor or has the CHRFLAG_INVINCIBLE flag and is shot in the arm or torso.
 *
 * Rise and fall are measured in VI retraces: 1/60 s on NTSC and 1/50 s on PAL.
 *
 */
f32 chrGetFlinchAmount(ChrRecord *chr)
{
    f32 temp_f2;
    f32 phi_f2;
#if defined(LEFTOVERDEBUG)
    f32 rise = 10.0f;
    f32 fall = 20.0f;
#else
    f32 rise = 8.0f;
    f32 fall = 16.0f;
#endif

    phi_f2 = chr->flinchcnt;
    temp_f2 = (f32) phi_f2;

    if (temp_f2 < rise)
    {
        phi_f2 = sinf(((temp_f2 * M_TAU_F) * 0.25f) / rise);
    }
    else
    {
        phi_f2 = 1.0f - sinf((((temp_f2 - rise) * M_TAU_F) * 0.25f) / fall);
    }

    return phi_f2;
}


#ifdef BUGFIX_R1
bool chrCanUseDKModeScaling(s32 bodynum, s32 headnum)
{
    if (j_text_trigger == FALSE)
    {
        return TRUE;
    }

    if ((bodynum != BODY_Boris) &&
        (bodynum != BODY_Ourumov) &&
        (bodynum != BODY_Trevelyan_Janus) &&
        (bodynum != BODY_Trevelyan_006) &&
        (bodynum != BODY_Valentin_) &&
        (bodynum != BODY_Xenia) &&
        (bodynum != BODY_Baron_Samedi) &&
        (bodynum != BODY_Jaws) &&
        (bodynum != BODY_Mayday) &&
        (bodynum != BODY_Oddjob) &&
        (bodynum != BODY_Natalya_Skirt) &&
        (bodynum != BODY_Natalya_Jungle_Fatigues) &&

        (headnum != BODY_Male_Pierce_Bond_1) &&
        (headnum != BODY_Male_Pierce_Bond_2) &&
        (headnum != BODY_Male_Pierce_Bond_3) &&
        (headnum != BODY_Male_Pierce_Bond_Parka) &&
        (headnum != BODY_Male_Pierce_Bond_Tuxedo) &&
        (headnum != BODY_Male_Mishkin))
    {
        return TRUE;
    }

    return FALSE;
}
#endif


#define PI_OVER_3 1.0471976f
#define FIVEPI_OVER_18 0.87266463f


/**
 * Address: 7F02083C
 */
void chrHandleJointPositioned(enum CHR_RENDER_PART bodypart, Mtxf *matrix)
{
    f32 scale;
    f32 xrot;
    f32 yrot;
    f32 zrot;
    f32 tmp;
    f32 amount;
    f32 savedposz;
    f32 savedposy;
    f32 savedposx;
    f32 sideback;
    Mtxf rotmtx;
    u16 hidden;
    ChrRecord *chr;

    scale = 1.0f;

    if (cheatIsActive(CHEAT_DK_MODE))
    {
#ifdef BUGFIX_R1
        chr = g_CurModelChr;
        if (chrCanUseDKModeScaling(chr->bodynum, chr->headnum))
#endif
        {
            if (bodypart == CHR_RENDERPART_HEAD)
            {
                scale = 4.0f;
            }
            else if ((bodypart == CHR_RENDERPART_LEFT_ARM) || (bodypart == CHR_RENDERPART_RIGHT_ARM))
            {
#ifdef BUGFIX_R1
                if (!(g_CurModelChr->chrflags & CHRFLAG_08000000))
#endif
                {
                    scale = 2.5f;
                }
            }
        }
    }

    if ((((bodypart != CHR_RENDERPART_LEFT_ARM) && (bodypart != CHR_RENDERPART_RIGHT_ARM)) && (bodypart != CHR_RENDERPART_TORSO)) && (bodypart != CHR_RENDERPART_HEAD))
    {
        return;
    }

    zrot = (yrot = (xrot = 0.0f));

#ifdef BUGFIX_R1
    //Use local chr earlier - although its just a pointer so...makes no difference?
    chr = g_CurModelChr;
    #define g_CurModelChr chr
#endif
    if (bodypart == CHR_RENDERPART_RIGHT_ARM)
    {
        xrot = g_CurModelChr->aimuprshoulder;
    }
    else if (bodypart == CHR_RENDERPART_LEFT_ARM)
    {
        xrot = g_CurModelChr->aimuplshoulder;
    }
    else if (bodypart == CHR_RENDERPART_TORSO)
    {
#ifdef BUGFIX_R1
    #undef g_CurModelChr
#else
        chr = g_CurModelChr;
#endif
        xrot = chr->aimupback;
        if (chr->hidden & CHRHIDDEN_0400)
        {
            if (PI_OVER_3 < xrot)
            {
                xrot -= PI_OVER_3;
            }
            else if (-FIVEPI_OVER_18 > xrot)
            {
                xrot += FIVEPI_OVER_18;
            }
            else
            {
                xrot = 0.0f;
            }
        }

        yrot = chr->aimsideback;

    }
    else if (bodypart == CHR_RENDERPART_HEAD)
    {
#ifndef BUGFIX_R1
        chr = g_CurModelChr;
#endif
        if (chr->hidden & CHRHIDDEN_0400)
        {
            xrot = chr->aimupback;

            if (chr->hidden & CHRHIDDEN_0400)
            {
                if (PI_OVER_3 < xrot)
                {
                    xrot = PI_OVER_3;
                }
                else if (xrot < -FIVEPI_OVER_18)
                {
                    xrot = -FIVEPI_OVER_18;
                }
            }
        }
        else if (chr->model->gunhand != GUNRIGHT)
        {
            xrot = chr->aimuplshoulder;
        }
        else
        {
            xrot = chr->aimuprshoulder;
        }
    }

    chr = g_CurModelChr;

    if (chr->flinchcnt >= 0)
    {
        if (0);

        if ((bodypart == CHR_RENDERPART_RIGHT_ARM) || (bodypart == CHR_RENDERPART_LEFT_ARM))
        {
            amount = chrGetFlinchAmount(chr) * M_TAU_F * 15.0f / 360.0f;
            chr = g_CurModelChr;
            hidden = chr->hidden;
            xrot -= amount;

            if (hidden & CHRHIDDEN_RAND_FLINCH_1)
            {
                yrot -= amount;
            }
            else if (hidden & CHRHIDDEN_RAND_FLINCH_2)
            {
                yrot += amount;
            }
        }
        else if (bodypart == CHR_RENDERPART_TORSO)
        {
            tmp = chrGetFlinchAmount(chr);
            tmp *= M_TAU_F;
            amount = tmp * 15.0f;
            chr = g_CurModelChr;
            hidden = chr->hidden;
            amount /= 360.0f;
            xrot += amount;

            if (hidden & CHRHIDDEN_RAND_FLINCH_1)
            {
                yrot += amount;
            }
            else if (hidden & CHRHIDDEN_RAND_FLINCH_2)
            {
                yrot -= amount;
            }

            if (hidden & CHRHIDDEN_RAND_FLINCH_4)
            {
                 zrot += (tmp * 10.0f) / 360.0f;
            }
            else if (hidden & CHRHIDDEN_RAND_FLINCH_8)
            {
                zrot -= (tmp * 10.0f) / 360.0f;
            }
        }
    }

    if ((((xrot == 0.0f) && (yrot == 0.0f)) && (zrot == 0.0f)) && (scale == 1.0f))
    {
        return;
    }

    sideback = chrlvGetSubrotySideback(chr);

    if (xrot < 0.0f)
    {
        xrot = -xrot;
    }
    else
    {
        xrot = M_TAU_F - xrot;
    }

    if (yrot < 0.0f)
    {
        yrot += M_TAU_F;
    }

    matrix_4x4_multiply_homogeneous_in_place(currentPlayerGetViewToWorldMtxf(), matrix);

    savedposx = matrix->m[3][0];
    savedposy = matrix->m[3][1];
    savedposz = matrix->m[3][2];

    matrix->m[3][0] = 0.0f;
    matrix->m[3][1] = 0.0f;
    matrix->m[3][2] = 0.0f;

    if ((xrot != 0.0f) || (zrot != 0.0f))
    {
        yrot -= sideback;

        if (yrot < 0.0f)
        {
            yrot += M_TAU_F;
        }

        matrix_4x4_set_rotation_around_y(yrot, &rotmtx);
        matrix_4x4_multiply_homogeneous_in_place(&rotmtx, matrix);

        if (xrot != 0.0f)
        {
            matrix_4x4_set_rotation_around_x(xrot, &rotmtx);
            matrix_4x4_multiply_homogeneous_in_place(&rotmtx, matrix);
        }

        if (zrot != 0.0f)
        {
            matrix_4x4_set_rotation_around_z(zrot, &rotmtx);
            matrix_4x4_multiply_homogeneous_in_place(&rotmtx, matrix);
        }

        matrix_4x4_set_rotation_around_y(sideback, &rotmtx);
        matrix_4x4_multiply_homogeneous_in_place(&rotmtx, matrix);
    }
    else
    {
        matrix_4x4_set_rotation_around_y(yrot, &rotmtx);
        matrix_4x4_multiply_homogeneous_in_place(&rotmtx, matrix);
    }

    if (scale != 1.0f)
    {
        matrix_scalar_multiply(scale, (f32 *) matrix);
    }

    matrix->m[3][0] = savedposx;
    matrix->m[3][1] = savedposy;
    matrix->m[3][2] = savedposz;

    matrix_4x4_multiply_homogeneous_in_place(camGetWorldToScreenMtxf(), matrix);
}


#undef PI_OVER_3
#undef FIVEPI_OVER_18


/**
 * Address 0x7F020D94.
 *
 * For visibility, player collision, tank collision, bullet collision, and explosion damage
 * tests the game needs to know which room(s) a character is in. This allows the game to perform
 * tests on only characters in loaded rooms, not the entire stage.
 */
void chrDetectRooms(ChrRecord *self)
{
    PropRecord *myprop;
    coord3d     lowerbounds;
    coord3d     upperbounds;

    // Create a roughly character sized bounding box.
    myprop        = self->prop;
    lowerbounds.x = myprop->pos.x - 50.0f;
    lowerbounds.y = self->ground - 1.0f;
    lowerbounds.z = myprop->pos.z - 50.0f;
    upperbounds.x = myprop->pos.x + 50.0f;
    upperbounds.y = myprop->pos.y + 100.0f;
    upperbounds.z = myprop->pos.z + 50.0f;

    // Delist the character prop from its previous room(s)
    chrpropDeregisterRooms(myprop);

    // Detect rooms overlapped by the bounding box
    chrpropUpdateRoomList(myprop, &lowerbounds, &upperbounds, 50.0f);

    // Re-register the character prop in those rooms
    chrpropRegisterRooms(myprop);
}


/**
 * Address 0x7F020E40.
 */
void chrUpdateAnim(ChrRecord *chr, chranimticks_t tickamount)
{
    Model *model;
    PropRecord* prop;

    model = chr->model;
    prop = chr->prop;

    if (!(chr->hidden & CHRHIDDEN_FREEZE))
    {
        getsuboffset(model, &chr->prevpos);
        chrModelTickAnim(model, tickamount, 1);
        subcalcpos(model);
        set_color_shading_from_tile(prop, &chr->nextcol);
        getsuboffset(model, &prop->pos);
        chrDetectRooms(chr);

        return;
    }

    subcalcpos(model);
    getsuboffset(model, &prop->pos);
}


/**
 * Address:
 * US: 0x7F020EF0
 * JP: 0x7F021188
 * EU: 0x7F020E68
 *
 *   This function does the following:
 * - Drive character animations
 * - Remove characters if needed
 * - Held weapon garbage collection
 * - Character visibility tests
 * - Render characters/hats/held weapons
 * - Drop held items
 * - Fire held weapons
 */
#ifndef TARGET_N64
u32 g_PortHwChrsTicked; /* GE HW pacing: characters simulated this tick (frametiming.c snapshots it) */
#endif
s32 chrTick(PropRecord *prop)
{
    ModelRenderData renderdata;
#ifndef TARGET_N64
    g_PortHwChrsTicked++;
#endif
    ChrRecord *chr;
    Model *model;
    s32 headSwitchVisible;
    s32 headVisible;
    chranimticks_t tickamount;

    renderdata = D_8002CC6C;
    chr = prop->chr;
    model = chr->model;
    headVisible = 1;
#ifndef TARGET_N64
    tickamount = portAnimTicks();
#else
    tickamount = g_ClockTimer;
#endif

    if ((!(chr->chrflags & CHRFLAG_HIDDEN)) || (chr->chrflags & CHRFLAG_00040000))
    {
        if (D_8002C904)
        {
            if (((ModelAnimation *)animation_table_ptrs1[g_AnimationTablePointerCountRelated]) != ((ModelAnimation *)1))
            {
                if (objecthandlerGetModelAnim(model) != ((ModelAnimation *)animation_table_ptrs1[g_AnimationTablePointerCountRelated]))
                {
                    modelSetAnimation(model, (ModelAnimation *)animation_table_ptrs1[g_AnimationTablePointerCountRelated], 0, 0.0f, 0.5f, 0.0f);
                }
            }
        }
        else
        {
            chrlvActionTick(chr);

            if (chr->model == NULL)
            {
                return TICKOP_FREE;
            }
        }

        if (D_8002C90C)
        {
            tickamount = 0;

            if (D_8002C910)
            {
                tickamount = 1;
            }
#ifdef DEBUG
            osSyncPrintf("anim=%d frame=%f backy=%f\n", g_AnimationTablePointerCountRelated, chr->model->animframe1, (chr->aimendlshoulder * 360.0) / 6.283185);
#endif
        }
    }

    if (chr->hidden & CHRHIDDEN_REMOVE)
    {
        chrpropCleanupForRemoval(prop);
        return TICKOP_FREE;
    }

    if (chr->weapons_held[GUNRIGHT] != NULL)
    {
        if (chr->weapons_held[GUNRIGHT]->obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
        {
            objFreePermanently(chr->weapons_held[GUNRIGHT]->obj, 1);
        }
    }

    if (chr->weapons_held[GUNLEFT] != NULL)
    {
        if (chr->weapons_held[GUNLEFT]->obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
        {
            objFreePermanently(chr->weapons_held[GUNLEFT]->obj, 1);
        }
    }

    if (chr->chrflags & CHRFLAG_HIDDEN)
    {
        headSwitchVisible = 0;
    }
    else
    {
        if (((prop->type == PROP_TYPE_VIEWER) && (g_playerPointers[getPlayerPointerIndex(prop)]->cameramode == 1)) || (chr->chrflags & CHRFLAG_CULL_USING_HITBOX))
        {
            headSwitchVisible = 1;

            if (((chr->actiontype == ACT_ANIM) && (chr->act_anim.unk02c == 0)) && (chr->act_anim.noTranslate != 0))
            {
                chrModelTickAnim(model, tickamount, 0);
            }
            else
            {
                chrUpdateAnim(chr, tickamount);
            }

            goto after_position_update;
        }

        if ((chr->actiontype == ACT_PATROL) || (chr->actiontype == ACT_GOPOS))
        {
            if (((chr->actiontype == ACT_PATROL) && (chr->act_patrol.waydata.mode == WAYMODE_MAGIC)) || ((chr->actiontype == ACT_GOPOS) && (chr->act_gopos.waydata.mode == WAYMODE_MAGIC)))
            {
                headSwitchVisible = posIsOnScreen(prop, &prop->pos, getinstsize(model), 1);

                if (headSwitchVisible)
                {
#ifdef DEBUG
                    osSyncPrintf("\nVISIBLE MAGIC MODE!!!!\n\n");
#endif

                    getsuboffset(model, &chr->prevpos);
                    subcalcpos(model);
                    set_color_shading_from_tile(prop, &chr->nextcol);
                    getsuboffset(model, &prop->pos);
                    chrDetectRooms(chr);
                }
            }
            else
            {
                chrUpdateAnim(chr, tickamount);
                headSwitchVisible = posIsOnScreen(prop, &prop->pos, getinstsize(model), 1);

                if (headSwitchVisible)
                {
                    if (chr->actiontype == ACT_PATROL)
                    {
                        chr->act_patrol.lastvisible60 = g_GlobalTimer;
                    }
                    else if (chr->actiontype == ACT_GOPOS)
                    {
                        chr->act_gopos.unk9c = g_GlobalTimer;
                    }
                }
            }
        }
        else if ((chr->actiontype == ACT_ANIM) && (chr->act_anim.unk02c == 0))
        {
            headSwitchVisible = posIsOnScreen(prop, &prop->pos, getinstsize(model), 1);

            if (headSwitchVisible && (chr->act_anim.noTranslate == 0))
            {
                chrUpdateAnim(chr, tickamount);
            }
            else
            {
                chrModelTickAnim(model, tickamount, 0);
            }
        }
        else if (chr->actiontype == ACT_STAND)
        {
            headSwitchVisible = posIsOnScreen(prop, &prop->pos, getinstsize(model), 1);

            if (headSwitchVisible || (chr->chrflags & CHRFLAG_INIT))
            {
                chrUpdateAnim(chr, tickamount);
            }
            else if (model->anim2 != NULL)
            {
                chrModelTickAnim(model, tickamount, 0);
            }
        }
        else
        {
            if (chr->chrflags & CHRFLAG_IGNORE_ANIM_TRANSLATION)
            {
                chrModelTickAnim(model, tickamount, 0);
            }
            else
            {
                chrUpdateAnim(chr, tickamount);
            }

            headSwitchVisible = posIsOnScreen(prop, &prop->pos, getinstsize(model), 1);
        }
    }

after_position_update:
    if (((chr->actiontype != ACT_STAND) || (model->anim2 != NULL)) || (prop->type == PROP_TYPE_VIEWER))
    {
        chr->hidden |= CHRHIDDEN_BACKGROUND_AI;
    }

    chrUpdateAimProperties(chr);

    if (chr->field_20 != NULL)
    {
        sub_GAME_7F06B248(chr->field_20);
        chr->field_20 = NULL;
    }

    if (headSwitchVisible)
    {
        if (get_debug_chrnum_flag()) {}

        prop->flags |= PROPFLAG_ONSCREEN;
        chr->chrflags |= CHRFLAG_HAS_BEEN_ON_SCREEN;

#ifdef BUGFIX_R1
    if (cheatIsActive(12))
    {
        if (chrCanUseDKModeScaling(chr->bodynum, chr->headnum))
        {
            modelSetDistanceScale(0.3125f);

            if (chr->chrflags & CHRFLAG_10000000)
            {
                chr->chrflags &= ~CHRFLAG_10000000;
                modelSetScale(chr->model, chr->model->scale / 0.8f);
            }
        }
    }
#else
        if (cheatIsActive(CHEAT_DK_MODE))
        {
            modelSetDistanceScale(0.3125f);
        }
#endif

        g_ModelJointPositionedFunc = (void (*)(s32, Mtxf *))chrHandleJointPositioned;
        g_CurModelChr = chr;

        renderdata.basemtx = camGetWorldToScreenMtxf();
        renderdata.mtxlist = dynAllocate(model->obj->numMatrices * (sizeof(Mtxf)));

        if (g_CurModelChr->flinchcnt >= 0)
        {
            g_CurModelChr->flinchcnt += g_ClockTimer;

#ifdef VERSION_EU
               if (g_CurModelChr->flinchcnt >= 24)
#else
               if (g_CurModelChr->flinchcnt >= 30)
#endif
            {
                g_CurModelChr->flinchcnt = -1;
            }
        }

        subcalcmatrices(&renderdata, model);

        g_ModelJointPositionedFunc = NULL;
        modelSetDistanceScale(1.0f);

        update_color_shading(&chr->shadecol, &chr->nextcol);

        prop->zDepth = sub_GAME_7F06C768(model);

        chr->field_20 = sub_GAME_7F06B120(NULL, model);

        chrRenderHeldWeapon(prop, GUNRIGHT, (Gfx **)(&chr->field_20));
        chrRenderHeldWeapon(prop, GUNLEFT, (Gfx **)(&chr->field_20));

        if (chr->handle_positiondata_hat != NULL
#ifndef TARGET_N64
            /* PORT (triage F1.2): the MP crash faulted below in
             * modelFindNodeMtx on a garbage hat attach chain. Validate
             * every link (prop->obj->model->attachedto_objinst) and skip
             * the hat render for this chr if any is stray. */
            && !portChrHatChainLooksBad(chr)
#endif
            )
        {
            ObjectRecord *hatobj;
            Model *hatmodel;

            hatobj = chr->handle_positiondata_hat->obj;
            hatmodel = hatobj->model;

            chr->handle_positiondata_hat->flags |= PROPFLAG_ONSCREEN;

            renderdata.basemtx = modelFindNodeMtx(model, hatmodel->attachedto_objinst, 0);
            renderdata.mtxlist = dynAllocate(hatmodel->obj->numMatrices * (sizeof(Mtxf)));

            instcalcmatrices(&renderdata, hatmodel);

            if ((chr->headnum >= HEAD_START) && (chr->headnum < BODY_Female_Sally))
            {
                coord3d pos;
                f32 xscale;
                f32 yscale;
                f32 zscale;
                Mtxf mtx;
                Mtxf tmp;
                HATTYPE hat;
                s32 unusedv;
                struct headHat *entry;
                volatile s32 changed;
                s32 headindex;

                pos = D_8002CCAC;

                hat = get_hat_model(chr->handle_positiondata_hat);

                headindex = chr->headnum - HEAD_START;
                entry = &((struct headHat (*)[6]) headHat_array_8003E464)[headindex][hat];

                if (!get_debug_render_raster())
                {
                    changed = 0;

                    if (joyGetButtons(PLAYER_1, L_TRIG))
                    {
                        if (joyGetButtons(PLAYER_1, A_BUTTON))
                        {
                            /* The differing spellings of 0.02 (and 0.995/1.005
                               below) are intentional and load-bearing: IDO interns
                               float literals by lexical spelling, and the ROM's
                               rodata pool contains six separate 0.02 words
                               (formerly hosted as D_80051D58..D_80051D6C). */
                            entry->zoffset -= 0.02f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, B_BUTTON))
                        {
                            entry->zoffset += 0.020f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, D_CBUTTONS))
                        {
                            entry->yoffset -= 0.0200f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, U_CBUTTONS))
                        {
                            entry->yoffset += 0.02000f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, L_CBUTTONS))
                        {
                            entry->xoffset -= 0.020000f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, R_CBUTTONS))
                        {
                            entry->xoffset += 0.0200000f;
                            changed = 1;
                        }
                    }

                    if (joyGetButtons(PLAYER_1, R_TRIG))
                    {
                        if (joyGetButtons(PLAYER_1, A_BUTTON))
                        {
                            entry->zsize *= 0.995f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, B_BUTTON))
                        {
                            entry->zsize *= 1.005f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, D_CBUTTONS))
                        {
                            entry->ysize *= 0.9950f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, U_CBUTTONS))
                        {
                            entry->ysize *= 1.0050f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, L_CBUTTONS))
                        {
                            entry->xsize *= 0.99500f;
                            changed = 1;
                        }

                        if (joyGetButtons(PLAYER_1, R_CBUTTONS))
                        {
                            entry->xsize *= 1.00500f;
                            changed = 1;
                        }
                    }
                }

                pos.x = entry->xoffset * 21.3f;
                pos.y = entry->yoffset * 21.3f;
                pos.z = entry->zoffset * 21.3f;

                xscale = entry->xsize;
                yscale = entry->ysize;
                zscale = entry->zsize;

                matrix_4x4_set_identity_and_position(&pos, &mtx);

                matrix_column_1_scalar_multiply(xscale, (f32 *)(&mtx));
                matrix_column_2_scalar_multiply(yscale, (f32 *)(&mtx));
                matrix_column_3_scalar_multiply_2(zscale, (f32 *)(&mtx));

                matrix_4x4_multiply_homogeneous((Mtxf *)hatmodel->render_pos, &mtx, &tmp);
                matrix_4x4_copy(&tmp, (Mtxf *)hatmodel->render_pos);

                if (hat == HATTYPE_PEAKED)
                {
                    headVisible = 0;
                }
            }

            if ((!(chr->hidden & CHRHIDDEN_DROP_HELD_ITEMS)) || (!(hatobj->runtime_bitflags & RUNTIMEBITFLAG_00000080)))
            {
                chr->field_20 = sub_GAME_7F06B120(chr->field_20, hatmodel);
            }
        }

        if (model->obj->Switches[4] != NULL)
        {
            union ModelRwData *rwdata;
            ModelFileHeader *headfile;

            rwdata = modelGetNodeRwData(model, model->obj->Switches[4]);
            headfile = rwdata->HeadPlaceholder.ModelFileHeader;

            if ((headfile != NULL) && (headfile->Switches[1] != NULL))
            {
                modelGetNodeRwData(model, headfile->Switches[1])->Switch.visible = headVisible;
            }
        }

        sub_GAME_7F06B29C(chr->field_20);
        chr->field_20 = sub_GAME_7F06BB28(chr->field_20);
    }
    else
    {
        if (chr->weapons_held[GUNRIGHT] != NULL)
        {
            chr->weapons_held[GUNRIGHT]->flags &= ~PROPFLAG_ONSCREEN;
        }

        if (chr->weapons_held[GUNLEFT] != NULL)
        {
            chr->weapons_held[GUNLEFT]->flags &= ~PROPFLAG_ONSCREEN;
        }

        if (chr->handle_positiondata_hat != NULL)
        {
            chr->handle_positiondata_hat->flags &= ~PROPFLAG_ONSCREEN;
        }

        prop->flags &= ~PROPFLAG_ONSCREEN;

        chr->shadecol.r = chr->nextcol.r;
        chr->shadecol.g = chr->nextcol.g;
        chr->shadecol.b = chr->nextcol.b;
        chr->shadecol.a = chr->nextcol.a;
    }

    if (!(chr->chrflags & CHRFLAG_HIDDEN))
    {
        if (chr->hidden & CHRHIDDEN_DROP_HELD_ITEMS)
        {
            PropRecord *dropprop = prop->child;
            PropRecord *unusedprop;

            while (dropprop != NULL)
            {
                PropRecord *nextprop = dropprop->prev;

                objDrop(dropprop);

                dropprop = nextprop;
            }

            chr->hidden &= ~CHRHIDDEN_DROP_HELD_ITEMS;
        }

        chrlvTriggerFireWeapon(chr);
    }

    return TICKOP_NONE;
}


/**
 * Address 0x7F021B20.
 */
void chrDropItems(ChrRecord *self)
{
    PropRecord *childprop = self->prop->child;
    while (childprop)
    {
        if ((childprop != self->handle_positiondata_hat) &&
            (childprop != self->weapons_held[GUNLEFT]) &&
            (childprop != self->weapons_held[GUNRIGHT]))
        {
            WeaponObjRecord *wep = childprop->weapon;
            if (!(wep->flags & 0x2000))
            {
                propobjSetDropped(childprop, 1);
            }
        }
        childprop = childprop->prev;
    };

    self->hidden |= 1;
}



/**
 * Unreferenced.
 *
 * Sets gBloodColour 3 bytes from paramter.
 *
 * @param colour: rgba_u8.
 *
 * Address 0x7F021BB4.
 */
void chrSetgBloodColour(rgba_u8 *colour)
{
    gBloodColour.r = colour->r;
    gBloodColour.g = colour->g;
    gBloodColour.b = colour->b;
}


/**
 * Unreferenced.
 *
 * Gets gBloodColour 3 bytes and sets them into parameter.
 *
 * @param colour: rgba_u8.
 *
 * Address 0x7F021BD8.
 */
void chrGetgBloodColour(rgba_u8 *colour)
{
    colour->r = gBloodColour.r;
    colour->g = gBloodColour.g;
    colour->b = gBloodColour.b;
}



/**
 * Address 0x7F021BFC.
*/
Gfx *chrRenderProp(PropRecord *prop, Gfx *gdl, s32 withalpha)
{
    ChrRecord *chr;
    Model *chrmodel;
    struct rgba_f32 spC0; // 192
    s32 spBC; // 188
    s32 spB8; // 184
    s32 chrfadealpha; // 180
    rgba_u8 temp_v1_2;
    ModelRenderData mrData; // 112
    struct view4f sp60; // -?? 96
    struct rgba_s32 chrShade;
    s32 sp4C; // 76
    PropRecord *prop_held_right; // 72
    PropRecord *prop_held_left; // 68
    PropRecord *prop_held_hat; // 64
    ObjectRecord *held_right_obj; // 60
    ObjectRecord *held_left_obj; // 56
    ObjectRecord *held_hat_obj; // 52

    //

    chr = prop->chr;
    chrmodel = chr->model;
    chrfadealpha = (s32) chr->fadealpha;

#ifndef TARGET_N64
    /* GE_INTRODBG=1 diagnostic: once a second, log every chr reaching the
     * render path while a fixed intro camera is up (its rooms, whether each is
     * rendered, eye depth, fade alpha). Pair with GE_PORTALDBG=1 to see which
     * portal walk let a room in. */
    {
        extern char *getenv(const char *);
        extern u32 bondviewGetCameraMode(void);
        extern f32 portWideFactor(void);
        extern s32 portFullHeightActive(void);
        extern void chraiGetPropRoomIds(PropRecord *self, s32 *roomids);
        static s32 s_IntroDbg = -1;
        if (s_IntroDbg < 0) { s_IntroDbg = getenv("GE_INTRODBG") != NULL; }
        if (s_IntroDbg && (bondviewGetCameraMode() == 1 || bondviewGetCameraMode() == 2) && (g_GlobalTimer % 60) == 0)
        {
            s32 rooms[8];
            s32 i;
            chraiGetPropRoomIds(prop, rooms);
            printf("introdbg: cam=%u chr=%d pos=(%.0f,%.0f,%.0f) zdepth=%.0f fade=%d hidden=%d rooms:",
                   bondviewGetCameraMode(), chr->chrnum, prop->pos.x, prop->pos.y, prop->pos.z, prop->zDepth,
                   chrfadealpha, (chr->chrflags & CHRFLAG_HIDDEN) != 0);
            for (i = 0; i < 8 && rooms[i] >= 0; i++) { printf(" %d%s", rooms[i], getROOMID_isRendered(rooms[i]) ? "R" : "-"); }
            printf(" wf=%.2f fullh=%d\n", portWideFactor(), portFullHeightActive());
            { /* the vanilla range-cull inputs (sub_GAME_7F054C58 hard cull on
               * the camera-forward distance, chrobjFogVisRangeRelated fade on
               * zDepth), evaluated regardless of distance_fading so the
               * numbers exist even when the gate is off. */
                extern coord3d *bondviewGetCurrentPlayersPosition(void);
                extern Mtxf *camGetWorldToScreenMtxf(void);
                extern f32 getPlayer_c_lodscalez(void);
                extern struct NearFogRecord *fogGetNearFogValuesP(void);
                f32 *nfd = (f32 *) fogGetNearFogValuesP(); /* NearFog, MaxVisRange, MaxObfuscationRange (f32 record) */
                f32 size = getinstsize(chrmodel);
                f32 lodz = getPlayer_c_lodscalez();
                coord3d *campos = bondviewGetCurrentPlayersPosition();
                Mtxf *m = camGetWorldToScreenMtxf();
                f32 dx = prop->pos.x - campos->x;
                f32 dy = prop->pos.y - campos->y;
                f32 dz = prop->pos.z - campos->z;
                f32 fwd = dx * m->m[0][0] + dy * m->m[0][1] + dz * m->m[0][2];
                f32 cullz = -1.0f;
                f32 fadez = -1.0f;
                if (nfd != NULL && size > 0.0f)
                {
                    if (fwd > nfd[2]) { cullz = ((fwd - nfd[2]) * 100.0f / size + nfd[2]) * lodz; }
                    if (prop->zDepth > nfd[2]) { fadez = ((prop->zDepth - nfd[2]) * 100.0f / size + nfd[2]) * lodz; }
                }
                printf("introdbg:   size=%.1f scale=%.4f lodz=%.3f nfd=%.0f/%.0f/%.0f campos=(%.0f,%.0f,%.0f) fwd=%.0f cullz=%.0f fadez=%.0f (vanilla culls if cullz>=MaxVis, fades out as fadez->MaxVis)\n",
                       size, chrmodel->scale, lodz,
                       nfd != NULL ? nfd[0] : 0.0f, nfd != NULL ? nfd[1] : 0.0f, nfd != NULL ? nfd[2] : 0.0f,
                       campos->x, campos->y, campos->z, fwd, cullz, fadez);
            }
        }
    }
#endif

    if (!(chr->chrflags & CHRFLAG_04000000))
    {
        f32 f = chrobjFogVisRangeRelated(prop, getinstsize(chrmodel)); //0-1
        chrfadealpha = (s32) (f * (f32) chrfadealpha);
    }

    if ((chrfadealpha < 0xFF) || (chr->chrflags & CHRFLAG_00020000))
    {
        if (withalpha == 0)
        {
            // nothing to do
            return gdl;
        }
        else
        {
            spB8 = 3;
        }
    }
    else
    {
        if (withalpha == 0)
        {
            spB8 = 1;
        }
        else
        {
            spB8 = 2;
        }
    }

    spBC = fogGetPropDistColor(prop, &spC0);
    if (spBC != 0)
    {
        if (chrfadealpha > 0)
        {
            mrData = D_8002CCBC;


            sp4C = 0x50;

            prop_held_right = chr->weapons_held[GUNRIGHT];
            prop_held_left = chr->weapons_held[GUNLEFT];
            prop_held_hat = chr->handle_positiondata_hat;
            held_right_obj = NULL;
            held_left_obj = NULL;
            held_hat_obj = NULL;

            if (prop_held_right != NULL)
            {
                held_right_obj = prop_held_right->obj;
            }

            if (prop_held_left != NULL)
            {
                held_left_obj = prop_held_left->obj;
            }

            if (prop_held_hat != NULL)
            {
                held_hat_obj = prop_held_hat->obj;
            }

            if ((getPropCombinedRoomsBBox2D(prop, &sp60) > 0) && !(chr->chrflags & CHRFLAG_CULL_USING_HITBOX))
            {
                gdl = bgScissorCurrentPlayerViewF(gdl, sp60.left, sp60.top, sp60.width, sp60.height);
            }
            else
            {
                gdl = bgScissorCurrentPlayerViewDefault(gdl);
            }

            mrData.flags = spB8;
            mrData.zbufferenabled = TRUE;
            mrData.gdl = gdl;

            if ((chr->chrflags & CHRFLAG_NO_SHADOW) != 0)
            {
                sp4C = 0;
            }
            else if (spBC == 1)
            {
                sp4C = ((1.0f - spC0.a) * (f32)(sp4C));
            }

            sub_GAME_7F073FC8(sp4C);

            chrShade.r = chr->shadecol.r;
            chrShade.g = chr->shadecol.g;
            chrShade.b = chr->shadecol.b;
            chrShade.a = chr->shadecol.a;

            lerp_rgba_s32_with_rgba_f32(&chrShade, spBC, &spC0);

            mrData.envcolour.word = ((gBloodColour.rgba[0] << 0x18) | (gBloodColour.rgba[1] << 0x10)) | (gBloodColour.rgba[2] << 0x08);
            mrData.fogcolour.word = (chrShade.rgba[0] << 0x18) | (chrShade.rgba[1] << 0x10) | (chrShade.rgba[2] << 0x08) | (chrShade.rgba[3] << 0x00);

            if (chrfadealpha < 0xFF)
            {
                mrData.PropType = 8;
                mrData.envcolour.word |= (u8)chrfadealpha;
            }
            else
            {
                mrData.PropType = 7;
            }

            g_playerPerm->time_other_players_on_screen += 1;
            drawjointlist(&mrData, chr->field_20);

            gdl = mrData.gdl;

            if ((held_right_obj != NULL) && (( held_right_obj->state & ((u8)(1 << withalpha) )) ))
            {
                gdl = explosionRenderBulletImpactOnProp(gdl, prop_held_right, withalpha);
            }

            if ((held_left_obj != NULL) && (( held_left_obj->state & ((u8)(1 << withalpha) )) ))
            {
                gdl = explosionRenderBulletImpactOnProp(gdl, prop_held_left, withalpha);
            }

            if ((held_hat_obj != NULL) && (( held_hat_obj->state & ((u8)(1 << withalpha) )) ))
            {
                gdl = explosionRenderBulletImpactOnProp(gdl, prop_held_hat, withalpha);
            }

            if (withalpha != 0)
            {
                bondviewTransformManyPosToViewMatrix(chr->model->render_pos, chr->model->obj->numMatrices);

                if ((held_right_obj != NULL) && ((held_right_obj->runtime_bitflags & 0x800) == 0))
                {
                    bondviewTransformManyPosToViewMatrix(held_right_obj->model->render_pos, held_right_obj->model->obj->numMatrices);
                }

                if ((held_left_obj != NULL) && ((held_left_obj->runtime_bitflags & 0x800) == 0))
                {
                    bondviewTransformManyPosToViewMatrix(held_left_obj->model->render_pos, held_left_obj->model->obj->numMatrices);
                }

                if (held_hat_obj != NULL)
                {
                    bondviewTransformManyPosToViewMatrix(held_hat_obj->model->render_pos, held_hat_obj->model->obj->numMatrices);
                }
            }
        }
    }

    if (withalpha != 0)
    {
        sub_GAME_7F06B248(chr->field_20);
        chr->field_20 = NULL;
    }

    return gdl;
}


/**
 * Creates a smoke puff at the front of the character when the character is shot,
 * and also has a ~50% chance of creating a second smoke puff just behind the character.
 */
void chrCreateHitPuffs(PropRecord *prop, s32 anim_id, coord3d *vec, coord3d *pos)
{
    s32 i;
    f32 scale;
    coord3d sp3c;
    s32 index;
    struct ChrHitReaction *entry;

    index = 0;
    i = 0;

    if (g_HitReactionTable[0].hitpart != -1)
    {
        do
        {
            if (anim_id == g_HitReactionTable[i].hitpart)
            {
                index = i;
                break;
            }

            i++;
        }
        while (g_HitReactionTable[i].hitpart != -1);
    }

    entry = &g_HitReactionTable[index];

    if (entry->backImpactPuffCount) {
        // True when randomGetNext() bit 2 is 0, so roughly 50% chance.
        if ((randomGetNext() & 4) == 0)
        {
            scale = (42.0f / sqrtf(vec->z * vec->z + (vec->x * vec->x + vec->y * vec->y))) + 1.0f;

            sp3c.x = vec->x * scale;
            sp3c.y = vec->y * scale;
            sp3c.z = vec->z * scale;

            mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &sp3c);

            bullet_spark_create(&sp3c, entry->backImpactPuffCount, entry->backImpactPuffSize, prop->stan->room);
        }
    }

    if (entry->impactPuffCount)
    {
        bullet_spark_create(pos, entry->impactPuffCount, entry->impactPuffSize, prop->stan->room);
    }
}


void chrCreateBloodStain(Model *model, s32 arg1, ModelNode *root, struct coord3d *pos)
{
    s32 sp_pos[3];
    s32 unused_stack_pad[1];
    s32 bestdist;
    ModelNode *bestnode;
    s32 bestindex;
    ModelNode *node;
    s32 opcode;
    ModelRoData_DisplayList_CollisionRecord *rodata;
    ModelRoData_DisplayList_CollisionRecord *relatedrodata;
    ModelRwData_DisplayList_CollisionRecord *relatedrwdata;
    ModelNode *relatednode;
    ModelNode *rwdata;
    Vertex *vtx;
    Vertex *newvertices;
    s32 i;
    s32 n;
    s32 paintval;
    s32 relatedindex;

    sp_pos[0] = pos->x;
    sp_pos[1] = pos->y;
    sp_pos[2] = pos->z;

    bestnode = NULL;
    bestindex = 0;
    bestdist = 0x7fffffff;
    node = root;

    while (node != NULL)
    {
        opcode = node->Opcode & 0xff;

        if (opcode == MODELNODE_OPCODE_LOD)
        {
            goto apply_distance;
        }

        if (opcode == MODELNODE_OPCODE_SWITCH)
        {
            goto apply_toggle;
        }

        if (opcode == MODELNODE_OPCODE_HEAD)
        {
            goto apply_head;
        }

        if (opcode != MODELNODE_OPCODE_DLCOLLISION)
        {
            goto after_opcode;
        }

        rodata = &node->Data->DisplayListCollisions;
        n = 0;

        if (rodata->numCollisionVertices > 0)
        {
            vtx = rodata->CollisionVertices;

            do
            {
                s32 dx;
                s32 dy;
                s32 dz;
                s32 dist;

                dx = sp_pos[0] - vtx->coord.x;
                dy = sp_pos[1] - vtx->coord.y;
                dz = sp_pos[2] - vtx->coord.z;
                dist = ((((u32) dx) * ((u32) dx)) + (((u32) dy) * ((u32) dy))) + (((u32) dz) * ((u32) dz));

                if (dist < bestdist)
                {
                    bestdist = dist;
                    bestnode = node;
                    bestindex = n;
                }

                n++;
                vtx++;
            }
            while (n < rodata->numCollisionVertices);
        }

        goto after_opcode;

apply_distance:
        modelApplyDistanceRelations(model, node);
        goto after_opcode;

apply_toggle:
        modelApplyToggleRelations(model, node);
        goto after_opcode;

apply_head:
        modelApplyHeadRelations(model, node);

after_opcode:
        if ((node->Child != NULL) && ((node == root) || ((opcode != MODELNODE_OPCODE_BBOX) && (opcode != MODELNODE_OPCODE_OP17))))
        {
            node = node->Child;
        }
        else
        {
            while (node != NULL)
            {
                if (node == root)
                {
                    node = NULL;
                    break;
                }

                if (node->Next != NULL)
                {
                    node = node->Next;
                    break;
                }

                node = node->Parent;
            }
        }
    }

    if (bestnode == NULL)
    {
        return;
    }

    node = (ModelNode *) bestnode->Data;
    rwdata = (ModelNode *) modelGetNodeRwData(model, bestnode);
    relatedrodata = NULL;
    relatedrwdata = NULL;
    relatedindex = 0;
    paintval = (randomGetNext() % 50) + 20;

    if (arg1 == 8)
    {
        paintval += 100;
    }

    if (arg1 == 0x0f)
    {
        paintval += 50;
    }

#ifdef GE_NATIVE_64
    relatednode = portModelCollisionNode(&((ModelRoData_DisplayList_CollisionRecord *)node)->CollisionVertices[bestindex]);
#else
    relatednode = (ModelNode *) ((ModelRoData_DisplayList_CollisionRecord *) node)->CollisionVertices[bestindex].CollisionRelatedNode;
#endif

    if (relatednode != NULL)
    {
        relatedrodata = &relatednode->Data->DisplayListCollisions;
        relatedrwdata = (ModelRwData_DisplayList_CollisionRecord *) modelGetNodeRwData(model, relatednode);
        relatedindex = ((ModelRoData_DisplayList_CollisionRecord *) node)->CollisionVertices[bestindex].CollisionRelatedIndex;
    }

    if (((ModelRwData_DisplayList_CollisionRecord *) rwdata)->Vertices == ((ModelRoData_DisplayList_CollisionRecord *) node)->Vertices)
    {
        newvertices = (Vertex *) vtxstore_allocate(((ModelRoData_DisplayList_CollisionRecord *) node)->numVertices, 0xcccc, 0, 0);

        if (newvertices != NULL)
        {
            s32 j;

            ((ModelRwData_DisplayList_CollisionRecord *) rwdata)->Vertices = newvertices;

            for (j = 0; j < ((ModelRoData_DisplayList_CollisionRecord *) node)->numVertices; j++)
            {
                newvertices[j] = ((ModelRoData_DisplayList_CollisionRecord *) node)->Vertices[j];
            }
        }
    }

    if ((relatedrwdata != NULL) && (relatedrwdata->Vertices == relatedrodata->Vertices))
    {
        newvertices = (Vertex *) vtxstore_allocate(relatedrodata->numVertices, 0xcccc, 0, 0);

        if (newvertices != NULL)
        {
            s32 j;

            relatedrwdata->Vertices = newvertices;

            for (j = 0; j < relatedrodata->numVertices; j++)
            {
                newvertices[j] = relatedrodata->Vertices[j];
            }
        }
    }

    if (((ModelRwData_DisplayList_CollisionRecord *) rwdata)->Vertices != ((ModelRoData_DisplayList_CollisionRecord *) node)->Vertices)
    {
        s32 index;

        index = ((ModelRoData_DisplayList_CollisionRecord *) node)->CollisionVertices[bestindex].index;

        while (index >= 0)
        {
            ((ModelRwData_DisplayList_CollisionRecord *) rwdata)->Vertices[index].a = paintval;
            index = ((ModelRoData_DisplayList_CollisionRecord *) node)->PointUsage[index];
        }
    }

    if ((relatedrwdata != NULL) && (relatedrwdata->Vertices != relatedrodata->Vertices))
    {
        s32 index;

        index = relatedrodata->CollisionVertices[relatedindex].index;

        while (index >= 0)
        {
            relatedrwdata->Vertices[index].a = paintval;
            index = relatedrodata->PointUsage[index];
        }
    }
#ifndef TARGET_N64
    { /* port: the per-instance vertices were edited in place - any GPU-retained part on them is stale (roc_hooks.c) */
        extern void portPartCacheVerticesFreed(const void *vtx);
        portPartCacheVerticesFreed(((ModelRwData_DisplayList_CollisionRecord *) rwdata)->Vertices);
        if (relatedrwdata != NULL) portPartCacheVerticesFreed(relatedrwdata->Vertices);
    }
#endif
}


/**
 *
 * Address: 7F022648
 *
 * Tests a shot's ray against a character.
 *
 * First tests the shot against the chr's bounding sphere expanded by the
 * largest held weapon's model size. On a bounds hit, the model hit list is
 * walked for a body part. A confirmed body hit is registered via chrpropAddBulletHit;
 * a near miss inside max range flags CHRFLAG_NEAR_MISS and bumps numclosearghs.
 *
 * PD: chr_test_hit
 */

/**
 * This assert is from the former NONMATCHING block and belongs somewhere in chrTestHit.
 *
 * #ifdef DEBUG
 * assert(hits && hits->HasHits());
 * #endif
 */
void chrTestHit(PropRecord *prop, ShotData *shotdata)
{
    ChrRecord *chr;
    Model *model;
    f32 modelsize;
    s32 hitpart;
    Model *hitmodel;
    ModelNode *hitnode;
    s32 hitbounds;
    struct HitThing hitthing;
    s32 mtxindex;
    ModelNode *dlnode;
    Mtxf *submatrix;
    f32 heldmodelsize;
    Mtxf *nodemtx;
    f32 len;
    struct WeaponObjRecord *weapon;
    f32 dist;
    ModelHitEntry *entry;
    coord3d viewdir;
    f32 size;
    s32 i;
    coord3d diff;
    f64 pad;

    chr = prop->chr;

    if (chr->actiontype == ACT_DEAD)
    {
        return;
    }

    model = chr->model;
    modelsize = getinstsize(model);

    if ((prop->flags & PROPFLAG_ONSCREEN) == FALSE)
    {
        return;
    }

    hitpart = HIT_NULL_PART;

    if (!((prop->zDepth - modelsize) < shotdata->maxdist))
    {
        return;
    }

    hitmodel = NULL;
    hitnode = NULL;
    hitbounds = FALSE;
    dlnode = NULL;
    submatrix = getsubmatrix(model);
    heldmodelsize = 0.0f;

    for (i = 0; i < 2; i++)
    {
        if (chr->weapons_held[i] != NULL)
        {
            weapon = chr->weapons_held[i]->weapon;
            size = model->scale * getinstsize(weapon->model);

            if (heldmodelsize < size)
            {
                heldmodelsize = size;
            }
        }
    }

    modelsize += heldmodelsize;

    if (projectileTestPropBoundingSphere(&shotdata->viewOrigin, &shotdata->viewDir, (coord3d *) &submatrix->m[3][0], modelsize))
    {
        hitbounds = TRUE;
        hitpart = HIT_LEFT_FOOT;
    }

    if (hitpart != HIT_NULL_PART)
    {
        entry = chr->field_20;
        hitpart = sub_GAME_7F06C010(&entry, &shotdata->viewOrigin, &shotdata->viewDir, &hitmodel, &hitnode);

        while ((hitpart == HIT_GUN) || (hitpart == HIT_HAT))
        {
            if (propobjFindHit(hitmodel, hitnode, &shotdata->viewOrigin, &shotdata->viewDir, &hitthing, &mtxindex, &dlnode))
            {
                break;
            }

            // Leave if (1) for matching.
            if (1)
            {
                hitpart = probably_damage_detail_blood_effect_related(&entry, &shotdata->viewOrigin, &shotdata->viewDir, &hitmodel, &hitnode);
            }
        }
    }

    if (hitpart > HIT_NULL_PART)
    {
        viewdir.x = shotdata->viewDir.x;
        viewdir.y = shotdata->viewDir.y;
        viewdir.z = shotdata->viewDir.z;

        submatrix = currentPlayerGetViewToWorldMtxf();
        mtx4RotateVecInPlace(submatrix, &viewdir);
        nodemtx = modelFindNodeMtx(hitmodel, hitnode, 0);

        // Leave for matching
        if (&diff);

        diff.x = nodemtx->m[3][0] - shotdata->viewOrigin.x;
        diff.y = nodemtx->m[3][1] - shotdata->viewOrigin.y;
        diff.z = nodemtx->m[3][2] - shotdata->viewOrigin.z;

        len = sqrtf((diff.z * diff.z) + ((diff.x * diff.x) + (diff.y * diff.y)));
        dist = -((shotdata->viewDir.z * len) + shotdata->viewOrigin.z);

        if (dist < shotdata->maxdist)
        {
            chrpropAddBulletHit(shotdata, prop, dist, hitpart, hitnode, &hitthing, mtxindex, dlnode, hitmodel, TRUE, FALSE);
        }
    }

    if (hitbounds && (prop->zDepth <= shotdata->maxdist))
    {
        chr->chrflags |= CHRFLAG_NEAR_MISS;
        chr->numclosearghs++;
    }
}

#ifndef TARGET_N64
/**
 * Enemy friendly fire: work out which body part a guard's stray shot struck, so
 * a head is a head and a limb a limb (Rare's guard-on-guard path passes
 * HIT_CHEST for every hit).
 *
 * On screen, this is chrTestHit's own test: the guard's world ray (from, dir)
 * is moved into view space through the inverse of the player's view-to-world
 * matrix, then walks the chr's model hit list as Bond's bullet does, stepping
 * past a gun or hat box whose real geometry the ray misses. The hit list is
 * from the last render, so this needs PROPFLAG_ONSCREEN, as Bond's does. Off
 * screen, or when the ray only clipped the collision cylinder, the part comes
 * from the height the ray passes the chr's axis, in bands of chrheight above
 * its ground.
 */
s32 g_PortStrayHitByModel; /* the last answer came from the model test (else the height bands) - GE_ENEMYFF_LOG */

s32 portChrStrayHitPart(PropRecord *prop, coord3d *from, coord3d *dir)
{
    ChrRecord *chr = prop->chr;
    s32 hitpart = HIT_NULL_PART;

    g_PortStrayHitByModel = FALSE;

    if ((prop->flags & PROPFLAG_ONSCREEN) && chr->model != NULL && chr->field_20 != NULL)
    {
        Mtxf *vtw = currentPlayerGetViewToWorldMtxf();
        coord3d rel;
        coord3d vo;
        coord3d vd;
        ModelHitEntry *entry = chr->field_20;
        Model *hitmodel = NULL;
        ModelNode *hitnode = NULL;
        struct HitThing hitthing;
        s32 mtxindex;
        ModelNode *dlnode = NULL;
        f32 len;
        s32 a;

        rel.x = from->x - vtw->m[3][0];
        rel.y = from->y - vtw->m[3][1];
        rel.z = from->z - vtw->m[3][2];
        for (a = 0; a < 3; a++)
        {
            const f32 sq = (vtw->m[a][0] * vtw->m[a][0]) + (vtw->m[a][1] * vtw->m[a][1]) + (vtw->m[a][2] * vtw->m[a][2]);

            if (sq <= 0.0f)
            {
                return HIT_CHEST;
            }
            vo.f[a] = ((vtw->m[a][0] * rel.x) + (vtw->m[a][1] * rel.y) + (vtw->m[a][2] * rel.z)) / sq;
            vd.f[a] = ((vtw->m[a][0] * dir->x) + (vtw->m[a][1] * dir->y) + (vtw->m[a][2] * dir->z)) / sq;
        }
        len = sqrtf((vd.x * vd.x) + (vd.y * vd.y) + (vd.z * vd.z));
        if (len > 0.0f)
        {
            vd.x /= len;
            vd.y /= len;
            vd.z /= len;
            hitpart = sub_GAME_7F06C010(&entry, &vo, &vd, &hitmodel, &hitnode);
            while ((hitpart == HIT_GUN) || (hitpart == HIT_HAT))
            {
                if (propobjFindHit(hitmodel, hitnode, &vo, &vd, &hitthing, &mtxindex, &dlnode))
                {
                    break;
                }
                hitpart = probably_damage_detail_blood_effect_related(&entry, &vo, &vd, &hitmodel, &hitnode);
            }
        }
    }

    g_PortStrayHitByModel = (hitpart > HIT_NULL_PART);
    if (hitpart <= HIT_NULL_PART)
    {   /* the height the ray passes the chr's vertical axis at */
        const f32 dxz = (dir->x * dir->x) + (dir->z * dir->z);
        f32 t = 0.0f;
        f32 h;

        if (dxz > 0.0f)
        {
            t = (((prop->pos.x - from->x) * dir->x) + ((prop->pos.z - from->z) * dir->z)) / dxz;
        }
        h = (chr->chrheight > 0.0f) ? ((from->y + (dir->y * t)) - chr->ground) / chr->chrheight : 0.6f;
        hitpart = (h >= 0.84f) ? HIT_HEAD
                : (h >= 0.55f) ? HIT_CHEST
                : (h >= 0.46f) ? HIT_PELVIS
                : (h >= 0.26f) ? HIT_LEFT_THIGH
                : (h >= 0.08f) ? HIT_LEFT_LEG
                : HIT_LEFT_FOOT;
    }
    return hitpart;
}
#endif


/**
 * Address: 7F022980
 *
 * Resolves a known hit against a character.
 */
void chrHandleBulletHit(struct ShotData *shot, struct BulletHit *bhit)
{
    f32 scale;
    Mtxf invmtx;
    coord3d nearhitpos;
    s32 temp;
    coord3d hitpos;
    Mtxf *mtx;
    ChrRecord *chr;
    PropRecord *prop;
    struct image_sound *sound;
    WeaponObjRecord *weaponobj;
    s32 i;
    s32 temp2;
    coord3d jointpos;
    struct image_sound *sound2;

    chr = bhit->prop->chr;

    // Calculate the view space hit position for impact effects.
    hitpos.f[0] = shot->viewOrigin.x - ((bhit->dist * shot->viewDir.x) / shot->viewDir.z);
    hitpos.f[1] = shot->viewOrigin.y - ((bhit->dist * shot->viewDir.y) / shot->viewDir.z);
    hitpos.f[2] = shot->viewOrigin.z - bhit->dist;

    scale = 1.0f - (42.0f / sqrtf(SQ(hitpos.f[0]) + SQ(hitpos.f[1]) + SQ(hitpos.f[2])));

    /** Use scale to create a near hit position closer to the camera.
     *  Probably to prevent the tracer effect and smoke puffs from landing inside the character model.
    */
    nearhitpos.x = hitpos.f[0] * scale;
    nearhitpos.y = hitpos.f[1] * scale;
    nearhitpos.z = hitpos.f[2] * scale;

    mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &nearhitpos);

    // Point the tracer effect towards the impact position.
    gunSetTracerTarget(&nearhitpos);

    // Make a fleshy impact sound.
    recall_joy2_hits_edit_detail_edit_flag(shot->weapon, bhit->prop, -1);

    chrCreateHitPuffs(bhit->prop, bhit->hitpart, &hitpos, &nearhitpos);

    // Apply damage to the character.
    if (!handles_shot_actors(chr, bhit->hitpart, &shot->dir, shot->weapon, TRUE)) {
        return;
    }

    if (bhit->hitpart == HIT_GUN) {
        for (i = 0; i != 2; i++) {
            prop = chr->weapons_held[i];

            if (prop != NULL) {
                weaponobj = (WeaponObjRecord *)prop->obj;
                /**
                 * If a character is holding an explosive such as a grenade, and the explosive is shot, detonate it.
                 */
                if (weaponobj->model == bhit->model) {
                    if (
                        ((WeaponObjRecord *)weaponobj)->weaponnum == ITEM_GRENADE ||
                        ((WeaponObjRecord *)weaponobj)->weaponnum == ITEM_GRENADEROUND ||
                        ((WeaponObjRecord *)weaponobj)->weaponnum == ITEM_ROCKETROUND ||
                        ((WeaponObjRecord *)weaponobj)->weaponnum == ITEM_TIMEDMINE ||
                        ((WeaponObjRecord *)weaponobj)->weaponnum == ITEM_BOMBCASE  ||
                        ((WeaponObjRecord *)weaponobj)->weaponnum == ITEM_REMOTEMINE  ||
                        ((WeaponObjRecord *)weaponobj)->weaponnum == ITEM_PROXIMITYMINE
                    )
                    {
                        propobjSetDropped(prop, 1);
                        chr->hidden |= 1;

                        objApplyDamage(prop->obj, gunItemGetDestructionAmount(shot->weapon), &hitpos, shot->weapon, get_cur_playernum());
                    // Create a bullet hole on the character's held weapon.
                    } else {
                        if (bhit->hit.texturenum < 0) {
                            sound = g_HitTypeSounds[0];
                        } else {
                            sound = g_HitTypeSounds[g_Textures[bhit->hit.texturenum].hitTexture];
                        }

                        temp = randomGetNext() % (s16)sound->thing2_len;
                        explosionCreateBulletImpact(&bhit->hit.hitpos, &bhit->hit.normal, sound->thing2[temp], 1, prop, bhit->room, 0);
                    }
                }
            }
        }

        return;
    }

    // Create a bullet on hole on a hat or helmet attached to a character's head.
    if (bhit->hitpart == HIT_HAT) {
        if (bhit->hit.texturenum < 0) {
            sound2 = g_HitTypeSounds[0];
        } else {
            sound2 = g_HitTypeSounds[g_Textures[bhit->hit.texturenum].hitTexture];
        }

        temp2 = randomGetNext() % (s16)sound2->thing2_len;
        explosionCreateBulletImpact(&bhit->hit.hitpos, &bhit->hit.normal, sound2->thing2[temp2], 1, chr->handle_positiondata_hat, bhit->room, 0);

        return;
    }

    mtx = modelFindNodeMtx(bhit->model, bhit->node, 0);

    jointpos.x = hitpos.f[0];
    jointpos.y = hitpos.f[1];
    jointpos.z = hitpos.f[2];

    jointpos.x += (jointpos.x - mtx->m[3][0]) * 0.5f;
    jointpos.y += (jointpos.y - mtx->m[3][1]) * 0.5f;
    jointpos.z += (jointpos.z - mtx->m[3][2]) * 0.5f;

    jointpos.x -= getjointsize(bhit->model, bhit->node) * 0.5f * shot->viewDir.x;
    jointpos.y -= getjointsize(bhit->model, bhit->node) * 0.5f * shot->viewDir.y;
    jointpos.z -= getjointsize(bhit->model, bhit->node) * 0.5f * shot->viewDir.z;

    matrix_4x4_set_inverse_rotation_and_translation(mtx, &invmtx);
    mtx4TransformVecInPlace(&invmtx, &jointpos);

    // Create a blood stain at the impact point.
    chrCreateBloodStain(bhit->model, bhit->hitpart, bhit->node, &jointpos);
}


/**
 * Removed.
 *
 * Address 0x7F022E1C.
 */
void chrRemoved7F022E1C(f32 arg0)
{
    // removed
}


void setanimationdebugflag(s32 param_1)
{
  D_8002C904 = param_1;
  return;
}


/**
 * Decrements g_AnimationTablePointerCountRelated.
 * If less than zero, the variable will then be set to the
 * number of non-zero entries in animation_table_ptrs1.
 *
 * Address 0x7F022E30.
 */
void chrDecrementAnimationTablePointerCount(void)
{
    g_AnimationTablePointerCountRelated--;

    if (g_AnimationTablePointerCountRelated < 0)
    {
        for (
            g_AnimationTablePointerCountRelated = 0;
            animation_table_ptrs1[g_AnimationTablePointerCountRelated+1] != 0;
            g_AnimationTablePointerCountRelated++)
        {
            // nothing to do.
        }
    }
}


/**
 * Decrements g_AnimationTablePointerCountRelated.
 * If the entry in animation_table_ptrs1 at that index is zero
 * then the global variable will be set to zero.
 *
 * Address 0x7F022E90.
 */
void chrIncrementAnimationTablePointerCount(void)
{
    g_AnimationTablePointerCountRelated++;

    if (animation_table_ptrs1[g_AnimationTablePointerCountRelated] == 0)
    {
        g_AnimationTablePointerCountRelated = 0;
    }
}


/**
 * Address 0x7F022EC8.
 */
void chrToggleD_8002C90C(void)
{
    D_8002C90C = !D_8002C90C;
}


void sub_GAME_7F022EE0(s32 param_1){
  D_8002C910 = param_1;
}


/**
 * Iterates all guards and checks if the noise is within the hearing scale distance.
 *
 * @param noise: noise amount to check.
 *
 * Address 0x7F022EEC.
 */
void chrCheckGuardsHeardSound(f32 noise)
{
    s32 i;

    for (i=0; i<g_NumChrSlots; i++)
    {
        if (g_ChrSlots[i].model != NULL)
        {
            if (chrGetDistanceToBond(&g_ChrSlots[i]) < g_ChrSlots[i].hearingscale * (noise * 100.0f))
            {
                chrlvAlertGuardToPlayerPosition(&g_ChrSlots[i]);
            }
        }
    }
}


/**
 * Iterates g_ChrSlots. Returns the first object that (1) model
 * is not null and (2) chrnum matches index.
 *
 * Address 0x7F022FC8.
 * chrFindByLiteralId
 */
ChrRecord* chrFindByLiteralId(s32 index)
{
    s32 i;

    for (i=0; i<g_NumChrSlots; i++)
    {
#ifndef TARGET_N64
        g_PortHwWork[PORTHW_CHRFIND_STEPS]++;
#endif
        if (g_ChrSlots[i].model != NULL && g_ChrSlots[i].chrnum == index)
        {
            return &g_ChrSlots[i];
        }
    }

    return NULL;
}


/**
 * Address 0x7F02302C.
 */
PropRecord *chrGetEquippedWeaponProp(ChrRecord *self, GUNHAND hand)
{
    return self->weapons_held[hand]; //0x160
}


/**
 * Address 0x7F02303C.
 */
PropRecord *chrGetEquippedWeaponPropWithCheck(ChrRecord *self, GUNHAND hand)
{
    PropRecord *gunprop = self->weapons_held[hand];
    if (gunprop)
    {
        WeaponObjRecord *wep = gunprop->weapon;

        if (bondwalkItemCheckBitflags(wep->weaponnum, WEAPONSTATBITFLAG_HOLD_AS_GUN) == 0)
        {
            gunprop = NULL;
        }
    }
    return gunprop;
}


/**
 * Updates character collision bounds based on chracter width.
 *
 * @param arg0: prop
 * @param arg1: out parameter, will contain character collision_bounds.
 * @param arg2: out parameter, will contain 0 or 4
 * @param y_out: out parameter, will be character ground + character height
 * @param ground: out parameter, will contain character ground
 *
 * Address 0x7F02308C.
 */
void chrUpdateCollisionBounds(PropRecord *prop, rect4f **polygon, s32 *edges, f32 *y_out, f32 *ground)
{
    ChrRecord *chr;

    chr = prop->chr;

    if (
        (chr->actiontype != ACT_DIE) &&
        (chr->actiontype != ACT_DEAD) &&
        ((chr->chrflags & (CHRFLAG_00010000 | CHRFLAG_HIDDEN)) == 0) &&
        ((chr->hidden & CHRHIDDEN_MOVING) == 0)
        )
    {
        *edges = 4;
        *polygon = &chr->collision_bounds;

        //collision box is a diamond around chr

        chr->collision_bounds.f[0] = prop->pos.x + chr->chrwidth;
        chr->collision_bounds.f[1] = prop->pos.z;

        chr->collision_bounds.f[2] = prop->pos.x;
        chr->collision_bounds.f[3] = prop->pos.z + chr->chrwidth;

        chr->collision_bounds.f[4] = prop->pos.x - chr->chrwidth;
        chr->collision_bounds.f[5] = prop->pos.z;

        chr->collision_bounds.f[6] = prop->pos.x;
        chr->collision_bounds.f[7] = prop->pos.z - chr->chrwidth;

        *ground = chr->ground;
        *y_out = *ground + chr->chrheight;

        return;
    }

    *edges = 0;
}


/**
 * @param arg0: prop
 * @param width: out parameter, will be set to character width
 * @param height: out parameter, will be set to character height - 20
 * @param always_20: out parameter, will be set to 20
 *
 * Address 0x7F023160.
 */
void chrGetChrWidthHeight(PropRecord *arg0, f32 *width, f32 *height, f32 *always_20)
{
    void *temp_v0;

    ChrRecord *c = arg0->chr;

    *width = c->chrwidth;
    *height = c->chrheight - 20.0f;
    *always_20 = 20.0f;
}


/**
 * Address 0x7F023188.
 */
f32 chrGetChrGround(PropRecord *arg0)
{
    ChrRecord *c = arg0->chr;
    return c->ground;
}


/**
 * Calculate auto aim position coordinates.
 *
 * US address 7F023194.
*/
s32 chrGetOnscreenRenderBounds(PropRecord *arg0, struct coord3d *arg1, struct coord2d *arg2, struct coord2d *arg3)
{
    struct ChrRecord *temp_v1;

    temp_v1 = arg0->chr;

    if (arg0->flags & PROPFLAG_ONSCREEN)
    {
        if ((temp_v1->actiontype != ACT_DIE) && (temp_v1->actiontype != ACT_DEAD) && !(temp_v1->chrflags & CHRFLAG_NO_AUTOAIM))
        {
            struct Model *model;
            RenderPosView *model_render_pos_1;
            RenderPosView *model_render_pos_2;

            model = temp_v1->model;
            model_render_pos_1 = &model->render_pos[0];
            model_render_pos_2 = &model->render_pos[1];

            arg1->f[2] = model_render_pos_2->pos.m[3][2] + ((model_render_pos_1->pos.m[3][2] - model_render_pos_2->pos.m[3][2]) * 0.25f);

            if (arg1->f[2] < 0.0f)
            {
                arg1->f[0] = model_render_pos_2->pos.m[3][0] + ((model_render_pos_1->pos.m[3][0] - model_render_pos_2->pos.m[3][0]) * 0.25f);
                arg1->f[1] = model_render_pos_2->pos.m[3][1] + ((model_render_pos_1->pos.m[3][1] - model_render_pos_2->pos.m[3][1]) * 0.25f);

                arg3->y = 0.0f;
                arg3->x = 0.0f;

                arg2->x = 0.0f;
                arg2->y = 0.0f;

                modelGetXYExtents(model, &arg2->y, &arg2->x, &arg3->y, &arg3->x);

                return 1;
            }
        }
    }

    return 0;
}
