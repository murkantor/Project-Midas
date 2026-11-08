#include <ultra64.h>
#include <memp.h>
#include "objecthandler.h"
#include "model.h"

// bss
//CODE.bss:80076A50
#ifndef TARGET_N64
/* PORT: on N64 the ModelHitEntry[600] pool is just the adjacent bss stand-ins
 * below, which the free-list init walks by adjacency. GCC doesn't keep bss
 * order (the init used to corrupt unrelated globals), so on PC the pool gets
 * its real size here and the stand-ins are N64-only. */
char g_ModelHitEntries[600 * sizeof(ModelHitEntry)];
#else
char g_ModelHitEntries[0xC];
//CODE.bss:80076A5C
u32 dword_CODE_bss_80076A5C;
//CODE.bss:80076A60
u32 dword_CODE_bss_80076A60;
//CODE.bss:80076A64;
u32 dword_CODE_bss_80076A64;
//CODE.bss:80076A68;
u32 dword_CODE_bss_80076A68;
//CODE.bss:80076A6C;
u32 dword_CODE_bss_80076A6C;
//CODE.bss:80076A70
u32 dword_CODE_bss_80076A70;
//CODE.bss:80076A74
u32 dword_CODE_bss_80076A74;
//CODE.bss:80076A78
char dword_CODE_bss_80076A78[0xC];
//CODE.bss:80076A84
u32 dword_CODE_bss_80076A84;
//CODE.bss:80076A88
u32 dword_CODE_bss_80076A88;
//CODE.bss:80076A8C
char dword_CODE_bss_80076A8C;
char dword_CODE_bss_80076A8D;
char dword_CODE_bss_80076A8E;
char dword_CODE_bss_80076A8F;
char dword_CODE_bss_80076A90[0x10];
//CODE.bss:80076AA0
char dword_CODE_bss_80076AA0[0x14];
//CODE.bss:80076AB4
char dword_CODE_bss_80076AB4;
char dword_CODE_bss_80076AB5;
char dword_CODE_bss_80076AB6;
char dword_CODE_bss_80076AB7;
char dword_CODE_bss_80076AB8[0x10];
//CODE.bss:80076AC8
char dword_CODE_bss_80076AC8[0x14];
//CODE.bss:80076ADC
char dword_CODE_bss_80076ADC;
char dword_CODE_bss_80076ADD;
char dword_CODE_bss_80076ADE;
char dword_CODE_bss_80076ADF;
char dword_CODE_bss_80076AE0[0x2E28];
//CODE.bss:80079908
char g_ModelHitEntriesPenultimate[0x28];
#endif /* TARGET_N64 (pool stand-ins; see g_ModelHitEntries above) */

//CODE.bss:80079930
struct AnimModelSlot *g_AnimModelSlots;
//CODE.bss:80079934
struct ModelSlot *g_ModelSlots;


// data
//D:80036070
s32 g_MaxAnimModelSlots = 0;
//D:80036074
s32 g_MaxModelSlots = 0;
//D:80036078
s32 g_ModelIsLvResetting = 0;
//D:8003607C
u32 D_8003607C = 0;
//D:80036080
u32 D_80036080 = 0;
//D:80036084
s32 g_ModelDistanceDisabled = 0;
//D:80036088
f32 g_ModelDistanceScale = 1.0;
//D:8003608C
struct Vertex* (*vtxallocator)(s32 numvertices) = NULL;
//D:80036090
void (*g_ModelJointPositionedFunc)(s32 mtxindex, Mtxf *mtx) = NULL;
//D:80036094
coord3d D_80036094 = {0};
//D:800360A0
coord3d D_800360A0 = {0};
//D:800360AC
coord3d D_800360AC = {0};
//D:800360B8
coord3d D_800360B8 = {0};
//D:800360C4
struct bondstruct_unk_op07_related D_800360C4[32] = {
    { 0, 0, 0x10 },
    { 1, 0x1000, 0xD },
    { 1, 0x1000, 0xD },
    { 9, 0x800, 0xC },
    { 9, 0x800, 0xC },
    { 0x19, 0x400, 0xB },
    { 0x19, 0x400, 0xB },
    { 0x39, 0x400, 0xB },
    { 0x39, 0x400, 0xB },
    { 0x59, 0x400, 0xB },
    { 0x59, 0x400, 0xB },
    { 0x79, 0x400, 0xB },
    { 0x79, 0x400, 0xB },
    { 0x99, 0x400, 0xB },
    { 0x99, 0x400, 0xB, },
    { 0xB9, 0x400, 0xB, },
    { 0xB9, 0x400, 0xB, },
    { 0xD9, 0x400, 0xB, },
    { 0xD9, 0x400, 0xB, },
    { 0xF9, 0x400, 0xB, },
    { 0xF9, 0x400, 0xB, },
    { 0x119, 0x400, 0xB, },
    { 0x119, 0x400, 0xB, },
    { 0x139, 0x400, 0xB, },
    { 0x139, 0x400, 0xB, },
    { 0x159, 0x400, 0xB, },
    { 0x159, 0x400, 0xB, },
    { 0x179, 0x800, 0xC, },
    { 0x179, 0x800, 0xC, },
    { 0x189, 0x1000, 0xD },
    { 0x189, 0x1000, 0xD },
    { 0x191, 0, 0x10 },
};

