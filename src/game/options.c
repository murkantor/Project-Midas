#ifndef TARGET_N64
#include "port_input.h"
#endif
#include <ultra64.h>
#include "game/debugmenu_handler.h"
#include "game/lv.h"
#include "game/mp_music.h"
#include "game/bondinv.h"
#include "game/gun.h"
#include "game/objective_status.h"
void reset_watch_item_is_actively_selected(void);
void set_controlstick_lr_disabled(void);
void disable_watch_stick_y_nav_ready(void);
void sub_GAME_7F0A5210(void);
#include <bondconstants.h>
#include <boss.h>
#include <fr.h>
#include <joy.h> /* Must be included: joyGetStickX/Y return s8, and without the
                  * prototype the PC ABI zero-extends the char, which reversed
                  * every negative stick direction in the watch menu. */
#include <music.h>
#include <os_extension.h>
#include <snd.h>
#include <random.h>
#include "options.h"
#include "bondview.h"
#include "dyn.h"
#include "file.h"
#include "front.h"
#include "language.h"
#include "player.h"
#include "textrelated.h"
#include "glass.h"
#include "frametiming.h"
#include "assets/obseg/text/LoptionE.h"
#ifndef TARGET_N64
#include "port_watchpages.h" /* the port watch pages */
/* Defined further down, next to the vanilla page drawer they replace; the
 * watch MOUSE layer needs them a few thousand lines earlier. */
extern s32 g_portWatchPage;
Gfx *portWatchDrawRows(Gfx *gdl);
void portWatchRowNavigate(void);
void portWatchResetPage(void);
void portWatchGotoSubPage(s32 page);
void portWatchStepRow(s32 row, s32 dir);
s32  portWatchActivateRow(void);
void watch_play_beep_sound(void);   /* vanilla, defined below: a bind row's key-text click runs the same A/Z fallback */
s32  portWatchCurrentRow(void);
s32  portWatchRowsOnPage(void);
s32  portWatchIndexLimit(void);
s32  portWatchIndexFirst(void);
void portWatchHarnessTick(void);
static s32 sPortWatchHarnessDraw;   /* 1 = this call is portWatchDrawRows', not the tick's */
s32  portWatchValuesFitPublic(s32 page, s32 row, s32 count);
s32  portWatchRowDropdown(s32 page, s32 row);
extern s32 portVideoDeleteEdge(void);   /* the Delete key, edge-latched */
extern s32 portReturnStartThisTick(void); /* video.c: START came from Return alone; weak 0 headless */
static void portWatchDropAnchor(s32 row);   /* park the list window at open */
static s32  portWatchDropVisible(void);
static void portWatchDropBox(s32 rowy, s32 *x0, s32 *y0, s32 *w, s32 *vis);
s32  portWatchValueColumnHit(s32 page, s32 row, s32 count, f32 mx);
f32  portWatchValueSplitX(s32 page, s32 row);
s32  portWatchRowY0(void);
s32  portWatchRowSlots(void);

/* MUSIC / FX bar position in watch-model z, moved down onto each bar's own
 * label (Rare drew it a line above). Shared by the drawers and the mouse hit
 * box, so keep it as the single definition. */
#define PORTWATCH_BAR_MUSIC_Z (-253.0f)
#define PORTWATCH_BAR_FX_Z    (-183.0f)
#define PORTWATCH_BAR_H       20.0f
/* The bar starts to the right of its label instead of running through the label
 * text. The right edge stays where Rare had it (-167 + 468 = 301). */
#define PORTWATCH_BAR_X       (-167.0f)
#define PORTWATCH_BAR_W       468.0f
s32  portWatchVisibleRows(void);
s32  portWatchScrollOffset(void);
void portWatchScrollFollow(void);
s32  portWatchWheelScroll(s32 notches); /* 1 = the wheel was used to scroll the view */
void portWatchEnterControllerTab(void);
s32  portWatchRaphnetView(void);
/* the Control tab's "input" row: press a button on the device (further down) */
extern s32  portInputAssignActive(void);
void portWatchAssignBegin(void);
s32  portWatchAssignTick(void);
s32  portWatchAssignSwallowing(void);
void portWatchAssignSwallowPoll(void);
s32  portWatchModalBack(void);
void portInputAssignCancel(void); /* input_pc.c - used above its extern further down */
Gfx *portWatchDrawDeviceRow(Gfx *gdl);
s32  portWatchRaphnetDeviceRowFocused(void);
void portWatchDeviceRowNavigate(void);
void portWatchRaphnetNavigate(void);
/* Set when the mouse (hover or click) placed the highlight this frame, so
 * portWatchScrollFollow must not scroll rows out from under a resting pointer.
 * Declared here, outside the GE_VIDEO mouse block, because
 * portWatchScrollFollow also builds without it. */
static s32 sPortWatchIndexByMouse;
#endif

#define WATCH_BACKGROUND_VERTEX_COUNT 30

#define WATCH_VOL_ADJUST_STEP 1024

#if defined(VERSION_US)
#define WATCH_ROTATION_FRAMES speedgraphframes
#else
#define WATCH_ROTATION_FRAMES jpD_800484D0
#endif

#if defined(VERSION_EU)
#define WATCH_PERSPECTIVE_FOVY    52.5f
#define WATCH_PERSPECTIVE_ASPECT  1.283847f
#else
#define WATCH_PERSPECTIVE_FOVY    50.5f
#define WATCH_PERSPECTIVE_ASPECT  1.3333334f
#endif

/* The watch pages' 3D content (inventory weapon, controller model) is
 * triangle-based, so on widescreen frames widen the authored aspect by
 * portFrameWideFactor to keep it inside the centred 4:3 band. The factor is 1.0
 * on pinned frames (frontend watch, cutscenes, 4:3 window), so those stay as
 * authored. */
#ifndef TARGET_N64
extern f32 portFrameWideFactor(void);
#define WATCH_FRAME_WIDE_MUL portFrameWideFactor()
#else
#define WATCH_FRAME_WIDE_MUL 1.0f
#endif
#define WATCH_PERSPECTIVE_ASPECT_RT (WATCH_PERSPECTIVE_ASPECT * WATCH_FRAME_WIDE_MUL)
#ifndef TARGET_N64
/* Gates the watch menu's held-button, stick and easing updates to once per 60
 * Hz field so they don't speed up at higher tick rates (see bondview2.c
 * portPauseBoxTick). Edge presses (joyGetButtonsPressedThisFrame) are
 * deliberately not gated. */
extern s32 portWatchMenuField(void);
#define PORT_MENU_FIELD_GATED 1
#else
#define portWatchMenuField() 1
#endif
#ifdef GE_VIDEO
void portWatchMouseTick(void);
s32 portWatchTakeClick(void);
Gfx *portWatchMouseDraw(Gfx *gdl);
extern s32 g_portWatchHoverTab;
extern s32 g_portWatchInvTextWidth;
#endif

#if defined(VERSION_EU)
#define OPTLABELS_ROW1_Y    0x5c
#define OPTLABELS_ROW2_Y    0x7a
#define OPTLABELS_ROW_PITCH 0x1e
#define OPTLABELS_COL_RET   0x5a
#define OPTLABELS_HINT_Y    0xe1
#else
#define OPTLABELS_ROW1_Y    0x52
#define OPTLABELS_ROW2_Y    0x6b
#define OPTLABELS_ROW_PITCH 0x19
#define OPTLABELS_COL_RET   0x4b
#define OPTLABELS_HINT_Y    0xc3
#endif

// bss
Mtx gfx_background_8007B0A0;
Mtx gfx_background_8007B0E0;

u32 D_80040990 = 0;
u32 watch_screen_index = 0;
#ifndef TARGET_N64
static s32 sPortWatchPendingPage = -1; /* portWatchRequestPage: page to land on at the next watch open */
#endif
u32 controller_options_index = 0;
u32 game_options_index = 0;
// data
//D:800409A0
s32 mission_brief_index = BRIEF_INDEX_OBJECTIVES;
//D:800409A4
s32 D_800409A4 = 0;
//D:800409A8
s32 watch_item_is_actively_selected = 0;
//D:800409AC
s32 D_800409AC = 0;
//D:800409B0
s32 watch_inventory_text_y = 0;
//D:800409B4
s32 watch_inventory_text_target_y = 0;
//D:800409B8
s32 g_curWatchItemIndex = 0;
//D:800409BC
f32 watch_inventory_cursor_pos = 0.0f;
//D:800409C0
bool watch_inventory_text_is_settled = FALSE;
//D:800409C4
s32 D_800409C4 = 0;
//D:800409C8
f32 D_800409C8 = 0.0f;
//D:800409CC
f32 D_800409CC = 0.0f;
//D:800409D0
s32 D_800409D0 = -1;
//D:800409D4
f32 D_800409D4 = 0.0f;
//D:800409D8
s32 D_800409D8 = 8;
//D:800409DC
u16 game_control_styles[] = {
    /*1.1 honey*/    getStringID(LOPTIONS, OPTION_STR_09_11HONEY_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF), /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF),
    /*1.2 solitaire*/getStringID(LOPTIONS, OPTION_STR_0A_12SOLITAIRE_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF), /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF),
    /*1.3 kissy*/    getStringID(LOPTIONS, OPTION_STR_0B_13KISSY_LF),   /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF),  /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF),
    /*1.4 goodnight*/getStringID(LOPTIONS, OPTION_STR_0C_14GOODNIGHT_LF),   /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF),  /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF),
    /*2.1 plenty*/   getStringID(LOPTIONS, OPTION_STR_0D_21PLENTY_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),
    /*2.2 galore*/   getStringID(LOPTIONS, OPTION_STR_0E_22GALORE_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),
    /*2.3 domino*/   getStringID(LOPTIONS, OPTION_STR_0F_23DOMINO_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),
    /*2.4 goodhead*/ getStringID(LOPTIONS, OPTION_STR_10_24GOODHEAD_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF)
};

