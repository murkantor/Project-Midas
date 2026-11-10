#include <math.h>
#include <ultra64.h>
#include <memp.h>
#include "sky.h"

#ifndef TARGET_N64
#include <stdio.h>  /* GE_ROOM_AUDIT sky frame-to-frame jitter probe */
#include <stdlib.h> /* GE_SKY_DOME knob */
#include "matrixmath.h"
#include "options.h" /* PORT_HUD_ANCHOR: the backdrop fills' SPAN marker */
#endif
#include "player.h"
#include "unk_092E50.h"
#include "bondview.h"
#include "lv.h"
#include "bgfog.h"
#include "bg.h"
#include "othermodemicrocode.h"
#include "fr.h"
#include "image_bank.h"

#define SKYABS(val) (val >= 0.0f ? (val) : -(val))

#ifndef TARGET_N64
/* Zoom lock: concavity is a matched pair of pixel shifts, the corner ray tilted
 * down by C px (skyGetWorldPosFromScreenPos) and the projected vertex shifted
 * back up by the same C (sub_GAME_7F097388). Both sites must use this shared
 * value, or at FOVs other than 60 degrees the whole sky is offset from the
 * screen corners. */
static f32 skyConcavityPixels(void)
{
    f32 concavity = fogGetCurrentEnvironmentp()->WaterConcavity;
    /* Default is the authentic N64 behaviour: WaterConcavity is an absolute
     * pixel shift of the 240-line frame. The FOV scaling below shrank that
     * shift and let clouds tile down into the flat sky band near the horizon,
     * so it is off unless GE_SKY_CONCSCALE=1. */
    static s32 scale = -1;
    if (scale < 0) { extern char *getenv(const char *); const char *e = getenv("GE_SKY_CONCSCALE"); scale = (e != NULL && e[0] != '0') ? 1 : 0; }
    if (!scale) return concavity;

    if (concavity != 0.0f)
    {
        f32 halffov = viGetFovY() * 0.5f * 0.017453292f;
        f32 t = tanf(halffov);

        if (t > 0.0001f)
        {
            concavity *= 0.57735027f / t; /* tan(30 deg) / tan(fovy/2) */
        }
    }

    return concavity;
}
#endif

// bss
s32 g_SkyStageNum;

s32 dword_CODE_bss_80079E94;
Mtxf dword_CODE_bss_80079E98;
u32 dword_CODE_bss_80079ED8;



/*
* Address: 0x7F093880
*/
void skyGetWorldPosFromScreenPos(f32 offset_x, f32 offset_y, coord3d* out) {
    Mtxf* player_mtxf;
    coord2d coords;
    f32 screen_top;
    f32 concavity;

    player_mtxf = currentPlayerGetViewToWorldMtxf();
    coords.x = getPlayer_c_screenleft() + offset_x;
    screen_top = getPlayer_c_screentop();
#ifndef TARGET_N64
    /* Zoom lock, ray half (see skyConcavityPixels). Stages with concavity 0 (Dam) are untouched. */
    concavity = skyConcavityPixels();
#else
    concavity = fogGetCurrentEnvironmentp()->WaterConcavity;
#endif
    coords.y = concavity + (offset_y + screen_top);
    transformAndNormalizeByLength2Dto3D(&coords, out, 100.0f);
    mtx4RotateVecInPlace(player_mtxf, out->f);
}

/*
* Address: 0x7F0938FC
*/
bool skyIsScreenCornerInSky(coord3d *corner3dpos, coord3d *dstpos, f32 *dstfrac)
{
    coord3d *eye = bondviewGetCurrentPlayersPosition();
    f32 f12 = 2.0f * corner3dpos->y / sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2] + 0.0001f);
    f32 sp2c;
    f32 f12_2;
    f32 sp24;
    u32 stack[2];
#ifdef DEBUG
    assert(eye[1] < fogGetCurrentEnvironmentp()->skyheight); //canonically bgFogGet()
#endif

    if (f12 > 1.0f)
    {
        f12 = 1.0f;
    }

    *dstfrac = 1.0f - f12;

    if (corner3dpos->y == 0.0f)
    {
        sp24 = 0.01f;
    }
    else
    {
        sp24 = corner3dpos->y;
    }

    if (sp24 > 0.0f)
    {
        sp2c = (fogGetCurrentEnvironmentp()->CloudRepeat - eye->y) / sp24;

#ifndef TARGET_N64
        { /* Optional sky convexity: instead of the authentic infinite flat
           * cloud plane, use an eye-centred spherical cap y(d) = CloudRepeat -
           * d^2/(2R) so low rays hit a finite horizon. GE_SKY_DOME=<radius>
           * sets R; 0 is the flat plane. */
            static f32 domeR = -2.0f;
            f32 dh2 = corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2];
            f32 hgt = fogGetCurrentEnvironmentp()->CloudRepeat - eye->y;

            if (domeR < -1.0f)
            {
                extern char *getenv(const char *);
                const char *e = getenv("GE_SKY_DOME");
                /* Defaults to 0 (flat, N64-authentic), because the eye-centred
                 * dome made the sky appear to turn faster than the camera and
                 * blurred the clouds. A future attempt needs a world-anchored
                 * cap and a UV-scale bound near the horizon. */
                domeR = (e != NULL) ? (f32) atof(e) : 0.0f;
            }

            if (domeR > 0.0f && dh2 > 1e-6f && hgt > 0.0f)
            {
                f32 disc = sp24 * sp24 + 2.0f * dh2 * hgt / domeR;
                f32 tdome = (sqrtf(disc) - sp24) * domeR / dh2;

                if (tdome > 0.0f && tdome < sp2c)
                {
                    sp2c = tdome;
                }
            }
        }
#endif

        f12_2 = sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2]) * sp2c;

#ifndef TARGET_N64
        { /* Edge feather: the flat plane is cut at the 300000 clamp before the
           * elevation fade finishes, so ramp the fade to 1.0 at the cut (cubic
           * ease). With the dome enabled this is nearly inert. */
            f32 dr = f12_2 / 300000.0f;
            if (dr > 1.0f)
            {
                dr = 1.0f;
            }
            dr = dr * dr * dr;
            *dstfrac = *dstfrac + (1.0f - *dstfrac) * dr;
        }
#endif

        if (f12_2 > 300000)
        {
            sp2c *= 300000 / f12_2;
        }

        dstpos->x = eye->x + sp2c * corner3dpos->f[0];
        dstpos->y = eye->y + sp2c * sp24;
        dstpos->z = eye->z + sp2c * corner3dpos->f[2];

        return TRUE;
    }

    return FALSE;
}


/*
* Address: 0x7F093A78
*/
bool skyIsCornerInWater(coord3d *corner3dpos, coord3d *dstpos, f32 *dstfrac)
{
    coord3d *eye = bondviewGetCurrentPlayersPosition();
    f32 f12 = -2.0f * corner3dpos->y / sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2] + 0.0001f);
    f32 sp2c;
    f32 f12_2;
    f32 sp24;
    u32 stack[2];
#ifdef DEBUG
    assert(eye[1] < fogGetCurrentEnvironmentp()->seaheight);
#endif

    if (f12 > 1.0f)
    {
        f12 = 1.0f;
    }

    *dstfrac = 1.0f - f12;

    if (corner3dpos->y == 0.0f)
    {
        sp24 = -0.01f;
    }
    else
    {
        sp24 = corner3dpos->y;
    }

    if (sp24 < 0.0f)
    {
        sp2c = (fogGetCurrentEnvironmentp()->WaterRepeat - eye->y) / sp24;
        f12_2 = sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2]) * sp2c;

#ifndef TARGET_N64
        { /* EDGE FEATHER: the water plane's mirror of the cloud cut -
           * see skyIsScreenCornerInSky above. */
            f32 dr = f12_2 / 300000.0f;
            if (dr > 1.0f)
            {
                dr = 1.0f;
            }
            dr = dr * dr * dr;
            *dstfrac = *dstfrac + (1.0f - *dstfrac) * dr;
        }
#endif

        if (f12_2 > 300000)
        {
            sp2c *= 300000 / f12_2;
        }

        dstpos->x = eye->x + sp2c * corner3dpos->f[0];
        dstpos->y = eye->y + sp2c * sp24;
        dstpos->z = eye->z + sp2c * corner3dpos->f[2];

        return TRUE;
    }

    return FALSE;
}

/*
* Address: 0x7F093BFC
*/
void skyCalculateEdgeVertex(coord3d *base, coord3d* ref, coord3d* out)
{
    f32 mult;

    mult = base->y / (base->y - ref->y);
    out->x = ((ref->x - base->x) * mult) + base->x;
    out->y = 0.0f;
    out->z = ((ref->z - base->z) * mult) + base->z;
}

f32 skyClamp(f32 a, f32 b, f32 c)
{
    if (a < b)
    {
        return b;
    }

    if (c < a)
    {
        return c;
    }

    return a;
}


f32 skyRound(f32 arg0)
{
    return (f32) (s32) (arg0 + 0.5f);
}


/* SCREEN-EDGE COVERAGE: the sky must reach the very edge of the screen
 * instead of bleeding out at the border.
 *
 * The sky is the only 2D thing in the world pass: sky.c builds it in 10.2
 * SCREEN coordinates and hands the RDP raw triangles, so nothing about the
 * RSP viewport constrains it - its outer edge is whatever this file says it
 * is. And this file addresses the last native pixel the way the RDP does,
 * INCLUSIVELY: every right/bottom limit is written (edge * 4) - 1 in 10.2,
 * or screenwidth - 0.25 in native units. That is a QUARTER of a pixel short
 * of the true edge.
 *
 * On the N64 the shortfall is below the raster: pixel 319 is sampled at
 * 319.5, which is still inside 319.75, so the sky covers it and the border is
 * flush. On PC the same quarter pixel is upscaled with everything else - in a
 * 1967-wide window 319.75 native lands at 1965.46, so columns 1965 and 1966
 * have no sky drawn on them at all. Nothing is drawn under the sky either, so
 * what shows through is the uncleared frame: a black hairline down the right
 * edge, two pixels here and wider the wider the window. The bottom limit has
 * the identical shortfall; world geometry usually hides it.
 *
 * SKY_EDGE_LAST_PX takes the limits to the true edge on PC and leaves the
 * N64's inclusive value exactly as it was.
 *
 * The snap in skyClampEdge is the second half. The limits above are only a
 * CLAMP - they cap a vertex that overshoots, but a vertex that lands just
 * INSIDE an edge is left there, and the sky's corner vertices are round-trip
 * results (screen corner -> cloud-plane ray -> reprojected through the sky's
 * own view-projection), so they land near the corner rather than on it. When
 * the horizon cuts a corner the mesh is drawn from those clamped corners
 * directly (the skyRenderTri branches, as opposed to the skyRenderFull ones
 * that write the rect edges in by hand), and a corner that reprojects a
 * fraction of a pixel inside leaves the same hairline on the LEFT or TOP,
 * where there is no inclusive-edge term to blame. Anything landing within one
 * native pixel of an edge is meant to BE that edge, so put it there. */
/* SKY_EDGE_LAST_PX is in native pixels, SKY_EDGE_LAST takes the EXCLUSIVE
 * view-rect edge in 10.2 units, i.e. (left + width) * 4. The N64 arms are
 * spelled out rather than derived from one another so the expressions stay
 * textually what they were before this change. */
#ifdef TARGET_N64
#define SKY_EDGE_LAST_PX 0.25f /* the RDP's inclusive last pixel */
#define SKY_EDGE_LAST(hi) ((hi) - 1.0f)
#define skyClampEdge(v, lo, hi) skyClamp((v), (lo), SKY_EDGE_LAST(hi))
#else
#define SKY_EDGE_LAST_PX 0.0f
#define SKY_EDGE_LAST(hi) (hi)
static f32 skyClampEdge(f32 v, f32 lo, f32 hi)
{
    if (v < lo + 4.0f)
    {
        return lo;
    }

    if (v > hi - 4.0f)
    {
        return hi;
    }

    return v;
}
#endif


#ifndef TARGET_N64
/* Debug: GE_SKYDBGCOL=1 draws the sky pure blue and clouds pure red, so a
 * capture tells sky from fogged world from glass at a glance. Affects sky.c
 * only; world fog colour is untouched. */
static s32 skyDbgColour(void)
{
    static s32 dbg = -1;
    if (dbg < 0)
    {
        extern char *getenv(const char *);
        const char *e = getenv("GE_SKYDBGCOL");
        dbg = (e != NULL && e[0] != '0') ? 1 : 0;
    }
    return dbg;
}
#endif

void skyChooseCloudVtxColour(SkyRelated18 *arg0, f32 arg1)
{
    f32 r = fogGetCurrentEnvironmentp()->Red;
    f32 g = fogGetCurrentEnvironmentp()->Green;
    f32 b = fogGetCurrentEnvironmentp()->Blue;
#ifndef TARGET_N64
    if (skyDbgColour())
    {
        r = 0.0f; g = 0.0f; b = 255.0f;
        arg0->r = r + 255.0f * (1.0f - r / 255.0f) * (1.0f - arg1);
        arg0->g = g;
        arg0->b = b + 0.0f;
        arg0->a = 0xff;
        return;
    }
#endif
    u32 unused;

    arg0->r = r + fogGetCurrentEnvironmentp()->CloudRed   * (1.0f - r / 255.0f) * (1.0f - arg1);
    arg0->g = g + fogGetCurrentEnvironmentp()->CloudGreen * (1.0f - g / 255.0f) * (1.0f - arg1);
    arg0->b = b + fogGetCurrentEnvironmentp()->CloudBlue  * (1.0f - b / 255.0f) * (1.0f - arg1);

    arg0->a = 0xff;
}