//D:80036244
coord3d D_80036244 = { 0 };
//D:80036250
u32 g_ModelAnimMergingEnabled = 1;
//D:80036254
coord3d D_80036254 = { 0 };

//D:80036260
u32 D_80036260 = 0;
//D:80036264
u32 D_80036264 = 0;
//D:80036268
u32 D_80036268 = 0x10;
//D:8003626C
u32 D_8003626C = 1;
//D:80036270
u32 D_80036270 = 0x1000;
//D:80036274
u32 D_80036274 = 0xD;
//D:80036278
u32 D_80036278 = 1;
//D:8003627C
u32 D_8003627C = 0x1000;
//D:80036280
u32 D_80036280 = 0xD;
//D:80036284
u32 D_80036284 = 9;
//D:80036288
u32 D_80036288 = 0x800;
//D:8003628C
u32 D_8003628C = 0xC;
//D:80036290
u32 D_80036290 = 9;
//D:80036294
u32 D_80036294 = 0x800;
//D:80036298
u32 D_80036298 = 0xC;
//D:8003629C
u32 D_8003629C = 0x19;
//D:800362A0
u32 D_800362A0 = 0x800;
//D:800362A4
u32 D_800362A4 = 0xC;
//D:800362A8
u32 D_800362A8 = 0x19;
//D:800362AC
u32 D_800362AC = 0x800;
//D:800362B0
u32 D_800362B0 = 0xC;
//D:800362B4
u32 D_800362B4 = 0x29;
//D:800362B8
u32 D_800362B8 = 0x800;
//D:800362BC
u32 D_800362BC = 0xC;
//D:800362C0
u32 D_800362C0 = 0x29;
//D:800362C4
u32 D_800362C4 = 0x800;
//D:800362C8
u32 D_800362C8 = 0xC;
//D:800362CC
u32 D_800362CC = 0x39;
//D:800362D0
u32 D_800362D0 = 0x800;
//D:800362D4
u32 D_800362D4 = 0xC;
//D:800362D8
u32 D_800362D8 = 0x39;
//D:800362DC
u32 D_800362DC = 0x800;
//D:800362E0
u32 D_800362E0 = 0xC;
//D:800362E4
u32 D_800362E4 = 0x49;
//D:800362E8
u32 D_800362E8 = 0x800;
//D:800362EC
u32 D_800362EC = 0xC;
//D:800362F0
u32 D_800362F0 = 0x49;
//D:800362F4
u32 D_800362F4 = 0x800;
//D:800362F8
u32 D_800362F8 = 0xC;
//D:800362FC
u32 D_800362FC = 0x59;
//D:80036300
u32 D_80036300 = 0x1000;
//D:80036304
u32 D_80036304 = 0xD;
//D:80036308
u32 D_80036308 = 0x59;
//D:8003630C
u32 D_8003630C = 0x1000;
//D:80036310
u32 D_80036310 = 0xD;
//D:80036314
u32 D_80036314 = 0x61;
//D:80036318
u32 D_80036318 = 0;
//D:8003631C
u32 D_8003631C[] = {
  0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

//D:800363EC0
Vertex D_800363E0 = {
    { 0, 0, 0 },
    0,
    { 0, 0 },
    0xFF,
    0xFF,
    0xFF,
    0xFF
};

//D:800363F0
u32 D_800363F0 = 0x50;
//D:800363F4
u32 D_800363F4 = 0;
//D:800363F8
Vtx D_800363F8 = {
    {
        { 0, 0, 0 },
        0,
        { 0, 0 },
        { 0xFF, 0xFF, 0xFF, 0x50 }
    }
};
//D:80036408
coord3d D_80036408 = { 1.0f, 0.0f, 0.0f };
//D:80036414
struct bondstruct_unk_animation_related* D_80036414 =  0;
//D:80036418
intptr_t D_80036418 =  0;
//D:8003641C
s32 D_8003641C =  0;




/*
*/

ModelHitEntry* sub_GAME_7F06B120(ModelHitEntry* head, Model* context) {
    ModelHitEntry* freeListCursor;
    ModelNode* sceneCursor;
    ModelNode* childPtr;
    s32 nodeType;

    sceneCursor = context->obj->RootNode;
    freeListCursor = g_ModelHitFreeList;

    while ((sceneCursor != NULL) && (freeListCursor != NULL)) {
        nodeType = sceneCursor->Opcode & 0xFF;

        switch (nodeType) {
        case 1:
        case 2:
        case 3:
        case 0xb:
        case 0xc:
        case 0xd:
        case 0xe:
        case 0xf:
        case 0x10:
        case 0x15:
            freeListCursor->model = context;
            freeListCursor->rootnode = sceneCursor;
            freeListCursor = freeListCursor->next;
            break;

        default:
            break;
        }

        childPtr = sceneCursor->Child;
        if (childPtr != NULL) {
            sceneCursor = childPtr;
            continue;
        }

        while (sceneCursor != NULL) {
            childPtr = sceneCursor->Next;
            if (childPtr != NULL) {
                sceneCursor = childPtr;
                break;
            }
            sceneCursor = sceneCursor->Parent;
        }
    }

    if (freeListCursor != g_ModelHitFreeList) {
        if (head != NULL) {
            ModelHitEntry *tail = head;

            while (tail->next != NULL) {
                tail = tail->next;
            }

            tail->next = g_ModelHitFreeList;
            g_ModelHitFreeList->prev = tail;
        } else {
            head = g_ModelHitFreeList;
        }

        if (freeListCursor != NULL) {
            ModelHitEntry* prevNode = freeListCursor->prev;
            if (prevNode != NULL) {
                prevNode->next = NULL;
                freeListCursor->prev = NULL;
            }
        }

        g_ModelHitFreeList = freeListCursor;
    }

    return head;
}


void sub_GAME_7F06B248(ModelHitEntry *entry)
{
    ModelHitEntry *oldhead;
    ModelHitEntry *tail;
    
    if (entry != NULL) {
        oldhead = g_ModelHitFreeList;
        if (oldhead != NULL) {
            tail = entry;
            while (tail->next != NULL) {
                tail = tail->next;
            }
            tail->next = oldhead;
            g_ModelHitFreeList->prev = tail;
        }
        g_ModelHitFreeList = entry;
    }
}


#define OP16_NODEINDEX_0C(data) (((ModelNode_Op16Record *)(data))->nodeindex0c)
#define OP16_NODEINDEX_0E(data) (((ModelNode_Op16Record *)(data))->nodeindex0e)
#define OP16_NODEINDEX_10(data) (((ModelNode_Op16Record *)(data))->nodeindex10)

#define OP16_POS_X_VOL(data) (((volatile ModelNode_Op16Record *)(data))->pos.f[0])
#define OP16_POS_Y_VOL(data) (((volatile ModelNode_Op16Record *)(data))->pos.f[1])
#define OP16_POS_Z_VOL(data) (((volatile ModelNode_Op16Record *)(data))->pos.f[2])

/**
 * Address: 7F06B29C
 */
void sub_GAME_7F06B29C(ModelHitEntry *arg0)
{
    ModelHitEntry *entry;
    ModelHitEntry *special;

    entry = arg0;
    special = NULL;

    while (arg0 != NULL) {
        ModelNode *node = arg0->rootnode;
        u16 opcode = node->Opcode;

        switch (opcode & 0xFF)
        {
            case MODELNODE_OPCODE_HEADER:
            {
                union ModelRoData *data;
                ModelNode *othernode;
                Mtxf *mtx;

                data = node->Data;
                othernode = data->Header.FirstGroupNode;

                mtx = modelFindNodeMtx(arg0->model, node, 0);

                if (othernode != NULL)
                {
                    Mtxf *othermtx;

                    othermtx = modelFindNodeMtx(arg0->model, othernode, 0);
                    arg0->sortvalue = -(mtx->m[3][2] + othermtx->m[3][2]) * 0.5f;
                }
                else
                {
                    arg0->sortvalue = -mtx->m[3][2];
                }
                break;
            }

            case MODELNODE_OPCODE_GROUP:
            {
                union ModelRoData *data;
                ModelNode *othernode;
                Mtxf *mtx;

                data = node->Data;
                othernode = data->Group.ChildGroupNode;

                mtx = modelFindNodeMtx(arg0->model, node, 0);

                if (othernode != NULL)
                {
                    Mtxf *othermtx;

                    othermtx = modelFindNodeMtx(arg0->model, othernode, 0);
                    arg0->sortvalue = -(mtx->m[3][2] + othermtx->m[3][2]) * 0.5f;
                }
                else
                {
                    arg0->sortvalue = -mtx->m[3][2];
                }
                break;
            }

            case MODELNODE_OPCODE_OP03:
            {
                union ModelRoData *data;
                ModelNode *othernode;
                Mtxf *mtx;

                data = node->Data;
                othernode = data->Group.ChildGroupNode;

                mtx = modelFindNodeMtx(arg0->model, node, 0);

                if (othernode != NULL)
                {
                    Mtxf *othermtx;

                    othermtx = modelFindNodeMtx(arg0->model, othernode, 0);
                    arg0->sortvalue = -(mtx->m[3][2] + othermtx->m[3][2]) * 0.5f;
                }
                else
                {
                    arg0->sortvalue = -mtx->m[3][2];
                }
                break;
            }

            case MODELNODE_OPCODE_GROUPSIMPLE:
            {
                Mtxf *mtx;

                mtx = modelFindNodeMtx(arg0->model, node, 0);
                arg0->sortvalue = -mtx->m[3][2];
                break;
            }

            case MODELNODE_OPCODE_OP14:
            {
                Mtxf *mtx;
                f32 *data;

                mtx = modelFindNodeMtx(arg0->model, node, 0);
                data = (f32 *)node->Data;

                arg0->sortvalue = -(data[0] * mtx->m[0][2]
                                + data[1] * mtx->m[1][2]
                                + data[2] * mtx->m[2][2]
                                + mtx->m[3][2]);
                break;
            }

            case MODELNODE_OPCODE_INTERLINK:
            {
                Mtxf *mtx;
                ModelRoData_InterlinkageRecord *interlinkage;
                f32 sortvalue1;
                f32 sortvalue2;

                mtx = modelFindNodeMtx(arg0->model, node, 0);
                interlinkage = &node->Data->Interlinkage;

                sortvalue1 = -(interlinkage->pos.x * mtx->m[0][2]
                            + interlinkage->pos.y * mtx->m[1][2]
                            + interlinkage->pos.z * mtx->m[2][2]
                            + mtx->m[3][2]);

                sortvalue2 = -(interlinkage->pos2.x * mtx->m[0][2]
                            + interlinkage->pos2.y * mtx->m[1][2]
                            + interlinkage->pos2.z * mtx->m[2][2]
                            + mtx->m[3][2]);

                if (sortvalue1 < sortvalue2)
                {
                    arg0->sortvalue = sortvalue1;
                }
                else
                {
                    arg0->sortvalue = sortvalue2;
                }
                break;
            }

            case MODELNODE_OPCODE_OP16:
            {
                Mtxf *mtx;
                f32 *data;

                mtx = modelFindNodeMtx(arg0->model, node, 0);
                data = (f32 *)node->Data;

                arg0->sortvalue = -(data[0] * mtx->m[0][2]
                                + data[1] * mtx->m[1][2]
                                + data[2] * mtx->m[2][2]
                                + mtx->m[3][2]);

                special = arg0;
                break;
            }

            case MODELNODE_OPCODE_OP11:
            {
                Mtxf *mtx;
                ModelRoData_Op11Record *op11;

                mtx = modelFindNodeMtx(arg0->model, node, 0);
                op11 = &node->Data->Op11;

                arg0->sortvalue = -(op11->pos.x * mtx->m[0][2]
                                + op11->pos.y * mtx->m[1][2]
                                + op11->pos.z * mtx->m[2][2]
                                + mtx->m[3][2]);
                break;
            }

            case MODELNODE_OPCODE_GUNFIRE:
            {
                Mtxf *mtx;
                f32 *data;

                mtx = modelFindNodeMtx(arg0->model, node, 0);
                data = (f32 *)node->Data;

                arg0->sortvalue = -(data[0] * mtx->m[0][2]
                                + data[1] * mtx->m[1][2]
                                + data[2] * mtx->m[2][2]
                                + mtx->m[3][2]);
                break;
            }

            case MODELNODE_OPCODE_SHADOW:
            {
                Mtxf *mtx;
                ModelRoData_ShadowRecord *shadow;
                union ModelRwData *rwdata;

                mtx = modelFindNodeMtx(arg0->model, node, 0);
                shadow = &node->Data->Shadow;

                rwdata = modelGetNodeRwData(arg0->model, shadow->HeaderNode);

                arg0->sortvalue = -(shadow->pos.x * mtx->m[0][2]
                                + (rwdata->Header.ground - rwdata->Header.pos.y) * mtx->m[1][2]
                                + shadow->pos.y * mtx->m[2][2]
                                + mtx->m[3][2]);
                break;
            }
        }

        arg0 = arg0->next;
    }

    if (special != NULL)
    {
        Model *model = special->model;

        if (model->attachedto != NULL)
        {
            ModelNode **switches;
            ModelNode *node0e;
            ModelNode *specialnode;
            void *op16data;
            ModelNode *node0c;
            ModelNode *node10;
            ModelHitEntry *entry0e;
            ModelHitEntry *entry0c;
            ModelHitEntry *entry10;
            Mtxf *mtx;
            f32 axis2dot;
            f32 axis1dot;
            f32 sort0e;
            coord3d axis2;
            coord3d axis1;
            f32 sort0c;
            f32 sort10;
            coord3d transformed;
            ModelHitEntry *scan;
            f32 tempf;

            tempf = special->sortvalue;
            specialnode = special->rootnode;
            op16data = specialnode->Data;

            switches = model->attachedto->obj->Switches;

            node0e = switches[OP16_NODEINDEX_0E(op16data)];
            node0c = switches[OP16_NODEINDEX_0C(op16data)];
            node10 = switches[OP16_NODEINDEX_10(op16data)];

            entry10 = NULL;
            entry0e = NULL;
            entry0c = NULL;


            mtx = modelFindNodeMtx(model, specialnode, 0);
            scan = entry;

            while (scan != NULL)
            {
                if (node0e == scan->rootnode)
                {
                    entry0e = scan;
                }

                if (node0c == scan->rootnode)
                {
                    entry0c = scan;
                }

                if (node10 == scan->rootnode)
                {
                    entry10 = scan;
                }

                scan = scan->next;
            }

            axis2.f[0] = mtx->m[2][0];
            axis2.f[1] = mtx->m[2][1];
            axis2.f[2] = mtx->m[2][2];

            axis1.f[0] = mtx->m[1][0];
            axis1.f[1] = mtx->m[1][1];
            axis1.f[2] = mtx->m[1][2];

            transformed.f[0] = OP16_POS_X_VOL(op16data) * mtx->m[0][0]
                          + OP16_POS_Y_VOL(op16data) * mtx->m[1][0]
                          + OP16_POS_Z_VOL(op16data) * mtx->m[2][0]
                          + mtx->m[3][0];
            
            transformed.f[1] = OP16_POS_X_VOL(op16data) * mtx->m[0][1]
                          + OP16_POS_Y_VOL(op16data) * mtx->m[1][1]
                          + OP16_POS_Z_VOL(op16data) * mtx->m[2][1]
                          + mtx->m[3][1];
            
            transformed.f[2] = OP16_POS_X_VOL(op16data) * mtx->m[0][2]
                          + OP16_POS_Y_VOL(op16data) * mtx->m[1][2]
                          + OP16_POS_Z_VOL(op16data) * mtx->m[2][2]
                          + mtx->m[3][2];

            axis2dot = axis2.f[0] * transformed.f[0]
                     + axis2.f[1] * transformed.f[1]
                     + axis2.f[2] * transformed.f[2];
            axis1dot = axis1.f[0] * transformed.f[0]
                     + axis1.f[1] * transformed.f[1]
                     + axis1.f[2] * transformed.f[2];
            
            
            sort0e = entry0e->sortvalue;
            sort0c = entry0c->sortvalue;
            sort10 = entry10->sortvalue;
            if (axis2dot < 0.0f)
            {
                if (sort10 < sort0e)
                {
                    if (sort10 < tempf)
                    {
                        special->sortvalue = sort10 - 0.000030517578125f; // 2/65536
                    }
                }
                else
                {
                    if (sort0e < tempf)
                    {
                        special->sortvalue = sort0e - 0.000030517578125f; // 2/65536
                    }
                }
            }
            else if (0.0f <= axis2dot)
            {
                if (tempf < sort0e)
                {
                    entry0e->sortvalue = tempf - 0.00006103515625f; // 4/65536
                }

                if (tempf < sort10)
                {
                    if (sort10 < sort0e)
                    {
                        entry10->sortvalue = tempf - 0.000091552734375f; // 6/65536
                    }
                    else
                    {
                        entry10->sortvalue = tempf - 0.000030517578125f; // 2/65536
                    }
                }
            }

            if (axis1dot < 0.0f)
            {
                if (sort0c < special->sortvalue)
                {
                    if (entry0e->sortvalue < special->sortvalue && sort0c < entry0e->sortvalue)
                    {
                        entry0e->sortvalue = sort0c - 0.00006103515625f; // 4/65536
                    }

                    if (entry10->sortvalue < special->sortvalue && sort0c < entry10->sortvalue)
                    {
                        if (sort10 < sort0e)
                        {
                            entry10->sortvalue = sort0c - 0.000091552734375f; // 6/65536
                        }
                        else
                        {
                            entry10->sortvalue = sort0c - 0.000030517578125f; // 2/65536
                        }
                    }

                    special->sortvalue = sort0c - 0.0000152587890625f; // 1/65536
                }
            }
            else if (0.0f <= axis1dot)
            {
                if (special->sortvalue < sort0c)
                {
                    entry0c->sortvalue = special->sortvalue - 0.0000152587890625f; // 1/65536

                    if (sort0c < sort0e)
                    {
                        if (entry0e->sortvalue < entry0c->sortvalue)
                        {
                            entry0c->sortvalue = entry0e->sortvalue - 0.0000152587890625f; // 1/65536
                        }
                    }

                    if (sort0c < sort10)
                    {
                        if (entry10->sortvalue < entry0c->sortvalue)
                        {
                            entry0c->sortvalue = entry10->sortvalue - 0.0000152587890625f; // 1/65536
                        }
                    }
                }
            }
        }
    }
}

#undef OP16_NODEINDEX_0C
#undef OP16_NODEINDEX_0E
#undef OP16_NODEINDEX_10
#undef OP16_POS_X_VOL
#undef OP16_POS_Y_VOL
#undef OP16_POS_Z_VOL


/**
 * Address: 7F06BB28
 */
ModelHitEntry *sub_GAME_7F06BB28(ModelHitEntry *modelhit)
{
    ModelHitEntry stacknodes[2];
    ModelHitEntry *last;
    ModelHitEntry *current;
    ModelHitEntry *next;
    ModelHitEntry *scan;
    ModelHitEntry *best;
    f32 bestvalue;

    if (modelhit != NULL)
    {
        last = modelhit;

        if (last->next != NULL)
        {
            do
            {
                last = last->next;

                if (next);
            }
            while (last->next != NULL);
        }

        stacknodes[1].next = modelhit;
        modelhit->prev = &stacknodes[1];

        stacknodes[0].prev = last;
        last->next = &stacknodes[0];

        current = &stacknodes[1];

        do
        {
            next = current->next;
            best = NULL;
            bestvalue = -M_U32_MAX_VALUE_F;

            if (next != &stacknodes[0])
            {
                scan = next;

                do
                {
                    if (bestvalue < scan->sortvalue)
                    {
                        bestvalue = scan->sortvalue;
                        best = scan;
                    }

                    scan = scan->next;
                }
                while (scan != &stacknodes[0]);
            }

            if (best != NULL)
            {
                best->next->prev = best->prev;
                best->prev->next = best->next;

                best->prev = current;
                best->next = current->next;

                current->next->prev = best;
                current->next = best;

                next = best;
            }

            current = next;
        }
        while (next != &stacknodes[0]);

        modelhit = stacknodes[1].next;
        stacknodes[1].next->prev = NULL;
        stacknodes[0].prev->next = NULL;
    }

    return modelhit;
}


#if defined(VERSION_US) || defined(VERSION_JP)
void drawjointlist(ModelRenderData *data, ModelHitEntry *entry)
{
    ModelNode *root;
    ModelNode *node;
    s32 descend;
    Gfx *gdl;
    s32 opcode;

    if (data->gdl == NULL)
    {
        osSyncPrintf("drawjointlist: no gfxlist!\n");
        return_null();
    }

    while (entry != NULL)
    {
#ifndef TARGET_N64
        { /* bring-up trace: entries seen (see gfx_pc.cpp GE_GFX_STATS) */
            extern u32 ge_dbg0;
            ge_dbg0++;
        }
#endif
        root = entry->rootnode;
        node = root;

        if (entry->model->obj == NULL)
        {
            osSyncPrintf("drawjointlist: no object! (0x%X)\n", entry->model);
            return_null();
        }

        if (!entry->model->obj->isLoaded)
        {
            osSyncPrintf("drawjointlist: object not initialised! (0x%X)\n", entry->model->obj);
            return_null();
        }

        if (data->unk18 != 0)
        {
            if (entry->sortvalue < getjointsize(entry->model, root))
            {
                node = NULL;
            }
        }
        else
        {
            gdl = data->gdl++;
            gSPSegment(gdl, SPSEGMENT_MODEL_MTX, osVirtualToPhysical(entry->model->render_pos));
        }

        if (node != NULL)
        {
            do
            {
#ifndef TARGET_N64
                { /* bring-up trace: nodes walked */
                    extern u32 ge_dbg1;
                    ge_dbg1++;
                }
#endif
                descend = 1;
                opcode = node->Opcode & 0xff;

                switch (opcode)
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
                    if (node == root)
                    {
                        sub_GAME_7F074534(data, entry->model, node);
                    }
                    else
                    {
                        descend = 0;
                    }
                    break;

                case MODELNODE_OPCODE_DL:
                case MODELNODE_OPCODE_OP05:
                case MODELNODE_OPCODE_OP06:
                case MODELNODE_OPCODE_OP07:
                case MODELNODE_OPCODE_LOD:
                case MODELNODE_OPCODE_BSP:
                case MODELNODE_OPCODE_BBOX:
                case MODELNODE_OPCODE_OP17:
                case MODELNODE_OPCODE_SWITCH:
                case MODELNODE_OPCODE_OP19:
                case MODELNODE_OPCODE_OP20:
                case MODELNODE_OPCODE_DLPRIMARY:
                case MODELNODE_OPCODE_HEAD:
                case MODELNODE_OPCODE_DLCOLLISION:
                default:
                    sub_GAME_7F074534(data, entry->model, node);
                    break;
                }

                if (descend && node->Child != NULL)
                {
                    node = node->Child;
                }
                else if (node != NULL)
                {
                    do
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
                    while (node != NULL);
                }
            }
            while (node != NULL);
        }

        entry = entry->next;
    }
}
#endif


