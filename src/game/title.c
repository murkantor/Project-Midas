#include "dyn.h"
#include "game/frametiming.h"
#include "game/rle.h"
#include <ultra64.h>
#include <PR/gbi.h>
#include <PR/gu.h>
#include <PR/os.h>
#include <assets/font_dl.h>
#include <assets/animationtable_data.h>
#include <bondgame.h>
#include <fr.h>
#include <macro.h>
#include <ramrom.h>
#include "blood_animation.h"
#include "bondtypes.h"
#include "chr.h"
#include "chr_b.h"
#include "chrobjdata.h"
#include "image.h"
#include "initanitable.h"
#include "math.h"
#include "math_floor.h"
#include "matrixmath.h"
#include "model.h"
#include "music.h"
#include "ob.h"
#include "objecthandler.h"
#include "title.h"
#include "title2.h"
#include "title3.h"


extern signed short sins(unsigned short x);


// bss
//CODE.bss:80069550
uintptr_t barrelDisplayListPtr;
//CODE.bss:80069554
Gfx *gunbarrelgfxListPointer;
//CODE.bss:80069558
Mtx *matrixBufferRareLogo0;
//CODE.bss:8006955C
Mtx *matrixBufferGunbarrel0;
//CODE.bss:80069560
Mtx *matrixBufferRareLogo1;
//CODE.bss:80069564
Mtx *matrixBufferRareLogo2;
//CODE.bss:80069568
Mtx *matrixBufferGunbarrel1;
//CODE.bss:8006956C
Mtx *matrixBufferIntroBackdrop;
//CODE.bss:80069570
Mtx *matrixBufferIntroBond;
//CODE.bss:80069574
f32 g_TitleX;
//CODE.bss:80069578
f32 g_TitleY;
//CODE.bss:8006957C
f32 titleTransitionX;
//CODE.bss:80069580
f32 titleTransitionY;
//CODE.bss:80069584
s16 word_CODE_bss_80069584;
//CODE.bss:80069588
uintptr_t dword_CODE_bss_80069588;
//CODE.bss:8006958C
uintptr_t dword_CODE_bss_8006958C;
//CODE.bss:80069590
uintptr_t virtualaddress;

/**
 * Address 80069594
 * EU .bss 800684D4
*/
s32 gunbarrelTimer;


// data
u32 D_8002A7D0 = 0;
u8 gunbarrel_mode = 0x3;
u32 D_8002A7D8 = 0;

struct FolderSelectColour g_FolderGradientBlack = { 0x00, 0x00, 0x00 };
struct FolderSelectColour g_FolderGradientWhite = { 0xFF, 0xFF, 0xFF };

Model *chrModelInstance = NULL;
Model *gunModelInstance = NULL;

ModelRenderData gunbarrelRenderData = { NULL, TRUE, 3, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, CULLMODE_BOTH };

f32 gunbarrelPosition1[3] = {1758.2957f, 220.0f, 684.28143f};
f32 gunbarrelPosition2[3] = {-0.97f, 0.0f, 0.24f};
f32 gunbarrelPosition3[3] = {0.0f, 1.0f, -0.0f};

Lights1 gunbarrelLights = gdSPDefLights1(0xDC, 0xDC, 0xDC, 0xFF, 0xFF, 0xFF, 0x00, 0x7F, 0x00);

f32 cameraPosition1[3] = {0.0f, 0.0f, 4883.0f};
f32 cameraPosition2[3] = {0.0f, 0.0f, -1.0f};
f32 cameraPosition3[3] = {0.0f, 1.0f, 0.0f};

f32 D_8002A89C = 0.0f;
s32 intro_eye_counter = 0;
u32 intro_state_blood_animation = 0;
struct coord3d D_8002A8A8 = { 0, 0, 0 };


/**
 * Manipulates matrices for the gun barrel and Rareware logo during the intro sequence.
 * 
 * @param gdl The display list to append graphics commands to.
 * @return Updated display list.
 */
Gfx *manipulateGunbarrelAndLogoMatrices(Gfx *gdl)
{
    guTranslate(&matrixBufferRareLogo2[D_8002A7D0], g_TitleX, g_TitleY, -5.0f);
    guTranslate(&matrixBufferGunbarrel1[D_8002A7D0], titleTransitionX, titleTransitionY, -5.0f);
    gSPDisplayList(gdl++, &dlBasicGeometry);

    gdl = sub_GAME_7F01C1A4(clear_framebuffer_black(gdl));

    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, 0xE6, 0xE6, 0xE6, 0x00);
    gSPDisplayList(gdl++, OS_K0_TO_PHYSICAL(gunbarrelgfxListPointer));
    gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferGunbarrel1[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    gSPDisplayList(gdl++, OS_K0_TO_PHYSICAL(gunbarrelgfxListPointer));

    return gdl;
}