struct game_options game_options_entries[] = {
    { {getStringID(LOPTIONS, OPTION_STR_11_LOOKUPDOWN_LF), getStringID(LOPTIONS, OPTION_STR_1C_REVERSE_LF), getStringID(LOPTIONS, OPTION_STR_1B_UPRIGHT_LF), 0}, 0}, //look up/down, reverse, upright
    { {getStringID(LOPTIONS, OPTION_STR_12_AUTOAIM_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //autoaim, off, on
    { {getStringID(LOPTIONS, OPTION_STR_14_AIMCONTROL_LF), getStringID(LOPTIONS, OPTION_STR_1E_HOLD_LF), getStringID(LOPTIONS, OPTION_STR_1D_TOGGLE_LF), 0}, 0}, //aim control, hold, toggle
    { {getStringID(LOPTIONS, OPTION_STR_15_SIGHTONSCREEN_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //sight on screen, off, on
    { {getStringID(LOPTIONS, OPTION_STR_13_LOOKAHEAD_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //look ahead, off, on
    { {getStringID(LOPTIONS, OPTION_STR_16_AMMOONSCREEN_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //ammo on screen, off, on
    { {getStringID(LOPTIONS, OPTION_STR_17_SCREEN_LF), getStringID(LOPTIONS, OPTION_STR_1F_FULL_LF), getStringID(LOPTIONS, OPTION_STR_20_WIDE_LF), getStringID(LOPTIONS, OPTION_STR_21_CINEMA_LF)}, 0}, //screen, full, wide, cinema
    { {getStringID(LOPTIONS, OPTION_STR_18_RATIO_LF), getStringID(LOPTIONS, OPTION_STR_22_NORMAL_LF), getStringID(LOPTIONS, OPTION_STR_23_169_LF), 0}, 0} //ratio, normal, 16:9
};

//D:80040ADC
u32 controlstick_lr_enabled = 0;
//D:80040AE0
u32 watch_stick_y_nav_ready = 0;
//D:80040AE4
u32 watch_stick_y_prev_active = 0;
//D:80040AE8
f32 D_80040AE8 = 0.0f;
//D:80040AEC
f32 D_80040AEC = 0.0f;
//D:80040AF0
f32 D_80040AF0 = 45.0f;
//D:80040AF4
u32 D_80040AF4 = 0xFF00A0;
//D:80040AF8
u32 D_80040AF8 = 0xA;
//D:80040AFC
u32 D_80040AFC = 0xFF;

//D:80040B00
u32 D_80040B00 = 0xA;

//D:80040B04
s32 g_WatchBackgroundGreen = 0xE0;

//D:80040B08
u32 g_WatchStaticScanlineAlpha = 0;

//D:80040B0C
u32 D_80040B0C = 0xFFA0;
//D:80040B10
u32 D_80040B10 = 0xF800;
//D:80040B14
f32 D_80040B14 = 0.0f;
//D:80040B18
f32 D_80040B18 = 0.0f;
//D:80040B1C
f32 D_80040B1C = 2.5f;
//D:80040B20
f32 g_WatchControllerPitch = 0.0f;
//D:80040B24
f32 g_WatchControllerSpinAngle = 0.0f;
//D:80040B28
f32 g_WatchControllerSpinSpeed = 0.0f;
//D:80040B2C
s32 D_80040B2C = 0;
//D:80040B30
f32 D_80040B30 = 0.0f;
//D:80040B34
f32 D_80040B34 = 0.0f;
//D:80040B38
f32 D_80040B38 = 0.0f;
//D:80040B3C
s32 D_80040B3C = 0;
//D:80040B40
s32 g_WatchStaticScanlineY = 0;
//D:80040B44
u16 D_80040B44 = 0x1;
//D:80040B48
u32 D_80040B48 = 0x32;
//D:80040B4C
u32 D_80040B4C = 0x32;
//D:80040B50
u32 D_80040B50 = 0x32;
//D:80040B54
u32 D_80040B54 = 0x32;
//D:80040B58
u16 mTrack2Vol = 0x7FFF;

//D:80040B5C
coord3d g_ControllerPos = {0.0f, 200.0f, -200.0f};



//D:80040B68
// 3D positions for the buttons at the sides of the screen for the 1x control styles.
WatchContButtonPositions g_1ContButtonPositions[] = {{
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f,
    -900.0f, 200.0f, -45.0f,     // Start
    715.0f, 200.0f, 393.0f,      // Joy Stick
    -875.0f, 200.0f, -210.0f,    // D-Pad
    900.0f, 200.0f, -260.0f,     // C-Up
    900.0f, 200.0f, -160.0f,     // C-Down
    850.0f, 200.0f, -210.0f,     // C-Left
    950.0f, 200.0f, -210.0f,     // C-Right
    900.0f, 200.0f, 128.0f,      // A
    900.0f, 200.0f, -45.0f,      // B
    -820.0f, 200.0f, -389.0f,    // L
    820.0f, 200.0f, -389.0f,     // R
    -830.0f, 200.0f, 78.0f       // Z
}};

//D:80040C44
// 3D positions for the buttons of the left controller for the 2x control styles.
WatchContButtonPositions g_2ContLeftButtonPositions[] = {{
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, -600.0f, 200.0f,
    500.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, -600.0f, 200.0f,
    240.0f, -600.0f, 200.0f,
    110.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, -600.0f, 200.0f,
    320.0f
}};

//D:80040D20
// 3D positions for the buttons of the right controller for the 2x control styles.
WatchContButtonPositions g_2ContRightButtonPositions[] = {{
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    2000.0f, 0.0f, 2000.0f, 2000.0f,
    0.0f, 2000.0f, 600.0f, 200.0f,
    500.0f, 2000.0f, 0.0f, 2000.0f,
    2000.0f, 0.0f, 2000.0f, 2000.0f,
    0.0f, 2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f, 2000.0f,
    600.0f, 200.0f, 240.0f, 600.0f,
    200.0f, 110.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f, 2000.0f,
    600.0f, 200.0f, 320.0f
}};

//D:80058440
const char D_80058440[];
//D:80058444
const char D_80058444[];
//D:80058448
const char aC_2[];
//D:80058450
const char D_80058450[];
//D:80058454
const char D_80058454[];

// forward declarations

void set_page_rectangle_colors(s32 watch_screen_index, struct WatchVertex *vertices);
Gfx *draw_watch_mission_status_page(Gfx *gdl, Mtx *param_2);
Gfx *unused_draw_watch_inventory_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_inventory_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_control_options_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_game_options_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_mission_briefing_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_background_health_and_armor_transitioning(Gfx *gdl, Mtx *param_2);
Gfx *draw_background_health_and_armor(Gfx *gdl, Mtx *arg1, s32 zoom_squish);
void game_option_select_value(u32 *param_1, u32 param_2);
#ifndef TARGET_N64
void portPlayerOptsAdoptIf(const u32 *value);
#endif
void watch_adjust_volume_slider(u16* arg0);
Gfx *sub_GAME_7F0A3B40(Gfx *gdl, s32 *arg1);
void update_volume_slider_verts(struct WatchVertex *verts, f32 fill_amount, s32 transition_width);
void sub_GAME_7F0A9684(s8 contpadnum, s32 *counter, f32 *value, f32 *step);
Gfx *display_text_buttons_dual_control(Gfx *gdl);
Gfx *sub_GAME_7F0A9AB8(Gfx *gdl);

/* The control style the watch's Controls page shows: the effective style
 * (including any padN_style override), so the picture, labels and style row
 * always match what the game runs. The style row still edits the save's own
 * field, as in vanilla. */
#ifndef TARGET_N64
#define PORT_SHOWN_CTLTYPE (cur_player_get_control_type())
#else
#define PORT_SHOWN_CTLTYPE (g_CurrentPlayer->cur_player_control_type_0)
#endif

/* THE SECOND CONTROLLER THE 2.x PICTURE SHOWS: the pad the game's 2.x
 * branch reads (bondview2.c) - a slot marked "2.xp2", else the console's
 * port 2, which is playernum + playerCount = 1 in solo. */
#ifndef TARGET_N64
static s8 portWatchSecondPad(void)
{
    extern s32 portInputSecondPadIndex(s32 playernum, s32 fam);
    const s32 type = cur_player_get_control_type();
    const s32 s = (type >= CONTROLLER_CONFIG_PLENTY)
                ? portInputSecondPadIndex(get_cur_playernum(), type - CONTROLLER_CONFIG_PLENTY) : -1;

    return (s >= 0 && s < MAXCONTROLLERS) ? (s8) s : 1;
}
#define PORT_SECOND_PAD (portWatchSecondPad())
#else
#define PORT_SECOND_PAD 1
#endif

// end forward declarations



void nullsub_7F0A4860(void)
{

}


void init_watch_at_start_of_stage(void)
{
#ifndef TARGET_N64
    sPortWatchPendingPage = -1; /* a request that never got applied dies with the stage */
#endif
    watch_screen_index = WATCH_INDEX_MISSION_STATUS;
    controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
    game_options_index = GAME_OPTIONS_INDEX_MUSIC;
    mission_brief_index = BRIEF_INDEX_OBJECTIVES;
    D_800409A4 = 0;
    watch_item_is_actively_selected = 0;
    D_800409AC = 0;
    watch_inventory_text_y = 0;
    watch_inventory_text_target_y = 0;
    g_curWatchItemIndex = 0;
    watch_inventory_cursor_pos = 0.0f;
    watch_inventory_text_is_settled = FALSE;
    D_800409C4 = 0;
    D_800409C8 = 0.0f;
    D_800409CC = 0.0f;
    D_800409D0 = -1;
    D_800409D4 = 0.0f;

    if (j_text_trigger ? 1 : 0 && 1)
    {
    }

    g_CurrentPlayer->neg_vspacing_for_control_type_entry = 0;
    g_CurrentPlayer->cur_player_control_type_1 = CONTROLLER_CONFIG_HONEY;
    g_CurrentPlayer->cur_player_control_type_0 = CONTROLLER_CONFIG_HONEY;
    g_CurrentPlayer->cur_player_control_type_2 = 0.0f;
    g_CurrentPlayer->has_set_control_type_data = TRUE;
    D_800409D8 = 8;

    controlstick_lr_enabled = 0;
    watch_stick_y_nav_ready = 0;
    watch_stick_y_prev_active = 0;
    D_80040AE8 = 0.0f;
    D_80040AEC = 0.0f;
    D_80040AF0 = 45.0f;
    D_80040AF4 = 0xff00a0;
    D_80040AF8 = 10;
    D_80040AFC = 0xff;
    D_80040B00 = 10;
    g_WatchBackgroundGreen = 0xe0;
    g_WatchStaticScanlineAlpha = 0;
    D_80040B0C = 0xffa0;
    D_80040B10 = 0xf800;
    D_80040B14 = 0.0f;
    D_80040B18 = 0.0f;
    D_80040B1C = 2.5f;
    g_WatchControllerPitch = 0.0f;
    g_WatchControllerSpinAngle = 0.0f;
    g_WatchControllerSpinSpeed = 0.0f;
    D_80040B2C = 0;
    D_80040B30 = 0.0f;
    D_80040B34 = 0.0f;
    D_80040B38 = 0.0f;
    D_80040B3C = 0;
    g_WatchStaticScanlineY = 0;
    D_80040B44 = 1;
    D_80040B48 = 0x32;
    D_80040B4C = 0x32;
    D_80040B50 = 0x32;
    D_80040B54 = 0x32;
    fileLoadSaveSettingsForSelectedFolder();
    mission_failed_or_aborted = FALSE;
}


void controller_deadzone_related(void)
{
    if (10 < joyGetStickX(PLAYER_1))
    {
        D_80040B50 = D_80040B50 + 1;
    }
    if (joyGetStickX(PLAYER_1) < -10)
    {
        D_80040B50 = D_80040B50 + -1;
    }
    if (10 < joyGetStickY(PLAYER_1))
    {
        D_80040B54 = D_80040B54 + -1;
    }
    if (joyGetStickY(PLAYER_1) < -10)
    {
        D_80040B54 = D_80040B54 + 1;
    }
}


Gfx * sub_GAME_7F0A4B40(Gfx *DL)
{
    if (10 < joyGetStickX(PLAYER_1))
    {
        D_80040B48 += 1;
    }

    if (joyGetStickX(PLAYER_1) < -10)
    {
        D_80040B48 -= 1;
    }

    if (10 < joyGetStickY(PLAYER_1))
    {
        D_80040B4C -= 1;
    }

    if (joyGetStickY(PLAYER_1) < -10)
    {
        D_80040B4C += 1;
    }

    gDPSetRenderMode(DL++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(DL++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(DL++, 0, 0, 0xFF, 0x00, 0x00, 0xFF);
    gDPFillRectangle(DL++, D_80040B48, D_80040B4C, D_80040B48+1, D_80040B4C+1);

    {
        u8 buffer [0x12];
        struct font * pFontFile;
        struct fontchar * pFontChars;
        s32 y;
        s32 x;

        pFontFile = ptrFontBankGothic;
        pFontChars = ptrFontBankGothicChars;
        sprintf(buffer,"%d, %d\n",D_80040B48,D_80040B4C);

        DL = microcode_constructor(DL++);

        textMeasure(&x, &y, buffer, pFontChars, pFontFile, 0);
        DL = textRender(DL, &D_80040B48, &D_80040B4C, buffer, pFontChars, pFontFile, 0xff0000ff, y, x, 0, 0);
        D_80040B4C = (D_80040B4C - pFontChars[91].height) + 1;
    }

    return DL;
}


u32 controllerCheckDualControllerTypesAllowed(void)
{
    if (joyGetControllerCount() >= 2)
    {
        if (cur_player_get_control_type() >= 4)
        {
            return 1;
        }
    }
    return 0;
}

#ifndef TARGET_N64
/* Logs each player's effective control style and its source whenever it
 * changes, since a padN_style override is otherwise invisible in the menu.
 * Called from the hot path; the compare keeps it silent when nothing changed. */
static void portStyleSay(int type, const char *why)
{
    static const char *const names[8] = { "1.1 Honey", "1.2 Solitaire", "1.3 Kissy", "1.4 Goodnight",
                                          "2.1 Plenty", "2.2 Galore", "2.3 Domino", "2.4 Goodhead" };
    static s32 said[4] = { -1, -1, -1, -1 };
    static const char *saidwhy[4];
    extern int printf(const char *, ...);
    const s32 pn = get_cur_playernum();

    if (pn < 0 || pn > 3 || (said[pn] == type && saidwhy[pn] == why))
    {
        return;
    }
    said[pn] = type;
    saidwhy[pn] = why;
    printf("ge: input: player %d control style = %s (%s)\n", (int) pn + 1,
                 (type >= 0 && type < 8) ? names[type] : "?", why);
}
#endif

int cur_player_get_control_type(void){
#ifndef TARGET_N64
    /* Twin-stick auto: a 1.x style on a pad that has a Look stick plays as 2.2
     * Galore (true analog move/look, fire on RT) without changing the saved
     * option. Real or N64-shaped pads have no Look stick and keep their 1.x
     * style, and an explicitly chosen 2.x style is left alone. */
    {
        extern s32 portJoyTwinStickCapable(s32 pad);
        /* padN_style: the player picked a style in Options > CONTROL, and a
         * pick beats both the saved style and the promotion below. -1 = auto,
         * which is the default and leaves every line under it untouched.
         * docs/port/port-inputs-vs-n64-inputs.md */
        extern s32 portInputStyleGet(s32 slot);
        extern s32 portInputPadIsN64(s32 slot);
        extern s32 get_is_ramrom_flag(void); /* ramromreplay.c */
        const int chosen = portInputStyleGet(get_cur_playernum());
        int type = g_CurrentPlayer->cur_player_control_type_0;

        /* Only applies in N64 input mode, like the menu row that sets it; port
         * mode ignores padN_style without clearing it. Also off during demo
         * playback, which must run the style it was recorded with or the
         * attract demos desync. */
        extern s32 portInputSecondPadIndex(s32 playernum, s32 fam);
        /* A 2.x style at 3-4 players needs a second controller (a right stick
         * or a slot marked 2.xp2), otherwise strafe, look and aim read a
         * non-existent port and go dead. Without one the pick is ignored and
         * Rare's clamped MP style plays instead. */
        const int nosecond = chosen >= CONTROLLER_CONFIG_PLENTY && getPlayerCount() >= 3
                             && !portJoyTwinStickCapable(get_cur_playernum())
                             && portInputSecondPadIndex(get_cur_playernum(), chosen - CONTROLLER_CONFIG_PLENTY) < 0;

        if (chosen >= 0 && !nosecond && portInputPadIsN64(get_cur_playernum()) && get_is_ramrom_flag() == 0)
        {
            portStyleSay(chosen, "chosen for this device in Options (padN_style / raphnet<chan>_style)");
            return chosen;
        }
        /* Not for a port-driven player: bondviewProcessInput already feeds
         * their right stick directly, so promoting to 2.x would apply it twice;
         * also not during demo playback (see
         * docs/port/port-inputs-vs-n64-inputs.md). */
        if (type >= CONTROLLER_CONFIG_HONEY && type < CONTROLLER_CONFIG_PLENTY
            && portJoyTwinStickCapable(get_cur_playernum())
            && portInputPadIsN64(get_cur_playernum())
            && get_is_ramrom_flag() == 0)
        {
            portStyleSay(CONTROLLER_CONFIG_GALORE, "twin-stick promotion of a 1.x save");
            return CONTROLLER_CONFIG_GALORE; /* 2.2 */
        }
        /* A port-driven player always plays 1.1 or 1.2, since port layouts are
         * one controller (Z = fire, L/R = aim, A = inventory) and other styles
         * left keyboard and modern pads unable to fire. Live only (file2.c
         * saves the raw field) and skipped during demo playback (see
         * docs/port/port-inputs-vs-n64-inputs.md). */
        if (!portInputPadIsN64(get_cur_playernum()) && get_is_ramrom_flag() == 0)
        {
            const int played = (type == CONTROLLER_CONFIG_SOLITARE || type == CONTROLLER_CONFIG_GOODNIGHT
                                || type == CONTROLLER_CONFIG_GALORE || type == CONTROLLER_CONFIG_GOODHEAD)
                             ? CONTROLLER_CONFIG_SOLITARE : CONTROLLER_CONFIG_HONEY;

            portStyleSay(played, (played == type) ? "the save file's - port mode drives movement and look"
                                                  : "the save's style folded to 1.1 / 1.2 - port mode keeps fire on z, aim on L / R");
            return played;
        }
        portStyleSay(type, (get_is_ramrom_flag() == 0) ? "the save file's"
                                                       : "the recording's own - a demo is playing");
        return type;
    }
#else
  return g_CurrentPlayer->cur_player_control_type_0;
#endif
}


void cur_player_set_control_type(int type)
{
    int langsize;

    g_CurrentPlayer->cur_player_control_type_0 = type;
    g_CurrentPlayer->cur_player_control_type_1 = type;
    g_CurrentPlayer->cur_player_control_type_2 = (float)type;

    langsize = j_text_trigger ? 14 : 10;

    g_CurrentPlayer->neg_vspacing_for_control_type_entry = -(langsize * type);
    g_CurrentPlayer->has_set_control_type_data = TRUE;

}

#ifndef TARGET_N64
/** PORT: look up/down for one player (1 = upright, 0 = reverse), using their own
 * [input] padN_look_updown from the MP Player Options if set, else the global
 * option. Used by the panel row, the profiles, and the game when two or more
 * players are active. */
s32 portPlayerLookUpright(s32 player)
{
    extern s32 portJoyPortPadOptGet(s32 pad, s32 which); /* joy_port.c: which 2 = own look value, -1 none */
    const s32 own = portJoyPortPadOptGet(player, 2);

    return (own >= 0) ? own : (s32) game_options_entries[PLAYER_OPTION_LOOK].current_value;
}

/**
 * PORT: Look Ahead as it stands for ONE player - their own [input]
 * padN_look_ahead from the MP setup's Player Options, else
 * the global option. Same rule as portPlayerLookUpright: read by the game
 * with two or more players only.
 */
s32 portPlayerAutoAim(s32 player)
{   /* PORT: Auto-Aim as it stands for ONE player - their own [input]
     * padN_auto_aim from the MP setup's Player Options, else
     * the global option. Read for multiplayer only (front.c). */
    extern s32 portJoyPortPadOptGet(s32 pad, s32 which); /* joy_port.c: which 4 = own auto-aim, -1 none */
    const s32 own = portJoyPortPadOptGet(player, 4);

    return (own >= 0) ? own : (s32) game_options_entries[PLAYER_OPTION_AUTOAIM].current_value;
}

s32 portPlayerLookAhead(s32 player)
{
    extern s32 portJoyPortPadOptGet(s32 pad, s32 which); /* joy_port.c: which 3 = own look ahead, -1 none */
    const s32 own = portJoyPortPadOptGet(player, 3);

    return (own >= 0) ? own : (s32) game_options_entries[PLAYER_OPTION_LOOKAHEAD].current_value;
}
#endif

u32 get_cur_player_look_vertical_inverted(void)
{
#ifndef TARGET_N64
    /* Rare kept one look setting for everyone; with two or more players each
     * now has their own. Not in a solo game, so multiplayer choices don't leak
     * into single player. */
    if (getPlayerCount() >= 2)
    {
        return (u32) portPlayerLookUpright(get_cur_playernum());
    }
#endif
    return game_options_entries[PLAYER_OPTION_LOOK].current_value;
}

void set_cur_player_look_vertical_inverted(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_LOOK].current_value = param_1;
}

s32 cur_player_get_autoaim(void)
{
    return game_options_entries[PLAYER_OPTION_AUTOAIM].current_value;
}

void cur_player_set_autoaim(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_AUTOAIM].current_value = param_1;
}

u32 cur_player_get_lookahead(void)
{
#ifndef TARGET_N64
    /* each player their own with two or more players, as look up/down above */
    if (getPlayerCount() >= 2)
    {
        return (u32) portPlayerLookAhead(get_cur_playernum());
    }
#endif
    return game_options_entries[PLAYER_OPTION_LOOKAHEAD].current_value;
}

void cur_player_set_lookahead(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_LOOKAHEAD].current_value = param_1;
}

u32 cur_player_get_aim_control(void)
{
    return game_options_entries[PLAYER_OPTION_AIM].current_value;
}

void cur_player_set_aim_control(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_AIM].current_value = param_1;
}

u32 cur_player_get_sight_onscreen_control(void)
{
    return game_options_entries[PLAYER_OPTION_SIGHT].current_value;
}
void cur_player_set_sight_onscreen_control(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_SIGHT].current_value = param_1;
}

u32 cur_player_get_ammo_onscreen_setting(void)
{
    return game_options_entries[PLAYER_OPTION_AMMODISPLAY].current_value;
}
void cur_player_set_ammo_onscreen_setting(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_AMMODISPLAY].current_value = param_1;
}

u32 cur_player_get_screen_setting(void)
{
    return game_options_entries[PLAYER_OPTION_SCREEN].current_value;
}
void cur_player_set_screen_setting(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_SCREEN].current_value = param_1;
}

SCREEN_RATIO_OPTION get_screen_ratio(void)
{
    return game_options_entries[PLAYER_OPTION_RATIO].current_value;
}

void set_screen_ratio(SCREEN_RATIO_OPTION ratio_option)
{
    game_options_entries[PLAYER_OPTION_RATIO].current_value = ratio_option;
}

#ifndef TARGET_N64
/** PORT "HUD Centering" (see options.h): TRUE when edge HUD elements should
 * anchor to their viewport's edges (window edges in solo/2P, each quadrant's
 * edges in 3P/4P, where they always anchor). GE_HUDEDGES=1/0 overrides the menu
 * setting for headless A/B captures. */
/* the classic pillars (bondview2.c) are wanted: the HUD slides into the 4:3
 * picture between them, with them, instead of sitting under them */
static s32 portClassicPillarsUp(void)
{
    extern s32 portClassicPillars(void);   /* video.c; weak 0 headless */

    return portClassicPillars() && getPlayerCount() == 1;
}

/* THE HUD SLIDE: the edge HUD and the health arcs do not jump between 4:3
 * and Wide; each has a 0..1 position (0 = the 4:3 band, 1 = the edges) that
 * eases to its setting over half a second (portHudSlideTick, once a frame in
 * play). hudAnchorsToEdges / healthAnchorsToEdges say whether to anchor at
 * all, and the anchor carries how far (PORT_HUD_ANCHOR, gfx_pc.cpp); the
 * arcs blend their orthos (bondview2.c). Back in play after the menus, they
 * start at their settings. */
static s32 hudWantsEdges(void);
static s32 healthWantsEdges(void);
static s32 hudWantsWide(void);
static s32 healthWantsWide(void);

/* While the classic pillars move, a Wide HUD rides with them: its slide is
 * the uncovered share of the edge band (1 - pillar progress). The pillar
 * width and the HUD offset are both linear in that band, so the gap between
 * the HUD and the pillar edge stays the same all the way, instead of the two
 * easing on their own curves one after the other. -1 = pillars still. */
static f32 portHudSlidePillarLed(s32 wide)
{
    extern f32 portClassicPillarsProgress(void);   /* bondview2.c */
    const f32 cp = portClassicPillarsProgress();

    return (wide && cp > 0.0f && cp < 1.0f) ? 1.0f - cp : -1.0f;
}
static f32 g_portHudSlide = -1.0f;      /* -1 = not ticked yet: start at the setting */
static f32 g_portHealthSlide = -1.0f;

static f32 portHudSlideEase(f32 t)
{
    return t * t * (3.0f - 2.0f * t);   /* smoothstep */
}

void portHudSlideTick(void)
{
    extern unsigned int g_PortFramesPumped;
    extern f32 portTickDelta;   /* frametiming.c: this tick's field delta */
    static unsigned int last;
    const f32 step = portTickDelta / 30.0f;   /* half a second end to end */
    const f32 hud = hudWantsEdges() ? 1.0f : 0.0f;
    const f32 health = healthWantsEdges() ? 1.0f : 0.0f;

    if (g_portHudSlide < 0.0f || g_PortFramesPumped - last > 2u)
    {   /* the first tick, or back in play after the menus: no slide */
        g_portHudSlide = hud;
        g_portHealthSlide = health;
    }
    last = g_PortFramesPumped;
    /* step toward the setting and stop on it: stepping away from it at rest made
     * a Wide HUD bounce between 1 and 1 - step every other tick */
    if (hud > g_portHudSlide) { g_portHudSlide = (g_portHudSlide + step < hud) ? g_portHudSlide + step : hud; }
    else { g_portHudSlide = (g_portHudSlide - step > hud) ? g_portHudSlide - step : hud; }
    if (health > g_portHealthSlide) { g_portHealthSlide = (g_portHealthSlide + step < health) ? g_portHealthSlide + step : health; }
    else { g_portHealthSlide = (g_portHealthSlide - step > health) ? g_portHealthSlide - step : health; }
    if (g_portHudSlide < 0.0f) { g_portHudSlide = 0.0f; }
    if (g_portHudSlide > 1.0f) { g_portHudSlide = 1.0f; }
    if (g_portHealthSlide < 0.0f) { g_portHealthSlide = 0.0f; }
    if (g_portHealthSlide > 1.0f) { g_portHealthSlide = 1.0f; }
}

/** How far the edge HUD sits toward the edges, eased: 0 = 4:3, 1 = Wide. */
f32 portHudSlide(void)
{
    const f32 led = portHudSlidePillarLed(hudWantsWide());

    if (led >= 0.0f)
    {
        return led;
    }
    return portHudSlideEase((g_portHudSlide < 0.0f) ? (hudWantsEdges() ? 1.0f : 0.0f) : g_portHudSlide);
}

/** The same for the health and armour arcs. */
f32 portHealthSlide(void)
{
    const f32 led = portHudSlidePillarLed(healthWantsWide());

    if (led >= 0.0f)
    {
        return led;
    }
    return portHudSlideEase((g_portHealthSlide < 0.0f) ? (healthWantsEdges() ? 1.0f : 0.0f) : g_portHealthSlide);
}

/** PORT_HUD_ANCHOR's w1 bits for the slide: present flag + 8-bit fraction */
u32 portHudAnchorFrac(void)
{
    return 0x01000000u | ((u32) (portHudSlide() * 255.0f + 0.5f) << 16);
}

/* HUD SCALE (midas.ini [display] hud_scale, video.c portHudScaleActive): the
 * in-game HUD drawn as on a 640x480 canvas. Each element is bracketed with the
 * screen point its layout is measured from (a viewport corner or edge, the
 * aim point for the sight), and gfx_pc.cpp draws its 2D rects at half size
 * about it, so the layout keeps its places and the elements get smaller. */
s32 portHudScaleOn(void)
{
    extern s32 portHudScaleActive(void);   /* video.c; weak 0 headless */

    /* the Cuba credits keep their full-size text */
    return bossGetStageNum() != LEVELID_CUBA && portHudScaleActive();
}

/** Opens a HUD element's bracket at pivot (x, y) in screen units; nothing when off. */
Gfx *portHudScaleBegin(Gfx *gdl, f32 x, f32 y)
{
    s32 x4 = (s32) (x * 4.0f + 0.5f);
    s32 y4 = (s32) (y * 4.0f + 0.5f);

    if (!portHudScaleOn())
    {
        return gdl;
    }
    x4 = (x4 < 0) ? 0 : (x4 > 0xFFE) ? 0xFFE : x4;
    y4 = (y4 < 0) ? 0 : (y4 > 0xFFE) ? 0xFFE : y4;
    gDPNoOpTag(gdl++, 0x58000000u | ((u32) x4 << 12) | (u32) y4);
    return gdl;
}

Gfx *portHudScaleEnd(Gfx *gdl)
{
    if (!portHudScaleOn())
    {
        return gdl;
    }
    gDPNoOpTag(gdl++, 0x58FFFFFFu);
    return gdl;
}

s32 hudAnchorsToEdges(void)
{
    return portHudSlide() > 0.0f;
}

static s32 hudWantsEdges(void)
{
    return hudWantsWide() && !portClassicPillarsUp();
}

/** The HUD's own Wide wish, the classic pillars aside. */
static s32 hudWantsWide(void)
{
    extern f32 portFrameWideFactor(void);
    extern char *getenv(const char *);
    static s32 env = -1;
    s32 wants;

    if (env < 0)
    {
        const char *e = getenv("GE_HUDEDGES");
        env = (e != NULL) ? ((e[0] != '0') ? 1 : 2) : 0;
    }
    {   /* 2+ players: Multiplayer Options' own HUD Layout (midas.ini mp_hud_wide);
         * the watch's "hud" row (Rare's ratio) decides solo */
        extern s32 portMpHudWide(void) __attribute__((weak));   /* video.c; absent headless = 4:3 */
        const s32 wide = (getPlayerCount() >= 2) ? ((portMpHudWide != NULL) && portMpHudWide())
                                                 : (get_screen_ratio() == SCREEN_RATIO_16_9);

        wants = (env == 1) ? TRUE : (env == 2) ? FALSE
            : (wide || getPlayerCount() >= 3);
    }

    return wants
        && (portFrameWideFactor() > 1.001f);
}

/** The radar's anchor: Wide hugs the viewport's right edge with the ammo and
 * names, 4:3 uses the centred band. Unlike hudAnchorsToEdges, 3P/4P quadrants
 * also follow the HUD Layout row here. */
s32 hudLayoutWide(void)
{
    extern s32 portMpHudWide(void) __attribute__((weak));   /* video.c */

    return hudAnchorsToEdges()
        && (getPlayerCount() < 3 || ((portMpHudWide != NULL) && portMpHudWide()));
}

/** Separate Wide/4:3 setting for the health and armour arcs (midas.ini [display]
 * health_wide), so the ammo and messages can sit at the edges with the health
 * popup centred. Same window test as hudAnchorsToEdges; GE_HUDEDGES still
 * forces both. */
s32 portHealthIsWide(void)
{
    extern s32 portHealthWide(void) __attribute__((weak));   /* video.c; absent headless = 4:3 */

    return (portHealthWide != NULL) ? portHealthWide() : 0;
}

s32 healthAnchorsToEdges(void)
{
    return portHealthSlide() > 0.0f;
}

static s32 healthWantsEdges(void)
{
    return healthWantsWide() && !portClassicPillarsUp();
}

/** The health arcs' own Wide wish, the classic pillars aside. */
static s32 healthWantsWide(void)
{
    extern f32 portFrameWideFactor(void);
    extern char *getenv(const char *);
    static s32 env = -1;
    s32 wants;

    if (env < 0)
    {
        const char *e = getenv("GE_HUDEDGES");
        env = (e != NULL) ? ((e[0] != '0') ? 1 : 2) : 0;
    }
    wants = (env == 1) ? TRUE : (env == 2) ? FALSE
        : (portHealthIsWide() || getPlayerCount() >= 3);

    return wants
        && (portFrameWideFactor() > 1.001f);
}
#endif

/* the vanilla per-target side effects of the L/R page switches. Kept
 * unconditional (not GE_VIDEO-gated): pure page-navigation/state logic,
 * no mouse or renderer dependency - tested headless in 2P. Only the watch-menu
 * MOUSE code below is genuinely video-only. */
static void portWatchGotoPage(s32 page)
{
    static const f32 zooms[WATCH_NUMBER_SCREENS] = { WATCHZOOM2, WATCHZOOM1, WATCHZOOM3, WATCHZOOM3, WATCHZOOM1 };
    s32 from = (s32) watch_screen_index;

    if (page == from || page < 0 || page >= WATCH_NUMBER_SCREENS)
    {
        return;
    }
#ifndef TARGET_N64
    /* Leaving a tab tears down its modal state first (open drop-down, armed key
     * capture, unaccepted resolution or display mode). Otherwise a mouse tab
     * click carried an open Video list onto the Control tab, where it invisibly
     * took input and applied its value. */
    portWatchResetPage();
#endif
    reset_watch_item_is_actively_selected();
    watch_screen_index = page;
    switch (page)
    {
        case WATCH_INDEX_MISSION_STATUS:   zero_D_800409A4(); break;
        case WATCH_INDEX_CONTROL_OPTIONS:  reset_controller_options_index(); set_controlstick_lr_disabled(); break;
        case WATCH_INDEX_GAME_OPTIONS:     reset_game_options_index(); set_controlstick_lr_disabled(); break;
        default: break;
    }
    if (zooms[page] != zooms[from])
    {
        sub_GAME_7F0A5210();
        trigger_watch_zoom(zooms[page], 15.0f);
    }
    else
    {
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
    }
}

/* bondview2.c: the Port watch_objectives bind opens the watch on a given
 * page. PENDING, not immediate: trigger_solo_watch_menu's open sequence
 * calls sub_GAME_7F0A69A8, which forces the page back to Mission Status
 * (otherwise it just pauses), so the flip is applied on the first
 * open tick (sub_GAME_7F0A6A80) with the L/R page-flip routine = zoom and
 * selection state follow. */

void portWatchRequestPage(s32 page)
{
    sPortWatchPendingPage = page;
}
static void portWatchApplyPendingPage(void)
{
    if (sPortWatchPendingPage >= 0)
    {
        portWatchGotoPage(sPortWatchPendingPage);
        sPortWatchPendingPage = -1;
    }
}

/* Right edge of a settings row in virtual px, shared by portWatchValueColumns
 * and the mouse hit test. Keep them the same: a gap let clicks fall through to
 * portWatchMouseTail, which flipped the watch to the next page. */
#define PORT_WATCH_ROW_RIGHT 305
/* game_options_index 0 and 1 are the volume sliders and the rows start at 2;
 * sub-pages keep that base and never focus the sliders, so one index serves all
 * three pages. Defined up here because the mouse layer needs it too. */
#define PORTWATCH_ROW_BASE 2
/* Raphnet page rows top to bottom: Rare's control style, the port's input
 * device picker, then Rare's controller row one row lower. Shared here by the
 * mouse layer, debug boxes, drawer and device row. */
#define PORTWATCH_STYLEROW_Y 0x1A
#define PORTWATCH_DEVROW_Y   0x2B
#define PORTWATCH_CTLROW_Y   0x3C
/* two seconds at the 60 Hz menu field: how long ANY controller button must
 * be held on a bind row to clear it */
#define PORTWATCH_UNBIND_FIELDS 120   /* two seconds of 60 Hz fields */
/* Value columns copied from the vanilla drawer so port rows line up with
 * Rare's; the mouse layer also uses them to decide which value was clicked.
 * Kept outside the GE_VIDEO guard because the drawer is built in any non-N64
 * build, including ones with no renderer. */
#define PORTWATCH_VAL3_X1 0xB4
#define PORTWATCH_VAL3_X2 0xE1
#define PORTWATCH_VAL3_X3 0x10E
#define PORTWATCH_VAL2_X1 0xC8
#define PORTWATCH_VAL2_X2 0xFA
/* a text row's hit band around its authored y; the drop-down drawer and the
 * scroll arrows (outside GE_VIDEO, like the columns above) line up on it too */
#define PORT_WATCH_ROW_ABOVE 3
#define PORT_WATCH_ROW_BELOW 11

#ifdef GE_VIDEO
#include <stdio.h>
#include "image_bank.h"
#include "othermodemicrocode.h"
#include "bondwalk2.h"
/*
 * WATCH MENU MOUSE: full mouse control of the pause watch.
 *
 * The crosshair follows the mouse in native 320x240 band space, the same
 * mapping as the frontend cursor. Page tabs and the Music/FX bars are 3D
 * watch-face quads, so bondview2.c portWatchProjectFace projects their model
 * rects through the frame's hand matrix and perspective, and the hit boxes
 * track every zoom level. Text rows are hit-tested in 2D at their authored y.
 *
 * Clicks come from the raw left button (video.c). The same click also arrives
 * as the bound Z, so anything the mouse layer consumes sets sPortWatchAteClick
 * and draw_watch_current_page skips its Z|A toggle once. GE_WATCHMOUSEDBG=1
 * overlays the computed hit boxes.
 */
extern s32 portMouseWatchPos(f32 *nx, f32 *ny, s32 *moved);
extern s32 portMouseClickPressed(void);
extern s32 portMouseClickHeld(void);
extern s32 portMouseWheelSteps(void);
extern u16 portMouseBindMask(void);
extern s32 portJoyPortWatchStep(void);
extern s32 portJoyPortWatchStepUpDown(void);
extern void portWatchProjectFace(f32 mx, f32 mz, f32 *nx, f32 *ny);
extern void portMouseMenuWarp(f32 gx, f32 gy, f32 left, f32 top, f32 width, f32 height);
extern void portMouseMenuPark(f32 gx, f32 gy, f32 cx, f32 cy, f32 left, f32 top, f32 width, f32 height);
extern char *getenv(const char *);

s32 g_portWatchHoverTab = -1;        /* tab under the crosshair, -1 none (set_page_rectangle_colors) */
s32 g_portWatchMouseOpen;            /* watch menu taking mouse input (video.c mutes mouse-driven L/R) */
extern s32 portMouseAimPressed(void);   /* video.c: the right-click (aim-bound mouse code) edge - B in the watch */
s32 g_portWatchInvTextWidth = 100;   /* inventory list width measured by the draw */
static f32 sPortWatchMouseX = 160.0f, sPortWatchMouseY = 120.0f;
static s32 sPortWatchCursorShown;    /* crosshair visible: set by mouse activity, cleared by pad/keyboard input */
static s32 sPortWatchArmed;          /* first tick of this pause done (cursor centred) */
static s32 sPortWatchAteClick;       /* this frame's click consumed: skip the Z|A toggle once */
static s32 sPortWatchDrag;           /* 0 none, 1 music bar, 2 fx bar */
static s32 sPortWatchDbg = -1;

#define PORT_WATCH_TAB_Z0 0x136
#define PORT_WATCH_TAB_Z1 (0x136 + 0x14)

static s32 portWatchDbg(void)
{
    if (sPortWatchDbg < 0)
    {
        sPortWatchDbg = getenv("GE_WATCHMOUSEDBG") != NULL;
    }
    return sPortWatchDbg;
}

/* bounding box (native px) of the watch-face model rect x0..x1, z0..z1 */
static void portWatchFaceBox(f32 x0, f32 z0, f32 x1, f32 z1, f32 *l, f32 *t, f32 *r, f32 *b)
{
    f32 px[4], py[4];
    s32 i;
    portWatchProjectFace(x0, z0, &px[0], &py[0]);
    portWatchProjectFace(x1, z0, &px[1], &py[1]);
    portWatchProjectFace(x0, z1, &px[2], &py[2]);
    portWatchProjectFace(x1, z1, &px[3], &py[3]);
    *l = *r = px[0];
    *t = *b = py[0];
    for (i = 1; i < 4; i++)
    {
        if (px[i] < *l) *l = px[i];
        if (px[i] > *r) *r = px[i];
        if (py[i] < *t) *t = py[i];
        if (py[i] > *b) *b = py[i];
    }
}

static void portWatchTabBox(s32 n, f32 *l, f32 *t, f32 *r, f32 *b)
{
    f32 x0 = (f32) (WATCH_SCREEN_SELECT_RECTANGLE_MIN_X + n * WATCH_SCREEN_SELECT_RECTANGLE_HSTEP);
    portWatchFaceBox(x0, (f32) PORT_WATCH_TAB_Z0, x0 + (f32) WATCH_SCREEN_SELECT_RECTANGLE_WIDTH, (f32) PORT_WATCH_TAB_Z1, l, t, r, b);
}

/* row = 0 music (z -275..-255), 1 fx (z -205..-185); authored in the slider drawers */
static void portWatchSliderBox(s32 row, f32 *l, f32 *t, f32 *r, f32 *b)
{
    f32 z0 = row ? PORTWATCH_BAR_FX_Z : PORTWATCH_BAR_MUSIC_Z;
    portWatchFaceBox(PORTWATCH_BAR_X, z0, PORTWATCH_BAR_X + PORTWATCH_BAR_W, z0 + PORTWATCH_BAR_H, l, t, r, b);
    *t -= 3.0f;   /* a little slack above/below the 20-unit bar */
    *b += 3.0f;
}

/** Where the bar's fill edge travels on screen: the two ends of its midline. At
 * the current zoom this equals portWatchSliderBox's bounding box, but it stays
 * correct if the page's zoom or hand angle changes. */
static void portWatchSliderTrack(s32 row, f32 *x0, f32 *x1)
{
    f32 z = (row ? PORTWATCH_BAR_FX_Z : PORTWATCH_BAR_MUSIC_Z) + PORTWATCH_BAR_H * 0.5f;
    f32 y0, y1;

    portWatchProjectFace(PORTWATCH_BAR_X, z, x0, &y0);
    portWatchProjectFace(PORTWATCH_BAR_X + PORTWATCH_BAR_W, z, x1, &y1);
}

/**
 * Converts a click position along the bar (0..1) into the fill amount to store.
 *
 * This is not the identity, because update_volume_slider_verts does not draw
 * the fill edge at W * f. It fades the fill over a transition band of width tw
 * = 30 * (1.2 - f), placed so its left end is at (W + tw) * f - tw, its centre
 * C at (W + tw) * f, and its right end at (W + tw) * f + tw. Left uncorrected,
 * the lit part of the bar sat to the right of the crosshair.
 *
 * So solve C(f) = t * C(1) for f, which gives 30f^2 - (W + 36)f + t(W + 6) = 0.
 * Normalising by C(1) rather than W keeps both ends exact (dragging to either
 * end still gives silence and full volume) and leaves under a pixel of error in
 * the middle.
 */
static f32 portWatchBarFill(f32 t)
{
    const f32 a = 30.0f;                        /* the transition's own scale */
    const f32 b = PORTWATCH_BAR_W + 36.0f;      /* 30 * 1.2 */
    const f32 c = PORTWATCH_BAR_W + 6.0f;       /* C(1) */
    f32 disc;

    if (t <= 0.0f)
    {
        return 0.0f;
    }
    if (t >= 1.0f)
    {
        return 1.0f;
    }
    disc = b * b - 4.0f * a * c * t;
    if (disc < 0.0f)
    {
        disc = 0.0f;
    }
    return (b - sqrtf(disc)) / (2.0f * a);
}

static s32 portWatchInside(f32 x, f32 y, f32 l, f32 t, f32 r, f32 b)
{
    return x >= l && x <= r && y >= t && y <= b;
}

/* sub_GAME_7F0A8378 for an explicit index (inventory row click) */
static void portWatchEquipIndex(s32 idx)
{
    if (idx < 0 || idx >= bondinvCountTotalItemsInInv())
    {
        return;
    }
    watch_inventory_cursor_pos = (f32) idx;
    g_curWatchItemIndex = idx;
    if (getCurrentPlayerWeaponId(0) == bondinvGetTextbyInvIndex(idx))
    {
        return;
    }
    currentPlayerUnEquipWeaponWrapper(0, bondinvGetTextbyInvIndex(idx));
    currentPlayerUnEquipWeaponWrapper(1, 0);
    bondinvSetCurEquippedItem(idx);
    D_800409C4 = 10;
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
}

static void portWatchSetVolume(s32 row, f32 t)
{
    u16 v;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    v = (u16) (t * (f32) VOLUME_MAX + 0.5f);
    if (row == 0)
    {
        set_mTrack2Vol(v);
    }
    else
    {
        sndApplyVolumeAllSfxSlot(v);
    }
}

/* bondview2.c portPauseBoxTick, watch state 0: forget this pause's state */
void portWatchMouseReset(void)
{
    (void) portMouseAimPressed();   /* a right-click aim in play must not arrive as the watch's first back */
    sPortWatchArmed = 0;
    sPortWatchCursorShown = 0;
    g_portWatchMouseOpen = 0;
    sPortWatchDrag = 0;
    sPortWatchAteClick = 0;
    g_portWatchHoverTab = -1;
}

/* draw_watch_current_page: TRUE once when the mouse layer consumed the click */
s32 portWatchTakeClick(void)
{
    s32 r = sPortWatchAteClick;
    sPortWatchAteClick = 0;
    return r;
}

/* sub_GAME_7F0A6A80 (watch state 5), after the START check */
/* forward: the two halves of portWatchMouseTick that the switch calls */
static void portWatchMouseControlVanilla(f32 mx, f32 my, s32 click, s32 wheel);
static void portWatchMouseTail(f32 mx, s32 click, s32 hittab);

void portWatchMouseTick(void)
{
    f32 mx, my;
    s32 moved, click, held, wheel, i;
    s32 hittab;   /* tab under the crosshair INCLUDING the current one (g_portWatchHoverTab skips it) */
    f32 l, t, r, b;

    if (!portMouseWatchPos(&mx, &my, &moved))
    {
        return;
    }
    g_portWatchMouseOpen = 1;
    if (portInputAssignActive() || portWatchAssignSwallowing())
    {
        /* While a bind row is listening, a click is the key being bound, so no
         * row, tab or slider may act on it. The one-shot latches are still
         * drained here and on the next tick, or the click would replay and
         * re-arm the prompt. */
        extern s32 portJoyPortUseMenuPressed(s32 pad);
        extern s32 portJoyPortMenuBackPressed(s32 pad);

        (void) portMouseClickPressed();
        (void) portMouseWheelSteps();
        (void) portJoyPortUseMenuPressed(PLAYER_1);
        (void) portJoyPortMenuBackPressed(PLAYER_1);
        (void) portMouseAimPressed();
        return;
    }
    { /* watch_prev / watch_next Port binds = the D-pad left/right edge (accepted
       * on every page without the L/R-trigger debug gate) */
        s32 step = portJoyPortWatchStep();
        if (step > 0) portJoyInjectPressed(PLAYER_1, R_JPAD);
        else if (step < 0) portJoyInjectPressed(PLAYER_1, L_JPAD);
    }
    { /* watch_up / watch_down = the D-pad up/down edge, one step per press;
       * holding them repeats through portWatchHoldRepeatTick */
        s32 step = portJoyPortWatchStepUpDown();
        if (step > 0) portJoyInjectPressed(PLAYER_1, D_JPAD);
        else if (step < 0) portJoyInjectPressed(PLAYER_1, U_JPAD);
    }
    { /* USE = CONFIRM: the Port use bind is A in the watch
       * too (front.c menu_init does the same for the frontend menus) */
        extern s32 portJoyPortUseMenuPressed(s32 pad);
        if (portJoyPortUseMenuPressed(PLAYER_1))
        {
            portJoyInjectPressed(PLAYER_1, A_BUTTON);
        }
    }
    { /* MENU BACK = B here too. Only that bind: the gadget buttons back out
       * of the frontend but have never done so in the watch. */
        extern s32 portJoyPortMenuBackPressed(s32 pad);
        if (portJoyPortMenuBackPressed(PLAYER_1))
        {
            portJoyInjectPressed(PLAYER_1, B_BUTTON);
        }
    }
    { /* Right-click acts as B, like the folder menus: it closes an open list or
       * steps a sub-page back, but never closes the watch (START does). Not
       * while a bind capture is armed, where the right button is the key being
       * bound. */
        if (portMouseAimPressed() && !portWatchBindCaptureActive())
        {
            portJoyInjectPressed(PLAYER_1, B_BUTTON);
        }
    }
    {   /* When the console is open the pointer is ignored too, not just buttons
         * and wheel, so hover can't move the highlight underneath it. The
         * crosshair stays put and the one-shot latches are drained. */
        extern s32 portConIsOpen(void);

        if (portConIsOpen())
        {
            (void) portMouseClickPressed();
            (void) portMouseWheelSteps();
            g_portWatchHoverTab = -1;
            return;
        }
    }
    {   /* With mouse look off ([input] mouse=0) input_pc.c stops sampling the
         * pointer, so its position is frozen and hit tests would land in the
         * wrong place. No pointer means no crosshair and no hit tests; the
         * keyboard and pad binds still work. */
        extern s32 portOptFind(const char *key);
        extern s32 portOptGet(s32 i);
        const s32 mo = portOptFind("mouse");

        if (mo >= 0 && portOptGet(mo) == 0)
        {
            (void) portMouseClickPressed();
            (void) portMouseWheelSteps();
            sPortWatchCursorShown = 0;
            g_portWatchHoverTab = -1;
            sPortWatchDrag = 0;
            return;
        }
    }
    if (!sPortWatchArmed)
    {
        /* gameplay mouse-look leaves the virtual pointer wherever it drifted
         * (usually clamped at an edge): start every pause centred */
        sPortWatchArmed = 1;
        portMouseMenuWarp(160.0f, 120.0f, 0.0f, 0.0f, 320.0f, 240.0f);
        portMouseWatchPos(&mx, &my, &moved);
        moved = 0;
        sPortWatchCursorShown = 0;
        (void) portMouseClickPressed();
        (void) portMouseWheelSteps();
    }
    { /* Clamp the crosshair to the 4:3 band (12 px inset) and park the OS
       * pointer at the clamp when it turns back, so reversing responds
       * immediately. A pointer moving outward or resting is left alone so it
       * can leave the window (see portMouseMenuPark in input_pc.c). */
        f32 cx = mx, cy = my;
        if (cx < 12.0f) cx = 12.0f;
        if (cx > 308.0f) cx = 308.0f;
        if (cy < 12.0f) cy = 12.0f;
        if (cy > 228.0f) cy = 228.0f;
        if (cx != mx || cy != my)
        {
            portMouseMenuPark(mx, my, cx, cy, 0.0f, 0.0f, 320.0f, 240.0f);
            mx = cx;
            my = cy;
        }
    }
    sPortWatchMouseX = mx;
    sPortWatchMouseY = my;
    click = portMouseClickPressed();
    held = portMouseClickHeld();
    wheel = portMouseWheelSteps();
    { /* Debug: GE_WATCHWHEEL=n[,poll] injects n wheel notches once at that poll
       * (default 2100) to test the wheel handler below. */
        static s32 parsed = -1;
        static s32 wn, wpoll = 2100;
        static u32 calls;
        extern char *getenv(const char *);
        if (parsed < 0)
        {
            const char *e = getenv("GE_WATCHWHEEL");
            parsed = 0;
            if (e != NULL && sscanf(e, "%d,%d", &wn, &wpoll) >= 1)
            {
                parsed = 1;
            }
        }
        calls++;
        if (parsed && calls == (u32) wpoll)
        {
            wheel += wn;
            fprintf(stderr, "ge: watchwheel: injected %d notches\n", (int) wn);
        }
    }
    /* The crosshair appears on any mouse activity and is dropped the
     * moment the pad / keyboard is used (button edge not driven by a mouse
     * bind, or the stick past the menu deadzone) */
    if (moved || click || wheel)
    {
        sPortWatchCursorShown = 1;
    }
    {
        u16 nonmouse = joyGetButtonsPressedThisFrame(PLAYER_1, 0xFFFF) & (u16) ~portMouseBindMask();
        s32 sx = joyGetStickX(PLAYER_1);
        s32 sy = joyGetStickY(PLAYER_1);
        if (nonmouse != 0 || sx > 20 || sx < -20 || sy > 20 || sy < -20)
        {
            if (sPortWatchCursorShown)
            { /* Logs (rate-limited) when the cursor is dropped because of pad
               * input, so a drifting stick or stuck button that hides the pause
               * cursor shows up in the run log. */
                static u32 lastlog;
                extern s32 g_GlobalTimer;
                if ((u32) g_GlobalTimer - lastlog >= 60u)
                {
                    lastlog = (u32) g_GlobalTimer;
                    fprintf(stderr, "ge: watch: mouse cursor dropped - pad/keyboard activity (buttons=0x%04x stick=%d,%d)\n",
                            (unsigned) nonmouse, (int) sx, (int) sy);
                }
            }
            sPortWatchCursorShown = 0;
        }
    }
    if (!sPortWatchCursorShown)
    {
        /* Pad/keyboard driving the menu - the parked pointer must not
         * keep re-selecting the row under it. No hover, click, wheel or
         * drag handling until the mouse moves, scrolls or clicks again. */
        g_portWatchHoverTab = -1;
        sPortWatchDrag = 0;
        return;
    }
    if (!held)
    {
        sPortWatchDrag = 0;
    }

    /* page tabs: hover highlight, click switches (any page, even while a
     * widget has focus - the click drops the focus first) */
    g_portWatchHoverTab = -1;
    hittab = -1;
    for (i = 0; i < WATCH_NUMBER_SCREENS; i++)
    {
        portWatchTabBox(i, &l, &t, &r, &b);
        if (portWatchInside(mx, my, l, t, r, b))
        {
            hittab = i;
            if (i != (s32) watch_screen_index)
            {
                g_portWatchHoverTab = i;
            }
            break;
        }
    }
    if (check_watch_page_transistion_running())
    {
        if (click)
        {
            sPortWatchAteClick = 1; /* its Mouse-Left Z must not activate a row
                                     * on the hidden page */
        }
        return; /* content hidden while the face re-zooms; the tabs still hover */
    }
    if (click && hittab == (s32) watch_screen_index
        && watch_screen_index == WATCH_INDEX_GAME_OPTIONS
        && g_portWatchPage != PORTWATCH_GENERAL)
    {
        /* Clicking the tab you're already on jumps straight back to Watch
         * General from any sub-page. The click is eaten so its Z doesn't also
         * activate General's first row. */
        portWatchGotoSubPage(PORTWATCH_GENERAL);
        sPortWatchAteClick = 1;
        return;
    }
    if (click && g_portWatchHoverTab >= 0)
    {
        portWatchGotoPage(g_portWatchHoverTab);
        g_portWatchHoverTab = -1;
        sPortWatchAteClick = 1;
        return;
    }
    switch (watch_screen_index)
    {
        case WATCH_INDEX_INVENTORY:
        {
            s32 lh = j_text_trigger ? 14 : 12;
            s32 base = 0x8C;
            s32 count = bondinvCountTotalItemsInInv();
            if (wheel != 0 && watch_item_is_actively_selected == 0)
            {
                /* Uses the live cursor (not g_curWatchItemIndex) so every notch
                 * of a fast spin counts, and truncates like the game, since the
                 * settle code parks the cursor at index + ~0.5. The new cursor
                 * is parked at its rest value so the settle has nothing to do. */
                s32 idx = (s32) watch_inventory_cursor_pos - wheel; /* wheel up = previous */
                if (idx < 0) idx = 0;
                if (idx > count - 1) idx = count - 1;
                watch_inventory_cursor_pos = (f32) idx + 0.5f;
                g_curWatchItemIndex = idx;
            }
            if (click && mx >= 0x4B && mx <= (f32) (g_portWatchInvTextWidth + 0x52)
                && my >= (f32) base && my < (f32) (base + lh * 5))
            {
                s32 k = ((s32) my - base) / lh;              /* visible row 0..4, highlight is row 2 */
                s32 idx = g_curWatchItemIndex + (k - 2);
                if (idx >= 0 && idx < count)
                {
                    portWatchEquipIndex(idx);
                }
                sPortWatchAteClick = 1;
            }
            break;
        }
        case WATCH_INDEX_CONTROL_OPTIONS:
            if (portWatchRaphnetView())
            {
                portWatchMouseControlVanilla(mx, my, click, wheel);
                break;
            }
            /* the port Controller page is the same row grid as Game Options -
             * same drawer, same columns, same hit test. Fall through rather
             * than keeping a second copy of it that can drift. */
            /* fallthrough */
        case WATCH_INDEX_GAME_OPTIONS:
        {
            s32 row = -1;
            const s32 pagerows = portWatchVisibleRows();

            if (portWatchDropIsVisible() && !portWatchDropIsOpen() && portWatchDropPage() == g_portWatchPage)
            {   /* the list is animating shut and its panel is still drawn
                 * over the rows: no hover or click reaches the rows under it
                 * (the second click of a double-click on an entry) */
                if (click)
                {
                    sPortWatchAteClick = 1;
                }
                break;
            }
            if (portWatchDropIsOpen() && portWatchDropPage() == g_portWatchPage)
            { /* THE LIST OWNS THE MOUSE while it is open. Without this the
               * wheel would scroll the rows BEHIND the panel (the row is not
               * "selected" any more - the list replaced that mode), and a
               * click would land on whatever the panel is covering. */
                const s32 drow = portWatchDropRow();
                const s32 rowy = portWatchRowY0() + YINC * (drow - portWatchScrollOffset());
                s32 dx0, dy0, dw, dn;
                /* The pointer must move before hover can change an open list's
                 * highlight, because a resting pointer often sits on a
                 * neighbour of the live entry and a second click would apply
                 * the wrong value. Until the pointer or wheel moves, a click
                 * just closes the list. */
                extern s32 portWatchDropOpens(void);
                static s32 dropSeen = -1;
                static s32 dropMoved;

                if (portWatchDropOpens() != dropSeen)
                {
                    dropSeen = portWatchDropOpens();
                    dropMoved = 0;
                }
                if ((moved || wheel != 0) && portWatchDropAnimDone())
                {
                    dropMoved = 1;
                }

                portWatchDropBox(rowy, &dx0, &dy0, &dw, &dn);

                if (wheel != 0)
                {
                    /* Scroll the view and let hover pick whatever lands under
                     * the cursor, rather than moving the highlight and calling
                     * scrollFollow, which would feed back with the pointer (see
                     * portWatchDropScrollBy). This makes the wheel feel like
                     * scrolling a list. */
                    if (portWatchDropCount() > dn)
                    {
                        portWatchDropScrollBy(-wheel, dn);   /* wheel up = earlier entries */
                    }
                    else
                    {
                        /* nothing to scroll, so there is no loop to have:
                         * move the highlight instead of doing nothing */
                        s32 k = (wheel > 0) ? wheel : -wheel;
                        while (k-- > 0)
                        {
                            portWatchDropMove((wheel > 0) ? -1 : 1);
                        }
                    }
                    portWatchDropBox(rowy, &dx0, &dy0, &dw, &dn);
                }
                if (mx >= (f32) dx0 && mx <= (f32) (dx0 + dw)
                    && my >= (f32) (dy0 - PORT_WATCH_ROW_ABOVE) && my < (f32) (dy0 - PORT_WATCH_ROW_ABOVE + dn * YINC))
                {   /* the band ends where the last drawn slot ends (entry e is
                     * y0 - ROW_ABOVE + YINC*e ..); the panel's padding below it
                     * used to pick the hidden entry past the window */
                    s32 e = ((s32) my - (dy0 - PORT_WATCH_ROW_ABOVE)) / YINC;

                    if (e > dn - 1) e = dn - 1;   /* never an entry outside the visible window */
                    /* A scrolling list keeps hover every tick, since its wheel
                     * moved the view and hover picks the entry. A short list's
                     * wheel moves the highlight itself, so there hover waits
                     * for the pointer to move or a click, otherwise it would
                     * snap the highlight straight back. */
                    if (!dropMoved)
                    {
                        if (click)
                        {   /* a click in place keeps the live value */
                            portWatchDropClose();
                            sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);   /* a close, as B / Escape */
                            sPortWatchAteClick = 1;
                        }
                    }
                    else
                    {
                        if (click || portWatchDropCount() > dn || (moved && wheel == 0))
                        {
                            portWatchDropSetIndex(portWatchDropScroll() + e);
                        }
                        if (click)
                        {
                            portWatchDropChoose();
                            sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);   /* as A's confirm */
                            sPortWatchAteClick = 1;
                        }
                    }
                }
                else if (click)
                {
                    /* a click anywhere else shuts the list without applying -
                     * the same as B, and what every drop-down does */
                    portWatchDropClose();
                    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);   /* as B / Escape */
                    sPortWatchAteClick = 1;
                }
                break;
            }
            if (portWatchBindCaptureActive())
            { /* While a bind row reads "Press a Key" the capture owns the
               * mouse, so hover and the wheel can't move the highlight and make
               * Delete or the hold-to-unbind hit the wrong row. Return rather
               * than break, because portWatchMouseTail would read a click at
               * the band edge as a page flip. */
                return;
            }
            const s32 indexfirst = portWatchIndexFirst();
            const s32 indexlimit = portWatchIndexLimit();

            if (wheel != 0 && watch_item_is_actively_selected == 0
                && portWatchWheelScroll(wheel))
            {   /* When the page is longer than its window the wheel scrolls the
                 * view and the row handler below must not also move the
                 * highlight. On a page that fits this returns 0 and the wheel
                 * walks the rows as before. */
                wheel = 0;
            }
            /* row 0/1 = music/fx labels+bars (General only), 2.. = the page's
             * own rows at YOFFSET_1 + YINC*i */
            if (g_portWatchPage == PORTWATCH_GENERAL
                && my >= (f32) (YOFFSET_8 - PORT_WATCH_ROW_ABOVE) && my <= (f32) (YOFFSET_8 + PORT_WATCH_ROW_BELOW)) row = 0;
            else if (g_portWatchPage == PORTWATCH_GENERAL
                && my >= (f32) (YOFFSET_9 - PORT_WATCH_ROW_ABOVE) && my <= (f32) (YOFFSET_9 + PORT_WATCH_ROW_BELOW)) row = 1;
            else
            {
                for (i = 0; i < pagerows; i++)   /* pagerows = the VISIBLE window */
                {
                    s32 y = portWatchRowY0() + YINC * i;
                    if (my >= (f32) (y - PORT_WATCH_ROW_ABOVE) && my <= (f32) (y + PORT_WATCH_ROW_BELOW))
                    {
                        row = portWatchScrollOffset() + i + 2;
                        break;
                    }
                }
            }
            /* the bars themselves (3D), a bit taller than the label rows */
            for (i = 0; g_portWatchPage == PORTWATCH_GENERAL && i < 2; i++)
            {
                portWatchSliderBox(i, &l, &t, &r, &b);
                if ((click && portWatchInside(mx, my, l, t, r, b)) || (held && sPortWatchDrag == i + 1))
                {
                    if (click)
                    {
                        sPortWatchDrag = i + 1;
                        sPortWatchAteClick = 1;
                    }
                    reset_watch_item_is_actively_selected();
                    game_options_index = i;
                    {
                        f32 tx0, tx1;
                        portWatchSliderTrack(i, &tx0, &tx1);
                        if (tx1 - tx0 > 1.0f)
                        {
                            portWatchSetVolume(i, portWatchBarFill((mx - tx0) / (tx1 - tx0)));
                        }
                    }
                    return;
                }
            }
            if (wheel != 0 && watch_item_is_actively_selected == 0)
            {
                /* wrap over THIS page's rows, not a constant 10 (spec Q8) */
                s32 span = indexlimit - indexfirst;
                s32 n = (s32) game_options_index - wheel;
                if (span > 0)
                {
                    while (n < indexfirst) n += span;
                    while (n >= indexlimit) n -= span;
                    game_options_index = n;
                }
            }
            else if (wheel != 0)
            {
                /* When the row is selected the wheel changes its value instead
                 * of moving the highlight; wheel up equals pressing right
                 * ("more"), matching the resolution ordering in video.c. */
                const s32 prow = (s32) game_options_index - PORTWATCH_ROW_BASE;
                const s32 dir = (wheel > 0) ? 1 : -1;
                s32 n = (wheel > 0) ? wheel : -wheel;

                if (n > 32)
                {
                    n = 32;   /* a flick is notches, not a mandate */
                }
                if (prow >= 0 && !portWatchRowLocked(g_portWatchPage, prow))   /* a greyed row takes no step and makes no
                                                                                * sound */
                {
                    const s32 playeropt = portWatchRowPlayerOption(g_portWatchPage, prow);

                    if (playeropt >= 0)
                    {
                        /* Rare's own rows (auto-aim, aim control, ...) keep
                         * their value in their own table and are stepped by
                         * game_option_toggle_input, which only reads the pad.
                         * Without this the wheel would work on every row except
                         * these. */
                        struct game_options *entry = &game_options_entries[playeropt];
                        const s32 cnt = entry->text[3] ? 3 : 2;
                        s32 v = (s32) entry->current_value + dir * n;

                        while (v < 0) v += cnt;
                        while (v >= cnt) v -= cnt;
                        if (v != (s32) entry->current_value)
                        {
                            game_option_select_value(&entry->current_value, (u32) v);
                        }
                    }
                    else
                    {
                        /* portWatchRowStep, not portWatchStepRow: the latter
                         * beeps per step, and a twelve-notch flick firing
                         * twelve sounds is not a menu, it is an alarm. One
                         * beep for the whole wheel event. */
                        s32 k = n;
                        while (k-- > 0)
                        {
                            portWatchRowStep(g_portWatchPage, prow, dir);
                        }
                        set_controlstick_lr_disabled();
                        sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
                    }
                }
                else if (g_portWatchPage == PORTWATCH_GENERAL)
                {
                    /* the volume bars are options too, and a wheel that moved
                     * every row on the page except these two would read as
                     * broken. Same step the D-pad takes. */
                    const s32 barrow = (s32) game_options_index;
                    const s32 vol = barrow ? (s32) sndGetSfxSlotFirstNaturalVolume()
                                           : (s32) get_mTrack2Vol();
                    s32 adj = vol + dir * n * WATCH_VOL_ADJUST_STEP;

                    if (adj < 0) adj = 0;
                    if (adj > VOLUME_MAX) adj = VOLUME_MAX;
                    portWatchSetVolume(barrow, (f32) adj / (f32) VOLUME_MAX);
                }
            }
            if (row >= 0 && mx >= (f32) (XOFFSET_1 - 8) && mx <= (f32) PORT_WATCH_ROW_RIGHT)
            {
                sPortWatchIndexByMouse = 1;   /* whatever sets the index in here, the pointer did it */
                /* A page longer than its window keeps hover every tick, since
                 * the wheel scrolled the view and hover picks the row under the
                 * pointer. On a page that fits the wheel moves the highlight,
                 * so hover waits for the pointer to move or a click, otherwise
                 * it would undo the wheel in the same tick. */
                if (watch_item_is_actively_selected == 0
                    && (click || portWatchRowsOnPage() > pagerows || (moved && wheel == 0)))
                {
                    game_options_index = row;   /* hover moves the highlight */
                }
                if (click && row >= 2 && portWatchRowKind(g_portWatchPage, row - 2) == PWROW_SUBSCREEN)
                {
                    reset_watch_item_is_actively_selected();
                    game_options_index = row;
                    portWatchGotoSubPage(portWatchRowSubPage(g_portWatchPage, row - 2));
                    sPortWatchAteClick = 1;
                }
                else if (click && row >= 2 && portWatchRowKind(g_portWatchPage, row - 2) == PWROW_BACK)
                {
                    portWatchGotoSubPage(portWatchParentPage(g_portWatchPage));
                    sPortWatchAteClick = 1;
                }
                else if (click && row >= 2 && portWatchRowLocked(g_portWatchPage, row - 2))
                {   /* a row greyed by Classic Mode - no list, no value,
                     * so no open beep or change click either */
                    reset_watch_item_is_actively_selected();
                    game_options_index = row;
                    sPortWatchAteClick = 1;
                }
                else if (click && row >= 2 && portWatchRowDropdown(g_portWatchPage, row - 2)
                         && mx >= portWatchValueSplitX(g_portWatchPage, row - 2))
                {
                    /* the value half of a drop-down row: open the list rather
                     * than stepping it blind, which is the whole point */
                    reset_watch_item_is_actively_selected();
                    game_options_index = row;
                    portWatchDropOpen(g_portWatchPage, row - 2);
                    portWatchDropAnchor(row - 2);
                    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
                    sPortWatchAteClick = 1;
                }
                else if (click && row >= 2 && portWatchRowKind(g_portWatchPage, row - 2) == PWROW_DEVICE)
                {
                    /* the "input" row: label or value, a click arms "press a button" */
                    game_options_index = row;
                    portWatchAssignBegin();
                    sPortWatchAteClick = 1;
                }
                else if (click && row >= 2 && mx >= portWatchValueSplitX(g_portWatchPage, row - 2)
                         && portWatchRowKind(g_portWatchPage, row - 2) == PWROW_BIND)
                {
                    /* Clicking a bind row's key text does what A/Z does (arm
                     * the capture, or the vanilla focus toggle if this device
                     * has nothing to bind) instead of stepping a value. Eat the
                     * click, or portWatchMouseTail reads it as a page flip. */
                    if (game_options_index != (u32) row)
                    {
                        reset_watch_item_is_actively_selected();   /* another row had focus */
                        game_options_index = row;
                    }
                    if (!portWatchActivateRow())
                    {
                        watch_play_beep_sound();   /* nothing to bind here: the vanilla Z|A toggle */
                    }
                    sPortWatchAteClick = 1;
                }
                else if (click && row >= 2 && mx >= portWatchValueSplitX(g_portWatchPage, row - 2))
                {
                    /* Clicking a value column sets it directly, using the same
                     * columns portWatchDrawRowValue draws. Rows with 2-3 values
                     * pick the clicked one; wider or ranged rows and the preset
                     * step toward the clicked arrow. */
                    const s32 prow = row - 2;
                    const s32 count = portWatchRowValueCount(g_portWatchPage, prow);
                    const s32 playeropt = portWatchRowPlayerOption(g_portWatchPage, prow);

                    reset_watch_item_is_actively_selected();
                    game_options_index = row;
                    if (playeropt >= 0)
                    {
                        /* Rare's own row: its value lives in its own table */
                        struct game_options *entry = &game_options_entries[playeropt];
                        u32 col = entry->text[3] ? ((mx < 202.0f) ? 0 : (mx < 247.0f) ? 1 : 2)
                                                 : ((mx < 225.0f) ? 0 : 1);
                        if (entry->current_value != col)
                        {
                            game_option_select_value(&entry->current_value, col);
                        }
                    }
                    else if (portWatchValueColumnHit(g_portWatchPage, prow, count, mx) >= 0)
                    {
                        /* Beep on a real change, as Rare's rows do via
                         * game_option_select_value, so port rows don't answer a
                         * click silently. */
                        const s32 pick = portWatchValueColumnHit(g_portWatchPage, prow, count, mx);

                        if (pick != portWatchRowValueIndex(g_portWatchPage, prow))
                        {
                            portWatchRowSetIndex(g_portWatchPage, prow, pick);
                            set_controlstick_lr_disabled();
                            sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
                        }
                    }
                    else
                    {
                        /* A row drawn as one value between < > arrows splits
                         * its click halves at the value's centre, so a click on
                         * the back arrow never steps forward. */
                        portWatchStepRow(prow, (mx < (f32) PORTWATCH_VAL3_X2) ? -1 : 1);
                    }
                    sPortWatchAteClick = 1;
                }
                else if (click && watch_item_is_actively_selected && game_options_index != (u32) row)
                {
                    /* clicking another row while one has focus: just move */
                    reset_watch_item_is_actively_selected();
                    game_options_index = row;
                    sPortWatchAteClick = 1;
                }
                /* else: label click = the vanilla Z|A focus toggle */
            }
            break;
        }
        case WATCH_INDEX_MISSION_STATUS:
        {
            /* abort widget (after the click that focuses it): hover picks
             * CANCEL (x 0x88) / CONFIRM (x 0xBD) at y 0x4C - only on mouse
             * activity so a pad user can still steer it; the click itself is
             * the vanilla Z (CONFIRM -> abort, CANCEL -> unfocus). */
            if (watch_item_is_actively_selected && (moved || click)
                && my >= (f32) (0x4C - PORT_WATCH_ROW_ABOVE) && my <= (f32) (0x4C + PORT_WATCH_ROW_BELOW)
                && mx >= 128.0f && mx <= 252.0f)
            {
                D_800409A4 = (mx >= 187.0f) ? 1 : 0;
            }
            break;
        }
        case WATCH_INDEX_MISSION_BRIEFING:
        {
            /* wheel walks the briefing pages in the vanilla down order
             * OBJECTIVES -> BACKGROUND -> M -> Q -> MONEYPENNY (enum values
             * 4,0,1,2,3 = +1 mod 5); wheel up = the up order */
            if (wheel != 0)
            {
                s32 idx = (s32) mission_brief_index;
                s32 n = wheel > 0 ? wheel : -wheel;
                while (n-- > 0)
                {
                    idx = (wheel > 0) ? (idx + 4) % 5 : (idx + 1) % 5;
                }
                mission_brief_index = idx;
                disable_watch_stick_y_nav_ready();
                reset_watch_item_is_actively_selected();
            }
            break;
        }
        default:
            break;
    }
    portWatchMouseTail(mx, click, hittab);
}

/* The vanilla Control Options mouse handling (control-style list + the two
 * rows). Only reached when a raphnet adapter is the selected device; every
 * other device gets the port page's grid. */
static void portWatchMouseControlVanilla(f32 mx, f32 my, s32 click, s32 wheel)
{
    (void) click;
    {
            /* The port's device row (index 2, PORTWATCH_CTL_INDEX_DEVICE) above
             * the two vanilla rows, so a mouse user who picks raphnet can
             * switch back. The wheel steps it from a hover, and a click steps
             * it by the side clicked and disarms it. */
            if ((watch_item_is_actively_selected == 0 || controller_options_index == 2)
                && mx >= (f32) (XOFFSET_1 - 8) && mx <= 300.0f
                && my >= (f32) (PORTWATCH_DEVROW_Y - PORT_WATCH_ROW_ABOVE) && my <= (f32) (PORTWATCH_DEVROW_Y + PORT_WATCH_ROW_BELOW))
            {
                /* Superseded: the row is now "press a button"
                 * (portWatchAssignBegin), as on the port page, so a click arms
                 * it and the wheel does nothing. Pressing any key then selects
                 * the keyboard, which gives keyboard-only players a way back. */
                (void) wheel;
                controller_options_index = 2;
                if (click)
                {
                    portWatchAssignBegin();
                    sPortWatchAteClick = 1;
                }
                return;
            }
            if (wheel != 0 && (watch_item_is_actively_selected == 0 || controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE))
            {
                /* The wheel scrolls the control style list (1.1, 1.2, ...) by
                 * focusing the style row as A does and moving its cursor; the
                 * vanilla scroller (sub_GAME_7F0A611C) eases the text and
                 * applies cur_player_control_type_0. */
                s32 idx = (s32) g_CurrentPlayer->cur_player_control_type_2 - wheel; /* truncate: rests at index + ~0.5 */
                f32 pos;
                if (idx < 0) idx = 0;
                if (idx > D_800409D8 - 1) idx = D_800409D8 - 1;
                pos = (f32) idx + 0.5f;
                if (watch_item_is_actively_selected == 0)
                {
                    controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
                    watch_item_is_actively_selected = 1;
                    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
                }
                g_CurrentPlayer->cur_player_control_type_2 = pos;
            }
            if (watch_item_is_actively_selected == 0 && mx >= (f32) (XOFFSET_1 - 8) && mx <= 300.0f)
            {
                if (my >= (f32) (PORTWATCH_STYLEROW_Y - PORT_WATCH_ROW_ABOVE) && my <= (f32) (PORTWATCH_STYLEROW_Y + PORT_WATCH_ROW_BELOW))
                {
                    controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
                }
                else if (my >= (f32) (PORTWATCH_CTLROW_Y - PORT_WATCH_ROW_ABOVE) && my <= (f32) (PORTWATCH_CTLROW_Y + PORT_WATCH_ROW_BELOW))
                {
                    controller_options_index = CONTROLLER_OPTIONS_INDEX_INPUTS;
                }
            }
    }
}

/* Anything no page handler claimed: a click on the tab you are already on is
 * swallowed, a click outside the tab strip flips the page. Runs last, for
 * every page. */
static void portWatchMouseTail(f32 mx, s32 click, s32 hittab)
{
    f32 l, t, r, b;

    if (click && !sPortWatchAteClick && hittab == (s32) watch_screen_index)
    {
        /* Clicking the tab you're already on does nothing; eat the click, or
         * its Z reaches draw_watch_current_page and acts on whatever row is
         * highlighted. It sits after the switch so an open drop-down is still
         * closed by the click and an armed pad capture still sees it. */
        sPortWatchAteClick = 1;
        return;
    }
    if (click && !sPortWatchAteClick)
    {
        /* A click at the far left or right of the face (health/armour bars,
         * bezel, band edge) flips to the previous/next page, wrapping, but only
         * if nothing above claimed it. The zone is 6 px outside the tab row and
         * follows the zoom. */
        f32 l0, r4, t0, b0;
        (void) t;
        (void) r;
        portWatchTabBox(0, &l0, &t0, &r4, &b0);
        portWatchTabBox(WATCH_NUMBER_SCREENS - 1, &l, &t, &r4, &b);
        if (mx < l0 - 6.0f || mx > r4 + 6.0f)
        {
            s32 page = (s32) watch_screen_index + ((mx < l0) ? -1 : 1);
            if (page < 0) page = WATCH_NUMBER_SCREENS - 1;
            if (page >= WATCH_NUMBER_SCREENS) page = 0;
            portWatchGotoPage(page);
            sPortWatchAteClick = 1;
        }
    }
}

/* end of draw_watch_current_page (menu open): crosshair + GE_WATCHMOUSEDBG boxes */
Gfx *portWatchMouseDraw(Gfx *gdl)
{
    s32 x = (s32) sPortWatchMouseX;
    s32 y = (s32) sPortWatchMouseY;
    s32 i;
    f32 l, t, r, b;

    if (!sPortWatchArmed)
    {
        return gdl;
    }
    if (portWatchDbg())
    {
        static u32 calls;
        gdl = microcode_constructor(gdl);
        for (i = 0; i < WATCH_NUMBER_SCREENS; i++)
        {
            portWatchTabBox(i, &l, &t, &r, &b);
            gdl = microcode_constructor_related_to_menus(gdl, (s32) l, (s32) t, (s32) r, (s32) b, 0xFF000060);
            if ((calls % 120) == 0)
            {
                fprintf(stderr, "ge: watchmouse: tab %d box %.1f,%.1f - %.1f,%.1f (page %d fovy %.2f)\n", i, l, t, r, b, (int) watch_screen_index, g_CurrentPlayer->zoominfovy);
            }
        }
        { /* the 2D row boxes of the current page */
            s32 lh = j_text_trigger ? 14 : 12;
            if (watch_screen_index == WATCH_INDEX_INVENTORY)
            {
                for (i = 0; i < 5; i++)
                {
                    gdl = microcode_constructor_related_to_menus(gdl, 0x4B, 0x8C + lh * i, g_portWatchInvTextWidth + 0x52, 0x8C + lh * (i + 1) - 1, (i & 1) ? 0xFFFF0040 : 0x00FFFF40);
                }
            }
            else if (watch_screen_index == WATCH_INDEX_GAME_OPTIONS)
            {
                gdl = microcode_constructor_related_to_menus(gdl, XOFFSET_1 - 8, YOFFSET_8 - PORT_WATCH_ROW_ABOVE, 300, YOFFSET_8 + PORT_WATCH_ROW_BELOW, 0xFFFF0040);
                gdl = microcode_constructor_related_to_menus(gdl, XOFFSET_1 - 8, YOFFSET_9 - PORT_WATCH_ROW_ABOVE, 300, YOFFSET_9 + PORT_WATCH_ROW_BELOW, 0x00FFFF40);
                for (i = 0; i < 8; i++)
                {
                    s32 y = YOFFSET_1 + YINC * i;
                    gdl = microcode_constructor_related_to_menus(gdl, XOFFSET_1 - 8, y - PORT_WATCH_ROW_ABOVE, 300, y + PORT_WATCH_ROW_BELOW, (i & 1) ? 0xFFFF0040 : 0x00FFFF40);
                }
                gdl = microcode_constructor_related_to_menus(gdl, 165, YOFFSET_1 - 6, 166, YOFFSET_1 + YINC * 8, 0xFFFFFFA0);
                gdl = microcode_constructor_related_to_menus(gdl, 202, YOFFSET_1 - 6, 203, YOFFSET_1 + YINC * 8, 0xFFFFFFA0);
                gdl = microcode_constructor_related_to_menus(gdl, 225, YOFFSET_1 - 6, 226, YOFFSET_1 + YINC * 8, 0xFFFF00A0);
                gdl = microcode_constructor_related_to_menus(gdl, 247, YOFFSET_1 - 6, 248, YOFFSET_1 + YINC * 8, 0xFFFFFFA0);
            }
            else if (watch_screen_index == WATCH_INDEX_CONTROL_OPTIONS)
            {
                gdl = microcode_constructor_related_to_menus(gdl, XOFFSET_1 - 8, PORTWATCH_STYLEROW_Y - PORT_WATCH_ROW_ABOVE, 300, PORTWATCH_STYLEROW_Y + PORT_WATCH_ROW_BELOW, 0xFFFF0040);
                gdl = microcode_constructor_related_to_menus(gdl, XOFFSET_1 - 8, PORTWATCH_CTLROW_Y - PORT_WATCH_ROW_ABOVE, 300, PORTWATCH_CTLROW_Y + PORT_WATCH_ROW_BELOW, 0x00FFFF40);
            }
        }
        for (i = 0; i < 2; i++)
        {
            portWatchSliderBox(i, &l, &t, &r, &b);
            gdl = microcode_constructor_related_to_menus(gdl, (s32) l, (s32) t, (s32) r, (s32) b, 0x0000FF60);
            if ((calls % 120) == 0)
            {
                fprintf(stderr, "ge: watchmouse: slider %d box %.1f,%.1f - %.1f,%.1f\n", i, l, t, r, b);
            }
        }
        if ((calls % 120) == 0)
        {
            extern Mtxf g_portWatchHandMtx;
            extern f32 g_portWatchFovy, g_portWatchAspect;
            f32 *m = &g_portWatchHandMtx.m[0][0];
            f32 px0, py0;
            portWatchProjectFace(-299.0f, 310.0f, &px0, &py0);
            fprintf(stderr, "ge: watchmouse: hand [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g] fovy %g asp %g scale %g pos %g cot %g pi %g proj(-299,310)=%g,%g\n",
                    m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15],
                    g_portWatchFovy, g_portWatchAspect, WATCH_MODEL(g_CurrentPlayer)->scale, g_CurrentPlayer->pause_watch_position, cosf(g_portWatchFovy * (M_PI_F / 360.0f)) / sinf(g_portWatchFovy * (M_PI_F / 360.0f)), (f64) M_PI_F, (f64) px0, (f64) py0);
            fprintf(stderr, "ge: watchmouse: mouse %.1f,%.1f shown %d hover %d\n", sPortWatchMouseX, sPortWatchMouseY, sPortWatchCursorShown, g_portWatchHoverTab);
            fflush(stderr);
        }
        calls++;
    }
    if (!sPortWatchCursorShown && !portWatchDbg())
    {
        return gdl; /* no mouse activity yet, or the pad/keyboard took over */
    }
    { /* Draw the main-menu crosshair sprite (frontDrawCursor) at the same size
       * and alpha. Call microcode_constructor first, because the page's last 3D
       * draw may have left a z-buffered pipe that culls a texrect. */
        f32 xypos[2];
        f32 halfedxy[2];
        gdl = microcode_constructor(gdl);
        texSelect(&gdl, crosshairimage, 4, 0, 0);
        xypos[0] = (f32) x;
        xypos[1] = (f32) y;
        /* Quarter size: half the frontend cursor's extents each way, so the 32
         * px sprite draws as a 16 px cross on the watch face. The whole texture
         * is still mapped, not cropped. */
        halfedxy[0] = crosshairimage->width * 0.25f;
        halfedxy[1] = crosshairimage->height * 0.25f;
        /* CROSSHAIR COLOUR (midas.ini crosshair_color): same sprite, same setting */
        portDisplayCrosshairImage(&gdl, xypos, halfedxy, crosshairimage->width, crosshairimage->height,
                                  220, (crosshairimage->level > 0));
    }
    return gdl;
}
#endif /* GE_VIDEO */

void watch_play_beep_sound(void) {

    if (watch_item_is_actively_selected == 1) {
        watch_item_is_actively_selected = 0;

    } else {
        watch_item_is_actively_selected = 1;
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
    }
}


void reset_watch_item_is_actively_selected(void){
  watch_item_is_actively_selected = 0;
}


u32 is_holding_greater_than_2E_left_on_stick(void)
{
    return (joyGetStickX(PLAYER_1) < -0x2d);
}


u32 is_holding_greater_than_2E_right_on_stick(void)
{
    return ((joyGetStickX(PLAYER_1) < 0x2e) ^ 1);
}


u32 get_controlstick_lr_enabled(void) {
  return controlstick_lr_enabled;
}


void set_controlstick_lr_disabled(void) {
  controlstick_lr_enabled = 0;
}


s32 sub_GAME_7F0A4FB0(void)
{
    return is_holding_greater_than_2E_left_on_stick() && get_controlstick_lr_enabled();
}


s32 sub_GAME_7F0A4FEC(void)
{
    return is_holding_greater_than_2E_right_on_stick() && get_controlstick_lr_enabled();
}


u32 is_holding_greater_than_2E_up_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < 0x2e) ^ 1;
}


u32 is_holding_greater_than_2E_down_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < -0x2d);
}


