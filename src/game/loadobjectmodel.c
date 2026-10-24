#include <ultra64.h>
#include "game/objecthandler.h"
#include <bondtypes.h>
#include "chrai.h"
#include "chrobjdata.h"
#ifndef TARGET_N64
#include "frametiming.h" /* tickhz=hw stage-load work counters */
#endif
#include "propobj.h"
#include "loadobjectmodel.h"
#include "stan.h"
#include "model.h"

/**
 * Address 0x7F056850.
 * @brief getposstan
*/
s32 getposstan(struct coord3d *pos, StandTile *stan, f32 radius, struct coord3d *posReturn, StandTile **stanReturn)
{
    posReturn->f[0] = pos->f[0];
    posReturn->f[1] = pos->f[1];
    posReturn->f[2] = pos->f[2];

    *stanReturn = stan;

    if (stan == 0)
    {
#ifdef DEBUG
        osSyncPrintf("getposstan: no stan!\n");
#endif
        return 0;
    }

    if ((radius > 0.0f) && (stanTestVolume(stanReturn, posReturn->f[0], posReturn->f[2], radius, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER, 0.0f, 1.0f) >= 0))
    {
#ifdef DEBUG
        osSyncPrintf("getposstan: circle not legal!\n");
#endif
        return 0;
    }

    return 1;
}

