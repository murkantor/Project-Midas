#include <ultra64.h>
#include <memp.h>
#include <macro.h>
#include <stddef.h>
#include "initanitable.h"
#include "othermodemicrocode.h"
#include "ramrom.h"
#include "model.h"
#include "../rmon.h" /*<PR/rmon.h>*/
#include "bondview.h"
#include "chr.h"
#include "chrobjdata.h"
#include "gbi_extension.h"
#include "initunk_005520.h"
#include "math_asinfacosf.h"
#include "math_floor.h"
#include "math_ceil.h"
#include "math_unk_05A9E0.h"
#include "objecthandler.h"
#include "quaternion.h"
#include "random.h"


typedef struct ModelGroupMtxBuildArg {
    u16 flags;
    u16 pad;
    ModelRoData_GroupRecord *group;
    ModelNode *parentnode;
} ModelGroupMtxBuildArg;

// forward declarations
#ifndef TARGET_N64
extern int portPtrLooksBad(const void *);
#endif
void modelSetAnimFrame2WithChrStuff(struct Model *model, f32 framea, f32 frameb, f32 frame2a, f32 frame2b);



//newfile per EU
bool modelmgrCanSlotFitRwdata(Model *modelslot, ModelFileHeader *modeldef)
{
    return modeldef->numRecords <= 0
        || (modelslot->datas != NULL && modelslot->rwdatalen >= modeldef->numRecords);
}


/**
 * Address: 7F06C094
 *
 * Allocates 0x20 bytes for a new model without animations.
 * Models that need animations use modelmgrInstantiateModelWithAnim.
 */
Model *modelmgrInstantiateModel(ModelFileHeader *header)
{
    Model *model;
    u32 *rwdata;
    s16 rwdatalen;

    model = NULL;
    rwdata = NULL;
    rwdatalen = -1;

    if (g_ModelIsLvResetting)
    {
        s32 i;

        for (i = 0; i < (g_MaxModelSlots - 30); i++)
        {
            if (g_ModelSlots[i].unk08 == 0)
            {
                model = (Model *)&g_ModelSlots[i];
                break;
            }
        }

        if (model == NULL)
        {
            model = mempAllocBytesInBank(offsetof(Model, anim), MEMPOOL_STAGE);
        }

        if (header->numRecords > 0)
        {
            rwdata = mempAllocBytesInBank((((header->numRecords * sizeof(void *)) + 0xf) | 0xf) ^ 0xf, MEMPOOL_STAGE);
            rwdatalen = header->numRecords;
        }
    }
    else
    {
        s32 i;

        for (i = 0; i < g_MaxModelSlots; i++)
        {
            if (g_ModelSlots[i].unk08 == 0 && modelmgrCanSlotFitRwdata((Model *)&g_ModelSlots[i], header))
            {
                rwdata = g_ModelSlots[i].unk10;
                rwdatalen = g_ModelSlots[i].unk02;
                model = (Model *)&g_ModelSlots[i];
                break;
            }
        }
    }

    if (model != NULL)
    {
        modelInit(model, header, rwdata);
        ((struct ModelSlot *)model)->unk02 = rwdatalen;
    }

    return model;
}


void clear_model_obj(Model* model)
{
    model->obj = NULL;
}


/**
 * Allocates 0xc0 bytes for a new model to allow enough memory for animations.
 */
Model *modelmgrInstantiateModelWithAnim(ModelFileHeader *modelFileHeader)
{
    Model *newModel;
    void *rwdatas;
    s16 rwdatalen;
    s32 i;
    s16 requiredRwdatalen;
    s32 i2;

    newModel = NULL;
    rwdatas = NULL;
    rwdatalen = -1;

    if (g_ModelIsLvResetting)
    {
        for (i = 0; i < (g_MaxAnimModelSlots - 10); i++)
        {
            if (g_AnimModelSlots[i].unk08 == 0)
            {
                newModel = (Model *)&g_AnimModelSlots[i];
                break;
            }
        }

        if (newModel == NULL)
        {
            newModel = mempAllocBytesInBank(ALIGN16_a(sizeof(Model)), MEMPOOL_STAGE);
        }

        requiredRwdatalen = modelFileHeader->numRecords;

#ifdef DEBUG
        if (modelFileHeader->numRecords > 140) osSyncPrintf("WARNING: increase OISAVESIZE to %d!\n", *(modelFileHeader->numRecords));
#endif

        if (requiredRwdatalen > 0)
        {
            i = requiredRwdatalen;
            rwdatas = mempAllocBytesInBank((((i * sizeof(void *)) + 0xf) | 0xf) ^ 0xf, MEMPOOL_STAGE);
            rwdatalen = modelFileHeader->numRecords;
        }
    }
    else
    {
        requiredRwdatalen = modelFileHeader->numRecords;

        for (i2 = 0; i2 < g_MaxAnimModelSlots; i2++)
        {
            if ((g_AnimModelSlots[i2].unk08 == 0) && ((requiredRwdatalen <= 0) || ((g_AnimModelSlots[i2].unk10 != NULL) &&(g_AnimModelSlots[i2].unk02 >= requiredRwdatalen))))
            {
                newModel = (Model *)&g_AnimModelSlots[i2];
                rwdatas = g_AnimModelSlots[i2].unk10;
                rwdatalen = g_AnimModelSlots[i2].unk02;
                break;
            }
        }
    }

    if (newModel != NULL)
    {
        animInit(newModel, modelFileHeader, rwdatas);
        newModel->rwdatalen = rwdatalen;
    }

    return newModel;
}


void modelAttachHead(Model *model, ModelNode *node,  ModelFileHeader *head)
{
    modelAttachPart(model,model->obj,node,head);
#ifdef DEBUG
    if (model->numRecords > 140 && g_ModelDistanceScale == 0) osSyncPrintf("WARNING: increase OASAVESIZE to %d!\n", *(model + 0x14));
#endif

    modelInitRwData(model,head->RootNode);
}


void clear_aircraft_model_obj(Model *objinstance)
{
    objinstance->obj = NULL;
    return;
}


void modelSetDistanceDisabled(s32 param_1) {
  g_ModelDistanceDisabled = param_1;
}


// PD: modelSetDistanceScale
void modelSetDistanceScale(f32 param_1) {
  g_ModelDistanceScale = param_1;
}


void sub_GAME_7F06C418(Vew4s32 *src, Vew4s32 *dst) {
    s32 i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            dst[i].v[j] = src[i].v[j];
        }
    }
}


void set_vtxallocator(Vertex *(*param_1)(s32)) {
  vtxallocator = param_1;
}


#if defined(LEFTOVERDEBUG)
// called after a debug print during failed model operation possible "exit()" function in debug
void return_null(void)
{
    // dump something 8 bytes long?

    return;
}
#endif


/**
 * Address: 7F06C474
 */
void modelCalculateScaledRootToOriginDir(Model* model, coord3d* coord)
{
    Mtxf* mtx;
    f32 dist;
    f32 neg_x;
    f32 neg_y;
    f32 neg_z;
    f32 inv_dist;

    mtx = getsubmatrix(model);
    neg_x = -mtx->m[3][0];
    neg_y = -mtx->m[3][1];
    neg_z = -mtx->m[3][2];

    dist = sqrtf((neg_x * neg_x) + (neg_y * neg_y) + (neg_z * neg_z));
    if (dist > 0.0f)
    {
        inv_dist = 1.0f / (model->scale * dist);
        coord->f[0] = neg_x * inv_dist;
        coord->f[1] = neg_y * inv_dist;
        coord->f[2] = neg_z * inv_dist;
        return;
    }

    coord->f[0] = 0.0f;
    coord->f[1] = 0.0f;
    coord->f[2] = 1.0f / model->scale;
}


/**
 * Address: 7F06C550
 */
void modelGetScaledRootToOriginDir(Model* model, coord3d* coord)
{
  modelCalculateScaledRootToOriginDir(model, coord);
}


// PD: model0001a524
s32 modelFindNodeMtxIndex(ModelNode *node, s32 arg1)
{
    s32 index;
    union ModelRoData *rodata1;
    union ModelRoData *rodata2;
    union ModelRoData *rodata3;
    union ModelRoData *rodata4;

    while (node)
    {
#ifndef TARGET_N64
        /* PORT: a hat attachment once led to a garbage node in multiplayer; report it
         * and return -1 (the caller then returns a NULL matrix) instead of crashing. */
        {
            extern int portPtrLooksBad(const void *);
            extern void portModelAuditReport(const char *, const void *, const void *, const void *);
            if (portPtrLooksBad(node)) {
                portModelAuditReport("modelFindNodeMtxIndex:node", node, NULL, NULL);
                return -1;
            }
        }
#endif
        switch (node->Opcode & 0xff)
        {
            case MODELNODE_OPCODE_HEADER:
                rodata1 = node->Data;
                return rodata1->Header.MatrixIndex;

            case MODELNODE_OPCODE_GROUP:
                rodata2 = node->Data;
                return rodata2->Group.MatrixIDs[arg1 == 0x200 ? 2 : (arg1 == 0x100 ? 1 : 0)];

            case MODELNODE_OPCODE_OP03:
                rodata3 = node->Data;
                return rodata3->Group.MatrixIDs[arg1 == 0x200 ? 2 : (arg1 == 0x100 ? 1 : 0)];

            case MODELNODE_OPCODE_GROUPSIMPLE:
                rodata4 = node->Data;
                return rodata4->GroupSimple.Group1;
                break;
        }

        node = node->Parent;
    }

    return -1;
}


// PD: model0001a5cc
Mtxf *modelFindNodeMtx(struct Model *model, struct ModelNode *node, s32 arg2) {
    s32 index = modelFindNodeMtxIndex(node, arg2);

    if (index >= 0) {
        return &model->render_pos[index].pos;
    }

    return NULL;
}


//rejoined per EU
// PD: model0001a60c
Mtxf *getsubmatrix(Model *objinst)
{
    #if defined(LEFTOVERDEBUG)
    if (!objinst)
    {
        osSyncPrintf("getsubmatrix: no objinst!\n");
        return_null();
    }
    if (!objinst->obj)
    {
        osSyncPrintf("getsubmatrix: objinst has no object!\n");
        return_null();
    }
    #endif
    return modelFindNodeMtx(objinst, objinst->obj->RootNode, 0);
}


// unreferenced
void sub_GAME_7F06C710(Model* model, coord3d* pos)
{
    Mtxf* mtx;

    mtx = getsubmatrix(model);
    if (mtx != NULL)
    {
        pos->f[0] = (f32) mtx->m[3][0];
        pos->f[1] = (f32) mtx->m[3][1];
        pos->f[2] = (f32) mtx->m[3][2];
        return;
    }

    pos->f[0] = 0.0f;
    pos->f[1] = 0.0f;
    pos->f[2] = 0.0f;
}


f32 sub_GAME_7F06C768(Model *objinst)
{
    Mtxf *mtx = getsubmatrix(objinst);
    if (mtx != 0)
    {
        return -mtx->m[3][2];
    }
    return 0.0f;
}


/**
 * Address 0x7F06C79C.
*/
union ModelRwData* modelGetNodeRwData(Model *Objinst, ModelNode *root)
{
    s32 index  = 0;
    union ModelRwData **data = Objinst->datas;

