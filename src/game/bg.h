#ifndef _BG_H_
#define _BG_H_
#include <ultra64.h>
#include <bondgame.h>
#include <bondtypes.h>
#include <bondconstants.h>

struct levelentry
{
    s32 levelID;
    void *bg_seg_filename;
    void *bg_stan_filename;
    f32 levelscale;
    f32 visibility;
    f32 unknownfloat;
};
// cannon definition
#define MAXPORTALSPERROOM 20

// cannonical name
#define PORTMAX 200

#ifdef TARGET_N64
#define BG_SEG_TO_PTR(base, off) ((void *) (((uintptr_t) (base)) + (((uintptr_t) (off)) + 0xF1000000)))
#else
/* PC: compiled-in bg files already hold real pointers, which pass through
 * unchanged; only values below 0x400000 or tagged with segment 0x0F are treated
 * as offsets (a `>= 0x10000000` test breaks on Windows). */
#define BG_SEG_IS_OFFSET(off) \
    ((uintptr_t) (off) < 0x00400000u || (((uintptr_t) (off)) >> 24) == 0x0Fu)
#define BG_SEG_TO_PTR(base, off) \
    (BG_SEG_IS_OFFSET(off) ? ((void *) (((uintptr_t) (base)) + (((uintptr_t) (off)) & 0x00FFFFFFu))) \
                           : ((void *) (uintptr_t) (off)))
#endif

typedef struct RoomVtxBatchBounds {
    s16 gdlindex;    // 0x00
    s16 pad02;       // 0x02

    union {
        struct {
            s32 xmin; // 0x04
            s32 ymin; // 0x08
            s32 zmin; // 0x0c
            s32 xmax; // 0x10
            s32 ymax; // 0x14
            s32 zmax; // 0x18
        };

        struct {
            s32 min[3]; // 0x04
            s32 max[3]; // 0x10
        };
    };
} RoomVtxBatchBounds; // size = 0x1c

typedef struct BoundVec { 
    s32 x, y, z; 
} BoundVec;

typedef struct s_room_info {
    // is room being rendered? boolean
    u8 room_rendered;                       // 0x00

    // is the room a neighbor to a room being rendered? boolean
    u8 room_neighbor_to_rendered;           // 0x01

    /**
     * Acts like a small room age counter.
     * 0 = unloaded
     * 1 = loaded/used recently
     * 2-3 = loaded but aging towards unload
     * 4 = unload on tick
     */
    u8 model_bin_loaded;                    // 0x02

    /**
     * Counts how often this room has been reached during the current portal
     * visibility traversal.
     */
    u8 portal_visit_count;                  // 0x03

    Vtx *vertices;                          // 0x04
    void *ptr_expanded_mapping_info;        // 0x08
    void *ptr_secondary_expanded_mapping_info; // 0x0c

    s32 csize_point_index_binary;           // 0x10
    s32 csize_primary_DL_binary;            // 0x14
    s32 csize_secondary_DL_binary;          // 0x18

    s32 usize_point_index_binary;           // 0x1c
    s32 usize_primary_DL_binary;            // 0x20
    s32 usize_secondary_DL_binary;          // 0x24

    s32 cur_room_totalsize;                 // 0x28
    RoomVtxBatchBounds *vtx_batch_bounds;   // 0x2c

    s16 num_vtx_batch_bounds;               // 0x30
    s16 field_32;                           // 0x32

    u8 room_loaded_mask;                    // 0x34
    u8 field_35;                            // 0x35
    s16 field_36;                           // 0x36

    coord3d minbounds;                      // 0x38
    coord3d maxbounds;                      // 0x44
} s_room_info; 

typedef struct s_bound_info
{
    #if defined(VERSION_EU)
    //eu is 0x18 total len
    u8 roomid;
    u8 pad1;
    // could be draw order?
    s16 unk1;
    u8 next;
    u8 pad2[3];
    struct bbox2d bbox;

    #else
    //us is 0x1C total len
    s32 roomid;
    // could be draw order?
    s32 unk1;
    struct bbox2d bbox;
    s32 next;
    #endif


} s_bound_info;

typedef struct bg_portal_entry
{
    u8 numPoints;
    u8 padding[3];
    coord3d point;
} bg_portal_entry;

typedef struct bg_portal_data_entry
{
    bg_portal_entry *offset_portal;
    u8 connectedRoom1;
    u8 connectedRoom2;
    u8 controlbytes1;
    u8 controlbytes2;
} bg_portal_data_entry;

typedef struct bg_room_data
{
    void* pPointTableBin;
    void* pPriMappingBin;
    void* pSecMappingBin;
    coord3d pos;
} bg_room_data;

#ifdef GE_NATIVE_64
bg_room_data *romfileBgNativeRooms(const void *data);
bg_portal_data_entry *romfileBgNativePortals(const void *data);
#endif

typedef struct s_specialportal
{
    u8 levelid;
    u8 portallist[];
} s_specialportal;

typedef struct unk_portalstruct
{
    s32 unk0;
    s32 unk4;
    s32 unk8;
    s32 unkC;
    s32 unk10;
} unk_portalstruct;

typedef struct PortalCache {
    s32 count;
    bbox2d bbox;
} PortalCache;

