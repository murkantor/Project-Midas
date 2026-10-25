#ifndef TARGET_N64
#include "port_options.h"
#endif
#include <os_extension.h>
#include "game/debugmenu_handler.h"
#include "game/options.h"
#include "game/propobj.h"
#include "game/ramromreplay.h"
#include "game/viewport.h"
#ifndef TARGET_N64
#include <stdio.h> /* frontDrawCursor GE_DUMP_BOND probe */
#endif
#include <assets/oddtextures.h>
#include "assets/image_externs.h"
#include <ultra64.h>
#include "gun.h"
#include <bondgame.h>
#include <bondconstants.h>
#include <boss.h>
#include <fr.h>
#include "language.h"
#include <joy.h>
#include <music.h>
#include <random.h>
#include <snd.h>
#include <macro.h>  
#include "bondview.h"
#include "chr.h"
#include "chr_b.h"
#include "chraction.h"
#include "cheat.h"
#include "chrobjdata.h"
#include "file2.h"
#include "front.h"
#include "image_bank.h"
#include "lv.h"
#include "initanitable.h"
#include "mp_weapon.h"
#include "math_floor.h"
#include "objective_status.h"
#include "player.h"
#include "spectrum.h"
#include "textrelated.h"
#include "blood_animation.h"
#include "game/othermodemicrocode.h"
#include "game/bondwalk2.h"
#include "game/file2.h"
#include "objecthandler.h"
#include "dyn.h"
#include "assets/obseg/text/LtitleE.h"
#include "matrixmath.h"
#include "bg.h"
#include "chrai.h"
#include "title.h"
#include <assets/font_dl.h>
#include "image.h"
#include "ob.h"
#include "gbi_extension.h"
#include "model.h"


/**
 * Each character showing in the display cast screen lasts 3 seconds with a half second cross-fade
 * and the camera velocity is tuned per refresh rate so its decay per second is the
 * same on PAL (0.95 ^ (60/50) = 0.9403). 
 */
#ifdef REFRESH_PAL
#define CAST_INTRO_LEN          (50 * 3)
#define CAST_INTRO_FADE         (50 / 2)
#define CAST_DAMP               0.940299988f
#define CAST_DAMP_COMP          0.0597000122f
#else
#define CAST_INTRO_LEN          (60 * 3)
#define CAST_INTRO_FADE         (60 / 2)
#define CAST_DAMP               0.94999999f
#define CAST_DAMP_COMP          0.050000012f
#endif
#define CAST_INTRO_FADEOUT      (CAST_INTRO_LEN - CAST_INTRO_FADE + 1)
#define CAST_INTRO_LEN_F        ((f32)CAST_INTRO_LEN)
#define CAST_INTRO_FADE_F       ((f32)CAST_INTRO_FADE)

struct BriefingDataSomething
{
    u16 textid;
    u16 enabled_difficulty;
};

struct BriefingData
{
    u16 brief[4];
    struct BriefingDataSomething objective[OBJECTIVES_MAX];
};

// bss
//CODE.bss:800695A0
f32 cast_camera_dist;
//CODE.bss:800695A4
f32 cast_camera_dist_start;
//CODE.bss:800695A8
f32 cast_camera_dist_end;
//CODE.bss:800695AC
f32 cast_camera_angle;
//CODE.bss:800695B0
f32 cast_camera_angle_start;
//CODE.bss:800695B4
f32 cast_camera_angle_end;
//CODE.bss:800695B8
f32 cast_camera_height;
//CODE.bss:800695BC
f32 cast_camera_height_start;
//CODE.bss:800695C0
f32 cast_camera_height_end;
//CODE.bss:800695C4
f32 flt_CODE_bss_800695C4; // unused
//CODE.bss:800695C8
struct coord3d cast_target_smoothed;
//CODE.bss:800695D4
f32 flt_CODE_bss_800695D4; // unused
//CODE.bss:800695D8
struct coord3d cast_target_accumulator;

//CODE.bss:800695E4
bool cast_camera_reset;

//CODE.bss:800695E8
struct coord3d cast_rootpos_smoothed;

//CODE.bss:800695F4
f32 flt_CODE_bss_800695F4;
//CODE.bss:800695F8
struct coord3d cast_rootvel_smoothed;
//CODE.bss:80069604
f32 flt_CODE_bss_80069604;

//CODE.bss:80069608
struct coord3d cast_rootvel_accumulator;


//CODE.bss:80069614
f32 ninLogoRotRate;

/**
 * Address 80069618
 * EU .bss 80068558
*/
f32 ninLogoScale;

//CODE.bss:8006961C
f32 flt_CODE_bss_8006961C;
//CODE.bss:80069620
coord3d folderpositions_camspace[MAX_FOLDER_COUNT];

//CODE.bss:80069650
u8 cheat_available[CHEAT_MAX];

//CODE.bss:800696A0
u8 g_CheatActivated[CHEAT_MAX];

#ifdef VERSION_EU

/**
 * EU .bss 80068630
*/
s32 mp_char_cur_select_player[MAX_PLAYER_COUNT];

/**
 * EU .bss 80068680
*/
s32 mp_char_prev_select_player[MAX_PLAYER_COUNT];

/**
 * EU .bss 80068650
*/
s32 array_favweapon[MAX_PLAYER_COUNT][GUNHANDS];

#else

/**
 * Address 800696F0
*/
s32 array_favweapon[MAX_PLAYER_COUNT][GUNHANDS];

/**
 * Address 80069710
*/
s32 mp_char_cur_select_player[MAX_PLAYER_COUNT];

/**
 * Address 80069720
*/
s32 mp_char_prev_select_player[MAX_PLAYER_COUNT];

#endif


/**
 * Address 80069730
 * EU .bss 80068690
*/
s32 mp_char_select_scroll_offset[MAX_PLAYER_COUNT];

//CODE.bss:80069740
bool player_has_selected_char[MAX_PLAYER_COUNT];

//CODE.bss:80069750
s32 size_mp_select_image_player[MAX_PLAYER_COUNT];
//CODE.bss:80069760
s32 dword_CODE_bss_80069760[MAX_PLAYER_COUNT];
//CODE.bss:80069770
char* g_textPtrTAB1;
//CODE.bss:80069774
char* g_textPtrTAB2;
//CODE.bss:80069778
char* g_textPtrTAB3;
//CODE.bss:8006977C
s32 current_mp_stage_highlighted;
//CODE.bss:80069780
s32 dword_CODE_bss_80069780;
//CODE.bss:80069784
s32 mission_difficulty_highlighted;
//CODE.bss:80069788
s32 teamsize;
//CODE.bss:8006978C
struct BriefingData *ptrbriefingdata;
//CODE.bss:80069790
s32 g_NewCheatUnlocked;
//CODE.bss:80069794
s32 highlight_enemy_reaction;
//CODE.bss:80069798
s32 highlight_enemy_health;
//CODE.bss:8006979C
s32 highlight_enemy_damage;
//CODE.bss:800697A0
s32 highlight_enemy_accuracy;

//CODE.bss:800697A4
s32 dword_CODE_bss_800697A4;

//CODE.bss:800697A8
s32 player_handicap[MAX_PLAYER_COUNT];

//CODE.bss:800697B8
s32 controlstyle_player[MAX_PLAYER_COUNT];

//CODE.bss:800697C8
s32 highlight_players;
//CODE.bss:800697CC
s32 highlight_scenario;
//CODE.bss:800697D0
s32 highlight_gameselect;
//CODE.bss:800697D4
s32 highlight_gamelength;
//CODE.bss:800697D8
s32 highlight_character;
//CODE.bss:800697DC
s32 highlight_weaponselect;
//CODE.bss:800697E0
s32 highlight_health;
//CODE.bss:800697E4
s32 highlight_controlstyle;
//CODE.bss:800697E8
s32 highlight_aimadjustment;
//CODE.bss:800697EC                     .align 4

/**
 * Address 0x800697F0
 * EU .bss 0x80058730
*/
u32 arrayUnlockedCheats[CHEAT_INVALID];


MENU current_menu = MENU_INVALID;
MENU menu_update = MENU_INVALID;
MENU maybe_prev_menu = MENU_INVALID;

s32 g_MenuTimer = 0;

s32 tab_start_selected = FALSE;
s32 tab_next_selected = FALSE;
s32 tab_prev_selected = FALSE;

s32 tab_start_highlight = FALSE;
s32 tab_next_highlight = FALSE;
s32 tab_prev_highlight = FALSE;

s32 selected_folder_num = FOLDER1;
s32 selected_folder_num_copy = FOLDER1;

GAMEMODE gamemode = GAMEMODE_INTRO;
s32 selected_stage = LEVELID_NONE;
s32 briefingpage = BRIEFING_INVALID;
DIFFICULTY selected_difficulty = DIFFICULTY_MULTI;

s32 g_AppendCheatSinglePlayer = FALSE;
s32 g_AppendCheatMultiPlayer = FALSE;

f32 cursor_h_pos = 220.0;
f32 cursor_v_pos = 165.0;

#ifdef GE_VIDEO
/* While the mouse owns the cursor (it placed it more recently than the stick),
 * menu snap sites do not move it at all, so mouse users never see the cursor
 * jump to an option; any stick deflection hands ownership back and snaps resume
 * (see frontCursorSnapAllowed). */
s32 g_portMenuMouseOwns;
/* While a stage runs, mouse ownership is dropped so the Mission Failed /
 * Complete / MP entry screens can snap the cursor to their first item; the
 * first mouse move afterwards re-seats the pointer onto that snapped cursor. */
static s32 g_portMenuMouseReseat;
static void frontStageRunningMouseReset(void)
{
    g_portMenuMouseOwns = 0;
    g_portMenuMouseReseat = 1;
}
static s32 frontCursorSnapAllowed(void)
{
    return !g_portMenuMouseOwns;
}
#else
#define frontCursorSnapAllowed() 1
#endif

s32 final_menu_briefing_page = BRIEFING_M;
s32 current_menu_briefing_page = BRIEFING_TITLE;

s32 folder_selection_screen_option_icon = 0;
s32 folder_selected_for_deletion = FOLDER_INVALID;
s32 folder_selected_for_deletion_choice = FOLDER2;

s32 mission_failed_or_aborted = FALSE;
s32 g_isBondKIA = FALSE;

s32 is_first_time_on_legal_screen = TRUE;
s32 is_first_time_on_main_menu = TRUE;

s32 prev_keypresses = FALSE;
s32 ge_logo_bool = FALSE;

s32 maybe_is_in_menu = TRUE;

#ifndef TARGET_N64
/* Global volumes (midas.ini music_volume / sfx_volume, see options.c
 * portApplyGlobalVolumes): vanilla forced both to full whenever the frontend
 * opened a menu or the actor intro, so these wrappers apply the player's
 * setting instead (TARGET_N64 keeps the constants). */
static s32 portMusicVolumeRawSafe(void)
{
    extern s32 portMusicVolumeRaw(void); /* video.c; weak full-volume headless */

    return portMusicVolumeRaw();
}

static void portApplyGlobalVolumesHere(void)
{
    extern void portApplyGlobalVolumes(s32 musicraw, s32 sfxraw); /* options.c */
    extern s32 portSfxVolumeRaw(void);                            /* video.c */

    portApplyGlobalVolumes(portMusicVolumeRawSafe(), portSfxVolumeRaw());
}
#endif
s32 screen_size = SCREEN_SIZE_320x240;

s32 spectrum_related_flag = 0;
s32 is_emulating_spectrum = FALSE;

s32 is_cheat_menu_available = FALSE;
/* PORT: Files menu 4th entry "PORT OPTIONS" - its slot (row 3
 * with cheats unlocked, row 2 without; rows are 0-based) and the chosen
 * flag. Kept OUT of `gamemode` (an enum of 3 that indexes downstream).
 * docs/port/port-options-menu.md */

u8 * ptr_logo_and_walletbond_DL = NULL;
intptr_t ptr_menu_videobuffer = 0;
struct Model *logoinst = NULL;
struct Model * walletinst[] = { NULL, NULL, NULL, NULL};

Lights1 gelogolight = gdSPDefLights1(
    0x96, 0x96, 0x96,
    0xFF, 0xFF, 0xFF,
    77, 77, 46
);

Lights1 ninlogolight = gdSPDefLights1(
    0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00,
    0, 0, 0
);


f32 slider_007_mode_reaction = 0.0f;
f32 slider_007_mode_health = 1.0f;
f32 slider_007_mode_damage = 1.0f;
f32 slider_007_mode_accuracy = 1.0f;

/* 0x8002A9B0 */ rgba_u8 textglowR = { 160, 0, 0, 0 };
/* 0x8002A9B4 */ rgba_u8 textglowG = { 150, 0, 0, 0 };
/* 0x8002A9B8 */ rgba_u8 textglowB = { 40, 0, 0, 0 };
/* 0x8002A9BC */ rgba_u8 textglowA = { 140, 0, 0, 0 };

struct coord3d legalpage_pos = {0.0f, 0.0f, 0.0f};

struct legal_screen_text legalpage_text_array[] = {
    {220,  30, CENTER_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_07_TWY), 0}, //"TWYCROSS BOARD OF GAME CLASSIFICATION\n"
    { 34,  83, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_08_CERT), 0}, //"This is to certify\n"
    {226,  84, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_09_NINRARE), 0}, //"(c) 1997 Nintendo/Rare\n"
    {226,  97, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_10_DANJAQ), 0}, //"(c) 1962, 1995 Danjaq, LLC. &\n"
    {226, 110, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_11_UAC), 0}, //"U.A.C. All Rights Reserved\n"
    {226, 122, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_12_EON), 0}, //"(c) 1997 Eon Productions\n"
    {227, 134, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_13_MACB), 0}, //"Ltd. & Mac B. Inc.\n"
    {219, 211, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_14_PERSONS), 0}, //"Suitable only for 1-4 persons\n"
    { 60, 169, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_15_PRESIDENT), 0}, //"PRESIDENT\n"
    { 60, 201, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_16_VICE), 0}, //"VICE\n"
    { 99, 266, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_17_NORMAN), 0}, //"James Bond theme by Monty Norman.\n"
    { 80, 280, LEFT_ALIGN, CENTER_ALIGN, getStringID(LTITLE, TITLE_STR_18_EMI), 0}  //"Used by permission of EMI Unart Catalog Inc.\n"
};

/* 0x8002AABC */ ModelRenderData legalscreen_MRD = {NULL,
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


struct coord3d nintendologo_pos = {0};

ModelRenderData       nintendologo_MRD = {NULL,
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


struct coord3d goldeneyelogo_pos = { 0 };

ModelRenderData       goldeneyelogo_MRD = {NULL,
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


struct coord3d folderpositions[] = {
    {-900.0f, 800.0f, 0.0f},
    {1800.0f, 800.0f, 0.0f},
    {-1800.0f, -200.0f, 0.0f},
    {900.0f, -200.0f, 0.0f}
};

struct rectbbox folder_option_COPY_bound = { 0 };
struct rectbbox folder_option_ERASE_bound = { 0 };
#ifndef TARGET_N64
/* PORT: the Files screen's bottom bar has four buttons (Copy / Erase / Options
 * / Exit Game) instead of Rare's two (docs/port/files-screen-buttons.md); like
 * the vanilla ones, their hit boxes are rebuilt every frame. */
enum { PORT_FSBTN_COPY, PORT_FSBTN_ERASE, PORT_FSBTN_OPTIONS, PORT_FSBTN_EXIT, PORT_FSBTN_COUNT };
/* The four buttons sit in two rows of two at 0.8 scale, centred on Rare's bar
 * line (y 285), because four in one row only fit at ~0.7. The space between the
 * folders (y ~250) and the letterbox (y ~314) only fits two 26 px rows, so
 * don't go past ~0.85 scale. */
#define PORT_FSBTN_Y_TOP     270   /* Copy, Erase */
#define PORT_FSBTN_Y_BOTTOM  298   /* Options, Exit Game */
#define PORT_FSBTN_X_LEFT    270   /* icon centres of the two columns; "Exit Game" ends at ~419 of 440 */
#define PORT_FSBTN_X_RIGHT   360
#define PORT_FSBTN_SCALE     0.8f  /* of Rare's size: icons AND labels */
#define PORT_FSBTN_LABEL_GAP 4     /* px between an icon's right edge and its label */
#define PORT_FSBTN_HIT_PAD   3     /* the click box is this much bigger than what is drawn, every side */
static struct rectbbox port_filesel_btn_bound[PORT_FSBTN_COUNT];
/* the modal box (defined with the Files screen's exit prompt, further down:
 * that prompt is its first caller and the code came out of it) */
static void portDlgOpen(const char *title, const char *yes, const char *no, s32 def);
static void portSheetSeedDevices(void);
static s32 portSheetStyleMenu(s32 code);
static s32 portSheetStyleCode(s32 menu);
static s32 portDlgIsOpen(void);
static void portDlgClose(void);
static s32 portDlgTick(void);
static s32 portDlgSaidYes(void);
static Gfx *portDlgDraw(Gfx *DL);
static s32 port_filesel_options_selected = FALSE; /* OPTIONS clicked: menu change at the end of the tick */
#endif

struct mission_folder_setup mission_folder_setup_entries[] = {
  /* string_ptr  folder_text_preset                                                         icon_text_preset                                            stage_id           unknown type             mission_num briefing_name_ptr */
    {"1",        getStringID(LTITLE, TITLE_STR_120_ARK)            /* Arkangelsk */,        0,                                                          LEVELID_NONE,           0, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_121_DAM)            /* Dam */,               0,                                                          LEVELID_DAM,            0, MISSION_PART,             0, "UbriefdamZ"},
    {"ii",       getStringID(LTITLE, TITLE_STR_122_FAC)            /* Facility */,          0,                                                          LEVELID_FACILITY,       0, MISSION_PART,             1, "UbriefarkZ"},
    {"iii",      getStringID(LTITLE, TITLE_STR_123_RUN)            /* Runway */,            0,                                                          LEVELID_RUNWAY,         0, MISSION_PART,             2, "UbriefrunZ"},
    {"2",        getStringID(LTITLE, TITLE_STR_124_SEV)            /* Severnaya */,         0,                                                          LEVELID_NONE,           0, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_125_SURF)           /* Surface */,           0,                                                          LEVELID_SURFACE,        0, MISSION_PART,             3, "UbriefsevxZ"},
    {"ii",       getStringID(LTITLE, TITLE_STR_126_BUNK)           /* Bunker */,            0,                                                          LEVELID_BUNKER1,        0, MISSION_PART,             4, "UbriefsevbunkerZ"},
    {"3",        getStringID(LTITLE, TITLE_STR_127_KIRG)           /* Kirghizstan */,       0,                                                          LEVELID_NONE,           1, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_128_SILO4)          /* Launch Silo #4 */,    getStringID(LTITLE, TITLE_STR_129_SILO)     /* Silo */,     LEVELID_SILO,           1, MISSION_PART,             5, "UbriefsiloZ"},
    {"4",        getStringID(LTITLE, TITLE_STR_130_MONTE)          /* Monte Carlo */,       0,                                                          LEVELID_NONE,           1, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_131_FRIG)           /* Frigate */,           0,                                                          LEVELID_FRIGATE,        1, MISSION_PART,             6, "UbriefdestZ"},
    {"5",        getStringID(LTITLE, TITLE_STR_124_SEV)            /* Severnaya */,         0,                                                          LEVELID_NONE,           1, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_125_SURF)           /* Surface */,           0,                                                          LEVELID_SURFACE2,       1, MISSION_PART,             7, "UbriefsevxbZ"},
    {"ii",       getStringID(LTITLE, TITLE_STR_126_BUNK)           /* Bunker */,            0,                                                          LEVELID_BUNKER2,        1, MISSION_PART,             8, "UbriefsevbZ"},
    {"6",        getStringID(LTITLE, TITLE_STR_132_STPETER)        /* St. Petersburg */,    0,                                                          LEVELID_NONE,           2, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_133_STATPARK)       /* Statue Park */,       getStringID(LTITLE, TITLE_STR_134_STAT)     /* Statue */,   LEVELID_STATUE,         2, MISSION_PART,             9, "UbriefstatueZ"},
    {"ii",       getStringID(LTITLE, TITLE_STR_135_MILARCH)        /* Military Archives */, getStringID(LTITLE, TITLE_STR_136_ARCH)     /* Archives */, LEVELID_ARCHIVES,       2, MISSION_PART,            10, "UbriefarchZ"},
    {"iii",      getStringID(LTITLE, TITLE_STR_137_STREETS)        /* Streets */,           0,                                                          LEVELID_STREETS,        2, MISSION_PART,            11, "UbriefpeteZ"},
    {"iv",       getStringID(LTITLE, TITLE_STR_138_DEPOT)          /* Depot */,             0,                                                          LEVELID_DEPOT,          2, MISSION_PART,            12, "UbriefdepoZ"},
    {"v",        getStringID(LTITLE, TITLE_STR_139_TRAIN)          /* Train */,             0,                                                          LEVELID_TRAIN,          2, MISSION_PART,            13, "UbrieftraZ"},
    {"7",        getStringID(LTITLE, TITLE_STR_140_CUBA)           /* Cuba */,              0,                                                          LEVELID_NONE,           3, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_141_JUN)            /* Jungle */,            0,                                                          LEVELID_JUNGLE,         3, MISSION_PART,            14, "UbriefjunZ"},
    {"ii",       getStringID(LTITLE, TITLE_STR_142_CONCENTER)      /* Control Center */,    getStringID(LTITLE, TITLE_STR_143_CON)      /* Control */,  LEVELID_CONTROL,        3, MISSION_PART,            15, "UbriefcontrolZ"},
    {"iii",      getStringID(LTITLE, TITLE_STR_144_WATERCAV)       /* Water Caverns */,     getStringID(LTITLE, TITLE_STR_145_CAV)      /* Caverns */,  LEVELID_CAVERNS,        3, MISSION_PART,            16, "UbriefcaveZ"},
    {"iv",       getStringID(LTITLE, TITLE_STR_146_ANTENNA)        /* Antenna Cradle */,    getStringID(LTITLE, TITLE_STR_147_CRADLE)   /* Cradle */,   LEVELID_CRADLE,         3, MISSION_PART,            17, "UbriefcradZ"},
    {"8",        getStringID(LTITLE, TITLE_STR_148_TEOTIHUACA)     /* Teotihuaca'n */,      0,                                                          LEVELID_NONE,           4, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_149_AZTECCOMPLEX)   /* Aztec Complex */,     getStringID(LTITLE, TITLE_STR_150_AZTEC)    /* Aztec */,    LEVELID_AZTEC,          4, MISSION_PART,            18, "UbriefaztZ"},
    {"9",        getStringID(LTITLE, TITLE_STR_151_ELSAGHIRA)      /* el-Saghira */,        0,                                                          LEVELID_NONE,           4, MISSION_HEADER,          -1, 0},
    {"i",        getStringID(LTITLE, TITLE_STR_152_EGYPTIANTEMPLE) /* Egyptian Temple */,   getStringID(LTITLE, TITLE_STR_153_EGYPTIAN) /* Egyptian */, LEVELID_EGYPT,          4, MISSION_PART,            19, "UbriefcrypZ"},
    {NULL,       0,                                                                         0,                                                          LEVELID_NONE,          -1, MISSION_PART,            -1, 0}
};

struct FolderSelectColour unknown_folderselect_constructor = { 0x14, 0x14, 0x14 };
struct FolderSelectColour unknown_folderselect_constructor_0 = { 0x32, 0x32, 0x32 };


ModelRenderData     unknown_folderselect = {NULL,
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

ModelRenderData       D_8002AF84           = {NULL,
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


f32 D_8002AFC4 = 0;
f32 D_8002AFC8 = 190.0;
f32 D_8002AFCC = -3300.0;
s32 cursor_xpos_table_mission_select[] = {73, 142, 212, 282, 352};

#if defined(VERSION_EU)
s32 cursor_ypos_table_mission_select[] = {62, 130, 201, 270};
#else
s32 cursor_ypos_table_mission_select[] = {62, 131, 201, 270};
#endif

struct MP_game_length_settings multi_game_lengths[] = {
    {getStringID(LTITLE, TITLE_STR_45_UNLIMITED), 0, 0, 0},                    /* unlimited */
    {getStringID(LTITLE, TITLE_STR_46_5MIN), 0, MINS_TO_TIMER60(5), 0},   /* 5 minutes */
    {getStringID(LTITLE, TITLE_STR_47_10MIN), 0, MINS_TO_TIMER60(10), 0},  /* 10 minutes */
    {getStringID(LTITLE, TITLE_STR_48_20MIN), 0, MINS_TO_TIMER60(20), 0},  /* 20 minutes */
    {getStringID(LTITLE, TITLE_STR_49_5PT), 0, 0, 5},                    /* first to 5 points */
    {getStringID(LTITLE, TITLE_STR_50_10PT), 0, 0, 10},                   /* first to 10 points */
    {getStringID(LTITLE, TITLE_STR_51_20PT), 0, 0, 20},                   /* first to 20 points */
    {getStringID(LTITLE, TITLE_STR_52_LASTALIVE), 0, 0, 0}                     /* last person alive wins */
};

struct mp_stage_playercount mp_player_counts[] = {
    {getStringID(LTITLE, TITLE_STR_53_NORMAL), 2, 4}, //"Normal"
    {getStringID(LTITLE, TITLE_STR_54_YOLT), 2, 4}, //"You Only Live Twice"
    {getStringID(LTITLE, TITLE_STR_55_FLAGTAG), 2, 4}, //"The Living Daylights [Flag Tag]"
    {getStringID(LTITLE, TITLE_STR_56_GG), 2, 4}, //"The Man With the Golden Gun"
    {getStringID(LTITLE, TITLE_STR_57_LTK), 2, 4}, //"License to Kill"
    {getStringID(LTITLE, TITLE_STR_58_2V2), 4, 4}, //"Team: 2 vs 2"
    {getStringID(LTITLE, TITLE_STR_59_3V1), 4, 4}, //"Team: 3 vs 1"
    {getStringID(LTITLE, TITLE_STR_60_2V1), 3, 3}  //"Team: 2 vs 1"
};

struct mp_stage_setup multi_stage_setups[] = {
    /* unlocked by default */
    {getStringID(LTITLE, TITLE_STR_154_RANDOM), getStringID(LTITLE, TITLE_STR_155_RANDOM2), IMG_MP_RANDOM, LEVELID_NONE, -1, 1, 4},
    {getStringID(LTITLE, TITLE_STR_156_TEMPLE), getStringID(LTITLE, TITLE_STR_157_TEMPLE2), IMG_MP_TEMPLE, LEVELID_TEMPLE, -1, 1, 4},
    {getStringID(LTITLE, TITLE_STR_158_COMPLEX), getStringID(LTITLE, TITLE_STR_159_COMPLEX2), IMG_MP_COMPLEX, LEVELID_COMPLEX, -1, 1, 4},
    {getStringID(LTITLE, TITLE_STR_162_CAVES), getStringID(LTITLE, TITLE_STR_163_CAVES2), IMG_MP_CAVES, LEVELID_CAVES, -1, 1, 4},
    {getStringID(LTITLE, TITLE_STR_160_LIBRARY), getStringID(LTITLE, TITLE_STR_161_LIBRARY2), IMG_MP_BASEMENT, LEVELID_LIBRARY, -1, 1, 4},
    {getStringID(LTITLE, TITLE_STR_269_BASEMENT), getStringID(LTITLE, TITLE_STR_270_BASEMENT2), IMG_MP_BASEMENT, LEVELID_BASEMENT, -1, 1, 4},
    {getStringID(LTITLE, TITLE_STR_271_STACK), getStringID(LTITLE, TITLE_STR_272_STACK2), IMG_MP_BASEMENT, LEVELID_STACK, -1, 1, 4},
    /* unlocked on level progression */
    {getStringID(LTITLE, TITLE_STR_164_FACMP), getStringID(LTITLE, TITLE_STR_165_FACMP22), IMG_MP_FACILITY, LEVELID_FACILITY, SP_LEVEL_FACILITY, 1, 4},
    {getStringID(LTITLE, TITLE_STR_166_SEVERNAYABUNKER), getStringID(LTITLE, TITLE_STR_167_BUNKER), IMG_MP_BUNKER2, LEVELID_BUNKER2, SP_LEVEL_BUNKER2, 1, 3},
    {getStringID(LTITLE, TITLE_STR_168_MILITARYARCHIVES), getStringID(LTITLE, TITLE_STR_169_ARCHIVES), IMG_MP_ARCHIVES, LEVELID_ARCHIVES, SP_LEVEL_ARCHIVES, 1, 3},
    {getStringID(LTITLE, TITLE_STR_170_WATERCAVERNS), getStringID(LTITLE, TITLE_STR_171_CAVERNS), IMG_MP_CAVERNS, LEVELID_CAVERNS, SP_LEVEL_CAVERNS, 1, 3},
    {getStringID(LTITLE, TITLE_STR_172_EGYPTIANTEMPLEMP), getStringID(LTITLE, TITLE_STR_173_EGYPTIANMP), IMG_MP_EGYPT, LEVELID_EGYPT, SP_LEVEL_EGYPT, 1, 2}
#ifndef TARGET_N64
    /* PORT: the cut rows below that work (Ump_setupstatueZ / Ump_setupcradZ are
     * in the ROM), named with the solo strings; Rare's MP strings for them are
     * the placeholders "stat" / "crad" */
    ,
    {getStringID(LTITLE, TITLE_STR_134_STAT), getStringID(LTITLE, TITLE_STR_134_STAT), IMG_MP_STATUE, LEVELID_STATUE, SP_LEVEL_STATUE, 1, 2},
    {getStringID(LTITLE, TITLE_STR_147_CRADLE), getStringID(LTITLE, TITLE_STR_147_CRADLE), IMG_MP_CRADLE, LEVELID_CRADLE, SP_LEVEL_CRADLE, 1, 2}
#endif
  //{getStringID(LTITLE, TITLE_STR_174_CITADEL), getStringID(LTITLE, TITLE_STR_175_CITADEL2), IMG_MP_RANDOM, LEVELID_CITADEL, -1, 1, 4}, //Citadel (old format setup)
  //{getStringID(LTITLE, TITLE_STR_176_DEST), getStringID(LTITLE, TITLE_STR_177_DEST2), IMG_MP_FRIGATE, LEVELID_FRIGATE, SP_LEVEL_FRIGATE, 1, 4}, //dest (has xbla setup)
  //{getStringID(LTITLE, TITLE_STR_178_STAT), getStringID(LTITLE, TITLE_STR_179_STAT2), IMG_MP_STATUE, LEVELID_STATUE, SP_LEVEL_STATUE, 1, 2}, //stat (works)
  //{getStringID(LTITLE, TITLE_STR_180_CRAD), getStringID(LTITLE, TITLE_STR_181_CRADLE2), IMG_MP_CRADLE, LEVELID_CRADLE, SP_LEVEL_CRADLE, 1, 2}, //crad (works)
  //{getStringID(LTITLE, TITLE_STR_182_AZT), getStringID(LTITLE, TITLE_STR_183_AZT2), IMG_MP_AZTEC, LEVELID_AZTEC, SP_LEVEL_AZTEC, 1, 4}, //azt (needs setup)
  //{getStringID(LTITLE, TITLE_STR_123_RUN), getStringID(LTITLE, TITLE_STR_123_RUN), IMG_MP_RUNWAY, LEVELID_RUNWAY, SP_LEVEL_RUN, 1, 4}, //runway (xbla setup)
  //{getStringID(LTITLE, TITLE_STR_121_DAM), getStringID(LTITLE, TITLE_STR_121_DAM), IMG_MP_DAM, LEVELID_DAM, SP_LEVEL_DAM, 1, 4}, //dam (xbla setup)
  //{getStringID(LTITLE, TITLE_STR_138_DEPOT), getStringID(LTITLE, TITLE_STR_138_DEPOT), IMG_MP_DEPOT, LEVELID_DEPOT, SP_LEVEL_DEPOT, 1, 4}, //depot (xbla setup)
};

#ifndef TARGET_N64
/* PORT, hard-coded: every multiplayer character is unlocked regardless of the
 * save, using 0x40, the value the unlock-all code sets (unlock_all_mp_chars),
 * which the character screen's progress check never counts back down. */
s32 num_chars_selectable_mp = PORT_MP_CHAR_COUNT;
#else
s32 num_chars_selectable_mp = 8;
/* PORT: the pickers show characters in a port order (portMpCharAt); on N64 the
 * shown order is the stored order */
#define portMpCharAt(pos) (pos)
#define portMpCharPos(c) (c)
#endif

#ifdef BUGFIX_R0
struct MP_selectable_chars mp_chr_setup[] = {
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Brosnan_Tuxedo,                   HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_191_NATALYA), FEMALE, IMG_MPC_NATALYA, BODY_Natalya_Skirt,            HEAD_Male_Brosnan_Tuxedo, 0.96609998},
    {getStringID(LTITLE, TITLE_STR_188_TREVELYAN), MALE,   IMG_MPC_TREVELYAN, BODY_Trevelyan_Janus,          HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_190_XENIA), FEMALE, IMG_MPC_XENIA, BODY_Xenia,                    HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_187_OURUMOV), MALE,   IMG_MPC_OURUMOV, BODY_Ourumov,                  HEAD_Male_Brosnan_Tuxedo, 1.0778},
    {getStringID(LTITLE, TITLE_STR_186_BORIS), MALE,   IMG_MPC_BORIS, BODY_Boris,                    HEAD_Male_Brosnan_Tuxedo, 0.9702},
    {getStringID(LTITLE, TITLE_STR_189_VALENTIN), MALE,   IMG_MPC_VALENTIN, BODY_Valentin_,                HEAD_Male_Brosnan_Tuxedo, 0.93239999},
    {getStringID(LTITLE, TITLE_STR_185_MISHKIN), MALE,   IMG_MPC_MISHKIN, BODY_Siberian_Guard_1_Mishkin, HEAD_Male_Mishkin, 1.0},
    {getStringID(LTITLE, TITLE_STR_194_MAYDAY), FEMALE, IMG_MPC_MAYDAY, BODY_Mayday,                   HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_193_JAWS), MALE,   IMG_MPC_JAWS, BODY_Jaws,                     HEAD_Male_Brosnan_Tuxedo, 1.199},
    {getStringID(LTITLE, TITLE_STR_195_ODDJOB), MALE,   IMG_MPC_ODDJOB, BODY_Oddjob,                   HEAD_Male_Brosnan_Tuxedo,0.78780001},
    {getStringID(LTITLE, TITLE_STR_192_BARONSAMEDI), MALE,   IMG_MPC_BARON, BODY_Baron_Samedi,             HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_253_RUSSIANSOLDIER), MALE,   IMG_MPC_RANDOM, BODY_Russian_Soldier,          HEAD_Male_Mark, 1.0},
    {getStringID(LTITLE, TITLE_STR_252_RUSSIANINFANTRY), MALE,   IMG_MPC_RANDOM, BODY_Russian_Infantry,         HEAD_Male_Karl, 1.0},
    {getStringID(LTITLE, TITLE_STR_263_SCIENTIST), MALE,   IMG_MPC_RANDOM, BODY_Scientist_1_Male,         HEAD_Male_Dave_Dr_Doak, 1.0},
    {getStringID(LTITLE, TITLE_STR_263_SCIENTIST), FEMALE, IMG_MPC_RANDOM, BODY_Scientist_2_Female,       HEAD_Female_Sally, 0.91},
    {getStringID(LTITLE, TITLE_STR_256_RUSSIANCOMMANDANT), MALE,   IMG_MPC_RANDOM, BODY_Russian_Commandant,       HEAD_Male_Martin, 1.0},
    {getStringID(LTITLE, TITLE_STR_254_JANUSMARINE), MALE,   IMG_MPC_RANDOM, BODY_Janus_Marine,             HEAD_Male_Steve_Ellis, 1.0},
    {getStringID(LTITLE, TITLE_STR_257_NAVALOFFICER), MALE,   IMG_MPC_RANDOM, BODY_Naval_Officer,            HEAD_Male_Duncan, 1.0},
    {getStringID(LTITLE, TITLE_STR_262_HELICOPTERPILOT), MALE,   IMG_MPC_RANDOM, BODY_Helicopter_Pilot,         HEAD_Male_Pete, 1.0},
    {getStringID(LTITLE, TITLE_STR_251_STPETERSBURGGUARD), MALE,   IMG_MPC_RANDOM, BODY_St_Petersburg_Guard,      HEAD_Male_Ken, 1.0},
    {getStringID(LTITLE, TITLE_STR_264_CIVILIAN), FEMALE, IMG_MPC_RANDOM, BODY_Civilian_1_Female,        HEAD_Female_Marion_Rosika, 0.9},
    {getStringID(LTITLE, TITLE_STR_264_CIVILIAN), MALE,   IMG_MPC_RANDOM, BODY_Civilian_2,               HEAD_Male_Graeme, 1.0},
    {getStringID(LTITLE, TITLE_STR_264_CIVILIAN), MALE,   IMG_MPC_RANDOM, BODY_Civilian_3,               HEAD_Male_Grant, 1.0},
    {getStringID(LTITLE, TITLE_STR_264_CIVILIAN), MALE,   IMG_MPC_RANDOM, BODY_Civilian_4,               HEAD_Male_Dwayne, 1.0},
    {getStringID(LTITLE, TITLE_STR_258_SIBERIANGUARD), MALE,   IMG_MPC_RANDOM, BODY_Siberian_Guard_1_Mishkin, HEAD_Male_Lee, 1.0},
    {getStringID(LTITLE, TITLE_STR_259_ARCTICCOMMANDO), MALE,   IMG_MPC_RANDOM, BODY_Arctic_Commando,          HEAD_Male_Chris, 1.0},
    {getStringID(LTITLE, TITLE_STR_258_SIBERIANGUARD), MALE,   IMG_MPC_RANDOM, BODY_Siberian_Guard_2,         HEAD_Male_Scott, 1.0},
    {getStringID(LTITLE, TITLE_STR_260_SIBERIANSPECIALFORCES), MALE,   IMG_MPC_RANDOM, BODY_Siberian_Special_Forces,  HEAD_Male_Alan, 1.0},
    {getStringID(LTITLE, TITLE_STR_250_JUNGLECOMMANDO), MALE,   IMG_MPC_RANDOM, BODY_Jungle_Commando,          HEAD_Male_Joel, 1.0},
    {getStringID(LTITLE, TITLE_STR_255_JANUSSPECIALFORCES), MALE,   IMG_MPC_RANDOM, BODY_Janus_Special_Forces,     HEAD_Male_B, 1.0},
    {getStringID(LTITLE, TITLE_STR_261_MOOKRAKERELITE), MALE,   IMG_MPC_RANDOM, BODY_Moonraker_Elite_1_Male,   HEAD_Male_Neil, 1.0},
    {getStringID(LTITLE, TITLE_STR_261_MOOKRAKERELITE), FEMALE, IMG_MPC_RANDOM, BODY_Moonraker_Elite_2_Female, HEAD_Female_Vivien, 0.88},
    {getStringID(LTITLE, TITLE_STR_196_ROSIKA), FEMALE, IMG_MPC_RANDOM, BODY_Rosika,                   HEAD_Female_Marion_Rosika, 0.88529998},
    {getStringID(LTITLE, TITLE_STR_197_KARL), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Karl, 1.0446},
    {getStringID(LTITLE, TITLE_STR_198_MARTIN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Martin, 1.0446},
    {getStringID(LTITLE, TITLE_STR_199_MARK), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Mark, 1.0446},
    {getStringID(LTITLE, TITLE_STR_200_DAVE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Dave_Dr_Doak, 1.0446},
    {getStringID(LTITLE, TITLE_STR_201_DUNCAN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Duncan, 1.0446},
    {getStringID(LTITLE, TITLE_STR_202_B), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_B, 1.0446},
    {getStringID(LTITLE, TITLE_STR_203_STEVEE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Steve_Ellis, 1.0446},
    {getStringID(LTITLE, TITLE_STR_204_GRANT), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Grant, 1.0446},
    {getStringID(LTITLE, TITLE_STR_205_GRAEME), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Graeme, 1.0446},
    {getStringID(LTITLE, TITLE_STR_206_KEN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Ken, 1.0446},
    {getStringID(LTITLE, TITLE_STR_207_ALAN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Alan, 1.0446},
    {getStringID(LTITLE, TITLE_STR_208_PETE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Pete, 1.0446},
    {getStringID(LTITLE, TITLE_STR_209_SHAUN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Shaun, 1.0446},
    {getStringID(LTITLE, TITLE_STR_210_DWAYNE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Dwayne, 1.0446},
    {getStringID(LTITLE, TITLE_STR_211_DES), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Des, 1.0446},
    {getStringID(LTITLE, TITLE_STR_212_CHRIS), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Chris, 1.0446},
    {getStringID(LTITLE, TITLE_STR_213_LEE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Lee, 1.0446},
    {getStringID(LTITLE, TITLE_STR_214_NEIL), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Neil, 1.0446},
    {getStringID(LTITLE, TITLE_STR_215_JIM), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Jim, 1.0446},
    {getStringID(LTITLE, TITLE_STR_216_ROBIN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Robin, 1.0446},
    {getStringID(LTITLE, TITLE_STR_217_STEVEH), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Steve_H, 1.0446},
    {getStringID(LTITLE, TITLE_STR_218_TERRORIST), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Terrorist, 1.0446},
    {getStringID(LTITLE, TITLE_STR_219_BIKER), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Biker, 1.0446},
    {getStringID(LTITLE, TITLE_STR_220_JOEL), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Joel, 1.0446},
    {getStringID(LTITLE, TITLE_STR_221_SCOTT), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Scott, 1.0446},
    {getStringID(LTITLE, TITLE_STR_222_JOE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Joe, 1.0446},
    {getStringID(LTITLE, TITLE_STR_223_SALLY), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Sally, 1.0446},
    {getStringID(LTITLE, TITLE_STR_224_MARION), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Marion_Rosika, 1.0446},
    {getStringID(LTITLE, TITLE_STR_225_MANDY), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Mandy, 1.0446},
    {getStringID(LTITLE, TITLE_STR_226_VIVIEN), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Vivien, 1.0446}
#ifndef TARGET_N64
    /* PORT: the cast's other outfits, appended so saved character numbers
     * keep their meaning. Names come from portMpCharName. */
    ,
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Special_Operations_Uniform,       HEAD_Male_Brosnan_Boiler, 1.0},
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Formal_Wear,                      HEAD_Male_Brosnan_Default, 1.0},
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Jungle_Fatigues,                  HEAD_Male_Brosnan_Jungle, 1.0},
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Parka,                            HEAD_Male_Brosnan_Default, 1.0},
    {getStringID(LTITLE, TITLE_STR_188_TREVELYAN), MALE, IMG_MPC_TREVELYAN, BODY_Trevelyan_006,               HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_191_NATALYA), FEMALE, IMG_MPC_NATALYA, BODY_Natalya_Jungle_Fatigues,       HEAD_Male_Brosnan_Tuxedo, 0.96609998}
#endif
};
#else
struct MP_selectable_chars mp_chr_setup[] = {
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Brosnan_Tuxedo,                   HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_191_NATALYA), FEMALE, IMG_MPC_NATALYA, BODY_Natalya_Skirt,            HEAD_Male_Brosnan_Tuxedo, 0.96609998},
    {getStringID(LTITLE, TITLE_STR_188_TREVELYAN), MALE,   IMG_MPC_TREVELYAN, BODY_Trevelyan_Janus,          HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_190_XENIA), FEMALE, IMG_MPC_XENIA, BODY_Xenia,                    HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_187_OURUMOV), MALE,   IMG_MPC_OURUMOV, BODY_Ourumov,                  HEAD_Male_Brosnan_Tuxedo, 1.0778},
    {getStringID(LTITLE, TITLE_STR_186_BORIS), MALE,   IMG_MPC_BORIS, BODY_Boris,                    HEAD_Male_Brosnan_Tuxedo, 0.9702},
    {getStringID(LTITLE, TITLE_STR_189_VALENTIN), MALE,   IMG_MPC_VALENTIN, BODY_Valentin_,                HEAD_Male_Brosnan_Tuxedo, 0.93239999},
    {getStringID(LTITLE, TITLE_STR_185_MISHKIN), MALE,   IMG_MPC_MISHKIN, BODY_Siberian_Guard_1_Mishkin, HEAD_Male_Mishkin, 1.0},
    {getStringID(LTITLE, TITLE_STR_194_MAYDAY), FEMALE, IMG_MPC_MAYDAY, BODY_Mayday,                   HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_193_JAWS), MALE,   IMG_MPC_JAWS, BODY_Jaws,                     HEAD_Male_Brosnan_Tuxedo, 1.199},
    {getStringID(LTITLE, TITLE_STR_195_ODDJOB), MALE,   IMG_MPC_ODDJOB, BODY_Oddjob,                   HEAD_Male_Brosnan_Tuxedo,0.78780001},
    {getStringID(LTITLE, TITLE_STR_192_BARONSAMEDI), MALE,   IMG_MPC_BARON, BODY_Baron_Samedi,             HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_287_RUSSIANSOLDIER), MALE,   IMG_MPC_RANDOM, BODY_Russian_Soldier,          HEAD_Male_Mark, 1.0},
    {getStringID(LTITLE, TITLE_STR_288_RUSSIANINFANTRY), MALE,   IMG_MPC_RANDOM, BODY_Russian_Infantry,         HEAD_Male_Karl, 1.0},
    {getStringID(LTITLE, TITLE_STR_289_SCIENTIST), MALE,   IMG_MPC_RANDOM, BODY_Scientist_1_Male,         HEAD_Male_Dave_Dr_Doak, 1.0},
    {getStringID(LTITLE, TITLE_STR_289_SCIENTIST), FEMALE, IMG_MPC_RANDOM, BODY_Scientist_2_Female,       HEAD_Female_Sally, 0.91},
    {getStringID(LTITLE, TITLE_STR_290_RUSSIANCOMMANDANT), MALE,   IMG_MPC_RANDOM, BODY_Russian_Commandant,       HEAD_Male_Martin, 1.0},
    {getStringID(LTITLE, TITLE_STR_291_JANUSMARINE), MALE,   IMG_MPC_RANDOM, BODY_Janus_Marine,             HEAD_Male_Steve_Ellis, 1.0},
    {getStringID(LTITLE, TITLE_STR_292_NAVALOFFICER), MALE,   IMG_MPC_RANDOM, BODY_Naval_Officer,            HEAD_Male_Duncan, 1.0},
    {getStringID(LTITLE, TITLE_STR_293_HELICOPTERPILOT), MALE,   IMG_MPC_RANDOM, BODY_Helicopter_Pilot,         HEAD_Male_Pete, 1.0},
    {getStringID(LTITLE, TITLE_STR_294_STPETERSBURGARD), MALE,   IMG_MPC_RANDOM, BODY_St_Petersburg_Guard,      HEAD_Male_Ken, 1.0},
    {getStringID(LTITLE, TITLE_STR_295_CIVILIAN), FEMALE, IMG_MPC_RANDOM, BODY_Civilian_1_Female,        HEAD_Female_Marion_Rosika, 0.9},
    {getStringID(LTITLE, TITLE_STR_295_CIVILIAN), MALE,   IMG_MPC_RANDOM, BODY_Civilian_2,               HEAD_Male_Graeme, 1.0},
    {getStringID(LTITLE, TITLE_STR_295_CIVILIAN), MALE,   IMG_MPC_RANDOM, BODY_Civilian_3,               HEAD_Male_Grant, 1.0},
    {getStringID(LTITLE, TITLE_STR_295_CIVILIAN), MALE,   IMG_MPC_RANDOM, BODY_Civilian_4,               HEAD_Male_Dwayne, 1.0},
    {getStringID(LTITLE, TITLE_STR_296_SIBERIANGUARD), MALE,   IMG_MPC_RANDOM, BODY_Siberian_Guard_1_Mishkin, HEAD_Male_Lee, 1.0},
    {getStringID(LTITLE, TITLE_STR_297_ARCTICCOMMANDO), MALE,   IMG_MPC_RANDOM, BODY_Arctic_Commando,          HEAD_Male_Chris, 1.0},
    {getStringID(LTITLE, TITLE_STR_296_SIBERIANGUARD), MALE,   IMG_MPC_RANDOM, BODY_Siberian_Guard_2,         HEAD_Male_Scott, 1.0},
    {getStringID(LTITLE, TITLE_STR_298_SIBERIANSPECIALFORCES), MALE,   IMG_MPC_RANDOM, BODY_Siberian_Special_Forces,  HEAD_Male_Alan, 1.0},
    {getStringID(LTITLE, TITLE_STR_299_JUNGLECOMMANDO), MALE,   IMG_MPC_RANDOM, BODY_Jungle_Commando,          HEAD_Male_Joel, 1.0},
    {getStringID(LTITLE, TITLE_STR_300_JANUSSPECIALFORCES), MALE,   IMG_MPC_RANDOM, BODY_Janus_Special_Forces,     HEAD_Male_B, 1.0},
    {getStringID(LTITLE, TITLE_STR_301_MOONRAKERELITE), MALE,   IMG_MPC_RANDOM, BODY_Moonraker_Elite_1_Male,   HEAD_Male_Neil, 1.0},
    {getStringID(LTITLE, TITLE_STR_301_MOONRAKERELITE), FEMALE, IMG_MPC_RANDOM, BODY_Moonraker_Elite_2_Female, HEAD_Female_Vivien, 0.88},
    {getStringID(LTITLE, TITLE_STR_196_ROSIKA), FEMALE, IMG_MPC_RANDOM, BODY_Rosika,                   HEAD_Female_Marion_Rosika, 0.88529998},
    {getStringID(LTITLE, TITLE_STR_197_KARL), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Karl, 1.0446},
    {getStringID(LTITLE, TITLE_STR_198_MARTIN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Martin, 1.0446},
    {getStringID(LTITLE, TITLE_STR_199_MARK), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Mark, 1.0446},
    {getStringID(LTITLE, TITLE_STR_200_DAVE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Dave_Dr_Doak, 1.0446},
    {getStringID(LTITLE, TITLE_STR_201_DUNCAN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Duncan, 1.0446},
    {getStringID(LTITLE, TITLE_STR_202_B), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_B, 1.0446},
    {getStringID(LTITLE, TITLE_STR_203_STEVEE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Steve_Ellis, 1.0446},
    {getStringID(LTITLE, TITLE_STR_204_GRANT), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Grant, 1.0446},
    {getStringID(LTITLE, TITLE_STR_205_GRAEME), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Graeme, 1.0446},
    {getStringID(LTITLE, TITLE_STR_206_KEN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Ken, 1.0446},
    {getStringID(LTITLE, TITLE_STR_207_ALAN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Alan, 1.0446},
    {getStringID(LTITLE, TITLE_STR_208_PETE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Pete, 1.0446},
    {getStringID(LTITLE, TITLE_STR_209_SHAUN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Shaun, 1.0446},
    {getStringID(LTITLE, TITLE_STR_210_DWAYNE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Dwayne, 1.0446},
    {getStringID(LTITLE, TITLE_STR_211_DES), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Des, 1.0446},
    {getStringID(LTITLE, TITLE_STR_212_CHRIS), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Chris, 1.0446},
    {getStringID(LTITLE, TITLE_STR_213_LEE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Lee, 1.0446},
    {getStringID(LTITLE, TITLE_STR_214_NEIL), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Neil, 1.0446},
    {getStringID(LTITLE, TITLE_STR_215_JIM), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Jim, 1.0446},
    {getStringID(LTITLE, TITLE_STR_216_ROBIN), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Robin, 1.0446},
    {getStringID(LTITLE, TITLE_STR_217_STEVEH), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Steve_H, 1.0446},
    {getStringID(LTITLE, TITLE_STR_218_TERRORIST), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Terrorist, 1.0446},
    {getStringID(LTITLE, TITLE_STR_219_BIKER), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Biker, 1.0446},
    {getStringID(LTITLE, TITLE_STR_220_JOEL), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Joel, 1.0446},
    {getStringID(LTITLE, TITLE_STR_221_SCOTT), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Scott, 1.0446},
    {getStringID(LTITLE, TITLE_STR_222_JOE), MALE,   IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Male_Joe, 1.0446},
    {getStringID(LTITLE, TITLE_STR_223_SALLY), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Sally, 1.0446},
    {getStringID(LTITLE, TITLE_STR_224_MARION), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Marion_Rosika, 1.0446},
    {getStringID(LTITLE, TITLE_STR_225_MANDY), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Mandy, 1.0446},
    {getStringID(LTITLE, TITLE_STR_226_VIVIEN), FEMALE, IMG_MPC_RANDOM, BODY_Brosnan_Tuxedo,                   HEAD_Female_Vivien, 1.0446}
#ifndef TARGET_N64
    /* PORT: the cast's other outfits, appended so saved character numbers
     * keep their meaning. Names come from portMpCharName. */
    ,
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Special_Operations_Uniform,       HEAD_Male_Brosnan_Boiler, 1.0},
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Formal_Wear,                      HEAD_Male_Brosnan_Default, 1.0},
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Jungle_Fatigues,                  HEAD_Male_Brosnan_Jungle, 1.0},
    {getStringID(LTITLE, TITLE_STR_184_BOND), MALE,   IMG_MPC_BROSNAN, BODY_Parka,                            HEAD_Male_Brosnan_Default, 1.0},
    {getStringID(LTITLE, TITLE_STR_188_TREVELYAN), MALE, IMG_MPC_TREVELYAN, BODY_Trevelyan_006,               HEAD_Male_Brosnan_Tuxedo, 1.0},
    {getStringID(LTITLE, TITLE_STR_191_NATALYA), FEMALE, IMG_MPC_NATALYA, BODY_Natalya_Jungle_Fatigues,       HEAD_Male_Brosnan_Tuxedo, 0.96609998}
#endif
};
#endif


struct MP_handicap_menu MP_handicap_table[] = {
    {getStringID(LTITLE, TITLE_STR_61_HERO), 0, 10.0},       // "Health -10\n(Hero)"
    {getStringID(LTITLE, TITLE_STR_62_VET4), 0, 2.8560996},  // "Health -4\n(Veteran)"
    {getStringID(LTITLE, TITLE_STR_63_VET3), 0, 2.1969998},  // "Health -3\n(Veteran)"
    {getStringID(LTITLE, TITLE_STR_64_VET2), 0, 1.6899998},  // "Health -2\n(Veteran)"
    {getStringID(LTITLE, TITLE_STR_65_VET1), 0, 1.3},        // "Health -1\n(Veteran)"
    {getStringID(LTITLE, TITLE_STR_66_HEALTH_NORMAL), 0, 1.0},        // "Health +0\n(Normal)"
    {getStringID(LTITLE, TITLE_STR_67_NOVICE1), 0, 0.76923078}, // "Health +1\n(Novice)"
    {getStringID(LTITLE, TITLE_STR_68_NOVICE2), 0, 0.59171599}, // "Health +2\n(Novice)"
    {getStringID(LTITLE, TITLE_STR_69_NOVICE3), 0, 0.45516616}, // "Health +3\n(Novice)"
    {getStringID(LTITLE, TITLE_STR_70_NOVICE4), 0, 0.35012782}, // "Health +4\n(Novice)"
    {getStringID(LTITLE, TITLE_STR_71_ROOKIE), 0, 0.1}         // "Health +10\n(Rookie)"
};

struct MP_controller_configuration_menu MP_controller_configuration_table[] = {
    {getStringID(LTITLE, TITLE_STR_277_11HONEY), 0, 1}, //1.1 Honey*
    {getStringID(LTITLE, TITLE_STR_278_12SOLITAIRE), 1, 1}, //1.2 Solitaire*
    {getStringID(LTITLE, TITLE_STR_279_13KISSY), 2, 1}, //1.3 Kissy*
    {getStringID(LTITLE, TITLE_STR_280_14GOODNIGHT), 3, 1}, //1.4 Goodnight*
    {getStringID(LTITLE, TITLE_STR_281_21PLENTY), 4, 2}, //2.1 Plenty*
    {getStringID(LTITLE, TITLE_STR_282_22GALORE), 5, 2}, //2.2 Galore*
    {getStringID(LTITLE, TITLE_STR_283_23DOMINO), 6, 2}, //2.3 Domino*
    {getStringID(LTITLE, TITLE_STR_284_24GOODHEAD), 7, 2}  //2.4 Goodhead*
};

struct MP_sight_aim_settings mp_sight_adjust_table[] = {
    {getStringID(LTITLE, TITLE_STR_72_SOFFAOFF), 0, 0}, // "Sight OFF, Auto Aim OFF"
    {getStringID(LTITLE, TITLE_STR_73_SONAOFF), 1, 0}, // "Sight ON, Auto Aim OFF"
    {getStringID(LTITLE, TITLE_STR_74_SOFFAON), 0, 1}, // "Sight OFF, Auto Aim ON"
    {getStringID(LTITLE, TITLE_STR_75_SONAON), 1, 1}  // "Sight ON, Auto Aim ON"
};

s32 selected_num_players = 0;
s32 player_char[MAX_PLAYER_COUNT] = { -1, -1, -1, -1 };
s32 MP_stage_selected = MP_STAGE_TEMPLE;
s32 game_length = LEN_10MIN; //10minutes
s32 aim_sight_adjustment = 3; //"Sight ON, Auto Aim ON"
s32 scenario = SCENARIO_NORMAL;

s32 unlock_stage_select = TRUE;
s32 unlock_game_length = TRUE;
s32 unlock_chars = TRUE;
s32 unlock_weapon_select = TRUE;
s32 unlock_handicap = TRUE;
s32 unlock_control_style = TRUE;
s32 unlock_aim_sight = TRUE;

u32 D_8002B560 = 0;

s16 solo_target_time_array[20][3] = {
    {0, 160, 0}, // Dam
    {0, 0, 125}, // Facility
    {300, 0, 0}, // Runway
    {0, 210, 0}, // Surface 1
    {0, 0, 240}, // Bunker 1
    {180, 0, 0}, // Silo
    {0, 270, 0}, // Frigate
    {0, 0, 255}, // Surface 2
    {90, 0, 0},  // Bunker 2
    {0, 195, 0}, // Statue
    {0, 0, 80},  // Archives
    {105, 0, 0}, // Streets
    {0, 100, 0}, // Depot
    {0, 0, 325}, // Train
    {225, 0, 0}, // Jungle
    {0, 600, 0}, // Control
    {0, 0, 570}, // Caverns
    {135, 0, 0}, // Cradle
    {0, 540, 0}, // Aztec
    {0, 0, 360}  // Egyptian
};

s32 totalunlockedcheats = 0;
s32 cheathighlighted = 0; // 0x8002B5E0


u32 MP_menu_selected_option = 0;
s32 intro_character_index = 0;
u32 randomly_selected_intro_animation = ANIM_idle;
u32 intro_animation_count = 0;
struct Model *cast_model = NULL;
struct Model *cast_model_weapon = NULL;
u32 full_actor_intro = 0;

struct intro_char intro_char_table[] = {
    {BODY_Brosnan_Tuxedo,             HEAD_Male_Brosnan_Tuxedo, getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_228_THEACTORS),             getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Special_Operations_Uniform, HEAD_Male_Brosnan_Boiler, getStringID(LTITLE, TITLE_STR_229_STARRING),      getStringID(LTITLE, TITLE_STR_232_007),                   getStringID(LTITLE, TITLE_STR_233_JAMESBOND),        0, 0},
    {BODY_Natalya_Skirt,              HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_229_STARRING),      getStringID(LTITLE, TITLE_STR_234_NATALYASIMONOVA),       getStringID(LTITLE, TITLE_STR_227_LF),               0, 0},
    {BODY_Trevelyan_006,              HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_229_STARRING),      getStringID(LTITLE, TITLE_STR_235_006),                   getStringID(LTITLE, TITLE_STR_236_ALECTREVELYAN),    0, 0},
    {BODY_Xenia,                      HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_230_ALSOFEATURING), getStringID(LTITLE, TITLE_STR_237_JANUSOPPERATIVE),       getStringID(LTITLE, TITLE_STR_238_XENIAONPTOPP),     0, 0},
    {BODY_Ourumov,                    HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_230_ALSOFEATURING), getStringID(LTITLE, TITLE_STR_239_GENERAL),               getStringID(LTITLE, TITLE_STR_240_ARKADYOURUMOV),    0, 0},
    {BODY_Boris,                      HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_230_ALSOFEATURING), getStringID(LTITLE, TITLE_STR_241_BORISGRISHENKO),        getStringID(LTITLE, TITLE_STR_227_LF),               0, 0},
    {BODY_Valentin_,                  HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_230_ALSOFEATURING), getStringID(LTITLE, TITLE_STR_242_EXKGBAGENT),            getStringID(LTITLE, TITLE_STR_243_VELENTINZUKOVSKY), 0, 0},
    {BODY_Siberian_Guard_1_Mishkin,   HEAD_Male_Mishkin,        getStringID(LTITLE, TITLE_STR_230_ALSOFEATURING), getStringID(LTITLE, TITLE_STR_244_DEFENSEMINISTER),       getStringID(LTITLE, TITLE_STR_245_DIMITRIMISHKIN),   0, 0},
    {BODY_Russian_Soldier,            HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_253_RUSSIANSOLDIER),        getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Russian_Infantry,           HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_252_RUSSIANINFANTRY),       getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Scientist_1_Male,           HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_263_SCIENTIST),             getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Scientist_2_Female,         HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_263_SCIENTIST),             getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Russian_Commandant,         HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_256_RUSSIANCOMMANDANT),     getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Janus_Marine,               HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_254_JANUSMARINE),           getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Naval_Officer,              HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_257_NAVALOFFICER),          getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Helicopter_Pilot,           HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_262_HELICOPTERPILOT),       getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_St_Petersburg_Guard,        HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_251_STPETERSBURGGUARD),     getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Civilian_1_Female,          HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_264_CIVILIAN),              getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Civilian_2,                 HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_264_CIVILIAN),              getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Civilian_3,                 HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_264_CIVILIAN),              getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Civilian_4,                 HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_264_CIVILIAN),              getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Siberian_Guard_1_Mishkin,   HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_258_SIBERIANGUARD),         getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Arctic_Commando,            HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_259_ARCTICCOMMANDO),        getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Siberian_Guard_2,           HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_258_SIBERIANGUARD),         getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Siberian_Special_Forces,    HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_260_SIBERIANSPECIALFORCES), getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Jungle_Commando,            HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_250_JUNGLECOMMANDO),        getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Janus_Special_Forces,       HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_255_JANUSSPECIALFORCES),    getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Moonraker_Elite_1_Male,     HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_261_MOOKRAKERELITE),        getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Moonraker_Elite_2_Female,   HEAD_RANDOM,              getStringID(LTITLE, TITLE_STR_227_LF),            getStringID(LTITLE, TITLE_STR_261_MOOKRAKERELITE),        getStringID(LTITLE, TITLE_STR_227_LF),               0, 1},
    {BODY_Mayday,                     HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_231_GUESTSTAR),     getStringID(LTITLE, TITLE_STR_246_MAYDAY),                getStringID(LTITLE, TITLE_STR_227_LF),               0, 0},
    {BODY_Jaws,                       HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_231_GUESTSTAR),     getStringID(LTITLE, TITLE_STR_247_JAWS),                  getStringID(LTITLE, TITLE_STR_227_LF),               0, 0},
    {BODY_Oddjob,                     HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_231_GUESTSTAR),     getStringID(LTITLE, TITLE_STR_248_ODDJOB),                getStringID(LTITLE, TITLE_STR_227_LF),               0, 0},
    {BODY_Baron_Samedi,               HEAD_FIXED,               getStringID(LTITLE, TITLE_STR_231_GUESTSTAR),     getStringID(LTITLE, TITLE_STR_249_BERONSAMEDI),           getStringID(LTITLE, TITLE_STR_227_LF),               0, 0},
    {0xFFFFFFFF,                      0,                        0,                                                0,                                                        0,                                                   0, 0}
};

struct intro_animation intro_animation_table[] = {
    {ANIM_spotting_bond, 98.0, 1.0, 0},
    {ANIM_fire_standing_draw_one_handed_weapon_fast, 21.0, 1.0, 1},
    {ANIM_fire_standing_draw_one_handed_weapon_slow, 26.0, 1.0, 1},
    {ANIM_fire_step_right_one_handed_weapon, 0.0, 1.0, 1},
    {ANIM_fire_kneel_forward_one_handed_weapon_fast, 0.0, 1.0, 1},
    {ANIM_running_one_handed_weapon, 0.0, 0.91000003, 1},
    {ANIM_draw_one_handed_weapon_and_stand_up, 31.0, 1.0, 1},
    {ANIM_aim_one_handed_weapon_left_right, 0.0, 1.0, 1},
    {ANIM_cock_one_handed_weapon_and_turn_around, 0.0, 1.0, 1},
    {ANIM_cock_one_handed_weapon_turn_around_and_stand_up, 0.0, 1.0, 1},
    {ANIM_draw_one_handed_weapon_and_turn_around, 0.0, 1.0, 1},
    {ANIM_drop_weapon_and_show_fight_stance, 248.0, 1.0, 0},
    {ANIM_laughing_in_disbelief, 150.0, 1.0, 0},
    {ANIM_fire_hip_forward_one_handed_weapon, 0.0, 0.89999998, 1},
    {ANIM_fire_standing_left_one_handed_weapon_fast, 0.0, 0.89999998, 1},
    {ANIM_fire_kneel_left_one_handed_weapon_fast, 0.0, 0.89999998, 1},
    {ANIM_draw_one_handed_weapon_and_look_around, 51.0, 1.0, 1},
    {ANIM_aim_one_handed_weapon_left, 0.0, 0.89999998, 1},
    {ANIM_aim_one_handed_weapon_right, 0.0, 0.89999998, 1},
    {ANIM_conversation, 37.0, 1.0, 2},
    {ANIM_conversation_listener, 300.0, 1.0, 2},
    {ANIM_conversation_cleaned, 120.0, 1.0, 2},
    {0xFFFFFFFF, 0.0, 0.0, 0}
};

struct coord3d zeroCoord = { 0 };

#define INTRO_RANDOM_RIFLE_LEN 6
struct intro_random_rifles
{
    PROP props[INTRO_RANDOM_RIFLE_LEN];
};

struct intro_random_rifles random_rifles_in_intro = {
    PROP_CHRKALASH,
    PROP_CHRM16,
    PROP_CHRFNP90,
    PROP_CHRAUTOSHOT,
    PROP_CHRGRENADELAUNCH,
    PROP_CHRSNIPERRIFLE
};

#define INTRO_RANDOM_PISTO_LEN 10
struct intro_random_pistols
{
    PROP props[INTRO_RANDOM_PISTO_LEN];
};

struct intro_random_pistols random_pistols_in_intro = {
    PROP_CHRWPPK,
    PROP_CHRWPPKSIL,
    PROP_CHRSKORPION,
    PROP_CHRUZI,
    PROP_CHRUZI,
    PROP_CHRTT33,
    PROP_CHRTT33,
    PROP_CHRRUGER,
    PROP_CHRLASER,
    PROP_CHRGOLDEN
};

struct coord3d cast_camera_pos    = { 0.0f, 0.0f, 1.0f }; // campos in constructor_menu18_displaycast is set to this, but is then immediately overwritten, so the values here have no effect.
struct coord3d cast_camera_target = { 0.0f, 0.0f, 0.0f };
struct coord3d cast_camera_up     = { 0.0f, 1.0f, 0.0f }; // Display cast character up vector

ModelRenderData cast_renderdata_default = { NULL,
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


struct coord3d cast_camera_offset = { 0.0f, 40.0f, 25.0f }; // vec in constructor_menu18_displaycast is initialized to this, but then gets overwritten before it's read, so these values don't have any effect.

u32 D_8002BAE8 = 0; // unused
u32 D_8002BAEC = 1; // unused

u16 color_palette_entries_50_percent[] = {1, 0x1F, 0x3C1, 0x3DF, 0x7801, 0x781F, 0x7BC1, 0x7BDF};


// forward declarations

Gfx* frontAddPreviousTabText(Gfx* DL);
s32 check_if_stage_completed_on_difficulty(int stage, DIFFICULTY difficulty);
s32 get_highest_unlocked_difficulty_for_level(s32 arg0);
Gfx *constructor_menu00_legalscreen(Gfx *DL);
Gfx *constructor_menu01_nintendo(Gfx *DL);
Gfx *constructor_menu02_rareware(Gfx *DL);
Gfx *constructor_menu03_eye(Gfx *DL);
Gfx *constructor_menu04_goldeneyelogo(Gfx *DL);
Gfx *constructor_menu05_fileselect(Gfx *DL);
Gfx *constructor_menu06_modesel(Gfx *DL);
Gfx *constructor_menu07_missionsel(Gfx *DL);
Gfx *constructor_menu08_difficulty(Gfx *DL);
Gfx *constructor_menu09_007options(Gfx *DL);
Gfx *constructor_menu0A_briefing(Gfx *DL);
Gfx *constructor_menu0C_missionfailed(Gfx *DL);
Gfx *constructor_menu0D_missioncomplete(Gfx *DL);
Gfx *constructor_menu0E_mpoptions(Gfx *DL);
Gfx *constructor_menu0F_mpcharsel(Gfx *DL);
Gfx *constructor_menu10_mphandicap(Gfx *DL);
Gfx *constructor_menu11_mpcontrol(Gfx *DL);
Gfx *constructor_menu12_mpstage(Gfx *DL);
Gfx *constructor_menu13_mpscenario(Gfx *DL);
Gfx *constructor_menu14_mpteams(Gfx *DL);
Gfx *constructor_menu15_cheat(Gfx *DL);
Gfx *constructor_menu16_nocontrollers(Gfx *DL);
Gfx *constructor_menu17_switchscreens(Gfx *DL);
Gfx *constructor_menu18_displaycast(Gfx *DL);
Gfx *constructor_menu19_spectrum(Gfx *DL);
#ifndef TARGET_N64
#endif
void disable_all_switches(Model *arg0);
void set_item_visibility_in_objinstance(Model* objinstance, s32 item, s32 mode);
void set_cursor_to_stage_solo(LEVEL_SOLO_SEQUENCE level);
Gfx *display_aligned_white_text_to_screen(Gfx *dl, s32 arg1, s32 arg2, s32 halign, s32 valign, u8 *arg5, struct fontchar *arg6, struct font *arg7);
void setCursorPOSforMode(int mode);
void set_cursor_pos_difficulty(int difficulty);

// end forward declarations.


Gfx *frontPrintText(Gfx *gdl, s32 *x, s32 *y, s8 *text, struct fontchar *second_font_table, struct font *first_font_table, s32 arg6, s32 view_x, s32 view_y, s32 arg9, s32 arga)
{
    if (j_text_trigger != 0)
    {
        gdl = textRenderOutlined(
            gdl,
            x,
            y,
            text,
            second_font_table,
            first_font_table,
            arg6,
            (textglowR.r << 0x18) | (textglowG.r << 0x10) | (textglowB.r << 8) | textglowA.r,
            view_x,
            view_y,
            arg9,
            arga);
    }
    else
    {
        gdl = textRender(
            gdl,
            x,
            y,
            text,
            second_font_table,
            first_font_table,
            arg6,
            view_x,
            view_y,
            arg9,
            arga);
    }

    return gdl;
}


s32 frontCheckIfCheatIsUnlocked(s32 cheat)
{
    switch(cheat)
    {
        case CHEAT_EXTRA_MP_CHARS:
        case CHEAT_MAXAMMO:
        case CHEAT_DEBUG_UNK5:
        case CHEAT_DEACTIVATE_INVINCIBILITY:
        case CHEAT_LINEMODE:
        case CHEAT_2X_HEALTH:
        case CHEAT_2X_ARMOR:
        case CHEAT_EXTRA_WEAPONS:
        case CHEAT_10X_HEALTH:
        case CHEAT_BONDPHASE:
        case CHEAT_DEBUG_POS:
        case CHEAT_UNLOCK_PAINTBALL:
        case CHEAT_UNLOCK_INVINCIBLE:
        case CHEAT_UNLOCK_DKMODE:
        case CHEAT_UNLOCK_2XGL:
        case CHEAT_UNLOCK_2XRL:
        case CHEAT_UNLOCK_TURBO:
        case CHEAT_UNLOCK_NORADAR:
        case CHEAT_UNLOCK_TINY:
        case CHEAT_UNLOCK_2XTKNIFE:
        case CHEAT_UNLOCK_FAST:
        case CHEAT_UNLOCK_INVIS:
        case CHEAT_UNLOCK_ENEMYRL:
        case CHEAT_UNLOCK_SLOW:
        case CHEAT_UNLOCK_SILVERPPK:
        case CHEAT_UNLOCK_2XHKNIFE:
        case CHEAT_UNLOCK_INFAMMO:
        case CHEAT_UNLOCK_2XFNP0:
        case CHEAT_UNLOCK_GOLDPPK:
        case CHEAT_UNLOCK_2XLASER:
        case CHEAT_UNLOCK_ALLGUNS:
        case CHEAT_UNLOCK_DAM:
        case CHEAT_UNLOCK_FACILITY:
        case CHEAT_UNLOCK_RUNWAY:
        case CHEAT_UNLOCK_SURFACE:
        case CHEAT_UNLOCK_BUNKER:
        case CHEAT_UNLOCK_SILO:
        case CHEAT_UNLOCK_FRIGATE:
        case CHEAT_UNLOCK_SURFACE2:
        case CHEAT_UNLOCK_BUNKER2:
        case CHEAT_UNLOCK_STATUE:
        case CHEAT_UNLOCK_ARCHIVES:
        case CHEAT_UNLOCK_STREETS:
        case CHEAT_UNLOCK_DEPOT:
        case CHEAT_UNLOCK_TRAIN:
        case CHEAT_UNLOCK_JUNGLE:
        case CHEAT_UNLOCK_CONTROL:
        case CHEAT_UNLOCK_CAVERNS:
        case CHEAT_UNLOCK_CRADLE:
        case CHEAT_UNLOCK_AZTEK:
        case CHEAT_UNLOCK_EGYPT:
#ifndef TARGET_N64
        case CHEAT_PORT_SNIPER_ROCKET:
        case CHEAT_PORT_ENEMY_FF:
        case CHEAT_PORT_UNLOCK_DOORS:
#endif
            return 0;

        case CHEAT_INVINCIBILITY:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_FACILITY);

        case CHEAT_ALLGUNS:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_EGYPT);

        case CHEAT_NO_RADAR_MP:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_FRIGATE);

        case CHEAT_FAST_ANIMATION:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_STATUE);

        case CHEAT_INVISIBILITY:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_ARCHIVES);

        case CHEAT_INFINITE_AMMO:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_CONTROL);

        case CHEAT_DK_MODE:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_RUNWAY);

        case CHEAT_TINY_BOND:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_SURFACE2);

        case CHEAT_SLOW_ANIMATION:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_DEPOT);

        case CHEAT_PAINTBALL:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_DAM);

        case CHEAT_SILVER_PP7:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_TRAIN);

        case CHEAT_GOLD_PP7:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_CRADLE);

        case CHEAT_ENEMY_ROCKETS:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_STREETS);

        case CHEAT_2X_ROCKET_LAUNCHER:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_BUNKER1);

        case CHEAT_2X_GRENADE_LAUNCHER:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_SURFACE1);

        case CHEAT_2X_RCP90:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_CAVERNS);

        case CHEAT_2X_THROWING_KNIFE:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_BUNKER2);

        case CHEAT_2X_HUNTING_KNIFE:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_JUNGLE);

        case CHEAT_2X_LASER:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_AZTEC);

        case CHEAT_TURBO_MODE:
            return fileGetIsCheatUnlocked( fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_SILO);

        case CHEAT_MAGNUM:
            return fileIsCradleCompletedForFolder(selected_folder_num);

        case CHEAT_LASER:
            return fileIsAztecCompletedOnSecretOr00ForFolder(selected_folder_num);

        case CHEAT_GOLDEN_GUN:
            return fileIsEgyptCompletedOn00ForFolder(selected_folder_num);

        default:
            do {/* WARNING: Do nothing block with infinite loop */
            } while( 1 );
        }
}


u16 *frontGetPlayersFavoriteWeaponInHand(int player,int hand) {
    return get_ptr_long_watch_text_for_item(array_favweapon[player][hand]);
}




void frontUpdateControlStickPosition(void) {
    s8 stickx = joyGetStickX(PLAYER_1);
    s8 sticky = -joyGetStickY(PLAYER_1);
#ifndef TARGET_N64
    {   /* A pad's D-pad does not move the crosshair: on Pro / Standard layouts
         * it arrives as full-deflection digital Move, so that axis is dropped
         * here and steps the open list instead (interface_menu1C_portsheet);
         * the analog stick, keyboard and mouse still point. */
        extern s32 portInputPadDigMoveP1(s32 *dx, s32 *dy) __attribute__((weak)); /* input_pc.c */
        s32 ddx = 0;
        s32 ddy = 0;

        if (portInputPadDigMoveP1 != NULL && portInputPadDigMoveP1(&ddx, &ddy))
        {
            if (ddx != 0) stickx = 0;
            if (ddy != 0) sticky = 0;
        }
    }
#endif


    if (stickx < -5) {
        stickx = (stickx + 5);
    }
    else  if (stickx >= 6) {
        stickx = (stickx - 5);
    }
    else {
        stickx = 0;
    }

    if (stickx >= 71) {
        stickx = 70;
    }
    else {
        if (stickx < -70) {
            stickx = -70;
        }
    }

    if (sticky < -5) {
        sticky = (sticky + 5);
    }
    else  if (sticky >= 6) {
        sticky = (sticky - 5);
    }
    else {
        sticky = 0;
    }

    if (sticky >= 71) {
        sticky = 70;
    }
    else {
        if (sticky < -70) {
            sticky = -70;
        }
    }

#ifdef GE_VIDEO
    { /* The mouse drives the menu crosshair (absolute position mapped through
       * the 4:3 band), but only when it moves, so the stick still works. If
       * anything else moved the cursor since the mouse last placed it (a menu
       * snap or the stick), re-seat the pointer onto the cursor and swallow
       * that tick's motion; values are pre-clamped to the cursor's legal box so
       * edge travel doesn't re-seat every tick. */
        extern s32 portMouseMenuPos(f32 *gx, f32 *gy, f32 left, f32 top, f32 width, f32 height);
        extern void portMouseMenuWarp(f32 gx, f32 gy, f32 left, f32 top, f32 width, f32 height);
        static f32 mouse_last_h = 1.0e9f; /* sentinel: first move re-seats */
        static f32 mouse_last_v;
        /* Ownership flips to the stick on any deflection and back to the mouse
         * when it places the cursor (see g_portMenuMouseOwns). The override
         * branch stays as a backstop for any snap site that isn't gated yet. */
        f32 mgx, mgy;
        if (stickx != 0 || sticky != 0)
        {
            g_portMenuMouseOwns = 0;
        }
        if (g_portMenuMouseReseat)
        {
            g_portMenuMouseReseat = 0;
            mouse_last_h = 1.0e9f;   /* force the handoff: the pointer re-seats onto the entry snap */
        }
        extern s32 portConIsOpen(void);

        if (!portConIsOpen()   /* while the console is open the pointer reads
                                * neutral, as the buttons do */
            && portMouseMenuPos(&mgx, &mgy,
                             getPlayer_c_screenleft(), getPlayer_c_screentop(),
                             getPlayer_c_screenwidth(), getPlayer_c_screenheight()))
        {
            if ((cursor_h_pos != mouse_last_h || cursor_v_pos != mouse_last_v) && !g_portMenuMouseOwns)
            {
                portMouseMenuWarp(cursor_h_pos, cursor_v_pos,
                                  getPlayer_c_screenleft(), getPlayer_c_screentop(),
                                  getPlayer_c_screenwidth(), getPlayer_c_screenheight());
            }
            else
            {
                if (mgx > getPlayer_c_screenleft() + getPlayer_c_screenwidth() - 20.0f) mgx = getPlayer_c_screenleft() + getPlayer_c_screenwidth() - 20.0f;
                if (mgx < getPlayer_c_screenleft() + 20.0f) mgx = getPlayer_c_screenleft() + 20.0f;
                if (mgy > getPlayer_c_screentop() + getPlayer_c_screenheight() - 20.0f) mgy = getPlayer_c_screentop() + getPlayer_c_screenheight() - 20.0f;
                if (mgy < getPlayer_c_screentop() + 20.0f) mgy = getPlayer_c_screentop() + 20.0f;
                cursor_h_pos = mgx;
                cursor_v_pos = mgy;
            }
            mouse_last_h = cursor_h_pos;
            mouse_last_v = cursor_v_pos;
            g_portMenuMouseOwns = 1; /* the mouse owns the cursor again */
        }
    }
#endif

    if (stickx > 0) {
        cursor_h_pos += (f32)(( (f32)stickx * 0.075f + 0.5f) * g_GlobalTimerDelta);
    }
    else if (stickx < 0) {
        cursor_h_pos += (f32)(( (f32)stickx * 0.075f - 0.5f) * g_GlobalTimerDelta);
    }

    if ((getPlayer_c_screenleft() + getPlayer_c_screenwidth() - 20.0f) < cursor_h_pos) {
        cursor_h_pos = (getPlayer_c_screenleft() + getPlayer_c_screenwidth() - 20.0f);
    }
    else {
        if (cursor_h_pos < getPlayer_c_screenleft() + 20.0f) {
            cursor_h_pos = getPlayer_c_screenleft() + 20.0f;
        }
    }

    if (sticky > 0) {
        cursor_v_pos += (f32)(( (f32)sticky * 0.075f + 0.5f) * g_GlobalTimerDelta);
    }
    else if (sticky < 0) {
        cursor_v_pos += (f32)(( (f32)sticky * 0.075f - 0.5f) * g_GlobalTimerDelta);
    }

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): menu-reversal hunt - the post-deadzone
       * stick the cursor integrates vs the raw joy value vs where the
       * cursor actually went, 2Hz. */
        extern char *getenv(const char *);
        static int dump = -1;
        static u32 calls;
        if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
        if (dump && (calls++ % 30) == 0)
        {
            fprintf(stderr, "cursor: raw=%d,%d post=%d,%d pos=%.1f,%.1f\n",
                    joyGetStickX(PLAYER_1), joyGetStickY(PLAYER_1),
                    stickx, sticky, cursor_h_pos, cursor_v_pos);
        }
    }
#endif

    if ((getPlayer_c_screentop() + getPlayer_c_screenheight() - 20.0f) < cursor_v_pos) {
        cursor_v_pos = (getPlayer_c_screentop() + getPlayer_c_screenheight() - 20.0f);
        return;
    }
    if (cursor_v_pos < (getPlayer_c_screentop() + 20.0f)) {
        cursor_v_pos = (getPlayer_c_screentop() + 20.0f);
    }
}



Gfx *frontDrawCursor(Gfx *DL)
{
    f32 xypos[2];
    f32 halfedxy[2];
    sImageTableEntry *image;
    s32 unused;
    s32 option;

    /* WIDESCREEN: no explicit clear here, because the scene-wide flag
     * self-expires (video.c) and frontend frames fall back to 4:3 on their own.
     * Clearing it here used to break the wide-rendered watch (pause) frames. */

    option = folder_selection_screen_option_icon;
    if (option == 0) {
        image = crosshairimage;
    }
    else if (option == 1) {
        image = mainfolderimages + IMG_COPY; //COPY
    }
    else if (option == 2) {
        image = mainfolderimages + IMG_DEL;
    }

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): the post-mission copy-icon cursor bug -
       * log which entry the cursor draws and its promoted index/pool word
       * so a menu->mission->menu pass shows where the aliasing happens. */
        extern char *getenv(const char *);
        static int dump = -1;
        static u32 calls;
        if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
        if (dump && (calls++ % 61) == 0)
        {
            fprintf(stderr,
                    "cursor: opt=%d img=%08x idx=%08x w=%d h=%d fmt=%d copyidx=%08x\n",
                    option, (uintptr_t) image, image->index, image->width,
                    image->height, image->format,
                    (mainfolderimages + IMG_COPY)->index);
            fflush(stderr);
        }
    }
#endif

    texSelect(&DL, image, 4, 0, 0);

    xypos[0] = floorFloat(cursor_h_pos + 0.5f);
    xypos[1] = floorFloat(cursor_v_pos + 0.5f);

    halfedxy[0] = image->width * 0.5f;
    halfedxy[1] = image->height * 0.5f;

#ifdef TARGET_N64
    display_image_at_position(&DL, &xypos, &halfedxy, image->width, image->height, 0, 0, 1, 255, 255, 255, 220, (image->level > 0), 0);
#else
    if (image == crosshairimage)
    {
        /* The menu cursor is the crosshair sprite, so it follows midas.ini crosshair_color;
         * the copy / delete cursors are their own icons and stay as they are. */
        portDisplayCrosshairImage(&DL, &xypos, &halfedxy, image->width, image->height, 220,
                                  (image->level > 0));
    }
    else
    {
        display_image_at_position(&DL, &xypos, &halfedxy, image->width, image->height, 0, 0, 1, 255, 255, 255, 220, (image->level > 0), 0);
    }
#endif

    return DL;
}


//********************************************************************************************************
// TABS
//********************************************************************************************************

Gfx* frontAddStartTabText(Gfx* DL)
{
    s32 v;
    s32 h;

    s32 textWidth;
    s32 textHeight;

    g_textPtrTAB1 = langGet(getStringID(LTITLE, TITLE_STR_04_START));
    setTextSpacingInverted(TRUE);
    textHeight = 0;
    textWidth = 0;
    //text is sideways so textWidth and textHeight are flipped
    textMeasure(&textWidth, &textHeight, g_textPtrTAB1, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    v = STARTTAB_TEXT_TOP;
    h = TABS_RIGHT_EDGE - (textWidth / 2);

    if (tab_start_highlight)
    {
        DL = microcode_constructor_related_to_menus(DL, (h - textWidth) + 1, STARTTAB_TEXT_TOP, h, STARTTAB_TEXT_BOTTOM, 50);
    }

    setTextOrientation(ROT_90CW);

    v = STARTTAB_TEXT_HALF - (textHeight / 2);
    DL = textRender(DL, &v, &h, g_textPtrTAB1, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF, viGetY(), viGetX(), 0, 0);
    setTextOrientation(ROT_NORMAL);
    setTextSpacingInverted(FALSE);
    return DL;
}


s32 frontCheckCursorOnStartTab(void)
{
    if ((TABS_LEFT_EDGE < cursor_h_pos) && (cursor_v_pos <= STARTTAB_TAB_BOTTOM))
    {
        return TRUE;
    }

    return FALSE;
}


Gfx* frontAddPreviousTabText(Gfx* DL)
{
    s32 v;
    s32 h;

    s32 textWidth;
    s32 textHeight;

    g_textPtrTAB3 = langGet(getStringID(LTITLE, TITLE_STR_06_PREVIOUS));
    setTextSpacingInverted(TRUE);
    textHeight = 0;
    textWidth = 0;
    //text is sideways so textWidth and textHeight are flipped
    textMeasure(&textWidth, &textHeight, g_textPtrTAB3, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    v = PREVTAB_TEXT_TOP;
    h = TABS_RIGHT_EDGE - (textWidth / 2);

    if (tab_prev_highlight != 0)
    {
        DL = microcode_constructor_related_to_menus(DL, (h - textWidth) + 1, PREVTAB_TEXT_TOP, h, PREVTAB_TEXT_BOTTOM, 50);
    }

    setTextOrientation(ROT_90CW);

    v = PREVTAB_TEXT_HALF - (textHeight / 2);
    DL = textRender(DL, &v, &h, g_textPtrTAB3, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF, viGetY(), viGetX(), 0, 0);
    setTextOrientation(ROT_NORMAL);
    setTextSpacingInverted(FALSE);

    return DL;
}


#ifndef TARGET_N64
/* Port: the three MP screens that wait for every player to confirm soft-lock
 * when midas.ini pads=N forces a missing pad, so an L press or the aim bind
 * (R_TRIG, right mouse) bails out; an on-tab exit label never rendered there,
 * so don't retry one. */
/* Port: TRUE when any of the first numplayers pads pressed L or the aim
 * bind (R_TRIG) this frame. Absent pads return 0 from
 * joyGetButtonsPressedThisFrame, so only a live pad / the mouse can fire it. */
extern s32 portJoyTwinStickCapable(s32 pad); /* joy_port.c: twin stick, see the style-select rules below */
s32 portMpWaitScreenExitPressed(s32 numplayers)
{
    s32 i;

    for (i = 0; i < numplayers; i++)
    {
        if (joyGetButtonsPressedThisFrame(i, L_TRIG | R_TRIG))
        {
            return TRUE;
        }
    }

    return FALSE;
}
#endif


u32 frontCheckCursorOnPreviousTab(void)
{
    if ((TABS_LEFT_EDGE < cursor_h_pos) && (PREVTAB_TAB_TOP < cursor_v_pos))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void frontSetCursorPositionToNextTab(void)
{
    if (frontCursorSnapAllowed())
    {
        cursor_h_pos = NEXTTAB_CURSOR_HPOS;
        cursor_v_pos = NEXTTAB_CURSOR_VPOS;
    }
}


Gfx* frontAddNextTabText(Gfx* DL)
{
    s32 v;
    s32 h;

    s32 textWidth;
    s32 textHeight;

    g_textPtrTAB2 = langGet(getStringID(LTITLE, TITLE_STR_05_NEXT)); //"NEXT\n"
    setTextSpacingInverted(TRUE);
    textHeight = 0;
    textWidth = 0;
    //text is sideways so textWidth and textHeight are flipped
    textMeasure(&textWidth, &textHeight, g_textPtrTAB2, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    v = NEXTTAB_TEXT_TOP;
    h = TABS_RIGHT_EDGE - (textWidth / 2);

    if (tab_next_highlight != 0)
    {
        DL = microcode_constructor_related_to_menus(DL, (h - textWidth) + 1, NEXTTAB_TEXT_TOP, h, NEXTTAB_TEXT_BOTTOM, 50);
    }

    setTextOrientation(ROT_90CW);

    v = NEXTTAB_TEXT_HALF - (textHeight / 2);
    DL = textRender(DL, &v, &h, g_textPtrTAB2, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF, viGetY(), viGetX(), 0, 0);
    setTextOrientation(ROT_NORMAL);
    setTextSpacingInverted(FALSE);
    return DL;
}


u32 frontCheckCursorOnNextTab(void)
{
    if (((TABS_LEFT_EDGE < cursor_h_pos) && (NEXTTAB_TAB_TOP < cursor_v_pos)) && (cursor_v_pos <= PREVTAB_TAB_TOP))
    {
        return TRUE;
    }

    return FALSE;
}



//********************************************************************************************************
//BEGIN MENU SECTIONS
//********************************************************************************************************

//********************************************************************************************************
//LEGAL SCREEN
//********************************************************************************************************
void init_menu00_legalscreen(void)
{
    s32 padding;
    struct coord3d pos;

    musicTrack1Stop();
#ifndef TARGET_N64
    /* the title stage's load puts the tracks back to full (lvlStageLoad) and
     * the menu_init seed runs once per process, so re-apply music_volume here
     * or the music after an attract demo ignores it */
    portApplyGlobalVolumesHere();
#endif
    maybe_is_in_menu = TRUE;
    g_MenuTimer = 0;
    pos = legalpage_pos;
    load_object_fill_header(PitemZ_entries[PROP_LEGALPAGE].header, PitemZ_entries[PROP_LEGALPAGE].filename, ptr_logo_and_walletbond_DL, 0x3c000, 0);
    modelCalculateRwDataLen(PitemZ_entries[PROP_LEGALPAGE].header);

    logoinst = modelmgrInstantiateModel(PitemZ_entries[PROP_LEGALPAGE].header);
    #ifdef DEBUG
        assert(logoinst);
    #endif
    modelSetScale(logoinst, 1.0f);
    setsuboffset(logoinst, &pos);
    fileValidateSaves();
}


void update_menu00_legalscreen(void)
{
    is_first_time_on_legal_screen = FALSE;
    clear_model_obj(logoinst);
}


void interface_menu00_legalscreen(void)
{
#ifdef REFRESH_PAL
#define MENU_LEGALSCREEN_MENU_TIMER_MAX (50*4+1)
#else
#define MENU_LEGALSCREEN_MENU_TIMER_MAX (60*4+1)
#endif

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);
#if defined(ISGOLDFINGER) || defined(USEEXPANSION)
    if (osGetMemSize != 0x00800000)
    {
        frontChangeMenu(MENU_NO_CONTROLLERS, TRUE); //double up as No expansion pak window
        return;
    }
#endif
    g_MenuTimer += g_ClockTimer;

    if (g_MenuTimer >= MENU_LEGALSCREEN_MENU_TIMER_MAX)
    {
        if ((joyGetControllerCount() < 1) && (is_first_time_on_legal_screen))
        {
            frontChangeMenu(MENU_NO_CONTROLLERS, TRUE);
        }
        else
        {
            frontChangeMenu(MENU_NINTENDO_LOGO, TRUE);
        }
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON) && (!is_first_time_on_legal_screen))
    {
        if (!is_first_time_on_main_menu)
        {
            frontChangeMenu(MENU_FILE_SELECT, TRUE);
        }
        else
        {
            frontChangeMenu(MENU_NINTENDO_LOGO, TRUE);
        }
    }
}


/**
 * Render a string of standard text on the legal screen e.g. "TWYCROSS BOARD OF GAME CLASSIFICATION"
 */
Gfx *display_aligned_white_text_to_screen(Gfx *dl, s32 arg1, s32 arg2, s32 halign, s32 valign, u8 *text, struct fontchar *arg6, struct font *arg7)
{
    s32 sp4C;
    s32 sp48;
    s32 x;
    s32 y;

    sp48 = 0;
    sp4C = 0;
    textMeasure(&sp4C, &sp48, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = arg1 - ((s32) (halign * sp48) / 2);
    y = arg2 - ((s32) (valign * sp4C) / 2);

    return textRender(dl, &x, &y, text, arg6, arg7, -1, viGetX(), viGetY(), 0, 0);
}


Gfx *constructor_menu00_legalscreen(Gfx *DL)
{
    struct legal_screen_text *legal_text_end;
    struct ModelRenderData renderdata;
    Mtxf *temp;
    Mtxf lookatmtx;
    s32 i;
    u8 *txt;
    Mtxf tmpmtx;
    struct legal_screen_text *legal_text_ptr;

    renderdata = legalscreen_MRD;
    
    DL = clear_framebuffer_black(DL);
    
#ifdef TARGET_N64
    matrix_4x4_set_lookat_target(&lookatmtx, 0.0f, 0.0f, 4000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, legal_text_ptr->v_pos * 0.0f);
#else
    /* legal_text_ptr is UNINITIALIZED here (assigned ~27 lines below); the
     * x0.0 made the garbage read harmless on N64 but the deref itself
     * faults on PC. The product is identically 0.0f. */
    matrix_4x4_set_lookat_target(&lookatmtx, 0.0f, 0.0f, 4000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
#endif
    
    renderdata.basemtx = &lookatmtx;
    renderdata.mtxlist = (Mtxf *) dynAllocate(logoinst->obj->numMatrices << 6);
    
    matrix_4x4_copy(&lookatmtx, renderdata.mtxlist);
    
    logoinst->render_pos = (union RenderPosView *) renderdata.mtxlist;
    
    modelUpdateNodeRelations(logoinst);
    
    renderdata.flags = 3;
    renderdata.zbufferenabled = 0;
    renderdata.gdl = DL;
    
    subdraw(&renderdata, logoinst);
    
    DL = renderdata.gdl;
    
    for (i = 0; i < logoinst->obj->numMatrices; i++)
    {
        matrix_4x4_copy((Mtxf *)&((s8 *)logoinst->render_pos)[i * 0x40], &tmpmtx);
        matrix_4x4_f32_to_s32(&tmpmtx, &((Mtxf *) logoinst->render_pos)[i]);
    }
    
    DL = microcode_constructor(DL);
    
    legal_text_ptr = legalpage_text_array;
#ifdef TARGET_N64
    legal_text_end = (struct legal_screen_text *)&legalscreen_MRD;
#else
    /* The N64 loop bound was the next global's address (legalscreen_MRD follows
     * the array in IDO's .data), which GCC places elsewhere, so only the first
     * legal line rendered; bound the loop by one past the end of the array
     * instead. */
    legal_text_end = legalpage_text_array +
        (sizeof(legalpage_text_array) / sizeof(legalpage_text_array[0]));
#endif

    do
    {
        DL = display_aligned_white_text_to_screen(DL, legal_text_ptr->h_pos, legal_text_ptr->v_pos, legal_text_ptr->halign, legal_text_ptr->valign, langGet(legal_text_ptr->txtID), ptrFontZurichBoldChars, ptrFontZurichBold);
        legal_text_ptr++;
    }
    while (legal_text_ptr < legal_text_end);

#ifndef TARGET_N64
    {   /* PORT: the "Project Midas: Build ..." line, centred one line above the
         * heading (16 units, mirroring the old offset below it). The stamp is
         * sysstubs.c's (git commit count + build date), the same string the log
         * prints, so it is not a lang string. */
        extern const char *portBuildString(void);
        extern const char *portPlatformName(void);   /* the git branch, else "PC-Port" (sysstubs.c) */
        char line[96];

        sprintf(line, "Project Midas %s: Build %s\n", portPlatformName(), portBuildString());
        DL = display_aligned_white_text_to_screen(DL, 220, 16, CENTER_ALIGN, CENTER_ALIGN, (u8 *) line,
                                                  ptrFontZurichBoldChars, ptrFontZurichBold);
    }
#endif

    return DL;
}


void reset_menutimer(void) {
    g_MenuTimer = 0;
}

void dummy7F00AAF8(void) {
    // (function likely stub)
}




//********************************************************************************************************
//SWITCH SCREEN
//********************************************************************************************************
void interface_menu17_switchscreens(void)
{
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    g_MenuTimer++;
    if ((g_MenuTimer >= 4) && (!spectrum_related_flag) && (!is_emulating_spectrum))
    {
        maybe_prev_menu = menu_update;
        menu_update = MENU_INVALID;
    }
}


Gfx * constructor_menu17_switchscreens(Gfx * DL)
{
    return clear_framebuffer_black(DL);
}





//********************************************************************************************************
//NINTENDO LOGO
//********************************************************************************************************
#ifndef TARGET_N64
static f32 g_PortLogoModelScale = 1.0f; /* GE_LOGOMODEL radius ratio */
#endif

void init_menu01_nintendo(void)
{
    struct coord3d pos;
    s32 logoprop = PROP_NINTENDOLOGO;
    f32 logoscale = 1.0f;

#ifndef TARGET_N64
    { /* GE_LOGOMODEL=<prop number> (texture debugging): render any prop model
       * in place of the Nintendo boot logo, spinning, scaled to fill the logo's
       * space and loaded the same way world pickups are (e.g. 187 = chrlaser,
       * 197 = chrfnp90, 24 = cctv). */
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        const char *e = getenv("GE_LOGOMODEL");
        if (e != NULL && e[0] >= '0' && e[0] <= '9')
        {
            s32 v = 0;
            s32 k;
            for (k = 0; e[k] >= '0' && e[k] <= '9'; k++)
            {
                v = v * 10 + (e[k] - '0');
            }
            if (v < PROP_MAX && PitemZ_entries[v].header != NULL)
            {
                f32 logor = PitemZ_entries[PROP_NINTENDOLOGO].header->BoundingVolumeRadius;
                f32 modelr = PitemZ_entries[v].header->BoundingVolumeRadius;
                logoprop = v;
                if (modelr > 0.0f)
                {
                    logoscale = logor / modelr;
                }
                printf("ge: logomodel: prop %d radius %f -> scale %f\n",
                       v, modelr, logoscale);
            }
        }
    }
#endif

    g_MenuTimer = 0;
    pos = nintendologo_pos;
#ifndef TARGET_N64
    if (logoprop != PROP_NINTENDOLOGO)
    {
        /* GE_LOGOMODEL: the vanilla call passes no texpool (the Nintendo
         * logo is untextured) - textured models come out flat white.
         * Split the buffer like the cast screen does: pool first, model
         * data after. */
        static struct texpool logoTexPool;
        texInitPool(&logoTexPool, ptr_logo_and_walletbond_DL, 0x19000);
        load_object_fill_header(PitemZ_entries[logoprop].header,
                                PitemZ_entries[logoprop].filename,
                                ptr_logo_and_walletbond_DL + 0x19000,
                                0x3c000 - 0x19000, &logoTexPool);
    }
    else
#endif
    load_object_fill_header(PitemZ_entries[logoprop].header, PitemZ_entries[logoprop].filename, ptr_logo_and_walletbond_DL, 0x3c000, 0);
    modelCalculateRwDataLen(PitemZ_entries[logoprop].header);
    logoinst = modelmgrInstantiateModel(PitemZ_entries[logoprop].header);
    modelSetScale(logoinst, 1.0f);
#ifndef TARGET_N64
    /* modelSetScale is not consulted by this screen's hand-built matrix
     * chain - the radius ratio is folded into the constructor's scalar
     * multiply instead */
    g_PortLogoModelScale = logoscale;
#endif
    setsuboffset(logoinst, &pos);
    musicTrack1Play(M_INTROSWOOSH);
    maybe_is_in_menu = TRUE;
    ninLogoRotRate = -1.39626348019f;
    ninLogoScale = 0.0183333326131f;

#ifndef TARGET_N64
    if (logoprop != PROP_NINTENDOLOGO)
    {
        ninLogoScale = 1.1f; /* GE_LOGOMODEL: skip the zoom-in */
    }
#endif
}




void update_menu01_nintendo(void)
{
    clear_model_obj(logoinst);
}




void interface_menu01_nintendo(void)
{
#ifdef REFRESH_PAL
/*~= 8.32 * 50 + 1*/
#define NINTENDO_TIMER_MAX 417
#else
/*~= 8.32 * 60 + 1*/
#define NINTENDO_TIMER_MAX 501
#endif

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    g_MenuTimer += g_ClockTimer;

#ifndef TARGET_N64
    { /* GE_LOGOMODEL: hold this screen at full light forever so the
       * swapped-in model can be screenshotted at leisure */
        extern char *getenv(const char *);
        static s32 hold = -1;
        if (hold < 0)
        {
            const char *e = getenv("GE_LOGOMODEL");
            hold = (e != NULL && e[0] >= '0' && e[0] <= '9');
        }
        if (hold)
        {
            g_MenuTimer = 100;
        }
    }
#endif

    if (g_MenuTimer >= NINTENDO_TIMER_MAX)
    {
        frontChangeMenu(MENU_RAREWARE_LOGO, TRUE);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON))
    {
        if (!is_first_time_on_main_menu)
        {
            frontChangeMenu(MENU_FILE_SELECT, TRUE);
        }
        else
        {
            prev_keypresses = TRUE;
            frontChangeMenu(MENU_RAREWARE_LOGO, TRUE);
        }
    }
}


Gfx *constructor_menu01_nintendo(Gfx *DL)
{
    ModelRenderData ninlogo;
    s32 padding;
    s32 ambiantlight;
    Mtxf ninlogoMtxf;
    s32 i;

    ninlogo = nintendologo_MRD;

#ifndef TARGET_N64
    if (g_PortLogoModelScale != 1.0f)
    {
        /* GE_LOGOMODEL: weapon models take their lighting from the fog colour
         * alpha, which is zero on this screen, so render them as an unfogged,
         * fully shaded mission prop so the texture is what's being judged. */
        ninlogo.PropType = 9;
        ninlogo.envcolour.word = 0;
        ninlogo.fogcolour.word = 0xFFFFFFFF;
    }
#endif

    DL = clear_framebuffer_black(DL);

#ifndef TARGET_N64
    if (g_PortLogoModelScale != 1.0f)
    {
        /* GE_LOGOMODEL: this screen never enables texture-coordinate
         * scaling, so every fetch lands on texel (0,0) and textured
         * models come out one flat colour. Missions set this up in the
         * world render; do it here. */
        gSPTexture(DL++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    }
#endif

    // Lights macro? These need to be on one line.
    gSPNumLights(DL++, 1); \
    gSPLight(DL++, &ninlogolight.l[0], 1); \
    gSPLight(DL++, &ninlogolight, 2);

#if defined(VERSION_EU)
    // 0x100000000 ? 0xFFFECD34 = 0x132CC (78540 decimal)
    ambiantlight = 0xFF - ((s32) ((g_MenuTimer * 0xFF) + 0xFFFECD34) / 83);
#else
    // 0x100000000 ? 0xFFFE8F72 = 0x1708E (94350 decimal)
    ambiantlight = 0xFF - ((s32) ((g_MenuTimer * 0xFF) + 0xFFFE8F72) / 100);
#endif
    if (ambiantlight >= 0x100)
    {
        ambiantlight = 0xff;
    }
    if (ambiantlight < 0)
    {
        ambiantlight = 0;
    }

    ninlogolight.a.l.colc[2] = ambiantlight;
    ninlogolight.a.l.colc[1] = ambiantlight;
    ninlogolight.a.l.colc[0] = ambiantlight;
    ninlogolight.a.l.col[2] = ambiantlight;
    ninlogolight.a.l.col[1] = ambiantlight;
    ninlogolight.a.l.col[0] = ambiantlight;

#if defined(VERSION_EU)
    ninLogoRotRate += 0.0209439527243f;
#else
    ninLogoRotRate += 0.017453292f;
#endif

    // Not sure if this is actually an `if` block, but the sp90 variable
    // needs to be declared here for the stack to match.
    if(1)
    {
        Mtxf tmpMtx;

        matrix_4x4_set_rotation_around_y(ninLogoRotRate, &tmpMtx);
        matrix_scalar_multiply_3(ninLogoScale, (f32*)&tmpMtx);
#ifndef TARGET_N64
        if (g_PortLogoModelScale != 1.0f)
        {
            matrix_scalar_multiply_3(g_PortLogoModelScale, (f32*)&tmpMtx);
        }
#endif

#if defined(VERSION_EU)
        ninLogoScale *= 1.09647190571f;
#else
        ninLogoScale *= 1.07977f;
#endif
        if (ninLogoScale > 1.1f)
        {
            ninLogoScale = 1.1f;
        }

        matrix_4x4_set_lookat_target(&ninlogoMtxf, 0.0f, 0.0f, 4000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        matrix_4x4_multiply_in_place(&ninlogoMtxf, &tmpMtx);
        matrix_4x4_copy(&tmpMtx, &ninlogoMtxf);
    }

    ninlogo.basemtx = &ninlogoMtxf;

    ninlogo.mtxlist = dynAllocate(logoinst->obj->numMatrices << 6);

    matrix_4x4_copy(&ninlogoMtxf, ninlogo.mtxlist);

    logoinst->render_pos = (union RenderPosView*)ninlogo.mtxlist;

    modelUpdateNodeRelations(logoinst);

    ninlogo.flags = 3;
    ninlogo.zbufferenabled = FALSE;
    ninlogo.gdl = DL;

    subdraw(&ninlogo, logoinst);

    DL = ninlogo.gdl;

    for (i=0; i<logoinst->obj->numMatrices; i++)
    {
        Mtxf tmpMtxf;
        s32 padding2;

        // hack: source address steps by sizeof(Mtxf), but can't get that to match
        matrix_4x4_copy(&((s8*)logoinst->render_pos)[i*0x40], &tmpMtxf);
        matrix_4x4_f32_to_s32(&tmpMtxf, &((Mtxf*)logoinst->render_pos)[i]);

        if(i);
    }

    return DL;
}












//********************************************************************************************************
//RARE LOGO
//********************************************************************************************************

/*
 * Address: 0x7F008B58
*/
void init_menu02_rarelogo(void)
{
    setupRarewareLogoData(ptr_logo_and_walletbond_DL, 0x78000);
    sndPlaySfx(g_musicSfxBufferPtr, RARELOGO_SFX, 0);
}

void update_menu02_rareware(void) {
    return;
}

void interface_menu02_rareware(void)
{
    viSetUseZBuf(FALSE);
    if (isGunBarrelInMode2())
    {
        frontChangeMenu(MENU_EYE_INTRO, TRUE);
        return;
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON))
    {
        if (!is_first_time_on_main_menu)
        {
            frontChangeMenu(MENU_FILE_SELECT, TRUE);
            return;
        }
        prev_keypresses = TRUE;
        frontChangeMenu(MENU_EYE_INTRO, TRUE);
    }
}

Gfx * constructor_menu02_rareware(Gfx * DL) {
    return retrieve_display_rareware_logo(DL);
}











//********************************************************************************************************
//GUNBARREL
//********************************************************************************************************
void init_menu03_gunbarrel(void) {
    initializeGunBarrelIntro(ptr_logo_and_walletbond_DL, 0x78000);
    musicTrack1Play(M_INTRO);
    maybe_is_in_menu = TRUE;
}

/*
 * Address: 0x7F00920C
*/
void update_menu_03_gunbarrel(void) {
    clearChrGunModelInstances();
}

void interface_menu03_eye(void) {
    viSetUseZBuf(FALSE);
    if (isGunBarrelInMode9()) {
        frontChangeMenu(MENU_GOLDENEYE_LOGO, TRUE);
        return;
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON)) {
        if (!is_first_time_on_main_menu) {
            frontChangeMenu(MENU_FILE_SELECT, TRUE);
            return;
        }
        prev_keypresses = TRUE;
        frontChangeMenu(MENU_GOLDENEYE_LOGO, TRUE);
    }
}

Gfx * constructor_menu03_eye(Gfx * DL) {
    return renderGunbarrelEyeIntroSequence (DL);
}




//********************************************************************************************************
//GOLDENEYE LOGO
//********************************************************************************************************
void init_menu04_goldeneyelogo(void)
{
    struct coord3d pos;

    g_MenuTimer = 0;
    pos = goldeneyelogo_pos;
    load_object_fill_header(PitemZ_entries[PROP_GOLDENEYELOGO].header, PitemZ_entries[PROP_GOLDENEYELOGO].filename, ptr_logo_and_walletbond_DL, 0x3c000, 0);
    modelCalculateRwDataLen(PitemZ_entries[PROP_GOLDENEYELOGO].header);
    logoinst = modelmgrInstantiateModel(PitemZ_entries[PROP_GOLDENEYELOGO].header);
    #ifdef DEBUG
        assert(logoinst);
    #endif

    modelSetScale(logoinst, 1.0f);
    setsuboffset(logoinst, &pos);
}





void update_menu04_goldeneye(void) {
    is_first_time_on_main_menu = FALSE;
    clear_model_obj(logoinst);
}

void interface_menu04_goldeneyelogo(void)
{
#ifdef REFRESH_PAL
#define GOLDENEYELOGO_TIMER_1 50*3
#define GOLDENEYELOGO_TIMER_2 (50/2)*3
#else
#define GOLDENEYELOGO_TIMER_1 60*3
#define GOLDENEYELOGO_TIMER_2 (60/2)*3
#endif

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);
    g_MenuTimer += g_ClockTimer;
    if (((!is_first_time_on_main_menu) || (GOLDENEYELOGO_TIMER_1 < g_MenuTimer)) ||
       ((ge_logo_bool && (GOLDENEYELOGO_TIMER_2 < g_MenuTimer)))) {
        if (g_MenuTimer > GOLDENEYELOGO_TIMER_1) {
            if (prev_keypresses) {
                frontChangeMenu(MENU_FILE_SELECT,TRUE);
            }
            else {
                do_extended_cast_display(FALSE);
                frontChangeMenu(MENU_DISPLAY_CAST,TRUE);
            }
        }
        else {
            if ((joyGetButtonsPressedThisFrame(PLAYER_1,ANY_BUTTON)) || ((is_first_time_on_main_menu && ge_logo_bool))) {
                frontChangeMenu(MENU_FILE_SELECT,TRUE);
            }
        }
    }
    else {
        if (joyGetButtonsPressedThisFrame(PLAYER_1,ANY_BUTTON)) {
            ge_logo_bool = TRUE;
        }
    }
}



Gfx *constructor_menu04_goldeneyelogo(Gfx *DL)
{
    ModelRenderData gelogo;
    s32 padding[2];
    Mtxf logoMatrix;
    s32 i;
    LookAt * logoLookAt;
    Mtxf logoReflectMtx;

    gelogo = goldeneyelogo_MRD;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);

    logoLookAt = (LookAt *)dynAllocateLights(2);
    guLookAtReflect(&logoReflectMtx, logoLookAt, 0.0f, 0.0f, 4000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    // Lights macro? These need to be on one line.
    gSPNumLights(DL++, 1); \
    gSPLight(DL++, &gelogolight.l[0], 1); \
    gSPLight(DL++, &gelogolight, 2);

    // gSPLookAt macro expands to gSPLookAtX + gSPLookAtY
    gSPLookAt(DL++, logoLookAt);

    matrix_4x4_set_lookat_target(&logoMatrix, 0.0f, 0.0f, 3000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    gelogo.basemtx = &logoMatrix;
    gelogo.mtxlist = dynAllocate(logoinst->obj->numMatrices << 6);

    matrix_scalar_multiply(1.2f, logoMatrix.m[0]);
    matrix_4x4_copy(&logoMatrix, gelogo.mtxlist);
    logoinst->render_pos = (union RenderPosView*)gelogo.mtxlist;

    modelUpdateNodeRelations(logoinst);

    gelogo.flags = 3;
    gelogo.zbufferenabled = FALSE;
    gelogo.gdl = DL;

    subdraw(&gelogo, logoinst);

    DL = gelogo.gdl;

    for (i=0; i<logoinst->obj->numMatrices; i++)
    {
        Mtxf tempMtxf;
        s32 padding2;

        // hack: source address steps by sizeof(Mtxf), but can't get that to match
        matrix_4x4_copy(&((s8*)logoinst->render_pos)[i*0x40], &tempMtxf);
        matrix_4x4_f32_to_s32(&tempMtxf, &((Mtxf*)logoinst->render_pos)[i]);

        if(i);
    }

    return DL;
}



void disable_all_switches(Model *arg0)
{
    s32 i;
    ModelNode *mnode;

    for (i = 0; i < arg0->obj->numSwitches; i++)
    {
        mnode = arg0->obj->Switches[i];

        if (mnode != NULL && (mnode->Opcode & 0xff) == MODELNODE_OPCODE_SWITCH)
        {
            union ModelRwData *unmrd;
            struct ModelRwData_SwitchRecord *srecord;

            unmrd = modelGetNodeRwData(arg0, mnode);
            srecord = (struct ModelRwData_SwitchRecord *)unmrd;
            srecord->visible = 0;
        }
    }
}


void set_item_visibility_in_objinstance(Model* objinstance, s32 item, s32 mode)
{
    if (objinstance->obj->Switches[item] != NULL)
    {
        ModelRwData_SwitchRecord *rwdata = &modelGetNodeRwData(objinstance, objinstance->obj->Switches[item])->Switch;

        rwdata->visible = mode;
    }
}


//********************************************************************************************************
//WALLETINIT
//********************************************************************************************************

void select_load_bond_picture(Model *objinstance, u32 bondID)
{
#ifdef ALL_BONDS
    set_item_visibility_in_objinstance(objinstance, SW_BROSNAN, (bondID == BOND_BROSNAN)); //brosnan picture
    set_item_visibility_in_objinstance(objinstance, SW_CONNERY, (bondID == BOND_CONNERY));
    set_item_visibility_in_objinstance(objinstance, SW_DALTON, (bondID == BOND_DALTON));
    set_item_visibility_in_objinstance(objinstance, SW_MOORE, (bondID == BOND_MOORE));
    set_item_visibility_in_objinstance(objinstance, SW_BROSNANCOVER, (bondID == BOND_BROSNAN)); //bigger brosnan picture
    set_item_visibility_in_objinstance(objinstance, SW_CONNERYCOVER, (bondID == BOND_CONNERY));
    set_item_visibility_in_objinstance(objinstance, SW_DALTONCOVER, (bondID == BOND_DALTON));
    set_item_visibility_in_objinstance(objinstance, SW_MOORECOVER, (bondID == BOND_MOORE));
#else
    set_item_visibility_in_objinstance(objinstance,SW_BROSNAN,TRUE); //brosnan picture
    set_item_visibility_in_objinstance(objinstance,SW_CONNERY,FALSE);
    set_item_visibility_in_objinstance(objinstance,SW_DALTON,FALSE);
    set_item_visibility_in_objinstance(objinstance,SW_MOORE,FALSE);
    set_item_visibility_in_objinstance(objinstance,SW_BROSNANCOVER,TRUE); //bigger brosnan picture
    set_item_visibility_in_objinstance(objinstance,SW_CONNERYCOVER,FALSE);
    set_item_visibility_in_objinstance(objinstance,SW_DALTONCOVER,FALSE);
    set_item_visibility_in_objinstance(objinstance,SW_MOORECOVER,FALSE);
#endif
}





/**
 * Address 0x7F00B8AC NTSC
*/
void load_walletbond(void)
{
    ModelNode *mnode;
    s32 i;

    if (walletinst[0] == NULL)
    {
        load_object_fill_header(
            PitemZ_entries[PROP_WALLETBOND].header,
            (s8*)PitemZ_entries[PROP_WALLETBOND].filename,
            (u8*)ptr_logo_and_walletbond_DL,
            0xA000,
            0);

        modelCalculateRwDataLen(PitemZ_entries[PROP_WALLETBOND].header);

        for (i = FOLDER1; i < MAX_FOLDER_COUNT; i++)
        {
            walletinst[i]  = modelmgrInstantiateModelWithAnim(PitemZ_entries[PROP_WALLETBOND].header);
            #ifdef DEBUG
            assert(walletinst[i]);
            #endif
            modelSetScale(walletinst[i], 1.0f);
        }

        mnode = PitemZ_entries[PROP_WALLETBOND].header->Switches[GFXHIT0_PICS];

        if (mnode != NULL)
        {
            struct ModelRoData_DisplayList_CollisionRecord *srecord;
            struct ModelNode *b;
            Gfx *arg0;

            b = (struct ModelNode *)mnode;
            srecord = b->Data;

#ifdef TARGET_N64
            arg0 = (s32)srecord->BaseAddr + ((s32)srecord->Primary & 0xffffff);
            bgApplyDynamicCCRMLUT(arg0, NULL, CCRMLUT_WALLETBOND);
#else
            /* Primary already holds the converted host display list
             * (portConvertModelGdl/modelNodeReplaceGdl), so the N64 base+offset
             * reconstruction would mangle a real pointer and crash on the
             * file-select screen. */
            arg0 = srecord->Primary;
            if (arg0 != NULL)
            {
                bgApplyDynamicCCRMLUT(arg0, NULL, CCRMLUT_WALLETBOND);
            }
#endif
        }
    }
}




void frontCleanUpWalletBond(void)
{
    s32 i;

    for (i = FOLDER1; i < MAX_FOLDER_COUNT; i++)
    {
        if (walletinst[i] == NULL) { continue; }

        clear_aircraft_model_obj(walletinst[i]);
        walletinst[i] = NULL;
    }
}



//********************************************************************************************************
//FILE SELECT
//********************************************************************************************************
void init_menu05_fileselect(void)
{
    s32 size = 0x6e000;
    Gfx* DL = (Gfx *)(ptr_logo_and_walletbond_DL + 4096 * 10);
    int i;

    prev_keypresses = FALSE;


    if (selected_folder_num < FOLDER1)
    {
        selected_folder_num = FOLDER1;
    }
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    folder_selected_for_deletion = FOLDER_INVALID;
    folder_selected_for_deletion_choice = FOLDER2;
#ifndef TARGET_N64
    port_filesel_options_selected = FALSE;
    portDlgClose(); /* an Exit Game box left up by the idle timeout must not come back armed */
#endif
    sub_GAME_7F008DE4(&DL, &size);
    load_walletbond();
    if (maybe_is_in_menu){
        musicTrack1Play(M_FOLDERS);
        maybe_is_in_menu = FALSE;
    }
    g_MenuTimer = 0;
    for(i=1;i<CHEAT_INVALID;i++){
        g_CheatActivated[i] = FALSE;
    }
    g_AppendCheatSinglePlayer = FALSE;
    g_AppendCheatMultiPlayer = FALSE;
}



void update_menu05_filesel(void)
{
    if ((menu_update == MENU_LEGAL_SCREEN) || (maybe_prev_menu == MENU_LEGAL_SCREEN))
    {
        frontCleanUpWalletBond();
    }
}



s32 get_chapter_briefing_entry(s32 briefing)
{
    s32 i;

    for (i = briefing; i >= 0; i--)
    {
        if (mission_folder_setup_entries[i].type != MISSION_PART)
        {
            return i;
        }
    }

    return -1;
}


void toggle_deletion_menu_for_folder(int index)
{
  struct coord2d local_8;

  transform3Dto2DCoords(&folderpositions_camspace[index],&local_8);
  if (frontCursorSnapAllowed())
  {
    cursor_h_pos = local_8.x + -1.0f;
    cursor_v_pos = local_8.y + 20.0f;
  }
}


/**
 * Address: 7F00BBCC
 * 
 * Tick function for the file folder select screen. Rebuild's each folder model's render matrix
 * then handles input. Also keeps track of the 30 second idle timeout for going back to the legal screen.
 */
s32 interface_menu05_fileselect(void)
{
    s32 walletnum;
    s32 foldernum;
    Mtxf lookatmtx;
    Mtxf walletmtx;
    struct coord3d *folderpos;

    /**
     *  Accumulate the menu timer if there's been no button inputs and no more than marginal stick input.
     *  The menu timer is checked at the end of this function to determine if we should switch to the legal screen.
     */
    if (((((joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON) != 0) || (joyGetStickX(0) < -5)) || (joyGetStickX(0) >= 6)) || (joyGetStickY(0) < -5)) || (joyGetStickY(0) >= 6))
    {
        g_MenuTimer = 0;
    }
    else
    {
        g_MenuTimer += g_ClockTimer;
    }

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    for (walletnum = 0; walletnum < MAX_FOLDER_COUNT; walletnum++)
    {
        folderpos = &folderpositions[walletnum];

        matrix_4x4_set_lookat_target(&lookatmtx, 0.0f, 0.0f, 4000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        matrix_4x4_set_identity_and_position(folderpos, &walletmtx);
        matrix_scalar_multiply(0.37f, (f32*)&walletmtx);
        matrix_4x4_multiply_in_place(&lookatmtx, &walletmtx);

        walletinst[walletnum]->render_pos = dynAllocate(walletinst[walletnum]->obj->numMatrices << 6);
        matrix_4x4_copy(&walletmtx, &walletinst[walletnum]->render_pos->pos);

        folderpositions_camspace[walletnum].f[0] = walletinst[walletnum]->render_pos->pos.m[3][0];
        folderpositions_camspace[walletnum].f[1] = walletinst[walletnum]->render_pos->pos.m[3][1];
        folderpositions_camspace[walletnum].f[2] = walletinst[walletnum]->render_pos->pos.m[3][2];

        disable_all_switches(walletinst[walletnum]);

        select_load_bond_picture(walletinst[walletnum], fileGetBondForFolder(walletnum));
        set_item_visibility_in_objinstance(walletinst[walletnum], 0xE, 1);
        set_item_visibility_in_objinstance(walletinst[walletnum], 0xD, 1);
    }

    if (selected_folder_num >= FOLDER1)
    {
        if (selected_folder_num == RAMROM_FOLDERNUM)
        {
            selected_folder_num = selected_folder_num_copy;
        }

        if ((selected_folder_num < FOLDER1) || (selected_folder_num >= MAX_FOLDER_COUNT))
        {
            selected_folder_num = FOLDER1;
        }

        toggle_deletion_menu_for_folder(selected_folder_num);
        selected_folder_num = -1;
    }

    if (folder_selected_for_deletion >= 0)
    {
        if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD | L_TRIG | L_CBUTTONS) != 0) && (folder_selected_for_deletion_choice == 0))
        {
            folder_selected_for_deletion_choice = 1;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
        else if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD | R_TRIG | R_CBUTTONS) != 0) && (folder_selected_for_deletion_choice != 0))
        {
            folder_selected_for_deletion_choice = 0;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
        if ((joyGetStickX(0) < -0x2D) && (folder_selected_for_deletion_choice == 0))
        {
            folder_selected_for_deletion_choice = 1;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
        else if ((joyGetStickX(0) >= 0x2E) && (folder_selected_for_deletion_choice != 0))
        {
            folder_selected_for_deletion_choice = 0;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
        if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON) != 0)
        {
            if (folder_selected_for_deletion_choice == 0)
            {
                fileDeleteSaveForFolder(folder_selected_for_deletion);
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
            }
            else
            {
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
            }
            toggle_deletion_menu_for_folder(folder_selected_for_deletion);
            folder_selected_for_deletion = -1;
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON) != 0)
        {
            toggle_deletion_menu_for_folder(folder_selected_for_deletion);
            folder_selected_for_deletion = -1;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
        }
    }
#ifndef TARGET_N64
    else if (portDlgIsOpen())
    {   /* EXIT GAME "Are you sure?" - Yes / No. The cursor stays live (mouse
         * users click a button), and the box itself owns all of that now. */
        if (portDlgTick() && portDlgSaidYes())
        {
#ifdef GE_VIDEO
            extern void portRequestQuit(void); /* video.c: SDL_QUIT, the close box's path (ini saved by the atexit hook) */
            portRequestQuit();
#else
            extern void exit(int);
            exit(0);
#endif
        }
    }
#endif
    else
    {
        for (foldernum = FOLDER1; foldernum < MAX_FOLDER_COUNT; foldernum++)
        {
            f32 xmax;
            f32 xmin;
            f32 ymax;
            f32 ymin;

            struct rectbbox folderbbox;

            LEVEL_SOLO_SEQUENCE higheststage;
            DIFFICULTY highestdifficulty;

            modelGetXYExtents(walletinst[foldernum], &xmax, &xmin, &ymax, &ymin);
#ifdef TARGET_N64
            projectRectCornersTo2D(&folderpositions_camspace[foldernum], &xmin, &ymin, &folderbbox.right, &folderbbox.left);
#else
            /* projectRectCornersTo2D reads coord pairs and writes four floats,
             * and the N64 call relied on IDO's stack/struct layout, which GCC
             * doesn't keep (leaving bbox fields uninitialised); pass explicit
             * pairs and assign them orientation-proof. */
            {
                struct coord2d xpair, ypair, ca, cb;
                xpair.f[0] = xmin;
                xpair.f[1] = xmax;
                ypair.f[0] = ymin;
                ypair.f[1] = ymax;
                projectRectCornersTo2D(&folderpositions_camspace[foldernum], &xpair, &ypair, &ca, &cb);
                folderbbox.right = ca.f[0] < cb.f[0] ? ca.f[0] : cb.f[0];
                folderbbox.left = ca.f[0] < cb.f[0] ? cb.f[0] : ca.f[0];
                folderbbox.down = ca.f[1] < cb.f[1] ? ca.f[1] : cb.f[1];
                folderbbox.up = ca.f[1] < cb.f[1] ? cb.f[1] : ca.f[1];
            }
#endif

#ifndef TARGET_N64
            { /* diagnostic (GE_DUMP_BOND): on any press, dump cursor vs
               * this folder's hover bbox - the Linux default-folder
               * select-refusal hunt. */
                extern char *getenv(const char *);
                static int dump = -1;
                if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
                if (dump && joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON) != 0)
                {
                    extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
                    f32 v[8];
                    v[0] = (f32)foldernum;
                    v[1] = cursor_h_pos;
                    v[2] = cursor_v_pos;
                    v[3] = folderbbox.right;
                    v[4] = folderbbox.left;
                    v[5] = folderbbox.down;
                    v[6] = folderbbox.up;
                    v[7] = (f32)folder_selection_screen_option_icon;
                    ge_dbg_vec("folderhit n/ch/cv/r/l/d/u/icon", v, 8);
                }
            }
#endif

            if ((folderbbox.right <= cursor_h_pos)
                && (cursor_h_pos <= folderbbox.left)
                && (folderbbox.down <= cursor_v_pos)
                && (cursor_v_pos <= folderbbox.up))
            {
                fileGetHighestStageDifficultyCompletedForFolder(foldernum, &higheststage, &highestdifficulty);
#ifdef DEBUG
                osSyncPrintf("selected file %d\n", foldernum);
#endif

                if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON) != 0)
                {
                    if (folder_selection_screen_option_icon == 0)
                    {
                        selected_folder_num = foldernum;
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, PAPER_TURN_SFX, NULL);
                    }
                    else if (folder_selection_screen_option_icon == 1)
                    {
                        fileCopyFolderToFirstFree(foldernum);
                        folder_selection_screen_option_icon = 0;
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, COPY_FILE_SFX, NULL);
                    }
                    else if (folder_selection_screen_option_icon == 2)
                    {
                        if ((higheststage >= SP_LEVEL_DAM) && (highestdifficulty >= DIFFICULTY_AGENT))
                        {
                            folder_selected_for_deletion = foldernum;
                            folder_selected_for_deletion_choice = 1;
                        }

                        folder_selection_screen_option_icon = 0;
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
                    }

                    break;
                }
            }
        }

#ifndef TARGET_N64
        if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON) != 0)
        {   /* the four bottom-bar buttons; Copy / Erase arm the cursor icon
             * exactly as Rare's did, the two new ones act on their own */
            s32 hit = -1;
            s32 b;

            for (b = 0; b < PORT_FSBTN_COUNT; b++)
            {
                if ((port_filesel_btn_bound[b].left <= cursor_h_pos)
                    && (cursor_h_pos <= port_filesel_btn_bound[b].right)
                    && (port_filesel_btn_bound[b].up <= cursor_v_pos)
                    && (cursor_v_pos <= port_filesel_btn_bound[b].down))
                {
                    hit = b;
                    break;
                }
            }

            if ((hit == PORT_FSBTN_COPY) || (hit == PORT_FSBTN_ERASE))
            {
                folder_selection_screen_option_icon = (hit == PORT_FSBTN_COPY) ? 1 : 2;
                sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_LOCK_SFX, NULL);
            }
            else if (hit == PORT_FSBTN_OPTIONS)
            {
                folder_selection_screen_option_icon = 0;
                port_filesel_options_selected = TRUE;
                sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, NULL);
            }
            else if (hit == PORT_FSBTN_EXIT)
            {
                folder_selection_screen_option_icon = 0;
                portDlgOpen("Are you sure?\n", "Yes\n", "No\n", 0);
                sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
            }
            else if (folder_selection_screen_option_icon != 0)
            {
                folder_selection_screen_option_icon = 0;
                sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
            }
        }
#else
        if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON) != 0)
        {
            if ((folder_option_COPY_bound.left <= cursor_h_pos)
                && (cursor_h_pos <= folder_option_COPY_bound.right)
                && (folder_option_COPY_bound.up <= cursor_v_pos)
                && (cursor_v_pos <= folder_option_COPY_bound.down))
            {
                folder_selection_screen_option_icon = 1;
                sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_LOCK_SFX, NULL);
            }
            else if ((folder_option_ERASE_bound.left <= cursor_h_pos)
                && (cursor_h_pos <= folder_option_ERASE_bound.right)
                && (folder_option_ERASE_bound.up <= cursor_v_pos)
                && (cursor_v_pos <= folder_option_ERASE_bound.down))
            {
                folder_selection_screen_option_icon = 2;
                sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_LOCK_SFX, NULL);
            }
            else if (folder_selection_screen_option_icon != 0)
            {
                folder_selection_screen_option_icon = 0;
                sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
            }
        }
#endif
        else if ((joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON) != 0)
            && (folder_selection_screen_option_icon != 0))
        {
            folder_selection_screen_option_icon = 0;
            sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
        }

        frontUpdateControlStickPosition();
    }

#ifndef TARGET_N64
    if (port_filesel_options_selected)
    {   /* OPTIONS: its page draws folder 1's paper the way the Files menu
         * draws a picked folder's (frontSetupMenuBackground needs a folder) */
        port_filesel_options_selected = FALSE;
        selected_folder_num = FOLDER1;
        frontChangeMenu(MENU_PORT_OPTIONS, FALSE);
        setCursorPOSforMode(0);

        return 0;
    }
#endif
    if (selected_folder_num >= FOLDER1)
    {
        frontChangeMenu(MENU_MODE_SELECT, FALSE);
        setCursorPOSforMode(0);

        return 0; /* was a bare return in a non-void fn (UB at -O2); sole caller ignores it */
    }

    // Change to the legal screen if 30 seconds of no input have elapsed
#if defined(VERSION_EU)
    if (g_MenuTimer >= 1501) // PAL (50fps): 30 seconds + 1 frame
#else
    if (g_MenuTimer >= 1801) // NTSC (60fps): 30 seconds + 1 frame
#endif
    {
        frontChangeMenu(MENU_LEGAL_SCREEN, TRUE);
    }
}


char* get_ptr_difficulty_name(s32 difficulty)
{
    char* text = NULL;
    switch (difficulty)
    {
    case 0:
        text = langGet(getStringID(LTITLE, TITLE_STR_19_AGENT)); //Agent
        break;
    case 1:
        text = langGet(getStringID(LTITLE, TITLE_STR_20_SECRETAGENT)); //Secret Agent
        break;
    case 2:
        text = langGet(getStringID(LTITLE, TITLE_STR_21_00AGENT)); //00 Agent
        break;
    case 3:
        text = langGet(getStringID(LTITLE, TITLE_STR_22_007)); //007
        break;
    }
    return text;
}


#ifndef TARGET_N64
/* ---- A MODAL BOX, FOR ANY SCREEN ------------------------------------------
 *
 * A reusable confirm dialog, lifted out of the Files screen's Exit Game prompt
 * so anything that needs confirming (deleting a saved layout, resetting a pad's
 * bindings, discarding an edit) can use it.
 *
 * It is modal: while one is open the screen underneath takes no input, so a
 * caller runs portDlgTick() first and does nothing else on a frame where it
 * returns 0.
 *
 * The answer is a pick, so it works on both pad and mouse: D-pad or C
 * left/right move the pick, moving the cursor over an answer picks it (a
 * resting cursor never overrides the pad), A acts on the pick and B cancels.
 * The caller chooses the starting pick; for a destructive question it should be
 * the harmless answer, so a stray press never deletes anything.
 */
/* The box is 140x52 at y 135 on the frontend's 440x330, where the exit prompt always sat.
 * It is deliberately not centred, so it doesn't overlap the Files screen's
 * photographs, which draw on top. */
#define PORT_DLG_W        140   /* a MINIMUM: a long question widens the box */
#define PORT_DLG_PAD      12    /* clear space either side of the question */
#define PORT_DLG_H        52
#define PORT_DLG_T        135
#define PORT_DLG_ROW2     27    /* answers, down from the box top */
#define PORT_DLG_SHEET    0x000000B4 /* black @ ~70%: the erase popup's 20% was hard to read over the wallets */
#define PORT_DLG_PICK     0xFFFFFF60 /* the picked answer: white @ ~38% - a black box vanished on the dark sheet */
#define PORT_DLG_TEXT     0xEBD879FF

#define PORT_DLG_MAX_ANSWERS 3
static s32 port_dlg_open;
static s32 port_dlg_pick;
static s32 port_dlg_done;          /* 0 = running, 1 = answered, -1 = cancelled */
static s32 port_dlg_n;             /* answers, drawn left to right in index order */
static char port_dlg_title[48];
static const char *port_dlg_answer[PORT_DLG_MAX_ANSWERS];
static struct rectbbox port_dlg_bound[PORT_DLG_MAX_ANSWERS];
static f32 port_dlg_cursor_h;      /* hover picks only on MOTION */
static f32 port_dlg_cursor_v;

/**
 * Any number of answers up to three, left to right (e.g. the "manage config"
 * popup: save / delete / cancel). `def` is the index the
 * pick starts on - the harmless one.
 */
static void portDlgOpenN(const char *title, const char *const *answers, s32 n, s32 def)
{
    s32 i;

    snprintf(port_dlg_title, sizeof(port_dlg_title), "%s", title);
    port_dlg_n = (n < 1) ? 1 : (n > PORT_DLG_MAX_ANSWERS) ? PORT_DLG_MAX_ANSWERS : n;
    for (i = 0; i < port_dlg_n; i++)
    {
        port_dlg_answer[i] = answers[i];
    }
    port_dlg_pick = (def < 0 || def >= port_dlg_n) ? port_dlg_n - 1 : def;
    port_dlg_done = 0;
    port_dlg_open = TRUE;
    port_dlg_cursor_h = cursor_h_pos;
    port_dlg_cursor_v = cursor_v_pos;
}

/** The yes / no form: YES sits left as the exit prompt always had it. `def`
 * nonzero starts on yes. */
static void portDlgOpen(const char *title, const char *yes, const char *no, s32 def)
{
    const char *a[2];

    a[0] = (yes != NULL) ? yes : "Yes\n";
    a[1] = (no != NULL) ? no : "No\n";
    portDlgOpenN(title, a, 2, def ? 0 : 1);
}

static s32 portDlgIsOpen(void)
{
    return port_dlg_open;
}

static void portDlgClose(void)
{
    port_dlg_open = FALSE;
    port_dlg_done = 0;
}

/** 1 = it closed this frame; portDlgSaidYes() says which way. */
static s32 portDlgTick(void)
{
    s32 pick = port_dlg_pick;
    s32 moved;
    s32 i;

    if (!port_dlg_open)
    {
        return 0;
    }
    moved = (cursor_h_pos != port_dlg_cursor_h) || (cursor_v_pos != port_dlg_cursor_v);
    port_dlg_cursor_h = cursor_h_pos;
    port_dlg_cursor_v = cursor_v_pos;

    if (joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD | L_CBUTTONS) != 0)
    {
        pick = (pick > 0) ? pick - 1 : 0;
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD | R_CBUTTONS) != 0)
    {
        pick = (pick < port_dlg_n - 1) ? pick + 1 : port_dlg_n - 1;
    }
    for (i = 0; moved && i < port_dlg_n; i++)
    {
        if ((port_dlg_bound[i].left <= cursor_h_pos) && (cursor_h_pos <= port_dlg_bound[i].right)
            && (port_dlg_bound[i].up <= cursor_v_pos) && (cursor_v_pos <= port_dlg_bound[i].down))
        {
            pick = i;
        }
    }
    if (pick != port_dlg_pick)
    {
        port_dlg_pick = pick;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON) != 0)
    {
        port_dlg_done = 1;
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON) != 0)
    {
        port_dlg_done = -1;
    }
    frontUpdateControlStickPosition();
    if (port_dlg_done != 0)
    {
        port_dlg_open = FALSE;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
        return 1;
    }
    return 0;
}

/** 1 = they picked YES (the first answer) and meant it. Cancel is never a yes. */
static s32 portDlgSaidYes(void)
{
    return port_dlg_done > 0 && port_dlg_pick == 0;
}

/** The answer they picked, by index; -1 = cancelled with B. */
static s32 portDlgPick(void)
{
    return (port_dlg_done > 0) ? port_dlg_pick : -1;
}

/** The same window with no answers: a title and a dimmer note under it,
 *  drawn while something else owns the input (the rebind capture). */
static Gfx *portDlgDrawNote(Gfx *DL, const char *title, const char *note)
{
    const s32 t = PORT_DLG_T;
    s32 th = 0, tw = 0, nh = 0, nw = 0;
    s32 w, l, x, y;

    textMeasure(&th, &tw, (char *) title, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    textMeasure(&nh, &nw, (char *) note, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    w = ((tw > nw) ? tw : nw) + (PORT_DLG_PAD * 2);
    if (w < PORT_DLG_W)
    {
        w = PORT_DLG_W;
    }
    l = (440 - w) / 2;
    DL = microcode_constructor_related_to_menus(DL, l, t, l + w, t + PORT_DLG_H, PORT_DLG_SHEET);
    x = l + (w / 2) - (tw / 2);
    y = t + 6;
    DL = textRender(DL, &x, &y, (char *) title, ptrFontZurichBoldChars, ptrFontZurichBold,
                    PORT_DLG_TEXT, viGetX(), viGetY(), 0, 0);
    x = l + (w / 2) - (nw / 2);
    y = t + PORT_DLG_ROW2;
    DL = textRender(DL, &x, &y, (char *) note, ptrFontZurichBoldChars, ptrFontZurichBold,
                    0xFFFFFFB0, viGetX(), viGetY(), 0, 0);
    return DL;
}

/** Draw it over whatever is underneath, rebuilding the hit boxes as it goes. */
static Gfx *portDlgDraw(Gfx *DL)
{
    const s32 t = PORT_DLG_T;
    struct point2d textpos;
    struct point2d textsize;
    s32 w;
    s32 l;
    s32 i;

    /* Measure the question first: the box grows to fit long caller strings and stays centred,
     * while a short question still gets the exit prompt's exact 140x52 at x 150. */
    textsize.p[0] = 0;
    textsize.p[1] = 0;
    textMeasure(&textsize.p[1], &textsize.p[0], port_dlg_title, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    w = textsize.p[0] + (PORT_DLG_PAD * 2);
    if (w < PORT_DLG_W)
    {
        w = PORT_DLG_W;
    }
    if (port_dlg_n > 2 && w < PORT_DLG_W * 3 / 2)
    {
        w = PORT_DLG_W * 3 / 2;   /* three answers need the room */
    }
    l = (440 - w) / 2;

    DL = microcode_constructor_related_to_menus(DL, l, t, l + w, t + PORT_DLG_H, PORT_DLG_SHEET);

    textpos.p[1] = l + (w / 2) - (textsize.p[0] / 2);
    textpos.p[0] = t + 6;
    DL = textRender(DL, &textpos.p[1], &textpos.p[0], port_dlg_title, ptrFontZurichBoldChars, ptrFontZurichBold,
            PORT_DLG_TEXT, viGetX(), viGetY(), 0, 0);

    for (i = 0; i < port_dlg_n; i++)
    {   /* index order, left to right: YES sits left, as the exit prompt has
         * always had it; a three-way box spreads evenly */
        char *text = (char *) port_dlg_answer[i];
        const s32 cx = l + (w * (2 * i + 1)) / (2 * port_dlg_n);

        textsize.p[0] = 0;
        textsize.p[1] = 0;
        textMeasure(&textsize.p[1], &textsize.p[0], text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        textpos.p[1] = cx - (textsize.p[0] / 2);
        textpos.p[0] = t + PORT_DLG_ROW2;

        port_dlg_bound[i].left = (f32) (textpos.p[1] - 6);
        port_dlg_bound[i].right = (f32) (textpos.p[1] + textsize.p[0] + 6);
        port_dlg_bound[i].up = (f32) (textpos.p[0] - 4);
        port_dlg_bound[i].down = (f32) (textpos.p[0] + textsize.p[1] + 4);

        if (port_dlg_pick == i)
        {
            DL = microcode_constructor_related_to_menus(DL, textpos.p[1] - 1, textpos.p[0] - 1,
                    textsize.p[0] + textpos.p[1] + 3, textsize.p[1] + textpos.p[0], PORT_DLG_PICK);
            DL = textRender(DL, &textpos.p[1], &textpos.p[0], text, ptrFontZurichBoldChars, ptrFontZurichBold,
                    -1, viGetX(), viGetY(), 0, 0);
        }
        else
        {
            DL = textRender(DL, &textpos.p[1], &textpos.p[0], text, ptrFontZurichBoldChars, ptrFontZurichBold,
                    PORT_DLG_TEXT, viGetX(), viGetY(), 0, 0);
        }
    }
    return DL;
}

#endif /* !TARGET_N64 */


/**
 * Address: 7F00C408
 * 
 * Render the file folder select screen. That includes the background texture, four 3D wallet models, the progress text written on them,
 * the crosshair cursor, and the "SELECT FILE", "Copy", and "Erase" text and icons.
 */
Gfx *constructor_menu05_fileselect(Gfx *DL)
{
    s32 foldernum;
    struct FolderSelectColour topcolour;
    struct FolderSelectColour bottomcolour;
    s32 j;
    s32 i;
    u8 *langp;
    s32 chapterindex;                       // index of the chapter (MISSION_HEADER) row in mission_folder_setup_entries
    ModelRenderData renderdata;
    s32 missionindex;                       // index of the mission (MISSION_PART) row in mission_folder_setup_entries
    Mtxf mtx;
    f32 folderx;
    struct point2d textpos;
    struct point2d textsize;
    LEVEL_SOLO_SEQUENCE higheststage;
    DIFFICULTY highestdifficulty;
    struct coord2d folderpos2d;             // folder position projected to screen space

    topcolour = unknown_folderselect_constructor;
    bottomcolour = unknown_folderselect_constructor_0;

    /** 
     * The background image doesn't fully cover the screen so it's necessary to clear the back framebuffer to opaque black.
     * This creates the impression of the folder menu having a black border.
     */ 
    DL = clear_framebuffer_black(DL);

    gDPSetCombineLERP(DL++, TEXEL0, PRIMITIVE, ENV_ALPHA, PRIMITIVE, 0, 0, 0, PRIMITIVE, TEXEL0, PRIMITIVE, ENV_ALPHA, PRIMITIVE, 0, 0, 0, PRIMITIVE);
    gDPSetEnvColor(DL++, 0xFF, 0xFF, 0xFF, 0x14);

    // Render the gun barrel background image.
    DL = titleRenderFolderMenuBackground(DL, (s32) floorFloat(((f32) viGetX() * -80.0f) / 1280.0f), &topcolour, &bottomcolour);

    gSPDisplayList(DL++, &dlBasicGeometry);
    gSPDisplayList(DL++, &dlFastPipelineSetup);

    for (j = 3; j >= 0; j--)
    {
        // struct copy
        renderdata = unknown_folderselect;

        renderdata.flags = 3;
        renderdata.zbufferenabled = FALSE;
        renderdata.gdl = DL;

        // Render the folder wall model.
        subdraw(&renderdata, walletinst[j]);

        DL = renderdata.gdl;

        for (i=0; i < walletinst[j]->obj->numMatrices; i++)
        {
            matrix_4x4_copy((Mtxf*)&((s8*)walletinst[j]->render_pos)[i * sizeof(Mtxf)], &mtx);
            matrix_4x4_f32_to_s32(&mtx, &((Mtxf*)walletinst[j]->render_pos)[i]);

        }
    }

    DL = microcode_constructor(DL);
    setTextSpacingInverted(0);

    for (foldernum = FOLDER1; foldernum < MAX_FOLDER_COUNT; foldernum++)
    {
        /* [14], not [4]: get_ptr_difficulty_name can return "Secret
         * Agent" (12 chars) or "00 Agent" (8 chars); + strcat's "\n" +
         * null terminator needs 14 for the longest case. The 4-byte
         * buffer overflowed on every strcpy/strcat here except plain
         * "007". */
        char difficultytext[14]; // spD0
        char missiontext[18];   // spBC: "Mission <chapter>.<part>\n"
        s32 padding3;
        struct coord3d * folderworldpos;

        folderworldpos = &folderpositions_camspace[foldernum];

        transform3Dto2DCoords(folderworldpos, &folderpos2d);

        if (foldernum == folder_selected_for_deletion)
        {
            textpos.p[1] = (s32) floorFloat(folderpos2d.f[0]) - 0x31;
            textpos.p[0] = (s32) floorFloat(folderpos2d.f[1]) + 0x19;

            DL = microcode_constructor_related_to_menus(DL, textpos.p[1], textpos.p[0], textpos.p[1] + 0x63, textpos.p[0] + 0x2A, 0x32);

            langp = langGet(getStringID(LTITLE, TITLE_STR_23_ERASEFILE)); /* Erase file? */
            textpos.p[1] = (s32) floorFloat(folderpos2d.f[0]) - 0x2F;
            textpos.p[0] = (s32) floorFloat(folderpos2d.f[1]) + 0x1E;

            DL = textRender(DL, &textpos.p[1], &textpos.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, 0xEBD879FF, viGetX(), viGetY(), 0, 0);

            langp = langGet(getStringID(LTITLE, TITLE_STR_24_CANCEL)); /* cancel */
            textpos.p[1] = (s32) floorFloat(folderpos2d.f[0]) - 0x2F;
            textpos.p[0] = (s32) floorFloat(folderpos2d.f[1]) + 0x32;

            if (folder_selected_for_deletion_choice != 0)
            {
                textsize.p[0] = 0;
                textsize.p[1] = 0;
                textMeasure(&textsize.p[1], &textsize.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                DL = microcode_constructor_related_to_menus(DL, textpos.p[1] - 1, textpos.p[0] - 1, textsize.p[0] + textpos.p[1] + 3, textsize.p[1] + textpos.p[0], 0x32);
                DL = textRender(DL, &textpos.p[1], &textpos.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
            }
            else
            {
                DL = textRender(DL, &textpos.p[1], &textpos.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, 0xEBD879FF, viGetX(), viGetY(), 0, 0);
            }

            langp = langGet(getStringID(LTITLE, TITLE_STR_25_CONFIRM)); /* confirm */

            folderx = floorFloat(folderpos2d.f[0]);
            textpos.p[1] = (((j_text_trigger != 0) ? 0x17 : -1) + (s32) folderx) - 1;
            textpos.p[0] = (s32) floorFloat(folderpos2d.f[1]) + 0x32;

            if (folder_selected_for_deletion_choice != 0)
            {
                DL = textRender(DL, &textpos.p[1], &textpos.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, 0xEBD879FF, viGetX(), viGetY(), 0, 0);
            }
            else
            {
                textsize.p[0] = 0;
                textsize.p[1] = 0;
                textMeasure(&textsize.p[1], &textsize.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                DL = microcode_constructor_related_to_menus(DL, textpos.p[1] - 1, textpos.p[0] - 1, textsize.p[0] + textpos.p[1] + 3, textsize.p[1] + textpos.p[0], 0x32);
                DL = textRender(DL, &textpos.p[1], &textpos.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
            }

        }
        else
        {
            fileGetHighestStageDifficultyCompletedForFolder(foldernum, &higheststage, &highestdifficulty);

            if ((higheststage >= SP_LEVEL_DAM) && (highestdifficulty >= DIFFICULTY_AGENT))
            {
                langp = get_ptr_difficulty_name(highestdifficulty);

                if (langp != NULL)
                {
                    strcpy((char *)&difficultytext, langp);
                    strcat((char *)&difficultytext, "\n");

                    textsize.p[0] = 0;
                    textsize.p[1] = 0;

                    textMeasure(&textsize.p[1], &textsize.p[0], (char *)&difficultytext, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                    textpos.p[1] = ((s32) floorFloat(folderpos2d.f[0]) - (textsize.p[0] / 2)) - 1;
                    textpos.p[0] = (s32) floorFloat(folderpos2d.f[1]) + 0x15;
                    DL = textRender(DL, &textpos.p[1], &textpos.p[0], (char *)&difficultytext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xEBD879FF, viGetX(), viGetY(), 0, 0);
                }

                if (highestdifficulty != DIFFICULTY_007)
                {
                    chapterindex = -1;

                    strcpy(missiontext, langGet(getStringID(LTITLE, TITLE_STR_26_MISSION))); /* Mission */

                    for (missionindex = 0; mission_folder_setup_entries[missionindex].folder_text_preset != 0; missionindex++)
                    {
                        if (higheststage == mission_folder_setup_entries[missionindex].mission_num)
                        {
                            chapterindex = get_chapter_briefing_entry(missionindex);
                            break;
                        }
                    }

                    if (chapterindex >= 0)
                    {
                        strcat((char *)missiontext, (char *)mission_folder_setup_entries[chapterindex].string_ptr);
                        strcat((char *)missiontext, ".");
                    }

                    strcat((char *)missiontext, (char *)mission_folder_setup_entries[missionindex].string_ptr);
                    strcat((char *)missiontext, "\n");
                    textsize.p[0] = 0;
                    textsize.p[1] = 0;

                    textMeasure(&textsize.p[1], &textsize.p[0], (char *)missiontext, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                    textpos.p[1] = ((s32) floorFloat(folderpos2d.f[0]) - (textsize.p[0] / 2)) - 1;
                    textpos.p[0] = (s32) floorFloat(folderpos2d.f[1]) + 0x2D;

                    DL = textRender(DL, &textpos.p[1], &textpos.p[0], (char *)missiontext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xEBD879FF, viGetX(), viGetY(), 0, 0);
                }
            }
        }
    }

    {
        // The above for loops makes a lot of compiler temp variables on the stack,
        // These need to be declared sometime after the above for loop.
#ifdef TARGET_N64
        struct coord2d copypos;
        struct coord2d copyhalfsize;
        struct coord2d erasepos;
        struct coord2d erasehalfsize;
#endif
        struct coord2d selectpos;
        struct coord2d selecthalfsize;

        setTextSpacingInverted(0);

#ifndef TARGET_N64
        { /* four buttons, two rows of two: every icon drawn at PORT_FSBTN_SCALE
           * of the Copy icon's height (the X is a smaller texture, so it is
           * scaled to match), its label at the same scale to the right,
           * centred on its row */
            static const s32 btn_x[PORT_FSBTN_COUNT] = { PORT_FSBTN_X_LEFT, PORT_FSBTN_X_RIGHT, PORT_FSBTN_X_LEFT, PORT_FSBTN_X_RIGHT };
            static const s32 btn_y[PORT_FSBTN_COUNT] = { PORT_FSBTN_Y_TOP, PORT_FSBTN_Y_TOP, PORT_FSBTN_Y_BOTTOM, PORT_FSBTN_Y_BOTTOM };
            static const s32 btn_img[PORT_FSBTN_COUNT] = { IMG_COPY, IMG_DEL, IMG_CHECK, IMG_X };
            f32 iconhalfh = (f32) (mainfolderimages + IMG_COPY)->height * 0.5f * PORT_FSBTN_SCALE;
            s32 b;

            for (b = 0; b < PORT_FSBTN_COUNT; b++)
            {
                sImageTableEntry *img = mainfolderimages + btn_img[b];
                struct rectbbox *bound = &port_filesel_btn_bound[b];
                /* EXTERNAL ICONS: Data/Textures/Base/option.bmp
                 * and exitgm.bmp replace the borrowed check and X when present
                 * (port_textures.c, docs/port/external-assets.md). Absent =
                 * the ROM image, as before. */
                extern struct sImageTableEntry *portTexExternal(const char *name);
                sImageTableEntry *ext = (b == PORT_FSBTN_OPTIONS) ? portTexExternal("option")
                                      : (b == PORT_FSBTN_EXIT)    ? portTexExternal("exitgm") : NULL;

                if (ext != NULL)
                {
                    img = ext;
                }
                struct coord2d iconpos;
                struct coord2d iconhalf;
                char *label;

                switch (b)
                {
                case PORT_FSBTN_COPY:    label = langGet(getStringID(LTITLE, TITLE_STR_27_COPY));  break; /* Copy */
                case PORT_FSBTN_ERASE:   label = langGet(getStringID(LTITLE, TITLE_STR_28_ERASE)); break; /* Erase */
                case PORT_FSBTN_OPTIONS: label = "Options\n"; break;
                default:                 label = "Exit Game\n"; break;
                }

                iconpos.f[0] = (f32) btn_x[b];
                iconpos.f[1] = (f32) btn_y[b];
                iconhalf.f[1] = iconhalfh;
                iconhalf.f[0] = iconhalfh * (f32) img->width / (f32) img->height;

                texSelect(&DL, img, 4, 0, 0);
                display_image_at_position(&DL, &iconpos.f[0], &iconhalf.f[0], img->width, img->height, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, (s32) img->level > 0, 0);

                portTextScale(PORT_FSBTN_SCALE);
                textsize.p[0] = 0;
                textsize.p[1] = 0;
                textMeasure(&textsize.p[1], &textsize.p[0], label, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                textpos.p[1] = (s32) (iconpos.f[0] + iconhalf.f[0]) + PORT_FSBTN_LABEL_GAP;
                textpos.p[0] = btn_y[b] - (textsize.p[1] / 2);

                /* A BIGGER TARGET THAN THE INK: padded left and right, and half
                 * the row pitch up and down - so the two rows' boxes meet
                 * exactly and can never overlap, which a fixed vertical pad
                 * on 26 px icons 27 px apart would */
                bound->left = iconpos.f[0] - iconhalf.f[0] - (f32) PORT_FSBTN_HIT_PAD;
                bound->right = (f32) (textpos.p[1] + textsize.p[0] + PORT_FSBTN_HIT_PAD);
                bound->up = iconpos.f[1] - (f32) (PORT_FSBTN_Y_BOTTOM - PORT_FSBTN_Y_TOP) * 0.5f;
                bound->down = iconpos.f[1] + (f32) (PORT_FSBTN_Y_BOTTOM - PORT_FSBTN_Y_TOP) * 0.5f;

                DL = microcode_constructor(DL); /* restore the text setup, not the icon's
                                                 * combiner */
                DL = textRender(DL, &textpos.p[1], &textpos.p[0], label, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
                portTextScale(1.0f);
            }
        }
#else
        langp = langGet(getStringID(LTITLE, TITLE_STR_27_COPY)); /* Copy */

        textsize.p[0] = 0;
        textsize.p[1] = 0;

        textMeasure(&textsize.p[1], &textsize.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        textpos.p[0] = 285 - (textsize.p[1] / 2);
        textpos.p[1] = 247;

        DL = textRender(DL, &textpos.p[1], &textpos.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
        folder_option_COPY_bound.right = (f32) (textsize.p[0] + textpos.p[1]);

        langp = langGet(getStringID(LTITLE, TITLE_STR_28_ERASE)); /* Erase */

        textsize.p[0] = 0;
        textsize.p[1] = 0;

        textMeasure(&textsize.p[1], &textsize.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        textpos.p[1] = 357;
        textpos.p[0] = 285 - (textsize.p[1] / 2);

        DL = textRender(DL, &textpos.p[1], &textpos.p[0], langp, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);

        folder_option_ERASE_bound.right = (f32) (textsize.p[0] + textpos.p[1]);

        copypos.f[0] = 225.0f;
        copypos.f[1] = 285.0f;

        copyhalfsize.f[0] = (f32) (mainfolderimages + IMG_COPY)->width * 0.5f;
        copyhalfsize.f[1] = (f32) (mainfolderimages + IMG_COPY)->height * 0.5f;

        texSelect(&DL, mainfolderimages, 4, 0, 0);
        display_image_at_position(&DL, &copypos.f[0], &copyhalfsize.f[0], mainfolderimages->width, mainfolderimages->height, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, (s32) mainfolderimages->level > 0, 0);

        folder_option_COPY_bound.left = copypos.f[0] - copyhalfsize.f[0];
        folder_option_COPY_bound.up = copypos.f[1] - copyhalfsize.f[1];
        folder_option_COPY_bound.down = copypos.f[1] + copyhalfsize.f[1];

        erasepos.f[0] = 335.0f;
        erasepos.f[1] = 285.0f;

        erasehalfsize.f[0] = (mainfolderimages + IMG_DEL)->width * 0.5f;
        erasehalfsize.f[1] = (mainfolderimages + IMG_DEL)->height * 0.5f;

        texSelect(&DL, mainfolderimages + IMG_DEL, 4, 0, 0);
        display_image_at_position(&DL, &erasepos.f[0], &erasehalfsize.f[0], (mainfolderimages + IMG_DEL)->width, (mainfolderimages + IMG_DEL)->height, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, (s32) (mainfolderimages + IMG_DEL)->level > 0, 0);

        folder_option_ERASE_bound.left = (f32) (erasepos.f[0] - erasehalfsize.f[0]);
        folder_option_ERASE_bound.up = (f32) (erasepos.f[1] - erasehalfsize.f[1]);
        folder_option_ERASE_bound.down = (f32) (erasepos.f[1] + erasehalfsize.f[1]);
#endif

        selectpos.f[0] = 110.0f;
        selectpos.f[1] = 285.0f;

        selecthalfsize.f[0] = (mainfolderimages + IMG_SEL)->width * 0.5f;
        selecthalfsize.f[1] = (mainfolderimages + IMG_SEL)->height * 0.5f;

        texSelect(&DL, mainfolderimages + IMG_SEL, 4, 0, 0);
        display_image_at_position(&DL, &selectpos.f[0], &selecthalfsize.f[0], (mainfolderimages + IMG_SEL)->width, (mainfolderimages + IMG_SEL)->height, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, (s32) (mainfolderimages + IMG_SEL)->level > 0, 0);
    }

#ifndef TARGET_N64
    if (portDlgIsOpen())
    {
        DL = portDlgDraw(DL);
    }
#endif

    if (folder_selected_for_deletion < 0)
    {
        DL = frontDrawCursor(DL);
    }

    return DL;
}


//********************************************************************************************************
//MODE SELECT
//********************************************************************************************************
#ifndef TARGET_N64
void update_menu15_cheat(void);

/* The watch's live Unlock All row can shrink what is unlocked mid-session,
 * which could leave an unearned cheat stuck on and hidden, so such cheats are
 * dropped at the next frontend screen (a mission that really ran with one on
 * still goes unrecorded). */
static s32 portCheatOnPortTab(s32 cheat);

static void frontDropUnearnedCheats(void)
{
    s32 i;
    s32 dropped = FALSE;

    for (i = CHEAT_EXTRA_MP_CHARS; i != CHEAT_INVALID; i++)
    {
        if (g_CheatActivated[i] && !frontCheckIfCheatIsUnlocked(i) && !portCheatOnPortTab(i))
        {
            g_CheatActivated[i] = FALSE;
            dropped = TRUE;
        }
    }
    if (dropped)
    {
        update_menu15_cheat();   /* the cheat-run flags, from what is left */
    }
}
#endif

void init_menu06_modeselect(void)
{
#ifndef TARGET_N64
    frontDropUnearnedCheats();
#endif
    gamemode = GAMEMODE_INTRO;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    load_walletbond();
    fileUpdateBondInCurrentFolder();
}


void update_menu06_modesel(void) 
{
    return;
}


void interface_menu06_modesel(void)
{
    u32 i;

    is_cheat_menu_available = FALSE;
    for (i=CHEAT_EXTRA_MP_CHARS; i != CHEAT_INVALID; i++)
    {
        if (frontCheckIfCheatIsUnlocked(i))
        {
            cheat_available[i] = TRUE;
            is_cheat_menu_available = TRUE;
        }
        else
        {
            cheat_available[i] = FALSE;
        }
    }
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    disable_all_switches(walletinst[0]);
    select_load_bond_picture(walletinst[0], fileGetBondForFolder(selected_folder_num));
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PHOTOBOND, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_EYESONLY, 1);
    tab_prev_highlight = FALSE;
    mission_difficulty_highlighted = DIFFICULTY_MULTI;
    /* PORT: the 4th entry, EXTENDED OPTIONS, is gone - its
     * settings live in the watch now (docs/port/watch-settings-pages.md) -
     * so the row block is back at Rare's own y 236/268/300, bands 243/275,
     * cursor base 0xE2. */
    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = TRUE;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            tab_prev_selected = TRUE;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        }
    }
    else if ((is_cheat_menu_available) && (275.0f <= cursor_v_pos))
    {
        mission_difficulty_highlighted = DIFFICULTY_00;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            gamemode = GAMEMODE_CHEATS;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
        }
    }
    else if ((243.0f <= cursor_v_pos) && (joyGetControllerCount() >= 2))
    {
        mission_difficulty_highlighted = DIFFICULTY_SECRET;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            gamemode = GAMEMODE_MULTI;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
        }
    }
    else
    {
        mission_difficulty_highlighted = DIFFICULTY_AGENT;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            gamemode = GAMEMODE_SOLO;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    frontUpdateControlStickPosition();
    if (gamemode == GAMEMODE_SOLO)
    {
        frontChangeMenu(MENU_MISSION_SELECT, FALSE);
        set_cursor_to_stage_solo(0);
        return;
    }
    if (gamemode == GAMEMODE_MULTI)
    {
        frontChangeMenu(MENU_MP_OPTIONS, FALSE);
        return;
    }
    if (gamemode == GAMEMODE_CHEATS)
    {
        frontChangeMenu(MENU_CHEAT, FALSE);
        return;
    }
    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_FILE_SELECT, FALSE);
    }
}


// Address 0x7F00D5E8 NTSC
Gfx *frontSetupMenuBackground(Gfx *DL)
{
    s32 padding;
    f32 temp_f0;
    f32 temp_f2;
    ModelRenderData sp10C;
    s32 i;
    Mtxf spC8;
    Mtxf sp88;
    Mtxf sp48;

    temp_f0 = folderpositions[selected_folder_num].f[0];
    temp_f2 = folderpositions[selected_folder_num].f[1];

    sp10C = D_8002AF84;

    temp_f0 += D_8002AFC4;
    temp_f2 += D_8002AFC8;

    matrix_4x4_set_lookat_target(&spC8, temp_f0, temp_f2, 4000.0f + D_8002AFCC, temp_f0, temp_f2, 0.0f, 0.0f, 1.0f, 0.0f);
    matrix_4x4_set_identity_and_position(&folderpositions[selected_folder_num], &sp88);
    matrix_scalar_multiply(0.25f, sp88.m[0]);
    matrix_4x4_multiply_in_place(&spC8, &sp88);

    sp10C.basemtx = &sp88;

    sp10C.mtxlist = dynAllocate(walletinst[0]->obj->numMatrices << 6);

    matrix_4x4_copy(&sp88, sp10C.mtxlist);

    walletinst[0]->render_pos = (union RenderPosView*)sp10C.mtxlist;

    sp10C.flags = 3;
    sp10C.zbufferenabled = FALSE;
    sp10C.gdl = DL;

    subdraw(&sp10C, walletinst[0]);

    DL = sp10C.gdl;

    for (i=0; i<walletinst[0]->obj->numMatrices; i++)
    {
        // hack: source address steps by sizeof(Mtxf), but can't get that to match
        matrix_4x4_copy(&((s8*)walletinst[0]->render_pos)[i*0x40], &sp48);
        matrix_4x4_f32_to_s32(&sp48, &((Mtxf*)walletinst[0]->render_pos)[i]);
    }


    return DL;
}



void setCursorPOSforMode(int mode)
{
    if (frontCursorSnapAllowed())
    {
        cursor_h_pos = 126.0f;
        cursor_v_pos = mode * 0x20 + 0xe2;
    }
}




Gfx* constructor_menu06_modesel(Gfx* DL)
{
    char *textstring;
    s32 x;
    s32 y;
    s32 y2;
    s32 x2;
    s32 text_color;

    DL = viSetFillColor(DL,0,0,0);
    DL = viFillScreen(DL);
    #ifdef VERSION_EU
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
    #endif
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    x = 0x96;
    y = 0xdc;
    DL = frontPrintText(DL, &x, &y, "1.\n", ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    textstring = langGet((g_AppendCheatSinglePlayer != 0) ? getStringID(LTITLE, TITLE_STR_117_CHEATSELECTMISSION) : getStringID(LTITLE, TITLE_STR_29_SELECTMISSION));

    textMeasure(&x2, &y2, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

    x = 0xAA;
    y = 0xdc;
    if (mission_difficulty_highlighted == 0)
    {
        DL = microcode_constructor_related_to_menus(DL, 0x94, 0xDA, y2 + 0xAF, 0xEA, 0x32);
    }

    DL = frontPrintText(DL, &x, &y, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    x = 0x96;
    y = 0xFC;
    if (joyGetControllerCount() >= 2)
    {
        text_color = 0xFF;
    }
    else
    {
        text_color = 0x70;
    }
    DL = frontPrintText(DL, &x, &y, "2.\n", ptrFontZurichBoldChars, ptrFontZurichBold, text_color, viGetX(), viGetY(), 0, 0);

#ifndef TARGET_N64
    /* Multiplayer Options sheet cheats are match options now, so they don't count here;
     * only a solo-menu cheat that still reaches MP counts. */
    textstring = langGet((g_AppendCheatMultiPlayer != 0) ? getStringID(LTITLE, TITLE_STR_276_CHEATMULTIPLAYER) : getStringID(LTITLE, TITLE_STR_30_MULTIPLAYER));
#else
    textstring = langGet((g_AppendCheatMultiPlayer != 0) ? getStringID(LTITLE, TITLE_STR_276_CHEATMULTIPLAYER) : getStringID(LTITLE, TITLE_STR_30_MULTIPLAYER));
#endif

    textMeasure(&x2, &y2, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

    x = 0xAA;
    y = 0xFC;
    if (mission_difficulty_highlighted == 1)
    {
        DL = microcode_constructor_related_to_menus(DL, 0x94, 0xFA, y2 + 0xAF, 0x10A, 0x32);
    }
    DL = frontPrintText(DL, &x, &y, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, text_color, viGetX(), viGetY(), 0, 0);

    if (is_cheat_menu_available != 0)
    {
        x = 0x96;
        y = 0x11C;
        DL = frontPrintText(DL, &x, &y, "3.\n", ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        textstring = langGet(getStringID(LTITLE, TITLE_STR_31_CHEATOPTIONS));

        textMeasure(&x2, &y2, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        x = 0xAA;
        y = 0x11C;
        if (mission_difficulty_highlighted == 2)
        {
            DL = microcode_constructor_related_to_menus(DL, 0x94, 0x11A, y2 + 0xAF, 0x12A, 0x32);
        }
        DL = frontPrintText(DL, &x, &y, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}


#ifndef TARGET_N64
//********************************************************************************************************
//OPTIONS (menu1B) + CREDITS (menu1C) - PORT, docs/port/files-screen-buttons.md
//
// The Files screen's OPTIONS button. Drawn like the Files menu (menu06): a
// folder's paper, three numbered rows at the Files menu's own row coordinates
// (y 0xDC / 0xFC / 0x11C, cursor bands 243 / 275) and the PREVIOUS tab; the top
// tab (Rare's START slot) reads CREDITS. The rows open the option sheets
// (menu1C, below) and the tab opens the Credits sheet. B, right click or
// PREVIOUS go back a page, returning the cursor to the row / tab that opened
// it.
//********************************************************************************************************
/* CONTROL is a submenu:
 *
 *   CONTROL  -> player 1..4
 *     PLAYER -> input device, rebind controller, everything else
 *       BINDS -> the bind list for that player's device
 *
 * New sheet kinds are appended; the folder maps its rows through
 * port_options_row_kind, so the folder's order is that table's order. */
enum { PORT_SHEET_GAMEPLAY, PORT_SHEET_CONTROL, PORT_SHEET_DISPLAY, PORT_SHEET_CREDITS,
       PORT_SHEET_PLAYER, PORT_SHEET_BINDS, PORT_SHEET_VIDEO, PORT_SHEET_AUDIO, PORT_SHEET_GRAPHICS,
       PORT_SHEET_GENERAL, PORT_SHEET_HUD,
       PORT_SHEET_MOUSEKB, PORT_SHEET_PAD, PORT_SHEET_CTLGENERAL, PORT_SHEET_PORTOPTS,
       /* MULTIPLAYER OPTIONS: opened from the MP setup
        * screen's last row, NOT from the Options folder - portSheetLeave sends
        * it back there. The settings multiplayer keeps apart from solo. */
       PORT_SHEET_MPOPTS,
       /* CLASSIC: which parts Classic Mode applies, opened from VIDEO's middle tab */
       PORT_SHEET_CLASSIC, PORT_SHEET_COUNT };
/* The Options folder's rows, top to bottom, at the Files menu's own row
 * coordinates (y 0xDC, pitch 0x20). Each is a submenu:
 *
 *   DISPLAY  -> VIDEO, AUDIO, GRAPHICS
 *   GAMEPLAY -> GENERAL SETTINGS, HUD
 *   CONTROL  -> PLAYER n -> MOUSE AND KEYBOARD or CONTROLLER (whichever the
 *               device is), GENERAL CONTROL OPTIONS
 */
#define PORT_OPTIONS_ROWS 3
#define PORT_OPTIONS_ROW_Y0    0xDC
#define PORT_OPTIONS_ROW_PITCH 0x20
static const s32 port_options_row_kind[PORT_OPTIONS_ROWS] = {
    PORT_SHEET_GAMEPLAY, PORT_SHEET_CONTROL, PORT_SHEET_DISPLAY,
};
/* Rare's Files menu rows are ALL CAPS ("SELECT MISSION"), so are these */
/* the DISPLAY sheet is titled AUDIO VISUAL */
static const char *const port_sheet_title[PORT_SHEET_COUNT] = { "GAMEPLAY\n", "CONTROL\n", "AUDIO VISUAL\n",
        "CREDITS\n", "PLAYER\n", "REBIND\n", "VIDEO\n", "AUDIO\n", "GRAPHICS\n",
        "GENERAL SETTINGS\n", "HUD\n",
        "MOUSE AND KEYBOARD\n", "CONTROLLER\n", "GENERAL CONTROL OPTIONS\n", "PORT OPTIONS\n",
        "GAMEPLAY OPTIONS\n", "CLASSIC\n" };   /* GAMEPLAY OPTIONS is not drawn: the two-column sheet has no heading */
static s32 port_options_row_highlighted = -1; /* row under the cursor, -1 = on a tab */
/* The folder's hubs: GAMEPLAY and AUDIO VISUAL reuse this folder screen to list
 * their pages, numbered; -1 means the folder's own three rows. A page's
 * PREVIOUS / B returns to its hub (portSheetLeave(TRUE) sets
 * port_options_keep_hub across init_menu1B). */
static s32 port_options_hub = -1;
static s32 port_options_keep_hub = FALSE;
static const s32 port_sheet_gameplay_pages[];
static const s32 port_sheet_display_pages[];
static s32 portOptionsRowCount(void);
static s32 portOptionsRowKind(s32 r);
static s32 port_sheet_kind = PORT_SHEET_CREDITS; /* which placeholder sheet menu1C shows / was asked for */
/* Where Back goes: a sheet opened from another sheet returns to it, and one
 * opened from the Options folder returns to the folder. The tree is only two
 * deep; the extra slot is cheap insurance. */
#define PORT_SHEET_DEPTH_MAX 4
static s32 port_sheet_parent[PORT_SHEET_DEPTH_MAX];
static s32 port_sheet_depth;
/* which player the CONTROL branch is editing, 0..3 */
static s32 port_sheet_player;
static s32 port_sheet_selected = FALSE;

/* GE_MENU harness: pick the sheet from the word's first two letters (gameplay,
 * general, graphics, hud, control, credits, display, video, audio, mouse, pad,
 * cgeneral, aiming, binds); gameplay and display open the hub pickers, the rest
 * open a page directly. */
s32 portMenuHarnessSheet(char first, char second)
{
    if (first == 'g') port_sheet_kind = (second == 'r') ? PORT_SHEET_GRAPHICS
                                      : (second == 'e') ? PORT_SHEET_GENERAL : PORT_SHEET_GAMEPLAY;
    else if (first == 'h') port_sheet_kind = PORT_SHEET_HUD;
    else if (first == 'd') port_sheet_kind = PORT_SHEET_DISPLAY;
    else if (first == 'a') port_sheet_kind = PORT_SHEET_AUDIO;   /* the aiming page was removed */
    else if (first == 'v') port_sheet_kind = PORT_SHEET_VIDEO;
    else if (first == 'm') port_sheet_kind = PORT_SHEET_MOUSEKB;
    else if (first == 'c' && second == 'g') port_sheet_kind = PORT_SHEET_CTLGENERAL;   /* GE_MENU=cgeneral */
    else if (first == 'c' && second == 'l') port_sheet_kind = PORT_SHEET_CLASSIC;      /* GE_MENU=classic */
    else if (first == 'p' && second == 'a') port_sheet_kind = PORT_SHEET_PAD;
    else if (first == 'p' && second == 'o') port_sheet_kind = PORT_SHEET_PORTOPTS;   /* GE_MENU=portopts */
    else if (first == 'p')
    {   /* GE_MENU=player[1-4]: straight to a player's page, which is two
         * A presses in and was otherwise unreachable headlessly - the input
         * mode and control style rows both live there */
        port_sheet_kind = PORT_SHEET_PLAYER;
        port_sheet_player = (second >= '1' && second <= '4') ? (second - '1') : 0;
    }
    else if (first == 'b') port_sheet_kind = PORT_SHEET_BINDS;   /* GE_MENU=binds: player 1's rebind page */
    else if (second == 'r') port_sheet_kind = PORT_SHEET_CREDITS;
    else port_sheet_kind = PORT_SHEET_CONTROL;
    return port_sheet_kind;
}

/* the top tab with its own string: frontAddStartTabText's drawing (Rare's
 * START tab slot, STARTTAB_* geometry, tab_start_highlight). CREDITS on
 * the Options folder, OPTIONS on every sheet (where it backs out to the Options
 * folder) */
/* The Credits sheet's top tab reads "1997" and plays the in-game credits
 * with the cast parade. The Video sheet's middle tab reads "CLASSIC" and opens
 * the CLASSIC sheet; keep a middle tab label under eight letters, since the
 * middle tab is the shortest (drawn with frontAddNextTabText's geometry and our
 * own string). */
#define PORT_CREDITS_TAB_TEXT "1997\n"
#define PORT_CLASSIC_TAB_TEXT "CLASSIC\n"

static Gfx *portAddMiddleTabText(Gfx *DL, char *text)
{
    s32 v;
    s32 h;
    s32 textWidth;
    s32 textHeight;

    setTextSpacingInverted(TRUE);
    textHeight = 0;
    textWidth = 0;
    //text is sideways so textWidth and textHeight are flipped
    textMeasure(&textWidth, &textHeight, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    h = TABS_RIGHT_EDGE - (textWidth / 2);

    if (tab_next_highlight)
    {
        DL = microcode_constructor_related_to_menus(DL, (h - textWidth) + 1, NEXTTAB_TEXT_TOP, h, NEXTTAB_TEXT_BOTTOM, 50);
    }

    setTextOrientation(ROT_90CW);

    v = NEXTTAB_TEXT_HALF - (textHeight / 2);
    DL = textRender(DL, &v, &h, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF, viGetY(), viGetX(), 0, 0);
    setTextOrientation(ROT_NORMAL);
    setTextSpacingInverted(FALSE);
    return DL;
}

/* 1 while the end credits were started from the CREDITS sheet rather than
 * earned: the cast parade then comes back to the sheet, not to a mission list
 * for a folder nobody chose (interface_menu18_displaycast). */
static s32 port_credits_roll;

/**
 * Play the game's own ending: the credits stage (LEVELID_CUBA) followed by the
 * extended cast parade, exactly as finishing Cradle does. Folder 1 is just a
 * placeholder for the launch; the credits stage writes nothing to a save
 * (boss.c skips it).
 */
static void portCreditsRoll(void)
{
    extern s32 g_portCreditsSkipParade;

    g_portCreditsSkipParade = FALSE;
    port_credits_roll = TRUE;
    selected_folder_num = FOLDER1;
    set_selected_difficulty(DIFFICULTY_AGENT);
    set_solo_and_ptr_briefing(LEVELID_CUBA);
    frontChangeMenu(MENU_RUN_STAGE, TRUE);
}

static Gfx *portAddTopTabText(Gfx *DL, char *text)
{
    s32 v;
    s32 h;
    s32 textWidth;
    s32 textHeight;

    setTextSpacingInverted(TRUE);
    textHeight = 0;
    textWidth = 0;
    //text is sideways so textWidth and textHeight are flipped
    textMeasure(&textWidth, &textHeight, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    v = STARTTAB_TEXT_TOP;
    h = TABS_RIGHT_EDGE - (textWidth / 2);

    if (tab_start_highlight)
    {
        DL = microcode_constructor_related_to_menus(DL, (h - textWidth) + 1, STARTTAB_TEXT_TOP, h, STARTTAB_TEXT_BOTTOM, 50);
    }

    setTextOrientation(ROT_90CW);

    v = STARTTAB_TEXT_HALF - (textHeight / 2);
    DL = textRender(DL, &v, &h, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF, viGetY(), viGetX(), 0, 0);
    setTextOrientation(ROT_NORMAL);
    setTextSpacingInverted(FALSE);
    return DL;
}

/* The Port Options page's top tab. "RESET ALL" is longer than the tab labels
 * Rare used, so it falls back to "RESET" if the font measures it past the
 * tab's text area. */
static char *portResetAllTabText(void)
{
    static char *text = NULL;

    if (text == NULL)
    {
        s32 textWidth = 0;
        s32 textHeight = 0;

        setTextSpacingInverted(TRUE);
        //text is sideways so textHeight is its length along the tab
        textMeasure(&textWidth, &textHeight, "RESET ALL\n", ptrFontBankGothicChars, ptrFontBankGothic, 0);
        setTextSpacingInverted(FALSE);
        text = (textHeight <= STARTTAB_TEXT_BOTTOM - STARTTAB_TEXT_TOP) ? "RESET ALL\n" : "RESET\n";
    }
    return text;
}

void init_menu1B_portoptions(void)
{
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_start_highlight = FALSE;
    port_options_row_highlighted = -1;
    if (!port_options_keep_hub)
    {
        port_options_hub = -1;
    }
    port_options_keep_hub = FALSE;
    port_sheet_selected = FALSE;
    load_walletbond();
    fileUpdateBondInCurrentFolder();
}

void update_menu1B_portoptions(void)
{
    return;
}

void interface_menu1B_portoptions(void)
{
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    /* the Multiplayer folder's background, shared with the Options menu:
     * interface_menu0E_mpoptions' four switches, no Bond photo */
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CONFIDENTIAL2, 1);
    tab_prev_highlight = FALSE;
    tab_start_highlight = FALSE;
    port_options_row_highlighted = -1;

    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = TRUE;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            tab_prev_selected = TRUE;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        }
    }
    else if (frontCheckCursorOnStartTab())
    {   /* the top tab: CREDITS */
        tab_start_highlight = TRUE;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            port_sheet_kind = PORT_SHEET_CREDITS;
            port_sheet_selected = TRUE;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
        }
    }
    else
    {   /* one cursor band per row, each starting where the row's highlight
         * box does (y - 2), so the band and the box are the same shape. Above
         * the first row still means the first, as the Files menu has it. */
        s32 r;

        port_options_row_highlighted = 0;
        for (r = 1; r < portOptionsRowCount(); r++)
        {
            if ((f32) (PORT_OPTIONS_ROW_Y0 + r * PORT_OPTIONS_ROW_PITCH - 2) <= cursor_v_pos)
            {
                port_options_row_highlighted = r;
            }
        }
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            const s32 kind = portOptionsRowKind(port_options_row_highlighted);

            if (port_options_hub < 0 && (kind == PORT_SHEET_GAMEPLAY || kind == PORT_SHEET_DISPLAY))
            {   /* a hub: the same screen, its pages numbered */
                port_options_hub = kind;
                if (frontCursorSnapAllowed())
                {
                    cursor_h_pos = 126.0f;
                    cursor_v_pos = (f32) (PORT_OPTIONS_ROW_Y0 + 6);
                }
            }
            else
            {
                port_sheet_kind = kind;
                port_sheet_selected = TRUE;
            }
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    frontUpdateControlStickPosition();

    if (port_sheet_selected)
    {
        frontChangeMenu(MENU_PORT_SHEET, FALSE);
        return;
    }
    if (tab_prev_selected && port_options_hub >= 0)
    {   /* out of a hub: back to the folder's rows, onto the hub's own */
        s32 r;

        for (r = 0; r < PORT_OPTIONS_ROWS && port_options_row_kind[r] != port_options_hub; r++)
        {
        }
        port_options_hub = -1;
        tab_prev_selected = FALSE;
        if (r < PORT_OPTIONS_ROWS && frontCursorSnapAllowed())
        {
            cursor_h_pos = 126.0f;
            cursor_v_pos = (f32) (PORT_OPTIONS_ROW_Y0 + r * PORT_OPTIONS_ROW_PITCH + 6);
        }
        return;
    }
    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_FILE_SELECT, FALSE);
    }
}

Gfx *constructor_menu1B_portoptions(Gfx *DL)
{
    char *textstring;
    char number[4];
    s32 i;
    s32 x;
    s32 y;
    s32 y2;
    s32 x2;

    DL = viSetFillColor(DL,0,0,0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    for (i = 0; i < portOptionsRowCount(); i++)
    {   /* row i starts at the Files menu's first row (y 0xDC); band y-2 .. y+14 */
        s32 row_y = PORT_OPTIONS_ROW_Y0 + (i * PORT_OPTIONS_ROW_PITCH);

        sprintf(number, "%d.\n", i + 1);
        x = 0x96;
        y = row_y;
        DL = frontPrintText(DL, &x, &y, number, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

        textstring = (char *) port_sheet_title[portOptionsRowKind(i)];
        textMeasure(&x2, &y2, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        x = 0xAA;
        y = row_y;
        if (port_options_row_highlighted == i)
        {
            DL = microcode_constructor_related_to_menus(DL, 0x94, row_y - 2, y2 + 0xAF, row_y + 14, 0x32);
        }
        DL = frontPrintText(DL, &x, &y, textstring, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }

    DL = portAddTopTabText(DL, "CREDITS\n");
    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}

//---- OPTION SHEETS (menu1C): the cheat page's sheet (TABS + BLANK + CLASSIFIED) ----
//
// port_sheet_kind picks the page. Option pages are driven by the port_options.c
// table (docs/port/ini-options-placement.md says which key went where).
//
// Layout: label at x 0x37, value at x 0xBE, first row at y 0x35, Rare's own
// cheat-page pitch (0x14) and thirteen rows a page (tighter spacing read as
// cramped). A page that would run past Rare's last line (y 293: thirteen rows
// plus APPLY) closes its pitch up instead. Longer pages turn over: NEXT (Rare's middle tab) goes a page on,
// PREVIOUS a page back, and PREVIOUS (or B) on the first page returns to the
// Options folder.
//
// Input: the cursor picks a row; A toggles a two-value row, steps a number, or
// opens the drop-down list for a row with more values; D-pad / C left-right and
// the wheel step any row. The list is the watch's drop-down redrawn for the
// paper: it hangs off the value column, shows as many entries as the sheet is
// tall (thirteen lines) and scrolls beyond that. Hovering highlights an entry,
// A picks, B closes without picking; it opens and closes with the watch's CRT
// switch-on animation, advanced on the 60 Hz menu field.
//
// DISPLAY has deferred rows: a pick only changes what the row shows (in red)
// until APPLY CHANGES is pressed. That row is pinned to the bottom slot of
// every DISPLAY page; turning a page keeps a pending pick, but leaving the
// sheet discards it, so nobody is stranded in a mode the monitor can't show.

#define PORT_SHEET_LABEL_X    0x37
#define PORT_SHEET_VALUE_X    0xBE
#define PORT_SHEET_TITLE_Y    0x21
#define PORT_SHEET_ROW_Y0     0x35
#define PORT_SHEET_ROW_PITCH  0x14  /* Rare's cheat page */
#define PORT_SHEET_PAGE_ROWS  13    /* Rare's cheat page shows twelve; VIDEO needs thirteen */
/* Two-column sheets (Gameplay Options) have no APPLY row and no heading,
 * so each column also uses the heading's and APPLY's lines: 14 rows. */
#define PORT_SHEET_TWOCOL_ROWS 14
#define PORT_SHEET_TWOCOL_Y0   (PORT_SHEET_ROW_Y0 - PORT_SHEET_ROW_PITCH)
#define PORT_SHEET_APPLY_SLOT 13    /* the pinned APPLY CHANGES row, after the page's rows */
#define PORT_SHEET_SLOTS      14    /* slots 0..12 are rows, 13 is APPLY */
#define PORT_SHEET_LAST_Y     (PORT_SHEET_ROW_Y0 + 12 * PORT_SHEET_ROW_PITCH)   /* 293: Rare's last line; no row goes lower */
#define PORT_SHEET_LIST_LINES 13    /* a drop-down list's window: the slot lines from the first row to y 293 */
#define PORT_SHEET_ROWS_MAX   80  /* the CONTROL page carries the whole bind list */
#define PORT_SHEET_LIST_MAX   32    /* a row with more values than this steps instead of listing (mouse sensitivity: 100) */
#define PORT_SHEET_LIST_PAD   4
#define PORT_SHEET_COL_PENDING 0xA00000FF /* a picked-but-not-applied value, and APPLY while one waits */
#define PORT_SHEET_COL_DIM     0x70       /* an unavailable row */
#define PORT_SHEET_COL_LIST_EDGE 0x000000C0
#define PORT_SHEET_COL_LIST_BG   0xEDE4C4FF /* opaque paper, so the rows under the list do not show through */
#define PORT_SHEET_COL_LIST_LIVE 0x00000032 /* the value in use: the folder's own highlight band */
#define PORT_SHEET_COL_LIST_HOT  0x000000A0 /* the entry under the cursor: white text on a dark band */
#define PORT_SHEET_ANIM_W 0.30f
#define PORT_SHEET_ANIM_H 0.22f
/* SETTING HELP ([display] setting_help): the highlighted row's line from
 * port_option_help.c, in the drop-down list's panel along the sheet's foot.
 * Two lines of the sheet's font, else three of the watch's small font. */
#define PORT_SHEET_HELP_X0     (PORT_SHEET_LABEL_X - PORT_SHEET_LIST_PAD - 1)
#define PORT_SHEET_HELP_X1     0x178   /* clear of the tabs (390) */
#define PORT_SHEET_HELP_Y0     0x124   /* rows on a page must end above this */
#define PORT_SHEET_HELP_Y1     0x147   /* the frontend picture ends at 330 */
#define PORT_SHEET_HELP_LINES  2
#define PORT_SHEET_HELP_PITCH  14
#define PORT_SHEET_HELP_SMALL_LINES 3
#define PORT_SHEET_HELP_SMALL_PITCH 10
#define PORT_SHEET_HELP_PITCH_MIN 16   /* rows never closer than this to make room */

extern s32 portOptFind(const char *key);
extern const char *portOptKey(s32 i);
extern s32 portOptIsToggle(s32 i);
extern const char *portOptLabel(s32 i);
extern const char *portOptValueText(s32 i);
extern s32 portOptValueCount(s32 i);
extern const char *portOptValueTextAt(s32 i, s32 idx);
extern s32 portOptValueIndex(s32 i);
extern s32 portOptValueIndexNear(s32 i, s32 dir);
extern s32 portOptGet(s32 i);
extern s32 portOptSetValue(s32 i, s32 v);
extern s32 portOptSetIndex(s32 i, s32 idx);
extern void portOptStep(s32 i, s32 dir);
extern s32 portOptIsRanged(s32 i);
extern void portOptSaveHold(s32 on);
extern void portClassicSetHold(s32 on);   /* the mode's own hold: see portSheetApply */
extern s32 portOptAvailable(s32 i);
extern s32 portVideoResCount(void);
extern s32 portVideoResGet(s32 i, s32 *w, s32 *h);
extern s32 portVideoResCurrent(void);
extern s32 portVideoResLocked(void);
extern s32 portVideoResApply(s32 i);
extern f32 portTickDelta;              /* frametiming.c: this tick's true field delta (0.5 at tickhz 120) */
s32 portMenuLookActive(void);          /* below: the look stick is quick-scrolling an open list */
s32 portMenuLookSteps(void);
#ifdef GE_VIDEO
extern s32 portMouseWheelSteps(void);
#else
#define portMouseWheelSteps() 0
#endif

enum { SHROW_OPT, SHROW_RES, SHROW_XHAIR, SHROW_LAYOUT, SHROW_SAVELAYOUT, SHROW_DELLAYOUT,
       SHROW_MANAGELAYOUT, /* save / delete / cancel popup */
       SHROW_BINDDEV, SHROW_BINDMODE, SHROW_CTRLSTYLE, SHROW_BIND, SHROW_PLAYERPICK,
       SHROW_GOBINDS, SHROW_GOSHEET,
       SHROW_DEVNAME, /* the device's name, on the line under Input Device - text, takes no input */
       SHROW_RESETBINDS, /* the rebind page's Reset to Default */
       SHROW_COLBREAK /* two-column sheets: the rows after it start the right column (no row of its own) */ };

/* Rebinding from the Options menu, so a player can fix their bindings before
 * starting a mission (the watch was the only bind screen). Everything
 * underneath is shared with the watch (port_watchpages.c); only the arm state
 * is separate, since the two screens are never up at once. */
extern s32         portBindCount(s32 dev);
extern const char *portBindLabel(s32 dev, s32 i);
extern const char *portBindValueText(s32 dev, s32 i);
extern s32         portBindModeIsN64(s32 dev);
extern s32         portBindModeCanN64(s32 dev);
extern void        portBindModeToggle(s32 dev);
extern s32         portBindCaptureBegin(s32 dev, s32 i);
extern s32         portBindCaptureActive(void);
extern s32         portBindCapturingRow(s32 i);
extern s32         portBindCaptureTick(void);
extern void        portBindCaptureCancel(void);
extern s32         portBindUnbind(s32 dev, s32 i);
extern const char *portInputDeviceLabel(s32 dev);
extern s32         portInputDeviceIniKey(s32 dev, const char *name, char *buf, s32 len);
extern int         strncmp(const char *str1, const char *str2, size_t n); /* src/str.h */

/* Which device each player is being set up with (0 = keyboard and mouse), kept
 * per player so one player's pages never edit another's bindings. Seeded player
 * N -> device N where that device exists, and changed from the page's own
 * input-device row. */
static s32 port_sheet_bind_dev_p[4];
#define port_sheet_bind_dev (port_sheet_bind_dev_p[port_sheet_player])
/* 60Hz FIELDS of held B that unbind a row - g_GlobalTimerDelta is in fields,
 * not ticks, so this is about half a second however fast the game runs */
#define PORT_SHEET_UNBIND_FIELDS 30

/* The pad layout rows on the CONTROL sheet, front end for input_pc.c's layout
 * presets (docs/port/input-and-display.md). They act on the page's own device,
 * never a hard-coded player 1, and are hidden for device 0 (the keyboard has no
 * layout). */
#define PORT_SHEET_LAYOUT_DEV port_sheet_bind_dev
extern s32 portInputLayoutCount(void);
extern const char *portInputLayoutName(s32 i);
extern s32 portInputLayoutIsUser(s32 i);
extern s32 portInputLayoutApply(s32 dev, s32 i);
extern s32 portInputLayoutSaveCurrent(s32 dev, const char *name);
extern const char *portInputLayoutActive(s32 dev);
extern const char *portInputLayoutShown(s32 dev);   /* input_pc.c: picked, else the seeded one */
extern s32 portInputLayoutDelete(s32 i);
extern s32 portInputDeviceCount(void);

/* Crosshair colour: midas.ini crosshair_color is a string ("default" or RRGGBB),
 * so it gets its own row kind rather than a port_options.c entry; -1 is Rare's
 * red. There is no separate "red" entry, because tinting the sprite red
 * reproduces the ROM's own texels, making it identical to "default". */
static const s32 port_xhair_rgb[] = {
    -1, 0xFFFFFF, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0080FF, 0xA500FF, 0xFF00FF, 0x808080,
};
/* Purple (A500FF). The ini holds the hex, never the index,
 * so a colour can go anywhere in the list. */
static const char *const port_xhair_name[] = {
    "Default\n", "White\n", "Orange\n", "Yellow\n", "Green\n", "Cyan\n", "Blue\n", "Purple\n", "Magenta\n", "Grey\n",
};
#define PORT_XHAIR_COUNT ((s32)(sizeof(port_xhair_rgb) / sizeof(port_xhair_rgb[0])))

extern s32 portCrosshairColor(void);
extern s32 portVideoIniSetKey(const char *key, const char *value);
extern s32 portInputDeviceStyleGet(s32 dev);

/* THE SAME LIST FOR THE WATCH: port_watchpages.c's PWROW_XHAIR row reads and sets it
 * through these, so the folder and the watch cannot offer different colours
 * or write the key two ways. */
s32 portXhairCount(void)
{
    return PORT_XHAIR_COUNT;
}

const char *portXhairName(s32 i)
{
    return (i >= 0 && i < PORT_XHAIR_COUNT) ? port_xhair_name[i] : "Custom\n";
}

/** The list index of the colour in force; -1 = a hex the list has no name for. */
s32 portXhairIndex(void)
{
    const s32 rgb = portCrosshairColor();
    s32 i;

    if (rgb == 0xFF0000)
    {
        return 0;   /* an older ini's "red": the same texels as default */
    }
    for (i = 0; i < PORT_XHAIR_COUNT; i++)
    {
        if (port_xhair_rgb[i] == rgb)
        {
            return i;
        }
    }
    return -1;
}

/** Apply list entry `i` - live, through the ini setter (video.c recolours the sprite). */
void portXhairApply(s32 i)
{
    char hex[12];

    if (i < 0 || i >= PORT_XHAIR_COUNT)
    {
        return;
    }
    if (port_xhair_rgb[i] < 0)
    {
        portVideoIniSetKey("crosshair_color", "default");
    }
    else
    {
        sprintf(hex, "%06X", (unsigned) port_xhair_rgb[i]);
        portVideoIniSetKey("crosshair_color", hex);
    }
}
extern void portInputDeviceStyleSet(s32 dev, s32 style);
/* `label` overrides the option table's own row text on this sheet only - the
 * watch shares that table and keeps its wording (NULL = the table's).
 * `page` is what a SHROW_GOSHEET row opens; the other kinds leave it 0. */
struct PortSheetRow { s32 kind; const char *key; const char *label; s32 page; };

/* which keys live on which page - docs/port/ini-options-placement.md */

/* GAMEPLAY's two pages. Six rows (look up/down, auto-aim, aim control, look
 * ahead, sight on screen, ammo on screen) are Rare's per-save options, made
 * global midas.ini keys (video.c s_CfgPlayerOpt) laid over the save by options.c
 * portPlayerOptsApply, since this folder is reached before any save is picked. */
static const struct PortSheetRow port_sheet_rows_general[] = {
    /* GENERAL SETTINGS = the watch's own rows, minus the ones the CONTROL and
     * DISPLAY menus already carry. Sight and ammo on
     * screen are on HUD. */
    { SHROW_OPT, "look_updown" }, { SHROW_OPT, "auto_aim" }, { SHROW_OPT, "aim_control" },
    { SHROW_OPT, "look_ahead" },
    /* the view's three, moved here from HUD */
    { SHROW_OPT, "aim_lock" }, { SHROW_OPT, "scope_steady" }, { SHROW_OPT, "head_roll" },
};
/* PORT OPTIONS: the port's own gameplay switches. p1 ends
 * match is ini-only (default on); play as ssf character is ini-only. */
static const struct PortSheetRow port_sheet_rows_portopts[] = {
    { SHROW_OPT, "unlock_all" },   /* first */
    { SHROW_OPT, "fix_shootthrough" }, { SHROW_OPT, "distance_fading" },
    { SHROW_OPT, "true_enemy_rockets" },
    { SHROW_OPT, "unlimited_explosions" },   /* default ON */
    { SHROW_OPT, "watch_static" },           /* the watch's fuzz, default ON (vanilla) */
    { SHROW_OPT, "fast_pause" },             /* the watch at Rare's top speed, default OFF */
    { SHROW_OPT, "mouse_auto_aim" },         /* off: no Auto-Aim on the mouse, above every other Auto-Aim setting */
    { SHROW_OPT, "high_lod", "High Detail Models\n" },   /* moved from GRAPHICS */
    { SHROW_OPT, "setting_help" },   /* the help box under the folder sheets, default ON */
};
static const struct PortSheetRow port_sheet_rows_hud[] = {
    { SHROW_OPT, "sight_on_screen" }, { SHROW_OPT, "always_crosshair" },
    { SHROW_XHAIR, NULL },   /* under Always Crosshair */
    { SHROW_OPT, "ammo_on_screen" }, { SHROW_OPT, "mission_timer" },
    /* center mission timer goes here - no such option exists yet */
    { SHROW_OPT, "pause_pillars" },
    /* aim lock, stamina sway and steady scope moved to General Settings */
    /* the watch's last two (the HUD and Health options), here too */
    { SHROW_OPT, "hud_wide" }, { SHROW_OPT, "health_wide" },
    { SHROW_OPT, "hud_scale" },   /* the HUD on a 640x480 canvas */
    { SHROW_OPT, "show_fps" },   /* the corner counter (bondview2.c portDrawFpsCounter) */
    { SHROW_OPT, "armor_hud" },   /* the arcs on a body armour pickup */
};
/* MULTIPLAYER OPTIONS: settings kept separate from single player; every key is
 * multiplayer's own copy (mp_*, video.c), and the solo keys of the same name
 * are never read with two or more players (the first two split Rare's four-way
 * "Aim" row). */
static const struct PortSheetRow port_sheet_rows_mpopts[] = {
    { SHROW_OPT, "mp_sight_on_screen" }, { SHROW_OPT, "mp_no_auto_aim" }, { SHROW_OPT, "mp_aim_lock" },
    /* each player's name in the corner of their own viewport (gunfire.c) */
    { SHROW_OPT, "mp_show_names" },
    /* multiplayer's own copies of two solo options */
    { SHROW_OPT, "mp_always_crosshair" }, { SHROW_OPT, "mp_fix_shootthrough" },
    /* match rules */
    { SHROW_OPT, "mp_spawn_weapon" }, { SHROW_OPT, "mp_random_spawns" }, { SHROW_OPT, "mp_head_roll" },
    { SHROW_OPT, "mp_unlimited_explosions" }, { SHROW_OPT, "mp_ltk_health" }, { SHROW_OPT, "mp_no_armor" },
    /* MP's own HUD Layout (the watch's "hud" row is solo's) */
    { SHROW_OPT, "mp_hud_wide" },
    /* a centred 4:3 with black side bars, any window (video.c portPillarboxFrame) */
    { SHROW_OPT, "mp_pillarbox" },
    /* ...and its own cheats, unlocked from the start, never solo (lv.c), in
     * the RIGHT column */
    { SHROW_COLBREAK, NULL },
    /* the left column is full: team damage heads the right one */
    { SHROW_OPT, "mp_friendly_fire" },
    { SHROW_OPT, "mp_invisibility" }, { SHROW_OPT, "mp_dk_mode" }, { SHROW_OPT, "mp_tiny_players" },
    { SHROW_OPT, "mp_paintball" }, { SHROW_OPT, "mp_no_radar" }, { SHROW_OPT, "mp_turbo_mode" },
    { SHROW_OPT, "mp_unlimited_ammo" },   /* MP-only unlimited ammo */
};
/* THE PLAYER PAGE: input device and rebind first, then
 * everything else. The bind list itself is a page further in. */
static const struct PortSheetRow port_sheet_rows_player[] = {
    /* The player's pages: device, rebind page, then that device's own page
     * (Mouse and Keyboard for device 0, Controller for a pad). Input mode,
     * control style and layout rows live on the rebind page. */
    { SHROW_BINDDEV, NULL },
    /* the input device name on its own line below "Input", truncated to fit */
    { SHROW_DEVNAME, NULL },
    { SHROW_GOBINDS, NULL },
    { SHROW_GOSHEET, NULL, NULL, PORT_SHEET_MOUSEKB },
    { SHROW_GOSHEET, NULL, NULL, PORT_SHEET_PAD },
    { SHROW_GOSHEET, NULL, NULL, PORT_SHEET_CTLGENERAL },
};
/* The player's control pages; a row may appear on two pages (the sensitivities
 * repeat on AIMING), and it is the same option either way. There is no
 * "controller sensitivity x / y": Look-stick rates are constants in bondview2.c
 * (PORT_PADLOOK_YAW_DEG_PER_SEC), and stick_scale is a Move-stick multiplier. */
static const struct PortSheetRow port_sheet_rows_mousekb[] = {
    /* mouse_mode retired (direct only) */
    { SHROW_OPT, "mouse" }, { SHROW_OPT, "smart_mouse_lock" },
    /* a deliberate gap here */
    { SHROW_OPT, "mouse_sens_x" }, { SHROW_OPT, "mouse_sens_y" }, { SHROW_OPT, "mouse_sens_aim" },
    /* not on his list; it is the keyboard-and-mouse's relationship to the
     * pad, so it waits here */
    { SHROW_OPT, "p1_hybrid" },
    /* aim inversion: General Settings' own look_updown row, here too (murkantor/Project-Midas#35) */
    { SHROW_OPT, "look_updown" },
};
static const struct PortSheetRow port_sheet_rows_pad[] = {
    /* twin_stick and right_stick_c removed - see port_options.c */
    { SHROW_OPT, "deadzone" }, { SHROW_OPT, "deadzone_look" },
    { SHROW_OPT, "trigger_threshold" },
    /* controller sensitivity x, controller sensitivity y go here */
    /* not on his list: the rest of the old pad rows */
    { SHROW_OPT, "stick_scale" }, { SHROW_OPT, "rumble" },
    { SHROW_OPT, "pad1_gadget_switch" },   /* swap sticks: ini-only */
    { SHROW_OPT, "pad1_lr_classic" },      /* L+R held = Classic Mode (N64 pads) */
    { SHROW_OPT, "smart_mouse_lock" },     /* every device's page: the pointer is
                                            * released outside missions whatever you
                                            * play with */
};
static const struct PortSheetRow port_sheet_rows_ctlgeneral[] = {
    { SHROW_OPT, "detonate_use_reload" },   /* always_walk removed (Walk is a bind) */
    { SHROW_OPT, "shoot_next_prev" },
    { SHROW_OPT, "no_aim_crouch" },          /* aim + down does not crouch (bondview2.c) */
    { SHROW_OPT, "background_input" },       /* pads work while the window is unfocused */
};
/* AIMING page removed: every row on it was a repeat of the
 * Mouse and Keyboard or Controller page. */
/* THE TWO PICKERS: each folder row that is a submenu, and its pages top to
 * bottom. A SHROW_GOSHEET row carries the page it opens in port_sheet_row_bind. */
static const s32 port_sheet_gameplay_pages[] = { PORT_SHEET_GENERAL, PORT_SHEET_HUD, PORT_SHEET_PORTOPTS };
static const s32 port_sheet_display_pages[] = { PORT_SHEET_VIDEO, PORT_SHEET_AUDIO };   /* GRAPHICS is empty */

/** The Options folder screen's rows: its own three, or a hub's pages. */
static s32 portOptionsRowCount(void)
{
    if (port_options_hub == PORT_SHEET_GAMEPLAY)
    {
        return (s32) (sizeof(port_sheet_gameplay_pages) / sizeof(port_sheet_gameplay_pages[0]));
    }
    if (port_options_hub == PORT_SHEET_DISPLAY)
    {
        return (s32) (sizeof(port_sheet_display_pages) / sizeof(port_sheet_display_pages[0]));
    }
    return PORT_OPTIONS_ROWS;
}

static s32 portOptionsRowKind(s32 r)
{
    if (port_options_hub == PORT_SHEET_GAMEPLAY)
    {
        return port_sheet_gameplay_pages[r];
    }
    if (port_options_hub == PORT_SHEET_DISPLAY)
    {
        return port_sheet_display_pages[r];
    }
    return port_options_row_kind[r];
}

/** A picker row's text: the page's name in the sheets' lowercase. */
static const char *portSheetPageLabel(s32 kind)
{
    switch (kind)
    {
        case PORT_SHEET_GENERAL:  return "General Settings\n";
        case PORT_SHEET_HUD:      return "HUD\n";
        case PORT_SHEET_PORTOPTS: return "Port Options\n";
        case PORT_SHEET_VIDEO:    return "Video\n";
        case PORT_SHEET_AUDIO:    return "Audio\n";
        case PORT_SHEET_GRAPHICS: return "Graphics\n";
        case PORT_SHEET_MOUSEKB:    return "Mouse and Keyboard\n";
        case PORT_SHEET_PAD:        return "Controller\n";
        case PORT_SHEET_CTLGENERAL: return "General Control Options\n";
    }
    return "?\n";
}

/* VIDEO / AUDIO / GRAPHICS rows. Requested rows with no option behind them yet
 * (folder music, MP death music, crosshair style / sway, explosion shake) are
 * left out rather than added as dead rows, and go in at the marked positions
 * once they exist. */
static const struct PortSheetRow port_sheet_rows_video[] = {
    /* the whole sheet is DEFERRED to APPLY CHANGES (portSheetDeferred).
     * widescreen, msaa, classic mode (classic_mode), mipmaps and 2d filtering
     * moved here from GRAPHICS. */
    { SHROW_OPT, "fullscreen" }, { SHROW_RES, NULL }, { SHROW_OPT, "vsync" },
    { SHROW_OPT, "widescreen" },
    /* N64 AA ABOVE CLASSIC MODE, by the same rule as the watch: Classic Mode
     * does not own the bit (its classic_n64aa part decides whether it touches
     * it), so the row is not greyed while the mode is on. Both of these are
     * settings a player reads as a pair, which is why they sit together. */
    { SHROW_OPT, "n64_aa", "N64 AA\n" },
    /* CLASSIC MODE SITS OVER WHAT IT FORCES: every row below
     * it is one of port_options.c's classic keys, greyed while it is on
     * (portSheetRowDisabled). A row that classic mode does not own goes ABOVE. */
    { SHROW_OPT, "classic_mode", "Classic Mode\n" },
    /* FIELD OF VIEW above 3D resolution. Not a classic key, so Classic
     * Mode leaves it live (portSheetRowDisabled goes by key) */
    { SHROW_OPT, "hfov_wide" },
    { SHROW_OPT, "weapon_fov" },   /* beside Field of View; not a classic key */
    { SHROW_OPT, "low_res" },
    { SHROW_OPT, "filtering" }, { SHROW_OPT, "anisotropy" }, { SHROW_OPT, "msaa" },
    { SHROW_OPT, "filter_2d" },   /* no Mipmaps row: part of Classic Mode */
    { SHROW_OPT, "maxfps" },   /* show_fps: on HUD */
};
/* CLASSIC: one On/Off per part of Classic Mode, live (not deferred). A part
 * switched off is left at the player's own setting while Classic Mode is on,
 * and its VIDEO row stays editable (port_options.c portClassicOwns). */
static const struct PortSheetRow port_sheet_rows_classic[] = {
    { SHROW_OPT, "classic_res" }, { SHROW_OPT, "classic_filter" }, { SHROW_OPT, "classic_aniso" },
    { SHROW_OPT, "classic_pacing" }, { SHROW_OPT, "classic_msaa" }, { SHROW_OPT, "classic_mipmaps" },
    { SHROW_OPT, "classic_filter2d" }, { SHROW_OPT, "classic_pillars" }, { SHROW_OPT, "classic_hitches" },
    { SHROW_OPT, "classic_n64aa" }, { SHROW_OPT, "classic_composite" },
};
static const struct PortSheetRow port_sheet_rows_audio[] = {
    { SHROW_OPT, "music_volume", "Music\n" },
    /* folder music goes here */
    { SHROW_OPT, "sfx_volume", "FX\n" },
    /* disable mp death music goes here */
    /* not one of the requested rows, but it is audio, so it lives here */
    { SHROW_OPT, "stereo_pan" },
    { SHROW_OPT, "mute_unfocused" },   /* silence while the window has no focus */
};
static const struct PortSheetRow port_sheet_rows_graphics[] = {
    /* EMPTY: FOV went to VIDEO, High Detail Models to PORT
     * OPTIONS; the page is off AUDIO VISUAL's list until it has rows */
    /* swap crosshair style, crosshair sway, explosion shake go here (gun fov is on VIDEO, beside FOV) */
    /* shadow_alpha: ini-only */
};

static s32 port_sheet_rows;                          /* rows on the current sheet (0 = a placeholder page) */
static s32 port_sheet_row_kind[PORT_SHEET_ROWS_MAX];
static s32 port_sheet_row_opt[PORT_SHEET_ROWS_MAX];  /* port_options.c index, -1 for RES */
static const char *port_sheet_row_label[PORT_SHEET_ROWS_MAX]; /* this sheet's own wording, NULL = the table's */
static s32 port_sheet_has_apply;                     /* DISPLAY: the pinned APPLY CHANGES slot */
static s32 port_sheet_page;                          /* 0-based */
static s32 port_sheet_pages;
static s32 port_sheet_highlighted = -1;              /* the SLOT under the cursor, -1 = none */
static s32 port_sheet_opt_fullscreen = -1;           /* the display-mode row's option index */
static s32 portSheetResLocked(void);   /* below: the Resolution lock as the sheet shows the mode */
/* Every row on the Video sheet is deferred: a pick is stored per row as an
 * index into its value list, drawn red, and applied by APPLY CHANGES (-1 = no
 * pick). */
static s32 port_sheet_pending[PORT_SHEET_ROWS_MAX];
/* resolution candidate, kept as a SIZE: video.c rebuilds its list when
 * the window moves, so an index picked earlier could name another mode by
 * APPLY. 0 = no pick. */
static s32 port_sheet_pending_resw;
static s32 port_sheet_pending_resh;

/** Where the picked size sits in the list NOW; -1 = no pick, or it is gone. */
static s32 portSheetPendingResIdx(void)
{
    s32 i, w, h;

    for (i = 0; port_sheet_pending_resw > 0 && i < portVideoResCount(); i++)
    {
        if (portVideoResGet(i, &w, &h) && w == port_sheet_pending_resw && h == port_sheet_pending_resh)
        {
            return i;
        }
    }
    return -1;
}

static void portSheetPendingClear(void)
{
    s32 r;

    for (r = 0; r < PORT_SHEET_ROWS_MAX; r++)
    {
        port_sheet_pending[r] = -1;
    }
    port_sheet_pending_resw = 0;
    port_sheet_pending_resh = 0;
}
/* the list */
static s32 port_sheet_list_row = -1;                 /* ROW whose list is up, -1 = none */
static s32 port_sheet_list_slot;                     /* its slot on the page */
static s32 port_sheet_list_idx;                      /* the entry under the highlight */
static s32 port_sheet_list_scroll;                   /* first visible entry */
static s32 port_sheet_list_top;                      /* screen y of the first visible entry, fixed at open */
static s32 port_sheet_list_vis;                      /* entries shown at once */
static f32 port_sheet_list_anim;                     /* 0..2: line, then open */
static s32 port_sheet_list_closing;

/* Two-column sheets lay out toggles like Rare's cheat page: slots 0..11 down
 * the left, 12..23 in a second column at x 0xDC, 24 rows a page, no APPLY slot.
 * Values are right-aligned to their column's edge because the port's labels run
 * longer than Rare's cheat names. */
static s32 port_sheet_twocol;
static s32 port_sheet_twocol_split = 12; /* rows in the left column (an SHROW_COLBREAK sets it) */
/* measured on the capture: the longest left label ("Shoot-Through Fix") ends
 * at x 174, the longest right one ("Spawn with Weapon") at 352, OFF is 26 wide */
/* The long labels go in the left column and the cheats in the right, so the
 * column split sits further right, and the whole block is shifted 5 units left. */
#define PORT_SHEET_TWOCOL_SHIFT  5
#define PORT_SHEET_COL1_LABEL_X  (PORT_SHEET_LABEL_X - PORT_SHEET_TWOCOL_SHIFT)
#define PORT_SHEET_COL2_LABEL_X  0xF0   /* 8 past the left column's values */
#define PORT_SHEET_COL1_VALUE_R  0xE8   /* right edge of a left-column value */
#define PORT_SHEET_COL2_VALUE_R  (0x180 - PORT_SHEET_TWOCOL_SHIFT)  /* clear of the tabs (390) */

static s32 portSheetSlotRow(s32 slot);

/** 1 = the help box is on ([display] setting_help) */
static s32 portSheetHelpOn(void)
{
    static s32 opt = -2;

    if (opt == -2)
    {
        opt = portOptFind("setting_help");
    }
    return opt >= 0 && portOptGet(opt) != 0;
}

/** The row pitch: Rare's, or less on a page whose rows would pass y 293 or reach the help box. */
static s32 portSheetPitch(void)
{
    const s32 y0 = port_sheet_twocol ? PORT_SHEET_TWOCOL_Y0 : PORT_SHEET_ROW_Y0;
    const s32 nslots = port_sheet_twocol ? 2 * PORT_SHEET_TWOCOL_ROWS : PORT_SHEET_SLOTS;
    s32 last = 0;
    s32 slot;
    s32 p;

    for (slot = 0; slot < nslots; slot++)
    {
        const s32 line = port_sheet_twocol ? slot % PORT_SHEET_TWOCOL_ROWS : slot;

        if (line > last && portSheetSlotRow(slot) != -2)
        {
            last = line;
        }
    }
    if (!portSheetHelpOn())
    {   /* thirteen rows and APPLY: close up to end on Rare's last line */
        return (last == 0 || y0 + last * PORT_SHEET_ROW_PITCH <= PORT_SHEET_LAST_Y) ? PORT_SHEET_ROW_PITCH
                                                                                   : (PORT_SHEET_LAST_Y - y0) / last;
    }
    if (last == 0 || y0 + last * PORT_SHEET_ROW_PITCH + 0xE < PORT_SHEET_HELP_Y0 - 2)
    {
        return PORT_SHEET_ROW_PITCH;
    }
    p = (PORT_SHEET_HELP_Y0 - 2 - 0xE - y0) / last;
    return (p < PORT_SHEET_HELP_PITCH_MIN) ? PORT_SHEET_HELP_PITCH_MIN : p;
}

static s32 portSheetSlotY(s32 slot)
{
    return port_sheet_twocol ? PORT_SHEET_TWOCOL_Y0 + (slot % PORT_SHEET_TWOCOL_ROWS) * portSheetPitch()
                             : PORT_SHEET_ROW_Y0 + slot * portSheetPitch();
}

/** the slot's label x */
static s32 portSheetSlotLabelX(s32 slot)
{
    if (port_sheet_twocol)
    {
        return (slot >= PORT_SHEET_TWOCOL_ROWS) ? PORT_SHEET_COL2_LABEL_X : PORT_SHEET_COL1_LABEL_X;
    }
    return PORT_SHEET_LABEL_X;
}

/** how many slots a page has: 14 (13 rows + APPLY), or 28 in two columns */
static s32 portSheetSlotCount(void)
{
    return port_sheet_twocol ? 2 * PORT_SHEET_TWOCOL_ROWS : PORT_SHEET_SLOTS;
}

/** rows a page holds */
static s32 portSheetPageCap(void)
{
    return port_sheet_twocol ? 2 * PORT_SHEET_TWOCOL_ROWS : PORT_SHEET_PAGE_ROWS;
}

/** 1 = the cursor at (cx, cy) is in this slot's band - and, in two columns, its column */
static s32 portSheetSlotHit(s32 slot, s32 cx, s32 cy)
{
    const s32 sy = portSheetSlotY(slot);

    if (cy < sy - 1 || cy >= sy - 1 + portSheetPitch())
    {
        return FALSE;
    }
    if (port_sheet_twocol)
    {
        return (slot >= PORT_SHEET_TWOCOL_ROWS) == (cx >= PORT_SHEET_COL2_LABEL_X - 4);
    }
    return TRUE;
}

/** rows on the current page (slots 0..n-1) */
static s32 portSheetPageRows(void)
{
    s32 n = port_sheet_rows - port_sheet_page * portSheetPageCap();

    if (n > portSheetPageCap()) n = portSheetPageCap();
    if (n < 0) n = 0;
    return n;
}

/** slot -> row index; -1 = the APPLY slot; -2 = an empty slot */
static s32 portSheetSlotRow(s32 slot)
{
    if (slot == PORT_SHEET_APPLY_SLOT && !port_sheet_twocol)
    {
        return port_sheet_has_apply ? -1 : -2;
    }
    if (port_sheet_twocol && port_sheet_page == 0)
    {   /* the left column holds the rows before the break, the right the rest */
        const s32 split = (port_sheet_twocol_split < PORT_SHEET_TWOCOL_ROWS) ? port_sheet_twocol_split : PORT_SHEET_TWOCOL_ROWS;
        const s32 row = (slot < PORT_SHEET_TWOCOL_ROWS) ? ((slot < split) ? slot : -2)
                                                       : split + (slot - PORT_SHEET_TWOCOL_ROWS);

        return (slot >= 0 && slot < 2 * PORT_SHEET_TWOCOL_ROWS && row >= 0 && row < port_sheet_rows) ? row : -2;
    }
    if (slot < 0 || slot >= portSheetPageRows())
    {
        return -2;
    }
    return port_sheet_page * portSheetPageCap() + slot;
}

/** 1 = nothing on this slot takes the cursor: no row, or a text line (SHROW_DEVNAME) */
static s32 portSheetSlotInert(s32 slot)
{
    const s32 row = portSheetSlotRow(slot);

    return row == -2 || (row >= 0 && port_sheet_row_kind[row] == SHROW_DEVNAME);
}

#define PORT_SHEET_DEVNAME_INDENT 0x0C
#define PORT_SHEET_RULE_R         0x170        /* the Control sheet's player rules end clear of the tabs */
#define PORT_SHEET_COL_RULE       0x00000070   /* black @ ~44%: a pencil line on the paper */
#define PORT_SHEET_DEVNAME_MAXW   (0x170 - PORT_SHEET_LABEL_X - PORT_SHEET_DEVNAME_INDENT)   /* clear of the tabs */

/** text as is when it fits in maxw, else cut at a character with "..." on (a static copy) */
static const char *portSheetFitText(const char *text, s32 maxw)
{
    static char out[64];
    s32 h = 0, w = 0;
    s32 n;

    textMeasure(&h, &w, (char *) text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    if (w <= maxw)
    {
        return text;
    }
    for (n = (s32) strlen(text); n > 0; n--)
    {
        if (text[n - 1] == '\n')
        {
            continue;
        }
        snprintf(out, sizeof(out), "%.*s...\n", (int) ((n < (s32) sizeof(out) - 5) ? n : (s32) sizeof(out) - 5), text);
        textMeasure(&h, &w, out, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        if (w <= maxw)
        {
            return out;
        }
    }
    return text;
}

/** Build the sheet's row list from its key table; unknown keys are skipped (and named on stderr). */
static s32 port_sheet_row_bind[PORT_SHEET_ROWS_MAX]; /* SHROW_BIND: index into the shared list */

static void portSheetBuild(s32 kind);
static void portSheetClassicPreset(void);

/** Go one level IN, remembering where from. */
static void portSheetOpen(s32 kind)
{
    if (port_sheet_depth < PORT_SHEET_DEPTH_MAX)
    {
        port_sheet_parent[port_sheet_depth] = port_sheet_kind;
        port_sheet_depth++;
    }
    port_sheet_kind = kind;
    portSheetBuild(kind);
}

/** 1 = went one level OUT. 0 = this sheet came from the folder, so leave. */
static s32 portSheetUp(void)
{
    if (port_sheet_depth <= 0)
    {
        return 0;
    }
    port_sheet_depth--;
    port_sheet_kind = port_sheet_parent[port_sheet_depth];
    /* a picked-but-not-applied display mode or resolution belongs to the page
     * being left; portSheetLeave has always thrown it away, and VIDEO now backs
     * out through here instead */
    portSheetPendingClear();
    portSheetBuild(port_sheet_kind);
    return 1;
}

static void portSheetBuild(s32 kind)
{
    const struct PortSheetRow *src = NULL;
    s32 n = 0;
    s32 i;

    port_sheet_rows = 0;
    port_sheet_has_apply = (kind == PORT_SHEET_VIDEO);
    port_sheet_twocol = (kind == PORT_SHEET_MPOPTS);   /* all toggles, the cheat page's two columns */
    port_sheet_opt_fullscreen = portOptFind("fullscreen");
    if (kind == PORT_SHEET_GENERAL)  { src = port_sheet_rows_general;  n = sizeof(port_sheet_rows_general)  / sizeof(port_sheet_rows_general[0]);  }
    if (kind == PORT_SHEET_HUD)      { src = port_sheet_rows_hud;      n = sizeof(port_sheet_rows_hud)      / sizeof(port_sheet_rows_hud[0]);      }
    if (kind == PORT_SHEET_PORTOPTS) { src = port_sheet_rows_portopts; n = sizeof(port_sheet_rows_portopts) / sizeof(port_sheet_rows_portopts[0]); }
    if (kind == PORT_SHEET_MPOPTS)   { src = port_sheet_rows_mpopts;   n = sizeof(port_sheet_rows_mpopts)   / sizeof(port_sheet_rows_mpopts[0]);   }
    if (kind == PORT_SHEET_MOUSEKB)    { src = port_sheet_rows_mousekb;    n = sizeof(port_sheet_rows_mousekb)    / sizeof(port_sheet_rows_mousekb[0]);    }
    if (kind == PORT_SHEET_PAD)        { src = port_sheet_rows_pad;        n = sizeof(port_sheet_rows_pad)        / sizeof(port_sheet_rows_pad[0]);        }
    if (kind == PORT_SHEET_CTLGENERAL) { src = port_sheet_rows_ctlgeneral; n = sizeof(port_sheet_rows_ctlgeneral) / sizeof(port_sheet_rows_ctlgeneral[0]); }
    if (kind == PORT_SHEET_PLAYER)   { src = port_sheet_rows_player;   n = sizeof(port_sheet_rows_player)   / sizeof(port_sheet_rows_player[0]);   }
    if (kind == PORT_SHEET_VIDEO)    { src = port_sheet_rows_video;    n = sizeof(port_sheet_rows_video)    / sizeof(port_sheet_rows_video[0]);    }
    if (kind == PORT_SHEET_CLASSIC)  { src = port_sheet_rows_classic;  n = sizeof(port_sheet_rows_classic)  / sizeof(port_sheet_rows_classic[0]);  }
    if (kind == PORT_SHEET_AUDIO)    { src = port_sheet_rows_audio;    n = sizeof(port_sheet_rows_audio)    / sizeof(port_sheet_rows_audio[0]);    }
    if (kind == PORT_SHEET_GRAPHICS) { src = port_sheet_rows_graphics; n = sizeof(port_sheet_rows_graphics) / sizeof(port_sheet_rows_graphics[0]); }
    port_sheet_twocol_split = PORT_SHEET_TWOCOL_ROWS;
    for (i = 0; i < n && port_sheet_rows < PORT_SHEET_ROWS_MAX; i++)
    {
        if (src[i].kind == SHROW_COLBREAK)
        {
            port_sheet_twocol_split = port_sheet_rows;
            continue;
        }
        s32 opt = -1;

        if (src[i].kind == SHROW_CTRLSTYLE && !portBindModeCanN64(port_sheet_bind_dev))
        {   /* device 0 has no style of its own (it follows slot 0); for a
             * pad the row stays and is greyed in port mode instead */
            continue;
        }
        if (src[i].kind == SHROW_BINDMODE && !portBindModeCanN64(port_sheet_bind_dev))
        {   /* device 0's mode is player 1's slot's; the row changed nothing */
            continue;
        }
        if (src[i].kind == SHROW_OPT && src[i].key != NULL
            && (strcmp(src[i].key, "pad1_gadget_switch") == 0 || strcmp(src[i].key, "pad1_lr_classic") == 0)
            && !portBindModeIsN64(port_sheet_bind_dev))
        {   /* B then Z exists for a real N64 pad; in port mode next_inventory
             * is bindable and the Pro layouts have no B. L+R Classic Mode the
             * same: a port-mode pad binds classic_toggle instead */
            continue;
        }
        if ((src[i].kind == SHROW_LAYOUT || src[i].kind == SHROW_SAVELAYOUT
             || src[i].kind == SHROW_DELLAYOUT || src[i].kind == SHROW_MANAGELAYOUT)
            && port_sheet_bind_dev == 0)
        {   /* layouts are a PAD's; the keyboard (device 0) has none, and now
             * that these rows follow the page's device they would do nothing */
            continue;
        }
        if (src[i].kind == SHROW_GOSHEET
            && ((src[i].page == PORT_SHEET_MOUSEKB && port_sheet_bind_dev != 0)
                || (src[i].page == PORT_SHEET_PAD && port_sheet_bind_dev == 0)))
        {   /* the device's own page only: no Controller
             * page under the keyboard, no Mouse and Keyboard under a pad */
            continue;
        }
        if (src[i].kind == SHROW_OPT)   /* the other kinds carry no ini key here */
        {
            if (port_sheet_bind_dev != 0 && src[i].key != NULL && strncmp(src[i].key, "pad1_", 5) == 0)
            {   /* PER-PAD ROWS: the table names pad1; a pad's page edits ITS
                 * slot's key (padN_gadget_switch is per device - the same rule
                 * as PORT_SHEET_LAYOUT_DEV). No slot behind the device = no
                 * row, never pad 1. */
                char padKey[32];

                if (!portInputDeviceIniKey(port_sheet_bind_dev, src[i].key + 5, padKey, (s32) sizeof(padKey)))
                {
                    continue;
                }
                opt = portOptFind(padKey);
            }
            else
            {
                opt = portOptFind(src[i].key);
            }
            if (opt < 0)
            {
                fprintf(stderr, "ge: options sheet: no such key '%s', row skipped\n", src[i].key);
                continue;
            }
        }
        port_sheet_row_kind[port_sheet_rows] = src[i].kind;
        port_sheet_row_opt[port_sheet_rows] = opt;
        port_sheet_row_label[port_sheet_rows] = src[i].label;
        port_sheet_row_bind[port_sheet_rows] = (src[i].kind == SHROW_GOSHEET) ? src[i].page : -1;
        port_sheet_rows++;
    }
    if (kind == PORT_SHEET_CONTROL)
    {   /* just the four players */
        s32 k;

        for (k = 0; k < 4 && port_sheet_rows < PORT_SHEET_ROWS_MAX; k++)
        {
            port_sheet_row_kind[port_sheet_rows] = SHROW_PLAYERPICK;
            port_sheet_row_opt[port_sheet_rows] = -1;
            port_sheet_row_bind[port_sheet_rows] = k;
            port_sheet_rows++;
            if (port_sheet_rows < PORT_SHEET_ROWS_MAX)
            {   /* the player's device on the line under the name */
                port_sheet_row_kind[port_sheet_rows] = SHROW_DEVNAME;
                port_sheet_row_opt[port_sheet_rows] = -1;
                port_sheet_row_bind[port_sheet_rows] = k;
                port_sheet_rows++;
            }
        }
    }
    if (kind == PORT_SHEET_DISPLAY || kind == PORT_SHEET_GAMEPLAY)
    {   /* a picker: just its pages */
        const s32 *pages = (kind == PORT_SHEET_DISPLAY) ? port_sheet_display_pages : port_sheet_gameplay_pages;
        const s32 np = (kind == PORT_SHEET_DISPLAY)
                     ? (s32) (sizeof(port_sheet_display_pages) / sizeof(port_sheet_display_pages[0]))
                     : (s32) (sizeof(port_sheet_gameplay_pages) / sizeof(port_sheet_gameplay_pages[0]));
        s32 k;

        for (k = 0; k < np && port_sheet_rows < PORT_SHEET_ROWS_MAX; k++)
        {
            port_sheet_row_kind[port_sheet_rows] = SHROW_GOSHEET;
            port_sheet_row_opt[port_sheet_rows] = -1;
            port_sheet_row_bind[port_sheet_rows] = pages[k];
            port_sheet_rows++;
        }
    }
    if (kind == PORT_SHEET_BINDS)
    {   /* the bind list, rebuilt every time because its LENGTH follows the
         * device and the Port / N64 mode */
        extern s32 portInputDeviceIsRaphnet(s32 dev);
        /* A RAPHNET gets this page for its CONTROL STYLE alone:
         * it cannot be rebound and has no layouts, but the style row is the
         * one place its raphnet<chan>_style (Auto, 1.x, 2.x P1 / P2) is set */
        const s32 raph = portInputDeviceIsRaphnet(port_sheet_bind_dev);
        const s32 nb = raph ? 0 : portBindCount(port_sheet_bind_dev);
        s32 k;

        if (port_sheet_rows < PORT_SHEET_ROWS_MAX && !raph)
        {   /* RESET TO DEFAULT is the first bind row, pushing everything down */
            port_sheet_row_kind[port_sheet_rows] = SHROW_RESETBINDS;
            port_sheet_row_opt[port_sheet_rows] = -1;
            port_sheet_row_bind[port_sheet_rows] = -1;
            port_sheet_rows++;
        }
        if (port_sheet_rows < PORT_SHEET_ROWS_MAX && portBindModeCanN64(port_sheet_bind_dev) && !raph)
        {   /* not for device 0: the keyboard follows player 1's slot */
            port_sheet_row_kind[port_sheet_rows] = SHROW_BINDMODE;
            port_sheet_row_opt[port_sheet_rows] = -1;
            port_sheet_row_bind[port_sheet_rows] = -1;
            port_sheet_rows++;
        }
        if (port_sheet_bind_dev != 0 && !raph)
        {   /* THE PAD'S CONFIG, here rather than on the player page:
             * "bind config" is the layout row renamed, showing
             * the loaded one; "manage config" pops save / delete / cancel */
            if (port_sheet_rows < PORT_SHEET_ROWS_MAX)
            {
                port_sheet_row_kind[port_sheet_rows] = SHROW_LAYOUT;
                port_sheet_row_opt[port_sheet_rows] = -1;
                port_sheet_row_bind[port_sheet_rows] = -1;
                port_sheet_rows++;
            }
            if (port_sheet_rows < PORT_SHEET_ROWS_MAX)
            {
                port_sheet_row_kind[port_sheet_rows] = SHROW_MANAGELAYOUT;
                port_sheet_row_opt[port_sheet_rows] = -1;
                port_sheet_row_bind[port_sheet_rows] = -1;
                port_sheet_rows++;
            }
        }
        if (port_sheet_rows < PORT_SHEET_ROWS_MAX && portBindModeCanN64(port_sheet_bind_dev))
        {   /* ALWAYS THERE for a pad, greyed out in port mode so the menu
             * doesn't shift up and down a line - portSheetRowDisabled */
            port_sheet_row_kind[port_sheet_rows] = SHROW_CTRLSTYLE;
            port_sheet_row_opt[port_sheet_rows] = -1;
            port_sheet_row_bind[port_sheet_rows] = -1;
            port_sheet_rows++;
        }
        for (k = 0; k < nb && port_sheet_rows < PORT_SHEET_ROWS_MAX; k++)
        {
            port_sheet_row_kind[port_sheet_rows] = SHROW_BIND;
            port_sheet_row_opt[port_sheet_rows] = -1;
            port_sheet_row_bind[port_sheet_rows] = k;
            port_sheet_rows++;
        }
    }
    port_sheet_pages = (port_sheet_rows + portSheetPageCap() - 1) / portSheetPageCap();
    if (port_sheet_pages < 1) port_sheet_pages = 1;
    port_sheet_page = 0;
#ifndef TARGET_N64
    { /* GE_SHEETPAGE=n: open on that page, so a capture can photograph a row
       * that is four pages in without driving the NEXT tab there. Never set
       * in midas.ini. */
        extern char *getenv(const char *);
        const char *e = getenv("GE_SHEETPAGE");

        if (e != NULL && e[0] >= '0' && e[0] <= '9')
        {
            port_sheet_page = e[0] - '0';
            if (port_sheet_page >= port_sheet_pages)
            {
                port_sheet_page = port_sheet_pages - 1;
            }
        }
    }
#endif
    if (kind == PORT_SHEET_VIDEO)
    {   /* Classic Mode is on but its locked rows are not at the forced values
         * (an older or hand-edited ini). Re-pick the preset as pending so APPLY
         * CHANGES lands it; rows already correct get no pick. */
        extern s32 portOptGet(s32 i);

        if (portOptFind("classic_mode") >= 0 && portOptGet(portOptFind("classic_mode")) != 0)
        {
            portSheetClassicPreset();
        }
    }
}

/** The active layout's index if the player made it, else -1 (nothing to delete). */
static s32 portSheetDelLayoutIdx(void)
{
    const char *active = portInputLayoutActive(PORT_SHEET_LAYOUT_DEV);
    s32 i;

    for (i = 0; active != NULL && active[0] != '\0' && i < portInputLayoutCount(); i++)
    {
        if (strcmp(portInputLayoutName(i), active) == 0)
        {
            return portInputLayoutIsUser(i) ? i : -1;
        }
    }
    return -1;
}

/* Which layout the open question is about. The list can move under a dialog
 * - a save on another frame reorders it - so the answer acts on the index
 * that was asked about, not on whatever is active when it is answered. */
static s32 port_sheet_del_idx = -1;
static s32 port_sheet_reset_open;   /* the dialog up is Reset to Default's */
/* Reset All on the Port Options page: 1 = the first question is up, 2 = the
 * second ("Are you sure?"); only a second Yes resets. */
static s32 port_sheet_resetall_step;
/* the open dialog is the "manage config" popup (save / delete / cancel),
 * not a plain yes/no; its answer is read by index */
static s32 port_sheet_manage_open = FALSE;
/* a device assign just finished: ignore confirms until the answering
 * control is released (see interface_menu1C_portsheet) */
static s32 port_sheet_swallow_press = FALSE;

/** How many entries the row's list has: 0 = the row steps instead. */
static s32 portSheetListCount(s32 row)
{
    s32 n = 0;

    if (row < 0 || row >= port_sheet_rows)
    {
        return 0;
    }
    if (port_sheet_row_kind[row] == SHROW_SAVELAYOUT)
    {
        return 0;   /* not a list: A opens the keyboard */
    }
    if (port_sheet_row_kind[row] == SHROW_DELLAYOUT || port_sheet_row_kind[row] == SHROW_MANAGELAYOUT)
    {
        return 0;   /* not a list: A asks the question */
    }
    if (port_sheet_row_kind[row] == SHROW_BIND
        || port_sheet_row_kind[row] == SHROW_BINDDEV
        || port_sheet_row_kind[row] == SHROW_BINDMODE
        || port_sheet_row_kind[row] == SHROW_CTRLSTYLE
        || port_sheet_row_kind[row] == SHROW_PLAYERPICK
        || port_sheet_row_kind[row] == SHROW_GOBINDS
        || port_sheet_row_kind[row] == SHROW_GOSHEET
        || port_sheet_row_kind[row] == SHROW_DEVNAME
        || port_sheet_row_kind[row] == SHROW_RESETBINDS)
    {
        return 0;   /* A arms a capture / steps the device / opens a page */
    }
    if (port_sheet_row_kind[row] == SHROW_LAYOUT)
    {   /* a layout is only ever PICKED from the list (portSheetStep
         * never steps one), so any count lists - 1, 2 or past the cap */
        return portInputLayoutCount();
    }
    else if (port_sheet_row_kind[row] == SHROW_XHAIR)
    {
        n = PORT_XHAIR_COUNT;
    }
    else if (port_sheet_row_kind[row] == SHROW_RES)
    {
        n = portSheetResLocked() ? 0 : portVideoResCount();
    }
    else
    {
        if (portOptIsRanged(port_sheet_row_opt[row]))
        {   /* A number has no list: named values pick from one, while a number
             * on a scale is dragged in the locked edit below, which is what A
             * opens on these rows. */
            return 0;
        }
        n = portOptValueCount(port_sheet_row_opt[row]);
    }
    return (n > 2 && n <= PORT_SHEET_LIST_MAX) ? n : 0;
}

static const char *portSheetResText(s32 i)
{
    static char text[24];
    s32 w = 0, h = 0;

    if (i < 0 || !portVideoResGet(i, &w, &h))
    {
        return "?\n";
    }
    sprintf(text, "%dx%d\n", (int) w, (int) h);
    return text;
}

static const char *portSheetListTextAt(s32 row, s32 i)
{
    if (port_sheet_row_kind[row] == SHROW_LAYOUT)
    {
        static char text[72];

        sprintf(text, "%s%s\n", portInputLayoutName(i), portInputLayoutIsUser(i) ? " *" : "");
        return text;
    }
    if (port_sheet_row_kind[row] == SHROW_XHAIR)
    {
        return (i >= 0 && i < PORT_XHAIR_COUNT) ? port_xhair_name[i] : "?\n";
    }
    if (port_sheet_row_kind[row] == SHROW_RES)
    {
        return portSheetResText(i);
    }
    return portOptValueTextAt(port_sheet_row_opt[row], i);
}

/** The entry in use (the applied one, not a pending pick); -1 = off-list. */
static s32 portSheetListLive(s32 row)
{
    if (port_sheet_row_kind[row] == SHROW_LAYOUT)
    {
        const char *active = portInputLayoutActive(PORT_SHEET_LAYOUT_DEV);
        s32 i;

        for (i = 0; active[0] != '\0' && i < portInputLayoutCount(); i++)
        {
            if (strcmp(portInputLayoutName(i), active) == 0)
            {
                return i;
            }
        }
#ifndef TARGET_N64
        {   /* nobody has chosen: the pad is on whatever matched first - name
             * THAT one, not the list's first entry (else NSO N64 (BT) lit for
             * an Xbox pad); -1 when nothing matches */
            extern s32 portInputLayoutInUse(s32 dev);
            return portInputLayoutInUse(PORT_SHEET_LAYOUT_DEV);
        }
#else
        return -1;   /* nobody has chosen: the pad is on whatever matched first */
#endif
    }
    if (port_sheet_row_kind[row] == SHROW_XHAIR)
    {   /* -1 = a hex the list has no name for: the row reads "custom" */
        const s32 rgb = portCrosshairColor();
        s32 i;

        if (rgb == 0xFF0000)
        {
            return 0;   /* an older ini's "red": the same texels as default now */
        }
        for (i = 0; i < PORT_XHAIR_COUNT; i++)
        {
            if (port_xhair_rgb[i] == rgb)
            {
                return i;
            }
        }
        return -1;
    }
    if (port_sheet_row_kind[row] == SHROW_RES)
    {
        return portVideoResCurrent();
    }
    return portOptValueIndex(port_sheet_row_opt[row]);
}

/** Deferred = remembered until APPLY CHANGES: every row on the VIDEO sheet. */
static s32 portSheetDeferred(s32 row)
{
    return row >= 0 && row < port_sheet_rows
        && (port_sheet_row_kind[row] == SHROW_RES
            || (port_sheet_row_kind[row] == SHROW_OPT && port_sheet_kind == PORT_SHEET_VIDEO));
}

/** The row's pending pick, or -1. */
static s32 portSheetPending(s32 row)
{
    if (row < 0 || row >= port_sheet_rows)
    {
        return -1;
    }
    if (port_sheet_row_kind[row] == SHROW_RES)
    {
        return portSheetPendingResIdx();
    }
    if (portSheetDeferred(row))
    {
        return port_sheet_pending[row];
    }
    return -1;
}

static s32 portSheetAnyPending(void)
{
    s32 r;

    if (portSheetPendingResIdx() >= 0)
    {
        return TRUE;
    }
    for (r = 0; r < port_sheet_rows; r++)
    {
        if (port_sheet_pending[r] >= 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* Classic Mode (the classic_mode key): turning it ON also picks hardware 3D
 * resolution, 3-point filtering, 8x anisotropy and hardware max fps as pending
 * picks for APPLY CHANGES to land together - and the N64 AA bit, if the mode's
 * classic_n64aa part is on (portSheetClassicPreset). Turning it OFF restores
 * the remembered values and clears the bit the mode set. */
static void portSheetPendSetKey(const char *key, s32 value)
{
    extern s32 portOptIndexOfValue(s32 i, s32 v);
    const s32 opt = portOptFind(key);
    s32 r;

    for (r = 0; opt >= 0 && r < port_sheet_rows; r++)
    {
        if (port_sheet_row_kind[r] == SHROW_OPT && port_sheet_row_opt[r] == opt)
        {
            const s32 idx = portOptIndexOfValue(opt, value);

            port_sheet_pending[r] = (idx < 0 || idx == portOptValueIndex(opt)) ? -1 : idx;
            return;
        }
    }
}

/* What classic mode owns, and the value it forces on each: port_options.c
 * ("classic mode"), ONE table shared with the watch, which sets them live
 * where this sheet pends them. PORT_SHEET_CLASSIC_KEYS only sizes the local
 * buffers; the count that is walked is portClassicCount(). */
extern s32         portClassicCount(void);
extern const char *portClassicKey(s32 i);
extern s32         portClassicVal(s32 i);
extern s32         portClassicOwns(const char *key);
extern s32         portClassicPartOn(s32 i);
#define PORT_SHEET_CLASSIC_KEYS 16

static void portSheetClassicPreset(void)
{
    extern s32 portClassicSetsN64Aa(void);   /* video.c: the classic_n64aa part */
    s32 i;

    for (i = 0; i < portClassicCount(); i++)
    {
        if (portClassicPartOn(i))   /* a part switched off on the CLASSIC sheet is not forced */
        {
            portSheetPendSetKey(portClassicKey(i), portClassicVal(i));
        }
    }
    /* The mode's ONLY link to the AA bit, pended as one more pick. It has to be
     * here rather than left to the choke point: APPLY CHANGES lands the whole
     * sheet under portClassicSetHold, which suppresses the transition - the
     * sheet IS the transition. Without this pick the preset would come on with
     * N64 edges still off, which is not the look it promises. */
    if (portClassicSetsN64Aa())
    {
        portSheetPendSetKey("n64_aa", 1);
    }
}

/** Is classic mode ON as this sheet shows it - the pending pick if there is one, else live? */
static s32 portSheetClassicShownOn(void)
{
    const s32 mode = portOptFind("classic_mode");
    s32 r;

    if (mode < 0 || port_sheet_kind != PORT_SHEET_VIDEO)
    {
        return FALSE;
    }
    for (r = 0; r < port_sheet_rows; r++)
    {
        if (port_sheet_row_kind[r] == SHROW_OPT && port_sheet_row_opt[r] == mode && port_sheet_pending[r] >= 0)
        {
            return port_sheet_pending[r] == 1;
        }
    }
    return portOptGet(mode) != 0;
}

/** The display mode as this sheet shows it - the pending pick if there is one, else
 * live (0 windowed, 1 borderless, 2 fullscreen; the row's index is its value). */
static s32 portSheetDisplayModeShown(void)
{
    s32 r;

    if (port_sheet_opt_fullscreen < 0)
    {
        return -1;
    }
    for (r = 0; r < port_sheet_rows; r++)
    {
        if (port_sheet_row_kind[r] == SHROW_OPT && port_sheet_row_opt[r] == port_sheet_opt_fullscreen
            && port_sheet_pending[r] >= 0)
        {
            return port_sheet_pending[r];
        }
    }
    return portOptGet(port_sheet_opt_fullscreen);
}

/** Resolution has no choice ("Display") only in borderless, as the sheet shows the
 * mode: picking Windowed or Fullscreen opens it before APPLY CHANGES, which sets
 * the mode first and the size after (portSheetApply). */
static s32 portSheetResLocked(void)
{
    const s32 mode = portSheetDisplayModeShown();

    return (mode >= 0) ? (mode == 1) : portVideoResLocked();
}

/* Turning Classic Mode OFF restores the four forced settings to what they were
 * before (snapshot kept in the ini as classic_saved, taken by portSheetApply),
 * as pending picks the player sees in red and APPLY lands. */
static void portSheetClassicRevert(void)
{
    extern s32 portVideoClassicSavedGet(s32 *v);
    extern s32 portClassicSetsN64Aa(void);   /* video.c: the classic_n64aa part */
    s32 v[PORT_SHEET_CLASSIC_KEYS];
    s32 n;
    s32 i, r;

    /* the ON pick's own forced picks go first, or OFF before APPLY leaves
     * them pending red on rows that just unlocked */
    for (r = 0; r < port_sheet_rows; r++)
    {
        if (port_sheet_row_kind[r] == SHROW_OPT && port_sheet_row_opt[r] >= 0
            && portClassicOwns(portOptKey(port_sheet_row_opt[r])))
        {
            port_sheet_pending[r] = -1;
        }
    }
    if (portOptGet(portOptFind("classic_mode")) == 0)
    {   /* never applied: live already holds what they were before, and a
         * snapshot here could only be an OLD cycle's */
        return;
    }
    n = portVideoClassicSavedGet(v);   /* 4 from an older ini: only those go back */
    for (i = 0; i < n && i < portClassicCount(); i++)
    {
        if (portClassicPartOn(i))   /* only what Classic Mode forced goes back */
        {
            portSheetPendSetKey(portClassicKey(i), v[i]);
        }
    }
    /* ...and the bit the mode turned on, on the same terms as the preset's own
     * pick: only if the mode owns it. A part-off mode never touched the bit, so
     * nothing here may either. */
    if (portClassicSetsN64Aa())
    {
        portSheetPendSetKey("n64_aa", 0);
    }
}

/** Live classic_mode 0 -> pending 1 on this apply? Then remember the four first. */
static void portSheetClassicSnapshot(void)
{
    extern void portVideoClassicSavedSet(const s32 *v);
    const s32 mode = portOptFind("classic_mode");
    s32 v[PORT_SHEET_CLASSIC_KEYS];
    s32 r, i;

    extern s32 portVideoClassicSavedGet(s32 *v);

    /* ALREADY ON with nothing remembered (classic_mode=1 from the launcher,
     * the console or an old ini): the repair picks portSheetOpen pended
     * are about to replace the player's own values, so remember those
     * unless this apply turns it off */
    if (mode >= 0 && portOptGet(mode) != 0 && portSheetClassicShownOn() && portVideoClassicSavedGet(v) == 0)
    {
        for (i = 0; i < portClassicCount() && i < PORT_SHEET_CLASSIC_KEYS; i++)
        {
            v[i] = portOptGet(portOptFind(portClassicKey(i)));
        }
        portVideoClassicSavedSet(v);
        return;
    }
    for (r = 0; mode >= 0 && r < port_sheet_rows; r++)
    {
        if (port_sheet_row_kind[r] == SHROW_OPT && port_sheet_row_opt[r] == mode
            && port_sheet_pending[r] == 1 && portOptGet(mode) == 0)
        {
            for (i = 0; i < portClassicCount() && i < PORT_SHEET_CLASSIC_KEYS; i++)
            {
                v[i] = portOptGet(portOptFind(portClassicKey(i)));
            }
            portVideoClassicSavedSet(v);
            return;
        }
    }
}

/** Take entry idx for the row: a deferred row only remembers it, any other applies it now. */
static void portSheetPick(s32 row, s32 idx)
{
    if (port_sheet_row_kind[row] == SHROW_LAYOUT)
    {
        portInputLayoutApply(PORT_SHEET_LAYOUT_DEV, idx);
        return;
    }
    if (port_sheet_row_kind[row] == SHROW_XHAIR)
    {   /* live AND saved in one call: portVideoIniSetKey routes the string
         * through the same setter midas.ini does and rewrites the key in place */
        char hex[12];

        if (idx < 0 || idx >= PORT_XHAIR_COUNT)
        {
            return;
        }
        if (port_xhair_rgb[idx] < 0)
        {
            portVideoIniSetKey("crosshair_color", "default");
        }
        else
        {
            sprintf(hex, "%06X", (unsigned) port_xhair_rgb[idx]);
            portVideoIniSetKey("crosshair_color", hex);
        }
        return;
    }
    if (port_sheet_row_kind[row] == SHROW_RES)
    {
        const s32 modeSame = portSheetDisplayModeShown() == portOptGet(port_sheet_opt_fullscreen);

        /* the live size is "no change" only while the mode stays: leaving
         * borderless, the desktop size is a real pick for the new window */
        if ((idx == portVideoResCurrent() && modeSame)
            || !portVideoResGet(idx, &port_sheet_pending_resw, &port_sheet_pending_resh))
        {
            port_sheet_pending_resw = 0;
            port_sheet_pending_resh = 0;
        }
    }
    else if (portSheetDeferred(row))
    {
        const s32 opt = port_sheet_row_opt[row];

        port_sheet_pending[row] = (idx == portOptValueIndex(opt)) ? -1 : idx;
        if (opt == port_sheet_opt_fullscreen && portSheetResLocked())
        {   /* borderless takes no size: a size picked first is dropped, not left red */
            port_sheet_pending_resw = 0;
            port_sheet_pending_resh = 0;
        }
        if (strcmp(portOptKey(opt), "classic_mode") == 0)
        {
            if (idx == 1)
            {
                portSheetClassicPreset();
            }
            else
            {
                portSheetClassicRevert();
            }
        }
    }
    else
    {
        portOptSetIndex(port_sheet_row_opt[row], idx);
    }
}

/** Left / right on a row: a deferred row moves its pick, any other steps live. */
static void portSheetStep(s32 row, s32 dir)
{
    if (port_sheet_row_kind[row] == SHROW_LAYOUT || port_sheet_row_kind[row] == SHROW_SAVELAYOUT
        || port_sheet_row_kind[row] == SHROW_DELLAYOUT || port_sheet_row_kind[row] == SHROW_BIND
        || port_sheet_row_kind[row] == SHROW_PLAYERPICK || port_sheet_row_kind[row] == SHROW_GOBINDS
        || port_sheet_row_kind[row] == SHROW_GOSHEET || port_sheet_row_kind[row] == SHROW_DEVNAME
        || port_sheet_row_kind[row] == SHROW_RESETBINDS)
    {
        return;   /* a layout is picked from the list, not stepped past */
    }
    if (port_sheet_row_kind[row] == SHROW_BINDDEV)
    {   /* NOT a stepper: A arms the assign, and the player presses a
         * button on the device instead of cycling (see
         * interface_menu1C_portsheet). Left / right do nothing here. */
        (void) dir;
        return;
    }
    if (port_sheet_row_kind[row] == SHROW_BINDMODE)
    {
        if (portBindModeCanN64(port_sheet_bind_dev))
        {
            portBindModeToggle(port_sheet_bind_dev);
            portSheetBuild(port_sheet_kind);   /* n64 mode adds the style row */
        }
        return;
    }
    if (port_sheet_row_kind[row] == SHROW_CTRLSTYLE)
    {   /* auto, 1.1 .. 1.4, then each 2.x as P1 and P2 -
         * 13 entries, wrapping; portSheetStyleMenu / portSheetStyleCode map
         * the menu position to the stored code */
        s32 v = portSheetStyleMenu(portInputDeviceStyleGet(port_sheet_bind_dev));

        v += (dir > 0) ? 1 : -1;
        if (v > 12) v = 0;
        if (v < 0) v = 12;
        portInputDeviceStyleSet(port_sheet_bind_dev, portSheetStyleCode(v));
        return;
    }
    if (port_sheet_row_kind[row] == SHROW_XHAIR)
    {
        s32 i = portSheetListLive(row);

        if (i < 0)
        {
            i = 0;   /* a custom hex enters the list at its start */
        }
        i += (dir > 0) ? 1 : -1;
        if (i >= PORT_XHAIR_COUNT) i = 0;
        if (i < 0) i = PORT_XHAIR_COUNT - 1;
        portSheetPick(row, i);
        return;
    }
    if (portSheetDeferred(row))
    {
        s32 n = (port_sheet_row_kind[row] == SHROW_RES) ? (portSheetResLocked() ? 0 : portVideoResCount())
                                                        : portOptValueCount(port_sheet_row_opt[row]);
        s32 i = portSheetPending(row);

        if (n <= 0)
        {
            return;
        }
        if (i < 0)
        {
            i = portSheetListLive(row);
            if (i < 0 && port_sheet_row_kind[row] == SHROW_OPT)
            {   /* an off-list Max FPS (100) steps to its neighbour
                 * (120 / 60), not from entry 0 to Uncapped / 240. -1 .. n,
                 * which the step and the wrap below bring into the list */
                i = portOptValueIndexNear(port_sheet_row_opt[row], (dir > 0) ? 1 : -1);
            }
            else if (i < 0)
            {
                i = 0;
            }
        }
        i += (dir > 0) ? 1 : -1;
        if (i >= n) i = 0;
        if (i < 0) i = n - 1;
        portSheetPick(row, i);
    }
    else
    {
        portOptStep(port_sheet_row_opt[row], dir);
    }
}

/* ---- THE ANALOG RAMP ------------------------------------------------------
 *
 * On ranged rows (a number on a scale: sensitivities, deadzone, trigger point,
 * stick scale, field of view, shadow strength) holding the stick left/right
 * changes the value, slowly for a gentle push and quickly for a full one.
 * portOptIsRanged decides which rows qualify, so the set follows the table.
 *
 * The rate is proportional to deflection, capped at PORT_SHEET_RAMP_PER_SEC of
 * the row's whole range per second, so long scales move in proportion. The
 * deadzone and full-deflection clamp match the frontend cursor's (5 and 70 in
 * frontUpdateControlStickPosition), so the stick feels the same everywhere.
 *
 * Unlike a held button, this:
 *   - banks the fraction between frames, so a gentle hold still advances;
 *   - runs on g_GlobalTimerDelta, so the speed is the same at any tickhz;
 *   - clamps at the ends instead of wrapping (portOptStep wraps by design for
 *     a press), which is why it sets an index rather than stepping.
 *
 * It only runs inside the locked edit below, where the crosshair is pinned, so
 * the stick dragging the value cannot also walk the crosshair off the row.
 */
#define PORT_SHEET_RAMP_PER_SEC 0.20f /* of the row's range, at full deflection */
#define PORT_SHEET_RAMP_DEAD    5     /* the frontend cursor's own stick deadzone */
#define PORT_SHEET_RAMP_FULL    70    /* and its own full-deflection clamp */
#define PORT_SHEET_RAMP_CLICK   4     /* steps between tick sounds: every one is a machine gun */

static s32 port_sheet_ramp_row = -1;  /* the row it is banking for, -1 = not ramping */
static f32 port_sheet_ramp_bank;      /* part of a step, carried between frames */
static s32 port_sheet_ramp_click;
static f32 port_sheet_ramp_fields;    /* GE_RAMPDBG: 60Hz fields this push has run for */
static s32 port_sheet_ramp_steps;     /* GE_RAMPDBG: steps it has applied */

#ifndef TARGET_N64
/* GE_RAMPDBG=1: log one line per stick push, showing the share of the row's
 * range moved per second of game time (what PORT_SHEET_RAMP_PER_SEC asks for).
 * Measure by summing g_GlobalTimerDelta, not by counting input polls, since a
 * poll can cover anywhere from 1 to 8 fields. */
static void portSheetRampReport(void)
{
    extern char *getenv(const char *);
    static s32 dbg = -1;

    if (dbg < 0)
    {
        dbg = getenv("GE_RAMPDBG") != NULL;
    }
    if (dbg && port_sheet_ramp_fields > 0.0f && port_sheet_ramp_row >= 0)
    {
        const s32 count = portOptValueCount(port_sheet_row_opt[port_sheet_ramp_row]);
        const f32 secs = port_sheet_ramp_fields / 60.0f;

        fprintf(stderr, "ge: ramp: %d steps of %d over %.2fs = %.1f%% of range per sec\n",
                (int) port_sheet_ramp_steps, (int) count, secs,
                (count > 0 && secs > 0.0f) ? (f32) port_sheet_ramp_steps * 100.0f / ((f32) count * secs) : 0.0f);
    }
    port_sheet_ramp_fields = 0.0f;
    port_sheet_ramp_steps = 0;
}
#else
#define portSheetRampReport()
#endif

/** Stop ramping. The ini hold belongs to the EDIT that contains it, not here. */
static void portSheetRampEnd(void)
{
    portSheetRampReport();
    port_sheet_ramp_row = -1;
    port_sheet_ramp_bank = 0.0f;
}

/** Move a ranged row by whole steps, CLAMPED at both ends (a press wraps; a drag must not). */
static void portSheetRampApply(s32 row, s32 steps)
{
    const s32 opt = port_sheet_row_opt[row];
    const s32 count = portOptValueCount(opt);
    /* An off-grid ini value starts from its grid neighbour on the far side of
     * the push, matching portOptStep's first move;
     * -1 / count for a value outside the scale, which the clamp below brings back in. */
    s32 idx = portOptValueIndexNear(opt, steps);

    if (count < 2)
    {
        return;
    }
    idx += steps;
    if (idx >= count)
    {
        idx = count - 1;
    }
    if (idx < 0)
    {
        idx = 0;
    }
    portOptSetIndex(opt, idx);
}

/** Run the ramp for the highlighted row. TRUE while it owns the horizontal stick. */
static s32 portSheetRampTick(s32 row)
{
    s32 raw;
    s32 mag;
    s32 count;
    s32 whole;
    f32 t;

    if (row < 0 || port_sheet_row_kind[row] != SHROW_OPT
        || !portOptIsRanged(port_sheet_row_opt[row]))
    {
        portSheetRampEnd();
        return FALSE;
    }
    raw = joyGetStickX(PLAYER_1);
    mag = (raw < 0) ? -raw : raw;
    count = portOptValueCount(port_sheet_row_opt[row]);
    if (mag <= PORT_SHEET_RAMP_DEAD || count < 2)
    {
        portSheetRampEnd();
        return FALSE;
    }
    if (port_sheet_ramp_row != row)
    {   /* A new push: seed the bank with one whole step so any push past the
         * deadzone moves the value at once, otherwise a gentle nudge looks
         * dead; after that the proportional rate takes over. */
        portSheetRampEnd();
        port_sheet_ramp_row = row;
        port_sheet_ramp_click = 0;
        port_sheet_ramp_bank = 1.0f;
    }
    if (mag > PORT_SHEET_RAMP_FULL)
    {
        mag = PORT_SHEET_RAMP_FULL;
    }
    t = (f32) (mag - PORT_SHEET_RAMP_DEAD) / (f32) (PORT_SHEET_RAMP_FULL - PORT_SHEET_RAMP_DEAD);
    port_sheet_ramp_bank += (f32) count * PORT_SHEET_RAMP_PER_SEC * t * g_GlobalTimerDelta / 60.0f;
    port_sheet_ramp_fields += g_GlobalTimerDelta;

    whole = (s32) port_sheet_ramp_bank;
    if (whole > 0)
    {
        port_sheet_ramp_bank -= (f32) whole;
        port_sheet_ramp_steps += whole;
        portSheetRampApply(row, (raw > 0) ? whole : -whole);
        if (port_sheet_ramp_click <= 0)
        {
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
            port_sheet_ramp_click = PORT_SHEET_RAMP_CLICK;
        }
        port_sheet_ramp_click -= whole;
    }
    return TRUE;
}

/* ---- THE LOCKED EDIT ------------------------------------------------------
 *
 * A ranged row is not changed by hovering it. A opens an edit: the crosshair is
 * pinned, the stick ramps the number, the D-pad / C buttons and the wheel step
 * it, and the edit ends on A (keep) or B (restore the value it had when the
 * edit opened). Nothing else on the sheet takes input meanwhile, since the
 * stick dragging the number would otherwise also move the crosshair off it.
 *
 * The whole edit is one ini write however it ends (portOptSaveHold), however
 * many times the value changes in between.
 */
static s32 port_sheet_edit_row = -1;   /* the row being edited, -1 = none */
static s32 port_sheet_edit_undo;       /* its RAW value when the edit opened; B restores it exactly (an off-grid 13% deadzone has no index) */
static f32 port_sheet_edit_cx;         /* where the crosshair is pinned for the edit */
static f32 port_sheet_edit_cy;

static void portSheetEditBegin(s32 row)
{
    port_sheet_edit_row = row;
    port_sheet_edit_undo = portOptGet(port_sheet_row_opt[row]);
    port_sheet_edit_cx = cursor_h_pos;
    port_sheet_edit_cy = cursor_v_pos;
    portSheetRampEnd();
    portOptSaveHold(TRUE);
}

/** End the edit: keep what is on screen, or put back what was there when it opened. */
static void portSheetEditEnd(s32 keep)
{
    if (port_sheet_edit_row < 0)
    {
        return;
    }
    if (!keep)
    {
        portOptSetValue(port_sheet_row_opt[port_sheet_edit_row], port_sheet_edit_undo);
    }
    port_sheet_edit_row = -1;
    portSheetRampEnd();
    portOptSaveHold(FALSE);   /* one write for the whole edit */
}

/** APPLY CHANGES: every pending row in sheet order - the display mode is
 * first on the sheet, and it decides whether a resolution may be picked -
 * then the resolution. */
static void portSheetApply(void)
{
    s32 r;
    s32 resW = 0, resH = 0;
    /* The resolution pick is an index into video.c's list, which is rebuilt
     * when the window size changes (as the display mode below can do), so
     * remember its size now and find it again afterwards, or a windowed <->
     * exclusive switch lands one entry off. */
    const s32 haveRes = (portSheetPendingResIdx() >= 0) && portVideoResGet(portSheetPendingResIdx(), &resW, &resH);
    /* CLASSIC MODE OFF on this apply: a saved value the row's list does not
     * name (a launcher Max FPS of 165) could not be pended as a pick, so it
     * is put back raw after the picks land, as the watch does. Only
     * on rows the player left without a pick of their own. */
    const s32 modeOpt = portOptFind("classic_mode");
    s32 classicOff = 0;
    s32 classicRaw[PORT_SHEET_CLASSIC_KEYS];
    s32 classicUse[PORT_SHEET_CLASSIC_KEYS] = { 0 };
    s32 classicRawN = 0;
    s32 i;

    for (r = 0; modeOpt >= 0 && r < port_sheet_rows; r++)
    {
        if (port_sheet_row_kind[r] == SHROW_OPT && port_sheet_row_opt[r] == modeOpt)
        {
            classicOff = (port_sheet_pending[r] == 0) && (portOptGet(modeOpt) != 0);
        }
    }
    if (classicOff)
    {
        extern s32 portVideoClassicSavedGet(s32 *v);
        extern s32 portOptIndexOfValue(s32 i, s32 v);

        classicRawN = portVideoClassicSavedGet(classicRaw);
        for (i = 0; i < classicRawN && i < portClassicCount(); i++)
        {
            const s32 opt = portOptFind(portClassicKey(i));

            if (opt < 0 || !portClassicPartOn(i) || portOptIndexOfValue(opt, classicRaw[i]) >= 0)
            {
                continue;   /* on the list: the pick did it */
            }
            classicUse[i] = 1;
            for (r = 0; r < port_sheet_rows; r++)
            {
                if (port_sheet_row_kind[r] == SHROW_OPT && port_sheet_row_opt[r] == opt
                    && port_sheet_pending[r] >= 0)
                {
                    classicUse[i] = 0;   /* the player picked something */
                }
            }
        }
    }

    portSheetClassicSnapshot();   /* before anything lands: the values being replaced */
    /* The sheet lands its own plan: portSheetClassicPreset/Revert pended the
     * classic values as red picks when the player chose the mode, and this loop
     * commits them. The choke point must stand down, or classic_mode landing mid-loop
     * would apply the mode on its own and the pended values would then land on
     * top of it - correct only by luck, and only while the row order happens to
     * favour it. This is the mechanism that makes that question unaskable. */
    portClassicSetHold(1);
    for (r = 0; r < port_sheet_rows; r++)
    {
        if (port_sheet_pending[r] >= 0 && port_sheet_row_kind[r] == SHROW_OPT)
        {
            portOptSetIndex(port_sheet_row_opt[r], port_sheet_pending[r]);
        }
    }
    for (i = 0; i < classicRawN && i < portClassicCount(); i++)
    {
        if (classicUse[i])
        {
            portOptSetValue(portOptFind(portClassicKey(i)), classicRaw[i]);
        }
    }
    portClassicSetHold(0);
    if (haveRes)
    {
        const s32 n = portVideoResCount();

        for (r = 0; r < n; r++)
        {
            s32 w = 0, h = 0;

            if (portVideoResGet(r, &w, &h) && w == resW && h == resH)
            {
                portVideoResApply(r);
                break;
            }
        }
    }
    portSheetPendingClear();
}

/* THE STYLE ROW'S 13 POSITIONS <-> input_pc.c's stored code: -1 auto,
 * 0..3 = 1.1 .. 1.4, 4..7 = 2.1 .. 2.4 as controller 1, 8..11 = the same as
 * controller 2 ("2.1p2"). Menu: 0 auto, 1..4 the 1.x, then 5 = 2.1 p1,
 * 6 = 2.1 p2, 7 = 2.2 p1 ... 12 = 2.4 p2. */
static s32 portSheetStyleMenu(s32 code)
{
    if (code < 0) return 0;
    if (code < 4) return code + 1;
    if (code < 8) return 5 + (code - 4) * 2;
    if (code < 12) return 6 + (code - 8) * 2;
    return 0;
}

static s32 portSheetStyleCode(s32 menu)
{
    if (menu <= 0) return -1;
    if (menu <= 4) return menu - 1;
    return ((menu - 5) & 1) ? 8 + (menu - 6) / 2 : 4 + (menu - 5) / 2;
}

/** 1 = the row is drawn dim and takes no input: the x.x style while the
 * port is driving the pad (the styles are not what is running then). */
static s32 portSheetRowDisabled(s32 row)
{
    extern s32 portInputDeviceIsRaphnet(s32 dev);
    s32 kind;

    if (row < 0 || row >= port_sheet_rows)
    {
        return 0;
    }
    kind = port_sheet_row_kind[row];
    if (kind == SHROW_CTRLSTYLE && !portBindModeIsN64(port_sheet_bind_dev))
    {
        return 1;
    }
    if (kind == SHROW_OPT && port_sheet_row_opt[row] >= 0 && portSheetClassicShownOn())
    {   /* Classic Mode greys out the rows it forces, from the moment it is
         * picked (while the forced values are still pending), so they can't be
         * edited out from under the preset before APPLY CHANGES. */
        if (portClassicOwns(portOptKey(port_sheet_row_opt[row])))
        {
            return 1;
        }
    }
    if (portInputDeviceIsRaphnet(port_sheet_bind_dev))
    {   /* A raphnet raw slot passes true N64 poll data through 1:1
         * (STICKMODE_RAW in input_pc.c), so it cannot be rebound or deadzoned
         * and these rows are dimmed rather than hidden; Rebind Controller stays
         * available but only shows the Control Style row. */
        if (kind == SHROW_GOSHEET && port_sheet_row_bind[row] == PORT_SHEET_CTLGENERAL)
        {
            return 1;
        }
        if (kind == SHROW_OPT && port_sheet_row_opt[row] >= 0)
        {
            const char *k = portOptKey(port_sheet_row_opt[row]);

            if (strcmp(k, "deadzone") == 0 || strcmp(k, "deadzone_look") == 0 || strcmp(k, "trigger_threshold") == 0
                || strcmp(k, "stick_scale") == 0)
            {
                return 1;
            }
        }
    }
    return 0;
}

/** The value column: the pending pick (red) if there is one, else what is live. */
static const char *portSheetValueText(s32 row, s32 *colour)
{
    s32 pend = portSheetPending(row);

    *colour = 0xFF;
    if (portSheetRowDisabled(row))
    {
        *colour = PORT_SHEET_COL_DIM;
    }
    if (pend >= 0)
    {
        *colour = PORT_SHEET_COL_PENDING;
        return portSheetListTextAt(row, pend);
    }
    if (port_sheet_row_kind[row] == SHROW_SAVELAYOUT)
    {
        return (portInputDeviceCount() > 1) ? "Press A\n" : "No Pad\n";
    }
    if (port_sheet_row_kind[row] == SHROW_MANAGELAYOUT)
    {
        return "\n";   /* an action, not a value */
    }
    if (port_sheet_row_kind[row] == SHROW_PLAYERPICK)
    {   /* the device is the line under it (SHROW_DEVNAME) */
        return "\n";
    }
    if (port_sheet_row_kind[row] == SHROW_GOBINDS || port_sheet_row_kind[row] == SHROW_GOSHEET
        || port_sheet_row_kind[row] == SHROW_DEVNAME || port_sheet_row_kind[row] == SHROW_RESETBINDS)
    {
        return "\n";   /* a page, not a value, so no "press A" text */
    }
    if (port_sheet_row_kind[row] == SHROW_BINDDEV)
    {
        extern s32 portInputAssignActive(void);

        return "\n";   /* the name (or the prompt) is the line under it, SHROW_DEVNAME */
    }
    if (port_sheet_row_kind[row] == SHROW_BINDMODE)
    {   /* no longer gated on what the device looks like - see
         * portBindModeCanN64 in port_watchpages.c */
        return portBindModeIsN64(port_sheet_bind_dev) ? "N64\n" : "Port\n";
    }
    if (port_sheet_row_kind[row] == SHROW_CTRLSTYLE)
    {   /* Rare's own control style names; the 2.x styles appear twice, where P1 / P2 says whether
         * this pad is controller 1 or controller 2 of the pair. */
        static const char *const name[13] = {
            "Auto\n",
            "1.1 Honey\n", "1.2 Solitaire\n", "1.3 Kissy\n", "1.4 Goodnight\n",
            "2.1 Plenty P1\n", "2.1 Plenty P2\n", "2.2 Galore P1\n", "2.2 Galore P2\n",
            "2.3 Domino P1\n", "2.3 Domino P2\n", "2.4 Goodhead P1\n", "2.4 Goodhead P2\n"
        };
        const s32 v = portSheetStyleMenu(portInputDeviceStyleGet(port_sheet_bind_dev));

        return name[(v >= 0 && v <= 12) ? v : 0];
    }
    if (port_sheet_row_kind[row] == SHROW_BIND)
    {
        if (portBindCapturingRow(port_sheet_row_bind[row]))
        {
            *colour = PORT_SHEET_COL_PENDING;
            return "Press a Key\n";
        }
        return portBindValueText(port_sheet_bind_dev, port_sheet_row_bind[row]);
    }
    if (port_sheet_row_kind[row] == SHROW_DELLAYOUT)
    {   /* only one a player made: the shipped layouts are ours, and deleting
         * the one a pad is using would strand it on nothing */
        const s32 i = portSheetDelLayoutIdx();

        if (portInputDeviceCount() <= 1)
        {
            return "No Pad\n";
        }
        if (i < 0)
        {
            *colour = PORT_SHEET_COL_DIM;
            return "Built In\n";
        }
        return "Press A\n";
    }
    if (port_sheet_row_kind[row] == SHROW_LAYOUT)
    {
        static char text[72];
        const s32 i = portSheetListLive(row);

        if (portInputDeviceCount() <= 1)
        {
            return "No Pad\n";
        }
        sprintf(text, "%s\n", (i >= 0) ? portInputLayoutName(i) : "Default");
        return text;
    }
    if (port_sheet_row_kind[row] == SHROW_XHAIR)
    {
        const s32 i = portSheetListLive(row);

        return (i >= 0) ? port_xhair_name[i] : "Custom\n";
    }
    if (port_sheet_row_kind[row] == SHROW_RES)
    {
        return portSheetResLocked() ? "Display\n" : portSheetResText(portVideoResCurrent());
    }
    if (!portOptAvailable(port_sheet_row_opt[row]))
    {
        *colour = PORT_SHEET_COL_DIM;
    }
    return portOptValueText(port_sheet_row_opt[row]);
}

static const char *portSheetLabel(s32 row)
{
    if (port_sheet_row_kind[row] == SHROW_LAYOUT) return "Bind Config\n";
    if (port_sheet_row_kind[row] == SHROW_SAVELAYOUT) return "Save This Layout\n";
    if (port_sheet_row_kind[row] == SHROW_DELLAYOUT) return "Delete This Layout\n";
    if (port_sheet_row_kind[row] == SHROW_MANAGELAYOUT) return "Manage Config\n";
    if (port_sheet_row_kind[row] == SHROW_PLAYERPICK)
    {
        static char text[16];
        sprintf(text, "Player %d\n", (s32) (port_sheet_row_bind[row] + 1));
        return text;
    }
    if (port_sheet_row_kind[row] == SHROW_GOBINDS)
    {   /* the keyboard is not a controller */
        return (port_sheet_bind_dev == 0) ? "Rebind Inputs\n" : "Rebind Controller\n";
    }
    if (port_sheet_row_kind[row] == SHROW_GOSHEET) return portSheetPageLabel(port_sheet_row_bind[row]);
    if (port_sheet_row_kind[row] == SHROW_BINDDEV) return "Input Device\n";
    if (port_sheet_row_kind[row] == SHROW_RESETBINDS) return "Reset to Default\n";
    if (port_sheet_row_kind[row] == SHROW_DEVNAME)
    {   /* the device, or the assign prompt while one is armed; cut to the tabs.
         * bind = the player it names on the Control sheet, -1 = the page's own */
        extern s32 portInputAssignActive(void);
        const s32 q = port_sheet_row_bind[row];

        extern s32 portInputSlotDevice(s32 slot);   /* input_pc.c: -1 = nothing in that slot */

        if (q >= 0)
        {   /* the Control sheet: each player's own device (the prompt is on the player's page) */
            if (q > 0 && portInputSlotDevice(q) <= 0)
            {   /* NO INPUT: players 2-4 have only the pad in
                 * their own slot; the keyboard is player 1's, so an empty slot
                 * is no input, not "Mouse and Keyboard" */
                return "No Input\n";
            }
            return portSheetFitText(portInputDeviceLabel((q < 4) ? port_sheet_bind_dev_p[q] : 0), PORT_SHEET_DEVNAME_MAXW);
        }
        if (!portInputAssignActive() && port_sheet_player > 0 && portInputSlotDevice(port_sheet_player) <= 0)
        {
            return "No Input\n";   /* as on the Control sheet */
        }
        return portSheetFitText(portInputAssignActive() ? "Press a Button on the Device\n"
                                                        : portInputDeviceLabel(port_sheet_bind_dev),
                                PORT_SHEET_DEVNAME_MAXW);
    }
    if (port_sheet_row_kind[row] == SHROW_BINDMODE) return "Input Mode\n";
    if (port_sheet_row_kind[row] == SHROW_CTRLSTYLE) return "Control Style\n";
    if (port_sheet_row_kind[row] == SHROW_BIND) return portBindLabel(port_sheet_bind_dev, port_sheet_row_bind[row]);
    if (port_sheet_row_kind[row] == SHROW_XHAIR) return "Crosshair Colour\n";
    if (port_sheet_row_kind[row] == SHROW_RES) return "Resolution\n";
    if (port_sheet_row_kind[row] == SHROW_OPT && port_sheet_row_label[row] != NULL) return port_sheet_row_label[row];
    return portOptLabel(port_sheet_row_opt[row]);
}

/* ---- THE ON-SCREEN KEYBOARD ----------------------------------------------
 *
 * Adapted from Perfect Dark (menuitemKeyboardRender / menuitemKeyboardTick,
 * g_KeyboardKeys). The grid is ten columns wide: four rows of characters, a row
 * of four wide buttons inside the same ten columns, then the port's own "type
 * with keyboard" row.
 *
 *    0 1 2 3 4 5 6 7 8 9
 *    A B C D E F G H I J
 *    K L M N O P Q R S T
 *    U V W X Y Z _ ? ! .          _ = space
 *    [ DEL ][ CAPS ][CANCEL][ OK ]   spans 0-1, 2-4, 5-7, 8-9
 *    [      TYPE WITH KEYBOARD     ]
 *
 * Kept from PD:
 *   - the wide row uses the same ten columns, and sideways movement loops
 *     until the column is a button start, instead of a second coordinate system;
 *   - OK refuses an empty or all-spaces name, CANCEL does not;
 *   - while a real keyboard is typing, CAPS follows the real shift / caps
 *     state, so the letter shown is the letter the key would produce.
 *
 * Port-specific: it is drawn as a sheet overlay with the same paper panel and
 * CRT switch-on as the drop-down list, and while it is open the crosshair is
 * pinned and the sheet underneath takes no input.
 */
#define PORT_OSK_COLS     10
#define PORT_OSK_CHARROWS 4
#define PORT_OSK_ROWS     6      /* 4 character rows + the wide row + "type with keyboard" */
#define PORT_OSK_ROW_WIDE 4
#define PORT_OSK_ROW_TYPE 5
/* Names are capped at 21 characters (+ terminator), deliberately more than the
 * field shows (about twelve of the widest glyph), so the field scrolls and
 * marks the overflow instead. */
#define PORT_OSK_MAXLEN   22
/* What the field shows when the head of the name has scrolled off. The
 * keyboard cannot type '<' - its charset is 0-9 A-Z space ? ! . - so this can
 * never be mistaken for part of the name, which a leading "..." could be. */
#define PORT_OSK_MORE     "<"
/* The keyboard font and cell size: GE_OSKFONT=small uses the watch's small font
 * (for split-screen sized panels), and the cells shrink with it. 18 units is
 * the wide default, since 16 was too narrow for the "TYPE WITH KEYBOARD" label. */
static s32 portOskSmallFont(void)
{
#ifdef TARGET_N64
    return 0;
#else
    extern char *getenv(const char *);
    static s32 v = -1;

    if (v < 0)
    {   /* The small font is the default; the panel is the same width either
         * way, and the name field always uses the frontend font because the
         * small font has no lowercase (GE_OSKFONT=large forces the old grid for
         * comparison). */
        const char *e = getenv("GE_OSKFONT");
        v = (e == NULL || (e[0] != 'l' && e[0] != 'L'));
    }
    return v;
#endif
}
#define PORT_OSK_CHARS (portOskSmallFont() ? ptrFontBankGothicChars : ptrFontZurichBoldChars)
#define PORT_OSK_FONT  (portOskSmallFont() ? ptrFontBankGothic : ptrFontZurichBold)
/* The cell is 20 wide in both fonts, so the name field (always drawn in the
 * frontend font) has room for twelve of the widest glyph. Only the width is
 * shared; the small font keeps its shorter cells so the panel fits a
 * split-screen viewport. */
#define PORT_OSK_CELL_W 20
#define PORT_OSK_CELL_H (portOskSmallFont() ? 11 : 15)
/* the head holds the title and the name field, and the FIELD IS ALWAYS THE
 * FRONTEND FONT, so there has to be room for that font even when the grid
 * is drawn in the small one */
#define PORT_OSK_HEAD   (portOskSmallFont() ? 32 : 36)
#define PORT_OSK_PAD    8   /* panel edge to the first cell */
#define PORT_OSK_COL_CAPS     0xA00000A0  /* caps is on */
#define PORT_OSK_COL_CAPS_HOT 0xD00000E0  /* ...and pointed at */

/**
 * Return the y to draw text at so its ink sits in the middle of a box, using
 * the glyph's own metrics, since frontPrintText's y is the top of the line.
 * `sample` is a representative character (the capitals and digits used here
 * share a baseline).
 */
static s32 portOskInkY(s32 boxTop, s32 boxH, char sample)
{
    const struct fontchar *fc = &PORT_OSK_CHARS[sample - 0x21];

    return boxTop + ((boxH - fc->height) / 2) - fc->baseline;
}
#define PORT_OSK_X0       0x37   /* the sheet's own label column */
#define PORT_OSK_Y0       0x52   /* room above for the title AND the name */

static const char port_osk_keys[PORT_OSK_CHARROWS][PORT_OSK_COLS] = {
    { '0','1','2','3','4','5','6','7','8','9' },
    { 'A','B','C','D','E','F','G','H','I','J' },
    { 'K','L','M','N','O','P','Q','R','S','T' },
    { 'U','V','W','X','Y','Z',' ','?','!','.' },
};
/* where each wide button starts, and how many columns it covers */
static const s32 port_osk_wide_col[4] = { 0, 2, 5, 8 };
static const s32 port_osk_wide_span[4] = { 2, 3, 3, 2 };
/* four characters each at most, so the wide buttons stay the same shape
 * whatever font draws them (hence BACK, not CANCEL) */
static const char *const port_osk_wide_text[4] = { "DEL\n", "CAPS\n", "BACK\n", "OK\n" };

/* The analog stick drives the keyboard selector in discrete steps (the
 * crosshair stays pinned), since most players reach for the stick, not the
 * D-pad. Its deadzone is much larger than the number ramp's so a resting thumb
 * never types a wrong character; a held push repeats faster the further it is
 * pushed. */
#define PORT_OSK_STICK_DEAD   18   /* of 70: a deliberate push, not a lean */
#define PORT_OSK_STICK_FULL   70
#define PORT_OSK_REPEAT_FIRST 0.34f /* held: the pause before it starts repeating */
#define PORT_OSK_REPEAT_SLOW  0.24f /* a push just past the deadzone */
#define PORT_OSK_REPEAT_FAST  0.07f /* a push to the rim */

/**
 * The step the stick is asking for this tick: 0 none, else 1 right, 2 left,
 * 3 down, 4 up. One axis only - a diagonal picks the axis it is pushed
 * hardest on, so a sloppy push never moves the selector twice.
 */
static s32 port_osk_stick_dir;       /* 0 none, else 1 right 2 left 3 down 4 up */
static f32 port_osk_stick_wait;      /* seconds until this held push steps again */

/**
 * Which way a player's stick is pushed right now (0 = deadzone, else 1 right 2
 * left 3 down 4 up). A button step marks the push as already seen, because W A
 * S D are bound to both the C buttons and the stick, and one key press would
 * otherwise step two or three times.
 */
static s32 portNavStickDir(s32 player)
{
    const s32 rawX = joyGetStickX(player);
    const s32 rawY = joyGetStickY(player);
    const s32 magX = (rawX < 0) ? -rawX : rawX;
    const s32 magY = (rawY < 0) ? -rawY : rawY;

    if (((magX < magY) ? magY : magX) <= PORT_OSK_STICK_DEAD)
    {
        return 0;
    }
    return (magX < magY) ? ((rawY > 0) ? 4 : 3) : ((rawX > 0) ? 1 : 2);
}

static s32 portOskStickStep(void)
{
    const s32 rawX = joyGetStickX(PLAYER_1);
    const s32 rawY = joyGetStickY(PLAYER_1);
    const s32 magX = (rawX < 0) ? -rawX : rawX;
    const s32 magY = (rawY < 0) ? -rawY : rawY;
    s32 dir;
    s32 mag;
    f32 t;

    if (magX < magY)
    {
        mag = magY;
        dir = (rawY > 0) ? 4 : 3;   /* N64 stick Y is positive UP */
    }
    else
    {
        mag = magX;
        dir = (rawX > 0) ? 1 : 2;
    }
    if (mag <= PORT_OSK_STICK_DEAD)
    {   /* released: the next push steps at once rather than waiting */
        port_osk_stick_dir = 0;
        port_osk_stick_wait = 0.0f;
        return 0;
    }
    if (dir != port_osk_stick_dir)
    {   /* a NEW push, or one that changed direction: step now */
        port_osk_stick_dir = dir;
        port_osk_stick_wait = PORT_OSK_REPEAT_FIRST;
        return dir;
    }
    port_osk_stick_wait -= (f32) g_GlobalTimerDelta / 60.0f;   /* delta is in 60Hz FIELDS */
    if (port_osk_stick_wait > 0.0f)
    {
        return 0;
    }
    if (mag > PORT_OSK_STICK_FULL)
    {
        mag = PORT_OSK_STICK_FULL;
    }
    t = (f32) (mag - PORT_OSK_STICK_DEAD) / (f32) (PORT_OSK_STICK_FULL - PORT_OSK_STICK_DEAD);
    port_osk_stick_wait = PORT_OSK_REPEAT_SLOW + (PORT_OSK_REPEAT_FAST - PORT_OSK_REPEAT_SLOW) * t;
    return dir;
}

static s32 port_osk_open;
static s32 port_osk_row;
static s32 port_osk_col;
static s32 port_osk_caps = 1;
static s32 port_osk_typing;          /* a real keyboard owns it */
static f32 port_osk_cursor_h;        /* the cursor at the last tick: hover picks only on MOTION */
static f32 port_osk_cursor_v;
static s32 port_osk_done;            /* 1 = OK, -1 = cancelled, 0 = still going */
static char port_osk_text[PORT_OSK_MAXLEN];
static char port_osk_title[32];

#ifdef GE_VIDEO
extern void portTextInputStart(void);
extern void portTextInputStop(void);
extern void portTextInputPoll(char *chr, s32 *key);
extern s32 portVideoCapsState(void);   /* 1 = shift or caps is on right now */
#define PORTTEXT_BACKSPACE 1
#define PORTTEXT_ACCEPT    2
#define PORTTEXT_CANCEL    3
#else
#define portTextInputStart()
#define portTextInputStop()
#define portTextInputPoll(c, k) do { if (c) *(c) = 0; if (k) *(k) = 0; } while (0)
#define portVideoCapsState() 0
#define PORTTEXT_BACKSPACE 1
#define PORTTEXT_ACCEPT    2
#define PORTTEXT_CANCEL    3
#endif

static void portOskClose(void)
{
    if (port_osk_typing)
    {
        portTextInputStop();
        port_osk_typing = FALSE;
    }
    port_osk_open = FALSE;
}

/** Open it with `start` already in the field (NULL = empty). */
static void portOskOpen(const char *title, const char *start)
{
    portOskClose();
    port_osk_open = TRUE;
    port_osk_done = 0;
    port_osk_row = 1;
    port_osk_col = 0;
    port_osk_caps = 1;
    port_osk_stick_dir = 0;
    port_osk_stick_wait = 0.0f;
    port_osk_cursor_h = cursor_h_pos;
    port_osk_cursor_v = cursor_v_pos;
#ifndef TARGET_N64
    {   /* the click that opened it must not press a cell */
        extern s32 portMouseClickPressed(void);

        (void) portMouseClickPressed();
    }
#endif
    snprintf(port_osk_title, sizeof(port_osk_title), "%s", (title != NULL) ? title : "NAME");
    snprintf(port_osk_text, sizeof(port_osk_text), "%s", (start != NULL) ? start : "");
}

/** PD's rule: OK is refused on nothing, or on nothing but spaces. */
static s32 portOskTextUsable(void)
{
    s32 i;

    for (i = 0; port_osk_text[i] != '\0'; i++)
    {
        if (port_osk_text[i] != ' ')
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void portOskAppend(char c)
{
    const s32 n = (s32) strlen(port_osk_text);

    if (n < PORT_OSK_MAXLEN - 1)
    {
        port_osk_text[n] = c;
        port_osk_text[n + 1] = '\0';
    }
}

static void portOskBackspace(void)
{
    const s32 n = (s32) strlen(port_osk_text);

    if (n > 0)
    {
        port_osk_text[n - 1] = '\0';
    }
}

/** The wide button under a column, 0..3. */
static s32 portOskWideAt(s32 col)
{
    s32 i;

    for (i = 3; i >= 0; i--)
    {
        if (col >= port_osk_wide_col[i])
        {
            return i;
        }
    }
    return 0;
}

/** Press whatever the cursor is on. */
static void portOskPress(void)
{
    if (port_osk_row < PORT_OSK_CHARROWS)
    {
        char c = port_osk_keys[port_osk_row][port_osk_col];

        /* The small font has only one case, so the name field is drawn in the frontend font
         * and CAPS gets a lit background, keeping the case always visible. */
        if (!port_osk_caps && c >= 'A' && c <= 'Z')
        {
            c = (char) (c - 'A' + 'a');
        }
        portOskAppend(c);
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        return;
    }
    if (port_osk_row == PORT_OSK_ROW_TYPE)
    {
        port_osk_typing = !port_osk_typing;
        if (port_osk_typing)
        {
            portTextInputStart();
        }
        else
        {
            portTextInputStop();
        }
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        return;
    }
    switch (portOskWideAt(port_osk_col))
    {
    case 0:
        portOskBackspace();
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        break;
    case 1:
        port_osk_caps = !port_osk_caps;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        break;
    case 2:
        port_osk_done = -1;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
        break;
    default:
        if (portOskTextUsable())
        {
            port_osk_done = 1;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        break;
    }
}

/** Screen box of a cell. */
static void portOskCellBox(s32 row, s32 col, s32 *x0, s32 *y0, s32 *w)
{
    s32 span = 1;

    if (row == PORT_OSK_ROW_WIDE)
    {
        const s32 b = portOskWideAt(col);
        col = port_osk_wide_col[b];
        span = port_osk_wide_span[b];
    }
    else if (row == PORT_OSK_ROW_TYPE)
    {
        col = 0;
        span = PORT_OSK_COLS;
    }
    *x0 = PORT_OSK_X0 + col * PORT_OSK_CELL_W;
    *y0 = PORT_OSK_Y0 + row * PORT_OSK_CELL_H;
    *w = span * PORT_OSK_CELL_W;
}

/** One frame. Returns 1 when it has closed; the caller then reads the text. */
static s32 portOskTick(void)
{
#ifdef GE_VIDEO
    /* the mouse's own buttons are the click path below; taken as Z as well
     * (Mouse Left's default bind) they pressed the cell a second time */
    extern u16 portMouseBindMask(void);
    const s32 press = joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON) & ~portMouseBindMask();
#else
    const s32 press = joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON);
#endif
    s32 moved = 0;

    if (!port_osk_open)
    {
        return 0;
    }

    if (port_osk_typing)
    {   /* a real keyboard owns the text; the grid still shows where it is */
        char c = 0;
        s32 key = 0;

        port_osk_caps = portVideoCapsState();
        portTextInputPoll(&c, &key);
        /* not '|' (the layout file's separator, the save refuses it),
         * '<' (PORT_OSK_MORE's mark) or a leading '#' (a comment line to the
         * loader) - the grid cannot type them either */
        if (c != 0 && c != '|' && c != '<' && !(c == '#' && port_osk_text[0] == '\0'))
        {
            portOskAppend(c);
        }
        if (key == PORTTEXT_BACKSPACE)
        {
            portOskBackspace();
        }
        else if (key == PORTTEXT_ACCEPT && portOskTextUsable())
        {
            port_osk_done = 1;
        }
        else if (key == PORTTEXT_CANCEL)
        {
            port_osk_done = -1;
        }
    }

    {   /* D-pad and C buttons step once per press; the stick repeats while
         * held. Buttons win a tie so a resting stick cannot swallow a press. */
        s32 step = 0;

        if (joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD | R_CBUTTONS))
        {
            step = 1;
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD | L_CBUTTONS))
        {
            step = 2;
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD | D_CBUTTONS))
        {
            step = 3;
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD | U_CBUTTONS))
        {
            step = 4;
        }
        if (step != 0)
        {   /* the stick's push, if there is one, is ALREADY SEEN - it is very
             * likely this same key (portNavStickDir). This used to disarm the
             * stick instead (dir = 0), which made a held W a new push on the
             * next tick: two steps a press, three if held. */
            port_osk_stick_dir = portNavStickDir(PLAYER_1);
            port_osk_stick_wait = PORT_OSK_REPEAT_FIRST;
        }
        else
        {
            step = portOskStickStep();
        }
        switch (step)
        {
        case 1:
            if (port_osk_row == PORT_OSK_ROW_WIDE)
            {   /* the NEXT button's start: col + 1 is still inside this
                 * button, and the snap below took it straight back */
                const s32 b = portOskWideAt(port_osk_col);

                port_osk_col = port_osk_wide_col[b] + port_osk_wide_span[b];
            }
            else
            {
                port_osk_col++;
            }
            moved = 1;
            break;
        case 2: port_osk_col--; moved = 1; break;
        case 3: port_osk_row = (port_osk_row + 1) % PORT_OSK_ROWS; moved = 1; break;
        case 4: port_osk_row = (port_osk_row + PORT_OSK_ROWS - 1) % PORT_OSK_ROWS; moved = 1; break;
        default: break;
        }
    }
    if (port_osk_col < 0) port_osk_col = PORT_OSK_COLS - 1;
    if (port_osk_col >= PORT_OSK_COLS) port_osk_col = 0;
    if (port_osk_row == PORT_OSK_ROW_WIDE)
    {   /* PD's rule: land on a button START, whichever way we came */
        port_osk_col = port_osk_wide_col[portOskWideAt(port_osk_col)];
    }
    else if (port_osk_row == PORT_OSK_ROW_TYPE)
    {
        port_osk_col = 0;
    }
    if (moved)
    {
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
    }

#ifndef TARGET_N64
    {   /* The mouse points and clicks on the keyboard, hit-tested against the
         * mouse's own position (portMouseMenuPos), since the cursor is pinned.
         * Hover only follows mouse motion so a resting pointer never overrides
         * the pad, while clicks are read every tick and always press the cell
         * under the pointer. */
        extern s32 portMouseMenuPos(f32 *x, f32 *y, f32 l, f32 t, f32 w, f32 h);
        extern s32 portMouseClickPressed(void);
        const s32 clicked = portMouseClickPressed();
        s32 stirred = 0;
        f32 mgx = 0.0f;
        f32 mgy = 0.0f;
        s32 r, c, x0, y0, w;

        extern s32 portConIsOpen(void);

        if (!portConIsOpen()   /* console open = neutral pointer */
            && portMouseMenuPos(&mgx, &mgy, getPlayer_c_screenleft(), getPlayer_c_screentop(),
                             getPlayer_c_screenwidth(), getPlayer_c_screenheight()))
        {
            stirred = (mgx != port_osk_cursor_h) || (mgy != port_osk_cursor_v);
            port_osk_cursor_h = mgx;
            port_osk_cursor_v = mgy;
        }
        mgx = port_osk_cursor_h;
        mgy = port_osk_cursor_v;
        for (r = 0; (stirred || clicked) && r < PORT_OSK_ROWS; r++)
        {
            for (c = 0; c < PORT_OSK_COLS; c++)
            {
                portOskCellBox(r, c, &x0, &y0, &w);
                if (mgx >= (f32) x0 && mgx < (f32) (x0 + w)
                    && mgy >= (f32) y0 && mgy < (f32) (y0 + PORT_OSK_CELL_H))
                {
                    port_osk_row = r;
                    port_osk_col = (r == PORT_OSK_ROW_WIDE) ? port_osk_wide_col[portOskWideAt(c)]
                                 : (r == PORT_OSK_ROW_TYPE) ? 0 : c;
                    if (clicked)
                    {
                        portOskPress();
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
                    }
                    /* ONE hit: a wide button's box is the same for every
                     * column it spans, so DEL deleted two and CAPS flipped
                     * back */
                    r = PORT_OSK_ROWS;
                    break;
                }
            }
        }
    }
#endif

    if (press)
    {
        portOskPress();
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        port_osk_done = -1;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
    }

    if (port_osk_done != 0)
    {
        portOskClose();
        return 1;
    }
    return 0;
}

static Gfx *portOskDraw(Gfx *DL)
{
    s32 x0, y0, w;
    s32 r, c;
    s32 x, y, th, tw;
    char label[8];

    /* the panel: the sheet's own paper with a dark edge, like the drop-down */
    DL = microcode_constructor_related_to_menus(DL, PORT_OSK_X0 - PORT_OSK_PAD, PORT_OSK_Y0 - PORT_OSK_HEAD,
            PORT_OSK_X0 + PORT_OSK_COLS * PORT_OSK_CELL_W + PORT_OSK_PAD,
            PORT_OSK_Y0 + PORT_OSK_ROWS * PORT_OSK_CELL_H + 3, PORT_SHEET_COL_LIST_EDGE);
    DL = microcode_constructor_related_to_menus(DL, PORT_OSK_X0 - PORT_OSK_PAD + 1, PORT_OSK_Y0 - PORT_OSK_HEAD + 1,
            PORT_OSK_X0 + PORT_OSK_COLS * PORT_OSK_CELL_W + PORT_OSK_PAD - 1,
            PORT_OSK_Y0 + PORT_OSK_ROWS * PORT_OSK_CELL_H + 2, PORT_SHEET_COL_LIST_BG);
    DL = microcode_constructor(DL);

    /* title on one line, the name being typed on its own below it - they
     * collided when the name sat beside a title of any length */
    x = PORT_OSK_X0;
    y = PORT_OSK_Y0 - PORT_OSK_HEAD + 3;
    DL = frontPrintText(DL, &x, &y, port_osk_title, PORT_OSK_CHARS, PORT_OSK_FONT, 0xFF, viGetX(), viGetY(), 0, 0);
    {
        char field[PORT_OSK_MAXLEN + 8];   /* the name, the caret, the marker, the newline */
        const s32 fy = PORT_OSK_Y0 - (PORT_OSK_HEAD / 2) + 1;

        /* Always the frontend font, so the name reads back with its real
         * capitalisation; when it is too long the field shows the tail (keeping
         * the caret visible) with a marker on the left, whose width is reserved
         * before trimming so it never pushes the caret out. */
        {
            const s32 room = PORT_OSK_COLS * PORT_OSK_CELL_W - 8;
            char raw[PORT_OSK_MAXLEN + 3];
            char *shown = raw;
            s32 fh, fw;

            sprintf(raw, "%s_\n", port_osk_text);   /* _ is the caret */
            textMeasure(&fh, &fw, shown, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
            if (fw <= room)
            {
                sprintf(field, "%s", raw);
            }
            else
            {
                s32 mh, mw;

                textMeasure(&mh, &mw, PORT_OSK_MORE "\n", ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                while ((mw + fw) > room && shown[0] != '\0' && shown[1] != '\0')
                {
                    shown++;
                    textMeasure(&fh, &fw, shown, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                }
                sprintf(field, "%s%s", PORT_OSK_MORE, shown);
            }
        }
        DL = microcode_constructor_related_to_menus(DL, PORT_OSK_X0, fy - 2,
                PORT_OSK_X0 + PORT_OSK_COLS * PORT_OSK_CELL_W - 2, fy + 14, 0x00000040);
        x = PORT_OSK_X0 + 3;
        y = fy - 2 + (((16) - ptrFontZurichBoldChars['A' - 0x21].height) / 2)
          - ptrFontZurichBoldChars['A' - 0x21].baseline;   /* ink centred, as the cells are */
        DL = frontPrintText(DL, &x, &y, field, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
    }

    for (r = 0; r < PORT_OSK_ROWS; r++)
    {
        for (c = 0; c < PORT_OSK_COLS; c++)
        {
            const s32 hot = (r == port_osk_row)
                && ((r == PORT_OSK_ROW_WIDE) ? (portOskWideAt(c) == portOskWideAt(port_osk_col))
                  : (r == PORT_OSK_ROW_TYPE) ? 1 : (c == port_osk_col));
            char *text = label;

            if (r == PORT_OSK_ROW_WIDE)
            {
                if (c != port_osk_wide_col[portOskWideAt(c)])
                {
                    continue;   /* a wide button is drawn once, at its start */
                }
                text = (char *) port_osk_wide_text[portOskWideAt(c)];
            }
            else if (r == PORT_OSK_ROW_TYPE)
            {
                if (c != 0)
                {
                    continue;
                }
                text = port_osk_typing ? "TYPING - PRESS TO STOP\n" : "TYPE WITH KEYBOARD\n";
            }
            else
            {
                label[0] = port_osk_keys[r][c];
                if (!port_osk_caps && label[0] >= 'A' && label[0] <= 'Z')
                {
                    label[0] = (char) (label[0] - 'A' + 'a');
                }
                label[1] = '\n';
                label[2] = '\0';
            }

            {   /* CAPS (lit red) and TYPE WITH KEYBOARD stay lit while on,
                 * since in the small font the letters can't show the case; a
                 * brighter shade marks the cell also under the cursor. */
                const s32 caps_lit = (r == PORT_OSK_ROW_WIDE && portOskWideAt(c) == 1 && port_osk_caps);
                const s32 type_lit = (r == PORT_OSK_ROW_TYPE && port_osk_typing);
                /* the flag is separate from the colour ON PURPOSE: a colour with the
                 * red byte set does not fit in an s32 as a positive number, so a
                 * "band < 0 means none" sentinel silently swallowed every red one */
                u32 band = 0;
                s32 banded = TRUE;

                if (caps_lit)
                {
                    band = hot ? PORT_OSK_COL_CAPS_HOT : PORT_OSK_COL_CAPS;
                }
                else if (hot)
                {
                    band = PORT_SHEET_COL_LIST_HOT;
                }
                else if (type_lit)
                {
                    band = 0x00000070;
                }
                else
                {
                    banded = FALSE;
                }

                portOskCellBox(r, c, &x0, &y0, &w);
                if (banded)
                {
                    DL = microcode_constructor_related_to_menus(DL, x0, y0 - 1, x0 + w - 2,
                            y0 + PORT_OSK_CELL_H - 2, band);
                }
                textMeasure(&th, &tw, text, PORT_OSK_CHARS, PORT_OSK_FONT, 0);
                x = x0 + ((w - 2 - tw) / 2);
                y = portOskInkY(y0 - 1, PORT_OSK_CELL_H - 1, 'A');
                DL = frontPrintText(DL, &x, &y, text, PORT_OSK_CHARS, PORT_OSK_FONT,
                        banded ? -1 : 0xFF, viGetX(), viGetY(), 0, 0);
            }
        }
    }
    DL = microcode_constructor(DL);
    return DL;
}

/* ---- the list ---- */

/* The watch's scroll rule (portWatchDropScrollFollow, PORTWATCH_DROP_MARGIN):
 * keep two entries visible on either side of the highlight, so it stops on the
 * third row from an edge and the list scrolls under it (pad and keys only; the
 * mouse hovers and the wheel scrolls directly). */
#define PORT_SHEET_LIST_MARGIN 2

static void portSheetListScrollFollow(void)
{
    const s32 count = portSheetListCount(port_sheet_list_row);
    /* a window too short to hold a margin both sides and a row between them
     * (it never is today - a list only scrolls past thirteen) gets a smaller one */
    const s32 margin = (port_sheet_list_vis > PORT_SHEET_LIST_MARGIN * 2) ? PORT_SHEET_LIST_MARGIN
                                                                        : (port_sheet_list_vis - 1) / 2;

    if (port_sheet_list_idx - margin < port_sheet_list_scroll)
    {
        port_sheet_list_scroll = port_sheet_list_idx - margin;
    }
    else if (port_sheet_list_idx + margin >= port_sheet_list_scroll + port_sheet_list_vis)
    {
        port_sheet_list_scroll = port_sheet_list_idx + margin - port_sheet_list_vis + 1;
    }
    if (port_sheet_list_scroll > count - port_sheet_list_vis) port_sheet_list_scroll = count - port_sheet_list_vis;
    if (port_sheet_list_scroll < 0) port_sheet_list_scroll = 0;
}

/**
 * Open the row's list. The window is as tall as the sheet allows (every
 * slot line, thirteen), and it is parked so the entry in use lands on the
 * row's own line where it can - the watch's anchor rule - scrolling the
 * contents rather than moving the window.
 */
static void portSheetListOpen(s32 row, s32 slot)
{
    const s32 count = portSheetListCount(row);
    const s32 rowy = portSheetSlotY(slot);
    const s32 ymax = PORT_SHEET_LAST_Y; /* the sheet's last line, either layout */
    s32 live;
    s32 top;
    s32 at;

    if (count <= 0)
    {
        return;
    }
    portSheetRampEnd();
    live = portSheetPending(row);
    if (live < 0) live = portSheetListLive(row);
    if (live < 0 && port_sheet_row_kind[row] == SHROW_OPT)
    {
        live = portOptValueIndexNear(port_sheet_row_opt[row], 0);   /* 165 opens on 180, never on Hardware */
    }
    if (live < 0) live = 0;
    port_sheet_list_row = row;
    port_sheet_list_slot = slot;
    port_sheet_list_vis = (count < PORT_SHEET_LIST_LINES) ? count : PORT_SHEET_LIST_LINES;
    port_sheet_list_idx = live;
    top = rowy - live * PORT_SHEET_ROW_PITCH;
    if (top > ymax - (port_sheet_list_vis - 1) * PORT_SHEET_ROW_PITCH)
    {
        top = ymax - (port_sheet_list_vis - 1) * PORT_SHEET_ROW_PITCH;
    }
    if (top < PORT_SHEET_ROW_Y0)
    {
        top = PORT_SHEET_ROW_Y0;
    }
    at = (rowy - top) / PORT_SHEET_ROW_PITCH;
    if (at < 0) at = 0;
    if (at > port_sheet_list_vis - 1) at = port_sheet_list_vis - 1;
    port_sheet_list_scroll = live - at;
    port_sheet_list_top = top;
    portSheetListScrollFollow();
    port_sheet_list_anim = 0.0f;
    port_sheet_list_closing = FALSE;
}

static void portSheetListClose(void)
{
    if (port_sheet_list_row >= 0)
    {
        port_sheet_list_closing = TRUE;
    }
}

static void portSheetListCloseNow(void)
{
    port_sheet_list_row = -1;
    port_sheet_list_closing = FALSE;
    port_sheet_list_anim = 0.0f;
}

/** The CRT switch-on, advanced once per 60 Hz field banked here from
  * portTickDelta; closing runs it back down and then the list is gone.
 * Not portWatchMenuField(): only MoveBond advances that, so in the frontend it
 * is often stale (0), and the list would never draw yet still own the sheet's
 * input. */
static void portSheetListAnimTick(void)
{
    static f32 field;   /* fields banked, the bondview2.c accumulator's rule */

    if (port_sheet_list_row < 0)
    {
        return;
    }
    field += portTickDelta;
    if (field > 4.0f) field = 4.0f;
    /* EVERY whole field banked: at Max FPS 30 a tick carries two, and
     * spending one per tick ran the animation at half speed */
    while (field >= 1.0f)
    {
        field -= 1.0f;
        if (!port_sheet_list_closing)
        {
            port_sheet_list_anim += (port_sheet_list_anim < 1.0f) ? PORT_SHEET_ANIM_W : PORT_SHEET_ANIM_H;
            if (port_sheet_list_anim > 2.0f) port_sheet_list_anim = 2.0f;
        }
        else
        {
            port_sheet_list_anim -= (port_sheet_list_anim > 1.0f) ? PORT_SHEET_ANIM_H : PORT_SHEET_ANIM_W;
            if (port_sheet_list_anim <= 0.0f)
            {
                portSheetListCloseNow();
                field = 0.0f;
                return;
            }
        }
    }
}

/** The open list's box: x0/y0 of its first entry, its width, entries shown. */
static void portSheetListBox(s32 *x0, s32 *y0, s32 *w, s32 *vis)
{
    const s32 count = portSheetListCount(port_sheet_list_row);
    s32 wid = 0;
    s32 i;

    for (i = 0; i < count; i++)
    {
        s32 th = 0, tw = 0;
        textMeasure(&th, &tw, (char *) portSheetListTextAt(port_sheet_list_row, i), ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        if (tw > wid) wid = tw;
    }
    wid += PORT_SHEET_LIST_PAD * 2;
    if (count > port_sheet_list_vis)
    {
        wid += 8; /* a gutter for the more-above / more-below marks */
    }
    *x0 = PORT_SHEET_VALUE_X - PORT_SHEET_LIST_PAD;
    *y0 = port_sheet_list_top;
    *w = wid;
    *vis = port_sheet_list_vis;
}

/** The heading. The two CONTROL sub-pages say WHOSE controls they are - one
 * "PLAYER" on four identical-looking pages would be a good way to rebind the
 * wrong pad. */
static const char *portSheetTitle(void)
{
    static char text[24];

    if (port_sheet_kind == PORT_SHEET_PLAYER)
    {
        sprintf(text, "PLAYER %d\n", (s32) (port_sheet_player + 1));
        return text;
    }
    if (port_sheet_kind == PORT_SHEET_BINDS)
    {
        sprintf(text, "P%d REBIND\n", (s32) (port_sheet_player + 1));
        return text;
    }
    return port_sheet_title[port_sheet_kind];
}

/** Back to the Options folder, pending picks thrown away, the pad cursor on the row (or the tab) that opened this sheet.
 * tohub: PREVIOUS / B off a page that a hub opened goes back to that hub; the OPTIONS tab (FALSE) goes to the folder's rows. */
static void portSheetLeave(s32 tohub)
{
    s32 root;

    portOskClose();
    portSheetEditEnd(TRUE);
    portSheetPendingClear();
    portSheetListCloseNow();
    /* the OPTIONS tab leaves from any depth: the folder row is the chain's
     * ROOT, not this sub-sheet */
    root = (port_sheet_depth > 0) ? port_sheet_parent[0] : port_sheet_kind;
    if (root == PORT_SHEET_MPOPTS)
    {   /* opened from the MP setup screen, not the folder: back there, onto
         * the row that opened it (its band starts at 0x105) */
        frontChangeMenu(MENU_MP_OPTIONS, FALSE);
        if (frontCursorSnapAllowed())
        {
            cursor_h_pos = 126.0f;
            cursor_v_pos = (f32) (0x105 + 6);
        }
        return;
    }
    if (port_options_hub >= 0 && tohub)
    {
        port_options_keep_hub = TRUE;   /* init_menu1B keeps the hub */
    }
    else if (port_options_hub >= 0)
    {
        root = port_options_hub;        /* the folder row is the hub's own */
        port_options_hub = -1;
    }
    frontChangeMenu(MENU_PORT_OPTIONS, FALSE);
    {   /* back onto the folder row that opened this sheet. Not
         * setCursorPOSforMode: that is the Files menu's 0x20 pitch. */
        s32 r;

        for (r = 0; r < portOptionsRowCount() && portOptionsRowKind(r) != root; r++)
        {
        }
        if (r < portOptionsRowCount())
        {
            if (frontCursorSnapAllowed())
            {
                cursor_h_pos = 126.0f;
                cursor_v_pos = (f32) (PORT_OPTIONS_ROW_Y0 + r * PORT_OPTIONS_ROW_PITCH + 6);
            }
            return;
        }
    }
    if (frontCursorSnapAllowed())
    {
        cursor_h_pos = NEXTTAB_CURSOR_HPOS;
        cursor_v_pos = STARTTAB_TEXT_HALF;
    }
}

/* B went down on a bind row: the page backs out on release, so a hold can
 * unbind instead. Cleared whenever a capture, dialog or keyboard took the
 * tick in between, and when the page opens, so a stale wait never backs
 * out later. */
static s32 port_sheet_bwait;
static s32 port_sheet_bunbound;     /* ...and the hold unbound, so no back */
static s32 port_sheet_bwait_live;   /* the previous tick reached the B check */

void init_menu1C_portsheet(void)
{
    port_sheet_bwait = FALSE;
    port_sheet_bwait_live = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    port_sheet_highlighted = -1;
    portSheetPendingClear();
    portOskClose();
    portSheetEditEnd(TRUE);
    portSheetListCloseNow();
    {   /* a Delete pressed before the sheet showed is not for its rows */
        extern s32 portVideoDeleteEdge(void);
        (void) portVideoDeleteEdge();
    }
    /* nor are wheel notches banked in a match or on the MP setup screen: the
     * first tick stepped (and saved) whatever row the cursor sat on */
    (void) portMouseWheelSteps();
    port_sheet_depth = 0;   /* opened from the folder: nothing above this one */
    portSheetSeedDevices();
    portSheetBuild(port_sheet_kind);
    {   /* GE_OSK=1: open the on-screen keyboard as the CONTROL sheet opens, so a
         * capture run can photograph it without steering to the row first.
         * Never set in midas.ini. */
        extern char *getenv(const char *);
        const char *e = getenv("GE_OSK");

        if (e != NULL && e[0] != '0' && port_sheet_kind == PORT_SHEET_CONTROL)
        {   /* GE_OSKTEXT seeds the field, so a capture can photograph a full
             * name without driving twelve key presses to get one */
            const char *seed = getenv("GE_OSKTEXT");

            portOskOpen("SAVE LAYOUT AS", (seed != NULL) ? seed : "");
            if (e[0] == 'l')
            {
                port_osk_caps = FALSE;   /* GE_OSK=lower: open with caps off */
            }
        }
    }
    load_walletbond();
}

/* Seed each player's device by slot (player 1 the keyboard, the rest their own
 * pad if plugged in), not by device number, which only lines up when every
 * lower slot is busy. Player 1 shows its pad only once the player picks it
 * (p1_view), and this is re-run after an assign since a pad can move between
 * slots. */
static void portSheetSeedDevices(void)
{
    extern s32 portInputDeviceSlot(s32 dev);
    extern s32 portInputP1ViewPad(void);
    const s32 ndev = portInputDeviceCount();
    s32 q, d;

    for (q = 0; q < 4; q++)
    {
        port_sheet_bind_dev_p[q] = 0;
        for (d = 1; (q > 0 || portInputP1ViewPad()) && d < ndev; d++)
        {
            if (portInputDeviceSlot(d) == q)
            {
                port_sheet_bind_dev_p[q] = d;
                break;
            }
        }
    }
}

void update_menu1C_portsheet(void)
{
    return;
}

/* A pad's D-pad navigates the sheet: on Pro / Standard layouts it arrives as
 * digital Move and no longer moves the crosshair. Once a tick, returns -1 / 0 /
 * +1 per axis (y up): a step on the press, then repeats while held (0.4 s, then
 * every 0.1 s, in 60 Hz fields). */
static void portSheetDpadStep(s32 *sx, s32 *sy)
{
    extern s32 portInputPadDigMoveP1(s32 *dx, s32 *dy) __attribute__((weak)); /* input_pc.c */
    static s32 prev[2];
    static f32 held[2];
    s32 d[2] = { 0, 0 };
    s32 out[2] = { 0, 0 };
    s32 a;

    if (portInputPadDigMoveP1 != NULL)
    {
        portInputPadDigMoveP1(&d[0], &d[1]);
    }
    for (a = 0; a < 2; a++)
    {
        if (d[a] != 0 && d[a] != prev[a])
        {
            out[a] = d[a];
            held[a] = 0.0f;
        }
        else if (d[a] != 0)
        {
            held[a] += g_GlobalTimerDelta;
            if (held[a] >= 24.0f)
            {
                out[a] = d[a];
                held[a] -= 6.0f;
            }
        }
        prev[a] = d[a];
    }
    *sx = out[0];
    *sy = out[1];
}

/* ---- SETTING HELP: the box along the sheet's foot ---------------------- */
extern const char *portOptHelp(const char *key);   /* port_option_help.c */
static f32 port_sheet_help_anim;   /* the list's CRT switch-on, 0..2, restarted when the box appears */

/** The key port_option_help.c files a row under; row -1 is APPLY CHANGES. */
static const char *portSheetHelpKey(s32 row)
{
    if (row == -1)
    {
        return "@apply";
    }
    switch (port_sheet_row_kind[row])
    {
        case SHROW_OPT:          return portOptKey(port_sheet_row_opt[row]);
        case SHROW_RES:          return "@resolution";
        case SHROW_XHAIR:        return "crosshair_color";
        case SHROW_BINDDEV:      return "@input_device";
        case SHROW_GOBINDS:      return "@rebind";
        case SHROW_PLAYERPICK:   return "@player";
        case SHROW_BINDMODE:     return "@bind_mode";
        case SHROW_CTRLSTYLE:    return "@control_style";
        case SHROW_LAYOUT:       return "@bind_config";
        case SHROW_MANAGELAYOUT: return "@manage_config";
        case SHROW_RESETBINDS:   return "@reset_binds";
        case SHROW_BIND:         return "@bind";
        case SHROW_GOSHEET:
            switch (port_sheet_row_bind[row])
            {
                case PORT_SHEET_MOUSEKB:    return "@page_mousekb";
                case PORT_SHEET_PAD:        return "@page_pad";
                case PORT_SHEET_CTLGENERAL: return "@page_ctlgeneral";
                case PORT_SHEET_GENERAL:    return "@page_general";
                case PORT_SHEET_HUD:        return "@page_hud";
                case PORT_SHEET_PORTOPTS:   return "@page_portopts";
                case PORT_SHEET_VIDEO:      return "@page_video";
                case PORT_SHEET_AUDIO:      return "@page_audio";
            }
            break;
    }
    return NULL;
}

/** The highlighted row's help line, NULL = no box: the row the open list or
 * edit belongs to, else the one under the cursor (never a tab). */
static const char *portSheetHelpText(void)
{
    s32 row;

    if (!portSheetHelpOn() || port_sheet_kind == PORT_SHEET_CREDITS || port_osk_open || portDlgIsOpen()
        || portBindCaptureActive())
    {
        return NULL;
    }
    if (port_sheet_list_row >= 0)
    {
        row = port_sheet_list_row;
    }
    else if (port_sheet_edit_row >= 0)
    {
        row = port_sheet_edit_row;
    }
    else if (port_sheet_highlighted >= 0 && !tab_prev_highlight && !tab_next_highlight && !tab_start_highlight)
    {
        row = portSheetSlotRow(port_sheet_highlighted);
    }
    else
    {
        return NULL;
    }
    if (row == -2)
    {
        return NULL;
    }
    {   /* GE_HELPTEXT=<text>: capture harness, never midas.ini - a long line to show the small font */
        extern char *getenv(const char *);
        const char *e = getenv("GE_HELPTEXT");

        if (e != NULL && e[0] != '\0')
        {
            return e;
        }
    }
    return portOptHelp(portSheetHelpKey(row));
}

/** The box's switch-on, in 60 Hz fields like the list's; it restarts each time the box appears. */
static void portSheetHelpTick(void)
{
    static f32 field;

    if (portSheetHelpText() == NULL)
    {
        port_sheet_help_anim = 0.0f;
        field = 0.0f;
        return;
    }
    field += portTickDelta;
    if (field > 4.0f) field = 4.0f;
    while (field >= 1.0f)
    {
        field -= 1.0f;
        port_sheet_help_anim += (port_sheet_help_anim < 1.0f) ? PORT_SHEET_ANIM_W : PORT_SHEET_ANIM_H;
        if (port_sheet_help_anim > 2.0f) port_sheet_help_anim = 2.0f;
    }
}

/**
 * The drop-down list's panel: a dark edge round opaque paper, and its CRT
 * switch-on (anim 0..1 a line grows from the centre, 1..2 it opens).
 * Fill rects are lower-right exclusive.
 */
static Gfx *portSheetDrawPanel(Gfx *DL, s32 bx0, s32 by0, s32 bx1, s32 by1, f32 anim)
{
    const f32 aw = (anim < 1.0f) ? anim : 1.0f;
    const f32 ah = (anim > 1.0f) ? anim - 1.0f : 0.0f;
    const s32 cx = (bx0 + bx1) / 2;
    const s32 cy = (by0 + by1) / 2;

    {   /* the animated size as a WIDTH placed about the centre, not two
         * halves: at full size it is exactly the box above, odd or even */
        const s32 curw = (s32) ((f32) (bx1 - bx0) * aw);
        s32 curh = (s32) ((f32) (by1 - by0) * ah);

        if (curh < 2) curh = 2;
        bx0 = cx - (bx1 - bx0) / 2 + ((bx1 - bx0) - curw) / 2;
        bx1 = bx0 + curw;
        by0 = cy - (by1 - by0) / 2 + ((by1 - by0) - curh) / 2;
        by1 = by0 + curh;
    }
    if (bx1 - bx0 < 2)
    {
        return DL;
    }
    DL = microcode_constructor_related_to_menus(DL, bx0, by0, bx1, by1, PORT_SHEET_COL_LIST_EDGE);
    if (bx1 - bx0 > 4 && by1 - by0 > 4)
    {
        DL = microcode_constructor_related_to_menus(DL, bx0 + 1, by0 + 1, bx1 - 1, by1 - 1, PORT_SHEET_COL_LIST_BG);
    }
    DL = microcode_constructor(DL);
    return DL;
}

#define PORT_SHEET_HELP_LINE 80
/** Word-wrap `text` to the box in one font; returns the lines it needs (more
 * than `maxlines` = it does not fit, and only the first `maxlines` are kept). */
static s32 portSheetHelpWrap(const char *text, s32 small, s32 maxlines, char out[][PORT_SHEET_HELP_LINE])
{
    const s32 maxw = PORT_SHEET_HELP_X1 - 1 - PORT_SHEET_LIST_PAD - PORT_SHEET_LABEL_X;
    struct fontchar *chars = small ? ptrFontBankGothicChars : ptrFontZurichBoldChars;
    struct font *font = small ? ptrFontBankGothic : ptrFontZurichBold;
    char cur[PORT_SHEET_HELP_LINE];
    char cand[PORT_SHEET_HELP_LINE + 2];
    s32 n = 0;

    cur[0] = '\0';
    while (*text != '\0')
    {
        s32 len = 0;
        s32 h = 0, w = 0;

        while (*text == ' ') text++;
        while (text[len] != '\0' && text[len] != ' ') len++;
        if (len == 0)
        {
            break;
        }
        snprintf(cand, sizeof(cand) - 1, "%s%s%.*s", cur, (cur[0] != '\0') ? " " : "", (int) len, text);
        strcat(cand, "\n");
        textMeasure(&h, &w, cand, chars, font, 0);
        if (w <= maxw || cur[0] == '\0')
        {
            cand[strlen(cand) - 1] = '\0';
            snprintf(cur, sizeof(cur), "%s", cand);
        }
        else
        {
            if (n < maxlines)
            {
                snprintf(out[n], PORT_SHEET_HELP_LINE - 1, "%s", cur);
            }
            n++;
            snprintf(cur, sizeof(cur), "%.*s", (int) len, text);
        }
        text += len;
    }
    if (cur[0] != '\0')
    {
        if (n < maxlines)
        {
            snprintf(out[n], PORT_SHEET_HELP_LINE - 1, "%s", cur);
        }
        n++;
    }
    return n;
}

/** The help box: the highlighted row's line, in the list's panel, on every folder sheet. */
static Gfx *portSheetDrawHelp(Gfx *DL)
{
    char lines[PORT_SHEET_HELP_SMALL_LINES][PORT_SHEET_HELP_LINE];
    const char *text = portSheetHelpText();
    s32 small = FALSE;
    s32 n, pitch, i, y;

    if (text == NULL)
    {
        return DL;
    }
    DL = portSheetDrawPanel(DL, PORT_SHEET_HELP_X0, PORT_SHEET_HELP_Y0, PORT_SHEET_HELP_X1, PORT_SHEET_HELP_Y1,
                            port_sheet_help_anim);
    if (port_sheet_help_anim < 2.0f)
    {
        return DL;   /* still switching on: the shell only */
    }
    n = portSheetHelpWrap(text, FALSE, PORT_SHEET_HELP_LINES, lines);
    pitch = PORT_SHEET_HELP_PITCH;
    if (n > PORT_SHEET_HELP_LINES)
    {   /* too long for the sheet's font: the watch's small one */
        small = TRUE;
        n = portSheetHelpWrap(text, TRUE, PORT_SHEET_HELP_SMALL_LINES, lines);
        pitch = PORT_SHEET_HELP_SMALL_PITCH;
        if (n > PORT_SHEET_HELP_SMALL_LINES)
        {   /* still too long: cut the last line */
            char *last = lines[PORT_SHEET_HELP_SMALL_LINES - 1];
            s32 len = (s32) strlen(last);

            n = PORT_SHEET_HELP_SMALL_LINES;
            for (; len > 0; len--)
            {
                char cut[PORT_SHEET_HELP_LINE + 8];
                s32 h = 0, w = 0;

                snprintf(cut, sizeof(cut), "%.*s...\n", (int) len, last);
                textMeasure(&h, &w, cut, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                if (w <= PORT_SHEET_HELP_X1 - 1 - PORT_SHEET_LIST_PAD - PORT_SHEET_LABEL_X)
                {
                    snprintf(last, PORT_SHEET_HELP_LINE - 1, "%.*s...", (int) len, cut);
                    break;
                }
            }
        }
    }
    y = (PORT_SHEET_HELP_Y0 + PORT_SHEET_HELP_Y1) / 2 - (n * pitch) / 2;
    for (i = 0; i < n; i++)
    {
        s32 x = PORT_SHEET_LABEL_X;
        s32 ly = y + i * pitch;

        strcat(lines[i], "\n");
        DL = frontPrintText(DL, &x, &ly, lines[i], small ? ptrFontBankGothicChars : ptrFontZurichBoldChars,
                            small ? ptrFontBankGothic : ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }
    return DL;
}

void interface_menu1C_portsheet(void)
{
    const s32 bwaitlive = port_sheet_bwait_live;   /* before any early return */
    extern s32 portVideoDeleteEdge(void);
    s32 slot;
    s32 row;
    s32 press;
    s32 wheel;
    s32 del;
    s32 page_back = FALSE;
    s32 page_on = FALSE;
    s32 leave = FALSE;
    s32 dpx;
    s32 dpy;
    static f32 caphold;   /* player 1's hold while another pad's capture is armed */

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);

    portSheetListAnimTick();
    portSheetHelpTick();
    press = joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON);
    wheel = portMouseWheelSteps();
    portSheetDpadStep(&dpx, &dpy);   /* every tick, before any early return: it tracks the press edge */
    port_sheet_bwait_live = FALSE;   /* set again only if this tick reaches the B check */
    /* the Delete edge is read ONCE, here, every tick and before any early
     * return: a press made while an assign, a dialog, the on-screen keyboard
     * or a list owns the tick, or over a row that is not a bind, is spent
     * now - never banked for the next bind row the cursor crosses */
    del = portVideoDeleteEdge();
    {   /* Hotplug under a device page: the page holds a device index, and a
         * plug or unplug in a lower slot puts another player's pad behind it.
         * When the slot behind an unchanged index moves, drop anything armed
         * for the old pad and re-derive every player's device. */
        extern s32 portInputDeviceSlot(s32 dev);
        extern s32 portInputAssignActive(void);
        static s32 lastDev = -1;
        static s32 lastSlot = -2;
        const s32 devpage = port_sheet_kind == PORT_SHEET_PLAYER || port_sheet_kind == PORT_SHEET_BINDS
                         || port_sheet_kind == PORT_SHEET_PAD || port_sheet_kind == PORT_SHEET_MOUSEKB
                         || port_sheet_kind == PORT_SHEET_CTLGENERAL;

        if (devpage && port_sheet_bind_dev == lastDev && portInputDeviceSlot(port_sheet_bind_dev) != lastSlot
            && !portInputAssignActive())
        {
            portBindCaptureCancel();
            portSheetListCloseNow();
            portOskClose();
            portSheetSeedDevices();
            portSheetBuild(port_sheet_kind);
        }
        lastDev = port_sheet_bind_dev;
        lastSlot = portInputDeviceSlot(port_sheet_bind_dev);
    }
    if (port_sheet_swallow_press)
    {   /* the key or button that just answered a device assign is still
         * down: it must not confirm the row it was pressed over (which
         * would arm the assign again). Swallow until everything is up. */
        if (joyGetButtons(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
        {
            press = 0;
        }
        else
        {
            port_sheet_swallow_press = FALSE;
        }
    }

    if (portBindCaptureActive())
    {   /* An armed capture owns the tick, so the input being bound can't also
         * confirm the row or steer the cursor off it. B cancels. */
        const f32 hx = cursor_h_pos;
        const f32 hy = cursor_v_pos;

        if (del)
        {   /* Delete while armed: unbind the row being bound */
            const s32 r = (port_sheet_highlighted >= 0) ? portSheetSlotRow(port_sheet_highlighted) : -2;

            portBindCaptureCancel();
            if (r >= 0 && port_sheet_row_kind[r] == SHROW_BIND
                && portBindUnbind(port_sheet_bind_dev, port_sheet_row_bind[r]))
            {
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
            }
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
        {
            portBindCaptureCancel();
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL); /* every other B on the sheet makes a sound
                                                                                                * too */
        }
        else
        {   /* When the capture listens to another pad, player 1's held button
             * (the A that armed it, or the mouse) is counted here, since the
             * engine only times the captured pad; on player 1's own pad this
             * reads 0 and the engine's count is used. */
            if (joyGetButtons(PLAYER_1, 0xFFFF))
            {
                caphold += g_GlobalTimerDelta;
            }
            else
            {
                caphold = 0.0f;
            }
            if (caphold >= 180.0f)
            {
                const s32 r = (port_sheet_highlighted >= 0) ? portSheetSlotRow(port_sheet_highlighted) : -2;

                caphold = 0.0f;
                portBindCaptureCancel();
                if (r >= 0 && port_sheet_row_kind[r] == SHROW_BIND
                    && portBindUnbind(port_sheet_bind_dev, port_sheet_row_bind[r]))
                {
                    sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
                }
                port_sheet_swallow_press = TRUE;   /* the held button must not arm it again */
            }
            else
            {
                portBindCaptureTick();
            }
        }
        frontUpdateControlStickPosition();
        cursor_h_pos = hx;
        cursor_v_pos = hy;
        return;
    }
    caphold = 0.0f;

    {   /* AN ARMED DEVICE ASSIGN OWNS THE TICK, like a bind capture: every
         * pad is blanked while we listen, and the press that answers must
         * not also confirm the row. The crosshair is pinned for the same
         * reason. */
        extern s32 portInputAssignActive(void);
        extern s32 portInputAssignTick(void);
        extern s32 portInputAssignTake(void);

        if (portInputAssignActive())
        {
            const f32 hx = cursor_h_pos;
            const f32 hy = cursor_v_pos;

            if (portInputAssignTick())
            {
                const s32 dev = portInputAssignTake();

                portSheetSeedDevices();   /* a pad may have moved between players */
                port_sheet_swallow_press = TRUE;
                if (dev >= 0)
                {
                    port_sheet_bind_dev = dev;
                    sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
                }
                portSheetBuild(port_sheet_kind);   /* the device's own pages and list */
            }
            frontUpdateControlStickPosition();
            cursor_h_pos = hx;
            cursor_v_pos = hy;
            return;
        }
    }

    if (portDlgIsOpen())
    {   /* THE QUESTION OWNS THE TICK. Nothing under it moves - that is what
         * makes it worth asking - so the sheet takes no input at all here. */
        if (portDlgTick())
        {
            if (port_sheet_manage_open)
            {   /* save / delete / cancel, by index; a two-answer box (no
                 * user layout to delete) is save / cancel */
                const s32 pick = portDlgPick();

                port_sheet_manage_open = FALSE;
                if (pick == 0)
                {   /* seeded only with the player's OWN layout - a
                     * shipped name would be cut to the field and saved as a
                     * same-named user copy */
                    portOskOpen("SAVE CONFIG AS", (portSheetDelLayoutIdx() >= 0) ? portInputLayoutActive(PORT_SHEET_LAYOUT_DEV) : NULL);
                }
                else if (pick == 1 && port_sheet_del_idx >= 0)
                {
                    portInputLayoutDelete(port_sheet_del_idx);
                    portSheetBuild(port_sheet_kind);   /* the bind config row's list changed */
                }
            }
            else if (port_sheet_resetall_step != 0)
            {
                extern void portOptResetDefaults(void);   /* port_options.c */

                if (!portDlgSaidYes())
                {
                    port_sheet_resetall_step = 0;
                }
                else if (port_sheet_resetall_step == 1)
                {   /* asked twice: it puts back every setting on every page */
                    port_sheet_resetall_step = 2;
                    portDlgOpen("Are you sure?\n", "Yes\n", "No\n", 0);
                }
                else
                {
                    port_sheet_resetall_step = 0;
                    portOptResetDefaults();
                    portSheetBuild(port_sheet_kind);   /* the rows read the defaults */
                }
            }
            else if (port_sheet_reset_open)
            {
                extern void portBindResetDefaults(s32 dev);   /* port_watchpages.c */

                port_sheet_reset_open = FALSE;
                if (portDlgSaidYes())
                {
                    portBindResetDefaults(port_sheet_bind_dev);
                    portSheetBuild(port_sheet_kind);   /* the bind rows read the new binds */
                }
            }
            else if (portDlgSaidYes() && port_sheet_del_idx >= 0)
            {
                portInputLayoutDelete(port_sheet_del_idx);
            }
            port_sheet_del_idx = -1;
        }
        return;
    }

    if (port_osk_open)
    {   /* THE KEYBOARD OWNS THE TICK, and the crosshair is pinned under it the
         * way the locked edit pins it - the pad that is typing is the pad that
         * would otherwise be moving the cursor off the panel. */
        const f32 hx = cursor_h_pos;
        const f32 hy = cursor_v_pos;
        const s32 wasTyping = port_osk_typing;

        if (portOskTick())
        {
            if (port_osk_done > 0 && !portInputLayoutSaveCurrent(PORT_SHEET_LAYOUT_DEV, port_osk_text))
            {   /* The save refused the name (or failed): keep the keyboard open
                 * with it, and keep SDL text input running so a typing physical
                 * keyboard keeps typing instead of Enter falling through to
                 * binds. */
                port_osk_open = TRUE;
                port_osk_done = 0;
                if (wasTyping)
                {
                    port_osk_typing = TRUE;
                    portTextInputStart();
                }
            }
        }
        frontUpdateControlStickPosition();
        if (!port_osk_typing)
        {   /* while a real keyboard types, let the mouse still point at keys */
            cursor_h_pos = hx;
            cursor_v_pos = hy;
        }
        return;
    }

    if (port_sheet_list_row >= 0)
    {   /* THE LIST OWNS THE INPUT while it is up (and takes none while it animates shut) */
        if (!port_sheet_list_closing)
        {
            const s32 count = portSheetListCount(port_sheet_list_row);
            s32 x0, y0, w, n, e;
            s32 was = port_sheet_list_idx;

            portSheetListBox(&x0, &y0, &w, &n);
            /* Hover picks only on cursor motion (the on-screen keyboard's rule, port_osk_cursor_h);
             * otherwise a crosshair parked on the list re-picks its entry
             * before every button step. */
            static f32 listh = -1.0f, listv = -1.0f;
            const s32 cursormoved = (cursor_h_pos != listh || cursor_v_pos != listv);

            listh = cursor_h_pos;
            listv = cursor_v_pos;
            if (port_sheet_list_anim >= 2.0f && cursormoved)
            {   /* hover: an entry under the cursor is the highlight; elsewhere leaves it alone */
                for (e = 0; e < n; e++)
                {
                    const s32 ey = y0 + e * PORT_SHEET_ROW_PITCH;
                    if (cursor_h_pos >= (f32) x0 && cursor_h_pos <= (f32) (x0 + w)
                        && cursor_v_pos >= (f32) (ey - 1) && cursor_v_pos < (f32) (ey - 1 + PORT_SHEET_ROW_PITCH))
                    {
                        port_sheet_list_idx = port_sheet_list_scroll + e;
                    }
                }
            }
            /* a pad's D-pad steps the list too (portSheetDpadStep): it is
             * digital Move, so the button checks never saw it */
            const s32 dstep = dpy;
            const s32 look = portMenuLookSteps();                       /* the look stick's quick scroll */
            const u16 cup = portMenuLookActive() ? 0 : U_CBUTTONS;    /* ...not its C-button echo too */
            const u16 cdn = portMenuLookActive() ? 0 : D_CBUTTONS;

            if (look != 0)
            {
                port_sheet_list_idx += look;
                if (port_sheet_list_idx < 0) port_sheet_list_idx = 0;
                if (port_sheet_list_idx > count - 1) port_sheet_list_idx = count - 1;
                portSheetListScrollFollow();
            }
            else if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD | cup) || dstep > 0) && port_sheet_list_idx > 0)
            {
                port_sheet_list_idx--;
                portSheetListScrollFollow();
            }
            else if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD | cdn) || dstep < 0) && port_sheet_list_idx < count - 1)
            {
                port_sheet_list_idx++;
                portSheetListScrollFollow();
            }
            else if (wheel != 0 && count > n)
            {   /* the wheel scrolls the window, not the highlight (the watch's rule) */
                port_sheet_list_scroll -= wheel;
                if (port_sheet_list_scroll > count - n) port_sheet_list_scroll = count - n;
                if (port_sheet_list_scroll < 0) port_sheet_list_scroll = 0;
            }
            if (port_sheet_list_idx != was)
            {
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
            }
            if (press)
            {   /* A picks the highlight WHEREVER the cursor is: a pad's cursor never left the label */
                portSheetPick(port_sheet_list_row, port_sheet_list_idx);
                portSheetListClose();
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }
            else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
            {
                portSheetListClose();
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
            }
        }
        {   /* A key that also drives the stick (W A S D, see portNavStickDir)
             * doesn't drag the crosshair down the list, so one press steps
             * exactly one entry; a real thumbstick and the mouse still point. */
            extern s32 portVideoKbdStickDown(void);   /* video.c; weak 0 headless */
            const f32 lx = cursor_h_pos;
            const f32 ly = cursor_v_pos;

            frontUpdateControlStickPosition();
            if (portVideoKbdStickDown())
            {
                cursor_h_pos = lx;
                cursor_v_pos = ly;
            }
        }
        return;
    }

    if (port_sheet_edit_row >= 0)
    {   /* THE EDIT OWNS THE TICK: the crosshair is pinned and only this row moves */
        const s32 erow = port_sheet_edit_row;

        if (press)
        {
            portSheetEditEnd(TRUE);
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
        {
            portSheetEditEnd(FALSE);
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
        }
        else
        {
            s32 stepped = 0;

            if (joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD | R_CBUTTONS))
            {
                portSheetStep(erow, 1);
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
                stepped = 1;
            }
            else if (joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD | L_CBUTTONS))
            {
                portSheetStep(erow, -1);
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
                stepped = 1;
            }
            if (stepped)
            {   /* the push is ALREADY SEEN: a keyboard A / D is the C button
                 * and the stick at once (portNavStickDir), and the ramp's
                 * new-push step made every tap two notches. A held key
                 * still ramps on at the proportional rate. */
                port_sheet_ramp_row = erow;
                port_sheet_ramp_bank = 0.0f;
                port_sheet_ramp_click = PORT_SHEET_RAMP_CLICK;
            }
            else if (wheel != 0)
            {
                portSheetStep(erow, (wheel > 0) ? 1 : -1);
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
            }
            portSheetRampTick(erow);
        }
        /* PINNED, BOTH AXES: written after everything else has had its say, so
         * neither the stick nor the mouse can move it while the edit is open. */
        cursor_h_pos = port_sheet_edit_cx;
        cursor_v_pos = port_sheet_edit_cy;
        return;
    }

    if (dpy != 0)
    {   /* D-pad up / down jumps the crosshair to the middle of the next row's
         * band in that direction, from anywhere including a tab;
         * A and every other gesture then work on it unchanged. */
        s32 cur = -1;
        s32 best = -1;
        s32 s;

        for (s = 0; s < portSheetSlotCount(); s++)
        {
            if (!portSheetSlotInert(s) && portSheetSlotHit(s, (s32) cursor_h_pos, (s32) cursor_v_pos))
            {
                cur = s;
            }
        }
        if (port_sheet_twocol)
        {   /* TWO COLUMNS: walk the slots in order - down the left column,
             * on into the right - and take the crosshair to that column */
            const s32 step = (dpy < 0) ? 1 : -1;

            best = (cur >= 0) ? cur + step : ((dpy < 0) ? 0 : portSheetSlotCount() - 1);
            while (best >= 0 && best < portSheetSlotCount() && portSheetSlotInert(best))
            {
                best += step;   /* over the empty end of the left column */
            }
            if (best < 0 || best >= portSheetSlotCount())
            {
                best = -1;
            }
            if (best >= 0)
            {
                cursor_h_pos = (f32) (portSheetSlotLabelX(best) + 8);
            }
        }
        for (s = 0; s < PORT_SHEET_SLOTS && !port_sheet_twocol; s++)
        {
            const s32 sy = portSheetSlotY(s);
            if (portSheetSlotInert(s))
            {
                continue;
            }
            if (dpy < 0 && ((cur >= 0) ? sy > portSheetSlotY(cur) : sy > (s32) cursor_v_pos)
                && (best < 0 || sy < portSheetSlotY(best)))
            {
                best = s;   /* the nearest row below */
            }
            if (dpy > 0 && ((cur >= 0) ? sy < portSheetSlotY(cur) : sy < (s32) cursor_v_pos)
                && (best < 0 || sy > portSheetSlotY(best)))
            {
                best = s;   /* the nearest row above */
            }
        }
        if (best >= 0)
        {
            cursor_v_pos = (f32) (portSheetSlotY(best) - 1 + portSheetPitch() / 2);
            if (cursor_h_pos > TABS_LEFT_EDGE)
            {   /* the tabs are the whole right-hand column: off them, onto the value */
                cursor_h_pos = (f32) PORT_SHEET_VALUE_X;
            }
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
    }

    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    port_sheet_highlighted = -1;
    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = TRUE;
        if (press)
        {
            page_back = TRUE;
        }
    }
    else if (port_sheet_kind == PORT_SHEET_PORTOPTS && frontCheckCursorOnStartTab())
    {   /* the RESET ALL tab: every setting back to its default (binds are kept),
         * after two questions that both start on No */
        tab_start_highlight = TRUE;
        if (press)
        {
            port_sheet_resetall_step = 1;
            portDlgOpen("Reset All Settings?\n", "Yes\n", "No\n", 0);
        }
    }
    else if (!port_sheet_twocol && port_sheet_kind != PORT_SHEET_CREDITS && frontCheckCursorOnStartTab())
    {   /* the OPTIONS tab: straight back to the folder from any page (Gameplay Options has none) */
        tab_start_highlight = TRUE;
        if (press)
        {
            leave = TRUE;
        }
    }
    else if (port_sheet_kind == PORT_SHEET_CREDITS && frontCheckCursorOnStartTab())
    {   /* the "1997" tab (top): the end credits and the cast parade */
        tab_start_highlight = TRUE;
        if (press)
        {
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, NULL);
            portCreditsRoll();
            return;
        }
    }
    else if (port_sheet_kind == PORT_SHEET_VIDEO && frontCheckCursorOnNextTab())
    {   /* the CLASSIC tab (middle): which parts Classic Mode applies. Picks not
         * yet applied here are dropped, as leaving the page does. */
        tab_next_highlight = TRUE;
        if (press)
        {
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, NULL);
            portSheetPendingClear();
            portSheetOpen(PORT_SHEET_CLASSIC);
            return;
        }
    }
    else if (port_sheet_page < port_sheet_pages - 1 && frontCheckCursorOnNextTab())
    {
        tab_next_highlight = TRUE;
        if (press)
        {
            page_on = TRUE;
        }
    }
    else
    {   /* the slot whose band (its line, one above to the next line) holds the cursor */
        for (slot = 0; slot < portSheetSlotCount(); slot++)
        {
            if (!portSheetSlotInert(slot) && portSheetSlotHit(slot, (s32) cursor_h_pos, (s32) cursor_v_pos))
            {
                port_sheet_highlighted = slot;
                break;
            }
        }
    }

    slot = port_sheet_highlighted;
    { /* Unbind on a bind row: Delete, or any button held two seconds (120
       * fields); a hold that began as the A that armed a capture is counted by
       * the capture engine instead (portInputPadCaptureHoldLong). Use a float
       * accumulator: g_GlobalTimerDelta hovers around 1.0 and (s32)0.9999 is
       * zero. */
        const s32 r = (slot >= 0) ? portSheetSlotRow(slot) : -2;
        const s32 onbind = (r >= 0 && port_sheet_row_kind[r] == SHROW_BIND);

        /* On a bind row, B backs out on release, so a two-second hold can unbind instead.
         * Injected B (right mouse button, Escape, Menu Back) is an edge with no
         * level, so it still backs out at once. */
        if (!bwaitlive)
        {
            port_sheet_bwait = FALSE;
        }
        port_sheet_bwait_live = TRUE;
        if (port_sheet_bwait)
        {
            if (!joyGetButtons(PLAYER_1, B_BUTTON))
            {
                port_sheet_bwait = FALSE;
                if (!port_sheet_bunbound)
                {
                    page_back = TRUE;
                }
            }
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
        {
            if (onbind && joyGetButtons(PLAYER_1, B_BUTTON))
            {
                port_sheet_bwait = TRUE;
                port_sheet_bunbound = FALSE;
            }
            else
            {
                page_back = TRUE;
            }
        }
        {
            static f32 holdany;
            static s32 firedany;
            /* a hold belongs to the row it was PRESSED on (the watch's
             * rule): -1 = nothing held, -2 = spent (began off a bind row, or
             * the row, page or sheet changed under it) */
            static s32 holdkey = -1;
            const s32 key = onbind ? (port_sheet_kind * 4096 + port_sheet_page * 64 + r) : -2;

            if (!joyGetButtons(PLAYER_1, 0xFFFF))
            {
                holdkey = -1;
            }
            else if (holdkey == -1)
            {
                holdkey = key;
            }
            else if (holdkey != key)
            {
                holdkey = -2;
            }
            if (onbind && del)
            {
                if (portBindUnbind(port_sheet_bind_dev, port_sheet_row_bind[r]))
                {
                    sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
                }
            }
            if (onbind && joyGetButtons(PLAYER_1, 0xFFFF) && holdkey == key)
            {
                holdany += g_GlobalTimerDelta;
                if (holdany >= 120.0f && !firedany)   /* two seconds */
                {
                    firedany = TRUE;
                    port_sheet_bunbound = TRUE;   /* a held B's release must not back out now */
                    if (portBindUnbind(port_sheet_bind_dev, port_sheet_row_bind[r]))
                    {
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
                    }
                }
            }
            else
            {
                holdany = 0.0f;
                firedany = FALSE;
            }
        }
    }

    row = (slot >= 0) ? portSheetSlotRow(slot) : -2;
    if (row != -2 && !page_back && !page_on && !leave && !portSheetRowDisabled(row))
    {
        if (press)
        {
            if (row == -1)
            {
                portSheetApply();
            }
            else if (portSheetListCount(row) > 0)
            {
                portSheetListOpen(row, slot);
            }
            else if (port_sheet_row_kind[row] == SHROW_SAVELAYOUT)
            {
                if (portInputDeviceCount() > 1)
                {
                    portOskOpen("SAVE LAYOUT AS", (portSheetDelLayoutIdx() >= 0) ? portInputLayoutActive(PORT_SHEET_LAYOUT_DEV) : NULL); /* seeded only with the player's own layout
                                                                                                                                          * name */
                }
            }
            else if (port_sheet_row_kind[row] == SHROW_PLAYERPICK)
            {
                port_sheet_player = port_sheet_row_bind[row];
                portSheetOpen(PORT_SHEET_PLAYER);
            }
            else if (port_sheet_row_kind[row] == SHROW_GOBINDS)
            {
                portSheetOpen(PORT_SHEET_BINDS);
            }
            else if (port_sheet_row_kind[row] == SHROW_RESETBINDS)
            {   /* the Exit prompt's window; starts on No - it throws away every bind */
                port_sheet_reset_open = TRUE;
                portDlgOpen("Reset All Binds?\n", "Yes\n", "No\n", 0);
            }
            else if (port_sheet_row_kind[row] == SHROW_GOSHEET)
            {
                portSheetOpen(port_sheet_row_bind[row]);
            }
            else if (port_sheet_row_kind[row] == SHROW_BIND)
            {   /* ADDS to the binding rather than replacing it, so one action can
                 * have several inputs; UNBIND is its own gesture, below. */
                portBindCaptureBegin(port_sheet_bind_dev, port_sheet_row_bind[row]);
            }
            else if (port_sheet_row_kind[row] == SHROW_BINDDEV)
            {   /* "press a button on the device": the pad
                 * that answers is pinned to this player's slot */
                extern s32 portInputAssignBegin(s32 slot);

                portInputAssignBegin(port_sheet_player);
            }
            else if (port_sheet_row_kind[row] == SHROW_BINDMODE
                     || port_sheet_row_kind[row] == SHROW_CTRLSTYLE)
            {
                portSheetStep(row, 1);
            }
            else if (port_sheet_row_kind[row] == SHROW_MANAGELAYOUT)
            {   /* save / delete / cancel, starting on cancel so a stray press never deletes;
                 * Delete is only offered for a layout the player made. */
                static const char *const three[3] = { "Save\n", "Delete\n", "Cancel\n" };
                static const char *const two[2] = { "Save\n", "Cancel\n" };

                port_sheet_del_idx = portSheetDelLayoutIdx();
                port_sheet_manage_open = TRUE;
                if (port_sheet_del_idx >= 0)
                {
                    portDlgOpenN("Manage Config\n", three, 3, 2);
                }
                else
                {
                    portDlgOpenN("Manage Config\n", two, 2, 1);
                }
            }
            else if (port_sheet_row_kind[row] == SHROW_DELLAYOUT)
            {   /* the dialog's second caller. Default answer is NO - this one
                 * throws away something the player made. */
                port_sheet_del_idx = portSheetDelLayoutIdx();
                if (port_sheet_del_idx >= 0)
                {   /* snprintf, not sprintf: the name comes out of a layout
                     * FILE, and a downloaded or hand-edited one never went
                     * through the keyboard's 12-character cap */
                    char q[96];

                    snprintf(q, sizeof(q), "Delete %s?\n", portInputLayoutName(port_sheet_del_idx));
                    portDlgOpen(q, "Yes\n", "No\n", 0);
                }
            }
            else if (port_sheet_row_kind[row] == SHROW_OPT
                     && portOptIsRanged(port_sheet_row_opt[row]))
            {
                portSheetEditBegin(row);
            }
            else
            {
                portSheetStep(row, 1);   /* a toggle: A is the whole interaction */
            }
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        else if (row >= 0 && (joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD | R_CBUTTONS) || dpx > 0))
        {   /* a pad's D-pad left / right changes the value too (portSheetDpadStep) */
            portSheetStep(row, 1);
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
        else if (row >= 0 && (joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD | L_CBUTTONS) || dpx < 0))
        {
            portSheetStep(row, -1);
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
        else if (row >= 0 && wheel != 0)
        {
            portSheetStep(row, (wheel > 0) ? 1 : -1);
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
        }
    }
    frontUpdateControlStickPosition();

    if (leave)
    {
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        portSheetLeave(FALSE);
    }
    else if (page_on)
    {
        port_sheet_page++;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
    }
    else if (page_back)
    {
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        if (port_sheet_page > 0)
        {
            port_sheet_page--;   /* a page back keeps a pending pick */
        }
        else if (!portSheetUp())
        {
            portSheetLeave(TRUE);   /* nothing above this one: out to the folder (or the hub that opened it) */
        }
    }
}

static Gfx *portSheetDrawList(Gfx *DL)
{
    const s32 count = portSheetListCount(port_sheet_list_row);
    const s32 live = portSheetListLive(port_sheet_list_row);
    s32 x0, y0, w, n, e;

    portSheetListBox(&x0, &y0, &w, &n);

    /* THE CRT SWITCH-ON (portSheetDrawPanel). Leave one pixel of paper between
     * the edge and the bands on all four sides: the box is x0-1 .. x0+w+1 and
     * y0-3 .. last+16. */
    DL = portSheetDrawPanel(DL, x0 - 1, y0 - 3, x0 + w + 1, y0 + (n - 1) * PORT_SHEET_ROW_PITCH + 16, port_sheet_list_anim);
    if (port_sheet_list_anim < 2.0f)
    {
        return DL; /* still opening or closing: the shell only */
    }

    /* the value in use gets a band, drawn before the text so the text sits on top */
    if (live >= port_sheet_list_scroll && live < port_sheet_list_scroll + n)
    {
        const s32 ly = y0 + (live - port_sheet_list_scroll) * PORT_SHEET_ROW_PITCH;
        DL = microcode_constructor_related_to_menus(DL, x0 + 1, ly - 1, x0 + w - 1, ly + 0xE, PORT_SHEET_COL_LIST_LIVE);
    }
    for (e = 0; e < n; e++)
    {
        const s32 idx = port_sheet_list_scroll + e;
        const s32 ey = y0 + e * PORT_SHEET_ROW_PITCH;
        char *text = (char *) portSheetListTextAt(port_sheet_list_row, idx);
        s32 x = x0 + PORT_SHEET_LIST_PAD;
        s32 y = ey;

        if (idx == port_sheet_list_idx)
        {
            DL = microcode_constructor_related_to_menus(DL, x0 + 1, ey - 1, x0 + w - 1, ey + 0xE, PORT_SHEET_COL_LIST_HOT);
            DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
        }
        else
        {
            DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }
    }
    /* more above / more below: three stacked bars narrowing away from the list */
    {
        const s32 ax = x0 + w - 7;
        s32 k;

        if (port_sheet_list_scroll > 0)
        {
            for (k = 0; k < 3; k++)
            {
                DL = microcode_constructor_related_to_menus(DL, ax + k, y0 + k, ax + 6 - k, y0 + k + 1, 0x000000FF);
            }
        }
        if (port_sheet_list_scroll + n < count)
        {
            const s32 ay = y0 + (n - 1) * PORT_SHEET_ROW_PITCH + 10;
            for (k = 0; k < 3; k++)
            {
                DL = microcode_constructor_related_to_menus(DL, ax + k, ay + 2 - k, ax + 6 - k, ay + 3 - k, 0x000000FF);
            }
        }
    }
    DL = microcode_constructor(DL);
    return DL;
}

/* ---- the CREDITS sheet ------------------------------------------------------
 *
 * One string per paragraph, wrapped here to the paper's width, so editing the
 * credits never means re-breaking lines by hand; "" is a blank line. The second
 * line is built, not typed: the platform name and build stamp come from
 * sysstubs.c, the same ones the legal screen shows.
 *
 * The credits use their own pitch: 15 units for a line of text and 6 for a
 * blank one, ending at y ~305, just inside the picture (the frontend is
 * letterboxed below ~314). Do not go under 15: the underscore in "ben_GEDepot"
 * sits low enough to overlap the next line at a smaller pitch. If the text
 * grows past the paper, narrow the gaps or split it over two pages. */
#define PORT_CREDITS_WIDTH      318   /* label x 0x37 to just short of the tabs */
#define PORT_CREDITS_PITCH      15
#define PORT_CREDITS_GAP        6

static const char *const port_credits_text[] = {
    "Project Midas",
    NULL,   /* "<platform> Build <stamp>" - see portSheetDrawCredits */
    "",
    "James Bond has returned August 17th 2026.",
    "",
    "A port based on the GoldenEye 007 decompilation by kholdfuzion.",
    "With contributions by Luchador, Trevor, AXDOOMER and many others.",
    "",
    "",
    "",
    "Developed by Murk17 and AXDOOMER.",
    "",
    "",   /* an extra line break below Developed by */
    "Project Midas logos and additional textures by Redvox57.",
    "",
    "Alpha tested by AdzyIn3D, Al, ben_GEDepot, EternallyAries, Graslu00, Paracusia, Raf, Redvox57, Scott, TH126, ShootersForever and ZKA.",
};

static Gfx *portSheetDrawCredits(Gfx *DL)
{
    extern const char *portBuildString(void);
    extern const char *portPlatformName(void);
    char built[96];
    s32 y = PORT_SHEET_ROW_Y0;
    s32 p;

    sprintf(built, "%s Build %s", portPlatformName(), portBuildString());
    for (p = 0; p < (s32) (sizeof(port_credits_text) / sizeof(port_credits_text[0])); p++)
    {
        const char *src = (port_credits_text[p] != NULL) ? port_credits_text[p] : built;
        char line[160];
        s32 len = 0;

        if (src[0] == '\0')
        {
            y += PORT_CREDITS_GAP;
            continue;
        }
        line[0] = '\0';
        while (*src != '\0')
        {   /* one word at a time: keep it if the line still fits, else flush */
            char trial[160];
            s32 wl = 0;
            s32 th = 0, tw = 0;

            while (src[wl] != '\0' && src[wl] != ' ')
            {
                wl++;
            }
            if (len + wl + 3 >= (s32) sizeof(trial))
            {
                break;
            }
            sprintf(trial, "%s%s%.*s\n", line, (len > 0) ? " " : "", (int) wl, src);
            textMeasure(&th, &tw, trial, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
            if (tw > PORT_CREDITS_WIDTH && len > 0)
            {
                s32 x = PORT_SHEET_LABEL_X;
                s32 ly = y;

                strcat(line, "\n");
                DL = frontPrintText(DL, &x, &ly, line, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
                y += PORT_CREDITS_PITCH;
                line[0] = '\0';
                len = 0;
                continue;   /* the same word again, on the new line */
            }
            trial[strlen(trial) - 1] = '\0';   /* drop the measuring newline */
            strcpy(line, trial);
            len = (s32) strlen(line);
            src += wl;
            while (*src == ' ')
            {
                src++;
            }
        }
        if (len > 0)
        {
            s32 x = PORT_SHEET_LABEL_X;
            s32 ly = y;

            strcat(line, "\n");
            DL = frontPrintText(DL, &x, &ly, line, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
            y += PORT_CREDITS_PITCH;
        }
    }
    return DL;
}

Gfx *constructor_menu1C_portsheet(Gfx *DL)
{
    s32 slot;
    s32 x;
    s32 y;
    s32 th;
    s32 tw;

    DL = viSetFillColor(DL,0,0,0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    /* the title where the old Extended Options page put its own; "2/2" beside it on a turned page */
    x = port_sheet_twocol ? PORT_SHEET_COL1_LABEL_X : PORT_SHEET_LABEL_X;
    y = PORT_SHEET_TITLE_Y;
    if (!port_sheet_twocol)   /* the two-column sheet has no heading: its rows start on its line */
    {
        DL = frontPrintText(DL, &x, &y, (char *) portSheetTitle(), ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }
    if (port_sheet_pages > 1)
    {
        char pagetext[12];
        sprintf(pagetext, "%d/%d\n", (int) port_sheet_page + 1, (int) port_sheet_pages);
        x = PORT_SHEET_VALUE_X;
        y = PORT_SHEET_TITLE_Y;
        DL = frontPrintText(DL, &x, &y, pagetext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }

    if (port_sheet_kind == PORT_SHEET_CREDITS)
    {
        DL = portSheetDrawCredits(DL);
    }
    else if (port_sheet_rows == 0)
    {   /* a page with nothing on it yet */
        x = PORT_SHEET_LABEL_X;
        y = PORT_SHEET_ROW_Y0;
        DL = frontPrintText(DL, &x, &y, "Placeholder\n", ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }

    for (slot = 0; slot < portSheetSlotCount(); slot++)
    {
        const s32 row = portSheetSlotRow(slot);
        char *label;
        char *value;
        s32 colour = 0xFF;
        s32 lcolour = 0xFF;
        s32 hot;

        if (row == -2)
        {
            continue;
        }
        if (row == -1)
        {   /* the pinned APPLY CHANGES row */
            label = "Apply Changes\n";
            value = portSheetAnyPending() ? "Pending\n" : "";
            if (portSheetAnyPending())
            {
                colour = PORT_SHEET_COL_PENDING;
                lcolour = PORT_SHEET_COL_PENDING;
            }
        }
        else
        {
            label = (char *) portSheetLabel(row);
            value = (char *) portSheetValueText(row, &colour);
            if ((port_sheet_row_kind[row] == SHROW_OPT && !portOptAvailable(port_sheet_row_opt[row]))
                || portSheetRowDisabled(row))
            {
                lcolour = PORT_SHEET_COL_DIM;
            }
            else if (port_sheet_row_kind[row] == SHROW_OPT
                     && portOptIsToggle(port_sheet_row_opt[row])
                     && portOptGet(port_sheet_row_opt[row]) != 0)
            {   /* A toggle that is on draws its value (not the label) in red,
                 * the same 0xA00000FF the cheats page uses for active cheats.
                 * Only toggles: on the deferred display rows (never toggles)
                 * the same red means "picked, not yet applied". */
                colour = PORT_SHEET_COL_PENDING;
            }
        }
        hot = (slot == port_sheet_highlighted && !tab_prev_highlight && !tab_next_highlight && !tab_start_highlight)
            || (port_sheet_list_row >= 0 && slot == port_sheet_list_slot)
            || (row >= 0 && row == port_sheet_edit_row);

        if (row >= 0 && port_sheet_row_kind[row] == SHROW_DEVNAME)
        {   /* indented under Input Device; the assign prompt in the pending red */
            extern s32 portInputAssignActive(void);

            x = portSheetSlotLabelX(slot) + PORT_SHEET_DEVNAME_INDENT;
            y = portSheetSlotY(slot);
            DL = frontPrintText(DL, &x, &y, label, ptrFontZurichBoldChars, ptrFontZurichBold,
                                (portInputAssignActive() && port_sheet_row_bind[row] < 0) ? PORT_SHEET_COL_PENDING : lcolour,
                                viGetX(), viGetY(), 0, 0);
            if (port_sheet_row_bind[row] < 0)
            {   /* the player page: the same rule under its device, at
                 * the height the Control sheet draws its rules between players */
                const s32 ry = portSheetSlotY(slot) + portSheetPitch() - (portSheetPitch() - 0xE) / 2 - 1;

                DL = microcode_constructor_related_to_menus(DL, portSheetSlotLabelX(slot) - 2, ry,
                                                            PORT_SHEET_RULE_R, ry + 1, PORT_SHEET_COL_RULE);
                DL = microcode_constructor(DL);
            }
            continue;
        }
        if (row > 0 && port_sheet_row_kind[row] == SHROW_PLAYERPICK)
        {   /* A RULE BETWEEN PLAYERS, for clarity: a hairline half way
             * between the last player's device line and this name */
            const s32 ry = portSheetSlotY(slot) - (portSheetPitch() - 0xE) / 2 - 1;

            DL = microcode_constructor_related_to_menus(DL, portSheetSlotLabelX(slot) - 2, ry,
                                                        PORT_SHEET_RULE_R, ry + 1, PORT_SHEET_COL_RULE);
            DL = microcode_constructor(DL);
        }
        textMeasure(&th, &tw, label, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        x = portSheetSlotLabelX(slot);
        y = portSheetSlotY(slot);
        if (hot)
        {
            DL = microcode_constructor_related_to_menus(DL, x - 2, y - 1, x + tw + 5, y + 0xE, 0x32);
        }
        DL = frontPrintText(DL, &x, &y, label, ptrFontZurichBoldChars, ptrFontZurichBold, lcolour, viGetX(), viGetY(), 0, 0);

        if (value[0] != '\0')
        {
            x = PORT_SHEET_VALUE_X;
            if (port_sheet_twocol)
            {   /* right-aligned to the column's edge (see port_sheet_twocol) */
                s32 vh = 0, vw = 0;

                textMeasure(&vh, &vw, value, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                x = ((slot >= PORT_SHEET_TWOCOL_ROWS) ? PORT_SHEET_COL2_VALUE_R : PORT_SHEET_COL1_VALUE_R) - vw;
            }
            y = portSheetSlotY(slot);
            if (row >= 0 && row == port_sheet_edit_row)
            {   /* being edited: the value on a dark band, the way the open
                 * list marks the entry under the cursor - the crosshair is
                 * pinned, so the band is the only thing saying where input
                 * is going */
                textMeasure(&th, &tw, value, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                DL = microcode_constructor_related_to_menus(DL, x - 3, y - 1, x + tw + 3, y + 0xE, PORT_SHEET_COL_LIST_HOT);
                DL = frontPrintText(DL, &x, &y, value, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
            }
            else
            {
                DL = frontPrintText(DL, &x, &y, value, ptrFontZurichBoldChars, ptrFontZurichBold, colour, viGetX(), viGetY(), 0, 0);
            }
        }
    }

    DL = portSheetDrawHelp(DL);   /* under the list, which may reach down over it */
    if (port_sheet_list_row >= 0)
    {
        DL = portSheetDrawList(DL); /* last, so it sits over the rows it covers */
    }
    if (portBindCaptureActive())
    {   /* REBIND: the capture says what it wants and how to
         * unbind, in the Exit prompt's window - Delete, or any button held
         * two seconds (the unbind at the top of the sheet tick) */
        DL = portDlgDrawNote(DL, (port_sheet_bind_dev == 0) ? "Press a Key\n" : "Press a Button\n",
                             "Del or Hold to Unbind\n");
    }
    if (port_osk_open)
    {
        DL = portOskDraw(DL);
    }
    if (portDlgIsOpen())
    {
        DL = portDlgDraw(DL);   /* over the keyboard too, if one is somehow up */
    }

    if (port_sheet_kind == PORT_SHEET_PORTOPTS)
    {
        DL = portAddTopTabText(DL, portResetAllTabText());
    }
    else if (port_sheet_kind == PORT_SHEET_CREDITS)
    {   /* Credits has "1997" on top instead of OPTIONS */
        DL = portAddTopTabText(DL, PORT_CREDITS_TAB_TEXT);
    }
    else if (!port_sheet_twocol)
    {   /* no OPTIONS tab on Gameplay Options */
        DL = portAddTopTabText(DL, "OPTIONS\n");
    }
    if (port_sheet_kind == PORT_SHEET_VIDEO)
    {
        DL = portAddMiddleTabText(DL, PORT_CLASSIC_TAB_TEXT);
    }
    else if (port_sheet_page < port_sheet_pages - 1)
    {
        DL = frontAddNextTabText(DL);
    }
    DL = frontAddPreviousTabText(DL);
    if (!port_osk_open || port_osk_typing)
    {   /* Hide the crosshair over the keyboard while it is pinned, since a
         * parked crosshair looks like a second selection; it comes back while a
         * real keyboard types, when the mouse can still point at cells. */
        DL = frontDrawCursor(DL);
    }
    return DL;
}
#endif /* !TARGET_N64 */


s32 pull_and_display_text_for_folder_a0(s32 arg0)
{
    int i;

    for (i=0; mission_folder_setup_entries[i].folder_text_preset != 0 ; i++)
    {
        if (arg0 == mission_folder_setup_entries[i].mission_num)
        {
            return i;
        }
    }

    return -1;
}


s32 check_if_stage_completed_on_difficulty(int stage, DIFFICULTY difficulty)
{
    s32 num;

    num = mission_folder_setup_entries[stage].mission_num;
    if (num >= 0)
    {
        return fileIsStageUnlockedAtDifficulty(selected_folder_num, num, difficulty);
    }
    return 0;
}





s32 get_highest_unlocked_difficulty_for_level(s32 arg0)
{
    s32 num;
    s32 difficulty;
    s32 temp_v0;

    if (mission_folder_setup_entries[pull_and_display_text_for_folder_a0(arg0)].stage_id >= 0)
    {
        num = DIFFICULTY_00;
        if (fileIs007ModeUnlocked(selected_folder_num) || get_debug_007_unlock_flag())
        {
            num = DIFFICULTY_007;
        }

        for (difficulty=num; difficulty >= 0; difficulty--)
        {
            temp_v0 = fileIsStageUnlockedAtDifficulty(selected_folder_num, arg0, difficulty);
            if (g_AppendCheatSinglePlayer == 0)
            {
                if (temp_v0 != DIFFICULTY_AGENT)
                {
                    return difficulty;
                }
            }
            else if (temp_v0 == DIFFICULTY_007)
            {
                return difficulty;
            }
        }
    }

    return -1;
}


//********************************************************************************************************
//MISSION SELECT
//********************************************************************************************************
void init_menu07_missionselect(void)
{
#ifndef TARGET_N64
    frontDropUnearnedCheats();   /* cheats left on by the watch's Unlock All row */
#endif
    selected_stage = LEVELID_NONE;
    briefingpage = BRIEFING_INVALID;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    if (maybe_is_in_menu)
    {
        musicTrack1Play(M_FOLDERS);
        maybe_is_in_menu = FALSE;
    }
    load_walletbond();
}


void update_menu07_missionsel(void) 
{
    return;
}


// Address 0x7F00DEB8 NTSC
void interface_menu07_missionsel(void)
{
    s32 var_s4;
    s32 var_s2;
    struct ModelRwData_DisplayList_CollisionRecord *temp_s1;
    s32 temp_a0;
    s32 i;
    s32 var_s1;
    struct ModelRoData_DisplayList_CollisionRecord *temp_s4;
    ModelNode *mnode;
    s32 padding;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], 0, 1);
    set_item_visibility_in_objinstance(walletinst[0], 0x13, 1);
    set_item_visibility_in_objinstance(walletinst[0], 0x14, 1);
    tab_prev_highlight = 0;
    mission_difficulty_highlighted = -1;

    if (frontCheckCursorOnPreviousTab() != 0)
    {
        tab_prev_highlight = 1;
    }
    else
    {
        for (var_s4 = 0; var_s4 < 5; )
        {
            if (var_s4 < 4)
            {
                if(!((cursor_xpos_table_mission_select[var_s4] + cursor_xpos_table_mission_select[var_s4+1]) * 0.5f > cursor_h_pos))
                {
                    var_s4++;
                    continue;
                }
            }

            break;
        }

        for (var_s2 = 0; var_s2 < 4; )
        {
            if (var_s2 < 3)
            {
                if(!((cursor_ypos_table_mission_select[var_s2] + cursor_ypos_table_mission_select[var_s2+1]) * 0.5f > cursor_v_pos))
                {
                    var_s2++;
                    continue;
                }
            }

            break;
        }

        for ( ; var_s2 > 0; var_s2--)
        {
            for (var_s1 = 0; var_s1 < 5; var_s1++)
            {
                if (get_highest_unlocked_difficulty_for_level((var_s2 * 5) + var_s1) >= 0)
                {
                    break;
                }
            }

            if (var_s1 < 5)
            {
                break;
            }

            if (1);
        }

        for ( ; var_s4 >= 0; var_s4--)
        {
            if (get_highest_unlocked_difficulty_for_level((var_s2 * 5) + var_s4) >= 0)
            {
                break;
            }
        }

        if (var_s4 < 0)
        {
            for (var_s4 = 0; var_s4 < 5; var_s4++)
            {
                if (get_highest_unlocked_difficulty_for_level((var_s2 * 5) + var_s4) >= 0)
                {
                    break;
                }
            }
        }

        mission_difficulty_highlighted = (var_s2 * 5) + var_s4;
    }

    mnode = walletinst[0]->obj->Switches[0x15];

    if (mnode != NULL)
    {
        temp_s4 = (struct ModelRoData_DisplayList_CollisionRecord *)mnode->Data;

        temp_s1 = (struct ModelRwData_DisplayList_CollisionRecord *)modelGetNodeRwData(walletinst[0], mnode);
        temp_s1->Vertices = dynAllocateVertices(temp_s4->numVertices);

        for (i = 0; i < temp_s4->numVertices; i++)
        {
            // struct copy
            temp_s1->Vertices[i] = temp_s4->Vertices[i];

            temp_a0 = i / 4;

            if (get_highest_unlocked_difficulty_for_level(temp_a0) >= 0)
            {
                if (temp_a0 == mission_difficulty_highlighted)
                {
                    temp_s1->Vertices[i].r = 0xff;
                    temp_s1->Vertices[i].g = 0xff;
                    temp_s1->Vertices[i].b = 0xff;
                    temp_s1->Vertices[i].a = 0xf5;
                }
                else
                {
                    temp_s1->Vertices[i].r = 0x6e;
                    temp_s1->Vertices[i].g = 0x6e;
                    temp_s1->Vertices[i].b = 0x6e;
                    temp_s1->Vertices[i].a = 0xff;
                }
            }
            else
            {
                temp_s1->Vertices[i].r = 0xf;
                temp_s1->Vertices[i].g = 0xf;
                temp_s1->Vertices[i].b = 0xf;
                temp_s1->Vertices[i].a = 0xff;
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(0, A_BUTTON | START_BUTTON | Z_TRIG) != 0)
    {
        if (tab_prev_highlight != 0)
        {
            tab_prev_selected = 1;

            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        else if (mission_difficulty_highlighted >= 0)
        {
            briefingpage = pull_and_display_text_for_folder_a0(mission_difficulty_highlighted);
            selected_stage = mission_folder_setup_entries[briefingpage].stage_id;
            tab_next_selected = 1;

            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
    }
    else if (joyGetButtonsPressedThisFrame(0, B_BUTTON) != 0)
    {
        tab_prev_selected = 1;

        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }

    frontUpdateControlStickPosition();

    if (tab_next_selected != 0)
    {
        frontChangeMenu(MENU_DIFFICULTY, 0);

#if defined(VERSION_JP) || defined(VERSION_EU)
        if (j_text_trigger != 0)
        {
            set_cursor_pos_difficulty(0);
        }
        else
#endif
        {
            set_cursor_pos_difficulty(get_highest_unlocked_difficulty_for_level(mission_folder_setup_entries[briefingpage].mission_num));
        }
    }
    else if (tab_prev_selected != 0)
    {
        frontChangeMenu(MENU_MODE_SELECT, 0);
        setCursorPOSforMode(0);
    }
}


void set_cursor_to_stage_solo(LEVEL_SOLO_SEQUENCE level)
{
    if (frontCursorSnapAllowed())
    {
        cursor_h_pos = cursor_xpos_table_mission_select[level % 5];
        cursor_v_pos = cursor_ypos_table_mission_select[level / 5];
    }
}

// Address 0x7F00E458 NTSC
Gfx *constructor_menu07_missionsel(Gfx *DL)
{
    s32 i;
    s32 spD8;
    s32 padding[3];
    s32 spC8;
    s32 spC4;
    s32 temp_s0;
    s32 var_s5;
    s32 padding2[5];
    u8 sp90[24];
    s32 sp8C;
    s32 sp88;
    s32 temp_v0_3;
    u8 *var_v0;
    s32 var_s3;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    gDPSetFogColor(DL++, 0xFF, 0xFF, 0xFF, 0xFF);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    for (spD8 = 0; spD8 < 5; spD8++)
    {
        var_s3 = spD8;

        for (i = 0; i < 4 ; i++, var_s3 += 5)
        {
            temp_s0 = pull_and_display_text_for_folder_a0(var_s3);

            if (get_highest_unlocked_difficulty_for_level(var_s3) >= 0)
            {
                var_s5 = 0x96969600;

                if (var_s3 == mission_difficulty_highlighted)
                {
                    var_s5 = -0x100;
                }

                if (mission_folder_setup_entries[temp_s0].icon_text_preset)
                {
                    strcpy(sp90, langGet(mission_folder_setup_entries[temp_s0].icon_text_preset));
                }
                else
                {
                    strcpy(sp90, langGet(mission_folder_setup_entries[temp_s0].folder_text_preset));
                }

                if (j_text_trigger == 0)
                {
                    temp_v0_3 = strlen(sp90) - 1;
                    for (; temp_v0_3 >= 0; temp_v0_3--)
                    {
                        if (sp90[temp_v0_3] >= 'a')
                        {
                            sp90[temp_v0_3] -= 0x20;
                        }
                    }
                }

                strcat(sp90, "\n");

                spC4 = 0;
                spC8 = 0;

                textMeasure(&spC8, &spC4, sp90, ptrFontBankGothicChars, ptrFontBankGothic, 0);

                sp8C = cursor_xpos_table_mission_select[spD8] - 0x1F;
                sp88 = (cursor_ypos_table_mission_select[i] - spC8) + 0x1D;

                DL = microcode_constructor_related_to_menus(DL, sp8C, sp88, sp8C + spC4, sp88 + spC8, 0);
                DL = textRender(DL, &sp8C, &sp88, sp90, ptrFontBankGothicChars, ptrFontBankGothic, var_s5 | 0xFF, viGetX(), viGetY(), 0, 0);

                sp8C = cursor_xpos_table_mission_select[spD8] - 0x1F;
                sp88 = (cursor_ypos_table_mission_select[i] - spC8) + 0x1D;

                DL = textRender(DL, &sp8C, &sp88, sp90, ptrFontBankGothicChars, ptrFontBankGothic, var_s5 | 0x64, viGetX(), viGetY(), 0, 0);

            }
        }
    }

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);

    return DL;
}



//********************************************************************************************************
//DIFFICULTY SELECT
//********************************************************************************************************
void init_menu08_difficultyselect(void)
{
    selected_difficulty = DIFFICULTY_MULTI;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    load_walletbond();
}

void update_menu08_difficulty(void) {
    return;
}


void interface_menu08_difficulty(void)
{
    s32 unlockedDifficulty;

    unlockedDifficulty = get_highest_unlocked_difficulty_for_level(mission_folder_setup_entries[briefingpage].mission_num);
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CONFIDENTIAL, 1);
    tab_prev_highlight = FALSE;
    mission_difficulty_highlighted = DIFFICULTY_MULTI;

    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = TRUE;
    }
    else if ((unlockedDifficulty >= DIFFICULTY_007) && (cursor_v_pos >= 275.0f))
    {
        mission_difficulty_highlighted = DIFFICULTY_007;
    }
    else if ((unlockedDifficulty >= DIFFICULTY_00) && (cursor_v_pos >= 243.0f))
    {
        mission_difficulty_highlighted = DIFFICULTY_00;
    }
    else if ((unlockedDifficulty > DIFFICULTY_AGENT) && (cursor_v_pos >= 211.0f))
    {
        mission_difficulty_highlighted = DIFFICULTY_SECRET;
    }
    else
    {
        mission_difficulty_highlighted = DIFFICULTY_AGENT;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {
        if (tab_prev_highlight)
        {
            tab_prev_selected = TRUE;

            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        else
        {
            if (mission_difficulty_highlighted >= DIFFICULTY_AGENT)
            {
                selected_difficulty = mission_difficulty_highlighted;
                tab_next_selected = TRUE;

                sndPlaySfx(g_musicSfxBufferPtr, PAPER_TURN_SFX, NULL);
            }
        }
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }

    frontUpdateControlStickPosition();

    if (tab_next_selected)
    {
        if (selected_difficulty == DIFFICULTY_007)
        {
            frontChangeMenu(MENU_007_OPTIONS, FALSE);
            frontSetCursorPositionToNextTab();

            return;
        }

        frontChangeMenu(MENU_BRIEFING, FALSE);
        frontSetCursorPositionToNextTab();

        return;
    }
    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MISSION_SELECT, FALSE);
        set_cursor_to_stage_solo(mission_folder_setup_entries[briefingpage].mission_num);
    }
}


Gfx * print_current_solo_briefing_stage_name(Gfx *DL, char *text)
{
    s32 x;
    s32 y;

    int chapter;

    if (selected_difficulty >= DIFFICULTY_AGENT)
    {
        strcpy(text, get_ptr_difficulty_name(selected_difficulty));
        strcat(text, langGet(getStringID(LTITLE, TITLE_STR_32_JB)));
        x = 0x37;
        y = 0x57;
        DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xff, viGetX(), viGetY(), 0, 0);
    }

    chapter = get_chapter_briefing_entry(briefingpage);
    if (chapter >=0 )
    {
        strcpy(text, langGet(getStringID(LTITLE, TITLE_STR_33_MISSION2)));
        strcat(text, mission_folder_setup_entries[chapter].string_ptr);
        strcat(text, ": ");
        strcat(text, langGet(mission_folder_setup_entries[chapter].folder_text_preset));
        strcat(text, "\n");
        x = 0x37;
        y = 0x67;
        DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xff, viGetX(), viGetY(), 0, 0);
    }

    strcpy(text, langGet(getStringID(LTITLE, TITLE_STR_34_PART)));
    strcat(text, mission_folder_setup_entries[briefingpage].string_ptr);
    strcat(text, ": ");
    strcat(text, langGet(mission_folder_setup_entries[briefingpage].folder_text_preset));
    strcat(text, "\n");
    x = 0x37;
    y = 0x77;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xff, viGetX(), viGetY(), 0, 0);
    return DL;
}


void set_cursor_pos_difficulty(int difficulty)
{
    if (frontCursorSnapAllowed())
    {
        cursor_h_pos = 106.0;
        cursor_v_pos = difficulty * 0x1e + 0xba;
    }
}





/**
 * See notes below. Assume this is not in original codebase.
*/
struct MatchHack_front_rodata_3000 { u8 data[0xbb8]; };

/**
 * See notes below. Assume this is not in original codebase.
 * (the struct isn't, but the 3000 character newline string is)
*/
#ifndef VERSION_EU
const struct MatchHack_front_rodata_3000 asc_D_8004F4B4 = { "\n" };
#endif

Gfx *constructor_menu08_difficulty(Gfx *DL)
{
    u8 *text_sp3180;
    s32 unlockedDifficulty; // sp3176
    u8 *text_sp3172;
    s32 x;    // sp3168
    s32 y;    // sp3164 (sp164 + 3000)

    // This should be a `u8 [3000]`, see notes below.
    struct MatchHack_front_rodata_3000 stagename_struct;

    u8 *text_sp160; // sp ??
    s32 sp9C;                         // sp156
    s32 sp98;                         // sp152
    f32 xypos[2];                     // sp144
    f32 halfedxy[2];                  // sp136
    struct sImageTableEntry *image;   // sp ??
    s32 i;

    unlockedDifficulty = get_highest_unlocked_difficulty_for_level(mission_folder_setup_entries[briefingpage].mission_num);

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
#ifdef VERSION_EU
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
#endif
    DL = frontSetupMenuBackground(DL);

#ifndef VERSION_EU
    /**
     * MatchHack notes:
     *
     * There is a chunk of auto generated code that copies 3000 characters
     * into the `stagename` variable. These 3000 characters are .rodata.
     * It would make sense that this is an array initialization spot,
     * but that means this is the start of a basic block which causes
     * problems ... but that might indicate there was a seperate DEBUG
     * difficulty menu. Anyways, a basic block split here messes
     * with the `DL` variable.
     * So that leaves struct copy, which just seems wrong.
     * But at least it matches.
     * -BB Feb 3, 2022
    */
    stagename_struct = asc_D_8004F4B4;
#endif

    DL = microcode_constructor(DL);
    DL = print_current_solo_briefing_stage_name(DL, &stagename_struct);

    //  "DIFFICULTY:\n"
    text_sp3180 = langGet(getStringID(LTITLE, TITLE_STR_35_DIFFICULTY));
    x = 0x37;
    y = 0x8F;
    DL = frontPrintText(DL, &x, &y, text_sp3180, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    if (mission_difficulty_highlighted >= 0)
    {
        DL = microcode_constructor_related_to_menus(
            DL,
            0x7E - (j_text_trigger ? 0x32 : 0),
            (mission_difficulty_highlighted * 0x1E) + 0xB2,
            0xF0,
            (mission_difficulty_highlighted * 0x1E) + 0xC3,
            0x32);
    }

    /**
     * Draw the list of available difficulties. Each item is
     * prefixed with a number.
    */
    for (i=0; i<DIFFICULTY_MAX; i++)
    {
        if (i == 0 || unlockedDifficulty >= i)
        {
            if (j_text_trigger)
            {
                switch (i)
                {
                    case 0:
                        text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_265_1DOT)); // "1.\n"
                        break;
                    case 1:
                        text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_266_2DOT)); // "2.\n"
                        break;
                    case 2:
                        text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_267_3DOT)); // "3.\n"
                        break;
                    case 3:
                        text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_268_4DOT)); // "4.\n"
                        break;
                }
            }
            else
            {
                sprintf(&stagename_struct, "%d.\n", i + 1);
                text_sp160 = &stagename_struct;
            }

            textMeasure(&sp98, &sp9C, text_sp160, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

            x = 0x82 - (j_text_trigger ? (sp9C - 0xA) : 0);
            y = (i * 0x1E) + 0xB4;
            DL = frontPrintText(DL, &x, &y, text_sp160, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

            switch (i)
            {
                case 0:
                    text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_36_AGENT2)); // "Agent\n"
                    break;
                case 1:
                    text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_37_SECRETAGENT2)); // "Secret Agent\n"
                    break;
                case 2:
                    text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_38_00AGENT2)); // "00 Agent\n"
                    break;
                case 3:
                    text_sp160 = langGet(getStringID(LTITLE, TITLE_STR_39_0072)); // "007\n"
                    break;
            }

            x = 0x96;
            y = (i * 0x1E) + 0xB4;
            DL = frontPrintText(DL, &x, &y, text_sp160, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }
    }

    DL = frontAddPreviousTabText(DL);

    /**
     * Draw the checkmark for completed difficulties.
    */
    for (i=0; i<3; i++)
    {
        if ((i == 0 || unlockedDifficulty >= i)
            && (check_if_stage_completed_on_difficulty(briefingpage, i) == 3))
        {
            image = &mainfolderimages[IMAGE_CHECK];

            xypos[0] = 280.0f;
            xypos[1] = (f32) ((i * 0x1E) + 0xBA);

            halfedxy[0] = image->width * 0.5f;
            halfedxy[1] = image->height * 0.5f;

            texSelect(&DL, image, 4, 0, 0);
            display_image_at_position(&DL, &xypos, &halfedxy, image->width, image->height, 0, 0, 1, 0xB4, 0, 0, 0xFF, image->level > 0, 0);
        }
    }

    DL = frontDrawCursor(DL);

    return DL;
}

//********************************************************************************************************
//007 DIFFICULTY SELECT
//********************************************************************************************************

void init_menu09_007difficultyselect(void)
{
    tab_start_selected = 0;
    tab_next_selected = 0;
    tab_prev_selected = 0;
    tab_prev_highlight = 0;
    tab_next_highlight = 0;
    tab_start_highlight = 0;
    highlight_enemy_reaction = 0;
    highlight_enemy_health = 0;
    highlight_enemy_damage = 0;
    highlight_enemy_accuracy = 0;
    load_walletbond();
}


void update_menu09_007options(void) 
{
    return;
}


void interface_menu09_007options(void)
{
    f32 temp_x;
    s32 temp_y;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    if (joyGetButtons(PLAYER_1, Z_TRIG|A_BUTTON) == 0)
    {
        tab_prev_highlight = FALSE;
        tab_next_highlight = FALSE;
        tab_start_highlight = FALSE;
        highlight_enemy_reaction = FALSE;
        highlight_enemy_health = FALSE;
        highlight_enemy_damage = FALSE;
        highlight_enemy_accuracy = FALSE;

        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = TRUE;
        }
        else if (frontCheckCursorOnNextTab())
        {

            tab_next_highlight = TRUE;
        }
        else if (frontCheckCursorOnStartTab())
        {
            tab_start_highlight = TRUE;
        }
        else
        {
            temp_y = (s32) cursor_v_pos;
            if (temp_y >= 0x107)
            {
                highlight_enemy_reaction = TRUE;
            }
            else if (temp_y >= 0xE6)
            {
                highlight_enemy_accuracy = TRUE;
            }
            else if (temp_y >= 0xC5)
            {
                highlight_enemy_damage = TRUE;
            }
            else if (temp_y >= 0xA4)
            {
                highlight_enemy_health = TRUE;
            }
            else
            {
                tab_next_highlight = TRUE;
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON))
    {
        tab_start_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON))
    {
        if (tab_next_highlight)
        {
            tab_next_selected = TRUE;
        }
        else if (tab_prev_highlight)
        {
            tab_prev_selected = TRUE;
        }
        else if (tab_start_highlight)
        {
            tab_start_selected = TRUE;
        }
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }

    if (joyGetButtons(PLAYER_1, Z_TRIG|A_BUTTON))
    {
        temp_x = (cursor_h_pos - 55.0f) / 300.0f;

        if (temp_x > 1.0f)
        {
            temp_x = 1.0f;
        }

        if (temp_x < 0.0f)
        {
            temp_x = 0.0f;
        }

        if (highlight_enemy_reaction)
        {
            slider_007_mode_reaction = temp_x;
        }
        else if (highlight_enemy_health)
        {
            slider_007_mode_health = (f32) (temp_x * temp_x * 10.0f);
        }
        else if (highlight_enemy_damage)
        {
            slider_007_mode_damage = (f32) (temp_x * temp_x * 10.0f);
        }
        else if (highlight_enemy_accuracy)
        {
            slider_007_mode_accuracy = (f32) (temp_x * temp_x * 10.0f);
        }
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();

    if (tab_start_selected)
    {
        frontChangeMenu(MENU_RUN_STAGE, TRUE);
        return;
    }

    if (tab_next_selected)
    {
        frontChangeMenu(MENU_BRIEFING, FALSE);
        return;
    }

    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_DIFFICULTY, FALSE);
        set_cursor_pos_difficulty(selected_difficulty);
    }
}


#if !defined(VERSION_EU)
const struct MatchHack_front_rodata_3000 asc_D_80050074 = { "\n" };
#endif

/**
 * Render the 007 Mode options. The slider bars are 300 pixels wide and
 * sit 33 pixels apart vertically.
 */
Gfx *constructor_menu09_007options(Gfx *DL)
{
    u8 *labeltext;
    s32 textx;
    s32 texty;
    struct MatchHack_front_rodata_3000 textbuffer;
    s32 textheight;
    s32 textwidth;
    s32 barwidth;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
#if defined(VERSION_EU)
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
#endif
    DL = frontSetupMenuBackground(DL);

#if !defined(VERSION_EU)
    textbuffer = asc_D_80050074;
#endif

    DL = microcode_constructor(DL);
    DL = print_current_solo_briefing_stage_name(DL, (char*)&textbuffer);

    labeltext = langGet(getStringID(LTITLE, TITLE_STR_40_SPECOPS)); /* SPECIAL OPTIONS */

    textx = 55;
    texty = 143;

    DL = frontPrintText(DL, &textx, &texty, (s8 *)labeltext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    /**
     * ENEMY HEALTH
     */
    labeltext = langGet(getStringID(LTITLE, TITLE_STR_42_HEALTH)); /* Enemy health */

    textx = 57;
    texty = 164;

    // Render the bar background with a black color and an opacity of 50/255.
    DL = microcode_constructor_related_to_menus(DL, 55, 181, 355, 192, 0x00000032);

    barwidth = (sqrtf(slider_007_mode_health / 10.0f)) * 300.0f;

    // Render the filled portion of the bar with a black color and an opacity of 100/255.
    DL = microcode_constructor_related_to_menus(DL, 55, texty + 17, barwidth + 55, texty + 28, 0x00000064);

    if (highlight_enemy_health != 0)
    {
        // Render a black rectangle over the "Enemy health" text to indicate it's highlighted.
        DL = microcode_constructor_related_to_menus(DL, 55, texty - 1, 199, texty + 14, 0x00000032);
    }

    // Render "Enemy health" text.
    DL = frontPrintText(DL, &textx, &texty, (s8 *)labeltext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    sprintf((char*)&textbuffer, "%d%%\n", (s32) (slider_007_mode_health * 100.0f));

    textwidth = 0;
    textheight = 0;

    textMeasure(&textheight, &textwidth, (char*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

    textx = 285 - textwidth;
    texty = 164;

    // Render enemy health percentage text.
    DL = frontPrintText(DL, &textx, &texty, (s8*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    /**
     * ENEMY DAMAGE
     */
    labeltext = langGet(getStringID(LTITLE, TITLE_STR_43_DAMAGE)); /* Enemy damage */

    textx = 57;
    texty = 197;

    DL = microcode_constructor_related_to_menus(DL, 55, 214, 355, 225, 0x00000032);
    barwidth = (sqrtf(slider_007_mode_damage / 10.0f)) * 300.0f;
    DL = microcode_constructor_related_to_menus(DL, 55, texty + 17, barwidth + 55, texty + 28, 0x00000064);

    if (highlight_enemy_damage != 0)
    {
        DL = microcode_constructor_related_to_menus(DL, 55, texty - 1, 199, texty + 14, 0x00000032);
    }

    DL = frontPrintText(DL, &textx, &texty, (s8*)labeltext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    sprintf((char*)&textbuffer, "%d%%\n", (s32) (slider_007_mode_damage * 100.0f));

    textwidth = 0;
    textheight = 0;

    textMeasure(&textheight, &textwidth, (char*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

    textx = 285 - textwidth;
    texty = 197;

    // Render enemy damage percentage text.
    DL = frontPrintText(DL, &textx, &texty, (s8*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    /**
     * ENEMY ACCURACY
     */
    labeltext = langGet(getStringID(LTITLE, TITLE_STR_44_ACCURACY)); /* Enemy accuracy */

    textx = 57;
    texty = 230;

    DL = microcode_constructor_related_to_menus(DL, 55, 247, 355, 258, 0x00000032);
    barwidth = (sqrtf(slider_007_mode_accuracy / 10.0f)) * 300.0f;
    DL = microcode_constructor_related_to_menus(DL, 55, texty + 17, barwidth + 55, texty + 28, 0x00000064);

    if (highlight_enemy_accuracy != 0)
    {
        DL = microcode_constructor_related_to_menus(DL, 55, texty - 1, 199, texty + 14, 0x00000032);
    }

    DL = frontPrintText(DL, &textx, &texty, (s8*)labeltext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    sprintf((char*)&textbuffer, "%d%%\n", (s32) (slider_007_mode_accuracy * 10.0f));

    textwidth = 0;
    textheight = 0;

    textMeasure(&textheight, &textwidth, (char*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

    textx = 285 - textwidth;
    texty = 230;

    // Render enemy accuracy percentage text.
    DL = frontPrintText(DL, &textx, &texty, (s8*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    /**
     * ENEMY REACTION
     */
    labeltext = langGet(getStringID(LTITLE, TITLE_STR_41_REACTION)); /* Enemy reaction speed */

    textx = 57;
    texty = 263;

    DL = microcode_constructor_related_to_menus(DL, 55, 280, 355, 291, 0x00000032);
    DL = microcode_constructor_related_to_menus(DL, 55, texty + 17, (s32) (slider_007_mode_reaction * 300.0f * 1) + 55, texty + 28, 0x00000064);

    if (highlight_enemy_reaction != 0)
    {
        DL = microcode_constructor_related_to_menus(DL, 55, texty - 1, 199, texty + 14, 0x00000032);
    }

    DL = frontPrintText(DL, &textx, &texty, (s8*)labeltext, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    sprintf((char*)&textbuffer, "%d%%\n", (s32) (slider_007_mode_reaction * 100.0f));

    textwidth = 0;
    textheight = 0;

    textMeasure(&textheight, &textwidth, (char*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

    textx = 285 - textwidth;
    texty = 263;

    // Render enemy reaction speed percentage text.
    DL = frontPrintText(DL, &textx, &texty, (s8*)&textbuffer, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    DL = frontAddStartTabText(DL);
    DL = frontAddPreviousTabText(DL);
    DL = frontAddNextTabText(DL);
    DL = frontDrawCursor(DL);

    return DL;
}


u32 get_player_control_style(s32 playernum)
{
  return controlstyle_player[playernum];
}


s32 check_if_mp_stage_unlocked(s32 stage)
{
    s32 players;
    if (!multi_stage_setups[stage].min_player)
    {
        return FALSE;
    }
#ifndef TARGET_N64
    /* PORT, hard-coded: every MP arena the table lists is open for any player
     * count, regardless of save progress, since a PC can draw four views of
     * anything. The table itself (max_player) is untouched, and four players on
     * a two-player arena is safe because the spawn picker is bounded by pad
     * count, not player count. */
    return TRUE;
#endif
    players=get_selected_num_players();
    if (multi_stage_setups[stage].max_player < players)
    {
        return FALSE;
    }
    if (multi_stage_setups[stage].unlock_after == -1)
    {
        return TRUE;
    }
    if ( fileIsStageUnlockedAtDifficulty(selected_folder_num, multi_stage_setups[stage].unlock_after, DIFFICULTY_AGENT))
    {
        return TRUE;
    }
    return FALSE;
}


void select_game_length(void)
{
    if (scenario != SCENARIO_TLD)
    {
        game_length = (s32) ((s32) (game_length + 1) % 7);
        return;
    }
    game_length = (s32) ((s32) (game_length + 1) % 4);
}


void copy_aim_settings_to_playerdata(void)
{
    s32 i;
    struct player_data * p;

#ifndef TARGET_N64
    {   /* PORT: Rare's four-way "Aim" row is two midas.ini keys now
         * (mp_sight_on_screen, mp_auto_aim, set on the Multiplayer Options
         * sheet). It is derived here, where a match start reads it (lv.c), so
         * it can never disagree with the ini. */
        extern s32 portMpSightAdjust(void); /* video.c; weak 3 headless */

        aim_sight_adjustment = portMpSightAdjust() & 3;
    }
#endif
    for (i = 0; i < 4; i++)
    {
        p = &g_playerPlayerData[i];
        p->autoaim = mp_sight_adjust_table[aim_sight_adjustment].autoaim;
        p->sight   = mp_sight_adjust_table[aim_sight_adjustment].sight;
#ifndef TARGET_N64
        {   /* mp_auto_aim 0 forces auto-aim off for everyone (the table above already says so);
             * 1 uses each player's own Player Options Auto-Aim, else the global option. */
            extern s32 portPlayerAutoAim(s32 player); /* options.c */

            if (aim_sight_adjustment & 2)
            {
                p->autoaim = portPlayerAutoAim(i) != 0;
            }
        }
#endif
    }
}


void advance_aim_settings_selection(void)
{
    aim_sight_adjustment = (s32) ((s32) (aim_sight_adjustment + 1) % 4);
    copy_aim_settings_to_playerdata();
}


void unlock_all_mp_chars(void) {
#ifndef TARGET_N64
    num_chars_selectable_mp = PORT_MP_CHAR_COUNT;
#else
    num_chars_selectable_mp = 0x40;
#endif
}

#ifndef TARGET_N64
/* Port names for characters that share Rare's name (NULL = Rare's name). */
static const char *const port_mp_char_names[PORT_MP_CHAR_COUNT] = {
    [2]  = "Trevelyan (Janus)\n",
    [14] = "Scientist (M)\n",       [15] = "Scientist (F)\n",
    [21] = "Civilian (F)\n",        [22] = "Civilian (M) 1\n",
    [23] = "Civilian (M) 2\n",      [24] = "Civilian (M) 3\n",
    [25] = "Siberian Guard 1\n",    [27] = "Siberian Guard 2\n",
    [31] = "Moonraker Elite (M)\n", [32] = "Moonraker Elite (F)\n",
    [64] = "Bond (Boiler Suit)\n",  [65] = "Bond (Formal)\n",
    [66] = "Bond (Jungle)\n",       [67] = "Bond (Parka)\n",
    [68] = "Trevelyan (006)\n",     [69] = "Natalya (Jungle)\n",
};

/** The multiplayer character's menu name, with "\n". */
char *portMpCharName(s32 c)
{
    if (c < 0 || c >= PORT_MP_CHAR_COUNT)
    {
        c = 0;
    }
    return (port_mp_char_names[c] != NULL) ? (char *) port_mp_char_names[c]
                                           : (char *) langGet(mp_chr_setup[c].text_preset);
}

_Static_assert(sizeof(mp_chr_setup) / sizeof(mp_chr_setup[0]) == PORT_MP_CHAR_COUNT, "PORT_MP_CHAR_COUNT");

/** How many multiplayer characters there are (port_profiles.c). */
s32 portMpCharCount(void)
{
    return PORT_MP_CHAR_COUNT;
}

/* The order the character pickers show, as mp_chr_setup indices. The cast's
 * other outfits are stored at 64..69 so saved character numbers keep their
 * meaning; here they sit after the character they belong to. The pickers'
 * cursors (mp_char_cur / prev_select_player) are positions in this list, and
 * player_char stays a character number. */
static const u8 port_mp_char_order[PORT_MP_CHAR_COUNT] = {
    0, 64, 65, 66, 67,   /* Bond, then his other outfits */
    1, 69,               /* Natalya, then her jungle fatigues */
    2, 68,               /* Trevelyan (Janus), then 006 */
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
    35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
};

/** The character shown at picker position `pos`. */
s32 portMpCharAt(s32 pos)
{
    return (pos >= 0 && pos < PORT_MP_CHAR_COUNT) ? port_mp_char_order[pos] : pos;
}

/** The picker position of character `c`. */
s32 portMpCharPos(s32 c)
{
    s32 pos;

    for (pos = 0; pos < PORT_MP_CHAR_COUNT; pos++)
    {
        if (port_mp_char_order[pos] == c)
        {
            return pos;
        }
    }
    return c;
}
#endif


s32 get_selected_num_players(void)
{
    if (gamemode == GAMEMODE_MULTI)
    {
        return selected_num_players;
    }
    return 1;
}

u16 get_player_mp_char_head(s32 player)
{
    if (player_char[player] < 0)
    {
        return mp_chr_setup[player].head;
    }
    return mp_chr_setup[player_char[player]].head;
}

u8 get_player_mp_char_gender(s32 player)
{
    if (player_char[player] < 0)
    {
        return mp_chr_setup[player].gender;
    }
    return mp_chr_setup[player_char[player]].gender;
}

u16 get_player_mp_char_body(s32 player)
{
    if (player_char[player] < 0)
    {
        return mp_chr_setup[player].body;
    }
    return mp_chr_setup[player_char[player]].body;
}

f32 get_player_mp_handicap(s32 player)
{
    return MP_handicap_table[player_handicap[player]].damage_modifier;
}

f32 get_player_mp_char_height(s32 player)
{
    if (player_char[player] < 0) {
        return mp_chr_setup[player].pov;
    }
    return mp_chr_setup[player_char[player]].pov;
}

#ifndef TARGET_N64
/* LIMITS (the MP setup's Limits screen, which replaced the Character row): the
 * time limit in minutes, the kill limit and the lives, 0 = none for each.
 * Game Length's presets write the first two. YOLT keeps Rare's two lives and
 * no limits, and flag tag has no kill limit. Demo playback uses Rare's
 * game_length, which the demo records. */
enum { PORT_LIM_TIME, PORT_LIM_KILLS, PORT_LIM_LIVES, PORT_LIM_COUNT };
static s32 port_mp_lim[PORT_LIM_COUNT] = { 10, 0, 0 };   /* Rare's default, LEN_10MIN */
extern s32 get_is_ramrom_flag(void);

/* Game Length's preset i into the time and kill limits */
static void portMpLimFromLength(s32 i)
{
    port_mp_lim[PORT_LIM_TIME] = multi_game_lengths[i].time / MINS_TO_TIMER60(1);
    port_mp_lim[PORT_LIM_KILLS] = multi_game_lengths[i].points;
}

s32 get_players_team_or_scenario_item_flag(int player);
void set_players_team_or_scenario_item_flag(int player, s32 flag);

s32 portMpTeamScenario(void)
{
    return scenario == SCENARIO_2v2 || scenario == SCENARIO_3v1 || scenario == SCENARIO_2v1;
}

/* TEAMS IN ANY SCENARIO: Game Limits' Teams row puts three or four players on
 * two sides, red (0) and blue (1), whatever the scenario - 2 v 1, 2 v 2 or
 * 3 v 1 as each player's colour falls. Rare's team scenarios are off the
 * Scenario list; they kept the sides in have_token_or_goldengun, which Flag
 * Tag and the Golden Gun overwrite during a match, so these sides have an
 * array of their own. Demos (and GE_MPSCENARIO) still play Rare's team
 * scenarios, with Rare's sides. */
static s32 port_mp_teams_on;
static s32 port_mp_team[MAX_PLAYER_COUNT] = { 0, 1, 0, 1 };

/* Flag Tag and the Golden Gun keep their holder's colours: no teams there */
static s32 portMpTeamsAllowed(void)
{
    return get_selected_num_players() >= 3 && scenario != SCENARIO_TLD && scenario != SCENARIO_MWTGG;
}

/* Teams set on Game Limits, with the players and a scenario for it */
static s32 portMpTeamsPicked(void)
{
    return port_mp_teams_on && portMpTeamsAllowed();
}

s32 portMpTeamsOn(void)
{
    return portMpTeamScenario() || (portMpTeamsPicked() && get_is_ramrom_flag() == 0);
}

s32 portMpTeamOf(s32 player)
{
    if (player < 0 || player >= MAX_PLAYER_COUNT)
    {
        return 0;
    }
    return portMpTeamScenario() ? (get_players_team_or_scenario_item_flag(player) != 0) : port_mp_team[player];
}

/* the radar's and score screen's colours: red 0 / blue 1 by team, else Flag
 * Tag's or the Golden Gun's holder as Rare's, else -1 for none */
s32 portMpColourSide(s32 player)
{
    if (portMpTeamsOn())
    {
        return portMpTeamOf(player);
    }
    if (scenario == SCENARIO_TLD || scenario == SCENARIO_MWTGG)
    {
        return g_playerPlayerData[player].have_token_or_goldengun != 0;
    }
    return -1;
}

/* A team scenario's teams live in have_token_or_goldengun, which Flag Tag and
 * the Golden Gun also write during a match and nothing resets, and B backs out
 * of Rare's team screen (portTeamsLeave) without committing. So the flags can
 * hold no teams at all (every player 0: one team, the lives check ends the
 * match at once and Friendly Fire off blocks every hit). When they don't make
 * the scenario's sides (2 v 2, or one lone player on team 1 against the rest),
 * use the team screen's own opening picks (init_menu14_mpteamsel's teamsize:
 * 2v2 = players 1 and 4 v 2 and 3; the lone player is 4, or 3 in 2v1). */
void portMpTeamsEnsure(void)
{
    const s32 n = get_selected_num_players();
    const s32 lone = (scenario == SCENARIO_2v1) ? 2 : 3;
    s32 count[2] = { 0, 0 };
    s32 i;

    if (!portMpTeamScenario())
    {   /* Game Limits' sides: each side needs a player, else the first split */
        for (i = 0; i < n; i++)
        {
            count[port_mp_team[i] != 0]++;
        }
        if (portMpTeamsPicked() && (count[0] == 0 || count[1] == 0))
        {
            for (i = 0; i < MAX_PLAYER_COUNT; i++)
            {
                port_mp_team[i] = i & 1;
            }
        }
        return;
    }
    for (i = 0; i < n; i++)
    {
        const s32 t = get_players_team_or_scenario_item_flag(i);

        if (t != 0 && t != 1)
        {
            count[0] = -1;
            break;
        }
        count[t]++;
    }
    if (count[0] >= 0 && count[1] == ((scenario == SCENARIO_2v2) ? 2 : 1) && count[0] == n - count[1])
    {
        return;   /* already the scenario's sides */
    }
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {   /* the team screen's commit rule (interface_menu14_mpteams) */
        set_players_team_or_scenario_item_flag(i, (scenario == SCENARIO_2v2) ? !(i == 0 || i == lone) : (i == lone));
    }
}

s32 portMpLives(void)
{
    if (scenario == SCENARIO_YOLT)
    {
        return 2;
    }
    return (get_is_ramrom_flag() == 0) ? port_mp_lim[PORT_LIM_LIVES] : 0;
}
#endif

s32 get_mp_timelimit(void) {
#ifndef TARGET_N64
  if (get_is_ramrom_flag() == 0)
  {
      const s32 mins = (scenario == SCENARIO_YOLT) ? 0 : port_mp_lim[PORT_LIM_TIME];

      return MINS_TO_TIMER60(mins);
  }
#endif
  return multi_game_lengths[game_length].time;
}

s32 get_mp_pointlimit(void) {
#ifndef TARGET_N64
  if (get_is_ramrom_flag() == 0)
  {
      return (scenario == SCENARIO_YOLT || scenario == SCENARIO_TLD) ? 0 : port_mp_lim[PORT_LIM_KILLS];
  }
#endif
  return multi_game_lengths[game_length].points;
}

#ifndef TARGET_N64
extern s32 portMpWeaponIsPortSet(void);   /* mp_weapon.c: custom / random / random set, 14 and up */
#define portMpWeaponIsCustomSet portMpWeaponIsPortSet
#endif

void reset_mp_options_for_scenario(MPSCENARIOS scenarioid)
{
#ifndef TARGET_N64
    const s32 oldlength = game_length;
#endif
    scenario = scenarioid;

    unlock_stage_select = 1;
    unlock_game_length = 1;
    unlock_chars = 1;
    unlock_weapon_select = 1;
    unlock_handicap = 1;
    unlock_control_style = 1;
    unlock_aim_sight = 1;


    switch(scenario)
    {
    case SCENARIO_NORMAL:
    case SCENARIO_2v2:
    case SCENARIO_3v1:
    case SCENARIO_2v1:
        if (6 < game_length)
        {
            game_length = 2;
        }
        if (0xd < getMPWeaponSet()
#ifndef TARGET_N64
            && !portMpWeaponIsCustomSet()   /* a custom set is index 14, not out of range */
#endif
           )
        {
            setMPWeaponSet(0xd);
        }
        break;
    case SCENARIO_YOLT:
        game_length = 7;

        if (0xd < getMPWeaponSet()
#ifndef TARGET_N64
            && !portMpWeaponIsCustomSet()   /* a custom set is index 14, not out of range */
#endif
           )
        {
            setMPWeaponSet(0xd);
        }
        unlock_game_length = 0;
        break;
    case SCENARIO_TLD:
        if (3 < game_length)
        {
            game_length = 2;
        }
        if (0xd < getMPWeaponSet()
#ifndef TARGET_N64
            && !portMpWeaponIsCustomSet()   /* a custom set is index 14, not out of range */
#endif
           )
        {
            setMPWeaponSet(0xd);
        }
        break;
    case SCENARIO_MWTGG:
        if (6 < game_length)
        {
            game_length = 2;
        }
        setMPWeaponSet(0xd);
        unlock_weapon_select = 0;
        break;
    case SCENARIO_LTK:
        if (6 < game_length)
        {
            game_length = 2;
        }
        setMPWeaponSet(1);
        unlock_handicap = 0;
    }
#ifndef TARGET_N64
    if (game_length != oldlength)
    {   /* the scenario moved Game Length: the limits follow it */
        portMpLimFromLength(game_length);
    }
    portMpTeamsEnsure();   /* a team scenario starts with real sides */
#endif
    return;
}




// Address 0x7F010608 NTSC
void init_mp_options_for_scenario(s32 numplayers)
{
    s32 i;

    if (numplayers < 2)
    {
        numplayers = 2;
    }

    selected_num_players = numplayers;

    for (i=0; i < selected_num_players; i++)
    {
        if (player_char[i] == -1)
        {
            player_char[i] = i;
#ifndef TARGET_N64
            {   /* a seat with a profile starts on the profile's character, not the default lineup */
                extern s32 portProfileCharacter(s32 player);
                const s32 c = portProfileCharacter(i);

                if (c >= 0)
                {
                    extern const char *portProfileActive(s32 player);

                    player_char[i] = c;
                    printf("ge: profiles: player %d starts as character %d (%s)\n", (int) i + 1, (int) c, portProfileActive(i));
                }
            }
#endif
            player_handicap[i] = 5;
        }

        if ((numplayers >= 3) && (controlstyle_player[i] >= CONTROLLER_CONFIG_PLENTY)
#ifndef TARGET_N64
            /* twin stick (joy_port.c): a 2.x style needs no second pad slot
             * for a player whose own controller has a right stick */
            && !portJoyTwinStickCapable(i)
#endif
            )
        {
            controlstyle_player[i] = CONTROLLER_CONFIG_HONEY;
        }
    }

    if ((mp_player_counts[scenario].max < selected_num_players) || (selected_num_players < mp_player_counts[scenario].min))
    {
        reset_mp_options_for_scenario(SCENARIO_NORMAL);

    }

#ifdef TARGET_N64 /* PORT: no arena has a player cap - see check_if_mp_stage_unlocked */
    if (multi_stage_setups[MP_stage_selected].max_player < selected_num_players)
    {
        MP_stage_selected = MP_STAGE_TEMPLE;
    }
#endif
}


//********************************************************************************************************
//MULTIPLAYER OPTIONS
//********************************************************************************************************
void init_menu0E_mpoptions(void)
{
    int numplayers;

    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;

    if (maybe_is_in_menu)
    {
#ifdef TARGET_N64
        sndApplyVolumeAllSfxSlot(0x7fff);
        musicTrack1ApplySeqpVol(0x7fff);
#else
        /* GLOBAL VOLUMES: vanilla forced both to full here, so the menus
         * ignored the player's setting - use the stored pair instead (options.c
         * portApplyGlobalVolumes). */
        portApplyGlobalVolumesHere();
#endif
        g_musicXTrack1Fade = MUSIC_FADESTATE_UNSET;
        musicTrack1Play(M_FOLDERS);
        maybe_is_in_menu = FALSE;
    }

    if (selected_num_players == 0)
    {
        numplayers = joyGetControllerCount();
        init_mp_options_for_scenario(numplayers);
    }

    if (check_if_mp_stage_unlocked(MP_stage_selected) == FALSE)
    {
        MP_stage_selected = MP_STAGE_TEMPLE;
    }

    highlight_players = FALSE;
    highlight_scenario = FALSE;
    highlight_gameselect = FALSE;
    highlight_gamelength = FALSE;
    highlight_character = FALSE;
    highlight_weaponselect = FALSE;
    highlight_health = FALSE;
    highlight_controlstyle = FALSE;
    highlight_aimadjustment = FALSE;
    load_walletbond();
}

void update_menu0E_mpoptions(void)
{
    return;
}





#ifndef TARGET_N64
/* menu 13's pick mode - the Game Length / Weapons lists (see portMpPickOpen) */
enum { PORT_MP_PICK_SCENARIO, PORT_MP_PICK_LENGTH, PORT_MP_PICK_WEAPONS,
       PORT_MP_PICK_CUSTOM, PORT_MP_PICK_EDIT,     /* the Weapons list's CUSTOM tab (portWset*) */
       PORT_MP_PICK_LIMITS,                        /* the Game Limits screen (portLim*) */
       PORT_MP_PICK_SETTINGS };                    /* Load / Save Settings (portMpSet*) */
#define PORT_SCEN_LISTED SCENARIO_2v2   /* Scenario's list: Rare's first five (teams are on Game Limits) */
static s32 port_mp_pick = PORT_MP_PICK_SCENARIO;
static s32 port_mp_pick_hover = -1;
static s32 port_highlight_mpsettings;   /* the setup's bottom row, Load / Save Settings */
static void portMpPickOpen(s32 kind);
static u8 *portWsetRowText(void);   /* the CUSTOM tab's screens, beside the pick list */
static u8 *portMpLengthText(void);
static void portMpSetOpen(void);
#endif

// Address 0x7F010848 NTSC
void interface_menu0E_mpoptions(void)
{
    s32 players_selected = 0;
    s32 scenario_selected = 0;
    s32 gameselect_selected = 0;
    s32 gamelength_selected = 0;
    s32 character_selected = 0;
    s32 weapon_selected = 0;
    s32 health_selected = 0;
    s32 controlstyle_selected = 0;
    s32 aimadjust_selected = 0;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    if (joyGetControllerCount() < 2)
    {
        frontChangeMenu(MENU_MODE_SELECT, 0);
        setCursorPOSforMode(gamemode);
    }

    if (joyGetControllerCount() < selected_num_players)
    {
        init_mp_options_for_scenario(joyGetControllerCount());
    }

    if (joyGetButtons(PLAYER_1, Z_TRIG|A_BUTTON) == 0)
    {
        tab_prev_highlight = 0;
        tab_next_highlight = 0;
        tab_start_highlight = 0;
        highlight_players = 0;
        highlight_scenario = 0;
        highlight_gameselect = 0;
        highlight_gamelength = 0;
        highlight_character = 0;
        highlight_weaponselect = 0;
        highlight_health = 0;
        highlight_controlstyle = 0;
        highlight_aimadjustment = 0;
#ifndef TARGET_N64
        port_highlight_mpsettings = 0;
#endif

        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = 1;
        }
        else if (frontCheckCursorOnStartTab())
        {
            tab_start_highlight = 1;
        }
        else
        {
            s32 temp_f6 = cursor_v_pos;
#ifndef TARGET_N64
            /* PORT: Limits moved onto Game Length, so Health, Player Options
             * and Gameplay Options sit one row higher and LOAD / SAVE SETTINGS
             * takes the bottom row (0x119) */
            if (temp_f6 >= 0x119)
            {
                port_highlight_mpsettings = 1;
            }
            else if ((temp_f6 >= 0x105) && (unlock_aim_sight))
            {
                highlight_aimadjustment = 1;
            }
            else if ((temp_f6 >= 0xF1) && (unlock_control_style))
            {
                highlight_controlstyle = 1;
            }
            else if ((temp_f6 >= 0xDD) && (unlock_handicap))
            {
                highlight_health = 1;
            }
#else
            if ((temp_f6 >= 0x119) && (unlock_aim_sight))
            {
                highlight_aimadjustment = 1;
            }
            else if ((temp_f6 >= 0x105) && (unlock_control_style))
            {
                highlight_controlstyle = 1;
            }
            else if ((temp_f6 >= 0xF1) && (unlock_handicap))
            {
                highlight_health = 1;
            }
            else if ((temp_f6 >= 0xDD) && (unlock_chars))
            {
                highlight_character = 1;
            }
#endif
            else if ((temp_f6 >= 0xC9) && (unlock_weapon_select))
            {
                highlight_weaponselect = 1;
            }
            else if ((temp_f6 >= 0xB5) && (unlock_game_length))
            {
                highlight_gamelength = 1;
            }
            else if ((temp_f6 >= 0xA1) && (unlock_stage_select))
            {
                highlight_gameselect = 1;
            }
            else if (temp_f6 >= 0x8D)
            {
                highlight_scenario = 1;
            }
            else
            {
                highlight_players = 1;
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON))
    {
        tab_start_selected = 1;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON))
    {
        if (tab_prev_highlight)
        {
            tab_prev_selected = 1;
        }

        if (tab_start_highlight)
        {
            tab_start_selected = 1;
        }
        else if (highlight_players)
        {
            players_selected = 1;
        }
        else if (highlight_scenario)
        {
            scenario_selected = 1;
        }
        else if (highlight_gameselect)
        {
            gameselect_selected = 1;
        }
        else if (highlight_gamelength)
        {
            gamelength_selected = 1;
        }
        else if (highlight_character)
        {
            character_selected = 1;
        }
        else if (highlight_weaponselect)
        {
            weapon_selected = 1;
        }
        else if (highlight_health)
        {
            health_selected = 1;
        }
        else if (highlight_controlstyle)
        {
            controlstyle_selected = 1;
        }
        else if (highlight_aimadjustment)
        {
            aimadjust_selected = 1;
        }
#ifndef TARGET_N64
        else if (port_highlight_mpsettings)
        {
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            portMpSetOpen();
            return;
        }
#endif

        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = 1;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CONFIDENTIAL2, 1);
    frontUpdateControlStickPosition();

    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MODE_SELECT, 0);
        setCursorPOSforMode(gamemode);
        return;
    }

    if (tab_start_selected)
    {
#ifndef TARGET_N64
        portMpTeamsEnsure();   /* the flags may have changed since the scenario was picked */
#endif
        if (multi_stage_setups[MP_stage_selected].stage_id < 0)
        {
            s32 temp_hi;
            do
            {
                temp_hi = randomGetNext() % (u32)(MP_STAGE_SELECTED_MAX - 1);
                selected_stage = multi_stage_setups[1 + temp_hi].stage_id;
            } while (check_if_mp_stage_unlocked(temp_hi + 1) == 0);
        }
        else
        {
            selected_stage = multi_stage_setups[MP_stage_selected].stage_id;
        }

        briefingpage = -1;
        frontChangeMenu(MENU_RUN_STAGE, 1);

        return;
    }

    if (players_selected)
    {
        s32 tmpNumPlayers;

        if (joyGetControllerCount() < selected_num_players + 1)
        {
            tmpNumPlayers = 2;
        }
        else
        {
            tmpNumPlayers = selected_num_players + 1;
        }

        init_mp_options_for_scenario(tmpNumPlayers);

        return;
    }
    if (scenario_selected)
    {
        frontChangeMenu(MENU_MP_SCENARIO_SELECT, 0);
        return;
    }
    if (gameselect_selected)
    {
        frontChangeMenu(MENU_MP_STAGE_SELECT, 0);
        return;
    }
    if (gamelength_selected)
    {
#ifndef TARGET_N64
        portMpPickOpen(PORT_MP_PICK_LIMITS);   /* Game Limits: time, kills, lives, teams (no presets) */
#else
        select_game_length();
#endif
        return;
    }
    if (character_selected)
    {
#ifndef TARGET_N64
        portMpPickOpen(PORT_MP_PICK_LIMITS);   /* LIMITS: characters are on Player Options now */
#else
        frontChangeMenu(MENU_MP_CHAR_SELECT, 0);
#endif
        return;
    }
    if (weapon_selected)
    {
#ifndef TARGET_N64
        portMpPickOpen(PORT_MP_PICK_WEAPONS);  /* a list, like Scenario */
#else
        incrementMPWeaponSet();
#endif
        return;
    }
    if (health_selected)
    {
        frontChangeMenu(MENU_MP_HANDICAP, 0);
        return;
    }
    if (controlstyle_selected)
    {
        frontChangeMenu(MENU_MP_CONTROL_STYLE, 0);
        return;
    }
    if (aimadjust_selected)
    {
#ifdef TARGET_N64
        advance_aim_settings_selection();
#else
        /* PORT: this row is "Multiplayer Options" - a sheet of the settings
         * multiplayer keeps apart from solo. Rare's four-way Aim value it used
         * to step is its first two rows. */
        port_sheet_kind = PORT_SHEET_MPOPTS;
        frontChangeMenu(MENU_PORT_SHEET, FALSE);
#endif
    }
}


Gfx * constructor_menu0E_mpoptions(Gfx *DL)
{
    u8 *text;

    char acStack12 [8];

    s32 x;
    s32 y;

    int iStack24;
    int iStack28;
    u32 entry;


  DL = viSetFillColor(DL,0,0,0);
  DL = viFillScreen(DL);
  #if defined(BUGFIX_R2)
  DL = viFillScreen(DL);
  DL = viFillScreen(DL);
  #endif
  DL = frontSetupMenuBackground(DL);
  DL = microcode_constructor(DL);
  text = langGet(getStringID(LTITLE, TITLE_STR_76_MPOPTIONS));
  x = 0x37;
  y = 0x5f;
  DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xff, viGetX(), viGetY(), 0, 0);

  text = langGet(getStringID(LTITLE, TITLE_STR_77_PLAYERS));
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0x79;
  if (highlight_players) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0x78,iStack28 + 0x3c,0x87,0x32);
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,0xff,viGetX(),viGetY(),0,0);

  text = langGet(getStringID(LTITLE, TITLE_STR_78_SCENARIO));
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0x8d;
  if (highlight_scenario) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0x8c,iStack28 + 0x3c,0x9b,0x32);
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,0xff,viGetX(),viGetY(),0,0);

  text = langGet(getStringID(LTITLE, TITLE_STR_79_LEVEL));
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0xa1;
  if (highlight_gameselect) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0xa0,iStack28 + 0x3c,0xaf,0x32);
  }
  if (unlock_stage_select) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

#ifndef TARGET_N64
  text = (u8 *) "Game Limits:\n";   /* opens Game Limits: time, kills, lives, teams */
#else
  text = langGet(getStringID(LTITLE, TITLE_STR_80_GAMELENGTH));
#endif
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0xb5;
  if (highlight_gamelength) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0xb4,iStack28 + 0x3c,0xc3,0x32);
  }
  if (unlock_game_length) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

  text = langGet(getStringID(LTITLE, TITLE_STR_81_WEAPONS));
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0xc9;
  if (highlight_weaponselect) {
    DL = microcode_constructor_related_to_menus(DL,0x37,200,iStack28 + 0x3c,0xd7,0x32);
  }
  if (unlock_weapon_select) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

#ifdef TARGET_N64 /* PORT: no Character row - characters are on Player Options, Limits on Game Length */
  text = langGet(getStringID(LTITLE, TITLE_STR_82_CHARACTER));
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0xdd;
  if (highlight_character) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0xdc,iStack28 + 0x3c,0xeb,0x32);
  }
  if (unlock_chars) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);
#endif

#ifndef TARGET_N64
#define PORT_MPROW_UP 0x14   /* Health, Player Options and Gameplay Options: one row up */
#else
#define PORT_MPROW_UP 0
#endif
  text = langGet(getStringID(LTITLE, TITLE_STR_83_HEALTH));
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0xf1 - PORT_MPROW_UP;
  if (highlight_health) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0xf0 - PORT_MPROW_UP,iStack28 + 0x3c,0xff - PORT_MPROW_UP,0x32);
  }
  if (unlock_handicap) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

#ifdef TARGET_N64
  text = langGet(getStringID(LTITLE, TITLE_STR_286_CONTROLSTYLE));
#else
  /* PORT: "Control Style" is PLAYER OPTIONS - the same
   * per-player split screen, holding each player's controller, profile and
   * control settings (menu11). The row under it, "Aim", is MULTIPLAYER
   * OPTIONS - a sheet, so it shows no value beside it. */
  text = (u8 *) "Player Options\n";
#endif
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0x105 - PORT_MPROW_UP;
  if (highlight_controlstyle) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0x104 - PORT_MPROW_UP,iStack28 + 0x3c,0x113 - PORT_MPROW_UP,0x32);
  }
  if (unlock_control_style) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

#ifdef TARGET_N64
  text = langGet(getStringID(LTITLE, TITLE_STR_84_AIM));
#else
  text = (u8 *) "Gameplay Options\n";   /* formerly "Multiplayer Options" */
#endif
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0x119 - PORT_MPROW_UP;
  if (highlight_aimadjustment) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0x118 - PORT_MPROW_UP,iStack28 + 0x3c,0x127 - PORT_MPROW_UP,0x32);
  }
  if (unlock_aim_sight) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

#ifndef TARGET_N64
  text = (u8 *) "Load / Save Settings\n";   /* where Gameplay Options was */
  textMeasure(&iStack24,&iStack28,text,ptrFontZurichBoldChars,ptrFontZurichBold,0);
  x = 0x39;
  y = 0x119;
  if (port_highlight_mpsettings) {
    DL = microcode_constructor_related_to_menus(DL,0x37,0x118,iStack28 + 0x3c,0x127,0x32);
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,0xFF,viGetX(),viGetY(),0,0);
#endif
#undef PORT_MPROW_UP

  sprintf(acStack12,"%d",selected_num_players);
  x = 0xa0;
  y = 0x79;
  DL = frontPrintText(DL, &x, &y, acStack12, ptrFontZurichBoldChars, ptrFontZurichBold, 0xff, viGetX(), viGetY(), 0, 0);

  text = langGet(mp_player_counts[scenario].stage);
  x = 0xa0;
  y = 0x8d;
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,0xff,viGetX(),viGetY(),0,0);

  text = langGet(multi_stage_setups[MP_stage_selected].folder_text_preset);
  x = 0xa0;
  y = 0xa1;
  if (unlock_stage_select) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

#ifndef TARGET_N64
  text = portMpLengthText();   /* the limits in force, by preset name where one fits */
#else
  text = langGet(multi_game_lengths[game_length].text_preset);
#endif
  x = 0xa0;
  y = 0xb5;
  if (unlock_game_length) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);


  text = langGet(*(getPtrMPWeaponSetTextID()));
#ifndef TARGET_N64
  {
      extern s32 portMpWeaponIsCustom(void);

      if (getMPWeaponSet() >= 14)
      {
          text = portWsetRowText();   /* a port set's own name: the custom set's, Random, Random Set */
      }
  }
#endif
  x = 0xa0;
  y = 0xc9;
  if (unlock_weapon_select) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);

#ifdef TARGET_N64 /* PORT: the row opens the Multiplayer Options sheet - no value beside it */
  text = langGet(mp_sight_adjust_table[aim_sight_adjustment].anonymous_0);
  x = 0xa0;
  y = 0x119;
  if (unlock_aim_sight) {
    entry = 0xFF;
  }
  else {
    entry = 0x70;
  }
  DL = frontPrintText(DL,&x,&y,text,ptrFontZurichBoldChars,ptrFontZurichBold,entry,viGetX(),viGetY(),0,0);
#endif
  DL = frontAddPreviousTabText(DL);
  DL = frontAddStartTabText(DL);
  DL = frontDrawCursor(DL);
  return DL;
}


bool get_players_who_have_selected_mp_char(s32 character)
{
    s32 i;
    bool char_is_selected;

#ifndef TARGET_N64
    /* SAME CHARACTER ALLOWED: several MP players may pick the same character.
     * Every caller asks this to keep characters unique -
     * the select gate (interface_menu0F), the dimmed portrait and the init
     * reassign - so "never taken" lifts the rule in one place. */
    (void) character;
    return FALSE;
#endif
    char_is_selected = FALSE;

    for (i = 0; i < selected_num_players; i++)
    {
        if (player_has_selected_char[i] && character == player_char[i])
        {
            char_is_selected = TRUE;
        }
    }

    return char_is_selected;
}

// Address 0x7F011C3C NTSC
void init_menu0f_mpcharsel(void)
{
    s32 i;

    tab_start_selected = 0;
    tab_next_selected = 0;
    tab_prev_selected = 0;
    tab_prev_highlight = 0;
    tab_next_highlight = 0;
    tab_start_highlight = 0;

#ifndef TARGET_N64
    for ( i = 0; i < PORT_MP_CHAR_COUNT; i += 1)
#else
    for ( i = 0; i < 64; i += 1)
#endif
    {
        s32 selected_photo = mp_chr_setup[i].select_photo;

        // Multiplayer character images are composed of 4 textures.

        // upper left
        if (mpcharselimages[selected_photo * 4 + 0].index < (u32)(MAX_TEXTURES - 1))
        {
            texLoadFromTextureNum(mpcharselimages[selected_photo * 4 + 0].index, 0);
        }

        // upper right
        if (mpcharselimages[selected_photo * 4 + 1].index < (u32)(MAX_TEXTURES - 1))
        {
            texLoadFromTextureNum(mpcharselimages[selected_photo * 4 + 1].index, 0);
        }

        // lower left
        if (mpcharselimages[selected_photo * 4 + 2].index < (u32)(MAX_TEXTURES - 1))
        {
            texLoadFromTextureNum(mpcharselimages[selected_photo * 4 + 2].index, 0);
        }

        // lower right
        if (mpcharselimages[selected_photo * 4 + 3].index < (u32)(MAX_TEXTURES - 1))
        {
            texLoadFromTextureNum(mpcharselimages[selected_photo * 4 + 3].index, 0);
        }
    }

#ifndef TARGET_N64
    if (num_chars_selectable_mp != PORT_MP_CHAR_COUNT)
#else
    if (num_chars_selectable_mp != 0x40)
#endif
    {
        if (fileIsStageUnlockedAtDifficulty(selected_folder_num, SP_LEVEL_CRADLE, DIFFICULTY_AGENT) == STAGESTATUS_COMPLETED)
        {
            num_chars_selectable_mp = 0x21;
        }
        else
        {
            num_chars_selectable_mp = 8;

            for ( i = 0; i < MAX_PLAYER_COUNT; i++)
            {
                if (player_char[i] >= num_chars_selectable_mp)
                {
                    s32 count = 0;

                    while (count < num_chars_selectable_mp)
                    {
                        if (get_players_who_have_selected_mp_char(count) != 0)
                        {
                            count++;
                            continue;
                        }

                        break;
                    }

                    player_char[i] = count;
                }
            }
        }
    }

    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        mp_char_cur_select_player[i] = portMpCharPos(player_char[i]);   /* positions in the shown order */
        mp_char_prev_select_player[i] = portMpCharPos(player_char[i]);
        mp_char_select_scroll_offset[i] = 0;
        player_has_selected_char[i] = 0;
        size_mp_select_image_player[i] = 0;
    }

    load_walletbond();
}



//********************************************************************************************************
//MULTIPLAYER CHARACTER SELECT
//********************************************************************************************************

void update_menu0F_mpcharsel(void) 
{
    return;
}


#ifndef TARGET_N64
/* The stick-held step waits for the slide to settle (prev == cur), not just for
 * its offset to be 0, since a keyboard A / D is both a C-button edge and a full
 * stick push and would otherwise step two portraits a tap; held-stick pacing is
 * unchanged. */
#define MPCHARSEL_SLIDE_IDLE(i) (mp_char_prev_select_player[i] == mp_char_cur_select_player[i])
#else
#define MPCHARSEL_SLIDE_IDLE(i) 1
#endif

// Address 0x7F011ED4 NTSC
void interface_menu0F_mpcharsel(void)
{
    s32 numplayers;
    s32 padding;
    s32 ready_players;
    s32 padding2;
    s32 i;
    s32 temp_v1_3;
    s32 step30 = 1;   /* 1 = the held / animated steps may advance this call */

    numplayers = get_selected_num_players();
    ready_players = 0;
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

#ifndef TARGET_N64
    { /* Run this screen's scroll slide, portrait grow / shrink and stick-held
       * step at 60 Hz whatever the tick rate (g_ClockTimer fields, a stall
       * still counts as one step). Edge presses (D-pad, C, A, B) are not gated,
       * so a press is never lost. */
        step30 = (g_ClockTimer > 0);
    }
#endif

    for (i = 0; i < numplayers; i++)
    {
        if (player_has_selected_char[i])
        {
            if (step30 && (size_mp_select_image_player[i] < 0xb) && (mp_char_prev_select_player[i] == mp_char_cur_select_player[i]))
            {
                size_mp_select_image_player[i] = size_mp_select_image_player[i] + 1;
            }

            if (joyGetButtonsPressedThisFrame(i, B_BUTTON))
            {
                player_has_selected_char[i] = 0;
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }
        }

        if (player_has_selected_char[i] == 0 && size_mp_select_image_player[i] > 0)
        {
            if (step30)
            {
                size_mp_select_image_player[i] = size_mp_select_image_player[i] - 1;
            }
        }
        else if (player_has_selected_char[i] == 0)
        {
            if (joyGetButtonsPressedThisFrame(i, L_JPAD|L_CBUTTONS)) // 0x202
            {
                if (mp_char_cur_select_player[i] > 0)
                {
                    mp_char_cur_select_player[i]--;
                }
            }
            else if (joyGetButtonsPressedThisFrame(i, R_JPAD|R_CBUTTONS)) // 0x101
            {
                if (mp_char_cur_select_player[i] < (num_chars_selectable_mp - 1))
                {
                    mp_char_cur_select_player[i]++;
                }
            }
            else if (joyGetButtonsPressedThisFrame(i, A_BUTTON|Z_TRIG|START_BUTTON)) // 0xb000
            {
                if (get_players_who_have_selected_mp_char(portMpCharAt(mp_char_cur_select_player[i])) == 0)
                {
                    player_char[i] = portMpCharAt(mp_char_cur_select_player[i]);
                    size_mp_select_image_player[i] = 1;
                    player_has_selected_char[i] = 1;

                    sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
                }
            }
            else if (joyGetStickXInRange(i, -2, 1) > 0)
            {
                if (step30 && mp_char_select_scroll_offset[i] == 0 && MPCHARSEL_SLIDE_IDLE(i))
                {
                    if (mp_char_cur_select_player[i] < (num_chars_selectable_mp - 1))
                    {
                        mp_char_cur_select_player[i]++;
                    }
                }
            }
            else if (step30 && (joyGetStickXInRange(i, -2, 1) < -1) && (mp_char_select_scroll_offset[i] == 0) && MPCHARSEL_SLIDE_IDLE(i))
            {
                if (mp_char_cur_select_player[i] > 0)
                {
                    mp_char_cur_select_player[i]--;
                }
            }
        }

        temp_v1_3 = (mp_char_prev_select_player[i] * 0x54) + mp_char_select_scroll_offset[i];

        if (!step30)
        {
            /* between 30 Hz steps: the slide holds where it is */
        }
        else if (mp_char_cur_select_player[i] * 0x54 < temp_v1_3)
        {
            mp_char_select_scroll_offset[i] -= 0xc;
            if (mp_char_select_scroll_offset[i] < 0)
            {
                mp_char_select_scroll_offset[i] += 0x54;
                mp_char_prev_select_player[i]--;
            }
        }
        else if (temp_v1_3 < mp_char_cur_select_player[i] * 0x54)
        {
            mp_char_select_scroll_offset[i] += 0xC;
            if (mp_char_select_scroll_offset[i] >= 0x54)
            {
                mp_char_select_scroll_offset[i] -= 0x54;
                mp_char_prev_select_player[i]++;
            }
        }

        if (player_has_selected_char[i] != 0 && size_mp_select_image_player[i] == 0xb)
        {
            ready_players++;
        }
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    frontUpdateControlStickPosition();

#ifndef TARGET_N64
    if (portMpWaitScreenExitPressed(numplayers))
    {
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }
    else
#endif
    if (ready_players == numplayers)
    {
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }
}



s32 frontCalculateCharacterImageAlpha(s32 arg0, s32 arg1, s32 arg2)
{
    if ((arg1 >= arg0) || (arg0 >= arg2))
    {
        return 0;
    }

    if (arg0 < (arg1 + 0x28))
    {
        return 0xF - ((arg1 - arg0) * 6);
    }

    if ((arg2 - 0x28) < arg0)
    {
        return 0xF - ((arg0 - arg2) * 6);
    }

    return 0xFF;
}


// Address 0x7F01231C NTSC
Gfx *frontRenderCharacterPortrait(Gfx *DL, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6)
{
    Mtx* spD4;
    Mtx* spD0;
    Vertex* spCC;
    s32 padding[26];
    u32 var_s2;
    u32 var_s3;
    s32 sp58;
    s32 sp54;
    s32 sp50;
    s32 var_v0;
    s32 selected_photo;
    s32 vtx_index;
    struct MP_selectable_chars *mpc;
    struct sImageTableEntry *simage;
    s32 j;
    s32 i;

    spD4 = dynAllocateMatrix();
    spD0 = dynAllocateMatrix();
    spCC = dynAllocateVertices(16);
    DL = microcode_constructor(DL);

    guOrtho(spD4, 0.0f, 440.0f, 0.0f, 330.0f, 1.0f, 10.0f, 1.0f);
    guRotate(spD0, 3.1415927f, 1.0f, 0.0f, 0.0f);

    vtx_index = 0;

    for (i = 0; i < 4; i++)
    {
        sp58 = arg6 + 0x23;
        sp54 = 0x14A - arg4;
        sp50 = arg6 + 0x2a;

        for (j = 0; j < 4; j++)
        {
            var_s3 = (j >= 2) ? j - 1 : j;
            var_s2 = (i >= 2) ? i - 1 : i;

            if ((get_players_who_have_selected_mp_char(arg5)) && (arg6 == 0))
            {
                var_v0 = 0x6E;
            }
            else
            {
                var_v0 = 0xFF;
            }

            spCC[vtx_index].coord.AsArray[0] = ((var_s3 - 1) * sp58) + arg3;
            spCC[vtx_index].coord.AsArray[1] = sp54 + ((var_s2 - 1) * sp50) + arg6;
            spCC[vtx_index].coord.AsArray[2] = -5;
            spCC[vtx_index].index = 0;

            if (j & 1)
            {
                spCC[vtx_index].s = 0x1000;
            }
            else
            {
                spCC[vtx_index].s = 0;
            }

            if (i & 1)
            {
                spCC[vtx_index].t = 0x1000;
            }
            else
            {
                spCC[vtx_index].t = 0;
            }

            spCC[vtx_index].r = var_v0;
            spCC[vtx_index].g = var_v0;
            spCC[vtx_index].b = var_v0;

            if (arg6 == 0)
            {
                spCC[vtx_index].a = frontCalculateCharacterImageAlpha(spCC[vtx_index].coord.AsArray[0], arg1, arg2);
            }
            else
            {
                spCC[vtx_index].a = 0xFF;
            }

            vtx_index++;
        }
    }

    gSPMatrix(DL++, OS_PHYSICAL_TO_K0(spD4), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(DL++, OS_PHYSICAL_TO_K0(spD0), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    gSPVertex(DL++, spCC, 16, 0);

    selected_photo = mp_chr_setup[arg5].select_photo * 4;
    simage = &mpcharselimages[mp_chr_setup[arg5].select_photo * 4];
    texSelect(&DL, simage + 2, 2, 0, 2);
    gSP1Triangle(DL++, 0, 1, 4, 0);
    gSP1Triangle(DL++, 4, 1, 5, 0);

    selected_photo = mp_chr_setup[arg5].select_photo * 4;
    simage = &mpcharselimages[mp_chr_setup[arg5].select_photo * 4];
    texSelect(&DL, simage + 3, 2, 0, 2);
    gSP1Triangle(DL++, 2, 3, 6, 0);
    gSP1Triangle(DL++, 6, 3, 7, 0);

    selected_photo = mp_chr_setup[arg5].select_photo * 4;
    simage = &mpcharselimages[mp_chr_setup[arg5].select_photo * 4];
    texSelect(&DL, simage + 0, 2, 0, 2);
    gSP1Triangle(DL++, 8, 9, 12, 0);
    gSP1Triangle(DL++, 12, 9, 13, 0);

    selected_photo = mp_chr_setup[arg5].select_photo * 4;
    simage = &mpcharselimages[mp_chr_setup[arg5].select_photo * 4];
    texSelect(&DL, simage + 1, 2, 0, 2);
    gSP1Triangle(DL++, 10, 11, 14, 0);
    gSP1Triangle(DL++, 14, 11, 15, 0);

    return DL;
}


Gfx *constructor_menu0F_mpcharsel(Gfx *DL)
{
    s32 numplayers;
    s32 playernum;
    u8 *text;
    s32 panel_left;  // X position where the portraits completely fade out on the left.
    s32 panel_top;   
    s32 panel_width; // Width of the character portrait panel (1 panel per player)
    s32 grid_offset; // Distance between the portrait panels
    s32 stackpad;
    s32 stackpad2;
    s32 portrait_y_base;
    s32 select_char_height;
    s32 select_char_width;
    s32 select_char_x;
    s32 select_char_y;
    s32 panel_center_x;
    s32 name_height;
    s32 name_width;
    s32 name_x;
    s32 name_y;

    numplayers = get_selected_num_players();
    
    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);
    DL = microcode_constructor_related_to_menus(DL, 0x26, 0xA9, 0x184, 0xAB, 0x90);

    if (numplayers >= 3)
    {
        DL = microcode_constructor_related_to_menus(DL, 0xD4, 0x1E, 0xD6, 0x136, 0x80);
    }

    for (playernum = 0; playernum < numplayers; playernum++)
    {
        panel_top = 0;

        if (numplayers == 2)
        {
            panel_left = 0x26;
            panel_width = 0x15E;
            grid_offset = (playernum > 0) ? (0x8C) : (0);
            panel_top = grid_offset + 0x1E;
        }
        else
        {
            panel_width = 0xAF;
            grid_offset = (playernum >= 2) ? (0x8C) : (0);
            panel_top = grid_offset + 0x1E;
            grid_offset = (playernum & 1) ? (0xAF) : (0);
            panel_left = grid_offset + 0x26;
        }
        
        gDPSetScissor(DL++, G_SC_NON_INTERLACE, (panel_left + 6), panel_top + 5, (panel_left + panel_width - 6), (panel_top + 0x8C - 5));

        panel_center_x = (panel_width >> 1) + panel_left;

        if ((player_has_selected_char[playernum] == 0) && (size_mp_select_image_player[playernum] == 0))
        {
            text = langGet(getStringID(LTITLE, TITLE_STR_85_SELECTCHARACTER));
            textMeasure(&select_char_height, &select_char_width, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
            select_char_x = panel_center_x - (select_char_width >> 1);
            select_char_y = panel_top + 5;
            DL = microcode_constructor(DL);
            DL = frontPrintText(DL, &select_char_x, &select_char_y, (s8 *) text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }
        
#ifndef TARGET_N64
        text = portMpCharName(portMpCharAt(mp_char_cur_select_player[playernum]));
#else
        text = langGet(mp_chr_setup[mp_char_cur_select_player[playernum]].text_preset);
#endif
        textMeasure(&name_height, &name_width, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        portrait_y_base = panel_top;
        name_x = panel_center_x - (name_width >> 1);
        name_y = (portrait_y_base + 0x46) + 0x32;
        DL = microcode_constructor(DL);
        DL = frontPrintText(DL, &name_x, &name_y, (s8 *) text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        DL = frontRenderCharacterPortrait(DL, panel_left + 0xD, (panel_left + panel_width) - 0xE, panel_center_x - mp_char_select_scroll_offset[playernum], portrait_y_base + 0x46, portMpCharAt(mp_char_prev_select_player[playernum]), size_mp_select_image_player[playernum]);

        if (((player_has_selected_char[playernum] == 0) && (size_mp_select_image_player[playernum] == 0)) || (mp_char_prev_select_player[playernum] != mp_char_cur_select_player[playernum]))
        {
            if (mp_char_prev_select_player[playernum] >= 3)
            {
                DL = frontRenderCharacterPortrait(DL, panel_left + 0xD, (panel_left + panel_width) - 0xE, (panel_center_x - mp_char_select_scroll_offset[playernum]) - 0xFC, portrait_y_base + 0x46, portMpCharAt(mp_char_prev_select_player[playernum] - 3), size_mp_select_image_player[playernum]);
            }

            if (mp_char_prev_select_player[playernum] >= 2)
            {
                DL = frontRenderCharacterPortrait(DL, panel_left + 0xD, (panel_left + panel_width) - 0xE, (panel_center_x - mp_char_select_scroll_offset[playernum]) - 0xA8, portrait_y_base + 0x46, portMpCharAt(mp_char_prev_select_player[playernum] - 2), size_mp_select_image_player[playernum]);
            }

            if (mp_char_prev_select_player[playernum] >= 1)
            {
                DL = frontRenderCharacterPortrait(DL, panel_left + 0xD, (panel_left + panel_width) - 0xE, (panel_center_x - mp_char_select_scroll_offset[playernum]) - 0x54, portrait_y_base + 0x46, portMpCharAt(mp_char_prev_select_player[playernum] - 1), size_mp_select_image_player[playernum]);
            }

            if (mp_char_prev_select_player[playernum] < (num_chars_selectable_mp - 1))
            {
                DL = frontRenderCharacterPortrait(DL, panel_left + 0xD, (panel_left + panel_width) - 0xE, (panel_center_x - mp_char_select_scroll_offset[playernum]) + 0x54, portrait_y_base + 0x46, portMpCharAt(mp_char_prev_select_player[playernum] + 1), size_mp_select_image_player[playernum]);
            }

            if (mp_char_prev_select_player[playernum] < (num_chars_selectable_mp - 2))
            {
                DL = frontRenderCharacterPortrait(DL, panel_left + 0xD, (panel_left + panel_width) - 0xE, (panel_center_x - mp_char_select_scroll_offset[playernum]) + 0xA8, portrait_y_base + 0x46, portMpCharAt(mp_char_prev_select_player[playernum] + 2), size_mp_select_image_player[playernum]);
            }

            if (mp_char_prev_select_player[playernum] < (num_chars_selectable_mp - 3))
            {
                DL = frontRenderCharacterPortrait(DL, panel_left + 0xD, (panel_left + panel_width) - 0xE, (panel_center_x - mp_char_select_scroll_offset[playernum]) + 0xFC, portrait_y_base + 0x46, portMpCharAt(mp_char_prev_select_player[playernum] + 3), size_mp_select_image_player[playernum]);
            }
        }
    }

    DL = combiner_bayer_lod_perspective(DL);

    return DL;
}


//********************************************************************************************************
//MULTIPLAYER HANDICAP
//********************************************************************************************************
void init_menu10_mphandicap(void)
{
    s32 i = 0;
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    for (i = 0; i < MAX_PLAYER_COUNT; i++) {
        player_has_selected_char[i] = FALSE;
        dword_CODE_bss_80069760[i] = 0;
    }
    load_walletbond();
}

void update_menu10_mphandicap(void) 
{
    return;
}


// Address 0x7F012EC4 NTSC
void interface_menu10_mphandicap(void)
{
    s32 sp44;
    s32 i;
    s32 var_fp;

    sp44 = get_selected_num_players();
    var_fp = 0;
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    for (i = 0; i < sp44; i++)
    {
        if (player_has_selected_char[i] && joyGetButtonsPressedThisFrame(i, B_BUTTON))
        {
            player_has_selected_char[i] = 0;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }

        if (player_has_selected_char[i] == 0)
        {
            // 0x202
            if (joyGetButtonsPressedThisFrame(i, L_JPAD|L_CBUTTONS) || ((joyGetStickXInRange(i, -2, 1) < -1) && dword_CODE_bss_80069760[i] != 0))
            {
                if (player_handicap[i] > 0)
                {
                    player_handicap[i]--;
                }
            }
            // 0x101
            else if (joyGetButtonsPressedThisFrame(i, R_JPAD|R_CBUTTONS) || ((joyGetStickXInRange(i, -2, 1) > 0) && dword_CODE_bss_80069760[i] != 0))
            {
                if (player_handicap[i] < 0xA)
                {
                    player_handicap[i]++;
                }
            }
            else if (joyGetButtonsPressedThisFrame(i, A_BUTTON|Z_TRIG|START_BUTTON)) // 0xb000
            {
                player_has_selected_char[i] = 1;
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }

            if ((joyGetStickXInRange(i, -2, 1) == -1) || (joyGetStickXInRange(i, -2, 1) == 0))
            {
                dword_CODE_bss_80069760[i] = 1;
            }
            else
            {
                dword_CODE_bss_80069760[i] = 0;
            }
        }

        if (player_has_selected_char[i] != 0)
        {
            var_fp++;
        }
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    frontUpdateControlStickPosition();

#ifndef TARGET_N64
    if (portMpWaitScreenExitPressed(sp44))
    {
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }
    else
#endif
    if (var_fp == sp44)
    {
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }
}



// Address 0x7F01317C NTSC
Gfx * constructor_menu10_mphandicap(Gfx *DL)
{
    s32 spCC;
    s32 i; // spE0
    u8 *text;
    s32 var_s3;
    s32 var_s5;
    s32 var_v1;
    u8 *text2;
    s32 padding4;
    s32 padding1;
    s32 var_s2;
    s32 spA4;
    s32 spA0;
    s32 sp9C;
    s32 sp98;
    s32 padding2;
    s32 sp90;
    s32 sp8C;
    s32 sp88;
    s32 sp84;
    s32 padding5;

    spCC = get_selected_num_players();

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);
    DL = microcode_constructor_related_to_menus(DL, 0x26, 0xA9, 0x184, 0xAB, 0x90);

    if (spCC >= 3)
    {
        DL = microcode_constructor_related_to_menus(DL, 0xD4, 0x1E, 0xD6, 0x136, 0x80);
    }

    for (i = 0; i < spCC; i++)
    {
        padding4 = player_has_selected_char[i];
        padding1 = player_handicap[i];

        var_s2 = 0;

        if (spCC == 2)
        {
            var_s3 = 0x26;
            var_s5 = 0x15E;
            var_v1 = (i > 0) ? 0x8C : 0;
            var_s2 = (var_v1) + 0x1E;
        }
        else
        {
            var_s5 = 0xAF;
            var_v1 = (i >= 2) ? 0x8C : 0;
            var_s2 = (var_v1) + 0x1E;
            var_v1 = (i & 1) ? 0xAF : 0;
            var_s3 = (var_v1) + 0x26;
        }

        gDPSetScissor(DL++, G_SC_NON_INTERLACE, (var_s3 + 6), var_s2 + 5, (var_s3 + var_s5 - 6), (var_s2 + 0x8C - 5));

        padding2 = ((var_s5 >> 1) + var_s3);
        padding5 = var_s2 + 0x46;

        if (player_has_selected_char[i] == 0)
        {
            text = langGet(getStringID(LTITLE, TITLE_STR_86_SELECTHANDICAP));
            textMeasure(&spA4, &spA0, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

            sp9C = padding2 - (spA0 >> 1);
            sp98 = padding5 - (spA4 >> 1) - 0xf;

            DL = microcode_constructor(DL);
            DL = frontPrintText(DL, &sp9C, &sp98, (s8*)text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }

        {
            text2 = langGet(MP_handicap_table[player_handicap[i]].text_preset);
            textMeasure(&sp90, &sp8C, text2, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
            sp88 = padding2 - (sp8C >> 1);
            sp84 = padding5 - (sp90 >> 1) + 0xf;

            DL = microcode_constructor(DL);
            DL = frontPrintText(DL, &sp88, &sp84, (s8*)text2, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }
    }

    DL = combiner_bayer_lod_perspective(DL);

    return DL;
}



#ifndef TARGET_N64
//********************************************************************************************************
//PLAYER OPTIONS (port) - what MENU_MP_CONTROL_STYLE is on the port
//********************************************************************************************************
/* Player Options replaces Rare's Control Style screen: each player assigns
 * their controller, then manages their profile and control settings.
 *
 * The screen keeps Rare's split (the same quadrants as the control style,
 * character and handicap screens), and each player drives their own panel with
 * their own controller. That is why it doesn't use the sheet system, which is
 * one page, one crosshair and player 1's buttons. A panel is a row list stepped
 * with that player's stick, D-pad or C buttons, with no crosshair.
 *
 * TWO PHASES:
 *   SEATING  players 2..n in turn press a button on the pad that is theirs
 *            (input_pc.c portInputAssignBeginEx). A pad already seated this
 *            pass cannot be taken again. Player 1 is never asked: if the press
 *            that opened this screen came from slot 0's pad, that pad is
 *            player 1's and is protected too; if it came from the keyboard or
 *            mouse, slot 0's pad is fair game for somebody else. Player 1
 *            skips a player with any button / key / click; an arm also gives
 *            up by itself after ten seconds. Skipped = that player keeps
 *            whatever they had.
 *   PANELS   every player at once: Profile, Style, Config, Always Walk,
 *            Detonate, Done. When everyone is Done the screen returns to the
 *            MP setup. B on a Done panel takes it back. L / R leaves at once
 *            (the wait screens' exit, portMpWaitScreenExitPressed).
 *
 * THE SETTINGS ARE EACH PLAYER'S OWN [input] padN_* keys, written as they
 * change; a PROFILE (port/src/port_profiles.c) is a named file of them.
 * docs/port/mp-player-options.md. */
extern s32 portInputAssignBeginEx(s32 slot, s32 excludeMask);
extern s32 portInputAssignTick(void);
extern s32 portInputAssignTake(void);
extern void portInputAssignCancel(void);
extern s32 portInputSlotAnyButtonDown(s32 slot);
extern s32 portInputSlotDevice(s32 slot);
extern s32 portInputStyleRaw(s32 slot);
extern const char *portInputStyleName(s32 code);
extern s32 portInputDeviceModeIsN64(s32 dev);
extern const char *portInputDeviceLabel(s32 dev);
extern s32 portInputLayoutFitsSlot(s32 slot, s32 i);
extern s32 portJoyPortPadOptGet(s32 pad, s32 which);
extern void portJoyPortPadOptSet(s32 pad, s32 which, s32 v);
extern s32 portProfileCount(void);
extern void portProfileRescan(void);
extern const char *portProfileName(s32 i);
extern const char *portProfileActive(s32 player);
extern s32 portProfileSeatOf(const char *name);
#include "port_mpstats.h" /* the Player Options Stats page */
extern s32 portProfileLoad(s32 player, s32 i);
extern s32 portProfileSave(s32 player, const char *name);
extern void portProfileClear(s32 player);
extern s32 portProfileDelete(s32 player, const char *name);
extern s32 portProfileNameMax(void);
extern void portInputDeviceModeSet(s32 dev, s32 isN64);
extern s32 portPlayerLookUpright(s32 player);   /* options.c: 1 upright, 0 reverse */
extern s32 portPlayerLookAhead(s32 player);     /* options.c: 1 on, 0 off */
extern s32 portPlayerAutoAim(s32 player);       /* options.c: 1 on, 0 off */
extern s32 portPlayerCrosshairColor(s32 player); /* video.c: own (2+ players) or the global; -1 = red */
extern void portPlayerCrosshairSet(s32 player, s32 rgb);
extern s32 portPlayerCrosshairOwn(s32 player);   /* video.c: -2 none, -1 red, else 0xRRGGBB */

/* The row's colour is the player's own, else the global; not portPlayerCrosshairColor,
 * whose in-match rule always answers the global in the front end, so left /
 * right would never move it. */
static s32 portPpoXhairColor(s32 i)
{
    const s32 own = portPlayerCrosshairOwn(i);

    return (own >= -1) ? own : portCrosshairColor();
}

enum { PORT_PPO_SEATING, PORT_PPO_PANELS };
/* no Always Walk row - Walk is a held bind and nothing else */
enum { PORT_PPO_ROW_PROFILE,
       PORT_PPO_ROW_CHAR,      /* the player's MP character (was the Character screen) */
       PORT_PPO_ROW_STYLE, PORT_PPO_ROW_CONFIG, PORT_PPO_ROW_LOOK,
       PORT_PPO_ROW_LOOKAHEAD, /* Look Ahead */
       PORT_PPO_ROW_AUTOAIM,   /* read when Force Auto Aim is PP */
       PORT_PPO_ROW_XHAIR,     /* crosshair colour (replaced the detonate row) */
       PORT_PPO_ROW_DONE, PORT_PPO_ROW_COUNT };
enum { PORT_PPO_VIEW_ROWS, PORT_PPO_VIEW_LIST, PORT_PPO_VIEW_KEYS,
       PORT_PPO_VIEW_STATS,    /* lifetime MP stats, Player Options only */
       PORT_PPO_VIEW_CHAR };   /* Rare's character carousel, in this panel only */
#define PORT_PPO_STATS_PAGES 3   /* Record, Shooting, Awards */
#define PORT_PPO_STATS_LINES 8   /* lines a page shows; Awards scrolls through its nineteen */

#define PORT_PPO_PANEL_H    0x8C   /* Rare's quadrant, as menu11 / 0F / 10 draw it */
#define PORT_PPO_HEAD_Y     7      /* from the panel's top */
/* EIGHT ROWS: at a pitch of 15 the four-way capture clipped "Done" at the
 * quadrant's foot, so the panel rows close up to 14 (the last row's text ends ~132). */
#define PORT_PPO_ROW_Y0     22
#define PORT_PPO_ROW_PITCH  14
/* the main rows only: nine of them fit the quadrant at this pitch */
#define PORT_PPO_MAIN_Y0    18
#define PORT_PPO_MAIN_PITCH 13
#define PORT_PPO_LIST_LINES 6
#define PORT_PPO_PAD_X      10     /* panel edge to the text */
#define PORT_PPO_KEY_COLS   10
#define PORT_PPO_KEY_ROWS   5      /* four of characters + DEL / OK */
#define PORT_PPO_KEY_W      14     /* ten of them fit the four-way quadrant */
#define PORT_PPO_KEY_H      13
#define PORT_PPO_KEY_Y0     46
#define PORT_PPO_NAME_BUF   16

/* Characters allowed in a profile name, which is also its file name, so '?' is
 * excluded (it can't appear in a Windows file name). Mixed case is allowed; the
 * keys can't show case in the small font, so CAPS stays lit red while on and
 * the name field shows exactly what was typed. */
static const char port_ppo_keys[4][PORT_PPO_KEY_COLS] = {
    { '0','1','2','3','4','5','6','7','8','9' },
    { 'A','B','C','D','E','F','G','H','I','J' },
    { 'K','L','M','N','O','P','Q','R','S','T' },
    { 'U','V','W','X','Y','Z',' ','-','!','.' },
};
/* row 4's three wide buttons: where each starts, how many columns it covers */
enum { PORT_PPO_WIDE_DEL, PORT_PPO_WIDE_CAPS, PORT_PPO_WIDE_OK, PORT_PPO_WIDE_COUNT };
static const s32 port_ppo_wide_col[PORT_PPO_WIDE_COUNT]  = { 0, 3, 7 };
static const s32 port_ppo_wide_span[PORT_PPO_WIDE_COUNT] = { 3, 4, 3 };
static const char *const port_ppo_wide_text[PORT_PPO_WIDE_COUNT] = { "DEL\n", "CAPS\n", "OK\n" };

static s32 portPpoWideAt(s32 col)
{
    return (col < 3) ? PORT_PPO_WIDE_DEL : (col < 7) ? PORT_PPO_WIDE_CAPS : PORT_PPO_WIDE_OK;
}

struct PortPpoPanel {
    s32 view;
    s32 row;
    s32 done;
    s32 armed;          /* FALSE until every button has been let go once: the press that seated the player, or opened the screen, is not a pick */
    s32 listPick;
    s32 listTop;
    s32 listN;          /* profiles in the list when listPick was set */
    char listName[48];  /* the profile listPick named, if it named one */
    s32 keyRow;
    s32 keyCol;
    s32 caps;
    s32 stickDir;       /* portPpoNav: the held push, 0 none */
    f32 stickWait;
    char text[PORT_PPO_NAME_BUF];
    s32 listMode;                   /* PORT_PPO_LIST_*: the menu, Load, "Delete Which?", "Delete NAME?" */
    char delName[48];               /* PORT_PPO_LIST_DELSURE: the profile to go */
    char keyMsg[24];                /* the keyboard's OK was refused: why, in the heading */
    unsigned long long keyMsgUntil; /* portProfNow() it shows until */
    s32 statsPage;                  /* PORT_PPO_VIEW_STATS: 0 record, 1 shooting, 2 awards */
    s32 statsScroll;                /* the Awards page's first line */
    s32 stats[PORT_MPSTAT_COUNT];   /* the profile's totals, read as the page opened */
};
static struct PortPpoPanel port_ppo[MAX_PLAYER_COUNT];
static s32 port_ppo_phase;
static s32 port_ppo_seat;      /* SEATING: the player being asked, 1..n-1 */
static s32 port_ppo_exclude;   /* SEATING: slots whose pad is spoken for */
static s32 port_ppo_p1pad;     /* player 1 is on slot 0's pad (else keyboard and mouse) */

static void portPpoPanelRect(s32 i, s32 n, s32 *px, s32 *py, s32 *pw)
{
    if (n == 2)
    {
        *px = 0x26;
        *pw = 0x15E;
        *py = ((i > 0) ? PORT_PPO_PANEL_H : 0) + 0x1E;
    }
    else
    {
        *px = ((i & 1) ? 0xAF : 0) + 0x26;
        *pw = 0xAF;
        *py = ((i >= 2) ? PORT_PPO_PANEL_H : 0) + 0x1E;
    }
}

static void portPpoBeginPanels(s32 n)
{
    s32 i;

    port_ppo_phase = PORT_PPO_PANELS;
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        memset(&port_ppo[i], 0, sizeof(port_ppo[i]));
    }
    (void) n;
}

static void portPpoSeatNext(s32 n)
{
    for (;;)
    {
        port_ppo_seat++;
        if (port_ppo_seat >= n)
        {
            portPpoBeginPanels(n);
            return;
        }
        if (portInputAssignBeginEx(port_ppo_seat, port_ppo_exclude))
        {
            return;
        }
    }
}

static void portPpoCharOpen(s32 i);

static void portPpoInit(void)
{
    extern char *getenv(const char *);
    const char *e = getenv("GE_PPO");   /* capture harness, never midas.ini: rows | list | keys = skip the seating, open on that view */
    const s32 n = get_selected_num_players();
    s32 i;

    portInputAssignCancel();
    /* the press that chose "Player Options" is still down: whose was it? */
    port_ppo_p1pad = portInputSlotAnyButtonDown(PLAYER_1) != 0;
    port_ppo_exclude = port_ppo_p1pad ? 1 : 0;
    port_ppo_phase = PORT_PPO_SEATING;
    port_ppo_seat = 0;
    if (e != NULL && e[0] != '\0')
    {
        portPpoBeginPanels(n);
        for (i = 0; i < MAX_PLAYER_COUNT; i++)
        {
            port_ppo[i].view = (e[0] == 'l') ? PORT_PPO_VIEW_LIST : (e[0] == 'k') ? PORT_PPO_VIEW_KEYS
                             : (e[0] == 's') ? PORT_PPO_VIEW_STATS : PORT_PPO_VIEW_ROWS;
            portMpStatsLoad(portProfileActive(i), port_ppo[i].stats);   /* GE_PPO=stats[2]: page 2 */
            port_ppo[i].statsPage = (e[0] == 's' && e[1] && e[2] && e[3] && e[4]) ? ((e[5] == '2') ? 1 : (e[5] == '3') ? 2 : 0) : 0;
            port_ppo[i].statsScroll = 0;
            port_ppo[i].listN = portProfileCount();   /* the list as it opens */
            port_ppo[i].keyRow = 1;
            port_ppo[i].caps = (e[1] != 'l');   /* GE_PPO=klower: open with caps off */
            snprintf(port_ppo[i].text, sizeof(port_ppo[i].text), "%s", (e[0] == 'k') ? "BOND" : "");
        }
        if (e[0] == 'c')
        {   /* GE_PPO=char: player 1's carousel, the other panels on their rows */
            portPpoCharOpen(PLAYER_1);
        }
        return;
    }
    portPpoSeatNext(n);
}

/* This player's step this tick: 0 none, else 1 right, 2 left, 3 down, 4 up.
 * D-pad and C buttons step once a press; the stick steps at once and then
 * repeats while held - the on-screen keyboard's feel (portOskStickStep), kept
 * per player here because four people are steering at once. */
static s32 portPpoNav(s32 i)
{
    struct PortPpoPanel *p = &port_ppo[i];
    const s32 rawX = joyGetStickX(i);
    const s32 rawY = joyGetStickY(i);
    const s32 magX = (rawX < 0) ? -rawX : rawX;
    const s32 magY = (rawY < 0) ? -rawY : rawY;
    s32 dir;
    s32 mag;
    f32 t;

    dir = joyGetButtonsPressedThisFrame(i, R_JPAD | R_CBUTTONS) ? 1
        : joyGetButtonsPressedThisFrame(i, L_JPAD | L_CBUTTONS) ? 2
        : joyGetButtonsPressedThisFrame(i, D_JPAD | D_CBUTTONS) ? 3
        : joyGetButtonsPressedThisFrame(i, U_JPAD | U_CBUTTONS) ? 4 : 0;
    if (dir != 0)
    {   /* ONE KEY IS OFTEN BOTH a C button and the stick (W A S D): the push
         * is already seen, or the next tick steps again - portNavStickDir */
        p->stickDir = portNavStickDir(i);
        p->stickWait = PORT_OSK_REPEAT_FIRST;
        return dir;
    }

    if (magX < magY)
    {
        mag = magY;
        dir = (rawY > 0) ? 4 : 3;   /* N64 stick Y is positive UP */
    }
    else
    {
        mag = magX;
        dir = (rawX > 0) ? 1 : 2;
    }
    if (mag <= PORT_OSK_STICK_DEAD)
    {
        p->stickDir = 0;
        p->stickWait = 0.0f;
        return 0;
    }
    if (dir != p->stickDir)
    {
        p->stickDir = dir;
        p->stickWait = PORT_OSK_REPEAT_FIRST;
        return dir;
    }
    p->stickWait -= (f32) g_GlobalTimerDelta / 60.0f;   /* delta is in 60Hz FIELDS */
    if (p->stickWait > 0.0f)
    {
        return 0;
    }
    if (mag > PORT_OSK_STICK_FULL)
    {
        mag = PORT_OSK_STICK_FULL;
    }
    t = (f32) (mag - PORT_OSK_STICK_DEAD) / (f32) (PORT_OSK_STICK_FULL - PORT_OSK_STICK_DEAD);
    p->stickWait = PORT_OSK_REPEAT_SLOW + (PORT_OSK_REPEAT_FAST - PORT_OSK_REPEAT_SLOW) * t;
    return dir;
}

/* the pad this player's Style and Config rows act on, or -1: an empty slot,
 * or player 1 on the keyboard (slot 0's pad, if any, is not what they hold) */
static s32 portPpoDevice(s32 i)
{
    if (i == PLAYER_1 && !port_ppo_p1pad)
    {
        return -1;
    }
    return portInputSlotDevice(i);
}

static s32 portPpoConfigFits(s32 i)
{
    s32 c;

    for (c = 0; c < portInputLayoutCount(); c++)
    {
        if (portInputLayoutFitsSlot(i, c))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static s32 portPpoRowDisabled(s32 i, s32 row)
{
    const s32 dev = portPpoDevice(i);

    if (row == PORT_PPO_ROW_STYLE)
    {   /* any pad: the row is the input mode AND the style (portPpoRowStep) */
        return dev < 0;
    }
    if (row == PORT_PPO_ROW_CONFIG)
    {
        return dev < 0 || !portPpoConfigFits(i);
    }
    return FALSE;
}

static const char *portPpoRowLabel(s32 row)
{
    static const char *const label[PORT_PPO_ROW_COUNT] = {
        "Profile\n", "Character\n", "Style\n", "Config\n", "Look Up/Down\n", "Look Ahead\n", "Auto-Aim\n", "Crosshair\n", "Done\n",
    };

    return label[row];
}

static const char *portPpoRowValue(s32 i, s32 row)
{
    static char text[MAX_PLAYER_COUNT][48];
    const s32 dev = portPpoDevice(i);

    text[i][0] = '\0';
    switch (row)
    {
        case PORT_PPO_ROW_PROFILE:
            /* no value: the panel's heading already names the profile */
            break;
        case PORT_PPO_ROW_CHAR:
            /* no value: A opens the carousel, which shows the pick */
            break;
        case PORT_PPO_ROW_STYLE:
        {
            const s32 code = portInputStyleRaw(i);

            /* One list: "Port" (the port drives the pad directly), then the
             * N64 styles 1.1 .. 2.4. "Auto" is shown, never offered, for an
             * N64-mode pad with no style picked. */
            /* Player 1 on keyboard and mouse shows "KBM": it has no style and no bind config,
             * so both rows are grey (portPpoDevice answers -1). */
            snprintf(text[i], sizeof(text[i]), "%s\n", (i == PLAYER_1 && !port_ppo_p1pad) ? "KBM"
                     : (dev < 0) ? "-"
                     : !portInputDeviceModeIsN64(dev) ? "Port"
                     : (code < 0) ? "Auto" : portInputStyleName(code));
            break;
        }
        case PORT_PPO_ROW_LOOK:
            /* Rare's own names for look inversion, as the General Settings
             * sheet and the watch spell them; Reverse (Rare's default) =
             * inverted. */
            snprintf(text[i], sizeof(text[i]), "%s\n", portPlayerLookUpright(i) ? "Upright" : "Reverse");
            break;
        case PORT_PPO_ROW_LOOKAHEAD:
            /* Rare's own option, its own words (the GENERAL SETTINGS sheet's On / Off) */
            snprintf(text[i], sizeof(text[i]), "%s\n", portPlayerLookAhead(i) ? "On" : "Off");
            break;
        case PORT_PPO_ROW_AUTOAIM:
            snprintf(text[i], sizeof(text[i]), "%s\n", portPlayerAutoAim(i) ? "On" : "Off");
            break;
        case PORT_PPO_ROW_CONFIG:
        {
            /* a fresh install has picked no config: name the one the pad is on */
            const char *a = (dev < 0) ? "-" : portInputLayoutShown(dev);

            snprintf(text[i], sizeof(text[i]), "%s\n", (a != NULL && a[0] != '\0') ? a : "None");
            break;
        }
        case PORT_PPO_ROW_XHAIR:
        {   /* the colour in force for this player: their own, else the global */
            const s32 rgb = portPpoXhairColor(i);
            s32 k;
            const char *name = "Custom\n";

            for (k = 0; k < PORT_XHAIR_COUNT; k++)
            {
                if (port_xhair_rgb[k] == rgb)
                {
                    name = port_xhair_name[k];
                }
            }
            snprintf(text[i], sizeof(text[i]), "%s", name);
            break;
        }
        default:
            break;
    }
    return text[i];
}

static void portPpoRowStep(s32 i, s32 row, s32 dir)
{
    const s32 dev = portPpoDevice(i);

    if (portPpoRowDisabled(i, row))
    {
        return;
    }
    if (row == PORT_PPO_ROW_STYLE)
    {   /* Position 0 is Port mode, 1..8 the styles; picking a style also sets
         * N64 mode (padN_inputmode), while picking Port keeps the stored style
         * for next time. From "Auto" or a "2.x P2" code, right goes to 1.1 and
         * left to Port. */
        extern s32 portInputDeviceIsRaphnet(s32 dev);
        const s32 code = portInputStyleRaw(i);
        const s32 cur = !portInputDeviceModeIsN64(dev) ? 0 : (code >= 0 && code <= 7) ? code + 1 : -1;
        /* a raphnet is always N64 - the mode setter refuses Port - so
         * its row cycles the eight styles only, 2.4 wrapping to 1.1 */
        const s32 next = portInputDeviceIsRaphnet(dev)
                       ? ((cur < 0) ? ((dir > 0) ? 1 : 8) : ((cur - 1 + dir + 8) % 8) + 1)
                       : (cur < 0) ? ((dir > 0) ? 1 : 0) : (cur + dir + 9) % 9;

        if (next == 0)
        {
            portInputDeviceModeSet(dev, FALSE);
        }
        else
        {
            if (!portInputDeviceModeIsN64(dev))
            {
                portInputDeviceModeSet(dev, TRUE);
            }
            portInputDeviceStyleSet(dev, next - 1);
        }
    }
    else if (row == PORT_PPO_ROW_LOOK)
    {
        portJoyPortPadOptSet(i, 2, !portPlayerLookUpright(i));
    }
    else if (row == PORT_PPO_ROW_LOOKAHEAD)
    {
        portJoyPortPadOptSet(i, 3, !portPlayerLookAhead(i));
    }
    else if (row == PORT_PPO_ROW_AUTOAIM)
    {
        portJoyPortPadOptSet(i, 4, !portPlayerAutoAim(i));
    }
    else if (row == PORT_PPO_ROW_CONFIG)
    {   /* the next bind config MADE FOR THIS PAD (portInputLayoutFitsSlot),
         * stepping from the one in use when none was picked */
        const char *active = portInputLayoutShown(dev);
        const s32 n = portInputLayoutCount();
        s32 cur = -1;
        s32 c, k;

        for (c = 0; c < n; c++)
        {
            if (active != NULL && strcmp(portInputLayoutName(c), active) == 0)
            {
                cur = c;
                break;
            }
        }
        for (k = 1; k <= n; k++)
        {
            c = (((cur < 0 && dir < 0) ? 0 : cur) + dir * k) % n;
            if (c < 0)
            {
                c += n;
            }
            if (portInputLayoutFitsSlot(i, c))
            {
                if (c != cur)
                {
                    portInputLayoutApply(dev, c);
                }
                break;
            }
        }
    }
    else if (row == PORT_PPO_ROW_XHAIR)
    {   /* the folder's colour list, from the colour in force, wrapping */
        const s32 rgb = portPpoXhairColor(i);
        s32 k = 0;
        s32 at;

        for (at = 0; at < PORT_XHAIR_COUNT; at++)
        {
            if (port_xhair_rgb[at] == rgb)
            {
                k = at;
                break;
            }
        }
        k = (k + ((dir < 0) ? PORT_XHAIR_COUNT - 1 : 1)) % PORT_XHAIR_COUNT;
        portPlayerCrosshairSet(i, port_xhair_rgb[k]);
    }
}

/* THE PROFILE MENU: the Profile row opens four entries:
 *
 *   New      the name keyboard (New Profile)
 *   Load     "Load Profile": every file in Data/Profiles, then - for a player
 *            who has one loaded - No Profile (unload)
 *   Delete   "Delete Which?" (the profiles), then "Delete NAME?" No / Yes
 *   Stats    the loaded profile's Stats page
 *
 * Load / Delete are greyed with no profiles on disk, Stats with none loaded. B
 * steps back one level. A profile loaded by another player is listed greyed as
 * "NAME (P2)" and cannot be picked (one player per profile). There is no Save
 * entry: a loaded profile saves itself (port_profiles.c AUTOSAVE). */
enum { PORT_PPO_LIST_TOP, PORT_PPO_LIST_LOAD, PORT_PPO_LIST_DELWHICH, PORT_PPO_LIST_DELSURE };
enum { PORT_PPO_PICK_LOAD, PORT_PPO_PICK_NEW, PORT_PPO_PICK_DELETE, PORT_PPO_PICK_STATS, PORT_PPO_PICK_NONE,
       PORT_PPO_PICK_DEL_NO, PORT_PPO_PICK_DEL_YES, PORT_PPO_PICK_OPENLOAD };

static s32 portPpoListCount(s32 i)
{
    switch (port_ppo[i].listMode)
    {
        case PORT_PPO_LIST_TOP:      return 4;
        case PORT_PPO_LIST_LOAD:     return portProfileCount() + ((portProfileActive(i)[0] != '\0') ? 1 : 0);
        case PORT_PPO_LIST_DELWHICH: return portProfileCount();
        default:                     return 2;
    }
}

static s32 portPpoListKind(s32 i, s32 idx)
{
    switch (port_ppo[i].listMode)
    {
        case PORT_PPO_LIST_TOP:
            return (idx == 0) ? PORT_PPO_PICK_NEW : (idx == 1) ? PORT_PPO_PICK_OPENLOAD
                 : (idx == 2) ? PORT_PPO_PICK_DELETE : PORT_PPO_PICK_STATS;
        case PORT_PPO_LIST_LOAD:
            return (idx < portProfileCount()) ? PORT_PPO_PICK_LOAD : PORT_PPO_PICK_NONE;
        case PORT_PPO_LIST_DELWHICH:
            return PORT_PPO_PICK_LOAD;   /* a profile - the pick deletes rather than loads */
        default:
            return (idx == 0) ? PORT_PPO_PICK_DEL_NO : PORT_PPO_PICK_DEL_YES;
    }
}

/* list entry idx is a profile sitting in ANOTHER seat */
static s32 portPpoListTaken(s32 i, s32 idx)
{
    s32 seat;

    if (portPpoListKind(i, idx) != PORT_PPO_PICK_LOAD)
    {
        return FALSE;
    }
    seat = portProfileSeatOf(portProfileName(idx));
    return seat >= 0 && seat != i;
}

/* greyed and not a pick: another seat's profile, or a menu entry with nothing behind it */
static s32 portPpoListGrey(s32 i, s32 idx)
{
    switch (portPpoListKind(i, idx))
    {
        case PORT_PPO_PICK_OPENLOAD:
        case PORT_PPO_PICK_DELETE:   return portProfileCount() == 0;
        case PORT_PPO_PICK_STATS:    return portProfileActive(i)[0] == '\0';
        default:                     return portPpoListTaken(i, idx);
    }
}

static const char *portPpoListText(s32 i, s32 idx)
{
    static char text[MAX_PLAYER_COUNT][48];

    switch (portPpoListKind(i, idx))
    {
        case PORT_PPO_PICK_LOAD:
            if (portPpoListTaken(i, idx))
            {
                snprintf(text[i], sizeof(text[i]), "%s (P%d)\n", portProfileName(idx),
                         (int) portProfileSeatOf(portProfileName(idx)) + 1);
            }
            else
            {
                snprintf(text[i], sizeof(text[i]), "%s\n", portProfileName(idx));
            }
            break;
        case PORT_PPO_PICK_NEW:      snprintf(text[i], sizeof(text[i]), "New\n"); break;
        case PORT_PPO_PICK_OPENLOAD: snprintf(text[i], sizeof(text[i]), "Load\n"); break;
        case PORT_PPO_PICK_DELETE:   snprintf(text[i], sizeof(text[i]), "Delete\n"); break;
        case PORT_PPO_PICK_STATS:    snprintf(text[i], sizeof(text[i]), "Stats\n"); break;
        case PORT_PPO_PICK_DEL_NO:   snprintf(text[i], sizeof(text[i]), "No\n"); break;
        case PORT_PPO_PICK_DEL_YES:  snprintf(text[i], sizeof(text[i]), "Yes, Delete\n"); break;
        default:                     snprintf(text[i], sizeof(text[i]), "No Profile\n"); break;
    }
    return text[i];
}

/* switch the list to `mode`, the highlight on `pick` */
static void portPpoListMode(s32 i, s32 mode, s32 pick)
{
    struct PortPpoPanel *p = &port_ppo[i];

    p->listMode = mode;
    p->listPick = pick;
    p->listTop = (pick >= PORT_PPO_LIST_LINES) ? pick - PORT_PPO_LIST_LINES + 1 : 0;
    p->listN = portProfileCount();
    p->listName[0] = '\0';
}

static void portPpoListOpen(s32 i)
{
    struct PortPpoPanel *p = &port_ppo[i];

    portProfileRescan();   /* somebody else may have saved one a moment ago */
    p->view = PORT_PPO_VIEW_LIST;
    portPpoListMode(i, PORT_PPO_LIST_TOP, 0);
}

static void portPpoKeysOpen(s32 i)
{
    struct PortPpoPanel *p = &port_ppo[i];

    p->view = PORT_PPO_VIEW_KEYS;
    p->keyRow = 1;
    p->keyCol = 0;
    p->caps = TRUE;
    p->text[0] = '\0';
}

/* The Character row's A: Rare's carousel in this player's panel only, on
 * Rare's own per-player state (mp_char_*_select_player and friends), so the
 * other panels carry on. init_menu0f_mpcharsel's texture loads come first;
 * texLoad keeps what the pool already holds. */
static void portPpoCharOpen(s32 i)
{
    const s32 cur = (player_char[i] >= 0) ? player_char[i] : i;
    s32 c;

    for (c = 0; c < PORT_MP_CHAR_COUNT; c++)
    {
        s32 k;

        for (k = 0; k < 4; k++)
        {
            if (mpcharselimages[mp_chr_setup[c].select_photo * 4 + k].index < (u32)(MAX_TEXTURES - 1))
            {
                texLoadFromTextureNum(mpcharselimages[mp_chr_setup[c].select_photo * 4 + k].index, 0);
            }
        }
    }
    mp_char_cur_select_player[i] = portMpCharPos(cur);   /* positions in the shown order */
    mp_char_prev_select_player[i] = portMpCharPos(cur);
    mp_char_select_scroll_offset[i] = 0;
    player_has_selected_char[i] = 0;
    size_mp_select_image_player[i] = 0;
    port_ppo[i].view = PORT_PPO_VIEW_CHAR;
}

/* interface_menu0F_mpcharsel's loop body for one player. A picks (the portrait
 * grows, then the panel goes back to its rows); B backs out with the character
 * as it was. */
static void portPpoCharTick(s32 i)
{
    const s32 step30 = (g_ClockTimer > 0);
    s32 at;

    if (player_has_selected_char[i])
    {
        if (step30 && (size_mp_select_image_player[i] < 0xb) && MPCHARSEL_SLIDE_IDLE(i))
        {
            size_mp_select_image_player[i]++;
        }
        if (size_mp_select_image_player[i] >= 0xb)
        {
            port_ppo[i].view = PORT_PPO_VIEW_ROWS;
            return;
        }
    }
    else if (joyGetButtonsPressedThisFrame(i, B_BUTTON))
    {
        port_ppo[i].view = PORT_PPO_VIEW_ROWS;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        return;
    }
    else if (joyGetButtonsPressedThisFrame(i, L_JPAD | L_CBUTTONS))
    {
        if (mp_char_cur_select_player[i] > 0)
        {
            mp_char_cur_select_player[i]--;
        }
    }
    else if (joyGetButtonsPressedThisFrame(i, R_JPAD | R_CBUTTONS))
    {
        if (mp_char_cur_select_player[i] < (num_chars_selectable_mp - 1))
        {
            mp_char_cur_select_player[i]++;
        }
    }
    else if (joyGetButtonsPressedThisFrame(i, A_BUTTON | Z_TRIG | START_BUTTON))
    {
        player_char[i] = portMpCharAt(mp_char_cur_select_player[i]);
        size_mp_select_image_player[i] = 1;
        player_has_selected_char[i] = 1;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetStickXInRange(i, -2, 1) > 0)
    {
        if (step30 && mp_char_select_scroll_offset[i] == 0 && MPCHARSEL_SLIDE_IDLE(i)
            && mp_char_cur_select_player[i] < (num_chars_selectable_mp - 1))
        {
            mp_char_cur_select_player[i]++;
        }
    }
    else if (step30 && (joyGetStickXInRange(i, -2, 1) < -1) && (mp_char_select_scroll_offset[i] == 0) && MPCHARSEL_SLIDE_IDLE(i))
    {
        if (mp_char_cur_select_player[i] > 0)
        {
            mp_char_cur_select_player[i]--;
        }
    }

    /* the slide toward the current pick, 0xC a step */
    at = (mp_char_prev_select_player[i] * 0x54) + mp_char_select_scroll_offset[i];
    if (!step30)
    {
        /* between 30 Hz steps: the slide holds where it is */
    }
    else if (mp_char_cur_select_player[i] * 0x54 < at)
    {
        mp_char_select_scroll_offset[i] -= 0xC;
        if (mp_char_select_scroll_offset[i] < 0)
        {
            mp_char_select_scroll_offset[i] += 0x54;
            mp_char_prev_select_player[i]--;
        }
    }
    else if (at < mp_char_cur_select_player[i] * 0x54)
    {
        mp_char_select_scroll_offset[i] += 0xC;
        if (mp_char_select_scroll_offset[i] >= 0x54)
        {
            mp_char_select_scroll_offset[i] -= 0x54;
            mp_char_prev_select_player[i]++;
        }
    }
}

static s32 portPpoNameUsable(const char *s)
{
    for (; *s != '\0'; s++)
    {
        if (*s != ' ' && *s != '.')
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void portPpoPanelTick(s32 i)
{
    struct PortPpoPanel *p = &port_ppo[i];
    const s32 accept = joyGetButtonsPressedThisFrame(i, A_BUTTON | Z_TRIG);
    const s32 back = joyGetButtonsPressedThisFrame(i, B_BUTTON);
    const s32 start = joyGetButtonsPressedThisFrame(i, START_BUTTON);
    s32 nav;

    if (!p->armed)
    {
        if (joyGetButtons(i, A_BUTTON | Z_TRIG | B_BUTTON | START_BUTTON) == 0)
        {
            p->armed = TRUE;
        }
        return;
    }
    if (p->done)
    {
        if (back)
        {
            p->done = FALSE;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        return;
    }
    if (p->view == PORT_PPO_VIEW_CHAR)
    {   /* Rare's carousel reads the pad itself */
        portPpoCharTick(i);
        return;
    }
    nav = portPpoNav(i);

    if (p->view == PORT_PPO_VIEW_ROWS)
    {
        if (nav == 3) p->row = (p->row + 1) % PORT_PPO_ROW_COUNT;
        if (nav == 4) p->row = (p->row + PORT_PPO_ROW_COUNT - 1) % PORT_PPO_ROW_COUNT;
        if (nav == 1 || nav == 2)
        {
            portPpoRowStep(i, p->row, (nav == 1) ? 1 : -1);
        }
        if (start)
        {   /* START = "I am done", from any row */
            p->done = TRUE;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        else if (accept)
        {
            if (p->row == PORT_PPO_ROW_PROFILE)
            {
                portPpoListOpen(i);
            }
            else if (p->row == PORT_PPO_ROW_CHAR)
            {
                portPpoCharOpen(i);
            }
            else if (p->row == PORT_PPO_ROW_DONE)
            {
                p->done = TRUE;
            }
            else
            {
                portPpoRowStep(i, p->row, 1);
            }
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        return;
    }

    if (p->view == PORT_PPO_VIEW_LIST)
    {
        const s32 m = portProfileCount();
        s32 n;

        if (m != p->listN && (p->listMode == PORT_PPO_LIST_TOP || p->listMode == PORT_PPO_LIST_DELSURE))
        {
            p->listN = m;   /* fixed entries; "Delete NAME?" holds the name by value */
        }
        if (m != p->listN)
        {   /* another panel saved a profile and the list re-sorted under
             * this one. The pick follows what it NAMED: the same profile by
             * name, or No Profile by its place after the list. */
            if (p->listPick >= p->listN)
            {
                p->listPick += m - p->listN;
            }
            else
            {
                s32 k;

                for (k = 0; k < m && strcmp(portProfileName(k), p->listName) != 0; k++)
                {
                }
                p->listPick = (k < m) ? k : 0;
            }
            p->listN = m;
        }
        n = portPpoListCount(i);
        if (n <= 0)
        {   /* Load / Delete Which? with nothing left (another panel deleted the last) */
            portPpoListMode(i, PORT_PPO_LIST_TOP, 0);
            n = portPpoListCount(i);
        }
        if (p->listPick >= n) p->listPick = n - 1;
        if (p->listPick < 0) p->listPick = 0;
        if (nav == 3) p->listPick = (p->listPick + 1) % n;
        if (nav == 4) p->listPick = (p->listPick + n - 1) % n;
        if (portPpoListKind(i, p->listPick) == PORT_PPO_PICK_LOAD)
        {
            snprintf(p->listName, sizeof(p->listName), "%s", portProfileName(p->listPick));
        }
        if (p->listPick < p->listTop) p->listTop = p->listPick;
        if (p->listPick >= p->listTop + PORT_PPO_LIST_LINES) p->listTop = p->listPick - PORT_PPO_LIST_LINES + 1;
        if (back)
        {   /* one level back: Delete NAME? -> Delete Which? -> the menu -> the rows */
            if (p->listMode == PORT_PPO_LIST_DELSURE)
            {
                portPpoListMode(i, PORT_PPO_LIST_DELWHICH, 0);
            }
            else if (p->listMode == PORT_PPO_LIST_DELWHICH)
            {
                portPpoListMode(i, PORT_PPO_LIST_TOP, 2);
            }
            else if (p->listMode == PORT_PPO_LIST_LOAD)
            {
                portPpoListMode(i, PORT_PPO_LIST_TOP, 1);
            }
            else
            {
                p->view = PORT_PPO_VIEW_ROWS;
            }
        }
        else if (accept && portPpoListGrey(i, p->listPick))
        {
            /* greyed: another seat's profile, or an entry with nothing behind it */
        }
        else if (accept && p->listMode == PORT_PPO_LIST_DELWHICH)
        {   /* this one: ask first, No highlighted */
            snprintf(p->delName, sizeof(p->delName), "%s", portProfileName(p->listPick));
            portPpoListMode(i, PORT_PPO_LIST_DELSURE, 0);
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        else if (accept)
        {
            switch (portPpoListKind(i, p->listPick))
            {
                case PORT_PPO_PICK_NEW:
                    portPpoKeysOpen(i);
                    break;
                case PORT_PPO_PICK_OPENLOAD:
                    portPpoListMode(i, PORT_PPO_LIST_LOAD, 0);
                    break;
                case PORT_PPO_PICK_DELETE:
                    portPpoListMode(i, PORT_PPO_LIST_DELWHICH, 0);
                    break;
                case PORT_PPO_PICK_STATS:
                    p->view = PORT_PPO_VIEW_STATS;
                    p->statsPage = 0;
                    p->statsScroll = 0;
                    portMpStatsLoad(portProfileActive(i), p->stats);
                    break;
                case PORT_PPO_PICK_LOAD:
                    portProfileLoad(i, p->listPick);
                    p->view = PORT_PPO_VIEW_ROWS;
                    break;
                case PORT_PPO_PICK_DEL_NO:
                    portPpoListMode(i, PORT_PPO_LIST_DELWHICH, 0);
                    break;
                case PORT_PPO_PICK_DEL_YES:
                    portProfileDelete(i, p->delName);
                    portProfileRescan();
                    portPpoListMode(i, PORT_PPO_LIST_TOP, 2);   /* back on the menu, on Delete */
                    break;
                default:   /* No Profile */
                    portProfileClear(i);
                    p->view = PORT_PPO_VIEW_ROWS;
                    break;
            }
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        return;
    }

    if (p->view == PORT_PPO_VIEW_STATS)
    {   /* left / right turn the page; A, B or START go back to the profile menu, on Stats */
        if (nav == 1) { p->statsPage = (p->statsPage + 1) % PORT_PPO_STATS_PAGES; p->statsScroll = 0; }
        if (nav == 2) { p->statsPage = (p->statsPage + PORT_PPO_STATS_PAGES - 1) % PORT_PPO_STATS_PAGES; p->statsScroll = 0; }
        if (p->statsPage == 2)
        {   /* up / down scroll the Awards page's nineteen lines */
            const s32 maxscroll = 2 + PORT_MPSTAT_AWARDS - PORT_PPO_STATS_LINES;

            if (nav == 3 && p->statsScroll < maxscroll) p->statsScroll++;
            if (nav == 4 && p->statsScroll > 0) p->statsScroll--;
        }
        if (back || accept || start)
        {
            p->view = PORT_PPO_VIEW_LIST;
            portPpoListMode(i, PORT_PPO_LIST_TOP, 3);
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        return;
    }

    {   /* The keyboard: row 4 is three wide buttons, DEL / CAPS / OK (port_ppo_wide_*).
         * B deletes a character or backs out of an empty field, and START = OK. */
        s32 len = (s32) strlen(p->text);
        s32 ok = FALSE;

        if (p->keyRow == 4 && (nav == 1 || nav == 2))
        {   /* button to button, landing on the next one's first column */
            const s32 w = (portPpoWideAt(p->keyCol) + ((nav == 1) ? 1 : PORT_PPO_WIDE_COUNT - 1)) % PORT_PPO_WIDE_COUNT;

            p->keyCol = port_ppo_wide_col[w];
        }
        else if (nav == 1) p->keyCol = (p->keyCol + 1) % PORT_PPO_KEY_COLS;
        else if (nav == 2) p->keyCol = (p->keyCol + PORT_PPO_KEY_COLS - 1) % PORT_PPO_KEY_COLS;
        if (nav == 3) p->keyRow = (p->keyRow + 1) % PORT_PPO_KEY_ROWS;
        if (nav == 4) p->keyRow = (p->keyRow + PORT_PPO_KEY_ROWS - 1) % PORT_PPO_KEY_ROWS;

        if (back)
        {
            if (len > 0)
            {
                p->text[len - 1] = '\0';
            }
            else
            {
                portPpoListOpen(i);
            }
        }
        else if (start)
        {
            ok = TRUE;
        }
        else if (accept)
        {
            if (p->keyRow < 4)
            {
                if (len < portProfileNameMax() && len < PORT_PPO_NAME_BUF - 1)
                {
                    char ch = port_ppo_keys[p->keyRow][p->keyCol];

                    if (!p->caps && ch >= 'A' && ch <= 'Z')
                    {
                        ch = (char) (ch - 'A' + 'a');
                    }
                    p->text[len] = ch;
                    p->text[len + 1] = '\0';
                }
            }
            else if (portPpoWideAt(p->keyCol) == PORT_PPO_WIDE_DEL)
            {
                if (len > 0)
                {
                    p->text[len - 1] = '\0';
                }
            }
            else if (portPpoWideAt(p->keyCol) == PORT_PPO_WIDE_CAPS)
            {
                p->caps = !p->caps;
            }
            else
            {
                ok = TRUE;
            }
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        if (ok && portPpoNameUsable(p->text) && portProfileSave(i, p->text))
        {
            p->view = PORT_PPO_VIEW_ROWS;
        }
        else if (ok)
        {   /* refused: say why for two seconds, in place of the heading */
            extern unsigned long long portProfNow(void);
            const s32 seat = portProfileSeatOf(p->text);

            if (!portPpoNameUsable(p->text))
            {
                snprintf(p->keyMsg, sizeof(p->keyMsg), "Type a Name");
            }
            else if (seat >= 0 && seat != i)
            {
                snprintf(p->keyMsg, sizeof(p->keyMsg), "In Use by P%d", (int) seat + 1);
            }
            else
            {
                snprintf(p->keyMsg, sizeof(p->keyMsg), "Could Not Save");
            }
            p->keyMsgUntil = portProfNow() + 2000000ull;
        }
    }
}

static void portPpoInterface(void)
{
    const s32 n = get_selected_num_players();
    s32 ndone = 0;
    s32 i;

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    frontUpdateControlStickPosition();

    if (port_ppo_phase == PORT_PPO_SEATING)
    {   /* an armed seat OWNS the tick: every pad is blanked while it listens */
        if (portInputAssignTick())
        {
            portInputAssignTake();   /* a pad, a skip or a timeout: all move on */
            port_ppo_exclude |= (1 << port_ppo_seat);   /* whatever they hold now is theirs */
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            portPpoSeatNext(n);
        }
        return;
    }

    for (i = 0; i < n; i++)
    {
        portPpoPanelTick(i);
        if (port_ppo[i].done)
        {
            ndone++;
        }
    }
    if (portMpWaitScreenExitPressed(n))
    {
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }
    else if (ndone == n)
    {
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }
}

/* text centred on cx; the frontend font */
static Gfx *portPpoPrintCentred(Gfx *DL, s32 cx, s32 y, const char *text, s32 alpha)
{
    s32 th, tw;
    s32 x;

    textMeasure(&th, &tw, (u8 *) text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = cx - (tw >> 1);
    DL = microcode_constructor(DL);
    return frontPrintText(DL, &x, &y, (s8 *) text, ptrFontZurichBoldChars, ptrFontZurichBold, alpha, viGetX(), viGetY(), 0, 0);
}

static Gfx *portPpoDrawRows(Gfx *DL, s32 i, s32 px, s32 py, s32 pw)
{
    const struct PortPpoPanel *p = &port_ppo[i];
    s32 row;

    for (row = 0; row < PORT_PPO_ROW_COUNT; row++)
    {
        const s32 y0 = py + PORT_PPO_MAIN_Y0 + row * PORT_PPO_MAIN_PITCH;
        const s32 grey = p->done || portPpoRowDisabled(i, row);
        const char *label = (row == PORT_PPO_ROW_DONE && p->done) ? "Ready\n" : portPpoRowLabel(row);
        const char *value = portPpoRowValue(i, row);
        s32 th, lw, vw;
        s32 x, y;

        if (row == p->row && !p->done)
        {
            DL = microcode_constructor_related_to_menus(DL, px + 7, y0 - 1, px + pw - 7, y0 + PORT_PPO_MAIN_PITCH - 1, 0x32);
        }
        textMeasure(&th, &lw, (u8 *) label, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        x = px + PORT_PPO_PAD_X;
        y = y0;
        DL = microcode_constructor(DL);
        DL = frontPrintText(DL, &x, &y, (s8 *) label, ptrFontZurichBoldChars, ptrFontZurichBold, grey ? 0x70 : 0xFF, viGetX(), viGetY(), 0, 0);
        if (value[0] != '\0')
        {   /* RIGHT-ALIGNED, and never over its label: a long bind config
             * name starts just past the label and runs off the panel's edge
             * (the panel's scissor cuts it) rather than being drawn on top */
            textMeasure(&th, &vw, (u8 *) value, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
            x = px + pw - PORT_PPO_PAD_X - vw;
            if (x < px + PORT_PPO_PAD_X + lw + 8)
            {
                x = px + PORT_PPO_PAD_X + lw + 8;
            }
            y = y0;
            DL = microcode_constructor(DL);
            DL = frontPrintText(DL, &x, &y, (s8 *) value, ptrFontZurichBoldChars, ptrFontZurichBold, grey ? 0x70 : 0xFF, viGetX(), viGetY(), 0, 0);
        }
    }
    return DL;
}

static Gfx *portPpoDrawList(Gfx *DL, s32 i, s32 px, s32 py, s32 pw)
{
    const struct PortPpoPanel *p = &port_ppo[i];
    const s32 n = portPpoListCount(i);
    s32 line;

    for (line = 0; line < PORT_PPO_LIST_LINES && p->listTop + line < n; line++)
    {
        const s32 idx = p->listTop + line;
        const s32 y0 = py + PORT_PPO_ROW_Y0 + line * PORT_PPO_ROW_PITCH;
        s32 x = px + PORT_PPO_PAD_X;
        s32 y = y0;

        if (idx == p->listPick)
        {
            DL = microcode_constructor_related_to_menus(DL, px + 7, y0 - 1, px + pw - 7, y0 + PORT_PPO_ROW_PITCH - 1, 0x32);
        }
        DL = microcode_constructor(DL);
        DL = frontPrintText(DL, &x, &y, (s8 *) portPpoListText(i, idx), ptrFontZurichBoldChars, ptrFontZurichBold,
                            portPpoListGrey(i, idx) ? 0x70 : 0xFF, viGetX(), viGetY(), 0, 0);
    }
    return DL;
}

/* "1h 05m" / "4m 09s" */
static void portPpoStatsTime(char *out, size_t n, s32 secs)
{
    if (secs >= 3600)
    {
        snprintf(out, n, "%dh %02dm\n", (int) (secs / 3600), (int) ((secs / 60) % 60));
    }
    else
    {
        snprintf(out, n, "%dm %02ds\n", (int) (secs / 60), (int) (secs % 60));
    }
}

/* n * 100 / d as "42%", "-" when there is nothing to divide by */
static void portPpoStatsPct(char *out, size_t n, s32 num, s32 den)
{
    if (den <= 0)
    {
        snprintf(out, n, "-\n");
        return;
    }
    num = (num > den) ? den : num;   /* shotgun pellets: hits can outnumber trigger pulls */
    snprintf(out, n, "%d%%\n", (int) ((num * 100 + den / 2) / den));
}

/* THE STATS PAGE: eight label / value lines in the rows' places, two pages */
static Gfx *portPpoDrawStats(Gfx *DL, s32 i, s32 px, s32 py, s32 pw)
{
    const struct PortPpoPanel *p = &port_ppo[i];
    const s32 *v = p->stats;
    const s32 hits = v[PORT_MPSTAT_HEAD] + v[PORT_MPSTAT_BODY] + v[PORT_MPSTAT_LIMB] + v[PORT_MPSTAT_OTHER];
    const s32 finished = v[PORT_MPSTAT_MATCHES] - v[PORT_MPSTAT_ABANDONED];
    char val[8][24];
    const char *label[8];
    s32 line;

    if (p->statsPage == 0)
    {
        label[0] = "Matches\n";     snprintf(val[0], sizeof(val[0]), "%d\n", (int) v[PORT_MPSTAT_MATCHES]);
        label[1] = "Wins\n";        snprintf(val[1], sizeof(val[1]), "%d\n", (int) v[PORT_MPSTAT_WINS]);
        label[2] = "Win Rate\n";    portPpoStatsPct(val[2], sizeof(val[2]), v[PORT_MPSTAT_WINS], finished);
        label[3] = "Abandoned\n";   snprintf(val[3], sizeof(val[3]), "%d\n", (int) v[PORT_MPSTAT_ABANDONED]);
        label[4] = "Kills\n";       snprintf(val[4], sizeof(val[4]), "%d\n", (int) v[PORT_MPSTAT_KILLS]);
        label[5] = "Deaths\n";      snprintf(val[5], sizeof(val[5]), "%d\n", (int) v[PORT_MPSTAT_DEATHS]);
        label[6] = "K/D\n";
        if (v[PORT_MPSTAT_DEATHS] > 0)
        {
            snprintf(val[6], sizeof(val[6]), "%.2f\n", (double) v[PORT_MPSTAT_KILLS] / (double) v[PORT_MPSTAT_DEATHS]);
        }
        else
        {
            snprintf(val[6], sizeof(val[6]), (v[PORT_MPSTAT_KILLS] > 0) ? "%d.00\n" : "-\n", (int) v[PORT_MPSTAT_KILLS]);
        }
        label[7] = "Time Played\n"; portPpoStatsTime(val[7], sizeof(val[7]), v[PORT_MPSTAT_SECONDS]);
    }
    else if (p->statsPage == 2)
    {   /* AWARDS: damage dealt / taken, then every award, nineteen lines through an
         * eight-line window (up / down scroll). The names are Rare's endscreen
         * strings (LmpmenuE), in its AWARD_* bit order. */
        static const char *const awardName[PORT_MPSTAT_AWARDS] = {
            "Lemming Award\n", "Where's the Ammo?\n", "Where's the Armor?\n", "AC -10 Award\n",
            "Marksmanship Award\n", "Most Professional\n", "Most Deadly\n", "Mostly Harmless\n",
            "Most Cowardly\n", "Most Frantic\n", "Most Honorable\n", "Most Dishonorable\n",
            "Shortest Innings\n", "Longest Innings\n", "Double Kill\n", "Triple Kill\n", "Quad Kill (Doh!)\n",
        };

        for (line = 0; line < 8; line++)
        {
            const s32 e = p->statsScroll + line;

            if (e == 0)
            {
                label[line] = "Damage Dealt\n";
                snprintf(val[line], sizeof(val[line]), "%d\n", (int) v[PORT_MPSTAT_DMG_DEALT]);
            }
            else if (e == 1)
            {
                label[line] = "Damage Taken\n";
                snprintf(val[line], sizeof(val[line]), "%d\n", (int) v[PORT_MPSTAT_DMG_TAKEN]);
            }
            else if (e - 2 < PORT_MPSTAT_AWARDS)
            {
                label[line] = awardName[e - 2];
                snprintf(val[line], sizeof(val[line]), "%d\n", (int) v[PORT_MPSTAT_AWARD0 + e - 2]);
            }
            else
            {
                label[line] = "\n";
                val[line][0] = '\n';
                val[line][1] = '\0';
            }
        }
        if (p->statsScroll > 0 || p->statsScroll + 8 < 2 + PORT_MPSTAT_AWARDS)
        {   /* more above / below: the drop-down's three narrowing bars, at the panel's right */
            const s32 ax = px + pw - 7;   /* right of the values (they end PORT_PPO_PAD_X in) */
            const s32 y0 = py + PORT_PPO_ROW_Y0;
            const s32 y1 = py + PORT_PPO_ROW_Y0 + 7 * PORT_PPO_ROW_PITCH + 10;
            s32 k;

            for (k = 0; k < 3; k++)
            {
                if (p->statsScroll > 0)
                {
                    DL = microcode_constructor_related_to_menus(DL, ax + k, y0 - 4 + k, ax + 6 - k, y0 - 3 + k, 0x000000FF);
                }
                if (p->statsScroll + 8 < 2 + PORT_MPSTAT_AWARDS)
                {
                    DL = microcode_constructor_related_to_menus(DL, ax + k, y1 + 2 - k, ax + 6 - k, y1 + 3 - k, 0x000000FF);
                }
            }
            DL = microcode_constructor(DL);
        }
    }
    else
    {
        label[0] = "Suicides\n";     snprintf(val[0], sizeof(val[0]), "%d\n", (int) v[PORT_MPSTAT_SUICIDES]);
        label[1] = "Shots\n";        snprintf(val[1], sizeof(val[1]), "%d\n", (int) v[PORT_MPSTAT_SHOTS]);
        label[2] = "Accuracy\n";     portPpoStatsPct(val[2], sizeof(val[2]), hits, v[PORT_MPSTAT_SHOTS]);
        label[3] = "Head Hits\n";    portPpoStatsPct(val[3], sizeof(val[3]), v[PORT_MPSTAT_HEAD], hits);
        label[4] = "Body Hits\n";    portPpoStatsPct(val[4], sizeof(val[4]), v[PORT_MPSTAT_BODY], hits);
        label[5] = "Limb Hits\n";    portPpoStatsPct(val[5], sizeof(val[5]), v[PORT_MPSTAT_LIMB], hits);
        label[6] = "Best Streak\n";  snprintf(val[6], sizeof(val[6]), "%d\n", (int) v[PORT_MPSTAT_STREAK]);
        label[7] = "Longest Life\n"; portPpoStatsTime(val[7], sizeof(val[7]), v[PORT_MPSTAT_LONGESTLIFE]);
    }
    for (line = 0; line < 8; line++)
    {
        const s32 y0 = py + PORT_PPO_ROW_Y0 + line * PORT_PPO_ROW_PITCH;
        s32 th, vw;
        s32 x = px + PORT_PPO_PAD_X;
        s32 y = y0;

        DL = microcode_constructor(DL);
        DL = frontPrintText(DL, &x, &y, (s8 *) label[line], ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        textMeasure(&th, &vw, (u8 *) val[line], ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        x = px + pw - PORT_PPO_PAD_X - vw;
        y = y0;
        DL = microcode_constructor(DL);
        DL = frontPrintText(DL, &x, &y, (s8 *) val[line], ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }
    return DL;
}

static Gfx *portPpoDrawKeys(Gfx *DL, s32 i, s32 px, s32 py, s32 pw)
{
    const struct PortPpoPanel *p = &port_ppo[i];
    const s32 gx = px + (pw >> 1) - ((PORT_PPO_KEY_COLS * PORT_PPO_KEY_W) >> 1);
    const s32 gy = py + PORT_PPO_KEY_Y0;
    char field[PORT_PPO_NAME_BUF + 4];
    s32 r, c;

    /* the name so far, with its caret, in a darker well over the keys */
    DL = microcode_constructor_related_to_menus(DL, gx, py + PORT_PPO_ROW_Y0 - 1, gx + PORT_PPO_KEY_COLS * PORT_PPO_KEY_W,
            py + PORT_PPO_ROW_Y0 + 15, 0x00000040);
    snprintf(field, sizeof(field), "%s_\n", p->text);
    {
        s32 x = gx + 3;
        s32 y = py + PORT_PPO_ROW_Y0;

        DL = microcode_constructor(DL);
        DL = frontPrintText(DL, &x, &y, (s8 *) field, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }
    for (r = 0; r < PORT_PPO_KEY_ROWS; r++)
    {
        for (c = 0; c < PORT_PPO_KEY_COLS; c++)
        {
            const s32 wide = (r == 4);
            const s32 span = wide ? port_ppo_wide_span[portPpoWideAt(c)] : 1;
            const s32 x0 = gx + c * PORT_PPO_KEY_W;
            const s32 y0 = gy + r * PORT_PPO_KEY_H;
            const s32 hot = (r == p->keyRow) && (wide ? (portPpoWideAt(c) == portPpoWideAt(p->keyCol)) : (c == p->keyCol));
            /* CAPS stays lit RED while it is on, brighter under the selector:
             * the small-font keys cannot show their case, so the button is
             * the only thing that can (the save-config keyboard's rule) */
            const s32 capslit = wide && portPpoWideAt(c) == PORT_PPO_WIDE_CAPS && p->caps;
            char label[8];
            s32 th, tw;
            s32 x, y;

            if (wide && c != port_ppo_wide_col[portPpoWideAt(c)])
            {
                continue;   /* a wide button is drawn once, at its start */
            }
            if (wide)
            {
                snprintf(label, sizeof(label), "%s", port_ppo_wide_text[portPpoWideAt(c)]);
            }
            else if (port_ppo_keys[r][c] == ' ')
            {
                snprintf(label, sizeof(label), "SP\n");   /* a blank cell reads as a gap in the grid, not as a key */
            }
            else
            {   /* caps off: the small font draws a lower-case letter as a
                 * SMALLER capital, so the grid does shrink to say so */
                char ch = port_ppo_keys[r][c];

                if (!p->caps && ch >= 'A' && ch <= 'Z')
                {
                    ch = (char) (ch - 'A' + 'a');
                }
                snprintf(label, sizeof(label), "%c\n", ch);
            }
            if (hot || capslit)
            {
                DL = microcode_constructor_related_to_menus(DL, x0, y0 - 1, x0 + span * PORT_PPO_KEY_W - 2,
                        y0 + PORT_PPO_KEY_H - 2,
                        capslit ? (hot ? PORT_OSK_COL_CAPS_HOT : PORT_OSK_COL_CAPS) : PORT_SHEET_COL_LIST_HOT);
            }
            /* the small font, as the save-config keyboard uses (PORT_OSK_CHARS):
             * its glyphs fit a 14-wide cell, the frontend font's W does not */
            textMeasure(&th, &tw, (u8 *) label, PORT_OSK_CHARS, PORT_OSK_FONT, 0);
            x = x0 + ((span * PORT_PPO_KEY_W - 2 - tw) >> 1);
            y = portOskInkY(y0 - 1, PORT_PPO_KEY_H - 1, (label[0] == ' ') ? 'A' : label[0]);
            DL = microcode_constructor(DL);
            DL = frontPrintText(DL, &x, &y, (s8 *) label, PORT_OSK_CHARS, PORT_OSK_FONT, (hot || capslit) ? -1 : 0xFF, viGetX(), viGetY(), 0, 0);
        }
    }
    return DL;
}

/* constructor_menu0F_mpcharsel's body for one player, in that player's panel
 * (the rectangles are the same ones). */
static Gfx *portPpoDrawChar(Gfx *DL, s32 i, s32 px, s32 py, s32 pw)
{
    const s32 cx = px + (pw >> 1);
    const s32 prev = mp_char_prev_select_player[i];
    const s32 slide = cx - mp_char_select_scroll_offset[i];
    const s32 size = size_mp_select_image_player[i];
    s32 k;

    if (player_has_selected_char[i] == 0 && size == 0)
    {
        DL = portPpoPrintCentred(DL, cx, py + 5, (const char *) langGet(getStringID(LTITLE, TITLE_STR_85_SELECTCHARACTER)), 0xFF);
    }
    DL = portPpoPrintCentred(DL, cx, py + 0x46 + 0x32, portMpCharName(portMpCharAt(mp_char_cur_select_player[i])), 0xFF);
    DL = frontRenderCharacterPortrait(DL, px + 0xD, (px + pw) - 0xE, slide, py + 0x46, portMpCharAt(prev), size);
    if ((player_has_selected_char[i] == 0 && size == 0) || prev != mp_char_cur_select_player[i])
    {   /* the neighbours, three each side */
        for (k = -3; k <= 3; k++)
        {
            if (k != 0 && prev + k >= 0 && prev + k < num_chars_selectable_mp)
            {
                DL = frontRenderCharacterPortrait(DL, px + 0xD, (px + pw) - 0xE, slide + k * 0x54, py + 0x46, portMpCharAt(prev + k), size);
            }
        }
    }
    return DL;
}

static Gfx *portPpoDraw(Gfx *DL)
{
    const s32 n = get_selected_num_players();
    s32 i;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);
    /* Rare's dividers, as the control style screen drew them */
    DL = microcode_constructor_related_to_menus(DL, 0x26, 0xA9, 0x184, 0xAB, 0x90);
    if (n >= 3)
    {
        DL = microcode_constructor_related_to_menus(DL, 0xD4, 0x1E, 0xD6, 0x136, 0x80);
    }

    for (i = 0; i < n; i++)
    {
        const struct PortPpoPanel *p = &port_ppo[i];
        char head[48];
        s32 px, py, pw;
        s32 cx;

        portPpoPanelRect(i, n, &px, &py, &pw);
        cx = px + (pw >> 1);
        gDPSetScissor(DL++, G_SC_NON_INTERLACE, px + 6, py + 5, px + pw - 6, py + PORT_PPO_PANEL_H - 5);

        if (port_ppo_phase == PORT_PPO_SEATING)
        {
            const s32 dev = portInputSlotDevice(i);

            snprintf(head, sizeof(head), "Player %d\n", (int) i + 1);
            DL = portPpoPrintCentred(DL, cx, py + PORT_PPO_HEAD_Y, head, 0xFF);
            if (i == port_ppo_seat)
            {
                DL = portPpoPrintCentred(DL, cx, py + 46, "Press a Button\n", 0xFF);
                DL = portPpoPrintCentred(DL, cx, py + 62, "on Your Controller\n", 0xFF);
                DL = portPpoPrintCentred(DL, cx, py + 92, "Player 1 Skips\n", 0x70);
            }
            else if (i == PLAYER_1)
            {
                DL = portPpoPrintCentred(DL, cx, py + 54,
                        (port_ppo_p1pad && dev > 0) ? portInputDeviceLabel(dev) : "Keyboard and Mouse\n", 0x70);
            }
            else if (i < port_ppo_seat)
            {
                DL = portPpoPrintCentred(DL, cx, py + 54, (dev > 0) ? portInputDeviceLabel(dev) : "No Input\n", 0x70);   /* "No Input", the folder's word */
            }
            else
            {
                DL = portPpoPrintCentred(DL, cx, py + 54, "Waiting\n", 0x70);
            }
            continue;
        }

        if (p->view == PORT_PPO_VIEW_CHAR)
        {   /* the carousel has its own heading */
            DL = portPpoDrawChar(DL, i, px, py, pw);
            continue;
        }
        if (p->view == PORT_PPO_VIEW_KEYS)
        {
            extern unsigned long long portProfNow(void);

            if (p->keyMsg[0] != '\0' && portProfNow() < p->keyMsgUntil)
            {
                snprintf(head, sizeof(head), "%s\n", p->keyMsg);
            }
            else
            {
                snprintf(head, sizeof(head), "Player %d Name\n", (int) i + 1);
            }
        }
        else if (p->view == PORT_PPO_VIEW_LIST && p->listMode == PORT_PPO_LIST_LOAD)
        {
            snprintf(head, sizeof(head), "Load Profile\n");
        }
        else if (p->view == PORT_PPO_VIEW_LIST && p->listMode == PORT_PPO_LIST_DELWHICH)
        {
            snprintf(head, sizeof(head), "Delete Which?\n");
        }
        else if (p->view == PORT_PPO_VIEW_LIST && p->listMode == PORT_PPO_LIST_DELSURE)
        {
            snprintf(head, sizeof(head), "Delete %s?\n", p->delName);
        }
        else if (p->view == PORT_PPO_VIEW_LIST)
        {
            snprintf(head, sizeof(head), "Player %d Profile\n", (int) i + 1);
        }
        else if (p->view == PORT_PPO_VIEW_STATS)
        {
            /* no name in 3P / 4P: a quadrant has no room for it */
            snprintf(head, sizeof(head), "%s%s%s %d/%d\n",
                     (get_selected_num_players() >= 3) ? "" : portProfileActive(i),
                     (get_selected_num_players() >= 3) ? "" : " ",
                     (p->statsPage == 0) ? "Record" : (p->statsPage == 1) ? "Shooting" : "Awards",
                     (int) p->statsPage + 1, PORT_PPO_STATS_PAGES);
        }
        else if (portProfileActive(i)[0] != '\0')
        {
            snprintf(head, sizeof(head), "%s\n", portProfileActive(i));
        }
        else
        {
            snprintf(head, sizeof(head), "Player %d\n", (int) i + 1);
        }
        DL = portPpoPrintCentred(DL, cx, py + PORT_PPO_HEAD_Y, head, 0xFF);
        if (p->view == PORT_PPO_VIEW_KEYS)
        {
            DL = portPpoDrawKeys(DL, i, px, py, pw);
        }
        else if (p->view == PORT_PPO_VIEW_LIST)
        {
            DL = portPpoDrawList(DL, i, px, py, pw);
        }
        else if (p->view == PORT_PPO_VIEW_STATS)
        {
            DL = portPpoDrawStats(DL, i, px, py, pw);
        }
        else
        {
            DL = portPpoDrawRows(DL, i, px, py, pw);
        }
    }

    DL = combiner_bayer_lod_perspective(DL);
    return DL;
}
#endif /* !TARGET_N64 */

//********************************************************************************************************
//MULTIPLAYER CONTROL OPTIONS
//********************************************************************************************************
void init_menu11_mpcontrol(void)
{
    s32 i;
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;

    for (i = 0; i < MAX_PLAYER_COUNT; i++) {
        player_has_selected_char[i] = FALSE;
        dword_CODE_bss_80069760[i] = 0;
    }

    load_walletbond();
#ifndef TARGET_N64
    portPpoInit();   /* PORT: this menu is PLAYER OPTIONS - see the block above */
#endif
}

void update_menu11_mpcontrols(void) {
    return;
}






// Address 0x7F013608 NTSC
// Address 0x7F013648 NTSC-J
void interface_menu11_mpcontrols(void)
{
    s32 sp44;
    s32 i;
    s32 var_fp;

    sp44 = get_selected_num_players();
    var_fp = 0;
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

#ifndef TARGET_N64
    /* PORT: this menu is PLAYER OPTIONS and Rare's control
     * style picker below is not reached. A player's style is their Style row
     * now - [input] padN_style, which already outranked this screen's
     * controlstyle_player[] (options.c cur_player_get_control_type). */
    portPpoInterface();
    return;
#endif

#if defined(BUGFIX_R1)
    #define MAYBE_OR_JOYCOUNT_NOT_4 || joyGetControllerCount() != 4
    #define MAYBE_AND_JOYCOUNT_4 && joyGetControllerCount() == 4
#else
    #define MAYBE_OR_JOYCOUNT_NOT_4
    #define MAYBE_AND_JOYCOUNT_4
#endif


    for (i = 0; i < sp44; i++)
    {
        if (player_has_selected_char[i] && joyGetButtonsPressedThisFrame(i, B_BUTTON))
        {
            player_has_selected_char[i] = 0;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }

        if (player_has_selected_char[i] == 0)
        {
            // 0x202
            if (joyGetButtonsPressedThisFrame(i, L_JPAD|L_CBUTTONS) || ((joyGetStickXInRange(i, -2, 1) < -1) && dword_CODE_bss_80069760[i] != 0))
            {
                // if greater than 0
                if (controlstyle_player[i] > CONTROLLER_CONFIG_HONEY)
                {
                    /* TWIN STICK (joy_port.c portJoyTwinStickCapable): the
                     * N64's 2.x two-pad coupling below exists because a 2.x
                     * player uses a second pad slot. A controller with its own
                     * right stick uses none, so any such player can pick 1.1 ..
                     * 2.4 freely; other pads keep the vanilla rules. */
                    // ... OR PLAYER_1 is 1.x OR controlstyle_player[i] is 1 + 2.x
                    if ((sp44 != 2) || (i != PLAYER_2) MAYBE_OR_JOYCOUNT_NOT_4 || (controlstyle_player[PLAYER_1] < CONTROLLER_CONFIG_PLENTY) || (controlstyle_player[i] >= CONTROLLER_CONFIG_GALORE)
#ifndef TARGET_N64
                        || portJoyTwinStickCapable(PLAYER_1) || portJoyTwinStickCapable(i)
#endif
                        )
                    {
                        controlstyle_player[i]--;
                    }

                    // ... AND PLAYER_1 is 1.x AND PLAYER_2 is 2.x
                    if ((sp44 == 2) && (i == PLAYER_1) MAYBE_AND_JOYCOUNT_4 && (controlstyle_player[PLAYER_1] < CONTROLLER_CONFIG_PLENTY) && (controlstyle_player[PLAYER_2] >= CONTROLLER_CONFIG_PLENTY)
#ifndef TARGET_N64
                        && !portJoyTwinStickCapable(PLAYER_2)
#endif
                        )
                    {
                        // PLAYER_2 is 1.1
                        controlstyle_player[PLAYER_2] = CONTROLLER_CONFIG_HONEY;
                    }
                }
            }
            // 0x101
            else if (joyGetButtonsPressedThisFrame(i, R_JPAD|R_CBUTTONS) || ((joyGetStickXInRange(i, -2, 1) > 0) && dword_CODE_bss_80069760[i] != 0))
            {
                // ... controlstyle_player[i] is 2.x
                if (((sp44 == 2) && (i == PLAYER_1) MAYBE_AND_JOYCOUNT_4) || controlstyle_player[i] >= CONTROLLER_CONFIG_PLENTY
#ifndef TARGET_N64
                    || portJoyTwinStickCapable(i)
#endif
                    )
                {
                    // if controlstyle_player[i] < 2.4
                    if (controlstyle_player[i] < CONTROLLER_CONFIG_GOODHEAD)
                    {
                        controlstyle_player[i]++;

                        // ... AND PLAYER_1 is 2.1 AND PLAYER_2 is 1.x
                        if ((i == PLAYER_1) && (controlstyle_player[PLAYER_1] == CONTROLLER_CONFIG_PLENTY) && (controlstyle_player[PLAYER_2] < CONTROLLER_CONFIG_PLENTY)
#ifndef TARGET_N64
                            && !portJoyTwinStickCapable(PLAYER_1)
#endif
                            )
                        {
                            // PLAYER_2 is 2.1
                            controlstyle_player[PLAYER_2] = CONTROLLER_CONFIG_PLENTY;
                        }
                    }
                }
                // else if controlstyle_player[i] < 1.45
                else if (controlstyle_player[i] < CONTROLLER_CONFIG_GOODNIGHT)
                {
                    controlstyle_player[i]++;
                }

            }
            else if (joyGetButtonsPressedThisFrame(i, A_BUTTON|Z_TRIG|START_BUTTON)) // 0xb000
            {
                player_has_selected_char[i] = 1;
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }

#if defined(VERSION_JP) || defined(VERSION_EU)
            if (joyGetControllerCount() != 4)
            {
                if (controlstyle_player[PLAYER_1] >= CONTROLLER_CONFIG_PLENTY)
                {
                    controlstyle_player[PLAYER_1] = CONTROLLER_CONFIG_HONEY;
                }

                if (controlstyle_player[PLAYER_2] >= CONTROLLER_CONFIG_PLENTY)
                {
                    controlstyle_player[PLAYER_2] = CONTROLLER_CONFIG_HONEY;
                }
            }
#endif

            if ((joyGetStickXInRange(i, -2, 1) == -1) || (joyGetStickXInRange(i, -2, 1) == 0))
            {
                dword_CODE_bss_80069760[i] = 1;
            }
            else
            {
                dword_CODE_bss_80069760[i] = 0;
            }
        }

        if (player_has_selected_char[i] != 0)
        {
            var_fp++;
        }
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    frontUpdateControlStickPosition();

#ifndef TARGET_N64
    if (portMpWaitScreenExitPressed(sp44))
    {
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }
    else
#endif
    if (var_fp == sp44)
    {
        frontChangeMenu(MENU_MP_OPTIONS, 0);
    }

#undef MAYBE_OR_JOYCOUNT_NOT_4
#undef MAYBE_AND_JOYCOUNT_4
}



// Address 0x7F013980 NTSC
Gfx * constructor_menu11_mpcontrol(Gfx *DL)
{
    s32 spCC;
    s32 i; // spE0
    u8 *text;
    s32 var_s3;
    s32 var_s5;
    s32 var_v1;
    u8 *text2;
    s32 padding4;
    s32 padding1;
    s32 var_s2;
    s32 spA4;
    s32 spA0;
    s32 sp9C;
    s32 sp98;
    s32 padding2;
    s32 sp90;
    s32 sp8C;
    s32 sp88;
    s32 sp84;
    s32 padding5;

    spCC = get_selected_num_players();

#ifndef TARGET_N64
    return portPpoDraw(DL);   /* PORT: PLAYER OPTIONS - see interface_menu11_mpcontrols */
#endif

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);
    DL = microcode_constructor_related_to_menus(DL, 0x26, 0xA9, 0x184, 0xAB, 0x90);

    if (spCC >= 3)
    {
        DL = microcode_constructor_related_to_menus(DL, 0xD4, 0x1E, 0xD6, 0x136, 0x80);
    }

    for (i = 0; i < spCC; i++)
    {
        padding4 = player_has_selected_char[i];
        padding1 = controlstyle_player[i];

        var_s2 = 0;

        if (spCC == 2)
        {
            var_s3 = 0x26;
            var_s5 = 0x15E;
            var_v1 = (i > 0) ? 0x8C : 0;
            var_s2 = (var_v1) + 0x1E;
        }
        else
        {
            var_s5 = 0xAF;
            var_v1 = (i >= 2) ? 0x8C : 0;
            var_s2 = (var_v1) + 0x1E;
            var_v1 = (i & 1) ? 0xAF : 0;
            var_s3 = (var_v1) + 0x26;
        }

        gDPSetScissor(DL++, G_SC_NON_INTERLACE, (var_s3 + 6), var_s2 + 5, (var_s3 + var_s5 - 6), (var_s2 + 0x8C - 5));

        padding2 = ((var_s5 >> 1) + var_s3);
        padding5 = var_s2 + 0x46;

        if (player_has_selected_char[i] == 0)
        {
            text = langGet(getStringID(LTITLE, TITLE_STR_285_SELECTCONTROLSTYLE));
            textMeasure(&spA4, &spA0, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

            sp9C = padding2 - (spA0 >> 1);
            sp98 = padding5 - (spA4 >> 1) - 0xf;

            DL = microcode_constructor(DL);
            DL = frontPrintText(DL, &sp9C, &sp98, (s8*)text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }

        {
            text2 = langGet(MP_controller_configuration_table[controlstyle_player[i]].text_preset);
            textMeasure(&sp90, &sp8C, text2, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
            sp88 = padding2 - (sp8C >> 1);
            sp84 = padding5 - (sp90 >> 1) + 0xf;

            DL = microcode_constructor(DL);
            DL = frontPrintText(DL, &sp88, &sp84, (s8*)text2, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }
    }

    DL = combiner_bayer_lod_perspective(DL);

    return DL;
}



//********************************************************************************************************
//MULTIPLAYER STAGE SELECT
//********************************************************************************************************
#ifndef TARGET_N64
/* PORT: THE ARENA GRID is the solo mission screen's: its film strip
 * (SW_SLIDES, 5 x 4 frames centred on cursor_x/ypos_table_mission_select)
 * with Rare's MP photos drawn in the frames instead of the solo ones
 * (SW_PICS). Both photo sets are 68 x 44, so they fill the frames exactly.
 * Arena n sits in frame n, row by row; the 4 x 3 grid had no room for the cut
 * arenas. A frame past the last arena lights nothing. */
#define PORT_MPSTAGE_COLS 5
#define PORT_MPSTAGE_ROWS 4

static void portMpStageInterface(void)
{
    s32 col, row, idx;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    if (joyGetButtons(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON) == 0)
    {
        tab_prev_highlight = FALSE;
        tab_next_highlight = FALSE;
        tab_start_highlight = FALSE;
        current_mp_stage_highlighted = -1;
        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = TRUE;
        }
        else
        {   /* the nearest frame, as interface_menu07_missionsel finds it */
            for (col = 0; col < PORT_MPSTAGE_COLS - 1
                 && (cursor_xpos_table_mission_select[col] + cursor_xpos_table_mission_select[col + 1]) * 0.5f <= cursor_h_pos; col++)
            {
            }
            for (row = 0; row < PORT_MPSTAGE_ROWS - 1
                 && (cursor_ypos_table_mission_select[row] + cursor_ypos_table_mission_select[row + 1]) * 0.5f <= cursor_v_pos; row++)
            {
            }
            idx = row * PORT_MPSTAGE_COLS + col;
            if (idx < MP_STAGE_SELECTED_MAX && check_if_mp_stage_unlocked(idx))
            {
                current_mp_stage_highlighted = idx;
            }
        }
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {
        if (tab_prev_highlight)
        {
            tab_prev_selected = TRUE;
        }
        else if (current_mp_stage_highlighted >= MP_STAGE_RANDOM)
        {
            tab_next_selected = TRUE;
            MP_stage_selected = current_mp_stage_highlighted;
        }
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_SLIDES, 1);   /* the film strip, not its solo photos */
    frontUpdateControlStickPosition();
    if (tab_next_selected || tab_prev_selected)
    {
        frontChangeMenu(MENU_MP_OPTIONS, FALSE);
    }
}

static Gfx *portMpStageConstruct(Gfx *DL)
{
    struct sImageTableEntry *simage;
    struct coord2d pos;
    struct coord2d half;
    s32 i;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);
    DL = combiner_bayer_lod_perspective(DL);
    gDPSetTextureFilter(DL++, G_TF_POINT);

    half.f[0] = 34.0f;   /* constructor_menu12_mpstage's photo, 0x44 x 0x2C */
    half.f[1] = 22.0f;
    for (i = 0; i < MP_STAGE_SELECTED_MAX; i++)
    {
        simage = &mpstageselimages[multi_stage_setups[i].photo];
        pos.f[0] = (f32) cursor_xpos_table_mission_select[i % PORT_MPSTAGE_COLS];
        pos.f[1] = (f32) cursor_ypos_table_mission_select[i / PORT_MPSTAGE_COLS];
        texSelect(&DL, simage, 1, 0, 2);
        if (i == current_mp_stage_highlighted)
        {
            gDPSetCycleType(DL++, G_CYC_2CYCLE);
            gDPSetFogColor(DL++, 0xff, 0xff, 0xff, 0x0a);
            gDPSetRenderMode(DL++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
            display_image_at_position(&DL, &pos.f[0], &half.f[0], 0x44, 0x2C, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, simage->level > 0, 1);
        }
        else if (check_if_mp_stage_unlocked(i))
        {
            display_image_at_position(&DL, &pos.f[0], &half.f[0], 0x44, 0x2C, 0, 0, 1, 0x6E, 0x6E, 0x6E, 0xFF, simage->level > 0, 0);
        }
        else
        {
            display_image_at_position(&DL, &pos.f[0], &half.f[0], 0x44, 0x2C, 0, 0, 1, 0xF, 0xF, 0xF, 0xFF, simage->level > 0, 0);
        }
    }

    DL = microcode_constructor(DL);
    for (i = 0; i < MP_STAGE_SELECTED_MAX; i++)
    {   /* the name along the photo's foot, as constructor_menu07_missionsel sets it */
        u8 name[32];
        s32 len, h = 0, w = 0, x, y;
        const s32 colour = (i == current_mp_stage_highlighted) ? -0x100 : 0x96969600;

        if (!check_if_mp_stage_unlocked(i))
        {
            continue;
        }
        snprintf((char *) name, sizeof(name) - 1, "%s", (char *) langGet(multi_stage_setups[i].select_screen_text_preset));
        for (len = 0; name[len] != '\0' && name[len] != '\n'; len++)
        {
            if (j_text_trigger == 0 && name[len] >= 'a' && name[len] <= 'z')
            {
                name[len] -= 0x20;   /* "Statue" -> "STATUE", like Rare's own MP names */
            }
        }
        name[len] = '\n';
        name[len + 1] = '\0';
        textMeasure(&h, &w, name, ptrFontBankGothicChars, ptrFontBankGothic, 0);
        x = cursor_xpos_table_mission_select[i % PORT_MPSTAGE_COLS] - 0x1F;
        y = (cursor_ypos_table_mission_select[i / PORT_MPSTAGE_COLS] - h) + 0x1D;
        DL = microcode_constructor_related_to_menus(DL, x, y, x + w, y + h, 0);
        DL = textRender(DL, &x, &y, name, ptrFontBankGothicChars, ptrFontBankGothic, colour | 0xFF, viGetX(), viGetY(), 0, 0);
        x = cursor_xpos_table_mission_select[i % PORT_MPSTAGE_COLS] - 0x1F;
        y = (cursor_ypos_table_mission_select[i / PORT_MPSTAGE_COLS] - h) + 0x1D;
        DL = textRender(DL, &x, &y, name, ptrFontBankGothicChars, ptrFontBankGothic, colour | 0x64, viGetX(), viGetY(), 0, 0);
    }

    DL = microcode_constructor(DL);
    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}
#endif

void init_menu12_mpstage(void)
{
  tab_start_selected = FALSE;
  tab_next_selected = FALSE;
  tab_prev_selected = FALSE;
  tab_prev_highlight = FALSE;
  tab_next_highlight = FALSE;
  tab_start_highlight = FALSE;
  current_mp_stage_highlighted = MP_STAGE_RANDOM;
  load_walletbond();
}

void update_menu12_mpstage(void) {
return;
}



void interface_menu12_mpstage(void)
{
    s32 row;
    s32 column;

#ifndef TARGET_N64
    portMpStageInterface();   /* PORT: the 5 x 4 arena grid (see portMpStageInterface) */
    return;
#endif
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    if (joyGetButtons(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON) == 0)
    {
        tab_prev_highlight = FALSE;
        tab_next_highlight = FALSE;
        tab_start_highlight = FALSE;
        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = TRUE;
        }
        else
        {
            if ((s32)cursor_v_pos >= 240)
            {
                row = 2;
            }
            else if ((s32)cursor_v_pos >= 170)
            {
              row = 1;
            } else
            {
                row = 0;
            }

            if ((s32) cursor_h_pos >= 292)
            {
                column = 3;
            }
            else if ((s32) cursor_h_pos >= 207)
            {
                column = 2;
            }
            else if ((s32) cursor_h_pos >= 122)
            {
                column = 1;
            }
            else
            {
                column = 0;
            }

            current_mp_stage_highlighted = (row * 4) + column;
            while (check_if_mp_stage_unlocked(current_mp_stage_highlighted) == 0)
            {
                current_mp_stage_highlighted = current_mp_stage_highlighted - 1;
            }
        }
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {
        if (tab_prev_highlight)
        {
            tab_prev_selected = TRUE;
        }
        else
        {
            if (current_mp_stage_highlighted >= MP_STAGE_RANDOM)
            {
                tab_next_selected = TRUE;
                MP_stage_selected = current_mp_stage_highlighted;
            }
        }
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS,  1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS,  1);
    frontUpdateControlStickPosition();
    if (tab_next_selected)
    {
        frontChangeMenu(MENU_MP_OPTIONS, FALSE);
        return;
    }
    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MP_OPTIONS, FALSE);
    }
}


// Address 0x7F014050 NTSC
Gfx * constructor_menu12_mpstage(Gfx *DL)
{
    s32 padding5;
    s32 sp130;
    s32 sp12C;
    s32 sp128;
    s32 sp124;
    s32 padding2[7];
    struct coord2d sp100;
    struct coord2d spF8;
    s32 padding4[3];
    s32 i_3;
    s32 i_2;
    s32 i_1;
    struct coord2d spD8;
    struct coord2d spD0;
    s32 var_s6;
    s32 var_s2_2;
    struct sImageTableEntry *simage;
    s32 count;
    s32 count_2;
    s32 padding;
    s32 spB4;

#ifndef TARGET_N64
    return portMpStageConstruct(DL);   /* PORT: the 5 x 4 arena grid */
#endif

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    for (i_1 = 0; i_1 != 3; i_1++)
    {
        DL = microcode_constructor_related_to_menus(DL, 0x25, 0x6c + (i_1 * 0x46), 0x185, 0xa0 + (i_1 * 0x46), 0x101010FF);
    }

    DL = combiner_bayer_lod_perspective(DL);

    gDPSetTextureFilter(DL++, G_TF_POINT);

    simage = mainfolderimages + 5;
    texSelect(&DL, simage, 1, 0, 2); // IMAGE_DOT

    spF8.f[0] = 176.0f;
    spF8.f[1] = 4.0f;
    sp100.f[0] = spF8.f[0] + 37.0f;
    sp100.f[1] = spF8.f[1] + 100.0f;

    for (i_2 = 0; i_2 < 3; i_2++)
    {
        display_image_at_position(&DL, &sp100.f[0], &spF8.f[0], 0x2F0, 0x12, 0, 0, 1, 0x6B, 0x67, 0x53, 0xFF, simage->level > 0, 0);
        sp100.f[1] += 60.0f;
        display_image_at_position(&DL, &sp100.f[0], &spF8.f[0], 0x2F0, 0x12, 0, 0, 1, 0x6B, 0x67, 0x53, 0xFF, simage->level > 0, 0);
        sp100.f[1] -= 60.0f;
        sp100.f[1] += 70.0f;
    }

    spD0.f[0] = 34.0f;
    spD0.f[1] = 22.0f;
    spD8.f[1] = spD0.f[1] + 108.0f + 4.0f;

    count = 0;

    for (spB4 = 0; spB4 != 3; spB4++)
    {
        spD8.f[0] = 86.0f;

        for (i_3 = 0; i_3 < 4; i_3++)
        {
            if (count < 12)
            {
                simage = &mpstageselimages[multi_stage_setups[count].photo];

                texSelect(&DL, simage, 1, 0, 2);

                if (count == current_mp_stage_highlighted)
                {
                    gDPSetCycleType(DL++, G_CYC_2CYCLE);
                    gDPSetFogColor(DL++, 0xff, 0xff, 0xff, 0x0a);
                    gDPSetRenderMode(DL++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);

                    display_image_at_position(&DL, &spD8.f[0], &spD0.f[0], 0x44, 0x2C, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, simage->level > 0, 1);
                }
                else if (check_if_mp_stage_unlocked(count) != 0)
                {
                    display_image_at_position(&DL, &spD8.f[0], &spD0.f[0], 0x44, 0x2C, 0, 0, 1, 0x6E, 0x6E, 0x6E, 0xFF, simage->level > 0, 0);
                }
                else
                {
                    display_image_at_position(&DL, &spD8.f[0], &spD0.f[0], 0x44, 0x2C, 0, 0, 1, 0xF, 0xF, 0xF, 0xFF, simage->level > 0, 0);
                }
            }

            spD8.f[0] += 85.0f;
            count++;
        }

        spD8.f[1] += 70.0f;
    }

    DL = microcode_constructor(DL);

    count_2 = 0;

    // not sure what's going on here, but this is required to get the stack to match.
    if(1)
    {
        s32 i_4;
        s32 sp9C;
        s32 padding6;
        s32 padding7;
        s32 sp90;
        s32 padding8;

        sp90 = 0x97;

        for (sp9C = 0; sp9C != 3; sp9C++)
        {
            var_s6 = 0x56;

            for (i_4 = 0; i_4 < 4; i_4++)
            {
                if (count_2 < 12 && check_if_mp_stage_unlocked(count_2))
                {
                    var_s2_2 = 0x96969600;

                    if (count_2 == current_mp_stage_highlighted)
                    {
                        var_s2_2 = -0x100;
                    }

                    textMeasure(&sp128, &sp124, langGet(multi_stage_setups[count_2].select_screen_text_preset), ptrFontBankGothicChars, ptrFontBankGothic, 0);

                    sp130 = var_s6 - 0x1F;
                    sp12C = sp90 - sp128;

                    DL = microcode_constructor_related_to_menus(DL, sp130, sp12C, sp130 + sp124, sp12C + sp128, 0);

                    DL = textRender(DL, &sp130, &sp12C, langGet(multi_stage_setups[count_2].select_screen_text_preset), ptrFontBankGothicChars, ptrFontBankGothic, var_s2_2 | 0xFF, viGetX(), viGetY(), 0, 0);

                    sp130 = var_s6 - 0x1F;
                    sp12C = sp90 - sp128;

                    DL = textRender(DL, &sp130, &sp12C, langGet(multi_stage_setups[count_2].select_screen_text_preset), ptrFontBankGothicChars, ptrFontBankGothic, var_s2_2 | 0x64, viGetX(), viGetY(), 0, 0);
                }

                var_s6 += 0x55;
                count_2++;
            }

            sp90 += 0x46;
        }
    }

    DL = microcode_constructor(DL);
    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);

    return DL;
}




MPSCENARIOS get_scenario(void)
{
  return scenario;
}

//********************************************************************************************************
//MULTIPLAYER SCENARIO SELECT
//********************************************************************************************************
void init_menu13_mpscenariosel(void)
{
#ifndef TARGET_N64
  if (port_mp_pick == PORT_MP_PICK_SCENARIO)
  {
      port_mp_pick_hover = -1;   /* nothing lit on Scenario's Custom row yet */
  }
#endif
  tab_start_selected = FALSE;
  tab_next_selected = FALSE;
  tab_prev_selected = FALSE;
  tab_prev_highlight = FALSE;
  tab_next_highlight = FALSE;
  tab_start_highlight = FALSE;
  load_walletbond();
}

void update_menu13_mpscenario(void) {
#ifndef TARGET_N64
    /* a Game Length / Weapons pick ends here, as menu 13 is left, not in
     * portMpPickLeave: that tick's constructor still draws the pick list,
     * where the reset there flashed Rare's Scenario list for a frame */
    port_mp_pick = PORT_MP_PICK_SCENARIO;
#endif
    return;
}

#ifndef TARGET_N64
/* Game Length and Weapons open this screen (Scenario's list, menu 13) in a pick
 * mode, so all three look and behave the same and a pick returns to setup. 0 =
 * Scenario (vanilla, untouched). */
/* (enum + state: declared above interface_menu0E_mpoptions, which opens the lists) */

extern s32 portMpWeaponSetCount(void);          /* mp_weapon.c */
extern u16 portMpWeaponSetTextID(s32 set);

#define PORT_MP_PICK_Y0      0x83   /* Scenario's first row */
#define PORT_MP_PICK_PITCH   0x16   /* ...and its pitch */
#define PORT_MP_PICK_PERCOL  7      /* rows a column holds at that pitch */
#define PORT_MP_PICK_COLW    0xA8   /* the second column, for Weapons' fourteen */

static s32 portMpPickCount(void)
{
    if (port_mp_pick == PORT_MP_PICK_LENGTH)
    {
        return (scenario != SCENARIO_TLD) ? 7 : 4;   /* select_game_length's own wrap */
    }
    return portMpWeaponSetCount();
}

static s32 portMpLengthPreset(void);

static s32 portMpPickCurrent(void)
{
    return (port_mp_pick == PORT_MP_PICK_LENGTH) ? portMpLengthPreset() : getMPWeaponSet();
}

static u8 *portMpPickText(s32 i)
{
    return (u8 *) langGet((port_mp_pick == PORT_MP_PICK_LENGTH) ? multi_game_lengths[i].text_preset
                                                                 : portMpWeaponSetTextID(i));
}

static void portMpPickPos(s32 i, s32 *x, s32 *y)
{
    *x = 0x39 + (i / PORT_MP_PICK_PERCOL) * PORT_MP_PICK_COLW;
    *y = PORT_MP_PICK_Y0 + (i % PORT_MP_PICK_PERCOL) * PORT_MP_PICK_PITCH;
}

/* THE WEAPONS LIST'S OWN LAYOUT:
 *
 *     Custom            (blank)
 *     Slappers only!    Remote Mines
 *     ...  sets 0-6     ...  sets 7-13
 *
 *     Random            Random Set
 *
 * Nine rows a column, so the pitch closes up to fit the paper. A cell is a set
 * index, PORT_MP_PICK_CUSTOM_CELL for Custom, one of the two random cells, or
 * -1 for nothing. */
#define PORT_MP_WPN_PITCH        0x12
#define PORT_MP_WPN_COL1         7       /* sets in the first column, under Custom - Rare's seven */
#define PORT_MP_PICK_CUSTOM_CELL 100
#define PORT_MP_PICK_RANDOM_CELL 101     /* set 15, mp_weapon.c */
#define PORT_MP_PICK_RANDSET_CELL 102    /* set 16 */
#define PORT_MP_WPN_LASTROW      (PORT_MP_WPN_COL1 + 1)

static s32 portMpWpnAt(s32 col, s32 row, s32 nsets)
{
    if (row == PORT_MP_WPN_LASTROW)
    {
        return (col == 0) ? PORT_MP_PICK_RANDOM_CELL : PORT_MP_PICK_RANDSET_CELL;
    }
    if (col == 0)
    {
        return (row == 0) ? PORT_MP_PICK_CUSTOM_CELL : (row <= PORT_MP_WPN_COL1) ? row - 1 : -1;
    }
    return (row >= 1 && PORT_MP_WPN_COL1 + row - 1 < nsets) ? PORT_MP_WPN_COL1 + row - 1 : -1;
}

static void portMpWpnPos(s32 cell, s32 *x, s32 *y)
{
    if (cell == PORT_MP_PICK_RANDOM_CELL || cell == PORT_MP_PICK_RANDSET_CELL)
    {
        *x = 0x39 + ((cell == PORT_MP_PICK_RANDSET_CELL) ? PORT_MP_PICK_COLW : 0);
        *y = PORT_MP_PICK_Y0 + PORT_MP_WPN_LASTROW * PORT_MP_WPN_PITCH;
        return;
    }
    const s32 col = (cell != PORT_MP_PICK_CUSTOM_CELL && cell >= PORT_MP_WPN_COL1) ? 1 : 0;
    const s32 row = (cell == PORT_MP_PICK_CUSTOM_CELL) ? 0 : col ? cell - PORT_MP_WPN_COL1 + 1 : cell + 1;

    *x = 0x39 + col * PORT_MP_PICK_COLW;
    *y = PORT_MP_PICK_Y0 + row * PORT_MP_WPN_PITCH;
}

/** menu0E: open the list for one of the two rows. */
static void portMpPickOpen(s32 kind)
{
    port_mp_pick = kind;
    port_mp_pick_hover = -1;
    frontChangeMenu(MENU_MP_SCENARIO_SELECT, 0);
}

static void portMpPickLeave(void)
{
    frontChangeMenu(MENU_MP_OPTIONS, FALSE); /* port_mp_pick resets in update_menu13_mpscenario */
}

/* CUSTOM WEAPON SETS: the Weapons list's first row, Custom, opens two more
 * pick modes of this same screen:
 *
 *   PORT_MP_PICK_CUSTOM  "Custom Weapons": New Set and Load Set. Load Set lists
 *                        the saved sets (port_wsets.c, WeaponSets/<NAME>.gewset,
 *                        up to 50) in the CRT drop-down and opens the one picked.
 *                        New Set opens an unnamed draft of the set in force:
 *                        build first, name at the end, to avoid piles of
 *                        badly named sets.
 *   PORT_MP_PICK_EDIT    the set's eight slots down the left (1 = weakest), with
 *                        Use This Set, Save As and Delete Set on the right. Save
 *                        As names the set on the sheet's keyboard (portOsk*) and
 *                        writes it. A draft writes nothing until then (leaving
 *                        drops it); a saved set saves every change at once. A
 *                        slot opens its weapon list (portWsetList*); D-pad
 *                        left / right steps it without the list. Delete asks
 *                        for a second press.
 *
 * A slot holds a weapon only; mp_weapon.c fills the rest from Rare's own sets.
 * B and the Previous tab step back one screen at a time. */
extern s32 portMpWeaponCatalogCount(void);           /* mp_weapon.c */
extern s32 portMpWeaponCatalogItem(s32 k);
extern s32 portMpWeaponCatalogIndex(s32 item);
extern void portMpWeaponSetItems(s32 set, s32 items[8]);
extern void portMpWeaponCustomApply(const s32 items[8]);
extern s32 portMpWeaponIsCustom(void);
extern s32 portWsetCount(void);                      /* port_wsets.c */
extern const char *portWsetName(s32 i);
extern void portWsetRescan(void);
extern s32 portWsetFull(void);
extern const char *portWsetCurrent(void);
extern void portWsetSetCurrent(const char *name);
extern s32 portWsetCleanName(const char *in, char *out, s32 n);
extern s32 portWsetLoad(const char *name, s32 items[8]);
extern s32 portWsetSave(const char *name, const s32 items[8]);
extern s32 portWsetDelete(const char *name);

#define PORT_WSET_EDIT_PITCH 0x12   /* eight slots in the height Scenario's seven take */
#define PORT_WSET_COL2_X     (0x39 + PORT_MP_PICK_COLW)

static char port_wset_name[24];     /* the set the editor has open */
static s32 port_wset_items[8];
static s32 port_wset_del_armed;     /* Delete Set was pressed once */
static s32 port_wset_list_slot = -1; /* the slot whose weapon list is up, -1 = none (portWsetList*) */
#define PORT_WSET_LIST_LOAD 8          /* port_wset_list_slot: Load Set's list of saved sets, not a slot's */
#define PORT_MPSET_LIST_LOAD 9         /* ...Load Settings' list of saved settings */
#define PORT_LIM_LIST_BASE  10         /* ...+ field (time / kills / lives): a limit's values, on Game Limits */
#define PORT_LIM_VALUE_X  (0x39 + 0x5C)   /* the Game Limits screen's value column */
#define PORT_MPSET_VALUE_X (0x39 + 0x78)  /* Load / Save Settings' value column (the name) */
#define PORT_MP_WEAPONSET_CUSTOM_IDX 14   /* mp_weapon.c PORT_MP_WEAPONSET_CUSTOM */
extern s32 portMpSetCount(void);        /* port_mpsettings.c */
extern const char *portMpSetName(s32 i);
extern void portMpSetRescan(void);
extern s32 portMpSetFull(void);
extern const char *portMpSetCurrent(void);
extern void portMpSetSetCurrent(const char *name);
extern s32 portMpSetRead(const char *name, char *buf, s32 size);
extern s32 portMpSetWrite(const char *name, const char *text);
extern s32 portMpSetDelete(const char *name);
static s32 portMpSetApply(const char *name);

static const char *portWsetWeaponName(s32 item)
{
    switch (item)
    {
        case ITEM_UNARMED:       return "Slappers";
        case ITEM_TT33:          return "DD44 Dostovei";
        case ITEM_WPPK:          return "PP7";
        case ITEM_WPPKSIL:       return "PP7 Silenced";
        case ITEM_RUGER:         return "Cougar Magnum";
        case ITEM_THROWKNIFE:    return "Throwing Knife";
        case ITEM_SKORPION:      return "Klobb";
        case ITEM_MP5K:          return "D5K Deutsche";
        case ITEM_UZI:           return "ZMG 9mm";
        case ITEM_FNP90:         return "RC-P90";
        case ITEM_AUTOSHOT:      return "Auto Shotgun";
        case ITEM_AK47:          return "KF7 Soviet";
        case ITEM_M16:           return "AR33 Assault";
        case ITEM_SNIPERRIFLE:   return "Sniper Rifle";
        case ITEM_GRENADE:       return "Hand Grenade";
        case ITEM_GRENADELAUNCH: return "Grenade Launcher";
        case ITEM_ROCKETLAUNCH:  return "Rocket Launcher";
        case ITEM_REMOTEMINE:    return "Remote Mine";
        case ITEM_TIMEDMINE:     return "Timed Mine";
        case ITEM_PROXIMITYMINE: return "Proximity Mine";
        case ITEM_LASER:         return "Moonraker Laser";
        case ITEM_GOLDENGUN:     return "Golden Gun";
        default:                 return "?";
    }
}

/* the Weapons row on the MP setup screen, while a custom set is in force */
static u8 *portWsetRowText(void)
{
    static char text[32];

    snprintf(text, sizeof(text), "%s\n", (getMPWeaponSet() == 15) ? "Random" : (getMPWeaponSet() == 16) ? "Random Set"
                                         : (portWsetCurrent()[0] != '\0') ? portWsetCurrent() : "Custom");
    return (u8 *) text;
}

static void portWsetListOpen(void)
{
    portWsetRescan();
    port_mp_pick = PORT_MP_PICK_CUSTOM;
    port_mp_pick_hover = -1;
    tab_start_highlight = FALSE;
}

static void portWsetEditOpen(const char *name)
{
    snprintf(port_wset_name, sizeof(port_wset_name), "%s", name);
    if (port_wset_name[0] == '\0')
    {   /* New Set: a draft of the set in force, nothing on disk yet */
        portMpWeaponSetItems(getMPWeaponSet(), port_wset_items);
    }
    else
    {
        portWsetLoad(port_wset_name, port_wset_items);
    }
    {   /* a hand-edited file's unknown weapon shows (and saves) as what plays: Slappers */
        s32 k;

        for (k = 0; k < 8; k++)
        {
            if (portMpWeaponCatalogIndex(port_wset_items[k]) < 0)
            {
                port_wset_items[k] = ITEM_UNARMED;
            }
        }
    }
    port_wset_del_armed = FALSE;
    port_wset_list_slot = -1;
    port_mp_pick = PORT_MP_PICK_EDIT;
    port_mp_pick_hover = -1;
}

/* the edit screen's rows: 0-7 the slots (left), 8 Use This Set, 9 Save As, 10 Delete Set (right) */
static void portWsetEditPos(s32 i, s32 *x, s32 *y)
{
    if (i < 8)
    {
        *x = 0x39;
        *y = PORT_MP_PICK_Y0 + i * PORT_WSET_EDIT_PITCH;
    }
    else
    {
        *x = PORT_WSET_COL2_X;
        *y = PORT_MP_PICK_Y0 + (i - 8) * PORT_MP_PICK_PITCH;
    }
}

static void portWsetPut(s32 slot, s32 item)
{
    port_wset_items[slot] = item;
    if (port_wset_name[0] != '\0')
    {   /* a saved set saves every change; a draft waits for Save As */
        portWsetSave(port_wset_name, port_wset_items);
    }
    if (portMpWeaponIsCustom() && strcmp(portWsetCurrent(), port_wset_name) == 0)
    {
        portMpWeaponCustomApply(port_wset_items);   /* the set in force follows its edits */
    }
}

/* D-pad left / right on a slot: the previous / next weapon, no list */
static void portWsetStep(s32 slot, s32 dir)
{
    const s32 n = portMpWeaponCatalogCount();
    s32 k = portMpWeaponCatalogIndex(port_wset_items[slot]);

    if (n <= 0)
    {
        return;
    }
    k = (k < 0) ? 0 : (k + ((dir < 0) ? n - 1 : 1)) % n;
    portWsetPut(slot, portMpWeaponCatalogItem(k));
}

/* Look-stick quick scroll for the CRT pop-up lists: returns this tick's steps
 * from player 1's look stick (+ = down), one step on the first push, then a
 * rate that grows with deflection (squared) up to ~36 entries a second in 60 Hz
 * fields; the C-up/down that stick also makes is ignored while pushed
 * (portMenuLookActive) to avoid double steps. */
#define PORT_LOOK_DEAD 24
s32 portMenuLookActive(void)
{
    extern s32 portInputPadLookYP1(void);   /* input_pc.c; weak 0 headless */
    const s32 y = portInputPadLookYP1();

    return y > PORT_LOOK_DEAD || y < -PORT_LOOK_DEAD;
}

s32 portMenuLookSteps(void)
{
    extern s32 portInputPadLookYP1(void);
    static f32 acc;
    static s32 wasdir;
    const s32 y = portInputPadLookYP1();
    const s32 dir = (y > PORT_LOOK_DEAD) ? -1 : (y < -PORT_LOOK_DEAD) ? 1 : 0;   /* stick up = up the list */
    f32 d;
    s32 steps;

    if (dir == 0)
    {
        acc = 0.0f;
        wasdir = 0;
        return 0;
    }
    if (dir != wasdir)
    {
        acc = 1.0f;   /* a flick is one step at once */
        wasdir = dir;
    }
    else
    {
        d = (f32) ((y < 0 ? -y : y) - PORT_LOOK_DEAD) / (f32) (85 - PORT_LOOK_DEAD);
        if (d > 1.0f) d = 1.0f;
        acc += (0.04f + d * d * 0.56f) * portTickDelta;
    }
    steps = (s32) acc;
    acc -= (f32) steps;
    return steps * dir;
}

/* The slot's weapon list: the options sheet's CRT drop-down, drawn and driven
 * the same way (A picks, B closes, wheel scrolls) but with its own state, so
 * the sheet's list code stays untouched; entries are mp_weapon.c's catalog in
 * game order. */
static s32 port_wset_list_idx;
static s32 port_wset_list_scroll;
static s32 port_wset_list_top;
static s32 port_wset_list_vis;
static f32 port_wset_list_anim;
static s32 port_wset_list_closing;

#define PORT_WSET_LIST_X (0x39 + 0x0E)   /* past the slot's number */

/* LOAD SET: the same list, its entries the saved sets, at the
 * Load Set row; picking one opens it in the editor */
/* a limit's values (PORT_LIM_LIST_BASE + field), defined with the Limits screen */
static s32 portLimListCount(s32 field);
static const char *portLimListText(s32 field, s32 idx);
static s32 portLimListLive(s32 field);
static s32 portLimListRowY(s32 field);
static void portLimListPick(s32 field, s32 idx);

static s32 portWsetListCount(void)
{
    if (port_wset_list_slot >= PORT_LIM_LIST_BASE)
    {
        return portLimListCount(port_wset_list_slot - PORT_LIM_LIST_BASE);
    }
    return (port_wset_list_slot == PORT_MPSET_LIST_LOAD) ? portMpSetCount()
         : (port_wset_list_slot == PORT_WSET_LIST_LOAD) ? portWsetCount() : portMpWeaponCatalogCount();
}

static const char *portWsetListText(s32 idx)
{
    if (port_wset_list_slot >= PORT_LIM_LIST_BASE)
    {
        return portLimListText(port_wset_list_slot - PORT_LIM_LIST_BASE, idx);
    }
    return (port_wset_list_slot == PORT_MPSET_LIST_LOAD) ? portMpSetName(idx)
         : (port_wset_list_slot == PORT_WSET_LIST_LOAD) ? portWsetName(idx) : portWsetWeaponName(portMpWeaponCatalogItem(idx));
}

/* the entry banded as the one in use: the slot's weapon, or the custom set in force */
static s32 portWsetListLive(void)
{
    s32 k;

    if (port_wset_list_slot >= PORT_LIM_LIST_BASE)
    {
        return portLimListLive(port_wset_list_slot - PORT_LIM_LIST_BASE);
    }
    if (port_wset_list_slot == PORT_MPSET_LIST_LOAD)
    {
        for (k = 0; k < portMpSetCount(); k++)
        {
            if (strcmp(portMpSetName(k), portMpSetCurrent()) == 0)
            {
                return k;
            }
        }
        return -1;
    }
    if (port_wset_list_slot != PORT_WSET_LIST_LOAD)
    {
        return portMpWeaponCatalogIndex(port_wset_items[port_wset_list_slot]);
    }
    for (k = 0; portMpWeaponIsCustom() && k < portWsetCount(); k++)
    {
        if (strcmp(portWsetName(k), portWsetCurrent()) == 0)
        {
            return k;
        }
    }
    return -1;
}

static s32 portWsetListX(void)
{
    return (port_wset_list_slot >= PORT_LIM_LIST_BASE) ? PORT_LIM_VALUE_X   /* over the value */
         : (port_wset_list_slot == PORT_MPSET_LIST_LOAD) ? PORT_MPSET_VALUE_X   /* in line with Save / Delete's name */
         : (port_wset_list_slot >= PORT_WSET_LIST_LOAD) ? 0x39 : PORT_WSET_LIST_X;
}

static void portWsetListFollow(void)
{
    const s32 count = portWsetListCount();
    const s32 margin = 2;   /* PORT_SHEET_LIST_MARGIN */

    if (port_wset_list_idx - margin < port_wset_list_scroll)
    {
        port_wset_list_scroll = port_wset_list_idx - margin;
    }
    else if (port_wset_list_idx + margin >= port_wset_list_scroll + port_wset_list_vis)
    {
        port_wset_list_scroll = port_wset_list_idx + margin - port_wset_list_vis + 1;
    }
    if (port_wset_list_scroll > count - port_wset_list_vis) port_wset_list_scroll = count - port_wset_list_vis;
    if (port_wset_list_scroll < 0) port_wset_list_scroll = 0;
}

static void portWsetListOpenFor(s32 slot)
{
    const s32 rowy = (slot >= PORT_LIM_LIST_BASE) ? portLimListRowY(slot - PORT_LIM_LIST_BASE)
                   : (slot == PORT_MPSET_LIST_LOAD) ? PORT_MP_PICK_Y0                        /* Load Settings, row 0 */
                   : (slot >= PORT_WSET_LIST_LOAD) ? PORT_MP_PICK_Y0 + PORT_MP_PICK_PITCH   /* Load Set, row 1 */
                                                   : PORT_MP_PICK_Y0 + slot * PORT_WSET_EDIT_PITCH;
    const s32 ymax = PORT_SHEET_LAST_Y;
    s32 count, live, top, at;

    port_wset_list_slot = slot;
    count = portWsetListCount();
    live = portWsetListLive();
    if (count <= 0)
    {
        port_wset_list_slot = -1;
        return;
    }
    if (live < 0) live = 0;
    port_wset_list_vis = (count < PORT_SHEET_LIST_LINES) ? count : PORT_SHEET_LIST_LINES;
    port_wset_list_idx = live;
    top = rowy - live * PORT_SHEET_ROW_PITCH;
    if (top > ymax - (port_wset_list_vis - 1) * PORT_SHEET_ROW_PITCH)
    {
        top = ymax - (port_wset_list_vis - 1) * PORT_SHEET_ROW_PITCH;
    }
    if (top < PORT_SHEET_ROW_Y0)
    {
        top = PORT_SHEET_ROW_Y0;
    }
    at = (rowy - top) / PORT_SHEET_ROW_PITCH;
    if (at < 0) at = 0;
    if (at > port_wset_list_vis - 1) at = port_wset_list_vis - 1;
    port_wset_list_scroll = live - at;
    port_wset_list_top = top;
    portWsetListFollow();
    port_wset_list_anim = 0.0f;
    port_wset_list_closing = FALSE;
}

/* the CRT switch-on / off, one step per banked 60 Hz field (portSheetListAnimTick's rule) */
static void portWsetListAnimTick(void)
{
    static f32 field;

    if (port_wset_list_slot < 0)
    {
        return;
    }
    field += portTickDelta;
    if (field > 4.0f) field = 4.0f;
    while (field >= 1.0f)
    {
        field -= 1.0f;
        if (!port_wset_list_closing)
        {
            port_wset_list_anim += (port_wset_list_anim < 1.0f) ? PORT_SHEET_ANIM_W : PORT_SHEET_ANIM_H;
            if (port_wset_list_anim > 2.0f) port_wset_list_anim = 2.0f;
        }
        else
        {
            port_wset_list_anim -= (port_wset_list_anim > 1.0f) ? PORT_SHEET_ANIM_H : PORT_SHEET_ANIM_W;
            if (port_wset_list_anim <= 0.0f)
            {
                port_wset_list_slot = -1;
                port_wset_list_closing = FALSE;
                port_wset_list_anim = 0.0f;
                field = 0.0f;
                return;
            }
        }
    }
}

static s32 portWsetListWidth(void)
{
    s32 wid = 0;
    s32 i;

    for (i = 0; i < portWsetListCount(); i++)
    {
        char text[32];
        s32 th = 0, tw = 0;

        snprintf(text, sizeof(text), "%s\n", portWsetListText(i));
        textMeasure(&th, &tw, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        if (tw > wid) wid = tw;
    }
    wid += PORT_SHEET_LIST_PAD * 2;
    if (portWsetListCount() > port_wset_list_vis)
    {
        wid += 8;   /* the more-above / more-below gutter */
    }
    return wid;
}

/* the list owns the tick while it is up: 1 = it took the input */
static s32 portWsetListTick(void)
{
    static f32 listh = -1.0f, listv = -1.0f;
    const s32 count = portWsetListCount();
    const s32 x0 = portWsetListX() - PORT_SHEET_LIST_PAD;
    const s32 w = portWsetListWidth();
    const s32 was = port_wset_list_idx;
    const s32 cursormoved = (cursor_h_pos != listh || cursor_v_pos != listv);
    s32 wheel;
    s32 e;

    if (port_wset_list_slot < 0)
    {
        return FALSE;
    }
    portWsetListAnimTick();
    listh = cursor_h_pos;
    listv = cursor_v_pos;
    if (port_wset_list_slot < 0 || port_wset_list_closing)
    {
        frontUpdateControlStickPosition();
        return TRUE;   /* shutting: takes nothing, lets nothing through */
    }
    if (port_wset_list_anim >= 2.0f && cursormoved)
    {
        for (e = 0; e < port_wset_list_vis; e++)
        {
            const s32 ey = port_wset_list_top + e * PORT_SHEET_ROW_PITCH;

            if (cursor_h_pos >= (f32) x0 && cursor_h_pos <= (f32) (x0 + w)
                && cursor_v_pos >= (f32) (ey - 1) && cursor_v_pos < (f32) (ey - 1 + PORT_SHEET_ROW_PITCH))
            {
                port_wset_list_idx = port_wset_list_scroll + e;
            }
        }
    }
    wheel = portMouseWheelSteps();
    {   /* up / down: a press steps, a hold repeats (0.4 s, then every 0.1 s -
         * portSheetDpadStep's cadence) - the pad's digital Move D-pad and the
         * D-pad / C buttons alike */
        extern s32 portInputPadDigMoveP1(s32 *dx, s32 *dy) __attribute__((weak)); /* input_pc.c */
        static f32 held[2];
        static s32 prev[2];
        s32 dx = 0, dy = 0;
        s32 dir[2];
        s32 step[2];
        s32 a;

        if (portInputPadDigMoveP1 != NULL)
        {
            portInputPadDigMoveP1(&dx, &dy);
        }
        const s32 lookon = portMenuLookActive();   /* the look stick quick-scrolls below; drop its C echo */

        dir[0] = dy > 0 || joyGetButtons(PLAYER_1, U_JPAD | (lookon ? 0 : U_CBUTTONS));
        dir[1] = dy < 0 || joyGetButtons(PLAYER_1, D_JPAD | (lookon ? 0 : D_CBUTTONS));
        for (a = 0; a < 2; a++)
        {
            step[a] = dir[a] && !prev[a];
            if (dir[a] && prev[a])
            {
                held[a] += portTickDelta;
                if (held[a] >= 24.0f)
                {
                    step[a] = TRUE;
                    held[a] -= 6.0f;
                }
            }
            else
            {
                held[a] = 0.0f;
            }
            prev[a] = dir[a];
        }
        {
            const s32 look = portMenuLookSteps();

            if (look != 0)
            {
                port_wset_list_idx += look;
                if (port_wset_list_idx < 0) port_wset_list_idx = 0;
                if (port_wset_list_idx > count - 1) port_wset_list_idx = count - 1;
                portWsetListFollow();
                wheel = 0;
            }
        }
        if (step[0] && port_wset_list_idx > 0)
        {
            port_wset_list_idx--;
            portWsetListFollow();
            wheel = 0;
        }
        else if (step[1] && port_wset_list_idx < count - 1)
        {
            port_wset_list_idx++;
            portWsetListFollow();
            wheel = 0;
        }
    }
    if (wheel != 0 && count > port_wset_list_vis)
    {   /* the wheel scrolls the window, not the highlight */
        port_wset_list_scroll -= wheel;
        if (port_wset_list_scroll > count - port_wset_list_vis) port_wset_list_scroll = count - port_wset_list_vis;
        if (port_wset_list_scroll < 0) port_wset_list_scroll = 0;
    }
    if (port_wset_list_idx != was)
    {
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, OPTION_CLICK2_SFX, NULL);
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {   /* A picks the highlight wherever the cursor is */
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        if (port_wset_list_slot >= PORT_LIM_LIST_BASE)
        {   /* a limit: the value, then the list shuts */
            portLimListPick(port_wset_list_slot - PORT_LIM_LIST_BASE, port_wset_list_idx);
            port_wset_list_closing = TRUE;
            frontUpdateControlStickPosition();
            return TRUE;
        }
        if (port_wset_list_slot == PORT_MPSET_LIST_LOAD)
        {   /* Load Settings: in force, back to the setup to see them */
            const s32 ok = portMpSetApply(portMpSetName(port_wset_list_idx));

            port_wset_list_slot = -1;
            if (ok)
            {
                portMpPickLeave();
            }
            frontUpdateControlStickPosition();
            return TRUE;
        }
        if (port_wset_list_slot == PORT_WSET_LIST_LOAD)
        {   /* Load Set: straight into the set's editor (which drops the list) */
            portWsetEditOpen(portWsetName(port_wset_list_idx));
            frontUpdateControlStickPosition();
            return TRUE;
        }
        portWsetPut(port_wset_list_slot, portMpWeaponCatalogItem(port_wset_list_idx));
        port_wset_list_closing = TRUE;
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        port_wset_list_closing = TRUE;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_M60AMMGUN_3_SFX, NULL);
    }
    frontUpdateControlStickPosition();
    return TRUE;
}

static Gfx *portWsetListDraw(Gfx *DL)
{
    const s32 count = portWsetListCount();
    const s32 live = portWsetListLive();
    const s32 x0 = portWsetListX() - PORT_SHEET_LIST_PAD;
    const s32 y0 = port_wset_list_top;
    const s32 w = portWsetListWidth();
    const s32 n = port_wset_list_vis;
    s32 bx0, by0, bx1, by1, cx, cy, e;
    f32 aw, ah;

    /* the sheet list's CRT switch-on: a line out of the centre, then open */
    aw = (port_wset_list_anim < 1.0f) ? port_wset_list_anim : 1.0f;
    ah = (port_wset_list_anim > 1.0f) ? port_wset_list_anim - 1.0f : 0.0f;
    bx0 = x0 - 1;
    bx1 = x0 + w + 1;
    by0 = y0 - 3;
    by1 = y0 + (n - 1) * PORT_SHEET_ROW_PITCH + 16;
    cx = (bx0 + bx1) / 2;
    cy = (by0 + by1) / 2;
    {
        const s32 curw = (s32) ((f32) (bx1 - bx0) * aw);
        s32 curh = (s32) ((f32) (by1 - by0) * ah);

        if (curh < 2) curh = 2;
        bx0 = cx - (bx1 - bx0) / 2 + ((bx1 - bx0) - curw) / 2;
        bx1 = bx0 + curw;
        by0 = cy - (by1 - by0) / 2 + ((by1 - by0) - curh) / 2;
        by1 = by0 + curh;
    }
    if (bx1 - bx0 < 2)
    {
        return DL;
    }
    DL = microcode_constructor_related_to_menus(DL, bx0, by0, bx1, by1, PORT_SHEET_COL_LIST_EDGE);
    if (bx1 - bx0 > 4 && by1 - by0 > 4)
    {
        DL = microcode_constructor_related_to_menus(DL, bx0 + 1, by0 + 1, bx1 - 1, by1 - 1, PORT_SHEET_COL_LIST_BG);
    }
    DL = microcode_constructor(DL);
    if (port_wset_list_anim < 2.0f)
    {
        return DL;
    }
    if (live >= port_wset_list_scroll && live < port_wset_list_scroll + n)
    {
        const s32 ly = y0 + (live - port_wset_list_scroll) * PORT_SHEET_ROW_PITCH;

        DL = microcode_constructor_related_to_menus(DL, x0 + 1, ly - 1, x0 + w - 1, ly + 0xE, PORT_SHEET_COL_LIST_LIVE);
    }
    for (e = 0; e < n; e++)
    {
        const s32 idx = port_wset_list_scroll + e;
        const s32 ey = y0 + e * PORT_SHEET_ROW_PITCH;
        char text[32];
        s32 x = x0 + PORT_SHEET_LIST_PAD;
        s32 y = ey;

        snprintf(text, sizeof(text), "%s\n", portWsetListText(idx));
        if (idx == port_wset_list_idx)
        {
            DL = microcode_constructor_related_to_menus(DL, x0 + 1, ey - 1, x0 + w - 1, ey + 0xE, PORT_SHEET_COL_LIST_HOT);
            DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
        }
        else
        {
            DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }
    }
    {   /* more above / more below */
        const s32 ax = x0 + w - 7;
        s32 k;

        if (port_wset_list_scroll > 0)
        {
            for (k = 0; k < 3; k++)
            {
                DL = microcode_constructor_related_to_menus(DL, ax + k, y0 + k, ax + 6 - k, y0 + k + 1, 0x000000FF);
            }
        }
        if (port_wset_list_scroll + n < count)
        {
            const s32 ay = y0 + (n - 1) * PORT_SHEET_ROW_PITCH + 10;

            for (k = 0; k < 3; k++)
            {
                DL = microcode_constructor_related_to_menus(DL, ax + k, ay + 2 - k, ax + 6 - k, ay + 3 - k, 0x000000FF);
            }
        }
    }
    DL = microcode_constructor(DL);
    return DL;
}


static void portWsetInterface(void)
{
    const s32 press = joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON);
    const s32 back = joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON);

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);

    if (port_osk_open)
    {   /* Save As: the keyboard owns the input, as on the sheet */
        const f32 hx = cursor_h_pos;
        const f32 hy = cursor_v_pos;

        if (portOskTick() && port_osk_done > 0)
        {
            char clean[24];
            const s32 usable = portWsetCleanName(port_osk_text, clean, sizeof(clean));
            s32 exists = FALSE;
            s32 k;

            for (k = 0; usable && k < portWsetCount(); k++)
            {
                exists |= (strcmp(portWsetName(k), clean) == 0);
            }
            if (!usable || (exists && strcmp(clean, port_wset_name) != 0) || (!exists && portWsetFull()))
            {   /* say why in the title and stay up with the name (the layout keyboard's rule):
                 * another set's name, a full list, or nothing usable typed */
                portOskOpen(!usable ? "NAME THIS SET" : exists ? "NAME IN USE" : "LIST FULL", port_osk_text);
            }
            else if (portWsetSave(clean, port_wset_items))
            {   /* the editor carries on, on the saved set */
                portWsetEditOpen(clean);
            }
        }
        frontUpdateControlStickPosition();
        if (!port_osk_typing)
        {
            cursor_h_pos = hx;
            cursor_v_pos = hy;
        }
        return;
    }

    if (portWsetListTick())
    {
        return;
    }

    if (joyGetButtons(PLAYER_1, Z_TRIG|A_BUTTON) == 0)
    {   /* the highlight follows the cursor while nothing is held */
        const s32 n = (port_mp_pick == PORT_MP_PICK_EDIT) ? 11 : 2;   /* New Set, Load Set */
        s32 i;

        tab_prev_highlight = frontCheckCursorOnPreviousTab();
        port_mp_pick_hover = -1;
        for (i = 0; !tab_prev_highlight && i < n; i++)
        {
            s32 x, y;
            const s32 pitch = (port_mp_pick == PORT_MP_PICK_EDIT && i < 8) ? PORT_WSET_EDIT_PITCH : PORT_MP_PICK_PITCH;

            if (port_mp_pick == PORT_MP_PICK_EDIT)
            {
                portWsetEditPos(i, &x, &y);
            }
            else
            {
                portMpPickPos(i, &x, &y);
            }
            if ((s32) cursor_v_pos >= y - 1 && (s32) cursor_v_pos < y - 1 + pitch
                && (s32) cursor_h_pos >= x - 4 && (s32) cursor_h_pos < x - 4 + PORT_MP_PICK_COLW)
            {
                port_mp_pick_hover = i;
            }
        }
        if (port_mp_pick_hover != 10)
        {
            port_wset_del_armed = FALSE;   /* Delete's second press must be the next thing done */
        }
    }

    if (port_mp_pick == PORT_MP_PICK_EDIT && port_mp_pick_hover >= 0 && port_mp_pick_hover < 8)
    {   /* D-pad left / right on a slot, for a pad */
        if (joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD))
        {
            portWsetStep(port_mp_pick_hover, -1);
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD))
        {
            portWsetStep(port_mp_pick_hover, 1);
        }
    }

    if (back || (press && tab_prev_highlight))
    {   /* one screen back: the editor to the sets, the sets to the Weapons list */
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        if (port_mp_pick == PORT_MP_PICK_EDIT)
        {
            portWsetListOpen();
        }
        else
        {
            port_mp_pick = PORT_MP_PICK_WEAPONS;
            port_mp_pick_hover = -1;
        }
        return;
    }
    if (!press || port_mp_pick_hover < 0)
    {
        frontUpdateControlStickPosition();
        return;
    }
    sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    if (port_mp_pick == PORT_MP_PICK_CUSTOM)
    {
        if (port_mp_pick_hover == 0)
        {
            portWsetEditOpen("");   /* a draft: build first, name it with Save As */
        }
        else
        {
            portWsetListOpenFor(PORT_WSET_LIST_LOAD);   /* the saved sets' drop-down (none saved: nothing) */
        }
    }
    else if (port_mp_pick_hover < 8)
    {   /* the slot's weapon list (the sheet's drop-down) */
        portWsetListOpenFor(port_mp_pick_hover);
    }
    else if (port_mp_pick_hover == 8)
    {   /* Use This Set: in force, named on the setup screen ("Custom" for an
         * unsaved draft), back there */
        portMpWeaponCustomApply(port_wset_items);
        portWsetSetCurrent(port_wset_name);
        portMpPickLeave();
        return;
    }
    else if (port_mp_pick_hover == 9)
    {   /* Save As: a draft's name, or a copy of a saved set under a new one */
        portOskOpen("SAVE SET AS", (port_wset_name[0] != '\0') ? port_wset_name : NULL);
    }
    else if (port_wset_name[0] == '\0')
    {
        /* Delete on a draft: nothing on disk to delete (greyed) */
    }
    else if (!port_wset_del_armed)
    {
        port_wset_del_armed = TRUE;
    }
    else
    {
        portWsetDelete(port_wset_name);
        portWsetListOpen();
    }
    frontUpdateControlStickPosition();
}

static Gfx *portWsetConstruct(Gfx *DL)
{
    char line[48];
    s32 x, y, h, w, i;
    const s32 edit = (port_mp_pick == PORT_MP_PICK_EDIT);
    const s32 n = edit ? 11 : 2;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    snprintf(line, sizeof(line), "%s\n", !edit ? "Custom Weapons" : (port_wset_name[0] != '\0') ? port_wset_name : "New Set");
    x = 0x37;
    y = 0x66;
    DL = frontPrintText(DL, &x, &y, (u8 *) line, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    for (i = 0; i < n; i++)
    {
        s32 alpha = 0xFF;
        s32 lit = (i == port_mp_pick_hover);

        if (edit && i < 8)
        {
            snprintf(line, sizeof(line), "%d  %s\n", (int) i + 1, portWsetWeaponName(port_wset_items[i]));
        }
        else if (edit)
        {
            snprintf(line, sizeof(line), "%s\n", (i == 8) ? "Use This Set" : (i == 9) ? "Save As"
                                                 : port_wset_del_armed ? "Confirm Delete" : "Delete Set");
            alpha = (i == 10 && port_wset_name[0] == '\0') ? 0x70 : 0xFF;   /* a draft has nothing to delete */
        }
        else
        {
            snprintf(line, sizeof(line), "%s\n", (i == 0) ? "New Set" : "Load Set");
            alpha = (i == 1 && portWsetCount() == 0) ? 0x70 : 0xFF;   /* nothing saved yet */
        }
        textMeasure(&h, &w, (u8 *) line, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        if (edit)
        {
            portWsetEditPos(i, &x, &y);
        }
        else
        {
            portMpPickPos(i, &x, &y);
        }
        if (lit)
        {
            DL = microcode_constructor_related_to_menus(DL, x - 2, y - 1, x + w + 3, y + 0xE, 0x32);
        }
        DL = frontPrintText(DL, &x, &y, (u8 *) line, ptrFontZurichBoldChars, ptrFontZurichBold, alpha, viGetX(), viGetY(), 0, 0);
    }

    DL = frontAddPreviousTabText(DL);
    if (port_wset_list_slot >= 0)
    {
        DL = portWsetListDraw(DL);
    }
    if (port_osk_open)
    {
        DL = portOskDraw(DL);
    }
    DL = frontDrawCursor(DL);
    return DL;
}

/* GAME LIMITS AND MATCH SETTINGS. Game Limits (the MP setup row that was Game
 * Length) is this screen's PORT_MP_PICK_LIMITS: Time Limit, Kill Limit, Lives
 * and Teams, one per row. A on a limit opens its values in the CRT drop-down;
 * D-pad left / right steps it. Teams (three or four players, any scenario)
 * turns sides on, and then each player has a row of their own whose A or
 * D-pad swaps them between red and blue: 2 v 1, 2 v 2 or 3 v 1 as the colours
 * fall. A swap that would empty a side is refused.
 *
 * Load / Save Settings (the setup's bottom row) is PORT_MP_PICK_SETTINGS:
 *
 *   Load Settings    the saved settings (port_mpsettings.c,
 *                    Data/MatchSettings/<NAME>.gemp, up to 50) in the drop-down;
 *                    the one picked is put in force and the setup returns.
 *   Save Settings    names them on the keyboard, offering the last name loaded
 *                    or saved (the only existing name it will overwrite).
 *   Delete Settings  deletes that name, asking twice.
 *
 * A file holds Scenario, Level, the three limits, Teams and the sides, the
 * weapon set (a custom set's eight weapons and name), each player's Health and
 * every Gameplay Options switch (portMpSetSave / portMpSetApply).
 *
 * A limit's values are v[] = { scenario, time, kills, lives }. */
s32 get_players_team_or_scenario_item_flag(int player);

enum { PORT_SCEN_BASE, PORT_SCEN_TIME, PORT_SCEN_KILLS, PORT_SCEN_LIVES, PORT_SCEN_FIELDS };
#define PORT_LIM_TEAMS_ROW 3   /* Time, Kills, Lives, Teams, then a row per player */
#define PORT_MPSET_ROWS    3   /* Load, Save, Delete */

static const s32 port_lim_time[] = { 0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };
static const s32 port_lim_kills[] = { 0, 1, 3, 5, 10, 15, 20, 25, 30, 50, 100 };
static const s32 port_lim_lives[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 35, 40, 45, 50 };

static s32 port_mpset_del_armed;      /* Delete Settings was pressed once */

static s32 portScenValid(s32 base)
{
    return base >= 0 && base < PORT_SCEN_LISTED
        && mp_player_counts[base].min <= get_selected_num_players()
        && get_selected_num_players() <= mp_player_counts[base].max;
}

/* a field Rare's scenario rules leave fixed: YOLT has two lives and no
 * limits, flag tag has no kill limit */
static s32 portLimFixed(s32 base, s32 field)
{
    return (base == SCENARIO_YOLT && field != PORT_SCEN_BASE)
        || (base == SCENARIO_TLD && field == PORT_SCEN_KILLS);
}

/* step a field to the next / previous value in its list, wrapping */
static void portLimStep(s32 v[PORT_SCEN_FIELDS], s32 field, s32 dir)
{
    const s32 *list;
    s32 n, k, at = 0;

    if (field == PORT_SCEN_BASE || portLimFixed(v[PORT_SCEN_BASE], field))
    {
        return;
    }
    list = (field == PORT_SCEN_TIME) ? port_lim_time : (field == PORT_SCEN_KILLS) ? port_lim_kills : port_lim_lives;
    n = (field == PORT_SCEN_TIME) ? ARRAYCOUNT(port_lim_time) : (field == PORT_SCEN_KILLS) ? ARRAYCOUNT(port_lim_kills)
                                                               : ARRAYCOUNT(port_lim_lives);
    for (k = 0; k < n; k++)
    {
        if (list[k] <= v[field])
        {
            at = k;   /* a hand-edited value steps from the nearest one below */
        }
    }
    v[field] = list[(at + ((dir < 0) ? n - 1 : 1)) % n];
}

static const char *portLimValue(const s32 v[PORT_SCEN_FIELDS], s32 field)
{
    static char text[40];
    const s32 x = v[field];

    if (v[PORT_SCEN_BASE] == SCENARIO_YOLT)
    {
        snprintf(text, sizeof(text), "%s\n", (field == PORT_SCEN_LIVES) ? "2" : "None");
        return text;
    }
    if (portLimFixed(v[PORT_SCEN_BASE], field) || x <= 0)
    {
        snprintf(text, sizeof(text), "%s\n", (field == PORT_SCEN_LIVES) ? "Unlimited" : "None");
    }
    else if (field == PORT_SCEN_TIME)
    {
        snprintf(text, sizeof(text), "%d Min\n", (int) x);
    }
    else
    {
        snprintf(text, sizeof(text), "%d\n", (int) x);
    }
    return text;
}

/* the live settings as a v[] */
static void portLimLive(s32 v[PORT_SCEN_FIELDS])
{
    v[PORT_SCEN_BASE] = scenario;
    v[PORT_SCEN_TIME] = port_mp_lim[PORT_LIM_TIME];
    v[PORT_SCEN_KILLS] = port_mp_lim[PORT_LIM_KILLS];
    v[PORT_SCEN_LIVES] = port_mp_lim[PORT_LIM_LIVES];
}

/* the Game Length preset the limits match, else -1 */
static s32 portMpLengthPreset(void)
{
    s32 i;

    if (scenario == SCENARIO_YOLT)
    {
        return game_length;
    }
    for (i = 0; i < 7; i++)
    {
        if (multi_game_lengths[i].time == MINS_TO_TIMER60(port_mp_lim[PORT_LIM_TIME])
            && multi_game_lengths[i].points == ((scenario == SCENARIO_TLD) ? 0 : port_mp_lim[PORT_LIM_KILLS]))
        {
            return i;
        }
    }
    return -1;
}

/* the Game Limits row: a preset's own name, else the limits spelled out;
 * then the lives, if any (YOLT's two are its own) */
static u8 *portMpLengthText(void)
{
    static char text[48];
    const s32 i = portMpLengthPreset();
    const s32 t = port_mp_lim[PORT_LIM_TIME];
    const s32 k = (scenario == SCENARIO_TLD) ? 0 : port_mp_lim[PORT_LIM_KILLS];
    const s32 lives = (scenario == SCENARIO_YOLT) ? 0 : port_mp_lim[PORT_LIM_LIVES];

    if (lives > 0)
    {
        char limits[40];
        const char *base = limits;

        if (i >= 0 && (t > 0 || k > 0))
        {
            base = (const char *) langGet(multi_game_lengths[i].text_preset);
        }
        else if (t > 0 && k > 0)
        {
            snprintf(limits, sizeof(limits), "%d Min, %d Pts", (int) t, (int) k);
        }
        else if (t > 0)
        {
            snprintf(limits, sizeof(limits), "%d Min", (int) t);
        }
        else if (k > 0)
        {
            snprintf(limits, sizeof(limits), "%d Points", (int) k);
        }
        else
        {
            limits[0] = '\0';
        }
        snprintf(text, sizeof(text), "%.*s%s%d %s\n", (int) strcspn(base, "\n"), base, (base[0] != '\0' && base[0] != '\n') ? ", " : "",
                 (int) lives, (lives == 1) ? "Life" : "Lives");
        return (u8 *) text;
    }
    if (i >= 0)
    {
        return langGet(multi_game_lengths[i].text_preset);
    }
    if (t > 0 && k > 0)
    {
        snprintf(text, sizeof(text), "%d Min, %d Pts\n", (int) t, (int) k);
    }
    else if (t > 0)
    {
        snprintf(text, sizeof(text), "%d Min\n", (int) t);
    }
    else
    {
        snprintf(text, sizeof(text), "%d Points\n", (int) k);
    }
    return (u8 *) text;
}

/* the Teams row: Off, else the sides, red v blue */
static const char *portLimTeamsText(void)
{
    static char text[16];
    s32 count[2] = { 0, 0 };
    s32 i;

    if (!portMpTeamsPicked())
    {
        return "Off\n";
    }
    for (i = 0; i < get_selected_num_players(); i++)
    {
        count[port_mp_team[i] != 0]++;
    }
    snprintf(text, sizeof(text), "%d v %d\n", (int) count[0], (int) count[1]);
    return text;
}

/* a player's row: swap sides, unless they are the last on theirs */
static void portLimTeamSwap(s32 player)
{
    s32 i, mates = 0;

    for (i = 0; i < get_selected_num_players(); i++)
    {
        mates += (i != player && port_mp_team[i] == port_mp_team[player]);
    }
    if (mates > 0)
    {
        port_mp_team[player] = !port_mp_team[player];
    }
}

static void portLimRowPos(s32 i, s32 *x, s32 *y)
{
    *x = 0x39;
    *y = PORT_MP_PICK_Y0 + i * PORT_MP_PICK_PITCH;
}

/* Game Limits: step one of the live settings */
static void portLimLiveStep(s32 row, s32 dir)
{
    s32 v[PORT_SCEN_FIELDS];

    portLimLive(v);
    portLimStep(v, row + 1, dir);   /* rows 0-2 are fields 1-3 */
    port_mp_lim[PORT_LIM_TIME] = v[PORT_SCEN_TIME];
    port_mp_lim[PORT_LIM_KILLS] = v[PORT_SCEN_KILLS];
    port_mp_lim[PORT_LIM_LIVES] = v[PORT_SCEN_LIVES];
}

/* THE LIMITS DROP-DOWN: A on a Time Limit / Kill Limit / Lives value opens the
 * options sheet's CRT list of that field's values (portWsetList*, slot
 * PORT_LIM_LIST_BASE + field); D-pad left / right still step. */
static const s32 *portLimListOf(s32 field, s32 *n)
{
    *n = (field == PORT_SCEN_TIME) ? ARRAYCOUNT(port_lim_time) : (field == PORT_SCEN_KILLS) ? ARRAYCOUNT(port_lim_kills)
                                                               : ARRAYCOUNT(port_lim_lives);
    return (field == PORT_SCEN_TIME) ? port_lim_time : (field == PORT_SCEN_KILLS) ? port_lim_kills : port_lim_lives;
}

static s32 portLimListCount(s32 field)
{
    s32 n;

    portLimListOf(field, &n);
    return n;
}

static const char *portLimListText(s32 field, s32 idx)
{
    static char text[24];
    s32 n;
    const s32 x = portLimListOf(field, &n)[idx];

    if (x <= 0)
    {
        return (field == PORT_SCEN_LIVES) ? "Unlimited" : "None";
    }
    snprintf(text, sizeof(text), (field == PORT_SCEN_TIME) ? "%d Min" : "%d", (int) x);
    return text;
}

/* the entry for the value in force: the nearest one below, as portLimStep reads it */
static s32 portLimListLive(s32 field)
{
    s32 v[PORT_SCEN_FIELDS];
    s32 n, k, at = 0;
    const s32 *list = portLimListOf(field, &n);

    portLimLive(v);
    for (k = 0; k < n; k++)
    {
        if (list[k] <= v[field])
        {
            at = k;
        }
    }
    return at;
}

/* the field's row: Time is the first */
static s32 portLimListRowY(s32 field)
{
    return PORT_MP_PICK_Y0 + (field - 1) * PORT_MP_PICK_PITCH;
}

static void portLimListPick(s32 field, s32 idx)
{
    s32 n;
    const s32 x = portLimListOf(field, &n)[idx];

    port_mp_lim[field - PORT_SCEN_TIME] = x;   /* fields 1-3 are PORT_LIM_TIME..LIVES */
}

/* parse a whole number, sign allowed (the settings file) */
static s32 portMpSetInt(const char *s)
{
    s32 neg = (*s == '-');
    s32 v = 0;

    s += neg;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s++ - '0');
    }
    return neg ? -v : v;
}

/* Save Settings: the settings in force to Data/MatchSettings/<name>.gemp */
static s32 portMpSetSave(const char *name)
{
    static char text[4096];
    s32 len = 0;
    s32 i;

#define PORT_MPSET_PUT(...) \
    len += snprintf(text + len, (len < (s32) sizeof(text)) ? sizeof(text) - len : 0, __VA_ARGS__)
    PORT_MPSET_PUT("[mpsettings]\nname=%s\nversion=1\n", name);
    PORT_MPSET_PUT("scenario=%d\n", (int) ((scenario < PORT_SCEN_LISTED) ? scenario : SCENARIO_NORMAL));
    PORT_MPSET_PUT("level=%d\n", (int) MP_stage_selected);
    PORT_MPSET_PUT("time=%d\nkills=%d\nlives=%d\n", (int) port_mp_lim[PORT_LIM_TIME],
                   (int) port_mp_lim[PORT_LIM_KILLS], (int) port_mp_lim[PORT_LIM_LIVES]);
    PORT_MPSET_PUT("teams=%d\n", (int) port_mp_teams_on);
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        PORT_MPSET_PUT("team%d=%d\n", (int) i + 1, (int) port_mp_team[i]);
    }
    PORT_MPSET_PUT("weapons=%d\n", (int) getMPWeaponSet());
    if (portMpWeaponIsCustom())
    {   /* the eight weapons themselves, so a renamed or deleted set still loads */
        s32 items[8];

        portMpWeaponSetItems(getMPWeaponSet(), items);
        PORT_MPSET_PUT("weapons_name=%s\n", portWsetCurrent());
        for (i = 0; i < 8; i++)
        {
            PORT_MPSET_PUT("weapon%d=%d\n", (int) i + 1, (int) items[i]);
        }
    }
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        PORT_MPSET_PUT("health%d=%d\n", (int) i + 1, (int) player_handicap[i]);
    }
    PORT_MPSET_PUT("[gameplay]\n");
    for (i = 0; i < (s32) ARRAYCOUNT(port_sheet_rows_mpopts); i++)
    {   /* every Gameplay Options switch, as midas.ini spells it */
        const struct PortSheetRow *r = &port_sheet_rows_mpopts[i];
        const s32 o = (r->kind == SHROW_OPT && r->key != NULL) ? portOptFind(r->key) : -1;
        char val[32];

        if (o >= 0 && portOptValueIni(o, val, sizeof(val)))
        {
            PORT_MPSET_PUT("%s=%s\n", r->key, val);
        }
    }
#undef PORT_MPSET_PUT
    if (len >= (s32) sizeof(text))
    {
        return FALSE;
    }
    return portMpSetWrite(name, text);
}

/* Load Settings: put a saved file in force; a key missing or out of range
 * keeps what is in force */
static s32 portMpSetApply(const char *name)
{
    static char text[4096];
    char wname[24] = "";
    s32 scen = -1, level = -1, teams = -1, wset = -1, wgot = 0;
    s32 lim[PORT_LIM_COUNT] = { -1, -1, -1 };
    s32 team[MAX_PLAYER_COUNT] = { -1, -1, -1, -1 };
    s32 health[MAX_PLAYER_COUNT] = { -1, -1, -1, -1 };
    s32 witems[8];
    char *line, *next;
    s32 i;

    if (!portMpSetRead(name, text, sizeof(text)))
    {
        return FALSE;
    }
    portOptSaveHold(TRUE);   /* one ini write for all the Gameplay Options */
    for (line = text; line != NULL && *line != '\0'; line = next)
    {
        char *eq, *val;
        s32 len, v;

        next = strchr(line, '\n');
        if (next != NULL)
        {
            *next++ = '\0';
        }
        len = (s32) strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
        {
            line[--len] = '\0';
        }
        eq = strchr(line, '=');
        if (eq == NULL || line[0] == '[' || line[0] == ';')
        {
            continue;
        }
        *eq = '\0';
        val = eq + 1;
        v = portMpSetInt(val);
        if (strcmp(line, "scenario") == 0)          scen = v;
        else if (strcmp(line, "level") == 0)        level = v;
        else if (strcmp(line, "time") == 0)         lim[PORT_LIM_TIME] = v;
        else if (strcmp(line, "kills") == 0)        lim[PORT_LIM_KILLS] = v;
        else if (strcmp(line, "lives") == 0)        lim[PORT_LIM_LIVES] = v;
        else if (strcmp(line, "teams") == 0)        teams = v;
        else if (strcmp(line, "weapons") == 0)      wset = v;
        else if (strcmp(line, "weapons_name") == 0) snprintf(wname, sizeof(wname), "%s", val);
        else if (strncmp(line, "team", 4) == 0 && line[4] >= '1' && line[4] <= '4' && line[5] == '\0')
        {
            team[line[4] - '1'] = v;
        }
        else if (strncmp(line, "health", 6) == 0 && line[6] >= '1' && line[6] <= '4' && line[7] == '\0')
        {
            health[line[6] - '1'] = v;
        }
        else if (strncmp(line, "weapon", 6) == 0 && line[6] >= '1' && line[6] <= '8' && line[7] == '\0')
        {
            witems[line[6] - '1'] = v;
            wgot |= 1 << (line[6] - '1');
        }
        else if (strncmp(line, "mp_", 3) == 0 && portOptFind(line) >= 0)
        {
            portOptSetIni(portOptFind(line), val);
        }
    }
    portOptSaveHold(FALSE);

    if (portScenValid(scen))
    {   /* first: Rare's reset also resets the length and may change the set */
        reset_mp_options_for_scenario(scen);
    }
    if (level >= 0 && level < MP_STAGE_SELECTED_MAX && check_if_mp_stage_unlocked(level))
    {
        MP_stage_selected = level;
    }
    for (i = 0; i < PORT_LIM_COUNT; i++)
    {
        if (lim[i] >= 0)
        {
            port_mp_lim[i] = (lim[i] > 999) ? 999 : lim[i];   /* as port_scens.c: minutes * 3600 stays inside an s32 */
        }
    }
    if (teams >= 0)
    {
        port_mp_teams_on = (teams != 0);
    }
    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        if (team[i] >= 0)
        {
            port_mp_team[i] = (team[i] != 0);
        }
        if (health[i] >= 0 && health[i] <= 10)
        {
            player_handicap[i] = health[i];
        }
    }
    portMpTeamsEnsure();
    if (wset == PORT_MP_WEAPONSET_CUSTOM_IDX && wgot == 0xFF)
    {
        portMpWeaponCustomApply(witems);
        portWsetSetCurrent(wname);
    }
    else if (wset >= 0 && ((wset < PORT_MP_WEAPONSET_CUSTOM_IDX && wset < portMpWeaponSetCount()) || wset == 15 || wset == 16))
    {
        setMPWeaponSet(wset);
        portWsetSetCurrent("");
    }
    portMpSetSetCurrent(name);
    return TRUE;
}

static s32 portLimRowGrey(s32 i)
{
    if (port_mp_pick == PORT_MP_PICK_LIMITS)
    {
        return (i == PORT_LIM_TEAMS_ROW) ? !portMpTeamsAllowed()
             : (i < PORT_LIM_TEAMS_ROW) ? portLimFixed(scenario, i + 1) : FALSE;
    }
    return (i == 0) ? (portMpSetCount() == 0)                  /* Load with nothing saved */
         : (i == 2) ? (portMpSetCurrent()[0] == '\0') : FALSE; /* Delete with no name */
}

static s32 portLimRowCount(void)
{
    if (port_mp_pick == PORT_MP_PICK_LIMITS)
    {
        return PORT_LIM_TEAMS_ROW + 1 + (portMpTeamsPicked() ? get_selected_num_players() : 0);
    }
    return PORT_MPSET_ROWS;
}

/* Load / Save Settings: opened from the setup's bottom row */
static void portMpSetOpen(void)
{
    portMpSetRescan();
    port_mpset_del_armed = FALSE;
    portMpPickOpen(PORT_MP_PICK_SETTINGS);
}

/* Game Limits and Load / Save Settings: the input */
static void portLimInterface(void)
{
    const s32 press = joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON);
    const s32 back = joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON);
    const s32 n = portLimRowCount();
    const s32 hov = port_mp_pick_hover;
    const s32 limits = (port_mp_pick == PORT_MP_PICK_LIMITS);

    portMpTeamsEnsure();   /* a side emptied by fewer players gets the first split */
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);

    if (port_osk_open)
    {   /* Save Settings: the keyboard owns the input */
        const f32 hx = cursor_h_pos;
        const f32 hy = cursor_v_pos;

        if (portOskTick() && port_osk_done > 0)
        {
            char clean[24];
            const s32 usable = portWsetCleanName(port_osk_text, clean, sizeof(clean));
            s32 exists = FALSE;
            s32 k;

            for (k = 0; usable && k < portMpSetCount(); k++)
            {
                exists |= (strcmp(portMpSetName(k), clean) == 0);
            }
            if (!usable || (exists && strcmp(clean, portMpSetCurrent()) != 0) || (!exists && portMpSetFull()))
            {
                portOskOpen(!usable ? "NAME THESE SETTINGS" : exists ? "NAME IN USE" : "LIST FULL", port_osk_text);
            }
            else if (portMpSetSave(clean))
            {
                portMpSetSetCurrent(clean);
                portMpSetRescan();
            }
        }
        frontUpdateControlStickPosition();
        if (!port_osk_typing)
        {
            cursor_h_pos = hx;
            cursor_v_pos = hy;
        }
        return;
    }

    if (portWsetListTick())
    {   /* a limit's or Load Settings' drop-down */
        return;
    }

    if (joyGetButtons(PLAYER_1, Z_TRIG|A_BUTTON) == 0)
    {   /* the highlight follows the cursor while nothing is held */
        s32 i;

        tab_prev_highlight = frontCheckCursorOnPreviousTab();
        port_mp_pick_hover = -1;
        for (i = 0; !tab_prev_highlight && i < n; i++)
        {
            s32 x, y;

            portLimRowPos(i, &x, &y);
            if ((s32) cursor_v_pos >= y - 1 && (s32) cursor_v_pos < y - 1 + PORT_MP_PICK_PITCH
                && (s32) cursor_h_pos >= x - 4 && (s32) cursor_h_pos < x + 0xF0)
            {
                port_mp_pick_hover = i;
            }
        }
        if (limits || port_mp_pick_hover != 2)
        {
            port_mpset_del_armed = FALSE;
        }
    }

    if (limits && hov >= 0)
    {   /* D-pad left / right on a value row */
        const s32 dir = joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD) ? -1
                      : joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD) ? 1 : 0;

        if (dir != 0 && !portLimRowGrey(hov))
        {
            if (hov < PORT_LIM_TEAMS_ROW)
            {
                portLimLiveStep(hov, dir);
            }
            else if (hov == PORT_LIM_TEAMS_ROW)
            {
                port_mp_teams_on = !port_mp_teams_on;
                portMpTeamsEnsure();
            }
            else
            {
                portLimTeamSwap(hov - PORT_LIM_TEAMS_ROW - 1);
            }
        }
    }

    if (back || (press && tab_prev_highlight))
    {   /* back to the setup */
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        portMpPickLeave();
        return;
    }
    if (!press || hov < 0)
    {
        frontUpdateControlStickPosition();
        return;
    }
    sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    if (portLimRowGrey(hov))
    {
        /* nothing to do there */
    }
    else if (limits)
    {
        if (hov < PORT_LIM_TEAMS_ROW)
        {
            portWsetListOpenFor(PORT_LIM_LIST_BASE + hov + 1);   /* rows 0-2 are fields 1-3 */
        }
        else if (hov == PORT_LIM_TEAMS_ROW)
        {
            port_mp_teams_on = !port_mp_teams_on;
            portMpTeamsEnsure();
        }
        else
        {
            portLimTeamSwap(hov - PORT_LIM_TEAMS_ROW - 1);
        }
    }
    else if (hov == 0)
    {
        portWsetListOpenFor(PORT_MPSET_LIST_LOAD);
    }
    else if (hov == 1)
    {
        portOskOpen("SAVE SETTINGS AS", (portMpSetCurrent()[0] != '\0') ? portMpSetCurrent() : NULL);
    }
    else if (!port_mpset_del_armed)
    {
        port_mpset_del_armed = TRUE;
    }
    else
    {
        portMpSetDelete(portMpSetCurrent());
        port_mpset_del_armed = FALSE;
    }
    frontUpdateControlStickPosition();
}

static Gfx *portLimConstruct(Gfx *DL)
{
    char line[48];
    s32 v[PORT_SCEN_FIELDS];
    s32 x, y, h, w, i;
    const s32 n = portLimRowCount();
    const s32 limits = (port_mp_pick == PORT_MP_PICK_LIMITS);

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    snprintf(line, sizeof(line), "%s\n", limits ? "Game Limits" : "Load / Save Settings");
    x = 0x37;
    y = 0x66;
    DL = frontPrintText(DL, &x, &y, (u8 *) line, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    portLimLive(v);
    for (i = 0; i < n; i++)
    {
        static const char *const limlabel[PORT_LIM_TEAMS_ROW + 1] = { "Time Limit", "Kill Limit", "Lives", "Teams" };
        static const char *const setlabel[PORT_MPSET_ROWS] = { "Load Settings", "Save Settings", "Delete Settings" };
        const s32 alpha = portLimRowGrey(i) ? 0x70 : 0xFF;
        const char *value = NULL;
        u32 valcolour = 0;   /* RGB over alpha: a side's name in its own colour */

        if (limits && i > PORT_LIM_TEAMS_ROW)
        {   /* a player's side */
            const s32 p = i - PORT_LIM_TEAMS_ROW - 1;

            snprintf(line, sizeof(line), "Player %d\n", (int) p + 1);
            value = port_mp_team[p] ? "Blue\n" : "Red\n";
            valcolour = port_mp_team[p] ? 0x2828FF00 : 0xD0000000;   /* the radar's blue and red */
        }
        else if (limits)
        {
            snprintf(line, sizeof(line), "%s\n", limlabel[i]);
            value = (i == PORT_LIM_TEAMS_ROW) ? portLimTeamsText() : portLimValue(v, i + 1);
        }
        else
        {
            static char named[32];

            snprintf(line, sizeof(line), "%s\n", (i == 2 && port_mpset_del_armed) ? "Confirm Delete" : setlabel[i]);
            if (i > 0 && portMpSetCurrent()[0] != '\0')
            {
                snprintf(named, sizeof(named), "%s\n", portMpSetCurrent());
                value = named;
            }
        }
        textMeasure(&h, &w, (u8 *) line, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        portLimRowPos(i, &x, &y);
        if (i == port_mp_pick_hover)
        {
            s32 vw = 0;

            if (value != NULL)
            {
                textMeasure(&h, &vw, (u8 *) value, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
                w = (limits ? PORT_LIM_VALUE_X : PORT_MPSET_VALUE_X) - x + vw;
            }
            DL = microcode_constructor_related_to_menus(DL, x - 2, y - 1, x + w + 3, y + 0xE, 0x32);
        }
        DL = frontPrintText(DL, &x, &y, (u8 *) line, ptrFontZurichBoldChars, ptrFontZurichBold, alpha, viGetX(), viGetY(), 0, 0);
        if (value != NULL)
        {
            portLimRowPos(i, &x, &y);
            x = limits ? PORT_LIM_VALUE_X : PORT_MPSET_VALUE_X;
            DL = frontPrintText(DL, &x, &y, (u8 *) value, ptrFontZurichBoldChars, ptrFontZurichBold, valcolour | alpha, viGetX(), viGetY(), 0, 0);
        }
    }

    DL = frontAddPreviousTabText(DL);
    if (port_wset_list_slot >= 0)
    {
        DL = portWsetListDraw(DL);
    }
    if (port_osk_open)
    {
        DL = portOskDraw(DL);
    }
    DL = frontDrawCursor(DL);
    return DL;
}

/* Rare's team screen leaves to the setup (only demos' and GE_MPSCENARIO's
 * team scenarios reach it now: teams are set on Game Limits) */
static void portTeamsLeave(void)
{
    frontChangeMenu(MENU_MP_OPTIONS, FALSE);
}

static void portMpPickInterface(void)
{
    const s32 n = portMpPickCount();

    if (port_mp_pick == PORT_MP_PICK_CUSTOM || port_mp_pick == PORT_MP_PICK_EDIT)
    {
        portWsetInterface();
        return;
    }
    if (port_mp_pick >= PORT_MP_PICK_LIMITS)
    {
        portLimInterface();
        return;
    }

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    if (joyGetButtons(PLAYER_1, Z_TRIG|A_BUTTON) == 0)
    {   /* the highlight follows the cursor while nothing is held, as Scenario's does */
        tab_prev_highlight = frontCheckCursorOnPreviousTab();
        port_mp_pick_hover = -1;
        if (!tab_prev_highlight && port_mp_pick == PORT_MP_PICK_WEAPONS && (s32) cursor_v_pos >= PORT_MP_PICK_Y0 - 1)
        {
            const s32 row = ((s32) cursor_v_pos - (PORT_MP_PICK_Y0 - 1)) / PORT_MP_WPN_PITCH;
            const s32 col = ((s32) cursor_h_pos >= 0x39 + PORT_MP_PICK_COLW - 4) ? 1 : 0;

            port_mp_pick_hover = portMpWpnAt(col, row, n);
        }
        else if (!tab_prev_highlight && (s32) cursor_v_pos >= PORT_MP_PICK_Y0 - 1)
        {
            const s32 row = ((s32) cursor_v_pos - (PORT_MP_PICK_Y0 - 1)) / PORT_MP_PICK_PITCH;
            const s32 col = (n > PORT_MP_PICK_PERCOL && (s32) cursor_h_pos >= 0x39 + PORT_MP_PICK_COLW - 4) ? 1 : 0;
            const s32 i = col * PORT_MP_PICK_PERCOL + row;

            if (row < PORT_MP_PICK_PERCOL && i < n)
            {
                port_mp_pick_hover = i;
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        if (tab_prev_highlight)
        {
            portMpPickLeave();
            return;
        }
        if (port_mp_pick_hover == PORT_MP_PICK_CUSTOM_CELL)
        {   /* Custom: the saved sets */
            portWsetListOpen();
            return;
        }
        if (port_mp_pick_hover == PORT_MP_PICK_RANDOM_CELL || port_mp_pick_hover == PORT_MP_PICK_RANDSET_CELL)
        {   /* rolled at every match's stage load (mp_weapon.c portMpWeaponRoll) */
            setMPWeaponSet((port_mp_pick_hover == PORT_MP_PICK_RANDOM_CELL) ? 15 : 16);
            portWsetSetCurrent("");
            portMpPickLeave();
            return;
        }
        if (port_mp_pick_hover >= 0)
        {
            if (port_mp_pick == PORT_MP_PICK_LENGTH)
            {
                game_length = port_mp_pick_hover;
                portMpLimFromLength(game_length);
            }
            else
            {
                setMPWeaponSet(port_mp_pick_hover);
                portWsetSetCurrent("");   /* a built-in set: no custom one in force */
            }
            portMpPickLeave();
            return;
        }
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        portMpPickLeave();
        return;
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();
}

static Gfx *portMpPickConstruct(Gfx *DL)
{
    const s32 n = portMpPickCount();

    if (port_mp_pick == PORT_MP_PICK_CUSTOM || port_mp_pick == PORT_MP_PICK_EDIT)
    {
        return portWsetConstruct(DL);
    }
    if (port_mp_pick >= PORT_MP_PICK_LIMITS)
    {
        return portLimConstruct(DL);
    }
    /* the bar sits on the row under the cursor, else on the value in force */
    const s32 lit = (port_mp_pick_hover >= 0) ? port_mp_pick_hover : portMpPickCurrent();
    s32 x;
    s32 y;
    s32 i;
    s32 h;
    s32 w;
    u8 *text;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    /* two whole getStringID calls: the macro does not parenthesise its slot,
     * so a ?: inside it binds to the bank's + and drops the bank */
    text = langGet((port_mp_pick == PORT_MP_PICK_LENGTH) ? getStringID(LTITLE, TITLE_STR_80_GAMELENGTH)
                                                         : getStringID(LTITLE, TITLE_STR_81_WEAPONS));
    x = 0x37;
    y = 0x66;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    for (i = (port_mp_pick == PORT_MP_PICK_WEAPONS) ? -1 : 0; i < n + ((port_mp_pick == PORT_MP_PICK_WEAPONS) ? 2 : 0); i++)
    {
        /* i = -1: the Weapons list's Custom row, lit while a custom set is in force;
         * i = n, n+1: Random and Random Set */
        const s32 cell = (i < 0) ? PORT_MP_PICK_CUSTOM_CELL : (i == n) ? PORT_MP_PICK_RANDOM_CELL
                       : (i == n + 1) ? PORT_MP_PICK_RANDSET_CELL : i;
        const s32 islit = (port_mp_pick == PORT_MP_PICK_WEAPONS)
                        ? ((port_mp_pick_hover >= 0) ? (cell == port_mp_pick_hover)
                                                     : (portMpWeaponIsCustom() ? (cell == PORT_MP_PICK_CUSTOM_CELL)
                                                        : (getMPWeaponSet() == 15) ? (cell == PORT_MP_PICK_RANDOM_CELL)
                                                        : (getMPWeaponSet() == 16) ? (cell == PORT_MP_PICK_RANDSET_CELL)
                                                        : (cell == getMPWeaponSet())))
                        : (i == lit);

        text = (i < 0) ? (u8 *) "Custom\n" : (i == n) ? (u8 *) "Random\n" : (i == n + 1) ? (u8 *) "Random Set\n"
             : portMpPickText(i);
        textMeasure(&h, &w, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        if (port_mp_pick == PORT_MP_PICK_WEAPONS)
        {
            portMpWpnPos(cell, &x, &y);
        }
        else
        {
            portMpPickPos(i, &x, &y);
        }
        if (islit)
        {
            DL = microcode_constructor_related_to_menus(DL, x - 2, y - 1, x + w + 3, y + 0xE, 0x32);
        }
        DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    }

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}
#endif






// Address 0x7F0147B8 NTSC
void interface_menu13_mpscenario(void)
{
    s32 isTeam;
    s32 scenarioid;
    s32 i;

#ifndef TARGET_N64
    if (port_mp_pick != PORT_MP_PICK_SCENARIO)
    {
        portMpPickInterface();   /* Game Length / Weapons list (above) */
        return;
    }
#endif
    isTeam = FALSE;
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    if (joyGetButtons(PLAYER_1, Z_TRIG|A_BUTTON) == 0) {
        tab_prev_highlight = FALSE;
        tab_next_highlight = FALSE;
        tab_start_highlight = FALSE;

        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = TRUE;
            dword_CODE_bss_80069780 = SCENARIO_NORMAL;
        }
        else
        {
            dword_CODE_bss_80069780 = SCENARIO_YOLT;

#ifndef TARGET_N64
            for (i = PORT_SCEN_LISTED - 1; i != 0; i--)   /* no team scenarios */
#else
            for (i = 7; i != 0; i--)
#endif
            {
                if (
                    (s32)cursor_v_pos  >=  (0x83 + (-i * -0x16))
                    && (mp_player_counts[i].min <= get_selected_num_players())
                    && (get_selected_num_players() <= mp_player_counts[i].max))
                {
                    dword_CODE_bss_80069780 = i + 1;
                    break;
                }
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {
        if (tab_prev_highlight)
        {
            tab_prev_selected = TRUE;
        }
        else
        {
            scenarioid = dword_CODE_bss_80069780 - 1;

            if ((scenarioid == SCENARIO_2v2) || (scenarioid == SCENARIO_3v1) || (scenarioid == SCENARIO_2v1))
            {
                reset_mp_options_for_scenario(scenarioid);
                isTeam = TRUE;
            }
            else
            {
                reset_mp_options_for_scenario(scenarioid);
                tab_prev_selected = TRUE;
            }
        }

        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();

    if (isTeam)
    {
        frontChangeMenu(MENU_MP_TEAMS, FALSE);
        return;
    }

    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MP_OPTIONS, FALSE);
    }
}


// Address 0x7F014A48 NTSC
Gfx * constructor_menu13_mpscenario(Gfx *DL)
{
    s32 padding1;
    s32 padding2;
    s32 padding3;
    s32 x; // sp88
    s32 y; // sp84
    s32 i;
    s32 sp7C;
    s32 sp78;

    u8 *text;
    s32 var_s3;

#ifndef TARGET_N64
    if (port_mp_pick != PORT_MP_PICK_SCENARIO)
    {
        return portMpPickConstruct(DL);   /* Game Length / Weapons list */
    }
#endif

    DL = viSetFillColor(DL,0,0,0);
    DL = viFillScreen(DL);
#ifdef VERSION_EU
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
#endif
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    text = langGet(getStringID(LTITLE, TITLE_STR_87_SCENARIO2));

    x = 0x37;
    y = 0x66;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

#ifndef TARGET_N64
    for (i = 0; i < PORT_SCEN_LISTED; i++)   /* no team scenarios: teams are on Game Limits */
#else
    for (i = 0; i < 8; i++)
#endif
    {
        var_s3 = 0xff;

        if (((s32) mp_player_counts[i].max < get_selected_num_players()) || (get_selected_num_players() < (s32) mp_player_counts[i].min))
        {
            var_s3 = 0x70;
        }

        text = langGet(mp_player_counts[i].stage);

        textMeasure(&sp7C, &sp78, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        x = 0x39;
        y = 0x83 + (i * 0x16);
        if ((i + 1) == dword_CODE_bss_80069780)
        {
            DL = microcode_constructor_related_to_menus(DL, 0x37, y - 1, sp78 + 0x3C, y + 0xE, 0x32);
        }

        DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, var_s3, viGetX(), viGetY(), 0, 0);
    }


    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);

    return DL;
}




s32 get_players_team_or_scenario_item_flag(int player)
{
  return g_playerPlayerData[player].have_token_or_goldengun;
}

void set_players_team_or_scenario_item_flag(int player,s32 flag)
{
  g_playerPlayerData[player].have_token_or_goldengun = flag;
}



//********************************************************************************************************
//MULTIPLAYER TEAM SELECT
//********************************************************************************************************

void init_menu14_mpteamsel(void)
{
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;

    if (scenario == SCENARIO_2v1) {
        teamsize = 2;
    }
    else {
        teamsize = 3;
    }

    D_8002B560 = 0;
    load_walletbond();
}

void update_menu14_mpteams(void) 
{
    return;
}


// Address 0x7F014D48 NTSC
void interface_menu14_mpteams(void)
{
    s32 i;
    s32 var_a1;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    D_8002B560 = (s32) (D_8002B560 + 1) % 20;

#ifndef TARGET_N64
    /* Port: B backs out of Teams like every other MP screen (vanilla has
     * no back here; needed for the mouse-back B injection in menu_init). */
    if (joyGetButtonsPressedThisFrame(0, B_BUTTON))
    {
        portTeamsLeave();   /* MP setup, or Limits if it came from there */
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else
#endif
    // 0x101
    if ((joyGetButtonsPressedThisFrame(0, R_JPAD|R_CBUTTONS)) || (joyGetStickXInRange(0, -2, 1) > 0))
    {
        if (scenario == SCENARIO_2v1)
        {
            if (!(teamsize & 1))
            {
                teamsize = 1;
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }
        }
        else if (!(teamsize & 1))
        {
            teamsize += 1;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
    }
    // 0x808
    else if ((joyGetButtonsPressedThisFrame(0, U_JPAD|U_CBUTTONS)) || (joyGetStickYInRange(0, -2, 1) > 0))
    {
        if (scenario == SCENARIO_2v2)
        {
            if (teamsize & 2)
            {
                teamsize = 1;
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }
        }
        else if (teamsize & 2)
        {
            teamsize -= 2;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
    }
    // 0x202
    else if ((joyGetButtonsPressedThisFrame(0, L_JPAD|L_CBUTTONS)) || (joyGetStickXInRange(0, -2, 1) < -1))
    {
        if (scenario == SCENARIO_2v2)
        {
            if (teamsize & 1)
            {
                teamsize = 2;
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }
        }
        else if (teamsize & 1)
        {
            teamsize -= 1;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
    }
    // 0x404
    else if ((joyGetButtonsPressedThisFrame(0, D_JPAD|D_CBUTTONS)) || (joyGetStickYInRange(0, -2, 1) < -1))
    {
        if (scenario == SCENARIO_2v1)
        {
            if (!(teamsize & 2))
            {
                teamsize = 2;
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
            }
        }
        else if (!(teamsize & 2))
        {
            teamsize += 2;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    frontUpdateControlStickPosition();

    if (joyGetButtonsPressedThisFrame(0, A_BUTTON|Z_TRIG|START_BUTTON)) // 0xb000
    {
        for (i = 0; i < 4; i++)
        {
            if (scenario == SCENARIO_2v2)
            {

                if (i == 0 || i == teamsize)
                {
                    var_a1 = 0;
                }
                else
                {
                    var_a1 = 1;
                }

                set_players_team_or_scenario_item_flag(i, var_a1);
            }
            else
            {
                if (i != teamsize)
                {
                    var_a1 = 0;
                }
                else
                {
                    var_a1 = 1;
                }

                set_players_team_or_scenario_item_flag(i, var_a1);
            }
        }

#ifndef TARGET_N64
        portTeamsLeave();
#else
        frontChangeMenu(MENU_MP_OPTIONS, 0);
#endif
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
}


/**
 * Address: 7F015138
 */
Gfx *constructor_menu14_mpteams(Gfx *DL)
{
    s32 numPlayers;
    s32 playerIndex;
    s32 layoutOffset;
    s32 titleY;
    s32 rectRightScaled;
    s32 useFirstTeam;
    s32 centerX;
    s32 rowTop;
    char *text;
    s32 textColour;
    s32 titleHeight;
    s32 titleWidth;
    s32 titleX;
    s32 titleYPos;
    s32 padding1;
    s32 textHeight;
    s32 textWidth;
    s32 textX;
    s32 textY;
    char *titleText;

    numPlayers = get_selected_num_players();

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);
    DL = microcode_constructor_related_to_menus(DL, 0x26, 0xA9, 0x184, 0xAB, 0x90);

    if (numPlayers >= 3)
    {
        DL = microcode_constructor_related_to_menus(DL, 0xD4, 0x1E, 0xD6, 0x136, 0x80);
    }

    for (playerIndex = 0; playerIndex < numPlayers; playerIndex++)
    {
        layoutOffset = (0, (playerIndex >= 2) ? 0x8C : 0);
        rowTop = layoutOffset + 0x1E;
        layoutOffset = ((playerIndex & 1) ? 0xAF : 0) + 0;

        gDPSetScissor(DL++, G_SC_NON_INTERLACE, layoutOffset + 0x2c, rowTop + 5, layoutOffset + 0xcf, rowTop + 0x87);

        centerX = layoutOffset + 0x7d;

        if (playerIndex == 0)
        {
            text = langGet(getStringID(LTITLE, TITLE_STR_88_SELECTTEAMS)); // "Select Teams"
            titleText = text;

            textMeasure(&titleHeight, &titleWidth, titleText, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

            titleYPos = rowTop + 5;
            titleX = centerX - (titleWidth >> 1);

            DL = frontPrintText(DL, &titleX, &titleYPos, titleText, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        }

        if (scenario == SCENARIO_2v2)
        {
            useFirstTeam = (playerIndex == 0) || (playerIndex == teamsize);
        }
        else
        {
            useFirstTeam = playerIndex != teamsize;
        }

        if (useFirstTeam)
        {
            text = langGet(getStringID(LTITLE, TITLE_STR_89_RED)); // "Red Team"
        }
        else
        {
            text = langGet(getStringID(LTITLE, TITLE_STR_90_BLUE)); // "Blue Team"
        }

        if (useFirstTeam)
        {
            textColour = 0xFF0000FF;
        }
        else
        {
            textColour = 0x0000FFFF;
        }

        textMeasure(&textHeight, &textWidth, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        textX = centerX - (textWidth >> 1);
        textY = (rowTop - (textHeight >> 1)) + 0x46;

        if ((playerIndex != teamsize) || ((playerIndex == teamsize) && (((s32) D_8002B560) < 0xA)))
        {
            DL = frontPrintText(DL, &textX, &textY, text, ptrFontZurichBoldChars, ptrFontZurichBold, textColour, viGetX(), viGetY(), 0, 0);
        }
    }

    DL = combiner_bayer_lod_perspective(DL);

    return DL;
}


void set_briefing_page(WATCH_BRIEFING_PAGE page)
{
    short new_var;

    // could be a dev typo
    if (current_menu_briefing_page = page)
    {
        // maybe previous code was optimized out
    }
}


// Address 0x7F015520 NTSC
void load_briefing_text_for_stage(void)
{
    u8 *temp_s0;
    s32 argg;

#ifndef TARGET_N64
    /* PORT: stages without a folder entry (cuba, or any unmatched direct
     * launch) leave briefingpage invalid - fall back to the first entry
     * rather than indexing off the table (wrong briefing text beats a
     * NULL-filename crash in the fail/complete menus). */
    if (briefingpage < 0)
    {
        briefingpage = 0;
    }
#endif

    // The briefing and language bank share the remaining menu buffer.
    temp_s0 = ptr_logo_and_walletbond_DL + 4096 * 10;

    // alright
    argg = 0x200;
    ptrbriefingdata = _fileNameLoadToAddr(mission_folder_setup_entries[briefingpage].briefing_name_ptr, FILELOADMETHOD_DEFAULT, (u8 *) temp_s0, argg);

    temp_s0 += argg;

    // what is this
    argg *= 879;

    langLoadToBank(langGetLangBankIndexFromStagenum(mission_folder_setup_entries[briefingpage].stage_id), (u8 *)temp_s0, argg);

    // what is this
    for (argg = 0; argg < OBJECTIVES_MAX; argg++)
    {
        if (ptrbriefingdata->objective[argg].textid == 0)
        {
            break;
        }
    }
}

//********************************************************************************************************
//BRIEFING SCREEN
//********************************************************************************************************
void init_menu0A_briefing(void)
{
    tab_next_selected = 0;
    tab_prev_selected = 0;
    final_menu_briefing_page = 5;
    set_briefing_page(0);
    load_walletbond();
    load_briefing_text_for_stage();
}


void update_menu0A_briefing(void)
{
  langClearBank(langGetLangBankIndexFromStagenum(mission_folder_setup_entries[briefingpage].stage_id));
  if (-1 < menu_update) {
    frontCleanUpWalletBond();
  }
}


void interface_menu0A_briefing(void)
{
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = TRUE;
    }
    else if ((frontCheckCursorOnNextTab()) && (current_menu_briefing_page < (final_menu_briefing_page - 1)))
    {
        tab_next_highlight = TRUE;
    }
    else if (frontCheckCursorOnStartTab())
    {
        tab_start_highlight = TRUE;
    }
    else if (current_menu_briefing_page < (final_menu_briefing_page - 1))
    {
        tab_next_highlight = TRUE;
    }
    else
    {
        tab_start_highlight = TRUE;
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON))
    {
        tab_next_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON))
    {
        if (tab_next_highlight)
        {
            set_briefing_page(current_menu_briefing_page + 1);
        }
        else if (tab_prev_highlight)
        {
            if (current_menu_briefing_page > BRIEFING_TITLE)
            {
                set_briefing_page(current_menu_briefing_page - 1);
            }
            else
            {
                tab_prev_selected = TRUE;
            }
        }
        else if (tab_start_highlight)
        {
            tab_next_selected = TRUE;
        }
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON) != 0)
    {
        if (current_menu_briefing_page > BRIEFING_TITLE)
        {
            set_briefing_page(current_menu_briefing_page - 1);
        }
        else
        {
            tab_prev_selected = TRUE;
        }
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_PHOTOBRIEF, current_menu_briefing_page == BRIEFING_TITLE);
    set_item_visibility_in_objinstance(walletinst[0], mission_folder_setup_entries[briefingpage].mission_num + 0x16, current_menu_briefing_page == BRIEFING_TITLE);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();
    if (tab_next_selected)
    {
        frontChangeMenu(MENU_RUN_STAGE, TRUE);
        return;
    }
    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MISSION_SELECT, FALSE);
        set_cursor_to_stage_solo(mission_folder_setup_entries[briefingpage].mission_num);
    }
}




// Address 0x7F015A08 NTSC
Gfx *print_objectives_and_status_to_menu(Gfx *DL, s32 arg1, u8 *arg2, s32 arg3)
{
    s32 i; // sp9C
    s32 sp98;
    s32 sp94;
    s32 sp90;
    s32 sp8C;
    s32 sp88;
    s32 var_s2;
    s32 var_fp;
    u8* text;
    s32 v1;

    sp98 = 0;
    var_fp = 0;
    i = 0;
    v1 = 0;

    for (i = 0; i < OBJECTIVES_MAX; i++)
    {
        if (ptrbriefingdata->objective[i].textid != 0 && selected_difficulty >= ptrbriefingdata->objective[i].enabled_difficulty)
        {
            v1 = ptrbriefingdata->objective[i].textid;
            text = langGet(v1);
            strcpy((u8*)arg2, "a.\n");
            *arg2 = *arg2 + sp98;

            sp94 = 0;
            sp90 = 0;
            textMeasure(&sp94, &sp90, (u8*)arg2, ptrFontZurichBoldChars, ptrFontZurichBold, 0);


            sp8C = 0x37;
            sp88 = (sp94 * var_fp) + arg1 + (i*0);
            DL = frontPrintText(DL, &sp8C, &sp88, (s8*)arg2, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

            sp8C = 0x4B;
            sp88 = (sp94 * var_fp) + arg1 + (i*0);

            if (arg3 != 0)
            {
                setTextWordWrap(2);
                textWrap(0xDC, (s8*)text, (s8*)arg2, ptrFontZurichBoldChars, ptrFontZurichBold);
                setTextWordWrap(0);
            }
            else
            {
                textWrap(0x140, (s8*)text, (s8*)arg2, ptrFontZurichBoldChars, ptrFontZurichBold);
            }

            DL = frontPrintText(DL, &sp8C, &sp88, (s8*)arg2,  ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

            if (arg3)
            {
                switch (get_status_of_objective(i))
                {
                    case 1:
                        text   = langGet(getStringID(LTITLE, TITLE_STR_91_COMPLETED));
                        var_s2 = 0xFF;
                        break;
                    default:
                    case 0:
                    case 2:
                        text   = langGet(getStringID(LTITLE, TITLE_STR_92_FAILED));
                        var_s2 = 0x780000FF;
                        break;
                }

                sp8C = 0x136;
                sp88 = (sp94 * var_fp) + arg1 + (i*0);

                DL = frontPrintText(DL, &sp8C, &sp88, (s8*)text, ptrFontZurichBoldChars, ptrFontZurichBold, var_s2, viGetX(), viGetY(), 0, 0);
            }

            var_fp += sub_GAME_7F0AC0E8(arg2);
            sp98++;
        }
    }

    return DL;
}


// Address 0x7F015D74 NTSC
#ifndef VERSION_EU
const struct MatchHack_front_rodata_3000 asc_D_80050C54 = { "\n" };
#endif

Gfx *constructor_menu0A_briefing(Gfx *DL)
{
    u8 *spC0C;
    s32 spC08;
    s32 spC04;
    struct MatchHack_front_rodata_3000 sp4C;

    DL = viSetFillColor(DL,0,0,0);
    DL = viFillScreen(DL);
#ifdef VERSION_EU
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
#endif
    DL = frontSetupMenuBackground(DL);

#if !defined(VERSION_EU)
    sp4C = asc_D_80050C54;
#endif

    DL = microcode_constructor(DL);
    DL = print_current_solo_briefing_stage_name(DL, (char*)&sp4C);

    switch (current_menu_briefing_page)
    {
        case BRIEFING_TITLE:
            spC0C = langGet(getStringID(LTITLE, TITLE_STR_93_PRIMARYOBJECTIVES ));
            break;

        case BRIEFING_OVERVIEW:
            spC0C = langGet(getStringID(LTITLE, TITLE_STR_94_BACKGROUND));
            break;

        case BRIEFING_M:
            spC0C = langGet(getStringID(LTITLE, TITLE_STR_95_MBRIEFING));
            break;

        case BRIEFING_Q:
            spC0C = langGet(getStringID(LTITLE, TITLE_STR_96_QBRANCH));
            break;

        case BRIEFING_MONEYPENNY:
            spC0C = langGet(getStringID(LTITLE, TITLE_STR_97_MONEYPENNY));
            break;
    }

    spC08 = 0x37;
    spC04 = 0x8F;
    DL = frontPrintText(DL, &spC08, &spC04, (s8*)spC0C, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    if (current_menu_briefing_page == BRIEFING_TITLE)
    {
        DL = print_objectives_and_status_to_menu(DL, 0xA7, (char*)&sp4C, 0);
    }
    else
    {
        switch (current_menu_briefing_page)
        {
            case BRIEFING_OVERVIEW:
                spC0C = langGet(ptrbriefingdata->brief[0]);
            break;

            case BRIEFING_M:
                spC0C = langGet(ptrbriefingdata->brief[1]);
            break;

            case BRIEFING_Q:
                spC0C = langGet(ptrbriefingdata->brief[2]);
            break;

            case BRIEFING_MONEYPENNY:
                spC0C = langGet(ptrbriefingdata->brief[3]);
            break;
        }

        spC08 = 0x37;
        spC04 = 0xA7;
        textWrap(0x140, (s8*)spC0C, (s8*)&sp4C, ptrFontZurichBoldChars, ptrFontZurichBold);
        setTextOverlapCorrection(8);
        DL = frontPrintText(DL, &spC08, &spC04, (s8*)&sp4C, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        setTextOverlapCorrection(-1);
    }

    DL = frontAddStartTabText(DL);
    DL = frontAddPreviousTabText(DL);

    if (current_menu_briefing_page < BRIEFING_MONEYPENNY)
    {
        DL = frontAddNextTabText(DL);
    }

    DL = frontDrawCursor(DL);

    return DL;
}



//********************************************************************************************************
//RUN STAGE
//********************************************************************************************************
void init_menu0B_runstage(void)
{
    bossSetLoadedStage(selected_stage);
    lvlSetSelectedDifficulty(selected_difficulty);
}


//********************************************************************************************************
//MISSION FAILED
//********************************************************************************************************
void init_menu0C_missionfailed(void)
{
  tab_next_selected = FALSE;
  tab_prev_selected = FALSE;
  load_walletbond();
  load_briefing_text_for_stage();
  frontSetCursorPositionToNextTab();
  if (maybe_is_in_menu != FALSE) {
#ifdef TARGET_N64
    sndApplyVolumeAllSfxSlot(0x7fff);
    musicTrack1ApplySeqpVol(0x7fff);
#else
    portApplyGlobalVolumesHere(); /* GLOBAL VOLUMES - was a forced 0x7fff pair */
#endif
    g_musicXTrack1Fade = MUSIC_FADESTATE_UNSET;
    musicTrack1Play(M_FOLDERS);
    maybe_is_in_menu = FALSE;
  }
}

void update_menu0C_missionfailed(void) {
    return;
}


void interface_menu0C_missionfailed(void)
{
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);
    tab_next_highlight = FALSE;
    tab_prev_highlight = FALSE;
    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = TRUE;
    }
    else
    {
        if (frontCheckCursorOnNextTab())
        {
            tab_next_highlight = TRUE;
        }
        else
        {
            tab_next_highlight = TRUE;
        }
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {
        if (tab_prev_highlight)
        {
            tab_prev_selected = TRUE;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        }
        else
        {
            if (tab_next_highlight)
            {
                tab_next_selected = TRUE;
                sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
            }
        }
    }
    else
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
        {
            tab_prev_selected = TRUE;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        }
    }
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();
    if (tab_next_selected)
    {
        frontChangeMenu(MENU_MISSION_COMPLETE, FALSE);
        return;
    }
    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MISSION_SELECT, FALSE);
        set_cursor_to_stage_solo(mission_folder_setup_entries[briefingpage].mission_num);
    }
}







s32 frontCompleteAllObjectivesAliveSuccess(void)
{
    s32 i;

    if (mission_failed_or_aborted || g_isBondKIA)
    {
        return 0;
    }

    for (i=0; i<10; i++)
    {
        if (ptrbriefingdata->objective[i].textid != 0
            && lvlGetSelectedDifficulty() >= ptrbriefingdata->objective[i].enabled_difficulty
            && get_status_of_objective(i) != OBJECTIVESTATUS_COMPLETE)
            {
                return 0;
            }
    }

    return 1;
}


Gfx * constructor_menu0C_missionfailed(Gfx *DL)
{
    s32 x;
    s32 y;

    char stagename[0xbb8];
    char *text;
    s32 phi_v1;

    s32 y2;
    s32 x2;


    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    #if defined(BUGFIX_R2)
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
    #endif
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);
    DL = print_current_solo_briefing_stage_name(DL, &stagename);

    text = langGet(getStringID(LTITLE, TITLE_STR_98_REPORT)); //REPORT:*
    x = 0x37;
    y = 0x8F;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    text = langGet(getStringID(LTITLE, TITLE_STR_99_MISSIONSTATUS)); //Mission status:*
    x2 = 0;
    y2 = 0;
    textMeasure(&y2, &x2, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = 0x37;
    y = 0xA7;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    if (g_isBondKIA)
    {
        text = langGet(getStringID(LTITLE, TITLE_STR_100_KIA)); // KILLED IN ACTION*
        phi_v1 = 0x78000000 | 0xFF;
    }
    else if (mission_failed_or_aborted)
    {
        text = langGet(getStringID(LTITLE, TITLE_STR_101_ABORTED)); // ABORTED*
        phi_v1 = 0x78000000 | 0xFF;
    }
    else if (frontCompleteAllObjectivesAliveSuccess())
    {

        text = langGet(getStringID(LTITLE, TITLE_STR_102_COMPLETED)); // Completed*
        phi_v1 = 0xFF;
    }
    else
    {
        text = langGet(getStringID(LTITLE, TITLE_STR_103_FAILED)); // FAILED*
        phi_v1 = 0x78000000 | 0xFF;
    }

    x = x2 + 0x37;
    y = 0xA7;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, phi_v1, viGetX(), viGetY(), 0, 0);

    DL = print_objectives_and_status_to_menu(DL, 0xBF, &stagename, 1);
    DL = frontAddNextTabText(DL);
    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}

//********************************************************************************************************
//MISSION COMPLETE
//********************************************************************************************************
void init_menu0D_missioncomplete(void)
{
    tab_next_selected = 0;
    tab_prev_selected = 0;
    load_walletbond();
    load_briefing_text_for_stage();
    if (g_NewCheatUnlocked)
    {
        sndPlaySfx(g_musicSfxBufferPtr, RARELOGO_SFX, 0);
    }
}

void update_menu0D_missioncomplete(void) {
    return;
}







void interface_menu0D_missioncomplete()
{
    int entry;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;

    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = 1;
    }
    else
    {
        if (frontCheckCursorOnNextTab())
        {
            tab_next_highlight = 1;
        }
        else
        {
            tab_next_highlight = 1;
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON|Z_TRIG|A_BUTTON))
    {
        if (tab_next_highlight)
        {
            tab_next_selected = 1;
            sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
        else if (tab_prev_highlight != FALSE)
        {
            tab_prev_selected = 1;
            sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
        }
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = 1;
        sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();

    if (tab_next_selected)
    {
#ifdef TARGET_N64
        if (frontCompleteAllObjectivesAliveSuccess() && (!g_AppendCheatSinglePlayer))
#else
        /* With cheats on the next dossier opens too (murkantor/Project-Midas#23); nothing is
         * saved for the run (end_of_mission_briefing). */
        if (frontCompleteAllObjectivesAliveSuccess())
#endif
        {
            if (mission_folder_setup_entries[briefingpage].mission_num == SP_LEVEL_CRADLE)
            {
                frontChangeMenu(MENU_RUN_STAGE, TRUE);
                selected_stage = LEVELID_CUBA;
            }
            else if (mission_folder_setup_entries[briefingpage].mission_num >= SP_LEVEL_AZTEC)
            {
                frontChangeMenu(MENU_MISSION_SELECT, FALSE);
                set_cursor_to_stage_solo((s32)mission_folder_setup_entries[briefingpage].mission_num);
            }
            else
            {
                entry = briefingpage + 1;
                while (mission_folder_setup_entries[entry].folder_text_preset)
                {
                    if (mission_folder_setup_entries[entry].stage_id >= 0)
                    {
                        break;
                    }

                    entry++;
                }

                if (mission_folder_setup_entries[entry].folder_text_preset
#ifndef TARGET_N64
                    /* with cheats, only a mission the folder would let you pick at this difficulty */
                    && (!g_AppendCheatSinglePlayer
                        || get_highest_unlocked_difficulty_for_level(mission_folder_setup_entries[entry].mission_num) >= selected_difficulty)
#endif
                    )
                {
                    briefingpage = entry;
                    selected_stage = mission_folder_setup_entries[entry].stage_id;
                }

                frontChangeMenu(MENU_BRIEFING, FALSE);
            }
        }
        else
        {
            frontChangeMenu(MENU_BRIEFING, FALSE);
        }
    }
    else if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MISSION_SELECT, FALSE);
        set_cursor_to_stage_solo((s32)mission_folder_setup_entries[briefingpage].mission_num);
    }

    return;
}

Gfx *constructor_menu0D_missioncomplete(Gfx *DL)
{
    s32 x;
    s32 y;
    u8 stagename[3000];
    char *text;
    s32 y2;
    s32 x2;
    s32 missiontimer;
    s32 killcount;
    s32 shotsfired;
    s32 headshots;
    s32 bodyshots;
    s32 limbshots;
    s32 reg4;
    s32 reg5;
    s32 reg6;
    s32 hitshots;
    s32 allhits;

    s32 missiontime;
    s32 temp;
    s32 besttime;
    s32 targettime;
    s32 difficulty;
    f32 hitPct;



    DL = viSetFillColor(DL, 0,0,0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    missiontimer = getMissiontimer();
    killcount = get_curplay_killcount();
    shotsfired = get_curplayer_shot_register(0);
    headshots = get_curplayer_shot_register(1);
    bodyshots = get_curplayer_shot_register(2);
    limbshots = get_curplayer_shot_register(3);
    reg4 = get_curplayer_shot_register(4);
    reg5 = get_curplayer_shot_register(5);
    reg6 = get_curplayer_shot_register(6);
    allhits = headshots + bodyshots + limbshots + reg4 + reg5;
    hitshots = allhits + reg6;
    if (allhits <= 0) {
        allhits = 1;
    }
    DL = print_current_solo_briefing_stage_name(DL, stagename);

    text = langGet(getStringID(LTITLE, TITLE_STR_104_STATS)); //STATISTICS:*
    x = 0x37;
    y = 0x8F;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    x2 = 0;
    y2 = 0;
    textMeasure(&y2, &x2, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

    text = langGet(getStringID(LTITLE, TITLE_STR_105_TIME)); //Time:*
    x = 0x37;
    y = 0xA7;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    besttime = fileGetSaveStageDifficultyTime( fileGetSaveForFoldernum(selected_folder_num), mission_folder_setup_entries[briefingpage].mission_num, selected_difficulty);
    if ((besttime == 0) || (!(besttime < 0x3FF))) {
        besttime = -1;
    }
    if (selected_difficulty >= DIFFICULTY_007) {
        difficulty = DIFFICULTY_00;
    }
    else {
        difficulty = selected_difficulty;
    }
    missiontime = missiontimer / 60;
    stagename[0] = '\0';
    targettime = solo_target_time_array[mission_folder_setup_entries[briefingpage].mission_num][difficulty];
#ifndef TARGET_N64
    {   /* mission_timer Extended Option ON: the debrief Time: gets the same
         * hundredths as the HUD timer. DISPLAY ONLY - fileUnlockStage...
         * still records whole seconds, the save format is untouched. */
        extern s32 portMissionTimer(void); /* video.c; weak 1 headless */

        if (portMissionTimer())
        {
            sprintf(stagename, "%02d:%02d:%02d", missiontime / 60, missiontime % 60, ((missiontimer % 60) * 100) / 60);
        }
        else
        {
            sprintf(stagename, "%02d:%02d", missiontime / 60, missiontime % 60);
        }
    }
#else
    sprintf(stagename, "%02d:%02d", missiontime / 60, missiontime % 60);
#endif
    x = 0x82;
    y = 0xA7;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

    if (g_NewCheatUnlocked) {
        stagename[0] = '\0';
        sprintf(stagename, "     [%s]", langGet(getStringID(LTITLE, TITLE_STR_275_NEWCHEATAVAILABLE))); //New Cheat Available
        DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xA00000FF, viGetX(), viGetY(), 0, 0);
    }


    if ((targettime > 0) && (selected_difficulty != DIFFICULTY_007)) {
        text = langGet(getStringID(LTITLE, TITLE_STR_274_TARGET)); //Target:
        x = 0x37;
        y = y2 + 0xA9;
        DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        stagename[0] = '\0';
        sprintf(stagename, "%02d:%02d", targettime / 60, targettime % 60);
        x = 0x82;
        y = y2 + 0xA9;
        DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        if (besttime >= 0) {
            if (besttime < 0x3FF) {
                stagename[0] = '\0';
                if (besttime >= 0) {
                    sprintf(stagename, "     (%s  %02d:%02d)", langGet(getStringID(LTITLE, TITLE_STR_273_BESTTIME)), besttime / 60, besttime % 60); //Best Time:
                }
                else {
                    sprintf(stagename, "");
                }
                DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
            }
        }
    }
    else {
        if (besttime >= 0) {
            if (besttime < 0x3FF) {
                text = langGet(getStringID(LTITLE, TITLE_STR_273_BESTTIME)); //Best Time:
                x = 0x37;
                y = y2 + 0xA9;
                DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
                stagename[0] = '\0';
                sprintf(stagename, "%02d:%02d", besttime / 60, besttime % 60);
                x = 0x82;
                y = y2 + 0xA9;
                DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
            }
        }
    }


    text = langGet(getStringID(LTITLE, TITLE_STR_106_ACCURACY)); //Accuracy:*
    x = 0x37;
    y = 0xCC;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    if (shotsfired > 0) {
        hitPct = (hitshots * 100.0f) / shotsfired;
    }
    else {
        hitPct = 0.0f;
    }
    sprintf(stagename, "%.1f%%",  hitPct);
    x = 0x82;
    y = 0xCC;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    text = langGet(getStringID(LTITLE, TITLE_STR_107_WEAPONOFCHOICE)); //Weapon of choice:*
    x = 0x37;
    y = 0xDC;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    strcpy(stagename, frontGetPlayersFavoriteWeaponInHand(0, 0));
    if ((array_favweapon[0][0] > 0) && (array_favweapon[0][1] == array_favweapon[0][0]))
    {
        temp = strlen(stagename)-1;
        sprintf(&stagename[temp], " x 2\n");
    }
    x = 0xBE;
    y = 0xDC;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    text = langGet(getStringID(LTITLE, TITLE_STR_108_SHOTTOTAL)); //Shot total:*
    x = 0x37;
    y = 0xF4;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    sprintf(stagename, "%d", shotsfired);
    x = 0x82;
    y = 0xF4;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    text = langGet(getStringID(LTITLE, TITLE_STR_109_HEADHITS)); //Head hits:*
    x = 0xB4;
    y = 0xF4;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    sprintf(stagename, "%d (%d%%)", headshots, (s32)(floorFloat(((headshots * 100.0f) / allhits) + 0.5f)));
    x = 0x12C;
    y = 0xF4;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    text = langGet(getStringID(LTITLE, TITLE_STR_110_BODYHITS)); //Body hits:*
    x = 0xB4;
    y = y2 + 0xF4;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    sprintf(stagename, "%d (%d%%)", bodyshots, (s32)floorFloat(((bodyshots * 100.0f) / allhits) + 0.5f));
    x = 0x12C;
    y = y2 + 0xF4;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    text = langGet(getStringID(LTITLE, TITLE_STR_111_LIMBHITS)); //Limb hits:*
    x = 0xB4;
    y = (y2 * 2) + 0xF4;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    sprintf(stagename, "%d (%d%%)", limbshots, (s32)floorFloat(((limbshots * 100.0f) / allhits) + 0.5f));
    x = 0x12C;
    y = (y2 * 2) + 0xF4;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    text = langGet(getStringID(LTITLE, TITLE_STR_112_OTHER)); //Others:*
    x = 0xB4;
    y = (y2 * 3) + 0xF4;
    temp = reg5 + reg4;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    sprintf(stagename, "%d (%d%%)", temp, (s32)floorFloat(((temp * 100.0f) / allhits) + 0.5f));
    x = 0x12C;
    y = (y2 * 3) + 0xF4;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    text = langGet(getStringID(LTITLE, TITLE_STR_113_KILLTOTAL)); //Kill total:*
    x = 0x37;
    y = y2 + 0xF4;
    DL = frontPrintText(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    sprintf(stagename, "%d", killcount);
    x = 0x82;
    y = y2 + 0xF4;
    DL = frontPrintText(DL, &x, &y, stagename, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);


    DL = frontAddNextTabText(DL);
    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);

    return DL;
}



//********************************************************************************************************
//CHEAT SCREEN
//********************************************************************************************************

#ifndef TARGET_N64
/* The Cheats folder's top tab flips to a second, always-available PORT list
 * (Line Mode, Sniper/Rocket, Enemy FF, Unlock Doors). They are ordinary
 * g_CheatActivated cheats that frontCheckIfCheatIsUnlocked never reports as
 * earned, so they stay off Rare's list and a mission run with one on goes
 * unrecorded. */
static const s32 port_cheat_ids[] = { CHEAT_LINEMODE, CHEAT_PORT_SNIPER_ROCKET, CHEAT_PORT_ENEMY_FF, CHEAT_PORT_UNLOCK_DOORS };
static const char *const port_cheat_names[] = { NULL /* Rare's string */, "Sniper/Rocket\n", "Enemy FF\n", "Unlock Doors\n" };
#define PORT_CHEAT_COUNT ((s32)(sizeof(port_cheat_ids) / sizeof(port_cheat_ids[0])))
static s32 port_cheat_tab;   /* the sheet shows the PORT list */

static s32 portCheatOnPortTab(s32 cheat)
{
    s32 k;

    for (k = 0; k < PORT_CHEAT_COUNT; k++)
    {
        if (port_cheat_ids[k] == cheat)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static char *portCheatName(s32 k)
{
    return (port_cheat_names[k] != NULL) ? (char *) port_cheat_names[k]
                                         : (char *) cheatGetMenuTextPointer(port_cheat_ids[k]);
}
#endif

void init_menu15_cheat(void)
{
    int i;

#ifndef TARGET_N64
    port_cheat_tab = FALSE;
#endif

    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    totalunlockedcheats = 0x0;

    for (i = 1; i != CHEAT_INVALID; i++) {
        if (cheat_available[i]) {
          arrayUnlockedCheats[totalunlockedcheats] = i;
          totalunlockedcheats++;
        }
    };
    load_walletbond();
}


void update_menu15_cheat(void)
{
  int i;
  u32 appendsp;
  u32 appendmp;

  appendsp = FALSE;
  appendmp = FALSE;

  for (i = 1; i != CHEAT_INVALID; i++) {
    if (g_CheatActivated[i]) {
        appendsp = TRUE;
        if (cheatCheckIfMPCheat(i)
#ifndef TARGET_N64
            /* these six reach MP from the Multiplayer Options sheet,
             * never from this menu (lv.c), so a pick here is no MP cheat */
            && i != CHEAT_INVISIBILITY && i != CHEAT_DK_MODE && i != CHEAT_TINY_BOND
            && i != CHEAT_PAINTBALL && i != CHEAT_NO_RADAR_MP && i != CHEAT_TURBO_MODE
#endif
            ) {
            appendmp = TRUE;
        }
    }
  };

  g_AppendCheatSinglePlayer = appendsp;
  g_AppendCheatMultiPlayer = appendmp;
}


// Address 0x7F017C88 NTSC
void interface_menu15_cheat(void)
{
    s32 i;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    MP_menu_selected_option = 0;

    if (joyGetButtons(PLAYER_1, A_BUTTON|Z_TRIG) == 0)
    {
        tab_prev_highlight = 0;
        tab_next_highlight = 0;
        tab_start_highlight = 0;
        cheathighlighted = 0;

        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = 1;
        }
#ifndef TARGET_N64
        else if (frontCheckCursorOnStartTab())
        {
            tab_start_highlight = 1;
            cheathighlighted = -1;   /* no row lit under the tab (Rare's draw tests only PREVIOUS) */
        }
        else if (port_cheat_tab)
        {   /* one column, Rare's pitch; above the first row is the first */
            for (i = PORT_CHEAT_COUNT - 1; i >= 0; i--)
            {
                if ((s32) cursor_v_pos >= (i * 0x14) + 0x35)
                {
                    cheathighlighted = i;
                    break;
                }
            }
        }
#endif
        else
        {
            if (totalunlockedcheats >= 0xD)
            {
                i = totalunlockedcheats - 0xD;
                if ((s32) cursor_h_pos >= 0xDC)
                {
                    for (; i >= 0; i--)
                    {
                        if ((s32) cursor_v_pos >= (i * 0x14) + 0x35)
                        {
                            cheathighlighted = i + 0xC;
                            break;
                        }
                    }
                }
            }

            if (cheathighlighted == 0)
            {
                i = (totalunlockedcheats >= 0xD)
                    ? 0xB
                    : totalunlockedcheats - 1;

                for (; i >= 0; i--)
                {
                    if ((s32) cursor_v_pos >= (i * 0x14) + 0x35)
                    {
                        cheathighlighted = i;
                        break;
                    }
                }
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON|Z_TRIG))
    {
        if (tab_prev_highlight != 0)
        {
            tab_prev_selected = 1;
        }
#ifndef TARGET_N64
        else if (tab_start_highlight != 0)
        {
            port_cheat_tab = !port_cheat_tab;
            cheathighlighted = 0;
        }
#endif
        else
        {
            MP_menu_selected_option = 1;
        }

        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }
    else if (joyGetButtonsPressedThisFrame(0, 0x4000U) != 0)
    {
        tab_prev_selected = 1;
        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();

#ifndef TARGET_N64
    if (tab_prev_selected && port_cheat_tab)
    {   /* backing out of the PORT list returns to Rare's list */
        port_cheat_tab = FALSE;
        tab_prev_selected = FALSE;
        cheathighlighted = 0;
        return;
    }
#endif

    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MODE_SELECT, 0);
        setCursorPOSforMode(gamemode);
        return;
    }

    if (MP_menu_selected_option)
    {
#ifndef TARGET_N64
    if (port_cheat_tab)
    {
        i = port_cheat_ids[cheathighlighted];
        g_CheatActivated[i] = 1 - g_CheatActivated[i];
        return;
    }
#endif
    i = arrayUnlockedCheats[cheathighlighted];
        g_CheatActivated[i] = 1 - g_CheatActivated[i];
    }
}



// Address 0x7F017F5C NTSC
// Address 0x7F017EB8 PAL
Gfx * constructor_menu15_cheat(Gfx *DL)
{
    s32 padding;
    s32 strID;
    s32 var_fp;
    s32 sp88;
    s32 sp84;
    s32 sp80;
    s32 sp7C;
    u8 *temp_v0;
    s32 var_s5;

    DL = viSetFillColor(DL,0,0,0);
    DL = viFillScreen(DL);
#ifdef VERSION_EU
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
#endif
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

#ifndef TARGET_N64
    if (port_cheat_tab)
    {   /* Rare's first column, as drawn below */
        for (var_fp = 0; var_fp < PORT_CHEAT_COUNT; var_fp++)
        {
            const s32 on = g_CheatActivated[port_cheat_ids[var_fp]];

            temp_v0 = (u8 *) portCheatName(var_fp);
            textMeasure(&sp80, &sp7C, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
            sp88 = 0x37;
            sp84 = (var_fp * 0x14) + 0x35;
            if ((var_fp == cheathighlighted) && !frontCheckCursorOnPreviousTab() && !frontCheckCursorOnStartTab())
            {
                DL = microcode_constructor_related_to_menus(DL, sp88 - 2, sp84 - 1, sp88 + sp7C + 5, sp84 + 0xE, 0x32);
            }
            DL = frontPrintText(DL, &sp88, &sp84, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

            temp_v0 = langGet(on ? getStringID(LTITLE, TITLE_STR_115_ON) : getStringID(LTITLE, TITLE_STR_116_OFF));
            sp88 = 0xB3;
            sp84 = (var_fp * 0x14) + 0x35;
            DL = frontPrintText(DL, &sp88, &sp84, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, on ? 0xA00000FF : 0xFF, viGetX(), viGetY(), 0, 0);
        }
        DL = portAddTopTabText(DL, "CHEATS\n");
        DL = frontAddPreviousTabText(DL);
        DL = frontDrawCursor(DL);
        return DL;
    }
#endif

    for (var_fp = 0; var_fp < ((totalunlockedcheats >= 0xD) ? 0xC : totalunlockedcheats); var_fp++)
    {
        temp_v0 = cheatGetMenuTextPointer(arrayUnlockedCheats[var_fp]);
        textMeasure(&sp80, &sp7C, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        sp88 = 0x37;
        sp84 = (var_fp * 0x14) + 0x35;
        if ((var_fp == cheathighlighted) && (frontCheckCursorOnPreviousTab() == 0))
        {
            DL = microcode_constructor_related_to_menus(DL, sp88 - 2, sp84 - 1, sp88 + sp7C + 5, sp84 + 0xE, 0x32);
        }

        DL = frontPrintText(DL, &sp88, &sp84, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

        strID  = (g_CheatActivated[arrayUnlockedCheats[var_fp]]) ? getStringID(LTITLE, TITLE_STR_115_ON) : getStringID(LTITLE, TITLE_STR_116_OFF);
        temp_v0 = langGet(strID);
        textMeasure(&sp80, &sp7C, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

        sp88 = 0xB3;
        sp84 = (var_fp * 0x14) + 0x35;

        DL = frontPrintText(DL, &sp88, &sp84, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, (g_CheatActivated[arrayUnlockedCheats[var_fp]]) ? 0xA00000FF : 0xFF, viGetX(), viGetY(), 0, 0);
    }

    if (totalunlockedcheats >= 0xd)
    {
        for (var_fp = 0; var_fp < (totalunlockedcheats - 0xC); var_fp++)
        {
            temp_v0 = cheatGetMenuTextPointer(arrayUnlockedCheats[var_fp + 12]);
            textMeasure(&sp80, &sp7C, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

            sp88 = 0xDC;
            sp84 = (var_fp * 0x14) + 0x35;
            if ((var_fp + 0xc == cheathighlighted) && (frontCheckCursorOnPreviousTab() == 0))
            {
                DL = microcode_constructor_related_to_menus(DL, sp88 - 2, sp84 - 1, sp88 + sp7C + 5, sp84 + 0xE, 0x32);
            }

            DL = frontPrintText(DL, &sp88, &sp84, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);

            strID  = (g_CheatActivated[arrayUnlockedCheats[var_fp + 12]]) ? getStringID(LTITLE, TITLE_STR_115_ON) : getStringID(LTITLE, TITLE_STR_116_OFF);
            temp_v0 = langGet(strID);
            textMeasure(&sp80, &sp7C, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

            sp88 = 0x158;
            sp84 = (var_fp * 0x14) + 0x35;
            strID = (g_CheatActivated[arrayUnlockedCheats[var_fp + 12]]) ? 0xA00000FF : 0xFF;

            DL = frontPrintText(DL, &sp88, &sp84, temp_v0, ptrFontZurichBoldChars, ptrFontZurichBold, strID, viGetX(), viGetY(), 0, 0);
        }
    }

#ifndef TARGET_N64
    DL = portAddTopTabText(DL, "PORT\n");
#endif
    DL = frontAddPreviousTabText(DL);

    DL = frontDrawCursor(DL);

    return DL;
}


//********************************************************************************************************
//NO CONTROLLER SCREEN
//********************************************************************************************************
void init_menu16_nocontroller(void) {
    return;
}


void update_menu16_nocontrollers(void) {
    return;
}


void interface_menu16_nocontrollers(void) {
    return;
}


Gfx *constructor_menu16_nocontrollers(Gfx *DL)
{
    s32 y2;
    s32 x2;
    s32 x;
    s32 y;
    u8 *text;
    s32 conConnected = joyGetConnectedControllers();
    s32 numContCon = 0;

    if ((conConnected & 8)) {
        numContCon++;
    }
    if ((conConnected & 4)) {
        numContCon++;
    }
    if ((conConnected & 2)) {
        numContCon++;
    }
    DL = microcode_constructor(clear_framebuffer_black(DL));


    if ((numContCon == 0) || (numContCon == 1) || (numContCon == 2) || (numContCon == 3)) {
        text = langGet(getStringID(LTITLE, TITLE_STR_118_NOCONT)); //NO CONTROLLER IN CONTROLLER SOCKET 1
    }
    textMeasure(&y2, &x2, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = 0xDC - (x2 >> 1);
    y = 0x99 - (y2 >> 1);
#ifdef BUGFIX_R1
    if (j_text_trigger) {
        DL = textRenderOutlined(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, 0x8000FF, viGetX(), viGetY(), 0, 0);
    }
    else {
#endif
        DL = textRender(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
#ifdef BUGFIX_R1
    }
#endif


    if ((numContCon == 0) || (numContCon == 1) || (numContCon == 2) || (numContCon == 3)) {
        text = langGet(getStringID(LTITLE, TITLE_STR_119_ATTACHCONT)); //PLEASE POWER OFF AND ATTACH A CONTROLLER
    }
    textMeasure(&y2, &x2, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = 0xDC - (x2 >> 1);
    y = 0xB1 - (y2 >> 1);
#ifdef BUGFIX_R1
    if (j_text_trigger) {
        DL = textRenderOutlined(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, 0x8000FF, viGetX(), viGetY(), 0, 0);
    }
    else {
#endif
        DL = textRender(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
#ifdef BUGFIX_R1
    }
#endif

    return DL;
}


void do_extended_cast_display(bool doExtended)
{
    full_actor_intro = doExtended;
    if (doExtended)
    {
        intro_character_index = 0;
        return;
    }
    intro_character_index = 1;
}


//********************************************************************************************************
//CAST SCREEN
//********************************************************************************************************
void init_menu18_displaycast(void)
{
    u8 *bufferPtr;
    s32 bufferRemaining;
    s32 allocSize;
    struct coord3d subOffset;
    struct texpool texPool;
    enum BODIES body;
    enum HEADS head;
    s32 weaponProp;
    ModelFileHeader *bodyHeader;
    ModelFileHeader *headHeader;
    ModelFileHeader *weaponHeader;
    s32 padding;
    bool flip;
    s32 cameraPreset;
    struct intro_random_rifles rifles;
    struct intro_random_pistols pistols;
    
    bufferPtr = ptr_logo_and_walletbond_DL;
    subOffset = zeroCoord;
    headHeader = NULL;
    flip = randomGetNext() & 1;

    if ((full_actor_intro != 0) && (intro_character_index == 0))
    {
#ifdef TARGET_N64
        musicTrack1ApplySeqpVol(VOLUME_MAX);
#else
        /* GLOBAL VOLUMES: vanilla played the intro at full volume whatever
         * the music setting; apply it here too. */
        musicTrack1ApplySeqpVol((u16) portMusicVolumeRawSafe());
#endif
        g_musicXTrack1Fade = 0;
        musicTrack1Play(M_INTRO);
    }

    for (intro_animation_count = 0; intro_animation_table[intro_animation_count].animID >= 0; intro_animation_count++)
    {
        // empty
    }

    randomly_selected_intro_animation = randomGetNext() % ((u32) intro_animation_count);
    body = intro_char_table[intro_character_index].body;
    head = intro_char_table[intro_character_index].head;
    cameraPreset = intro_animation_table[randomly_selected_intro_animation].camera_preset;
    
    if (body == BODY_Special_Operations_Uniform)
    {
        // Keep the multiple assignments on the same line for matching.
        switch (randomGetNext() % 5U) 
        { 
            case 0: 
                break; 
            case 1: 
                body = BODY_Formal_Wear; head = HEAD_Male_Brosnan_Default; 
                break; 
            case 2: 
                body = BODY_Jungle_Fatigues; head = HEAD_Male_Brosnan_Default; 
                break; 
            case 3: 
                body = BODY_Parka; head = HEAD_Male_Brosnan_Default;
                break; 
            case 4: 
                body = BODY_Brosnan_Tuxedo; head = HEAD_Male_Brosnan_Tuxedo; 
            break; 
        } 
    } 
    else if (body == BODY_Natalya_Skirt) 
    { 
        if (randomGetNext() & 1) 
        { 
            body = BODY_Natalya_Jungle_Fatigues; 
        } 
    } 
    else if ((body == BODY_Trevelyan_006) && (randomGetNext() & 1))
    { 
        body = BODY_Trevelyan_Janus; 
    } 

    if (head == HEAD_RANDOM) 
    { 
        head = get_random_head(body); 
    } 

    allocSize = 0x19000; 
    bufferRemaining = 0x18160; 

    if (1);

    bufferRemaining = bufferRemaining + allocSize; 

    zbufSetBuffer(ALIGN64_V2(bufferPtr + bufferRemaining), Z_BUFFER_4_3_WIDTH, Z_BUFFER_4_3_HEIGHT);
    texInitPool(&texPool, bufferPtr, allocSize);

    bufferRemaining -= allocSize;
    bufferPtr += allocSize;
    bodyHeader = c_item_entries[body].header;

    load_object_fill_header(c_item_entries[body].header, c_item_entries[body].filename, bufferPtr, bufferRemaining, &texPool);
    allocSize = ALIGN64_V2(get_pc_buffer_remaining_value(c_item_entries[body].filename));
    bufferRemaining -= allocSize;
    bufferPtr += allocSize;

    if (head >= 0)
    {
        headHeader = c_item_entries[head].header;
        load_object_fill_header(c_item_entries[head].header, c_item_entries[head].filename, bufferPtr, bufferRemaining, &texPool);
        allocSize = ALIGN64_V2(get_pc_buffer_remaining_value(c_item_entries[head].filename));
        bufferRemaining -= allocSize;
        bufferPtr += allocSize;
    }

    cast_model = setup_chr_instance(body, head, bodyHeader, headHeader, 0);
    modelSetScale((Model *) cast_model, 0.1f);

    if (cameraPreset != INTRO_WEAPON_TYPE_NONE)
    {
        if (cameraPreset == INTRO_WEAPON_TYPE_RIFLE)
        {
            rifles = random_rifles_in_intro;
            weaponProp = rifles.props[randomGetNext() % 6];
        }
        else
        {
            pistols = random_pistols_in_intro;
            weaponProp = pistols.props[randomGetNext() % 10];
        }

        // Replace Cougar Magnum with PP7 if Cradle has not been completed on any folder.
        if (weaponProp == PROP_CHRRUGER)
        {
            if (fileIsCradleCompletedAnyFolder() == FALSE)
            {
                weaponProp = PROP_CHRWPPK;
            }
        }

        // Replace Laser with PP7 if Aztec has not been completed on Secret Agent or 00 Agent on any folder.
        if (weaponProp == PROP_CHRLASER)
        {
            if (check_aztec_completed_any_folder_secret_00() == FALSE)
            {
                weaponProp = PROP_CHRWPPK;
            }
        }

        // Replace Golden Gun with PP7 if Egyptian has not been completed on 00 Agent on any folder.
        if (weaponProp == PROP_CHRGOLDEN)
        {
            if (fileIsEgyptCompletedOn00AnyFolder() == FALSE)
            {
                weaponProp = PROP_CHRWPPK;
            }
        }

        weaponHeader = PitemZ_entries[weaponProp].header;
        load_object_fill_header(weaponHeader, PitemZ_entries[weaponProp].filename, bufferPtr, bufferRemaining, &texPool);
        allocSize = ALIGN64_V2(get_pc_buffer_remaining_value(PitemZ_entries[weaponProp].filename));
        bufferRemaining -= allocSize;
        bufferPtr += allocSize;

        modelCalculateRwDataLen(weaponHeader);
        cast_model_weapon = modelmgrInstantiateModel(weaponHeader);
        modelSetScale(cast_model_weapon, 0.1f);
        cast_model_weapon->attachedto = cast_model;
        cast_model_weapon->attachedto_objinst = cast_model->obj->Switches[(flip != FALSE) ? (5) : (3)];
    }
    else
    {
        cast_model_weapon = NULL;
    }

    modelSetAnimTranslationScale(cast_model, 0.1f);
    setsuboffset(cast_model, &subOffset);
    setsubroty(cast_model, 0.0f);

#if defined VERSION_EU
    modelSetAnimPlaySpeed(cast_model, 0.6f, 0);
#else
    modelSetAnimPlaySpeed(cast_model, 0.5f, 0);
#endif
    modelSetAnimation(cast_model, animation_table_ptrs1[intro_animation_table[randomly_selected_intro_animation].animID], flip, intro_animation_table[randomly_selected_intro_animation].startframeoffset, intro_animation_table[randomly_selected_intro_animation].playback_speed, 0.0f);

    g_MenuTimer = 0;
    cast_camera_dist_start = ((((f32) ((u32) randomGetNext())) * (1.0f / U32_MAX)) * 80.0f) + 70.0f;
    cast_camera_dist_end = ((((f32) ((u32) randomGetNext())) * (1.0f / U32_MAX)) * 80.0f) + 70.0f;
    cast_camera_angle_start = ((((f32) ((u32) randomGetNext())) * (1.0f / U32_MAX)) - 0.5f) * M_TAU_F;
    cast_camera_angle_end = ((((f32) ((u32) randomGetNext())) * (1.0f / U32_MAX)) - 0.5f) * 2.5132742f;
    cast_camera_height_start = ((((f32) ((u32) randomGetNext())) * (1.0f / U32_MAX)) * 200.0f) + (-100.0f);
    cast_camera_height_end = ((((f32) ((u32) randomGetNext())) * (1.0f / U32_MAX)) * 200.0f) + (-100.0f);
    cast_camera_dist = cast_camera_dist_start;
    cast_camera_angle = cast_camera_angle_start;
    cast_camera_height = cast_camera_height_start;
    cast_camera_reset = TRUE;
    cast_rootpos_smoothed.f[0] = 0.0f;
    cast_rootpos_smoothed.f[1] = 0.0f;
    cast_rootpos_smoothed.f[2] = 0.0f;
}


void update_menu18_displaycast(void) {
    if (cast_model != 0) {
        clear_aircraft_model_obj(cast_model);
    }
    if (cast_model_weapon != 0) {
        clear_model_obj(cast_model_weapon);
        return;
    }
}


// Address 0x7F018F60 NTSC

#ifndef TARGET_N64
s32 g_portCreditsSkipParade; /* bondview2.c: START during the credits - the stage end skips the parade too */

/**
 * Where the extended (post-credits) parade goes when it is over - its last
 * character, START during it, or START during the credits before it
 * (g_portCreditsSkipParade). One copy so the three cannot drift.
 */
static void portCastParadeExit(void)
{
    printf("ge: credits: cast parade over (or skipped) -> %s\n",
           port_credits_roll ? "the CREDITS sheet" : "the mission list");
    g_portCreditsSkipParade = FALSE;
    if (port_credits_roll)
    {   /* the "1997" tab, from the CREDITS sheet: back to the sheet. Rare's arm
         * below lands on the mission list with the cursor on Cradle, which
         * is right for somebody who just FINISHED Cradle and nonsense for
         * somebody who has not picked a folder. */
        port_credits_roll = FALSE;
        full_actor_intro = 0;
        port_sheet_kind = PORT_SHEET_CREDITS;
        if (maybe_is_in_menu)
        {   /* the parade's intro theme off, the folder music back, as Rare's
             * arm gets from init_menu07 */
            musicTrack1Play(M_FOLDERS);
            maybe_is_in_menu = FALSE;
        }
        frontChangeMenu(MENU_PORT_SHEET, 1);
        return;
    }
    frontChangeMenu(MENU_MISSION_SELECT, 1);
    set_cursor_to_stage_solo(SP_LEVEL_CRADLE);
    full_actor_intro = 0;
}
#endif

void interface_menu18_displaycast(void)
{
    s32 f;

#ifndef TARGET_N64
    if (full_actor_intro && g_portCreditsSkipParade)
    {   /* START during the credits (bondview2.c): the parade goes too. Tested
         * HERE, not in MENU_RUN_STAGE - that handler runs when the credits
         * stage LAUNCHES and queues the parade then, before START can be
         * pressed. */
        portCastParadeExit();
        return;
    }
#endif

    // Unlike all other menu functions, this one uses a FOV of 46.0 degrees instead of 60.0 degrees.
    viSetFovY(46.0f);
    viSetZRange(10.0f, 2000.0f);
    viSetUseZBuf(TRUE);
#ifdef TARGET_N64
    viSetAspect(ASPECT_RATIO_SD);
#else
    { /* WIDESCREEN: the cast parade renders wide like the mission tick
       * (bondview.c), widening only the projection aspect and declaring the
       * frame wide, while the name texrects stay pinned to the 4:3 band (the
       * flag self-expires, so surrounding menus stay 4:3). */
        extern f32 portWideFactor(void);       /* windowAspect/(4:3), video.c; weak 1.0 headless */
        extern void portSetSceneWide(s32 wide);
        f32 wf = portWideFactor();

        viSetAspect(ASPECT_RATIO_SD * wf);
        portSetSceneWide(wf > 1.001f);
    }
#endif
    set_cur_player_screen_size(440, 330);
    viSetViewSize(440, 330);
    set_cur_player_viewport_size(0, 0);
    viSetViewPosition(0, 0);

    g_MenuTimer += g_ClockTimer;

#if defined(REFRESH_PAL)
#define INTERFACE_MENU18_TIMER 151
#else
#define INTERFACE_MENU18_TIMER 181
#endif

    if (g_MenuTimer >= INTERFACE_MENU18_TIMER)
    {
#undef INTERFACE_MENU18_TIMER
        intro_character_index++;

        while (1)
        {
            if (intro_char_table[intro_character_index].flag && full_actor_intro == 0 && 1)
            {
                intro_character_index++;
                continue;
            }

            if ((intro_char_table[intro_character_index].body == BODY_Moonraker_Elite_1_Male) && (check_aztec_completed_any_folder_secret_00()) == 0)
            {
                intro_character_index++;
                continue;
            }

            if ((intro_char_table[intro_character_index].body == BODY_Moonraker_Elite_2_Female) && (check_aztec_completed_any_folder_secret_00() == 0))
            {
                intro_character_index++;
                continue;
            }

            if ((intro_char_table[intro_character_index].body == BODY_Mayday) && (check_aztec_completed_any_folder_secret_00() == 0 && (randomGetNext() % 0x2710U)))
            {
                intro_character_index++;
                continue;
            }

            if ((intro_char_table[intro_character_index].body == BODY_Jaws) && (check_aztec_completed_any_folder_secret_00() == 0 && (randomGetNext() % 0x2710U)))
            {
                intro_character_index++;
                continue;
            }

            if ((intro_char_table[intro_character_index].body == BODY_Oddjob) && (fileIsEgyptCompletedOn00AnyFolder() == 0 && (randomGetNext() % 0x2710U)))
            {
                intro_character_index++;
                continue;
            }

            if ((intro_char_table[intro_character_index].body == BODY_Baron_Samedi) && (fileIsEgyptCompletedOn00AnyFolder() == 0 && (randomGetNext() % 0x2710U) ))
            {
                intro_character_index++;
                continue;
            }

            break;
        }

        f = intro_character_index;

        if (intro_char_table[f].body < 0)
        {
            intro_character_index = 0;
            f = 0;
        }

        if (intro_character_index > 0)
        {
            frontChangeMenu(MENU_DISPLAY_CAST, 1);
        }
        else if (full_actor_intro != 0)
        {
#ifdef TARGET_N64
            frontChangeMenu(MENU_MISSION_SELECT, 1);
            set_cursor_to_stage_solo(SP_LEVEL_CRADLE);
            full_actor_intro = 0;
#else
            portCastParadeExit();
#endif
        }
        else
        {
            select_ramrom_to_play();
        }
    }

    else if ((joyGetButtonsPressedThisFrame(PLAYER_1, 0xFFFF) != 0) && (!full_actor_intro))
    {
        frontChangeMenu(MENU_FILE_SELECT, TRUE);
    }
#ifndef TARGET_N64
    else if (full_actor_intro && joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON) != 0)
    {   /* START skips the post-credits parade - straight to
         * where its last character would have taken us. The attract parade
         * above already skips on any button. */
        portCastParadeExit();
    }
#endif
}


Gfx *constructor_menu18_displaycast(Gfx *DL)
{
    coord3d campos = cast_camera_pos;
    coord3d tarpos = cast_camera_target;
    coord3d up = cast_camera_up;
    Light *light;
    Mtxf mtx;
    ModelRenderData renderdata = cast_renderdata_default;
    s32 pad1A4;
    s32 pad1A0;
    Mtxf mtx2;
    s32 x;
    s32 y;
    s32 textwidth;
    s32 textheight;
    s32 i;
    f32 menufrac = (f32)g_MenuTimer / CAST_INTRO_LEN_F;
    f32 fade;
    char *text;
    coord3d vec = cast_camera_offset;
    coord3d suboffset;
    s32 pad120;
    s32 pad124;
    Mtx lookatmtx;
    s32 padDC;
 
    if (g_MenuTimer < 0 || g_MenuTimer >= CAST_INTRO_LEN)
    {
        fade = 0.0f;
    }
    else if (g_MenuTimer < CAST_INTRO_FADE)
    {
        fade = (f32)g_MenuTimer / CAST_INTRO_FADE_F;
    }
    else if (g_MenuTimer >= CAST_INTRO_FADEOUT)
    {
        fade = (f32)(CAST_INTRO_LEN - g_MenuTimer) / CAST_INTRO_FADE_F;
    }
    else
    {
        fade = 1.0f;
    }
 
    DL = viFillScreen(viSetFillColor(DL, 0, 0, 0));
 
    cast_camera_dist = (cast_camera_dist_end - cast_camera_dist_start) * menufrac + cast_camera_dist_start;
    cast_camera_angle = (cast_camera_angle_end - cast_camera_angle_start) * menufrac + cast_camera_angle_start;
    cast_camera_height = (cast_camera_height_end - cast_camera_height_start) * menufrac + cast_camera_height_start;
 
    if (cast_camera_angle < 0.0f)
    {
        cast_camera_angle += M_TAU_F;
    }
 
    campos.x = cast_camera_dist * sinf(cast_camera_angle) + cosf(cast_camera_angle) * 0.2f * cast_camera_dist;
    campos.y = cast_camera_height;
    campos.z = cast_camera_dist * cosf(cast_camera_angle) - sinf(cast_camera_angle) * 0.2f * cast_camera_dist;
 
    tarpos.x += cosf(cast_camera_angle) * 0.2f * cast_camera_dist;
    tarpos.z += -sinf(cast_camera_angle) * 0.2f * cast_camera_dist;
 
    light = dynAllocateLights(2);
 
    guLookAtReflect(&lookatmtx, (LookAt *)light, 0.0f, 0.0f, 4000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
 
    gSPNumLights(DL++, 1); \
    gSPLight(DL++, &gelogolight.l[0], 1); \
    gSPLight(DL++, &gelogolight, 2);
 
    gSPLookAt(DL++, light);
 
    modelTickAnim(cast_model, g_ClockTimer, 1);
    modelSetDistanceDisabled(1);
    sub_GAME_7F073FC8(0);
    subcalcpos(cast_model);
 
    if (cast_model_weapon != NULL && cast_model_weapon->obj->Switches[0] != NULL)
    {
        modelGetNodeRwData( cast_model_weapon, cast_model_weapon->obj->Switches[0])->Gunfire.visible = 0;
    }
 
    if (cast_model_weapon != NULL && cast_model_weapon->obj->Switches[2] != NULL)
    {
        modelGetNodeRwData(cast_model_weapon, cast_model_weapon->obj->Switches[2])->Switch.visible = 0;
    }
 
    renderdata.mtxlist = (Mtxf *)dynAllocate(cast_model->obj->numMatrices << 6);
 
    matrix_4x4_set_identity(&mtx);
 
    renderdata.basemtx = &mtx;
 
    subcalcmatrices(&renderdata, cast_model);
    getsuboffset(cast_model, &suboffset);
 
    if (cast_camera_reset)
    {
        cast_rootpos_smoothed.y = suboffset.y;
    }
 
#ifndef TARGET_N64
    /* GE_TICKHZ: suboffset comes from modelTickAnim(cast_model, g_ClockTimer,
     * 1), so divide by that integer field count, not the fractional tick delta,
     * or the cast parade walks at double speed at tickhz=120 (skipped on a
     * 0-field sub-tick, which has no new motion). */
    {
        const f32 castdiv = (g_ClockTimer > 0) ? (f32) g_ClockTimer : 1.0f;

        vec.f[0] = (suboffset.x - cast_rootpos_smoothed.x) / castdiv;
        vec.f[1] = (suboffset.y - cast_rootpos_smoothed.y) / castdiv;
        vec.f[2] = (suboffset.z - cast_rootpos_smoothed.z) / castdiv;
    }
#else
    vec.f[0] = (suboffset.x - cast_rootpos_smoothed.x) / g_GlobalTimerDelta;
    vec.f[1] = (suboffset.y - cast_rootpos_smoothed.y) / g_GlobalTimerDelta;
    vec.f[2] = (suboffset.z - cast_rootpos_smoothed.z) / g_GlobalTimerDelta;
#endif
 
    if (cast_camera_reset)
    {
        cast_rootvel_accumulator.x = vec.f[0] / CAST_DAMP_COMP;
        cast_rootvel_accumulator.y = vec.f[1] / CAST_DAMP_COMP;
        cast_rootvel_accumulator.z = vec.f[2] / CAST_DAMP_COMP;
    }
 
    for (i = 0; i < g_ClockTimer; i++)
    {
        cast_rootvel_accumulator.x = vec.f[0] + CAST_DAMP * cast_rootvel_accumulator.x;
        cast_rootvel_accumulator.y = vec.f[1] + CAST_DAMP * cast_rootvel_accumulator.y;
        cast_rootvel_accumulator.z = vec.f[2] + CAST_DAMP * cast_rootvel_accumulator.z;
    }
 
    cast_rootvel_smoothed.x = cast_rootvel_accumulator.x * CAST_DAMP_COMP;
    cast_rootvel_smoothed.y = cast_rootvel_accumulator.y * CAST_DAMP_COMP;
    cast_rootvel_smoothed.z = cast_rootvel_accumulator.z * CAST_DAMP_COMP;
 
    cast_rootpos_smoothed.x += cast_rootvel_smoothed.x * g_GlobalTimerDelta;
    cast_rootpos_smoothed.y += cast_rootvel_smoothed.y * g_GlobalTimerDelta;
    cast_rootpos_smoothed.z += cast_rootvel_smoothed.z * g_GlobalTimerDelta;
 
    mtx4TransformVecInPlace(cast_model->render_pos, &vec);
 
    vec.f[0] -= cast_rootpos_smoothed.x;
    vec.f[1] -= cast_rootpos_smoothed.y;
    vec.f[2] -= cast_rootpos_smoothed.z;
 
    if (cast_camera_reset)
    {
        cast_camera_reset = FALSE;

        cast_target_accumulator.x = vec.f[0] / CAST_DAMP_COMP;
        cast_target_accumulator.y = vec.f[1] / CAST_DAMP_COMP;
        cast_target_accumulator.z = vec.f[2] / CAST_DAMP_COMP;
    }
 
    for (i = 0; i < g_ClockTimer; i++)
    {
        cast_target_accumulator.x = vec.f[0] + CAST_DAMP * cast_target_accumulator.x;
        cast_target_accumulator.y = vec.f[1] + CAST_DAMP * cast_target_accumulator.y;
        cast_target_accumulator.z = vec.f[2] + CAST_DAMP * cast_target_accumulator.z;
    }
 
    cast_target_smoothed.x = cast_target_accumulator.x * CAST_DAMP_COMP;
    cast_target_smoothed.y = cast_target_accumulator.y * CAST_DAMP_COMP;
    cast_target_smoothed.z = cast_target_accumulator.z * CAST_DAMP_COMP;
 
    vec.f[0] = cast_rootpos_smoothed.x + cast_target_smoothed.x;
    vec.f[1] = (cast_rootpos_smoothed.y + cast_target_smoothed.y) - 10.0f;
    vec.f[2] = cast_rootpos_smoothed.z + cast_target_smoothed.z;
 
    tarpos.x += vec.f[0];
    tarpos.y += vec.f[1];
    tarpos.z += vec.f[2];
 
    campos.x += cast_rootpos_smoothed.x;
    campos.y += cast_rootpos_smoothed.y + 52.5f;
    campos.z += cast_rootpos_smoothed.z;
 
    matrix_4x4_set_lookat_target(&mtx, campos.x, campos.y, campos.z, tarpos.x, tarpos.y, tarpos.z, up.x, up.y, up.z);
 
    renderdata.basemtx = &mtx;
    renderdata.mtxlist = (Mtxf *)dynAllocate(cast_model->obj->numMatrices << 6);
 
    subcalcmatrices(&renderdata, cast_model);
 
    if (cast_model_weapon != NULL)
    {
        renderdata.basemtx = modelFindNodeMtx(cast_model, cast_model_weapon->attachedto_objinst, 0);
 
        if (cast_model_weapon->attachedto_objinst == cast_model->obj->Switches[5])
        {
            matrix_4x4_set_rotation_around_z(M_PI_F, &mtx2);
            matrix_4x4_multiply_in_place(renderdata.basemtx, &mtx2);
 
            renderdata.basemtx = &mtx2;
        }
 
        renderdata.mtxlist = (Mtxf *)dynAllocate(cast_model_weapon->obj->numMatrices << 6);
 
        instcalcmatrices(&renderdata, cast_model_weapon);
    }
 
    renderdata.PropType = PROP_TYPE_EXPLOSION;
    renderdata.zbufferenabled = TRUE;
    renderdata.gdl = DL;
    renderdata.flags = 3;
 
    subdraw(&renderdata, cast_model);
 
    if (cast_model_weapon != NULL)
    {
        subdraw(&renderdata, cast_model_weapon);
    }
 
    DL = renderdata.gdl;
 
    modelSetDistanceDisabled(0);
 
    for (i = 0; i < cast_model->obj->numMatrices; i++)
    {
        matrix_4x4_copy(&((s8 *)cast_model->render_pos)[i * sizeof(Mtxf)], &mtx2);
        matrix_4x4_f32_to_s32(&mtx2, &((Mtxf *)cast_model->render_pos)[i]);
    }
 
    if (cast_model_weapon != NULL)
    {
        for (i = 0; i < cast_model_weapon->obj->numMatrices; i++)
        {
            matrix_4x4_copy(&((s8 *)cast_model_weapon->render_pos)[i * sizeof(Mtxf)], &mtx2);
 
            matrix_4x4_f32_to_s32(&mtx2, &((Mtxf *)cast_model_weapon->render_pos)[i]);
        }
    }
 
    gDPSetScissor(DL++, G_SC_NON_INTERLACE, 0, 0, 440, 330);
    gDPPipeSync(DL++);
    gDPSetCycleType(DL++, G_CYC_1CYCLE);
    gDPSetColorDither(DL++, G_CD_DISABLE);
    gDPSetTexturePersp(DL++, G_TP_NONE);
    gDPSetAlphaCompare(DL++, G_AC_NONE);
    gDPSetTextureLOD(DL++, G_TL_TILE);
    gDPSetTextureFilter(DL++, G_TF_BILERP);
    gDPSetTextureConvert(DL++, G_TC_FILT);
    gDPSetTextureLUT(DL++, G_TT_NONE);
    gDPSetRenderMode(DL++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(DL++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(DL++, 0, 0, 0, 0, 0, 0xFF - (s32)(255.0f * fade));
    gDPFillRectangle(DL++, 0, 0, 440, 330);
    gDPPipeSync(DL++);
    gDPSetColorDither(DL++, G_CD_BAYER);
    gDPSetTexturePersp(DL++, G_TP_PERSP);
    gDPSetTextureLOD(DL++, G_TL_LOD);
 
    DL = microcode_constructor(DL);
 
    if (full_actor_intro == FALSE)
    {
        text = langGet( (u16)intro_char_table[intro_character_index].text1);
 
        textMeasure(&textheight, &textwidth, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
 
        x = 315 - textwidth / 2;
        y = 108;
 
        DL = microcode_constructor_related_to_menus(DL, x, 108, x + textwidth + 1, textheight + 109, 0);
 
        DL = textRender(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, (u32)(255.0f * fade) | 0xFFFFFF00, viGetX(), viGetY(), 0, 0);
    }
 
    text = langGet((u16)intro_char_table[intro_character_index].text2);
 
    textMeasure(&textheight, &textwidth, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
 
    x = 315 - textwidth / 2;
    y = 152;
 
    DL = microcode_constructor_related_to_menus(DL, x, 152, x + textwidth + 1, textheight + 153, 0);
 
    DL = textRender(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, (u32)(255.0f * fade) | 0xFFFFFF00, viGetX(), viGetY(), 0, 0);
 
    text = langGet((u16)intro_char_table[intro_character_index].text3);
 
    textMeasure(&textheight, &textwidth, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
 
    x = 315 - textwidth / 2;
    y = 174;
 
    DL = microcode_constructor_related_to_menus(DL, x, 174, x + textwidth + 1, textheight + 175, 0);
 
    return textRender(DL, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, (u32)(255.0f * fade) | 0xFFFFFF00, viGetX(), viGetY(), 0, 0);
}


//********************************************************************************************************
//SPECTRUM EMULATOR
//********************************************************************************************************
void init_menu19_spectrum(void)
{
    #if defined(LEFTOVERSPECTRUM)
    SPECTRUMROMS rom = ROM_SABRE;

    if (!joyGetButtons(2, L_CBUTTONS))
    {
        if (joyGetButtons(2, R_CBUTTONS))
        {
            rom = ROM_ATIC;
        }
        else if (joyGetButtons(2, U_CBUTTONS))
        {
            rom = ROM_JETPAC;
        }
        else if (joyGetButtons(2, D_CBUTTONS))
        {
            rom = ROM_JETMAN;
        }
        else if (joyGetButtons(2, L_JPAD))
        {
            rom = ROM_ALIEN8;
        }
        else if (joyGetButtons(2, R_JPAD))
        {
            rom = ROM_GUNFRIGHT;
        }
        else if (joyGetButtons(2, U_JPAD))
        {
            rom = ROM_UNDER;
        }
        else
        {
            rom = ROM_SABRE;
            if (joyGetButtons(2, D_JPAD))
            {
                rom = ROM_KNIGHTLORE;
            }
        }
    }
    init_spectrum_game(rom);
    #endif
}


void update_menu19_spectrum(void) 
{
    return;
}


void interface_menu19_spectrum(void)
{
    #if defined(LEFTOVERSPECTRUM)
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);
    run_spectrum_game();
    #endif
}


#if !defined(LEFTOVERSPECTRUM)
Gfx* constructor_menu19_spectrum(Gfx* DL)
{
    return DL;
}
#else
// Address 0x7F01A4E0 NTSC
Gfx * constructor_menu19_spectrum(Gfx * DL)
{
    DL = clear_framebuffer_black(DL);

    gDPSetCycleType(DL++, G_CYC_1CYCLE);
    gDPSetRenderMode(DL++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    gSPClearGeometryMode(DL++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(DL++, G_CC_DECALRGB, G_CC_DECALRGB);
    gDPSetTextureFilter(DL++, G_TF_POINT);
    gDPSetTextureImage(DL++, G_IM_FMT_CI, G_IM_SIZ_16b, 1, color_palette_entries_50_percent);
    gDPSetTile(DL++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 1, 0x100, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(DL++);
    gDPLoadTLUTCmd2(DL++, G_TX_LOADTILE, 7);
    gDPPipeSync(DL++);
    gDPSetTextureLUT(DL++, G_TT_RGBA16);

    DL = spectrum_draw_screen(DL);

    return DL;
}
#endif


void frontChangeMenu(MENU menu, s32 reload)
{
    if ((menu == MENU_RUN_STAGE) || (menu == MENU_SPECTRUM_EMU))
    {
        is_emulating_spectrum = TRUE;
    }

    if (reload)
    {
        menu_update = menu;
        return;
    }

    maybe_prev_menu = menu;
}


MENU get_currentmenu(void)
{
  return current_menu;
}


#ifndef TARGET_N64
/* SMART MOUSE LOCK (bondview2.c portGameplayHasPointer): 1 only while a
 * mission is running; every menu, briefing, file select and the Spectrum
 * emulator return 0, because there the pointer belongs to the player. */
s32 portStageRunning(void)
{
    return current_menu == MENU_RUN_STAGE;
}
#endif


#ifndef TARGET_N64
/* GE_STAGE=<name|LEVELID> [GE_DIFF=0..3]: debug launch straight into a solo
 * mission, skipping every menu (e.g. GE_STAGE=frigate). Uses the ramrom demo
 * launch recipe (folder + difficulty + briefing + jump to MENU_RUN_STAGE),
 * fired once after the first menus settle. */
static void portDirectLaunchTick(void)
{
    extern char *getenv(const char *);
    extern int printf(const char *, ...);
    static const struct { const char *name; s32 id; } stages[] = {
        { "dam", LEVELID_DAM },           { "facility", LEVELID_FACILITY },
        { "runway", LEVELID_RUNWAY },     { "surface", LEVELID_SURFACE },
        { "bunker1", LEVELID_BUNKER1 },   { "silo", LEVELID_SILO },
        { "frigate", LEVELID_FRIGATE },   { "surface2", LEVELID_SURFACE2 },
        { "bunker2", LEVELID_BUNKER2 },   { "statue", LEVELID_STATUE },
        { "archives", LEVELID_ARCHIVES }, { "streets", LEVELID_STREETS },
        { "depot", LEVELID_DEPOT },       { "train", LEVELID_TRAIN },
        { "jungle", LEVELID_JUNGLE },     { "control", LEVELID_CONTROL },
        { "caverns", LEVELID_CAVERNS },   { "cradle", LEVELID_CRADLE },
        { "aztec", LEVELID_AZTEC },       { "egypt", LEVELID_EGYPT },
        { "citadel", LEVELID_CITADEL },   { "complex", LEVELID_COMPLEX },
        { "temple", LEVELID_TEMPLE },     { "cuba", LEVELID_CUBA },
    };
    static s32 state; /* 0 = unparsed, 1 = armed, 2 = done/disabled */
    static s32 stage;
    static s32 calls;
    s32 i;

    if (state == 2)
    {
        return;
    }

    if (state == 0)
    {
        const char *e = getenv("GE_STAGE");
        state = 2;
        {   /* -stage <name> asks for exactly this; the env var is the older way
             * and stays supported. The flag wins, like every other flag. */
            extern const char *videoBootStage(void);   /* video.c; weak NULL */
            const char *f = videoBootStage();

            if (f != NULL)
            {
                e = f;
            }
        }
        if (e != NULL && e[0] != '\0')
        {
            if (e[0] >= '0' && e[0] <= '9')
            {
                stage = 0;
                for (i = 0; e[i] >= '0' && e[i] <= '9'; i++)
                {
                    stage = stage * 10 + (e[i] - '0');
                }
                state = 1;
            }
            else
            {
                for (i = 0; i < (s32) (sizeof(stages) / sizeof(stages[0])); i++)
                {
                    s32 j = 0;
                    while (stages[i].name[j] != '\0' &&
                           (e[j] | 0x20) == stages[i].name[j])
                    {
                        j++;
                    }
                    if (stages[i].name[j] == '\0' && e[j] == '\0')
                    {
                        stage = stages[i].id;
                        state = 1;
                        break;
                    }
                }
            }
            if (state != 1)
            {
                printf("ge: -stage/GE_STAGE=%s not recognized\n", e);
            }
        }
        return;
    }

    /* GE_STAGE_AFTER=<ticks> delays the launch (default 120 = ~2s, enough for
     * the boot menus to settle). A long delay lets the harness sit in the menus
     * first, which some stage-load crashes need to reproduce. */
    {
        static s32 after = -1;

        if (after < 0)
        {
            const char *a = getenv("GE_STAGE_AFTER");

            after = 120;

            if (a != NULL && a[0] >= '0' && a[0] <= '9')
            {
                s32 v = 0;

                for (i = 0; a[i] >= '0' && a[i] <= '9'; i++)
                {
                    v = v * 10 + (a[i] - '0');
                }

                after = v;
            }
        }

        if (++calls < after)
        {
            return; /* let the boot menus settle before hijacking */
        }
    }

    {
        const char *d = getenv("GE_DIFF");
        s32 diff = (d != NULL) ? (d[0] - '0') : 0;
        if (diff < DIFFICULTY_AGENT || diff >= DIFFICULTY_MAX)
        {
            diff = DIFFICULTY_AGENT;
        }
        selected_folder_num = FOLDER1;
        set_selected_difficulty(diff);
        set_solo_and_ptr_briefing(stage);
        frontChangeMenu(MENU_RUN_STAGE, TRUE);
        printf("ge: GE_STAGE direct launch: levelid=%d diff=%d\n", stage, diff);
    }
    state = 2;
}

/* GE_MENU=files|select|options|gameplay|control|display|credits[,after]: frontend
 * test harness. After `after` menu ticks (default 120, boot menus settled) it
 * jumps to the Files folder menu (MENU_MODE_SELECT, folder 1), the Files screen
 * (MENU_FILE_SELECT), its Options page (MENU_PORT_OPTIONS) or one of the
 * Options folder's sheets (MENU_PORT_SHEET), so GE_SHOT can capture layouts
 * unattended.
 *
 * Related knobs:
 *   GE_AUTOSTICK=x,y,after,until  holds a stick (used to measure the analog ramp).
 *   GE_CURSOR=x,y[,ticks]         parks the menu crosshair (hover / click tests
 *                                 with GE_AUTOBTN; GE_NOINPUT skips the mouse so
 *                                 it stays put). Without `ticks` it parks forever,
 *                                 for layout captures; with it, it lets go after
 *                                 that many ticks so a test can watch where the
 *                                 game itself moves the cursor. */
static void portMenuHarnessTick(void)
{
    extern char *getenv(const char *);
    extern int printf(const char *, ...);
    static s32 state;
    static s32 calls;
    static s32 after = 120;
    static s32 target;
    static f32 cx = -1.0f, cy = -1.0f;
    static s32 chold = -1;   /* ticks to keep parking for, -1 = forever */
    static s32 cheld;

    if (state == 0)
    {
        const char *e = getenv("GE_MENU");
        const char *c = getenv("GE_CURSOR");
        s32 i;

        state = 2;
        if (c != NULL)
        {
            f32 v = 0.0f;
            for (i = 0; c[i] >= '0' && c[i] <= '9'; i++) v = v * 10.0f + (c[i] - '0');
            cx = v;
            if (c[i] == ',')
            {
                v = 0.0f;
                for (i++; c[i] >= '0' && c[i] <= '9'; i++) v = v * 10.0f + (c[i] - '0');
                cy = v;
            }
            if (c[i] == ',')
            {
                v = 0.0f;
                for (i++; c[i] >= '0' && c[i] <= '9'; i++) v = v * 10.0f + (c[i] - '0');
                chold = (s32) v;
            }
        }
        if (e != NULL && e[0] != '\0')
        {
            if (e[0] == 'f') target = MENU_MODE_SELECT;
            else if (e[0] == 'i') target = MENU_MISSION_SELECT;   /* GE_MENU=imissions: the solo mission grid */
            else if (e[0] == 's') target = MENU_FILE_SELECT;  /* GE_MENU=select: the Files screen */
            else if (e[0] == 'o') target = MENU_PORT_OPTIONS; /* GE_MENU=options: the OPTIONS page */
            else if (e[0] == 'x')
            {   /* Multiplayer setup: GE_MENU=xsetup[2-4], xplayers[2-4] or
                 * xoptions, where the digit is the player count (default 2). */
                const char *z = e;
                s32 np = 2;

                while (*z != '\0' && *z != ',') z++;
                if (z > e + 1 && z[-1] >= '2' && z[-1] <= '4')
                {
                    np = z[-1] - '0';
                }
                gamemode = GAMEMODE_MULTI;   /* what mode select sets on the way here; get_selected_num_players says 1 without it */
                init_mp_options_for_scenario(np);
                if (e[1] == 'p')
                {
                    target = MENU_MP_CONTROL_STYLE;
                }
                else if (e[1] == 'o')
                {
                    port_sheet_kind = PORT_SHEET_MPOPTS;
                    target = MENU_PORT_SHEET;
                }
                else if (e[1] == 'c' || e[1] == 'e' || e[1] == 'n' || e[1] == 'l')
                {   /* =xcustom: the Weapons list's Custom sets; =xedit: the first saved set's
                     * editor; =xnew: a New Set draft; =xload: Custom with Load Set's list up */
                    port_mp_pick = PORT_MP_PICK_WEAPONS;
                    if (e[1] == 'n')
                    {
                        portWsetEditOpen("");
                    }
                    else if (e[1] == 'l')
                    {
                        portWsetListOpen();
                        portWsetListOpenFor(PORT_WSET_LIST_LOAD);
                    }
                    else if (e[1] == 'c' || portWsetCount() == 0)
                    {
                        portWsetListOpen();
                    }
                    else
                    {
                        portWsetEditOpen(portWsetName(0));
                    }
                    target = MENU_MP_SCENARIO_SELECT;
                }
                else if (e[1] == 's' && e[2] == 't')
                {   /* =xstage[2-4]: the arena grid */
                    target = MENU_MP_STAGE_SELECT;
                }
                else if (e[1] == 'm')
                {   /* =xmlimits: the Game Limits screen; =xmlives: with Lives' drop-down up;
                     * =xmsettings: Load / Save Settings */
                    port_mp_pick = (e[2] == 's') ? PORT_MP_PICK_SETTINGS : PORT_MP_PICK_LIMITS;
                    port_mp_pick_hover = -1;
                    portMpSetRescan();
                    if (e[2] == 'l' && e[3] == 'i' && e[4] == 'v')
                    {
                        portWsetListOpenFor(PORT_LIM_LIST_BASE + PORT_SCEN_LIVES);
                    }
                    target = MENU_MP_SCENARIO_SELECT;
                }
                else if (e[1] == 'l' || e[1] == 'w')
                {   /* =xlength / =xweapons: the Game Length / Weapons lists */
                    port_mp_pick = (e[1] == 'l') ? PORT_MP_PICK_LENGTH : PORT_MP_PICK_WEAPONS;
                    port_mp_pick_hover = -1;
                    target = MENU_MP_SCENARIO_SELECT;
                }
                else
                {
                    target = MENU_MP_OPTIONS;
                }
            }
            else if (e[0] == 'g' || e[0] == 'd' || e[0] == 'c' || e[0] == 'p' || e[0] == 'a' || e[0] == 'v'
                     || e[0] == 'h' || e[0] == 'm' || e[0] == 'b')
            {   /* GE_MENU=gameplay|general|hud|graphics|control|display|video|audio|credits|player[1-4]
                 * |mouse|pad|cgeneral|aiming (the last four are a player's control pages) */
                extern s32 portMenuHarnessSheet(char first, char second);
                const char *z = e;
                char sec = e[1];

                while (*z != '\0' && *z != ',') z++;   /* to the terminator or the ',N' tick
                                                        * suffix */
                if (z > e + 1 && z[-1] >= '1' && z[-1] <= '4')
                {   /* GE_MENU=player2 - the player number is the LAST
                     * character, not e[1] ('l'), which quietly gave
                     * player 1 every time */
                    sec = z[-1];
                }
                target = MENU_PORT_SHEET;
                portMenuHarnessSheet(e[0], sec);
            }
            /* GE_MENU=port is gone with the Extended Options menu;
             * the settings live in the watch now and
             * GE_WATCHPAGE parks it on a page - docs/port/watch-settings-pages.md */
            for (i = 0; e[i] != '\0' && e[i] != ','; i++) { }
            if (e[i] == ',')
            {
                after = 0;
                for (i++; e[i] >= '0' && e[i] <= '9'; i++) after = after * 10 + (e[i] - '0');
            }
            if (target != 0) state = 1;
        }
    }
    if (cx >= 0.0f && cy >= 0.0f && (chold < 0 || cheld < chold))
    {
        cursor_h_pos = cx;
        cursor_v_pos = cy;
        if (state == 2)
        {
            cheld++;   /* count only once the menu we asked for is up */
        }
    }
    if (state != 1)
    {
        return;
    }
    if (++calls < after)
    {
        return;
    }
    selected_folder_num = FOLDER1;
    frontChangeMenu(target, FALSE);
    printf("ge: GE_MENU harness: menu=%d\n", target);
    state = 2;
}

/* GE_MP=<arena> [GE_MPSCENARIO=0..7] [GE_MPPLAYERS=2..4]: direct multiplayer
 * launch. MP arenas need an MP setup (stage table index + scenario + player
 * count), not a solo difficulty; this mirrors the MP start-tab handler (the
 * multi_stage_setups lookup + briefingpage=-1).
 *
 * Scenarios: 0 Normal, 1 YOLT, 2 Flag Tag, 3 Golden Gun, 4 LTK, 5 2v2, 6 3v1, 7
 * 2v1. GE_MPCHARS=a,b,c,d picks the characters (see the parser below) and
 * GE_MPLENGTH=0..7 the game length (default LEN_10MIN); the weapon set stays at
 * its default. */
static void portDirectLaunchMpTick(void)
{
    extern char *getenv(const char *);
    extern int printf(const char *, ...);
    static const struct { const char *name; s32 index; } arenas[] = {
        { "temple", 1 },   { "complex", 2 },  { "caves", 3 },
        { "library", 4 },  { "basement", 5 }, { "stack", 6 },
        { "facility", 7 }, { "bunker", 8 },   { "archives", 9 },
        { "caverns", 10 }, { "egypt", 11 },
        { "statue", MP_STAGE_STATUE }, { "cradle", MP_STAGE_CRADLE },
    };
    static s32 state;
    static s32 mpindex;
    static s32 calls;
    s32 i;

    if (state == 2)
    {
        return;
    }

    if (state == 0)
    {
        const char *e = getenv("GE_MP");
        state = 2;
        if (e != NULL && e[0] != '\0')
        {
            for (i = 0; i < (s32) (sizeof(arenas) / sizeof(arenas[0])); i++)
            {
                s32 j = 0;
                while (arenas[i].name[j] != '\0' && (e[j] | 0x20) == arenas[i].name[j])
                {
                    j++;
                }
                if (arenas[i].name[j] == '\0' && e[j] == '\0')
                {
                    mpindex = arenas[i].index;
                    state = 1;
                    break;
                }
            }
            if (state != 1)
            {
                printf("ge: GE_MP=%s not recognized\n", e);
            }
        }
        return;
    }

    if (++calls < 120)
    {
        return;
    }

    {
        const char *s = getenv("GE_MPSCENARIO");
        const char *p = getenv("GE_MPPLAYERS");
        const char *gl = getenv("GE_MPLENGTH");
        s32 scen = (s != NULL) ? (s[0] - '0') : SCENARIO_NORMAL;
        s32 players = (p != NULL) ? (p[0] - '0') : 2;
        if (scen < SCENARIO_NORMAL || scen >= MPSCENARIOS_MAX)
        {
            scen = SCENARIO_NORMAL;
        }
        if (players < 2 || players > 4)
        {
            players = 2;
        }
        /* GE_MPCHARS=a,b,c,d picks each player's MP character by index into
         * mp_chr_setup (0 Bond, 1 Natalya, 2 Trevelyan, 3 Xenia, 4 Ourumov,
         * 5 Boris, 6 Valentin, 7 Mishkin, 8 Mayday, 9 Jaws, 10 Oddjob, 11 Baron
         * Samedi, 12+ unlockable extras); -1 in a slot keeps that player's
         * default. */
        {
            const char *c = getenv("GE_MPCHARS");

            if (c != NULL && c[0] != '\0')
            {
                s32 slot = 0;
                s32 k = 0;

                while (c[k] != '\0' && slot < MAX_PLAYER_COUNT)
                {
                    s32 neg = 0;
                    s32 v = 0;
                    s32 digits = 0;

                    if (c[k] == '-')
                    {
                        neg = 1;
                        k++;
                    }
                    while (c[k] >= '0' && c[k] <= '9')
                    {
                        v = v * 10 + (c[k] - '0');
                        k++;
                        digits++;
                    }
                    if (digits > 0)
                    {
                        player_char[slot] = neg ? -1 : v;
                        printf("ge: GE_MPCHARS: player %d = char %d\n", slot + 1, player_char[slot]);
                    }
                    slot++;
                    if (c[k] == ',')
                    {
                        k++;
                    }
                    else if (c[k] != '\0')
                    {
                        break; /* unparsable tail - keep what we have */
                    }
                }
            }
        }

        /* GE_MPLENGTH=0..7 picks the game length (0 unlimited, 1 5min, 2 10min,
         * 3 20min, 4/5/6 first to 5/10/20 points, 7 last alive; default
         * LEN_10MIN). A time limit is the only hands-off way to reach the end
         * screen headless. */
        if (gl != NULL && gl[0] >= '0' && gl[0] <= '7')
        {
            game_length = gl[0] - '0';
            portMpLimFromLength(game_length);   /* the limits read on the port */
            printf("ge: GE_MPLENGTH: game_length=%d\n", game_length);
        }
        {   /* GE_MPWSET=<NAME>: play Data/WeaponSets/<NAME>.gewset; =random / =randomset the rolled sets */
            const char *ws = getenv("GE_MPWSET");
            s32 items[8];

            if (ws != NULL && (strcmp(ws, "random") == 0 || strcmp(ws, "randomset") == 0))
            {   /* =random / =randomset: the two rolled sets (mp_weapon.c portMpWeaponRoll) */
                setMPWeaponSet((ws[6] == '\0') ? 15 : 16);
                printf("ge: GE_MPWSET: %s\n", ws);
            }
            else if (ws != NULL && ws[0] != '\0' && portWsetLoad(ws, items))
            {
                portMpWeaponCustomApply(items);
                portWsetSetCurrent(ws);
                printf("ge: GE_MPWSET: custom weapon set %s\n", ws);
            }
        }

        selected_folder_num = FOLDER1;
        gamemode = GAMEMODE_MULTI;
        selected_num_players = players; /* human count - boss.c gates on >= 2 */
        scenario = scen;
        {   /* GE_MPTEAMS=rbrb: Game Limits' sides, one letter a player (r red, b blue) */
            const char *t = getenv("GE_MPTEAMS");

            if (t != NULL && t[0] != '\0')
            {
                s32 k;

                port_mp_teams_on = TRUE;
                for (k = 0; k < MAX_PLAYER_COUNT && t[k] != '\0'; k++)
                {
                    port_mp_team[k] = (t[k] == 'b' || t[k] == 'B' || t[k] == '1');
                }
                printf("ge: GE_MPTEAMS: %s\n", t);
            }
        }
        {   /* GE_MPSET=<NAME>: Data/MatchSettings/<NAME>.gemp in force (its arena too) */
            const char *ms = getenv("GE_MPSET");

            if (ms != NULL && ms[0] != '\0')
            {
                printf("ge: GE_MPSET: %s %s\n", ms, portMpSetApply(ms) ? "loaded" : "NOT FOUND");
                scen = scenario;
                mpindex = MP_stage_selected;
            }
        }
        portMpTeamsEnsure();
        MP_stage_selected = mpindex;
        selected_stage = multi_stage_setups[mpindex].stage_id;
        briefingpage = -1;
        frontChangeMenu(MENU_RUN_STAGE, TRUE);
        printf("ge: GE_MP direct launch: arena=%d levelid=%d scenario=%d players=%d\n",
               mpindex, selected_stage, scen, players);
    }
    state = 2;
}

/* -skip [N]: walk the boot screens without waiting them out.
 *
 * The chain is legal -> Nintendo -> Rareware -> eye -> GoldenEye logo -> file
 * select, each held by its own timer in its own interface function. A bare
 * -skip skips all five (land on the file select, which is what a capture run
 * or an impatient launch wants); -skip N skips N of them, so -skip 2 sits on the
 * Rareware logo with the Nintendo one already gone.
 *
 * It advances with frontChangeMenu(next, TRUE), the same call the real screens
 * make on their timers and on a button press - including the eye intro, which
 * already supports jumping to the logo mid-animation when someone mashes a
 * button. Nothing here reaches into a screen's internals, so each screen's
 * update_* still runs and still tears its own state down.
 *
 * The legal screen is the one screen a button press cannot skip on a first
 * boot (interface_menu00_legalscreen ignores ANY_BUTTON until
 * is_first_time_on_legal_screen clears), which is why this drives the
 * transition directly rather than faking input.
 *
 * One skip per arrival: MENU_SWITCH_SCREENS is the fade between two screens and
 * is left alone, so the budget is spent when a screen is actually reached, not
 * per frame. */
static void portBootSkipTick(void)
{
    extern s32 videoBootSkipScreens(void);   /* video.c; weak 0 headless */
    static s32 budget = -2;                  /* -2 = not read yet */
    static s32 spent;
    static MENU skipped = MENU_INVALID;      /* the screen we already skipped */
    MENU next = MENU_INVALID;

    if (budget == -2)
    {
        budget = videoBootSkipScreens();
        if (budget < 0)
        {
            budget = 5;   /* "all": the chain is five screens long */
        }
        if (budget > 0)
        {
            extern int printf(const char *, ...);

            printf("ge: -skip: skipping %d boot screen%s\n",
                   (int) budget, (budget == 1) ? "" : "s");
        }
    }
    if (budget <= 0 || current_menu == MENU_SWITCH_SCREENS)
    {
        return;
    }
    if (current_menu == skipped)
    {
        return;   /* this screen already spent its skip */
    }
    switch (current_menu)
    {
        case MENU_LEGAL_SCREEN:    next = MENU_NINTENDO_LOGO;  break;
        case MENU_NINTENDO_LOGO:   next = MENU_RAREWARE_LOGO;  break;
        case MENU_RAREWARE_LOGO:   next = MENU_EYE_INTRO;      break;
        case MENU_EYE_INTRO:       next = MENU_GOLDENEYE_LOGO; break;
        case MENU_GOLDENEYE_LOGO:  next = MENU_FILE_SELECT;    break;
        default:                   return;   /* past the chain: done skipping */
    }
    skipped = current_menu;
    spent++;
    if (spent >= budget)
    {
        budget = 0;   /* last one: the next screen is the one the caller wanted */
    }
    frontChangeMenu(next, TRUE);
}

/* GE_DEMO=<name|0-13>: play one of the 14 attract recordings immediately
 * (dam1 dam2 facility1 facility2 facility3 runway1 runway2 bunker1 bunker2
 * silo1 silo2 frigate1 frigate2 train - ramrom_table order, ramromreplay.c).
 *
 * The recordings are the real N64 input streams (port/src/ramrom_data.c). Any
 * keypress ends playback, and a sim divergence trips the per-block
 * randseed/checksum guards (ramrom_replay_handler); both fade back to the title
 * by design. Don't combine with GE_STAGE/GE_MP: all of them would fire and the
 * last frontChangeMenu wins. */
static void portDirectLaunchDemoTick(void)
{
    extern char *getenv(const char *);
    extern int printf(const char *, ...);
    extern void portPlayRamromDemo(s32 index); /* ramromreplay.c */
    static const char *const demos[] = {
        "dam1", "dam2", "facility1", "facility2", "facility3",
        "runway1", "runway2", "bunker1", "bunker2", "silo1",
        "silo2", "frigate1", "frigate2", "train",
    };
    static s32 state; /* 0 = unparsed, 1 = armed, 2 = done/disabled */
    static s32 demoidx;
    static s32 calls;
    s32 i;

    if (state == 2)
    {
        return;
    }

    if (state == 0)
    {
        const char *e = getenv("GE_DEMO");
        state = 2;
        if (e != NULL && e[0] != '\0')
        {
            if (e[0] >= '0' && e[0] <= '9')
            {
                demoidx = 0;
                for (i = 0; e[i] >= '0' && e[i] <= '9'; i++)
                {
                    demoidx = demoidx * 10 + (e[i] - '0');
                }
                if (demoidx < (s32) (sizeof(demos) / sizeof(demos[0])))
                {
                    state = 1;
                }
            }
            else
            {
                for (i = 0; i < (s32) (sizeof(demos) / sizeof(demos[0])); i++)
                {
                    s32 j = 0;
                    while (demos[i][j] != '\0' && (e[j] | 0x20) == demos[i][j])
                    {
                        j++;
                    }
                    if (demos[i][j] == '\0' && e[j] == '\0')
                    {
                        demoidx = i;
                        state = 1;
                        break;
                    }
                }
            }
            if (state != 1)
            {
                printf("ge: GE_DEMO=%s not recognized\n", e);
            }
        }
        return;
    }

    if (++calls < 120)
    {
        return; /* let the boot menus settle (~2s) before hijacking */
    }

    portPlayRamromDemo(demoidx);
    printf("ge: GE_DEMO direct playback: %s (index %d)\n", demos[demoidx], demoidx);
    state = 2;
}
#endif

void menu_init(void)
{
    s32 var_v0;

#ifndef TARGET_N64
    { /* Global volumes: seed the running game from midas.ini on the first frontend
       * tick, since the patched force-sites only fire on menus that (re)start
       * music. Not done from the midas.ini parser, which runs before the sound
       * system exists and would dereference NULL. */
        extern void portApplyGlobalVolumesOnce(void);

        portApplyGlobalVolumesOnce();
        {   /* the six global player options, over the table's boot defaults */
            extern void portPlayerOptsApplyOnce(void); /* options.c */

            portPlayerOptsApplyOnce();
        }
    }
    portDirectLaunchTick();
    portDirectLaunchMpTick();
    portDirectLaunchDemoTick();
    portBootSkipTick();
    portMenuHarnessTick();
#ifdef GE_VIDEO
    { /* A mouse aim press (right button by default) acts as B, the "back",
       * in every frontend menu; pad and keyboard R are untouched. */
        extern s32 portMouseAimPressed(void);
        extern s32 g_portWatchMouseOpen;
        /* The watch's mouse layer mutes mouse-driven L/R while the watch is up;
         * aborting a mission from the watch skipped the reset, leaving it muted
         * in the frontend so mouse-back stopped working. */
        g_portWatchMouseOpen = 0;
        if (portMouseAimPressed())
        {
            portJoyInjectPressed(PLAYER_1, B_BUTTON);
        }
    }
#endif
    { /* Escape = Back (B) in every frontend menu, through the same path as
       * mouse-back; keyboard = player 1. The in-play half (open/close the
       * watch) is in bondview2.c, and the two never run in the same frame, so
       * one latch serves both. */
        extern s32 portEscPressed(void); /* input_pc.c; weak 0 headless */

        if (portEscPressed())
        {
            portJoyInjectPressed(PLAYER_1, B_BUTTON);
        }
    }
    { /* USE = CONFIRM: the Port `use` bind (keyboard =
       * pad 0, a controller's own row = its pad) acts as A in every frontend
       * menu, per pad - the door key confirms. joy_port.c owns the latch. */
        extern s32 portJoyPortUseMenuPressed(s32 pad);
        extern s32 portJoyPortUseHeld(s32 pad);
        extern s32 portJoyPortBackMenuPressed(s32 pad);
        extern void portJoyInjectHeld(s8 contpadnum, u16 mask);
        s32 pad;
        for (pad = 0; pad < 4; pad++)
        {
            if (portJoyPortUseMenuPressed(pad))
            {
                portJoyInjectPressed((s8) pad, A_BUTTON);
            }
            /* ... and use held = A held: the 007-mode bars drag on
             * joyGetButtons(A|Z), a level, and on the Xbox layout the pad's A
             * is the use bind, so the press alone left the bars stuck
             * (mouse-left is a real Z). */
            if (portJoyPortUseHeld(pad))
            {
                portJoyInjectHeld((s8) pad, A_BUTTON);
            }
            /* ... and either inventory bind = B (back) */
            if (portJoyPortBackMenuPressed(pad))
            {
                portJoyInjectPressed((s8) pad, B_BUTTON);
            }
        }
    }
#endif

    if (current_menu == MENU_SWITCH_SCREENS)
    {
        if (spectrum_related_flag != 0)
        {
            if (viGetFrameBuf2() == (cfb_16[1]))
            {
                screen_size = SCREEN_SIZE_440x330;
                spectrum_related_flag = 0;
            }
        }
        else if ((is_emulating_spectrum != 0) && (viGetFrameBuf2() == cfb_16[0]))
        {
            screen_size = SCREEN_SIZE_320x240;
            is_emulating_spectrum = 0;
        }
    }

#if defined(VERSION_EU)
#define MENU_INIT_ASPECT_440 ASPECT_RATIO_EU_400_330
#define MENU_INIT_ASPECT_320 ASPECT_RATIO_EU_320_272
#else
#define MENU_INIT_ASPECT_440 ASPECT_RATIO
#define MENU_INIT_ASPECT_320 ASPECT_RATIO
#endif

    if (screen_size != SCREEN_SIZE_320x240)
    {
        if (viGetFrameBuf2() == (cfb_16[1]))
        {
            viSetFrameBuf2(ptr_menu_videobuffer);
        }

        viSetAspect(MENU_INIT_ASPECT_440);
        viSetXY(440, 330);
        viSetBuf(440, 330);
        set_cur_player_screen_size(440, 330);
        viSetViewSize(440, 330);
        set_cur_player_viewport_size(0, 0);
        viSetViewPosition(0, 0);
    }
    else
    {
        viSetAspect(MENU_INIT_ASPECT_320);
        viSetXY(320, SCREEN_HEIGHT);
        viSetBuf(320, SCREEN_HEIGHT);
        set_cur_player_screen_size(320, SCREEN_HEIGHT);
        viSetViewSize(320, SCREEN_HEIGHT);
        set_cur_player_viewport_size(0, 0);
        viSetViewPosition(0, 0);
    }

#undef MENU_INIT_ASPECT_440
#undef MENU_INIT_ASPECT_320

    if (
        ((menu_update > MENU_INVALID) || (maybe_prev_menu > MENU_INVALID))
        && (current_menu != MENU_SWITCH_SCREENS))
    {
        switch(current_menu) {
            case MENU_LEGAL_SCREEN:           update_menu00_legalscreen();          break;
            case MENU_NINTENDO_LOGO:          update_menu01_nintendo();             break;
            case MENU_RAREWARE_LOGO:          update_menu02_rareware();             break;
            case MENU_EYE_INTRO:              update_menu_03_gunbarrel();                 break;
            case MENU_GOLDENEYE_LOGO:         update_menu04_goldeneye();            break;
            case MENU_FILE_SELECT:            update_menu05_filesel();              break;
            case MENU_MODE_SELECT:            update_menu06_modesel();              break;
#ifndef TARGET_N64
            case MENU_PORT_OPTIONS:           update_menu1B_portoptions();          break;
            case MENU_PORT_SHEET:             update_menu1C_portsheet();            break;
#endif
            case MENU_MISSION_SELECT:         update_menu07_missionsel();           break;
            case MENU_DIFFICULTY:             update_menu08_difficulty();           break;
            case MENU_007_OPTIONS:            update_menu09_007options();           break;
            case MENU_BRIEFING:               update_menu0A_briefing();             break;
            case MENU_MISSION_FAILED:         update_menu0C_missionfailed();        break;
            case MENU_MISSION_COMPLETE:       update_menu0D_missioncomplete();      break;
            case MENU_MP_OPTIONS:             update_menu0E_mpoptions();            break;
            case MENU_MP_SCENARIO_SELECT:     update_menu13_mpscenario();           break;
            case MENU_MP_CHAR_SELECT:         update_menu0F_mpcharsel();            break;
            case MENU_MP_TEAMS:               update_menu14_mpteams();              break;
            case MENU_MP_HANDICAP:            update_menu10_mphandicap();           break;
            case MENU_MP_CONTROL_STYLE:       update_menu11_mpcontrols();           break;
            case MENU_MP_STAGE_SELECT:        update_menu12_mpstage();              break;
            case MENU_CHEAT:                  update_menu15_cheat();                break;
            case MENU_NO_CONTROLLERS:         update_menu16_nocontrollers();        break;
            case MENU_DISPLAY_CAST:           update_menu18_displaycast();          break;
            case MENU_SPECTRUM_EMU:           update_menu19_spectrum();             break;
#ifndef TARGET_N64
#endif
        }

        if (menu_update > MENU_INVALID)
        {
            current_menu = MENU_SWITCH_SCREENS;
            reset_menutimer();
        }
    }

    if (maybe_prev_menu > MENU_INVALID)
    {
        current_menu = maybe_prev_menu;
        maybe_prev_menu = MENU_INVALID;

#ifndef TARGET_N64
        { /* EXTERNAL TEXTURES have an Options-only set (port_textures.c):
           * the wallet's stamps and photo are one shared model, so the pool
           * is swapped on the way in and out of the Options folder */
            extern void portTexContextSet(s32 options);

            portTexContextSet(current_menu == MENU_PORT_OPTIONS
                              || (current_menu == MENU_PORT_SHEET && port_sheet_kind != PORT_SHEET_MPOPTS)); /* the MP sheet is the MP wallet's */
        }
#endif
        switch(current_menu) {
            case MENU_LEGAL_SCREEN:           init_menu00_legalscreen();            break;
            case MENU_NINTENDO_LOGO:          init_menu01_nintendo();               break;
            case MENU_RAREWARE_LOGO:          init_menu02_rarelogo();               break;
            case MENU_EYE_INTRO:              init_menu03_gunbarrel();               break;
            case MENU_GOLDENEYE_LOGO:         init_menu04_goldeneyelogo();          break;
            case MENU_FILE_SELECT:            init_menu05_fileselect();             break;
            case MENU_MODE_SELECT:            init_menu06_modeselect();             break;
#ifndef TARGET_N64
            case MENU_PORT_OPTIONS:           init_menu1B_portoptions();            break;
            case MENU_PORT_SHEET:             init_menu1C_portsheet();              break;
#endif
            case MENU_MISSION_SELECT:         init_menu07_missionselect();          break;
            case MENU_DIFFICULTY:             init_menu08_difficultyselect();       break;
            case MENU_007_OPTIONS:            init_menu09_007difficultyselect();    break;
            case MENU_BRIEFING:               init_menu0A_briefing();               break;
            case MENU_RUN_STAGE:
#ifdef GE_VIDEO
                frontStageRunningMouseReset();
#endif
                init_menu0B_runstage();
                break;
            case MENU_MISSION_FAILED:         init_menu0C_missionfailed();          break;
            case MENU_MISSION_COMPLETE:       init_menu0D_missioncomplete();        break;
            case MENU_MP_OPTIONS:             init_menu0E_mpoptions();              break;
            case MENU_MP_SCENARIO_SELECT:     init_menu13_mpscenariosel();          break;
            case MENU_MP_CHAR_SELECT:         init_menu0f_mpcharsel();              break;
            case MENU_MP_TEAMS:               init_menu14_mpteamsel();              break;
            case MENU_MP_HANDICAP:            init_menu10_mphandicap();             break;
            case MENU_MP_CONTROL_STYLE:       init_menu11_mpcontrol();              break;
            case MENU_MP_STAGE_SELECT:        init_menu12_mpstage();                break;
            case MENU_CHEAT:                  init_menu15_cheat();                  break;
            case MENU_NO_CONTROLLERS:         init_menu16_nocontroller();           break;
            case MENU_DISPLAY_CAST:           init_menu18_displaycast();            break;
            case MENU_SPECTRUM_EMU:           init_menu19_spectrum();               break;
#ifndef TARGET_N64
#endif
        }
    }

#ifdef GE_VIDEO
    {   /* Who is player 1 (input_pc.c): keyboard and mouse, unless a pad
         * presses something during the intro screens, which moves that pad to
         * player 1. Called here rather than from the pad poll because the
         * reassign must not run during a controller read. */
        extern void portInputP1ClaimWindow(s32 open);
        /* The fade between two intro screens still counts as the intro:
         * MENU_SWITCH_SCREENS sorts after MENU_GOLDENEYE_LOGO, which used to
         * close the window at the first switch. The switch state carries the
         * window's last answer instead of giving its own. */
        static s32 intro = TRUE;

        if (current_menu != MENU_SWITCH_SCREENS)
        {
            intro = (current_menu <= MENU_GOLDENEYE_LOGO);
        }
        portInputP1ClaimWindow(intro);
    }
#endif

#ifndef TARGET_N64
    {   /* profiles save themselves (port_profiles.c AUTOSAVE): a change that
         * has settled, or everything at once as a stage starts */
        extern void portProfileAutoSave(s32 flush);
        portProfileAutoSave(current_menu == MENU_RUN_STAGE);
    }
#endif

    switch(current_menu) {
        case MENU_LEGAL_SCREEN:           interface_menu00_legalscreen();           break;
        case MENU_SWITCH_SCREENS:         interface_menu17_switchscreens();         break;
        case MENU_NINTENDO_LOGO:          interface_menu01_nintendo();              break;
        case MENU_RAREWARE_LOGO:          interface_menu02_rareware();              break;
        case MENU_EYE_INTRO:              interface_menu03_eye();                   break;
        case MENU_GOLDENEYE_LOGO:         interface_menu04_goldeneyelogo();         break;
        case MENU_FILE_SELECT:            interface_menu05_fileselect();            break;
        case MENU_MODE_SELECT:            interface_menu06_modesel();               break;
#ifndef TARGET_N64
        case MENU_PORT_OPTIONS:           interface_menu1B_portoptions();           break;
        case MENU_PORT_SHEET:             interface_menu1C_portsheet();             break;
#endif
        case MENU_MISSION_SELECT:         interface_menu07_missionsel();            break;
        case MENU_DIFFICULTY:             interface_menu08_difficulty();            break;
        case MENU_007_OPTIONS:            interface_menu09_007options();            break;
        case MENU_BRIEFING:               interface_menu0A_briefing();              break;
        case MENU_MISSION_FAILED:         interface_menu0C_missionfailed();         break;
        case MENU_MISSION_COMPLETE:       interface_menu0D_missioncomplete();       break;
        case MENU_MP_OPTIONS:             interface_menu0E_mpoptions();             break;
        case MENU_MP_SCENARIO_SELECT:     interface_menu13_mpscenario();            break;
        case MENU_MP_CHAR_SELECT:         interface_menu0F_mpcharsel();             break;
        case MENU_MP_TEAMS:               interface_menu14_mpteams();               break;
        case MENU_MP_HANDICAP:            interface_menu10_mphandicap();            break;
        case MENU_MP_CONTROL_STYLE:       interface_menu11_mpcontrols();            break;
        case MENU_MP_STAGE_SELECT:        interface_menu12_mpstage();               break;
        case MENU_CHEAT:                  interface_menu15_cheat();                 break;
        case MENU_NO_CONTROLLERS:         interface_menu16_nocontrollers();         break;
        case MENU_DISPLAY_CAST:           interface_menu18_displaycast();           break;
        case MENU_SPECTRUM_EMU:           interface_menu19_spectrum();              break;
#ifndef TARGET_N64
#endif
        case MENU_RUN_STAGE:
#ifdef GE_VIDEO
            frontStageRunningMouseReset();   /* the menu after the stage gets its entry snap */
#endif
            if (interface_menu0B_runstage())
            {
#ifndef TARGET_N64
                port_credits_roll = FALSE;   /* a credits roll that ended here never reaches the cast parade */
#endif
                frontChangeMenu(MENU_LEGAL_SCREEN, 1);
            }
            else if (gamemode == GAMEMODE_MULTI)
            {
                frontChangeMenu(MENU_MP_OPTIONS, 1);
            }
            /**
             * The intro roster only shows main characters. The credits roster shows minor characters like guards.
             */
            else if (selected_stage == LEVELID_CUBA)
            {
                do_extended_cast_display(TRUE);
                frontChangeMenu(MENU_DISPLAY_CAST, 1);
            }
            else
            {
                frontChangeMenu(MENU_MISSION_FAILED, 1);
            }
    }

}


Gfx * menu_jump_constructor_handler(Gfx *DL)
{
    switch(current_menu) {
        case MENU_LEGAL_SCREEN:
            DL = constructor_menu00_legalscreen(DL);
            break;
        case MENU_SWITCH_SCREENS:
            DL = constructor_menu17_switchscreens(DL);
            break;
        case MENU_NINTENDO_LOGO:
            DL = constructor_menu01_nintendo(DL);
            break;
        case MENU_RAREWARE_LOGO:
            DL = constructor_menu02_rareware(DL);
            break;
        case MENU_EYE_INTRO:
            DL = constructor_menu03_eye(DL);
            break;
        case MENU_GOLDENEYE_LOGO:
            DL = constructor_menu04_goldeneyelogo(DL);
            break;
        case MENU_FILE_SELECT:
            DL = constructor_menu05_fileselect(DL);
            break;
        case MENU_MODE_SELECT:
            DL = constructor_menu06_modesel(DL);
            break;
#ifndef TARGET_N64
        case MENU_PORT_OPTIONS:
            DL = constructor_menu1B_portoptions(DL);
            break;
        case MENU_PORT_SHEET:
            DL = constructor_menu1C_portsheet(DL);
            break;
#endif
        case MENU_MISSION_SELECT:
            DL = constructor_menu07_missionsel(DL);
            break;
        case MENU_DIFFICULTY:
            DL = constructor_menu08_difficulty(DL);
            break;
        case MENU_007_OPTIONS:
            DL = constructor_menu09_007options(DL);
            break;
        case MENU_BRIEFING:
            DL = constructor_menu0A_briefing(DL);
            break;
        case MENU_MISSION_FAILED:
            DL = constructor_menu0C_missionfailed(DL);
            break;
        case MENU_MISSION_COMPLETE:
            DL = constructor_menu0D_missioncomplete(DL);
            break;
        case MENU_MP_OPTIONS:
            DL = constructor_menu0E_mpoptions(DL);
            break;
        case MENU_MP_SCENARIO_SELECT:
            DL = constructor_menu13_mpscenario(DL);
            break;
        case MENU_MP_CHAR_SELECT:
            DL = constructor_menu0F_mpcharsel(DL);
            break;
        case MENU_MP_TEAMS:
            DL = constructor_menu14_mpteams(DL);
            break;
        case MENU_MP_HANDICAP:
            DL = constructor_menu10_mphandicap(DL);
            break;
        case MENU_MP_CONTROL_STYLE:
            DL = constructor_menu11_mpcontrol(DL);
            break;
        case MENU_MP_STAGE_SELECT:
            DL = constructor_menu12_mpstage(DL);
            break;
        case MENU_CHEAT:
            DL = constructor_menu15_cheat(DL);
            break;
        case MENU_NO_CONTROLLERS:
            DL = constructor_menu16_nocontrollers(DL);
            break;
        case MENU_DISPLAY_CAST:
            DL = constructor_menu18_displaycast(DL);
            break;
        case MENU_SPECTRUM_EMU:
            DL = constructor_menu19_spectrum(DL);
            break;
    }

    return DL;
}


