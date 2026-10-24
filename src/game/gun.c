#include <ultra64.h>
#include "game/bg.h"
#include "game/dyn.h"
#ifndef TARGET_N64
#include <stdio.h>  /* get_ptr_item_statistics out-of-range report */
#include <string.h> /* memset - full-Model stack instances */
#endif
#include <gbi_extension.h>
#include "include/limits.h"
#include <bondconstants.h>
#include <bondtypes.h>
#include <bondgame.h>
#include <music.h>
#include <snd.h>
#include "bondview.h"
#include "bondinv.h"
#include "gun.h"
#include "chrobjdata.h"
#include "game/propobj.h"
#include "game/objective_status.h"
#include "quaternion.h"
#include "image_bank.h"
#include "bondwalk2.h"
#include "othermodemicrocode.h"
#include "player.h"
#include "lv.h"
#include "random.h"
#include "math_asinfacosf.h"
#include "loadobjectmodel.h"
#include "objecthandler.h"
#include "image.h"
#include "tex.h"
#include "debugmenu_handler.h"
#include "fr.h"
#include "assets/obseg/text/LgunE.h"
#include "textrelated.h"
#include "chrai.h"
#include "model.h"
#include "options.h"
#include "mpmenu.h"
#include "joy.h"
#include "matrixmath.h"
#include "bondinv.h"
#include "stan.h"
#include "gbi_extension.h"
#include "frametiming.h" /* speedgraphframes */


// bss
ALSoundState *g_CasingSfxState;
ALSoundState* g_UnusedSfxState; // Unused, type assumed from surrounding variables.
ALSoundState* g_ImpactSfxStates[NUM_IMPACT_SFX_STATES];

CasingRecord g_Casings[20];
s32 dword_CODE_bss_80076A48; // Unused

#ifdef REFRESH_PAL
    /* PAL */
    #define THROWN_ITEM_REFRESH_RATE                   50
    #define THROWN_ITEM_TIMER_SOLO                     250
    #define THROWN_ITEM_TIMER_MULTI                    150
    #define THROWN_ITEM_TIMER_DEFAULT                  200
    #define GLGRENADE_TIMER                            1000
    #define DUAL_WIELD_TRIGGER_SWAP_TICKS              24
    #define DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS       36
    #define WATCH_SOUND_DURATION_TICKS                 250
    #define GUN_SPRING_DAMP                            0.9402999877929688f
    #define GUN_SPRING_SCALE                           0.05970001220703125f
#else
    /* NTSC */
    #define THROWN_ITEM_REFRESH_RATE                   60
    #define THROWN_ITEM_TIMER_SOLO                     300
    #define THROWN_ITEM_TIMER_MULTI                    180
    #define THROWN_ITEM_TIMER_DEFAULT                  240
    #define GLGRENADE_TIMER                            1200
    #define DUAL_WIELD_TRIGGER_SWAP_TICKS              20
    #define DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS       30
    #define WATCH_SOUND_DURATION_TICKS                 300
    #define GUN_SPRING_DAMP                            0.95f
    #define GUN_SPRING_SCALE                           0.050000012f
#endif

extern f32 g_GLGrenadeLaunchUnk8C;
extern f32 g_GLGrenadeLaunchUnk94;
extern f32 g_TankShellSpeed;

// data
////D:80032440
//rgba_u8 D_80032440[] = {
//	{0x96, 0x96, 0x96, 0},
//	{0x96, 0x96, 0x96, 0}
//};
//
////D:80032448
//rgba_u8 D_80032448[] = {
//	{0xFF, 0xFF, 0xFF, 0},
//	{0xFF, 0xFF, 0xFF, 0},
//	{0xB2, 0x4D, 0x2E, 0}
//};
/**
 * Controls the lighting on environment mapped weapons such as the Cougar Magnum and Golden Gun.
 */
Lights1 g_WeaponEnvmapLight = gdSPDefLights1(
    0x96, 0x96, 0x96,   // ambient RGB
    0xff, 0xff, 0xff,   // diffuse RGB
    0xb2, 0x4d, 0x2e);  // direction
//D:80032454
//u32 D_80032454 = 0;

//D:80032458
u32 D_80032458 = 0;

//D:8003245C
u32 size_item_buffer[] = {0x14820, 0x14820};

//D:80032464
u32 D_80032464[] ={0x7530, 0x7530};



//D:8003246C
CartridgeModelFileRecord ejected_cartridge[] = {
	{&cartridge_header, "GcartridgeZ"},
	{&cartrifle_header, "GcartrifleZ"},
	{&cartblue_header, "GcartblueZ"},
	{&cartshell_header, "GcartshellZ"},
	{0, ""}
};

#include <assets/obseg/gun/gunWeaponStats.inc.c>

//D:80033924
#include <assets/obseg/gun/gunModelFileRecord.inc.c>

//D:80034C9C
u32 cartridges_eject = 0;
//D:80034CA0
u32 g_gunDebKeyframeIndex = 0;

//D:80034CA4
u32 D_80034CA4[] = {
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41000000,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	0x3F000000,    0x41000000,           0x0,    0x40C00000,
	0xBFC00000,           0x0,    0x40B487B1,    0x3E70C0AD,
	0x3E0AE536,    0x3F000000,    0x41000000,           0x0,
	0x41480000,    0xC0600000,           0x0,    0x40C159EC,
	0x3D374BC7,    0x3F0E4378,    0x3F000000,    0x41000000,
	       0x0,    0xC1200000,    0xC1300000,           0x0,
	0x3F9ED962,    0x3EA24C40,    0x3F8B0DF1,    0x3F000000,
	0x41000000,           0x0,    0xC1600000,    0xC1700000,
	       0x0,    0x3FEA4780,    0x40C498E3,    0x3FA316D3,
	0x3F000000,    0x41200000,           0x0,    0xBF800000,
	0xC1100000,           0x0,    0x3EC4BBA1,    0x3EB87C42,
	0x3DD75968,    0x3F000000,    0x41200000,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,    0x3F000000,    0x41A00000,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41A00000,           0x1,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0,           0
};

u32 D_80034E0C[] = {
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41000000,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	0x3F000000,    0x41000000,           0x0,    0xC1080000,
	0xC0C00000,           0x0,    0x40AF7506,    0x40BAB4B9,
	0x40C2A5C2,    0x3F000000,    0x41000000,           0x0,
	0xC0400000,    0xC0600000,           0x0,    0x3ECE08F2,
	0x40B75721,    0x40B62409,    0x3F000000,    0x41000000,
	       0x0,    0xBF000000,    0xC1080000,           0x0,
	0x3F9DFD7A,    0x40B768CD,    0x40B37BDF,    0x3F000000,
	0x41000000,           0x0,    0x40E00000,    0xC1E40000,
	0xBFC00000,    0x3FA74949,    0x40B63EBC,    0x40B6443D,
	0x3F000000,    0x41200000,           0x0,    0xBFC00000,
	0xC1100000,           0x0,    0x3D8ADEEC,    0x40C84E72,
	0x3E506749,    0x3F000000,    0x41200000,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,    0x3F000000,    0x41A00000,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41A00000,           0x1,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
           0x0,           0x0
};

/**
 * Throwing Knife animation for when Z is pressed/held down.
 */
struct Weapon1PTransformKeyframe throwKnifeDrawBackKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Throwing Knife animation for when Z is released.
 */
struct Weapon1PTransformKeyframe throwKnifeReleaseKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Grenade which is strange since you cannot see it on screen. Perhaps the developers once intended to have a proper first person Grenade throwing animation?
 */
struct Weapon1PTransformKeyframe grenadeThrowKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 10.0f, 12.5f, 17.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Timed Mine, but changing the durations has no effect.
 */