    switch (root->Opcode & 0xff)
    {
        case MODELNODE_OPCODE_HEADER:
        {
            index = root->Data->Header.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_DLCOLLISION:
        {
            index = root->Data->DisplayListCollisions.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_OP07:
        {
            index = root->Data->Op07.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_LOD:
        {
            index = root->Data->LOD.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_SWITCH:
        {
            index = root->Data->Switch.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_BSP:
        {
            index = root->Data->BSP.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_OP11:
        {
            index = root->Data->Op11.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_GUNFIRE:
        {
            index = root->Data->Gunfire.RwDataIndex;
            break;
        }
        case MODELNODE_OPCODE_HEAD:
        {
            index = root->Data->HeadPlaceholder.RwDataIndex;
            break;
        }
    }

    while (root->Parent)
    {
        root = root->Parent;
        if ((root->Opcode & 0xFF) == MODELNODE_OPCODE_HEAD)
        {
            ModelRwData_HeadPlaceholderRecord *tmp = modelGetNodeRwData(Objinst, root);
            data = tmp->RwDatas;
            break;
        }
    }

    return &data[index];
}



void getpartoffset(Model *objinst, ModelNode *part, coord3d *offset) //#MATCH - however OPCODE 3 needs defining
{
    #if defined(LEFTOVERDEBUG)
    if (!objinst)
    {
        osSyncPrintf("getpartoffset: no objinst!");
        return_null();
    }
    if (!part)
    {
        osSyncPrintf("getpartoffset: no partdesc!");
        return_null();
    }
    #endif
    switch (part->Opcode & 0xFF)
    {
        case MODELNODE_OPCODE_HEADER:
        {
            struct modeldata_root *root = modelGetNodeRwData(objinst, part);
            offset->x                   = root->pos.x;
            offset->y                   = root->pos.y;
            offset->z                   = root->pos.z;
            break;
        }
        case MODELNODE_OPCODE_GROUP:
        {
            ModelRoData_GroupRecord *prt = &part->Data->Group;
            offset->x                  = prt->Origin.x;
            offset->y                  = prt->Origin.y;
            offset->z                  = prt->Origin.z;
            break;
        }
        case MODELNODE_OPCODE_OP03:
        {
            ModelRoData_GroupSimpleRecord *prt = &part->Data->GroupSimple; //UNUSED at this time
            offset->x                        = prt->Origin.x;
            offset->y                        = prt->Origin.y;
            offset->z                        = prt->Origin.z;
            break;
        }
        case MODELNODE_OPCODE_GROUPSIMPLE:
        {
            ModelRoData_GroupSimpleRecord *prt = &part->Data->GroupSimple;
            offset->x                        = prt->Origin.x;
            offset->y                        = prt->Origin.y;
            offset->z                        = prt->Origin.z;
            break;
        }
        default:
        {
            offset->x = 0.0f;
            offset->y = 0.0f;
            offset->z = 0.0f;
            break;
        }
    }
}


void setpartoffset(Model *model, ModelNode *node, coord3d *pos)
{
#if defined(LEFTOVERDEBUG)
    if (!model) {
        osSyncPrintf("setpartoffset: no objinst!");
        return_null();
    }

    if (!node) {
        osSyncPrintf("setpartoffset: no partdesc!");
        return_null();
    }
    else
    {
        // huh?
    }
#endif
    switch (node->Opcode & 0xff)
    {
        case MODELNODE_OPCODE_HEADER:
            {
                ModelRwData_HeaderRecord *rwdata = modelGetNodeRwData(model, node);
                coord3d diff[1];

                diff[0].x = pos->x - rwdata->pos.x;
                diff[0].z = pos->z - rwdata->pos.z;

                rwdata->pos.x = pos->x;
                rwdata->pos.y = pos->y;
                rwdata->pos.z = pos->z;

                rwdata->unk24.x += diff[0].x; rwdata->unk24.z += diff[0].z;
                rwdata->unk34.x += diff[0].x; rwdata->unk34.z += diff[0].z;
                rwdata->unk40.x += diff[0].x; rwdata->unk40.z += diff[0].z;
                rwdata->unk4c.x += diff[0].x; rwdata->unk4c.z += diff[0].z;
            }
            break;
        case MODELNODE_OPCODE_GROUP:
            {
                ModelRoData_GroupRecord *rodata = &node->Data->Group;
                rodata->Origin.x = pos->x;
                rodata->Origin.y = pos->y;
                rodata->Origin.z = pos->z;
            }
            break;
        case MODELNODE_OPCODE_OP03:
            {
                ModelRoData_GroupRecord *rodata = &node->Data->Group;
                rodata->Origin.x = pos->x;
                rodata->Origin.y = pos->y;
                rodata->Origin.z = pos->z;
            }
            break;
        case MODELNODE_OPCODE_GROUPSIMPLE:
            {
                ModelRoData_GroupSimpleRecord *rodata = &node->Data->GroupSimple;
                rodata->Origin.x = pos->x;
                rodata->Origin.y = pos->y;
                rodata->Origin.z = pos->z;
            }
            break;
    }
}


void getsuboffset(Model *objinst, coord3d *offset) //#MATCH
{
    #if defined(LEFTOVERDEBUG )
    if (!objinst)
    {
        osSyncPrintf("getsuboffset: no objinst!");
        return_null();
    }

    if (!objinst->obj)
    {
        osSyncPrintf("getsuboffset: objinst has no object!");
        return_null();
    }
    #endif
    getpartoffset(objinst, objinst->obj->RootNode, offset);
}




void setsuboffset(Model *objinst, coord3d *offset) //#MATCH
{
    #if defined(LEFTOVERDEBUG )
    if (!objinst)
    {
        osSyncPrintf("setsuboffset: no objinst!");
        return_null();
    }
    if (!objinst->obj)
    {
        osSyncPrintf("setsuboffset: objinst has no object!");
        return_null();
    }
    #endif
    setpartoffset(objinst, objinst->obj->RootNode, offset);
}





/**
 * Address 0x7F06CC80.
 */
f32 getsubroty(Model *objinst)
{
    ModelNode *root;

    #if defined(LEFTOVERDEBUG)
    if(0)
    {
        // removed
    }

    if (objinst == NULL)
    {
        osSyncPrintf("getsubroty: no objinst!");
        return_null();
    }

    if(0)
    {
        // removed
    }

    if (objinst->obj == NULL)
    {
        osSyncPrintf("getsubroty: objinst has no object!");
        return_null();
    }

    if(0)
    {
        // removed
    }

    if (objinst->obj->RootNode == NULL)
    {
        osSyncPrintf("getsubroty: objinst has no root part!");
        return_null();
    }

    if(0)
    {
        // removed
    }
    #endif

    root = objinst->obj->RootNode;
    if ((root->Opcode & 0xFF) == MODELNODE_OPCODE_HEADER)
    {
        return ((struct modeldata_root *)modelGetNodeRwData(objinst, root))->subroty;
    }

    return 0.0f;
}


void setsubroty(Model *model, f32 angle)
{
    ModelNode* node;
#if defined(LEFTOVERDEBUG)
    if (!model)
    {
        osSyncPrintf("setsubroty: no objinst!");
        return_null();
    }

    if (!model->obj) //< needs to be v1 not a1
    {
        osSyncPrintf("setsubroty: objinst has no object!");
        return_null();
    }

    if (!model->obj->RootNode)
    {
        osSyncPrintf("setsubroty: objinst has no root part!");
        return_null();
    }
#endif
    node = model->obj->RootNode;
    if ((node->Opcode & 0xff) == MODELNODE_OPCODE_HEADER)
    {
        ModelRwData_HeaderRecord *rwdata = modelGetNodeRwData(model, node);
        f32 diff = angle - rwdata->unk14;

        if (diff < 0) { diff += M_TAU_F; }

        rwdata->unk30 += diff;

        if (rwdata->unk30 >= M_TAU_F) { rwdata->unk30 -= M_TAU_F; }

        rwdata->unk20 += diff;

        if (rwdata->unk20 >= M_TAU_F) { rwdata->unk20 -= M_TAU_F; }

        rwdata->unk14 = angle;
    }
}


void modelSetScale(Model *objinst, f32 scale)
{
    objinst->scale = scale;
}


/**
 * Address: 7F06CE84
 *
 * Scales only the translation component of the root node of an animation.
 * For example, the animation for the plane flight in Runway's outro doesn't
 * actually move the plane very far. This function is used to scale up the translation
 * ~10x to allow it to fly near the camera.
 */
void modelSetAnimTranslationScale(Model* model, f32 scale)
{
    model->anim_translation_scale = scale;
}


f32 getjointsize(Model *model, ModelNode *node)
{
    Model     *temp_a2;
    ModelNode *temp_a1;
    s32        temp_t7;

#if defined(LEFTOVERDEBUG)
    if (!model)
    {
        osSyncPrintf("getjointsize: no objinst!\n");
        return_null();
    }
#endif

    if (node)
    {
        do
        {
            switch (node->Opcode & 0xFF)
            {
                case MODELNODE_OPCODE_HEADER:
                {
                    ModelRoData_HeaderRecord *rodata = &node->Data->Header;
                    return rodata->GroupsAsF32 * model->scale;
                }
                case MODELNODE_OPCODE_GROUP:
                {
                    ModelRoData_GroupRecord *rodata = &node->Data->Group;
                    return rodata->BoundingVolumeRadius * model->scale;
                }
                case MODELNODE_OPCODE_OP03:
                {
                    ModelRoData_GroupRecord *rodata = &node->Data->Group;
                    return rodata->BoundingVolumeRadius * model->scale;
                }
                case MODELNODE_OPCODE_GROUPSIMPLE:
                {
                    ModelRoData_GroupSimpleRecord *rodata = &node->Data->GroupSimple;
                    return rodata->BoundingVolumeRadius * model->scale;
                }
                case MODELNODE_OPCODE_OP11:
                {
                    ModelRoData_Op11Record *rodata = &node->Data->Op11;
                    return rodata->BoundingVolumeRadius * model->scale;
                }
                case MODELNODE_OPCODE_GUNFIRE:
                {
                    ModelRoData_GunfireRecord *rodata = &node->Data->Gunfire;
                    return rodata->Scale * model->scale;
                }
                case MODELNODE_OPCODE_SHADOW:
                {
                    ModelRoData_ShadowRecord *rodata = &node->Data->Shadow;
                    return rodata->Scale * model->scale;
                }
                case MODELNODE_OPCODE_OP14:
                {
                    ModelRoData_Op14Record *rodata = &node->Data->Op14;
                    return rodata->Scale * model->scale;
                }
                case MODELNODE_OPCODE_INTERLINK:
                {
                    ModelRoData_InterlinkageRecord *rodata = &node->Data->Interlinkage;
                    return rodata->Scale * model->scale;
                }
                case MODELNODE_OPCODE_OP16:
                {
                    ModelNode_Op16Record *rodata = &node->Data->Op16;
                    return rodata->Scale * model->scale;
                }
                default:
                    node = node->Parent;
            }
        } while (node);
    }

    return 0.0f;
}


/**
 * Address 0x7F06D00C.
 * PD: model0001af80
*/
f32 getinstsize(Model *arg0)
{
    #if defined(LEFTOVERDEBUG)
    if (arg0 == NULL)
    {
        osSyncPrintf("getinstsize: no objinst!\n");
        return_null();
    }

    if (arg0->obj == NULL)
    {
        osSyncPrintf("getinstsize: no objdesc!\n");
        return_null();
    }
    #endif

    return arg0->obj->BoundingVolumeRadius * arg0->scale;
}



// PD: model0001af98
void interpolate3dVectors(vec3d *v, vec3d *w, float frac)
{
    v->x += (w->x - v->x) * frac;
    v->y += (w->y - v->y) * frac;
    v->z += (w->z - v->z) * frac;
  return;
}


// PD: model0001afe8
f32 sub_GAME_7F06D0CC(f32 arg0, f32 angle, f32 mult)
{
    f32 value = angle - arg0;

    if (angle < arg0)
    {
        value += M_TAU_F;
    }

    if (value < M_PI_F)
    {
        arg0 += value * mult;

        if (arg0 >= M_TAU_F)
        {
            arg0 -= M_TAU_F;
        }
    }
    else
    {
        arg0 -= (M_TAU_F - value) * mult;

        if (arg0 < 0)
        {
            arg0 += M_TAU_F;
        }
    }

    return arg0;
}


// PD: model0001b07c
void sub_GAME_7F06D160(coord3d *arg0, coord3d *arg1, f32 mult)
{
    arg0->x = sub_GAME_7F06D0CC(arg0->x, arg1->x, mult);
    arg0->y = sub_GAME_7F06D0CC(arg0->y, arg1->y, mult);
    arg0->z = sub_GAME_7F06D0CC(arg0->z, arg1->z, mult);
}


/**
 * Address: 7F06D1CC
 */
u16 modelAnimReadRootMotionValue(ModelAnimation *anim, s32 fieldIndex, s32 extraBitOffset)
{
    anim = portAnimationResolve(anim);
    u32 result;
    u32 new_var2;
    u32 oldResult;
    struct ModelAnimBitField *desc;
    u8 *byteptr;
    u32 totalBitOffset;
    u32 byteIndex;
    u32 mask;
    u8 bitsRemaining;
    u8 bitsThisRead;

    result = 0;
    desc = anim->bitDescriptors + fieldIndex;
    bitsRemaining = desc->bitCount;

    if (bitsRemaining > 0)
    {
        totalBitOffset = extraBitOffset + desc->bitOffset;
        byteIndex = totalBitOffset >> 3;
        totalBitOffset &= 7;
        byteptr = anim->bitStream + byteIndex;
        bitsThisRead = 8 - totalBitOffset;

        if (bitsRemaining >= bitsThisRead)
        {
            do
            {
                mask = (1 << bitsThisRead) - 1;
                bitsRemaining -= bitsThisRead;
                result |= ((*byteptr) & mask) << bitsRemaining;
                result &= 0xffff;
                byteptr++;
                bitsThisRead = 8;
            }
            while (bitsRemaining >= 8);
        }

        if (bitsRemaining > 0)
        {
            result |= ((*byteptr) >> (bitsThisRead - bitsRemaining)) & ((1 << bitsRemaining) - 1);
            result &= 0xffff;
        }

        bitsRemaining = desc->bitCount;

        if (bitsRemaining < 16)
        {
            oldResult = result;
            mask = 1 << (bitsRemaining - 1);

            if (result & mask)
            {
                result = ((new_var2 = oldResult) | (((1 << (16 - bitsRemaining)) - 1) << bitsRemaining)) & 0xffff;
            }
        }
    }

    result = desc->valueOffset + ((0, result));
    return result;
}


/**
 * Address: 7F06D2E4
 */
u16 sub_GAME_7F06D2E4(s32 jointnum, s32 flip, ModelSkeleton *skeleton, ModelAnimation *anim, s32 frame, coord16 *out)
{
    anim = portAnimationResolve(anim);
    u32 scaled;
    s32 base;
    u32 angle_raw;
    u16 angle_ret;

    scaled = ((u32) anim->unk0C) * ((u32) frame);

    if (flip)
    {
        base = skeleton->Joints[jointnum].mtxB;
    }
    else
    {
        base = skeleton->Joints[jointnum].mtxA;
    }

    out->x = modelAnimReadRootMotionValue(anim, base, scaled);
    out->y = modelAnimReadRootMotionValue(anim, base + 1, scaled);
    out->z = modelAnimReadRootMotionValue(anim, base + 2, scaled);
    angle_raw = modelAnimReadRootMotionValue(anim, base + 3, scaled);
    angle_ret = angle_raw;

    if (flip)
    {
        out->x = -out->x;

        if (angle_raw != 0)
        {
            angle_ret = 0x10000 - (angle_raw & 0xFFFFFFFFu);
        }
    }

    return angle_ret;
}


f32 sub_GAME_7F06D3F4(s32 jointnum, s32 flip, ModelSkeleton *skeleton, ModelAnimation *anim, s32 frame, coord3d *pos)
{
    anim = portAnimationResolve(anim);
    s16 tmp[3];
    u16 angle;

    angle = sub_GAME_7F06D2E4(jointnum, flip, skeleton, anim, frame, tmp);

    pos->x = (f32)tmp[0];
    pos->y = (f32)tmp[1];
    pos->z = (f32)tmp[2];

    return ((f32)angle * M_TAU_F) / M_U16_MAX_VALUE_F;
}


/**
 * Address: 7F06D490
 */
void sub_GAME_7F06D490(Model *model, ModelNode *modelNode)
{
    union ModelRwData *rw;
    coord3d sp38;
    coord3d sp2c;
    f32 y;

    rw = modelGetNodeRwData(model, modelNode);

    if (rw->Header.unk00 != 0)
    {
        return;
    }

    sp38.x = rw->Header.unk34.x;
    sp38.y = rw->Header.unk34.y;
    sp38.z = rw->Header.unk34.z;
    rw->Header.unk14 = rw->Header.unk30;

    if (model->unk2c != 0.0f)
    {
        if (rw->Header.unk01 != 0)
        {
            interpolate3dVectors(&sp38, &rw->Header.unk24, model->unk2c);

            // Weird do while loop but needed for matching.
            do
            {
                rw->Header.unk14 = sub_GAME_7F06D0CC(rw->Header.unk30, rw->Header.unk20, model->unk2c);
            }
            while (model->unka0 * 0);
        }
    }

    if ((model->anim2 != 0) || (model->unk84 != 0.0f))
    {
        if (rw->Header.unk02 != 0)
        {
            y = rw->Header.unk4c.y;

            if (model->unk5c != 0.0f)
            {
                y += (rw->Header.unk40.y - y) * model->unk5c;
            }

            sp38.y += (y - sp38.y) * model->unk84;
        }
    }

    sp2c.x = sp38.x;
    sp2c.y = sp38.y;
    sp2c.z = sp38.z;

    if (model->unka0 && !((s32 (*)(Model *, coord3d *, coord3d *, f32 *)) model->unka0)(model, &rw->Header.pos, &sp2c, &rw->Header.ground))
    {
        return;
    }

    sp38.x = sp2c.x - sp38.x;
    sp38.z = sp2c.z - sp38.z;

    rw->Header.pos.x = sp2c.x;
    rw->Header.pos.y = ((f32) sp2c.y) + rw->Header.ground;
    rw->Header.pos.z = sp2c.z;

    rw->Header.unk34.x += sp38.x;
    rw->Header.unk34.z += sp38.z;

    if (rw->Header.unk01 != 0)
    {
        rw->Header.unk24.x += sp38.x;
        rw->Header.unk24.z += sp38.z;
    }

    if (rw->Header.unk02 != 0)
    {
        rw->Header.unk4c.x += sp38.x;
        rw->Header.unk4c.z += sp38.z;
        rw->Header.unk40.x += sp38.x;
        rw->Header.unk40.z += sp38.z;
    }
}


void subcalcpos(Model *arg0)
{
    struct ModelNode *root;

#if defined(LEFTOVERDEBUG)
    if (arg0 == NULL)
    {
        osSyncPrintf("subcalcpos: no objanim!\n");
        return_null();
    }

    if (arg0->obj == 0)
    {
        osSyncPrintf("subcalcpos: no objdesc!\n");
        return_null();
    }
#endif

    root = arg0->obj->RootNode;
    if ((root != NULL) && ((root->Opcode & 0xFF) == 1))
    {
        sub_GAME_7F06D490(arg0, root);
    }
}

void process_01_group_heading(ModelRenderData* renderdata, Model* model, ModelNode* node)
{
    union ModelRoData* rodata;
    union ModelRwData* rwdata;
    f32 scale;
    f32* pos;
    f32 unk14;
    Mtxf* var_a3;
    s32 modeltype;
    RenderPosView* renderpos;
    Mtxf sp20;

    rodata = node->Data;
    rwdata = modelGetNodeRwData(model, node);

    scale = model->scale;
    pos = &rwdata->Header.pos.x;
    unk14 = rwdata->Header.unk14;
    modeltype = rodata->Header.MatrixIndex;
    renderpos = &model->render_pos[modeltype];

    if (node->Parent != NULL)
    {
        var_a3 = modelFindNodeMtx(model, node->Parent, 0);
    }
    else
    {
        var_a3 = renderdata->basemtx;
    }

    if (rwdata->Header.unk18 != 0.0f)
    {
        unk14 = sub_GAME_7F06D0CC(unk14, rwdata->Header.unk1c, rwdata->Header.unk18);
    }

    if (var_a3 != NULL)
    {
        matrix_4x4_set_position_and_rotation_around_y(pos, unk14, &sp20);

        if (scale != 1.0f)
        {
            matrix_scalar_multiply_2(scale, sp20.m[0]);
        }

        matrix_4x4_multiply_homogeneous(var_a3, &sp20, &renderpos->pos);
        return;
    }

    matrix_4x4_set_position_and_rotation_around_y(pos, unk14, &renderpos->pos);

    if (scale != 1.0f)
    {
        matrix_scalar_multiply_2(scale, renderpos->pos.m[0]);
    }
}


/**
 * Address: 7F06D8B0
 */
void modelBuildGroupMatrices(Mtxf **parentMtx, Model *model, ModelGroupMtxBuildArg *mgm, coord3d *rot)
{
    u32 flags;
    ModelRoData_GroupRecord *group;
    Mtxf *parent;
    Mtxf *matrix0_mtx;
    Mtxf tmp;
    s32 matrix0;
    s32 matrix1;
    s32 matrix2;
    RenderPosView *render_pos;
    Mtxf *parentNodeMtx;
    f32 *origin;
    s32 has_matrix2;
    quatf q;
    quatf q2;
    Mtxf *dst;
    f32 angle;
    Mtxf **parentMtxPtr;

    flags = mgm->flags;
    group = mgm->group;
    matrix0 = group->MatrixID0;
    matrix1 = group->MatrixID1;
    parentMtxPtr = parentMtx;
    origin = group->Origin.f;
    matrix2 = group->MatrixID2;
    render_pos = model->render_pos;

    if (((Mtxf *) mgm->parentnode) != NULL)
    {
        parentNodeMtx = modelFindNodeMtx(model, (ModelNode *) ((Mtxf *) mgm->parentnode), 0);
        parent = parentNodeMtx;
    }
    else
    {
        parent = parentMtxPtr[0];
    }

    has_matrix2 = flags & MODELGROUP_MTX_HAS_MATRIX2;
    matrix0_mtx = (Mtxf *) mgm->parentnode;

    if (parent != NULL)
    {
        matrix_4x4_set_position_and_rotation_around_xyz(&group->Origin, rot, &tmp);

        matrix0_mtx = &render_pos[matrix0].pos;
        matrix_4x4_multiply_homogeneous(parent, &tmp, matrix0_mtx);

        if (g_ModelJointPositionedFunc != NULL)
        {
            g_ModelJointPositionedFunc(matrix0, matrix0_mtx);
        }
    }
    else
    {
        matrix_4x4_set_position_and_rotation_around_xyz(&group->Origin, rot, &render_pos[matrix0].pos);
    }

    if (flags & MODELGROUP_MTX_HAS_MATRIX1)
    {
        quaternion_set_rotation_around_xyzf(rot->f, q);
        quaternion_7F05BC68(q, 0.5f, q2);

        if (parent != NULL)
        {
            quaternion_to_transform_matrix(origin, q2, tmp.m);
            matrix_4x4_multiply_homogeneous(parent, &tmp, &render_pos[matrix1].pos);
        }
        else
        {
            quaternion_to_transform_matrix(origin, q2, (render_pos + matrix1)->pos.m);
        }
    }

    if (has_matrix2)
    {
        if (parent != NULL)
        {
            dst = &tmp;
        }
        else
        {
            dst = &render_pos[matrix2].pos;
        }

        angle = rot->y;

        if (angle < M_PI_F)
        {
            angle = angle * 0.5f;
        }
        else
        {
            angle = M_TAU_F - ((M_TAU_F - angle) * 0.5f);
        }

        matrix_4x4_set_rotation_around_y(angle, dst);

        if (angle >= M_PI_F)
        {
            angle = M_TAU_F - angle;
        }
        if (angle < 0.890118f)
        {
            angle = modelGetBendStretchScale(angle);
        }
        else
        {
            angle = 1.5f;
        }

        matrix_column_3_scalar_multiply_2(angle, (f32 *) dst);
        matrix_4x4_set_position(&group->Origin, dst);

        if (parent != NULL)
        {
            matrix_4x4_multiply_homogeneous(parent, dst, &render_pos[matrix2].pos);
        }
    }
}


void sub_GAME_7F06DB5C(ModelRenderData *arg0, Model *arg1, ModelNode *arg2, quatf arg3)
{
    s32 spA4;
    ModelRoData_GroupRecord *spA0;
    Mtxf *sp9C;
    s32 _gap98;
    Mtxf sp58;
    s32 sp54;
    s32 sp50;
    s32 sp4C;
    RenderPosView *sp48;
    s32 sp44;
    intptr_t *new_var;
    s32 sp40;
    quatf sp2C;
    Mtxf *sp28;
    f32 sp24;
    s32 _gap20;
    intptr_t sp1C;

    spA4 = arg2->Opcode;
    spA0 = (ModelRoData_GroupRecord *)arg2->Data;
    sp54 = spA0->MatrixID0;
    sp50 = spA0->MatrixID1;
    sp4C = spA0->MatrixID2;
    new_var = &sp1C;
    sp48 = arg1->render_pos;
    sp1C = (intptr_t)arg2->Parent;

    if (*new_var != 0) {
        sp9C = arg0->basemtx;
        sp9C = modelFindNodeMtx(arg1, (ModelNode *)sp1C, 0);
    } else {
        sp9C = arg0->basemtx;
    }

    if (sp9C != 0) {
        quaternion_to_transform_matrix(&spA0->Origin, arg3, &sp58);
        sp1C = (intptr_t)&sp48[sp54];
        matrix_4x4_multiply_homogeneous(sp9C, &sp58, (Mtxf *)sp1C);
        if (g_ModelJointPositionedFunc != NULL) {
            g_ModelJointPositionedFunc(sp54, (Mtxf *)sp1C);
        }
    } else {
        quaternion_to_transform_matrix(&spA0->Origin, arg3, (Mtxf *)&sp48[sp54]);
    }

    if (spA4 & 0x100) {
        quaternion_7F05BC68(arg3, 0.5f, sp2C);
        if (sp9C != 0) {
            quaternion_to_transform_matrix(&spA0->Origin, sp2C, &sp58);
            matrix_4x4_multiply_homogeneous(sp9C, &sp58, (Mtxf *)&sp48[sp50]);
        } else {
            quaternion_to_transform_matrix(&spA0->Origin, sp2C, (Mtxf *)&sp48[sp50]);
        }
    }

    if (spA4 & 0x200) {
        if (sp9C != 0) {
            sp28 = &sp58;
        } else {
            sp28 = (Mtxf *)&sp48[sp4C];
        }
        sp24 = 2.0f * acosf(*arg3);
        if (sp24 < 3.1415927f) {
            sp24 = sp24 * 0.5f;
        } else {
            sp24 = 6.2831855f - ((6.2831855f - sp24) * 0.5f);
        }
        matrix_4x4_set_rotation_around_y(sp24, sp28);
        if (sp24 >= 3.1415927f) {
            sp24 = 6.2831855f - sp24;
        }
        if (sp24 < 0.890118f) {
            sp24 = modelGetBendStretchScale(sp24);
        } else {
            sp24 = 1.5f;
        }
        matrix_column_3_scalar_multiply_2(sp24, (f32 *)sp28);
        matrix_4x4_set_position(&spA0->Origin, sp28);
        if (sp9C != 0) {
            matrix_4x4_multiply_homogeneous(sp9C, sp28, (Mtxf *)&sp48[sp4C]);
        }
    }
}


/**
 * Address: 7F06DE04
 */
u32 modelAnimReadBitsAsU16Angle(u8 *bitstream, u8 width, u32 bitOffset)
{
    u32 value = 0;
    u32 mask;
    u8 numbitsthisbyte;
    u8 remainingbits;

    remainingbits = width;
    value *= bitOffset / 8;

    if(1);

    remainingbits = width;
    bitstream += bitOffset / 8;
    bitOffset %= 8;
    numbitsthisbyte = 8 - bitOffset;

    while (remainingbits >= numbitsthisbyte)
    {
        remainingbits -= numbitsthisbyte;
        mask = (1 << numbitsthisbyte) - 1;
        value |= ((u16)((*bitstream) & mask)) << remainingbits;
        value &= 0xffff;
        bitstream++;
        numbitsthisbyte = 8;
    }

    if (remainingbits > 0)
    {
        mask = (1 << remainingbits) - 1;
        value |= ((*bitstream) >> (numbitsthisbyte - remainingbits)) & mask;
        value &= 0xffff;
    }

    value <<= 16 - width;

    return value & 0xffff;
}


/**
 * Address: 7F06DEC0
 */
void sub_GAME_7F06DEC0(s32 jointnum, s32 flip, ModelSkeleton *skeleton, ModelAnimation *anim, u8 *bitstream, coord3d *rot)
{
    anim = portAnimationResolve(anim);
    u32 bitoffset;
    u8 width;
    u16 rotation[3];

#ifndef TARGET_N64
    /* A chr mid-setup can tick with no animation bound, where the N64 read junk through NULL;
     * keeping the caller's default rotation is the safe equivalent. */
    if (anim == NULL || bitstream == NULL)
    {
        return;
    }
#endif

    width = anim->unk06;

    // Mirrored joint rotation?
    if (flip)
    {
        bitoffset = skeleton->Joints[jointnum].mtxB * width;
    }
    else
    {
        bitoffset = skeleton->Joints[jointnum].mtxA * width;
    }

    width = anim->unk06;

    rotation[0] = modelAnimReadBitsAsU16Angle(bitstream, width, bitoffset);
    bitoffset += (unsigned long) width;

    rotation[1] = modelAnimReadBitsAsU16Angle(bitstream, width, bitoffset);
    bitoffset += width;

    rotation[2] = modelAnimReadBitsAsU16Angle(bitstream, width, bitoffset);

    rot->x = (rotation[0] * M_TAU_F) / M_U16_MAX_VALUE_F;

    if (flip)
    {
        if (rotation[1] != 0)
        {
            rot->y = ((0x10000 - rotation[1]) * M_TAU_F) / M_U16_MAX_VALUE_F;
        }
        else
        {
            rot->y = 0.0f;
        }

        if (rotation[2] != 0)
        {
            rot->z = ((0x10000 - rotation[2]) * M_TAU_F) / M_U16_MAX_VALUE_F;
        }
        else
        {
            rot->z = 0.0f;
        }
    }
    else
    {
        rot->y = (rotation[1] * M_TAU_F) / M_U16_MAX_VALUE_F;
        rot->z = (rotation[2] * M_TAU_F) / M_U16_MAX_VALUE_F;
    }
}


void process_02_position(ModelRenderData *arg0, Model *model, ModelNode *node)
{
    union
    {
        s32 v;
        long long int force_structure_alignment;
    } jointnum;

    ModelSkeleton *skeleton;
    coord3d rot1;
    coord3d rot2;
    coord3d rot3;
    quatf q1;
    quatf q2;
    quatf result;
    coord3d rot4;
    ModelRoData_GroupRecord *group;

    group = &node->Data->Group;
    jointnum.v = group->JointID;
    skeleton = model->obj->Skeleton;

    rot1 = D_80036094;

    sub_GAME_7F06DEC0(jointnum.v, model->gunhand, skeleton, model->anim, model->unk34, &rot1);

    if (model->unk2c != 0.0f)
    {
        rot2 = D_800360A0;
        sub_GAME_7F06DEC0(jointnum.v, model->gunhand, skeleton, model->anim, model->unk38, &rot2);
        sub_GAME_7F06D160(&rot1, &rot2, model->unk2c);
    }

    if (model->unk84 != 0.0f)
    {
        rot3 = D_800360AC;
        sub_GAME_7F06DEC0(jointnum.v, model->unk25, skeleton, model->anim2, model->unk64, &rot3);

        if (model->unk5c != 0.0f)
        {
            rot4 = D_800360B8;
            sub_GAME_7F06DEC0(jointnum.v, model->unk25, skeleton, model->anim2, model->unk68, &rot4);
            sub_GAME_7F06D160(&rot3, &rot4, model->unk5c);
        }

        quaternion_set_rotation_around_xyzf(&rot1, q1);
        quaternion_set_rotation_around_xyzf(&rot3, q2);
        quaternion_ensure_shortest_path(q1, q2);
        quaternion_slerp(q1, q2, model->unk84, result);
        sub_GAME_7F06DB5C(arg0, model, node, result);
    }
    else
    {
        modelBuildGroupMatrices(arg0, model, node, &rot1);
    }
}


/**
 * Address: 7F06E2B8
 */
void sub_GAME_7F06E2B8(ModelRenderData *renderData, Model *model, ModelNode *node, f32 angle)
{
    s32 opcode;
    union ModelRoData *data;
    Mtxf *mtx;
    RenderPosView *render_pos;
    Mtxf localMtx;
    s32 m0;
    s32 m1;
    s32 m2;
    RenderPosView *renderPosBase;
    f32 *origin;
    s32 matrix2Flag;
    Mtxf *localMtxPtr;
    Mtxf *matrixPtr;
    f32 scalar;

    opcode = node->Opcode;
    data = node->Data;
    m0 = data->Group.MatrixID0;
    m1 = data->Group.MatrixID1;
    m2 = data->Group.MatrixID2;
    renderPosBase = model->render_pos;
    render_pos = renderPosBase;

    if (node->Parent != NULL)
    {
        mtx = modelFindNodeMtx(model, ((0, node))->Parent, 0);
    }
    else
    {
        mtx = renderData->basemtx;
    }

    localMtxPtr = &localMtx;

    if (mtx != NULL)
    {
        matrix_4x4_set_position_and_rotation_around_y((f32 *) &data->Group.Origin, angle, localMtxPtr);
        matrix_4x4_multiply_homogeneous(mtx, &localMtx, &render_pos[m0].pos);
    }
    else
    {
        origin = (f32 *) &data->Group.Origin;
        matrix_4x4_set_position_and_rotation_around_y(origin, angle, &render_pos[m0].pos);
    }

    matrix2Flag = 0x200;
    m0 = opcode & matrix2Flag;

    if ((opcode & 0x100) || m0)
    {
        if (matrixPtr);

        if (angle < M_PI_F)
        {
            angle = angle * 0.5f;
        }
        else
        {
            angle = M_TAU_F - ((M_TAU_F - angle) * 0.5f);
        }
    }

    if (opcode & 0x100)
    {
        if (mtx != NULL)
        {
            matrix_4x4_set_position_and_rotation_around_y((f32 *) &data->Group.Origin, angle, &localMtx);
            matrix_4x4_multiply_homogeneous(mtx, localMtxPtr, &render_pos[m1].pos);
        }
        else
        {
            matrix_4x4_set_position_and_rotation_around_y((f32 *) &data->Group.Origin, angle, &render_pos[m1].pos);
        }
    }

    if (m0)
    {
        if (mtx != NULL)
        {
            matrixPtr = &localMtx;
        }
        else
        {
            matrixPtr = &render_pos[m2].pos;
        }

        matrix_4x4_set_rotation_around_y(angle, matrixPtr);

        if (M_PI_F <= angle)
        {
            angle = M_TAU_F - angle;
        }

        if (angle < 0.890118f)
        {
            scalar = modelGetBendStretchScale(angle);
        }
        else
        {
            scalar = 1.5f;
        }

        matrix_column_3_scalar_multiply_2(scalar, (f32 *) matrixPtr);
        matrix_4x4_set_position(&data->Group.Origin, matrixPtr);

        if (mtx != NULL)
        {
            matrix_4x4_multiply_homogeneous(mtx, matrixPtr, &render_pos[m2].pos);
        }
    }
}


// Decodes a packed joint angle from the animation bitstream using either mtxA or mtxB.
f32 sub_GAME_7F06E540(s32 jointIndex, s32 useMtxB, ModelSkeleton *skeleton, ModelAnimation *anim, u8 *bitstream)
{
    anim = portAnimationResolve(anim);
    u32 bitOffset;
    u32 raw;
    u8 width;
    f32 angle;

    angle = 0.0f;
    width = anim->unk06;

    if (useMtxB != 0) {
        bitOffset = skeleton->Joints[jointIndex].mtxB * width;
    } else {
        bitOffset = skeleton->Joints[jointIndex].mtxA * width;
    }

    raw = modelAnimReadBitsAsU16Angle(bitstream, width, bitOffset);

    if (useMtxB != 0) {
        if (raw != 0) {
            angle = ((f32)(s32)(0x10000 - raw) * M_TAU_F) / M_U16_MAX_VALUE_F;
        }
    } else {
        angle = ((f32)raw * M_TAU_F) / M_U16_MAX_VALUE_F;
    }

    return angle;
}


void process_03_unknown(ModelRenderData *renderData, Model *model, ModelNode *node)
{
    ModelSkeleton *skeleton;
    ModelRoData_GroupRecord *rodata;
    s32 jointIndex;
    f32 angle;
    f32 tmp2;
    f32 tmp;

    rodata = &node->Data->Group;
    jointIndex = rodata->JointID;
    skeleton = model->obj->Skeleton;

    angle = sub_GAME_7F06E540(jointIndex, model->gunhand, skeleton, model->anim, (u8 *)model->unk34);

    if (model->unk2c != 0.0f) {
        tmp = sub_GAME_7F06E540(jointIndex, model->gunhand, skeleton, model->anim, (u8 *)model->unk38);
        angle = sub_GAME_7F06D0CC(angle, tmp, model->unk2c);
    }

    if (model->unk84 != 0.0f) {
        tmp = sub_GAME_7F06E540(jointIndex, model->unk25, skeleton, model->anim2, (u8 *)model->unk64);

        if (model->unk5c != 0.0f) {
            tmp2 = sub_GAME_7F06E540(jointIndex, model->unk25, skeleton, model->anim2, (u8 *)model->unk68);
            tmp = sub_GAME_7F06D0CC(tmp, tmp2, model->unk5c);
        }

        angle = sub_GAME_7F06D0CC(angle, tmp, model->unk84);
    }

    sub_GAME_7F06E2B8(renderData, model, node, angle);
}


void process_15_subposition(ModelRenderData* arg0, Model *model, ModelNode *node)
{
    union ModelRoData *rodata = node->Data;
    Mtxf *sp68;
    Mtxf sp28;
    s32 mtxindex = rodata->GroupSimple.Group1;
    RenderPosView *matrices = model->render_pos;

    if (node->Parent)
    {
        sp68 = modelFindNodeMtx(model, node->Parent, 0);
    }
    else
    {
        sp68 = arg0->basemtx;
    }

    if (sp68)
    {
        matrix_4x4_set_identity_and_position(&rodata->GroupSimple.Origin, &sp28);
        matrix_4x4_multiply_homogeneous(sp68, &sp28, &matrices[mtxindex]);
    }
    else
    {
        matrix_4x4_set_identity_and_position(&rodata->GroupSimple.Origin, &matrices[mtxindex]);
    }
}

/*
* Address: 0x7F06E858
*/
void modelUpdateDistanceRelations(Model* model, ModelNode* node)
{
    union ModelRoData *rodata = node->Data;
    union ModelRwData *rwdata = modelGetNodeRwData(model, node);
    Mtxf *mtx = modelFindNodeMtx(model, node, 0);
    f32 distance;

#ifndef TARGET_N64
    /* midas.ini [display] high_lod=1: always select the highest-detail LOD
     * (distance 0 picks the MinDistance-0 record) - same mechanism the
     * game itself uses for showcase screens via g_ModelDistanceDisabled. */
    extern s32 portHighLod(void);
    if (g_ModelDistanceDisabled || portHighLod())
#else
    if (g_ModelDistanceDisabled)
#endif
    {
        distance = 0;
    }
    else
    {
        distance = -mtx->m[3][2] * getPlayer_c_lodscalez();

        if (g_ModelDistanceScale != 1)
        {
            distance *= g_ModelDistanceScale;
        }
    }

    if (distance > rodata->LOD.MinDistance * model->scale || rodata->LOD.MinDistance == 0)
    {
        if (distance <= rodata->LOD.MaxDistance * model->scale)
        {
            rwdata->LOD.visible = TRUE;
            node->Child = rodata->LOD.Affects;
            return;
        }
    }

    rwdata->LOD.visible = FALSE;
    node->Child = NULL;
}

/*
* Address: 0x7F06E970
*/
void modelApplyDistanceRelations(Model* model, ModelNode* node)
{
    ModelRoData_LODRecord *rodata = &node->Data->LOD;
    ModelRwData_LODRecord *rwdata = modelGetNodeRwData(model, node);

    if (rwdata->visible)
    {
        node->Child = rodata->Affects;
    }
    else
    {
        node->Child = NULL;
    }
}


void modelApplyToggleRelations(Model* model, ModelNode* node)
{
    ModelRoData_SwitchRecord *rodata = &node->Data->Switch;
    ModelRwData_SwitchRecord *rwdata = modelGetNodeRwData(model, node);

    if (rwdata->visible)
    {
        node->Child = rodata->Controls;
    }
    else
    {
        node->Child = NULL;
    }
}


void modelApplyHeadRelations(Model* model, ModelNode* bodynode)
{
    struct ModelRwData_HeadPlaceholderRecord *rwdata = modelGetNodeRwData(model, bodynode);

    if (rwdata->ModelFileHeader)
    {
        ModelNode *headnode = rwdata->ModelFileHeader->RootNode;

        bodynode->Child = headnode;

        while (headnode)
        {
            headnode->Parent = bodynode;
            headnode = headnode->Next;
        }
    }
}


void modelApplyReorderRelationsByArg(ModelNode *basenode, bool visible)
{
    union ModelRoData *rodata = basenode->Data;
    ModelNode *node1;
    ModelNode *node2;
    ModelNode *loopnode;

    if (visible)
    {
        node1 = rodata->BSP.leftChild;
        node2 = rodata->BSP.rightChild;
    }
    else
    {
        node1 = rodata->BSP.rightChild;
        node2 = rodata->BSP.leftChild;
    }

    if (node1)
    {
        // I think what's happening here is there's two groups of siblings,
        // where node1 and node2 are the head nodes. Either group can be first,
        // and this is ensuring the node1 group is first.
        // Note that node2 might be NULL.

        basenode->Child = node1;
        node1->Prev = NULL;

        // Skip through node1's siblings until node2 is found or the end is
        // reached
        loopnode = node1;

        while (loopnode->Next && loopnode->Next != node2)
        {
            loopnode = loopnode->Next;
        }

        loopnode->Next = node2;

        if (node2)
        {
            // Append node2 and its siblings to node1's siblings
            node2->Prev = loopnode;
            loopnode = node2;

            while (loopnode->Next && loopnode->Next != node1)
            {
                loopnode = loopnode->Next;
            }

            loopnode->Next = NULL;
        }
    }
    else
    {
        basenode->Child = node2;

        if (node2)
        {
            node2->Prev = NULL;
        }
    }
}


void modelApplyReorderRelations(Model* model, ModelNode* node)
{
    union ModelRwData *rwdata = modelGetNodeRwData(model, node);
    modelApplyReorderRelationsByArg(node, rwdata->BSP.visible);
}


void modelUpdateReorderRelations(Model *model, ModelNode *node)
{
    union ModelRoData *rodata = node->Data;
    union ModelRwData *rwdata = modelGetNodeRwData(model, node);
    Mtxf *mtx = modelFindNodeMtx(model, node, 0);
    coord3d sp38;
    coord3d sp2c;
    f32 tmp;

    if (rodata->BSP.reserved == 0)
    {
        sp38.x = rodata->BSP.Vector.f[0];
        sp38.y = rodata->BSP.Vector.f[1];
        sp38.z = rodata->BSP.Vector.f[2];
        mtx4RotateVecInPlace(mtx, sp38.f);
    }
    else if (rodata->BSP.reserved == 2)
    {
        sp38.x = mtx->m[1][0] * rodata->BSP.Vector.f[1];
        sp38.y = mtx->m[1][1] * rodata->BSP.Vector.f[1];
        sp38.z = mtx->m[1][2] * rodata->BSP.Vector.f[1];
    }
    else if (rodata->BSP.reserved == 3)
    {
        sp38.x = mtx->m[2][0] * rodata->BSP.Vector.f[2];
        sp38.y = mtx->m[2][1] * rodata->BSP.Vector.f[2];
        sp38.z = mtx->m[2][2] * rodata->BSP.Vector.f[2];
    }
    else if (rodata->BSP.reserved == 1)
    {
        sp38.x = mtx->m[0][0] * rodata->BSP.Vector.f[0];
        sp38.y = mtx->m[0][1] * rodata->BSP.Vector.f[0];
        sp38.z = mtx->m[0][2] * rodata->BSP.Vector.f[0];
    }

    sp2c.x = rodata->BSP.Point.f[0];
    sp2c.y = rodata->BSP.Point.f[1];
    sp2c.z = rodata->BSP.Point.f[2];

    mtx4TransformVecInPlace(mtx, &sp2c);

    tmp = sp38.f[0] * sp2c.f[0] + sp38.f[1] * sp2c.f[1] + sp38.f[2] * sp2c.f[2];

    if (tmp < 0)
    {
        rwdata->BSP.visible = TRUE;
    }
    else
    {
        rwdata->BSP.visible = FALSE;
    }

    modelApplyReorderRelations(model, node);
}


void process_07_unknown(Model *model, ModelNode *node)
{
    union ModelRoData *rodata = node->Data;
    union ModelRwData *rwdata = modelGetNodeRwData(model, node);
    Mtxf *mtx = modelFindNodeMtx(model, node, 0);
    f32 ratio;
    f32 coord_multiplied;
    coord3d coord;
    s32 index1;
    f32 theta;
    s32 index2;
    s32 index3;

    modelGetScaledRootToOriginDir(model, &coord);

    theta = acosf(((coord.x * mtx->m[1][0]) + (coord.y * mtx->m[1][1])) + (coord.z * mtx->m[1][2]));
    ratio = acosf((((coord.x * mtx->m[2][0]) + (coord.y * mtx->m[2][1])) + (coord.z * mtx->m[2][2])) / sinf(theta));
    coord_multiplied = ((coord.x * mtx->m[0][0]) + (coord.y * mtx->m[0][1])) + (coord.z * mtx->m[0][2]);

    if ((coord_multiplied < 0.0f) && (ratio > 0.0f))
    {
        ratio = M_TAU_F - ratio;
    }

    index1 = (theta * 64.0f) / M_TAU_F;

    index2 = (s32) ((ratio * M_U16_MAX_VALUE_F) / M_TAU_F);
    index2 += D_800360C4[index1].unk04;
    index2 = index2 >> D_800360C4[index1].unk0C;

    index3 = index2 + D_800360C4[index1].unk00;

    rwdata->Op07.index = rodata->Op07.Data[index3];
}


void modelUpdateRelationsQuick(Model *model, ModelNode *parent)
{
    ModelNode *node = parent->Child;
    ModelNode **unused_parent;

    while (node)
    {
        s32 type = node->Opcode & 0xff;
        bool dochildren = TRUE;

        switch (type)
        {
            case MODELNODE_OPCODE_HEADER:
            case MODELNODE_OPCODE_GROUP:
            case MODELNODE_OPCODE_OP03:
            case MODELNODE_OPCODE_OP11:
            case MODELNODE_OPCODE_GUNFIRE:
            case MODELNODE_OPCODE_SHADOW:
            case MODELNODE_OPCODE_OP14:
            case MODELNODE_OPCODE_INTERLINK:
            case MODELNODE_OPCODE_OP16:
            case MODELNODE_OPCODE_GROUPSIMPLE:
                dochildren = FALSE;
                break;
            case MODELNODE_OPCODE_LOD:
                modelUpdateDistanceRelations(model, node);
                break;
            case MODELNODE_OPCODE_BSP:
                modelUpdateReorderRelations(model, node);
                break;
            case MODELNODE_OPCODE_OP07:
                process_07_unknown(model, node);
                break;
            case MODELNODE_OPCODE_HEAD:
                modelApplyHeadRelations(model, node);
                break;
            case MODELNODE_OPCODE_DLCOLLISION:
                break;
        }

        if (dochildren && node->Child)
        {
            node = node->Child;
        }
        else
        {
            unused_parent = &parent;
            while (node)
            {
                if (node == parent->Parent)
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
}


/*
 * Address: 0x7F06EFC4
*/
void modelUpdateNodeRelations(Model *model)
{
    ModelNode *node = model->obj->RootNode;

    while (node)
    {
        u32 type = node->Opcode & 0xff;

        switch (type)
        {
            case MODELNODE_OPCODE_LOD:
                modelUpdateDistanceRelations(model, node);
                break;

            case MODELNODE_OPCODE_BSP:
                modelUpdateReorderRelations(model, node);
                break;

            case MODELNODE_OPCODE_OP07:
                process_07_unknown(model, node);
                break;

            case MODELNODE_OPCODE_SWITCH:
                modelApplyToggleRelations(model, node);
                break;

            case MODELNODE_OPCODE_HEAD:
                modelApplyHeadRelations(model, node);
                break;

            case MODELNODE_OPCODE_HEADER:
            case MODELNODE_OPCODE_DLCOLLISION:
            default:
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


void modelUpdateMatrices(ModelRenderData *arg0, Model *model)
{
    ModelNode *node = model->obj->RootNode;

    while (node)
    {
        u32 type = node->Opcode & 0xff;

        switch (type)
        {
            case MODELNODE_OPCODE_HEADER:
                process_01_group_heading(arg0, model, node);
                break;

            case MODELNODE_OPCODE_GROUP:
                process_02_position(arg0, model, node);
                break;

            case MODELNODE_OPCODE_OP03:
                process_03_unknown(arg0, model, node);
                break;

            case MODELNODE_OPCODE_GROUPSIMPLE:
                process_15_subposition(arg0, model, node);
                break;

            case MODELNODE_OPCODE_LOD:
                modelUpdateDistanceRelations(model, node);
                break;

            case MODELNODE_OPCODE_BSP:
                modelUpdateReorderRelations(model, node);
                break;

            case MODELNODE_OPCODE_OP07:
                process_07_unknown(model, node);
                break;

            case MODELNODE_OPCODE_SWITCH:
                modelApplyToggleRelations(model, node);
                break;

            case MODELNODE_OPCODE_HEAD:
                modelApplyHeadRelations(model, node);
                break;

            case MODELNODE_OPCODE_DLCOLLISION:
            default:
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


void instcalcmatrices(ModelRenderData* arg0, Model* arg1)
{
#if defined(LEFTOVERDEBUG)
    if (arg1 == NULL)
    {
        osSyncPrintf("instcalcmatrices: no objinst!\n");
        return_null();
    }

    if (arg0->basemtx == NULL)
    {
        osSyncPrintf("instcalcmatrices: no basemtx!\n");
        return_null();
    }

    if (arg0->mtxlist == NULL)
    {
        osSyncPrintf("instcalcmatrices: no mtxlist!\n");
        return_null();
    }
#endif
    arg1->render_pos = (RenderPosView* ) arg0->mtxlist;
    arg0->mtxlist += arg1->obj->numMatrices;
    modelUpdateMatrices((ModelRenderData* ) arg0, arg1);
}


/**
 * Address 0x7F06F2F8 (VERSION_US, VERSION_JP)
 * Address 0x7F06F670 (VERSION_EU)
*/
void subcalcmatrices(ModelRenderData *arg0, struct Model *arg1)
{
#if defined(LEFTOVERDEBUG)
    if (arg1 == NULL)
    {
        osSyncPrintf("subcalcmatrices: no objanim!\n");
        return_null();
    }

    if (arg0->basemtx == NULL)
    {
        osSyncPrintf("subcalcmatrices: no basemtx!\n");
        return_null();
    }

    if (arg0->mtxlist == NULL)
    {
        osSyncPrintf("subcalcmatrices: no mtxlist!\n");
        return_null();
    }
#endif

    if (arg1->anim != NULL)
    {
#if defined(LEFTOVERDEBUG)
        if ((arg1->attachedto != NULL) && (arg1->attachedto_objinst == NULL))
        {
            osSyncPrintf("subcalcmatrices: no attach for objinst!\n");
            return_null();
        }

        if (((s32) arg1->framea < 0) || ((s32) arg1->framea >= (s32) arg1->anim->unk04))
        {
            osSyncPrintf("subcalcmatrices: framea out of range!\n");
            return_null();
        }

        if (((s32) arg1->frameb < 0) || ((s32) arg1->frameb >= (s32) arg1->anim->unk04))
        {
            osSyncPrintf("subcalcmatrices: frameb out of range!\n");
            return_null();
        }

        if ((arg1->unk84 == 0) || ((arg1->unk84 != 0) && (arg1->anim2 != NULL)))
        {
            //
        }
        else
        {
            osSyncPrintf("subcalcmatrices: no anim2!\n");
            return_null();
        }

        if (
            (arg1->anim2 != NULL)
            && (
                (arg1->anim2 == NULL)
                || (arg1->frame2a < 0)
                || ((s32) arg1->frame2a >= (s32) arg1->anim2->unk04)
                )
            )
        {
            osSyncPrintf("subcalcmatrices: frame2a out of range!\n");
            return_null();
        }

        if (
            (arg1->anim2 == NULL)
            || (
                (arg1->anim2 != NULL)
                 && (arg1->frame2b >= 0)
                 && ((s32) arg1->frame2b < (s32) arg1->anim2->unk04)
                )
            )
        {
            //
        }
        else
        {
            osSyncPrintf("subcalcmatrices: frame2b out of range!\n");
            return_null();
        }
#endif

        arg1->unk34 = loadAnimationFrame(arg1->anim, arg1->framea, arg1->obj->Skeleton);

        if (arg1->unk2c != 0.0f)
        {
            arg1->unk38 = loadAnimationFrame(arg1->anim, arg1->frameb, arg1->obj->Skeleton);
        }

        if (arg1->anim2 != NULL)
        {
            arg1->unk64 = loadAnimationFrame(arg1->anim2, arg1->frame2a, arg1->obj->Skeleton);

            if (arg1->unk5c != 0.0f)
            {
                arg1->unk68 = loadAnimationFrame(arg1->anim2, arg1->frame2b, arg1->obj->Skeleton);
            }
        }

        modelResetAnimationsScratchBuffer();
    }

    instcalcmatrices(arg0, arg1);
}

/**
 * Address 0x7F06F5AC.
*/
struct ModelAnimation * objecthandlerGetModelAnim(struct Model* model) {
    return model->anim;
}

s8 objecthandlerGetModelGunhand(Model *model)
{
    return model->gunhand;
}


/**
 * Address 0x7F06F5BC.
*/
f32 modelGetAnimFrame(Model *model)
{
    return model->animframe1;
}


f32 modelGetAnimEndFrame(Model *model)
{
    f32 end;
    ModelAnimation *modelAnimation;

    end = model->endframe;

    if (end >= 0.0f)
    {
        return end;
    }

    modelAnimation = model->anim;

    if (modelAnimation != NULL)
    {
        return modelAnimation->unk04 - 1;
    }

    return 0.0f;
}


f32 modelGetAnimSpeed(Model *model)
{
    return model->speed;
}


/**
 * Address 0x7F06F618.
 * PD: modelGetAbsAnimSpeed
*/
f32 modelGetAbsAnimSpeed(Model *model)
{
    f32 speed;

    speed = model->speed;

    if (speed < 0.0f)
    {
        speed = -speed;
    }

    return speed;
}

/**
 * Unused Function
 * Unreferenced
*/
f32 modelGetEffectiveAnimSpeed(Model *model) {
    return modelGetAnimSpeed(model) * model->playspeed;
}


s32 modelConstrainOrWrapAnimFrame(s32 frame, ModelAnimation *anim, f32 endframe)
{
    anim = portAnimationResolve(anim);
    if (frame < 0) {
        if (anim->unk07 & 1) {
            frame = anim->unk04 - ((-frame) % anim->unk04);
        } else {
            frame = 0;
        }
    }
    else if ((0.0f <= endframe) && ((s32)endframe < frame)) {
        frame = ceilFloatToInt(endframe);
    }
    else if (frame >= anim->unk04) {
        if (anim->unk07 & 1) {
            frame %= anim->unk04;
        } else {
            frame = anim->unk04 - 1;
        }
    }
    else {
    }

    return frame;
}


void modelCopyAnimForMerge(Model *model, f32 timemerge)
{
    ModelAnimation *anim;
    ModelNode *root;
    struct modeldata_root *rwdata;
    s32 opcode;

    if (0.0f < timemerge) {
        anim = model->anim;

        if (anim != NULL) {
            root = model->obj->RootNode;
            opcode = root->Opcode & 0xff;

            model->anim2 = anim;
            model->animframe2 = model->animframe1;
            model->unk5c = model->unk2c;
            model->unk25 = model->gunhand;
            model->frame2a = model->framea;
            model->frame2b = model->frameb;
            model->speed2 = model->speed;
            model->unk74 = model->newspeed;
            model->unk78 = model->oldspeed;
            model->unk7c = model->timespeed;
            model->unk80 = model->elapsespeed;
            model->unk6c = model->endframe;

            if (opcode == MODELNODE_OPCODE_HEADER) {
                rwdata = (struct modeldata_root *)modelGetNodeRwData(model, root);
                rwdata->unk02 = 1;
                rwdata->unk4c.x = rwdata->unk34.x;
                rwdata->unk4c.y = rwdata->unk34.y;
                rwdata->unk4c.z = rwdata->unk34.z;
                rwdata->unk40.x = rwdata->unk24.x;
                rwdata->unk40.y = rwdata->unk24.y;
                rwdata->unk40.z = rwdata->unk24.z;
            }

            return;
        }
    }

    model->anim2 = NULL;
}


void modelSetAnimation2(Model *model, ModelAnimation *anim, s32 flip, f32 frame, f32 speed, f32 arg5)
{
    anim = portAnimationResolve(anim);
    s32 hadNoAnim = !model->anim;
    s32 padding;
    s32 type;

    if (model->anim2 != NULL) {
        model->unk88 = arg5;
        model->unk8c = 0.0f;
        model->unk84 = 1.0f;
    } else {
        model->unk88 = 0.0f;
        model->unk84 = 0.0f;
    }

    model->anim = anim;
    model->gunhand = flip;
    model->endframe = -1.0f;
    model->speed = speed;
    model->timespeed = 0.0f;

    modelSetAnimFrame(model, frame);

    model->animlooping = 0;

    type = model->obj->RootNode->Opcode & 0xff;

    if (type == MODELNODE_OPCODE_HEADER) {
        ModelRoData_HeaderRecord *rodata = &model->obj->RootNode->Data->Header;
        ModelRwData_HeaderRecord *rwdata = modelGetNodeRwData(model, model->obj->RootNode);
        f32 temp_f14;
        f32 sinAngle;
        f32 scale;
        f32 cosAngle;
        s32 animPart;
        coord3d pos;
        f32 angleDelta;
        coord3d tmppos[2];
        ModelSkeleton *skeleton;

        animPart = rodata->AnimPart;
        skeleton = model->obj->Skeleton;
        scale = model->scale * model->anim_translation_scale;
        pos = D_80036244;

        angleDelta = sub_GAME_7F06D3F4(animPart, model->gunhand, skeleton, model->anim, model->frameb, &pos
        );

        if (scale != 1.0f) {
            pos.f[0] *= scale;
            pos.f[1] *= scale;
            pos.f[2] *= scale;
        }

        cosAngle = cosf(rwdata->unk14);
        sinAngle = sinf(rwdata->unk14);
        scale = model->unk2c;

        if (model->unk2c == 0.0f) {
            rwdata->unk34.x = rwdata->pos.f[0];
            rwdata->unk34.y = rwdata->pos.f[1] - rwdata->ground;
            rwdata->unk34.z = rwdata->pos.f[2];

            rwdata->unk30 = rwdata->unk14;

            tmppos[1].x = rwdata->unk34.f[0] + (pos.f[0] * cosAngle) + (pos.f[2] * sinAngle);
            tmppos[1].y = pos.f[1];
            tmppos[1].z = rwdata->unk34.f[2] - (pos.f[0] * sinAngle) + (pos.f[2] * cosAngle);

            rwdata->unk24.x = tmppos[1].f[0];
            rwdata->unk24.y = tmppos[1].f[1];
            rwdata->unk24.z = tmppos[1].f[2];

            if (rwdata->unk18 == 0.0f) {
                rwdata->unk20 = rwdata->unk30 + angleDelta;

                if (rwdata->unk20 >= M_TAU_F) {
                    rwdata->unk20 -= M_TAU_F;
                }
            }

            rwdata->unk01 = 1;
        } else {
            f32 x;
            f32 y;
            f32 z;

            x = pos.f[0] * cosAngle + pos.f[2] * sinAngle;
            y = pos.f[1];
            z = -pos.f[0] * sinAngle + pos.f[2] * cosAngle;

            tmppos[0].x = rwdata->pos.f[0] + x * (1.0f - model->unk2c);
            tmppos[0].y = y;
            tmppos[0].z = rwdata->pos.f[2] + z * (1.0f - model->unk2c);

            rwdata->unk24.x = tmppos[0].x;
            rwdata->unk24.y = tmppos[0].y;
            rwdata->unk24.z = tmppos[0].z;

            rwdata->unk34.f[0] = rwdata->unk24.f[0] - x;
            rwdata->unk34.f[1] = (rwdata->pos.f[1] - rwdata->ground) - (y - (rwdata->pos.f[1] - rwdata->ground)) * model->unk2c / (1.0f - model->unk2c);
            rwdata->unk34.f[2] = rwdata->unk24.f[2] - z;

            temp_f14 = rwdata->unk14 - angleDelta;

            if (temp_f14 < 0.0f) {
                temp_f14 += M_TAU_F;
            }

            rwdata->unk30 = sub_GAME_7F06D0CC(rwdata->unk14, temp_f14, model->unk2c);

            if (rwdata->unk18 == 0.0f) {
                rwdata->unk20 = rwdata->unk30 + angleDelta;

                if (rwdata->unk20 >= M_TAU_F) {
                    rwdata->unk20 -= M_TAU_F;
                }
            }

            rwdata->unk01 = 1;
        }

        if (hadNoAnim) {
            rwdata->unk34.y = rwdata->unk24.y;
        }
    }
}


void modelSetAnimationWithMerge(Model *model, ModelAnimation *modelAnimation, s32 flip, f32 startframe, f32 speed, f32 timemerge, s32 domerge) {
    if (domerge != 0) {
        modelCopyAnimForMerge(model, timemerge);
    }
    modelSetAnimation2(model, modelAnimation, flip, startframe, speed, timemerge);
}


void modelSetAnimation(Model *model, ModelAnimation *modelAnimation, s32 flip, f32 startframe, f32 speed, f32 merge) {
    modelCopyAnimForMerge(model, merge);
    modelSetAnimation2(model, modelAnimation, flip, startframe, speed, merge);
}


/*
 * Match-only overlay types for sub_GAME_7F06FCFC.
 */
typedef struct ModelCopyHead {
    u32 words[8];      // 0x00-0x1f
} ModelCopyHead;


typedef struct ModelCopyBc {
    u32 words[0x2f];   // 0x00-0xbb
} ModelCopyBc;


/**
 * Unreferenced.
 *
 * The function copies the Model data through anim_translation_scale (0x00-0xbb),
 * then restores the destination's base/resource fields (0x00-0x1f).
 * Maybe some kind of old/abandoned anim copy function.
 */
void sub_GAME_7F06FCFC(Model *src, Model *dst)
{
    ModelCopyHead tmp;

    tmp = *(ModelCopyHead *)dst;
    *(ModelCopyBc *)dst = *(ModelCopyBc *)src;
    *(ModelCopyHead *)dst = tmp;
}


void modelSetAnimLooping(Model *model, f32 loopframe, f32 loopmerge) {
    model->animlooping = 1;
    model->animloopframe = loopframe;
    model->animloopmerge = loopmerge;
}


void modelSetAnimEndFrame(Model *model, f32 endframe) {
    ModelAnimation *modelAnimation = model->anim;

    if ((modelAnimation != NULL) && (endframe < (modelAnimation->unk04 - 1))) {
        model->endframe = endframe;
    } else {
        model->endframe = -1.0f;
    }
#ifdef DEBUG
    // not too sure why debug wants to call this - must have some significance when most debug has been stripped from this file in XBLA
    modelSetAnimFrame(model, (int)model->animframe1);
#endif
}

void modelSetAnimFlipFunction(Model *model, void *callback) {
    model->animflipfunc = (intptr_t)callback;
}


/**
 * Unused Function
*/
void sub_GAME_7F06FE44(Model *model, s32 arg1) {
    model->unk9c = arg1;
}

void modelSetAnimSpeed(Model *model, f32 anim_speed, f32 startframe) {

    if (startframe > 0.0f) {
        model->timespeed = startframe;
        model->newspeed = anim_speed;
        model->elapsespeed = 0.0f;
        model->oldspeed = model->speed;
        return;
    }

    model->speed = anim_speed;
    model->timespeed = 0.0f;
}

/**
 * @param arg0:
 * @param arg1:
 * @param arg2: must be non-zero.
 *
 * Address 0x7F06FE90.
*/
void sub_GAME_7F06FE90(Model *model, f32 arg1, f32 arg2)
{
    f32 temp_f0;
    f32 phi_f2;
    f32 t;

    temp_f0 = model->animframe1;

    if (temp_f0 <= arg1)
    {
        phi_f2 = arg1 - temp_f0;
    }
    else
    {
        phi_f2 = ( (f32)model->anim->unk04 - temp_f0) + arg1;
    }

    t = model->speed + ((2.0f * phi_f2) / arg2);
    modelSetAnimSpeed(model, t, arg2);
}

void modelSetAnimPlaySpeed(Model *model, f32 animation_rate, f32 startframe) {
    if (startframe > 0.0f) {
        model->unkb0 = startframe;
        model->animrate = animation_rate;
        model->unkb4 = 0.0f;
        model->unkac = model->playspeed;
        return;
    }
    model->playspeed = animation_rate;
    model->unkb0 = 0.0f;
}


void sub_GAME_7F06FF5C(Model *model, intptr_t arg1) {
    model->unka0 = arg1;
}


void modelSetAnimFrame(Model* model, f32 frame)
{
    s32 framea;
    s32 frameb;
    bool forwards;

#ifndef TARGET_N64
    /* PORT: model->anim has been seen holding a small raw value instead of a pointer
     * in multiplayer; report and skip instead of crashing in modelConstrainOrWrapAnimFrame. */
    {
        extern int portPtrLooksBad(const void *);
        extern void portModelAuditReport(const char *, const void *, const void *, const void *);
        if (portPtrLooksBad(model->anim)) {
            portModelAuditReport("modelSetAnimFrame:anim", model->anim, model, model->obj);
            return;
        }
    }
#endif

    framea = floorFloatToInt(frame);

    forwards = (model->speed >= 0);
    frameb = (forwards ? framea + 1 : framea - 1);

    model->framea = modelConstrainOrWrapAnimFrame(framea, model->anim, model->endframe);
    model->frameb = modelConstrainOrWrapAnimFrame(frameb, model->anim, model->endframe);

    if (model->framea == model->frameb)
    {
        model->unk2c = 0.0f;
        model->animframe1 = model->framea;
    }
    else if (forwards)
    {
        f32 tmp = frame - framea;
        model->unk2c = tmp;
        model->animframe1 = model->framea + tmp;
    }
    else
    {
        f32 tmp = 1.0f - (frame - (f32) frameb);
        model->unk2c = tmp;
        model->animframe1 = model->frameb + (1.0f - tmp);
    }
}


void modelSetAnimFrame2(Model* model, f32 frame1, f32 frame2)
{
    s32 framea;
    s32 frameb;
    bool forwards;

    modelSetAnimFrame(model, frame1);

    if (model->anim2 != NULL)
    {
#ifndef TARGET_N64
        /* PORT: same raw-value-as-pointer guard for anim2. */
        {
            extern int portPtrLooksBad(const void *);
            extern void portModelAuditReport(const char *, const void *, const void *, const void *);
            if (portPtrLooksBad(model->anim2)) {
                portModelAuditReport("modelSetAnimFrame2:anim2", model->anim2, model, model->obj);
                return;
            }
        }

        /* PORT: sibling of the modelSetAnimFrame2WithChrStuff NaN/huge guard;
         * this function is also reached directly (modelTickAnimCore's
         * !update_chrstuff path), bypassing that guard. A NaN/huge frame2 would
         * write a bad frame2a/frame2b that subcalcmatrices then reads out of
         * bounds. */
        if (frame2 != frame2 || frame2 > 1000000.0f || frame2 < -1000000.0f) {
            extern void portModelAuditReport(const char *, const void *, const void *, const void *);
            portModelAuditReport("modelSetAnimFrame2:frame2(NaN/huge)", model->anim2, model, model->obj);
            return;
        }
#endif
        framea = floorFloatToInt(frame2);

        forwards = (model->speed2 >= 0.0f);
        frameb = forwards ? (framea + 1) : (framea - 1);

        model->frame2a = modelConstrainOrWrapAnimFrame(framea, model->anim2, model->unk6c);
        model->frame2b = modelConstrainOrWrapAnimFrame(frameb, model->anim2, model->unk6c);

        if (model->frame2a == model->frame2b)
        {
            model->unk5c = 0.0f;
            model->animframe2 = model->frame2a;
        }
        else if (forwards != 0)
        {
            f32 tmp = frame2 - framea;
            model->unk5c = tmp;
            model->animframe2 = model->frame2a + tmp;
        }
        else
        {
            f32 tmp = 1.0f - (frame2 - (f32) frameb);
            model->unk5c = tmp;
            model->animframe2 = model->frame2b + (1.0f - tmp);
        }
    }
}


/**
 * Address 0x7F0701D4.
*/
void modelSetAnimMergingEnabled(s32 arg0)
{
    g_ModelAnimMergingEnabled = arg0;
}


/**
 * Address 0x7F0701E0.
*/
u32 modelIsAnimMergingEnabled(void)
{
    return g_ModelAnimMergingEnabled;
}


void modelSetAnimFrame2WithChrStuff(Model *model, f32 framea, f32 frameb, f32 frame2a, f32 frame2b)
{
    Model *modelptr;
    ModelRwData_HeaderRecord *header;
    ModelNode *root;
    ModelNode *node;
    s32 jointnum;
    ModelSkeleton *skeleton;
    f32 scale;
    s32 curframe;
    f32 speed;
    coord3d pos;
    f32 anglecur;
    f32 cosangle;
    s32 vb;
    s32 endframe;
    coord3d pos34;
    s32 forward;
    coord3d pos24;
    f32 angle20;
    s32 flag;
    f32 angledelta;
    f32 sinangle;
    f32 speed2;
    f32 v[3];
    s32 framenum;
    s32 va;
    f32 inv;
    s32 end2;

#ifndef TARGET_N64
    /* PORT: modelSetAnimFrame/modelSetAnimFrame2 already guard model->anim
     * against a bad pointer; this ChrStuff variant needs the same guard so a
     * model ticked before its animation is assigned is reported and skipped
     * instead of faulting. */
    {
        extern int portPtrLooksBad(const void *);
        extern void portModelAuditReport(const char *, const void *, const void *, const void *);
        if (portPtrLooksBad(model->anim)) {
            portModelAuditReport("modelSetAnimFrame2WithChrStuff:anim", model->anim, model, model->obj);
            return;
        }
    }

    /* PORT: a NaN or absurdly large frame value (e.g. from a NaN model->speed)
     * makes the uncapped frame-walking while(1) loop below run for hundreds of
     * millions of iterations, freezing the game, so report it and skip this
     * tick's chr-stuff update. The same check covers frame2a/frame2b, since a
     * NaN there makes subcalcmatrices read the animation table out of bounds
     * (its range check does not actually return). */
    if (framea != framea || frameb != frameb
        || framea > 1000000.0f || framea < -1000000.0f
        || frameb > 1000000.0f || frameb < -1000000.0f
        || frame2a != frame2a || frame2b != frame2b
        || frame2a > 1000000.0f || frame2a < -1000000.0f
        || frame2b > 1000000.0f || frame2b < -1000000.0f) {
        extern void portModelAuditReport(const char *, const void *, const void *, const void *);
        portModelAuditReport("modelSetAnimFrame2WithChrStuff:frame(NaN/huge)", model->anim, model, model->obj);
        return;
    }
#endif

    modelptr = model;
    root = modelptr->obj->RootNode;
    va = root->Opcode;

    if ((va & 0xff) == 1)
    {
        node = (ModelNode *) root->Data;
        header = (ModelRwData_HeaderRecord *) modelGetNodeRwData(modelptr, root);

        if (header->unk00 == 0)
        {
            /* upstream's u16 AnimPart field (ex raw-word ModelType>>16)
             * is endian-neutral - one read serves both targets */
            jointnum = ((union ModelRoData *) root->Data)->Header.AnimPart;
            skeleton = modelptr->obj->Skeleton;
            scale = modelptr->scale * modelptr->anim_translation_scale;
            pos = D_80036254;

            pos34.f[0] = header->unk34.x;
            pos34.f[1] = header->unk34.y;
            pos34.f[2] = header->unk34.z;
            anglecur = header->unk30;
            pos24.f[0] = header->unk24.x;
            pos24.f[1] = header->unk24.y;
            pos24.f[2] = header->unk24.z;
            angle20 = header->unk20;
            flag = header->unk01;

            speed = modelptr->speed;

            if (speed < 0.0f)
            {
                speed = -speed;
            }

            speed2 = modelptr->speed2;

            if (speed2 < 0.0f)
            {
                speed2 = -speed2;
            }

            forward = 0;

            if (framea <= frameb)
            {
                forward = 1;
            }

            if (forward)
            {
                curframe = floorFloatToInt(framea) + 1;
                endframe = floorFloatToInt(frameb);
            }
            else
            {
                curframe = ceilFloatToInt(framea) - 1;
                endframe = ceilFloatToInt(frameb);
            }

            while (1)
            {
                if (forward)
                {
                    if (endframe < curframe)
                    {
                        break;
                    }
                }
                else
                {
                    if (curframe < endframe)
                    {
                        break;
                    }
                }

                framenum = modelConstrainOrWrapAnimFrame(curframe, modelptr->anim, modelptr->endframe);
                modelptr->framea = framenum;

                if (flag)
                {
                    pos34.f[0] = pos24.f[0];
                    pos34.f[1] = pos24.f[1];
                    pos34.f[2] = pos24.f[2];

                    if (header->unk18 == 0.0f)
                    {
                        anglecur = angle20;
                    }
                }
                else
                {
                    angledelta = sub_GAME_7F06D3F4(jointnum, modelptr->gunhand, skeleton, modelptr->anim, framenum, &pos);

                    if (scale != 1.0f)
                    {
                        pos.x *= scale;
                        pos.y *= scale;
                        pos.z *= scale;
                    }

                    if (!forward)
                    {
                        pos.x = -pos.x;
                        pos.z = -pos.z;

                        if (0.0f < angledelta)
                        {
                            angledelta = M_TAU_F - angledelta;
                        }
                    }

                    cosangle = cosf(header->unk14);
                    sinangle = sinf(header->unk14);

                    pos34.f[0] += (pos.x * cosangle) + (pos.z * sinangle);
                    pos34.f[1] = pos.y;
                    pos34.f[2] += (-pos.x * sinangle) + (pos.z * cosangle);

                    if (header->unk18 == 0.0f)
                    {
                        anglecur += angledelta;

                        if (M_TAU_F <= anglecur)
                        {
                            anglecur -= M_TAU_F;
                        }
                    }
                }

                if (forward)
                {
                    curframe += 1;
                }
                else
                {
                    curframe -= 1;
                }

                framenum = modelConstrainOrWrapAnimFrame(curframe, modelptr->anim, modelptr->endframe);
                modelptr->frameb = framenum;

                if (modelptr->frameb != modelptr->framea)
                {
                    angledelta = sub_GAME_7F06D3F4(jointnum, modelptr->gunhand, skeleton, modelptr->anim, framenum, &pos);
                    flag = 1;

                    if (scale != 1.0f)
                    {
                        pos.x *= scale;
                        pos.y *= scale;
                        pos.z *= scale;
                    }

                    if (!forward)
                    {
                        pos.x = -pos.x;
                        pos.z = -pos.z;

                        if (0.0f < angledelta)
                        {
                            angledelta = M_TAU_F - angledelta;
                        }
                    }

                    cosangle = cosf(header->unk30);
                    sinangle = sinf(header->unk30);

                    if (g_ModelAnimMergingEnabled && modelptr->anim2 != NULL)
                    {
                        pos24.f[0] = (pos.z * sinangle) + (pos.x * cosangle);
                        pos24.f[2] = (pos.z * cosangle) + (-pos.x * sinangle);

                        if (0.0f < speed)
                        {
                            f32 t = modelptr->unk84 - (modelptr->playspeed / (speed * modelptr->unk88));

                            if (t < 0.0f)
                            {
                                t = 0.0f;
                            }

                            t = (modelptr->unk84 + t) * 0.5f;

                            v[0] = ((header->unk40.x - header->unk4c.x) * speed2) / speed;
                            v[2] = ((header->unk40.z - header->unk4c.z) * speed2) / speed;

                            pos24.f[0] += (v[0] - pos24.f[0]) * t;
                            pos24.f[2] += (v[2] - pos24.f[2]) * t;
                        }
                        else
                        {
                            pos24.f[0] += (header->unk40.x - header->unk4c.x) * modelptr->unk84;
                            pos24.f[2] += (header->unk40.z - header->unk4c.z) * modelptr->unk84;
                        }

                        pos24.f[0] += pos34.f[0];
                        pos24.f[2] += pos34.f[2];
                        pos24.f[1] = pos.y;
                    }
                    else
                    {
                        pos24.f[0] = (pos34.f[0] + (pos.f[0] * cosangle)) + (pos.f[2] * sinangle);
                        pos24.f[1] = pos.f[1];
                        pos24.f[2] = (pos34.f[2] - (pos.f[0] * sinangle)) + (pos.f[2] * cosangle);
                    }

                    if (0.0f < header->unk5c)
                    {
                        if (0.0f < speed)
                        {
                            inv = 1.0f / speed;

                            if (header->unk5c < inv)
                            {
                                inv = header->unk5c;
                                header->unk5c = 0.0f;
                            }
                            else
                            {
                                header->unk5c -= inv;
                            }

                            angledelta += header->unk58 * inv;

                            if (angledelta < 0.0f)
                            {
                                angledelta += M_TAU_F;
                            }
                            else if (M_TAU_F <= angledelta)
                            {
                                angledelta -= M_TAU_F;
                            }
                        }
                    }

                    if (header->unk18 == 0.0f)
                    {
                        angle20 = anglecur + angledelta;

                        if (M_TAU_F <= angle20)
                        {
                            angle20 -= M_TAU_F;
                        }
                    }
                }
            }

            header->unk34.x = pos34.f[0];
            header->unk34.y = pos34.f[1];
            header->unk34.z = pos34.f[2];
            header->unk30 = anglecur;
            header->unk24.x = pos24.f[0];
            header->unk24.y = pos24.f[1];
            header->unk24.z = pos24.f[2];
            header->unk20 = angle20;

            va = modelptr->framea;
            vb = modelptr->frameb;

            if (vb == va)
            {
                modelptr->unk2c = 0.0f;
                modelptr->animframe1 = va;
            }
            else if (forward)
            {
                modelptr->unk2c = frameb - (f32)endframe;
                modelptr->animframe1 = (f32)va + modelptr->unk2c;
            }
            else
            {
                modelptr->unk2c = (f32)endframe - frameb;
                modelptr->animframe1 = (f32)vb + (1.0f - modelptr->unk2c);
            }

            if (modelptr->anim2 != NULL)
            {
                curframe = floorFloatToInt(frame2a);
                end2 = floorFloatToInt(frame2b);

                if ((forward && (curframe < end2)) || (!forward && (end2 < curframe)))
                {
                    if (header->unk02)
                    {
                        header->unk4c.y = header->unk40.y;
                    }
                    else
                    {
                        header->unk4c.y = header->unk34.y;
                    }

                    modelptr->frame2a = modelConstrainOrWrapAnimFrame(end2, modelptr->anim2, modelptr->unk6c);
                    framenum = modelConstrainOrWrapAnimFrame(end2 + 1, modelptr->anim2, modelptr->unk6c);
                    modelptr->frame2b = framenum;

                    sub_GAME_7F06D3F4(jointnum, modelptr->unk25, skeleton, modelptr->anim2, framenum, &pos);

                    if (scale != 1.0f)
                    {
                        pos.y *= scale;
                    }

                    header->unk40.y = pos.y;
                    header->unk02 = 1;
                }

                if (forward)
                {
                    modelptr->unk5c = frame2b - (f32)end2;
                    modelptr->animframe2 = (f32)modelptr->frame2a + modelptr->unk5c;
                }
                else
                {
                    modelptr->unk5c = 1.0f - (frame2b - (f32)end2);
                    modelptr->animframe2 = (f32)modelptr->frame2b + (1.0f - modelptr->unk5c);
                }
            }
            else
            {
                header->unk02 = 0;
            }
        }
        else
        {
            modelSetAnimFrame2(modelptr, frameb, frame2b);
        }
    }
    else
    {
        modelSetAnimFrame2(modelptr, frameb, frame2b);
    }
}


#ifndef TARGET_N64
/* PC: the vanilla body plus one extra step scaled by `frac` for a fractional tick; the whole ticks
 * still run bit-identically to vanilla (see frametiming.c "Sub-field SMOOTHING"). */
static void modelTickAnimCore(struct Model *model, s32 numticks, f32 frac, s32 update_chrstuff)
#define ANIMSTEP(x) ((x) * s)
#else
void modelTickAnim(struct Model *model, s32 numticks, s32 update_chrstuff)
#define ANIMSTEP(x) (x)
#endif
{
    f32 frame;
    f32 frame2;
    f32 animlast;

    frame = model->animframe1;
    frame2 = model->animframe2;

#ifndef TARGET_N64
    if (numticks > 0 || frac > 0.0f)
    {
        while (numticks > 0 || frac > 0.0f)
        {
            f32 s = 1.0f;
#else
    if (numticks > 0)
    {
        while (numticks > 0)
        {
#endif
            f32 playspeed;
            f32 speed;
            f32 limit;
            f32 endframe;
            f32 saved_newspeed;
            f32 saved_oldspeed;
            f32 saved_timespeed;
            f32 saved_elapsespeed;
            f32 loopframe;

#ifndef TARGET_N64
            if (numticks <= 0)
            {
                s = frac;       /* the trailing fractional step */
                frac = 0.0f;
            }
#endif

            if (model->unkb0 > 0.0f)
            {
                model->unkb4 += ANIMSTEP(1.0f);

                if (model->unkb4 < model->unkb0)
                {
                    model->playspeed = model->unkac + ((model->animrate - model->unkac) * model->unkb4) / model->unkb0;
                }
                else
                {
                    model->unkb0 = 0.0f;
                    model->playspeed = model->animrate;
                }
            }

            playspeed = model->playspeed;

            if (model->unk88 > 0.0f)
            {
                model->unk8c += ANIMSTEP(playspeed);

                if (model->unk8c == 0.0f)
                {
                    model->unk84 = 1.0f;
                    playspeed = model->playspeed;
                }
                else if (model->unk8c < model->unk88)
                {
                    model->unk84 = (model->unk88 - model->unk8c) / model->unk88;
                    playspeed = model->playspeed;
                }
                else
                {
                    model->unk88 = 0.0f;
                    model->unk84 = 0.0f;
                    model->anim2 = NULL;
                    playspeed = model->playspeed;
                }
            }

            if (model->timespeed > 0.0f)
            {
                model->elapsespeed += ANIMSTEP(playspeed);

                if (model->elapsespeed < model->timespeed)
                {
                    model->speed = model->oldspeed
                        + ((model->newspeed - model->oldspeed) * model->elapsespeed)
                        / model->timespeed;
                    playspeed = model->playspeed;
                }
                else
                {
                    model->timespeed = 0.0f;
                    playspeed = model->playspeed;
                    model->speed = model->newspeed;
                }
            }

            speed = model->speed;
            frame += ANIMSTEP(playspeed * speed);

            if (model->anim2 != NULL)
            {
                if (model->unk7c > 0.0f)
                {
                    model->unk80 += ANIMSTEP(playspeed);

                    if (model->unk80 < model->unk7c)
                    {
                        model->speed2 = model->unk78 + ((model->unk74 - model->unk78) * model->unk80) / model->unk7c;
                        playspeed = model->playspeed;
                    }
                    else
                    {
                        model->unk7c = 0.0f;
                        playspeed = model->playspeed;
                        model->speed2 = model->unk74;
                    }
                }

                if (frame2);

                frame2 += ANIMSTEP(playspeed * model->speed2);
            }

            if (model->animlooping)
            {
                animlast = model->anim->unk04 - 1;
                endframe = model->endframe;

                if (endframe);

                if (speed >= 0.0f)
                {
                    limit = animlast;
                    loopframe = model->animloopframe;

                    if (endframe >= 0.0f && endframe < animlast)
                    {
                        limit = endframe;
                    }
                }
                else
                {
                    limit = model->animloopframe;
                    loopframe = animlast;

                    if (endframe >= 0.0f && endframe < animlast)
                    {
                        loopframe = endframe;
                    }
                }

                if ((speed >= 0.0f && frame >= limit) || (speed < 0.0f && frame <= limit))
                {
                    saved_newspeed = model->newspeed;
                    saved_oldspeed = model->oldspeed;
                    saved_timespeed = model->timespeed;
                    saved_elapsespeed = model->elapsespeed;

                    if (update_chrstuff)
                    {
                        modelSetAnimFrame2WithChrStuff(model, model->animframe1, limit, 0.0f, 0.0f);
                    }
                    else
                    {
                        modelSetAnimFrame2(model, limit, 0.0f);
                    }

                    modelSetAnimation(model, model->anim, model->gunhand, loopframe, model->speed, model->animloopmerge);

                    model->animlooping = 1;
                    model->endframe = endframe;
                    model->newspeed = saved_newspeed;
                    model->oldspeed = saved_oldspeed;
                    model->timespeed = saved_timespeed;
                    model->elapsespeed = saved_elapsespeed;

                    frame2 = frame;
                    frame = loopframe + frame - limit;

                    if (model->animflipfunc != 0)
                    {
                        ((void (*)(void))model->animflipfunc)();
                    }
                }
            }

            numticks--;
        }

        if (update_chrstuff)
        {
            if (model->anim2 != NULL)
            {
                modelSetAnimFrame2WithChrStuff(model, model->animframe1, frame, model->animframe2, frame2);
            }
            else
            {
                modelSetAnimFrame2WithChrStuff(model, model->animframe1, frame, 0.0f, 0.0f);
            }
        }
        else
        {
            if (model->anim2 != NULL)
            {
                modelSetAnimFrame2(model, frame, frame2);
            }
            else
            {
                modelSetAnimFrame2(model, frame, 0.0f);
            }
        }
    }
}
#undef ANIMSTEP

#ifndef TARGET_N64
void modelTickAnim(struct Model *model, s32 numticks, s32 update_chrstuff)
{
    modelTickAnimCore(model, numticks, 0.0f, update_chrstuff);
}

/**
 * Advances animation by a fractional number of ticks (whole ticks exactly as
 * modelTickAnim). Call with portAnimTicks() so vanilla play and demo playback
 * stay identical.
 */
void modelTickAnimF(struct Model *model, f32 ticks, s32 update_chrstuff)
{
    s32 whole;
    f32 frac;

    if (!(ticks > 0.0f))
    {
        return;
    }

    whole = (s32) ticks;
    frac = ticks - (f32) whole;

    modelTickAnimCore(model, whole, frac, update_chrstuff);
}
#endif


/**
 * @brief Model Type 1: 1Cycle No Secondary
 * @param[in,out] renderdata append cycle, CC and RM to display List
 */
void modelApplyRenderModeType1(ModelRenderData *renderdata)
{
    gDPPipeSync(renderdata->gdl++);
    gDPSetCycleType(renderdata->gdl++, G_CYC_1CYCLE);

    if (renderdata->zbufferenabled)
    {
        gDPSetRenderMode(renderdata->gdl++, G_RM_AA_ZB_OPA_SURF2, G_RM_AA_ZB_OPA_SURF);
    }
    else
    {
        gDPSetRenderMode(renderdata->gdl++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    }

    gDPSetCombineMode(renderdata->gdl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
}

/**
 * @brief Model Type 3: GunLighting - Reduced Secondary Commands (guns)
    This Type Uses Vertex Alpha for Secondary Surfaces and uses the FOG Alpha value for applying Fog/"Lighting".
 * @param renderdata
 * @param isPrimary
 */
void modelApplyRenderModeType3(ModelRenderData *renderdata, bool isPrimary)
{
    if (renderdata->PropType == PROP_TYPE_VIEWER+1)
    {
        if (isPrimary)
        {
            u8 r, g, b, a;
            gDPPipeSync(renderdata->gdl++);
            gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);

            r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
            g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
            b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
            a = _SHIFTR(renderdata->fogcolour.word, 0, 8);
            gDPSetFogColor(renderdata->gdl++, r, g, b, a);

            r = _SHIFTR(renderdata->envcolour.word, 24, 8);
            g = _SHIFTR(renderdata->envcolour.word, 16, 8);
            b = _SHIFTR(renderdata->envcolour.word, 8, 8);
            a = 0xFF;
            gDPSetEnvColor(renderdata->gdl++, r, g, b, a);

            gDPSetCombineLERP(renderdata->gdl++, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, ENVIRONMENT, SHADE, ENVIRONMENT, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);

            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
            }
        }
        else
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
        }
    }
    else if (renderdata->PropType == PROP_TYPE_EXPLOSION+1)
    {
        if (isPrimary)
        {
            u8 r, g, b, a;
            gDPPipeSync(renderdata->gdl++);
            gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);

            r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
            g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
            b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
            a = _SHIFTR(renderdata->fogcolour.word, 0, 8);
            gDPSetFogColor(renderdata->gdl++, r, g, b, a);

            r = _SHIFTR(renderdata->envcolour.word, 24, 8);
            g = _SHIFTR(renderdata->envcolour.word, 16, 8);
            b = _SHIFTR(renderdata->envcolour.word, 8, 8);
            a = _SHIFTR(renderdata->envcolour.word, 0, 8);
            gDPSetEnvColor(renderdata->gdl++, r, g, b, a);

            gDPSetCombineLERP(renderdata->gdl++, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, 0, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);

            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
        }
    }
    else if (renderdata->PropType == PROP_TYPE_SMOKE+1)
    {
        if ((renderdata->envcolour.word & 0xFF) == 0)
        {
            if (isPrimary)
            {
                u8 r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
                u8 g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
                u8 b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
                u8 a = _SHIFTR(renderdata->fogcolour.word, 0, 8);

                gDPPipeSync(renderdata->gdl++);
                gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
                gDPSetFogColor(renderdata->gdl++, r, g, b, a);
                gDPSetEnvColor(renderdata->gdl++, 0xFF, 0xFF, 0xFF, 0xFF);
                gDPSetPrimColor(renderdata->gdl++, 0, 0, 0, 0, 0, (renderdata->envcolour.word >> 8) & 0xFF);

                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, PRIMITIVE);

                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
                }
            }
            else
            {
                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
                }
            }
        }
        else
        {
            if (isPrimary)
            {
                u8 r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
                u8 g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
                u8 b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
                u8 a = _SHIFTR(renderdata->fogcolour.word, 0, 8);

                gDPPipeSync(renderdata->gdl++);
                gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
                gDPSetFogColor(renderdata->gdl++, r, g, b, a);
                gDPSetEnvColor(renderdata->gdl++, 0, 0, 0, renderdata->envcolour.word & 0xFF);

                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, SHADE, ENVIRONMENT, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);

                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_TEX_EDGE2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_TEX_EDGE2);
                }
            }
            else
            {
                gDPSetPrimColor(renderdata->gdl++, 0, 0, 0, 0, 0, (renderdata->envcolour.word >> 8) & 0xFF);
                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, SHADE, ENVIRONMENT, TEXEL0, 0, COMBINED, 0, SHADE, 0, 1, 0, PRIMITIVE, COMBINED);

                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_TEX_EDGE2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_TEX_EDGE2);
                }
            }
        }
    }
    else if (renderdata->PropType == PROP_TYPE_CHR+1)
    {
        if (isPrimary)
        {
            u8 r = _SHIFTR(renderdata->envcolour.word, 24, 8);
            u8 g = _SHIFTR(renderdata->envcolour.word, 16, 8);
            u8 b = _SHIFTR(renderdata->envcolour.word, 8, 8);
            u8 a = _SHIFTR(renderdata->envcolour.word, 0, 8);

            gDPPipeSync(renderdata->gdl++);
            gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
            gDPSetFogColor(renderdata->gdl++, r, g, b, a);

            gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);

            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
            }
        }
        else
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
        }
    }
    else if (renderdata->PropType == PROP_TYPE_WEAPON+1)
    {
        u8 r, g, b, a;
        if (isPrimary)
        {
            gDPPipeSync(renderdata->gdl++);
            gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);

            r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
            g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
            b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
            a = _SHIFTR(renderdata->fogcolour.word, 0, 8);
            gDPSetFogColor(renderdata->gdl++, r, g, b, a);

            a = renderdata->envcolour.word & 0xFF;

            if (a < 255)
            {
                gDPSetEnvColor(renderdata->gdl++, 0xFF, 0xFF, 0xFF, a);

                if (renderdata->envcolour.word & 0xFF00)
                {
                    gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, SHADE, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
                }
                else
                {
                    gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
                }
            }
            else
            {
                gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);
            }

            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
        }
        else
        {
            a = renderdata->envcolour.word & 0xFF;

            if (a < 255)
            {
                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL0, 0, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
            }
            else
            {
                gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);
            }
        }
    }
    else
    {
        if (isPrimary)
        {
            gDPPipeSync(renderdata->gdl++);
            gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
            gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);

            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_PASS, G_RM_AA_OPA_SURF2);
            }
        }
        else
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_PASS, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_PASS, G_RM_AA_XLU_SURF2);
            }
        }
    }
}

