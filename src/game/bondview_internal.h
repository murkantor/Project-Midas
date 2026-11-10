#ifndef _BONDVIEW_INTERNAL_H_
#define _BONDVIEW_INTERNAL_H_

extern coord3d g_CamFrustumTopNormal;
extern f32 g_CamFrustumTopOffset;
extern coord3d g_CamFrustumBottomNormal;
extern f32 g_CamFrustumBottomOffset;
extern coord3d g_CamFrustumLeftNormal;
extern f32 g_CamFrustumLeftOffset;
extern coord3d g_CamFrustumRightNormal;
extern f32 g_CamFrustumRightOffset;
extern f32 g_CamFrustumNearOffset;
extern vec3d g_ForceBondMoveOffset;
extern s32 g_SurroundBondWithExplosionsTicks;
extern s32 g_PlayerTickExplodeCreatePosition;
extern struct coord3d g_TankModelPositionOffset;
extern s32 g_TankEngineSfxVolume;
extern s32 g_EnterTankAudioState;
extern f32 g_TankEnteringSitHeight;
extern f32 g_TankEnteringSitHeightRemain;
extern f32 g_TankEnterBondHorizAngleDeg;
extern f32 g_TankEnterBondVertAngleDeg;
extern struct coord3d g_EnterTankCoord;
extern ITEM_IDS starting_weapon[2];
extern struct coord3d flt_CODE_bss_800799E8;
extern struct PropRecord *dword_CODE_bss_800799F4;
extern PadRecord *g_CameraLookAtBondPad;
extern CutsceneRecord *gBondViewCutscene;
extern f32 flt_CODE_bss_80079A00;
extern f32 flt_CODE_bss_80079A04;
extern f32 flt_CODE_bss_80079A08;
extern f32 flt_CODE_bss_80079A0C;
extern f32 flt_CODE_bss_80079A10;
extern s32 dword_CODE_bss_80079A14;
extern enum CAMERAMODE dword_CODE_bss_80079A18;
extern s32 dword_CODE_bss_80079A1C;
extern s32 mission_timer;
#if defined(VERSION_JP) || defined(VERSION_EU)
extern f32 watch_time_0;
#else
extern s32 watch_time_0;
#endif
extern char stringbuffer_lowerleft[5][BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH];
#if defined(BUGFIX_R1)
extern s32 dword_CODE_bss_jp80079Cd8[5];
extern s32 dword_CODE_bss_jp80079CEC[5];
#endif
extern PadRecord *g_Startpad[16];
extern s32 startpadcount;
extern s32 dword_CODE_bss_80079C6C;
#if defined(LEFTOVERDEBUG)
extern char stringbuffer_top[2][BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH];
#endif
extern StandTilePoint *dword_CODE_bss_80079DA0;
extern StandTilePoint *dword_CODE_bss_80079DA4;
extern s32 dword_CODE_bss_80079DA8[BSS_80079DA8_LENGTH];
#ifndef VERSION_EU
extern char dword_CODE_bss_80079DC8[0x3c];
#else
extern char dword_CODE_bss_80079DC8[2][BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH];
extern char dword_CODE_bss_80079EF6[0x3c];
#endif
extern f32 g_MpSwirlRotateSpeed;
extern f32 g_MpSwirlAngleDegrees;
extern f32 g_MpSwirlForwardSpeed;
extern f32 g_MpSwirlDistance;
extern s32 D_80036420;
extern s32 g_bondviewForceDisarm;
extern uintptr_t resolution;
extern s32 cameraBufferToggle;
extern s32 cameraFrameCounter1;
extern s32 cameraFrameCounter2;
extern s32 camera_80036438;
extern s32 credits_state;
extern CreditsEntry *credits_pointer;
extern s32 g_SurroundBondWithExplosionsFlag;
extern s32 g_PlayerIsInTank;
extern struct PropRecord *g_WorldTankProp;
extern struct PropRecord *g_PlayerTankProp;
extern f32 g_PlayerTankYOffset;
extern ALSoundState *g_TankSfxState[2];
extern f32 g_TankTurnSpeed;
extern f32 g_TankOrientationAngle;
extern f32 tank_turret_unused_angle;
extern f32 g_TankTurretVerticalAngle;
extern f32 g_TankTurretVerticalAngleRelated;
extern f32 g_TankTurretOrientationAngleRad;
extern f32 g_TankTurretOrientationAngleDeg;
extern f32 tank_turret_turn_speed;
extern s32 g_BondCanEnterTank;
extern f32 g_TankTurretAngle;
extern f32 g_TankTurretTurn;
extern s32 g_ExplodeTankOnDeathFlag;
extern s32 g_TankDamagePenaltyTicks;
extern enum CAMERAMODE g_CameraMode;
extern enum CAMERAMODE g_CameraAfterCinema;
extern s32 camera_fade_active;
extern s32 stop_time_flag;
extern f32 camera_transition_timer;
extern s32 intro_camera_index;
extern struct SetupIntroSwirl *g_IntroSwirl;
extern s32 is_timer_active;
extern bool g_PlayerInvincible;
extern struct SetupIntroCamera *g_CurrentSetupIntroCamera;
extern s32 g_SetupIntroCameraCount;
extern struct SetupIntroCamera *ptr_random06cam_entry;
extern s32 g_VisibleToGuardsFlag;
extern s32 obj_collision_flag;
extern f32 D_800364CC;
extern f32 D_800364D0;
extern f32 D_800364D4;
extern s32 g_bondviewBondDeathAnimations[];
extern s32 g_bondviewBondDeathAnimationsCount;
extern enum CAMERAMODE camera_mode;
extern s32 g_IntroAnimationIndex;
extern struct struct_4 stage_intro_anim_table[];
extern f32 watch_transition_time;
extern WeaponObjRecord dummy_08_pp7_obj[];
extern struct DamageType g_DamageTypes[];
extern struct HealthDisplayDuration g_HealthDisplayDurations[8];
extern struct coord3d g_DefaultMoveBondOffset;
extern struct coord3d g_DefaultFrozenPlayerPos;
extern struct coord3d g_DefaultFrozenPlayerPos2;
extern struct coord3d g_DefaultFrozenPlayerOffset;
extern struct coord3d g_DefaultFrozenMoveOffset;
extern struct coord3d ZeroCoordShake;
extern ModelRenderData D_8003683C;
extern coord3d ZeroCoordWatchPos;
extern coord3d ZeroCoordSpawnPos;
extern s32 status_bar_text_buffer_index;
extern s32 display_statusbar;
#ifdef BUGFIX_R0
extern struct font *copy_1stfonttable;
extern struct fontchar *copy_2ndfonttable;
#endif
extern s32 upper_text_buffer_index;
extern s32 display_upper_text_window;
extern s32 upper_text_window_timer;
extern s32 g_UpperTextDisplayFlag;
extern DirectionLabels g_DebugCompassLabels;
extern s32 g_PlayerTickCount;
extern struct firing_anim_struct firing_animation_groups[][6];
extern s32 D_80036AB8;
extern s32 D_80036ABC;
extern f32 D_80036AC0;
extern f32 D_80036AC4;
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

#endif