/*
* Address: 0x7F093FA4
*/
void skyChooseWaterVtxColour(SkyRelated18 *arg0, f32 arg1)
{
    f32 r = fogGetCurrentEnvironmentp()->Red;
    f32 g = fogGetCurrentEnvironmentp()->Green;
    f32 b = fogGetCurrentEnvironmentp()->Blue;
    u32 unused;

    arg0->r = r + fogGetCurrentEnvironmentp()->WaterRed   * (1.0f - r / 255.0f) * (1.0f - arg1);
    arg0->g = g + fogGetCurrentEnvironmentp()->WaterGreen * (1.0f - g / 255.0f) * (1.0f - arg1);
    arg0->b = b + fogGetCurrentEnvironmentp()->WaterBlue  * (1.0f - b / 255.0f) * (1.0f - arg1);
    arg0->a = 0xff;
}

/*
* Address: 0x7F094298
* Has to do with converting a float to a 32-bits value that's later read in two 16-bits parts
* Possibly converting floats to fixed point
*/
u32 sub_GAME_7F094298(f32 arg0)
{
    u32 result;

    if (arg0 > 32767.9f) { arg0 = 32767.9f; }
    if (arg0 < -32767.9f) { arg0 = -32767.9f; }

    if (arg0 < 0)
    {
        result = arg0 * -65536;
        result = -result;
    }
    else
    {
        result = 65536 * arg0;
    }

    return result;
}


void skySetStageNum(s32 stagenum) {
  g_SkyStageNum = stagenum;
}


/*
* Address: 0x7F094438
*/
void skyTick(void)
{
    #if defined(VERSION_EU)
    g_SkyCloudOffset += g_GlobalTimerDelta;
    #else
    g_SkyCloudOffset += g_ClockTimer;
    #endif
    if ( g_SkyCloudOffset > 4096.0f)
    {
        g_SkyCloudOffset -= 4096.0f;
    }
#ifndef TARGET_N64
    { /* Debug: GE_SKY_NOSCROLL=1 pins the cloud scroll phase so screenshot A/B
       * comparisons between runs compare geometry, not scroll offset. Capture
       * tool only. */
        extern char *getenv(const char *);
        static s32 noscroll = -1;
        if (noscroll < 0)
        {
            noscroll = getenv("GE_SKY_NOSCROLL") != NULL;
        }
        if (noscroll)
        {
            g_SkyCloudOffset = 0.0f;
        }
    }
#endif
}

Gfx* sub_GAME_7F09343C(Gfx*, s32);

Gfx *skyRender(Gfx *gdl)
{
    coord3d sp6a4;
    coord3d sp698;
    coord3d sp68c;
    coord3d sp680;
    coord3d sp674;
    coord3d sp668;
    coord3d sp65c;
    coord3d sp650;
    coord3d sp644;
    coord3d sp638;
    coord3d sp62c;
    coord3d sp620;
    coord3d sp614;
    coord3d sp608;
    coord3d sp5fc;
    coord3d sp5f0;
    coord3d sp5e4;
    coord3d sp5d8;
    coord3d sp5cc;
    coord3d sp5c0;
    coord3d sp5b4;
    coord3d sp5a8;
    coord3d sp59c;
    coord3d sp590;
    f32 sp58c;
    f32 sp588;
    f32 sp584;
    f32 sp580;
    f32 sp57c;
    f32 sp578;
    f32 sp574;
    f32 sp570;
    f32 sp56c;
    f32 sp568;
    f32 sp564;
    f32 sp560;
    f32 sp55c;
    f32 sp558;
    f32 sp554;
    f32 sp550;
    f32 sp54c;
    f32 sp548;
    s32 s1;
    s32 j;
    s32 k;
    s32 sp538;
    s32 sp534;
    s32 sp530;
    s32 sp52c;
    SkyRelated18 sp4b4[5];
    SkyRelated18 sp43c[5];
    f32 tmp;
    f32 scale;
    bool sp430;
    struct CurrentEnvironmentRecord *env;

    scale = get_room_data_float1() / 30.0f;
    sp430 = FALSE;
    env = fogGetCurrentEnvironmentp();

    if (!fogGetCurrentEnvironmentp()->Clouds)
    {
        if (getPlayerCount() == 1)
        {
            gDPSetCycleType(gdl++, G_CYC_FILL);

            gdl = viSetFillColor(gdl, env->Red, env->Green, env->Blue);

            gDPFillRectangle(gdl++, viGetViewLeft(), viGetViewTop(),
                    viGetViewLeft() + viGetViewWidth() - 1,
                    viGetViewTop() + viGetViewHeight() - 1);

            gDPPipeSync(gdl++);
            return gdl;
        }

        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_FILL);

        gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);

#ifndef TARGET_N64
        PORT_HUD_ANCHOR(gdl, PORT_HUD_ANCHOR_SPAN);   /* the player's backdrop: see the band below */
#endif
        gDPFillRectangle(gdl++,
                g_CurrentPlayer->viewleft, g_CurrentPlayer->viewtop,
                (g_CurrentPlayer->viewleft + g_CurrentPlayer->viewx) - 1,
                (g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy) - 1);
#ifndef TARGET_N64
        PORT_HUD_ANCHOR_OFF(gdl);
#endif

        gDPPipeSync(gdl++);
        return gdl;
    }

    gdl = viSetFillColor(gdl, env->Red, env->Green, env->Blue);

    if (&sp6a4);

    skyGetWorldPosFromScreenPos(0.0f, 0.0f, &sp6a4);
    skyGetWorldPosFromScreenPos(getPlayer_c_screenwidth() - 0.1f, 0.0f, &sp698);
    skyGetWorldPosFromScreenPos(0.0f, getPlayer_c_screenheight() - 0.1f, &sp68c);
    skyGetWorldPosFromScreenPos(getPlayer_c_screenwidth() - 0.1f, getPlayer_c_screenheight() - 0.1f, &sp680);

    sp538 = skyIsScreenCornerInSky(&sp6a4, &sp644, &sp58c);
    sp534 = skyIsScreenCornerInSky(&sp698, &sp638, &sp588);
    sp530 = skyIsScreenCornerInSky(&sp68c, &sp62c, &sp584);
    sp52c = skyIsScreenCornerInSky(&sp680, &sp620, &sp580);

    skyIsCornerInWater(&sp6a4, &sp5e4, &sp56c);
    skyIsCornerInWater(&sp698, &sp5d8, &sp568);
    skyIsCornerInWater(&sp68c, &sp5cc, &sp564);
    skyIsCornerInWater(&sp680, &sp5c0, &sp560);

#ifndef TARGET_N64
    { /* Debug: GE_SKY_PROBE=1 logs once a second the FOV, the world XZ where
       * the screen-centre ray hits the cloud plane (which must stay constant
       * while zooming with a still camera), and corner 0's hit. If the centre
       * hit holds while clouds visibly slide, the fault is in the renderer; if
       * it moves with FOV, it's in the game-side ray math. */
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        static int skyprobe = -1;
        static u32 skyprobecalls;
        if (skyprobe < 0)
        {
            skyprobe = getenv("GE_SKY_PROBE") != NULL;
        }
        if (skyprobe && (skyprobecalls++ % 61) == 0)
        {
            coord3d ctrdir;
            coord3d ctrpos;
            f32 ctrfrac;
            skyGetWorldPosFromScreenPos(getPlayer_c_screenwidth() * 0.5f,
                                        getPlayer_c_screenheight() * 0.5f, &ctrdir);
            if (skyIsScreenCornerInSky(&ctrdir, &ctrpos, &ctrfrac))
            {
                printf("skyprobe: fovy=%.3f ctr=(%.1f,%.1f) c0=(%.1f,%.1f) c0sky=%d scroll=%.1f\n",
                       viGetFovY(), ctrpos.f[0], ctrpos.f[2],
                       sp644.f[0], sp644.f[2], sp538, g_SkyCloudOffset);
            }
            else
            {
                printf("skyprobe: fovy=%.3f ctr=BELOW-HORIZON c0=(%.1f,%.1f) c0sky=%d\n",
                       viGetFovY(), sp644.f[0], sp644.f[2], sp538);
            }
        }
    }
