#include <game/chrObjRandom.h>
/*---------------------------------------------------------------------

	File		propobj.c

	Comments	Prop Objects code.

  ---------------------------------------------------------------------*/

#include <ultra64.h>
#include "game/debugmenu_handler.h"
#include "game/front.h"
#include "game/othermodemicrocode.h"
#include "game/dyn.h"
#include "game/loadobjectmodel.h"
#ifndef TARGET_N64
#include <stdio.h> /* GE_DUMP_BOND glass hit-path probes */
#include <stdlib.h>
#endif
#include <math.h>
#include <PR/libaudio.h>
#include <assets/oddtextures.h>
#include <bondgame.h>
#include <boss.h>
#include <limits.h>
#include <music.h>
#include <memp.h>
#include <snd.h>
#include <gbi_extension.h>
#include "propobj.h"
#include "assets/obseg/text/LpropobjE.h"
#include "bg.h"
#include "bgfog.h"
#include "bondaicommands.h"
#include "bondinv.h"
#include "bondview.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "chrobjdata.h"
#include "explosion.h"
#include "fr.h"
#include "glass.h"
#include "gun.h"
#include "image_bank.h"
#include "lv.h"
#include "language.h"
#include "math_floor.h"
#include "math_asinfacosf.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "model.h"
#include "objecthandler.h"
#include "objective_status.h"
#include "player.h"
#include "quaternion.h"
#include "random.h"
#include "stan.h"
#include "stanintersection.h"
#include "tex.h"
#include "textrelated.h"
#include "vtxstore.h"
#ifndef TARGET_N64
#include "frametiming.h"   /* portAnimTicks / portLagStep: sub-field smoothing */
#include "cheat.h"         /* cheatIsActive(CHEAT_PORT_UNLOCK_DOORS) - propdoorInteract */
extern s32 g_PortFadeDbg;  /* the fade diagnostic (cl_fadedbg), defined beside posIsOnScreen */
static void portFadeNote(PropRecord *prop, s32 state, s32 alpha, f32 foga);
#endif


#if defined(VERSION_JP) || defined(VERSION_EU)
#define MONITOR_TIMER_DELTA g_JP_GlobalTimerDelta
#else
#define MONITOR_TIMER_DELTA g_GlobalTimerDelta
#endif

#ifndef TARGET_N64
/* Monitor scripts count their pauses in whole fields, so their scrolls must too or
 * one-field steps smear across sub-ticks and drift from the pauses (murkantor/Project-Midas#27). */
#undef MONITOR_TIMER_DELTA
#define MONITOR_TIMER_DELTA ((portTickHz() != 60) ? (f32) g_ClockTimer : g_GlobalTimerDelta)
#endif

#if defined(VERSION_JP)
#define OBJECT_INTERACTION_TIMER_DELTA g_JP_GlobalTimerDelta
#else
#define OBJECT_INTERACTION_TIMER_DELTA g_GlobalTimerDelta
#endif

#define PROXIMITY_MINE_TRIGGER_DISTANCE 62500.0f

// aprox 135 deg/s/s divided by 60fps
// (135/60/60/(180/M_PI_F))
#define CAM_ACCEL 0.00065449846f 

// 34.3 deg/s/s
#define AUTOGUN_SPIN_ACCEL_PER_FRAME    0.009973311f

// 2058 deg/s or 5.7 rotations per second
#define AUTOGUN_SPIN_MAX_SPEED          0.5983986f

//If squared speed exceeds this, the rocket stops accelerating. This corresponds to ~166.6 units/s.
#define ROCKET_SPEED_BREAK_THRESHOLD    27777.773f

//Horizontal friction applied when projectile is sliding.
#define PROJECTILE_FRICTION_FACTOR      0.9f

#if defined(VERSION_EU)

#define AUTOGUN_YAW_MAX_SPEED          0.0008377581f

#define AUTOGUN_YAW_ACCEL_PER_FRAME    0.0000139626345f

#define AUTOGUN_PITCH_ACCEL_PER_FRAME  0.0000069813173f

#define AUTOGUN_PITCH_MAX_SPEED        0.00041887906f

#define AUTOGUN_ALERT_ACCEL_PER_FRAME  0.0010471976f

#define AUTOGUN_TRACKING_FRAMES 100

#define TRUCK_TURN_ACCEL_PER_FRAME     0.0001308997f

#define TRUCK_TURN_DECEL_PER_FRAME     0.0002617994f

#define TRUCK_TURN_MAX_SPEED           0.007853982f

#define PROJECTILE_LIFETIME_FRAMES 2000

#define GRENADE_SMOKE_FRAMES 0xFB

#define CCTV_ALARM_FRAMES 250.0f

#else

// 2.4 deg/s (2.4/60/(180/M_PI_F}))
#define AUTOGUN_YAW_MAX_SPEED          0.00069813174f

// 2.4 deg/s/s (2.4/60/60/(180/M_PI_F))
#define AUTOGUN_YAW_ACCEL_PER_FRAME    0.000011635529f

// 1.2 deg/s 
#define AUTOGUN_PITCH_ACCEL_PER_FRAME  0.0000058177643f

// 1.2 deg/s/s 
#define AUTOGUN_PITCH_MAX_SPEED        0.00034906587f

//Faster rotation when autogun is actively tracking. 3 deg/s
#define AUTOGUN_ALERT_ACCEL_PER_FRAME  0.0008726647f

#define AUTOGUN_TRACKING_FRAMES 120

//0.375 deg/s/s - hmm, wheels maybe?
#define TRUCK_TURN_ACCEL_PER_FRAME     0.000109083085f

//0.75 deg/s/s
#define TRUCK_TURN_DECEL_PER_FRAME     0.00021816617f

// 22.5 deg/s 
#define TRUCK_TURN_MAX_SPEED           0.006544985f

// 40 seconds
#define PROJECTILE_LIFETIME_FRAMES 2400

#define GRENADE_SMOKE_FRAMES 301

#define CCTV_ALARM_FRAMES 300.0f

#endif


/* From the decomp.me ctx -- not present in any repo header. Without the macro,
 * C89 turns U32_TO_F32(...) into an implicit function call (5 sites, ~31 insns
 * short, wrong FP allocation everywhere downstream). */
#define U32MAX 4294967295
#define U32_TO_F32(x) (x*(1.0f/U32MAX))

/* The two named pool constants from the retired GLOBAL_ASM block. Both are the
 * compiler's own literal pool regenerated from these literals (menu18 precedent);
 * value 0x3e8e38e4 = 0.27777779f in both slots of the shipped pool. */
#define ROCKET_INITIAL_GRAVITY_MODIFIER  0.27777779f
#define PROP_PROJECTILE_GRAVITY_MODIFIER 0.27777779f

/* 0x80030AC8 */ s32 alarm_timer = 0;
/* 0x80030ACC */ s32 *ptr_alarm_sfx = 0;
/* 0x80030AD0 */ f32 toxic_gas_sound_timer = 0.0;
/* 0x80030AD4 */ s32 activate_gas_sound_timer = FALSE;
/* 0x80030AD8 */ coord3d gasLeakSource = { 0.0f, 0.0f, 0.0f };
/* 0x80030ADC */ s32 D_80030ADC = 0;
/* 0x80030AE0 */ f32 gasLeakTimer = 0.0f;
/* 0x80030AE4 */ ALSoundState *ptr_gas_sound = NULL;
/* 0x80030AE8 */ s32 clock_drawn_flag = 1;
/* 0x80030AEC */ s32 clock_enable = 0;
/* 0x80030AF0 */ f32 clock_time = 0;
/* 0x80030AF4 */ s32 g_RemoteMineOwnerTriggerFlag = 0;
/* 0x80030AF8 */ s32 g_NextWeaponSlot = 0; // numbers between 0 and 30
/* 0x80030AFC */ s32 g_NextHatSlot = 0;
/* 0x80030B00 */ ObjectRecord *g_LevelLoadPropSwitch = NULL;
/* 0x80030B04 */ LockDoorRecord *g_LevelLoadPropLockDoor = NULL;
/* 0x80030B08 */ ObjectRecord *g_LevelLoadPropSafeItem = NULL;
/* 0x80030B0C */ struct PropRecord * D_80030B0C = NULL;
/* 0x80030B10 */ s32 bodypartshot = 0xFFFFFFFF;
/* 0x80030B14 */ f32 F_80030B14 = 1.0;
/* 0x80030B18 */ f32 F_80030B18 = 1.0;
/* 0x80030B1C */ f32 g_AutogunPendingDamageTick = 1.0;
/* 0x80030B20 */ f32 g_AutogunDamageScalar = 1.0;
/* 0x80030B24 */ f32 F_80030B24 = 1.0;

/*
* Set on level load.
*/
f32 g_SoloAmmoMultiplier = 1.0;

extern struct ModelAnimation *animation_table_ptrs2[];

struct tvcmd {
    MonitorCmdWord type;
    intptr_t time;
    MonitorCmdWord arg2;
};

// Forward declarations.

s32 updateDoorDisplacement(DoorRecord* door);
s32 objGetShotsTaken(ObjectRecord *);
void sub_GAME_7F04AC20(PropRecord *prop, ModelRenderData *, s32 arg2);
bool chrobjSeparatingAxisTheorem(rect4f* rect1, s32 numvertices0, rect4f* rect2, s32 numvertices1);
void chrobjSndCreatePostEvent(ALSoundState *state, coord3d *pos, f32 low, f32 high);
void remove_obj_from_temp_proxmine_table(WeaponObjRecord* proxy);
void add_obj_to_temp_proxmine_table(WeaponObjRecord* proxy);
s32 sub_GAME_7F042EB4(struct ObjectRecord *arg0, f32 *arg1, struct coord3d *arg2, struct coord3d *arg3, s32 arg4, s32 arg5);
s32 objTryMovePropWithCollision(ObjectRecord *obj, coord3d *arg1, coord3d *arg2, coord3d *arg3, s32 arg4);
s32 handles_projectile_motion(struct ObjectRecord *arg0, coord3d *arg1, coord3d *arg2, coord3d *arg3, s32 arg4, s32 arg5);
void objSettle(struct ObjectRecord *arg0, struct coord3d *arg1);
void door7F054FB4(struct DoorRecord *arg0);
void door7F0526EC(DoorRecord *door, Mtxf *rhs);
void objBreakCCTVGlass(ObjectRecord *obj);
void save_img_index_to_obj_ani_slot(MonitorRecord *mon, void *unk88);
void save_ptr_monitor_ani_code_to_obj_ani_slot(MonitorRecord *mon, void *image);
s32 sub_GAME_7F06C010(ModelHitEntry **entryptr, coord3d *modelRayStart, coord3d *modelRayDir, Model **outModel, ModelNode **outNode);
AmmoCrateRecord *ammocrateAllocate(void);
ModelNode* sub_GAME_7F04B478(ObjectRecord* obj);
bool sub_GAME_7F04B590(ModelFileHeader* arg0, ModelNode* arg1);
void detonate_proxmine_In_range(coord3d *pos);
void doorDeactivatePortal(DoorRecord *door);
void doorSetOpenState(DoorRecord *door, s32 state);
s32  sub_GAME_7F053894(coord3d *pos, f32 low, f32 high);
void sub_GAME_7F053A3C(DoorRecord *door);

// End forward declarations.

u32 chrObjRandomGetNext(void);

/* PD: projectileFree (similar but not the same structure) */
void projectileFree(Projectile* projectile)
{
    ALSoundState* sound1;
    ALSoundState* sound2;

    if (projectile->flags & PROJECTILEFLAG_LAUNCHING)
    {
        sound1 = projectile->sounds[0];
        if ((sound1 != 0) && (sndGetPlayingState((ALSoundState* ) sound1) != 0))
        {
            sndDeactivate((ALSoundState* ) projectile->sounds[0]);
        }

        sound2 = projectile->sounds[1];
        if ((sound2 != 0) && (sndGetPlayingState((ALSoundState* ) sound2) != 0))
        {
            sndDeactivate((ALSoundState* ) projectile->sounds[1]);
        }
    }
    projectile->flags |= PROJECTILEFLAG_FREE;
}


void projectileReset(Projectile *projectile)
{
    projectile->flags = 0;

    projectile->speed.x = 0.0f;
    projectile->speed.y = 0.0f;
    projectile->speed.z = 0.0f;

    projectile->unk10.x = 0.0f;
    projectile->unk10.y = 0.0f;
    projectile->unk10.z = 0.0f;

    projectile->unk1C = 0.0f;

    projectile->unk60 = 1.0f;
    projectile->ownerprop = NULL;
    projectile->unk8C = 0.05f;
    projectile->unk90 = 0;
    projectile->unk94 = 0.0f;
    projectile->lastSfxTimer = -1;
    projectile->soundSlot = 0;
    projectile->unkA8 = 0;
    projectile->unkAC = -1;
    projectile->droptype = DROPTYPE_DEFAULT;
    projectile->refreshrate = 0;
    projectile->unkC0 = 1.0f;
    projectile->unkC4 = 1.0f;
    projectile->unkC8 = 1.0f;
    projectile->age = 0;
    projectile->obj = 0;
    projectile->unkE8 = 0;
}


Projectile *projectileAllocate(void)
{
    s32 bestindex;
    s32 i;

    bestindex = -1;

    // Happy path - find one that is already free
    for (i = 0; i < PROJECTILES_ARR_MAX; i++)
    {
        if (g_Projectiles[i].flags & PROJECTILEFLAG_FREE)
        {
            projectileReset(g_Projectiles + i);
            return (g_Projectiles + i);
        }
    }

    // Find one with the lowest unkE8 (some kind of age/timer?)
    // and some other conditions
    for (i = 0; i < PROJECTILES_ARR_MAX; i++)
    {
        if (g_Projectiles[i].obj && (bestindex < 0 || g_Projectiles[i].unkE8 < g_Projectiles[bestindex].unkE8))
        {
            bestindex = i;
        }
    }

    if (bestindex >= 0)
    {
        // Reset and return it
        objFreeEmbedmentOrProjectile(g_Projectiles[bestindex].obj->prop);
        g_Projectiles[bestindex].obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;

        projectileReset(g_Projectiles + bestindex);
        return (g_Projectiles + bestindex);
    }
    else
    {
        return NULL;
    }
}


void sub_GAME_7F03FDA8(PropRecord *prop)
{
    ObjectRecord *po = prop->obj; //canonical name
    if (po->runtime_bitflags & RUNTIMEBITFLAG_EMBEDDED)
    {
        #ifdef DEBUG
        assert(po->move.attach->fallinfo==NULL);
        #endif
        po->embedment->projectile = projectileAllocate();
    }
    else if ((po->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0)
    {
        po->projectile = projectileAllocate();

        if (po->projectile)
        {
            po->runtime_bitflags |= RUNTIMEBITFLAG_HASPROJECTILE;
        }
    }
}


void projectileSetSticky(PropRecord *prop)
{
    ObjectRecord *obj = prop->obj;
    Projectile *projectile = NULL;

    if (obj->runtime_bitflags & RUNTIMEBITFLAG_EMBEDDED)
    {
        projectile = obj->embedment->projectile;
    }
    else if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
    {
        projectile = obj->projectile;
    }

    if (projectile)
    {
        projectile->flags |= PROJECTILEFLAG_STICKY;
        if (prop->stan)
        {
            projectile->unkCC[0] = prop->stan->room;
            projectile->unkCC[1] = 0xFF;
            return;
        }
        projectile->unkCC[0] = 0xFFU;
    }
}


void embedmentFree(Embedment *embedment)
{
    embedment->flags |= EMBEDMENTFLAG_FREE;
}


Embedment *embedmentAllocate(void)
{
    s32 i;

    for (i = 0; i < EMBEDMENT_ARR_MAX; i++)
    {
        if (g_Embedments[i].flags & 1)
        {
            g_Embedments[i].flags = 0;
            g_Embedments[i].projectile = NULL;
            return &g_Embedments[(u32)i];
        }
    }

    return NULL;
}


/**
 * This doesn't exactly return the number of shots taken but it's the best way
 * to describe the behaviour of the function without writing a novel into the
 * function's name.
 *
 * The number returned is 0 when at full health and only ever increments as the
 * object takes damage. While healthy, the number scales from 0 to 4 based on
 * how close it is to being destroyed, where 4 is destroyed. After being
 * destroyed, the number increments at 1 per shot up to a max of 12.
 */

s32 objGetShotsTaken(ObjectRecord *obj)
{
    if (!(obj->state & PROPSTATE_DESTROYED))
    {
        return (obj->maxdamage * 3.0f) / obj->damage;
    }

    return obj->maxdamage + 4.0f;
}


/**
 * Return 0 if not destroyed
 * Return 1 if at destroyed level 1
 * Return 2 if at destroyed level 2
 * Return 3 if at destroyed level 3
 *
 * Each destroyed level is a new phase of visual brokenness. Typically the
 * object is destroyed and it looks broken (level 1), then after a couple of
 * shots it enters level 2, and a few shots later level 3.
 *
 * While healthy, damage goes from 0 to maxdamage (eg. 1000) but this function
 * returns 0 due to the if statement.
 *
 * When destroyed, damage is reset to 0 then incremented at one unit per shot,
 * so four shots causes it to enter a new destroyed level.
 */

s32 objGetDestroyedLevel(ObjectRecord *obj)
{
    if (!(obj->state & PROPSTATE_DESTROYED))
    {
        return 0; //if Not Dead
    }
    return ((s32) obj->maxdamage >> 2) + 1;
}


ModelRoData_BoundingBoxRecord *chrobjGetBboxFromObjFile(ModelFileHeader *obj)
{
    ModelNode *mdlnext;

    if (obj->RootNode->Child)
    {
        //for each next node, check for BBox
        for (mdlnext = obj->RootNode->Child; mdlnext; mdlnext = mdlnext->Next)
        {
            if (mdlnext->Opcode == MODELNODE_OPCODE_BBOX)
            {
                return mdlnext->Data;
            }
        }

        //none found, check FIRST child
        if (obj->RootNode->Child->Child)
        {
            //for each next node, check for BBox
            for (mdlnext = obj->RootNode->Child->Child; mdlnext; mdlnext = mdlnext->Next)
            {
                if (mdlnext->Opcode == MODELNODE_OPCODE_BBOX)
                {
                    return mdlnext->Data;
                }
            }
        }
    }
    return NULL;
}


struct ModelRoData_BoundingBoxRecord* chrobjGetBboxFromObjectRecord(ObjectRecord *arg0)
{
    return (struct ModelRoData_BoundingBoxRecord *)chrobjGetBboxFromObjFile(arg0->model->obj);
}


void set_color_shading_from_tile(PropRecord *prop, u8 col[4])
{
    s32 tmp;
    s32 min;
    s32 med;
    s32 max;
    s32 tmp2;
    s32 range;

    copy_tile_RGB_as_24bit(prop->stan, prop->pos.x, prop->pos.z, col);

    tmp = (col[0] * 79 + col[1] * 156 + col[2] * 21) >> 8;
    col[3] = (255 - tmp) * 0.75f;

    max = 0;
    min = 0;
	med = 0;

	if (col[1] > col[0]) {
		max = 1;
	} else {
		min = 1;
	}

	if (col[2] > col[max]) {
		med = max;
		max = 2;
	} else if (col[2] > col[min]) {
		med = 2;
	} else {
		med = min;
		min = 2;
	}

	if (col[max] > 0) {
		tmp2 = col[med] * (col[max] - col[min]) / col[max];
		range = col[max] - col[min];
		col[min] = 0;
		col[med] = tmp2;
		col[max] = range;
	}

    col[0] >>= 1;
    col[1] >>= 1;
    col[2] >>= 1;
}


/*
 * Address: 0x7F0402B4
*/
void sub_GAME_7F0402B4(PropRecord *prop, rgba_u8 *color)
{
    struct DoorRecord *door = prop->door;
    if (door->flags & 0x400 ){ return; }

    set_color_shading_from_tile(prop, color);
    color->r >>= 1;
    color->g >>= 1;
    color->b >>= 1;
}


void update_color_shading(rgba_u8 *dest, rgba_u8 *src)
{
    s32 val_diff;
    s32 val_new;
    s32 i;

    for (i = 0; i < 4; i++)
    {
        val_diff = (src->rgba[i] - dest->rgba[i]);
        val_new = dest->rgba[i] + ((val_diff + 7) >> 3);
        dest->rgba[i] = val_new;
    }
}


/*
 * Address: 0x7F040384
*/
void lerp_rgba_s32_with_rgba_f32(rgba_s32* dest, s32 enable, rgba_f32* src)
{
    if (enable == 1)
    {
#ifndef TARGET_N64
        /* PORT: Rare's lerp left distant props brighter than the fogged walls
         * behind them, so compose this RDP fog colour (G_RM_FOG_PRIM_A) to give
         * the prop the room fog F at its depth, with sA = dest->a:
         *   fogA = sA + F(1 - sA), FOGRGB = ((1 - F) sA shade + F fog) / fogA
         * GE_PROPFOG_LERP=1 restores Rare's lerp. */
        extern char *getenv(const char *);
        static int rarelerp = -1;
        if (rarelerp < 0)
        {
            rarelerp = getenv("GE_PROPFOG_LERP") != NULL;
        }
        if (!rarelerp)
        {
            f32 fogf = src->a;
            f32 sa = (f32) dest->a / 255.0f;
            f32 foga;
            f32 fr = src->r * 255.0f;
            f32 fg = src->g * 255.0f;
            f32 fb = src->b * 255.0f;

            if (fogf < 0.0f) fogf = 0.0f;
            if (fogf > 1.0f) fogf = 1.0f;
            foga = sa + fogf * (1.0f - sa);

            src->r = fr;
            src->g = fg;
            src->b = fb;

            if (foga > 0.0005f)
            {
                f32 ws = (1.0f - fogf) * sa / foga;
                f32 wf = fogf / foga;
                dest->r = (s32) (ws * dest->r + wf * fr);
                dest->g = (s32) (ws * dest->g + wf * fg);
                dest->b = (s32) (ws * dest->b + wf * fb);
            }
            else
            {
                dest->r = (s32) fr;
                dest->g = (s32) fg;
                dest->b = (s32) fb;
            }
            dest->a = (s32) (foga * 255.0f + 0.5f);
            if (dest->a > 255) dest->a = 255;
            return;
        }
#endif
        src->r *= 255.0f;
        src->g *= 255.0f;
        src->b *= 255.0f;

        if (1) { dest->r = (s32)((src->a * (src->r - dest->r)) + dest->r); }
        if (1) { dest->g = (s32)((src->a * (src->g - dest->g)) + dest->g); }
        if (1) { dest->b = (s32)((src->a * (src->b - dest->b)) + dest->b); }
        dest->a = (s32)((src->a * (255.0f - dest->a)) + dest->a);
    }
}


/**
 * Address 0x7F040484.
*/
void chrobjCollisionRelated(ObjectRecord *obj)
{
    struct ModelRoData_BoundingBoxRecord *bbox;
    Mtxf sp24;

    if (obj->ptr_allocated_collisiondata_block != NULL)
    {
        bbox = chrobjGetBboxFromObjectRecord(obj);
        matrix_4x4_copy(&obj->mtx, &sp24);
        matrix_4x4_set_position(&obj->runtime_pos, &sp24);
        sub_GAME_7F03F540(bbox, &sp24, &obj->ptr_allocated_collisiondata_block->polygon, obj->ptr_allocated_collisiondata_block);

        obj->ptr_allocated_collisiondata_block->bottom = obj->runtime_pos.f[1] + chrpropSumMatrixPosY(bbox, &sp24);
        obj->ptr_allocated_collisiondata_block->top = obj->runtime_pos.f[1] + chrpropSumMatrixNegY(bbox, &sp24);

        if (obj->type == PROPDEF_AIRCRAFT)
        {
            obj->ptr_allocated_collisiondata_block->bottom -= 200.0f;
        }
    }
}


PropRecord* objInit(ObjectRecord* obj, ModelFileHeader* model_header, PropRecord* prop, Model* model)
{
    if (prop == NULL)
    {
        prop = chrpropAllocate();
    }

    if (model == NULL)
    {
        if (obj->type == PROPDEF_AIRCRAFT)
        {
            model = modelmgrInstantiateModelWithAnim(model_header);
        }
        else
        {
            model = modelmgrInstantiateModel(model_header);
        }
    }

    if ((prop != NULL) && (model != NULL))
    {
        obj->model = model;
        obj->ptr_allocated_collisiondata_block = NULL;

        if (obj->flags & 0x100)
        {
            obj->ptr_allocated_collisiondata_block = mempAllocBytesInBank(0x50U, MEMPOOL_STAGE);
            obj->state = (u8) (obj->state | PROPSTATE_EXT_COLISION_BLOCK);
        }
        else
        {
            obj->state = (u8) (obj->state & 0xFFF7);
        }

        obj->prop = prop;
        obj->projectile = NULL;

        obj->shadecol.r = 0;
        obj->shadecol.g = 0;
        obj->shadecol.b = 0;
        obj->shadecol.a = 0;

        obj->nextcol.r = 0;
        obj->nextcol.g = 0;
        obj->nextcol.b = 0;
        obj->nextcol.a = 0;

        obj->maxdamage = 0.0f;
        *((s16*)&obj->model->unk00) = -1;
        obj->model->chr = NULL;
        modelSetScale(obj->model, PitemZ_entries[obj->obj].scale);
        prop->type = 1;
        prop->obj = obj;
        prop->pos.x = 0.0f;
        obj->runtime_pos.x = 0.0f;
        prop->pos.y = 0.0f;
        obj->runtime_pos.y = 0.0f;
        prop->pos.z = 0.0f;
        obj->runtime_pos.z = 0.0f;
        prop->stan = NULL;
    }
    else
    {
        if (model != NULL)
        {
            if (obj->type == PROPDEF_AIRCRAFT)
            {
                clear_aircraft_model_obj(model);
            }
            else
            {
                clear_model_obj(model);
            }
        }

        if (prop != NULL)
        {
            chrpropFree(prop);
            prop = NULL;
        }
    }

    return prop;
}


PropRecord* objInitWithModelDef(ObjectRecord* object, ModelFileHeader* header)
{
  return objInit(object, header, 0, 0);
}


PropRecord* objInitWithAutoModel(ObjectRecord* obj)
{
    return objInitWithModelDef(obj, PitemZ_entries[obj->obj].header);
}


// Changes the color shade on the object, e.g. when walking in a darker area or under a colored light.
void objChangeShading(ObjectRecord* obj, coord3d* pos, Mtxf* matrix, StandTile* stan) {

    PropRecord *prop = obj->prop;

    matrix_4x4_copy(matrix, &obj->mtx);

    obj->runtime_pos.x = prop->pos.x = pos->x;
    obj->runtime_pos.y = prop->pos.y = pos->y;
    obj->runtime_pos.z = prop->pos.z = pos->z;

    prop->stan = stan;

    sub_GAME_7F0402B4(obj->prop, &obj->nextcol);

    obj->shadecol.r = obj->nextcol.r;
    obj->shadecol.g = obj->nextcol.g;
    obj->shadecol.b = obj->nextcol.b;
    obj->shadecol.a = obj->nextcol.a;
}


// Unreferenced function (unused)
void sub_GAME_7F0407F4(ObjectRecord* obj, coord3d* pos, Mtxf* matrix, StandTile* stan)
{
    u32 a; // Adds 4 bytes to the stack so it matches. Could be anything 4 bytes long.
    struct ModelRoData_BoundingBoxRecord *modelunk = chrobjGetBboxFromObjFile(obj->model->obj);

    pos->y = stanGetPositionYValue(stan, pos->x, pos->z) + 4.0f;
    pos->y = pos->y - chrpropSumMatrixPosY(modelunk, matrix);

    objChangeShading(obj, pos, matrix, stan);
    chrobjCollisionRelated(obj);
}


//moveToPad
void sub_GAME_7F04088C(ObjectRecord *baseobj, struct coord3d *pos, Mtxf *matrix, StandTile *stan, struct coord3d *pos2)
{
    int padd[1];
    ModelRoData_BoundingBoxRecord *modelBoundingBox;
    f32 xmax;
    f32 ymin;
    coord3d newPos;
    StandTile *mStan;
    Mtxf mtxcopy;

    modelBoundingBox = chrobjGetBboxFromObjFile(baseobj->model->obj);
    xmax = chrpropBBOXGetYmin(modelBoundingBox);
    ymin = chrpropBBOXGetYmax(modelBoundingBox);
    mStan = stan;

    if (baseobj->flags & 4)
    {
        matrix_4x4_set_rotation_around_z(M_PI, &mtxcopy);
        matrix_4x4_multiply_in_place(matrix, &mtxcopy);
        newPos.x = pos2->f[0] - (mtxcopy.m[1][0] * ymin);
        newPos.y = pos2->f[1] - (mtxcopy.m[1][1] * ymin);
        newPos.z = pos2->f[2] - (mtxcopy.m[1][2] * ymin);
    }
    else if (baseobj->flags & 8)
    {
        matrix_4x4_copy(matrix, &mtxcopy);
        newPos.x = pos2->f[0] - (mtxcopy.m[1][0] * xmax);
        newPos.y = pos2->f[1] - (mtxcopy.m[1][1] * xmax);
        newPos.z = pos2->f[2] - (mtxcopy.m[1][2] * xmax);
    }
    else
    {
        ObjectRecord *roomObj;
        f32 distfromTileCenter;
        struct rect4f *byrefA;
        s32 byrefB;
        f32 byrefC;
        f32 byrefD;

        distfromTileCenter = stanGetPositionYValue(mStan, pos->f[0], pos->f[2]);

        matrix_4x4_copy(matrix, &mtxcopy);
        newPos.x = pos2->f[0] - (mtxcopy.m[1][0] * xmax);
        newPos.z = pos2->f[2] - (mtxcopy.m[1][2] * xmax);
        roomObj  = sub_GAME_7F03FAB0(pos, stan->room);

        if (roomObj)
        {
            PropRecord *roomObjProp = roomObj->prop;
            chraiGetCollisionBounds(roomObjProp, &byrefA, &byrefB, &byrefC, &byrefD);

            if ((distfromTileCenter < byrefC) && (byrefD < ((mtxcopy.m[1][1] * (ymin - xmax)) + distfromTileCenter + 4.0f)))
            {
                newPos.y = byrefC - (mtxcopy.m[1][1] * xmax);
                baseobj->runtime_bitflags |= RUNTIMEBITFLAG_00008000;
            }
            else
            {
                newPos.y = (distfromTileCenter - (mtxcopy.m[1][1] * xmax)) + 4.0f;
            }
        }
        else
        {
            newPos.y = (distfromTileCenter - (mtxcopy.m[1][1] * xmax)) + 4.0f;
        }
    }

    if (!(baseobj->flags2 & 1) && walkTilesBetweenPoints_NoCallback(&mStan, pos->f[0], pos->f[2], newPos.x, newPos.z))
    {
        objChangeShading(baseobj, &newPos, &mtxcopy, mStan);
    }
    else
    {
        objChangeShading(baseobj, pos, &mtxcopy, stan);
        if ((baseobj->flags2 & 1) || (baseobj->flags & 0x1000))
        {
            baseobj->runtime_pos.x = newPos.x;
            baseobj->runtime_pos.y = newPos.y;
            baseobj->runtime_pos.z = newPos.z;
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("prop not positioned correctly!\n");
        }
        #endif
    }

    chrobjCollisionRelated(baseobj);
}


void sub_GAME_7F040BA0(ObjectRecord *obj, coord3d *pos, Mtxf *arg2, StandTile *stan2, coord3d *pos2)
{
    Mtxf *sp6C_ptr;
    f32 (*sp6Cm_ptr)[4];
    f32 spBC;
    coord3d posdiff;
    StandTile *stan;
    Mtxf matrix;
    Mtxf sp2C;

    spBC = chrpropBBOXGetZmin(chrobjGetBboxFromObjFile(obj->model->obj));
    stan = stan2;
    sp6C_ptr = &matrix;

    matrix_4x4_set_rotation_around_x(4.712389f, sp6C_ptr);

    sp6Cm_ptr = matrix.m;

    matrix_4x4_set_rotation_around_y(M_PI_F, &sp2C);
    matrix_4x4_multiply_in_place(&sp2C, sp6C_ptr);
    matrix_4x4_multiply_in_place(arg2, &matrix);

    posdiff.x = pos2->x - (sp6Cm_ptr[2][0] * spBC);
    posdiff.y = pos2->y - (sp6Cm_ptr[2][1] * spBC);
    posdiff.z = pos2->z - (sp6Cm_ptr[2][2] * spBC);

    if ((!(((s32) obj->flags2) & 1)) && (walkTilesBetweenPoints_NoCallback(&stan, pos->x, pos->z, posdiff.x, posdiff.z) != 0))
    {
        objChangeShading(obj, &posdiff, &matrix, stan);
    }
    else
    {
        objChangeShading(obj, pos, &matrix, stan2);
        obj->runtime_pos.x = posdiff.x;
        obj->runtime_pos.y = posdiff.y;
        obj->runtime_pos.z = posdiff.z;
    }

    chrobjCollisionRelated(obj);
}


void objFreeEmbedmentOrProjectile(PropRecord *prop)
{
    ObjectRecord *obj = prop->obj;
    if (obj->runtime_bitflags & RUNTIMEBITFLAG_EMBEDDED)
    {
        if (obj->embedment)
        {
            if (obj->embedment->projectile)
            {
                projectileFree(obj->embedment->projectile);
            }
            #ifdef DEBUG
            else
            {
                osSyncPrintf("ERROR: PROPHIDD_ATTACHED was, but move.attach was NULL\a\n");
            osSyncPrintf("po->obj=%d\n", obj->obj);
                osSyncPrintf("p->flags=%08x\n", prop->flags);
                osSyncPrintf("po->flags2=%08x\n", obj->flags2);
                osSyncPrintf("p->timetoregen=%d\n", prop->timetoregen);
            }
            #endif

            embedmentFree(obj->embedment);
        }
        obj->embedment = NULL;
        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_EMBEDDED;
    }
    else if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
    {
        projectileFree(obj->projectile);
        obj->projectile = NULL;
        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_HASPROJECTILE;
    }
}


void objFree(ObjectRecord* obj, s32 freeprop, s32 canregen)
{
    PropRecord *child;

    if (obj->type == PROPDEF_AUTOGUN)
    {
        AutogunRecord* record = (AutogunRecord*)obj;
        if ((record->unkC4 != NULL) && (sndGetPlayingState(record->unkC4) != 0))
        {
            sndDeactivate(record->unkC4);
        }

        if ((record->unkC8 != NULL) && (sndGetPlayingState(record->unkC8) != 0))
        {
            sndDeactivate(record->unkC8);
        }
    }
    else if (obj->type == PROPDEF_COLLECTABLE)
    {
        WeaponObjRecord* record = (WeaponObjRecord*) obj;
        WeaponObjRecord* record2 = record->dualweapon;
        if (record2 != NULL)
        {
            record2->dualweapon = NULL;
            record->dualweapon = NULL;
        }
    }
    else if (obj->type == PROPDEF_DOOR)
    {
        DoorRecord* record = (DoorRecord*) obj;
        if ((record->openSoundState != NULL) && (sndGetPlayingState(record->openSoundState) != 0))
        {
            sndDeactivate(record->openSoundState);
        }

        if ((record->closeSoundState != NULL) && (sndGetPlayingState(record->closeSoundState) != 0))
        {
            sndDeactivate(record->closeSoundState);
        }
    }
    else if (obj->type == PROPDEF_TINTED_GLASS)
    {
        TintedGlassRecord* record = (TintedGlassRecord*) obj;
        if (record->portalnum >= 0)
        {
            bgToggleDataPortalsContrlBytes1Bit1(record->portalnum, 1);
        }
    }
    else if (obj->type == PROPDEF_AIRCRAFT)
    {
        AircraftRecord* record = (AircraftRecord*) obj;
        if ((record->Sound != NULL) && (sndGetPlayingState(record->Sound) != 0))
        {
            sndDeactivate(record->Sound);
        }
    }
    else if (obj->type == PROPDEF_VEHICHLE)
    {
        VehichleRecord* record = (VehichleRecord*) obj;
        if ((record->Sound != NULL) && (sndGetPlayingState(record->Sound) != 0))
        {
            sndDeactivate(record->Sound);
        }
    }

    if (obj->prop != NULL)
    {
        explosionClearBulletImpactRoomByFlag(obj->prop, FALSE);
        explosionClearBulletImpactRoomByFlag(obj->prop, TRUE);

        if (canregen == 0)
        {
            objFreeEmbedmentOrProjectile(obj->prop);

            if (obj->prop->parent != NULL)
            {
                objDetach(obj->prop);
            }

            chrpropDeregisterRooms(obj->prop);

            child = obj->prop->child;
            while (child)
            {
                PropRecord* next = child->prev;
                objFreePermanently(child->obj, TRUE);
                child = next;
            }

            if (obj->prop->type != PROP_TYPE_DOOR)
            {
                sub_GAME_7F050DE8(obj->model);
            }

            if (obj->type == PROPDEF_AIRCRAFT)
            {
                clear_aircraft_model_obj(obj->model);
            }
            else
            {
                clear_model_obj(obj->model);
            }

            if (freeprop != 0)
            {
                chrpropDelist(obj->prop);
                chrpropDisable(obj->prop);
                chrpropFree(obj->prop);
            }

            obj->prop = NULL;
        }
    }
}


void objFreePermanently(struct ObjectRecord * obj, bool freeprop)
{
    objFree(obj, freeprop, 0);
}


float objGetWidth(struct ObjectRecord * obj)
{
    if (obj->type == PROP_TYPE_WEAPON)
    {
        return 20.0f;
    }
    return 10.0f;
}


/**
 * Address: 7F041074
 */
bool projectileTestPropBoundingSphere(coord3d *zeropos, coord3d *pos, coord3d *vec, f32 scale)
{
    vec3d vector;
    f32 dist2rd;

    vector.x = vec->x - zeropos->x;
    vector.y = vec->y - zeropos->y;
    vector.z = vec->z - zeropos->z;

    dist2rd = pos->f[0] * vector.f[0] + pos->f[1] * vector.f[1] + pos->f[2] * vector.f[2];

    if (dist2rd > 0) {
        f32 a = pos->f[0] * pos->f[0] + pos->f[1] * pos->f[1] + pos->f[2] * pos->f[2];
        f32 b = vector.f[0] * vector.f[0] + vector.f[1] * vector.f[1] + vector.f[2] * vector.f[2];

        if ((b - scale * scale) * a <= dist2rd * dist2rd) {
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Address: 7F041160
 */
bool projectileLineTestModel(ObjectRecord *obj, coord3d *modelRayOrigin, coord3d *modelRayDir, coord3d *hitPos, coord3d *hitNormal, Model **hitModel, ModelNode **hitNode)
{
    bool found;
    Mtxf *mtx;
    ModelNode *node;
    struct HitThing hitthing;
    s32 mtxindex;
    ModelNode *hitnode;
    Model *model;

    model = obj->model;
    found = 0;
    node = NULL;
    hitnode = NULL;

    // Fast path: test door bounding box.
    if (obj->type == PROPDEF_DOOR) 
    {
        found = modelTestRayIntersectsTransformedBBox(&((DoorRecord *)obj)->bbox, &model->render_pos[0].pos, modelRayOrigin, modelRayDir);
        node = model->obj->RootNode;
        if (found > 0) 
        {
            if (propobjFindHit(model, node, modelRayOrigin, modelRayDir, &hitthing, &mtxindex, &hitnode) == 0) 
            {
                found = 0;
            }
        }
    // General case: traverse model's nodes for collision.
    } 
    else 
    {
        do 
        {
            found = modelFindNextProjectileHitCandidate(model, modelRayOrigin, modelRayDir, &node);

            if (found > 0) 
            {
                bool hit = propobjFindHit(model, node, modelRayOrigin, modelRayDir, &hitthing, &mtxindex, &hitnode);
                if (hit != 0) 
                {
                    break;
                }
            }
        } while (found > 0);
    }

    if (found > 0) 
    {
        mtx = &model->render_pos[mtxindex].pos;

        hitPos->x = hitthing.hitpos.x;
        hitPos->y = hitthing.hitpos.y;
        hitPos->z = hitthing.hitpos.z;

        mtx4TransformVecInPlace(mtx, hitPos);
        mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), hitPos);

        hitNormal->x = hitthing.normal.x;
        hitNormal->y = hitthing.normal.y;
        hitNormal->z = hitthing.normal.z;

        mtx4RotateVecInPlace(mtx, hitNormal);

        if (hitNormal->f[0] * modelRayDir->f[0] + hitNormal->f[1] * modelRayDir->f[1] + hitNormal->f[2] * modelRayDir->f[2] > 0.0f) 
        {
            hitNormal->x = -hitNormal->x;
            hitNormal->y = -hitNormal->y;
            hitNormal->z = -hitNormal->z;
        }

        mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), hitNormal);

        if (hitNormal->x != 0.0f || hitNormal->y != 0.0f || hitNormal->z != 0.0f) 
        {
            guNormalize(&hitNormal->x, &hitNormal->y, &hitNormal->z);
        } 
        else 
        {
            hitNormal->z = 1.0f;
        }

        *hitModel = model;
        *hitNode = hitnode;
        return TRUE;
    }

    return FALSE;
}


bool sub_GAME_7F041400(PropRecord *prop, coord3d *rayStart, coord3d *rayEnd, coord3d *rayDir, coord3d *hitPos, coord3d *hitNormal, f32 *hitDist)
{
    struct rect4f *polygon;
    s32 numedges;
    f32 ymax;
    f32 ymin;
    s32 pad;
    s32 pad2;
    s32 i;
    f32 bestfrac;
    coord2d edgeStart2d;
    coord2d edgeEnd2d;
    coord2d rayStart2d;
    coord2d rayEnd2d;
    s32 bestedge;
    coord3d edgeStart3d;
    coord3d edgeEnd3d;
    f32 dist;
    coord3d intersection;
    s32 next;

    bestfrac = 1.0f;
    bestedge = -1;
    chraiGetCollisionBounds(prop, &polygon, &numedges, &ymax, &ymin);

    if (numedges > 0)
    {
        if (!(((ymax < rayStart->y) && (ymax < rayEnd->y)) || ((rayStart->y < ymin) && (rayEnd->y < ymin))))
        {
            rayStart2d.x = rayStart->x;
            rayStart2d.y = rayStart->z;
            rayEnd2d.x = rayEnd->x;
            rayEnd2d.y = rayEnd->z;
            for (i = 0; i < numedges; i++)
            {
                next = (i + 1) % numedges;
                if (doSegmentsIntersect(rayStart->x, rayStart->z, rayEnd->x, rayEnd->z, polygon->points[i].x, polygon->points[i].y, polygon->points[next].x, polygon->points[next].y))
                {
                    edgeStart2d.x = polygon->points[i].x;
                    edgeStart2d.y = polygon->points[i].y;
                    edgeEnd2d.x = polygon->points[next].x;
                    edgeEnd2d.y = polygon->points[next].y;
                    dist = calculateSegmentIntersectionFraction(&rayStart2d, &rayEnd2d, &edgeStart2d, &edgeEnd2d);

                    if (dist < bestfrac)
                    {
                        bestfrac = dist;
                        bestedge = i;
                    }
                }
            }
            if (bestedge > 0)
            {
                next = (bestedge + 1) % numedges;
                edgeStart3d.x = polygon->points[bestedge].x;
                edgeStart3d.y = 0.0f;
                edgeStart3d.z = polygon->points[bestedge].y;
                edgeEnd3d.x = polygon->points[next].x;
                edgeEnd3d.y = 0.0f;
                edgeEnd3d.z = polygon->points[next].y;

                chrlvLineLineIntersection(&edgeStart3d, &edgeEnd3d, rayStart, rayDir, &intersection);
                dist = (rayDir->z * (intersection.z - rayStart->z)) + (((intersection.x - rayStart->x) * rayDir->x) + ((intersection.y - rayStart->y) * rayDir->y));

                if (dist < (*hitDist))
                {
                    *hitDist = dist;
                    hitPos->x = intersection.x;
                    hitPos->y = intersection.y;
                    hitPos->z = intersection.z;
                    hitNormal->x = -rayDir->x;
                    hitNormal->y = 0.0f;
                    hitNormal->z = -rayDir->z;

                    if ((hitNormal->x != 0.0f) || (hitNormal->z != 0.0f))
                    {
                        guNormalize(&hitNormal->x, &hitNormal->y, &hitNormal->z);
                    } else
                    {
                        hitNormal->z = 1.0f;
                    }

                    D_80030B0C = prop;
                    bodypartshot = HIT_NULL_PART;
                    g_CurrentProjectileModel = NULL;
                    dword_CODE_bss_80075B74 = NULL;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}


/**
 * Address: 7F0417DC
 *
 * Test a single object for collision between the object and a projectile.
 * If there is a collision, update hit parameters with collision data.
 * @return TRUE if the object and projectile collide, FALSE otherwise.
 */
bool projectileTestObjectCollision(ObjectRecord *obj, coord3d *worldRayOrigin, coord3d *worldRayEnd, coord3d *worldRayDir, f32 maxDist, coord3d *modelRayOrigin, coord3d *modelRayDir, coord3d *hitPos, coord3d *hitNormal, f32 *hitDist, Model **hitModel, ModelNode **hitNode)
{
    Model *modelstack[1];
    f32 instsize;
    f32 value;
    f32 dx;
    f32 dy;
    f32 dz;
    PropRecord *prop;

    instsize = getinstsize(modelstack[0] = obj->model);
    prop = obj->prop;
    value = 0.0f;

    if (prop->parent == NULL) {
        dx = obj->runtime_pos.x - worldRayOrigin->x;
        dy = obj->runtime_pos.y - worldRayOrigin->y;
        dz = obj->runtime_pos.z - worldRayOrigin->z;
        value = (dz * worldRayDir->z) + ((dx * worldRayDir->x) + (dy * worldRayDir->y));
    }

    if (-instsize <= value) {
        if (value <= maxDist + instsize) {
            // Precise line test for on screen props.
            if (prop->flags & PROPFLAG_ONSCREEN) {
                if (projectileLineTestModel(obj, modelRayOrigin, modelRayDir, hitPos, hitNormal, hitModel, hitNode)) {
                    dx = hitPos->x - worldRayOrigin->x;
                    dy = hitPos->y - worldRayOrigin->y;
                    dz = hitPos->z - worldRayOrigin->z;
                    value = (dz * worldRayDir->z) + ((dx * worldRayDir->x) + (dy * worldRayDir->y));

                    if (0.0f <= value) {
                        if (value <= maxDist) {
                            *hitDist = value;
                            return TRUE;
                        }
                    }
                }
            // Cheaper bounds test for off screen props.
            } else {
                prop = obj->prop;
                instsize = getinstsize(modelstack[0]);

                if (projectileTestPropBoundingSphere(worldRayOrigin, worldRayDir, &obj->runtime_pos, instsize)) {
                    *hitDist = maxDist;

                    if (sub_GAME_7F041400(prop, worldRayOrigin, worldRayEnd, worldRayDir, hitPos, hitNormal, hitDist))
                    {
                        *hitModel = modelstack[0];
                        *hitNode = *(ModelNode **)modelstack[0]->obj;
                        return TRUE;
                    }
                }
            }
        }
    }

    return FALSE;
}


/**
 * Address: 7F0419E4
 *
 * Tests an object and its on screen child hierarchy for projectile collision.
 * If a closer hit is found, updates the caller's hit collision data.
 * @returns TRUE if this object or one of its recursive children produced a closer hit, FALSE otherwise.
 */
bool projectileTestObjectCollisionRecursive(ObjectRecord *obj, coord3d *worldRayOrigin, coord3d *worldRayEnd, coord3d *worldRayDir, f32 maxDist, coord3d *modelRayOrigin, coord3d *modelRayDir, coord3d *bestHitPos, coord3d *bestHitNormal, f32 *bestHitDist)
{
    coord3d hitPos;
    coord3d hitNormal;
    f32 hitDist;
    Model *hitModel;
    ModelNode *hitNode;
    bool found;
    PropRecord *prop;
    PropRecord *child;

    prop = obj->prop;
    found = FALSE;

    if (projectileTestObjectCollision(obj, worldRayOrigin, worldRayEnd, worldRayDir, maxDist, modelRayOrigin, modelRayDir, &hitPos, &hitNormal, &hitDist, &hitModel, &hitNode))
    {
        if (hitDist < *bestHitDist)
        {
            *bestHitDist = hitDist;

            bestHitPos->x = hitPos.x;
            bestHitPos->y = hitPos.y;
            bestHitPos->z = hitPos.z;

            bestHitNormal->x = hitNormal.x;
            bestHitNormal->y = hitNormal.y;
            bestHitNormal->z = hitNormal.z;

            D_80030B0C = obj->prop;
            bodypartshot = -1;
            g_CurrentProjectileModel = hitModel;
            dword_CODE_bss_80075B74 = hitNode;

            found = TRUE;
        }
    }

    if (prop->flags & PROPFLAG_ONSCREEN)
    {
        child = prop->child;

        while (child != NULL)
        {
            if (child->flags & PROPFLAG_ONSCREEN)
            {
                if (projectileTestObjectCollisionRecursive(child->obj, worldRayOrigin, worldRayEnd, worldRayDir, maxDist, modelRayOrigin, modelRayDir, bestHitPos, bestHitNormal, bestHitDist))
                {
                    found = TRUE;
                }
            }

            child = child->prev;
        }
    }

    return found;
}




bool sub_GAME_7F041BB8(ChrRecord *chr, coord3d *arg1, coord3d *arg2, f32 arg3, coord3d *arg4, coord3d *arg5, coord3d *arg6, coord3d *arg7, f32 *arg8) {
    f32 instSize;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 planeDist;
    f32 *new_var2;
    f32 hitDist;
    f32 pad;
    s32 bodyPart;
    Model *model;
    ModelNode *node;
    ModelHitEntry *entry;
    Mtxf *mtx;
    PropRecord *prop;

    prop = chr->prop;
    instSize = getinstsize(chr->model);
    dx = prop->pos.x - arg1->x;
    dy = prop->pos.y - arg1->y;
    dz = prop->pos.z - arg1->z;
    planeDist = (dz * arg2->z) + ((dx * arg2->x) + (dy * arg2->y));

    if ((-instSize <= planeDist) && (planeDist <= (arg3 + instSize)) && (prop->flags & PROPFLAG_ONSCREEN)) {
        entry = chr->field_20;
        bodyPart = sub_GAME_7F06C010(&entry, arg4, arg5, &model, &node);
        if (bodyPart > 0) {
            mtx = modelFindNodeMtx(model, node, 0);
            dx = mtx->m[3][0] - arg4->x;
            dy = mtx->m[3][1] - arg4->y;
            new_var2 = mtx->m[3];
            dz = new_var2[2] - arg4->z;
            hitDist = (dz * arg5->z) + ((dx * arg5->x) + (dy * arg5->y));
            if (hitDist < *arg8) {
                *arg8 = hitDist;
                arg6->x = (arg2->x * hitDist) + arg1->x;
                arg6->y = (arg2->y * hitDist) + arg1->y;
                arg6->z = (arg2->z * hitDist) + arg1->z;
                arg7->x = -arg2->x;
                arg7->y = 0.0f;
                arg7->z = -arg2->z;
                if ((arg7->x != 0.0f) || (arg7->z != 0.0f)) {
                    guNormalize(&arg7->x, &arg7->y, &arg7->z);
                } else {
                    arg7->z = 1.0f;
                }
                D_80030B0C = prop;
                bodypartshot = bodyPart;
                g_CurrentProjectileModel = model;
                dword_CODE_bss_80075B74 = node;
                return 1;
            }
        }
    }
    return 0;
}


bool projectileFindCollidingProp(PropRecord *ignoreProp, coord3d *worldRayStart, coord3d *worldRayEnd, u32 cdtypes, coord3d *outHitPos, coord3d *outHitNormal, s32 *rooms)
{
    bool result;
    f32 dist;
    s16 *propnumptr;
    f32 spa8;
    bool found_collision;
    coord3d sp98;
    ChrRecord *chr;
    coord3d sp88;
    coord3d sp7c;
    PropRecord *iterprop;
    PropRecord *playerstank;
    ObjectRecord *obj;
    s32 unused;

    result = FALSE;
    found_collision = FALSE;
    playerstank = get_ptr_for_players_tank();

    sp98.x = worldRayEnd->x - worldRayStart->x;
    sp98.y = worldRayEnd->y - worldRayStart->y;
    sp98.z = worldRayEnd->z - worldRayStart->z;

    dist = sqrtf(sp98.f[0] * sp98.f[0] + sp98.f[1] * sp98.f[1] + sp98.f[2] * sp98.f[2]);

    if (dist == 0.0f)
    {
        return FALSE;
    }

    sp98.x *= (1.0f / dist);
    sp98.y *= (1.0f / dist);
    sp98.z *= (1.0f / dist);

    sp88.x = worldRayStart->x;
    sp88.y = worldRayStart->y;
    sp88.z = worldRayStart->z;

    mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &sp88);

    sp7c.x = sp98.x;
    sp7c.y = sp98.y;
    sp7c.z = sp98.z;

    mtx4RotateVecInPlace(camGetWorldToScreenMtxf(), sp7c.f);

    spa8 = dist;

    if (cdtypes != 0)
    {
        roomGetProps(rooms);

        for (propnumptr = ptr_list_object_lookup_indices; *propnumptr >= 0; propnumptr++)
        {
            iterprop = &g_Props[*propnumptr];

            if (iterprop != ignoreProp)
            {
                if (iterprop->type == PROP_TYPE_OBJ
                        || iterprop->type == PROP_TYPE_WEAPON
                        || iterprop->type == PROP_TYPE_DOOR)
                {
                    obj = iterprop->obj;

                    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_ISRETICK) == 0 && (obj->flags2 & PROPFLAG2_THROWTHROUGH) == 0) {
                        if (iterprop->type == PROP_TYPE_DOOR)
                        {
                            if ((cdtypes & CDTYPE_DOORS) == 0 && (propDoorGetCdTypes(iterprop) & cdtypes) == 0)
                            {
                                continue;
                            }
                        }
                        else
                        {
                            if ((cdtypes & CDTYPE_OBJS) == 0)
                            {
                                continue;
                            }
                        }

                        if ((iterprop != playerstank) || !(obj->state & PROPSTATE_20))
                        {
                            if (projectileTestObjectCollisionRecursive(obj, worldRayStart, worldRayEnd, &sp98, dist, &sp88, &sp7c, outHitPos, outHitNormal, &spa8))
                            {
                                found_collision = TRUE;
                            }
                        }
                    }
                } else if (iterprop->type == PROP_TYPE_CHR
                        || (iterprop->type == PROP_TYPE_VIEWER && iterprop->chr))
                {
                    chr = iterprop->chr;

                    if (iterprop->type == PROP_TYPE_VIEWER)
                    {
                        if (!g_playerPointers[getPlayerPointerIndex(iterprop)]->field_AC || (cdtypes & CDTYPE_PLAYERS) == 0)
                        {
                            continue;
                        }
                    }
                    else if (iterprop->type == PROP_TYPE_CHR)
                    {
                        if ((chr->hidden & CHRHIDDEN_MOVING) || (cdtypes & CDTYPE_CHRS) == 0)
                        {
                            continue;
                        }
                    }

                    if (sub_GAME_7F041BB8(chr, worldRayStart, &sp98, dist, &sp88, &sp7c, outHitPos, outHitNormal, &spa8))
                    {
                        found_collision = TRUE;
                    }
                }
                else if (iterprop->type == PROP_TYPE_VIEWER && g_playerPointers[getPlayerPointerIndex(iterprop)]->field_AC)
                {
                    if (sub_GAME_7F041400(iterprop, worldRayStart, worldRayEnd, &sp98, outHitPos, outHitNormal, &spa8))
                    {
                        found_collision = TRUE;
                    }
                }
            }
        }
    }

    if (found_collision)
    {
        result = TRUE;

        flt_CODE_bss_80075B78.x = sp98.x;
        flt_CODE_bss_80075B78.y = sp98.y;
        flt_CODE_bss_80075B78.z = sp98.z;

        flt_CODE_bss_80075B88.x = sp7c.x;
        flt_CODE_bss_80075B88.y = sp7c.y;
        flt_CODE_bss_80075B88.z = sp7c.z;

    }

    return result;
}


/**
 * this function contains 
 * osSyncPrintf("stanLineObjGfx: %d rooms is more than %d\n",arg0+0x58,20);
 */
s32 handles_projectile_motion(struct ObjectRecord *arg0, coord3d *arg1, coord3d *arg2, coord3d *arg3, s32 arg4, s32 arg5)
{
    PropRecord *prop;
    StandTile *tile;
    s32 result;
    HitThing hit;
    coord3d endpos;
    s32 i;
    s32 roomCount;
    s32 roomNums[121];
    u8 roomSet[8];
    coord3d *hitpos;
    coord3d diff;
    struct ObjectRecord *obj;
    s32 *roomPtr;
    f32 dist;
    f32 factor;
    f32 scale;

    prop = arg0->prop;
    tile = prop->stan;
    result = 1;
    D_80030B0C = NULL;
    obj = arg0;
    hitpos = arg2;

    if (((obj->runtime_pos.x == arg1->x) && (obj->runtime_pos.y == arg1->y)) && (obj->runtime_pos.z == arg1->z))
    {
        goto end;
    }

    endpos.x = arg1->x;
    endpos.y = arg1->y;
    endpos.z = arg1->z;

    if ((!(obj->runtime_bitflags & RUNTIMEBITFLAG_00000080)) || (!(obj->projectile->flags & PROPFLAG_ENABLED)))
    {
        goto end;
    }

    roomCount = 0;
    bgFindRoomsAlongSegment(&obj->runtime_pos, &endpos, obj->projectile->unkCC, roomSet, roomNums, &roomCount, 20);

    if (roomCount > 20)
    {
        roomCount = 20;
    }

    i = bgCopyVisibleRoomsToList(&roomNums[roomCount], 100);
    roomCount = roomCount + i;
    roomNums[roomCount] = -1;

    if (roomNums[0] < 0)
    {
        goto after_bg_loop;
    }

    roomPtr = roomNums;

bg_loop:
    if (bgTestBulletHitBackground(&arg0->runtime_pos, &endpos, *roomPtr, &hit))
    {
        scale = get_room_data_float2();

        // Keep this for matching.
        if (roomPtr && roomPtr && roomPtr);

        hit.hitpos.x *= scale;
        hit.hitpos.y *= scale;
        hit.hitpos.z *= scale;

        if ((((((arg0->runtime_pos.x <= endpos.x) && (hit.hitpos.x <= endpos.x)) && (arg0->runtime_pos.x <= hit.hitpos.x))
           || (((endpos.x <= obj->runtime_pos.x) && (endpos.x <= hit.hitpos.x)) && (hit.hitpos.x <= obj->runtime_pos.x)))
           && ((((obj->runtime_pos.y <= endpos.y) && (hit.hitpos.y <= endpos.y)) && (arg0->runtime_pos.y <= hit.hitpos.y))
           || (((endpos.y <= obj->runtime_pos.y) && (endpos.y <= hit.hitpos.y)) && (arg0->runtime_pos.y >= hit.hitpos.y))))
           && ((((obj->runtime_pos.z <= endpos.z) && (hit.hitpos.z <= endpos.z)) && (arg0->runtime_pos.z <= hit.hitpos.z))
           || (((endpos.z <= arg0->runtime_pos.z) && (endpos.z <= hit.hitpos.z)) && (hit.hitpos.z <= obj->runtime_pos.z))))
        {
            if (!(((arg0->runtime_pos.x == ((0, hit.hitpos)).x)  && (obj->runtime_pos.y == ((0, hit.hitpos)).y)) && (arg0->runtime_pos.z == ((0, hit.hitpos)).z)))
            {
                result = 0;

                hitpos->x = hit.hitpos.x;
                hitpos->y = hit.hitpos.y;
                hitpos->z = hit.hitpos.z;

                arg3->x = hit.normal.x;
                arg3->y = hit.normal.y;
                arg3->z = hit.normal.z;

                if (((arg3->x != 0.0f) || (arg3->y != 0.0f)) || (arg3->z != 0.0f))
                {
                    guNormalize(&arg3->x, &arg3->y, &arg3->z);
                }
                else
                {
                    arg3->z = 1.0f;
                }
            }
        }
    }

    roomPtr++;

    if ((*roomPtr) >= 0)
    {
        goto bg_loop;
    }

after_bg_loop:
    if (result == 0)
    {
        diff.x = arg1->x - arg0->runtime_pos.x;
        diff.y = arg1->y - arg0->runtime_pos.y;
        diff.z = arg1->z - arg0->runtime_pos.z;

        dist = sqrtf((diff.z * diff.z) + ((diff.x * diff.x) + (diff.y * diff.y)));

        if (&diff);

        if (0.1f < dist)
        {
            factor = 0.1f;
            factor = factor / dist;
        }
        else
        {
            factor = 0.5f;
        }

        hitpos->x -= factor * diff.x;
        hitpos->y -= factor * diff.y;
        hitpos->z -= factor * diff.z;

        endpos.x = hitpos->x;
        endpos.y = hitpos->y;
        endpos.z = hitpos->z;
    }

    if (!projectileFindCollidingProp(prop, &obj->runtime_pos, &endpos, CDTYPE_ALL & ~CDTYPE_BG, hitpos, arg3, roomNums))
    {
        if ((result == 0) && (arg4 != 0))
        {
            result = 2;

            if (arg5 == 0)
            {
                bgFindRoomsAlongSegment(&arg0->runtime_pos, hitpos, obj->projectile->unkCC, roomSet, roomNums, &roomCount, 20);
            }
        }
        else if ((result == 1) || (arg5 == 0))
        {
            if (result != 1)
            {
                bgFindRoomsAlongSegment(&arg0->runtime_pos, &endpos, obj->projectile->unkCC, roomSet, roomNums, &roomCount, 20);
            }

            obj->runtime_pos.x = endpos.x;
            obj->runtime_pos.z = endpos.z;
            prop->pos.y = (dist = ((0, endpos)).y);
            arg0->runtime_pos.y = dist;
        }
    }
    else if (arg5 == 0)
    {
        // Keep if (1) for matching.
        if (1)
        {
            endpos.x = obj->runtime_pos.x;
            endpos.z = arg0->runtime_pos.z;

            bgFindRoomsAlongSegment(&arg0->runtime_pos, &endpos, obj->projectile->unkCC, roomSet, roomNums, &roomCount, 20);

            dist = endpos.y;
        }

        prop->pos.y = dist;
        obj->runtime_pos.y = dist;

        if (arg4)
        {
            result = 2;
        }
        else
        {
            result = 0;
        }
    }

    if ((result != 1) && (arg5 != 0))
    {
        goto end;
    }

    i = 0;

    if (!(obj->projectile->flags & PROPFLAG_00000008))
    {
        tile = prop->stan;

        if ((walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, obj->runtime_pos.x, arg0->runtime_pos.z) == TRUE) && (tile != NULL))
        {
            prop->stan = tile;
            prop->pos.x = arg0->runtime_pos.x;
            prop->pos.z = obj->runtime_pos.z;
        }
        else
        {
            obj->projectile->flags |= PROPFLAG_00000008;
            prop->flags |= PROPFLAG_00000008;
        }
    }

    if (obj->projectile->flags & PROPFLAG_00000008)
    {
        tile = stanFindTileBelowPos(&obj->runtime_pos, roomSet, NULL);

        if (tile != NULL)
        {
            prop->stan = tile;
            prop->pos.x = obj->runtime_pos.x;
            prop->pos.z = obj->runtime_pos.z;
            obj->projectile->flags &= ~PROPFLAG_00000008;
            prop->flags &= ~PROPFLAG_00000008;
        }
    }

    if (roomSet[0] != 0xff)
    {
        do
        {
            obj->projectile->unkCC[i] = roomSet[i];
            i++;
        }
        while ((roomSet[i] != 0xff) && (i != 7));
    }

    arg0->projectile->unkCC[i] = 0xff;

end:
    return result;
}


/**
 * Address: 7F042A0C
 */
s32 objTryMovePropWithCollision(ObjectRecord *obj, coord3d *targetpos, coord3d *arg2, coord3d *arg3, s32 arg4)
{
    PropRecord *prop;
    StandTile *stan;
    f32 width;
    s32 result;
    coord3d dir;
    coord3d target;
    f32 tmp;
    f32 ymax;
    f32 ymin;
    coord3d edgeA;
    coord3d edgeB;
    coord3d partialpos;
    coord3d rayinfo;
    coord2d edgeStart;
    coord2d edgeEnd;
    coord2d dir2d;
    f32 *new_var;
    s32 pad;

    prop = obj->prop;
    stan = prop->stan;
    width = objGetWidth(obj);
    result = 1;
    D_80030B0C = NULL;

    // Early return if target position and object's current position are the same.
    if (((targetpos->f[0] == prop->pos.f[0]) && (targetpos->f[1] == prop->pos.f[1])) && (targetpos->f[2] == prop->pos.f[2]))
    {
        goto done;
    }

    target.f[0] = targetpos->f[0];
    target.f[1] = targetpos->f[1];
    target.f[2] = targetpos->f[2];

    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_00000080) == 0)
    {
        goto done;
    }

    tmp = stanGetPositionYValue(prop->stan, prop->pos.x, prop->pos.z);

    if ((obj->embedment->flags & PROJECTILEFLAG_00000040) == 0)
    {
        ymax = 0.0f;
        ymin = 1.0f;
    }
    else if (target.f[1] < prop->pos.y)
    {
        ymax = prop->pos.y - tmp;
        ymin = target.f[1] - tmp;
    }
    else
    {
        ymax = target.f[1] - tmp;
        ymin = prop->pos.y - tmp;
    }

    stanResetHits();

    if ((stanTestLineUnobstructed(&stan, prop->pos.x, prop->pos.z, target.f[0], target.f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, ymax, ymin, 0.0f, 1.0f) != 0) && (stan != NULL))
    {
        if (stanTestVolume(&stan, target.f[0], target.f[2], width, 0x1f, ymax, ymin) < 0)
        {
            obj->runtime_pos.x = target.f[0];
            obj->runtime_pos.z = target.f[2];
            prop->stan = stan;
            prop->pos.x = target.f[0];
            prop->pos.z = target.f[2];
            tmp = target.f[1];
            prop->pos.y = tmp;
            obj->runtime_pos.y = tmp;
            goto done;
        }
    }

    getCollisionEdge_maybe(&edgeA, &edgeB);

    arg3->x = edgeB.z - edgeA.z;
    arg3->y = 0.0f;
    arg3->z = edgeA.x - edgeB.x;

    if (arg3->x != 0.0f)
    {
        goto normalize;
    }

    if (arg3->z == 0.0f)
    {
        goto unitz;
    }

normalize:
    guNormalize(&arg3->x, &arg3->y, &arg3->z);
    goto normaldone;

unitz:
    arg3->z = 1.0f;

normaldone:
    if (((target.f[0] != prop->pos.f[0]) || (target.f[1] != prop->pos.f[1])) || (target.f[2] != prop->pos.f[2]))
    {
        dir.x = target.f[0] - prop->pos.x;
        dir.y = target.f[1] - prop->pos.y;
        dir.z = target.f[2] - prop->pos.z;

        chrlvStanLineDirIntersection(&prop->pos, &dir, arg2);

        rayinfo.x = width;
        rayinfo.y = prop->pos.x;
        rayinfo.z = prop->pos.z;

        edgeStart.x = edgeA.x;
        edgeStart.y = edgeA.z;
        edgeEnd.x = edgeB.x;
        edgeEnd.y = edgeB.z;

        dir2d.x = target.f[0] - prop->pos.x;
        dir2d.y = target.f[2] - prop->pos.z;

        new_var = &partialpos.z;
        tmp = calculateRayToSegmentIntersectionNormalized(&rayinfo, &edgeStart, &edgeEnd, &dir2d);

        stan = prop->stan;

        partialpos.x = prop->pos.x + ((dir2d.x * tmp) * 0.99000001f);
        partialpos.y = target.f[1];
        partialpos.z = prop->pos.z + ((dir2d.y * tmp) * 0.99000001f);

        if ((stanTestLineUnobstructed(&stan, prop->pos.x, prop->pos.z, partialpos.x, *new_var, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, ymax, ymin, 0.0f, 1.0f) != 0) && (stan != NULL))
        {
            if (stanTestVolume(&stan, partialpos.x, partialpos.z, width, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, ymax, ymin) < 0)
            {
                obj->runtime_pos.x = partialpos.x;
                obj->runtime_pos.z = partialpos.z;
                prop->stan = stan;
                prop->pos.x = partialpos.x;
                prop->pos.z = partialpos.z;
            }
        }
    }
    else
    {
        arg2->x = target.f[0];
        arg2->y = target.f[1];
        arg2->z = target.f[2];
    }

    tmp = partialpos.y;
    prop->pos.y = tmp;
    obj->runtime_pos.y = tmp;
    result = 0;

done:
    return result;
}


/**
 * US address 7F042EB4.
*/
s32 sub_GAME_7F042EB4(struct ObjectRecord *arg0, f32 *arg1, struct coord3d *arg2, struct coord3d *arg3, s32 arg4, s32 arg5)
{
    if ((arg0->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && (arg0->projectile->flags & PROJECTILEFLAG_STICKY))
    {
        return handles_projectile_motion(arg0, arg1, arg2, arg3, arg4, arg5);
    }

    return objTryMovePropWithCollision(arg0, arg1, arg2, arg3, arg4);
}


/**
 * Update a speed and distance travelled, factoring in acceleration,
 * deceleration and the global update multiplier.
 *
 * The new speed and distance done are written back to those pointers.
 *
 * offsets: 077A48, 7F042F18
 * (copied from Perfect Dark)
 */
#ifndef TARGET_N64
/* Set by chrlvApplySpeed around its call: chr steering runs once per whole
 * field, so field-paced callers integrate the action tick's whole fields rather
 * than portAnimTicks() (which made guards under-turn and drift off patrol paths
 * at high tick rates). Per-tick callers (doors, lifts, autoguns) keep sub-field
 * smoothing; at tickhz 60 and in demo replay both are the same. */
s32 g_portApplySpeedFieldPaced; /* 2 = step exactly one whole field */
#endif

void chrobjApplySpeed(f32 *openPosition, f32 maxFrac, f32 *speedPtr, f32 accel, f32 decel, f32 maxSpeed)
{
    f32 speed = *speedPtr;
    s32 i;
#ifndef TARGET_N64
    /* Sub-field smoothing: whole fields step exactly as vanilla (s == 1),
     * then one trailing step scaled by the fractional remainder, so doors
     * and lifts move every tick instead of only on field ticks. */
    f32 ticks = (g_portApplySpeedFieldPaced > 1) ? 1.0f
              : g_portApplySpeedFieldPaced ? (f32) g_ClockTimer : portAnimTicks();
    s32 whole = (s32) ticks;
    f32 frac = ticks - (f32) whole;
    s32 steps = whole + ((frac > 0.0f) ? 1 : 0);
#define SPD(x) ((x) * s)
#else
#define steps g_ClockTimer
#define SPD(x) (x)
#endif

    for (i = 0; i < steps; i++)
    {
#ifndef TARGET_N64
        f32 s = (i < whole) ? 1.0f : frac;
#endif
        f32 limit = speed * speed * 0.5f / decel;
        f32 distRemaining = maxFrac - *openPosition;
        if (distRemaining > 0.0f)
        {
            if (speed > 0.0f && distRemaining <= limit)
            {
                // Slow down for end
                speed -= SPD(decel);

                if (speed < decel)
                {
                    speed = decel;
                }
            }
            else if (speed < maxSpeed)
            {
                // Accelerate
                if (speed < 0.0f)
                {
                    speed += SPD(decel);
                }
                else
                {
                    speed += SPD(accel);
                }

                if (speed > maxSpeed)
                {
                    speed = maxSpeed;
                }
            }

            if (SPD(speed) >= distRemaining)
            {
                *openPosition = maxFrac;
                break;
            }

            *openPosition += SPD(speed);
        }
        else
        {
            if (speed < 0.0f && -distRemaining <= limit)
            {
                speed += SPD(decel);

                if (speed > -decel)
                {
                    speed = -decel;
                }
            }
            else if (speed > -maxSpeed)
            {
                if (speed > 0.0f)
                {
                    speed -= SPD(decel);
                }
                else
                {
                    speed -= SPD(accel);
                }

                if (speed < -maxSpeed)
                {
                    speed = -maxSpeed;
                }
            }

            if (SPD(speed) <= distRemaining)
            {
                *openPosition = maxFrac;
                break;
            }

            *openPosition += SPD(speed);
        }
    }
    *speedPtr = speed;
#undef SPD
#undef steps
}





/**
 * Address 0x7F04310C.
*/
void chrobjCallsApplySpeed(f32 *openPosition, f32 maxFrac, f32 *speedPtr, f32 accel, f32 decel, f32 maxSpeed)
{
    if (maxFrac - *openPosition < -M_PI_F)
    {
        maxFrac += M_TAU_F;
    }
    else if (maxFrac - *openPosition >= M_PI_F)
    {
        maxFrac -= M_TAU_F;
    }

    chrobjApplySpeed(openPosition, maxFrac, speedPtr, accel, decel, maxSpeed);

    if (*openPosition < 0.0f)
    {
        *openPosition = *openPosition + M_TAU_F;
    }

    if (*openPosition >= M_TAU_F)
    {
        *openPosition = *openPosition - M_TAU_F;
    }
}


/**
 * Address: 7F0431E4
 *
 * This function handles an object's transition from the bouncing state to the rest state.
 */
void objSettle(ObjectRecord *obj, coord3d *arg1)
{
    coord3d angles;
    Mtxf rotmtx;
    Mtxf aimmtx;
    Mtxf scalemtx;
    f32 x;
    f32 z;
    f32 invlen;
    f32 tmp;
    f32 modelscale;
    s32 pad;
    f32 angle;
    Projectile *projectile;

    if (((obj->runtime_bitflags &= ~0x00010000) & 0x80) == 0) {
        return;
    }

    projectile = obj->projectile;

    if (((u8 *)obj)[3] == 1) {
        projectileFree(projectile);
        obj->projectile = 0;
        obj->runtime_bitflags &= ~0x80;
        return;
    }

    projectile->ownerprop = 0;
    projectile->flags &= ~PROJECTILEFLAG_AIRBORNE;
    projectile->flags &= ~PROJECTILEFLAG_STICKY;

    matrix_4x4_get_rotation_around_xyz(&obj->mtx, &angles);
    matrix_4x4_set_rotation_around_xyz(&angles, &rotmtx);
    quaternion_set_rotation_around_xyzf((f32 *)&angles, projectile->unk68);

    matrix_4x4_set_rotation_inverse(&rotmtx, &aimmtx);
    matrix_4x4_multiply(&aimmtx, &obj->mtx, &scalemtx);

    projectile->unkC0 = sqrtf(
        ((scalemtx.m[0][0] * scalemtx.m[0][0])
        + (scalemtx.m[0][1] * scalemtx.m[0][1]))
        + (scalemtx.m[0][2] * scalemtx.m[0][2]));

    projectile->unkC4 = sqrtf(
        ((scalemtx.m[1][0] * scalemtx.m[1][0])
        + (scalemtx.m[1][1] * scalemtx.m[1][1]))
        + (scalemtx.m[1][2] * scalemtx.m[1][2]));

    projectile->unkC8 = sqrtf(
        ((scalemtx.m[2][0] * scalemtx.m[2][0])
        + (scalemtx.m[2][1] * scalemtx.m[2][1]))
        + (scalemtx.m[2][2] * scalemtx.m[2][2]));

    x = obj->mtx.m[0][0];
    z = obj->mtx.m[0][2];

    if ((x != 0.0f) || (z != 0.0f)) {
        invlen = 1.0f / sqrtf((x * x) + (z * z));
        x *= invlen;
        z *= invlen;
    } else {
        z = 1.0f;
        x = 0.0f;
    }

    aimmtx.m[0][0] = x;
    aimmtx.m[0][1] = 0.0f;
    aimmtx.m[0][2] = z;
    aimmtx.m[0][3] = 0.0f;

    if (obj->mtx.m[1][1] >= 0.0f) {
        aimmtx.m[1][0] = 0.0f;
        aimmtx.m[1][1] = 1.0f;
        aimmtx.m[1][2] = 0.0f;
        aimmtx.m[1][3] = 0.0f;

        aimmtx.m[2][0] = -z;
        aimmtx.m[2][1] = 0.0f;
        aimmtx.m[2][2] = x;
        aimmtx.m[2][3] = 0.0f;
    } else {
        aimmtx.m[1][0] = 0.0f;
        aimmtx.m[1][1] = -1.0f;
        aimmtx.m[1][2] = 0.0f;
        aimmtx.m[1][3] = 0.0f;

        aimmtx.m[2][0] = z;
        aimmtx.m[2][1] = 0.0f;
        aimmtx.m[2][2] = -x;
        aimmtx.m[2][3] = 0.0f;
    }

    aimmtx.m[3][0] = 0.0f;
    aimmtx.m[3][1] = 0.0f;
    aimmtx.m[3][2] = 0.0f;
    aimmtx.m[3][3] = 1.0f;

    matrix_4x4_get_rotation_around_xyz(&aimmtx, &angles);
    quaternion_set_rotation_around_xyzf((f32 *)&angles, projectile->unk78);
    quaternion_ensure_shortest_path(projectile->unk68, projectile->unk78);

    projectile->unk60 = 0.0f;

    angle = acosf(
        ((aimmtx.m[0][0] * rotmtx.m[0][0])
        + (aimmtx.m[0][1] * rotmtx.m[0][1]))
        + (aimmtx.m[0][2] * rotmtx.m[0][2]));

    if (((angle > 0.0f) && (obj->mtx.m[0][1] > 0.0f))
        && (arg1->y < obj->mtx.m[0][1]))
    {
        projectile->unk64 = 0.050000001f / ((angle * 4.0f) / M_TAU_F);
    } else if (((angle > 0.0f) && (obj->mtx.m[0][1] < 0.0f))
        && (obj->mtx.m[0][1] < arg1->y))
    {
        projectile->unk64 = 0.050000001f / ((angle * 4.0f) / M_TAU_F);
    } else {
        modelscale = obj->model->scale;

        tmp = acosf(
            (((arg1->x * obj->mtx.m[0][0])
            + (arg1->y * obj->mtx.m[0][1]))
            + (obj->mtx.m[0][2] * arg1->z))
            / (modelscale * modelscale));

        tmp = tmp / g_GlobalTimerDelta;

        if (angle != 0.0f) {
            projectile->unk64 = tmp / angle;
        } else {
            projectile->unk64 = 1.0f;
        }
    }

    if (projectile->unk64 < 0.0f) {
        projectile->unk64 = -projectile->unk64;
    }

    if (projectile->unk64 < 0.029999999f) {
        projectile->unk64 = 0.029999999f;
    } else if (0.15000001f < projectile->unk64) {
        projectile->unk64 = 0.15000001f;
    }
}


/**
 * Address: 7F043650
 * 
 * Plays and maintains the sound of the whoosh of a throwing knife spinning through the air.
 */
void objUpdateThrowKnifeSound(ObjectRecord *obj) 
{
    if (!(obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)) 
    {
        return;
    }

    if ((obj->projectile->flags & PROJECTILEFLAG_AIRBORNE) && ((s32)obj->projectile->unk90 <= 0) && (obj->runtime_bitflags & RUNTIMEBITFLAG_THROWING_KNIFE_RELATED)) 
    {

        s16 Throwing_knife_SFX[] = {0x5F, 0x60, 0x61};
        s32 slot;
        s32 sfxindex;

        slot = obj->projectile->soundSlot;
        sfxindex = randomGetNext() % 3;

#if defined(LEFTOVERDEBUG)
        if ((s32)obj->projectile->lastSfxTimer < g_GlobalTimer - 6) {
#else
        if ((s32)obj->projectile->lastSfxTimer < g_GlobalTimer - 5) 
        {
#endif
            if (obj->projectile->sounds[slot] != NULL) 
            {
                if (sndGetPlayingState(obj->projectile->sounds[slot])) 
                {
                    sndDeactivate(obj->projectile->sounds[slot]);
                }
            }
        }

        if (obj->projectile->sounds[slot] != NULL) 
        {
            return;
        }

        if (!lvlGetControlsLockedFlag()) 
        {
            sndPlaySfx(g_musicSfxBufferPtr, Throwing_knife_SFX[sfxindex], &obj->projectile->sounds[slot]);

            chrobjSndCreatePostEventDefault(obj->projectile->sounds[slot], &obj->prop->pos);

            obj->projectile->lastSfxTimer = g_GlobalTimer;
            obj->projectile->soundSlot = 1 - slot;
        }
    } 
    else 
    {
        obj->runtime_bitflags &= ~PROJECTILEFLAG_00000020;

        if (obj->projectile->sounds[0] != NULL)
        {
            if (sndGetPlayingState(obj->projectile->sounds[0])) 
            {
                sndDeactivate(obj->projectile->sounds[0]);
            }
        }

        if (obj->projectile->sounds[1] != NULL) 
        {
            if (sndGetPlayingState(obj->projectile->sounds[1])) 
            {
                sndDeactivate(obj->projectile->sounds[1]);
            }
        }
    }
}


ModelRenderData D_80030B34 = {NULL,
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




//[80030B74	00	Bond]
MonitorCmdWord monAnim00Bond[] = {
    MONUSEIMAGE(IMGBOND),
    MONHORZSCROLL(0x400, 20),
    MONHOLDTIME(20),
    MONVERTSCROLL(0x400, 20),
    MONRGBA(COLOR_BLACK, 20),
    MONHOLDTIME(20),
    MONZOOMSQUARE(0x200, 20),
    MONRGBA(COLOR_WHITE, 20),
    MONHOLDTIME(20),
    MONZOOMSQUARE(0x400, 20),
    MONHOLDTIME(20),
    MONLOOP()
};

//[80030C00	01	Desktops, Satellite]
MonitorCmdWord monAnim01DesktopsSatellite[] = {
     MONUSEIMAGE(IMG2DMATH),
     MONHORZSCROLL(0x400, 20),
     MONHOLDTIME(20),
     MONVERTSCROLL(0x400, 20),
     MONRGBA(COLOR_BLACK, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x200, 20),
     MONRGBA(COLOR_WHITE, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x400, 20),
     MONHOLDTIME(20),
     MONUSEIMAGE(IMGSATELLITE),
     MONHORZSCROLL(0x400, 20),
     MONHOLDTIME(20),
     MONVERTSCROLL(0x400, 20),
     MONRGBA(COLOR_BLACK, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x200, 20),
     MONRGBA(COLOR_WHITE, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x400, 20),
     MONHOLDTIME(20),
     MONUSEIMAGE(IMGDESKTOP),
     MONHORZSCROLL(0x400, 20),
     MONHOLDTIME(20),
     MONVERTSCROLL(0x400, 20),
     MONRGBA(COLOR_BLACK, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x200, 20),
     MONRGBA(COLOR_WHITE, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x400, 20),
     MONHOLDTIME(20),
     MONUSEIMAGE(IMGDESKTOPSTAGGERED),
     MONHORZSCROLL(0x400, 20),
     MONHOLDTIME(20),
     MONVERTSCROLL(0x400, 20),
     MONRGBA(COLOR_BLACK, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x200, 20),
     MONRGBA(COLOR_WHITE, 20),
     MONHOLDTIME(20),
     MONZOOMSQUARE(0x400, 20),
     MONHOLDTIME(20),
     MONLOOP(),
};

//[80030E24	02	10 screens: astrological]
MonitorCmdWord monAnim02Astrological[] = {
     MONUSEIMAGE(IMGSHUTTLE1), MONHOLDTIME(80),
     MONUSEIMAGE(IMGSHUTTLE2), MONHOLDTIME(80),
     MONUSEIMAGE(IMGEARTHFULL1), MONHOLDTIME(80),
     MONUSEIMAGE(IMGEARTHFULL2), MONHOLDTIME(80),
     MONUSEIMAGE(IMGBLUESTARS), MONHOLDTIME(80),
     MONUSEIMAGE(IMGGALAXY1), MONHOLDTIME(80),
     MONUSEIMAGE(IMGGALAXY2), MONHOLDTIME(80),
     MONUSEIMAGE(IMGEARTHTEXT), MONHOLDTIME(80),
     MONUSEIMAGE(IMGTARGETEARTH), MONHOLDTIME(80),
     MONUSEIMAGE(IMGGALAXY3), MONHOLDTIME(80),
     MONLOOP(),
};

//[80030EC8	0F	7 screens: satellite, targetting, ]
MonitorCmdWord monAnim0FSatelliteTargeting[] = {
     MONUSEIMAGE(IMGEARTH), MONHOLDTIME(80),
     MONUSEIMAGE(IMGDESKTOPBANG), MONHOLDTIME(80),
     MONUSEIMAGE(IMGHEATMAP), MONHOLDTIME(80),
     MONUSEIMAGE(IMG2DMATH), MONHOLDTIME(80),
     MONUSEIMAGE(IMGSATELLITE), MONHOLDTIME(80),
                        MONHOLDTIME(80),
     MONUSEIMAGE(IMGTARGETEARTH), MONHOLDTIME(80),
     MONUSEIMAGE(IMGEARTHFULL2), MONHOLDTIME(80),
     MONLOOP()
};

//[80030F44	03	3 wave patterns]
MonitorCmdWord monAnim03ThreeWavePattern[] = {
     MONRGBA(COLOR_MINESHAFT3, 1),
     MONUSEIMAGE(IMGSINE),
     MONHORZSCROLL(0x800, 120),
     MONHOLDTIME(120),
     MONZOOMWIDTH(0x100, 1),
     MONZOOMHEIGHT(0x200, 60),
     MONHORZSCROLL(0xFFFFE000, 120),
     MONHOLDTIME(120),
     MONZOOMWIDTH(0x400, 1),
     MONZOOMHEIGHT(0x400, 60),
     MONZOOMHEIGHT(0x400, 60),
     MONHORZSCROLL(0x800, 120),
     MONHOLDTIME(120),
     MONZOOMWIDTH(0x80, 1),
     MONZOOMHEIGHT(0x800, 60),
     MONZOOMHEIGHT(0x400, 120),
     MONVERTSCROLL(0x400, 60),
     MONHORZSCROLL(0x200, 120),
     MONHOLDTIME(120),
     MONLOOP()
};

//[80031018	04	wave pattern]
MonitorCmdWord monAnim04WavePattern[] = {
     MONRGBA(COLOR_MINESHAFT3, 1),
     MONUSEIMAGE(IMGSINE),
     MONZOOMWIDTH(0x80, 1),
     MONZOOMHEIGHT(0x800, 60),
     MONZOOMHEIGHT(0x400, 120),
     MONVERTSCROLL(0x400, 10),
     MONHORZSCROLL(0x200, 40),
     MONHOLDTIME(120),
     MONLOOP()
};

//[80031074	05	green text up]
MonitorCmdWord monAnim05GreenTextUp[] = {
     MONUSEIMAGE(IMGTEXT),
     MONRGBA(COLOR_BARELYGREENOPAQUE, 1),
     MONVERTSCROLL(0xFFFFFE00, 80),
     MONHOLDTIME(120),
     MONVERTSCROLL(0xFFFFFF00, 20),
     MONHOLDTIME(120),
     MONVERTSCROLL(0xFFFFFF80, 10),
     MONHOLDTIME(40),
     MONVERTSCROLL(0xFFFFFE00, 40),
     MONHOLDTIME(60),
     MONVERTSCROLL(0xFFFFFFC0, 30),
     MONHOLDTIME(120),
     MONLOOP()
};

//[800310F0	06	red text down]
MonitorCmdWord monAnim06RedTextDown[] = {
     MONUSEIMAGE(IMGTEXT),
     MONRGBA(COLOR_DIESEL, 1),
     MONVERTSCROLL(0x200, 80),
     MONHOLDTIME(120),
     MONVERTSCROLL(0x100, 20),
     MONHOLDTIME(120),
     MONVERTSCROLL(0x80, 10),
     MONHOLDTIME(40),
     MONVERTSCROLL(0x200, 40),
     MONHOLDTIME(60),
     MONVERTSCROLL(0x40, 30),
     MONHOLDTIME(120),
     MONVERTSCROLL(0x100, 20),
     MONHOLDTIME(120),
     MONVERTSCROLL(0x80, 10),
     MONLOOP()
};

//[8003118C	07	d. green text down]
MonitorCmdWord monAnim07GreenTextDown[] = {
     MONUSEIMAGE(IMGTEXT),
     MONRGBA(COLOR_DEEPFIR, 1),
     MONVERTSCROLL(0x200, 80),
     MONHOLDTIME(120),
     MONVERTSCROLL(0x80, 10),
     MONHOLDTIME(40),
     MONVERTSCROLL(0x100, 20),
     MONHOLDTIME(120),
     MONVERTSCROLL(0x80, 10),
     MONHOLDTIME(40),
     MONVERTSCROLL(0x200, 40),
     MONHOLDTIME(60),
     MONVERTSCROLL(0x40, 30),
     MONHOLDTIME(120),
     MONLOOP()
};

//[8003121C	08	red bar graph +]
MonitorCmdWord monAnim08RedBarGraph[] = {
     MONUSEIMAGE(IMGBARS),
     MONRGBA(COLOR_VERDUNGREEN, 1),
     MONHORZSCROLL(0x280, 1),
     MONHOLDTIME(10),
     MONLOOP()
};

//[80031248	09	blue bar graph +]
MonitorCmdWord monAnim09BlueBarGraph[] = {
     MONUSEIMAGE(IMGBARS),
     MONRGBA(COLOR_CYPRUS, 1),
     MONHORZSCROLL(0x280, 1),
     MONHOLDTIME(10),
     MONLOOP()
};

//[80031274	0A	green bar graph -]
MonitorCmdWord monAnim0AGreenBarGraph[] = {
     MONUSEIMAGE(IMGBARS),
     MONRGBA(COLOR_TOMTHUMB, 1),
     MONHORZSCROLL(0xFFFFFD80, 1),
     MONHOLDTIME(10),
     MONLOOP()
};

//[800312A0	subroutine	used by radar]
MonitorCmdWord monAnimRadarSub1[] = {
     MONRGBA(COLOR_GREEN, 20),
     MONJUMPTO(monAnimRadarSub3)
};

//[800312B4	subroutine	used by radar]
MonitorCmdWord monAnimRadarSub2[] = {
     MONRGBA(COLOR_SANFELIX, 20),
     MONJUMPTO(monAnimRadarSub3)
};

//[800312C8	subroutine	used by radar]
MonitorCmdWord monAnimRadarSub3[] = {
     MONROTATEIMAGE(0xB6),
     MONHOLDTIME(1),
     MONJUMPCHANCE(monAnimRadarSub1, TWO_PERCENT_CHANCE),
     MONJUMPCHANCE(monAnimRadarSub2, 0x147A),
     MONLOOP()
};

//[800312F4	0B	radar]
MonitorCmdWord monAnim0BRadar[] = {
     MONUSEIMAGE(IMGTRIANGLE),
     MONRGBA(COLOR_ALMOSTDARKGREEN, 1),
     MONJUMPTO(monAnimRadarSub2)
};

//[80031310	0C	spinning cube]
MonitorCmdWord monAnim0CSpinningCube[] = {
     MONUSEIMAGE(IMGCUBE1),
     MONRGBA(COLOR_MINSK, 30),
     MONHOLDTIME(5),
     MONUSEIMAGE(IMGCUBE2),
     MONHOLDTIME(5),
     MONUSEIMAGE(IMGCUBE3),
     MONHOLDTIME(5),
     MONUSEIMAGE(IMGCUBE4),
     MONHOLDTIME(5),
     MONLOOP()
};

//[80031360	10	global map]
MonitorCmdWord monAnim10GlobalMap[] = {
     MONUSEIMAGE(IMGWORLDMAP),
     MONRGBA(COLOR_SEAGREEN, 30),
     MONHORZSCROLL(0xFFFFFC00, 1024),
     MONHOLDTIME(1440),
     MONHORZSCROLLNA(0x288, 360),
     MONVERTSCROLLNA(0x3AA, 360),
     MONZOOMSQUARE(0x80, 300),
     MONRGBA(COLOR_BLACK, 60),
     MONHOLDTIME(60),
     MONRGBA(COLOR_GRAY, 10),
     MONHOLDTIME(90),
     MONRGBA(COLOR_APPLE2, 30),
     MONHOLDTIME(30),
     MONRGBA(COLOR_LOTUS, 60),
     MONHOLDTIME(60),
     MONRGBA(COLOR_GRAY, 60),
     MONHOLDTIME(60),
     MONHORZSCROLLNA(0x200, 360),
     MONVERTSCROLLNA(0x200, 360),
     MONZOOMSQUARE(0x400, 720),
     MONHOLDTIME(300),
     MONUSEIMAGE(IMGWORLDMAP),
     MONHOLDTIME(420),
     MONRGBA(COLOR_STRONGGREEN, 30),
     MONHOLDTIME(30),
     MONRGBA(COLOR_GREENKELP, 60),
     MONHOLDTIME(60),
     MONLOOP()
};

//[80031490	0D	3 screens: location, weapon armed, ]
MonitorCmdWord monAnim0DLocWeapArmed[] = {
     MONRGBA(COLOR_BLACK, 1),
     MONRGBA(COLOR_SILVER, 400),
     MONUSEIMAGE(1),
     MONHOLDTIME(680),
     MONUSEIMAGE(2),
     MONHOLDTIME(680),
     MONUSEIMAGE(4),
     MONHOLDTIME(180),
     MONRGBA(COLOR_PESTO, 1),
     MONUSEIMAGE(4),
     MONHOLDTIME(200),
     MONLOOP()
};

//[800314F8	0E	red target]
MonitorCmdWord monAnim0ERedTarget[] = {
     MONZOOMSQUARE(0x400, 1),
     MONRGBA(COLOR_THUNDERBIRD, 1),
     MONUSEIMAGE(6),
     MONHOLDTIME(600),
     MONRGBA(COLOR_SILVER, 5),
     MONHOLDTIME(5),
     MONRGBA(COLOR_MINESHAFT, 60),
     MONUSEIMAGE(IMGSTATIC),
     MONRGBA(COLOR_CODGRAY, 100),
     MONHOLDTIME(400),
     MONLOOP()
};

//[8003156C	11	Karl yelling]
MonitorCmdWord monAnim11KarlYelling[] = {
     MONRGBA(COLOR_DARKGREEN, 0),
     MONUSEIMAGE(IMGTALK1),
     MONHOLDTIME(5),
     MONUSEIMAGE(IMGTALK2),
     MONHOLDTIME(5),
     MONUSEIMAGE(IMGTALK3),
     MONHOLDTIME(5),
     MONUSEIMAGE(IMGTALK4),
     MONHOLDTIME(10),
     MONUSEIMAGE(IMGTALK2),
     MONHOLDTIME(5),
     MONLOOP()
};

//[800315CC	12	skateboard]
MonitorCmdWord monAnim12Skateboard[] = {
     MONUSEIMAGE(IMGSKATEBOARD4),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONHORZSCROLL(0x264, 30),
     MONUSEIMAGE(IMGSKATEBOARD1),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONUSEIMAGE(IMGSKATEBOARD2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONUSEIMAGE(IMGSKATEBOARD3),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONHORZSCROLL(0x19C, 40),
     MONUSEIMAGE(IMGSKATEBOARD2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONUSEIMAGE(IMGSKATEBOARD1),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONRGBA(COLOR_DARKGREEN, 0),
     MONHOLDTIME(3),
     MONRGBA(COLOR_DARKERGREEN, 0),
     MONHOLDTIME(2),
     MONLOOP()
};

//[80031848	13	police guy]
MonitorCmdWord monAnim13PoliceGuy[] = {
    MONRGBA(COLOR_DARKGREEN2, 0),
    MONUSEIMAGE(IMGFIST1),
    MONHOLDTIME(5),
    MONUSEIMAGE(IMGFIST2),
    MONHOLDTIME(5),
    MONUSEIMAGE(IMGFIST3),
    MONHOLDTIME(5),
    MONUSEIMAGE(IMGFIST4),
    MONHOLDTIME(5),
    MONLOOP()
};

//[80031898	14	'off']
MonitorCmdWord monAnim14Off[] = {
    MONUSEIMAGE(IMGSINE),
    MONRGBA(COLOR_BARELYGREEN, 1),
    MONHOLDTIME(5),
    MONLOOP()
};

//[800318B8	15	randomly select one of seven animations]
MonitorCmdWord monAnim15RandomSeven[] = {
    MONJUMPCHANCE(monAnim04WavePattern, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim11KarlYelling, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim08RedBarGraph, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim09BlueBarGraph, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim0AGreenBarGraph, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim06RedTextDown, TWENTY_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim07GreenTextDown, FOURTY_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim05GreenTextUp, HUNDRED_PERCENT_CHANCE),
    MONLOOP()
};

//[8003191C	16	randomly select random screens + random effects or boring]
MonitorCmdWord monAnim16RandomFour[] = {
    MONJUMPCHANCE(monAnim03ThreeWavePattern, TWO_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim08RedBarGraph, TWO_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim05GreenTextUp, TWO_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim17RandImageEffect, SIXTY_PERCENT_CHANCE),
    MONLOOP()
};

//[80031950	17	Base Function for random screens + random effects]
MonitorCmdWord monAnim17RandImageEffect[] = {
    MONJUMPCHANCE(monRandEffectChanceSHUTTLE1, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceSHUTTLE2, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceEARTHFULL1, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceEARTHFULL2, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceBLUESTARS, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceGALAXY1, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceGALAXY2, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceEARTHTEXT, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceTARGETEARTH, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandEffectChanceGALAXY3, TEN_PERCENT_CHANCE),
    MONHOLDTIME(100),
    MONLOOP()
};

//[800319D4	18	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceSHUTTLE1[] = {
    MONUSEIMAGE(IMGSHUTTLE1),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[800319F0	19	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceSHUTTLE2[] = {
    MONUSEIMAGE(IMGSHUTTLE2),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031A0C	1A	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceEARTHFULL1[] = {
    MONUSEIMAGE(IMGEARTHFULL1),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031A28	1B	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceEARTHFULL2[] = {
    MONUSEIMAGE(IMGEARTHFULL2),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031A44	1C	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceBLUESTARS[] = {
    MONUSEIMAGE(IMGBLUESTARS),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031A60	1D	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceGALAXY1[] = {
    MONUSEIMAGE(IMGGALAXY1),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031A7C	1E	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceGALAXY2[] = {
    MONUSEIMAGE(IMGGALAXY2),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031A98	1F	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceEARTHTEXT[] = {
    MONUSEIMAGE(IMGEARTHTEXT),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031AB4	20	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceTARGETEARTH[] = {
    MONUSEIMAGE(IMGTARGETEARTH),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031AD0	21	random screens + random effects - set image]
MonitorCmdWord monRandEffectChanceGALAXY3[] = {
    MONUSEIMAGE(IMGGALAXY3),
    MONHOLDTIME(20),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, HUNDRED_PERCENT_CHANCE)
};

//[80031AEC	22	random screens + random effects - colourizer]
MonitorCmdWord monRandChanceScrollOrZoomRandRGBN[] = {
    MONJUMPCHANCE(monRandChanceScrollOrZoomRed, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandChanceScrollOrZoomGreen, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monRandChanceScrollOrZoomBlue, TEN_PERCENT_CHANCE),
    MONRGBA(COLOR_SILVER, 60),
    MONJUMPTO(monRandChanceScrollOrZoom)
};

//[80031B24	23	random screens + random effects - colourizer]
MonitorCmdWord monRandChanceScrollOrZoomRed[] = {
    MONRGBA(COLOR_PERSIANRED, 60),
    MONJUMPTO(monRandChanceScrollOrZoom)
};

MonitorCmdWord monRandChanceScrollOrZoomGreen[] = {
    MONRGBA(COLOR_APPLE, 60),
    MONJUMPTO(monRandChanceScrollOrZoom)
};

MonitorCmdWord monRandChanceScrollOrZoomBlue[] = {
    MONRGBA(COLOR_GOVERNORBAY, 60),
    MONJUMPTO(monRandChanceScrollOrZoom)
};

MonitorCmdWord monRandChanceScrollOrZoom[] = {
    MONHOLDTIME(50),
    MONJUMPCHANCE(monAnim27RandomEffectScrollRight, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim28RandomEffectScrollUpFast, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim29RandomEffectScrollUp, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim2ARandEffectScrollZoom1, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim2ARandEffectScrollZoom2, TEN_PERCENT_CHANCE),
    MONHOLDTIME(300),
    MONJUMPTO(monAnim2CRandEffectWaitRoute)
};


//[80031BB4	27	random screens + random effects - scroll right]
MonitorCmdWord monAnim27RandomEffectScrollRight[] = {
    MONHORZSCROLL(0x800, 120),
    MONHOLDTIME(120),
    MONJUMPTO(monAnim2CRandEffectWaitRoute)
};

//[80031BD0	28	random screens + random effects - scroll up fast]
MonitorCmdWord monAnim28RandomEffectScrollUpFast[] = {
    MONVERTSCROLL(0x2000, 50),
    MONHOLDTIME(200),
    MONJUMPTO(monAnim2CRandEffectWaitRoute)
};

//[80031BEC	29	random screens + random effects - scroll up]
MonitorCmdWord monAnim29RandomEffectScrollUp[] = {
    MONVERTSCROLL(0x2000, 200),
    MONHOLDTIME(200),
    MONJUMPTO(monAnim2CRandEffectWaitRoute)
};

//[80031C08	2A	random screens + random effects - scroll and zoom]
MonitorCmdWord monAnim2ARandEffectScrollZoom1[] = {
    MONHORZSCROLLNA(0x288, 300),
    MONVERTSCROLLNA(0x3AA, 300),
    MONZOOMSQUARE(0x80, 200),
    MONHOLDTIME(300),
    MONHORZSCROLLNA(0x200, 50),
    MONVERTSCROLLNA(0x200, 200),
    MONZOOMSQUARE(0x400, 720),
    MONHOLDTIME(600),
    MONJUMPTO(monAnim2CRandEffectWaitRoute)
};

//[80031C80	2B	random screens + random effects - scroll and zoom]
MonitorCmdWord monAnim2ARandEffectScrollZoom2[] = {
    MONHORZSCROLLNA(0x320, 400),
    MONVERTSCROLLNA(0x190, 400),
    MONZOOMSQUARE(0x80, 200),
    MONHOLDTIME(300),
    MONHORZSCROLLNA(0xC8, 200),
    MONVERTSCROLLNA(0x190, 800),
    MONZOOMSQUARE(0x200, 720),
    MONHOLDTIME(800),
    MONZOOMSQUARE(0x400, 720),
    MONHORZSCROLLNA(0x200, 100),
    MONVERTSCROLLNA(0x200, 60),
    MONHOLDTIME(500),
    MONJUMPTO(monAnim2CRandEffectWaitRoute)
};

//[80031D30	2C	random screens + random effects - wait and route]
MonitorCmdWord monAnim2CRandEffectWaitRoute[] = {
    MONHOLDTIME(50),
    MONJUMPCHANCE(monRandChanceScrollOrZoomRandRGBN, TEN_PERCENT_CHANCE),
    MONJUMPCHANCE(monAnim2DRandEffectFlash, TWENTY_PERCENT_CHANCE),
    MONJUMPTO(monAnim17RandImageEffect)
};

//[80031D58	2D	random screens + random effects - flash]
MonitorCmdWord monAnim2DRandEffectFlash[] = {
    MONHOLDTIME(50),
    MONRGBA(COLOR_WHITE, 10),
    MONRGBA(COLOR_BLACK, 5),
    MONRGBA(COLOR_WHITE, 10),
    MONHOLDTIME(25),
    MONRGBA(COLOR_BLACK, 200),
    MONHOLDTIME(500),
    MONJUMPTO(monAnim17RandImageEffect)
};

//[80031DA8	2E	red brightening screen]
MonitorCmdWord monAnim2ERedBrightening[] = {
     MONUSEIMAGE(IMGKEYBOARDKEY),
     MONZOOMSQUARE(0x200, 0),
     MONRGBA(COLOR_ALIZARINCRIMSON, 60),
     MONHOLDTIME(60),
     MONRGBA(COLOR_MINESHAFT2, 10),
     MONHOLDTIME(10),
     MONLOOP()
};

//[80031DF4	2F	green brightening screen]
MonitorCmdWord monAnim2FGreenBrightening[] = {
     MONUSEIMAGE(IMGKEYBOARDKEY),
     MONZOOMSQUARE(0x200, 0),
     MONRGBA(COLOR_APPLE, 60),
     MONHOLDTIME(60),
     MONRGBA(COLOR_MINESHAFT2, 10),
     MONHOLDTIME(10),
     MONLOOP()
};

//[80031E40	30	grey solid]
MonitorCmdWord monAnim30GreySolid[] = {
     MONUSEIMAGE(IMGKEYBOARDKEY),
     MONZOOMSQUARE(0x200, 0),
     MONRGBA(COLOR_MINESHAFT2, 10),
     MONHOLDTIME(10),
     MONLOOP()
};

//[80031E78	31	red solid]
MonitorCmdWord monAnim31RedSolid[] = {
     MONUSEIMAGE(IMGKEYBOARDKEY),
     MONZOOMSQUARE(0x200, 0),
     MONRGBA(COLOR_ALIZARINCRIMSON, 10),
     MONHOLDTIME(10),
     MONLOOP()
};

//[80031EB0	32	green solid]
MonitorCmdWord monAnim32GreenSolid[] = {
     MONUSEIMAGE(IMGKEYBOARDKEY),
     MONZOOMSQUARE(0x200, 0),
     MONRGBA(COLOR_APPLE, 10),
     MONHOLDTIME(10),
     MONLOOP()
};

//[80031EE8	33	black solid]
MonitorCmdWord monAnim33BlackSolid[] = {
     MONUSEIMAGE(0),
     MONRGBA(COLOR_BLACK, 0),
     MONSTOPANIM()
};

//[80031F00	34	???	Not Included in Normal List - linked @ 0x9544]
MonitorCmdWord monAnim34[] = {
     MONZOOMSQUARE(0x400, 0),
     MONHOLDTIME(1),
     MONZOOMSQUARE(0x1000, 20),
     MONHOLDTIME(20),
     MONLOOP()
};

//[80031F44	35	Taser	Not Included in Normal List!]
MonitorCmdWord monAnim35Taser[] = {
     MONUSEIMAGE(IMGBOND),
     MONHORZSCROLL(0x400, 20), MONHOLDTIME(20),
     MONVERTSCROLL(0x400, 20), MONRGBA(COLOR_BLACK, 20), MONHOLDTIME(20),
     MONZOOMSQUARE(0x200, 20), MONRGBA(COLOR_WHITE, 20), MONHOLDTIME(20),
     MONZOOMSQUARE(0x400, 20), MONHOLDTIME(20),
     MONLOOP()
};

/**
 * Address 0x80031FD0.
*/
ModelRenderData D_80031FD0 = {  NULL,
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
                                {0,0,0,0},
                                {0,0,0,0},
                                CULLMODE_BOTH};

void sub_GAME_7F043838(coord3d *arg0, Mtxf *arg1)
{
    f32 sp124;
    f32 sp120;
    f32 sp11c;
    f32 sp118;
    f32 sp114;
    f32 f0;
    f32 sp10c;
    f32 sp108;
    f32 sp104;
    f32 a;
    f32 b;
    f32 stack;
    f32 spf4;
    f32 spf0;
    Mtxf spb0;
    Mtxf sp70;
    Mtxf sp30;
    coord3d sp24;

    f0 = sqrtf(arg0->f[0] * arg0->f[0] + arg0->f[1] * arg0->f[1] + arg0->f[2] * arg0->f[2]);

    sp10c = arg0->x / f0;
    sp108 = arg0->y / f0;
    sp104 = arg0->z / f0;

    if (sp10c == 0.0f && sp104 == 0.0f)
    {
        sp124 = 0.0f;
        sp120 = 0.0f;
        sp11c = sp108;
        sp118 = 1.0f;
        sp114 = 0.0f;
    }
    else
    {
        a = sqrtf(sp10c * sp10c + sp104 * sp104);
        b = sp10c / a;

        sp118 = sp104 / a;
        sp114 = -b;

        sp124 = sp108 * b;
        sp120 = -a;
        sp11c = sp108 * sp118;
    }

    spf4 = atan2f(sp118, sp114);

    matrix_4x4_set_rotation_around_y(-spf4, &spb0);

    sp24.x = sp124;
    sp24.y = sp120;
    sp24.z = sp11c;

    mtx4RotateVecInPlace(&spb0, sp24.f);

    spf0 = atan2f(sp24.x, sp24.y);

    matrix_4x4_set_rotation_around_y(-1.5707964f + spf4, &sp70);
    matrix_4x4_set_rotation_around_x(-1.5707964f - spf0, &sp30);

    matrix_4x4_multiply(&sp70, &sp30, arg1);
}

void sub_GAME_7F0439B8(ObjectRecord* obj, coord3d* pos, StandTile* stan, coord3d* arg3)
{
    Mtxf matrix;
    f32 temp_f0;

    sub_GAME_7F043838(arg3, &matrix);
    matrix_scalar_multiply(obj->model->scale, matrix.m[0]);
    objChangeShading(obj, pos, &matrix, stan);

    temp_f0 = chrpropBBOXGetYmin(chrobjGetBboxFromObjFile(obj->model->obj));

    obj->runtime_pos.f[0] -= temp_f0 * obj->mtx.m[1][0];
    obj->runtime_pos.f[1] -= temp_f0 * obj->mtx.m[1][1];
    obj->runtime_pos.f[2] -= temp_f0 * obj->mtx.m[1][2];

    chrobjCollisionRelated(obj);
}


bool objEmbed(PropRecord *prop, PropRecord *parent, Model *model, ModelNode *node)
{
    if (parent->flags & PROPFLAG_ONSCREEN)
    {
        ObjectRecord *obj = prop->obj;

        Mtxf mtx1;
        Mtxf mtx2;
        Mtxf mtx3;
        Mtxf* nodemtx;

        obj->embedment = embedmentAllocate();

        if (obj->embedment)
        {
            nodemtx = modelFindNodeMtx(model, node, 0);

            obj->runtime_bitflags |= RUNTIMEBITFLAG_EMBEDDED;

            chrpropDeregisterRooms(prop);
            chrpropDelist(prop);
            chrpropDisable(prop);

            obj->model->attachedto = model;
            obj->model->attachedto_objinst = node;

            chrpropReparent(prop, parent);

            matrix_4x4_copy(&obj->mtx, &mtx1);
            matrix_4x4_set_position(&obj->runtime_pos, &mtx1);
            matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), nodemtx, &mtx2);
            matrix_4x4_invert_affine((f32 (*)[4]) &mtx2.m, (f32 (*)[4]) &mtx3.m);
            matrix_4x4_multiply_homogeneous((Mtxf* ) &mtx3.m, &mtx1, &obj->embedment->matrix);

            return TRUE;
        }
    }

    return FALSE;
}


/**
 * Named same as Perfect Dark.
*/
#if defined(VERSION_JP) || defined(VERSION_EU)
s32 propExplode(PropRecord *prop, s32 /* enum EXPLOSION_DEF */ explosionType)
#else
void propExplode(PropRecord *prop, s32 /* enum EXPLOSION_DEF */ explosionType)
#endif
{
    ObjectRecord *prop_obj; // sp92
    s32 playernum; // sp88
#if defined(VERSION_JP) || defined(VERSION_EU)
    s32 ret;
#endif
    struct PropRecord *parent;
    struct StandTile *stan; // sp80
    struct coord3d pos;
    Mtxf *mtx;

    prop_obj = prop->obj;
    playernum = (prop_obj->runtime_bitflags & RUNTIMEBITFLAG_OWNER) >> RUNTIMEBITSHIFT_OWNER;

    if (prop->parent)
    {
        parent = prop->parent;

        while (parent->parent)
        {
			parent = parent->parent;
		}

        stan = parent->stan;

        if (prop->flags & PROPFLAG_ONSCREEN)
        {
            mtx = getsubmatrix(prop_obj->model);

            pos.x = mtx->m[3][0];
			pos.y = mtx->m[3][1];
			pos.z = mtx->m[3][2];

            mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &pos);
        }
        else
        {
            pos.x = parent->pos.x;
			pos.y = parent->pos.y;
			pos.z = parent->pos.z;
        }

        if ((parent->flags & PROPFLAG_00000008) == 0
            && walkTilesBetweenPoints_NoCallback(&stan, parent->pos.f[0], parent->pos.f[2], pos.x, pos.z))
        {
#if defined(VERSION_JP) || defined(VERSION_EU)
    ret =
#endif
            explosionCreate(0, &pos, stan, (s16) explosionType, (prop_obj->flags & 0xE) == 0, playernum, parent->rooms, 0);
        }
        else
        {
#if defined(VERSION_JP) || defined(VERSION_EU)
    ret =
#endif
            explosionCreate(0, &pos, stan, (s16) explosionType, 0, playernum, parent->rooms, 1);
        }
    }
    else
    {
#if defined(VERSION_JP) || defined(VERSION_EU)
    ret =
#endif
        explosionCreate(
            0,
            &prop_obj->runtime_pos,
            prop->stan,
            (s16) explosionType,
            (prop_obj->flags & 0xE) == 0 && (prop->flags & PROPFLAG_00000008) == 0,
            playernum,
            prop->rooms,
            (prop->flags & PROPFLAG_00000008) != 0);
    }

#if defined(VERSION_JP) || defined(VERSION_EU)
    return ret;
#endif
}



/**
 * US address 7F043D70.
 * JP address 7F044074.
 * EU address 7F043E34.
 *
 * Seems to be a subset of Perfect Dark weaponTick.
*/
void chrobjWeaponTick(struct PropRecord* prop)
{
    struct ObjectRecord* obj;
    struct WeaponObjRecord *weapon;
#if defined(VERSION_US)
    u32 owner_player_number;
    u32 owner_player_as_bitflag;
#else
    s32 exp_result;
    u32 owner_player_number;
    s32 p1;
    u32 owner_player_as_bitflag;
#endif
    struct PropRecord* player_prop;
    f32 diff_x;
    f32 diff_z;
    f32 diff_y;

    obj = prop->obj;

    if (get_player_position_in_shuffled(get_cur_playernum()) != 0)
    {
        return;
    }

    if (obj->type == PROP_TYPE_EXPLOSION) // 7
    {
        if (obj->flags & PROPFLAG_IS_DRONE_GUN)
        {
            propExplode(prop, EXPLOSION_DEF_DRONE);
            obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
        }

        return;
    }

    if (obj->type == PROP_TYPE_SMOKE) // 8
    {
        weapon = prop->weapon;

        if (((weapon->weaponnum == ITEM_GRENADE) || (weapon->weaponnum == ITEM_GRENADEROUND )) && (weapon->timer >= 0))
        {
            weapon->timer -= g_ClockTimer;

            if (weapon->timer < 0)
            {
                propExplode(prop, (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM) ? EXPLOSION_DEF_MASSIVE : EXPLOSION_DEF_STANDARD);
                obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
            }
        }
        else if (weapon->weaponnum == ITEM_ROCKETROUND)
        {
            if (weapon->timer == 0)
            {
                propExplode(prop, (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM) ? EXPLOSION_DEF_MASSIVE : EXPLOSION_DEF_STANDARD);
                obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
            }
        }
        else if (weapon->weaponnum == ITEM_PLASTIQUE)
        {
            if (weapon->timer == 0)
            {
                propExplode(prop, EXPLOSION_DEF_MASSIVE);
                obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
                SurroundWithExplosions(PLASTIQUE_EXPLOSION_DELAY_TICKS);
                countdownTimerSetVisible(2, FALSE);
            }
        }
        else if (((weapon->weaponnum == ITEM_TIMEDMINE) || (weapon->weaponnum == ITEM_BOMBCASE)) && (weapon->timer >= 0))
        {
            weapon->timer -= g_ClockTimer;

            if (weapon->timer < 0)
            {
#if defined(VERSION_US)
                propExplode(prop, (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM) ? EXPLOSION_DEF_MASSIVE : EXPLOSION_DEF_STANDARD);
#else
                exp_result = propExplode(prop, (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM) ? EXPLOSION_DEF_MASSIVE : EXPLOSION_DEF_STANDARD);
                if (exp_result == 0)
                {
                    return;
                }
#endif
                weapon->timer = -1;
                obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;

            }
        }
        else if (weapon->weaponnum == ITEM_REMOTEMINE)
        {
            if (g_RemoteMineOwnerTriggerFlag)
            {
                owner_player_number = (obj->runtime_bitflags & RUNTIMEBITFLAG_OWNER) >> RUNTIMEBITSHIFT_OWNER;
                owner_player_as_bitflag = (1 << owner_player_number);
                if (g_RemoteMineOwnerTriggerFlag & owner_player_as_bitflag)
                {
                    weapon->timer = 0;
                }
            }

            if (weapon->timer > 1)
            {
                weapon->timer -= g_ClockTimer;

                if (weapon->timer < 2)
                {
                    weapon->timer = 1;
                }
            }
            else if (weapon->timer == 0)
            {
#if defined(VERSION_US)
                if (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM)
                {
                    propExplode(prop, EXPLOSION_DEF_MASSIVE);
                }
                else if (bossGetStageNum() == LEVELID_FACILITY)
                {
                    propExplode(prop, EXPLOSION_DEF_FACILITY_REMOTE);
                }
                else
                {
                    propExplode(prop, EXPLOSION_DEF_STANDARD);
                }
#else
                if (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM)
                {
                    p1 = EXPLOSION_DEF_MASSIVE;
                }
                else
                {
                    p1 = EXPLOSION_DEF_STANDARD;

                    if (bossGetStageNum() == LEVELID_FACILITY)
                    {
                        p1 = EXPLOSION_DEF_FACILITY_REMOTE;
                    }
                }

                exp_result = propExplode(prop, p1);
                if (exp_result == 0)
                {
                    return;
                }
#endif
                weapon->timer = -1;
                obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
            }

        }
        else if (weapon->weaponnum == ITEM_PROXIMITYMINE)
        {
            if (weapon->timer > 1)
            {
                weapon->timer -= g_ClockTimer;

                if (weapon->timer < 2)
                {
                    weapon->timer = 1;
                    add_obj_to_temp_proxmine_table(weapon);
                }
            }
            else if (weapon->timer == 1)
            {
                player_prop = getCurrentPlayerProp();

                diff_x = player_prop->pos.f[0] - prop->pos.f[0];
                diff_y = player_prop->pos.f[1] - prop->pos.f[1];
                diff_z = player_prop->pos.f[2] - prop->pos.f[2];

                if ((diff_x * diff_x) + (diff_y * diff_y) + (diff_z * diff_z) < PROXIMITY_MINE_TRIGGER_DISTANCE)
                {
                    weapon->timer = 0;
                }
            }

            if (weapon->timer == 0)
            {
#if defined(VERSION_US)
                propExplode(prop, (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM) ? EXPLOSION_DEF_MASSIVE : EXPLOSION_DEF_STANDARD);
#else
                exp_result = propExplode(prop, (obj->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM) ? EXPLOSION_DEF_MASSIVE : EXPLOSION_DEF_STANDARD);
                if (exp_result == 0)
                {
                    return;
                }
#endif

                weapon->timer = -1;
                obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
                remove_obj_from_temp_proxmine_table(weapon);
            }
        }
    }
}



void objDropRecursively(PropRecord *prop)
{
	PropRecord *child = prop->child;

	while (child)
    {
		PropRecord *prev = child->prev;
		objDropRecursively(child);
		objDrop(child);
		child = prev;
	}
}


void sub_GAME_7F04424C(PropRecord* prop)
{
    ObjectRecord* obj;
    PropRecord* next;
    PropRecord* child;

    obj = prop->obj;
    if (obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
    {
        objFree(obj, 1, obj->state & PROPSTATE_RESPAWN);
        return;
    }

    prop->flags &= ~(PROPFLAG_ONSCREEN);
    chrobjWeaponTick(prop);

    child = prop->child;
    while (child != NULL)
    {
        next = child->prev;
        sub_GAME_7F04424C(child);
        child = next;
    }
}


void sub_GAME_7F0442DC(PropRecord* prop)
{
    ObjectRecord* obj;
    Model* model;
    PropRecord* child;
    PropRecord* prev;
    Mtxf* mtx;

    obj = prop->obj;
    model = obj->model;

    if (obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
    {
        objFree(obj, 1, (obj->state & PROPSTATE_RESPAWN));
        return;
    }

    if ((model->attachedto_objinst != NULL) && (obj->runtime_bitflags & RUNTIMEBITFLAG_EMBEDDED))
    {
        mtx = modelFindNodeMtx(model->attachedto, model->attachedto_objinst, 0);
        prop->flags |= PROPFLAG_ONSCREEN;
        model->render_pos = (RenderPosView*)dynAllocate(model->obj->numMatrices << 6);

        matrix_4x4_multiply_homogeneous(mtx, &obj->embedment->matrix, (Mtxf*)model->render_pos);
        modelUpdateRelationsQuick(model, model->obj->RootNode);
        chrobjWeaponTick(prop);

        child = prop->child;
        while (child != NULL)
        {
            prev = child->prev;
            sub_GAME_7F0442DC(child);
            child = prev;
        }
    }
    else
    {
        prop->flags &= ~(PROPFLAG_ONSCREEN);
        chrobjWeaponTick(prop);

        child = prop->child;
        while (child != NULL)
        {
            prev = child->prev;
            sub_GAME_7F04424C(child);
            child = prev;
        }
    }
}


/**
 * Address: 7F044414
 * Description: Separating Axis Theorem
 *
 * Return true if both blocks are not intersecting on the X/Z plane.
 * PD: cdBlockExcludesBlockLaterally
 */
bool chrobjSeparatingAxisTheorem(rect4f* rect1, s32 numvertices0, rect4f* rect2, s32 numvertices1)
{
    f64 diff2;
    f64 diff1;
    s32 j;
    s32 k;
    s32 next;
    s32 i;
    f64 sum3;
    f64 sum2;
    f64 sum1;
    coord3d tmp;

    for (i = 0; i < numvertices0; i++)
    {
        next = (i + 1) % numvertices0;
        diff1 = rect1->points[next].y - (f64)rect1->points[i].y;
        diff2 = rect1->points[i].x - (f64)rect1->points[next].x;

        if (diff1 == 0.0f && diff2 == 0.0f)
        {
            tmp.x = rect1->points[i].x;
            tmp.y = 0.0f;
            tmp.z = rect1->points[i].y;
            if (chrpropTestPointInPolygon(&tmp, rect2, numvertices1))
            {
                return FALSE;
            }
        }
        else
        {
            sum1 = rect1->points[i].x * diff1 + rect1->points[i].y * diff2;
            j = (next + 1) % numvertices0;

            while (j != i)
            {
                sum2 = rect1->points[j].x * diff1 + rect1->points[j].y * diff2;

                if (sum2 != sum1) { break; }

                j = (j + 1) % numvertices0;
            }

            for (k = 0; k < numvertices1; k++)
            {
                sum3 = rect2->points[k].x * diff1 + rect2->points[k].y * diff2;

                if (sum2 == sum1)
                {
                    sum2 = sum1 - sum3 + sum1;
                }

                if (sum3 < sum1 && sum2 < sum1) { break; }
                if (sum3 > sum1 && sum2 > sum1) { break; }
            }

            if (k == numvertices1)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}


/**
 * Address 0x7F0446B8 (NTSC)
 * Address 0x7F0449A0 (NTSC-J)
 *
 * Description: Does a 2D collision check between two (convex?) polygons.
 *
 * Note: The NTSC version is 7 to 8 times faster than the others.
 *       Was this an attempt at optimization or to fix a bug?
 *
 * Deepseek says JP/EU's new code will detect edges cases such as a polygon
 * fully contained into another. NTSC's only check is SAT, which misses when
 * the polygons have edges that don’t overlap. NTSC's code handles 95% of
 * collisions so it should be called first.
 *
 * So they fixed a bug, but didn't do it the right way so it wouldn't affect performance.
*/
s32 chrobjTestPolygonsTouchingOrOverlap2D(struct rect4f *arg0, s32 arg1, struct rect4f *arg2, s32 arg3)
{
#if defined(VERSION_JP) || defined(VERSION_EU)
    s32 i;
    struct coord3d sp48;

    for (i=0; i<arg1; i++)
    {
        sp48.f[0] = arg0->points[i].f[0];
        sp48.f[1] = 0.0f;
        sp48.f[2] = arg0->points[i].f[1];

        if (chrpropTestPointInPolygon(&sp48, arg2, arg3) != 0)
        {
            return 1;
        }
    }

    for (i=0; i<arg3; i++)
    {
        sp48.f[0] = arg2->points[i].f[0];
        sp48.f[1] = 0.0f;
        sp48.f[2] = arg2->points[i].f[1];

        if (chrpropTestPointInPolygon(&sp48, arg0, arg1) != 0)
        {
            return 1;
        }
    }
#endif

    if (chrobjSeparatingAxisTheorem(arg0, arg1, arg2, arg3))
    {
        return 0;
    }

    if (chrobjSeparatingAxisTheorem(arg2, arg3, arg0, arg1))
    {
        return 0;
    }

    return 1;
}






/**
 * Checks whether a point collision with a convex polygon is within the specified radius.
 * @param point: 3d point to test collision with polygon. Only (x,z) are used.
 * @param collision_radius: Collision radius of point to test.
 * @param polygon: Convex polygon.
 * @param edges: Number of edges to test in polygon.
 *
 * Address 0x7F044718.
*/
s32 chrobjTestPointPolygonCollision(struct coord3d *point, f32 collision_radius, struct rect4f *polygon, s32 edges)
{
    f32 temp_f0;
    f32 temp_f26;
    f32 px;
    f32 pz;
    f32 temp_f30;
    s32 i;
    struct coord2d *temp_s0;

    px = point->f[0];
    pz = point->f[2];

    for (i=0; i<edges; i++)
    {
        temp_s0 = &polygon->points[(i+1) % edges];

        temp_f0 = stanGetSignedPointLineDistance(polygon->points[i].f[0], polygon->points[i].f[1], temp_s0->f[0], temp_s0->f[1], px, pz);

        if (temp_f0 < 0.0f)
        {
            temp_f0 = -temp_f0;
        }

        temp_f26 = distBetweenPoints2d(polygon->points[i].f[0], polygon->points[i].f[1], px, pz);
        temp_f30 = distBetweenPoints2d(temp_s0->f[0], temp_s0->f[1], px, pz);

        if ((temp_f0 < collision_radius)
            && ((temp_f26 < collision_radius)
                || (temp_f30 < collision_radius)
                || stanPointProjectsOntoEdge(polygon->points[i].f[0], polygon->points[i].f[1], temp_s0->f[0], temp_s0->f[1], px, pz)
            )
        )
        {
            return 1;
        }
    }

    return 0;
}


#ifndef TARGET_N64
/* The truck's Bond-brush check (portVehiclePlayerBrush) re-runs the test with
 * one prop ignored, and needs to know which prop refused. */
static PropRecord *s_portCollideSkip;
static PropRecord *s_portCollideBlocker;
#endif

/**
 * NTSC address 0x7F0448A8.
*/
s32 sub_GAME_7F0448A8(struct PropRecord *argProp)
{
    s32 var_s0;
    struct rect4f *polygon2;
    s32 edges2;
    f32 chrTop;
    f32 chrBottom;
    s32 roomids[8];
    s16 *temp_s0;
    f32 height;
    f32 arbitratyNumber;
    f32 radius;
    f32 ground;
    PropRecord *propss;
    ChrRecord *chr;
    struct rect4f *polygon;
    s32 edges;
    f32 top;
    f32 bottom;

#ifndef TARGET_N64
    s_portCollideBlocker = NULL;
#endif
    chraiGetCollisionBounds(argProp, &polygon2, &edges2, &chrTop, &chrBottom);

    if (edges2 <= 0)
    {
        return 1;
    }

    chraiGetPropRoomIds(argProp, (s32*)&roomids);
    roomGetProps((s32*)&roomids);

    propss = (PropRecord *)&g_Props;

    for (temp_s0 = ptr_list_object_lookup_indices; *temp_s0 >= 0; temp_s0++)
    {
        PropRecord *prop = &propss[*temp_s0];

#ifndef TARGET_N64
        if (prop == s_portCollideSkip)
        {
            continue;
        }
#endif

        if (prop != argProp)
        {
            if ((prop->type == PROP_TYPE_VIEWER) || (prop->type == PROP_TYPE_CHR))
            {
                chr = prop->chr;
                if ((chr == NULL) || !(chr->chrflags & CHRFLAG_HIDDEN))
                {
                    chrpropGetCollisionBounds(prop, &radius, &height, &arbitratyNumber);

                    ground = sub_GAME_7F03CFE8(prop);
                    arbitratyNumber += ground;
                    height += ground;

                    if (arbitratyNumber <= chrTop)
                    {
                        var_s0 = 1;

                        if (chrBottom <= height)
                        {
                            if (chrpropTestPointInPolygon(&prop->pos, polygon2, edges2) != 0)
                            {
                                var_s0 = 0;
                            }

                            // 'else if' avoided here
                            if ((var_s0 != 0) && (chrobjTestPointPolygonCollision(&prop->pos, radius, polygon2, edges2) != 0))
                            {
                                var_s0 = 0;
                            }

                            if (var_s0 == 0)
                            {
#ifndef TARGET_N64
                                s_portCollideBlocker = prop;
#endif
                                if ((prop->type == PROP_TYPE_CHR) && (argProp->type == PROP_TYPE_DOOR))
                                {
                                    prop->chr->hidden |= CHRHIDDEN_OFFSCREEN_PATROL;
                                }

                                return 0;
                            }
                        }
                    }
                }
            }
            else if (
                ((prop->type == PROP_TYPE_OBJ) || (prop->type == PROP_TYPE_WEAPON) || (prop->type == PROP_TYPE_DOOR))
                && (
                    (argProp->type != PROP_TYPE_DOOR)
                    || ((prop->type != PROP_TYPE_DOOR) && ((prop->obj->type != PROPDEF_SAFE)) && (prop->obj->type != PROPDEF_AIRCRAFT))))
            {
                chraiGetCollisionBounds(prop, &polygon, &edges, &top, &bottom);

                if ((edges > 0)
                    && (bottom <= chrTop)
                    && (chrBottom <= top)
                    && (chrobjTestPolygonsTouchingOrOverlap2D(polygon, edges, polygon2, edges2) != 0))
                {
#ifndef TARGET_N64
                    s_portCollideBlocker = prop;
#endif
                    return 0;
                }
            }
        }
    }

    return 1;
}


/**
 * Address: 7F044B38
 */
s32 sub_GAME_7F044B38(ObjectRecord *obj)
{
    Model *model;
    PropRecord *prop;
    f32 *normalzptr;
    coord3d *modelpoint0;
    coord3d *modelpoint1;
    coord3d *modelpoint2;
    coord3d *modelpoint3;
    coord3d point0;
    coord3d point1;
    coord3d point2;
    coord3d point3;
    s32 result;
    Mtxf mtx;
    StandTile *tile;
    ModelRoData_BoundingBoxRecord *bbox;
    coord3d edge0;
    coord3d edge1;
    coord3d edge2;
    coord3d edge3;
    f32 cross2y;
    f32 cross0y;
    f32 cross1y;
    f32 cross3y;
    coord3d normal;
    coord3d yawvec;
    f32 ypos;

    model = obj->model;
    prop = obj->prop;
    modelpoint0 = (coord3d *) model->obj->Switches[1]->Data;
    modelpoint1 = (coord3d *) model->obj->Switches[2]->Data;
    modelpoint2 = (coord3d *) model->obj->Switches[3]->Data;
    modelpoint3 = (coord3d *) model->obj->Switches[4]->Data;
    result = 1;
    bbox = (ModelRoData_BoundingBoxRecord *) model->obj->Switches[6]->Data;

    matrix_4x4_set_rotation_around_y(((VehichleRecord *)obj)->roty, &mtx);
    matrix_scalar_multiply(model->scale, (f32 *) (&mtx));
    matrix_4x4_set_position(&obj->runtime_pos, &mtx);

    matrix_4x4_transform_vector(&mtx, modelpoint0, &point0);
    matrix_4x4_transform_vector(&mtx, modelpoint1, &point1);
    matrix_4x4_transform_vector(&mtx, modelpoint2, &point2);
    matrix_4x4_transform_vector(&mtx, modelpoint3, &point3);

    tile = prop->stan;
    if (walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, point0.x, point0.z))
    {
        point0.y = stanGetPositionYValue(tile, point0.x, point0.z);
    }
    else
    {
        result = 0;
    }

    tile = prop->stan;
    if (walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, point1.x, point1.z))
    {
        point1.y = stanGetPositionYValue(tile, point1.x, point1.z);
    }
    else
    {
        result = 0;
    }

    tile = prop->stan;
    if (walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, point2.x, point2.z))
    {
        point2.y = stanGetPositionYValue(tile, point2.x, point2.z);
    }
    else
    {
        result = 0;
    }

    tile = prop->stan;
    if (walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, point3.x, point3.z))
    {
        point3.y = stanGetPositionYValue(tile, point3.x, point3.z);
    }
    else
    {
        result = 0;
    }

    if (result)
    {
        edge0.x = point1.x - point0.x;
        edge0.y = point1.y - point0.y;
        edge0.z = point1.z - point0.z;
        guNormalize(&edge0.x, &edge0.y, &edge0.z);

        edge1.x = point3.x - point1.x;
        edge1.y = point3.y - point1.y;
        edge1.z = point3.z - point1.z;
        guNormalize(&edge1.x, &edge1.y, &edge1.z);

        edge2.x = point2.x - point3.x;
        edge2.y = point2.y - point3.y;
        edge2.z = point2.z - point3.z;
        guNormalize(&edge2.x, &edge2.y, &edge2.z);

        edge3.x = point0.x - point2.x;
        edge3.y = point0.y - point2.y;
        edge3.z = point0.z - point2.z;
        guNormalize(&edge3.x, &edge3.y, &edge3.z);

        normalzptr = &edge1.z;

        cross0y = (edge0.z * edge1.x) - ((*normalzptr) * edge0.x);
        cross1y = ((*normalzptr) * edge2.x) - (edge2.z * edge1.x);
        cross2y = (edge2.z * edge3.x) - (edge3.z * edge2.x);
        cross3y = (edge3.z * edge0.x) - (edge0.z * edge3.x);

        if (((cross1y <= cross0y) && (cross2y <= cross0y)) && (cross3y <= cross0y))
        {
            normal.x = (edge0.y * edge1.z) - (edge1.y * edge0.z);
            normal.y = cross0y;
            normal.z = (edge0.x * edge1.y) - (edge1.x * edge0.y);
        }
        else if ((cross2y <= cross1y) && (cross3y <= cross1y))
        {
            normal.x = (edge1.y * edge2.z) - (edge2.y * edge1.z);
            normal.y = cross1y;
            normal.z = (edge1.x * edge2.y) - (edge2.x * edge1.y);
        }
        else if (cross3y <= cross2y)
        {
            normal.x = (edge2.y * edge3.z) - (edge3.y * edge2.z);
            normal.y = cross2y;
            normal.z = (edge2.x * edge3.y) - (edge3.x * edge2.y);
        }
        else
        {
            normal.x = (edge3.y * edge0.z) - (edge0.y * edge3.z);
            normal.y = cross3y;
            normal.z = (edge3.x * edge0.y) - (edge0.x * edge3.y);
        }

        normalzptr = &normal.z;

        yawvec.x = sinf(((VehichleRecord *)obj)->roty);
        yawvec.y = 0.0f;
        yawvec.z = cosf(((VehichleRecord *)obj)->roty);

        matrix_4x4_set_identity(&obj->mtx);

        if (obj->mtx.m[0][0])
        {
            // empty
        }

        if (obj->mtx.m[1][1])
        {
            // empty
        }

        obj->mtx.m[1][0] = normal.x;

        if (obj->mtx.m[1][0])
        {
            // empty
        }

        obj->mtx.m[1][1] = normal.y;

        if ((&yawvec) && (&yawvec))
        {
            // empty
        }

        obj->mtx.m[1][2] = ((f32*)&normal)[2];

        if (normal.x)
        {
            // empty
        }

        obj->mtx.m[0][0] = (obj->mtx.m[1][1] * yawvec.f[2]) - (obj->mtx.m[1][2] * yawvec.f[1]);
        obj->mtx.m[0][1] = (obj->mtx.m[1][2] * yawvec.f[0]) - (obj->mtx.m[1][0] * yawvec.f[2]);
        obj->mtx.m[0][2] = (obj->mtx.m[1][0] * (f32)(yawvec.y)) - (obj->mtx.m[1][1] * yawvec.f[0]);

        obj->mtx.m[2][0] = (((f32 *)(&obj->mtx))[1] * ((f32 *)(&obj->mtx))[6]) - (((f32 *)(&obj->mtx))[2] * obj->mtx.m[1][1]);
        obj->mtx.m[2][1] = (((f32 *)(&obj->mtx))[2] * ((f32 *)(&obj->mtx))[4]) - (((f32 *)(&obj->mtx))[0] * ((f32 *)(&obj->mtx))[6]);
        obj->mtx.m[2][2] = (((f32 *)(&obj->mtx))[0] * obj->mtx.m[1][1]) - (((f32 *)(&obj->mtx))[1] * ((f32 *)(&obj->mtx))[4]);

        matrix_scalar_multiply(model->scale, (f32 *) (&obj->mtx));
        matrix_4x4_transform_vector(&obj->mtx, modelpoint0, &point1);

        ypos = (point0.y - (chrpropBBOXGetYmin(bbox) * model->scale)) - point1.y;
        prop->pos.y = ypos;
        obj->runtime_pos.y = ypos;
    }
    else
    {
        matrix_4x4_set_rotation_around_y(((VehichleRecord *)obj)->roty, &obj->mtx);
        matrix_scalar_multiply(model->scale, (f32 *) (&obj->mtx));

        ypos = stanGetPositionYValue(prop->stan, prop->pos.x, prop->pos.z) - ((chrpropBBOXGetYmin(bbox) + modelpoint0->f[1]) * model->scale);

        prop->pos.y = ypos;
        obj->runtime_pos.y = ypos;
    }

    return result;
}


#ifndef TARGET_N64
/* PORT: windowed doors read their opaque distance from the raw 32-bit word at
 * DoorRecord+0xC4, but the port's little-endian _mkword/_mkshort swap its
 * halves, so door glass always drew fully clear. Reassemble the word in N64
 * byte order from the typed fields. */
static s32 doorGlassOpaqueDistWord(struct DoorRecord *door)
{
    return ((s32) door->CullDist << 16) | ((s32) (u8) door->soundType << 8) | (s32) (u8) door->fadeTime60;
}
#endif

s32 glassCalculateOpacity(coord3d *pos, f32 xludist, f32 opadist, f32 arg3)
{
    coord3d *campos = &getCurrentPlayerProp()->pos;
    s32 opacity;
    f32 xdiff = pos->x - campos->x;
    f32 ydiff = pos->y - campos->y;
    f32 zdiff = pos->z - campos->z;

    f32 distance = sqrtf(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

    if (distance > opadist)
    {
        opacity = 255;
    } else if (distance < xludist)
    {
        opacity = arg3 * 255;
    }
    else
    {
        opacity = (((distance - xludist) * (1.0f - arg3)) / (opadist - xludist) + arg3) * 255;
    }

    return opacity;
}


#ifndef TARGET_N64
#define PORT_VEHICLE_CARRY_MAX 8
#define PORT_VEHICLE_BRUSH_SLACK 0.01f /* float noise in a player's distance to the truck near x=17000 */

/* Far from the origin a short step is only a few float ulps, so rounding
 * loses part of the truck's sideways motion every tick and it drifts off its
 * heading into door frames. Carry each truck's rounding remainder to its next step. */
struct PortVehicleCarry
{
    VehichleRecord *truck;
    f32 x;
    f32 z;
    f64 remx;
    f64 remz;
    s32 lossfield; /* g_GlobalTimer of the truck's last refusal speed loss */
};

static struct PortVehicleCarry s_portVehicleCarry[PORT_VEHICLE_CARRY_MAX];
static struct PortVehicleCarry s_portVehicleCarryPending;

/* Stage load (lv.c): the slots key on setup addresses the new stage reuses,
 * and entries left by earlier stages would otherwise fill the table for good. */
void portVehicleCarryReset(void)
{
    s32 i;

    for (i = 0; i < PORT_VEHICLE_CARRY_MAX; i++)
    {
        s_portVehicleCarry[i].truck = NULL;
    }

    s_portVehicleCarryPending.truck = NULL;
}

static void portVehicleCarryStep(VehichleRecord *truck, f64 stepx, f64 stepz, struct coord3d *dest)
{
    struct PortVehicleCarry *c = NULL;
    f64 remx = 0.0;
    f64 remz = 0.0;
    f64 wantx;
    f64 wantz;
    s32 i;

    s_portVehicleCarryPending.truck = NULL;

    if (!portFastTickFixOn())
    {
        return;
    }

    for (i = 0; i < PORT_VEHICLE_CARRY_MAX; i++)
    {
        if (s_portVehicleCarry[i].truck == truck)
        {
            c = &s_portVehicleCarry[i];
            break;
        }
    }

    if (c != NULL && c->x == truck->runtime_pos.f[0] && c->z == truck->runtime_pos.f[2])
    {
        remx = c->remx;
        remz = c->remz;
    }

    wantx = (f64) truck->runtime_pos.f[0] + stepx + remx;
    wantz = (f64) truck->runtime_pos.f[2] + stepz + remz;
    dest->f[0] = (f32) wantx;
    dest->f[2] = (f32) wantz;

    s_portVehicleCarryPending.truck = truck;
    s_portVehicleCarryPending.x = dest->f[0];
    s_portVehicleCarryPending.z = dest->f[2];
    s_portVehicleCarryPending.remx = wantx - (f64) dest->f[0];
    s_portVehicleCarryPending.remz = wantz - (f64) dest->f[2];
}

/* The move was taken: keep its remainder for the truck's next step. */
static void portVehicleCarryCommit(VehichleRecord *truck)
{
    s32 i;
    s32 slot = 0;

    if (s_portVehicleCarryPending.truck != truck)
    {
        return;
    }

    for (i = 0; i < PORT_VEHICLE_CARRY_MAX; i++)
    {
        if (s_portVehicleCarry[i].truck == truck || s_portVehicleCarry[i].truck == NULL)
        {
            slot = i;
            break;
        }
    }

    if (s_portVehicleCarry[slot].truck != truck)
    {
        s_portVehicleCarry[slot].lossfield = g_GlobalTimer - 2;
    }

    s_portVehicleCarry[slot].truck = truck;
    s_portVehicleCarry[slot].x = s_portVehicleCarryPending.x;
    s_portVehicleCarry[slot].z = s_portVehicleCarryPending.z;
    s_portVehicleCarry[slot].remx = s_portVehicleCarryPending.remx;
    s_portVehicleCarry[slot].remz = s_portVehicleCarryPending.remz;
    s_portVehicleCarryPending.truck = NULL;
}

/* A refused step zeroes the truck's speed (vanilla). At 30 fps a refusal can
 * cost it at most once a frame; a faster tick tries many more steps per field,
 * so brushing past Bond kept zeroing it and the truck crawled. On ticks under
 * two fields the speed loss runs at most once per two fields; a refused step
 * in between just doesn't move the truck. */
static s32 portVehicleSpeedLossDue(VehichleRecord *truck)
{
    struct PortVehicleCarry *c = NULL;
    s32 i;

    if (!portFastTickFixOn())
    {
        return 1;
    }

    for (i = 0; i < PORT_VEHICLE_CARRY_MAX; i++)
    {
        if (s_portVehicleCarry[i].truck == truck)
        {
            c = &s_portVehicleCarry[i];
            break;
        }
    }

    for (i = 0; c == NULL && i < PORT_VEHICLE_CARRY_MAX; i++)
    {
        if (s_portVehicleCarry[i].truck == NULL)
        {
            c = &s_portVehicleCarry[i];
            c->truck = truck;
            c->x = c->z = 0.0f;
            c->remx = c->remz = 0.0;
            c->lossfield = g_GlobalTimer - 2;
        }
    }

    if (c == NULL)
    {
        return 1;
    }

    if (g_GlobalTimer >= c->lossfield && g_GlobalTimer - c->lossfield < 2)
    {
        return 0;
    }

    c->lossfield = g_GlobalTimer;
    return 1;
}

/* Distance from (x, z) to the polygon's outline, 0 inside it. */
static f32 portPointPolygonDist2D(f32 x, f32 z, struct rect4f *poly, s32 edges)
{
    f32 best = -1.0f;
    s32 inside = 0;
    s32 i;

    for (i = 0; i < edges; i++)
    {
        struct coord2d *a = &poly->points[i];
        struct coord2d *b = &poly->points[(i + 1) % edges];
        f32 ex = b->f[0] - a->f[0];
        f32 ez = b->f[1] - a->f[1];
        f32 len2 = ex * ex + ez * ez;
        f32 t = 0.0f;
        f32 dx;
        f32 dz;
        f32 d;

        if (len2 > 0.0f)
        {
            t = ((x - a->f[0]) * ex + (z - a->f[1]) * ez) / len2;
            t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
        }

        dx = x - (a->f[0] + t * ex);
        dz = z - (a->f[1] + t * ez);
        d = sqrtf(dx * dx + dz * dz);

        if (best < 0.0f || d < best)
        {
            best = d;
        }

        if (((a->f[1] > z) != (b->f[1] > z)) && (x < a->f[0] + (ex * (z - a->f[1])) / ez))
        {
            inside = !inside;
        }
    }

    return inside ? 0.0f : best;
}

/* Bond running beside the truck stopped it on fast ticks: his own collision
 * holds him at his radius from its side, so whether the touch test refuses a
 * step is float noise, and a fast tick takes enough steps that some always
 * are; on Hardware pacing the same light touch refuses none. When the only
 * prop refusing a straight step is a player who is not ahead of the truck's
 * front, and the step brings the truck no closer to him, the step is taken.
 * A turn into him (the Dam tunnel entrance), a player in front, doors, guards
 * and objects still refuse it. Same gate as the other fast-tick fixes, so 30
 * fps and below, Hardware pacing and demos stay vanilla. */
static s32 portVehiclePlayerBrush(PropRecord *prop, struct coord3d *from, f32 roty, f32 previousYaw)
{
    PropRecord *player = s_portCollideBlocker;
    struct rect4f *poly;
    s32 edges;
    f32 top;
    f32 bottom;
    f32 fx;
    f32 fz;
    f32 front = 0.0f;
    f32 before;
    f32 after;
    f32 radius;
    f32 height;
    f32 ground;
    s32 clear;
    s32 i;

    if (!portFastTickFixOn() || player == NULL || player->type != PROP_TYPE_VIEWER || roty != previousYaw)
    {
        return 0;
    }

    /* The heading, not the step: a slow truck's step is a float ulp or two
     * and points anywhere. */
    fx = sinf(roty);
    fz = cosf(roty);
    chraiGetCollisionBounds(prop, &poly, &edges, &top, &bottom);

    if (edges <= 0)
    {
        return 0;
    }

    for (i = 0; i < edges; i++)
    {
        f32 f = (poly->points[i].f[0] - prop->pos.f[0]) * fx + (poly->points[i].f[1] - prop->pos.f[2]) * fz;

        if (i == 0 || f > front)
        {
            front = f;
        }
    }

    if ((player->pos.f[0] - prop->pos.f[0]) * fx + (player->pos.f[2] - prop->pos.f[2]) * fz > front)
    {
        return 0;
    }

    /* The polygon only moved, so the one before the step is this one shifted back. */
    after = portPointPolygonDist2D(player->pos.f[0], player->pos.f[2], poly, edges);
    before = portPointPolygonDist2D(player->pos.f[0] + (prop->pos.f[0] - from->f[0]), player->pos.f[2] + (prop->pos.f[2] - from->f[2]), poly, edges);

    /* No closer than he already is, or still outside his radius but for float
     * noise, so a run of tiny steps can never creep the truck into him. */
    chrpropGetCollisionBounds(player, &radius, &height, &ground);

    if (after < before && after < radius - PORT_VEHICLE_BRUSH_SLACK)
    {
        return 0;
    }

    s_portCollideSkip = player;
    clear = sub_GAME_7F0448A8(prop);
    s_portCollideSkip = NULL;

    return clear;
}
#endif

s32 objTick(struct PropRecord *prop)
{
	Mtxf *mtxs;
	f32 temp_f14_3;
	struct coord3d RocketCurrent;
	s32 objMovedThisFrame;
	f32 temp_f20;
	f32 nextVerticalSpeed;
	struct PropRecord *sp684;
	TICKOP tickop;
	f32 previousOpenPosition; // Start-of-tick snapshot for how open a door is.

    /** 
     * Relevant only for MP. TRUE on the one pass per frame that advances this object's
	 * shared state (projectile physics, door sounds) -- things you want to calculate
     * only once per frame, not for every player per frame. Normally applies to
	 * first player in the shuffled order determined by get_player_position_in_shuffled().
     * But for an in-flight projectile it applies to the player who fired it.
	 * Code outside this guard is per-viewport render setup and runs every pass. 
     */
	bool isSimOwner;

	s32 playerCount;
	bool applyFogCull;
	struct ALSoundState *sfx_state;
	s32 projectileAlive;

    /**
     * 0 - the move was blocked by geometry
     * 1 - the move completed unobstructed
     * 2 - the move was blocked and the caller asked for embedding
     */
	s32 moveResult;

	struct coord3d sp658;
	struct coord3d sp64C;
	struct WeaponObjRecord *weaponObj;
	f32 temp_f12_5;
	struct ModelRoData_BoundingBoxRecord *projectileBBox;
	f32 sp63C;
	f32 bboxBottomOffset;
	struct coord3d previousXAxis; // snapshot of the object's local X axis before this tick's rotation
	struct coord3d collisionNormal;
	struct coord3d collisionPoint;
	s32 hitGround;
	s32 bounceCondition;
	struct Projectile *projectile;
	s32 projectileStopped;
	struct coord3d pad5F8;
	struct coord3d pad5EC;
	f32 temp_f0_13;
	s32 cctvSeesPlayer;
	f32 var_f2;
	f32 temp_f0_14;
	f32 angleDelta;
	struct coord3d bloodStainPos;
	Mtxf inverseNodeMatrix;
	Mtxf *temp_s0_10;
	f32 previousVerticalSpeed;
	struct PropRecord *playerProp2;
	f32 yawError;
	s32 var_v0_3;
	f32 var_f0_2;
	f32 m_PropGravity;
	struct coord3d sp564;
	f32 temp_f0_31;
	f32 sp550[4];
	struct ModelRoData_BoundingBoxRecord *objectBBox;
	s32 sp548;
	struct coord3d sp53C;
	struct coord3d sp530;
#if defined(VERSION_JP) || defined(VERSION_EU)
	u32 jp_stack_pad[1];
#endif
	s32 temp_v0_32;
	s32 *temp_a1_6;
	f32 angleDelta_7;
	struct coord3d *temp_s0_13;
	struct coord3d *temp_s0_14;
	f32 m_RocketGravity;
	struct PropRecord *playerProp;
	f32 xdiff;
	f32 ydiff;
	struct ModelNode **temp_v1_7;
	f32 var_f0_3;
	struct Projectile *temp_v0_40;
	waypoint *currentWaypoint;
	Mtxf *temp_s0_21;
	struct WeaponObjRecord *airborneWeapon;
	struct StandTile *sp4F0;
	f32 angleDelta_9;
	s32 var_a0_6;
	struct WeaponObjRecord * temp_v1_10;
	Mtxf *temp_s2_7;
	struct Projectile * Rocket;
	f32 sp4D8;
	f32 targetPitch;
	struct coord3d *temp_v1_11;
	f32 temp_f20_4;
	f32 temp_f0_35;
	struct coord3d playerDirVec;
	f32 horizontalDistSq;
	f32 distanceToPlayer;
	f32 horizontalDist;
	s32 AutogunSeesPlayer;
	s32 isTracking;
	s32 hasLineOfSight;
	f32 sp4A0;
	f32 playerYaw;
	f32 playerPitch;
	f32 sp494;
	struct StandTile *collisionTile;
	f32 angleDelta_6;
	f32 var_f2_7;
	f32 temp_f2_23;
	struct ObjectRecord *obj;
	f32 targetYaw;
	struct coord3d *waypointPosition;
	struct Model *model;
	f32 temp_f12;
	bool canEmbed;
	struct StandTile *currentTile;
	s32 var_a0;
	f32 sp460;
	struct StandTile *temp_s2;
	struct coord3d sp450;
	struct StandTile *nextTile;
	f32 temp_f14_2;
	struct coord3d forwardDir;
	f32 previousYaw;
	f32 sp434;
	f32 truckAngularVelocity;
	struct coord3d vec424;
	struct coord3d vec418;
	struct coord3d vec40C;
	struct coord3d vec400;
	struct ObjectRecord *temp_v0_31;
	struct CCTVRecord * bottom_pad;
	Mtxf *objectMatrix;
	Mtxf *projectileMatrix;
	u32 pad3E4[3];
	Mtxf tempMatrix2;
	u32 pad3A0;
	struct DoorRecord *sp39C;
	u32 render_pad398;
	f32 sp394;
	Mtxf *sp390;
	s32 sp38C;
	Mtxf * render_pad388;
	f32 sp384;
	f32 sp380;
	u32 tempMatrix_head[3];
	struct CCTVRecord *sp370;
	struct DoorRecord * pad36C;
	struct coord3d sp360;
	Mtxf tempMatrix;
	struct AutogunRecord * poAGun;
	struct AutogunRecord *sp318;
	struct VehichleRecord * poTruck;
	struct coord3d sp308;
	f32 sp304;
	f32 sp300;
	Mtxf *sp2FC;
	s32 sp2F8;
	struct AircraftRecord * render_pad2F4;
	u32 pad2EC[2];
	Mtxf sp2AC;
	Mtxf sp26C;
	struct TintedGlassRecord * pad268;
	f32 var_f2_6;
	struct coord3d *sp260;
	struct coord3d *sp25C;
	struct coord3d *sp258;
	struct coord3d *sp254;
	f32 sp250;
	f32 sp24C;
	f32 sp248;
	s32 var_v1_5;
	s32 truckShouldPlayEngineSound;
	Mtxf sp200;
	struct coord3d *sp1FC;
	struct PadRecord *var_v1_4;
	struct ModelFileHeader *temp_v0_29;
	s32 var_s2_6;
	ModelRenderData sp1B0;
	struct DoorRecord * pad1AC;
	Mtxf sp16C;
	struct DoorRecord * door;
	struct coord3d *sp164;
	struct coord3d *sp160;
	struct coord3d *sp15C;
	struct ModelRoData_BoundingBoxRecord *sp158;
	f32 sp154;
	bool moveOnlyIfPathClear;
	struct Model *temp_s0_6;
	f32 *temp_v0_25;
	struct coord3d *sp168;
	f32 *temp_s0_5;
	s32 sp13C;
	s32 sp138;
	struct coord3d sp12C;
	struct coord3d sp120;
	s32 sp11C;
	struct coord3d sp110;
	struct StandTile *sp10C;
	struct StandTile *sp108;
	s32 sp104;
	struct PropRecord *sp100;
	struct beam *beam;
	s32 var_s2_5;
	Mtxf spB8;
    
	obj = prop->obj;
	model = obj->model;

	objMovedThisFrame = 0;
	tickop = TICKOP_NONE;
    
    /**
     * Since objMovedThisFrame and tickop are both set to 0 right above here,
     * this condition can never be true. previousOpenPosition is unitialized so even if this did execute
     * it would subtract from garbage. But that wouldn't matter since previousOpenPosition is set 0.0f right after this anyway.
     */
	if (objMovedThisFrame > tickop)
	{
		previousOpenPosition -= (getjointsize(g_CurrentProjectileModel, dword_CODE_bss_80075B74) * 0.5f) * flt_CODE_bss_80075B88.f[0];
	}

	previousOpenPosition = 0.0f;
	playerCount = getPlayerCount();
	applyFogCull = TRUE;

	if (obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE)
	{
		objFree(obj, 0, obj->state & PROPSTATE_RESPAWN);
		return 1;
	}
	else if (obj->runtime_bitflags & RUNTIMEBITFLAG_ISRETICK)
	{
		obj->runtime_bitflags &= ~RUNTIMEBITFLAG_ISRETICK;
	}
	else if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
	{
		prop->flags &= 0xFFFD;
		obj->runtime_bitflags |= RUNTIMEBITFLAG_ISRETICK;
		return 3;
	}

	if (playerCount == 1)
	{
		isSimOwner = TRUE;
	}
	else
	{
		isSimOwner = get_player_position_in_shuffled(get_cur_playernum()) == 0;

		if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
		{
			projectile = obj->projectile;

			if (projectile->ownerprop != NULL
#if defined(VERSION_JP) || defined(VERSION_EU)
				&& getPlayerPointerIndex(projectile->ownerprop) >= 0
#endif
			)
			
            isSimOwner = projectile->ownerprop == g_CurrentPlayer->prop;
			
		}
	}

    /**
     * sp100 is a PropRecord* but is used to store a color here. It should really be two
     * separate variables, and probably was, but got merged during decompilation.
     */
#if !defined(VERSION_EU)
	sp100 = (struct PropRecord *) &obj->nextcol;
#endif
	if (isSimOwner)
	{
		if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
		{
			Rocket = obj->projectile;
			Rocket->age += g_ClockTimer;

			if (((s32) Rocket->age) > PROJECTILE_LIFETIME_FRAMES)
			{
				obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
			}

			if (Rocket->flags & PROJECTILEFLAG_00000100)
			{
				moveOnlyIfPathClear = TRUE;

				if (obj->type == PROPDEF_COLLECTABLE)
				{
					weaponObj = (struct WeaponObjRecord *) obj;
                    
					if (weaponObj->weaponnum == ITEM_ROCKETROUND)
					{
						moveOnlyIfPathClear = FALSE;
					}
				}

				if (Rocket->ownerprop != NULL)
				{
					sub_GAME_7F03D058(Rocket->ownerprop, 0);
				}

				moveResult = sub_GAME_7F042EB4(obj, &Rocket->unkd4, &sp64C, &sp658, 0, moveOnlyIfPathClear);

				if (Rocket->ownerprop != NULL)
				{
					sub_GAME_7F03D058(Rocket->ownerprop, 1);
				}

                /*
                * Fragile code: PROJECTILEFLAG_00000100 is only set by gunInitProjectileFromPlayer()
                * whose callers always pass a PROPDEF_COLLECTABLE WeaponObjRecord.
                * Therefore weaponObj is initialized above on every valid path reaching this point.
                * But if this flag is ever used with another object type, the NULL check
                * below would read an uninitialized pointer.
                */
				if (((moveResult != 1) && (weaponObj != NULL)) && (weaponObj->weaponnum == ITEM_ROCKETROUND))
				{
					weaponObj->timer = 0;
				}

				Rocket->flags &= ~PROJECTILEFLAG_00000100;
			}

			RocketCurrent.f[0] = obj->runtime_pos.f[0];
			RocketCurrent.f[1] = obj->runtime_pos.f[1];
			RocketCurrent.f[2] = obj->runtime_pos.f[2];

			if (Rocket->refreshrate > 0)
			{
				Rocket->refreshrate -= g_ClockTimer;
			}

			if (obj->projectile->flags & PROJECTILEFLAG_AIRBORNE)
			{
				airborneWeapon = (struct WeaponObjRecord *) obj;
				projectileBBox = chrobjGetBboxFromObjectRecord(obj);
				hitGround = 0;
				bounceCondition = 0;
				projectileStopped = 0;
				bboxBottomOffset = 1.0f;
				temp_f20 = obj->runtime_pos.f[1];
				canEmbed = FALSE;
				Rocket->unkA8 += g_ClockTimer;
				previousXAxis.f[0] = obj->mtx.m[0][0];
				previousXAxis.f[1] = obj->mtx.m[0][1];
				previousXAxis.f[2] = obj->mtx.m[0][2];

				if (Rocket->flags & PROJECTILEFLAG_00000020)
				{
					m_RocketGravity = ROCKET_INITIAL_GRAVITY_MODIFIER;
					if (Rocket->unk1C < m_RocketGravity)
					{
						Rocket->unkB4 += Rocket->unk10.f[1] * g_GlobalTimerDelta;
						Rocket->unkB0 += Rocket->unkB4 * g_GlobalTimerDelta;
                        // Gravity modifier increases at 1/90 per frame until reaching ROCKET_INITIAL_GRAVITY_MODIFIER
                        // I would have thought this is somehow related to turning 9.8m/s/s into per frame accel? 
						Rocket->unk1C += (1.0f / 90.0f) * g_GlobalTimerDelta;
						if (Rocket->unk1C > m_RocketGravity)
						{
							Rocket->unk1C = m_RocketGravity;
						}
					}
					else if (RocketCurrent.f[1] < Rocket->unkB0)
					{
						Rocket->unkB4 += Rocket->unk10.f[1] * g_GlobalTimerDelta;
						Rocket->unkB0 += Rocket->unkB4 * g_GlobalTimerDelta;
                        //Smooth vertical interpolation when rocket is rising toward its ballistic apex.
						RocketCurrent.f[1] += (0.07f * (Rocket->unkB0 - RocketCurrent.f[1])) * g_GlobalTimerDelta;
					}
					else
					{
						RocketCurrent.f[1] = Rocket->unkB0;
						Rocket->flags &= ~PROJECTILEFLAG_00000020;
						Rocket->unk1C = 0.0f;
						Rocket->flags |= PROJECTILEFLAG_POWERED;
						Rocket->speed.f[1] = Rocket->unkB4;
					}
				}

				m_PropGravity = PROP_PROJECTILE_GRAVITY_MODIFIER;

				if (!(Rocket->flags & PROJECTILEFLAG_POWERED))
				{
					Rocket->speed.f[1] += (Rocket->unk10.f[1] + Rocket->unk1C) * g_GlobalTimerDelta;
					temp_f12 = Rocket->speed.f[1];
					nextVerticalSpeed = temp_f12 - (m_PropGravity * g_GlobalTimerDelta);
                    //apparently Standard trapezoidal integrator. (y += (dt * (v + v_next)) * 0.5f)
					RocketCurrent.f[1] += (g_GlobalTimerDelta * (temp_f12 + nextVerticalSpeed)) * 0.5f;
					Rocket->speed.f[1] = nextVerticalSpeed;
				}
				else
				{
					Rocket->speed.f[1] += (Rocket->unk10.f[1] + Rocket->unk1C) * g_GlobalTimerDelta;
					RocketCurrent.f[1] += Rocket->speed.f[1] * g_GlobalTimerDelta;
				}

				objectMatrix = &obj->mtx;
				projectileMatrix = &Rocket->mtx;
				Rocket->speed.f[0] += Rocket->unk10.f[0] * g_GlobalTimerDelta;
				Rocket->speed.f[2] += Rocket->unk10.f[2] * g_GlobalTimerDelta;
				RocketCurrent.f[0] += Rocket->speed.f[0] * g_GlobalTimerDelta;
				RocketCurrent.f[2] += Rocket->speed.f[2] * g_GlobalTimerDelta;
				sub_GAME_7F057DF8(objectMatrix, projectileMatrix, g_ClockTimer);

                // Determine which projectiles can stick to surfaces.
				if ((obj->type == PROPDEF_COLLECTABLE) && (((((((airborneWeapon->weaponnum == ITEM_REMOTEMINE) || (airborneWeapon->weaponnum == ITEM_TIMEDMINE)) || (airborneWeapon->weaponnum == ITEM_PROXIMITYMINE)) || (airborneWeapon->weaponnum == ITEM_BOMBCASE)) || (airborneWeapon->weaponnum == ITEM_BUG)) || (airborneWeapon->weaponnum == ITEM_MICROCAMERA)) || (airborneWeapon->weaponnum == ITEM_PLASTIQUE)))
				{
					canEmbed = TRUE;
				}

				if (Rocket->ownerprop != NULL)
				{
					sub_GAME_7F03D058(Rocket->ownerprop, 0);
				}

				moveResult = sub_GAME_7F042EB4(obj, &RocketCurrent.f[0], &collisionPoint, &collisionNormal, canEmbed, 0);

				if (Rocket->ownerprop != NULL)
				{
					sub_GAME_7F03D058(Rocket->ownerprop, 1);
				}

				objMovedThisFrame = 1;

				if ((moveResult == 2) && (((temp_v1_11 = (struct coord3d *) D_80030B0C) == NULL) || ((((struct PropRecord *) temp_v1_11)->type != PROP_TYPE_CHR) && (((struct PropRecord *) temp_v1_11)->type != PROP_TYPE_VIEWER))))
				{
					sp548 = 0;
					if ((temp_v1_11 != NULL) && ((temp_v0_31 = ((struct PropRecord *) temp_v1_11)->obj)->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE))
					{
						sp548 = 1;
					}

					if (sp548 == 0)
					{
						projectileFree(Rocket);
						obj->projectile = NULL;
						obj->runtime_bitflags &= ~RUNTIMEBITFLAG_HASPROJECTILE;
						if (prop->flags & PROPFLAG_00000008)
						{
							prop->flags |= PROPFLAG_00000010;
						}

						chrobjSndCreatePostEventDefault(sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, ATTACH_MINE_SFX, NULL), &prop->pos);
						objectivestatusCheckDeposit(((struct WeaponObjRecord *) obj)->weaponnum, prop->stan->room);
						sub_GAME_7F0439B8(obj, &collisionPoint, prop->stan, &collisionNormal);
						if (D_80030B0C != NULL)
						{
							temp_s2 = prop->stan;
							if (objEmbed(prop, D_80030B0C, g_CurrentProjectileModel, dword_CODE_bss_80075B74) != 0)
							{
								prop->stan = temp_s2;
								tickop = TICKOP_CHANGEDLIST;
								projectileStopped = 1;
							}
						}
					}
				}

				if (projectileStopped == 0)
				{
					playerProp2 = D_80030B0C;

					if ((playerProp2 != NULL) && (obj->type == PROPDEF_COLLECTABLE))
					{
						temp_v1_10 = (struct WeaponObjRecord *) obj;

						if (temp_v1_10->weaponnum == ITEM_THROWKNIFE)
						{
							if ((playerProp2->type == PROP_TYPE_CHR) || (((playerProp2->type == PROP_TYPE_VIEWER) && (playerProp2->obj != NULL)) && (getPlayerPointerIndex(playerProp2) != get_cur_playernum())))
							{
								playerProp2 = D_80030B0C;
								temp_v0_40 = obj->projectile;
								temp_s0_13 = (struct coord3d *) playerProp2->chr;

								if ((((temp_v0_40->flags & PROJECTILEFLAG_AIRBORNE) && (((s32) temp_v0_40->unk90) <= 0)) && (obj->runtime_bitflags & RUNTIMEBITFLAG_THROWING_KNIFE_RELATED)) && (handles_shot_actors((struct ChrRecord *) temp_s0_13, bodypartshot, &flt_CODE_bss_80075B78, ((struct WeaponObjRecord *) obj)->weaponnum, 1) != 0))
								{
									projectileStopped = 1;

									if (Rocket->unk8C > 0.0f)
									{
										temp_f14_3 = ((Rocket->speed.f[0] * collisionNormal.f[0]) + (Rocket->speed.f[1] * collisionNormal.f[1])) + (Rocket->speed.f[2] * collisionNormal.f[2]);
										temp_f14_3 *= -(Rocket->unk8C + 1.0f);
										Rocket->speed.f[0] += temp_f14_3 * collisionNormal.f[0];
										Rocket->speed.f[1] += temp_f14_3 * collisionNormal.f[1];
										Rocket->speed.f[2] += temp_f14_3 * collisionNormal.f[2];
									}

									if (!(Rocket->flags & PROJECTILEFLAG_00000200))
									{
										mtxLoadRandomRotation(projectileMatrix);
									}

									Rocket->unk90 += 1;
									recall_joy2_hits_edit_detail_edit_flag(((struct WeaponObjRecord *) obj)->weaponnum, D_80030B0C, -1);

									if (((D_80030B0C->flags & PROPFLAG_ONSCREEN) && (bodypartshot != HIT_GUN)) && (bodypartshot != HIT_HAT))
									{
										playerProp2 = (struct PropRecord *) modelFindNodeMtx(g_CurrentProjectileModel, dword_CODE_bss_80075B74, 0);

										bloodStainPos.f[0] = collisionPoint.f[0];
										bloodStainPos.f[1] = collisionPoint.f[1];
										bloodStainPos.f[2] = collisionPoint.f[2];

										mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &bloodStainPos);

										bloodStainPos.f[0] += (bloodStainPos.f[0] - ((Mtxf *) playerProp2)->m[3][0]) * 0.5f;
										bloodStainPos.f[1] += (bloodStainPos.f[1] - ((Mtxf *) playerProp2)->m[3][1]) * 0.5f;
										bloodStainPos.f[2] += (bloodStainPos.f[2] - ((Mtxf *) playerProp2)->m[3][2]) * 0.5f;

										bloodStainPos.f[0] -= (getjointsize(g_CurrentProjectileModel, dword_CODE_bss_80075B74) * 0.5f) * flt_CODE_bss_80075B88.f[0];
										bloodStainPos.f[1] -= (getjointsize(g_CurrentProjectileModel, dword_CODE_bss_80075B74) * 0.5f) * flt_CODE_bss_80075B88.f[1];
										bloodStainPos.f[2] -= (getjointsize(g_CurrentProjectileModel, dword_CODE_bss_80075B74) * 0.5f) * flt_CODE_bss_80075B88.f[2];
    
										matrix_4x4_set_inverse_rotation_and_translation((Mtxf *) playerProp2, &inverseNodeMatrix);
										mtx4TransformVecInPlace(&inverseNodeMatrix, &bloodStainPos);
										chrCreateBloodStain(g_CurrentProjectileModel, bodypartshot, dword_CODE_bss_80075B74, &bloodStainPos);
									}
								}
							}
						}
						else if (temp_v1_10->weaponnum == ITEM_ROCKETROUND)
						{
							var_v0_3 = playerProp2->type;
							projectileStopped = 1;
							if (var_v0_3 == 3)
							{
								chrlvExplosionDamage((ChrRecord *) playerProp2->chr, &obj->runtime_pos, 2.0f, 1);
							}
							else if ((var_v0_3 == 1) || (var_v0_3 == 4))
							{
								var_a0 = obj->runtime_bitflags;
								objApplyDamage(playerProp2->obj, 100.0f, &obj->runtime_pos, ITEM_ROCKETROUND, (s32) (((u32) (var_a0 & RUNTIMEBITFLAG_OWNER)) >> RUNTIMEBITSHIFT_OWNER));
							}

							((struct WeaponObjRecord *) obj)->timer = 0;
						}
					}
				}

				if (projectileStopped == 0)
				{
					if (moveResult == 0)
					{
						if (Rocket->unk8C > 0.0f)
						{
							previousVerticalSpeed = Rocket->speed.f[1];
							temp_f14_3 = ((Rocket->speed.f[0] * collisionNormal.f[0]) + (Rocket->speed.f[1] * collisionNormal.f[1])) + (Rocket->speed.f[2] * collisionNormal.f[2]);
							temp_f14_3 *= -(Rocket->unk8C + 1.0f);
							Rocket->speed.f[0] += temp_f14_3 * collisionNormal.f[0];
							Rocket->speed.f[1] += temp_f14_3 * collisionNormal.f[1];
							Rocket->speed.f[2] += temp_f14_3 * collisionNormal.f[2];

							if ((previousVerticalSpeed <= 0.0f) && ((Rocket->speed.f[1] >= 0.0f) || (temp_f20 <= obj->runtime_pos.f[1])))
							{
								bounceCondition = 1;
							}
						}
					}

                    /** 
                     * sp63C is never written anywhere in this function, so this loads an uninitialised stack
					 * word albeit harmlessly. Either the block below overwrites temp_f20 with the stan floor
					 * height, or it does not run and hitGround stays 0, leaving temp_f20 unread. Likely the
					 * original had a separate uninitialised local for the floor height and the compiler
					 * materialised its home here.
					 * Note temp_f20 changes meaning at this point: above it is the Y at the start of the
					 * tick (used by the bounce test), below it's the stan floor height.
                     */
					temp_f20 = sp63C;

					if (!(Rocket->flags & PROJECTILEFLAG_00000008))
					{
						temp_f20 = stanGetPositionYValue(prop->stan, prop->pos.f[0], prop->pos.f[2]);
						bboxBottomOffset = chrpropSumMatrixPosY(projectileBBox, &objectMatrix[0]);
						hitGround = (prop->pos.f[1] < (temp_f20 - bboxBottomOffset));
					}
					else
					{
						if (Rocket && Rocket && Rocket);
					}

					if ((hitGround) || (moveResult == 0))
					{
						if (!(Rocket->flags & PROJECTILEFLAG_00000200))
						{
							mtxLoadRandomRotation(projectileMatrix);
						}

						Rocket->unk90 += 1;
					}

					if ((hitGround) || (bounceCondition))
					{
						if (hitGround)
						{
                            // The projectile has hit the ground so raise it 4 units above the ground.
							obj->runtime_pos.f[1] = (prop->pos.f[1] = (temp_f20 - bboxBottomOffset) + 4.0f);
						}
						else
						{
							var_f2 = (collisionPoint.f[1] - bboxBottomOffset) + 4.0f;
							obj->runtime_pos.f[1] = (prop->pos.f[1] = var_f2);
						}

						if (!(obj->runtime_bitflags & RUNTIMEBITFLAG_00010000))
						{
							obj->runtime_bitflags |= RUNTIMEBITFLAG_00000100;
						}

						if (Rocket->unk8C > 0.0f)
						{
							Rocket->speed.f[1] *= -Rocket->unk8C;

							if (Rocket->speed.f[1] < 2.2222223f)
							{
								if ((Rocket->flags & PROJECTILEFLAG_00000002) && (Rocket->unk90 == 1))
								{
									Rocket->speed.f[1] = 2.2222223f;
								}
								else
								{
									objSettle(obj, &previousXAxis);
								}
							}
						}
						else
						{
							objSettle(obj, &previousXAxis);
						}
					}

					if (obj->type == PROPDEF_COLLECTABLE)
					{
						if (airborneWeapon->weaponnum == ITEM_THROWKNIFE)
						{
#ifndef TARGET_N64
							if (portProjectileBelowWorld(obj))
							{
								/* Fallen below the level's lowest floor: remove
								 * it now instead of holding a g_Projectiles
								 * slot and replaying its whoosh until it ages
								 * out. RUNTIMEBITFLAG_REMOVE frees the slot and
								 * its sounds on the next tick. */
								obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
							}
							else
#endif
							{
								objUpdateThrowKnifeSound(airborneWeapon);
							}
						}
						else if (airborneWeapon->weaponnum == ITEM_ROCKETROUND)
						{
							if ((moveResult == 0) || (hitGround))
							{
								airborneWeapon->timer = 0;
							}
							else
							{
								nextVerticalSpeed = ((Rocket->speed.f[0] * Rocket->speed.f[0]) + (Rocket->speed.f[1] * Rocket->speed.f[1])) + (Rocket->speed.f[2] * Rocket->speed.f[2]);

								if (nextVerticalSpeed > ROCKET_SPEED_BREAK_THRESHOLD)
								{
									Rocket->unk10.f[0] = 0.0f;
									Rocket->unk10.f[1] = 0.0f;
									Rocket->unk10.f[2] = 0.0f;
								}

								if (((s32) Rocket->unkA8) >= GRENADE_SMOKE_FRAMES)
								{
									Rocket->unk1C = 0.0f;
									Rocket->flags &= ~(PROJECTILEFLAG_POWERED | PROJECTILEFLAG_00000020);
								}
								else
								{
									explosionCreateSmoke(&airborneWeapon->runtime_pos, prop->stan, 8, prop->rooms, (prop->flags & PROPFLAG_00000008) != 0);
								}
							}
						}
						else if (airborneWeapon->weaponnum == ITEM_GRENADEROUND)
						{
							if ((hitGround != 0) || (bounceCondition != 0))
							{
								airborneWeapon->timer = 0;
							}
							else
							{
								explosionCreateSmoke(&obj->runtime_pos, prop->stan, 9, prop->rooms, (prop->flags & PROPFLAG_00000008) != 0);
							}
						}

						if ((moveResult == 0) || (hitGround != 0))
						{
							if (((s32) Rocket->unkAC) < (((s32) g_GlobalTickCount) - 2))
							{
								if ((airborneWeapon->weaponnum == ITEM_THROWKNIFE) || (airborneWeapon->weaponnum == ITEM_KNIFE))
								{
									sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, KNIFE_HIT_WALL_SFX, NULL);
								}
								else
								{
									sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DROP_GUN_SFX, NULL);
								}

								chrobjSndCreatePostEventDefault(sfx_state, &prop->pos);
							}

							Rocket->unkAC = g_GlobalTickCount;
						}
					}
				}

				if (((airborneWeapon->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) && (Rocket->flags & PROJECTILEFLAG_FALLING)) && (!(g_GlobalTickCount & 7)))
				{
					sp564.f[0] = airborneWeapon->runtime_pos.f[0] + 400.0f;
					sp564.f[1] = airborneWeapon->runtime_pos.f[1] - 1800.0f;
					sp564.f[2] = airborneWeapon->runtime_pos.f[2];

					if (!(g_GlobalTickCount & 0xF))
					{
						sp564.f[2] += 400.0f;
					}
					else
					{
						sp564.f[2] -= 400.0f;
					}

					explosionCreate(NULL, &sp564, airborneWeapon->prop->stan, 0x14, 0, 0, airborneWeapon->prop->rooms, 0);
                    //Spawn smoke every 40 frames
					if ((((s32) g_GlobalTickCount) % 40) == 0)
					{
						explosionCreateSmoke(&sp564, airborneWeapon->prop->stan, 0xA, airborneWeapon->prop->rooms, 1);
					}
				}
			}
			else
			{
				projectileAlive = 1;

				if (Rocket->unk60 < 1.0f)
				{
					Rocket->unk60 += Rocket->unk64 * g_GlobalTimerDelta;

					if (g_ClockTimer > 0)
					{
						Rocket->unk64 *= 1.1f;
					}

					if ((Rocket->unk60 > 1.0f) || (Rocket->flags & PROJECTILEFLAG_00000008))
					{
						Rocket->unk60 = 1.0f;
					}

					quaternion_slerp((f32 *) (&Rocket->unk68), (f32 *) (&Rocket->unk78), Rocket->unk60, (f32 *) (&sp550));
					objectMatrix = &obj->mtx;
					quaternion_to_matrix((f32 *) (&sp550), (f32 *) (&obj->mtx));
					matrix_column_1_scalar_multiply(Rocket->unkC0, (f32 *) objectMatrix);
					matrix_column_2_scalar_multiply(Rocket->unkC4, (f32 *) objectMatrix);
					matrix_column_3_scalar_multiply_2(Rocket->unkC8, (f32 *) objectMatrix);
					projectileAlive = 0;
				}

                // Apply horizontal slide and friction to a projectile that has landed.
				if ((((Rocket->speed.f[0] != 0.0f) || (Rocket->speed.f[2] != 0.0f)) || (Rocket->unk60 < 1.0f)) && (!(Rocket->flags & PROJECTILEFLAG_00000008)))
				{
					objectMatrix = &obj->mtx;
					objectBBox = chrobjGetBboxFromObjectRecord(obj);
					projectileAlive = 0;

					for (sp548 = 0; sp548 < g_ClockTimer; sp548++)
					{
						RocketCurrent.f[0] += Rocket->speed.f[0];
						RocketCurrent.f[2] += Rocket->speed.f[2];
						if (Rocket->unk60 >= 1.0f)
						{
							if (Rocket->unk94 > 0.0f)
							{
								temp_f12_5 = (Rocket->unk94 * g_GlobalTimerDelta) / sqrtf((Rocket->speed.f[0] * Rocket->speed.f[0]) + (Rocket->speed.f[2] * Rocket->speed.f[2]));
								if (temp_f12_5 >= 1.0f)
								{
									Rocket->speed.f[0] = 0.0f;
									Rocket->speed.f[2] = 0.0f;
								}
								else
								{
									Rocket->speed.f[0] -= Rocket->speed.f[0] * temp_f12_5;
									Rocket->speed.f[2] -= Rocket->speed.f[2] * temp_f12_5;
								}
							}
							else
							{
								Rocket->speed.f[0] *= PROJECTILE_FRICTION_FACTOR;
								Rocket->speed.f[2] *= PROJECTILE_FRICTION_FACTOR;
							}
						}
					}

					sub_GAME_7F042EB4(obj, &RocketCurrent.f[0], &sp530, &sp53C, 0, 0);
					objMovedThisFrame = 1;
					temp_f20 = stanGetPositionYValue(prop->stan, prop->pos.f[0], prop->pos.f[2]);
					angleDelta = (temp_f20 - chrpropSumMatrixPosY(objectBBox, objectMatrix)) + 4.0f;
					prop->pos.f[1] = angleDelta;
					obj->runtime_pos.f[1] = angleDelta;
					if ((Rocket->speed.f[0] < 0.1f) && (Rocket->speed.f[0] > (-0.1f)))
					{
						if ((Rocket->speed.f[2] < 0.1f) && (Rocket->speed.f[2] > (-0.1f)))
						{
							Rocket->speed.f[2] = 0.0f;
							Rocket->speed.f[0] = 0.0f;
						}
					}
				}

				if ((projectileAlive != 0) || (Rocket->flags & PROJECTILEFLAG_00000008))
				{
					projectileFree(Rocket);
					obj->projectile = NULL;
					obj->runtime_bitflags &= ~RUNTIMEBITFLAG_HASPROJECTILE;
					if (prop->flags & PROPFLAG_00000008)
					{
						prop->flags |= PROPFLAG_00000010;
					}

#if defined(VERSION_JP) || defined(VERSION_EU)
					if (obj->type == PROPDEF_COLLECTABLE)
					{
						objectivestatusCheckDeposit(((struct WeaponObjRecord *) obj)->weaponnum, prop->stan->room);
					}
#endif
				}
			}
		}

		if (objMovedThisFrame != 0)
		{
			objectMatrix = (Mtxf *) (&obj->runtime_pos);

			chrobjCollisionRelated(obj);
			setupUpdateObjectRoomPosition(obj);
#if defined(VERSION_EU)
			sub_GAME_7F0402B4(obj->prop, &obj->nextcol);
#else
			sub_GAME_7F0402B4(obj->prop, (rgba_u8 *) sp100);
#endif
			detonate_proxmine_In_range((struct coord3d *) objectMatrix);
		}

		if (obj->type == PROPDEF_DOOR)
		{
			door = (struct DoorRecord *) prop->obj;
			previousOpenPosition = door->openPosition;
#if defined(VERSION_EU)
			if ((((((s32) door->openedTime) > 0) && (door->openstate == DOORSTATE_STATIONARY)) && (!(door->flags & PROPFLAG_DOOR_KEEPOPEN))) && (((s32) door->openedTime) < (((s32) g_GlobalTimer) - ((((s32) door->autoCloseFrames) * 50) / 60))))
#elif defined(VERSION_JP)
			if ((((((s32) door->openedTime) > 0) && (door->openstate == DOORSTATE_STATIONARY)) && (!(door->flags & PROPFLAG_DOOR_KEEPOPEN))) && (((s32) door->openedTime) < (((s32) g_GlobalTimer) - ((s32) door->autoCloseFrames))))
#else
			if ((((((s32) door->openedTime) > 0) && (((s32) door->openedTime) < (((s32) g_GlobalTimer) - ((s32) door->autoCloseFrames)))) && (door->openstate == DOORSTATE_STATIONARY)) && (!(door->flags & PROPFLAG_DOOR_KEEPOPEN)))
#endif
			{
				doorActivate(door, DOORSTATE_CLOSING);
			}

			if (door->openstate == DOORSTATE_WAITING)
			{
				pad1AC = door->linkedDoor;
				var_v1_5 = 1;
				while ((pad1AC != NULL) && (pad1AC != door))
				{
					if ((pad1AC->openstate != DOORSTATE_STATIONARY) || (pad1AC->openPosition > 0.0f))
					{
						var_v1_5 = 0;
					}

					pad1AC = pad1AC->linkedDoor;
				}


				if (var_v1_5 != 0)
				{
					doorSetOpenState(door, 1);
				}
			}

			if (((door->doorType == DOORTYPE_FALLAWAY) && (doorIsClosed(door) != 0)) && (doorIsPadlockFree(door) != 0))
			{
				doorActivateWrapper(prop);
			}

#ifndef TARGET_N64
			/* GE_TICKHZ: door7F054FB4 moves a door's whole linked ring, and at
			 * raised tick rates the vanilla gate let every door of the ring
			 * move it, so double doors opened too fast. Gate on the per-tick
			 * serial instead: exactly one pass per ring per tick at any rate,
			 * identical to vanilla at tickhz=60. */
			if (door->lastcalc60i != g_PortTickSerial)
#else
			if ((door->lastcalc60i < g_GlobalTimer) || (g_ClockTimer == 0))
#endif
			{
				door7F054FB4(door);
			}
		}
		else if ((obj->type == PROPDEF_CCTV) && (!(obj->flags & PROPFLAG_IS_DRONE_GUN)))
		{
			bottom_pad = (struct CCTVRecord *) prop->obj;

			if (bottom_pad->unkD4 != 0)
			{
				m_RocketGravity = bottom_pad->unkCC;
			}
			else
			{
				m_RocketGravity = bottom_pad->unkD0;
			}

			playerProp = getCurrentPlayerProp();
			xdiff = playerProp->pos.f[0] - obj->runtime_pos.f[0];
			temp_f0_13 = bottom_pad->unkE8;
			ydiff = playerProp->pos.f[1] - obj->runtime_pos.f[1];
			temp_f14_3 = playerProp->pos.f[2] - obj->runtime_pos.f[2];
			cctvSeesPlayer = 1;

			if ((temp_f0_13 > 0.0f) && ((temp_f0_13 * temp_f0_13) < (((xdiff * xdiff) + (ydiff * ydiff)) + (temp_f14_3 * temp_f14_3))))
			{
				cctvSeesPlayer = 0;
			}

			if (obj->flags & PROPFLAG_INMOTION)
			{
				cctvSeesPlayer = 0;
			}

			if (cctvSeesPlayer != 0)
			{
				temp_f0_14 = atan2f(xdiff, temp_f14_3);
				var_f2 = bottom_pad->unkC8;

				if (var_f2 < 0.0f)
				{
					var_f2 += M_TAU_F;
				}
				else if (var_f2 >= M_TAU_F)
				{
					var_f2 -= M_TAU_F;
				}

				var_f2 += bottom_pad->unkC4;

				if (var_f2 >= M_TAU_F)
				{
					var_f2 -= M_TAU_F;
				}

				angleDelta = temp_f0_14 - var_f2;

				if (temp_f0_14 < var_f2)
				{
					angleDelta += M_TAU_F;
				}

				angleDelta -= M_PI_F;

				if (angleDelta < 0.0f)
				{
					angleDelta += M_TAU_F;
				}

				if (angleDelta > M_PI_F)
				{
					angleDelta -= M_TAU_F;
				}

				if ((angleDelta > DegToRad(45)) || (angleDelta < DegToRad(-45)))
				{
					cctvSeesPlayer = 0;
				}
			}

			if (cctvSeesPlayer != 0)
			{
				sp4F0 = prop->stan;
				bondviewUpdateGuardTankFlagsRelated(playerProp, 0);
				if (stanTestLineUnobstructed(&sp4F0, prop->pos.f[0], prop->pos.f[2], playerProp->pos.f[0], playerProp->pos.f[2], 0x1B, 100.0f, 100.0f, 0.0f, 1.0f) != 0)
				{
					bottom_pad->timer += g_ClockTimer;
					if (bottom_pad->timer >= ((s32) (CCTV_ALARM_FRAMES * F_80030B14)))
					{
						alarmActivate();
						bottom_pad->timer = 0;
					}
				}

				bondviewUpdateGuardTankFlagsRelated(playerProp, 1);
			}

			if (bottom_pad->unkC8 < m_RocketGravity)
			{
				var_f2_6 = ((bottom_pad->unkD8 * bottom_pad->unkD8) * 0.5f) / CAM_ACCEL; if ((m_RocketGravity - var_f2_6) <= bottom_pad->unkC8)
				{
					bottom_pad->unkD8 = (f32) (bottom_pad->unkD8 - (CAM_ACCEL * g_GlobalTimerDelta));
					if (bottom_pad->unkD8 < CAM_ACCEL)
					{
						bottom_pad->unkD8 = CAM_ACCEL;
					}
				}
				else if (bottom_pad->unkD8 < bottom_pad->unkDC)
				{
					var_f2_6 = bottom_pad->unkD8 + (CAM_ACCEL * g_GlobalTimerDelta);
					if (bottom_pad->unkDC < var_f2_6)
					{
						var_f2_6 = bottom_pad->unkDC;
					}

					if (bottom_pad->unkC8 < (m_RocketGravity - (((var_f2_6 * var_f2_6) * 0.5f) / CAM_ACCEL)))
					{
						bottom_pad->unkD8 = var_f2_6;
					}
				}

				bottom_pad->unkC8 += bottom_pad->unkD8 * g_GlobalTimerDelta;
				if (m_RocketGravity <= bottom_pad->unkC8)
				{
					bottom_pad->unkC8 = m_RocketGravity;
					bottom_pad->unkD8 = 0.0f;
					bottom_pad->unkD4 = 0;
				}
			}
			else
			{
				var_f2_6 = ((bottom_pad->unkD8 * bottom_pad->unkD8) * 0.5f) / CAM_ACCEL; if (bottom_pad->unkC8 <= (m_RocketGravity + var_f2_6))
				{
					bottom_pad->unkD8 = (f32) (bottom_pad->unkD8 - (CAM_ACCEL * g_GlobalTimerDelta));
					if (bottom_pad->unkD8 < CAM_ACCEL)
					{
						bottom_pad->unkD8 = CAM_ACCEL;
					}
				}
				else if (bottom_pad->unkD8 < bottom_pad->unkDC)
				{
					var_f2_6 = bottom_pad->unkD8 + (CAM_ACCEL * g_GlobalTimerDelta);
					if (bottom_pad->unkDC < var_f2_6)
					{
						var_f2_6 = bottom_pad->unkDC;
					}

					if (bottom_pad->unkC8 > (m_RocketGravity + (((var_f2_6 * var_f2_6) * 0.5f) / CAM_ACCEL)))
					{
						bottom_pad->unkD8 = var_f2_6;
					}
				}

				bottom_pad->unkC8 -= bottom_pad->unkD8 * g_GlobalTimerDelta;
				if (bottom_pad->unkC8 <= m_RocketGravity)
				{
					bottom_pad->unkC8 = m_RocketGravity;
					bottom_pad->unkD8 = 0.0f;
					bottom_pad->unkD4 = 1;
				}
			}
		}
		else if ((obj->type == PROPDEF_AUTOGUN) && (!(obj->flags & PROPFLAG_IS_DRONE_GUN)))
		{
			poAGun = (struct AutogunRecord *) prop->obj;
			playerProp2 = getCurrentPlayerProp();
			AutogunSeesPlayer = 0;
			isTracking = 0;
			hasLineOfSight = 0;
			if (obj->flags2 & PROPFLAG_IS_DOUBLE)
			{
				if (obj->flags2 & PROPFLAG2_40000000)
				{
					poAGun->unk98 = poAGun->unk9C;
					poAGun->rot_related = poAGun->unk90;
				}
				else if ((poAGun->unk90 == poAGun->rot_related) && (poAGun->unk9C == poAGun->unk98))
				{
					poAGun->unk98 = (((U32_TO_F32(randomGetNext()) * 39.0f) + 1.0f) * M_TAU_F) / 360.0f; //degtorad 
					poAGun->rot_related = U32_TO_F32(randomGetNext()) * M_TAU_F;
				}

				chrobjCallsApplySpeed(&poAGun->unk90, poAGun->rot_related, &poAGun->unk94, AUTOGUN_YAW_ACCEL_PER_FRAME, AUTOGUN_YAW_ACCEL_PER_FRAME, AUTOGUN_YAW_MAX_SPEED);
				chrobjCallsApplySpeed(&poAGun->unk9C, poAGun->unk98, &poAGun->unkA0, AUTOGUN_PITCH_ACCEL_PER_FRAME, AUTOGUN_PITCH_ACCEL_PER_FRAME, AUTOGUN_PITCH_MAX_SPEED);
			}
			else
			{
				var_f0_2 = playerProp2->pos.f[0] - obj->runtime_pos.f[0];
				playerDirVec.f[1] = (playerProp2->pos.f[1] - obj->runtime_pos.f[1]) - 20.0f;//Aim 20 units below player’s head
				temp_f2_23 = playerProp2->pos.f[2] - obj->runtime_pos.f[2];
				horizontalDistSq = (var_f0_2 * var_f0_2) + (temp_f2_23 * temp_f2_23);
				playerDirVec.f[2] = var_f0_2;
				playerDirVec.f[0] = temp_f2_23;
				horizontalDist = sqrtf(horizontalDistSq);
				distanceToPlayer = horizontalDist;
				if (obj->flags & PROPFLAG_DOOR_TWOWAY)
				{
					horizontalDistSq += playerDirVec.f[1] * playerDirVec.f[1];
					distanceToPlayer = sqrtf(horizontalDistSq);
				}

				sp4A0 = chrlvGetAimLimitAngle(horizontalDistSq);
				sp4D8 = poAGun->rot_related;
				targetPitch = poAGun->unk98;
				if (distanceToPlayer <= poAGun->aimdist)
				{
					if (sp4A0);
					playerYaw = atan2f(playerDirVec.f[2], playerDirVec.f[0]);
					playerPitch = atan2f(playerDirVec.f[1], horizontalDist);
					if ((obj->flags & PROPFLAG_NO_AMMO) || (obj->flags & PROPFLAG_INMOTION))
					{
						AutogunSeesPlayer = 1;
					}
					else
					{
						yawError = playerYaw - poAGun->unk90;
						if (yawError < 0.0f)
						{
							yawError += M_TAU_F;
						}

						if (yawError > M_PI_F)
						{
							yawError -= M_TAU_F;
						}

						var_f2_6 = playerPitch - poAGun->unk9C;
						if (var_f2_6 < 0.0f)
						{
							if (horizontalDist)
							{
								horizontalDist = (horizontalDist) ? (horizontalDist) : (horizontalDist);
							}
						}

						if ((yawError < DegToRad(70)) && (yawError > DegToRad(-70)))
						{
							AutogunSeesPlayer = 1;
						}
					}

					if (AutogunSeesPlayer != 0)
					{
						sp494 = playerYaw - poAGun->rot_related;
						collisionTile = prop->stan;
						if (sp494 < (-M_PI_F))
						{
							sp494 += M_TAU_F;
						}
						else if (sp494 >= M_PI_F)
						{
							sp494 -= M_TAU_F;
						}

						bondviewUpdateGuardTankFlagsRelated(playerProp2, 0);
						if ((((sp494 <= poAGun->unk88) && (poAGun->unk8C <= sp494)) && (stanTestLineUnobstructed(&collisionTile, prop->pos.f[0], prop->pos.f[2], playerProp2->pos.f[0], playerProp2->pos.f[2], 0x1B, prop->pos.f[1], prop->pos.f[1], playerProp2->pos.f[1], playerProp2->pos.f[1]) != 0)) && ((collisionTile) == playerProp2->stan))
						{
							obj->flags |= PROPFLAG_INMOTION;
							hasLineOfSight = 1;
							sp4D8 = playerYaw;
							targetPitch = playerPitch;
						}
						else if ((poAGun->unkB8 >= 0) && ((g_GlobalTimer - AUTOGUN_TRACKING_FRAMES) < poAGun->unkB8)) //cooldown 2 seconds
						{
							sp4D8 = poAGun->unk90;
							targetPitch = poAGun->unk9C;
						}
						else
						{
							AutogunSeesPlayer = 0;
						}

						bondviewUpdateGuardTankFlagsRelated(playerProp2, 1);
					}
				}

				if (AutogunSeesPlayer != 0)
				{
					sp4A0 = chrlvGetAimLimitAngle(horizontalDistSq);
				}

				if (poAGun->is_active != 0)
				{
                    //Sway once every 2 seconds while firing
					sp4D8 += (sp4A0 * 0.8f) * sinf((((f32) (((s32) g_GlobalTimer) % AUTOGUN_TRACKING_FRAMES)) * M_TAU_F) / (f32) AUTOGUN_TRACKING_FRAMES);
					if (sp4D8 < 0.0f)
					{
						sp4D8 += M_TAU_F;
					}

					if (sp4D8 >= M_TAU_F)
					{
						sp4D8 -= M_TAU_F;
					}
				}

				var_f0_2 = sp4D8 - poAGun->rot_related;
				if (var_f0_2 < (-M_PI_F))
				{
					var_f0_2 += M_TAU_F;
				}
				else if (var_f0_2 >= M_PI_F)
				{
					var_f0_2 -= M_TAU_F;
				}

				if (poAGun->unk88 < var_f0_2)
				{
					sp4D8 = poAGun->rot_related + poAGun->unk88;
				}
				else if (var_f0_2 < poAGun->unk8C)
				{
					sp4D8 = poAGun->rot_related + poAGun->unk8C;
				}

				if (sp4D8 < 0.0f)
				{
					sp4D8 += M_TAU_F;
				}

				if (sp4D8 >= M_TAU_F)
				{
					sp4D8 -= M_TAU_F;
				}

				chrobjCallsApplySpeed(&poAGun->unk90, sp4D8, &poAGun->unk94, AUTOGUN_ALERT_ACCEL_PER_FRAME  , AUTOGUN_ALERT_ACCEL_PER_FRAME  , poAGun->speed);
				chrobjCallsApplySpeed(&poAGun->unk9C, targetPitch, &poAGun->unkA0, AUTOGUN_ALERT_ACCEL_PER_FRAME  , AUTOGUN_ALERT_ACCEL_PER_FRAME  , poAGun->speed);
				temp_f12_5 = sp4D8 - poAGun->unk90;
				if (temp_f12_5 < 0.0f)
				{
					temp_f12_5 += M_TAU_F;
				}

				if (temp_f12_5 > M_PI_F)
				{
					temp_f12_5 -= M_TAU_F;
				}

				var_f2_6 = targetPitch - poAGun->unk9C; yawError = targetPitch;
				if (var_f2_6 < 0.0f)
				{
					var_f2_6 += M_TAU_F;
				}

				if (var_f2_6 > M_PI_F)
				{
					var_f2_6 -= M_TAU_F;
				}

				poAGun->is_active = 0;
				if (AutogunSeesPlayer != 0)
				{
					if ((((temp_f12_5 < sp4A0) && ((-sp4A0) < temp_f12_5)) && (var_f2_6 < sp4A0)) && ((-sp4A0) < var_f2_6))
					{
						poAGun->is_active = 1;
						isTracking = 1;
						if (hasLineOfSight != 0)
						{
							poAGun->unkB8 = (s32) g_GlobalTimer;
							poAGun->unkBC = (s32) g_GlobalTimer;
						}
					}
					else
					{
						angleDelta_6 = 2.0f * sp4A0;
						if ((((temp_f12_5 < angleDelta_6) && ((-angleDelta_6) < temp_f12_5)) && (var_f2_6 < angleDelta_6)) && ((-angleDelta_6) < var_f2_6))
						{
							poAGun->is_active = 1;
							isTracking = 1;
							if (hasLineOfSight != 0)
							{
								poAGun->unkB8 = (s32) g_GlobalTimer;
							}
						}
						else if ((poAGun->unkB8 >= 0) && ((g_GlobalTimer - AUTOGUN_TRACKING_FRAMES) < poAGun->unkB8))
						{
							poAGun->is_active = 1;
							isTracking = 1;
						}
					}
				}

#ifndef TARGET_N64
				{ /* GE_SNDDBG 1Hz autogun status */
					static s32 snddbg = -1;
					if (snddbg < 0) { extern char *getenv(const char *); snddbg = getenv("GE_SNDDBG") != NULL; }
					if (snddbg && (g_GlobalTimer % 60) == 0 && g_ClockTimer > 0)
					{
						extern int printf(const char *, ...);
						printf("ge: agstat gt=%d pos=(%.0f %.0f %.0f) dist=%.0f aimdist=%.0f sees=%d los=%d track=%d active=%d yaw=%.2f pitch=%.2f flags=%x flags2=%x\n",
						       g_GlobalTimer, obj->runtime_pos.f[0], obj->runtime_pos.f[1], obj->runtime_pos.f[2], distanceToPlayer, poAGun->aimdist,
						       AutogunSeesPlayer, hasLineOfSight, isTracking, poAGun->is_active, poAGun->unk90, poAGun->unk9C, (u32) obj->flags, (u32) obj->flags2);
					}
				}
#endif
				if (isTracking != 0) //firing
				{
					poAGun->unkB0 += AUTOGUN_SPIN_ACCEL_PER_FRAME * g_GlobalTimerDelta;
					if (poAGun->unkB0 > AUTOGUN_SPIN_MAX_SPEED)
					{
						poAGun->unkB0 = AUTOGUN_SPIN_MAX_SPEED;
					}
				}
				else if (poAGun->unkB0 > 0.0f)
				{
#ifndef TARGET_N64
					if (portSmoothOn())
					{
						portDecayStep(&poAGun->unkB0, 0.99f, g_GlobalTimerDelta);
					}
					else
#endif
					for (var_v0_3 = 0; var_v0_3 < g_ClockTimer; var_v0_3++)
					{
						poAGun->unkB0 *= 0.99f; //barrel loses 45% of its spin per second when idle
					}


					if (poAGun->unkB0 <= 0.0001f)
					{
						poAGun->unkB0 = 0.0f;
					}
				}

				if (poAGun->unkB0 > 0.0f)
				{
					poAGun->unkB4 += poAGun->unkB0 * g_GlobalTimerDelta;
					while (poAGun->unkB4 >= M_TAU_F)
					{
						poAGun->unkB4 -= M_TAU_F;
					}

				}
			}
		}
		else if (obj->type == PROPDEF_VEHICHLE)
		{
			poTruck = (struct VehichleRecord *) obj;
			targetYaw = poTruck->roty;
			waypointPosition = NULL;

			ai((PropDefHeaderRecord *) poTruck, 1);

			if (poTruck->speedtime60 >= 0.0f)
			{
				if (poTruck->speedtime60 <= g_GlobalTimerDelta)
				{
					poTruck->speed = poTruck->speedaim;
				}
				else
				{
					poTruck->speed += ((poTruck->speedaim - poTruck->speed) * g_GlobalTimerDelta) / poTruck->speedtime60;
				}

				poTruck->speedtime60 -= g_GlobalTimerDelta;
			}

			truckShouldPlayEngineSound = 0;

			if (((!(obj->flags2 & PROPFLAG2_00080000)) && (objIsHealthy(obj) != 0)) && ((poTruck->speed > 0.0f) || (poTruck->speedaim > 0.0f)))
			{
				truckShouldPlayEngineSound = sub_GAME_7F053894(&poTruck->runtime_pos, 2000.0f, 3000.0f);
			}

			if (truckShouldPlayEngineSound > 0)
			{
				if (((poTruck->Sound == NULL) || (sndGetPlayingState(poTruck->Sound) == 0)) && (lvlGetControlsLockedFlag() == 0))
				{
					sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, TRUCK_RUN_SFX, &poTruck->Sound);
				}

				if (poTruck->Sound != NULL)
				{
					sndCreatePostEvent(poTruck->Sound, 8, truckShouldPlayEngineSound);
#ifndef TARGET_N64
					portSndApplyPan(poTruck->Sound, &poTruck->runtime_pos);
#endif
				}
			}
			else if ((poTruck->Sound != NULL) && (sndGetPlayingState(poTruck->Sound) != 0))
			{
				sndDeactivate(poTruck->Sound);
			}

			if (poTruck->path != NULL)
			{
				temp_a1_6 = &poTruck->path->waypoints[poTruck->nextstep];
				currentWaypoint = &g_CurrentSetup.pathwaypoints[*temp_a1_6];
				waypointPosition = &g_CurrentSetup.pads[currentWaypoint->padID].pos;
				targetYaw = atan2f(waypointPosition->f[0] - poTruck->runtime_pos.f[0], waypointPosition->f[2] - poTruck->runtime_pos.f[2]);
				if (poTruck->flags & PROPFLAG_INMOTION)
				{
					poTruck->roty = targetYaw;
					obj->flags &= ~PROPFLAG_INMOTION;
					sub_GAME_7F044B38(poTruck);
				}
			}
			else if (poTruck->flags & PROPFLAG_INMOTION) 
			{
				poTruck->roty = atan2f(poTruck->mtx.m[2][0], poTruck->mtx.m[2][2]);
				poTruck->flags &= ~PROPFLAG_INMOTION;
				sub_GAME_7F044B38(poTruck);
			}

			if (poTruck->speed > 0.0f)
			{
				truckAngularVelocity = 0.0f;
				currentTile = prop->stan;
				previousYaw = poTruck->roty;
				sp434 = poTruck->turnrot60;
				if (waypointPosition != NULL)
				{
					truckAngularVelocity = 0.0f;
					forwardDir.f[0] = sinf(poTruck->roty);
					forwardDir.f[1] = 0.0f;
					forwardDir.f[2] = cosf(poTruck->roty);
					if (chrlvGeometryRelated7F02FC34(&poTruck->runtime_pos, &forwardDir, waypointPosition, 10.0f) != 0)
					{
						targetYaw = poTruck->roty;
					}
				}

				chrobjCallsApplySpeed(&poTruck->roty, targetYaw, &poTruck->turnrot60, TRUCK_TURN_ACCEL_PER_FRAME, TRUCK_TURN_DECEL_PER_FRAME, TRUCK_TURN_MAX_SPEED);
				while (poTruck->roty >= M_TAU_F)
				{
					poTruck->roty -= M_TAU_F;
				}


				while (poTruck->roty < 0.0f)
				{
					poTruck->roty += M_TAU_F;
				}


				if (targetYaw == poTruck->roty)
				{
					if ((poTruck->turnrot60 <= TRUCK_TURN_DECEL_PER_FRAME) && (poTruck->turnrot60 >= (-TRUCK_TURN_DECEL_PER_FRAME)))
					{
						poTruck->turnrot60 = 0.0f;
					}
				}

				temp_s0_5 = (f32 *) model->obj->Switches[3]->Data;
				if (g_GlobalTimerDelta > 0.0f)
				{
					truckAngularVelocity = (poTruck->roty - previousYaw) / g_GlobalTimerDelta;
				}

				if (truckAngularVelocity < 0.0f)
				{
					truckAngularVelocity += M_TAU_F;
				}

				sp460 = ((temp_s0_5[2] * model->scale) * sinf(truckAngularVelocity)) * g_GlobalTimerDelta;
				forwardDir.f[0] = sinf(poTruck->roty);
				forwardDir.f[1] = 0.0f;
				forwardDir.f[2] = cosf(poTruck->roty);
				RocketCurrent.f[0] = (poTruck->runtime_pos.f[0] + ((poTruck->speed * g_GlobalTimerDelta) * forwardDir.f[0])) - (forwardDir.f[2] * sp460);
				RocketCurrent.f[1] = poTruck->runtime_pos.f[1];
				RocketCurrent.f[2] = (poTruck->runtime_pos.f[2] + ((poTruck->speed * g_GlobalTimerDelta) * forwardDir.f[2])) + (forwardDir.f[0] * sp460);
#ifndef TARGET_N64
				portVehicleCarryStep(poTruck, ((f64) poTruck->speed * g_GlobalTimerDelta * forwardDir.f[0]) - ((f64) forwardDir.f[2] * sp460),
				                     ((f64) poTruck->speed * g_GlobalTimerDelta * forwardDir.f[2]) + ((f64) forwardDir.f[0] * sp460), &RocketCurrent);
#endif
				if ((stanTestLineUnobstructed(&currentTile, prop->pos.f[0], prop->pos.f[2], RocketCurrent.f[0], RocketCurrent.f[2], 0x1F, 0.0f, 1.0f, 0.0f, 1.0f) != 0) && (stanTestVolume(&currentTile, RocketCurrent.f[0], RocketCurrent.f[2], 10.0f, 0x1F, 0.0f, 1.0f) < 0))
				{
					nextTile = prop->stan;
					sp450.f[0] = prop->pos.f[0];
					sp450.f[1] = prop->pos.f[1];
					sp450.f[2] = prop->pos.f[2];
					prop->stan = currentTile;
					poTruck->runtime_pos.f[0] = (prop->pos.f[0] = RocketCurrent.f[0]);
					poTruck->runtime_pos.f[2] = (prop->pos.f[2] = RocketCurrent.f[2]);
					chrobjCollisionRelated(obj);
					setupUpdateObjectRoomPosition(obj);
					var_s2_5 = sub_GAME_7F0448A8(prop);
#ifndef TARGET_N64
					if (var_s2_5 == 0)
					{
						var_s2_5 = portVehiclePlayerBrush(prop, &sp450, poTruck->roty, previousYaw);
					}
#endif
					if (var_s2_5 != 0)
					{
						temp_v0_25 = (f32 *) model->obj->Switches[10]->Data;
						vec424.f[0] = temp_v0_25[1] * poTruck->mtx.m[0][0];
						vec424.f[2] = temp_v0_25[1] * poTruck->mtx.m[0][2];
						vec418.f[0] = temp_v0_25[2] * poTruck->mtx.m[0][0];
						vec418.f[2] = temp_v0_25[2] * poTruck->mtx.m[0][2];
						vec40C.f[0] = temp_v0_25[5] * poTruck->mtx.m[2][0];
						vec40C.f[2] = temp_v0_25[5] * poTruck->mtx.m[2][2];
						vec400.f[0] = temp_v0_25[6] * poTruck->mtx.m[2][0];
						vec400.f[2] = temp_v0_25[6] * poTruck->mtx.m[2][2];
						currentTile = prop->stan;
						if (((((walkTilesBetweenPoints_NoCallback(&currentTile, prop->pos.f[0], prop->pos.f[2], (prop->pos.f[0] + vec424.f[0]) + vec40C.f[0], (prop->pos.f[2] + vec424.f[2]) + vec40C.f[2]) == 0) || (walkTilesBetweenPoints_NoCallback(&currentTile, (prop->pos.f[0] + vec424.f[0]) + vec40C.f[0], (prop->pos.f[2] + vec424.f[2]) + vec40C.f[2], (prop->pos.f[0] + vec418.f[0]) + vec40C.f[0], (prop->pos.f[2] + vec418.f[2]) + vec40C.f[2]) == 0)) || (walkTilesBetweenPoints_NoCallback(&currentTile, (prop->pos.f[0] + vec418.f[0]) + vec40C.f[0], (prop->pos.f[2] + vec418.f[2]) + vec40C.f[2], (prop->pos.f[0] + vec418.f[0]) + vec400.f[0], (prop->pos.f[2] + vec418.f[2]) + vec400.f[2]) == 0)) || (walkTilesBetweenPoints_NoCallback(&currentTile, (prop->pos.f[0] + vec418.f[0]) + vec400.f[0], (prop->pos.f[2] + vec418.f[2]) + vec400.f[2], (prop->pos.f[0] + vec424.f[0]) + vec400.f[0], (prop->pos.f[2] + vec424.f[2]) + vec400.f[2]) == 0)) || (walkTilesBetweenPoints_NoCallback(&currentTile, (prop->pos.f[0] + vec424.f[0]) + vec400.f[0], (prop->pos.f[2] + vec424.f[2]) + vec400.f[2], (prop->pos.f[0] + vec424.f[0]) + vec40C.f[0], (prop->pos.f[2] + vec424.f[2]) + vec40C.f[2]) == 0))
						{
							var_s2_5 = 0;
						}
					}

					if (var_s2_5 != 0)
					{
#ifndef TARGET_N64
						portVehicleCarryCommit(poTruck);
#endif
						sub_GAME_7F044B38(poTruck);
						sub_GAME_7F0402B4(prop, &poTruck->nextcol);
						detonate_proxmine_In_range(&poTruck->runtime_pos);
						if ((waypointPosition != NULL) && (chrlvIsArrivingLaterallyAtPos(&sp450, &RocketCurrent, waypointPosition, 100.0f) != 0))
						{
							poTruck->nextstep++;
							if (poTruck->path->waypoints[poTruck->nextstep] < 0)
							{
								poTruck->path = NULL;
								poTruck->speedaim = 0.0f;
								poTruck->speedtime60 = 60.0f;
							}
						}
					}
					else
					{
#ifndef TARGET_N64
						if (portVehicleSpeedLossDue(poTruck))
#endif
						{
							if (poTruck->speedtime60 < 0.0f)
							{
								poTruck->speedaim = (f32) poTruck->speed;
								poTruck->speedtime60 = 60.0f;
							}

							poTruck->speed = 0.0f;
						}
						poTruck->roty = previousYaw;
						poTruck->turnrot60 = sp434;
						prop->stan = nextTile;
						obj->runtime_pos.f[0] = (prop->pos.f[0] = sp450.f[0]);
						obj->runtime_pos.f[1] = (prop->pos.f[1] = sp450.f[1]);
						obj->runtime_pos.f[2] = (prop->pos.f[2] = sp450.f[2]);
						chrobjCollisionRelated(obj);
						setupUpdateObjectRoomPosition(obj);
					}
				}
				else
				{
#ifndef TARGET_N64
					if (portVehicleSpeedLossDue(poTruck))
#endif
					{
						if (poTruck->speedtime60 < 0.0f)
						{
							poTruck->speedaim = (f32) poTruck->speed;
							poTruck->speedtime60 = 60.0f;
						}

						poTruck->speed = 0.0f;
					}
					poTruck->roty = previousYaw;
					poTruck->turnrot60 = sp434;
				}
			}
			else if (poTruck->flags & PROPFLAG_INMOTION)
			{
				poTruck->roty = atan2f(poTruck->mtx.m[2][0], poTruck->mtx.m[2][2]);
				poTruck->flags &= ~PROPFLAG_INMOTION;
				sub_GAME_7F044B38(poTruck);
			}
		}
		else if (obj->type == PROPDEF_AIRCRAFT)
		{
			render_pad2F4 = (struct AircraftRecord *) obj;

			ai((PropDefHeaderRecord *) render_pad2F4, 1);

			temp_s0_6 = render_pad2F4->model;

			if (temp_s0_6->anim != NULL)
			{
				setsuboffset(temp_s0_6, &render_pad2F4->runtime_pos);
#if defined(VERSION_EU)
				modelSetAnimPlaySpeed(render_pad2F4->model, 1.2f, 0.0f);
#endif
				temp_s0_6 = render_pad2F4->model;

				if (temp_s0_6->anim == animation_table_ptrs2[1])
				{
					modelSetAnimTranslationScale(temp_s0_6, 10.438f);
					setsubroty(render_pad2F4->model, M_PI_F);
				}
				else if (bossGetStageNum() == 22)
				{
					modelSetAnimTranslationScale(render_pad2F4->model, 1.0438f);
					setsubroty(render_pad2F4->model, 2.3561945f);
				}
				else if (bossGetStageNum() == 26)
				{
					modelSetAnimTranslationScale(render_pad2F4->model, 1.0438f);
					setsubroty(render_pad2F4->model, 3.9269907f);
				}
				else
				{
					modelSetAnimTranslationScale(render_pad2F4->model, 1.0438f);
					setsubroty(render_pad2F4->model, 0.0f);
				}

#ifndef TARGET_N64
				modelTickAnimF(render_pad2F4->model, portAnimTicks(), 1);   /* fractional, see frametiming.c */
#else
				modelTickAnim(render_pad2F4->model, g_ClockTimer, 1);
#endif
				subcalcpos(render_pad2F4->model);
				getsuboffset(render_pad2F4->model, &render_pad2F4->runtime_pos);
				prop->pos.f[0] = render_pad2F4->runtime_pos.f[0];
				prop->pos.f[2] = render_pad2F4->runtime_pos.f[2];
				if (render_pad2F4->pad < 10000)
				{
					var_v1_4 = &g_CurrentSetup.pads[render_pad2F4->pad];
				}
				else
				{
					var_v1_4 = (PadRecord *) (&g_CurrentSetup.boundpads[render_pad2F4->pad - 10000]);
				}

				prop->pos.f[1] = var_v1_4->pos.f[1] + render_pad2F4->runtime_pos.f[1];
				render_pad2F4->runtime_pos.f[1] = prop->pos.f[1];
				setsuboffset(render_pad2F4->model, &render_pad2F4->runtime_pos);
			}

			angleDelta = render_pad2F4->speedtime60;
			if (angleDelta >= 0.0f)
			{
				if (angleDelta <= g_GlobalTimerDelta)
				{
					render_pad2F4->speed = (f32) render_pad2F4->speedaim;
					angleDelta = *((volatile f32 *) (&render_pad2F4->speedtime60));
				}
				else
				{
					render_pad2F4->speed += ((render_pad2F4->speedaim - render_pad2F4->speed) * g_GlobalTimerDelta) / angleDelta;
				}

				render_pad2F4->speedtime60 = (f32) (angleDelta - g_GlobalTimerDelta);
			}

			if (render_pad2F4->rotaryspeedtime >= 0.0f)
			{
				if (render_pad2F4->rotaryspeedtime <= g_GlobalTimerDelta)
				{
					render_pad2F4->rotaryspeed = (f32) render_pad2F4->rotaryspeedaim;
				}
				else
				{
					angleDelta = render_pad2F4->rotaryspeed;
					render_pad2F4->rotaryspeed += ((render_pad2F4->rotaryspeedaim - render_pad2F4->rotaryspeed) * g_GlobalTimerDelta) / render_pad2F4->rotaryspeedtime;
				}

				render_pad2F4->rotaryspeedtime -= g_GlobalTimerDelta;
			}

			truckShouldPlayEngineSound = 0;
			if ((((!(render_pad2F4->flags2 & PROPFLAG2_00080000)) && (objIsHealthy(obj) != 0)) && (render_pad2F4->rotaryspeed != 0.0f)) && (!(render_pad2F4->flags & PROPFLAG_INMOTION)))
			{
				truckShouldPlayEngineSound = sub_GAME_7F053894(&render_pad2F4->runtime_pos, 5000.0f, 6000.0f);
			}

			if (truckShouldPlayEngineSound > 0)
			{
				if (((render_pad2F4->Sound == NULL) || (sndGetPlayingState(render_pad2F4->Sound) == 0)) && (lvlGetControlsLockedFlag() == 0))
				{
					sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, HELI_RUN_SFX, &render_pad2F4->Sound);
				}

				if (render_pad2F4->Sound != NULL)
				{
					sndCreatePostEvent(render_pad2F4->Sound, 8, truckShouldPlayEngineSound);
#ifndef TARGET_N64
					portSndApplyPan(render_pad2F4->Sound, &render_pad2F4->runtime_pos);
#endif
				}
			}
			else if ((render_pad2F4->Sound != NULL) && (sndGetPlayingState(render_pad2F4->Sound) != 0))
			{
				sndDeactivate(render_pad2F4->Sound);
			}
		}
	}

	if (obj->type == PROPDEF_TINTED_GLASS)
	{
		pad268 = (struct TintedGlassRecord *) prop->obj;
		pad268->calculatedopacity = glassCalculateOpacity(&obj->runtime_pos, pad268->TintDist, pad268->CullDist, pad268->unk90);
		if ((pad268->portalnum >= 0) && (playerCount == 1))
		{
			if (pad268->calculatedopacity == 0xFF)
			{
				bgToggleDataPortalsContrlBytes1Bit1(pad268->portalnum, 0);
			}
			else
			{
				bgToggleDataPortalsContrlBytes1Bit1(pad268->portalnum, 1);
			}
		}

		applyFogCull = FALSE;
	}
	else if ((obj->type == PROPDEF_DOOR) && (((struct DoorRecord *) obj)->doorFlags & DOORFLAG_WINDOWED))
	{
		pad36C = (struct DoorRecord *) prop->obj;
		var_s2_6 = 1;
#ifdef TARGET_N64
		pad36C->calculatedopacity = glassCalculateOpacity(&obj->runtime_pos, pad36C->TintDist, *((s32 *) (((u8 *) pad36C) + 0xC4)), 0.0f);
#else
		/* PORT: the 0xC4 raw word in N64 byte order (see doorGlassOpaqueDistWord) */
		pad36C->calculatedopacity = glassCalculateOpacity(&obj->runtime_pos, pad36C->TintDist, doorGlassOpaqueDistWord(pad36C), 0.0f);
#endif
		if (playerCount == 1)
		{
			if ((pad36C->calculatedopacity != 0xFF) || (pad36C->openPosition > 0.0f))
			{
				var_s2_6 = 0;
			}

			temp_v0_29 = model->obj;
			if ((temp_v0_29->Skeleton == (&skeleton_door)) && (modelGetNodeRwData(model, temp_v0_29->Switches[1])->Switch.visible == 0))
			{
				var_s2_6 = 0;
			}

			if (var_s2_6 != 0)
			{
				doorDeactivatePortal(pad36C);
			}
			else
			{
				doorActivatePortal(pad36C);
			}
		}
	}

	if ((obj->type == PROPDEF_TANK) && (get_ptr_for_players_tank() == prop))
	{
		var_v1_5 = 1;
	}
	else if (obj->flags2 & PROPFLAG2_04000000)
	{
		var_v1_5 = 1;
	}
	else
	{
		var_v1_5 = ((!(obj->runtime_bitflags & RUNTIMEBITFLAG_00000800)) && (!(obj->flags2 & PROPFLAG2_00080000))) ? (posIsOnScreen(prop, &obj->runtime_pos, getinstsize(model), applyFogCull)) : (0);
	}

	if (var_v1_5 != 0)
	{
		if (isSimOwner)
		{
			update_color_shading(&obj->shadecol, &obj->nextcol);
		}

		prop->flags |= PROPFLAG_ONSCREEN;
		mtxs = dynAllocate(model->obj->numMatrices << 6);
		model->render_pos = (RenderPosView *) mtxs;

		if (obj->type == PROPDEF_DOOR)
		{
			sp39C = (struct DoorRecord *) prop->obj;
			door7F0526EC(sp39C, mtxs);
			matrix_4x4_multiply_homogeneous_in_place(camGetWorldToScreenMtxf(), mtxs);

			if (model->obj->Skeleton == (&skeleton_eyelid_door))
			{
				sp394 = M_TAU_F - ((sp39C->openPosition * M_TAU_F) / 360.0f);
				render_pad388 = &mtxs[1];
				temp_s0_10 = (Mtxf *) model->obj->Switches[1]->Data;
				matrix_4x4_set_rotation_around_x(sp394, render_pad388);
				matrix_4x4_set_position((struct coord3d *) temp_s0_10, render_pad388);
				matrix_4x4_multiply_in_place(mtxs, render_pad388);
				render_pad388 = &mtxs[2];
				temp_s0_10 = (Mtxf *) model->obj->Switches[2]->Data;
				matrix_4x4_set_rotation_around_x(M_TAU_F - sp394, render_pad388);
				matrix_4x4_set_position((struct coord3d *) temp_s0_10, render_pad388);
				matrix_4x4_multiply_in_place(mtxs, render_pad388);
			}
			else if (model->obj->Skeleton == (&skeleton_iris_door))
			{
				sp380 = 0.0f;
				sp384 = (sp39C->openPosition * M_TAU_F) / 360.0f;
				temp_f0_31 = sp39C->maxFrac * 0.3f;
				if (temp_f0_31 < sp39C->openPosition)
				{
					sp380 = (((sp39C->maxFrac * (sp39C->openPosition - temp_f0_31)) / (sp39C->maxFrac - temp_f0_31)) * M_TAU_F) / 360.0f;
					if (isSimOwner)
					{
						if (previousOpenPosition <= temp_f0_31)
						{
							chrobjSndCreatePostEventDefault(sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, METAL_SLIDE_OPEN_SFX, NULL), &prop->pos);
						}
					}
				}
				else if (isSimOwner)
				{
					if (temp_f0_31 < previousOpenPosition)
					{
						chrobjSndCreatePostEventDefault(sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, METAL_SLIDE_CLOSE_SFX, NULL), &prop->pos);
					}
				}

				sp38C = 0;
				do
				{
					temp_v0_32 = sp38C << 1;
					var_v1_5 = temp_v0_32 + 2;
					sp390 = (Mtxf *) model->obj->Switches[temp_v0_32 + 1]->Data;
					matrix_4x4_set_rotation_around_z(sp380, (Mtxf *) ((((u8 *) mtxs) + (temp_v0_32 * (sizeof(Mtxf)))) + (sizeof(Mtxf))));
					matrix_4x4_set_position((struct coord3d *) sp390, (Mtxf *) ((((u8 *) mtxs) + (temp_v0_32 * (sizeof(Mtxf)))) + (sizeof(Mtxf))));
					matrix_4x4_multiply_in_place(mtxs, (Mtxf *) ((((u8 *) mtxs) + (temp_v0_32 * (sizeof(Mtxf)))) + (sizeof(Mtxf))));
					sp390 = (Mtxf *) model->obj->Switches[var_v1_5]->Data;
					matrix_4x4_set_rotation_around_z(sp384, (Mtxf *) (((u8 *) mtxs) + (var_v1_5 << 6)));
					matrix_4x4_set_position((struct coord3d *) sp390, &mtxs[var_v1_5]);
					matrix_4x4_multiply_in_place((Mtxf *) ((((u8 *) mtxs) + (temp_v0_32 * (sizeof(Mtxf)))) + (sizeof(Mtxf))), &mtxs[var_v1_5]);
					sp38C++;
				}

				while (sp38C != 6);
			}
		}
		else
		{
			matrix_4x4_copy(&obj->mtx, &tempMatrix2);
			matrix_4x4_set_position(&obj->runtime_pos, &tempMatrix2);
			matrix_4x4_multiply_homogeneous(camGetWorldToScreenMtxf(), &tempMatrix2, mtxs);

			if (obj->type == PROPDEF_CCTV)
			{
				sp370 = (struct CCTVRecord *) prop->obj;
				angleDelta_7 = sp370->unkC8;
				temp_s0_13 = (struct coord3d *) model->obj->Switches[0]->Data;

				if (angleDelta_7 < 0.0f)
				{
					angleDelta_7 += M_TAU_F;
				}
				else if (angleDelta_7 >= M_TAU_F)
				{
					angleDelta_7 -= M_TAU_F;
				}

				matrix_4x4_set_rotation_around_y(angleDelta_7, &tempMatrix);
				matrix_4x4_multiply(&tempMatrix, &sp370->unk84, &mtxs[1]);
				sp360.f[0] = temp_s0_13->f[0];
				sp360.f[1] = temp_s0_13->f[1];
				sp360.f[2] = temp_s0_13->f[2];
				mtx4TransformVecInPlace(&tempMatrix2, &sp360);

				/* PORT note: the vanilla math is correct here - the anchor is
				 * the joint pivot, as in the autogun. An earlier PC-only offset
				 * (rotating about the head joint) lowered the head and hid the
				 * neck, so don't reintroduce it. */
				matrix_4x4_set_position(&sp360, &mtxs[1]);
				matrix_4x4_multiply_homogeneous_in_place(camGetWorldToScreenMtxf(), &mtxs[1]);
			}
			else if (obj->type == PROPDEF_AUTOGUN)
			{
				sp318 = (struct AutogunRecord *) prop->obj;
				sp304 = sp318->unk90 + M_PI_2F;
				sp300 = -sp318->unk9C;

				if (sp304 >= M_TAU_F)
				{
					sp304 -= M_TAU_F;
				}

				temp_s0_13 = model->obj->Switches[1]->Data;
				sp308.f[0] = temp_s0_13->f[0];
				sp308.f[1] = temp_s0_13->f[1];
				sp308.f[2] = temp_s0_13->f[2];

				mtx4TransformVecInPlace(&tempMatrix2, &sp308);
				matrix_4x4_set_rotation_around_y(sp304, &mtxs[1]);
				matrix_4x4_set_position(&sp308, &mtxs[1]);
				matrix_scalar_multiply(sp318->model->scale, mtxs[1].m[0]);
				matrix_4x4_multiply_homogeneous_in_place(camGetWorldToScreenMtxf(), &mtxs[1]);
				temp_s0_13 = (struct coord3d *) model->obj->Switches[2]->Data;
				matrix_4x4_set_rotation_around_z(sp300, &mtxs[2]);
				matrix_4x4_set_position(temp_s0_13, &mtxs[2]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[1], &mtxs[2]);

				if (model->obj->Switches[3] != NULL)
				{
					sp2FC = modelFindNodeMtx(model, model->obj->Switches[3], 0);
					temp_s0_13 = (struct coord3d *) model->obj->Switches[3]->Data;
					matrix_4x4_set_rotation_around_x(sp318->unkB4, sp2FC);
					matrix_4x4_set_position(temp_s0_13, sp2FC);
					matrix_4x4_multiply_homogeneous_in_place(&mtxs[2], sp2FC);
				}

				if (model->obj->Switches[4] != NULL)
				{
					sp2FC = modelFindNodeMtx(model, model->obj->Switches[4], 0);
					temp_s0_13 = (struct coord3d *) model->obj->Switches[4]->Data;
					matrix_4x4_set_identity_and_position(temp_s0_13, sp2FC);
					matrix_4x4_multiply_homogeneous_in_place(&mtxs[2], sp2FC);
				}

				if (model->obj->Switches[6] != NULL)
				{
					sp2FC = modelFindNodeMtx(model, model->obj->Switches[6], 0);
					temp_s0_13 = (struct coord3d *) model->obj->Switches[6]->Data;
					matrix_4x4_set_rotation_around_x(sp318->unkB4, sp2FC);
					matrix_4x4_set_position(temp_s0_13, sp2FC);
					matrix_4x4_multiply_homogeneous_in_place(&mtxs[2], sp2FC);
				}
			}
			else if (obj->type == PROPDEF_COLLECTABLE)
			{
				Mtxf *temp_a1_4;
				sp2F8 = 1;

				if (sp2F8 < model->obj->numMatrices)
				{
					do
					{
						matrix_4x4_set_identity((Mtxf *) (((u8 *) mtxs) + (sp2F8 * 64)));
						sp2F8++;
					}

					while (sp2F8 < model->obj->numMatrices);
				}
			}
			else if (obj->type == PROPDEF_RACK)
			{
				matrix_4x4_set_identity_and_position(model->obj->Switches[0]->Data, &mtxs[1]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[1]);
				matrix_4x4_set_identity_and_position(model->obj->Switches[1]->Data, &mtxs[2]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[2]);
				matrix_4x4_set_identity_and_position(model->obj->Switches[2]->Data, &mtxs[3]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[3]);
				matrix_4x4_set_identity_and_position(model->obj->Switches[3]->Data, &mtxs[4]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[4]);
			}
			else if (obj->type == PROPDEF_VEHICHLE)
			{
				struct VehichleRecord *vehicle_render = (struct VehichleRecord *) obj;

				var_f0_3 = 0.0f;
				temp_v1_7 = model->obj->Switches;
				sp260 = temp_v1_7[1]->Data;
				sp25C = temp_v1_7[2]->Data;
				sp258 = temp_v1_7[3]->Data;
				sp254 = temp_v1_7[4]->Data;
				temp_v0_25 = temp_v1_7[6]->Data;
				sp250 = (temp_v0_25[4] - temp_v0_25[3]) * model->scale;

				if (isSimOwner)
				{
					var_f0_3 = ((vehicle_render->speed * g_GlobalTimerDelta) * M_TAU_F) / ((sp250 * M_TAU_F) * 0.5f);
					vehicle_render->wheelxrot += var_f0_3;
					while (vehicle_render->wheelxrot >= M_TAU_F)
					{
						vehicle_render->wheelxrot -= M_TAU_F;
					}


					while (vehicle_render->wheelxrot < 0.0f)
					{
						vehicle_render->wheelxrot += M_TAU_F;
					}

				}

				vehicle_render->wheelxrot += var_f0_3;
				while (vehicle_render->wheelxrot >= M_TAU_F)
				{
					vehicle_render->wheelxrot -= M_TAU_F;
				}


				while (vehicle_render->wheelxrot < 0.0f)
				{
					vehicle_render->wheelxrot += M_TAU_F;
				}


				matrix_4x4_set_rotation_around_x(vehicle_render->wheelxrot, &sp2AC);
				if (vehicle_render->speed > 0.0f)
				{
					sp24C = vehicle_render->turnrot60;
					sp250 = (sp258->f[2] - sp260->f[2]) * model->scale;
					if (sp24C < 0.0f)
					{
						sp24C = -sp24C;
					}

					sp248 = sinf(sp24C) * sp250;
					vehicle_render->wheelyrot = atan2f(sp248, (cosf(sp24C) * sp250) - (sp250 - vehicle_render->speed));
					if (vehicle_render->wheelyrot < sp24C)
					{
						vehicle_render->wheelyrot = sp24C;
					}

					if (vehicle_render->turnrot60 > 0.0f)
					{
						vehicle_render->wheelyrot = M_TAU_F - vehicle_render->wheelyrot;
					}
				}

				matrix_4x4_set_rotation_around_y(vehicle_render->wheelyrot, &sp26C);
				matrix_4x4_copy(&sp2AC, &mtxs[3]);
				matrix_4x4_set_position(sp258, &mtxs[3]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[3]);
				matrix_4x4_copy(&sp2AC, &mtxs[4]);
				matrix_4x4_set_position(sp254, &mtxs[4]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[4]);
				matrix_4x4_multiply_homogeneous_in_place(&sp26C, &sp2AC);
				matrix_4x4_copy(&sp2AC, &mtxs[1]);
				matrix_4x4_set_position(sp260, &mtxs[1]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[1]);
				matrix_4x4_copy(&sp2AC, &mtxs[2]);
				matrix_4x4_set_position(sp25C, &mtxs[2]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[2]);
			}
			else if (obj->type == PROPDEF_AIRCRAFT)
			{
				struct AircraftRecord *aircraft_render = (struct AircraftRecord *) obj;

				sp1FC = model->obj->Switches[2]->Data;

				if ((g_ClockTimer > 0) && (isSimOwner))
				{
					aircraft_render->rotoryrot += aircraft_render->rotaryspeed;

					while (aircraft_render->rotoryrot >= M_TAU_F)
					{
						aircraft_render->rotoryrot -= M_TAU_F;
					}

					while (aircraft_render->rotoryrot < 0.0f)
					{
						aircraft_render->rotoryrot += M_TAU_F;
					}
				}

				if (aircraft_render->model->anim != NULL)
				{
					sp1B0 = D_80030B34;
					sp1B0.basemtx = camGetWorldToScreenMtxf();
					sp1B0.mtxlist = &mtxs[0];
					subcalcmatrices(&sp1B0, aircraft_render->model);
				}
				else
				{
					matrix_4x4_copy(&mtxs[0], &mtxs[1]);
				}

				if (aircraft_render->flags & PROPFLAG_INMOTION)
				{
					matrix_4x4_set_rotation_around_z(aircraft_render->rotoryrot, &sp200);
				}
				else
				{
					matrix_4x4_set_rotation_around_y(aircraft_render->rotoryrot, &sp200);
				}

				matrix_4x4_copy(&sp200, &mtxs[2]);
				matrix_4x4_set_position(sp1FC, &mtxs[2]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[1], &mtxs[2]);

				if (model->obj->Switches[3] != NULL)
				{
					temp_s0_14 = (struct coord3d *) model->obj->Switches[3]->Data;
					temp_s0_21 = modelFindNodeMtx(model, model->obj->Switches[3], 0);
					matrix_4x4_set_rotation_around_x(aircraft_render->rotoryrot, &sp200);
					matrix_4x4_copy(&sp200, temp_s0_21);
					matrix_4x4_set_position(temp_s0_14, temp_s0_21);
					matrix_4x4_multiply_homogeneous_in_place(&mtxs[1], temp_s0_21);
				}

				if (model->obj->Switches[4] != NULL)
				{
					temp_s0_14 = (struct coord3d *) model->obj->Switches[4]->Data;
					temp_s0_21 = modelFindNodeMtx(model, model->obj->Switches[4], 0);
					matrix_4x4_set_identity_and_position(temp_s0_14, temp_s0_21);
					matrix_4x4_multiply_homogeneous_in_place(&mtxs[1], temp_s0_21);
				}
			}
			else if (obj->type == PROPDEF_TANK)
			{
				struct TankRecord *tank_render = (struct TankRecord *) obj;

				temp_v1_7 = model->obj->Switches;
				sp168 = temp_v1_7[1]->Data;
				sp164 = temp_v1_7[3]->Data;
				sp160 = temp_v1_7[4]->Data;
				sp15C = temp_v1_7[2]->Data;
				sp158 = temp_v1_7[6]->Data;
				sp154 = -tank_render->turret_vertical_angle;

				if (sp154 < 0.0f)
				{
					sp154 += M_TAU_F;
				}

				angleDelta_9 = -tank_render->turret_orientation_angle;

				if (angleDelta_9 < 0.0f)
				{
					angleDelta_9 += M_TAU_F;
				}

				matrix_4x4_set_rotation_around_y(angleDelta_9, &mtxs[1]);
				matrix_4x4_set_position(sp168, &mtxs[1]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[0], &mtxs[1]);
				matrix_4x4_set_rotation_around_x(sp154, &mtxs[3]);
				matrix_4x4_set_position(sp164, &mtxs[3]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[1], &mtxs[3]);
				matrix_4x4_set_rotation_around_y(M_PI_2F, &mtxs[4]);
				matrix_4x4_set_position(sp160, &mtxs[4]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[3], &mtxs[4]);
				matrix_4x4_set_identity_and_position(sp15C, &mtxs[2]);
				matrix_4x4_multiply_homogeneous_in_place(&mtxs[1], &mtxs[2]);
				matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), &mtxs[1], &sp16C);
				sub_GAME_7F03F540(sp158, &sp16C, &tank_render->rect, (struct collision_data *) (&tank_render->collision));

				if (model->obj->Switches[7] != NULL)
				{
					modelGetNodeRwData(model, model->obj->Switches[7])->Gunfire.visible = (s16) tank_render->is_firing_tank;
				}

				if (model->obj->Switches[8] != NULL)
				{
					modelGetNodeRwData(model, model->obj->Switches[8])->Switch.visible = 0;
				}
			}
		}

		modelUpdateRelationsQuick(model, model->obj->RootNode);

		prop->zDepth = -((Mtxf *) model->render_pos)[0].m[3][2];

		chrobjWeaponTick(prop);

		{
			struct PropRecord *current = prop->child;
			while (current != NULL)
			{
				sp684 = current->prev;
				sub_GAME_7F0442DC(current);
				current = sp684;
			}

		}
	}
	else
	{
		prop->flags &= ~PROPFLAG_ONSCREEN;
		chrobjWeaponTick(prop);
		{
			struct PropRecord *current = prop->child;
			while (current != NULL)
			{
				sp684 = current->prev;
				sub_GAME_7F04424C(current);
				current = sp684;
			}

		}
	}

	if (obj->runtime_bitflags & RUNTIMEBITFLAG_00000100)
	{
		obj->runtime_bitflags &= ~RUNTIMEBITFLAG_00000100;
		objApplyDamage(obj, (U32_TO_F32(randomGetNext()) * 4.0f) + 2.0f, &prop->pos, 0, (s32) (((u32) (obj->runtime_bitflags & RUNTIMEBITFLAG_OWNER)) >> RUNTIMEBITSHIFT_OWNER));
	}

	if (isSimOwner)
	{
		if (obj->type == PROPDEF_DOOR)
		{
			sub_GAME_7F053A3C((struct DoorRecord *) prop->obj);
		}
		else if ((obj->type == PROPDEF_AUTOGUN) && (lvlGetControlsLockedFlag() == 0))
		{
			struct AutogunRecord *autogun = (struct AutogunRecord *) prop->obj;
			f32 beam_xdiff;
			f32 dist_local;
			struct beam *beam_local;
			f32 beam_ydiff;
			f32 beam_collisionTile;
			u32 beam_pad[2];
#ifndef TARGET_N64
			static s32 s_AgLastFireGt = -1, s_AgStampGt = -1;
			static f32 s_AgFireDelta = 2.0f;
			s32 agFire;
#endif

			sp13C = 0;
			sp138 = 0;

#ifndef TARGET_N64
			{ /* GE_SNDDBG: one-shot position census so a harness can spawn
			   * in front of an autogun (GE_SPAWNXYZ) */
				static s32 snddbg = -1, census = 0;
				static PropRecord *seen[64];
				s32 ci, dup = 0;
				if (snddbg < 0) { extern char *getenv(const char *); snddbg = getenv("GE_SNDDBG") != NULL; }
				for (ci = 0; ci < census; ci++) { if (seen[ci] == prop) { dup = 1; break; } }
				if (snddbg && census < 64 && !dup)
				{
					extern int printf(const char *, ...);
					seen[census] = prop;
					census++;
					printf("ge: agpos %d pos=(%.0f %.0f %.0f) drone=%d\n", census, prop->pos.f[0], prop->pos.f[1], prop->pos.f[2], (obj->flags & PROPFLAG_IS_DRONE_GUN) != 0);
				}
			}
#endif
#ifndef TARGET_N64
			/* Turret fire is written per tick, so it fired faster at higher
			 * tick rates; gate it to every second field of g_GlobalTimer so it
			 * fires at 30/s like the console, never on sub-ticks or while
			 * paused. Damage uses the 2 fields each step covers so DPS is
			 * unchanged, and GE_AGRATE=60 restores per-tick firing. */
			{
				static s32 agrate = -1;
				if (agrate < 0) { extern char *getenv(const char *); const char *e = getenv("GE_AGRATE"); agrate = (e && *e) ? atoi(e) : 30; }
				if (agrate == 60)
				{
					agFire = (g_ClockTimer > 0);
					s_AgFireDelta = g_GlobalTimerDelta;
				}
				else
				{
					agFire = (g_ClockTimer > 0) && ((g_GlobalTimer >> 1) != ((g_GlobalTimer - g_ClockTimer) >> 1));
					if (agFire && s_AgStampGt != g_GlobalTimer)
					{
						s_AgFireDelta = (s_AgLastFireGt >= 0 && (g_GlobalTimer - s_AgLastFireGt) <= 4) ? (f32) (g_GlobalTimer - s_AgLastFireGt) : 2.0f;
						s_AgLastFireGt = g_GlobalTimer;
						s_AgStampGt = g_GlobalTimer;
					}
				}
			}
			if (agFire && (autogun->is_active != 0) && (!(obj->flags & PROPFLAG_IS_DRONE_GUN)))
#else
			if ((autogun->is_active != 0) && (!(obj->flags & PROPFLAG_IS_DRONE_GUN)))
#endif
			{
				autogun->unkAC = autogun->unkAC + 1;
				sp13C = (autogun->unkAC & 1) == 0;

				if (model->obj->Switches[5] != 0)
				{
					sp138 = (autogun->unkAC & 1) == 1;
				}

				if (autogun->unkC0 < g_GlobalTimer)
				{
#ifndef TARGET_N64
					{
						static s32 snddbg = -1;
						if (snddbg < 0) { extern char *getenv(const char *); snddbg = getenv("GE_SNDDBG") != NULL; }
						if (snddbg)
						{
							extern int printf(const char *, ...);
							extern s32 g_PortTickSerial;
							printf("ge: agsnd ser=%d gt=%d ac=%d c4=%p/%d c8=%p/%d -> %s\n",
							       g_PortTickSerial, g_GlobalTimer, autogun->unkAC,
							       (void *) autogun->unkC4, autogun->unkC4 ? sndGetPlayingState(autogun->unkC4) : -1,
							       (void *) autogun->unkC8, autogun->unkC8 ? sndGetPlayingState(autogun->unkC8) : -1,
							       (autogun->unkC4 == NULL) ? "slot0" : (autogun->unkC8 == NULL) ? "slot1" : "DROPPED");
						}
					}
#endif
					if ((autogun->unkC4 != NULL) && (sndGetPlayingState(autogun->unkC4) != 0))
					{
						sndDeactivate(autogun->unkC4);
					}

					if ((autogun->unkC8 != NULL) && (sndGetPlayingState(autogun->unkC8) != 0))
					{
						sndDeactivate(autogun->unkC8);
					}

					if (autogun->unkC4 == NULL)
					{
						sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_B9_CANNON_SHORT_SFX, &autogun->unkC4);
						chrobjSndCreatePostEventDefault(autogun->unkC4, &prop->pos);
					}
					else if (autogun->unkC8 == NULL)
					{
						sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_B9_CANNON_SHORT_SFX, &autogun->unkC8);
						chrobjSndCreatePostEventDefault(autogun->unkC8, &prop->pos);
					}

					autogun->unkC0 = (s32) (g_GlobalTimer + 2);
				}

				if ((sp13C != 0) || (sp138 != 0))
				{
					sp11C = 1;
					sp10C = NULL;
					sp108 = prop->stan;
					sp104 = (autogun->unkAC & 3) == 0;
					sp100 = getCurrentPlayerProp();
					var_a0_6 = 5;

					if ((model->obj->Switches[7] != 0) && (!(autogun->unkAC & 7)))
					{
						var_a0_6 = 7;
					}

					if ((prop->flags & PROPFLAG_ONSCREEN) && (model->obj->Switches[var_a0_6] != NULL))
					{
						temp_s2_7 = modelFindNodeMtx(model, model->obj->Switches[var_a0_6], 0);
						temp_v1_11 = model->obj->Switches[var_a0_6]->Data;
						sp12C.f[0] = temp_v1_11->f[0];
						sp12C.f[1] = temp_v1_11->f[1];
						sp12C.f[2] = temp_v1_11->f[2];
						matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), temp_s2_7, &spB8);
						mtx4TransformVecInPlace(&spB8, &sp12C);
						if (walkTilesBetweenPoints_NoCallback(&sp108, prop->pos.f[0], prop->pos.f[2], sp12C.f[0], sp12C.f[2]) == 0)
						{
							sp12C.f[0] = prop->pos.f[0];
							sp12C.f[1] = prop->pos.f[1];
							sp12C.f[2] = prop->pos.f[2];
						}
					}
					else
					{
						sp12C.f[0] = prop->pos.f[0];
						sp12C.f[1] = prop->pos.f[1];
						sp12C.f[2] = prop->pos.f[2];
					}

					sp120.f[0] = cosf(autogun->unk9C) * sinf(autogun->unk90);
					sp120.f[1] = sinf(autogun->unk9C);
					sp120.f[2] = cosf(autogun->unk9C) * cosf(autogun->unk90);
					sp110.f[0] = sp12C.f[0] + (sp120.f[0] * 65536.0f);
					sp110.f[1] = sp12C.f[1] + (sp120.f[1] * 65536.0f);
					sp110.f[2] = sp12C.f[2] + (sp120.f[2] * 65536.0f);

					stanResetHits();

					if (stanTestLineUnobstructed(&sp108, sp12C.f[0], sp12C.f[2], sp110.f[0], sp110.f[2], 2, 100.0f, 100.0f, 0.0f, 1.0f) == 0)
					{
						chrlvStanLineDirIntersection(&sp12C, &sp120, &sp110);
						sp10C = sp108;
						sp110.f[0] -= 26.0f * sp120.f[0];
						sp110.f[1] -= 26.0f * sp120.f[1];
						sp110.f[2] -= 26.0f * sp120.f[2];
					}

					if (g_GlobalTimer == ((s32) autogun->unkBC))
					{
						beam_xdiff = sp100->pos.f[0] - sp12C.f[0];
						beam_ydiff = sp100->pos.f[1] - sp12C.f[1];
						beam_collisionTile = sp100->pos.f[2] - sp12C.f[2];
						temp_f20_4 = ((beam_xdiff * beam_xdiff) + (beam_ydiff * beam_ydiff)) + (beam_collisionTile * beam_collisionTile);
						beam_xdiff = sp110.f[0] - sp12C.f[0];
						beam_ydiff = sp110.f[1] - sp12C.f[1];
						beam_collisionTile = sp110.f[2] - sp12C.f[2];
						if ((temp_f20_4 <= (((beam_xdiff * beam_xdiff) + (beam_ydiff * beam_ydiff)) + (beam_collisionTile * beam_collisionTile))) && (bondviewGetIfCurrentPlayerDamageShowTime() == 0))
						{
							temp_f0_35 = sqrtf(temp_f20_4);
#ifndef TARGET_N64
							var_f2_7 = (0.16f * s_AgFireDelta) * g_AutogunPendingDamageTick; /* fields this 30/s step covers */
#else
							var_f2_7 = (0.16f * OBJECT_INTERACTION_TIMER_DELTA) * g_AutogunPendingDamageTick;
#endif
							if (temp_f0_35 > 200.0f)
							{
								var_f2_7 *= 200.0f / temp_f0_35;
							}

							autogun->unkD4 += var_f2_7;
							if (autogun->unkD4 >= 1.0f)
							{
								bondviewCallRecordDamageKills((gunItemGetDestructionAmount(14) * 0.125f) * g_AutogunDamageScalar, autogun->unk90, -1, 1);
								autogun->unkD4 = 0.0f;
								if (bondviewGetIfCurrentPlayerDamageShowTime() != 0)
								{
									sp11C = 0;
								}
							}
						}
					}

					if (sp11C != 0)
					{
						if (sp10C != NULL)
						{
							bullet_spark_create(&sp110, 1, 26.0f, (s16) sp10C->room);
						}

						recall_joy2_hits_edit_flag(14, &sp110, -1);
					}
					else
					{
						sp110.f[0] = sp100->pos.f[0];
						sp110.f[1] = sp100->pos.f[1];
						sp110.f[2] = sp100->pos.f[2];
						recall_joy2_hits_edit_detail_edit_flag(14, sp100, -1);
					}

					if (sp104 != 0)
					{
						beam_local = autogun->beam;
						beam_local->from.f[0] = sp12C.f[0];
						beam_local->from.f[1] = sp12C.f[1];
						beam_local->from.f[2] = sp12C.f[2];
						beam_local->dir.f[0] = sp110.f[0] - beam_local->from.f[0];
						beam_local->dir.f[1] = sp110.f[1] - beam_local->from.f[1];
						beam_local->dir.f[2] = sp110.f[2] - beam_local->from.f[2];
						dist_local = sqrtf(((beam_local->dir.f[0] * beam_local->dir.f[0]) + (beam_local->dir.f[1] * beam_local->dir.f[1])) + (beam_local->dir.f[2] * beam_local->dir.f[2]));
						temp_f2_23 = 1.0f / dist_local;
						beam_local->dir.f[0] = (f32) (beam_local->dir.f[0] * temp_f2_23);
						beam_local->dir.f[1] = (f32) (beam_local->dir.f[1] * temp_f2_23);
						beam_local->dir.f[2] = (f32) (beam_local->dir.f[2] * temp_f2_23);

						if (dist_local > 10000.0f)
						{
							dist_local = 10000.0f;
						}

						beam_local->age = 0;
						beam_local->weaponnum = ITEM_FNP90;
						beam_local->maxdist = dist_local;

						if (dist_local < 500.0f)
						{
							dist_local = 500.0f;
						}

						if (beam_local->weaponnum == ITEM_LASER)
						{
							beam_local->speed = 0.25f * dist_local;
							beam_local->mindist = 0.6f * dist_local;

							if (beam_local->mindist > 3000.0f)
							{
								beam_local->mindist = 3000.0f;
							}

							beam_local->dist = ((-0.1f) - (U32_TO_F32(randomGetNext()) * 0.3f)) * dist_local;
						}
						else
						{
							beam_local->speed = 0.2f * dist_local;
							beam_local->mindist = 0.2f * dist_local;

							if (beam_local->mindist > 3000.0f)
							{
								beam_local->mindist = 3000.0f;
							}

							beam_local->dist = ((2.0f * U32_TO_F32(randomGetNext())) - 1.0f) * beam_local->speed;
						}
					}
				}
			}

#ifndef TARGET_N64
			/* 30/s cadence: hold the flash flags on the in-between ticks (an
			 * inactive or drone gun still clears them every tick as vanilla) */
			if (agFire || autogun->is_active == 0 || (obj->flags & PROPFLAG_IS_DRONE_GUN))
#endif
			{
			if (model->obj->Switches[5] != NULL)
			{
				modelGetNodeRwData(model, model->obj->Switches[5])->Gunfire.visible = (s16) sp13C;
			}

			if (model->obj->Switches[7] != NULL)
			{
				modelGetNodeRwData(model, model->obj->Switches[7])->Gunfire.visible = (s16) sp138;
			}
			}
		}

		objDropRecursively(prop);
	}

	if (tickop == TICKOP_CHANGEDLIST)
	{
		prop->stan = NULL;
	}

	return tickop;
}


/**
 * Address: 7F049B58
 *
 * Draws tracers for characters other than the player, and draws tracers for drone guns as well.
 */
Gfx *weaponRenderTracers(Gfx *gdl)
{
    ChrRecord *chr;
    ChrRecord *chr2;
    PropRecord *prop;
    ObjectRecord *obj;
    s32 playernum;
    s32 type;
    s32 type_viewer;
    s32 obj_type_0d;
    s32 match;
    s32 one;
    s32 type_chr;

    prop = chrpropGetActiveTail();
    if (prop != NULL)
    {
        type_viewer = PROP_TYPE_VIEWER;
        obj_type_0d = 0x0d;
        one = 1;
        type_chr = PROP_TYPE_CHR;

        do
        {
            type = prop->type;
            if (type_chr == type)
            {
                chr = prop->chr;
                gdl = sub_GAME_7F061E18(gdl, &chr->beams[0], one);
                gdl = sub_GAME_7F061E18(gdl, &chr->beams[1], one);
            }
            else if (one == type)
            {
                obj = prop->obj;
                match = obj_type_0d == obj->type;
                if (match)
                {
                    gdl = sub_GAME_7F061E18(gdl, (BeamRecord *)((AutogunRecord *)obj)->beam, one);
                }
            }
            else if (type_viewer == type)
            {
                if (prop->voidp != NULL)
                {
                    playernum = getPlayerPointerIndex(prop);
                    if (get_cur_playernum() != playernum)
                    {
                        chr2 = prop->chr;
                        gdl = sub_GAME_7F061E18(gdl, &chr2->beams[0], one);
                        gdl = sub_GAME_7F061E18(gdl, &chr2->beams[1], one);
                    }
                }
            }
            prop = prop->prev;
        }
        while (prop != NULL);
    }
    return gdl;
}


void save_ptr_monitor_ani_code_to_obj_ani_slot(MonitorRecord *mon, void *image)
{
    mon->cmdlist  = image;
    mon->offset = 0;
}


void monitorSetImageByNum(MonitorRecord *mon, s32 monAnimID)
{
    MonitorCmdWord *image = monAnim00Bond;
    switch (monAnimID)
    {
         default:
         case 0:
            break;
         case 1:
            image = &monAnim01DesktopsSatellite;
            break;
        case 2:
            image = &monAnim02Astrological;
            break;
        case 3:
            image = &monAnim03ThreeWavePattern;
            break;
        case 4:
            image = &monAnim04WavePattern;
            break;
        case 5:
            image = &monAnim05GreenTextUp;
            break;
        case 6:
            image = &monAnim06RedTextDown;
            break;
        case 7:
            image = &monAnim07GreenTextDown;
            break;
        case 8:
            image = &monAnim08RedBarGraph;
            break;
        case 9:
            image = &monAnim09BlueBarGraph;
            break;
        case 10:
            image = &monAnim0AGreenBarGraph;
            break;
        case 11:
            image = &monAnim0BRadar;
            break;
        case 12:
            image = &monAnim0CSpinningCube;
            break;
        case 13:
            image = &monAnim0DLocWeapArmed;
            break;
        case 14:
            image = &monAnim0ERedTarget;
            break;
        case 15:
            image = &monAnim0FSatelliteTargeting;
            break;
        case 16:
            image = &monAnim10GlobalMap;
            break;
        case 17:
            image = &monAnim11KarlYelling;
            break;
        case 18:
            image = &monAnim12Skateboard;
            break;
        case 19:
            image = &monAnim13PoliceGuy;
            break;
        case 20:
            image = &monAnim14Off;
            break;
        case 21:
            image = &monAnim15RandomSeven;
            break;
        case 22:
            image = &monAnim16RandomFour;
            break;
        case 23:
            image = &monAnim17RandImageEffect;
            break;
        case 24:
            image = &monRandEffectChanceSHUTTLE1;
            break;
        case 25:
            image = &monRandEffectChanceSHUTTLE2;
            break;
        case 26:
            image = &monRandEffectChanceEARTHFULL1;
            break;
        case 27:
            image = &monRandEffectChanceEARTHFULL2;
            break;
        case 28:
            image = &monRandEffectChanceBLUESTARS;
            break;
        case 29:
            image = &monRandEffectChanceGALAXY1;
            break;
        case 30:
            image = &monRandEffectChanceGALAXY2;
            break;
        case 31:
            image = &monRandEffectChanceEARTHTEXT;
            break;
        case 32:
            image = &monRandEffectChanceTARGETEARTH;
            break;
        case 33:
            image = &monRandEffectChanceGALAXY3;
            break;
        case 34:
            image = &monRandChanceScrollOrZoomRandRGBN;
            break;
        case 35:
            image = &monRandChanceScrollOrZoomRed;
            break;
        case 36:
            image = &monRandChanceScrollOrZoomGreen;
            break;
        case 37:
            image = &monRandChanceScrollOrZoomBlue;
            break;
        case 38:
            image = &monRandChanceScrollOrZoom;
            break;
        case 39:
            image = &monAnim27RandomEffectScrollRight;
            break;
        case 40:
            image = &monAnim28RandomEffectScrollUpFast;
            break;
        case 41:
            image = &monAnim29RandomEffectScrollUp;
            break;
        case 42:
            image = &monAnim2ARandEffectScrollZoom1;
            break;
        case 43:
            image = &monAnim2ARandEffectScrollZoom2;
            break;
        case 44:
            image = &monAnim2CRandEffectWaitRoute;
            break;
        case 45:
            image = &monAnim2DRandEffectFlash;
            break;
        case 46:
            image = &monAnim2ERedBrightening;
            break;
        case 47:
            image = &monAnim2FGreenBrightening;
            break;
        case 48:
            image = &monAnim30GreySolid;
            break;
        case 49:
            image = &monAnim31RedSolid;
            break;
        case 50:
            image = &monAnim32GreenSolid;
            break;
        case 51:
            image = &monAnim33BlackSolid;
            break;
    }
    save_ptr_monitor_ani_code_to_obj_ani_slot(mon,  image);
}


void save_img_index_to_obj_ani_slot(MonitorRecord *mon, void *unk88)
{
    mon->tconfig = unk88;
}


Gfx *process_monitor_animation_microcode(Model *model, ModelNode *node, MonitorRecord *screen, Gfx *gdl, s32 arg4, s32 arg5)
{
    if (node && (node->Opcode & 0xff) == MODELNODE_OPCODE_DLCOLLISION) 
    {
        Vertex *vertices = dynAllocateVertices(4);
        Gfx *savedgdl = gdl++;
        union ModelRoData *rodata = node->Data;
        union ModelRwData *rwdata = modelGetNodeRwData(model, node);
        sImageTableEntry *tconfig;
        bool yielding = FALSE;

        while (!yielding) 
        {
            struct tvcmd *m = (struct tvcmd *) &screen->cmdlist[screen->offset];

            switch (m->type) 
            {
            case TVCMD_STOPSCROLL:
                screen->xmidinc = 0.0f;
                screen->ymidinc = 0.0f;
                screen->offset++;
                break;
            case TVCMD_SCROLLRELX:
                screen->xmidfrac = 0.0f;
                screen->xmidinc = 1.0f / m->arg2;
                screen->xmidold = screen->xmid;
                screen->xmidnew = screen->xmid + (s32)m->time * (1.0f / 1024.0f);
                screen->offset += 3;
                break;
            case TVCMD_SCROLLRELY:
                screen->ymidfrac = 0.0f;
                screen->ymidinc = 1.0f / m->arg2;
                screen->ymidold = screen->ymid;
                screen->ymidnew = screen->ymid + (s32)m->time * (1.0f / 1024.0f);
                screen->offset += 3;
                break;
            case TVCMD_SCROLLABSX:
                screen->xmidfrac = 0.0f;
                screen->xmidinc = 1.0f / m->arg2;
                screen->xmidold = screen->xmid;
                screen->xmidnew = (s32)m->time * (1.0f / 1024.0f);
                screen->offset += 3;
                break;
            case TVCMD_SCROLLABSY:
                screen->ymidfrac = 0.0f;
                screen->ymidinc = 1.0f / m->arg2;
                screen->ymidold = screen->ymid;
                screen->ymidnew = (s32)m->time * (1.0f / 1024.0f);
                screen->offset += 3;
                break;
            case TVCMD_SCALEABSX:
                screen->xscalefrac = 0.0f;
                screen->xscaleinc = 1.0f / m->arg2;
                screen->xscaleold = screen->xscale;
                screen->xscalenew = (s32)m->time * (1.0f / 1024.0f);
                screen->offset += 3;
                break;
            case TVCMD_SCALEABSY:
                screen->yscalefrac = 0.0f;
                screen->yscaleinc = 1.0f / m->arg2;
                screen->yscaleold = screen->yscale;
                screen->yscalenew = (s32)m->time * (1.0f / 1024.0f);
                screen->offset += 3;
                break;
            case TVCMD_SETTEXTURE:
                save_img_index_to_obj_ani_slot(screen, m->time);
                screen->offset += 2;
                break;
            case TVCMD_PAUSE:
                if (screen->pause60 >= 0) 
                {
                    screen->pause60 -= g_ClockTimer;

                    if (screen->pause60 >= 0) 
                    {
                        yielding = TRUE;
                    } 
                    else 
                    {
                        screen->offset += 2;
                    }
                } 
                else 
                {
                    #ifdef DEBUG
                    assert(m->time>0);
                    #endif

                    yielding = TRUE;
                    screen->pause60 = m->time;
                }
                break;
            case TVCMD_SETCMDLIST:
                save_ptr_monitor_ani_code_to_obj_ani_slot(screen, (u32 *) m->time);
                break;
            case TVCMD_RANDSETCMDLIST:
                if ((randomGetNext() >> 16) < m->arg2) 
                {
                    save_ptr_monitor_ani_code_to_obj_ani_slot(screen, (u32 *) m->time);
                } 
                else 
                {
                    screen->offset += 3;
                }
                break;
            case TVCMD_RESTART:
                screen->offset = 0;
                break;
            case TVCMD_YIELD:
                yielding = TRUE;
                break;
            case TVCMD_SETCOLOUR:
                screen->colfrac = 0.0f;
                screen->colinc = 1.0f / m->arg2;

                screen->redold = screen->red;
                screen->rednew = ((u32)m->time >> 24) & 0xff;

                screen->greenold = screen->green;
                screen->greennew = ((u32)m->time >> 16) & 0xff;

                screen->blueold = screen->blue;
                screen->bluenew = ((u32)m->time >> 8) & 0xff;

                screen->alphaold = screen->alpha;
                screen->alphanew = m->time & 0xff;

                screen->offset += 3;
                break;
            case TVCMD_ROTATEABS:
                screen->rot = m->time * M_TAU_F / M_U16_MAX_VALUE_F;
                screen->offset += 2;
                break;
            case TVCMD_ROTATEREL:
                screen->rot += MONITOR_TIMER_DELTA * m->time * M_TAU_F / M_U16_MAX_VALUE_F;

                if (screen->rot >= M_TAU_F) 
                {
                    screen->rot -= M_TAU_F;
                }

                if (screen->rot < 0.0f) 
                {
                    screen->rot += M_TAU_F;
                }

                screen->offset += 2;
                break;
            }
        }

        if (screen->rot == (1.0f / 1024.0f));

        // Increment X scale
        if (screen->xscaleinc > 0.0f) 
        {
            screen->xscalefrac += screen->xscaleinc * MONITOR_TIMER_DELTA;

            if (screen->xscalefrac < 1.0f) 
            {
                screen->xscale = screen->xscaleold + (screen->xscalenew - screen->xscaleold) * screen->xscalefrac;
            } 
            else 
            {
                screen->xscalefrac = 1.0f;
                screen->xscaleinc = 0.0f;
                screen->xscale = screen->xscalenew;
            }
        }

        // Increment Y scale
        if (screen->yscaleinc > 0.0f) 
        {
            screen->yscalefrac += screen->yscaleinc * MONITOR_TIMER_DELTA;

            if (screen->yscalefrac < 1.0f) 
            {
                screen->yscale = screen->yscaleold + (screen->yscalenew - screen->yscaleold) * screen->yscalefrac;
            } 
            else 
            {
                screen->yscalefrac = 1.0f;
                screen->yscaleinc = 0.0f;
                screen->yscale = screen->yscalenew;
            }
        }

        // Increment X scroll
        if (screen->xmidinc > 0.0f) 
        {
            screen->xmidfrac += screen->xmidinc * MONITOR_TIMER_DELTA;

            if (screen->xmidfrac < 1.0f) 
            {
                screen->xmid = screen->xmidold + (screen->xmidnew - screen->xmidold) * screen->xmidfrac;
            } 
            else 
            {
                screen->xmidfrac = 1.0f;
                screen->xmidinc = 0.0f;
                screen->xmid = screen->xmidnew;
            }
        }

        // Increment Y scroll
        if (screen->ymidinc > 0.0f) 
        {
            screen->ymidfrac += screen->ymidinc * MONITOR_TIMER_DELTA;

            if (screen->ymidfrac < 1.0f) 
            {
                screen->ymid = screen->ymidold + (screen->ymidnew - screen->ymidold) * screen->ymidfrac;
            } 
            else 
            {
                screen->ymidfrac = 1.0f;
                screen->ymidinc = 0.0f;
                screen->ymid = screen->ymidnew;
            }
        }

        // Increment colour change
        if (screen->colinc > 0.0f) 
        {
            screen->colfrac += screen->colinc * MONITOR_TIMER_DELTA;

            if (screen->colfrac < 1.0f) 
            {
                screen->red = screen->redold + (s32) ((screen->rednew - screen->redold) * screen->colfrac);
                screen->green = screen->greenold + (s32) ((screen->greennew - screen->greenold) * screen->colfrac);
                screen->blue = screen->blueold + (s32) ((screen->bluenew - screen->blueold) * screen->colfrac);
                screen->alpha = screen->alphaold + (s32) ((screen->alphanew - screen->alphaold) * screen->colfrac);
            } 
            else 
            {
                screen->colfrac = 1.0f;
                screen->colinc = 0.0f;
                screen->red = screen->rednew;
                screen->green = screen->greennew;
                screen->blue = screen->bluenew;
                screen->alpha = screen->alphanew;
            }
        }

        // Set up everything for rendering
        rwdata->DisplayListCollisions.gdl = gdl;
        rwdata->DisplayListCollisions.Vertices = vertices;

        vertices[0] = rodata->DisplayListCollisions.Vertices[0];
        vertices[1] = rodata->DisplayListCollisions.Vertices[1];
        vertices[2] = rodata->DisplayListCollisions.Vertices[2];
        vertices[3] = rodata->DisplayListCollisions.Vertices[3];

        if ((uintptr_t)screen->tconfig < 100)
        {
            tconfig = &monitorimages[(intptr_t)screen->tconfig];
        } 
        else 
        {
            tconfig = screen->tconfig;
        }

        {
            f32 tmp1;
            f32 tmp2;

            if (tconfig != NULL) 
            {
                u32 stack[11];
                f32 xfrac1;
                f32 yfrac1;
                f32 xfrac2;
                f32 yfrac2;
                f32 cosrot;
                f32 sinrot;
                f32 rotscale;

                xfrac1 = screen->xscale / 2.0f;
                yfrac1 = screen->yscale / 2.0f;
                xfrac2 = xfrac1;
                yfrac2 = yfrac1;

                if (xfrac1 || 1);
                if (yfrac1 || 1);

                tmp1 = 1.5707964f;
                if (tmp1 == tmp1);

                if (screen->rot != 0.0f) 
                {
                    cosrot = cosf(screen->rot);
                    rotscale = 1.4141999f;
                    cosrot *= rotscale;
                    sinrot = sinf(screen->rot);

                    if (rotscale == rotscale);
                    if (sinrot == sinrot);
                    if (sinrot == sinrot);

                    xfrac1 *= cosrot;
                    sinrot *= rotscale;
                    yfrac1 *= sinrot;
                    xfrac2 *= sinrot;
                    yfrac2 *= cosrot;
                }

                tmp1 = xfrac1 * yfrac1 * xfrac2;

                if (tmp1 * yfrac2);

                vertices[0].s = tconfig->width  * (screen->xmid + xfrac1) * 32.0f;
                vertices[0].t = tconfig->height * (screen->ymid + yfrac1) * 32.0f;

                vertices[1].s = tconfig->width  * (screen->xmid - xfrac2) * 32.0f;
                vertices[1].t = tconfig->height * (screen->ymid + yfrac2) * 32.0f;

                vertices[2].s = tconfig->width  * (screen->xmid - xfrac1) * 32.0f;
                vertices[2].t = tconfig->height * (screen->ymid - yfrac1) * 32.0f;

                vertices[3].s = tconfig->width  * (screen->xmid + xfrac2) * 32.0f;
                vertices[3].t = tconfig->height * (screen->ymid - yfrac2) * 32.0f;
            }

            tmp2 = tmp1;
        }

        if (1) 
        {
            u8 tmpc;
            u8 tmpc2;
            tmpc = screen->red;
            vertices[3].r = tmpc;
            vertices[2].r = tmpc;
            vertices[1].r = tmpc;
            vertices[0].r = tmpc;

            tmpc = screen->green;
            vertices[3].g = tmpc;
            vertices[2].g = tmpc;
            vertices[1].g = tmpc;
            vertices[0].g = tmpc;

            tmpc2 = screen->blue;
            vertices[3].b = tmpc2;
            vertices[2].b = tmpc2;
            vertices[1].b = tmpc2;
            vertices[0].b = tmpc2;

            tmpc = screen->alpha;
            vertices[3].a = tmpc;
            vertices[2].a = tmpc;
            vertices[1].a = tmpc;
            vertices[0].a = tmpc;
        }

        if (screen->alpha < 255) 
        {
            arg5 = 2;
        }

        // Render the image
        gSPSetGeometryMode(gdl++, G_CULL_BACK);

        texSelect(&gdl, tconfig, arg5, arg4, 2);

        gSPMatrix(gdl++, osVirtualToPhysical(model->render_pos), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPSegment(gdl++, SPSEGMENT_MODEL_VTX, osVirtualToPhysical(vertices));
        gSPVertex(gdl++, 0x04000000, 4, 0);
        gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
        gSPEndDisplayList(gdl++);

        gSPBranchList(savedgdl++, gdl);
    }

    return gdl;
}


void sub_GAME_7F04AC20(PropRecord *prop, ModelRenderData *mrData, s32 arg2)
{
    if (prop->flags & PROPFLAG_ONSCREEN)
    {
        ObjectRecord *obj;
        Model *model;
        s32 alpha;
        PropRecord *child;
        Gfx *gdl;
        s32 mN;
        s32 m0;
        ObjectRecord *o;
        DoorRecord *door;
        s32 pad[4];
        s32 recolor;
        s32 v;
        ModelNode *node;
        Gfx *g3;
        Gfx *g;
        Gfx *g2;
        bool destroyed;
        union ModelRwData **rwdataSlot;

        obj = prop->obj;
        model = obj->model;
        destroyed = (obj->flags & PROPFLAG_00000200) != FALSE;

        if (destroyed)
        {
            destroyed = get_BONDdata_field_10E0();
            destroyed = destroyed != 0;
        }

        gdl = mrData->gdl;

        if (obj->type == PROPDEF_MONITOR)
        {
            if (mrData->flags & 1)
            {
                if (obj->flags2 & PROPFLAG2_00010000)
                {
                    mN = 0;
                }
                else if (obj->flags & PROPFLAG_FIXED_MONITOR)
                {
                    mN = 8;
                }
                else
                {
                    mN = 1;
                }

                gdl = process_monitor_animation_microcode(model, model->obj->Switches[0], &((MonitorObjRecord *)prop->obj)->Monitor, gdl, mN, 1);
            }
        }
        else if (obj->type == PROPDEF_MULTI_MONITOR)
        {
            if (mrData->flags & 1)
            {
                o = prop->obj;

                if (obj->flags2 & PROPFLAG2_00010000)
                {
                    mN = 0;
                }
                else if (obj->flags & PROPFLAG_FIXED_MONITOR)
                {
                    mN = 8;
                }
                else
                {
                    mN = 1;
                }

                gdl = process_monitor_animation_microcode(model, model->obj->Switches[0], &((MultiMonitorObjRecord *)o)->Monitor[0], gdl, mN, 1);

                if (obj->flags2 & PROPFLAG2_00010000)
                {
                    mN = 0;
                }
                else if (obj->flags & (PROPFLAG_FIXED_MONITOR | PROPFLAG_SPECIAL_FUNC))
                {
                    mN = 8;
                }
                else
                {
                    mN = 1;
                }

                gdl = process_monitor_animation_microcode(model, model->obj->Switches[1], &((MultiMonitorObjRecord *)o)->Monitor[1], gdl, mN, 1);

                gdl = process_monitor_animation_microcode(model, model->obj->Switches[2], &((MultiMonitorObjRecord *)o)->Monitor[2], gdl, mN, 1);

                gdl = process_monitor_animation_microcode(model, model->obj->Switches[3], &((MultiMonitorObjRecord *)o)->Monitor[3], gdl, mN, 1);
            }
        }

        if (obj->type == PROPDEF_DOOR)
        {
            door = prop->door;

            gSPClearGeometryMode(gdl++, G_CULL_BOTH);

            if (door->doorFlags & DOORFLAG_FLIP)
            {
                mrData->cullmode = CULLMODE_FRONT;
            }
            else
            {
                mrData->cullmode = CULLMODE_BACK;
            }

            if (mrData->PropType == PROP_TYPE_MAX)
            {
                mrData->envcolour.word &= 0xffffff00;
            }
        }
        else
        {
            node = sub_GAME_7F04B478(obj);
            recolor = 0;

            if (node != NULL)
            {
                union ModelRoData *nodedata;

                nodedata = node->Data;

                if (nodedata != NULL)
                {
                    rwdataSlot = &obj->model->datas[nodedata->DisplayListCollisions.RwDataIndex];

                    if (nodedata->DisplayListCollisions.Vertices != (Vertex *)*rwdataSlot)
                    {
                        recolor = 1;
                    }
                }
            }

            v = objGetDestroyedLevel(obj);

            if ((v == 0) || !recolor)
            {
                mrData->cullmode = CULLMODE_BACK;

                if (mrData->PropType == PROP_TYPE_MAX)
                {
                    mrData->envcolour.word &= 0xffffff00;
                }
            }
            else
            {
                v = objGetDestroyedLevel(obj);
                mrData->cullmode = CULLMODE_NONE;

                if (mrData->PropType == PROP_TYPE_MAX)
                {
                    alpha = v * 50 + 100;

                    if (alpha >= 256)
                    {
                        alpha = 255;
                    }

                    mrData->envcolour.word &= 0xffffff00;
                    mrData->envcolour.word |= alpha;
                }
                else if (v > 0)
                {
                    mrData->envcolour.word |= 0xff00;
                }
            }
        }

        if (destroyed)
        {
            // Keep on one line for matching.
            g3 = gdl++; gSPMatrix(g3, get_BONDdata_field_10E0(), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
        }

        mrData->gdl = gdl;
        subdraw(mrData, model);
        gdl = mrData->gdl;

        if (obj->type == PROPDEF_DOOR)
        {
            gSPClearGeometryMode(gdl++, G_CULL_BOTH);
        }

        if (obj->state & (1 << arg2))
        {
            gdl = explosionRenderBulletImpactOnProp(gdl, prop, arg2);
        }

        if (destroyed)
        {
            // Keep on one line for matching.
            g2 = gdl++; gSPMatrix(g2, currentPlayerGetProjectionMatrix(), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
        }

        mrData->gdl = gdl;

        for (child = prop->child; child != NULL; child = child->prev)
        {
            sub_GAME_7F04AC20(child, mrData, arg2);
        }

        if (arg2)
        {
            if (destroyed != FALSE)
            {
                sub_GAME_7F08BEEC((Mtxf *)model->render_pos, model->obj->numMatrices);
            }
            else
            {
                bondviewTransformManyPosToViewMatrix(model->render_pos, model->obj->numMatrices);
            }
        }
    }
}


/**
 * Address 0x7F04B150.
*/

Gfx *chrobjRenderProp(PropRecord *prop, Gfx *gdl, s32 arg2)
{
    struct rgba_f32 spB0;
    s32 spAC;
    s32 spA8;
    ModelRenderData mrData;
    struct view4f sp58;
    struct rgba_s32 sp48;
    s32 sp44;
    ObjectRecord *obj;
    s32 objAlpha;
    f32 temp_f0;
    s32 temp_v0_4;
    s32 phi_a0;

    obj = prop->obj;

    mrData = D_80031FD0;

    objAlpha = 0xFF;
    spAC = fogGetPropDistColor(prop, &spB0);

    if (spAC == 0)
    {
#ifndef TARGET_N64
        if (g_PortFadeDbg) { portFadeNote(prop, 3, 0, spB0.rgba[3]); }
#endif
        return gdl;
    }

    if ((u8) obj->type != PROPDEF_TINTED_GLASS)
    {
        temp_f0 = chrobjFogVisRangeRelated(prop, getinstsize(obj->model));

        if (((s32) prop->timetoregen > 0) && ((s32) prop->timetoregen < CHROBJ_TIMETOREGEN))
        {
            temp_f0 *= ((CHROBJ_TIMETOREGEN_F - (f32) prop->timetoregen) / CHROBJ_TIMETOREGEN_F);
        }

        objAlpha = (s32) (temp_f0 * 255.0f);

        if (objAlpha <= 0)
        {
#ifndef TARGET_N64
            if (g_PortFadeDbg) { portFadeNote(prop, 4, 0, spAC == 1 ? spB0.rgba[3] : 0.0f); }
#endif
            return gdl;
        }
    }
#ifndef TARGET_N64
    if (g_PortFadeDbg) { portFadeNote(prop, objAlpha < 0xFF ? 5 : 6, objAlpha, spAC == 1 ? spB0.rgba[3] : 0.0f); }
#endif

    if ((objAlpha < 0xFF) || (obj->flags2 & 0x10000))
    {
        if (arg2 == 0)
        {
            return gdl;
        }

        sp44 = 3;
    }
    else
    {

        sp44 = (arg2 == 0) ? 1 : 2;
    }

    if ((getPropCombinedRoomsBBox2D(prop, &sp58) > 0) && (((s32)obj->flags2 << 5) >= 0))
    {
        gdl = bgScissorCurrentPlayerViewF(gdl, sp58.left, sp58.top, sp58.width, sp58.height);
    }
    else
    {
        gdl = bgScissorCurrentPlayerViewDefault(gdl);
    }

    mrData.flags = sp44;
    mrData.zbufferenabled = (obj->flags2 & 0x10000) == 0;

    mrData.gdl = gdl;

    if (objAlpha < 0xFF)
    {
        mrData.PropType = 5;
        mrData.envcolour.word = objAlpha;
    }
    else
    {
        mrData.PropType = 9;

        if (obj->type == PROPDEF_TINTED_GLASS)
        {
            mrData.envcolour.word = ((struct TintedGlassRecord*)obj)->calculatedopacity << 8;
        }
        else if ((obj->type == PROPDEF_DOOR) && (((struct DoorRecord*)obj)->doorFlags & DOORFLAG_WINDOWED))
        {
            mrData.envcolour.word = ((struct DoorRecord*)obj)->calculatedopacity << 8;
        }
        else
        {
            mrData.envcolour.word = 0;
        }

#ifndef TARGET_N64
        { /* GE_GLASSDBG=1 (diag): one line per tinted-glass draw with the
           * object, its opacity (PRIM alpha), the tint/opaque distances and the
           * distance to Bond.
           * Rate-limited to one line per object per ~1s. */
            static s32 dbg = -1;
            extern char *getenv(const char *);
            if (dbg < 0) dbg = getenv("GE_GLASSDBG") != NULL;
            if (dbg && (g_GlobalTimer % 60) == 0)
            {
                PropRecord *pp = getCurrentPlayerProp();
                f32 dx = obj->runtime_pos.x - pp->pos.x;
                f32 dy = obj->runtime_pos.y - pp->pos.y;
                f32 dz = obj->runtime_pos.z - pp->pos.z;
                s32 tint = 0, opa = 0, opac = -1;
                if (obj->type == PROPDEF_TINTED_GLASS)
                {
                    tint = ((struct TintedGlassRecord*)obj)->TintDist;
                    opa = ((struct TintedGlassRecord*)obj)->CullDist;
                    opac = ((struct TintedGlassRecord*)obj)->calculatedopacity;
                }
                else if (obj->type == PROPDEF_DOOR)
                {
                    tint = ((struct DoorRecord*)obj)->TintDist;
                    opa = doorGlassOpaqueDistWord((struct DoorRecord*)obj);
                    opac = ((struct DoorRecord*)obj)->calculatedopacity;
                }
                fprintf(stderr, "glassdbg: type=%d pad=%d opac=%d tint=%d opa=%d dist=%.0f pos=(%.0f %.0f %.0f) env=%08x\n",
                        obj->type, obj->pad, opac, tint, opa, sqrtf(dx * dx + dy * dy + dz * dz),
                        obj->runtime_pos.x, obj->runtime_pos.y, obj->runtime_pos.z, mrData.envcolour.word);
            }
        }
#endif
    }

    temp_v0_4 = objGetShotsTaken(obj);
    phi_a0 = 0xFF - (temp_v0_4 * 0x15);

    if (phi_a0 < 0)
    {
        phi_a0 = 0;
    }

    sp48.r = (s32) (obj->shadecol.rgba[0] * phi_a0) >> 8;
    sp48.g = (s32) (obj->shadecol.rgba[1] * phi_a0) >> 8;
    sp48.b = (s32) (obj->shadecol.rgba[2] * phi_a0) >> 8;
    sp48.a = obj->shadecol.rgba[3] + temp_v0_4 * 0xF;

    if (sp48.a >= 0x100)
    {
        sp48.a = 0xFF;
    }

    lerp_rgba_s32_with_rgba_f32(&sp48, spAC, &spB0);

    mrData.fogcolour.word = (sp48.rgba[0] << 0x18) | (sp48.rgba[1] << 0x10) | (sp48.rgba[2] << 0x08) | (sp48.rgba[3] << 0x00);

    sub_GAME_7F04AC20(prop, &mrData, arg2);

    return mrData.gdl;
}


ModelNode* sub_GAME_7F04B478(ObjectRecord* obj)
{
    ModelFileHeader* header = obj->model->obj;
    ModelNode *node = header->RootNode;

    while (node)
    {
        u32 type = node->Opcode & 0xff;

        switch (type)
        {
            case MODELNODE_OPCODE_DLCOLLISION:
                return node;
            case MODELNODE_OPCODE_LOD:
                modelApplyDistanceRelations(obj->model, node);
                break;
            case MODELNODE_OPCODE_SWITCH:
                modelApplyToggleRelations(obj->model, node);
                break;
            case MODELNODE_OPCODE_HEAD:
                modelApplyHeadRelations(obj->model, node);
                break;
        }

        if (node->Child)
        {
            node = node->Child;
        }
        else
        {
            while (node)
            {
                if (node->Next)
                {
                    node = node->Next;
                    break;
                }

                node = node->Parent;
            }
        }
    }

    return NULL;
}


bool sub_GAME_7F04B590(ModelFileHeader* arg0, ModelNode* arg1)
{
    ModelSkeleton* onescreen = &skeleton_console_one_screen;
    ModelSkeleton* fourscreen = &skeleton_console_four_screen;

    if ((onescreen == arg0->Skeleton))
    {
        if (arg1 == arg0->Switches[0])
        {
            return FALSE;
        }
    }

    if (fourscreen == arg0->Skeleton)
    {
        if ((arg1 == arg0->Switches[0]) || (arg1 == arg0->Switches[1]) || (arg1 == arg0->Switches[2]) || (arg1 == arg0->Switches[3]))
        {
            return FALSE;
        }
    }

    return TRUE;
}


typedef struct Word4 { u32 w0; u32 w1; u32 w2; u32 w3; } Word4;


/*
*   objDeform - Deform an object due to it being destroyed.
*   PD has a very similar function of the same name
*   Address: 7F04B610
*/
void objDeform(ObjectRecord *obj, E_EXPLOSIONTYPE explosiontype)
{
    ModelNode *node;
    ModelRoData_DisplayList_CollisionRecord *rodata;
    Model *model;
    Vertex **vtxslot;
    Vertex *newverts;
    s32 adjust_height;
    s32 ymid;
    s32 deformseed;
    f32 shrink;
    bool allow_blackening;
    ModelNode *nodeCopy;
    f32 yscale;
    s32 chance;
    s32 i;
    f32 modelscale;
    s32 offset;
    s32 ymin;
    s32 ymax;
    
    model = obj->model;

    // Keep on one line for matching.
    ymin = 99999; ymax = -99999;
    
    node = sub_GAME_7F04B478(obj);
    nodeCopy = node;
    
    if (nodeCopy == NULL)
    {
        return;
    }
    
    rodata = (ModelRoData_DisplayList_CollisionRecord *) nodeCopy->Data;
    
    if (rodata == NULL)
    {
        return;
    }
    
    if (!sub_GAME_7F04B590(obj->model->obj, nodeCopy))
    {
        return;
    }
    
    vtxslot = (Vertex **) (&model->datas[rodata->RwDataIndex]);
    
#ifdef VERSION_EU
    if (obj->obj < PROP_WINDOW)
    {
        /* explosiontype is a deform-stage counter (objExplode passes
         * (shots >> 2) + 1, growing without bound as an already-destroyed
         * object keeps getting shot), so explosiontype/explosiontype+3 can
         * exceed this table's per-object seed[6]. On real hardware that
         * just spills into the next object's seed[] entry; express it as
         * a flat read over the whole table instead of a single seed[6]
         * subscript so it's well-defined instead of UB, while reading the
         * identical bytes. */
        if (randomGetNext() & 1)
        {
            deformseed = (u16) ((s16 *) object_explosion_details.seeds)[(obj->obj * 6) + explosiontype];
        }
        else
        {
            deformseed = (u16) ((s16 *) object_explosion_details.seeds)[(obj->obj * 6) + explosiontype + 3];
        }
    }
    else
    {
        deformseed = 0;
    }
#else
    /* Same flat-read rationale as the VERSION_EU arm above, but this
     * table's element is {TypeID; Seed[6]} (7 s16 words/element), so the
     * flat offset needs +1 to skip TypeID. */
    if (randomGetNext() & 1)
    {
        deformseed = (u16) ((s16 *) object_explosion_details)[(obj->obj * 7) + 1 + explosiontype];
    }
    else
    {
        deformseed = (u16) ((s16 *) object_explosion_details)[(obj->obj * 7) + 1 + (ymid = explosiontype + 3)];
    }
#endif
    
    if (get_debug_explosioninfo_flag() || (deformseed == 0))
    {
        get_debug_explosioninfo_flag();
        deformseed = randomGetNext();
        
        if (get_debug_explosioninfo_flag())
        {
            deformseed &= 0xffff;
        }
    }
    
    explosionClearBulletImpactRoom(obj->prop);
    
    if (obj->obj == PROP_TV_HOLDER)
    {
        adjust_height = 0;
    }
    else
    {
        adjust_height = 1;
    }
    
    newverts = vtxstore_allocate(rodata->numVertices, 0x0b0b, model->obj, objGetDestroyedLevel(obj));
    
    if (newverts != NULL)
    {
        if ((*vtxslot) != rodata->Vertices)
        {
            for (i = 0, offset = 0; i < rodata->numVertices; i++, offset += sizeof(Vertex))
            {
                *((Word4 *) (((u8 *) newverts) + offset)) = *((Word4 *) (((u8 *) (*vtxslot)) + offset));
            }

            sub_GAME_7F09C044(*vtxslot);
        }
        else
        {
            for (i = 0, offset = 0; i < ((0, rodata))->numVertices; i++, offset += sizeof(Vertex))
            {
                *((Word4 *) (((u8 *) newverts) + offset)) = *((Word4 *) (((u8 *) rodata->Vertices) + offset));
            }
        }
        
        *vtxslot = newverts;
    }
    else
    {
        if ((*vtxslot) != rodata->Vertices)
        {
            sub_GAME_7F09C044(*vtxslot);
            *vtxslot = rodata->Vertices;
            obj->runtime_bitflags |= 4;
        }
        
        shrink = 0.85000002f;
        obj->mtx.m[1][0] *= shrink;
        obj->mtx.m[1][1] *= shrink;
        obj->mtx.m[1][2] *= shrink;
        
        if (!adjust_height)
        {
            return;
        }
        
        node = (ModelNode *) obj->model->obj;
        modelscale = obj->model->scale;
        node = (ModelNode *) chrobjGetBboxFromObjFile((ModelFileHeader *) node);
        obj->prop->pos.y += (modelscale * chrpropBBOXGetYmin((ModelRoData_BoundingBoxRecord *) node)) * 0.15000001f;
        obj->runtime_pos.y += (modelscale * chrpropBBOXGetYmin((ModelRoData_BoundingBoxRecord *) node)) * 0.15000001f;
        return;
    }
    
    // The gray desks and wood table are exempt from blackening. DESK2 is not used in the vanilla game.
    if (((obj->obj == PROP_DESK1) || (obj->obj == PROP_DESK2)) || (obj->obj == PROP_WOODEN_TABLE1))
    {
        allow_blackening = FALSE;
    }
    else
    {
        allow_blackening = TRUE;
    }
    
    if (rodata->numVertices > 0)
    {
        i = 0;
        offset = 0;
        
        do
        {
            if (((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y < ymin)
            {
                ymin = ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y;
            }
            
            if (ymax < ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y)
            {
                ymax = ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y;
            }
            
            i++;
            offset += sizeof(Vertex);
            
        } while (offset < (rodata->numVertices << 4));
    }
    
    i = 0;
    ymid = (ymin + ymax) >> 1;

#ifdef VERSION_EU
    if (explosiontype == EXPLOSION_MEDIUM2)
#else
    if ((explosiontype * 2) == EXPLOSION_SMALL)
#endif
    {
        if ((ymid - ymin) >= 41)
        {
            ymid = ymin + 40;
        }
    }

    if ((ymax - ymin) >= 61)
    {
#ifdef VERSION_EU
        if (explosiontype < EXPLOSION_MEDIUM2)
#else
        if ((explosiontype * 2) < EXPLOSION_SMALL)
#endif
        {
            yscale = 0.89999998f;
        }
        else
        {
            yscale = ((f32) ((ymax - ymin) - 60)) / ((f32) (ymax - ymin));
        }
    }
    else
    {
        yscale = 1.0f;
    }

    if (rodata->numVertices <= 0)
    {
        return;
    }
    
    offset = 0;
    
    do
    {
        chrObjRandomSetSeed(((((Vertex *) (((u8 *) rodata->Vertices) + offset))->coord.z + ((Vertex *) (((u8 *) rodata->Vertices) + offset))->coord.x) + ((Vertex *) (((u8 *) rodata->Vertices) + offset))->coord.y) + deformseed);
        chance = 0;
        
        if (allow_blackening)
        {
            if (obj->mtx.m[1][1] >= 0.0f)
            {
                if (((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y >= ymid)
                {
                    if (adjust_height)
                    {
                        chance = 90;
                    }
                    else
                    {
                        chance = 20;
                    }
                }
                else if (adjust_height)
                {
                    chance = 20;
                }
                else
                {
                    chance = 90;
                }
            }
            else if ((ymid ^ 0) >= ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y)
            {
                if (adjust_height)
                {
                    chance = 90;
                }
                else
                {
                    chance = 20;
                }
            }
            else if (adjust_height)
            {
                chance = 20;
            }
            else
            {
                chance = 90;
            }
        }
        else
        {
            chance = 0;
        }
            
        if (1)
        {
            if (((s32) (chrObjRandomGetNext() % 100)) < chance)
            {
                ((Vertex *) (((u8 *) (*vtxslot)) + offset))->r = 0;
                ((Vertex *) (((u8 *) (*vtxslot)) + offset))->g = 0;
                ((Vertex *) (((u8 *) (*vtxslot)) + offset))->b = 0;
                ((Vertex *) (((u8 *) (*vtxslot)) + offset))->a = 255;
            }
            else
#ifdef VERSION_EU
            if (explosiontype == EXPLOSION_BREAK_OBJECT2)
#else
            if ((explosiontype * 2) == EXPLOSION_MEDIUM)
#endif
            {
                ((Vertex *) (((u8 *) (*vtxslot)) + offset))->a = 0;
            }
            
            ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y = (((f32) (((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y - ymin)) * yscale) + ((f32) ymin);
            ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.x += (chrObjRandomGetNext() % 80) - 40;
            ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y += (chrObjRandomGetNext() % 80) - 40;
            ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.z += (chrObjRandomGetNext() % 80) - 40;
                
            if (((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y < ymin)
            {
                ((Vertex *) (((u8 *) (*vtxslot)) + offset))->coord.y = ymin;
            }

            i++;
            offset += sizeof(Vertex);
        }
    } while (i < rodata->numVertices);
}


void objBounce(ObjectRecord *obj, coord3d *arg1)
{
    coord3d dir;
    coord3d rot = {0, 0, 0};
    Projectile *projectile = NULL;

    sub_GAME_7F03FDA8(obj->prop);

    if (obj->runtime_bitflags & RUNTIMEBITFLAG_EMBEDDED) {
        projectile = obj->embedment->projectile;
    } else if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) {
        projectile = obj->projectile;
    }

    if (projectile) {
        projectile->speed.x = (RANDOMFRAC() * 1.6666666f * 4.0f) - 3.3333333f;
        projectile->speed.y = (RANDOMFRAC() * 1.6666666f * 2.0f) + 3.3333333f;
        projectile->speed.z = (RANDOMFRAC() * 1.6666666f * 4.0f) - 3.3333333f;

#ifdef VERSION_EU
        rot.x = (RANDOMFRAC() * 7.53982257843f * 0.015625f) - 0.058904863894f;
        rot.y = (RANDOMFRAC() * 7.53982257843f * 0.015625f) - 0.058904863894f;
        rot.z = (RANDOMFRAC() * 7.53982257843f * 0.015625f) - 0.058904863894f;
#else
        rot.x = (RANDOMFRAC() * M_TAU_F * 0.015625f) - 0.049087387f;
        rot.y = (RANDOMFRAC() * M_TAU_F * 0.015625f) - 0.049087387f;
        rot.z = (RANDOMFRAC() * M_TAU_F * 0.015625f) - 0.049087387f;
#endif

        matrix_4x4_set_rotation_around_xyz((f32*)&rot, &projectile->mtx);

        projectile->flags |= PROJECTILEFLAG_AIRBORNE;

        dir.x = arg1->x;
        dir.y = arg1->y;
        dir.z = arg1->z;

        mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), (f32*)&dir);

        projectile->speed.x += 3.3333333f * dir.x;
        projectile->speed.z += 3.3333333f * dir.z;
        projectile->ownerprop = getCurrentPlayerProp();
        projectile->unk90 = 1;
    }
}


void propobjSetDropped(PropRecord *prop, DROPTYPE droptype)
{
    PropRecord *parent = prop->parent;

    if (parent)
    {
        ObjectRecord *obj = prop->obj;

        sub_GAME_7F03FDA8(prop);

        if ((obj->runtime_bitflags & RUNTIMEBITFLAG_EMBEDDED) && obj->embedment->projectile)
        {
            obj->embedment->projectile->droptype = droptype;
        }
        else if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
        {
            obj->projectile->droptype = droptype;
        }
    }
}


void objDetach(PropRecord *prop)
{
    PropRecord *parent = prop->parent;

    if (parent)
    {
        ObjectRecord *obj = prop->obj;
        Model *model = obj->model;

        chrpropDetach(prop);

        model->attachedto_objinst = NULL;

        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_HASOWNER;

        if (parent->type == PROP_TYPE_CHR || parent->type == PROP_TYPE_VIEWER)
        {
            ChrRecord *chr = parent->chr;

            if (chr)
            {
                if (prop == chr->handle_positiondata_hat)
                {
                    chr->handle_positiondata_hat = NULL;
                }
                else if (prop == chr->weapons_held[GUNRIGHT])
                {
                    chrSetFiring(chr, GUNRIGHT, FALSE);
                    chr->weapons_held[GUNRIGHT] = NULL;
                }
                else if (prop == chr->weapons_held[GUNLEFT])
                {
                    chrSetFiring(chr, GUNLEFT, FALSE);
                    chr->weapons_held[GUNLEFT] = NULL;
                }
            }
        }
    }
}


s32 objDrop(PropRecord *prop)
{
    PropRecord *parent = prop->parent;
    Projectile *projectile;
    ObjectRecord *obj = prop->obj;
    Model *model;
    Mtxf spB8;
    PropRecord *root;
    StandTile* rootstan;

    if ((obj->runtime_bitflags & RUNTIMEBITFLAG_EMBEDDED) && obj->embedment->projectile)
    {
        Projectile* projectile2 = obj->embedment->projectile;
        embedmentFree(obj->embedment);

        obj->projectile = projectile2;
        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_EMBEDDED;
        obj->runtime_bitflags |= RUNTIMEBITFLAG_HASPROJECTILE;
    }

    if (parent && (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE))
    {
        model = obj->model;
        projectile = obj->projectile;
        root = parent;

        projectile->flags |= PROJECTILEFLAG_AIRBORNE;
        projectile->ownerprop = parent;

        if (projectile->droptype == DROPTYPE_SURRENDER && parent->type == PROP_TYPE_CHR)
        {
            ChrRecord* chr = parent->chr;
            Model *chrmodel = chr->model;
            coord3d rot = { 0.0f, 0.0f, 0.0f };
            f32 angle = getsubroty(chrmodel);

            projectile->speed.x = sinf(angle) * 1.6666666f;
            projectile->speed.y = -RANDOMFRAC() * 1.6666666f * 0.5f;
            projectile->speed.z = cosf(angle) *  1.6666666f;

#ifdef VERSION_EU
            rot.x = (RANDOMFRAC() * 7.53982257843f * 0.0078125f) - 0.029452431947f;
            rot.y = (RANDOMFRAC() * 7.53982257843f * 0.0078125f) - 0.029452431947f;
            rot.z = (RANDOMFRAC() * 7.53982257843f * 0.0078125f) - 0.029452431947f;
#else
            rot.x = (RANDOMFRAC() * M_TAU_F * 0.0078125f) - 0.024543693f;
            rot.y = (RANDOMFRAC() * M_TAU_F * 0.0078125f) - 0.024543693f;
            rot.z = (RANDOMFRAC() * M_TAU_F * 0.0078125f) - 0.024543693f;
#endif

            matrix_4x4_set_rotation_around_xyz(rot.f, &projectile->mtx);
        }
        else if (projectile->droptype == DROPTYPE_THROWGRENADE && parent->type == PROP_TYPE_CHR)
        {
            ChrRecord* chr = parent->chr;
            Model *chrmodel = chr->model;
            coord3d rot = { 0.0f, 0.0f, 0.0f };
            f32 angle = getsubroty(chrmodel);

            projectile->speed.x = sinf(angle) * 13.333333f;
            projectile->speed.y = 6.6666665f;
            projectile->speed.z = cosf(angle) * 13.333333f;

#ifdef VERSION_EU
            rot.x = (RANDOMFRAC() * 7.53982257843f * 0.0078125f) - 0.029452431947f;
            rot.y = (RANDOMFRAC() * 7.53982257843f * 0.0078125f) - 0.029452431947f;
            rot.z = (RANDOMFRAC() * 7.53982257843f * 0.0078125f) - 0.029452431947f;
#else
            rot.x = (RANDOMFRAC() * M_TAU_F * 0.0078125f) - 0.024543693f;
            rot.y = (RANDOMFRAC() * M_TAU_F * 0.0078125f) - 0.024543693f;
            rot.z = (RANDOMFRAC() * M_TAU_F * 0.0078125f) - 0.024543693f;
#endif

            matrix_4x4_set_rotation_around_xyz(rot.f, &projectile->mtx);
            projectile->flags |= 0x40;

        }
        else if (projectile->droptype == DROPTYPE_HAT)
        {
            coord3d rot = { 0.0f, 0.0f, 0.0f };
            PropRecord *playerprop = getCurrentPlayerProp();
            f32 x = parent->pos.x - playerprop->pos.x;
            f32 z = parent->pos.z - playerprop->pos.z;
            f32 angle = atan2f(x, z);

            projectile->speed.x = ((2.0f * (RANDOMFRAC() * 1.6666666f)) + 3.3333333f) * sinf(angle);
            projectile->speed.y = 2.0f * (RANDOMFRAC() * 1.6666666f);
            projectile->speed.z = ((2.0f * (RANDOMFRAC() * 1.6666666f)) + 3.3333333f) * cosf(angle);

#ifdef VERSION_EU
            rot.x = (RANDOMFRAC() * 7.53982257843f * 0.03125f) - 0.117809727788f;
            rot.y = (RANDOMFRAC() * 7.53982257843f * 0.03125f) - 0.117809727788f;
            rot.z = (RANDOMFRAC() * 7.53982257843f * 0.03125f) - 0.117809727788f;
#else
            rot.x = (RANDOMFRAC() * M_TAU_F * 0.03125f) - 0.09817477f;
            rot.y = (RANDOMFRAC() * M_TAU_F * 0.03125f) - 0.09817477f;
            rot.z = (RANDOMFRAC() * M_TAU_F * 0.03125f) - 0.09817477f;
#endif

            matrix_4x4_set_rotation_around_xyz(rot.f, &projectile->mtx);
        }
        else
        {
            // DROPTYPE_OWNERREAP ?
            sub_GAME_7F057C14(&projectile->speed, &projectile->mtx);
        }

        while (root->parent != NULL)
        {
            root = root->parent;
        }

        rootstan = root->stan;

        if (prop->flags & PROPFLAG_ONSCREEN)
        {
            // Do collision checks
            f32 objwidth = objGetWidth(obj);
            Mtxf *sp58 = getsubmatrix(model);
            s32 cdtypes = CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER;

            matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), sp58, &spB8);

            if (projectile->flags & 0x40)
            {
                cdtypes = CDTYPE_OBJS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER;
            }

            sub_GAME_7F03D058(root, FALSE);

            if ((stanTestLineUnobstructed(&rootstan, root->pos.f[0], root->pos.f[2], spB8.m[3][0], spB8.m[3][2], cdtypes, 0.0f, 1.0f, 0.0f, 1.0f) != 0)
                && (stanTestVolume(&rootstan, spB8.m[3][0], spB8.m[3][2], objwidth, cdtypes, 0.0f, 1.0f) < 0))
            {
                prop->stan = rootstan;

            }
            else
            {
                prop->stan = root->stan;
                spB8.m[3][0] = root->pos.x;
                spB8.m[3][2] = root->pos.z;
            }

            sub_GAME_7F03D058(root, TRUE);
            prop->zDepth = -sp58->m[3][2];

        }
        else
        {
            // No collision checks
            // Helpful for throwing mines through doors during speedruns
            prop->stan = root->stan;
            matrix_4x4_set_identity(&spB8);
            matrix_scalar_multiply(model->scale, spB8.m[0]);
            matrix_4x4_set_position(&root->pos, &spB8);
        }

        objDetach(prop);
        chrpropActivate(prop);
        chrpropEnable(prop);

        obj->runtime_pos.x = prop->pos.x = spB8.m[3][0];
        obj->runtime_pos.y = prop->pos.y = spB8.m[3][1];
        obj->runtime_pos.z = prop->pos.z = spB8.m[3][2];

        spB8.m[3][0] = 0.0f;
        spB8.m[3][1] = 0.0f;
        spB8.m[3][2] = 0.0f;

        matrix_4x4_copy(&spB8, &obj->mtx);

        sub_GAME_7F0402B4(obj->prop, &obj->nextcol);

        obj->shadecol.r = obj->nextcol.r;
        obj->shadecol.g = obj->nextcol.g;
        obj->shadecol.b = obj->nextcol.b;
        obj->shadecol.a = obj->nextcol.a;

        setupUpdateObjectRoomPosition(obj);

        return TRUE;
    }

    return FALSE;
}


/**
 * Make an object fall. Eg. due to it sitting on a table which is now destroyed,
 * or because it was a chopper that is now destroyed.
 */
void objFall(ObjectRecord *obj, s32 playernum)
{
    obj->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
    obj->runtime_bitflags |= (playernum << RUNTIMEBITSHIFT_OWNER);

    if ((obj->flags2 & PROPFLAG2_NOFALL) == 0
            && (obj->flags & PROPFLAG_RENDERPOSTBG)
            && (obj->runtime_bitflags & (RUNTIMEBITFLAG_EMBEDDED | RUNTIMEBITFLAG_HASPROJECTILE)) == 0)
    {

        coord3d rot = {0, 0, 0};
        Projectile *projectile = NULL;
        s32 unused;

        sub_GAME_7F03FDA8(obj->prop);

        if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
        {
            projectile = obj->projectile;
        }

        if (projectile)
        {
            projectile->speed.x = RANDOMFRAC() * 1.6666666f - 0.8333333f;
            projectile->speed.y = RANDOMFRAC() * 1.6666666f * 2.0f + 1.6666666f;
            projectile->speed.z = RANDOMFRAC() * 1.6666666f - 0.8333333f;

            if ((obj->flags2 & PROPFLAG2_FALLWITHOUTROTATION) == 0)
            {
#ifdef VERSION_EU
                rot.x = ((RANDOMFRAC() * 7.5398226f) / 320.0f) - 0.011780973f;
                rot.y = ((RANDOMFRAC() * 7.5398226f) / 320.0f) - 0.011780973f;
                rot.z = ((RANDOMFRAC() * 7.5398226f) / 320.0f) - 0.011780973f;
#else
                rot.x = ((RANDOMFRAC() * M_TAU_F) / 320.0f) - 0.009817477f;
                rot.y = ((RANDOMFRAC() * M_TAU_F) / 320.0f) - 0.009817477f;
                rot.z = ((RANDOMFRAC() * M_TAU_F) / 320.0f) - 0.009817477f;
#endif
            }

            matrix_4x4_set_rotation_around_xyz(rot.f, &projectile->mtx);

            projectile->flags |= PROJECTILEFLAG_AIRBORNE;

            obj->flags &= ~PROPFLAG_00000100;
            obj->runtime_bitflags &= ~RUNTIMEBITFLAG_00008000;
        }
    }
}


/**
 * Destroy the objects that the given prop is supporting.
 *
 * For example, destroying a table will also destroy all the props that are
 * sitting on that table.
 */
void objDestroySupportedObjects(PropRecord* tableprop, s32 playernum)
{
    ObjectRecord* obj;
    ObjectRecord* tableobj;
    PropRecord* prop;
    rect4f* rect;
    s32 edges;
    u8 room;

    tableobj = tableprop->obj;
    room = tableprop->stan->room;

    chraiGetCollisionBoundsWithoutY(tableprop, &rect, &edges);

    if (edges > 0)
    {
        prop = chrpropGetActiveTail();
        while (prop)
        {
            if (((prop->type == PROP_TYPE_OBJ) || (prop->type == PROP_TYPE_WEAPON)) && (prop->stan->room == room))
            {
                obj = prop->obj;
                if ((tableobj->runtime_pos.y < obj->runtime_pos.y)
                        && ((s32) obj->runtime_bitflags & RUNTIMEBITFLAG_00008000)
                        && (chrpropTestPointInPolygon(&obj->runtime_pos, rect, edges) != 0))
                {
                    objFall(obj, playernum);
                }
            }
            prop = prop->prev;
        }
    }
}


void objExplode(ObjectRecord *obj, coord3d *target_pos, s32 playernum)
{
    PropRecord *prop;
    PropRecord *tailprop;
    s16 explosion_type;
    StandTile *stan;
    s32 shots;

    if (!(obj->damage < obj->maxdamage))
    {
        if (objGetDestroyedLevel(obj) == 0)
        {
            return;
        }
    }

    prop = obj->prop;
#if defined(VERSION_EU)
    explosion_type = ((s8 *) object_explosion_details.typeids)[obj->obj];
#else
    explosion_type = object_explosion_details[obj->obj].TypeID;
#endif
    tailprop = prop;

    if (tailprop->parent != (NULL))
    {
        do
        {
            tailprop = tailprop->parent;
        }
        while (tailprop->parent != NULL);
    }

    stan = tailprop->stan;

    if (objGetDestroyedLevel(obj) == 0)
    {
        ((PropDefHeaderRecord *)obj)->state |= PROPSTATE_DESTROYED;
        obj->maxdamage = 0.0f;

        if (stan != NULL)
        {
            if ((!(tailprop->flags & PROPFLAG_00000008)) && walkTilesBetweenPoints_NoCallback(&stan, tailprop->pos.x, tailprop->pos.z, target_pos->x, target_pos->z))
            {
                explosionCreate(prop, target_pos, stan, explosion_type, (obj->flags & 0xe) == 0, playernum, tailprop->rooms, 0);
            }
            else
            {
                explosionCreate(prop, target_pos, tailprop->stan, explosion_type, 0, playernum, tailprop->rooms, 1);
            }
        }

        if (obj->flags2 & PROPFLAG2_00002000)
        {
            obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
            return;
        }

        objDeform(obj, 1);

        if (tailprop != prop)
        {
            return;
        }

        objDestroySupportedObjects(prop, playernum);

        if (obj->runtime_bitflags & RUNTIMEBITFLAG_00008000)
        {
            if ((randomGetNext() % 3) != 0)
            {
                return;
            }
            if (tailprop && tailprop);
            if(1);
        }

        obj->runtime_bitflags |= RUNTIMEBITFLAG_00010000;
        objFall(obj, playernum);
        return;
    }

    shots = objGetShotsTaken(obj);

    if ((shots & 3) == 0)
    {
        objDeform(obj, (shots >> 2) + 1);

        if (stan != NULL)
        {
            if ((!(tailprop->flags & PROPFLAG_00000008)) && walkTilesBetweenPoints_NoCallback(&stan, tailprop->pos.x, tailprop->pos.z, target_pos->x, target_pos->z))
            {
                explosionCreate(prop, target_pos, stan, 0x10, (obj->flags & (PROPFLAG_ONSCREEN | PROPFLAG_ENABLED | PROPFLAG_00000008)) == 0, playernum, tailprop->rooms, 0);
            }
            else
            {
                explosionCreate(prop, target_pos, tailprop->stan, 0x10, 0, playernum, tailprop->rooms, 1);
            }
        }
    }

    if ((0 < objGetDestroyedLevel(obj)) && (((PropDefHeaderRecord *)obj)->state & PROPSTATE_RESPAWN))
    {
        if (obj->runtime_bitflags & RUNTIMEBITFLAG_00001000)
        {
            ((PropDefHeaderRecord *)obj)->state |= PROPSTATE_10;
        }
        else
        {
            ((PropDefHeaderRecord *)obj)->state &= ~PROPSTATE_10;
        }
#if defined(VERSION_EU)
        prop->timetoregen = 1000;
#else
        prop->timetoregen = 1200;
#endif
    }

    if (shots >= 12)
    {
        obj->runtime_bitflags |= RUNTIMEBITFLAG_00001000;
        if(1);
        obj->flags &= ~RUNTIMEBITFLAG_00000100;
    }
}


BoundVec D_8003204C = {0x7FFF, 0x7FFF, 0x7FFF};
BoundVec D_80032058 = {-0x8000, -0x8000, -0x8000};
coord3d  D_80032064 = {0, 0, 0};
BoundVec D_80032070 = {0x7FFF, 0x7FFF, 0x7FFF};
BoundVec D_8003207C = {-0x8000, -0x8000, -0x8000};
coord3d  D_80032088 = {0, 0, 0};


bool bgTestHitOnObj(coord3d *arg0, coord3d *arg1, coord3d *arg2, Gfx *gdl, Gfx *gdl2, Vertex *vertices, struct HitThing *hitthing)
{
    Vertex *vtxbase;
    HitThing hitbuf;
    Vertex *pt0;
    Vertex *pt1;
    s32 result[1];
    s32 padB[3];
    s32 idx[3];
    s32 padC;
    BoundVec bboxMin;
    BoundVec bboxMax;
    s32 padD[3];
    coord3d zero;
    f32 bestDist;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 d;
    s32 idx2[3];
    Vertex *pt2;
    BoundVec bboxMin2;
    BoundVec bboxMax2;
    Gfx *cmdStart;
    Gfx *tcmd;
    Vertex *v;
    coord3d zero2;
    s32 s2;
    s32 i;
    s32 op;
    s32 texnum;
    
    bestDist = M_U32_MAX_VALUE_F;
    result[0] = 0;
    cmdStart = gdl;

    /* PORT: on PC model display lists are converted native-word streams with
     * host pointers, so the N64's byte-position operand reads never matched and
     * shooting glass found no triangles. The PC code extracts operands with
     * w0/w1 word math instead, and ignores `vertices` because converted G_VTX
     * words are already host pointers. */
    while (1)
    {
#ifdef TARGET_N64
        op = *((s8 *) gdl);
#else
        op = (s8) (gdl->words.w0 >> 24);
#endif
        if (op == (s8)G_ENDDL)
        {
            if (gdl2 == 0)
            {
                break;
            }

            cmdStart = gdl2;

            if (0);

            gdl = gdl2;
            gdl2 = 0;

            continue;
        }

        if (op == G_VTX)
        {
#ifdef TARGET_N64
            op = ((u8 *) gdl)[1] & 0xf;
            padC = ((u32 *) gdl)[1] & 0x00ffffff;
            vtxbase = (Vertex *) ((((s32) vertices) + padC) - (op << 4));
#else
            /* v0 nibble in w0; w1 is usually a host vertex pointer, but some
             * models (e.g. glass) keep segment-relative vertex words. Segment
             * words resolve against the caller's vertex base like the N64 code;
             * host pointers are used directly. */
            op = (gdl->words.w0 >> 16) & 0xf;
            {
                uintptr_t vaddr = gdl->words.w1;

                if (vaddr >= 0x10000000u)
                {
                    vtxbase = (Vertex *) (((u8 *) (uintptr_t) vaddr) - (op << 4));
                }
                else
                {
                    vtxbase = (Vertex *) ((((intptr_t) vertices) + (vaddr & 0x00ffffff)) - (op << 4));
                }
            }
#endif
            gdl++;

            continue;
        }

        if (op == (s8)G_TRI1)
        {
            bboxMin = D_8003204C;
            bboxMax = D_80032058;

#ifdef TARGET_N64
            idx[0] = ((u8 *) gdl)[5] / 10;
            idx[1] = ((u8 *) gdl)[6] / 10;
            idx[2] = ((u8 *) gdl)[7] / 10;
#else
            idx[0] = ((gdl->words.w1 >> 16) & 0xff) / 10;
            idx[1] = ((gdl->words.w1 >> 8) & 0xff) / 10;
            idx[2] = (gdl->words.w1 & 0xff) / 10;
#endif

            for (i = 0; i < 3; i++)
            {
                v = vtxbase;
                v += idx[i];

                if (v->coord.x < bboxMin.x)
                {
                    bboxMin.x = v->coord.x;
                }
                if (bboxMax.x < v->coord.x)
                {
                    bboxMax.x = v->coord.x;
                }
                if (v->coord.y < bboxMin.y)
                {
                    bboxMin.y = v->coord.y;
                }
                if (bboxMax.y < v->coord.y)
                {
                    bboxMax.y = v->coord.y;
                }
                if (v->coord.z < bboxMin.z)
                {
                    bboxMin.z = v->coord.z;
                }
                if (bboxMax.z < v->coord.z)
                {
                    bboxMax.z = v->coord.z;
                }
            }

            if (bgTestRayIntersectsBbox(arg0, arg2, (s32 *) (&bboxMin), (s32 *) (&bboxMax)))
            {
                zero = D_80032064;
                pt0 = vtxbase; pt0 += idx[0]; pt1 = vtxbase; pt1 += idx[1]; pt2 = vtxbase; pt2 += idx[2];

                if (intersectRayTriangle(pt0, pt1, pt2, &zero, arg0, arg1, arg2, &hitbuf))
                {
                    dx = (f32) (((s32) hitbuf.hitpos.x) - ((s32) arg0->x));
                    dy = (f32) (((s32) hitbuf.hitpos.y) - ((s32) arg0->y));
                    dz = (f32) (((s32) hitbuf.hitpos.z) - ((s32) arg0->z));
                    tcmd = gdl;
#ifdef TARGET_N64
                    if (((*((u8 *) gdl)) != 253) && (cmdStart < gdl))
                    {
                        do
                        {
                            tcmd--;

                            if ((*((u8 *) tcmd)) == 253)
                            {
                                break;
                            }
                        } while (cmdStart < tcmd);

                    }

                    if (tcmd == cmdStart)
                    {
                        texnum = -1;
                    }
                    else
                    {
                        padC = ((u32 *) tcmd)[1] - 8;
                        texnum = *((u16 *) (padC | 0x80000000));
                    }
#else
                    /* native opcode compare; w1 = host texture pointer, its
                     * id halfword sits 8 bytes before the pixel data (no K0
                     * mapping on PC) */
                    if (((tcmd->words.w0 >> 24) != G_SETTIMG) && (cmdStart < gdl))
                    {
                        do
                        {
                            tcmd--;

                            if ((tcmd->words.w0 >> 24) == G_SETTIMG)
                            {
                                break;
                            }
                        } while (cmdStart < tcmd);
                    }

                    if (tcmd == cmdStart || (tcmd->words.w0 >> 24) != G_SETTIMG)
                    {
                        texnum = -1;
                    }
                    else
                    {
                        /* Some textures overridden at draw time (e.g. glass)
                         * keep a raw segment word instead of a host pointer.
                         * Use the default material unless it is a real pointer
                         * or an IMAGESEG tag (0xABCDxxxx). */
                        uintptr_t timg = tcmd->words.w1;

                        if (timg >= 0x10000000u && (timg >> 16) != 0xABCDu)
                        {
                            texnum = *((u16 *) (uintptr_t) (timg - 8));
                        }
                        else
                        {
                            texnum = -1;
                        }
                    }
#endif

                    d = ((dx * dx) + (dy * dy)) + (dz * dz);

                    if (d < bestDist)
                    {
                        bestDist = d;
                        hitthing->hitpos.x = hitbuf.hitpos.x;
                        hitthing->hitpos.y = hitbuf.hitpos.y;
                        hitthing->hitpos.z = hitbuf.hitpos.z;
                        hitthing->normal.x = hitbuf.normal.x;
                        hitthing->normal.y = hitbuf.normal.y;
                        hitthing->normal.z = hitbuf.normal.z;
                        hitthing->vtx0 = &vtxbase[idx[0]];
                        hitthing->vtx1 = &vtxbase[idx[1]];
                        hitthing->vtx2 = &vtxbase[idx[2]];
                        hitthing->texturenum = texnum;
                        hitthing->tricmd = gdl;
                        hitthing->unk28 = 0;
                        result[0] = 1;
                    }
                }
            }
        }
        else if (op == (s8)G_TRI4)
        {
            s2 = 0;
            do
            {
                bboxMin2 = D_80032070;
                bboxMax2 = D_8003207C;

#ifdef TARGET_N64
                if (s2 == 0)
                {
                    idx2[0] = ((u32 *) gdl)[1] & 0xf;
                    idx2[1] = ((u32) ((u8 *) gdl)[7]) >> 4;
                    idx2[2] = ((u32 *) gdl)[0] & 0xf;
                }
                else if (s2 == 1)
                {
                    idx2[0] = ((u8 *) gdl)[6] & 0xf;
                    idx2[1] = ((u32) ((u16 *) gdl)[3]) >> 12;
                    idx2[2] = ((u32) ((u8 *) gdl)[3]) >> 4;
                }
                else if (s2 == 2)
                {
                    idx2[0] = ((u16 *) gdl)[2] & 0xf;
                    idx2[1] = ((u32) ((u8 *) gdl)[5]) >> 4;
                    idx2[2] = ((u8 *) gdl)[2] & 0xf;
                }
                else
                {
                    idx2[0] = ((u8 *) gdl)[4] & 0xf;
                    idx2[1] = ((u32 *) gdl)[1] >> 28;
                    idx2[2] = ((u32) ((u16 *) gdl)[1]) >> 12;
                }
#else
                /* same nibbles via word math (BE byte/halfword positions
                 * mapped onto native w0/w1) */
                if (s2 == 0)
                {
                    idx2[0] = gdl->words.w1 & 0xf;
                    idx2[1] = (gdl->words.w1 >> 4) & 0xf;
                    idx2[2] = gdl->words.w0 & 0xf;
                }
                else if (s2 == 1)
                {
                    idx2[0] = (gdl->words.w1 >> 8) & 0xf;
                    idx2[1] = (gdl->words.w1 >> 12) & 0xf;
                    idx2[2] = (gdl->words.w0 >> 4) & 0xf;
                }
                else if (s2 == 2)
                {
                    idx2[0] = (gdl->words.w1 >> 16) & 0xf;
                    idx2[1] = (gdl->words.w1 >> 20) & 0xf;
                    idx2[2] = (gdl->words.w0 >> 8) & 0xf;
                }
                else
                {
                    idx2[0] = (gdl->words.w1 >> 24) & 0xf;
                    idx2[1] = gdl->words.w1 >> 28;
                    idx2[2] = (gdl->words.w0 >> 12) & 0xf;
                }
#endif

                for (i = 0; i < 3; i++)
                {
                    v = vtxbase;
                    v += idx2[i];

                    if (v->coord.x < bboxMin2.x)
                    {
                        bboxMin2.x = v->coord.x;
                    }
                    if (bboxMax2.x < v->coord.x)
                    {
                        bboxMax2.x = v->coord.x;
                    }
                    if (v->coord.y < bboxMin2.y)
                    {
                        bboxMin2.y = v->coord.y;
                    }
                    if (bboxMax2.y < v->coord.y)
                    {
                        bboxMax2.y = v->coord.y;
                    }
                    if (v->coord.z < bboxMin2.z)
                    {
                        bboxMin2.z = v->coord.z;
                    }
                    if (bboxMax2.z < v->coord.z)
                    {
                        bboxMax2.z = v->coord.z;
                    }
                }

                if (bgTestRayIntersectsBbox(arg0, arg2, (s32 *) (&bboxMin2), (s32 *) (&bboxMax2)))
                {
                    zero2 = D_80032088;
                    pt0 = vtxbase; pt0 += idx2[0]; pt1 = vtxbase; pt1 += idx2[1]; pt2 = vtxbase; pt2 += idx2[2];

                    if (intersectRayTriangle(pt0, pt1, pt2, &zero2, arg0, arg1, arg2, &hitbuf))
                    {
                        dx = (f32) (((s32) hitbuf.hitpos.x) - ((s32) arg0->x));
                        dy = (f32) (((s32) hitbuf.hitpos.y) - ((s32) arg0->y));
                        dz = (f32) (((s32) hitbuf.hitpos.z) - ((s32) arg0->z));
                        tcmd = gdl;

#ifdef TARGET_N64
                        if (((*((u8 *) gdl)) != G_SETTIMG) && (cmdStart < gdl))
                        {
                            do
                            {
                                tcmd--;
                                if ((*((u8 *) tcmd)) == G_SETTIMG)
                                {
                                    break;
                                }
                            } while (cmdStart < tcmd);

                        }

                        if (tcmd == cmdStart)
                        {
                            texnum = -1;
                        }
                        else
                        {
                            padC = ((u32 *) tcmd)[1] - 8;
                            texnum = *((u16 *) (padC | 0x80000000));
                        }
#else
                        /* see the G_TRI1 twin above */
                        if (((tcmd->words.w0 >> 24) != G_SETTIMG) && (cmdStart < gdl))
                        {
                            do
                            {
                                tcmd--;
                                if ((tcmd->words.w0 >> 24) == G_SETTIMG)
                                {
                                    break;
                                }
                            } while (cmdStart < tcmd);
                        }

                        if (tcmd == cmdStart || (tcmd->words.w0 >> 24) != G_SETTIMG)
                        {
                            texnum = -1;
                        }
                        else
                        {
                            /* see the G_TRI1 twin above */
                            uintptr_t timg = tcmd->words.w1;

                            if (timg >= 0x10000000u && (timg >> 16) != 0xABCDu)
                            {
                                texnum = *((u16 *) (uintptr_t) (timg - 8));
                            }
                            else
                            {
                                texnum = -1;
                            }
                        }
#endif

                        d = ((dx * dx) + (dy * dy)) + (dz * dz);

                        if (d < bestDist)
                        {
                            bestDist = d;
                            hitthing->hitpos.x = hitbuf.hitpos.x;
                            hitthing->hitpos.y = hitbuf.hitpos.y;
                            hitthing->hitpos.z = hitbuf.hitpos.z;
                            hitthing->normal.x = hitbuf.normal.x;
                            hitthing->normal.y = hitbuf.normal.y;
                            hitthing->normal.z = hitbuf.normal.z;
                            hitthing->vtx0 = &vtxbase[idx2[0]];
                            hitthing->vtx1 = &vtxbase[idx2[1]];
                            hitthing->vtx2 = &vtxbase[idx2[2]];
                            hitthing->texturenum = texnum;
                            hitthing->tricmd = gdl;
                            hitthing->unk28 = s2 + 1;
                            result[0] = 1;
                        }
                    }
                }
                s2++;
            } while (s2 != 4);
        }
        gdl++;
    }
 
    return result[0];
}


// PD: obj_find_hitthing_by_gfx_tris
bool propobjFindHit(Model *model, ModelNode *startNode, coord3d *rayPos, coord3d *rayDir, struct HitThing *hitthing, s32 *dstmtxindex, ModelNode **dstnode)
{
    coord3d spec;
    coord3d spe0;
    coord3d spd4;
    Mtxf *spd0;
    bool done;
    ModelNode *node;
    Vertex *vertices;

    spd0 = NULL;
    done = FALSE;
    node = startNode;
    vertices = NULL;

    while (node && !done)
    {
        u32 type = node->Opcode & 0xff;
        Gfx *s3 = NULL;
        void *s5 = NULL;

        switch (type)
        {
            case MODELNODE_OPCODE_DLCOLLISION:
                {
                    ModelRoData_DisplayList_CollisionRecord *rodata = &node->Data->DisplayListCollisions;
                    ModelRwData_DisplayList_CollisionRecord *rwdata = modelGetNodeRwData(model, node);

                    if (rwdata->gdl != NULL)
                    {
#ifdef TARGET_N64
                        if (rwdata->gdl == rodata->Primary)
                        {
                            s3 = (Gfx *)((uintptr_t)rodata->BaseAddr + ((u32)rodata->Primary & 0xffffff));
                        }
                        else
                        {
                            s3 = rwdata->gdl;
                        }

                        if (rodata->Secondary != NULL)
                        {
                            s5 = (void *)((uintptr_t)rodata->BaseAddr + ((u32)rodata->Secondary & 0xffffff));
                        }
#else
                        /* PORT: Primary/Secondary already hold converted host GDL pointers,
                         * which the N64's base + low-24-bits rebuild would
                         * mangle (see the matching fix in front.c). */
                        s3 = rwdata->gdl;

                        if (rodata->Secondary != NULL)
                        {
                            s5 = rodata->Secondary;
                        }
#endif

                        vertices = rwdata->Vertices;
                    }
                }
                break;

            case MODELNODE_OPCODE_DL:
                {
                    ModelRoData_DisplayListRecord *rodata = &node->Data->DisplayList;

                    if (rodata->Primary != NULL)
                    {
#ifdef TARGET_N64
                        s3 = (Gfx *)((uintptr_t)rodata->BaseAddr + ((u32)rodata->Primary & 0xffffff));

                        if (rodata->Secondary != NULL)
                        {
                            s5 = (Gfx *)((uintptr_t)rodata->BaseAddr + ((u32)rodata->Secondary & 0xffffff));
                        }
#else
                        /* PORT: as above - the converted pointers ARE the DLs */
                        s3 = rodata->Primary;

                        if (rodata->Secondary != NULL)
                        {
                            s5 = rodata->Secondary;
                        }
#endif

                        vertices = (void *)(uintptr_t)rodata->BaseAddr;
                    }
                }
                break;

            case MODELNODE_OPCODE_LOD:
                modelApplyDistanceRelations(model, node);
                break;

            case MODELNODE_OPCODE_SWITCH:
                modelApplyToggleRelations(model, node);
                break;

            case MODELNODE_OPCODE_HEAD:
                modelApplyHeadRelations(model, node);
                break;
        }

        if (s3 != NULL)
        {
            s32 mtxindex = modelFindNodeMtxIndex(node, 0);
            Mtxf *mtx = NULL;
            Mtxf sp64;

            if (mtxindex >= 0)
            {
                mtx = (Mtxf *)&model->render_pos[mtxindex]; // TODO: adjust
            }

            if (mtx && mtx != spd0)
            {
                spd0 = mtx;

                matrix_4x4_invert_affine(mtx->m, sp64.m);

                spec.x = rayPos->x;
                spec.y = rayPos->y;
                spec.z = rayPos->z;

                mtx4TransformVecInPlace(&sp64, &spec);

                spd4.x = rayDir->x;
                spd4.y = rayDir->y;
                spd4.z = rayDir->z;

                mtx4RotateVecInPlace(&sp64, &spd4);

                spe0.x = spd4.x * 32767.0f + spec.x;
                spe0.y = spd4.y * 32767.0f + spec.y;
                spe0.z = spd4.z * 32767.0f + spec.z;
            }

            if (bgTestHitOnObj(&spec, &spe0, &spd4, s3, s5, vertices, hitthing))
            {
                *dstmtxindex = mtxindex;
                *dstnode = node;
                done = TRUE;
            }
        }

        if (node->Child)
        {
            node = node->Child;
        } else {
            while (node)
            {
                if (node == startNode)
                {
                    node = NULL;
                    break;
                }

                if (node->Next)
                {
                    node = node->Next;
                    break;
                }

                node = node->Parent;
            }
        }
    }

    return done;
}


void sub_GAME_7F04DCB4(ObjectRecord* obj)
{
    PropRecord* prop;
    struct ModelRoData_BoundingBoxRecord *bbox;

    prop = obj->prop;
    bbox = chrobjGetBboxFromObjectRecord(obj);
    explosionClearBulletImpactRoomByFlag(prop, FALSE);
    explosionClearBulletImpactRoomByFlag(prop, TRUE);

    sub_GAME_7F0A1DA0(&obj->runtime_pos.f[0],
        &obj->mtx.m[0][0], &obj->mtx.m[1][0], &obj->mtx.m[2][0],
        bbox->Bounds.xmin, bbox->Bounds.xmax,
        bbox->Bounds.ymin, bbox->Bounds.ymax,
        bbox->Bounds.zmin, bbox->Bounds.zmax);

    obj->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
    obj->state |= PROPSTATE_DESTROYED;
    obj->maxdamage = 0.0f;
}


/**
 * Address: 7F04DD68
 */
void sub_GAME_7F04DD68(DoorRecord *door)
{
    PropRecord *prop;
    Model *model;
    struct ModelRoData_BoundingBoxRecord *bbox;
    struct ModelRwData_SwitchRecord *switchdata;
    Mtxf mtx;

    prop = door->prop;
    model = door->model;
    bbox = (struct ModelRoData_BoundingBoxRecord *) model->obj->Switches[2]->Data;

    door7F0526EC(door, &mtx);
    sub_GAME_7F0A1DA0(&mtx.m[3][0], &mtx.m[0][0], &mtx.m[1][0], &mtx.m[2][0], bbox->Bounds.xmin, bbox->Bounds.xmax, bbox->Bounds.ymin, bbox->Bounds.ymax, bbox->Bounds.zmin, bbox->Bounds.zmax);

    explosionClearBulletImpactRoomByFlag(prop, 1);

    switchdata = (struct ModelRwData_SwitchRecord *)modelGetNodeRwData(model, model->obj->Switches[1]);
    switchdata->visible = FALSE;
}


/**
 * Address: 7F04DE18
 */
void objBreakCCTVGlass(ObjectRecord *obj)
{
    PropRecord *prop;
    s32 unused[2];
    union ModelRoData *rodata;
    Mtxf *node_mtx;
    Mtxf glassNodeWorldMtx;
    Model *model;

    prop = obj->prop;
    model = obj->model;

    if (prop->flags & PROPFLAG_ONSCREEN)
    {
        rodata = model->obj->Switches[2]->Data;

        node_mtx = modelFindNodeMtx(model, model->obj->Switches[1], 0);

        matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), node_mtx, &glassNodeWorldMtx);

        sub_GAME_7F0A1DA0(glassNodeWorldMtx.m[3], glassNodeWorldMtx.m[0], glassNodeWorldMtx.m[1], glassNodeWorldMtx.m[2], ((f32 *)rodata)[1], ((f32 *)rodata)[2], ((f32 *)rodata)[3], ((f32 *)rodata)[4], ((f32 *)rodata)[5], ((f32 *)rodata)[6]);
    }

    explosionClearBulletImpactRoomByFlag(prop, 1);

    *(s32 *)modelGetNodeRwData(model, model->obj->Switches[3]) = 0;
}


/**
 * Address 0x7F04DEFC.
*/
void maybe_detonate_object_and_its_children(PropRecord *prop, f32 damage, struct coord3d *pos, s32 arg3, s32 owner)
{
    PropRecord *node;
    ObjectRecord *prop_obj;

    prop_obj = prop->obj;

    prop_obj->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
    prop_obj->runtime_bitflags |= (owner << RUNTIMEBITSHIFT_OWNER);

    if ((s32)(prop_obj->runtime_bitflags << 0xc) >= 0)
    {
        node = prop->child;
        while (node != NULL)
        {
            PropRecord *iter_next = node->prev;
            // recursive call:
            maybe_detonate_object_and_its_children(node, damage, pos, arg3, owner);
            node = iter_next;
        }

        objApplyDamage(prop->obj, damage, pos, arg3, owner);
    }
}





bool check_if_destroyable_object_type(PropDefHeaderRecord *obj)//#MATCH
{
    switch (obj->type)
    {
        case PROPDEF_DOOR:
        case PROPDEF_PROP:
        case PROPDEF_ALARM:
        case PROPDEF_CCTV:
        case PROPDEF_MONITOR:
        case PROPDEF_MULTI_MONITOR:
        case PROPDEF_RACK:
        case PROPDEF_AUTOGUN:
        case PROPDEF_GAS_RELEASING:
        case PROPDEF_VEHICHLE:
        case PROPDEF_AIRCRAFT:
        case PROPDEF_UNK41:
        case PROPDEF_GLASS:
        case PROPDEF_SAFE:
        case PROPDEF_TANK:
        case PROPDEF_TINTED_GLASS:
            return TRUE;
        default:
            return FALSE;
    }
}


bool objIsCollectable(PropDefHeaderRecord *obj)
{
    switch (obj->type)
    {
        case PROPDEF_KEY:
        case PROPDEF_MAGAZINE:
        case PROPDEF_COLLECTABLE:
        case PROPDEF_HAT:
        case PROPDEF_AMMO:
        case PROPDEF_ARMOUR:
            return TRUE;
        default:
            return FALSE;
    }
}


bool objIsMortal(ObjectRecord* obj)
{
    if (obj->type == PROPDEF_DOOR)
    {
        return FALSE;
    }
    if ((objIsCollectable((PropDefHeaderRecord* ) obj) != 0) && (obj->type != PROPDEF_ARMOUR))
    {
        if (!(obj->flags & PROPFLAG_FORCEMORTAL))
        {
            return FALSE;
        }
    }
    else if (obj->flags & PROPFLAG_INVINCIBLE)
    {
        return FALSE;
    }
    return TRUE;
}


/**
 * Address 0x7F04E0CC.
*/
void chrobjMaybeDetonateObjectIfFlags(ObjectRecord *obj, f32 damage, coord3d *pos, ITEM_IDS item, s32 owner)
{
    if ((obj->flags2 & 0x4000) == 0)
    {
        objApplyDamage(obj, damage, pos, item, owner);
    }
}


ObjectRecord blank_07_object = {
    0x0100, //extrascale
    0x0, //state
    0x07, //type
    0, //obj
    0xFFFF, //pad
    0x00000001, //flags
    0, //flags2
    NULL, //prop
    NULL, //model
    {
       1.0f, 0.0f, 0.0f, 0.0f,
       0.0f, 1.0f, 0.0f, 0.0f,
       0.0f, 0.0f, 1.0f, 0.0f,
       0.0f, 0.0f, 0.0f, 1.0f
    }, //mtx
    {0.0, 0.0, 0.0},//runtime_pos
    {0x00000000}, //runtime_bitflags
    NULL, //ptr_allocated_collisiondata_block
    NULL, //projectile/embedment
    0.0f, //maxdamage
    1000.0f, //damage
    {0xFF, 0xFF, 0xFF, 0x00}, //shadecol
    {0xFF, 0xFF, 0xFF, 0x00}, //nextcol
};


// Formerly maybe_detonate_object
#ifndef TARGET_N64
/* GE_DMGDBG=1: one line per damage event, naming the check that ignored it.
 * The three ways a prop can shrug off a hit look the same in game:
 *   mortal=0        - objIsMortal said no (INVINCIBLE, a collectable
 *                     without FORCEMORTAL, or a door)
 *   cap huge        - obj->damage is the capacity from the setup file, read
 *                     as a 16.16 word; a byte-order mistake here makes props
 *                     one-shot or immortal
 *   no line at all  - the shot never reached this function: the hit test
 *                     missed
 * NOTE the field names read backwards: obj->damage is the capacity, and
 * obj->maxdamage accumulates the damage taken. */
void portDmgDbg(ObjectRecord *obj, f32 damage, ITEM_IDS itemnum, const char *stage)
{
    extern char *getenv(const char *);
    extern int printf(const char *, ...);
    static s32 dbg = -1;

    if (dbg < 0)
    {
        dbg = getenv("GE_DMGDBG") != NULL;
    }
    if (!dbg || obj == NULL)
    {
        return;
    }
    {
        extern u32 g_PortFramesPumped; /* frame stamp: lets a screenshot run be
                                        * correlated to the exact damage event */
        printf("ge: dmg: f=%u ", g_PortFramesPumped);
    }
    printf("%-8s type=%d model=%d item=%d hit=%.1f taken=%.1f cap=%.1f "
           "flags=%08x flags2=%08x mortal=%d destroyed=%d\n",
           stage, obj->type, obj->obj, (s32) itemnum, damage,
           obj->maxdamage, obj->damage,
           (u32) obj->flags, (u32) obj->flags2,
           objIsMortal(obj), objGetDestroyedLevel(obj));
}
#endif

void objApplyDamage(ObjectRecord *obj, f32 damage, coord3d *pos, ITEM_IDS itemnum, s32 playernum)
{
    s32 flags;
    s32 shotsmod;
    f32 maxdamage;
    PropRecord *child;
    MultiAmmoCrateRecord *slotview;
    s32 startslot;
    s32 slot;
    AmmoCrateRecord *crate;
    PropRecord *nextchild;
    ObjectRecord tmpobj;
    
    /**
     * Set the OWNER field (bits 17-18) to the player index; clear-then-set, equivalent to obj->owner = playernum.
     * Used to record which player is responsible for the damage.
     */ 
    obj->runtime_bitflags &= ~RUNTIMEBITFLAG_OWNER;
    obj->runtime_bitflags |= playernum << RUNTIMEBITSHIFT_OWNER;

#ifndef TARGET_N64
    portDmgDbg(obj, damage, itemnum, "enter");
#endif

    if (obj->type == PROPDEF_GAS_RELEASING)
    {
        if (objGetDestroyedLevel(obj) == 1)
        {
            return;
        }
    }

    if (itemnum == ITEM_UNARMED)
    {
        if (objIsCollectable((PropDefHeaderRecord *)obj))
        {
            // Equivalent to: if (!(obj->flags & PROPFLAG_00800000)) keep like this for matching
            if ((s32)(obj->flags << 8) >= 0)
            {
                return;
            }
            goto apply_damage;
        }

        // Equivalent to: if (obj->flags & PROPFLAG_01000000) keep like this for matching
        if ((s32)(obj->flags << 7) < 0)
        {
            return;
        }

        goto apply_damage;
    }

    flags = obj->flags;

    if (flags & PROPFLAG_INVINCIBLE)
    {
        return;
    }

    if (obj->type == PROPDEF_COLLECTABLE)
    {
        s32 weaponnum = ((WeaponObjRecord *)obj)->weaponnum;

        // For these items, if they take damage, set their timers to 0, effectively causing them to immediately detonate.
        if (weaponnum == ITEM_GRENADE
                || weaponnum == ITEM_TIMEDMINE
                || weaponnum == ITEM_REMOTEMINE
                || weaponnum == ITEM_PROXIMITYMINE
                || weaponnum == ITEM_ROCKETROUND
                || weaponnum == ITEM_GRENADEROUND
                || weaponnum == ITEM_BOMBCASE
                || weaponnum == ITEM_PLASTIQUE)
        {
            ((WeaponObjRecord *)obj)->timer = 0;
        }

        return;
    }

    if (obj->type == PROPDEF_MAGAZINE)
    {
        s32 ammotype = ((AmmoCrateRecord *)obj)->ammoType;

        // slot and startslot are double purposed here for matching.
        slot = AMMO_REMOTEMINE;
        startslot = AMMO_PROXMINE;

        if (ammotype == AMMO_GRENADE
                || ammotype == AMMO_ROCKETS
                || slot == ammotype
                || startslot == ammotype
                || ammotype == AMMO_TIMEDMINE
                || ammotype == AMMO_GRENADEROUND
                || ammotype == AMMO_EXPLOSIVEPEN
                || ammotype == AMMO_BOMBCASE
                || ammotype == AMMO_DYNAMITE)
        {
            obj->flags = flags | 0x10000000;
        }

        return;
    }

    if (!objIsMortal(obj))
    {
#ifndef TARGET_N64
        portDmgDbg(obj, damage, itemnum, "IMMORTAL");
#endif
        return;
    }

apply_damage:
    // If the object is not yet destroyed, accumulate damage into the object's maxdamage field.
    if (objGetDestroyedLevel(obj) == 0)
    {
        obj->maxdamage += damage * 250.0f; 
    }
    /**
     * If already destroyed, further damage is based on number of shots taken, not how much damage is taken.
     * maxdamage is repurposed to to hold the number of shots the object has taken since it reached the current destroyed level.
     * When 4 shots are taken the object moves to the next destroyed level.
     */
    else
    {
        shotsmod = objGetShotsTaken(obj) % 4;
        maxdamage = (f32)(4 - shotsmod);

        if (maxdamage < damage)
        {
            damage = maxdamage;
        }
        else if (damage < 1.0f)
        {
            damage = 1.0f;
        }

        obj->maxdamage += damage;
    }

    if (obj->type == PROPDEF_GLASS || obj->type == PROPDEF_TINTED_GLASS)
    {
        if (obj->damage <= obj->maxdamage)
        {
            sub_GAME_7F04DCB4(obj);
        }
    }
    else
    {
        propobjSetDropped(obj->prop, DROPTYPE_DEFAULT);
        objExplode(obj, pos, playernum);
    }

    // What is this?
    if (obj->type == PROPDEF_AMMO)
    {
        if (objGetDestroyedLevel(obj) == 1)
        {
            startslot = randomGetNext() % AMMOTYPE_GLOBAL_MAX;
            slot = startslot;

            do
            {
                s32 off = slot * sizeof(struct multiammocrateslot);
                slotview = (MultiAmmoCrateRecord *)((u8 *)obj + off);

                if (slotview->slots[0].quantity > 0 && slotview->slots[0].modelnum != 0xffff)
                {
                    crate = ammocrateAllocate();

                    if (crate != NULL)
                    {
                        u16 modelnum = slotview->slots[0].modelnum;

                        tmpobj = blank_07_object;
                        *(ObjectRecord *)crate = tmpobj;

                        // Matching trick.
                        slotview++;
                        slotview--;

                        crate->obj = modelnum;
                        crate->ammoType = slot + 1;

                        if (crate->ammoType == AMMO_9MM_2)
                        {
                            crate->ammoType = AMMO_9MM;
                        }

                        if (objInitWithModelDef((ObjectRecord *)crate, PitemZ_entries[modelnum].header) != NULL)
                        {
                            modelSetScale(crate->model, crate->model->scale);
                            chrpropReparent(crate->prop, obj->prop);
                        }

                        break;
                    }
                }

                slot++;
                slot %= AMMOTYPE_GLOBAL_MAX;
            }
            while (slot != startslot);
        }
    }

    /**
     * For drone guns, CCTV cameras, and monitors, cease unique functionality when destroyed, set monitors to black.
     */
    if (obj->type == PROPDEF_AUTOGUN)
    {
        obj->flags |= PROPFLAG_NO_AMMO;

        if (objGetDestroyedLevel(obj) == 1)
        {
            obj->flags |= PROPFLAG_IS_DRONE_GUN;
        }
    }
    else if (obj->type == PROPDEF_CCTV)
    {
        if (objGetDestroyedLevel(obj) == 1)
        {
            obj->flags |= PROPFLAG_CCTV_DISABLED;
        }
    }
    else if (obj->type == PROPDEF_MONITOR)
    {
        if (objGetDestroyedLevel(obj) == 1)
        {
            save_ptr_monitor_ani_code_to_obj_ani_slot(&((MonitorObjRecord *)obj)->Monitor, monAnim33BlackSolid);
        }
    }
    else if (obj->type == PROPDEF_MULTI_MONITOR)
    {
        if (objGetDestroyedLevel(obj) == 1)
        {
            save_ptr_monitor_ani_code_to_obj_ani_slot(&((MultiMonitorObjRecord *)obj)->Monitor[0], monAnim33BlackSolid);
            save_ptr_monitor_ani_code_to_obj_ani_slot(&((MultiMonitorObjRecord *)obj)->Monitor[1], monAnim33BlackSolid);
            save_ptr_monitor_ani_code_to_obj_ani_slot(&((MultiMonitorObjRecord *)obj)->Monitor[2], monAnim33BlackSolid);
            save_ptr_monitor_ani_code_to_obj_ani_slot(&((MultiMonitorObjRecord *)obj)->Monitor[3], monAnim33BlackSolid);
        }
    }
    else if (obj->type == PROPDEF_GAS_RELEASING)
    {
        if (objGetDestroyedLevel(obj) == 1)
        {
            init_trigger_toxic_gas_effect(&obj->runtime_pos);
        }
    }
    // Damage done to an armor gets subtracted from the amount of armor it provides when picked up.
    else if (obj->type == PROPDEF_ARMOUR)
    {
        if (objGetDestroyedLevel(obj) == 0)
        {
            ((BodyArmourRecord *)obj)->amount = ((BodyArmourRecord *)obj)->initialamount
                * (obj->damage - obj->maxdamage) / obj->damage;
        }
        else
        {
            ((BodyArmourRecord *)obj)->amount = 0.0f;
        }
    }

    // If an object is destroyed drop its children, for example the monitor holders in Bunker.
    if (objGetDestroyedLevel(obj) == 1)
    {
        child = obj->prop->child;

        while (child != NULL)
        {
            PropRecord *nextchild = child->prev;

            propobjSetDropped(child, DROPTYPE_DEFAULT);
            child = nextchild;
        }
    }
}


/**
 * This is the former non-matching block for sub_GAME_7F04E720. These asserts belong somewhere in sub_GAME_7F04E720
 *
 *   void sub_GAME_7F04E720(PropRecord* prop, struct ShotData* hitinfo) {
 *      mtx4TransformVecInPlace(?, hitinfo.hitpos);
 *  assert(!IsBadVec3d( (vec3d*)hitinfo.hitpos  );
 *  mtx4TransformVecInPlace(?, hitinfo.normal);
 *         assert(!IsBadVec3d( (vec3d*)hitinfo.normal  );
 * }
 */

/**
 * Address: 7F04E720
 */
void sub_GAME_7F04E720(PropRecord *prop, struct ShotData *hitinfo)
{
    ObjectRecord *obj = prop->obj;
    Model *model;
    PropRecord *child;
    PropRecord *next;
    s32 hitpart;
    ModelNode *node;
    HitThing hit;
    s32 mtxindex;
    coord3d pos;
    s8 g[4];
    ModelNode *hitnode;

    if (hit.hitpos.z);

    if (((obj->runtime_bitflags & RUNTIMEBITFLAG_00001000) == FALSE) && (prop->flags & PROPFLAG_ONSCREEN))
    {
        // Dummy goto is needed for matching.
        goto dummy_label_995911;

dummy_label_995911:
        child = prop->child;

        while (child != NULL)
        {
            next = child->prev;
            sub_GAME_7F04E720(child, hitinfo);
            child = next;
        }

        model = obj->model;
        node = NULL;

        if ((obj->type == PROPDEF_DOOR) && (((((DoorRecord *) obj)->doorFlags & DOORFLAG_CLIP_TO_BBOX) || (((DoorRecord *) obj)->doorType == DOORTYPE_EYE)) || (((DoorRecord *) obj)->doorType == DOORTYPE_IRIS)))
        {
            hitpart = modelTestRayIntersectsTransformedBBox(&((DoorRecord *) obj)->bbox, &model->render_pos->pos, &hitinfo->viewOrigin, &hitinfo->viewDir);
            node = model->obj->RootNode;

            if (hitpart > HIT_NULL_PART)
            {
                if (!propobjFindHit(model, node, &hitinfo->viewOrigin, &hitinfo->viewDir, &hit, &mtxindex, &hitnode))
                {
                    hitpart = HIT_NULL_PART;
                }
            }
        }
        else
        {
            do
            {
                hitpart = modelFindNextProjectileHitCandidate(model, &hitinfo->viewOrigin, &hitinfo->viewDir, &node);

                if (hitpart > HIT_NULL_PART)
                {
                    if (propobjFindHit(model, node, &hitinfo->viewOrigin, &hitinfo->viewDir, &hit, &mtxindex, &hitnode))
                    {
                        break;
                    }
                }
            }
            while (hitpart > HIT_NULL_PART);
        }

        if (hitpart > HIT_NULL_PART)
        {
            bool penetrates;
            Mtxf *mtx;

            mtx = &model->render_pos[mtxindex].pos;
            pos.x = hit.hitpos.x;
            pos.y = hit.hitpos.y;
            pos.z = hit.hitpos.z;

            mtx4TransformVecInPlace(mtx, &pos);

            if ((-pos.z) <= hitinfo->maxdist)
            {
                penetrates = TRUE;

                if ((obj->flags & PROPFLAG2_00020000) == FALSE)
                {
                    if ((obj->type == PROPDEF_GLASS) || (obj->type == PROPDEF_TINTED_GLASS))
                    {
                        penetrates = FALSE;
                    }
                    else if ((obj->model->obj->Skeleton == (&skeleton_door)) && (hitnode == obj->model->obj->Switches[3]))
                    {
                        penetrates = FALSE;
                    }
                }

                chrpropAddBulletHit(hitinfo, prop, -pos.z, hitpart, node, &hit, mtxindex, hitnode, model, penetrates, (obj->flags2 & PROPFLAG2_00100000) != FALSE);
            }
        }
    }
}


// PD: obj_test_hit
void sub_GAME_7F04E9BC(PropRecord* prop, struct ShotData* shotdata)
{
    ObjectRecord *obj;
    f32 tmp;
    Model *model;
    struct ModelRoData_BoundingBoxRecord *bbox;

    obj = prop->obj;
    model = obj->model;
    bbox = chrobjGetBboxFromObjectRecord(obj);

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): the shooting-glass hunt - log every
       * glass prop offered to a shot and which gate passes/blocks it, so
       * one aimed shot in a real session names the failing stage. */
        static int dump = -1;
        if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
        if (dump && (obj->type == PROPDEF_GLASS || obj->type == PROPDEF_TINTED_GLASS))
        {
            fprintf(stderr, "glasstest: type=%d onscreen=%d rtb1000=%d shootthru=%d\n",
                    obj->type, (prop->flags & PROPFLAG_ONSCREEN) != 0,
                    (obj->runtime_bitflags & RUNTIMEBITFLAG_00001000) != 0,
                    (obj->flags2 & PROPFLAG2_SHOOTTHROUGH) != 0);
            fflush(stderr);
        }
    }
#endif

    if ((prop->flags & PROPFLAG_ONSCREEN)
            && (obj->runtime_bitflags & RUNTIMEBITFLAG_00001000) == 0
            && (obj->flags2 & PROPFLAG2_SHOOTTHROUGH) == 0) {
        tmp = -(model->render_pos->pos.m[3][2] + chrpropSumMatrixNegZ(bbox, (Mtxf*)model->render_pos));

        if (tmp <= shotdata->maxdist) {
            sub_GAME_7F04E720(prop, (void*)shotdata);
        }
    }
}


/**
 * Address: 7F04EA68
 */
void objHit(ShotData *shotdata, BulletHit *hit)
{
    ObjectRecord *obj;
    coord3d pos;
    PropRecord *rootprop;

    rootprop = hit->prop;

    while (rootprop->parent != NULL)
    {
        rootprop = rootprop->parent;
    }

    obj = hit->prop->obj;

    pos.x = shotdata->viewOrigin.x - ((hit->dist * shotdata->viewDir.x) / shotdata->viewDir.z);
    pos.y = shotdata->viewOrigin.y - ((hit->dist * shotdata->viewDir.y) / shotdata->viewDir.z);
    pos.z = shotdata->viewOrigin.z - hit->dist;

    pos.x -= 26.0f * shotdata->viewDir.x;
    pos.y -= 26.0f * shotdata->viewDir.y;
    pos.z -= 26.0f * shotdata->viewDir.z;

    mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &pos);

    if (hit->countsAsPenetration != 0)
    {
        gunSetTracerTarget(&pos);
    }

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): companion to the glasstest probe - a
       * line here means the glass DID take the hit and the fault is in
       * the impact/decal/sfx response downstream. */
        static int dump = -1;
        if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
        if (dump && (obj->type == PROPDEF_GLASS || obj->type == PROPDEF_TINTED_GLASS))
        {
            fprintf(stderr, "glasshit: type=%d pen=%d weapon=%d room=%d\n",
                    obj->type, hit->countsAsPenetration, shotdata->weapon, hit->room);
            fflush(stderr);
        }
    }
#endif

    bullet_spark_create(&pos, 1, 26.0f, rootprop->stan->room);

    if ((objIsHealthy(obj) && objIsMortal(obj)) && (hit->countsAsPenetration != 0))
    {
        inc_curplayer_hitcount_with_weapon(shotdata->weapon, SHOT_REGISTER_OBJECT);
    }

    if (hit->countsAsPenetration == 0)
    {
        sub_GAME_7F064720(&hit->prop->pos);
    }
    else
    {
        recall_joy2_hits_edit_detail_edit_flag(shotdata->weapon, hit->prop, hit->hit.texturenum);
    }

    if (shotdata->weapon != ITEM_WATCHLASER)
    {
        if (hit->countsAsPenetration == 0)
        {
            PropRecord *hitprop;
            s8 room_clear_flag;
            s16 impact_type;

            hitprop = hit->prop;
            room_clear_flag = 0;

            if (obj->model->obj->Skeleton == &skeleton_door)
            {
                room_clear_flag = 1;
            }

            impact_type = (randomGetNext() % 3) + 0x11;

            explosionCreateBulletImpact(&hit->hit.hitpos, &hit->hit.normal, impact_type, 1, hitprop, hit->room, room_clear_flag);
        }
        else
        {
            struct image_sound *impact_sounds;
            s32 thing2_index;
            s8 room_clear_flag;
            s16 texturenum;

            texturenum = hit->hit.texturenum;
            room_clear_flag = 0;

            if (texturenum < 0)
            {
                impact_sounds = g_HitTypeSounds[0];
            }
            else
            {
#ifdef TARGET_N64
                impact_sounds = g_HitTypeSounds[((u8 *)&g_Textures[texturenum])[0] & 0x0f];
#else
                /* PORT: byte-0 family - see chrprop.c bg-hit twin; the raw
                 * byte read grabs hitSound on LE, not hitTexture. */
                impact_sounds = g_HitTypeSounds[g_Textures[texturenum].hitTexture];
#endif
            }

            thing2_index = randomGetNext() % impact_sounds->thing2_len;

            if (((obj->model->obj->Skeleton == &skeleton_door) && (hit->unk44 == obj->model->obj->Switches[3])) || ((obj->model->obj->Skeleton == &skeleton_cctv) && (hit->unk44 == obj->model->obj->Switches[1])))
            {
                room_clear_flag = 1;
            }

            explosionCreateBulletImpact(&hit->hit.hitpos, &hit->hit.normal, impact_sounds->thing2[thing2_index], 1, hit->prop, hit->room, room_clear_flag);
        }
    }

    {
        f32 damage;

        damage = gunItemGetDestructionAmount(shotdata->weapon);

        // On Agent player shots to drone guns do 2x normal damage.
        if (obj->type == PROPDEF_AUTOGUN)
        {
            damage *= F_80030B24;
        }
        // Shots to CCTV camera glass do 100x normal damage.
        else if (obj->type == PROPDEF_CCTV)
        {
            if (obj->model->obj->Skeleton == &skeleton_cctv)
            {
                if (hit->unk44 == obj->model->obj->Switches[1])
                {
                    damage *= 100.0f;
                    objBreakCCTVGlass(obj);
                }
            }

            damage *= F_80030B18;
        }

        chrobjMaybeDetonateObjectIfFlags(obj, damage, &pos, shotdata->weapon, get_cur_playernum());
    }

    if ((obj->model->obj->Skeleton == &skeleton_door) && (hit->countsAsPenetration == 0))
    {
        ((DoorRecord *)obj)->unkbd++;

        if (((DoorRecord *)obj)->unkbd >= 3)
        {
            sub_GAME_7F04DD68(obj);
        }
    }

    objDropRecursively(hit->prop);

    {
        bool do_bounce;

        do_bounce = FALSE;

        if (objIsCollectable(obj))
        {
            if (!(obj->flags & PROPFLAG_00400000))
            {
                do_bounce = TRUE;
            }
        }
        else if (obj->flags & PROPFLAG_00200000)
        {
            do_bounce = TRUE;
        }

        if (obj->flags2 & PROPFLAG2_00000002)
        {
            if (!objIsHealthy(obj))
            {
                do_bounce = TRUE;
            }
        }

        if (obj->flags2 & PROPFLAG2_LINKEDTOSAFE)
        {
            do_bounce = FALSE;
        }

        if (do_bounce)
        {
            objBounce(obj, &shotdata->viewDir);
        }
    }
}


bool objIsHealthy(ObjectRecord *self) //#MATCH
{
    return objGetDestroyedLevel(self) == 0;
}


bool objTestForInteract(PropRecord* prop)
{
    f32 xdiff;
    ObjectRecord *obj;
    PropRecord *player;
    f32 var_f2;
    f32 ydiff;
    f32 zdiff;
    f32 var_f0;
    f32 anglediff;
    f32 playerangle;
    f32 sp30;
    StandTile *stan;
    f32 xzdiff;
    f32 angle;

    obj = prop->obj;

    if (((obj->type == PROP_TYPE_PLAYER)
            || (obj->flags & PROPFLAG_00080000)
            || (obj->runtime_bitflags & (RUNTIMEBITFLAG_00000001 | RUNTIMEBITFLAG_00000002 | RUNTIMEBITFLAG_TAGGED))))
    {
        if ((prop->flags & PROPFLAG_ONSCREEN)
                && (objIsHealthy(obj) != 0)
                && !(obj->flags & PROPFLAG_CANNOT_ACTIVATE))
        {

            player = getCurrentPlayerProp();

            xdiff = obj->runtime_pos.x - player->pos.x;
            ydiff = obj->runtime_pos.y - player->pos.y;
            zdiff = obj->runtime_pos.z - player->pos.z;

            stan = player->stan;

            if ((obj->type == 0x28) && (obj->flags & PROPFLAG_DOOR_OPENTOFRONT))
            {
                var_f0 = 400.0f;
                var_f2 = 160000.0f;
                sp30 = 2.0943952f;
            }
            else
            {
                var_f0 = 200.0f;
                var_f2 = 40000.0f;
                sp30 = 0.3926991f;
            }

            xzdiff = ((xdiff * xdiff) + (zdiff * zdiff));

            if ((xzdiff < var_f2) && (ydiff < var_f0) && (-var_f0 < ydiff))
            {

                angle = atan2f(xdiff, zdiff);
                playerangle = bondviewGetPlayerYawRadians();
                anglediff = angle - playerangle;

                if (angle < playerangle)
                {
                    anglediff += M_TAU_F;
                }

                if (anglediff > M_PI_F)
                {
                    anglediff = M_TAU_F - anglediff;
                }

                if (anglediff <= sp30)
                {
                    if (!(obj->flags2 & PROPFLAG2_INTERACTCHECKLOS) || (walkTilesBetweenPoints_NoCallback(&stan, player->pos.x, player->pos.z, prop->pos.x, prop->pos.z) != 0))
                    {
                        g_InteractProp = prop;
                    }
                }
            }
        }
    }
    return TRUE;
}


/*
 * Return TYPE if Collected or Interacted (except for Alarm which always returns False)
 */
TICKOP propobjInteract(PropRecord *prop)
{
    ObjectRecord *obj        = prop->obj;
    TICKOP op = TICKOP_NONE;

    if (obj->type == PROPDEF_ALARM)
    {
        sndPlaySfx(g_musicSfxBufferPtr, ALARM_SWITCH_SFX, 0);
        if (alarmIsActive())
        {
            alarmDeactivate();
        }
        else
        {
            alarmActivate();
        }
    }

    if (obj->flags & PROPFLAG_00080000)
    {
        op = propPickupByPlayer(prop, TRUE);
    }

    obj->runtime_bitflags |= RUNTIMEBITFLAG_ACTIVATED;
    sub_GAME_7F03E6A0(prop);

    return op;
}


void sub_GAME_7F04F218(PropRecord* prop, s32 arg1) {
    ChrRecord* chr;
    chr = prop->chr;

    if (arg1 != 0)
    {
        chr->accuracyrating = (u8) chr->accuracyrating & ~0x20;
    }
    else
    {
        chr->accuracyrating = (u8) chr->accuracyrating | 0x20;
    }
}


void sub_GAME_7F04F244(PropRecord* prop, rect4f** polygon, s32* edges, f32* top, f32* bottom)
{
    ObjectRecord* obj;
    obj = prop->obj;

    if ((obj->ptr_allocated_collisiondata_block != NULL) && (obj->flags & PROPFLAG_00000100) && !(obj->state & PROPSTATE_20))
    {
        *edges = obj->ptr_allocated_collisiondata_block->edges;
        *polygon = &obj->ptr_allocated_collisiondata_block->polygon;
        *bottom = obj->ptr_allocated_collisiondata_block->bottom;
        *top = obj->ptr_allocated_collisiondata_block->top;
        return;
    }

    *edges = 0;
}


/* param2/param3 are unused; callers pass (ammotype, quantity) as integers,
 * which IDO silently coerced to the old u8* params and gcc rejects */
void append_text_picked_up(u8 *buffer, AMMOTYPE param2, u32 param3)
{
  u8 *str;

  str = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_00_PICKEDUP)); //Picked up
  strcat(buffer,str);
  return;
}





void append_text_ammo_amount_word(u8 *buffer, AMMOTYPE ammotype,u32 amount)
{
    u8 *textfiletext;

    switch(ammotype) {
    case AMMO_9MM:
    case AMMO_9MM_2:
    case AMMO_RIFLE:
    case AMMO_PLASTIQUE:
        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_01_SOME)); //some
        strcat(buffer,textfiletext);
        break;
    case AMMO_SHOTGUN:
    case AMMO_GRENADE:
    case AMMO_ROCKETS:
    case AMMO_REMOTEMINE:
    case AMMO_PROXMINE:
    case AMMO_TIMEDMINE:
    case AMMO_KNIFE:
    case AMMO_GRENADEROUND:
    case AMMO_MAGNUM:
    case AMMO_GGUN:
    case AMMO_DARTS:
    case AMMO_FLARE:
    case AMMO_PITON:
    case AMMO_DYNAMITE:
    case AMMO_BUG:
    case AMMO_MICRO_CAMERA:
        if (amount == 1) {
            textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_02_A)); //a
            strcat(buffer,textfiletext);
        }
        else {
            textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_01_SOME)); //some
            strcat(buffer,textfiletext);
        }
        break;
    case AMMO_EXPLOSIVEPEN:
    case AMMO_BOMBCASE:
        if (amount == 1) {
            textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_03_AN)); //an
            strcat(buffer,textfiletext);
        }
        else {
            textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_01_SOME)); //some
            strcat(buffer,textfiletext);
        }
        break;
    case AMMO_GEKEY:
    case AMMO_TOKEN:
        if (amount == 1) {
            textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_04_THE)); //the
            strcat(buffer,textfiletext);
        }
        else {
            textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_01_SOME)); //some
            strcat(buffer,textfiletext);
        }
    }
    return;
}


void apped_text_ammotype(u8 *buffer, AMMOTYPE ammotype, s32 amount)
{
    u8 *textfiletext;
    if (((ammotype == AMMO_9MM) || (ammotype == AMMO_9MM_2)) || (ammotype == AMMO_RIFLE))
    {
        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_05_AMMO)); //ammo
        strcat(buffer,textfiletext);
    }
    else
    {
        if (ammotype == AMMO_KNIFE)
        {
            textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_0F_THROWING)); //throwing
            strcat(buffer,textfiletext);
            if (amount == 1)
            {
                textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_10_KNIFE)); //knife
                strcat(buffer,textfiletext);
            }
            else
            {
                textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_11_KNIVES)); //knives
                strcat(buffer,textfiletext);
            }
        }
        else
        {
            if (ammotype == AMMO_DYNAMITE)
            {
                if (amount == 1)
                {
                    textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_19_STICK)); //stick
                    strcat(buffer,textfiletext);
                }
                else
                {
                    textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_1A_STICKS)); //sticks
                    strcat(buffer,textfiletext);
                }
                textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_18_OFDYNAMITE)); //of dynamite
                strcat(buffer,textfiletext);
            }
            else
            {
                switch(ammotype)
                {
                    case AMMO_SHOTGUN:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_06_SHOTGUNCARTRIDGE)); //shotgun cartridge
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_MAGNUM:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_07_MAGNUMBULLET)); //magnum bullet
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_GGUN:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_08_GOLDENBULLET)); //golden bullet
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_GRENADE:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_09_HANDGRENADE)); //hand grenade
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_GRENADEROUND:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_0A_GRENADEROUND)); //grenade round
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_ROCKETS:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_0B_ROCKET)); //rocket
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_REMOTEMINE:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_0C_REMOTEMINE)); //remote mine
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_PROXMINE:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_0D_PROXIMITYMINE)); //proximity mine
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_TIMEDMINE:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_0E_TIMEDMINE)); //timed mine
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_DARTS:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_13_DART)); //dart
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_EXPLOSIVEPEN:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_14_EXPLOSIVEPEN)); //explosive pen
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_BOMBCASE:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_15_EXPLOSIVECASE)); //explosive case
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_FLARE:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_16_FLARE)); //flare
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_PITON:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_17_PITON)); //piton
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_BUG:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_1B_BUG)); //bug
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_MICRO_CAMERA:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_1C_MICROCAMERA)); //micro camera
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_GEKEY:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_1D_GOLDENEYEKEY)); //GoldenEye key
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_TOKEN:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_1E_TOKEN)); //token
                        strcat(buffer,textfiletext);
                        break;
                    case AMMO_PLASTIQUE:
                        textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_1F_PLASTIQUE)); //plastique
                        strcat(buffer,textfiletext);
                        break;
                }
                if (1 < amount)
                {
                    textfiletext = langGet(getStringID(LPROPOBJ,PROPOBJ_STR_12_S)); //s
                    strcat(buffer,textfiletext);
                }
            }
        }
    }
}


void set_sound_effect_for_ammo_collection(AMMOTYPE ammotype)
{
    switch(ammotype) {
        case AMMO_9MM:
        case AMMO_9MM_2:
        case AMMO_RIFLE:
        case AMMO_SHOTGUN:
        case AMMO_GRENADE:
        case AMMO_ROCKETS:
        case AMMO_GRENADEROUND:
        case AMMO_MAGNUM:
        case AMMO_GGUN:
        case AMMO_DARTS:
        case AMMO_EXPLOSIVEPEN:
        case AMMO_FLARE:
        case AMMO_PITON:
        case AMMO_DYNAMITE:
        case AMMO_GEKEY:
        case AMMO_TOKEN:
            sndPlaySfx(g_musicSfxBufferPtr,PICKUP_AMMO_SFX,0);
            break;
        case AMMO_REMOTEMINE:
        case AMMO_PROXMINE:
        case AMMO_TIMEDMINE:
        case AMMO_BOMBCASE:
        case AMMO_BUG:
        case AMMO_MICRO_CAMERA:
        case AMMO_PLASTIQUE:
            sndPlaySfx(g_musicSfxBufferPtr,PICKUP_MINE_SFX,0);
            break;
        case AMMO_KNIFE:
            sndPlaySfx(g_musicSfxBufferPtr,PICKUP_KNIFE_SFX,0);
    }
}


void set_sound_effect_for_weapontype_collection(ITEM_IDS weapontype)
{
    if ((weapontype == ITEM_KNIFE) || (weapontype == ITEM_THROWKNIFE))
    {
        sndPlaySfx(g_musicSfxBufferPtr,PICKUP_KNIFE_SFX,0);
    }
    else
    {
        if ((weapontype == ITEM_REMOTEMINE) || (weapontype == ITEM_PROXIMITYMINE) || (weapontype == ITEM_TIMEDMINE) ||
            (weapontype == ITEM_BOMBCASE) || (weapontype == ITEM_BUG) || (weapontype == ITEM_MICROCAMERA) ||
            (weapontype == ITEM_PLASTIQUE))
        {
            sndPlaySfx(g_musicSfxBufferPtr,PICKUP_MINE_SFX,0);
        }
        else
        {
            if ((weapontype == ITEM_GRENADE) || (weapontype == ITEM_GRENADEROUND ) || (weapontype == ITEM_ROCKETROUND))
            {
                sndPlaySfx(g_musicSfxBufferPtr,PICKUP_AMMO_SFX,0);
            }
            else
            {
                if (weapontype == ITEM_LASER)
                {
                    sndPlaySfx(g_musicSfxBufferPtr,PICKUP_LASER_SFX,0);
                }
                else
                {
                    sndPlaySfx(g_musicSfxBufferPtr,PICKUP_GUN_SFX,0);
                }
            }
        }
    }
}


//!FIXME, i need to be properly split from chrai.c
void prepare_ammo_type_collection_text(u8 *finaltext, AMMOTYPE ammotype, u32 quantity)
{
    *finaltext = 0;
    if (j_text_trigger != 0)
    {
        apped_text_ammotype(finaltext,ammotype,quantity);
        if (getPlayerCount() < 3)
        {
            append_text_picked_up(finaltext, ammotype, quantity);
        }
        strcat(finaltext, "\n");
        return;
    }
    if (getPlayerCount() < 3)
    {
        append_text_picked_up(finaltext, ammotype, quantity);
        append_text_ammo_amount_word(finaltext, ammotype, quantity);
    }
    apped_text_ammotype(finaltext, ammotype, quantity);
    strcat(finaltext, ".\n");
}


void display_text_when_ammo_collected(s32 ammotype, s32 quantity)
{
    char buffer[100] = "";
    prepare_ammo_type_collection_text(buffer, ammotype, quantity);
#ifdef VERSION_US
    hudmsgBottomShow(buffer);
#else
    jp_hudmsgBottomShow(buffer);
#endif
}

void add_ammo_to_inventory(AMMOTYPE ammotype,int amount,int doplaysound,int dodisplaytext)
{
    int curammo;
    int maxammo;

    if (0 < amount)
    {
        curammo = check_cur_player_ammo_amount_in_inventory(ammotype);
        maxammo = get_max_ammo_for_type(ammotype);
        if (curammo < maxammo)
        {
            curammo = check_cur_player_ammo_amount_in_inventory(ammotype);
            give_cur_player_ammo(ammotype,curammo + amount);
#if defined(BUGFIX_R1)
        }
#endif
            if (dodisplaytext != 0)
            {
                display_text_when_ammo_collected(ammotype,amount);
            }

            if (doplaysound != 0)
            {
                set_sound_effect_for_ammo_collection(ammotype);
            }

            if (ammotype == AMMO_GRENADE)
            {
                bondinvAddInvItem(ITEM_GRENADE);
            }
            else if (ammotype == AMMO_REMOTEMINE)
            {
                bondinvAddInvItem(ITEM_REMOTEMINE);
                bondinvAddInvItem(ITEM_TRIGGER);
            }
            else if (ammotype == AMMO_PROXMINE)
            {
                bondinvAddInvItem(ITEM_PROXIMITYMINE);
            }
            else if (ammotype == AMMO_TIMEDMINE)
            {
                bondinvAddInvItem(ITEM_TIMEDMINE);
            }
            else if (ammotype == AMMO_KNIFE)
            {
                bondinvAddInvItem(ITEM_THROWKNIFE);
            }
            else if (ammotype == AMMO_BOMBCASE)
            {
                bondinvAddInvItem(ITEM_BOMBCASE);
            }
            else if (ammotype == AMMO_BUG)
            {
                bondinvAddInvItem(ITEM_BUG);
            }
            else if (ammotype == AMMO_MICRO_CAMERA)
            {
                bondinvAddInvItem(ITEM_MICROCAMERA);
            }
            else if (ammotype == AMMO_GEKEY)
            {
                bondinvAddInvItem(ITEM_GOLDENEYEKEY);
            }
            else if (ammotype == AMMO_TOKEN)
            {
                bondinvAddInvItem(ITEM_TOKEN);
            }
            else if (ammotype == AMMO_PLASTIQUE)
            {
                bondinvAddInvItem(ITEM_PLASTIQUE);
            }
#if !defined(BUGFIX_R1)
        }
#endif
    }
}


s32 get_ammo_in_magazine(AmmoCrateRecord *crate)
{
    s32 qty = 1;

    switch (crate->ammoType)
    {
        case AMMO_9MM:     qty = 10; break;
        case AMMO_9MM_2:   qty = 10; break;
        case AMMO_RIFLE:   qty = 10; break;
        case AMMO_SHOTGUN: qty =  5; break;
        case AMMO_MAGNUM:  qty =  5; break;
        case AMMO_GGUN:    qty =  3; break;
        case AMMO_DARTS:   qty =  4; break;
    }

    if (qty > 1 && getPlayerCount() == 1)
    {
        qty *= g_SoloAmmoMultiplier;
    }

    return qty;
}

s32 ammo_collected_from_weapon(WeaponObjRecord *weapon)
{
    s32 ammotype;
    s32 qty;

    ammotype = get_ammo_type_for_weapon(weapon->weaponnum);
    qty = 1;

    if (weapon->flags & PROPFLAG_NO_AMMO)
    {
        return 0;
    }

    switch (ammotype)
    {
        case AMMO_9MM:          qty = 10; break;
        case AMMO_9MM_2:        qty = 10; break;
        case AMMO_RIFLE:        qty = 10; break;
        case AMMO_SHOTGUN:      qty =  5; break;
        case AMMO_MAGNUM:       qty =  5; break;
        case AMMO_GGUN:         qty =  3; break;
        case AMMO_DARTS:        qty =  4; break;
        case AMMO_GRENADEROUND: qty =  3; break;
    }

    if (qty > 1 && getPlayerCount() == 1)
    {
        qty *= g_SoloAmmoMultiplier;
    }

    return qty;
}


void generate_language_specific_text_for_weapon(u8 *finalstring, ITEM_IDS itemtype)
{
    u32 morethan2players;

    morethan2players = FALSE;

#ifndef TARGET_N64
    { /* With 3+ players neither branch below writes the first byte, so the
       * strcat()s appended to an uninitialized stack buffer and pickups showed
       * junk like "U Klobb". Vanilla relied on stack luck; terminate the string
       * here (the ammo pickup text already does this). */
        *finalstring = 0;
    }
#endif

    if (j_text_trigger != 0)
    {
          strcpy(finalstring,"");
          if (2 < getPlayerCount())
          {
              morethan2players = TRUE;
          }
    }
    else
    {
          if (getPlayerCount() < 3)
          {
             //Picked up
            strcpy(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_00_PICKEDUP)));
          }
    }

    switch(itemtype)
    {
        case ITEM_THROWKNIFE:
        case ITEM_GRENADE:
        case ITEM_TIMEDMINE:
        case ITEM_PROXIMITYMINE:
        case ITEM_REMOTEMINE:
        case ITEM_BOMBCASE:
        case ITEM_PLASTIQUE:
        case ITEM_BUG:
        case ITEM_MICROCAMERA:
        case ITEM_GOLDENEYEKEY:
        case ITEM_ROCKETROUND:
        case ITEM_GRENADEROUND :
        case ITEM_TOKEN:
            prepare_ammo_type_collection_text(finalstring,get_ammo_type_for_weapon(itemtype),1);
            return;
        case ITEM_KNIFE:
            //a hunting knife.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_20_AHUNTINGKNIFE)));
            break;
        case ITEM_WPPK:
            //a PP7.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_21_APPK)));
            break;
        case ITEM_WPPKSIL:
            //a silenced PP7.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_22_ASILENCEDPPK)));
            break;
        case ITEM_TT33:
            //a DD44 Dostovei.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_23_ATT33)));
            break;
        case ITEM_SKORPION:
            //a Klobb.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_24_ASPKORPION)));
            break;
        case ITEM_AK47:
            //a KF7 Soviet.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_25_ANAK47)));
            break;
        case ITEM_UZI:
            //a ZMG (9mm).
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_26_ANUZI)));
            break;
        case ITEM_MP5K:
            //a D5K Deutsche.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_27_ANMP5K)));
            break;
        case ITEM_MP5KSIL:
            //a silenced D5K.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_28_ASILENCEDMP5)));
            break;
        case ITEM_SPECTRE:
            //a Phantom.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_29_ASPECTRE)));
            break;
        case ITEM_M16:
            //an AR33 assault rifle.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_2A_ANM16)));
            break;
        case ITEM_FNP90:
            //an RC-P90.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_2B_ANFNP90)));
            break;
        case ITEM_SHOTGUN:
            //a shotgun.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_2C_ASHOTGUN)));
            break;
        case ITEM_AUTOSHOT:
            //an automatic shotgun.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_2D_ANAUTOSHOTGUN)));
            break;
        case ITEM_SNIPERRIFLE:
            //a sniper rifle.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_2E_ASNIPERRIFLE)));
            break;
        case ITEM_GRENADELAUNCH:
            //a grenade launcher.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_2F_AGRENADELAUNCHER)));
            break;
        case ITEM_ROCKETLAUNCH:
            //a rocket launcher.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_30_AROCKETLAUNCHER)));
            break;
        case ITEM_RUGER:
            //a Cougar Magnum.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_31_ARUGERMAGNUM)));
            break;
        case ITEM_GOLDENGUN:
            //the Golden Gun.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_32_THEGOLDENGUN)));
            break;
        case ITEM_LASER:
            //a Moonraker laser.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_33_AMOOKRAKERLASER)));
            break;
        case ITEM_FLAREPISTOL:
            //a flare pistol.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_34_AFLAREPISTOL)));
            break;
        case ITEM_PITONGUN:
            //a piton gun.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_35_APITONGUN)));
            break;
        case ITEM_SILVERWPPK:
            //a silver PP7.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_36_ASILVERPPK)));
            break;
        case ITEM_GOLDWPPK:
            //a gold PP7.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_37_AGOLDPPK)));
            break;
        case ITEM_KEYCARD:
            //a keycard.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_38_AKEYCARD)));
            break;
        case ITEM_KEYYALE:
            //a yale key.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_39_AYALEKEY)));
            break;
        case ITEM_KEYBOLT:
            //a bolt key.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_3A_ABOLTKEY)));
            break;
        default:
            //a new weapon.
            strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_3B_ANEWWEAPON)));
            break;
    }

    if ((j_text_trigger != 0) && (!morethan2players))
    {
        if (finalstring[strlen(finalstring) - 1] == '\n')
        {
            finalstring[strlen(finalstring) - 1] = '\0';
        }
        //Picked up
        strcat(finalstring, langGet(getStringID(LPROPOBJ,PROPOBJ_STR_00_PICKEDUP)));
        strcat(finalstring,"\n");
    }

}


void display_text_for_weapon_in_lower_left_corner(ITEM_IDS weaponid)
{
    char acStack100 [100];

    generate_language_specific_text_for_weapon(acStack100,weaponid);
    HUDMESSAGEBOTTOM(acStack100);
    return;
}





// Perfect Dark propobj.c: s32 propPickupByPlayer(struct prop *prop, bool showhudmsg)
TICKOP propPickupByPlayer(PropRecord *prop, bool showstring)
{
    ObjectRecord *obj;
    TICKOP op;

    op = TICKOP_NONE;
    obj = prop->obj;

    if (g_CurrentPlayer->bonddead || g_ClockTimer == 0)
    {
        return TICKOP_NONE;
    }

    switch (obj->type)
    {
        case PROPDEF_KEY:
        {
            sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, KEYCARD_SFX, 0);

            if (showstring)
            {
                char *text = bondinvGetActivatedTextObject(obj);

                if (text == NULL)
                {
                    text = langGet(getStringID(LPROPOBJ, PROPOBJ_STR_3C_PICKEDUPAKEY)); // "Picked up a key.\n",
                }

#if defined(VERSION_JP) || defined(VERSION_EU)
                jp_hudmsgBottomShow(text);
#else
                hudmsgBottomShow(text);
#endif
            }

            op = INV_ITEM_PICKUP;

            break;
        }

        case PROPDEF_MAGAZINE:
        {
            struct AmmoCrateRecord *crate;
            s32 amount;

            crate = (struct AmmoCrateRecord *)prop->obj;

            amount = get_ammo_in_magazine(crate);
            add_ammo_to_inventory(crate->ammoType, amount, 1, showstring);

            op = TICKOP_FREE;

            break;
        }

        case PROPDEF_AMMO:
        {
            struct MultiAmmoCrateRecord *multicrate;
            s32 i;
            s32 ammoquantity;
            AMMOTYPE ammotype;

            multicrate = (struct MultiAmmoCrateRecord *)prop->obj;

            for (i = AMMO_NONE; i < AMMOTYPE_GLOBAL_MAX; i++)
            {
                ammotype = i + 1;

                if (ammotype == AMMO_9MM_2)
                {
                    ammotype = AMMO_9MM;
                }

                ammoquantity = multicrate->slots[i].quantity;

                if (getPlayerCount() == 1)
                {
                    ammoquantity *= g_SoloAmmoMultiplier;
                }

                add_ammo_to_inventory(ammotype, ammoquantity, 0, showstring);
            }

            sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, PICKUP_AMMO_SFX, 0);

            op = TICKOP_FREE;

            break;
        }

        case PROPDEF_COLLECTABLE: // weapon
        {
            WeaponObjRecord* wep;
            bool collected;
            s32 ammo_type;

            collected = 0;
            wep = (WeaponObjRecord *)prop->obj;

            set_sound_effect_for_weapontype_collection(wep->weaponnum);

            if (wep->weaponnum == ITEM_REMOTEMINE)
            {
                bondinvAddInvItem(ITEM_TRIGGER);
            }
            else if (wep->weaponnum == ITEM_TOKEN)
            {
                currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_TOKEN);
            }

            if (obj->runtime_bitflags & RUNTIMEBITFLAG_DESTROYED)
            {
                if (wep->weaponnum < ITEM_BOMBCASE)
                {
                    bondinvAddWeaponByProp(prop);
                }

                if (showstring)
                {
                    char *text = bondinvGetActivatedTextObject(obj);

                    if (text)
                    {
#if defined(VERSION_JP) || defined(VERSION_EU)
                        jp_hudmsgBottomShow(text);
#else
                        hudmsgBottomShow(text);
#endif
                    }
                    else
                    {
                        display_text_for_weapon_in_lower_left_corner(wep->weaponnum);
                    }

                    collected = 1;
                }

                op = TICKOP_GIVETOPLAYER;
            }
            else
            {
                if (bondinvAddWeaponByProp(prop) != 0)
                {
                    collected = 1;
                }

                if (showstring)
                {
                    char *text = bondinvGetActivatedTextWeapon(wep->weaponnum);

                    if (text)
                    {
                        collected = 1;

#if defined(VERSION_JP) || defined(VERSION_EU)
                        jp_hudmsgBottomShow(text);
#else
                        hudmsgBottomShow(text);
#endif
                    }
                    else if (collected)
                    {
                        display_text_for_weapon_in_lower_left_corner(wep->weaponnum);
                    }
                }

                op = TICKOP_FREE;
            }

            ammo_type = get_ammo_type_for_weapon(wep->weaponnum);

            if (ammo_type)
            {
                s32 pickupqty = ammo_collected_from_weapon(wep);

                if (pickupqty > 0)
                {
                    if (check_cur_player_ammo_amount_in_inventory(ammo_type) < get_max_ammo_for_type(ammo_type))
                    {
                        s32 heldqty = check_cur_player_ammo_amount_in_inventory(ammo_type);

                        give_cur_player_ammo(ammo_type, heldqty + pickupqty);

                        if ((collected == 0) && showstring)
                        {
                            display_text_when_ammo_collected(ammo_type, pickupqty);
                        }
                    }
                }
            }

            break;
        }

        case PROPDEF_ARMOUR: // Body armor
        {
            bondviewAddCurrentPlayerArmor(((struct BodyArmourRecord *)prop->obj)->amount);
            sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, ARMOUR_COLLECT_SFX, 0);

            if (showstring)
            {
                char *text = bondinvGetActivatedTextObject(obj);

                if (text == NULL)
                {
                    if (getPlayerCount() < 3)
                    {
                        text = langGet(getStringID(LPROPOBJ, PROPOBJ_STR_3D_PICKEDUPSOMEBODEYARMOUR)); // "Picked up some body armor.\n",
                    }
                    else
                    {
                        text = langGet(getStringID(LPROPOBJ, PROPOBJ_STR_3E_BODYARMOUR)); // "body armor.\n",
                    }

                }

#if defined(VERSION_JP) || defined(VERSION_EU)
                jp_hudmsgBottomShow(text);
#else
                hudmsgBottomShow(text);
#endif
            }

            op = TICKOP_FREE;

            break;
        }

        case PROPDEF_PROP:
        //case PROPDEF_ALARM:
        //case PROPDEF_CCTV:
        //case PROPDEF_GUARD:
        //case PROPDEF_MONITOR:
        //case PROPDEF_MULTI_MONITOR:
        //case PROPDEF_RACK:
        //case PROPDEF_AUTOGUN:
        //case PROPDEF_LINK:
        //case PROPDEF_DEBRIS:
        //case PROPDEF_UNK16:
        //case PROPDEF_HAT:
        //case PROPDEF_GUARD_ATTRIBUTE:
        //case PROPDEF_SWITCH:
        case PROPDEF_TAG:
        //case PROPDEF_OBJECTIVE_START:
        //case PROPDEF_OBJECTIVE_END:
        //case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
        //case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
        //case PROPDEF_OBJECTIVE_FAIL_CONDITION:
        //case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
        //case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
        //case PROPDEF_OBJECTIVE_PHOTOGRAPH:
        //case PROPDEF_OBJECTIVE_NULL:
        //case PROPDEF_OBJECTIVE_ENTER_ROOM:
        //case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
        //case PROPDEF_OBJECTIVE_COPY_ITEM:
        //case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
        //case PROPDEF_GAS_RELEASING:
        //case PROPDEF_RENAME:
        //case PROPDEF_LOCK_DOOR:
        //case PROPDEF_VEHICHLE:
        //case PROPDEF_AIRCRAFT:
        //case PROPDEF_UNK41:
        //case PROPDEF_GLASS:
        //case PROPDEF_SAFE:
        //case PROPDEF_SAFE_ITEM:
        //case PROPDEF_TANK:
        //case PROPDEF_CAMERAPOS:
        case PROPDEF_TINTED_GLASS:
        default:
        {
            sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, KEYCARD_SFX, 0);

            if (showstring)
            {
                char *text = bondinvGetActivatedTextObject(obj);
                if (text == NULL)
                {
                    text = langGet(getStringID(LPROPOBJ, PROPOBJ_STR_3F_PICKEDUPSOMETHING)); // "Picked up something.\n",
                }

#if defined(VERSION_JP) || defined(VERSION_EU)
                jp_hudmsgBottomShow(text);
#else
                hudmsgBottomShow(text);
#endif
            }

            op = TICKOP_GIVETOPLAYER;

            break;
        }
    }

    if ((op == TICKOP_FREE) && ((obj->runtime_bitflags & RUNTIMEBITFLAG_TAGGED) == 0))
    {
        objFree(obj, 0, (obj->state & RUNTIMEBITFLAG_REMOVE));

        return INV_ITEM_WEAPON;
    }

    if (op != TICKOP_NONE)
    {
        bondinvAddPropToInv(prop);

        return TICKOP_GIVETOPLAYER;
    }

    return TICKOP_NONE;
}


// They're missing from .h associated with them, meaning they're only found in the .c file.
// Move them to a .h to fix the issue. This will also fix the issue with other functions
// which couldn't be brought from decomp.me

extern bool objCanPickupFromSafe(ObjectRecord *obj);
extern s32 bondinvHasInvItem(ITEM_IDS weapon);
extern s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
extern s32 get_max_ammo_for_weapon(ITEM_IDS weapon);
extern s32 get_ammo_count_for_weapon(ITEM_IDS weapon);
extern bool bondinvHasDualWeapon(ITEM_IDS right, ITEM_IDS left);
extern s32 check_cur_player_ammo_amount_in_inventory(AMMOTYPE type);

TICKOP objTickPlayer(struct PropRecord* prop)
{
    struct ObjectRecord* obj;

    obj = prop->obj;

    if ((objIsCollectable(obj) != 0) && (obj->type != PROPDEF_HAT))
    {
        if (obj->flags & PROPFLAG_UNCOLLECTABLE)
        {
            return TICKOP_NONE;
        }
    } 
    else 
    {
        if (!(obj->flags & PROPFLAG_00040000))
        {
            return TICKOP_NONE;
        }
    }

    if (obj->flags & PROPFLAG_00080000)
    {
        return TICKOP_NONE;
    }

    if (obj->runtime_bitflags & 0x80) 
    {
        if (((s32)obj->projectile->refreshrate > 0) && (obj->projectile->unk90 == 0)) 
        {
            return TICKOP_NONE;
        }
    }

    if (objCanPickupFromSafe(obj) == 0) 
    {
        return TICKOP_NONE;
    }

    if (obj->type == PROPDEF_COLLECTABLE) 
    {
        struct WeaponObjRecord* weaponObj;
        s32 var_a1;
        s32 obj_2;

        weaponObj = (WeaponObjRecord*)prop->obj;

        if (((weaponObj->weaponnum == ITEM_GRENADE) || (weaponObj->weaponnum == ITEM_GRENADEROUND))
            && ((weaponObj->timer >= 0) || (obj->runtime_bitflags & 4)))
        {
            return TICKOP_NONE;
        }

        if (((weaponObj->weaponnum == ITEM_REMOTEMINE)
                || (weaponObj->weaponnum == ITEM_PROXIMITYMINE)
                || (weaponObj->weaponnum == ITEM_TIMEDMINE)
                || (weaponObj->weaponnum == ITEM_BOMBCASE)
                || (weaponObj->weaponnum == ITEM_BUG)
                || (weaponObj->weaponnum == ITEM_MICROCAMERA)
                || (weaponObj->weaponnum == ITEM_PLASTIQUE))
            && ((weaponObj->timer >= 0) || (obj->runtime_bitflags & 4)))
        {
            return TICKOP_NONE;
        }

        if ((weaponObj->weaponnum == ITEM_ROCKETROUND) && (obj->runtime_bitflags & 0x80))
        {
            return TICKOP_NONE;
        }

        if (bondinvHasInvItem(weaponObj->weaponnum) != 0) 
        {
            if (get_ammo_type_for_weapon(weaponObj->weaponnum) != 0) 
            {
                if (get_ammo_count_for_weapon(weaponObj->weaponnum) >= get_max_ammo_for_weapon(weaponObj->weaponnum)) 
                {
                    if ((weaponObj->dualweapon != NULL) || (weaponObj->LinkedWeaponType >= 0)) 
                    {
                        if (weaponObj->dualweapon != NULL) 
                        {
                            var_a1 = weaponObj->dualweapon->weaponnum;
                            obj_2 = var_a1;
                        } 
                        else 
                        {
                            var_a1 = weaponObj->LinkedWeaponType;
                            obj_2 = var_a1;
                        }

                        if (weaponObj->flags & 0x10000000) 
                        {
                            var_a1 = weaponObj->weaponnum;
                        } 
                        else 
                        {
                            obj_2 = weaponObj->weaponnum;
                        }

                        if (bondinvHasDualWeapon(obj_2, var_a1) != 0) 
                        {
                            return TICKOP_NONE;
                        }
                    } 
                    else 
                    {
                        return TICKOP_NONE;
                    }
                }
            }
        }
    }
    else if (obj->type == PROPDEF_MAGAZINE)
    {
        struct AmmoCrateRecord* ammoCrateObj;

        ammoCrateObj = (AmmoCrateRecord*)prop->obj;

#if defined(VERSION_JP) || defined(VERSION_EU)
        if (check_cur_player_ammo_amount_in_inventory(ammoCrateObj->ammoType) >= get_max_ammo_for_type(ammoCrateObj->ammoType)
            && !(
                ((ammoCrateObj->ammoType == AMMO_GRENADE) && (!bondinvHasInvItem(ITEM_GRENADE)))
                || ((ammoCrateObj->ammoType == AMMO_REMOTEMINE) && (!bondinvHasInvItem(ITEM_REMOTEMINE)))
                || ((ammoCrateObj->ammoType == AMMO_PROXMINE) && (!bondinvHasInvItem(ITEM_PROXIMITYMINE)))
                || ((ammoCrateObj->ammoType == AMMO_TIMEDMINE) && (!bondinvHasInvItem(ITEM_TIMEDMINE)))
                || ((ammoCrateObj->ammoType == AMMO_KNIFE) && (!bondinvHasInvItem(ITEM_THROWKNIFE)))
            )
        ) {
            return TICKOP_NONE;
        }
#else
        if (check_cur_player_ammo_amount_in_inventory(ammoCrateObj->ammoType) >= get_max_ammo_for_type(ammoCrateObj->ammoType)) {
            return TICKOP_NONE;
        }
#endif
    } 
    else if (obj->type == PROPDEF_AMMO) 
    {
        struct MultiAmmoCrateRecord* multiAmmoCrateObj;
        s32 sp6C;
        s32 i;

        multiAmmoCrateObj = (MultiAmmoCrateRecord*)prop->obj;
        sp6C = 1;

        if (objGetDestroyedLevel(obj) != 0)
        {
            return 0;
        }

        for (i = 0; i < AMMOTYPE_GLOBAL_MAX; i++)
        {
            s32 sp64;

            sp64 = i + 1;

            if (i == 1) {
                sp64 = 1;
            }

            if (multiAmmoCrateObj->slots[i].quantity > 0) {
                if (check_cur_player_ammo_amount_in_inventory(sp64) < get_max_ammo_for_type(sp64)) {
                    sp6C = 0;
                    break;
                }

#if defined(VERSION_JP) || defined(VERSION_EU)
                if (((sp64 == AMMO_GRENADE) && (!bondinvHasInvItem(ITEM_GRENADE)))
                    || ((sp64 == AMMO_REMOTEMINE) && (!bondinvHasInvItem(ITEM_REMOTEMINE)))
                    || ((sp64 == AMMO_PROXMINE) && (!bondinvHasInvItem(ITEM_PROXIMITYMINE)))
                    || ((sp64 == AMMO_TIMEDMINE) && (!bondinvHasInvItem(ITEM_TIMEDMINE)))
                    || ((sp64 == AMMO_KNIFE) && (!bondinvHasInvItem(ITEM_THROWKNIFE))))
                {
                    sp6C = 0;
                    break;
                }
#endif
            }
        }

        if (sp6C) 
        {
            return TICKOP_NONE;
        }
    } 
    else if (obj->type == PROPDEF_ARMOUR) 
    {
        struct BodyArmourRecord* armorObj;
        s32 ignore;
        s32 sp58;

        armorObj = (BodyArmourRecord*)prop->obj;
        ignore = 0;

        if (armorObj->amount <= currentPlayerGetArmor()) 
        {
            ignore = 1;
        } 
        else if (getPlayerCount() >= 2) 
        {
            sp58 = get_scenario();

            if ((sp58 == 2) && (bondinvIsAliveWithFlag() != 0)) 
            {
                ignore = 1;
            } 
            else if ((sp58 == 3) && (bondinvHasGoldenGun() != 0)) 
            {
                ignore = 1;
            }
        }

        if (ignore != 0) 
        {
            return TICKOP_NONE;
        }
    }

    if ((bondviewGetPlayerPitchRadians() < -0.7853982f) && (g_CurrentPlayer->magnetattracttime < 0))
    {
        return TICKOP_NONE;
    }

    {
        f32 temp_f0;
        f32 temp_f12;
        f32 temp_f2;
#if defined(VERSION_US)
        s32 var_v0;
#endif
        struct PropRecord* temp_v0_5;
#if defined(VERSION_JP) || defined(VERSION_EU)
        s32 pad;
#endif
        s32 pickup;

        temp_v0_5 = getCurrentPlayerProp();

        temp_f0 = obj->runtime_pos.x - temp_v0_5->pos.x;
        temp_f12 = obj->runtime_pos.y - temp_v0_5->pos.y;
        temp_f2 = obj->runtime_pos.z - temp_v0_5->pos.z;

        if (g_CurrentPlayer->magnetattracttime >= 0x3C) 
        {
            pickup = (((temp_f0 * temp_f0) + (temp_f2 * temp_f2)) <= 122500.0f)
                && (temp_f12 >= -500.0f)
                && (temp_f12 <= 500.0f);
        } 
        else 
        {
            pickup = (((temp_f0 * temp_f0) + (temp_f2 * temp_f2)) <= 10000.0f)
                && (temp_f12 >= -200.0f)
                && (temp_f12 <= 200.0f);
        }

        if ((pickup != 0) && !(obj->flags2 & 0x1000)) 
        {
            struct StandTile* stan = temp_v0_5->stan;

            if ((stanTestLineUnobstructed(&stan, temp_v0_5->pos.x, temp_v0_5->pos.z, prop->pos.x, prop->pos.z, 2, 30.0f, 30.0f, 0.0f, 1.0f) == 0) || (stan != prop->stan))
            {
                pickup = 0;
            }
        }

        if (pickup != 0) 
        {
            return propPickupByPlayer(prop, TRUE);
        }

        return TICKOP_NONE;
    }
}


/*
* Address: 7F050D30
*/
bool objGetOnscreenRenderBounds(PropRecord *prop, coord3d *arg1, struct coord2d *arg2, struct coord2d *arg3)
{
    if (prop->flags & PROPFLAG_ONSCREEN)
    {
        ObjectRecord *obj = prop->obj;
        Mtxf *matrix = getsubmatrix(obj->model);

        arg1->z = matrix->m[3][2];

        if (arg1->z < 0)
        {
            arg1->x = matrix->m[3][0];
            arg1->y = matrix->m[3][1];

            arg3->f[0] = 0;
            arg3->f[1] = 0;

            arg2->f[0] = 0;
            arg2->f[1] = 0;

            modelGetXYExtents(obj->model, &arg2->f[1], &arg2->f[0], &arg3->f[1], &arg3->f[0]);

            return TRUE;
        }
    }

    return FALSE;
}


void sub_GAME_7F050DE8(Model* model)
{
    ModelNode* node;
    ModelFileHeader* header;
    union ModelRoData* rodata;
    union ModelRwData* rwdata;

    header = model->obj;
    node = header->RootNode;

    while (node != NULL)
    {
        switch (node->Opcode & 0xFF)
        {
            case MODELNODE_OPCODE_DLCOLLISION:
                rodata = node->Data;
                rwdata = modelGetNodeRwData(model, node);

                if ((rwdata->DisplayListCollisions.Vertices != rodata->DisplayListCollisions.Vertices) && (sub_GAME_7F04B590(header, node) != 0))
                {
                    sub_GAME_7F09C044(rwdata->DisplayListCollisions.Vertices);
                    rwdata->DisplayListCollisions.Vertices = rodata->DisplayListCollisions.Vertices;
                }
                break;
            case MODELNODE_OPCODE_LOD:
                modelApplyDistanceRelations(model, node);
                break;
            case MODELNODE_OPCODE_SWITCH:
                modelApplyToggleRelations(model, node);
                break;
            case MODELNODE_OPCODE_HEAD:
                modelApplyHeadRelations(model, node);
                break;
        }

        if (node->Child)
        {
            node = node->Child;
        }
        else
        {
            while (node)
            {
                if (node->Next)
                {
                    node = node->Next;
                    break;
                }

                node = node->Parent;
            }
        }
    }
}


PropRecord *hatApplyToChr(HatRecord *hat, ChrRecord *chr, ModelFileHeader *filedata, PropRecord *prop, Model *model)
{
    prop = objInit((ObjectRecord*)hat, filedata, prop, model);

    if (prop && hat->model)
    {
        f32 scale = hat->extrascale * (1.0f / 256.0f);

        modelSetScale(hat->model, hat->model->scale * scale);
        hat->model->attachedto = chr->model;
        hat->model->attachedto_objinst = chr->model->obj->Switches[6];

        chrpropReparent(prop, chr->prop);
        chr->handle_positiondata_hat = prop;
    }

    return prop;
}


void hatLoadAndApplyToChr(HatRecord *hat, PropRecord *arg1)
{
    s32 unused;
    s32 obj_idx;
    obj_idx = (u32) hat->obj;
    modelLoad(obj_idx);
    hatApplyToChr(hat, arg1, PitemZ_entries[obj_idx].header, NULL, 0);
}


void hatAssignToChr(HatRecord* hat, ChrRecord* chr)
{
    hat->damage = (*(s32*)&hat->damage / M_U16_MAX_VALUE_F);
    hatLoadAndApplyToChr(hat, chr);
}


PropRecord *hatCreateForChr(ChrRecord *chr, s32 modelnum, u32 flags)
{
    ModelFileHeader *modeldef;
    PropRecord *prop;
    Model *model;
    HatRecord *hat;

    modeldef = PitemZ_entries[modelnum].header;

    modelLoad(modelnum);
    prop = chrpropAllocate();
    model = modelmgrInstantiateModel(modeldef);
    hat = hatCreate(prop == NULL, model == NULL, modeldef);

    if (prop == NULL)
    {
        prop = chrpropAllocate();
    }

    if (model == NULL)
    {
        model = modelmgrInstantiateModel(modeldef);
    }

    if (hat && prop && model)
    {
        HatRecord tmp = {
            0x0100, // extrascale
            0x0,    // state
            0x11,   // type
            0,      // obj
            0,      // pad
            0x00004000, // flags
            0,      // flags2
            NULL,   // prop
            NULL,   // model

            { 1.0f, 0.0f, 0.0f, 0.0f,
              0.0f, 1.0f, 0.0f, 0.0f,
              0.0f, 0.0f, 1.0f, 0.0f,
              0.0f, 0.0f, 0.0f, 1.0f
            }, // mtx

            { 0.0f, 0.0f, 0.0f }, // runtime_pos

            { 0x00000000 }, // runtime_bitflags
            NULL, // ptr_allocated_collisiondata_block
            NULL, // projectile/embedment
            0.0f, // maxdamage
            1000.0f, // damage
            { 0xFF, 0xFF, 0xFF, 0x00 }, // shadecol
            { 0xFF, 0xFF, 0xFF, 0x00 }, // nextcol
        };

        *hat = tmp;

        hat->obj = modelnum;
        hat->flags = flags | PROPFLAG_ASSIGNEDTOCHR;
        hat->pad = chr->chrnum;

        prop = hatApplyToChr(hat, chr, modeldef, prop, model);
    }
    else
    {
        if (model)
        {
            clear_model_obj(model);
        }

        if (prop)
        {
            chrpropFree(prop);
            prop = NULL;
        }
    }

    return prop;
}


// PD: weaponCreate
WeaponObjRecord* weaponCreate(bool musthaveprop, bool musthavemodel, ModelFileHeader *modeldef)
{
    s32 i;
    WeaponObjRecord *tmp;
    WeaponObjRecord *sp4c = NULL;
    WeaponObjRecord *sp48 = NULL;
    s32 sp44 = -1;
    s32 sp40 = -1;
    s32 sp3c = -1;

    for (i = g_NextWeaponSlot; TRUE; )
    {

        if (g_WeaponSlots[i].prop == NULL)
        {
            if (!musthaveprop && !musthavemodel)
            {
                sp44 = i;
                break;
            }
        }  else if ((g_WeaponSlots[i].runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0 && (g_WeaponSlots[i].state & PROPSTATE_RESPAWN) == 0)
        {
            WeaponObjRecord* slot = &g_WeaponSlots[i];
            if (((slot->timer <= 0) && (slot->prop->parent == NULL))
                    || (((slot->weaponnum == ITEM_REMOTEMINE) || (slot->weaponnum == ITEM_PROXIMITYMINE) || (slot->weaponnum == ITEM_TIMEDMINE))
                            && ((slot->prop->parent == NULL) || (slot->prop->parent->type == PROP_TYPE_OBJ) || (slot->prop->parent->type == PROP_TYPE_DOOR) || (slot->prop->parent->type == PROP_TYPE_WEAPON))))
            {
                if (!musthavemodel || modelmgrCanSlotFitRwdata(slot->model, modeldef))
                {
                    if ((slot->prop->flags & PROPFLAG_ONSCREEN) == 0 && sp40 < 0)
                    {
                        sp40 = i;
                    }

                    if (sp3c < 0)
                    {
                        sp3c = i;
                    }
                }
            }
        }

        i = (i + 1) % MAX_WEAPON_SLOTS;

        if (i == g_NextWeaponSlot)
        {
            break;
        }
    }

    if (sp44 >= 0)
    {
        g_NextWeaponSlot = (sp44 + 1) % MAX_WEAPON_SLOTS;
        return &g_WeaponSlots[sp44];
    }

    tmp = (WeaponObjRecord *)setupFindObjForReuse(PROPDEF_COLLECTABLE, (ObjectRecord **)&sp4c, (ObjectRecord **)&sp48, musthaveprop, musthavemodel, modeldef);

    if (tmp)
    {
        return tmp;
    }

    if (sp40 >= 0)
    {
        if (g_WeaponSlots[sp40].prop)
        {
            objFreePermanently((ObjectRecord *)&g_WeaponSlots[sp40], TRUE);
        }

        g_NextWeaponSlot = (sp40 + 1) % MAX_WEAPON_SLOTS;
        return (g_WeaponSlots + sp40);
    }

    if (sp4c)
    {
        if (sp4c->prop)
        {
            objFreePermanently((ObjectRecord *)sp4c, TRUE);
        }

        return sp4c;
    }

    if (sp3c >= 0)
    {
        if (g_WeaponSlots[sp3c].prop)
        {
            objFreePermanently((ObjectRecord *)&g_WeaponSlots[sp3c], TRUE);
        }

        g_NextWeaponSlot = (sp3c + 1) % MAX_WEAPON_SLOTS;
        return (g_WeaponSlots + sp3c);
    }

    if (sp48)
    {
        if (sp48->prop)
        {
            objFreePermanently((ObjectRecord *)sp48, TRUE);
        }

        return sp48;
    }

    return NULL;
}


void sub_GAME_7F051588(void)
{
    weaponCreate(FALSE, FALSE, NULL);
}


HatRecord *hatCreate(bool musthaveprop, bool musthavemodel, ModelFileHeader *modeldef)
{
	s32 i;
	HatRecord *tmp;
	HatRecord *sp5c = NULL;
	HatRecord *sp58 = NULL;
	s32 sp54 = -1;
	s32 var_s1 = -1;
	s32 var_s3 = -1;

	for (i = g_NextHatSlot; TRUE; )
	{
		if (g_HatSlots[i].prop == NULL)
		{
			if (!musthaveprop && !musthavemodel)
			{
				sp54 = i;
				break;
			}
		}
		else if ((g_HatSlots[i].runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0
				&& g_HatSlots[i].prop->parent == NULL
				&& (!musthavemodel || modelmgrCanSlotFitRwdata(g_HatSlots[i].model, modeldef)))
		{
			if ((g_HatSlots[i].prop->flags & PROPFLAG_ONSCREEN) == 0 && var_s1 < 0)
			{
				var_s1 = i;
			}

			if (var_s3 < 0)
			{
				var_s3 = i;
			}
		}

		i = (i + 1) % MAX_HAT_SLOTS;

		if (i == g_NextHatSlot)
		{
			break;
		}
	}

	if (sp54 >= 0)
	{
		g_NextHatSlot = (sp54 + 1) % MAX_HAT_SLOTS;
		return (g_HatSlots + sp54);
	}

	tmp = (HatRecord *)setupFindObjForReuse(PROPDEF_HAT, (ObjectRecord **)&sp5c, (ObjectRecord **)&sp58, musthaveprop, musthavemodel, modeldef);

	if (tmp)
	{
		return tmp;
	}

	if (var_s1 >= 0)
	{
		if (g_HatSlots[var_s1].prop)
		{
			objFreePermanently((ObjectRecord*)&g_HatSlots[var_s1], TRUE);
		}

		g_NextHatSlot = (var_s1 + 1) % MAX_HAT_SLOTS;
		return (g_HatSlots + var_s1);
	}

	if (sp5c)
	{
		if (sp5c->prop)
		{
			objFreePermanently((ObjectRecord*)sp5c, TRUE);
		}

		return sp5c;
	}

	if (var_s3 >= 0)
	{
		if (g_HatSlots[var_s3].prop)
		{
			objFreePermanently((ObjectRecord*)&g_HatSlots[var_s3], TRUE);
		}

		g_NextHatSlot = (var_s3 + 1) % MAX_HAT_SLOTS;
		return (g_HatSlots + var_s3);
	}

	if (sp58)
	{
		if (sp58->prop)
		{
			objFreePermanently((ObjectRecord*)sp58, TRUE);
		}

		return sp58;
	}

	return NULL;
}


HatRecord* sub_GAME_7F0518A8(void)
{
    return hatCreate(0, 0, NULL);
}


AmmoCrateRecord *ammocrateAllocate(void)
{
    s32 i;

    // Try to find a free one
    for (i = 0; i < MAX_AMMO_CRATES; i++)
    {
        if (g_AmmoCrates[i].prop == NULL)
        {
            return (g_AmmoCrates + i);
        }
    }

    // Find one that can be freed off-screen
    for (i = 0; i < MAX_AMMO_CRATES; i++)
    {
        if ((g_AmmoCrates[i].runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0
                && (g_AmmoCrates[i].state & PROPSTATE_RESPAWN) == 0
                && g_AmmoCrates[i].prop->parent == NULL
                && (g_AmmoCrates[i].prop->flags & 0x02) == 0)
        {
            objFreePermanently(&g_AmmoCrates[i], TRUE);
            return (g_AmmoCrates + i);
        }
    }

    // Find one that can be freed on-screen
    for (i = 0; i < MAX_AMMO_CRATES; i++)
    {
        if ((g_AmmoCrates[i].runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0
                && (g_AmmoCrates[i].state & PROPSTATE_RESPAWN) == 0
                && g_AmmoCrates[i].prop->parent == NULL)
        {
            objFreePermanently(&g_AmmoCrates[i], TRUE);
            return (g_AmmoCrates + i);
        }
    }

    return NULL;
}


void trigger_remote_mine_detonation(void)
{
    u32 uVar1 = 1 << (get_cur_playernum());
    g_RemoteMineOwnerTriggerFlag = uVar1 | g_RemoteMineOwnerTriggerFlag;
    sndPlaySfx(g_musicSfxBufferPtr, WATCH_DETONATE_MINE_SFX, NULL);
}


/**
 * Get Key with ID from Prop (or child of prop)
 * @param ID: ID of key
 * @param prop: Prop to search
 * @return: Key if found
 * @RenameTo: objGetKeyIfExist
*/
KeyRecord *check_if_entry_is_collectable(s32 ID, PropRecord *prop) //#MATCH
{
    KeyRecord * key;
    PropRecord *p;

    if (prop->type == PROPDEF_KEY)
    {
        key = prop->obj;
        if (ID == key->keyID)
        {
            return key;
        }
    }

    for (p = prop->child; p; p = p->prev)
    {
        key = check_if_entry_is_collectable(ID, p);
        if (key)
        {
            return key;
        }
    }
    return NULL;
}




/**
 * Get Key if has been "dropped"
 * @param KeyID: ID of Key to Find
 * @return: Key if found and "Dropped"
 * @RenameTo: objGetKeyIfDropped
*/
KeyRecord *weaponFindThrown(s32 KeyID) //MATCH
{
    KeyRecord  *obj;
    PropRecord *prop;

    for (prop = chrpropGetActiveTail(); prop; prop = prop->prev)
    {
        obj = check_if_entry_is_collectable(KeyID, prop);
        if (obj && (!(obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)))
        {
            return obj;
        }
    }

    return NULL;
}

void add_obj_to_temp_proxmine_table(WeaponObjRecord* proxy)
{
    s32 i = 0;

    while (1) {
        if (proxy_mine_table[i] == NULL)
        {
            proxy_mine_table[i] = proxy;
            #ifdef DEBUG
                assert(i<PROXIMITYARRMAX);
            #endif

            return;
        }
        i++;
        if (i == 30)
        {
            return;
        }
    }
}


void remove_obj_from_temp_proxmine_table(WeaponObjRecord* proxy)
{
    s32 i = 0;

    while (1)
    {
        if (proxy_mine_table[i] == proxy)
        {
            proxy_mine_table[i] = NULL;
            return;
        }
        i++;
        if (i == 30)
        {
            return;
        }
    }
}

/*
* Address: 0x7f051bcc
*/
void detonate_proxmine_In_range(coord3d* pos)
{
    s32 i;
    for (i = 0; i < 30; i++)
    {
        WeaponObjRecord* obj = proxy_mine_table[i];

        if (obj && (obj->timer == 1))
        {
            f32 diff_x;
            f32 diff_z;
            f32 diff_y;
            f32 dist_sqr;
            diff_x = pos->x - obj->runtime_pos.x;
            diff_y = pos->y - obj->runtime_pos.y;
            diff_z = pos->z - obj->runtime_pos.z;
            dist_sqr = (diff_x * diff_x) + (diff_y * diff_y) + (diff_z * diff_z);

            if (dist_sqr < PROXIMITY_MINE_TRIGGER_DISTANCE)
            {
                obj->timer = 0;
            }
        }
    }
}


void check_guard_detonate_proxmine(void)
{
    ChrRecord* guard;
    s32 numslots;
    s32 i;

    numslots = get_numguards();

    for (i = 0; i < numslots; i++)
    {
        guard = &g_ChrSlots[i];
        if ((guard->model != NULL) && (guard->hidden & CHRHIDDEN_BACKGROUND_AI))
        {
            coord3d pos;
            chrlvGetPatrolPercentOrPosition(guard, &pos);
            detonate_proxmine_In_range(&pos);
        }
    }
}


void propweaponSetDual(WeaponObjRecord *leftweapon, WeaponObjRecord *rightweapon) //#MATCH
{
    leftweapon->LinkedWeaponType  = rightweapon->weaponnum;
    leftweapon->dualweapon        = rightweapon;
    rightweapon->LinkedWeaponType = leftweapon->weaponnum;
    rightweapon->dualweapon       = leftweapon;
}


PropRecord* complete_object_data_block_return_position_entry(WeaponObjRecord* obj, ModelFileHeader* model_header, PropRecord* prop, Model* model)
{
    prop = objInit((ObjectRecord*)obj, model_header, prop, model);
    if (prop != NULL)
    {
        prop->type = 4;
        weaponSetGunfireVisible(prop, 0);
    }

    return prop;
}


PropRecord* sub_GAME_7F051DD8(struct ObjectRecord* arg0, ModelFileHeader* arg1)
{
    PropRecord* prop;

    prop = objInitWithModelDef(arg0, arg1);
    if (prop != NULL)
    {
        prop->type = PROP_TYPE_WEAPON;
        weaponSetGunfireVisible(prop, 0);
    }
    return prop;
}


bool chrEquipWeapon(WeaponObjRecord *wep, ChrRecord *chr)
{
    WeaponObjRecord *wep2;
    GUNHAND hand = wep->flags & PROPFLAG_WEAPON_LEFTHANDED;

    if (wep->flags & PROPFLAG_WEAPON_LEFTHANDED)
    {
        hand = GUNLEFT;
    }
    else
    {
        hand = GUNRIGHT;
    }

    wep2 = wep;
    if (wep2->prop && wep2->model)
    {
        if (!(wep2->flags & PROPFLAG_CONCEAL_GUN))
        {
            if (!chr->weapons_held[hand])
            {
                wep2->model->attachedto = chr->model;

                if (hand == GUNRIGHT)
                {
                    wep2->model->attachedto_objinst = chr->model->obj->Switches[3];
                }
                else
                {
                    wep2->model->attachedto_objinst = chr->model->obj->Switches[5];
                }

                chr->weapons_held[hand] = wep2->prop;

                if (wep2->flags & PROPFLAG_IS_DOUBLE && chr->weapons_held[1 - hand])
                {
                    propweaponSetDual(wep2, chr->weapons_held[1 - hand]->obj);
                }
            }
            else
            {
                 #ifdef DEBUG
                    osSyncPrintf("attempted multiple attach!!!\n");
                #endif
                return FALSE;
            }
        }
        chrpropReparent(wep2->prop, chr->prop);
    }
    return TRUE;
}


PropRecord *sub_GAME_7F051F30(WeaponObjRecord *weapon, ChrRecord *chr, ModelFileHeader *modeldef, PropRecord *prop, Model *model)
{
	prop = complete_object_data_block_return_position_entry(weapon, modeldef, prop, model);

	if (prop && weapon->model)
	{
		f32 scale = weapon->extrascale * (1.0f / 256.0f);

		modelSetScale(weapon->model, weapon->model->scale * scale);
        chrEquipWeapon(weapon, chr);
        if (weapon->model);
	}

	return prop;
}


void sub_GAME_7F051FD4(WeaponObjRecord *weapon, ChrRecord *chr)
{
	u32 stack;
	s32 modelnum = weapon->obj;

	modelLoad(modelnum);
	sub_GAME_7F051F30(weapon, chr, PitemZ_entries[modelnum].header, 0, 0);
}


void sub_GAME_7F052030(WeaponObjRecord* arg0, ChrRecord* arg1)
{
    arg0->damage = (*(s32*)&arg0->damage) / M_U16_MAX_VALUE_F;
    sub_GAME_7F051FD4(arg0, arg1);
}


WeaponObjRecord blank_08_object_preset_1 = {
    0x0100, //extrascale
    0x0, //state
    0x08, //type
    0, //obj
    1, //pad
    0x00000000, //flags
    0, //flags2
    NULL, // prop
    NULL, // model
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    }, //mtx
    { 0.0, 0.0, 0.0 }, //runtime_pos
    {0x00000000 }, //runtime_bitflags
    NULL, //ptr_allocated_collisiondata_block
    NULL, //projectile/embedment
    0.0f, //maxdamage
    1000.0f,//damage
    { 0xFF, 0xFF, 0xFF, 0x00 }, // shadecol
    { 0xFF, 0xFF, 0xFF, 0x00 }, // nextcol
    ITEM_UNARMED, //weaponnum
    -1, //LinkedWeaponType
    -1, //timer
    NULL //dualweapon
};


/**
 * Address: 7F05206C
 *
 * @param modelnum: index into PitemZ_entries, which is enum PROP
 * @param weaponid: object_weapon.gun_pickup value
 */
ObjectRecord *create_new_item_instance_of_model(PROP modelnum, s32 weaponid)
{
    ModelFileHeader *modeldef;
    PropRecord *prop;
    Model *model;
    WeaponObjRecord *obj;

    modeldef = PitemZ_entries[modelnum].header;

    modelLoad(modelnum);

    prop = chrpropAllocate();

    model = modelmgrInstantiateModel(modeldef);

    obj = weaponCreate(prop == NULL, model == NULL, modeldef);

    if (prop == NULL)
    {
        prop = chrpropAllocate();
    }

    if (model == NULL)
    {
        model = modelmgrInstantiateModel(modeldef);
    }

    if (obj != NULL && prop != NULL && model != NULL)
    {
        WeaponObjRecord tmp = blank_08_object_preset_1;

        *obj = tmp;

        obj->weaponnum = weaponid;
        obj->obj = modelnum;

        complete_object_data_block_return_position_entry(obj, modeldef, prop, model);
    }
    else
    {
        obj = NULL;

        if (model != NULL)
        {
            clear_model_obj(model);
        }

        if (prop != NULL)
        {
            chrpropFree(prop);
        }
    }

    return (ObjectRecord *)obj;
}


/**
 * Set removed flag on hand
 */
void chrSetWeaponFlag4(ChrRecord *chr, GUNHAND hand) //#MATCH
{
    if (chr->weapons_held[hand])
    {
        chr->weapons_held[hand]->weapon->runtime_bitflags |= RUNTIMEBITFLAG_REMOVE;
    }
}

WeaponObjRecord blank_08_object_preset_4001 = {
    0x0100, //extrascale
    0x0, //state
    0x08, //type
    0, //obj
    0x4001, //pad
    0x00000000, //flags
    0, //flags2
    NULL, //prop
    NULL, //model
    {
       1.0f, 0.0f, 0.0f, 0.0f,
       0.0f, 1.0f, 0.0f, 0.0f,
       0.0f, 0.0f, 1.0f, 0.0f,
       0.0f, 0.0f, 0.0f, 1.0f
    }, //mtx
    {0.0, 0.0, 0.0},//runtime_pos
    {0x00000000}, //runtime_bitflags
    NULL, //ptr_allocated_collisiondata_block
    NULL, //projectile/embedment
    0.0f, //maxdamage
    1000.0f, //damage
    { 0xFF, 0xFF, 0xFF, 0x00 }, // shadecol
    { 0xFF, 0xFF, 0xFF, 0x00 }, // nextcol
    ITEM_UNARMED, //weaponnu
    -1, //LinkedWeaponType
    -1, //timer
    NULL //dualweapon
};

/**
 * NTSC address 0x7F052214.
*/
PropRecord *something_with_generating_object(ChrRecord *self, s32 propid, ITEM_IDS itemid, s32 flags, WeaponObjRecord *weapon, ItemModelFileRecord *prop_header)
{
    Model *objinst;
    PropRecord *lastobjentry;

    if (!prop_header)
    {
#ifndef TARGET_N64
        /* MP bring-up guard (see modelLoad): a bad propid here fed the
         * NULL-header AV on 4-player load. Log and bail - the chr simply
         * doesn't get the weapon, which every caller tolerates. */
        if (propid < 0 || propid >= 340 || PitemZ_entries[propid].header == NULL)
        {
            extern int printf(const char *, ...);
            printf("something_with_generating_object: BAD propid=%d itemid=%d chr=%p\n",
                   propid, itemid, (void *) self);
            return NULL;
        }
#endif
        prop_header = PitemZ_entries[propid].header;
        modelLoad(propid);
    }

    lastobjentry = chrpropAllocate();
    objinst = modelmgrInstantiateModel((ModelFileHeader *)prop_header);

    if (!weapon)
    {
        weapon = weaponCreate(lastobjentry == NULL, objinst == NULL, (ModelFileHeader *)prop_header);
    }

    if (!lastobjentry)
    {
        lastobjentry = chrpropAllocate();
    }

    if (!objinst)
    {
        objinst = modelmgrInstantiateModel((ModelFileHeader *)prop_header);
    }

    if (weapon && lastobjentry && objinst)
    {
        WeaponObjRecord new_weapon = blank_08_object_preset_4001;
        *weapon = new_weapon;

        weapon->weaponnum = itemid;
        weapon->obj = propid;
        weapon->flags = flags | 0x4000;

        // pad = chrnum ???
        weapon->pad = self->chrnum;

        lastobjentry = sub_GAME_7F051F30(weapon, self, (ModelFileHeader *)prop_header, lastobjentry, objinst);
    }
    else
    {
        if (objinst)
        {
            clear_model_obj(objinst);
        }

        if (lastobjentry)
        {
            chrpropFree(lastobjentry);
            lastobjentry = NULL;
        }
    }

    return lastobjentry; //should be new weapon
}


/**
 * Add New Weapon to chr
 */
PropRecord *chrGiveWeapon(ChrRecord *self, s32 PropID, ITEM_IDS ItemID, s32 flags) //#MATCH
{
#ifndef TARGET_N64
    extern s32 portTrueEnemyRocketsEnabled(void);
    if (portTrueEnemyRocketsEnabled() && cheatIsActive(CHEAT_ENEMY_ROCKETS))
    {
        switch ((s8) ItemID)
        {
            case ITEM_KNIFE:
            case ITEM_THROWKNIFE:
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
            case ITEM_SHOTGUN:
            case ITEM_AUTOSHOT:
            case ITEM_SNIPERRIFLE:
            case ITEM_RUGER:
            case ITEM_GOLDENGUN:
            case ITEM_SILVERWPPK:
            case ITEM_GOLDWPPK:
            case ITEM_LASER:
            case ITEM_WATCHLASER:
            case ITEM_REMOTEMINE:
            case ITEM_TRIGGER:
            case ITEM_TASER:
                PropID = PROP_CHRROCKETLAUNCH;
                ItemID = ITEM_ROCKETLAUNCH;
                break;
        }
    }
#endif
    return something_with_generating_object(self, PropID, ItemID, flags, NULL, NULL);
}


/**
 * Must remain immediately above chrRenderHeldWeapon for matching.
*/
ModelRenderData D_800322A4 = {
    0,
    1,
    3,
    0,
    0,
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
    0
};


/**
 * Render the weapon(s) characters are holding including the muzzle flash.
 * Address: 0x7f0523f8
 */
void chrRenderHeldWeapon(void *renderContext, GUNHAND hand, Gfx **gdl)
{
    ChrRecord *chr;
    PropRecord *prop;
    ObjectRecord *weaponObj;
    Model *heldModel;
    ModelRenderData renderData;
    Model *chrModel;
    u32 pad; // stack alignment for matching
    Mtxf rotationMtx;

    chr = ((ChrRenderContext *)renderContext)->chr;
    prop = chrGetEquippedWeaponProp(chr, hand);

    if (prop != NULL) {

        weaponObj = prop->obj;

        if (!(weaponObj->runtime_bitflags & RUNTIMEBITFLAG_00000800))
        {
            if ((s32)(weaponObj->flags2 << 12) >= 0) 
            {
                heldModel = weaponObj->model;
                renderData = D_800322A4;

                chrModel = chr->model;
                prop->flags |= PROPFLAG_ONSCREEN;

                renderData.basemtx = modelFindNodeMtx(chrModel, heldModel->attachedto_objinst, 0);

                if (hand == GUNLEFT) 
                {
                    matrix_4x4_set_rotation_around_z(3.1415927f, &rotationMtx);
                    matrix_4x4_multiply_in_place(renderData.basemtx, &rotationMtx);
                    renderData.basemtx = &rotationMtx;
                }

                renderData.mtxlist = dynAllocate(heldModel->obj->numMatrices * sizeof(Mtxf));
                instcalcmatrices(&renderData, heldModel);

                if (gdl != NULL) 
                {
                    if (!(weaponObj->runtime_bitflags & RUNTIMEBITFLAG_00000080)) 
                    {
                        *gdl = sub_GAME_7F06B120(*gdl, heldModel);
                    }
                }

                return;
            }
        }

        prop->flags &= ~PROPFLAG_ONSCREEN;
    }
}


/*
* Address: 0x7f052554
*/
TICKOP weaponTickPlayer(struct PropRecord* arg0)
{
    return objTickPlayer(arg0);
}




/*
* Address: 0x7f052574
*/
void weaponSetGunfireVisible(PropRecord *prop, s32 firing)
{
    ObjectRecord *obj = prop->obj;
    Model *model = obj->model;
    ModelNode *node;

    if (model && model->obj->Skeleton == &skeleton_prop_weapon) {
        node = model->obj->Switches[0];
        if (node) {
            struct ModelRwData_GunfireRecord *rwdata = modelGetNodeRwData(model, node);
            rwdata->visible = firing;
        }

        node = model->obj->Switches[2];
        if (node) {
            struct ModelRwData_BSPRecord *rwdata = modelGetNodeRwData(model, node);
            rwdata->visible = firing;
        }
    }
}



/*
* Address: 0x7f052604
*/
s32 weaponIsGunfireVisible(PropRecord *prop)
{
    ObjectRecord *obj = prop->obj;
    Model *model = obj->model;
    ModelNode *node;

    if (model && model->obj->Skeleton == &skeleton_prop_weapon) {
        node = model->obj->Switches[0];
        if (node) {
            struct ModelRwData_GunfireRecord *rwdata = modelGetNodeRwData(model, node);
            return rwdata->visible;
        }

        node = model->obj->Switches[2];
        if (node) {
            struct ModelRwData_BSPRecord *rwdata = modelGetNodeRwData(model, node);
            return rwdata->visible;
        }
    }

    return FALSE;
}




/*
* Alternative name: getHatType
* Address: 0x7f052684
*/
HATTYPE get_hat_model(PropRecord *prop) //#MATCH
{
    ObjectRecord *objinst = prop->obj;
    switch (objinst->obj)
    {
        case PROP_HATFURRY:
        case PROP_HATFURRYBROWN:
        case PROP_HATFURRYBLACK:
        {
            return HATTYPE_FURRY;
        }

        case PROP_HATTBIRD:
        case PROP_HATTBIRDBROWN:
        {
            return HATTYPE_BIRD;
        }

        case PROP_HATHELMET:
        case PROP_HATHELMETGREY:
        {
            return HATTYPE_HELMATE;
        }

        case PROP_HATMOON:

        {
            return HATTYPE_MOON;
        }
        case PROP_HATBERET:
        case PROP_HATBERETBLUE:
        case PROP_HATBERETRED:
        {
            return HATTYPE_BERRET;
        }

        case PROP_HATPEAKED:
        {
            return HATTYPE_PEAKED;
        }

        default:
        {
            return HATTYPE_OTHER;
        }
    }
}





/**
 * US address 7F0526EC.
*/
void door7F0526EC(DoorRecord *door, Mtxf *rhs)
{
    Mtxf lhs;
    struct coord3d sp54;
    struct coord3d sp48;
    BoundPadRecord* temp_v0_2;
    struct coord3d sp38;
    struct coord3d sp2C;

    if ((door->doorType == DOORTYPE_SWINGING) || (door->doorType == DOORTYPE_AZTECCHAIR))
    {
        temp_v0_2 = &g_CurrentSetup.boundpads[door->pad];

        sp38.f[0] = (temp_v0_2->up.f[1] * temp_v0_2->look.f[2]) - (temp_v0_2->up.f[2] * temp_v0_2->look.f[1]); // cross product
        sp38.f[1] = (temp_v0_2->up.f[2] * temp_v0_2->look.f[0]) - (temp_v0_2->up.f[0] * temp_v0_2->look.f[2]); // cross product
        sp38.f[2] = (temp_v0_2->up.f[0] * temp_v0_2->look.f[1]) - (temp_v0_2->up.f[1] * temp_v0_2->look.f[0]); // cross product

        sp54.f[0] = temp_v0_2->pos.f[0] + (temp_v0_2->up.f[0] * temp_v0_2->bbox.ymin);
        sp54.f[1] = temp_v0_2->pos.f[1] + (temp_v0_2->up.f[1] * temp_v0_2->bbox.ymin);
        sp54.f[2] = temp_v0_2->pos.f[2] + (temp_v0_2->up.f[2] * temp_v0_2->bbox.ymin);

        if (door->doorType == DOORTYPE_AZTECCHAIR)
        {
            sp54.f[0] += sp38.f[0] * temp_v0_2->bbox.xmax;
            sp54.f[1] += sp38.f[1] * temp_v0_2->bbox.xmax;
            sp54.f[2] += sp38.f[2] * temp_v0_2->bbox.xmax;
        }
        else if (door->flags & PROPFLAG_DOOR_OPENTOFRONT)
        {
            sp54.f[0] += sp38.f[0] * temp_v0_2->bbox.xmax;
            sp54.f[1] += sp38.f[1] * temp_v0_2->bbox.xmax;
            sp54.f[2] += sp38.f[2] * temp_v0_2->bbox.xmax;
        }
        else
        {
            sp54.f[0] += sp38.f[0] * temp_v0_2->bbox.xmin;
            sp54.f[1] += sp38.f[1] * temp_v0_2->bbox.xmin;
            sp54.f[2] += sp38.f[2] * temp_v0_2->bbox.xmin;
        }

        sp48.f[0] = door->runtime_pos.f[0] - sp54.f[0];
        sp48.f[1] = door->runtime_pos.f[1] - sp54.f[1];
        sp48.f[2] = door->runtime_pos.f[2] - sp54.f[2];

        matrix_4x4_copy(&door->mtx, rhs);
        matrix_4x4_set_identity_and_position(&sp48, &lhs);
        matrix_4x4_multiply_in_place(&lhs, rhs);

        if (door->doorType == DOORTYPE_AZTECCHAIR)
        {
            if (door->flags & PROPFLAG_DOOR_OPENTOFRONT)
            {
                matrix_4x4_set_rotation_around_z(M_TAU_F - ((door->openPosition * M_TAU_F) / 360.0f), &lhs);
            }
            else
            {
                matrix_4x4_set_rotation_around_z((door->openPosition * M_TAU_F) / 360.0f, &lhs);
            }
        }
        else if (door->flags & PROPFLAG_DOOR_OPENTOFRONT)
        {
            matrix_4x4_set_rotation_around_y(M_TAU_F - ((door->openPosition * M_TAU_F) / 360.0f), &lhs);
        }
        else
        {
            matrix_4x4_set_rotation_around_y((door->openPosition * M_TAU_F) / 360.0f, &lhs);
        }

        matrix_4x4_multiply_in_place(&lhs, rhs);
        matrix_4x4_set_identity_and_position(&sp54, &lhs);
        matrix_4x4_multiply_in_place(&lhs, rhs);
    }
    else if ((door->doorType == DOORTYPE_EYE) || (door->doorType == DOORTYPE_IRIS))
    {
        matrix_4x4_copy(&door->mtx, rhs);
        matrix_4x4_set_position(&door->runtime_pos, rhs);
    }
    else
    {
        sp2C.f[0] = (door->frac * door->openPosition) + door->runtime_pos.x;
        sp2C.f[1] = (door->unkac * door->openPosition) + door->runtime_pos.y;
        sp2C.f[2] = (door->unkb0 * door->openPosition) + door->runtime_pos.z;

        matrix_4x4_copy(&door->mtx, rhs);
        matrix_4x4_set_position(&sp2C, rhs);
    }

    if (door->doorFlags & DOORFLAG_FLIP)
    {
        matrix_column_3_scalar_multiply_2(-1.0f, rhs);
    }
}



/**
 * NTSC address 0x7F052B00.
*/
void doorUpdateBbox(DoorRecord *door)
{
    struct ModelRoData_BoundingBoxRecord *door_bb;
    Mtxf sp2C;

    door_bb = (struct ModelRoData_BoundingBoxRecord *)door->model->obj->RootNode->Child->Data;

    // struct copy
    door->bbox = *door_bb;

    // Resize the door's bounding box.
    if (door->doorFlags & DOORFLAG_CLIP_TO_BBOX)
    {
        if (door->doorType == DOORTYPE_VERTICAL)
        {
            door->bbox.Bounds.ymax = door_bb->Bounds.ymax + (door_bb->Bounds.ymin - door_bb->Bounds.ymax) * door->openPosition;
        }
        else
        {
            door->bbox.Bounds.xmin = door_bb->Bounds.xmin + (door_bb->Bounds.xmax - door_bb->Bounds.xmin) * door->openPosition;
        }
    }

    if (door->perimFrac <= door->openPosition)
    {
        door->ptr_allocated_collisiondata_block->edges = 0;

        return;
    }

    door7F0526EC(door, &sp2C);
    sub_GAME_7F03F540(&door->bbox, &sp2C, &door->ptr_allocated_collisiondata_block->polygon, door->ptr_allocated_collisiondata_block);

    if (door->doorType == DOORTYPE_VERTICAL)
    {
        door->ptr_allocated_collisiondata_block->bottom = door->runtime_pos.f[1] + chrpropSumMatrixPosY(&door->bbox, &sp2C);
    }
    else if (door->doorType == DOORTYPE_FALLAWAY)
    {
        door->ptr_allocated_collisiondata_block->bottom = door->runtime_pos.f[1] - 10000.0f;
    }
    else
    {
        door->ptr_allocated_collisiondata_block->bottom = sp2C.m[3][1] + chrpropSumMatrixPosY(&door->bbox, &sp2C);

        if (door->doorFlags & DOORFLAG_EXTENDEDY)
        {
            door->ptr_allocated_collisiondata_block->bottom -= 1000.0f;
        }
    }

    if (((door->doorType == DOORTYPE_EYE) && (0 < door->openPosition - (0.4f * door->maxFrac)))
        || ((door->doorType == DOORTYPE_IRIS) && (0 < door->openPosition - (0.4f * door->maxFrac)))
        )
    {

        door->ptr_allocated_collisiondata_block->top = door->ptr_allocated_collisiondata_block->bottom + 50.0f;
    }
    else if (door->doorType == DOORTYPE_FALLAWAY)
    {
        door->ptr_allocated_collisiondata_block->top = door->runtime_pos.f[1] + 1000.0f;
    }
    else
    {
        door->ptr_allocated_collisiondata_block->top = sp2C.m[3][1] + chrpropSumMatrixNegY(&door->bbox, &sp2C);

        if (door->doorFlags & DOORFLAG_EXTENDEDY)
        {
            door->ptr_allocated_collisiondata_block->top += 1000.0f;
        }
    }

}


/**
 * Address: 7F052D8C
 * 
 * This function physically resizes sliding doors with DOORFLAG_CLIP_TO_BBOX as they move.
 * When the door is opening, it shrinks. When the door is closing, it expands.
 * This prevents z-fighting if the door's face is parallel with the suface it's sliding into.
 * Examples include the brown security doors in Facility and the sliding stone doors in Egyptian and Temple.
 */
void doorBuildClippedVertices(DoorRecord *inDoor)
{
    Model *mdl;
    ModelNode *mdlDLCNode;
    struct ModelRoData_DisplayList_CollisionRecord *src;
    DoorRecord *door;
    s32 k;
    s16 cutoff;
    s32 i;
    s32 j;
    Vertex *cur;
    Vertex *n1;
    Vertex *n2;
    Vertex *n3;
    Vertex *dcur;
    Vertex *d1;
    Vertex *d2;
    Vertex *d3;

    if (inDoor->doorFlags & DOORFLAG_CLIP_TO_BBOX)
    {
        door = inDoor;
        mdl = inDoor->model;
        mdlDLCNode = mdl->obj->RootNode->Child->Child;
        src = (struct ModelRoData_DisplayList_CollisionRecord *)mdlDLCNode->Data;
        inDoor = (DoorRecord *)modelGetNodeRwData(mdl, mdlDLCNode);

        if (door->doorType == DOORTYPE_VERTICAL)
        {
            cutoff = door->bbox.Bounds.ymax + 0.5f;
        }
        else
        {
            cutoff = door->bbox.Bounds.xmin + 0.5f;
        }

        ((struct ModelRwData_DisplayList_CollisionRecord *)inDoor)->Vertices = dynAllocateVertices(src->numVertices);

        for (i = 0; i < src->numVertices / 4; i++)
        {
            for (j = 0; j < 4; j++)
            {
                dcur = &(&((struct ModelRwData_DisplayList_CollisionRecord *)inDoor)->Vertices[i * 4])[j];
                d1 = &(&((struct ModelRwData_DisplayList_CollisionRecord *)inDoor)->Vertices[i * 4])[(j + 1) % 4];
                d2 = &(&((struct ModelRwData_DisplayList_CollisionRecord *)inDoor)->Vertices[i * 4])[(j + 2) % 4];
                d3 = &(&((struct ModelRwData_DisplayList_CollisionRecord *)inDoor)->Vertices[i * 4])[(j + 3) % 4];
                cur = &(&src->Vertices[i * 4])[j];
                n1 = &(&src->Vertices[i * 4])[(j + 1) % 4];
                n2 = &(&src->Vertices[i * 4])[(j + 2) % 4];
                n3 = &(&src->Vertices[i * 4])[(j + 3) % 4];

                if (j == 0)
                {
                    *dcur = *cur;
                    *d1 = *n1;
                    *d2 = *n2;
                    *d3 = *n3;
                    if (1);
                }

                // Rebuild the S/T coords for vertically sliding doors to mask the door's resizing.
                if (door->doorType == DOORTYPE_VERTICAL)
                {
                    if (cur->coord.y >= cutoff)
                    {
                        if (cur->coord.x == n1->coord.x && cur->coord.z == n1->coord.z && cur->coord.y != n1->coord.y)
                        {
                            dcur->s = ((cur->coord.y - cutoff) * (n1->s - cur->s) / (cur->coord.y - n1->coord.y)) + cur->s;
                            dcur->t = ((cur->coord.y - cutoff) * (n1->t - cur->t) / (cur->coord.y - n1->coord.y)) + cur->t;
                        }
                        else if (cur->coord.x == n2->coord.x && cur->coord.z == n2->coord.z && cur->coord.y != n2->coord.y)
                        {
                            dcur->s = ((cur->coord.y - cutoff) * (n2->s - cur->s) / (cur->coord.y - n2->coord.y)) + cur->s;
                            dcur->t = ((cur->coord.y - cutoff) * (n2->t - cur->t) / (cur->coord.y - n2->coord.y)) + cur->t;
                        }
                        else if (cur->coord.x == n3->coord.x && cur->coord.z == n3->coord.z && cur->coord.y != n3->coord.y)
                        {
                            dcur->s = ((cur->coord.y - cutoff) * (n3->s - cur->s) / (cur->coord.y - n3->coord.y)) + cur->s;
                            dcur->t = ((cur->coord.y - cutoff) * (n3->t - cur->t) / (cur->coord.y - n3->coord.y)) + cur->t;
                        }

                        dcur->coord.y = cutoff;
                    }
                }
                // Rebuild the S/T coords for horizontally sliding doors.
                else
                {
                    if (cur->coord.x <= cutoff)
                    {
                        if (cur->coord.y == n1->coord.y && cur->coord.z == n1->coord.z && cur->coord.x != n1->coord.x)
                        {
                            dcur->s = ((cutoff - cur->coord.x) * (n1->s - cur->s) / (n1->coord.x - cur->coord.x)) + cur->s;
                            dcur->t = ((cutoff - cur->coord.x) * (n1->t - cur->t) / (n1->coord.x - cur->coord.x)) + cur->t;
                        }
                        else if (cur->coord.y == n2->coord.y && cur->coord.z == n2->coord.z && cur->coord.x != n2->coord.x)
                        {
                            dcur->s = ((cutoff - cur->coord.x) * (n2->s - cur->s) / (n2->coord.x - cur->coord.x)) + cur->s;
                            dcur->t = ((cutoff - cur->coord.x) * (n2->t - cur->t) / (n2->coord.x - cur->coord.x)) + cur->t;
                        }
                        else if (cur->coord.y == n3->coord.y && cur->coord.z == n3->coord.z && cur->coord.x != n3->coord.x)
                        {
                            dcur->s = ((cutoff - cur->coord.x) * (n3->s - cur->s) / (n3->coord.x - cur->coord.x)) + cur->s;
                            dcur->t = ((cutoff - cur->coord.x) * (n3->t - cur->t) / (n3->coord.x - cur->coord.x)) + cur->t;
                        }

                        dcur->coord.x = cutoff;
                    }
                }
            }
        }
    }
}


/**
 * objToggleDoorPortal / doorActivatePortal
 * Toggles (Open/Closed) the portal linked with door
 * @param door: Door to toggle portal on
 */
void doorActivatePortal(DoorRecord *door)
{
    if (door->portalNumber >= 0)
    {
        bgToggleDataPortalsContrlBytes1Bit1(door->portalNumber, TRUE);
    }
}


/**
 * objToggleDoorPortal / doorDeactivatePortal
 * Toggles (Open/Closed) the portal linked with door
 * @param door: Door to toggle portal on
 */
void doorDeactivatePortal(DoorRecord *door) {
    if (door->portalNumber >= 0)
    {
        bgToggleDataPortalsContrlBytes1Bit1(door->portalNumber, FALSE);
    }
}


PropRecord* doorInit(DoorRecord* door, coord3d* pos, Mtxf* mtx, StandTile* stan, coord3d* coord, coord3d* centre) {
    PropRecord* prop;
    f32 scale;

    prop = objInitWithAutoModel((ObjectRecord* ) door);
    scale = PitemZ_entries[door->obj].scale;
    door->ptr_allocated_collisiondata_block = mempAllocBytesInBank(0x50U, MEMPOOL_STAGE);

    matrix_4x4_copy(mtx, &door->mtx);
    matrix_scalar_multiply(scale, door->mtx.m[0]);

    door->frac  = (f32) coord->x;
    door->unkac = (f32) coord->y;
    door->unkb0 = (f32) coord->z;

    if (door->flags & PROPFLAG_80000000) {
        door->openPosition = door->maxFrac;
    } else {
        door->openPosition = 0.0f;
    }

    door->speed = 0.0f;
    door->openstate = DOORSTATE_STATIONARY;
    door->unkbd = 0;
    door->linkedDoor = NULL;

    if (door->doorFlags & DOORFLAG_CLIP_TO_BBOX) {
        union ModelRoData *rodata = door->model->obj->RootNode->Child->Child->Data;
        door->unkcc = mempAllocBytesInBank(rodata->DisplayListCollisions.numVertices * sizeof(Vertex), MEMPOOL_STAGE);
    } else {
        door->unkcc = NULL;
    }

    door->portalNumber = -1;
    door->openSoundState = 0;
    door->closeSoundState = 0;

    prop->type = PROP_TYPE_DOOR;
    prop->door = door;
    prop->pos.x = pos->x;
    prop->pos.y = pos->y;
    prop->pos.z = pos->z;
    prop->stan = stan;

    door->runtime_pos.x = centre->x;
    door->runtime_pos.y = centre->y;
    door->runtime_pos.z = centre->z;
    door->flags |= PROPFLAG_00000100;

    doorUpdateBbox(door);
    doorBuildClippedVertices(door);
    sub_GAME_7F0402B4(door->prop, &door->nextcol);

    door->shadecol.r = door->nextcol.r;
    door->shadecol.g = door->nextcol.g;
    door->shadecol.b = door->nextcol.b;
    door->shadecol.a = door->nextcol.a;

    return prop;
}


s32 sub_GAME_7F0537B8(f32 distance, f32 min, f32 max)
{
    s32 retval;

    if (distance <= 200.0f)
    {
        retval = SHRT_MAX;
    }
    else if (max <= distance)
    {
        retval = 0.0f;
    }
    else if (min <= distance)
    {
        retval = ((max - distance) * 10000.0f) / (max - min);
    }
    else
    {
        retval = SHRT_MAX - (s32)((sqrtf(distance - 200.0f) * 22767.0f) / sqrtf(min - 200.0f));
    }

    return retval;
}


s32 sub_GAME_7F053894(coord3d *pos, f32 low, f32 high)
{
    PropRecord *prop;
    s32 index;
    f32 shortest_distance;
    f32 diffx;
    f32 diffy;
    f32 diffz;
    f32 distance;
    s32 count;

    shortest_distance = high;
    count = getPlayerCount();

    for (index = 0; index < count; index++)
    {
        prop  = g_playerPointers[index]->prop;
        diffx = prop->pos.x - pos->x;
        diffy = prop->pos.y - pos->y;
        diffz = prop->pos.z - pos->z;
        distance = sqrtf(diffx * diffx + diffy * diffy + diffz * diffz);

        if (distance < shortest_distance)
        {
            shortest_distance = distance;
        }
    }
    return sub_GAME_7F0537B8(shortest_distance, low, high);
}


#ifndef TARGET_N64
/* ---------------------------------------------------------------------------
 * STEREO PANNING of positional sfx (solo only).
 *
 * Vanilla never drives pan from an emitter's position. snd.c starts every
 * voice at soundState->pan == AL_PAN_CENTER plus the bank's static
 * samplePan, and no game code posts AL_SNDP_PAN_EVT, so positional audio is
 * distance ATTENUATION only (sub_GAME_7F053894 -> AL_SNDP_VOL_EVT above).
 * Everything below that was already stereo: port/src/audio_pc.c keeps
 * separate MAIN_L/MAIN_R buses, the envmixer ramps the two channels from
 * independent volume/rate/target triples, and libultrare/audio/env.c turns
 * a pan into an equal-power L/R pair. The only missing piece was a pan
 * value, which is what this block supplies.
 *
 * SOLO ONLY. With two to four viewports there is no single listener: a
 * sound to the left of player 1 is to the right of player 2, and one pair
 * of speakers cannot serve both, so a shared pan would be wrong for
 * everyone but whoever it was computed for. getPlayerCount() >= 2 drops the
 * listener (portSndSetListener below) and every voice stays at the vanilla
 * centre - identical output to before this feature existed.
 * ------------------------------------------------------------------------ */

/* Pan offset at full lateral displacement, out of the 63 that AL_PAN_LEFT /
 * AL_PAN_RIGHT allow either side of AL_PAN_CENTER. Deliberately short of
 * hard panning: a voice that collapses into one speaker as it crosses the
 * screen edge reads as a fault rather than as placement, and GE's own music
 * never pans harder than this. */
#define SND_PAN_SPREAD 48.0f

/* Under this range the direction to an emitter is dominated by its own size
 * and by the camera sitting forward of Bond's chest, so it swings wildly
 * over a step or two for no audible reason; fade the pan back to centre
 * instead. 200 is the engine's own near-field constant - the distance
 * sub_GAME_7F0537B8 subtracts before its sqrt falloff. */
#define SND_PAN_NEARFIELD 200.0f

static coord3d s_SndListenerPos;
static coord3d s_SndListenerRight;
static s32     s_SndListenerValid = 0;

/**
 * Snapshot the listener from the camera that is about to be rendered.
 * Called from bondview.c currentPlayerSetViewToWorldMtxf, the one place the
 * view->world matrix is installed, so the listener always follows what the
 * player is actually looking through - Bond, a cinematic camera, the
 * intro fly-through.
 *
 * The matrix MUST be copied, not held: bondview2.c builds it into
 * g_CurrentPlayer->field_68, which comes from dynAllocateMatrix() - a
 * per-frame display list pool. Keeping the pointer would have the next
 * tick's sound posts read whatever the renderer recycled that memory into.
 *
 * Row 0 is the camera's world-space RIGHT axis and row 3 its world position
 * (matrixmath.c matrix_4x4_set_basis_and_position). View space is +x right:
 * bondview.c transform3Dto2DCoords maps a view point to
 * screen_centre - x/z * k, and z is negative ahead of the camera, so a
 * positive x lands right of centre.
 */
void portSndSetListener(Mtxf *viewToWorld)
{
    if (viewToWorld == NULL || getPlayerCount() >= 2)
    {
        s_SndListenerValid = 0;
        return;
    }

    s_SndListenerRight.x = viewToWorld->m[0][0];
    s_SndListenerRight.y = viewToWorld->m[0][1];
    s_SndListenerRight.z = viewToWorld->m[0][2];

    s_SndListenerPos.x = viewToWorld->m[3][0];
    s_SndListenerPos.y = viewToWorld->m[3][1];
    s_SndListenerPos.z = viewToWorld->m[3][2];

    s_SndListenerValid = 1;
}

/**
 * The pan (AL_PAN_LEFT..AL_PAN_RIGHT, AL_PAN_CENTER when there is nothing
 * to say) for a world position, as heard from the snapshotted listener.
 *
 * The lateral term is the emitter's view-space x divided by its range, i.e.
 * the sine of its azimuth - so a sound directly overhead or directly ahead
 * pans to centre on its own, and one abeam pans fully, without a separate
 * elevation case.
 */
static s32 portSndPanForXYZ(f32 x, f32 y, f32 z)
{
    f32 dx;
    f32 dy;
    f32 dz;
    f32 distance;
    f32 lateral;
    s32 pan;

    if (!s_SndListenerValid)
    {
        return AL_PAN_CENTER;
    }

    dx = x - s_SndListenerPos.x;
    dy = y - s_SndListenerPos.y;
    dz = z - s_SndListenerPos.z;

    distance = sqrtf(dx * dx + dy * dy + dz * dz);
    if (distance < 1.0f)
    {
        return AL_PAN_CENTER; /* on top of the listener: no direction to give */
    }

    lateral = (dx * s_SndListenerRight.x + dy * s_SndListenerRight.y + dz * s_SndListenerRight.z) / distance;

    if (distance < SND_PAN_NEARFIELD)
    {
        lateral *= distance / SND_PAN_NEARFIELD;
    }

    pan = AL_PAN_CENTER + (s32)(lateral * SND_PAN_SPREAD);

    if (pan < AL_PAN_LEFT)
    {
        pan = AL_PAN_LEFT;
    }
    else if (pan > AL_PAN_RIGHT)
    {
        pan = AL_PAN_RIGHT;
    }
    return pan;
}

s32 portSndPanForPos(coord3d *pos)
{
    if (pos == NULL)
    {
        return AL_PAN_CENTER;
    }
    return portSndPanForXYZ(pos->x, pos->y, pos->z);
}

/**
 * Say where one sound is, if that has changed since we last said it.
 * Shared by the owner-driven post and the per-field re-pan, so both agree
 * on what "unchanged" means.
 */
static void portSndPanPost(ALSoundState *state, const f32 *pos)
{
    s32 pan = portSndPanForXYZ(pos[0], pos[1], pos[2]);

    if ((s32) state->pan != pan)
    {
        sndCreatePostEvent(state, AL_SNDP_PAN_EVT, pan);
    }
}

/**
 * Pan one sound to where it is in the world. Sits beside the distance
 * volume post at every positional call site, and posts exactly the way the
 * volume post does - unconditionally on the state the caller holds.
 *
 * Do NOT try to shortcut this by writing ->pan directly on `state` when it
 * is not yet SOUND_STATE_PLAYING. Every state the callers hand over is a
 * SEQUENCE HEAD, and a head is never the state that owns the voice: a
 * 1200-frame Dam capture found 1200 of 1200 posts landing on a state in
 * SOUND_STATE_INIT, so a "only post to a live voice" gate fires never and
 * a direct write lands on the wrong object. snd.c's event loop is what
 * bridges the two - AL_SNDP_PAN_EVT is deliberately absent from its
 * isEventForSingleSound mask, so a pan event walks the whole chain via
 * link.next and each member applies it, with alSynSetPan reached on
 * whichever one is actually playing. Same as AL_SNDP_VOL_EVT.
 *
 * The unchanged-pan early-out is what keeps the cost down: pan is quantised
 * to 127 steps and a continuous emitter (a door, a truck, the heli) posts
 * every frame, so without it this would be a second event stream beside the
 * volume one that snd.c's GE_TICKHZ deferral exists to bound. The head's
 * own ->pan is written by the handler, which is what makes it a valid
 * "what did we last say" record to compare against.
 */
void portSndApplyPan(ALSoundState *state, coord3d *pos)
{
    if (state == NULL || pos == NULL)
    {
        return;
    }

    /* Record the emitter FIRST, and regardless of whether panning is
     * switched on or the listener is up yet: this is the only time most
     * sounds ever tell us where they are, and portSndPanTick below is what
     * turns that one statement into "keeps following the player". Recording
     * unconditionally also means flipping the option on mid-level takes
     * effect on sounds that are already playing. */
    state->portPanPos[0] = pos->x;
    state->portPanPos[1] = pos->y;
    state->portPanPos[2] = pos->z;
    state->portPanTracked = 1;

    if (!s_SndListenerValid || !portStereoPan())
    {
        return;
    }

    portSndPanPost(state, state->portPanPos);
}

/**
 * Re-pan every live tracked sound against the current listener. Called once
 * a FIELD from lv.c, beside sndFlushDeferredVolumes.
 *
 * This is what makes the panning survive a turn. Nearly every positional
 * sfx is a one-shot whose owner posts its position exactly once, at the
 * moment it starts (`chrobjSndCreatePostEventDefault(sndPlaySfx(...), &pos)`
 * is the shape); only the continuous emitters - doors, trucks, the heli,
 * the AI slots - refresh per frame. Without this pass a guard's death
 * scream is pinned to the ear it began in and stays there while the player
 * spins, which is precisely the "sound stops tracking when I turn" report.
 *
 * Walking snd.c's allocated list rather than a table of our own is what
 * keeps the lifetime honest: a finished sound is unlinked from that list by
 * sndUnlinkClearSound, and a recycled state has portPanTracked cleared by
 * sndSetupSound, so there is nothing here to invalidate or evict.
 *
 * Field-scoped by construction, so it cannot flood the event queue at a
 * raised GE_TICKHZ the way a per-tick pass would, and the unchanged-pan
 * check in portSndPanPost means a player standing still posts nothing.
 */
void portSndPanTick(void)
{
    ALSoundState *state;
    s32 guard;

    if (!s_SndListenerValid || !portStereoPan())
    {
        return;
    }

    /* bounded like sndPlayerVoiceHandler's spin net: the list is built by
     * pointer surgery in sndSetupSound, and a walk here must not be what
     * turns a corrupted link into a hang */
    state = sndGetAllocatedList();
    for (guard = 0; state != NULL && guard < 512; guard++)
    {
        if (state->portPanTracked)
        {
            portSndPanPost(state, state->portPanPos);
        }
        state = (ALSoundState *) state->link.next;
    }
}
#endif


#ifndef TARGET_N64
/* How far below the lowest room floor a projectile must fall before it is
 * binned, in ROOM-BOUNDS units (bgGetLevelFloorY's space). Room boxes are
 * tight, so a projectile can dip fractionally under the lowest one while
 * still in a legitimate shaft or pit; a room's worth of clearance below the
 * floor removes that doubt and still fires within a second of a real fall
 * (a knife is accelerating under gravity by then). Expressed in room-bounds
 * units on purpose - it scales with each level's own units, which differ by
 * more than 2x across the game (0.236 on Cradle and Dam, 0.507 on
 * Archives). */
#define PROJECTILE_VOID_MARGIN 200.0f

/**
 * Has this projectile fallen out of the world? See the call site in the
 * thrown-knife tick for why that is worth acting on.
 *
 * Returns FALSE, always, on a level where no room reported a usable
 * bounding box - with no floor to measure against there is no safe answer,
 * and the vanilla lifetime timeout still cleans up eventually.
 */
s32 portProjectileBelowWorld(ObjectRecord *obj)
{
    f32 floorY = bgGetLevelFloorY();

    if (floorY == BG_LEVEL_FLOOR_NONE)
    {
        return FALSE;
    }

    return (obj->runtime_pos.y * get_room_data_float1()) < (floorY - PROJECTILE_VOID_MARGIN);
}
#endif


void chrobjSndCreatePostEvent(ALSoundState *state, coord3d *pos, f32 low, f32 high)
{
    sndCreatePostEvent(state, 8, sub_GAME_7F053894(pos, low, high));
#ifndef TARGET_N64
    /* the choke point nearly every positional sfx already goes through for
     * its distance volume - doors, chrs, guns, explosions, glass, mines,
     * autoguns - so panning them is one call here */
    portSndApplyPan(state, pos);
#endif
}


s32 sub_GAME_7F0539B8(f32 vol)
{
    return sub_GAME_7F0537B8(vol, 5000.0f, 6000.0f);
}


s32 sub_GAME_7F0539E4(coord3d *pos)
{
    return sub_GAME_7F053894(pos, 5000.0f, 6000.0f);
}


void chrobjSndCreatePostEventDefault(ALSoundState *state, coord3d *pos)
{
    chrobjSndCreatePostEvent(state, pos, 5000.0f,  6000.0f);
}


void sub_GAME_7F053A3C(DoorRecord* arg0)
{
    s32 open_playing;
    s32 close_playing;
    s32 sp1C;

    open_playing = (arg0->openSoundState != NULL) && (sndGetPlayingState(arg0->openSoundState) != 0);
    close_playing = (arg0->closeSoundState != NULL) && (sndGetPlayingState(arg0->closeSoundState) != 0);

    if ((open_playing != 0) || (close_playing != 0))
    {

        sp1C = sub_GAME_7F0539E4(&arg0->prop->pos);

        if (lvlGetControlsLockedFlag() != 0)
        {
            sp1C = 0;
        }

        if (open_playing != 0)
        {
            #ifdef DEBUG
            assert( po->audiostate!=NULL);
            #endif
            sndCreatePostEvent(arg0->openSoundState, 8, sp1C);
#ifndef TARGET_N64
            portSndApplyPan(arg0->openSoundState, &arg0->prop->pos);
#endif
        }

        if (close_playing != 0)
        {
            #ifdef DEBUG
            assert( po->audiostate2!=NULL);
            #endif
            sndCreatePostEvent(arg0->closeSoundState, 8, sp1C);
#ifndef TARGET_N64
            portSndApplyPan(arg0->closeSoundState, &arg0->prop->pos);
#endif
        }
    }
}


void door7F053B10(DoorRecord *door) //#MATCH
{
    if (door->openSoundState && sndGetPlayingState(door->openSoundState))
    {
        sndDeactivate(door->openSoundState);
    }

    if (door->closeSoundState && sndGetPlayingState(door->closeSoundState))
    {
        sndDeactivate(door->closeSoundState);
    }
}




void doorPlayOpenSound0(DoorRecord *door) {
    ALSoundState *soundState = NULL;
    ALSoundState *pendingState = NULL;

    door7F053B10(door);

    if (door->openSoundState == NULL)
    {
        pendingState = &door->openSoundState;
    }
    else if (door->closeSoundState == NULL)
    {
        pendingState = &door->closeSoundState;
    }

    switch (door->doorOpenSound)
    {
    case DOOR_OPEN_SOUND_01:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_SLIDE1_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_02:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_METAL:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_LOOP_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_04:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SINGLE_LOOP_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_WOOD:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_OPEN_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_06:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_WOOD_2:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_CATCH_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_WOOD_3:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_09:
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_SHUTTER_OPEN_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_METAL_2:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_OPEN_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_11:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL_3:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_OPEN3_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_13:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_HYDROLIC:
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_HYDRAL_CLOSE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_STONE:
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_SLIDE_STONE_OPEN_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_16:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_OPEN_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL_4:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        if (soundState != NULL)
        {
            chrobjSndCreatePostEventDefault(soundState, &door->prop->pos);
        }
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_LOOP_SFX, pendingState);
        }
        break;
    }

    if (soundState != NULL)
    {
        chrobjSndCreatePostEventDefault(soundState, &door->prop->pos);
    }

    sub_GAME_7F053A3C(door);
}






void doorPlayOpenSound1(DoorRecord *door) {
    ALSoundState *soundState = NULL;
    ALSoundState *pendingState = NULL;

    door7F053B10(door);

    if (door->openSoundState == NULL)
    {
        pendingState = &door->openSoundState;
    }
    else if (door->closeSoundState == NULL)
    {
        pendingState = &door->closeSoundState;
    }

    switch (door->doorOpenSound)
    {
    case DOOR_OPEN_SOUND_01:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_SLIDE1_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_02:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_METAL:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_LOOP_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_04:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SINGLE_LOOP_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_WOOD_2:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_CATCH_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_WOOD_3:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_09:
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_SHUTTER_OPEN_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_13:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_HYDROLIC:
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_HYDRAL_CLOSE_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_STONE:
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_SLIDE_STONE_OPEN_SFX, pendingState);
        }
        break;
    case DOOR_OPEN_SOUND_16:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_OPEN_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL_4:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        if (soundState != NULL)
        {
            chrobjSndCreatePostEventDefault(soundState, &door->prop->pos);
        }
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_OPEN_SFX, NULL);
        if (pendingState != NULL)
        {
            sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_LOOP_SFX, pendingState);
        }
        break;
    }

    if (soundState != NULL)
    {
        chrobjSndCreatePostEventDefault(soundState, &door->prop->pos);
    }

    sub_GAME_7F053A3C(door);
}





void doorPlayCloseSound0(DoorRecord *door) {
    ALSoundState *soundState = NULL;

    door7F053B10(door);

    switch (door->doorOpenSound)
    {
    case DOOR_OPEN_SOUND_01:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_02:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_04:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_WOOD_2:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_WOOD_3:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_09:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SHUTTER_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_13:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_HYDROLIC:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_HYDRAL_OPEN_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_STONE:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SLIDE_STONE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_16:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL_4:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_CLOSE_SFX, NULL);
        break;
    }

    if (door); // Fix for recomp not matching

    if (soundState != NULL)
    {
        chrobjSndCreatePostEventDefault(soundState, &door->prop->pos);
    }

    sub_GAME_7F053A3C(door);
}





void doorPlayCloseSound1(DoorRecord *door)
{
    ALSoundState *soundState = NULL;

    door7F053B10(door);

    switch (door->doorOpenSound)
    {
    case DOOR_OPEN_SOUND_01:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_02:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_04:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_WOOD:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_06:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_WOOD_2:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SMART_CATCH1_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_WOOD_3:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_WOOD_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_09:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SHUTTER_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL_2:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_11:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL_3:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE3_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_13:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, TRAIN_SLIDE_DOOR_SLIDE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_HYDROLIC:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_HYDRAL_OPEN_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_STONE:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, DOOR_SLIDE_STONE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_16:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, HEAVY_SLIDE_CLOSE_SFX, NULL);
        break;
    case DOOR_OPEN_SOUND_METAL_4:
        soundState = sndPlaySfx(g_musicSfxBufferPtr, METAL_SLIDE_CLOSE_SFX, NULL);
        break;
    }

    if (door); // Fix for recomp not matching

    if (soundState != NULL) {
        chrobjSndCreatePostEventDefault(soundState, &door->prop->pos);
    }

    sub_GAME_7F053A3C(door);
}


/**
 * Play the door open sound and activate the door's portal,
 */
void doorStartOpen(DoorRecord *door)
{
    door->flags &= ~PROPFLAG_DOOR_KEEPOPEN;
    door->runtime_bitflags |= RUNTIMEBITFLAG_BEENOPENED;

    doorPlayOpenSound0(door);
    doorActivatePortal(door);

    if (door->doorType == DOORTYPE_FALLAWAY)
    {
        struct collision_data *col = door->ptr_allocated_collisiondata_block;
        door->flags |= PROPFLAG_CANNOT_ACTIVATE;
        door->perimFrac = 0;

        if (col) { col->edges = 0; }
        door->flags &= ~PROPFLAG_00000100;
    }
}


/**
 * Play the door close sound
 */
void doorStartClose(DoorRecord *door)
{
    door->flags &= ~PROPFLAG_DOOR_KEEPOPEN;
    doorPlayOpenSound1(door);
}


void doorFinishOpen(DoorRecord *door)
{
    doorPlayCloseSound0(door);

    if (door->doorType == DOORTYPE_FALLAWAY)
    {
        sub_GAME_7F03FDA8(door->prop);

        if (door->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
        {
            door->projectile->flags |= 1;
            matrix_4x4_set_identity(&door->projectile->mtx);
        }
    }
}


void doorFinishClose(DoorRecord* door)
{
    doorPlayCloseSound1(door);
    doorDeactivatePortal(door);
}


/**
 * Apply the given state to an individual door (not its siblings).
 *
 * Handles playing door open/close sounds and activating the portal if opening.
 */
void doorSetOpenState(DoorRecord *door, s32 newstate)
{
    if (newstate == DOORSTATE_OPENING)
    {
        if (door->openstate == DOORSTATE_STATIONARY || door->openstate == DOORSTATE_WAITING)
        {
            doorStartOpen(door);
        }

        door->openstate = newstate;
    }
    else if (newstate == DOORSTATE_CLOSING)
    {
        if (door->openstate == DOORSTATE_STATIONARY && door->openPosition > 0)
        {
            doorStartClose(door);
        }

        if ((door->openstate != DOORSTATE_STATIONARY && door->openstate != DOORSTATE_WAITING) || door->openPosition > 0)
        {
            door->openstate = newstate;
        }
        else if (door->openstate == DOORSTATE_WAITING)
        {
            door->openstate = DOORSTATE_STATIONARY;
        }
    }
    else
    {
        door->openstate = newstate;
    }
}


void doorActivate(DoorRecord *door, DOORSTATE State) //#MATCH
{
    DoorRecord *linkeddoor;
    DOORSTATE   LinkedState = State;

    if (door->flags2 & 0x40000000) //Close first door before opening second
    {
        if (State == DOORSTATE_OPENING)
        {
            LinkedState = DOORSTATE_CLOSING;
            if (door->openstate == DOORSTATE_STATIONARY)
            {
                State = DOORSTATE_WAITING;
            }
        }
    }

    doorSetOpenState(door, State);

    linkeddoor = door->linkedDoor;

    while (linkeddoor && linkeddoor != door)
    {
        doorSetOpenState(linkeddoor, LinkedState);
        linkeddoor = linkeddoor->linkedDoor;
    };
}


bool doorIsClosed(DoorRecord *door)
{
    return ((door->openstate == DOORSTATE_STATIONARY) || (door->openstate == DOORSTATE_WAITING)) && (door->openPosition <= 0.0f);
}

/*
* Address: 7F054A64
* Description: Computes the 2D bounding box for every room the prop is in
*              so it can be used for scissors. Returns true when the prop
*              has at least one room bounding box.
*/
s32 getPropCombinedRoomsBBox2D(PropRecord *prop, bbox2d *bbox)
{
    s32 room_ids[8];
    s32 *rooms;
    bool result = FALSE;
    s32 room_id;
    bbox2d bbox2;

    chraiGetPropRoomIds(prop, room_ids);
    rooms = room_ids;
    room_id = *rooms;

    while (room_id >= 0)
    {
        if (bgGet2dBboxByRoomId(room_id, &bbox2))
        {
            if (result)
            {
                if (bbox->min.x > bbox2.min.x)
                {
                    bbox->min.x = bbox2.min.x;
                }
                if (bbox->min.y > bbox2.min.y)
                {
                    bbox->min.y = bbox2.min.y;
                }
                if (bbox->max.x < bbox2.max.x)
                {
                    bbox->max.x = bbox2.max.x;
                }
                if (bbox->max.y < bbox2.max.y)
                {
                    bbox->max.y = bbox2.max.y;
                }
            }
            else
            {
                bbox->min.x = bbox2.min.x;
                bbox->min.y = bbox2.min.y;
                bbox->max.x = bbox2.max.x;
                bbox->max.y = bbox2.max.y;
            }
            result = TRUE;
        }
        rooms++;
        room_id = *rooms;
    }

    return result;
}


/**
 * Address 0x7F054B80.
*/
f32 chrobjFogVisRangeRelated(PropRecord *prop, f32 size)
{
    f32 ret;
#if defined(LEFTOVERDEBUG)
    struct NearFogRecord *nfd;
#else
    struct NearFogRecordF *nfd;
#endif
    f32 temp_f12;

    ret = 1.0f;

#ifndef TARGET_N64
    /* midas.ini [display] distance_fading (off by default): this is a
     * SECOND, independent distance-fade curve from fogGetPropDistColor -
     * it fades chr/obj alpha toward 0 (not just a colour tint) as
     * zDepth crosses NearFogRecord's MaxObfuscationRange/MaxVisRange/
     * NearFog thresholds, closer than the room's own fog/cull radius
     * (see docs/port/fog-and-distance.md "chr visibility fade"). Gating
     * only fogGetPropDistColor left this curve running unchecked, so
     * guards and props still visibly faded/vanished with the option
     * "disabled". */
    {
        extern s32 portDistanceFadingEnabled(void);
        if (!portDistanceFadingEnabled())
        {
            return ret;
        }
    }
#endif

    nfd = fogGetNearFogValuesP();

    if ((nfd != NULL) && (nfd->MaxObfuscationRange < prop->zDepth))
    {
        temp_f12 = getPlayer_c_lodscalez();
        temp_f12 = ((((prop->zDepth - nfd->MaxObfuscationRange) * 100.0f) / size) + nfd->MaxObfuscationRange) * temp_f12;

        if (nfd->MaxVisRange <= temp_f12)
        {
            ret = 0.0f; //im invisible
        }
        else
        {
            if (nfd->NearFog < temp_f12)
            {
                ret = (nfd->MaxVisRange - temp_f12) / (nfd->MaxVisRange - nfd->NearFog);// power of fog (0 - 1 ) where 0 is full fog, and 1 is no fog
            }
        }
    }


    return ret;
}


bool sub_GAME_7F054C58(coord3d *coord, f32 arg1)
{
    bool result = TRUE;
    coord3d *ptr;
    coord3d tmp;
    f32 sp20;

#ifndef TARGET_N64
    /* With midas.ini distance_fading off, skip this hard distance cull too,
     * or objects stay fully opaque and then pop out at the cutoff. */
    {
        extern s32 portDistanceFadingEnabled(void);
        if (!portDistanceFadingEnabled())
        {
            return result;
        }
    }
#endif

    ptr = (coord3d*)fogGetNearFogValuesP();

    if (ptr != NULL)
    {
        coord3d *campos = bondviewGetCurrentPlayersPosition();
        Mtxf *mtx = camGetWorldToScreenMtxf();

        tmp.x = coord->x - campos->x;
        tmp.y = coord->y - campos->y;
        tmp.z = coord->z - campos->z;

        sp20 = tmp.f[0] * mtx->m[0][0] + tmp.f[1] * mtx->m[0][1] + tmp.f[2] * mtx->m[0][2];
#ifndef TARGET_N64
        {   /* Measure the cull with the same camera depth the distance fade
             * uses, so objects vanish exactly where the fade reaches 0. Rare's
             * row-0 measure is not depth, and GE_RARECULL=1 restores it. */
            extern char *getenv(const char *);
            static s32 rare = -1;

            if (rare < 0)
            {
                const char *e = getenv("GE_RARECULL");
                rare = (e != NULL && e[0] != '0') ? 1 : 0;
            }
            if (!rare)
            {
                sp20 = -(tmp.f[0] * mtx->m[0][2] + tmp.f[1] * mtx->m[1][2] + tmp.f[2] * mtx->m[2][2]);
            }
        }
#endif

        if (sp20 > ptr->z)
        {
            f32 scalez = getPlayer_c_lodscalez();
            sp20 = ((sp20 - ptr->z) * 100 / arg1 + ptr->z) * scalez;

            if (sp20 >= ptr->y)
            {
                result = FALSE;
            }
        }
    }

    return result;
}


/**
 * Address: 7F054D6C
 */
#ifndef TARGET_N64
/* FADE DIAGNOSTIC (console cl_fadedbg 1): keeps each object's last
 * visibility verdict and logs every change with its depth and the check that
 * decided it, so walking up to a popping object names the cause:
 *   farfog   fogPositionIsVisibleThroughFog said no (Rare's far-fog test)
 *   cull     sub_GAME_7F054C58 said no (the distance cull)
 *   fogsat   fogGetPropDistColor: fully fogged, not drawn
 *   faded    chrobjFogVisRangeRelated: alpha 0, not drawn
 *   fading   drawn with alpha < 255
 *   solid    drawn opaque */
s32 g_PortFadeDbg;
extern s32 portDistanceFadingEnabled(void);   /* video.c */

void portFadeDbgSet(s32 on)
{
    g_PortFadeDbg = on;
}

static void portFadeNote(PropRecord *prop, s32 state, s32 alpha, f32 foga)
{
    static const char *const names[] = { "?", "farfog", "cull", "fogsat", "faded", "fading", "solid" };
    static PropRecord *keys[512];
    static u8 vals[512];
    u32 h = ((u32) (uintptr_t) prop >> 4) & 511;
    s32 k;
    f32 depth;

    for (k = 0; k < 512; k++, h = (h + 1) & 511)
    {
        if (keys[h] == prop || keys[h] == NULL)
        {
            break;
        }
    }
    if (k == 512)
    {
        return;   /* full: a long session's worth of props; quiet rather than wrong */
    }
    if (keys[h] == prop && vals[h] == (u8) state)
    {
        return;
    }
    {   /* the camera depth from the position (zDepth is only set once drawn) */
        Mtxf *m = camGetWorldToScreenMtxf();
        coord3d *c = bondviewGetCurrentPlayersPosition();
        const f32 dx = prop->pos.x - c->x, dy = prop->pos.y - c->y, dz = prop->pos.z - c->z;

        depth = -(dx * m->m[0][2] + dy * m->m[1][2] + dz * m->m[2][2]);
    }
    printf("ge: fade: prop %p type %d model %d depth %.0f: %s -> %s (alpha %d, fog %.2f, fading %s)\n",
           (void *) prop, prop->obj != NULL ? (int) (u8) prop->obj->type : -1,
           prop->obj != NULL ? (int) prop->obj->obj : -1, depth,
           keys[h] == prop ? names[vals[h]] : "new", names[state], (int) alpha, foga,
           portDistanceFadingEnabled() ? "on" : "off");
    keys[h] = prop;
    vals[h] = (u8) state;
}
#endif

bool posIsOnScreen(PropRecord *prop, coord3d *pos, f32 arg2, bool arg3)
{
    s32 room_ids[8];
    s32 *rooms;
    s32 roomnum;
    bool result;
    bbox2d bbox;

    result = FALSE;
    chraiGetPropRoomIds(prop, room_ids);
    rooms = room_ids;
    roomnum = *rooms;

    while (roomnum >= 0)
    {
        if (getROOMID_isRendered(roomnum) != 0)
        {
#ifndef TARGET_N64
            if (g_PortFadeDbg && prop->type == PROP_TYPE_OBJ)
            {
                if (!fogPositionIsVisibleThroughFog(pos, arg2))
                {
                    portFadeNote(prop, 1, 0, 0.0f);
                }
                else if (arg3 && !sub_GAME_7F054C58(pos, arg2))
                {
                    portFadeNote(prop, 2, 0, 0.0f);
                }
            }
#endif
            if (fogPositionIsVisibleThroughFog(pos, arg2) && (!arg3 || sub_GAME_7F054C58(pos, arg2)))
            {
                if (getPropCombinedRoomsBBox2D(prop, &bbox) != 0)
                {
                    result = camIsPosInScreenBox(pos, arg2, &bbox);
                }
                else
                {
                    result = camIsPosInScreen(pos, arg2);
                }

                if (result)
                {
                    coord3d *campos = bondviewGetCurrentPlayersPosition();
                    f32 xdiff = pos->x - campos->x;
                    f32 ydiff = pos->y - campos->y;
                    f32 zdiff = pos->z - campos->z;

                    if (xdiff * xdiff + ydiff * ydiff + zdiff * zdiff > 32000 * 32000)
                    {
                        result = FALSE;
                    }
                }
            }

            break;
        }

        rooms++;
        roomnum = *rooms;
        result = FALSE;
    }

    return result;
}



#ifndef TARGET_N64
/* GE_DOORDBG=1 (diag): one line per completed door motion (opening/closing
 * -> stationary), so door speed can be checked with numbers:
 *   fields  = sum of portAnimTicks() the door actually integrated
 *   expect  = fields a vanilla 60Hz run needs for the same travel
 *             (chrobjApplySpeed re-run with s == 1)
 *   ticks   = distinct g_PortTickSerial values seen while moving
 *   passes  = updateDoorDisplacement calls while moving
 *             (passes > ticks = the ring ran more than once per tick;
 *             fields >> expect = inflated delta)
 *   ms      = wall clock from first moving pass to the finish
 *             (ms * 60 / 1000 == expect means real-time vanilla speed)
 *   t/wall  = g_GlobalTimer and wall-clock ms at the finish, to line up
 *             two runs
 * Slots are per moving door; reversing mid-motion restarts the
 * measurement. No game state is touched. */
struct PortDoorDbg
{
    DoorRecord *door;
    s32 state;
    u32 startCount;
    s32 startTimer;
    s32 lastSerial;
    s32 ticks;
    s32 passes;
    f32 fields;
    f32 startPos;
};

#define PORT_DOORDBG_SLOTS 32
static struct PortDoorDbg s_portDoorDbg[PORT_DOORDBG_SLOTS];

static s32 portDoorDbgOn(void)
{
    static s32 on = -1;
    extern char *getenv(const char *);
    if (on < 0)
    {
        const char *env = getenv("GE_DOORDBG");
        on = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }
    return on;
}

/* vanilla whole-step chrobjApplySpeed from 0 to `dist`, in fields */
static s32 portDoorDbgVanillaFields(f32 dist, f32 accel, f32 decel, f32 maxSpeed)
{
    f32 pos = 0.0f;
    f32 speed = 0.0f;
    s32 n = 0;

    if (dist <= 0.0f || decel <= 0.0f)
    {
        return 0;
    }

    while (pos < dist && n < 100000)
    {
        f32 limit = speed * speed * 0.5f / decel;
        f32 rem = dist - pos;

        n++;

        if (speed > 0.0f && rem <= limit)
        {
            speed -= decel;
            if (speed < decel) speed = decel;
        }
        else if (speed < maxSpeed)
        {
            speed += accel;
            if (speed > maxSpeed) speed = maxSpeed;
        }

        if (speed >= rem)
        {
            break;
        }

        pos += speed;
    }

    return n;
}

static struct PortDoorDbg *portDoorDbgSlot(DoorRecord *door)
{
    s32 i;
    struct PortDoorDbg *freeslot = NULL;

    for (i = 0; i < PORT_DOORDBG_SLOTS; i++)
    {
        if (s_portDoorDbg[i].door == door)
        {
            return &s_portDoorDbg[i];
        }
        if (freeslot == NULL && s_portDoorDbg[i].door == NULL)
        {
            freeslot = &s_portDoorDbg[i];
        }
    }

    return freeslot;
}

/* called from updateDoorDisplacement on every moving pass */
static void portDoorDbgPass(DoorRecord *door)
{
    struct PortDoorDbg *d;

    if (!portDoorDbgOn())
    {
        return;
    }

    d = portDoorDbgSlot(door);
    if (d == NULL)
    {
        return;
    }

    if (d->door != door || d->state != door->openstate)
    {
        d->door = door;
        d->state = door->openstate;
        d->startCount = osGetCount();
        d->startTimer = g_GlobalTimer;
        d->lastSerial = -1;
        d->ticks = 0;
        d->passes = 0;
        d->fields = 0.0f;
        d->startPos = door->openPosition;
    }

    d->passes++;
    d->fields += portAnimTicks();
    if (d->lastSerial != g_PortTickSerial)
    {
        d->lastSerial = g_PortTickSerial;
        d->ticks++;
    }
}

/* called from door7F054FB4 when a door's motion finishes */
static void portDoorDbgFinish(DoorRecord *door)
{
    struct PortDoorDbg *d;
    u32 counts;
    f32 ms;
    f32 dist;
    s32 expect;

    if (!portDoorDbgOn())
    {
        return;
    }

    d = portDoorDbgSlot(door);
    if (d == NULL || d->door != door)
    {
        return;
    }

    counts = osGetCount() - d->startCount;
    ms = (f32) counts / 46875.0f;   /* osGetCount: 46875000 counts/s */
    dist = door->openPosition - d->startPos;
    if (dist < 0.0f) dist = -dist;
    expect = portDoorDbgVanillaFields(dist, door->accel, door->decel, door->maxSpeed);

    fprintf(stderr,
            "doordbg: t=%d wall=%.0f pad=%d type=%d %s travel=%.3f fields=%.2f expect=%d ticks=%d passes=%d ms=%.0f (=%.1f fields@60) timer+%d tickhz=%d spd=%.4f acc=%.5f\n",
            g_GlobalTimer, (f32) osGetCount() / 46875.0f, door->pad, door->doorType,
            (d->state == DOORSTATE_OPENING) ? "OPEN " : "CLOSE",
            dist, d->fields, expect, d->ticks, d->passes,
            ms, ms * 60.0f / 1000.0f, g_GlobalTimer - d->startTimer,
            portTickHz(), door->maxSpeed, door->accel);

    d->door = NULL;
}
#endif

/**
* Loaded to 7F054EA8.
*/
s32 updateDoorDisplacement(DoorRecord* door)
{
    int isMoving = 0;

    if (door->openstate == DOORSTATE_OPENING)
    {
#ifndef TARGET_N64
        portDoorDbgPass(door);
#endif
        chrobjApplySpeed(&door->openPosition, door->maxFrac, &door->speed, door->accel, door->decel, door->maxSpeed);

        if (door->maxFrac <= door->openPosition)
        {
            door->openPosition = door->maxFrac;
        }
        else
        {
            if (door->openPosition <= 0.0f)
            {
                door->openPosition = 0.0f;
            }
        }

        isMoving = 1;
    }
    else if (door->openstate == DOORSTATE_CLOSING)
    {
#ifndef TARGET_N64
        portDoorDbgPass(door);
#endif
        chrobjApplySpeed(&door->openPosition, 0.0f, &door->speed, door->accel, door->decel, door->maxSpeed);

        if (door->maxFrac <= door->openPosition)
        {
            door->openPosition = door->maxFrac;
        }
        else
        {
            if (door->openPosition <= 0.0f)
            {
                door->openPosition = 0.0f;
            }
        }

        isMoving = 1;
    }

    return isMoving;
}



#ifndef TARGET_N64
#define PORT_DOOR_RING_MAX 4

/* A fractional step lets a blocked door creep far closer to a truck or guard than
 * the 60 tick does, and they then catch on it. Also require the ring's whole-field
 * step from the same state to be clear, as the 60 tick would test it. */
static s32 portDoorSubFieldClear(DoorRecord *door, f32 *speeds, s32 count)
{
    extern s32 g_portApplySpeedFieldPaced;
    f32 pos[PORT_DOOR_RING_MAX];
    f32 spd[PORT_DOOR_RING_MAX];
    DoorRecord *d;
    s32 clear = 1;
    f32 ticks = portAnimTicks();
    s32 i;

    /* A whole-field door step is already the step re-tested here. */
    if (count > PORT_DOOR_RING_MAX || !portFastTickFixOn() || ticks == (f32) (s32) ticks)
    {
        return 1;
    }

    for (d = door, i = 0; i < count; d = d->linkedDoor, i++)
    {
        pos[i] = d->openPosition;
        spd[i] = d->speed;

        if (d->openstate == DOORSTATE_OPENING || d->openstate == DOORSTATE_CLOSING)
        {
            d->openPosition = d->lastcalc60f;
            d->speed = speeds[i];
            g_portApplySpeedFieldPaced = 2;
            chrobjApplySpeed(&d->openPosition, (d->openstate == DOORSTATE_OPENING) ? d->maxFrac : 0.0f,
                             &d->speed, d->accel, d->decel, d->maxSpeed);
            g_portApplySpeedFieldPaced = 0;

            if (d->openPosition > d->maxFrac)
            {
                d->openPosition = d->maxFrac;
            }
            else if (d->openPosition < 0.0f)
            {
                d->openPosition = 0.0f;
            }
        }

        doorUpdateBbox(d);
    }

    for (d = door, i = 0; i < count && clear; d = d->linkedDoor, i++)
    {
        clear = sub_GAME_7F0448A8(d->prop);
    }

    for (d = door, i = 0; i < count; d = d->linkedDoor, i++)
    {
        d->openPosition = pos[i];
        d->speed = spd[i];
        doorUpdateBbox(d);
    }

    return clear;
}
#endif

/**
 * NTSC address 0x7F054FB4.
*/
void door7F054FB4(DoorRecord *door)
{
    Model *temp_a0;
    ModelNode *temp_a1;
    s32 var_s4;
    DoorRecord *var_s1;
    s32 var_s5;
    s32 var_a0;

    struct ModelRoData_DisplayList_CollisionRecord *temp_s0;
    struct ModelRwData_DisplayList_CollisionRecord *temp_v0_3;

#ifndef TARGET_N64
    f32 portSpeeds[PORT_DOOR_RING_MAX];
    s32 portCount = 0;
#endif

    var_s4 = 0;
    var_s5 = 1;

    var_s1 = door;
    while (var_s1 != NULL)
    {
        var_s1->lastcalc60f = var_s1->openPosition;
#ifndef TARGET_N64
        if (portCount < PORT_DOOR_RING_MAX)
        {
            portSpeeds[portCount] = var_s1->speed;
        }
        portCount++;
#endif
        if (updateDoorDisplacement(var_s1) != 0)
        {
            var_s4 = 1;
        }

        var_s1 = var_s1->linkedDoor;

        if (var_s1 == door)
        {
            break;
        }
    }

    var_s1 = door;
    if ((var_s4 != 0))
    {
        while (var_s1 != NULL)
        {
            doorUpdateBbox(var_s1);
            var_s5 = sub_GAME_7F0448A8(var_s1->prop);

            if (var_s5 == 0)
            {
                break;
            }

            var_s1 = var_s1->linkedDoor;

            if (var_s1 == door)
            {
                break;
            }
        }
#ifndef TARGET_N64
        if (var_s5 != 0)
        {
            var_s5 = portDoorSubFieldClear(door, portSpeeds, portCount);
        }
#endif
    }

    var_s1 = door;
    while (var_s1 != NULL)
    {
        if (var_s4)
        {
            if (var_s5 != 0)
            {
                if (var_s1->openstate == DOORSTATE_OPENING)
                {
                    if (var_s1->maxFrac <= var_s1->openPosition)
                    {
#ifndef TARGET_N64
                        portDoorDbgFinish(var_s1);
#endif
                        var_s1->openstate = DOORSTATE_STATIONARY;
                        var_s1->speed = 0.0f;
                        var_s1->openedTime = (u32) g_GlobalTimer;

                        doorFinishOpen(var_s1);
                    }
                }
                else if ((var_s1->openstate == DOORSTATE_CLOSING) && (var_s1->openPosition <= 0.0f))
                {
#ifndef TARGET_N64
                    portDoorDbgFinish(var_s1);
#endif
                    var_s1->openstate = DOORSTATE_STATIONARY;
                    var_s1->speed = 0.0f;
                    var_s1->openedTime = 0;

                    doorFinishClose(var_s1);
                }

                sub_GAME_7F0402B4(var_s1->prop, &var_s1->nextcol);
            }
            else
            {
                var_s1->speed = 0.0f;
                var_s1->openPosition = var_s1->lastcalc60f;

                doorUpdateBbox(var_s1);
            }

            doorBuildClippedVertices(var_s1);
        }
        else if  (var_s1->doorFlags & DOORFLAG_CLIP_TO_BBOX)
        {
            temp_a0 = var_s1->model;
            temp_a1 = temp_a0->obj->RootNode->Child->Child;
            temp_s0 = (struct ModelRoData_DisplayList_CollisionRecord *)temp_a1->Data;
            temp_v0_3 = (struct ModelRwData_DisplayList_CollisionRecord*)modelGetNodeRwData(temp_a0, temp_a1);

            if (temp_v0_3->Vertices != var_s1->unkcc)
            {
                for (var_a0 = 0; var_a0 < temp_s0->numVertices; var_a0++)
                {
                    // struct copy
                    var_s1->unkcc[var_a0] = temp_v0_3->Vertices[var_a0];
                }
#ifndef TARGET_N64
                { /* port: unkcc is rewritten in place with the stopped door's
                   * clipped shape, so a GPU-retained copy captured earlier is
                   * stale and must be dropped (roc_hooks.c; otherwise e.g.
                   * Train's closed door draws as a sliver) */
                    extern void portPartCacheVerticesFreed(const void *vtx);
                    portPartCacheVerticesFreed(var_s1->unkcc);
                }
#endif
            }

            temp_v0_3->Vertices = var_s1->unkcc;
        }

#ifndef TARGET_N64
        var_s1->lastcalc60i = g_PortTickSerial;
#else
        var_s1->lastcalc60i = g_GlobalTimer;
#endif

        var_s1 = var_s1->linkedDoor;

        if (var_s1 == door)
        {
            break;
        }
    }
}


// PD: door0f08f604
void door7F05522C(DoorRecord *door, f32 *arg1, f32 *arg2, s32 altcoordsystem)
{
    f32 anglediff;
    PropRecord *playerprop;
    BoundPadRecord *pad;
    coord3d backImpactPuffCount;
    coord3d normal;
    f32 xmin;
    f32 xmax;
    coord3d playerpos;
    f32 angle2;
    f32 cosine;
    f32 sine;
    f32 angle;
    f32 y1;
    f32 x1;
    f32 playerangle;
    f32 anglediff2;
    f32 scale;
    f32 xbound;

    pad = &g_CurrentSetup.boundpads[door->pad];
    playerprop = getCurrentPlayerProp();

    if (1) { scale = 1.0f; }
    playerpos.f[0] = (((g_CurrentPlayer->field_488.theta_transform.x * 30.0f) * scale) * 0.75f) + playerprop->pos.x;
    playerpos.f[1] = playerprop->pos.y;
    playerpos.f[2] = (((g_CurrentPlayer->field_488.theta_transform.z * 30.0f) * scale) * 0.75f) + playerprop->pos.z;

    if (altcoordsystem != 0)
    {
        xmin = pad->bbox.xmin;
        xmax = pad->bbox.xmax;
        normal.f[0] = (pad->up.y * pad->look.z) - (pad->look.y * pad->up.z);
        normal.f[1] = (pad->up.z * pad->look.x) - (pad->look.z * pad->up.x);
        normal.f[2] = (pad->up.x * pad->look.y) - (pad->look.x * pad->up.y);
    }
    else
    {
        xmin = pad->bbox.ymin;
        xmax = pad->bbox.ymax;
        normal.f[0] = pad->up.x;
        normal.f[1] = pad->up.y;
        normal.f[2] = pad->up.z;
    }

    x1 = (pad->pos.x + (normal.x * xmin)) - playerpos.x;
    y1 = (pad->pos.z + (normal.z * xmin)) - playerpos.z;
    angle = atan2f(x1, y1);

    playerangle = bondviewGetPlayerYawRadians();
    anglediff = angle - playerangle;

    scale = (angle - playerangle) + M_TAU_F;
    if (angle < playerangle)
    {
        anglediff = scale;
    }

    if (anglediff > M_PI_F)
    {
        anglediff = anglediff - M_TAU_F;
    }

    if (door->doorType == DOORTYPE_SWINGING)
    {
        angle2 = (door->openPosition * M_TAU_F) / 360.0f;

        if (door->flags & 0x20000000)
        {
            angle2 = M_TAU_F - angle2;
        }

        cosine = cosf(angle2);
        sine = sinf(angle2);

        xbound = xmax - xmin;
        x1 = ((pad->pos.x + (normal.x * xmin)) + (xbound * ((normal.x * cosine) + (normal.z * sine)))) - playerpos.x;
        y1 = ((pad->pos.z + (normal.z * xmin)) + (xbound * (((-normal.x) * sine) + (normal.z * cosine)))) - playerpos.z;

        angle = atan2f(x1, y1);
        playerangle = bondviewGetPlayerYawRadians();

        anglediff2 = angle - playerangle;
        if (angle < playerangle)
        {
            anglediff2 += M_TAU_F;
        }
        if (anglediff2 > M_PI_F)
        {
            anglediff2 -= M_TAU_F;
        }
    }
    else
    {
        x1 = (pad->pos.x + (normal.x * xmax)) - playerpos.x;
        y1 = (pad->pos.z + (normal.z * xmax)) - playerpos.z;

        angle = atan2f(x1, y1);
        playerangle = bondviewGetPlayerYawRadians();

        anglediff2 = angle - playerangle;

        if (normal.x);
        if (xmax);

        if (angle < playerangle)
        {
            anglediff2 += M_TAU_F;
        }

        if (anglediff2 > M_PI_F)
        {
            anglediff2 -= M_TAU_F;
        }
    }

    if (anglediff < anglediff2)
    {
        *arg1 = anglediff;
        *arg2 = anglediff2;
    }
    else
    {
        *arg1 = anglediff2;
        *arg2 = anglediff;
    }
}


// PD: func0f08f968
bool door7F0555F8(DoorRecord *door, bool altcoordsystem)
{
    bool checkmore;
    f32 sp50;
    f32 sp4c;
    DoorRecord *sibling;
    f32 limit;
    f32 sp40;
    f32 sp3c;

    checkmore = TRUE;
    limit = 0.34906587f;

    if (g_InteractProp == NULL)
    {
        door7F05522C(door, &sp50, &sp4c, altcoordsystem);

        if ((sp50 >= -limit) && (sp50 <= limit) && (sp4c >= -limit) && (sp4c <= limit))
        {
            g_InteractProp = door->prop;
            checkmore = FALSE;
        }
        else
        {
            sibling = door->linkedDoor;

            while (sibling != NULL && sibling != door && (sp50 >= 0.0f || sp4c < 0.0f))
            {
                door7F05522C(sibling, &sp40, &sp3c, altcoordsystem);

                if ((sp50 > 0.0f) && (sp40 < sp50))
                {
                    sp50 = sp40;
                }

                if ((sp4c < 0.0f) && (sp4c < sp3c))
                {
                    sp4c = sp3c;
                }

                sibling = sibling->linkedDoor;
            }

            if ((sp4c - sp50) < M_PI_F && (sp50 < 0.0f) && (sp4c > 0.0f))
            {
                g_InteractProp = door->prop;
                checkmore = FALSE;
            }
        }
    }

    return checkmore;
}


bool doorTestForInteract(PropRecord *prop)
{
	bool checkmore;
	DoorRecord *door;
    bool maybe;
    PropRecord *playerprop;
    f32 xdiff;
    f32 ydiff;
    f32 zdiff;
    BoundPadRecord *boundpads;
    u8 rooms1[32];
    u8 rooms2[32];
    s32 unused[2];

    checkmore = TRUE;
    door = prop->door;

	if ((door->flags & PROPFLAG_CANNOT_ACTIVATE) == 0
			&& door->maxFrac > 0
			&& (prop->flags & PROPFLAG_ONSCREEN))
    {
		maybe = FALSE;
		playerprop = getCurrentPlayerProp();

		xdiff = door->runtime_pos.x - playerprop->pos.x;
		ydiff = door->runtime_pos.y - playerprop->pos.y;
		zdiff = door->runtime_pos.z - playerprop->pos.z;

		if (xdiff * xdiff + zdiff * zdiff < 40000.0f && ydiff < 200.0f && ydiff > -200.0f)
        {
			maybe = TRUE;
		}
        else
        {
            chraiGetPropRoomIds(prop, (s32*)rooms1);
            chraiGetPropRoomIds(playerprop, (s32*)rooms2);
            if (sub_GAME_7F03DB70((s32*)rooms1, (s32*)rooms2) != 0)
            {
                boundpads = &g_CurrentSetup.boundpads[door->pad];
                if (chrpropTestPointInPaddedBoundPad(&playerprop->pos, 150.0f, boundpads))
                {
                    maybe = TRUE;
                }
            }
		}

		if (maybe)
        {
            checkmore = door7F0555F8(door, FALSE);

            if (checkmore && (door->flags2 & PROPFLAG2_DOOR_ALTCOORDSYSTEM))
            {
                checkmore = door7F0555F8(door, TRUE);
            }
		}
	}

	return checkmore;
}


void doorActivateWrapper(PropRecord *prop) //#MATCH
{
    DoorRecord *door = prop->door;

    if ((door->openstate == DOORSTATE_OPENING) || (door->openstate == DOORSTATE_WAITING))
    {
        doorActivate(door, DOORSTATE_CLOSING);
    }
    else if (door->openstate == DOORSTATE_CLOSING)
    {
        doorActivate(door, DOORSTATE_OPENING);
    }
    else if (door->openstate == DOORSTATE_STATIONARY)
    {
        if (door->openPosition > 0.5f)
        {
            doorActivate(door, DOORSTATE_CLOSING);
        }
        else
        {
            doorActivate(door, DOORSTATE_OPENING);
        }
    }
    door->runtime_bitflags |= RUNTIMEBITFLAG_ACTIVATED;
    door->flags2 &= ~8;
    sub_GAME_7F03E6A0(prop);
}


bool posIsInFrontOfDoor(PropRecord *prop, DoorRecord *door)
{
    BoundPadRecord *pad;
    coord3d diff;
    coord3d normal;
    f32 dot;
    f32 side;

    pad = &g_CurrentSetup.boundpads[((ObjectRecord *) door)->pad];

    normal.f[0] = (pad->up.y * pad->look.z) - (pad->look.y * pad->up.z);
    normal.f[1] = (pad->up.z * pad->look.x) - (pad->look.z * pad->up.x);
    normal.f[2] = (pad->up.x * pad->look.y) - (pad->look.x * pad->up.y);

    diff.x = prop->pos.x - pad->pos.x;
    diff.y = prop->pos.y - pad->pos.y;
    diff.z = prop->pos.z - pad->pos.z;

    dot = (side = ((diff.x * normal.f[0]) + (diff.y * normal.f[1])) + (diff.z * normal.f[2]));

    if (door->doorFlags & DOORFLAG_FLIP)
    {
        side = -dot;
    }

    if (side < 0.0f)
    {
        return FALSE;
    }
    else if (side > 0.0f)
    {
        return TRUE;
    }
    else
    {
        return TRUE;
    }
}


void doorsChooseSwingDirection(PropRecord *chrprop, DoorRecord *door)
{
    if ((door->flags & PROPFLAG_DOOR_TWOWAY) && door->openstate == PROPSTATE_NONE && door->openPosition == 0.0f)
    {
        bool infront = posIsInFrontOfDoor(chrprop, door);
        u32 wantflag = 0;

        if ((door->doorFlags & DOORFLAG_FLIP) == 0)
        {
            if (!infront)
            {
                wantflag = PROPFLAG_DOOR_OPENTOFRONT;
            }
        }
        else
        {
            if (infront)
            {
                wantflag = PROPFLAG_DOOR_OPENTOFRONT;
            }
        }

        // If flags are different
        if ((s32)((door->flags ^ wantflag) << 2) < 0)
        {
            // Toggle direction on door and siblings
            DoorRecord *sibling = door;

            do
            {
                sibling->flags ^= PROPFLAG_DOOR_OPENTOFRONT;
                sibling = sibling->linkedDoor;
            } while (sibling && sibling != door);
        }
    }
}


TICKOP propdoorInteract(PropRecord* doorprop)
{
    s32 unused;
    s32 sp28;
    PropRecord* playerprop;
    DoorRecord* door;
    textoverride* txt;

    door = doorprop->door;
    sp28 = 0;
    playerprop = getCurrentPlayerProp();

    if (door->keyflags == 0)
    {
        sp28 = 1;
    }
    else if (bondinvCheckHasKeyFlags(door->keyflags) != 0)
    {
        sp28 = 1;
    }
    else if (posIsInFrontOfDoor(playerprop, door) != 0)
    {
        if ((door->flags2 & PROPFLAG2_10000000) && !(door->flags2 & PROPFLAG2_08000000))
        {
            sp28 = 1;
        }
    }
    else if (!(door->flags2 & PROPFLAG2_10000000) && (door->flags2 & PROPFLAG2_08000000))
    {
        sp28 = 1;
    }

    if (doorIsPadlockFree(door) == 0)
    {
        sp28 = 0;
    }
#ifndef TARGET_N64
    if (cheatIsActive(CHEAT_PORT_UNLOCK_DOORS))
    {   /* UNLOCK DOORS (the cheat menu's PORT tab): the
         * player opens every door - no key, an unshot padlock, the locked
         * side of a one-way door. Guards' own door use is not this path. */
        sp28 = 1;
    }
#endif

    if (sp28 != 0)
    {
        doorsChooseSwingDirection(playerprop, door);
        doorActivateWrapper(doorprop);
    }
    else if ((door->openstate == DOORSTATE_STATIONARY) && (door->openPosition < 0.5f))
    {
        if (!(door->flags2 & PROPFLAG2_00000004))
        {
            txt = bondinvGetTextbyObj((ObjectRecord*)door);
            if ((txt != NULL) && (txt->pickuptext != 0))
            {
#ifdef VERSION_US
                hudmsgBottomShow(langGet((s32) txt->pickuptext));
#else
                jp_hudmsgBottomShow(langGet((s32) txt->pickuptext));
#endif
            }
            else
            {
#ifdef VERSION_US
                hudmsgBottomShow(langGet(0xA440));
#else
                jp_hudmsgBottomShow(langGet(0xA440));
#endif
            }
        }
        door->runtime_bitflags |= RUNTIMEBITFLAG_ACTIVATED;
        door->flags2 |= PROPFLAG2_00000008;
        #ifdef XBLA
        //part of showing key marker
            if ((door->keyflags) && (get_current_difficulty() == DIFFICULTY_AGENT)) {
                Function_822881F8(door->keyflags);
            }
        #endif
    }

    return TICKOP_NONE;
}


void alarmActivate(void)
{
    if (alarm_timer < 1) {
        alarm_timer = 1;
    }
    return;
}


void deactivate_alarm_sound_effect(void)
{
    if ((ptr_alarm_sfx != 0) && (sndGetPlayingState(ptr_alarm_sfx) != AL_STOPPED)) {
        sndDeactivate(ptr_alarm_sfx);
    }
    return;
}


void alarmDeactivate(void)
{
  alarm_timer = 0;
  deactivate_alarm_sound_effect();
  return;
}


bool alarmIsActive(void)
{
  return (0 < alarm_timer);
}


void init_trigger_toxic_gas_effect(coord3d *source) //#MATCH
{
    activate_gas_sound_timer = TRUE;
    gasLeakTimer               = 0.0f;
    gasLeakSource.x             = source->x;
    gasLeakSource.y             = source->y;
    gasLeakSource.z             = source->z;
    if (bossGetStageNum() == LEVELID_EGYPT)
    {
        gasTimeToFullOpacity = 120.0f;
        gasDoesDamageFlag = FALSE;
        return;
    }
    gasTimeToFullOpacity = 3600.0f;
    gasDoesDamageFlag = TRUE;
}


void check_deactivate_gas_sound(void)
{
    if ((ptr_gas_sound != NULL) && (sndGetPlayingState(ptr_gas_sound) != AL_STOPPED)) {
        sndDeactivate(ptr_gas_sound);
    }
    return;
}


bool check_if_toxic_gas_activated() //#MATCH
{
    return (toxic_gas_sound_timer > 0);
}


void handle_gas_damage(void)
{
    if (activate_gas_sound_timer != 0)
    {
        toxic_gas_sound_timer += g_GlobalTimerDelta;
        if (gasTimeToFullOpacity <= toxic_gas_sound_timer)
        {
            toxic_gas_sound_timer = gasTimeToFullOpacity;
            activate_gas_sound_timer = 0;
        }
    }

    if (toxic_gas_sound_timer > 0.0f && g_PlayerInvincible == FALSE)
    {
        fogSwitchToSolosky2(toxic_gas_sound_timer / gasTimeToFullOpacity);

        if (gasDoesDamageFlag == 0) { return; }

#ifdef VERSION_EU
        if (D_80030ADC < (g_GlobalTimer - 0xBB))
#else
        if (D_80030ADC < (g_GlobalTimer - 0xE1))
#endif
        {
            D_80030ADC = g_GlobalTimer;
            if (toxic_gas_sound_timer >= 600.0f)
            {
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, COUGH_SFX, NULL);
            }
            if (toxic_gas_sound_timer >= 1800.0f)
            {
                record_damage_kills(0.125f, 0.0f, 0.0f, -1, 0);
            }
        }

        if (gasLeakTimer < gasTimeToFullOpacity)
        {
            gasLeakTimer = gasLeakTimer + g_GlobalTimerDelta;
            if ((ptr_gas_sound == NULL) && (lvlGetControlsLockedFlag() == 0))
            {
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, GAS_HISS_SFX, (ALSoundState* ) &ptr_gas_sound);
            }
            if (ptr_gas_sound != NULL)
            {
                chrobjSndCreatePostEventDefault(ptr_gas_sound, &gasLeakSource);
            }
        }
        else
        {
            if ((ptr_gas_sound != NULL) && (sndGetPlayingState(ptr_gas_sound) != 0))
            {
                sndDeactivate(ptr_gas_sound);
            }
        }
    }
}


void countdownTimerSetVisible(int clocklockbits, bool unset)
{
    if (unset)
    {
        clock_drawn_flag &= ~clocklockbits;
        return;
    }
    clock_drawn_flag |= clocklockbits;
}

bool is_clock_drawn_onscreen(void)
{
    return clock_drawn_flag == FALSE;
}

void countdownTimerSetValue(f32 time)
{
    clock_time = time;
}

f32 countdownTimerGetValue(void)
{
    return clock_time;
}

void countdownTimerSetRunning(bool enable)
{
    clock_enable = enable;
}

bool countdownTimerIsRunning(void)
{
    return clock_enable;
}

void if_enabled_reset_clock(void)
{
    if (clock_enable != 0) {
        clock_time = clock_time - g_GlobalTimerDelta;
    }
}

const char D_80052A44[] = ":\n";

/*
    Renders the on-screen countdown timer
    using minutes, seconds and milliseconds
    in the following format

    00 : 00 : 00

    Timer value is set using countdownTimerSetValue()
*/
Gfx *countdownTimerRender(Gfx *DL)
{
    s32 mins;
    s32 secs;
    s32 ms;
    s32 valign_offset;
    s32 unused;
    f32 time;

    if (clock_drawn_flag == 0) {

        time = clock_time;
        if (time < 0.0f) {
            time = -time;
        }

        mins = (s32) floorFloat(time / 3600.0f);
        secs = (s32) floorFloat(time / 60.0f) - (mins * 60);
        ms = ((s32) floorFloat((time * 100.0f) / 60.0f) - (mins * 6000)) - (secs * 100);

#ifndef TARGET_N64
        {   /* HUD Scale (options.c): the clock is laid out about the screen's
             * centre column and the view's bottom */
            extern Gfx *portHudScaleBegin(Gfx *gdl, f32 x, f32 y);

            DL = portHudScaleBegin(DL, (f32) viGetX() * 0.5f, (f32) (viGetViewTop() + viGetViewHeight()));
        }
#endif
        DL = microcode_constructor(DL);

        #if defined(VERSION_US) || defined(VERSION_JP)
            valign_offset = 18;
        #else
            valign_offset = 28;
        #endif

        // Minutes
        DL = gunDrawHudInteger(DL, (mins % 100) / 10, 0x82, HUDHALIGN_MIDDLE, ( viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);
        DL = gunDrawHudInteger(DL, mins % 10, 0x8A, HUDHALIGN_MIDDLE, ( viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);

        // :
        DL = gunDrawHudString(DL, &D_80052A44, 0x93, HUDHALIGN_MIDDLE, (viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);

        // Seconds
        DL = gunDrawHudInteger(DL, (secs % 60) / 10, 0x9C, HUDHALIGN_MIDDLE, (viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);
        DL = gunDrawHudInteger(DL, secs % 10, 0xA4, HUDHALIGN_MIDDLE, (viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);

        // :
        DL = gunDrawHudString(DL, &D_80052A44, 0xAD, HUDHALIGN_MIDDLE, (viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);

        // Milliseconds
        DL = gunDrawHudInteger(DL, (ms % 100) / 10, 0xB6, HUDHALIGN_MIDDLE, (viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);
        DL = gunDrawHudInteger(DL, ms % 10, 0xBE, HUDHALIGN_MIDDLE, (viGetViewTop() + viGetViewHeight()) - valign_offset, HUDVALIGN_MIDDLE, 1);

        DL = combiner_bayer_lod_perspective(DL);
#ifndef TARGET_N64
        {
            extern Gfx *portHudScaleEnd(Gfx *gdl);

            DL = portHudScaleEnd(DL);
        }
#endif
    }

    return DL;
}


void handle_alarm_gas_timer_calldamage(void)
{
    if (alarmIsActive() != 0)
    {
        if ((ptr_alarm_sfx == 0) && (lvlGetControlsLockedFlag() == 0))
        {
            sndPlaySfx(g_musicSfxBufferPtr, ALARM3_SFX, &ptr_alarm_sfx);
        }

        alarm_timer = alarm_timer + g_ClockTimer;

        if (CHROBJ_GAS_TIMER < alarm_timer)
        {
            alarmDeactivate();
        }
    }

    handle_gas_damage();
    if_enabled_reset_clock();
    check_guard_detonate_proxmine();
    g_RemoteMineOwnerTriggerFlag = 0;

    return;
}


void sub_GAME_7F056690(void)
{
    Model *s3;
    PropRecord *s2;
    ObjectRecord *s1;
    union ModelRwData **new_var;
    ModelNode *t0;
    ModelRoData_DisplayList_CollisionRecord *s0;

    s2 = chrpropGetActiveTail();
    for (; s2 != NULL; s2 = s2->prev)
    {
        if ((s2->type == 1) && ((s2->flags & 2) == 0))
        {
            s1 = s2->obj;
            if (s1->state & 0x80)
            {
                s3 = s1->model;
                t0 = sub_GAME_7F04B478(s1);
                if (t0 == NULL)
                {
                    return;
                    t0 = sub_GAME_7F04B478(s1);
                }
                s0 = (ModelRoData_DisplayList_CollisionRecord *)t0->Data;
                if (s0 == NULL)
                {
                    return;
                }
                if (sub_GAME_7F04B590(s1->model->obj, t0) != 0)
                {
                    new_var = &s3->datas[s0->RwDataIndex];
                    if ((intptr_t)s0->Vertices != (intptr_t)*new_var)
                    {
                        objFreePermanently(s1, 1);
                        return;
                    }
                }
            }
        }
    }
}


void drop_inventory(void)
{
    ChrRecord *playerchr;
    PropRecord *prop;
    enum ITEM_IDS item;
    enum PROP propid;

    playerchr = g_CurrentPlayer->prop->chr;

    chrSetWeaponFlag4(playerchr, GUNRIGHT);
    chrSetWeaponFlag4(playerchr, GUNLEFT);

    for (item = ITEM_FIST; item != ITEM_IDS_MAX; item++)
    {
        propid = getPropForHeldItem(item);

        /* (s32): gcc's unsigned enum PROP defeats a plain >= 0 for the
         * -1 sentinel (see bondview.c hand-init note) */
        if (((s32) propid >= 0) && (bondinvHasInvItem(item) != 0))
        {
            prop = something_with_generating_object(playerchr, propid, item, 0x20000000, NULL, NULL);

            if (prop != NULL)
            {
                propobjSetDropped(prop, DROPTYPE_DEFAULT);
                objDrop(prop);
            }
        }
    }
}