//Todo: finish this func
/**
 * Get Size of Prop Definition
 * @param pdef:  Prop Defenition to get size of
 * @return Size of prop in Words (32bit)
*/
s32 sizepropdef(PropDefHeaderRecord *pdef)
{
#ifdef GE_NATIVE_64
    extern s32 romfileNativePropSize(u8 type);
    return romfileNativePropSize(pdef->type) / 4;
#endif
    #if 1
    switch (pdef->type)
    {
        case PROPDEF_GUARD:
            return sizeof(GuardRecord) / 4;
        case PROPDEF_DOOR:
            return sizeof(DoorRecord) / 4;
        case PROPDEF_DOOR_SCALE:
            return sizeof(GlobalDoorScaleRecord) / 4;
        case PROPDEF_PROP:
            return sizeof(ObjectRecord) / 4;
        case PROPDEF_GLASS:
            return sizeof(ObjectRecord) / 4; // Rare typo?
        case PROPDEF_TINTED_GLASS:
            return sizeof(TintedGlassRecord) / 4;
        case PROPDEF_SAFE:
            return sizeof(ObjectRecord) / 4; // wrong
        case PROPDEF_GAS_RELEASING:
            return sizeof(ObjectRecord) / 4; // also wrong, gas is only 2 words
        case PROPDEF_KEY:
            return sizeof(KeyRecord) / 4;
        case PROPDEF_ALARM:
            return sizeof(ObjectRecord) / 4;
        case PROPDEF_CCTV:
            return 0x3b;//return sizeof(GlassRecord) / 4;
        case PROPDEF_MAGAZINE:
            return 0x21;//return sizeof(GlassRecord) / 4;
        case PROPDEF_COLLECTABLE:
            return 0x22;//return sizeof(GlassRecord) / 4;
        case PROPDEF_MONITOR:
            return 0x40;//return sizeof(GlassRecord) / 4;
        case PROPDEF_MULTI_MONITOR:
            return 0x95;//return sizeof(GlassRecord) / 4;
        case PROPDEF_RACK:
            return sizeof(ObjectRecord) / 4;
        case PROPDEF_AUTOGUN:
            return 0x36;//return sizeof(GlassRecord) / 4;
        case PROPDEF_LINK:
            return 3;//return sizeof(GlassRecord) / 4;
        case PROPDEF_HAT:
            return sizeof(ObjectRecord) / 4;
        case PROPDEF_GUARD_ATTRIBUTE:
            return 3;//return sizeof(GlassRecord) / 4;
        case PROPDEF_SWITCH:
            return 4;//return sizeof(GlassRecord) / 4;
        case PROPDEF_SAFE_ITEM:
            return 5;//return sizeof(GlassRecord) / 4;
        case PROPDEF_AMMO:
            return 0x2d;//return sizeof(GlassRecord) / 4;
        case PROPDEF_ARMOUR:
            return 0x22;//return sizeof(GlassRecord) / 4;
        case PROPDEF_TAG:
            return 4;//return sizeof(GlassRecord) / 4;
        case PROPDEF_RENAME:
            return 10;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_START:
            return 4;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_END:
            return 1;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
            return 2;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
            return 2;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_FAIL_CONDITION:
            return 2;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
            return 2;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
            return 2;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_PHOTOGRAPH:
            return 4;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_NULL:
            return 1;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_ENTER_ROOM:
            return 4;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
            return 5;//return sizeof(GlassRecord) / 4;
        case PROPDEF_OBJECTIVE_COPY_ITEM:
            return 1;//return sizeof(GlassRecord) / 4;
        case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
            return 4;//return sizeof(GlassRecord) / 4;
        case PROPDEF_LOCK_DOOR:
            return 4;//return sizeof(GlassRecord) / 4;
        case PROPDEF_VEHICHLE:
            return 0x2c;//return sizeof(GlassRecord) / 4;
        case PROPDEF_AIRCRAFT:
            return 0x2d;//return sizeof(GlassRecord) / 4;
        case PROPDEF_TANK:
            return 0x38;//return sizeof(GlassRecord) / 4;
        case PROPDEF_CAMERAPOS:
            return 7;//return sizeof(GlassRecord) / 4;
        default:
            #ifdef DEBUG
            osSyncPrintf("sizepropdef: unknown prop def type %d!!\n",pdef->type);
            #endif
            return sizeof(PropDefHeaderRecord) / 4;;
    }
    #else
    switch (pdef->type)
    {
        case PROPDEF_GUARD:
            return sizeof(GuardRecord) / 4;
        case PROPDEF_DOOR:
            return sizeof(DoorRecord) / 4;
        case PROPDEF_DOOR_SCALE:
            return sizeof(GlobalDoorScaleRecord) / 4;
        case PROPDEF_PROP:
            return sizeof(ObjectRecord) / 4;
        case PROPDEF_GLASS:
            return sizeof(GlassRecord) / 4;
        case PROPDEF_TINTED_GLASS:
            return sizeof(TintedGlassRecord) / 4;
        case PROPDEF_SAFE:
            return sizeof(SafeRecord) / 4;
        case PROPDEF_GAS_RELEASING:
            return sizeof(GasReleasingRecord) / 4;
        case PROPDEF_KEY:
            return sizeof(KeyRecord) / 4;
        case PROPDEF_ALARM:
            return sizeof(ObjectRecord) / 4;
        case PROPDEF_CCTV:
            return sizeof(CCTVRecord) / 4;
        case PROPDEF_MAGAZINE:
            return sizeof(AmmoCrateRecord) / 4;
        case PROPDEF_COLLECTABLE:
            return sizeof(WeaponObjRecord) / 4;
        case PROPDEF_MONITOR:
            return sizeof(MonitorObjRecord) / 4;
        case PROPDEF_MULTI_MONITOR:
            return sizeof(MultiMonitorObjRecord) / 4;
        case PROPDEF_RACK:
            return sizeof(ObjectRecord) / 4;
        case PROPDEF_AUTOGUN:
            return sizeof(AutogunRecord) / 4;
        case PROPDEF_LINK:
            return sizeof(LinkRecord) / 4;
        case PROPDEF_HAT:
            return sizeof(HatRecord) / 4;
        case PROPDEF_GUARD_ATTRIBUTE:
            return sizeof(GuardAttributeRecord) / 4;
        case PROPDEF_SWITCH:
            return sizeof(LinkRecord) / 4; /* Switch is typedef'd to LinkRecord */
        case PROPDEF_SAFE_ITEM:
            return sizeof(SafeObjectRecord) / 4;
        case PROPDEF_AMMO:
            return sizeof(MultiAmmoCrateRecord) / 4;
        case PROPDEF_ARMOUR:
            return sizeof(BodyArmourRecord) / 4;
        case PROPDEF_TAG:
            return sizeof(TagObjectRecord) / 4;
        case PROPDEF_RENAME:
            return sizeof(RenameObjectRecord) / 4;
        case PROPDEF_OBJECTIVE_START:
            return sizeof(MissionObjectiveRecord) / 4;
        case PROPDEF_OBJECTIVE_END:
            return sizeof(MissionObjectiveRecord) / 4; /* End mission objective shares layout */
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
            return sizeof(DestroyObjectRecord) / 4;
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
            return sizeof(CompleteConditionRecord) / 4;
        case PROPDEF_OBJECTIVE_FAIL_CONDITION:
            return sizeof(FailConditionRecord) / 4;
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
            return sizeof(CollectObjectRecord) / 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
            return sizeof(DepositObjectRecord) / 4;
        case PROPDEF_OBJECTIVE_PHOTOGRAPH:
            return sizeof(PhotographObjectRecord) / 4;
        case PROPDEF_OBJECTIVE_NULL:
            return sizeof(NULLObjectRecord) / 4;
        case PROPDEF_OBJECTIVE_ENTER_ROOM:
            return sizeof(EnterRoomRecord) / 4;
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
            return sizeof(DepositObjectInRoomRecord) / 4;
        case PROPDEF_OBJECTIVE_COPY_ITEM:
            return sizeof(CoopyObjectRecord) / 4;
        case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
            return sizeof(WatchMenuObjectiveTextRecord) / 4;
        case PROPDEF_LOCK_DOOR:
            return sizeof(LockDoorRecord) / 4;
        case PROPDEF_VEHICHLE:
            return sizeof(VehichleRecord) / 4;
        case PROPDEF_AIRCRAFT:
            return sizeof(AircraftRecord) / 4;
        case PROPDEF_TANK:
            return sizeof(TankRecord) / 4;
        case PROPDEF_CAMERAPOS:
            return sizeof(CutsceneRecord) / 4;
        default:
#ifdef DEBUG
            osSyncPrintf("sizepropdef: unknown prop def type %d!!\n", pdef->type);
#endif
            return sizeof(PropDefHeaderRecord) / 4;
    }
    #endif
}




