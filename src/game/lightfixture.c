/**
 * GE's light system is quite rudimentary and does just two things when a light is destroyed:
 * 
 * 1) it darkens the light fixture's own vertices in place. Each vertex
 *      color component is shifted right by 2 so the fixture's polygons draw
 *      at a quarter brightness and read as burnt out
 * 
 * 2) it spawns glass shards from the light fixture
 * 
 * Two tables are important for this sytem.
 * 
 * light_fixture_table[100] is a directory of the fixtures currently loaded. An
 * entry is (room_index, DL start, DL end): the run of a room's display list 
 * drawn with a single light texture. Note that one entry is NOT one light fixture,
 * but rather a batch of tris in a room using the same texture. Thus one entry could
 * represent multiple light fixtures so long as those fixtures use the same texture.
 * Function lightIsCoordNearDarkenedVertex() determines what counts as one light fixture.
 * 
 * darkened_light_table[512] is the persistent record of what has been shot
 * out, as (room_index, vertex index). It has to be separate and persistent
 * because vertex data is reread from the file whenever a room is streamed
 * back in, which wipes the in-place darkening. It is a circular buffer, so on
 * a level with a great many broken lights the oldest ones silently come back.
 * 
 * Lifecycle:
 * Level start   On level start lightFixtureInitTables() zeroes both tables.
 * 
 * Room load     bgLoadRoomPrimaryGdl() calls clear_light_fixturetable_in_room(),
 *               which drops that room's stale fixture entries and latches the
 *               room number into current_light_fixture_room. Then
 *               texLoadFromGdl() walks the room's display list. Every time it
 *               emits a texture that check_if_imageID_is_light() recognises
 *               it opens an entry via lightFixtureEntryBegin() 
 *               and closes it with lightFixtureEntryEnd(). So the
 *               fixture directory is a by-product of texture processing.
 * 
 * Room unload   Nothing! delete_room_data() frees ptr_expanded_mapping_info,
 *               the display list these entries point into, but never
 *               touches light_fixture_table. Thus a level could in theory have
 *               enough lights to fill the light_fixture_table and after that
 *               point you wouldn't be able to shoot out any more lights. But I
 *               don't believe any levels actually have enough rooms and lights 
 *               to fill the table.
 *
 * Vertex load   after bgLoadRoomVtxData(), bg.c calls redarken_lights_in_room()
 *               to re-apply the >>2 to every vertex the darkened table lists for
 *               that room, restoring lights the player broke earlier.
 *
 * Bullet hit    chrprop.c checks check_if_imageID_is_light() on the hit
 *               texture and, if it matches, calls lightFixtureBreak() with the
 *               display list command of the triangle that was hit.
 * 
 * Vertex pointers inside a room display list are segmented addresses - segment
 * SPSEGMENT_BG_VTX (14) in the top byte, a 24-bit offset below - because the
 * display list was built offline. The RSP resolves them from the gSPSegment
 * that bg.c issues at draw time, but code here reads the vertices with the CPU
 * and so has to resolve them by hand: room vertex base + offset. That is the
 * "(ptr & 0xFF000000) == 0x0E000000" test in lightFindVertexBaseForTri.
 */

#include <ultra64.h>
#include "lightfixture.h"
#include "bg.h"
#include "glass.h"
#include <bondconstants.h>
#include <assets/image_externs.h>
#include <PR/gbi.h>
#include <gbi_extension.h>

#define LIGHTFIXTURE_TABLE_MAX 200
#define DARKENED_LIGHT_TABLE_MAX 1024

s_lightfixture light_fixture_table[LIGHTFIXTURE_TABLE_MAX];
s16 current_light_fixture_slot;
s16 current_light_fixture_room;

/**
 * The darkened light table can hold a maximum of 1024 vertices. It is a circular table so if that limit is exceeded,
 * the oldest records are overwritten first.
 */
struct s_darkened_light darkened_light_table[DARKENED_LIGHT_TABLE_MAX];

s32 dword_CODE_bss_80083318; // unused
s32 cur_entry_darkened_light_table = 0;

s32 D_80046034[] = {0, 0, 0, 0, 0, 0, 0}; // unused