u32 get_watch_stick_y_nav_ready(void)
{
    return watch_stick_y_nav_ready;
}


void disable_watch_stick_y_nav_ready(void)
{
    watch_stick_y_nav_ready = 0;
}


#ifndef TARGET_N64
/* Hold to repeat: holding up/down (stick, the pad's digital Move, D-pad, C
 * buttons or the watch_up / watch_down binds) repeats after 0.4 s and then
 * every 0.1 s, like portSheetDpadStep.
 * Computed once per watch tick and ORed into every page's up/down checks, since
 * vanilla stepped once and waited for the stick to recentre. */
static s32 port_watch_rep[2];   /* this tick: 0 up, 1 down */

static void portWatchHoldRepeatTick(void)
{
    extern f32 portTickDelta;   /* frametiming.c: this tick's field delta */
    extern s32 portInputPadDigMoveP1(s32 *dx, s32 *dy) __attribute__((weak)); /* input_pc.c */
    extern void portJoyPortWatchHeldUpDown(s32 *up, s32 *down) __attribute__((weak)); /* joy_port.c */
    static f32 held[2];
    static s32 prev[2];
    s32 dx = 0, dy = 0;
    s32 bindup = 0, binddown = 0;
    s32 dir[2];
    s32 a;

    if (portInputPadDigMoveP1 != NULL)
    {
        portInputPadDigMoveP1(&dx, &dy);
    }
    if (portJoyPortWatchHeldUpDown != NULL)
    {
        portJoyPortWatchHeldUpDown(&bindup, &binddown);
    }
    dir[0] = is_holding_greater_than_2E_up_on_stick() || dy > 0 || bindup || joyGetButtons(PLAYER_1, U_JPAD | U_CBUTTONS);
    dir[1] = is_holding_greater_than_2E_down_on_stick() || dy < 0 || binddown || joyGetButtons(PLAYER_1, D_JPAD | D_CBUTTONS);
    for (a = 0; a < 2; a++)
    {
        port_watch_rep[a] = FALSE;
        if (dir[a] && prev[a])
        {
            held[a] += portTickDelta;
            if (held[a] >= 24.0f)
            {
                port_watch_rep[a] = TRUE;
                held[a] -= 6.0f;
            }
        }
        else
        {
            held[a] = 0.0f;   /* the first step is the press's own (edge / nav-ready) */
        }
        prev[a] = dir[a];
    }
}
#endif

s32 sub_GAME_7F0A5088(void)
{
#ifndef TARGET_N64
    if (port_watch_rep[0])
    {
        return TRUE;
    }
#endif
    return is_holding_greater_than_2E_up_on_stick() && get_watch_stick_y_nav_ready();
}


s32 sub_GAME_7F0A50C4(void)
{
#ifndef TARGET_N64
    if (port_watch_rep[1])
    {
        return TRUE;
    }
#endif
    return is_holding_greater_than_2E_down_on_stick() && get_watch_stick_y_nav_ready();
}


u32 is_holding_less_than_10_up_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < 0x10) ^ 1;
}


u32 is_holding_less_than_10_down_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < -0xf);
}


u32 watch_stick_y_was_active(void)
{
    return watch_stick_y_prev_active;
}


void reset_watch_stick_y_latch(void) 
{
    watch_stick_y_prev_active = 0;
}


s32 watch_stick_y_pressed_up(void)
{
    return is_holding_less_than_10_up_on_stick() && !watch_stick_y_was_active();
}


s32 watch_stick_y_pressed_down(void)
{
    return is_holding_less_than_10_down_on_stick() && !watch_stick_y_was_active();
}


void sub_GAME_7F0A51D8(void)
{
#ifndef TARGET_N64
    {   /* With watch static turned off, skip the burst here (the only place one
         * starts), so there's no static sound and the level stays at 0xE0 with
         * no scanline, fade or flicker. */
        extern s32 portWatchStatic(void);   /* video.c; weak 1 headless */

        if (!portWatchStatic())
        {
            return;
        }
    }
#endif
    g_WatchBackgroundGreen = 0x80;
    sndPlaySfx(g_musicSfxBufferPtr, WATCH_STATIC_SFX, NULL);
    return;
}


void sub_GAME_7F0A5210(void)
{
    set_controlstick_lr_disabled();
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
    if ((D_80040B10 << 0x10) < randomGetNext()) {
        sub_GAME_7F0A51D8();
    }
    return;
}


// initial pause screen: WATCH_INDEX_MISSION_STATUS
void watch_screen0_navigation(void)
{
    s32 goto_watch_screen_index_4;
    s32 goto_watch_screen_index_1;

    if (watch_item_is_actively_selected == 0)
    {
        goto_watch_screen_index_4 = FALSE;
        goto_watch_screen_index_1 = FALSE;

        if (get_debug_gunwatchpos_flag() == 0)
        {
            if (joyGetButtonsPressedThisFrame(PLAYER_1, L_TRIG|L_CBUTTONS))
            {
                goto_watch_screen_index_4 = TRUE;
            }
            if (joyGetButtonsPressedThisFrame(PLAYER_1, R_TRIG|R_CBUTTONS))
            {
                goto_watch_screen_index_1 = TRUE;
            }
        }


        if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD)) || (sub_GAME_7F0A4FB0()))
        {
            goto_watch_screen_index_4 = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD)) || (sub_GAME_7F0A4FEC()))
        {
            goto_watch_screen_index_1 = TRUE;
        }

        if (goto_watch_screen_index_4)
        {
            watch_screen_index = WATCH_INDEX_MISSION_BRIEFING;
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
        }

        if (goto_watch_screen_index_1)
        {
            watch_screen_index = WATCH_INDEX_INVENTORY;
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
            return;
        }
    }
    else if ((D_800409A4) && (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON))
#ifdef GE_VIDEO
             /* a click the watch mouse layer swallowed (this tab's own icon,
              * portWatchMouseTail) is no CONFIRM. PEEK, never
              * portWatchTakeClick: this runs in the tick, and the frame's
              * draw_watch_current_page takes the flag for its Z|A toggle */
             && !sPortWatchAteClick
#endif
             )
    {
        D_800409A4 = 0;
        set_missionstate(MISSION_STATE_0);
        bossRunTitleStage();
        mission_failed_or_aborted = TRUE;
        deleteCurrentSelectedFolder();
    }
}


// pause screen: WATCH_INDEX_INVENTORY
void watch_screen1_navigation(void)
{
    s32 goto_watch_screen_index_0;
    s32 goto_watch_screen_index_2;

    if (watch_item_is_actively_selected == 0)
    {
        goto_watch_screen_index_0 = FALSE;
        goto_watch_screen_index_2 = FALSE;

        if (get_debug_gunwatchpos_flag() == FALSE)
        {
            if (joyGetButtonsPressedThisFrame(PLAYER_1, L_TRIG|L_CBUTTONS))
            {
                goto_watch_screen_index_0 = TRUE;
            }
            if (joyGetButtonsPressedThisFrame(PLAYER_1, R_TRIG|R_CBUTTONS))
            {
                goto_watch_screen_index_2 = TRUE;
            }
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD)) || (sub_GAME_7F0A4FB0()))
        {
            goto_watch_screen_index_0 = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD)) || (sub_GAME_7F0A4FEC()))
        {
            goto_watch_screen_index_2 = TRUE;
        }

        if (goto_watch_screen_index_0)
        {
            watch_screen_index = WATCH_INDEX_MISSION_STATUS;
            zero_D_800409A4();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM2, 15.0f);

        }

        if (goto_watch_screen_index_2)
        {
            watch_screen_index = WATCH_INDEX_CONTROL_OPTIONS;
#ifndef TARGET_N64
            /* every way into the tab starts clean, this one (Inventory ->
             * right, the usual way in) too: without it a picker left on a
             * raphnet kept showing that page */
            portWatchResetPage();
            reset_controller_options_index();
#endif
            set_controlstick_lr_disabled();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
        }
    }
}


void unused_watch_screen_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            watch_screen_index = WATCH_INDEX_INVENTORY;
            set_controlstick_lr_disabled();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            watch_screen_index = WATCH_INDEX_CONTROL_OPTIONS;
            reset_controller_options_index();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
        }
    }
}


// WATCH_INDEX_CONTROL_OPTIONS
void watch_screen2_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_INVENTORY;
#ifndef TARGET_N64
            portWatchResetPage(); /* leaving the tab closes any sub-page, list or capture */
#endif
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_GAME_OPTIONS;
            reset_game_options_index();
            set_controlstick_lr_disabled();
        }
    }
}


// WATCH_INDEX_GAME_OPTIONS
void watch_screen3_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_CONTROL_OPTIONS;
#ifndef TARGET_N64
            portWatchResetPage(); /* leaving the tab closes any sub-page */
#endif
            reset_controller_options_index();
            set_controlstick_lr_disabled();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_MISSION_BRIEFING;
#ifndef TARGET_N64
            portWatchResetPage();
#endif
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
        }
    }
}


// WATCH_INDEX_MISSION_BRIEFING
void watch_screen4_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            watch_screen_index = WATCH_INDEX_GAME_OPTIONS;
            reset_game_options_index();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            watch_screen_index = WATCH_INDEX_MISSION_STATUS;
            zero_D_800409A4();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM2, 15.0f);
        }
    }
}


void controller_options_controlstyle_navigation(void)
{
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_INPUTS;
            disable_watch_stick_y_nav_ready();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_INPUTS;
            disable_watch_stick_y_nav_ready();
        }
    }
}


void controller_options_inputs_navigation(void)
{
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
            disable_watch_stick_y_nav_ready();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
            disable_watch_stick_y_nav_ready();
        }
    }
}


void sub_GAME_7F0A5998(void)
{
    s32 aux;

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        game_options_index = game_options_index - 1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
    else if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        game_options_index = game_options_index + 1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }

    aux = game_options_index;

    if (aux >= 10)
    {
        game_options_index = GAME_OPTIONS_INDEX_MUSIC;
        return;
    }

    if (aux < 0)
    {
        game_options_index = GAME_OPTIONS_INDEX_RATIO;
    }
}


/* N64 only now, along with game_options_fx_volume_navigation and
 * sub_GAME_7F0A5998 below: the PC build routes every Game Options index through
 * portWatchRowNavigate, which wraps over the page's own row count instead of
 * Rare's fixed ten. */
void game_options_music_volume_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        game_options_index = GAME_OPTIONS_INDEX_RATIO;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        game_options_index = GAME_OPTIONS_INDEX_FX;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void game_options_fx_volume_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        game_options_index = GAME_OPTIONS_INDEX_MUSIC;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        game_options_index = GAME_OPTIONS_INDEX_LOOK_UPDOWN;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void game_options_inventory_navigation(void)
{
    s32 count;
    s32 item_line_height;
    s32 selected_item_line_height;
#ifndef TARGET_N64
    /* A key or a pad's digital Move drives the stick at full tilt, which scrolls an
     * item every tick; while one is down the list steps once per press and repeats. */
    extern s32 portVideoKbdStickDown(void);
    extern void portVideoKbdDigitalMove(s32 *dx, s32 *dy) __attribute__((weak)); /* video.c */
    extern s32 portInputPadDigMoveP1(s32 *dx, s32 *dy) __attribute__((weak));
    s32 port_dx = 0;
    s32 port_dy = 0;
    s32 port_ky = 0;
    s32 port_stick;

    if (portVideoKbdDigitalMove != NULL)
    {
        portVideoKbdDigitalMove(&port_dx, &port_ky);
    }
    if (portInputPadDigMoveP1 != NULL)
    {
        portInputPadDigMoveP1(&port_dx, &port_dy);
    }
    port_stick = !(portVideoKbdStickDown() && port_ky != 0) && port_dy == 0;
#else
#define port_stick 1
#endif

    count = bondinvCountTotalItemsInInv();

    if (!get_debug_gunwatchpos_flag() && port_stick)
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD | U_CBUTTONS) || joyGetStickY(PLAYER_1) >= 0x47)
        {
            if (((s32) watch_inventory_cursor_pos > 0) && !watch_item_is_actively_selected)
            {
                watch_inventory_cursor_pos -= 1.0f;
            }
        }
        else
        {
            if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD | D_CBUTTONS) || joyGetStickY(PLAYER_1) < -0x46)
            {
                goto down_body;
            }

            goto after_updown;

down_body:
            if (((s32) watch_inventory_cursor_pos < count - 1) && !watch_item_is_actively_selected)
            {
                watch_inventory_cursor_pos += 1.0f;
            }