/*
 *Return Item by Setup index
 *Setup Array is most likley PropDefHeaderRecord since size was 4
 */
ObjectRecord *setupGetPtrToCommandByIndex(s32 index) //#MATCH
{
    PropDefHeaderRecord *object = g_CurrentSetup.propDefs; //wow, first use of header, cool

    if (index >= 0 && object)
    {
        s32 i;
        for (i = 0; object->type != PROPDEF_END; i++)
        {
#ifndef TARGET_N64
            g_PortHwWork[PORTHW_SETUPIDX_STEPS]++;
#endif
            if (i == index)
            {
                return object;
            }

            object = sizepropdef(object) + object; //This is correct order, using += swaps t7/s1
        }
    }

    return NULL;
}




/**
 * Address 0x7F056B1C.
*/
s32 tagGetCommandIndex(struct ObjectRecord *tag)
{
    PropDefHeaderRecord *object;
    s32 i;

    object = g_CurrentSetup.propDefs;

    if (object != NULL)
    {
        for (i = 0; object->type != PROPDEF_END; i++)
        {
            if ((void*)object == (void*)tag)
            {
                return i;
            }

            object = sizepropdef(object) + object;
        }
    }

    return -1;
}



/**
 * Address 0x7F056BA8.
*/
s32 setupGetCommandIndexByProp(struct PropRecord *prop)
{
    PropDefHeaderRecord *object;
    s32 i;

    object = g_CurrentSetup.propDefs;

    if (object != NULL)
    {
        for (i = 0; object->type != PROPDEF_END; i++)
        {
            if ((void*)((struct ObjectRecord *)object)->prop == (void*)prop)
            {
                return i;
            }

            object = sizepropdef(object) + object;
        }
    }

    return -1;
}