Gfx *insert_sight_backdrop_eye_intro(Gfx *gdl)
{
    guTranslate(&matrixBufferRareLogo2[D_8002A7D0], g_TitleX + 768.0f, g_TitleY - 40.0f, -5.0f);
    guScale(&matrixBufferGunbarrel1[D_8002A7D0], 2.7f, 2.57f, 1.0f);
    gSPDisplayList(gdl++, &dlBasicGeometry);
    gSPDisplayList(gdl++, &dlFastPipelineSetup);

    gdl = sub_GAME_7F01C1A4(gdl);

    gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferGunbarrel1[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW));
    gSPDisplayList(gdl++, OS_K0_TO_PHYSICAL(gunbarrelgfxListPointer));

    return gdl;
}


/**
 * Address: 7F007CC8
 */
Gfx *titleRenderFolderMenuBackground(Gfx *gdl, s32 xOffset, struct FolderSelectColour *topColour, struct FolderSelectColour *bottomColour)
{
    gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetTextureFilter(gdl++, G_TF_POINT);
    gDPPipeSync(gdl++);

    return titleRenderFolderMenuBackgroundLines(gdl, OS_K0_TO_PHYSICAL(dword_CODE_bss_8006958C), xOffset, topColour, bottomColour);
}


Gfx *insert_sniper_sight_eye_intro(Gfx *gdl)
{
    struct FolderSelectColour topGradientColour = g_FolderGradientBlack;
    struct FolderSelectColour bottomGradientColour = g_FolderGradientWhite;

    gSPDisplayList(gdl++, &dlBasicGeometry);

    gdl = clear_framebuffer_black(gdl);

    gDPSetCombineMode(gdl++, G_CC_MODULATEI_PRIM, G_CC_MODULATEI_PRIM);

    return titleRenderFolderMenuBackground(gdl, floorFloat((viGetX() * g_TitleX) / 1280.0f), &topGradientColour, &bottomGradientColour);
}


Gfx *sub_GAME_7F007E70(Gfx *gdl, u32 alpha)
{
    gdl = sub_GAME_7F01C1A4(gdl);

    gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, 0x00, 0x00, 0x00, alpha);
    gDPSetColorDither(gdl++, G_CD_MAGICSQ);
#ifndef TARGET_N64
    /* Spans the window for the same reason as the red wash this fades out
     * (blood_animation.c sub_GAME_7F01CA18): if the fade stayed pinned the
     * red side bands would survive it while the centre went black.
     * 0x48 = G_MIRRORPILLAR_EXT, emitted inline as lv.c does for 0x47. */
    gdl->words.w0 = (u32) 0x48 << 24;
    gdl->words.w1 = 1;
    gdl++;
#endif
    gDPFillRectangle(gdl++, 0, 0, viGetX(), viGetY());
#ifndef TARGET_N64
    gdl->words.w0 = (u32) 0x48 << 24;
    gdl->words.w1 = 0;
    gdl++;
#endif

    return gdl;
}


/**
 * Address: 7F007F30
 */
