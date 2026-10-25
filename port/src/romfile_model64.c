/* Adapted from MGB64 (MIT), commit 0d1d40b4cde18302ec9789853a50f5545e679062.
 * See tools/THIRD_PARTY_NOTICES.txt for the copyright and license. */
/* N64 model files contain 32-bit segment-0x05 offsets. Convert their nodes,
 * read-only records, textures and display lists into native pointer layouts. */

#ifdef GE_NATIVE_64

#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "romfile_model64.h"

/* ------------------------------------------------------------------ */
/* Native allocation tracker                                           */
/* ------------------------------------------------------------------ */
/* Track native model allocations for release when the stage pool resets. */

typedef struct NativeAllocNode {
    struct NativeAllocNode *next;
    void *ptr;
} NativeAllocNode;

static NativeAllocNode *s_nativeAllocHead = NULL;
static void clearCollisionLinks(void);

/* Tracked calloc: allocates memory and records it for later bulk-free. */
static void *trackedCalloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (ptr) {
        NativeAllocNode *node = (NativeAllocNode *)malloc(sizeof(NativeAllocNode));
        if (node) {
            node->ptr = ptr;
            node->next = s_nativeAllocHead;
            s_nativeAllocHead = node;
        }
    }
    return ptr;
}

void modelConvertFreeAll(void) {
    clearCollisionLinks();
    NativeAllocNode *cur = s_nativeAllocHead;
    while (cur) {
        NativeAllocNode *next = cur->next;
        free(cur->ptr);
        free(cur);
        cur = next;
    }
    s_nativeAllocHead = NULL;
}

/* ------------------------------------------------------------------ */
/* N64 binary layout constants                                        */
/* ------------------------------------------------------------------ */

#define N64_VMA_BASE  0x05000000u

/* N64 ModelNode: 24 bytes */
#define N64_NODE_SIZE    24
#define N64_OFF_OPCODE   0   /* u16 */
#define N64_OFF_DATA     4   /* u32 ptr */
#define N64_OFF_PARENT   8   /* u32 ptr */
#define N64_OFF_NEXT     12  /* u32 ptr */
#define N64_OFF_PREV     16  /* u32 ptr */
#define N64_OFF_CHILD    20  /* u32 ptr */

/* N64 rodata sizes (bytes on N64, where pointers = 4 bytes) */
#define N64_RODATA_HEADER    16  /* u32 + ptr + u16×2 + u16×2 */
#define N64_RODATA_GROUP     28  /* 3f32 + u16 + 3s16 + ptr + f32 */
#define N64_RODATA_DL        20  /* 3ptr + ptr + u16 + s8 + pad */
#define N64_RODATA_LOD       16  /* 2f32 + ptr + u16×2 */
#define N64_RODATA_SWITCH    8   /* ptr + u16×2 */
#define N64_RODATA_BSP       36  /* 6f32 + 2ptr + s16 + u16 */
#define N64_RODATA_BBOX      28  /* u32 + 6f32 */
#define N64_RODATA_OP17      32  /* s32 + f32 + 3f32 + ptr + 2f32 */
#define N64_RODATA_GROUPSIMPLE 20 /* 3f32 + s16 + u16 + f32 */
#define N64_RODATA_DLPRIMARY 16  /* s32 + ptr + ptr + ptr */
#define N64_RODATA_OP06      24  /* 5u32 + ptr */
#define N64_RODATA_OP11      72  /* 16u32 + f32 + u16×2 + ptr */
#define N64_RODATA_GUNFIRE   40  /* 6f32 + ptr + f32 + u16×2 + u32 */
#define N64_RODATA_SHADOW    32  /* 4f32 + 2ptr + f32 + ptr */
#define N64_RODATA_INTERLINK 28  /* 3f32 + 3u32 + f32 */
#define N64_RODATA_OP16      24  /* 3f32 + 2u32 + f32 */
#define N64_RODATA_DLCOLL    32  /* 2ptr + ptr + 2s16 + ptr + ptr + s16 + u16 + ptr */
#define N64_RODATA_CHILD     8   /* u8 + u8 + u16 + ptr */

/* N64 ModelFileTextures: 12 bytes (u32 + 7×u8 + pad) — same on PC */
#define N64_TEXTURES_SIZE    12

/* ------------------------------------------------------------------ */
/* Byte-reading helpers (big-endian)                                   */
/* ------------------------------------------------------------------ */