/**
 * @brief Model Type 4: Normal Fog/Lighting object
    This Type Uses Vertex Alpha for Secondary Surfaces and uses the FOG Alpha value for applying Fog/"Lighting".
 * @param renderdata
 * @param isPrimary Type of DisplayList
 */
void modelApplyRenderModeType4(ModelRenderData *renderdata, bool isPrimary)
{
    if (renderdata->PropType == PROP_TYPE_VIEWER+1)
    {
        u8 r, g, b, a;
        gDPPipeSync(renderdata->gdl++);
        gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);

        r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
        g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
        b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
        a = _SHIFTR(renderdata->fogcolour.word, 0, 8);
        gDPSetFogColor(renderdata->gdl++, r, g, b, a);

        r = _SHIFTR(renderdata->envcolour.word, 24, 8);
        g = _SHIFTR(renderdata->envcolour.word, 16, 8);
        b = _SHIFTR(renderdata->envcolour.word, 8, 8);
        a = 0xFF;
        gDPSetEnvColor(renderdata->gdl++, r, g, b, a);

        gDPSetCombineLERP(renderdata->gdl++, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, ENVIRONMENT, SHADE, ENVIRONMENT, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);

        if (isPrimary)
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
            }
        }
        else
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
        }
    }
    else if (renderdata->PropType == PROP_TYPE_EXPLOSION+1)
    {
        u8 r, g, b, a;
        gDPPipeSync(renderdata->gdl++);
        gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);

        r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
        g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
        b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
        a = _SHIFTR(renderdata->fogcolour.word, 0, 8);
        gDPSetFogColor(renderdata->gdl++, r, g, b, a);

        r = _SHIFTR(renderdata->envcolour.word, 24, 8);
        g = _SHIFTR(renderdata->envcolour.word, 16, 8);
        b = _SHIFTR(renderdata->envcolour.word, 8, 8);
        a = _SHIFTR(renderdata->envcolour.word, 0, 8);
        gDPSetEnvColor(renderdata->gdl++, r, g, b, a);

        gDPSetCombineLERP(renderdata->gdl++, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, 0, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);

        if (renderdata->zbufferenabled)
        {
            gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
        }
        else
        {
            gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
        }
    }
    else if (renderdata->PropType == PROP_TYPE_SMOKE+1)
    {
        if ((renderdata->envcolour.word & 0xFF) == 0)
        {
            u8 r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
            u8 g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
            u8 b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
            u8 a = _SHIFTR(renderdata->fogcolour.word, 0, 8);

            gDPPipeSync(renderdata->gdl++);
            gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
            gDPSetFogColor(renderdata->gdl++, r, g, b, a);
            gDPSetEnvColor(renderdata->gdl++, 0xFF, 0xFF, 0xFF, 0xFF);
            gDPSetPrimColor(renderdata->gdl++, 0, 0, 0, 0, 0, ((renderdata->envcolour.word >> 8 ) & 0xFF));

            if (isPrimary)
            {
                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, PRIMITIVE);

                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
                }
            }
            else
            {
                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, PRIMITIVE);

                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
                }
            }
        }
        else
        {
            u8 r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
            u8 g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
            u8 b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
            u8 a = _SHIFTR(renderdata->fogcolour.word, 0, 8);

            gDPPipeSync(renderdata->gdl++);
            gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
            gDPSetFogColor(renderdata->gdl++, r, g, b, a);
            gDPSetEnvColor(renderdata->gdl++, 0, 0, 0, renderdata->envcolour.word & 0xFF);

            if (isPrimary)
            {
                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, SHADE, ENVIRONMENT, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);

                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_TEX_EDGE2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_TEX_EDGE2);
                }
            }
            else
            {
                gDPSetPrimColor(renderdata->gdl++, 0, 0, 0, 0, 0, (renderdata->envcolour.word >> 8) & 0xFF);
                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, SHADE, ENVIRONMENT, TEXEL0, 0, COMBINED, 0, SHADE, 0, 1, 0, PRIMITIVE, COMBINED);

                if (renderdata->zbufferenabled)
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_TEX_EDGE2);
                }
                else
                {
                    gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_TEX_EDGE2);
                }
            }
        }
    }
    else if (renderdata->PropType == PROP_TYPE_CHR+1)
    {
        u8 r = _SHIFTR(renderdata->envcolour.word, 24, 8);
        u8 g = _SHIFTR(renderdata->envcolour.word, 16, 8);
        u8 b = _SHIFTR(renderdata->envcolour.word, 8, 8);
        u8 a = _SHIFTR(renderdata->envcolour.word, 0, 8);

        gDPPipeSync(renderdata->gdl++);
        gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
        gDPSetFogColor(renderdata->gdl++, r, g, b, a);

        gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);

        if (isPrimary)
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
            }
        }
        else
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
        }
    }
    else if (renderdata->PropType == PROP_TYPE_WEAPON+1)
    {
        u8 r, g, b, a;

        gDPPipeSync(renderdata->gdl++);
        gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);

        r = _SHIFTR(renderdata->fogcolour.word, 24, 8);
        g = _SHIFTR(renderdata->fogcolour.word, 16, 8);
        b = _SHIFTR(renderdata->fogcolour.word, 8, 8);
        a = _SHIFTR(renderdata->fogcolour.word, 0, 8);
        gDPSetFogColor(renderdata->gdl++, r, g, b, a);

        a = renderdata->envcolour.word & 0xFF;

        if (a < 255)
        {
            gDPSetEnvColor(renderdata->gdl++, 0xFF, 0xFF, 0xFF, a);

            if (isPrimary)
            {
                if (renderdata->envcolour.word & 0xFF00) //apply inverse vertex alpha if any
                {
                    gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, SHADE, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
                }
                else
                {
                    gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
                }
            }
            else
            {
                gDPSetCombineLERP(renderdata->gdl++, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL0, 0, ENVIRONMENT, 0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
            }
        }
        else
        {
            gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);
        }

        if (renderdata->zbufferenabled)
        {
            gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
        }
        else
        {
            gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
        }
    }
    else
    {
        gDPPipeSync(renderdata->gdl++);
        gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);
        gDPSetFogColor(renderdata->gdl++, 0xFF, 0xFF, 0xFF, 0x00);
        gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);

        if (isPrimary)
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
            }
        }
        else
        {
            if (renderdata->zbufferenabled)
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            }
            else
            {
                gDPSetRenderMode(renderdata->gdl++, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
        }
    }
}