Gfx *sub_GAME_7F007F30(Gfx *gdl, s32 count, Mtxf *matrix)
{
#if defined(REFRESH_PAL)
    #define BOND_EYE_ANIM_START 114
    #define BOND_EYE_ANIM_SPEEDUP 176
    #define BOND_EYE_FIRE_SHOT 191
#else
    #define BOND_EYE_ANIM_START 137
    #define BOND_EYE_ANIM_SPEEDUP 212
    #define BOND_EYE_FIRE_SHOT 230
#endif
    u32 pad;
    ModelRenderData renderData;
    u32 pad2[2];
    ModelHitEntry *entry;
    bool playedShot;
    s32 i;

    renderData = gunbarrelRenderData;
    playedShot = FALSE;

    for (i = 0; i < count; i++)
    {
        if (gunbarrelTimer >= 0)
        {
            gunbarrelTimer++;

            if (gunbarrelTimer == BOND_EYE_ANIM_START)
            {
                // Terrible memory read that might just freeze the game. Fixed so it wouldn't happen anymore.
                modelSetAnimation(chrModelInstance, (struct ModelAnimation *) (PTR_ANIM_bond_eye_fire + (intptr_t) &ptr_animation_table->data), 0, 2.0f, 0.910000026f, 16.0f);
            }

            if (gunbarrelTimer == BOND_EYE_ANIM_SPEEDUP)
            {
                modelSetAnimSpeed(chrModelInstance, 1.6f, 8.0f);
            }
        }

        modelTickAnim(chrModelInstance, 1, 1);

        if (gunbarrelTimer == BOND_EYE_FIRE_SHOT)
        {
            sndPlaySfx(g_musicSfxBufferPtr, GUN_RIFLE7BIG_1_SFX, NULL);
            playedShot = TRUE;
        }
    }

    modelSetDistanceDisabled(1);
#ifdef TARGET_N64
    sub_GAME_7F073FC8(80);
#else
    { /* The intro shadow has its own strength (midas.ini intro_shadow_alpha) */
        extern void portShadowIntensityIntro(s32 arg0);
        portShadowIntensityIntro(80);
    }
#endif
    subcalcpos(chrModelInstance);

    if (gunModelInstance->obj->Switches[0] != NULL)
    {
        modelGetNodeRwData(gunModelInstance, gunModelInstance->obj->Switches[0])->Gunfire.visible = playedShot;
    }

    if (gunModelInstance->obj->Switches[2] != NULL)
    {
        modelGetNodeRwData(gunModelInstance, gunModelInstance->obj->Switches[2])->Switch.visible = playedShot;
    }

    renderData.basemtx = matrix;
    renderData.mtxlist = dynAllocate(chrModelInstance->obj->numMatrices << 6);
    subcalcmatrices(&renderData, chrModelInstance);

    renderData.basemtx = modelFindNodeMtx(chrModelInstance, gunModelInstance->attachedto_objinst, 0);
    renderData.mtxlist = dynAllocate(gunModelInstance->obj->numMatrices << 6);
    instcalcmatrices(&renderData, gunModelInstance);

#ifndef TARGET_N64
    { /* bring-up (GE_DUMP_BOND): the walk's chr transforms come out screens
       * off-axis - dump each link of the chain to find where garbage enters:
       * lookat basemtx vs root suboffset vs the composed root matrix. */
        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
        static u32 throttle;
        if ((throttle++ & 15) == 0) {
            coord3d off;
            Mtxf *rootm = modelFindNodeMtx(chrModelInstance, chrModelInstance->obj->RootNode, 0);
            getsuboffset(chrModelInstance, &off);
            ge_dbg_vec("bond base.r0", matrix->m[0], 4);
            ge_dbg_vec("bond base.r3", matrix->m[3], 4);
            ge_dbg_vec("bond suboff", &off.x, 3);
            ge_dbg_vec("bond root.r3", rootm->m[3], 4);
        }
    }
#endif

    entry = sub_GAME_7F06B120(NULL, chrModelInstance);
    entry = sub_GAME_7F06B120(entry, gunModelInstance);
    sub_GAME_7F06B29C(entry);
    entry = sub_GAME_7F06BB28(entry);

    renderData.PropType = 7;
    renderData.zbufferenabled = FALSE;
    renderData.gdl = gdl;
    renderData.flags = 1;
    drawjointlist(&renderData, entry);

    renderData.flags = 2;
    drawjointlist(&renderData, entry);

    modelSetDistanceDisabled(0);
    sub_GAME_7F06B248(entry);

    for (i = 0; i < chrModelInstance->obj->numMatrices; i++)
    {
        Mtxf sp88;

        matrix_4x4_copy((Mtxf *) &((s8 *) chrModelInstance->render_pos)[i * sizeof(Mtxf)], &sp88);
        matrix_4x4_f32_to_s32(&sp88, &((Mtxf *) chrModelInstance->render_pos)[i]);
    }

    for (i = 0; i < gunModelInstance->obj->numMatrices; i++)
    {
        Mtxf sp48;

        matrix_4x4_copy((Mtxf *) &((s8 *) gunModelInstance->render_pos)[i * sizeof(Mtxf)], &sp48);
        matrix_4x4_f32_to_s32(&sp48, &((Mtxf *) gunModelInstance->render_pos)[i]);
    }

    return renderData.gdl;
}


