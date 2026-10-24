#include <ultra64.h>
#include "bondview.h"
#include "lv.h"
#include "unk_092E50.h"

// bss
//CODE.bss:80079E80
f32 flt_CODE_bss_80079E80;
//CODE.bss:80079E84
f32 flt_CODE_bss_80079E84;
//CODE.bss:80079E88
f32 flt_CODE_bss_80079E88;


// data
//D:8003FCC0
Gfx MipMap2C_Something_Setup[] = {
    gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_4b, 4, 0, 0, 0, G_TX_WRAP, 6, 0, G_TX_WRAP, 6, 0),
    gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_4b, 4, 0, 1, 0, G_TX_WRAP, 6, 0, G_TX_WRAP, 6, 0),
    gsDPSetTileSize(0, 2, 2, 0, 0),
    gsDPSetTileSize(1, 2, 2, 0, 0),
    gsDPSetPrimColor(0, 15, 255, 255, 255, 255),
    gsDPSetTextureDetail(G_TD_CLAMP),
    gsDPSetTextureFilter(G_TF_BILERP),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, SHADE, 0),
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2),
    gsDPSetTextureLOD(G_TL_TILE),
    gsDPSetCycleType(G_CYC_2CYCLE),
    gsSPSetGeometryMode(G_CULL_BACK ),
    gsSPEndDisplayList()
};

//D:8003FD28
Gfx MipMap2C_Something2_Setup[] = {
    gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 0, 0, G_TX_WRAP, 5, 0, G_TX_WRAP, 5, 0),
    gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 1, 0, G_TX_WRAP, 5, 0, G_TX_WRAP, 5, 0),
    gsDPSetTileSize(0, 2, 2, 0, 0),
    gsDPSetTileSize(1, 2, 2, 0, 0),
    gsDPSetPrimColor(0, 15, 255, 255, 255, 255),
    gsDPSetTextureDetail(G_TD_CLAMP),
    gsDPSetTextureFilter(G_TF_BILERP),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, SHADE, 0),
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2),
    gsDPSetTextureLOD(G_TL_TILE),
    gsDPSetCycleType(G_CYC_2CYCLE),
    gsSPSetGeometryMode(G_CULL_BACK ),
    gsSPEndDisplayList()
};

//End Dl means this gfx list cannot go any further. perhaps below is a vtx array?

u32 D_8003FD90 = 0;
f32 g_SkyCloudOffset = 0;
f32 D_8003FD98[] = { 0, 0 };