/**
 * @brief Model Type 2: 2Cycle No Secondary
 * @param[in,out] renderdata append cycle, CC and RM to display List
 */
void modelApplyRenderModeType2(ModelRenderData *renderdata)
{
    gDPPipeSync(renderdata->gdl++);
    gDPSetCycleType(renderdata->gdl++, G_CYC_2CYCLE);

    if (renderdata->zbufferenabled)
    {
        gDPSetRenderMode(renderdata->gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
    }
    else
    {
        gDPSetRenderMode(renderdata->gdl++, G_RM_PASS, G_RM_AA_OPA_SURF2);
    }

    gDPSetCombineMode(renderdata->gdl++, G_CC_TRILERP, G_CC_MODULATEIA2);
}


void modelApplyCullMode(ModelRenderData *renderdata)
{
    if (renderdata->cullmode == CULLMODE_NONE)
    {
        gSPClearGeometryMode(renderdata->gdl++, G_CULL_BOTH);
    }
    else if (renderdata->cullmode == CULLMODE_FRONT)
    {
        gSPSetGeometryMode(renderdata->gdl++, G_CULL_FRONT);
    }
    else if (renderdata->cullmode == CULLMODE_BACK)
    {
        gSPSetGeometryMode(renderdata->gdl++, G_CULL_BACK);
    }
}


#ifndef TARGET_N64
/* port (GPU-retained model parts, port/fast3d/gfx_roomcache.inl): the next
 * G_DL is a part list whose vertices load through segment vtxseg (roc_hooks.c) */
extern Gfx *portPartCacheEmit(Gfx *gdl, s32 vtxseg);
#endif
void modelRenderNodeGundl(ModelRenderData* renderdata, ModelNode* arg1)
{
    ModelRoData_DisplayListRecord* rodata = &arg1->Data->DisplayList;

    if (renderdata->unk18 == 0)
    {
        if ((renderdata->flags & 1) && rodata->Primary)
        {
            gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(rodata->BaseAddr));

            if (renderdata->cullmode)
            {
                modelApplyCullMode(renderdata);
            }

            if (rodata->ModelType == 1)
            {
                modelApplyRenderModeType1(renderdata);
            }
            else if (rodata->ModelType == 3)
            {
                modelApplyRenderModeType3(renderdata, 1);
            }
            else if (rodata->ModelType == 4)
            {
                modelApplyRenderModeType4(renderdata, 1);
            }
            else if (rodata->ModelType == 2)
            {
                modelApplyRenderModeType2(renderdata);
            }

#ifndef TARGET_N64
            /* the gun-DL node's lists address their vertices through SPSEGMENT_MODEL_COL1 = the model's base */
            renderdata->gdl = portPartCacheEmit(renderdata->gdl, SPSEGMENT_MODEL_COL1);
#endif
            gSPDisplayList(renderdata->gdl++, rodata->Primary);

            if ((rodata->ModelType == 3) && rodata->Secondary)
            {
                modelApplyRenderModeType3(renderdata, 0);
#ifndef TARGET_N64
                renderdata->gdl = portPartCacheEmit(renderdata->gdl, SPSEGMENT_MODEL_COL1);
#endif
                gSPDisplayList(renderdata->gdl++, rodata->Secondary);
            }
        }

        if ((renderdata->flags & 2) && rodata->Primary && (rodata->ModelType == 4) && rodata->Secondary)
        {
            gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(rodata->BaseAddr));

            if (renderdata->cullmode)
            {
                modelApplyCullMode(renderdata);
            }

            modelApplyRenderModeType4(renderdata, 0);
#ifndef TARGET_N64
            renderdata->gdl = portPartCacheEmit(renderdata->gdl, SPSEGMENT_MODEL_COL1);
#endif
            gSPDisplayList(renderdata->gdl++, rodata->Secondary);
        }
    }
}

