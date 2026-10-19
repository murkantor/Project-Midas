#include <ultra64.h>
#include "game/othermodemicrocode.h"
#include "game/propobj.h"
#include "game/bondview.h"
#include "game/dyn.h"
#include <limits.h>
#include <music.h>
#include "glass.h"
#include "image_bank.h"
#include "lv.h"
#include "math_atan2f.h"
#include "objective_status.h"
#include "player.h"
#include "random.h"


#ifndef VERSION_EU
#define SHARD_HORIZ_VEL_SCALE 1.5f
#define SHARD_VERT_VEL_SCALE 3.0f
#define SHARD_ANGVEL_SCALE 0.1f
#else
#define SHARD_HORIZ_VEL_SCALE 1.8f
#define SHARD_VERT_VEL_SCALE 3.6f
#define SHARD_ANGVEL_SCALE 0.12f
#endif

#define BULLET_SPARKS_MAX 20

// bss
//CODE.bss:8007A160
s32 SHATTERED_WINDOW_PIECES_BUFFER_LEN;
//CODE.bss:8007A164
s_shattered_window_piece* ptr_shattered_window_pieces;
//CODE.bss:8007A168
u32 dword_CODE_bss_8007A168;
//CODE.bss:8007A16C
u32 dword_CODE_bss_8007A16C;



// data
//D:80040940
s32 g_NextShardNum = 0;
u32 D_80040944 = 0;
u32 D_80040948 = 0;
u32 D_8004094C = 0;
u32 D_80040950 = 0;
u32 D_80040954 = 0;
u32 D_80040958 = 0;
u32 D_8004095C = 0;







// rodata
void sub_GAME_7F0A1DA0(coord3d *pos, coord3d *xaxis, coord3d *yaxis, coord3d *zaxis, f32 xmin, f32 xmax, f32 ymin, f32 ymax, f32 arg8, f32 arg9)
{
    f32      len;
    f32      angle;
    f32      rand_range;
    s32      step;
    s32      xcount;
    coord3d  shardpos;
    s32      ycount;
    s32      xlimit;
    s32      x;
    f32      shard_size;
    s32      y;
    s32      new_var;
    coord3d  basepos;
    coord3d  xnorm;
    coord3d  ynorm;
    coord3d *new_var3;
    xnorm.x = xaxis->x;
    xnorm.y = xaxis->y;
    xnorm.z = xaxis->z;
    len     = xnorm.x * xnorm.x;
    len     = sqrtf((xnorm.z * xnorm.z) + (len + (xnorm.y * xnorm.y)));
    xnorm.x *= 1.0f / len;
    xnorm.y *= 1.0f / len;
    xnorm.z *= 1.0f / len;
    xmin *= len;
    xmax *= len;
    ynorm.x = yaxis->x;
    ynorm.y = yaxis->y;
    ynorm.z = yaxis->z;
    len     = ynorm.x * ynorm.x;
    len     = sqrtf((ynorm.z * ynorm.z) + (len + (ynorm.y * ynorm.y)));
    ynorm.x *= 1.0f / len;
    ynorm.y *= 1.0f / len;
    ynorm.z *= 1.0f / len;
    ymin *= len;
    ymax *= len;
    if (&ymax) {}
    if (&xnorm) {}
    angle    = atan2f(zaxis->x, zaxis->z);
    arg8     = xmax - xmin;
    arg9     = ymax - ymin;
    new_var3 = &ynorm;
    if (xmin) {}
    shard_size = sqrtf((0, (arg8 * arg9) / (SHATTERED_WINDOW_PIECES_BUFFER_LEN / 2)));
    step       = (s32)shard_size;
    new_var    = step;
    basepos.x  = (pos->x + ((xmin + ((f32)(new_var >> 1))) * xnorm.x)) + ((*new_var3).x * (ymin + ((f32)(new_var >> 1))));
    basepos.y  = (pos->y + ((xmin + ((f32)(new_var >> 1))) * xnorm.y)) + ((*new_var3).y * (ymin + ((f32)(new_var >> 1))));
    basepos.z  = (pos->z + ((xmin + ((f32)(new_var >> 1))) * xnorm.z)) + ((*new_var3).z * (ymin + ((f32)(new_var >> 1))));
    chrobjSndCreatePostEventDefault(sndPlaySfx(g_musicSfxBufferPtr, 0x47, 0), pos);
    y      = 0;
    xlimit = (xcount = (s32)(arg8 / ((f32)step)));
    ycount = (s32)(arg9 / ((f32)new_var));
    if (ycount > 0)
    {
        f32 rand_min;
        do
        {
            if (TRUE)
            {
                x = 0;
            }
            if (xcount > 0)
            {
                do
                {
                    shardpos.x = (basepos.x + ((x * ((f32)step)) * xnorm.x)) + ((*new_var3).x * (((f32)y) * ((f32)new_var)));
                    shardpos.y = (basepos.y + ((x * ((f32)step)) * xnorm.y)) + ((*new_var3).y * (y * ((f32)new_var)));
                    shardpos.z = (basepos.z + ((x * ((f32)step)) * xnorm.z)) + ((*new_var3).z * (((f32)y) * ((f32)new_var)));
                    glassCreateShard(&shardpos, angle, (((randomGetNext() * 2.3283064e-10f) * 0.7f) + 0.1f) * shard_size);
                    x++;
                } while (x != xlimit);
            }
            y++;
        } while (y < ycount);
    }
}

