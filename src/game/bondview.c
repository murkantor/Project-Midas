#include <ultra64.h>
#include <math.h>
#ifndef TARGET_N64
#include <stdio.h> /* MP bring-up spawn log */
#endif
#include <bondtypes.h>
#include <boss.h>
#include <fr.h>
#include <joy.h>
#include <music.h>
#include <snd.h>
#include <str.h>
#include <options.h>
#include "bg.h"
#include "bgroomtrans.h"
#include "blood_animation.h"
#include "bondhead.h"
#include "bondinv.h"
#include "bondview.h"
#include "chr.h"
#include "chr_b.h"
#include "chraction.h"
#include "chrai.h"
#include "debugmenu_handler.h"
#include "explosion.h"
#include "file.h"
#include "frametiming.h"
#include "front.h"
#include "glass.h"
#include "gun.h"
#include "initanitable.h"
#include "language.h"
#include "loadobjectmodel.h"
#include "lv.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "model.h"
#include "mp_music.h"
#include "mpmenu.h"
#include "objecthandler.h"
#include "objective_status.h"
#include "os_extension.h"
#include "player.h"
#include "propobj.h"
#include "quaternion.h"
#include "random.h"
#include "stan.h"
#include "stanintersection.h"
#include "textrelated.h"

#ifdef VERSION_EU

    #define BONDVIEW_AUTOAIM_TIME 0x19 /* 25 */

    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_A 0x1a
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_B 0x19
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_C 0x64

    #define BONDVIEW_UPPER_TEXT_TIMER_A 0x33
    #define BONDVIEW_UPPER_TEXT_TIMER_B 0x32
    #define BONDVIEW_UPPER_TEXT_TIMER_C 0xc8

    #define EU_CAMERA_8003642C_ASPECT 1.19047617912f

#else

    #define BONDVIEW_AUTOAIM_TIME 0x1e /* 30 */

    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_A 0x1f
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_B 0x1e
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_C 0x78

    #define BONDVIEW_UPPER_TEXT_TIMER_A 0x3d
    #define BONDVIEW_UPPER_TEXT_TIMER_B 0x3c
    #define BONDVIEW_UPPER_TEXT_TIMER_C 0xf0

#endif

/*cannonically these are both*/
#define BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH 0x97
#define BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH 0x65
/*these*/
#define MAXTALKMESSLEN 150
#define MAXMESSAGELEN 100



#if defined(VERSION_US)
    #define BONDVIEW_2ND_FONTTABLE(_param) copy_2ndfonttable
    #define BONDVIEW_1ST_FONTTABLE(_param) copy_1stfonttable
#elif defined(VERSION_JP) || defined(VERSION_EU)
    #define BONDVIEW_2ND_FONTTABLE(_param) dword_CODE_bss_jp80079CEC[_param]
    #define BONDVIEW_1ST_FONTTABLE(_param) dword_CODE_bss_jp80079Cd8[_param]
#endif


#if defined(VERSION_US) || defined(VERSION_JP)
    #define BONDVIEW_VIEW_TOP_OFFSET_1 0x0C
    #define BONDVIEW_VIEW_TOP_OFFSET_2 0x28
    #define BONDVIEW_VIEW_TOP_OFFSET_3 0x10
#elif defined(VERSION_EU)
    #define BONDVIEW_VIEW_TOP_OFFSET_1 0x16
    #define BONDVIEW_VIEW_TOP_OFFSET_2 0x32
    #define BONDVIEW_VIEW_TOP_OFFSET_3 0x14
#endif


#if defined(VERSION_EU)
    #define TANKUPDATEROTATION_SCALE 0.904799997807f
    #define TANKTURRETVERTICALANGLERELATED_SCALE 0.928399980068f
    #define TANK_UNKD0_SCALE 0.79960000515f
    #define CHR_OBJ_ACCEL_SPEED_FACTOR 0.6f
    #define CHR_OBJ_MAXSPEED 6.0f
    #define MAX_SPEED_FACTOR 0.8f
    #define TANK_DAMAGE_PENTALTY_TICKS 75

    #define TANK_VERT_ANGLE_FACTOR 0.0716000199318f
    #define TANK_VERT_ANGLE_RAD_FACTOR 0.0952f

    #define MAX_AIMLOCK_SPEED_DEFAULT 0.8344f

    #define THREE_SECOND_TICKS 150
    #define PLAYER_TICKEXPLODE_FACTOR 12

    #define CLIPPING_CLOCK_FACTOR 0.765100002289f
    #define CLIPPING_FIELD88_FACTOR 0.234899997711f
    #define CLIPPING_FIELD8C_VALUE 12
    #define CLIPPING_FIELD90_VALUE -5.625f
#else
    #define TANKUPDATEROTATION_SCALE 0.92f
    #define TANKTURRETVERTICALANGLERELATED_SCALE 0.94f
    #define TANK_UNKD0_SCALE 0.83f
    #define CHR_OBJ_ACCEL_SPEED_FACTOR 0.5f
    #define CHR_OBJ_MAXSPEED 5.0f
    #define MAX_SPEED_FACTOR 0.8f
    #define TANK_DAMAGE_PENTALTY_TICKS 90

    #define TANK_VERT_ANGLE_FACTOR 0.0600000023842f
    #define TANK_VERT_ANGLE_RAD_FACTOR 0.0799999833107f
    #define MAX_AIMLOCK_SPEED_DEFAULT 0.86f

    #define THREE_SECOND_TICKS 180
    #define PLAYER_TICKEXPLODE_FACTOR 15

    #define CLIPPING_CLOCK_FACTOR 0.8f
    #define CLIPPING_FIELD88_FACTOR 0.19999999f
    #define CLIPPING_FIELD8C_VALUE 15
    #define CLIPPING_FIELD90_VALUE -4.5f
#endif

#define FULL_CROUCH_OFFSET -100.0f

#define SPEED_REGULAR_MAX  1.0f
#define SPEED_RUN_MAX      1.25f
#define SPEED_TICK_ADJUST  0.01f
#define TANK_MAX_SPEED     15.0f


#define FLOAT_TEN_A 10.0f
#define FLOAT_TEN_B 10.00f

#include "bondview_internal.h"


// bss
coord3d g_CamFrustumTopNormal;
f32 g_CamFrustumTopOffset;
coord3d g_CamFrustumBottomNormal;
f32 g_CamFrustumBottomOffset;
coord3d g_CamFrustumLeftNormal;
f32 g_CamFrustumLeftOffset;
coord3d g_CamFrustumRightNormal;
f32 g_CamFrustumRightOffset;
f32 g_CamFrustumNearOffset;

f32 flt_CODE_bss_80079984; // unused
f32 flt_CODE_bss_80079988; // unused
f32 flt_CODE_bss_8007998C; // unused

// data
//D:80036420
s32 D_80036420 = 0;

/**
 * When set, will increment each tick until reaching a threshold value (4).
 * Then current items will be unequipped from left and run hands.
 * Address 0x80036424.
*/
s32 g_bondviewForceDisarm = 0;

//D:80036428
uintptr_t resolution = 0;
//D:8003642C
s32 cameraBufferToggle = 0;
//D:80036430
s32 cameraFrameCounter1 = 0;
//D:80036434
s32 cameraFrameCounter2 = 0;
//D:80036438
s32 camera_80036438 = 0;
//D:8003643C
s32 credits_state = 0;
//D:80036440
CreditsEntry *credits_pointer = NULL;
//D:80036444
s32 g_SurroundBondWithExplosionsFlag = 0;

//D:80036448
s32 g_PlayerIsInTank = 0;

//D:8003644C
struct PropRecord *g_WorldTankProp = NULL;

//D:80036450 cannonically bondonprop2
struct PropRecord *g_PlayerTankProp = NULL;

/**
 * Related to g_PlayerTankProp.
 * Address 0x80036454.
 */
f32 g_PlayerTankYOffset = 0;

/**
 * US address 80036458.
*/
ALSoundState * g_TankSfxState[2] = { NULL, NULL };

/**
 * min -3.749999, max +3.749999
 * Address 0x80036460.
*/
f32 g_TankTurnSpeed = 0;

/**
 * Address 0x80036464.
*/
f32 g_TankOrientationAngle = 0;

//D:80036468
f32 tank_turret_unused_angle = 0.0f;

/**
 * Argument to sinf,cosf.
 *
 * Address 0x8003646C.
 */
f32 g_TankTurretVerticalAngle = 0;

/**
 * Address 0x80036470.
*/
f32 g_TankTurretVerticalAngleRelated = 0;

/**
 * Address 0x80036474.
*/
f32 g_TankTurretOrientationAngleRad = 0;

//D:80036478
f32 g_TankTurretOrientationAngleDeg = 0;

//D:8003647C
f32 tank_turret_turn_speed = 0;

/**
 * Can enter tank, remains set once Bond is in tank.
 * Address 0x80036480.80036480
*/
s32 g_BondCanEnterTank = 0;

/**
 * Address 0x80036484.
*/
f32 g_TankTurretAngle = 0;

/**
 * Address 0x80036488.
*/
f32 g_TankTurretTurn = 0;

//D:8003648C
s32 g_ExplodeTankOnDeathFlag = 0;
//D:80036490
s32 g_TankDamagePenaltyTicks = 0;
//D:80036494
enum CAMERAMODE g_CameraMode = CAMERAMODE_NONE;
//D:80036498
enum CAMERAMODE g_CameraAfterCinema = CAMERAMODE_NONE;
//D:8003649C
s32 camera_fade_active = 0;
//D:800364A0
s32 stop_time_flag = 0;
//D:800364A4
f32 camera_transition_timer = 0;
//D:800364A8
s32 intro_camera_index = 1;
//D:800364AC
struct SetupIntroSwirl *g_IntroSwirl = NULL;
//D:800364B0
s32 is_timer_active = 1;
//D:800364B4
bool g_PlayerInvincible = FALSE;
//D:800364B8
struct SetupIntroCamera* g_CurrentSetupIntroCamera = NULL;
//D:800364BC
s32 g_SetupIntroCameraCount = 0;
//D:800364C0
struct SetupIntroCamera *ptr_random06cam_entry = NULL;

