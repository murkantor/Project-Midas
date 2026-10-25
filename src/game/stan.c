#include <ultra64.h>
#include "game/dyn.h"
#include "token.h"
//#include <bondtypes.h>
#include <deb.h>
#include "stan.h"
#ifndef TARGET_N64
#include "frametiming.h" /* tickhz=hw stage-load work counters */
#endif
#include "bg.h"
#include "chrai.h"
#include "chr.h"
#include "stanintersection.h"
#include "assert.h"

#ifndef TARGET_N64
#include <stdlib.h> /* calloc - portStanPack rebuilds the packed tile image */
#include <string.h>

#ifndef TARGET_N64
/* Records which blocker a move hit, stamped with the tick and the test that
 * found it, because stanSavedColl_posData can be stale from an earlier call.
 * LOAD-BEARING, not just diagnostics: portUnwedgePlayer() in bondview2.c uses
 * it to tell a static blocker from a chr. */
PropRecord *g_portBlockProp;
s32 g_portBlockSrc;   /* 1 = stanTestLineUnobstructed, 2 = stanTestVolume */
s32 g_portBlockTick;
f32 g_portBlockTop, g_portBlockBot;   /* prop collision block bounds at the hit */
f32 g_portBlockLo,  g_portBlockHi;    /* the two values they were compared against */
s32 g_portBlockGate;                  /* the spCC/sp108 enable flag */
f32 g_portBlockFloor;                 /* floor Y the gate was built from */
void *g_portBlockTileUsed;            /* the tile that floor came from */
f32 g_portBlockTX, g_portBlockTZ;     /* the target point the floor was sampled at */
s32 g_portBlockRooms[24]; s32 g_portBlockRoomCount;  /* the room set the prop scan used */
extern s32 g_GlobalTimer;
#endif
#endif

void getTileMidPoint(StandTile *tile, coord3d *out);

// bss


struct StanPrefixRecord *stan_prefix;
s32 dword_CODE_bss_8007B124;

//CODE.bss:8007B128
StandTile *firststaninroom[139];
//CODE.bss:8007B354
s32 dword_CODE_bss_8007B354;
//CODE.bss:8007B358 //stan list array
StanRoomBounds g_StanRoomBounds[139];
//CODE.bss:8007B9DC
s32 dword_CODE_bss_8007B9DC; //region?
//CODE.bss:8007B9E0
s32 dword_CODE_bss_8007B9E0;

// All relating to a saved collision, but not one struct
//CODE.bss:8007B9E4
StandTile *stanSavedColl_tile;
//CODE.bss:8007B9E8
s32 stanSavedColl_pointI;
//CODE.bss:8007B9EC
s32 stanSavedColl_unknown;
//CODE.bss:8007B9F0
struct coord2d stanSavedColl_pntA;
//CODE.bss:8007B9F8
struct coord2d stanSavedColl_pntB;
//CODE.bss:8007BA00
f32 stanSavedColl_someMin;

//CODE.bss:8007BA04
PropRecord * stanSavedColl_posData;

//CODE.bss:8007BA08
s32 dword_CODE_bss_8007BA08;
//CODE.bss:8007BA0C
StandTile * dword_CODE_bss_8007BA0C;
//CODE.bss:8007BA10
StandTile *bfsTileStack[352];


// data