struct Weapon1PTransformKeyframe timedMineThrowKeyframes[6] = {
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 12.5f, 17.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Proximity Mine. Changing the durations does effect the time it takes to throw the mine, although nothing is seen on screen.
 */
struct Weapon1PTransformKeyframe proxMineThrowKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Remote Mine. Changing the durations does not effect the time it takes to throw them,
 * but it does change the time it takes before you can throw another.
 */
struct Weapon1PTransformKeyframe remoteMineThrowKeyframes[7] = {
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Slapper attack when the hand starts at the right of the screen then chops downward and to the left.
 */
Weapon1PTransformKeyframe fistMeleeKeyframes1[10] = {
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 10.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 10.0f },
    { 0, {   6.0f, 23.0f, 0.0f }, {  5.91572f, 0.085832f, 0.219482f }, 0.5f, 10.0f },
    { 0, {  18.0f, 35.0f, 9.5f }, { 4.998193f, 0.084203f, 0.268954f }, 0.5f, 10.0f },
    { 0, { -20.0f, 25.5f, 4.0f }, { 0.126148f, 0.304284f, 0.548047f }, 0.5f, 10.0 },
    { 0, { -28.0f, -4.0f, 2.0f }, { 0.506821f,  0.51473f, 0.484098f }, 0.5f,  1.0f },
    { 0, { -28.0f, -4.0f, 2.0f }, { 0.506821f,  0.51473f, 0.484098f }, 0.5f,  1.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 20.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 20.0f },
    { 1, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.0f,  0.0f }
};

/**
 * Slapper attack when the hand moves to the left of the screen then chops downward and to the right.
 */
Weapon1PTransformKeyframe fistMeleeKeyframes2[10] = {
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
       { 0, { -6.0f, 23.0f, 0.0f}, { 5.08683f, 6.131295f, 5.534376f}, 0.5f, 10.0f},
       { 0, { -18.0f, 35.0f, 9.5f}, { 4.880698f, 0.070396f, 5.53615f}, 0.5f, 10.0f},
       { 0, { 8.0f, 25.5f, 4.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 10.0f},
       { 0, { 28.0f, -4.0f, 2.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 1.0f},
       { 0, { 28.0f, -4.0f, 2.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 1.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
       { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Sniper swing right to left.
 */
Weapon1PTransformKeyframe sniperMeleeKeyframes1[11] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 9.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, {9.5f, -0.5f, 3.5f}, { 0.291053f, 5.584375f, 6.212358f}, 0.5f, 8.0f},
    { 0, {18.0f, 7.5f, 3.5f}, { 0.439372f, 5.945201f, 5.993666f}, 0.5f, 8.0f},
    { 0, {-9.0f, 8.5f, 5.5f}, { 0.704803f, 0.194459f, 6.168447f}, 0.5f, 7.0f},
    { 0, {-29.0f, -5.5f, 5.5f}, { 2.281831f, 1.106353f, 1.489998f}, 0.5f, 7.0f},
    { 0, {-57.5f, -27.5f, 5.5f}, { 2.281831f, 1.106353f, 1.489998f}, 0.5f, 7.0f},
    { 0, {-19.5f, -20.0f, 5.5f}, { 1.22519f, 0.726087f, 1.210713f}, 0.5f, 15.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Sniper swing left to right.
 */
Weapon1PTransformKeyframe sniperMeleeKeyframes2[11] = {
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f,  9.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f,  8.0f },
    { 0, { -15.5f,  0.5f, 15.0f }, {  0.9344959855079651f,  0.6256099939346313f,  0.2237969934940338f }, 0.5f,  8.0f },
    { 0, { -23.0f,  2.0f, 12.0f }, {  1.8016400337219238f,  0.9494050145149231f,  0.6307389736175537f }, 0.5f,  8.0f },
    { 0, { -18.0f, -0.5f,  4.0f }, {  0.8478249907493591f,  0.9247649908065796f, 0.07744300365447998f }, 0.5f,  7.0f },
    { 0, {  10.5f,  5.0f,  2.5f }, { 0.22940599918365479f, 0.24570399522781372f, 0.09906300157308578f }, 0.5f,  7.0f },
    { 0, {  18.0f,  5.0f,  2.5f }, { 0.03281300142407417f,    6.20933723449707f,  0.1350640058517456f }, 0.5f,  7.0f },
    { 0, {   9.5f,  3.5f, -1.5f }, {   6.273238182067871f,   6.005795001983643f, 0.08971499651670456f }, 0.5f,  7.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f, 20.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f, 20.0f },
    { 1, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.0f,  0.0f },
};

/**
 * Animation when the Taser is lowering to fire position.
 */
Weapon1PTransformKeyframe taserFireKeyFrames[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.5f, -6.0f, -8.0f}, { 0.439468f, 0.278829f, 0.195178f}, 0.5f, 8.0f},
    { 0, { -2.0f, -8.0f, -10.0f}, { 1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    { 0, { -2.0f, -8.0f, -10.0f}, { 1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Animation once the Taser has fired and goes back to idle position.
 */
Weapon1PTransformKeyframe taserRaiseKeyframes[6] = {
    {0, { -2.0f, -8.0f, -10.0f}, {1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    {0, { -2.0f, -8.0f, -10.0f}, {1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    {0, { 0.5f, -6.0f, -8.0f}, {0.439468f, 0.278829f, 0.195178f}, 0.5f, 8.0f},
    {0, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    {0, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    {1, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

coord3d D_80035C40 = {0.0f, 0.0f, 0.0f};
coord3d D_80035C4C = {0.0f, 0.0f, 0.0f};
coord3d D_80035C58 = {0.0f, 0.0f, -1.0f};
coord3d D_80035C64 = {0.0f, 1.0f, 0.0f};
coord3d D_80035C70 = {6.2536321f, 6.2592888f, 0.204238f};
coord3d D_80035C7C = {0.25044999f, 0.90482301f, 0.28716999f};
coord3d D_80035C88 = {1.715736f, 0.37460899f, 0.92193699f};

//D:80035C94
f32 D_80035C94 = 0;


//D:80035C98
Vtx D_80035C98 = {0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff };
//D:80035CA8
coord3d D_80035CA8 = { 0.0f, 0.0f, 0.0f };
//D:80035CB4
coord3d D_80035CB4 = { 0.0f, 0.0f, 0.0f };
//D:80035CC0
u32 D_80035CC0 = 0;



//D:80035CC4
u32 D_80035CC4[] =                      { 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,           0,  0};
/* ModelRenderData D_8002CCBC = {NULL,
                                TRUE,
                                0x00000003,
                                NULL,
                                NULL,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                CULLMODE_BOTH};
*/
//D:80035D00
u32 D_80035D00 = 0;
//D:80035D04
u32 D_80035D04[] = {1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//D:80035D44
u32 watchControllerButtonBases[] = {
	1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

//D:0x80035E04
struct RicochetSoundsSmall ricochet_sounds_small = {
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_5_A_SFX,      RICO_5_B_SFX,      RICO_5_C_SFX,      RICO_5_D_SFX,
    RICO_6_HBBA_A_SFX, RICO_6_HBBA_B_SFX, RICO_6_HBBA_C_SFX, RICO_6_HBBA_D_SFX
};

//D:80035E2C
struct PunchSounds punch_sounds = {
    PUNCH1_SFX,
    PUNCH2_SFX,
    PUNCH3_SFX
};

//D:80035E34
struct BulletFleshSounds bullet_flesh_sounds = {
    HIT_BULLET_FLESH_SFX,
    HIT_BULLET_FLESH_SFX
};

struct LaserRichochetSounds laser_ricochet_sounds = {
    RICO_LASER2_SFX,
    RICO_LASER3_SFX
};

struct RicochetSoundsLarge ricochet_sounds_large = {
	RICO_12_GBU_A_SFX, RICO_12_GBU_B_SFX, RICO_12_GBU_C_SFX, RICO_12_GBU_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_5_A_SFX,      RICO_5_B_SFX,      RICO_5_C_SFX,      RICO_5_D_SFX,
    RICO_6_HBBA_A_SFX, RICO_6_HBBA_B_SFX, RICO_6_HBBA_C_SFX, RICO_6_HBBA_D_SFX
};

//D:80035E84
struct EarWhistleSounds ear_whistle_sounds = {
    RICO_EAR_WHISTLE1_SFX,
    RICO_EAR_WHISTLE2_SFX,
    RICO_EAR_WHISTLE3_SFX,
    RICO_EAR_WHISTLE4_SFX,
    RICO_EAR_WHISTLE5_SFX
};

//D:80035E90
struct sfx2 watchlaser_fire_sounds = { RICO_LASER2_SFX, RICO_LASER3_SFX };
//D:80035E94
struct sfx3 knife_throw_sounds = { KNIFE_THROW1_SFX, KNIFE_THROW2_SFX, KNIFE_THROW3_SFX };
//D:80035E9C
struct gun_trigger_state g_ZeroTriggerState = { 0, 0 };
//D:80035EA0
//u32 D_80035EA0 = 0;
/* Originally a single 12-byte zero-initialized coord3d template; decomp
 * split it into 3 separate u32 words (D_80035EA4/A8/AC) and gunfire.c read
 * all 12 bytes back through a cast to coord3d*, 8 bytes past this word's
 * actual size. Typed correctly here instead; the other two words were unused
 * elsewhere and are absorbed into it. */
coord3d D_80035EA4 = {0};
//D:80035EB0
/* 16 words (64 bytes), not 15: sizeof(ModelRenderData) is 0x40 and
 * sub_GAME_7F068EC4 (gunfire.c) reads this whole array through a cast to
 * ModelRenderData* - the missing 16th word made that a 4-byte overread
 * past the array. */
u32 g_DefaultCasingModelRenderData[] = {0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//D:80035EEC
u32 dword_D_80035EEC = 0; // Unused

//D:80035EF0
#define AMMO_RELATED_MAX 30
AmmoStats ammo_related[AMMO_RELATED_MAX] = {
    { 0x0    , 0x00000000,   0.0f, },
    { 0x320  , 0x02000C84,   0.0f, },
    { 0xC8   , 0x00000000,   0.0f, },
    { 0x190  , 0x02000C90,  -2.0f, },
    { 0x64   , 0x02000C9C,   0.0f, },
    { 0xC    , 0x02000CD8,   0.0f, },
    { 0x3    , 0x02000CC0,  -2.0f, },
    { 0xA    , 0x02000CFC,   1.0f, },
    { 0xA    , 0x02000D14,   1.0f, },
    { 0xA    , 0x02000D08,   1.0f, },
    { 0xA    , 0x02000CA8,   0.0f, },
    { 0xC    , 0x02000CB4,   0.0f, },
    { 0xC8   , 0x02000CE4,   0.0f, },
    { 0x64   , 0x02000CF0,   0.0f, },
    { 0x32   , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x2    , 0x00000000,   0.0f, },
    { 0x8    , 0x00000000,   0.0f, },
    { 0x6    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x1    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x3E8  , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x32   , 0x02000D20,  -1.0f, },
    { 0x1    , 0x00000000,   0.0f, },
};

//was previously attached to ammo_related[] (array at D:80035EF0)
//D:80036058
u16 D_80036058[] = { 0, 0, 0, 0, };

extern struct ModelSkeleton skeleton_gun_kf7;

//i may belong to objecthandler.c
// D:80036060 canonically freedist
struct ModelHitEntry *g_ModelHitFreeList = NULL;

#ifdef TARGET_N64
typedef struct ModelHeader {
            s16                unk00;
            s16                Type;
            struct ChrRecord  *chr;
            ModelFileHeader   *obj;
            RenderPosView     *render_pos;
            union ModelRwData **datas;
            f32               scale;
            struct Model     *attachedto;
            ModelNode        *attachedto_objinst;
} ModelHeader;
#else
/* PORT: this truncated 0x20-byte Model is stack-allocated and cast to a full
 * Model*, so on GCC the fields past it read stack garbage (the watch menu's 3D
 * gun didn't draw). On PC the name is simply the full Model (a macro, so
 * `struct ModelHeader` resolves too) and both stack sites zero it before use. */
#define ModelHeader Model
#endif


// forward declarations

void bullet_path_from_screen_center(coord3d* arg0, coord3d* arg1, enum GUNHAND arg2);
void gunInitProjectileFromPlayer(ObjectRecord *obj, coord3d *targetpos, Mtxf *arg2, coord3d *velocity, Mtxf *arg4);
s32 gunSample1PTransform(Weapon1PTransformKeyframe *keyframes, f32 time, Mtxf *matrix, GUNHAND hand);
void analyzeGEKey(void);
void give_weapon_case_items(void);
struct ModelFileHeader * get_ptr_weapon_model_header_line(ITEM_IDS weapon);
s32 get_ammo_in_hands_weapon(enum GUNHAND hand);
s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
f32 gunSetHorizontalOffset(GUNHAND hand);
void give_weapon_case_items(void);
void sub_GAME_7F05DA8C(GUNHAND hand, ITEM_IDS weaponnum_watchmenu);
void sub_GAME_7F05E808(GUNHAND hand);
void sub_GAME_7F0649D8(enum GUNHAND hand);
void gunCreateBeamForHand(enum GUNHAND hand);
CasingRecord* casingCreate(ModelFileHeader* header, Mtxf* mtx);
void sub_GAME_7F068508(GUNHAND handnum, f32 floor_y_pos);
Vtx *dynAllocateVertices(s32 count);
Mtx *dynAllocateMatrix(void);
void divide3DCoordinates(coord3d *in, f32 divisor, coord3d *out);

// end forward declarations

// current debug keyframes  (Statically assigned at compile time)
#define DEB_KEYFRAMES sniperMeleeKeyframes2


void set_cartridges_eject(u32 uParm1)
{
    cartridges_eject = uParm1;
}


u32 get_cartridges_eject(void)
{
    return cartridges_eject;
}


// Unreferenced debPrintKeyframe
// Address: ~ 7F05C538
void nullsub_73(void)
{
#ifdef DEBUG
    osSyncPrintf("\t{");
    osSyncPrintf("0");
    osSyncPrintf(",{%ff,%ff,%ff}", DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos.x, DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos.y, DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos.z);
    osSyncPrintf(",{%ff,%ff,%ff}", DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot.x, DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot.y, DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot.z);
    osSyncPrintf(",0.5f,20.0f");
    osSyncPrintf("},\n");
#endif
    return;
}


// Unreferenced - force keyframe to position
// Address: 7F05C540
void sub_GAME_7F05C540(coord3d* pos)
{
    Weapon1PTransformKeyframe* temp_v0;

    temp_v0 = &DEB_KEYFRAMES[g_gunDebKeyframeIndex];
    temp_v0->pos.x += pos->x;
    temp_v0->pos.y += pos->y;
    temp_v0->pos.z += pos->z;
}


// Unreferenced
void sub_GAME_7F05C594(Mtxf* mtxf)
{
    Mtxf sp18;
    matrix_4x4_set_rotation_around_xyz(&DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot, &sp18);
    matrix_4x4_multiply_in_place(mtxf, &sp18);
    matrix_4x4_get_rotation_around_xyz(&sp18, &DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot);
}


void sub_GAME_7F05C614(void)
{
    if (!cartridges_eject) { return; }

    g_CurrentPlayer->hands[0].field_92C = 1;
    matrix_4x4_set_rotation_around_xyz(&DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot, (Mtxf *)&g_CurrentPlayer->hands[0].field_8EC);
    matrix_4x4_set_position(&DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos, (Mtxf *)&g_CurrentPlayer->hands[0].field_8EC);
    cartridges_eject = 0;
}


// Unreferenced increment keyframe index, loop back to start if final keyframe is reached
// Address: 7F05C6B8
void gunDebAdvanceKeyframe(void)
{
    g_gunDebKeyframeIndex++;
    if (DEB_KEYFRAMES[g_gunDebKeyframeIndex].isFinalKey & 1)
    {
        g_gunDebKeyframeIndex = 0;
    }
}


/**
 * Address: 7F05C6FC
 *
 * Sample a first person weapon transform keyframe animation at `time` and
 * write the interpolated transform into `matrix`
 *
 * @returns 1 when an in-progress frame was interpolated, 0 when `time` reached
 * the final keyframe and the static end pose was written.
 */
s32 gunSample1PTransform(Weapon1PTransformKeyframe *keyframes, f32 time, Mtxf *matrix, GUNHAND hand)
{
    Weapon1PTransformKeyframe *current;
    f32 frac;
    f32 tangent;
    coord3d posResult;
    quatf qResult;
    quatf q0;
    quatf q1;
    quatf q2;
    quatf q3;
    s32 i;

    i = 1;
    frac = keyframes[1].duration;
    current = keyframes + i;

    while (time >= current->duration)
    {
        time -= current->duration;
        i++;
        current++;

        if (current[2].isFinalKey & 1)
        {
            break;
        }
    }

     // Comma operator is intentional: nudges IDO to emit addu s0,a0,t8.
    current = (0, keyframes) + i;

    i = 2;

    if (current[i].isFinalKey & 1)
    {
        matrix_4x4_set_rotation_around_xyz(&current->rot, matrix);
        matrix_4x4_set_position(&current->pos, matrix);
        return 0;
    }

    frac = time / current->duration;
    tangent = current->interpParam;

    quaternion_set_rotation_around_xyzf(&current[-1].rot, &q0);
    quaternion_set_rotation_around_xyzf(&current->rot, &q1);
    quaternion_set_rotation_around_xyzf(&current[1].rot, &q2);
    quaternion_set_rotation_around_xyzf(&current[2].rot, &q3);

    quaternion_ensure_shortest_path(&q1, &q2);
    quaternion_ensure_shortest_path(&q2, &q3);
    quaternion_ensure_shortest_path(&q1, &q0);

    quaternion_7F05C2F0(&q0, &q1, &q2, &q3, frac, &qResult);

    coord3dCubicSplineInterp(&current[-1].pos, &current->pos, &current[1].pos, &current[2].pos, frac, tangent, &posResult);

    if (hand == GUNLEFT)
    {
        posResult.x = -posResult.x;
        qResult[0] = -qResult[0];
        qResult[1] = -qResult[1];
    }

    quaternion_to_matrix(&qResult, matrix);
    matrix_4x4_set_position(&posResult, matrix);

    return 1;
}


WeaponStats *get_ptr_item_statistics(ITEM_IDS item)
{
#ifndef TARGET_N64
    /* Every weapon accessor comes through here unchecked, so an out-of-range
     * item id (seen from a guard's kneel attack in Cradle) read wild memory.
     * Log each bad id once and return defaults so the game keeps running. */
    if ((u32) item >= ITEM_IDS_MAX)
    {
        static u32 lastbad = 0xFFFFFFFFu;

        if ((u32) item != lastbad)
        {
            lastbad = (u32) item;
            fprintf(stderr, "ge: get_ptr_item_statistics: item id %d out of range - serving defaults\n", item);
            fflush(stderr);
        }

        return &default_weaponstats;
    }
#endif

    if (gitem_structs[item].has_no_model == 0)
    { /* weapon has model, return stats struct */
#ifndef TARGET_N64
        if (gitem_structs[item].item_weapon_stats == NULL)
        {
            return &default_weaponstats;
        }
#endif
        return gitem_structs[item].item_weapon_stats;
    }
    return &default_weaponstats; /* no model, return defaults */
}




void copy_item_in_hand(coord3d *pos)
{
    ITEM_IDS item;
    WeaponStats *stats;

    item = getCurrentPlayerWeaponId(0);
    stats = get_ptr_item_statistics(item);

    pos->x = stats->PosX;
    pos->y = stats->PosY;
    pos->z = stats->PosZ;
}


void copy_item_in_hand_to_main_list(coord3d *pos) {

    WeaponStats *stats;
    ITEM_IDS item;

    item = getCurrentPlayerWeaponId(0);
    stats = get_ptr_item_statistics(item);

    stats->PosX = pos->x;
    stats->PosY = pos->y;
    stats->PosZ = pos->z;
}


void bgunCalculateBlend(enum GUNHAND handnum)
{
    s32 sp60[2];
    s32 sp58[2];
    f32 mult = get_ptr_item_statistics(getCurrentPlayerWeaponId(handnum))->Sway;

    sp60[handnum] = (g_CurrentPlayer->hands[handnum].curblendpos + 2) % 4;
    sp58[handnum] = (g_CurrentPlayer->hands[handnum].curblendpos + 1) % 4;
    g_CurrentPlayer->hands[handnum].curblendpos = sp58[handnum];

    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.08f * mult;
    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].y = (RANDOMFRAC() - 0.5f) * 0.1f * mult;
    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].z = -1;

    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.1f * mult;
    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].y = 1;
    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 0.1f * mult;

    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].x = (RANDOMFRAC() * 0.75f) + 1.5f;
    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].y = (2 + RANDOMFRAC()) * g_CurrentPlayer->hands[handnum].blendscale1;
    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 2.5f;

    if (g_CurrentPlayer->hands[handnum].sideflag < 0)
    {
        g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].x *= -1;

        if (g_CurrentPlayer->hands[handnum].sideflag == -2)
        {
            g_CurrentPlayer->hands[handnum].sideflag = 1;
        }
        else
        {
            g_CurrentPlayer->hands[handnum].sideflag = -2;
        }
    }
    else
    {
        if (g_CurrentPlayer->hands[handnum].sideflag == 2)
        {
            g_CurrentPlayer->hands[handnum].sideflag = -1;
        }
        else
        {
            g_CurrentPlayer->hands[handnum].sideflag = 2;
        }
    }

    g_CurrentPlayer->hands[handnum].blendscale1 = -g_CurrentPlayer->hands[handnum].blendscale1;
}


s32 Gun_hand_without_item(enum GUNHAND arg0)
{
    return g_CurrentPlayer->hand_invisible[arg0] > 0
        || (g_CurrentPlayer->hand_item[arg0] == 0 && g_CurrentPlayer->field_2A44[arg0] < 0);
}


s32 get_itemtype_in_hand(GUNHAND hand)
{
    return g_CurrentPlayer->hand_item[hand];
}


ModelFileHeader *get_ptr_itemheader_in_hand(GUNHAND hand)
{
    return &g_CurrentPlayer->copy_of_body_obj_header[hand];
}


u8 * getPlayerWeaponBufferForHand(GUNHAND hand)
{
    return g_CurrentPlayer->ptr_hand_weapon_buffer[hand];
}


u32 getSizeBufferWeaponInHand(s32 hand)
{
    return size_item_buffer[hand];
}


void remove_item_in_hand(GUNHAND hand)
{
  g_CurrentPlayer->hand_invisible[hand] = 0;
  g_CurrentPlayer->hand_item[hand] = ITEM_UNARMED;
  g_CurrentPlayer->field_2A44[hand] = -1;
  g_CurrentPlayer->lock_hand_model[hand] = 1;
  return;
}


void place_item_in_hand_swap_and_make_visible(GUNHAND hand, ITEM_IDS item)
{
    if (g_CurrentPlayer->lock_hand_model[hand]) { return; }

    if (g_CurrentPlayer->hand_invisible[hand] >= 0)
    {
        if (item != g_CurrentPlayer->hand_item[hand])
        {
            g_CurrentPlayer->hand_invisible[hand] = -1;
            g_CurrentPlayer->field_2A44[hand] = item;
        }
        return;
    }

    if (item != g_CurrentPlayer->hand_item[hand])
    {
        g_CurrentPlayer->field_2A44[hand] = item;
        return;
    }

    g_CurrentPlayer->hand_invisible[hand] = 1;
}


char *gunGetFilename(ITEM_IDS item)
{
    if (item == ITEM_FIST)
    {
        item = g_CurrentPlayer->cur_item_weapon_getname;
    }
    return gitem_structs[item].item_file_name;
}


 ModelFileHeader *get_ptr_weapon_model_header_line(ITEM_IDS weapon)
{
    if (weapon == ITEM_FIST)
    {
        weapon = g_CurrentPlayer->cur_item_weapon_getname;
    }
    return gitem_structs[weapon].item_header;
}


int getCurrentWeaponOrItem(void)
{
    return g_CurrentPlayer->cur_item_weapon_getname;
}


void used_to_load_1st_person_model_on_demand(GUNHAND hand)
{
    u32              size_buffer_weapon;
    s8              *ptr_item_text;
    ModelFileHeader *ptr_weapon_model;
    u8              *buffer_weapon;
    enum ITEM_IDS    item;

    if ((g_CurrentPlayer->hand_invisible[hand] < 0) && (g_CurrentPlayer->lock_hand_model[hand] == 0))
    {
        if ((g_CurrentPlayer->hand_invisible[hand] < -2) || (g_CurrentPlayer->hand_item[hand] == ITEM_UNARMED))
        {
            item             = g_CurrentPlayer->field_2A44[hand];
            ptr_item_text    = (s8 *)gunGetFilename(item);
            ptr_weapon_model = get_ptr_weapon_model_header_line(item);

            if ((ptr_item_text != NULL) && (ptr_weapon_model != NULL))
            {
                buffer_weapon      = getPlayerWeaponBufferForHand(hand);
                size_buffer_weapon = getSizeBufferWeaponInHand(hand);

                g_CurrentPlayer->copy_of_body_obj_header[hand] = *ptr_weapon_model;

                if (item == ITEM_SUIT_LF_HAND)
                {
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0xBD70, size_buffer_weapon + 0xFFFF4290);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, 0xBD70, &g_CurrentPlayer->item_related[hand]);
                }
                else if ((item == ITEM_TRIGGER) || (item == ITEM_WATCHLASER))
                {
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0xAFD0, size_buffer_weapon + 0xFFFF5030);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, 0xAFD0, &g_CurrentPlayer->item_related[hand]);
                }
                else
                {
                    texInitPool(&g_CurrentPlayer->item_related[hand], &buffer_weapon[D_80032464[hand]], size_buffer_weapon - D_80032464[hand]);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, D_80032464[hand], &g_CurrentPlayer->item_related[hand]);
                }
            }

            g_CurrentPlayer->hand_invisible[hand] = 1;
            g_CurrentPlayer->hand_item[hand]      = item;
            g_CurrentPlayer->field_2A44[hand]     = -1;
        }
        else
        {
            g_CurrentPlayer->hand_invisible[hand]--;
        }
    }
}


#ifndef TARGET_N64
/* Diagnostic (GE_GUN_AUDIT, run once by pcsched): force-load a few weapon
 * models through the normal hands/watch path and report each texture-table
 * entry's state. Names textures that fail to load without needing gameplay;
 * combine with GE_DUMP_BOND for per-display-list detail. */
void portGunTexAudit(void)
{
    extern ModelFileHeader ak47_header;
    extern ModelFileHeader laser_header;
    extern ModelFileHeader grenadelaunch_header;
    extern ModelFileHeader wppk_header;
    extern ModelFileHeader m16_header;
    extern ModelFileHeader fnp90_header;
    static ModelFileHeader *heads[6];
    static char *names[] = { "Gak47Z", "GlaserZ", "GgrenadelaunchZ", "GwppkZ", "Gm16Z", "Gfnp90Z" };
    s32 f;
    s32 t;

    heads[0] = &ak47_header;
    heads[1] = &laser_header;
    heads[2] = &grenadelaunch_header;
    heads[3] = &wppk_header;
    heads[4] = &m16_header;
    heads[5] = &fnp90_header;

    for (f = 0; f < 6; f++)
    {
        ModelFileHeader h = *heads[f];

        fprintf(stderr, "gunaudit: ==== %s numtextures=%d ====\n", names[f], h.numtextures);
        fflush(stderr);

        load_object_fill_header(&h, (u8 *)names[f], NULL, 0, NULL);

        for (t = 0; t < h.numtextures; t++)
        {
            /* texLoad replaced TextureID with the pool data pointer; the
             * pool allocation writes the texture number 8 bytes before the
             * data (texLoad's left-side header), so it is recoverable. */
            uintptr_t id = h.Textures[t].TextureID;
            s32 texnum = (id >= (u32)MAX_TEXTURES) ? *(s16 *)((u8 *)id - 8) : (s32)id;
            extern struct texpool *portModelTexPool(void);
            struct tex *rec = texFindInPool(texnum, portModelTexPool());

            fprintf(stderr, "gunaudit:   tex[%02d] id=%4d %ux%u tiles=%u depth=%u -> %zx%s",
                    t, texnum,
                    h.Textures[t].Width, h.Textures[t].Height,
                    h.Textures[t].MipMapTiles,
                    h.Textures[t].RenderDepth, (size_t)id,
                    (id < (u32)MAX_TEXTURES) ? "  UNCONVERTED" : "");

            if (rec != NULL)
            {
                u32 *d = (u32 *) rec->data;

                fprintf(stderr,
                        " | pool %ux%u fmt=%u siz=%u maxlod=%u lut=%u unk0a=%u data[0..3]=%08x %08x %08x %08x",
                        rec->width, rec->height, rec->gbiformat, rec->depth,
                        rec->maxlod, rec->lutmodeindex, rec->unk0a,
                        d[0], d[1], d[2], d[3]);

                if (rec->depth == 3 && rec->width == 32 && rec->height == 32)
                {
                    /* RGBA32 flash sheets: centre-row texels tell whether
                     * the decoded colours are sane (white/yellow core) */
                    u32 *mid = d + 16 * 32 + 14;

                    fprintf(stderr, " mid=%08x %08x %08x %08x",
                            mid[0], mid[1], mid[2], mid[3]);
                }
            }
            fputc('\n', stderr);
        }

        if (f == 4)
        {
            /* dump every converted node display list so the streams can be
             * diffed offline (used to track down the green muzzle flash) */
            extern void ge_dbg_gfxdump(const char *tag, void *gdl, int count);
            ModelNode *node = NULL;
            Gfx *gdl = NULL;
            s32 n = 0;

            modelIterateDisplayLists(&h, &node, &gdl);
            while (node != NULL && gdl != NULL && n < 40)
            {
                char tag[64];
                s32 len = 0;

                while (len < 96 && ((u8)(gdl[len].words.w0 >> 24)) != (u8)G_ENDDL)
                {
                    len++;
                }
                sprintf(tag, "m16 dl%02d op=%02x len=%d", n, node->Opcode & 0xff, len + 1);
                ge_dbg_gfxdump(tag, gdl, len + 1);

                n++;
                modelIterateDisplayLists(&h, &node, &gdl);
            }
        }
        fflush(stderr);
    }
}
#endif


// Called by unused functions.
ITEM_IDS sub_GAME_7F05D334(ITEM_IDS item, s32 arg1)
{
    while (arg1 > 0)
    {
        do
        {
            item = (item + 1) % ITEM_BOMBCASE;
        } while (bondinvItemAvailable(item) == 0);
        arg1--;
    }

    while (arg1 < 0)
    {
        do
        {
            item--;
            if (item < 0)
            {
                item = 0x20 - (-(item + 1) % ITEM_BOMBCASE);
            }
        } while (bondinvItemAvailable(item) == 0);
        arg1++;
    }

    return item;
}


ITEM_IDS get_next_weapon_in_cycle_for_hand(GUNHAND hand, s32 direction)
{
	if (g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_LOWER)
    {
		if (
			(direction < 0 && (g_CurrentPlayer->hands[hand].field_8B8 > 0)) ||
			(direction > 0 && (g_CurrentPlayer->hands[hand].field_8B8 < 0)) )
        {
			return getCurrentPlayerWeaponId(hand);
		}
		else
        {
			return g_CurrentPlayer->hands[hand].weapon_next_weapon;
		}

    }

    if (g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_SWAP)
    {
        return g_CurrentPlayer->hands[hand].weapon_next_weapon;
    }

    return getCurrentPlayerWeaponId(hand);
}


void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection)
{
    if ((g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_LOWER) || (g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_SWAP))
    {
        g_CurrentPlayer->hands[hand].field_8B0 = g_CurrentPlayer->hands[hand].field_890;

#ifdef VERSION_EU
        if (getPlayerCount() == 1) {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xE;
        } else {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xA;
        }
#else
        if (getPlayerCount() == 1) {
            g_CurrentPlayer->hands[hand].field_8B0 += 0x11;
        } else {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xD;
        }
#endif
    }

    if (get_next_weapon_in_cycle_for_hand(hand, 0) != nextWeapon)
    {
        if ((g_CurrentPlayer->hands[hand].weapon_action_state != GUN_ANIM_STATE_SWITCH_LOWER) && (g_CurrentPlayer->hands[hand].weapon_action_state != GUN_ANIM_STATE_SWITCH_SWAP))
        {
            g_CurrentPlayer->hands[hand].weapon_current_animation = 5;
        }

        g_CurrentPlayer->hands[hand].weapon_next_weapon = nextWeapon;
        g_CurrentPlayer->hands[hand].weapon_animation_trigger = 1;
        g_CurrentPlayer->hands[hand].field_8B8 = cycleDirection;
    }
}


// Unused
void sub_GAME_7F05D610(GUNHAND hand)
{
    gunRequestHandWeaponChange(hand, sub_GAME_7F05D334(get_next_weapon_in_cycle_for_hand(hand, 0), 1), 0);
}


// Unused
void sub_GAME_7F05D650(GUNHAND hand)
{
    gunRequestHandWeaponChange(hand, sub_GAME_7F05D334(get_next_weapon_in_cycle_for_hand(hand, 0), -1), 0);
}


void sub_GAME_7F05D690(void)
{
    currentPlayerEquipWeaponWrapper(GUNRIGHT, g_CurrentPlayer->hands[GUNRIGHT].previous_weapon);
    currentPlayerEquipWeaponWrapper(GUNLEFT, g_CurrentPlayer->hands[GUNLEFT].previous_weapon);
}


void advance_through_inventory(void)
{
    ITEM_IDS nextright;
    ITEM_IDS nextleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, 1);

    if ((nextright >= ITEM_BOMBCASE) || (nextleft >= ITEM_BOMBCASE))
    {
        nextright = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        nextleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else
    {
        bondinvCycleForward(&nextright, &nextleft, FALSE);
    }

    gunRequestHandWeaponChange(GUNRIGHT, nextright, 1);
    gunRequestHandWeaponChange(GUNLEFT, nextleft, 1);
}


void backstep_through_inventory(void)
{
    ITEM_IDS nextright;
    ITEM_IDS nextleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, -1);
    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, -1);

    if ((nextright >= ITEM_BOMBCASE) || (nextleft >= ITEM_BOMBCASE))
    {
        nextright = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        nextleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else
    {
        bondinvCycleBackward(&nextright, &nextleft, FALSE);
    }

    gunRequestHandWeaponChange(GUNRIGHT, nextright, -1);
    gunRequestHandWeaponChange(GUNLEFT, nextleft, -1);
}

void autoadvance_on_deplete_all_ammo(void)
{
	ITEM_IDS nextright;
	ITEM_IDS nextleft;
	ITEM_IDS duperight;
	ITEM_IDS dupeleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    duperight = nextright;

    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, 1);
    dupeleft = nextleft;

    if ((duperight >= ITEM_BOMBCASE) || (dupeleft >= ITEM_BOMBCASE))
    {
        duperight = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        dupeleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else if ((duperight == ITEM_REMOTEMINE) && ((bondinvItemAvailable(ITEM_TRIGGER))))
    {
        duperight = ITEM_TRIGGER;
        dupeleft = ITEM_UNARMED;
    }
    else
    {
        bondinvCycleForward(&duperight, &dupeleft, TRUE);

        if ((duperight < nextright) || ((duperight == nextright) && (nextleft >= dupeleft)))
        {
			duperight = nextright;
			dupeleft = nextleft;
			bondinvCycleBackward(&duperight, &dupeleft, TRUE);
        }
    }

    gunRequestHandWeaponChange(GUNRIGHT, duperight, 1);
    gunRequestHandWeaponChange(GUNLEFT, dupeleft, 1);
}

s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 next_weapon) {
    g_CurrentPlayer->hands[hand].weapon_current_animation = 5;
    g_CurrentPlayer->hands[hand].weapon_next_weapon = next_weapon;
    g_CurrentPlayer->hands[hand].weapon_animation_trigger = 0;
    return next_weapon; /* was falling off a non-void fn (UB at -O2); no caller uses the value */
}

void attempt_reload_item_in_hand(GUNHAND hand) {
    s32 ammo_type = get_ammo_type_for_weapon(getCurrentPlayerWeaponId(hand));
    if (ammo_type != 0) {
        if (g_CurrentPlayer->hands[hand].weapon_current_animation == 0) {
            g_CurrentPlayer->hands[hand].weapon_current_animation = 9;
        }
    }
}

ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand) {
    return g_CurrentPlayer->hands[hand].weaponnum;
}

void draw_item_in_hand(GUNHAND hand, s32 next_weapon) {
	g_CurrentPlayer->hands[hand].weapon_current_animation = 0xE;
	g_CurrentPlayer->hands[hand].weapon_next_weapon = next_weapon;
}

ITEM_IDS get_item_in_hand_or_watch_menu(GUNHAND hand) {
	if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu >= 0) {
		return g_CurrentPlayer->hands[hand].weaponnum_watchmenu;
	} else {
		return g_CurrentPlayer->hands[hand].weaponnum;
	}
}

void sub_GAME_7F05DA8C(GUNHAND hand, ITEM_IDS weaponnum_watchmenu) {
    place_item_in_hand_swap_and_make_visible(hand, weaponnum_watchmenu);
	g_CurrentPlayer->hands[hand].weaponnum_watchmenu = weaponnum_watchmenu;
}

void sub_GAME_7F05DAE4(GUNHAND hand) {
    if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu >= 0) {
        place_item_in_hand_swap_and_make_visible(hand, g_CurrentPlayer->hands[hand].weaponnum);
		g_CurrentPlayer->hands[hand].weaponnum_watchmenu = -1;
    }
}


void currentPlayerUnEquipWeaponWrapper(enum GUNHAND hand, enum ITEM_IDS weapid)
{
    enum ITEM_IDS weapon_num;
    s32 ammo_type;

    weapon_num = g_CurrentPlayer->hands[hand].weaponnum;
    ammo_type = get_ammo_type_for_weapon(weapon_num);

    if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu < 0)
    {
        place_item_in_hand_swap_and_make_visible(hand, weapid);
    }

    if (g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine > 0)
    {
        g_CurrentPlayer->ammoheldarr[ammo_type] += g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine;
    }

    if (weapon_num < ITEM_BOMBCASE)
    {
        g_CurrentPlayer->hands[hand].previous_weapon = weapon_num;
    }

    if (getPlayerCount() >= 2)
    {
        sub_GAME_7F09B368(hand);
    }

    sub_GAME_7F05FB00(hand);
    g_CurrentPlayer->hands[hand].weaponnum = weapid;
    g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine = 0;
    g_CurrentPlayer->hands[hand].field_A4C = 0;
    g_CurrentPlayer->hands[hand].field_A50 = 0;
    bondinvDetermineEquippedItem();
}


s8 get_hands_firing_status(GUNHAND hand) {
    return g_CurrentPlayer->hands[hand].weapon_firing_status;
}

f32 sub_GAME_7F05DCB8(GUNHAND hand) {
	return g_CurrentPlayer->hands[hand].field_A34;
}

/**
 * Positions the gun to the right or left side of the screen depending on which hand is holding it.
 */
f32 gunSetHorizontalOffset(GUNHAND hand)
{
	f32 offset;

	if (hand == GUNRIGHT)
	{
		offset = get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNRIGHT))->PosX;
	}
	else
	{
		offset = -get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNLEFT))->PosX;
	}

	return offset;
}

