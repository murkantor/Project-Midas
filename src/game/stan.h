#ifndef _STAN_H_
#define _STAN_H_
#include <ultra64.h>

#include <bondtypes.h>

struct StanPrefixRecord {
    s32 stanfile;
    StandTile *ptr_firstroom;
};

// RGB? I've called them 'triple' because I don't really know what RGB is
// No parens around params
#define STAN_TRIPLE_TO_PNT_INDEX(tile, tripleIndex) (tile->hdrTail >> (8 - 4*tripleIndex) & 0xF)
#define STAN_POINT_COUNT(tile) (tile->tail.half & 0xF)

#define STAN_MID_SPECIAL(tile) (tile->mid.half & 0xF)
#define STAN_MID_R(tile) ((tile->mid.half >> 0x04) & 0xF)
#define STAN_MID_G(tile) ((tile->mid.half >> 0x08) & 0xF)
#define STAN_MID_B(tile) ((tile->mid.half >> 0x0C) & 0xF)

/* Extract pointCount from a tail halfword: the matched code uses IDO's
 * MSB-first bitfield position (top nibble, >>12) but GCC packs LSB-first, so
 * the compiled-in stan data holds it in the low nibble. One macro handles both
 * packings; the N64 arm expands to the original expression. */
#ifdef TARGET_N64
#define STAN_PTCOUNT(tailhalf) (((tailhalf) >> 12) & 0xf)
/* nibble of mid.half by its IDO shift (special=12, r=8, g=4, b=0) */
#define STAN_MID_NIB(half, ido_shift) (((half) >> (ido_shift)) & 0xF)
#else
#define STAN_PTCOUNT(tailhalf) ((tailhalf) & 0xf)
/* GCC packs the bitfields LSB-first: every nibble position mirrors */
#define STAN_MID_NIB(half, ido_shift) (((half) >> (12 - (ido_shift))) & 0xF)
#endif

#define STAN_TAIL_POINT_COUNT(tile) (tile->tail.half & 0xF)/*canonically getsides()*/
#define STAN_TAIL_C(tile) ((tile->tail.half >> 0x04) & 0xF)
#define STAN_TAIL_D(tile) ((tile->tail.half >> 0x08) & 0xF)
#define STAN_TAIL_E(tile) ((tile->tail.half >> 0x0C) & 0xF)

struct move_bond_collision {
    struct coord3d bondCollision;
    struct coord3d sp190;
    struct coord3d sp19C;
    struct coord3d sp1A8;
};

typedef enum StanCollisionResult {
    STAN_COLLISION_NONE = -2,
    STAN_COLLISION_FOUND = 2,
    STAN_COLLISION_TRAVERSAL_LIMIT = 5
} StanCollisionResult;

typedef struct StanRoomBounds {
    union {
        struct {
            s16 minX;
            s16 minY;
            s16 minZ;
            s16 maxX;
            s16 maxY;
            s16 maxZ;
        };
        struct {
            s16 min[3];
            s16 max[3];
        };
    };
} StanRoomBounds;

/////////////////
// extern

extern f32 stanSavedColl_someMin;
extern PropRecord *stanSavedColl_posData;
extern struct StandTile *standTileStart;
extern s32 stanlinelog_flag;
extern StandTile *firststaninroom[139];
extern StanRoomBounds g_StanRoomBounds[139];
extern s32 dword_CODE_bss_8007B9DC;

/////////////////
// prototypes

// Necessary forward declaration
void noteTileRoomIfDifferentToPrev( StandTile *tile,  StandTile *unused,  struct StandTileWalkCallbackRecord *data);