static inline u32 rd32(const u8 *p) {
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static inline u16 rd16(const u8 *p) {
    return ((u16)p[0]<<8)|p[1];
}
static inline s16 rds16(const u8 *p) {
    return (s16)(((u16)p[0]<<8)|p[1]);
}
static inline f32 rdf32(const u8 *p) {
    u32 v = rd32(p);
    f32 f;
    memcpy(&f, &v, 4);
    return f;
}

/* ------------------------------------------------------------------ */
/* VMA/offset helpers                                                  */
/* ------------------------------------------------------------------ */

/* Convert N64 VMA to file-data offset. Returns -1 if null/zero. */
static inline s32 vma2off(u32 vma) {
    return (vma && vma >= N64_VMA_BASE) ? (s32)(vma - N64_VMA_BASE) : -1;
}

/* Convert N64 VMA to a pointer into the original file data buffer.
 * Used for raw data blocks (vertices, Gfx commands) that stay in-place. */
static inline void *vma2ptr(u32 vma, u8 *filedata) {
    return (vma && vma >= N64_VMA_BASE) ? (void *)(filedata + (vma - N64_VMA_BASE)) : NULL;
}

/* ------------------------------------------------------------------ */
/* Offset→Pointer mapping table                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    u32   file_off;
    void *native;
} MapEntry;

typedef struct {
    MapEntry *entries;
    int count;
    int capacity;
} OffsetMap;

static void mapInit(OffsetMap *m) {
    m->count = 0;
    m->capacity = 512;
    m->entries = (MapEntry *)calloc(m->capacity, sizeof(MapEntry));
}

static void mapAdd(OffsetMap *m, u32 off, void *ptr) {
    if (m->count >= m->capacity) {
        m->capacity *= 2;
        m->entries = (MapEntry *)realloc(m->entries, m->capacity * sizeof(MapEntry));
    }
    m->entries[m->count].file_off = off;
    m->entries[m->count].native = ptr;
    m->count++;
}

static void *mapLookup(const OffsetMap *m, u32 off) {
    for (int i = 0; i < m->count; i++) {
        if (m->entries[i].file_off == off)
            return m->entries[i].native;
    }
    return NULL;
}

/* Lookup by file offset, converting from VMA first */
static void *mapLookupVMA(const OffsetMap *m, u32 vma) {
    s32 off = vma2off(vma);
    return (off >= 0) ? mapLookup(m, (u32)off) : NULL;
}

static void mapFree(OffsetMap *m) {
    free(m->entries);
    m->entries = NULL;
    m->count = m->capacity = 0;
}

static u8 *sFileData;
static size_t sFileSize;
static u8 *sVertexDone;
static OffsetMap sDisplayLists;

typedef struct CollisionLink {
    const Vertex *vertex;
    ModelNode *node;
    struct CollisionLink *next;
} CollisionLink;
static CollisionLink *sCollisionLinks;
static void clearCollisionLinks(void) { sCollisionLinks = NULL; }

ModelNode *portModelCollisionNode(const Vertex *vertex)
{
    for (CollisionLink *link = sCollisionLinks; link; link = link->next) {
        if (link->vertex == vertex) return link->node;
    }
    return NULL;
}

static Gfx *nativeDisplayList(const Gfx *raw)
{
    if (!raw) return NULL;
    size_t off = (const u8 *)raw - sFileData;
    if (off >= sFileSize) return NULL;
    Gfx *found = mapLookup(&sDisplayLists, (u32)off);
    if (found) return found;
    size_t count = 0;
    while (off + (count + 1) * 8 <= sFileSize) {
        u8 op = sFileData[off + count * 8];
        count++;
        if (op == 0xb8 || op == 0xdf) break;
    }
    if (!count) return NULL;
    Gfx *dl = trackedCalloc(count, sizeof(*dl));
    mapAdd(&sDisplayLists, (u32)off, dl);
    for (size_t i = 0; i < count; i++) {
        const u8 *src = sFileData + off + i * 8;
        u32 w0 = rd32(src), w1 = rd32(src + 4);
        u8 op = w0 >> 24;
        dl[i].words.w0 = w0;
        dl[i].words.w1 = w1;
        if ((w1 >> 24) == 5 && (w1 & 0xffffff) < sFileSize) {
            u8 *target = sFileData + (w1 & 0xffffff);
            if (op == 0x06 || op == 0xde) {
                dl[i].words.w1 = (uintptr_t)nativeDisplayList((Gfx *)target);
            } else if (op == 0x01 || op == 0x03 || op == 0x04 || op == 0xfd) {
                dl[i].words.w1 = (uintptr_t)target;
            }
        }
    }
    return dl;
}

static void nativeVertices(Vertex *vertices, s32 count)
{
    if (!vertices || count <= 0) return;
    size_t off = (u8 *)vertices - sFileData;
    if (off >= sFileSize || (size_t)count > (sFileSize - off) / 16) return;
    for (s32 i = 0; i < count; i++) {
        u8 *p = (u8 *)&vertices[i];
        size_t pos = p - sFileData;
        if (sVertexDone[pos]) continue;
        sVertexDone[pos] = 1;
        for (s32 j = 0; j < 12; j += 2) {
            u8 byte = p[j]; p[j] = p[j + 1]; p[j + 1] = byte;
        }
    }
}

static void nativeModelGeometry(OffsetMap *nodes)
{
    for (s32 i = 0; i < nodes->count; i++) {
        ModelNode *node = nodes->entries[i].native;
        if (!node->Data) continue;
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_DLCOLLISION) {
            ModelRoData_DisplayList_CollisionRecord *ro = &node->Data->DisplayListCollisions;
            for (s32 j = 0; j < ro->numCollisionVertices; j++) {
                Vertex *vertex = &ro->CollisionVertices[j];
                u32 ref = rd32((u8 *)vertex + 8);
                ModelNode *related = mapLookupVMA(nodes, ref);
                if (related) {
                    CollisionLink *link = trackedCalloc(1, sizeof(*link));
                    link->vertex = vertex; link->node = related;
                    link->next = sCollisionLinks; sCollisionLinks = link;
                }
            }
        }
    }
    for (s32 i = 0; i < nodes->count; i++) {
        ModelNode *node = nodes->entries[i].native;
        if (!node->Data) continue;
        switch (node->Opcode & 0xff) {
        case MODELNODE_OPCODE_DL: {
            ModelRoData_DisplayListRecord *ro = &node->Data->DisplayList;
            nativeVertices(ro->Vertices, ro->numVertices);
            ro->Primary = nativeDisplayList(ro->Primary);
            ro->Secondary = nativeDisplayList(ro->Secondary);
            break;
        }
        case MODELNODE_OPCODE_DLPRIMARY: {
            ModelRoData_DisplayListPrimaryRecord *ro = &node->Data->DisplayListPrimary;
            /* Star gunfire stores a quad count; dorottex reads four vertices
             * per entry, unlike the other display-list node types. */
            nativeVertices(ro->Vertices, ro->numVertices * 4);
            ro->Primary = nativeDisplayList(ro->Primary);
            break;
        }
        case MODELNODE_OPCODE_DLCOLLISION: {
            ModelRoData_DisplayList_CollisionRecord *ro = &node->Data->DisplayListCollisions;
            nativeVertices(ro->Vertices, ro->numVertices);
            nativeVertices(ro->CollisionVertices, ro->numCollisionVertices);
            for (s32 j = 0; j < ro->numCollisionVertices; j++) {
                u8 *vertex = (u8 *)&ro->CollisionVertices[j];
                u16 index = rd16(vertex + 12);
                memcpy(vertex + 12, &index, 2);
            }
            for (s32 j = 0; j < ro->numVertices; j++) {
                u16 v = rd16((u8 *)&ro->PointUsage[j]);
                memcpy(&ro->PointUsage[j], &v, 2);
            }
            ro->Primary = nativeDisplayList(ro->Primary);
            ro->Secondary = nativeDisplayList(ro->Secondary);
            break;
        }
        default: break;
        }
    }
}


