#ifndef _CHROBJHANDLER_H_
#define _CHROBJHANDLER_H_

#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include <snd.h>   

#ifdef VERSION_EU

#define CHROBJ_TIMETOREGEN   50
#define CHROBJ_TIMETOREGEN_F 50.0f
#define CHROBJ_GAS_TIMER 0x5dc /* 1500 */

#define PLASTIQUE_EXPLOSION_DELAY_TICKS 100

#else

#define CHROBJ_TIMETOREGEN   60
#define CHROBJ_TIMETOREGEN_F 60.0f
#define CHROBJ_GAS_TIMER 0x708 /* 1800 */
#define PLASTIQUE_EXPLOSION_DELAY_TICKS 120

#endif

extern f32 F_80030B14;
extern f32 F_80030B18;
extern f32 g_AutogunPendingDamageTick;
extern f32 g_AutogunDamageScalar;
extern f32 F_80030B24;
extern f32 g_SoloAmmoMultiplier;

extern s32 alarm_timer;
extern s32 *ptr_alarm_sfx;
extern f32 toxic_gas_sound_timer;
extern s32 activate_gas_sound_timer;
extern coord3d gasLeakSource;
extern s32 D_80030ADC;
extern f32 gasLeakTimer;
extern ALSoundState *ptr_gas_sound;
extern s32 clock_drawn_flag;
extern s32 clock_enable;
extern f32 clock_time;
extern s32 g_RemoteMineOwnerTriggerFlag;
extern s32 g_NextWeaponSlot;
extern s32 g_NextHatSlot;
extern ObjectRecord *g_LevelLoadPropSwitch;
extern LockDoorRecord *g_LevelLoadPropLockDoor;
extern ObjectRecord *g_LevelLoadPropSafeItem;
extern struct PropRecord *D_80030B0C;
extern s32 bodypartshot;
extern f32 g_AutogunPendingDamageTick;
extern f32 g_AutogunDamageScalar;
extern f32 g_SoloAmmoMultiplier;
extern struct Model *g_CurrentProjectileModel;
extern struct ModelNode *dword_CODE_bss_80075B74;

#if defined(VERSION_EU)
extern ExplosionDetailsRecordEuList object_explosion_details;
#else
extern ExplosionDetailsRecord object_explosion_details[];
#endif

/**
 * @param arg0: Prop for tank
 * @param arg1: maybe flags
 */
void                 sub_GAME_7F04F218(struct PropRecord *arg0, s32 arg1);

void                 objFreePermanently(struct ObjectRecord *obj, bool freeprop);

void                 chrobjApplySpeed(f32 *openPosition, f32 maxFrac, f32 *speedPtr, f32 accel, f32 decel, f32 maxSpeed);
void                 chrobjCallsApplySpeed(f32 *openPosition, f32 maxFrac, f32 *speedPtr, f32 accel, f32 decel, f32 maxSpeed);
Gfx                 *weaponRenderTracers(Gfx *gdl);
void                 set_color_shading_from_tile(PropRecord *prop, u8 col[4]);
void                 propobjSetDropped(PropRecord *prop, DROPTYPE droptype);
void                 objDropRecursively(PropRecord *prop);
void                 chrobjSndCreatePostEventDefault(ALSoundState *, coord3d *);
void                 alarmActivate(void);
void                 weaponSetGunfireVisible(PropRecord *prop, s32 firing);
s32                  weaponIsGunfireVisible(PropRecord *);
s32                  sub_GAME_7F0539E4(coord3d *pos); //getVolume?
#ifndef TARGET_N64
/* STEREO PANNING of positional sfx, solo only (propobj.c, beside the
 * distance-volume helpers). portSndSetListener is called once a frame from
 * bondview.c; portSndApplyPan sits beside every positional volume post.
 * portStereoPan (video.c, weak 1 in libultra_shim.c) is the midas.ini switch. */
void                 portSndSetListener(Mtxf *viewToWorld);
s32                  portSndPanForPos(coord3d *pos);
void                 portSndApplyPan(ALSoundState *state, coord3d *pos);
void                 portSndPanTick(void); /* per-field re-pan; lv.c calls it */
/* TRUE once a projectile has fallen below every room's bounding box, i.e.
 * out of the world. Used by the thrown-knife tick to bin knives thrown into
 * the void instead of letting them run the full PROJECTILE_LIFETIME_FRAMES. */