void stanInit(void);
void setLevelScale(f32 ls);
void debugStanView(s8 joyX, s8 joyY, u16 joyBtns);
void sub_GAME_7F0AF630(s32 arg0);
void stanResetHits(void);
Gfx * stanRenderDebugStanView(Gfx *arg0);
Gfx * sub_GAME_7F0B303C(Gfx *arg0);
Gfx * sub_GAME_7F0B3034(Gfx *arg0);
Gfx * sub_GAME_7F0B312C(Gfx *arg0, s32 arg1);
Gfx * sub_GAME_7F0B3024(Gfx *ptrdl, StandTilePoint *tile_point, u32 RGBAColor);
s32 walkTilesBetweenPoints_NoCallback(StandTile **tileStack, f32 start_x, f32 start_z, f32 dest_x, f32 dest_z);
s32 stanTestPointWithinTileBoundsMaybe(StandTile *tile, f32 p_x, f32 p_z);
f32 stanGetPositionYValue(StandTile* tile, f32 p_x, f32 p_z);
s32 getCollisionEdge_maybe(coord3d *pntA, coord3d *pntB);
s32 stanTestLocusEdgeAboveY(StandTile **tile, f32 target_x, f32 target_z, f32 radius, f32 yThreshold);
s32 sub_GAME_7F0B20D0(StandTile** tileStack, f32 target_x, f32 target_z, f32 unknown);

s32 stanTestLineUnobstructed(StandTile **pTile, f32 p_x, f32 p_z, f32 dest_x, f32 dest_z, int cdtypes, f32 unkHeight, f32 unkA, f32 unkB, f32 unkC);
StandTile* stanFillSearch(StandTile* srcTile, tilePredicate_t tilePred);
s32 sub_GAME_7F0B0D0C(StandTile *tile, f32 start_x, f32 start_z, StandTile **destTile, f32 dest_x, f32 dest_z, s32 *roomBuffer, s32 maxBufSize);
s32 sub_GAME_7F0B0C24(StandTile **tileStack, f32 start_x, f32 start_z, f32 dest_x, f32 dest_z, s32 *roomBuffer, s32 *rtnCountSize, s32 maxBufSize);
s32 stanTestVolume(StandTile **, f32 posX, f32 posY, f32 radius, s32 cdtypes, f32 float1, f32 float2);
s32 getTileRoom(StandTile* tile);
PropRecord *sub_GAME_7F0B1410(StandTile *arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4, s32 arg5);
void copy_tile_RGB_as_24bit(StandTile *tile, f32 p_x, f32 p_z, u8 *rtn);
s32 stanTileDistanceRelated(struct StandTile **arg0, f32 arg1, f32 arg2, f32 arg3, struct StandTileLocusCallbackRecord *arg4);
s32 *stanGetLocusField0(struct StandTileLocusCallbackRecord *arg0);
s32 stanGetLocusCount(struct StandTileLocusCallbackRecord *arg0);
f32 distBetweenPoints2d(f32 o_x,f32 o_z,f32 p_x,f32 p_z);
bool stanPointProjectsOntoEdge(f32 x1, f32 z1, f32 x2, f32 z2, f32 x3, f32 z3);
f32 stanGetSignedPointLineDistance(f32 x1, f32 z1, f32 x2, f32 z2, f32 x3, f32 z3);
void stanGetMoveBondCollisionTiles(StandTile **tile1, StandTile **tile2, coord3d *coords);
struct StandTile *sub_GAME_7F0AFB78(f32 *arg_x, f32 *arg_y, f32 *arg_z, f32 arg3);
bool doSegmentsIntersect(f32 start1X, f32 start1Z, f32 end1X, f32 end1Z, f32 start2X, f32 start2Z, f32 end2X, f32 end2Z);
struct StandTilePoint *stanMatchTileName(char *id);
s32 isPointInsideTriStandTileUnscaled_Maybe(struct StandTile *tile, f32 p_x, f32 p_z);
s32 sub_GAME_7F0B21B0(StandTile **tileStack, f32 target_x, f32 target_z, f32 radius, s32 *rooms, s32 *count_rtn, s32 bufMax);
StandTile *stanFindTileBelowPos(coord3d *pos, u8 *rooms, f32 *yRtn);

void stanDetermineEOF(struct StanPrefixRecord *file /* canonically r */, s32 origBase, u8 *newBase);
void stanLoadFile(struct StanPrefixRecord *file);
bool stanTileHasZeroArea(StandTile *tile);

#endif