#endif

    if (sp538 != sp530)
    {
        sp54c = getPlayer_c_screentop() + getPlayer_c_screenheight() * (sp6a4.f[1] / (sp6a4.f[1] - sp68c.f[1]));

        skyGetWorldPosFromScreenPos(0.0f, sp54c, &sp65c);
        skyCalculateEdgeVertex(&sp6a4, &sp68c, &sp65c);
        skyIsScreenCornerInSky(&sp65c, &sp5fc, &sp574);
        skyIsCornerInWater(&sp65c, &sp59c, &sp554);
    }
    else
    {
        sp54c = 0.0f;
    }

    if (sp534 != sp52c)
    {
        sp548 = getPlayer_c_screentop() + getPlayer_c_screenheight() * (sp698.f[1] / (sp698.f[1] - sp680.f[1]));

        skyGetWorldPosFromScreenPos(getPlayer_c_screenwidth() - 0.1f, sp548, &sp650);
        skyCalculateEdgeVertex(&sp698, &sp680, &sp650);
        skyIsScreenCornerInSky(&sp650, &sp5f0, &sp570);
        skyIsCornerInWater(&sp650, &sp590, &sp550);
    }
    else
    {
        sp548 = 0.0f;
    }

    if (sp538 != sp534)
    {
        skyGetWorldPosFromScreenPos(getPlayer_c_screenleft() + getPlayer_c_screenwidth() * (sp6a4.f[1] / (sp6a4.f[1] - sp698.f[1])), 0.0f, &sp674);
        skyCalculateEdgeVertex(&sp6a4, &sp698, &sp674);
        skyIsScreenCornerInSky(&sp674, &sp614, &sp57c);
        skyIsCornerInWater(&sp674, &sp5b4, &sp55c);
    }

    if (sp530 != sp52c)
    {
        tmp = getPlayer_c_screenleft() + getPlayer_c_screenwidth() * (sp68c.f[1] / (sp68c.f[1] - sp680.f[1]));

        skyGetWorldPosFromScreenPos(tmp, getPlayer_c_screenheight() - 0.1f, &sp668);
        skyCalculateEdgeVertex(&sp68c, &sp680, &sp668);
        skyIsScreenCornerInSky(&sp668, &sp608, &sp578);
        skyIsCornerInWater(&sp668, &sp5a8, &sp558);
    }

    switch ((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c)
    {
        case 15:
            s1 = 0;
            break;

        case 0:
            s1 = 4;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp5cc.f[0] * scale;
            sp43c[2].unk04 = sp5cc.f[1] * scale;
            sp43c[2].unk08 = sp5cc.f[2] * scale;
            sp43c[3].unk00 = sp5c0.f[0] * scale;
            sp43c[3].unk04 = sp5c0.f[1] * scale;
            sp43c[3].unk08 = sp5c0.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5cc.f[0];
            sp43c[2].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5c0.f[0];
            sp43c[3].unk10 = sp5c0.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp568);
            skyChooseWaterVtxColour(&sp43c[2], sp564);
            skyChooseWaterVtxColour(&sp43c[3], sp560);
            break;

        case 3:
            s1 = 4;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp59c.f[0] * scale;
            sp43c[2].unk04 = sp59c.f[1] * scale;
            sp43c[2].unk08 = sp59c.f[2] * scale;
            sp43c[3].unk00 = sp590.f[0] * scale;
            sp43c[3].unk04 = sp590.f[1] * scale;
            sp43c[3].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp59c.f[0];
            sp43c[2].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp590.f[0];
            sp43c[3].unk10 = sp590.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp568);
            skyChooseWaterVtxColour(&sp43c[2], sp554);
            skyChooseWaterVtxColour(&sp43c[3], sp550);
            break;

        case 12:
            s1 = 4;
            sp430 = TRUE;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5cc.f[0] * scale;
            sp43c[1].unk04 = sp5cc.f[1] * scale;
            sp43c[1].unk08 = sp5cc.f[2] * scale;
            sp43c[2].unk00 = sp590.f[0] * scale;
            sp43c[2].unk04 = sp590.f[1] * scale;
            sp43c[2].unk08 = sp590.f[2] * scale;
            sp43c[3].unk00 = sp59c.f[0] * scale;
            sp43c[3].unk04 = sp59c.f[1] * scale;
            sp43c[3].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5cc.f[0];
            sp43c[1].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp590.f[0];
            sp43c[2].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp59c.f[0];
            sp43c[3].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp560);
            skyChooseWaterVtxColour(&sp43c[1], sp564);
            skyChooseWaterVtxColour(&sp43c[2], sp550);
            skyChooseWaterVtxColour(&sp43c[3], sp554);
            break;

        case 10:
            s1 = 4;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp5c0.f[0] * scale;
            sp43c[1].unk04 = sp5c0.f[1] * scale;
            sp43c[1].unk08 = sp5c0.f[2] * scale;
            sp43c[2].unk00 = sp5b4.f[0] * scale;
            sp43c[2].unk04 = sp5b4.f[1] * scale;
            sp43c[2].unk08 = sp5b4.f[2] * scale;
            sp43c[3].unk00 = sp5a8.f[0] * scale;
            sp43c[3].unk04 = sp5a8.f[1] * scale;
            sp43c[3].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5c0.f[0];
            sp43c[1].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5b4.f[0];
            sp43c[2].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5a8.f[0];
            sp43c[3].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp568);
            skyChooseWaterVtxColour(&sp43c[1], sp560);
            skyChooseWaterVtxColour(&sp43c[2], sp55c);
            skyChooseWaterVtxColour(&sp43c[3], sp558);
            break;

        case 5:
            s1 = 4;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp5e4.f[0] * scale;
            sp43c[1].unk04 = sp5e4.f[1] * scale;
            sp43c[1].unk08 = sp5e4.f[2] * scale;
            sp43c[2].unk00 = sp5a8.f[0] * scale;
            sp43c[2].unk04 = sp5a8.f[1] * scale;
            sp43c[2].unk08 = sp5a8.f[2] * scale;
            sp43c[3].unk00 = sp5b4.f[0] * scale;
            sp43c[3].unk04 = sp5b4.f[1] * scale;
            sp43c[3].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5e4.f[0];
            sp43c[1].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5a8.f[0];
            sp43c[2].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5b4.f[0];
            sp43c[3].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp564);
            skyChooseWaterVtxColour(&sp43c[1], sp56c);
            skyChooseWaterVtxColour(&sp43c[2], sp558);
            skyChooseWaterVtxColour(&sp43c[3], sp55c);
            break;

        case 14:
            s1 = 3;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5a8.f[0] * scale;
            sp43c[1].unk04 = sp5a8.f[1] * scale;
            sp43c[1].unk08 = sp5a8.f[2] * scale;
            sp43c[2].unk00 = sp590.f[0] * scale;
            sp43c[2].unk04 = sp590.f[1] * scale;
            sp43c[2].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5a8.f[0];
            sp43c[1].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp590.f[0];
            sp43c[2].unk10 = sp590.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp560);
            skyChooseWaterVtxColour(&sp43c[1], sp558);
            skyChooseWaterVtxColour(&sp43c[2], sp550);
            break;

        case 13:
            s1 = 3;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp59c.f[0] * scale;
            sp43c[1].unk04 = sp59c.f[1] * scale;
            sp43c[1].unk08 = sp59c.f[2] * scale;
            sp43c[2].unk00 = sp5a8.f[0] * scale;
            sp43c[2].unk04 = sp5a8.f[1] * scale;
            sp43c[2].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp59c.f[0];
            sp43c[1].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5a8.f[0];
            sp43c[2].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp564);
            skyChooseWaterVtxColour(&sp43c[1], sp554);
            skyChooseWaterVtxColour(&sp43c[2], sp558);
            break;

        case 11:
            s1 = 3;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp590.f[0] * scale;
            sp43c[1].unk04 = sp590.f[1] * scale;
            sp43c[1].unk08 = sp590.f[2] * scale;
            sp43c[2].unk00 = sp5b4.f[0] * scale;
            sp43c[2].unk04 = sp5b4.f[1] * scale;
            sp43c[2].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp590.f[0];
            sp43c[1].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5b4.f[0];
            sp43c[2].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp568);
            skyChooseWaterVtxColour(&sp43c[1], sp550);
            skyChooseWaterVtxColour(&sp43c[2], sp55c);
            break;

        case 7:
            s1 = 3;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5b4.f[0] * scale;
            sp43c[1].unk04 = sp5b4.f[1] * scale;
            sp43c[1].unk08 = sp5b4.f[2] * scale;
            sp43c[2].unk00 = sp59c.f[0] * scale;
            sp43c[2].unk04 = sp59c.f[1] * scale;
            sp43c[2].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5b4.f[0];
            sp43c[1].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp59c.f[0];
            sp43c[2].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp55c);
            skyChooseWaterVtxColour(&sp43c[2], sp554);
            break;

        case 1:
            s1 = 5;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp5e4.f[0] * scale;
            sp43c[1].unk04 = sp5e4.f[1] * scale;
            sp43c[1].unk08 = sp5e4.f[2] * scale;
            sp43c[2].unk00 = sp5d8.f[0] * scale;
            sp43c[2].unk04 = sp5d8.f[1] * scale;
            sp43c[2].unk08 = sp5d8.f[2] * scale;
            sp43c[3].unk00 = sp590.f[0] * scale;
            sp43c[3].unk04 = sp590.f[1] * scale;
            sp43c[3].unk08 = sp590.f[2] * scale;
            sp43c[4].unk00 = sp5a8.f[0] * scale;
            sp43c[4].unk04 = sp5a8.f[1] * scale;
            sp43c[4].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5e4.f[0];
            sp43c[1].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5d8.f[0];
            sp43c[2].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp590.f[0];
            sp43c[3].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp5a8.f[0];
            sp43c[4].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp564);
            skyChooseWaterVtxColour(&sp43c[1], sp56c);
            skyChooseWaterVtxColour(&sp43c[2], sp568);
            skyChooseWaterVtxColour(&sp43c[3], sp550);
            skyChooseWaterVtxColour(&sp43c[4], sp558);
            break;

        case 2:
            s1 = 5;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp5c0.f[0] * scale;
            sp43c[2].unk04 = sp5c0.f[1] * scale;
            sp43c[2].unk08 = sp5c0.f[2] * scale;
            sp43c[3].unk00 = sp5a8.f[0] * scale;
            sp43c[3].unk04 = sp5a8.f[1] * scale;
            sp43c[3].unk08 = sp5a8.f[2] * scale;
            sp43c[4].unk00 = sp59c.f[0] * scale;
            sp43c[4].unk04 = sp59c.f[1] * scale;
            sp43c[4].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5c0.f[0];
            sp43c[2].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5a8.f[0];
            sp43c[3].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp59c.f[0];
            sp43c[4].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp568);
            skyChooseWaterVtxColour(&sp43c[2], sp560);
            skyChooseWaterVtxColour(&sp43c[3], sp558);
            skyChooseWaterVtxColour(&sp43c[4], sp554);
            break;

        case 4:
            s1 = 5;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5cc.f[0] * scale;
            sp43c[1].unk04 = sp5cc.f[1] * scale;
            sp43c[1].unk08 = sp5cc.f[2] * scale;
            sp43c[2].unk00 = sp5e4.f[0] * scale;
            sp43c[2].unk04 = sp5e4.f[1] * scale;
            sp43c[2].unk08 = sp5e4.f[2] * scale;
            sp43c[3].unk00 = sp5b4.f[0] * scale;
            sp43c[3].unk04 = sp5b4.f[1] * scale;
            sp43c[3].unk08 = sp5b4.f[2] * scale;
            sp43c[4].unk00 = sp590.f[0] * scale;
            sp43c[4].unk04 = sp590.f[1] * scale;
            sp43c[4].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5cc.f[0];
            sp43c[1].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5e4.f[0];
            sp43c[2].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5b4.f[0];
            sp43c[3].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp590.f[0];
            sp43c[4].unk10 = sp590.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp560);
            skyChooseWaterVtxColour(&sp43c[1], sp564);
            skyChooseWaterVtxColour(&sp43c[2], sp56c);
            skyChooseWaterVtxColour(&sp43c[3], sp55c);
            skyChooseWaterVtxColour(&sp43c[4], sp550);
            break;

        case 8:
            s1 = 5;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp5c0.f[0] * scale;
            sp43c[1].unk04 = sp5c0.f[1] * scale;
            sp43c[1].unk08 = sp5c0.f[2] * scale;
            sp43c[2].unk00 = sp5cc.f[0] * scale;
            sp43c[2].unk04 = sp5cc.f[1] * scale;
            sp43c[2].unk08 = sp5cc.f[2] * scale;
            sp43c[3].unk00 = sp59c.f[0] * scale;
            sp43c[3].unk04 = sp59c.f[1] * scale;
            sp43c[3].unk08 = sp59c.f[2] * scale;
            sp43c[4].unk00 = sp5b4.f[0] * scale;
            sp43c[4].unk04 = sp5b4.f[1] * scale;
            sp43c[4].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5c0.f[0];
            sp43c[1].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5cc.f[0];
            sp43c[2].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp59c.f[0];
            sp43c[3].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp5b4.f[0];
            sp43c[4].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp568);
            skyChooseWaterVtxColour(&sp43c[1], sp560);
            skyChooseWaterVtxColour(&sp43c[2], sp564);
            skyChooseWaterVtxColour(&sp43c[3], sp554);
            skyChooseWaterVtxColour(&sp43c[4], sp55c);
            break;

        default:
            return gdl;
    }

    if (s1 > 0)
    {
        Mtxf sp3cc;
        Mtxf sp38c;
        SkyRelated38 sp274[5];
        s32 i;
        s32 unused[3];

        matrix_4x4_multiply(currentPlayerGetProjectionMatrixF(), camGetWorldToScreenMtxf(), &sp3cc);
        guScaleF(dword_CODE_bss_80079E98.m, 1.0f / scale, 1.0f / scale, 1.0f / scale);
        matrix_4x4_multiply(&sp3cc, &dword_CODE_bss_80079E98, &sp38c);

        for (i = 0; i < s1; i++)
        {
            sub_GAME_7F097388(&sp43c[i], &sp38c, 130, 65535.0f, 65535.0f, &sp274[i]);

            sp274[i].unk28 = skyClampEdge(sp274[i].unk28, getPlayer_c_screenleft() * 4.0f, (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f);
            sp274[i].unk2c = skyClampEdge(sp274[i].unk2c, getPlayer_c_screentop() * 4.0f, (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f);

            if (sp274[i].unk2c > getPlayer_c_screentop() * 4.0f + 4.0f
                    && sp274[i].unk2c < (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 4.0f)
            {
                sp274[i].unk2c -= 4.0f;
            }
        }

        if (!fogGetCurrentEnvironmentp()->IsWater)
        {
            f32 f14 = 1279.0f;
            f32 f2 = 0.0f;
            f32 f16 = 959.0f;
            f32 f12 = 0.0f;

            for (j = 0; j < s1; j++)
            {
                if (sp274[j].unk28 < f14) { f14 = sp274[j].unk28; }
                if (sp274[j].unk28 > f2) { f2 = sp274[j].unk28; }

                if (sp274[j].unk2c < f16) { f16 = sp274[j].unk2c; }
                if (sp274[j].unk2c > f12) { f12 = sp274[j].unk2c; }
            }

            gDPPipeSync(gdl++);
            gDPSetCycleType(gdl++, G_CYC_FILL);
            gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);
            gDPSetTexturePersp(gdl++, G_TP_NONE);
#ifndef TARGET_N64
            {   /* Keep this FILL band inside the view rect: a FILL rect's
                 * lower-right edge is inclusive, so the exclusive view edge
                 * would paint one pixel past it. That pixel shows as a sky-blue
                 * line in the letterbox bars at high resolutions. */
                s32 fx0 = (s32) (f14 * 0.25f);
                s32 fy0 = (s32) (f16 * 0.25f);
                s32 fx1 = (s32) (f2 * 0.25f);
                s32 fy1 = (s32) (f12 * 0.25f);
                s32 lastx = (s32) (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) - 1;
                s32 lasty = (s32) (getPlayer_c_screentop() + getPlayer_c_screenheight()) - 1;

                if (fx1 > lastx) { fx1 = lastx; }
                if (fy1 > lasty) { fy1 = lasty; }

                /* SPAN: a backdrop, on the 3D's window mapping. In a 3P/4P
                 * frame it would otherwise be pinned into the quadrant's 4:3
                 * band, leaving black strips at its sides (gfx_dp_fill_rectangle) */
                PORT_HUD_ANCHOR(gdl, PORT_HUD_ANCHOR_SPAN);
                gDPFillRectangle(gdl++, fx0, fy0, fx1, fy1);
                PORT_HUD_ANCHOR_OFF(gdl);
            }
#else
            gDPFillRectangle(gdl++, (s32)(f14 * 0.25f), (s32)(f16 * 0.25f), (s32)(f2 * 0.25f), (s32)(f12 * 0.25f));
#endif
            gDPPipeSync(gdl++);
            gDPSetTexturePersp(gdl++, G_TP_PERSP);
        }
        else
        {
            gDPPipeSync(gdl++);

#ifndef TARGET_N64
            { /* diagnostic (GE_DUMP_BOND): what image does the water use? */
                extern char *getenv(const char *);
                static int logged;
                if (logged < 2 && getenv("GE_DUMP_BOND") != NULL)
                {
                    extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
                    sImageTableEntry *ent = &skywaterimages[fogGetCurrentEnvironmentp()->WaterImageId];
                    f32 v[6];
                    logged++;
                    v[0] = (f32)fogGetCurrentEnvironmentp()->WaterImageId;
                    v[1] = (f32)ent->index;
                    v[2] = (f32)ent->width;
                    v[3] = (f32)ent->height;
                    v[4] = (f32)ent->format;
                    v[5] = (f32)ent->depth;
                    ge_dbg_vec("waterimg id/idx/w/h/fmt/depth", v, 6);
                    if (ent->index >= 0x10000000u)
                    { /* promoted: index -> texel data; the pool RECORD holds
                       * the REAL format (texSelect reads it, not the entry) */
                        extern void ge_dbg_gfxdump(const char *tag, void *gdl, int count);
                        extern struct tex *texFindInPool(s32 texturenum, struct texpool *pool);
                        u16 texnum = ((u16 *)ent->index)[-4];
                        struct tex *rec = texFindInPool(texnum, NULL);
                        f32 w2[2];
                        w2[0] = (f32)texnum;
                        w2[1] = (f32)(uintptr_t)rec;
                        ge_dbg_vec("waterpool texnum/rec", w2, 2);
                        if (rec != NULL)
                        {
                            ge_dbg_gfxdump("waterrec", rec, 2);
                        }
                    }
                }
            }
#endif

            texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->WaterImageId], 1, 0, 2);
            gdl = sub_GAME_7F09343C(gdl, 0); // ???
            gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);

            if (s1 == 4)
            {
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[1], &sp274[3], 130.0f, TRUE);

                if (sp430)
                {
                    sp274[0].unk2c++;
                    sp274[1].unk2c++;
                    sp274[2].unk2c++;
                    sp274[3].unk2c++;
#ifndef TARGET_N64
                    /* The quarter-pixel nudge that separates the two halves of
                     * the water quad runs AFTER the clamp, so with the vertices
                     * now sitting on the exclusive bottom edge it walks one
                     * quarter pixel past it - and past the view rect is the
                     * letterbox bar. Harmless on the N64's numbers (it landed
                     * on the inclusive edge and the nudge took it to the
                     * exclusive one); re-cap it here. */
                    for (i = 0; i < 4; i++)
                    {
                        f32 bottom = (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f;

                        if (sp274[i].unk2c > bottom)
                        {
                            sp274[i].unk2c = bottom;
                        }
                    }
#endif
                }

                gdl = skyRenderTri(gdl, &sp274[3], &sp274[2], &sp274[0], 130.0f, TRUE);
            }
            else if (s1 == 5)
            {
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[1], &sp274[2], 130.0f, TRUE);
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[2], &sp274[3], 130.0f, TRUE);
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[3], &sp274[4], 130.0f, TRUE);
            }
            else if (s1 == 3)
            {
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[1], &sp274[2], 130.0f, TRUE);
            }
        }
    }

    switch ((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c)
    {
        case 0:
            return gdl;

        case 15:
            s1 = 4;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp62c.f[0] * scale;
            sp4b4[2].unk04 = sp62c.f[1] * scale;
            sp4b4[2].unk08 = sp62c.f[2] * scale;
            sp4b4[3].unk00 = sp620.f[0] * scale;
            sp4b4[3].unk04 = sp620.f[1] * scale;
            sp4b4[3].unk08 = sp620.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[2].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp620.f[0] * 0.1f;
            sp4b4[3].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp584);
            skyChooseCloudVtxColour(&sp4b4[3], sp580);
            break;

        case 12:
            s1 = 4;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp5fc.f[0] * scale;
            sp4b4[2].unk04 = sp5fc.f[1] * scale;
            sp4b4[2].unk08 = sp5fc.f[2] * scale;
            sp4b4[3].unk00 = sp5f0.f[0] * scale;
            sp4b4[3].unk04 = sp5f0.f[1] * scale;
            sp4b4[3].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp574);
            skyChooseCloudVtxColour(&sp4b4[3], sp570);
            break;

        case 3:
            s1 = 4;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp62c.f[0] * scale;
            sp4b4[1].unk04 = sp62c.f[1] * scale;
            sp4b4[1].unk08 = sp62c.f[2] * scale;
            sp4b4[2].unk00 = sp5f0.f[0] * scale;
            sp4b4[2].unk04 = sp5f0.f[1] * scale;
            sp4b4[2].unk08 = sp5f0.f[2] * scale;
            sp4b4[3].unk00 = sp5fc.f[0] * scale;
            sp4b4[3].unk04 = sp5fc.f[1] * scale;
            sp4b4[3].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[1].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp584);
            skyChooseCloudVtxColour(&sp4b4[2], sp570);
            skyChooseCloudVtxColour(&sp4b4[3], sp574);
            break;

        case 5:
            s1 = 4;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp620.f[0] * scale;
            sp4b4[1].unk04 = sp620.f[1] * scale;
            sp4b4[1].unk08 = sp620.f[2] * scale;
            sp4b4[2].unk00 = sp614.f[0] * scale;
            sp4b4[2].unk04 = sp614.f[1] * scale;
            sp4b4[2].unk08 = sp614.f[2] * scale;
            sp4b4[3].unk00 = sp608.f[0] * scale;
            sp4b4[3].unk04 = sp608.f[1] * scale;
            sp4b4[3].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp620.f[0] * 0.1f;
            sp4b4[1].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp614.f[0] * 0.1f;
            sp4b4[2].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp608.f[0] * 0.1f;
            sp4b4[3].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp580);
            skyChooseCloudVtxColour(&sp4b4[2], sp57c);
            skyChooseCloudVtxColour(&sp4b4[3], sp578);
            break;

        case 10:
            s1 = 4;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp644.f[0] * scale;
            sp4b4[1].unk04 = sp644.f[1] * scale;
            sp4b4[1].unk08 = sp644.f[2] * scale;
            sp4b4[2].unk00 = sp608.f[0] * scale;
            sp4b4[2].unk04 = sp608.f[1] * scale;
            sp4b4[2].unk08 = sp608.f[2] * scale;
            sp4b4[3].unk00 = sp614.f[0] * scale;
            sp4b4[3].unk04 = sp614.f[1] * scale;
            sp4b4[3].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp644.f[0] * 0.1f;
            sp4b4[1].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp608.f[0] * 0.1f;
            sp4b4[2].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp614.f[0] * 0.1f;
            sp4b4[3].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp58c);
            skyChooseCloudVtxColour(&sp4b4[2], sp578);
            skyChooseCloudVtxColour(&sp4b4[3], sp57c);
            break;

        case 1:
            s1 = 3;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp608.f[0] * scale;
            sp4b4[1].unk04 = sp608.f[1] * scale;
            sp4b4[1].unk08 = sp608.f[2] * scale;
            sp4b4[2].unk00 = sp5f0.f[0] * scale;
            sp4b4[2].unk04 = sp5f0.f[1] * scale;
            sp4b4[2].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp608.f[0] * 0.1f;
            sp4b4[1].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp578);
            skyChooseCloudVtxColour(&sp4b4[2], sp570);
            break;

        case 2:
            s1 = 3;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp5fc.f[0] * scale;
            sp4b4[1].unk04 = sp5fc.f[1] * scale;
            sp4b4[1].unk08 = sp5fc.f[2] * scale;
            sp4b4[2].unk00 = sp608.f[0] * scale;
            sp4b4[2].unk04 = sp608.f[1] * scale;
            sp4b4[2].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[1].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp608.f[0] * 0.1f;
            sp4b4[2].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp574);
            skyChooseCloudVtxColour(&sp4b4[2], sp578);
            break;

        case 4:
            s1 = 3;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp5f0.f[0] * scale;
            sp4b4[1].unk04 = sp5f0.f[1] * scale;
            sp4b4[1].unk08 = sp5f0.f[2] * scale;
            sp4b4[2].unk00 = sp614.f[0] * scale;
            sp4b4[2].unk04 = sp614.f[1] * scale;
            sp4b4[2].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[1].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp614.f[0] * 0.1f;
            sp4b4[2].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp570);
            skyChooseCloudVtxColour(&sp4b4[2], sp57c);
            break;

        case 8:
            s1 = 3;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp614.f[0] * scale;
            sp4b4[1].unk04 = sp614.f[1] * scale;
            sp4b4[1].unk08 = sp614.f[2] * scale;
            sp4b4[2].unk00 = sp5fc.f[0] * scale;
            sp4b4[2].unk04 = sp5fc.f[1] * scale;
            sp4b4[2].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp614.f[0] * 0.1f;
            sp4b4[1].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp57c);
            skyChooseCloudVtxColour(&sp4b4[2], sp574);
            break;

        case 14:
            s1 = 5;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp644.f[0] * scale;
            sp4b4[1].unk04 = sp644.f[1] * scale;
            sp4b4[1].unk08 = sp644.f[2] * scale;
            sp4b4[2].unk00 = sp638.f[0] * scale;
            sp4b4[2].unk04 = sp638.f[1] * scale;
            sp4b4[2].unk08 = sp638.f[2] * scale;
            sp4b4[3].unk00 = sp5f0.f[0] * scale;
            sp4b4[3].unk04 = sp5f0.f[1] * scale;
            sp4b4[3].unk08 = sp5f0.f[2] * scale;
            sp4b4[4].unk00 = sp608.f[0] * scale;
            sp4b4[4].unk04 = sp608.f[1] * scale;
            sp4b4[4].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp644.f[0] * 0.1f;
            sp4b4[1].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp638.f[0] * 0.1f;
            sp4b4[2].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp608.f[0] * 0.1f;
            sp4b4[4].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp58c);
            skyChooseCloudVtxColour(&sp4b4[2], sp588);
            skyChooseCloudVtxColour(&sp4b4[3], sp570);
            skyChooseCloudVtxColour(&sp4b4[4], sp578);
            break;

        case 13:
            s1 = 5;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp620.f[0] * scale;
            sp4b4[2].unk04 = sp620.f[1] * scale;
            sp4b4[2].unk08 = sp620.f[2] * scale;
            sp4b4[3].unk00 = sp608.f[0] * scale;
            sp4b4[3].unk04 = sp608.f[1] * scale;
            sp4b4[3].unk08 = sp608.f[2] * scale;
            sp4b4[4].unk00 = sp5fc.f[0] * scale;
            sp4b4[4].unk04 = sp5fc.f[1] * scale;
            sp4b4[4].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp620.f[0] * 0.1f;
            sp4b4[2].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp608.f[0] * 0.1f;
            sp4b4[3].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[4].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp580);
            skyChooseCloudVtxColour(&sp4b4[3], sp578);
            skyChooseCloudVtxColour(&sp4b4[4], sp574);
            break;

        case 11:
            s1 = 5;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp62c.f[0] * scale;
            sp4b4[1].unk04 = sp62c.f[1] * scale;
            sp4b4[1].unk08 = sp62c.f[2] * scale;
            sp4b4[2].unk00 = sp644.f[0] * scale;
            sp4b4[2].unk04 = sp644.f[1] * scale;
            sp4b4[2].unk08 = sp644.f[2] * scale;
            sp4b4[3].unk00 = sp614.f[0] * scale;
            sp4b4[3].unk04 = sp614.f[1] * scale;
            sp4b4[3].unk08 = sp614.f[2] * scale;
            sp4b4[4].unk00 = sp5f0.f[0] * scale;
            sp4b4[4].unk04 = sp5f0.f[1] * scale;
            sp4b4[4].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[1].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp644.f[0] * 0.1f;
            sp4b4[2].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp614.f[0] * 0.1f;
            sp4b4[3].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[4].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp584);
            skyChooseCloudVtxColour(&sp4b4[2], sp58c);
            skyChooseCloudVtxColour(&sp4b4[3], sp57c);
            skyChooseCloudVtxColour(&sp4b4[4], sp570);
            break;

        case 7:
            s1 = 5;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp620.f[0] * scale;
            sp4b4[1].unk04 = sp620.f[1] * scale;
            sp4b4[1].unk08 = sp620.f[2] * scale;
            sp4b4[2].unk00 = sp62c.f[0] * scale;
            sp4b4[2].unk04 = sp62c.f[1] * scale;
            sp4b4[2].unk08 = sp62c.f[2] * scale;
            sp4b4[3].unk00 = sp5fc.f[0] * scale;
            sp4b4[3].unk04 = sp5fc.f[1] * scale;
            sp4b4[3].unk08 = sp5fc.f[2] * scale;
            sp4b4[4].unk00 = sp614.f[0] * scale;
            sp4b4[4].unk04 = sp614.f[1] * scale;
            sp4b4[4].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp620.f[0] * 0.1f;
            sp4b4[1].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[2].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp614.f[0] * 0.1f;
            sp4b4[4].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp580);
            skyChooseCloudVtxColour(&sp4b4[2], sp584);
            skyChooseCloudVtxColour(&sp4b4[3], sp574);
            skyChooseCloudVtxColour(&sp4b4[4], sp57c);
            break;

        default:
            return gdl;
    }

    gDPPipeSync(gdl++);

    texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->SkyImageId], 1, 0, 2);

    if (1);