#if defined(VERSION_EU)
void drawjointlist(ModelRenderData *data, ModelHitEntry *entry)
{
    ModelNode *root;
    ModelNode *node;
    s32 descend;
    Gfx *gdl;
    s32 opcode;

    while (entry != NULL)
    {
        root = entry->rootnode;
        node = root;

        if (data->unk18 != 0)
        {
            if (entry->sortvalue < getjointsize(entry->model, root))
            {
                node = NULL;
            }
        }
        else
        {
            gdl = data->gdl++;
            gSPSegment(gdl, SPSEGMENT_MODEL_MTX, osVirtualToPhysical(entry->model->render_pos));
        }

        if (node != NULL)
        {
            do
            {
#ifndef TARGET_N64
                { /* bring-up trace: nodes walked */
                    extern u32 ge_dbg1;
                    ge_dbg1++;
                }
#endif
                descend = 1;
                opcode = node->Opcode & 0xff;

                switch (opcode)
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
                    if (node == root)
                    {
                        sub_GAME_7F074534(data, entry->model, node);
                    }
                    else
                    {
                        descend = 0;
                    }
                    break;

                case MODELNODE_OPCODE_DL:
                case MODELNODE_OPCODE_OP05:
                case MODELNODE_OPCODE_OP06:
                case MODELNODE_OPCODE_OP07:
                case MODELNODE_OPCODE_LOD:
                case MODELNODE_OPCODE_BSP:
                case MODELNODE_OPCODE_BBOX:
                case MODELNODE_OPCODE_OP17:
                case MODELNODE_OPCODE_SWITCH:
                case MODELNODE_OPCODE_OP19:
                case MODELNODE_OPCODE_OP20:
                case MODELNODE_OPCODE_DLPRIMARY:
                case MODELNODE_OPCODE_HEAD:
                case MODELNODE_OPCODE_DLCOLLISION:
                default:
                    sub_GAME_7F074534(data, entry->model, node);
                    break;
                }

                if (descend && node->Child != NULL)
                {
                    node = node->Child;
                }
                else if (node != NULL)
                {
                    do
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
                    while (node != NULL);
                }
            }
            while (node != NULL);
        }

        entry = entry->next;
    }
}
#endif