/**
 * Flag to toggle invisibility cheat.
 * 1 = visible to guards
 * 0 = not visible to guards
 *
 * Address 0x800364C4.
 */
s32 g_VisibleToGuardsFlag = TRUE;

//D:800364C8
s32 obj_collision_flag = TRUE;
//D:800364CC
f32 D_800364CC = 1.0;
//D:800364D0
f32 D_800364D0 = 1.0;
//D:800364D4
f32 D_800364D4 = 1.0;

/**
 * Address 0x800364D8.
*/
s32 g_bondviewBondDeathAnimations[] = {
    PTR_ANIM_death_forward_face_down,
    PTR_ANIM_death_forward_spin_face_up,
    PTR_ANIM_death_backward_fall_face_up1,
    PTR_ANIM_death_backward_spin_face_down_right,
    PTR_ANIM_death_backward_spin_face_up_right,
    PTR_ANIM_death_backward_spin_face_down_left,
    PTR_ANIM_death_backward_spin_face_up_left,
    PTR_ANIM_death_forward_face_down_hard,
    PTR_ANIM_death_forward_face_down_soft,
    PTR_ANIM_death_fetal_position_right,
    PTR_ANIM_death_fetal_position_left,
    PTR_ANIM_death_backward_fall_face_up2,
    0
};

/**
 * Address 0x8003650C.
*/
s32 g_bondviewBondDeathAnimationsCount = 0;

//D:80036510
enum CAMERAMODE camera_mode = CAMERAMODE_NONE;
//D:80036514
s32 g_IntroAnimationIndex = 0;

//D:80036518
struct struct_4 stage_intro_anim_table[] = {
    {PTR_ANIM_extending_left_hand, 95.0, -1.0, 0.02},
    {PTR_ANIM_fire_standing_draw_one_handed_weapon_fast, 7.0, 40.0, 0.5},
    {PTR_ANIM_draw_one_handed_weapon_and_look_around, 0.0, -1.0, 0.5},
    {PTR_ANIM_draw_one_handed_weapon_and_stand_up, 0.0, -1.0, 0.5},
    {PTR_ANIM_aim_one_handed_weapon_left_right, 0.0, -1.0, 0.5},
    {PTR_ANIM_cock_one_handed_weapon_and_turn_around, 0.0, -1.0, 0.5},
    {PTR_ANIM_cock_one_handed_weapon_turn_around_and_stand_up, 0.0, -1.0, 0.5},
    {PTR_ANIM_draw_one_handed_weapon_and_turn_around, 0.0, -1.0, 0.5},
    {PTR_ANIM_bond_eye_fire_alt, 0.0, -1.0, 0.5}
};

//D:800365A8
f32 watch_transition_time = 0.90909088;

//D:800365AC
WeaponObjRecord dummy_08_pp7_obj[] = {
    0x0100,
    0x00,
    0x08,
    PROP_CHRWPPK,
    0x4000,
    0x00000000,
    0x00000000,
    NULL,
    NULL,
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    },
    {0.0f, 0.0f, 0.0f},
    {0x00000000},
    NULL,
    NULL,
    0.0f,
    1000.0f,
    {0xff, 0xff, 0xff, 0x00},
    0xff,
    0xff,
    0xff,
    0x00,
    ITEM_WPPK,
    -1,
    -1,
    NULL
};


//D:80036634
struct DamageType g_DamageTypes[] = {
        {   0,    10.0,    60.0,    0.6,    0,    5.0,    40.0,    1.0,            0xFF,       0xFF,       0xFF}, // 1 bars
        {   0,    10.0,    60.0,    0.6,    0,    5.0,    40.0,    1.0,            0xFF,       0xFF,       0xFF},
        {   0,    10.0,    50.0,    0.6,    0,    5.0,    30.0,    0.800000011921, 0xFF,       0xFF,       0xFF},
        {   0,    10.0,    40.0,    0.6,    0,    5.0,    25.0,    0.600000023842, 0xFF,       0xFF,       0xFF},
        {   0,    10.0,    35.0,    0.6,    0,    5.0,    22.0,    0.550000011921, 0xFF,       0xFF,       0xFF},
        {   0,    10.0,    30.0,    0.6,    0,    5.0,    19.0,    0.5,            0xFF,       0xFF,       0xFF},
        {   0,    10.0,    30.0,    0.6,    0,    5.0,    17.0,    0.449999988079, 0xFF,       0xFF,       0xFF},
        {   0,    10.0,    30.0,    0.6,    0,    5.0,    15.0,    0.40000000596,  0xFF,       0xFF,       0xFF}  // 8 bars
};


//D:80036794
/**
 * The second column is how many frames before the gauge switches from showing the old health to the new health.
 * The third column is how many frames before the health display is hidden.
 */
struct HealthDisplayDuration g_HealthDisplayDurations[8] = {
    { 0, 40, 100 }, // 1 bar of health
    { 0, 30, 80 },
    { 0, 20, 60 },
    { 0, 20, 60 },
    { 0, 20, 60 },
    { 0, 20, 50 },
    { 0, 20, 50 },
    { 0, 20, 50 }  // 8 bars of health
};

/**
 * US Address 0x800367F4.
*/
struct coord3d g_DefaultMoveBondOffset = { 0 };

/**
 * struct player property `pos` .
 * US address 80036800.
 */
struct coord3d g_DefaultFrozenPlayerPos = { 0 };

/**
 * struct player property `pos2`.
 * US address 8003680C.
 */
struct coord3d g_DefaultFrozenPlayerPos2 = { 0, 0, 1.0f };

/**
 * struct player property `offset`.
 * US address 80036818.
 */
struct coord3d g_DefaultFrozenPlayerOffset = { 0, 1.0f, 0 };

/**
 * struct player property `offset`.
 * US address 80036824.
 */
struct coord3d g_DefaultFrozenMoveOffset = { 0 };

//D:80036830
struct coord3d ZeroCoordShake = { 0 };