//D:80040F30
// Indexed by StandTile.mid.headerMid.special.
u8 g_StanTileSpecialFlags[] = {
    0x8D, 0x86, 0x04, 0xC5,
    0x9D, 0xA4, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

s32 stan_c_debug_notice_list_entry = 0;

//D:80040F44
f32 level_scale = 1.0;
//D:80040F48
f32 inv_level_scale = 1.0;
//D:80040F4C
u8 list_of_tilesizes[] = {
    0x20,0x20,0x20,0x20,
    0x28,0x30,0x38,0x40,
    0x48,0x50,0x58,0x00
};
//D:80040F58
struct StandTile * standTileStart = NULL;
//D:80040F5C
intptr_t ptr_firstroom_0 = 0;
//D:80040F60
struct StandTile* stanTileEnd = NULL;
//D:80040F64
s32 D_80040F64[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//D:80040FAC
s32 D_80040FAC = 0;
//D:80040FB0
s32 m_stanRegion = 0;
//D:80040FB4
s32 stanlinelog_flag = 0;

#if defined(LEFTOVERDEBUG)

s32 D_80040FB8[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0
};
#endif
//D:800413BC
s32 D_800413BC =  0;
//D:800413C0
f32 D_800413C0 =  0.0;
//D:800413C4
f32 D_800413C4 =  0.0;
//D:800413C8
s32 D_800413C8 =  1;
//D:800413CC
s32 D_800413CC =  1;
//D:800413D0
s32 D_800413D0[] =  {0, 0, 0, 0, 0, 0, 0, 0};


// rodata
//D:800585A0
const char aCDCC[] = "%c%d%c%c";
//D:800585AC
const char aStan_c_debug[] = "stan_c_debug";
//D:800585BC
const char aStanlinelog[] = "-stanlinelog";

// forward declarations

s32 stanIsSpecialBit1Set(StandTile *arg0, struct StandTileLocusCallbackRecord* arg1);
s32 stanCheckLinkedSpecialTile(StandTile *tile, s32 pointIdx, f32 arg2, f32 arg3, f32 arg4, struct StandTileLocusCallbackRecord *record);
s32 sub_GAME_7F0B21B0(StandTile **tileStack, f32 target_x, f32 target_z, f32 radius, s32 *rooms, s32 *count_rtn, s32 bufMax);
f32 getShortest2dDispToInfTripleEdge(StandTile *tile, s32 start3index, f32 p_x, f32 p_z);
StanCollisionResult sub_GAME_7F0B1DDC(struct StandTile**, f32, f32, f32, standTileLocusCallback_A_t, standTileLocusCallback_B_t, standTileLocusCallback_C_t, struct StandTileLocusCallbackRecord*);
s32 stanLocusAddTileRoomIfNew(StandTile *tile, struct StandTileLocusCallbackRecord *rec);
s32 *stanGetLocusField0(struct StandTileLocusCallbackRecord *arg0);
s32 stanGetLocusCount(struct StandTileLocusCallbackRecord *arg0);
bool stanLocusEdgeIsAboveY(StandTile *tile, s32 edgeIndex, f32 edgeDist, f32 distToPointA, f32 distToPointB, struct StandTileLocusCallbackRecord *record);

// end forward declarations

s32 stanBitwiseCastF32(f32 arg0)
{
    // disgusting
    return *(s32*)&arg0;
}


// maybe getstanroomID and returns a string
char *sub_GAME_7F0AEF3C(StandTile *tile)
{
    char *buffer;
    s32 nextidx;
    s32 type;
    s32 letter;
    s32 digit_raw;
    s32 masked_number;
    s32 idpart1;
    u8 idpart2;
    s32 idx;
    
    idx = D_80040FAC;
    buffer = (char *)D_80040F64 + (idx * 9);
    
    idpart1 = *((u16 *) tile);
    digit_raw = ((u8 *) tile)[2];
    idpart2 = digit_raw;
    
    letter = idpart2 >> 3;
    idx = (idx + 1) & 7;
    masked_number = idpart1 & 0x7fff;
    type = (idpart1 >> 15) & 1;
    letter &= 0x1f;
    nextidx = idx;
    digit_raw = idpart2 & 7;
    
    if (digit_raw)
    {
        idpart1 = *((u16 *) tile);
    }
    
    D_80040FAC = nextidx;
    
    if (!digit_raw)
    {
        if (digit_raw && digit_raw);
        
        idpart1 = 0;
    }
    else 
    {
        idpart1 = digit_raw + '0';
    }
    
    sprintf(buffer, aCDCC, type + 'p', masked_number, letter + 'a', idpart1);
    
    return buffer;
}


/**
 * Unreferenced.
 */
void sub_GAME_7F0AEFE0(StandTile *tile)
{
    sub_GAME_7F0AEF3C(tile); //maybe getstanroomID
}


//stanChecksf
u32 stanRemovedAnimationRoutine(s32 arg0) 
{
#ifdef DEBUG
    if (arg0 < ptr_firstroom_0)
    {
        osSyncPrintf("checksf: ERROR line %d %08x<%08x", __LINE__, arg0, ptr_firstroom_0);
    }
    if (stanTileEnd < arg0)
    {
        osSyncPrintf("checksf: ERROR line %d %08x>%08x", __LINE__, arg0, stanTileEnd);
    }
#endif
    return 0;
}


void stanInit(void) 
{
    debTryAdd(&stan_c_debug_notice_list_entry, &aStan_c_debug); //"stan_c_debug");
}


/**
 * Address: 7F0AF038
 */
void stanBuildRoomData(void)
{
    StandTile *tile;
    u8 lastRoom;
    s32 i;
    s32 j;
    s32 k;

    lastRoom = 0xff;
    dword_CODE_bss_8007B9DC = 0;

    // Must remain on one line for matching.
    for (k = 0; k < 139; k++) firststaninroom[k] = NULL;

    tile = stan_prefix->ptr_firstroom;

    while (*(u32 *)tile)
    {
        if (tile->room != lastRoom)
        {
            lastRoom = tile->room;

            if (dword_CODE_bss_8007B9DC <= lastRoom)
            {
                dword_CODE_bss_8007B9DC = lastRoom + 1;
            }

            firststaninroom[lastRoom] = tile;

            g_StanRoomBounds[lastRoom].min[0] = g_StanRoomBounds[lastRoom].min[1] = g_StanRoomBounds[lastRoom].min[2] = 0x7fff;
            g_StanRoomBounds[lastRoom].max[0] = g_StanRoomBounds[lastRoom].max[1] = g_StanRoomBounds[lastRoom].max[2] = -0x8000;
        }

        for (i = 0; i < (tile->tail.hdrTail.pointCount & 0xf); i++)
        {
            for (j = 0; j < 3; j++)
            {
                if (tile->points[i].AsArray[j] < g_StanRoomBounds[lastRoom].min[j])
                {
                    g_StanRoomBounds[lastRoom].min[j] = tile->points[i].AsArray[j];
                }

                if (tile->points[i].AsArray[j] > g_StanRoomBounds[lastRoom].max[j])
                {
                    g_StanRoomBounds[lastRoom].max[j] = tile->points[i].AsArray[j];
                }
            }
        }

        tile = (StandTile *)(((u8 *)tile) + list_of_tilesizes[tile->tail.hdrTail.pointCount & 0xf]);
    }
}


/**
 * Address: 7F0AF20C
 * 
 * Finds the highest stan file beneath pos. If a tile is found, yRtn is set to the tile
 * height beneath pos.
 * @returns NULL if no suitable tile is found.
 */
StandTile *stanFindTileBelowPos(coord3d *pos, u8 *rooms, f32 *yRtn)
{
    StandTile *firstTile;
    f32 scaled[3];
    StandTile *tile;
    s16 scaledShort[3];
    coord3d *midPointPtr;
    u8 *tileSizes;
    StandTile *tileStack;
    StandTile *bestTile;
    f32 maxY;
    f32 bestY;
    f32 edgeDist;
    f32 tileY;
    s32 nearEdge;
    coord3d midPoint;
    s32 tailhalf;
    s32 room;
    StandTile **roomFirstTiles;
    s32 i;

    maxY = 32767.0f;
    bestY = -3.4028235e38f;
    bestTile = NULL;

    scaled[0] = pos->x * level_scale;
    scaled[1] = pos->y * level_scale;
    scaled[2] = pos->z * level_scale;

    if (maxY < scaled[1]) 
    {
        scaled[1] = maxY;
    }

    if (scaled[1] < -32767.0f) 
    {
        scaled[1] = -32767.0f;
    }

    scaledShort[0] = scaled[0];
    scaledShort[1] = scaled[1];
    scaledShort[2] = scaled[2];

    tileSizes = list_of_tilesizes;
    midPointPtr = &midPoint;
    room = 0; \
    if (dword_CODE_bss_8007B9DC > 0) \
    { \
        roomFirstTiles = (StandTile **) &firststaninroom; \
        do 
        {
            firstTile = *roomFirstTiles;
            
            if (firstTile != NULL)
            {
                if (scaledShort[0] < (&((StanRoomBounds *) g_StanRoomBounds)[room])->minX)
                {
                    goto next_room;
                }

                if ((&((StanRoomBounds *) g_StanRoomBounds)[room])->maxX < scaledShort[0])
                {
                    goto next_room;
                }

                if (scaledShort[2] < (&((StanRoomBounds *) g_StanRoomBounds)[room])->minZ)
                {
                    goto next_room;
                }

                if ((&((StanRoomBounds *) g_StanRoomBounds)[room])->maxZ < scaledShort[2])
                {
                    goto next_room;
                }

                if (scaledShort[1] < (&((StanRoomBounds *) g_StanRoomBounds)[room])->minY)
                {
                    goto next_room;
                }

                if (rooms != NULL)
                {
                    for (i = 0; (rooms[i] != 0xff) && (i != 4); i++)
                    {
                        if (room == rooms[i])
                        {
                            goto found_room;
                        }
                    }
                    goto next_room;
found_room:
                    ;
                }

                tile = firstTile;
                firstTile = *roomFirstTiles;

                while (((*((u32 *) tile)) != 0) && (tile->room == room))
                {
                    for (i = 0; i < 3; i++)
                    {
                        edgeDist = getShortest2dDispToInfTripleEdge(tile, i, scaled[0], scaled[2]);

                        if (edgeDist < -2.0f)
                        {
                            goto nexttile;
                        }
                        
                        if (edgeDist < 2.0f)
                        {
                            nearEdge = 1;
                        }
                    }
                    
                    if (stanTileHasZeroArea(tile))
                    {
                        goto nexttile;
                    }
                    
                    if (nearEdge)
                    {
                        getTileMidPoint(tile, midPointPtr);
                        tileStack = tile;
                        
                        if (!walkTilesBetweenPoints_NoCallback(&tileStack, midPointPtr->x, midPointPtr->z, pos->x, pos->z)) 
                        {
                            goto nexttile;
                        }
                        
                        if (tileStack != tile) 
                        {
                            goto nexttile;
                        }
                    }
                    
                    tileY = stanGetPositionYValue(tile, pos->x, pos->z);
                    
                    if (pos->y < tileY)
                    {
                        goto nexttile;
                    }
                    
                    if (bestY < tileY)
                    {
                        bestTile = tile;
                        bestY = tileY;
                    }
nexttile:
                    tailhalf = tile->tail.half;
                    tile = (StandTile *) (((u8 *) tile) + list_of_tilesizes[STAN_PTCOUNT(tailhalf)]); \
                } \
next_room:
                ;
            }
            
            room++;
            roomFirstTiles++;
        } while (room < dword_CODE_bss_8007B9DC);
    }
    
    if ((bestTile != NULL) && (yRtn != NULL))
    {
        *yRtn = bestY;
    }
        
    return bestTile;
}


#ifndef TARGET_N64
/* ---- packed stan image (PC) --------------------------------------------
 * The generated stan files emit each tile as its own C global, so GCC can leave
 * small zero gaps between tiles and can even place the header global in the
 * middle of the tile stream. Every tile walker expects the N64's back-to-back
 * stream (stride = list_of_tilesizes[pointCount]), so walking the globals in
 * place stops early or reads garbage.
 *
 * Instead, rebuild a packed byte-image once per stan file at load:
 *  - walk the generated globals, skipping zero padding words; the
 *    StandFileFooter "unstric" magic marks the end of the stream;
 *  - copy each tile's list_of_tilesizes[n] bytes back to back after a
 *    rebuilt header whose room slots hold real pointers into the image
 *    (the N64 post-decode state, so no offset promotion is done later).
 *
 * The image is allocated with calloc (NEVER memp: its arenas reset per stage)
 * and cached by first-tile address so stage reloads reuse it. */

#define STAN_PACK_CACHE_MAX 40

static struct
{
    StandTile *firsttile;
    struct StanPrefixRecord *packed;
} s_PackedStanCache[STAN_PACK_CACHE_MAX];

static u8 *portStanNextTileGlobal(u8 *p, u8 *hdr, u32 hdrsize)
{
    s32 i;

    /* generated tile globals are >=4-aligned; inter-global padding is zero
     * fill. 128 bytes covers the small alignment gaps seen between tile
     * globals plus the all-zero terminator tile. */
    for (i = 0; i < 128; i += 4)
    {
        u8 *q = p + i;
        u32 w;

        if (q >= hdr && q < hdr + hdrsize)
        {
            /* GCC can place the header global inside the tile stream (e.g. tile_0, header, tile_1).
             * Skip over the whole header span so its zero word isn't mistaken for the end. */
            return portStanNextTileGlobal(hdr + hdrsize, hdr, hdrsize);
        }

        w = *(u32 *) q;

        if (w == 0)
        {
            continue;
        }
        if (w == 0x74736E75u) /* "unst" - the generated footer's magic */
        {
            return NULL;
        }
        if ((w & 0x00FFFFFFu) == 0)
        {
            /* A real tile can have name id 0 (Complex has one), so accept it
             * when its header looks sane (plausible room, 1..10 points);
             * stopping here truncated the stan and let players fall out of the
             * map. The real end of the stream is the footer magic checked
             * above. */
            u32 room = w >> 24;
            s32 n = STAN_PTCOUNT(((StandTile *) q)->tail.half);

            if (room == 0 || room >= 128 || n < 1 || n > 10)
            {
                return NULL;
            }
        }
        return q;
    }

    return NULL;
}

static u32 portStanTileSize(u8 *p)
{
    s32 n = STAN_PTCOUNT(((StandTile *) p)->tail.half);

    /* real files top out well under 11 points (Dam max is 7);
     * list_of_tilesizes[11] is 0 and 12..15 are out of bounds - a nibble
     * that big means we misparsed a foreign global, so stop the walk */
    return (n <= 10) ? list_of_tilesizes[n] : 0;
}

static struct StanPrefixRecord *portStanPack(struct StanPrefixRecord *file)
{
    u32 *srcw = (u32 *) file;
    u32 nrooms;   /* entries after ptr_firstroom: BE OFFSET words */
    u32 o_first;  /* original-file offset of the first tile */
    u32 total;
    u32 ntiles;
    u32 badrooms;
    u32 i;
    u8 *p;
    u8 *dst;
    u32 *dstw;
    struct StanPrefixRecord *packed;

    for (i = 0; i < STAN_PACK_CACHE_MAX; i++)
    {
        if (s_PackedStanCache[i].firsttile == file->ptr_firstroom)
        {
            return s_PackedStanCache[i].packed;
        }
    }

    for (nrooms = 0; srcw[2 + nrooms] != 0; nrooms++);
    o_first = 8 + (nrooms + 1) * 4;

    total = 0;
    ntiles = 0;
    p = (u8 *) file->ptr_firstroom;
    while (p != NULL)
    {
        u32 size = portStanTileSize(p);

        if (size == 0)
        {
            break;
        }
        total += size;
        ntiles++;
        p = portStanNextTileGlobal(p + size, (u8 *) srcw, o_first);
    }

    /* +8: the zero word terminating the tile walk (portAllocHigh memory
     * arrives zeroed). High allocation: stan tile pointers are classified
     * by address range (>= 0x10000000 = host) - the Linux brk heap sits
     * below that. */
    {
        extern void *portAllocHigh(u32 size);
        u32 need = o_first + total + 8;
        /* The high arena is bump-only and never freed, so allocating a new
         * packed stan per stage load eventually ran out of memory and crashed.
         * Reuse the block whenever the next stage fits (the old stage's data is
         * dead by then) and only grow when it doesn't. */
        static u8 *s_Packed;
        static u32 s_PackedCap;

        if (s_Packed != NULL && need <= s_PackedCap)
        {
            u32 z;
            packed = s_Packed;

            /* Reusing the block would leave s_PackedStanCache entries for
             * earlier levels pointing at another level's tiles, which broke pad
             * resolution and crashed stage loads (e.g. Bunker 1). Drop any
             * stale cache entries for this block so a cache entry exists only
             * while the block still holds that stan. */
            for (i = 0; i < STAN_PACK_CACHE_MAX; i++)
            {
                if (s_PackedStanCache[i].packed == (struct StanPrefixRecord *) s_Packed)
                {
                    s_PackedStanCache[i].firsttile = NULL;
                    s_PackedStanCache[i].packed = NULL;
                }
            }

            for (z = 0; z < need; z++)   /* callers rely on zeroed memory */
            {
                s_Packed[z] = 0;
            }
        }
        else
        {
            packed = portAllocHigh(need);
            if (packed != NULL)
            {
                s_Packed = (u8 *) packed;
                s_PackedCap = need;
            }
        }
    }
    if (packed == NULL)
    {
        /* Out of high memory: bail instead of faulting on a NULL store.
         * The caller sees an unpacked stan and the stage load fails
         * loudly rather than crashing at address 4. */
        printf("ge: FATAL: portStanPack: no high memory for %u bytes "
               "(stan pack); stage cannot load\n", o_first + total + 8);
        return file;   /* unpacked - caller keeps the source stan */
    }
    dstw = (u32 *) packed;
    dstw[1] = (u32) ((u8 *) packed + o_first);

    badrooms = 0;
    for (i = 0; i < nrooms; i++)
    {
        u32 w = srcw[2 + i];
        u32 be = ((w & 0xFFu) << 24) | ((w & 0xFF00u) << 8) |
                 ((w >> 8) & 0xFF00u) | (w >> 24);

        if (be < o_first || be >= o_first + total)
        {
            badrooms++;
        }
        dstw[2 + i] = (u32) ((u8 *) packed + be);
    }

    dst = (u8 *) packed + o_first;
    p = (u8 *) file->ptr_firstroom;
    while (p != NULL)
    {
        u32 size = portStanTileSize(p);

        if (size == 0)
        {
            break;
        }
        memcpy(dst, p, size);
        dst += size;
        p = portStanNextTileGlobal(p + size, (u8 *) srcw, o_first);
    }

    for (i = 0; i < STAN_PACK_CACHE_MAX; i++)
    {
        if (s_PackedStanCache[i].firsttile == NULL)
        {
            s_PackedStanCache[i].firsttile = file->ptr_firstroom;
            s_PackedStanCache[i].packed = packed;
            break;
        }
    }

    { /* bring-up (GE_DUMP_BOND): pack summary; badrooms MUST be 0 - a
       * nonzero value means the stream walk under/overran the real file
       * (bad stop sentinel) and room pointers landed outside the tiles. */
        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
        f32 v[7];
        v[0] = (f32) ntiles;
        v[1] = (f32) total;
        v[2] = (f32) o_first;
        v[3] = (f32) nrooms;
        v[4] = (f32) badrooms;
        v[5] = (f32) (u32) packed;
        v[6] = (f32) (u32) file->ptr_firstroom;
        ge_dbg_vec("stanpack ntiles/bytes/ofirst/rooms/bad/dst/src", v, 7);
    }

    return packed;
}

/* romfile_stan.c's probe compares its ROM-converted image against this, the
 * packed form compiled-in stans take at load */
struct StanPrefixRecord *portStanPackForProbe(struct StanPrefixRecord *file)
{
    return portStanPack(file);
}

/* set by load_bg_file (bg.c) right after the stan loads: a ROM-served
 * stan (port/src/romfile_stan.c) arrives ALREADY in the packed shape, with
 * real pointers in its header - portStanPack must not run on it */
s32 g_portStanFromRom;
#endif


void stanLoadFile(struct StanPrefixRecord *file)
{
    struct StanPrefixRecord *prefix = &stan_prefix;
    s32 tokenIndexMask;

#ifndef TARGET_N64
    /* every walker downstream (stanBuildRoomData and all) needs the PACKED
     * image; stanDetermineEOF already built it, this is a cache lookup */
    /* same predicate change as stanDetermineEOF: NULL check, not an
     * address-range sniff (which broke on the Windows image base) */
    if (file->ptr_firstroom != NULL && !g_portStanFromRom)
    {
        file = portStanPack(file);
    }
#endif

    m_stanRegion = 1;
    tokenIndexMask = !file->ptr_firstroom;
#ifdef GE_NATIVE_64
    stan_prefix = file;
#else
    prefix->stanfile = file;
#endif
    tokenIndexMask = 1;

    /*
     * Matching artifacts.
     */
    if (prefix);
    if (prefix);
    if (prefix);

    standTileStart = (StandTile *)(((u8 *)file->ptr_firstroom) - 0x80);

    if (tokenFind(tokenIndexMask, aStanlinelog))
    {
        stanlinelog_flag = 1;
    }

    stanBuildRoomData();
    setLevelScale(1.0f);
}


//stanRegion()
void sub_GAME_7F0AF630(s32 arg0)
{
#ifdef DEBUG
    StandTile **rooms;

    rooms = &stan_prefix->ptr_firstroom;

    if (arg0 < 0)
    {
        if (rooms[m_stanRegion - 1] != NULL)
        {
            m_stanRegion--;
        }
    }
    else if (arg0 == 0)
    {
        m_stanRegion = 1;
    }
    else if (rooms[m_stanRegion] != NULL)
    {
        m_stanRegion++;
    }

    osSyncPrintf("stanRegion():  region=%d", m_stanRegion);
#endif
    return;
}


/**
 * Address: 7F0AF638
 * 
 * Unreferenced
 * 
 * Somewhere in this function a loop is checked for overflow
 * if (i < param4)
 * {
 *     printf("stanFillin: Stack overflow %d>%d",local_20,uStack);
 * }
 */
s32 stanFillin(StandTile *starttile, u8 targetbit, StandTile **stack) // Canonical function name
{
    StandTile *tile;
    StandTile *linkedtile;
    StandTilePoint *point;
    u16 *tmp;
    s32 pointcount;
    s32 link;
    s32 result;
    s32 count;
    s32 i;
    s32 stackcount;

    count = 0;
    stack[0] = starttile;
    stackcount = 1;

    for (stack += stackcount; stackcount != 0;)
    {
        tile = stack[-1];
        stackcount--;
        stack--;
        i = 0;

        if (targetbit != (((*((u16 *) tile)) >> 15) & 1))
        {
            tmp = (u16 *) tile;
            *tmp ^= 0x8000;
            result = stanTileHasZeroArea(tile);
            point = (StandTilePoint *) tile;

            if (stackcount);
            
            if (result == 0)
            {
                count++;
            }

            pointcount = STAN_PTCOUNT((&tile->tail)->half);

            if (pointcount > 0)
            {
                do
                {
                    link = point[1].link;
                    i++;

                    if (stackcount);
                    
                    if ((link >> 4) != 0)
                    {
                        linkedtile = (StandTile *) (((u8 *) standTileStart) + (((0, link)) << 3));

                        if (targetbit != (((*((u16 *) linkedtile)) >> 15) & 1))
                        {
                            *stack = linkedtile;
                            pointcount = STAN_PTCOUNT(tile->tail.half);
                            stackcount++;
                            stack++;
                        }
                    }

                    point++;

                    if (starttile);
                }
                while (i < pointcount);
            }
        }
    }

    return count;
}


/**
 * Address: 7F0AF760
 * 
 * Returns true if x/z coords from the three point indices out of tile->tail.half are colinear i.e. the triangle has zero horizontal area.
 */
bool stanTileHasZeroArea(StandTile *tile)
{
    s32 AB[3];
    s32 AC[3];
    u32 crossStore[2];
    s32 temp1, temp2, temp3;
    

    temp1 = STAN_MID_NIB(tile->tail.half, 8);
    temp2 = STAN_MID_NIB(tile->tail.half, 4);
    temp3 = STAN_MID_NIB(tile->tail.half, 0);

    AB[0] = tile->points[temp2].x - tile->points[temp1].x;
    AB[2] = tile->points[temp2].z - tile->points[temp1].z;
    
    AC[0] = tile->points[temp3].x - tile->points[temp1].x;
    AC[2] = tile->points[temp3].z - tile->points[temp1].z;

    crossStore[0] = (AB[2] * AC[0]) - (AB[0] * AC[2]);

    return crossStore[0] == 0;
}


/**
 * Address: 7F0AF808
 * 
 * Unreferenced.
 */
StandTile *stanFindFloorTileBelowY(f32 x, f32 maxY, f32 z, f32 radius)
{
    StandTile *tileStack[2];
    StandTile *tile;
    s32 temp;

    tile = stan_prefix->ptr_firstroom;

    while (*(u32 *)tile != 0)
    {
        tileStack[0] = tile;

        if (stanTileHasZeroArea(tile) == 0)
        {
            if (isPointInsideTriStandTileUnscaled_Maybe(tile, x, z))
            {
                if (sub_GAME_7F0B20D0(tileStack, x, z, radius))
                {
                    if (tileStack[0] == tile)
                    {
                        if (stanGetPositionYValue(tile, x, z) < maxY)
                        {
                            return tile;
                        }
                    }
                }
            }
        }

        temp = tile->tail.hdrTail.pointCount;
        tile = (StandTile *)((u8 *)tile + list_of_tilesizes[temp & 0xf]);
    }

    return NULL;
}
void getTileMidPoint(StandTile *tile, coord3d *out)
{
    u16 tail;
    u8 indexA;
    u32 indexB;
    u32 indexC;
    StandTilePoint *pointA;
    StandTilePoint *pointB;
    unsigned int new_var2;
    StandTilePoint *pointC;
    s16 *new_var3;

#ifdef TARGET_N64
    tail = (indexC = tile->tail.half);
    new_var2 = (tail & 0xFFFF) >> 4;
    indexA = (tail >> 8) & 0xf;
    indexB = new_var2 & 0xf;
    indexC = indexC & 0xf;
#else
    /* headerC/headerD/headerE by IDO shift (see STAN_MID_NIB) */
    tail = tile->tail.half;
    new_var2 = tail;
    indexA = STAN_MID_NIB(tail, 8);
    indexB = STAN_MID_NIB(tail, 4);
    indexC = STAN_MID_NIB(tail, 0);
#endif
    new_var3 = &(&tile->points[indexC])->x;
    pointA = &tile->points[indexA];
    pointB = &tile->points[indexB];
    out->x = (((((f32) pointA->x) + ((f32) pointB->x)) + ((f32) (*new_var3))) / 3.0f) * inv_level_scale;
    out->y = (((((f32) (&tile->points[indexA])->y) + ((f32) pointB->y)) + ((f32) (&tile->points[indexC])->y)) / 3.0f) * inv_level_scale;
    out->z = (((((f32) (&tile->points[indexA])->z) + ((f32) pointB->z)) + ((f32) ((float) (&tile->points[indexC])->z))) / 3.0f) * inv_level_scale;
}


void getPointJustInsideOfTileTriple(StandTile *tile, s32 tripleIndex /*canonically c */, coord3d *out)
{
    coord3d midPoint;
    s32 pntIndex;

    #ifdef DEBUG
    assert(c<3);
    #endif

    pntIndex = STAN_MID_NIB(tile->tail.half, 8 - (tripleIndex * 4));
    
    if (1);
    if (&midPoint);
    
    out->x = ((f32) tile->points[pntIndex].x) * inv_level_scale;
    out->y = ((f32) tile->points[pntIndex].y) * inv_level_scale;
    out->z = ((f32) tile->points[pntIndex].z) * inv_level_scale;
    
    getTileMidPoint(tile, &midPoint);
    
    // 10% of the way from the actual tile point towards the tile's centre.
    out->x = (midPoint.x * 0.1f) + (0.9f * out->x);
    out->y = (midPoint.y * 0.1f) + (0.9f * out->y);
    out->z = (midPoint.z * 0.1f) + (0.9f * out->z);
}


/*
* Address: 0x7F0AFB1C
 */
f32 sub_GAME_7F0AFB1C(coord3d *p,coord3d *q)
{
    // Should be a coord3d or vec3d, but they used an array which
    // causes lots of data reads and writes to the stack.
    f32 components[3];

    components[0] = q->x - p->x;
    components[1] = q->y - p->y;
    components[2] = q->z - p->z;

    return components[0]*components[0] + components[1]*components[1] + components[2]*components[2];
}


StandTile *sub_GAME_7F0AFB78(f32 *x, f32 *y, f32 *z, f32 arg3)
{
    StandTile *tile;
    s32 tileTail;
    StandTile *stack[1];
    StandTile *bestTile;
    s32 i;
    s32 midpointIndex;
    coord3d original;
    coord3d candidate;
    f32 bestDist;
    f32 dist;

    bestTile = NULL;
    original.x = *x;
    original.y = *y;
    original.z = *z;
    midpointIndex = 3;
    bestDist = M_U32_MAX_VALUE_F;

    if (&original);
#ifndef TARGET_N64
    g_PortHwWork[PORTHW_NEAR_CALLS]++;
#endif

    tile = stan_prefix->ptr_firstroom;

    if (*((u32 *) tile))
    {
        do
        {
#ifndef TARGET_N64
            g_PortHwWork[PORTHW_NEAR_TILES]++;
#endif
#ifdef TARGET_N64
            if (((((u16 *) tile)[0] >> 15) & 1) != 1)
#else
            /* Read the id bitfield instead of the raw big-endian u16 the N64
             * tested: on little-endian the raw bit lands in the tile number and
             * skipped many tiles. That made the nearest-tile finder return NULL
             * and dropped doors such as one on Frigate. */
            if (((tile->id >> 23) & 1) != 1)
#endif
            {
                if (stanTileHasZeroArea(tile) == FALSE)
                {
                    for (i = 0; i != 4; i++)
                    {
#ifndef TARGET_N64
                        g_PortHwWork[PORTHW_NEAR_POINTS]++;
#endif
                        if (i == midpointIndex)
                        {
                            getTileMidPoint(tile, &candidate);
                        }
                        else
                        {
                            getPointJustInsideOfTileTriple(tile, i, &candidate);
                        }

                        stack[0] = tile;

                        if (sub_GAME_7F0B20D0(stack, candidate.x, candidate.z, arg3) < 0)
                        {
                            if (x);
                            if (y);
                            if (z);

                            dist = sub_GAME_7F0AFB1C(&candidate, &original);

                            if (dist < bestDist)
                            {
                                bestTile = tile;
                                bestDist = dist;
                                *x = candidate.x;
                                *y = candidate.y;
                                *z = candidate.z;
                            }
                        }
                    }
                }
            }

            tileTail = tile->tail.half;
            tile = (StandTile *) (((u8 *) tile) + list_of_tilesizes[STAN_PTCOUNT(tileTail)]);
            
        } while (*((u32 *) tile));
    }

    return bestTile;
}


// Returns the shortest distance from (p_x,p_z) to the infinite extention of tile's index-th edge, projected into XZ.
// Where the edge is vertical (or degenerate) they just return the distance between the points.
// cannonically tile is sf and index is ei
f32 getShortest2dDispToInfTileEdge(StandTile *tile,s32 index,f32 p_x,f32 p_z)
{
    s32 nextIndex;
    f32 edge_x;
    f32 edge_z;
    f32 edge_len; //canonically d

    f32 v_x;
    f32 v_z;
    f32 crossProduct;

    // 3 unused. We use 2 for the points to make our code cleaner,
    //   though it seems much more likely that the variables were used in the else clause.
    struct StandTilePoint* currPnt;
    struct StandTilePoint* nextPnt;
    f32 UNUSED;

    #ifdef DEBUG
    assert(ei<getsides(sf));
    #endif

    // Omiting the '& 0xF' is equivalent, but keeping it is necessary to match.
    // Perhaps the structure isn't correct but this seems much cleaner than doing an explicit >> 0xC.
    nextIndex = (index + 1) % STAN_PTCOUNT(tile->tail.half);

    nextPnt = &tile->points[nextIndex];
    currPnt = &tile->points[index];
    edge_x = (f32)(nextPnt->x - currPnt->x);
    edge_z = (f32)(nextPnt->z - currPnt->z);

    edge_len = sqrtf(edge_x * edge_x + edge_z * edge_z);

    if (edge_len == 0) {
        // Degenerate case, edge is vertical
        // They just return the distance between the points, which is sensible and the correct value in 3 dimensions.
        v_x = p_x - (f32)tile->points[nextIndex].x;
        v_z = p_z - (f32)tile->points[nextIndex].z;
        return sqrtf(v_x * v_x + v_z * v_z);
    }
    else
    {
        #ifdef DEBUG
        assert(d>0.0f);
        #endif

        // | (AP x AB) / ||AB|| | = ||PA|| sin(a),
        // so we're returning the SIGNED displacement
        crossProduct = (
            edge_z * (p_x - (f32)tile->points[index].x)
            +
            -edge_x * (p_z - (f32)tile->points[index].z)
        );
        return crossProduct / edge_len;
    }

}


f32 getShortest2dDispToInfTripleEdge(StandTile *tile, s32 start3index, f32 p_x, f32 p_z)
{
    f32 dx;
    f32 edgeX;
    f32 edgeZ;
    f32 edgeLen;
    f32 dz;
    f32 crossProduct;
    s32 end3index;
    s32 currPntI;
    s32 nextPntI;
    s32 tail;

    #ifdef DEBUG
    assert(ei<getsides(sf));
    #endif

    nextPntI = 2;

    if (start3index != nextPntI) {
        end3index = start3index + 1;
    } else {
        end3index = 0;
    }

    start3index = STAN_MID_NIB(tile->tail.half, 8 - (start3index << nextPntI));
    end3index = STAN_MID_NIB(tile->tail.half, 8 - (end3index << nextPntI));

    edgeX = tile->points[end3index].x - tile->points[start3index].x;
    edgeZ = tile->points[end3index].z - tile->points[start3index].z;
    edgeLen = sqrtf((edgeX * edgeX) + (edgeZ * edgeZ));

    if (edgeLen == 0.0f) {
        dx = p_x - tile->points[end3index].x;
        dz = p_z - tile->points[end3index].z;
        return sqrtf((dx * dx) + (dz * dz));
    }

    #ifdef DEBUG
    assert(d>0.0f);
    #endif

    crossProduct = (edgeZ * (p_x - tile->points[start3index].x)) + (-edgeX * (p_z - tile->points[start3index].z));
    return crossProduct / edgeLen;
}


f32 getShortest2dDispToInfTileEdgeUnscaled(StandTile *tile, int index,f32 x,f32 z)
{
  f32 disp;

  disp = getShortest2dDispToInfTileEdge(tile, index, x * level_scale, z * level_scale);
  return disp * inv_level_scale;
}


f32 getShortest2dDispToInfTripleEdgeUnscaled(StandTile *tile,s32 start3index,f32 p_x,f32 p_z)
{
  f32 disp;

  disp = getShortest2dDispToInfTripleEdge(tile, start3index, p_x * level_scale, p_z * level_scale);
  return disp * inv_level_scale;
}


f32 distToTilePnt2D(StandTile *tile,int pntI,f32 p_x,f32 p_z)
{
  f32 len;

  p_x -= (f32)tile->points[pntI].x;
  p_z -= (f32)tile->points[pntI].z;
  return sqrtf(p_x * p_x + p_z * p_z);
}


/**
 * Unreferenced.
 */
f32 sub_GAME_7F0B00C4(StandTile *tile, s32 pntI, f32 p_x, f32 p_z)
{
    p_x *= level_scale;
    p_z *= level_scale;

    p_x -= tile->points[pntI].x;
    p_z -= tile->points[pntI].z;

    return sqrtf((p_x * p_x) + (p_z * p_z)) * inv_level_scale;
}


/**
 * Address: 7F0B0140
 * 
 * Unreferenced.
 */
f32 stanPointDot2D(StandTile *tile, s32 index, f32 x, f32 z)
{
    StandTilePoint *point;

    point = &tile->points[index];

    x *= level_scale;
    z *= level_scale;

    return (((f32)point->z * z) + (x * (f32)point->x)) * inv_level_scale;
}


/**
 * Address: 7F0B0198
 * Returns true if the X/Z point's perpendicular projection onto the edge's line
 * lands between the edge endpoints A and B.
 */
bool stanPointProjectsOntoTileEdge(StandTile *tile, s32 edgeIndex, f32 p_x, f32 p_z)
{
    StandTilePoint *point;
    f32 edgeXCopy;
    f32 startX;
    f32 startZ;
    f32 edgeX;
    f32 edgeZ;
    StandTilePoint *nextPoint;

    point = &tile->points[edgeIndex];

    startX = point->x;
    startZ = point->z;

    edgeIndex = (edgeIndex + 1) % (STAN_PTCOUNT(tile->tail.half));

    point = (nextPoint = &tile->points[edgeIndex]);

    edgeX = point->x;
    edgeX = edgeX - startX;

    edgeZ = point->z;
    edgeZ = edgeZ - startZ;

    p_x -= startX;
    p_z -= startZ;

    edgeXCopy = edgeX;

    startZ = (edgeXCopy * edgeXCopy) + (edgeZ * edgeZ);
    startX = (p_x * edgeXCopy) + (p_z * edgeZ);

    edgeZ = startX;

    return ((startZ < edgeZ) && (edgeZ < 0.0f))
        || ((0.0f < edgeZ) && (edgeZ < startZ));
}


// Determines if inside (presumably - it effectively does an && of the checks on signs of cross products)
//   based on the 3 edges. So probably only for triangular tiles.
s32 isPointInsideTriStandTile_Maybe(StandTile *tile, f32 p_x, f32 p_z)
{
    f32 disp;
    s32 i;

    for (i = 0; i != 3; i++)
    {
        disp = getShortest2dDispToInfTripleEdge(tile,i,p_x,p_z);
        if (disp < 0) {
            return 0;
        }
    }

    return 1;
}



s32 isPointInsideTriStandTileUnscaled_Maybe(StandTile *tile, f32 p_x, f32 p_z)
{
    f32 disp;
    s32 i;

    for (i = 0; i != 3; i++)
    {
        disp = getShortest2dDispToInfTripleEdgeUnscaled(tile,i,p_x,p_z);
        if (disp < 0) {
            return 0;
        }
    }

    return 1;
}


/*
* Address: 0x7F0B0400
*/
f32 sub_GAME_7F0B0400(StandTile *tile, s32 start3index, f32 p_x, f32 p_z)
{
    f32 temp_f0;
    f32 temp_f2;
    f32 temp_f14;
    s32 var_a0;
    s32 padding;

    f32 tempf;
    s32 extra_padding[2];

    #ifdef DEBUG
    assert(ei<getsides(sf));
    #endif

    var_a0 = (start3index != 2) ? start3index + 1 : 0;

    start3index = STAN_MID_NIB(tile->tail.half, 8 - (start3index << 2));
    var_a0 = STAN_MID_NIB(tile->tail.half, 8 - (var_a0 << 2));

    temp_f2 = (f32)(tile->points[var_a0].x - tile->points[start3index].x);
    temp_f14 = (f32)(tile->points[var_a0].z - tile->points[start3index].z);

    temp_f0 = sqrtf((temp_f2 * temp_f2) + (temp_f14 * temp_f14));

    if (temp_f0 == 0.0f) {
        return 0.0f;
    }

    #ifdef DEBUG
    assert(d>0.0f);
    #endif

    tempf = (temp_f14 * (p_x - tile->points[start3index].x)) + ((p_z - tile->points[start3index].z) * -temp_f2);
    return tempf / temp_f0;
}




bool stanTestPointWithinTileBoundsMaybe(StandTile *tile, f32 p_x, f32 p_z)
{
    f32 unk;
    s32 i;

    p_x *= level_scale;
    p_z *= level_scale;

    for (i = 0; i != 3; i++)
    {
        unk = sub_GAME_7F0B0400(tile, i, p_x, p_z);

        if (unk < -2)
        {
            return FALSE;
        }
    }

    return TRUE;
}



// A->B ACWS returns 1, CWS (including opposite) returns -1.
// Identical direction and |A| >= |B| returns 0
int getRotationalDirectionBetween(f32 a_x,f32 a_z,f32 b_x,f32 b_z)
{
    // The main 2 cases : return the sign of AxB where it's non-zero
    if (a_z * b_x < a_x * b_z) {
        return 1;
    }
    if (a_x * b_z < a_z * b_x) {
        return -1;
    }

    // [AxB == 0 now]

    // If the vectors are opposite, default to clockwise
    if ((a_x * b_x < 0) || (a_z * b_z < 0)) {
      return -1;
    }

    // If A is shorter, return anti-clockwise
    if (a_x * a_x + a_z * a_z < b_x * b_x + b_z * b_z) {
        return 1;   // ACWS
    }

    // Identical direction, |A| >= |B|
    return 0;
}


/**
 * Address: 7F0B0688
 * Tests whether 2D segments start1->end1 and start2->end2 intersect, as a cheap
 * gate before calculateSegmentIntersectionFraction.
 */
bool doSegmentsIntersect(f32 start1X, f32 start1Z, f32 end1X, f32 end1Z, f32 start2X, f32 start2Z, f32 end2X, f32 end2Z)
{
    s32 unused1;
    s32 unused2;
    f32 start1RelX;
    f32 start1RelZ;
    f32 seg1Dx;
    f32 seg1Dz;
 
    start1RelX = start1X - start2X;
    start1RelZ = start1Z - start2Z;
    seg1Dx = end1X - start1X;
    seg1Dz = end1Z - start1Z;
 
    return
        (
            (getRotationalDirectionBetween(seg1Dx, seg1Dz, -start1RelX, -start1RelZ)
            * getRotationalDirectionBetween(seg1Dx, seg1Dz, end2X - start1X, end2Z - start1Z)) < 1)
        &&
        (
            (getRotationalDirectionBetween(end2X - start2X, end2Z - start2Z, start1RelX, start1RelZ)
            * getRotationalDirectionBetween(end2X - start2X, end2Z - start2Z, end1X - start2X, end1Z - start2Z)) < 1)
        ;
}


#if defined(LEFTOVERDEBUG)
s32 sub_GAME_7F0B07BC(f32 arg0, f32 arg1, f32 arg2, f32 arg3,
                      f32 arg4, f32 arg5, f32 arg6, f32 arg7, s32 arg8) {
    f32 a_x;
    f32 a_z;
    f32 b_x;
    f32 b_z;
    f32 c_x;
    f32 c_z;

    s32 val1;
    s32 unused4;
    s32 rc;
    s32 unused5;
    s32 unused6;
    s32 val2;
    s32 unused7;
    s32 unused8;
    s32 tmp;

    rc = 1;

    b_x = arg0 - arg4;
    b_z = arg1 - arg5;
    a_x = arg2 - arg0;
    a_z = arg3 - arg1;

    tmp = getRotationalDirectionBetween(a_x, a_z, -b_x, -b_z);
    val1 = tmp * getRotationalDirectionBetween(a_x, a_z, arg6 - arg0, arg7 - arg1);

    c_x = arg6 - arg4;
    c_z = arg7 - arg5;

    tmp = getRotationalDirectionBetween(c_x, c_z, b_x, b_z);
    val2 = tmp * getRotationalDirectionBetween(c_x, c_z, arg2 - arg4, arg3 - arg5);

    if (val1 >= arg8) {
        rc = 0;
    }

    if (val2 >= arg8) {
        rc = 0;
    }

    return rc;
}
#else
s32 sub_GAME_7F0B07BC(f32 arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4, f32 arg5, f32 arg6, f32 arg7, s32 arg8) {
    f32 a_z;
    f32 a_x;
    f32 b_z;
    f32 b_x;
    f32 c_x;

    s32 val2;
    s32 val1;
	f32 c_z;
    s32 rc;

    rc = 1;

    b_x = -(arg0 - arg4);
    b_z = -(arg1 - arg5);
	a_x = (arg2 - arg0);
    a_z = (arg3 - arg1);
    c_x = arg6 - arg0;
    c_z = arg7 - arg1;
    val1 = getRotationalDirectionBetween(a_x, a_z, b_x, b_z) * getRotationalDirectionBetween(a_x, a_z, c_x, c_z);

	a_x = (arg6 - arg4);
    a_z = (arg7 - arg5);
    b_x = (arg0 - arg4);
    b_z = (arg1 - arg5);
    c_x = arg2 - arg4;
    c_z = arg3 - arg5;

    val2 = getRotationalDirectionBetween(a_x, a_z, b_x, b_z) * getRotationalDirectionBetween(a_x, a_z, c_x, c_z);

    if (val1 >= arg8) {
        rc = 0;
    }

    if (val2 >= arg8) {
        rc = 0;
    }
    return rc;
}
#endif


bool sub_GAME_7F0B0914(StandTile **tileStack, f32 start_x, f32 start_z, f32 dest_x, f32 dest_z, standTileWalkCallback_t callback, struct StandTileWalkCallbackRecord *callbackData)
{
    StandTile *tile;
    StandTile *previousTile;
    StandTile *previousPreviousTile;
    StandTile *linkedTile;
    StandTile *nextTile;
    f32 lineNegDz;
    f32 lineDx;
    s32 uninitialized;
    s32 edgeIndex;
    s32 crossings;
    s32 iterationCount;
    s32 savedPointIndex;
    StandTilePoint *nextPoint;
    StandTilePoint *curPoint;
    s32 nextPointIndex;
    s32 hasLink;

#ifndef TARGET_N64
    g_PortHwWork[PORTHW_WALK_CALLS]++;
    /* PORT GUARD: a pad whose stan never resolved gives a NULL start tile,
     * which the N64 silently read through but crashes on PC. Report it and walk
     * nothing, so the object stays at its pad position (it may float). */
    if (*tileStack == NULL)
    {
        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
        static f32 count;
        f32 v[3];
        count += 1.0f;
        v[0] = count;
        v[1] = start_x;
        v[2] = start_z;
        ge_dbg_vec("stanwalk NULL start tile (count/x/z)", v, 3);
        return 0;
    }
#endif

    start_x *= level_scale;
    start_z *= level_scale;
    dest_x *= level_scale;
    dest_z *= level_scale;

    tile = *tileStack;
    previousTile = *tileStack;
    previousPreviousTile = *tileStack;
    lineNegDz = -(dest_z - start_z);
    crossings = 0;
    nextTile = NULL;
    iterationCount = 0;
    lineDx = dest_x - start_x;

    savedPointIndex = uninitialized;\
    while (1)
    {
        crossings = 0;

        if (callback)
        {
            callback(tile, previousTile, callbackData);
#ifdef DEBUG
            ossyncPrintf("{\"%s\",0x%08x,0x%08x,0x%08x,0x%08x},\t/* %8.3f %8.3f  %8.3f %8.3f */\n", GetStanRoomID(*tilestack), start_x, start_z, dest_x, dest_z, start_x, start_z);
#endif
        }

        curPoint = (StandTilePoint *) tile;

        for (edgeIndex = 0; edgeIndex < (tile->tail.hdrTail.pointCount & 0xF); edgeIndex++, curPoint++)
        {
            nextPointIndex = (edgeIndex + 1) % (tile->tail.hdrTail.pointCount & 0xF);
            nextPoint = &((StandTilePoint *) tile)[nextPointIndex];

            if (((lineNegDz * (nextPoint[1].x - curPoint[1].x)) + (lineDx * (nextPoint[1].z - curPoint[1].z))) <= 0.0f)
            {
                hasLink = curPoint[1].link >> 4 != 0;

                if (sub_GAME_7F0B07BC(start_x, start_z, dest_x, dest_z, curPoint[1].x, curPoint[1].z, nextPoint[1].x, nextPoint[1].z, hasLink))
                {
                    linkedTile = &standTileStart[curPoint[1].link];
                    crossings++;

                    if (previousTile != linkedTile && previousPreviousTile != linkedTile)
                    {
                        savedPointIndex = edgeIndex;
                        nextTile = curPoint[1].link >> 4 != 0 ? linkedTile : NULL;
                    }

                    // Optimized away but required to allocate edgeIndex to the target register.
                    if (edgeIndex && edgeIndex && edgeIndex)
                    {
                    }
                }
            }
        }

#ifdef DEBUG
        assert(crossings != 0);
        assert(crossings != 3);
        osSyncPrintf("sf: stanLineDo %d   %5.1f %5.1f %5.1f %5.1f  %s %s %s\n", 3, start_x, start_z, dest_x, dest_z, sub_GAME_7F0AEF3C(tile), sub_GAME_7F0AEF3C(previousTile), sub_GAME_7F0AEF3C(previousPreviousTile));
#endif

        previousPreviousTile = previousTile;
        previousTile = tile;

        if (tile == nextTile)
        {
            crossings = 0;
        }

        tile = nextTile;

        if (crossings == 0)
        {
            return TRUE;
        }

        if (iterationCount++ >= 0x1f5 || nextTile == NULL || crossings == 0)
        {
            stanSavedColl_tile = previousTile;
            stanSavedColl_pointI = savedPointIndex;
#ifdef DEBUG
            osSyncPrintf("stanLine: Looping; ret=0\n");
#endif
            return FALSE;
        }

        *tileStack = nextTile;
    }
}


/**
 * Name: walkTilesBetweenPoints_NoCallback
 * Address 0x7F0B0BE4.
*/
s32 walkTilesBetweenPoints_NoCallback(StandTile **tileStack, f32 start_x, f32 start_z, f32 dest_x, f32 dest_z)
{
    return sub_GAME_7F0B0914(tileStack, start_x, start_z, dest_x, dest_z, 0, 0);
}


/**
 * Name: walkTilesBetweenPoints_NotingRooms
 * Address 0x7F0B0BE4.
*/
s32 sub_GAME_7F0B0C24(StandTile **tileStack, f32 start_x, f32 start_z, f32 dest_x, f32 dest_z, s32 *roomBuffer, s32 *rtnCountSize, s32 maxBufSize)
{
    struct StandTileWalkCallbackRecord callbackData;
    s32 rtn;


    callbackData.roomBuf = roomBuffer;
    callbackData.count = 0;
    callbackData.bufMax = maxBufSize;
    callbackData.lastRoom = -1;

    rtn = sub_GAME_7F0B0914(tileStack, start_x, start_z, dest_x, dest_z, noteTileRoomIfDifferentToPrev, &callbackData);

    *rtnCountSize = callbackData.count;
    return rtn;
}


/*
* Address: 0x0x7f0b0c98
*/
void noteTileRoomIfDifferentToPrev(StandTile *tile, StandTile *unused, struct StandTileWalkCallbackRecord *data)
{
    s32 newRoom;

    if (tile->room != data->lastRoom && data->count < data->bufMax)
    {
        newRoom = (s32)tile->room;
        *data->roomBuf = newRoom;
        data->lastRoom = newRoom;
        data->roomBuf += 1;
        data->count += 1;
    }

    return;
}



/*
* Address: 0x7f0b0cec
*/
void noteTileRoomIfDifferentToPrev_2(StandTile *tile, StandTile *unused, struct StandTileWalkCallbackRecord *data) {
    noteTileRoomIfDifferentToPrev(tile, unused, data);
}


/**
 * Builds a list of room IDs between a start position and a destination position.
 * Only used for objects on set paths e.g. patrolling guards.
 */
s32 sub_GAME_7F0B0D0C(StandTile *tile, f32 start_x, f32 start_z, StandTile **destTile, f32 dest_x, f32 dest_z, s32 *roomBuffer, s32 maxBufSize)
{
    StandTile *savedTile;
    s32 count;

    savedTile = tile;
    count = 0;

    if (*destTile != NULL) {
        u8 roomA; // Source tile's room
        u8 roomB; // Destination tile's room

        roomA = tile->room;
        roomB = (*destTile)->room;

        // Fast path: start and destination tiles are in the same room, return a count of 1.
        if (roomB == roomA) {  
            roomBuffer[0] = roomA;
            return 1;
        }

        // Next fastest case: both rooms directly connected by a portal, write two room IDs and return a count of 2.
        if (bgRoomsSharePortal(roomA & 0xff, roomB & 0xff)) { 
            roomBuffer[0] = tile->room;
            roomBuffer[1] = (*destTile)->room;
            return 2;
        }
    }

    /**
     * Full path check: store the rooms between the points in roomBuffer and
     * their number in count, returning 0 if the path check fails.
     */
    if (!sub_GAME_7F0B0C24(&savedTile, start_x, start_z, dest_x, dest_z, roomBuffer, &count, maxBufSize)) {
        return 0;
    }

    if (maxBufSize < count) {
        count = maxBufSize;
    }

    if (*destTile == NULL) {
        *destTile = savedTile;
    }

    if (savedTile != *destTile) {
        #ifdef DEBUG
        osSyncPrintf("stan %s(%d) != %s(%d) from=%s\n", GetStanRoomID(savedTile),
        /*funcForTileNumber(savedTile)*/, GetStanRoomID(tile), /*funcForTileNumber(tile)*/, GetStanRoomID(roomBuffer));
        #endif
        return 0;
    }

    return count;
}


/**
 * Can change global variables:
 *
 * - D_800413BC
 * - stanSavedColl_pntA
 * - stanSavedColl_pntB
 * - stanSavedColl_tile
 * - stanSavedColl_pointI
 * - stanSavedColl_posData
 *
 * US address 7F0B0E24.
 *
 * 'testLineUnobstructed'
*/
s32 stanTestLineUnobstructed(StandTile **pTile, f32 p_x, f32 p_z, f32 dest_x, f32 dest_z, s32 cdtypes, f32 unkHeight, f32 unkA, f32 unkB, f32 unkC)
{
    struct PropRecord *prop;
    s32 retval; // sp158
    StandTile *sp154; // sp154
    struct coord2d sp14C;
    struct coord2d sp144;
    f32 sp140;
    s32 point_index;
    struct coord2d sp134;
    struct coord2d sp12C;
    s32 loop_flag;
    s32 sp124; // sp124
    s32 padding;
    s32 spD0[0x14]; //spD0
    s32 spCC; // spCC
    s32 next;
    f32 spC4;
    f32 spC0;
    f32 temp_f0_2;
    s16 *spB8;
    struct rect4f *polygon; // spB4
    s32 numvertices0; // spB0
    //f32 unused2;
    s32 i;
    f32 temp_f0;
    f32 spA4;
    f32 spA0; // spA0
    f32 temp_f2;
    s32 already_set;

    sp140 = 1.0f;
    sp124 = 0;
    spCC = (unkA <= unkHeight);
    already_set = 0;

    sp154 = *pTile;
    sp14C.f[0] = p_x;
    sp14C.f[1] = p_z;
    sp144.f[0] = dest_x;
    sp144.f[1] = dest_z;

    retval = sub_GAME_7F0B0C24(&sp154, p_x, p_z, dest_x, dest_z, &spD0[0], &sp124, 0x14);


    if (sp124 > 20)
    {
        #ifdef DEBUG
            osSyncPrintf("stanLineObjType: %d rooms is more than %d\n", retval, 20);
#endif

        sp124 = 20;
    }

    if (retval == 0)
    {
        s32 padding[2];

        point_index = (stanSavedColl_pointI + 1) % (s32)(STAN_PTCOUNT(stanSavedColl_tile->tail.half));
        D_800413BC = 1;

        stanSavedColl_pntA.f[0] = (f32) stanSavedColl_tile->points[stanSavedColl_pointI].x * inv_level_scale;
        stanSavedColl_pntA.f[1] = (f32) stanSavedColl_tile->points[stanSavedColl_pointI].z * inv_level_scale;

        stanSavedColl_pntB.f[0] = (f32) stanSavedColl_tile->points[point_index].x * inv_level_scale;
        stanSavedColl_pntB.f[1] = (f32) stanSavedColl_tile->points[point_index].z * inv_level_scale;

        sp140 = calculateSegmentIntersectionFraction(&sp14C, &sp144, &stanSavedColl_pntA, &stanSavedColl_pntB);
    }
    else
    {
        //
    }

    stanSavedColl_posData = NULL;

    if (cdtypes != 0)
    {
        spD0[sp124] = -1;
        roomGetProps((s32 *)&spD0);

        for (spB8 = ptr_list_object_lookup_indices; *spB8 >= 0; spB8++)
        {
            prop = &g_Props[*spB8];

            if (propIsOfCdType(prop, cdtypes) != 0)
            {
                chraiGetCollisionBounds(prop, &polygon, &numvertices0, &spA4, &spA0);

                if (numvertices0 > 0)
                {
                    for (i = 0; i < numvertices0; i++)
                    {
                        next = (i + 1) % numvertices0;

                        if (doSegmentsIntersect(p_x, p_z, dest_x, dest_z, polygon->points[i].f[0], polygon->points[i].f[1], polygon->points[next].f[0], polygon->points[next].f[1]) != 0)
                        {
                            sp134.f[0] = polygon->points[i].f[0];
                            sp134.f[1] = polygon->points[i].f[1];
                            sp12C.f[0] = polygon->points[next].f[0];
                            sp12C.f[1] = polygon->points[next].f[1];

                            temp_f0 = calculateSegmentIntersectionFraction(&sp14C, &sp144, &sp134, &sp12C);

                            if (temp_f0 < sp140)
                            {
                                loop_flag = 1;

                                if (spCC != 0)
                                {
                                    if (already_set == 0)
                                    {
                                        already_set = 1;

                                        if (unkC <= unkB)
                                        {
                                            spC4 = unkB - unkHeight;
                                            spC0 = unkC - unkA;
                                        }
                                        else
                                        {
                                            if (sp140 < 1.0f)
                                            {
                                                dest_x -= p_x;
                                                dest_x *= sp140;
                                                dest_x = p_x + dest_x;

                                                dest_z -= p_z;
                                                dest_z *= sp140;
                                                dest_z = p_z + dest_z;
                                            }

                                            temp_f0_2 = stanGetPositionYValue(*pTile, p_x, p_z);
                                            unkHeight += temp_f0_2;
                                            unkA += temp_f0_2;
                                            temp_f2 = (stanGetPositionYValue(sp154, dest_x, dest_z) - temp_f0_2) / sp140;
                                            spC0 = temp_f2;
                                            spC4 = temp_f2;
                                        }
                                    }

                                    if ((spA4 <= ((spC0 * temp_f0) + unkA)) || (((spC4 * temp_f0) + unkHeight) <= spA0))
                                    {
                                        loop_flag = 0;
                                    }
                                }

                                if (loop_flag != 0)
                                {
                                    retval = 0;
                                    sp140 = temp_f0;
                                    D_800413BC = 1;
                                    stanSavedColl_pntA = sp134;
                                    stanSavedColl_pntB = sp12C;
                                    stanSavedColl_tile = NULL;
                                    stanSavedColl_pointI = 0;
                                    stanSavedColl_posData = prop;
                                    #ifndef TARGET_N64
                                    g_portBlockProp = prop;   /* GE_BLOCKDBG provenance - see bondview2.c */
                                    g_portBlockSrc = 1;
                                    g_portBlockTop = spA4; g_portBlockBot = spA0;
                                    g_portBlockLo = (spC0 * temp_f0) + unkA;
                                    g_portBlockHi = (spC4 * temp_f0) + unkHeight;
                                    g_portBlockGate = spCC;
                                    g_portBlockTick = g_GlobalTimer;
                                    #endif
                                    sp154 = NULL;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (sp154 == NULL)
    {
        sp154 = *pTile;

        dest_x -= p_x;
        dest_x *= sp140;
        dest_x = p_x + dest_x;

        dest_z -= p_z;
        dest_z *= sp140;
        dest_z = p_z + dest_z;
        /*stanlineret = */ walkTilesBetweenPoints_NoCallback(&sp154, p_x, p_z, dest_x, dest_z);
        #ifdef DEBUG
        assert(stanlineret==1)
        #endif
    }

    *pTile = sp154;
    stanSavedColl_someMin = sp140;

    return retval;
}


PropRecord *sub_GAME_7F0B1410(StandTile *t, f32 start_x, f32 start_z, f32 end_x, f32 end_z, s32 cdtypes)
{
    f32 frac;
    PropRecord *prop;
    coord2d lineStart;
    coord2d lineEnd;
    struct coord2d *tmp;
    s32 pad;
    coord2d edgeStart;
    coord2d edgeEnd;
    s32 i;
    StandTile *tile;
    s32 roomCount;
    s32 roomBuffer[21];
    s16 *propIndexPtr;
    struct rect4f *polygon;
    s32 numEdges;
    PropRecord *bestProp;
    s32 next;
    f32 bestFrac;

    bestProp = NULL;
    bestFrac = 1.0f;

    tile = t;
    roomCount = 0;

    sub_GAME_7F0B0C24(&tile, start_x, start_z, end_x, end_z, roomBuffer, &roomCount, 20);

    if (roomCount >= 21)
    {
        // The comment below was in the unmatched function. I have left it where I think it was most likely meant to go.
        //osSyncPrintf("stanLineDoor: %d rooms is more than %d\n");
        roomCount = 20;
    }

    lineStart.f[0] = start_x;
    lineStart.f[1] = start_z;
    lineEnd.f[0] = end_x;
    lineEnd.f[1] = end_z;

    if (cdtypes != 0)
    {
        roomBuffer[roomCount] = -1;
        roomGetProps(roomBuffer);

        propIndexPtr = ptr_list_object_lookup_indices;

        // Fake but needed for matching.
        if (polygon);

        if (*propIndexPtr >= 0)
        {
            do
            {
                prop = &g_Props[*propIndexPtr];

                if (propIsOfCdType(prop, cdtypes))
                {
                    chraiGetCollisionBoundsWithoutY(prop, &polygon, &numEdges);

                    if (numEdges > 0)
                    {
                        i = 0;

                        while (i < numEdges)
                        {
                            next = (i + 1) % numEdges;

                            if (doSegmentsIntersect(start_x, start_z, end_x, end_z, polygon->points[i].f[0], polygon->points[i].f[1], polygon->points[next].f[0], polygon->points[next].f[1]))
                            {
                                edgeStart.f[0] = polygon->points[i].f[0];
                                tmp = &polygon->points[i];
                                edgeStart.f[1] = (*tmp).f[1];

                                edgeEnd.f[0] = polygon->points[next].f[0];
                                edgeEnd.f[1] = polygon->points[next].f[1];

                                frac = calculateSegmentIntersectionFraction(&lineStart, &lineEnd, &edgeStart, &edgeEnd);

                                if (frac < bestFrac)
                                {
                                    bestFrac = frac;
                                    bestProp = prop;
                                }
                            }

                            i++;
                        }
                    }
                }

                propIndexPtr++;
            }
            while (*propIndexPtr >= 0);
        }
    }

    return bestProp;
}


/**
 * Address: 7F0B16C4
 * 
 * Computes the signed perpendicular distance from point P to the infinite
 * line that goes through point A and point B.
 * The sign indicates which side of the line the point is on.
 */
f32 stanGetSignedPointLineDistance(f32 a_x, f32 a_z, f32 b_x, f32 b_z, f32 p_x, f32 p_z)
{
    u32 stack[8];
    f32 result; //d

    result = sqrtf((b_x - a_x) * (b_x - a_x) + (b_z - a_z) * (b_z - a_z));

    if (result == 0.0f)
    {
        return sqrtf((p_x - b_x) * (p_x - b_x) + (p_z - b_z) * (p_z - b_z));
    }
    #ifdef DEBUG
    assert(d>0.0F);
    #endif
    return ((b_z - a_z) * (p_x - a_x) + -(b_x - a_x) * (p_z - a_z)) / result;
}


f32 distBetweenPoints2d(f32 o_x,f32 o_z,f32 p_x,f32 p_z)
{
    p_x -= o_x;
    p_z -= o_z;
    return sqrtf(p_x * p_x + p_z * p_z);
}


/**
 * Address: 7F0B17E4
 * 
 * Tests whether a point P's perpendicular projection onto the infinite line
 * going through points A and B falls inside the finite edge segment.
 */
bool stanPointProjectsOntoEdge(f32 a_x, f32 a_z, f32 b_x, f32 b_z, f32 p_x, f32 p_z)
{
    f32 f0;
    f32 f2;
    f32 f16;
    f32 f18;

    p_x -= a_x;
    p_z -= a_z;

    f0 = b_x - a_x;
    f2 = b_z - a_z;

    f16 = p_x * f0 + p_z * f2;
    f18 = f0 * f0 + f2 * f2;

    return (f18 < f16 && f16 < 0) || (f16 > 0 && f16 < f18);
}


/**
 * Can change global variables
 * - D_800413BC
 * - stanSavedColl_pntA
 * - stanSavedColl_pntB
 * - stanSavedColl_tile
 * - stanSavedColl_pointI
 * - stanSavedColl_posData
 *
 * US address 7F0B18B8.
 * Perfect Dark cdTestVolume (from context)
*/
s32 stanTestVolume(StandTile **arg0, f32 arg1, f32 arg2, f32 arg3, s32 cdtypes, f32 arg5, f32 arg6)
{
    s32 i; // stack ??
    f32 var_f20; // stack ??
    f32 var_f24; // stack ??
    s32 temp_v0; // stack ??
    s32 next; // stack ??

    s32 sp108;
    f32 temp_f0;  // stack ??
    s16 *sp100;
    s32 spFC;
    struct PropRecord *prop; // no stack
    s32 spA8[0x14];
    struct rect4f *polygon;
    s32 numvertices0;  // spa0
    f32 temp_f0_3; // stack ??
    f32 temp_f0_2; // stack ??
    f32 sp94;
    f32 sp90;

    s32 padding1;
    s32 padding2;

    sp108 = (arg6 <= arg5);

    spFC = 0;

    temp_v0 = sub_GAME_7F0B21B0(arg0, arg1, arg2, arg3, &spA8[0], &spFC, 20);
    if (temp_v0 >= 0)
    {
        return temp_v0;
    }


    if (spFC > 20)
    {
        #ifdef DEBUG
            osSyncPrintf("stanCircleLegalXFObjTypeY: %d rooms is more than %d\n",spFC,20);
        #endif
        spFC = 20;
    }

    stanSavedColl_posData = NULL;

    if (cdtypes)
    {
        if (sp108)
        {
            temp_f0 = stanGetPositionYValue(*arg0, arg1, arg2);
            #ifndef TARGET_N64
            g_portBlockFloor = temp_f0; g_portBlockTileUsed = (void *) *arg0; g_portBlockTX = arg1; g_portBlockTZ = arg2;
            #endif
            arg5 += temp_f0;
            arg6 += temp_f0;
        }

        spA8[spFC] = -1;
        #ifndef TARGET_N64
        {   s32 rr;
            g_portBlockRoomCount = (spFC < 24) ? spFC : 24;
            for (rr = 0; rr < g_portBlockRoomCount; rr++) { g_portBlockRooms[rr] = spA8[rr]; }
        }
        #endif
        roomGetProps(&spA8[0]);

        for (sp100 = ptr_list_object_lookup_indices; *sp100 >= 0; sp100++)
        {
            prop = &g_Props[*sp100];

            if (propIsOfCdType(prop, cdtypes) != 0)
            {
                chraiGetCollisionBounds(prop, &polygon, &numvertices0, &sp94, &sp90);
                if ((numvertices0 > 0) && ((sp108 == 0) || ((sp90 <= arg5) && (arg6 <= sp94))))
                {
                    var_f24 = -1.0f;

                    i=0;
                    while(1)
                    {
                        next = (i + 1) % numvertices0;

                        var_f20 = stanGetSignedPointLineDistance(polygon->points[i].f[0], polygon->points[i].f[1], polygon->points[next].f[0], polygon->points[next].f[1], arg1, arg2);

                        if (var_f20 < 0.0f)
                        {
                            var_f20 = -var_f20;
                        }

                        if (var_f24 < var_f20)
                        {
                            temp_f0_2 = distBetweenPoints2d(polygon->points[i].f[0], polygon->points[i].f[1], arg1, arg2);
                            temp_f0_3 = distBetweenPoints2d(polygon->points[next].f[0], polygon->points[next].f[1], arg1, arg2);

                            if ((var_f20 < arg3)
                                && (
                                    (temp_f0_2 < arg3)
                                    || (temp_f0_3 < arg3)
                                    || (stanPointProjectsOntoEdge(polygon->points[i].f[0], polygon->points[i].f[1], polygon->points[next].f[0], polygon->points[next].f[1], arg1, arg2) != 0)))
                            {
                                D_800413BC = 1;
                                var_f24 = var_f20;

                                stanSavedColl_pntA.f[0] = polygon->points[i].f[0];
                                stanSavedColl_pntA.f[1] = polygon->points[i].f[1];
                                stanSavedColl_pntB.f[0] = polygon->points[next].f[0];
                                stanSavedColl_pntB.f[1] = polygon->points[next].f[1];
                                stanSavedColl_tile = NULL;
                                stanSavedColl_pointI = 0;
                                stanSavedColl_posData = prop;
                                #ifndef TARGET_N64
                                g_portBlockProp = prop;   /* GE_BLOCKDBG provenance - see bondview2.c */
                                g_portBlockSrc = 2;
                                g_portBlockTop = sp94; g_portBlockBot = sp90;
                                g_portBlockLo = arg6; g_portBlockHi = arg5;
                                g_portBlockGate = sp108;
                                g_portBlockTick = g_GlobalTimer;
                                #endif
                            }
                        }

                        if (next == 0)
                        {
                            break;
                        }

                        i = next;
                    }

                    if (var_f24 > -1.0f)
                    {
                        return 2;
                    }
                }
            }
        }
    }

    return -2;
}



//stanResetHits
void stanResetHits(void) {
    stanSavedColl_tile = 0;
    stanSavedColl_pointI = 0;
    D_800413BC = 0;
}

StandTile *sub_GAME_7F0B1CE0(void)
{
    #ifdef DEBUG
        osSyncPrintf("Don\'t call stanCircleLegalHit()!\n");
    #endif
    return stanSavedColl_tile;
}

s32 sub_GAME_7F0B1CEC(void)
{
    #ifdef DEBUG
        osSyncPrintf("Don\'t call stanCircleLegalHitEdge()!\n");
    #endif

    return stanSavedColl_pointI;
}


void getTileEdgePoints(StandTile *tile, s32 pointI, coord3d *currPntRtn, coord3d *nextPointRtn)
{
    f32 scale;

    scale = inv_level_scale;

    currPntRtn->x = tile->points[pointI].x * scale;
    currPntRtn->y = tile->points[pointI].y * scale;
    currPntRtn->z = tile->points[pointI].z * scale;

    /**
     * This line could potentially become:
     * pointI = (pointI + 1) % STAN_POINT_COUNT(tile);
     * 
     * If STAN_POINT_COUNT were redefined as:
     * #define STAN_POINT_COUNT(tile) (STAN_PTCOUNT((tile)->tail.half))
     * Something to consider?
     */
    pointI = (pointI + 1) % (STAN_PTCOUNT(tile->tail.half));

    nextPointRtn->x = tile->points[pointI].x * scale;
    nextPointRtn->y = tile->points[pointI].y * scale;
    nextPointRtn->z = tile->points[pointI].z * scale;
}


/**
 * Traverses linked stan tiles that intersect a circle at the given X/Z
 * position; callbacks can observe tiles, reject edges, or continue past
 * blocking edges. Returns STAN_COLLISION_FOUND, STAN_COLLISION_TRAVERSAL_LIMIT
 * (tile stack full) or STAN_COLLISION_NONE.
 */
StanCollisionResult sub_GAME_7F0B1DDC(StandTile **startTile, f32 x, f32 z, f32 radius, standTileLocusCallback_A_t callbackA, standTileLocusCallback_B_t callbackB, standTileLocusCallback_C_t callbackC, struct StandTileLocusCallbackRecord *record)
{
    s32 i;
    StandTile *tileStack[39];
    StandTile *tile;
    StandTile *linkedTile;
    s32 visitedCount;
    s32 cat;
    s32 pointI;
    s32 pointINext;
    s32 nextPointI;
    f32 edgeDist;
    f32 pointDistA;
    f32 pointDistB;

    x *= level_scale;
    z *= level_scale;
    radius *= level_scale;
    visitedCount = 0;
    cat = 1;
    tileStack[0] = *startTile;

    do
    {
        tile = tileStack[visitedCount++];
        pointI = 0;

#ifndef TARGET_N64
        if (tile == NULL)
        { /* bring-up (GE_DUMP_BOND): a NULL got into the walk stack -
           * dump the state so the pushing link is identifiable */
            extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
            static s32 once;
            if (once < 4)
            {
                f32 v[8];
                s32 h;
                once++;
                v[0] = (f32) visitedCount;
                v[1] = (f32) cat;
                v[2] = (f32) (u32) *startTile;
                v[3] = (f32) (u32) standTileStart;
                for (h = 0; h < 4; h++)
                {
                    v[4 + h] = (f32) (u32) tileStack[h];
                }
                ge_dbg_vec("locusnull visited/cat/start/stanbase/s0..s3", v, 8);
            }
        }
#endif

        if (callbackA != NULL)
        {
            callbackA(tile, record);
        }

        if ((STAN_PTCOUNT(tile->tail.half)) > 0)
        {
            do
            {
                pointINext = pointI;
                do
                {
                    i = cat;
                    pointINext = pointINext + 1;
                    if (i);

                    nextPointI = pointINext;
                    nextPointI %= STAN_PTCOUNT(tile->tail.half);
                    edgeDist = getShortest2dDispToInfTileEdge(tile, pointI, x, z);
                    pointDistA = distToTilePnt2D(tile, pointI, x, z);
                    pointDistB = distToTilePnt2D(tile, nextPointI, x, z);

                    if (edgeDist < radius && (pointDistA < radius || pointDistB < radius || stanPointProjectsOntoTileEdge(tile, pointI, x, z)))
                    {
                        if ((callbackB == NULL || !callbackB(tile, pointI, edgeDist, pointDistA, pointDistB, record)) && (tile->points[pointI].link >> 4))
                        {
                            linkedTile = ((uintptr_t) standTileStart) + (tile->points[pointI].link << 3);
                            for (i = cat - 1; i >= 0; i--)
                            {
                                if (linkedTile == tileStack[i])
                                {
                                    goto next_point;
                                }
                            }
#ifdef TARGET_N64
                            tileStack[cat] = linkedTile;
                            cat++;
                            goto next_point;
#else
                            /* Bound the push: a dense tile area can overflow
                             * the 39-slot stack (the N64 overwrote its own
                             * stack frame). Dropping the link just visits fewer
                             * tiles. */
                            if (cat < (s32) (sizeof(tileStack) / sizeof(tileStack[0]))) {
                                tileStack[cat] = linkedTile;
                                cat++;
                            }
                            goto next_point;
#endif
                        }

                        stanSavedColl_tile = tile;
                        stanSavedColl_pointI = pointI;
                        if (callbackC != NULL && callbackC(tileStack, cat, record) == 1)
                        {
                            goto next_point;
                        }
                        return STAN_COLLISION_FOUND;
                    }
                    next_point:
                    pointI = pointINext;
                }
                while (FALSE);
            }
            while (pointINext < (STAN_PTCOUNT((*tile).tail.half)));
        }

#ifdef DEBUG
        if (cat>= 0x190)
        {
            osSyncPrintf("cat=%d !!!!!!!!!!!!!!!!!!!! NEED TO INCREASE ARRAY SIZE\n",cat);
        }
#endif
        // if (cat >= 0x320)
        // {
        //     return 5; //error value?
        // }

        if (((u32) cat) >= 41)
        {
            return STAN_COLLISION_TRAVERSAL_LIMIT;
        }
    }
    while (visitedCount < cat);

    visitedCount = radius;

    if (callbackC != NULL)
    {
        if (visitedCount);
        callbackC(tileStack, cat, record);
    }

    return STAN_COLLISION_NONE;
}




s32 sub_GAME_7F0B20D0(StandTile **tileStack, f32 target_x, f32 target_z, f32 unknown) {
    return sub_GAME_7F0B1DDC(tileStack, target_x, target_z, unknown, NULL, NULL, NULL, NULL);
}


/**
 * Address: 7F0B2110
 * Stan traversal callback that adds the tile's room ID to the caller's room
 * list if not already there, always returning 0 so traversal continues.
 */
s32 stanLocusAddTileRoomIfNew(StandTile *tile, struct StandTileLocusCallbackRecord *rec)
{
    s32 roomCount;
    s32 room;
    StandTile *t;
    struct StandTileLocusCallbackRecord *record;
    s32 i;
    s32 *ptr;
    
    record = rec;
    i = 0;
    roomCount = record->count;
    t = tile;
    
    // Only search for duplicates if more than 0 rooms have been collected.
    if (roomCount > 0)
    {
        room = t->room;
        ptr = record->rooms;
        
        //If the tile's room is already in the room list, return immediately.
        do
        {
            if (room == (*ptr))
            {
                return 0;
            }
            
            i++;
            ptr++;
        }
        while (i < record->count);
    }
    
    // The room has not been collected yet so append it.
    if (roomCount < rec->bufMax)
    {
        rec->rooms[roomCount] = tile->room;
        rec->count = rec->count + 1;
    }
    
    return 0;
}


s32 incrNearEdgeCount(StandTile **tileStack, s32 stackHeight, struct StandTileLocusCallbackRecord* data) {
    data->nearEdgeCount += 1;
    return 1;
}


/**
 * Address: 7F0B21B0
 */
s32 sub_GAME_7F0B21B0(StandTile **tileStack, f32 target_x, f32 target_z, f32 radius, s32 *rooms, s32 *count_rtn, s32 bufMax)
{
    struct StandTileLocusCallbackRecord data;
    s32 rtn;

    data.rooms = rooms;
    data.count = 0;
    data.bufMax = bufMax;
    data.nearEdgeCount = 0;

    rtn = sub_GAME_7F0B1DDC(tileStack, target_x, target_z, radius,
        stanLocusAddTileRoomIfNew, NULL, incrNearEdgeCount, &data
    );

    *count_rtn = data.count;

    if (1 < data.nearEdgeCount) {
        return 2;
    }

    return rtn;
}


/**
 * Address 0x7F0B2244.
*/
s32 stanIsSpecialBit1Set(StandTile *arg0, struct StandTileLocusCallbackRecord *arg1)
{
    s32 val = STAN_MID_NIB(arg0->mid.half, 12);
    if (g_StanTileSpecialFlags[val] & STANTILEFLAG_FORCECROUCH)
    {
        arg1->rooms = (s32 *)1;
    }

    return 0;
}


/**
 * Address: 7F0B2274
 */
s32 stanCheckLinkedSpecialTile(StandTile *tile, s32 pointIdx, f32 arg2, f32 arg3, f32 arg4, struct StandTileLocusCallbackRecord *record)
{
    u16 link;
    StandTile *target;
    s32 mid;

    link = tile->points[pointIdx].link;

    if ((link >> 4) != 0) {
        target = (StandTile *)(link + (StandTile *)standTileStart);

        mid = target->mid.half;

        if (g_StanTileSpecialFlags[STAN_MID_NIB(mid, 12)] & STANTILEFLAG_FORCECROUCH) {
            record->rooms = (s32 *)1;
            return 1;
        }

        mid = target->mid.half;

        if (g_StanTileSpecialFlags[STAN_MID_NIB(mid, 12)] & STANTILEFLAG_LADDER) {
            dword_CODE_bss_8007BA0C = target;
            record->count = 1;
            return 0;
        }
    }

    return 0;
}


/**
 * Address 0x7F0B2314.
*/
s32 stanTileDistanceRelated(StandTile **arg0, f32 arg1, f32 arg2, f32 arg3, struct StandTileLocusCallbackRecord *arg4)
{
    s32 i;

#ifdef TARGET_N64
    // HACK:
    for(i=0;;)
    {
        ((s32*)arg4)[i+0] = 0;
        ((s32*)arg4)[i+1] = 0;
        ((s32*)arg4)[i+2] = 0;
        ((s32*)arg4)[i+3] = 0;
        i+=4;
        if (i>15) break;
    }
#else
    /* The callbacks share the record's fields with room collection. Use the
     * native field offsets: on 64-bit hosts rooms occupies two s32 words. */
    arg4->rooms = NULL;
    arg4->count = 0;
    arg4->bufMax = 0;
    arg4->nearEdgeCount = 0;
    (void) i;
#endif

    // maybe something like:
    /*
    for(i=0;i<3;i++)
    {
        arg4[i].unk00 = 0;
        arg4[i].count = 0;
        arg4[i].bufMax = 0;
        arg4[i].nearEdgeCount = 0;
    }
    */

    return sub_GAME_7F0B1DDC(arg0, arg1, arg2, arg3, stanIsSpecialBit1Set, stanCheckLinkedSpecialTile, NULL, arg4);
}


s32 *stanGetLocusField0(struct StandTileLocusCallbackRecord *arg0)
{
    return arg0->rooms;
}


s32 stanGetLocusCount(struct StandTileLocusCallbackRecord *arg0)
{
    return arg0->count;
}


/**
 * Address: 7F0B23AC
 */
void stanGetTileOrderedPointWorldPos(StandTile *tile, s32 pointnum, coord3d *out)
{
    StandTilePoint *point;
    f32 scale;

#ifdef TARGET_N64
    pointnum = tile->tail.half >> (8 - (pointnum * 4));
#else
    /* the mask lands at the array index below - only the shift flips */
    pointnum = tile->tail.half >> (12 - (8 - (pointnum * 4)));
#endif

    point = &tile->points[pointnum & 0xf];

    scale = inv_level_scale;

    out->x = point->x * scale;
    out->y = point->y * scale;
    out->z = point->z * scale;
}


void stanGetMoveBondCollisionTiles(StandTile **tile1, StandTile **tile2, coord3d *coords)
{
    StandTile *curtileStore;
    StandTile *baseTile;
    StandTile *linktile;
    s32 curtilePointI;
    s32 i;
    s32 j;
    s32 k;
    s32 target;

    baseTile = dword_CODE_bss_8007BA0C;

    target = baseTile->tail.hdrTail.pointCount & 0xf;

    i = 0;

    if (i < target)
    {
        do
        {
            linktile = (StandTile *)((u8 *)standTileStart + (baseTile->points[i].link << 3));

            if ((baseTile->points[i].link >> 4) != 0)
            {
                s32 linkTileMid;
            
                linkTileMid = linktile->mid.half;
            
                if (g_StanTileSpecialFlags[STAN_MID_NIB(linkTileMid, 12)] & STANTILEFLAG_LADDER)
                {
#ifdef DEBUG
                    assert(getsides(linktile) == 3);
#endif
                    curtilePointI = (i + 2) % 3;
            
                    *tile1 = baseTile;
                    *tile2 = linktile;
            
                    j = 0;
                    curtileStore = baseTile;

                    while (1)
                    {
                        for (k = 0; k < 3; k++)
                        {
                            stanGetTileOrderedPointWorldPos(
                                linktile,
                                ((j >> 2) + k) % 3,
                                (coord3d *)((intptr_t)coords + (((j + k) & 3) * 0xc)));
                        }

                        stanGetTileOrderedPointWorldPos(
                            curtileStore,
                            curtilePointI,
                            (coord3d *)((intptr_t)coords + (((j + 3) & 3) * 0xc)));

                        j++;

                        if (j == 12)
                        {
#ifdef DEBUG
                            osSyncPrintf("rotate==12\n");
#endif
                            break;
                        }

                        if (!(coords[2].y < coords[0].y)
                                && !(coords[2].y < coords[1].y)
                                && !(coords[3].y < coords[0].y)
                                && !(coords[3].y < coords[1].y))
                        {
                            break;
                        }
                    }

                    return;
                }
            }

            i++;

            if (i < target)
            {
                continue;
            }

            break;
        }
        while (TRUE);
    }
#ifdef DEBUG
    osSyncPrintf("Ladder %s has no neighbouring ladder stan\n", GetStanRoomID(baseTile));
#endif
}


/**
 * Address: 7F0B260C
 * 
 * Callback function.
 * 
 * For a given edge, return true if the edge is vertically above yThreshold.
 */
bool stanLocusEdgeIsAboveY(StandTile *tile, s32 edgeIndex, f32 edgeDist, f32 distToPointA, f32 distToPointB, struct StandTileLocusCallbackRecord *record)
{
    f32 *yThreshold = (f32 *)record;
    s32 nextIndex;
    s32 pointCount;
    f32 *threshold;
    s32 pointCountReload;

    threshold = yThreshold;

    if (*yThreshold < (f32)tile->points[edgeIndex].y)
    {
        /**  
         * The duplicated point count calculation is required for matching.
         * This is really just nextIndex = (edgeIndex + 1) % pointCount;
         */
        pointCount = STAN_PTCOUNT(tile->tail.half);
        pointCountReload = STAN_PTCOUNT(tile->tail.half);

        nextIndex = (edgeIndex + 1) % pointCount;

        pointCount = pointCountReload;

        if (*threshold < (f32)tile->points[nextIndex].y)
        {
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * US address 7F0B26B8.
*/
s32 stanTestLocusEdgeAboveY(StandTile **tile, f32 target_x, f32 target_z, f32 radius, f32 yThreshold)
{
    f32 data;

    data = yThreshold * level_scale;

    /// TODO: Why is this cast wrong?

    return sub_GAME_7F0B1DDC(tile, target_x, target_z, radius, NULL, stanLocusEdgeIsAboveY, NULL, (struct StandTileLocusCallbackRecord*)&data);
}


typedef struct BfsSearchLocals {
    StandTile **stackptr;
    s32 pad48;
    s32 pad4c;
    s32 pad50;
    s32 pad54;
    s32 lastnumtiles;
} BfsSearchLocals;


// Slow breadth-first search over tiles. Possible improvements:
// 1. Each wave restarts at bfsTileStack[0], rescanning tiles from earlier
//    waves; it could resume from loc.lastnumtiles.
// 2. The visited check is linear in seenCount, which grows with every tile
//    found; a faster visited set would help on long searches.
// 3. The closest pad to each tile is static and could be precomputed.
// 4. There is a single caller and the second argument is always the same,
//    so the tilePred result could be returned directly.
StandTile *stanFillSearch(StandTile *starttile, tilePredicate_t predicate) // stanFillSearch is the canonical name for this function
{
    StandTile **stackbase;
    BfsSearchLocals loc;
    StandTile **tileStartAddr;
    StandTilePoint *point;
    StandTile *linkedtile;
    s32 seenCount;
    s32 pointindex;
    s32 pointcount;
    s32 link;
    s32 i;
    s32 stackindex;
    StandTile **tileStack;

    if (predicate(starttile))
    {
        return starttile;
    }

    tileStartAddr = &standTileStart;

    // Add the starting tile to the discovered tile stack.
    bfsTileStack[0] = starttile;
    seenCount = 1;

    // Process the discovered tiles in waves. loc.lastnumtiles is the number of tiles that existed at the beginning of the current wave.
    for (loc.lastnumtiles = 1, tileStack = bfsTileStack, stackbase = tileStack;; loc.lastnumtiles = seenCount)
    {
        stackindex = 0;

        if (seenCount > 0)
        {
            // This restarts at the beginning of the stack for every wave rather than beginning at the new frontier.
            loc.stackptr = tileStack;

            do
            {
                pointindex = 0;
                starttile = *loc.stackptr;
                point = (StandTilePoint *)starttile;
                pointcount = STAN_PTCOUNT(starttile->tail.half);

                // Loop over the tile's linked points.
                if (pointcount > 0)
                {
                    do
                    {
                        link = point[1].link;

                        // This assignment is logically redundant but needed for matching.
                        tileStartAddr = &standTileStart;

                        // Keep this expression as written. Simplifying it to "link << 3" changes register usage.
                        linkedtile = (StandTile *)(((u8 *)(*tileStartAddr)) + (link << ((0, 3))));

                        // A zero high portion indicates that there is no valid linked tile to visit.
                        if ((link >> 4) != 0)
                        {
                            // Loop to see if this is a new tile (disgusting)
                            for (i = 0; i < seenCount; i++)
                            {
                                if (linkedtile == tileStack[i])
                                {
                                    goto nextpoint;
                                }
                            }

                            // Return as soon as a matching tile is found.
                            if (predicate(linkedtile))
                            {
                                return linkedtile;
                            }

                            // Add the newly discovered tile to the stack.
                            stackbase[seenCount] = linkedtile;
                            seenCount++;

                            if ((u32)seenCount >= 351)
                            {
                                #ifdef DEBUG
                                    osSyncPrintf("Out of confs[] in stanFillSearch()\n");
                                #endif
                                return 0;
                            }

                            pointcount = STAN_PTCOUNT(starttile->tail.half);
                        }

nextpoint:
                        pointindex++;
                        point++;
                    }
                    while (pointindex < pointcount);
                }

                stackindex++;
                loc.stackptr++;
            }
            while (stackindex < loc.lastnumtiles);
        }

        if (predicate || tileStartAddr);

        // We only continue if we made progress with this iteration
        if (seenCount != loc.lastnumtiles)
        {
            // This is logically redundant because both pointers refer to bfsTileStack, but it's still needed for matching.
            stackbase = tileStack;

            continue;
        }

        // No new tiles were discovered so the search is exhausted.
        return 0;
    }
}


/**
 * @param pntA: out parameter, will contain stanSavedColl_pntA (x,z)
 * @param pntB: out parameter, will contain stanSavedColl_pntB (x,z)
 */
bool getCollisionEdge_maybe(coord3d *pntA, coord3d *pntB)
{
    if (stanSavedColl_tile)
    {
        getTileEdgePoints(stanSavedColl_tile, stanSavedColl_pointI, pntA, pntB);

        return TRUE;
    }
    else
    {
        if (D_800413BC)
        {
            pntA->x = stanSavedColl_pntA.f[0];
            pntA->y = 0;
            pntA->z = stanSavedColl_pntA.f[1];

            pntB->x = stanSavedColl_pntB.f[0];
            pntB->y = 0;
            pntB->z = stanSavedColl_pntB.f[1];

            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
}



void setLevelScale(f32 ls)
{
    level_scale = ls;
    inv_level_scale = (1.0f / ls);
    #ifdef DEBUG
    if (level_scale != 1.0)
    {
        osSyncPrintf("%5.2fm squared total area\n", /*lots of math*/ 1* inv_level_scale * inv_level_scale * 0.01 * 0.01);
        //...
        osSyncPrintf("%5.2fm squared BB extent\n\n",/*more maths*/ 1 * inv_level_scale * inv_level_scale * 0.01 * 0.01);
    }
    #endif
    return;
}




/**
 * Calculates y value on a tile, according to (x,z) position.
 *
 * Address 0x7F0B2970.
 */
f32 stanGetPositionYValue(StandTile *tile, f32 p_x, f32 p_z)
{
    f32 a[3]; // sp 132, vector a
    f32 b[3]; // sp 120, vector b
    s64 cp[3]; // sp 96, cross product vector (a x b)
    s64 rsum;
    s32 temp_a3;
    s32 temp_t6;
    s32 temp_t7;

#ifndef TARGET_N64
    /* Some pads (including MP spawn pads) have no stan tile; the N64 read
     * garbage through the NULL tile, which crashes on PC. Return ground level 0
     * instead; the setup lint already flags these pads at load. */
    if (tile == NULL)
    {
        return 0.0f;
    }
#endif

    p_x *= level_scale;
    temp_t6 = STAN_MID_NIB(tile->tail.half, 8);
    temp_t7 = STAN_MID_NIB(tile->tail.half, 4);
    temp_a3 = STAN_MID_NIB(tile->tail.half, 0);
    p_z *= level_scale;

    a[0] = (f32) (tile->points[temp_t7].x - tile->points[temp_t6].x);
    a[1] = (f32) (tile->points[temp_t7].y - tile->points[temp_t6].y);
    a[2] = (f32) (tile->points[temp_t7].z - tile->points[temp_t6].z);

    b[0] = (f32) (tile->points[temp_a3].x - tile->points[temp_t6].x);
    b[1] = (f32) (tile->points[temp_a3].y - tile->points[temp_t6].y);
    b[2] = (f32) (tile->points[temp_a3].z - tile->points[temp_t6].z);

    // implicit call to __f_to_ll
    // This is the cross product, a x b
    cp[0] = (s64)((a[1] * b[2]) - (a[2] * b[1]));
    cp[1] = (s64)((a[2] * b[0]) - (a[0] * b[2]));
    cp[2] = (s64)((a[0] * b[1]) - (a[1] * b[0]));

    // implicit call to __ll_mul
    rsum = ((s64)cp[0] * (s64)tile->points[temp_t6].x)
        + ((s64)cp[1] * (s64)tile->points[temp_t6].y)
        + ((s64)cp[2] * (s64)tile->points[temp_t6].z);

    // don't divide by zero
    if (cp[1] == 0)
    {
        return (f32) tile->points[temp_t6].y * inv_level_scale;
    }

    return (f32) ((((f64)(rsum) - ((f64) p_x * (f64)cp[0])) - ((f64) p_z * (f64)cp[2])) / (f64)(cp[1])) * inv_level_scale;
}


void copy_tile_RGB_as_24bit(StandTile *tile, f32 p_x, f32 p_z, u8 *rtn)
{
#ifndef TARGET_N64
    /* LAME-PAD class (see stanGetPositionYValue): the MP spawn's NULL
     * stan reaches the tile-colour read too - neutral white instead of
     * an AV. */
    if (tile == NULL)
    {
        rtn[0] = rtn[1] = rtn[2] = 0xFF;
        return;
    }
#endif
    u8 B = STAN_MID_NIB(tile->mid.half, 8);
    u8 C = STAN_MID_NIB(tile->mid.half, 4);
    u8 D = STAN_MID_NIB(tile->mid.half, 0);
    rtn[0] = (B << 0x4) | B;
    rtn[1] = (C << 0x4) | C;
    rtn[2] = (D << 0x4) | D;
}


/**
 * Address: 7F0B2C74
 */
void stanGetTileHeaderCYBounds(StandTile *tile, f32 *out)
{
    f32 y0;
    f32 y1;
    f32 y2;
    f32 min;
    f32 max;

     /*
     * @bug All three reads use headerC ((tail >> 8) & 0xf) where headerC/D/E were clearly intended.
     * It doesn't matter: the call chain leads nowhere, so this is dead code.
     */
    y0 = (f32)tile->points[(tile->tail.half >> 8) & 0xf].y;
    y1 = (f32)tile->points[(tile->tail.half >> 8) & 0xf].y;
    y2 = (f32)tile->points[(tile->tail.half >> 8) & 0xf].y;

    min = y1;

    if (y0 < y1)
    {
        min = y0;
    }

    if (y2 < min)
    {
        min = y2;
    }

    max = y0;

    if (y0 < y1)
    {
        max = y1;
    }

    if (max < y2)
    {
        max = y2;
    }

    out[0] = min * inv_level_scale;
    out[1] = max * inv_level_scale;
}


/**
 * Address: 7F0B2D14
 */
f32 stanGetTileHeaderCMinY(StandTile *tile) {
    f32 vs[2];

    stanGetTileHeaderCYBounds(tile, vs);
    return vs[0];
}


void debugStanView(s8 joyX, s8 joyY, u16 joyBtns) {
    return;
}


/**
 * Address: 7F0B2D48
 */
Gfx * stanRenderDebugStanView(Gfx *arg0) {
    return arg0;
}


 /**
 * Get 24bit id stanIdHi from id string
 * @param stanIdHi: 1bit Type, 15bit Integer ID.
 * @param stanIdLo: 5bit stanIdLo File (a-z) and 3bit subtri 0-7
 * canonically Named
 */
void stanPackId(char *id, u16 *stanIdHi, u8 *stanIdLo)
{
    u32   bitsnumber; // sp3c
    char *str_end;    // sp38
    s32   y;          // sp34
    s32   bitsletter; // sp30
    s32   bitsfile;   // sp2c
    s32   bitssubtri; // sp28
    s32   var1;       // sp24

    var1 = id[0] - 'p';

    if (var1 < 0 || var1 > 1) // is p or q the first char (q never used?)
    {
        #ifdef ENABLE_LOG
            osSyncPrintf("stanPackId(): Bad letter chr \'%c\' in \"%s\"\n", id[0], id);
        #endif
    }
    else
    {
        bitsletter = id[0] - 'p'; // yes this is right, Im duplicated.
        bitsnumber = strtol(id + 1, &str_end, 10);

        if (id == str_end - 1)
        {
            #ifdef ENABLE_LOG
                osSyncPrintf("stanPackId(): Bad integer in \"%s\"\n", id);
            #endif
        }
        else if (bitsnumber <= 32767)
        {
            if (str_end[0] - 'a' < 0 || str_end[0] - 'a' >= 26)
            {
                #ifdef ENABLE_LOG
                    osSyncPrintf("stanPackId(): Bad file chr \'%c\' in \"%s\"\n", str_end[0], id);
                #endif
            }
            else
            {
                bitsfile = str_end[0] - 'a';
                if (1)
                    ;
                bitssubtri = str_end[1];

                if (bitssubtri != 0 && bitssubtri != '0')
                {
                    bitssubtri -= '0';
                }

                if (bitssubtri < 0 || bitssubtri >= 8)
                {
                    #ifdef ENABLE_LOG
                        osSyncPrintf("stanPackId(): Bad subtri chr \'%c\' in \"%s\"\n", str_end[1], id);
                    #endif
                }
                else
                {
                    if (str_end[1] == 0 || str_end[2] == 0)
                    {
                        #ifdef DEBUG
                        assert(bitsletter>=0&&bitsletter<=1);     // # 1094 "stan.c"
                        assert(bitsnumber>=0&&bitsnumber<=32767); // # 1095 "stan.c"
                        assert(bitsfile >=0&&bitsfile <=31);      // # 1096 "stan.c"
                        assert(bitssubtri>=0&&bitssubtri<=7);     // # 1097 "stan.c"
                        #endif
                        *stanIdHi = bitsletter << 0xf | bitsnumber;
                        *stanIdLo = bitsfile << 3 | bitssubtri;
                        return;
                    }
                    else
                    {
                        #ifdef ENABLE_LOG
                            osSyncPrintf("stanPackId(): Stan id too long \"%s\"\n", id);
                        #endif
                    }
                }
            }
        }
        else
        {
            #ifdef ENABLE_LOG
                osSyncPrintf("stanPackId(): Integer %d out of range in \"%s\"\n", bitsnumber, id);
            #endif
        }
    }
    *stanIdHi = -1;
    *stanIdLo = -1;
    return;
}


struct StandTilePoint *stanMatchTileName(char *id)
{
    StandTilePoint *tile;
    u16 stanIdHi;
    u8 stanIdLo;
    s16 tmp;

#ifndef TARGET_N64
    g_PortHwWork[PORTHW_MATCH_CALLS]++;
#endif
    if (*id == '\0') {
        return NULL;
    }

    stanPackId(id, &stanIdHi, &stanIdLo);

    tile = stan_prefix->ptr_firstroom;

#ifndef TARGET_N64
    { /* bring-up (GE_DUMP_BOND, once): what does the matcher's walk
       * actually see - first ids, walk length, terminator address. */
        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
        static s32 once;
        if (!once) {
            StandTilePoint *t = tile;
            s32 walked = 0;
            f32 v[8];
            once = 1;
            v[0] = (f32) (u32) t;
            v[1] = (f32) ((StandTile *) t)->id;
            while (*(u32 *) t != 0 && walked < 100000) {
                s16 h = t->link;
                walked++;
                t = (StandTilePoint *) ((u8 *) t + list_of_tilesizes[STAN_PTCOUNT(h)]);
            }
            v[2] = (f32) walked;
            v[3] = (f32) (u32) t;
            v[4] = (f32) sizeof(StandTilePoint);
            v[5] = (f32) (u32) &(((StandTilePoint *) 0)->link);
            v[6] = (f32) (u32) &(((StandTile *) 0)->points);
            v[7] = (f32) (u16) tile->link;
            ge_dbg_vec("stanmatch start/id0/walked/end/szpt/offlink/offpts/link0", v, 8);
        }
    }
#endif

    while (*(u32 *)tile != 0) {
#ifndef TARGET_N64
        g_PortHwWork[PORTHW_MATCH_STEPS]++;
#endif
#ifdef TARGET_N64
        if ((u16)tile->x == stanIdHi) {
            if (*((u8 *)&tile->y) == stanIdLo) {
                return tile;
            }
        }
#else
        /* Read the id bitfield, not the big-endian byte positions the N64 used:
         * on little-endian those bytes hold the wrong bits. Otherwise no tile
         * ever matches and every pad resolves to a NULL stan. */
        {
            u32 tid = ((StandTile *) tile)->id;

            if ((u16) (tid >> 8) == stanIdHi && (u8) tid == stanIdLo) {
                return tile;
            }
        }
#endif

        tmp = tile->link;
        tile = (StandTilePoint *)((u8 *)tile +
            list_of_tilesizes[STAN_PTCOUNT(tmp)]);
    }

    return NULL;
}


#ifdef XBLADEBUG
StandTile RemovedDebugFunctionOrXBLAUnique_7F0B2EFC()
{
    lVar1 = param_2;
    local_10 = *(stanPrefix + 4);
    cStack00000017 = param_1;
    sStack0000001e = param_2;
    while( true ) {
    if (*local_10 == 0) {
        return NULL;
    }
    cVar2 = sStack0000001e;
    if ((*local_10 == sStack0000001e) && (cVar2 = cStack00000017, *(local_10 + 2) == cStack00000017)
        ) break;
    local_10 = Function_8238ED08(local_10,lVar1,in_r5,in_r6,in_r7,in_r8,in_r9,cVar2,
                                    in_stack_ffffffab,in_stack_ffffffaf,in_stack_ffffffb4);
    }
    return local_10;
}
#endif


void sub_GAME_7F0B2F00(StandTilePoint** arg0) {
    *arg0 = stanMatchTileName(*arg0);
}


void stanDetermineEOF(struct StanPrefixRecord *file /* canonically r */, s32 origBase, u8 *newBase)
{
    intptr_t delta;
    void **roomPtr;
    StandTile *tile;
    u8 *tileSizes;
    
    delta = ((intptr_t) newBase) - origBase;
#ifndef TARGET_N64
    /* PC: compiled-in stan files already hold real pointers, so the
     * offset-to-pointer rebase must be zero or every room pointer is doubled. A
     * zero delta also makes stage reloads safe to repeat. */
    /* NULL here means the asset never linked. Do not use an address-range
     * check instead: Windows can place globals below 0x10000000. */
    if (file->ptr_firstroom != NULL) {
        delta = 0;
        /* Swap to the packed byte-image, since the per-global tiles can't be
         * walked in place (see portStanPack). Its room slots already hold real
         * pointers, and a stan served from the ROM is already in this form. */
        if (!g_portStanFromRom) {
            file = portStanPack(file);
        }
    } else {
        printf("ge: stanDetermineEOF: NULL ptr_firstroom (missing stan asset?) hdr=%p\n",
               (void *) file);
    }
#endif
    stan_prefix = file;
    
    #ifdef DEBUG
    assert(*r==0);
    #endif
  
    standTileStart = (StandTile *)(((intptr_t)file->ptr_firstroom + delta) - 0x80);
    ptr_firstroom_0 = (intptr_t)file->ptr_firstroom + delta;
    
    newBase = list_of_tilesizes;
    roomPtr = (void **)&file->ptr_firstroom;
    
#ifdef TARGET_N64
    if (file->ptr_firstroom != NULL)
    {
        do
        {
            *roomPtr = (void *) ((s32) (*roomPtr) + delta);
            roomPtr++;
        }
        while (*roomPtr != NULL);
    }

    tile = (StandTile *) (roomPtr + ((0, 1)));
#else
    /* The file is the packed image, whose room slots are already real pointers,
     * so the N64 offset-promotion loop is skipped. Running it over real heap
     * pointers could mangle any whose low 16 bits are zero. */
    (void) roomPtr;
    tile = (StandTile *) ptr_firstroom_0;
#endif
    
    if ((*(s32 *) tile) != 0)
    {
        do
        {
            stanTileEnd = tile;

            // Fake but required for matching.
            if (tile->tail.half);

#ifndef TARGET_N64
            { /* bring-up (GE_DUMP_BOND): tile-walk stride audit - print the
               * first steps so a desync against the generated tile symbols
               * is visible immediately. */
                extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
                static s32 steps;
                if (steps < 8) {
                    f32 v[10];
                    s32 h;
                    steps++;
                    v[0] = (f32) (u32) tile;
                    v[1] = (f32) ((u8 *) list_of_tilesizes)[STAN_PTCOUNT(tile->tail.half)];
                    for (h = 0; h < 8; h++) {
                        v[2 + h] = (f32) ((u16 *) tile)[h];
                    }
                    ge_dbg_vec("stanwalk addr/step/h0..h7", v, 10);
                }
            }
#endif

            tile = (StandTile *)((intptr_t)tile
                + (tileSizes = newBase)[STAN_PTCOUNT(tile->tail.half)]);
        }
        while (*(s32 *) tile != 0);
    }
    
    stan_prefix = file;
}


/**
 Get the room the tile belongs to
 @param tile: Tile to quiry
 @return the room number the tile is located in
 @exception Although room is u8, this needs to be s32 for matching ai.
 */
s32 getTileRoom(StandTile *tile)
{
    return tile->room;
}


//incorrect here so that both this and sub_GAME_7F0B4F9C match
extern s32 sub_GAME_7F0B4F9C(u8 arg0) ;

s32 sub_GAME_7F0B2FE0(StandTile *tile)
{
    // u8 -> s32 -> u8 causes the odd asm

    s32 room = tile->room;

    return sub_GAME_7F0B4F9C(room);
}

/**
 * Address: 7F0B3004
 * 
 * Unused.
 */
f32 stanGetTileHeaderCMinYWrapper(StandTile *tile) {
    return stanGetTileHeaderCMinY(tile);
}

Gfx * sub_GAME_7F0B3024(Gfx *ptrdl, StandTilePoint *tile_point, u32 RGBAColor) {
    return ptrdl;
}

Gfx * sub_GAME_7F0B3034(Gfx *arg0) {
    return arg0;
}

Gfx * sub_GAME_7F0B303C(Gfx * arg0) {
    return arg0;
}

s32 sub_GAME_7F0B3044(void) {
    s32 sp1C;
    f32 temp_f0;

    sp1C = 0;
    if (((dynGetFreeGfx(dynGetMasterDisplayList()) < 0x1000) || (dynGetFreeVtx() < 0x1000)) && (*D_800413D0 == 0)) {
        D_800413C0 = 0.0f;
        D_800413C4 = 0.0f;
        D_800413C8 = D_800413CC;
        *D_800413D0 = 1;
    }
    if (*D_800413D0 == 0) {
        D_800413C0 += D_800413C4;
        temp_f0 = D_800413C0;
        if (temp_f0 > 1.0f) {
            sp1C = 1;
            D_800413C0 = temp_f0 - 1.0f;
        }
    }
    D_800413CC += 1;
    return sp1C;
}

Gfx * sub_GAME_7F0B312C(Gfx *arg0, s32 arg1)
{
    #ifdef DEBUG
      qword *pqVar1;
      qword *pqVar2;
      int iVar3;
      char cVar4;
      ulonglong uVar5;
      ulonglong uVar6;
      uint uStack00000014;
      stanRecord *psStack0000001c;
      char cStack00000027;
      ushort **ppuStack0000002c;
      uint uStack00000034;
      dword local_80;
      char *local_30;
      dword local_2c;
      uint local_28;
      dword local_24;
      dword local_20;
      qword local_10;
      dword local_8;

      uStack00000034 = param_5;
      ppuStack0000002c = param_4;
      uVar6 = ZEXT48(param_2);
      *ppuStack0000002c = param_2;
      local_30 = &DAT_00000001;
      uStack00000014 = param_1;
      psStack0000001c = param_2;
      cStack00000027 = param_3;
      pqVar1 = Function_8237F158();
      pqVar2 = Function_8237F058();
      uVar5 = 0;
      Function_82278968(pqVar2,pqVar1,0,param_4,param_5,param_6,uVar6);
      do {
        do {
          if (local_30 == NULL) {
            Function_82279088();
            return;
          }
          local_30 = local_30 + -1;
          psStack0000001c = ppuStack0000002c[local_30];
        } while (*psStack0000001c >> 0xf == cStack00000027);
        *psStack0000001c = *psStack0000001c ^ 0x8000;
        stanTileHasZeroArea(psStack0000001c);
        iVar3 = sub_GAME_7F0B3044();
        if (iVar3 != 0) {
          Function_8238AC90(psStack0000001c,uStack00000014,uVar5,param_4,param_5,param_6,uVar6);
        }
        for (local_28 = 0; cVar4 = getsides(psStack0000001c), local_28 < cVar4; local_28 = local_28 + 1)
        {
          if (psStack0000001c[local_28 + 1].tail >> 4 == 0) {
            iVar3 = sub_GAME_7F0B3044();
            if (iVar3 != 0) {
              uVar5 = local_28 + 1;
              cVar4 = getsides(psStack0000001c);
              trapWord(6,cVar4,0);
              trapWord(5,cVar4 & ~(((uVar5 & 0x7fffffff) << 1 | (uVar5 << 0x20) >> 0x3f) - 1),0xffff);
              uVar5 = uVar5 - (uVar5 / cVar4) * cVar4;
              param_4 = 0xffffffffffffffc0;
              Function_8238B8F0(psStack0000001c,local_28,uVar5,0xffffffffffffffc0,param_5,param_6,uVar6)
              ;
            }
          }
          else if (*(stanTileStart + psStack0000001c[local_28 + 1].tail) >> 0xf != cStack00000027) {
            uVar6 = ZEXT48(ppuStack0000002c);
            ppuStack0000002c[local_30] = stanTileStart + psStack0000001c[local_28 + 1].tail;
            local_30 = local_30 + 1;
            if (uStack00000034 < local_30) {
              osSyncPrintf("stanFillinVis: Stack overflow %d>%d",local_30,uStack00000034);
              return;
            }
          }
        }
      } while( TRUE );
    #endif
    return arg0;
}


/**
 * Unreferenced.
 */
s32 sub_GAME_7F0B3138(StandTile *tile, StandTile **pTile, f32 p_x, f32 p_z, f32 dest_x, f32 dest_z, s32 cdtypes, f32 unkHeight, f32 unkA)
{
    // Fake but needed for matching.
    if (pTile);

    return stanTestLineUnobstructed(pTile, p_x, p_z, dest_x, dest_z, cdtypes, unkHeight, unkA, 0.0f, 1.0f);
}


void sub_GAME_7F0B31A4(s32 arg0, StandTile *arg1, f32 arg2, f32 arg3, f32 arg4, s32 arg5, f32 arg6, f32 arg7) {
    stanTestVolume(arg1, arg2, arg3, arg4, arg5, arg6, arg7);
}