s32 probably_damage_detail_blood_effect_related(ModelHitEntry **entryptr, coord3d *raypos, coord3d *raydir, Model **outModel, ModelNode **inoutNode)
{
    ModelHitEntry *entry = *entryptr;
    ModelNode *node;
    s32 descend;
    ModelNode *root;
    ModelNode *next;

    while (entry != NULL) {
        ModelNode *resume = *inoutNode;

        root = entry->rootnode;
        descend = TRUE;

        if (resume != NULL) {
            node = resume;
            *inoutNode = NULL;
        } else {
            node = root;
        }

        while (node != NULL) {
            if (descend && node->Child != NULL) {
                node = node->Child;
            } else {
                if (node != NULL) {
walk_node:
                    if (node == root) {
                        node = NULL;
                    } else {
                        next = node->Next;

                        if (next != NULL) {
                            node = next;
                        } else {
                            node = node->Parent;

                            if (node != NULL) {
                                goto walk_node;
                            }
                        }
                    }
                }

                if (node == NULL) {
                    break;
                }
            }

            descend = TRUE;
            
            {
                u16 opcode = node->Opcode;
            
                descend = TRUE;
            
                switch (opcode & 0xff) {
                case MODELNODE_OPCODE_HEADER:
                case MODELNODE_OPCODE_GROUP:
                case MODELNODE_OPCODE_OP03:
                case MODELNODE_OPCODE_GROUPSIMPLE:
                    descend = FALSE;
                    break;
            
                case MODELNODE_OPCODE_OP11:
                case MODELNODE_OPCODE_GUNFIRE:
                case MODELNODE_OPCODE_SHADOW:
                case MODELNODE_OPCODE_OP14:
                case MODELNODE_OPCODE_INTERLINK:
                case MODELNODE_OPCODE_OP16:
                    descend = FALSE;
                    break;
            
                case MODELNODE_OPCODE_BBOX:
                    if (modelTestRayIntersectsNodeBBox(entry->model, node, raypos, raydir)) {
                        *outModel = entry->model;
                        *inoutNode = node;
                        *entryptr = entry;
            
                        return *(s32 *)node->Data;
                    }
            
                    descend = FALSE;
                    break;
            
                case MODELNODE_OPCODE_OP17:
                    if (sub_GAME_7F074CAC(entry->model, node, raypos, raydir)) {
                        *outModel = entry->model;
                        *inoutNode = node;
                        *entryptr = entry;
            
                        return *(s32 *)node->Data;
                    }
            
                    descend = FALSE;
                    break;
            
                case MODELNODE_OPCODE_LOD:
                    modelApplyDistanceRelations(entry->model, node);
                    break;
            
                case MODELNODE_OPCODE_SWITCH:
                    modelApplyToggleRelations(entry->model, node);
                    break;
            
                case MODELNODE_OPCODE_HEAD:
                    modelApplyHeadRelations(entry->model, node);
                    break;
            
                case MODELNODE_OPCODE_DLCOLLISION:
                default:
                    break;
                }
            }
        }

        entry = entry->prev;
    }

    *entryptr = NULL;
    return 0;
}