f32 get_item_in_hand_zoom(void) {
    if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE) {
        return g_CurrentPlayer->sniper_zoom;
    }
    if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
        return g_CurrentPlayer->camera_zoom;
    }
    return get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNRIGHT))->Zoom;
}

#ifndef TARGET_N64
/* GE_TICKHZ: the zoom step multiplies once per tick, so for sub-field ticks
 * (delta < 1) use factor^delta to keep the per-second zoom rate independent of
 * tickhz. delta >= 1 must keep the single vanilla multiply or demo playback
 * diverges; powf is prototyped explicitly because IDO's math.h has no float
 * functions under GCC. */
static f32 gunZoomStepFactor(f32 zoom)
{
    extern float powf(float, float);
    f32 factor = 1.0f + (zoom * 0.1f);

    if (g_GlobalTimerDelta >= 1.0f)
    {
        return factor;
    }
    return powf(factor, g_GlobalTimerDelta);
}
#else
#define gunZoomStepFactor(zoom) (1.0f + ((zoom) * 0.1f))
#endif

void camera_sniper_zoom_out(f32 zoom)
{
	if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE) {
		g_CurrentPlayer->sniper_zoom *= gunZoomStepFactor(zoom);
		if (g_CurrentPlayer->sniper_zoom > GE_DEFAULT_FOV) {
			g_CurrentPlayer->sniper_zoom = GE_DEFAULT_FOV;
		}
	}
	else
	{
		if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
			g_CurrentPlayer->camera_zoom *= gunZoomStepFactor(zoom);
			if (g_CurrentPlayer->camera_zoom > GE_DEFAULT_FOV) {
				g_CurrentPlayer->camera_zoom = GE_DEFAULT_FOV;
			}
		}
	}
}