#ifndef TARGET_N64
/* ==================== GE_LIGHTDBG (set GE_LIGHTDBG=1) ====================
 * Diagnostic for shot lights going fully transparent: logs each fixture entry
 * as it opens (room, DL run, preceding render state, first tri's colours and
 * position) and every vertex darkening, to stderr with prefix "lightdbg:". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
s32 g_LightDbgTexnum; /* set by tex.c right before lightFixtureEntryBegin */
s32 g_LightDbgRoomLoad; /* set by bg.c around its texLoadFromGdl calls, which
                         * also convert model DLs that get registered as fixture
                         * entries; the dump must not resolve those vertex
                         * addresses against a room */
#include "bgroomtrans.h"
void extract_vertex_indices_from_triangle(Gfx* gfx, u32 tri_type, s32* idx1, s32* idx2, s32* idx3);
Vtx *lightFindVertexBaseForTri(Gfx *gfx, s32 room_index);

static int lightDbgOn(void)
{
    static int on = -1;
    if (on < 0) on = getenv("GE_LIGHTDBG") != NULL;
    return on;
}

static void lightDbgDumpEntry(s32 slot)
{
    s_lightfixture *e = &light_fixture_table[slot];
    s_room_info *r;
    Gfx *lo;
    Gfx *g;
    s32 ntri = 0;
    s32 ntri4 = 0;
    s32 firstType = -1;
    Gfx *firstTri = NULL;

    if (e->room_index <= 0 || e->ptr_start_pertinent_DL == NULL) return;
    if (!g_LightDbgRoomLoad) {
        fprintf(stderr, "lightdbg: entry slot=%d room=%d tex=%d dl=%p..%p ncmd=%d (MODEL dl - vanilla quirk, not dumped)\n",
                slot, e->room_index, g_LightDbgTexnum, (void *)e->ptr_start_pertinent_DL,
                (void *)e->ptr_end_pertinent_DL, (int)(e->ptr_end_pertinent_DL - e->ptr_start_pertinent_DL));
        return;
    }
    r = &g_BgRoomInfo[e->room_index];
    fprintf(stderr, "lightdbg: entry slot=%d room=%d tex=%d dl=%p..%p ncmd=%d\n", slot, e->room_index,
            g_LightDbgTexnum, (void *)e->ptr_start_pertinent_DL, (void *)e->ptr_end_pertinent_DL,
            (int)(e->ptr_end_pertinent_DL - e->ptr_start_pertinent_DL));

    /* clamp the backwards state walk to the DL the run lives in */
    lo = e->ptr_start_pertinent_DL - 64;
    if (r->ptr_expanded_mapping_info != NULL &&
        (u8 *)e->ptr_start_pertinent_DL >= (u8 *)r->ptr_expanded_mapping_info &&
        (u8 *)e->ptr_start_pertinent_DL < (u8 *)r->ptr_expanded_mapping_info + r->usize_primary_DL_binary) {
        if (lo < (Gfx *)r->ptr_expanded_mapping_info) lo = (Gfx *)r->ptr_expanded_mapping_info;
        fprintf(stderr, "lightdbg:   in PRIMARY dl\n");
    } else if (r->ptr_secondary_expanded_mapping_info != NULL &&
        (u8 *)e->ptr_start_pertinent_DL >= (u8 *)r->ptr_secondary_expanded_mapping_info &&
        (u8 *)e->ptr_start_pertinent_DL < (u8 *)r->ptr_secondary_expanded_mapping_info + r->usize_secondary_DL_binary) {
        if (lo < (Gfx *)r->ptr_secondary_expanded_mapping_info) lo = (Gfx *)r->ptr_secondary_expanded_mapping_info;
        fprintf(stderr, "lightdbg:   in SECONDARY dl\n");
    } else {
        /* normal at EntryEnd time: the room's DL pointer is registered only
         * after texLoadFromGdl returns; the expanded DL follows the vertex
         * data in the same mema block, so clamp to the block start instead */
        if (r->vertices != NULL && lo < (Gfx *)r->vertices) lo = (Gfx *)r->vertices;
    }

    for (g = lo; g < e->ptr_end_pertinent_DL; g++) {
        u8 op = (u8)(g->words.w0 >> 24);
        if (op == G_SETOTHERMODE_L || op == G_SETOTHERMODE_H || op == G_SETCOMBINE || op == G_RDPSETOTHERMODE ||
            op == G_SETGEOMETRYMODE || op == G_CLEARGEOMETRYMODE || op == G_SETENVCOLOR || op == G_SETPRIMCOLOR) {
            fprintf(stderr, "lightdbg:   %+4d: op=%02x %08x %08x\n", (int)(g - e->ptr_start_pertinent_DL), op,
                    g->words.w0, g->words.w1);
        }
        if (g >= e->ptr_start_pertinent_DL) {
            if ((s8)op == G_TRI1) { ntri++; if (firstTri == NULL) { firstTri = g; firstType = 0; } }
            if ((s8)op == G_TRI4) { ntri4++; if (firstTri == NULL) { firstTri = g; firstType = 1; } }
        }
    }
    fprintf(stderr, "lightdbg:   tris: tri1=%d tri4=%d\n", ntri, ntri4);
    if (firstTri != NULL) {
        s32 i1, i2, i3;
        Vtx *vb;
        coord3d org;
        f32 sc = get_room_data_float2();
        extract_vertex_indices_from_triangle(firstTri, firstType, &i1, &i2, &i3);
        vb = lightFindVertexBaseForTri(firstTri, e->room_index);
        getRoomPositionScaledByIndex(e->room_index, &org);
        fprintf(stderr, "lightdbg:   first tri idx=%d,%d,%d cn=[%02x %02x %02x %02x] [%02x %02x %02x %02x] [%02x %02x %02x %02x]\n",
                i1, i2, i3,
                vb[i1].v.cn[0], vb[i1].v.cn[1], vb[i1].v.cn[2], vb[i1].v.cn[3],
                vb[i2].v.cn[0], vb[i2].v.cn[1], vb[i2].v.cn[2], vb[i2].v.cn[3],
                vb[i3].v.cn[0], vb[i3].v.cn[1], vb[i3].v.cn[2], vb[i3].v.cn[3]);
        fprintf(stderr, "lightdbg:   first tri world v1=(%.0f,%.0f,%.0f) v2=(%.0f,%.0f,%.0f) v3=(%.0f,%.0f,%.0f)\n",
                vb[i1].v.ob[0] * sc + org.x, vb[i1].v.ob[1] * sc + org.y, vb[i1].v.ob[2] * sc + org.z,
                vb[i2].v.ob[0] * sc + org.x, vb[i2].v.ob[1] * sc + org.y, vb[i2].v.ob[2] * sc + org.z,
                vb[i3].v.ob[0] * sc + org.x, vb[i3].v.ob[1] * sc + org.y, vb[i3].v.ob[2] * sc + org.z);
    }
}
#endif