after_updown:
            ;
        }

        if (joyGetButtons(PLAYER_1, U_JPAD | U_CBUTTONS))
        {
            if (((s32) watch_inventory_cursor_pos > 0) && !watch_item_is_actively_selected)
            {
                if (portWatchMenuField()) watch_inventory_cursor_pos -= 0.1f;
            }
        }
        else if (joyGetButtons(PLAYER_1, D_JPAD | D_CBUTTONS))
        {
            if (((s32) watch_inventory_cursor_pos < count - 1) && !watch_item_is_actively_selected)
            {
                if (portWatchMenuField()) watch_inventory_cursor_pos += 0.1f;
            }
        }
    }

    if (port_stick
        && joyGetStickY(PLAYER_1) >= 0x1f
        && joyGetStickY(PLAYER_1) < 0x46
        && g_curWatchItemIndex > 0
        && !watch_item_is_actively_selected)
    {
        if (portWatchMenuField()) watch_inventory_cursor_pos -= (f32) joyGetStickY(PLAYER_1) / 300.0f;
    }
    else if (port_stick
        && joyGetStickY(PLAYER_1) < -0x1e
        && joyGetStickY(PLAYER_1) >= -0x45
        && (s32) watch_inventory_cursor_pos < count - 1
        && !watch_item_is_actively_selected)
    {
        if (portWatchMenuField()) watch_inventory_cursor_pos -= (f32) joyGetStickY(PLAYER_1) / 300.0f;
    }
#ifdef TARGET_N64
#undef port_stick
#endif

#ifndef TARGET_N64
    /* hold to repeat here too (portWatchHoldRepeatTick) */
#define PORT_INV_UP   (watch_stick_y_pressed_up() || port_watch_rep[0])
#define PORT_INV_DOWN (watch_stick_y_pressed_down() || port_watch_rep[1])
#else
#define PORT_INV_UP   watch_stick_y_pressed_up()
#define PORT_INV_DOWN watch_stick_y_pressed_down()
#endif
    if (PORT_INV_UP && g_curWatchItemIndex > 0 && !watch_item_is_actively_selected)
    {
        watch_inventory_cursor_pos -= 1.0f;
    }
    else if (PORT_INV_DOWN && g_curWatchItemIndex < count - 1 && !watch_item_is_actively_selected)
    {
        watch_inventory_cursor_pos += 1.0f;
    }
#undef PORT_INV_UP
#undef PORT_INV_DOWN

    if (is_holding_less_than_10_up_on_stick() || is_holding_less_than_10_down_on_stick())
    {
        watch_stick_y_prev_active = 1;
    }
    else
    {
        watch_stick_y_prev_active = 0;
    }

    if ((f32) count - 0.5f < watch_inventory_cursor_pos)
    {
        watch_inventory_cursor_pos = (f32) count - 0.5f;
    }

    if (watch_inventory_cursor_pos < -0.5f)
    {
        watch_inventory_cursor_pos = -0.5f;
    }

    // The current item is determined by the integer part of the cursor's position.
    g_curWatchItemIndex = (s32) watch_inventory_cursor_pos;

    if (j_text_trigger)
    {
        item_line_height = 14;
    }
    else
    {
        item_line_height = 12;
    }

    selected_item_line_height = j_text_trigger ? 14 : 12;
    watch_inventory_text_target_y = (2 * selected_item_line_height) + (-g_curWatchItemIndex * item_line_height);

    if (watch_inventory_text_target_y < watch_inventory_text_y)
    {
        if (portWatchMenuField()) watch_inventory_text_y = (watch_inventory_text_y - ((watch_inventory_text_y - watch_inventory_text_target_y) / 3)) - 1;
        watch_inventory_text_is_settled = FALSE;
    }
    else if (watch_inventory_text_y < watch_inventory_text_target_y)
    {
        if (portWatchMenuField()) watch_inventory_text_y = (watch_inventory_text_y + ((watch_inventory_text_target_y - watch_inventory_text_y) / 3)) + ((0, 1));
        watch_inventory_text_is_settled = FALSE;
    }
    else
    {
        watch_inventory_text_is_settled = TRUE;
    }

    if (((f32) g_curWatchItemIndex + 0.55f < watch_inventory_cursor_pos)
        && !joyGetButtons(PLAYER_1, 0xffff)) // Any button
    {
        if (portWatchMenuField()) watch_inventory_cursor_pos -= 0.1f;
    }
    else if (watch_inventory_cursor_pos <= (f32) g_curWatchItemIndex + 0.45f
        && !joyGetButtons(PLAYER_1, 0xffff)) // Any button
    {
        if (portWatchMenuField()) watch_inventory_cursor_pos += 0.1f;
    }
}


void sub_GAME_7F0A611C(f32 *arg0, s32 *arg1, s32 arg2, s32 *arg3, s32 *arg4, s32 *arg5, s32 arg6, s32 arg7, s32 arg8)
{
    if (!get_debug_gunwatchpos_flag())
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD | U_CBUTTONS) || joyGetStickY(PLAYER_1) >= 0x47)
        {
            if ((s32)*arg0 > 0 && arg7)
            {
                *arg0 -= 1.0f;
            }
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD | D_CBUTTONS) || joyGetStickY(PLAYER_1) < -0x46)
        {
            if ((s32)*arg0 < arg2 - 1 && arg7)
            {
                *arg0 += 1.0f;
            }
        }

        if (joyGetButtons(PLAYER_1, U_JPAD | U_CBUTTONS))
        {
            if ((s32)*arg0 > 0 && arg7)
            {
                if (portWatchMenuField()) *arg0 -= 0.1f;
            }
        }
        else if (joyGetButtons(PLAYER_1, D_JPAD | D_CBUTTONS))
        {
            if ((s32)*arg0 < arg2 - 1 && arg7)
            {
                if (portWatchMenuField()) *arg0 += 0.1f;
            }
        }
    }

    if (joyGetStickY(PLAYER_1) >= 0x1f && joyGetStickY(PLAYER_1) < 0x46 && *arg1 > 0 && arg7)
    {
        if (portWatchMenuField()) *arg0 -= (f32)joyGetStickY(PLAYER_1) / 300.0f;
    }
    else if (joyGetStickY(PLAYER_1) < -0x1e && joyGetStickY(PLAYER_1) >= -0x45 && (s32)*arg0 < arg2 - 1 && arg7)
    {
        if (portWatchMenuField()) *arg0 -= (f32)joyGetStickY(PLAYER_1) / 300.0f;
    }

    if (watch_stick_y_pressed_up() && *arg1 > 0 && arg7)
    {
        *arg0 -= 1.0f;
    }
    else if (watch_stick_y_pressed_down() && *arg1 < arg2 - 1 && arg7)
    {
        *arg0 += 1.0f;
    }

    if (is_holding_less_than_10_up_on_stick() || is_holding_less_than_10_down_on_stick())
    {
        watch_stick_y_prev_active = 1;
    }
    else
    {
        watch_stick_y_prev_active = 0;
    }

    if ((f32)arg2 - 0.5f < *arg0)
    {
        *arg0 = (f32)arg2 - 0.5f;
    }

    if (*arg0 < -0.5f)
    {
        *arg0 = -0.5f;
    }

    *arg1 = (s32)*arg0;
    *arg4 = (arg6 * arg8) + (-*arg1 * arg8);

    if (*arg4 < *arg3)
    {
        *arg3 = (*arg3 - ((*arg3 - *arg4) / 3)) - 1;
        *arg5 = 0;
    }
    else if (*arg3 < *arg4)
    {
        *arg3 = (((*arg4 - *arg3) / 3) + *arg3) + 1;
        *arg5 = 0;
    }
    else
    {
        *arg5 = 1;
    }

    if ((f32)*arg1 + 0.55f < *arg0)
    {
        if (!joyGetButtons(PLAYER_1, ANY_BUTTON))
        {
            if (portWatchMenuField()) *arg0 -= 0.1f;
            return;
        }
    }

    if (*arg0 <= (f32)*arg1 + 0.45f)
    {
        if (!joyGetButtons(PLAYER_1, ANY_BUTTON))
        {
            if (portWatchMenuField()) *arg0 += 0.1f;
        }
    }
}


void mission_brief_background_navigation(void)
{
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        mission_brief_index = BRIEF_INDEX_OBJECTIVES;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        mission_brief_index = BRIEF_INDEX_M;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void mission_brief_m_briefing_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_BACKGROUND;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_Q;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void mission_brief_q_branch_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_M;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_MONEYPENNY;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}

void mission_brief_moneypenny_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_Q;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_OBJECTIVES;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}

void mission_brief_objectives_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_MONEYPENNY;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_BACKGROUND;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void build_watch_static_scanline_vertices(Vtx *vertices)
{
    s32 halfWidth; // Half width for the scanline
    Vtx *vertex;
    s32 zoffs;
    s32 greenChannelIndex;
    s32 side;

    // Fit the width of the scanline to the watch's green circle
    halfWidth = sqrtf(213444.0f - ((f32) (g_WatchStaticScanlineY * g_WatchStaticScanlineY)));

    for (zoffs = 0; zoffs != 8; zoffs += 4)
    {
        for (side = -1; side != 3; side += 2)
        {
            vertex = vertices;

            vertex->v.ob[0] = halfWidth * side;
            vertex->v.ob[1] = 0;
            vertex->v.ob[2] = zoffs + g_WatchStaticScanlineY;

            vertex->v.cn[1] = 0xA0;
            vertex->v.flag = 0;
            vertex->v.tc[0] = 0;
            vertex->v.tc[1] = 0;
            vertex->v.cn[0] = 0;

            greenChannelIndex = 1;
            vertex->v.cn[greenChannelIndex] = 0xA0;
            vertex->v.cn[2] = 0;
            vertex->v.cn[3] = g_WatchStaticScanlineAlpha;

            vertices++;
        }
    }
}


void sub_GAME_7F0A69A8(void)
{
    /* (port note: this forces Mission Status at every open and close - a
     * regular pause MUST open there; only the explicit
     * watch_objectives bind lands elsewhere, via sPortWatchPendingPage) */
    if (joyGetControllerCount() < 2)
    {
        D_800409D8 = 4;
    }
    else
    {
        D_800409D8 = 8;
    }
    reset_watch_item_is_actively_selected();
    watch_screen_index = WATCH_INDEX_MISSION_STATUS;
#ifndef TARGET_N64
    portWatchResetPage(); /* every open and close starts clean */
#endif
    mission_brief_index = BRIEF_INDEX_OBJECTIVES;
    D_800409C8 = 0.999f;
    D_800409CC = 0.9999f;
    bondinvDetermineEquippedItem();
}


/**
 * Address 0x7F0A6A2C. (VERSION_US, VERSION_JP)
 * Address 0x7F0A5D78. (VERSION_EU)
*/
f32 watchWrapAroundPI(f32 arg0)
{
    if (arg0 > M_PI_F)
    {
        arg0 = arg0 - M_TAU_F;
    }
    else if (arg0 < M_MINUS_PI_F)
    {
        arg0 = arg0 + M_TAU_F;
    }
    return arg0;
}


#ifndef TARGET_N64
/* An open drop-down or armed key capture owns left/right as well as up/down, so
 * the L/R tab flip must not run under either; this is sampled before
 * portWatchRowNavigate, which can end those states. The stick's left/right is a
 * latch, so it's consumed too and must recentre before it can flip a tab. */
static s32 portWatchNavModal(void)
{
    if (portWatchDropIsOpen() || portWatchBindCaptureActive())
    {
        set_controlstick_lr_disabled();
        return 1;
    }
    return 0;
}
#endif

extern f32 jpD_800484D0;
void sub_GAME_7F0A6A80(void)
{
    u32 temp_1;
    s32 temp_2;
    s32 temp_3;
    u32 random_value;
#ifndef TARGET_N64
    s32 portModal; /* portWatchNavModal(), sampled before portWatchRowNavigate */
#endif

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON)
#ifndef TARGET_N64
        && !portReturnStartThisTick() /* spec 6: Return confirms, it does not close the watch */
        /* an open list, armed capture or "press a button" is closed first, as
         * Escape and watch_objectives do: closing over a key capture left the
         * keyboard and mouse blanked in play */
        && !portWatchModalBack()
#endif
        )
    {
        set_open_close_solo_watch_menu_to1();
    }

#ifndef TARGET_N64
    portWatchApplyPendingPage(); /* watch_objectives bind: land on the requested page */
    portWatchHarnessTick();      /* GE_WATCHPAGE: park the watch on a page for a capture */
#endif
#ifdef GE_VIDEO
    portWatchMouseTick();
#endif
#ifndef TARGET_N64
    { /* The face's volume bars only change the live volume, so save it as soon
       * as a bar is let go (D-pad edit ended, drag released, wheel notch).
       * Otherwise quitting with the watch up lost it and Display > Audio showed
       * stale values; portAdoptSaveVolumes writes only on a real change. */
        extern void portAdoptSaveVolumes(s32 musicraw, s32 sfxraw, s32 fromload);
        const s32 baredit = watch_screen_index == WATCH_INDEX_GAME_OPTIONS && g_portWatchPage == PORTWATCH_GENERAL
                            && watch_item_is_actively_selected && game_options_index <= 1;

        if (!baredit
#ifdef GE_VIDEO
            && sPortWatchDrag == 0
#endif
            )
        {
            portAdoptSaveVolumes(get_mTrack2Vol(), sndGetSfxSlotFirstNaturalVolume(), 0);
        }
    }
#endif

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): watch-menu reversal hunt - raw stick
       * vs which watch screen/item is selected, 2Hz. */
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        static int dump = -1;
        static u32 calls;
        if (dump < 0)
        {
            dump = getenv("GE_DUMP_BOND") != NULL;
        }
        if (dump && (calls++ % 30) == 0)
        {
            printf("watch: raw=%d,%d lrrdy=%d yrdy=%d screen=%d gameopt=%d\n",
                   joyGetStickX(PLAYER_1), joyGetStickY(PLAYER_1),
                   controlstick_lr_enabled, watch_stick_y_nav_ready,
                   watch_screen_index, game_options_index);
        }
    }
#endif

    if (controlstick_lr_enabled == 0)
    {
        if ((joyGetStickX(PLAYER_1) >= -0xA) && (joyGetStickX(PLAYER_1) < 0xB))
        {
            controlstick_lr_enabled = 1;
        }
        else if ((joyGetStickX(PLAYER_1) < 0xB) && (joy7000C174(PLAYER_1) >= 0xB))
        {
            controlstick_lr_enabled = 1;
        }
        else if ((joyGetStickX(PLAYER_1) >= -0xA) && (joy7000C174(PLAYER_1) < -0xA))
        {
            controlstick_lr_enabled = 1;
        }
    }

#ifndef TARGET_N64
    portWatchHoldRepeatTick();
#endif
    if (watch_stick_y_nav_ready == 0)
    {
        if ((joyGetStickY(PLAYER_1) >= -0xA) && (joyGetStickY(PLAYER_1) < 0xB))
        {
            watch_stick_y_nav_ready = 1;
        }
        else if ((joyGetStickY(PLAYER_1) < 0xB) && (joy7000C284(PLAYER_1) >= 0xB))
        {
            watch_stick_y_nav_ready = 1;
        }
        else if ((joyGetStickY(PLAYER_1) >= -0xA) && (joy7000C284(PLAYER_1) < -0xA))
        {
            watch_stick_y_nav_ready = 1;
        }
    }

    temp_2 = D_80040AF8;
    if (temp_2 < 0)
    {
        D_80040AF4 = D_80040AF4 + 0xFFF00000;
    }
    D_80040AF8 = temp_2 - 1;

    if (D_80040AF4 < 0x5F00A1U)
    {
        D_80040AF4 = 0xFF00A0U;
        D_80040AF8 = 0xF;
    }

    temp_3 = D_80040B00;
    if (temp_3 < 0)
    {
        D_80040AFC = D_80040AFC - 0x10;
    }
    D_80040B00 = temp_3 - 1;

    if (D_80040AFC < 0x60U)
    {
        D_80040AFC = 0xFFU;
        D_80040B00 = 0xF;
    }
    #ifdef VERSION_US
    D_80040B14 += ((D_80040B1C * speedgraphframes * M_TAU_F) / 360.0f);
    #else
    D_80040B14 += ((D_80040B1C * jpD_800484D0 * M_TAU_F) / 360.0f);
    #endif

    D_80040B14 = watchWrapAroundPI(D_80040B14);

    temp_1 = D_80040B0C << 0x10;
    if (temp_1 < randomGetNext())
    {
        sub_GAME_7F0A51D8();
    }

    if (g_WatchBackgroundGreen < 0xE0)
    {
        random_value = randomGetNext();
        g_WatchBackgroundGreen += (random_value >> 0x1E);
    }

    if (g_WatchBackgroundGreen > 0xe0) {
        g_WatchBackgroundGreen = 0xe0;
    }

    g_WatchStaticScanlineAlpha = ((-g_WatchBackgroundGreen * 4) + 0x380);
    g_WatchStaticScanlineY = g_WatchStaticScanlineY - 4;

    if (g_WatchStaticScanlineY >= 0x157) {
        g_WatchStaticScanlineY = -0x156;
    }

    if (g_WatchStaticScanlineY < -0x156) {
        g_WatchStaticScanlineY = 0x156;
    }

    D_80040B44 = (s16)D_80040B44 + 1;
    D_80040B44 = (s16)D_80040B44 & 1;

    switch (watch_screen_index)
    {
        case WATCH_INDEX_MISSION_STATUS:
            watch_screen0_navigation();
            break;

        case WATCH_INDEX_CONTROL_OPTIONS:
#ifndef TARGET_N64
            if (portWatchAssignTick())
            {
                break;   /* "press a button": no walk, no tab flip, until it answers */
            }
            if (!portWatchRaphnetView())
            {
                /* the port page owns this tab unless a raphnet adapter is
                 * the selected device */
                portModal = portWatchNavModal();
                portWatchRowNavigate();
                if (!portModal) /* a list or an armed capture owns left/right */
                {
                    watch_screen2_navigation();
                }
                break;
            }
            /* the raphnet page is THREE rows, so Rare's two handlers below -
             * each of which means "go to the other row" in both directions -
             * cannot walk it. One navigator owns all three. */
            portWatchRaphnetNavigate();
#else
            switch (controller_options_index)
            {
                case CONTROLLER_OPTIONS_INDEX_STYLE:
                    controller_options_controlstyle_navigation();
                    break;

                case CONTROLLER_OPTIONS_INDEX_INPUTS:
                    controller_options_inputs_navigation();
            }
#endif
            watch_screen2_navigation();
            break;

        case WATCH_INDEX_GAME_OPTIONS:
#ifndef TARGET_N64
            /* Not a switch on game_options_index any more: Rare's switch
             * stopped at index 9, so the port pages' tenth row had no
             * navigation and the list couldn't wrap. portWatchRowNavigate wraps
             * over the page's own rows (sliders included) and also cancels a
             * pending resolution change. */
            portModal = portWatchNavModal();
            portWatchRowNavigate();
#else
            switch (game_options_index)
            {
                case GAME_OPTIONS_INDEX_MUSIC:
                    game_options_music_volume_navigation();
                    break;

                case GAME_OPTIONS_INDEX_FX:
                    game_options_fx_volume_navigation();
                    break;

                case GAME_OPTIONS_INDEX_LOOK_UPDOWN:
                case GAME_OPTIONS_INDEX_AUTO_AIM:
                case GAME_OPTIONS_INDEX_AIM_CONTROL:
                case GAME_OPTIONS_INDEX_SIGHT_ONSCREEN:
                case GAME_OPTIONS_INDEX_LOOK_AHEAD:
                case GAME_OPTIONS_INDEX_AMMO_ONSCREEN:
                case GAME_OPTIONS_INDEX_SCREEN_SIZE:
                case GAME_OPTIONS_INDEX_RATIO:
                    sub_GAME_7F0A5998();
            }
#endif
#ifndef TARGET_N64
            if (!portModal) /* a list or an armed capture owns left/right */
#endif
            {
                watch_screen3_navigation();
            }
            break;

        case WATCH_INDEX_MISSION_BRIEFING:
            watch_screen4_navigation();
            break;

        case WATCH_INDEX_INVENTORY:
            watch_screen1_navigation();
    }
}



Gfx *sub_GAME_7F0A6EE8(Gfx *DL)
{
    gSPSetGeometryMode(DL++, G_CULL_BACK);
    gDPSetCycleType(DL++, G_CYC_1CYCLE);
    gDPPipelineMode(DL++, G_PM_1PRIMITIVE);
    gDPSetScissor(DL++, G_SC_NON_INTERLACE, 0, 0,viGetX(), viGetY() );
    gDPSetTextureLOD(DL++, G_TL_TILE);
    gDPSetTextureLUT(DL++, G_TT_NONE);
    gDPSetTextureDetail(DL++, G_TD_CLAMP);
    gDPSetTexturePersp(DL++, G_TP_PERSP);
    gDPSetTextureFilter(DL++, G_TF_BILERP);
    gDPSetTextureConvert(DL++, G_TC_FILT);
    gDPSetCombineLERP(DL++, 0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE);
    gDPSetCombineKey(DL++, G_CK_NONE);
    gDPSetAlphaCompare(DL++, G_AC_NONE);
    gDPSetRenderMode(DL++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetColorDither(DL++, G_CD_MAGICSQ);

    return DL;
}


void set_page_rectangle_colors(s32 watch_screen_index, struct WatchVertex *vertices)
{
    s32 i;

    // Unselected rectangles.
    for (i = 0; i < 20; i++)
    {
        vertices[i].color.r = 0x20;
        vertices[i].color.g = 0x70;
        vertices[i].color.b = 0x20;
    }

#ifdef GE_VIDEO
    // Port: tab under the mouse crosshair (a mid green between idle and current).
    if (g_portWatchHoverTab >= 0 && g_portWatchHoverTab != watch_screen_index)
    {
        for (i = g_portWatchHoverTab * 4; i <= g_portWatchHoverTab * 4 + 3; i++)
        {
            vertices[i].color.r = 0x38;
            vertices[i].color.g = 0xB0;
            vertices[i].color.b = 0x38;
        }
    }
#endif
    // Currently selected page rectangle.
    for (i = watch_screen_index * 4; i <= watch_screen_index * 4 + 3; i++)
    {
        vertices[i].color.r = 0x50;
        vertices[i].color.g = 0xF0;
        vertices[i].color.b = 0x50;

        // Currently selected page rectangle, but something else is in focus e.g. toggling options or manipulating the controller on the controller screen.
        if (watch_item_is_actively_selected)
        {
            vertices[i].color.r = 0x30;
            vertices[i].color.g = 0xA0;
            vertices[i].color.b = 0x30;
        }
    }
}


/**
 * @param gdl:
 * @param arg1: Something about watch view matrix.
 * @param zoom_squish: When set, will "unfold" the interior watch watch area (green + bars)
 * based on view distance during pause animation. This is used when starting to pause and bring
 * the watch up, and exiting pause menu to resume game play. If this is disabled then
 * the interior area will always be the same size as the watch container.
*/
Gfx *draw_background_health_and_armor(Gfx *gdl, Mtx *arg1, s32 zoom_squish)
{
    int i;
    struct WatchVertex *sp48;
    struct WatchVertex *sp44;
    Gfx *sp40;
    Gfx *sp3C;

    s32 stack_pad[6];

    f32 scale;

    sp48 = dynAllocateVertices(WATCH_BACKGROUND_VERTEX_COUNT);
    sp44 = dynAllocateVertices(WATCH_BACKGROUND_VERTEX_COUNT);
    sp40 = dynAllocate(0xF8);
    sp3C = dynAllocate(0xF8);

    /**
     * It seems at this point in rendering the background watch arms (hour,minute,second)
     * have already been drawn. Whatever is about to be drawn next is oriented
     * to the second hand of the watch.
     * The following commands reset the orientation for the background green
     * area and health bars. Or, commenting this out will draw the background
     * and slowly spin it around in sync with the second hand.
    */
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetPrimColor(gdl++, 0, 0, 0xE6, 0xE6, 0xE6, 0x00);
    gSPMatrix(gdl++, arg1, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    // end initial setup.

    scale = 1;

    if (check_watch_page_transistion_running())
    {
        scale = (g_CurrentPlayer->zoomintime * (g_CurrentPlayer->zoominfovynew - g_CurrentPlayer->zoominfovyold))
            / g_CurrentPlayer->zoomintimemax;

        if (scale < 0.0f)
        {
            scale = -scale;
        }

        if (scale > 1)
        {
            scale = 1;
        }

        scale = scale * scale;
    }

    if (zoom_squish == 1)
    {
        scale = 0.05f;
        g_WatchBackgroundGreen = 0xE0;

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x4 || g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x6)
        {
            scale = bondviewWatchAnimationRelated();
        }
    }

    guScale(&gfx_background_8007B0A0, 0.25f, 0.25f, 0.25f);

    gSPMatrix(gdl++, &gfx_background_8007B0A0, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);

    if (zoom_squish == 0)
    {
        gSPClearGeometryMode(gdl++, G_CULL_BOTH);
        // draw body armor bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_body_armor_bar_gdl));
        // draw health bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_health_bar_gdl));
    }

    /**
     * This section renders main background, side health bar & body armor bars while zooming in or out.
    */
    guScale(&gfx_background_8007B0E0, 1, 1, scale);

    gSPMatrix(gdl++, &gfx_background_8007B0E0, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);

    if (zoom_squish == 1)
    {
        gSPClearGeometryMode(gdl++, G_CULL_BOTH);
        // draw body armor bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_body_armor_bar_gdl));
        // draw health bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_health_bar_gdl));
    }
    /**
     * End health bar zoom section
    */

    sub_GAME_7F0A33F8(sp44, WATCH_BACKGROUND_VERTEX_COUNT, 0.92f, 0);
    draw_watch_background(sp3C, OS_PHYSICAL_TO_K0(sp44), WATCH_BACKGROUND_VERTEX_COUNT, 0);

    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, 0x00, 0xFF, 0x00, 0x00);
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(sp3C));
    gDPPipeSync(gdl++);

    /**
     * This section renders the green background area of the watch menu.
    */
    if (g_WatchBackgroundGreen < 0xE0)
    {
        sub_GAME_7F0A33F8(sp48, WATCH_BACKGROUND_VERTEX_COUNT, 0.899999976158f, 0);
        draw_watch_background(sp40, OS_PHYSICAL_TO_K0(sp48), WATCH_BACKGROUND_VERTEX_COUNT, 0);

        gDPSetRenderMode(gdl++, G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2);
    }
    else
    {
        sub_GAME_7F0A33F8(sp48, WATCH_BACKGROUND_VERTEX_COUNT, 0.899999976158f, 1);
        draw_watch_background(sp40, OS_PHYSICAL_TO_K0(sp48), WATCH_BACKGROUND_VERTEX_COUNT, 1);

        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    }

    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(sp40));
    /**
     * // end green background area.
    */

    /**
     * This section renders the green rectangles/page select at the bottom of the screen.
     * This is setup in bondview trigger_solo_watch_menu.
    */
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(g_CurrentPlayer->buffer_for_watch_greenbackdrop_DL));
    /**
     * // end green rectangles/page select section
    */

    for (i=0; i<WATCH_BACKGROUND_VERTEX_COUNT; i++)
    {
        sp48[i].color.a = (s8)g_WatchBackgroundGreen;
        sp44[i].color.a = (s8)g_WatchBackgroundGreen;
    }

    if (g_WatchBackgroundGreen < 0xE0)
    {
        // Create the thin green scanline that moves up the screen while the watch does static.
        build_watch_static_scanline_vertices(g_CurrentPlayer->buffer_for_watch_static_vertices);

        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(g_CurrentPlayer->buffer_for_watch_static_DL));
    }

    return gdl;
}


Gfx *draw_background_health_and_armor_transitioning(Gfx *gdl, Mtx *param_2)
{
    return draw_background_health_and_armor(gdl, param_2, 1);
}


Gfx *draw_abort_cancel_confirm(Gfx *gdl)
{
    s32 sp7C;
    s32 sp78;
    s32 sp74;
    s32 sp70;
    s32 sp6C;
    s32 sp68;
    s32 sp64;
    s32 sp60;

    struct font *pFontFile;
    struct fontchar *pFontChars;
    u8 *sp54;
    u8 *sp50;
    u8 *sp4C;

    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;
    sp54 = langGet(getStringID(LOPTIONS, OPTION_STR_24_ABORT_LF)); //abort:
    sp50 = langGet(getStringID(LOPTIONS, OPTION_STR_25_CONFIRM_LF)); //confirm
    sp4C = langGet(getStringID(LOPTIONS, OPTION_STR_26_CANCEL_LF)); //cancel
    sp7C = 0x51;

    sp78 = (j_text_trigger ? 0xF : 0) + 0xBD;

    sp74 = (j_text_trigger ? 0xA : 0) + 0x88;

    sp70 = sp6C = sp68 = (j_text_trigger ? 3 : 0) + (PAL ? 0x4E : 0x4C);

    if (watch_item_is_actively_selected != 0)
    {
        if (D_800409A4 == 0)
        {
            if ((joyGetStickX(PLAYER_1) >= 0x2E) || (joyGetButtons(PLAYER_1, 0x111) != 0))
            {
                D_800409A4 = 1;
            }
        }
        else
        {
            if (D_800409A4 != 0)
            {
                if ((joyGetStickX(PLAYER_1) < -0x2D) || (joyGetButtons(PLAYER_1, 0x222) != 0))
                {
                    D_800409A4 = 0;
                }
            }
        }
    }

    if (watch_item_is_actively_selected != 0)
    {
        textMeasure(&sp60, &sp64, sp54, pFontChars, pFontFile, 0);

        gdl = textRender(gdl, &sp7C, &sp70, sp54, pFontChars, pFontFile, 0xA0FFA0F0, sp64, sp60, 0, 0);

        if (D_800409A4 != 0)
        {
            gdl = textRenderOutlined(gdl, &sp78, &sp6C, sp50, pFontChars, pFontFile, -1, 0x7000A0, viGetX(), viGetY(), 0, 0);
            gdl = textRender(gdl, &sp74, &sp68, sp4C, pFontChars, pFontFile, 0xFF00B0, viGetX(), viGetY(), 0, 0);
        }
        else
        {
            if (D_800409A4 == 0)
            {
                gdl = textRender(gdl, &sp78, &sp6C, sp50, pFontChars, pFontFile, 0xFF00B0, viGetX(), viGetY(), 0, 0);
                gdl = textRenderOutlined(gdl, &sp74, &sp68, sp4C, pFontChars, pFontFile, -1, 0x7000A0, viGetX(), viGetY(), 0, 0);
            }
        }
    }
    else
    {
        textMeasure(&sp60, &sp64, sp54, pFontChars, pFontFile, 0);
        gdl = textRender(gdl, &sp7C, &sp70, sp54, pFontChars, pFontFile, 0x800080, sp64, sp60, 0, 0);
        gdl = textRender(gdl, &sp78, &sp6C, sp50, pFontChars, pFontFile, 0x800080, viGetX(), viGetY(), 0, 0);
        gdl = textRender(gdl, &sp74, &sp68, sp4C, pFontChars, pFontFile, 0x800080, viGetX(), viGetY(), 0, 0);
    }

    return gdl;
}


Gfx *draw_text_mission_status(Gfx *gdl)
{
    u8 *txtptr_1;
    u8 *txtptr_2;
    s32 sp64;
    s32 sp60;
    s32 sp5C;
    s32 sp58;
    struct font *pFontFile;
    struct fontchar *pFontChars;
    s32 sp4C;
    s32 joffset;

    txtptr_1 = langGet(getStringID(LOPTIONS, OPTION_STR_27_MISSIONSTATUS_LF)); //mission status:
    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;

    if (objectiveIsAllComplete())
    {
        sp4C = 0xFF00B0;
        txtptr_2 = langGet(getStringID(LOPTIONS, OPTION_STR_28_COMPLETE_LF)); //complete
    }
    else
    {
        sp4C = D_80040AF4;
        txtptr_2 = langGet(getStringID(LOPTIONS, OPTION_STR_29_INCOMPLETE_LF)); //incomplete
    }

    gdl = microcode_constructor(gdl);
    textMeasure(&sp5C, &sp58, txtptr_1, pFontChars, pFontFile, 0);
    sp64 = 0x51;
    sp60 = YOFFSET_MISSIONSTATUS;
    gdl = textRender(gdl, &sp64, &sp60, txtptr_1, pFontChars, pFontFile, 0xFF00B0, sp58, sp5C, 0, 0);

    if (j_text_trigger)
    {
        joffset = 0x22;
    }
    else
    {
        joffset = 0;
    }

    sp64 = sp64 + sp58 + joffset + 4;
    sp60 = sp60 - sp5C;
    textMeasure(&sp5C, &sp58, txtptr_2, pFontChars, pFontFile, 0);
    gdl = textRender(gdl, &sp64, &sp60, txtptr_2, pFontChars, pFontFile, sp4C, sp58, sp5C, 0, 0);
    gdl = draw_abort_cancel_confirm(gdl);

    return gdl;
}


Gfx *empty_draw_function(Gfx *gdl) {
  return gdl;
}


Gfx *draw_text_q_watch_v201_beta(Gfx *gdl)
{
    u8 *txtptr;
    s32 sp50;
    s32 sp4C;
    s32 sp48;
    s32 sp44;
    struct font *pFontFile;
    struct fontchar *pFontChars;
    s32 joffset;

    txtptr = langGet(getStringID(LOPTIONS, OPTION_STR_2B_QWATCHVERSION_LF)); //q watch v2.01 beta

    if (j_text_trigger)
    {
        joffset = -5;
    }
    else
    {
        joffset = 0;
    }
    sp50 = joffset + 0x65;
    sp4C = YOFFSET_7;
    sp48 = 0;
    sp44 = 0;
    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;
    gdl = microcode_constructor(gdl);
    textMeasure(&sp48, &sp44, txtptr, pFontChars, pFontFile, 0);
    gdl = textRender(gdl, &sp50, &sp4C, txtptr, pFontChars, pFontFile, 0xFF00B0, sp44, sp48, 0, 0);
    return gdl;
}