/**
 * Creates a triangular shard of glass.
 */
void glassCreateShard(coord3d* pos, f32 rotX, f32 shard_size)
{
    /**
     * Horizontal X velocity component with randomness. Range: [-1.0, +1.0]
     */
    f32 randSymmetricX = (2.0f * (randomGetNext() * (1.0f / (f32)UINT_MAX))) - 1.0f;

    /**
     * Vertical velocity component. Range: [ -0.12, +1.0]
     * Biased so that most shards get an upward push but some have a chance of getting a small downward push.
     */
    f32 randBiasedY = (randomGetNext() * (1.0f / (f32)UINT_MAX) * 1.12f) - .12f;
    
    /**
     * Horizontal Z velocity component with randomness. Range: [-1.0, +1.0]
     */
    f32 randSymmetricZ = (2.0f * (randomGetNext() * (1.0f / (f32)UINT_MAX))) - 1.0f;

    u8 alpha; // Shard opacity

    ptr_shattered_window_pieces[g_NextShardNum].active = 1;
    ptr_shattered_window_pieces[g_NextShardNum].pos.x = pos->x;
    ptr_shattered_window_pieces[g_NextShardNum].pos.y = pos->y;
    ptr_shattered_window_pieces[g_NextShardNum].pos.z = pos->z;

    ptr_shattered_window_pieces[g_NextShardNum].velocity.x= randSymmetricX * SHARD_HORIZ_VEL_SCALE;
    ptr_shattered_window_pieces[g_NextShardNum].velocity.y = randBiasedY * SHARD_VERT_VEL_SCALE;
    ptr_shattered_window_pieces[g_NextShardNum].velocity.z = randSymmetricZ * SHARD_HORIZ_VEL_SCALE;

    ptr_shattered_window_pieces[g_NextShardNum].v1x = ((randomGetNext() * (1.0f / (f32)UINT_MAX) * 0.5f) + 1.0f) * shard_size;
    ptr_shattered_window_pieces[g_NextShardNum].v1y = ((randomGetNext() * (1.0f / (f32)UINT_MAX) * 0.5f) + 1.0f) * shard_size;
    ptr_shattered_window_pieces[g_NextShardNum].v1z = 0;

    ptr_shattered_window_pieces[g_NextShardNum].v2x = ((randomGetNext() * (1.0f / (f32)UINT_MAX) * 0.5f) + 1.0f) * shard_size;
    ptr_shattered_window_pieces[g_NextShardNum].v2y = ((randomGetNext() * (1.0f / (f32)UINT_MAX) * 0.5f) + 1.0f) * -shard_size;
    ptr_shattered_window_pieces[g_NextShardNum].v2z = 0;

    ptr_shattered_window_pieces[g_NextShardNum].v3x = ((randomGetNext() * (1.0f / (f32)UINT_MAX) * 0.5f) + 1.0f) * -shard_size;
    ptr_shattered_window_pieces[g_NextShardNum].v3y = ((randomGetNext() * (1.0f / (f32)UINT_MAX) * 0.5f) + 1.0f) * -shard_size;
    ptr_shattered_window_pieces[g_NextShardNum].v3z = 0;
    
    ptr_shattered_window_pieces[g_NextShardNum].v1s = 0;
    ptr_shattered_window_pieces[g_NextShardNum].v1t = 0;
    ptr_shattered_window_pieces[g_NextShardNum].v2s = 0;
    ptr_shattered_window_pieces[g_NextShardNum].v2t = 0;
    ptr_shattered_window_pieces[g_NextShardNum].v3s = 0;
    ptr_shattered_window_pieces[g_NextShardNum].v3t = 0;

    /**
     * Create a colored gradient over the verts for environment mapping.
     */
    ptr_shattered_window_pieces[g_NextShardNum].v1r = 5;
    ptr_shattered_window_pieces[g_NextShardNum].v1g = 5;
    ptr_shattered_window_pieces[g_NextShardNum].v1b = 0x7E;
    ptr_shattered_window_pieces[g_NextShardNum].v2r = 5;
    ptr_shattered_window_pieces[g_NextShardNum].v2g = 0xFB;
    ptr_shattered_window_pieces[g_NextShardNum].v2b = 0x7E;
    ptr_shattered_window_pieces[g_NextShardNum].v3r = 0xFB;
    ptr_shattered_window_pieces[g_NextShardNum].v3g = 0xFB;
    ptr_shattered_window_pieces[g_NextShardNum].v3b = 0x7E;
    ptr_shattered_window_pieces[g_NextShardNum].v3a = 0xFF;

    alpha = ptr_shattered_window_pieces[g_NextShardNum].v3a;
    ptr_shattered_window_pieces[g_NextShardNum].v2a = alpha;
    ptr_shattered_window_pieces[g_NextShardNum].v1a = alpha;

    ptr_shattered_window_pieces[g_NextShardNum].rot.x = rotX;
    ptr_shattered_window_pieces[g_NextShardNum].rot.y = 0.0f;
    ptr_shattered_window_pieces[g_NextShardNum].rot.z = (randomGetNext() * (1.0f / (f32)UINT_MAX)) * M_TAU_F;

    /**
     * Impart a random angular velocity on each shard piece.
     */
    ptr_shattered_window_pieces[g_NextShardNum].angvel.x = (randomGetNext() * (1.0f / (f32)UINT_MAX)) * SHARD_ANGVEL_SCALE;
    ptr_shattered_window_pieces[g_NextShardNum].angvel.y = (randomGetNext() * (1.0f / (f32)UINT_MAX)) * SHARD_ANGVEL_SCALE;
    ptr_shattered_window_pieces[g_NextShardNum].angvel.z = (randomGetNext() * (1.0f / (f32)UINT_MAX)) * SHARD_ANGVEL_SCALE;

    g_NextShardNum++;
    if (g_NextShardNum >= SHATTERED_WINDOW_PIECES_BUFFER_LEN) {
        g_NextShardNum = 0;
    }
}