/*

A1 is primary = 1, secondary = 0
Inside the T8 or whatever temporary register indicates gun or not gun (0 = gun, or UseZ = 1), for different render mode

Bool UseZ //guns = false
Bool


Model Type 0: NoSetup.
    type 0 Has No DL Setup and will use whaterver is currently set.

Model Type 1: 1Cycle No Sec
    E700000000000000 pipesync()
    BA00140200000000 CycleType(1c)
    if UseZ
      B900031D00552078 SetRendermode(AA_ZB_OPA_1) //cin ain cmem amem
    else
      B900031D00552048 SetRendermode(AA_OPA_1) //cin ain cmem amem
    end if
    FC121824FF33FFFF SetCombine(MODULATERGBA)
    No Secondary

Model Type 2: 2Cycle No Sec
    E700000000000000 pipesync()
    BA00140200100000 CycleType(2c)
    if UseZ
      B900031D0C192078 SetRendermode(AA_ZB_OPA_2) // cin 0 cin 1 //colour only
    else
      B900031D0C192048 SetRendermode(AA_OPA_2) // cin 0 cin 1 //colour only
    end if
    FC26A0041F1093FF SetCombine(TRILERP, MODULATERGBA)
    No Secondary
Model Type 3: GunLighting - Reduced Secondary Commands (guns)
    This Type Uses Vertex Alpha for Secondary Surfaces and uses the FOG Alpha value for applying Fog/"Lighting".
    Primary
    E700000000000000 pipesync()
    BA00140200100000 CycleType(2c)
    F800000000000026 SetFogColor(0,0,0,38)
    if dltype = full
      if guard
        FB0000005A0000FF SetEnvColor(90,0,0,255)
        FC1598045FFEDBF8 SetCombine(((Texel0-Env)*ShadeA+Env)
                         ((Texel0-Env)*Shade+Env),
                         MODULATERGB_DECALA)
      else if prop
        FB000000FFFFFFFF SetEnvColor(255,255,255,255)
        FA00000000000000 SetPrimColor(0,0,0,0)
        FC26A0041F1093FB SetCombine(TRILERP, MODULATERGB_ADDPRIM_A)
      endif
    else
      FC26A0041F1093FF SetCombine(TRILERP, MODULATERGBA)
    endif
    if UseZ
      B900031DC4112078 SetRendermode(AA_ZB_OPA_StanFOG_2)
    else
      B900031DC4112048 SetRendermode(AA_OPA_StanFOG_2) //acvg
    endif

    Secondary
    if UseZ
      B900031DC41049D8 SetRendermode(AA_Zcmp_XLU_StanFOG_2)
    else
      B900031DC41041C8 SetRendermode(AA_OPA_StanFOG_2)//FcBl ClrOnCvg
    endif

Model Type 4: Normal Fog/Lighting object
    This Type Uses Vertex Alpha for Secondary Surfaces and uses the FOG Alpha value for applying Fog/"Lighting".
    Primary
    E700000000000000 pipesync()
    BA00140200100000 CycleType(2c)
    F800000000000026 SetFogColor(0,0,0,38)
    if dltype = full
      if guard
        FB0000005A0000FF SetEnvColor(90,0,0,255)
        FC1598045FFEDBF8 SetCombine(((Texel0-Env)*ShadeA+Env)
                         ((Texel0-Env)*Shade+Env),
                         MODULATERGB_DECALA)
      else if prop
        FB000000FFFFFFFF SetEnvColor(255,255,255,255)
        FA00000000000000 SetPrimColor(0,0,0,0)
        FC26A0041F1093FB SetCombine(TRILERP, MODULATERGB_ADDPRIM_A)
      endif
    else
      FC26A0041F1093FF SetCombine(TRILERP, MODULATERGBA)
    endif
    if UseZ
      B900031DC4112078 SetRendermode(AA_ZB_OPA_StanFOG_2)
    else
      B900031DC4112048 SetRendermode(AA_OPA_StanFOG_2) //acvg
    endif

    Secondary
    E700000000000000 pipesync()
    BA00140200100000 CycleType(2c)
    F800000000000026 SetFogColor(0,0,0,38)
    if dltype = full
      if guard
        FB0000005A0000FF SetEnvColor(90,0,0,255)
        FC1598045FFEDBF8 SetCombine(((Texel0-Env)*ShadeA+Env)
                         ((Texel0-Env)*Shade+Env),
                         MODULATERGB_DECALA)
      else if prop
        FB000000FFFFFFFF SetEnvColor(255,255,255,255)
        FA00000000000000 SetPrimColor(0,0,0,0)
        FC26A0041F1093FB SetCombine(TRILERP, MODULATERGB_ADDPRIM_A)
      endif
    else
      FA00000000000000 SetPrimColor(0,0,0,0)
      FC26A0041F1093FB SetCombine(TRILERP, MODULATERGB_ADDPRIM_A)
    endif
    if UseZ
      B900031DC41049D8 SetRendermode(AA_Zcmp_XLU_StanFOG_2)
    else
      B900031DC41041C8 SetRendermode(AA_OPA_StanFOG_2)//FcBl ClrOnCvg
    endif
*/

/**
* 7F072A0C
* DisplayList Setups Depend on Object Type, Prop Guard or Gun.
These are applied to each part of an object at runtime and can be overridden. loading the next part will use these values once more.
GeometryMode is not in setup and is persistent accross parts.
*/
void modelRenderNodeDl(ModelRenderData *renderdata, Model *model, ModelNode *node)
{
    union ModelRoData *rodata = node->Data;

#ifndef TARGET_N64
    { /* GE_PARTDBG=1: logs each DLCOLLISION render visit (which pass, and whether the
       * part's primary/secondary DLs are live), up to a cap. */
        extern char *getenv(const char *);
        static s32 pd = -1;
        static s32 pdn = 0;
        if (pd < 0) { pd = getenv("GE_PARTDBG") != NULL; }
        if (pd && pdn < 400)
        {
            union ModelRwData *rw = modelGetNodeRwData(model, node);
            pdn++;
            printf("partdbg: node=%p ro=%p type=%d flags=%x unk18=%d prim=%p sec=%p rwgdl=%p rwvtx=%p rovtx=%p\n",
                    node, rodata, rodata->DisplayListCollisions.ModelType,
                    renderdata->flags, renderdata->unk18,
                    rodata->DisplayListCollisions.Primary,
                    rodata->DisplayListCollisions.Secondary,
                    rw->DisplayListCollisions.gdl,
                    rw->DisplayListCollisions.Vertices,
                    rodata->DisplayListCollisions.Vertices);
        }
    }
#endif

    if (!renderdata->unk18)
    {
        if (renderdata->flags & 1)
        {
            union ModelRwData *rwdata = modelGetNodeRwData(model, node);

            if (rwdata->DisplayListCollisions.gdl)
            {
#ifndef TARGET_N64
                { /* bring-up trace: DL nodes with a live gdl (GE_GFX_STATS) */
                    extern u32 ge_dbg2;
                    ge_dbg2++;
                }
#endif
                gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(rodata->DisplayListCollisions.BaseAddr));

                if (renderdata->cullmode)
                {
                    modelApplyCullMode(renderdata);
                }

                if (rodata->DisplayListCollisions.ModelType == 1)
                {
                    modelApplyRenderModeType1(renderdata);
                }
                else if (rodata->DisplayListCollisions.ModelType == 3)
                {
                    modelApplyRenderModeType3(renderdata, TRUE);
                }
                else if (rodata->DisplayListCollisions.ModelType == 4)
                {
                    modelApplyRenderModeType4(renderdata, TRUE);
                }
                else if (rodata->DisplayListCollisions.ModelType == 2)
                {
                    modelApplyRenderModeType2(renderdata);
                }

                gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_VTX, osVirtualToPhysical(rwdata->DisplayListCollisions.Vertices));
#ifndef TARGET_N64
                renderdata->gdl = portPartCacheEmit(renderdata->gdl, SPSEGMENT_MODEL_VTX);
#endif

                gSPDisplayList(renderdata->gdl++, rwdata->DisplayListCollisions.gdl);

                if (rodata->DisplayListCollisions.ModelType == 3 && rodata->DisplayListCollisions.Secondary)
                {
                    modelApplyRenderModeType3(renderdata, FALSE);
#ifndef TARGET_N64
                    renderdata->gdl = portPartCacheEmit(renderdata->gdl, SPSEGMENT_MODEL_VTX);
#endif
                    gSPDisplayList(renderdata->gdl++, rodata->DisplayListCollisions.Secondary);
                }
            }
        }

        if (renderdata->flags & 2)
        {
            union ModelRwData *rwdata = modelGetNodeRwData(model, node);

            if (rwdata->DisplayListCollisions.gdl && rodata->DisplayListCollisions.ModelType == 4 && rodata->DisplayListCollisions.Secondary)
            {
                gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(rodata->DisplayListCollisions.BaseAddr));

                if (renderdata->cullmode)
                {
                    modelApplyCullMode(renderdata);
                }

                gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_VTX, osVirtualToPhysical(rwdata->DisplayListCollisions.Vertices));

                modelApplyRenderModeType4(renderdata, FALSE);
#ifndef TARGET_N64
                renderdata->gdl = portPartCacheEmit(renderdata->gdl, SPSEGMENT_MODEL_VTX);
#endif

                gSPDisplayList(renderdata->gdl++, rodata->DisplayListCollisions.Secondary);
            }
        }
    }
}


void sub_GAME_7F072C10(ModelRenderData *param_1, struct Model *param_2, struct ModelNode *param_3)
{
    return;
}


/**
 * Star gunfire is a muzzle flash in a first person perspective, where the
 * muzzle flash has 3 or 4 "arms" that flare out from the main body.
 *
 * This function reads vertices from the model definition, tweaks them randomly,
 * writes them to a newly allocated vertices table and queues the node's
 * displaylist to the renderdata's DL.
 */