typedef struct bg_queued_portal_entry {

    #if defined(VERSION_EU)
    u8 arg0;           // 0x00
    u8 roomnum;        // 0x01
    s16 portalnum;     // 0x02
    f32 sp4[4];        // 0x04
    #else
    s32 arg0;          // 0x00
    s32 roomnum;       // 0x04
    s32 portalnum;     // 0x08
    s32 arg3;          // 0x0c
    f32 sp10[4];       // 0x10
    #endif
} bg_queued_portal_entry;

extern bg_portal_data_entry *g_BgPortals;
extern struct unk_portalstruct table_for_portals[PORTMAX];
extern s32 g_MaxNumRooms;
extern f32 room_data_float2;

extern bg_room_data * ptr_bgdata_room_fileposition_list;
extern s_room_info g_BgRoomInfo[];
extern Gfx *ptrDynamic_CC_RM_LUT[];
extern Gfx DL_LUT_PRIMARY_ADDFOG[];


void bgInit(void);

// sub_GAME_7F033B38 requres arg be s32
bool bgRoomsSharePortal(s32 roomA, s32 roomB);

//f32 sub_GAME_7F0B4F9C(s32 room); // u8 not s32 for sub_GAME_7F0B2FE0
s32 bgCopyVisibleRoomsToList(s32 *rooms, s32 max);
s32 bgCopyDrawnRoomsToList(s32 *rooms, s32 max);
u32 bgDecompress(u8* source, u8 *target);
bool bgTestBulletHitBackground(coord3d *from, coord3d *to, s32 roomnum, struct HitThing *hit);
void delete_room_data(s32 roomID);
void load_bg_file(LEVEL_INDEX stagenum);

s32 bgDebugRemoved7F0B9DE4(s32 arg0, s32 arg1, s32 arg2);
void bgRemoved7F0B9DF4(s32 arg0);
s8 bgSwapConnectedRooms(s32 index);
s32 bgGetDataPortalsControlBytes1Bit1(s32 index);
void bgToggleDataPortalsContrlBytes1Bit1(s32 index, s32 toggle);
s32 bgGetDataPortalsControlBytes1Bit2(s32 arg0);
void bgClearDataPortalsControlBytes1Low2Bits(s32 index);
void bgSetDataPortalsControlBytes1Bit2(s32 index);
void sub_GAME_7F0B9A7C(s32 portalnum);
void sub_GAME_7F0B9A2C(s32 portalnum);
void bgRoomVisibilityRelated(void);
Gfx* bgLevelRender(Gfx *arg0);
Gfx *bgScissorCurrentPlayerView(Gfx *arg0, s32 left, s32 top, s32 width, s32 height);
Gfx* bgScissorCurrentPlayerViewDefault(Gfx* arg0);
Gfx* bgScissorCurrentPlayerViewF(Gfx* arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4);
f32 get_room_data_float1(void);
u8 getROOMID_isRendered(int roomID);
s32 bgGet2dBboxByRoomId(s32 room_id, struct bbox2d *result);
f32 bgGetLevelVisibilityScale(void);
void bgRectOutersect(struct bbox2d *a, struct bbox2d *b);
f32 get_room_data_float2(void);
s32 bgGetPortalBetweenRooms(s32 arg0, s32 arg1, struct coord3d *arg2, struct coord3d *arg3);
void sub_GAME_7F0B96CC(s32 portalnum, f32 *out);
void bgApplyDynamicCCRMLUT(Gfx *arg0, Gfx *arg1, enum CCRMLUT arg2);
void sub_GAME_7F0BA2D4(coord3d *, coord3d *, s32 *, s32 *, s32);
#ifndef TARGET_N64
/* Sentinel for "no room in this level has a measurable bounding box", so a
 * caller can tell that apart from a genuine floor height and do nothing. */
#define BG_LEVEL_FLOOR_NONE 3.0e38f
/* Lowest point of any room's bounding box, in ROOM-BOUNDS space
 * (world * room_data_float1). Computed once per level load; returns
 * BG_LEVEL_FLOOR_NONE when no room had a real box. See bg.c. */
f32 bgGetLevelFloorY(void);
#endif
void bgFindRoomsAlongSegment(coord3d *pos1, coord3d *pos2, u8 *initialRooms, u8 *outRoomSet, s32 *outRoomNums, s32 *outRoomNumsCount, s32 outRoomNumsMax);
s32 sub_GAME_7F0B9E04(coord3d *arg0, coord3d *arg1);
void bgRoomCalcBB(s32 room);
void bgGetRoomCenter(s32 roomnum, coord3d *dst);

char *bgDebPrintROOMID(s32 roomId);


s32 getIndexOfPORTALID(intptr_t portalID);
s32 sub_GAME_7F0B993C(s32 arg0);
bool bgProjectRoomCoordToScreen(coord3d* src, coord3d* dst);
#ifdef VERSION_EU
void sub_GAME_7F0B7F84(s32 roomnum, s32 portalnum, s32 depth, bbox2d *parentbox);
#else
s32 sub_GAME_7F0B7F84(s32 value, s32 roomnum, s32 portalnum, s32 depth, bbox2d *parentbox);
#endif
s32 getMaxNumRooms(void);
u8 getROOMID_isNeighborToRendered(s32 roomID);
bool bgTestRayIntersectsBbox(coord3d *origin, coord3d *dir, s32 *bbox_min, s32 *bbox_max);
s32 bgGetConnectedRooms(s32 roomIndex, s32* list, s32 max);
void cleanup_rooms(void);

#endif