s32                  portProjectileBelowWorld(ObjectRecord *obj);
s32                  portStereoPan(void);
#endif
ObjectRecord        *create_new_item_instance_of_model(PROP modelnum, s32 weaponid);
void                 objApplyDamage(ObjectRecord *obj, f32 damage, coord3d *pos, ITEM_IDS, s32 playernum);
void                 chrobjMaybeDetonateObjectIfFlags(ObjectRecord *arg0, f32 arg1, coord3d *arg2, ITEM_IDS item, s32 arg4);
void                 sub_GAME_7F03FDA8(PropRecord *);
void                 projectileSetSticky(PropRecord *);
void                 chrobjCollisionRelated(ObjectRecord *);
void                 objChangeShading(ObjectRecord *, coord3d *, Mtxf *, StandTile *);
s32                  projectileTestPropBoundingSphere(coord3d *arg0, coord3d *arg1, coord3d *arg2, f32 arg3);
void                 sub_GAME_7F04F244(PropRecord *arg0, struct rect4f **arg1, s32 *arg2, f32 *arg3, f32 *arg4);
void                 doorActivate(DoorRecord *door, DOORSTATE State);
bool                 posIsInFrontOfDoor(PropRecord *prop, DoorRecord *door);
void                 doorsChooseSwingDirection(PropRecord *arg0, DoorRecord *arg1);
Gfx                 *chrobjRenderProp(PropRecord *arg0, Gfx *arg1, s32 arg2);

f32                  chrobjFogVisRangeRelated(PropRecord *prop, f32 size);
s32                  getPropCombinedRoomsBBox2D(PropRecord *prop, bbox2d *bbox);

// note: rgba to rgb
void                 lerp_rgba_s32_with_rgba_f32(rgba_s32 *arg0, s32 arg1, rgba_f32 *arg2);
void                 trigger_remote_mine_detonation(void);