s32 modelLoad(s32 modelid)
{
#ifndef TARGET_N64
    /* Guard against a NULL PitemZ_entries[id].header, seen as an instant crash
     * loading 4-player MP via the hand-weapon equip path; a NULL here means an
     * out-of-range id or a stomped table. Log the culprit and soft-fail, since
     * callers tolerate a missing weapon model far better than a crash. */
    {
        extern int printf(const char *, ...);
        if (modelid < 0 || modelid >= 340 || PitemZ_entries[modelid].header == NULL)
        {
            printf("modelLoad: BAD modelid=%d header=%p (table 0..339)\n",
                   modelid,
                   (modelid >= 0 && modelid < 340) ? (void *) PitemZ_entries[modelid].header : (void *) 0);
            return FALSE;
        }
    }
#endif
    if (PitemZ_entries[modelid].header->RootNode == NULL)
    {
        fileLoad(PitemZ_entries[modelid].header,PitemZ_entries[modelid].filename/*, "prop"*/);
        modelCalculateRwDataLen(PitemZ_entries[modelid].header);
        return TRUE;
    }
#ifndef TARGET_N64
    /* PORT: compiled-in prop models link with RootNode already set, so the
     * vanilla branch above never assigns per-node RwDataIndexes and every part
     * of a multi-part model shares slot 0 (other parts then draw the last
     * part's display list). Call modelCalculateRwDataLen once per modelid here,
     * BEFORE the first instance allocates its rwdata, as the chr/gun/menu load
     * paths already do. */
    {
        static u8 s_RwIndexed[340];
        if (modelid >= 0 && modelid < 340 && !s_RwIndexed[modelid])
        {
            s_RwIndexed[modelid] = 1;
            modelCalculateRwDataLen(PitemZ_entries[modelid].header);
        }
    }
#endif
    return FALSE;
}




/**
 * Address 0x7F056CA0.
*/
void setupUpdateObjectRoomPosition(ObjectRecord *obj)
{
    PropRecord *temp_s1;
    struct coord3d sp40;
    struct coord3d sp34;
    struct ModelRoData_BoundingBoxRecord *sp30;
    f32 phi_f20;

    temp_s1 = obj->prop;
    phi_f20 = 0.0f;
    chrpropDeregisterRooms(temp_s1);

    if (obj->flags2 & 0x20000)
    {
        if (temp_s1->stan != NULL)
        {
            temp_s1->rooms[0] = temp_s1->stan->room;
            temp_s1->rooms[1] = (u8)-1;
        }
        else
        {
            temp_s1->rooms[0] = (u8)-1;
        }
    }
    else
    {
        sp30 = chrobjGetBboxFromObjectRecord(obj);

        if (sp30 != NULL)
        {
            sp40.f[0] = chrpropSumMatrixPosX(sp30, &obj->mtx) - 30.0f;
            sp40.f[1] = chrpropSumMatrixPosY(sp30, &obj->mtx);
            sp40.f[2] = chrpropSumMatrixPosZ(sp30, &obj->mtx) - 30.0f;

            sp34.f[0] = chrpropSumMatrixNegX(sp30, &obj->mtx) + 30.0f;
            sp34.f[1] = chrpropSumMatrixNegY(sp30, &obj->mtx);
            sp34.f[2] = chrpropSumMatrixNegZ(sp30, &obj->mtx) + 30.0f;

            if (phi_f20 < -sp40.f[0])
            {
                phi_f20 = -sp40.f[0];
            }

            if (phi_f20 < -sp40.f[2])
            {
                phi_f20 = -sp40.f[2];
            }

            if (phi_f20 < sp34.f[0])
            {
                phi_f20 = sp34.f[0];
            }

            if (phi_f20 < sp34.f[2])
            {
                phi_f20 = sp34.f[2];
            }

            sp40.f[0] += obj->runtime_pos.f[0];
            sp40.f[1] += obj->runtime_pos.f[1];
            sp40.f[2] += obj->runtime_pos.f[2];

            sp34.f[0] += obj->runtime_pos.f[0];
            sp34.f[1] += obj->runtime_pos.f[1];
            sp34.f[2] += obj->runtime_pos.f[2];

            chrpropUpdateRoomList(temp_s1, &sp40, &sp34, phi_f20);
        }
    }

    chrpropRegisterRooms(temp_s1);
}



