/**
 * Portal Gun marks: two persistent coloured ovals in room space, outside the
 * game's bullet-impact ring buffer so shooting never evicts them. Each is an
 * untextured fan: a translucent centre and 12 rim points at full colour, on
 * the surface basis explosionCreateBulletImpact builds for wall hits. Drawn
 * from RENDER_WORLD, right after the bullet impacts.
 */
#include <ultra64.h>
#include <bondtypes.h>
#include "decals.h"

extern float sqrtf(float);
extern float sinf(float);
extern float cosf(float);

extern Vtx g_BulletImpactDefaultVertex;
extern struct coord3d *getRoomPositionByIndex(s32 roomID);
extern f32 get_room_data_float1(void);
extern s32 explosionRoundFloat(f32 arg0);
extern u8 getROOMID_isRendered(s32 roomID);
extern Gfx *applyRoomMatrixToDisplayList(Gfx *gdl, int roomID);

#define DECALS 2
#define RIM    12

/* static: on Windows the mod DLL sits above 0x10000000 (port_mods.c checks),
 * so a display list may point straight at these */
static struct
{
    s16 room;
    Vtx v[RIM + 1];
} s_decal[DECALS] = { { -1 }, { -1 } };

void funDecalClear(s32 i)
{
    if ((i >= 0) && (i < DECALS))
    {
        s_decal[i].room = -1;
    }
}

void funDecalSet(s32 i, coord3d *pos, coord3d *normal, s32 room, f32 halfw, f32 halfh, u8 r, u8 g, u8 b)
{
    struct coord3d *roompos;
    f32 rx, rz;      /* right, horizontal */
    f32 ux, uy, uz;  /* up */
    f32 px, py, pz;
    s32 k;

    if ((i < 0) || (i >= DECALS))
    {
        return;
    }

    if ((normal->f[0] == 0.0f) && (normal->f[2] == 0.0f))
    {
        rx = 1.0f; rz = 0.0f;
        ux = 0.0f; uy = 0.0f; uz = 1.0f;
    }
    else
    {
        f32 len = sqrtf((normal->f[0] * normal->f[0]) + (normal->f[1] * normal->f[1]) + (normal->f[2] * normal->f[2]));
        f32 nx = normal->f[0] / len;
        f32 ny = normal->f[1] / len;
        f32 nz = normal->f[2] / len;
        f32 h = sqrtf((nx * nx) + (nz * nz));

        rx = nz / h;
        rz = -nx / h;
        ux = -ny * (nx / h);
        uy = h;
        uz = -ny * (nz / h);
    }

    roompos = getRoomPositionByIndex(room);
    px = (pos->f[0] * get_room_data_float1()) - roompos->f[0];
    py = (pos->f[1] * get_room_data_float1()) - roompos->f[1];
    pz = (pos->f[2] * get_room_data_float1()) - roompos->f[2];
    halfw *= get_room_data_float1();
    halfh *= get_room_data_float1();

    for (k = 0; k <= RIM; k++)
    {
        Vtx *v = &s_decal[i].v[k];
        f32 cw = 0.0f;
        f32 ch = 0.0f;

        *v = g_BulletImpactDefaultVertex;
        if (k > 0)
        {
            const f32 a = (f32) (k - 1) * (6.2831855f / RIM);

            cw = cosf(a) * halfw;
            ch = sinf(a) * halfh;
        }
        v->v.ob[0] = explosionRoundFloat(px + (cw * rx) + (ch * ux));
        v->v.ob[1] = explosionRoundFloat(py + (ch * uy));
        v->v.ob[2] = explosionRoundFloat(pz + (cw * rz) + (ch * uz));
        v->v.tc[0] = 0;
        v->v.tc[1] = 0;
        /* the centre is darker and see-through: a glowing ring */
        v->v.cn[0] = (k == 0) ? (r / 3) : r;
        v->v.cn[1] = (k == 0) ? (g / 3) : g;
        v->v.cn[2] = (k == 0) ? (b / 3) : b;
        v->v.cn[3] = (k == 0) ? 0x70 : 0xFF;
    }
    s_decal[i].room = room;
}

Gfx *funDecalsRender(Gfx *gdl)
{
    s32 i;
    s32 k;
    s32 set = 0;

    for (i = 0; i < DECALS; i++)
    {
        if ((s_decal[i].room < 0) || !getROOMID_isRendered(s_decal[i].room))
        {
            continue;
        }
        if (!set)
        {
            set = 1;
            gDPPipeSync(gdl++);
            gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_LIGHTING | G_TEXTURE_GEN);
            gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
            gDPSetCycleType(gdl++, G_CYC_1CYCLE);
            gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
            gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
        }
        gdl = applyRoomMatrixToDisplayList(gdl, s_decal[i].room);
        gSPVertex(gdl++, osVirtualToPhysical(s_decal[i].v), RIM + 1, 0);
        for (k = 1; k <= RIM; k++)
        {
            gSP1Triangle(gdl++, 0, k, (k == RIM) ? 1 : (k + 1), 0);
        }
    }
    if (set)
    {
        gDPPipeSync(gdl++);
    }
    return gdl;
}
