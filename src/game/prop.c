#include <ultra64.h>
#include "game/bondinv.h"
#include "game/cheat.h"
#include "game/deb_loadallmodels.h"
#include "game/front.h"
#include "game/initguards.h"
#include "game/initunk_005520.h"
#include "game/lv.h"
#include <memp.h>
#ifndef TARGET_N64
#include <stddef.h> /* offsetof - the weapon-alias contract pin */
#include <stdio.h>  /* portLintSetup reports */
#include <stdlib.h> /* malloc - portRestoreSetupRegion snapshots */
#include <string.h>
#endif
#include "game/mp_weapon.h"
#include "game/bondview_r.h"
#include "bg.h"
#include "bondview_r.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "propobj.h"
#include "inititemslots.h"
#include "initobjects.h"
#include "initpathtablesomething.h"
#include "limits.h"
#include "loadobjectmodel.h"
#include "language.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "mp_weapon.h"
#include "ob.h"
#include "objective.h"
#include "objective_status.h"
#include "objecthandler.h"
#include "player.h"
#include "prop.h"
#include "stan.h"
#include "model.h"
#include "token.h"

/**
 * EU .bss 0x80068480
*/
ITEM_IDS lastmpweaponnum;

// redeclare with the element count so ARRAYCOUNT works in proplvreset2
extern ItemModelFileRecord PitemZ_entries[341];

// forward declarations

s32 load_proptype(PROPDEF_TYPE type);
void sub_GAME_7F001BD4(struct BoundPadRecord *pad, struct coord3d *arg1);
void domakedefaultobj(s32 arg0, ObjectRecord *arg1, s32 cmdindex);
void weaponAssignToHome(s32 arg0, WeaponObjRecord* weapon, s32 cmdindex);
void setupHat(s32 arg0, ObjectRecord* hat, s32 cmdindex);
void setupKey(s32 arg0, ObjectRecord* key, s32 cmdindex);
void setupCctv(s32 arg0, CCTVRecord *arg1, s32 cmdindex);
void setupAutogun(s32 stageID, AutogunRecord *autogun, s32 cmdindex);
void setupHangingMonitors(s32 arg0, ObjectRecord* rack, s32 cmdindex);
void setupSingleMonitor(s32 stageID, MonitorObjRecord *monitor, s32 cmdindex);
void setupMultiMonitor(s32 stageID, MultiMonitorObjRecord* monitor, s32 cmdindex);
void sub_GAME_7F00324C(struct BoundPadRecord *arg0, s32 *arg1, s32 *arg2, struct coord3d *arg3, struct coord3d *arg4);
void setupDoor(s32 arg0, struct DoorRecord *door, s32 arg2);


s32 load_proptype(PROPDEF_TYPE type)
{
    PropDefHeaderRecord *propdef = (PropDefHeaderRecord *) g_CurrentSetup.propDefs;
    s32 count = 0;

    if (propdef != NULL)
    {
        while (propdef->type != PROPDEF_END)
        {
            if (propdef->type == (type & 0xFF))
            {
                count ++;
            }
            propdef = &propdef[sizepropdef((PropDefHeaderRecord* ) propdef)];
        }
    }
    return count;
}


/**
 * perfect dark padGetCentre (pad.c)
 *
 * NTSC address 0x7F001BD4.
*/
void sub_GAME_7F001BD4(struct BoundPadRecord *pad, struct coord3d *arg1)
{
    struct coord3d normal;
    f32 scale;
    struct bbox bb;
    f32 temp;

    bb.zmax = pad->bbox.xmin;
    bb.zmin = pad->bbox.xmax;
    bb.ymax = pad->bbox.ymin;
    bb.ymin = pad->bbox.ymax;
    bb.xmax = pad->bbox.zmin;
    bb.xmin = pad->bbox.zmax;

    normal.f[0] = (pad->up.f[1] * pad->look.f[2]) - (pad->up.f[2] * pad->look.f[1]);
    normal.f[1] = (pad->up.f[2] * pad->look.f[0]) - (pad->up.f[0] * pad->look.f[2]);
    normal.f[2] = (pad->up.f[0] * pad->look.f[1]) - (pad->up.f[1] * pad->look.f[0]);

    temp = (normal.f[0] * normal.f[0]) + (normal.f[1] * normal.f[1]) + (normal.f[2] * normal.f[2]);
    scale = 1.0f / sqrtf(temp);

    normal.f[0] *= scale;
    normal.f[1] *= scale;
    normal.f[2] *= scale;

    arg1->f[0] = pad->pos.f[0] + (
			(bb.zmax + bb.zmin) * normal.f[0] +
			(bb.ymax + bb.ymin) * pad->up.f[0] +
			(bb.xmax + bb.xmin) * pad->look.f[0]) * 0.5f;

	arg1->f[1] = pad->pos.f[1] + (
			(bb.zmax + bb.zmin) * normal.f[1] +
			(bb.ymax + bb.ymin) * pad->up.f[1] +
			(bb.xmax + bb.xmin) * pad->look.f[1]) * 0.5f;

	arg1->f[2] = pad->pos.f[2] + (
			(bb.zmax + bb.zmin) * normal.f[2] +
			(bb.ymax + bb.ymin) * pad->up.f[2] +
			(bb.xmax + bb.xmin) * pad->look.f[2]) * 0.5f;

}

/**
 * NTSC address 0x7F001D9C.
*/
#ifndef TARGET_N64
/* per-load counter for the "prop not positioned" diagnostic below */
s32 g_PortNotPositioned;
#endif

void domakedefaultobj(s32 arg0, ObjectRecord *arg1, s32 cmdindex)
{
    s32 padding;
    s32 spF0;
    f32 var_f0;
    struct coord3d spE0;
    struct StandTile *spDC;
    struct coord3d spD0;
    StandTile *spCC;
    Mtxf sp8C;
    struct coord3d sp80;
    struct PropRecord *var_v0;
    f32 sp78;
    s32 sp74;
    struct BoundPadRecord *var_s0;
    ChrRecord *sp6C;
    ModelRoData_BoundingBoxRecord *temp_v0_3;
    struct PadRecord *sp64;
    struct PropRecord *sp60;
    s32 padding2;
    f32 sp58;
    f32 sp54;
    f32 sp50;
    s32 padding3;
    f32 sp48;

    spF0 = arg1->obj;
    var_s0 = NULL;

    modelLoad(spF0);

#ifndef TARGET_N64
    /* PORT: a bad model id leaves a NULL header, which becomes model->obj ==
     * NULL and crashes setup functions like setupCctv that walk
     * obj->model->obj->Switches[0]. Skip the prop here instead so the stage
     * still loads. */
    if (spF0 < 0 || spF0 >= (s32) (ARRAYCOUNT(PitemZ_entries) - 1) ||
        PitemZ_entries[spF0].header == NULL)
    {
        extern int printf(const char *, ...);
        printf("ge: domakedefaultobj: prop %d has no model header (model id %d) - prop skipped\n",
               cmdindex + 1, spF0);
        return;
    }
#endif

    sp78 = arg1->extrascale * 0.00390625f;

    arg1->damage = *(s32*)&arg1->damage / 65536.0f;

    if (getPlayerCount() >= 2)
    {
        sp74 = 1;

        if ((get_scenario() == SCENARIO_TLD) && (arg1->obj == PROP_FLAG))
        {
            sp74 = 0;
        }
        else if ((get_scenario() == SCENARIO_MWTGG) && (arg1->obj == PROP_CHRGOLDEN))
        {
            sp74 = 0;
        }

        if (sp74 != 0)
        {
            arg1->state |= PROPSTATE_RESPAWN; // respawn enabled
        }
    }

    if (arg1->flags & PROPFLAG_INSIDEANOTHEROBJ)
    {
        if (arg1->type == PROP_TYPE_SMOKE)
        {
            sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
        }
        else
        {
            objInitWithModelDef(arg1, PitemZ_entries[spF0].header);
        }

        modelSetScale(arg1->model, arg1->model->scale * sp78);
    }
    else if (arg1->flags & PROPFLAG_ASSIGNEDTOCHR)
    {
        sp6C = chrFindByLiteralId(arg1->pad);

        if ((sp6C != NULL) && (sp6C->prop != NULL) && (sp6C->model != NULL))
        {
            if (arg1->type == 8)
            {
                var_v0 = sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
            }
            else
            {
                var_v0 = objInitWithModelDef(arg1, PitemZ_entries[spF0].header);
            }

            modelSetScale(arg1->model, arg1->model->scale * sp78);
            chrpropReparent(var_v0, sp6C->prop);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakedefaultobj: no chr number %d for obj number %d!\n",arg1->pad,cmdindex + 1);
        }
        #endif
    }
    else
    {
        if (isNotBoundPad(arg1->pad))
        {
            sp64 = &g_CurrentSetup.pads[arg1->pad];

            matrix_4x4_set_basis_and_position_target(&sp8C, 0.0f, 0.0f, 0.0f, -sp64->look.f[0], -sp64->look.f[1], -sp64->look.f[2], sp64->up.f[0], sp64->up.f[1], sp64->up.f[2]);

            spD0.f[0] = sp64->pos.f[0];
            spD0.f[1] = sp64->pos.f[1];
            spD0.f[2] = sp64->pos.f[2];

            if (arg1->flags & PROPFLAG_ONSCREEN)
            {
                sp80.f[0] = sp64->pos.f[0];
                sp80.f[1] = sp64->pos.f[1];
                sp80.f[2] = sp64->pos.f[2];
            }
            else
            {
                // same as above?

                sp80.f[0] = sp64->pos.f[0];
                sp80.f[1] = sp64->pos.f[1];
                sp80.f[2] = sp64->pos.f[2];
            }

            spCC = sp64->stan;
        }
        else
        {
            var_s0 = &g_CurrentSetup.boundpads[getBoundPadNum(arg1->pad)];

            matrix_4x4_set_basis_and_position_target(&sp8C, 0.0f, 0.0f, 0.0f, -var_s0->look.f[0], -var_s0->look.f[1], -var_s0->look.f[2], var_s0->up.f[0], var_s0->up.f[1], var_s0->up.f[2]);

            if (!(arg1->flags2 & PROPFLAG2_00000001))
            {
                sub_GAME_7F001BD4(var_s0, &spD0);

                sp80.f[0] = spD0.f[0] + (var_s0->up.f[0] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));
                sp80.f[1] = spD0.f[1] + (var_s0->up.f[1] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));
                sp80.f[2] = spD0.f[2] + (var_s0->up.f[2] * ((var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f));

                spCC = var_s0->stan;

                if (walkTilesBetweenPoints_NoCallback(&spCC, var_s0->pos.f[0], var_s0->pos.f[2], spD0.f[0], spD0.f[2]) == 0)
                {
                    spD0.f[0] = var_s0->pos.f[0];
                    spD0.f[1] = var_s0->pos.f[1];
                    spD0.f[2] = var_s0->pos.f[2];

                    spCC = var_s0->stan;

                    if (!(arg1->flags & PROPFLAG_ONSCREEN) && !(arg1->flags & PROPFLAG_00001000))
                    {
                        // removed
                        #ifdef DEBUG
                            osSyncPrintf("object number %d not positioned correctly!\n",cmdindex + 1);
                        #endif
                    }
                }
            }
            else
            {
                spD0.f[0] = var_s0->pos.f[0];
                spD0.f[1] = var_s0->pos.f[1];
                spD0.f[2] = var_s0->pos.f[2];

                spCC = var_s0->stan;

                sub_GAME_7F001BD4(var_s0, &sp80);

                sp80.f[0] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[0];
                sp80.f[1] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[1];
                sp80.f[2] += (var_s0->bbox.ymin - var_s0->bbox.ymax) * 0.5f * var_s0->up.f[2];
            }
        }

        if (getposstan(&spD0, spCC, 0.0f, &spE0, &spDC) != 0)
        {
            if (arg1->type == PROP_TYPE_SMOKE)
            {
                sp60 = sub_GAME_7F051DD8(arg1, PitemZ_entries[spF0].header);
            }
            else
            {
                sp60 = objInitWithAutoModel(arg1);
            }

            if (var_s0 != NULL)
            {
                temp_v0_3 = chrobjGetBboxFromObjectRecord(arg1);
                if (temp_v0_3 != NULL)
                {
                    sp58 = 1.0f;
                    sp54 = 1.0f;
                    sp50 = 1.0f;

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000020))
                    {
                        if (temp_v0_3->Bounds.xmin < temp_v0_3->Bounds.xmax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp58 = (var_s0->bbox.xmax - var_s0->bbox.xmin) / ((temp_v0_3->Bounds.xmax - temp_v0_3->Bounds.xmin) * arg1->model->scale);
                            }
                            else
                            {
                                sp58 = (var_s0->bbox.xmax - var_s0->bbox.xmin) / ((temp_v0_3->Bounds.xmax - temp_v0_3->Bounds.xmin) * arg1->model->scale);
                            }
                        }
                    }

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000040))
                    {
                        if (temp_v0_3->Bounds.ymin < temp_v0_3->Bounds.ymax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp50 = (var_s0->bbox.zmax - var_s0->bbox.zmin) / ((temp_v0_3->Bounds.ymax - temp_v0_3->Bounds.ymin) * arg1->model->scale);
                            }
                            else
                            {
                                sp54 = (var_s0->bbox.ymax - var_s0->bbox.ymin) / ((temp_v0_3->Bounds.ymax - temp_v0_3->Bounds.ymin) * arg1->model->scale);
                            }
                        }
                    }

                    if (arg1->flags & (PROPFLAG_00000010 | PROPFLAG_00000080))
                    {
                        if (temp_v0_3->Bounds.zmin < temp_v0_3->Bounds.zmax)
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                sp54 = (var_s0->bbox.ymax - var_s0->bbox.ymin) / ((temp_v0_3->Bounds.zmax - temp_v0_3->Bounds.zmin) * arg1->model->scale);
                            }
                            else
                            {
                                sp50 = (var_s0->bbox.zmax - var_s0->bbox.zmin) / ((temp_v0_3->Bounds.zmax - temp_v0_3->Bounds.zmin) * arg1->model->scale);
                            }
                        }
                    }

                    var_f0 = sp58;

                    if (sp54 < var_f0)
                    {
                        var_f0 = sp54;
                    }

                    if (sp50 < var_f0)
                    {
                        var_f0 = sp50;
                    }

                    sp48 = sp58;

                    if (sp58 < sp54)
                    {
                        sp48 = sp54;
                    }

                    if (sp48 < sp50)
                    {
                        sp48 = sp50;
                    }

                    if (arg1->flags & PROPFLAG_00000010)
                    {
                        sp50 = var_f0;
                        sp54 = var_f0;
                        sp58 = var_f0;
                    }
                    else
                    {
                        if (!(arg1->flags & PROPFLAG_00000020))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.xmax == temp_v0_3->Bounds.xmin)
                                {
                                    sp58 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.xmax == temp_v0_3->Bounds.xmin)
                            {
                                sp58 = sp48;
                            }
                        }

                        if (!(arg1->flags & PROPFLAG_00000040))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.ymax == temp_v0_3->Bounds.ymin)
                                {
                                    sp50 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.ymax == temp_v0_3->Bounds.ymin)
                            {
                                sp54 = sp48;
                            }
                        }

                        if (!(arg1->flags & PROPFLAG_00000080))
                        {
                            if (arg1->flags & PROPFLAG_ONSCREEN)
                            {
                                if (temp_v0_3->Bounds.zmax == temp_v0_3->Bounds.zmin)
                                {
                                    sp54 = sp48;
                                }
                            }
                            else if (temp_v0_3->Bounds.zmax == temp_v0_3->Bounds.zmin)
                            {
                                sp50 = sp48;
                            }
                        }
                    }

                    sp58 /= sp48;
                    sp54 /= sp48;
                    sp50 /= sp48;

                    if ((sp58 <= 0.000001f) || (sp54 <= 0.000001f) || (sp50 <= 0.000001f))
                    {
                        #ifdef DEBUG
                        osSyncPrintf("Scale warning: object number %d has a small scale: %f,%f,%f\n",cmdindex +1, sp58,sp54,sp50);
                        #endif
                        sp50 = 1.0f;
                        sp54 = 1.0f;
                        sp58 = 1.0f;
                    }

                    matrix_column_1_scalar_multiply(sp58, sp8C.m[0]);
                    matrix_column_2_scalar_multiply(sp54, sp8C.m[0]);
                    matrix_column_3_scalar_multiply_2(sp50, sp8C.m[0]);

                    modelSetScale(arg1->model, arg1->model->scale * sp48);
                }
            }

            modelSetScale(arg1->model, arg1->model->scale * sp78);
            matrix_scalar_multiply(arg1->model->scale, sp8C.m[0]);

            if (arg1->flags & PROPFLAG_ONSCREEN)
            {
                sub_GAME_7F040BA0(arg1, &spE0, &sp8C, spDC, &sp80);
            }
            else
            {
                sub_GAME_7F04088C(arg1, &spE0, &sp8C, spDC, &sp80);
            }

            setupUpdateObjectRoomPosition(arg1);
            chrpropActivate(sp60);
            chrpropEnable(sp60);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakedefaultobj: prop obj number %d not reset!\n",cmdindex + 1);
        }
        #elif !defined(TARGET_N64)
        else
        {
            /* PORT: retail only logs this in DEBUG, but on PC it predicts a
             * crash: no floor was found under the pad, so objInit never ran and
             * model/prop stay NULL. Log the prop's name so a later crash in a
             * setup*() function has a suspect. */
            extern int printf(const char *, ...);
            /* a broken stan loses every prop, so cap the log spam per stage load
             * (proplvreset2 resets the counter via portPropResetNotPositioned()). */
            if (g_PortNotPositioned < 32)
            {
                g_PortNotPositioned++;
                printf("ge: domakedefaultobj: prop %d (model id %d, type %d, pad %d) not positioned - "
                       "no stan under its pad; prop has no model%s\n",
                       cmdindex + 1, spF0, arg1->type, arg1->pad,
                       (g_PortNotPositioned == 32) ? " (further lines suppressed this load)" : "");
            }
        }
        #endif
    }
}