ModelRenderData D_8003683C = {NULL,
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

//D:8003687C
coord3d ZeroCoordWatchPos = {0};
//D:80036888
s32 D_80036888 = 0; // unused/padding
//D:8003688C
coord3d ZeroCoordSpawnPos = {0};
//D:80036898
s32 status_bar_text_buffer_index = 0;
//D:8003689C
s32 display_statusbar = 0;
#ifdef BUGFIX_R0
//D:800368A0
struct font *copy_1stfonttable = 0;
//D:800368A4
struct fontchar *copy_2ndfonttable = 0;
#endif
//D:800368A8
s32 upper_text_buffer_index = 0;
//D:800368AC
s32 display_upper_text_window = 0;
//D:800368B0
s32 upper_text_window_timer = 0xFFFFFFFF;
s32 g_UpperTextDisplayFlag = 0;
//D:800368B8
DirectionLabels g_DebugCompassLabels = {{"n", "ne", "e", "se", "s", "sw", "w", "nw", "n"}};
s32 g_PlayerTickCount = 0;

//D:800368D8
struct firing_anim_struct firing_animation_groups[][6] = {
    {{pistol_firing_animation_group1, 0, 0.1, 79.0, 87.0},
     {&D_80030660[2], 0, 0.5, 0.0, -1.0},
     {&D_80030660[3], 0, 0.5, 0.0, -1.0},
     {&D_80030660[8], 0, 0.5, 0.0, -1.0},
     {&D_80030660[9], 0, 0.5, 0.0, -1.0},
     {crouched_pistol_firing_animation_group1, 0, 0.1, 56.0, 68.0}},
    {{rifle_firing_animation_group1, 0, 0.050000001, 35.0, 40.0},
     {&D_80030660[0], 0, 0.5, 0.0, -1.0},
     {&D_80030660[1], 0, 0.5, 0.0, -1.0},
     {&D_80030660[8], 0, 0.5, 0.0, -1.0},
     {&D_80030660[9], 0, 0.5, 0.0, -1.0},
     {crouched_rifle_firing_animation_groupA, 0, 0.1, 45.0, 55.0}},
    {{NULL, 0x8194, 0.25, 0.0, -1.0},
     {NULL, 0x8204, 0.5, 0.0, -1.0},
     {NULL, 0x777C, 0.5, 0.0, -1.0},
     {&D_80030660[8], 0, 0.5, 0.0, -1.0},
     {&D_80030660[9], 0, 0.5, 0.0, -1.0},
     {NULL, 0x6C18, 0.050000001, 28.0, 29.0}},
    {{doubles_firing_animation_group1, 0, 0.1, 32.0, 42.0},
     {&D_80030660[4], 0, 0.5, 0.0, -1.0},
     {&D_80030660[5], 0, 0.5, 0.0, -1.0},
     {&D_80030660[8], 0, 0.5, 0.0, -1.0},
     {&D_80030660[8], 0, 0.5, 0.0, -1.0},
     {crouched_doubles_firing_animation_group1, 0, 0.1, 37.0, 47.0}}
 };

//D:80036AB8
s32 D_80036AB8 = 2;
//D:80036ABC
s32 D_80036ABC = 0xFFFFFFFF;
//D:80036AC0
f32 D_80036AC0 = 1.0;
//D:80036AC4
f32 D_80036AC4 = 0.1;

// forward declarations

void bondviewUpdatePlayerRoom(struct player *player);
s32 chrTick(PropRecord *prop);
void bondviewDeregisterPlayerRoom(struct player *player);
void bondviewUpdateWatchZoomIn(void);
void bondviewSetPauseWatchRelated(f32 arg0);
void bondviewSetPauseWatchRelatedAlt(f32 arg0);
void bondviewStepWatchAnimation(void);
f32 bondviewGetPauseAnimationPercent(void);
void bondviewCurrentPlayerUpdateSpeedVerta(f32 value);
void bondviewCurrentPlayerUpdateSpeedTheta(f32 value);
s16 bondviewGetCurrentPlayerViewportWidth(void);
s16 bondviewGetCurrentPlayerViewportHeight(void);
s16 bondviewGetCurrentPlayerViewportUly(void);
void currentPlayerTickChrFade(void);
void currentPlayerUpdateColourScreenProperties(void);
s16 getWidth320or440(void);
s16 getHeight330or240(void);
void bondviewAdvanceCameraMode(void);
bool currentPlayerIsFadeComplete(void);
s16 get_curplayer_viewport_ulx(void);
void bondviewFrozenMoveBond(s8, s8, u16, u16);
void bondviewMovePlayerUpdateViewport(s8 arg0, s8 arg1, u16 arg2);
void bondviewUpdateCurrentRoomPosition(s32 arg0);
void trigger_solo_watch_menu(s32 arg0);
void bondviewUpdatePlayerCollisionBounds(void);
void bondviewGetTankCollisionBounds(struct rect4f *, coord3d *, f32);
void bondviewIntroCameraTextTick(void);
void bondviewUpperTextWindowTimerTick(void);
void MoveBond(s8 arg0, s8 arg1, u16 arg2, u16 arg3);
void bondviewProcessInput(s8 arg0, s8 arg1, u16 arg2, u16 arg3);
void bondviewPlayerTickDamageAndHealth(void);
void bondviewPlayerTickExplode(void);
void bondviewPlayerStopAudioForPause(void);
void bondviewWatchAnimationTick(void);
void bondviewUpdatePlayerCollisionPositionFields(void);
void bondviewTankModelRotationRelated(void);
s32 bondviewTankCollisionStatus(struct coord3d *collision_position, StandTile *arg1, f32 tank_orientation_angle, struct coord3d *arg3, struct coord3d *arg4);
s32 bondviewCallTankCollisionStatus(struct coord3d *arg0, struct StandTile *arg1, f32 arg2);
s32 sub_GAME_7F07CDD4(struct coord3d *arg0, f32 arg1, struct StandTile **arg2);
s32 bondviewTryMoveToStan(struct coord3d *arg0, struct StandTile **stan);
s32 bondviewTestLineUnobstructed(StandTile **pTile, f32 p_x, f32 p_z, f32 dest_x, f32 dest_z, s32 cdtypes, struct coord3d *coord_p, struct coord3d *coord_dest);

s32 bondviewTryFractionMovePlayerCollision(struct coord3d *next_pos, struct coord3d *collision1_pt0, struct coord3d *collision1_pt1, struct coord3d *collision2_pt0, struct coord3d *collision2_pt1);
s32 bondviewTryEdgeMovePlayerCollision(struct coord3d *prior_next_pos, struct coord3d *collision_pt0, struct coord3d *collision_pt1);
s32 bondviewTryEndHopPlayerCollision(struct coord3d *prior_next_pos, struct coord3d *collision_pt0, struct coord3d *collision_pt1);
void bondviewApplyVertaTheta(void);

f32 bheadGetBreathingValue(void);
void bondviewMoveAnimationTick(f32 speed, f32 speedforwards, f32 speedsideways);
void bondviewCalcUpdatePlayerCollision(struct coord3d *offset, s32 allow_scoot);
f32 bondviewSetupPauseTransition(bool topause);
void bondviewStartPauseTransition(f32 duration);
void bondviewStartUnpauseTransition(f32 duration);
bool bondViewIsPauseTransitioning(void);
f32 sub_GAME_7F080228(f32 arg0);
void currentPlayerSetSwayTarget(s32 value);
void currentPlayerAdjustCrouchPos(s32 value);
void bondviewUpdateSpeedSideways(s32 arg0);
void bondviewUpdateSpeedForwards(s32 arg0);
void bondviewFrozenCameraTick(u16 buttons, u16 oldbuttons, struct coord3d *pos, struct coord3d *pos2, struct coord3d *offset, struct StandTile **stan, struct coord3d *arg6);
void bondviewCalcIntroSwirlCamera(s32, f32, struct coord3d *, struct coord3d *);
s32 pickDeathCameraAngles(PropRecord *prop1, coord3d *pos, PropRecord *prop2, coord3d *collision_pos, StandTile *tile, f32 camera_dist);
Gfx* hudmsgBottomRender(Gfx* arg0);
Gfx *sub_GAME_7F08AAE8(Gfx *gdl);
Gfx *bondviewRenderCredits(Gfx *gdl);
Gfx *bondviewRenderWatch(Gfx *gdl);
Gfx *bondviewRenderGaugeBars(Gfx *gdl);

// end forward declarations

void nullsub_75(void)
{
    return;
}


void currentPlayerSetScreenSize(f32 width, f32 height)
{
    g_CurrentPlayer->c_screenwidth = width;
    g_CurrentPlayer->c_screenheight = height;
    g_CurrentPlayer->c_halfwidth = width * 0.5f;
    g_CurrentPlayer->c_halfheight = height * 0.5f;
}


void currentPlayerSetScreenPosition(f32 left, f32 top)
{
    g_CurrentPlayer->c_screenleft = left;
    g_CurrentPlayer->c_screentop = top;
}


void currentPlayerSetPerspective(f32 near, f32 fovy, f32 aspect)
{
    g_CurrentPlayer->c_perspnear = near;
    g_CurrentPlayer->c_perspfovy = fovy;
    g_CurrentPlayer->c_perspaspect = aspect;
}


void currentPlayerSetCameraScale(void)
{
	f32 fVar4;
	f32 tmp;
	f32 fVar5;
	f32 fVar2;

	g_CurrentPlayer->c_scaley = sinf(mDegToHalfRad(g_CurrentPlayer->c_perspfovy)) / (cosf(mDegToHalfRad(g_CurrentPlayer->c_perspfovy)) * g_CurrentPlayer->c_halfheight);
	g_CurrentPlayer->c_scalex = (g_CurrentPlayer->c_scaley * g_CurrentPlayer->c_perspaspect * g_CurrentPlayer->c_halfheight) / g_CurrentPlayer->c_halfwidth;

	g_CurrentPlayer->c_recipscalex = 1.0f / g_CurrentPlayer->c_scalex;
	g_CurrentPlayer->c_recipscaley = 1.0f / g_CurrentPlayer->c_scaley;

    g_CurrentPlayer->c_scalelod = g_CurrentPlayer->c_scaley;
    g_CurrentPlayer->c_scalelod60 = sinf(DegToRad(30)) / (cosf(DegToRad(30)) * 120.0f);
	g_CurrentPlayer->c_lodscalez = g_CurrentPlayer->c_scalelod / g_CurrentPlayer->c_scalelod60;
	tmp = (g_CurrentPlayer->c_lodscalez * M_U16_MAX_VALUE_F);

	if (tmp > M_U32_MAX_VALUE_F) 
    {
		g_CurrentPlayer->c_lodscalezu32 = -1;
	} 
    else 
    {
		g_CurrentPlayer->c_lodscalezu32 = tmp;
	}

	fVar2 = g_CurrentPlayer->c_halfheight * g_CurrentPlayer->c_scaley;
	fVar4 = 1.0f / sqrtf(fVar2 * fVar2 + 1.0f);
	g_CurrentPlayer->c_cameratopnorm.x = 0;
	g_CurrentPlayer->c_cameratopnorm.y = fVar4;
	g_CurrentPlayer->c_cameratopnorm.z = fVar2 * fVar4;

	fVar5 = -g_CurrentPlayer->c_halfwidth * g_CurrentPlayer->c_scalex;
	fVar4 = 1.0f / sqrtf(fVar5 * fVar5 + 1.0f);
	g_CurrentPlayer->c_cameraleftnorm.x = -fVar4;
	g_CurrentPlayer->c_cameraleftnorm.y = 0;
	g_CurrentPlayer->c_cameraleftnorm.z = -fVar5 * fVar4;
}


/**
 * Address: 7F077EEC.
 * 
 * Transforms a 2D screen coordinate to a 3D world coordinate
 *
 * 'out' looks to be a vector which probably has the length 'length'
 * It starts from the middle of the screenn.
 */
void transformAndNormalizeByLength2Dto3D(coord2d *in, coord3d *out, f32 length)
{
    f32 norm;
    f32 x;
    f32 y;
    f32 z;

    y = (g_CurrentPlayer->c_halfheight - (in->y - g_CurrentPlayer->c_screentop)) * g_CurrentPlayer->c_scaley;
    x = ((in->x - g_CurrentPlayer->c_screenleft) - g_CurrentPlayer->c_halfwidth) * g_CurrentPlayer->c_scalex;
    z = -1.0f;
    norm = length / sqrtf((x * x) + (y * y) + (z * z));
    out->x = (x * norm);
    out->y = (y * norm);
    out->z = (-1.0f * norm);
}


void scale3DCoordinates(coord3d *in, f32 value, coord3d *out)
{
    out->y = ((in->y * value) * g_CurrentPlayer->c_scaley);
    out->x = ((in->x * value) * g_CurrentPlayer->c_scalex);
}


void transform3Dto2DCoords(coord3d *in, coord2d *out)
{
    f32 inv_z = (1.0f / in->z);
    out->y = (in->y * inv_z * g_CurrentPlayer->c_recipscaley) + (g_CurrentPlayer->c_screentop + g_CurrentPlayer->c_halfheight);
    out->x = (g_CurrentPlayer->c_screenleft + g_CurrentPlayer->c_halfwidth) - (in->x * inv_z * g_CurrentPlayer->c_recipscalex);
}

#ifndef TARGET_N64
/* WIDESCREEN aim mapping: the wide 3D projection spreads x across the whole
 * window while the pinned 2D layer (crosshair, HUD) compresses it into the
 * centred 4:3 band, and crosshair_angle/field_FFC stay in PINNED coords, so any
 * 2D<->3D aim conversion must go through these. Both are affine about the
 * current player's viewport centre, use g_bondviewSceneWideFactor from this
 * frame's projection, and are the identity at 4:3. */
f32 g_bondviewSceneWideFactor = 1.0f;

f32 bondviewAimUnpinX(f32 x) /* pinned 2D coord -> same window pixel in perspective coords */
{
    f32 cx = (f32) get_curplayer_viewport_ulx()
        + 0.5f * (f32) bondviewGetCurrentPlayerViewportWidth();
    return cx + (x - cx) / g_bondviewSceneWideFactor;
}

f32 bondviewAimPinX(f32 x) /* perspective coord -> same window pixel in pinned 2D coords */
{
    f32 cx = (f32) get_curplayer_viewport_ulx()
        + 0.5f * (f32) bondviewGetCurrentPlayerViewportWidth();
    return cx + (x - cx) * g_bondviewSceneWideFactor;
}
#endif

void transform3Dto2DWithZScaling(coord3d *in, coord3d *out)
{
	f32 inv_z;

	if (in->z == 0.0f)
    {
		inv_z = -100000000000000000000.0f;
	} 
    else
    {
		inv_z = 1.0f / in->z;
	}

	out->y = in->y * inv_z * g_CurrentPlayer->c_recipscaley + (g_CurrentPlayer->c_screentop + g_CurrentPlayer->c_halfheight);
	out->x = (g_CurrentPlayer->c_screenleft + g_CurrentPlayer->c_halfwidth) - in->x * inv_z * g_CurrentPlayer->c_recipscalex;
}


void divide3DCoordinates(coord3d *in, f32 divisor, coord3d *out)
{
	out->y = in->y * (1.0f / divisor) * g_CurrentPlayer->c_recipscaley;
	out->x = in->x * (1.0f / divisor) * g_CurrentPlayer->c_recipscalex;
}


void transform3DCoordinatesWithAngle(coord3d *in, coord3d *out, f32 value1, f32 angle, f32 value2)
{
    f32 var1;
    f32 x;
    f32 y;
    f32 z;
    f32 var2 = sinf(mDegToHalfRad(angle)) / (cosf(mDegToHalfRad(angle)) * g_CurrentPlayer->c_halfheight);
    f32 var3 = (var2 * value2 * g_CurrentPlayer->c_halfheight) / g_CurrentPlayer->c_halfwidth;
    y = (g_CurrentPlayer->c_halfheight - (in->y - g_CurrentPlayer->c_screentop)) * var2;
    x = ((in->x - g_CurrentPlayer->c_screenleft) - g_CurrentPlayer->c_halfwidth) * var3;
    z = -1.0f;
    var1 = value1 / sqrtf((x * x) + (y * y) + (z * z));
    out->x = (x * var1);
    out->y = (y * var1);
    out->z = (-1.0f * var1);
}


/**
 * Unreferenced.
 *
 * Address 0x7F078258.
 */
void transform3DCoordinatesWithAngleAndValue(coord3d *in, coord3d *out, f32 angle, f32 value)
{
    f32 var1 = (cosf(mDegToHalfRad(angle)) * g_CurrentPlayer->c_halfheight) / (sinf(mDegToHalfRad(angle)) * in->f[2]);
    f32 var2 = (var1 * g_CurrentPlayer->c_halfwidth) / (value * g_CurrentPlayer->c_halfheight);

    out->f[1] = (in->f[1] * var1) + (g_CurrentPlayer->c_screentop + g_CurrentPlayer->c_halfheight);
    out->f[0] = (g_CurrentPlayer->c_screenleft + g_CurrentPlayer->c_halfwidth) - (in->f[0] * var2);
}

void currentPlayerSetMatrix10C4(Mtx *matrix) {
    g_CurrentPlayer->field_10C4 = matrix;
}

Mtx *currentPlayerGetMatrix10C4(void) {
    return g_CurrentPlayer->field_10C4;
}

void currentPlayerSetMatrix10C8(Mtx *matrix) {
    g_CurrentPlayer->field_10C8 = matrix;
}

Mtx *currentPlayerGetMatrix10C8(void) {
    return g_CurrentPlayer->field_10C8;
}

void currentPlayerSetProjectionMatrix(Mtx *matrix) {
    g_CurrentPlayer->projmatrix = matrix;
}

Mtx *currentPlayerGetProjectionMatrix(void) {
    return g_CurrentPlayer->projmatrix;
}

void set_BONDdata_field_10E0(intptr_t arg0) {
    g_CurrentPlayer->field_10E0 = arg0;
}

intptr_t get_BONDdata_field_10E0(void) {
    return g_CurrentPlayer->field_10E0;
}

void *currentPlayerSetMatrix10CC(Mtxf *matrix) {
    g_CurrentPlayer->field_10E8 = g_CurrentPlayer->field_10CC;
    g_CurrentPlayer->field_10CC = matrix;
    return matrix; /* was falling off a non-void fn (UB at -O2); no caller uses the value */
}

Mtxf *camGetWorldToScreenMtxf(void) {
    return g_CurrentPlayer->field_10CC;
}

void currentPlayerSetProjectionMatrixF(Mtxf *matrix) {
    g_CurrentPlayer->projmatrixf = matrix;
}

Mtxf *currentPlayerGetProjectionMatrixF(void) {
    return g_CurrentPlayer->projmatrixf;
}

Mtxf *currentPlayerGetMatrix10E8(void) {
    return g_CurrentPlayer->field_10E8;
}

void sub_GAME_7F078404(s32 arg0) {
    g_CurrentPlayer->field_10D0 = arg0;
}

s32 sub_GAME_7F078414(void) {
    return g_CurrentPlayer->field_10D0;
}

void currentPlayerSetViewToWorldMtxf(Mtxf *matrix) {
    g_CurrentPlayer->field_10EC = g_CurrentPlayer->viewtoworldmtxf;
    g_CurrentPlayer->viewtoworldmtxf = matrix;
#ifndef TARGET_N64
    /* STEREO PANNING: the one place the view->world matrix is installed, so
     * the one place the sfx listener can be snapshotted from the camera the
     * player is actually looking through. propobj.c copies it out - this
     * matrix lives in the per-frame dynAllocateMatrix pool. */
    portSndSetListener(matrix);
#endif
}

Mtxf *currentPlayerGetViewToWorldMtxf(void) {
    return g_CurrentPlayer->viewtoworldmtxf;
}

Mtxf *currentPlayerGetMatrix10EC(void) {
    return g_CurrentPlayer->field_10EC;
}

void sub_GAME_7F078464(intptr_t arg0) {
    g_CurrentPlayer->field_10E4 = arg0;
}

intptr_t sub_GAME_7F078474(void)
{
    return g_CurrentPlayer->field_10E4;
}

f32 getPlayer_c_lodscalez(void)
{
    return g_CurrentPlayer->c_lodscalez;
}

u32 getPlayer_c_lodscalezu32(void)
{
    return g_CurrentPlayer->c_lodscalezu32;
}

f32 getPlayer_c_screenwidth(void)
{
    return g_CurrentPlayer->c_screenwidth;
}

f32 getPlayer_c_screenheight(void)
{
    return g_CurrentPlayer->c_screenheight;
}

f32 getPlayer_c_screenleft(void)
{
    return g_CurrentPlayer->c_screenleft;
}

f32 getPlayer_c_screentop(void)
{
    return g_CurrentPlayer->c_screentop;
}

f32 getPlayer_c_perspfovy(void)
{
    return g_CurrentPlayer->c_perspfovy;
}

f32 getPlayer_c_perspaspect(void)
{
    return g_CurrentPlayer->c_perspaspect;
}

void getPlayer_c_cameratopnorm(coord3d *out)
{
    out->x = (g_CurrentPlayer->c_cameratopnorm).x;
    out->y = (g_CurrentPlayer->c_cameratopnorm).y;
    out->z = (g_CurrentPlayer->c_cameratopnorm).z;
}

void getPlayer_c_cameratopnorm_inverted_y(coord3d *out)
{
    out->x = (g_CurrentPlayer->c_cameratopnorm).x;
    out->y = -(g_CurrentPlayer->c_cameratopnorm).y;
    out->z = (g_CurrentPlayer->c_cameratopnorm).z;
}

void getPlayer_c_cameraleftnorm(coord3d *out)
{
    out->x = (g_CurrentPlayer->c_cameraleftnorm).x;
    out->y = (g_CurrentPlayer->c_cameraleftnorm).y;
    out->z = (g_CurrentPlayer->c_cameraleftnorm).z;
}

void getPlayer_c_cameraleftnorm_inverted_x(coord3d *out)
{
    out->x = -(g_CurrentPlayer->c_cameraleftnorm).x;
    out->y = (g_CurrentPlayer->c_cameraleftnorm).y;
    out->z = (g_CurrentPlayer->c_cameraleftnorm).z;
}

f32 getPlayer_c_perspnear(void)
{
    return g_CurrentPlayer->c_perspnear;
}


/**
 * Address: 7F0785DC
 *
 * Update the world space frustum planes used for object visibility tests.
 */
void bondviewUpdateFrustumPlanes()
{
    f32 h_div;
    f32 h2;
    f32 h;
    f32 nh_div;
    f32 nh2_div;
    f32 h2_div;

    h = g_CurrentPlayer->c_halfheight * g_CurrentPlayer->c_scaley;
    h_div = 1.0f / sqrtf((h * h) + 1.0f);
    h *= h_div;
    nh_div = -h_div;

    g_CamFrustumTopNormal.x = (-nh_div * g_CurrentPlayer->viewtoworldmtxf->m[1][0]) + (h * g_CurrentPlayer->viewtoworldmtxf->m[2][0]);
    g_CamFrustumTopNormal.y = (-nh_div * g_CurrentPlayer->viewtoworldmtxf->m[1][1]) + (h * g_CurrentPlayer->viewtoworldmtxf->m[2][1]);
    g_CamFrustumTopNormal.z = (-nh_div * g_CurrentPlayer->viewtoworldmtxf->m[1][2]) + (h * g_CurrentPlayer->viewtoworldmtxf->m[2][2]);

    g_CamFrustumTopOffset = (g_CamFrustumTopNormal.x * g_CurrentPlayer->viewtoworldmtxf->m[3][0])
                          + (g_CamFrustumTopNormal.y * g_CurrentPlayer->viewtoworldmtxf->m[3][1])
                          + (g_CamFrustumTopNormal.z * g_CurrentPlayer->viewtoworldmtxf->m[3][2]);

    g_CamFrustumBottomNormal.x = (nh_div * g_CurrentPlayer->viewtoworldmtxf->m[1][0]) + (h * g_CurrentPlayer->viewtoworldmtxf->m[2][0]);
    g_CamFrustumBottomNormal.y = (nh_div * g_CurrentPlayer->viewtoworldmtxf->m[1][1]) + (h * g_CurrentPlayer->viewtoworldmtxf->m[2][1]);
    g_CamFrustumBottomNormal.z = (nh_div * g_CurrentPlayer->viewtoworldmtxf->m[1][2]) + (h * g_CurrentPlayer->viewtoworldmtxf->m[2][2]);

    g_CamFrustumBottomOffset = (g_CamFrustumBottomNormal.x * g_CurrentPlayer->viewtoworldmtxf->m[3][0])
                             + (g_CamFrustumBottomNormal.y * g_CurrentPlayer->viewtoworldmtxf->m[3][1])
                             + (g_CamFrustumBottomNormal.z * g_CurrentPlayer->viewtoworldmtxf->m[3][2]);

    h2 = (-g_CurrentPlayer->c_halfwidth) * g_CurrentPlayer->c_scalex;
    h2_div = 1.0f / sqrtf((h2 * h2) + 1.0f);
    h2 *= h2_div;
    nh2_div = -h2_div;

    g_CamFrustumLeftNormal.x = (nh2_div * g_CurrentPlayer->viewtoworldmtxf->m[0][0]) - (h2 * g_CurrentPlayer->viewtoworldmtxf->m[2][0]);
    g_CamFrustumLeftNormal.y = (nh2_div * g_CurrentPlayer->viewtoworldmtxf->m[0][1]) - (h2 * g_CurrentPlayer->viewtoworldmtxf->m[2][1]);
    g_CamFrustumLeftNormal.z = (nh2_div * g_CurrentPlayer->viewtoworldmtxf->m[0][2]) - (h2 * g_CurrentPlayer->viewtoworldmtxf->m[2][2]);

    g_CamFrustumLeftOffset = (g_CamFrustumLeftNormal.x * g_CurrentPlayer->viewtoworldmtxf->m[3][0])
                           + (g_CamFrustumLeftNormal.y * g_CurrentPlayer->viewtoworldmtxf->m[3][1])
                           + (g_CamFrustumLeftNormal.z * g_CurrentPlayer->viewtoworldmtxf->m[3][2]);

    g_CamFrustumRightNormal.x = (-nh2_div * g_CurrentPlayer->viewtoworldmtxf->m[0][0]) - (h2 * g_CurrentPlayer->viewtoworldmtxf->m[2][0]);
    g_CamFrustumRightNormal.y = (-nh2_div * g_CurrentPlayer->viewtoworldmtxf->m[0][1]) - (h2 * g_CurrentPlayer->viewtoworldmtxf->m[2][1]);
    g_CamFrustumRightNormal.z = (-nh2_div * g_CurrentPlayer->viewtoworldmtxf->m[0][2]) - (h2 * g_CurrentPlayer->viewtoworldmtxf->m[2][2]);

    g_CamFrustumRightOffset = (g_CamFrustumRightNormal.x * g_CurrentPlayer->viewtoworldmtxf->m[3][0])
                            + (g_CamFrustumRightNormal.y * g_CurrentPlayer->viewtoworldmtxf->m[3][1])
                            + (g_CamFrustumRightNormal.z * g_CurrentPlayer->viewtoworldmtxf->m[3][2]);

    g_CamFrustumNearOffset = (g_CurrentPlayer->viewtoworldmtxf->m[2][0] * g_CurrentPlayer->viewtoworldmtxf->m[3][0])
                           + (g_CurrentPlayer->viewtoworldmtxf->m[2][1] * g_CurrentPlayer->viewtoworldmtxf->m[3][1])
                           + (g_CurrentPlayer->viewtoworldmtxf->m[2][2] * g_CurrentPlayer->viewtoworldmtxf->m[3][2]);
}


/**
 * Address: 7F078950
 *
 * Unreferenced.
 */
void bondviewGetFrustumTopPlane(coord3d *normal, f32 *offset)
{
    normal->x = g_CamFrustumTopNormal.x;
    normal->y = g_CamFrustumTopNormal.y;
    normal->z = g_CamFrustumTopNormal.z;
    *offset = g_CamFrustumTopOffset;
}


/**
 * Address: 7F078980
 *
 * Unreferenced.
 */
void bondviewGetFrustumBottomPlane(coord3d *normal, f32 *offset)
{
    normal->x = g_CamFrustumBottomNormal.x;
    normal->y = g_CamFrustumBottomNormal.y;
    normal->z = g_CamFrustumBottomNormal.z;
    *offset = g_CamFrustumBottomOffset;
}


/**
 * Address: 7F0789B0
 *
 * Unreferenced.
 */
void bondviewGetFrustumLeftPlane(coord3d *normal, f32 *offset)
{
    normal->x = g_CamFrustumLeftNormal.x;
    normal->y = g_CamFrustumLeftNormal.y;
    normal->z = g_CamFrustumLeftNormal.z;
    *offset = g_CamFrustumLeftOffset;
}


/**
 * Address: 7F0789E0
 *
 * Unreferenced.
 */
void bondviewGetFrustumRightPlane(coord3d *normal, f32 *offset)
{
    normal->x = g_CamFrustumRightNormal.x;
    normal->y = g_CamFrustumRightNormal.y;
    normal->z = g_CamFrustumRightNormal.z;
    *offset = g_CamFrustumRightOffset;
}


/**
 * Address: 7F078A10
 *
 * Unreferenced.
 */
void bondviewGetFrustumNearPlane(coord3d *normal, f32 *offset)
{
    normal->x = g_CurrentPlayer->viewtoworldmtxf->m[2][0];
    normal->y = g_CurrentPlayer->viewtoworldmtxf->m[2][1];
    normal->z = g_CurrentPlayer->viewtoworldmtxf->m[2][2];
    *offset = g_CamFrustumNearOffset;
}


/**
 * Check if the 3D coordinate is within the screen
 *
 * Takes dot product of some position and compares each to an associated scalar value.
 * Returns 0 if the dot product exceeds the scalar amount, 1 otherwise.
 *
 * @param pos: Applies dot product of this position against g_CurrentPlayer->viewtoworldmtxf
 * and four coords starting at g_CamFrustumLeftNormal.
 *
 * @param margin: Value added to g_CamFrustumNearOffset to compare g_CurrentPlayer->viewtoworldmtxf,
 * and the four values starting at g_CamFrustumLeftOffset.
 *
 * Address 0x7F078A58.
 */
bool camIsPosInScreen(coord3d *pos, f32 margin)
{
    if (g_CamFrustumNearOffset + margin < (g_CurrentPlayer->viewtoworldmtxf->m[2][0] * pos->f[0]) + (g_CurrentPlayer->viewtoworldmtxf->m[2][1] * pos->f[1]) + (g_CurrentPlayer->viewtoworldmtxf->m[2][2] * pos->f[2]))
    {
        return FALSE;
    }

    if (g_CamFrustumLeftOffset + margin < (g_CamFrustumLeftNormal.f[0] * pos->f[0]) + (g_CamFrustumLeftNormal.f[1] * pos->f[1]) + (g_CamFrustumLeftNormal.f[2] * pos->f[2]))
    {
        return FALSE;
    }

    if (g_CamFrustumRightOffset + margin < (g_CamFrustumRightNormal.f[0] * pos->f[0]) + (g_CamFrustumRightNormal.f[1] * pos->f[1]) + (g_CamFrustumRightNormal.f[2] * pos->f[2]))
    {
        return FALSE;
    }

    if (g_CamFrustumTopOffset + margin < (g_CamFrustumTopNormal.f[0] * pos->f[0]) + (g_CamFrustumTopNormal.f[1] * pos->f[1]) + (g_CamFrustumTopNormal.f[2] * pos->f[2]))
    {
        return FALSE;
    }

    if (g_CamFrustumBottomOffset + margin < (g_CamFrustumBottomNormal.f[0] * pos->f[0]) + (g_CamFrustumBottomNormal.f[1] * pos->f[1]) + (g_CamFrustumBottomNormal.f[2] * pos->f[2]))
    {
        return FALSE;
    }

    return TRUE;
}


/**
 * Similar to the above function but checks if the 3D point is within an arbitrary box instead of the whole screen.
 * 
 * @param pos: 3D coordinate in absolute world space.
 * 
 * @param margin: is a slack in world units applied as a sphere around the point. The point is rejected only
 * if it is more than 'margin' outside a box plane.
 * 
 * @param box: screen space rectangle with 'min' being the top-left corner and 'max' the bottom-right corner.
 */
bool camIsPosInScreenBox(coord3d *pos, f32 margin, bbox2d *box)
{
    coord3d topnormal;
    f32 topoffset;
    coord3d bottomnormal;
    f32 bottomoffset;
    coord3d leftnormal;
    f32 leftoffset;
    coord3d rightnormal;
    f32 rightoffset;
    f32 leftinvlen;
    f32 xslope;
    f32 yslope;
    f32 rightinvlen;
    f32 topinvlen;
    f32 bottominvlen;
    f32 leftneginvlen;
    f32 rightneginvlen;
    f32 topneginvlen;
    f32 bottomneginvlen;

    if (g_CamFrustumNearOffset + margin < g_CurrentPlayer->viewtoworldmtxf->m[2][0] * pos->f[0] + g_CurrentPlayer->viewtoworldmtxf->m[2][1] * pos->f[1] + g_CurrentPlayer->viewtoworldmtxf->m[2][2] * pos->f[2])
    {
        return FALSE;
    }

    xslope = (box->min.x - g_CurrentPlayer->c_screenleft - g_CurrentPlayer->c_halfwidth) * g_CurrentPlayer->c_scalex;

    leftinvlen = 1.0f / sqrtf(xslope * xslope + 1.0f);
    xslope *= leftinvlen;
    leftneginvlen = -leftinvlen;

    leftnormal.f[0] = leftneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[0][0] - xslope * g_CurrentPlayer->viewtoworldmtxf->m[2][0];
    leftnormal.f[1] = leftneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[0][1] - xslope * g_CurrentPlayer->viewtoworldmtxf->m[2][1];
    leftnormal.f[2] = leftneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[0][2] - xslope * g_CurrentPlayer->viewtoworldmtxf->m[2][2];

    leftoffset = leftnormal.f[0] * g_CurrentPlayer->viewtoworldmtxf->m[3][0] + leftnormal.f[1] * g_CurrentPlayer->viewtoworldmtxf->m[3][1] + leftnormal.f[2] * g_CurrentPlayer->viewtoworldmtxf->m[3][2];

    if (leftoffset + margin < leftnormal.f[0] * pos->f[0] + leftnormal.f[1] * pos->f[1] + leftnormal.f[2] * pos->f[2])
    {
        return FALSE;
    }

    xslope = -(box->max.x - g_CurrentPlayer->c_screenleft - g_CurrentPlayer->c_halfwidth) * g_CurrentPlayer->c_scalex;
    rightinvlen = 1.0f / sqrtf(xslope * xslope + 1.0f);
    xslope *= rightinvlen;
    rightneginvlen = -rightinvlen;

    rightnormal.f[0] = -rightneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[0][0] - xslope * g_CurrentPlayer->viewtoworldmtxf->m[2][0];
    rightnormal.f[1] = -rightneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[0][1] - xslope * g_CurrentPlayer->viewtoworldmtxf->m[2][1];
    rightnormal.f[2] = -rightneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[0][2] - xslope * g_CurrentPlayer->viewtoworldmtxf->m[2][2];

    rightoffset = rightnormal.f[0] * g_CurrentPlayer->viewtoworldmtxf->m[3][0] + rightnormal.f[1] * g_CurrentPlayer->viewtoworldmtxf->m[3][1] + rightnormal.f[2] * g_CurrentPlayer->viewtoworldmtxf->m[3][2];

    if (rightoffset + margin < rightnormal.f[0] * pos->f[0] + rightnormal.f[1] * pos->f[1] + rightnormal.f[2] * pos->f[2])
    {
        return FALSE;
    }

    yslope = (g_CurrentPlayer->c_halfheight - (box->min.y - g_CurrentPlayer->c_screentop)) * g_CurrentPlayer->c_scaley;
    topinvlen = 1.0f / sqrtf(yslope * yslope + 1.0f);
    yslope *= topinvlen;
    topneginvlen = -topinvlen;

    topnormal.f[0] = -topneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[1][0] + yslope * g_CurrentPlayer->viewtoworldmtxf->m[2][0];
    topnormal.f[1] = -topneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[1][1] + yslope * g_CurrentPlayer->viewtoworldmtxf->m[2][1];
    topnormal.f[2] = -topneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[1][2] + yslope * g_CurrentPlayer->viewtoworldmtxf->m[2][2];

    topoffset = topnormal.f[0] * g_CurrentPlayer->viewtoworldmtxf->m[3][0] + topnormal.f[1] * g_CurrentPlayer->viewtoworldmtxf->m[3][1] + topnormal.f[2] * g_CurrentPlayer->viewtoworldmtxf->m[3][2];

    if (topoffset + margin < topnormal.f[0] * pos->f[0] + topnormal.f[1] * pos->f[1] + topnormal.f[2] * pos->f[2])
    {
        return FALSE;
    }

    yslope = -(g_CurrentPlayer->c_halfheight - (box->max.y - g_CurrentPlayer->c_screentop)) * g_CurrentPlayer->c_scaley;
    bottominvlen = 1.0f / sqrtf(yslope * yslope + 1.0f);
    yslope *= bottominvlen;
    bottomneginvlen = -bottominvlen;

    bottomnormal.f[0] = bottomneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[1][0] + yslope * g_CurrentPlayer->viewtoworldmtxf->m[2][0];
    bottomnormal.f[1] = bottomneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[1][1] + yslope * g_CurrentPlayer->viewtoworldmtxf->m[2][1];
    bottomnormal.f[2] = bottomneginvlen * g_CurrentPlayer->viewtoworldmtxf->m[1][2] + yslope * g_CurrentPlayer->viewtoworldmtxf->m[2][2];

    bottomoffset = bottomnormal.f[0] * g_CurrentPlayer->viewtoworldmtxf->m[3][0] + bottomnormal.f[1] * g_CurrentPlayer->viewtoworldmtxf->m[3][1] + bottomnormal.f[2] * g_CurrentPlayer->viewtoworldmtxf->m[3][2];

    if (bottomoffset + margin < bottomnormal.f[0] * pos->f[0] + bottomnormal.f[1] * pos->f[1] + bottomnormal.f[2] * pos->f[2])
    {
        return FALSE;
    }

    return TRUE;
}


//split here makes sense to have the pd split make sense
#ifndef TARGET_N64
#include "stan.h"

/**
 * RANDOM SPAWNS: AX's "safest spawn", ported from kholdfuzion/goldeneye_src
 * (getSafestSpawn and lvl.c's per-player g_StartpadVisibility update); it
 * replaces this file's older cone-and-sight test and reduces spawn kills.
 *
 * Each frame, after bgRoomVisibilityRelated, every start pad is marked per
 * player: 1 = its room is rendered for them, 2 = its room neighbours one that
 * is (cleared while they are dead). The pick walks the pads from a random start
 * in up to seven passes, each looser than the last, skipping a pad when:
 *   - passes 0-4: it is in a room another living player renders;
 *   - passes 0-3: ...or in a room next to one they render;
 *   - passes 0-5: another living player has a line of sight to it (the
 *     guards' stan walk, walls only - doors and windows block);
 *   - every pass: another living player is nearer than 1000 / 100 / 10
 *     units (passes 0 / 1 / 2 onward).
 * The room tests are skipped at a match's first spawn (the table isn't valid
 * yet). If no pad qualifies, any pad is chosen at random.
 */
static u8 sPortStartpadVis[4][0x10];

/** lv.c, per player after bgRoomVisibilityRelated: refresh that player's row. */
void portStartpadVisibilityUpdate(void)
{
    const s32 p = get_cur_playernum();
    s32 j;

    if (p < 0 || p >= 4 || getPlayerCount() < 2)
    {
        return;
    }
    for (j = 0; j < startpadcount && j < 0x10; j++)
    {
        u8 v = 0;

        if (g_CurrentPlayer->bondhealth > 0.0f && g_Startpad[j] != NULL && g_Startpad[j]->stan != NULL)
        {
            const s32 room = getTileRoom(g_Startpad[j]->stan);

            if (getROOMID_isRendered(room) > 0)
            {
                v |= 1;
            }
            if (getROOMID_isNeighborToRendered(room) > 0)
            {
                v |= 2;
            }
        }
        sPortStartpadVis[p][j] = v;
    }
}

/** another living player (not `me`): their player record, else NULL */
static struct player *portSpawnOther(s32 i, s32 me)
{
    struct player *p;

    if (i == me)
    {
        return NULL;
    }
    p = g_playerPointers[i];
    return (p != NULL && p->prop != NULL && p->bondhealth > 0.0f) ? p : NULL;
}

/** AX's getSafestSpawn, for player `me`. */
static s32 portMpSafestSpawn(s32 me)
{
    const s32 count = getPlayerCount();
    const u8 random = (u8) randomGetNext();
    s32 just_started = FALSE;
    f32 distance = 10000.0f;
    s32 i;
    s32 j;
    s32 k;

    for (i = 0; i < count; i++)
    {
        if (g_playerPointers[i] == NULL || g_playerPointers[i]->prop == NULL)
        {
            just_started = TRUE;   /* visibility rows are not valid yet */
            break;
        }
    }

    for (j = 0; j < 7; j++)
    {
        if (j < 3)
        {
            distance /= 10.0f;   /* 1000, 100, then 10 from the third pass on */
        }
        for (i = 0; i < startpadcount; i++)
        {
            const s32 si = (i + random) % startpadcount;
            PadRecord *pad = g_Startpad[si];
            s32 bad = FALSE;

            for (k = 0; k < count && !bad; k++)
            {
                struct player *p = portSpawnOther(k, me);
                f32 dx;
                f32 dz;

                if (p == NULL)
                {
                    continue;
                }
                if (!just_started && si < 0x10)
                {
                    if (j < 5 && (sPortStartpadVis[k][si] & 1))
                    {
                        bad = TRUE;   /* in a room they are drawing */
                        break;
                    }
                    if (j < 4 && (sPortStartpadVis[k][si] & 2))
                    {
                        bad = TRUE;   /* next door to one */
                        break;
                    }
                }
                if (j < 6)
                {
                    StandTile *tile = p->prop->stan;

                    if (stanTestLineUnobstructed(&tile, p->prop->pos.x, p->prop->pos.z, pad->pos.x, pad->pos.z,
                                                 0, 0.0f, 1.0f, 0.0f, 1.0f))
                    {
                        bad = TRUE;   /* in their sight */
                        break;
                    }
                }
                dx = p->prop->pos.x - pad->pos.x;
                dz = p->prop->pos.z - pad->pos.z;
                if (sqrtf((dx * dx) + (dz * dz)) < distance)
                {
                    bad = TRUE;   /* too close */
                }
            }
            if (!bad)
            {
                return si;
            }
        }
    }
    return randomGetNext() % startpadcount;
}
#endif

s32 bondviewGetRandomSpawnPadIndex(void)
{
    PadRecord *pad;
    PropRecord *player_prop;
    s32 player_count;
    f32 diff_x;
    s32 pad_index;
    s32 player_num;
    s32 player_index;
    s32 enemy_nearby;
    s32 attempt_num;
    f32 dist;
    f32 diff_z;

    // set up initial values
    player_num = get_cur_playernum();
    player_count = getPlayerCount();
    enemy_nearby = TRUE;
#ifdef DEBUG
    osSyncPrintf("choosing a start pad for player %d\n", player_num);
#endif
#ifndef TARGET_N64
    { /* RANDOM SPAWNS (Multiplayer Options): with the option on, AX's safest
       * spawn (portMpSafestSpawn, above) replaces Rare's in-turn pad walk,
       * except during demo playback, which must pick pads the vanilla way. */
        extern s32 portMpRandomSpawns(void); /* video.c; weak 0 headless */
        extern s32 get_is_ramrom_flag(void);

        if (player_count >= 2 && startpadcount > 0 && portMpRandomSpawns() && get_is_ramrom_flag() == 0)
        {   /* AX's safest spawn (above) in place of Rare's walk */
            return portMpSafestSpawn(player_num);
        }
    }
#endif

    // loop pads until no enemy is within 1000 units
    for (attempt_num = 0; enemy_nearby && (attempt_num < startpadcount);)
    {
        attempt_num++;
        enemy_nearby = FALSE;
        g_CurrentPlayer->field_29E0++;
        pad_index = ( g_CurrentPlayer->field_29E0) % (startpadcount);
#ifdef DEBUG
        osSyncPrintf("testing pad %d\n", pad_index);
#endif

        for (player_index = 0; player_index < player_count; player_index++)
        {
            // don't consider yourself as an enemy
            if (player_index == player_num) { continue; }

            // make sure the player prop is valid
            player_prop = g_playerPointers[player_index]->prop;
            if (player_prop == 0)
            {
#ifdef DEBUG
                osSyncPrintf("Player %d has no prop\n", player_index);
#endif
                continue;
            }

            // find distance between enemy and this pad
            pad = g_Startpad[pad_index];
            diff_x = player_prop->pos.x - pad->pos.x;
            diff_z = player_prop->pos.z - pad->pos.z;
            dist = sqrtf((diff_x * diff_x) + (diff_z * diff_z));
#ifdef DEBUG
            osSyncPrintf("Distance from player %d (%f, %f)->(%f, %f)= %f\n", player_index, pad->pos.x, pad->pos.z, player_prop->pos.x, player_prop->pos.z, dist);
#endif

            // if pad is within 1000, don't pick it
            if (dist < 1000)
            {
#ifdef DEBUG
                osSyncPrintf("Too close to player %d (closer than 10m)\n", player_index);
#endif
                enemy_nearby = TRUE;
            }
        }
    }

    do {} while (0); // leftover debug code? - Probably catch Player has no Prop

    // loop pads until no enemy is within 100 units
    for (; enemy_nearby && (attempt_num < startpadcount);)
    {
        attempt_num++;
        enemy_nearby = FALSE;
        g_CurrentPlayer->field_29E0++;
        pad_index = ((s32) g_CurrentPlayer->field_29E0) % ((s32) startpadcount);
#ifdef DEBUG
        osSyncPrintf("testing pad %d (second try)\n", pad_index);
#endif

        for (player_index = 0; player_index < player_count; player_index++)
        {
            // don't consider yourself as an enemy
            if (player_index == player_num) { continue; }

            // make sure the player prop is valid
            player_prop = g_playerPointers[player_index]->prop;
            if (player_prop == 0)
            {
#ifdef DEBUG
                osSyncPrintf("Player %d has no prop\n", player_index);
#endif
                continue;
            }

            // find distance between enemy and this pad
            pad = g_Startpad[pad_index];
            diff_x = player_prop->pos.x - pad->pos.x;
            diff_z = player_prop->pos.z - pad->pos.z;
            dist = sqrtf((diff_x * diff_x) + (diff_z * diff_z));

#ifdef DEBUG
            osSyncPrintf("Distance from player %d (%f, %f)->(%f, %f)= %f\n", player_index, pad->pos.x, pad->pos.z, player_prop->pos.x, player_prop->pos.z, dist);
#endif
            // if pad is within 100, don't pick it
            if (dist < 100.f)
            {
#ifdef DEBUG
                osSyncPrintf("Too close to player %d (closer than 1m)\n", player_index);
#endif
                enemy_nearby = TRUE;
            }
        }
    }

    // if we searched through all pads and failed to find a safe one, just pick one at random
    if (enemy_nearby)
    {
#ifdef DEBUG
        osSyncPrintf("**** No decent start pad found for player %d - picking a random one ****\n", player_index);
#endif
        pad_index = (randomGetNext() % (startpadcount));
    }

    return pad_index;
}


/**
 * Resets the current player's per-life state to defaults. Position,
 * health/armour, movement speed, etc...
 * 
 * Called when a stage is loaded, then called again for each time a player respawns in MP.
 */
void init_player_BONDdata(void)
{
    if (getPlayerCount() >= 2)
    {
        g_CurrentPlayer->controldef = get_player_control_style(get_cur_playernum());
        cur_player_set_control_type(get_player_control_style(get_cur_playernum()));
    }

    g_CurrentPlayer->current_model_pos.f[0] = 0.0f;
    g_CurrentPlayer->current_model_pos.f[1] = 0.0f;
    g_CurrentPlayer->current_model_pos.f[2] = 0.0f;
    g_CurrentPlayer->previous_model_pos.f[0] = 0.0f;
    g_CurrentPlayer->previous_model_pos.f[1] = 0.0f;
    g_CurrentPlayer->previous_model_pos.f[2] = 0.0f;
    g_CurrentPlayer->current_room_pos.f[0] = 0.0f;
    g_CurrentPlayer->current_room_pos.f[1] = 0.0f;
    g_CurrentPlayer->current_room_pos.f[2] = 0.0f;
    g_CurrentPlayer->cameramode = 0;
    g_CurrentPlayer->pos.f[0] = 0.0f;
    g_CurrentPlayer->pos.f[1] = 0.0f;
    g_CurrentPlayer->pos.f[2] = 0.0f;
    g_CurrentPlayer->pos2.f[0] = 0.0f;
    g_CurrentPlayer->pos2.f[1] = 0.0f;
    g_CurrentPlayer->pos2.f[2] = 1.0f;
    g_CurrentPlayer->offset.f[0] = 0.0f;
    g_CurrentPlayer->offset.f[1] = 1.0f;
    g_CurrentPlayer->offset.f[2] = 0.0f;
    g_CurrentPlayer->pos3.f[0] = 0.0f;
    g_CurrentPlayer->pos3.f[1] = 0.0f;
    g_CurrentPlayer->pos3.f[2] = 0.0f;
    g_CurrentPlayer->cameratile = 0;
    g_CurrentPlayer->field_3C4 = 0.0f;
    g_CurrentPlayer->field_3C8 = 0.0f;
    g_CurrentPlayer->field_3CC = 1.0f;
    g_CurrentPlayer->field_84 = 0.0f;
    g_CurrentPlayer->field_88 = 0.0f;
    g_CurrentPlayer->field_8C = 0;
    g_CurrentPlayer->vertical_bounce_adjust = 0.0f;
    g_CurrentPlayer->field_94 = 0;
    g_CurrentPlayer->field_98 = 0.0f;
    g_CurrentPlayer->swaytarget = 0.0f;
    g_CurrentPlayer->swayoffset0 = 0.0f;
    g_CurrentPlayer->swayoffset2 = 0.0f;
    g_CurrentPlayer->crouchpos = CROUCH_STAND;
    g_CurrentPlayer->autocrouchpos = CROUCH_STAND;
    g_CurrentPlayer->ducking_height_offset = 0.0f;
    g_CurrentPlayer->field_A4 = 0.0f;
    g_CurrentPlayer->field_AC = 1;
    g_CurrentPlayer->field_D0 = 0;
    g_CurrentPlayer->bonddead = 0;
    g_CurrentPlayer->bondhealth = 1.0f;
    g_CurrentPlayer->bondarmour = 0.0f;
    g_CurrentPlayer->oldhealth = 1.0f;
    g_CurrentPlayer->oldarmour = 0.0f;
    g_CurrentPlayer->apparenthealth = 1.0f;
    g_CurrentPlayer->apparentarmour = 0.0f;
    g_CurrentPlayer->damageshowtime = -1;
    g_CurrentPlayer->healthshowtime = -1;
    g_CurrentPlayer->watch_pause_time = 0;
    g_CurrentPlayer->timer_1C4 = 0;
    g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x0;
    g_CurrentPlayer->outside_watch_menu = TRUE;
    g_CurrentPlayer->open_close_solo_watch_menu = FALSE;
    g_CurrentPlayer->field_1A0 = 0;
    g_CurrentPlayer->bondbreathing = 0.0f;
    g_CurrentPlayer->speedtheta = 0.0f;
    g_CurrentPlayer->vv_costheta = 1.0f;
    g_CurrentPlayer->vv_sintheta = 0.0f;
    g_CurrentPlayer->vv_verta = -4.0f;
    g_CurrentPlayer->vv_verta360 = (f32) g_CurrentPlayer->vv_verta;
    if (g_CurrentPlayer->vv_verta360 < 0.0f)
    {
        g_CurrentPlayer->vv_verta360 = (f32) (g_CurrentPlayer->vv_verta360 + 360.0f);
    }
    g_CurrentPlayer->speedverta = 0.0f;
    g_CurrentPlayer->vv_cosverta = 1.0f;
    g_CurrentPlayer->vv_sinverta = 0.0f;
    g_CurrentPlayer->speedsideways = 0.0f;
    g_CurrentPlayer->speedstrafe = 0.0f;
    g_CurrentPlayer->speedforwards = 0.0f;
    g_CurrentPlayer->speedgo = 0.0f;
    g_CurrentPlayer->speedboost = 1.0f;
    g_CurrentPlayer->speedmaxtime60 = 0;
    g_CurrentPlayer->bondshotspeed.x = 0.0f;
    g_CurrentPlayer->bondshotspeed.y = 0.0f;
    g_CurrentPlayer->bondshotspeed.z = 0.0f;
    g_CurrentPlayer->docentreupdown = FALSE;
    g_CurrentPlayer->lastupdown60 = 0;
    g_CurrentPlayer->prevupdown = FALSE;
    g_CurrentPlayer->movecentrerelease = FALSE;
    g_CurrentPlayer->lookaheadcentreenabled = TRUE;
    g_CurrentPlayer->automovecentreenabled = TRUE;
    g_CurrentPlayer->fastmovecentreenabled = FALSE;
    g_CurrentPlayer->automovecentre = TRUE;
    g_CurrentPlayer->insightaimmode = FALSE;
    g_CurrentPlayer->autoyaimenabled = TRUE;
    g_CurrentPlayer->autoaimy = 0.0f;
    g_CurrentPlayer->autoaim_target_y = NULL;
    g_CurrentPlayer->autoyaimtime60 = -1;
    g_CurrentPlayer->autoxaimenabled = TRUE;
    g_CurrentPlayer->autoaimx = 0.0f;
    g_CurrentPlayer->autoaim_target_x = NULL;
    g_CurrentPlayer->autoxaimtime60 = -1;
    g_CurrentPlayer->colourscreenred = 0xff;
    g_CurrentPlayer->colourscreengreen = 0xff;
    g_CurrentPlayer->colourscreenblue = 0xff;
    g_CurrentPlayer->colourscreenfrac = 0.0f;
    g_CurrentPlayer->colourfadetime60 = -1.0f;
    g_CurrentPlayer->colourfadetimemax60 = -1.0f;
    g_CurrentPlayer->colourfaderedold = 0xff;
    g_CurrentPlayer->colourfaderednew = 0xff;
    g_CurrentPlayer->colourfadegreenold = 0xff;
    g_CurrentPlayer->colourfadegreennew = 0xff;
    g_CurrentPlayer->colourfadeblueold = 0xff;
    g_CurrentPlayer->colourfadebluenew = 0xff;
    g_CurrentPlayer->colourfadefracold = 0.0f;
    g_CurrentPlayer->colourfadefracnew = 0.0f;
    g_CurrentPlayer->bondfadetime60 = -1.0f;
    g_CurrentPlayer->bondfadetimemax60 = -1.0f;
    g_CurrentPlayer->bondfadefracold = 0.0f;
    g_CurrentPlayer->bondfadefracnew = 0.0f;
    g_CurrentPlayer->field_42c = 2;
    g_CurrentPlayer->controldef = CONTROLLER_CONFIG_HONEY;
    g_CurrentPlayer->pause_starting_angle = 0.0f;
    g_CurrentPlayer->pause_saved_verta = 0.0f;
    g_CurrentPlayer->pause_target_verta = 0.0f;
    g_CurrentPlayer->pause_transition_time = 0.0f;
    g_CurrentPlayer->pause_transition_duration = 0.0f;
    g_CurrentPlayer->pause_state = 0;
    g_CurrentPlayer->step_in_view_watch_animation = 0;
    g_CurrentPlayer->pause_animation_counter = 0.0f;
    g_CurrentPlayer->pausing_flag = FALSE;
    g_CurrentPlayer->buttons_pressed = (u16)0;
    g_CurrentPlayer->prev_buttons_pressed = (u16)0;
    g_CurrentPlayer->field_29C0 = 15.0f;
    g_CurrentPlayer->registeredroom = -1;
    g_CurrentPlayer->field_2A08 = 0.0f;
    g_CurrentPlayer->field_2A0C = 0.0f;
    g_CurrentPlayer->field_2A6C = 0;
    g_CurrentPlayer->field_2A70 = 0;
}


void bondviewPlayerBeginLife(void)
{
    g_CurrentPlayer->eyeheight = ((g_playerPerm->player_perspective_height * 185.0f * (s32)1) - 10.0f);

    // Reset per-life counters
    g_CurrentPlayer->kills_this_life = 0;
    g_CurrentPlayer->lifestarttime60 = getMissiontimer();
    g_CurrentPlayer->healthdisplaytime = 0;

    bondinvAddInvItem(ITEM_FIST);

    if (getPlayerCount() >= 2)
    {
        currentPlayerEquipWeaponWrapper(GUNLEFT, starting_weapon[GUNLEFT]);
        currentPlayerEquipWeaponWrapper(GUNRIGHT, starting_weapon[GUNRIGHT]);

        if (g_CurrentPlayer->bodyModel == NULL)
        {
            solo_char_load();
        }
    }
}


/**
 * Here sway refers to what we commonly call lean.
 * 
 * This function is called with the following values:
 * -1 to lean left
 *  0 for no lean
 *  1 to lean right
 */
void currentPlayerSetSwayTarget(s32 value)
{
    g_CurrentPlayer->swaytarget = (value * 75.0f);
}


void currentPlayerAdjustCrouchPos(s32 value)
{
    g_CurrentPlayer->crouchpos = g_CurrentPlayer->crouchpos + value;

    if (g_CurrentPlayer->crouchpos < CROUCH_SQUAT) 
    {
        g_CurrentPlayer->crouchpos = CROUCH_SQUAT;
    } 
    else if (g_CurrentPlayer->crouchpos > CROUCH_STAND) 
    {
        g_CurrentPlayer->crouchpos = CROUCH_STAND;
    }
}


s32 currentPlayerGetCrouchPos(void)
{
    return ((g_CurrentPlayer->crouchpos < g_CurrentPlayer->autocrouchpos) ? g_CurrentPlayer->crouchpos : g_CurrentPlayer->autocrouchpos);
}


s32 playerGetCrouchPos(s32 playernum)
{
	return (g_playerPointers[playernum]->crouchpos < g_playerPointers[playernum]->autocrouchpos)
		? g_playerPointers[playernum]->crouchpos
		: g_playerPointers[playernum]->autocrouchpos;
}


void currentPlayerSetCameraMode(s32 mode)
{
    g_CurrentPlayer->cameramode = mode;
}


/**
 * Compares current player position to parameters. If different, sets current
 * player position values to parameter values.
 * Also updates related room pointer.
 *
 * Address 0x7F079A60.
 */
void bondviewSetCurrentPlayerPosition(coord3d *pos, coord3d *pos2, coord3d *offset, StandTile *tile, coord3d *stan_walk_start)
{
    StandTile *tilefromstart;
    StandTile *tilefromprev;

    if (
        (pos->f[0] != g_CurrentPlayer->pos.f[0])
        || (pos->f[1] != g_CurrentPlayer->pos.f[1])
        || (pos->f[2] != g_CurrentPlayer->pos.f[2])
        || (pos2->f[0] != g_CurrentPlayer->pos2.f[0])
        || (pos2->f[1] != g_CurrentPlayer->pos2.f[1])
        || (pos2->f[2] != g_CurrentPlayer->pos2.f[2])
        || (offset->f[0] != g_CurrentPlayer->offset.f[0])
        || (offset->f[1] != g_CurrentPlayer->offset.f[1])
        || (offset->f[2] != g_CurrentPlayer->offset.f[2])
        || (g_CurrentPlayer->cameratile == NULL))
    {
        tilefromstart = tile;

        if (walkTilesBetweenPoints_NoCallback(&tilefromstart, stan_walk_start->f[0], stan_walk_start->f[2], pos->f[0], pos->f[2]))
        {
            // @bug ...? This is either a bug or removed code, this function has no side effects.
            // Return value should used to check if point is safe for stan.
            stanTestPointWithinTileBoundsMaybe(tilefromstart, pos->f[0], pos->f[2]);
            g_CurrentPlayer->cameratile = tilefromstart;
        }
        else
        {
            if (g_CurrentPlayer->cameratile != NULL)
            {
                tilefromprev = g_CurrentPlayer->cameratile;
                if (walkTilesBetweenPoints_NoCallback(&tilefromprev, g_CurrentPlayer->pos.f[0], g_CurrentPlayer->pos.f[2], pos->f[0], pos->f[2]))
                {
                    g_CurrentPlayer->cameratile = tilefromprev;
                }
                else
                {
                    g_CurrentPlayer->cameratile = tilefromstart;
                }
            }
            else
            {
                g_CurrentPlayer->cameratile = tilefromstart;
            }
        }

        g_CurrentPlayer->pos.f[0] = pos->f[0];
        g_CurrentPlayer->pos.f[1] = pos->f[1];
        g_CurrentPlayer->pos.f[2] = pos->f[2];
        g_CurrentPlayer->pos2.f[0] = pos2->f[0];
        g_CurrentPlayer->pos2.f[1] = pos2->f[1];
        g_CurrentPlayer->pos2.f[2] = pos2->f[2];
        g_CurrentPlayer->offset.f[0] = offset->f[0];
        g_CurrentPlayer->offset.f[1] = offset->f[1];
        g_CurrentPlayer->offset.f[2] = offset->f[2];
        g_CurrentPlayer->pos3.f[0] = g_CurrentPlayer->pos.f[0];
        g_CurrentPlayer->pos3.f[2] = g_CurrentPlayer->pos.f[2];
        g_CurrentPlayer->pos3.f[1] = stanGetPositionYValue(g_CurrentPlayer->cameratile, g_CurrentPlayer->pos.f[0], g_CurrentPlayer->pos.f[2]);
    }
}