#ifndef _BONDWALK_H_
#define _BONDWALK_H_
typedef struct GunModelFileRecord {
    void *item_header;
    char *item_file_name;
    s32   has_no_model;
    void *item_weapon_stats;
    u16   upper_watch_text;
    u16   lower_watch_text;
    f32   watch_pos_x, watch_pos_y, watch_pos_z;
    f32   x_rotation,  y_rotation;
    u16   weapon_of_choice_text, watch_equipment_text;
    f32   equip_watch_x, equip_watch_y, equip_watch_z;
} GunModelFileRecord;
#endif
extern GunModelFileRecord gitem_structs[];
/* forward declarations so the compiler uses f32 calling convention ($f0) */
extern f32 bondinvGetVposWatchForIndex(s32 index);
extern f32 bondinvGetHposWatchForIndex(s32 index);
extern f32 bondinvGetDepthWatchForIndex(s32 index);
extern f32 bondinvGetDifferent45AngleForIndex(s32 index);
extern f32 bondinvGetXrotWatchForIndex(s32 index);
extern f32 bondinvGetYrotWatchForIndex(s32 index);
Gfx* draw_current_hand_item_and_ammo(Gfx* gdl) {
    Mtx* sp114;
    u16 perspNorm;
    Mtxf matrix2;
    Mtxf matrix;
    s32 sp8C;
    s32 sp88;
    s32 sp84;
    s32 sp80;
    struct font *sp7C;
    struct fontchar *sp78;
    s32 temp_v0;
    s32 sp70;
    f32 sp6C;
    f32 sp68;
    f32 sp64;
    f32 sp60;
    f32 rotx;
    f32 roty;
    s8* text;
    s8* text2;
    struct GunModelFileRecord *gitem;

    sp114 = dynAllocateMatrix();
    sp84 = 0;
    sp80 = 0;

    sp7C = ptrFontBankGothic;
    sp78 = ptrFontBankGothicChars;

    temp_v0 = bondinvGetCurEquippedItem();
    sp70 = bondinvGetTextbyInvIndex(temp_v0);
    sp6C = bondinvGetVposWatchForIndex(temp_v0);
    sp68 = bondinvGetHposWatchForIndex(temp_v0);
    sp64 = bondinvGetDepthWatchForIndex(temp_v0);
    sp60 = bondinvGetDifferent45AngleForIndex(temp_v0);
    rotx = bondinvGetXrotWatchForIndex(temp_v0);
    roty = bondinvGetYrotWatchForIndex(temp_v0);
    text = bondinvGetFirstTitlebyIndex(temp_v0);
    text2 = bondinvGetSecondTitlebyIndex(temp_v0);

    if (get_debug_gunwatchpos_flag() != 0) {
        gitem = &gitem_structs[getCurrentPlayerWeaponId(0)];

        if (joyGetButtons(0, 2) != 0) {
           gitem->watch_pos_y -= 2.0f;
        }

        if (joyGetButtons(0, 1) != 0) {
            gitem->watch_pos_y += 2.0f;
        }

        if (joyGetButtons(0, 4) != 0) {
           gitem->watch_pos_x += 2.0f;
        }

        if (joyGetButtons(0, 8) != 0) {
            gitem->watch_pos_x -= 2.0f;
        }

        if (joyGetButtons(0, 0x20) != 0) {
            gitem->watch_pos_z *= 0.98000002f;
        }

        if (joyGetButtons(0, 0x10) != 0) {
             gitem->watch_pos_z *= 1.0204082f;
        }
#ifdef DEBUG

            osSyncPrintf("gun watch pos x=%f[CL,CR] y=%f[CD,CU] z=%f[TL,TR] ", gitem->watch_pos_x, gitem->watch_pos_y, gitem->watch_pos_z);

#endif
    }


#if defined(LEFTOVERDEBUG)
    guPerspective(sp114, &perspNorm, sp60, 1.33333337f * WATCH_FRAME_WIDE_MUL, 10.0f, 10000.0f, 1.0f);
#else
    guPerspective(sp114, &perspNorm, sp60, 1.2838470f * WATCH_FRAME_WIDE_MUL, 10.0f, 10000.0f, 1.0f);
#endif

    gSPMatrix(gdl++, osVirtualToPhysical(sp114), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    matrix_4x4_set_rotation_around_y((roty * 6.2831855f) / 360.0f, &matrix2);
    matrix_4x4_set_rotation_around_z(6.2831855f - ((rotx * 6.2831855f) / 360.0f), &matrix);
    matrix_4x4_multiply_in_place(&matrix, &matrix2);
    matrix_4x4_set_lookat_target(&matrix, sp64, sp6C, sp68, 0.0f, sp6C, sp68, 0.0f, 1.0f, 0.0f);
    matrix_4x4_multiply_in_place(&matrix, &matrix2);

    gdl = sub_GAME_7F0A6EE8(gdl);

    if (g_WatchBackgroundGreen < 0xE0) {
        gdl = set_enviro_fog_for_items_in_solo_watch_menu(gdl, sp70, &matrix2, g_WatchBackgroundGreen + 1, 0x64DC6428);
    } else {
        gdl = set_enviro_fog_for_items_in_solo_watch_menu(gdl, sp70, &matrix2, 0xFF, 0x64DC6428);
    }

    gdl = microcode_constructor(gunDrawWatchAmmoDisplay(gdl));

    sp8C = 0x60;
#if defined(LEFTOVERDEBUG)
    sp88 = 0xA0;
#else
    sp88 = 0xBC;
#endif
    textMeasure(&sp84, &sp80, text, sp78, sp7C, 0);
    gdl = textRender(gdl, &sp8C, &sp88, text, sp78, sp7C, 0xFF00B0, sp80, sp84, 0, 0);

#if defined(LEFTOVERDEBUG)
    sp88 = 0xAA;
#else
    sp88 = 0xC6;
#endif
    textMeasure(&sp84, &sp80, text2, sp78, sp7C, 0);
    gdl = textRender(gdl, &sp8C, &sp88, text2, sp78, sp7C, 0xFF00B0, sp80, sp84, 0, 0);

    return gdl;
}


Gfx *draw_watch_mission_status_page(Gfx *gdl, Mtx *param_2)
{
    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        gdl = draw_text_q_watch_v201_beta(gdl);
        gdl = draw_text_mission_status(gdl);
        gdl = draw_current_hand_item_and_ammo(empty_draw_function(gdl));
    }
    else
    {
        check_watch_page_transistion_running();
    }

    return gdl;
}


void sub_GAME_7F0A8378(void)
{
#ifdef GE_VIDEO
    if (portWatchTakeClick())
    {
        return; /* watch mouse layer already equipped the clicked row */
    }
#endif
    if (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON) == 0) {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON) == 0)
        {
            return;
        }

        if (getCurrentPlayerWeaponId(0) == bondinvGetTextbyInvIndex(g_curWatchItemIndex))
        {
            return;
        }
    }

    currentPlayerUnEquipWeaponWrapper(0, bondinvGetTextbyInvIndex(g_curWatchItemIndex));
    currentPlayerUnEquipWeaponWrapper(1, 0);
    bondinvSetCurEquippedItem(g_curWatchItemIndex);
    D_800409C4 = 10;
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
}


#ifndef _BONDWALK_H_
typedef struct GunModelFileRecord {
    void *item_header;
    char *item_file_name;
    s32   has_no_model;
    void *item_weapon_stats;
    u16   upper_watch_text;
    u16   lower_watch_text;
    f32   watch_pos_x, watch_pos_y, watch_pos_z;
    f32   x_rotation,  y_rotation;
    u16   weapon_of_choice_text, watch_equipment_text;
    f32   equip_watch_x, equip_watch_y, equip_watch_z;
} GunModelFileRecord;
#endif
extern GunModelFileRecord gitem_structs[];
/* forward declarations so the compiler uses f32 calling convention ($f0) */
extern f32 bondinvGet45AngleForIndex(s32 index);
extern f32 bondinvGetHoffsetForIndex(s32 index);
extern f32 bondinvGetVoffsetForIndex(s32 index);
extern f32 bondinvGetDepthForIndex(s32 index);
extern f32 bondinvGetXrotWatchForIndex(s32 index);
extern f32 bondinvGetYrotWatchForIndex(s32 index);
extern u16 *bondinvGetNameByIndex(s32 index);


Gfx *draw_watch_inventory_page(Gfx *gdl, Mtx *param_2)
{
    Mtx *sp924;
    u16 perspNorm;
    Mtxf sp8E0;
    Mtxf sp8A0;
    f32 temp_cos;
    f32 temp_sin;
    f32 sp894;
    f32 sp890;
    f32 sp88C;
    f32 sp888;
    s32 sp884;
    f32 sp880;
    f32 sp87C;
    s32 temp_s0_3;
    GunModelFileRecord *gitem;
    s32 x1;
    s32 y1;

    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        sp924 = dynAllocateMatrix();
        sp894 = bondinvGet45AngleForIndex(g_curWatchItemIndex);
        sp890 = bondinvGetHoffsetForIndex(g_curWatchItemIndex);
        sp88C = bondinvGetVoffsetForIndex(g_curWatchItemIndex);
        sp888 = bondinvGetDepthForIndex(g_curWatchItemIndex);
        sp884 = bondinvGetTextbyInvIndex(g_curWatchItemIndex);
        sp880 = bondinvGetXrotWatchForIndex(g_curWatchItemIndex);
        sp87C = bondinvGetYrotWatchForIndex(g_curWatchItemIndex);

        if (get_debug_gunwatchpos_flag() != 0)
        {
            gitem = &gitem_structs[getCurrentPlayerWeaponId(0)];

            if (joyGetButtons(0, L_CBUTTONS) != 0)
            {
                gitem->equip_watch_x -= 2.0f;
            }

            if (joyGetButtons(0, R_CBUTTONS) != 0)
            {
                gitem->equip_watch_x += 2.0f;
            }

            if (joyGetButtons(0, D_CBUTTONS) != 0)
            {
                gitem->equip_watch_y += 2.0f;
            }

            if (joyGetButtons(0, U_CBUTTONS) != 0)
            {
                gitem->equip_watch_y -= 2.0f;
            }

            if (joyGetButtons(0, L_TRIG) != 0)
            {
                gitem->equip_watch_z *= 0.98000002f;
            }

            if (joyGetButtons(0, R_TRIG) != 0)
            {
                gitem->equip_watch_z *= 1.0204082f;
            }

#if defined(VERSION_US) && defined(DEBUG)
            osSyncPrintf(
                "gun list pos x=%f[CL,CR] y=%f[CD,CU] z=%f[TL,TR] ",
                gitem->equip_watch_x,
                gitem->equip_watch_y,
                gitem->equip_watch_z);
#endif
        }

        guPerspective(sp924, &perspNorm, sp894, WATCH_PERSPECTIVE_ASPECT_RT, 10.0f, 10000.0f, 1.0f);

#undef WATCH_INV_ASPECT_RATIO

        gSPMatrix(gdl++, osVirtualToPhysical(sp924), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

        matrix_4x4_set_rotation_around_y((sp87C * M_TAU_F) / 360.0f, &sp8E0);
        matrix_4x4_set_rotation_around_z(M_TAU_F - ((sp880 * M_TAU_F) / 360.0f), &sp8A0);
        matrix_4x4_multiply_in_place(&sp8A0, &sp8E0);

        temp_cos = cosf(D_80040B14) * sp888;
        temp_sin = sinf(D_80040B14) * sp888;

        matrix_4x4_set_lookat_target(&sp8A0, temp_cos, sp88C, temp_sin + sp890, 0.0f, sp88C, sp890, 0.0f, 1.0f, 0.0f);

        matrix_4x4_multiply_in_place(&sp8A0, &sp8E0);

        gdl = set_enviro_fog_for_items_in_solo_watch_menu(sub_GAME_7F0A6EE8(gdl), sp884, &sp8E0, 0x40, 0xA0FFA03C);

        {
            s32 i;
            s32 textheight;
            s32 textwidth;
            struct font *pFontFile2;
            struct fontchar *pFontChars2;
            char string_builder_allocation[2000];

#if defined(VERSION_JP) || defined(VERSION_EU)
            struct font *pFontFile;
            s32 base_y;
            char formattedString[32];
#endif

#define LINEHEIGHT() (j_text_trigger ? 14 : 12)

#if defined(VERSION_JP) || defined(VERSION_EU)
#define WATCH_INV_BASE_Y() base_y
#else
#define WATCH_INV_BASE_Y() 0x8C
#endif

            textheight = 0;
            textwidth = 0;

#if defined(VERSION_EU)
            pFontFile2 = ptrFontBankGothic;
            pFontChars2 = ptrFontBankGothicChars;
            base_y = (j_text_trigger) ? (0x82) : (0xAA);
            string_builder_allocation[0] = 0;
#elif defined(VERSION_JP)
            pFontFile2 = ptrFontBankGothic;
            pFontChars2 = ptrFontBankGothicChars;
            base_y = (j_text_trigger) ? (0x82) : (0x8C);
            string_builder_allocation[0] = 0;
#else
            string_builder_allocation[0] = 0;
            pFontFile2 = ptrFontBankGothic;
            pFontChars2 = ptrFontBankGothicChars;
#endif

            for (i = 0; i < bondinvCountTotalItemsInInv(); i++)
            {
                char *name = bondinvGetNameByIndex(i);

                strcat(string_builder_allocation, name);
            }

            if (D_800409C4 > 0)
            {
                D_800409C4--;
            }

            game_options_inventory_navigation();

            x1 = 0x4E;
            y1 = WATCH_INV_BASE_Y();

            temp_s0_3 = 1;
            temp_s0_3 = (LINEHEIGHT() * 2) + WATCH_INV_BASE_Y() + temp_s0_3;

            gdl = microcode_constructor(gdl);

            textMeasure(&textheight, &textwidth, string_builder_allocation, pFontChars2, pFontFile2, LINEHEIGHT());
#ifdef GE_VIDEO
            g_portWatchInvTextWidth = textwidth;   /* watch mouse: row hit width */
#endif

            gdl = microcode_constructor_related_to_menus(gdl, 0x4E, WATCH_INV_BASE_Y(), textwidth + 0x4E, (LINEHEIGHT() * 5) + WATCH_INV_BASE_Y(), 0);

            gdl = textRender(gdl, &x1, &y1, string_builder_allocation, pFontChars2, pFontFile2, 0xAA00B0, textwidth + 1, LINEHEIGHT() * 5, watch_inventory_text_y, LINEHEIGHT());

            gdl = microcode_constructor_related_to_menus(gdl, 0x4B, temp_s0_3, textwidth + 0x52, (LINEHEIGHT() + temp_s0_3) - 2, 0x800050);

            {
#if !defined(VERSION_JP) && !defined(VERSION_EU)
                char formattedString[32];
                struct font *pFontFile;
#endif
                struct fontchar *pFontChars;
                s32 x2;
                s32 y2;
                char *invItemName;

                pFontFile = ptrFontBankGothic;
                pFontChars = ptrFontBankGothicChars;
                invItemName = bondinvGetNameByIndex(g_curWatchItemIndex);

                sprintf(formattedString, "%d, %d\n%d %f\n", watch_inventory_text_y, watch_inventory_text_target_y, g_curWatchItemIndex, (f64) watch_inventory_cursor_pos);

                gdl = microcode_constructor(gdl);

                textMeasure(&y2, &x2, formattedString, pFontChars, pFontFile, 0);

                if (watch_inventory_text_is_settled != 0)
                {
                    textMeasure(&y2, &x2, invItemName, pFontChars, pFontFile, LINEHEIGHT());

                    x1 = 0x4E;
                    y1 = (LINEHEIGHT() * 2) + WATCH_INV_BASE_Y();

                    if (D_800409C4 == 0)
                    {
                        gdl = textRender(gdl, &x1, &y1, invItemName, pFontChars, pFontFile, 0xA0FFA0F0, x2, 0x64, 0, LINEHEIGHT());
                    }
                    else
                    {
                        gdl = textRenderOutlined(gdl, &x1, &y1, invItemName, pFontChars, pFontFile, -1, 0x7000A0, x2 + 1, 0x64, 0, LINEHEIGHT());
                    }

                    sub_GAME_7F0A8378();
                }
            }

#undef WATCH_INV_BASE_Y
#undef LINEHEIGHT
        }
    }

    return gdl;
}


Gfx *unused_draw_watch_inventory_page(Gfx *gdl, Mtx *param_2) {
    s32 temp_1;
    s32 sp70;
    s32 sp6C;
    s32 sp64,sp68; //unused?

    s32 sp60;
    s32 sp5C;
    s32 sp58;
    s32 sp54;
    struct font *pFontFile;
    struct fontchar *pFontChars;

    u16 *long_name;
    s32 temp_2;

    sp58 = 0;
    sp54 = 0;

    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;

    long_name = bondinvGetLongNameByIndex(g_curWatchItemIndex);
    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        temp_1 = D_800409C4;
        if (temp_1 > 0)
        {
            D_800409C4 = temp_1 - 1;
        }

        game_options_inventory_navigation();
        gdl = microcode_constructor(gdl);

        textMeasure(&sp58, &sp54, long_name, pFontChars, pFontFile, 0);

        sp70 = ((s32) (0xAA - sp54) / 2) + 0x4B;
        temp_2 = sp70;

        sp6C = 0x1E;
        gdl = microcode_constructor_related_to_menus(gdl, temp_2, 0x1E, sp60, sp5C, 0x800050);

        if (watch_inventory_text_is_settled)
        {
            sub_GAME_7F0A8378();
            if (D_800409C4 == 0)
            {
                gdl = textRender(gdl, &sp70, &sp6C, long_name, pFontChars, pFontFile, 0xA0FFA0F0, sp54, 0x64, 0, 0);
            }
            else
            {
                gdl = textRenderOutlined(gdl, &sp70, &sp6C, long_name, pFontChars, pFontFile, -1, 0x7000A0, sp54 + 1, 0x64, 0, 0);
            }
        }
        else
        {
            gdl = textRender(gdl, &sp70, &sp6C, long_name, pFontChars, pFontFile, 0xAA00B0, sp54, 0x64, 0, 0);
        }

    }

    return gdl;
}


/**
 * Address: 7F0A8D40
 */
void update_volume_slider_verts(struct WatchVertex *verts, f32 fill_amount, s32 transition_width)
{
    s32 i;
    struct WatchVertex *vtx;
    s32 xdiff;
    s32 filledrightx;

    xdiff = verts[2].coord1.x - verts[4].coord1.x;
    transition_width = (s32) (((f32) transition_width) * (1.2f - fill_amount));
    i = 0;
    vtx = verts;

    /**
     * Verts 0-3: unfilled right section.
     * Dark green.
     */
    do
    {
        i++;
        vtx++;
        vtx[-1].color.r = 0x20;
        vtx[-1].color.g = 0x40;
        vtx[-1].color.b = 0x20;
        vtx[-1].color.a = 0xE0;
    } while (i < 4);

    i = 4;
    vtx = &verts[4];

    /**
     * Verts 4-9: filled left section and transition.
     * Verts 10 and 11: moving boundary between transition and unfilled section.
     */
    do
    {
        filledrightx = xdiff + transition_width;

        if (i < 10)
        {
            // The filled section gets brighter as the volume increases.
            s32 rb;
            s32 g;
            rb = ((s32) (48.0f * fill_amount)) + 0x40;
            g = ((s32) (96.0f * fill_amount)) + 0x80;
            vtx->color.r = rb;
            vtx->color.g = g;
            vtx->color.b = rb;

            // Left edge of the transition band.
            if (i >= 6)
            {
                vtx->coord1.x = (s32) ((((f32) verts[4].coord1.x) + ((((f32) xdiff) + ((f32) transition_width)) * fill_amount)) - ((f32) transition_width));
                if (vtx->coord1.x < verts[4].coord1.x)
                {
                    vtx->coord1.x = verts[4].coord1.x;
                }
            }
        }
        else
        {
        // Right edge of the transition band.
        vtx->coord1.x = (s32) ((((f32) verts[4].coord1.x) + (((f32) filledrightx) * fill_amount)) + ((f32) transition_width));
            if (verts[2].coord1.x < vtx->coord1.x)
            {
                vtx->coord1.x = verts[2].coord1.x;
            }
        }

    i++;
    vtx++;

    } while (i != 12);

    // Make the unfilled section begin at the right edge of the transition area.
    filledrightx = verts[10].coord1.x;
    verts[1].coord1.x = filledrightx;
    verts[0].coord1.x = filledrightx;
}



/**
 * Address: 7F0A8ED0
 */
void watch_adjust_volume_slider(u16* outVolume) {
#ifndef TARGET_N64
    if (!portWatchMenuField()) { return; } /* 60 Hz menu field (bondview2.c portPauseBoxTick) */
#endif
    s32 joy_x;
    s32 adjusted_volume;

    joy_x = joyGetStickX(PLAYER_1);
    adjusted_volume = *outVolume;

    if (joyGetButtons(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) {
        adjusted_volume = adjusted_volume + WATCH_VOL_ADJUST_STEP;
    } else if (joyGetButtons(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) {
        adjusted_volume = adjusted_volume - WATCH_VOL_ADJUST_STEP;
    }

    // Clamp stick deflection
    if (joy_x >= 0x47) {
        joy_x = 0x46;
    } else if (joy_x < -0x46) {
        joy_x = -0x46;
    }

    // Increase volume
    if (joy_x >= 8) {
        adjusted_volume += (joy_x * 0x800 + -0x3800) / 0x46;
    // Decrease volume
    } else if (joy_x < -7) {
        adjusted_volume += (joy_x * 0x800 + 0x3800) / 0x46;
    }

    // Clamp volume between min and max allowed volume.
    if (adjusted_volume >= VOLUME_MAX + 1) {
        *outVolume = VOLUME_MAX;
    } else if (adjusted_volume < 0) {
        *outVolume = 0;
    } else {
        *outVolume = adjusted_volume;
    }
}


/**
 * Address: 7F0A8FEC
 */
Gfx *draw_fx_volume_slider(Gfx *gdl)
{
    u16 volume;
    f32 fvolume;
    struct WatchVertex *vtx1;
    struct WatchVertex *vtx;
    Gfx *cmd;

    vtx1 = (struct WatchVertex *)dynAllocateVertices(12);

    volume = sndGetSfxSlotFirstNaturalVolume();

    if (watch_item_is_actively_selected && game_options_index == 1)
    {
        watch_adjust_volume_slider(&volume);
    }

    fvolume = (f32)(u32)volume / 32767.0f;

    sndApplyVolumeAllSfxSlot(volume);

    if (1);

    cmd = gdl++;
    gDPSetRenderMode(cmd, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx1));
    vtx = setup_watch_rectangles(vtx1, 0, 0, (s32) PORTWATCH_BAR_W, (s32) PORTWATCH_BAR_H, (s32) PORTWATCH_BAR_X, (s32) PORTWATCH_BAR_FX_Z);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    vtx = setup_watch_rectangles(vtx, 0, 0, (s32) PORTWATCH_BAR_W, (s32) PORTWATCH_BAR_H, (s32) PORTWATCH_BAR_X, (s32) PORTWATCH_BAR_FX_Z);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    setup_watch_rectangles(vtx, 0, 0, (s32) PORTWATCH_BAR_W, (s32) PORTWATCH_BAR_H, (s32) PORTWATCH_BAR_X, (s32) PORTWATCH_BAR_FX_Z);

    update_volume_slider_verts(vtx1, fvolume, 30);

    return gdl;
}


u16 call_sndGetSfxSlotFirstNaturalVolume(void) {
    return sndGetSfxSlotFirstNaturalVolume();
}


void sub_GAME_7F0A91A0(u16 arg0) {
    sndApplyVolumeAllSfxSlot(arg0);
}


/**
 * Address: 7F0A91C8
 */
Gfx *draw_music_volume_slider(Gfx *gdl)
{
    u16 volume;
    f32 fvolume;
    struct WatchVertex *vtx1;
    struct WatchVertex *vtx;
    Gfx *cmd;

    vtx1 = (struct WatchVertex *)dynAllocateVertices(12);
    volume = get_mTrack2Vol();

    if (watch_item_is_actively_selected && game_options_index == 0) {
        watch_adjust_volume_slider(&volume);
    }

    fvolume = (f32)(u32)volume / 32767.0f;
    set_mTrack2Vol(volume);

    if(1);

    cmd = gdl++;
    gDPSetRenderMode(cmd, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx1));
    vtx = setup_watch_rectangles(vtx1, 0, 0, (s32) PORTWATCH_BAR_W, (s32) PORTWATCH_BAR_H, (s32) PORTWATCH_BAR_X, (s32) PORTWATCH_BAR_MUSIC_Z);


    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    vtx = setup_watch_rectangles(vtx, 0, 0, (s32) PORTWATCH_BAR_W, (s32) PORTWATCH_BAR_H, (s32) PORTWATCH_BAR_X, (s32) PORTWATCH_BAR_MUSIC_Z);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    setup_watch_rectangles(vtx, 0, 0, (s32) PORTWATCH_BAR_W, (s32) PORTWATCH_BAR_H, (s32) PORTWATCH_BAR_X, (s32) PORTWATCH_BAR_MUSIC_Z);

    update_volume_slider_verts(vtx1, fvolume, 30);

    return gdl;
}


u16 get_mTrack2Vol(void)
{
  return mTrack2Vol;
}

#ifndef TARGET_N64
/* Rare's six player options (game_options_entries[0..5]) are kept as one global
 * set in midas.ini (video.c s_CfgPlayerOpt) laid over the save file's, like the
 * volumes. A file load seeds the set only if none exists; watch changes are
 * adopted into it at once and the options-menu rows re-apply here. */
#define PORT_PLAYEROPT_COUNT 6

/** midas.ini -> the live table. No-op until a global set exists. */
void portPlayerOptsApply(void)
{
    extern s32 portPlayerOptCfg(s32 which); /* video.c */
    s32 i;

    for (i = 0; i < PORT_PLAYEROPT_COUNT; i++)
    {
        const s32 v = portPlayerOptCfg(i);

        if (v >= 0)
        {
            game_options_entries[i].current_value = (u32) v;
        }
    }
    {   /* and the HUD ratio (video.c hud_wide) */
        extern s32 portHudWideCfg(void);

        if (portHudWideCfg() >= 0)
        {
            game_options_entries[PLAYER_OPTION_RATIO].current_value = (u32) portHudWideCfg();
        }
    }
}

/** The live table -> midas.ini. `fromload` = seed only (file2.c). */
void portPlayerOptsAdoptLive(s32 fromload)
{
    extern void portPlayerOptsAdopt(const s32 *vals, s32 fromload); /* video.c */
    s32 vals[PORT_PLAYEROPT_COUNT];
    s32 i;

    for (i = 0; i < PORT_PLAYEROPT_COUNT; i++)
    {
        vals[i] = (s32) game_options_entries[i].current_value;
    }
    portPlayerOptsAdopt(vals, fromload);
}

/** game_option_select_value: adopt if the value it just wrote is one of the six. */
void portPlayerOptsAdoptIf(const u32 *value)
{
    s32 i;

    for (i = 0; i < PORT_PLAYEROPT_COUNT; i++)
    {
        if (value == &game_options_entries[i].current_value)
        {
            portPlayerOptsAdoptLive(0);
            return;
        }
    }
    if (value == &game_options_entries[PLAYER_OPTION_RATIO].current_value)
    {   /* the watch's HUD row: global too (video.c hud_wide) */
        extern void portHudWideAdopt(s32 wide);

        portHudWideAdopt((s32) *value);
    }
}

/** front.c menu_init: lay the global set over the boot defaults, once. */
void portPlayerOptsApplyOnce(void)
{
    static s32 done;

    if (done)
    {
        return;
    }
    done = 1;
    portPlayerOptsApply();
}

/** Apply the stored global pair (midas.ini music_volume / sfx_volume). */
void portApplyStoredVolumes(void)
{
    extern s32 portMusicVolumeRaw(void); /* video.c; weak full-volume headless */
    extern s32 portSfxVolumeRaw(void);
    extern void portApplyGlobalVolumes(s32 musicraw, s32 sfxraw);

    portApplyGlobalVolumes(portMusicVolumeRaw(), portSfxVolumeRaw());
}

/** front.c menu_init: apply the stored pair once, on the first frontend tick. */
void portApplyGlobalVolumesOnce(void)
{
    static s32 done;

    if (done)
    {
        return;
    }
    done = 1;
    portApplyStoredVolumes();
}

/**
 * GLOBAL VOLUMES: keep the player's music and sound levels in menus and intros
 * too.
 *
 * Vanilla keeps Sound and Music volume in the save file, and the frontend
 * forces both to full every time it opens a menu (init_menu*_ and the actor
 * intro), so the setting only applies in-game and there is none before a file
 * is loaded.
 *
 * The port keeps the last save file's pair in midas.ini (video.c music_volume /
 * sfx_volume, mirrored by file2.c whenever a file's settings are loaded or
 * saved) and applies those wherever vanilla forced full. This function is that
 * applier, and the Extended Options rows call it too, so a change is heard
 * immediately.
 *
 * mTrack2Vol is the music setting the rest of the game reads back (mp_music.c
 * sub_GAME_7F0C0BF0 -> musicTrack1ApplySeqpVol), so setting it makes in-level
 * music follow as well.
 */
void portApplyGlobalVolumes(s32 musicraw, s32 sfxraw)
{
    static s32 lastm = -1, lastx = -1;

    set_mTrack2Vol((u16) musicraw);
    sndApplyVolumeAllSfxSlot((u16) sfxraw);
    musicTrack1ApplySeqpVol((u16) musicraw);

    if (musicraw != lastm || sfxraw != lastx)
    {
        extern int printf(const char *, ...);

        lastm = musicraw;
        lastx = sfxraw;
        printf("ge: audio: global volumes - music %d%%, sound %d%%\n",
               (musicraw * 100 + 0x3FFF) / 0x7FFF, (sfxraw * 100 + 0x3FFF) / 0x7FFF);
    }
}

#endif


void set_mTrack2Vol(u16 param_1)
{
    mTrack2Vol = param_1;
    musicTrack2ApplySeqpVol(mTrack2Vol);
}


/**
 * Address: 7F0A9398
 *
 * This draws the text for the toggle options (both option titles and values).
 * It also draws button names and the actions mapped to them on the controller screen.
 */
Gfx *draw_options_labels(Gfx *gdl, s32 x, s32 y, char *text, u32 colour, s32 outlined, u32 outlinecolour, s32 centre, s32 drawbg, u32 bgcolour, s32 rightalign)
{
    s32 textx;
    s32 textright;
    s32 textbottom;
    s32 textwidth;
    s32 textheight;
    struct font *font;
    struct fontchar *chars;

    font = ptrFontBankGothic;
    chars = ptrFontBankGothicChars;

    textMeasure(&textheight, &textwidth, text, chars, font, 10);

    if (centre)
    {
        textx = x - (textwidth / 2);
    }
    else if (rightalign)
    {
        textx = x - textwidth;
    }
    else
    {
        textx = x;
    }

    textright = textx + textwidth;
    textbottom = y + textheight;

    if (g_WatchBackgroundGreen < 0xe0)
    {
        /**
         * Increases the effect of fuzzy static mode on the text,
         * making text pixels more subject to disappearing or almost disappearing.
         */
        gDPSetRenderMode(gdl++, G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2);
    }
    else
    {
        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    }

    if (drawbg)
    {
        gdl = microcode_constructor_related_to_menus(gdl, textx - 1, (y + outlined) + 1, textright + 1, textbottom + 1, bgcolour);
    }

    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

    if (!outlined)
    {
        gdl = textRender(gdl, &textx, &y, text, chars, font, colour, textwidth, textheight, 0, 10);
    }

    if (outlined)
    {
        gdl = textRenderOutlined(gdl, &textx, &y, text, chars, font, colour, outlinecolour, textwidth + 1, textheight, 0, 10);
    }

    return gdl;
}


f32 sub_GAME_7F0A95C4(f32 param_1, f32 param_2, f32 param_3)
{
    if (param_1 < param_2) {
        param_1 += (param_2 - param_1) / param_3;
    } else if (param_2 < param_1) {
        param_1 -= (param_1 - param_2) / param_3;
    }

    return param_1;
}


s32 sub_GAME_7F0A9610(void) {

    if ((g_WatchControllerSpinAngle < 0.1f) &&
        (g_WatchControllerSpinAngle > -0.1f) &&
        (g_WatchControllerPitch < 0.1f) &&
        (g_WatchControllerPitch > -0.1f))
    {

        return 1;

    }
    return 0;
}


void sub_GAME_7F0A9684(s8 contpadnum, s32 *counter, f32 *value, f32 *step)
{
    s32 count;

    if ((joyGetStickX(contpadnum) >= 10) || (joyGetStickX(contpadnum) < -9))
    {
        if (watch_item_is_actively_selected)
        {
            count = 0;

            if (controller_options_index == 1)
            {
                goto zero_done;
            }
        }
    }

    count = *counter;

    if (count < 100)
    {
        count = (*counter = count + 1);
        count = *counter;
    }

    goto counter_done;

zero_done:
    *counter = 0;

counter_done:
    if (count >= 100)
    {
        *value = sub_GAME_7F0A95C4(*value, (-(*step)) / 10.0f, 4.0f);
    }
    else if (watch_item_is_actively_selected && (controller_options_index == 1))
    {
        *value = sub_GAME_7F0A95C4(*value, (((-((f32) joyGetStickX(contpadnum))) * 0.2f) * M_TAU_F) / 360.0f, 4.0f);
    }
}


/**
 * Address: 7F0A97D0
 */
Gfx *draw_controller_style_text(Gfx *gdl)
{
    char pad[12];
    char text[2000];
    s32 x;
    s32 y;
    s32 i;
    u16 *stringids;
    s32 textheight;
    s32 textwidth;
    struct font *font;
    struct fontchar *chars;
    s32 tmp;
    u8 *selectedtext;

    font = ptrFontBankGothic;
    chars = ptrFontBankGothicChars;
    textheight = 0;
    textwidth = 0;
    text[0] = '\0';
    i = 0;

    if (D_800409D8 > 0)
    {
        stringids = game_control_styles;

        do
        {
            strcat(text, langGet(*stringids));
            i++;
            stringids += 10;
            if (i);
        }
        while (i < D_800409D8);
    }

#ifndef TARGET_N64
    if (!(watch_item_is_actively_selected && controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE))
    {   /* Scroll the dim style list to the effective style the row shows (a
         * raphnet<chan>_style or padN_style beats the save), so the two names
         * don't overlap and a pick starts from the right entry. Only the
         * scroller moves; the save's field changes when the row is actually
         * used. */
        const s32 shown = PORT_SHOWN_CTLTYPE;

        if (shown != g_CurrentPlayer->cur_player_control_type_0 && shown >= 0 && shown < D_800409D8)
        {
            g_CurrentPlayer->cur_player_control_type_2 = (f32) shown;
            g_CurrentPlayer->cur_player_control_type_1 = -(((j_text_trigger) ? 0xe : 0xa) * shown);
            g_CurrentPlayer->neg_vspacing_for_control_type_entry = g_CurrentPlayer->cur_player_control_type_1;
        }
    }
#endif
    if (watch_item_is_actively_selected)
    {
        if (controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE)
        {
#ifndef TARGET_N64
            const int wasType = g_CurrentPlayer->cur_player_control_type_0;
#endif
            sub_GAME_7F0A611C(&g_CurrentPlayer->cur_player_control_type_2, &g_CurrentPlayer->cur_player_control_type_0, D_800409D8, &g_CurrentPlayer->neg_vspacing_for_control_type_entry, &g_CurrentPlayer->cur_player_control_type_1, (s32 *) (&g_CurrentPlayer->has_set_control_type_data), 0, 1, (j_text_trigger) ? (0xe) : (0xa));
#ifndef TARGET_N64
            { /* If this player's device has its own style (raphnet<chan>_style
               * or padN_style), it outranks the save in
               * cur_player_get_control_type, so write the pick there too. It
               * takes effect now and is remembered for that device. */
                extern s32 portInputStyleGet(s32 slot);
                extern s32 portInputSlotDevice(s32 slot);
                extern void portInputDeviceStyleSet(s32 dev, s32 style);
                const s32 pn = get_cur_playernum();

                if (g_CurrentPlayer->cur_player_control_type_0 != wasType && portInputStyleGet(pn) >= 0
                    && portInputSlotDevice(pn) > 0)
                {
                    portInputDeviceStyleSet(portInputSlotDevice(pn), g_CurrentPlayer->cur_player_control_type_0);
                }
            }
#endif
        }
    }

    x = 0xaa;
    tmp = j_text_trigger;
    y = 0x1a;

    textMeasure(&textheight, &textwidth, text, chars, font, (tmp) ? (0xe) : (0xa));

    i = (j_text_trigger) ? (0xe) : (0xa);

    gdl = textRender(gdl, &x, &y, text, chars, font, 0x00aa00b0, textwidth, i, g_CurrentPlayer->neg_vspacing_for_control_type_entry, (j_text_trigger) ? (0xe) : (0xa));

    if (g_CurrentPlayer->has_set_control_type_data != 0)
    {
        selectedtext = langGet(*(u16 *)((u8 *) game_control_styles + (PORT_SHOWN_CTLTYPE * 20)));

        textMeasure(&textheight, &textwidth, selectedtext, chars, font, (j_text_trigger) ? (0xe) : (0xa));

        x = 0xaa;

        if (j_text_trigger ? 1 : 0)
        {
            goto selected_y_set;
        }

        goto selected_y_set;

selected_y_set:
        y = 0x1a;

        selectedtext = langGet(*(u16 *)((u8 *) game_control_styles + (PORT_SHOWN_CTLTYPE * 20)));

        gdl = textRender(gdl, &x, &y, selectedtext, chars, font, 0xa0ffa0f0, textwidth, 0x64, 0, (j_text_trigger) ? (0xe) : (0xa));
    }

    return gdl;
}

 
Gfx *sub_GAME_7F0A9AB8(Gfx *gdl)
{
    u8 *dirtext1;
    u8 *dirtext2;
 
    if (game_options_entries[0].current_value == 1)
    {
        dirtext1 = langGet(getStringID(LOPTIONS, OPTION_STR_2D_UP_LF));
        dirtext2 = langGet(getStringID(LOPTIONS, OPTION_STR_2C_DOWN_LF));
    }
    else
    {
        dirtext1 = langGet(getStringID(LOPTIONS, OPTION_STR_2C_DOWN_LF));
        dirtext2 = langGet(getStringID(LOPTIONS, OPTION_STR_2D_UP_LF));
    }
 
    {
        char strA[] = "(A)\n";
        char strB[] = "(B)\n";
        char strZ[] = "(Z)\n";
        char strL[] = "(L)\n";
        char strR[] = "(R)\n";
        char strC[] = "(C)\n";
        char strPlus[] = "(+)\n";
        char strS[] = "(S)\n";
        char str3D[] = "(3D)\n";
        u8 *ctext;
        u8 *dpadtext;
        s32 buttons;
        s32 showmovesight;
        volatile unsigned int y;
        u8 pad[8];
 
        showmovesight = 0;
        gdl = microcode_constructor(gdl);
 
        if (joyGetButtons(PLAYER_1, L_TRIG))
        {
            gdl = draw_options_labels(gdl, 0x32, OPTLABELS_ROW1_Y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 8)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
 
            if (*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 8) == getStringID(LOPTIONS, OPTION_STR_01_AIM_LF))
            {
                showmovesight = 1;
            }
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x32, OPTLABELS_ROW1_Y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 8)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        }
 
        y = OPTLABELS_ROW2_Y;
 
        if (controller_options_index != 1 || !watch_item_is_actively_selected || !joyGetButtons(PLAYER_1, U_JPAD | D_JPAD | L_JPAD | R_JPAD))
        {
            gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 14)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        }
        else
        {
            if (joyGetButtons(PLAYER_1, U_JPAD))
            {
                if (*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 14) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                {
                    dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_30_FORWARD_LF));
                }
                else
                {
                    dpadtext = dirtext1;
                }
            }
            else if (joyGetButtons(PLAYER_1, D_JPAD))
            {
                if (*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 14) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                {
                    dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_31_BACK_LF));
                }
                else
                {
                    dpadtext = dirtext2;
                }
            }
            else if (joyGetButtons(PLAYER_1, L_JPAD))
            {
                dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_2F_SIDESTEP_LF));
            }
            else if (joyGetButtons(PLAYER_1, R_JPAD))
            {
                dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_2E_SIDESTEP_LF));
            }
 
            gdl = draw_options_labels(gdl, 0x32, y, dpadtext, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
        }
 
        y += OPTLABELS_ROW_PITCH;
        gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 16)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        y += OPTLABELS_ROW_PITCH;
 
        if (joyGetButtons(PLAYER_1, Z_TRIG))
        {
            gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 6)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
 
            if (*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 6) == getStringID(LOPTIONS, OPTION_STR_01_AIM_LF))
            {
                showmovesight = 1;
            }
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 6)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        }
 
        y -= OPTLABELS_COL_RET;
 
        if (joyGetButtons(PLAYER_1, R_TRIG))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 10)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
 
            if (*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 10) == getStringID(LOPTIONS, OPTION_STR_01_AIM_LF))
            {
                showmovesight = 1;
            }
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 10)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        y += OPTLABELS_ROW_PITCH;
 
        if (controller_options_index != 1 || !watch_item_is_actively_selected || !joyGetButtons(PLAYER_1, U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 12)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
        else
        {
            buttons = joyGetButtons(PLAYER_1, U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS);
 
            /* power-of-two test: exactly one C button held */
            if ((buttons & (buttons - 1U)) == 0)
            {
                if (joyGetButtons(PLAYER_1, U_CBUTTONS))
                {
                    if (*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 12) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                    {
                        ctext = langGet(getStringID(LOPTIONS, OPTION_STR_30_FORWARD_LF));
                    }
                    else
                    {
                        ctext = dirtext1;
                    }
                }
                else if (joyGetButtons(PLAYER_1, D_CBUTTONS))
                {
                    if (*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 12) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                    {
                        ctext = langGet(getStringID(LOPTIONS, OPTION_STR_31_BACK_LF));
                    }
                    else
                    {
                        ctext = dirtext2;
                    }
                }
                else if (joyGetButtons(PLAYER_1, L_CBUTTONS))
                {
                    ctext = langGet(getStringID(LOPTIONS, OPTION_STR_2F_SIDESTEP_LF));
                }
                else if (joyGetButtons(PLAYER_1, R_CBUTTONS))
                {
                    ctext = langGet(getStringID(LOPTIONS, OPTION_STR_2E_SIDESTEP_LF));
                }
 
                gdl = draw_options_labels(gdl, 0x10e, y, ctext, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
            }
            else
            {
                gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 12)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
            }
        }
 
        y += OPTLABELS_ROW_PITCH;
 
        if (joyGetButtons(PLAYER_1, B_BUTTON))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 4)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 4)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        y += OPTLABELS_ROW_PITCH;
 
        if (joyGetButtons(PLAYER_1, A_BUTTON))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 2)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 2)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        if (showmovesight)
        {
            gdl = draw_options_labels(gdl, 0xfa, OPTLABELS_HINT_Y, langGet(getStringID(LOPTIONS, OPTION_STR_08_MOVESIGHT_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
        }
        else
        {
            gdl = draw_options_labels(gdl, 0xfa, OPTLABELS_HINT_Y, langGet(*(u16 *)((u8 *)game_control_styles + (PORT_SHOWN_CTLTYPE * 20) + 18)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        return gdl;
    }
}


Gfx *display_text_buttons_dual_control(Gfx *gdl)
{
    u8 *textptr_aux;

    gdl = microcode_constructor(gdl);

    if (joyGetButtons(PLAYER_1, A_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0); //weapon
    }
    else
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0); //weapon
    }

    if (joyGetButtons(PLAYER_1, B_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0); //action
    }
    else
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0); //action
    }

    if ((PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_PLENTY) || (PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_GALORE))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF)); //fire
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_01_AIM_LF)); //aim
    }

    if (joyGetButtons(PLAYER_1, Z_TRIG))
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_5, textptr_aux, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
    }
    else
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_5, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
    }

    if ((PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_PLENTY) || (PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_DOMINO))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF)); //move
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF)); //look
    }

    gdl = draw_options_labels(gdl, 0x5A, YOFFSET_4, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);

    if (joyGetButtons(PORT_SECOND_PAD, A_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1); //weapon
    }
    else
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1); //weapon
    }

    if (joyGetButtons(PORT_SECOND_PAD, B_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1); //action
    }
    else
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1); //action
    }

    if ((PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_PLENTY) || (PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_GALORE))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_01_AIM_LF)); //aim
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF)); //fire
    }

    if (joyGetButtons(PORT_SECOND_PAD, Z_TRIG))
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_5, textptr_aux, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
    }
    else
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_5, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
    }

    if ((PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_PLENTY) || (PORT_SHOWN_CTLTYPE == CONTROLLER_CONFIG_DOMINO))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF)); //look
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF)); //move
    }

    gdl = draw_options_labels(gdl, 0xE6, YOFFSET_4, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
    return gdl;
}