/**
 * NTSC address 0x7F002738. PAL address 0x7F002738.
*/
void weaponAssignToHome(s32 arg0, WeaponObjRecord* weapon, s32 cmdindex)
{
    s32 padding;
    bool hastoken;
    ChrRecord* chr;
    bool giveweapon;
    s32 temp_a0;
    struct s_mp_weapon_set* weapon_set;

    if ((weapon->flags & PROPFLAG_ASSIGNEDTOCHR))
    {
        chr = chrFindByLiteralId(weapon->pad);

        if (chr && chr->prop && chr->model)
        {
            if (cheatIsActive(CHEAT_ENEMY_ROCKETS))
            {
                switch ((s8)weapon->weaponnum)
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
                        weapon->weaponnum = ITEM_ROCKETLAUNCH;
                        weapon->obj = PROP_CHRROCKETLAUNCH;
                        weapon->extrascale = 256;
                        break;
                }
            }

            weaponLoadProjectileModels((s8)weapon->weaponnum);
            sub_GAME_7F052030(weapon, chr);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakeweaponobj: no chr number %d for obj number %d!\n",weapon->pad, cmdindex + 1);
        }
        #endif
    }
    else
    {
        hastoken = 1;
        giveweapon = 1;

        if (getPlayerCount() >= 2)
        {
            lastmpweaponnum = -1;

            switch ((u8)weapon->weaponnum)
            {
                case ITEM_UNARMED + 0xF0:
                case ITEM_FIST + 0xF0:
                case ITEM_KNIFE + 0xF0:
                case ITEM_THROWKNIFE + 0xF0:
                case ITEM_WPPK + 0xF0:
                case ITEM_WPPKSIL + 0xF0:
                case ITEM_TT33 + 0xF0:
                case ITEM_SKORPION + 0xF0:
                    weapon_set = getPtrMPWeaponSetData();

                    temp_a0 = (u8)weapon->weaponnum - 0xF0;
                    lastmpweaponnum = temp_a0;

                    weapon->weaponnum = weapon_set[temp_a0].itemID;
                    weapon->obj = weapon_set[temp_a0].propID;
#if defined(VERSION_EU)
                    weapon->extrascale = (weapon_set[temp_a0].size16);
#else
                    weapon->extrascale = (weapon_set[temp_a0].size * 256.0f);
#endif

                    giveweapon = weapon_set[temp_a0].allowpickup;

                    break;

                case ITEM_TOKEN:

                    hastoken = 1;
                    giveweapon = 1;

                    if (get_scenario() != SCENARIO_TLD)
                    {
                        giveweapon = 0;
                    }
                    break;
            }
        }

#ifndef TARGET_N64
        {
            extern s32 portTrueEnemyRocketsEnabled(void);
            if (portTrueEnemyRocketsEnabled() && cheatIsActive(CHEAT_ENEMY_ROCKETS))
            {
                switch ((s8) weapon->weaponnum)
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
                        weapon->weaponnum = ITEM_ROCKETLAUNCH;
                        weapon->obj = PROP_CHRROCKETLAUNCH;
                        weapon->extrascale = 256;
                        break;
                }
            }
        }
#endif

        if ((weapon->weaponnum != ITEM_UNARMED) && giveweapon)
        {
            weaponLoadProjectileModels(weapon->weaponnum);
            domakedefaultobj(arg0, (struct ObjectRecord*)weapon, cmdindex);
        }
    }
}

//i should be object hat
void setupHat(s32 arg0, ObjectRecord* hat, s32 cmdindex)
{
    if (hat->flags & PROPFLAG_ASSIGNEDTOCHR) {
        ChrRecord* chr = chrFindByLiteralId(hat->pad);
        if (chr && chr->prop && chr->model) {
            hatAssignToChr(hat, chr);
        }
        #ifdef DEBUG
        else
        {
            osSyncPrintf("domakehatobj: no chr number %d for obj number %d!\n",hat->pad, cmdindex + 1);
        }
        #endif
    } else {
        domakedefaultobj(arg0, hat, cmdindex);
    }
}

//i should be object key
void setupKey(s32 arg0, ObjectRecord* key, s32 cmdindex)
{
    domakedefaultobj(arg0, key, cmdindex);
}


/**
 * NTSC address 0x7F002A3C.
*/
void setupCctv(s32 arg0, CCTVRecord *arg1, s32 cmdindex)
{
    struct coord3d *temp_a2;
    struct PadRecord *sp50;
    struct coord3d sp44;
    Mtxf *sp3C;

    domakedefaultobj(arg0, (struct ObjectRecord*)arg1, cmdindex);

    if (arg1->lookpad >= 0)
    {
#ifndef TARGET_N64
        /* PORT: the model chain can be NULL here (bad model id, or the prop
         * couldn't be positioned), so skip the camera's pad-relative setup and
         * keep its default orientation. Check every link, including
         * Switches[0]->Data and ->prop, since the code below dereferences them
         * all. */
        if (arg1->model == NULL || arg1->prop == NULL || arg1->model->obj == NULL ||
            arg1->model->obj->Switches == NULL || arg1->model->obj->Switches[0] == NULL ||
            arg1->model->obj->Switches[0]->Data == NULL)
        {
            extern int printf(const char *, ...);
            static s32 warned;
            if (warned < 8)
            {
                warned++;
                printf("ge: setupCctv: CCTV %d (model id %d, pad %d) has no model data - "
                       "skipping pad setup (model=%p prop=%p obj=%p)\n",
                       cmdindex, arg1->obj, arg1->lookpad,
                       (void *) arg1->model, (void *) arg1->prop,
                       (void *) ((arg1->model != NULL) ? arg1->model->obj : NULL));
            }
            return;
        }
#endif
        temp_a2 = (struct coord3d*)arg1->model->obj->Switches[0]->Data;

        if (isNotBoundPad(arg1->lookpad))
        {
            sp50 = &g_CurrentSetup.pads[arg1->lookpad];
        }
        else
        {
            sp50 = (struct PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(arg1->lookpad)];
        }

        sp44.f[0] = temp_a2->f[0];
        sp44.f[1] = temp_a2->f[1];
        sp44.f[2] = temp_a2->f[2];

        mtx4RotateVecInPlace(&arg1->mtx, &sp44);

        sp3C = &arg1->unk84;

        sp44.f[0] += arg1->prop->pos.f[0];
        sp44.f[1] += arg1->prop->pos.f[1];
        sp44.f[2] += arg1->prop->pos.f[2];

        /* CCTV aim: the vanilla code, fed the camera's look-target pad
         * (arg1->lookpad) rather than the inherited ObjectRecord.pad, which is
         * its own mount. Using the mount pad made the camera aim at itself;
         * setupAutogun reads its target pad the same way. */
        matrix_4x4_set_basis_and_position_target(sp3C, 0.0f, 0.0f, 0.0f, sp44.f[0] - sp50->pos.f[0], sp44.f[1] - sp50->pos.f[1], sp44.f[2] - sp50->pos.f[2], 0.0f, 1.0f, 0.0f);
        matrix_scalar_multiply(arg1->model->scale, sp3C->m[0]);

        if (arg1->convert_to_f32 == 0)
        {
            arg1->convert_to_f32 = 1;
            arg1->unkCC = (*(s32*)&arg1->unkCC * M_TAU_F) / 65536.0f;
            arg1->unkD0 = (*(s32*)&arg1->unkD0 * M_TAU_F) / 65536.0f;
            arg1->unkDC = (*(s32*)&arg1->unkDC * M_TAU_F) / 65536.0f;
            arg1->unkE8 = *(s32*)&arg1->unkE8;
        }

        arg1->unkD4 = 0;
        arg1->unkD8 = 0.0f;
        arg1->unkC8 = arg1->unkCC;
        arg1->unkC4 = atan2f(sp44.f[0] - sp50->pos.f[0], sp44.f[2] - sp50->pos.f[2]);
        arg1->timer = 0;

#ifndef TARGET_N64
        { /* GE_DUMP_BOND: log each CCTV camera's pad look direction, aim vector
           * and their agreement (dot < 0 means aiming backward of the pad's facing). */
            extern char *getenv(const char *);
            static int probing = -1;
            if (probing < 0) probing = getenv("GE_DUMP_BOND") != NULL;
            if (probing) {
                f32 ax = sp44.f[0] - sp50->pos.f[0];
                f32 ay = sp44.f[1] - sp50->pos.f[1];
                f32 az = sp44.f[2] - sp50->pos.f[2];
                f32 dotlook = ax * sp50->look.f[0] + ay * sp50->look.f[1] + az * sp50->look.f[2];
                fprintf(stderr,
                        "cctvprobe: pad=%d look=(%.2f,%.2f,%.2f) up=(%.2f,%.2f,%.2f) aim=(%.2f,%.2f,%.2f) yaw=%.2f dotlook=%.3f%s\n",
                        arg1->lookpad,
                        sp50->look.f[0], sp50->look.f[1], sp50->look.f[2],
                        sp50->up.f[0], sp50->up.f[1], sp50->up.f[2],
                        ax, ay, az, arg1->unkC4, dotlook,
                        dotlook < 0.0f ? "  <-- BACKWARD" : "");
                fprintf(stderr,
                        "cctvprobe:   lenslocal=(%.2f,%.2f,%.2f) mtxX=(%.2f,%.2f,%.2f) mtxY=(%.2f,%.2f,%.2f) mtxZ=(%.2f,%.2f,%.2f)\n",
                        temp_a2->f[0], temp_a2->f[1], temp_a2->f[2],
                        arg1->mtx.m[0][0], arg1->mtx.m[0][1], arg1->mtx.m[0][2],
                        arg1->mtx.m[1][0], arg1->mtx.m[1][1], arg1->mtx.m[1][2],
                        arg1->mtx.m[2][0], arg1->mtx.m[2][1], arg1->mtx.m[2][2]);
                {
                    ModelRoData_BoundingBoxRecord *bb = chrobjGetBboxFromObjectRecord((ObjectRecord *) arg1);
                    fprintf(stderr,
                            "cctvprobe:   proppos=(%.2f,%.2f,%.2f) padpos=(%.2f,%.2f,%.2f) modelbbox=%s",
                            arg1->prop->pos.f[0], arg1->prop->pos.f[1], arg1->prop->pos.f[2],
                            sp50->pos.f[0], sp50->pos.f[1], sp50->pos.f[2],
                            bb == NULL ? "NULL\n" : "");
                    if (bb != NULL) {
                        fprintf(stderr, "(%.2f..%.2f, %.2f..%.2f, %.2f..%.2f)\n",
                                bb->Bounds.xmin, bb->Bounds.xmax, bb->Bounds.ymin, bb->Bounds.ymax, bb->Bounds.zmin, bb->Bounds.zmax);
                    }
                    if (!isNotBoundPad(arg1->lookpad)) {
                        struct BoundPadRecord *bp = &g_CurrentSetup.boundpads[getBoundPadNum(arg1->lookpad)];
                        fprintf(stderr, "cctvprobe:   boundpad bbox x(%.1f..%.1f) y(%.1f..%.1f) z(%.1f..%.1f)\n",
                                bp->bbox.xmin, bp->bbox.xmax, bp->bbox.ymin, bp->bbox.ymax,
                                bp->bbox.zmin, bp->bbox.zmax);
                    }
                }
                fflush(stderr);
            }
        }
#endif
    }
}

