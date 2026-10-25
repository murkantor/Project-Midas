#include <ultra64.h>
#include <bondaicommands.h>
#include <bondgame.h>
#include <bondconstants.h>
#include "chraction.h"
#include <limits.h>
#include <math.h>
#include "include/math.h"
#include <music.h>
#include <random.h>
#include "bg.h"
#include "bgfog.h"
#include "bondhead.h"
#include "bondview.h"
#include "chr.h"
#include "chr_b.h"
#include "chrai.h"
#include "file.h"
#include "front.h"
#include "glass.h"
#include "gun.h"
#include "initanitable.h"
#include "loadobjectmodel.h"
#include "lv.h"
#include "math_asinfacosf.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "model.h"
#include "objecthandler.h"
#include "player.h"
#include "propobj.h"
#include "stan.h"
#include "frametiming.h" /* portTickCadence - see the guard accuracy note in chrlvUpdateShotbondsum */
#ifndef TARGET_N64
#include "cheat.h" /* cheatIsActive(CHEAT_PORT_ENEMY_FF) - chrlvFireWeaponRelated */
#endif

#ifndef TARGET_N64
/* Guard aim cadence for live play, in vsyncs per scoring event; vanilla used
 * the tick's own span, which tied accuracy to frame rate. 3.0 matches the N64's
 * roughly 20 fps tick (measured ~3.1 from the demo recordings), where 60Hz PC
 * play would be 1.0. */
#define GUARD_AIM_CADENCE 3.0f
#endif


point2d D_800309F0 = {0, 0};

// forward declarations

u32 weaponIsOneHanded            (PropRecord *arg0);
void chrlvIdleAnimationRelated                (ChrRecord *self, f32 arg1);
f32 chrlvGetGuard007SpeedRating               (ChrRecord *self, f32 min, f32 max);
s32 chrlvGetGuard007SpeedRatingInt            (ChrRecord *self, s32 arg1);
f32 chrlvGetGuard007ArghRating                (ChrRecord *self, f32 min, f32 max);
void chrlvKneelingAnimationRelated            (ChrRecord *self);
void chrlvIdleAnimationRelated7F023E14        (ChrRecord *self, f32 arg1);
void chrlvKneelingAnimationRelated7F023E48    (ChrRecord *self);
void chrKneelChooseAnimation                  (ChrRecord *self);
void chrlvPerformAnimationForActor            (ChrRecord *self, s32 arg1, s32 arg2, s32 arg3, u8 arg4, s32 arg5);
void chrStartAlarmChooseAnimation             (ChrRecord *self);
void chrlvThrowGrenade                        (ChrRecord *self, PropRecord *prop, GUNHAND hand, s32 startframe);
void chrlvSpotBondAnimationRelated            (ChrRecord *self, f32 arg1);
void chrlvActorShuffleFeet                    (ChrRecord *self);
void chrlvSurrenderAnimationRelated           (ChrRecord *self);
void chrlvActorLookFlustered                  (ChrRecord *self);
void chrlvActorThrowWeaponSurrender           (ChrRecord *self);
void chrlvActorFadeAway                       (ChrRecord *self);
void chrlvDeathStaggerAnimationRelated        (ChrRecord *self);
void chrlvAttackActionRelated                 (ChrRecord *self);
f32 chrlvDistanceToChrRelated                 (ChrRecord *self, s32 arg1, s32 arg2);
f32 get_distance_actor_to_position            (ChrRecord *self, coord3d *arg1);
f32 chrlvPathingCollisionRelated              (PropRecord *arg0, f32 arg1, f32 arg2, s32 cdtypes, f32 unkHeight, f32 unkA);
f32 chrlvPathingCollisionRelated7F0264B0      (PropRecord *arg0, f32 arg1, f32 arg2);
void triggered_on_shot_hit                    (ChrRecord *self, coord3d *arg1, f32 arg2, s32 req_animation_id, ITEM_IDS item);
s32 chrlvAttackAnimationRelated7F026F30       (ChrRecord *self, f32 *result);
s32 chrlvStanRoomRelated                      (ChrRecord *self, coord3d *arg1, StandTile *tile);
f32 chrlvModelScaleAnimationRelated           (ChrRecord *self);
void chrlvActGoposRelated                     (ChrRecord *self, coord3d *arg1, StandTile **arg2);
s32 chrlvMovementTargetRelated                (ChrRecord *self);
waypoint *get_ptrpreset_in_table_matching_tile           (StandTile* tile);
s32 check_if_any_path_preset_lies_on_tile     (StandTile* tile);
f32 chrlvPadPresetRelated                     (coord3d *arg0, waypoint *arg1);
waypoint *chrlvStanPathRelated                (coord3d *arg0, StandTile *arg1);
s32 chrlvStanRoomRelatedPad                   (ChrRecord *self, PadRecord *arg1);
void sub_GAME_7F025560                        (ChrRecord *self, s32 attack_type, s32 arg2);
coord3d *chrlvGetChrOrPresetLocation          (ChrRecord *self, s32 flags, s32 lookup_id, StandTile **stan);
void chrStopFiring                            (ChrRecord *self);
void sub_GAME_7F0281F4                        (ChrRecord *self);
s32 plot_course_for_actor                     (ChrRecord *self, coord3d *arg1, StandTile *stan, SPEED speed);
void chrlvPlotCourseRelated                   (ChrRecord *self);
void chrlvActGoposSetTargetPosRelated         (ChrRecord *self);
void chrlvActGoposIncCurIndex                 (ChrRecord *self);
void play_hit_soundeffect_and_proper_volume   (ChrRecord *self);
void get_sound_at_range                       (ChrRecord *self, s32 arg1, s32 arg2);
void chrlvSetGoposSegDistTotal                (ChrRecord *self, struct waydata *arg1, coord3d *arg2);
void chrlvIterateGuardSeeShotDie              (ChrRecord *, s32);
s32 chrlvCall7F02982C                         (PropRecord *arg0, coord3d *arg1, f32 arg2);
void chrlvTickSurrender                       (ChrRecord *self);
void chrlvWalkingAnimationRelated             (ChrRecord *self);
void setSeenBondTimeToNow                     (ChrRecord *guardData);
s32 chrlvAttackRelated7F0292A8                (ChrRecord *self, coord3d *arg1, StandTile *arg2);
s32 chrlvMaybeSameRoom                        (ChrRecord *self, coord3d *arg1, StandTile *arg2);
s32 chrlvCurrentPlayerCall7F0B0E24            (ChrRecord *self);
s32 chrlvCall7F0B0E24WithChrWidthHeight       (PropRecord *arg0, coord3d *arg1, coord3d *arg2);
void chrlvSetTargetToPlayer                   (ChrRecord *self);
s32 chrSawTargetRecently                      (ChrRecord *);
s32 chrCheckTargetInSight                     (ChrRecord *self);
void chrlvModelRotyRelated                    (ChrRecord *self, s32 arg1, coord3d *arg2);
s32 chrIsNotDeadOrShot                        (ChrRecord *chr);
void chrlvTickAnim                            (ChrRecord *self);
void chrlvTickDead                            (ChrRecord *self);
void chrlvTickArgh                            (ChrRecord *self);
void chrlvTickPreArgh                         (ChrRecord *self);
void chrlvTickSidestep                        (ChrRecord *self);
void chrlvTickJumpout                         (ChrRecord *self);
void chrlvTickTest                            (ChrRecord *self);
void chrlvTickStartAlarm                      (ChrRecord *self);
void chrlvTickSurprised                       (ChrRecord *self);
void sub_GAME_7F02BFE4                        (ChrRecord *self, s32 arg1, s32 arg2);
s32 chrlvSetSubroty                           (ChrRecord *self, s32 arg1, f32 arg2, f32 arg3, f32 arg4);
s32 chrlvUpdateAimendsideback                 (ChrRecord *self, struct weapon_firing_animation_table *arg1, s32 arg2, s32 arg3, f32 arg4);
void chrlvResetAimend                         (ChrRecord *self);
void chrlvToggleHiddenRelated                 (ChrRecord *self, s32 arg1, s32 arg2);
void chrlvUpdateShotbondsum                   (ChrRecord *self, s32 *arg1, s32 *arg2, ITEM_IDS item);
f32 sub_GAME_7F02C27C                         (ChrRecord *self);
void chrlvFireWeaponRelated                   (ChrRecord *self, s32 hand);
s32 chrlvAttackrollAnimationRelated7F02E2E0   (ChrRecord *self);
void chrlvAttackrollAnimationRelated7F02E3B8  (ChrRecord *self);
void sub_GAME_7F0256F0                        (ChrRecord *self, s32 attack_type, s32 arg2);
void chrlvTickAttack                          (ChrRecord *self);
void chrlvTickAttackCommon                    (ChrRecord *);
void chrlvInitActAttackWalk                   (ChrRecord *chr, s32);
void sub_GAME_7F024CF8                        (ChrRecord *self, coord3d *arg1);
void chrlvTickThrowGrenade                    (ChrRecord *self);
void chrlvTickBondIntro                       (ChrRecord *self);
void chrlvTickBondDieRemoved                  (ChrRecord *self);
s32 chrlvApplySpeed                           (ChrRecord *self, coord3d *arg1, s32 arg2, f32 *speedPtr);
void chrlvTickAttackWalk                      (ChrRecord *self);
void chrlvTickRunPos                          (ChrRecord *self);
s32 sub_GAME_7F030128                         (ChrRecord *self, coord3d *point, StandTile *arg2, coord3d *dest, StandTile * arg4, s32 cdtypes);
s32 sub_GAME_7F0301FC                         (ChrRecord *self, coord3d *point, StandTile *arg2, coord3d *dest, f32 arg4, s32 cdtypes);
s32 sub_GAME_7F0304AC                         (ChrRecord *self, coord3d *arg1, StandTile *arg2, coord3d *arg3, coord3d *arg4, StandTile *arg5, s32 cdtypes);
void chrlvSwapIfDiffArg2Determinate           (coord3d *arg0, coord3d *arg1, coord3d *arg2);
s32 sub_GAME_7F03081C                         (ChrRecord *self, coord3d *arg1, StandTile *arg2, coord3d *arg3, coord3d *arg4, coord3d *arg5, f32 arg6, f32 arg7, s32 cdtypes);
s32 sub_GAME_7F030D70                         (ChrRecord *self, coord3d *arg1, StandTile *arg2, coord3d *arg3, coord3d *arg4, coord3d *arg5, f32 arg6, f32 arg7, s32 cdtypes);
void chrlvTravelTickMagic                     (ChrRecord *self, struct waydata *arg1, f32 arg2, coord3d *arg3, StandTile *arg4);
void chrlvTravelTick                          (ChrRecord *, coord3d *, StandTile *, struct waydata *);
void chrlvTickGoPos                           (ChrRecord *self);
void chrlvSetNextActPatrolStepPadPos          (ChrRecord *self);
void chrlvAdvancePatrolStep                   (ChrRecord *self);
void chrlvTickPatrol                          (ChrRecord *self);
f32 get_distance_actor_to_position            (ChrRecord *self, coord3d *pos);
s32 chrResolveId                              (ChrRecord *self, s32 id);
s32 sub_GAME_7F033780                         (waypoint *arg0, coord3d *arg1, f32 angle);
s32 chrlvFindPathNeighborRelated              (coord3d *bondpos, StandTile *stan, f32 rot, u8 quadrant);
s32 chrIsPosOffScreen                         (coord3d *arg0, StandTile *arg1);
PropRecord *chrSpawnAtCoord(s32 bodynum, s32 headnum, coord3d *pos, StandTile *stan, f32 angle, AIListRecord *ailist, s32 spawnflags);
void chrlvInitActAttack                       (ChrRecord *self, struct anim_group_info ** arg1, s32 arg2, point2d *arg3, s32 attack_type, s32 arg5, s32 arg6);
s32 chrlvPatrolCalculateStep                  (ChrRecord *self, bool *forward, s32 numsteps);
bool chrlvIsPosClearOfObjectBounds            (coord3d *pos, StandTile *stan);
s32 sub_GAME_7F03130C                         (ChrRecord *self,coord3d *arg1,s32 arg2,coord3d *arg3,f32 arg4,s32 arg5,coord3d *arg6,struct waydata *arg7,f32 arg8,s32 arg9,s32 set_copy);
void chrlvTickStand                           (ChrRecord *self);
PadRecord * chrlvGetPatrolStepPad             (ChrRecord *self, s32 numsteps);



// end forward declarations


/**
 * Address 0x7F0234D0.
 */
Model * retrieve_header_for_body_and_head(s32 body, s32 head, u32 bitflags)
{
    ModelFileHeader *body_header;
    ModelFileHeader *head_header;
    s32 sunglasses;

    body_header = c_item_entries[body].header;
    head_header = NULL;

    sunglasses = 0;

    if ((bitflags & 1))
    {
        sunglasses = 1;
    }
    else if ((bitflags & 2))
    {
        sunglasses = (randomGetNext() & 1) == 0;
    }

    if ((head >= 0) && (c_item_entries[body].hasHead == 0))
    {
        head_header = c_item_entries[head].header;
    }

    return setup_chr_instance(body, head, body_header, head_header, sunglasses);
}


s32 get_current_random_body(void)
{
  return list_of_bodies[current_random_body];
}


/**
 * Address 0x7F0235AC.
 * Get a Random Male Head Only
 * @param id: Integer Index of body
 * @return an integer ID of a head to use
 */
s32 bodyChooseHead(s32 id)
{
    s32 ret;

    if (c_item_entries[id].isMale)
    {
        ret = randomGetNext() & 3;
        ret = ((s32)current_random_male_head + ret) % (s32)num_male_heads;
        ret = random_male_heads[ret];
    }
    else
    {
        ret = random_female_heads[current_random_female_head];
    }

    return ret;
}


/**
 * Get a Random head for body ID
 * @param id: Integer Index of body
 * @return an integer ID of a head to use
*/
s32 get_random_head(s32 id)
{
    return (c_item_entries[id].isMale ? random_male_heads[randomGetNext() % num_male_heads] : random_female_heads[randomGetNext() % num_female_heads]);
}


/**
 * Address 0x7F02370C.
*/
void expand_09_characters(s32 stageid, GuardRecord *arg1, s32 arg2)
{
    struct PadRecord *pad;
    struct ChrRecord *temp_v0_5;
    struct StandTile *sp54; // 84
    struct coord3d sp48; // 72
    struct PropRecord *temp_v0_4;
    struct ChrModelFileRecord *cmfr;
    f32 sp3C; // 60
    struct Model *sp38; //56
    s32 bodyid;
    s32 headid;

    pad = &g_CurrentSetup.pads[arg1->PadID];

    if (getposstan(&pad->pos, pad->stan, 20.0f, &sp48, &sp54) != 0)
    {
        headid = -1;
        bodyid = (arg1->BodyID == 0xFFFF)
            ? get_current_random_body()
            : arg1->BodyID;

        cmfr = &c_item_entries[bodyid];
        if (cmfr->hasHead == 0)
        {
            headid = (arg1->HeadID >= 0)
                ? arg1->HeadID
                : bodyChooseHead(bodyid);
        }

        sp38 = retrieve_header_for_body_and_head(bodyid, headid, (u32) arg1->bitflags);

        if (sp38 != 0)
        {
            sp3C = atan2f(pad->look.f[0], pad->look.f[2]);
            temp_v0_4 = chrAllocate(sp38, &sp48, sp3C, sp54, ailistFindById(arg1->AIListID));

            if (temp_v0_4 != 0)
            {
                chrpropActivate(temp_v0_4);
                chrpropEnable(temp_v0_4);

                temp_v0_5 = temp_v0_4->chr;
                temp_v0_5->chrnum = (s16) arg1->chrnum;
                temp_v0_5->hearingscale = ((f32)arg1->health) / 1000.0f;
                temp_v0_5->visionrange = (f32)arg1->ReactionTime;
                temp_v0_5->padpreset1 = (s16) arg1->Preset;
                temp_v0_5->chrpreset1 = (s16) arg1->chrpreset1;
                temp_v0_5->headnum = (s8) headid;
                temp_v0_5->bodynum = (s8) bodyid;

                if ((arg1->bitflags & 4) != 0)
                {
                    temp_v0_5->chrflags |= CHRFLAG_CLONE;
                }

                if ((arg1->bitflags & 8) != 0)
                {
                    temp_v0_5->chrflags |= CHRFLAG_INVINCIBLE;
                }

                arg1->Data = temp_v0_5;
            }
        }
    }
    #ifdef DEBUG
    else
    {
    osSyncPrintf("chr not reset! (prop num=%d chr num=%d stan=%s) ",arg2 + 1, arg1->chrnum, GetStanName(pad->stan));
    }
    #endif
}

/*
* above is chrlv.c
//#possible file break - rest of file is chraction.c
*/

/**
 * Address 0x7F023910.
 * Tests whether an equipped weapon uses one-handed animations.
 */
u32 weaponIsOneHanded(PropRecord *arg0)
{
    if (arg0 != NULL)
    {
        return bondwalkItemCheckBitflags(arg0->weapon->weaponnum, WEAPONSTATBITFLAG_ONLY_1_HANDED);
    }

    return 0U;
}


/**
 * Address 0x7F023948.
 */
void chrlvIdleAnimationRelated(ChrRecord *self, f32 duration)
{
    PropRecord *left;
    PropRecord *right;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    if (
        ((left != NULL) && (right != NULL))
        || ((left == NULL) && (right == NULL))
        || (weaponIsOneHanded(left) != 0)
        || (weaponIsOneHanded(right) != 0))
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_idle_unarmed), randomGetNext() & 1, 0, 0.25f, duration);
        modelSetAnimLooping(self->model, 0, 16.0f);
    }
    else if ((right != NULL) || (left != NULL))
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_idle), left != NULL, 0, 0.25f, duration);
        modelSetAnimLooping(self->model, 0, 16.0f);
        modelSetAnimEndFrame(self->model, 120.0f);
    }

    return;
}


/**
 * Address 0x7F023A94 (VERSION_US).
 * Address 0x7F023D94 (other)
 */
#ifdef REFRESH_PAL
#define RATE 1.2f
#else
#define RATE 1.0f
#endif

/**
 * At the end of a character's kneeling animation smoothly merge into their standing animation.
 */
void chrlvMergeKneelToStand(ChrRecord *self, f32 mergetime)
{
    f32 fsleep;

    chrStopFiring(self);
    self->actiontype = ACT_STAND;

    self->act_stand.prestand = 0;
    self->act_stand.face_entitytype = 0;
    self->act_stand.face_entityid = 0;
    self->act_stand.reaim = 0;
    self->act_stand.turning = 2;
    self->act_stand.checkfacingwall = 0;
    //eu bug, doesnt use pal version of CHRLV_SEEN_RECENT_CHECK) + CHRLV_DEFAULT_TIMER;
    //so temp hardcoded to 120) + 180;
    self->act_stand.wallcount = (randomGetNext() % (u32) 120) + 180;

    fsleep = mergetime;

    if (self->model->playspeed != RATE)
    {
#if defined(BUGFIX_R1)
        fsleep *= (RATE / self->model->playspeed);
#else
        fsleep = mergetime / self->model->playspeed;
#endif
    }

    if (fsleep > 127.0f)
    {
        fsleep = 127.0f;
    }

    self->sleep = (s8) (s32) fsleep;
    chrlvIdleAnimationRelated(self, mergetime);
}


/**
 * @param arg0: guard
 * @param min: min reaction speed range
 * @param max: max reaction speed range
 * Address 0x7F023B5C.
 */
f32 chrlvGetGuard007SpeedRating(ChrRecord *self, f32 min, f32 max)
{
    f32 ret;

    ret = (f32) self->speedrating;
    ret = (get_007_reaction_speed() * (100.0f - ret)) + ret;
    return ((ret * (max - min)) / 100.0f) + min;
}



/**
 * @param self: guard
 * @param scale: scale factor
 * Address 0x7F023BC0.
 */
s32 chrlvGetGuard007SpeedRatingInt(ChrRecord *self, s32 scale)
{
    s32 ret;

    ret = (s32) self->speedrating;
    ret = (s32)(get_007_reaction_speed() * (f32)(100 - ret)) + ret;
    return ((100 - ret) * scale) / 100;
}




/**
 * @param arg0: guard
 * @param min: min argh speed range
 * @param max: max argh speed range
 * Address 0x7F023C54.
 */
f32 chrlvGetGuard007ArghRating(ChrRecord *self, f32 min, f32 max)
{
    f32 ret;

    ret = (f32) self->arghrating;
    ret = (get_007_reaction_speed() * (100.0f - ret)) + ret;
    return ((ret * (max - min)) / 100.0f) + min;
}




/**
 * Address 0x7F023CB8.
 * PD: chrStand
 */
void chrlvKneelingAnimationRelated(ChrRecord *self)
{
    if (self->actiontype == ACT_KNEEL)
    {
        chrStopFiring(self);

        self->actiontype = ACT_STAND;
        self->act_stand.prestand = 1;
        self->act_stand.face_entitytype = 0;
        self->act_stand.face_entityid = 0;
        self->act_stand.reaim = 0;
        self->act_stand.turning = 2;
        self->act_stand.checkfacingwall = 0;
        // bug/typo??: this is the only code like this not adjusted for VERSION_EU
        self->act_stand.wallcount = (randomGetNext() % 120) + 180;
        self->sleep = 0;

        if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_fire_kneel_forward_one_handed_weapon_slow))
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_kneel_forward_one_handed_weapon_slow), (s32) self->model->gunhand, 109.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
            modelSetAnimEndFrame(self->model, 140.0f);
        }
        else
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_kneel_left_leg), (s32) self->model->gunhand, 120.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
            modelSetAnimEndFrame(self->model, 151.0f);
        }

        return;
    }

    chrlvMergeKneelToStand(self, 16.0f);
}



/**
 * Address 0x7F023E14.
 * PD: func0f02ed28
 */
void chrlvIdleAnimationRelated7F023E14(ChrRecord *self, f32 arg1)
{
    chrlvMergeKneelToStand(self, arg1);
    self->act_stand.checkfacingwall = 1;
}




/**
 * Address 0x7F023E48.
 * PD: chrStop
 */
void chrlvKneelingAnimationRelated7F023E48(ChrRecord *self)
{
    chrlvKneelingAnimationRelated(self);
    self->act_stand.checkfacingwall = 1;
}





/**
 * Address 0x7F023E74.
 * PD: chrKneelChooseAnimation
 */
void chrKneelChooseAnimation(ChrRecord *self)
{
    PropRecord *left;
    PropRecord *right;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);
    chrStopFiring(self);

    if ((left && right)
        || (!left && !right)
        || weaponIsOneHanded(left)
        || weaponIsOneHanded(right))
    {
        s32 r = randomGetNext() & 1;
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_kneel_forward_one_handed_weapon_slow), r, 0.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
        modelSetAnimEndFrame(self->model, 28.0f);
    }
    else if (right || left)
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_kneel_left_leg), left != NULL, 0.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
        modelSetAnimEndFrame(self->model, 27.0f);
    }

    self->actiontype = ACT_KNEEL;
    self->sleep = 0;
}



/**
 * Address 0x7F023FE4.
 */
void chrlvPerformAnimationForActor(ChrRecord *self, s32 animID, s32 startframe, s32 endframe, u8 bitfield, s32 interpol_time60)
{
    f32 startframef = (f32)startframe;
    f32 phi_f0;

    phi_f0 = 0.5f;
    if ((bitfield & ANIM_REVERSE_LOOPING_ANIMATION) != 0)
    {
        phi_f0 = -0.5f;
    }

    chrStopFiring(self);
    modelSetAnimation(self->model, (void *)animation_table_ptrs1[animID], (bitfield & ANIM_MIRROR) != 0, startframef, phi_f0, (f32)interpol_time60);

    if (endframe >= 0)
    {
        modelSetAnimEndFrame(self->model, (f32)endframe);
    }

    if ((bitfield & ANIM_TRANSLATION_SCALE_4X) != 0)
    {
        modelSetAnimTranslationScale(self->model, self->model->anim_translation_scale * 4.0f);
    }

    self->chrflags &= ~CHRFLAG_02000000;
    self->actiontype = ACT_ANIM;

    self->act_anim.unk02c = (bitfield & ANIM_UNKNOWN) != 0;
    self->act_anim.holdLastFrame = (bitfield & ANIM_LOOP_HOLD_LAST_FRAME) != 0;
    self->act_anim.playSfx       = (bitfield & ANIM_PLAY_SFX) != 0;
    self->act_anim.idleOnEnd     = (bitfield & ANIM_IDLE_POSE_WHEN_COMPLETE) != 0;
    self->act_anim.noTranslate   = (bitfield & ANIM_NO_TRANSLATION) != 0;

    if (self->act_anim.idleOnEnd)
    {
        self->sleep = (s8) interpol_time60;
    }
    else
    {
        self->sleep = 0;
    }
}



/**
 * Extend left hand = ACT_STARTALARM.
 *
 * Address 0x7F024150.
 * PD: chrStartAlarmChooseAnimation
 */
void chrStartAlarmChooseAnimation(ChrRecord *self)
{
    PropRecord *left = chrGetEquippedWeaponProp(self, GUNLEFT);
    PropRecord *right = chrGetEquippedWeaponProp(self, GUNRIGHT);
    bool flip = FALSE;

    if (left && !right)
    {
        flip = TRUE;
    }
    else if ((left && right) || (!left && !right))
    {
        flip = randomGetNext() & 1;
    }

    chrStopFiring(self);

    self->actiontype = ACT_STARTALARM;
    self->sleep = 0;

    modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_extending_left_hand), flip, 40.0f, 1.0f, 16.0f);
    modelSetAnimEndFrame(self->model, 82.0f);
}



/**
 * Address 0x7F024238.
 * 
 * Play the grenade throw animation. Takes a PropRecord* as an argument but
 * does not use it.
 * 
 * PD: chrThrowGrenade
 */
void chrlvThrowGrenade(ChrRecord *self, PropRecord *prop, GUNHAND hand, s32 startframe)
{
    chrStopFiring(self);

    self->actiontype = ACT_THROWGRENADE;
    self->sleep = 0;

    if (startframe != 0)
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_throw_grenade), hand != 0, 0.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
    }
    else
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_throw_grenade), hand != 0, 84.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
    }

    modelSetAnimEndFrame(self->model, 193.0f);
}




/**
 * Address 0x7F024334.
 */
void chrlvSpotBondAnimationRelated(ChrRecord *self, f32 arg1)
{
    PropRecord *left;
    PropRecord *right;
    s32 sp2C;
    f32 objarg4;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    sp2C = 0;
    if ((left != NULL) && (right == NULL))
    {
        sp2C = 1;
    }
    else if (((left != NULL) && (right != NULL)) || ((left == NULL) && (right == NULL)))
    {
        sp2C = randomGetNext() & 1;
    }

    objarg4 = chrlvGetGuard007SpeedRating(self, 0.6f, 0.96000004f); // 0.96000004 is different from 0.96
    modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_spotting_bond), sp2C, 10.0f, objarg4, arg1);
    modelSetAnimEndFrame(self->model, 52.0f);
}




/**
 * Address 0x7F024418.
 */
void chrlvActorShuffleFeet(ChrRecord *self)
{
    f32 temp_f0;

    temp_f0 = chrGetAngleToBond(self);

    if ((temp_f0 < 0.17453294f) || (temp_f0 > 6.1086526f))
    {
        chrlvSpotBondAnimationRelated(self, 16.0f);
        chrStopFiring(self);
        self->actiontype = ACT_SURPRISED;
        self->sleep = 0;

        return;
    }

    if (chrHasStoppedOrPatroling(self) == 0)
    {
        chrlvKneelingAnimationRelated(self);
    }
}



/**
 * Address 0x7F0244AC.
 */
void chrlvSurrenderAnimationRelated(ChrRecord *self)
{
    chrStopFiring(self);
    self->actiontype = ACT_SURPRISED;
    self->sleep = 0;
    modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_surrendering_armed), randomGetNext() & 1, 0.0f, chrlvGetGuard007SpeedRating(self, 0.35f, 0.56f), 16.0f);
    modelSetAnimEndFrame(self->model, 7.0f);
}



/**
 * Address 0x7F024548.
 * PD: chrSurprisedChooseAnimation
 */
void chrlvActorLookFlustered(ChrRecord *self)
{
    u32 sp2C;

    sp2C = randomGetNext() % 3U;

    chrStopFiring(self);

    self->actiontype = ACT_SURPRISED;
    self->sleep = 0;
    modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_look_around), randomGetNext() & 1, 17.0f, 0.6f, 16.0f);

    if (sp2C == 0)
    {
        modelSetAnimEndFrame(self->model, chrlvGetGuard007SpeedRating(self, 38.0f, 8.0f));
    }
    else if (sp2C == 1)
    {
        modelSetAnimEndFrame(self->model, chrlvGetGuard007SpeedRating(self, 66.0f, 8.0f));
    }
    else
    {
        modelSetAnimEndFrame(self->model, chrlvGetGuard007SpeedRating(self, 96.0f, 8.0f));
    }
}




/**
 * Address 0x7F024648.
 * PD: chrSurrenderChooseAnimation
 */
void chrlvActorThrowWeaponSurrender(ChrRecord *self)
{
    PropRecord *left;
    PropRecord *right;

    if (self->actiontype != ACT_SURRENDER)
    {
        left = chrGetEquippedWeaponProp(self, GUNLEFT);
        right = chrGetEquippedWeaponProp(self, GUNRIGHT);

        chrStopFiring(self);

        self->actiontype = ACT_SURRENDER;

        if ((right != NULL) || (left != NULL))
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_surrendering_armed_drop_weapon), randomGetNext() & 1, 0.0f, 0.5f, 16.0f);
            modelSetAnimLooping(self->model, 40.0f, 16.0f);

            self->sleep = 0x10;

            if (left != 0)
            {
                propobjSetDropped(left, 2);
            }
            if (right != 0)
            {
                propobjSetDropped(right, 2);
            }

            self->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
        }
        else
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_surrendering_armed), randomGetNext() & 1, 0.0f, 0.5f, 16.0f);
            modelSetAnimLooping(self->model, 30.0f, 16.0f);

            self->sleep = 0x10;
        }

        chrDropItems(self);
    }
}



/**
 * Address 0x7F0247B8.
 */
void chrlvActorFadeAway(ChrRecord *self)
{
    if (self->actiontype != ACT_DEAD)
    {
        chrStopFiring(self);
        self->actiontype = ACT_DEAD;
        self->act_dead.allowfade = -1;
        self->sleep = 0;
    }
}



/**
 * chrStepToSide
 * Address 0x7F024800.
 * PD: chrSidestepChooseAnimation (Somewhat similar)
 */
void chrlvSideStepAnimationRelated(ChrRecord *self, GUNHAND side)
{
    PropRecord *left;
    PropRecord *right;
    s32 sp2C;
    s32 phi_v1;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);
    sp2C = 0;
    phi_v1 = 0;

    if ((left != NULL) && (right != NULL))
    {
        sp2C = randomGetNext() & 1;
        phi_v1 = randomGetNext() & 1;
    }
    else if (weaponIsOneHanded(left) == 0)
    {
        if ((weaponIsOneHanded(right) == 0) && ((left != NULL) || (right != NULL)))
        {
            sp2C = left != 0;
            phi_v1 = randomGetNext() & 1;
        }
    }

    chrStopFiring(self);

    self->actiontype = ACT_SIDESTEP;
    self->sleep = 0;

    if (phi_v1 == 0)
    {
        if (side != GUNRIGHT)
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_side_step_left), 0, 5.0f, chrlvGetGuard007SpeedRating(self, 0.55f, 0.88000005f), 16.0f);
            modelSetAnimEndFrame(self->model, 27.0f);
        }
        else
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_side_step_left), 1, 5.0f, chrlvGetGuard007SpeedRating(self, 0.55f, 0.88000005f), 16.0f);
            modelSetAnimEndFrame(self->model, 27.0f);
        }

        return;
    }

    if (((side != GUNRIGHT) && (sp2C == 0)) ||
        ((side == GUNRIGHT) && (sp2C != 0)))
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_slide_left), sp2C, 5.0f, chrlvGetGuard007SpeedRating(self, 0.7f, 1.12f), 16.0f);
        modelSetAnimEndFrame(self->model, 34.0f);

    }
    else
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_slide_right), sp2C, 5.0f, chrlvGetGuard007SpeedRating(self, 0.7f, 1.12f), 16.0f);
        modelSetAnimEndFrame(self->model, 32.0f);
    }

    return;
}



/**
 * chrHopToSide
 * Address 0x7F024A84.
 * PD: chrSidestepChooseAnimation (somewhat similar)
 */
void chrlvFireJumpToSideAnimationRelated(ChrRecord *self, GUNHAND side)
{
    PropRecord *left;
    PropRecord *right;
    s32 side2;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    side2 = GUNRIGHT;

    if ((left != NULL) && (right == NULL))
    {
        side2 = GUNLEFT;
    }
    else if (
        ((left != NULL) && (right != NULL))
        || ((left == NULL) && (right == NULL))
        || (weaponIsOneHanded(left) != 0)
        || (weaponIsOneHanded(right) != 0))
    {
        side2 = randomGetNext() & 1;
    }

    chrStopFiring(self);

    self->actiontype = ACT_JUMPOUT;
    self->sleep = 0;

    if (((side != GUNRIGHT) && (side2 == GUNRIGHT)) ||
        ((side == GUNRIGHT) && (side2 != GUNRIGHT)))
    {
        if ((randomGetNext() & 1) != 0)
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_jump_to_side_left), side2, 5.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
            modelSetAnimEndFrame(self->model, 49.0f);
        }
        else
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_jump_to_side_right), side2, 130.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
            modelSetAnimEndFrame(self->model, 173.0f);
        }

        return;
    }

    if ((randomGetNext() & 1) != 0)
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_jump_to_side_right), side2, 20.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
        modelSetAnimEndFrame(self->model, 63.0f);
    }
    else
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_fire_jump_to_side_left), side2, 91.0f, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);
        modelSetAnimEndFrame(self->model, 136.0f);
    }

    return;
}



/**
 *  // run to coord
 * Address 0x7F024CF8 (not EU).
 * Address 0x7F024CE0 (VERSION_EU).
 * PD: chrJumpOutChooseAnimation (has a few things in common)
 */
void sub_GAME_7F024CF8(ChrRecord *self, coord3d *arg1)
{
    f32 dx;
    f32 dz;
    s32 unused;
    f32 sq;
    PropRecord *left;
    PropRecord *right;
    s32 sp2C;
    s32 phi_a2;

    dx = self->prop->pos.f[0] - arg1->f[0];
    dz = self->prop->pos.f[2] - arg1->f[2];
    sq = sqrtf((dx * dx) + (dz * dz));

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    sp2C = 1;

    if (((left != NULL) && (right != NULL)) || ((left == NULL) && (right == NULL)))
    {
        sp2C = 0;
        phi_a2 = randomGetNext() & 1;
    }
    else
    {
        if ((weaponIsOneHanded(left)) || (weaponIsOneHanded(right)))
        {
            sp2C = 0;
            phi_a2 = left != 0;
        }
        else
        {
            phi_a2 = left != 0;
        }
    }

    chrStopFiring(self);

    self->actiontype = ACT_RUNPOS;
    self->act_runpos.pos.f[0] = arg1->f[0];
    self->act_runpos.pos.f[1] = arg1->f[1];
    self->act_runpos.pos.f[2] = arg1->f[2];
    self->sleep = 0;
    self->act_runpos.turnspeed = 0;
    self->act_runpos.neardist = 30.0f;

    if (sp2C)
    {
#ifdef VERSION_EU
        self->act_runpos.eta60 = (s32) (((sq / (D_80030988 * 0.5f)) * 50.0f) / 60.0f);
#else
        self->act_runpos.eta60 = (s32) (sq / (D_80030988 * 0.5f));
#endif
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_running), phi_a2, 0, 0.5f, 16.0f);
    }
    else
    {
#ifdef VERSION_EU
        self->act_runpos.eta60 = (s32) (((sq / (D_80030994 * 0.5f)) * 50.0f) / 60.0f);
#else
        self->act_runpos.eta60 = (s32) (sq / (D_80030994 * 0.5f));
#endif
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_running_one_handed_weapon), phi_a2, 0, 0.5f, 16.0f);
    }
}



// unused / unreferenced
void chrlvDeathStaggerAnimationRelated(ChrRecord *self)
{
    chrStopFiring(self);
    self->actiontype = ACT_TEST;
    self->sleep = 0;
    modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_death_stagger_back_to_wall), 0, 10.0f, 0.5f, 16.0f);
    modelSetAnimLooping(self->model, 10.0f, 16.0f);
    modelSetAnimEndFrame(self->model, 40.0f);
}




/**
 * Called from actor_fire_or_aim_at_target_update, where action type is ACT_ATTACK.
 *
 * Address 0x7F024F8C.
 */
void chrlvAttackActionRelated(ChrRecord *self)
{
    Model* model = self->model;

    struct weapon_firing_animation_table *f = self->act_attack.animfloats;

    if ((self->act_attack.attacktype & TARGET_AIM_ONLY) != 0)
    {
        if ((f->recoil_start_frame >= 0.0f) && (f->recoil_start_frame < f->shoot_start_frame))
        {
            modelSetAnimEndFrame(model, f->recoil_start_frame);
        }
        else
        {
            modelSetAnimEndFrame(model, f->shoot_start_frame);
        }
    }
    else if (self->act_attack.unk36 != 0)
    {
        if (f->recoil_start_frame >= 0.0f)
        {
            modelSetAnimEndFrame(model, f->recoil_start_frame);
        }
        else
        {
            modelSetAnimEndFrame(model, f->shoot_start_frame);
        }
    }
    else if (f->recoil_start_frame >= 0.0f)
    {
        modelSetAnimEndFrame(model, f->recoil_start_frame);
    }
    else if (f->end_frame >= 0.0f)
    {
        modelSetAnimEndFrame(model, f->end_frame);
    }
    else
    {
        modelSetAnimEndFrame(model, -1.0f);
    }
}



/**
 * Address 0x7F0250BC.
 */
f32 chrlvDistanceToChrRelated(ChrRecord *self, s32 arg1, s32 arg2)
{
    f32 ret;
    StandTile *out_unused;

    if ((arg1 & 2) != 0)
    {
        return 0.0f;
    }

    if ((arg1 & 0x10) != 0)
    {
        ret = ((f32) arg2 * M_TAU_F) / M_U16_MAX_VALUE_F;

        ret -= getsubroty(self->model);

        if (ret < 0.0f)
        {
            ret += M_TAU_F;
        }

        return ret;
    }

    return get_distance_actor_to_position(self, chrlvGetChrOrPresetLocation(self, arg1, arg2, &out_unused));
}



 /**
  * @param self:
  * @param arg1: address of array of firing animations (example: ptr_pistol_firing_animation_groups)
  * @param arg2: flag of some sort related to calculating distance
  * @param arg3: flags
  * @param attack_type:
  * @param arg5: chrlvDistanceToChrRelated arg2
  * @param arg6: set self->act_attack.unk54 to this
  *
  * Address 0x7F02516C.
  */
void chrlvInitActAttack(ChrRecord *self, struct anim_group_info **arg1, s32 arg2, point2d *arg3, s32 attack_type, s32 arg5, s32 arg6)
{
    /**
     * Two unused stack variables, I tried to use them with the animation table
     * but couldn't get a match.
    */

    //
    Model *self_model; // 140
    s32 phi_s7;
    s32 next_anim;
    struct weapon_firing_animation_table *panim_float; // 128
    s32 unused;
    s32 unused2;
    s32 phi_s6;
    f32 dist;
    s32 anim_index;
    ChrRecord *temp_chr;
    point2d sp60; // 96
    point2d sp58; // 88
    s32 i;

    self_model = self->model;
    sp60 = D_800309A8;
    sp58 = D_800309B0;
    self->actiontype = ACT_ATTACK;
    phi_s6 = 1;
    phi_s7 = 0;

    dist = chrlvDistanceToChrRelated(self, attack_type, arg5);

    if (arg2 != 0)
    {
        anim_index = (s32) ((((M_TAU_F - dist) * 32.0f) / M_TAU_F) + 0.5f);
    }
    else
    {
        anim_index = (s32) (((dist * 32.0f) / M_TAU_F) + 0.5f);
    }

    if (anim_index >= 0x20)
    {
        anim_index = 0;
    }

    next_anim = (u32)randomGetNext() % (u32)arg1[anim_index]->len;

    // I can't get a `li t0,72` without explicit multiply, but
    // it seems array dereference would be more correct here?
    // Something like:
    //     &arg1[anim_index]->table[next_anim]
    //     arg1[anim_index]->table + next_anim
    panim_float = (struct weapon_firing_animation_table *)(
            (intptr_t)arg1[anim_index]->table + (s32)((s32)next_anim * (s32)sizeof(struct weapon_firing_animation_table))
        );

    if ((self->chrflags & CHRSTART_FORCENOBLOOD)
        && (panim_float->anim.anim == portAnimationFromOffset(PTR_ANIM_fire_hip)))
    {
        // should be:
        //     panim_float = &arg1[anim_index]->table[(next_anim + 1) % len]
        // where `len = arg1[anim_index]->len`
        panim_float = (struct weapon_firing_animation_table *)(
            (intptr_t)arg1[anim_index]->table + (s32)(((next_anim + 1) % arg1[anim_index]->len) * (s32)sizeof(struct weapon_firing_animation_table))
        );
    }

    for (i=0; i<2; i++)
    {
        if (arg3->p[i] != 0)
        {
            temp_chr = chrGetEquippedWeaponProp(self, i)->chr;

            if (bondwalkItemGetAutomaticFiringRate((s32) temp_chr->act_attack.attack_item) < 0)
            {
                sp60.p[i] = 1;
                if ((s32)temp_chr->act_attack.attack_item == ITEM_LASER)
                {
                    phi_s6 = 0;
                }
            }
            else
            {
                phi_s6 = 0;
                phi_s7 = 1;
            }

            if (((s32)temp_chr->act_attack.attack_item == ITEM_ROCKETLAUNCH) || ((s32)temp_chr->act_attack.attack_item == ITEM_GRENADELAUNCH))
            {
                sp58.p[i] = 1;
            }
        }
    }

    self->act_attack.unk30 = 1;
    self->act_attack.animfloats = panim_float;
    self->act_attack.unk31 = 0;
    self->act_attack.unk32 = (u32)randomGetNext() & 1U;
    self->act_attack.unk38[1] = arg3->p[1];
    self->act_attack.unk38[0] = arg3->p[0];
    self->act_attack.unk3a[1] = sp60.p[1];
    self->act_attack.unk3a[0] = sp60.p[0];
    self->act_attack.unk3c[1] = sp58.p[1];
    self->act_attack.unk3c[0] = sp58.p[0];
    self->act_attack.unk36 = phi_s6;
    self->act_attack.unk37 = phi_s7;
    self->act_attack.unk40 = 0;
    self->act_attack.unk33 = 0;

    if ((sp58.p[1] != 0) || (sp58.p[0] != 0))
    {
        if ((sp58.p[1] != 0) && (sp58.p[0] != 0))
        {
            self->act_attack.unk34 = 2;
        }
        else
        {
            self->act_attack.unk34 = 1;
        }
    }
    else
    {
        if ((attack_type & 0x80) != 0)
        {
            self->act_attack.unk34 = 1;
        }
        else
        {
            self->act_attack.unk34 = (randomGetNext() & 3) + 2;
        }

        if ((arg3->p[0] != 0) && (arg3->p[1] != 0))
        {
            self->act_attack.unk34 += (randomGetNext() & 3) + 2;
        }
    }

    self->act_attack.attacktype = attack_type;
    self->act_attack.entityid = arg5;
    self->act_attack.unk54 = arg6;
    self->act_attack.type_of_motion = 0;
    self->act_attack.unk44 = 0;
    self->act_attack.attack_time = 0;
    self->sleep = 0;

    modelSetAnimation(
        self_model,
        (struct ModelAnimation *) panim_float->anim.anim,
        arg2,
        panim_float->start_frame,
        chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f),
        16.0f);

    chrlvAttackActionRelated(self);
}



/**
 * Address 0x7F025560.
*/
void sub_GAME_7F025560(ChrRecord *self, s32 attack_type, s32 arg2)
{
    PropRecord *left;
    PropRecord *right;
    s32 last_arg2;
    struct anim_group_info **animation_pointer;
    point2d sp;
    PropRecord * left2;
    PropRecord * right2;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    sp = D_800309B8;

    if ((left != NULL) && (right != NULL))
    {
        left2 = chrGetEquippedWeaponPropWithCheck(self, GUNLEFT);
        right2 = chrGetEquippedWeaponPropWithCheck(self, GUNRIGHT);

        if ((left2 != NULL) && (right2 != NULL))
        {
            last_arg2 = (u32)randomGetNext() & (u32)1;

            if (((u32)randomGetNext() % 3U) == 0)
            {
                animation_pointer = (struct anim_group_info **)ptr_pistol_firing_animation_groups;
                sp.p[GUNLEFT] = last_arg2;
                sp.p[GUNRIGHT] = !last_arg2;
            }
            else
            {
                animation_pointer = (struct anim_group_info **)ptr_doubles_firing_animation_groups;
                sp.p[GUNLEFT] = 1;
                sp.p[GUNRIGHT] = 1;
            }
        }
        else
        {
            last_arg2 = right2 == 0;
            animation_pointer = (struct anim_group_info **)ptr_pistol_firing_animation_groups;
            sp.p[GUNLEFT] = last_arg2;
            sp.p[GUNRIGHT] = !last_arg2;
        }
    }
    else
    {
        if ((weaponIsOneHanded(left) != 0) || (weaponIsOneHanded(right) != 0))
        {
            last_arg2 = left != 0;
            animation_pointer = (struct anim_group_info **)ptr_pistol_firing_animation_groups;
            sp.p[GUNLEFT] = last_arg2;
            sp.p[GUNRIGHT] = !last_arg2;
        }
        else
        {
            last_arg2 = left != 0;
            animation_pointer = (struct anim_group_info **)ptr_rifle_firing_animation_groups;
            sp.p[GUNLEFT] = last_arg2;
            sp.p[GUNRIGHT] = !last_arg2;
        }
    }

    chrlvInitActAttack(self, animation_pointer, last_arg2, &sp, attack_type, arg2, 1);
}



/**
 * Address 0x7F0256F0.
 * PD: chrAttackKneel.
*/
void sub_GAME_7F0256F0(ChrRecord *self, s32 attack_type, s32 arg2)
{
    PropRecord *left;
    PropRecord *right;
    s32 last_arg2;
    struct anim_group_info **animation_pointer;
    point2d sp;
    PropRecord * left2;
    PropRecord * right2;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    sp = D_800309C0;

    if ((left != NULL) && (right != NULL))
    {
        left2 = chrGetEquippedWeaponPropWithCheck(self, GUNLEFT);
        right2 = chrGetEquippedWeaponPropWithCheck(self, GUNRIGHT);

        if ((left2 != NULL) && (right2 != NULL))
        {
            last_arg2 = (u32)randomGetNext() & (u32)1;

            if (((u32)randomGetNext() % 3U) == 0)
            {
                animation_pointer = (struct anim_group_info **)ptr_crouched_pistol_firing_animation_groups;
                sp.p[GUNLEFT] = last_arg2;
                sp.p[GUNRIGHT] = !last_arg2;
            }
            else
            {
                animation_pointer = (struct anim_group_info **)ptr_crouched_doubles_firing_animation_groups;
                sp.p[GUNLEFT] = 1;
                sp.p[GUNRIGHT] = 1;
            }
        }
        else
        {
            last_arg2 = right2 == 0;
            animation_pointer = (struct anim_group_info **)ptr_crouched_pistol_firing_animation_groups;
            sp.p[GUNLEFT] = last_arg2;
            sp.p[GUNRIGHT] = !last_arg2;
        }
    }
    else
    {
        if ((weaponIsOneHanded(left) != 0) || (weaponIsOneHanded(right) != 0))
        {
            last_arg2 = left != 0;
            animation_pointer = (struct anim_group_info **)ptr_crouched_pistol_firing_animation_groups;
            sp.p[GUNLEFT] = last_arg2;
            sp.p[GUNRIGHT] = !last_arg2;
        }
        else
        {
            last_arg2 = left != 0;
            animation_pointer = (struct anim_group_info **)ptr_crouched_rifle_firing_animation_groups;
            sp.p[GUNLEFT] = last_arg2;
            sp.p[GUNRIGHT] = !last_arg2;
        }
    }

    chrlvInitActAttack(self, animation_pointer, last_arg2, &sp, attack_type, arg2, 0);
}


/**
 * // run forward shooting
 * Address 0x7F02587C.
*/
void chrlvInitActAttackWalk(ChrRecord *chr, s32 arg1)
{
    struct weapon_firing_animation_table *panim_float; // 132
    s32 i; //
    ChrRecord *tmp_chr; //
    s32 sp78; // 120
    point2d sp70; // 112
    point2d sp68; // 104
    point2d sp60; // 96
    PropRecord *left;
    PropRecord *left2;
    PropRecord *right;
    PropRecord *right2;
    s32 phi_v1;
    u32 unused = 1;

    left = chrGetEquippedWeaponProp(chr, GUNLEFT);
    right = chrGetEquippedWeaponProp(chr, GUNRIGHT);

    sp70 = D_800309C8;
    sp68 = D_800309D0;
    sp60 = D_800309D8;

    if ((left != NULL) && (right != NULL))
    {
        left2 = chrGetEquippedWeaponPropWithCheck(chr, GUNLEFT);
        right2 = chrGetEquippedWeaponPropWithCheck(chr, GUNRIGHT);

        phi_v1 = 0U;

        if ((left2 != NULL) && (right2 != NULL))
        {
            sp78 = (u32)randomGetNext() & 1U;
            phi_v1 = (u32)randomGetNext() % 3U;
        }
        else
        {
            sp78 = right2 == 0;
        }

        if (phi_v1 == 0)
        {
            if (arg1 != 0)
            {
                panim_float = &D_80030660[3];
            }
            else
            {
                panim_float = &D_80030660[2];
            }

            if (sp78)
            {
                sp70.p[1] = 1;
            }
            else
            {
                // bug/mistake/typo.
                sp70.p[0] = sp70.p[0] = 1;
            }
        }
        else if (phi_v1 == 1)
        {
            if (arg1 != 0)
            {
                panim_float = &D_80030660[5];
            }
            else
            {
                panim_float = &D_80030660[4];
            }

            sp70.p[1] = 1;
            sp70.p[0] = 1;
        }
        else
        {
            if (arg1 != 0)
            {
                panim_float = &D_80030660[7];
            }
            else
            {
                panim_float = &D_80030660[6];
            }

            sp70.p[1] = 1;
            sp70.p[0] = 1;
        }
    }
    else if (weaponIsOneHanded(left) || weaponIsOneHanded(right))
    {
        sp78 = left != NULL;

        if (arg1)
        {
            panim_float = &D_80030660[3];
        }
        else
        {
            panim_float = &D_80030660[2];
        }

        if (sp78)
        {
            sp70.p[1] = 1;
        }
        else
        {
            sp70.p[0] = 1;
        }
    }
    else
    {
        sp78 = left != NULL;

        if (arg1)
        {
            panim_float = &D_80030660[1];
        }
        else
        {
            panim_float = &D_80030660[0];
        }

        if (sp78)
        {
            sp70.p[1] = 1;
        }
        else
        {
            sp70.p[0] = 1;
        }
    }

    for (i=0; i<2; i++)
    {
        if (sp70.p[i])
        {
            tmp_chr = chrGetEquippedWeaponProp(chr, i)->chr;

            if (bondwalkItemGetAutomaticFiringRate((s32) tmp_chr->act_attackwalk.attack_item) < 0)
            {
                sp68.p[i] = 1;
            }

            if ((tmp_chr->act_attackwalk.attack_item == ITEM_ROCKETLAUNCH) || (tmp_chr->act_attackwalk.attack_item == ITEM_GRENADELAUNCH))
            {
                sp60.p[i] = 1;
            }
        }
    }

    chr->actiontype = ACT_ATTACKWALK;
    chr->act_attackwalk.clock_timer30 = 0;
    #if defined(REFRESH_PAL)
    chr->act_attackwalk.clock_timer34 = ((u32) randomGetNext() % (u32) (s32) (333.333343506f * g_AiReactionSpeed)) + CHRLV_SEEN_RECENT_CHECK;
    #else
    chr->act_attackwalk.clock_timer34 = ((u32) randomGetNext() % (u32) (s32) (400.0f * g_AiReactionSpeed)) + CHRLV_SEEN_RECENT_CHECK;
    #endif
    chr->act_attackwalk.unk038 = 0;
    chr->act_attackwalk.animfloats = panim_float;
    chr->act_attackwalk.timer40 = 0;
    chr->act_attackwalk.unk044 = (u32)randomGetNext() & 1U;
    chr->act_attackwalk.unk48[1] = (s8) sp70.p[1];
    chr->act_attackwalk.unk48[0] = (s8) sp70.p[0];
    chr->act_attackwalk.unk4a[1] = (s8) sp68.p[1];
    chr->act_attackwalk.unk4a[0] = (s8) sp68.p[0];
    chr->act_attackwalk.unk4C[1] = (s8) sp60.p[1];
    chr->act_attackwalk.unk4C[0] = (s8) sp60.p[0];
    chr->sleep = 0;
    chr->act_attackwalk.speed = 0.0f;

    modelSetAnimation(chr->model, (struct ModelAnimation *) panim_float->anim.anim, sp78, panim_float->start_frame, 0.5f, 16.0f);
}


/**
 * chrRollToSide
 * Address 0x7F025C40.
*/
void chrlvInitActAttackRoll(ChrRecord *chr, GUNHAND side)
{
    Model *self_model; // 140
    struct weapon_firing_animation_table *panim_float; // 136
    PropRecord *left; // any
    PropRecord *left_2; // any
    s32 sp7C; // 124
    s32 sp78; // 120
    ChrRecord *sp70; // any
    ChrRecord *temp_v1_2; // 112
    PropRecord *right; // any
    point2d sp64; // 100
    PropRecord *right_2; // any
    s32 sp5C; // 92
    point2d sp54; // 84
    point2d sp4C; // 76
    s8 phi_s3; // 72
    s32 i; // 68

    self_model = chr->model;
    left = chrGetEquippedWeaponProp(chr, GUNLEFT);
    right = chrGetEquippedWeaponProp(chr, GUNRIGHT);
    sp78 = 0;
    sp64 = D_800309E0;
    sp5C = 0;
    sp54 = D_800309E8;
    sp4C = D_800309F0;
    phi_s3 = 1;

    if ((left != NULL) && (right != NULL))
    {
        left_2 = chrGetEquippedWeaponPropWithCheck(chr, GUNLEFT);
        right_2 = chrGetEquippedWeaponPropWithCheck(chr, GUNRIGHT);

        if ((left_2 != NULL) && (right_2 != NULL))
        {
            sp7C = (u32)randomGetNext() & 1U;
            sp78 = 1;

            if (((u32)randomGetNext() % 3U) == 0)
            {
                sp64.p[1] = sp7C;
                sp64.p[0] = sp7C == 0;
            }
            else
            {
                sp64.p[1] = 1;
                sp64.p[0] = 1;
            }
        }
        else
        {
            sp7C = right_2 == NULL;
            sp78 = 1;
            sp64.p[1] = sp7C;
            sp64.p[0] = sp7C == 0;
        }
    }
    else if (weaponIsOneHanded(left) || weaponIsOneHanded(right))
    {
        sp7C = left != NULL;
        sp78 = 1;
        sp64.p[1] = sp7C;
        sp64.p[0] = sp7C == 0;
    }
    else
    {
        sp7C = left != NULL;
        sp64.p[1] = sp7C;
        sp64.p[0] = sp7C == 0;
    }

    if (((side != GUNRIGHT) && (sp7C == 0)) ||
        ((side == GUNRIGHT) && (sp7C != 0)))
    {
        if ((u32)randomGetNext() & 1U)
        {
            panim_float = &D_80030078[0];
        }
        else
        {
            panim_float = &D_80030078[2];
        }
    }
    else if ((u32)randomGetNext() & 1U)
    {
        panim_float = &D_80030078[1];
    }
    else
    {
        panim_float = &D_80030078[3];
    }

    if (sp78 != 0)
    {
        panim_float += 4;
    }

    for (i=0; i<2; i++)
    {
        if (sp64.p[i] != 0)
        {
            temp_v1_2 = chrGetEquippedWeaponProp(chr, i)->chr;

            if (bondwalkItemGetAutomaticFiringRate((s32) temp_v1_2->act_attackroll.attack_item) < 0)
            {
                sp54.p[i] = 1;
                if (temp_v1_2->act_attackroll.attack_item == ITEM_LASER)
                {
                    phi_s3 = 0;
                }
            }
            else
            {
                phi_s3 = 0;
                sp5C = 1;
            }

            if ((temp_v1_2->act_attackroll.attack_item == ITEM_ROCKETLAUNCH) || (temp_v1_2->act_attackroll.attack_item == ITEM_GRENADELAUNCH))
            {
                sp4C.p[i] = 1;
            }
        }
    }

    chr->actiontype = ACT_ATTACKROLL;
    chr->act_attackroll.animfloats = panim_float;
    chr->act_attackroll.unk31 = 0;
    chr->act_attackroll.unk32 = (u32)randomGetNext() & (u32)1;
    chr->act_attackroll.unk38[1] = sp64.p[1];
    chr->act_attackroll.unk38[0] = sp64.p[0];
    chr->act_attackroll.unk3a[1] = sp54.p[1];
    chr->act_attackroll.unk3a[0] = sp54.p[0];
    chr->act_attackroll.unk3c[1] = sp4C.p[1];
    chr->act_attackroll.unk3c[0] = sp4C.p[0];
    chr->act_attackroll.unk36 = phi_s3;
    chr->act_attackroll.unk37 = sp5C;
    chr->act_attackroll.unk35 = sp78;
    chr->act_attackroll.unk40 = 0;
    chr->act_attackroll.unk33 = 0;
    chr->act_attackroll.unk30 = 1;

    if ((sp4C.p[1] != 0) || (sp4C.p[0] != 0))
    {
        if ((sp4C.p[1] != 0) && (sp4C.p[0] != 0))
        {
            chr->act_attackroll.unk34 = 2;
        }
        else
        {
            chr->act_attackroll.unk34 = (s8) 1U;
        }
    }
    else
    {
        chr->act_attackroll.unk34 = (s32)((u32)randomGetNext() & 3U) + 2;

        if ((sp64.p[0] != 0) && (sp64.p[1] != 0))
        {
            chr->act_attackroll.unk34 += (s32)((u32)randomGetNext() & 3U) + 2;
        }
    }

    chr->act_attackroll.unk4c[0] = 1;
    chr->act_attackroll.unk4c[1] = 0;
    chr->act_attackroll.unk54[0] = 1;
    chr->act_attackroll.unk54[1] = 0;
    chr->act_attackroll.unk44 = 0;
    chr->act_attackroll.attack_time = 0;
    chr->sleep = 0;

    modelSetAnimation(
        self_model,
        (struct ModelAnimation *) panim_float->anim.anim,
        sp7C,
        panim_float->start_frame,
        chrlvGetGuard007SpeedRating(chr, 0.5f, 0.8f),
        16.0f);

    if (sp78 == 0)
    {
        if (phi_s3 != 0)
        {
            if (panim_float->recoil_end_frame >= 0.0f)
            {
                modelSetAnimEndFrame(self_model, panim_float->recoil_end_frame);
            }
            else
            {
                modelSetAnimEndFrame(self_model, panim_float->shoot_end_frame);
            }
        }
        else if (panim_float->recoil_start_frame >= 0.0f)
        {
            modelSetAnimEndFrame(self_model, panim_float->recoil_start_frame);
        }
        else if (panim_float->end_frame >= 0.0f)
        {
            modelSetAnimEndFrame(self_model, panim_float->end_frame);
        }
    }
}



/**
 * Line-line intersection, where arg0 and arg1 are two points on line1, and arg2 and arg3 are a point and a direction of line2.
 * 3d coord/vector are passed as arguments, but only the 2d (x,z) values are used to find the intersection.
 *
 * @param line1_p1: first point to describe line1
 * @param line1_p2: second point to describe line1
 * @param line2_p3: first point to describe line2
 * @param dir: vector giving direction of line2
 * @param result: contains result
 *
 * Address 0x7F026130.
 */
void chrlvLineLineIntersection(coord3d *line1_p1, coord3d *line1_p2, coord3d *line2_p3, coord3d *dir, coord3d *result)
{
    /*
     * Line1 = P1 + u * (P2 - P1)
     * Line2 = P3 + v * D
     *
     * Intersection is where Line1==Line2, or:
     *     P1 + u * (P2 - P1) = P3 + v * D
     *
     * u and v are unknown.
     *
     * Isolate u:
     *
     * u = (P3 + v*D - P1) / (P2 - P1)
     */
    f32 denom;

    // solve for v. (much algebra follows, not shown)

    denom = (dir->f[2] * (line1_p2->f[0] - line1_p1->f[0])) - (dir->f[0] * (line1_p2->f[2] - line1_p1->f[2]));

    if (denom != 0.0f)
    {
        f32 v = (
            ((line1_p1->f[2] - line2_p3->f[2]) * (line1_p2->f[0] - line1_p1->f[0]))
            + ((line2_p3->f[0] - line1_p1->f[0]) * (line1_p2->f[2] - line1_p1->f[2]))
        ) / denom;

        // v is known, denom is non-zero, plug back into equation for Line2 = P3 + v * D

        result->f[0] = line2_p3->f[0] + (dir->f[0] * v);
        result->f[1] = line2_p3->f[1] + (dir->f[1] * v);
        result->f[2] = line2_p3->f[2] + (dir->f[2] * v);
    }
    else if ((dir->f[0] == 0.0f) && (dir->f[2] == 0.0f))
    {
        // else, denominator is zero, but direction is also zero, so assume Line 2 point as result
        result->f[0] = line2_p3->f[0];
        result->f[1] = line2_p3->f[1];
        result->f[2] = line2_p3->f[2];
    }
    else
    {
        // all other cases, fallback to Line 1 first point
        result->f[0] = line1_p1->f[0];
        result->f[1] = line1_p1->f[1];
        result->f[2] = line1_p1->f[2];
    }
}



/**
 * Line-line intersection.
 * The first two points are retrieved from getCollisionEdge_maybe.
 * The arguments to the method supply the other line, described by a point and direction.
 *
 * 3d coord/vector are passed as arguments, but only the 2d (x,z) values are used to find the intersection.
 *
 * @param line2_p3: first point to describe line2
 * @param dir: vector giving direction of line2
 * @param result: out parameter, contains result.
 *
 * Address 0x7F02624C.
 */
void chrlvStanLineDirIntersection(coord3d *line2_p3, coord3d *dir, coord3d *result)
{
    coord3d sp2C;
    coord3d sp20;

    getCollisionEdge_maybe(&sp2C, &sp20);
    chrlvLineLineIntersection(&sp2C, &sp20, line2_p3, dir, result);
}


/**
 * @param arg0:
 * @param arg1:
 * @param result: out parameter, contains result.
 *
 * Address 0x7F026298.
 */
void chrlvStanPointPointIntersection(coord3d *arg0, coord3d *arg1, coord3d *result)
{
    coord3d sp2C;
    coord3d sp20;
    f32 v;

    getCollisionEdge_maybe(&sp2C, &sp20);

    // see comments in chrlvLineLineIntersection

    v = ((arg1->f[0] * (sp2C.f[2] - arg0->f[2])) - (arg1->f[2] * (sp2C.f[0] - arg0->f[0])))
        / ((arg1->f[2] * (sp20.f[0] - sp2C.f[0])) - (arg1->f[0] * (sp20.f[2] - sp2C.f[2])));

    result->f[0] = sp2C.f[0] + ((sp20.f[0] - sp2C.f[0]) * v);
    result->f[1] = sp2C.f[1] + ((sp20.f[1] - sp2C.f[1]) * v);
    result->f[2] = sp2C.f[2] + ((sp20.f[2] - sp2C.f[2]) * v);
}


/**
 * Address 0x7F026364.
 */
f32 chrlvPathingCollisionRelated(PropRecord *arg0, f32 arg1, f32 arg2, s32 cdtypes, f32 unkHeight, f32 unkA)
{
    coord3d sp5C; // sp92
    f32 dest_x; // sp88
    f32 dest_z; // sp84
    StandTile *stan; // sp80
    ChrRecord *chr; // sp76
    f32 ret;
    coord3d sp3C;

    stan = arg0->stan;
    chr = arg0->chr;

    sp5C.f[0] = sinf(arg1);
    sp5C.f[1] = 0.0f;
    sp5C.f[2] = cosf(arg1);
    dest_x = arg0->pos.f[0] + (sp5C.f[0] * arg2);
    dest_z = arg0->pos.f[2] + (sp5C.f[2] * arg2);

    chrSetMoving(chr, 0);
    stanResetHits();

    if (stanTestLineUnobstructed(&stan, arg0->pos.f[0], arg0->pos.f[2], dest_x, dest_z, cdtypes, unkHeight, unkA, 0.0f, 1.0f) != 0)
    {
        ret = arg2;
    }
    else
    {
        chrlvStanLineDirIntersection(&arg0->pos, &sp5C, &sp3C);
        dest_x = sp3C.f[0] - arg0->pos.f[0];
        dest_z = sp3C.f[2] - arg0->pos.f[2];
        ret = sqrtf((dest_x * dest_x) + (dest_z * dest_z));
    }

    chrSetMoving(chr, 1);

    return ret;
}


/**
 * Address 0x7F0264B0.
*/
f32 chrlvPathingCollisionRelated7F0264B0(PropRecord *arg0, f32 arg1, f32 arg2)
{
    f32 sp2C;
    f32 sp28;
    f32 sp24;

    chrGetChrWidthHeight(arg0, &sp24, &sp2C, &sp28);
    return chrlvPathingCollisionRelated(arg0, arg1, arg2, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, sp2C, sp28);
}


/**
 * @param arg0:
 * @param arg1:
 * @param arg2:
 * @param req_animation_id: Lookup by id property in g_HitReactionTable
 * @param item: argument to bondwalkItemGetForceOfImpact
 *
 * Address 0x7F026508.
 */
void triggered_on_shot_hit(ChrRecord *self, coord3d *arg1, f32 arg2, s32 req_animation_id, ITEM_IDS item)
{
    // stack offset in decimal

    s32 flag9c; // 156(sp)
    PropRecord *prop; // 152
    struct Model *model; // 148
    s32 another_flag; // 144
    f32 impact_force; // ?
    s32 animation_something_index; // 136
    s32 flag1; // 132
    ModelAnimation *sp80 = NULL; // ?
    struct ChrHitReaction *something_ani = NULL; // ?
    f32 fa;
    f32 fb;
    f32 f_under; // 112(sp)
    f32 f_over; // 108(sp)
    f32 ft;
    struct StruckAnim *struck_ani; // 100
    s32 i;

    flag9c = 1;
    prop = self->prop;
    model = self->model;
    another_flag = 0;
    animation_something_index = 0;

    if ((self->prop->type != PROP_TYPE_VIEWER) || (getPlayerCount() < 2))
    {
        flag1 = (self->actiontype == ACT_ARGH) && (g_GlobalTimer == self->act_argh.unk30);

        for (i=0; g_HitReactionTable[i].hitpart != -1; i++)
        {
            if (req_animation_id == g_HitReactionTable[i].hitpart)
            {
                animation_something_index = i;

                break;
            }
        }

        if (self->damage >= self->maxdamage)
        {
            if (((arg2 < 1.5707964f) || (arg2 > 4.712389f)) && ((randomGetNext() % (u32)0x14) == 0))
            {
                ft = getsubroty(model) + M_PI_F;

                fa = ft + 0.17453294f;
                f_under = ft - 0.17453294f;

                if (fa >= M_TAU_F)
                {
                    fa -= M_TAU_F;
                }

                if (f_under >= M_TAU_F)
                {
                    f_under -= M_TAU_F;
                }

                f_over = chrlvPathingCollisionRelated7F0264B0(prop, fa, 150.0f);
                f_under = chrlvPathingCollisionRelated7F0264B0(prop, f_under, 150.0f);

                if ((f_over < 150.0f) && (f_under < 150.0f))
                {
                    ft = f_over - f_under;
                    if ((ft < 10.0f) && (ft > -10.0f))
                    {
                        struck_ani = &death_stagger[randomGetNext() & 1];

                        chrStopFiring(self);
                        self->actiontype = ACT_DIE;
                        self->act_die.notifychrindex = 0;
                        self->act_die.thudframe1 = struck_ani->thudframe1;
                        self->act_die.thudframe2 = struck_ani->thudframe2;
                        self->sleep = 0;
                        self->act_die.timeextra = 0.0f;

                        modelSetAnimationWithMerge(model, struck_ani->struck_anim, struck_ani->flip, 0.0f, struck_ani->speed, 16.0f, flag1 == 0);

                        if (struck_ani->endframe >= 0.0f)
                        {
                            modelSetAnimEndFrame(model, struck_ani->endframe);
                        }

                        // Note: PD sets the chrwidth to 10 when a guard dies slumped against an object or wall
                        self->chrwidth = 10.0f;

                        another_flag = 1;
                    }
                }
            }

            if (another_flag == 0)
            {
                if ((g_HitReactionTable[animation_something_index].deathAnims != NULL) && (g_HitReactionTable[animation_something_index].deathAnimCount > 0))
                {
                    struct StruckAnim *struck_anib; // sp(92)
                    s32 tr;

                    if (0)
                    {
                        // removed
                    }

                    another_flag = 1;

                    tr = (randomGetNext() % (u32)g_HitReactionTable[animation_something_index].deathAnimCount);
                    struck_anib = &g_HitReactionTable[animation_something_index].deathAnims[tr];
                    chrStopFiring(self);

                    self->actiontype = ACT_DIE;
                    self->act_die.notifychrindex = 0;
                    self->act_die.thudframe1 = struck_anib->thudframe1;
                    self->act_die.thudframe2 = struck_anib->thudframe2;
                    self->sleep = 0;
                    self->act_die.timeextra = 0.0f;

                    modelSetAnimationWithMerge(model, struck_anib->struck_anim, struck_anib->flip, 0.0f, struck_anib->speed, 16.0f, flag1 == 0);

                    if (struck_anib->struck_anim == (portAnimationFromOffset(PTR_ANIM_death_neck)) && ((randomGetNext() % (u32)0x64) != 0))
                    {
                        modelSetAnimEndFrame(model, 241.0f);
                    }
                    else if (struck_anib->endframe >= 0.0f)
                    {
                        modelSetAnimEndFrame(model, struck_anib->endframe);
                    }

                    impact_force = bondwalkItemGetForceOfImpact(item);

                    if ((impact_force <= 0.0f) && ((self->chrflags & CHRFLAG_IMPACT_ALWAYS) != 0))
                    {
                        impact_force = 6.0f;
                    }

                    if ((struck_anib->knockback != 0) && (impact_force > 0.0f))
                    {
                        self->act_die.elapseextra = 0.0f;
                        self->act_die.timeextra = ((impact_force * 90.0f) / 6.0f);
                        self->act_die.extraspeed.f[0] = (arg1->f[0] * impact_force);
                        self->act_die.extraspeed.f[1] = (arg1->f[1] * impact_force);
                        self->act_die.extraspeed.f[2] = (arg1->f[2] * impact_force);
                    }
                }
            }

            chrDropItems(self);
            increment_num_kills_display_text_in_MP();

            if (self->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN)
            {
                inc_cur_civilian_casualties();
            }
        }
        else
        {
            if ((req_animation_id == 7) && (arg2 > 2.3561945f) && (arg2 < 3.926991f) && ((u32) (randomGetNext() % (u32)5) < 2U))
            {
                u32 sp54 = randomGetNext() % (u32)5;
                chrStopFiring(self);
                self->actiontype = ACT_ARGH;
                self->act_argh.notifychrindex = 0;
                self->act_argh.unk30 = g_GlobalTimer;
                self->sleep = 0;

                if ((randomGetNext() & 1) != 0)
                {
                    sp80 = portAnimationFromOffset(PTR_ANIM_hit_butt_long);
                    modelSetAnimationWithMerge(model, sp80, randomGetNext() & 1, 10.f, 0.5f, 16.0f, flag1 == 0);

                    if (sp54 < 2U)
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, 34.0f, 8.0f));
                    }
                    else if (sp54 < 4U)
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, 71.0f, 8.0f));
                    }
                    else
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, (f32) (sp80->unk04 - 1), 8.0f));
                    }
                }
                else
                {
                    sp80 = portAnimationFromOffset(PTR_ANIM_hit_butt_short);
                    modelSetAnimationWithMerge(model, sp80, randomGetNext() & 1, 0.0f, 0.5f, 16.0f, flag1 == 0);

                    if (sp54 < 2U)
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, 37.0f, 8.0f));
                    }
                    else if (sp54 < 4U)
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, 70.0f, 8.0f));
                    }
                    else
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, (f32) (sp80->unk04 - 1), 8.0f));
                    }
                }

                another_flag = 1;
            }

            if (another_flag == 0)
            {
                if ((g_HitReactionTable[animation_something_index].flinchAnims != NULL) && (g_HitReactionTable[animation_something_index].flinchAnimCount > 0))
                {
                    PropRecord *temp_left = chrGetEquippedWeaponProp(self, GUNLEFT);
                    PropRecord *temp_right = chrGetEquippedWeaponProp(self, GUNRIGHT);
                    s32 tr;
                    struct StruckAnim *struck_ani;
                    s32 ff = flag1 == 0;

                    another_flag = 1;
                    something_ani = &g_HitReactionTable[animation_something_index];

                    if ((&g_HitReactionTable[9] == something_ani) && (temp_left != NULL))
                    {
                        animation_something_index = 10;
                    }
                    else if ((&g_HitReactionTable[12] == something_ani) && (temp_right != NULL))
                    {
                        animation_something_index = 13;
                    }

                    something_ani = &g_HitReactionTable[animation_something_index];
                    tr = (randomGetNext() % (u32) something_ani->flinchAnimCount);
                    struck_ani = &something_ani->flinchAnims[tr];

                    chrStopFiring(self);

                    self->actiontype = ACT_ARGH;
                    self->act_argh.notifychrindex = 0;
                    self->act_argh.unk30 = g_GlobalTimer;
                    self->sleep = 0;

                    modelSetAnimationWithMerge(model, struck_ani->struck_anim, struck_ani->flip, 0.0f, struck_ani->speed, 16.0f, ff);

                    if (struck_ani->endframe >= 0.0f)
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, struck_ani->endframe, 8.0f));
                    }
                    else
                    {
                        modelSetAnimEndFrame(model, chrlvGetGuard007ArghRating(self, (f32) (struck_ani->struck_anim->unk04 - 1), 8.0f));
                    }
                }
            }

            flag9c = 0;
        }

        if (flag9c && another_flag)
        {
            if ((self->weapons_held[GUNRIGHT] != NULL) && ((self->weapons_held[GUNRIGHT]->obj->flags & PROPFLAG_AIUNDROPPABLE) == FALSE))
            {
                propobjSetDropped(self->weapons_held[GUNRIGHT], 1);
                self->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
            }

            if ((self->weapons_held[GUNLEFT] != NULL) && ((self->weapons_held[GUNLEFT]->obj->flags & PROPFLAG_AIUNDROPPABLE) == FALSE))
            {
                propobjSetDropped(self->weapons_held[GUNLEFT], 1);
                self->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
            }
        }
    }
}


/**
 * @param self:
 * @param result: out parameter, will contain result
 * @returns status indicating if result is set
 *
 * Address 0x7F026F30.
*/
s32 chrlvAttackAnimationRelated7F026F30(ChrRecord *self, f32 *result)
{
    s32 flag;
    f32 out_val;

    flag = 0;

    if (self->actiontype == ACT_ATTACKROLL)
    {
        if (self->act_attackroll.unk35 != 0)
        {
            if (
                (self->act_attackroll.animfloats == &D_80030078[4])
                || (self->act_attackroll.animfloats == &D_80030078[5])
                || (self->act_attackroll.animfloats == &D_80030078[6])
                || (self->act_attackroll.animfloats == &D_80030078[7]))
            {
                out_val = self->act_attackroll.animfloats->unk04 - 8.0f;

                if (self->act_attackroll.animfloats->end_frame < self->act_attackroll.animfloats->unk04)
                {
                    out_val = self->act_attackroll.animfloats->end_frame;
                }

                if (modelGetAnimFrame(self->model) < out_val)
                {
                    *result = out_val;
                    flag = 1;
                }
            }
        }
        else
        {
            out_val = self->act_attackroll.animfloats->unk04 - 8.0f;
            if (modelGetAnimFrame(self->model) < out_val)
            {
                *result = out_val;
                flag = 1;
            }
        }
    }
    else if (self->actiontype == ACT_PREARGH)
    {
        // typo/mistake, return without setting *result
        flag = 1;
    }

    return flag;
}


/**
 * Address 0x7F027060.
 */
void play_sound_for_shot_actor(ChrRecord *self)
{
    PropRecord *prop;
    bool male;
    ALSoundState *sndstate;
 
    static s32 male_guard_yelp_counter = 0;
    static s32 female_guard_yelp_counter = 0;
 
    prop = self->prop;
    male = 0;

    if (prop->type == PROP_TYPE_VIEWER)
    {
        if (g_playerPointers[getPlayerPointerIndex(prop)]->bonddead != FALSE)
        {
            return;
        }
    }

    if (self->prop->type == PROP_TYPE_VIEWER)
    {
        if (getPlayerCount() == 1)
        {
            if (c_item_entries[self->bodynum].isMale != FALSE)
            {
                male = TRUE;
            }
        }
        else
        {
            if (get_player_mp_char_gender(getPlayerPointerIndex(self->prop)) != FEMALE)
            {
                male = TRUE;
            }
        }
    }
    else
    {
        if (c_item_entries[self->bodynum].isMale != FALSE)
        {
            male = TRUE;
        }
    }

    if (male)
    {
        s16 male_yelps[] = {
            GET_HIT_MALE0_SFX,  GET_HIT_MALE1_SFX,  GET_HIT_MALE2_SFX,  GET_HIT_MALE3_SFX,  GET_HIT_MALE4_SFX,
            GET_HIT_MALE5_SFX,  GET_HIT_MALE6_SFX,  GET_HIT_MALE7_SFX,  GET_HIT_MALE8_SFX,  GET_HIT_MALE9_SFX,
            GET_HIT_MALE10_SFX, GET_HIT_MALE11_SFX, GET_HIT_MALE12_SFX, GET_HIT_MALE13_SFX, GET_HIT_MALE14_SFX,
            GET_HIT_MALE15_SFX, GET_HIT_MALE16_SFX, GET_HIT_MALE17_SFX, GET_HIT_MALE18_SFX, GET_HIT_MALE19_SFX,
            GET_HIT_MALE20_SFX, GET_HIT_MALE21_SFX, GET_HIT_MALE22_SFX, GET_HIT_MALE23_SFX, GET_HIT_MALE24_SFX
        };
        
        sndstate = sndPlaySfx(g_musicSfxBufferPtr, male_yelps[male_guard_yelp_counter], NULL);
        male_guard_yelp_counter++;

        if (male_guard_yelp_counter >= 25)
        {
            male_guard_yelp_counter = 0;
        }
    }
    else
    {
        s16 female_yelps[] = {
            GET_HIT_GIRL1_SFX,
            GET_HIT_GIRL2_SFX,
            GET_HIT_GIRL3_SFX
        };

        sndstate = sndPlaySfx(g_musicSfxBufferPtr, female_yelps[female_guard_yelp_counter], NULL);
        female_guard_yelp_counter++;

        if (female_guard_yelp_counter >= 3)
        {
            female_guard_yelp_counter = 0;
        }
    }

    chrobjSndCreatePostEventDefault(sndstate, &self->prop->pos);
}


//metal_ricochet_SFX and D_80030A44 must be placed here for matching.
s16 metal_ricochet_SFX[3] = {HIT_BULLET_METAL_A3_SFX, HIT_BULLET_METAL_A_SFX, HIT_BULLET_METAL_B_SFX};
coord3d D_80030A44 = {0, 0, 0};

/**
 * Address 0x7F02727C.
*/
bool handles_shot_actors(ChrRecord *self, s32 hitpart, coord3d *vector, s32 weaponid, bool isPlayer)
{
    s32 hattype;                     //sp78
    PropRecord *myprop = self->prop; //sp60
    s32 padd;

    // Handle hat shots.
    if (hitpart == HIT_HAT && self->handle_positiondata_hat)
    {
        hattype = get_hat_model(self->handle_positiondata_hat);

        if (hattype == HATTYPE_MOON) //moon - count as head hit
        {
            hitpart = HIT_HEAD;
        }
        else if (hattype != HATTYPE_HELMATE) //normal hats - knock off
        {
            propobjSetDropped(self->handle_positiondata_hat, 4); //propobjSetDropped
            self->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;           //drop hat
        }
        else //steel helmate - ricochet
        {
#ifdef TARGET_N64
            s16 mrs[3] = metal_ricochet_SFX;
#else
            /* array-from-array copy-init is an IDO extension */
            s16 mrs[3];
            mrs[0] = metal_ricochet_SFX[0];
            mrs[1] = metal_ricochet_SFX[1];
            mrs[2] = metal_ricochet_SFX[2];
#endif
            ALSoundState * p = sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, mrs[randomGetNext() % 3U], NULL);
            chrobjSndCreatePostEventDefault(p, &self->prop->pos);
        }
    }

    // Handle incrementing player shot count
    if (isPlayer && hitpart)
    {
        switch (hitpart)
        {
            case HIT_HEAD:
            {
                inc_curplayer_hitcount_with_weapon(weaponid, SHOT_REGISTER_HEAD);
                break;
            }
            case HIT_GUN:
            {
                inc_curplayer_hitcount_with_weapon(weaponid, SHOT_REGISTER_GUN);
                break;
            }
            case HIT_HAT:
            {
                inc_curplayer_hitcount_with_weapon(weaponid, SHOT_REGISTER_HAT);
                break;
            }
            case HIT_CHEST:
            case HIT_PELVIS:
            {
                inc_curplayer_hitcount_with_weapon(weaponid, SHOT_REGISTER_BODY);
                break;
            }
            default:
            {
                inc_curplayer_hitcount_with_weapon(weaponid, SHOT_REGISTER_LIMB);
                break;
            }
        }
    }

    self->numarghs++;
    self->chrflags |= CHRFLAG_WAS_HIT;

    // If the chr is invincible, make them flinch then we're done
    if (self->chrflags & CHRFLAG_INVINCIBLE)
    {
        chrSetHiddenToRandom(self); //chrFlinchBody
        return FALSE;
    }

    // If chr is dying or already dead then we're done
    if ((self->actiontype != ACT_DIE) && (self->actiontype != ACT_DEAD))
    {
        vec3d vec;
        f32   angle;
        f32   damageToCause;
        s32   playerNum;

        damageToCause = gunItemGetDestructionAmount(weaponid);

        if (isPlayer && (getPlayerCount() == 1))
        {
            damageToCause *= g_AiHealthModifier;
        }

        vec.x = myprop->pos.x - vector->x;
        vec.y = myprop->pos.y - vector->y;
        vec.z = myprop->pos.z - vector->z;
        angle = get_distance_actor_to_position(self, &vec); //chrGetAngleToPos

        if (hitpart == HIT_GENERAL)
        {
            // Halve the damage because it's doubled for torso below
            hitpart = HIT_CHEST;
            damageToCause *= 0.5f;
        }
        else if (hitpart == HIT_GENERALHALF)
        {
            // Likewise, quarter it here so it becomes half below
            hitpart = HIT_CHEST;
            damageToCause *= 0.25f;
        }

        if (weaponid == ITEM_FIST)
        {
            if ((self->actiontype != ACT_STAND) &&
                (self->actiontype != ACT_PATROL) &&
                (self->actiontype != ACT_SURRENDER) &&
                (self->actiontype != ACT_ANIM) &&
                ((self->actiontype != ACT_GOPOS) || self->act_gopos.unk59))
            {
                // Punching and pistol whipping is less effective from the front
                if ((angle < DegToRad(60.0)) || (angle > DegToRad(300.0)))
                {
                    damageToCause *= 0.125f;
                }
                else if ((angle < DegToRad(180.0 - 60.0)) || (angle > DegToRad(180.0 + 60.0)))
                {
                    damageToCause *= 0.25f;
                }
                else
                {
                    damageToCause *= 0.5f;
                }
            }
        }

        // Apply damage multipliers based on which body parts were hit,
        // and flinch head if shot in the head - PD Only
        if (hitpart == HIT_HEAD)
        {
            damageToCause *= 4.0f;
        }
        else if (hitpart == HIT_CHEST)
        {
            damageToCause *= 2.0f;
        }
        else if (hitpart == HIT_GUN)
        {
            damageToCause = 0.0f;
        }
        else if (hitpart == HIT_HAT)
        {
            damageToCause = 0.0f;
        }

        if (self->prop->type == PROP_TYPE_VIEWER)
        {
            playerNum = get_cur_playernum();
            set_cur_player(getPlayerPointerIndex(self->prop));
            record_damage_kills(damageToCause * 0.125f, vector->x, vector->z, playerNum, 1);
            set_cur_player(playerNum);
        }
        else
        {
            self->chrflags |= CHRFLAG_WAS_DAMAGED;
#    ifdef XBLA
            if (!cheatIsActive(76))
#    endif
                self->damage += damageToCause;

            if (self->damage < 0.0f)
            {
                f32 endframe = -1.0f; //sp34
                if (!chrlvAttackAnimationRelated7F026F30(self, &endframe))
                {
                    chrSetHiddenToRandom(self);
                    return FALSE;
                }
            }
        }

        if (hitpart != HIT_HAT)
        {
            // Cancel current animation and prepare for argh
            f32 endframe2 = -1.0f; //sp30
            play_sound_for_shot_actor(self);

            if (chrlvAttackAnimationRelated7F026F30(self, &endframe2)) //chrIsAnimPreventingArgh
            {
                if (endframe2 >= 0.0f)
                {
                    modelSetAnimEndFrame(self->model, endframe2); //modelSetAnimEndFrame
                }

                self->actiontype         = ACT_PREARGH;
                self->act_preargh.pos.x  = vector->x;
                self->act_preargh.pos.y  = vector->y;
                self->act_preargh.pos.z  = vector->z;
                self->act_preargh.unk038 = angle;
                self->act_preargh.unk03c = hitpart;
                self->act_preargh.unk040 = weaponid;
                self->sleep              = 0;
            }
            else
            {
                triggered_on_shot_hit(self, vector, angle, hitpart, weaponid);
            }
        }
    }

    return TRUE;
}




/**
 * Address 0x7F027804.
*/
s32 chrlvExplosionDamage(ChrRecord *self, coord3d *arg1, f32 damage, s32 arg3)
{
    Model *self_model; // 84
    PropRecord *self_prop; // 80
    f32 subroty; // 76
    f32 atan; // 72
    f32 norm; // any
    s32 sp40; // 64
    f32 phi_f12; // any
    struct explosion_death_animation *sp38; // 56
    coord3d sp2C; // 44
    s32 t;

    self_model = self->model;
    self_prop = self->prop;

    if ((self->actiontype == ACT_DEAD) || (self->actiontype == ACT_DIE))
    {
        return 0;
    }

    self->chrflags |= CHRFLAG_WAS_HIT;
    if (self->chrflags & CHRFLAG_INVINCIBLE)
    {
        return 0;
    }

    self->numarghs += 1;
    self->damage += damage;
    self->chrflags |= CHRFLAG_WAS_DAMAGED;

    if (self->damage > 0.0f)
    {
        self->damage = self->maxdamage;

        subroty = getsubroty(self_model);

        atan = atan2f(self_prop->pos.f[0] - arg1->f[0], self_prop->pos.f[2] - arg1->f[2]);

        sp2C.f[0] = self_prop->pos.f[0] - arg1->f[0];
        sp2C.f[1] = self_prop->pos.f[1] - arg1->f[1];
        sp2C.f[2] = self_prop->pos.f[2] - arg1->f[2];

        // avoid divide by zero
        if ((sp2C.f[0] == 0.0f) && (sp2C.f[1] == 0.0f) && (sp2C.f[2] == 0.0f))
        {
            sp2C.f[2] = 1.0f;
        }

        norm = (5.0f * damage) / sqrtf(((sp2C.f[0] * sp2C.f[0]) + (sp2C.f[1] * sp2C.f[1])) + (sp2C.f[2] * sp2C.f[2]));
        phi_f12 = atan - subroty;

        sp2C.f[0] *= norm;
        sp2C.f[1] *= norm;
        sp2C.f[2] *= norm;

        self->fallspeed.f[0] = sp2C.f[0];
        self->fallspeed.f[1] = sp2C.f[1];
        self->fallspeed.f[2] = sp2C.f[2];

        if (atan < subroty)
        {
            phi_f12 += M_TAU_F;
        }

        sp40 = (s32) (((phi_f12 * 8.0f) / M_TAU_F) + 0.5f);

        if (sp40 >= EXPLOSION_ANIMATION_TABLE_LEN)
        {
            sp40 = 0;
        }

        t = (u32)randomGetNext() % (u32) explosion_animation_table[sp40].count;
        sp38 = &D_8002E648[
            explosion_animation_table[sp40].table[t]
            ];

        chrStopFiring(self);

        self->actiontype = ACT_DIE;
        self->act_die.notifychrindex = 0;
        self->act_die.thudframe1 = sp38->anonymous_5;
        self->sleep = 0;
        self->act_die.thudframe2 = -1.0f;
        self->act_die.timeextra = 0.0f;

        modelSetAnimation(
            self_model,
            (struct ModelAnimation *) ((s32)sp38->anonymous_0 + (intptr_t)&ptr_animation_table->data),
            sp38->anonymous_1,
            sp38->anonymous_3,
            sp38->anonymous_2,
            8.0f);

        if (sp38->anonymous_6 >= 0.0f)
        {
            modelSetAnimEndFrame(self_model, sp38->anonymous_6);
        }

        if (arg3 != 0)
        {
            play_sound_for_shot_actor(self);
        }

        chrDropItems(self);
        increment_num_kills_display_text_in_MP();

        if (self->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN)
        {
            inc_cur_civilian_casualties();
        }

        if ((self->weapons_held[GUNRIGHT] != NULL) && ((self->weapons_held[GUNRIGHT]->obj->flags & 0x2000) == 0))
        {
            propobjSetDropped(self->weapons_held[GUNRIGHT], 1);
            self->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
        }

        if ((self->weapons_held[GUNLEFT] != NULL) && ((self->weapons_held[GUNLEFT]->obj->flags & 0x2000) == 0))
        {
            propobjSetDropped(self->weapons_held[GUNLEFT], 1);
            self->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
        }

        return 1;
    }

    return 0;
}


/**
 * Address 0x7F027BF4.
 * 
 * Given a stan tile, find the first waypoint on that tile.
 */
waypoint *get_ptrpreset_in_table_matching_tile(StandTile *tile)
{
    waypoint *head;
    waypoint *wp;

    head = g_CurrentSetup.pathwaypoints;

    if (head != NULL) {
        wp = head;
        while (wp->padID >= 0) {
            PadRecord* var_v0 = &g_CurrentSetup.pads[wp->padID];
            if (tile == var_v0->stan) {
                return wp;
            }
            wp++;
        }
    }

    return NULL;
}


/**
 * Address 0x7F027C60.
*/
s32 check_if_any_path_preset_lies_on_tile(StandTile* tile)
{
    return get_ptrpreset_in_table_matching_tile(tile) != NULL;
}


/**
 * 100% match, unsure of argument types.
 * Addresss 0x7F027C84.
*/
f32 chrlvPadPresetRelated(coord3d *arg0, waypoint *arg1)
{
    f32 temp_f12;
    f32 temp_f2;
    PadRecord *temp_v0;

    temp_v0 = &g_CurrentSetup.pads[arg1->padID];
    temp_f2 = temp_v0->pos.f[0] - arg0->f[0];
    temp_f12 = temp_v0->pos.f[2] - arg0->f[2];
    return (temp_f2 * temp_f2) + (temp_f12 * temp_f12);
}



/**
 * Address 0x7F027CD4.
*/
waypoint *chrlvStanPathRelated(coord3d *arg0, StandTile *arg1)
{
    StandTile *tile = NULL;
    f32 temp_f20;
    waypoint *ret = NULL;
    waypoint *wayp = NULL;
    s32 *n = NULL;

    tile = stanFillSearch(arg1, check_if_any_path_preset_lies_on_tile);
    if (tile != NULL)
    {
        ret = get_ptrpreset_in_table_matching_tile(tile);

        if (ret != NULL)
        {
            temp_f20 = chrlvPadPresetRelated(arg0, ret);

            for (n = ret->neighbours; *n >= 0; n++)
            {
                wayp = &g_CurrentSetup.pathwaypoints[*n];

                if (chrlvPadPresetRelated(arg0, wayp) < temp_f20)
                {
                    ret = wayp;
                }
            }
        }
    }

    return ret;
}




/**
 * Address 0x7F027DB0.
*/
s32 chrlvStanRoomRelated(ChrRecord *self, coord3d *arg1, StandTile *tile)
{

#define BUFFER_SIZE_7F027DB0 0x14

    s32 sp48[BUFFER_SIZE_7F027DB0];
    PropRecord *prop;
    s32 tile_something;
    s32 i;

    prop = self->prop;
    tile_something = sub_GAME_7F0B0D0C(prop->stan, prop->pos.x, prop->pos.f[2], &tile, arg1->f[0], arg1->f[2], &sp48[0], BUFFER_SIZE_7F027DB0);

    if (tile_something > 0 && tile_something < BUFFER_SIZE_7F027DB0)
    {
        for (i=0; i<tile_something; i++)
        {
            if (getROOMID_isRendered(sp48[i]) != 0)
            {
                return 0;
            }
        }
    }
    else
    {
        return 0;
    }

    return 1;
}



/**
 * Address 0x7F027E70.
*/
s32 chrlvStanRoomRelatedPad(ChrRecord *self, PadRecord *arg1)
{
    return chrlvStanRoomRelated(self, &arg1->pos, arg1->stan);
}




/**
 * Address 0x7F027E90.
 * PD: chrGoPosInitMagic
*/
void chrlvSetGoposSegDistTotal(ChrRecord *self, struct waydata *arg1, coord3d *arg2)
{
    PropRecord *prop;
    f32 dx;
    f32 dz;
    f32 sp18;

    prop = self->prop;
    dx = arg2->f[0] - prop->pos.f[0];
    dz = arg2->f[2] - prop->pos.f[2];

    sp18 = atan2f(dx, dz);

    arg1->mode = 6;
    arg1->segdistdone = 0.0f;
    arg1->segdisttotal = sqrtf((dx * dx) + (dz * dz));

    setsubroty(self->model, sp18);
}


/**
 * @param self:
 * @param target_point: out paramter, will contain target position
 * @param target_stan: out parameter, will contain pointer to target stan
 *
 * Address 0x7F027F20.
 * PD: chrGoPosGetCurWaypointInfoWithFlags (somewhat similar)
*/
void chrlvActGoposRelated(ChrRecord *self, coord3d *target_point, StandTile **target_stan)
{
    waypoint *waypoint;
    PadRecord *pad;

    waypoint = self->act_gopos.waypoints[self->act_gopos.curindex];

    if (waypoint != 0)
    {
        pad = &g_CurrentSetup.pads[waypoint->padID];

        target_point->f[0] = pad->pos.f[0];
        target_point->f[1] = pad->pos.f[1];
        target_point->f[2] = pad->pos.f[2];

        *target_stan = pad->stan;
    }
    else
    {
        target_point->f[0] = self->act_gopos.targetpos.f[0];
        target_point->f[1] = self->act_gopos.targetpos.f[1];
        target_point->f[2] = self->act_gopos.targetpos.f[2];

        *target_stan = self->act_gopos.target;
    }
}


/**
 * Address 0x7F027FA8.
 * PD: func0f0370a8 (but GE has much more cases)
*/
f32 chrlvModelScaleAnimationRelated(ChrRecord *self)
{
    f32 scale_factor = D_80030984;

    if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_sprinting))
    {
        scale_factor = D_8003098C;
    }
    else if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_running))
    {
        scale_factor = D_80030988;
    }
    else if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_sprinting_one_handed_weapon))
    {
        scale_factor = D_80030998;
    }
    else if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_running_one_handed_weapon))
    {
        scale_factor = D_80030994;
    }
    else if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_walking_unarmed))
    {
        scale_factor = D_80030990;
    }
    // typo/mistake, `ANIM_DATA_sprinting_one_handed_weapon` is duplicate of above.
    // compiler swaps addition order when reading this from the stack, unlike addresses only seen once (seen once means not saved to stack).
    else if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_sprinting_one_handed_weapon))
    {
        scale_factor = D_800309A4;
    }
    else if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_running_female))
    {
        scale_factor = D_800309A0;
    }
    else if (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_walking_female))
    {
        scale_factor = D_8003099C;
    }

    return self->model->scale * scale_factor * 9.999999f;
}





/**
 * Address 0x7F028144.
 * PD: chrGoPosCalculateBaseTtl
*/
s32 chrlvMovementTargetRelated(ChrRecord *self)
{
    f32 dx;
    f32 dz;
    PropRecord *temp_v0;
    coord3d sp20; // sp32
    StandTile *sp1C; // 28
    f32 sp18; // 24

    sp18 = modelGetAbsAnimSpeed(self->model);
    chrlvActGoposRelated(self, &sp20, &sp1C);
    temp_v0 = self->prop;
    dx = sp20.f[0] - temp_v0->pos.f[0];
    dz = sp20.f[2] - temp_v0->pos.f[2];

    if (dx < 0.0f)
    {
        dx = -dx;
    }

    if (dz < 0.0f)
    {
        dz = -dz;
    }

    return (s32) ((dx + dz) / (chrlvModelScaleAnimationRelated(self) * sp18));
}



/**
 * Address 0x7F0281F4.
 * PD: chrGoPosClearRestartTtl
*/
void sub_GAME_7F0281F4(ChrRecord *self)
{
    self->act_gopos.unk5a = 0;
}


/**
 * Address 0x7F0281FC (US,JP)
 * Address 0x7F028214 (VERSION_EU)
 * PD: chrGoPosConsiderRestart
*/
void chrlvPlotCourseRelated(ChrRecord *self)
{
    s32 temp_a1;
    s32 temp_v0;
    s32 temp_v1;

    if (self->act_gopos.waydata.mode != WAYMODE_MAGIC)
    {
        temp_v0 = self->act_gopos.unk5a;

        if (temp_v0 == 0)
        {
#ifndef REFRESH_PAL
            temp_a1 = (chrlvMovementTargetRelated(self) * 2) + 300;
#else
            temp_a1 = ((chrlvMovementTargetRelated(self) * 100) + 15000) / 60;
#endif

            if (temp_a1 >= 0x10000)
            {
                temp_a1 = (u16)-1;
            }

            self->act_gopos.unk5a = (s16)temp_a1;

            return;
        }

        temp_v1 = (u16)g_ClockTimer;

        if (temp_v1 >= temp_v0)
        {
            plot_course_for_actor(self, &self->act_gopos.targetpos, self->act_gopos.target, self->act_gopos.unk59);

            return;
        }

        self->act_gopos.unk5a = (u16) (temp_v0 - temp_v1);
    }
}



/**
 * Address 0x7F02828C.
 * PD: chrGoPosInitExpensive
*/
void chrlvActGoposSetTargetPosRelated(ChrRecord *self)
{
    coord3d sp1C;
    StandTile *sp18;

    chrlvActGoposRelated(self, (coord3d *) &sp1C, &sp18);

    self->act_gopos.waydata.mode = 0;
    self->act_gopos.waydata.unk01 = 0;
    self->act_gopos.waydata.unk02 = 0;

    self->act_gopos.waydata.pos.f[0] = sp1C.f[0];
    self->act_gopos.waydata.pos.f[1] = sp1C.f[1];
    self->act_gopos.waydata.pos.f[2] = sp1C.f[2];

    sub_GAME_7F0281F4(self);
}



/**
 * Address 0x7F0282E0.
 * PD: chrGoPosAdvanceWaypoint
*/
void chrlvActGoposIncCurIndex(ChrRecord *self)
{
    if (self->act_gopos.curindex < 3)
    {
        self->act_gopos.curindex++;
    }
    else
    {
        waypoint * p = self->act_gopos.waypoints[self->act_gopos.curindex];

        self->act_gopos.curindex = 1;

        waypointFindRoute(p, self->act_gopos.target_path, (waypoint **)&self->act_gopos.waypoints, MAX_CHRWAYPOINTS);
    }

    chrlvActGoposSetTargetPosRelated(self);
}



/**
 * Determines which step index the chr will be at given their current index, the
 * number of steps to take and in which direction (forward or back).
 *
 * Returns the step index and populates *forward with true or false depending on
 * whether the chr will be traversing the path in the forward direction at that
 * point.
 *
 * Address 0x7F028348.
 *
 * PD: chrPatrolCalculateStep
 */
s32 chrlvPatrolCalculateStep(ChrRecord *self, bool *forward, s32 numsteps)
{
    s32 nextstep = self->act_patrol.nextstep;
    bool isforward = *forward;

    if (numsteps < 0)
    {
        isforward = !isforward;
        numsteps = -numsteps;
    }

    while (numsteps > 0)
    {
        numsteps--;

        if (isforward)
        {
            nextstep++;

            if (self->act_patrol.path->data[nextstep] < 0)
            {
                nextstep -= 2;

                // Reached the end of the list
                if (self->act_patrol.path->flags & 1)
                {
                    nextstep = 0;
                }
                else
                {
                    isforward = FALSE;
                }
            }
        }
        else
        {
            nextstep--;

            if (nextstep < 0)
            {
                nextstep = 1;

                // Reached the start of the list
                if (self->act_patrol.path->flags & 1)
                {
                    nextstep = self->act_patrol.path->len - 1;
                }
                else
                {
                    isforward = TRUE;
                }
            }
        }
    }

    *forward = isforward;

    return nextstep;
}


/**
 * Address 0x7F0283FC.
 * 
 * PD: chrPatrolCalculatePadNum (had some nice finds when searching for "patrol" in "chraction.c" in PD)
*/
PadRecord *chrlvGetPatrolStepPad(ChrRecord *self, s32 numsteps)
{
    waypoint *wp;
    s32 forward;
    s32 step;
    s32 *data;

    forward = self->act_patrol.forward;
    step = chrlvPatrolCalculateStep(self, &forward, numsteps);

    data = &self->act_patrol.path->data[step];

    wp = &g_CurrentSetup.pathwaypoints[*data];

    return &g_CurrentSetup.pads[wp->padID];
}


/**
 * Address 0x7F028474.
*/
PadRecord * chrlvGetNextPatrolStepPad(ChrRecord *self)
{
    return chrlvGetPatrolStepPad(self, 0);
}


/**
 * Address 0x7F028494.
*/
void chrlvSetNextActPatrolStepPadPos(ChrRecord *self)
{
    PadRecord *temp_v0;

    temp_v0 = chrlvGetNextPatrolStepPad(self);
    self->act_patrol.waydata.mode = 0;
    self->act_patrol.waydata.unk01 = 0;
    self->act_patrol.waydata.unk02 = 0;
    self->act_patrol.waydata.pos.f[0] = temp_v0->pos.f[0];
    self->act_patrol.waydata.pos.f[1] = temp_v0->pos.f[1];
    self->act_patrol.waydata.pos.f[2] = temp_v0->pos.f[2];
}


/**
 * Address 0x7F0284DC.
*/
void chrlvAdvancePatrolStep(ChrRecord *self)
{
    self->act_patrol.nextstep = chrlvPatrolCalculateStep(self, &self->act_patrol.forward, 1);
    chrlvSetNextActPatrolStepPadPos(self);
}


/**
 * Address 0x7F028510.
 * 
 * Returns true if pos is not inside the 2D collision footprint of
 * any normal object (PROP_TYPE_OBJ) in the stan's room.
*/
bool chrlvIsPosClearOfObjectBounds(coord3d *pos, StandTile *stan)
{
    s32 roomids[8];
    s16 *propnum;
    PropRecord *props = (PropRecord *)&g_Props;
    struct rect4f *polygon;
    s32 numedges;

    roomids[0] = stan->room;
    roomids[1] = -1;

    roomGetProps(roomids);

    for (propnum = ptr_list_object_lookup_indices; *propnum >= 0; propnum++)
    {
        PropRecord *prop = &props[*propnum];

        if (prop->type == PROP_TYPE_OBJ)
        {
            chraiGetCollisionBoundsWithoutY(prop, &polygon, &numedges);

            if (numedges > 0 && chrpropTestPointInPolygon(pos, polygon, numedges))
            {
                return FALSE;
            }
        }
    }

    return TRUE;
}


/**
 * contrast with @see chrlvTravelTick
 * Address 0x7F028600.
*/
#ifndef TARGET_N64
/* GE_TICKHZ: chrlvActionTick runs once per 60Hz field, so motion it integrates
 * must scale by the whole fields covered (g_ClockTimer), not the sub-field
 * g_GlobalTimerDelta. Otherwise offscreen patrol travel, run distances and
 * attack turning slow down at higher tick rates; the two are equal at tickhz
 * 60, so vanilla and demos are unchanged. */
#define CHRLV_FIELD_DELTA ((f32) g_ClockTimer)
#else
#define CHRLV_FIELD_DELTA g_GlobalTimerDelta
#endif

#ifndef TARGET_N64
/* Arrival-segment anchor: the chr's position at its PREVIOUS action tick, not
 * chr->prevpos (only one sim tick old at raised tick rates). See the note at
 * its definition below. */
coord3d *portChrArriveFrom(ChrRecord *self);
#define CHRLV_ARRIVE_FROM(self) portChrArriveFrom(self)
#else
#define CHRLV_ARRIVE_FROM(self) (&(self)->prevpos)
#endif

#ifndef TARGET_N64
/* GE_AIDBG=1: guard path-following diagnostics. Logs one line per node
 * arrival (patrol step, gopos waypoint or target) with the lateral distance the
 * chr actually arrived at, and one per course replot (the lastmoveok timeout
 * that an overshoot ends up in). Compare arrival distances and replot counts
 * across GE_TICKHZ values to measure path drift. GE_AIDBG=2 also logs a per-chr
 * breadcrumb every 64 fields. */
static s32 sPortAiDbgLevel = -1;
static s32 portAiDbgLevel(void)
{
    if (sPortAiDbgLevel < 0)
    {
        extern char *getenv(const char *);
        const char *e = getenv("GE_AIDBG");
        sPortAiDbgLevel = (e != NULL && e[0] != '0') ? (e[0] == '2' ? 2 : 1) : 0;
    }
    return sPortAiDbgLevel;
}
static void portAiDbgArrive(ChrRecord *self, const char *what, coord3d *tgt)
{
    extern int printf(const char *, ...);
    f32 dx, dz;
    if (portAiDbgLevel() < 1)
    {
        return;
    }
    dx = self->prop->pos.x - tgt->x;
    dz = self->prop->pos.z - tgt->z;
    printf("ge: aidbg t=%d chr=%d ARRIVE %s dist=%.1f pos=(%.0f %.0f) tgt=(%.0f %.0f)\n",
           g_GlobalTimer, self->chrnum, what, sqrtf(dx * dx + dz * dz),
           self->prop->pos.x, self->prop->pos.z, tgt->x, tgt->z);
}
static void portAiDbgReplot(ChrRecord *self)
{
    extern int printf(const char *, ...);
    if (portAiDbgLevel() < 1)
    {
        return;
    }
    printf("ge: aidbg t=%d chr=%d REPLOT pos=(%.0f %.0f)\n",
           g_GlobalTimer, self->chrnum, self->prop->pos.x, self->prop->pos.z);
}
static void portAiDbgCrumb(ChrRecord *self, coord3d *tgt, const char *mode)
{
    extern int printf(const char *, ...);
    extern f32 getsubroty(Model *model);
    f32 dx, dz, yaw, bearing;
    if (portAiDbgLevel() < 2 || (g_GlobalTimer & 15) != 0)
    {
        return;
    }
    dx = tgt->x - self->prop->pos.x;
    dz = tgt->z - self->prop->pos.z;
    yaw = getsubroty(self->model);
    bearing = atan2f(dx, dz);
    printf("ge: aidbg t=%d chr=%d POS %s dist=%.1f yaw=%.2f brg=%.2f spd=%.4f\n",
           g_GlobalTimer, self->chrnum, mode, sqrtf(dx * dx + dz * dz),
           yaw, bearing,
           (self->actiontype == ACT_PATROL) ? self->act_patrol.speed : -1.0f);
}
#endif

void chrlvTravelTickMagic(ChrRecord *self, struct waydata *arg1, f32 arg2, coord3d *arg3, StandTile *arg4)
{
    /**
     * Three unused stack variables.
    */
    PropRecord *self_prop;
    s32 unused1;
    s32 unused2;
    s32 unused3;
    u8 curindex;
    waypoint *pta;
    PadRecord *pad;
    coord3d sp40;
    StandTile *sp3C;

    self->invalidmove = 0;
    self->lastmoveok60 = g_GlobalTimer;
    arg1->segdistdone += arg2 * modelGetAbsAnimSpeed(self->model) * CHRLV_FIELD_DELTA;   /* GE_TICKHZ: field-paced, see CHRLV_FIELD_DELTA */

    if (arg1->segdisttotal <= arg1->segdistdone)
    {
        chrSetMoving(self, 0);

        if (
            (stanTestVolume(&arg4, arg3->f[0], arg3->f[2], self->chrwidth, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f) < 0)
            && chrlvIsPosClearOfObjectBounds(arg3, arg4))
        {
            self_prop = self->prop;
            self_prop->stan = arg4;
            self_prop->pos.f[0] = arg3->f[0];
            self_prop->pos.f[1] = arg3->f[1];
            self_prop->pos.f[2] = arg3->f[2];
            self->chrflags |= CHRFLAG_INIT;

            setsuboffset(self->model, arg3);
            sub_GAME_7F01FC10(self->model, &self_prop->pos, &self_prop->pos, &self->ground);
            chrDetectRooms(self);

            if (self->actiontype == ACT_PATROL)
            {
                chrlvAdvancePatrolStep(self);
                chrlvSetGoposSegDistTotal(self, arg1, chrlvGetNextPatrolStepPad(self));
            }
            else if (self->actiontype == ACT_GOPOS)
            {
                curindex = self->act_gopos.curindex;

                if (self->act_gopos.waypoints[curindex] == NULL)
                {
                    if (curindex > 0)
                    {
                        pta = self->act_gopos.waypoints[curindex - 1];
                        pad = &g_CurrentSetup.pads[pta->padID];

                        setsubroty(
                            self->model,
                            atan2f(self_prop->pos.f[0] - pad->pos.f[0], self_prop->pos.f[2] - pad->pos.f[2]));
                    }

                    chrlvKneelingAnimationRelated7F023E48(self);
                }
                else
                {
                    chrlvActGoposIncCurIndex(self);
                    chrlvActGoposRelated(self, &sp40, &sp3C);
                    chrlvSetGoposSegDistTotal(self, arg1, &sp40);
                }
            }
        }
        else
        {
            arg1->segdistdone = arg1->segdisttotal;

            if (self->actiontype == ACT_PATROL)
            {
                self->act_patrol.lastvisible60 = g_GlobalTimer;
                chrlvSetNextActPatrolStepPadPos(self);
            }
            else
            {
                self->act_gopos.unk9c = g_GlobalTimer;
                chrlvActGoposSetTargetPosRelated(self);
            }
        }

        chrSetMoving(self, 1);
    }
}


/**
 * Gets character segment percent completed.
 * If action type is ACT_PATROL, computes distance between chr and target pad.
 * If action type is ACT_GOPOS, computes distance between chr and target position.
 * Otherwise result is chr->prop position.
 *
 * @param self:
 * @param arg1: Out parameter. Contains result.
 *
 * Address 0x7F028894.
 * PD: chrCalculatePosition.
*/
void chrlvGetPatrolPercentOrPosition(ChrRecord *self, coord3d *arg1)
{
    PadRecord *pad;
    f32 percent;
    coord3d sp2C; // 44
    StandTile *stan;

    if ((self->actiontype == ACT_PATROL) && (self->act_patrol.waydata.mode == WAYMODE_MAGIC))
    {
        pad = chrlvGetNextPatrolStepPad(self);

        if (self->act_patrol.waydata.segdisttotal <= self->act_patrol.waydata.segdistdone)
        {
            arg1->f[0] = pad->pos.f[0];
            arg1->f[1] = pad->pos.f[1];
            arg1->f[2] = pad->pos.f[2];

            return;
        }

        percent = self->act_patrol.waydata.segdistdone / self->act_patrol.waydata.segdisttotal;

        arg1->f[0] = self->prop->pos.f[0] + ((pad->pos.f[0] - self->prop->pos.f[0]) * percent);
        arg1->f[1] = self->prop->pos.f[1] + ((pad->pos.f[1] - self->prop->pos.f[1]) * percent);
        arg1->f[2] = self->prop->pos.f[2] + ((pad->pos.f[2] - self->prop->pos.f[2]) * percent);

        return;
    }

    if ((self->actiontype == ACT_GOPOS) && (self->act_gopos.waydata.mode == WAYMODE_MAGIC))
    {
        chrlvActGoposRelated(self, &sp2C, &stan);

        if (self->act_gopos.waydata.segdisttotal <= self->act_gopos.waydata.segdistdone)
        {
            arg1->f[0] = sp2C.f[0];
            arg1->f[1] = sp2C.f[1];
            arg1->f[2] = sp2C.f[2];

            return;
        }

        percent = self->act_gopos.waydata.segdistdone / self->act_gopos.waydata.segdisttotal;

        arg1->f[0] = self->prop->pos.f[0] + ((sp2C.f[0] - self->prop->pos.f[0]) * percent);
        arg1->f[1] = self->prop->pos.f[1] + ((sp2C.f[1] - self->prop->pos.f[1]) * percent);
        arg1->f[2] = self->prop->pos.f[2] + ((sp2C.f[2] - self->prop->pos.f[2]) * percent);

        return;
    }

    arg1->f[0] = self->prop->pos.f[0];
    arg1->f[1] = self->prop->pos.f[1];
    arg1->f[2] = self->prop->pos.f[2];
}



/**
 * @param self:
 * @param arg1: sprinting animation when 2, running animation when 1, otherwise walking animation
 * @param arg2:
 *
 * Address 0x7F028A5C.
*/
void get_sound_at_range(ChrRecord *self, s32 arg1, s32 arg2)
{
    PropRecord *left;
    PropRecord *right;
    s32 ani_arg;
    s32 flag;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    if (((left != NULL) && (right != NULL)) || ((left == NULL) && (right == NULL)))
    {
        flag = 0;
        ani_arg = randomGetNext() & 1;
    }
    else
    {
        s32 t;
        if (weaponIsOneHanded(left) || weaponIsOneHanded(right))
        {
            t = 0;
            flag = t;
            ani_arg = left != NULL;
        }
        else
        {
            t = 1;
            flag = t;
            ani_arg = left != NULL;
        }
    }

    if (flag != 0)
    {
        if (arg1 == 2)
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_sprinting), ani_arg, 0.0f, 0.5f, 16.0f);
        }
        else if (arg1 == 1)
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_running), ani_arg, 0.0f, 0.5f, 16.0f);
        }
        else
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_walking), ani_arg, 0.0f, 0.5f, 16.0f);
        }

        return;
    }

    if (arg2)
    {
        if (arg1 == 2)
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_sprinting_one_handed_weapon), ani_arg, 0.0f, 0.5f, 16.0f);
        }
        else if (arg1 == 1)
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_running_one_handed_weapon), ani_arg, 0.0f, 0.5f, 16.0f);
        }
        else
        {
            modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_walking_unarmed), ani_arg, 0.0f, 0.5f, 16.0f);
        }

        return;
    }

    if (arg1 == 2)
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_sprinting_one_handed_weapon), ani_arg, 0.0f, 0.5f, 16.0f);
    }
    else if (arg1 == 1)
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_running_female), ani_arg, 0.0f, 0.5f, 16.0f);
    }
    else
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_walking_female), ani_arg, 0.0f, 0.5f, 16.0f);
    }

    return;
}



/**
 * Address 0x7F028DA0.
*/
void play_hit_soundeffect_and_proper_volume( ChrRecord *self)
{
    get_sound_at_range(self, self->act_gopos.unk59, c_item_entries[self->bodynum].isMale);
}




/**
 * Address 0x7F028DDC.
*/
s32 plot_course_for_actor(ChrRecord *self, coord3d *arg1, StandTile *stan, SPEED speed)
{
    PropRecord *prop; //sp 100
    waypoint *prop_waypoint; // sp96
    waypoint *target_waypoint; // sp92
    waypoint *sp44[MAX_CHRWAYPOINTS];
    s32 i;
    coord3d sp34;
    StandTile *sp30;
    s32 phi_v0;

    prop = self->prop;

    phi_v0 = (self->actiontype == ACT_GOPOS) && (self->act_gopos.unk59 == (u8)speed);

    prop_waypoint = chrlvStanPathRelated(&prop->pos, prop->stan);
    target_waypoint = chrlvStanPathRelated(arg1, stan);

    if ((prop_waypoint != NULL)
        && (target_waypoint != NULL)
        && !(waypointFindRoute(prop_waypoint, target_waypoint, (waypoint **)&sp44, MAX_CHRWAYPOINTS) < 2)
    )
    {
        chrStopFiring(self);

        self->actiontype = ACT_GOPOS;

        self->act_gopos.targetpos.f[0] = arg1->f[0];
        self->act_gopos.targetpos.f[1] = arg1->f[1];
        self->act_gopos.targetpos.f[2] = arg1->f[2];
        self->act_gopos.target = stan;
        self->act_gopos.target_path = target_waypoint;
        self->act_gopos.curindex = 0;
        self->act_gopos.unk59 = speed;
        self->act_gopos.speed = 0.0f;
        self->act_gopos.waydata.age = (s32) (randomGetNext() % 100U);
        self->act_gopos.waydata.unk03 = 0;
        self->act_gopos.unk9c = -1;

        for (i=0; i<MAX_CHRWAYPOINTS; i++)
        {
            self->act_gopos.waypoints[i] = sp44[i];
        }

        chrlvActGoposSetTargetPosRelated(self);
        self->sleep = 0;

        if (phi_v0 == 0)
        {
            play_hit_soundeffect_and_proper_volume(self);
        }

        chrlvActGoposRelated(self, &sp34, &sp30);

        if (((prop->flags & 2) == 0) && (chrlvStanRoomRelated(self, &sp34, sp30) != 0))
        {
            chrlvSetGoposSegDistTotal(self, &self->act_gopos.waydata, &sp34);
        }

        return 1;
    }

    return 0;
}




/**
 * Address 0x7F028FAC.
*/
void chrlvWalkingAnimationRelated(ChrRecord *self)
{
    PropRecord *left;
    PropRecord *right;
    s32 ani_arg;
    s32 flag;

    left = chrGetEquippedWeaponProp(self, GUNLEFT);
    right = chrGetEquippedWeaponProp(self, GUNRIGHT);

    if (((left != NULL) && (right != NULL)) || ((left == NULL)) && (right == NULL))
    {
        flag = 0;
        ani_arg = randomGetNext() & 1;
    }
    else
    {
        s32 t;
        if (weaponIsOneHanded(left) || weaponIsOneHanded(right))
        {
            t = 0;
            flag = t;
            ani_arg = left != NULL;
        }
        else
        {
            t = 1;
            flag = t;
            ani_arg = left != NULL;
        }
    }

    if (flag != 0)
    {
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_walking), ani_arg, 0.0f, 0.5f, 16.0f);
    }
    else
    {
        f32 tf = (0.5f * D_80030984) / D_80030990;
        modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_walking_unarmed), ani_arg, 0.0f, tf, 16.0f);
    }

    return;
}


/**
 * Address: 7F0290F8
 */
void set_actor_on_path(ChrRecord *self, struct patrol_path *path)
{
    PadRecord *pad;
    s32 next_step = -1;
    PropRecord *prop = self->prop;
    s32 count = 0;
    PadRecord *new_var;
    waypoint *pta;
    StandTile *stan;
    s32 *dataptr;
    f32 dx;
    f32 dz;

    if (path->data[0] >= 0)
    {
        do
        {
            dataptr = &path->data[count];
            pta = &g_CurrentSetup.pathwaypoints[*dataptr];
            new_var = &g_CurrentSetup.pads[pta->padID];
            pad = new_var;

#ifndef TARGET_N64
            { /* bring-up (GE_DUMP_BOND): log each step resolving the patrol path,
               * to find which pointer is garbage when the pads lookup crashes */
                extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
                static s32 once;
                if (once < 6 && ((u32) *dataptr > 4096 || (u32) pta->padID > 4096))
                {
                    f32 v[7];
                    once++;
                    v[0] = (f32) (u32) path;
                    v[1] = (f32) (u32) path->data;
                    v[2] = (f32) *dataptr;
                    v[3] = (f32) count;
                    v[4] = (f32) (u32) pta;
                    v[5] = (f32) pta->padID;
                    v[6] = (f32) (u32) g_CurrentSetup.pads;
                    ge_dbg_vec("pathbad path/data/wpidx/count/wp/padid/pads", v, 7);
                }
            }
#endif

            if ((pad->stan != NULL) && (pad->stan == prop->stan))
            {
                dx = pad->pos.f[0] - prop->pos.f[0];
                dz = pad->pos.f[2] - prop->pos.f[2];

                if (((dx * dx) + (dz * dz)) < 10000.0f)
                {
                    if (((pad->pos.f) && (pad->pos.f)) && (pad->pos.f));
                    
                    next_step = count;
                    break;
                }
            }

            count++;
        }
        while (path->data[count] >= 0);
    }

    if (next_step < 0)
    {
        #ifdef DEBUG
        osSyncPrintf("Patrol first step not found for chr number %d\n",self->chrnum );
        #endif
        next_step = 0;
    }

    chrStopFiring(self);

    self->actiontype = ACT_PATROL;
    self->act_patrol.path = path;
    self->act_patrol.nextstep = next_step;
    self->act_patrol.forward = 1;
    self->act_patrol.waydata.age = randomGetNext() % 0x64U;
    self->act_patrol.waydata.unk03 = 0;
    self->act_patrol.lastvisible60 = -1;
    self->act_patrol.speed = 0.0f;

    chrlvSetNextActPatrolStepPadPos(self);

    self->sleep = 0;

    chrlvWalkingAnimationRelated(self);

    pad = chrlvGetNextPatrolStepPad(self);

    if ((self->prop->flags & PROPFLAG_ONSCREEN) == FALSE)
    {
        if (chrlvStanRoomRelatedPad(self, pad) != 0)
        {
            chrlvSetGoposSegDistTotal(self, &self->act_patrol.waydata, &pad->pos);
        }
    }
}


void setSeenBondTimeToNow(ChrRecord *self)
{
  self->seen_bond_time = g_GlobalTimer;
  return;
}



/**
 * Address 0x7F0292A8.
*/
s32 chrlvAttackRelated7F0292A8(ChrRecord *self, coord3d *arg1, StandTile *arg2)
{
    s32 ret;
    s32 flags;
    StandTile *stan;
    StandTile *sp40;
    coord3d *sp3C;

    ret = 0;
    flags = TARGET_BOND;

    if (self->actiontype == ACT_ATTACK)
    {
        flags = self->act_attack.attacktype;
    }

    if ((flags & TARGET_FRONT_OF_CHR) != 0)
    {
        ret = 1;
    }
    else
    {
        stan = arg2;
        sp3C = chrlvGetChrOrPresetLocation(self, flags, self->act_attack.entityid, &sp40);
        chrSetMoving(self, 0);

        if ((flags & 1) != 0)
        {
            bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 0);

            if (bondviewGetVisibleToGuardsFlag() != 0)
            {
                if ((stanTestLineUnobstructed(&stan, arg1->x, arg1->f[2], sp3C->x, sp3C->f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, arg1->f[1], arg1->f[1], sp3C->f[1], sp3C->f[1]) != 0) && (stan == sp40))
                {
                    setSeenBondTimeToNow(self);
                    ret = 1;
                }
            }

            bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 1);
        }
        else if ((flags & 4) != 0)
        {
            if ((stanTestLineUnobstructed(&stan, arg1->x, arg1->f[2], sp3C->x, sp3C->f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, arg1->f[1], arg1->f[1], sp3C->f[1], sp3C->f[1]) != 0) && (stan == sp40))
            {
                ret = 1;
            }
        }
        else if ((flags & 8) != 0)
        {
            if ((stanTestLineUnobstructed(&stan, arg1->x, arg1->f[2], sp3C->x, sp3C->f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, arg1->f[1], arg1->f[1], sp3C->f[1], sp3C->f[1]) != 0) && (stan == sp40))
            {
                ret = 1;
            }
        }

        chrSetMoving(self, 1);
    }

    return ret;
}




/**
 * Address 0x7F0294BC.
*/
bool chrCanSeeBond(ChrRecord *self)
{
    bool pass = FALSE;
    PropRecord *myprop;
    PropRecord *bondprop;
    StandTile *mystan;
    f32 myheight;

    if (bondviewGetVisibleToGuardsFlag())
    {
        myprop   = self->prop;
        bondprop = getCurrentPlayerProp();
        myheight = self->chrheight - 20.0f;

        chrSetMoving(self, FALSE);
        bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 0);

        mystan = myprop->stan;

#ifdef TARGET_N64
        if (stanTestLineUnobstructed(&mystan, myprop->pos.x, myprop->pos.z, bondprop->pos.x, bondprop->pos.z, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, myheight, myheight, 0.0f, 1.0f) && (mystan == bondprop->stan))
        {
            setSeenBondTimeToNow(self);
            pass = TRUE;
        }
#else
        /* PORT: split the line-of-sight test into its two failure causes (the
         * line hit something, or it ended on a different tile than Bond's) so
         * GE_LOSDBG=1 can log which one changed. */
        {
            s32 unob = stanTestLineUnobstructed(&mystan, myprop->pos.x, myprop->pos.z, bondprop->pos.x, bondprop->pos.z, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, myheight, myheight, 0.0f, 1.0f);
            s32 samestan = (mystan == bondprop->stan);

            {
                extern char *getenv(const char *);
                extern int printf(const char *, ...);
                static s32 losdbg = -1;
                static u8 seen[256];   /* per-chr last state + 1, 0 = never */
                static s32 lasttick;
                s32 slot = (s32) ((u32) self->chrnum & 0xFF);
                s32 state = (unob ? 2 : 0) | (samestan ? 1 : 0);

                if (losdbg < 0)
                {
                    losdbg = getenv("GE_LOSDBG") != NULL;
                }
                if (losdbg &&
                    (seen[slot] != (u8) (state + 1) ||
                     (s32) (g_GlobalTimer - lasttick) > 300))
                {
                    seen[slot] = (u8) (state + 1);
                    lasttick = g_GlobalTimer;
                    printf("ge: los: chr %d -> Bond: unobstructed=%d endtile=%p bondtile=%p (%s) "
                           "me=(%.0f,%.0f) bond=(%.0f,%.0f)\n",
                           self->chrnum, unob, (void *) mystan, (void *) bondprop->stan,
                           samestan ? "same tile" : "TILE MISMATCH",
                           myprop->pos.x, myprop->pos.z, bondprop->pos.x, bondprop->pos.z);
                }
            }

            if (unob && samestan)
            {
                setSeenBondTimeToNow(self);
                pass = TRUE;
            }
        }
#endif

        chrSetMoving(self, TRUE);
        bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 1);
    }

    return pass;
}




/**
 * Address 0x7F0295D0.
*/
bool check_if_position_in_same_room(ChrRecord *self, coord3d *pos, StandTile *stan)
{
    PropRecord *myprop   = self->prop;
    StandTile  *propstan;
    f32         myheight = self->chrheight - 20.0f;
    bool        pass     = FALSE;

    chrSetMoving(self, 0);

    propstan = myprop->stan;

    if (stanTestLineUnobstructed(&propstan, myprop->pos.x, myprop->pos.z, pos->x, pos->z, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE, myheight, myheight, 0.0f, 1.0f) && (propstan == stan))
    {
        pass = TRUE;
    }

    chrSetMoving(self, 1);

    return pass;
}



/**
 * Address 0x7F02969C.
*/
s32 chrlvMaybeSameRoom(ChrRecord *self, coord3d *arg1, StandTile *arg2)
{
    f32 atan;
    f32 roty;
    f32 df;

    roty = getsubroty(self->model);
    atan = atan2f(arg1->f[0] - self->prop->pos.f[0], arg1->f[2] - self->prop->pos.f[2]);
    df = atan - roty;

    if (atan < roty)
    {
        df += M_TAU_F;
    }
    // if NOT in rear left quadrant?
    if ((df < DegToRad(100)) || (df > DegToRad(260)))
    {
        return check_if_position_in_same_room(self, arg1, arg2);
    }

    return 0;
}




/**
 * Address 0x7F029760.
*/
s32 chrlvCurrentPlayerCall7F0B0E24(ChrRecord *self)
{
    PropRecord *sp3C;
    PropRecord *bond_prop;
    StandTile *bond_stan;
    s32 ret;

    sp3C = self->prop;
    bond_prop = getCurrentPlayerProp();
    ret = 0;

    bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 0);

    bond_stan = bond_prop->stan;

    if ((stanTestLineUnobstructed(
            &bond_stan,
            bond_prop->pos.f[0],
            bond_prop->pos.f[2],
            sp3C->pos.f[0],
            sp3C->pos.f[2],
            0x13,
            bond_prop->pos.f[1],
            bond_prop->pos.f[1],
            0.0f,
            1.0f) != 0)
        && (bond_stan == sp3C->stan))
    {
        ret = 1;
    }

    bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 1);

    return ret;
}




/**
 * Address 0x7F02982C.
*/
s32 chrlvCall7F0B0E24WithChrWidthHeight(PropRecord *arg0, coord3d *arg1, coord3d *arg2)
{
    ChrRecord *sp7C;
    f32 sp78;
    f32 sp74;
    f32 sp70;
    f32 sp6C;
    StandTile *stan;
    f32 chrx;
    f32 chrz;
    s32 ret; // sp92
    f32 sp58; // sp88
    f32 sp54; // sp84
    f32 sp50; // sp80

    sp7C = arg0->chr;
    chrx = arg2->f[0] * sp7C->chrwidth * 1.2f;
    chrz = arg2->f[2] * sp7C->chrwidth * 1.2f;
    ret = 0;

    chrGetChrWidthHeight(arg0, &sp50, &sp58, &sp54);
    chrSetMoving(sp7C, 0);

    sp78 = arg0->pos.f[0] + chrz;
    sp74 = arg0->pos.f[2] - chrx;
    sp70 = arg1->f[0] + chrz + chrx;
    sp6C = (arg1->f[2] - chrx) + chrz;

    stan = arg0->stan;

    if (
        (stanTestLineUnobstructed(&stan, arg0->pos.f[0], arg0->pos.f[2], sp78, sp74, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER , sp58, sp54, 0.0f, 1.0f) != 0)
        && (stanTestLineUnobstructed(&stan, sp78, sp74, sp70, sp6C, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER , sp58, sp54, 0.0f, 1.0f) != 0)
        )
    {
        sp78 = arg0->pos.f[0] - chrz;
        sp74 = arg0->pos.f[2] + chrx;
        sp70 = (arg1->f[0] - chrz) + chrx;
        sp6C = arg1->f[2] + chrx + chrz;

        // why is this getting set again?
        stan = arg0->stan;

        if (
            (stanTestLineUnobstructed(&stan, arg0->pos.f[0], arg0->pos.f[2], sp78, sp74, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER , sp58, sp54, 0.0f, 1.0f) != 0)
            && (stanTestLineUnobstructed(&stan, sp78, sp74, sp70, sp6C, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER , sp58, sp54, 0.0f, 1.0f) != 0)
            )
        {
            ret = 1;
        }
    }

    chrSetMoving(sp7C, 1);

    return ret;
}




/**
 * Addres 0x7F029A94.
*/
s32 chrlvCall7F02982C(PropRecord *arg0, coord3d *arg1, f32 arg2)
{
    coord3d sp1C;

    sp1C.f[0] = arg0->pos.f[0] + (arg1->f[0] * arg2);
    sp1C.f[1] = arg0->pos.f[1];
    sp1C.f[2] = arg0->pos.f[2] + (arg1->f[2] * arg2);

    return chrlvCall7F0B0E24WithChrWidthHeight(arg0, &sp1C, arg1);
}



/**
 * Unreferenced.
 *
 * Address 0x7F029AF0.
*/
s32 chrlvCall7F0B0E24Normalized(PropRecord *arg0, coord3d *arg1)
{
    coord3d sp24;
    f32 temp_f2;

    sp24.f[0] = arg1->f[0] - arg0->pos.f[0];
    sp24.f[1] = 0.0f;
    sp24.f[2] = arg1->f[2] - arg0->pos.f[2];

    if ((sp24.f[0] == 0.0f) && (sp24.f[2] == 0.0f))
    {
        return 1;
    }

    temp_f2 = 1.0f / sqrtf(SQR(sp24.f[0]) + SQR(sp24.f[2]));

    sp24.f[0] *= temp_f2;
    sp24.f[2] *= temp_f2;

    return chrlvCall7F0B0E24WithChrWidthHeight(arg0, arg1, &sp24);
}




/**
 * Same as chrlvAlertGuardToPlayerPosition, except without setting `hidden` flag 0x2.
 *
 * Address 0x7F029BB0.
*/
void chrlvSetTargetToPlayer(ChrRecord *self)
{
    PropRecord *temp_v0;

    temp_v0 = getCurrentPlayerProp();
    self->lastseetarget60 = g_GlobalTimer;
    self->lastknowntargetpos.f[0] = temp_v0->pos.f[0];
    self->lastknowntargetpos.f[1] = temp_v0->pos.f[1];
    self->lastknowntargetpos.f[2] = temp_v0->pos.f[2];
    self->targetTile = temp_v0->stan;
}




/**
 * See also chrlvSetTargetToPlayer.
 *
 * Address 0x7F029C00.
 */
void chrlvAlertGuardToPlayerPosition(ChrRecord *self)
{
    PropRecord *temp_v0;

    temp_v0 = getCurrentPlayerProp();
    self->hidden |= CHRHIDDEN_ALERT_GUARD_RELATED;
    self->lastheartarget60 = g_GlobalTimer;
    self->lastknowntargetpos.f[0] = temp_v0->pos.x;
    self->lastknowntargetpos.f[1] = temp_v0->pos.y;
    self->lastknowntargetpos.f[2] = temp_v0->pos.z;
    self->targetTile = temp_v0->stan;
}


/**
 * Address 0x7F029C5C.
*/
bool chrHasStoppedOrPatroling(ChrRecord *self) //chrHasStoppedOrPatroling
{
    if ((self->actiontype == ACT_STAND) && !self->act_stand.prestand && !self->act_stand.reaim)
    {
        return TRUE;
    }
    else if (self->actiontype == ACT_ANIM)
    {
        if (self->act_anim.playSfx ||
            ((modelGetAnimSpeed(self->model) >= 0.0f) && modelGetAnimFrame(self->model) >= modelGetAnimEndFrame(self->model)) ||
            ((modelGetAnimSpeed(self->model)  < 0.0f) && modelGetAnimFrame(self->model) <= 0.0f)
           )
        {
            return TRUE;
        }
    }
    else if (self->actiontype == ACT_PATROL)
    {
        return TRUE;
    }

    return FALSE;
}




/**
 * Address 0x7F029D70.
*/
bool chrCheckTargetInSight(ChrRecord *self)
{
    PropRecord *myprop;
    PropRecord *bondprop;
    f32         rrr;
    f32         vec2rd;
    f32         myRadDirection;
    coord3d     vec;
    f32         atn;
    f32         radChangeToFaceBond;
    bool        pass;
    u32         rt;
    s32         distance;

    myprop               = self->prop;
    bondprop             = getCurrentPlayerProp();
    myRadDirection       = getsubroty(self->model);
    //Note: x and z get swapped
    vec.z                = bondprop->pos.x - myprop->pos.x;
    vec.y                = bondprop->pos.y - myprop->pos.y;
    vec.x                = bondprop->pos.z - myprop->pos.z;

    atn = atan2f(vec.z, vec.x);

    pass                = FALSE;
    rrr                 = atn - myRadDirection;
    radChangeToFaceBond = rrr;

    if (atn < myRadDirection)
    {
        radChangeToFaceBond = rrr + M_TAU_F;
    }

    if (chrSawTargetRecently(self))
    {
        pass = TRUE;
    }
    else
    {
        vec2rd = SQR(vec.z) + SQR(vec.y) + SQR(vec.x);

        if (
            /*within 220 degrees of forward and within range*/
            (
                (vec2rd < (self->visionrange * self->visionrange * 100.0f * 100.0f)) &&
                ((radChangeToFaceBond < DegToRad(110)) || (radChangeToFaceBond > DegToRad(360 - 110)))
            )
            ||
            /*or within clamped minimum of 200*/
            (
                (vec2rd < SQR(200)) &&
                ((radChangeToFaceBond < DegToRad(110)) || (radChangeToFaceBond > DegToRad(360 - 110)))
            )
        )
        {
            if (vec2rd < fogGetScaledFarFogIntensitySquared())
            {
                distance = (s32)((sqrtf(vec2rd) * 30.0f) / 16000.0f);

                //Not facing bond
                if ((radChangeToFaceBond > DegToRad(45)) && (radChangeToFaceBond < DegToRad(360.0 - 45)))
                {
                    f32 f0 = radChangeToFaceBond;
                    if (radChangeToFaceBond > M_PI_F)
                    {
                        //confine/wrap to half
                        f0 = M_TAU_F - radChangeToFaceBond;
                    }

                    f0 -= DegToRad(45);

                    distance *= (s32)((f0 * 24.0f) / M_TAU_F) + 1;
                }

                distance = chrlvGetGuard007SpeedRatingInt(self, distance) + 1;
                pass = ((u32)randomGetNext() % (u32)distance) == 0;
            }
        }
    }

#ifndef TARGET_N64
    { /* GE_LOSDBG: the sight ray (chrCanSeeBond) is only the last of several
       * checks, so a chr can act as if Bond is invisible while the ray works.
       * Log which check failed:
       *   cone  - angle to Bond outside +/-110 degrees of facing
       *   range - beyond visionrange (metres * 100) and the 200-unit close range
       *   fog   - beyond fogGetScaledFarFogIntensitySquared() (world units from
       *           the viewport z-range / visibility scale; the port's fog changes
       *           make this the most likely to differ from N64)
       *   roll  - the random detection roll ((rand %% distance) == 0)
       *   ray   - all checks passed but chrCanSeeBond said no
       * Logged per chr when the outcome changes, plus a 5s heartbeat. */
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        static s32 dbg = -1;
        static u8 seen[256];
        static s32 lasttick;
        s32 slot = (s32) ((u32) self->chrnum & 0xFF);
        f32 dist2 = SQR(vec.z) + SQR(vec.y) + SQR(vec.x);
        f32 fog2 = fogGetScaledFarFogIntensitySquared();
        f32 range2 = self->visionrange * self->visionrange * 100.0f * 100.0f;
        f32 conedeg = radChangeToFaceBond * 360.0f / M_TAU_F;
        s32 incone = (radChangeToFaceBond < DegToRad(110)) || (radChangeToFaceBond > DegToRad(360 - 110));
        const char *why = pass ? "SEES" :
                          (!incone ? "cone" :
                          ((dist2 >= range2 && dist2 >= SQR(200)) ? "range" :
                          ((dist2 >= fog2) ? "fog" : "roll/ray")));

        if (dbg < 0)
        {
            dbg = getenv("GE_LOSDBG") != NULL;
        }
        if (dbg && (seen[slot] != (u8) (pass + 1) || (s32) (g_GlobalTimer - lasttick) > 300))
        {
            seen[slot] = (u8) (pass + 1);
            lasttick = g_GlobalTimer;
            printf("ge: sight: chr %d %s dist=%.0f visionrange=%.1f (limit %.0f) fog_limit=%.0f "
                   "cone=%.0fdeg incone=%d sawrecently=%d\n",
                   (s32) self->chrnum, why, sqrtf(dist2), self->visionrange, sqrtf(range2),
                   sqrtf(fog2), conedeg, incone, chrSawTargetRecently(self));
        }
    }
#endif

    if (pass)
    {
        pass = chrCanSeeBond(self);
    }

    if (pass)
    {
        chrlvSetTargetToPlayer(self);
    }

    return pass;
}



/**
 * get vector to run on
 * @param self:
 * @param side: If GUNLEFT set result is (dz, -dx), otherwise (-dz, dx).
 * @param arg2: Out parameter. Contains result vector.
 *
 * Address 0x7F02A044.
*/
void chrlvNormDistanceToPlayer(ChrRecord *self, GUNHAND side, vec3d *vec)
{
    PropRecord *prop;
    f32 norm;
    f32 dx;
    f32 dz;
    PropRecord *player_prop;

    prop = self->prop;
    player_prop = getCurrentPlayerProp();
    dx = player_prop->pos.f[0] - prop->pos.f[0];
    dz = player_prop->pos.f[2] - prop->pos.f[2];

    norm = sqrtf((dx * dx) + (dz * dz));

    dx = dx / norm;
    dz = dz / norm;

    if (side != GUNRIGHT)
    {
        vec->f[1]  = 0;
        vec->f[0]  = dz;
        vec->f[2]  = -(dx);
    }
    else
    {
        vec->f[2]  = dx;
        vec->f[0]  = -(dz);
        vec->f[1]  = 0;
    }
}




/**
 * chrIsClearLow
 * @see sub_GAME_7F02A1E8
 * Address 0x7F02A0EC.
*/
s32 sub_GAME_7F02A0EC(ChrRecord *self, GUNHAND side, f32 distance)
{
    PropRecord *prop;
    coord3d sp28;
    coord3d sp1C;

    prop = self->prop;
    chrlvNormDistanceToPlayer(self, side, &sp28);

    sp1C.f[0] = prop->pos.f[0] + (sp28.f[0] * distance);
    sp1C.f[1] = prop->pos.f[1];
    sp1C.f[2] = prop->pos.f[2] + (sp28.f[2] * distance);

    return chrlvCall7F0B0E24WithChrWidthHeight(prop, &sp1C, &sp28);
}




/**
 * @param self:
 * @param arg1: flag. If set result is (cos, -sin), otherwise (-cos, sin).
 * @param arg2: out parameter, contains coordinate result.
 *
 * Address 0x7F02A15C.
*/
void chrlvModelRotyRelated(ChrRecord *self, s32 arg1, coord3d *arg2)
{
    f32 temp_f12;

    temp_f12 = getsubroty(self->model);

    if (arg1 != 0)
    {
        arg2->f[0] = cosf(temp_f12);
        arg2->f[1] = 0.0f;
        arg2->f[2] = -sinf(temp_f12);
    }
    else
    {
        arg2->f[0] = -cosf(temp_f12);
        arg2->f[1] = 0.0f;
        arg2->f[2] = sinf(temp_f12);
    }
}




/**
 * chrIsClear
 * @see sub_GAME_7F02A0EC
 *
 * Address 0x7F02A1E8.
*/
s32 sub_GAME_7F02A1E8(ChrRecord *self, GUNHAND side, f32 distance)
{
    PropRecord *prop;
    coord3d sp28;
    coord3d sp1C;

    prop = self->prop;
    chrlvModelRotyRelated(self, side, &sp28);

    sp1C.f[0] = prop->pos.f[0] + (sp28.f[0] * distance);
    sp1C.f[1] = prop->pos.f[1];
    sp1C.f[2] = prop->pos.f[2] + (sp28.f[2] * distance);

    return chrlvCall7F0B0E24WithChrWidthHeight(prop, &sp1C, &sp28);
}




bool chrIsNotDeadOrShot(ChrRecord *self)
{
    s8 currentaction = self->actiontype;

    if ((currentaction == ACT_DIE) || (currentaction == ACT_DEAD) || (currentaction == ACT_PREARGH)
        || (currentaction == ACT_ARGH) && !(self->chrflags & CHRFLAG_00000200))
    {
        return FALSE;
    }

    return TRUE;
}



bool chrIsDead(ChrRecord *self)
{
    s8 currentaction = self->actiontype;

    return ((currentaction == ACT_DIE) || (currentaction == ACT_DEAD));
}



/**
 * Address 0x7F02A2C8.
*/
bool actor_steps_sideways(ChrRecord *self)
{
    PropRecord *myprop;
    PropRecord *bondprop;
    int pad1; //needed for stack size - check debug rom
    f32 myRadDirection;
    int pad2; //needed for stack size
    f32 myRadDirectionToBond;
    f32 radChangeToFaceBond;
    GUNHAND HopOtherDirection; //needed for stack size
    GUNHAND HopDirection;
    f32 myNormalizedRadToBond;

    if (chrIsNotDeadOrShot(self))
    {
        myprop                = self->prop;
        bondprop              = getCurrentPlayerProp();
        myRadDirection        = getsubroty(self->model);
        myRadDirectionToBond  = atan2f(bondprop->pos.x - myprop->pos.x, bondprop->pos.z - myprop->pos.z);
        radChangeToFaceBond   = myRadDirectionToBond - myRadDirection;
        myNormalizedRadToBond = radChangeToFaceBond;

        if (myRadDirectionToBond < myRadDirection) //avoid negative radians
        {
            myNormalizedRadToBond = radChangeToFaceBond + M_TAU_F;
        }

        if ((myNormalizedRadToBond < DegToRad(45)) || (myNormalizedRadToBond > DegToRad(360.0 - 45)) ||        /*Front*/
            ((myNormalizedRadToBond > DegToRad(180.0 - 45)) && (myNormalizedRadToBond < DegToRad(180.0 + 45))) /*Back*/
        )
        {
            HopDirection = (randomGetNext() & 1) == 0;         //Hop Left or Right
            if (sub_GAME_7F02A1E8(self, HopDirection, 100.0f)) //able to step dir?
            {
                chrlvSideStepAnimationRelated(self, HopDirection);
                return TRUE;
            }

            HopOtherDirection = HopDirection == 0;

            if (sub_GAME_7F02A1E8(self, HopOtherDirection, 100.0f)) //able to step other dir?
            {
                chrlvSideStepAnimationRelated(self, HopOtherDirection);
                return TRUE;
            }
        }
    }

    return FALSE; //unable to step
}



/**
 * Address 0x7F02A428.
*/
bool actor_hops_sideways(ChrRecord *self)
{
    PropRecord *myprop;
    PropRecord *bondprop;
    int pad1; //needed for stack size - check debug rom
    f32 myRadDirection;
    int pad2; //needed for stack size
    f32 myRadDirectionToBond;
    f32 radChangeToFaceBond;
    GUNHAND HopOtherDirection; //needed for stack size
    GUNHAND HopDirection;
    f32 myNormalizedRadToBond;

    if (chrIsNotDeadOrShot(self))
    {
        myprop                = self->prop;
        bondprop              = getCurrentPlayerProp();
        myRadDirection        = getsubroty(self->model);
        myRadDirectionToBond  = atan2f(bondprop->pos.x - myprop->pos.x, bondprop->pos.z - myprop->pos.z);
        radChangeToFaceBond   = myRadDirectionToBond - myRadDirection;
        myNormalizedRadToBond = radChangeToFaceBond;

        if (myRadDirectionToBond < myRadDirection) //avoid negative radians
        {
            myNormalizedRadToBond = radChangeToFaceBond + M_TAU_F;
        }

        if ((myNormalizedRadToBond < DegToRad(45)) || (myNormalizedRadToBond > DegToRad(360.0 - 45)) ||        /*Front*/
            ((myNormalizedRadToBond > DegToRad(180.0 - 45)) && (myNormalizedRadToBond < DegToRad(180.0 + 45))) /*Back*/
        )
        {
            HopDirection = (randomGetNext() & 1) == 0;         //Hop Left or Right

            if (sub_GAME_7F02A1E8(self, HopDirection, 200.0f)) //able to hop dir?
            {
                chrlvFireJumpToSideAnimationRelated(self, HopDirection);
                return TRUE;
            }

            HopOtherDirection = HopDirection == 0;

            if (sub_GAME_7F02A1E8(self, HopOtherDirection, 200.0f)) //able to hop other dir?
            {
                chrlvFireJumpToSideAnimationRelated(self, HopOtherDirection);
                return TRUE;
            }
        }
    }

    return FALSE; //unable to hop
}



/**
 * Address 0x7F02A588.
*/
bool actor_jogs_sideways(ChrRecord *self)
{
    PropRecord *myprop;
    f32         distToRun;
    vec3d       TargetVector;
    coord3d     TargetCoord;

    if (chrIsNotDeadOrShot(self) && ((g_GlobalTimer - self->lastwalk60) >= CHRLV_RECENT_TIME_CHECK)) //>3 seconds since last walk
    {
        myprop    = self->prop;
        distToRun = ((u32)randomGetNext() * (1.0f / UINT_MAX) * 200.0f) + 200.0f;         //random dist to run between 0 and 200
        chrlvNormDistanceToPlayer(self, ((u32)randomGetNext() & 1) == 0, &TargetVector);  //get vector to run on

        TargetCoord.x = (TargetVector.x * distToRun) + myprop->pos.x;
        TargetCoord.y = myprop->pos.y;
        TargetCoord.z = (TargetVector.z * distToRun) + myprop->pos.z;

        if (chrlvCall7F0B0E24WithChrWidthHeight(myprop, &TargetCoord, &TargetVector))
        {
            sub_GAME_7F024CF8(self, &TargetCoord);
            return TRUE;
        }

        TargetVector.x = -TargetVector.x;
        TargetVector.z = -TargetVector.z;
        TargetCoord.x  = (TargetVector.x * distToRun) + myprop->pos.x;
        TargetCoord.y  = myprop->pos.y;
        TargetCoord.z  = (TargetVector.z * distToRun) + myprop->pos.z;


        if (chrlvCall7F0B0E24WithChrWidthHeight(myprop, &TargetCoord, &TargetVector))
        {
            sub_GAME_7F024CF8(self, &TargetCoord);
            return TRUE;
        }
    }

    return FALSE;
}



/**
 * Address 0x7F02A704.
*/
bool actor_walks_and_fires(ChrRecord *self)
{
    PropRecord *myprop;
    PropRecord *bondprop;

    if (chrIsNotDeadOrShot(self))
    {
        myprop   = self->prop;
        bondprop = getCurrentPlayerProp();

        if (
            (chrGetEquippedWeaponPropWithCheck(self, GUNRIGHT) || chrGetEquippedWeaponPropWithCheck(self, GUNLEFT))
            &&
            ((g_GlobalTimer - self->lastwalk60) >= CHRLV_RECENT_TIME_CHECK)
            )
        {
            f32 dx = bondprop->pos.x - myprop->pos.x;
            f32 dy = bondprop->pos.y - myprop->pos.y;
            f32 dz = bondprop->pos.z - myprop->pos.z;

            if ( (SQR(dx) + SQR(dy) + SQR(dz)) >= (1000000.0f))
            {
                chrlvInitActAttackWalk(self, SPEED_WALK);
                return TRUE;
            }
        }
    }

    return FALSE;
}



/**
 * Address 0x7F02A7F8.
*/
bool actor_runs_and_fires(ChrRecord *self)
{
    PropRecord *myprop;
    PropRecord *bondprop;

    if (chrIsNotDeadOrShot(self))
    {
        myprop   = self->prop;
        bondprop = getCurrentPlayerProp();

        if (
            (chrGetEquippedWeaponPropWithCheck(self, GUNRIGHT) || chrGetEquippedWeaponPropWithCheck(self, GUNLEFT))
            &&
            ((g_GlobalTimer - self->lastwalk60) >= CHRLV_RECENT_TIME_CHECK)
            )
        {
            f32 dx = bondprop->pos.x - myprop->pos.x;
            f32 dy = bondprop->pos.y - myprop->pos.y;
            f32 dz = bondprop->pos.z - myprop->pos.z;

            if ((SQR(dx) + SQR(dy) + SQR(dz)) >= (1000000.0f))
            {
                chrlvInitActAttackWalk(self, SPEED_RUN);
                return TRUE;
            }
        }
    }

    return FALSE;
}



/**
 * Address 0x7F02A8EC.
*/
bool actor_rolls_fires_crouched(ChrRecord *self)
{
    PropRecord *myprop;
    PropRecord *bondprop;

    vec3d vec;

    GUNHAND HopOtherDirection;
    GUNHAND HopDirection;
    float vec2rd;

    if (chrIsNotDeadOrShot(self))
    {
        myprop   = self->prop;
        bondprop = getCurrentPlayerProp();

        if (chrGetEquippedWeaponPropWithCheck(self, GUNRIGHT) || chrGetEquippedWeaponPropWithCheck(self, GUNLEFT))
        {
            vec.x  = bondprop->pos.x - myprop->pos.x;
            vec.y  = bondprop->pos.y - myprop->pos.y;
            vec.z  = bondprop->pos.z - myprop->pos.z;
            vec2rd = SQR(vec.x) + SQR(vec.y) + SQR(vec.z);

            if (SQR(200.0f) <= vec2rd) /*Bond GT 200 from chr*/
            {
                HopDirection = (randomGetNext() & 1) == 0; //Hop Left or Right

                if (sub_GAME_7F02A0EC(self, HopDirection, 200))
                {
                    chrlvInitActAttackRoll(self, HopDirection);
                    return TRUE;
                }

                HopOtherDirection = HopDirection == 0;

                if (sub_GAME_7F02A0EC(self, HopOtherDirection, 200))
                {
                    chrlvInitActAttackRoll(self, HopOtherDirection);
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}



/**
 * Address 0x7F02AA1C.
*/
bool actor_aim_at_actor(ChrRecord *self, s32 attack_type, s32 b)
{
    if ((chrIsNotDeadOrShot(self)) &&
        ((chrGetEquippedWeaponPropWithCheck(self, GUNRIGHT)) || (chrGetEquippedWeaponPropWithCheck(self, GUNLEFT))))
    {
        sub_GAME_7F025560(self, attack_type, b);
        return TRUE;
    }

    return FALSE;
}




/**
 * Address 0x7F02AA88.
*/
bool actor_kneel_aim_at_actor(ChrRecord *self, s32 targettype, s32 targetid)
{
    if ((chrIsNotDeadOrShot(self)) &&
        ((chrGetEquippedWeaponPropWithCheck(self, GUNRIGHT)) || (chrGetEquippedWeaponPropWithCheck(self, GUNLEFT))))
    {
        sub_GAME_7F0256F0(self, targettype, targetid);
        return TRUE;
    }

    return FALSE;
}



/**
 * Address 0x7F02AAF4
*/
bool actor_fire_or_aim_at_target_update(ChrRecord *self, s32 newtargettype, s32 newtargetid)
{
    if (self->actiontype == ACT_ATTACK)
    {
        if (self->act_attack.attacktype & (TARGET_AIM_ONLY | TARGET_DONTTURN))
        {
            // Hack borrowed from the 1964 emulator: it clears TARGET_AIM_ONLY
            // (and likely other flags), which may stop guards firing forever
            // and also fixed crate guards freezing.
#ifndef TARGET_N64
            /* PORT diagnostic: GE_NO1964HACK=1 disables the hack, to compare
             * guard fire/accuracy feel. Unset keeps the hack active. */
            static int portNo1964Hack = -1;
            if (portNo1964Hack < 0) {
                extern char *getenv(const char *);
                portNo1964Hack = getenv("GE_NO1964HACK") != NULL;
            }
            if (!portNo1964Hack)
#endif
            if (self->model->framea == self->model->frameb)
            {
                // Danger. Could cause out of bound reads if this keeps
                // getting called.
                self->model->frameb++;
            }

            self->act_attack.attacktype = newtargettype;
            self->act_attack.entityid   = newtargetid;
            chrlvAttackActionRelated(self);
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F02AB44.
*/
bool check_set_actor_standing_still(ChrRecord *self, s32 faceentitytype, s32 faceentityid)
{
    if (chrIsNotDeadOrShot(self) != 0)
    {
        if (self->actiontype != ACT_STAND)
        {
            chrlvKneelingAnimationRelated(self);
        }

        self->act_stand.face_entitytype = faceentitytype;
        self->act_stand.face_entityid   = faceentityid;
        self->act_stand.reaim          = 0;
        self->act_stand.checkfacingwall          = 0;

        return TRUE;
    }

    return FALSE;
}



/**
 * Address 0x7F02ABB4.
*/
bool chrGoToPad(ChrRecord *self, s32 padid, SPEED speed)
{
    PadRecord *pad;
    StandTile *stan2; //sp38
    coord3d region;
    StandTile *stan; //sp28 - wow, deliberate duplicate...

    if ((padid >= 0) && chrIsNotDeadOrShot(self) && (g_SeenBondRecentlyGuardCount < 10))
    {
        padid = chrResolvePadId(self, padid);
        if (isNotBoundPad(padid))
        {
            pad = &g_CurrentSetup.pads[padid];
        }
        else
        {
            pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padid)];
        }

        stan = pad->stan;
        if (stan)
        {
            //if pad is not vertical
            if (pad->up.y < 0.5f)
            {
                stan2    = stan;
                region.x = (pad->up.x * (self->chrwidth * 1.1f)) + pad->pos.x;
                region.y = (pad->up.y * (self->chrwidth * 1.1f)) + pad->pos.y;
                region.z = (pad->up.z * (self->chrwidth * 1.1f)) + pad->pos.z;

                //if able to reach region surrounding pad?
                if (walkTilesBetweenPoints_NoCallback(&stan2, pad->pos.x, pad->pos.z, region.x, region.z) &&
                    plot_course_for_actor(self, &region, stan2, speed))
                {
                    return TRUE;
                }
                #ifdef DEBUG
                else
                {
                    osSyncPrintf("could not go to pad %d!!!\n", padid);
                    }
                #endif
            }
            else if (plot_course_for_actor(self, &pad->pos, stan, speed))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}



/**
 * Address 0x7F02AD54.
*/
bool if_actor_able_set_on_path(ChrRecord *self, void *pathid)
{
    if (pathid && chrIsNotDeadOrShot(self))
    {
        set_actor_on_path(self, pathid);
        return TRUE;
    }

    return FALSE;
}



/**
 * Address 0x7F02AD98.
 * PD: chrTickStand
*/
void chrlvTickStand(ChrRecord *self)
{
    s32 i;             // any
    f32 aaa;
    s32 bbb;
    PropRecord *left;  // 160
    PropRecord *right; // 156
    s32 index;         // any
    s32 ccc;
    f32 sp74[8];       // 116
    f32 subroty;       // 112
    f32 temp_f0;       // 108
    f32 subrotyarg2;   // any
    s32 j;             // any
    s32 sp44[8];       // 68
    s32 z; // required to push $f0 below

    if (self->sleep > 0)
    {
        return;
    }

    if (self->act_stand.prestand != 0)
    {
        // needs to save $f0 into sp(0x3c)
        if (modelGetAnimFrame(self->model) >= modelGetAnimEndFrame(self->model))
        {
            chrlvIdleAnimationRelated(self, 8.0f);
            self->act_stand.prestand = 0;
        }

        self->sleep = 0;

        return;
    }

    if (self->act_stand.face_entitytype > 0)
    {
        if (self->act_stand.reaim)
        {
            subrotyarg2 = objecthandlerGetModelAnim(self->model)->unk04 - 1;
            self->act_stand.turning = chrlvSetSubroty(self, self->act_stand.turning, subrotyarg2, 1.0f, 0.0f);

            if (self->act_stand.turning != 1)
            {
                chrlvIdleAnimationRelated(self, 8.0f);
                self->act_stand.reaim = 0;

                if (self->act_stand.face_entitytype & 0x10)
                {
                    self->act_stand.face_entitytype = 0;
                }
            }
        }
        else
        {
            temp_f0 = chrlvDistanceToChrRelated(self, self->act_stand.face_entitytype, self->act_stand.face_entityid);
            if ((temp_f0 > 0.34906587f) && (temp_f0 < 5.9341197f))
            {
                left = chrGetEquippedWeaponProp(self, 1);
                right = chrGetEquippedWeaponProp(self, 0);

                self->act_stand.reaim = 1;
                self->act_stand.turning = 1;

                if (((left != NULL) && (right != NULL))
                    || ((left == NULL) && (right == NULL))
                    || weaponIsOneHanded(left)
                    || weaponIsOneHanded(right))
                {
                    // required to fix stack above
                    // looks like it doesn't matter which `s32` is used.
                    i = (s32)((u32)randomGetNext() & 1U);
                    modelSetAnimation(
                        self->model,
                        // awkward fix: addu instruction is backwards
                        portAnimationFromOffset(PTR_ANIM_walking_unarmed),
                        i,
                        0.0f,
                        0.5f,
                        16.0f);

                    modelSetAnimEndFrame(
                        self->model,
                        (portAnimationFromOffset(PTR_ANIM_walking_unarmed)->unk04 - 1));
                }
                else if ((right != NULL) || (left != NULL))
                {
                    modelSetAnimation(
                        self->model,
                        // awkward fix: addu instruction is backwards
                        portAnimationFromOffset(PTR_ANIM_walking),
                        left != NULL,
                        0.0f,
                        0.5f,
                        16.0f);

                    modelSetAnimEndFrame(
                        self->model,
                        (portAnimationFromOffset(PTR_ANIM_walking)->unk04 - 1));
                }
            }
            else if (self->act_stand.face_entitytype & 0x10)
            {
                self->act_stand.face_entitytype = 0;
            }
        }

        self->sleep = 0;

        return;
    }

    self->sleep = ((u32)randomGetNext() % 5U) + 0xE;

    if (self->act_stand.checkfacingwall)
    {
        if (self->chrflags & CHRFLAG_00000080)
        {
            self->act_stand.checkfacingwall = 0;
            return;
        }

        self->act_stand.wallcount -= self->sleep;
        if (self->act_stand.wallcount < 0)
        {
            subroty = getsubroty(self->model);

            temp_f0 = subroty;
            for (i = 0; i < 8; i++)
            {
                temp_f0 += DegToRad(45);

                if (temp_f0 >= M_TAU_F)
                {
                    temp_f0 -= M_TAU_F;
                }

                sp74[i] = chrlvPathingCollisionRelated(self->prop, temp_f0, 1000.0f, 0, 0.0f, 1.0f);
            }

            for (i = 0; i < 8; i++)
            {
                sp44[i] = i;
            }

            /**
             * Selection sort.
            */
            for (i=0; i<7; i++)
            {
                index = i;

                for (j = i + 1; j < 8; j++)
                {
                    if (sp74[sp44[j]] < sp74[sp44[index]])
                    {
                        index = j;
                    }
                }

                j = sp44[i];
                sp44[i] = sp44[index];
                sp44[index] = j;
            }

            index = -1;
            if (sp74[0] < 490.0f)
            {
                if (sp74[sp44[4]] < 200.0f)
                {
                    index = 7;
                }
                else if ((sp44[0] == 0) || (sp44[1] == 0) || (sp44[2] == 0))
                {
                    if (((sp44[3] == 4) || (sp44[4] == 4)) && ((randomGetNext() % 3U) == 0))
                    {
                        if (sp44[3] == 4)
                        {
                            index = 3;
                        }
                        else
                        {
                            index = 4;
                        }
                    }
                    else
                    {
                        index = 5 + (randomGetNext() % 3U);
                    }
                }
                else if (((sp44[0] == 1) || (sp44[0] == 7)) && (sp44[5] != 0) && (sp44[6] != 0) && (sp44[7] != 0))
                {
                    index = 5 + (randomGetNext() % 3U);
                }
            }

            if (index >= 0)
            {
                i = sp44[index];
                temp_f0 = ((f32)i * M_TAU_F * 0.125f) + subroty;

                if (temp_f0 >= M_TAU_F)
                {
                    temp_f0 -= M_TAU_F;
                }

                check_set_actor_standing_still(self, 0x10, (s32) ((temp_f0 * M_U16_MAX_VALUE_F) / M_TAU_F));
            }
            else
            {
                self->act_stand.checkfacingwall = 0;
            }
        }
    }
}



void chrlvTickKneel(ChrRecord *actor) {
    actor->sleep = 0;
}

// #still chraction.c


/**
 * Address 0x7F02B4E8.
*/
void chrlvTickAnim(ChrRecord *self)
{
    s32 unused[1];

    if (self->act_init.padding[1] == 0)
    {
        f32 sp20 = modelGetAnimFrame(self->model);

        if (modelGetAnimEndFrame(self->model) <= sp20)
        {
            chrlvKneelingAnimationRelated(self);
        }
    }

    if (
        (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_sneeze))
        && (modelGetAnimFrame(self->model) >= 42.0f)
        && !(self->chrflags & CHRFLAG_02000000)
       )
    {
        if (((g_GlobalTickCount & 1) == 0) && (chrGetDistanceToBond(self) < 800.0f))
        {
            chrobjSndCreatePostEventDefault(sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, SNEEZE_SFX, 0), &self->prop->pos);
        }

        self->chrflags |= CHRFLAG_02000000;
    }

    if (((s32) self->sleep <= 0) && (self->act_init.padding[3] != 0))
    {
        self->sleep = (randomGetNext() % 5U) + 0xE;
    }
}



/**
 * Address 0x7F02B638.
*/
void chrlvTickSurrender(ChrRecord *self)
{
    Model *model;

    if ((s32) self->sleep <= 0)
    {
        model = self->model;
        self->sleep = 0x10;

        if ((objecthandlerGetModelAnim(model) == portAnimationFromOffset(PTR_ANIM_surrendering_armed_drop_weapon))
            && (modelGetAnimFrame(model) >= 80.0f))
        {
            coord3d sp30 = D_80030A44;

            f32 t = getsubroty(model);

            sp30.f[0] = -sinf(t);
            sp30.f[2] = -cosf(t);

            if (chrlvCall7F02982C(self->prop, &sp30, 20.0f) == 0)
            {
                modelSetAnimation(self->model, portAnimationFromOffset(PTR_ANIM_surrendering_armed), randomGetNext() & 1, 30.0f, 0.5f, 16.0f);
                modelSetAnimLooping(self->model, 30.0f, 16.0f);
            }
        }
    }
}



/**
 * Address 0x7F02B774.
*/
void chrlvTickDead(ChrRecord *self)
{
    if (self->act_init.padding[0] >= 0)
    {
        self->act_init.padding[0] += g_ClockTimer;

        if (self->act_init.padding[0] >= CHRLV_TICK_DEAD_CHECK)
        {
            self->hidden |= CHRHIDDEN_REMOVE;
        }
        else
        {
            self->fadealpha = (u8) ((s32) ((CHRLV_TICK_DEAD_CHECK - self->act_init.padding[0]) * 0xFF) / CHRLV_TICK_DEAD_CHECK);
        }

        return;
    }

    self->act_init.padding[0] = 0;
}




/**
 * @param self:
 * @param flag: shot/die flag. 0 == shot, else die.
 *
 * Address 0x7F02B800.
*/
void chrlvIterateGuardSeeShotDie(ChrRecord *self, s32 flag)
{
    ChrRecord *guard;
    PropRecord *self_prop;
    f32 dx;
    f32 dz;
    f32 dy;
    s32 numguards;
    PropRecord *guard_prop;
    s32 i = 0;
    s32 alert_count = 0;

    numguards = get_numguards();

    /*
     * Maybe there's removed code in these if,elseif blocks?
    */
    if (self->actiontype == ACT_ARGH)
    {
        i = self->act_init.padding[0];
    }
    else if (self->actiontype == ACT_DIE)
    {
        i = self->act_init.padding[0];
    }

    for (; i < numguards && alert_count < 4; i++)
    {
        guard = &g_ChrSlots[i];

        if (guard->model != NULL)
        {
            guard_prop = guard->prop;
            self_prop = self->prop;
            dx = guard_prop->pos.f[0] - self_prop->pos.f[0];
            dy = guard_prop->pos.f[1] - self_prop->pos.f[1];
            dz = guard_prop->pos.f[2] - self_prop->pos.f[2];

            if (((dx * dx) + (dy * dy) + (dz * dz)) < 4000000.0f)
            {
                alert_count++;

                if (chrlvMaybeSameRoom(guard, &self_prop->pos, self_prop->stan))
                {
                    if (flag == 0)
                    {
                        guard->chrseeshot = self->chrnum;
                    }
                    else
                    {
                        guard->chrseedie = self->chrnum;
                    }
                }
            }
        }
    }

    if (self->actiontype == ACT_ARGH)
    {
        self->act_init.padding[0] = i;
    }
    else if (self->actiontype == ACT_DIE)
    {
        self->act_init.padding[0] = i;
    }
}




/**
 * Address 0x7F02B9A4.
 * PD: void chrTickDie(struct chrdata *chr).
*/
void chrlvTickDie(ChrRecord *self)
{
    Model *model = self->model;

    ALSoundState * p;

    s16 body_hit_SFX[] = {0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85};

    static s32 thud_index = 0;

    if ((self->act_die.thudframe1 >= 0.0f) && (self->act_die.thudframe1 <= modelGetAnimFrame(model)))
    {
        p = sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, body_hit_SFX[thud_index], NULL);

        chrobjSndCreatePostEventDefault(p, &self->prop->pos);

        thud_index++;
        if (thud_index >= 0xB)
        {
            thud_index = 0;
        }

        self->act_die.thudframe1 = -1.0f;
    }

    if ((self->act_die.thudframe2 >= 0.0f) && (self->act_die.thudframe2 <= modelGetAnimFrame(model)))
    {
        p = sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, body_hit_SFX[thud_index], NULL);

        chrobjSndCreatePostEventDefault(p, &self->prop->pos);

        thud_index++;
        if (thud_index >= 0xB)
        {
            thud_index = 0;
        }

        self->act_die.thudframe2 = -1.0f;
    }

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        if (objecthandlerGetModelAnim(model) == portAnimationFromOffset(PTR_ANIM_death_left_leg))
        {
            modelSetAnimation(
                model,
                portAnimationFromOffset(PTR_ANIM_jump_backwards),
                objecthandlerGetModelGunhand(model) == 0,
                50.0f,
                0.3f,
                (portAnimationFromOffset(PTR_ANIM_jump_backwards)->unk04 - 1.0f) - 50.0f);

            modelSetAnimSpeed(model, 0.5f, (portAnimationFromOffset(PTR_ANIM_jump_backwards)->unk04 - 1.0f) - 50.0f);

            return;
        }

        chrlvActorFadeAway(self);
    }

    chrlvIterateGuardSeeShotDie(self, 1);
}




/**
 * Address 0x7F02BC80.
*/
void chrlvTickArgh(ChrRecord *self)
{
    Model *model = self->model;

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        chrlvSetTargetToPlayer(self);

        if (objecthandlerGetModelAnim(model) == portAnimationFromOffset(PTR_ANIM_death_left_leg))
        {
            chrlvIdleAnimationRelated7F023E14(self, 26.0f);
        }
        else
        {
            chrlvKneelingAnimationRelated7F023E48(self);
        }
    }

    chrlvIterateGuardSeeShotDie(self, 0);
}



/**
 * Address 0x7F02BD20.
*/
void chrlvTickPreArgh(ChrRecord *self)
{
    Model *model;
    coord3d sp30;

    model = self->model;

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        sp30.f[0] = self->act_preargh.pos.f[0];
        sp30.f[1] = self->act_preargh.pos.f[1];
        sp30.f[2] = self->act_preargh.pos.f[2];
        triggered_on_shot_hit(self, &sp30, self->act_preargh.unk038, self->act_preargh.unk03c, self->act_preargh.unk040);
    }
}




/**
 * @see chrlvTickJumpout
 * @see chrlvTickTest
 * @see chrlvTickStartAlarm
 *
 * Address 0x7F02BDA4.
*/
void chrlvTickSidestep(ChrRecord *self)
{
    Model *model = self->model;

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        chrlvSetTargetToPlayer(self);
        chrlvIdleAnimationRelated7F023E14(self, 10.0f);
    }
}


/**
 * @see chrlvTickSidestep
 * @see chrlvTickTest
 * @see chrlvTickStartAlarm
 *
 * Address 0x7F02BE00.
*/
void chrlvTickJumpout(ChrRecord *self)
{
    Model *model = self->model;

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        chrlvSetTargetToPlayer(self);
        chrlvKneelingAnimationRelated7F023E48(self);
    }
}




/**
 * @see chrlvTickSidestep
 * @see chrlvTickJumpout
 * @see chrlvTickStartAlarm
 *
 * Address 0x7F02BE58.
*/
void chrlvTickTest(ChrRecord *self)
{
    Model *model = self->model;

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        chrlvKneelingAnimationRelated(self);
    }
}



/**
 * @see chrlvTickSidestep
 * @see chrlvTickJumpout
 * @see chrlvTickTest
 *
 * Address 0x7F02BEA8.
*/
void chrlvTickStartAlarm(ChrRecord *self)
{
    Model *model = self->model;

    // bug/typo, should be 50.0f on VERSION_EU
    if (modelGetAnimFrame(model) >= 60.0f)
    {
        alarmActivate();
    }

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        chrlvKneelingAnimationRelated7F023E48(self);
    }
}



/**
 * Address 0x7F02BF24.
*/
void chrlvTickSurprised(ChrRecord *self)
{
    Model *model = self->model;

    if (modelGetAnimFrame(model) >= modelGetAnimEndFrame(model))
    {
        if (objecthandlerGetModelAnim(model) == portAnimationFromOffset(PTR_ANIM_surrendering_armed))
        {
            chrlvIdleAnimationRelated7F023E14(self, 26.0f);
        }
        else if (objecthandlerGetModelAnim(model) == portAnimationFromOffset(PTR_ANIM_spotting_bond))
        {
            chrlvIdleAnimationRelated7F023E14(self, 26.0f);
        }
        else
        {
            chrlvKneelingAnimationRelated7F023E48(self);
        }
    }
}



void sub_GAME_7F02BFE4(ChrRecord *self, s32 arg1, s32 arg2)
{
    PropRecord *prop;
    ChrRecord *temp_v1;
    s32 phi_a1;
    u8 sp33;
    s16 sp30;
    ALSoundState **phi_a2;

    prop = chrGetEquippedWeaponProp(self, arg1);
    temp_v1 = prop->chr;
    phi_a1 = 0;

    sp33 = bondwalkItemGetSoundTriggerRate((s32) temp_v1->act_attack.attack_item);
    sp30 = bondwalkItemGetSound((s32) temp_v1->act_attack.attack_item);

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): log the item id read from the weapon prop and the
       * sound it maps to; a wrong but valid item with Sound=0 fires silently. */
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        static int dump = -1;
        if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
        if (dump && arg2 != 0)
        {
            printf("gunsnd: chr=%d hand=%d item=%d snd=0x%x rate=%d\n",
                    self->chrnum, arg1, temp_v1->act_attack.attack_item, (u16) sp30, sp33);
        }
    }
#endif

    if (arg2 != 0)
    {
        if (sp33 > 0)
        {
            if (((self->hidden & 0x80) == 0) && (self->field_178[arg1] < g_GlobalTimer))
            {
                phi_a1 = 1;
            }
        }
        else
        {
            phi_a1 = 1;
        }
    }

    if (phi_a1 != 0)
    {
        if (self->field_160[arg1].ptr_SEbuffer1 != NULL)
        {
            if (sndGetPlayingState(self->field_160[arg1].ptr_SEbuffer1) != 0)
            {
                sndDeactivate(self->field_160[arg1].ptr_SEbuffer1);
            }
        }

        if (self->field_160[arg1].ptr_SEbuffer2 != NULL)
        {
            if (sndGetPlayingState(self->field_160[arg1].ptr_SEbuffer2) != 0)
            {
                sndDeactivate(self->field_160[arg1].ptr_SEbuffer2);
            }
        }

        if (((u16) sp30) != 0)
        {
            phi_a2 = NULL;

            if (self->field_160[arg1].ptr_SEbuffer1 == NULL)
            {
                phi_a2 = (ALSoundState **) (&self->field_160[arg1].ptr_SEbuffer1);
            }
            else if (self->field_160[arg1].ptr_SEbuffer2 == NULL)
            {
                phi_a2 = (ALSoundState **) (&self->field_160[arg1].ptr_SEbuffer2);
            }

            if (phi_a2 != NULL)
            {
                sndPlaySfx(g_musicSfxBufferPtr, sp30, (ALSoundState *) phi_a2);
                chrobjSndCreatePostEventDefault(*phi_a2, &self->prop->pos);
                self->field_178[arg1] = ((0, g_GlobalTimer)) + ((s32) sp33);
                self->hidden |= 0x80;
            }
        }
    }
}


/**
 * Address 0x7F02C190.
*/
f32 chrlvGetSubrotySideback(ChrRecord *self)
{
    Model *model;
    f32 phi_f12;
    f32 ret;

    model = self->model;
    ret = getsubroty(model) + self->aimsideback;
    phi_f12 = 0.0f;

    if (ret >= M_TAU_F)
    {
        ret = ret - M_TAU_F;
    }
    else if (ret < 0.0f)
    {
        ret = ret + M_TAU_F;
    }

    if ((self->actiontype == ACT_ATTACK) || (self->actiontype == ACT_ATTACKROLL))
    {
        phi_f12 = self->act_attack.animfloats->angle_offset;
    }
    else if (self->actiontype == ACT_BONDMULTI)
    {
        if (self->act_bondmulti.unk2c != NULL)
        {
            phi_f12 = self->act_bondmulti.unk2c[3];
        }
    }

    if (phi_f12 != 0.0f)
    {
        if (self->model->gunhand != GUNRIGHT)
        {
            phi_f12 = M_TAU_F - phi_f12;
        }

        ret = ret + phi_f12;

        if (ret >= M_TAU_F)
        {
            ret = ret - M_TAU_F;
        }
    }

    return ret;
}




/**
 * Address 0x7F02C27C.
*/
f32 sub_GAME_7F02C27C(ChrRecord *self)
{
    f32 temp_f2;

    temp_f2 = self->aimuprshoulder + self->aimupback;
    if (temp_f2 < 0.0f)
    {
        temp_f2 = temp_f2 + M_TAU_F;
    }

    return temp_f2;
}



/**
 * Address 0x7F02C2B0.
*/
s32 chrlvSetSubroty(ChrRecord *self, s32 arg1, f32 arg2, f32 arg3, f32 arg4)
{
    Model *model;
    f32 sp28; //sp40
    f32 dist;
    f32 roty;
    s32 unused[1];
    f32 temp_f14;

    if (arg1 != 2)
    {
        model = self->model;
        sp28 = modelGetAnimFrame(model);
        roty = getsubroty(model);

#if defined(BUGFIX_R1)
        temp_f14 = 0.06283186f * arg3 * g_JP_GlobalTimerDelta * model->playspeed;
#else /* VERSION_US */
        temp_f14 = 0.06283186f * arg3 * CHRLV_FIELD_DELTA * model->playspeed;   /* GE_TICKHZ: field-paced (attack turn) */
#endif

        if (self->actiontype == ACT_ATTACK)
        {
            dist = chrlvDistanceToChrRelated(self, self->act_attack.attacktype, self->act_attack.entityid);
        }
        else if (self->actiontype == ACT_STAND)
        {
            dist = chrlvDistanceToChrRelated(self, self->act_stand.face_entitytype, self->act_stand.face_entityid);
        }
        else
        {
            PropRecord* p;
            p = getCurrentPlayerProp();
            dist = get_distance_actor_to_position(self, &p->pos);
        }

        dist = dist - arg4;

        if (dist < 0.0f)
        {
            dist = dist + M_TAU_F;
        }

        if ((dist < temp_f14) || ((M_TAU_F - temp_f14) < dist))
        {
            roty += dist;
            if (roty >= M_TAU_F)
            {
                roty -= M_TAU_F;
            }

            setsubroty(model, roty);
            arg1 = 3;
        }
        else if (dist < M_PI_F)
        {
            roty += temp_f14;
            if (roty >= M_TAU_F)
            {
                roty -= M_TAU_F;
            }

            setsubroty(model, roty);
        }
        else
        {
            roty -= temp_f14;

            if (roty < 0.0f)
            {
                roty += M_TAU_F;
            }

            setsubroty(model, roty);
        }

        if (arg2 <= sp28)
        {
            arg1 = 2;
        }
    }

    return arg1;
}




/**
 * @param self:
 * @param arg1:
 * @param arg2:
 * @param arg3:
 * @param arg4:
 *
 * Address 0x7F02C4C0.
*/
s32 chrlvUpdateAimendsideback(ChrRecord *self, struct weapon_firing_animation_table *arg1, s32 arg2, s32 arg3, f32 arg4)
{
    f32 sp164; // sp356
    f32 calc_aimendsideback; // sp352
    u32 attack_type; // sp348
    s32 entity_id; // sp344
    s32 ret; // sp340
    f32 dx; // sp336
    f32 dy; // sp332
    f32 dz; // sp328
    f32 dxdydz_square; // sp324
    Model *self_model;
    PropRecord *self_prop; // sp316
    s32 seen_bond_flag; // sp312
    coord3d *current_player_pos; //sp308
    f32 ducking_height; // sp304
    StandTile *pstan; // sp300
    coord3d sp120; // sp288
    PropRecord *player_prop;
    f32 subroty; // sp280

    /////////////////////

    ret = 1;
    sp164 = 0.0f;
    attack_type = TARGET_BOND;
    entity_id = 0;
    calc_aimendsideback = 0.0f;

    if (self->actiontype == ACT_ATTACK)
    {
        attack_type = self->act_attack.attacktype;
        entity_id = self->act_attack.entityid;
    }
    else if (self->actiontype == ACT_STAND)
    {
        attack_type = self->act_stand.face_entitytype;
        entity_id = self->act_stand.face_entityid;
    }

    if ((attack_type & TARGET_FRONT_OF_CHR) == 0)
    {
        player_prop = getCurrentPlayerProp();
        self_prop = self->prop;
        current_player_pos = &player_prop->pos;

        dx = player_prop->pos.f[0] - self_prop->pos.f[0];
        dy = player_prop->pos.f[1] - self_prop->pos.f[1];
        dz = player_prop->pos.f[2] - self_prop->pos.f[2];

        dxdydz_square = (dx * dx) + (dy * dy) + (dz * dz);

        if (attack_type & TARGET_BOND)
        {
            if ((attack_type & TARGET_DONTTURN) != 0)
            {
                seen_bond_flag = 1;
            }
            else
            {
                seen_bond_flag = chrCanSeeBond(self);
            }
        }
        else
        {
            seen_bond_flag = 1;
        }

        if (attack_type & TARGET_BOND > 0)
        {
            ducking_height = bondviewGetPlayerDuckingHeightRelated(g_CurrentPlayer);
            if ((self->chrflags & CHRSTART_FORCENOBLOOD) != 0)
            {
                if (((dx * dx) + (dy * dy) + (dz * dz)) < 160000.0f)
                {
                    if (self_prop->pos.f[1] < (current_player_pos->f[1] - (2.0f * ducking_height)))
                    {
                        dy -= ducking_height * (0.55f + (0.1f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX)) * arg4));
                    }
                    else if ((current_player_pos->f[1] - (ducking_height * 0.5f)) < self_prop->pos.f[1])
                    {
                        dy -= ducking_height * (0.15f + (0.1f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX)) * arg4));
                    }
                    else
                    {
                        dy = (((f32) (u32)randomGetNext() * (1.0f / UINT_MAX) * 0.1f * arg4) + 1.0f) * 40.0f;
                    }
                }
                else
                {
                    dy += ducking_height * (0.025f - (0.05f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX)) * arg4));
                }
            }
            else if (((dx * dx) + (dy * dy) + (dz * dz)) > 1000000.0f)
            {
                if (((u32)randomGetNext() % 3U) == 0)
                {
                    dy += ducking_height * (0.05f + (0.1f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX)) * arg4));
                }
                else
                {
                    dy -= ducking_height * (0.05f + (0.55f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX)) * arg4));
                }
            }
            else
            {
                if (self_prop->pos.f[1] < (current_player_pos->f[1] - ducking_height))
                {
                    dy -= ducking_height * (0.55f + (0.1f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX)) * arg4));
                }
                else if ((current_player_pos->f[1] - (ducking_height * 0.5f)) < self_prop->pos.f[1])
                {
                    dy -= ducking_height * (0.15f + (0.1f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX)) * arg4));
                }
                else
                {
                    dy = (((f32) (u32)randomGetNext() * (1.0f / UINT_MAX) * 0.1f * arg4) - 0.05f) * ducking_height;
                }
            }
        }
        else
        {
            getsuboffset(self->model, &sp120);
            current_player_pos = chrlvGetChrOrPresetLocation(self, attack_type, entity_id, &pstan);
            dx = current_player_pos->f[0] - sp120.f[0];
            dy = current_player_pos->f[1] - sp120.f[1];
            dz = current_player_pos->f[2] - sp120.f[2];
        }

        if ((attack_type & 0x100) == 0)
        {
            f32 sr = sqrtf((dx * dx) + (dz * dz));
            sp164 = atan2f(dy, sr);

            if (sp164 >= M_PI_F)
            {
                sp164 = sp164 - M_TAU_F;
            }
        }

        if (seen_bond_flag)
        {
            Model *weapon_prop_model; // sp272
            coord3d sp104; // sp260
            PropRecord *weapon_prop;
            struct modeldata_root *temp_v0_4;
            Mtxf spBC; // sp188
            f32 *spB8;  // sp184
            coord3d spAC; //sp172
            s32 intersect_flag; // sp140
            Mtxf sp68;
            coord3d sp5C; // sp92
            coord3d sp50; // sp80
            coord3d sp44; // sp68
            Mtxf *temp_a0;
            struct ObjectRecord *obj;
            f32 t1;

            ////////////////////////////////////////////

            subroty = chrlvGetSubrotySideback(self);

            if (arg3)
            {
                weapon_prop = chrGetEquippedWeaponProp(self, GUNRIGHT);
            }
            else
            {
                weapon_prop = chrGetEquippedWeaponProp(self, GUNLEFT);
            }

            // This if block is a slight modification of @see sub_GAME_7F02D630.
            if ((weapon_prop != NULL) && (weapon_prop->flags & 2) && (dxdydz_square < 1000000.0f))
            {
                obj = weapon_prop->obj;
                weapon_prop_model = obj->model;
                intersect_flag = 0;

                if (weapon_prop_model->obj->Switches[0])
                {
                    temp_a0 = modelFindNodeMtx(weapon_prop_model, weapon_prop_model->obj->Switches[0], 0);
                    spB8 = weapon_prop_model->obj->Switches[0]->Data;
                    sub_GAME_7F058E78(temp_a0, &spBC);

                    matrix_4x4_multiply_homogeneous_in_place(currentPlayerGetMatrix10EC(), &spBC);

                    spAC.f[0] = spB8[0];
                    spAC.f[1] = spB8[1];
                    spAC.f[2] = spB8[2];

                    mtx4TransformVecInPlace(&spBC, &spAC);

                    sp104.f[0] = spAC.f[0];
                    sp104.f[1] = spAC.f[1];
                    sp104.f[2] = spAC.f[2];

                    intersect_flag = 1;
                }
                else if (weapon_prop_model->obj->Switches[1])
                {
                    temp_a0 = modelFindNodeMtx(weapon_prop_model, weapon_prop_model->obj->Switches[1], 0);
                    sub_GAME_7F058E78(temp_a0, &sp68);
                    matrix_4x4_multiply_homogeneous_in_place(currentPlayerGetMatrix10EC(), &sp68);
                    sp104.f[0] = sp68.m[3][0];
                    sp104.f[1] = sp68.m[3][1];
                    sp104.f[2] = sp68.m[3][2];

                    intersect_flag = 1;
                }

                if (intersect_flag != 0)
                {
                    sp50.f[0] = sinf(subroty);
                    sp50.f[1] = 0.0f;
                    sp50.f[2] = cosf(subroty);
                    sp44.f[0] = self_prop->pos.f[0] - dz;
                    sp44.f[1] = self_prop->pos.f[1];
                    sp44.f[2] = self_prop->pos.f[2] + dx;
                    chrlvLineLineIntersection(&self_prop->pos, &sp44, &sp104, &sp50, &sp5C);
                    dx = current_player_pos->f[0] - sp5C.f[0];
                    dz = current_player_pos->f[2] - sp5C.f[2];
                }
            }

            t1 = atan2f(dx, dz);

            calc_aimendsideback = t1 - subroty;
            if (t1 < subroty)
            {
                calc_aimendsideback = t1 - subroty + M_TAU_F;
            }

            temp_v0_4 = (struct modeldata_root*)modelGetNodeRwData(self->model, self->model->obj->RootNode);

            if (temp_v0_4->unk5c > 0.0f)
            {
                calc_aimendsideback = calc_aimendsideback - (temp_v0_4->unk5c * temp_v0_4->unk58);

                if (calc_aimendsideback < 0.0f)
                {
                    calc_aimendsideback = calc_aimendsideback + M_TAU_F;
                }

                if (calc_aimendsideback >= M_TAU_F)
                {
                    calc_aimendsideback = calc_aimendsideback - M_TAU_F;
                }
            }

            if ((attack_type & 1) && ((attack_type & 0x60) == 0))
            {
                t1 = (((f32) ((s32) ((s32) ((f32) g_GlobalTimer * self->model->playspeed) + self->chrnum) % 60) * M_TAU_F) / 60.0f);
                t1 = sinf(t1) * (chrlvGetAimLimitAngle(dxdydz_square) * 0.5f);
                calc_aimendsideback += t1;

                if (calc_aimendsideback < 0.0f)
                {
                    calc_aimendsideback = calc_aimendsideback + M_TAU_F;
                }

                if (calc_aimendsideback >= M_TAU_F)
                {
                    calc_aimendsideback = calc_aimendsideback - M_TAU_F;
                }
            }

            if (calc_aimendsideback >= M_PI_F)
            {
                calc_aimendsideback = calc_aimendsideback - M_TAU_F;
            }

            calc_aimendsideback += self->aimsideback;

            if (self->model->gunhand != GUNRIGHT)
            {
                if (calc_aimendsideback < -arg1->max_left)
                {
                    calc_aimendsideback = -arg1->max_left;
                    ret = 0;
                }
                else if (-arg1->max_right < calc_aimendsideback)
                {
                    calc_aimendsideback = -arg1->max_right;
                    ret = 0;
                }
            }
            else
            {
                if (arg1->max_left < calc_aimendsideback)
                {
                    calc_aimendsideback = arg1->max_left;
                    ret = 0;
                }
                else if (calc_aimendsideback < arg1->max_right)
                {
                    calc_aimendsideback = arg1->max_right;
                    ret = 0;
                }
            }
        }
    }

    chrlvUpdateAimendbackShoulders(self, arg1, arg2, arg3, sp164);
    self->aimendsideback = calc_aimendsideback;
    self->aimendcount = 0xA;

    return ret;
}




/**
 * Calculates and sets chr aimendrshoulder, aimendlshoulder, and aimendback.
 * rshoulder defaults to 0.0f, lshoulder defaults to @param next.
 *
 * @param self:
 * @param arg1: Firing pose limits and free-arm aim fractions.
 * @param same: When set, both shoulders will receive lshoulder value. Only
 *     applies with @param swap is set.
 * @param swap: When set, aimendrshoulder will get the calculated lshoulder value,
 *     and aimendlshoulder will get the rshoulder value. If both @param swap and
 *     @param same is set they will both be set to lshoulder value.
 * @param next: Starting aimendlshoulder value.
 *
 * Address 0x7F02D048.
*/
void chrlvUpdateAimendbackShoulders(ChrRecord *self, struct weapon_firing_animation_table *arg1, s32 same, s32 swap, f32 next)
{
    f32 next_lshoulder;
    f32 next_rshoulder;
    f32 next_aimendback;

    next_rshoulder = 0.0f;
    next_aimendback = 0.0f;
    next_lshoulder = next;

    if (arg1 != NULL)
    {
        if (arg1->max_up < next)
        {
            next_aimendback = next - arg1->max_up;
            next_lshoulder = arg1->max_up;
        }

        else if (next < arg1->max_down)
        {
            next_aimendback = next - arg1->max_down;
            next_lshoulder = arg1->max_down;
        }

        if (next_lshoulder > 0.0f)
        {
            next_rshoulder = arg1->free_arm_frac_up * next_lshoulder;
        }
        else
        {
            next_rshoulder = arg1->free_arm_frac_down * next_lshoulder;
        }
    }

    if (swap != 0)
    {
        self->aimendrshoulder = next_lshoulder;

        if (same != 0)
        {
            self->aimendlshoulder = next_lshoulder;
        }
        else
        {
            self->aimendlshoulder = next_rshoulder;
        }
    }
    else
    {
        self->aimendrshoulder = next_rshoulder;
        self->aimendlshoulder = next_lshoulder;
    }

    self->aimendback = next_aimendback;
}





/**
 * Address 0x7F02D0F8.
*/
void chrlvResetAimend(ChrRecord *self)
{
    self->aimendcount = 0xA;
    self->aimendrshoulder = 0.0f;
    self->aimendlshoulder = 0.0f;
    self->aimendback = 0.0f;
    self->aimendsideback = 0.0f;
}



/**
 * Address 0x7F02D118.
 * PD: chrSetFiring
*/
void chrSetFiring(ChrRecord *self, s32 hand, s32 firing)
{
    PropRecord *prop;

    prop = chrGetEquippedWeaponProp(self, hand);

    if (prop != NULL)
    {
        weaponSetGunfireVisible(prop, firing);
    }
}



/**
 * Unreferenced.
 *
 * Address 0x7F02D148.
*/
s32 sub_GAME_7F02D148(ChrRecord *self, s32 hand)
{
    PropRecord *prop;

    prop = chrGetEquippedWeaponProp(self, hand);

    if (prop != NULL)
    {
        return weaponIsGunfireVisible(prop);
    }

    return 0;
}


/**
 * Address 0x7F02D184.
 * PD: chrStopFiring
*/
void chrStopFiring(ChrRecord *self)
{
    chrSetFiring(self, GUNRIGHT, FALSE);
    chrSetFiring(self, GUNLEFT, FALSE);
    chrlvResetAimend(self);
}


/**
 * Address 0x7F02D1C4.
*/
void chrlvToggleHiddenRelated(ChrRecord *self, s32 hand, s32 arg2)
{
    if (arg2 != 0)
    {
        if (hand == GUNLEFT)
        {
            self->hidden |= CHRHIDDEN_FIRE_WEAPON_LEFT;
        }
        else
        {
            self->hidden |= CHRHIDDEN_FIRE_WEAPON_RIGHT;
        }
    }
    else if (hand == GUNLEFT)
    {
        self->hidden &= ~CHRHIDDEN_FIRE_WEAPON_LEFT; // CHRHIDDEN_FIRE_WEAPON_LEFT
    }
    else
    {
        self->hidden &= ~CHRHIDDEN_FIRE_WEAPON_RIGHT; // CHRHIDDEN_FIRE_WEAPON_RIGHT
    }

    if (arg2 == 0)
    {
        chrSetFiring(self, hand, FALSE);
    }
}




/**
 * Address 0x7F02D244.
*/
f32 chrlvGetAimLimitAngle(f32 sqdist)
{
    if (sqdist > (1600.0f * 1600.0f))
    {
        return (M_PI_F / 167.5f);
    }

    if (sqdist > (800.0f * 800.0f))
    {
        return (M_PI_F / 83.5f);
    }

    if (sqdist > (400.0f * 400.0f))
    {
        return (M_PI_F / 42.0f);
    }

    if (sqdist > (200.0f * 200.0f))
    {
        return (M_PI_F / 21.0f);
    }

    return (M_PI_F / 12.5f);
}



/**
 * @param arg0:
 * @param arg1: out parameter. bool. Whether or not self has correct line of site to hit player.
 * @param arg2: out parameter. bool. True if damage done, false otherwise.
 * @param item: weapon doing damage
 *
 * Address 0x7F02D2E4.
*/
void chrlvUpdateShotbondsum(ChrRecord *self, s32 *arg1, s32 *arg2, ITEM_IDS item)
{
    f32 limit_angle;
    f32 dxdydz_square;
    f32 dx; // sp84
    f32 dy; // sp80
    f32 dz; // sp76
    f32 atan; // sp72
    f32 subroty; // sp68
    f32 phi_f2; // sp64
    PropRecord *player_prop;
    f32 temp_f0_3;
    PropRecord *self_prop;
    f32 t2; // sp48
    f32 phi_f2_4; // sp44
    s32 padding; // unused
    s32 phi_v1;

    player_prop = getCurrentPlayerProp();
    self_prop = self->prop;

    dx = player_prop->pos.f[0] - self_prop->pos.f[0];
    dy = player_prop->pos.f[1] - self_prop->pos.f[1];
    dz = player_prop->pos.f[2] - self_prop->pos.f[2];

    atan = atan2f(dx, dz);
    subroty = chrlvGetSubrotySideback(self);
    phi_f2 = atan - subroty;
    dxdydz_square = (dx * dx) + (dy * dy) + (dz * dz);

    limit_angle = chrlvGetAimLimitAngle(dxdydz_square);

    if (phi_f2 < 0.0f)
    {
        phi_f2 += M_TAU_F;
    }

    phi_v1 = (phi_f2 < limit_angle);

    if ((phi_f2 < limit_angle) == 0)
    {
        phi_v1 = ((M_TAU_F - limit_angle) < phi_f2);
    }

    *arg1 = phi_v1;
    *arg2 = 0;

    if ((bondviewGetIfCurrentPlayerDamageShowTime() == 0) && (phi_v1 != 0))
    {
        temp_f0_3 = sqrtf(dxdydz_square);

#ifndef TARGET_N64
        /* PORT: this is added once per fire event, which happens once per field
         * at any tick rate, so scaling by the sub-field g_GlobalTimerDelta made
         * guards less accurate at higher frame rates. Scale by a whole-field
         * cadence instead (see the note below for why live play pins it). */
        {
            /* Live play uses a fixed console-like cadence (3.0, the N64's ~20
             * fps tick) because this multiplier effectively sets guard
             * accuracy, and a 60Hz value of 1.0 made guards about three times
             * less deadly than on N64. Demo replay keeps the recorded cadence
             * so playback stays in sync; damage per hit and accuracy modifiers
             * are unchanged. */
            extern s32 is_ramrom_flag;
            extern s32 g_ramromPlayBackFlag;
            f32 cad = (is_ramrom_flag != 0 || g_ramromPlayBackFlag != 0)
                        ? (f32) portTickCadence
                        : GUARD_AIM_CADENCE;

            phi_f2_4 = 0.16f * cad;
        }
#elif defined(VERSION_JP)
        phi_f2_4 = 0.16f * g_JP_GlobalTimerDelta;
#else
        phi_f2_4 = 0.16f * g_GlobalTimerDelta;
#endif

        if (temp_f0_3 > 300.0f)
        {
            phi_f2_4 *= (300.0f / temp_f0_3);
        }

        if ((s32) self->accuracyrating > 0)
        {
            phi_f2_4 *= (1.0f + ((f32) self->accuracyrating / 10.0f));
        }
        else if ((s32) self->accuracyrating < 0)
        {
            if ((s32) self->accuracyrating < -0x63)
            {
                phi_f2_4 = 0.0f;
            }
            else
            {
                phi_f2_4 *= ((f32) (self->accuracyrating + 0x64) / 100.0f);
            }
        }

        if (get_007_accuracy_mod() <= 1.0f)
        {
            phi_f2_4 *= get_007_accuracy_mod();
        }
        else
        {
            phi_f2_4 *= (9.0f / (10.001f - get_007_accuracy_mod()));
        }

        phi_f2_4 *= g_AiAccuracyModifier;

        if (bondwalkItemGetAutomaticFiringRate(item) <= 0)
        {
            phi_f2_4 *= 2.0f;
        }

        if ((item == ITEM_SHOTGUN) || (item == ITEM_AUTOSHOT))
        {
            phi_f2_4 *= 2.0f;
        }

        self->shotbondsum += phi_f2_4;

        if (self->shotbondsum >= 1.0f)
        {
            t2 = (0.125f * gunItemGetDestructionAmount(item) * g_AiDamageModifier) * get_007_damage_mod();

            if ((item == ITEM_SHOTGUN) || (item == ITEM_AUTOSHOT))
            {
                t2 *= 3.0f;
            }

            bondviewCallRecordDamageKills(t2, subroty, -1, 1);

            self->shotbondsum = 0.0f;

            if (bondviewGetIfCurrentPlayerDamageShowTime() != 0)
            {
                *arg2 = 1;
            }
        }
    }
}


/**
 * Slight modification of a part of @see chrlvUpdateAimendsideback.
 *
 * Address 0x7F02D630.
*/
s32 sub_GAME_7F02D630(ChrRecord *self, GUNHAND hand, coord3d *arg2)
{
    struct ObjectRecord *obj;
    PropRecord *weapon_prop;
    Model *weapon_prop_model; // sp188
    s32 ret;
    Mtxf *temp_a0; // sp180
    Mtxf sp74;
    f32 *spB8;
    Mtxf *temp_a0_2; // sp108
    Mtxf sp68; // sp44

    weapon_prop = chrGetEquippedWeaponProp(self, hand);
    ret = 0;

    if ((weapon_prop != NULL) )
    {
        obj = weapon_prop->obj;
        weapon_prop_model = obj->model;

        if ((weapon_prop->flags & 2))
        {
            if (weapon_prop_model->obj->Switches[0])
            {
                temp_a0 = modelFindNodeMtx(weapon_prop_model, weapon_prop_model->obj->Switches[0], 0);
                spB8 = weapon_prop_model->obj->Switches[0]->Data;

                arg2->f[0] = spB8[0];
                arg2->f[1] = spB8[1];
                arg2->f[2] = spB8[2];

                matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), temp_a0, &sp74);
                mtx4TransformVecInPlace(&sp74, arg2);

                ret = 1;
            }
            else if (weapon_prop_model->obj->Switches[1])
            {
                temp_a0_2 = modelFindNodeMtx(weapon_prop_model, weapon_prop_model->obj->Switches[1], 0);
                matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), temp_a0_2, &sp68);

                arg2->f[0] = sp68.m[3][0];
                arg2->f[1] = sp68.m[3][1];
                arg2->f[2] = sp68.m[3][2];

                ret = 1;
            }
        }
    }

    return ret;
}



/**
 * Address 0x7F02D734.
*/
void chrlvFireWeaponRelated(ChrRecord *self, s32 hand)
{
    PropRecord *self_prop; // 644
    s32 phi_a2; // ?
    s32 sp27C; // stack 636
    s32 sp278;
    ChrRecord *prop_selfchr; // 628
    PropRecord *player_prop; // 624
    s32 phi_v1; // ?
    s32 sp268; // 616
    s32 sp264; // 612
    coord3d sp258; // 600
    StandTile *sp254; // 596
    f32 subroty; // 592
    f32 sp24C; // 588
    coord3d sp240; // 576
    StandTile *self_stan; // 572
    StandTile *sp238; // 568
    s32 sp234; // 564
    s32 sp230; // 560
    s32 sp22C; // 556
    coord3d sp220;
    s32 sp21C;
    f32 dy;
    f32 dz;
    f32 dx;
    f32 sp20C; // 524
    struct WeaponObjRecord *sp208;
    Mtxf sp1C8;
    coord3d sp1BC;  // 444
    PropRecord *weapon_prop;
    coord3d sp1AC; // 428
    Mtxf sp16C;
    Mtxf sp12C;
    struct WeaponObjRecord *sp128; // 296
    Mtxf spE8;
    coord3d spDC; // 220
    Mtxf sp9C;
    Mtxf sp5C; // 92
    s32 sp44;
    s32 unused;
    f32 sp4C;

    self_prop = self->prop;
    weapon_prop = chrGetEquippedWeaponProp(self, hand);

    if (weapon_prop != NULL)
    {
        sp27C = 0;
        sp278 = 0;
        prop_selfchr = weapon_prop->chr;
        player_prop = getCurrentPlayerProp();
        phi_v1 = 1;

        if (self->actiontype == ACT_ATTACK)
        {
            phi_v1 = self->act_attack.attacktype;
        }

        sp44 = phi_v1 & 1;

        if (
            (sp44 == 0)
            || (self->seen_bond_time >= (g_GlobalTimer - CHRLV_SEEN_RECENT_CHECK))
            || (bondwalkItemGetAutomaticFiringRate(prop_selfchr->act_attack.attack_item) < 0))
        {
            sp268 = 0;
            sp264 = 0;

            self->firecount[hand]++;

            if (bondwalkItemGetAutomaticFiringRate(prop_selfchr->act_attack.attack_item) < 0)
            {
                sp268 = 1;
                sp264 = 1;
            }
            else if (((s32) self->firecount[hand] % bondwalkItemGetAutomaticFiringRate(prop_selfchr->act_attack.attack_item)) == 0)
            {
                sp268 = 1;

                if ((((s32) self->firecount[hand] % (s32) (bondwalkItemGetAutomaticFiringRate(prop_selfchr->act_attack.attack_item) * 2)) == 0)
                    || (prop_selfchr->act_attack.attack_item == ITEM_LASER))
                {
                    sp264 = 1;
                }
            }
            else
            {
                sp278 = 1;
            }

            if (sp268 != 0)
            {
                sp254 = NULL;
                subroty = chrlvGetSubrotySideback(self);
                sp24C = sub_GAME_7F02C27C(self);
                self_stan = self_prop->stan;
                sp27C = 1;

                if (sub_GAME_7F02D630(self, hand, (coord3d *) &sp240) == 0)
                {
                    sp240.f[0] = self_prop->pos.f[0];
                    sp240.f[1] = self_prop->pos.f[1] + 30.0f;
                    sp240.f[2] = self_prop->pos.f[2];

                    if (hand == 1)
                    {
                        sp240.f[0] += cosf(subroty) * 10.0f;
                        sp240.f[2] += -sinf(subroty) * 10.0f;
                    }
                    else
                    {
                        sp240.f[0] += -cosf(subroty) * 10.0f;
                        sp240.f[2] += sinf(subroty) * 10.0f;
                    }
                }

                if (stanTestLineUnobstructed(&self_stan, self_prop->pos.x, self_prop->pos.f[2], sp240.f[0], sp240.f[2], CDTYPE_DOORS, sp240.f[1] - self->ground, sp240.f[1] - self->ground, 0.0f, 1.0f) != 0)
                {
                    sp238 = self_stan;
                }
                else
                {
                    self->firecount[hand]--;
                    sp27C = 0;
                }

                if (sp27C != 0)
                {
                    sp234 = 0;
                    sp230 = 0;
                    sp22C = 1;

                    sp21C = chrlvAttackRelated7F0292A8(self, &sp240, sp238);

                    sp220.f[0] = cosf(sp24C) * sinf(subroty);
                    sp220.f[1] = sinf(sp24C);
                    sp220.f[2] = cosf(sp24C) * cosf(subroty);

                    sp258.f[0] = sp240.f[0] + (sp220.f[0] * M_U16_MAX_VALUE_F);
                    sp258.f[1] = sp240.f[1] + (sp220.f[1] * M_U16_MAX_VALUE_F);
                    sp258.f[2] = sp240.f[2] + (sp220.f[2] * M_U16_MAX_VALUE_F);

                    chrSetMoving(self, 0);
                    stanResetHits();
                    self_stan = sp238;

                    if (stanTestLineUnobstructed(&self_stan, sp240.f[0], sp240.f[2], sp258.f[0], sp258.f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, sp240.f[1], sp240.f[1], sp258.f[1], sp258.f[1]) == 0)
                    {
                        chrlvStanLineDirIntersection(&sp240, &sp220, &sp258);
                        sp254 = self_stan;
                        sp258.f[0] -= 26.0f * sp220.f[0];
                        sp258.f[1] -= 26.0f * sp220.f[1];
                        sp258.f[2] -= 26.0f * sp220.f[2];
                    }

                    chrSetMoving(self, 1);

                    dx = sp258.f[0] - sp240.f[0];
                    dy = sp258.f[1] - sp240.f[1];
                    dz = sp258.f[2] - sp240.f[2];

                    sp20C = (dx * dx) + (dy * dy) + (dz * dz);

                    if (prop_selfchr->act_attack.attack_item == ITEM_ROCKETLAUNCH)
                    {
                        if (((dx * dx) + (dy * dy) + (dz * dz)) > 160000.0f)
                        {
                            sp208 = (struct WeaponObjRecord *)create_new_item_instance_of_model(PROP_CHRROCKET, 0x56);
                            if (sp208 != NULL)
                            {
                                matrix_4x4_set_identity(&sp1C8);
                                matrix_4x4_set_rotation_around_x(sp24C, &sp16C);
                                matrix_4x4_set_rotation_around_y(subroty, &sp12C);
                                matrix_4x4_multiply_homogeneous_in_place(&sp12C, &sp16C);

                                sp1AC.f[0] = sp220.f[0] * 1.111111f;
                                sp1AC.f[1] = sp220.f[1] * 1.111111f;
                                sp1AC.f[2] = sp220.f[2] * 1.111111f;

#ifndef TARGET_N64
                                { /* GE_TICKHZ: launch velocity is direction
                                   * times the firing tick's delta, which was ~3
                                   * vsyncs on N64, so use the same pinned
                                   * console cadence as guard aim in live play.
                                   * Replay keeps the recorded cadence for demo
                                   * sync; only the first few frames differ
                                   * since rockets accelerate afterwards. */
                                    extern s32 is_ramrom_flag;
                                    extern s32 g_ramromPlayBackFlag;
                                    f32 cad = (is_ramrom_flag != 0 || g_ramromPlayBackFlag != 0)
                                                ? (f32) portTickCadence
                                                : GUARD_AIM_CADENCE;
                                    sp1BC.f[0] = sp1AC.f[0] * cad;
                                    sp1BC.f[1] = sp1AC.f[1] * cad;
                                    sp1BC.f[2] = sp1AC.f[2] * cad;
                                }
#else
                                sp1BC.f[0] = sp1AC.f[0] * g_GlobalTimerDelta;
                                sp1BC.f[1] = sp1AC.f[1] * g_GlobalTimerDelta;
                                sp1BC.f[2] = sp1AC.f[2] * g_GlobalTimerDelta;
#endif

                                gunInitProjectileObject((ObjectRecord *)sp208, &sp240, sp238, &sp16C, &sp1BC, &sp1C8, self_prop);

                                if (sp208->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
                                {
                                    sp208->projectile->flags |= 0x80;
                                    sp208->timer = -1;
                                    sp208->projectile->flags |= 0x20;

                                    sp208->projectile->unkB0 = sp208->runtime_pos.y;
                                    sp208->projectile->unkB4 = sp208->projectile->speed.f[1];

                                  /*  sp208->projectile->unk10.x = sp1AC.f[0];
                                    sp208->projectile->unk10.y = sp1AC.f[1];
                                    sp208->projectile->unk10.z = sp1AC.f[2];*/
                                    sp208->projectile->unk10.x = sp1AC.f[0];
                                    sp208->projectile->unk10.y = sp1AC.f[1];
                                    sp208->projectile->unk10.z = sp1AC.f[2];

                                    if (sp208->projectile->sounds[0] == NULL)
                                    {
                                        sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, ROCKET_LAUNCH_SFX, (ALSoundState *)&sp208->projectile->sounds[0]);
                                    }
                                    else if (sp208->projectile->sounds[1] == NULL)
                                    {
                                        sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, ROCKET_LAUNCH_SFX, (ALSoundState *)&sp208->projectile->sounds[1]);
                                    }
                                }
                            }
                        }
                        else
                        {
                            sp27C = 0;
                        }
                    }
                    else if (prop_selfchr->act_attack.attack_item == ITEM_GRENADELAUNCH)
                    {
                        if (((dx * dx) + (dy * dy) + (dz * dz)) > 160000.0f)
                        {
                            sp128 = (struct WeaponObjRecord *)create_new_item_instance_of_model(PROP_CHRGRENADEROUND, 0x57);
                            if (sp128 != NULL)
                            {
                                matrix_4x4_set_identity(&spE8);
                                spDC.f[0] = sp220.f[0] * 33.333332f;
                                spDC.f[1] = sp220.f[1] * 33.333332f;
                                spDC.f[2] = sp220.f[2] * 33.333332f;
                                matrix_4x4_set_rotation_around_x(sp24C, &sp9C);
                                matrix_4x4_set_rotation_around_y(subroty, &sp5C);
                                matrix_4x4_multiply_homogeneous_in_place(&sp5C, &sp9C);
                                sp128->timer = CHRLV_DEFAULT_TIMER;
                                gunInitProjectileObject((ObjectRecord *) sp128, &sp240, sp238, &sp9C, &spDC, &spE8, self_prop);

                                if (sp128->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
                                {
                                    sp128->projectile->unk8C = 0.3f;
                                    sp128->projectile->unk94 = 0.13333333f;
#ifdef REFRESH_PAL
                                    sp128->projectile->refreshrate = 50;
#else
                                    sp128->projectile->refreshrate = 60;
#endif
                                }
                            }
                        }
                        else
                        {
                            sp27C = 0;
                        }
                    }
                    else
                    {
                        if ((sp44 != 0) && (sp21C != 0))
                        {
                            dx = (player_prop->pos.f[0] - sp240.f[0]) - (sp220.f[0] * 15.0f);
                            dy = (player_prop->pos.f[1] - sp240.f[1]) - (sp220.f[1] * 15.0f);
                            dz = (player_prop->pos.f[2] - sp240.f[2]) - (sp220.f[2] * 15.0f);

                            if (((dx * dx) + (dy * dy) + (dz * dz)) <= sp20C)
                            {
                                chrlvUpdateShotbondsum(self, &sp234, &sp230, prop_selfchr->act_attack.attack_item);
                                sp22C = sp230 == 0;

                                if ((sp234 != 0) && ((self->actiontype == ACT_ATTACK) || (self->actiontype == ACT_ATTACKROLL)))
                                {
                                    self->act_attack.attack_time = g_GlobalTimer;
                                }
                            }
                        }
                        else
                        {
                            if ((self->actiontype == ACT_ATTACK) || (self->actiontype == ACT_ATTACKROLL))
                            {
                                self->act_attack.attack_time = g_GlobalTimer;
                            }
                        }

                        if (sp230 != 0)
                        {
                            sp258.f[0] = player_prop->pos.f[0];
                            sp258.f[1] = player_prop->pos.f[1];
                            sp258.f[2] = player_prop->pos.f[2];
                            sp254 = player_prop->stan;
                            recall_joy2_hits_edit_detail_edit_flag(prop_selfchr->act_attack.attack_item, &player_prop->type, -1);
                        }
                        else
                        {
                            if ((
                                    (stanSavedColl_posData == NULL)
                                    || ((stanSavedColl_posData->type != PROP_TYPE_CHR) && (stanSavedColl_posData->type != PROP_TYPE_VIEWER))
                                )
                                && (sp20C < 10000.0f))
                            {
                                sp22C = 0;
                            }
                        }

                        if (sp22C != 0)
                        {
                            if (sp254 != 0)
                            {
                                bullet_spark_create(&sp258, 1, 26.0f, (s16) sp254->room);
                            }

                            if (stanSavedColl_posData != NULL)
                            {
                                recall_joy2_hits_edit_detail_edit_flag(prop_selfchr->act_attack.attack_item, &stanSavedColl_posData->type, -1);

                                if (stanSavedColl_posData->type == PROP_TYPE_CHR)
                                {
                                    /* Enemy friendly-fire cheat: a guard's shot
                                     * at Bond that hits another chr first
                                     * damages it and plays a pain animation.
                                     * The AI still only targets Bond, so this
                                     * stray-shot case is the whole cheat. */
                                    if ((self->chrflags & CHRFLAG_CAN_SHOOT_CHRS) != 0
#ifndef TARGET_N64
                                        || cheatIsActive(CHEAT_PORT_ENEMY_FF)
#endif
                                        )
                                    {
#ifndef TARGET_N64
                                        /* Rare's flagged guards hit HIT_CHEST (0xF) every time; the
                                         * cheat finds the real part (chr.c portChrStrayHitPart) */
                                        extern s32 portChrStrayHitPart(PropRecord *prop, coord3d *from, coord3d *dir);
                                        static s32 fflog = -1;
                                        const s32 ffpart = cheatIsActive(CHEAT_PORT_ENEMY_FF)
                                                         ? portChrStrayHitPart(stanSavedColl_posData, &sp240, &sp220) : 0xF;

                                        if (fflog < 0)
                                        {
                                            extern char *getenv(const char *);
                                            const char *e = getenv("GE_ENEMYFF_LOG");

                                            fflog = (e != NULL && e[0] == '1');
                                        }
                                        if (fflog)
                                        {
                                            extern int printf(const char *, ...);
                                            extern s32 g_PortStrayHitByModel;

                                            printf("ge: enemyff: chr %d's shot hit chr %d (item %d, part %d by %s)\n", (int) self->chrnum,
                                                   (int) stanSavedColl_posData->chr->chrnum, (int) prop_selfchr->act_attack.attack_item, (int) ffpart,
                                                   g_PortStrayHitByModel ? "model" : "height");
                                        }
                                        handles_shot_actors(stanSavedColl_posData->chr, ffpart, &sp220, prop_selfchr->act_attack.attack_item, 0);
#else
                                        handles_shot_actors(stanSavedColl_posData->chr, 0xF, &sp220, prop_selfchr->act_attack.attack_item, 0);
#endif
                                    }
                                }
                                else if ((stanSavedColl_posData->type == PROP_TYPE_OBJ) || (stanSavedColl_posData->type == PROP_TYPE_WEAPON))
                                {
                                    chrobjMaybeDetonateObjectIfFlags(
                                        stanSavedColl_posData->obj,
                                        gunItemGetDestructionAmount(prop_selfchr->act_attack.attack_item),
                                        &sp258,
                                        prop_selfchr->act_attack.attack_item,
                                        get_cur_playernum());
                                }
                            }
                            else
                            {
                                recall_joy2_hits_edit_flag(prop_selfchr->act_attack.attack_item, &sp258, -1);
                            }
                        }

                        if (sp264 != 0)
                        {
                            switch (prop_selfchr->act_attack.attack_item)
                            {
                                case ITEM_WPPK:
                                case ITEM_WPPKSIL:
                                case ITEM_TT33:
                                case ITEM_SKORPION:
                                case ITEM_AK47:
                                case ITEM_UZI:
                                case ITEM_MP5K:
                                case ITEM_MP5KSIL:
                                case ITEM_SPECTRE:
                                case ITEM_M16:
                                case ITEM_FNP90:
                                case ITEM_RUGER:
                                case ITEM_GOLDENGUN:
                                case ITEM_SILVERWPPK:
                                case ITEM_GOLDWPPK:
                                case ITEM_LASER:
                                    sp264 = 1;
                                    break;

                                default:
                                    sp264 = 0;
                                    break;
                            }
                        }

                        if (sp264 != 0)
                        {
                            CapBeamLengthAndDecideIfRendered(&self->beams[hand], prop_selfchr->act_attack.attack_item, &sp240, &sp258);
                        }
                    }
                }
            }

            phi_a2 = (sp27C != 0) || (sp278 != 0);

            sub_GAME_7F02BFE4(self, hand, phi_a2);
        }

        chrSetFiring(self, hand, sp27C);
    }
}


/**
 * Address 0x7F02E26C.
*/
void chrlvTriggerFireWeapon(ChrRecord *self)
{
    self->hidden &= ~CHRHIDDEN_FIRE_TRACER;

    if (self->hidden & CHRHIDDEN_FIRE_WEAPON_RIGHT)
    {
        chrlvFireWeaponRelated(self, GUNRIGHT);

        self->hidden &= ~CHRHIDDEN_FIRE_WEAPON_RIGHT;
    }

     if (self->hidden & CHRHIDDEN_FIRE_WEAPON_LEFT)
    {
        chrlvFireWeaponRelated(self, GUNLEFT);

        self->hidden &= ~CHRHIDDEN_FIRE_WEAPON_LEFT;
    }
}


/**
 * Address 0x7F02E2E0.
*/
s32 chrlvAttackrollAnimationRelated7F02E2E0(ChrRecord *self)
{
    Model *model;
    struct weapon_firing_animation_table *p;
    s32 sp24;

    if ((self->act_attackroll.animfloats == &D_80030078[2]) || (self->act_attackroll.animfloats == &D_80030078[3]))
    {
        model = self->model;
        sp24 = (s32) model->gunhand;
        self->act_attackroll.unk30 = 2;
        self->act_attackroll.animfloats = &D_80030078[1];
        self->sleep = 0;

        p = &D_80030078[1];

        modelSetAnimation(
            model,
            (void *) p->anim.anim,
            sp24,
            p->shoot_end_frame,
            chrlvGetGuard007SpeedRating(self, 0.7f, 1.12f),
            22.0f);

        if (D_80030078[1].end_frame >= 0.0f)
        {
            modelSetAnimEndFrame(model, D_80030078[1].end_frame);
        }

        return 1;
    }

    return 0;
}


/**
 * Address 0x7F02E3B8.
*/
void chrlvAttackrollAnimationRelated7F02E3B8(ChrRecord *self)
{
    Model *model;

    model = self->model;

    if (self->act_attackroll.animfloats->recoil_end_frame > 0.0f)
    {
        modelSetAnimation(
            model,
            objecthandlerGetModelAnim(model),
            (s32) model->gunhand,
            self->act_attackroll.animfloats->recoil_end_frame,
            chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f),
            8.0f);
    }
    else
    {
        modelSetAnimation(
            model,
            objecthandlerGetModelAnim(model),
            (s32) model->gunhand,
            self->act_attackroll.animfloats->shoot_end_frame,
            chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f),
            8.0f);
    }

    if (self->act_attackroll.animfloats->end_frame >= 0.0f)
    {
        modelSetAnimEndFrame(model, self->act_attackroll.animfloats->end_frame);
    }
}


/**
 * Address 0x7F02E4C0.
 * Address 0x7F02E4F4 (VERSION_EU).
*/
void chrlvTickAttackCommon(ChrRecord *self)
{
    s32 i;
    Model *self_model;
    f32 df;
    f32 temp_f0_6;
    f32 fp1; // 92
    f32 phi_f20;
    f32 fp2;
    f32 fn40; // 80
    f32 fanon1; // 76

    self_model = self->model;
    phi_f20 = modelGetAnimFrame(self_model);

    if (
#ifdef REFRESH_PAL
        (self->act_attack.attack_time < (self->act_attack.unk44 - 25))
#else
        (self->act_attack.attack_time < (self->act_attack.unk44 - 30))
#endif
        && (self_model->anim2 == NULL))
    {
        if (((self->act_attack.animfloats->shoot_start_frame + 10.0f) < phi_f20)
            && (phi_f20 < self->act_attack.animfloats->shoot_end_frame))
        {
            if (((self->act_attack.animfloats->recoil_end_frame < 0.0f)) || (phi_f20 < self->act_attack.animfloats->recoil_end_frame))
            {
                if (self->act_attack.unk36 == 0)
                {
                    if (chrlvAttackrollAnimationRelated7F02E2E0(self) == 0)
                    {
                        modelSetAnimation(
                            self_model,
                            objecthandlerGetModelAnim(self_model),
                            (s32) self_model->gunhand,
                            self->act_attack.animfloats->shoot_end_frame,
                            chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f),
                            8.0f);

                        if (self->act_attack.animfloats->end_frame >= 0.0f)
                        {
                            modelSetAnimEndFrame(self_model, self->act_attack.animfloats->end_frame);
                        }
                    }
                }
                else
                {
                    chrlvAttackrollAnimationRelated7F02E3B8(self);
                }

                self->act_attack.unk33 = (s8) (self->act_attack.unk34 + 1);
                phi_f20 = modelGetAnimFrame(self_model);
            }
        }
    }

    if (modelGetAnimEndFrame(self_model) <= phi_f20)
    {
        if ((self->act_attack.unk37 != 0) || (self->act_attack.unk34 < self->act_attack.unk33))
        {
            if (chrlvAttackrollAnimationRelated7F02E2E0(self) == 0)
            {
                if ((self->act_attack.attacktype & TARGET_BOND) != 0)
                {
                    chrlvSetTargetToPlayer(self);
                }

                chrlvKneelingAnimationRelated7F023E48(self);

                return;
            }
        }
        else if (self->act_attack.unk33 == self->act_attack.unk34)
        {
            self->act_attack.unk33++;
            chrlvAttackrollAnimationRelated7F02E3B8(self);
        }
        else if (self->act_attack.unk31 != 0)
        {
            temp_f0_6 = 0.5f;

            if (self->act_attack.unk36 != 0)
            {
                if (self->act_attack.animfloats->recoil_start_frame > 0.0f)
                {
                    fp1 = self->act_attack.animfloats->recoil_start_frame;
                }
                else
                {
                    fp1 = self->act_attack.animfloats->shoot_start_frame;
                }

                if (self->act_attack.animfloats->recoil_end_frame > 0.0f)
                {
                    fp2 = self->act_attack.animfloats->recoil_end_frame;
                }
                else
                {
                    fp2 = self->act_attack.animfloats->shoot_end_frame;
                }
            }
            else
            {
                fp1 = self->act_attack.animfloats->shoot_start_frame;

                if (self->act_attack.animfloats->recoil_start_frame > 0.0f)
                {
                    fp2 = self->act_attack.animfloats->recoil_start_frame;
                }
                else
                {
                    fp2 = self->act_attack.animfloats->shoot_end_frame;
                }
            }

            df = fp2 - fp1;
            if (df < 12.0f)
            {
                temp_f0_6 = (df * 0.5f) / 12.0f;
            }
            else if (df > 16.0f)
            {
                temp_f0_6 = df * 0.5f * 0.0625f;
            }

            if ((self->act_attack.unk3a[0] != 0) && (self->act_attack.unk3a[1] != 0))
            {
                temp_f0_6 = 2.0f * temp_f0_6;
            }

            self->act_attack.unk31 = 0;

            modelSetAnimation(self_model, objecthandlerGetModelAnim(self_model), (s32) self_model->gunhand, fp1, temp_f0_6, 8.0f);
            modelSetAnimEndFrame(self_model, fp2);
        }

        phi_f20 = modelGetAnimFrame(self_model);
    }

    if ((self->act_attack.attacktype & TARGET_DONTTURN) == 0)
    {
        fn40 = self->act_attack.animfloats->angle_offset;
        fanon1 = self->act_attack.animfloats->unk04;

        if ((self->act_attack.attacktype & TARGET_AIM_ONLY) != 0)
        {
            if (modelGetAnimEndFrame(self_model) < fanon1)
            {
                fanon1 = modelGetAnimEndFrame(self_model);
            }
        }

        if (self_model->gunhand != GUNRIGHT)
        {
            fn40 = M_TAU_F - fn40;
        }

        self->act_attack.unk30 = chrlvSetSubroty(
            self,
            (s32) self->act_attack.unk30,
            fanon1,
            chrlvGetGuard007SpeedRating(self, 1.0f, 1.6f),
            fn40);
    }

    if ((self->act_attack.animfloats->aim_start_frame < phi_f20) && (phi_f20 < self->act_attack.animfloats->aim_end_frame))
    {
        chrlvUpdateAimendsideback(self, self->act_attack.animfloats, (s32) self->act_attack.unk38[1], (s32) self->act_attack.unk38[0], 1.0f);
    }
    else
    {
        chrlvResetAimend(self);
    }

    for (i=0; i<2; i++)
    {
        if (self->act_attack.unk38[i] != 0)
        {
            if (self->act_attack.unk3a[i] == 0)
            {
                if ((self->act_attack.animfloats->shoot_start_frame <= phi_f20) && (phi_f20 < self->act_attack.animfloats->shoot_end_frame))
                {
                    chrlvToggleHiddenRelated(self, i, 1);
                    self->act_attack.unk44 = g_GlobalTimer;

                    if (self->actiontype == ACT_ATTACKROLL)
                    {
#ifdef REFRESH_PAL
                        df = ((self->act_attack.animfloats->shoot_end_frame - self->act_attack.animfloats->shoot_start_frame) * 50.0f) / 60.0f;
#else
                        df = self->act_attack.animfloats->shoot_end_frame - self->act_attack.animfloats->shoot_start_frame;
#endif

                        if (df < 30.0f)
                        {
#ifdef REFRESH_PAL
                            if ((s32) self->act_attack.unk40 >= (50 - ((s32) df * 2)))
#else
                            if ((s32) self->act_attack.unk40 >= (60 - ((s32) df * 2)))
#endif
                            {
                                modelSetAnimSpeed(self_model, 0.5f, 0.0f);
                            }
                            else
                            {
                                modelSetAnimSpeed(self_model, 0.1f, 0.0f);
                                self->act_attack.unk40 += g_ClockTimer;
                            }
                        }
                        else
                        {
                            modelSetAnimSpeed(self_model, 0.5f, 0.0f);
                        }
                    }
                    else
                    {
                        modelSetAnimSpeed(self_model, 0.5f, 0.0f);
                    }
                }
                else
                {
                    chrlvToggleHiddenRelated(self, i, 0);

                    if (self->actiontype == ACT_ATTACKROLL)
                    {
                        modelSetAnimSpeed(self_model, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 0.0f);
                    }
                    else
                    {
                        modelSetAnimSpeed(self_model, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 0.0f);
                    }
                }
            }
            else if (
                (self->act_attack.unk31 == 0)
                && ((i == self->act_attack.unk32) || (self->act_attack.unk3a[self->act_attack.unk32] == 0))
                && (
                    (
                        ( self->act_attack.animfloats->recoil_start_frame >= 0.0f)
                        && (self->act_attack.animfloats->recoil_start_frame <= phi_f20)
                        && (phi_f20 <= self->act_attack.animfloats->recoil_end_frame))
                    ||
                    (
                        (self->act_attack.animfloats->recoil_start_frame < 0.0f)
                        && (self->act_attack.animfloats->shoot_start_frame <= phi_f20)
                    )))
            {
                self->act_attack.unk31 = 1;
                self->act_attack.unk32 = (s8) (1 - self->act_attack.unk32);
                self->act_attack.unk33++;
                self->act_attack.unk44 = g_GlobalTimer;

                chrlvToggleHiddenRelated(self, i, 1);
            }
            else
            {
                chrlvToggleHiddenRelated(self, i, 0);
            }
        }
        else
        {
            chrlvToggleHiddenRelated(self, i, 0);
        }
    }
}


/**
 * Address 0x7F02EBFC (VERSION_US).
 * Adresss 0x7F02EF04 (other).
*/
void chrlvTickAttack(ChrRecord *self)
{
    Model *self_model;
    f32 temp_f0;
    f32 phi_f2;

    self_model = self->model;
    temp_f0 = modelGetAnimFrame(self_model);

    if (self->act_attack.type_of_motion)
    {
        if (self->act_attack.type_of_motion == 1)
        {
            if (self->act_attack.animfloats->recoil_end_frame >= 0.0f)
            {
                phi_f2 = self->act_attack.animfloats->recoil_end_frame;
            }
            else
            {
                phi_f2 = self->act_attack.animfloats->shoot_end_frame;
            }

            modelSetAnimation(
                self_model,
                objecthandlerGetModelAnim(self_model),
                (s32) self_model->gunhand,
                phi_f2,
                chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f),
                16.0f);

            if (self->act_attack.animfloats->end_frame >= 0.0f)
            {
                modelSetAnimEndFrame(self_model, self->act_attack.animfloats->end_frame);
            }

            self->act_attack.type_of_motion = 2;
            chrlvResetAimend(self);

            return;
        }

        if (self->act_attack.type_of_motion == 2)
        {
            if (modelGetAnimEndFrame(self_model) <= temp_f0)
            {
                self->act_attack.attacktype &= ~TARGET_DONTTURN;

                if (self->act_attack.unk54 != 0)
                {
                    sub_GAME_7F025560(self, (s32) self->act_attack.attacktype, self->act_attack.entityid);

                    return;
                }

                sub_GAME_7F0256F0(self, (s32) self->act_attack.attacktype, self->act_attack.entityid);

                return;
            }

            return;
        }
    }

    if ((self->act_attack.attacktype & TARGET_AIM_ONLY) != 0)
    {
        if ((self->act_attack.attacktype & TARGET_DONTTURN) != 0)
        {
            if (chrlvUpdateAimendsideback(self, self->act_attack.animfloats, (s32) self->act_attack.unk38[1], (s32) self->act_attack.unk38[0], 0.2f) == 0)
            {
                self->act_attack.type_of_motion = 1;
            }

            return;
        }

        if (modelGetAnimEndFrame(self_model) <= temp_f0)
        {
            self->act_attack.attacktype |= TARGET_DONTTURN;
            self->act_attack.unk30 = 2;

            return;
        }
    }

    if (self->act_attack.unk36 == 0)
    {
        if ((self->act_attack.animfloats->recoil_end_frame > 0.0f) && (temp_f0 <= self->act_attack.animfloats->recoil_end_frame))
        {
            if (modelGetAnimEndFrame(self_model) <= temp_f0)
            {
                modelSetAnimation(
                    self_model,
                    objecthandlerGetModelAnim(self_model),
                    (s32) self_model->gunhand,
                    self->act_attack.animfloats->recoil_end_frame,
                    chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f),
                    16.0f);

                if (self->act_attack.unk37 != 0)
                {
                    if (self->act_attack.animfloats->end_frame >= 0.0f)
                    {
                        modelSetAnimEndFrame(self_model, self->act_attack.animfloats->end_frame);
                    }
                }
                else
                {
                    modelSetAnimEndFrame(self_model, self->act_attack.animfloats->shoot_end_frame);
                }
            }
        }
    }

    chrlvTickAttackCommon(self);
}


/**
 * Address 0x7F02EEE0.
*/
void chrlvTickAttackRoll(ChrRecord *self)
{
    Model *temp_a0; // 68
    f32 temp_f0;
    struct weapon_firing_animation_table *phi_v1; // 60
    s32 sp38; // 56
    f32 phi_f2_2; // 52
    struct modeldata_root *temp_v0_2;
    struct weapon_firing_animation_table *temp_v0;

    if (self->act_attackroll.unk35 != 0)
    {
        temp_a0 = self->model;
        temp_f0 = modelGetAnimFrame(temp_a0);

        if (
            (self->act_attackroll.animfloats == &D_80030078[4])
            || (self->act_attackroll.animfloats == &D_80030078[5])
            || (self->act_attackroll.animfloats == &D_80030078[6])
            || (self->act_attackroll.animfloats == &D_80030078[7])
        )
        {
            if (self->act_attackroll.animfloats->end_frame <= temp_f0)
            {
                sp38 = (s32) temp_a0->gunhand;
                phi_v1 = &self->act_attackroll.animfloats[4];

                phi_f2_2 = 16.0f;

                if ((self->act_attackroll.unk38[1] != 0) && (self->act_attackroll.unk38[0] != 0))
                {
                    if ((randomGetNext() & 1) == 0)
                    {
                        phi_v1 = &phi_v1[4];
                    }
                    else
                    {
                        phi_v1 = &phi_v1[8];
                    }
                }

                if (phi_v1 == &D_80030078[8])
                {
                    phi_f2_2 = 24.0f;
                }
                else if (phi_v1 == &D_80030078[9])
                {
                    phi_f2_2 = 24.0f;
                }
                else if (phi_v1 == &D_80030078[10])
                {
                    phi_f2_2 = 32.0f;
                }
                else if (phi_v1 == &D_80030078[11])
                {
                    phi_f2_2 = 44.0f;
                }
                else if (phi_v1 == &D_80030078[12])
                {
                    phi_f2_2 = 24.0f;
                }
                else if (phi_v1 == &D_80030078[13])
                {
                    phi_f2_2 = 34.0f;
                }
                else if (phi_v1 == &D_80030078[14])
                {
                    phi_f2_2 = 32.0f;
                }
                else if (phi_v1 == &D_80030078[15])
                {
                    phi_f2_2 = 44.0f;
                }
                else if (phi_v1 == &D_80030078[16])
                {
                    phi_f2_2 = 24.0f;
                }
                else if (phi_v1 == &D_80030078[17])
                {
                    phi_f2_2 = 34.0f;
                }
                else if (phi_v1 == &D_80030078[18])
                {
                    phi_f2_2 = 32.0f;
                }
                else if (phi_v1 == &D_80030078[19])
                {
                    phi_f2_2 = 44.0f;
                }

                self->act_attackroll.unk30 = 2;
                self->act_attackroll.animfloats = phi_v1;
                self->sleep = 0;

                modelSetAnimation(temp_a0, (void *) phi_v1->anim.anim, sp38, phi_v1->start_frame, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), phi_f2_2);

                if (self->act_attackroll.unk36 != 0)
                {
                    if (phi_v1->recoil_end_frame >= 0.0f)
                    {
                        modelSetAnimEndFrame(temp_a0, phi_v1->recoil_end_frame);
                    }
                    else
                    {
                        modelSetAnimEndFrame(temp_a0, phi_v1->shoot_end_frame);
                    }
                }
                else if (phi_v1->recoil_start_frame >= 0.0f)
                {
                    modelSetAnimEndFrame(temp_a0, phi_v1->recoil_start_frame);
                }
                else if (phi_v1->end_frame >= 0.0f)
                {
                    modelSetAnimEndFrame(temp_a0, phi_v1->end_frame);
                }

                if (self->act_attackroll.animfloats->angle_offset != 0.0f)
                {
                    temp_v0_2 = (struct modeldata_root *)modelGetNodeRwData(temp_a0, temp_a0->obj->RootNode);
                    temp_v0_2->unk5c = phi_f2_2;
                    temp_v0_2->unk58 = (-self->act_attackroll.animfloats->angle_offset / phi_f2_2);

                    if (sp38 != GUNRIGHT)
                    {
                        temp_v0_2->unk58 = -temp_v0_2->unk58;
                    }
                }
            }
        }
        else if (
            (
                (self->act_attackroll.animfloats == &D_80030078[8])
                || (self->act_attackroll.animfloats == &D_80030078[9])
                || (self->act_attackroll.animfloats == &D_80030078[10])
                || (self->act_attackroll.animfloats == &D_80030078[11])
                || (self->act_attackroll.animfloats == &D_80030078[12])
                || (self->act_attackroll.animfloats == &D_80030078[13])
                || (self->act_attackroll.animfloats == &D_80030078[14])
                || (self->act_attackroll.animfloats == &D_80030078[15])
                || (self->act_attackroll.animfloats == &D_80030078[16])
                || (self->act_attackroll.animfloats == &D_80030078[17])
                || (self->act_attackroll.animfloats == &D_80030078[18])
                || (self->act_attackroll.animfloats == &D_80030078[19])
            )
            && (self->act_attackroll.unk36 == 0))
        {
            if ((self->act_attackroll.animfloats->recoil_end_frame > 0.0f) && (temp_f0 <= self->act_attackroll.animfloats->recoil_end_frame))
            {
                if (modelGetAnimEndFrame(temp_a0) <= temp_f0)
                {
                    modelSetAnimation(temp_a0, objecthandlerGetModelAnim(temp_a0), (s32) temp_a0->gunhand, self->act_attackroll.animfloats->recoil_end_frame, chrlvGetGuard007SpeedRating(self, 0.5f, 0.8f), 16.0f);

                    if (self->act_attackroll.unk37 != 0)
                    {
                        if (self->act_attackroll.animfloats->end_frame >= 0.0f)
                        {
                            modelSetAnimEndFrame(temp_a0, self->act_attackroll.animfloats->end_frame);
                        }
                    }
                    else
                    {
                        modelSetAnimEndFrame(temp_a0, self->act_attackroll.animfloats->shoot_end_frame);
                    }
                }
            }
        }
    }
    chrlvTickAttackCommon(self);
}


/**
 * Address 0x7F02F3F8.
*/
void chrlvTickThrowGrenade(ChrRecord *self)
{
    Model *self_model;
    f32 temp_f2;
    s32 gunhand;
    PropRecord *held_prop;

    self_model = self->model;
    temp_f2 = modelGetAnimFrame(self_model);
    gunhand = (self_model->gunhand != GUNRIGHT) ? GUNLEFT : GUNRIGHT;
    held_prop = chrGetEquippedWeaponProp(self, gunhand);

    if ((temp_f2 >= 20.0f) && (held_prop != NULL))
    {
        struct ObjectRecord *obj = held_prop->obj;
        obj->runtime_bitflags &= ~0x800;
    }

    if ((temp_f2 >= 61.0f) && (held_prop != NULL))
    {
        struct WeaponObjRecord *weap = held_prop->weapon;
        weap->timer = CHRLV_DEFAULT_TIMER;
    }

    if ((temp_f2 >= 119.0f) && (held_prop != NULL))
    {
        propobjSetDropped(self->weapons_held[gunhand], 3);
        self->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
    }

    if (modelGetAnimFrame(self_model) >= modelGetAnimEndFrame(self_model))
    {
        chrlvKneelingAnimationRelated7F023E48(self);

        return;
    }

    if ((temp_f2 >= 87.0f) && (temp_f2 <= 110.0f))
    {
        chrlvSetSubroty(self, 1, 110.0f, chrlvGetGuard007SpeedRating(self, 1.0f, 1.6f), 0.0f);
    }
}


/**
 * Address 0x7F02F5A4.
*/
void chrlvTickBondIntro(ChrRecord *self)
{
    Model *self_model;
    f32 sp28;

    self_model = self->model;
    sp28 = modelGetAnimFrame(self_model);

    if ((sp28 < 86.0f) && (modelGetAnimEndFrame(self_model) <= sp28))
    {
        modelSetAnimation(
            self_model,
            portAnimationFromOffset(PTR_ANIM_fire_standing_draw_one_handed_weapon_fast),
            0,
            86.0f,
            modelGetAnimSpeed(self_model),
            24.0f);

        modelSetAnimEndFrame(self_model, 131.0f);

        return;
    }

    if (modelGetAnimEndFrame(self_model) <= sp28)
    {
        chrlvKneelingAnimationRelated(self);
    }
}


/**
 * Address 0x7F02F688.
*/
void chrlvTickBondDieRemoved(ChrRecord *self)
{
    // removed.
}


#if defined(REFRESH_NTSC)
/* NTSC */
#define MAX_SPEED_A 0.2991993f
#define ACCEL_A 0.014959966f

#define MAX_SPEED_B1 0.019634955f
#define MAX_SPEED_B2 0.09817477f
#define MAX_SPEED_B3 0.19634955f
#define ACCEL_B 0.014959966f

#define MAX_SPEED_C1 0.009817477f
#define MAX_SPEED_C2 0.049087387f
#define MAX_SPEED_C3 0.12566371f
#define ACCEL_C 0.009817477f
#endif

#if defined(REFRESH_PAL)
/* PAL */
#define MAX_SPEED_A 0.359039157629013f
#define ACCEL_A 0.0179519578814507f

#define MAX_SPEED_B1 0.0235619451850653f
#define MAX_SPEED_B2 0.117809727787972f
#define MAX_SPEED_B3 0.235619455575943f
#define ACCEL_B 0.0179519578814507f

#define MAX_SPEED_C1 0.0117809725925326f
#define MAX_SPEED_C2 0.0589048638939858f
#define MAX_SPEED_C3 0.150796458125114f
#define ACCEL_C 0.0117809725925326f
#endif

/**
 * Address 0x7F02F690.
*/
s32 chrlvApplySpeed(ChrRecord *self, coord3d *arg1, s32 arg2, f32 *speedPtr)
{
    f32 maxSpeed;
    Model *self_model; // 72
    PropRecord *self_prop;
    f32 accel;
    f32 maxFrac; // 60
    f32 openPosition; // 56
    f32 phi_f2;
    f32 f0f0;
    f32 dx;
    f32 dz;
    s32 sp24;

    self_prop = self->prop;
    self_model = self->model;

    dx = arg1->f[0] - self_prop->pos.f[0];
    dz = arg1->f[2] - self_prop->pos.f[2];

    maxFrac = atan2f(dx, dz);
    openPosition = getsubroty(self_model);

    sp24 = 0;
    phi_f2 = maxFrac - openPosition;

    if (maxFrac < openPosition)
    {
        phi_f2 = phi_f2 + M_TAU_F;
    }

    f0f0 = phi_f2;

    if (phi_f2 > M_PI_F)
    {
        f0f0 = M_TAU_F - phi_f2;
    }

    if (arg2 == 2)
    {
        maxSpeed = MAX_SPEED_A;
        accel = ACCEL_A;
    }
    else if (arg2 == 1)
    {
        if (f0f0 < 0.3926991f)
        {
            maxSpeed = MAX_SPEED_B1;
        }
        else if (f0f0 < 1.2566371f)
        {
            maxSpeed = MAX_SPEED_B2;
        }
        else
        {
            maxSpeed = MAX_SPEED_B3;
        }

        accel = ACCEL_B;
    }
    else
    {
        if (f0f0 < 0.3926991f)
        {
            maxSpeed = MAX_SPEED_C1;
        }
        else if (f0f0 < 1.2566371f)
        {
            maxSpeed = MAX_SPEED_C2;
        }
        else
        {
            maxSpeed = MAX_SPEED_C3;
        }

        accel = ACCEL_C;
    }

    maxSpeed *= self_model->playspeed;
    accel *= self_model->playspeed;

#ifndef TARGET_N64
    {   /* field-paced caller: integrate whole fields, not this tick's
         * sub-field delta (see g_portApplySpeedFieldPaced, propobj.c) */
        extern s32 g_portApplySpeedFieldPaced;
        g_portApplySpeedFieldPaced = 1;
    }
#endif
    // void chrobjCallsApplySpeed(f32 *openPosition, f32 maxFrac, f32 *speedPtr, f32 accel, f32 decel, f32 maxSpeed)
    chrobjCallsApplySpeed(
        &openPosition,
        maxFrac,
        speedPtr,
        accel,
        accel * 2.0f,
        maxSpeed);
#ifndef TARGET_N64
    {
        extern s32 g_portApplySpeedFieldPaced;
        g_portApplySpeedFieldPaced = 0;
    }
#endif

    if (openPosition == maxFrac)
    {
        *speedPtr = 0.0f;
        sp24 = 1;
    }

    setsubroty(self_model, openPosition);

    return sp24;
}


/**
 * Address 0x7F02F888.
*/
void chrlvTickAttackWalk(ChrRecord *self)
{
    Model *self_model;
    PropRecord *player_prop;
    f32 temp_f0;
    f32 temp_f2;
    PropRecord *self_prop;
    s32 i;

    self_model = self->model;
    self_prop = self->prop;
    player_prop = getCurrentPlayerProp();
    self->act_attackwalk.clock_timer30 += g_ClockTimer;
    self->lastwalk60 = g_GlobalTimer;

    if (
        (self->invalidmove == 1)
        || (self->lastmoveok60 < (g_GlobalTimer - CHRLV_LASTMOVEOK60_CHECK))
        || (self->act_attackwalk.clock_timer34 < self->act_attackwalk.clock_timer30))
    {
        if (modelGetAnimFrame(self_model) > ((f32)objecthandlerGetModelAnim(self_model)->unk04 * 0.5f))
        {
            sub_GAME_7F06FE90(self_model, 0.0f, 16.0f);
        }
        else
        {
            sub_GAME_7F06FE90(self_model, (f32)objecthandlerGetModelAnim(self_model)->unk04 * 0.5f, 16.0f);
        }

        chrlvSetTargetToPlayer(self);
        chrlvKneelingAnimationRelated7F023E48(self);

        return;
    }

    temp_f0 = player_prop->pos.f[0] - self_prop->pos.f[0];
    temp_f2 = player_prop->pos.f[2] - self_prop->pos.f[2];
    if ((temp_f0 < 300.0f) && (temp_f0 > -300.0f) && (temp_f2 < 300.0f) && (temp_f2 > -300.0f))
    {
        chrlvSetTargetToPlayer(self);
        chrlvKneelingAnimationRelated7F023E48(self);

        return;
    }

    if (chrlvApplySpeed(self, &player_prop->pos, 0, &self->act_attackwalk.speed) != 0)
    {
        self->act_attackwalk.unk038 = 1;
    }

    if (self->act_attackwalk.clock_timer30 >= CHRLV_ATTACKWALK_CLOCK_TIMER30_MIN)
    {
        chrlvUpdateAimendsideback(self, self->act_attackwalk.animfloats, (s32) self->act_attackwalk.unk48[1], (s32) self->act_attackwalk.unk48[0], 1.0f);
    }
    else
    {
        chrlvResetAimend(self);
    }

    if (self->act_attackwalk.unk038 && !(self->act_attackwalk.clock_timer30 < CHRLV_ATTACKWALK_CLOCK_TIMER30_MAX))
    {
        for (i=0; i<2; i++)
        {
            if (self->act_attackwalk.unk48[i])
            {
                if (self->act_attackwalk.unk4a[i] == 0)
                {
                    chrlvToggleHiddenRelated(self, i, 1);
                }
                else
                {
                    if ((self->act_attackwalk.timer40 < self->act_attackwalk.clock_timer30)
                        && ((i == self->act_attackwalk.unk044) || (self->act_attackwalk.unk4a[self->act_attackwalk.unk044] == 0)))
                    {
                        self->act_attackwalk.timer40 = self->act_attackwalk.clock_timer30;

                        if (self->act_attackwalk.unk4a[1 - i])
                        {
                            if (self->act_attackwalk.unk4C[i])
                            {
                                self->act_attackwalk.timer40 += CHRLV_ATTACKWALK_TIMER40_A;
                            }
                            else
                            {
                                self->act_attackwalk.timer40 += CHRLV_ATTACKWALK_TIMER40_B;
                            }
                        }
                        else if (self->act_attackwalk.unk4C[i])
                        {
                            self->act_attackwalk.timer40 += CHRLV_ATTACKWALK_TIMER40_C;
                        }
                        else
                        {
                            self->act_attackwalk.timer40 += CHRLV_ATTACKWALK_TIMER40_D;
                        }

                        self->act_attackwalk.unk044 = 1 - self->act_attackwalk.unk044;

                        chrlvToggleHiddenRelated(self, i, 1);
                    }
                    else
                    {
                        chrlvToggleHiddenRelated(self, i, 0);
                    }
                }
            }
            else
            {
                chrlvToggleHiddenRelated(self, i, 0);
            }
        }

        return;
    }

    chrlvToggleHiddenRelated(self, GUNLEFT, 0);
    chrlvToggleHiddenRelated(self, GUNRIGHT, 0);
}


/**
 * @param arg0: point in 3d
 * @param arg1: 3 vec
 * @param arg0: point in 3d
 * @param arg0: scalar value
 *
 * Address 0x7F02FC34.
*/
s32 chrlvGeometryRelated7F02FC34(coord3d *arg0, coord3d *arg1, coord3d *arg2, f32 arg3)
{
    coord3d dd;
    f32 temp_f14;

    dd.f[0] = arg2->f[0] - arg0->f[0];
    dd.f[2] = arg2->f[2] - arg0->f[2];

    if ((arg1->f[0] == 0.0f) && (arg1->f[2] == 0.0f))
    {
        return ((dd.f[0] * dd.f[0]) + (dd.f[2] * dd.f[2])) <= (arg3 * arg3);
    }

    temp_f14 = (arg1->f[0] * dd.f[0]) + (arg1->f[2] * dd.f[2]);

    if (temp_f14 > 0.0f)
    {
        f32 tf14_2 = temp_f14 * temp_f14;
        f32 f2 = (arg1->f[0] * arg1->f[0]) + (arg1->f[2] * arg1->f[2]);
        f32 f3 = (dd.f[0] * dd.f[0]) + (dd.f[2] * dd.f[2]);

        if ((f2 * (f3 - (arg3 * arg3))) <= tf14_2)
        {
            return 1;
        }

        return 0;
    }

    return 0;
}


/**
 * @param prevpos: point in 3d
 * @param curpos: 3 vec
 * @param prevpos: point in 3d
 * @param prevpos: scalar value
 *
 * Address 0x7F02FD50.
 *
 * PD posIsArrivingLaterallyAtPos.
*/
s32 chrlvIsArrivingLaterallyAtPos(coord3d *prevpos, coord3d *curpos, coord3d *targetpos, f32 range)
{
    coord3d sp34;

    if ((prevpos->f[0] <= targetpos->f[0] - range) && (curpos->f[0] <= targetpos->f[0] - range))
    {
        return 0;
    }

    if ((targetpos->f[0] + range <= prevpos->f[0]) && (targetpos->f[0] + range <= curpos->f[0]))
    {
        return 0;
    }

    if ((prevpos->f[2] <= targetpos->f[2] - range) && (curpos->f[2] <= targetpos->f[2] - range))
    {
        return 0;
    }

    if ((targetpos->f[2] + range <= prevpos->f[2]) && (targetpos->f[2] + range <= curpos->f[2]))
    {
        return 0;
    }

    sp34.f[0] = curpos->f[0] - prevpos->f[0];
    sp34.f[1] = 0.0f;
    sp34.f[2] = curpos->f[2] - prevpos->f[2];

    return chrlvGeometryRelated7F02FC34(prevpos, &sp34, targetpos, range);
}


/**
 * Address 0x7F02FE78.
 * PD chrTickRunPos
*/
void chrlvTickRunPos(ChrRecord *self)
{
    PropRecord *self_prop;
    Model *self_model;
    f32 phi_f2; // 52

    self_prop = self->prop;
    self_model = self->model;
    self->lastwalk60 = g_GlobalTimer;

    if(1)
    {
        // removed
    }

    if ((self->invalidmove == 1)
        || (self->lastmoveok60 < (g_GlobalTimer - CHRLV_LASTMOVEOK60_CHECK))
        || (chrlvIsArrivingLaterallyAtPos(CHRLV_ARRIVE_FROM(self), &self_prop->pos, &self->act_runpos.pos, self->act_runpos.neardist)))
    {
        f32 offset = 0;

        // Maybe had debug side effects, otherwise this doesn't do anything.
        objecthandlerGetModelAnim(self_model);

        phi_f2 = modelGetAnimFrame(self_model) - offset;

        if (phi_f2 < 0.0f)
        {
            phi_f2 += (f32)objecthandlerGetModelAnim(self_model)->unk04;
        }

        if (((f32)objecthandlerGetModelAnim(self_model)->unk04 * 0.5f) < phi_f2)
        {
            phi_f2 = (f32)objecthandlerGetModelAnim(self_model)->unk04 - offset;
            sub_GAME_7F06FE90(self_model, phi_f2, 16.0f);
        }
        else
        {
            phi_f2 = ((f32)objecthandlerGetModelAnim(self_model)->unk04 * 0.5f) - offset;

            if (phi_f2 < 0)
            {
                phi_f2 += (f32)objecthandlerGetModelAnim(self_model)->unk04;
            }

            sub_GAME_7F06FE90(self_model, phi_f2, 16.0f);
        }

        chrlvKneelingAnimationRelated7F023E48(self);

        return;
    }

    chrlvApplySpeed(self, &self->act_runpos.pos, 1, &self->act_runpos.turnspeed);

    if (self->act_runpos.eta60 > 0)
    {
        self->act_runpos.eta60 -= g_ClockTimer;
    }
    else
    {
        f32 sp2C;

        sp2C = D_80030988;

        if (objecthandlerGetModelAnim(self_model) == portAnimationFromOffset(PTR_ANIM_running_one_handed_weapon))
        {
            sp2C = D_80030994;
        }

        self->act_runpos.neardist += sp2C * CHRLV_FIELD_DELTA * modelGetAbsAnimSpeed(self_model);   /* GE_TICKHZ: field-paced */
    }
}


/**
 * Address 0x7F030128.
*/
s32 sub_GAME_7F030128(ChrRecord *self, coord3d *point, StandTile *arg2, coord3d *dest, StandTile * arg4, s32 cdtypes)
{
    StandTile *sp44;
    s32 sp40;
    f32 sp3C;
    f32 sp38;
    f32 sp34;

    sp44 = arg2;
    sp40 = 0;

    chrGetChrWidthHeight(self->prop, &sp34, &sp3C, &sp38);

    chrSetMoving(self, 0);

    if (
        stanTestLineUnobstructed(&sp44, point->f[0], point->f[2], dest->f[0], dest->f[2], cdtypes, sp3C, sp38, 0.0f, 1.0f)
        && ((arg4 == NULL) || (sp44 == arg4)))
    {
        sp40 = 1;
    }

    chrSetMoving(self, 1);

    return sp40;
}


/**
 * Address 0x7F0301FC.
*/
s32 sub_GAME_7F0301FC(ChrRecord *self, coord3d *point, StandTile *arg2, coord3d *dest, f32 arg4, s32 cdtypes)
{
    StandTile *pstan;
    coord3d dd;
    f32 temp_f20;
    f32 temp_f22;
    f32 norm;
    s32 ret; // 104
    f32 sp64;
    f32 sp60;
    f32 sp5C;

    ret = 0;

    chrGetChrWidthHeight(self->prop, &sp5C, &sp64, &sp60);

    dd.f[0] = dest->f[0] - point->f[0];
    dd.f[1] = 0.0f;
    dd.f[2] = dest->f[2] - point->f[2];

    if ((dd.f[0] == 0.0f) && (dd.f[2] == 0.0f))
    {
        ret = 1;
    }
    else
    {
        norm = (dd.f[0] * dd.f[0]) + (dd.f[2] * dd.f[2]);
        norm = 1.0f / sqrtf(norm);

        dd.f[0] *= norm;
        dd.f[2] *= norm;

        temp_f20 = arg4 * dd.f[0];
        temp_f22 = arg4 * dd.f[2];

        chrSetMoving(self, 0);

        pstan = arg2;

        if (stanTestLineUnobstructed(&pstan, point->f[0], point->f[2], point->f[0] + temp_f22, point->f[2] - temp_f20, cdtypes, sp64, sp60, 0.0f, 1.0f)
            && stanTestLineUnobstructed(&pstan, point->f[0] + temp_f22, point->f[2] - temp_f20, dest->f[0] + temp_f22, dest->f[2] - temp_f20, cdtypes, sp64, sp60, 0.0f, 1.0f))
        {
            pstan = arg2;

            if (stanTestLineUnobstructed(&pstan, point->f[0], point->f[2], point->f[0] - temp_f22, point->f[2] + temp_f20, cdtypes, sp64, sp60, 0.0f, 1.0f)
                && stanTestLineUnobstructed(&pstan, point->f[0] - temp_f22, point->f[2] + temp_f20, dest->f[0] - temp_f22, dest->f[2] + temp_f20, cdtypes, sp64, sp60, 0.0f, 1.0f))
            {
                ret = 1;
            }
        }

        chrSetMoving(self, 1);
    }

    return ret;
}


/**
 * Address 0x7F0304AC.
*/
s32 sub_GAME_7F0304AC(ChrRecord *self, coord3d *mypos, StandTile *mystan, coord3d *arg3, coord3d *bondpos, StandTile *bondstan, s32 cdtypes)
{
    StandTile *sp44;
    bool pass;
    f32 sp3C;
    f32 sp38;
    f32 sp34;
    StandTile *sp30;

    sp44 = mystan; // duplicate var? needed?
    pass = FALSE;

    chrGetChrWidthHeight(self->prop, &sp34, &sp3C, &sp38);
    chrSetMoving(self, 0);

    if (stanTestLineUnobstructed(&sp44, mypos->x, mypos->z, arg3->x, arg3->z, cdtypes, sp3C, sp38, 0.0f, 1.0f))
    {
        sp30 = sp44; // duplicate var? needed?

        if (stanTestLineUnobstructed(&sp30, arg3->x, arg3->z, bondpos->x, bondpos->z, cdtypes, sp3C, sp38, 0.0f, 1.0f)
            && ((bondstan == NULL) || (sp30 == bondstan)))
        {
            pass = TRUE;
        }
    }

    chrSetMoving(self, 1);

    return pass;
}


/**
 * Unreferenced.
 *
 * Address 0x7F0305E0.
*/
s32 sub_GAME_7F0305E0(ChrRecord *self, coord3d *arg1, StandTile *arg2, coord3d *arg3, coord3d *arg4, f32 arg5, s32 cdtypes)
{
    StandTile *sp4C;
    s32 sp48;
    f32 sp44;
    f32 sp40;
    f32 sp3C;
    StandTile *sp38;

    sp4C = arg2;
    sp48 = 0;

    chrGetChrWidthHeight(self->prop, &sp3C, &sp44, &sp40);
    chrSetMoving(self, 0);

    if (stanTestLineUnobstructed(&sp4C, arg1->x, arg1->f[2], arg3->x, arg3->f[2], cdtypes, sp44, sp40, 0.0f, 1.0f))
    {
        sp38 = sp4C;

        if (stanTestLineUnobstructed(&sp38, arg3->x, arg3->f[2], arg4->x, arg4->f[2], cdtypes, sp44, sp40, 0.0f, 1.0f)
            && sub_GAME_7F0301FC(self, arg1, arg2, arg3, arg5, cdtypes)
            && sub_GAME_7F0301FC(self, arg3, sp4C, arg4, arg5, cdtypes))
        {
            sp48 = 1;
        }
    }

    chrSetMoving(self, 1);

    return sp48;
}


/**
 * Subtract arg0 from arg1. Take the determinate of the result and arg2.
 * If determinate is not greater than zero, then swap arg0 and arg1.
 *
 * Address 0x7F03074C.
*/
void chrlvSwapIfDiffArg2Determinate(coord3d *arg0, coord3d *arg1, coord3d *arg2)
{
    coord3d spock;
    coord3d kirk;

    spock.f[0] = arg1->f[0] - arg0->f[0];
    spock.f[1] = arg1->f[1] - arg0->f[1];
    spock.f[2] = arg1->f[2] - arg0->f[2];

    kirk.f[0] = -arg2->f[2];
    kirk.f[1] = 0.0f;
    kirk.f[2] = arg2->f[0];

    if (!(((kirk.f[0] * spock.f[0]) + (kirk.f[2] * spock.f[2])) > 0.0f))
    {
        spock.f[0] = arg0->f[0];
        spock.f[1] = arg0->f[1];
        spock.f[2] = arg0->f[2];

        arg0->f[0] = arg1->f[0];
        arg0->f[1] = arg1->f[1];
        arg0->f[2] = arg1->f[2];

        arg1->f[0] = spock.f[0];
        arg1->f[1] = spock.f[1];
        arg1->f[2] = spock.f[2];
    }
}


/**
 * Very similar to @see sub_GAME_7F030D70 .
 * Address 0x7F03081C.
*/
s32 sub_GAME_7F03081C(ChrRecord *self, coord3d *arg1, StandTile *arg2, coord3d *arg3, coord3d *arg4, coord3d *arg5, f32 arg6, f32 arg7, s32 cdtypes)
{
    StandTile *spAC;
    coord3d spA0;
    f32 sp9C; // 156
    f32 sp98; // 152
    f32 sp94; // 148
    f32 sp90; // 144
    f32 norm;
    s32 sp88; // 136
    s32 sp84; // 132
    coord3d sp78;
    coord3d sp6C;
    coord3d sp60;
    coord3d sp54;
    s32 sp50;
    f32 sp4C;
    f32 sp48;
    f32 sp44;

    sp88 = 0;
    sp84 = 0;
    sp50 = 0;

    chrGetChrWidthHeight(self->prop, &sp44, &sp4C, &sp48);

    spA0.f[0] = arg3->f[0] - arg1->f[0];
    spA0.f[1] = 0.0f;
    spA0.f[2] = arg3->f[2] - arg1->f[2];

    if ((spA0.f[0] == 0.0f) && (spA0.f[2] == 0.0f))
    {
        return 1;
    }

    norm = (spA0.f[0] * spA0.f[0]) + (spA0.f[2] * spA0.f[2]);
    norm = 1.0f / sqrtf(norm);

    spA0.f[0] *= norm;
    spA0.f[2] *= norm;

    sp9C = 0.95f * (arg7 * spA0.f[0]);
    sp98 = 0.95f * (arg7 * spA0.f[2]);

    sp94 = 1.2f * (arg7 * spA0.f[0]);
    sp90 = 1.2f * (arg7 * spA0.f[2]);

    chrSetMoving(self, 0);
    stanResetHits();

    spAC = arg2;

    if ((stanTestLineUnobstructed(
        &spAC,
        arg1->f[0],
        arg1->f[2],
        arg1->f[0] + sp98,
        arg1->f[2] - sp9C,
        cdtypes,
        sp4C,
        sp48,
        0.0f,
        1.0f) == 0)
        || (stanTestLineUnobstructed(
            &spAC,
            arg1->f[0] + sp98,
            arg1->f[2] - sp9C,
            (arg3->f[0] + sp90) + (spA0.f[0] * arg6),
            (arg3->f[2] - sp94) + (spA0.f[2] * arg6),
            cdtypes,
            sp4C,
            sp48,
            0.0f,
            1.0f) == 0))
    {
        sp88 = 1;

        getCollisionEdge_maybe(&sp78, &sp6C);
        chrlvSwapIfDiffArg2Determinate(&sp78, &sp6C, &spA0);
    }

    spAC = arg2;

    if ((stanTestLineUnobstructed(
        &spAC,
        arg1->f[0],
        arg1->f[2],
        arg1->f[0] - sp98,
        arg1->f[2] + sp9C,
        cdtypes,
        sp4C,
        sp48,
        0.0f,
        1.0f) == 0)
        || (stanTestLineUnobstructed(
            &spAC,
            arg1->f[0] - sp98,
            arg1->f[2] + sp9C,
            (arg3->f[0] - sp90) + (spA0.f[0] * arg6),
            (arg3->f[2] + sp94) + (spA0.f[2] * arg6),
            cdtypes,
            sp4C,
            sp48,
            0.0f,
            1.0f) == 0))
    {
        sp84 = 1;

        getCollisionEdge_maybe(&sp60, &sp54);
        chrlvSwapIfDiffArg2Determinate(&sp60, &sp54, &spA0);
    }

    if ((sp88 != 0) && (sp84 != 0))
    {
        chrlvSwapIfDiffArg2Determinate(&sp78, &sp60, &spA0);
        chrlvSwapIfDiffArg2Determinate(&sp6C, &sp54, &spA0);

        arg4->f[0] = sp78.f[0];
        arg4->f[1] = sp78.f[1];
        arg4->f[2] = sp78.f[2];

        arg5->f[0] = sp54.f[0];
        arg5->f[1] = sp54.f[1];
        arg5->f[2] = sp54.f[2];
    }
    else if (sp88 != 0)
    {
        arg4->f[0] = sp78.f[0];
        arg4->f[1] = sp78.f[1];
        arg4->f[2] = sp78.f[2];

        arg5->f[0] = sp6C.f[0];
        arg5->f[1] = sp6C.f[1];
        arg5->f[2] = sp6C.f[2];
    }
    else if (sp84 != 0)
    {
        arg4->f[0] = sp60.f[0];
        arg4->f[1] = sp60.f[1];
        arg4->f[2] = sp60.f[2];

        arg5->f[0] = sp54.f[0];
        arg5->f[1] = sp54.f[1];
        arg5->f[2] = sp54.f[2];
    }
    else
    {
        spAC = arg2;

        if (stanTestLineUnobstructed(&spAC, arg1->f[0], arg1->f[2], arg3->f[0], arg3->f[2], cdtypes, sp4C, sp48, 0.0f, 1.0f)
            && stanTestVolume(&spAC, arg3->f[0], arg3->f[2], arg7, cdtypes, sp4C, sp48) < 0)
        {
            sp50 = 1;
        }
        else
        {
            getCollisionEdge_maybe(arg4, arg5);
            chrlvSwapIfDiffArg2Determinate(arg4, arg5, &spA0);
        }
    }

    chrSetMoving(self, 1);

    return sp50;
}


/**
 * Very similar to @see sub_GAME_7F03081C .
 *
 * Address 0x7F030D70.
*/
s32 sub_GAME_7F030D70(ChrRecord *self, coord3d *arg1, StandTile *arg2, coord3d *arg3, coord3d *arg4, coord3d *arg5, f32 arg6, f32 arg7, s32 cdtypes)
{
    StandTile *spAC;
    coord3d spA0;
    f32 sp9C; // 164
    f32 sp98; // 160
    f32 sp94; // 156
    f32 sp90; // 152
    f32 norm;
    s32 sp88; // 144
    s32 sp84; // 140
    coord3d sp78; // x
    coord3d sp6C;
    coord3d sp60;
    coord3d sp54;
    s32 sp50;
    f32 stanval1;
    f32 stanval2;
    f32 sp4C;
    f32 sp48;
    f32 sp44;

    sp88 = 0;
    sp84 = 0;
    sp50 = 0;

    chrGetChrWidthHeight(self->prop, &sp44, &sp4C, &sp48);

    spA0.f[0] = arg3->f[0] - arg1->f[0];
    spA0.f[1] = 0.0f;
    spA0.f[2] = arg3->f[2] - arg1->f[2];

    if ((spA0.f[0] == 0.0f) && (spA0.f[2] == 0.0f))
    {
        return 1;
    }

    norm = (spA0.f[0] * spA0.f[0]) + (spA0.f[2] * spA0.f[2]);
    norm = 1.0f / sqrtf(norm);

    spA0.f[0] *= norm;
    spA0.f[2] *= norm;

    sp9C = 0.95f * (arg7 * spA0.f[0]);
    sp98 = 0.95f * (arg7 * spA0.f[2]);

    sp94 = 1.2f * (arg7 * spA0.f[0]);
    sp90 = 1.2f * (arg7 * spA0.f[2]);

    chrSetMoving(self, 0);
    stanResetHits();

    spAC = arg2;

    if ((stanTestLineUnobstructed(
        &spAC,
        arg1->f[0],
        arg1->f[2],
        arg1->f[0] + sp98,
        arg1->f[2] - sp9C,
        cdtypes,
        sp4C,
        sp48,
        0.0f,
        1.0f) == 0)
        || (stanTestLineUnobstructed(
            &spAC,
            arg1->f[0] + sp98,
            arg1->f[2] - sp9C,
            (arg3->f[0] + sp90) + (spA0.f[0] * arg6),
            (arg3->f[2] - sp94) + (spA0.f[2] * arg6),
            cdtypes,
            sp4C,
            sp48,
            0.0f,
            1.0f) == 0))
    {
        sp88 = 1;

        getCollisionEdge_maybe(&sp78, &sp6C);
        chrlvSwapIfDiffArg2Determinate(&sp78, &sp6C, &spA0);

        stanval1 = stanSavedColl_someMin;
    }

    spAC = arg2;

    if ((stanTestLineUnobstructed(
        &spAC,
        arg1->f[0],
        arg1->f[2],
        arg1->f[0] - sp98,
        arg1->f[2] + sp9C,
        cdtypes,
        sp4C,
        sp48,
        0.0f,
        1.0f) == 0)
        || (stanTestLineUnobstructed(
            &spAC,
            arg1->f[0] - sp98,
            arg1->f[2] + sp9C,
            (arg3->f[0] - sp90) + (spA0.f[0] * arg6),
            (arg3->f[2] + sp94) + (spA0.f[2] * arg6),
            cdtypes,
            sp4C,
            sp48,
            0.0f,
            1.0f) == 0))
    {
        sp84 = 1;

        getCollisionEdge_maybe(&sp60, &sp54);
        chrlvSwapIfDiffArg2Determinate(&sp60, &sp54, &spA0);

        stanval2 = stanSavedColl_someMin;
    }

    if ((sp88 != 0) && (sp84 != 0))
    {
        if (stanval1 < stanval2)
        {
            arg4->f[0] = sp78.f[0];
            arg4->f[1] = sp78.f[1];
            arg4->f[2] = sp78.f[2];

            arg5->f[0] = sp6C.f[0];
            arg5->f[1] = sp6C.f[1];
            arg5->f[2] = sp6C.f[2];
        }
        else
        {
            arg4->f[0] = sp60.f[0];
            arg4->f[1] = sp60.f[1];
            arg4->f[2] = sp60.f[2];

            arg5->f[0] = sp54.f[0];
            arg5->f[1] = sp54.f[1];
            arg5->f[2] = sp54.f[2];
        }
    }
    else if (sp88 != 0)
    {
        arg4->f[0] = sp78.f[0];
        arg4->f[1] = sp78.f[1];
        arg4->f[2] = sp78.f[2];

        arg5->f[0] = sp6C.f[0];
        arg5->f[1] = sp6C.f[1];
        arg5->f[2] = sp6C.f[2];
    }
    else if (sp84 != 0)
    {
        arg4->f[0] = sp60.f[0];
        arg4->f[1] = sp60.f[1];
        arg4->f[2] = sp60.f[2];

        arg5->f[0] = sp54.f[0];
        arg5->f[1] = sp54.f[1];
        arg5->f[2] = sp54.f[2];
    }
    else
    {
        spAC = arg2;

        if (stanTestLineUnobstructed(&spAC, arg1->f[0], arg1->f[2], arg3->f[0], arg3->f[2], cdtypes, sp4C, sp48, 0.0f, 1.0f)
            && stanTestVolume(&spAC, arg3->f[0], arg3->f[2], arg7, cdtypes, sp4C, sp48) < 0)
        {
            sp50 = 1;
        }
        else
        {
            getCollisionEdge_maybe(arg4, arg5);
            chrlvSwapIfDiffArg2Determinate(arg4, arg5, &spA0);
        }
    }

    chrSetMoving(self, 1);

    return sp50;
}


/**
 * Address 0x7F03130C.
*/
s32 sub_GAME_7F03130C(
    ChrRecord *self,
    coord3d *arg1,
    s32 arg2,
    coord3d *arg3,
    f32 arg4,
    s32 arg5,
    coord3d *arg6,
    struct waydata *arg7,
    f32 arg8,
    s32 cdtypes,
    s32 set_copy)
{
    PropRecord *self_prop; // -- 124
    coord3d dd; // -- 112
    coord3d sp64; // -- 100
    f32 norm; // -- 96
    f32 phi_f12; // 92
    coord3d sp50; // 80
    coord3d *sp4C; // 76
    coord3d *sp48; // 72

    self_prop = self->prop;

    if (arg2 != 0)
    {
        sp4C = arg1;
        sp48 = arg3;
    }
    else
    {
        sp4C = arg3;
        sp48 = arg1;
    }

    dd.f[0] = arg1->f[0] - self_prop->pos.f[0];
    dd.f[1] = 0.0f;
    dd.f[2] = arg1->f[2] - self_prop->pos.f[2];

    norm = 1.0f / sqrtf((dd.f[0] * dd.f[0]) + (dd.f[2] * dd.f[2]));

    dd.f[0] *= arg4 * norm;
    dd.f[2] *= arg4 * norm;

    if (arg4 * norm > 1.0f)
    {
        phi_f12 = DegToRad(45);
    }
    else
    {
        phi_f12 = acosf(arg4 * norm);
    }

    if ((arg2 == 0) && (phi_f12 != 0.0f))
    {
        phi_f12 = M_TAU_F - phi_f12;
    }

    sp50.f[0] = (-cosf(phi_f12) * dd.f[0]) + (sinf(phi_f12) * dd.f[2]);
    sp50.f[1] = 0.0f;
    sp50.f[2] = (-sinf(phi_f12) * dd.f[0]) - (cosf(phi_f12) * dd.f[2]);

    sp64.f[0] = arg1->f[0] + sp50.f[0];
    sp64.f[1] = arg1->f[1];
    sp64.f[2] = arg1->f[2] + sp50.f[2];

    if (sub_GAME_7F03081C(self, &self_prop->pos, self_prop->stan, &sp64, sp4C, sp48, arg8, self->chrwidth, cdtypes)
        && ((arg5 == 0) || sub_GAME_7F0304AC(self, &self_prop->pos, self_prop->stan, &sp64, arg6, NULL, cdtypes)))
    {
        if (set_copy != 0)
        {
            arg7->unk03 = 1;

            arg7->pos_copy.f[0] = sp64.f[0];
            arg7->pos_copy.f[1] = sp64.f[1];
            arg7->pos_copy.f[2] = sp64.f[2];
        }
        else
        {
            arg7->unk02 = 1;

            arg7->pos.f[0] = sp64.f[0];
            arg7->pos.f[1] = sp64.f[1];
            arg7->pos.f[2] = sp64.f[2];
        }

        return 1;
    }

    return 0;
}


/**
 * Iterates travel mode. Used by both ACT_GOPOS and ACT_PATROL.
 * 10% chance to open door.
 * Calls apply speed.
 *
 * @see chrlvTickPatrol
 * @see chrlvTickGoPos
 * contrast with @see chrlvTravelTickMagic
 *
 * Address 0x7F0315A4.
*/
void chrlvTravelTick(ChrRecord *self, coord3d *arg1, StandTile *arg2, struct waydata *arg3)
{
    s32 spF0;
    coord3d sp100; // 260
    coord3d spF4; // 244
    s32 i; // 240
    f32 spe0;
    f32 spE8; // 232
    f32 spE4; // 228
    struct ObjectRecord *obj;
    f32 spC4;
    f32 spB4;
    f32 atan_pos2_a; // 212
    f32 atan_pos3_a;
    PropRecord *self_prop;
    f32 dx;
    f32 atan_pos2_b; // 196
    f32 atan_pos3_b;
    f32 dy;
    f32 dz;
    f32 atan_pos2_c; // 180
    f32 atan_pos3_c;
    s32 max;
    f32 atan_pos;
    s32 temp_t1;
    s32 cdtypes;
    PropRecord *phi_s3;
    s32 stack_01;
    s32 stack_02;

    self_prop = self->prop;
    cdtypes = CDTYPE_OBJS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_DOORSLOCKEDTOAI;
    if ((self->hidden & CHRHIDDEN_OFFSCREEN_PATROL) != 0)
    {
        cdtypes = CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER;
    }

    max=1;
    for (i=0; i<1; i++)
    {
        if ((arg3->mode == WAYMODE_0) || (arg3->mode == WAYMODE_2))
        {
            sp100.f[0] = arg1->f[0];
            sp100.f[1] = arg1->f[1];
            sp100.f[2] = arg1->f[2];

            if (sub_GAME_7F03081C(self, &self_prop->pos, self_prop->stan, &sp100, &arg3->pos2, &arg3->pos3, -(self->chrwidth), self->chrwidth, CDTYPE_PATHBLOCKER) != 0)
            {
                arg3->unk02 = (u8) 1;
                arg3->pos.f[0] = sp100.f[0];
                arg3->pos.f[1] = sp100.f[1];
                arg3->pos.f[2] = sp100.f[2];
                arg3->mode = WAYMODE_4;
            }
            else
            {
                if (arg3->mode == WAYMODE_0)
                {
                    arg3->mode = (s8) WAYMODE_1;
                    arg3->unk01 = 0;
                }
                else if (arg3->mode == WAYMODE_2)
                {
                    arg3->mode = WAYMODE_3;
                    arg3->unk01 = 0;
                }
            }
        }
        else if (arg3->mode == WAYMODE_1)
        {
            spE8 = self->chrwidth * 1.2f * 1.05f;

            if (sub_GAME_7F03130C(self, &arg3->pos2, 1, &spF4, spE8, 1, arg1, arg3, 0.0f, 0x10, 0) != 0)
            {
                arg3->mode = WAYMODE_4;
            }
            else if (sub_GAME_7F03130C(self, &arg3->pos3, 0, &spF4, spE8, 1, arg1, arg3, 0.0f, 0x10, 0) != 0)
            {
                arg3->mode = WAYMODE_4;
            }
            else
            {
                arg3->unk01 = arg3->unk01 + 1;
                if (arg3->unk01 >= MAX_WAYMODE)
                {
                    arg3->mode = WAYMODE_2;
                }
            }
        }
        else if (arg3->mode == WAYMODE_3)
        {
            spE4 = self->chrwidth * 1.2f * 1.05f;

            if (sub_GAME_7F03130C(self, &arg3->pos2, 1, &spF4, spE4, 0, NULL, arg3, 0.0f, 0x10, 0) != 0)
            {
                arg3->mode = WAYMODE_4;
            }
            else if (sub_GAME_7F03130C(self, &arg3->pos3, 0, &spF4, spE4, 0, NULL, arg3, 0.0f, 0x10, 0) != 0)
            {
                arg3->mode = WAYMODE_4;
            }
            else
            {
                arg3->unk01 = arg3->unk01 + 1;
                if (arg3->unk01 >= MAX_WAYMODE)
                {
                    arg3->unk02 = 0;
                    arg3->unk03 = (u8) (s8) arg3->unk02;
                    arg3->pos_copy.f[0] =
                        arg3->pos.f[0] = arg1->f[0];
                    arg3->pos_copy.f[1] =
                        arg3->pos.f[1] = arg1->f[1];
                    arg3->pos_copy.f[2] =
                        arg3->pos.f[2] = arg1->f[2];
                    arg3->mode = WAYMODE_0;
                }
            }
        }
        else if (arg3->mode == WAYMODE_4)
        {
            if (sub_GAME_7F030D70(self, &self_prop->pos, self_prop->stan, &arg3->pos, &arg3->pos2, &arg3->pos3, -(self->chrwidth), self->chrwidth, cdtypes) != 0)
            {
                arg3->unk03 = max;
                arg3->mode = WAYMODE_0;
                arg3->pos_copy.f[0] = arg3->pos.f[0];
                arg3->pos_copy.f[1] = arg3->pos.f[1];
                arg3->pos_copy.f[2] = arg3->pos.f[2];
            }
            else
            {
                arg3->mode = WAYMODE_5;
                arg3->unk01 = 0;
            }
        }
        else if (arg3->mode == WAYMODE_5)
        {
            spe0 = self->chrwidth * 1.2f * 1.05f;

            atan_pos =       atan2f( arg3->pos.f[0] - self_prop->pos.f[0],  arg3->pos.f[2] - self_prop->pos.f[2]);
            atan_pos2_a = atan_pos - atan2f(arg3->pos2.f[0] - self_prop->pos.f[0], arg3->pos2.f[2] - self_prop->pos.f[2]);
            atan_pos3_a = atan_pos - atan2f(arg3->pos3.f[0] - self_prop->pos.f[0], arg3->pos3.f[2] - self_prop->pos.f[2]);

            if (atan_pos2_a < 0.0f)
            {
                atan_pos2_a = atan_pos2_a + M_TAU_F;
            }

            if (atan_pos2_a >= M_PI_F)
            {
                atan_pos2_a = atan_pos2_a - M_TAU_F;
            }

            if (atan_pos2_a < 0.0f)
            {
                atan_pos2_a = -atan_pos2_a;
            }

            if (atan_pos3_a < 0.0f)
            {
                atan_pos3_a = atan_pos3_a + M_TAU_F;
            }

            if (atan_pos3_a >= M_PI_F)
            {
                atan_pos3_a = atan_pos3_a - M_TAU_F;
            }

            if (atan_pos3_a < 0.0f)
            {
                atan_pos3_a = -atan_pos3_a;
            }

            if (atan_pos2_a < atan_pos3_a)
            {
                if (sub_GAME_7F03130C(self, &arg3->pos2, 1, &spF4, spe0, 0, NULL, arg3, spe0 * 1.1f, cdtypes, 1) != 0)
                {
                    arg3->mode = WAYMODE_0;
                    break;
                }
                else
                {
                    atan_pos2_b = atan_pos - atan2f(arg3->pos2.f[0] - self_prop->pos.f[0], arg3->pos2.f[2] - self_prop->pos.f[2]);
                    atan_pos3_b = atan_pos - atan2f(spF4.f[0] - self_prop->pos.f[0], spF4.f[2] - self_prop->pos.f[2]);

                    if (atan_pos2_b < 0.0f)
                    {
                        atan_pos2_b = atan_pos2_b + M_TAU_F;
                    }
                    if (atan_pos2_b >= M_PI_F)
                    {
                        atan_pos2_b = atan_pos2_b - M_TAU_F;
                    }
                    if (atan_pos2_b < 0.0f)
                    {
                        atan_pos2_b = -atan_pos2_b;
                    }

                    if (atan_pos3_b < 0.0f)
                    {
                        atan_pos3_b = atan_pos3_b + M_TAU_F;
                    }
                    if (atan_pos3_b >= M_PI_F)
                    {
                        atan_pos3_b = atan_pos3_b - M_TAU_F;
                    }
                    if (atan_pos3_b < 0.0f)
                    {
                        atan_pos3_b = -atan_pos3_b;
                    }

                    if ((atan_pos3_b < atan_pos2_b) && (sub_GAME_7F03130C(self, &spF4, 0, &spF4, spe0, 0, NULL, arg3, spe0 * 1.1f, cdtypes, 1) != 0))
                    {
                        arg3->mode = WAYMODE_0;
                        break;
                    }
                }
            }
            else
            {
                if (sub_GAME_7F03130C(self,  &arg3->pos3, 0, &spF4, spe0, 0, NULL, arg3, spe0 * 1.1f, cdtypes, 1) != 0)
                {
                    arg3->mode = WAYMODE_0;
                    break;
                }
                else
                {
                    atan_pos2_c = atan_pos - atan2f(arg3->pos3.f[0] - self_prop->pos.f[0], arg3->pos3.f[2] - self_prop->pos.f[2]);
                    atan_pos3_c = atan_pos - atan2f(spF4.f[0] - self_prop->pos.f[0], spF4.f[2] - self_prop->pos.f[2]);

                    if (atan_pos2_c < 0.0f)
                    {
                        atan_pos2_c = atan_pos2_c + M_TAU_F;
                    }
                    if (atan_pos2_c >= M_PI_F)
                    {
                        atan_pos2_c = atan_pos2_c - M_TAU_F;
                    }
                    if (atan_pos2_c < 0.0f)
                    {
                        atan_pos2_c = -atan_pos2_c;
                    }

                    if (atan_pos3_c < 0.0f)
                    {
                        atan_pos3_c = atan_pos3_c + M_TAU_F;
                    }
                    if (atan_pos3_c >= M_PI_F)
                    {
                        atan_pos3_c = atan_pos3_c - M_TAU_F;
                    }
                    if (atan_pos3_c < 0.0f)
                    {
                        atan_pos3_c = -atan_pos3_c;
                    }

                    if ((atan_pos3_c < atan_pos2_c) && (sub_GAME_7F03130C(self, &spF4, 1, &spF4, spe0, 0, NULL, arg3, spe0 * 1.1f, cdtypes, 1) != 0))
                    {
                        arg3->mode = WAYMODE_0;
                        break;
                    }
                }
            }

            arg3->unk01 = arg3->unk01 + 1;
            if (arg3->unk01 >= MAX_WAYMODE)
            {
                arg3->unk03 = 0;
                arg3->mode = WAYMODE_0;
            }
        }
    }

    if (arg3->unk03 == 0)
    {
        arg3->pos_copy.f[0] = arg3->pos.f[0];
        arg3->pos_copy.f[1] = arg3->pos.f[1];
        arg3->pos_copy.f[2] = arg3->pos.f[2];
    }

    if (((s32) arg3->age % 10) == 0)
    {
        phi_s3 = sub_GAME_7F0B1410(self_prop->stan, self_prop->pos.f[0], self_prop->pos.f[2], arg3->pos_copy.f[0], arg3->pos_copy.f[2], 0x5000);

        if (phi_s3 != NULL)
        {
            obj = phi_s3->obj;
            if (!(obj->flags2 & PROPFLAG_DOOR_OPENTOFRONT))
            {
                dx = phi_s3->pos.f[0] - self_prop->pos.f[0];
                dy = phi_s3->pos.f[1] - self_prop->pos.f[1];
                dz = phi_s3->pos.f[2] - self_prop->pos.f[2];

                if (((dx * dx) + (dy * dy) + (dz  * dz )) < 40000.0f)
                {
                    sub_GAME_7F0281F4(self);
                    doorsChooseSwingDirection(self_prop, phi_s3->door);
                    doorActivate(phi_s3->door, 1);

                    if (((self->hidden & CHRHIDDEN_OFFSCREEN_PATROL) == 0)
                        && (objecthandlerGetModelAnim(self->model) != portAnimationFromOffset(PTR_ANIM_idle_unarmed))
                        && (objecthandlerGetModelAnim(self->model) != portAnimationFromOffset(PTR_ANIM_idle)))
                    {
                        chrlvIdleAnimationRelated(self, 16.0f);
                        self->lastmoveok60 = g_GlobalTimer;
                    }
                }
                else
                {
                    phi_s3 = NULL;
                }
            }
            else
            {
                phi_s3 = NULL;
            }
        }

        if ((phi_s3 == NULL) || ((self->hidden & CHRHIDDEN_OFFSCREEN_PATROL) != 0))
        {
            if ((objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_idle_unarmed))
                || (objecthandlerGetModelAnim(self->model) == portAnimationFromOffset(PTR_ANIM_idle)))
            {
                if (self->actiontype == ACT_PATROL)
                {
                    chrlvWalkingAnimationRelated(self);
                }
                else
                {
                    play_hit_soundeffect_and_proper_volume(self);
                }
            }

            if (phi_s3 == NULL)
            {
                self->hidden &= ~(CHRHIDDEN_OFFSCREEN_PATROL);
            }
        }
    }

    if (self->actiontype == ACT_PATROL)
    {
        chrlvApplySpeed(self, &arg3->pos_copy, 0, &self->act_patrol.speed);
    }
    else
    {
        chrlvApplySpeed(self, &arg3->pos_copy, (s32) self->act_gopos.unk59, &self->act_gopos.speed);

        if (self->act_gopos.unk59 == 2)
        {
            if (self->act_gopos.speed != 0.0f)
            {
                modelSetAnimSpeed(self->model, 0.25f, 0.0f);
            }
            else if (self->chrflags & CHRFLAG_INCREASE_RUNNING_SPEED)
            {
                modelSetAnimSpeed(self->model, 0.65f, 0.0f);
            }
            else
            {
                modelSetAnimSpeed(self->model, 0.5f, 0.0f);
            }
        }
        else if (self->act_gopos.unk59 == 1)
        {
            if (self->act_gopos.speed != 0.0f)
            {
                modelSetAnimSpeed(self->model, 0.4f, 0.0f);
            }
            else
            {
                modelSetAnimSpeed(self->model, 0.5f, 0.0f);
            }
        }
    }
}


/**
 * Address 0x7F032088.
*/
void chrlvTickGoPos(ChrRecord *self)
{
    waypoint *wp;
    coord3d *wp_pos;
    StandTile *wp_stan;
    PropRecord *self_prop;
    s32 sp74;
    coord3d sp68;
    StandTile *sp64;
    coord3d sp58;
    StandTile *sp54;
    PadRecord *pad; // 80
    s32 phi_v1; // 76
    s32 unused[4]; // maybe used by the nested if statements ?

    self_prop = self->prop;
    sp74 = 0;
    self->act_gopos.waydata.age += 1;
    self->lastwalk60 = g_GlobalTimer;

    if (self->lastmoveok60 < (g_GlobalTimer - CHRLV_LASTMOVEOK60_CHECK))
    {
#ifndef TARGET_N64
        portAiDbgReplot(self);
#endif
        plot_course_for_actor(self, &self->act_gopos.targetpos, self->act_gopos.target, (s32) self->act_gopos.unk59);
    }

    chrlvPlotCourseRelated(self);

    if ((self->act_gopos.waydata.mode != WAYMODE_MAGIC) && ((self->act_gopos.unk9c + CHRLV_DEFAULT_TIMER) < g_GlobalTimer))
    {
        chrlvActGoposRelated(self, &sp68, &sp64);

        if (chrlvStanRoomRelated(self, &sp68, sp64))
        {
            sp74 = 1;
            chrlvSetGoposSegDistTotal(self, &self->act_gopos.waydata, &sp68);
        }
    }

    if (g_SeenBondRecentlyGuardCount >= 0xA)
    {
        chrlvKneelingAnimationRelated7F023E48(self);

        return;
    }

    if (self->act_gopos.waydata.mode == WAYMODE_MAGIC)
    {
        chrlvActGoposRelated(self, &sp58, &sp54);

        if ((sp74 == 0)
            && ((self_prop->flags & PROPFLAG_ONSCREEN) || (chrlvStanRoomRelated(self, &sp58, sp54) == 0)))
        {
            chrlvActGoposSetTargetPosRelated(self);
            self->act_gopos.unk9c = g_GlobalTimer;

            return;
        }

        chrlvTravelTickMagic(self, &self->act_gopos.waydata, chrlvModelScaleAnimationRelated(self), &sp58, sp54);

        return;
    }


    phi_v1 = 0;

    wp = self->act_gopos.waypoints[self->act_gopos.curindex];

    if (wp != NULL)
    {
        if (chrlvIsArrivingLaterallyAtPos(CHRLV_ARRIVE_FROM(self), &self_prop->pos, &g_CurrentSetup.pads[wp->padID].pos, 30.0f) != 0)
        {
#ifndef TARGET_N64
            portAiDbgArrive(self, "gopos-wp", &g_CurrentSetup.pads[wp->padID].pos);
#endif
            phi_v1 = 1;
        }
#ifndef TARGET_N64
        portAiDbgCrumb(self, &g_CurrentSetup.pads[wp->padID].pos, "gopos");
#endif
    }
    else
    {
        if (chrlvIsArrivingLaterallyAtPos(CHRLV_ARRIVE_FROM(self), &self_prop->pos, &self->act_gopos.targetpos, 30.0f) != 0)
        {
#ifndef TARGET_N64
            portAiDbgArrive(self, "gopos-tgt", &self->act_gopos.targetpos);
#endif
            chrlvKneelingAnimationRelated7F023E48(self);

            return;
        }
    }

    if (phi_v1 != 0)
    {
        chrlvActGoposIncCurIndex(self);
    }

    if (((s32) self->act_gopos.waydata.age % 10) == 5)
    {
        wp = self->act_gopos.waypoints[self->act_gopos.curindex];

        if (wp != NULL)
        {
            wp = self->act_gopos.waypoints[self->act_gopos.curindex + 1];

            if (wp != NULL)
            {
                wp = self->act_gopos.waypoints[self->act_gopos.curindex + 2];

                if (wp != NULL)
                {
                    pad = &g_CurrentSetup.pads[wp->padID];
                    wp_pos = &pad->pos;
                    wp_stan = pad->stan;
                }
                else
                {
                    wp_pos = &self->act_gopos.targetpos;
                    wp_stan = self->act_gopos.target;
                }

                if (sub_GAME_7F030128(self, &self_prop->pos, self_prop->stan, wp_pos, wp_stan, CDTYPE_PATHBLOCKER)
                    && sub_GAME_7F0301FC(self, &self_prop->pos, self_prop->stan, wp_pos, self->chrwidth * 1.2f, CDTYPE_PATHBLOCKER))
                {
                    chrlvActGoposIncCurIndex(self);
                    chrlvActGoposIncCurIndex(self);
                }
            }
        }
    }

    if (((s32) self->act_gopos.waydata.age % 10) == 0)
    {
        wp = self->act_gopos.waypoints[self->act_gopos.curindex];

        if (wp != NULL)
        {
            wp = self->act_gopos.waypoints[self->act_gopos.curindex + 1];

            if (wp != NULL)
            {
                pad = &g_CurrentSetup.pads[wp->padID];
                wp_pos = &pad->pos;
                wp_stan = pad->stan;
            }
            else
            {
                wp_pos = &self->act_gopos.targetpos;
                wp_stan = self->act_gopos.target;
            }

            if (sub_GAME_7F030128(self, &self_prop->pos, self_prop->stan, wp_pos, wp_stan, CDTYPE_PATHBLOCKER)
                && sub_GAME_7F0301FC(self, &self_prop->pos, self_prop->stan, wp_pos, self->chrwidth * 1.2f, CDTYPE_PATHBLOCKER))
            {
                chrlvActGoposIncCurIndex(self);
            }
        }
    }

    wp = self->act_gopos.waypoints[self->act_gopos.curindex];

    if (wp != NULL)
    {
        pad = &g_CurrentSetup.pads[wp->padID];
        wp_pos = &pad->pos;
        wp_stan = pad->stan;
    }
    else
    {
        wp_pos = &self->act_gopos.targetpos;
        wp_stan = self->act_gopos.target;
    }

    chrlvTravelTick(self, wp_pos, wp_stan, &self->act_gopos.waydata);
}


/**
 * Address 0x7F032548.
*/
void chrlvTickPatrol(ChrRecord *self)
{
    PropRecord *self_prop;
    s32 unused_1;
    s32 sp34;
    PadRecord *temp_v0;

    self_prop = self->prop;
    temp_v0 = (PadRecord *) chrlvGetNextPatrolStepPad(self);
    sp34 = 0;
    self->act_patrol.waydata.age += 1;
    self->lastwalk60 = g_GlobalTimer;

    if ((self->act_patrol.waydata.mode != WAYMODE_MAGIC)
        && ((self->act_patrol.lastvisible60 + CHRLV_DEFAULT_TIMER) < g_GlobalTimer)
        && chrlvStanRoomRelatedPad(self, temp_v0))
    {
        sp34 = 1;
        chrlvSetGoposSegDistTotal(self, &self->act_patrol.waydata, &temp_v0->pos);
    }

    if (self->act_patrol.waydata.mode == WAYMODE_MAGIC)
    {
        if ((sp34 == 0)
            && ((self_prop->flags & PROPFLAG_ONSCREEN) || (chrlvStanRoomRelatedPad(self, temp_v0) == 0)))
        {
            self->act_patrol.lastvisible60 = g_GlobalTimer;
            chrlvSetNextActPatrolStepPadPos(self);
        }
        else
        {
            chrlvTravelTickMagic(self, &self->act_patrol.waydata, D_80030984, &temp_v0->pos, temp_v0->stan);
        }
    }
    else
    {
        if(1)
        {
            // removed
        }

        if (chrlvIsArrivingLaterallyAtPos(CHRLV_ARRIVE_FROM(self), &self_prop->pos, &temp_v0->pos, 30.0f))
        {
#ifndef TARGET_N64
            portAiDbgArrive(self, "patrol", &temp_v0->pos);
#endif
            chrlvAdvancePatrolStep(self);
            temp_v0 = (PadRecord *)chrlvGetNextPatrolStepPad(self);
        }

#ifndef TARGET_N64
        portAiDbgCrumb(self, &temp_v0->pos, "walk");
#endif
        chrlvTravelTick(self, &temp_v0->pos, temp_v0->stan, &self->act_patrol.waydata);
    }
}


#ifndef TARGET_N64
/* PORT: AI cadence governor.
 *
 * The PC ticks the sim every frame (~60/s) while the N64 ticked every 2+ vsyncs
 * (~25-30/s). AI sleeps are already time-scaled, but zero-sleep loops re-enter
 * the ai() script interpreter every tick, so per-tick random rolls (spawn
 * scripts, guard decisions) fired about twice as often. This runs ai() only
 * once 2+ vsync units have built up per chr, the console's cadence, while
 * movement/animation keep running every frame so guards stay smooth.
 *
 * Demos already tick in 2+ unit steps, so replay is never deferred. The
 * accumulators live in a side table because ChrRecord's layout must not grow.
 * GE_NOAIGOV=1 restores per-tick ai(). */
#define PORT_AIGOV_SLOTS 512
static u8 portAiGovAcc[PORT_AIGOV_SLOTS];
static s32 portAiGovOff = -1;

static int portAiGovDefer(ChrRecord *self)
{
    s32 slot;

    if (portAiGovOff < 0)
    {
        extern char *getenv(const char *);
        portAiGovOff = getenv("GE_NOAIGOV") != NULL;
    }
    if (portAiGovOff)
    {
        return 0;
    }
    {
        /* Demo replay MUST run vanilla per-tick ai(): it must match the N64
         * recordings, and some recorded ticks have g_ClockTimer < 2. Live play only. */
        extern s32 is_ramrom_flag;          /* live "replaying now" state */
        extern s32 g_ramromPlayBackFlag;    /* demo launch pending */
        if (is_ramrom_flag != 0 || g_ramromPlayBackFlag != 0)
        {
            return 0;
        }
    }
    if (self->chrflags & CHRFLAG_00040000)
    {
        return 0;           /* force-AI flag bypasses, as vanilla */
    }
    /* Keep the existing background-actor policy. Use full-width integer
     * addresses for membership: subtracting unrelated pointers is undefined,
     * and narrowing the difference can alias another allocation above 4 GiB. */
    if (g_ActiveChrs == NULL)
    {
        return 0;
    }
    {
        uintptr_t d = (uintptr_t) self - (uintptr_t) g_ActiveChrs;
        if (d >= (uintptr_t) g_ActiveChrsCount * sizeof(ChrRecord)
            || d % sizeof(ChrRecord) != 0)
        {
            return 0;       /* not a g_ActiveChrs element: never defer */
        }
        slot = (s32) (d / sizeof(ChrRecord));
    }
    if (slot >= g_ActiveChrsCount || (u32) slot >= PORT_AIGOV_SLOTS)
    {
        return 0;           /* out-of-table chr: never defer */
    }
    if (portAiGovAcc[slot] >= 200)
    {
        portAiGovAcc[slot] = 200;   /* stale-slot clamp */
    }
    portAiGovAcc[slot] += (u8) g_ClockTimer;
    if (portAiGovAcc[slot] < 2)
    {
        return 1;           /* defer ai() to next frame */
    }
    portAiGovAcc[slot] = 0;
    return 0;
}

/* PORT: anchor for the guard node-arrival test.
 *
 * chrlvIsArrivingLaterallyAtPos tests the segment swept since chr->prevpos, but
 * prevpos updates every sim tick while the test runs once per field. At raised
 * tick rates the segment shrinks to a tiny sliver, arrival rarely fires, and
 * guards overshoot their path nodes and drift off their routes.
 *
 * Fix: remember each chr's position at its previous action tick in a side table
 * (ChrRecord must not grow; indexed like portAiGovDefer) and use that as the
 * segment start. At tickhz 60, and in demo replay, this equals prevpos, so
 * behaviour is unchanged; stale slots fall back to prevpos. */
#define PORT_ARRIVE_SLOTS 512
static coord3d portChrArrivePos[PORT_ARRIVE_SLOTS];
static u32 portChrArriveStamp[PORT_ARRIVE_SLOTS]; /* g_GlobalTimer + 1 at store; 0 = empty */

static s32 portChrArriveSlot(ChrRecord *self)
{
    u32 d;
    s32 slot;

    if (g_ActiveChrs == NULL)
    {
        return -1;
    }
    d = (u32) ((u8 *) self - (u8 *) g_ActiveChrs);
    if (d % sizeof(ChrRecord) != 0)
    {
        return -1;
    }
    slot = d / sizeof(ChrRecord);
    if (slot >= g_ActiveChrsCount || (u32) slot >= PORT_ARRIVE_SLOTS)
    {
        return -1;
    }
    return slot;
}

coord3d *portChrArriveFrom(ChrRecord *self)
{
    s32 slot = portChrArriveSlot(self);

    if (slot < 0 || portChrArriveStamp[slot] == 0
        || (u32) (g_GlobalTimer + 1u - portChrArriveStamp[slot]) > 8u)
    {
        return &self->prevpos; /* no fresh action-tick anchor: vanilla segment */
    }
    return &portChrArrivePos[slot];
}

static void portChrArriveStore(ChrRecord *self)
{
    s32 slot;

    if (self->prop == NULL) /* empty/dead slot - chrlvAllChrTick walks those too */
    {
        return;
    }
    slot = portChrArriveSlot(self);
    if (slot >= 0)
    {
        portChrArrivePos[slot] = self->prop->pos;
        portChrArriveStamp[slot] = (u32) g_GlobalTimer + 1u;
    }
}
#endif

/**
 * Address 0x7F0326BC.
*/
void chrlvActionTick(ChrRecord *self)
{
    if (g_ClockTimer > 0)
    {
        if (self->actiontype == ACT_INIT)
        {
            self->chrflags |= CHRFLAG_INIT;
            chrlvMergeKneelToStand(self, 0.0f);
            self->sleep = 0;
        }

        if ((self->hidden & CHRHIDDEN_TIMER_ACTIVE) != 0)
        {
            self->timer60 += g_ClockTimer;
        }

        self->sleep -= g_ClockTimer;

        if (((s32)self->sleep < 0) || (self->chrflags & CHRFLAG_00040000))
        {
            self->sleep = 0;
#ifndef TARGET_N64
            if (!portAiGovDefer(self))
#endif
            ai(self, PROP_TYPE_CHR);

            switch (self->actiontype)
            {
                case ACT_STAND:
                    chrlvTickStand(self);
                    break;
                case ACT_KNEEL:
                    chrlvTickKneel(self);
                    break;
                case ACT_ANIM:
                    chrlvTickAnim(self);
                    break;
                case ACT_DIE:
                    chrlvTickDie(self);
                    break;
                case ACT_ARGH:
                    chrlvTickArgh(self);
                    break;
                case ACT_PREARGH:
                    chrlvTickPreArgh(self);
                    break;
                case ACT_SIDESTEP:
                    chrlvTickSidestep(self);
                    break;
                case ACT_JUMPOUT:
                    chrlvTickJumpout(self);
                    break;
                case ACT_DEAD:
                    chrlvTickDead(self);
                    break;
                case ACT_ATTACK:
                    chrlvTickAttack(self);
                    break;
                case ACT_ATTACKWALK:
                    chrlvTickAttackWalk(self);
                    break;
                case ACT_ATTACKROLL:
                    chrlvTickAttackRoll(self);
                    break;
                case ACT_RUNPOS:
                    chrlvTickRunPos(self);
                    break;
                case ACT_PATROL:
                    chrlvTickPatrol(self);
                    break;
                case ACT_GOPOS:
                    chrlvTickGoPos(self);
                    break;
                case ACT_SURRENDER:
                    chrlvTickSurrender(self);
                    break;
                case ACT_TEST:
                    chrlvTickTest(self);
                    break;
                case ACT_SURPRISED:
                    chrlvTickSurprised(self);
                    break;
                case ACT_STARTALARM:
                    chrlvTickStartAlarm(self);
                    break;
                case ACT_THROWGRENADE:
                    chrlvTickThrowGrenade(self);
                    break;
                case ACT_BONDINTRO:
                    chrlvTickBondIntro(self);
                    break;
                case ACT_BONDDIE:
                    chrlvTickBondDieRemoved(self);
                    break;
            }

            self->chrflags &= ~CHRFLAG_NEAR_MISS;
            self->hidden &= ~(CHRHIDDEN_ALERT_GUARD_RELATED | CHRHIDDEN_BACKGROUND_AI);
            self->chrseeshot = CHR_FREE;
            self->chrseedie  = CHR_FREE;
        }

#ifndef TARGET_N64
        /* AFTER the handlers consumed the previous anchor: this action
         * tick's position becomes the next one's arrival-segment start
         * (see portChrArriveFrom). Runs for sleeping chrs too, so the
         * anchor stays fresh across sleep gaps. */
        portChrArriveStore(self);
#endif
    }
}


/**
 * Calls chrlvActionTick on all characters, and updates count of guards that have recently seen bond.
 *
 * Address 0x7F03291C.
*/
#ifndef TARGET_N64
/* GE_CHRDUMP=1: every 5 seconds, log one line per live chr with its chrnum,
 * body/head, position, action type and time since it last saw its target.
 * Useful for finding where a chr is and whether it exists yet. */
static void portChrDump(void)
{
    extern char *getenv(const char *);
    extern int printf(const char *, ...);
    static s32 dump = -1;
    static s32 tick;
    s32 i;

    if (dump < 0)
    {
        dump = getenv("GE_CHRDUMP") != NULL;
    }
    if (!dump)
    {
        return;
    }
    if ((tick++ % 300) != 0)
    {
        return;
    }
    /* g_ActiveChrs is the BACKGROUND AI-list array (deb_loadallmodels builds it
     * from ailists >= 0x1000, and it is NULL in a normal mission) - the real
     * chrs are the guard slots, which is what the loop below this one walks. */
    { /* Bond's own tile state, same cadence - chrCanSeeBond compares a chr's
       * walk end tile against THIS pointer, so if it does not track him the
       * whole AI goes blind no matter how healthy the rays are. */
        PropRecord *bp = getCurrentPlayerProp();

        if (bp != NULL)
        {
            printf("ge: bond: t=%d wall=%.0f pos=(%.0f,%.0f,%.0f) prop_stan=%p tile=%p tile_portals=%p\n",
                   g_GlobalTimer, (f32) osGetCount() / 46875.0f,
                   bp->pos.f[0], bp->pos.f[1], bp->pos.f[2], (void *) bp->stan,
                   (void *) g_CurrentPlayer->field_488.current_tile_ptr,
                   (void *) g_CurrentPlayer->field_488.current_tile_ptr_for_portals);
        }
    }

    for (i = 0; i < get_numguards(); i++)
    {
        ChrRecord *c = &g_ChrSlots[i];

        if (c->prop == NULL || c->model == NULL)
        {
            continue;
        }
        printf("ge: chr %2d num=%4d body=%3d head=%3d act=%2d pos=(%.0f,%.0f,%.0f) "
               "sawtarget=%d ailist=%p aioff=%d flags=%08x onscr=%d mode=%d step=%d lastvis=%d\n",
               i, (s32) c->chrnum, (s32) c->bodynum, (s32) c->headnum,
               (s32) c->actiontype,
               c->prop->pos.f[0], c->prop->pos.f[1], c->prop->pos.f[2],
               chrSawTargetRecently(c), (void *) c->ailist, (s32) c->aioffset,
               (u32) c->chrflags,
               (c->prop->flags & PROPFLAG_ONSCREEN) != 0,
               /* travel mode (WAYMODE_MAGIC = offscreen pad hopping) and the
                * patrol step / last-visible stamp, for phase questions like
                * "why is this guard in that room on the port and not on N64" */
               (c->actiontype == ACT_PATROL) ? (s32) c->act_patrol.waydata.mode
               : (c->actiontype == ACT_GOPOS) ? (s32) c->act_gopos.waydata.mode : -1,
               (c->actiontype == ACT_PATROL) ? (s32) c->act_patrol.nextstep : -1,
               (c->actiontype == ACT_PATROL) ? (s32) c->act_patrol.lastvisible60
               : (c->actiontype == ACT_GOPOS) ? (s32) c->act_gopos.unk9c : -1);
               
               /* and the command byte the script is parked on: (AiListp + Offset)->cmd
                * is exactly what the interpreter fetches (chrai.c:910), so this names
                * the blocking AI command without decoding the whole list. */
               if (c->ailist != NULL)
               {
                   const u8 *ai = (const u8 *) (c->ailist + c->aioffset);
                   printf("ge: chr %2d aicmd=0x%02x args=%02x %02x %02x %02x\n",
                          i, ai[0], ai[1], ai[2], ai[3], ai[4]);
               }
    }
}
#endif

void chrlvAllChrTick(void)
{
    s32 i;
    s32 max;
    ChrRecord *guard;

    max = get_numguards();

    for (i=0; i<g_ActiveChrsCount; i++)
    {
        chrlvActionTick(&g_ActiveChrs[i]);
    }

#ifndef TARGET_N64
    portChrDump();
#endif

    g_SeenBondRecentlyGuardCount = 0;

    for (i=0; i<max; i++)
    {
        guard = &g_ChrSlots[i];

        if (guard->model != NULL)
        {
            if ((guard->lastseetarget60 > 0) && (g_GlobalTimer - guard->lastseetarget60 < CHRLV_SEEN_RECENT_CHECK))
            {
                g_SeenBondRecentlyGuardCount++;
            }
        }
    }
}


/**
 * Address 0x7F032B68.
*/
s32 chrSawTargetRecently(ChrRecord *self)
{
    if ((self->lastseetarget60 > 0) && ((g_GlobalTimer - self->lastseetarget60) < CHRLV_10_SEC_TIMER))
    {
        return TRUE;
    }
    return FALSE;
}


/**
 * Address 0x7F032BA0.
*/
s32 chrHeardTargetRecently(ChrRecord *self)
{
    if ((self->lastheartarget60 > 0) && ((g_GlobalTimer - self->lastheartarget60) < CHRLV_10_SEC_TIMER))
    {
        return TRUE;
    }
    return FALSE;
}


/**
 * Address 0x7F032BD8.
 * get angle to pos in Radians
*/
f32 get_distance_actor_to_position(ChrRecord *self, coord3d *pos)
{
    f32         radToPos;
    f32         radMyHeading;
    PropRecord *myprop;
    f32         anglebetween;

    radMyHeading = getsubroty(self->model);
    myprop       = self->prop;
    anglebetween = atan2f(pos->x - myprop->pos.x, pos->z - myprop->pos.z);
    radToPos     = anglebetween - radMyHeading;

    if (anglebetween < radMyHeading)
    {
        radToPos = radToPos + M_TAU_F;
    }

    return radToPos;
}


/**
 * Address 0x7F032C4C.
*/
f32 chrGetAngleToBond(ChrRecord *self)
{
    return get_distance_actor_to_position(self, &getCurrentPlayerProp()->pos);
}


/**
 * @param self:
 * @param flags: Lookup mode. 4 == lookup guard. 8 == lookup pad preset. Else, use current player.
 * @param lookup_id: Lookup id for guard or preset.
 * @param stan: Out parameter. Will contain found stan.
 * @returns: Position of found item (depends on lookup mode).
 *
 * Address 0x7F032C78.
*/
coord3d *chrlvGetChrOrPresetLocation(ChrRecord *self, s32 flags, s32 lookup_id, StandTile **stan)
{
    ChrRecord *guard;
    PropRecord *player_prop;
    s32 padid;
    PadRecord *preset_pad;

    if ((flags & 4) != 0)
    {
        guard = chrFindById(self, lookup_id);

        if ((guard == 0) || (guard->prop == 0))
        {
            guard = self;
        }

        *stan = (StandTile *) self->prop->stan;

        return &guard->prop->pos;
    }

    if ((flags & 8) != 0)
    {
        padid = chrResolvePadId(self, lookup_id);

        if (isNotBoundPad(padid))
        {
            preset_pad = &g_CurrentSetup.pads[padid];
        }
        else
        {
            preset_pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padid)];
        }

        *stan = (StandTile *) preset_pad->stan;

        return &preset_pad->pos;
    }

    player_prop = getCurrentPlayerProp();
    *stan = (StandTile *) player_prop->stan;

    return &player_prop->pos;
}


/**
 * Address 0x7F032D70.
*/
f32 chrGetAngleFromBond(ChrRecord *self)
{
    f32 radBondHeading   = bondviewGetPlayerYawRadians();
    PropRecord *myprop   = self->prop;
    PropRecord *bondprop = getCurrentPlayerProp();
    f32 anglebetween     = atan2f(myprop->pos.x - bondprop->pos.x, myprop->pos.z - bondprop->pos.z);
    f32 radFromBond      = anglebetween - radBondHeading;

    if (anglebetween < radBondHeading)
    {
        radFromBond = radFromBond + M_TAU_F;
    }

    return radFromBond;
}


/**
 * Address 0x7F032DE4.
*/
f32 chrGetDistanceToBond(ChrRecord *guardData)
{
    PropRecord *guardPosData;
    PropRecord *playerPosData;
    float xDiff;
    float yDiff;
    float zDiff;

    guardPosData = guardData->prop;
    playerPosData = getCurrentPlayerProp();
    xDiff = playerPosData->pos.x - guardPosData->pos.x;
    yDiff = playerPosData->pos.y - guardPosData->pos.y;
    zDiff = playerPosData->pos.z - guardPosData->pos.z;

    return sqrtf(SQR(xDiff) + SQR(yDiff) + SQR(zDiff));
}


/**
 * Address 0x7F032E48.
*/
f32 chrGetDistanceToPad(ChrRecord *self, s32 padID)
{
    PropRecord *myprop;
    PadRecord *pad;

    myprop = self->prop;
    padID  = chrResolvePadId(self, padID);

    if (isNotBoundPad(padID))
    {
        pad = (PadRecord *)&g_CurrentSetup.pads[padID];
    }
    else
    {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padID)];
    }

    return sqrtf(
        SQR(pad->pos.x - myprop->pos.x) +
        SQR(pad->pos.y - myprop->pos.y) +
        SQR(pad->pos.z - myprop->pos.z));
}


/**
 * Address 0x7F032EFC.
*/
bool check_if_room_for_preset_loaded(ChrRecord *self, s32 padnum)
{
    PadRecord *pad;
    StandTile *padstan;

    padnum = chrResolvePadId(self, padnum);

    if (isNotBoundPad(padnum))
    {
        pad = (PadRecord *)&g_CurrentSetup.pads[padnum];
    }
    else
    {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padnum)];
    }

    padstan = pad->stan;

    if (padstan)
    {
        return getROOMID_isRendered(getTileRoom(padstan));
    }

    return FALSE;
}


s32 chrResolvePadId(ChrRecord *guardData,s32 padNo)
{
    // Guard's target pad.
    if (padNo == PAD_PRESET1)
    {
        #ifdef DEBUG
        if (guardData->padpreset1 < 0)
        {
            osSyncPrintf("preset less than zero char = %d\n", guardData->chrnum);
        }
        #endif
        padNo = (s32)guardData->padpreset1;
    }

    return padNo;
}


/**
 * Address 0x7F032FAC.
*/
s32 chrResolveId(ChrRecord *self, s32 id)
{
    if (id == (u8)CHR_SEE_SHOT)
    {
        id = self->chrseeshot;
    }
    else if (id == (u8)CHR_SEE_DIE)
    {
        id = self->chrseedie;
    }
    else if (id == (u8)CHR_PRESET)
    {
        id = self->chrpreset1;
    }
    else if (id == (u8)CHR_SELF)
    {
        id = self->chrnum;
    }
    else if (id == (u8)CHR_CLONE)
    {
        id = self->chrnum + 0x2710;
    }
    else if (id == (u8)CHR_BOND_CINEMA)
    {
        if (g_CurrentPlayer->prop->chr)
        {
            id = g_CurrentPlayer->prop->chr->chrnum;
        }
    }

    return id;
}


/**
 * Address 0x7F033040.
 * chrFindById
*/
ChrRecord *chrFindById(ChrRecord *self, s32 guard_id)
{
    s32 i;
    ChrRecord* guard;

    guard_id = chrResolveId(self, guard_id);
    guard = chrFindByLiteralId(guard_id);

    if (guard == NULL)
    {
        for (i=0; i<g_ActiveChrsCount; i++)
        {
            if (guard_id == g_ActiveChrs[i].chrnum)
            {
                guard = &g_ActiveChrs[i];
                break;
            }
        }
    }

    return guard;
}


/**
 * Address 0x7F0330C4.
*/
f32 chrGetDistanceToChr(ChrRecord *self, s32 chrID)
{
    PropRecord *myprop;
    ChrRecord  *chr;
    f32         distance;

    myprop   = self->prop;
    chr      = chrFindById(self, chrID);
    distance = 0.0f;

    if (chr && chr->model && chr->prop)
    {
        distance = sqrtf(
            SQR(chr->prop->pos.x - myprop->pos.x) +
            SQR(chr->prop->pos.y - myprop->pos.y) +
            SQR(chr->prop->pos.z - myprop->pos.z));
    }

    return distance;
}


/**
 * Address 0x7F033154.
*/
f32 chrGetDistanceFromBondToPad(ChrRecord *self, s32 padid)
{
    PropRecord *bondprop;
    PadRecord *pad;

    bondprop = getCurrentPlayerProp();
    padid    = chrResolvePadId(self, padid);

    if (isNotBoundPad(padid))
    {
        pad = (PadRecord *)&g_CurrentSetup.pads[padid];
    }
    else
    {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padid)];
    }

    return sqrtf(
        SQR(pad->pos.x - bondprop->pos.x) +
        SQR(pad->pos.y - bondprop->pos.y) +
        SQR(pad->pos.z - bondprop->pos.z));
}


/**
 * The property is named "flags2".
 * Address 0x7F033218.
*/
void chrSetFlags2(ChrRecord *self, u8 flags2)
{
    self->flags2 |= flags2;
}


/**
 * The property is named "flags2".
 * Address 0x7F03322C.
*/
void chrUnsetFlags2(ChrRecord *self, u8 flags2)
{
    self->flags2 &= ~flags2;
}


/**
 * The property is named "flags2".
 * Address 0x7F033244.
*/
s32 chrHasFlags2(ChrRecord *self, u8 flags2)
{
    return (self->flags2 & flags2) != 0;
}


/**
 * The property is named "flags2".
 * Address 0x7F033260.
*/
void chrSetFlags2ById(ChrRecord *self, s32 chrNum, u8 flags2)
{
    ChrRecord *chr;

    chr = chrFindById(self, chrNum);

    if (chr != NULL)
    {
        chrSetFlags2(chr, flags2);
    }
}


/**
 * The property is named "flags2".
 * Address 0x7F033290.
*/
void chrUnsetFlags2ById(ChrRecord *self, s32 chrNum, u8 flags2)
{
    ChrRecord *chr;

    chr = chrFindById(self, chrNum);

    if (chr != NULL)
    {
        chrUnsetFlags2(chr, flags2);
    }
}


/**
 * The property is named "flags2".
 * Address 0x7F0332C0.
*/
bool chrHasFlags2ById(ChrRecord *self, s32 chrNum, u8 flags2)
{
    ChrRecord *chr;

    chr = chrFindById(self, chrNum);

    if (chr != NULL)
    {
        return chrHasFlags2(chr, flags2);
    }

    return FALSE;
}


/**
 * Address 0x7F0332FC.
*/
void chrSetStageFlags(ChrRecord *self, s32 arg1)
{
    objectiveregisters1 |= arg1;
}


/**
 * Address 0x7F033318.
*/
void chrUnsetStageFlags(ChrRecord *self, u32 flags)
{
    objectiveregisters1 = ~flags & objectiveregisters1; //shorthand does not match
}


/**
 * Address 0x7F033338.
*/
bool chrHasStageFlag(ChrRecord *self, s32 flags)
{
    return (objectiveregisters1 & flags) != 0;
}


/**
 * Address 0x7F033354.
*/
bool chrIsHearingBond(ChrRecord *self)
{
    return (self->hidden & CHRHIDDEN_ALERT_GUARD_RELATED) != 0;
}


/**
 * Address 0x7F033364.
*/
bool chrTrySurrender(ChrRecord *self)
{
    if (chrIsNotDeadOrShot(self))
    {
        chrlvActorThrowWeaponSurrender(self);

        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F0333A0.
*/
bool chrFadeOut(ChrRecord *self)
{
    chrlvActorFadeAway(self);

    return TRUE;
}


/**
 * Address 0x7F0333C4.
*/
void chrRestartTimer(ChrRecord *self)
{
    self->timer60 = 0;
    self->hidden |= CHRHIDDEN_TIMER_ACTIVE;
}


/**
 * Address 0x7F0333D8.
*/
f32 chrGetTimer(ChrRecord *self)
{
    return self->timer60 / CHRLV_FRAMERATE_F;
}


/**
 * Address 0x7F0333F8.
*/
bool sub_GAME_7F0333F8(ChrRecord *self)
{
    Model  *mymodel;
    coord3d zeropos;
    coord3d pos;
    vec3d vec;
    f32     scale;

    if (chrlvCurrentPlayerCall7F0B0E24(self))
    {
        mymodel = self->model;
        scale   = getinstsize(mymodel) * 0.8f;
        sub_GAME_7F068190(&zeropos, &pos);
        getsuboffset(mymodel, &vec);
        mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &vec);

        if (projectileTestPropBoundingSphere(&zeropos, &pos, &vec, scale))
        {
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F033490.
*/
bool chrIfNearMiss(ChrRecord *self)
{
    return (self->chrflags & CHRFLAG_NEAR_MISS) != 0;
}


/**
 * Address 0x7F0334A0.
*/
bool chrGoToBond(ChrRecord *self, SPEED speed)
{
    PropRecord *bondprop;

    if (chrIsNotDeadOrShot(self) && (g_SeenBondRecentlyGuardCount < 10))
    {
        bondprop = getCurrentPlayerProp();

        if (plot_course_for_actor(self, &bondprop->pos, bondprop->stan, speed))
        {
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F03350C0.
*/
bool chrGoToChr(ChrRecord *self, s32 chrid, SPEED speed)
{
    ChrRecord *chr;
    PropRecord *chrprop;

    if (chrIsNotDeadOrShot(self) && (g_SeenBondRecentlyGuardCount < 10))
    {
        chr = chrFindById(self, chrid);
        if (chr && chr->model && chr->prop)
        {
            chrprop = chr->prop;

            if (plot_course_for_actor(self, &chrprop->pos, chrprop->stan, speed))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}


/**
 * Return number of hits.
 *
 * Address 0x7F0335A4.
 * PD: chrGetNumArghs
 */
s8 chrGetNumArghs(ChrRecord *self)
{
    return self->numarghs;
}


/**
 * Return number of near misses
 *
 * Address 0x7F0335AC.
 * PD: chrGetNumCloseArghs
 */
s8 chrGetNumCloseArghs(ChrRecord *self)
{
    return self->numclosearghs;
}


/**
 * Return false if chrseeshot is negative.
 *
 * Address 0x7F0335B4.
 */
bool chrSawInjury(ChrRecord *self)
{
    return ((self->chrseeshot < 0) ^ 1);
}


/**
 * Return false if chrseedie is negative.
 *
 * Address 0x7F0335C4.
 */
bool chrSawDeath(ChrRecord *self)
{
    return ((self->chrseedie < 0) ^ 1);
}


/**
 * Address 0x7F0335D4.
*/
bool chraiStopAnimation(ChrRecord *self)
{
    if (chrIsNotDeadOrShot(self))
    {
        chrlvKneelingAnimationRelated7F023E48(self);

        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F033610.
*/
bool chrTrySurprisedOneHand(ChrRecord *self)
{
    if (chrIsNotDeadOrShot(self))
    {
        chrlvActorShuffleFeet(self);

        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F03364C.
*/
bool chrTrySurprisedSurrender(ChrRecord *self)
{
    if (chrIsNotDeadOrShot(self))
    {
        chrlvSurrenderAnimationRelated(self);

        return TRUE;

    }
    return FALSE;
}


/**
 * Address 0x7F033688.
*/
bool chrTrySurprisedLookAround(ChrRecord *self)
{
    if (chrIsNotDeadOrShot(self))
    {
        chrlvActorLookFlustered(self);

        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F0336C4.
*/
bool check_if_able_to_then_kneel(ChrRecord *self)
{
    if (chrIsNotDeadOrShot(self))
    {
        chrKneelChooseAnimation(self);

        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F033700.
*/
s32 check_if_able_to_then_perform_animation(ChrRecord *self, s32 animID, s32 startframe, s32 endframe, u8 bitfield, s32 interpol_time60)
{
    if (chrIsNotDeadOrShot(self))
    {
        chrlvPerformAnimationForActor(self, animID, startframe, endframe, bitfield, interpol_time60);

        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F033760.
 * PD: chrCanHearAlarm
*/
bool chrCanHearAlarm(ChrRecord *self)
{
    /*
     possibly this was to be more advanced than simply
     a stub to alarmIsActive.
     It could have for example done a room check
     since this has a "self" reference.
     */
    return alarmIsActive();
}


/**
 * Address 0x7F033780.
 * PD: waypointIsWithin90DegreesOfPosAngle
*/
s32 sub_GAME_7F033780(waypoint *arg0, coord3d *arg1, f32 angle)
{
    f32 temp_f0;
    PadRecord *pad;
    f32 dx;
    f32 dz;
    f32 ff;

    pad = &g_CurrentSetup.pads[arg0->padID];
    dx = pad->pos.f[0] - arg1->f[0];
    dz = pad->pos.f[2] - arg1->f[2];

    temp_f0 = atan2f(dx, dz);
    ff = angle - temp_f0;

    if (angle < temp_f0)
    {
        ff += M_TAU_F;
    }

    if ((ff < DegToRad(90)) || (ff > DegToRad(270)))
    {
        return 1;
    }

    return 0;
}


/**
 * Attempt to find a waypoint near pos which is in a particular quadrant to pos,
 * then return its padnum.
 *
 * For example, pos is typically the player's position, angle is the direction
 * the player is facing, and quadrant is which quadrant (front/back/left/right)
 * that is desired relative to the player's position and angle.
 *
 * The function starts by finding the closest waypoint to the pos. If it's not
 * in the quadrant then its neighouring waypoints are checked too. If none of
 * those are in the quadrant then no further checks are made and the function
 * returns -1.
 *
 * Address 0x7F033834.
 * PD: chrFindWaypointWithinPosQuadrant
*/
s32 chrlvFindPathNeighborRelated(coord3d *bondpos, StandTile *stan, f32 rot, u8 quadrant)
{
    s32 padnum_2;
    s32 temp_s1;
    s32 temp_s1_2;
    waypoint *waypoint;
    s32 path_id;
    s32 neighbor_index;

    waypoint = chrlvStanPathRelated(bondpos, stan);

    if (waypoint)
    {
        switch (quadrant)
        {
            case QUADRANT_BACK:
                rot = rot + DegToRad(180);
                break;

            case QUADRANT_SIDE1:
                rot = rot + DegToRad(90);
                break;

            case QUADRANT_SIDE2:
                rot = rot + DegToRad(270);
                break;

            case QUADRANT_FRONT:
                break;
        }

        if (rot >= M_TAU_F)
        {
            rot = rot - M_TAU_F;
        }

        if (sub_GAME_7F033780(waypoint, bondpos, rot))
        {
            return waypoint->padID;
        }

        for (
            neighbor_index = 0, path_id = waypoint->neighbours[neighbor_index];
            path_id>=0;
            neighbor_index++, path_id = waypoint->neighbours[neighbor_index]
            )
        {
            if (sub_GAME_7F033780(&g_CurrentSetup.pathwaypoints[path_id], bondpos, rot) != 0)
            {
                return g_CurrentSetup.pathwaypoints[path_id].padID;
            }
        }
    }

    return -1;
}


/**
 * Address 0x7F033998.
*/
bool check_2328_preset_set_with_method(ChrRecord *self, u8 quadrant)
{
    PropRecord *myprop;
    PropRecord *bondprop;

    waypoint *myclosestwaypoint;
    waypoint *bondsclosestwaypoint;

    waypoint *sp2C[PATH_FINDING_WP_LIMIT];

    if ((quadrant == QUADRANT_2NDWPTOTARGET) || (quadrant == QUADRANT_20))
    {
        myprop               = self->prop;
        bondprop             = getCurrentPlayerProp();
        myclosestwaypoint    = chrlvStanPathRelated(&myprop->pos, myprop->stan);
        bondsclosestwaypoint = chrlvStanPathRelated(&bondprop->pos, bondprop->stan);

        if (myclosestwaypoint != NULL && bondsclosestwaypoint != NULL)
        {
            if (quadrant == QUADRANT_2NDWPTOTARGET)
            {
                if (waypointFindRoute(myclosestwaypoint, bondsclosestwaypoint, (waypoint **)&sp2C, PATH_FINDING_WP_LIMIT) >= PATH_FINDING_WP_LIMIT)
                {
                    self->padpreset1 = sp2C[1]->padID;

                    return TRUE;
                }
            }
            else
            {
                myclosestwaypoint = waypointFindNextStepToward(myclosestwaypoint, bondsclosestwaypoint);
                if (myclosestwaypoint != NULL)
                {
                    self->padpreset1 = myclosestwaypoint->padID;

                    return TRUE;
                }
            }
        }
    }
    else
    {
        s32 closestpadid = chrlvFindPathNeighborRelated(&self->prop->pos, self->prop->stan, getsubroty(self->model), quadrant);

        if (closestpadid >= 0)
        {
            self->padpreset1 = closestpadid;

            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F033AAC.
*/
bool sub_GAME_7F033AAC(ChrRecord *self, u8 padnum)
{
    f32 sp1C;
    s32 bondnearestpad;
    PropRecord *bondprop;

    if ((padnum == 16) || (padnum == 32))
    {
        return check_2328_preset_set_with_method(self, padnum);
    }

    sp1C           = bondviewGetPlayerYawRadians();
    bondprop       = getCurrentPlayerProp();
    bondnearestpad = chrlvFindPathNeighborRelated(&bondprop->pos, bondprop->stan, sp1C, padnum);

    if (bondnearestpad >= 0)
    {
        self->padpreset1 = bondnearestpad;

        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F033B38.
 * PD: chrSetChrPresetToChrNearPos
*/
bool sub_GAME_7F033B38(ChrRecord *self, f32 distance)
{
    PropRecord *myprop;
    ChrRecord *chr;
    s32 numguards;
    coord3d distneg;
    coord3d distplus;
    s32 myroom;
    s32 i;

    numguards = get_numguards();
    myprop    = self->prop;
    myroom    = myprop->stan->room;

    distneg.x  = myprop->pos.x - distance;
    distplus.x = myprop->pos.x + distance;
    distneg.y  = myprop->pos.y - distance;
    distplus.y = myprop->pos.y + distance;
    distneg.z  = myprop->pos.z - distance;
    distplus.z = myprop->pos.z + distance;

    for (i = 0; i < numguards; i++)
    {
        chr = &g_ChrSlots[i];

        if ((chr != self) && chr->model && !chrIsDead(chr))
        {
            coord3d *pos = &chr->prop->pos;

            if (
                (pos->x >= distneg.x)  &&
                (pos->x <= distplus.x) &&
                (pos->y >= distneg.y)  &&
                (pos->y <= distplus.y) &&
                (pos->z >= distneg.z)  &&
                (pos->z <= distplus.z) &&
                ((chr->prop->stan->room == myroom) || bgRoomsSharePortal(myroom, chr->prop->stan->room)))
            {
                self->chrpreset1 = chr->chrnum;

                return TRUE;
            }
        }
    }
    return FALSE;
}


/**
 * Address 0x7F033CF4.
*/
void chrSetChrPreset(ChrRecord *self, s32 id)
{
    self->chrpreset1 = chrResolveId(self, id);
}


/**
 * Address 0x7F033D1C.
*/
void chrSetChrPreset2(ChrRecord *self, s32 id, s32 id2)
{
    ChrRecord *chr;

    chr = chrFindById(self, id);

    if (chr)
    {
        chr->chrpreset1 = chrResolveId(self, id2);
    }
}


/**
 * Address 0x7F033D5C.
*/
void chrSetPadPreset( ChrRecord *self, s32 padid)
{
    self->padpreset1 = chrResolvePadId(self, padid);
}


/**
 * Address 0x7F033D84.
*/
void chrSetPadPresetByChrnum(ChrRecord *self, s32 chrid, s32 padid)
{
    ChrRecord *chr = chrFindById(self, chrid);

    if (chr)
    {
        chr->padpreset1 = chrResolvePadId(self, padid);
    }
}


/**
 * Address 0x7F033DC4.
*/
s32 chrIsTargetNearlyInSight(ChrRecord *self)
{
    PropRecord *player_prop;
    PropRecord *self_prop;
    StandTile *stan;
    coord3d sp48;
    coord3d sp3C;

    player_prop = getCurrentPlayerProp();
    self_prop   = self->prop;
    stan        = self_prop->stan;

    stanResetHits();

    if (walkTilesBetweenPoints_NoCallback(&stan, self_prop->pos.x, self_prop->pos.z, player_prop->pos.x, player_prop->pos.z))
    {
        return FALSE;
    }
    else
    {
        getCollisionEdge_maybe(&sp48, &sp3C); //extreme edges of stan tile

        if (
            sub_GAME_7F0304AC(self, &self_prop->pos, self_prop->stan, &sp48, &player_prop->pos, player_prop->stan, 0)
            || sub_GAME_7F0304AC(self, &self_prop->pos, self_prop->stan, &sp3C, &player_prop->pos, player_prop->stan, 0))
        {
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F033EAC.
 * PD: chrIsPosOffScreen
*/
s32 chrIsPosOffScreen(coord3d *arg0, StandTile *tile)
{
    bool offscreen;
    bbox2d box;

    offscreen = TRUE;

    if (getROOMID_isRendered(getTileRoom(tile)) && fogPositionIsVisibleThroughFog(arg0, 0.0f))
    {
        if (bgGet2dBboxByRoomId(getTileRoom(tile), &box))
        {
            offscreen = camIsPosInScreenBox(arg0, 200.0f, &box) == 0;
        }
        else
        {
            offscreen = camIsPosInScreen(arg0, 200.0f) == 0;
        }
    }

    return offscreen;
}


/**
 * Address 0x7F033F48.
 * PD: chrAdjustPosForSpawn
*/
bool chrAdjustPosForSpawn(coord3d *pos, StandTile **arg1, f32 facing, bool allowonscreen)
{
    coord3d testpos;
    StandTile *s;
    s32 i;
    StandTile **spp;

    s = *arg1;
    spp = &s;

    if ((stanTestVolume(spp, pos->f[0], pos->z, 20.0f, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f) < 0) &&
        (allowonscreen || chrIsPosOffScreen(pos, *arg1)))
    {
        return TRUE;
    }

    for (i = 0; i < 8; i++)
    {
        testpos.f[0] = pos->f[0] + (sinf(facing) * 60.0f);
        testpos.f[1] = pos->f[1];
        testpos.f[2] = pos->f[2] + (cosf(facing) * 60.0f);

        s = *arg1;

        if (stanTestLineUnobstructed(spp, pos->f[0], pos->f[2], testpos.f[0], testpos.f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f, 0.0f, 1.0f)
            && (stanTestVolume(spp, testpos.f[0], testpos.f[2], 20.0f, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f) < 0)
            && (allowonscreen || chrIsPosOffScreen(&testpos, s)))
        {
            *arg1 = s;

            pos->f[0] = testpos.f[0]; //send back upstream
            pos->f[2] = testpos.f[2];

            return TRUE;
        }

        facing += 0.7853982f;

        if (facing >= M_TAU_F) //clamp to 1 revolution
        {
            facing -= M_TAU_F;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F03415C.
 * PD: chrSpawnAtCoord
*/
PropRecord *chrSpawnAtCoord(s32 bodynum, s32 headnum, coord3d *pos, StandTile *stan, f32 angle, AIListRecord *ailist, s32 spawnflags)
{
    PropRecord *chrprop;
    coord3d newpos; //struct copy here would have been more efficient
    ChrRecord *chr;
    StandTile *stancopy;
    Model *chrHeader;

    if (chrGetNumFree() >= 3)
    {
        if (headnum < 0)
        {
            headnum = bodyChooseHead(bodynum);
        }

        newpos.x = pos->x;
        newpos.y = pos->y;
        newpos.z = pos->z;
        stancopy = stan;

        if (chrAdjustPosForSpawn(&newpos, &stancopy, angle, ((spawnflags & 0x10) != 0)))
        {
            chrHeader = retrieve_header_for_body_and_head(bodynum, headnum, spawnflags);

            if (chrHeader != NULL)
            {
                chrprop = chrAllocate(chrHeader, &newpos, angle, stancopy, ailist);

                if (chrprop != NULL)
                {
                    chrpropActivateThisFrame(chrprop);
                    chrpropEnable(chrprop);
                    chr          = chrprop->chr;
                    chr->headnum = headnum;
                    chr->bodynum = bodynum;

                    return chrprop;
                }
            }
        }
    }

    return NULL;
}


/**
 * Address 0x7F034258.
*/
PropRecord *chrSpawnAtPad(ChrRecord *self, s32 bodynum, s32 headnum, s32 padid, AIListRecord *ailist, s32 flags)
{
    PadRecord *pad;
    padid = chrResolvePadId(self, padid);

    if (isNotBoundPad(padid))
    {
        pad = (PadRecord *)&g_CurrentSetup.pads[padid];
    }
    else
    {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padid)];
    }
    //<- not here...
    #ifdef ENABLE_LOG
    osSyncPrintf("%s%s new char x = %f, y = %f, z = %f \n", "", "", pad->pos.x, pad->pos.y, pad->pos.z);
    #endif
    return chrSpawnAtCoord(bodynum, headnum, &pad->pos, pad->stan, atan2f(pad->look.f[0], pad->look.f[2]), ailist, flags);
}


/**
 * Address 0x7F034308.
 */
PropRecord *chrSpawnAtChr(ChrRecord *self, s32 bodynum, s32 headnum, s32 chrnum, AIListRecord *ailist, s32 flags)
{
    ChrRecord *chr;
    chr = chrFindById(self, chrnum);

    if (!(chr->chrflags & CHRFLAG_HAS_BEEN_ON_SCREEN))
    {
        f32 chrRadHeading   = getsubroty(chr->model);
        PropRecord *chrprop = chr->prop;

        return chrSpawnAtCoord(bodynum, headnum, &chrprop->pos, chrprop->stan, chrRadHeading, ailist, flags);
    }

    return NULL;
}


/**
 * Address 0x7F034388.
*/
bool chrIfInPadRoom(ChrRecord *self, s32 chrnum, s32 padnum)
{
    PadRecord *pad;
    ChrRecord *chr;

    chr    = chrFindById(self, chrnum);
    padnum = chrResolvePadId(self, padnum);

    if (isNotBoundPad(padnum))
    {
        pad = (PadRecord *)&g_CurrentSetup.pads[padnum];
    }
    else
    {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padnum)];
    }

    if (pad->stan && chr)
    {
        if (chr->prop && (pad->stan->room == chr->prop->stan->room))
        {
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F03444C.
*/
bool check_if_actor_is_at_preset(ChrRecord *self, s32 padnum)
{
    PropRecord *bondprop;
    PadRecord  *pad;

    bondprop = getCurrentPlayerProp();
    padnum   = chrResolvePadId(self, padnum);

    if (isNotBoundPad(padnum))
    {
        pad = (PadRecord *)&g_CurrentSetup.pads[padnum];
    }
    else
    {
        pad = (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padnum)];
    }
#ifndef TARGET_N64
    { /* GE_PADDBG=1: trace pad-room tests, one line per second per pad id */
        static s32 dbg = -1;
        static s32 seen[32]; static u32 seencalls[32]; static s32 nseen;
        if (dbg < 0) { extern char *getenv(const char *); dbg = getenv("GE_PADDBG") != NULL; }
        if (dbg)
        {
            s32 k;
            for (k = 0; k < nseen; k++) if (seen[k] == padnum) break;
            if (k == nseen && nseen < 32) { seen[nseen] = padnum; seencalls[nseen] = 0; nseen++; }
            if (k < 32 && (seencalls[k]++ % 60) == 0)
            {
                extern int printf(const char *, ...);
                printf("paddbg: pad=%d bound=%d stan=%p padroom=%d bondroom=%d padpos=(%.0f,%.0f,%.0f)\n",
                        padnum, isNotBoundPad(padnum) ? 0 : 1, (void *) pad->stan, pad->stan ? (int) pad->stan->room : -1,
                        bondprop->stan ? (int) bondprop->stan->room : -1, pad->pos.x, pad->pos.y, pad->pos.z);
            }
        }
    }
#endif

    if (pad->stan && (pad->stan->room == bondprop->stan->room))
    {
        return TRUE;
    }

    return FALSE;
}


/**
 * Address 0x7F0344FC.
*/
bool removed_animation_routine_27(ChrRecord *self)
{
    return FALSE;
}


/**
 * Address 0x7F034508.
*/
bool removed_animation_routine_2B(ChrRecord *self)
{
    return FALSE;
}


/**
 * Address 0x7F034514.
*/
bool chrTryStartAlarm(ChrRecord *self, s32 PadId)
{
    ObjectRecord *objinst;

    PadId = chrResolvePadId(self, PadId);

    if (chrIsNotDeadOrShot(self))
    {
        objinst = scan_position_data_table_for_normal_object_at_preset(PadId);

        if (objinst && objIsHealthy(objinst))
        {
            chrStartAlarmChooseAnimation(self);

            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address 0x7F03457C.
*/
bool actor_draws_throws_grenade_at_player_if_possible(ChrRecord *self)
{
    PropRecord *Left;
    PropRecord *Right;

    PropRecord      *NewGrenadeProp;
    WeaponObjRecord *NewGrenadeObj;
    WeaponObjRecord *LeftWep;
    WeaponObjRecord *RightWep;

    s32 flags;
    //GUNHAND hand;
    //"grenade prob: no chr number %d for obj number %d!\n"
    if (((u32)randomGetNext() % (u32)0xFF) >= self->grenadeprob)
    {
        return FALSE;
    }

    if (chrGetDistanceToBond(self) < 10.0f)
    {
        return FALSE;
    }

    if (chrIsNotDeadOrShot(self))
    {
        Left  = chrGetEquippedWeaponProp(self, GUNLEFT);
        Right = chrGetEquippedWeaponProp(self, GUNRIGHT);

        if (Right && (RightWep = Right->weapon, RightWep->weaponnum == ITEM_GRENADE))
        {
            chrlvThrowGrenade(self, Right, GUNRIGHT, 0);

            return TRUE;
        }

        if (Left && (LeftWep = Left->weapon, LeftWep->weaponnum == ITEM_GRENADE))
        {
            chrlvThrowGrenade(self, Left, GUNLEFT, 0);

            return TRUE;
        }

        if (!Left || !Right)
        {
            flags = 0;

            if (Right)
            {
                flags = 0x10000000;
            }

            NewGrenadeProp = chrGiveWeapon(self, 0xC4, ITEM_GRENADE, flags);

            if (NewGrenadeProp)
            {
                NewGrenadeObj = NewGrenadeProp->weapon;
                NewGrenadeObj->runtime_bitflags |= 0x800; //manual bitflags are more effecient

                chrlvThrowGrenade(self, NewGrenadeProp, !Right ? GUNRIGHT : GUNLEFT, 1);

                return TRUE;
            }
        }
    }

    return FALSE;
}


/**
 * Address 0x7F0346FC.
 * chrDropItem
*/
bool chrDropItem(ChrRecord *self, s32 modelnum, u8 weaponid)
{
    WeaponObjRecord *NewModel = (WeaponObjRecord *)create_new_item_instance_of_model(modelnum, weaponid);

    if (NewModel && NewModel->prop)
    {
        modelSetScale(NewModel->model, NewModel->model->scale);
        chrpropReparent(NewModel->prop, self->prop);
        NewModel->timer = CHRLV_DEFAULT_TIMER;
        propobjSetDropped(NewModel->prop, 1);
        self->hidden = self->hidden | CHRHIDDEN_DROP_HELD_ITEMS;

        return TRUE;
    }

    return FALSE;
}
