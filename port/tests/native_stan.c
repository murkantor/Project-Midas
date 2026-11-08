#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "game/stan.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

extern StandTile *standTileStart;
void ge_dbg_vec(const char *tag, const f32 *values, s32 count)
{
    (void)tag; (void)values; (void)count;
}

int main(void)
{
    /* Packed tile links are in units of eight bytes, even on a 64-bit host. */
    union { u64 align; u8 bytes[256]; } image = {0};
    StandTile *floor = (StandTile *)image.bytes;
    StandTile *special = (StandTile *)(image.bytes + 128);
    StandTile *start = floor;
    struct {
        struct StandTileLocusCallbackRecord record;
        u64 guard;
    } result;

    standTileStart = floor;
    floor->tail.hdrTail.pointCount = 3;
    floor->points[0].x = -100; floor->points[0].z = -100;
    floor->points[1].x = 100; floor->points[1].z = -100;
    floor->points[2].z = 100;
    floor->points[0].link = 16;
    result.guard = 0x0123456789abcdefULL;

    special->mid.headerMid.special = 3; /* Ladder, beside the floor edge. */
    memset(&result.record, 0xa5, sizeof(result.record));
    stanTileDistanceRelated(&start, 0, -90, 20, &result.record);
    CHECK(stanGetLocusField0(&result.record) == NULL);
    CHECK(stanGetLocusCount(&result.record) == 1);
    CHECK(result.guard == 0x0123456789abcdefULL);

    special->mid.headerMid.special = 1; /* A low opening must still crouch. */
    stanTileDistanceRelated(&start, 0, -90, 20, &result.record);
    CHECK(stanGetLocusField0(&result.record) != NULL);
    CHECK(stanGetLocusCount(&result.record) == 0);

    special->mid.headerMid.special = 0;
    stanTileDistanceRelated(&start, 0, -90, 20, &result.record);
    CHECK(stanGetLocusField0(&result.record) == NULL);
    CHECK(stanGetLocusCount(&result.record) == 0);
    CHECK(result.record.bufMax == 0);
    CHECK(result.record.nearEdgeCount == 0);
    CHECK(result.guard == 0x0123456789abcdefULL);
    puts("native stan tests passed");
    return 0;
}