void setupAutogun(s32 stageID, AutogunRecord *autogun, s32 cmdindex)
{
    s8 *beam;

    domakedefaultobj(stageID, (ObjectRecord *) autogun, cmdindex);

#ifdef VERSION_EU
    autogun->speed = ((*((s32 *) (&autogun->speed))) * 7.5398226f) / 65536.0f;
    autogun->aimdist = ((*((s32 *) (&autogun->aimdist))) * 100.0f) / 65536.0f;
    autogun->unk88 = ((*((s32 *) (&autogun->unk88))) * M_TAU_F) / 65536.0f;
    autogun->unk8C = ((*((s32 *) (&autogun->unk8C))) * M_TAU_F) / 65536.0f;
#endif

    autogun->unkAC = 0;
    autogun->unkB8 = -1;
    autogun->unkBC = -1;
    autogun->unkC0 = -1;
    autogun->unkC4 = 0;
    autogun->unkC8 = 0;
    autogun->unk90 = 0.0f;
    autogun->unk94 = 0.0f;
    autogun->rot_related = 0.0f;
    autogun->unk9C = 0.0f;
    autogun->unkA0 = 0.0f;
    autogun->unk98 = 0.0f;
    autogun->unkB0 = 0.0f;
    autogun->unkB4 = 0.0f;

#ifndef VERSION_EU
    autogun->speed = ((*((s32 *) (&autogun->speed))) * M_TAU_F) / 65536.0f;
    autogun->aimdist = ((*((s32 *) (&autogun->aimdist))) * 100.0f) / 65536.0f;
    autogun->unk88 = ((*((s32 *) (&autogun->unk88))) * M_TAU_F) / 65536.0f;
    autogun->unk8C = ((*((s32 *) (&autogun->unk8C))) * M_TAU_F) / 65536.0f;
#endif

    beam          = mempAllocBytesInBank(0x30U, MEMPOOL_STAGE);
    autogun->beam = beam;
    *beam = -1;

    autogun->is_active = FALSE;
    autogun->unkD4 = 0.0f;

    if (autogun->padID >= 0)
    {
        s32 stack1;
        f32 xdiff;
        f32 ydiff;
        f32 zdiff;
        PadRecord *pad;
        PropRecord *prop;

        if (autogun->padID < 0x2710)
        {
            if (1);
            pad = &g_CurrentSetup.pads[autogun->padID];
        }
        else
        {
            pad = &g_CurrentSetup.boundpads[getBoundPadNum(autogun->padID)];
        }

        prop = autogun->prop;

        xdiff = pad->pos.x - prop->pos.x;
        ydiff = pad->pos.y - prop->pos.y;
        zdiff = pad->pos.z - prop->pos.z;

        autogun->rot_related = atan2f(xdiff, zdiff);
        autogun->unk98 = atan2f(ydiff, sqrtf((xdiff * xdiff) + (zdiff * zdiff)));
    }
}


//i should be object rack
void setupHangingMonitors(s32 arg0, ObjectRecord* rack, s32 cmdindex)
{
    domakedefaultobj(arg0, rack, cmdindex);
}


void setupSingleMonitor(s32 stageID, MonitorObjRecord *monitor, s32 cmdindex)
{
    MonitorRecord *record;
    s32 unused;
    s32 modelnum;
    ObjectRecord *owner;
    PropRecord *prop;
    f32 scale;
#ifndef TARGET_N64
    s32 ownerok; /* see the owner-model guard below */
#endif

    monitor->Monitor = g_MonitorAnimController;
    record = &monitor->Monitor;
    monitorSetImageByNum(&monitor->Monitor, monitor->ImageNum);

    if (monitor->pad < 0 && (monitor->flags & PROPFLAG_INSIDEANOTHEROBJ) == 0)
    {
        modelnum = monitor->obj;
        owner = (struct ObjectRecord *)setupGetPtrToCommandByIndex(cmdindex + monitor->OwnerOffset);

        modelLoad(modelnum);

        scale = monitor->extrascale * (1.0f / 256.0f);
        monitor->damage = *(s32*)&monitor->damage / M_U16_MAX_VALUE_F;

        if (getPlayerCount() >= 2)
        {
            monitor->state |= PROPSTATE_RESPAWN;
        }

        prop = objInitWithAutoModel((ObjectRecord*)monitor);
        monitor->embedment = embedmentAllocate();

#ifndef TARGET_N64
        /* PORT: the monitor attaches to its owner's model, which is NULL if the
         * owner couldn't be positioned, so skip the attach and keep the
         * monitor's own model. This should be unreachable since the stan cache
         * fix, but stays as a safety net. */
        ownerok = (owner != NULL && owner->model != NULL &&
                   owner->model->obj != NULL &&
                   owner->model->obj->Switches != NULL);

        if (prop != NULL && monitor->embedment != NULL && !ownerok)
        {
            extern int printf(const char *, ...);
            static s32 warned;

            if (warned < 8)
            {
                warned++;
                printf("ge: setupSingleMonitor: monitor %d has no owner model "
                       "(owner %d) - skipping the attach\n",
                       cmdindex, cmdindex + monitor->OwnerOffset);
            }
        }

        if (prop && monitor->embedment && ownerok)
#else
        if (prop && monitor->embedment)
#endif
        {
            monitor->runtime_bitflags |= RUNTIMEBITFLAG_EMBEDDED;
            modelSetScale(monitor->model, monitor->model->scale * scale);
            monitor->model->attachedto = owner->model;

            if (monitor->OwnerPart == 0)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[0];
            }
            else if (monitor->OwnerPart == 1)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[1];
            }
            else if (monitor->OwnerPart == 2)
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[2];
            }
            else
            {
                monitor->model->attachedto_objinst = owner->model->obj->Switches[3];;
            }

            chrpropReparent(prop, owner->prop);
            matrix_4x4_set_rotation_around_x(0.36651915f, (Mtxf*)&monitor->embedment->matrix);
            matrix_scalar_multiply(monitor->model->scale / owner->model->scale, (f32*)&monitor->embedment->matrix);
        }
    }
    else
    {
        domakedefaultobj(stageID, (ObjectRecord*)monitor, cmdindex);
    }

    if ((monitor->flags & PROPFLAG_MONITOR_RENDERPOSTBG) && monitor->prop)
    {
        monitor->prop->flags |= PROPFLAG_RENDERPOSTBG;
    }
}


void setupMultiMonitor(s32 stageID, MultiMonitorObjRecord* monitor, s32 cmdindex)
{
#ifdef TARGET_N64
    monitor->Monitor[0] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[0], monitor->ImageNums[0]);

    monitor->Monitor[1] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[1], monitor->ImageNums[1]);

    monitor->Monitor[2] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[2], monitor->ImageNums[2]);

    monitor->Monitor[3] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[3], monitor->ImageNums[3]);
#else
    /* PORT: ImageNums is a u8[4] stored as one raw hex word in the generated
     * setups, so on little-endian its bytes are reversed. Read ImageNums[3-i]
     * so each screen gets its own animation (only visible when the four values
     * differ). */
    monitor->Monitor[0] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[0], monitor->ImageNums[3]);

    monitor->Monitor[1] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[1], monitor->ImageNums[2]);

    monitor->Monitor[2] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[2], monitor->ImageNums[1]);

    monitor->Monitor[3] = g_MonitorAnimController;
    monitorSetImageByNum(&monitor->Monitor[3], monitor->ImageNums[0]);
#endif

    domakedefaultobj(stageID, monitor, cmdindex);
}

void sub_GAME_7F00324C(struct BoundPadRecord *arg0, s32 *arg1, s32 *arg2, struct coord3d *arg3, struct coord3d *arg4)
{
    StandTile *sp4C;
    struct coord3d normal;
    s32 padding;
    struct coord3d center;
    StandTile *sp2C;
    f32 scale;

    sub_GAME_7F001BD4(arg0, &center);
    sp2C = (StandTile *)arg0->stan;

    if (walkTilesBetweenPoints_NoCallback(&sp2C, arg0->pos.f[0], arg0->pos.f[2], center.f[0], center.f[2]) == 0)
    {
        sp2C = (StandTile *)arg0->stan;
        center.f[0] = arg0->pos.f[0];
        center.f[1] = arg0->pos.f[1];
        center.f[2] = arg0->pos.f[2];
    }

    normal.f[0] = (arg0->up.f[1] * arg0->look.f[2]) - (arg0->up.f[2] * arg0->look.f[1]);
    normal.f[1] = (arg0->up.f[2] * arg0->look.f[0]) - (arg0->up.f[0] * arg0->look.f[2]);
    normal.f[2] = (arg0->up.f[0] * arg0->look.f[1]) - (arg0->up.f[1] * arg0->look.f[0]);

    scale = 1.0f / sqrtf(((normal.f[0] * normal.f[0]) + (normal.f[1] * normal.f[1])) + (normal.f[2] * normal.f[2]));
    sp4C = sp2C;

    normal.f[0] *= scale;
    normal.f[1] *= scale;
    normal.f[2] *= scale;

    arg3->f[0] = center.f[0] + (normal.f[0] * 50.0f);
    arg3->f[1] = center.f[1];
    arg3->f[2] = center.f[2] + (normal.f[2] * 50.0f);


    walkTilesBetweenPoints_NoCallback(&sp4C, center.f[0], center.f[2], arg3->f[0], arg3->f[2]);

    if (1);

#ifndef TARGET_N64
    /* lame-pad door: stan never resolved (see the stanwalk NULL guard);
     * -1 = the "no room" sentinel this function already uses for arg2 */
    if (sp4C == NULL)
    {
        *arg1 = -1;
        *arg2 = -1;
        return;
    }
#endif
    *arg1 = (s32) sp4C->room;
    sp4C = sp2C;

    arg4->f[0] = center.f[0] - (normal.f[0] * 50.0f);
    arg4->f[1] = center.f[1];
    arg4->f[2] = center.f[2] - (normal.f[2] * 50.0f);

    walkTilesBetweenPoints_NoCallback(&sp4C, center.f[0], center.f[2], arg4->f[0], arg4->f[2]);

    if (1);

#ifndef TARGET_N64
    if (sp4C == NULL)
    {
        *arg2 = -1;
        return;
    }
#endif
    *arg2 = (s32) sp4C->room;

    if (*arg2 == *arg1)
    {
        *arg2 = -1;
    }
}