/**
 * Address: 7F0AADC0
 * 
 * Draw the controller model(s) and the individual buttons.
 */
Gfx *draw_watch_controller(Gfx *gdl)
{
    Mtx *perspmtx;
    Mtxf identity;
    Mtxf zrotmtx;
    Mtxf xrotmtx;
    Mtxf tmpmtx1;
    Mtxf tmpmtx2;
    Mtxf modelmtx;
    Mtxf finalmtx;
    coord3d pos;
    u16 perspNorm;
    Mtxf lookat1;
    Mtxf lookat2;
    void *watchTable;
    s32 green;
    s8 contpadnum0;
    s8 contpadnum1;
    WatchContButtonPositions table0;
    WatchContButtonPositions table1;
    WatchContButtonPositions table2;
    Gfx *cmd0;
    Gfx *cmd1;

    perspmtx = dynAllocateMatrix();
    pos = g_ControllerPos;
    contpadnum0 = 0;
    contpadnum1 = PORT_SECOND_PAD;
    table0 = g_1ContButtonPositions[0];
    table1 = g_2ContLeftButtonPositions[0];
    table2 = g_2ContRightButtonPositions[0];

    sub_GAME_7F0A9684(0, &D_80040B2C, &g_WatchControllerSpinSpeed, &g_WatchControllerSpinAngle);
    matrix_4x4_set_identity(&identity);

    g_WatchControllerSpinAngle += (g_WatchControllerSpinSpeed * ((f32) WATCH_ROTATION_FRAMES)) * 0.5f;
    g_WatchControllerSpinAngle = watchWrapAroundPI(g_WatchControllerSpinAngle);

    matrix_4x4_set_rotation_around_z(g_WatchControllerSpinAngle, &zrotmtx);

    if (watch_item_is_actively_selected && (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS))
    {
        g_WatchControllerPitch = sub_GAME_7F0A95C4(g_WatchControllerPitch, (((f32) joyGetStickY(PLAYER_1)) * M_TAU_F) / 360.0f, 4.0f);
    }
    else
    {
        g_WatchControllerPitch = sub_GAME_7F0A95C4(g_WatchControllerPitch, 0.0f, 4.0f);
    }

    matrix_4x4_set_rotation_around_x((-g_WatchControllerPitch) - 0.78539819f, &xrotmtx);
    matrix_4x4_multiply(&identity, &zrotmtx, &tmpmtx1);
    matrix_4x4_multiply(&tmpmtx1, &xrotmtx, &tmpmtx2);
    matrix_4x4_set_identity_and_position(&pos, &tmpmtx1);
    matrix_4x4_multiply(&tmpmtx1, &tmpmtx2, &modelmtx);

    if (controllerCheckDualControllerTypesAllowed())
    {
        f32 eye = 495.0f;

        watchTable = &table1;

        if (1);

        matrix_4x4_set_lookat_target(&lookat1, eye, 2500.0f, 32.0f, eye, 0.0f, 32.0f, 0.0f, 0.0f, -1.0f);
    }
    else
    {
        watchTable = &table0;
        matrix_4x4_set_lookat_target(&lookat1, -5.0f, 2000.0f, -168.0f, -5.0f, 0.0f, -168.0f, 0.0f, 0.0f, -1.0f);
    }

    matrix_4x4_multiply(&lookat1, &modelmtx, &finalmtx);

    guPerspective(perspmtx, &perspNorm, WATCH_PERSPECTIVE_FOVY, WATCH_PERSPECTIVE_ASPECT_RT, 1000.0f, 3000.0f, 1.0f);

    cmd0 = gdl++; \
    gSPMatrix(cmd0, osVirtualToPhysical(perspmtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

    gdl = sub_GAME_7F0A6EE8(gdl);
    green = g_WatchBackgroundGreen;

    if (green < 0xe0)
    {
        gdl = watchRenderController(gdl, &finalmtx, green - 6, 1, watchTable, &contpadnum0);
    }
    else
    {
        gdl = watchRenderControllerOpaque(gdl, &finalmtx, 1, watchTable, &contpadnum0);
    }

    if (controllerCheckDualControllerTypesAllowed())
    {
        sub_GAME_7F0A9684(PORT_SECOND_PAD, &D_80040B3C, &D_80040B38, &D_80040B34);
        matrix_4x4_set_identity(&identity);

        D_80040B34 += (D_80040B38 * ((f32) WATCH_ROTATION_FRAMES)) * 0.5f;
        D_80040B34 = watchWrapAroundPI(D_80040B34);

        matrix_4x4_set_rotation_around_z(D_80040B34, &zrotmtx);

        if (watch_item_is_actively_selected && (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS))
        {
            D_80040B30 = sub_GAME_7F0A95C4(D_80040B30, (((f32) joyGetStickY(PORT_SECOND_PAD)) * M_TAU_F) / 360.0f, 4.0f);
        }
        else
        {
            D_80040B30 = sub_GAME_7F0A95C4(D_80040B30, 0.0f, 4.0f);
        }

        matrix_4x4_set_rotation_around_x((-D_80040B30) - 0.78539819f, &xrotmtx);
        matrix_4x4_multiply(&identity, &zrotmtx, &tmpmtx1);
        matrix_4x4_multiply(&tmpmtx1, &xrotmtx, &tmpmtx2);
        matrix_4x4_set_identity_and_position(&pos, &tmpmtx1);
        matrix_4x4_multiply(&tmpmtx1, &tmpmtx2, &modelmtx);
        matrix_4x4_set_lookat_target(&lookat2, -505.0f, 2500.0f, 32.0f, -505.0f, 0.0f, 32.0f, 0.0f, 0.0f, -1.0f);
        matrix_4x4_multiply(&lookat2, &modelmtx, &finalmtx);

        guPerspective(perspmtx, &perspNorm, WATCH_PERSPECTIVE_FOVY, WATCH_PERSPECTIVE_ASPECT_RT, 1000.0f, 3000.0f, 1.0f);

        /**
         * This should be something like:
         * 
         *   cmd1 = gdl++;
         *   gSPMatrix(cmd1, osVirtualToPhysical(perspmtx),G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
         * 
         * but it doesn't match for some reason.
         */
        cmd1 = gdl++; cmd1->words.w0 = 0x01030040; cmd1->words.w1 = osVirtualToPhysical(perspmtx);

        gdl = sub_GAME_7F0A6EE8(gdl);
        green = g_WatchBackgroundGreen;

        if (green < 0xe0)
        {
            gdl = watchRenderController(gdl, &finalmtx, green - 6, 1, &table2, &contpadnum1);
        }
        else
        {
            gdl = watchRenderControllerOpaque(gdl, &finalmtx, 1, &table2, &contpadnum1);
        }
    }

    if (controllerCheckDualControllerTypesAllowed())
    {
        gdl = display_text_buttons_dual_control(gdl);
    }
    else
    {
        gdl = sub_GAME_7F0A9AB8(gdl);
    }

    return gdl;
}


void reset_controller_options_index(void) {
    controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
#ifndef TARGET_N64
    portWatchEnterControllerTab(); /* the port device picker + action list */
#endif
}


void reset_game_options_index(void) {
    game_options_index = 0;
#ifndef TARGET_N64
    /* arriving at the tab always lands on General, never on whichever
     * sub-page was open last time (spec section 3) */
    portWatchResetPage();
#endif
}


void zero_D_800409A4(void) {
    D_800409A4 = 0;
}


u32 return_arg0_7F0AB4B0(u32 uParm1) {
    return uParm1;
}


Gfx *draw_watch_control_options_page(Gfx *gdl, Mtx *param_2) {
    s32 phi_s1;
    u16 *textptr;
    s32 sp5C;
    s32 sp58;
    s32 sp54;
    s32 sp50;
    struct font *pFontFile;
    struct fontchar *pFontChars;

#ifndef TARGET_N64
    /* This tab is the device picker plus the action list, except for a raphnet
     * adapter, which can't be remapped and keeps the vanilla N64 controller
     * picture and 1.x/2.x styles. The vanilla body below is that raphnet case,
     * with a device row added on top so raphnet isn't a one-way door. */
    if (!portWatchRaphnetView())
    {
        if (g_portWatchPage < PORTWATCH_CONTROLLER)
        {
            portWatchEnterControllerTab(); /* see the same guard in the Game Options drawer */
        }
        gdl = draw_background_health_and_armor(gdl, param_2, 0);
        if (check_watch_page_transistion_running() != 1)
        {
            gdl = portWatchDrawRows(gdl);
        }
        return gdl;
    }
#endif

    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1) {

        gdl = draw_watch_controller(gdl);
#ifndef TARGET_N64
        gdl = portWatchDrawDeviceRow(gdl);
#endif
        pFontFile = ptrFontBankGothic;
        pFontChars = ptrFontBankGothicChars;

        gdl = microcode_constructor(gdl);
        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_32_CONTROLSTYLE_LF)); //control style

        sp5C = XOFFSET_1;
        sp58 = 0x1A;
        phi_s1 = 0xFF00B0;
        if (controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE)
        {
            phi_s1 = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                phi_s1 = -1;
            }
        }

        textMeasure(&sp50, &sp54, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE))
        {
            gdl = textRenderOutlined(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, 0x7000A0, sp54 + 1, sp50, 0, 0);
        } else
        {
            gdl = textRender(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, sp54, sp50, 0, 0);
        }

        gdl = draw_controller_style_text(gdl);
        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_33_CONTROLLER_LF)); //controller;

        phi_s1 = 0xFF00B0;

        if (controllerCheckDualControllerTypesAllowed())
        {
            textptr = langGet(getStringID(LOPTIONS, OPTION_STR_34_CONTROLLERS_LF)); //controllers;
        }
#ifndef TARGET_N64
        /* The raphnet page's third row is now a button that opens
         * PORTWATCH_CTLGENERAL (the two general raphnet options) via
         * draw_watch_current_page. The controller picture belongs to the page
         * and is unchanged. */
        textptr = (u8 *) "general control options\n";
#endif
        sp5C = XOFFSET_1;
#ifndef TARGET_N64
        sp58 = PORTWATCH_CTLROW_Y; /* one row down: the port's "input" row has 0x2B now */
#else
        sp58 = 0x2B;
#endif

        if (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS)
        {
            phi_s1 = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                phi_s1 = -1;
            }
        }

        textMeasure(&sp50, &sp54, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS))
        {
            gdl = textRenderOutlined(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, 0x7000A0, sp54 + 1, sp50, 0, 0);
        } else
        {
            gdl = textRender(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, sp54, sp50, 0, 0);
        }

    }
    return gdl;
}


void game_option_select_value(u32 *param_1, u32 param_2)
{
    *param_1 = param_2;
    set_controlstick_lr_disabled();
    sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
#ifndef TARGET_N64
    portPlayerOptsAdoptIf(param_1);
#endif
}


void game_option_toggle_input(s32 option_index)
{
    if ( (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD) || sub_GAME_7F0A4FB0()) && watch_item_is_actively_selected )
    {
        if (game_options_entries[option_index].current_value == 1)
        {
            game_option_select_value(&game_options_entries[option_index].current_value, 0);
        }
        else if (game_options_entries[option_index].current_value == 2)
        {
            game_option_select_value(&game_options_entries[option_index].current_value, 1);
        }
    }
    else
    {
        if ( (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD) || sub_GAME_7F0A4FEC()) && watch_item_is_actively_selected )
        {
            if (game_options_entries[option_index].current_value == 0)
            {
                game_option_select_value(&game_options_entries[option_index].current_value, 1);
            }
            else if ( (game_options_entries[option_index].current_value == 1) && game_options_entries[option_index].text[3] )
            {
                game_option_select_value(&game_options_entries[option_index].current_value, 2);
            }
        }
    }
}


/**
 * Address: 7F0AB908
 *
 * Set the color and draw the text for the values of the toggle options.
 * For example, draw the "ON" and "OFF" text for the Auto-Aim option,
 * but not the "AUTO-AIM" text itself.
 *
 * Options are highlighted by using the controller to advance up and down the toggle options list,
 * but options are not selected until the A button is pressed.
 */
Gfx *draw_toggle_option_values(Gfx *gdl, s32 y, s32 option_index, u32 state)
{
    s32 colour1;
    s32 colour2;
    s32 colour3;
    s32 x1;
    s32 x2;
    struct game_options *entry;
    struct game_options *drawentry;

    colour1 = 0x00800080;
    colour2 = 0x00800080;
    colour3 = 0x00800080;

    entry = &game_options_entries[option_index];

    if (j_text_trigger)
    {
        x1 = 0xAA;
        if (1);
    }
    else
    {
        x1 = 0xB4;
    }

    if (j_text_trigger)
    {
        x2 = 0xDC;
    }
    else
    {
        x2 = 0xE1;
    }

    // Option is unhighlighted
    if (state == 0)
    {
        goto state_unhighlighted;
    }
    // Option is highlighted
    else if (state == 1)
    {
        goto state_highlighted;
    }
    // Option is selected
    else if (state == 2)
    {
        goto state_selected;
    }
    goto after_state;

state_unhighlighted:
    entry = &game_options_entries[option_index];
    if (entry->current_value == 0)
    {
        colour1 = 0x00FF00B0;
    }
    else if (entry->current_value == 1)
    {
        colour2 = 0x00FF00B0;
    }
    else if (entry->current_value == 2)
    {
        colour3 = 0x00FF00B0;
    }
    goto after_state;

/**
 * Sets color of the active value of the highlighted option.
 * These use the same colors as the active values of the unhighlighted options,
 * so changing the highlighted option has no visual effect.
 */
state_highlighted:
    entry = &game_options_entries[option_index];
    if (entry->current_value == 0)
    {
        colour1 = 0x00FF00B0;
    }
    else if (entry->current_value == 1)
    {
        colour2 = 0x00FF00B0;
    }
    else if (entry->current_value == 2)
    {
        colour3 = 0x00FF00B0;
    }
    goto after_state;

/**
 * Make the active value of the selected option extra bright.
 */
state_selected:
    game_option_toggle_input(option_index);
    entry = &game_options_entries[option_index];
    if (entry->current_value == 0)
    {
        colour1 = 0xA0FFA0F0;
    }
    else if (entry->current_value == 1)
    {
        colour2 = 0xA0FFA0F0;
    }
    else if (entry->current_value == 2)
    {
        colour3 = 0xA0FFA0F0;
    }

after_state:
    if (entry->text[3] == 0)
    {
        if (j_text_trigger)
        {
            x1 = 0xBE;
        }
        else
        {
            x1 = 0xC8;
        }

        if (j_text_trigger)
        {
            // This weird code must be kept on one line for matching.
            x2 = 0xFA; } else { x2 = 0xFA; }
        }

        drawentry = entry;

        gdl = draw_options_labels(gdl, x1, y, langGet(drawentry->text[1]), colour1, 0, -1, 1, 0, 0x3000B0, 0); // Draw text of option's first value e.g. "Full" for the Screen option.
        gdl = draw_options_labels(gdl, x2, y, langGet(drawentry->text[2]), colour2, 0, -1, 1, 0, 0x3000B0, 0); // Draw text of option's second value e.g. "Wide" for the Screen option.

        if (drawentry->text[3])
        {
            gdl = draw_options_labels(gdl, 0x10E, y, langGet(drawentry->text[3]), colour3, 0, -1, 1, 0, 0x3000B0, 0); // Draw text of option's third value e.g. "Cinema" for the Screen option.
        }

    return gdl;
}


#ifndef TARGET_N64
#include <stdio.h>   /* the GE_WATCHNAVDBG line; GE_VIDEO builds already have it */
/*
 * PORT WATCH PAGES
 *
 * The Game Options tab used to be two volume sliders and Rare's eight
 * per-player toggles. It is now two volume sliders and eight port rows, two of
 * which open sub-pages (Display, Gameplay) that reuse the same grid.
 * port/src/port_watchpages.c defines the rows; this block draws them and feeds
 * them input (see docs/port/menu-overhaul-spec.md).
 *
 * Everything follows the vanilla eight-row grid (YOFFSET_1 + YINC*i, i < 8) and
 * colour scheme, so the page still looks like GoldenEye. The one new idea is
 * that a row with more than three values draws one value between < > arrows
 * instead of laying every value across the line.
 */

/* which sub-page of the Game Options tab is showing */
s32 g_portWatchPage = PORTWATCH_GENERAL;

#define PORTWATCH_COL_VALUE_OFF 0x00800080
#define PORTWATCH_COL_VALUE_ON  0x00FF00B0
#define PORTWATCH_COL_SELECTED  0xA0FFA0F0
#define PORTWATCH_COL_LABEL     0xFF00B0
#define PORTWATCH_COL_OUTLINE   0x7000A0
#define PORTWATCH_COL_BG        0x3000B0
/* the button plate: the same dark-green family as the vanilla label plate,
 * twice as strong, so "display options" reads as something you press */
#define PORTWATCH_COL_BUTTON    0x6000C0
/* the drop-down panel: 0xRRGGBBAA like every colour here (r is 0 - this face
 * is green). Dark and nearly opaque so the rows it covers do not read through
 * it, with a brighter edge so it looks like something that OPENED rather than
 * text floating on the face. */
#define PORTWATCH_COL_DROP_BG   0x00200AF0
#define PORTWATCH_COL_DROP_EDGE 0x009030F0
/* the band behind the value currently IN USE */
#define PORTWATCH_COL_DROP_LIVE 0x00501AF0
/* the more-above / more-below arrows */
#define PORTWATCH_COL_DROP_ARROW 0x00FF40FF

s32 portWatchRowsOnPage(void)
{
    return portWatchPageRows(g_portWatchPage);
}

/* Scrolling for port pages that don't fit the grid (mainly the Controller
 * page): the grid shows a PORTWATCH_VISROWS window that follows the highlight.
 * Reset on every page change, so a page always opens at its top. */
/* the scroll marks: WHITE, because the watch face behind them is dark */
#define PORTWATCH_SCROLLMARK_COL 0xFFFFFFFF
#define PORTWATCH_RULE_COL       0x00FF0070   /* the Controller page's rule: the label green, faint */
static s32 sPortWatchScroll;
/* the index the window last centred on, so the follow can tell a real
 * selection move from the same selection sitting still */
static s32 sPortWatchFollowIndex = -1;
/* Set when a tab or page is (re)entered, so the next rows-draw re-seeds the
 * clunk tracker instead of comparing against the previous visit. Sim thread
 * only (watch tick + DL build). */
static s32 sPortWatchHiliteReseed;
static s32 sPortWatchHoldUnbound;   /* the bind-row hold unbound: a held B's
                                     * release does not back out */

/** Where the first row is drawn and how many fit: General keeps Rare's YOFFSET_1
 * under the volume bars, other pages start at YOFFSET_8 and fit eleven rows.
 * Eleven rows put the last at y 187, just above the tab strip (~207), so
 * measure before raising it. */
s32 portWatchRowY0(void)
{
    return (g_portWatchPage == PORTWATCH_GENERAL) ? YOFFSET_1 : YOFFSET_8;
}

s32 portWatchRowSlots(void)
{
    return (g_portWatchPage == PORTWATCH_GENERAL) ? PORTWATCH_GENERAL_ROWS : PORTWATCH_VISROWS;
}

s32 portWatchVisibleRows(void)
{
    const s32 rows = portWatchRowsOnPage();
    const s32 slots = portWatchRowSlots();

    return (rows > slots) ? slots : rows;
}

s32 portWatchScrollOffset(void)
{
    return sPortWatchScroll;
}

/** The mouse wheel on a page longer than its window scrolls the view, and pulls
 * the highlight onto the nearest visible row when the view leaves it. That
 * keeps them in step so A never acts on a hidden row. */
s32 portWatchWheelScroll(s32 notches)
{
    const s32 rows = portWatchRowsOnPage();
    const s32 vis = portWatchVisibleRows();
    s32 max = rows - vis;

    if (notches == 0 || max <= 0)
    {
        return 0;   /* nothing hidden: leave the wheel to whatever else wants it */
    }
    sPortWatchScroll -= notches;   /* wheel up = earlier rows */
    if (sPortWatchScroll > max)
    {
        sPortWatchScroll = max;
    }
    if (sPortWatchScroll < 0)
    {
        sPortWatchScroll = 0;
    }
    {   /* the highlight comes along onto the nearest visible row, so A,
         * Enter and Delete never act on a row scrolled out of sight. Marked as
         * followed, or portWatchScrollFollow would snap the view back to it. */
        const s32 row = (s32) game_options_index - PORTWATCH_ROW_BASE;

        if (row >= 0 && (row < sPortWatchScroll || row >= sPortWatchScroll + vis))
        {
            game_options_index = PORTWATCH_ROW_BASE + ((row < sPortWatchScroll) ? sPortWatchScroll : sPortWatchScroll + vis - 1);
            sPortWatchFollowIndex = (s32) game_options_index;
        }
    }
    return 1;
}

/** Keep the highlighted row inside the window, and the window inside the page. */
void portWatchScrollFollow(void)
{
    const s32 rows = portWatchRowsOnPage();
    const s32 vis = portWatchVisibleRows();
    const s32 row = (s32) game_options_index - PORTWATCH_ROW_BASE;
    s32 max = rows - vis;

    if (max < 0)
    {
        max = 0;
    }
    /* Only re-centre when the selection actually moved, since this runs every
     * frame and would otherwise undo every wheel scroll. Keyboard and pad
     * navigation change the index, so they still pull the window along. */
    if (row >= 0 && (s32) game_options_index != sPortWatchFollowIndex)
    {
        /* Keep two rows of margin either side of the highlight (like
         * PORTWATCH_DROP_MARGIN), so on long pages the view scrolls before the
         * highlight reaches the edge. Not for a pointer-placed highlight, which
         * would make the page scroll under the mouse in a loop. */
        const s32 margin = sPortWatchIndexByMouse ? 0 : (vis > 4) ? 2 : (vis - 1) / 2;

        sPortWatchFollowIndex = (s32) game_options_index;
        if (row - margin < sPortWatchScroll)
        {
            sPortWatchScroll = row - margin;
        }
        else if (row + margin >= sPortWatchScroll + vis)
        {
            sPortWatchScroll = row + margin - vis + 1;
        }
    }
    sPortWatchIndexByMouse = 0;
    if (sPortWatchScroll > max)
    {
        sPortWatchScroll = max;
    }
    if (sPortWatchScroll < 0)
    {
        sPortWatchScroll = 0;
    }
}

/** Highest game_options_index on this page, exclusive. */
s32 portWatchIndexLimit(void)
{
    return PORTWATCH_ROW_BASE + portWatchRowsOnPage();
}

/** Lowest focusable index: the sliders exist on General only. */
s32 portWatchIndexFirst(void)
{
    return (g_portWatchPage == PORTWATCH_GENERAL) ? GAME_OPTIONS_INDEX_MUSIC : PORTWATCH_ROW_BASE;
}

/** The row the highlight is on, or -1 when it is on a volume slider. */
s32 portWatchCurrentRow(void)
{
    s32 row = (s32) game_options_index - PORTWATCH_ROW_BASE;

    return (row >= 0 && row < portWatchRowsOnPage()) ? row : -1;
}

/**
 * Go to a page. The highlight lands on the first row, never on a slider that
 * the sub-page does not draw, and nothing stays "selected" across the move.
 */
/* Back remembers where you were: a page's highlight and scroll are saved when
 * entering a sub-page and restored when backing out. Going deeper starts at the
 * top; closing the watch or leaving the tab forgets (-1 = none). */
static s32 sPortWatchPageRow[PORTWATCH_PAGES];
static s32 sPortWatchPageScroll[PORTWATCH_PAGES];
static s32 sPortWatchPageRowInit;

static void portWatchPageMemoryClear(void)
{
    s32 i;

    for (i = 0; i < PORTWATCH_PAGES; i++)
    {
        sPortWatchPageRow[i] = -1;
        sPortWatchPageScroll[i] = 0;
    }
    sPortWatchPageRowInit = 1;
}

void portWatchGotoSubPage(s32 page)
{
    const s32 from = g_portWatchPage;
    s32 back;

    if (!sPortWatchPageRowInit)
    {
        portWatchPageMemoryClear();
    }
    portInputAssignCancel();
    portWatchResCancel();
    portWatchDeferCancel();
    portWatchDropCloseNow();
    portWatchBindCaptureCancel();
    (void) portVideoDeleteEdge(); /* a Delete pressed before this page showed is not for its rows */
    if (page < 0 || page >= PORTWATCH_PAGES)
    {
        page = PORTWATCH_GENERAL;
    }
    back = (from >= 0 && from < PORTWATCH_PAGES && from != page && portWatchParentPage(from) == page);
    if (from >= 0 && from < PORTWATCH_PAGES && !back)
    {   /* going deeper: keep this page's place for the way back */
        sPortWatchPageRow[from] = (s32) game_options_index;
        sPortWatchPageScroll[from] = sPortWatchScroll;
    }
    g_portWatchPage = page;
    game_options_index = PORTWATCH_ROW_BASE;
    sPortWatchScroll = 0;
    reset_watch_item_is_actively_selected();
    portWatchPageEntered(page);   /* VIDEO: classic mode's forced rows, if they drifted */
    if (back && sPortWatchPageRow[page] >= PORTWATCH_ROW_BASE)
    {   /* backing out: the row it was left from, if the page still has it */
        const s32 r = sPortWatchPageRow[page] - PORTWATCH_ROW_BASE;

        if (r < portWatchRowsOnPage())
        {
            game_options_index = sPortWatchPageRow[page];
            sPortWatchScroll = sPortWatchPageScroll[page];
        }
        sPortWatchPageRow[page] = -1;
    }
    sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
}

/**
 * Called whenever the watch leaves this tab (or closes): a sub-page must
 * never be what greets the player next time, and the highlight must be
 * somewhere the page actually draws.
 */
void portWatchResetPage(void)
{
    portInputAssignCancel();
    portWatchResCancel();
    portWatchDeferCancel();
    portWatchDropCloseNow();
    portWatchBindCaptureCancel();
    (void) portVideoDeleteEdge(); /* a Delete pressed before this page showed is not for its rows */
    g_portWatchPage = PORTWATCH_GENERAL;
    sPortWatchScroll = 0;
    sPortWatchHiliteReseed = 1;
    portWatchPageMemoryClear();   /* a new visit starts at the top of every page */
}

/** A / Z on a button row acts instead of toggling "selected". 1 = handled. */
/** A back press (B, the Back/watch_objectives bind, or Escape) while a drop-down
 * or key capture is open; returns 1 if consumed so the caller (e.g. bondview2.c
 * closing the watch) ignores it. Nothing is applied: the list only writes via
 * portWatchDropChoose, so the old value stays live. */
s32 portWatchModalBack(void)
{
    if (portWatchDropIsOpen())
    {
        portWatchDropClose();
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
        return 1;
    }
    if (portWatchBindCaptureActive())
    {
        portWatchBindCaptureCancel();
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
        return 1;
    }
    if (portInputAssignActive())
    {   /* "press a button" is modal the same way: Escape cancelled it in
         * input_pc.c AND closed the whole watch */
        portInputAssignCancel();
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
        return 1;
    }
    return 0;
}

s32 portWatchActivateRow(void)
{
    s32 row = portWatchCurrentRow();
    s32 kind;

    if (row < 0)
    {
        return 0;
    }
    kind = portWatchRowKind(g_portWatchPage, row);
    if (kind == PWROW_DEVICE)
    {
        portWatchAssignBegin();   /* "press a button" - see portWatchAssignTick */
        return 1;
    }
    if (kind == PWROW_BIND)
    {
        /* Arm the key capture. Returns 0 for a PAD bind, which this
         * cannot capture yet - the vanilla Z|A toggle then runs and the row
         * behaves as it did before, rather than arming something that will
         * never fire. */
        if (portWatchBindCaptureActive())
        {
            portWatchBindCaptureCancel();   /* a second press backs out */
            sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);   /* a cancel sounds like one */
            return 1;
        }
        if (portWatchBindCaptureBegin(g_portWatchPage, row))
        {
            sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
            return 1;
        }
        return 0;
    }
    if (portWatchDropIsOpen())
    {
        /* the list is the edit mode: A / Z on it is the CONFIRM. Returns 1 so
         * the vanilla Z|A toggle never runs - the row must not also drop into
         * the old arrows mode underneath. */
        portWatchDropChoose();
        reset_watch_item_is_actively_selected();
        sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
        return 1;
    }
    if (portWatchRowLocked(g_portWatchPage, row))
    {
        return 1;   /* greyed by Classic Mode - A is swallowed silently, no beep and no edit mode */
    }
    if (portWatchRowDropdown(g_portWatchPage, row))
    {
        /* Opening the list plays the beep, like pressing A on vsync.
         * watch_play_beep_sound is skipped for this row because we return 1, so
         * play it here. */
        portWatchDropOpen(g_portWatchPage, row);
        portWatchDropAnchor(row);
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
        return 1;
    }
    if (portWatchRowDeferred(g_portWatchPage, row))
    {
        /* exactly the resolution row's contract, for an ordinary ini row
         * (display mode). Returns 0 so the vanilla Z|A toggle still runs and
         * the row comes in and out of edit mode with it. */
        if (watch_item_is_actively_selected)
        {
            portWatchDeferCommit();
        }
        else
        {
            portWatchDeferBegin(g_portWatchPage, row);
        }
        return 0;
    }
    if (kind == PWROW_RESOLUTION)
    {
        /* A / Z is the Accept. Entering the row seeds the candidate, leaving
         * it applies - and this deliberately returns 0 so the vanilla toggle
         * still runs and the row comes out of edit mode. */
        if (watch_item_is_actively_selected)
        {
            portWatchResCommit();
        }
        else
        {
            portWatchResBegin();
        }
        return 0;
    }
    if (kind == PWROW_SUBSCREEN)
    {
        portWatchGotoSubPage(portWatchRowSubPage(g_portWatchPage, row));
        return 1;
    }
    if (kind == PWROW_ACTION)
    {   /* Reset to Default: arm, then do (port_watchpages.c) */
        portWatchRowAction(g_portWatchPage, row);
        sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
        return 1;
    }
    if (kind == PWROW_BACK)
    {
        portWatchGotoSubPage(portWatchParentPage(g_portWatchPage));
        return 1;
    }
    return 0;
}

/** Step the highlighted row, with the vanilla click and stick-latch. */
void portWatchStepRow(s32 row, s32 dir)
{
    if (row < 0 || dir == 0)
    {
        return;
    }
    if (portWatchRowLocked(g_portWatchPage, row))
    {
        return;   /* the step is refused, so no click */
    }
    portWatchRowStep(g_portWatchPage, row, dir);
    set_controlstick_lr_disabled();
    sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
}

/**
 * Left / right on a selected row. Mirrors game_option_toggle_input exactly -
 * same buttons, same stick edge predicates, same "only while selected" gate -
 * so a port row and a Rare row feel identical.
 */