void lightFixtureInitTables(void)
{
    s32 i;

    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        light_fixture_table[i].room_index = 0;
    }

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        darkened_light_table[i].room_index = 0;
    }

    cur_entry_darkened_light_table = 0;
}


s32 lightFixtureFindFreeSlot(void)
{
    s32 i;

    for (i = 0; i != LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (light_fixture_table[i].room_index == 0)
        {
            return i;
        }
    }

    return LIGHTFIXTURE_TABLE_MAX;
}


void lightFixtureEntryBegin(Gfx *DL)
{
    current_light_fixture_slot = lightFixtureFindFreeSlot();

    if (current_light_fixture_slot != LIGHTFIXTURE_TABLE_MAX)
    {
        light_fixture_table[current_light_fixture_slot].room_index = current_light_fixture_room;
        light_fixture_table[current_light_fixture_slot].ptr_start_pertinent_DL = DL;
    }
}


void lightFixtureEntryEnd(Gfx *DL)
{
    if (current_light_fixture_slot != LIGHTFIXTURE_TABLE_MAX)
    {
        light_fixture_table[current_light_fixture_slot].ptr_end_pertinent_DL = DL;
#ifndef TARGET_N64
        if (lightDbgOn()) lightDbgDumpEntry(current_light_fixture_slot);
#endif
    }
}