extern f32 g_DoorScale;
/**
 *
 * NTSC ADDRESS: 7F003480 PAL ADDRESS: 7F0033F0 Perfect Dark: void
 * setupCreateDoor(struct doorobj *door, s32 cmdindex)
*/
void setupDoor(s32 arg0, struct DoorRecord *door, s32 arg2)
{
    s32 padding; // no sp
    s32 modelnum;
    struct BoundPadRecord *pad;
    StandTile *sp1C8_stan;
    PropRecord *prop;
    struct coord3d sp1B8;
    s32 portalnum; //sp1b4
    s32 sp1B0;
    s32 sp1AC;
    struct coord3d sp1A0;
    struct coord3d sp194;
    struct PortalMetric sp180;
    struct ModelRoData_BoundingBoxRecord *temp_v0;
    struct coord3d sp170;
    StandTile *sp16C;
    Mtxf sp12C;
    f32 temp_f2; // no sp
    ModelFileHeader *sp124;
    struct coord3d sp118;                           /* compiler-managed */
    StandTile *sp114_stan;
    Mtxf spD4;
    struct coord3d spC8;
    Mtxf sp88;
    struct coord3d sp7C;
    struct bbox bb2;
    f32 xscale;
    f32 yscale;
    f32 zscale;
    f32 scale;
    //StandTile *stan;
    u8 *padding2;

    modelnum = door->obj;

    portalnum = -1;
    sp1B0 = -1;
    sp1AC = -1;

    modelLoad(modelnum);

    pad = &(g_CurrentSetup.boundpads[door->pad]);

    if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) || (door->flags & PROPFLAG_NO_PORTAL_CLOSE))
    {
        sub_GAME_7F00324C(pad, &sp1B0, &sp1AC, &sp1A0, &sp194);

        if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) && (sp1B0 >= 0) && (sp1AC >= 0))
        {
            portalnum = bgGetPortalBetweenRooms(sp1B0, sp1AC, &sp1A0, &sp194);
        }
    }

    if (g_DoorScale != 1.0f)
    {
        if (portalnum >= 0)
        {
            sub_GAME_7F0B96CC(portalnum, &sp180);
            sp180.min *= get_room_data_float2();

            temp_f2 = (pad->pos.f[0] * sp180.normal.f[0]) + (pad->pos.f[1] * sp180.normal.f[1]) + (pad->pos.f[2] * sp180.normal.f[2]);

            if (g_DoorScale < 1.0f)
            {
                temp_f2 = (temp_f2 - sp180.min) * (1.0f - g_DoorScale);
                sp170.f[0] = pad->pos.f[0] - (sp180.normal.f[0] * temp_f2);
                sp170.f[1] = pad->pos.f[1] - (sp180.normal.f[1] * temp_f2);
                sp170.f[2] = pad->pos.f[2] - (sp180.normal.f[2] * temp_f2);
            }
            else
            {
                temp_f2 = (temp_f2 - sp180.min) * (g_DoorScale - 1.0f);
                sp170.f[0] = pad->pos.f[0] + (sp180.normal.f[0] * temp_f2);
                sp170.f[1] = pad->pos.f[1] + (sp180.normal.f[1] * temp_f2);
                sp170.f[2] = pad->pos.f[2] + (sp180.normal.f[2] * temp_f2);
            }

            sp16C = pad->stan;
            if (walkTilesBetweenPoints_NoCallback(&sp16C, pad->pos.f[0], pad->pos.f[2], sp170.f[0], sp170.f[2]) != 0)
            {
                pad->stan = sp16C;
                pad->pos.f[0] = sp170.f[0];
                pad->pos.f[1] = sp170.f[1];
                pad->pos.f[2] = sp170.f[2];
                pad->bbox.xmin *= g_DoorScale;
                pad->bbox.xmax *= g_DoorScale;
            }
 #ifdef DEBUG
            else
            {
                osSyncPrintf("volume for door object number %d did not have depth changed!\n",arg2 + 1);
            }
            #endif
        }
        else
        {
            pad->bbox.xmin *= g_DoorScale;
            pad->bbox.xmax *= g_DoorScale;
        }
    }

    if (getposstan(&pad->pos, pad->stan, 0.0f, &sp1B8, &sp1C8_stan) != 0)
    {
        matrix_4x4_set_basis_and_position_target(&sp12C, 0, 0, 0, -pad->look.f[0], -pad->look.f[1], -pad->look.f[2], pad->up.f[0], pad->up.f[1], pad->up.f[2]);
        sp124 = PitemZ_entries[modelnum].header;
        sp114_stan = sp1C8_stan;

        bb2.zmax = pad->bbox.xmin;
        bb2.zmin = pad->bbox.xmax; //78
        bb2.ymax = pad->bbox.ymin; //74
        bb2.ymin = pad->bbox.ymax; //70
        bb2.xmax = pad->bbox.zmin; //6c
        bb2.xmin = pad->bbox.zmax; //68

        matrix_4x4_set_rotation_around_x(M_HALF_PI, &spD4);
        matrix_4x4_set_rotation_around_z(M_HALF_PI, &sp88);
        matrix_4x4_multiply_in_place(&sp88, &spD4);
        matrix_4x4_multiply_in_place(&sp12C, &spD4);
        sub_GAME_7F001BD4(pad, &sp118);

        temp_v0 = (struct ModelRoData_BoundingBoxRecord *)sp124->RootNode->Child->Data;

        xscale = (bb2.ymin - bb2.ymax) / (temp_v0->Bounds.xmax - temp_v0->Bounds.xmin);
        yscale = (bb2.xmin - bb2.xmax) / (temp_v0->Bounds.ymax - temp_v0->Bounds.ymin);
        zscale = (bb2.zmin - bb2.zmax) / (temp_v0->Bounds.zmax - temp_v0->Bounds.zmin);

        if ((xscale <= 0.000001f) || (yscale <= 0.000001f) || (zscale <= 0.000001f))
        {
            #ifdef DEBUG
            osSyncPrintf("Scale warning: door object number %d has a small scale: %f,%f,%f\n",arg2 +1, xscale,yscale,zscale);
            #endif
            xscale =
                yscale =
                zscale = 1.0f;
        }

        matrix_column_1_scalar_multiply(xscale, spD4.m[0]);
        matrix_column_2_scalar_multiply(yscale, spD4.m[0]);
        matrix_column_3_scalar_multiply_2(zscale, spD4.m[0]);

        spC8.f[0] = sp118.f[0];
        spC8.f[1] = sp118.f[1];
        spC8.f[2] = sp118.f[2];

        if (!(door->flags2 & 1))
        {
            if (walkTilesBetweenPoints_NoCallback(&sp114_stan, sp1B8.f[0], sp1B8.f[2], sp118.f[0], sp118.f[2]) != 0)
            {
                sp1C8_stan = sp114_stan;
            }
            else
            {
                sp118.f[0] = sp1B8.f[0];
                sp118.f[2] = sp1B8.f[2];

                if (!(door->flags & 0x1000)) // prop flag PROPFLAG_00001000 "Absolute Position"
                {
                    #ifdef DEBUG
                    osSyncPrintf("door object number %d not positioned correctly!\n",arg2 +1);
                    #endif

                }
            }
        }
        else
        {
            sp118.f[0] = sp1B8.f[0];
            sp118.f[1] = sp1B8.f[1];
            sp118.f[2] = sp1B8.f[2];
        }

        if ((door->doorType == DOORTYPE_VERTICAL) || (door->doorType == DOORTYPE_FALLAWAY))
        {
            sp7C.f[0] = pad->look.f[0] * (bb2.xmin - bb2.xmax);
            sp7C.f[1] = pad->look.f[1] * (bb2.xmin - bb2.xmax);
            sp7C.f[2] = pad->look.f[2] * (bb2.xmin - bb2.xmax);
        }
        else
        {
            sp7C.f[0] = pad->up.f[0] * (bb2.ymax - bb2.ymin);
            sp7C.f[1] = pad->up.f[1] * (bb2.ymax - bb2.ymin);
            sp7C.f[2] = pad->up.f[2] * (bb2.ymax - bb2.ymin);
        }

        // These values are stored in the setup files as integers, but at
		// runtime they are floats. Hence reading a "float" as an integer,
		// converting it to a float and writing it back to the same property.
		door->maxFrac = *(s32 *) &door->maxFrac / 65536.0f;
		door->perimFrac = *(s32 *) &door->perimFrac / 65536.0f;
#if defined(VERSION_EU)
        door->accel = (*(s32 *) &door->accel) * 1.2f / 65536.0f;
		door->decel = (*(s32 *) &door->decel) * 1.2f / 65536.0f;
		door->maxSpeed = (*(s32 *) &door->maxSpeed) * 1.2f / 65536.0f;
#else
		door->accel = (*(s32 *) &door->accel) / 65536.0f;
		door->decel = (*(s32 *) &door->decel) / 65536.0f;
		door->maxSpeed = (*(s32 *) &door->maxSpeed) / 65536.0f;
#endif

        prop = doorInit(door, &sp118, &spD4, sp1C8_stan, &sp7C, &spC8);
        if (door->flags & PROPFLAG_CULL_BEHIND_DOOR)
        {
            door->portalNumber = portalnum;
            if ((portalnum >= 0) && (door->openPosition == 0.0f))
            {
                doorDeactivatePortal(door);
            }
            #ifdef DEBUG
            else
            {
                osSyncPrintf("No portal for door object number %d ",arg2 + 1);
            }
            #endif
        }

        prop->rooms[0] = prop->stan->room;
        chrpropRegisterRoom(prop, prop->stan->room);
        prop->rooms[1] = 0xFFU;
        prop->rooms[2] = 0xFFU;

        if ((door->flags & PROPFLAG_CULL_BEHIND_DOOR) || (door->flags & PROPFLAG_NO_PORTAL_CLOSE))
        {
            if (sp1B0 != prop->stan->room)
            {
                if (sp1B0 >= 0)
                {
                    prop->rooms[1] = sp1B0;
                    chrpropRegisterRoom(prop, sp1B0);
                }
            }
            else if (sp1AC >= 0)
            {
                prop->rooms[1] = sp1AC;
                chrpropRegisterRoom(prop, sp1AC);
            }

            if (prop->rooms[1] != 0xff && 1)
            {
                if (!prop->stan->room)
                {
                    #ifdef DEBUG
                        osSyncPrintf("3 rooms for door object number %d\n",arg2 + 1);
                    #endif
                }

            }
            #ifdef DEBUG
            else
            {
                osSyncPrintf("No 2nd room for door object number %d\n",arg2 + 1);
            }
            #endif
        }

        if (door->model != NULL)
        {
            scale = xscale;

            if (scale < yscale)
            {
                scale = yscale;
            }

            if (scale < zscale)
            {
                scale = zscale;
            }

            modelSetScale(door->model, door->model->scale * scale);
        }

        chrpropActivate(prop);
        chrpropEnable(prop);

        if (door->linkedDoorOffset != 0)
        {
            door->linkedDoor = (struct DoorRecord *)setupGetPtrToCommandByIndex(door->linkedDoorOffset + arg2);
        }
    }
    else
    {
        door->prop = NULL;
        #ifdef DEBUG
            osSyncPrintf("proplvreset: prop door object number %d not reset!\n",arg2 + 1);
        #endif
    }

}


// Perfect Dark void setupLoadFiles(s32 stagenum)

/* Setup-file section promotion: on N64 the sections are file offsets rebased
 * onto the RAM copy, but the compiled-in setup files already hold real
 * pointers. Those must pass through unchanged, since adding the base again
 * doubles them and crashes. */
#ifdef TARGET_N64
#define SETUP_PROMOTE(base, val) ((void *) (((u32) (base)) + ((u32) (val))))
#else
/* Don't try to tell pointers from offsets by address (e.g. `val >=
 * 0x10000000`): that only holds for some image bases and caused pointer
 * doubling crashes. Compiled-in setups always pass through; only ROM-loaded
 * setups use the N64 promotion. */
/* ROM loading (port/src/romfile_setup.c): a setup loaded from the ROM is the
 * N64 image (offsets), so it uses the N64 promotion. Set right after the file loads, below. */
s32 g_portSetupFromRom;
#define SETUP_PROMOTE(base, val) (g_portSetupFromRom ? (void *) (((uintptr_t) (base)) + ((uintptr_t) (val))) : (void *) (uintptr_t) (val))

/* bring-up (GE_DUMP_BOND): called at load milestones to find which step
 * corrupts the path waypoints data. */
s32 portDbgWpBad(void)
{
    s32 i;

    if (g_CurrentSetup.pathwaypoints == NULL)
    {
        return 0;
    }
    for (i = 0; g_CurrentSetup.pathwaypoints[i].padID != -1 && i < 1000; i++)
    {
        if ((u32) g_CurrentSetup.pathwaypoints[i].padID > 4096u)
        {
            return 1;
        }
    }
    return 0;
}