void camera_sniper_zoom_in(f32 zoom)
{
	if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE) {
		g_CurrentPlayer->sniper_zoom /= gunZoomStepFactor(zoom);
		if (g_CurrentPlayer->sniper_zoom < 7.0f) {
			g_CurrentPlayer->sniper_zoom = 7.0f;
		}
	}
	else
	{
		if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
			g_CurrentPlayer->camera_zoom /= gunZoomStepFactor(zoom);
			if (g_CurrentPlayer->camera_zoom < 7.0f) {
				g_CurrentPlayer->camera_zoom = 7.0f;
			}
		}
	}
}

f32 gunItemGetDestructionAmount(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->DestructionAmount;
}


f32 bondwalkItemGetForceOfImpact(ITEM_IDS item)
{
	return get_ptr_item_statistics(item)->ForceOfImpact;
}

/**
 * Address 0x7F05DFCC
 */
s8 bondwalkItemGetAutomaticFiringRate(ITEM_IDS item) {
#ifndef TARGET_N64
    /* GE_TICKHZ: pick the rate from portTickCadence (the last whole-field
     * delta), not speedgraphframes, which alternates 1,0 under sub-field ticks.
     * Otherwise the 0-ticks return the un-doubled rate and gunTickHandState
     * fires full-auto weapons at double speed; identical at GE_TICKHZ=60. */
    if (portTickCadence == 1)
#else
    if (speedgraphframes == 1)
#endif
    {
        return get_ptr_item_statistics(item)->AutomaticFiringRate * 2;
    }

    return get_ptr_item_statistics(item)->AutomaticFiringRate;
}