bool check_if_imageID_is_light(s32 imageID)
{
    if ((imageID == IMAGE_WALL_LAMP)     ||
        (imageID == IMAGE_203_LIGHT)     ||
        (imageID == IMAGE_205_LIGHT)     ||
        (imageID == IMAGE_252_LIGHT)     ||
        (imageID == IMAGE_253)           || // typically very small square lights
        (imageID == IMAGE_PANEL_LAMP)    ||
        (imageID == IMAGE_255_LIGHT)     ||
        (imageID == IMAGE_256_LIGHT)     ||
        (imageID == IMAGE_HANGING_LAMP)  ||
        (imageID == IMAGE_NEON_LAMP)     ||
        (imageID == IMAGE_LINEAR_LAMP))
    {
        // Will darken when shot
        return 1;
    } 
    else
    {
        return 0;
    }
}


/**
 * Returns the vertex array that a triangle command's indices refer to.
 *
 * Scans backwards to the gSPVertex that last loaded vertices, then resolves its
 * address. That address is normally a segment-14 (SPSEGMENT_BG_VTX) reference,
 * because room display lists are built offline; the RSP resolves those itself
 * at draw time, but this runs on the CPU and so adds the room's vertex base by
 * hand.
 */
Vtx *lightFindVertexBaseForTri(Gfx *gfx, s32 room_index)
{
    Vtx * ret;

    /* dma.cmd is the signed 8-bit command byte in w0 bits 31:24 and dma.addr is
     * all of w1; reading via words makes this endian-independent. The (s8) cast
     * reproduces the signed bitfield read exactly. */
    while ((s8)(gfx->words.w0 >> 24) != G_VTX)
    {
        gfx--;
    }

    ret = (Vtx *) gfx->words.w1;

    if (((uintptr_t) ret >> 24) == SPSEGMENT_BG_VTX)
    {
        ret = (Vtx *) ((u8 *)g_BgRoomInfo[room_index].vertices + ((uintptr_t) ret & 0xFFFFFF));
    }

    return ret;
}


void extract_vertex_indices_from_triangle(Gfx* gfx, u32 tri_type, s32* idx1, s32* idx2, s32* idx3)
{
    /* All command decoding is done on words.w0/w1 so it is endian-independent.
     *
     * case 0: F3DEX G_TRI1. w1 = flag<<24 | v0<<16 | v1<<8 | v2, with the
     * indices pre-multiplied by 10.
     *
     * cases 1-4: GE's G_TRI4 (see gSP4Triangles in gbi_extension.h):
     *   w0 = cmd<<24 | z4<<12 | z3<<8 | z2<<4 | z1
     *   w1 = y4<<28 | x4<<24 | y3<<20 | x3<<16 | y2<<12 | x2<<8 | y1<<4 | x1
     */
    switch (tri_type)
    {
        case 0:
            *idx1 = (s32) ((gfx->words.w1 >> 16) & 0xFF) / 10;
            *idx2 = (s32) ((gfx->words.w1 >> 8) & 0xFF) / 10;
            *idx3 = (s32) (gfx->words.w1 & 0xFF) / 10;
            break;
        case 1:
            *idx1 = gfx->words.w1 & 0xF;
            *idx2 = (gfx->words.w1 >> 4) & 0xF;
            *idx3 = gfx->words.w0 & 0xF;
            break;
        case 2:
            *idx1 = (gfx->words.w1 >> 8) & 0xF;
            *idx2 = (gfx->words.w1 >> 12) & 0xF;
            *idx3 = (gfx->words.w0 >> 4) & 0xF;
            break;
        case 3:
            *idx1 = (gfx->words.w1 >> 16) & 0xF;
            *idx2 = (gfx->words.w1 >> 20) & 0xF;
            *idx3 = (gfx->words.w0 >> 8) & 0xF;
            break;
        case 4:
            *idx1 = (gfx->words.w1 >> 24) & 0xF;
            *idx2 = (gfx->words.w1 >> 28) & 0xF;
            *idx3 = (gfx->words.w0 >> 12) & 0xF;
            break;
    }
}


void extract_vertex_coords_from_triangle(Gfx * gfx, u32 tri_type, s32 room_index, coord16 * out1, coord16 * out2, coord16 * out3)
{
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = lightFindVertexBaseForTri(gfx, room_index);

    out1->AsArray[0] = (s16) vertices[idx1].v.ob[0];
    out1->AsArray[1] = (s16) vertices[idx1].v.ob[1];
    out1->AsArray[2] = (s16) vertices[idx1].v.ob[2];

    out2->AsArray[0] = (s16) vertices[idx2].v.ob[0];
    out2->AsArray[1] = (s16) vertices[idx2].v.ob[1];
    out2->AsArray[2] = (s16) vertices[idx2].v.ob[2];

    out3->AsArray[0] = (s16) vertices[idx3].v.ob[0];
    out3->AsArray[1] = (s16) vertices[idx3].v.ob[1];
    out3->AsArray[2] = (s16) vertices[idx3].v.ob[2];
}