Gfx *insert_bond_eye_intro(Gfx *gdl) {
    Mtxf matrix;
    u16 perspNorm;
    guTranslate(&matrixBufferIntroBackdrop[D_8002A7D0], 0.0f, 0.0f, 0.0f);
    guPerspective(&matrixBufferIntroBond[D_8002A7D0], &perspNorm, 46.0f, (320.0f / 240.0f), 10.0f, 10000.0f, 1.0f);
    gSPPerspNormalize(gdl++, perspNorm);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(gdl++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferIntroBond[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
    gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferIntroBackdrop[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    
    matrix_4x4_set_lookat_target(&matrix, gunbarrelPosition1[0], gunbarrelPosition1[1], gunbarrelPosition1[2], (gunbarrelPosition1[0] + gunbarrelPosition2[0]), (gunbarrelPosition1[1] + gunbarrelPosition2[1]), (gunbarrelPosition1[2] + gunbarrelPosition2[2]), gunbarrelPosition3[0], gunbarrelPosition3[1], gunbarrelPosition3[2]);

#if defined REFRESH_PAL
    return sub_GAME_7F007F30(gdl, 1, &matrix);
#else
    return sub_GAME_7F007F30(gdl, 2, &matrix);
#endif
}


extern Gfx *D_020043E8;
extern Gfx *DL_RAREWARETEXT;
extern Gfx *D_02004758;
extern u8 *D_02004FE8;
extern u8 *D_02005FF0;

Gfx *load_display_rare_logo(Gfx *gdl, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
    cameraPosition1[2] = arg3;
    gSPDisplayList(gdl++, &dlBasicGeometry);
    gdl = clear_framebuffer_black(gdl);
    {
        u16 perspNorm;
        guPerspective(&matrixBufferRareLogo0[D_8002A7D0], &perspNorm, 60.0f, (320.0f / 240.0f), 100.0f, 5000.0f, 1.0f);
        gSPPerspNormalize(gdl++, perspNorm);
    }
    gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferRareLogo0[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, (G_SHADE | G_CULL_BACK | G_LIGHTING | G_TEXTURE_GEN | G_SHADING_SMOOTH));
    guLookAt(&matrixBufferRareLogo1[D_8002A7D0], cameraPosition1[0], cameraPosition1[1], cameraPosition1[2], (cameraPosition1[0] + cameraPosition2[0]), (cameraPosition1[1] + cameraPosition2[1]), (cameraPosition1[2] + cameraPosition2[2]), cameraPosition3[0], cameraPosition3[1], cameraPosition3[2]);
    gSPMatrix(gdl++,  osVirtualToPhysical(&matrixBufferRareLogo1[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
    guRotate(&matrixBufferRareLogo2[D_8002A7D0], D_8002A89C, 0.0f, 1.0f, 0.0f);
#if defined(REFRESH_PAL)
    D_8002A89C += 2.4f;
#else
    D_8002A89C += 2.0f;
#endif
    gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferRareLogo2[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW));
    gSPSetLights1(gdl++, gunbarrelLights);
    gunbarrelLights.a.l.col[0] = gunbarrelLights.a.l.col[1] = gunbarrelLights.a.l.col[2] = gunbarrelLights.a.l.colc[0] = gunbarrelLights.a.l.colc[1] = gunbarrelLights.a.l.colc[2] = arg4;
    gDPPipeSync(gdl++);
    gDPPipeSync(gdl++);
    gDPSetCombineMode(gdl++, G_CC_MODULATEI, G_CC_MODULATEI);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetTextureDetail(gdl++, G_TD_CLAMP);
    gDPSetTextureLOD(gdl++, G_TL_TILE);
    gDPSetTextureLUT(gdl++, G_TT_NONE);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gDPSetTextureConvert(gdl++, G_TC_FILT);
    gDPPipeSync(gdl++);
    gDPPipeSync(gdl++);
    gSPTexture(gdl++, 0x0800, 0x0800, 0, G_TX_RENDERTILE, G_ON);
    gDPLoadTextureBlock(gdl++, &D_02004FE8, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, (G_TX_NOMIRROR | G_TX_WRAP), (G_TX_NOMIRROR | G_TX_WRAP), 5, 5, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetPrimColor(gdl++, 0, 0, arg4, arg4, arg4, 0xFF);
    gSPDisplayList(gdl++, &D_020043E8);
    gSPDisplayList(gdl++, &DL_RAREWARETEXT);
    gDPLoadTextureBlock(gdl++, &D_02005FF0, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, (G_TX_NOMIRROR | G_TX_WRAP), (G_TX_NOMIRROR | G_TX_WRAP), 5, 5, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetPrimColor(gdl++, 0, 0, ((arg4 * 0xF0) / 0xFF), ((arg4 * 0xD0) / 0xFF), ((arg4 * 0xF0) / 0xFF), 0xFF);
    gSPDisplayList(gdl++, &D_02004758);

    return gdl;
}


extern void *_rarewarelogoSegmentRomStart;
extern void *_rarewarelogoSegmentStart;
extern void *_rarewarelogoSegmentEnd; 
void setupRarewareLogoData(uintptr_t address, s32 size) {
    gunbarrel_mode = 0;
    g_TitleX = 880.0f;
    D_8002A89C = -40.0f;
    intro_eye_counter = 0;
    virtualaddress = address;
    romCopy(virtualaddress, &_rarewarelogoSegmentRomStart, ALIGN64_V2((uintptr_t)&_rarewarelogoSegmentEnd - (uintptr_t)&_rarewarelogoSegmentStart));

#ifndef TARGET_N64
    { /* the logo data is typed host references throughout (the segment
       * copy above is vestigial); only its RGBA16 texel blobs need the
       * 16-bit halfword exchange - one-shot, see segments.c */
        extern void portRarewareLogoFixTexels(void);
        portRarewareLogoFixTexels();
    }
#endif
}


Gfx *retrieve_display_rareware_logo(Gfx *gdl)
{
#if defined(REFRESH_PAL)
#define RAREWARE_LOGO_DEN 58
#define RAREWARE_LOGO_SUB 33915
#define RAREWARE_LOGO_EYE_COUNT1 216
#define RAREWARE_LOGO_EYE_COUNT2 241
#else
#define RAREWARE_LOGO_DEN 70
#define RAREWARE_LOGO_SUB 40800
#define RAREWARE_LOGO_EYE_COUNT1 260
#define RAREWARE_LOGO_EYE_COUNT2 290
#endif

    D_8002A7D0 = (1 - D_8002A7D0);
    gSPSegment(gdl++, SPSEGMENT_GETITLE, osVirtualToPhysical(virtualaddress));
    if ((gunbarrel_mode == 0) || (gunbarrel_mode == 1)) {
        s32 var1;
        s32 var2;
        var1 = (intro_eye_counter * 255) / RAREWARE_LOGO_DEN;
        if (var1 > 255) {
            var1 = 255;
        }
        if (var1 < 0) {
            var1 = 0;
        }

        var2 = 255 - (((intro_eye_counter * 255) - RAREWARE_LOGO_SUB) / RAREWARE_LOGO_DEN);
        if (var2 > 255) {
            var2 = 255;
        }
        if (var2 < 0) {
            var2 = 0;
        }
        gdl = load_display_rare_logo(gdl, 403, 488, g_TitleX, (var1 * var2) / 255);
        if (intro_eye_counter++ >= RAREWARE_LOGO_EYE_COUNT1) {
            if (intro_eye_counter >= RAREWARE_LOGO_EYE_COUNT2) {
                intro_eye_counter = 0;
                gunbarrel_mode++;
                gunbarrel_mode++;
            }
        }
    }

    return gdl;
}


s32 isGunBarrelInMode2(void) {
    return (gunbarrel_mode == 2);
}


#ifdef TARGET_N64
extern void *unknown2;
extern void *unknown2_end;
#else
/* PORT (port/src/ramrom_data.c): these are POINTER VARIABLES that hold the
 * compiled-in RLE blob until romdata.c repoints them at the user's cart;
 * `&unknown2` / `&unknown2_end` below are the pointer values. */
extern u8 *port_unknown2;
extern u8 *port_unknown2_end;
#define unknown2     (*port_unknown2)
#define unknown2_end (*port_unknown2_end)
#endif
void sub_GAME_7F008DE4(u8 **addr, s32 *size) {
    dword_CODE_bss_8006958C = *addr;
    *size -= 0x40400;
    *addr += 0x40400;
    dword_CODE_bss_80069588 = *addr;
    romCopy(dword_CODE_bss_80069588, (void *)(intptr_t)&unknown2, ALIGN64_V2(((uintptr_t)&unknown2_end - (uintptr_t)&unknown2)));
    rle_expand_8bit(dword_CODE_bss_80069588, dword_CODE_bss_8006958C);
}


void initializeGunBarrelIntro(u8 *gfxBuffer, s32 bufferSize)
{
    struct ModelAnimation *animation;
    struct coord3d sp50;
    struct texpool texturePool;
    s32 temp_t9;
    s32 startframe;
    
    gunbarrel_mode = 2;
    
    guOrtho(matrixBufferGunbarrel0, 0.0f, 1280.0f, 0.0f, 960.0f, 1.0f, 8.0f, 256.0f);
    
    g_TitleX = -30.0f;
    g_TitleY = 482.0f;
    titleTransitionX = -100.0f;
    titleTransitionY = 482.0f;
    word_CODE_bss_80069584 = 0x42;
    barrelDisplayListPtr = gfxBuffer;
    bufferSize -= 0x200;
    gfxBuffer += 0x200;
    
    createGunbarrelRenderHole(barrelDisplayListPtr, 0x1E);
    
    gunbarrelgfxListPointer = (Gfx*)gfxBuffer;
    bufferSize -= 32 * sizeof(Gfx);
    gfxBuffer += 32 * sizeof(Gfx);
    
#ifdef TARGET_N64
    sub_GAME_7F01BFF8(gunbarrelgfxListPointer, barrelDisplayListPtr + 0x80000000, 0x1E);
#else
    /* hand-rolled PHYSICAL_TO_K0 (physical DL pointer -> K0); identity on PC */
    sub_GAME_7F01BFF8(gunbarrelgfxListPointer, barrelDisplayListPtr, 0x1E);
#endif
    sub_GAME_7F008DE4((u8 **)&gfxBuffer, &bufferSize);

    // struct copy
    sp50 = D_8002A8A8;
    
    temp_t9 = 0x12C00;
    
    texInitPool(&texturePool, gfxBuffer, temp_t9);
    
    gfxBuffer += temp_t9;
    bufferSize -= temp_t9;
    
    load_object_fill_header(c_item_entries[BODY_Brosnan_Tuxedo].header, c_item_entries[BODY_Brosnan_Tuxedo].filename, gfxBuffer, bufferSize, &texturePool);
    
    temp_t9 = ((get_pc_buffer_remaining_value(c_item_entries[BODY_Brosnan_Tuxedo].filename) + 0x3F) | 0x3F) ^ 0x3F;
    bufferSize -= temp_t9;
    gfxBuffer += temp_t9;
    
    load_object_fill_header(c_item_entries[BODY_Male_Pierce_Bond_Tuxedo].header, c_item_entries[BODY_Male_Pierce_Bond_Tuxedo].filename, gfxBuffer, bufferSize, &texturePool);
    
    temp_t9 = ((get_pc_buffer_remaining_value(c_item_entries[BODY_Male_Pierce_Bond_Tuxedo].filename) + 0x3F) | 0x3F) ^ 0x3F;
    bufferSize -= temp_t9;
    gfxBuffer += temp_t9;
    
    chrModelInstance = setup_chr_instance(BODY_Brosnan_Tuxedo, BODY_Male_Pierce_Bond_Tuxedo, c_item_entries[BODY_Brosnan_Tuxedo].header, c_item_entries[BODY_Male_Pierce_Bond_Tuxedo].header, 0);

    modelSetScale(chrModelInstance, 0.18779343f);
    modelSetAnimTranslationScale(chrModelInstance, 1.0f);
    setsuboffset(chrModelInstance, &sp50);
    setsubroty(chrModelInstance, 0.0f);
#if defined(VERSION_EU)
    #define S_7F008E80_ANIM_SPEED 0.6f
#else
    #define S_7F008E80_ANIM_SPEED 0.5f
#endif
    modelSetAnimPlaySpeed(chrModelInstance, S_7F008E80_ANIM_SPEED, 0.0f);
#undef S_7F008E80_ANIM_SPEED
    
    animation = portAnimationResolve((struct ModelAnimation*)((intptr_t)ptr_animation_table + PTR_ANIM_bond_eye_walk));
    startframe = animation->unk04 - 0x44;
    while (startframe < 0) // Very dangerous, can cause infinite loops
    {
        startframe += animation->unk04;
    }
    
    modelSetAnimation(chrModelInstance, (struct ModelAnimation *) animation, 0, (f32) startframe, 0.91f, 0.0f);
    load_object_fill_header(PitemZ_entries[PROP_CHRWPPK].header, PitemZ_entries[PROP_CHRWPPK].filename, gfxBuffer, bufferSize, &texturePool);
    
    temp_t9 = ((get_pc_buffer_remaining_value(PitemZ_entries[PROP_CHRWPPK].filename) + 0x3F) | 0x3F) ^ 0x3F;
    bufferSize -= temp_t9;
    gfxBuffer += temp_t9;
    
    modelCalculateRwDataLen(PitemZ_entries[191].header);
    
    gunModelInstance = modelmgrInstantiateModel(PitemZ_entries[PROP_CHRWPPK].header);
    
    modelSetScale(gunModelInstance, 0.18779343f);
    
    gunModelInstance->attachedto = chrModelInstance;
    gunModelInstance->attachedto_objinst = chrModelInstance->obj->Switches[3];
    gunbarrelTimer = 0;    
}


void clearChrGunModelInstances(void)
{
    if (chrModelInstance)
    {
        clear_aircraft_model_obj(chrModelInstance);
    }

    if (gunModelInstance)
    {
        clear_model_obj(gunModelInstance);
    }
}

#ifndef REFRESH_PAL
    #define XINC 6.0f
    #define XDEC 12.0f
    #define XDEC2 6
    #define XDEC3 5.8183274f
    #define INCVAL 0x38E
    #define INTRO_EYE_COUNTER_CASE_4 108
    #define INTRO_EYE_COUNTER_CASE_5_ADD 8
    #define INTRO_EYE_COUNTER_CASE_6 0x1e
#else
    #define XINC 7.0f
    #define XDEC 14.0f
    #define XDEC2 6
    #define XDEC3 3.63643622398f
    #define INCVAL 0x444
    #define INTRO_EYE_COUNTER_CASE_4 90
    #define INTRO_EYE_COUNTER_CASE_5_ADD 9
    #define INTRO_EYE_COUNTER_CASE_6 0x19
#endif


/*
 * Address: 0x7F009254
*/
#ifndef TARGET_N64
extern s32 portBloodFrameDue(u32 *stamp);         /* blood_animation.c: 30fps */
extern s32 portPaceDue(u32 *stamp, u32 counts);   /* frametiming.c */
extern u32 portTickFieldGateCounts(void);         /* one 60Hz field, in counts */
static u32 sGunbarrelBloodStamp;
static u32 sGunbarrelStepStamp;
s32 portGunbarrelClampPhase(void); /* below: modes 3..7 = the 30fps-authored barrel phase */
#endif

Gfx *renderGunbarrelEyeIntroSequence (Gfx *gdl) {
#ifndef TARGET_N64
    /* PORT: this function builds a display list once per presented frame, and
     * its steps (`g_TitleX += XINC`, `intro_eye_counter--`, INCVAL sway) are
     * raw per-call counters, so tying them to frame rate made the barrel run
     * too fast. */
    /* PORT: step the sequence once per admitted sim tick, which boss.c holds at
     * exactly 30Hz through the barrel (modes 3..7), so the sway, blood curtain,
     * durations and Bond's walk share one clock and a re-presented frame
     * redraws the same state. GE_INTROSTEPPACE=1 restores the old real-time
     * pacer for comparison. */
    static s32 sStepPace = -1;
    static u32 sLastStepTick;
    extern u32 g_PortTicksAdmitted; /* boss.c */
    s32 step;
    if (sStepPace < 0) {
        extern char *getenv(const char *);
        sStepPace = getenv("GE_INTROSTEPPACE") != NULL;
    }
    if (sStepPace) {
        step = portPaceDue(&sGunbarrelStepStamp, portTickFieldGateCounts());
    } else {
        step = (g_PortTicksAdmitted != sLastStepTick);
        sLastStepTick = g_PortTicksAdmitted;
    }
#else
    const s32 step = 1;
#endif

    D_8002A7D0 = (1 - D_8002A7D0);
    switch (gunbarrel_mode - 2)
    {
    case 0:
        gdl = manipulateGunbarrelAndLogoMatrices(gdl);
        if (step) {
            g_TitleX += XINC;
            if (word_CODE_bss_80069584 < 0) {
                word_CODE_bss_80069584 = 200;
                titleTransitionX = (g_TitleX - XDEC);
            } else {
#if defined(VERSION_EU)
                word_CODE_bss_80069584 -= 7;
#else
                word_CODE_bss_80069584 -= 6;
#endif
            }
            if (g_TitleX > 1390.0f) {
                gunbarrel_mode++;
                g_TitleX = 1276.0f;
            }
        }
        break;

    case 1:
        #if defined(LEFTOVERDEBUG)
        gSPDisplayList(gdl++, &dlBasicGeometry);
        gdl = clear_framebuffer_black(gdl++);
        gdl = clear_framebuffer_black(gdl++);
        gdl = clear_framebuffer_black(gdl++);
        gdl = clear_framebuffer_black(gdl++);
        gdl = clear_framebuffer_black(gdl++);
        #endif
        gdl = insert_sniper_sight_eye_intro(gdl++);
        gdl = insert_sight_backdrop_eye_intro(gdl++);
        
        if (g_TitleX < 600.0f) {
            gdl = insert_bond_eye_intro(gdl);
        }
        if (step) {
            g_TitleX -= XDEC3;
            if (g_TitleX <= -80.0f) {
                gunbarrel_mode++;
                intro_eye_counter = 20;
            }
        }
        break;

    case 2:
        gdl = insert_sniper_sight_eye_intro(gdl);
        gdl = insert_sight_backdrop_eye_intro(gdl);
        gdl = insert_bond_eye_intro(gdl);
        if (step) {
            intro_eye_counter--;
            if (intro_eye_counter < 0) {
                gunbarrel_mode++;
                die_blood_image_routine(0);
                intro_state_blood_animation = 0;
                intro_eye_counter = 1;
#ifndef TARGET_N64
                sGunbarrelBloodStamp = 0; /* 30fps clock starts with the sequence */
#endif
            }
        }
        break;

    case 3:
#ifdef TARGET_N64
        intro_eye_counter--;
        if (intro_eye_counter == 0) {
            intro_state_blood_animation = die_blood_image_routine(1);
            intro_eye_counter = 2;
        }
#else
        /* PORT: the die-blood drip runs at 30fps / portGunbarrelBloodDiv()
         * (default 15fps) while the rest of the intro stays at 30Hz. die_blood
         * completing still advances mode 5, so that phase lengthens to match. */
        {
            static s32 sBloodTick;
            if (step && (++sBloodTick % portGunbarrelBloodDiv()) == 0) {
                intro_state_blood_animation = die_blood_image_routine(1);
            }
        }
#endif
        gdl = insert_sniper_sight_eye_intro(gdl);
        gdl = insert_sight_backdrop_eye_intro(gdl);
        gdl = insert_bond_eye_intro(gdl);
        gdl = gunbarrelBloodOverlayDL(gdl);
        if (intro_state_blood_animation != 0) {
            gunbarrel_mode++;
            word_CODE_bss_80069584 = 0;
            titleTransitionX = g_TitleX;
            intro_eye_counter = 0;
        }
        break;

    case 4:
        if (step) {
            word_CODE_bss_80069584 += INCVAL;
            intro_eye_counter++;
            g_TitleX = ((sins(word_CODE_bss_80069584) * 64.0f) / 32768.0f) + titleTransitionX;
        }
        gdl = insert_sniper_sight_eye_intro(gdl);
        gdl = insert_sight_backdrop_eye_intro(gdl);
        gdl = insert_bond_eye_intro(gdl);
        gdl = sub_GAME_7F01CA18(gdl);
        if (step && intro_eye_counter >= INTRO_EYE_COUNTER_CASE_4)
        {
            intro_eye_counter = 0;
            gunbarrel_mode++;
        }
        break;

    case 5:
        if (step) {
            word_CODE_bss_80069584 += INCVAL;
            g_TitleX = ((sins(word_CODE_bss_80069584) * 64.0f) / 32768.0f) + titleTransitionX;
        }
        gdl = insert_sniper_sight_eye_intro(gdl);
        gdl = insert_sight_backdrop_eye_intro(gdl);
        gdl = insert_bond_eye_intro(gdl);
        gdl = sub_GAME_7F01CA18(gdl);

        if (step) {
            intro_eye_counter += INTRO_EYE_COUNTER_CASE_5_ADD;
        }

        gdl = sub_GAME_7F007E70(gdl, intro_eye_counter);
        if (step && intro_eye_counter >= 0xF7) {
            intro_eye_counter = 0;
            gunbarrel_mode++;                
        }
        break;

    case 6:
        gSPDisplayList(gdl++, &dlBasicGeometry);
        gdl = clear_framebuffer_black(gdl);
        if (step && intro_eye_counter++ >= INTRO_EYE_COUNTER_CASE_6) {
            intro_eye_counter = 0;
            gunbarrel_mode++;
        }
        break;
    };

    return gdl;
}


s32 isGunBarrelInMode9(void) {
    return (gunbarrel_mode == 9);
}


#ifndef TARGET_N64
/* boss.c 30Hz tick gate for the barrel intro: full speed for the dot sweep,
 * halved from mode 3 (barrel texture appears) through mode 7. The scene was
 * authored for 30fps NTSC (the walk anim ticks twice per frame), so PC's 60Hz
 * ran it at double speed. */
s32 portGunbarrelClampPhase(void)
{
    return (gunbarrel_mode >= 3 && gunbarrel_mode <= 7);
}
#endif