u8 bondwalkItemGetSoundTriggerRate(ITEM_IDS item) {
    return get_ptr_item_statistics(item)->SoundTriggerRate;
}


u16 bondwalkItemGetSound(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->Sound;
}


u8 bondwalkItemGetObjectsShootThrough(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->ObjectsShootThrough;
}


s32 bondwalkItemHasAmmo(ITEM_IDS item)
{
    if (bondwalkItemCheckBitflags(item, WEAPONSTATBITFLAG_HAS_AMMO) != 0)
    {
        if ((get_ammo_type_for_weapon(item) == 0) || (get_ammo_count_for_weapon(item) > 0))
        {
            return 1;
        }
    }
    return 0;
}


u32 bondwalkItemCheckBitflags(ITEM_IDS item, u32 mask)
{
  return ((get_ptr_item_statistics(item)->BitFlags & mask) != 0);
}


void gunSetBondWeaponSway(f32 breathing, f32 arg1, f32 arg2, f32 arg3)
{
    f32 dampt[2];
    s32 i;
    u32 unused[2];
    f32 sp50 = arg2;
    f32 sp4c;
    u32 stack2;
    f32 minbreathing;

    if (sp50 < 0.0f) { sp50 = -sp50; }

    if (arg1 > 0.8f)
    {
        g_CurrentPlayer->gunposamplitude = 1.0f;
    }
    else
    {
        if (arg1 > 0.1f)
        {
            f32 tmp = (1.0f - cosf((arg1 - 0.1f) * M_TAU_F / 2.8f));
            g_CurrentPlayer->gunposamplitude = 0.8f * tmp + 0.2f;
        }
        else
        {
            g_CurrentPlayer->gunposamplitude = 0.1f;
        }
    }

    if (g_CurrentPlayer->gunposamplitude < (bondviewGetBondBreathing() * 0.3f))
    {
        g_CurrentPlayer->gunposamplitude = bondviewGetBondBreathing() * 0.3f;
    }

    if (g_CurrentPlayer->gunposamplitude < 0.5f * sp50)
    {
        g_CurrentPlayer->gunposamplitude = 0.5f * sp50;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->field_1080 = (g_CurrentPlayer->field_1080 * (PAL ? 0.9403f : 0.95f)) + g_CurrentPlayer->gunposamplitude;
    }

    g_CurrentPlayer->gunposamplitude = g_CurrentPlayer->field_1080 * (PAL ? 0.059700012f : 0.050000012f);

    minbreathing = 0.016666668f * sp50;
    if (breathing < minbreathing)
    {
        breathing = minbreathing;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->field_107C = (g_CurrentPlayer->field_107C * (PAL ? 0.9403f : 0.95f)) + breathing;
    }

    breathing = g_CurrentPlayer->field_107C * (PAL ? 0.059700012f : 0.050000012f);

    sp4c = breathing * g_GlobalTimerDelta;
    dampt[0] = g_CurrentPlayer->hands[0].dampt + sp4c;

    while (dampt[0] >= 1.0f)
    {
        bgunCalculateBlend(GUNRIGHT);
        dampt[0] -= 1.0f;
        g_CurrentPlayer->syncoffset++;
    }

    g_CurrentPlayer->synccount += g_GlobalTimerDelta;

    if (g_CurrentPlayer->synccount > 60.0f)
    {
        g_CurrentPlayer->synccount = 0.0f;
        g_CurrentPlayer->syncchange = (RANDOMFRAC() - 0.5f) * 0.2f / 60.0f;
    }

    if (g_CurrentPlayer->syncchange + sp4c > 0.0f)
    {
        g_CurrentPlayer->gunsync += g_CurrentPlayer->syncchange;
    }

    if (g_CurrentPlayer->gunsync > 0.5f)
    {
        g_CurrentPlayer->gunsync = 0.5f;
    }
    else if (g_CurrentPlayer->gunsync < -0.5f)
    {
        g_CurrentPlayer->gunsync = -0.5f;
    }
    else if (g_CurrentPlayer->gunsync < 0.1f && g_CurrentPlayer->gunsync > -0.1f)
    {
        if (g_CurrentPlayer->gunsync > 0.0f)
        {
            g_CurrentPlayer->gunsync = -0.1f;
        }
        else
        {
            g_CurrentPlayer->gunsync = 0.1f;
        }
    }

    dampt[1] = dampt[0] + g_CurrentPlayer->syncoffset + g_CurrentPlayer->gunsync;

    while (dampt[1] >= 1.0f)
    {
        bgunCalculateBlend(GUNLEFT);
        dampt[1] -= 1.0f;
        g_CurrentPlayer->syncoffset--;
    }

    for (i = 0; i < 2; i++)
    {
        g_CurrentPlayer->hands[i].dampt = dampt[i];
        g_CurrentPlayer->hands[i].weapon_theta_displacement = -1.75f * arg3;
        g_CurrentPlayer->hands[i].weapon_verta_displacement = -2.0f * arg2;
    }
}


