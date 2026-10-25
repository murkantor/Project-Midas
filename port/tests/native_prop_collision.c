#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/chrai.h"
#include "game/propobj.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

PropRecord g_Props[2];
static s16 indices[] = {0, 1, -1};
s16 *ptr_list_object_lookup_indices = indices;
static struct rect4f polygon;
static unsigned boundsCalls;

void chraiGetCollisionBounds(PropRecord *prop, struct rect4f **out, s32 *edges, f32 *top, f32 *bottom)
{
    (void)prop;
    *out = &polygon; *edges = 4; *top = 100; *bottom = 0;
}
void chraiGetPropRoomIds(PropRecord *prop, s32 *rooms) { (void)prop; rooms[0] = -1; }
void roomGetProps(s32 *rooms) { (void)rooms; }
void chrpropGetCollisionBounds(PropRecord *prop, f32 *radius, f32 *top, f32 *bottom)
{
    (void)prop;
    boundsCalls++;
    *radius = 10; *top = 90; *bottom = 0;
}
f32 sub_GAME_7F03CFE8(PropRecord *prop) { (void)prop; return 0; }
s32 chrpropTestPointInPolygon(coord3d *point, struct rect4f *poly, s32 edges)
{
    (void)point; (void)poly; (void)edges;
    return 1; /* Candidate's centre lies inside the door's swept bounds. */
}

int main(void)
{
    /* Both address-bit values must give the same result. The old ObjectRecord
     * overlay mistook this ChrRecord.prop pointer for the character flags. */
    unsigned char *storage = calloc(1, 4096 + sizeof(PropRecord));
    CHECK(storage != NULL);
    uintptr_t aligned = ((uintptr_t)storage + 1023) & ~(uintptr_t)1023;
    for (unsigned bit = 0; bit < 2; bit++) {
        for (unsigned viewer = 0; viewer < 2; viewer++) {
            for (unsigned hidden = 0; hidden < 2; hidden++) {
                ChrRecord chr = {0};
                memset(g_Props, 0, sizeof(g_Props));
                g_Props[0].type = PROP_TYPE_DOOR;
                g_Props[1].type = viewer ? PROP_TYPE_VIEWER : PROP_TYPE_CHR;
                g_Props[1].chr = &chr;
                chr.prop = (PropRecord *)(aligned + bit * 1024);
                chr.chrflags = hidden ? CHRFLAG_HIDDEN : 0;
                boundsCalls = 0;
                CHECK(sub_GAME_7F0448A8(&g_Props[0]) == hidden);
                CHECK(boundsCalls == !hidden);
                CHECK(!!(chr.hidden & CHRHIDDEN_OFFSCREEN_PATROL) == (!viewer && !hidden));
            }
        }
    }
    free(storage);
    puts("native prop collision tests passed");
    return 0;
}