/* PORT: setup-file snapshot and restore.
 *
 * The N64 copies the setup file fresh from ROM on every stage load, but the
 * PC's compiled-in setup stays modified in memory. Loading changes it in ways
 * that can't be repeated: integer-to-float conversions in place, ailist IDs
 * rewritten to pointers, and runtime pointers stored in the defs. A second load
 * would convert floats twice and trust stale pointers, which crashed.
 *
 * So snapshot the original bytes and restore them before every reload.
 * Restored: propDefs, intro, pads, boundpads. Not restored (unchanged on PC):
 * pathwaypoints, waypointgroups, ailists, patrolpaths, padnames; extend this if
 * a reload bug points at one of those. */
static u32 portMeasurePropDefs(u32 *defs)
{
    u32 words = 0;

    while (((PropDefHeaderRecord *) (defs + words))->type != PROPDEF_END)
    {
        words += sizepropdef((PropDefHeaderRecord *) (defs + words));
    }

    return words + 1; /* include the END header word */
}

/* The intro stream also changes on load (int-to-float conversions, and CAMERA
 * lang indexes rewritten to pointers), so it is restored too. The generated
 * intro TYPE word packs type << 24 (see INTRO_TYPE's PC arm). */
static u32 portMeasureIntro(u32 *rec)
{
    u32 words = 0;

    for (;;)
    {
        u32 step;

        switch (rec[words] >> 24)
        {
            case INTROTYPE_SPAWN:   step = sizeof(struct SetupIntroSpawn) / 4;   break;
            case INTROTYPE_ITEM:    step = sizeof(struct SetupIntroItem) / 4;    break;
            case INTROTYPE_AMMO:    step = sizeof(struct SetupIntroAmmo) / 4;    break;
            case INTROTYPE_SWIRL:   step = sizeof(struct SetupIntroSwirl) / 4;   break;
            case INTROTYPE_ANIM:    step = sizeof(struct SetupIntroAnim) / 4;    break;
            case INTROTYPE_CUFF:    step = sizeof(struct SetupIntroCuff) / 4;    break;
            case INTROTYPE_CAMERA:  step = sizeof(struct SetupIntroCamera) / 4;  break;
            case INTROTYPE_WATCH:   step = sizeof(struct SetupIntroWatch) / 4;   break;
            case INTROTYPE_CREDITS: step = sizeof(struct SetupIntroCredits) / 4; break;
            case INTROTYPE_END:     return words + 1;
            default:                step = sizeof(struct SetupIntroEmpty) / 4;   break;
        }

        words += step;
    }
}

/* The pad lists change too: pad->pos and boundpad bbox are scaled by levelscale
 * on every load, so a reload would shrink them. Records are fixed-size and end
 * at plink == NULL. */
static u32 portMeasurePads(u32 *pads)
{
    struct PadRecord *pad = (struct PadRecord *) pads;

    while (pad->plink != NULL)
    {
        pad++;
    }

    return ((uintptr_t) (pad + 1) - (uintptr_t) pads) / 4; /* include the terminator record */
}

static u32 portMeasureBoundPads(u32 *pads)
{
    struct BoundPadRecord *vol = (struct BoundPadRecord *) pads;

    while (vol->plink != NULL)
    {
        vol++;
    }

    return ((uintptr_t) (vol + 1) - (uintptr_t) pads) / 4;
}

/* Load-time setup lint: byte-swapped or garbled setup data shows up as
 * references that don't resolve, so check them once per stage load and log one
 * line per problem. No output means the setup's cross-references are sound. */
static void portLintSetup(void)
{
    s32 bad = 0;

    { /* Weapon props are read through ChrRecord, so act_attack.attack_item must
       * overlay WeaponObjRecord.weaponnum. Keep this pinned at compile time (it
       * has drifted before, see bondtypes.h). */
        typedef char portAliasContract[
            (offsetof(ChrRecord, act_attack.attack_item)
             == offsetof(WeaponObjRecord, weaponnum)) ? 1 : -1];
    }
    s32 padcount = 0;
    s32 index;
    PropDefHeaderRecord *phead;
    struct PadRecord *pad;
    struct BoundPadRecord *vol;

    if (g_CurrentSetup.pads != NULL)
    {
        for (pad = g_CurrentSetup.pads; pad->plink != NULL; pad++)
        {
            if (pad->stan == NULL || ((StandTile *) pad->stan)->id == 0)
            {
                /* Pad with no (or a zeroed) floor tile: find a tile from the
                 * pad's position like the padrc fallback does, then walk back
                 * toward the pad so it ends up under or near it. A wrong but
                 * real nearby tile beats crashes or spawning into the void; the
                 * lint line still reports the bad data. */
                const char *why = (pad->stan == NULL) ? "no stan" : "ZERO-TILE stan";
                coord3d fixpos;
                fixpos.f[0] = pad->pos.f[0];
                fixpos.f[1] = pad->pos.f[1];
                fixpos.f[2] = pad->pos.f[2];
                pad->stan = sub_GAME_7F0AFB78(&fixpos.f[0], &fixpos.f[1], &fixpos.f[2], 0);

                if (pad->stan != NULL)
                {
                    extern s32 getTileRoom(StandTile *tile);
                    StandTile *walked = (StandTile *) pad->stan;
                    f32 dx, dy, dz;
                    if (walkTilesBetweenPoints_NoCallback(&walked, fixpos.f[0], fixpos.f[2],
                                                          pad->pos.f[0], pad->pos.f[2]) && walked != NULL
                        && walked->id != 0)
                    {
                        pad->stan = walked;
                    }
                    dx = fixpos.f[0] - pad->pos.f[0];
                    dy = fixpos.f[1] - pad->pos.f[1];
                    dz = fixpos.f[2] - pad->pos.f[2];
                    fprintf(stderr,
                            "ge: setup lint: pad %d (%s) %s - FIXED room=%d moved=%.0f pos=(%.0f %.0f %.0f)\n",
                            (s32) (pad - g_CurrentSetup.pads), pad->plink, why,
                            getTileRoom((StandTile *) pad->stan),
                            sqrtf(dx * dx + dy * dy + dz * dz),
                            pad->pos.f[0], pad->pos.f[1], pad->pos.f[2]);
                }
                else
                {
                    fprintf(stderr, "ge: setup lint: pad %d (%s) %s - UNRESOLVED\n",
                            (s32) (pad - g_CurrentSetup.pads), pad->plink, why);
                }
                if (pad->stan == NULL)
                {
                    bad++;
                }
            }

            padcount++;
        }
    }

    if (g_CurrentSetup.boundpads != NULL)
    {
        for (vol = g_CurrentSetup.boundpads; vol->plink != NULL; vol++)
        {
            if (vol->stan == NULL || ((StandTile *) vol->stan)->id == 0)
            {
                coord3d fixpos;
                fixpos.f[0] = vol->pos.f[0];
                fixpos.f[1] = vol->pos.f[1];
                fixpos.f[2] = vol->pos.f[2];
                vol->stan = sub_GAME_7F0AFB78(&fixpos.f[0], &fixpos.f[1], &fixpos.f[2], 0);

                if (vol->stan != NULL)
                {
                    StandTile *walked = (StandTile *) vol->stan;
                    if (walkTilesBetweenPoints_NoCallback(&walked, fixpos.f[0], fixpos.f[2],
                                                          vol->pos.f[0], vol->pos.f[2]) && walked != NULL
                        && walked->id != 0)
                    {
                        vol->stan = walked;
                    }
                }

                fprintf(stderr, "ge: setup lint: boundpad %d (%s) has no/zero stan%s\n",
                        (s32) (vol - g_CurrentSetup.boundpads), vol->plink,
                        (vol->stan != NULL) ? " - FIXED via position finder" : " - UNRESOLVED");
                if (vol->stan == NULL)
                {
                    bad++;
                }
            }
        }
    }

    phead = (PropDefHeaderRecord *) g_CurrentSetup.propDefs;
    index = 0;

    if (phead != NULL)
    {
        while (phead->type != PROPDEF_END)
        {
            switch (phead->type)
            {
                case PROPDEF_GUARD:
                {
                    GuardRecord *g = (GuardRecord *) phead;

                    if (g->Data == NULL)
                    {
                        fprintf(stderr, "ge: setup lint: guard %d (chrnum %d, pad %d) did not spawn\n",
                                index, (s16) g->chrnum, g->PadID);
                        bad++;
                    }

                    if (g->chrpreset1 != 0xFFFF && chrFindByLiteralId((s16) g->chrpreset1) == NULL)
                    {
                        fprintf(stderr, "ge: setup lint: guard %d (chrnum %d) chrpreset %d resolves to no chr\n",
                                index, (s16) g->chrnum, (s16) g->chrpreset1);
                        bad++;
                    }

                    if (g->Preset != 0xFFFF && isNotBoundPad(g->Preset) && g->Preset >= padcount)
                    {
                        fprintf(stderr, "ge: setup lint: guard %d (chrnum %d) pad preset %d out of range (%d pads)\n",
                                index, (s16) g->chrnum, g->Preset, padcount);
                        bad++;
                    }
                    break;
                }

                case PROPDEF_OBJECTIVE_START:
                case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
                {
                    /* both layouts share the header/menu/reserved/text head */
                    struct objective_entry *o = (struct objective_entry *) phead;
                    u8 *txt = (o->text != 0) ? langGet(o->text) : NULL;

                    if (o->text == 0 || txt == NULL || txt[0] == '\0')
                    {
                        fprintf(stderr, "ge: setup lint: %s %d text id 0x%04x unresolved\n",
                                (phead->type == PROPDEF_OBJECTIVE_START) ? "objective" : "watchtext",
                                index, o->text);
                        bad++;
                    }
                    break;
                }
            }

            phead = (PropDefHeaderRecord *) (((u32 *) phead) + sizepropdef(phead));
            index++;
        }
    }

    if (bad != 0)
    {
        fprintf(stderr, "ge: setup lint: %d issue(s) above\n", bad);
        fflush(stderr);
    }
}

static void portRestoreSetupRegion(u32 *ptr, u32 (*measure)(u32 *))
{
    /* 256 slots covers all setup files (over 96 already); a setup that doesn't
     * fit is never restored, so it would crash on its next reload. Log loudly
     * if this ever fills. */
    static struct {
        u32 *ptr;
        u32 *copy;
        u32 words;
    } saved[256];
    static s32 savedcount = 0;
    u32 words;
    s32 i;

    if (ptr == NULL)
    {
        return;
    }

    for (i = 0; i < savedcount; i++)
    {
        if (saved[i].ptr == ptr)
        {
            memcpy(ptr, saved[i].copy, saved[i].words * sizeof(u32));
            return;
        }
    }

    if (savedcount >= (s32) ARRAYCOUNT(saved))
    {
        static s32 warned;

        if (!warned)
        {
            warned = 1;
            fprintf(stderr, "ge: portRestoreSetupRegion: snapshot registry full (%d regions) - "
                            "setup regions from here on are NOT restored between loads\n",
                    savedcount);
        }
        return;
    }

    /* first sight: the data is pristine - measure and snapshot */
    words = measure(ptr);

    saved[savedcount].ptr = ptr;
    saved[savedcount].copy = malloc(words * sizeof(u32));
    saved[savedcount].words = words;

    if (saved[savedcount].copy == NULL)
    {
        return;
    }

    memcpy(saved[savedcount].copy, ptr, words * sizeof(u32));
    savedcount++;
}

void portDbgCheckWp(const char *tag)
{
    extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
    f32 v[5];
    s32 bad = 0;
    s32 first = -1;
    s32 last = -1;
    s32 i;

    if (g_CurrentSetup.pathwaypoints == NULL)
    {
        return;
    }
    for (i = 0; g_CurrentSetup.pathwaypoints[i].padID != -1 && i < 1000; i++)
    {
        if ((u32) g_CurrentSetup.pathwaypoints[i].padID > 4096u)
        {
            bad++;
            last = i;
            if (first < 0)
            {
                first = i;
            }
        }
    }
    v[0] = (f32) g_CurrentSetup.pathwaypoints[67].padID;
    v[1] = (f32) bad;
    v[2] = (f32) first;
    v[3] = (f32) last;
    v[4] = (f32) i;
    ge_dbg_vec(tag, v, 5);
}
#endif

#ifndef TARGET_N64
/* GE_PROPDUMP=1 (triage F5): one line per positioned prop after a stage load -
 * index, type, model id, pad, world position and damage capacity. Finding the
 * thing to shoot at was the blocker for testing F5.2/F5.3 headlessly; the CCTV
 * probe solved that for cameras and this generalises it. */