static void portWatchRowInput(s32 row)
{
    s32 dir = 0;

    if (!watch_item_is_actively_selected)
    {
        return;
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS | L_TRIG | L_JPAD) || sub_GAME_7F0A4FB0())
    {
        dir = -1;
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS | R_TRIG | R_JPAD) || sub_GAME_7F0A4FEC())
    {
        dir = 1;
    }
    portWatchStepRow(row, dir);
}

/** One value of a row, centred on x (a vanilla column). Not draw_options_labels,
 * whose clip box is too tight and drops descender glyphs like Q entirely, which
 * matters for the bind list's key names. */
static Gfx *portWatchDrawValueAt(Gfx *gdl, s32 x, s32 y, const char *text, u32 colour)
{
    s32 h = 0;
    s32 w = 0;
    s32 tx;
    s32 ty = y;

    if (text == NULL || text[0] == '\0')
    {
        return gdl;
    }
    textMeasure(&h, &w, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic, 10);
    tx = x - w / 2;

    if (g_WatchBackgroundGreen < 0xe0)
    {
        /* the watch's fuzzy-static effect, as draw_options_labels does it */
        gDPSetRenderMode(gdl++, G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2);
    }
    else
    {
        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    }
    return textRender(gdl, &tx, &ty, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic,
                      colour, w, h + 8, 0, 10);
}

static s32 portWatchTextWidth(const char *text)
{
    s32 h = 0;
    s32 w = 0;

    textMeasure(&h, &w, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic, 10);
    return w;
}

/* the face's usable width from XOFFSET_1 ("press a button" on the Input line ends near x 272) */
#define PORTWATCH_HINT_MAXW 204

/** text as is when it fits in maxw, else cut at a character with "..." on (a static copy) */
static char *portWatchFitText(const char *text, s32 maxw)
{
    static char out[64];
    s32 n;

    if (portWatchTextWidth(text) <= maxw)
    {
        return (char *) text;
    }
    for (n = (s32) strlen(text); n > 0; n--)
    {
        if (text[n - 1] == '\n')
        {
            continue;
        }
        snprintf(out, sizeof(out), "%.*s...\n", (int) ((n < (s32) sizeof(out) - 5) ? n : (s32) sizeof(out) - 5), text);
        if (portWatchTextWidth(out) <= maxw)
        {
            return out;
        }
    }
    return (char *) text;
}

/** Checks whether laying every value across the line fits; Rare sized the
 * columns for short words, and port values like "nintendo 64 2x" overlapped.
 * Measures rather than relying on short words, falling back to one value with
 * arrows. */
/** Lays out a row's value columns, returning how many were placed (0 = use one
 * value with arrows) and their centres in x[]; columns slide right for long
 * labels, and two-value rows may close up. The drawer and mouse hit test both
 * call this, so they always agree. */
static s32 portWatchValueColumns(s32 page, s32 row, s32 count, s32 *x)
{
    const s32 rightedge = PORT_WATCH_ROW_RIGHT;
    const s32 mingap = (count == 2) ? 6 : 4;
    const char *label;
    s32 leftedge;
    s32 w[3];
    s32 shift;
    s32 i;

    if (count < 2 || count > 3)
    {
        return 0;
    }
    label = portWatchRowLabel(page, row);
    if (label == NULL)
    {
        s32 playeropt = portWatchRowPlayerOption(page, row);
        label = (playeropt >= 0) ? (const char *) langGet(game_options_entries[playeropt].text[0]) : "";
    }
    leftedge = XOFFSET_1 + portWatchTextWidth(label) + 8;

    x[0] = (count == 3) ? PORTWATCH_VAL3_X1 : PORTWATCH_VAL2_X1;
    x[1] = (count == 3) ? PORTWATCH_VAL3_X2 : PORTWATCH_VAL2_X2;
    x[2] = PORTWATCH_VAL3_X3;
    for (i = 0; i < count; i++)
    {
        w[i] = portWatchTextWidth(portWatchRowValueTextAt(page, row, i));
    }

    /* slide the whole group right until the label is clear... */
    shift = leftedge - (x[0] - w[0] / 2);
    if (shift < 0)
    {
        shift = 0;
    }
    /* ...but never past the right edge; give back whatever does not fit */
    {
        s32 over = (x[count - 1] + shift + w[count - 1] / 2) - rightedge;
        if (over > 0)
        {
            shift -= over;
            if (shift < 0)
            {
                shift = 0;
            }
        }
    }
    for (i = 0; i < count; i++)
    {
        x[i] += shift;
    }

    /* still overlapping the label? a two-value row may close its own gap
     * before giving up, since both words are short */
    if (x[0] - w[0] / 2 < leftedge && count == 2)
    {
        s32 room = (x[1] - w[1] / 2) - (x[0] + w[0] / 2) - mingap;
        s32 pull = leftedge - (x[0] - w[0] / 2);

        if (pull > room)
        {
            pull = room;
        }
        if (pull > 0)
        {
            x[0] += pull;
        }
    }

    if (x[0] - w[0] / 2 < leftedge || x[count - 1] + w[count - 1] / 2 > rightedge)
    {
        return 0;
    }
    for (i = 1; i < count; i++)
    {
        if (x[i - 1] + w[i - 1] / 2 + mingap > x[i] - w[i] / 2)
        {
            return 0;
        }
    }
    return count;
}

static s32 portWatchValuesFit(s32 page, s32 row, s32 count)
{
    s32 x[3];

    return portWatchValueColumns(page, row, count, x) > 0;
}

/** A row opens a drop-down list instead of < > arrows exactly when its values
 * don't all fit across the line (portWatchValuesFit). To widen it to any row
 * with more than two values, drop that call. */
s32 portWatchRowDropdown(s32 page, s32 row)
{
    const s32 kind = portWatchRowKind(page, row);
    const s32 count = portWatchRowValueCount(page, row);

    if (count < 3)
    {
        return 0;
    }
    if (kind == PWROW_RESOLUTION || kind == PWROW_XHAIR || kind == PWROW_LAYOUT)
    {
        return 1;   /* 25 modes / 9 colours / long layout names: the list is the sane way to pick one */
    }
    if (kind != PWROW_OPT)
    {
        return 0;
    }
    return !portWatchValuesFit(page, row, count);
}

/* Drop-down geometry: the list hangs off its row, left edge under the first
 * value column, clamped to the face so a low row opens upward. portWatchDropBox
 * is the single source the input also reads. */
#define PORTWATCH_DROP_VIS   7      /* max visible entries: 7 * YINC = 105 px, inside the face */
#define PORTWATCH_DROP_PAD   4
/* The box stays between the topmost and bottommost option slots of the full
 * grid (portWatchRowSlots, not portWatchVisibleRows), so a long list can use
 * the whole face even on a short page. Derived from the row grid so it can't
 * drift from the layout. */
#define PORTWATCH_DROP_TOP   (portWatchRowY0())
#define PORTWATCH_DROP_BOT   (portWatchRowY0() + YINC * portWatchRowSlots())
/* the glass is an oval, so a panel may not use the full text width that a
 * single LINE of text can - it is tall and would run onto the bezel */
#define PORTWATCH_DROP_RIGHT 288

/** Parks the list's window at open so the entry in use lands on the row's own
 * line, scrolling (with "more above" arrows) when clamping the panel to the
 * face makes that impossible. Called once at open, like Perfect Dark's
 * menuitemDropdownInit; normal follow-the-highlight scrolling takes over after. */
static s32 portWatchDropVisible(void);

static void portWatchDropAnchor(s32 row)
{
    const s32 rowy = portWatchRowY0() + YINC * (row - portWatchScrollOffset());
    const s32 n = portWatchDropVisible();
    const s32 count = portWatchDropCount();
    const s32 livei = portWatchDropLiveIndex();
    const s32 live = (livei >= 0) ? livei : portWatchDropIndex();   /* off-grid: the nearest entry the open seeded */
    s32 top = rowy - YINC * live;
    s32 slot;

    if (n <= 0)
    {
        return;
    }
    if (top + n * YINC > PORTWATCH_DROP_BOT)
    {
        top = PORTWATCH_DROP_BOT - n * YINC;
    }
    if (top < PORTWATCH_DROP_TOP)
    {
        top = PORTWATCH_DROP_TOP;
    }
    slot = (rowy - top) / YINC;
    if (slot < 0) slot = 0;
    if (slot > n - 1) slot = n - 1;
    portWatchDropSetScroll((live - slot > count - n) ? count - n : live - slot);
    portWatchDropSetTop(top);   /* and it does not move again while it is open */
    /* settle the window through the SAME margin rule the navigation uses, so
     * the list does not jump a row the first time the highlight moves. On a
     * list short enough not to scroll this does nothing, which is why the
     * live entry still lands exactly on its row line there. */
    portWatchDropScrollFollow(n);
}

/** How many entries this list shows at once. */
static s32 portWatchDropVisible(void)
{
    const s32 n = portWatchDropCount();

    return (n < PORTWATCH_DROP_VIS) ? n : PORTWATCH_DROP_VIS;
}

/**
 * Where the open list sits: x0/y0 of its first entry, and its width.
 * `rowy` is the screen y of the row the list belongs to.
 */
static void portWatchDropBox(s32 rowy, s32 *x0, s32 *y0, s32 *w, s32 *vis)
{
    const s32 n = portWatchDropVisible();
    const s32 count = portWatchDropCount();
    s32 wid = 0;
    s32 i;
    s32 top;

    for (i = 0; i < count; i++)
    {
        const s32 tw = portWatchTextWidth(portWatchDropTextAt(i));
        if (tw > wid)
        {
            wid = tw;
        }
    }
    wid += PORTWATCH_DROP_PAD * 2;
    if (count > n)
    {
        wid += 8;   /* a gutter for the more-above / more-below arrows, so they
                     * never sit on top of a value (they did at first) */
    }

    /* The panel's top is fixed once by portWatchDropAnchor when the list opens,
     * so the box doesn't slide around as the list scrolls. `rowy` is only a
     * fallback for a list that was never anchored. */
    top = portWatchDropTop();
    if (top <= 0)
    {
        top = rowy;
    }
    if (top + n * YINC > PORTWATCH_DROP_BOT)
    {
        top = PORTWATCH_DROP_BOT - n * YINC;
    }
    if (top < PORTWATCH_DROP_TOP)
    {
        top = PORTWATCH_DROP_TOP;
    }

    /* Centred on the value column, where the single value it replaces was
     * drawn. Not right-aligned to PORT_WATCH_ROW_RIGHT, because the watch glass
     * is an oval and that would put the panel over the bezel. */
    *x0 = PORTWATCH_VAL3_X2 - wid / 2;
    if (*x0 + wid > PORTWATCH_DROP_RIGHT)
    {
        *x0 = PORTWATCH_DROP_RIGHT - wid;
    }
    if (*x0 < XOFFSET_1)
    {
        *x0 = XOFFSET_1;   /* a list wider than the face starts at the label */
    }
    *y0 = top;
    *w = wid;
    *vis = n;
}

/** The mouse layer draws no text, so it asks rather than measuring. */
s32 portWatchValuesFitPublic(s32 page, s32 row, s32 count)
{
    return portWatchValuesFit(page, row, count);
}

/**
 * Which value column a click at `mx` landed on, or -1 if this row is not
 * drawn as columns. Splits on the MIDPOINTS of the columns the drawer
 * actually used, so a row whose columns slid right is still clickable.
 */
s32 portWatchValueColumnHit(s32 page, s32 row, s32 count, f32 mx)
{
    s32 col[3];
    s32 cols = portWatchValueColumns(page, row, count, col);
    s32 i;

    if (cols <= 0)
    {
        return -1;
    }
    for (i = 0; i < cols - 1; i++)
    {
        if (mx < (f32) ((col[i] + col[i + 1]) / 2))
        {
            return i;
        }
    }
    return cols - 1;
}

/** Where a row's value half starts, for the mouse: Rare's rows split at x 165,
 * while port rows split halfway into the gap before their (possibly slid-right)
 * columns. Otherwise a click on the end of a long label would hit a value
 * column instead of focusing the row. */
f32 portWatchValueSplitX(s32 page, s32 row)
{
    const char *label = portWatchRowLabel(page, row);
    s32 x;

    if (label == NULL)
    {
        return 165.0f;   /* PWROW_PLAYEROPT: Rare's fixed columns */
    }
    x = XOFFSET_1 + portWatchTextWidth(label) + 4;
    return (f32) ((x > 165) ? x : 165);
}

/** Draws the value half of a row (state 0 = plain, 1 = highlighted, 2 =
 * highlighted and selected). Rows with two or three values show every value
 * with the live one lit; wider or ranged rows show the live value alone between
 * arrows. */
static Gfx *portWatchDrawRowValue(Gfx *gdl, s32 y, s32 row, s32 state)
{
    const s32 page = g_portWatchPage;
    const s32 kind = portWatchRowKind(page, row);
    const s32 count = portWatchRowValueCount(page, row);
    const s32 live = portWatchRowValueIndex(page, row);
    const u32 activeColour = (state == 2) ? PORTWATCH_COL_SELECTED : PORTWATCH_COL_VALUE_ON;

    if (kind == PWROW_SUBSCREEN || PWROW_BACK == kind || kind == PWROW_ACTION)
    {
        return gdl; /* buttons carry no value; the label is the whole row */
    }

    {
        s32 col[3];
        s32 cols = portWatchValueColumns(page, row, count, col);
        s32 i;

        for (i = 0; i < cols; i++)
        {
            const u32 colour = (i == live) ? activeColour : PORTWATCH_COL_VALUE_OFF;

            gdl = portWatchDrawValueAt(gdl, col[i], y, portWatchRowValueTextAt(page, row, i), colour);
        }
        if (cols > 0)
        {
            return gdl;
        }
    }

    /* one value, centred on the middle column, with arrows hugging it while
     * the row is selected so it is obvious that left/right does something */
    {
        const char *text = (count >= 2 && live >= 0) ? portWatchRowValueTextAt(page, row, live)
                                                     : portWatchRowValueText(page, row);
        const s32 half = portWatchTextWidth(text) / 2;

        gdl = portWatchDrawValueAt(gdl, PORTWATCH_VAL3_X2, y, text, activeColour);
        if (state == 2)
        {
            /* NOT the dim value colour: it is the same green as the watch
             * face and the arrows simply vanished into it */
            /* TRAP: the trailing newline is load-bearing, because textMeasure
             * only counts height on '\n' and a zero-height clip box makes the
             * glyph vanish. The colour matters too, since the dim value green
             * matches the watch face and hid the arrows. */
            gdl = portWatchDrawValueAt(gdl, PORTWATCH_VAL3_X2 - half - 8, y, "<\n", PORTWATCH_COL_VALUE_ON);
            gdl = portWatchDrawValueAt(gdl, PORTWATCH_VAL3_X2 + half + 8, y, ">\n", PORTWATCH_COL_VALUE_ON);
        }
    }
    return gdl;
}

/**
 * The eight rows of the current page. Replaces the vanilla draw_toggle_options
 * body; a PWROW_PLAYEROPT row is handed straight back to Rare's own drawer,
 * which also carries that row's input, so auto-aim and sight-on-screen behave
 * exactly as they always did wherever they now live.
 */
/** Plays the clunk (OPTION_CHOOSE_SFX) when the highlight lands somewhere new,
 * tracked in one place so no movement path is missed or double-fired.
 * Deliberately silent on page changes and list open/close, which have their own
 * sounds. */
static void portWatchHighlightSound(void)
{
    static s32 lastIndex = -1;
    static s32 lastEntry = -1;
    static s32 lastOpen = -1;
    static s32 lastPage = -1;
    const s32 index = (s32) game_options_index;
    const s32 open = portWatchDropIsOpen() ? 1 : 0;
    const s32 entry = open ? portWatchDropIndex() : -1;
    const s32 page = g_portWatchPage;

    if (sPortWatchHiliteReseed)
    { /* first rows-draw since the tab/page was (re)entered: the statics hold
       * the last visit, not last frame. Seed silently. */
        sPortWatchHiliteReseed = 0;
        lastIndex = -1;
    }
    if (lastIndex >= 0 && lastPage == page && lastOpen == open
        && !check_watch_page_transistion_running()   /* a tab change has its own */
        && (index != lastIndex || entry != lastEntry))
    {
        /* Silent by request: no click when the highlight moves. The tracker
         * stays for its debug line. */
#ifdef GE_VIDEO /* portWatchDbg lives in the watch-mouse block */
        if (portWatchDbg())
        {
            fprintf(stderr, "ge: watchclunk: row %d->%d entry %d->%d\n",
                    (int) lastIndex, (int) index, (int) lastEntry, (int) entry);
        }
#endif
    }
    lastIndex = index;
    lastEntry = entry;
    lastOpen = open;
    lastPage = page;
}

Gfx *portWatchDrawRows(Gfx *gdl)
{
    portWatchHighlightSound();
    /* GE_WATCHPAGE debug harness: re-assert the forced page at draw time too,
     * because the "selected" flag doesn't survive from tick to draw in a
     * GE_NOINPUT run. One cached env lookup per frame. */
    sPortWatchHarnessDraw = 1;
    portWatchHarnessTick();
    sPortWatchHarnessDraw = 0;
#ifdef GE_VIDEO /* the slider helpers live in the watch-mouse block */
    if (portWatchDbg())
    { /* GE_WATCHMOUSEDBG: the volume bars' drag geometry, once. bbox = what
       * portWatchSliderBox's four projected corners span, track = the two ends
       * of the midline the fill edge actually travels along. */
        static s32 once;
        if (!once && g_portWatchPage == PORTWATCH_GENERAL)
        {
            s32 i;
            once = 1;
            for (i = 0; i < 2; i++)
            {
                f32 bl, bt, br, bb, tx0, tx1;
                portWatchSliderBox(i, &bl, &bt, &br, &bb);
                portWatchSliderTrack(i, &tx0, &tx1);
                fprintf(stderr, "ge: watchbar %d: bbox %.2f..%.2f (w %.2f)  track %.2f..%.2f (w %.2f)  left err %+.2f px  right err %+.2f px\n",
                        (int) i, bl, br, br - bl, tx0, tx1, tx1 - tx0, tx0 - bl, tx1 - br);
            }
            { /* where the drawn fade band lands against the crosshair for a
               * click at t along the bar. This is the gate on portWatchBarFill:
               * the band must STRADDLE the cursor, not sit right of it. */
                f32 tx0, tx1, ppu;
                s32 k;
                portWatchSliderTrack(0, &tx0, &tx1);
                ppu = (tx1 - tx0) / PORTWATCH_BAR_W;
                for (k = 1; k <= 3; k++)
                {
                    const f32 t = 0.25f * (f32) k;
                    const f32 cur = tx0 + t * (tx1 - tx0);   /* the crosshair */
                    const f32 f = portWatchBarFill(t);
                    const f32 tw = 30.0f * (1.2f - f);
                    const f32 cen = tx0 + ((PORTWATCH_BAR_W + tw) * f) * ppu;

                    fprintf(stderr, "ge: watchbar fill t=%.2f: f=%.4f band %.2f..%.2f (%.2f px left of the cursor, %.2f right) centre err %+.2f px\n",
                            (double) t, (double) f,
                            (double) (cen - tw * ppu), (double) (cen + tw * ppu),
                            (double) (cur - (cen - tw * ppu)), (double) ((cen + tw * ppu) - cur),
                            (double) (cen - cur));
                }
            }
        }
    }
#endif
    {
    const s32 page = g_portWatchPage;
    const s32 vis = portWatchVisibleRows();
    s32 v;
    s32 y = portWatchRowY0();

    portWatchScrollFollow();
    gdl = microcode_constructor(gdl);

    for (v = 0; v < vis; v++, y += YINC)
    {
        const s32 i = sPortWatchScroll + v;
        const s32 kind = portWatchRowKind(page, i);
        const s32 playeropt = portWatchRowPlayerOption(page, i);
        const char *label = portWatchRowLabel(page, i);
        const s32 selected = ((s32) game_options_index - PORTWATCH_ROW_BASE) == i;
        const s32 state = selected ? (watch_item_is_actively_selected ? 2 : 1) : 0;
        const s32 button = (kind == PWROW_SUBSCREEN || kind == PWROW_BACK || kind == PWROW_ACTION);
        char *text;

        if (kind == PWROW_PLAYEROPT)
        {
            /* Rare's row, Rare's drawer: label, values AND the left/right
             * handling (draw_toggle_option_values calls game_option_toggle_input
             * for the selected row). Do not reimplement it here. */
            if (state == 2)
            {
                gdl = draw_toggle_option_values(
                    draw_options_labels(gdl, XOFFSET_1, y, (char *) langGet(game_options_entries[playeropt].text[0]),
                                        -1, 1, PORTWATCH_COL_OUTLINE, 0, 0, PORTWATCH_COL_BG, 0),
                    y, playeropt, 2);
            }
            else
            {
                gdl = draw_toggle_option_values(
                    draw_options_labels(gdl, XOFFSET_1, y, (char *) langGet(game_options_entries[playeropt].text[0]),
                                        (state == 1) ? PORTWATCH_COL_SELECTED : PORTWATCH_COL_LABEL,
                                        0, -1, 0, 0, PORTWATCH_COL_BG, 0),
                    y, playeropt, state);
            }
            continue;
        }

        text = (char *) ((label != NULL) ? label : "?\n");
        if (kind == PWROW_HINT)
        {   /* a hint wider than the face is cut with "..." (the device name
             * under Input) */
            text = portWatchFitText(text, PORTWATCH_HINT_MAXW);
        }

        if (button)
        {
            /* Draws these rows as visible buttons (a filled plate behind the
             * label), left-aligned with every other row's label. */
            gdl = draw_options_labels(gdl, XOFFSET_1, y, text,
                                      (state == 0) ? PORTWATCH_COL_LABEL : PORTWATCH_COL_SELECTED,
                                      (state == 2), PORTWATCH_COL_OUTLINE, 0, 1, PORTWATCH_COL_BUTTON, 0);
            continue;
        }

        if (!portWatchRowAvailable(page, i))
        {
            /* no renderer linked for this key: show it, dimmed, rather than
             * leaving a hole in the grid */
            gdl = draw_options_labels(gdl, XOFFSET_1, y, text, PORTWATCH_COL_VALUE_OFF, 0, -1, 0, 0,
                                      PORTWATCH_COL_BG, 0);
            gdl = portWatchDrawValueAt(gdl, PORTWATCH_VAL3_X2, y, "n/a\n", PORTWATCH_COL_VALUE_OFF);
            continue;
        }

        if (portWatchRowLocked(page, i))
        {
            /* Classic mode owns this row: show its forced value dimmed and take
             * no input (port_watchpages.c refuses the step, pick and list), but
             * still highlight the label so the cursor isn't lost. A press must
             * not leave the row selected, since nothing here would ever release
             * it. */
            if (state == 2)
            {
                reset_watch_item_is_actively_selected();
            }
            gdl = draw_options_labels(gdl, XOFFSET_1, y, text,
                                      (state != 0) ? PORTWATCH_COL_SELECTED : PORTWATCH_COL_VALUE_OFF,
                                      0, -1, 0, 0, PORTWATCH_COL_BG, 0);
            gdl = portWatchDrawValueAt(gdl, PORTWATCH_VAL3_X2, y, (char *) portWatchRowValueText(page, i),
                                       PORTWATCH_COL_VALUE_OFF);
            continue;
        }

        if (state == 2)
        {
            gdl = draw_options_labels(gdl, XOFFSET_1, y, text, -1, 1, PORTWATCH_COL_OUTLINE, 0, 0,
                                      PORTWATCH_COL_BG, 0);
            portWatchRowInput(i); /* where the vanilla drawer takes its input too */
        }
        else
        {
            gdl = draw_options_labels(gdl, XOFFSET_1, y, text,
                                      (state == 1) ? PORTWATCH_COL_SELECTED : PORTWATCH_COL_LABEL,
                                      0, -1, 0, 0, PORTWATCH_COL_BG, 0);
        }

        gdl = portWatchDrawRowValue(gdl, y, i, state);
    }

    if (page == PORTWATCH_CONTROLLER)
    {   /* Draws a hairline under the input device-name line (the hint after the
         * Input row), from the labels to the face's edge, like the folder's
         * Control sheet has between players. */
        s32 v;

        for (v = 0; v < vis; v++)
        {
            const s32 i = sPortWatchScroll + v;

            if (i > 0 && portWatchRowKind(page, i) == PWROW_HINT && portWatchRowKind(page, i - 1) == PWROW_DEVICE)
            {
                const s32 ry = portWatchRowY0() + YINC * v + YINC - 3;

                gdl = microcode_constructor_related_to_menus(gdl, XOFFSET_1 - 2, ry,
                        XOFFSET_1 + PORTWATCH_HINT_MAXW, ry + 1, PORTWATCH_RULE_COL);
            }
        }
    }
    if (page == PORTWATCH_GENERAL)
    {   /* A RULE BETWEEN RARE'S ROWS AND THE PORT'S PICKERS (below Screen,
         * above Gameplay): above the first sub-page button
         * that follows a player-option row */
        s32 v;

        for (v = 0; v < vis; v++)
        {
            const s32 i = sPortWatchScroll + v;

            if (i > 0 && portWatchRowKind(page, i) == PWROW_SUBSCREEN && portWatchRowKind(page, i - 1) == PWROW_PLAYEROPT)
            {
                const s32 ry = portWatchRowY0() + YINC * v - 3;

                gdl = microcode_constructor_related_to_menus(gdl, XOFFSET_1 - 2, ry,
                        XOFFSET_1 + PORTWATCH_HINT_MAXW, ry + 1, PORTWATCH_RULE_COL);
            }
        }
    }

    {   /* More above / more below markers: three stacked bars in the gutter
         * left of the labels, the same mark the frontend sheets use on scrolled
         * drop-downs. Drawn only when rows are hidden, so a page that fits
         * looks unchanged. */
        const s32 rows = portWatchRowsOnPage();
        const s32 ax = XOFFSET_1 - 9;
        s32 k;

        /* A bar is drawn from ax+k to ax+6-k, so k = 0 is the wide end: an up
         * arrow needs its wide end last, a down arrow first. White, because the
         * watch face is dark. */
        if (sPortWatchScroll > 0)
        {   /* MORE ABOVE: an arrow pointing UP, narrow end at the top */
            const s32 ay = portWatchRowY0() - PORT_WATCH_ROW_ABOVE;

            for (k = 0; k < 3; k++)
            {
                gdl = microcode_constructor_related_to_menus(gdl, ax + 2 - k, ay + k,
                        ax + 4 + k, ay + k + 1, PORTWATCH_SCROLLMARK_COL);
            }
        }
        if (sPortWatchScroll + vis < rows)
        {   /* MORE BELOW: an arrow pointing DOWN, wide end at the top */
            const s32 ay = portWatchRowY0() + YINC * (vis - 1) + PORT_WATCH_ROW_BELOW;

            for (k = 0; k < 3; k++)
            {
                gdl = microcode_constructor_related_to_menus(gdl, ax + k, ay + k,
                        ax + 6 - k, ay + k + 1, PORTWATCH_SCROLLMARK_COL);
            }
        }
    }

    /* the open list goes LAST so it sits over the rows it covers */
    if (portWatchDropIsVisible() && portWatchDropPage() == page)
    {
        const s32 slot = portWatchDropRow() - portWatchScrollOffset();

        if (slot >= 0 && slot < vis)
        {
            const s32 rowy = portWatchRowY0() + YINC * slot;
            const s32 live = portWatchDropLiveIndex();
            s32 x0, y0, w, n, e;

            const f32 aw = portWatchDropAnimW();
            const f32 ah = portWatchDropAnimH();
            s32 bx0, by0, bx1, by1, cx, cy;

            /* No portWatchDropScrollFollow here: the window may only move when
             * the highlight moves, never from the draw. The draw also runs
             * during mouse hover, and chasing the hovered entry causes the
             * turbo-scroll loop. */
            portWatchDropBox(rowy, &x0, &y0, &w, &n);

            /* CRT switch-on animation over the panel's full extent: a
             * horizontal line grows from the centre (aw), then opens vertically
             * (ah). Closing runs the same values back down. */
            bx0 = x0 - 2;
            bx1 = x0 + w + 2;
            by0 = y0 - PORT_WATCH_ROW_ABOVE - 2;
            by1 = y0 + n * YINC + 1;
            cx = (bx0 + bx1) / 2;
            cy = (by0 + by1) / 2;
            {
                const s32 halfw = (s32) (((f32) (bx1 - bx0) * 0.5f) * aw);
                s32 halfh = (s32) (((f32) (by1 - by0) * 0.5f) * ah);

                if (halfh < 1)
                {
                    halfh = 1;   /* the scan line itself, before it opens */
                }
                bx0 = cx - halfw;
                bx1 = cx + halfw;
                by0 = cy - halfh;
                by1 = cy + halfh;
            }
            if (bx1 - bx0 < 2)
            {
                return gdl;   /* nothing of it on screen yet */
            }

            /* a border drawn as a slightly larger rectangle behind the panel:
             * without it the list reads as text floating on the face rather
             * than as something that opened on top of it */
            gdl = microcode_constructor_related_to_menus(gdl, bx0, by0, bx1, by1, PORTWATCH_COL_DROP_EDGE);
            if (bx1 - bx0 > 4 && by1 - by0 > 4)
            {
                gdl = microcode_constructor_related_to_menus(gdl, bx0 + 2, by0 + 2, bx1 - 2, by1 - 2,
                                                             PORTWATCH_COL_DROP_BG);
            }
            gdl = microcode_constructor(gdl);

            if (!portWatchDropAnimDone())
            {
                return gdl;   /* still opening or closing: the shell only */
            }

            /* The value in use gets a band behind it, drawn before the entries
             * so the text sits on top, because PORTWATCH_COL_VALUE_ON and
             * PORTWATCH_COL_LABEL are the same green. As in Perfect Dark, the
             * highlighted entry and the applied value are shown independently. */
            if (live >= portWatchDropScroll() && live < portWatchDropScroll() + n)
            {
                const s32 ly = y0 + YINC * (live - portWatchDropScroll());

                gdl = microcode_constructor_related_to_menus(gdl, x0 + 1, ly - PORT_WATCH_ROW_ABOVE + 1,
                                                             x0 + w - 1, ly + YINC - PORT_WATCH_ROW_ABOVE,
                                                             PORTWATCH_COL_DROP_LIVE);
                gdl = microcode_constructor(gdl);
            }

            for (e = 0; e < n; e++)
            {
                const s32 idx = portWatchDropScroll() + e;
                const s32 ey = y0 + YINC * e;
                const s32 hot = (idx == portWatchDropIndex());
                char *etext = (char *) portWatchDropTextAt(idx);

                if (hot)
                {
                    gdl = draw_options_labels(gdl, x0 + PORTWATCH_DROP_PAD, ey, etext, -1, 1,
                                              PORTWATCH_COL_OUTLINE, 0, 0, PORTWATCH_COL_BG, 0);
                }
                else
                {
                    gdl = draw_options_labels(gdl, x0 + PORTWATCH_DROP_PAD, ey, etext,
                                              PORTWATCH_COL_LABEL, 0, -1, 0, 0, PORTWATCH_COL_BG, 0);
                }
            }

            /* More above / more below arrows on the box, drawn as stacked
             * rectangles because the watch font has no up or down arrow glyph. */
            {
                const s32 ax = x0 + w - 7;
                s32 k;

                if (portWatchDropScroll() > 0)
                {
                    for (k = 0; k < 3; k++)
                    {
                        gdl = microcode_constructor_related_to_menus(gdl, ax + (2 - k), y0 - 1 + k,
                                                                     ax + 4 + k, y0 + k,
                                                                     PORTWATCH_COL_DROP_ARROW);
                    }
                }
                if (portWatchDropScroll() + n < portWatchDropCount())
                {
                    const s32 ay = y0 + n * YINC - 6;
                    for (k = 0; k < 3; k++)
                    {
                        gdl = microcode_constructor_related_to_menus(gdl, ax + k, ay + k,
                                                                     ax + 6 - k, ay + 1 + k,
                                                                     PORTWATCH_COL_DROP_ARROW);
                    }
                }
                gdl = microcode_constructor(gdl);
            }
        }
    }
    }

    return gdl;
}

/* The Control Options tab is the port page unless the device is a raphnet
 * adapter, which keeps the vanilla N64 page plus one port row (the device
 * picker, middle of the three). Without that row, choosing raphnet would be a
 * one-way door out of the port page. */
/* a third value for controller_options_index (the vanilla enum has two).
 * Kept here rather than in options.h: it is a port row, and the vanilla
 * enum is a decompiled contract. */
#define PORTWATCH_CTL_INDEX_DEVICE 2

void portWatchEnterControllerTab(void)
{
    /* every way INTO this tab starts clean - including the drawer's
     * derive-from-the-tab guard: anything still open belongs to a page the
     * player has left (what portWatchResetPage cancels, less the assign,
     * which cannot be armed on the way in) */
    portWatchResCancel();
    portWatchDeferCancel();
    portWatchDropCloseNow();
    portWatchBindCaptureCancel();
    { /* Open the tab on this player's own device: the pad in their slot, or
       * mouse and keyboard when there is none. Player 1 follows p1_view like
       * the folder's sheet (front.c portSheetSeedDevices), since keyboard and a
       * pad share slot 1, and this re-runs when an assign completes. */
        extern s32 portInputSlotDevice(s32 slot);
        extern s32 portInputP1ViewPad(void);
        const s32 pn = get_cur_playernum();
        const s32 dev = (pn == 0 && !portInputP1ViewPad()) ? 0 : portInputSlotDevice(pn);

        portWatchDeviceSet((dev > 0) ? dev : 0);
    }
    portWatchDeviceValidate();
    (void) portVideoDeleteEdge(); /* same: arriving at the device page starts with no stale Delete */
    g_portWatchPage = PORTWATCH_CONTROLLER;
    game_options_index = PORTWATCH_ROW_BASE;
    sPortWatchScroll = 0;
    sPortWatchHiliteReseed = 1;
    reset_watch_item_is_actively_selected();
}

/* The input row asks you to press a button on the device (input_pc.c
 * portInputAssign*, shared with the folder), pinning it to this player's slot
 * (padN= in midas.ini). While armed it owns the Control tab's tick like a bind
 * capture; it times out, and leaving the page cancels it. */
extern s32  portInputAssignBegin(s32 slot);
extern s32  portInputAssignActive(void);
extern s32  portInputAssignTick(void);
extern s32  portInputAssignTake(void);
extern void portInputAssignCancel(void);

void portWatchAssignBegin(void)
{
    portWatchDropCloseNow();
    portWatchBindCaptureCancel();
    reset_watch_item_is_actively_selected();
    if (portInputAssignBegin(get_cur_playernum()))
    {
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
    }
}

/* The press that answered must not also confirm: once input_pc.c stops blanking
 * pads, a still-held A would re-arm the assign forever. Held until A and Z are
 * both up, like the folder's port_sheet_swallow_press. */
static s32 sPortWatchAssignSwallow;

s32 portWatchAssignSwallowing(void)
{
    return sPortWatchAssignSwallow;
}

void portWatchAssignSwallowPoll(void)
{
    if (sPortWatchAssignSwallow && !portInputAssignActive() && !joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG))
    {
        sPortWatchAssignSwallow = 0;
    }
}

/** 1 = an armed assign owns this tick. */
s32 portWatchAssignTick(void)
{
    if (!portInputAssignActive())
    {
        if (sPortWatchAssignSwallow && !joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG))
        {
            sPortWatchAssignSwallow = 0;
        }
        return 0;
    }
    if (portInputAssignTick())
    {
        const s32 dev = portInputAssignTake();   /* -1 = timed out / Escape: nothing changed */

        sPortWatchAssignSwallow = 1;
        if (dev >= 0)
        {
            portWatchDeviceSet(dev);
            sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
        }
        /* the device may have swapped this whole page (raphnet <-> port), so
         * land on the top of whichever one it is now */
        portWatchDeviceValidate();
        g_portWatchPage = PORTWATCH_CONTROLLER;
        game_options_index = PORTWATCH_ROW_BASE;
        sPortWatchScroll = 0;
        reset_controller_options_index();
        reset_watch_item_is_actively_selected();
    }
    disable_watch_stick_y_nav_ready();
    return 1;
}