void                 objDetach(PropRecord *prop);
void                 objFreeEmbedmentOrProjectile(PropRecord *);
void                 objBounce(ObjectRecord *obj, coord3d *arg1);
bool                 alarmIsActive(void);
void                 init_trigger_toxic_gas_effect(coord3d *source);
void                 chrSetWeaponFlag4(ChrRecord *chr, GUNHAND hand);
void                 door7F053B10(DoorRecord *door);
void                 doorActivatePortal(DoorRecord *door);
void                 doorUpdateBbox(DoorRecord *door);
s32                  sub_GAME_7F0539B8(f32 vol);
void                 monitorSetImageByNum(MonitorRecord *mon, s32 monAnimID);
void                 propweaponSetDual(WeaponObjRecord *leftweapon, WeaponObjRecord *rightweapon);
f32                  countdownTimerGetValue(void);
bool                 countdownTimerIsRunning(void);
void                 countdownTimerSetRunning(bool enable);
void                 countdownTimerSetValue(f32 time);
void                 countdownTimerSetVisible(int clocklockbits, bool unset);
void                 sub_GAME_7F04088C(ObjectRecord *baseobj, struct coord3d *pad, Mtxf *matrix, StandTile *stan, struct coord3d *pos2);
void                 sub_GAME_7F040BA0(ObjectRecord *obj, struct coord3d *pos, Mtxf *arg2, StandTile *stan2, struct coord3d *pos2);
bool                 chrEquipWeapon(WeaponObjRecord *wep, ChrRecord *chr);
bool                 objIsCollectable(PropDefHeaderRecord *obj);
TICKOP               propPickupByPlayer(PropRecord *prop, bool showstring);
TICKOP               propobjInteract(PropRecord *prop);
s32                  objGetDestroyedLevel(ObjectRecord *obj);
void                 doorActivateWrapper(PropRecord *prop);
bool                 objIsHealthy(ObjectRecord *self);
bool                 objIsMortal(ObjectRecord* obj);
KeyRecord            *weaponFindThrown(s32 ID);
bool                 check_if_toxic_gas_activated(void);
HatRecord           *hatCreate(bool musthaveprop, bool musthavemodel, ModelFileHeader *modeldef);
PropRecord*          objInitWithAutoModel(ObjectRecord* obj);
PropRecord*          objInitWithModelDef(ObjectRecord* object, ModelFileHeader* header);
Embedment           *embedmentAllocate(void);
void                 objDetach(PropRecord *prop);
void                 objUpdateThrowKnifeSound(ObjectRecord *obj);
s32                  chrobjTestPolygonsTouchingOrOverlap2D(struct rect4f *arg0, s32 arg1, struct rect4f *arg2, s32 arg3);
s32                  chrobjTestPointPolygonCollision(struct coord3d *arg0, f32 arg1, struct rect4f *arg2, s32 arg3);
void                 maybe_detonate_object_and_its_children(PropRecord *arg0, f32 arg1, struct coord3d *arg2, s32 arg3, s32 owner);
struct ModelRoData_BoundingBoxRecord* chrobjGetBboxFromObjectRecord(ObjectRecord *arg0);
void                 deactivate_alarm_sound_effect(void);
bool                 chrpropTestPointInPaddedBoundPad(coord3d* pos, f32 arg1, BoundPadRecord *boundpads);
void                 sub_GAME_7F0A1DA0(coord3d*, coord3d*, coord3d*, coord3d*, f32, f32, f32, f32, f32, f32);
void                 modelGetXYExtents(Model *model, f32 *arg1, f32 *arg2, f32 *arg3, f32 *arg4);
s32                  sub_GAME_7F0448A8(struct PropRecord *arg0);
PropRecord*          sub_GAME_7F051DD8(struct ObjectRecord* arg0, ModelFileHeader* arg1);
void                 sub_GAME_7F052030(WeaponObjRecord* arg0, ChrRecord* arg1);
void                 drop_inventory(void);
void                 sub_GAME_7F056690(void);
f32                  bondviewGetPlayerPitchRadians(void);
TICKOP               objTickPlayer(struct PropRecord* arg0);
TICKOP               weaponTickPlayer(struct PropRecord* arg0);
void                 sub_GAME_7F04DD68(DoorRecord *door);
bool                 posIsOnScreen(PropRecord *prop, coord3d *pos, f32 arg2, bool arg3);
void                 update_color_shading(rgba_u8 *dest, rgba_u8 *src);
void                 chrRenderHeldWeapon(void *renderContext, GUNHAND hand, Gfx **gdl);
HATTYPE              get_hat_model(PropRecord *prop);
s32                  objDrop(PropRecord *prop);
void                 sub_GAME_7F050DE8(Model* model);
PropRecord          *something_with_generating_object(ChrRecord *self, s32 propid, ITEM_IDS itemid, s32 flags, WeaponObjRecord *weapon, ItemModelFileRecord *prop_header);
Gfx                 *countdownTimerRender(Gfx *DL);

PropRecord *hatCreateForChr(ChrRecord *chr, s32 modelnum, u32 flags);

Gfx *process_monitor_animation_microcode(Model *model, ModelNode *node, MonitorRecord *screen, Gfx *gdl, s32 arg4, s32 arg5);

PropRecord* doorInit(DoorRecord* door, coord3d* pos, Mtxf* mtx, StandTile* stan, coord3d* coord, coord3d* centre);


bool is_clock_drawn_onscreen(void);
bool propobjFindHit(Model *model, ModelNode *startNode, coord3d *rayPos, coord3d *rayDir, struct HitThing *hitthing, s32 *dstmtxindex, ModelNode **dstnode);
void sub_GAME_7F04E9BC(PropRecord* prop, struct ShotData* shotdata);
void objHit(ShotData *shotdata, BulletHit *hit);
bool objTestForInteract(PropRecord* prop);
bool doorTestForInteract(PropRecord *prop);
TICKOP propdoorInteract(PropRecord* doorprop);
s32 objTick(struct PropRecord *prop);
void handle_alarm_gas_timer_calldamage(void);
void save_img_index_to_obj_ani_slot(MonitorRecord *mon, void *unk88);
void add_ammo_to_inventory(AMMOTYPE ammotype,int amount,int doplaysound,int dodisplaytext);
void set_sound_effect_for_weapontype_collection(ITEM_IDS weapontype);
void display_text_for_weapon_in_lower_left_corner(ITEM_IDS weaponid);
bool objGetOnscreenRenderBounds(PropRecord *prop, coord3d *arg1, struct coord2d *arg2, struct coord2d *arg3);
void hatAssignToChr(HatRecord* hat, ChrRecord* chr);
void doorDeactivatePortal(DoorRecord *door);
bool doorIsClosed(DoorRecord *door);

#endif