void gunSetOffsetRelated(f32 param_1)
{
    g_CurrentPlayer->hands[GUNRIGHT].gunofs2_z = (1.0f - cosf(param_1)) * 5.0f;
    g_CurrentPlayer->hands[GUNLEFT].gunofs2_z = (1.0f - cosf(param_1)) * 5.0f;
}


f32 get_value_if_watch_is_on_hand_or_not(GUNHAND hand)
{
  if ((getCurrentPlayerWeaponId(hand) == ITEM_TRIGGER) || (getCurrentPlayerWeaponId(hand) == ITEM_WATCHLASER))
  {
    return 0.08726647f;
  }
  else
  {
    return 0.17453294f;
  }
}


void sub_GAME_7F05E6B4(enum GUNHAND hand, s32 arg1)
{
    if (arg1 != 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A84 < get_value_if_watch_is_on_hand_or_not(hand))
        {
            g_CurrentPlayer->hands[hand].field_A84 += (0.029088823f * g_GlobalTimerDelta);
        }
        if (g_CurrentPlayer->hands[hand].field_A84 > get_value_if_watch_is_on_hand_or_not(hand)) {
            g_CurrentPlayer->hands[hand].field_A84 = get_value_if_watch_is_on_hand_or_not(hand);
        }
    }
    else
    {
        if (g_CurrentPlayer->hands[hand].field_A84 > 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A84 -= (0.017453294f * g_GlobalTimerDelta);
        }
        if (g_CurrentPlayer->hands[hand].field_A84 < 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A84 = 0.0f;
        }
    }
}


void sub_GAME_7F05E808(GUNHAND hand) {
	g_CurrentPlayer->hands[hand].field_A8C = 1;
}


void sub_GAME_7F05E83C(GUNHAND hand)
{
    f32 recoil_back;

    recoil_back = get_ptr_item_statistics(get_item_in_hand_or_watch_menu(hand))->BoltRecoilBack;

    if (g_CurrentPlayer->hands[hand].field_A8C != 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A88 < recoil_back)
        {
            g_CurrentPlayer->hands[hand].field_A88 = (g_CurrentPlayer->hands[hand].field_A88 + (recoil_back * 0.25f * g_GlobalTimerDelta));

        }
        if (recoil_back <= g_CurrentPlayer->hands[hand].field_A88) {
            g_CurrentPlayer->hands[hand].field_A88 = recoil_back;
            g_CurrentPlayer->hands[hand].field_A8C = 0;
        }
    }
    else if (g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine > 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A88 > 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A88 = (g_CurrentPlayer->hands[hand].field_A88 - (recoil_back * 0.16666667f * g_GlobalTimerDelta));

        }
        if (g_CurrentPlayer->hands[hand].field_A88 < 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A88 = 0.0f;
        }
    }
}


void sub_GAME_7F05E978(Model* model, s32 val)
{
    if (model->obj->Switches[8] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[8])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[9] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[9])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[10] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[10])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[11] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[11])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[12] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[12])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[13] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[13])->DisplayList.unk00 = val;
    }

    if (model->obj->numSwitches >= 0x24)
    {
        if (model->obj->Switches[35] != NULL)
        {
            modelGetNodeRwData(model, model->obj->Switches[35])->DisplayList.unk00 = val;
        }
    }
}


void sub_GAME_7F05EA94(Model* model, s32 val)
{
    ModelNode* switch_14;
    ModelNode* switch_15;

    if (model->obj->numSwitches >= 0x10)
    {
        switch_14 = model->obj->Switches[14];
        if (switch_14 != NULL)
        {
            // Guessing DisplayList here
            modelGetNodeRwData(model, switch_14)->DisplayList.unk00 = val;
        }

        switch_15 = model->obj->Switches[15];
        if (switch_15 != NULL)
        {
            // Guessing DisplayList here
            modelGetNodeRwData(model, switch_15)->DisplayList.unk00 = val;
        }
    }
}


/**
 * Address 0x7F05EB0C.
*/
void gunInitProjectileObject(ObjectRecord *obj, coord3d *pos, StandTile *stan, Mtxf *matrix, coord3d *velocity, Mtxf *arg5, PropRecord *owner)
{
    PropRecord *temp_s1;
    Projectile *temp_v0;

    temp_s1 = obj->prop;

    if (temp_s1 != NULL)
    {
        chrpropActivate(temp_s1);
        chrpropEnable(temp_s1);
        matrix_scalar_multiply(obj->model->scale, matrix);
        objChangeShading(obj, pos, matrix, stan);

        // loadobjectmodel.c
        setupUpdateObjectRoomPosition(obj);

        chrobjCollisionRelated(obj);
        sub_GAME_7F03FDA8(temp_s1);

        if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
        {
            temp_v0 = obj->projectile;
            temp_v0->flags |= 0x41;
            obj->projectile->ownerprop = owner;
            projectileSetSticky(temp_s1);
            matrix_4x4_copy(arg5, &obj->projectile->mtx);
            obj->projectile->speed.f[0] = velocity->f[0];
            obj->projectile->speed.f[1] = velocity->f[1];
            obj->projectile->speed.f[2] = velocity->f[2];
            obj->projectile->obj = obj;
            obj->projectile->unkE8 = g_GlobalTickCount;
        }
    }
}


/**
 * Address: 7F05EC1C
 *
 * Determines where the projectile may safely enter the world. Ideally that is the targetpos position, but if targetpos
 * is obstructed the player's position used as a fallback. This prevents the player from launching
 * projectiles through nearby surfaces.
 */
void gunInitProjectileFromPlayer(ObjectRecord *obj, coord3d *targetpos, Mtxf *arg2, coord3d *velocity, Mtxf *arg4)
{
    PropRecord *playerprop;
    coord3d pos;
    StandTile *tile;
    u32 pad_c[2];
    f32 yhi;
    f32 ylo;
    s32 usedfallback;
    f32 stanheight;
    /* [8], not [2]: bgFindRoomsAlongSegment (bg.c) always reads its
     * fromRooms arg as 8 bytes (see propobj.c's "u8 roomSet[8]" at its
     * other call sites; unkCC[8] is documented "array of rooms"). The
     * 2-byte buffer here only had room for the initial-room + terminator
     * this function itself sets, overflowing by 6 bytes on every read
     * (thrown-weapon spawn). */
    u8 rooms[8];
    s32 pad_rooms;
    u8 pad_a[0x4c];
    /* [0x14] (20), not a lone s32: bgFindRoomsAlongSegment's traversedRooms
     * out-param is an array sized by its maxTraversedRooms arg (0x14 is
     * passed below) - it writes up to 20 s32 entries via
     * `traversedRooms[count] = allRooms[count]`. propobj.c's call sites
     * use s32 roomNums[121] for the same param; this one only had room
     * for a single s32, overflowing by up to 76 bytes on every shot
     * (grenade launcher). sp50 (traversedRoomCount)
     * is a single s32 out-param and is correctly sized already. */
    s32 sp54[0x14];
    s32 sp50;
    s32 pad_sp;

    // fake
    if (obj->prop);
    if (obj->prop == NULL) {
        return;
    }

    playerprop = getCurrentPlayerProp();
    stanheight = bondviewGetPlayerStanHeight(g_CurrentPlayer);

    usedfallback = 0;

    if (targetpos->y < playerprop->pos.y) {
        yhi = playerprop->pos.y - stanheight;
        ylo = targetpos->y - stanheight;
    }
    else
    {
        yhi = targetpos->y - stanheight;
        ylo = playerprop->pos.y - stanheight;
    }

    tile = playerprop->stan;
    bondviewUpdateGuardTankFlagsRelated(playerprop, 0);

    // If there is no obstruction, spawn the projectile at the target position.
    if (stanTestLineUnobstructed(&tile, playerprop->pos.x, playerprop->pos.z, targetpos->x, targetpos->z, 0x1f, yhi, ylo, 0.0f, 1.0f))
    {
        pos.x = targetpos->x;
        pos.y = targetpos->y;
        pos.z = targetpos->z;
    }
    // Otherwise spawn it from the player's position.
    else
    {
        tile = playerprop->stan;
        pos.x = playerprop->pos.x;
        pos.y = playerprop->pos.y;
        pos.z = playerprop->pos.z;
        usedfallback = 1;
    }

    bondviewUpdateGuardTankFlagsRelated(playerprop, 1);

    gunInitProjectileObject(obj, &pos, tile, arg2, velocity, arg4, playerprop);

    if (obj->runtime_bitflags & 0x80) {
        if (usedfallback) {
            obj->projectile->flags |= PROJECTILEFLAG_00000100;
            ((coord3d *)&obj->projectile->unkd4)->x = targetpos->x;
            ((coord3d *)&obj->projectile->unkd4)->y = targetpos->y;
            ((coord3d *)&obj->projectile->unkd4)->z = targetpos->z;
        }

        rooms[0] = bondviewGetCurrentPlayersRoom();
        rooms[1] = 0xff;

        bgFindRoomsAlongSegment(bondviewGetCurrentPlayersPosition3(), &pos, rooms, obj->projectile->unkCC, sp54, &sp50, 0x14);
    }
}