void redarken_lights_in_room(s32 room_index)
{
    Vtx * vertex;
    s32 i;
    struct s_darkened_light* unk;

    vertex = g_BgRoomInfo[room_index].vertices;

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        unk = &darkened_light_table[i];

        if (room_index != unk->room_index) { continue; }

#ifndef TARGET_N64
        if (lightDbgOn()) {
            fprintf(stderr, "lightdbg: redarken room=%d vtx=%d cn=[%02x %02x %02x %02x]\n", room_index, unk->vtx_index,
                    vertex[unk->vtx_index].v.cn[0], vertex[unk->vtx_index].v.cn[1],
                    vertex[unk->vtx_index].v.cn[2], vertex[unk->vtx_index].v.cn[3]);
        }
#endif
        vertex[unk->vtx_index].v.cn[0] >>= 2;
        vertex[unk->vtx_index].v.cn[1] >>= 2;
        vertex[unk->vtx_index].v.cn[2] >>= 2;
        vertex[unk->vtx_index].v.cn[3] >>= 2;
#ifndef TARGET_N64
        { /* the room cache holds a copy of these colours (roc_hooks.c) */
            extern void portRoomCacheRoomFreed(s32 room);
            portRoomCacheRoomFreed(room_index);
        }
#endif
    }
}


void darken_vertex_in_room(Vtx * vertex, s32 room_index)
{
    s32 vtx_index;

    // Check if this vertex was already darkened
    if (darkened_light_table_contains_vertex(vertex, room_index) != 0) { return; }

    // weird memory stuff going on here
    vtx_index = ((uintptr_t)vertex - (uintptr_t)g_BgRoomInfo[room_index].vertices) >> 4;

    darkened_light_table[cur_entry_darkened_light_table].room_index = (u16) room_index;
    darkened_light_table[cur_entry_darkened_light_table].vtx_index = vtx_index;

#ifndef TARGET_N64
    if (lightDbgOn()) {
        fprintf(stderr, "lightdbg: darken room=%d vtx=%d cn=[%02x %02x %02x %02x] -> [%02x %02x %02x %02x] (entry %d)\n",
                room_index, vtx_index, vertex->v.cn[0], vertex->v.cn[1], vertex->v.cn[2], vertex->v.cn[3],
                vertex->v.cn[0] >> 2, vertex->v.cn[1] >> 2, vertex->v.cn[2] >> 2, vertex->v.cn[3] >> 2,
                cur_entry_darkened_light_table);
    }
#endif
    vertex->v.cn[0] >>= 2;
    vertex->v.cn[1] >>= 2;
    vertex->v.cn[2] >>= 2;
    vertex->v.cn[3] >>= 2;
#ifndef TARGET_N64
    { /* the room cache holds a copy of these colours (roc_hooks.c) */
        extern void portRoomCacheRoomFreed(s32 room);
        portRoomCacheRoomFreed(room_index);
    }
#endif

    cur_entry_darkened_light_table++;

    if (cur_entry_darkened_light_table >= DARKENED_LIGHT_TABLE_MAX)
    {
        cur_entry_darkened_light_table = 0;
    }
}


s32 darkened_light_table_contains_vertex(Vtx * vertex, s32 room_index)
{
    u32 vtx_index;
    s32 i;

    // weird memory stuff going on here
    vtx_index = ((uintptr_t)vertex - (uintptr_t)g_BgRoomInfo[room_index].vertices) >> 4;

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        if ((room_index == darkened_light_table[i].room_index) && ((s32)vtx_index == darkened_light_table[i].vtx_index))
        {
            return TRUE;
        }
    }

    return FALSE;
}


void darken_triangle_in_room(Gfx *gfx, u32 tri_type, s32 room_index)
{
    Vtx * vertex;
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = lightFindVertexBaseForTri(gfx, room_index);

    darken_vertex_in_room(&vertices[idx1], room_index);
    darken_vertex_in_room(&vertices[idx2], room_index);

    vertex = &vertices[idx3];
    darken_vertex_in_room(vertex, room_index);
}