void dorottex(ModelRenderData *renderdata, ModelNode *node)
{
    if (renderdata->unk18 == 0 && (renderdata->flags & 2))
    {

        ModelRoData_DisplayListPrimaryRecord *rodata = &node->Data->DisplayListPrimary;
        s32 i;

        if (rodata->Primary)
        {
            Vertex *src;
            Vertex *dst;

            src = (Vertex *) rodata->Vertices;

#ifndef VERSION_EU
            if (vtxallocator != NULL)
            {
            }
            else
            {
                osSyncPrintf("dorottex: no vtx allocator!\n");
                return_null();
            }
#endif
            dst = vtxallocator(rodata->numVertices * 4);

            gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_VTX, osVirtualToPhysical(dst));
            gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(rodata->BaseAddr));

            gDPSetFogColor(renderdata->gdl++, 0x00, 0x00, 0x00, 0x00);
            gSPDisplayList(renderdata->gdl++, rodata->Primary);

            for (i = 0; i < rodata->numVertices; i++)
            {
                u16 rand1 = (randomGetNext() << 10) & 0xffff;
                s32 s4 = ((coss(rand1) << 5) * 181) >> 18;
                s32 s3 = ((sins(rand1) << 5) * 181) >> 18;
                s32 s1 = (u32)randomGetNext() >> 31;
                s32 mult = 0x10000 - (randomGetNext() & 0x3fff);
                s32 corner1 = 0x200 + s3;
                s32 corner2 = 0x200 - s3;
                s32 corner3 = 0x200 - s4;
                s32 corner4 = 0x200 + s4;

                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];

                dst[0].s = corner3;
                dst[0].t = corner2;
                dst[0].coord.x = (src[(s1 + 0) % 4].coord.x * mult) >> 16;
                dst[0].coord.y = (src[(s1 + 0) % 4].coord.y * mult) >> 16;
                dst[0].coord.z = (src[(s1 + 0) % 4].coord.z * mult) >> 16;

                dst[1].s = corner1;
                dst[1].t = corner3;
                dst[1].coord.x = (src[(s1 + 1) % 4].coord.x * mult) >> 16;
                dst[1].coord.y = (src[(s1 + 1) % 4].coord.y * mult) >> 16;
                dst[1].coord.z = (src[(s1 + 1) % 4].coord.z * mult) >> 16;

                dst[2].s = corner4;
                dst[2].t = corner1;
                dst[2].coord.x = (src[(s1 + 2) % 4].coord.x * mult) >> 16;
                dst[2].coord.y = (src[(s1 + 2) % 4].coord.y * mult) >> 16;
                dst[2].coord.z = (src[(s1 + 2) % 4].coord.z * mult) >> 16;

                dst[3].s = corner2;
                dst[3].t = corner4;
                dst[3].coord.x = (src[(s1 + 3) % 4].coord.x * mult) >> 16;
                dst[3].coord.y = (src[(s1 + 3) % 4].coord.y * mult) >> 16;
                dst[3].coord.z = (src[(s1 + 3) % 4].coord.z * mult) >> 16;

                src += 4;
                dst += 4;
            }
        }
    }
}


void sub_GAME_7F073038(ModelRenderData *renderdata, struct sImageTableEntry *tconfig, s32 arg2)
{
    texSelect(&renderdata->gdl, tconfig, arg2, renderdata->zbufferenabled, 2);
}


void sub_GAME_7F07306C(s32 param_1,struct Model *param_2,struct ModelNode *param_3)
{
    return;
}