struct hand g_DefaultHandStateTemplate = {
    0, /* weaponnum */
    -1, /* weaponnum_watchmenu */
    0, /* previous_weapon */
    0, /* weapon_firing_status */
    0, /* field_87D */
    1, /* field_87E */
    0, /* field_87F */
    0, /* weapon_hold_time */
    0, /* field_884 */
    0, /* field_888 */
    0, /* field_88C */
    0, /* field_890 */
    0, /* weapon_action_state */
    0, /* weapon_current_animation */
    0, /* weapon_ammo_in_magazine */
    0, /* field_8A0 */
    0, /* numvisibleshells */
    0, /* field_8A8 */
    0, /* weapon_next_weapon */
    0, /* field_8B0 */
    0, /* weapon_animation_trigger */
    0, /* field_8B8 */
    0, /* field_8BC */
    0, /* field_8C0 */
    0, /* field_8C4 */
    0, /* field_8C8 */
    0, /* field_8CC */
    0, /* field_8D0 */
    0, /* field_8D4 */
    0, /* field_8D8 */
    0, /* field_8DC */
    0, /* field_8E0 */
    0, /* field_8E4 */
    0, /* field_8E8 */
    { { {1.0f,0.0f,0.0f,0.0f}, {0.0f,1.0f,0.0f,0.0f}, {0.0f,0.0f,1.0f,0.0f}, {0.0f,0.0f,0.0f,1.0f} } }, /* field_8EC (identity) */
    0, /* field_92C */
    0, /* sway_pos_x */
    0, /* sway_pos_y */
    0, /* sway_pos_z */
    0, /* sway_look_x */
    0, /* sway_look_y */
    -1.0f, /* sway_look_z */
    0, /* sway_up_x */
    1.0f, /* sway_up_y */
    0, /* sway_up_z */
    0, /* spring_pos_x */
    0, /* spring_pos_y */
    0, /* spring_pos_z */
    0, /* spring_look_x */
    0, /* spring_look_y */
    /* spring_look_z = 1 / GUN_SPRING_SCALE, so the derived sway vector starts unit-length */
#if defined(BUGFIX_R2)
    -16.7504158f,
#else
    -19.999996f,
#endif
    0, /* spring_up_x */
    /* spring_up_y = 1 / GUN_SPRING_SCALE */
#if defined(BUGFIX_R2)
    16.7504158f,
#else
    19.999996f,
#endif
    0, /* spring_up_z */
    { {{0,0,0}}, {{0,0,0}}, {{0,0,0}}, {{0,0,0}} }, /* blendpos[4] */
    { {{0,0,-1.0f}}, {{0,0,-1.0f}}, {{0,0,-1.0f}}, {{0,0,-1.0f}} }, /* blendlook[4] */
    { {{0,1.0f,0}}, {{0,1.0f,0}}, {{0,1.0f,0}}, {{0,1.0f,0}} }, /* blendup[4] */
    0, /* curblendpos */
    0, /* dampt */
    1.0f, /* blendscale */
    1.0f, /* blendscale1 */
    0, /* sideflag */
    0, /* weapon_theta_displacement */
    0, /* weapon_verta_displacement */
    0, /* field_A24 */
    0, /* gunofs2_x */
    0, /* gunofs2_y */
    0, /* gunofs2_z */
    0, /* field_A34 */
    0, /* field_A38 */
    0, /* field_A3C */
    1000.0f, /* field_A40 */
    0, /* audioHandle */
    0, /* field_A48 */
    0, /* field_A4C */
    0, /* field_A50 */
    { -1, 0, 0, 0, {{0,0,0}}, {{0,0,0}}, 0.0f, 0.0f, 0.0f, 0.0f }, /* weapon_beam */
    0, /* noise */
    0, /* field_A84 */
    0, /* field_A88 */
    0, /* field_A8C */
    0, /* rocket */
    0, /* firedrocket */
    0, /* gunmtx_camspace.m[0][0] */
    0, /* gunmtx_camspace.m[0][1] */
    0, /* gunmtx_camspace.m[0][2] */
    0, /* gunmtx_camspace.m[0][3] */
    0, /* gunmtx_camspace.m[1][0] */
    0, /* gunmtx_camspace.m[1][1] */
    0, /* gunmtx_camspace.m[1][2] */
    0, /* gunmtx_camspace.m[1][3] */
    0, /* gunmtx_camspace.m[2][0] */
    0, /* gunmtx_camspace.m[2][1] */
    0, /* gunmtx_camspace.m[2][2] */
    0, /* gunmtx_camspace.m[2][3] */
    0, /* gunmtx_camspace.m[3][0] */
    0, /* gunmtx_camspace.m[3][1] */
    0, /* gunmtx_camspace.m[3][2] */
    0, /* gunmtx_camspace.m[3][3] */
    { { {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0} } }, /* throw_item_pos_related */
    { { {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0} } }, /* throw_item_pos_related_prev */
    { {0,0,0} }, /* field_B58 */
    0, /* field_B64 */
    0, /* field_B68 */
    0, /* field_B6C */
    0, /* field_B70 */
    0, /* mtxlist */
    0, /* field_B78 */
    0, /* field_B7C */
    0, /* field_B80 */
    0, /* field_B84 */
    0, /* modeldatas */
    0, /* field_B8C */
    0, /* field_B90 */
    0, /* field_B94 */
    0, /* field_B98 */
    0, /* field_B9C */
    0, /* field_BA0 */
    0, /* field_BA4 */
    0, /* field_BA8 */
    0, /* field_BAC */
    0, /* field_BB0 */
    0, /* field_BB4 */
    0, /* field_BB8 */
    0, /* field_BBC */
    0, /* field_BC0 */
    0, /* field_BC4 */
    0, /* field_BC8 */
    0, /* field_BCC */
    0, /* field_BD0 */
    0, /* field_BD4 */
    0, /* field_BD8 */
    0, /* field_BDC */
    0, /* field_BE0 */
    0, /* field_BE4 */
    0, /* field_BE8 */
    0, /* field_BEC */
    0, /* field_BF0 */
    0, /* field_BF4 */
    0, /* field_BF8 */
    0, /* field_BFC */
    0, /* field_C00 */
    0, /* field_C04 */
    0, /* volley */
    { {0,0,0} }, /* item_related */
};


u64 D_80040148[] = { 0, 0, 0 }; // Unused.