s32 darkened_light_table_contains_triangle(Gfx * gfx, u32 tri_type, s32 room_index)
{
    s32 out3;
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;
    s32 out2;
    s32 out1;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = lightFindVertexBaseForTri(gfx, room_index);
    out1 = darkened_light_table_contains_vertex(&vertices[idx2], room_index);
    out2 = darkened_light_table_contains_vertex(&vertices[idx1], room_index);
    out3 = darkened_light_table_contains_vertex(&vertices[idx3], room_index);
    return out3 + out2 + out1;
}


/**
 * Test whether a tri belongs to a light fixture region that should also be darkened.
 */
s32 lightIsCoordNearDarkenedVertex(coord16 * coord, s32 room_index)
{
    s32 dx;
    s32 dy;
    s32 dz;
    s32 i;
    Vtx * vertex;

    i = 0;
    do
    {
        if (room_index == darkened_light_table[i].room_index)
        {
            vertex = &g_BgRoomInfo[room_index].vertices[darkened_light_table[i].vtx_index];

            dx = vertex->v.ob[0] - coord->AsArray[0];
            dy = vertex->v.ob[1] - coord->AsArray[1];
            dz = vertex->v.ob[2] - coord->AsArray[2];

            if (dx < 0) { dx = -dx; }
            if (dy < 0) { dy = -dy; }
            if (dz < 0) { dz = -dz; }

            if ((dx + dy + dz) < (s32) (get_room_data_float1() * 100.0f))
            {
                return 1;
            }
        }
    } while (++i < DARKENED_LIGHT_TABLE_MAX);

    return 0;
}


/**
 * Darken the vertices belonging to a light fixture and spawn shards of glass.
 * 
 * When a bullet hits a triangle, lightFixtureBreak searches for a light_fixture_table entry where:
 * room_index == entry->room_index
 * gfx >= entry->ptr_start_pertinent_DL
 * gfx <  entry->ptr_end_pertinent_DL
 * 
 */