void dotube(ModelRenderData* renderdata, Model* model, ModelNode* node)
{
    s32 rw_index_sel;
    s32 rw_index_sel2;
    s32 c_entry_count;
    struct ModelRoData_Op07Record *rodata2;
    s32 c_entry2_count;
    s32 c_entry_index;
    s32 renderpos_index;
    ModelNode *node_from_07;
    struct ModelRoData_Child *c_entry2;
    Vertex *vtx2;
    u8 *entry2_04;
    s32 unused1;
    bool swap_order;
    struct ModelRoData_Op07Record *rodata;
    struct ModelRoData_Child *c_entry;
    struct ModelRwData_Op07Record *rwdata;
    Vertex *vtx_10;
    Vertex *vtx_10_2;
    RenderPosView *render_pos2;
    RenderPosView *render_pos;
    s32 rw_index2;
    s32 rw_index;
    Vertex *vtx1;
    struct ModelRwData_Op07Record *rwdata2;
    u8 *entry_04;
    s32 unused2;
    s32 renderpos_index2;
    s32 unused3;

    rodata = &node->Data->Op07;
    rwdata = &modelGetNodeRwData(model, node)->Op07;

    if (rodata->unk00 != NULL)
    {
        node_from_07 = rodata->unk00;
    }
    else
    {
        node_from_07 = rodata->unk04;
    }

    rodata2 = &node_from_07->Data->Op07;
    rwdata2 = &modelGetNodeRwData(model, node_from_07)->Op07;
    swap_order = 1;

    if (renderdata->flags & 1)
    {
        renderpos_index2 = modelFindNodeMtxIndex(node, 0);
        render_pos2 = &model->render_pos[renderpos_index2];
        rw_index = rwdata->index;
        rw_index2 = rwdata2->index;
        c_entry = &rodata->Children[rw_index];

        if (rodata->unk00 != NULL)
        {
            rw_index_sel = rw_index2;
            rw_index_sel2 = rw_index;
            renderpos_index = modelFindNodeMtxIndex(node, 0x200);
        }
        else
        {
            rw_index_sel = rw_index;
            rw_index_sel2 = rw_index2;
            renderpos_index = modelFindNodeMtxIndex(rodata->unk04, 0x200);
        }

        render_pos = &model->render_pos[renderpos_index];

        c_entry_index = ((rw_index_sel2 - rw_index_sel) + rodata->NumChildren) % rodata->NumChildren;

        if ((c_entry_index >= 2) && (c_entry_index < 7))
        {
            if (c_entry_index < 4)
            {
                c_entry_index = ((c_entry_index / 2) + rw_index_sel + rodata->NumChildren) % rodata->NumChildren;
            }
            else
            {
                c_entry_index = ((rw_index_sel - ((8 - c_entry_index) / 2)) + rodata->NumChildren) % rodata->NumChildren;
                swap_order = 0;
            }
        }
        else if ((c_entry_index >= 0xA) && (c_entry_index < 0xF))
        {
            if (c_entry_index >= 0xD)
            {
                c_entry_index = ((rw_index_sel - ((0x10 - c_entry_index) / 2)) + rodata->NumChildren) % rodata->NumChildren;
            }
            else
            {
                c_entry_index = (((c_entry_index - 8) / 2) + rw_index_sel + rodata->NumChildren) % rodata->NumChildren;
                swap_order = 0;
            }
        }
        else
        {
            if ((c_entry_index >= 7) && (c_entry_index < 0xA))
            {
                swap_order = 0;
            }
            c_entry_index = rw_index_sel;
        }

        entry_04 = c_entry->unk04;

        gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(rodata->BaseAddr));

        for (c_entry_count = c_entry->NumEntries; c_entry_count > 0; c_entry_count--)
        {
            switch (*entry_04)
            {
                case MODELNODE_CHILD_VTX:
                    {
                        struct ModelRoData_Child_Vtx* child_vtx = ((struct ModelRoData_Child_Vtx*)entry_04);
#if defined(LEFTOVERDEBUG)
                        if (vtxallocator == NULL)
                        {
                            osSyncPrintf("dotube: no vtx allocator!\n");
                            return_null();
                        }
#endif
                        vtx1 = vtxallocator(2);
                        vtx2 = &vtx1[1];

                        *vtx1 = rodata->Vertices[child_vtx->VtxIndex];
                        *vtx2 = rodata->Vertices[child_vtx->VtxIndex+1];

                        if (rodata->unk04 != NULL)
                        {
                            c_entry2 = &rodata->Children[c_entry_index];
                            entry2_04 = c_entry2->unk04;

                            for (c_entry2_count = c_entry2->NumEntries; c_entry2_count > 0; c_entry2_count--)
                            {
                                struct ModelRoData_Child_Vtx* entry2_04_child = ((struct ModelRoData_Child_Vtx*)entry2_04);
                                if (entry2_04_child->Type == (u8) 1) {
                                    vtx_10   = &rodata->Vertices[entry2_04_child->VtxIndex];
                                    vtx_10_2 = vtx_10+1;

                                    vtx1->coord.AsArray[0] = vtx_10->coord.AsArray[0];
                                    vtx1->coord.AsArray[1] = vtx_10->coord.AsArray[1];
                                    vtx1->coord.AsArray[2] = vtx_10->coord.AsArray[2];

                                    vtx2->coord.AsArray[0] = vtx_10_2->coord.AsArray[0];
                                    vtx2->coord.AsArray[1] = vtx_10_2->coord.AsArray[1];
                                    vtx2->coord.AsArray[2] = vtx_10_2->coord.AsArray[2];
                                    break;
                                }

                                switch (*entry2_04)
                                {
                                    case MODELNODE_CHILD_VTX:
                                        entry2_04 += sizeof(struct ModelRoData_Child_Vtx);
                                        break;
                                    case MODELNODE_CHILD_IMAGE:
                                        entry2_04 += sizeof(struct ModelRoData_Child_Image);
                                        break;
                                    case MODELNODE_CHILD_TRI:
                                        entry2_04 += sizeof(struct ModelRoData_Child_Tri);
                                        break;
                                }
                            }

                        }
                        else
                        {
                            c_entry2 = &rodata2->Children[c_entry_index];
                            entry2_04 = c_entry2->unk04;

                            for (c_entry2_count = c_entry2->NumEntries; c_entry2_count > 0; c_entry2_count--)
                            {
                                struct ModelRoData_Child_Vtx* entry2_04_child = ((struct ModelRoData_Child_Vtx*)entry2_04);
                                if (entry2_04_child->Type == (u8) 1)
                                {
                                    vtx_10   = &rodata2->Vertices[entry2_04_child->VtxIndex];
                                    vtx_10_2 = vtx_10 + 1;
                                    if (swap_order != 0)
                                    {
#if defined(LEFTOVERDEBUG)
                                        if (vtx_10->coord.AsArray);
#endif
                                        vtx1->coord.AsArray[0] = vtx_10_2->coord.AsArray[0];
                                        vtx1->coord.AsArray[1] = vtx_10_2->coord.AsArray[1];
                                        vtx1->coord.AsArray[2] = vtx_10_2->coord.AsArray[2];

                                        vtx2->coord.AsArray[0] = vtx_10->coord.AsArray[0];
                                        vtx2->coord.AsArray[1] = vtx_10->coord.AsArray[1];
                                        vtx2->coord.AsArray[2] = vtx_10->coord.AsArray[2];
                                    }
                                    else
                                    {
                                        vtx1->coord.AsArray[0] = vtx_10->coord.AsArray[0];
                                        vtx1->coord.AsArray[1] = vtx_10->coord.AsArray[1];
                                        vtx1->coord.AsArray[2] = vtx_10->coord.AsArray[2];

                                        vtx2->coord.AsArray[0] = vtx_10_2->coord.AsArray[0];
                                        vtx2->coord.AsArray[1] = vtx_10_2->coord.AsArray[1];
                                        vtx2->coord.AsArray[2] = vtx_10_2->coord.AsArray[2];
                                    }
                                    break;
                                }

                                switch (*entry2_04)
                                {
                                    case MODELNODE_CHILD_VTX:
                                        entry2_04 += sizeof(struct ModelRoData_Child_Vtx);
                                        break;

                                    case MODELNODE_CHILD_IMAGE:
                                        entry2_04 += sizeof(struct ModelRoData_Child_Image);
                                        break;

                                    case MODELNODE_CHILD_TRI:
                                        entry2_04 += sizeof(struct ModelRoData_Child_Tri);
                                        break;
                                }
                            }
                        }

                        gSPMatrix(renderdata->gdl++, osVirtualToPhysical(render_pos), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                        gSPVertex(renderdata->gdl++, osVirtualToPhysical(vtx1), 2, 0);
                        gSPMatrix(renderdata->gdl++, osVirtualToPhysical(render_pos2), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                        gSPVertex(renderdata->gdl++, osVirtualToPhysical(&rodata->Vertices[child_vtx->VtxIndex] + 2), (u32)(child_vtx->unk01 - 2), 2);

                        entry_04 += sizeof(struct ModelRoData_Child_Vtx);

                        break;
                    }

                case MODELNODE_CHILD_IMAGE:
                    {
                        struct ModelRoData_Child_Image* child_image = (struct ModelRoData_Child_Image*)entry_04;
                        if (child_image->ImageIndex != 0xFF)
                        {
                            sub_GAME_7F073038(renderdata, &rodata->Images[child_image->ImageIndex], 1);
                            entry_04 += sizeof(struct ModelRoData_Child_Image);
                        }
                        else
                        {
                            sub_GAME_7F073038(renderdata, NULL, 1);
                            entry_04 += sizeof(struct ModelRoData_Child_Image);
                        }
                        break;
                    }

                case MODELNODE_CHILD_TRI:
                    {
                        struct ModelRoData_Child_Tri* child_tri = (struct ModelRoData_Child_Tri*)entry_04;
                        gSP1Triangle(renderdata->gdl++, child_tri->VtxIndex1, child_tri->VtxIndex2, child_tri->VtxIndex3, 0);
                        entry_04 += sizeof(struct ModelRoData_Child_Tri);
                        break;
                    }
            }
        }
    }
}


void sub_GAME_7F0737EC(s32 param_1,struct Model *param_2, struct ModelNode *param_3)
{
    return;
}


void sub_GAME_7F0737FC(s32 param_1,struct Model *param_2,struct ModelNode *param_3)
{
    return;
}


// PD: modelRenderNodeChrGunfire
void dogfnegx(ModelRenderData *renderdata, Model *model, ModelNode *node)
{
    u32 unused[3];
    f32 negspc0;
    ModelRoData_GunfireRecord *rodata = &node->Data->Gunfire;
    union ModelRwData *rwdata = modelGetNodeRwData(model, node);
    sImageTableEntry *tconfig;
    f32 spf0;
    f32 spec;
    coord3d spe0;
    f32 spdc;
    f32 spd8;
    f32 spd4;
    f32 spd0;
    f32 spcc;
    f32 spc8;
    f32 spc4;
    f32 spc0;
    f32 spbc;
    f32 negspcc;
    f32 negspc8;
    f32 scale;
    Mtxf *mtx;
    f32 tmp;
    coord3d sp9c;
    coord3d sp90;
    Vertex vtxtemplate = D_800363E0;
    Vertex *vertices;
    f32 distance;

    if ((renderdata->flags & 2) && rwdata->Gunfire.visible)
    {
        s32 index = modelFindNodeMtxIndex(node, 0);
        mtx = &model->render_pos[index].pos;

        spe0.x = -(rodata->Offset.f[0] * mtx->m[0][0] + rodata->Offset.f[1] * mtx->m[1][0] + rodata->Offset.f[2] * mtx->m[2][0] + mtx->m[3][0]);
        spe0.y = -(rodata->Offset.f[0] * mtx->m[0][1] + rodata->Offset.f[1] * mtx->m[1][1] + rodata->Offset.f[2] * mtx->m[2][1] + mtx->m[3][1]);
        spe0.z = -(rodata->Offset.f[0] * mtx->m[0][2] + rodata->Offset.f[1] * mtx->m[1][2] + rodata->Offset.f[2] * mtx->m[2][2] + mtx->m[3][2]);

        distance = sqrtf(spe0.f[0] * spe0.f[0] + spe0.f[1] * spe0.f[1] + spe0.f[2] * spe0.f[2]);

        if (distance > 0)
        {
            f32 tmp = 1 / (model->scale * distance);
            spe0.f[0] *= tmp;
            spe0.f[1] *= tmp;
            spe0.f[2] *= tmp;
        }
        else
        {
            spe0.f[0] = 0;
            spe0.f[1] = 0;
            spe0.f[2] = 1 / model->scale;
        }

        spec = acosf(spe0.f[0] * mtx->m[1][0] + spe0.f[1] * mtx->m[1][1] + spe0.f[2] * mtx->m[1][2]);
        spf0 = acosf(-(spe0.f[0] * mtx->m[2][0] + spe0.f[1] * mtx->m[2][1] + spe0.f[2] * mtx->m[2][2]) / sinf(spec));

        tmp = -(spe0.f[0] * mtx->m[0][0] + spe0.f[1] * mtx->m[0][1] + spe0.f[2] * mtx->m[0][2]);

        if (tmp < 0)
        {
            spf0 = M_TAU_F - spf0;
        }

        spdc = cosf(spf0);
        spd8 = sinf(spf0);
        spd4 = cosf(spec);
        spd0 = sinf(spec);

        scale = 0.75f + (randomGetNext() % 128) * (1.0f / 256.0f); // 0.75 to 1.25

        sp9c.f[0] = rodata->Size.f[0] * scale;
        sp9c.f[1] = rodata->Size.f[1] * scale;
        sp9c.f[2] = rodata->Size.f[2] * scale;

        spcc = sp9c.f[0] * spdc * 0.5f;
        spc8 = sp9c.f[2] * spd8 * 0.5f;
        spc4 = sp9c.f[1] * spd0 * 0.5f;

        spc0 = sp9c.f[0] * spd4 * spd8 * 0.5f;
        spbc = sp9c.f[2] * spd4 * spdc * 0.5f;

        negspcc = -spcc;
        negspc8 = -spc8;
        negspc0 = -spc0;

        sp90.f[0] = rodata->Offset.f[0] - sp9c.f[0] * 0.5f;
        sp90.f[1] = rodata->Offset.f[1];
        sp90.f[2] = rodata->Offset.f[2];

#if defined (LEFTOVERDEBUG)
        if (vtxallocator == NULL) {
            osSyncPrintf("dogfnegx: no vtx allocator!\n");
            return_null();
        }
#endif

        vertices = vtxallocator(4);

        vertices[0] = vtxtemplate;
        vertices[1] = vtxtemplate;
        vertices[2] = vtxtemplate;
        vertices[3] = vtxtemplate;

        vertices[0].coord.x = sp90.f[0] + negspcc + negspc0;
        vertices[0].coord.y = sp90.f[1] - spc4;
        vertices[0].coord.z = sp90.f[2] - negspc8 + -spbc;
        vertices[1].coord.x = sp90.f[0] + negspcc - negspc0;
        vertices[1].coord.y = sp90.f[1] + spc4;
        vertices[1].coord.z = sp90.f[2] - negspc8 - -spbc;
        vertices[2].coord.x = sp90.f[0] - negspcc - negspc0;
        vertices[2].coord.y = sp90.f[1] + spc4;
        vertices[2].coord.z = sp90.f[2] + negspc8 - -spbc;
        vertices[3].coord.x = sp90.f[0] - negspcc + negspc0;
        vertices[3].coord.y = sp90.f[1] - spc4;
        vertices[3].coord.z = sp90.f[2] + negspc8 + -spbc;

        gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(rodata->BaseAddr));

        if (rodata->Image)
        {
            s32 centre;
            u16 sp62;
            s32 sp5c;
            s32 sp58;

            tconfig = rodata->Image;

            sp62 = (randomGetNext() * 1024) & 0xffff;
            sp5c = (coss(sp62) * tconfig->width * 0xb5) >> 18;
            sp58 = (sins(sp62) * tconfig->width * 0xb5) >> 18;

            centre = tconfig->width << 4;

            vertices[0].s = centre - sp5c;
            vertices[0].t = centre - sp58;
            vertices[1].s = centre + sp58;
            vertices[1].t = centre - sp5c;
            vertices[2].s = centre + sp5c;
            vertices[2].t = centre + sp58;
            vertices[3].s = centre - sp58;
            vertices[3].t = centre + sp5c;

            sub_GAME_7F073038(renderdata, tconfig, 4);
        }
        else
        {
            sub_GAME_7F073038(renderdata, NULL, 1);
        }

        gSPSetGeometryMode(renderdata->gdl++, G_CULL_BACK);
        gSPMatrix(renderdata->gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(renderdata->gdl++, osVirtualToPhysical(vertices), 4, 0);
        if (1);
        gSP2Triangles(renderdata->gdl++, 0, 1, 2, 0, 2, 3, 0, 0);
    }
}


void sub_GAME_7F073FC8(s32 arg0)
{
#ifdef TARGET_N64
    D_800363F0 = arg0;
#else
    /* Every character shadow's intensity passes through here and is scaled by
     * midas.ini [display] shadow_alpha (percent of vanilla), because shadows look
     * darker on PC than on real hardware; GE_SHADOWALPHA=N overrides it. */
    extern s32 portShadowAlphaPct(void);
    s32 pct = portShadowAlphaPct();
    D_800363F0 = (arg0 * pct) / 100;
#endif
}

#ifndef TARGET_N64
/* The title-screen Bond-walk intro's shadow (title.c sets 80) is fixed at 15% of vanilla to match
 * real hardware, with no ini setting and no shadow_alpha scaling (80 * 15 / 100 = 12). */
#define PORT_INTRO_SHADOW_PCT 15
void portShadowIntensityIntro(s32 arg0)
{
    D_800363F0 = (arg0 * PORT_INTRO_SHADOW_PCT) / 100;
}
#endif



void doshadow(ModelRenderData *renderdata, Model *model, ModelNode *node)
{
    sImageTableEntry *image;
    RenderPosView *mtx;
    ModelRoData_ShadowRecord *shadow;
    Vtx vtxtemplate;
    Vtx *vtx;
    s16 y;
    s32 temp;
    f32 sizex;
    f32 sizey;
    f32 height;
    ModelRwData_HeaderRecord *rwdata;

    if ((s32) D_800363F0 <= 0)
    {
        return;
    }

    shadow = &node->Data->Shadow;
    vtxtemplate = D_800363F8;
    rwdata = (ModelRwData_HeaderRecord *) modelGetNodeRwData(model, shadow->HeaderNode);
    height = rwdata->pos.y - rwdata->ground;
    sizex = shadow->size.x;
    sizey = shadow->size.y;

    if (!(renderdata->flags & 2))
    {
        return;
    }

    if ((renderdata->PropType == PROP_TYPE_CHR) || (renderdata->PropType == PROP_TYPE_SMOKE))
    {
        temp = (vtxtemplate.v.cn[3] = ((renderdata->envcolour.word & 0xff) * D_800363F0) / 255);
    }
    else
    {
        vtxtemplate.v.cn[3] = D_800363F0;
    }

    temp = modelFindNodeMtxIndex(node, 0);
    mtx = &model->render_pos[temp];

    if (renderdata->zbufferenabled)
    {
        y = (2.0f - height) / model->scale;
    }
    else
    {
        y = (-height) / model->scale;
    }

    if (height < 50.0f)
    {
        sizex *= 1.25f;
        sizey *= 1.25f;
    }
    else if (300.0f < height)
    {
        sizex = 0.0f;
        sizey = sizex;
    }
    else
    {
        sizex *= (300.0f - height) / 200.0f;
        sizey *= (300.0f - height) / 200.0f;
    }

#ifdef LEFTOVERDEBUG
    if (vtxallocator == NULL)
    {
        if (1);
        osSyncPrintf("doshadow: no vtx allocator!\n");
        return_null();
    }
#endif

    vtx = (Vtx *) vtxallocator(4);

    vtx[0] = vtxtemplate;
    vtx[1] = vtxtemplate;
    vtx[2] = vtxtemplate;
    vtx[3] = vtxtemplate;

    vtx[0].v.ob[0] = shadow->pos.x - sizex;
    vtx[0].v.ob[1] = y;
    vtx[0].v.ob[2] = shadow->pos.y - sizey;
    vtx[1].v.ob[0] = shadow->pos.x - sizex;
    vtx[1].v.ob[1] = y;
    vtx[1].v.ob[2] = shadow->pos.y + sizey;
    vtx[2].v.ob[0] = shadow->pos.x + sizex;
    vtx[2].v.ob[1] = y;
    vtx[2].v.ob[2] = shadow->pos.y + sizey;
    vtx[3].v.ob[0] = shadow->pos.x + sizex;
    vtx[3].v.ob[1] = y;
    vtx[3].v.ob[2] = shadow->pos.y - sizey;

    gSPSegment(renderdata->gdl++, SPSEGMENT_MODEL_COL1, osVirtualToPhysical(shadow->BaseAddr));

    if (shadow->image != NULL)
    {
        image = shadow->image;

#ifndef TARGET_N64
        { /* diagnostic (GE_DUMP_BOND): logs the shadow image's actual format
           * (texture entry vs pool record). */
            extern char *getenv(const char *);
            static int logged;
            if (logged < 2 && getenv("GE_DUMP_BOND") != NULL)
            {
                extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
                extern void ge_dbg_gfxdump(const char *tag, void *gdl, int count);
                f32 v[6];
                logged++;
                v[0] = (f32)image->index;
                v[1] = (f32)image->width;
                v[2] = (f32)image->height;
                v[3] = (f32)image->format;
                v[4] = (f32)image->depth;
                v[5] = (f32)image->level;
                ge_dbg_vec("shadowimg idx/w/h/fmt/depth/lvl", v, 6);
                if (image->index >= 0x10000000u)
                {
                    extern struct tex *texFindInPool(s32 texturenum, struct texpool *pool);
                    extern struct texpool *portModelTexPool(void);
                    u16 texnum = ((u16 *)image->index)[-4];
                    struct tex *recglobal = texFindInPool(texnum, NULL);
                    struct tex *recmodel = texFindInPool(texnum, portModelTexPool());
                    v[0] = (f32)texnum;
                    v[1] = (f32)(recglobal != NULL);
                    v[2] = (f32)(recmodel != NULL);
                    ge_dbg_vec("shadowpool num/global/model", v, 3);
                    if (recglobal != NULL)
                    {
                        ge_dbg_gfxdump("shadowrec-global", recglobal, 2);
                    }
                    if (recmodel != NULL)
                    {
                        ge_dbg_gfxdump("shadowrec-model", recmodel, 2);
                    }
                }
            }
        }
#endif

        vtx[0].v.tc[0] = 0;
        vtx[0].v.tc[1] = 0;
        vtx[1].v.tc[0] = (image->width << 5) - 1;
        vtx[1].v.tc[1] = 0;
        vtx[2].v.tc[0] = (image->width << 5) - 1;
        vtx[2].v.tc[1] = (image->height << 5) - 1;
        vtx[3].v.tc[0] = 0;
        vtx[3].v.tc[1] = (image->height << 5) - 1;

        sub_GAME_7F073038(renderdata, image, 4);
    }
    else
    {
        sub_GAME_7F073038(renderdata, NULL, 1);
    }

    gSPSetGeometryMode(renderdata->gdl++, G_CULL_BACK);

    {
        Gfx *gdl;
        gSPMatrix(renderdata->gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(renderdata->gdl++, osVirtualToPhysical(vtx), 4, 0);
        // gSP2Triangles(gdl, 0, 1, 2, 0, 2, 3, 0, 0) via GE's G_TRI4. The macro
        // itself can't be used here: its inner Gfx temp would be the block's gfx
        // pointer local and shift the spill homes off the original layout.
        gdl = renderdata->gdl++;
        gdl->words.w0 = _SHIFTL(G_TRI4, 24, 8) | _SHIFTL(2, 0, 4); gdl->words.w1 = _SHIFTL(3, 12, 4) | _SHIFTL(2, 8, 4) | _SHIFTL(1, 4, 4);
    }
}


void sub_GAME_7F074514(s32 param_1,struct Model *param_2,struct ModelNode *param_3)
{
    return;
}


void sub_GAME_7F074524(Gfx *param_1,struct Model *param_2, struct ModelNode *param_3)
{
    return;
}


void sub_GAME_7F074534(ModelRenderData* data, Model* model, ModelNode* node) {
    u32 id = node->Opcode & 0xFF;
    switch (id) {
    case MODELNODE_OPCODE_LOD:
        modelApplyDistanceRelations(model, node);
        return;
    case MODELNODE_OPCODE_SWITCH:
        modelApplyToggleRelations(model, node);
        return;
    case MODELNODE_OPCODE_HEAD:
        modelApplyHeadRelations(model, node);
        return;
    case MODELNODE_OPCODE_BSP:
        modelApplyReorderRelations(model, node);
        return;
    case MODELNODE_OPCODE_OP11:
        sub_GAME_7F0737FC(data, model, node);
        return;
    case MODELNODE_OPCODE_GUNFIRE:
        dogfnegx(data, model, node);
        return;
    case MODELNODE_OPCODE_SHADOW:
        doshadow(data, model, node);
        return;
    case MODELNODE_OPCODE_BBOX:
        sub_GAME_7F074514(data, model, node);
        return;
    case MODELNODE_OPCODE_OP17:
        sub_GAME_7F074524(data, model, node);
        return;
    case MODELNODE_OPCODE_DL:
        modelRenderNodeGundl(data, node);
        return;
    case MODELNODE_OPCODE_DLCOLLISION:
        modelRenderNodeDl(data, model, node);
        return;
    case MODELNODE_OPCODE_OP20:
        sub_GAME_7F072C10(data, model, node);
        return;
    case MODELNODE_OPCODE_DLPRIMARY:
        dorottex(data, node);
        return;
    case MODELNODE_OPCODE_OP05:
        sub_GAME_7F07306C(data, model, node);
        return;
    case MODELNODE_OPCODE_OP07:
        dotube(data, model, node);
        return;
    case MODELNODE_OPCODE_OP06:
        sub_GAME_7F0737EC(data,model,node);
        return;
    case MODELNODE_OPCODE_HEADER:
    case MODELNODE_OPCODE_GROUP:
    case MODELNODE_OPCODE_OP03:
    case MODELNODE_OPCODE_OP14:
    case MODELNODE_OPCODE_INTERLINK:
    case MODELNODE_OPCODE_OP16:
    default:
        return;
    }
}


void subdraw(ModelRenderData *mrData, Model *mdl)
{
    ModelNode *root = mdl->obj->RootNode;
    #if defined(LEFTOVERDEBUG)

    if (mrData->gdl == NULL)
    {
        osSyncPrintf("subdraw: no gfxlist!\n");
        return_null();
    }

    if (mdl->obj->isLoaded)
    {
    }
    else
    {
        osSyncPrintf("subdraw: object not initialised! (0x%X)\n", (uintptr_t)mdl->obj);
        return_null();
    }
    #endif
    gSPSegment(mrData->gdl++, 3, osVirtualToPhysical(mdl->render_pos));

    while (root != NULL)
    {
        sub_GAME_7F074534(mrData, mdl, root);

        if (root->Child)
        {
            root = root->Child;
        }
        else
        {
            while (root)
            {
                if (root->Next)
                {
                    root = root->Next;
                    break;
                }
                root = root->Parent;
            }
        }
    }
}


// unreferenced
void sub_GAME_7F074790(ModelRenderData* arg0, Model* arg1)
{
    subcalcpos(arg1);
    subcalcmatrices(arg0, arg1);
    subdraw((intptr_t) arg0, arg1);
}


/**
 * Address: 7F0747D0
 *
 * Ray vs transformed bounding box test. The bbox is transformed by mtx while the ray
 * is defined by pos and dir.
 *
 * @returns TRUE if the ray intersects the bbox, otherwise FALSE.
 */
bool modelTestRayIntersectsTransformedBBox(ModelRoData_BoundingBoxRecord *bbox, Mtxf *mtx, coord3d *pos, coord3d *dir)
{
    f32 xthingx;
    f32 xthingy;
    f32 xthingz;
    u32 stack1[1];
    f32 xpmin;
    f32 xpmax;
    f32 xsum1;
    f32 xsum2;
    f32 xsum3;
    f32 negL2;
    f32 pmin;
    f32 ythingx;
    f32 ythingy;
    f32 ythingz;
    f32 pmax;
    f32 ypmin;
    f32 ypmax;
    f32 ysum1;
    f32 ysum2;
    f32 ysum3;
    f32 mult1;
    f32 mult2;
    f32 bestsum2;
    f32 bestsum1;
    f32 anotherbestsum3;
    f32 anotherbestsum1;
    f32 mult3;
    f32 mult4;
    f32 zthingx;
    f32 zthingy;
    f32 zthingz;
    u32 stack3[1];
    f32 zpmin;
    f32 zpmax;
    f32 zsum1;
    f32 zsum2;
    f32 zsum3;

    xthingx = mtx->m[0][0] * mtx->m[0][0];
    xthingy = mtx->m[0][1] * mtx->m[0][1];
    xthingz = mtx->m[0][2] * mtx->m[0][2];

    negL2 = -((xthingx + xthingy) + xthingz);

    xpmin = negL2;
    xpmin *= bbox->Bounds.xmin;

    xpmax = negL2;
    xpmax *= bbox->Bounds.xmax;

    xsum1 = ((mtx->m[0][0] * dir->f[0]) + (mtx->m[0][1] * dir->f[1])) + (mtx->m[0][2] * dir->f[2]);
    xsum2 = ((mtx->m[0][0] * (pos->f[0] - mtx->m[3][0])) + (mtx->m[0][1] * (pos->f[1] - mtx->m[3][1]))) + (mtx->m[0][2] * (pos->f[2] - mtx->m[3][2]));

    xsum3 = -(xsum2 + xpmax);
    xsum2 = -(xsum2 + xpmin);

    if (xsum1 < 0.0f)
    {
        xsum1 = -xsum1;
        xsum2 = -xsum2;
        xsum3 = -xsum3;
    }

    if ((xsum2 < 0.0f) && (xsum3 < 0.0f))
    {
        return FALSE;
    }

    if (xsum3 < xsum2)
    {
        f32 tmp = xsum2;

        xsum2 = xsum3;
        xsum3 = tmp;
    }

    ythingx = mtx->m[1][0] * mtx->m[1][0];
    ythingy = mtx->m[1][1] * mtx->m[1][1];
    ythingz = mtx->m[1][2] * mtx->m[1][2];

    negL2 = -((ythingx + ythingy) + ythingz);

    ypmin = negL2;
    ypmin *= bbox->Bounds.ymin;

    ypmax = negL2;
    ypmax *= bbox->Bounds.ymax;

    ysum1 = ((mtx->m[1][0] * dir->f[0]) + (mtx->m[1][1] * dir->f[1])) + (mtx->m[1][2] * dir->f[2]);
    ysum2 = ((mtx->m[1][0] * (pos->f[0] - mtx->m[3][0])) + (mtx->m[1][1] * (pos->f[1] - mtx->m[3][1]))) + (mtx->m[1][2] * (pos->f[2] - mtx->m[3][2]));

    ysum3 = -(ysum2 + ypmax);
    ysum2 = -(ysum2 + ypmin);

    if (ysum1 < 0.0f)
    {
        ysum1 = -ysum1;
        ysum2 = -ysum2;
        ysum3 = -ysum3;
    }

    if ((ysum2 < 0.0f) && (ysum3 < 0.0f))
    {
        return FALSE;
    }

    if (ysum3 < ysum2)
    {
        f32 tmp = ysum2;

        ysum2 = ysum3;
        ysum3 = tmp;
    }

    mult1 = ysum2 * xsum1;
    mult2 = xsum2 * ysum1;
    mult3 = xsum3 * ysum1;
    mult4 = ysum3 * xsum1;

    if (mult1 < mult2)
    {
        if (mult4 < mult2)
        {
            return FALSE;
        }

        bestsum2 = xsum2;
        bestsum1 = xsum1;
    }
    else
    {
        if (mult3 < mult1)
        {
            return FALSE;
        }

        bestsum2 = ysum2;
        bestsum1 = ysum1;
    }

    if (mult3 < mult4)
    {
        anotherbestsum3 = xsum3;
        anotherbestsum1 = xsum1;
    }
    else
    {
        anotherbestsum3 = ysum3;
        anotherbestsum1 = ysum1;
    }

    zthingx = mtx->m[2][0] * mtx->m[2][0];
    zthingy = mtx->m[2][1] * mtx->m[2][1];
    zthingz = mtx->m[2][2] * mtx->m[2][2];

    negL2 = -((zthingx + zthingy) + zthingz);

    zpmin = negL2;
    zpmin *= bbox->Bounds.zmin;

    zpmax = negL2;
    zpmax *= bbox->Bounds.zmax;

    zsum1 = ((mtx->m[2][0] * dir->f[0]) + (mtx->m[2][1] * dir->f[1])) + (mtx->m[2][2] * dir->f[2]);
    zsum2 = ((mtx->m[2][0] * (pos->f[0] - mtx->m[3][0])) + (mtx->m[2][1] * (pos->f[1] - mtx->m[3][1]))) + (mtx->m[2][2] * (pos->f[2] - mtx->m[3][2]));

    zsum3 = -(zsum2 + zpmax);
    zsum2 = -(zsum2 + zpmin);

    if (zsum1 < 0.0f)
    {
        zsum1 = -zsum1;
        zsum2 = -zsum2;
        zsum3 = -zsum3;
    }

    if ((zsum2 < 0.0f) && (zsum3 < 0.0f))
    {
        return FALSE;
    }

    if (zsum3 < zsum2)
    {
        f32 tmp = zsum2;

        zsum2 = zsum3;
        zsum3 = tmp;
    }

    if ((bestsum2 * zsum1) < (zsum2 * bestsum1))
    {
        if ((anotherbestsum3 * zsum1) < (zsum2 * anotherbestsum1))
        {
            return FALSE;
        }
    }
    else if ((zsum3 * bestsum1) < (bestsum2 * zsum1))
    {
        return FALSE;
    }

    return TRUE;
}


/**
 * Address: 7F074C68
 */
bool modelTestRayIntersectsNodeBBox(Model *model, ModelNode *node, coord3d *pos, coord3d *dir)
{
    ModelRoData_BoundingBoxRecord *bbox = &node->Data->BoundingBox;

    return modelTestRayIntersectsTransformedBBox(bbox, modelFindNodeMtx(model, node, 0), pos, dir);
}


/**
 * Address: 7F074CAC
 */
s32 sub_GAME_7F074CAC(Model *model, ModelNode *node, coord3d *raypos, coord3d *raydir)
{
    ModelRoData_Op17Record *hitData;
    Mtxf *nodeMtx;
    ModelOp17MainStack rayData[1];
    f32 centerProjection;
    Mtxf *otherNodeMtx;
    ModelOp17AxisStack axisData[1];
    f32 scaledProjection;
    f32 projectionScalar;
    f32 secondAxisScale;
    f32 centerDistanceSq;
    u32 nodeFlags;
    f32 directionDotProduct;

    hitData = (ModelRoData_Op17Record *) node->Data;
    nodeMtx = modelFindNodeMtx(model, node, 0);
    rayData->data.rel = D_80036408;
    rayData->data.radiusSq = hitData->radiusSq;
    rayData->data.dir.f[0] = raydir->x;
    rayData->data.dir.f[1] = raydir->y;
    rayData->data.dir.f[2] = raydir->z;
    nodeFlags = node->Opcode;

    if (nodeFlags & 0x100)
    {
        rayData->data.pos.f[0] = hitData->pos.f[0];
        rayData->data.pos.f[1] = hitData->pos.f[1];
        rayData->data.pos.f[2] = hitData->pos.f[2];
        rayData->data.rel.f[0] = (((rayData->data.pos.f[0] * nodeMtx->m[0][0]) + (rayData->data.pos.f[1] * nodeMtx->m[1][0])) + (rayData->data.pos.f[2] * nodeMtx->m[2][0])) + (nodeMtx->m[3][0] - raypos->x);
        rayData->data.rel.f[1] = (((rayData->data.pos.f[0] * nodeMtx->m[0][1]) + (rayData->data.pos.f[1] * nodeMtx->m[1][1])) + (rayData->data.pos.f[2] * nodeMtx->m[2][1])) + (nodeMtx->m[3][1] - raypos->y);
        rayData->data.rel.f[2] = (((rayData->data.pos.f[0] * nodeMtx->m[0][2]) + (rayData->data.pos.f[1] * nodeMtx->m[1][2])) + (rayData->data.pos.f[2] * nodeMtx->m[2][2])) + (nodeMtx->m[3][2] - raypos->z);
    }
    else if (nodeFlags & 0x200)
    {
        if (hitData->othernode != NULL)
        {
            otherNodeMtx = modelFindNodeMtx(model, hitData->othernode, 0);
            rayData->data.rel.f[0] = ((nodeMtx->m[3][0] + otherNodeMtx->m[3][0]) * 0.5f) - (*raypos).f[0];
            rayData->data.rel.f[1] = ((nodeMtx->m[3][1] + otherNodeMtx->m[3][1]) * 0.5f) - (*raypos).f[1];
            rayData->data.rel.f[2] = ((nodeMtx->m[3][2] + otherNodeMtx->m[3][2]) * 0.5f) - (*raypos).f[2];
        }
        else
        {
            rayData->data.rel.f[0] = nodeMtx->m[3][0] - raypos->x;
            rayData->data.rel.f[1] = nodeMtx->m[3][1] - raypos->y;
            rayData->data.rel.f[2] = nodeMtx->m[3][2] - raypos->z;
        }
    }
    else
    {
        rayData->data.rel.f[0] = nodeMtx->m[3][0] - raypos->x;
        rayData->data.rel.f[1] = nodeMtx->m[3][1] - raypos->y;
        rayData->data.rel.f[2] = nodeMtx->m[3][2] - raypos->z;
    }

    nodeFlags = node->Opcode;

    if (nodeFlags & 0x400)
    {
        projectionScalar = hitData->scale1;
        secondAxisScale = hitData->scale2;
        scaledProjection = (((rayData->data.dir.f[0] * nodeMtx->m[0][0]) + (rayData->data.dir.f[1] * nodeMtx->m[0][1])) + (rayData->data.dir.f[2] * nodeMtx->m[0][2])) * projectionScalar;
        directionDotProduct = scaledProjection;
        rayData->data.dir.f[0] = rayData->data.dir.f[0] + (nodeMtx->m[0][0] * directionDotProduct);
        rayData->data.dir.f[1] = rayData->data.dir.f[1] + (nodeMtx->m[0][1] * directionDotProduct);
        rayData->data.dir.f[2] = rayData->data.dir.f[2] + (nodeMtx->m[0][2] * directionDotProduct);
        centerProjection = (((rayData->data.rel.f[0] * nodeMtx->m[0][0]) + (rayData->data.rel.f[1] * nodeMtx->m[0][1])) + (rayData->data.rel.f[2] * nodeMtx->m[0][2])) * projectionScalar;
        rayData->data.rel.f[0] = rayData->data.rel.f[0] + (nodeMtx->m[0][0] * centerProjection);
        rayData->data.rel.f[1] = rayData->data.rel.f[1] + (nodeMtx->m[0][1] * centerProjection);
        rayData->data.rel.f[2] = rayData->data.rel.f[2] + (nodeMtx->m[0][2] * centerProjection);
        scaledProjection = (((rayData->data.dir.f[0] * nodeMtx->m[1][0]) + (rayData->data.dir.f[1] * nodeMtx->m[1][1])) + (rayData->data.dir.f[2] * nodeMtx->m[1][2])) * secondAxisScale;
        directionDotProduct = scaledProjection;
        rayData->data.dir.f[0] = rayData->data.dir.f[0] + (nodeMtx->m[1][0] * directionDotProduct);
        rayData->data.dir.f[1] = rayData->data.dir.f[1] + (nodeMtx->m[1][1] * directionDotProduct);
        rayData->data.dir.f[2] = rayData->data.dir.f[2] + (nodeMtx->m[1][2] * directionDotProduct);
        projectionScalar = (((rayData->data.rel.f[0] * nodeMtx->m[1][0]) + (rayData->data.rel.f[1] * nodeMtx->m[1][1])) + (rayData->data.rel.f[2] * nodeMtx->m[1][2])) * secondAxisScale;
        rayData->data.rel.f[0] = rayData->data.rel.f[0] + (nodeMtx->m[1][0] * projectionScalar);
        rayData->data.rel.f[1] = rayData->data.rel.f[1] + (nodeMtx->m[1][1] * projectionScalar);
        rayData->data.rel.f[2] = rayData->data.rel.f[2] + (nodeMtx->m[1][2] * projectionScalar);
    }
    else if (((nodeFlags & 0x800) || (nodeFlags & 0x1000)) || (nodeFlags & 0x2000))
    {
        axisData->axisAdjustment.scale = hitData->scale1;
        if ((nodeFlags & 0x800))
        {
            axisData->axisAdjustment.axis.f[0] = nodeMtx->m[0][0];
            axisData->axisAdjustment.axis.f[1] = nodeMtx->m[0][1];
            axisData->axisAdjustment.axis.f[2] = nodeMtx->m[0][2];
        }
        else if (nodeFlags & 0x1000)
        {
            axisData->axisAdjustment.axis.f[0] = nodeMtx->m[1][0];
            axisData->axisAdjustment.axis.f[1] = nodeMtx->m[1][1];
            axisData->axisAdjustment.axis.f[2] = nodeMtx->m[1][2];
        }
        else if (nodeFlags & 0x2000)
        {
            axisData->axisAdjustment.axis.f[0] = nodeMtx->m[2][0];
            axisData->axisAdjustment.axis.f[1] = nodeMtx->m[2][1];
            axisData->axisAdjustment.axis.f[2] = nodeMtx->m[2][2];
        }

        directionDotProduct = axisData->axisAdjustment.scale * (
        (rayData->data.dir.f[0] * axisData->axisAdjustment.axis.f[0]) +
        (rayData->data.dir.f[1] * axisData->axisAdjustment.axis.f[1]) +
        (rayData->data.dir.f[2] * axisData->axisAdjustment.axis.f[2])
        );

        rayData->data.dir.f[0] = rayData->data.dir.f[0] + (axisData->axisAdjustment.axis.f[0] * directionDotProduct);
        rayData->data.dir.f[1] = rayData->data.dir.f[1] + (axisData->axisAdjustment.axis.f[1] * directionDotProduct);
        rayData->data.dir.f[2] = rayData->data.dir.f[2] + (axisData->axisAdjustment.axis.f[2] * directionDotProduct);

        scaledProjection = axisData->axisAdjustment.scale * (
        (rayData->data.rel.f[0] * axisData->axisAdjustment.axis.f[0]) +
        (rayData->data.rel.f[1] * axisData->axisAdjustment.axis.f[1]) +
        (rayData->data.rel.f[2] * axisData->axisAdjustment.axis.f[2])
        );

        rayData->data.rel.f[0] = rayData->data.rel.f[0] + (axisData->axisAdjustment.axis.f[0] * scaledProjection);
        rayData->data.rel.f[1] = rayData->data.rel.f[1] + (axisData->axisAdjustment.axis.f[1] * scaledProjection);
        rayData->data.rel.f[2] = rayData->data.rel.f[2] + (axisData->axisAdjustment.axis.f[2] * scaledProjection);
    }

    projectionScalar =
    (rayData->data.dir.f[0] * rayData->data.rel.f[0]) +
    (rayData->data.dir.f[1] * rayData->data.rel.f[1]) +
    (rayData->data.dir.f[2] * rayData->data.rel.f[2]);

    if (0.0f < projectionScalar)
    {
        directionDotProduct = (rayData->data.dir.f[0] * rayData->data.dir.f[0]) + (rayData->data.dir.f[1] * rayData->data.dir.f[1]) + (rayData->data.dir.f[2] * rayData->data.dir.f[2]);
        centerDistanceSq = (rayData->data.rel.f[0] * rayData->data.rel.f[0]) + (rayData->data.rel.f[1] * rayData->data.rel.f[1]) + (0, rayData->data.rel.f[2] * rayData->data.rel.f[2]);

        if (((centerDistanceSq - rayData->data.radiusSq) * directionDotProduct) <= (projectionScalar * projectionScalar))
        {
            return 1;
        }
        else
        {
            return 0;
        }
    }

    return 0;
}


/**
 * Address: 7F0752FC
 */
u32 modelFindNextProjectileHitCandidate(Model *model, coord3d *arg1, coord3d *arg2, ModelNode **nodeptr)
{
    ModelNode *node;
    s32 descend;
    u32 opcode;

    descend = TRUE;

    if (*nodeptr != NULL) {
        node = *nodeptr;
        *nodeptr = NULL;
    } else {
        node = model->obj->RootNode;
    }

    if (node != NULL) {
        do {
            if (descend != 0 && node->Child != NULL) {
                node = node->Child;
            } else {
                if (node != NULL) {
                    do {
                        if (node->Next != NULL) {
                            node = node->Next;
                            break;
                        }

                        node = node->Parent;
                    } while (node != NULL);
                }

                if (node == NULL) {
                    break;
                }
            }

            descend = TRUE;
            opcode = node->Opcode & 0xff;

            /*
            * This switch is written as opcode - 1 to match the compiler's jump-table generation.
            * The real opcodes run from HEADER=1 through DLCOLLISION=24, so the compiler
            * normalizes them to a zero-based table index by subtracting 1.
            */
            switch (opcode - 1) {
            case MODELNODE_OPCODE_BBOX - 1:
                if (modelTestRayIntersectsNodeBBox(model, node, arg1, arg2) != 0) {
                    *nodeptr = node;
                    return *(u32 *)node->Data;
                }
                descend = FALSE;
                break;

            case MODELNODE_OPCODE_OP17 - 1:
                if (sub_GAME_7F074CAC(model, node, arg1, arg2) != 0) {
                    *nodeptr = node;
                    return *(u32 *)node->Data;
                }
                descend = FALSE;
                break;

            case MODELNODE_OPCODE_LOD - 1:
                modelApplyDistanceRelations(model, node);
                break;

            case MODELNODE_OPCODE_SWITCH - 1:
                modelApplyToggleRelations(model, node);
                break;

            case MODELNODE_OPCODE_HEAD - 1:
                modelApplyHeadRelations(model, node);
                break;
            case MODELNODE_OPCODE_HEADER - 1:
            case MODELNODE_OPCODE_DLCOLLISION - 1:
            default:
                break;
            }
        } while (node != NULL);
    }

    return 0;
}


/**
 * Unreferenced
 */
u32 *sub_GAME_7F07549C(void *arg0, f32 *arg1, f32 *arg2, ModelNode **nodeptr)
{
    *nodeptr = NULL;
    return modelFindNextProjectileHitCandidate(arg0, arg1, arg2, nodeptr);
}


/**
 * Address 7F0754BC.
 * Copy animation from ROM to RAM
*/
uintptr_t loadAnimationFrame(ModelAnimation* anim, s32 frame, ModelSkeleton* unused)
{
    anim = portAnimationResolve(anim);
    uintptr_t ret;
    uintptr_t source;
    s32 frameSize;
    uintptr_t dest;
    u32 size;

    ret = 0;
    frameSize = anim->unk0E >> 3; // divide by 8

    if (sizeof(void *) > 4 || (anim->address & 0x80000000)) // If animation's address is in RAM
    {
        // Load that frame from RAM
        ret = anim->address + (frame * frameSize);
    }
    else if (D_80036414 != NULL) // should never be NULL after initAnimationsBuffer is called
    {
        // Get dest from this D_80036414 which points to an array. Align to 16 bytes.
        dest = ((uintptr_t) (D_80036414->animBufferPtr2 + 15) >> 4) * 16;
        ret = dest;

        // Get source of this animation in ROM with the offset of the frame we'll load
        source = anim->address + (frame * frameSize);
        if (source & 1)
        {
            source--;
            frameSize++;
            ret++;
        }

        // Size of frame but 16-bytes aligned. Observed to be 80 bytes. Might differ for non-guards.
        size = ((u32) (frameSize + 15) >> 4) * 16;

        // This copies one animation frame from ROM to the destination in RAM
        romCopy((void* ) dest, (void* ) source, size);

        // Increment this which serves nothing
        D_80036414->uselessPointer += 1;

        // Set this to point to the end of the copied frame
        // This allows to copy another frame after this one
        D_80036414->animBufferPtr2 = dest + size;
    }
    return ret;
}


/**
 * Address 7F0755B0.
*/
void modelResetAnimationsScratchBuffer(void)
{
    if (D_80036414 != NULL) // should never be NULL after initAnimationsBuffer is called
    {
        // Reset the pointer to point to the start of the array
        D_80036414->animBufferPtr2 = D_80036414->animBufferPtr1;
        D_80036414->uselessPointer = NULL;
    }
}


#ifdef TARGET_N64
#define PROMOTE(var) \
    if (var) \
        var = (void *)((u32)var + diff)
#else
/* PORT: compiled-in model files hold real pointers except about 105 raw
 * segment-5 words the asset generator left behind, so only 0x05-tagged values
 * are rebased onto the file base (real pointers and NULL never look like that). */
#define PROMOTE(var) \
    if (((u32)(var) & 0xFF000000) == 0x05000000) \
        var = (void *)((u32)(var) - 0x05000000 + (u32)vma + (u32)diff)
#endif

void modelPromoteNodeOffsetsToPointers(ModelNode *node, u32 vma, u32 fileramaddr)
{
    s32 diff = fileramaddr - vma;
    s32 i;

    while (node)
    {
        u32 type = node->Opcode & 0xff;

        PROMOTE(node->Data);
        PROMOTE(node->Parent);
        PROMOTE(node->Next);
        PROMOTE(node->Prev);
        PROMOTE(node->Child);

        switch (type)
        {
            case MODELNODE_OPCODE_HEADER:
                {
                    ModelRoData_HeaderRecord* rodata = &node->Data->Header;
                    PROMOTE(rodata->FirstGroup);
                    break;
                }

            case MODELNODE_OPCODE_GROUP:
                {
                    ModelRoData_GroupRecord* rodata = &node->Data->Group;
                    PROMOTE(rodata->ChildGroup);
                    break;
                }

            case MODELNODE_OPCODE_OP03:
                {
                    ModelRoData_GroupRecord* rodata = &node->Data->Group;
                    PROMOTE(rodata->ChildGroup);
                    break;
                }

            case MODELNODE_OPCODE_DL:
                {
                    ModelRoData_DisplayListRecord* rodata = &node->Data->DisplayList;
                    PROMOTE(rodata->Vertices);
#ifndef TARGET_N64
                    /* Models loaded from the ROM hold 0x05 segment offsets that
                     * the CPU DL walkers can't use, so promote them to real
                     * pointers here (a no-op for compiled-in models). */
                    PROMOTE(rodata->Primary);
                    PROMOTE(rodata->Secondary);
#endif
                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_DLCOLLISION:
                {
                    ModelRoData_DisplayList_CollisionRecord* rodata = &node->Data->DisplayListCollisions;
#ifndef TARGET_N64
                    /* PORT: the byte-order fixups below must run exactly once
                     * per record, but on PC this walk re-runs over the same
                     * compiled-in bytes on every reload; BaseAddr (0 until set
                     * at the end of this case) marks done. */
                    s32 raw = (rodata->BaseAddr != (void *)fileramaddr);
#endif
#ifndef TARGET_N64
                    PROMOTE(rodata->Primary);   /* ROM-loaded models (see DL case) */
                    PROMOTE(rodata->Secondary);
#endif
                    PROMOTE(rodata->Vertices);
                    PROMOTE(rodata->CollisionVertices);
                    PROMOTE(rodata->PointUsage);
                    for (i = 0; i < rodata->numCollisionVertices; i++)
                    {
#ifndef TARGET_N64
                        if (raw)
                        {
                            /* For collision vertices the +0x8 union is a
                             * pointer, but the generator emitted it as two s16
                             * texcoords, so on little-endian its halves are
                             * swapped; unswap it (low half 0x0500) or
                             * chrCreateBloodStain crashes. */
                            u32 w = (uintptr_t) rodata->CollisionVertices[i].LinkedTo;

                            if (w != 0 && (w & 0x0000FFFFu) == 0x00000500u)
                            {
                                rodata->CollisionVertices[i].LinkedTo =
                                    (void *)((w << 16) | (w >> 16));

                                /* Same problem for the +0xc union: emitted as
                                 * r/g/b/a bytes, its s16 CollisionRelatedIndex
                                 * is byte-swapped on little-endian and must be
                                 * fixed. */
                                {
                                    u16 ri = (u16) rodata->CollisionVertices[i].CollisionRelatedIndex;

                                    rodata->CollisionVertices[i].CollisionRelatedIndex =
                                        (s16) ((ri >> 8) | (ri << 8));
                                }
                            }
                        }
#endif
                        PROMOTE(rodata->CollisionVertices[i].LinkedTo);
                    }
                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_OP20:
                {
                    ModelRoData_HeaderRecord* rodata = &node->Data->Header;
                    PROMOTE(rodata->FirstGroup);
                    break;
                }

            case MODELNODE_OPCODE_OP05:
                {
                    ModelRoData_Op05Record* rodata = &node->Data->Op05;

                    // shared with op07
                    PROMOTE(rodata->Children);
                    PROMOTE(rodata->Vertices);
                    PROMOTE(rodata->Images);
                    for (i = 0; i < rodata->NumChildren; i++)
                    {
                        PROMOTE(rodata->Children[i].unk04);
                    }

                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_OP07:
                {
                    ModelRoData_Op07Record* rodata = &node->Data->Op07;
                    PROMOTE(rodata->unk00);
                    PROMOTE(rodata->unk04);

                    // shared with op05
                    PROMOTE(rodata->Children);
                    PROMOTE(rodata->Vertices);
                    PROMOTE(rodata->Images);
                    for (i = 0; i < rodata->NumChildren; i++)
                    {
                        PROMOTE(rodata->Children[i].unk04);
                    }

                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_OP06:
                {
                    ModelRoData_Op06Record* rodata = &node->Data->Op06;
                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_LOD:
                {
                    ModelRoData_LODRecord* rodata = &node->Data->LOD;
                    PROMOTE(rodata->Affects);
                    node->Child = rodata->Affects;
                    break;
                }

            case MODELNODE_OPCODE_SWITCH:
                {
                    ModelRoData_SwitchRecord* rodata = &node->Data->Switch;
                    PROMOTE(rodata->Controls);
                    break;
                }

            case MODELNODE_OPCODE_BSP:
                {
                    ModelRoData_BSPRecord* rodata = &node->Data->BSP;
                    PROMOTE(rodata->leftChild);
                    PROMOTE(rodata->rightChild);
                    break;
                }

            case MODELNODE_OPCODE_OP17:
                {
                    ModelRoData_GroupRecord* rodata = &node->Data->Group;
                    PROMOTE(rodata->ChildGroup);
                    break;
                }

            case MODELNODE_OPCODE_OP11:
                {
                    ModelRoData_Op11Record* rodata = &node->Data->Op11;
                    PROMOTE(rodata->unk0c[15]);
                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_GUNFIRE:
                {
                    ModelRoData_GunfireRecord* rodata = &node->Data->Gunfire;
                    PROMOTE(rodata->Image);
                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_SHADOW:
                {
                    ModelRoData_ShadowRecord* rodata = &node->Data->Shadow;
                    PROMOTE(rodata->image);
                    PROMOTE(rodata->Header);
                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            case MODELNODE_OPCODE_DLPRIMARY:
                {
                    ModelRoData_DisplayListPrimaryRecord* rodata = &node->Data->DisplayListPrimary;
                    PROMOTE(rodata->Vertices);
#ifndef TARGET_N64
                    PROMOTE(rodata->Primary);   /* ROM-loaded models (see DL case) */
#endif
                    rodata->BaseAddr = (void *)fileramaddr;
                    break;
                }

            default:
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

/**
 * Address 7F075A90.
*/
void sub_GAME_7F075A90(ModelFileHeader *header, s32 vma, u32 addr) {
    s32 diff = addr - vma;
    s32 i;

    for(i = 0;i < header->numSwitches;i++)
    {
        PROMOTE(header->Switches[i]);
    }
    modelPromoteNodeOffsetsToPointers(header->RootNode, vma, addr);
}

#ifndef TARGET_N64
/**
 * PORT diagnostic: GE_COLAUDIT=1 checks a model's collision related-node links
 * right after the promote walk.
 *
 * Each collision vertex has a related-node pointer and a related index that
 * modelPromoteNodeOffsetsToPointers byte-fixes exactly once; if that goes
 * wrong, chrCreateBloodStain follows a bad pointer or index.
 *
 * Every link must point to a DLCOLLISION node whose collision table contains
 * the index, so any line printed is a real bug, and "0 bad" after a level
 * reload shows the fixups did not run twice.
 */
void portModelCollisionAudit(ModelNode *node, const char *name)
{
    static s32 armed = -1;
    s32 links = 0;
    s32 bad = 0;
    s32 steps = 0;

    if (armed < 0)
    {
        extern char *getenv(const char *);
        armed = getenv("GE_COLAUDIT") != NULL;
    }

    if (!armed || node == NULL)
    {
        return;
    }

    while (node != NULL && steps++ < 8192)
    {
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_DLCOLLISION && node->Data != NULL)
        {
            ModelRoData_DisplayList_CollisionRecord *rodata = &node->Data->DisplayListCollisions;
            s32 i;

            for (i = 0; i < rodata->numCollisionVertices; i++)
            {
                ModelNode  *rel = (ModelNode *) rodata->CollisionVertices[i].CollisionRelatedNode;
                s32         idx = rodata->CollisionVertices[i].CollisionRelatedIndex;
                const char *fault = NULL;

                if (rel == NULL)
                {
                    continue;
                }

                links++;

                if (portPtrLooksBad(rel))
                {
                    fault = "unusable node pointer";
                }
                else if ((rel->Opcode & 0xff) != MODELNODE_OPCODE_DLCOLLISION)
                {
                    fault = "target is not a DLCOLLISION node";
                }
                else if (rel->Data == NULL)
                {
                    fault = "target node has no rodata";
                }
                else if (idx < 0 || idx >= rel->Data->DisplayListCollisions.numCollisionVertices)
                {
                    fault = "related index out of range";
                }

                if (fault != NULL)
                {
                    bad++;

                    if (bad <= 8)
                    {
                        printf("ge: COLAUDIT %s: node %p vtx %d -> %p idx %d: %s\n",
                               name, (void *) node, i, (void *) rel, idx, fault);
                    }
                }
            }
        }

        if (node->Child != NULL)
        {
            node = node->Child;
        }
        else
        {
            while (node != NULL)
            {
                if (node->Next != NULL)
                {
                    node = node->Next;
                    break;
                }

                node = node->Parent;
            }
        }
    }

    printf("ge: COLAUDIT %s: %d collision link(s), %d bad\n", name, links, bad);
}
#endif

/**
 * unreferenced
 * Address 7F075B08.
*/
void REMOVED_sub_GAME_7F075B08(s32 param_1,s32 param_2,s32 param_3,s32 param_4)
{
    return;
}


s32 modelCalculateRwDataIndexes(ModelNode *basenode)
{
    u16 len = 0;
    ModelNode *node = basenode;
    union ModelRoData *rodata;

    while (node)
    {
        u32 type = node->Opcode & 0xff;

        switch (type)
        {
            case MODELNODE_OPCODE_HEADER:
                if (1)
                {
                    ModelRoData_HeaderRecord *rodata = &node->Data->Header;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_HeaderRecord) + sizeof(void *) - 1) / sizeof(void *);
                    break;
                }
            case MODELNODE_OPCODE_OP07:
                if (1)
                {
                    ModelRoData_Op07Record *rodata = &node->Data->Op07;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_Op07Record) + sizeof(void *) - 1) / sizeof(void *);
                    break;
                }
            case MODELNODE_OPCODE_LOD:
                if (1)
                {
                    ModelRoData_LODRecord *rodata = &node->Data->LOD;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_LODRecord) + sizeof(void *) - 1) / sizeof(void *);
                    node->Child = rodata->Affects;
                    break;
                }
            case MODELNODE_OPCODE_SWITCH:
                if (1)
                {
                    ModelRoData_SwitchRecord *rodata = &node->Data->Switch;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_SwitchRecord) + sizeof(void *) - 1) / sizeof(void *);
                    node->Child = rodata->Controls;
                    break;
                }
            case MODELNODE_OPCODE_HEAD:
                if (1)
                {
                    ModelRoData_HeadPlaceholderRecord *rodata = &node->Data->HeadPlaceholder;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_HeadPlaceholderRecord) + sizeof(void *) - 1) / sizeof(void *);
                    node->Child = NULL;
                    break;
                }
            case MODELNODE_OPCODE_BSP:
                if (1)
                {
                    ModelRoData_BSPRecord *rodata = &node->Data->BSP;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_BSPRecord) + sizeof(void *) - 1) / sizeof(void *);
                    modelApplyReorderRelationsByArg(node, FALSE);
                    break;
                }
            case MODELNODE_OPCODE_OP11:
                if (1)
                {
                    ModelRoData_Op11Record *rodata = &node->Data->Op11;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_Op11Record) + sizeof(void *) - 1) / sizeof(void *);
                    break;
                }
            case MODELNODE_OPCODE_GUNFIRE:
                if (1)
                {
                    ModelRoData_GunfireRecord *rodata = &node->Data->Gunfire;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_GunfireRecord) + sizeof(void *) - 1) / sizeof(void *);
                    break;
                }
            case MODELNODE_OPCODE_DLCOLLISION:
                if (1)
                {
                    ModelRoData_DisplayList_CollisionRecord *rodata = &node->Data->DisplayListCollisions;
                    rodata->RwDataIndex = len;
                    len += (sizeof(struct ModelRwData_DisplayList_CollisionRecord) + sizeof(void *) - 1) / sizeof(void *);
                    break;
                }
            default:
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
                if (node == basenode->Parent)
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

    return len;
}