void update_broken_windows(void) {
    f32 var_f0;
    s32 i;
    s32 j;

    if (g_ClockTimer < 15) {
        var_f0 = (f32)g_ClockTimer;
    } else {
        var_f0 = 15.0f;
    }
    if (SHATTERED_WINDOW_PIECES_BUFFER_LEN > 0) {
        i = 0;
        do {
            if (ptr_shattered_window_pieces[i].active > 0) {
                ptr_shattered_window_pieces[i].active += (s32)var_f0;
                j = 0;
                ptr_shattered_window_pieces[i].rot.x += ptr_shattered_window_pieces[i].angvel.x * var_f0;
                ptr_shattered_window_pieces[i].rot.y += ptr_shattered_window_pieces[i].angvel.y * var_f0;
                ptr_shattered_window_pieces[i].rot.z += ptr_shattered_window_pieces[i].angvel.z * var_f0;
                ptr_shattered_window_pieces[i].pos.x += ptr_shattered_window_pieces[i].velocity.x * var_f0;
                ptr_shattered_window_pieces[i].pos.z += ptr_shattered_window_pieces[i].velocity.z * var_f0;
                if ((s32)var_f0 > 0) {
                    do {
                        ptr_shattered_window_pieces[i].pos.y += ptr_shattered_window_pieces[i].velocity.y;
                        ptr_shattered_window_pieces[i].velocity.y -= 0.1f;
                        j++;
                    } while (j < (s32)var_f0);
                }
                if (ptr_shattered_window_pieces[i].active >= 0x96) {
                    ptr_shattered_window_pieces[i].active = 0;
                }
                if (ptr_shattered_window_pieces[i].pos.y < -30000.0f ||
                    ptr_shattered_window_pieces[i].pos.y > 30000.0f) {
                    ptr_shattered_window_pieces[i].active = 0;
                }
            }
            i++;
        } while (i < SHATTERED_WINDOW_PIECES_BUFFER_LEN);
    }
}