void lightFixtureBreak(Gfx * hit_gfx, u32 tri_type, s32 room_index)
{
    s16 diff_z_12;
 
    // Vertices of the hit triangle
    coord16 hit_vtx1;
    coord16 hit_vtx2;
    coord16 hit_vtx3;
 
    // Corners of whichever candidate tri is currently being tested for darkening
    coord16 cand_vtx1;
    coord16 cand_vtx2;
    coord16 cand_vtx3;
 
    s16 diff_x_23;
    s16 diff_x_12;
    Gfx *fixture_gfx;
    f32 edge_frac;
    s32 j;
    s8 darken_tri1;
    s8 darken_tri4;
    s16 diff_y_23;
    s16 diff_y_12;
    s16 diff_x_13;
    s16 diff_z_23;
    f32 shard_step_12;
    f32 shard_step_13;
    f32 shard_step_23;
    coord3d room_origin;
    coord3d shard_pos;
    s32 i;
    s16 diff_z_13;
    f32 edge_length;
    s16 diff_y_13;
 
    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        /** 
          *  Check if this shot landed in the same room as this light fixture table entry. If not, skip this iteration.
          *  This is what makes stale slots in the light fixture array safe. An unloaded room's entries keep dangling
          *  DL pointers, and they are rejected here before the pointer comparisons
          *  below could ever look at them. 
          */
        if (room_index != light_fixture_table[i].room_index) 
        { 
            continue; 
        }
 
        if (hit_gfx < light_fixture_table[i].ptr_start_pertinent_DL) 
        { 
            continue; 
        }
 
        if (hit_gfx >= light_fixture_table[i].ptr_end_pertinent_DL) 
        { 
            continue; 
        }
 
#ifndef TARGET_N64
        if (lightDbgOn()) {
            fprintf(stderr, "lightdbg: BREAK room=%d slot=%d hit_gfx=%p (+%d) tri_type=%u already=%d\n",
                    room_index, i, (void *)hit_gfx, (int)(hit_gfx - light_fixture_table[i].ptr_start_pertinent_DL),
                    tri_type, darkened_light_table_contains_triangle(hit_gfx, tri_type, light_fixture_table[i].room_index));
        }
#endif
        // If this tri is already darkened, do not darken it again.
        if (darkened_light_table_contains_triangle(hit_gfx, tri_type, light_fixture_table[i].room_index) != 0) 
        { 
            return; 
        }
 
        //Darken the exact triangle that was shot. The check for other tris that are part of this light fixture comes later.
        darken_triangle_in_room(hit_gfx, tri_type, light_fixture_table[i].room_index);
 
        /**
         * Measure the hit tri's three edges and turn each into a step size for the shard loops below.
         *
         * edge_length is in room units; get_room_data_float2() is 1 / level scale, so multiplying gives 
         * the edge length in world units, and 10.0f / that is the fraction of the edge 
         * spanning 10 world units. The loops add it to edge_frac from 0 to 1, so shards land every 10
         * world units and a longer edge sheds proportionally more. The per-axis diffs are kept because 
         * those loops reuse them as the direction to interpolate along.
         */
        extract_vertex_coords_from_triangle(hit_gfx, tri_type, light_fixture_table[i].room_index, &hit_vtx1, &hit_vtx2, &hit_vtx3);
 
		diff_x_12 = hit_vtx1.AsArray[0] - hit_vtx2.AsArray[0];
		diff_x_13 = hit_vtx1.AsArray[0] - hit_vtx3.AsArray[0];
		diff_x_23 = hit_vtx2.AsArray[0] - hit_vtx3.AsArray[0];
 
		diff_y_12 = hit_vtx1.AsArray[1] - hit_vtx2.AsArray[1];
		diff_y_13 = hit_vtx1.AsArray[1] - hit_vtx3.AsArray[1];
		diff_y_23 = hit_vtx2.AsArray[1] - hit_vtx3.AsArray[1];
 
		diff_z_12 = hit_vtx1.AsArray[2] - hit_vtx2.AsArray[2];
		diff_z_13 = hit_vtx1.AsArray[2] - hit_vtx3.AsArray[2];
		diff_z_23 = hit_vtx2.AsArray[2] - hit_vtx3.AsArray[2];
 
        edge_length = sqrtf((diff_x_12 * diff_x_12) + (diff_y_12 * diff_y_12) + (diff_z_12 * diff_z_12));
        shard_step_12 = 10.0f / (get_room_data_float2() * edge_length);
 
        edge_length = sqrtf((diff_x_13 * diff_x_13) + (diff_y_13 * diff_y_13) + (diff_z_13 * diff_z_13));
        shard_step_13 = 10.0f / (get_room_data_float2() * edge_length);
 
        edge_length = sqrtf((diff_x_23 * diff_x_23) + (diff_y_23 * diff_y_23) + (diff_z_23 * diff_z_23));
        shard_step_23 = 10.0f / (get_room_data_float2() * edge_length);
 
        getRoomPositionScaledByIndex(light_fixture_table[i].room_index, &room_origin);
 
        /**
         * Spawn glass shards along the edges of the hit tri.
         * Shards are spawned at fixed length intervals i.e. a long edge spawns more shards than a short edge.
         * Positions are converted from room space to world space for the glassCreateShard() function.
         */
        for (edge_frac = 0.0f; edge_frac < 1.0f; edge_frac += shard_step_12)
        {
            shard_pos.x = ((hit_vtx2.AsArray[0] + (diff_x_12 * edge_frac)) * get_room_data_float2()) + room_origin.f[0];
            shard_pos.y = ((hit_vtx2.AsArray[1] + (diff_y_12 * edge_frac)) * get_room_data_float2()) + room_origin.f[1];
            shard_pos.z = ((hit_vtx2.AsArray[2] + (diff_z_12 * edge_frac)) * get_room_data_float2()) + room_origin.f[2];
            glassCreateShard(&shard_pos, 0.0f, 10.0f);
        }
 
        for (edge_frac = 0.0f; edge_frac < 1.0f; edge_frac += shard_step_13)
        {
            shard_pos.x = ((hit_vtx3.AsArray[0] + (diff_x_13 * edge_frac)) * get_room_data_float2()) + room_origin.f[0];
            shard_pos.y = ((hit_vtx3.AsArray[1] + (diff_y_13 * edge_frac)) * get_room_data_float2()) + room_origin.f[1];
            shard_pos.z = ((hit_vtx3.AsArray[2] + (diff_z_13 * edge_frac)) * get_room_data_float2()) + room_origin.f[2];
            glassCreateShard(&shard_pos, 0.0f, 10.0f);
        }
 
        for (edge_frac = 0.0f; edge_frac < 1.0f; edge_frac += shard_step_23)
        {
            shard_pos.x = ((hit_vtx3.AsArray[0] + (diff_x_23 * edge_frac)) * get_room_data_float2()) + room_origin.f[0];
            shard_pos.y = ((hit_vtx3.AsArray[1] + (diff_y_23 * edge_frac)) * get_room_data_float2()) + room_origin.f[1];
            shard_pos.z = ((hit_vtx3.AsArray[2] + (diff_z_23 * edge_frac)) * get_room_data_float2()) + room_origin.f[2];
            glassCreateShard(&shard_pos, 0.0f, 10.0f);
        }
 
        /**
         * Iterate over all tris in the fixture's display list range.
         * If any vertex of a tri is close to a previously darkened vertex,
         * darken the entire tri. This ensures the entire fixture is darkened, not just the hit tri.
         */
        for (fixture_gfx = light_fixture_table[i].ptr_start_pertinent_DL; fixture_gfx < light_fixture_table[i].ptr_end_pertinent_DL; fixture_gfx++)
        {
            /* Command byte is w0 bits 31:24; the (s8) cast reproduces the
             * signed dma.cmd bitfield read (G_TRI1 = -65, G_TRI4 = -79). */
            if ((s8)(fixture_gfx->words.w0 >> 24) == G_TRI1)
            {
                darken_tri1 = 0;
 
                extract_vertex_coords_from_triangle(fixture_gfx, 0, light_fixture_table[i].room_index, &cand_vtx1, &cand_vtx2, &cand_vtx3);
 
                if (lightIsCoordNearDarkenedVertex(&cand_vtx1, light_fixture_table[i].room_index) != 0)
                {
                    darken_tri1 = 1;
                }
                else if (lightIsCoordNearDarkenedVertex(&cand_vtx2, light_fixture_table[i].room_index) != 0)
                {
                    darken_tri1 = 1;
                }
                else if (lightIsCoordNearDarkenedVertex(&cand_vtx3, light_fixture_table[i].room_index) != 0)
                {
                    darken_tri1 = 1;
                }
 
                if (darken_tri1 != 0)
                {
                    darken_triangle_in_room(fixture_gfx, 0, light_fixture_table[i].room_index);
                }
            }
            else if ((s8)(fixture_gfx->words.w0 >> 24) == G_TRI4)
            {
                for (j = 0; j < 4; j++)
                {
                    darken_tri4 = 0;
 
                    extract_vertex_coords_from_triangle(fixture_gfx, j + 1, light_fixture_table[i].room_index, &cand_vtx1, &cand_vtx2, &cand_vtx3);
 
                    if (lightIsCoordNearDarkenedVertex(&cand_vtx1, light_fixture_table[i].room_index) != 0)
                    {
                        darken_tri4 = 1;
                    }
                    else if (lightIsCoordNearDarkenedVertex(&cand_vtx2, light_fixture_table[i].room_index) != 0)
                    {
                        darken_tri4 = 1;
                    }
                    else if (lightIsCoordNearDarkenedVertex(&cand_vtx3, light_fixture_table[i].room_index) != 0)
                    {
                        darken_tri4 = 1;
                    }
 
                    if (darken_tri4 != 0)
                    {
                        darken_triangle_in_room(fixture_gfx, j + 1, light_fixture_table[i].room_index);
                    }
                }
            }
        }
        return;
    }
}


/** 
 * When a room loads bgLoadRoomPrimaryGdl() calls this function.
 * The function walks through the entire light_fixture_table and frees the slots
 * that belong to the room being loaded. The level's room numbers start at 1 so setting
 * the room index to 0 is a way to indicate the slot is free. Then texLoadFromGdl() refills slots
 * for that room via first-fit, so a reloaded room may land in entirely different slots than before.
 */
void clear_light_fixturetable_in_room(s32 room_index)
{
    s32 i;


    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (room_index == light_fixture_table[i].room_index)
        {
            light_fixture_table[i].room_index = 0;
        }
    }

    current_light_fixture_room = room_index;
}