void modelCalculateRwDataLen(struct ModelFileHeader *objheader)
{
  #if defined(LEFTOVERDEBUG)
    objheader->isLoaded = 1;
  #endif
    objheader->numRecords = modelCalculateRwDataIndexes(objheader->RootNode);
}


void modelInitRwData(Model *model, ModelNode *startnode)
{
    ModelNode *node = startnode;

    while (node)
    {
        u32 type = node->Opcode & 0xFF;

        switch (type)
        {
            case MODELNODE_OPCODE_HEADER:
                if (1)
                {
                    ModelRwData_HeaderRecord* rwdata = &modelGetNodeRwData(model, node)->Header;

                    rwdata->unk00 = 0;
                    rwdata->ground = 0;
                    rwdata->pos.x = 0;
                    rwdata->pos.y = 0;
                    rwdata->pos.z = 0;
                    rwdata->unk14 = 0;
                    rwdata->unk18 = 0;
                    rwdata->unk1c = 0;

                    rwdata->unk01 = 0;
                    rwdata->unk34.x = 0;
                    rwdata->unk34.y = 0;
                    rwdata->unk34.z = 0;
                    rwdata->unk30 = 0;
                    rwdata->unk24.x = 0;
                    rwdata->unk24.y = 0;
                    rwdata->unk24.z = 0;
                    rwdata->unk20 = 0;

                    rwdata->unk02 = 0;
                    rwdata->unk4c.x = 0;
                    rwdata->unk4c.y = 0;
                    rwdata->unk4c.z = 0;
                    rwdata->unk40.x = 0;
                    rwdata->unk40.y = 0;
                    rwdata->unk40.z = 0;
                    rwdata->unk5c = 0;
                    break;
                }

            case MODELNODE_OPCODE_OP07:
                if (1)
                {
                    ModelRwData_Op07Record* rwdata = &modelGetNodeRwData(model, node)->Op07;
                    rwdata->index = 0;
                    break;
                }


            case MODELNODE_OPCODE_LOD:
                if (1)
                {
                    ModelRoData_LODRecord* rodata = &node->Data->LOD;
                    ModelRwData_LODRecord* rwdata = &modelGetNodeRwData(model, node)->LOD;
                    rwdata->visible = FALSE;
                    node->Child = rodata->Affects;
                    break;
                }

            case MODELNODE_OPCODE_SWITCH:
                if (1)
                {
                    ModelRoData_SwitchRecord* rodata = &node->Data->Switch;
                    ModelRwData_SwitchRecord* rwdata = &modelGetNodeRwData(model, node)->Switch;
                    rwdata->visible = TRUE;
                    node->Child = rodata->Controls;
                    break;
                }

            case MODELNODE_OPCODE_HEAD:
                if (1)
                {
                    ModelRwData_HeadPlaceholderRecord* rwdata = &modelGetNodeRwData(model, node)->HeadPlaceholder;
                    rwdata->ModelFileHeader = NULL;
                    rwdata->RwDatas = NULL;
                    break;
                }

            case MODELNODE_OPCODE_BSP:
                if (1)
                {
                    ModelRwData_BSPRecord* rwdata = &modelGetNodeRwData(model, node)->BSP;
                    rwdata->visible = FALSE;
                    modelApplyReorderRelations(model, node);
                    break;
                }

            case MODELNODE_OPCODE_OP11:
                if (1)
                {
                    ModelRwData_Op11Record* rwdata = &modelGetNodeRwData(model, node)->Op11;
                    rwdata->unk00 = FALSE;
                    break;
                }

            case MODELNODE_OPCODE_GUNFIRE:
                if (1)
                {
                    ModelRwData_GunfireRecord* rwdata = &modelGetNodeRwData(model, node)->Gunfire;
                    rwdata->visible = FALSE;
                    break;
                }

            case MODELNODE_OPCODE_DLCOLLISION:
                if (1)
                {
                    ModelRoData_DisplayList_CollisionRecord* rodata = &node->Data->DisplayListCollisions;
                    ModelRwData_DisplayList_CollisionRecord* rwdata = &modelGetNodeRwData(model, node)->DisplayListCollisions;
                    rwdata->Vertices = rodata->Vertices;
                    rwdata->gdl = rodata->Primary;
                    break;
                }

            default:
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
                if (node == startnode->Parent)
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
}


void modelInit(struct Model *objinst, struct ModelFileHeader *header, u32 *data)
{
  objinst->obj = header;
  objinst->datas = data;
  objinst->rwdatalen = -1;
  objinst->attachedto = NULL;
  objinst->attachedto_objinst = NULL;
  objinst->scale = 1.0;
#ifndef TARGET_N64
  /* PORT: GE_MODEL_AUDIT=1 checks the node tree at load and reports bad links,
   * naming the model instead of crashing later. */
  {
      extern void portModelAuditWalk(const void *, const void *);
      portModelAuditWalk(header, header->RootNode);
  }
#endif
  modelInitRwData(objinst, header->RootNode);
}


// PD: animInit
void animInit(struct Model *objinst, struct ModelFileHeader *header, u32 *data)
{
    modelInit(objinst, header, data);
    objinst->anim = NULL;
    objinst->anim2 = NULL;
    objinst->animlooping = 0;
    objinst->animflipfunc = 0;
    objinst->unk9c = 0;
    objinst->unka0 = 0;
    objinst->unk2c = 0.0f;
    objinst->timespeed = 0.0f;
    objinst->unk5c = 0.0f;
    objinst->unk7c = 0.0f;
    objinst->unk84 = 0.0f;
    objinst->unk88 = 0.0f;
    objinst->unkb0 = 0.0f;
    objinst->speed = 1.0f;
    objinst->speed2 = 1.0f;
    objinst->playspeed = 1.0f;
    objinst->anim_translation_scale = 1.0f;
    objinst->endframe = -1.0f;
    objinst->unk6c = -1.0f;
}


// PD: model00023108
void modelAttachPart(Model *pmodel, ModelFileHeader *pmodeldef, ModelNode *pnode, ModelFileHeader *cmodeldef)
{
    ModelRwData_HeadPlaceholderRecord *rwdata = modelGetNodeRwData(pmodel, pnode);
    ModelNode *node;

    rwdata->ModelFileHeader = cmodeldef;
    rwdata->RwDatas = &pmodel->datas[pmodeldef->numRecords];

    pnode->Child = cmodeldef->RootNode;

    node = pnode->Child;

    while (node)
    {
        node->Parent = pnode;
        node = node->Next;
    }

    pmodeldef->numRecords += modelCalculateRwDataIndexes(pnode->Child);
}


/**
 * This function can be called repeatedly to iterate a model's display lists.
 *
 * On the first call, the value passed as nodeptr should point to a NULL value.
 * Each time the function is called, it will update *gdlptr to point to the next
 * display list, and will update *nodeptr to point to the current node. On
 * subsequent calls, the same values should be passed as nodeptr and gdlptr so
 * the function can continue correctly.
 *
 * Note that some node types support multiple display lists, so the function
 * may return the same node while it iterates the display lists for that node.
 */
void modelIterateDisplayLists(ModelFileHeader *fileheader, ModelNode **nodeptr, Gfx **gdlptr)
{
    ModelNode *node = *nodeptr;
    union ModelRoData *rodata;
    Gfx *gdl = NULL;

    if (node == NULL)
    {
        node = fileheader->RootNode;
    }

    while (node)
    {
        u32 type = node->Opcode & 0xff;

        switch (type)
        {
            case MODELNODE_OPCODE_DL:
                rodata = node->Data;

                if (node != *nodeptr)
                {
                    gdl = rodata->DisplayList.Primary;
                }
                else if (rodata->DisplayList.Secondary != *gdlptr)
                {
                    gdl = rodata->DisplayList.Secondary;
                }
                break;

            case MODELNODE_OPCODE_DLCOLLISION:
                rodata = node->Data;

                if (node != *nodeptr)
                {
                    gdl = rodata->DisplayListCollisions.Primary;
                }
                else if (rodata->DisplayListCollisions.Secondary != *gdlptr)
                {
                    gdl = rodata->DisplayListCollisions.Secondary;
                }
                break;

            case MODELNODE_OPCODE_DLPRIMARY:
                rodata = node->Data;

                if (node != *nodeptr)
                {
                    gdl = rodata->DisplayListPrimary.Primary;
                }
                break;

            case MODELNODE_OPCODE_LOD:
                rodata = node->Data;
                node->Child = rodata->LOD.Affects;
                break;

            case MODELNODE_OPCODE_SWITCH:
                rodata = node->Data;
                node->Child = rodata->Switch.Controls;
                break;

            case MODELNODE_OPCODE_BSP:
                modelApplyReorderRelationsByArg(node, TRUE);
                break;
        }

        if (gdl) { break; }

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

    *gdlptr = gdl;
    *nodeptr = node;
}


void modelNodeReplaceGdl(u32 arg0, ModelNode *node, Gfx *find, Gfx *replacement)
{
    union ModelRoData *rodata;
    u32 type = node->Opcode & 0xff;

    switch (type) {
        case MODELNODE_OPCODE_DL:
            rodata = node->Data;

            if (rodata->DisplayList.Primary == find)
            {
                rodata->DisplayList.Primary = replacement;
                return;
            }

            if (rodata->DisplayList.Secondary == find)
            {
                rodata->DisplayList.Secondary = replacement;
                return;
            }
            break;

        case MODELNODE_OPCODE_DLCOLLISION:
            rodata = node->Data;

            if (rodata->DisplayListCollisions.Primary == find)
            {
                rodata->DisplayListCollisions.Primary = replacement;
                return;
            }

            if (rodata->DisplayListCollisions.Secondary == find)
            {
                rodata->DisplayListCollisions.Secondary = replacement;
                return;
            }
            break;

        case MODELNODE_OPCODE_DLPRIMARY:
            rodata = node->Data;

            if (rodata->DisplayListPrimary.Primary == find)
            {
                rodata->DisplayListPrimary.Primary = replacement;
                return;
            }
            break;
    }
}