static void portPropDump(void)
{
    extern char *getenv(const char *);
    extern int printf(const char *, ...);
    PropDefHeaderRecord *phead;
    s32 index = 0;

    if (getenv("GE_PROPDUMP") == NULL || g_CurrentSetup.propDefs == NULL)
    {
        return;
    }

    phead = g_CurrentSetup.propDefs;

    while (phead->type != PROPDEF_END)
    {
        /* Every ObjectRecord-shaped propdef is handled, not a hand-picked
         * subset, because a misplaced air vent was hiding under a type the
         * subset skipped. DOOR/GLASS/TAG carry the same header. */
        switch (phead->type)
        {
            case PROPDEF_DOOR:
            case PROPDEF_PROP:
            case PROPDEF_KEY:
            case PROPDEF_ALARM:
            case PROPDEF_AMMO:
            case PROPDEF_ARMOUR:
            case PROPDEF_GAS_RELEASING:
            case PROPDEF_MAGAZINE:
            case PROPDEF_COLLECTABLE:
            case PROPDEF_RACK:
            case PROPDEF_MONITOR:
            case PROPDEF_MULTI_MONITOR:
            case PROPDEF_AUTOGUN:
            case PROPDEF_HAT:
            case PROPDEF_CCTV:
            case PROPDEF_GLASS:
            case PROPDEF_TINTED_GLASS:
            case PROPDEF_SAFE:
            {
                ObjectRecord *obj = (ObjectRecord *) phead;

                printf("ge: prop %3d type=%2d model=%3d pad=%5d room=%3d cap=%.1f flags=%08x pos=(%.0f,%.0f,%.0f)%s\n",
                       index, obj->type, obj->obj, obj->pad,
                       (obj->prop != NULL) ? (s32) obj->prop->rooms[0] : -1,
                       obj->damage, (u32) obj->flags,
                       (obj->prop != NULL) ? obj->prop->pos.f[0] : 0.0f,
                       (obj->prop != NULL) ? obj->prop->pos.f[1] : 0.0f,
                       (obj->prop != NULL) ? obj->prop->pos.f[2] : 0.0f,
                       (obj->prop != NULL) ? "" : " NO-PROP");
                break;
            }
        }

        phead = (PropDefHeaderRecord *) (((u32 *) phead) + sizepropdef(phead));
        index++;
    }
}
#endif