/**
 * Address: 0x7F06C010
* https://decomp.me/scratch/IDiXU
 * #MATCH! Unlikley match, not sure why we are setting the root node to the last node.
 
f32 sub_GAME_7F06C010(ModelFileHeader *head, s32 unused, s32 unused2, s32 *arg3, s32 *arg4)
{
    ModelNode *lastnode = head->RootNode;

    while (lastnode->Next != NULL)
    {
        lastnode = lastnode->Next;
    }

    head->RootNode = lastnode;
    *arg3          = 0;
    *arg4          = 0;
    return probably_damage_detail_blood_effect_related(head, unused, unused2, arg3, arg4);
}*/


/**
 * Address: 7F06C010
 * 
 * Update: decompiling probably_damage_detail_blood_effect_related revealed that the first argument for this function is a ModelHitEntry.
 * The previous function matched because both ModelNode and ModelHitEntry happen to have a normal 32 bit field at offset 0x0c.
 */
s32 sub_GAME_7F06C010(ModelHitEntry **entryptr, coord3d *modelRayStart, coord3d *modelRayDir, Model **outModel, ModelNode **outNode)
{
    ModelHitEntry *entry = *entryptr;

    while (entry->next != NULL) 
    {
        entry = entry->next;
    }

    *entryptr = entry;
    *outModel = NULL;
    *outNode = NULL;

    return probably_damage_detail_blood_effect_related( entryptr, modelRayStart, modelRayDir, outModel, outNode);
}