#ifndef TARGET_N64
    if (skyDbgColour())
    {
        gDPSetEnvColor(gdl++, 0, 0, 255, 0xff);
    }
    else
#endif
    gDPSetEnvColor(gdl++, fogGetCurrentEnvironmentp()->Red, fogGetCurrentEnvironmentp()->Green, fogGetCurrentEnvironmentp()->Blue, 0xff);
    gDPSetCombineLERP(gdl++,
            SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE,
            SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE);

    {
        Mtxf sp1ec;
        Mtxf sp1ac;
        SkyRelated38 sp94[5];
        s32 i;
        s32 stack[2];

        matrix_4x4_multiply(currentPlayerGetProjectionMatrixF(), camGetWorldToScreenMtxf(), &sp1ec);
        guScaleF(dword_CODE_bss_80079E98.m, 1.0f / scale, 1.0f / scale, 1.0f / scale);
        matrix_4x4_multiply(&sp1ec, &dword_CODE_bss_80079E98, &sp1ac);

        for (i = 0; i < s1; i++)
        {
            sub_GAME_7F097388(&sp4b4[i], &sp1ac, 130, 65535.0f, 65535.0f, &sp94[i]);

            sp94[i].unk28 = skyClampEdge(sp94[i].unk28, getPlayer_c_screenleft() * 4.0f, (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f);
            sp94[i].unk2c = skyClampEdge(sp94[i].unk2c, getPlayer_c_screentop() * 4.0f, (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f);
        }

#ifndef TARGET_N64
        { /* Debug: GE_SKY_PROBE=2 logs, once a second, three checks for the sky
           * rotating out of step with the camera (skyclose, skypivot, skyzen).
           * Each compares where the sky puts a cloud point with where the
           * camera sees it. */
            static int skyprobe2 = -1;
            static u32 probe2calls;
            if (skyprobe2 < 0)
            {
                char *e = getenv("GE_SKY_PROBE");
                skyprobe2 = (e != NULL && atoi(e) >= 2);
            }
            if (skyprobe2 && (probe2calls++ % 61) == 0)
            {
                f32 sw = getPlayer_c_screenwidth();
                f32 sh = getPlayer_c_screenheight();
                f32 sl = getPlayer_c_screenleft();
                f32 st = getPlayer_c_screentop();
                f32 ppx[3];
                f32 ppy[3];
                coord3d *eye = bondviewGetCurrentPlayersPosition();
                Mtxf *vm = currentPlayerGetViewToWorldMtxf();
                SkyRelated18 in18;
                SkyRelated38 out38;
                coord3d dir;
                coord3d hit;
                f32 frac;
                s32 k;

                ppx[0] = 0.0f;      ppy[0] = 0.0f;
                ppx[1] = sw - 0.1f; ppy[1] = 0.0f;
                ppx[2] = sw * 0.5f; ppy[2] = 0.0f;

                in18.unk0c = 0.0f;
                in18.unk10 = 0.0f;
                in18.r = in18.g = in18.b = in18.a = 0;

                printf("skyprobe2: fovy=%.2f fwd=(%.3f,%.3f) pivot-d=(%.2f,%.2f,%.2f)\n",
                       viGetFovY(), vm->m[2][0], vm->m[2][2],
                       vm->m[3][0] - eye->f[0], vm->m[3][1] - eye->f[1], vm->m[3][2] - eye->f[2]);

                for (k = 0; k < 3; k++)
                {
                    skyGetWorldPosFromScreenPos(ppx[k], ppy[k], &dir);
                    if (skyIsScreenCornerInSky(&dir, &hit, &frac))
                    {
                        in18.unk00 = hit.f[0] * scale;
                        in18.unk04 = hit.f[1] * scale;
                        in18.unk08 = hit.f[2] * scale;
                        sub_GAME_7F097388(&in18, &sp1ac, 130, 65535.0f, 65535.0f, &out38);
                        printf("skyclose: k=%d d=(%+.2f,%+.2f)px w=%.4f\n", k,
                               out38.unk28 * 0.25f - (sl + ppx[k]),
                               out38.unk2c * 0.25f - (st + ppy[k]),
                               out38.unk0c);
                    }
                    else
                    {
                        printf("skyclose: k=%d NOT-IN-SKY\n", k);
                    }
                }

                /* zenith: the plane point straight overhead */
                in18.unk00 = eye->f[0] * scale;
                in18.unk04 = fogGetCurrentEnvironmentp()->CloudRepeat * scale;
                in18.unk08 = eye->f[2] * scale;
                sub_GAME_7F097388(&in18, &sp1ac, 130, 65535.0f, 65535.0f, &out38);
                if (out38.unk0c > 0.0f)
                {
                    f32 zx = out38.unk28 * 0.25f - sl;
                    f32 zy = out38.unk2c * 0.25f - st;
                    if (zx > 0.0f && zx < sw && zy > 0.0f && zy < sh)
                    {
                        skyGetWorldPosFromScreenPos(zx, zy, &dir);
                        if (skyIsScreenCornerInSky(&dir, &hit, &frac))
                        {
                            printf("skyzen: scr=(%.1f,%.1f) track-d=(%+.1f,%+.1f)wu\n",
                                   zx, zy, hit.f[0] - eye->f[0], hit.f[2] - eye->f[2]);
                        }
                    }
                    else
                    {
                        printf("skyzen: OFFSCREEN (%.1f,%.1f)\n", zx, zy);
                    }
                }
                else
                {
                    printf("skyzen: BEHIND\n");
                }
            }
        }
#endif

#ifndef TARGET_N64
        { /* GE_ROOM_AUDIT jitter probe: each frame, project sky fan corner 0
           * through both the sky's float view-projection (sp1ec) and the
           * fixed-point camera matrix the world uses (field_10E0). Deltas
           * oscillating frame to frame under camera motion indicate a
           * sky-vs-world matrix mismatch. */
            extern s32 portRoomAuditEnabled(void);
            if (portRoomAuditEnabled()) {
                extern char *getenv(const char *);
                extern u32 portRoomAuditGetFrame(void);
                Mtxf fixedm;
                f32 wx = sp4b4[0].unk00, wy = sp4b4[0].unk04, wz = sp4b4[0].unk08;
                f32 fw, fx, fy, qw, qx, qy;
                Mtxf *fm = &sp1ec;

                sub_GAME_7F058E78((Mtxf *) get_BONDdata_field_10E0(), &fixedm);

                fx = wx * fm->m[0][0] + wy * fm->m[1][0] + wz * fm->m[2][0] + fm->m[3][0];
                fy = wx * fm->m[0][1] + wy * fm->m[1][1] + wz * fm->m[2][1] + fm->m[3][1];
                fw = wx * fm->m[0][3] + wy * fm->m[1][3] + wz * fm->m[2][3] + fm->m[3][3];

                /* the quantized matrix bakes the 1/scale guScaleF in via the
                 * world pipeline; apply the same scale composition as sp1ac */
                qx = wx * fixedm.m[0][0] + wy * fixedm.m[1][0] + wz * fixedm.m[2][0] + fixedm.m[3][0];
                qy = wx * fixedm.m[0][1] + wy * fixedm.m[1][1] + wz * fixedm.m[2][1] + fixedm.m[3][1];
                qw = wx * fixedm.m[0][3] + wy * fixedm.m[1][3] + wz * fixedm.m[2][3] + fixedm.m[3][3];

                if (fw != 0.0f && qw != 0.0f) {
                    coord3d *dbgeye = bondviewGetCurrentPlayersPosition();
                    fprintf(stderr, "skydbg f=%u tick=%d eye=(%.3f,%.3f) sky=(%.4f,%.4f) d=(%.5f,%.5f)\n",
                            portRoomAuditGetFrame(), (s32) g_ClockTimer,
                            dbgeye->x, dbgeye->z,
                            fx / fw, fy / fw,
                            fx / fw - qx / qw, fy / fw - qy / qw);
                }
            }
        }
#endif

        if (s1 == 4)
        {
            if (((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c) == 12)
            {
                if (sp548 < sp54c)
                {
                    if (sp94[3].unk2c >= sp94[1].unk2c + 4.0f)
                    {
                        sp94[0].unk28 = getPlayer_c_screenleft() * 4.0f;
                        sp94[0].unk2c = getPlayer_c_screentop() * 4.0f;
                        sp94[1].unk28 = SKY_EDGE_LAST((getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f);
                        sp94[1].unk2c = getPlayer_c_screentop() * 4.0f;
                        sp94[2].unk28 = getPlayer_c_screenleft() * 4.0f;
                        sp94[3].unk28 = SKY_EDGE_LAST((getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f);

                        gdl = skyRenderFull(gdl, &sp94[0], &sp94[1], &sp94[2], &sp94[3], 130.0f);
                    }
                    else
                    {
                        gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
                    }
                }
                else if (sp94[2].unk2c >= sp94[0].unk2c + 4.0f)
                {
                    sp94[0].unk28 = getPlayer_c_screenleft() * 4.0f;
                    sp94[0].unk2c = getPlayer_c_screentop() * 4.0f;
                    sp94[1].unk28 = SKY_EDGE_LAST((getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f);
                    sp94[1].unk2c = getPlayer_c_screentop() * 4.0f;
                    sp94[2].unk28 = getPlayer_c_screenleft() * 4.0f;
                    sp94[3].unk28 = SKY_EDGE_LAST((getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f);

                    gdl = skyRenderFull(gdl, &sp94[1], &sp94[0], &sp94[3], &sp94[2], 130.0f);
                }
                else
                {
                    gdl = skyRenderTri(gdl, &sp94[1], &sp94[0], &sp94[3], 130.0f, TRUE);
                }
            }
            else
            {
                gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[3], 130.0f, TRUE);
                gdl = skyRenderTri(gdl, &sp94[3], &sp94[2], &sp94[0], 130.0f, TRUE);
            }
        }
        else if (s1 == 5)
        {
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[2], &sp94[3], 130.0f, TRUE);
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[3], &sp94[4], 130.0f, TRUE);
        }
        else if (s1 == 3)
        {
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
        }
    }

    return gdl;
}


void sub_GAME_7F097388(SkyRelated18 *arg0, Mtxf *arg1, u16 arg2, f32 arg3, f32 arg4, SkyRelated38 *arg5)
{
    f32 sp68[4];
    f32 sp64;
    f32 sp60;
    f32 f22;
    f32 f0;
    f32 sp48[4];
    f32 sp38[4];
    f32 mult;

    mult = arg2 / 65536.0f;

    sp68[0] = (arg0->unk00 * arg1->m[0][0] + arg0->unk04 * arg1->m[1][0] + arg0->unk08 * arg1->m[2][0]) + arg1->m[3][0];
    sp68[1] = (arg0->unk00 * arg1->m[0][1] + arg0->unk04 * arg1->m[1][1] + arg0->unk08 * arg1->m[2][1]) + arg1->m[3][1];
    sp68[2] = (arg0->unk00 * arg1->m[0][2] + arg0->unk04 * arg1->m[1][2] + arg0->unk08 * arg1->m[2][2]) + arg1->m[3][2];
    sp68[3] = (arg0->unk00 * arg1->m[0][3] + arg0->unk04 * arg1->m[1][3] + arg0->unk08 * arg1->m[2][3]) + arg1->m[3][3];

    sp60 = arg0->unk0c * (arg3 / 65536.0f);
    sp64 = arg0->unk10 * (arg4 / 65536.0f);

    if (sp68[3] == 0.0f)
    {
        f22 = 32767.0f;
    }
    else
    {
        f22 = 1.0f / (sp68[3] * mult);
    }

    f0 = f22;
    if (f0 < 0.0f) { f0 = 32767.0f; }

    sp48[0] = sp68[0] * f0 * mult;
    sp48[1] = sp68[1] * f0 * mult;
    sp48[2] = sp68[2] * f0 * mult;
    sp48[3] = sp68[3] * f0 * mult;

    sp38[0] = sp48[0] * (getPlayer_c_screenwidth() * 2) + (getPlayer_c_screenwidth() * 2 + getPlayer_c_screenleft() * 4);
    sp38[1] = -sp48[1] * (getPlayer_c_screenheight() * 2) + (getPlayer_c_screenheight() * 2 + getPlayer_c_screentop() * 4);

    sp38[2] = sp48[2] * 511.0f + 511.0f;
    sp38[3] = sp48[3] * 0 + 0;

    sp38[0] = skyClamp(sp38[0], -4090.0f, 4090.0f);
    sp38[1] = skyClamp(sp38[1], -4090.0f, 4090.0f);
    sp38[2] = skyClamp(sp38[2], 0.0f, 32767.0f);
    sp38[3] = skyClamp(sp38[3], 0.0f, 32767.0f);

    arg5->unk00 = sp68[0];
    arg5->unk04 = sp68[1];
    arg5->unk08 = sp68[2];
    arg5->unk0c = sp68[3];
    arg5->unk20 = sp60;
    arg5->unk24 = sp64;
    arg5->unk28 = sp38[0];
#ifndef TARGET_N64
    /* Zoom lock, vertex half: must undo the same pixel offset the ray side
     * added, or the whole sky sits off the screen corners by the difference. */
    arg5->unk2c = sp38[1] - skyConcavityPixels() * 4.0f;
#else
    arg5->unk2c = sp38[1] - fogGetCurrentEnvironmentp()->WaterConcavity * 4.0f;
#endif
    arg5->unk30 = sp38[2];
    arg5->unk34 = f22;

    arg5->r = arg0->r;
    arg5->g = arg0->g;
    arg5->b = arg0->b;
    arg5->a = arg0->a;
}

/*
* Address: 0x7F0977B4
*/
bool skyVerticesAreTheSame(SkyRelated38 *arg0, SkyRelated38 *arg1)
{
    f32 f0;
    f32 f1;

    f0 = arg0->unk28 - arg1->unk28;
    f1 = arg0->unk2c - arg1->unk2c;
    return sqrtf((f0 * f0) + (f1 * f1)) < 1.0f ? TRUE : FALSE;
}

/*
* Address: 0x7F097818
*/
Gfx *skyRenderTri(Gfx *gdl, SkyRelated38 *arg1, SkyRelated38 *arg2, SkyRelated38 *arg3, f32 arg4, bool textured)
{
    SkyRelated38 *sp484;
    SkyRelated38 *sp480;
    SkyRelated38 *sp47c;
    s32 i;
    f32 sp474;
    f32 sp470;
    f32 sp46c;
    f32 sp468;
    f32 sp464;
    f32 sp460;

    f32 sp45c[1];
    f32 sp458[1];
    f32 sp454[1];
    f32 sp450[1];
    f32 sp44c[1];
    f32 sp448[1];

    f32 sp444;
    f32 sp440;

    f32 sp43c[1];
    f32 sp438[1];
    f32 sp434[1];
    f32 sp430[1];
    f32 sp42c[1];
    f32 sp428[1];
    f32 sp424[1];
    f32 sp420[1];

    f32 sp41c;
    f32 sp418;
    f32 sp414;
    f32 sp410;
    f32 sp40c;
    f32 sp408;
    f32 sp404;
    f32 sp400;

    f32 sp3fc[1];
    f32 sp3f8[1];
    f32 sp3f4[1];
    f32 sp3f0[1];
    f32 sp3ec[1];
    f32 sp3e8[1];
    f32 sp3e4[1];
    f32 sp3e0[1];

    f32 sp3dc[1];
    f32 sp3d8[1];
    f32 sp3d4[1];
    f32 sp3d0[1];
    f32 sp3cc[1];
    f32 sp3c8[1];
    f32 sp3c4[1];
    f32 sp3c0[1];

    f32 sp3bc[1];
    f32 sp3b8[1];
    f32 sp3b4[1];
    f32 sp3b0[1];
    f32 sp3ac[1];
    f32 sp3a8[1];
    f32 sp3a4[1];
    f32 sp3a0[1];
    f32 sp39c[1];
    f32 sp398[1];
    f32 sp394[1];
    f32 sp390[1];
    f32 sp38c[1];
    f32 sp388[1];
    f32 sp384[1];
    f32 sp380[1];
    f32 sp37c;
    f32 sp378;
    f32 sp374[1];
    f32 sp370[1];
    f32 sp36c[1];
    f32 sp368;
    f32 sp364[1];
    f32 sp360[1];
    f32 sp35c[1];
    f32 sp358[1];
    f32 sp354[1];
    f32 sp350[1];
    f32 sp34c[1];
    f32 sp348[1];
    f32 sp344[1];
    f32 sp340[1];
    f32 sp33c[1];
    f32 sp338[1];
    f32 sp334[1];
    f32 sp330[1];
    f32 sp310[8];
    f32 sp2f0[8];
    f32 sp2d0[8];
    f32 sp2b0[8];
    f32 sp290[8];
    f32 sp270[8];
    f32 sp250[8];
    f32 sp230[8];
    f32 sp210[8];
    f32 f2;
    f32 sp208[1];
    f32 sp204[1];
    f32 sp200[1];
    u32 stack[4];
    f32 sp1d0[8];
    f32 sp1b0[8];
    u32 stack2;
    f32 sp1a8[1];
    f32 sp1a4[1];
    f32 sp1a0[1];
    SkyRelated38 *swap1;
    SkyRelated38 *swap2;
    SkyRelated38 *swap3;
    f32 sp190[1];
    u32 stack3;

    if (skyVerticesAreTheSame(arg1, arg2) || skyVerticesAreTheSame(arg2, arg3) || skyVerticesAreTheSame(arg3, arg1))
    {
        return gdl;
    }

    sp378 = arg4 / 65536.0f;

    sp474 = arg2->unk28 - arg1->unk28;
    sp470 = arg2->unk2c - arg1->unk2c;
    sp46c = arg3->unk28 - arg1->unk28;
    sp468 = arg3->unk2c - arg1->unk2c;

    sp444 = ((sp46c * sp470) - (sp474 * sp468)) / 65536.0f;

    if (sp444 == 0.0f)
    {
        return gdl;
    }

    sp440 = 1.0f / sp444;

#ifndef TARGET_N64
    { /* Pass this triangle's exact float corners to the renderer before the
       * fixed-point RDP packing below quantizes them, which fixes sky jitter
       * (see gfx_pc gfx_rdp_triangle). The returned sequence number goes into a
       * G_NOOP marker ahead of the RDP stream to pair the command with its
       * entry. */
        extern u32 ge_sky_push(s32 n, const f32 *x, const f32 *y, const f32 *s, const f32 *t,
                               const f32 *iw, const f32 *r, const f32 *g, const f32 *b,
                               const f32 *a, f32 stmul);
        SkyRelated38 *pv[3];
        f32 px[3], py[3], ps_[3], pt_[3], piw[3], pr[3], pg[3], pb[3], pa[3];
        s32 pi;
        u32 seq;

        pv[0] = arg1; pv[1] = arg2; pv[2] = arg3;
        for (pi = 0; pi < 3; pi++) {
            px[pi] = pv[pi]->unk28 * 0.25f;
            py[pi] = pv[pi]->unk2c * 0.25f;
            ps_[pi] = pv[pi]->unk20;
            pt_[pi] = pv[pi]->unk24;
            piw[pi] = pv[pi]->unk34;
            pr[pi] = pv[pi]->r;
            pg[pi] = pv[pi]->g;
            pb[pi] = pv[pi]->b;
            pa[pi] = pv[pi]->a;
        }
        seq = ge_sky_push(3, px, py, ps_, pt_, piw, pr, pg, pb, pa, arg4 / 65536.0f);
        gImmp1(gdl++, G_NOOP, 0x53590000 | seq);
    }
#endif

    sp484 = arg1;
    sp480 = arg2;
    sp47c = arg3;

    if (sp480->unk2c < sp484->unk2c)
    {
        swap1 = sp480;
        sp480 = sp484;
        sp484 = swap1;

        sp444 *= -1.0f;
        sp440 *= -1.0f;
    }

    if (sp47c->unk2c < sp480->unk2c)
    {
        swap2 = sp480;
        sp480 = sp47c;
        sp47c = swap2;

        sp444 *= -1.0f;
        sp440 *= -1.0f;
    }

    if (sp480->unk2c < sp484->unk2c)
    {
        swap3 = sp480;
        sp480 = sp484;
        sp484 = swap3;

        sp444 *= -1.0f;
        sp440 *= -1.0f;
    }

    sp420[0] = sp480->unk28 * 0.25f;
    sp424[0] = 0.0f;
    sp428[0] = sp484->unk28 * 0.25f;
    sp42c[0] = 0.0f;
    sp430[0] = sp484->unk28 * 0.25f;
    sp434[0] = 0.0f;
    sp43c[0] = 0.0f;
    sp438[0] = 0.0f;

    sp448[0] = sp47c->unk28;
    sp44c[0] = sp47c->unk2c;
    sp450[0] = sp480->unk28;
    sp454[0] = sp480->unk2c;
    sp458[0] = sp484->unk28;
    sp45c[0] = sp484->unk2c;

    sp474 = sp450[0] - sp458[0];
    sp470 = sp454[0] - sp45c[0];
    sp46c = sp448[0] - sp458[0];
    sp468 = sp44c[0] - sp45c[0];
    sp464 = sp448[0] - sp450[0];
    sp460 = sp44c[0] - sp454[0];

    sp3fc[0] = 0.0f;
    sp3f8[0] = 0.0f;
    sp3dc[0] = 0.0f;
    sp3d8[0] = 0.0f;
    sp3a0[0] = 0.0f;
    sp3a8[0] = 0.0f;
    sp3b0[0] = 0.0f;
    sp3bc[0] = 0.0f;
    sp3b8[0] = 0.0f;
    sp380[0] = 0.0f;
    sp388[0] = 0.0f;
    sp390[0] = 0.0f;
    sp39c[0] = 0.0f;
    sp398[0] = 0.0f;

    sp3e0[0] = sp464 / 4.0f;
    sp3e4[0] = sp460 / 4.0f;
    sp3e8[0] = sp474 / 4.0f;
    sp3ec[0] = sp470 / 4.0f;
    sp3f0[0] = sp46c / 4.0f;
    sp3f4[0] = sp468 / 4.0f;

    sp3c0[0] = sp464 * 4.0f;
    sp3c4[0] = sp460 * 4.0f;
    sp3c8[0] = sp474 * 4.0f;
    sp3cc[0] = sp470 * 4.0f;
    sp3d0[0] = sp46c * 4.0f;
    sp3d4[0] = sp468 * 4.0f;

    sp3a4[0] = 4.0f / sp460;
    sp3ac[0] = 4.0f / sp470;
    sp3b4[0] = 4.0f / sp468;

    sp384[0] = sp464 / sp460;
    sp38c[0] = sp474 / sp470;
    sp394[0] = sp46c / sp468;

    sp384[0] = skyClamp(sp384[0], -1878.0f, 1877.0f);
    sp38c[0] = skyClamp(sp38c[0], -1878.0f, 1877.0f);
    sp394[0] = skyClamp(sp394[0], -1878.0f, 1877.0f);

    f2 = (sp484->unk2c * 0.25f);
    sp37c = f2 - (s32) f2;
    sp408 = sp428[0] - skyRound(sp38c[0] * 8192.0f) * (1.0f / 8192.0f) * sp37c;
    sp410 = sp430[0] - skyRound(sp394[0] * 8192.0f) * (1.0f / 8192.0f) * sp37c;

    gImmp1(gdl++, G_RDPHALF_1, (textured ? (G_TRI_SHADE_TXTR << 24) : (G_TRI_FILL << 24))
            | (sp444 < 0.0f ? 0x00800000 : 0)
            | (s32) sp47c->unk2c);
    gImmp1(gdl++, G_RDPHALF_CONT, (s32) sp480->unk2c << 16 | (s32) sp484->unk2c);

    gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(sp480->unk28 * 0.25f));
    gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp384[0]));

    gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(sp410));
    gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp394[0]));

    gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(sp408));
    gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp38c[0]));

    if (!textured)
    {
        return gdl;
    }

    sp36c[0] = sp484->unk0c * sp378;
    sp370[0] = sp480->unk0c * sp378;
    sp374[0] = sp47c->unk0c * sp378;

    sp368 = sp36c[0];

    if (sp370[0] < sp368) { sp368 = sp370[0]; }
    if (sp374[0] < sp368) { sp368 = sp374[0]; }

    sp368 *= 0.5f;

    sp35c[0] = sp484->unk34 * sp368;
    sp360[0] = sp480->unk34 * sp368;
    sp364[0] = sp47c->unk34 * sp368;

    sp338[0] = sp35c[0] * sp484->unk20;
    sp33c[0] = sp35c[0] * sp484->unk24;
    sp340[0] = sp35c[0] * 32767.0f;
    sp344[0] = sp360[0] * sp480->unk20;
    sp348[0] = sp360[0] * sp480->unk24;
    sp34c[0] = sp360[0] * 32767.0f;
    sp350[0] = sp364[0] * sp47c->unk20;
    sp354[0] = sp364[0] * sp47c->unk24;
    sp358[0] = sp364[0] * 32767.0f;

    sp330[0] = SKYABS(sp338[0]);
    sp334[0] = SKYABS(sp33c[0]);

    if (sp330[0] < SKYABS(sp344[0])) { sp330[0] = SKYABS(sp344[0]); }
    if (sp334[0] < SKYABS(sp348[0])) { sp334[0] = SKYABS(sp348[0]); }
    if (sp330[0] < SKYABS(sp350[0])) { sp330[0] = SKYABS(sp350[0]); }
    if (sp334[0] < SKYABS(sp354[0])) { sp334[0] = SKYABS(sp354[0]); }

    sp310[0] = sp484->r + 0.5f;
    sp310[1] = sp484->g + 0.5f;
    sp310[2] = sp484->b + 0.5f;
    sp310[3] = sp484->a + 0.5f;

    sp2f0[0] = sp480->r + 0.5f;
    sp2f0[1] = sp480->g + 0.5f;
    sp2f0[2] = sp480->b + 0.5f;
    sp2f0[3] = sp480->a + 0.5f;

    sp2d0[0] = sp47c->r + 0.5f;
    sp2d0[1] = sp47c->g + 0.5f;
    sp2d0[2] = sp47c->b + 0.5f;
    sp2d0[3] = sp47c->a + 0.5f;

    sp310[4] = sp338[0]; sp310[5] = sp33c[0]; sp310[6] = sp340[0];
    sp2f0[4] = sp344[0]; sp2f0[5] = sp348[0]; sp2f0[6] = sp34c[0];
    sp2d0[4] = sp350[0]; sp2d0[5] = sp354[0]; sp2d0[6] = sp358[0];

    sp310[7] = sp484->unk30;
    sp2f0[7] = sp480->unk30;
    sp2d0[7] = sp47c->unk30;

    for (i = 0; i < 8; i++)
    {
        sp2b0[i] = sp2f0[i] - sp310[i];
        sp290[i] = sp2d0[i] - sp310[i];
    }

    for (i = 0; i < 8; i++)
    {
        sp250[i] = (sp290[i] * sp3cc[0] - sp2b0[i] * sp3d4[0]) / 65536.0f;
        sp270[i] = (sp2b0[i] * sp3d0[0] - sp290[i] * sp3c8[0]) / 65536.0f;
        sp290[i] = sp250[i] * sp440;
        sp2b0[i] = sp270[i] * sp440;
        sp230[i] = sp2b0[i] + sp290[i] * sp394[0];
        sp210[i] = sp310[i] - sp230[i] * sp37c;
    }

    {
        u32 sp168;
        u32 sp164;
        u32 sp160;
        u32 sp15c;
        u32 sp158;
        u32 sp154;
        u32 sp150;
        u32 sp14c;
        u32 sp148;
        u32 sp144;
        u32 sp140;
        u32 sp13c;
        u32 sp138;
        u32 sp134;
        u32 sp130;
        u32 sp12c;

        sp168 = sub_GAME_7F094298(sp210[0]);
        sp164 = sub_GAME_7F094298(sp210[1]);
        sp160 = sub_GAME_7F094298(sp210[2]);
        sp15c = sub_GAME_7F094298(sp210[3]);

        sp158 = sub_GAME_7F094298(sp290[0]);
        sp154 = sub_GAME_7F094298(sp290[1]);
        sp150 = sub_GAME_7F094298(sp290[2]);
        sp14c = sub_GAME_7F094298(sp290[3]);

        sp138 = sub_GAME_7F094298(sp2b0[0]);
        sp134 = sub_GAME_7F094298(sp2b0[1]);
        sp130 = sub_GAME_7F094298(sp2b0[2]);
        sp12c = sub_GAME_7F094298(sp2b0[3]);

        sp148 = sub_GAME_7F094298(sp230[0]);
        sp144 = sub_GAME_7F094298(sp230[1]);
        sp140 = sub_GAME_7F094298(sp230[2]);
        sp13c = sub_GAME_7F094298(sp230[3]);

        gImmp1(gdl++, G_RDPHALF_1, (sp168 & 0xffff0000) | (sp164 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp160 & 0xffff0000) | (sp15c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp158 & 0xffff0000) | (sp154 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp150 & 0xffff0000) | (sp14c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp168 & 0x0000ffff) << 16 | (sp164 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp160 & 0x0000ffff) << 16 | (sp15c & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp158 & 0x0000ffff) << 16 | (sp154 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp150 & 0x0000ffff) << 16 | (sp14c & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp148 & 0xffff0000) | (sp144 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp140 & 0xffff0000) | (sp13c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp138 & 0xffff0000) | (sp134 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp130 & 0xffff0000) | (sp12c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp148 & 0x0000ffff) << 16 | (sp144 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp140 & 0x0000ffff) << 16 | (sp13c & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp138 & 0x0000ffff) << 16 | (sp134 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp130 & 0x0000ffff) << 16 | (sp12c & 0x0000ffff));
    }

    sp200[0] = sp330[0] / 32.0f;
    sp204[0] = sp334[0] / 32.0f;
    sp208[0] = sp368 / 32.0f;

    for (i = 0; i < 8; i++)
    {
        sp1d0[i] = SKYABS(sp290[i]) / 32.0f;
        sp1b0[i] = SKYABS(sp2b0[i]) / 32.0f;
    }

    sp1a0[0] = sp200[0] + (2 * sp1d0[4]) + sp1b0[4];
    sp1a4[0] = sp204[0] + (2 * sp1d0[5]) + sp1b0[5];
    sp1a8[0] = sp208[0] + (2 * sp1d0[6]) + sp1b0[6];

    if (sp1a0[0] < sp1a4[0]) { sp1a0[0] = sp1a4[0]; }
    if (sp1a0[0] < sp1a8[0]) { sp1a0[0] = sp1a8[0]; }

    sp1a0[0] /= 1024.0f;

    if (sp1a0[0] > 1.0f)
    {
        sp190[0] = 1.0f / sp1a0[0];
    }
    else
    {
        sp190[0] = 1.0f;
    }

    {
        u32 spe8;
        u32 spe4;
        u32 spe0;
        u32 spdc;
        u32 spd8;
        u32 spd4;
        u32 spd0;
        u32 spcc;
        u32 spc8;
        u32 spc4;
        u32 spc0;
        u32 spbc;
        u32 spb8;
        u32 spb4;
        u32 spb0;
        u32 spac;

        spe8 = sub_GAME_7F094298(sp210[4] * sp190[0]);
        spe4 = sub_GAME_7F094298(sp210[5] * sp190[0]);
        spe0 = sub_GAME_7F094298(sp210[6] * sp190[0]);
        spdc = 0;

        spd8 = sub_GAME_7F094298(sp290[4] * sp190[0]);
        spd4 = sub_GAME_7F094298(sp290[5] * sp190[0]);
        spd0 = sub_GAME_7F094298(sp290[6] * sp190[0]);
        spcc = 0;

        spb8 = sub_GAME_7F094298(sp2b0[4] * sp190[0]);
        spb4 = sub_GAME_7F094298(sp2b0[5] * sp190[0]);
        spb0 = sub_GAME_7F094298(sp2b0[6] * sp190[0]);
        spac = 0;

        spc8 = sub_GAME_7F094298(sp230[4] * sp190[0]);
        spc4 = sub_GAME_7F094298(sp230[5] * sp190[0]);
        spc0 = sub_GAME_7F094298(sp230[6] * sp190[0]);
        spbc = 0;

        gImmp1(gdl++, G_RDPHALF_1, (spe8 & 0xffff0000) | (spe4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spe0 & 0xffff0000) | (spdc & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spd8 & 0xffff0000) | (spd4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spd0 & 0xffff0000) | (spcc & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spe8 & 0x0000ffff) << 16 | (spe4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spe0 & 0x0000ffff) << 16 | (spdc & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spd8 & 0x0000ffff) << 16 | (spd4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spd0 & 0x0000ffff) << 16 | (spcc & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spc8 & 0xffff0000) | (spc4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spc0 & 0xffff0000) | (spbc & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spb8 & 0xffff0000) | (spb4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spb0 & 0xffff0000) | (spac & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spc8 & 0x0000ffff) << 16 | (spc4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spc0 & 0x0000ffff) << 16 | (spbc & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spb8 & 0x0000ffff) << 16 | (spb4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_2, (spb0 & 0x0000ffff) << 16 | (spac & 0x0000ffff));
    }

    return gdl;
}