typedef struct {
    u32 *items;
    int count;
    int capacity;
} OffsetWorkList;

static void workListInit(OffsetWorkList *list)
{
    list->count = 0;
    list->capacity = 128;
    list->items = (u32 *)calloc((size_t)list->capacity, sizeof(u32));
}

static void workListPush(OffsetWorkList *list, s32 off)
{
    if (off < 0) {
        return;
    }

    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->items = (u32 *)realloc(list->items,
                                     (size_t)list->capacity * sizeof(u32));
    }

    list->items[list->count++] = (u32)off;
}

static s32 workListPop(OffsetWorkList *list)
{
    if (list->count <= 0) {
        return -1;
    }

    return (s32)list->items[--list->count];
}

static void workListFree(OffsetWorkList *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* ------------------------------------------------------------------ */
/* Rodata conversion (N64 binary → native struct)                      */
/* ------------------------------------------------------------------ */

static void convertRoData(
    const u8 *filedata,
    ModelNode *node,
    s32 data_off,
    u8 *filedata_mut,   /* mutable for in-place raw data */
    OffsetMap *map)
{
    u16 type = node->Opcode & 0xFF;
    const u8 *dp = filedata + data_off;
    union ModelRoData *rodata = NULL;

    switch (type)
    {
    case MODELNODE_OPCODE_HEADER:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_HeaderRecord *h = &rodata->Header;
        h->AnimPart = rd16(dp);
        h->MatrixIndex = rds16(dp + 2);
        /* h->FirstGroup — deferred to pointer fixup */
        h->Group1 = rd16(dp + 8);
        h->Group2 = rd16(dp + 10);
        h->RwDataIndex = rd16(dp + 12);
        h->reserved = rd16(dp + 14);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_GROUP:
    case MODELNODE_OPCODE_OP03:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_GroupRecord *g = &rodata->Group;
        g->Origin.f[0] = rdf32(dp + 0);
        g->Origin.f[1] = rdf32(dp + 4);
        g->Origin.f[2] = rdf32(dp + 8);
        g->JointID = rd16(dp + 12);
        g->MatrixIDs[0] = rds16(dp + 14);
        g->MatrixIDs[1] = rds16(dp + 16);
        g->MatrixIDs[2] = rds16(dp + 18);
        /* g->ChildGroup — deferred to pointer fixup */
        g->BoundingVolumeRadius = rdf32(dp + 24);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_OP17:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_Op17Record *o = &rodata->Op17;
        o->hitpart = (s32)rd32(dp + 0);
        o->radiusSq = rdf32(dp + 4);
        o->pos.f[0] = rdf32(dp + 8);
        o->pos.f[1] = rdf32(dp + 12);
        o->pos.f[2] = rdf32(dp + 16);
        o->scale1 = rdf32(dp + 24);
        o->scale2 = rdf32(dp + 28);
        /* o->othernode — deferred to pointer fixup */
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_DL:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_DisplayListRecord *d = &rodata->DisplayList;
        /* Display list and vertex data stay in the original file buffer
         * (big-endian, processed by N64 DL interpreter) */
        d->Primary   = (Gfx *)vma2ptr(rd32(dp + 0), filedata_mut);
        d->Secondary = (Gfx *)vma2ptr(rd32(dp + 4), filedata_mut);
        d->BaseAddr  = (void *)filedata_mut;
        d->Vertices  = (Vertex *)vma2ptr(rd32(dp + 12), filedata_mut);
        d->numVertices = rd16(dp + 16);
        d->ModelType = (s8)*(dp + 18);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_DLPRIMARY:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_DisplayListPrimaryRecord *d = &rodata->DisplayListPrimary;
        d->numVertices = (s32)rd32(dp + 0);
        d->Vertices = (Vertex *)vma2ptr(rd32(dp + 4), filedata_mut);
        d->Primary  = (Gfx *)vma2ptr(rd32(dp + 8), filedata_mut);
        d->BaseAddr = (void *)filedata_mut;
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_DLCOLLISION:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_DisplayList_CollisionRecord *c = &rodata->DisplayListCollisions;
        c->Primary   = (Gfx *)vma2ptr(rd32(dp + 0), filedata_mut);
        c->Secondary = (Gfx *)vma2ptr(rd32(dp + 4), filedata_mut);
        c->Vertices  = (Vertex *)vma2ptr(rd32(dp + 8), filedata_mut);
        c->numVertices = rds16(dp + 12);
        c->numCollisionVertices = rds16(dp + 14);
        c->CollisionVertices = (Vertex *)vma2ptr(rd32(dp + 16), filedata_mut);
        c->PointUsage = (s16 *)vma2ptr(rd32(dp + 20), filedata_mut);
        c->ModelType  = rds16(dp + 24);
        c->RwDataIndex = rd16(dp + 26);
        c->BaseAddr   = (void *)filedata_mut;
        /* CollisionVertices[i].LinkedTo fixup is complex — defer */
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_LOD:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_LODRecord *l = &rodata->LOD;
        l->MinDistance = rdf32(dp + 0);
        l->MaxDistance = rdf32(dp + 4);
        /* l->Affects — deferred to pointer fixup (points to a ModelNode) */
        l->RwDataIndex = rd16(dp + 12);
        l->reserved = rd16(dp + 14);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_SWITCH:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_SwitchRecord *s = &rodata->Switch;
        /* s->Controls — deferred to pointer fixup (points to a ModelNode) */
        s->RwDataIndex = rd16(dp + 4);
        s->reserved = rd16(dp + 6);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_BSP:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_BSPRecord *b = &rodata->BSP;
        b->Point.f[0]  = rdf32(dp + 0);
        b->Point.f[1]  = rdf32(dp + 4);
        b->Point.f[2]  = rdf32(dp + 8);
        b->Vector.f[0] = rdf32(dp + 12);
        b->Vector.f[1] = rdf32(dp + 16);
        b->Vector.f[2] = rdf32(dp + 20);
        /* b->leftChild, rightChild — deferred to pointer fixup (ModelNode*) */
        b->reserved    = rds16(dp + 32);
        b->RwDataIndex = rd16(dp + 34);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_BBOX:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_BoundingBoxRecord *bb = &rodata->BoundingBox;
        bb->ModelNumber = rd32(dp + 0);
        /* Authored model sources define bbox records as:
         * { xmin, xmax, ymin, ymax, zmin, zmax }.
         * Placement, collision, and rendering all assume that same axis order. */
        bb->Bounds.xmin = rdf32(dp + 4);
        bb->Bounds.xmax = rdf32(dp + 8);
        bb->Bounds.ymin = rdf32(dp + 12);
        bb->Bounds.ymax = rdf32(dp + 16);
        bb->Bounds.zmin = rdf32(dp + 20);
        bb->Bounds.zmax = rdf32(dp + 24);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_GROUPSIMPLE:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_GroupSimpleRecord *gs = &rodata->GroupSimple;
        gs->Origin.f[0] = rdf32(dp + 0);
        gs->Origin.f[1] = rdf32(dp + 4);
        gs->Origin.f[2] = rdf32(dp + 8);
        gs->Group1 = rds16(dp + 12);
        gs->Group2 = rd16(dp + 14);
        gs->BoundingVolumeRadius = rdf32(dp + 16);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_GUNFIRE:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_GunfireRecord *gf = &rodata->Gunfire;
        gf->Offset.f[0] = rdf32(dp + 0);
        gf->Offset.f[1] = rdf32(dp + 4);
        gf->Offset.f[2] = rdf32(dp + 8);
        gf->Size.f[0]   = rdf32(dp + 12);
        gf->Size.f[1]   = rdf32(dp + 16);
        gf->Size.f[2]   = rdf32(dp + 20);
        {
            void *img_raw = vma2ptr(rd32(dp + 24), filedata_mut);
            if (img_raw) {
                /* Byte-swap sImageTableEntry from N64 big-endian buffer */
                sImageTableEntry *img = (sImageTableEntry *)trackedCalloc(1, sizeof(sImageTableEntry));
                const u8 *ip = (const u8 *)img_raw;
                img->index  = rd32(ip + 0);
                img->width  = ip[4];
                img->height = ip[5];
                img->level  = ip[6];
                img->format = ip[7];
                img->depth  = ip[8];
                img->flagsS = ip[9];
                img->flagsT = ip[10];
                img->pad    = ip[11];
                gf->Image   = img;
            } else {
                gf->Image   = NULL;
            }
        }
        gf->Scale        = rdf32(dp + 28);
        gf->RwDataIndex  = rd16(dp + 32);
        gf->reserved     = rd16(dp + 34);
        gf->BaseAddr     = (void *)filedata_mut;
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_SHADOW:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_ShadowRecord *sh = &rodata->Shadow;
        sh->pos.f[0]  = rdf32(dp + 0);
        sh->pos.f[1]  = rdf32(dp + 4);
        sh->size.f[0] = rdf32(dp + 8);
        sh->size.f[1] = rdf32(dp + 12);
        {
            void *img_raw = vma2ptr(rd32(dp + 16), filedata_mut);
            if (img_raw) {
                sImageTableEntry *img = (sImageTableEntry *)trackedCalloc(1, sizeof(sImageTableEntry));
                const u8 *ip = (const u8 *)img_raw;
                img->index  = rd32(ip + 0);
                img->width  = ip[4];
                img->height = ip[5];
                img->level  = ip[6];
                img->format = ip[7];
                img->depth  = ip[8];
                img->flagsS = ip[9];
                img->flagsT = ip[10];
                img->pad    = ip[11];
                sh->image   = img;
            } else {
                sh->image   = NULL;
            }
        }
        /* sh->Header — deferred to pointer fixup */
        sh->Scale     = rdf32(dp + 24);
        sh->BaseAddr  = (void *)filedata_mut;
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_OP06:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_Op06Record *o = &rodata->Op06;
        o->unk00 = rd32(dp + 0);
        o->unk04 = rd32(dp + 4);
        o->unk08 = rd32(dp + 8);
        o->unk0C = rd32(dp + 12);
        o->unk10 = rd32(dp + 16);
        o->BaseAddr = (void *)filedata_mut;
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_OP11:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_Op11Record *o = &rodata->Op11;
        for (int i = 0; i < 16; i++)
            o->unk0c[i] = rd32(dp + i * 4);
        o->BoundingVolumeRadius = rdf32(dp + 64);
        o->RwDataIndex = rd16(dp + 68);
        o->unk46 = rd16(dp + 70);
        o->BaseAddr = (void *)filedata_mut;
        /* PROMOTE does unk0c[15] which is a pointer — deferred */
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_INTERLINK:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_InterlinkageRecord *il = &rodata->Interlinkage;
        il->pos.f[0]  = rdf32(dp + 0);
        il->pos.f[1]  = rdf32(dp + 4);
        il->pos.f[2]  = rdf32(dp + 8);
        il->pos2.f[0] = rdf32(dp + 12);
        il->pos2.f[1] = rdf32(dp + 16);
        il->pos2.f[2] = rdf32(dp + 20);
        il->Scale     = rdf32(dp + 24);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_OP16:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelNode_Op16Record *o = &rodata->Op16;
        o->pos.f[0]  = rdf32(dp + 0);
        o->pos.f[1]  = rdf32(dp + 4);
        o->pos.f[2]  = rdf32(dp + 8);
        o->nodeindex0c = rds16(dp + 12);
        o->nodeindex0e = rds16(dp + 14);
        o->nodeindex10 = rds16(dp + 16);
        o->unk12 = rds16(dp + 18);
        o->Scale     = rdf32(dp + 20);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_OP20:
    {
        /* Same as HEADER */
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_HeaderRecord *h = &rodata->Header;
        h->AnimPart = rd16(dp);
        h->MatrixIndex = rds16(dp + 2);
        h->Group1 = rd16(dp + 8);
        h->Group2 = rd16(dp + 10);
        h->RwDataIndex = rd16(dp + 12);
        h->reserved = rd16(dp + 14);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_OP05:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_Op05Record *o = &rodata->Op05;
        o->NumChildren = (s32)rd32(dp + 0);
        o->Children = NULL;  /* complex — needs per-child conversion */
        o->Vertices = (Vertex *)vma2ptr(rd32(dp + 8), filedata_mut);
        o->Images   = (struct sImageTableEntry *)vma2ptr(rd32(dp + 12), filedata_mut);
        /* Copy the 400-byte Data block raw */
        memcpy(o->Data, dp + 16, 400);
        o->unk1A0 = rd32(dp + 0x1A0);
        o->BaseAddr = (void *)filedata_mut;
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_OP07:
    {
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_Op07Record *o = &rodata->Op07;
        /* o->unk00, unk04 — ModelNode pointers, deferred */
        o->NumChildren = (s32)rd32(dp + 8);
        o->Children = NULL;  /* complex — needs per-child conversion */
        o->Vertices = (Vertex *)vma2ptr(rd32(dp + 16), filedata_mut);
        o->Images   = (struct sImageTableEntry *)vma2ptr(rd32(dp + 20), filedata_mut);
        /* Copy the 400-byte Data block raw */
        memcpy(o->Data, dp + 24, 400);
        o->unk1A8 = rd16(dp + 0x1A8);
        o->RwDataIndex = rd16(dp + 0x1AA);
        o->BaseAddr = (void *)filedata_mut;
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    case MODELNODE_OPCODE_HEAD:
    {
        /* HeadPlaceholder — only has RwDataIndex */
        rodata = trackedCalloc(1, sizeof(*rodata));
        ModelRoData_HeadPlaceholderRecord *hp = &rodata->HeadPlaceholder;
        hp->RwDataIndex = rd16(dp + 0);
        mapAdd(map, (u32)data_off, rodata);
        break;
    }

    default:
        mapAdd(map, (u32)data_off, NULL);
        break;
    }

    node->Data = rodata;
}

static void convertNodeGraph(
    const u8 *filedata,
    s32 start_off,
    OffsetMap *nodeMap,
    OffsetMap *rodataMap)
{
    OffsetWorkList work;

    if (start_off < 0) {
        return;
    }

    workListInit(&work);
    workListPush(&work, start_off);

    while (work.count > 0) {
        s32 node_off = workListPop(&work);
        const u8 *np;
        u16 opcode;
        u32 data_vma;
        ModelNode *node;
        s32 data_off;

        if (node_off < 0 || mapLookup(nodeMap, (u32)node_off) != NULL) {
            continue;
        }

        np = filedata + node_off;
        opcode = rd16(np + N64_OFF_OPCODE);
        data_vma = rd32(np + N64_OFF_DATA);

        if ((opcode & 0xFF) > MODELNODE_OPCODE_MAX) {
            fprintf(stderr,
                    "[MODEL-CONVERT] ERROR: invalid opcode 0x%04X at node_off=%d\n",
                    opcode, node_off);
            continue;
        }

        node = (ModelNode *)trackedCalloc(1, sizeof(ModelNode));
        node->Opcode = opcode;
        mapAdd(nodeMap, (u32)node_off, node);

        data_off = vma2off(data_vma);
        if (data_off >= 0) {
            const u8 *dp = filedata + data_off;

            convertRoData(filedata, node, data_off, (u8 *)filedata, rodataMap);

            switch (opcode & 0xFF) {
            case MODELNODE_OPCODE_LOD:
                workListPush(&work, vma2off(rd32(dp + 8)));
                break;

            case MODELNODE_OPCODE_SWITCH:
                workListPush(&work, vma2off(rd32(dp + 0)));
                break;

            case MODELNODE_OPCODE_BSP:
                workListPush(&work, vma2off(rd32(dp + 24)));
                workListPush(&work, vma2off(rd32(dp + 28)));
                break;

            case MODELNODE_OPCODE_OP17:
                workListPush(&work, vma2off(rd32(dp + 20)));
                break;

            case MODELNODE_OPCODE_SHADOW:
                workListPush(&work, vma2off(rd32(dp + 20)));
                break;

            case MODELNODE_OPCODE_OP07:
                workListPush(&work, vma2off(rd32(dp + 0)));
                workListPush(&work, vma2off(rd32(dp + 4)));
                break;

            default:
                break;
            }
        }

        workListPush(&work, vma2off(rd32(np + N64_OFF_PARENT)));
        workListPush(&work, vma2off(rd32(np + N64_OFF_NEXT)));
        workListPush(&work, vma2off(rd32(np + N64_OFF_PREV)));
        workListPush(&work, vma2off(rd32(np + N64_OFF_CHILD)));
    }

    workListFree(&work);
}

/* ------------------------------------------------------------------ */
/* Pointer fixup pass                                                  */
/* ------------------------------------------------------------------ */

/* After all nodes and rodata are allocated and in the map, fix up all
 * cross-pointers (node→node, rodata→node, rodata→rodata). */
static void fixupPointers(
    const u8 *filedata,
    OffsetMap *nodeMap,   /* maps file_off → native ModelNode* */
    OffsetMap *rodataMap) /* maps file_off → native rodata */
{
    /* Fix up node pointers (Parent, Next, Prev, Child) */
    for (int i = 0; i < nodeMap->count; i++) {
        u32 noff = nodeMap->entries[i].file_off;
        ModelNode *node = (ModelNode *)nodeMap->entries[i].native;
        const u8 *np = filedata + noff;

        u32 parent_vma = rd32(np + N64_OFF_PARENT);
        u32 next_vma   = rd32(np + N64_OFF_NEXT);
        u32 prev_vma   = rd32(np + N64_OFF_PREV);
        u32 child_vma  = rd32(np + N64_OFF_CHILD);

        node->Parent = (ModelNode *)mapLookupVMA(nodeMap, parent_vma);
        node->Next   = (ModelNode *)mapLookupVMA(nodeMap, next_vma);
        node->Prev   = (ModelNode *)mapLookupVMA(nodeMap, prev_vma);
        node->Child  = (ModelNode *)mapLookupVMA(nodeMap, child_vma);
    }

    /* Fix up rodata cross-pointers */
    for (int i = 0; i < nodeMap->count; i++) {
        ModelNode *node = (ModelNode *)nodeMap->entries[i].native;
        if (!node || !node->Data) continue;

        u32 noff = nodeMap->entries[i].file_off;
        const u8 *np = filedata + noff;
        u32 data_vma = rd32(np + N64_OFF_DATA);
        s32 data_off = vma2off(data_vma);
        if (data_off < 0) continue;
        const u8 *dp = filedata + data_off;

        u16 type = node->Opcode & 0xFF;

        switch (type)
        {
        case MODELNODE_OPCODE_HEADER:
        case MODELNODE_OPCODE_OP20:
        {
            ModelRoData_HeaderRecord *h = &node->Data->Header;
            u32 fg_vma = rd32(dp + 4);
            h->FirstGroup = (ModelRoData_GroupRecord *)mapLookupVMA(rodataMap, fg_vma);
            break;
        }

        case MODELNODE_OPCODE_GROUP:
        case MODELNODE_OPCODE_OP03:
        {
            ModelRoData_GroupRecord *g = &node->Data->Group;
            u32 cg_vma = rd32(dp + 20);
            g->ChildGroup = (ModelRoData_GroupRecord *)mapLookupVMA(rodataMap, cg_vma);
            break;
        }

        case MODELNODE_OPCODE_OP17:
        {
            ModelRoData_Op17Record *o = &node->Data->Op17;
            u32 related_vma = rd32(dp + 20);
            o->othernode = (ModelNode *)mapLookupVMA(nodeMap, related_vma);
            break;
        }

        case MODELNODE_OPCODE_LOD:
        {
            ModelRoData_LODRecord *l = &node->Data->LOD;
            u32 aff_vma = rd32(dp + 8);
            l->Affects = (ModelNode *)mapLookupVMA(nodeMap, aff_vma);
            /* Also set node->Child to Affects, matching PROMOTE behavior */
            node->Child = l->Affects;
            break;
        }

        case MODELNODE_OPCODE_SWITCH:
        {
            ModelRoData_SwitchRecord *s = &node->Data->Switch;
            u32 ctrl_vma = rd32(dp + 0);
            s->Controls = (ModelNode *)mapLookupVMA(nodeMap, ctrl_vma);
            break;
        }

        case MODELNODE_OPCODE_BSP:
        {
            ModelRoData_BSPRecord *b = &node->Data->BSP;
            u32 left_vma  = rd32(dp + 24);
            u32 right_vma = rd32(dp + 28);
            b->leftChild  = (ModelNode *)mapLookupVMA(nodeMap, left_vma);
            b->rightChild = (ModelNode *)mapLookupVMA(nodeMap, right_vma);
            break;
        }

        case MODELNODE_OPCODE_SHADOW:
        {
            ModelRoData_ShadowRecord *sh = &node->Data->Shadow;
            u32 hdr_vma = rd32(dp + 20);
            /* Header is cast to ModelNode* by doshadow for modelGetNodeRwData,
             * so resolve against nodeMap (not rodataMap). */
            sh->Header = (ModelRoData_HeaderRecord *)mapLookupVMA(nodeMap, hdr_vma);
            break;
        }

        case MODELNODE_OPCODE_OP07:
        {
            ModelRoData_Op07Record *o = &node->Data->Op07;
            u32 n0_vma = rd32(dp + 0);
            u32 n4_vma = rd32(dp + 4);
            o->unk00 = (ModelNode *)mapLookupVMA(nodeMap, n0_vma);
            o->unk04 = (ModelNode *)mapLookupVMA(nodeMap, n4_vma);

            /* Children array: each entry is N64_RODATA_CHILD bytes on N64 */
            u32 children_vma = rd32(dp + 12);
            if (children_vma && o->NumChildren > 0) {
                s32 ch_off = vma2off(children_vma);
                if (ch_off >= 0) {
                    ModelRoData_Child *children = (ModelRoData_Child *)trackedCalloc(
                        o->NumChildren, sizeof(ModelRoData_Child));
                    for (int ci = 0; ci < o->NumChildren; ci++) {
                        const u8 *cp = filedata + ch_off + ci * N64_RODATA_CHILD;
                        children[ci].NumEntries = cp[0];
                        children[ci].unk01 = cp[1];
                        children[ci].unk02 = rd16(cp + 2);
                        children[ci].unk04 = (u8 *)vma2ptr(rd32(cp + 4), (u8*)filedata);
                    }
                    o->Children = children;
                }
            }
            break;
        }

        case MODELNODE_OPCODE_OP05:
        {
            ModelRoData_Op05Record *o = &node->Data->Op05;
            u32 children_vma = rd32(dp + 4);
            if (children_vma && o->NumChildren > 0) {
                s32 ch_off = vma2off(children_vma);
                if (ch_off >= 0) {
                    ModelRoData_Child *children = (ModelRoData_Child *)trackedCalloc(
                        o->NumChildren, sizeof(ModelRoData_Child));
                    for (int ci = 0; ci < o->NumChildren; ci++) {
                        const u8 *cp = filedata + ch_off + ci * N64_RODATA_CHILD;
                        children[ci].NumEntries = cp[0];
                        children[ci].unk01 = cp[1];
                        children[ci].unk02 = rd16(cp + 2);
                        children[ci].unk04 = (u8 *)vma2ptr(rd32(cp + 4), (u8*)filedata);
                    }
                    o->Children = children;
                }
            }
            break;
        }

        case MODELNODE_OPCODE_OP11:
        {
            /* FID-0039 (documented, not a live defect): retail promotes
             * unk0c[15] as a pointer (model.c:14040 PROMOTE) and BaseAddr, but
             * OP11 is an "unused but referenced" opcode (bondtypes.h) whose
             * render handler sub_GAME_7F0737FC (model.c:10845) is a no-op `return;`
             * in the SHIPPING game — the same no-op the port reproduces. unk0c[15]
             * is read as a pointer ONLY by that no-op render and by the retail
             * modelPromoteNodeOffsetsToPointers path, which the 64-bit converter
             * REPLACES. The port-used OP11 fields (BoundingVolumeRadius, RwDataIndex)
             * are converted correctly above. So storing NULL here is behaviourally
             * invisible AND the safe representation (a 64-bit pointer cannot fit in
             * u32[16]); a truncated pointer would be strictly worse if OP11 render
             * were ever un-no-op'd. See docs/fidelity/derivations/FID-0037-0039-0107-batch.md. */
            ModelRoData_Op11Record *o = &node->Data->Op11;
            o->unk0c[15] = 0;
            break;
        }

        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main entry: convert an N64 model binary for 64-bit PC               */
/* ------------------------------------------------------------------ */

/**
 * modelConvertN64Binary — Convert N64 model binary to native 64-bit structs.
 *
 * Called from load_object_fill_header() in place of sub_GAME_7F075A90.
 * Walks the N64 binary data, creates native ModelNode and rodata structs,
 * and fixes up all pointers.
 *
 * @param header    The ModelFileHeader (numSwitches/numtextures already set)
 * @param filedata  Pointer to loaded file data (N64 big-endian binary)
 */
void modelConvertN64Binary(ModelFileHeader *header, void *filedata, size_t size)
{
    u8 *fd = (u8 *)filedata;
    sFileData = fd;
    sFileSize = size;
    sVertexDone = calloc(size, 1);
    mapInit(&sDisplayLists);
    s32 numSw = header->numSwitches;
    s32 numTex = header->numtextures;

    /* Sanity check */
    if (numSw < 0 || numSw > 1000 || numTex < 0 || numTex > 1000) {
        fprintf(stderr, "[MODEL-CONVERT] ERROR: bad header values (sw=%d tex=%d)\n", numSw, numTex);
        header->RootNode = NULL;
        header->Switches = NULL;
        header->Textures = NULL;
        return;
    }

    u8 *tex_base = fd + numSw * 4;
    header->Textures = (ModelFileTextures *)trackedCalloc(numTex, sizeof(ModelFileTextures));
    for (int i = 0; i < numTex; i++) {
        u8 *tp = tex_base + i * N64_TEXTURES_SIZE;
        header->Textures[i].TextureID = rd32(tp);
        memcpy(&header->Textures[i].Width, tp + 4, 7);
    }

    /* Inline 16/32-bit texels use the renderer's native integer byte order. */
    for (int i = 0; i < numTex; i++) {
        ModelFileTextures *texture = &header->Textures[i];
        u32 id = texture->TextureID;
        u32 depth = texture->RenderDepth;
        u32 length = ((u32)texture->Width * texture->Height << depth) >> 1;
        u32 offset = id & 0xffffff;
        int duplicate = 0;
        for (int j = 0; j < i; j++)
            if (header->Textures[j].TextureID == id) duplicate = 1;
        if (!duplicate && (id >> 24) == 5 && depth >= 2 &&
            offset <= size && length <= size - offset) {
            u32 unit = depth == 2 ? 2 : 4;
            for (u32 j = 0; j + unit <= length; j += unit) {
                for (u32 k = 0; k < unit / 2; k++) {
                    u8 tmp = fd[offset + j + k];
                    fd[offset + j + k] = fd[offset + j + unit - 1 - k];
                    fd[offset + j + unit - 1 - k] = tmp;
                }
            }
        }
    }

    /* Compute root node offset in file data */
    s32 root_off = numSw * 4 + numTex * N64_TEXTURES_SIZE;

    /* Initialize offset maps */
    OffsetMap nodeMap, rodataMap;
    mapInit(&nodeMap);
    mapInit(&rodataMap);

    /* ---- Phase 1: Convert every node graph needed by root and switch table ---- */
    convertNodeGraph(fd, root_off, &nodeMap, &rodataMap);

    for (int i = 0; i < numSw; i++) {
        convertNodeGraph(fd, vma2off(rd32(fd + i * 4)), &nodeMap, &rodataMap);
    }

    /* ---- Phase 2: Fix up all cross-pointers ---- */
    fixupPointers(fd, &nodeMap, &rodataMap);
    nativeModelGeometry(&nodeMap);
    mapFree(&sDisplayLists);
    free(sVertexDone);

    /* ---- Phase 3: Update ModelFileHeader ---- */

    /* Root node */
    header->RootNode = (ModelNode *)mapLookup(&nodeMap, (u32)root_off);

    /* Switches array: allocate native pointer array */
    if (numSw > 0) {
        ModelNode **new_switches = (ModelNode **)trackedCalloc(numSw, sizeof(ModelNode *));
        for (int i = 0; i < numSw; i++) {
            u32 sw_vma = rd32(fd + i * 4);
            new_switches[i] = (ModelNode *)mapLookupVMA(&nodeMap, sw_vma);
        }
        header->Switches = new_switches;
    }

    mapFree(&nodeMap);
    mapFree(&rodataMap);
}

#endif /* GE_NATIVE_64 */