// Water animation controller.
void sub_GAME_7F092E50(void)
{
#ifdef VERSION_EU
    f32 delta = g_GlobalTimerDelta;
#else
    f32 delta = g_ClockTimer;
#endif

    flt_CODE_bss_80079E80 += delta * 0.25f;

    if (flt_CODE_bss_80079E80 >= 256.0f)
    {
        flt_CODE_bss_80079E80 -= 256.0f;
    }

    if (flt_CODE_bss_80079E80 < 0.0f)
    {
        flt_CODE_bss_80079E80 += 256.0f;
    }

    flt_CODE_bss_80079E84 += delta * 0.1f;

    if (flt_CODE_bss_80079E84 >= 256.0f)
    {
        flt_CODE_bss_80079E84 -= 256.0f;
    }

    if (flt_CODE_bss_80079E84 < 0.0f)
    {
        flt_CODE_bss_80079E84 += 256.0f;
    }

    flt_CODE_bss_80079E88 += delta * 0.04f;

    // 6.2831802f is not quite equal to M_TAU_F. Leave as literal value here.
    if (flt_CODE_bss_80079E88 >= 6.2831802f)
    {
        flt_CODE_bss_80079E88 -= 6.2831802f;
    }

    if (flt_CODE_bss_80079E88 < 0.0f)
    {
        flt_CODE_bss_80079E88 += 6.2831802f;
    }
    
    /* Entries [2]/[3] are gsDPSetTileSize commands; write sl (bits 23:12) and
     * tl (bits 11:0) through words.w0 so it works on any endianness, matching
     * the original 12-bit bit-field stores. */
    MipMap2C_Something_Setup[2].words.w0 = (MipMap2C_Something_Setup[2].words.w0 & ~0xFFF000) | (((u32)flt_CODE_bss_80079E80 & 0xFFF) << 12);
    MipMap2C_Something_Setup[2].words.w0 = (MipMap2C_Something_Setup[2].words.w0 & ~0xFFF) | ((u32)flt_CODE_bss_80079E84 & 0xFFF);
    MipMap2C_Something_Setup[3].words.w0 = (MipMap2C_Something_Setup[3].words.w0 & ~0xFFF000) | ((u32)(((s32)flt_CODE_bss_80079E80 + 90) & 0xFF) << 12);
    MipMap2C_Something_Setup[3].words.w0 = (MipMap2C_Something_Setup[3].words.w0 & ~0xFFF) | (u32)(((s32)flt_CODE_bss_80079E84 + 150) & 0xFF);
    MipMap2C_Something_Setup[4].words.w0 = (MipMap2C_Something_Setup[4].words.w0 & ~0xFF) | (u32) ((sinf(flt_CODE_bss_80079E88) * 127.0f) + 128.0f);

    MipMap2C_Something2_Setup[2].words.w0 = (MipMap2C_Something2_Setup[2].words.w0 & ~0xFFF000) | (((u32)flt_CODE_bss_80079E80 & 0xFFF) << 12);
    MipMap2C_Something2_Setup[2].words.w0 = (MipMap2C_Something2_Setup[2].words.w0 & ~0xFFF) | ((u32)flt_CODE_bss_80079E84 & 0xFFF);
    MipMap2C_Something2_Setup[3].words.w0 = (MipMap2C_Something2_Setup[3].words.w0 & ~0xFFF000) | ((u32)(((s32)flt_CODE_bss_80079E80 + 90) & 0xFF) << 12);
    MipMap2C_Something2_Setup[3].words.w0 = (MipMap2C_Something2_Setup[3].words.w0 & ~0xFFF) | (u32)(((s32)flt_CODE_bss_80079E84 + 150) & 0xFF);
    MipMap2C_Something2_Setup[4].words.w0 = (MipMap2C_Something_Setup[4].words.w0 & ~0xFF) | (u32) ((sinf(flt_CODE_bss_80079E88) * 127.0f) + 128.0f);
}


Gfx* sub_GAME_7F09343C(Gfx *gdl, s32 arg1)
{
    if (arg1 != 0)
    {
        gSPDisplayList(gdl++, MipMap2C_Something_Setup);
    }
    else
    {
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 0, 0, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 0, 1, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTileSize(gdl++, 0, 0, 0, 0, 0);
        gDPSetTileSize(gdl++, 1, 90, 150, 0, 0);
        gDPSetPrimColor(gdl++, 0, (sinf(flt_CODE_bss_80079E88) * 127.0f + 128.0f), 0xFF, 0xFF, 0xFF, 0xFF);
        gDPSetTextureDetail(gdl++, G_TD_CLAMP);
        gDPSetTextureFilter(gdl++, G_TF_BILERP);
        gDPSetCombineLERP(gdl++, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetCycleType(gdl++, G_CYC_2CYCLE);
        gSPSetGeometryMode(gdl++, G_CULL_BACK);
    }
    return gdl;
}


Gfx* sub_GAME_7F09365C(Gfx *gdl, s32 arg1)
{
    if (arg1 != 0)
    {
        gSPDisplayList(gdl++, MipMap2C_Something2_Setup);
    }
    else
    {
        gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 0, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 1, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTileSize(gdl++, 0, 0, 0, 0, 0);
        gDPSetTileSize(gdl++, 1, 90, 150, 0, 0);
        gDPSetPrimColor(gdl++, 0, (sinf(flt_CODE_bss_80079E88) * 127.0f + 128.0f), 0xFF, 0xFF, 0xFF, 0xFF);
        gDPSetTextureDetail(gdl++, G_TD_CLAMP);
        gDPSetTextureFilter(gdl++, G_TF_BILERP);
        gDPSetCombineLERP(gdl++, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0); /* expands to FC272C04 1F1093FF */
        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetCycleType(gdl++, G_CYC_2CYCLE);
        gSPSetGeometryMode(gdl++, G_CULL_BACK);
    }

    return gdl;
}