/**
 * Address: 7F0A2C44
 */
Gfx *glassRenderShards(Gfx *gdl)
{
    Mtxf mtxf;
    s32 i;
    Mtx *mtx;

    texSelect(&gdl, glassoverlayimage + 1, 2, 1, 2);

    gSPTexture(gdl++, 0x0D80, 0x0D80, 2, G_TX_RENDERTILE, 1);
    gDPSetCycleType(gdl++, G_CYC_2CYCLE);
    gDPSetTextureLOD(gdl++, G_TL_LOD);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gSPSetGeometryMode(gdl++, G_LIGHTING | G_TEXTURE_GEN);
    gSPMatrix(gdl++, osVirtualToPhysical(get_BONDdata_field_10E0()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

    #define WINDOW_PIECE(_i) ((s_shattered_window_piece*)((u8*)ptr_shattered_window_pieces + ((_i) * sizeof(s_shattered_window_piece))))

    for (i = 0; i < SHATTERED_WINDOW_PIECES_BUFFER_LEN; i++) {
        if (WINDOW_PIECE(i)->active > 0) {
            mtx = dynAllocateMatrix();

            matrix_4x4_set_position_and_rotation_around_xyz(&WINDOW_PIECE(i)->pos, &WINDOW_PIECE(i)->rot, &mtxf);

            mtxf.m[3][0] -= g_CurrentPlayer->current_model_pos.x;
            mtxf.m[3][1] -= g_CurrentPlayer->current_model_pos.y;
            mtxf.m[3][2] -= g_CurrentPlayer->current_model_pos.z;

            matrix_4x4_f32_to_s32(&mtxf, (Mtxf *)mtx);

            gSPMatrix(gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPVertex(gdl++, osVirtualToPhysical(&WINDOW_PIECE(i)->v1x), 3, 0);
            gSP1Triangle(gdl++, 0, 1, 2, 0);
        }
    }
    #undef WINDOW_PIECE

    gSPClearGeometryMode(gdl++, G_LIGHTING | G_TEXTURE_GEN);
    gSPMatrix(gdl++, (uintptr_t)currentPlayerGetProjectionMatrix(), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gdl++, (uintptr_t)currentPlayerGetMatrix10C8(), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    return gdl;
}