/**
 * Address 0x7F05EE24 (NTSC)
 * Address 0x7F05F2DC (PAL)
*/
void generate_player_thrown_grenade(s32 hand)
{
    s32 padding;
    Mtxf spFC;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    struct WeaponObjRecord *wor;
    s32 new_prop_type;
    coord3d sp94; // sp148 - bullet_path_from_screen_center's out-param is
                  // a coord3d (12 bytes), not the s32 this was declared as
    struct coord3d base_speed_vec; // sp136
    struct PropRecord* player_prop; // sp132
    struct coord3d *bondprevpos;  // sp128
    Mtxf sp40_f;
    ALSoundState *sfx_state;
    s32 current_weapon;
    s32 unused;

    wor = NULL;
    base_velocity = 16.666666f;

    player_prop = getCurrentPlayerProp();
    bondprevpos = getCurrentPlayerPrevPos();
    current_weapon = getCurrentPlayerWeaponId(hand);

    sub_GAME_7F057C14(&throw_speed_vec, &spFC);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), (f32*)&base_speed_vec);

    throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
    throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
    throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];
#ifndef TARGET_N64
    {   /* Weapon FOV: thrown from the hand as drawn, toward the same aim */
        coord3d aimdir = base_speed_vec;

        if (gunWeaponFovLaunch(hand, &spE0, &aimdir))
        {
            throw_speed_vec.f[0] += (aimdir.f[0] - base_speed_vec.f[0]) * base_velocity;
            throw_speed_vec.f[1] += (aimdir.f[1] - base_speed_vec.f[1]) * base_velocity;
            throw_speed_vec.f[2] += (aimdir.f[2] - base_speed_vec.f[2]) * base_velocity;
        }
    }
#endif

    matrix_4x4_set_identity(&spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);
    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

    wor = create_new_item_instance_of_model(PROP_CHRGRENADE, current_weapon);

    if (wor != NULL)
    {
        wor->timer = THROWN_ITEM_TIMER_DEFAULT - g_CurrentPlayer->last_z_trigger_timer;

        if (wor->timer < 0)
        {
            wor->timer = 0;
        }

        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
        wor->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

        gunInitProjectileFromPlayer(wor, &spE0, &spA0_a, &throw_speed_vec, &spFC);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.3f;
            wor->projectile->unk94 = 0.13333333f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;
#line 1849
            sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GRENADE_THROW_SFX, NULL);

            if (sfx_state != NULL)
            {
                chrobjSndCreatePostEventDefault(sfx_state, (struct coord3d *) &wor->runtime_pos);
            }
        }
    }
}


/**
 * Address 0x7F05F09C (NTSC)
 * Address 0x7F05F554 (PAL)
*/
void generate_player_thrown_knife(s32 hand)
{
    struct WeaponObjRecord *wor;
    Mtxf spFC;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    s32 padding;
    s32 new_prop_type;
    coord3d sp94; // bullet_path_from_screen_center's out-param is a coord3d
                  // (12 bytes), not the s32 this was declared as
    struct coord3d base_speed_vec;
    Mtxf sp40_f;
    struct PropRecord* player_prop;
    struct coord3d *bondprevpos;

    wor = NULL;
    base_velocity = 25.0f;

    player_prop = getCurrentPlayerProp();
    bondprevpos = getCurrentPlayerPrevPos();

    sub_GAME_7F057C14(&throw_speed_vec, &spFC);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), (f32*)&base_speed_vec);

    throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
    throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
    throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];
#ifndef TARGET_N64
    {   /* Weapon FOV: thrown from the hand as drawn, toward the same aim */
        coord3d aimdir = base_speed_vec;

        if (gunWeaponFovLaunch(hand, &spE0, &aimdir))
        {
            throw_speed_vec.f[0] += (aimdir.f[0] - base_speed_vec.f[0]) * base_velocity;
            throw_speed_vec.f[1] += (aimdir.f[1] - base_speed_vec.f[1]) * base_velocity;
            throw_speed_vec.f[2] += (aimdir.f[2] - base_speed_vec.f[2]) * base_velocity;
        }
    }
#endif

    matrix_4x4_set_rotation_around_z(4.712389f, &spA0_a);
    matrix_4x4_set_rotation_around_x(M_PI_F, &sp40_f);
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);

    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

    guRotateF(&spFC, 360.0f / ((randomGetNext() * (0.5f / (f32)INT_MAX)) + 12.1f), spA0_a.m[1][0], spA0_a.m[1][1], spA0_a.m[1][2]);

    wor = create_new_item_instance_of_model(PROP_CHRKNIFE, ITEM_THROWKNIFE);

    if (wor != NULL)
    {
        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
        wor->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

        gunInitProjectileFromPlayer(wor, &spE0, &spA0_a, &throw_speed_vec, &spFC);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.1f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            wor->runtime_bitflags |= RUNTIMEBITFLAG_THROWING_KNIFE_RELATED;
        }

        objUpdateThrowKnifeSound(wor);
    }
}





/**
 * Address 0x7F05F358 (NTSC)
 * Address 0x7F05F810 (PAL)
 * throwmineremote
*/
void generate_player_thrown_object(s32 hand)
{
    s32 padding;
    Mtxf unk_mtxf;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    struct WeaponObjRecord *wor;
    s32 new_prop_type;
    coord3d sp94; // sp148 - bullet_path_from_screen_center's out-param is
                  // a coord3d (12 bytes), not the s32 this was declared as
    struct coord3d base_speed_vec; // sp136
    struct PropRecord* player_prop; // sp132
    struct coord3d *bondprevpos;  // sp128
    Mtxf sp40_f;
    ALSoundState *sfx_state;
    s32 current_weapon;
    s32 unused;

    wor = NULL;
    base_velocity = 16.666666f;

    player_prop = getCurrentPlayerProp();
    bondprevpos = getCurrentPlayerPrevPos();
    current_weapon = getCurrentPlayerWeaponId(hand);

    if (current_weapon == ITEM_GOLDENEYEKEY)
    {
        base_velocity = 6.6666665f;
    }

    sub_GAME_7F057C14(&throw_speed_vec, &unk_mtxf);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), (f32*)&base_speed_vec);

    throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
    throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
    throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];
#ifndef TARGET_N64
    {   /* Weapon FOV: thrown from the hand as drawn, toward the same aim */
        coord3d aimdir = base_speed_vec;

        if (gunWeaponFovLaunch(hand, &spE0, &aimdir))
        {
            throw_speed_vec.f[0] += (aimdir.f[0] - base_speed_vec.f[0]) * base_velocity;
            throw_speed_vec.f[1] += (aimdir.f[1] - base_speed_vec.f[1]) * base_velocity;
            throw_speed_vec.f[2] += (aimdir.f[2] - base_speed_vec.f[2]) * base_velocity;
        }
    }
#endif

    matrix_4x4_set_identity(&spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);
    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

    if (current_weapon == ITEM_GOLDENEYEKEY)
    {
        wor = bondinvRemovePropWeaponByID(current_weapon);
        bondinvRemoveItemByID(current_weapon);

        if (wor != NULL)
        {
            objDetach(wor->prop);
        }

        sub_GAME_7F05D690();
    }

    if (wor == NULL)
    {
        new_prop_type = PROP_CHRREMOTEMINE;

        switch (current_weapon)
        {
        case ITEM_PROXIMITYMINE:
            new_prop_type = PROP_CHRPROXIMITYMINE;
            break;
        case ITEM_TIMEDMINE:
            new_prop_type = PROP_CHRTIMEDMINE;
            break;
        case ITEM_BOMBCASE:
            new_prop_type = PROP_CHRBOMBCASE;
            break;
        case ITEM_BUG:
            new_prop_type = PROP_CHRBUG;
            break;
        case ITEM_MICROCAMERA:
            new_prop_type = PROP_CHRMICROCAMERA;
            break;
        case ITEM_GOLDENEYEKEY:
            new_prop_type = PROP_CHRGOLDENEYEKEY;
            break;
        case ITEM_PLASTIQUE:
            new_prop_type = PROP_CHRPLASTIQUE;
            break;
#ifdef DEBUG
        default:
            assertmsg2(current_weapon = PROP_CHRREMOTEMINE, "throwmineremote - Not a mine!");
#endif

        }

        wor = create_new_item_instance_of_model(new_prop_type, current_weapon);
    }

    if (wor != NULL)
    {
        switch (current_weapon)
        {
            case ITEM_REMOTEMINE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_PROXIMITYMINE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_TIMEDMINE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_BOMBCASE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_PLASTIQUE:
            case ITEM_BUG:
            case ITEM_MICROCAMERA:
            case ITEM_GOLDENEYEKEY:
                wor->timer = 1;
            break;

            default:
                wor->timer = THROWN_ITEM_TIMER_DEFAULT;
            break;
        }

        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
        wor->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

        gunInitProjectileFromPlayer(wor, &spE0, &spA0_a, &throw_speed_vec, &unk_mtxf);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.1f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;
        #line 2218
            sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GRENADE_THROW_SFX, NULL);

            if (sfx_state != NULL)
            {
                chrobjSndCreatePostEventDefault(sfx_state, (struct coord3d *) &wor->runtime_pos);
            }
        }
    }
}


/**
 * Address: 7F05F73C
 *
 * Spawns Grenade Launcher rounds and makes them inherit the player's momentum.
 */
void gunSpawnGLGrenade(s32 handnum)
{
    WeaponObjRecord *grenadeobj;
    struct hand *hand;
    Mtxf identitymtx;
    coord3d launchvel;
    s32 pad;
    Mtxf launchmtx;
    coord3d aimpos;
    coord3d aimdir;
    PropRecord *playerprop;
    coord3d *prevplayerpos;
#ifndef TARGET_N64
    coord3d launchpos;
#endif

    hand = &g_CurrentPlayer->hands[handnum];

    playerprop = getCurrentPlayerProp();
    prevplayerpos = getCurrentPlayerPrevPos();

    matrix_4x4_set_identity(&identitymtx);
    bullet_path_from_screen_center(&aimpos, &aimdir, handnum);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &aimdir);
#ifndef TARGET_N64
    launchpos = hand->field_B58;
    gunWeaponFovLaunch(handnum, &launchpos, &aimdir);   /* Weapon FOV: from the drawn launcher, toward the same aim */