/**
 * Address 0x7F056EA0.
*/
 ObjectRecord *setupCommandGetObject(s32 stageID, s32 index)
{
     PropDefHeaderRecord *obj;

    obj = setupGetPtrToCommandByIndex(index);

    if (obj != NULL)
    {
        switch (obj->type)
        {
            case PROPDEF_DOOR:    //1
            case PROPDEF_PROP: //3:
            case PROPDEF_KEY:  //4:
            case PROPDEF_ALARM: // 5:
            case PROPDEF_CCTV: //6:
            case PROPDEF_MAGAZINE: //7:
            case PROPDEF_COLLECTABLE: // 8:
            case PROPDEF_MONITOR: //10:
            case PROPDEF_MULTI_MONITOR:// 11:
            case PROPDEF_RACK: //12:
            case PROPDEF_AUTOGUN: //13:
            case PROPDEF_HAT: //17:
            case PROPDEF_AMMO: //20:
            case PROPDEF_ARMOUR: //21:
            case PROPDEF_GAS_RELEASING: //36:
            case PROPDEF_VEHICHLE: //39:
            case PROPDEF_AIRCRAFT: //40:
            case PROPDEF_UNK41: //41:
            case PROPDEF_GLASS: //42:
            case PROPDEF_SAFE: //43:
            case PROPDEF_TANK: //45:
            case PROPDEF_TINTED_GLASS:                 //47:
                return obj;
            break;

            case PROPDEF_DOOR_SCALE: //2
            case PROPDEF_GUARD:           //9 :
            case PROPDEF_LINK:            //14:
            case PROPDEF_GUARD_ATTRIBUTE: //18:
            case PROPDEF_SWITCH:          //19:
            case PROPDEF_TAG:             //22:
            case PROPDEF_OBJECTIVE_START: //23
            case PROPDEF_OBJECTIVE_END:
            case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
            case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
            case PROPDEF_OBJECTIVE_FAIL_CONDITION:
            case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
            case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
            case PROPDEF_OBJECTIVE_PHOTOGRAPH:
            case PROPDEF_OBJECTIVE_NULL:
            case PROPDEF_OBJECTIVE_ENTER_ROOM:
            case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
            case PROPDEF_OBJECTIVE_COPY_ITEM:
            case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT: //35
            case PROPDEF_RENAME:                    //37:
            case PROPDEF_LOCK_DOOR:                 //38:
            case PROPDEF_SAFE_ITEM:                 //44:
            case PROPDEF_CAMERAPOS:                 //46:
                return NULL;
            break;

            // not included:
            //case PROPDEF_DEBRIS: //15:
            //case PROPDEF_UNK16: //16:
            // return ???
        }
    }
    return obj;
}


ObjectRecord *setupFindObjForReuse(s32 wanttype, ObjectRecord **offscreenobjptr, ObjectRecord **anyobjptr, bool musthaveprop, bool musthavemodel, ModelFileHeader *modeldef)
{
    ObjectRecord *offscreenobj = NULL;
    ObjectRecord *anyobj = NULL;

    u32 *cmd = g_CurrentSetup.propDefs;

/* PORT: (u8)word reads the type byte only on big-endian (type is struct offset
 * 3), so on little-endian the terminator and type checks never matched. Same
 * fault as cleanupObjects. */
#ifdef TARGET_N64
#define REUSE_TYPE(c) ((u8)(c)[0])
#else
#define REUSE_TYPE(c) (((PropDefHeaderRecord *)(c))->type)
#endif

    if (cmd)
    {
        while (REUSE_TYPE(cmd) != PROPDEF_END)
        {
            if ((wanttype & 0xff) == REUSE_TYPE(cmd))
            {
                ObjectRecord *obj = (ObjectRecord *)cmd;

                if (obj->prop == NULL)
                {
                    if (!musthaveprop && !musthavemodel)
                    {
                        return obj;
                    }
                }
                else if (wanttype != PROP_TYPE_SMOKE
                        && (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0
                        && (obj->state & PROPSTATE_RESPAWN) == 0
                        && obj->prop->parent == NULL
                        && (!musthavemodel || modelmgrCanSlotFitRwdata(obj->model, modeldef)))
                {
                    if (offscreenobj == NULL && (obj->prop->flags & PROPFLAG_ONSCREEN) == 0)
                    {
                        offscreenobj = obj;
                    }

                    if (anyobj == NULL)
                    {
                        anyobj = obj;
                    }
                }
            }

            cmd = cmd + sizepropdef(cmd);
        }
    }

    *offscreenobjptr = offscreenobj;
    *anyobjptr = anyobj;

    return NULL;
}