void proplvreset2(enum LEVELID stageId)
{
    ItemModelFileRecord *pitem;
    s32 withchrs;
    s32 withobjs;

#ifndef TARGET_N64
    {
        extern s32 g_PortNotPositioned;
        g_PortNotPositioned = 0; /* the diagnostic cap is per stage load */
    }
#endif

    withchrs = (((void *) tokenFind(1, "-nochr")) == NULL) && (((void *) tokenFind(1, "-noprop")) == NULL);
    withobjs = (((void *) tokenFind(1, "-noobj")) == NULL) && (((void *) tokenFind(1, "-noprop")) == NULL);

    g_DoorScale = 1.0f;

    /**
     * Mark every prop model as "not resident" (RootNode doubles as a loaded
     * flag for modelLoad()) so the loads below fetch data. The last
     * PitemZ_entries entry is a terminator, hence the loop length minus 1.
     */
    for (pitem = PitemZ_entries; pitem < &PitemZ_entries[ARRAYCOUNT(PitemZ_entries) - 1]; pitem++)
    {
        pitem->header->RootNode = NULL;
    }

    if ((stageId <= (LEVELID_MAX + 1)) && setup_text_pointers[stageId])
    {
        char strResource[0x100] = ""; // Scratch buffer for synthesizing the
                                      // setup file's name at runtime.
        s32 numAnimatedObjects = 0;
        s32 numObjects = 0;
        s32 i1 = 0;
        s32 i2 = 0;
        s32 i3 = 0;
        f32 roompos_1;
        s32 i5 = 0;
        s32 i8;
        f32 roompos_2;
        struct stagesetup *local_stage;

        strResource[0] = setup_text_pointers[stageId][0]; // 'U' -> "U"
        strResource[1] = 0; // Terminate so strcat has a valid string.

        /**
         * There are no slots for the mp stages in setup_text_pointers. The name
         * is created by adding "mp_" after the "U" e.g. "Ump_setuparchZ"
         */
        if (getPlayerCount() >= 2)
        {
            strcat(strResource, "mp_"); // -> "Ump_"
        }

        strcat(strResource, setup_text_pointers[stageId] + 1); // Add remaining text back U[mp_] +
                                                               // setupxxxZ

        g_ptrStageSetupFile = _fileNameLoadToBank(strResource, FILELOADMETHOD_DEFAULT, 256, MEMPOOL_STAGE);

#ifndef TARGET_N64
        {
            extern s32 romdataFileServed(s32 index);
            g_portSetupFromRom = romdataFileServed(fileGetIndex((u8 *) strResource));
        }
#endif
#ifdef GE_NATIVE_64
        {
            extern struct stagesetup *romfileSetupNative(const u8 *base);
            g_ptrStageSetupFile = romfileSetupNative((const u8 *)g_ptrStageSetupFile);
            g_portSetupFromRom = 0;
        }
#endif
        local_stage = g_ptrStageSetupFile;
        langLoadToAddr(langGetLangBankIndexFromStagenum(stageId));

        /**
         * The setup file stores every internal reference as a byte offset from
         * the start of the file, so rebase them all onto the RAM copy at
         * local_stage.
         */
        g_CurrentSetup.pathwaypoints = SETUP_PROMOTE(local_stage, local_stage->pathwaypoints);
        g_CurrentSetup.waypointgroups = SETUP_PROMOTE(local_stage, local_stage->waypointgroups);
        g_CurrentSetup.intro = SETUP_PROMOTE(local_stage, local_stage->intro);
        g_CurrentSetup.propDefs = SETUP_PROMOTE(local_stage, local_stage->propDefs);
        g_CurrentSetup.patrolpaths = SETUP_PROMOTE(local_stage, local_stage->patrolpaths);
        g_CurrentSetup.ailists = SETUP_PROMOTE(local_stage, local_stage->ailists);
        g_CurrentSetup.pads = SETUP_PROMOTE(local_stage, local_stage->pads);
        g_CurrentSetup.boundpads = SETUP_PROMOTE(local_stage, local_stage->boundpads);

#ifndef TARGET_N64
#ifndef GE_NATIVE_64
        /* a ROM-served setup is a fresh copy every load (the N64 way) -
         * the snapshot/restore exists only for the compiled-in files */
        if (!g_portSetupFromRom)
        {
            portRestoreSetupRegion((u32 *) g_CurrentSetup.propDefs, portMeasurePropDefs);
            portRestoreSetupRegion((u32 *) g_CurrentSetup.intro, portMeasureIntro);
            portRestoreSetupRegion((u32 *) g_CurrentSetup.pads, portMeasurePads);
            portRestoreSetupRegion((u32 *) g_CurrentSetup.boundpads, portMeasureBoundPads);
        }

        /* PORT: some setup words were emitted as raw big-endian literals
         * instead of through _mkword, so swap these known words on each load:
         *  GUARD word 3 (Preset / chrpreset1 u16s, else hostages flee at spawn),
         *  OBJECTIVE_START word 2 (halves) and word 3 (full byte reverse),
         *  WATCH_MENU_OBJECTIVE_TEXT word 2 (halves). */
        {
            u32 *pd = (u32 *) g_CurrentSetup.propDefs;

            if (pd != NULL)
            {
                while (((PropDefHeaderRecord *) pd)->type != PROPDEF_END)
                {
                    u32 w;

                    switch (((PropDefHeaderRecord *) pd)->type)
                    {
                        case PROPDEF_GUARD:
                            pd[3] = (pd[3] >> 16) | (pd[3] << 16);
                            break;

                        /* Damage capacity (word 29, offset 0x74) is read as one
                         * 16.16 word, but _mkword's pair swap put the value in
                         * the low half, so every destructible object broke in
                         * one shot. Swap it back. */
                        case PROPDEF_DOOR:
                        case PROPDEF_PROP:
                        case PROPDEF_KEY:
                        case PROPDEF_ALARM:
                        case PROPDEF_CCTV:
                        case PROPDEF_MAGAZINE:
                        case PROPDEF_COLLECTABLE:
                        case PROPDEF_MONITOR:
                        case PROPDEF_MULTI_MONITOR:
                        case PROPDEF_RACK:
                        case PROPDEF_AUTOGUN:
                        case PROPDEF_HAT:
                        case PROPDEF_AMMO:
                        case PROPDEF_ARMOUR:
                        case PROPDEF_GAS_RELEASING:
                        case PROPDEF_VEHICHLE:
                        case PROPDEF_AIRCRAFT:
                        case PROPDEF_UNK41:
                        case PROPDEF_GLASS:
                        case PROPDEF_SAFE:
                        case PROPDEF_TANK:
                        case PROPDEF_TINTED_GLASS:
                            pd[29] = (pd[29] >> 16) | (pd[29] << 16);
                            break;

                        case PROPDEF_OBJECTIVE_START:
                            pd[2] = (pd[2] >> 16) | (pd[2] << 16);
                            w = pd[3];
                            pd[3] = (w >> 24) | ((w >> 8) & 0xff00) | ((w & 0xff00) << 8) | (w << 24);
                            break;

                        case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
                            pd[2] = (pd[2] >> 16) | (pd[2] << 16);
                            break;

                        /* GUARD_ATTRIBUTE word 2 (unk8 u16 / unkA s8 /
                         * GrenadeProb s8) was emitted raw, so GrenadeProb read
                         * as 0 on PC. Fully byte-reverse it (unk8/unkA are
                         * always 0, so their order doesn't matter). */
                        case PROPDEF_GUARD_ATTRIBUTE:
                            w = pd[2];
                            pd[2] = (w >> 24) | ((w >> 8) & 0xff00) | ((w & 0xff00) << 8) | (w << 24);
                            break;
                    }

                    pd += sizepropdef((PropDefHeaderRecord *) pd);
                }
            }
        }
#endif

#endif

        // Pad names and bound names are optional. An offset of 0 means absent.
        if (local_stage->padnames != 0)
        {
            g_CurrentSetup.padnames = SETUP_PROMOTE(local_stage, local_stage->padnames);
        }
        else
        {
            g_CurrentSetup.padnames = NULL;
        }

        if (local_stage->boundpadnames != 0)
        {
            g_CurrentSetup.boundpadnames = SETUP_PROMOTE(local_stage, local_stage->boundpadnames);
        }
        else
        {
            g_CurrentSetup.boundpadnames = NULL;
        }

        if (g_CurrentSetup.pathwaypoints)
        {
            for (i1 = 0; g_CurrentSetup.pathwaypoints[i1].padID >= 0; i1++)
            {
                g_CurrentSetup.pathwaypoints[i1].neighbours = SETUP_PROMOTE(local_stage, g_CurrentSetup.pathwaypoints[i1].neighbours);
            }
        }

        if (g_CurrentSetup.waypointgroups)
        {
            for (i2 = 0; g_CurrentSetup.waypointgroups[i2].neighbours; i2++)
            {
                g_CurrentSetup.waypointgroups[i2].neighbours = SETUP_PROMOTE(local_stage, g_CurrentSetup.waypointgroups[i2].neighbours);
                g_CurrentSetup.waypointgroups[i2].waypoints = SETUP_PROMOTE(local_stage, g_CurrentSetup.waypointgroups[i2].waypoints);
            }
        }

        // Convert ailist pointers a.k.a. Action Blocks from file-local to proper pointers
        {
            AIListRecord *ailists = g_CurrentSetup.ailists;
            if (ailists)
            {
                for (i3 = 0; g_CurrentSetup.ailists[i3].ailist != 0; i3++)
                {
                    g_CurrentSetup.ailists[i3].ailist = SETUP_PROMOTE(local_stage, g_CurrentSetup.ailists[i3].ailist);
                }
            }
        }

        if (g_CurrentSetup.patrolpaths)
        {
            for (i3 = 0; g_CurrentSetup.patrolpaths[i3].waypoints != NULL; i3++)
            {
                g_CurrentSetup.patrolpaths[i3].waypoints = SETUP_PROMOTE(local_stage, g_CurrentSetup.patrolpaths[i3].waypoints);

                for (i5 = 0; g_CurrentSetup.patrolpaths[i3].waypoints[i5] >= 0; i5++)
                {
                    // Empty
                }

                g_CurrentSetup.patrolpaths[i3].len = i5;
            }
        }

        if (g_CurrentSetup.pads)
        {
            struct PadRecord *pad;

            roompos_1 = get_room_data_float2();
            pad = g_CurrentSetup.pads;

            for (; pad->plink != NULL; pad++)
            {
                pad->plink = SETUP_PROMOTE(local_stage, pad->plink);
                pad->pos.f[0] *= roompos_1;
                pad->pos.f[1] *= roompos_1;
                pad->pos.f[2] *= roompos_1;
        
#ifdef DEBUG
                {
                    s32 sret = init_pathtable_something(pad, pad->plink, &pad->stan);
                    if (sret == 0)
                    {
                        osSyncPrintf("pad number %d has no stan! (%s)\n", (s32)(pad - g_CurrentSetup.pads), pad->plink);
                    }
                    else if (sret == 2)
                    {
                        osSyncPrintf("pad number %d changed stan from %s to %s\n", (s32)(pad - g_CurrentSetup.pads), pad->plink, GetStanName(pad->stan));
                    }
                }
#else
                init_pathtable_something(pad, pad->plink, &pad->stan);
#endif

                if (1);
            }
        }

        if (g_CurrentSetup.boundpads)
        {
            struct BoundPadRecord *vol;

            roompos_2 = get_room_data_float2();
            vol = g_CurrentSetup.boundpads;
            
            for (; vol->plink != NULL; vol++)
            {
                /** Ugly matching hack. 
                *   TODO: investigate if there's a way to get rid of this.
                */
                if ((((u32) local_stage) ^ 0) + ((u32)vol->plink));

                vol->plink = SETUP_PROMOTE(local_stage, vol->plink);
                vol->pos.f[0] *= roompos_2;
                vol->pos.f[1] *= roompos_2;
                vol->pos.f[2] *= roompos_2;
                vol->bbox.xmin *= roompos_2;
                vol->bbox.xmax *= roompos_2;
                vol->bbox.ymin *= roompos_2;
                vol->bbox.ymax *= roompos_2;
                vol->bbox.zmin *= roompos_2;
                vol->bbox.zmax *= roompos_2;

#ifdef DEBUG
                {
                    s32 sret = init_pathtable_something((struct PadRecord *) vol, vol->plink, &vol->stan);
                    if (sret == 0)
                    {
                        osSyncPrintf("vol number %d has no stan! (%s)\n", (s32)(vol - g_CurrentSetup.boundpads), vol->plink);
                    }
                    else if (sret == 2)
                    {
                        osSyncPrintf("vol number %d changed stan from %s to %s\n", (s32)(vol - g_CurrentSetup.boundpads), vol->plink, GetStanName(vol->stan));
                    }
                }
#else
                init_pathtable_something((struct PadRecord *) vol, vol->plink, &vol->stan);
#endif

                if (1);
            }
        }

        if (g_CurrentSetup.padnames)
        {
            for (i1 = 0; g_CurrentSetup.padnames[i1].p; i1++)
            {
                g_CurrentSetup.padnames[i1].p = SETUP_PROMOTE(local_stage, g_CurrentSetup.padnames[i1].p);
            }
        }

        if (g_CurrentSetup.boundpadnames)
        {
            // Required for matching.
            if (g_CurrentSetup.ailists && g_CurrentSetup.ailists);

            for (i1 = 0; g_CurrentSetup.boundpadnames[i1].p; i1++)
            {
                g_CurrentSetup.boundpadnames[i1].p = SETUP_PROMOTE(local_stage, g_CurrentSetup.boundpadnames[i1].p);
            }
        }

        // PD rejoins here

        if (withchrs)
        {
            alloc_init_GUARDdata_entries(load_proptype(PROPDEF_GUARD));
            numAnimatedObjects += load_proptype(PROPDEF_GUARD);
            numObjects += load_proptype(PROPDEF_COLLECTABLE);
            numObjects += load_proptype(PROPDEF_KEY);
            numObjects += load_proptype(PROPDEF_HAT);
        }
        else
        {
            alloc_init_GUARDdata_entries(0); // chrmgrConfigure
        }

        if (withobjs)
        {
            // load std props for all stages
            numObjects += load_proptype(PROPDEF_DOOR);
            numObjects += load_proptype(PROPDEF_CCTV);
            numObjects += load_proptype(PROPDEF_AUTOGUN);
            numObjects += load_proptype(PROPDEF_RACK);
            numObjects += load_proptype(PROPDEF_MONITOR);
            numObjects += load_proptype(PROPDEF_MULTI_MONITOR);
            numObjects += load_proptype(PROPDEF_ARMOUR);
            numObjects += load_proptype(PROPDEF_PROP);
            numObjects += load_proptype(PROPDEF_GLASS);
            numObjects += load_proptype(PROPDEF_TINTED_GLASS);
            numObjects += load_proptype(PROPDEF_SAFE);
            numObjects += load_proptype(PROPDEF_UNK41);
            numObjects += load_proptype(PROPDEF_GAS_RELEASING);
            numObjects += load_proptype(PROPDEF_ALARM);
            numObjects += load_proptype(PROPDEF_MAGAZINE);
            numObjects += load_proptype(PROPDEF_AMMO);
            numObjects += load_proptype(PROPDEF_VEHICHLE);
            numObjects += load_proptype(PROPDEF_TANK);
            numAnimatedObjects += load_proptype(PROPDEF_AIRCRAFT);
        }

        modelmgrAllocateModelSlots(numObjects);
        modelmgrAllocateAnimModelSlots(numAnimatedObjects);

        for (i8 = 0; i8 < getPlayerCount(); i8++)
        {
            set_cur_player(i8);
            alloc_additional_item_slots(load_proptype(PROPDEF_LINK));
        }

        if (g_CurrentSetup.propDefs)
        {
            PropDefHeaderRecord *phead;
            s32 flags;
            s32 pdefIndex;

            // per-difficulty "Don't Load" mask: PROPFLAG2_00000010/20/40 for Agent/Secret/00
            flags = 1 << (lvlGetSelectedDifficulty() + 4);

            /**
             * Complete the skip loading mask started on the line above. Checks for:
             * - don't load on 2 players
             * - don't load on 3 players
             * - don't load on 4 players
             * - don't load in multiplayer
             */
            if (getPlayerCount() >= 2)
            {
                flags |= 1 << (getPlayerCount() + 20);
            }

            phead = g_CurrentSetup.propDefs;
            pdefIndex = 0;

            while ((i1 = phead->type) != PROPDEF_END)
            {
#ifndef TARGET_N64
                {
                    extern void portHwNotePropdef(s32 type);
                    portHwNotePropdef(phead->type); /* tickhz=hw stage-load setup work (frametiming.c) */
                }
#endif
                switch (phead->type)
                {
                    case PROPDEF_GUARD_ATTRIBUTE:
                    {
                        GuardAttributeRecord *pdef_guarda;
                        u8 prob;
                        ChrRecord *chr;
                        pdef_guarda = (GuardAttributeRecord *) phead;
                        prob = (u8) pdef_guarda->GrenadeProb;
                        chr = chrFindByLiteralId(pdef_guarda->chrnum);
                        if ((chr && chr->prop) && chr->model)
                        {
                            chr->grenadeprob = prob;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("grenade prob: no chr number %d for obj number %d! ", pdef_guarda->GrenadeProb, pdefIndex + 1);
                        }
#endif
                        break;
                    }
                    case PROPDEF_GUARD:
                        if (withchrs)
                        {
                            expand_09_characters(stageId, (struct GuardRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_DOOR:
                        if (withobjs && (!(((struct DoorRecord *) phead)->flags2 & flags)))
                        {
                            setupDoor(stageId, (struct DoorRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_DOOR_SCALE:
                        g_DoorScale = ((struct GlobalDoorScaleRecord *) phead)->Scale / M_U16_MAX_VALUE_F;
                        break;
                    case PROPDEF_COLLECTABLE:
                        if (withchrs && (!(((struct WeaponObjRecord *) phead)->flags2 & flags)))
                        {
                            weaponAssignToHome(stageId, (struct WeaponObjRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_KEY:
                        if (withchrs && (!(((struct KeyRecord *) phead)->flags2 & flags)))
                        {
                            setupKey(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_HAT:
                        if (withchrs && (!(((struct ObjectRecord *) phead)->flags2 & flags)))
                        {
                            setupHat(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_CCTV:
                        if (withobjs && (!(((struct CCTVRecord *) phead)->flags2 & flags)))
                        {
                            setupCctv(stageId, (struct CCTVRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_AUTOGUN:
                        if (withobjs && (!(((struct AutogunRecord *) phead)->flags2 & flags)))
                        {
                            setupAutogun(stageId, (struct AutogunRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_RACK:
                        if (withobjs && (!(((struct ObjectRecord *) phead)->flags2 & flags)))
                        {
                            setupHangingMonitors(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_MONITOR:
                        if (withobjs && (!(((struct MonitorObjRecord *) phead)->flags2 & flags)))
                        {
                            setupSingleMonitor(stageId, (struct MonitorObjRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_MULTI_MONITOR:
                        if (withobjs && (!(((struct MultiMonitorObjRecord *) phead)->flags2 & flags)))
                        {
                            setupMultiMonitor(stageId, (struct MultiMonitorObjRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_ARMOUR:
                    {
                        struct BodyArmourRecord *pdef_ba = (struct BodyArmourRecord *) phead;
#ifndef TARGET_N64
                        /* No Armor (Multiplayer Options): don't create armour
                         * pickups, the same way a difficulty-excluded object is
                         * left out above (its prop stays NULL). */
                        extern s32 portMpNoArmorActive(void); /* video.c */
                        if (portMpNoArmorActive())
                        {
                            break;
                        }
#endif
#ifndef VERSION_US
                        if (withobjs && (((pdef_ba->flags2 & flags) == 0) || j_text_trigger)) // JP: armour setup also proceeds when
                                                                                              // j_text_trigger is set
#else
                        if (withobjs && ((pdef_ba->flags2 & flags) == 0))
#endif
                        {
                            pdef_ba->initialamount = (*((s32 *) (&pdef_ba->initialamount))) / M_U16_MAX_VALUE_F;
                            pdef_ba->amount = pdef_ba->initialamount;
                            domakedefaultobj(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_TINTED_GLASS:
                    {
                        if (withobjs && (!(((struct TintedGlassRecord *) phead)->flags2 & flags)))
                        {
                            if (((struct TintedGlassRecord *) phead)->flags & PROPFLAG_GLASS_HASPORTAL)
                            {
                                if (!(((struct TintedGlassRecord *) phead)->pad < 10000))
                                {
                                    struct coord3d up;
                                    struct coord3d up2;
                                    BoundPadRecord *pad3d;

                                    pad3d = &g_CurrentSetup.boundpads[((struct TintedGlassRecord *) phead)->pad - 10000];
                                    sub_GAME_7F001BD4(pad3d, &up);
                                    up2.x = (10.0f * pad3d->up.x) + up.x;
                                    up2.y = (10.0f * pad3d->up.y) + up.y;
                                    up2.z = (10.0f * pad3d->up.z) + up.z;
                                    up.x -= 10.0f * pad3d->up.x;
                                    up.y -= 10.0f * pad3d->up.y;
                                    up.z -= 10.0f * pad3d->up.z;

                                    ((struct TintedGlassRecord *) phead)->portalnum = sub_GAME_7F0B9E04(&up, &up2);
#ifndef TARGET_N64
                                    { /* glass-portal audit: pad, derived portal (-1 = none) */
                                        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
                                        f32 v[3];
                                        v[0] = (f32) ((struct TintedGlassRecord *) phead)->pad;
                                        v[1] = (f32) ((struct TintedGlassRecord *) phead)->portalnum;
                                        v[2] = (f32) ((struct TintedGlassRecord *) phead)->flags2;
                                        ge_dbg_vec("glassportal pad/portal/flags2", v, 3);
                                    }
#endif
                                    ((struct TintedGlassRecord *) phead)->unk90 = (*((s32 *) (&((struct TintedGlassRecord *) phead)->unk90))) / M_U16_MAX_VALUE_F;
                                }
                            }
                            domakedefaultobj(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_PROP:
                    case PROPDEF_ALARM:
                    case PROPDEF_MAGAZINE:
                    case PROPDEF_GAS_RELEASING:
                    case PROPDEF_UNK41:
                    case PROPDEF_GLASS:
                    case PROPDEF_SAFE:
                        if (withobjs && (!(((ObjectRecord *) phead)->flags2 & flags)))
                        {
                            domakedefaultobj(stageId, (struct ObjectRecord *) phead, pdefIndex);
                        }
                        break;
                    case PROPDEF_AMMO:
                    {
                        struct MultiAmmoCrateRecord *pdef_macr = (struct MultiAmmoCrateRecord *) phead;
                        s32 ammoqty = 1;
                        s32 i9;

                        if (getPlayerCount() >= 2)
                        {
                            struct s_mp_weapon_set *mpweapon = &getPtrMPWeaponSetData()[lastmpweaponnum];
                            
                            ammoqty = mpweapon->ammoamount;
                            if (mpweapon->ammotype);
                            pdef_macr->slots[mpweapon->ammotype - 1].quantity = ammoqty;
                        }

                        if (((ammoqty > 0) && withobjs) && (!(pdef_macr->flags2 & flags)))
                        {
                            for (i9 = 0; i9 < AMMOTYPE_GLOBAL_MAX; i9++)
                            {
                                if ((pdef_macr->slots[i9].quantity > 0) && (pdef_macr->slots[i9].modelnum != 0xFFFF))
                                {
                                    modelLoad(pdef_macr->slots[i9].modelnum);
                                }
                            }

                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_macr, pdefIndex);
                        }
                        break;
                    }
                    case PROPDEF_TANK:
                        if (withobjs && (!(((struct TankRecord *) phead)->flags2 & flags)))
                        {
                            struct TankRecord *pdef_tank = (struct TankRecord *) phead;
                            struct PropRecord *tank_prop;

                            s32 padding;
                            f32 stan_y = 0.0f;
                            s32 paddinggg[4];

                            weaponLoadProjectileModels(ITEM_TANKSHELLS);
                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_tank, pdefIndex);
                            pdef_tank->turret_vertical_angle = 0.0f;
                            pdef_tank->turret_orientation_angle = 0.0f;
                            pdef_tank->tank_orientation_angle = M_TAU_F - atan2f(pdef_tank->mtx.m[2][0], pdef_tank->mtx.m[2][2]);
                            tank_prop = pdef_tank->prop;

                            if (tank_prop)
                            {
                                stan_y = stanGetPositionYValue(tank_prop->stan, tank_prop->pos.f[0], tank_prop->pos.f[2]);
                            }

                            pdef_tank->stan_y = stan_y;
#ifdef VERSION_EU
                            pdef_tank->unkD0 = stan_y / 0.2004f; // EU-tuned constant
#else
                            pdef_tank->unkD0 = stan_y / 0.17000002f;
#endif
                        }
                        break;
                    case PROPDEF_VEHICHLE:
                        if (withobjs && (!(((struct VehichleRecord *) phead)->flags2 & flags)))
                        {
                            struct VehichleRecord *pdef_veh = (struct VehichleRecord *) phead;

                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_veh, pdefIndex);

                            if (pdef_veh->model != NULL)
                            {
                                if (pdef_veh->model->obj->Switches[5] != NULL)
                                {
                                    modelGetNodeRwData(pdef_veh->model, pdef_veh->model->obj->Switches[5])->Raw.unk00 = (pdef_veh->flags & 0x10000000) == 0;
                                }
                            }

                            pdef_veh->speed        = 0.0f;
                            pdef_veh->wheelxrot    = 0.0f;
                            pdef_veh->wheelyrot    = 0.0f;
                            pdef_veh->speedaim     = 0.0f;
                            pdef_veh->turnrot60    = 0.0f;
                            pdef_veh->roty         = 0.0f;
                            pdef_veh->speedtime60  = -1.0f;
                            pdef_veh->ailist       = ailistFindById(pdef_veh->ailist);
                            pdef_veh->aioffset     = 0;
                            pdef_veh->aireturnlist = -1;
                            pdef_veh->path         = 0;
                            pdef_veh->nextstep     = 0;
                            pdef_veh->Sound        = 0;
                        }
                        break;
                    case PROPDEF_AIRCRAFT:
                        if (withobjs && (!(((struct AircraftRecord *) phead)->flags2 & flags)))
                        {
                            struct AircraftRecord *pdef_air = (struct AircraftRecord *) phead;

                            domakedefaultobj(stageId, (struct ObjectRecord *) pdef_air, pdefIndex);
                            pdef_air->speed           = 0.0f;
                            pdef_air->speedaim        = 0.0f;
                            pdef_air->rotoryrot       = 0.0f;
                            pdef_air->rotaryspeed     = 0.0f;
                            pdef_air->rotaryspeedaim  = 0.0f;
                            pdef_air->yrot            = 0.0f;
                            pdef_air->speedtime60     = -1.0f;
                            pdef_air->rotaryspeedtime = -1.0f;
                            pdef_air->ailist          = ailistFindById(pdef_air->ailist);
                            pdef_air->aioffset        = 0;
                            pdef_air->aireturnlist    = -1;
                            pdef_air->nextstep        = 0;
                            pdef_air->path            = 0;
                            pdef_air->Sound           = 0;
                        }
                        break;
                    case PROPDEF_TAG:
                    {
                        struct TagObjectRecord *pdef_tag;
                        struct ObjectRecord *taggedobj;

                        pdef_tag = (struct TagObjectRecord *) phead;
                        taggedobj = setupCommandGetObject(stageId, pdefIndex + ((s32) pdef_tag->OffsetToObj));
                        pdef_tag->TaggedObject = taggedobj;

                        if (taggedobj)
                        {
                            taggedobj->runtime_bitflags |= RUNTIMEBITFLAG_TAGGED;
                        }

                        set_parent_cur_tag_entry(pdef_tag);
                        break;
                    }
                    case PROPDEF_RENAME:
                    {
                        struct RenameObjectRecord *pdef_ren;
                        struct ObjectRecord *targetobj;

                        pdef_ren = (struct RenameObjectRecord *) phead;
                        i3 = pdef_ren->TagID + pdefIndex;
                        targetobj = setupCommandGetObject(stageId, i3);
                        pdef_ren->renobj = targetobj;

                        if (targetobj)
                        {
                            targetobj->runtime_bitflags |= RUNTIMEBITFLAG_DESTROYED;
                        }

                        bondinvAddTextOverride((struct textoverride *) pdef_ren);
                        break;
                    }
                    case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
                        setup_briefing_text_entry_parent((struct setup_objective_text *) phead);
                        break;
                    case PROPDEF_CAMERAPOS:
                    {
                        struct CutsceneRecord *pdef_cam = (struct CutsceneRecord *) phead;

                        pdef_cam->pos.f[0] = (*((s32 *) (&pdef_cam->pos.f[0]))) / 100.0f;
                        pdef_cam->pos.f[1] = (*((s32 *) (&pdef_cam->pos.f[1]))) / 100.0f;
                        pdef_cam->pos.f[2] = (*((s32 *) (&pdef_cam->pos.f[2]))) / 100.0f;
                        pdef_cam->theta = (*((s32 *) (&pdef_cam->theta))) / M_U16_MAX_VALUE_F;
                        pdef_cam->verta = (*((s32 *) (&pdef_cam->verta))) / M_U16_MAX_VALUE_F;
                        break;
                    }
                    case PROPDEF_OBJECTIVE_START:
                        add_ptr_to_objective((struct objective_entry *) phead);
                        break;
                    case PROPDEF_OBJECTIVE_ENTER_ROOM:
                        set_parent_cur_obj_enter_room((struct criteria_roomentered *) phead);
                        break;
                    case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
                        set_parent_cur_obj_deposited_in_room((struct criteria_deposit *) phead);
                        break;
                    case PROPDEF_OBJECTIVE_PHOTOGRAPH:
                        set_parent_cur_obj_photograph((struct criteria_picture *) phead);
                        break;
                }

                phead = (PropDefHeaderRecord *) (((u32 *) phead) + sizepropdef(phead));
                pdefIndex++;
            }

            phead = g_CurrentSetup.propDefs;
            pdefIndex = 0;

            while (phead->type != PROPDEF_END)
            {
                switch (phead->type)
                {
                    case PROPDEF_PROP:
                    case PROPDEF_KEY:
                    case PROPDEF_MAGAZINE:
                    case PROPDEF_COLLECTABLE:
                    case PROPDEF_MONITOR:
                    case PROPDEF_AMMO:
                    case PROPDEF_ARMOUR:
                    case PROPDEF_GAS_RELEASING:
                    case PROPDEF_UNK41:
                    case PROPDEF_GLASS:
                    case PROPDEF_SAFE:
                    case PROPDEF_TINTED_GLASS:
                    {
                        struct ObjectRecord *pdef_obj = (struct ObjectRecord *) phead;

                        if (pdef_obj->prop && (pdef_obj->flags & PROPFLAG_INSIDEANOTHEROBJ))
                        {
                            u32 offset = pdef_obj->pad;
                            struct ObjectRecord *inobj = setupCommandGetObject(stageId, offset + pdefIndex);

                            if (inobj && inobj->prop)
                            {
                                pdef_obj->runtime_bitflags |= RUNTIMEBITFLAG_HASOWNER;
                                modelSetScale(pdef_obj->model, pdef_obj->model->scale);
                                chrpropReparent(pdef_obj->prop, inobj->prop);
                            }

#ifdef DEBUG
                            //possibly wrong place
                            else
                            {
                                osSyncPrintf("inobj link not found for object number %d\n", pdefIndex + 1);
                            }
#endif

                        }
                        break;
                    }
                    case PROPDEF_LINK:
                    {
                        struct LinkRecord *pdef_link = (struct LinkRecord *) phead;
                        struct WeaponObjRecord *guna = (struct WeaponObjRecord *) setupGetPtrToCommandByIndex(pdef_link->Index1 + pdefIndex);
                        struct WeaponObjRecord *gunb = (struct WeaponObjRecord *) setupGetPtrToCommandByIndex(pdef_link->Index2 + pdefIndex);

                        if (guna && gunb)
                        {
                            if ((guna->type == PROPDEF_COLLECTABLE) && (gunb->type == PROPDEF_COLLECTABLE))
                            {
                                propweaponSetDual(guna, gunb);
                            }
#ifdef DEBUG
                            else
                            {
                                osSyncPrintf("link type wrong for doublegun object number %d\n", pdefIndex + 1);
                            }
                        }
                        else
                        {
                            osSyncPrintf("link not found for doublegun object number %d\n", pdefIndex + 1);
#endif

                        }

                        break;
                    }
                    case PROPDEF_SWITCH:
                    {
                        struct LinkRecord *pdef_switch;
                        struct ObjectRecord *doorA;
                        struct ObjectRecord *doorB;
                        s32 index1;
                        s32 index2;

                        pdef_switch = (struct LinkRecord *) phead;
                        index1 = pdef_switch->Index1;
                        index2 = pdef_switch->Index2;
                        doorA = (struct ObjectRecord *) setupCommandGetObject(stageId, pdefIndex + index1);
                        doorB = (struct ObjectRecord *) setupGetPtrToCommandByIndex(pdefIndex + index2);

                        if ((((doorA && doorA->prop) && doorB) && (doorB->type == PROPDEF_DOOR)) && doorB->prop)
                        {
                            pdef_switch->first = doorA->prop;
                            pdef_switch->second = doorB->prop;
                            initSetLevelLoadPropSwitch(pdef_switch);
                            doorA->runtime_bitflags |= RUNTIMEBITFLAG_00000001; // linked door
                        }

#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("doorlink object number %d not initialised\n", pdefIndex + 1);
                        }
#endif

                        break;
                    }
                    case PROPDEF_SAFE_ITEM:
                    {
                        s32 index1;
                        struct SafeObjectRecord *pdef_safe;
                        s32 index2;
                        s32 index3;
                        struct ObjectRecord *safe_item;
                        struct SafeRecord *safe;
                        struct DoorRecord *door;

                        pdef_safe = (struct SafeObjectRecord *) phead;
                        index1 = pdef_safe->Index1;
                        index2 = pdef_safe->Index2;
                        index3 = pdef_safe->Index3;
                        safe_item = setupCommandGetObject(stageId, pdefIndex + index1);
                        safe = (struct SafeRecord *) setupCommandGetObject(stageId, pdefIndex + index2);
                        door = (struct DoorRecord *) setupCommandGetObject(stageId, pdefIndex + index3);

                        if (((((((safe_item && safe_item->prop) && safe) && safe->prop) && (safe->type == PROPDEF_SAFE)) && door) && door->prop) && (door->type == PROPDEF_DOOR))
                        {
                            pdef_safe->item = safe_item;
                            pdef_safe->safe = safe;
                            pdef_safe->door = door;
                            initSetLevelLoadPropSafeItem((struct ObjectRecord *) pdef_safe);
                            safe_item->flags2 |= PROPFLAG2_LINKEDTOSAFE;
                            door->flags2 |= PROPFLAG2_LINKEDTOSAFE;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("safelink object number %d not initialised\n", pdefIndex + 1);
                        }
#endif
                        break;
                    }
                    case PROPDEF_LOCK_DOOR:
                    {
                        struct LockDoorRecord *pdef_lock_door;
                        struct DoorRecord *door;
                        struct ObjectRecord *lock;
                        s32 index1;
                        s32 index2;

                        pdef_lock_door = (struct LockDoorRecord *) phead;

                        index1 = pdef_lock_door->Index1;
                        index2 = pdef_lock_door->Index2;

                        door = (struct DoorRecord *) setupCommandGetObject(stageId, pdefIndex + index1);
                        lock = setupCommandGetObject(stageId, pdefIndex + index2);

                        if ((((door && door->prop) && lock) && lock->prop) && (door->type == PROPDEF_DOOR))
                        {
                            pdef_lock_door->door = door;
                            pdef_lock_door->lock = lock;
                            initSetLevelLoadPropLockDoor(pdef_lock_door);
                            door->runtime_bitflags |= RUNTIMEBITFLAG_PADLOCKEDDOOR;
                        }
#ifdef DEBUG
                        else
                        {
                            osSyncPrintf("doorlock object number %d not initialised\n", pdefIndex + 1);
                        }
#endif
                        break;
                    }
                }

                phead = (PropDefHeaderRecord *) (((u32 *) phead) + sizepropdef(phead));
                pdefIndex += 1;
            }
        }
    }
    else
    {
        g_CurrentSetup.pathwaypoints = NULL;
        g_CurrentSetup.waypointgroups = NULL;
        g_CurrentSetup.intro = 0;
        g_CurrentSetup.propDefs = 0;
        g_CurrentSetup.patrolpaths = NULL;
        g_CurrentSetup.ailists = NULL;
        g_CurrentSetup.pads = NULL;
        g_CurrentSetup.boundpads = NULL;
        g_CurrentSetup.padnames = NULL;
        g_CurrentSetup.boundpadnames = NULL;
        alloc_init_GUARDdata_entries(0);
        modelmgrAllocateModelSlots(0);
        modelmgrAllocateAnimModelSlots(0);
    }

#ifndef TARGET_N64
    portLintSetup();
    portPropDump();
#endif

    alloc_false_GUARDdata_to_exec_global_action();
}