#endif

    launchvel.x = aimdir.x * 33.333332f;
    launchvel.y = aimdir.y * 33.333332f;
    launchvel.z = aimdir.z * 33.333332f;

    if (g_ClockTimer > 0)
    {
        launchvel.x += (playerprop->pos.x - prevplayerpos->x) / g_GlobalTimerDelta;
        launchvel.y += (playerprop->pos.y - prevplayerpos->y) / g_GlobalTimerDelta;
        launchvel.z += (playerprop->pos.z - prevplayerpos->z) / g_GlobalTimerDelta;
    }

    matrix_4x4_copy(&g_CurrentPlayer->hands[handnum].throw_item_pos_related, &launchmtx);

    launchmtx.m[3][0] = 0.0f;
    launchmtx.m[3][1] = 0.0f;
    launchmtx.m[3][2] = 0.0f;

    grenadeobj = create_new_item_instance_of_model(PROP_CHRGRENADEROUND, ITEM_GRENADEROUND);

    if (grenadeobj != NULL)
    {
        grenadeobj->timer = GLGRENADE_TIMER;
        grenadeobj->runtime_bitflags &= ~RUNTIMEBITFLAG_OWNER;
        grenadeobj->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

#ifdef TARGET_N64
        gunInitProjectileFromPlayer(grenadeobj, &hand->field_B58, &launchmtx, &launchvel, (s32 *)&identitymtx);
#else
        gunInitProjectileFromPlayer(grenadeobj, &launchpos, &launchmtx, &launchvel, (s32 *)&identitymtx);
#endif

        if (grenadeobj->runtime_bitflags & RUNTIMEBITFLAG_00000080)
        {
            grenadeobj->projectile->unk8C = g_GLGrenadeLaunchUnk8C;
            grenadeobj->projectile->unk94 = g_GLGrenadeLaunchUnk94;
            grenadeobj->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;
        }
    }
}


/**
 * Address: 0x7F05F928
 * This function is responsible for attaching a rocket to the end of the Rocket Launcher and updating its matrices.
 */
void gunUpdateAttachedRocket(s32 handIndex)
{
    struct hand *entry;
    AttachedObj *attachedRocket;
    Model *rocketModel;
    Mtxf worldMtx;
    PropRecord *prop;
    AttachmentChild *attachmentChild;

    entry = &g_CurrentPlayer->hands[handIndex];

    attachedRocket = entry->rocket;

    if (attachedRocket == NULL)
    {
        return;
    }

    attachmentChild = attachedRocket->child;

    if (attachmentChild == NULL)
    {
        return;
    }

    prop = getCurrentPlayerProp();
    rocketModel = attachedRocket->model;

    matrix_4x4_copy(&entry->throw_item_pos_related, &worldMtx);

    worldMtx.m[3][0] = 0.0f;
    worldMtx.m[3][1] = 0.0f;
    worldMtx.m[3][2] = 0.0f;

    matrix_scalar_multiply(attachedRocket->model->scale, (f32 *)&worldMtx);

    objChangeShading(attachedRocket, &entry->field_B58, &worldMtx, prop->stan);
    chrobjCollisionRelated(attachedRocket);

    rocketModel->render_pos = dynAllocate((s32)rocketModel->obj->numMatrices << 6);

    matrix_4x4_copy(&attachedRocket->transform, &worldMtx);
    matrix_4x4_set_position((Mtxf *)&attachedRocket->position, &worldMtx);

    matrix_4x4_multiply_homogeneous(camGetWorldToScreenMtxf(), &worldMtx, rocketModel->render_pos);
    modelUpdateRelationsQuick(rocketModel, rocketModel->obj->RootNode);

    attachmentChild->flags1 |= 2;
    attachmentChild->unk18 = -rocketModel->render_pos->pos.m[3][2];
}


/*
* Address: 0x7f05fa7c
*/
void currentPlayerCreateRocket(GUNHAND hand)
{
    struct hand * hand_ptr;
    struct WeaponObjRecord * rocket;

    hand_ptr = &g_CurrentPlayer->hands[hand];

    if ((hand_ptr->rocket == NULL) && (hand_ptr->weapon_ammo_in_magazine > 0))
    {
        rocket = (struct WeaponObjRecord *)create_new_item_instance_of_model(PROP_CHRROCKET, ITEM_ROCKETROUND);

        if (rocket != NULL)
        {
            hand_ptr->rocket = (ObjectRecord *)rocket;
            hand_ptr->firedrocket = 0;
            rocket->timer = 1;
        }
    }
}


/*
* Address: 0x7F05FB00
* This function frees some sort of ObjectRecord from the given hand
*/
#if defined(VERSION_EU)
void sub_GAME_7F05FB00(enum GUNHAND hand)
{
    struct hand* hand_ptr;
    ObjectRecord* hand_obj_record;

    hand_ptr = &g_CurrentPlayer->hands[hand];
    hand_obj_record = hand_ptr->rocket;

    if (hand_obj_record != NULL)
    {
        objFreePermanently(hand_obj_record, 1);
        hand_ptr->rocket = NULL;
    }
}


extern f32 D_80053DDC;

/*
* Address: 0x7F05FB64
*/
void gunFireTankShell(s32 handnum)
{
    WeaponObjRecord *obj;
    struct hand *hand;
    Mtxf identitymtx;
    coord3d velocity;
    ObjectRecord *tankobj;
    coord3d unscaledvelocity;
    Mtxf shellmtx;
    coord3d screenpos;
    coord3d aimdir;
    PropRecord *playerprop;
    coord3d *prevplayerpos;
    ITEM_IDS weaponid;
    coord3d spawnpos;
    PropRecord *tankprop;

    hand = &g_CurrentPlayer->hands[handnum];

    playerprop = getCurrentPlayerProp();
    prevplayerpos = getCurrentPlayerPrevPos();
    weaponid = getCurrentPlayerWeaponId(handnum);

    matrix_4x4_set_identity(&identitymtx);

    if (weaponid == ITEM_TANKSHELLS)
    {
        tankprop = get_ptr_for_players_tank();

        if (1);

        if ((tankprop != NULL) && (tankprop->flags & TANK_RUN_STATE_RUNNING))
        {
            bondviewSet3dCoord7F07CEB0(&aimdir);
        }
        else
        {
            sub_GAME_7F068190(&screenpos, &aimdir);
            mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &aimdir);
        }

        velocity.x = aimdir.x * g_TankShellSpeed;
        velocity.y = aimdir.y * g_TankShellSpeed;
        velocity.z = aimdir.z * g_TankShellSpeed;

        if (g_ClockTimer > 0) {
            velocity.x += (playerprop->pos.x - prevplayerpos->x) / g_GlobalTimerDelta;
            velocity.y += (playerprop->pos.y - prevplayerpos->y) / g_GlobalTimerDelta;
            velocity.z += (playerprop->pos.z - prevplayerpos->z) / g_GlobalTimerDelta;
        }

        if ((tankprop != NULL) && (tankprop->flags & TANK_RUN_STATE_RUNNING))
        {
            tankobj = tankprop->obj;
            spawnpos.x = tankobj->model->render_pos[4].pos.m[3][0];
            spawnpos.y = tankobj->model->render_pos[4].pos.m[3][1];
            spawnpos.z = tankobj->model->render_pos[4].pos.m[3][2];

            mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &spawnpos);
        }
        else
        {
            spawnpos.x = playerprop->pos.x;
            spawnpos.y = playerprop->pos.y;
            spawnpos.z = playerprop->pos.z;
        }

        if ((g_CurrentPlayer && g_CurrentPlayer));

        setSixExplosionAndSmokeEntries();
    }
    else
    {
        bullet_path_from_screen_center(&screenpos, &aimdir, handnum);
        mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &aimdir);

        spawnpos.x = hand->field_B58.x;
        spawnpos.y = hand->field_B58.y;
        spawnpos.z = hand->field_B58.z;
#ifndef TARGET_N64
        gunWeaponFovLaunch(handnum, &spawnpos, &aimdir);   /* from the drawn launcher, toward the same aim */
#endif

        if (1);

        unscaledvelocity.x = aimdir.x * D_80053DDC;
        unscaledvelocity.y = aimdir.y * D_80053DDC;
        unscaledvelocity.z = aimdir.z * D_80053DDC;

        velocity.x = unscaledvelocity.x * g_GlobalTimerDelta;
        velocity.y = unscaledvelocity.y * g_GlobalTimerDelta;
        velocity.z = unscaledvelocity.z * g_GlobalTimerDelta;

        if (g_ClockTimer > 0)
        {
            velocity.x += (playerprop->pos.x - prevplayerpos->x) / g_GlobalTimerDelta;
            velocity.y += (playerprop->pos.y - prevplayerpos->y) / g_GlobalTimerDelta;
            velocity.z += (playerprop->pos.z - prevplayerpos->z) / g_GlobalTimerDelta;
        }
    }

    matrix_4x4_copy(&g_CurrentPlayer->hands[handnum].throw_item_pos_related, &shellmtx);

    shellmtx.m[3][0] = 0.0f;
    shellmtx.m[3][1] = 0.0f;
    shellmtx.m[3][2] = 0.0f;

    if (hand->rocket != NULL)
    {
        obj = (WeaponObjRecord *) hand->rocket;
        hand->firedrocket = 1;
    }
    else
    {
        obj = (WeaponObjRecord *) create_new_item_instance_of_model(PROP_CHRROCKET, ITEM_ROCKETROUND);
    }

    if (obj == NULL)
    {
        return;
    }

    obj->timer = -1;
    obj->runtime_bitflags &= ~RUNTIMEBITFLAG_OWNER;
    obj->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

    gunInitProjectileFromPlayer(obj, &spawnpos, &shellmtx, &velocity, (s32 *) &identitymtx);

    if (obj->runtime_bitflags & RUNTIMEBITFLAG_00000080)
    {
        obj->projectile->flags |= PROJECTILEFLAG_LAUNCHING;

        if (weaponid != ITEM_TANKSHELLS)
        {
            obj->projectile->flags |= PROJECTILEFLAG_00000020;
            obj->projectile->unkB0 = obj->runtime_pos.y;
            obj->projectile->unkB4 = obj->projectile->speed.y;
            obj->projectile->unk10.x = unscaledvelocity.x;
            obj->projectile->unk10.y = unscaledvelocity.y;
            obj->projectile->unk10.z = unscaledvelocity.z;
            obj->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            if (obj->projectile->sounds[0] == NULL)
            {
                sndPlaySfx(g_musicSfxBufferPtr, 1, &obj->projectile->sounds[0]);
            }
            else if (obj->projectile->sounds[1] == NULL)
            {
                sndPlaySfx(g_musicSfxBufferPtr, 1, &obj->projectile->sounds[1]);
            }
        }
    }
}
#endif

const char g_GunHudIntegerFormat[] = "%d\n";
const char aSD[] = "%s: %d\n";
const char g_GunDeathCountFormat[] = "%s %d %s\n";
const char aSD_0[] = "%s: %d\n";