/** 1 = show the vanilla page (picture + control styles) instead of the port one. */
s32 portWatchRaphnetView(void)
{
    /* EXCEPT on general control options: that page is the
     * port's row grid for every device, the raphnet adapter included - its
     * third row opens it, and "back" returns here to the picture. */
    /* every Control-tab path asks this first, every tick: an unplug while the
     * tab is open must not leave the picker past the end of the device list
     * (tab entry was the only check - see portWatchDeviceStep's promise) */
    extern s32 portInputDeviceSlot(s32 dev);
    static s32 lastDev = -1;
    static s32 lastId = -100;   /* -100 = nothing seen yet (an id is >= -2) */
    const s32 pre = portWatchDevice();

    portWatchDeviceValidate();
    {   /* A hotplug under the tab can leave the picker's device index naming a
         * different pad, so the page, lists or pending edits would act on the
         * wrong device. When the device behind an unchanged index changes,
         * reset to the top of the tab as an assign does. */
        const s32 dev = portWatchDevice();
        const s32 id = portInputDeviceSlot(dev) * 2 + (portWatchDeviceIsRaphnet(dev) ? 1 : 0);

        if ((dev != pre || (dev == lastDev && id != lastId && lastId != -100)) && !portInputAssignActive())
        {
            portWatchDropCloseNow();
            portWatchDeferCancel();
            portWatchBindCaptureCancel();
            g_portWatchPage = PORTWATCH_CONTROLLER;
            game_options_index = PORTWATCH_ROW_BASE;
            sPortWatchScroll = 0;
            reset_controller_options_index();
            reset_watch_item_is_actively_selected();
        }
        lastDev = dev;
        lastId = id;
    }
    return portWatchDeviceIsRaphnet(portWatchDevice()) && g_portWatchPage != PORTWATCH_CTLGENERAL;
}

/** The port device row drawn on top of the vanilla raphnet page. */
Gfx *portWatchDrawDeviceRow(Gfx *gdl)
{
    const s32 focused = (controller_options_index == PORTWATCH_CTL_INDEX_DEVICE);
    const s32 selected = focused && watch_item_is_actively_selected;
    const s32 y = PORTWATCH_DEVROW_Y;

    /* "input", not "controller": Rare's own "controller" row is right under it */
    gdl = draw_options_labels(gdl, XOFFSET_1, y, "input\n",
                              selected ? (u32) -1 : focused ? PORTWATCH_COL_SELECTED : PORTWATCH_COL_LABEL,
                              selected, PORTWATCH_COL_OUTLINE, 0, 0, PORTWATCH_COL_BG, 0);
    {   /* LEFT-ALIGNED UNDER THE CONTROL STYLE'S NAME.
         * draw_controller_style_text starts
         * "1.2 Solitaire" at x 0xAA; portWatchDrawValueAt CENTRES on its x, so
         * hand it the text's own middle. */
        extern const char *portWatchDeviceText(void);   /* port_watchpages.c */
        extern s32 portInputAssignActive(void);
        const char *dev = portInputAssignActive() ? "PRESS A BUTTON\n" : portWatchFitText(portWatchDeviceText(), 300 - 0xAA);
        s32 th = 0;
        s32 tw = 0;

        textMeasure(&th, &tw, (char *) dev, ptrFontBankGothicChars, ptrFontBankGothic, 10);
        gdl = portWatchDrawValueAt(gdl, 0xAA + tw / 2, y, dev,
                                   selected ? PORTWATCH_COL_SELECTED : PORTWATCH_COL_VALUE_ON);
    }
    return gdl;
}

s32 portWatchRaphnetDeviceRowFocused(void)
{
    return controller_options_index == PORTWATCH_CTL_INDEX_DEVICE;
}

/** Left/right step the device while the device row is selected. */
void portWatchDeviceRowNavigate(void)
{
    if (!watch_item_is_actively_selected)
    {
        return;
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS | L_TRIG | L_JPAD) || sub_GAME_7F0A4FB0())
    {
        portWatchDeviceStep(-1);
        set_controlstick_lr_disabled();
        sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS | R_TRIG | R_JPAD) || sub_GAME_7F0A4FEC())
    {
        portWatchDeviceStep(1);
        set_controlstick_lr_disabled();
        sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
    }
}

/** Up/down over the raphnet page's three rows (control style, input,
 * controller), wrapping like the vanilla pair. It owns the whole walk with one
 * input read and one move, because bolting a row onto Rare's two-row handler
 * double-moved in the same frame. */
void portWatchRaphnetNavigate(void)
{
    static const s32 order[3] = {   /* as drawn: control style, input, controller */
        CONTROLLER_OPTIONS_INDEX_STYLE, PORTWATCH_CTL_INDEX_DEVICE, CONTROLLER_OPTIONS_INDEX_INPUTS,
    };
    s32 dir = 0;
    s32 at;

    if (watch_item_is_actively_selected)
    { /* a selected row owns the input: the style list and the picture are
       * vanilla's, the device row is ours */
        if (portWatchRaphnetDeviceRowFocused())
        {
            portWatchDeviceRowNavigate();
        }
        return;
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS | U_JPAD) || sub_GAME_7F0A5088())
    {
        dir = -1;
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS | D_JPAD) || sub_GAME_7F0A50C4())
    {
        dir = 1;
    }
    if (dir == 0)
    {
        return;
    }
    for (at = 0; at < 2 && order[at] != (s32) controller_options_index; at++)
    {
    }
    controller_options_index = order[(at + dir + 3) % 3];
    disable_watch_stick_y_nav_ready();
}

/** Debug: GE_WATCHPAGE="tab[,subpage[,row[,selected]]]" parks the open watch on
 * one page every tick so headless captures can photograph it (open the watch
 * first, e.g. with GE_JOYPORT_AUTO="watch_objectives:<poll>"). Never set in
 * midas.ini. */
void portWatchHarnessTick(void)
{
    static s32 shown = 0;   /* a draw has shown the stepped (pending) value */
    static s32 parsed = -1;
    static s32 tab = -1;
    static s32 sub = PORTWATCH_GENERAL;
    static s32 row = -1;
    static s32 sel = 0;
    /* 5th/6th fields: step the selected row n times, then Accept it on the
     * NEXT tick. The resolution row applies only on Accept, and there is no
     * other way to exercise that without a keyboard. */
    static s32 step = 0;
    static s32 accept = 0;
    static s32 stepped = 0;

    if (parsed < 0)
    {
        extern char *getenv(const char *);
        const char *e = getenv("GE_WATCHPAGE");

        parsed = 0;
        if (e != NULL && e[0] != 0)
        {
            parsed = 1;
            sscanf(e, "%d,%d,%d,%d,%d,%d", &tab, &sub, &row, &sel, &step, &accept);
        }
    }
    if (!parsed)
    {
        return;
    }
    { /* Debug: GE_WATCHDEV=raphnet puts the Control tab's device picker on the
       * raphnet adapter so the vanilla page can be photographed headlessly.
       * Needs the adapter plugged in; otherwise the picker is left alone. */
        extern char *getenv(const char *);
        static s32 devdone;
        const char *d = getenv("GE_WATCHDEV");

        /* Re-asserted every tick while a raphnet device exists, because
         * entering the Control tab resets the picker to the player's own
         * device. With no raphnet it's left alone. */
        if (d != NULL && d[0] == 'r' && !portWatchDeviceIsRaphnet(portWatchDevice()))
        {
            extern s32 portInputDeviceIsRaphnet(s32 dev);
            s32 k;
            s32 have = 0;

            for (k = 1; k < portInputDeviceCount(); k++)
            {
                have |= portInputDeviceIsRaphnet(k);
            }
            for (k = 0; have && k < 8 && !portWatchDeviceIsRaphnet(portWatchDevice()); k++)
            {
                portWatchDeviceStep(1);
            }
            if (!devdone && !portWatchDeviceIsRaphnet(portWatchDevice()))
            {
                portWatchDeviceValidate();
            }
            devdone = 1;
        }
    }
    if (tab >= 0 && tab < WATCH_NUMBER_SCREENS)
    {
        /* Go through the L/R page-flip routine rather than assigning the index,
         * because each page has its own watch zoom. Forcing the index alone
         * gives the right page at the wrong zoom, with the volume bars off
         * their labels. */
        portWatchGotoPage(tab);
    }
    /* both tabs the port pages own; the Control tab's pages are
     * PORTWATCH_CONTROLLER and PORTWATCH_CONTROLLER_MORE */
    if (watch_screen_index != WATCH_INDEX_GAME_OPTIONS
        && watch_screen_index != WATCH_INDEX_CONTROL_OPTIONS)
    {
        return;
    }
    if (sub >= 0 && sub < PORTWATCH_PAGES)
    {
        g_portWatchPage = sub;
        portWatchPageEntered(sub);   /* parked, not opened: do what opening it does (idempotent) */
    }
    if (row >= 0)
    {
        game_options_index = PORTWATCH_ROW_BASE + row;
    }
    watch_item_is_actively_selected = sel ? 1 : 0;

    { /* On a row with a drop-down, `sel` opens the list (as A now does), `step`
       * moves inside it and `accept` picks. Same three fields, so existing
       * recipes still work. */
        const s32 hrow = (s32) game_options_index - PORTWATCH_ROW_BASE;

        if (sel && hrow >= 0 && portWatchRowDropdown(g_portWatchPage, hrow)
            && !portWatchDropIsOpen())
        {
            portWatchDropOpen(g_portWatchPage, hrow);
            portWatchDropAnchor(hrow);
        }
        /* `sel` on a BIND row arms the key capture, the way A does - so
         * GE_WATCHBINDKEY has something to be captured into */
        if (sel && hrow >= 0 && !portWatchBindCaptureActive()
            && portWatchRowKind(g_portWatchPage, hrow) == PWROW_BIND)
        {
            portWatchBindCaptureBegin(g_portWatchPage, hrow);
        }
        if (portWatchDropIsOpen())
        {
            watch_item_is_actively_selected = 0; /* the list IS the edit mode */
        }
    }

    if (step != 0 && !stepped)
    {
        const s32 dir = (step > 0) ? 1 : -1;
        s32 k = (step > 0) ? step : -step;

        while (k-- > 0)
        {
            if (portWatchDropIsOpen())
            {
                portWatchDropMove(dir);
                portWatchDropScrollFollow(portWatchDropVisible());
            }
            else
            {
                portWatchRowStep(g_portWatchPage, (s32) game_options_index - PORTWATCH_ROW_BASE, dir);
            }
        }
        stepped = 1;
        return; /* let one frame draw the PENDING value before accepting */
    }
    if (accept && stepped)
    {   /* the accept is the first TICK after a draw showed the
         * pending value - the same frame's draw call used to commit it */
        if (sPortWatchHarnessDraw)
        {
            shown = 1;
            return;
        }
        if (!shown)
        {
            return;
        }
        accept = 0;
        if (portWatchDropIsOpen())
        {
            portWatchDropChoose();
        }
        else if (portWatchRowKind(g_portWatchPage, (s32) game_options_index - PORTWATCH_ROW_BASE) == PWROW_DEVICE)
        {
            portWatchAssignBegin();   /* the "input" row: photograph "press a button" */
        }
        else
        {
            portWatchResCommit();
            portWatchDeferCommit();
        }
    }
}

/**
 * Up / down over the current page. Replaces sub_GAME_7F0A5998's hard-coded
 * wrap at 10 (2 sliders + 8 toggles), which is wrong the moment a page has a
 * different number of rows.
 */
void portWatchRowNavigate(void)
{
    s32 idx;
    s32 dir = 1;
    const s32 first = portWatchIndexFirst();
    const s32 limit = portWatchIndexLimit();
    const s32 before = (s32) game_options_index;

    portWatchDropAnimTick();
    { /* Unbind: hold any controller button for two seconds on a bind row, or
       * press Delete; runs before the capture branch so it works armed or not,
       * but not for buttons the capture blanks (a known gap on the Mouse and
       * Keyboard page). Counted in 60 Hz menu fields, so two seconds holds at
       * any tick rate. */
        static s32 held;
        static s32 fired;
        const s32 brow = portWatchCurrentRow();
        const s32 isbind = (brow >= 0)
                        && portWatchRowKind(g_portWatchPage, brow) == PWROW_BIND;
        /* spent EVERY tick, not behind `isbind &&`: a Delete pressed off a
         * bind row is thrown away here, never banked for the next bind row
         * the highlight reaches */
        const s32 del = portVideoDeleteEdge();
        /* while a capture is armed only ITS row may be unbound. Nothing moves
         * the highlight off it any more (the mouse layer yields), but refusing
         * beats wiping an action the player never armed */
        const s32 ontarget = !portWatchBindCaptureActive()
                          || portWatchBindCapturing(g_portWatchPage, brow);
        const s32 down = joyGetButtons(PLAYER_1, 0xFFFF) != 0;
        static s32 holdRow = -2;   /* the row this hold was PRESSED on: -2 = nothing held, -3 = spent */
        static s32 holdPage = -1;
        static s32 lastRow = -2;   /* where the highlight was on the previous call */
        static s32 lastPage = -1;

        /* A hold belongs to the row it was pressed on: it only counts if the
         * highlight was already on this row when it began, and is spent once
         * the highlight leaves. Otherwise a held key that moved the highlight
         * would unbind the row it landed on. */
        if (!down)
        {
            holdRow = -2;
        }
        else if (holdRow == -2)
        {
            holdRow = (brow == lastRow && g_portWatchPage == lastPage) ? brow : -3;
            holdPage = g_portWatchPage;
        }
        else if (holdRow != brow || holdPage != g_portWatchPage)
        {
            holdRow = -3;
        }
        lastRow = brow;
        lastPage = g_portWatchPage;

        if (isbind && down && holdRow == brow && holdPage == g_portWatchPage)
        {
            if (portWatchMenuField())
            {
                held++;
            }
            if (held >= PORTWATCH_UNBIND_FIELDS && !fired && ontarget)
            {
                fired = 1;   /* once per hold, not once per field */
                sPortWatchHoldUnbound = 1;
                portWatchBindCaptureCancel();
                if (portWatchBindUnbind(g_portWatchPage, brow))
                {
                    sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
                }
            }
        }
        else
        {
            held = 0;
            fired = 0;
        }
        if (isbind && del && ontarget)
        {
            portWatchBindCaptureCancel();
            if (portWatchBindUnbind(g_portWatchPage, brow))
            {
                sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
            }
        }
    }
    if (portWatchBindCaptureActive())
    { /* an armed capture swallows navigation: every key belongs to it, which
       * is the whole point. It ends itself when something is pressed, and
       * Escape inside it cancels (video.c refuses to bind Escape). */
        portWatchBindCaptureTick();
        if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
        {
            if (portWatchBindCaptureActive())
            {
                sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);   /* the cancel sounds as Escape's does */
            }
            portWatchBindCaptureCancel();
        }
        disable_watch_stick_y_nav_ready();
        return;
    }
    if (portWatchDropIsOpen())
    { /* the list owns up/down while it is open, and the highlight does not
       * move off the row underneath it. B closes without applying - nothing
       * WAS applied, so there is nothing to undo. */
        if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
        {
            portWatchDropClose();   /* animated: B is a cancel, not a page change */
            sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);   /* as Escape's close */
            disable_watch_stick_y_nav_ready();
            return;
        }
        {   /* LOOK-STICK QUICK SCROLL (as front.c portMenuLookSteps) */
            extern s32 portMenuLookSteps(void);
            extern s32 portMenuLookActive(void);
            const s32 look = portMenuLookSteps();
            const u16 cup = portMenuLookActive() ? 0 : U_CBUTTONS;   /* not its C-button echo too */
            const u16 cdn = portMenuLookActive() ? 0 : D_CBUTTONS;

            if (look != 0)
            {
                s32 k;

                for (k = 0; k < ((look < 0) ? -look : look); k++)
                {
                    portWatchDropMove((look < 0) ? -1 : 1);
                }
                portWatchDropScrollFollow(portWatchDropVisible());
                disable_watch_stick_y_nav_ready();
            }
            else if (joyGetButtonsPressedThisFrame(PLAYER_1, cup | U_JPAD) || sub_GAME_7F0A5088())
            {
                portWatchDropMove(-1);
                portWatchDropScrollFollow(portWatchDropVisible());
                disable_watch_stick_y_nav_ready();
            }
            else if (joyGetButtonsPressedThisFrame(PLAYER_1, cdn | D_JPAD) || sub_GAME_7F0A50C4())
            {
                portWatchDropMove(1);
                portWatchDropScrollFollow(portWatchDropVisible());
                disable_watch_stick_y_nav_ready();
            }
        }
        /* No unconditional follow here: run every tick it combined with
         * portWatchMouseTick's hover to run the list to its end. The window
         * moves only when the pad moves the highlight. */
        { /* The resolution list can shrink while open (window resized or moved
           * to another monitor), so keep the window inside it or the last slot
           * draws blank and a click applies a stale entry. This never chases
           * the highlight, so it can't cause the loop above. */
            const s32 maxs = portWatchDropCount() - portWatchDropVisible();

            if (portWatchDropScroll() > maxs)
            {
                portWatchDropSetScroll(maxs);
            }
        }
        return;
    }

    /* B backs out of a port sub-page, exactly like its Back row; the Menu Back
     * bind and a raphnet's B both arrive as B. Tab roots stay inert as in
     * vanilla, and an open capture or list already took its B above. */
    {   /* HOLD B TO UNBIND TOO (as the folder): on a bind
         * row B's back waits for the release - a tap backs out, a two-second
         * hold unbinds (above) and does not. An injected B (right-click,
         * Escape, Menu Back) has no level and backs out at once. */
        static s32 bwait;
        const s32 subpage = g_portWatchPage != PORTWATCH_GENERAL && g_portWatchPage != PORTWATCH_CONTROLLER;
        s32 goback = 0;

        if (bwait)
        {
            if (!joyGetButtons(PLAYER_1, B_BUTTON))
            {
                bwait = 0;
                goback = !sPortWatchHoldUnbound;
            }
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON) && subpage)
        {
            const s32 brow = portWatchCurrentRow();

            if (brow >= 0 && portWatchRowKind(g_portWatchPage, brow) == PWROW_BIND && joyGetButtons(PLAYER_1, B_BUTTON))
            {
                bwait = 1;
                sPortWatchHoldUnbound = 0;
            }
            else
            {
                goback = 1;
            }
        }
        if (goback && subpage)
        {
            portWatchGotoSubPage(portWatchParentPage(g_portWatchPage)); /* plays the click itself */
            disable_watch_stick_y_nav_ready();
            return;
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS | U_JPAD) || sub_GAME_7F0A5088())
    {
        game_options_index = game_options_index - 1;
        dir = -1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS | D_JPAD) || sub_GAME_7F0A50C4())
    {
        game_options_index = game_options_index + 1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }

    /* game_options_index is unsigned, so "went below first" shows up as a
     * huge value - test it as signed, the way the vanilla code could not */
    idx = (s32) game_options_index;
    if (idx >= limit)
    {
        game_options_index = first;
    }
    else if (idx < first)
    {
        game_options_index = limit - 1;
    }

    if ((s32) game_options_index != before)
    {
        /* the highlight moved off whatever it was on: an unaccepted
         * resolution or display mode is discarded. Compare against the value
         * from BEFORE the up/down above - `idx` is the post-move one and would
         * only ever catch the wrap. */
        portWatchResCancel();
        portWatchDeferCancel();
    portWatchDropCloseNow();
    portWatchBindCaptureCancel();
    }

    { /* Skip hint rows in the direction of travel, bounded so a page of only
       * hints can't spin forever. `dir` comes from the key pressed, since
       * comparing indexes before and after the wrap got the direction wrong. */
        s32 guard = limit - first + 1;

        while (guard-- > 0
               && (s32) game_options_index >= PORTWATCH_ROW_BASE
               && !portWatchRowSelectable(g_portWatchPage, (s32) game_options_index - PORTWATCH_ROW_BASE))
        {
            s32 next = (s32) game_options_index + dir;
            if (next >= limit)
            {
                next = first;
            }
            else if (next < first)
            {
                next = limit - 1;
            }
            game_options_index = next;
        }
    }
    portWatchScrollFollow();
    { /* GE_WATCHNAVDBG=1: every move of the highlight, with the page's own
       * bounds. This is the gate on "the last row is navigable": walk the
       * page with a pulsed D_JPAD and the log must wrap, not stop. */
        static s32 dbg = -1;
        extern char *getenv(const char *);
        if (dbg < 0)
        {
            dbg = getenv("GE_WATCHNAVDBG") != NULL;
        }
        if (dbg && (s32) game_options_index != before)
        {
            fprintf(stderr, "ge: watchnav: page %d row %d -> %d (first %d limit %d)\n",
                    (int) g_portWatchPage, (int) before, (int) game_options_index, (int) first, (int) limit);
        }
    }
}
#endif /* TARGET_N64 */

Gfx *draw_toggle_options(Gfx *gdl)
{
    s32 y_offset;
    s32 i;

#ifndef TARGET_N64
    /* MENU OVERHAUL: the eight per-player toggles are not what this
     * page shows any more - see portWatchDrawRows above. The vanilla body
     * below is kept verbatim for the N64 build and as the reference for what
     * the colours and geometry are supposed to be. */
    return portWatchDrawRows(gdl);
#endif

    gdl = microcode_constructor(gdl);

    for (i = 0, y_offset = YOFFSET_1; i < 8; i = i + 1, y_offset = y_offset + YINC) {

        if ( i == game_options_index - 2)
        {
            // Draw option that is highlighted and selected, if there is one.
            if (watch_item_is_actively_selected)
            {
                gdl = draw_toggle_option_values(draw_options_labels(gdl, XOFFSET_1, y_offset, langGet(game_options_entries[i].text[0]), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0), y_offset, i, 2);
            }
            // Draw option that is highlighted but not selected, if there is one.
            else
            {
                gdl = draw_toggle_option_values(draw_options_labels(gdl, XOFFSET_1, y_offset, langGet(game_options_entries[i].text[0]), 0xA0FFA0F0, 0, -1, 0, 0, 0x3000B0, 0), y_offset, i, 1);
            }
        }
        // Draw the options that are neither highlighted nor selected.
        else
        {
            gdl = draw_toggle_option_values(draw_options_labels(gdl, XOFFSET_1, y_offset, langGet(game_options_entries[i].text[0]), 0xFF00B0, 0, -1, 0, 0, 0x3000B0, 0), y_offset, i, 0);
        }

    }

    return gdl;
}


Gfx *draw_watch_game_options_page(Gfx *gdl, Mtx *param_2) {
    s32 sp5C;
    u16 *textptr;
    s32 sp54;
    s32 sp50;
    s32 sp4C;
    s32 sp48;

    struct font *pFontFile;
    struct fontchar *pFontChars;

    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
#ifndef TARGET_N64
        /* Whose page is showing is derived from the TAB, not trusted from
         * whatever was left behind: the two tabs share one page variable, and
         * a path into this tab that forgot to reset it would otherwise draw
         * the Controller rows here. */
        if (g_portWatchPage >= PORTWATCH_CONTROLLER)
        {
            portWatchResetPage();
        }
        /* the volume sliders belong to General; a sub-page draws its own
         * eight rows and nothing else (spec section 3) */
        if (g_portWatchPage != PORTWATCH_GENERAL)
        {
            return draw_toggle_options(gdl);
        }
#endif
        gdl = draw_music_volume_slider(gdl);
        gdl = draw_fx_volume_slider(gdl);
        pFontFile = ptrFontBankGothic;
        pFontChars = ptrFontBankGothicChars;
        gdl = microcode_constructor(gdl);

        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_35_MUSIC_LF)); //music

        sp54 = XOFFSET_1;
        sp50 = YOFFSET_8;

        sp5C = 0xFF00B0;

        if (game_options_index == 0)
        {
            sp5C = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                sp5C = -1;
            }
        }

        textMeasure(&sp48, &sp4C, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (game_options_index == 0))
        {
            gdl = textRenderOutlined(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, 0x7000A0, sp4C + 1, sp48, 0, 0);
        }
        else
        {
            gdl = textRender(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, sp4C, sp48, 0, 0);
        }

        sp5C = 0xFF00B0;
        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_36_FX_LF)); //fx

        sp54 = XOFFSET_1;
        sp50 = YOFFSET_9;


        if (game_options_index == 1)
        {
            sp5C = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                sp5C = -1;
            }
        }

        textMeasure(&sp48, &sp4C, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (game_options_index == 1))
        {
            gdl = textRenderOutlined(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, 0x7000A0, sp4C + 1, sp48, 0, 0);
        }
        else
        {
            gdl = textRender(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, sp4C, sp48, 0, 0);
        }

        gdl = draw_toggle_options(gdl);
    }

    return gdl;
}


int sub_GAME_7F0AC0E8(u8 *arg) {
    u8 cVar1;
    int count;

    cVar1 = *arg;
    count = 0;

    while (cVar1) {

        if (cVar1 == 0xA) {
            count = count + 1;
        }

        cVar1 = arg[1];
        arg += 1;
    }

    return count;
}


u8 *sub_GAME_7F0AC120(u8 *arg)
{
    u8 *ret;

    ret = arg;

    while (*arg != 0)
    {
        if (*arg == 0xa)
        {
            if (*++arg != 0)
            {
                ret = arg;
            }
        }
#if defined(VERSION_EU)
        else
        {
            arg++;
        }
#else
            arg++;
#endif
    }

    return ret;
}


#if defined(VERSION_EU)
//D:800577C0
const char D_800577C0[] = "\n";
#endif


Gfx *draw_watch_mission_briefing_page(Gfx *gdl, Mtx *param_2)
{
    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        /**
         * spDAC, spDA4, spD68 are unused.
         * Maybe vestigial tables for formatting the briefings.
         */
        s32 spDAC[15] = {0x34, 0x2f, 0x2d, 0x2a, 0x28, 0x25, 0x25, 0x28, 0x2a, 0x2d, 0x2f, 0x34, 0x37, 0x40, -1};
        s32 spDA4[2] = {0x4b, -1};
        s32 spD68[15] = {0x10e, 0x113, 0x116, 0x119, 0x11a, 0x11b, 0x11b, 0x11a, 0x119, 0x116, 0x113, 0x10e, 0x108, 0xfe, -1};

        s32 boxLeft;
        s32 boxTop;
        s32 boxRight;
        s32 boxBottom;
        s32 textHeight = 0;
        s32 textWidth = 0;

        struct font *font = ptrFontBankGothic;
        struct fontchar *chars = ptrFontBankGothicChars;

#if defined(VERSION_EU)
            char wrappedText[3000];
#else
            char wrappedText[3000] = "\n";
#endif

        char pageTitle[0x20];
        char *completeText;
        char *incompleteText;
        char *failedText;
        char *titleText;

        s32 lineCount;
        s32 objectiveRow;
        s32 objY;
        s32 objX;

        completeText = langGet(0xac28);
        incompleteText = langGet(0xac29);
        failedText = langGet(0xac37);
        titleText = get_ptr_text_for_watch_breifing_page(BRIEFING_TITLE);

#if defined(VERSION_EU)
        strcpy(wrappedText, D_800577C0);
#endif

        gdl = microcode_constructor(gdl);
        textMeasure(&textHeight, &textWidth, titleText, chars, font, 0);

        boxLeft = ((0xaa - textWidth) / 2) + 0x4b;
        boxTop = 0x1e;
        boxRight = boxLeft + textWidth;
        boxBottom = boxTop + textHeight;

        gdl = draw_blackbox_to_screen(gdl, &boxLeft, &boxTop, &boxRight, &boxBottom);
        gdl = textRender(gdl, &boxLeft, &boxTop, titleText, chars, font, 0xa0ffa0f0, textWidth, textHeight, 0, 0);

        boxLeft = 0x41;

        switch (mission_brief_index)
        {
            case BRIEF_INDEX_BACKGROUND:
                sprintf(pageTitle, langGet(0xac38));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_OVERVIEW), wrappedText, chars, font);
                mission_brief_background_navigation();
                break;

            case BRIEF_INDEX_M:
                sprintf(pageTitle, langGet(0xac39));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_M), wrappedText, chars, font);
                mission_brief_m_briefing_navigation();
                break;

            case BRIEF_INDEX_Q:
                sprintf(pageTitle, langGet(0xac3a));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_Q), wrappedText, chars, font);
                mission_brief_q_branch_navigation();
                break;

            case BRIEF_INDEX_MONEYPENNY:
                sprintf(pageTitle, langGet(0xac3b));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_MONEYPENNY), wrappedText, chars, font);
                mission_brief_moneypenny_navigation();
                break;

            case BRIEF_INDEX_OBJECTIVES:
            {
#if defined(VERSION_EU)
                char objectiveBuffer[200];
#else
                char objectiveBuffer[200] = "";
#endif
                u32 colour;
                s32 i;
                s32 j;
                char *objectiveText;
                s32 visibleObjectiveIndex;
                s32 pad;
                char *objectiveLetterPtr;

                lineCount = 1;
                objectiveRow = 0;
                visibleObjectiveIndex = 0;

                setTextOverlapCorrection((j_text_trigger) ? (1) : (5));
                sprintf(pageTitle, langGet(0xac3c));

                for (i = 0; i < objectiveGetCount(); i++)
                {
                    if (get_difficulty_for_objective(i) <= lvlGetSelectedDifficulty())
                    {
                        objectiveText = get_text_for_objective(i);
                        objectiveBuffer[0] = '\0';

                        for (j = 0; j < lineCount; j++)
                        {
                            strcat(objectiveBuffer, D_80058440);
                        }

                        for (j = 0; j < objectiveRow; j++)
                        {
                            strcat(objectiveBuffer, D_80058444);
                        }

                        objectiveLetterPtr = objectiveBuffer + strlen(objectiveBuffer);

                        sprintf(objectiveLetterPtr, aC_2, visibleObjectiveIndex + 'a');
                        strcat(objectiveBuffer, objectiveText);

                        objY = boxTop + ((j_text_trigger) ? (1) : (5));
                        objX = 0x3c;

                        gdl = textRender(gdl, &objX, &objY, objectiveBuffer, chars, font, 0x00ff00b0, viGetX(), viGetY(), 0, 10);
                        lineCount += sub_GAME_7F0AC0E8(objectiveLetterPtr);
                        textMeasure(&textHeight, &textWidth, sub_GAME_7F0AC120(objectiveLetterPtr), chars, font, 10);

                        if ((textWidth + 0x3c) < (viGetX() / 2))
                        {
                            lineCount--;
                        }

                        objectiveBuffer[0] = '\0';

                        for (j = 0; j < lineCount; j++)
                        {
                            strcat(objectiveBuffer, D_80058450);
                        }
                        for (j = 0; j < objectiveRow; j++)
                        {
                            strcat(objectiveBuffer, D_80058454);
                        }

                        switch (get_status_of_objective(i))
                        {
                            case OBJECTIVESTATUS_INCOMPLETE:
                                strcat(objectiveBuffer, incompleteText);
                                colour = (D_80040AFC << 16) | 0x400040ff;
                                break;

                            case OBJECTIVESTATUS_COMPLETE:
                                strcat(objectiveBuffer, completeText);
                                colour = 0xa0ffa0f0;
                                break;

                            case OBJECTIVESTATUS_FAILED:
                                strcat(objectiveBuffer, failedText);

                                if (j_text_trigger)
                                {
                                    colour = 0xa0ffa0f0;
                                }
                                else
                                {
                                    colour = 0x00ff00b0;
                                }
                                break;

                            default:
                                break;
                        }

                        textMeasure(&textHeight, &textWidth, objectiveBuffer, chars, font, 10);
                        objY = boxTop + ((j_text_trigger) ? (1) : (5));

                        if (j_text_trigger)
                        {
                            objX = 0xf5 - textWidth;
                        }
                        else
                        {
                            objX = 0xaf;
                        }

                        gdl = textRender(gdl, &objX, &objY, objectiveBuffer, chars, font, colour, 0xd2, viGetY(), 0, 10);

                        objectiveRow++;
                        visibleObjectiveIndex++;
                    }
                }

                setTextOverlapCorrection(-1);
                mission_brief_objectives_navigation();

                break;

            }
        }

        textMeasure(&textHeight, &textWidth, pageTitle, chars, font, 10);
        gdl = textRender(gdl, &boxLeft, &boxTop, pageTitle, chars, font, 0xa0ffa0f0, 0xd2, 0x82, 0, 10);

        boxTop += 5;
        boxLeft = 0x3c;

        textMeasure(&textHeight, &textWidth, wrappedText, chars, font, 10);
        gdl = textRender(gdl, &boxLeft, &boxTop, wrappedText, chars, font, 0x00ff00b0, viGetX(), viGetY(), 0, 10);
    }

    return gdl;
}


//D:80058440
const char D_80058440[] = " \n";
//D:80058444
const char D_80058444[] = " \n\n";
//D:80058448
const char aC_2[] = "%c: ";
//D:80058450
const char D_80058450[] = " \n";
//D:80058454
const char D_80058454[] = " \n\n";


/**
 * Address: 7F0ACA28
 */
Gfx *draw_watch_current_page(Gfx *gdl, Mtx *arg1, s32 watch_transitioning)
{
    set_page_rectangle_colors(watch_screen_index, (struct WatchVertex *)g_CurrentPlayer->buffer_for_watch_greenbackdrop_vertices);

    if (watch_transitioning == TRUE)
    {
        set_BONDdata_outside_watch_menu_flag(FALSE);
        sub_GAME_7F0BD8FC(0);

#ifndef TARGET_N64
        /* every frame, on every tab: the "input" row's answering press is let
         * go of here (portWatchAssignTick only runs on the Control tab, and a
         * swallow left armed would eat A on the other four). NOT the tick
         * itself: that would run the listener twice a frame. */
        portWatchAssignSwallowPoll();
#endif
        // Handle A or Z button click when in any page but inventory page
        if ((watch_screen_index != WATCH_INDEX_INVENTORY) && (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON))
#ifdef GE_VIDEO
            && !portWatchTakeClick()   /* a mouse click the watch mouse layer already acted on */
#endif
            )
        {
#ifndef TARGET_N64
            if (portInputAssignActive() || portWatchAssignSwallowing())
            {
                /* armed: this press is the ANSWER (a key, or mouse left = Z),
                 * not a confirm - input_pc.c reads it, nothing here does. And
                 * just after: the answering press, still held. */
            }
            else
            /* the raphnet page's "input" row arms the assign, as the port
             * page's does through portWatchActivateRow */
            if (watch_screen_index == WATCH_INDEX_CONTROL_OPTIONS && portWatchRaphnetView()
                && portWatchRaphnetDeviceRowFocused())
            {
                portWatchAssignBegin();
            }
            else
            /* the raphnet page's third row: a button into general control
             * options, not Rare's "turn the controller picture" edit mode */
            if (watch_screen_index == WATCH_INDEX_CONTROL_OPTIONS && portWatchRaphnetView()
                && controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS)
            {
                portWatchGotoSubPage(PORTWATCH_CTLGENERAL);
            }
            else
            /* "display options" / "gameplay options" / "back" are buttons:
             * A or Z on one of them navigates instead of arming the
             * left/right value editor */
            if ((watch_screen_index != WATCH_INDEX_GAME_OPTIONS
                 && !(watch_screen_index == WATCH_INDEX_CONTROL_OPTIONS && !portWatchRaphnetView()))
                || !portWatchActivateRow())
#endif
            watch_play_beep_sound();
        }

        switch (watch_screen_index)
        {
            case WATCH_INDEX_MISSION_STATUS:
                gdl = draw_watch_mission_status_page(gdl, arg1);
                break;
            case WATCH_INDEX_INVENTORY:
                gdl = draw_watch_inventory_page(gdl, arg1);
                break;
            case WATCH_INDEX_CONTROL_OPTIONS:
                gdl = draw_watch_control_options_page(gdl, arg1);
                break;
            case WATCH_INDEX_GAME_OPTIONS:
                gdl = draw_watch_game_options_page(gdl, arg1);
                break;
            case WATCH_INDEX_MISSION_BRIEFING:
                gdl = draw_watch_mission_briefing_page(gdl, arg1);
        }
#ifdef GE_VIDEO
        /* a consumed click lives one frame. The Inventory tab never
         * reaches the Z|A test above, so a click there (or one that made no
         * Z) left the flag up to eat the next pad/keyboard A on another tab. */
        (void) portWatchTakeClick();
#endif
        /* the watch mouse crosshair (portWatchMouseDraw) is drawn at the end
         * of maybe_mp_interface so nothing later in the frame covers it */
    }
    else if (watch_transitioning == FALSE)
    {
        sub_GAME_7F0BD8FC(1);
        set_BONDdata_outside_watch_menu_flag(TRUE);
        gdl = draw_background_health_and_armor_transitioning(gdl, arg1);
    }

    return gdl;
}