/*
* Address: 0x7F098A2C
*/
Gfx *skyRenderFull(Gfx *gdl, SkyRelated38 *arg1, SkyRelated38 *arg2, SkyRelated38 *arg3, SkyRelated38 *arg4, f32 arg5)
{
    SkyRelated38 *sp4cc;
    SkyRelated38 *sp4c8;
    SkyRelated38 *sp4c4;
    s32 i;
    u32 stack;
    f32 sp4b8;
    f32 sp4b4;
    f32 sp4b0;
    f32 sp4ac;
    f32 sp4a8;
    f32 sp4a4;
    f32 sp4a0[1];
    f32 sp49c[1];
    f32 sp498[1];
    f32 sp494[1];
    f32 sp490[1];
    f32 sp48c[1];
    f32 sp488;
    f32 sp484;
    f32 sp480[1];
    f32 sp47c[1];
    f32 sp478[1];
    f32 sp474[1];
    f32 sp470[1];
    f32 sp46c[1];
    f32 sp468[1];
    f32 sp464[1];
    SkyRelated38 *swap1;
    SkyRelated38 *swap2;
    SkyRelated38 *swap3;
    f32 sp454[1];
    u32 stack07;
    f32 sp44c[1];
    u32 stack08;
    u32 stack09;
    f32 sp440[1];
    f32 sp43c[1];
    f32 sp438[1];
    f32 sp434[1];
    f32 sp430[1];
    f32 sp42c[1];
    f32 sp428[1];
    f32 sp424[1];
    f32 sp420[1];
    f32 sp41c[1];
    f32 sp418[1];
    f32 sp414[1];
    f32 sp410[1];
    f32 sp40c[1];
    f32 sp408[1];
    f32 sp404[1];
    f32 sp400[1];
    f32 sp3fc[1];
    f32 sp3f8[1];
    f32 sp3f4[1];
    f32 sp3f0[1];
    f32 sp3ec[1];
    f32 sp3e8[1];
    f32 sp3e4[1];
    f32 sp3e0[1];
    f32 sp3dc[1];
    f32 sp3d8[1];
    f32 sp3d4[1];
    f32 sp3d0[1];
    f32 sp3cc[1];
    f32 sp3c8[1];
    f32 sp3c4[1];
    f32 sp3c0;
    f32 sp3bc[1];
    f32 sp3b8[1];
    f32 sp3b4[1];
    f32 sp3b0[1];
    f32 sp3ac;
    f32 sp3a8[1];
    f32 sp3a4[1];
    f32 sp3a0[1];
    f32 sp39c[1];
    f32 sp398[1];
    f32 sp394[1];
    f32 sp390[1];
    f32 sp38c[1];
    f32 sp388[1];
    f32 sp384[1];
    f32 sp380[1];
    f32 sp37c[1];
    f32 sp378[1];
    f32 sp374[1];
    f32 sp370[1];
    f32 sp36c[1];
    f32 sp368[1];
    f32 sp364[1];
    f32 sp354[4];
    f32 sp334[8];
    f32 sp314[8];
    f32 sp2f4[8];
    f32 sp2d4[8];
    f32 sp2b4[8];
    f32 sp294[8];
    f32 sp274[8];
    f32 sp254[8];
    u32 stack10;
    u32 stack11;
    u32 stack12;
    u32 stack13;
    u32 stack00;
    f32 sp23c[1];
    f32 sp238[1];
    f32 sp234[1];
    f32 sp214[8];
    f32 sp1f4[8];
    u32 stack03;
    u32 stack04;
    u32 stack05;
    u32 stack06;
    u32 stack15;
    f32 sp1dc[1];
    f32 sp1d8[1];
    f32 sp1d4[1];
    u32 stack01;
    u32 stack02;
    u32 stack14;
    f32 sp1c4[1];
    u32 stack16;

    if (skyVerticesAreTheSame(arg1, arg2)
            || skyVerticesAreTheSame(arg2, arg3)
            || skyVerticesAreTheSame(arg3, arg1)
            || skyVerticesAreTheSame(arg4, arg1)
            || skyVerticesAreTheSame(arg4, arg2)
            || skyVerticesAreTheSame(arg4, arg3))
    {
        return gdl;
    }

    sp3c0 = arg5 / 65536.0f;

#ifndef TARGET_N64
    { /* float sidechannel, trapezoid variant - see skyRenderTri. The fan
       * in the decoder walks the perimeter, so push TL,TR,BR,BL (args
       * arrive TL,TR,BL,BR from the caller). */
        extern u32 ge_sky_push(s32 n, const f32 *x, const f32 *y, const f32 *s, const f32 *t,
                               const f32 *iw, const f32 *r, const f32 *g, const f32 *b,
                               const f32 *a, f32 stmul);
        SkyRelated38 *pv[4];
        f32 px[4], py[4], ps_[4], pt_[4], piw[4], pr[4], pg[4], pb[4], pa[4];
        s32 pi;
        u32 seq;

        pv[0] = arg1; pv[1] = arg2; pv[2] = arg4; pv[3] = arg3;
        for (pi = 0; pi < 4; pi++) {
            px[pi] = pv[pi]->unk28 * 0.25f;
            py[pi] = pv[pi]->unk2c * 0.25f;
            ps_[pi] = pv[pi]->unk20;
            pt_[pi] = pv[pi]->unk24;
            piw[pi] = pv[pi]->unk34;
            pr[pi] = pv[pi]->r;
            pg[pi] = pv[pi]->g;
            pb[pi] = pv[pi]->b;
            pa[pi] = pv[pi]->a;
        }
        seq = ge_sky_push(4, px, py, ps_, pt_, piw, pr, pg, pb, pa, arg5 / 65536.0f);
        gImmp1(gdl++, G_NOOP, 0x53590000 | seq);
    }
#endif

    sp4b8 = arg2->unk28 - arg1->unk28;
    sp4b4 = arg2->unk2c - arg1->unk2c;
    sp4b0 = arg3->unk28 - arg1->unk28;
    sp4ac = arg3->unk2c - arg1->unk2c;

    sp488 = ((sp4b0 * sp4b4) - (sp4b8 * sp4ac)) / 65536.0f;

    sp484 = 1.0f / sp488;

    sp4cc = arg1;
    sp4c8 = arg2;
    sp4c4 = arg3;

    if (sp4c8->unk2c < sp4cc->unk2c)
    {
        swap1 = sp4c8;
        sp4c8 = sp4cc;
        sp4cc = swap1;

        sp488 *= -1.0f;
        sp484 *= -1.0f;
    }

    if (sp4c4->unk2c < sp4c8->unk2c)
    {
        swap2 = sp4c8;
        sp4c8 = sp4c4;
        sp4c4 = swap2;

        sp488 *= -1.0f;
        sp484 *= -1.0f;
    }

    if (sp4c8->unk2c < sp4cc->unk2c)
    {
        swap3 = sp4c8;
        sp4c8 = sp4cc;
        sp4cc = swap3;

        sp488 *= -1.0f;
        sp484 *= -1.0f;
    }

    sp464[0] = sp4c8->unk28 * 0.25f;
    sp468[0] = 0.0f;
    sp46c[0] = sp4cc->unk28 * 0.25f;
    sp470[0] = 0.0f;
    sp474[0] = sp4cc->unk28 * 0.25f;
    sp478[0] = 0.0f;
    sp480[0] = 0.0f;
    sp47c[0] = 0.0f;

    sp48c[0] = sp4c4->unk28;
    sp490[0] = sp4c4->unk2c;
    sp494[0] = sp4c8->unk28;
    sp498[0] = sp4c8->unk2c;
    sp49c[0] = sp4cc->unk28;
    sp4a0[0] = sp4cc->unk2c;

    sp4b8 = sp494[0] - sp49c[0];
    sp4b4 = sp498[0] - sp4a0[0];
    sp4b0 = sp48c[0] - sp49c[0];
    sp4ac = sp490[0] - sp4a0[0];
    sp4a8 = sp48c[0] - sp494[0];
    sp4a4 = sp490[0] - sp498[0];

    sp440[0] = 0.0f;
    sp43c[0] = 0.0f;
    sp420[0] = 0.0f;
    sp41c[0] = 0.0f;
    sp3e4[0] = 0.0f;
    sp3ec[0] = 0.0f;
    sp3f4[0] = 0.0f;
    sp400[0] = 0.0f;
    sp3fc[0] = 0.0f;
    sp3c4[0] = 0.0f;
    sp3cc[0] = 0.0f;
    sp3d4[0] = 0.0f;
    sp3e0[0] = 0.0f;
    sp3dc[0] = 0.0f;

    sp424[0] = sp4a8 / 4.0f;
    sp428[0] = sp4a4 / 4.0f;
    sp42c[0] = sp4b8 / 4.0f;
    sp430[0] = sp4b4 / 4.0f;
    sp434[0] = sp4b0 / 4.0f;
    sp438[0] = sp4ac / 4.0f;

    sp404[0] = sp4a8 * 4.0f;
    sp408[0] = sp4a4 * 4.0f;
    sp40c[0] = sp4b8 * 4.0f;
    sp410[0] = sp4b4 * 4.0f;
    sp414[0] = sp4b0 * 4.0f;
    sp418[0] = sp4ac * 4.0f;

    sp3e8[0] = 4.0f / sp4a4;
    sp3f0[0] = 4.0f / sp4b4;
    sp3f8[0] = 4.0f / sp4ac;

    sp3c8[0] = sp4a8 / sp4a4;
    sp3d0[0] = sp4b8 / sp4b4;
    sp3d8[0] = sp4b0 / sp4ac;

    sp3c8[0] = skyClamp(sp3c8[0], -1878.0f, 1877.0f);
    sp3d0[0] = skyClamp(sp3d0[0], -1878.0f, 1877.0f);
    sp3d8[0] = skyClamp(sp3d8[0], -1878.0f, 1877.0f);

    sp44c[0] = sp46c[0];
    sp454[0] = sp474[0];

    if (arg1->unk28 < arg2->unk28)
    {
        f32 sp1bc;

        if (arg3->unk2c - arg4->unk2c < 1.0f)
        {
            sp1bc = -1878.0f;
        }
        else
        {
            sp1bc = -(getPlayer_c_screenwidth() - SKY_EDGE_LAST_PX) / ((arg3->unk2c - arg4->unk2c) / 4.0f);
        }

        gImmp1(gdl++, G_RDPHALF_1, (G_TRI_SHADE_TXTR << 24) | 0x00800000 | (s32) arg3->unk2c);
        gImmp1(gdl++, G_RDPHALF_CONT, (s32) arg4->unk2c << 16 | (s32) arg1->unk2c);

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft() + getPlayer_c_screenwidth() - SKY_EDGE_LAST_PX));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp1bc));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft()));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft() + getPlayer_c_screenwidth() - SKY_EDGE_LAST_PX));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));
    }
    else
    {
        f32 sp198;

        if (arg3->unk2c - arg4->unk2c < 1.0f)
        {
            sp198 = 1877.0f;
        }
        else
        {
            sp198 = (getPlayer_c_screenwidth() - SKY_EDGE_LAST_PX) / ((arg3->unk2c - arg4->unk2c) / 4.0f);
        }

        gImmp1(gdl++, G_RDPHALF_1, 0xce000000 | (s32) arg3->unk2c);
        gImmp1(gdl++, G_RDPHALF_CONT, (s32) arg4->unk2c << 16 | (s32) arg1->unk2c);

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft()));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp198));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft() + getPlayer_c_screenwidth() - SKY_EDGE_LAST_PX));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft()));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));
    }

    sp3b0[0] = sp4cc->unk0c * sp3c0;
    sp3b4[0] = sp4c8->unk0c * sp3c0;
    sp3b8[0] = sp4c4->unk0c * sp3c0;
    sp3bc[0] = arg4->unk0c * sp3c0;

    sp3ac = sp3b0[0];

    if (sp3b4[0] < sp3ac) { sp3ac = sp3b4[0]; }
    if (sp3b8[0] < sp3ac) { sp3ac = sp3b8[0]; }
    if (sp3bc[0] < sp3ac) { sp3ac = sp3bc[0]; }

    sp3ac *= 0.5f;

    sp39c[0] = sp4cc->unk34 * sp3ac;
    sp3a0[0] = sp4c8->unk34 * sp3ac;
    sp3a4[0] = sp4c4->unk34 * sp3ac;
    sp3a8[0] = arg4->unk34 * sp3ac;

    sp36c[0] = sp39c[0] * sp4cc->unk20;
    sp370[0] = sp39c[0] * sp4cc->unk24;
    sp374[0] = sp39c[0] * 32767.0f;
    sp378[0] = sp3a0[0] * sp4c8->unk20;
    sp37c[0] = sp3a0[0] * sp4c8->unk24;
    sp380[0] = sp3a0[0] * 32767.0f;
    sp384[0] = sp3a4[0] * sp4c4->unk20;
    sp388[0] = sp3a4[0] * sp4c4->unk24;
    sp38c[0] = sp3a4[0] * 32767.0f;
    sp390[0] = sp3a8[0] * arg4->unk20;
    sp394[0] = sp3a8[0] * arg4->unk24;
    sp398[0] = sp3a8[0] * 32767.0f;

    sp364[0] = SKYABS(sp36c[0]);
    sp368[0] = SKYABS(sp370[0]);

    if (sp364[0] < SKYABS(sp378[0])) { sp364[0] = SKYABS(sp378[0]); }
    if (sp368[0] < SKYABS(sp37c[0])) { sp368[0] = SKYABS(sp37c[0]); }
    if (sp364[0] < SKYABS(sp384[0])) { sp364[0] = SKYABS(sp384[0]); }
    if (sp368[0] < SKYABS(sp388[0])) { sp368[0] = SKYABS(sp388[0]); }
    if (sp364[0] < SKYABS(sp390[0])) { sp364[0] = SKYABS(sp390[0]); }
    if (sp368[0] < SKYABS(sp394[0])) { sp368[0] = SKYABS(sp394[0]); }

    sp354[0] = sp36c[0]; sp354[1] = sp370[0]; sp354[2] = sp374[0];
    sp334[0] = sp378[0]; sp334[1] = sp37c[0]; sp334[2] = sp380[0];
    sp314[0] = sp384[0]; sp314[1] = sp388[0]; sp314[2] = sp38c[0];

    sp354[3] = sp4cc->unk30;
    sp334[3] = sp4c8->unk30;
    sp314[3] = sp4c4->unk30;

    for (i = 0; i < 4; i++)
    {
        sp2f4[i] = sp334[i] - sp354[i];
        sp2d4[i] = sp314[i] - sp354[i];
    }

    for (i = 0; i < 4; i++)
    {
        sp294[i] = ((sp2d4[i] * sp410[0]) - (sp2f4[i] * sp418[0])) / 65536.0f;
        sp2b4[i] = ((sp2f4[i] * sp414[0]) - (sp2d4[i] * sp40c[0])) / 65536.0f;
        sp2d4[i] = sp294[i] * sp484;
        sp2f4[i] = sp2b4[i] * sp484;
        sp274[i] = sp2b4[i] * sp484;
        sp254[i] = sp354[i];
    }

    {
        f32 mult = arg4->unk2c / arg3->unk2c;

        f32 sp170 = arg4->r + ((arg1->r - arg3->r) * mult);
        f32 sp16c = arg4->g + ((arg1->g - arg3->g) * mult);
        f32 sp168 = arg4->b + ((arg1->b - arg3->b) * mult);
        f32 sp164 = arg4->a + ((arg1->a - arg3->a) * mult);

        u32 sp160 = arg1->r * 65536.0f;
        u32 sp15c = arg1->g * 65536.0f;
        u32 sp158 = arg1->b * 65536.0f;
        u32 sp154 = arg1->a * 65536.0f;

        u32 sp150 = sub_GAME_7F094298((sp170 - arg1->r) / ((arg2->unk28 - arg1->unk28) * 0.25f));
        u32 sp14c = sub_GAME_7F094298((sp16c - arg1->g) / ((arg2->unk28 - arg1->unk28) * 0.25f));
        u32 sp148 = sub_GAME_7F094298((sp168 - arg1->b) / ((arg2->unk28 - arg1->unk28) * 0.25f));
        u32 sp144 = sub_GAME_7F094298((sp164 - arg1->a) / ((arg2->unk28 - arg1->unk28) * 0.25f));

        u32 sp140;
        u32 sp13c;
        u32 sp138;
        u32 sp134;
        u32 sp130;
        u32 sp12c;
        u32 sp128;
        u32 sp124;

        sp140 = sp130 = sub_GAME_7F094298((arg3->r - arg1->r) / ((arg3->unk2c - arg1->unk2c) * 0.25f));
        sp13c = sp12c = sub_GAME_7F094298((arg3->g - arg1->g) / ((arg3->unk2c - arg1->unk2c) * 0.25f));
        sp138 = sp128 = sub_GAME_7F094298((arg3->b - arg1->b) / ((arg3->unk2c - arg1->unk2c) * 0.25f));
        sp124 = sp134 = sub_GAME_7F094298((arg3->a - arg1->a) / ((arg3->unk2c - arg1->unk2c) * 0.25f));

        gImmp1(gdl++, G_RDPHALF_1, (sp160 & 0xffff0000) | (sp15c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp158 & 0xffff0000) | (sp154 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp150 & 0xffff0000) | (sp14c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp148 & 0xffff0000) | (sp144 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp160 & 0x0000ffff) << 16 | (sp15c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp158 & 0x0000ffff) << 16 | (sp154 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp150 & 0x0000ffff) << 16 | (sp14c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp148 & 0x0000ffff) << 16 | (sp144 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp140 & 0xffff0000) | (sp13c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp138 & 0xffff0000) | (sp134 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp130 & 0xffff0000) | (sp12c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp128 & 0xffff0000) | (sp124 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp140 & 0x0000ffff) << 16 | (sp13c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp138 & 0x0000ffff) << 16 | (sp134 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp130 & 0x0000ffff) << 16 | (sp12c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp128 & 0x0000ffff) << 16 | (sp124 & 0x0000ffff));
    }

    sp234[0] = sp364[0] / 32.0f;
    sp238[0] = sp368[0] / 32.0f;
    sp23c[0] = sp3ac / 32.0f;

    for (i = 0; i < 4; i++)
    {
        sp214[i] = SKYABS(sp2d4[i]) / 32.0f;
        sp1f4[i] = SKYABS(sp2f4[i]) / 32.0f;
    }

    sp1d4[0] = sp234[0] + (2 * sp214[0]) + sp1f4[0];
    sp1d8[0] = sp238[0] + (2 * sp214[1]) + sp1f4[1];
    sp1dc[0] = sp23c[0] + (2 * sp214[2]) + sp1f4[2];

    if (sp1d4[0] < sp1d8[0]) { sp1d4[0] = sp1d8[0]; }
    if (sp1d4[0] < sp1dc[0]) { sp1d4[0] = sp1dc[0]; }

    sp1d4[0] *= (1.0f / 1024.0f);

    if (sp1d4[0] > 1.0f)
    {
        sp1c4[0] = 1.0f / sp1d4[0];
    }
    else
    {
        sp1c4[0] = 1.0f;
    }

    {
        u32 spe0;
        u32 spdc;
        u32 spd8;
        u32 spd4;
        u32 spd0;
        u32 spcc;
        u32 spc8;
        u32 spc4;
        u32 spc0;
        u32 spbc;
        u32 spb8;
        u32 spb4;
        u32 spb0;
        u32 spac;
        u32 spa8;
        u32 spa4;

        spe0 = sub_GAME_7F094298(sp254[0] * sp1c4[0]);
        spdc = sub_GAME_7F094298(sp254[1] * sp1c4[0]);
        spd8 = sub_GAME_7F094298(sp254[2] * sp1c4[0]);
        spd4 = sub_GAME_7F094298(sp254[3] * sp1c4[0]);

        spd0 = sub_GAME_7F094298(sp2d4[0] * sp1c4[0]);
        spcc = sub_GAME_7F094298(sp2d4[1] * sp1c4[0]);
        spc8 = sub_GAME_7F094298(sp2d4[2] * sp1c4[0]);
        spc4 = sub_GAME_7F094298(sp2d4[3] * sp1c4[0]);

        spb0 = sub_GAME_7F094298(sp2f4[0] * sp1c4[0]);
        spac = sub_GAME_7F094298(sp2f4[1] * sp1c4[0]);
        spa8 = sub_GAME_7F094298(sp2f4[2] * sp1c4[0]);
        spa4 = sub_GAME_7F094298(sp2f4[3] * sp1c4[0]);

        spc0 = sub_GAME_7F094298(sp274[0] * sp1c4[0]);
        spbc = sub_GAME_7F094298(sp274[1] * sp1c4[0]);
        spb8 = sub_GAME_7F094298(sp274[2] * sp1c4[0]);
        spb4 = sub_GAME_7F094298(sp274[3] * sp1c4[0]);

        gImmp1(gdl++, G_RDPHALF_1, (spe0 & 0xffff0000) | (spdc & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spd8 & 0xffff0000) | (spd4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spd0 & 0xffff0000) | (spcc & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spc8 & 0xffff0000) | (spc4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spe0 & 0x0000ffff) << 16 | (spdc & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spd8 & 0x0000ffff) << 16 | (spd4 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spd0 & 0x0000ffff) << 16 | (spcc & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spc8 & 0x0000ffff) << 16 | (spc4 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spc0 & 0xffff0000) | (spbc & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spb8 & 0xffff0000) | (spb4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spb0 & 0xffff0000) | (spac & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spa8 & 0xffff0000) | (spa4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spc0 & 0x0000ffff) << 16 | (spbc & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spb8 & 0x0000ffff) << 16 | (spb4 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spb0 & 0x0000ffff) << 16 | (spac & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_2, (spa8 & 0x0000ffff) << 16 | (spa4 & 0x0000ffff));
    }

    return gdl;
}
