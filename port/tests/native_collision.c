#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "gbi_extension.h"
#include "game/bg.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

extern s_room_info g_BgRoomInfo[];
void bgBuildRoomVtxBounds(s32 room);
extern Vtx *lightFindVertexBaseForTri(Gfx *gfx, s32 room);

/* Room-bound cache allocations are owned by the fixture. */
void *memaAlloc(u32 size) { return malloc(size); }

static void testRoomCollision(s32 firstSlot, s32 tri4, s32 opening)
{
    Vertex vertices[6] = {0};
    Gfx commands[4] = {0};
    bg_room_data rooms[2] = {0};
    coord3d from = {{0, 0, 0}}, to = {{0, 0, 100}};
    HitThing hit = {0};
    s32 found;

    ptr_bgdata_room_fileposition_list = rooms;
    vertices[0].coord.x = -10; vertices[0].coord.y = -10; vertices[0].coord.z = 10;
    vertices[1].coord.x = 10; vertices[1].coord.y = -10; vertices[1].coord.z = 10;
    vertices[2].coord.y = 10; vertices[2].coord.z = 10;
    for (s32 i = 0; i < 3; i++) {
        vertices[3 + i] = vertices[i];
        vertices[3 + i].coord.z = 50;
    }
    if (opening) {
        /* The ray crosses this batch's bounds but misses its actual triangle. */
        vertices[3].coord.y = 10;
        vertices[4].coord.y = -4;
        vertices[5].coord.x = 10;
    }

    gDPSetTile(&commands[0], G_IM_FMT_IA, G_IM_SIZ_8b, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    gSPVertex(&commands[1], (void *)(uintptr_t)0x04000030, 3, firstSlot);
    if (tri4) {
        gSP4Triangles(&commands[2], firstSlot, firstSlot + 1, firstSlot + 2,
            firstSlot, firstSlot + 1, firstSlot + 2,
            firstSlot, firstSlot + 1, firstSlot + 2,
            firstSlot, firstSlot + 1, firstSlot + 2);
    } else {
        gSP1Triangle(&commands[2], firstSlot, firstSlot + 1, firstSlot + 2, 0);
    }
    gSPEndDisplayList(&commands[3]);
    g_BgRoomInfo[1].vertices = vertices;
    g_BgRoomInfo[1].ptr_expanded_mapping_info = commands;
    bgBuildRoomVtxBounds(1);
    CHECK(g_BgRoomInfo[1].vtx_batch_bounds != NULL);
    CHECK(g_BgRoomInfo[1].vtx_batch_bounds[0].zmin == 50);
    found = bgTestBulletHitBackground(&from, &to, 1, &hit);
    CHECK(found == !opening);
    if (found) {
        CHECK(hit.hitpos.z == 50);
        CHECK(hit.vtx0 == &vertices[3]);
        CHECK(hit.vtx1 == &vertices[4]);
        CHECK(hit.vtx2 == &vertices[5]);
        CHECK(hit.tileformat == G_IM_FMT_IA);
        CHECK(hit.tilesize == G_IM_SIZ_8b);
    }
    free(g_BgRoomInfo[1].vtx_batch_bounds);
    memset(&g_BgRoomInfo[1], 0, sizeof(g_BgRoomInfo[1]));
}

static void testLightVertexAddress(void)
{
    Vertex vertices[6] = {0};
    Gfx commands[2] = {0};
    gSPVertex(&commands[0], (void *)(uintptr_t)0x0e000030, 3, 0);
    gSP1Triangle(&commands[1], 0, 1, 2, 0);
    g_BgRoomInfo[1].vertices = vertices;
    CHECK(lightFindVertexBaseForTri(&commands[1], 1) == (Vtx *)&vertices[3]);
    commands[0].words.w1 = (uintptr_t)vertices;
    CHECK(lightFindVertexBaseForTri(&commands[1], 1) == (Vtx *)vertices);
    /* A native address whose low bits resemble a segment ID is still native.
     * The function only returns this token; it must never dereference it. */
    commands[0].words.w1 = UINT64_C(0x10e000030);
    CHECK((uintptr_t)lightFindVertexBaseForTri(&commands[1], 1) == UINT64_C(0x10e000030));
    memset(&g_BgRoomInfo[1], 0, sizeof(g_BgRoomInfo[1]));
}

int main(void)
{
    testLightVertexAddress();
    for (s32 slot = 0; slot <= 4; slot += 4)
        for (s32 tri4 = 0; tri4 < 2; tri4++)
            for (s32 opening = 0; opening < 2; opening++)
                testRoomCollision(slot, tri4, opening);
    puts("native collision tests passed");
    return 0;
}
