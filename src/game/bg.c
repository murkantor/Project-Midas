#include <ultra64.h>
#include "game/lightfixture.h"
#include "game/model.h"
#include "game/tex.h"
#include "game/ob.h"
#include "mema.h"
#include <PR/os.h>
#include <PR/gbi.h>
#include <gbi_extension.h>
#include <bondconstants.h>
#include <deb.h>
#include <fr.h>
#include <memp.h>
#include "bg.h"
#include "bondview.h"
#include "chr.h"
#include "debug_camera.h"
#include "debugmenu_handler.h"
#include "decompress.h"
#include "bgfog.h"
#include "lv.h"
#include "math_ceil.h"
#include "matrixmath.h"
#include "player.h"
#include "stan.h"
#include "explosion.h"
#include "bgroomtrans.h"

#ifndef TARGET_N64
#include <stdio.h>  /* GE_ROOM_AUDIT portal-corruption diagnostic */
#include <stdlib.h>
#include <string.h>
#endif


#define BG_STACK_SIZE 20
#define STAGES_MAX 38

typedef struct GlobalVisCommand {
    u8 type;
    u8 length;
    u8 padding[2];
    s32 arg;
} GlobalVisCommand;

typedef struct Unk80081600 {
    bbox2d unk0;
    s32 unk10;
    s32 unk14;
} Unk80081600;

enum GlobalVisOpcode {
    VISOP_END = 0x00,
    VISOP_PUSH = 0x01,
    VISOP_POP = 0x02,
    VISOP_AND = 0x03,
    VISOP_OR = 0x04,
    VISOP_NOT = 0x05,
    VISOP_XOR = 0x06,
    VISOP_PUSH_IF_ROOM_IN_RANGE = 0x14,
    VISOP_FORCE_VISIBLE = 0x1e,
    VISOP_MATCH_PORTAL_VIS = 0x1f,
    VISOP_ADD_VISIBLE_ROOM = 0x20,
    VISOP_REMOVE_VIS = 0x21,
    VISOP_VISIBLE_IF_SEEN_THROUGH_PORTAL = 0x22,
    VISOP_NOT_VISIBLE_IF_SEEN_THROUGH_PORTAL = 0x23,
    VISOP_DISABLE_ROOM = 0x24,
    VISOP_DISABLE_ROOM_RANGE = 0x25,
    VISOP_PRELOAD_ROOM = 0x26,
    VISOP_PRELOAD_ROOM_RANGE = 0x27,
    VISOP_IF_STATEMENT = 0x50,
    VISOP_DONT_EXEC_COMMANDS_EVEN_ON_RETURN = 0x51,
    VISOP_ENDIF_CONTINUE_EXEC = 0x52,
    VISOP_IF_STATEMENT_PULL_FROM_STACK = 0x5a,
    VISOP_TOGGLE_EXEC_VS_READONLY = 0x5b,
    VISOP_ENDIF = 0x5c
};

extern struct unk_portalstruct table_for_portals[PORTMAX];
extern intptr_t ptr_bgdata_offsets;
extern s32 dword_CODE_bss_8007FF88;
extern s32 *dword_CODE_bss_8007FF90;
extern f32 *dword_CODE_bss_8007FF94;
extern s_bound_info dword_CODE_bss_8007FFA0[];
#ifndef VERSION_EU
extern s32 dword_CODE_bss_8007FF98;
extern s32 dword_CODE_bss_8007FF9C;
#else
extern u32 missingeubytes[4];
extern s32 eu_bss_8007BFA0;
extern s32 eu_bss_8007BFA4;
#endif
extern s32 dword_CODE_bss_800815f0;
extern s32 dword_CODE_bss_800815f4;
extern s32 dword_CODE_bss_800815f8;

#ifdef VERSION_EU
#define BG_PORTAL_QUEUE_LEN 250
#else
#define BG_PORTAL_QUEUE_LEN 500
#endif

// bss
//CODE.bss:8007BF90
intptr_t ptr_bg_data;

//CODE.bss:8007BF94
intptr_t gptr_stan;

/**
 * address 8007BF98
 * EU .bss 80079ee8
*/
s32 dword_CODE_bss_8007BF98;

#ifdef VERSION_EU
s32 eu_bss_80079EEC;
#endif

/**
 * address 8007BFA0
 * EU .bss 80079ef0
*/
#ifdef VERSION_EU
char list_visible_rooms_in_cur_global_vis_packet[0x8c];
#else
char list_visible_rooms_in_cur_global_vis_packet[0x98];
#endif

/**
 * address 8007C038
 * EU .bss 80079f7c
*/
s32 num_visible_rooms_in_cur_global_vis_packet;

/* D:800413F0 Level gCurrentLevel = {0, 1.0, 1.0, 1.0, 1}; cant check this
   anymore however will concede seperate vars since below gets match? */
s32 *ptr_bg_c_debug_debug_notice_list = 0;
//D:800413F4
f32 room_data_float1 = 1.0;
//D:800413F8
f32 room_data_float2 = 1.0;
//D:800413FC Private member - use bgGetLevelVisibilityScale outside this file
f32 mCurrentLevelVisibilityScale = 1.0;
//D:80041400
s32 levelentry_index = 1;

/**
 * Something related to player screen.
 * Maybe x, y, width, height.
 * DefaultScreenXMin = 1; //always 1 (related to getvideosettings) Xmin
 * DefaultScreenYMin = 1; //always 1 (related to getvideosettings)Ymin
 * SubtractFromWidth = -1; //always -1 (related to getvideosettings)
 * SubtractFromHeight = -1; //always -1 (related to getvideosettings)
 * Address 0x80041404.
 */
s32 bgViewRelated[] = { 1, 1, -1, -1 };


/**
 * Array of info about all the rooms on the level
 * Some info is different according to which player is being rendered
 * This array is also why there's a 150 is room limit
 * Canonical name: roominf
 * Address 0x80041414
*/
s_room_info g_BgRoomInfo[MAXROOMCOUNT] = {0};
//D:800442F4 canonically roomnumber
s32 g_MaxNumRooms = MAXROOMCOUNT;

/**
 * Limits the number of rooms that can be loaded per frame.
 * Address: 0x800442F8
 */
s32 g_RoomLoadBudget = 0;

//D:800442FC
u8 D_800442FC[PORTMAX] = {0};
//D:800443C4
u8 D_800443C4[PORTMAX] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};



//D:8004448C
struct levelentry levelinfotable[] = {
/*  levelID;            bg_seg_filename;        bg_stan_filename;      levelscale;  visibility; unknownfloat;*/
    {LEVELID_BUNKER1,  "bg/bg_sev_all_p.seg",  "Tbg_sev_all_p_stanZ",  0.53931433,  1.0,        23.148148},
    {LEVELID_SILO,     "bg/bg_silo_all_p.seg", "Tbg_silo_all_p_stanZ", 0.47256002,  1.0,        29.069},
    {LEVELID_STATUE,   "bg/bg_stat_all_p.seg", "Tbg_stat_all_p_stanZ", 0.107202865, 1.0,        0.0801},
    {LEVELID_CONTROL,  "bg/bg_arec_all_p.seg", "Tbg_arec_all_p_stanZ", 0.49886572,  1.0,        80.645164},
    {LEVELID_ARCHIVES, "bg/bg_arch_all_p.seg", "Tbg_arch_all_p_stanZ", 0.50678575,  1.0,        54.347824},
    {LEVELID_TRAIN,    "bg/bg_tra_all_p.seg",  "Tbg_tra_all_p_stanZ",  0.15019713,  1.0,        19.53125},
    {LEVELID_FRIGATE,  "bg/bg_dest_all_p.seg", "Tbg_dest_all_p_stanZ", 0.44757429,  1.0,        36.764706},
    {LEVELID_BUNKER2,  "bg/bg_sevb_all_p.seg", "Tbg_sevb_all_p_stanZ", 0.53931433,  1.0,        23.148148},
    {LEVELID_AZTEC,    "bg/bg_azt_all_p.seg",  "Tbg_azt_all_p_stanZ",  0.35300568,  1.0,        52.083332},
    {LEVELID_STREETS,  "bg/bg_pete_all_p.seg", "Tbg_pete_all_p_stanZ", 0.34187999,  1.0,        42.372883},
    {LEVELID_DEPOT,    "bg/bg_depo_all_p.seg", "Tbg_depo_all_p_stanZ", 0.21847887,  1.0,        17.605633},
    {LEVELID_COMPLEX,  "bg/bg_ref_all_p.seg",  "Tbg_ref_all_p_stanZ",  0.94285715,  1.0,        37.878788},
    {LEVELID_EGYPT,    "bg/bg_cryp_all_p.seg", "Tbg_cryp_all_p_stanZ", 0.25608,     1.0,        23.584906},
    {LEVELID_DAM,      "bg/bg_dam_all_p.seg",  "Tbg_dam_all_p_stanZ",  0.23363999,  0.2,        100.0},
    {LEVELID_FACILITY, "bg/bg_ark_all_p.seg",  "Tbg_ark_all_p_stanZ",  1.20648,     1.0,        64.102562},
    {LEVELID_RUNWAY,   "bg/bg_run_all_p.seg",  "Tbg_run_all_p_stanZ",  0.089571431, 1.0,        4.5537338},
    {LEVELID_SURFACE,  "bg/bg_sevx_all_p.seg", "Tbg_sevx_all_p_stanZ", 0.45445713,  0.2,        22.603975},
    {LEVELID_JUNGLE,   "bg/bg_jun_all_p.seg",  "Tbg_jun_all_p_stanZ",  0.094662853, 1.0,        6.6844921},
    {LEVELID_TEMPLE,   "bg/bg_dish_all_p.seg", "Tbg_dish_all_p_stanZ", 0.47142857,  1.0,        147.05882},
    {LEVELID_CAVERNS,  "bg/bg_cave_all_p.seg", "Tbg_cave_all_p_stanZ", 0.26824287,  1.0,        13.44086},
    {LEVELID_CITADEL,  "bg/bg_cat_all_p.seg",  "Tbg_cat_all_p_stanZ",  0.76852286,  1.0,        38.461536},
    {LEVELID_CRADLE,   "bg/bg_crad_all_p.seg", "Tbg_crad_all_p_stanZ", 0.23571429,  1.0,        43.103451},
    {LEVELID_SHO,      "bg/bg_sho_all_p.seg",  "Tbg_sho_all_p_stanZ",  0.528,       1.0,        21.18644},
    {LEVELID_SURFACE2, "bg/bg_sevx_all_p.seg", "Tbg_sevx_all_p_stanZ", 0.45445713,  0.2,        22.603975},
    {LEVELID_ELD,      "bg/bg_eld_all_p.seg",  "Tbg_eld_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_BASEMENT, "bg/bg_ame_all_p.seg",  "Tbg_ame_all_p_stanZ",  0.65999997,  1.0,        37.878788},
    {LEVELID_STACK,    "bg/bg_ame_all_p.seg",  "Tbg_ame_all_p_stanZ",  0.65999997,  1.0,        37.878788},
    {LEVELID_LUE,      "bg/bg_lue_all_p.seg",  "Tbg_lue_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_LIBRARY,  "bg/bg_ame_all_p.seg",  "Tbg_ame_all_p_stanZ",  0.65999997,  1.0,        37.878788},
    {LEVELID_RIT,      "bg/bg_rit_all_p.seg",  "Tbg_rit_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_CAVES,    "bg/bg_oat_all_p.seg",  "Tbg_oat_all_p_stanZ",  0.14142857,  1.0,        10.123456},
    {LEVELID_EAR,      "bg/bg_ear_all_p.seg",  "Tbg_ear_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_LEE,      "bg/bg_lee_all_p.seg",  "Tbg_lee_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_LIP,      "bg/bg_lip_all_p.seg",  "Tbg_lip_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_CUBA,     "bg/bg_len_all_p.seg",  "Tbg_len_all_p_stanZ",  0.094662853, 1.0,        6.6844921},
    {LEVELID_WAX,      "bg/bg_wax_all_p.seg",  "Tbg_wax_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_PAM,      "bg/bg_pam_all_p.seg",  "Tbg_pam_all_p_stanZ",  0.94285715,  1.0,        10.123456},
    {LEVELID_MAX,      "bg/bgx.seg",           "TbgxZ",                0.94285715,  1.0,        1.0}
};

//D:8004481C
u32 D_8004481C[] = {0x1000100, 0};

//D:80044824
#ifdef TARGET_N64
s_specialportal specialportalarray[] = {
    {0x03,
        {0x2C,0x2E,0x32, 0x37,0x3E,0x3F,0x4E, 0x56,0x59,0x5D,0x72, 0x76,0x79,0x7A,0xFF}},
    {0x11,
        {0x00,0x3A,0xFF}}
};
#else
/* PORT: gcc rejects initializing a flexible array member in a nested context,
 * so this is a byte array with the identical packed contents (the two
 * variable-length elements back to back, 20 bytes); its only reader walks the
 * data through a (u8 *) cast. */
u8 specialportalarray[] = {
    0x03, 0x2C,0x2E,0x32, 0x37,0x3E,0x3F,0x4E, 0x56,0x59,0x5D,0x72, 0x76,0x79,0x7A,0xFF,
    0x11, 0x00,0x3A,0xFF,
};
#endif

/**
 * Bond's current room.
 * Address 0x80044838
*/
s32 g_BgCurrentRoom = 1;

/**
 * Total number of rooms drawn for the current frame.
 * Address 0x8004483C
*/
s32 g_BgNumberOfRoomsDrawn = 0;

#if defined(VERSION_EU)
s32 eu_cdata_0x1f0d0 = 0;
s32 eu_cdata_0x1f0d4 = 0;
#endif

//D:80044840
Lights1 GlobalLight = gdSPDefLights1(
    150,150,150,        /* ambient color grey */ //D:80044840
    255,255,255,
    77,77,46    /* white light from the upper west-south-west (42 up, 244') */ //D:80044848
);



//D:80044858
s32 D_80044858 = 0;
//D:8004485C
s32 D_8004485C = 1;

// forward declarations

void unload_rooms(void);
Gfx *sub_GAME_7F0B8D78(Gfx *arg0);
Gfx *sub_GAME_7F0B3C8C(Gfx *arg0);
s32 bgCheckIfRoomModelNeedsLoad(s32 roomID);
void bgLoadRoomModelData(s32 room);
void bgBuildRoomVtxBounds(s32 roomID);
Gfx *bgRenderRoomPrimary(Gfx *gdl, s32 room_index);
Gfx *bgRenderRoomSecondary(Gfx *gdl, s32 room_index);

Gfx *bgScissorCurrentPlayerView(Gfx *arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4);

bool bgIsRoomOnScreen(s32 roomID, struct rectbbox *screenbox);
s32 sub_GAME_7F0B39BC(s32 curroom, s32 unk1, bbox2d *screensize, s32 next);
void bgUpdateCurrentPlayerScreenMinMax(void);
void *sub_GAME_7F0B8A24(s32 *pc);
void bgDetermineVisibleRooms(void);
s32 sub_GAME_7F0B5864(s32 portalnum, bbox2d *screenbox);
f32 sub_GAME_7F0B9990(s32 portalnum);
void sub_GAME_7F0B95D8(s32 roomID);
#if defined(VERSION_EU)
void sub_GAME_7F0B7F84(s32 roomnum, s32 portalnum, s32 depth, bbox2d *parentbox);
#endif
void sub_GAME_7F0B4810(f32 arg0);
s32 sub_GAME_7F0B5528(s32 portalnum, f32 scale, coord3d *points);
void bgOrderPortal(s32 portalnum);
/* no upstream declaration exists (IDO implicit-decl'd it); called well
 * before its definition, and GCC treats implicit declaration as an error */
s32 sub_GAME_7F0B9F14(s32 portalnum, coord3d *pos1, coord3d *pos2);

// end forward declarations


void bgInit(void) 
{
    if (0)
    {
        g_BgPortals = NULL;
        ptr_bgdata_offsets = 0;
        dword_CODE_bss_8007FF88 = 0;
        ptr_bgdata_room_fileposition_list = NULL;
        dword_CODE_bss_8007FF90 = NULL;
        dword_CODE_bss_8007FF94 = NULL;
#ifdef VERSION_EU
        dword_CODE_bss_8007FFA0[0].roomid = 0;
        missingeubytes[0] = 0;
        dword_CODE_bss_800815f0 = 0;
        dword_CODE_bss_800815f4 = 0;
        dword_CODE_bss_800815f8 = 0;
        eu_bss_8007BFA0 = 0;
        eu_bss_8007BFA4 = 0;
#else
        dword_CODE_bss_8007FF98 = 0;
        dword_CODE_bss_8007FF9C = 0;
        dword_CODE_bss_8007FFA0[0].roomid = 0;
        dword_CODE_bss_800815f0 = 0;
        dword_CODE_bss_800815f4 = 0;
        dword_CODE_bss_800815f8 = 0;
#endif
    }

    debTryAdd(&ptr_bg_c_debug_debug_notice_list, "bg_c_debug");
}


void sub_GAME_7F0B37EC(void) {
    u8 *ptr;
    u8 *end;
    u8 portal;
    u8 cur;
    u32 masked;

    ptr = (u8 *)specialportalarray;
#ifdef TARGET_N64
    end = (u8 *)&g_BgCurrentRoom;
#else
    end = specialportalarray + sizeof(specialportalarray);
#endif

    do {
        if (levelentry_index == *ptr++) {
            do {
                portal = ptr[0];
                while (ptr[1] >= portal) {
                    g_BgPortals[portal].controlbytes1 |= 2;
                    portal++;
                }

                ptr += 2;
            } while (ptr[0] != 0xff);
        } else {
            do {
                ptr += 2;
            } while (ptr[0] != 0xff);
        }

        ptr++;
    } while ((uintptr_t)ptr < (uintptr_t)end);
}


/*
* Unused function
* Address: 7F0B38B4
*/
void sub_GAME_7F0B38B4(s32 arg0, u8 *arg1) {

    u8 *s0;
    u8 *entry_start;
    s32 v0;
    s32 v1;

    s0 = arg1;
    v0 = *arg1;
    v1 = arg0 & 0xff;

    do {
        entry_start = s0;

        do {
            if (v1 == v0) goto found;
            v0 = *(s0 + 1);
            s0++;
        } while (v0 != 0);

        do { s0++; } while (*s0 != 0);
        s0++;
        goto next_entry;

found:
        s0 = entry_start;
        for (v0 = *entry_start; ; ) {
            if (bgIsRoomOnScreen(v0 ^ 0, &g_CurrentPlayer->screensize)) {
                sub_GAME_7F0B39BC(*s0, 0, &g_CurrentPlayer->screensize, 1);
            }
            v0 = *(s0 + 1);
            s0++;
            if (v0 == 0) break;
        }
        s0++;
        v0 = *s0;
        do {
            if (bgIsRoomOnScreen(v0 ^ 0, &g_CurrentPlayer->screensize)) {
                sub_GAME_7F0B39BC(*s0, 0, &g_CurrentPlayer->screensize, 1);
            }
            v0 = *(s0 + 1);
            s0++;
        } while (v0 != 0);
        return;

next_entry:
        v0 = *s0;
    } while (v0 != 0);

}


/**
 * Address 0x7F0B39BC.
 *
*/
s32 sub_GAME_7F0B39BC(int curroom,int unk1, bbox2d * screensize, s32 next)
{
    int i;
    int temp;

    g_BgRoomInfo[curroom].room_rendered = '\x01';

    if (g_BgRoomInfo[curroom].room_loaded_mask != '\0') {
        return 0;
    }

    // Need g_BgNumberOfRoomsDrawn in a3
    for (i = 0; i < g_BgNumberOfRoomsDrawn; i++)
    {
        if (curroom == dword_CODE_bss_8007FFA0[i].roomid) {
            if (dword_CODE_bss_8007FFA0[i].unk1 < unk1) {
                dword_CODE_bss_8007FFA0[i].unk1 = unk1;
            }
            bgRectOutersect(screensize,&dword_CODE_bss_8007FFA0[i].bbox);
            temp = dword_CODE_bss_8007FFA0[i].next;
            dword_CODE_bss_8007FFA0[i].bbox.min.x = screensize->min.x;
            dword_CODE_bss_8007FFA0[i].bbox.min.y = screensize->min.y;
            dword_CODE_bss_8007FFA0[i].bbox.max.x = screensize->max.x;
            dword_CODE_bss_8007FFA0[i].bbox.max.y = screensize->max.y;
            dword_CODE_bss_8007FFA0[i].next = temp | next;

            return temp;
        }
    }

#if defined(VERSION_EU)
    i = g_BgNumberOfRoomsDrawn;
    if (i >= 0x78) {
        i = 0x77;
    }
    dword_CODE_bss_8007FFA0[i].roomid = curroom;
    dword_CODE_bss_8007FFA0[i].unk1 = unk1;
    dword_CODE_bss_8007FFA0[i].bbox.min.x = screensize->min.x;
    dword_CODE_bss_8007FFA0[i].bbox.min.y = screensize->min.y;
    dword_CODE_bss_8007FFA0[i].bbox.max.x = screensize->max.x;
    dword_CODE_bss_8007FFA0[i].bbox.max.y = screensize->max.y;
    dword_CODE_bss_8007FFA0[i].next = next;
    eu_cdata_0x1f0d0++;
    if (eu_cdata_0x1f0d0 < 0x78) {
        g_BgNumberOfRoomsDrawn = eu_cdata_0x1f0d0;
    }

    return 0;
#else
    i = g_BgNumberOfRoomsDrawn;
    dword_CODE_bss_8007FFA0[i].roomid = curroom;
    dword_CODE_bss_8007FFA0[i].unk1 = unk1;
    dword_CODE_bss_8007FFA0[i].bbox.min.x = screensize->min.x;
    dword_CODE_bss_8007FFA0[i].bbox.min.y = screensize->min.y;
    dword_CODE_bss_8007FFA0[i].bbox.max.x = screensize->max.x;
    dword_CODE_bss_8007FFA0[i].bbox.max.y = screensize->max.y;
    dword_CODE_bss_8007FFA0[i].next = next;
    g_BgNumberOfRoomsDrawn = i + 1;

    if (g_BgNumberOfRoomsDrawn) {}

    return 0;
#endif
}


/*
* Unused function
* Address: ?
*/
void bgZeroPortalsToRoom(s32 roomnum)
{
  g_BgRoomInfo[roomnum].portal_visit_count = 0;
}


/*
* Unused function
* Address: 7F0B3B20
*/
s32 bgFindFirstPortalVisitedRoom(void)
{
    s32 i;

    for (i=0;i<MAXROOMCOUNT;i++)
    {
        if (g_BgRoomInfo[i].portal_visit_count) {
            return i;
        };
    }
    return -1;
}


/**
 * Address: 7F0B3BC4
 */
void bgResetPortalVisitCounts(void)
{
  s32 i;

  g_BgNumberOfRoomsDrawn = 0;
#ifdef VERSION_EU
  eu_cdata_0x1f0d0 = 0;
#endif
  i = 0;
  while (i != MAXROOMCOUNT)
  {
    g_BgRoomInfo[i].portal_visit_count = 0;
    i++;
  }
}


/**
 * Searches dword_CODE_bss_8007FFA0 for matching id.
 * If found, sets result to 2d bbox and returns 1.
 * Otherwise result is set to empty 2d bbox and returns 0.
 *
 * @param room_id: room id to search for.
 * @param result: Out parameter. Contains result 2d bbox.
 *
 * Address 0x7F0B3C0C.
*/
s32 bgGet2dBboxByRoomId(s32 room_id, struct bbox2d *result)
{
    s32 i;
    for (i=0; i<g_BgNumberOfRoomsDrawn; i++)
    {
        if (room_id == dword_CODE_bss_8007FFA0[i].roomid)
        {
            result->f[0][0] = dword_CODE_bss_8007FFA0[i].bbox.f[0][0];
            result->f[0][1] = dword_CODE_bss_8007FFA0[i].bbox.f[0][1];
            result->f[1][0] = dword_CODE_bss_8007FFA0[i].bbox.f[1][0];
            result->f[1][1] = dword_CODE_bss_8007FFA0[i].bbox.f[1][1];

            return 1;
        }
    }

    // It's pointless to set those because when this function returns false, result is unused
    result->f[0][0] = 0.0f;
    result->f[0][1] = 0.0f;
    result->f[1][0] = 0.0f;
    result->f[1][1] = 0.0f;

    return 0;
}


/**
 * Address: 7F0B3C8C
 */
#ifndef TARGET_N64
#define PORT_DXLU_GROUP(gdl, mode) do { (gdl)->words.w0 = (u32) 0x49 << 24 /* G_DXLU_GROUP_EXT */; (gdl)->words.w1 = (mode); (gdl)++; } while (0)
#else
#define PORT_DXLU_GROUP(gdl, mode) do { } while (0)
#endif

Gfx *sub_GAME_7F0B3C8C(Gfx *gdl)
{
#ifdef VERSION_EU
    s16 i;
#else
    s32 i;
#endif
    s32 j;
#ifndef TARGET_N64
    /* GE_ROOMLIST=1 (diag): once per second, the visible
     * room set the portal walk produced - id, portal-hop bucket, screen
     * bbox. Diff default vs GE_ALLROOMS=1 to name a wrongly culled room. */
    {
        extern char *getenv(const char *);
        static s32 rl = -1;
        if (rl < 0) { rl = getenv("GE_ROOMLIST") != NULL; }
        if (rl && (g_GlobalTimer % 60) == 0)
        {
            s32 k;
            printf("roomlist: n=%d:", g_BgNumberOfRoomsDrawn);
            for (k = 0; k < g_BgNumberOfRoomsDrawn; k++)
            {
                printf(" %d(b%d %.0f,%.0f-%.0f,%.0f)", dword_CODE_bss_8007FFA0[k].roomid,
                       dword_CODE_bss_8007FFA0[k].unk1,
                       dword_CODE_bss_8007FFA0[k].bbox.min.x, dword_CODE_bss_8007FFA0[k].bbox.min.y,
                       dword_CODE_bss_8007FFA0[k].bbox.max.x, dword_CODE_bss_8007FFA0[k].bbox.max.y);
            }
            printf("\n");
        }
    }
#endif
#ifdef VERSION_EU
    s16 b_max;
    s16 b_min;
#else
    s32 b_max;
    s32 b_min;
#endif
    s32 notdone;
#ifdef VERSION_EU
    b_max = 0;
    b_min = 32767;
#else
    b_min = 99999999;
    b_max = 0;
#endif
 
    for (j = 0; j < g_BgNumberOfRoomsDrawn; j++)
    {
        if (b_max < dword_CODE_bss_8007FFA0[j].unk1)
        {
            b_max = dword_CODE_bss_8007FFA0[j].unk1;
        }
 
        if (dword_CODE_bss_8007FFA0[j].unk1 < b_min)
        {
            b_min=dword_CODE_bss_8007FFA0[j].unk1;
        }
    }
 
    for (i = b_min; i <= b_max; i++)
    {
    #ifdef DEBUG
        notdone = g_BgNumberOfRoomsDrawn;
    #endif
        for (j = 0; j < g_BgNumberOfRoomsDrawn; j++)
        {
            if (i == dword_CODE_bss_8007FFA0[j].unk1)
            {
                gSPMatrix(gdl++, osVirtualToPhysical((void*)currentPlayerGetProjectionMatrix()), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
                gdl = fogRenderClearFogMode(gdl);
 
                if (get_debug_do_draw_obj())
                {
                    if (sub_GAME_7F0BD8F0())
                    {
                        gdl = chrpropsRenderPass(gdl, dword_CODE_bss_8007FFA0[j].roomid, 0);
                    }
                }
 
                gSPMatrix(gdl++, osVirtualToPhysical((void*)get_BONDdata_field_10E0()), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
                gdl = fogSetRenderFogColor(
                    bgScissorCurrentPlayerViewF(
                        gdl++,
                        dword_CODE_bss_8007FFA0[j].bbox.min.x,
                        dword_CODE_bss_8007FFA0[j].bbox.min.y,
                        dword_CODE_bss_8007FFA0[j].bbox.max.x,
                        dword_CODE_bss_8007FFA0[j].bbox.max.y),
                    0);
 
                if (get_debug_do_draw_bg())
                {
                    if (sub_GAME_7F0BD8F0())
                    {
                        gdl = bgRenderRoomPrimary(gdl, dword_CODE_bss_8007FFA0[j].roomid);
                    }
                }
 
                gSPMatrix(gdl++, osVirtualToPhysical((void*)currentPlayerGetProjectionMatrix()), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
                gdl = fogRenderClearFogMode(gdl);
 
                if (get_debug_do_draw_obj())
                {
                    if (sub_GAME_7F0BD8F0())
                    {
                        gdl = chrpropsRenderPass(gdl, dword_CODE_bss_8007FFA0[j].roomid, 2);
                    }
                }
 
                if (1);
            }
    #ifdef DEBUG
            notdone--;
    #endif
        }
    }
    #ifdef DEBUG
    assert(notdone == 0);
    #endif
 
    gdl = bgScissorCurrentPlayerViewDefault(fogRenderClearFogMode(gdl));
    gSPMatrix(gdl++, osVirtualToPhysical((void*)get_BONDdata_field_10E0()), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
 
    if (sub_GAME_7F0BD8F0())
    {
        /* PORT: scorch marks and bg bullet impacts are decals glued to opaque
         * geometry, and vanilla draws them here, before the room XLU pass and
         * prop alpha pass. Mode 4 pins them first in the deferred transparency
         * sort, otherwise an explosion at the decal's surface would sort
         * farther and the mark would paint over the fireball. */
        PORT_DXLU_GROUP(gdl, 4);
        gdl = explosionRenderScorchBuffer(gdl);
        gdl = explosionCallRenderBulletImpactOnProp(gdl);
        PORT_DXLU_GROUP(gdl, 0);
    }
 
    if (g_BgNumberOfRoomsDrawn);
    if (dword_CODE_bss_8007FFA0);
 
#ifndef TARGET_N64
    /* PC: the transparent pass needs true far-to-near order. On N64 same-depth
     * rooms rely on the RDP's coverage system to limit out-of-order XLU
     * overlap, which the port's blender does not emulate, so ties on unk1 (the
     * authored portal-hop depth, still the primary key) are broken by camera
     * distance to the room centre, farthest first. */
    {
        extern PropRecord *getCurrentPlayerProp(void);
        s32 order[128];
        f32 dist[128];
        s32 n = g_BgNumberOfRoomsDrawn;
        s32 k;
        PropRecord *plr = getCurrentPlayerProp();

        if (n > 128)
        {
            n = 128;
        }

        for (j = 0; j < n; j++)
        {
            coord3d c;
            order[j] = j;
            bgGetRoomCenter(dword_CODE_bss_8007FFA0[j].roomid, &c);
            if (plr != NULL)
            {
                f32 dx = c.x - plr->pos.x;
                f32 dy = c.y - plr->pos.y;
                f32 dz = c.z - plr->pos.z;
                dist[j] = dx * dx + dy * dy + dz * dz;
            }
            else
            {
                dist[j] = 0.0f;
            }
        }

        /* insertion sort: unk1 DESC, then dist DESC */
        for (j = 1; j < n; j++)
        {
            s32 v = order[j];
            s32 vd = dword_CODE_bss_8007FFA0[v].unk1;
            f32 vf = dist[v];
            for (k = j - 1; k >= 0; k--)
            {
                s32 u = order[k];
                if ((dword_CODE_bss_8007FFA0[u].unk1 > vd)
                    || (dword_CODE_bss_8007FFA0[u].unk1 == vd && dist[u] >= vf))
                {
                    break;
                }
                order[k + 1] = u;
            }
            order[k + 1] = v;
        }

        for (k = 0; k < n; k++)
        {
            j = order[k];
            {
#else
    for (i = b_max; i >= b_min; i--)
    {
    #ifdef DEBUG
        notdone = g_BgNumberOfRoomsDrawn;
    #endif

        for (j = 0; j < g_BgNumberOfRoomsDrawn; j++)
        {
            if (i == dword_CODE_bss_8007FFA0[j].unk1)
            {
#endif
                gSPMatrix(gdl++, osVirtualToPhysical((void*)get_BONDdata_field_10E0()), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
                gdl = fogSetRenderFogColor(
                    bgScissorCurrentPlayerViewF(
                        gdl++,
                        dword_CODE_bss_8007FFA0[j].bbox.min.x,
                        dword_CODE_bss_8007FFA0[j].bbox.min.y,
                        dword_CODE_bss_8007FFA0[j].bbox.max.x,
                        dword_CODE_bss_8007FFA0[j].bbox.max.y),
                    1);

                /* PORT: room-block scope for the deferred XLU sort (mode 5
                 * begin / 6 end): this room's XLU list and its prop alpha
                 * passes share a scope, and the sorter keeps their authored
                 * order whatever the depth keys say (see gfx_pc.cpp
                 * gfx_dxlu_authored_pair). */
                PORT_DXLU_GROUP(gdl, 5);
                if (get_debug_do_draw_bg())
                {
                    if (sub_GAME_7F0BD8F0())
                    {
                        /* PORT: marks this room's XLU list as a sort unit
                         * (renderer G_DXLU_GROUP_EXT mode 3), so rooms sort
                         * against each other by mean distance and per-triangle
                         * inside the room. GE_XLU_PERTRI=1 ignores the marker;
                         * see gfx_pc.cpp and docs/port/xlu-sort-groups.md. */
                        PORT_DXLU_GROUP(gdl, 3);
                        gdl = bgRenderRoomSecondary(gdl, dword_CODE_bss_8007FFA0[j].roomid);
                        PORT_DXLU_GROUP(gdl, 0);
                    }
                }

                gSPMatrix(gdl++, osVirtualToPhysical((void*)currentPlayerGetProjectionMatrix()), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
                gdl = fogRenderClearFogMode(gdl);

                if (get_debug_do_draw_obj())
                {
                    if (sub_GAME_7F0BD8F0())
                    {
                        gdl = chrpropsRenderPass(gdl, dword_CODE_bss_8007FFA0[j].roomid, 1);
                    }
                }
                PORT_DXLU_GROUP(gdl, 6);

                if (1);
            }
#ifndef TARGET_N64
        }
    }
#else
    #ifdef DEBUG
            notdone--;
    #endif
        }
    }
    #ifdef DEBUG
    assert(notdone == 0);
    #endif
#endif

    return gdl;
}


/*
 * Address: 0x7F0B4034
*/
s32 getPriMappingBinCount(s32 room)
{
    s32 i = room;

    while (ptr_bgdata_room_fileposition_list[i].pPriMappingBin == 0)
    {
        i++;
    }

    return i;
}


/*
 * Address: 0x7F0B4084
*/
s32 getSecMappingBinCount(s32 room)
{
    s32 i = room;

    while (ptr_bgdata_room_fileposition_list[i].pSecMappingBin == 0)
    {
        i++;
    }

    return i;
}


/*
 * Address: 0x7F0B40D4
*/
s32 getPointTableBinCount(s32 room)
{
    s32 i = room;
    
    while (ptr_bgdata_room_fileposition_list[i].pPointTableBin == 0)
    {
        i++;
    }

    return i;
}


#ifndef TARGET_N64
/* PC: the generated bg files emit each 8-byte global-visibility command as two
 * u32 literals with the N64 big-endian byte image baked into the first word
 * (type in the top byte). Read natively on a little-endian host the type lands
 * in the wrong byte, every list looked like VISOP_END and the whole script
 * silently did nothing (Frigate's ship exterior only draws through it). Swap
 * the packed word to native order once per list; the arg word already reads
 * natively, and the compiled-in data persists across stage reloads, so the swap
 * must not be repeated. */
static void portSwapGlobalVisCommands(u32 *w)
{
    static u32 *swappedlists[STAGES_MAX];
    static s32 swappedcount = 0;
    u32 word;
    s32 i;

    for (i = 0; i < swappedcount; i++)
    {
        if (swappedlists[i] == w)
        {
            return;
        }
    }

    if (swappedcount >= STAGES_MAX)
    {
        return; /* registry full (cannot happen: one list per bg file); do nothing rather than risk a double swap */
    }

    swappedlists[swappedcount++] = w;

    do
    {
        word = *w;
        *w = (word >> 24) | ((word >> 8) & 0xff00) | ((word & 0xff00) << 8) | (word << 24);
        w += 2;
    } while ((word >> 24) != 0); /* the type-0 terminator entry ends the list */
}


/* PC: the generated bg files (bg_all_p.h) declare the portal record's two
 * control bytes as one native u16 (the ROM's [controlbytes1][controlbytes2]
 * pair as a literal). Read back through the game's { u8 controlbytes1; u8
 * controlbytes2; } overlay the two bytes land exchanged, so the authored
 * traversal slack read as 0 on every portal and planar portals became knife
 * edges (sky drawn through the world beyond a doorway). Swap each pair once per
 * list; the compiled-in data persists across stage reloads. */
static void portSwapPortalControlBytes(bg_portal_data_entry *list)
{
    static bg_portal_data_entry *swappedlists[STAGES_MAX];
    static s32 swappedcount = 0;
    s32 i;

    for (i = 0; i < swappedcount; i++)
    {
        if (swappedlists[i] == list)
        {
            return;
        }
    }

    if (swappedcount >= STAGES_MAX)
    {
        return; /* registry full (cannot happen: one list per bg file) */
    }

    swappedlists[swappedcount++] = list;

    for (i = 0; list[i].offset_portal != NULL; i++)
    {
        u8 t = list[i].controlbytes1;
        list[i].controlbytes1 = list[i].controlbytes2;
        list[i].controlbytes2 = t;
    }
}
#endif


/*
 * Address: 0x7F0B4124
*/
void load_bg_file(LEVEL_INDEX levelid)
{
    typedef struct bg_envdata_entry_local {
        u8 type;
        u8 pad[3];
        s32 data;
    } bg_envdata_entry_local;
    s32 i;
    s32 size;
    s32 header[0x10];
    s32 *data;
 
    levelentry_index = 0;

    for (i = 0; i < MAXROOMCOUNT; i++) 
    {
        g_BgRoomInfo[i].vtx_batch_bounds = NULL;
    }
 
    for (i = 0; i < STAGES_MAX; i++)
    {
        if (levelinfotable[i].levelID == levelid)
        {
            levelentry_index = i;
        }
    }
 
    lightFixtureInitTables();
 
#ifdef TARGET_N64
    ptr_bg_data = (s32)header;
    obLoadBGFileBytesAtOffset(levelinfotable[levelentry_index].bg_seg_filename, (u8 *) ptr_bg_data, 0, 0x40);

    if (((levelid && ptr_bg_data) && levelentry_index));

    ptr_bgdata_offsets = ptr_bg_data;
    ptr_bgdata_room_fileposition_list = (bg_room_data *) BG_SEG_TO_PTR(ptr_bg_data, ((s32 *)ptr_bg_data)[1]);

    size = (((((u32) ptr_bgdata_room_fileposition_list[1].pPointTableBin) & 0x00ffffff) - 1) | 0xf) + 1;

    ptr_bg_data = (s32) mempAllocBytesInBank(size, 4);
    obLoadBGFileBytesAtOffset(levelinfotable[levelentry_index].bg_seg_filename, (u8 *) ptr_bg_data, 0, size);
#else
    /* PC: the compiled-in bg file is typed C with real pointers, so the N64
     * size-and-copy step does not apply. Use the linked data in place, since
     * BG_SEG_TO_PTR passes pointers through on PC. */
    {
        extern u8 *obGetBGFileData(u8 *bgname);
        extern s32 romdataFileServed(s32 index);
        extern s32 romfileBgConvert(u8 *data, u32 size, const char *name);
        extern u32 obGetFileRomSize(s32 index);
        char *bgname = levelinfotable[levelentry_index].bg_seg_filename;
        s32 bgidx = fileGetIndex((u8 *) bgname);
        (void) size;
        (void) header;
        ptr_bg_data = (intptr_t) obGetBGFileData((u8 *) bgname);
        {
            extern void portHwNoteBgHeader(s32 bgidx);
            portHwNoteBgHeader(bgidx); /* tickhz=hw: the N64's two header reads (frametiming.c) */
        }
        /* ROM loading: a served bg is the cart's big-endian N64 image, so
         * convert its uncompressed front matter to host layout in place before
         * anything reads the header. Room geometry is handled by the
         * room-streaming path and segment offsets are resolved by BG_SEG_TO_PTR
         * as on N64. */
        if (romdataFileServed(bgidx)) {
            romfileBgConvert((u8 *) ptr_bg_data, obGetFileRomSize(bgidx), bgname);
        }
        ptr_bgdata_offsets = ptr_bg_data;
        ptr_bgdata_room_fileposition_list =
            (bg_room_data *) BG_SEG_TO_PTR(ptr_bg_data, ((s32 *) ptr_bg_data)[1]);
#ifdef GE_NATIVE_64
        ptr_bgdata_room_fileposition_list = romfileBgNativeRooms((void *)ptr_bg_data);
#endif
    }
#endif
 
    gptr_stan = (intptr_t) _fileNameLoadToBank(levelinfotable[levelentry_index].bg_stan_filename, 2, 0, 4);
#ifdef GE_NATIVE_64
    {
        extern struct StanPrefixRecord *romfileStanNativePrefix(u8 *data);
        gptr_stan = (intptr_t)romfileStanNativePrefix((u8 *)gptr_stan);
    }
#endif


#ifndef TARGET_N64
    { /* ROM loading Phase D: tell stan.c whether this stan came off the
       * cart (already packed, real pointers) or is the compiled-in file */
        extern s32 g_portStanFromRom;
        extern s32 romdataFileServed(s32 index);
        g_portStanFromRom = romdataFileServed(fileGetIndex((u8 *) levelinfotable[levelentry_index].bg_stan_filename));
    }
    { /* bring-up (sanitizer sweep): a stage whose stan asset is missing
       * hands stanDetermineEOF garbage - identify the stage FIRST */
        printf("ge: load_bg_file levelid=%d stan=%s hdr=%p first=%08x\n",
               levelid, levelinfotable[levelentry_index].bg_stan_filename,
               (void *) gptr_stan, gptr_stan ? ((u32 *) gptr_stan)[1] : 0);
    }
#endif

    stanDetermineEOF((struct StanPrefixRecord *) gptr_stan, 0, (u8 *) gptr_stan);
    stanLoadFile((struct StanPrefixRecord *) gptr_stan);
 
    sub_GAME_7F0B4810(levelinfotable[levelentry_index].levelscale);
    setLevelScale(levelinfotable[levelentry_index].levelscale);
    setDebugCameraScale(levelinfotable[levelentry_index].levelscale);
    chrRemoved7F022E1C(levelinfotable[levelentry_index].levelscale);
 
    mCurrentLevelVisibilityScale = levelinfotable[levelentry_index].visibility;
 
    sub_GAME_7F08976C(mCurrentLevelVisibilityScale);
    matrix_4x4_7F058C4C(mCurrentLevelVisibilityScale);
 
    data = (s32 *)ptr_bg_data;
    dword_CODE_bss_8007BF98 = *data;
    dword_CODE_bss_8007FF88 = 1;
 
    if (dword_CODE_bss_8007BF98 == 0)
    {
        dword_CODE_bss_8007FF88 = 2;
        ptr_bgdata_offsets = (intptr_t)data;
        ptr_bgdata_room_fileposition_list = (bg_room_data *) BG_SEG_TO_PTR(data, ((s32 *)ptr_bgdata_offsets)[1]);
#ifdef GE_NATIVE_64
        ptr_bgdata_room_fileposition_list = romfileBgNativeRooms(data);
#endif
        
        // Keep this fake goto for matching.
        goto dummy_label_543534; dummy_label_543534: ;
 
        g_MaxNumRooms = 0;

        for (i = 1; ptr_bgdata_room_fileposition_list[i].pPriMappingBin != NULL; i++) 
        {
            g_MaxNumRooms++;  
        }
 
        g_BgPortals = (bg_portal_data_entry *) BG_SEG_TO_PTR(data, ((s32 *)ptr_bgdata_offsets)[2]);
#ifdef GE_NATIVE_64
        g_BgPortals = romfileBgNativePortals(data);
#endif

        if (1);

        if (((s32 *)ptr_bgdata_offsets)[3] == 0)
        {
            dword_CODE_bss_8007FF90 = 0;
        }
        else
        {
            dword_CODE_bss_8007FF90 = (s32 *) BG_SEG_TO_PTR(data, ((s32 *)ptr_bgdata_offsets)[3]);

#ifndef TARGET_N64
            /* ROM loading Phase F: a served bg was byteswapped to native by
             * romfileBgConvert (which handled the vis command args), so this
             * getools-artifact fixup must NOT run on it - it would re-swap */
            {
                extern s32 romdataFileServed(s32 index);
                if (!romdataFileServed(fileGetIndex((u8 *) levelinfotable[levelentry_index].bg_seg_filename))) {
                    portSwapGlobalVisCommands((u32 *) dword_CODE_bss_8007FF90);
                }
            }
#endif

            if (((s32 *)ptr_bgdata_offsets)[4] == 0)
            {
                dword_CODE_bss_8007FF94 = NULL;
            }
            else
            {
                dword_CODE_bss_8007FF94 = (f32 *) BG_SEG_TO_PTR(data, ((s32 *)ptr_bgdata_offsets)[4]);
            }
        }
 
        for (i = 0; g_BgPortals[i].offset_portal != (NULL); i++)
        {
            g_BgPortals[i].offset_portal = (bg_portal_entry *) BG_SEG_TO_PTR(ptr_bg_data, g_BgPortals[i].offset_portal);
        }

#ifndef TARGET_N64
        /* served bg: the control-byte pair is already the cart's runtime order
         * (romfileBgConvert left it untouched), so skip the compiled-only swap */
        {
            extern s32 romdataFileServed(s32 index);
            if (!romdataFileServed(fileGetIndex((u8 *) levelinfotable[levelentry_index].bg_seg_filename))) {
                portSwapPortalControlBytes(g_BgPortals);
            }
        }
#endif

        if (dword_CODE_bss_8007FF90 != NULL)
        {
            for (i = 0; ((bg_envdata_entry_local *)dword_CODE_bss_8007FF90)[i].type != 0; i++)
            {
                if (((bg_envdata_entry_local *)dword_CODE_bss_8007FF90)[i].type == ENVIRONMENTDATA_ALT)
                {
#ifndef TARGET_N64
                    /* PORT: the compiled-in list persists across stage reloads
                     * and this walk converts in place, so an already-converted
                     * entry (a portal index below PORTMAX) must be skipped or a
                     * replay would mangle it through the pointer walk. */
                    if ((u32) ((bg_envdata_entry_local *)dword_CODE_bss_8007FF90)[i].data < PORTMAX)
                    {
                        continue;
                    }
#endif
                    ((bg_envdata_entry_local *)dword_CODE_bss_8007FF90)[i].data = getIndexOfPORTALID((intptr_t) BG_SEG_TO_PTR(ptr_bg_data, ((bg_envdata_entry_local *)dword_CODE_bss_8007FF90)[i].data));
                }
            }
        }
 
        for (i = 1; i < g_MaxNumRooms; i++)
        {
            g_BgRoomInfo[i].model_bin_loaded = 0;
            g_BgRoomInfo[i].field_35 = 0;
 
            if (ptr_bgdata_room_fileposition_list[i].pPriMappingBin != (NULL))
            {
                s32 primaryindex;
                s32 secondaryindex;
                primaryindex = getPriMappingBinCount(i + 1);
                secondaryindex = getSecMappingBinCount(i);
 
                if (primaryindex <= secondaryindex)
                {
                    g_BgRoomInfo[i].csize_primary_DL_binary = ((s32) ptr_bgdata_room_fileposition_list[primaryindex].pPriMappingBin) - ((s32) ptr_bgdata_room_fileposition_list[i].pPriMappingBin);
                }
                else
                {
                    g_BgRoomInfo[i].csize_primary_DL_binary = ((s32) ptr_bgdata_room_fileposition_list[secondaryindex].pSecMappingBin) - ((s32) ptr_bgdata_room_fileposition_list[i].pPriMappingBin);
                }
            }
            else
            {
                g_BgRoomInfo[i].csize_primary_DL_binary = 0;
            }
 
            if (ptr_bgdata_room_fileposition_list[i].pSecMappingBin != (NULL))
            {
                s32 primaryindex;
                s32 secondaryindex;
                primaryindex = getPriMappingBinCount(i + 1);
                secondaryindex = getSecMappingBinCount(i + 1);
 
                if (primaryindex <= secondaryindex)
                {
                    g_BgRoomInfo[i].csize_secondary_DL_binary = ((s32) ptr_bgdata_room_fileposition_list[primaryindex].pPriMappingBin) - ((s32) ptr_bgdata_room_fileposition_list[i].pSecMappingBin);
                }
                else
                {
                    g_BgRoomInfo[i].csize_secondary_DL_binary = ((s32) ptr_bgdata_room_fileposition_list[secondaryindex].pSecMappingBin) - ((s32) ptr_bgdata_room_fileposition_list[i].pSecMappingBin);
                }
            }
            else
            {
                g_BgRoomInfo[i].csize_secondary_DL_binary = 0;
            }
 
            if (ptr_bgdata_room_fileposition_list[i].pPointTableBin != (NULL))
            {
                s32 pointindex;
                pointindex = getPointTableBinCount(i + 1);
                g_BgRoomInfo[i].csize_point_index_binary = ((s32) ptr_bgdata_room_fileposition_list[pointindex].pPointTableBin) - ((s32) ptr_bgdata_room_fileposition_list[i].pPointTableBin);
            }
            else
            {
                g_BgRoomInfo[i].csize_point_index_binary = 0;
            }
 
            g_BgRoomInfo[i].cur_room_totalsize = -1;
        }
 
        initializeRoomData();
 
        for (i = 1; i < g_MaxNumRooms; i++)
        {
            bgRoomCalcBB(i);
        }

#ifndef TARGET_N64
        {
            extern void portBgCalcLevelFloor(void);
            portBgCalcLevelFloor(); /* every room's box is measured by here */
        }
#endif

        for (i = 0; g_BgPortals[i].offset_portal != (NULL); i++)
        {
            D_800443C4[i] = sub_GAME_7F0B993C(i);
        }
 
        for (i = 0; g_BgPortals[i].offset_portal != (NULL); i++)
        {
            bgOrderPortal(i);
        }
 
        for (i = 0; i < g_MaxNumRooms; i++)
        {
            sub_GAME_7F0B95D8(i);
        }
 
        for (i = 0; g_BgPortals[i].offset_portal != (NULL); i++)
        {
            g_BgPortals[i].controlbytes1 &= 0xfe;
        }
 
        sub_GAME_7F0B37EC();
    }
 
    fogRemoved7F0BAA5C(levelid);
    g_RoomLoadBudget = 200;
}


void cleanup_rooms(void) {
    unload_rooms();
    matrix_4x4_7F058C4C(1.0);
}


void sub_GAME_7F0B4810(f32 arg0) {
    room_data_float1 = arg0;
    room_data_float2 = (f32) (1.0f / arg0);
}


f32 get_room_data_float2(void){
  return room_data_float2;
}


f32 get_room_data_float1(void){
  return room_data_float1;
}


f32 sub_GAME_7F0B4848(void)
{
    return levelinfotable[levelentry_index].unknownfloat / levelinfotable[levelentry_index].levelscale;
}


//sub_GAME_7F0B4878
f32 bgGetLevelVisibilityScale(void) {
    return mCurrentLevelVisibilityScale;
}


// defined later in the same file
extern void bgRoomsTickUnload(void);

void bgRoomVisibilityRelated(void)
{
    bg_portal_data_entry *portal;
    bg_portal_entry *next;
    coord3d *pos;
    coord3d *pos3;
    u8 *portalflags;
    s32 room;
    s32 portalnum;
    s32 lastportal;
    s32 depth;
    s32 maxdepth;
    s32 offset;
    s32 cammode;

    lastportal = -1;

    num_visible_rooms_in_cur_global_vis_packet = 0;

    if (get_player_position_in_shuffled(get_cur_playernum()) == 0) {
        bgRoomsTickUnload();
    }

    cammode = bondviewGetCameraMode();

#ifndef TARGET_N64
    /* The 3-room budget throttled N64 cart DMA per frame. On PC loads are cheap
     * and in 4P the shared budget starved whoever drew last, leaving freshly
     * entered rooms empty, so always use the generous cutscene budget. */
    g_RoomLoadBudget = 0xc8;
#else
    g_RoomLoadBudget = 3;
#endif

    switch (cammode) {
    case CAMERAMODE_INTRO:
    case CAMERAMODE_FADESWIRL:
    case CAMERAMODE_SWIRL:
    case CAMERAMODE_POSEND:
    case CAMERAMODE_MP:
        g_RoomLoadBudget = 0xc8;
        break;
    case CAMERAMODE_FP:
    case CAMERAMODE_DEATH_CAM_SP:
    case CAMERAMODE_DEATH_CAM_MP:
    case CAMERAMODE_FP_NOINPUT:
        break;
    }

    room = bondviewGetCurrentPlayersRoom();
    g_BgCurrentRoom = room;

    pos = bondviewGetCurrentPlayersPosition();
    pos3 = bondviewGetCurrentPlayersPosition3();

    // FAKE
    if (1) {}

    for (depth = 0, maxdepth = 11; depth != maxdepth; depth++) {
        for (portalnum = 0; g_BgPortals[portalnum].offset_portal != NULL; portalnum++) {

            if (D_800443C4[portalnum] != 0) {
                continue;
            }
            if (portalnum == lastportal) {
                continue;
            }

            if (((room == g_BgPortals[portalnum].connectedRoom1 || room == g_BgPortals[portalnum].connectedRoom2) && sub_GAME_7F0B9F14(portalnum, pos, pos3))) {
                lastportal = portalnum;

                room = (room ^ g_BgPortals[portalnum].connectedRoom1) ^ g_BgPortals[portalnum].connectedRoom2;

                break;
            }
        }

        if (g_BgPortals[portalnum].offset_portal == NULL) {
            break;
        }
    }

    g_BgCurrentRoom = room;
    bgDetermineVisibleRooms();

#ifndef TARGET_N64
    { /* GE_ROOM_AUDIT debug: watch each player's visibility result (is the seed
       * room in the draw list, script-blocked, loaded, and consistent with the
       * player's tracked room). */
        extern s32 portRoomAuditEnabled(void);
        extern u32 portRoomAuditGetFrame(void);
        if (portRoomAuditEnabled()) {
            u32 frame = portRoomAuditGetFrame();
            s32 pl = get_cur_playernum();
            s32 inlist = 0;
            s32 i;
            static u32 lastBad[4];
            static u32 lastSummary[4];

            for (i = 0; i < g_BgNumberOfRoomsDrawn; i++) {
                if (dword_CODE_bss_8007FFA0[i].roomid == room) {
                    inlist = 1;
                    break;
                }
            }

            if ((!inlist || g_BgRoomInfo[room].room_loaded_mask != 0) &&
                frame - lastBad[pl & 3] >= 30u) {
                lastBad[pl & 3] = frame;
                fprintf(stderr,
                        "roomaudit: *** VIS MISS p%d seed=%d curidx=%d inlist=%d mask=%d loaded=%d listlen=%d f=%u\n",
                        pl, room, g_CurrentPlayer->curRoomIndex, inlist,
                        g_BgRoomInfo[room].room_loaded_mask,
                        g_BgRoomInfo[room].model_bin_loaded,
                        g_BgNumberOfRoomsDrawn, frame);
                fflush(stderr);
            }

            if (frame - lastSummary[pl & 3] >= 120u) {
                lastSummary[pl & 3] = frame;
                fprintf(stderr,
                        "roomaudit: vis p%d seed=%d curidx=%d listlen=%d loaded=%d f=%u\n",
                        pl, room, g_CurrentPlayer->curRoomIndex,
                        g_BgNumberOfRoomsDrawn,
                        g_BgRoomInfo[room].model_bin_loaded, frame);
                fflush(stderr);
            }
        }
    }
#endif
}



void addToByteSetMaxSize15(u8* set, u8 newElement) {
    s32 i = 0;

    while (i < 0x10 && set[i] != 0xFF) {
        if (newElement == set[i]) {
            return;
        }
        i++;
    }

    if (i < 0xF) {
        set[i] = newElement;
        set[i+1] = -1;
    }
    return;
}


#ifndef TARGET_N64
/**
 * ROOM TRACKING ON LEVELS WITH NO PORTALS (Cradle and friends).
 *
 * bgFindRoomsAlongSegment finds the rooms a projectile crosses by walking
 * PORTALS out from the rooms it was in. Every one of its loops is bounded by
 * `g_BgPortals[n].offset_portal != 0`, so on a level whose bg ships no
 * portals at all the whole traversal is a no-op: `finalRooms` comes back as
 * the seed it went in with and `traversedRooms` holds that one room. The
 * caller (propobj.c) then collision-tests the projectile against that room's
 * geometry ONLY - so a rocket or grenade crossing into any other room meets
 * no walls and no floor and sails straight through. Measured: Cradle reports
 * `portals=0 rooms=37`, against 194 portals on Dam and 103 on Archives.
 *
 * The fallback is the geometric question the portals were answering: which
 * rooms does this segment actually touch? Each room carries a world-space
 * AABB in g_BgRoomInfo[].minbounds/maxbounds, so we can just test them.
 *
 * SPACE: room bounds are in `world * room_data_float1`, the same space
 * sub_GAME_7F0BA2D4 scales its query bbox into. Verified by probe rather
 * than assumed - the player's spawn point, times room_data_float1, lands
 * inside its own stan room's box on all three axes on Cradle (room 35),
 * Dam (135) and Archives (14), while `* room_data_float2` is out by an
 * order of magnitude. (chrpropFlagRoomsFromRayTest folds in an extra
 * bgGetLevelVisibilityScale(); that is a red herring - it is 0.2 on Dam,
 * where the unscaled factor is what checks out, and that function is marked
 * Unreferenced.)
 *
 * Two different answers come out of one scan, because the caller wants two
 * different things:
 *   traversedRooms - every room the swept segment's AABB touches. This is
 *       the collision-test set, and it is the half that actually stops the
 *       projectile.
 *   finalRooms     - the rooms containing the segment's END point, i.e.
 *       where the projectile is NOW. It becomes projectile->unkCC (next
 *       frame's seed) and is what propobj.c hands stanFindTileBelowPos to
 *       find the floor under a grenade, so it must be "here", not "passed
 *       through". Falls back to the swept set, then to the incoming seed,
 *       so it is never left empty.
 *
 * OVERFLOW: the room ids are u8 with 0xFF as the terminator, and every
 * destination here is small. addToByteSetMaxSize15 caps the working sets at
 * 15 entries plus a terminator inside u8[16]. finalRooms is the caller's
 * roomSet[8] / projectile->unkCC[8], so it takes at most 7 ids and the
 * terminator - the same cap the portal path writes. traversedRooms is
 * bounded by BOTH the caller's maxTraversedRooms and the 15 the working set
 * can hold. Rooms are scanned from 1 (room 0 is not a real room - every
 * other room loop in this file starts at 1) and never past 254, so a valid
 * id can never collide with the 0xFF terminator.
 */
/* defined further down this file, next to the portal walk it serves */
bool bgIsBboxOverlapping(coord3d *bbmin1, coord3d *bbmax1, coord3d *bbmin2, coord3d *bbmax2);

/**
 * THE FLOOR OF THE WORLD: the lowest point of any room's bounding box.
 *
 * Anything below this has left the level and is never going to touch
 * geometry again - the thrown-knife tick (propobj.c) uses it to bin knives
 * thrown into the void instead of letting them fall for their full
 * PROJECTILE_LIFETIME_FRAMES holding a slot out of the 20 in g_Projectiles
 * and restarting their whoosh every six fields the whole way down.
 *
 * Kept in ROOM-BOUNDS space (world * room_data_float1, the space
 * portFindRoomsByBounds works in) rather than converted to world here,
 * because room_data_float1 is set by sub_GAME_7F0B4810 and this runs at bg
 * load time where that ordering is not something to depend on. The caller
 * scales its world Y into this space to compare, which is what every other
 * room-bounds test in the codebase does.
 *
 * Only rooms with a REAL box count. bgRoomCalcBB leaves the bounds untouched
 * for a room with neither gfx nor stans, so an unmeasured room reads as
 * all-zero - and a stray 0.0 would sit far ABOVE the true floor on most
 * levels (Cradle's is -1918) and cull knives while they were still inside
 * the level. `maxbounds.y > minbounds.y` excludes a degenerate box; if that
 * leaves nothing, the sentinel survives and the caller culls nothing at all.
 */
static f32 s_BgLevelFloorY = BG_LEVEL_FLOOR_NONE;

f32 bgGetLevelFloorY(void)
{
    return s_BgLevelFloorY;
}

void portBgCalcLevelFloor(void)
{
    s32 room;

    s_BgLevelFloorY = BG_LEVEL_FLOOR_NONE;

    for (room = 1; room < g_MaxNumRooms; room++)
    {
        s_room_info *ri = &g_BgRoomInfo[room];

        if (ri->maxbounds.y > ri->minbounds.y
            && (s_BgLevelFloorY == BG_LEVEL_FLOOR_NONE || ri->minbounds.y < s_BgLevelFloorY))
        {
            s_BgLevelFloorY = ri->minbounds.y;
        }
    }
}

static void portFindRoomsByBounds(coord3d *fromPos, coord3d *toPos, u8 *fromRooms,
                                  u8 *finalRooms, s32 *traversedRooms,
                                  s32 *traversedRoomCount, s32 maxTraversedRooms)
{
    coord3d segMin;
    coord3d segMax;
    coord3d endMin;
    coord3d endMax;
    u8 sweptRooms[16];
    u8 hereRooms[16];
    const u8 *src;
    s32 room;
    s32 count;
    s32 i;
    s32 maxRoom;

    sweptRooms[0] = 0xff;
    hereRooms[0] = 0xff;

    for (i = 0; i < 3; i++)
    {
        f32 a = fromPos->f[i] * room_data_float1;
        f32 b = toPos->f[i] * room_data_float1;

        segMin.f[i] = (a < b) ? a : b;
        segMax.f[i] = (a < b) ? b : a;
        endMin.f[i] = b;
        endMax.f[i] = b;
    }

    maxRoom = (g_MaxNumRooms < 255) ? g_MaxNumRooms : 255;

    for (room = 1; room < maxRoom; room++)
    {
        s_room_info *ri = &g_BgRoomInfo[room];

        if (bgIsBboxOverlapping(&ri->minbounds, &ri->maxbounds, &segMin, &segMax))
        {
            addToByteSetMaxSize15(sweptRooms, (u8) room);

            if (bgIsBboxOverlapping(&ri->minbounds, &ri->maxbounds, &endMin, &endMax))
            {
                addToByteSetMaxSize15(hereRooms, (u8) room);
            }
        }
    }

    /* "where it is now", with the swept set and then the incoming seed as
     * fallbacks - the room AABBs do not tile space, so an end point can
     * legitimately land in none of them */
    src = (hereRooms[0] != 0xff) ? hereRooms
        : ((sweptRooms[0] != 0xff) ? sweptRooms : fromRooms);

    for (count = 0; count < 7 && src[count] != 0xff; count++)
    {
        finalRooms[count] = src[count];
    }

    finalRooms[count] = 0xff;

    /* bounded by the caller's cap AND by what the working set can hold */
    for (count = 0; count < maxTraversedRooms && count < 15 && sweptRooms[count] != 0xff; count++)
    {
        traversedRooms[count] = sweptRooms[count];
    }

    *traversedRoomCount = count;
}
#endif

/**
 * Address: 7F0B4AB4
 */
void bgFindRoomsAlongSegment(coord3d *fromPos, coord3d *toPos, u8 *fromRooms, u8 *finalRooms, s32 *traversedRooms, s32 *traversedRoomCount, s32 maxTraversedRooms)
{
    u8 sourceRooms[16];
    u8 foundRooms[16];
    u8 allRooms[16];
    s32 portalnum;
    s32 i;
    s32 count;
    u8 portalStatuses[PORTMAX];

#ifndef TARGET_N64
    /* No portals on this level, so the portal walk below cannot answer
     * anything - scan the room bounding boxes instead (see above). The
     * NULL check also covers the bgLoadFile path that leaves g_BgPortals
     * unset, which the loops below would dereference. */
    if (g_BgPortals == NULL || g_BgPortals[0].offset_portal == NULL)
    {
        portFindRoomsByBounds(fromPos, toPos, fromRooms, finalRooms,
                              traversedRooms, traversedRoomCount, maxTraversedRooms);
        return;
    }
#endif

    for (portalnum = 0; g_BgPortals[portalnum].offset_portal != 0; portalnum++)
    {
        portalStatuses[portalnum] = sub_GAME_7F0B9F14(portalnum, fromPos, toPos);
    }

    for (count = 0; count < 8; count++)
    {
        sourceRooms[count] = fromRooms[count];
    }

    for (count = 0; count < 8; count++)
    {
        allRooms[count] = fromRooms[count];
    }

    count = 0;

    while ((sourceRooms[count] != 0xff) && (count < 16))
    {
        count++;
    }

    count = 0;

    do
    {
        foundRooms[0] = 0xff;

        for (portalnum = 0; g_BgPortals[portalnum].offset_portal != 0; portalnum++)
        {
            for (i = 0; (sourceRooms[i] != 0xff) && (i < 16); i++)
            {
                if (portalStatuses[portalnum] == 1)
                {
                    if (g_BgPortals[portalnum].connectedRoom1 == sourceRooms[i])
                    {
                        addToByteSetMaxSize15(foundRooms, g_BgPortals[portalnum].connectedRoom2);
                        addToByteSetMaxSize15(allRooms, g_BgPortals[portalnum].connectedRoom2);
                        portalStatuses[portalnum] = 0;
                    }
                }

                if (portalStatuses[portalnum] == 2)
                {
                    if (g_BgPortals[portalnum].connectedRoom2 == sourceRooms[i])
                    {
                        addToByteSetMaxSize15(foundRooms, g_BgPortals[portalnum].connectedRoom1);
                        addToByteSetMaxSize15(allRooms, g_BgPortals[portalnum].connectedRoom1);
                        portalStatuses[portalnum] = 0;
                    }
                }
            }
        }

        if (foundRooms[0] == 0xff)
        {
            break;
        }

        for (count = 0; count < 16; count++)
        {
            sourceRooms[count] = foundRooms[count];
        }

        count = 0;

        while ((foundRooms[count] != 0xff) && (count < 16))
        {
            count++;
        }

        count = 0;
    }
    while (foundRooms[count] != 0xff);

    for (count = 0; (count < 7) && (sourceRooms[count] != 0xff); count++)
    {
        finalRooms[count] = sourceRooms[count];
    }

    finalRooms[count] = 0xff;

    for (count = 0; (count < maxTraversedRooms) && (allRooms[count] != 0xff); count++)
    {
        traversedRooms[count] = allRooms[count];
    }

    *traversedRoomCount = count;
}


/**
 * Address 0x7F0B4E40.
*/
Gfx *bgLevelRender(Gfx *arg0)
{
    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5)
    {
        // Game is paused. Do not render the level.
        // This improves performance in the watch.
        return arg0;
    }

#ifndef TARGET_N64
    /* in-band marker for the renderer: a player's world view begins here -
     * reopens the deferred-XLU capture window (0x46 = G_DXLU_NEWVIEW_EXT).
     * Emitted in the DL because this function runs at BUILD time; the
     * capture window is an EXECUTION-time concept. */
    arg0->words.w0 = (u32)0x46 << 24;
    arg0->words.w1 = 0;
    arg0++;
#endif
    gSPSetLights1(arg0++, GlobalLight);
    gSPLookAt(arg0++, sub_GAME_7F078474());
    gSPSegment(arg0++, SPSEGMENT_BG_DL, ptr_bg_data);

    if (dword_CODE_bss_8007FF88 == 1)
    {
        gSPDisplayList(arg0++, dword_CODE_bss_8007BF98);
    }
    else
    {
        arg0 = fogRenderClearFogMode(bgScissorCurrentPlayerViewDefault(sub_GAME_7F0B8D78(fogSetRenderFogColor(arg0, 0))));
    }

    gSPMatrix(arg0++, g_viProjectionMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

    return bondviewGfxPlayerField5cMatrix(arg0++);
}





f32 sub_GAME_7F0B4F9C(s32 arg0)
{
	return dword_CODE_bss_8007FF94[arg0 + 1];
}






/**
 * Calls @see bgScissorCurrentPlayerView with default current player values.
 * Address 0x7F0B4FB4.
 */
Gfx* bgScissorCurrentPlayerViewDefault(Gfx* arg0)
{
    return bgScissorCurrentPlayerView(
        arg0,
        g_CurrentPlayer->viewleft,
        g_CurrentPlayer->viewtop,
        g_CurrentPlayer->viewleft + g_CurrentPlayer->viewx,
        g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy);
}





/**
 * Same as @see bgScissorCurrentPlayerView, but accepts float parameters.
 * Address 0x7F0B4FF4.
 */
Gfx* bgScissorCurrentPlayerViewF(Gfx* arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4)
{
#ifndef TARGET_N64
    { /* TEMP DIAG (GE_NOROOMSCISSOR=1): full-view scissor for every room */
        static int noscis = -1;
        if (noscis < 0) noscis = getenv("GE_NOROOMSCISSOR") != NULL;
        if (noscis) {
            return bgScissorCurrentPlayerViewDefault(arg0);
        }
    }
#endif
    return bgScissorCurrentPlayerView(
        arg0,
        (s32)arg1,
        (s32)arg2,
        ceilFloatToInt(arg3),
        ceilFloatToInt(arg4));
}





/**
 * Specifies the drawing area (the scissoring box).
 * View is bound to current player view properties, but parameters can clip to smaller area.
 *
 * @param arg0: Display list pointer
 * @param left: Screen's left edge coordinates. Must be >= g_CurrentPlayer->viewleft otherwise ignored.
 * @param top: Screen's top edge coordinates. Must be >= g_CurrentPlayer->viewtop otherwise ignored.
 * @param width: Screen's right edge coordinates. Must be <= g_CurrentPlayer->viewleft+viewx otherwise ignored.
 * @param height: Screen's left bottom coordinates. Must be <= g_CurrentPlayer->viewtop+viewy otherwise ignored.
 *
 * Address 0x7F0B5058.
*/
Gfx *bgScissorCurrentPlayerView(Gfx *arg0, s32 left, s32 top, s32 width, s32 height)
{
    struct player *temp_v0;

    temp_v0 = g_CurrentPlayer;

    if (left < (s32) temp_v0->viewleft)
    {
        left = (s32) temp_v0->viewleft;
    }

    if (top < (s32) temp_v0->viewtop)
    {
        top = (s32) temp_v0->viewtop;
    }

    if (temp_v0->viewleft + temp_v0->viewx < width)
    {
        width = temp_v0->viewleft + temp_v0->viewx;
    }

    if (temp_v0->viewtop + temp_v0->viewy < height)
    {
        height = temp_v0->viewtop + temp_v0->viewy;
    }

    gDPSetScissor(arg0++, G_SC_NON_INTERLACE, left, top, width, height);

    return arg0;
}


void sub_GAME_7F0B5168(void) 
{
    s32 i;

    for (i = 0; i < PORTMAX; i++) 
    {
        table_for_portals[i].unk0 = -1;
    }
}


/**
 * Unreferenced.
 *
 * Loosely checks that arg1 surrounds arg0. Requires points be ordered according to min/max.
 *
 * Address 0x7F0B519C.
 */
s32 bgRectIsInside(struct bbox2d *arg0, struct bbox2d *arg1)
{
    if (arg1->min.x <= arg0->min.x)
    {
        if (arg0->min.x <= arg1->max.x)
        {
            if (arg1->min.y <= arg0->min.y)
            {
                if (arg0->min.y <= arg1->max.y)
                {
                    return 1;
                }
            }
        }
    }
    return 0;
}


/**
 * Address: 7F0B5208
 */
bool bgIsRoomOnScreen(s32 roomID, struct rectbbox *screenbox)
{
    s32 i;
    coord3d projected;
    coord3d corner;
    s32 count_z;
    s32 count_failed_projection;
    s32 count_left;
    s32 count_right;
    s32 count_top;
    s32 count_bottom;
    f32 zrange[2];

    count_z = 0;
    count_failed_projection = 0;
    count_left = 0;
    count_right = 0;
    count_top = 0;
    count_bottom = 0;

    viGetZRange(zrange);

    zrange[1] = zrange[1] / mCurrentLevelVisibilityScale;

#ifndef TARGET_N64
    f32 dbg_minz = 1e30f; /* GE_ROOMCULLDBG: nearest in-front corner depth */
#endif
    for (i = 0; i < 8; i++) {
        if (i & 1) {
            corner.x = g_BgRoomInfo[roomID].minbounds.x;
        } else {
            corner.x = g_BgRoomInfo[roomID].maxbounds.x;
        }

        if (i & 2) {
            corner.y = g_BgRoomInfo[roomID].minbounds.y;
        } else {
            corner.y = g_BgRoomInfo[roomID].maxbounds.y;
        }

        if (i & 4) {
            corner.z = g_BgRoomInfo[roomID].minbounds.z;
        } else {
            corner.z = g_BgRoomInfo[roomID].maxbounds.z;
        }

        if (bgProjectRoomCoordToScreen(&corner, &projected) == 0) {
            if (zrange[1] <= -projected.z) {
                count_z++;
            }

            if (screenbox->left <= projected.x) {
                count_left++;
            }

            if (projected.x <= screenbox->right) {
                count_right++;
            }

            if (screenbox->up <= projected.y) {
                count_top++;
            }

            if (projected.y <= screenbox->down) {
                count_bottom++;
            }

            count_failed_projection++;
        } else {
#ifndef TARGET_N64
            if (-projected.z < dbg_minz) dbg_minz = -projected.z;
#endif
            if (zrange[1] <= -projected.z) {
                count_z++;
            }

            if (projected.x <= screenbox->left) {
                count_left++;
            } else if (screenbox->right <= projected.x) {
                count_right++;
            }

            if (projected.y <= screenbox->up) {
                count_top++;
            } else if (screenbox->down <= projected.y) {
                count_bottom++;
            }
        }
    }

    /**
     * If all 8 of the room's bounding box corners are behind the camera,
     * reject this room
     */
#ifndef TARGET_N64
    { /* GE_ROOMCULLDBG=1 debug: logs every room removed by the Z test alone,
       * with the nearest in-front corner's projected depth against the
       * threshold and the true world distance to the bbox. */
        static s32 dbg = -1;
        static u32 calls;
        if (dbg < 0) { extern char *getenv(const char *); dbg = getenv("GE_ROOMCULLDBG") != NULL; }
        if (dbg && count_z == 8 && count_failed_projection < 8 && (calls % 60) < 1)
        {
            extern coord3d *bondviewGetCurrentPlayersPosition(void);
            coord3d *eye = bondviewGetCurrentPlayersPosition();
            f32 bx0 = g_BgRoomInfo[roomID].minbounds.x * room_data_float2, bx1 = g_BgRoomInfo[roomID].maxbounds.x * room_data_float2;
            f32 by0 = g_BgRoomInfo[roomID].minbounds.y * room_data_float2, by1 = g_BgRoomInfo[roomID].maxbounds.y * room_data_float2;
            f32 bz0 = g_BgRoomInfo[roomID].minbounds.z * room_data_float2, bz1 = g_BgRoomInfo[roomID].maxbounds.z * room_data_float2;
            f32 cx = eye->x < bx0 ? bx0 : (eye->x > bx1 ? bx1 : eye->x);
            f32 cy = eye->y < by0 ? by0 : (eye->y > by1 ? by1 : eye->y);
            f32 cz = eye->z < bz0 ? bz0 : (eye->z > bz1 ? bz1 : eye->z);
            f32 ddx = cx - eye->x, ddy = cy - eye->y, ddz = cz - eye->z;
            fprintf(stderr, "roomcull: room=%d zcull minprojz=%.1f zrange=%.1f worlddist=%.1f bbox=(%.0f..%.0f, %.0f..%.0f, %.0f..%.0f) eye=(%.0f,%.0f,%.0f) rdf2=%.4f\n",
                    roomID, dbg_minz, zrange[1], sqrtf(ddx * ddx + ddy * ddy + ddz * ddz), bx0, bx1, by0, by1, bz0, bz1, eye->x, eye->y, eye->z, room_data_float2);
        }
        if (roomID == 1) calls++;
    }
#endif
    if (count_failed_projection == 8
            || count_z == 8
            || count_left == 8
            || count_right == 8
            || count_top == 8
            || count_bottom == 8) {
        return FALSE;
    }

    return TRUE;
}


/**
 * Address: 7F0B5488
 */
bool bgProjectRoomCoordToScreen(coord3d* src, coord3d* dst)
{
    Mtxf* temp_a0;
    s32 var_v0;

    temp_a0 = camGetWorldToScreenMtxf();
    dst->x = src->x * room_data_float2;
    dst->y = src->y * room_data_float2;
    dst->z = src->z * room_data_float2;
    mtx4TransformVecInPlace(temp_a0, dst);

    transform3Dto2DWithZScaling(dst, dst);

    if (dst->z > 0.0f)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}


s32 sub_GAME_7F0B5528(s32 portalnum, f32 arg1, coord3d *arg2)
{
    Mtxf *matrix;
    coord3d *point;
    s32 i;
    s32 next;
    f32 zrange[2];
    s32 allbehind;
    struct PortalMetric metric;
    s32 len;

    matrix = camGetWorldToScreenMtxf();
    allbehind = 1;
    viGetZRange(zrange);
    zrange[1] /= mCurrentLevelVisibilityScale;

    for (i = 0; i < g_BgPortals[portalnum].offset_portal->numPoints; i++) {
        point = &arg2[i];

        point->x = (&g_BgPortals[portalnum].offset_portal->point)[i].x;
        point->y = (&g_BgPortals[portalnum].offset_portal->point)[i].y;
        point->z = (&g_BgPortals[portalnum].offset_portal->point)[i].z;

        if (arg1 != 0.0f) {
            sub_GAME_7F0B96CC(portalnum, (f32 *) &metric);

            point->x += metric.normal.x * arg1;
            point->y += metric.normal.y * arg1;
            point->z += metric.normal.z * arg1;
        }

        point = &arg2[i];
        point->x *= room_data_float2;
        point->y *= room_data_float2;
        point->z *= room_data_float2;

        mtx4TransformVecInPlace(matrix, point);

#ifndef TARGET_N64
        { /* GE_PORTALFAR=n (diag): scale the 0.9*zrange portal far-Z
           * threshold, for A/B comparison against a reference N64 frame */
            static f32 pf = -1.0f;
            if (pf < 0.0f) { extern char *getenv(const char *); const char *e = getenv("GE_PORTALFAR"); pf = (e != NULL) ? (f32) atof(e) : 1.0f; if (pf <= 0.0f) pf = 1.0f; }
            if (-zrange[1] * 0.9f * pf < point->z) {
                allbehind = 0;
            }
        }
#else
        if (-zrange[1] * 0.9f < point->z) {
            allbehind = 0;
        }
#endif
    }

#ifndef TARGET_N64
    { /* GE_ROOMCULLDBG=1: portals the far-Z test drops (every point past
       * 0.9*zrange) - projected z vs the threshold vs the WORLD distance of
       * the portal's first point, once per second per portal. */
        static s32 dbg = -1;
        if (dbg < 0) { extern char *getenv(const char *); dbg = getenv("GE_ROOMCULLDBG") != NULL; }
        if (dbg && allbehind && (g_GlobalTimer % 60) == 0)
        {
            extern coord3d *bondviewGetCurrentPlayersPosition(void);
            coord3d *eye = bondviewGetCurrentPlayersPosition();
            f32 wx = (&g_BgPortals[portalnum].offset_portal->point)[0].x * room_data_float2 - eye->x;
            f32 wy = (&g_BgPortals[portalnum].offset_portal->point)[0].y * room_data_float2 - eye->y;
            f32 wz = (&g_BgPortals[portalnum].offset_portal->point)[0].z * room_data_float2 - eye->z;
            fprintf(stderr, "portalcull: portal=%d zthresh=%.1f p0z=%.1f worlddist0=%.1f npts=%d\n",
                    portalnum, -zrange[1] * 0.9f, arg2[0].z, sqrtf(wx * wx + wy * wy + wz * wz), g_BgPortals[portalnum].offset_portal->numPoints);
        }
    }
#endif
    if (allbehind) {
        return 0;
    }

    len = g_BgPortals[portalnum].offset_portal->numPoints;

    for (i = 0; i < g_BgPortals[portalnum].offset_portal->numPoints; i++) {
        point = &arg2[i];
        next = (i + 1) % g_BgPortals[portalnum].offset_portal->numPoints;

        if ((point->z > 0.0f && arg2[next].z <= 0.0f) || (point->z <= 0.0f && arg2[next].z > 0.0f)) {
            f32 scale = -point->z / (arg2[next].z - point->z);

            arg2[len].x = point->x + ((arg2[next].x - point->x) * scale);
            arg2[len].y = point->y + ((arg2[next].y - point->y) * scale);
            point = &arg2[i];
            arg2[len].z = 0.0f;
            len++;
        }
    }

    return len;
}


s32 sub_GAME_7F0B5864(s32 portalnum, bbox2d *bbox)
{
    f32 scale;
    PortalCache *cache;
    s32 j;
    coord3d points[19];
    coord2d screenpos;
    bbox2d bounds;
    s32 pointcount;
    s32 onscreencount;
    s32 i;

    cache = &table_for_portals[portalnum];

#ifndef TARGET_N64
    { /* GE_ALLROOMS=1 debug: treat every portal as fully visible (full-screen
       * bbox, no projection/far/behind tests) so the room walk reaches
       * everything; combine with GE_FARX to lift the room z-cull. Separates
       * "room culled" from "geometry absent". */
        extern char *getenv(const char *);
        static s32 allrooms = -1;
        if (allrooms < 0)
        {
            allrooms = getenv("GE_ALLROOMS") != NULL;
        }
        if (allrooms)
        {
            bbox->min.x = g_CurrentPlayer->screensize.min.x;
            bbox->min.y = g_CurrentPlayer->screensize.min.y;
            bbox->max.x = g_CurrentPlayer->screensize.max.x;
            bbox->max.y = g_CurrentPlayer->screensize.max.y;
            return 1;
        }
    }
#endif

    if (cache->count >= 0)
    {
        bbox->min.x = cache->bbox.min.x;
        bbox->min.y = cache->bbox.min.y;
        bbox->max.x = cache->bbox.max.x;
        bbox->max.y = cache->bbox.max.y;

        return cache->count;
    }

    scale = sub_GAME_7F0B9990(portalnum);
    pointcount = sub_GAME_7F0B5528(portalnum, scale, points);

    if (scale > 0.0f)
    {
        pointcount += sub_GAME_7F0B5528(portalnum, -scale, &points[pointcount]);
    }

#ifndef TARGET_N64
    { /* GE_PORTALPTS=<n> (diag): once per second, portal
       * n's raw corner WORLD points, their view-space z, and each in-front
       * point's screen projection - is the projected box faithful? */
        extern char *getenv(const char *);
        static s32 want = -2;
        if (want == -2) { const char *e = getenv("GE_PORTALPTS"); want = (e && e[0]) ? (s32) atol(e) : -1; }
        if (want == portalnum && (g_GlobalTimer % 60) == 0)
        {
            s32 k;
            printf("portalpts: p=%d scale=%f npts=%d\n", portalnum, scale, pointcount);
            for (k = 0; k < g_BgPortals[portalnum].offset_portal->numPoints; k++)
            {
                printf("portalpts:   world[%d]=(%.1f, %.1f, %.1f)\n", k,
                       (&g_BgPortals[portalnum].offset_portal->point)[k].x * room_data_float2,
                       (&g_BgPortals[portalnum].offset_portal->point)[k].y * room_data_float2,
                       (&g_BgPortals[portalnum].offset_portal->point)[k].z * room_data_float2);
            }
            for (k = 0; k < pointcount; k++)
            {
                coord2d sp;
                if (points[k].z <= 0.0f)
                {
                    transform3Dto2DWithZScaling(&points[k], &sp);
                    printf("portalpts:   proj[%d] viewz=%.2f -> screen=(%.1f, %.1f)\n", k, points[k].z, sp.x, sp.f[1]);
                }
                else
                {
                    printf("portalpts:   proj[%d] viewz=%.2f BEHIND\n", k, points[k].z);
                }
            }
        }
    }
#endif

    onscreencount = 0;
    i = 0;

#ifndef TARGET_N64
    {
        s32 behindcount = 0;
#endif

    if (pointcount > 0)
    {
        j = 0;

        do
        {
            if (points[j].z <= 0.0f)
            {
                transform3Dto2DWithZScaling(&points[j], &screenpos);

                if (onscreencount == 0)
                {
                    bounds.min.x = (bounds.max.x = screenpos.x);
                    bounds.min.y = (bounds.f[1][1] = screenpos.f[1]);
                }
                else
                {
                    if (screenpos.x < bounds.min.x)
                    {
                        bounds.min.x = screenpos.x;
                    }
                    
                    if (bounds.max.x < screenpos.x)
                    {
                        bounds.max.x = screenpos.x;
                    }
                    
                    if (screenpos.y < bounds.min.y)
                    {
                        bounds.min.y = screenpos.y;
                    }
                    
                    if (bounds.max.y < screenpos.y)
                    {
                        bounds.max.y = screenpos.y;
                    }
                }

            onscreencount++;
            }
#ifndef TARGET_N64
            else
            {
                behindcount++;
            }
#endif

        i++;
        j++;

        }
        while (i != pointcount);
    }

    if (onscreencount == 0)
    {
        bounds.max.y = (bounds.min.x = 0.0f);
        bounds.min.y = 0.0f;
        bounds.max.x = 0.0f;
    }
    else
    {
        if ((bounds.max.x <= bounds.min.x) || (bounds.max.y <= bounds.min.y))
        {
            bounds.min.x = g_CurrentPlayer->screensize.min.x;
            bounds.min.y = g_CurrentPlayer->screensize.min.y;
            bounds.max.x = g_CurrentPlayer->screensize.max.x;
            bounds.max.y = g_CurrentPlayer->screensize.max.y;
        }
#ifndef TARGET_N64
        else if (behindcount > 0)
        {
            /* Vanilla bug fix: a portal crossing the camera plane builds its rect
             * from only its in-front corners, too small, so rooms beyond it get
             * scissored off; use the full view when the box already reaches the
             * screen. Not during demo playback, because this rect gates portal
             * descent and a bigger one would desync the recorded RNG stream. */
            extern char *getenv(const char *);
            extern s32 get_is_ramrom_flag(void);
            extern s32 bgRectIntersect(struct bbox2d *a, struct bbox2d *b);
            static s32 off = -1;
            bbox2d vis;

            if (off < 0)
            {
                off = getenv("GE_WALLFIX_OFF") != NULL;
            }
            vis = bounds;
            if (!off && get_is_ramrom_flag() == 0 && bgRectIntersect(&vis, &g_CurrentPlayer->screensize))
            {
                bounds.min.x = g_CurrentPlayer->screensize.min.x;
                bounds.min.y = g_CurrentPlayer->screensize.min.y;
                bounds.max.x = g_CurrentPlayer->screensize.max.x;
                bounds.max.y = g_CurrentPlayer->screensize.max.y;
            }
        }
#endif
    }

#ifndef TARGET_N64
    }
#endif

    bbox->min.x = bounds.min.x;
    bbox->min.y = bounds.min.y;
    bbox->max.x = bounds.max.x;
    bbox->max.y = bounds.max.y;

    cache->bbox.min.x = bbox->min.x;
    cache->bbox.min.y = bbox->min.y;
    cache->bbox.max.x = bbox->max.x;
    cache->bbox.max.y = bbox->max.y;
    cache->count = onscreencount;

    return onscreencount;
}


/**
 * Unreferenced.
 *
 * Address 0x7F0B5B14.
 */
Gfx *bgFillRectangle(Gfx *gdl, s32 ulx, s32 uly, s32 lrx, s32 lry)
{
    gDPFillRectangle(gdl++, ulx, uly, lrx + 1, lry + 1);
    return gdl;
}


/**
 * Unreferenced.
 */
void bgFillRectangleWithSides(Gfx *gdl, s32 ulx, s32 uly, s32 lrx, s32 lry)
{
    bgFillRectangle(
        bgFillRectangle(
            bgFillRectangle(
                bgFillRectangle(gdl,
                ulx, uly, lrx, uly), /* full rectangle */
                lrx, uly, lrx, lry), /* right side */
                ulx, lry, lrx, lry), /* bottom */
                ulx, uly, ulx, lry); /* top */
}



/**
 * Determines if two rectangles overlap, adjusting first argument to be the intersection.
 *
 * Returns 0 if no intersection (or edge equality), 1 otherwise.
 *
 * Address 0x7F0B5BDC.
 */
s32 bgRectIntersect(struct bbox2d *a, struct bbox2d *b)
{
	a->min.x = a->min.x > b->min.x ? a->min.x : b->min.x;
	a->min.y = a->min.y > b->min.y ? a->min.y : b->min.y;
	a->max.x = b->max.x > a->max.x ? a->max.x : b->max.x;
	a->max.y = b->max.y > a->max.y ? a->max.y : b->max.y;

	if (a->min.x >= a->max.x) {
		a->min.x = a->max.x;
		return FALSE;
	}

	if (a->max.y <= a->min.y) {
		a->min.y = a->max.y;
		return FALSE;
	}

	return TRUE;
}


// Address: 0x7F0B5CC0
// Does a union. Increases the size of 'a' so it contains 'b'.
void bgRectOutersect(struct bbox2d *a, struct bbox2d *b)
{
    (a->min).x = ((a->min).x < (b->min).x) ? (a->min).x : (b->min).x;
    (a->min).y = ((a->min).y < (b->min).y) ? (a->min).y : (b->min).y;
    (a->max).x = ((a->max).x > (b->max).x) ? (a->max).x : (b->max).x;
    (a->max).y = ((a->max).y > (b->max).y) ? (a->max).y : (b->max).y;
}

// Address: 0x7f0b5d58
// Does a shallow copy of 'b' into 'a'. Equivalent to '*a = *b;'.
void bbox2dCopy(struct bbox2d *a, struct bbox2d *b)
{
    (a->min).x = (b->min).x;
    (a->min).y = (b->min).y;
    (a->max).x = (b->max).x;
    (a->max).y = (b->max).y;
}


/**
 * Formats a portal ID for debug output.
 */
char *bgDebPrintPORTALID(s32 portID)
{
    static char bgDebPortalOutBuffer[10][9];
    static s32 bgDebPortalOutLineNum = 0;
    char *portIdStr;

    bgDebPortalOutLineNum = (bgDebPortalOutLineNum + 1) % 10;
    portIdStr = bgDebPortalOutBuffer[bgDebPortalOutLineNum];

    sprintf(portIdStr, "PORT%d", portID);

    return portIdStr;
}


/**
 * Formats a room ID for debug output.
 */
char *bgDebPrintROOMID(s32 roomId)
{
    static char bgDebRoomOutBuffer[10][9];
    static s32 bgDebRoomOutLineNum = 0;
    char *roomIdStr;

    bgDebRoomOutLineNum = (bgDebRoomOutLineNum + 1) % 10;
    roomIdStr = bgDebRoomOutBuffer[bgDebRoomOutLineNum];

    sprintf(roomIdStr, "ROOM%d", roomId);

    return roomIdStr;
}


/**
 * These definitions must come AFTER bgDebPrintROOMID for matching.
 */

/**
 * address 8007C100
 * EU .bss 8007A040
*/
bg_queued_portal_entry g_BgPortalQueue[BG_PORTAL_QUEUE_LEN];

bg_portal_data_entry *g_BgPortals;
intptr_t ptr_bgdata_offsets;
s32 dword_CODE_bss_8007FF88;
bg_room_data *ptr_bgdata_room_fileposition_list;
s32 *dword_CODE_bss_8007FF90;
f32 *dword_CODE_bss_8007FF94;

#ifdef VERSION_EU
s_bound_info dword_CODE_bss_8007FFA0[124];
u32 missingeubytes[4];
s32 dword_CODE_bss_800815f0;
s32 dword_CODE_bss_800815f4;
s32 dword_CODE_bss_800815f8;
s32 eu_bss_8007BFA0;
s32 eu_bss_8007BFA4;
#else
s32 dword_CODE_bss_8007FF98;
s32 dword_CODE_bss_8007FF9C;
s_bound_info dword_CODE_bss_8007FFA0[204];
s32 dword_CODE_bss_800815f0;
s32 dword_CODE_bss_800815f4;
s32 dword_CODE_bss_800815f8;
#endif

//D:80044868
BoundVec D_80044868 = {0x7FFF, 0x7FFF, 0x7FFF};
//D:80044874
BoundVec D_80044874 = {-0x8000, -0x8000, -0x8000};
//D:80044880
BoundVec D_80044880 = {0x7FFF, 0x7FFF, 0x7FFF};
//D:8004488C
BoundVec D_8004488C = {-0x8000, -0x8000, -0x8000};
//D:80044898
s32 D_80044898 = 0;
//D:8004489C
s32 D_8004489C = 0xF;
//D:800448A0
s32 g_BgPortalQueueWriteIndex = 0;
//D:800448A4
s32 g_BgPortalQueueReadIndex = 0;

/**
 * Local stack.
 *
 * Address 0x800448A8.
 */
s32 g_BgStack[BG_STACK_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/**
 * Current top of the stack.
 *
 * Address 0x800448F8.
 */
s32 g_BgStackCount = 0;

//D:800448FC
s32 current_visibility = 0;

//D:80044900
f32 D_80044900 = 0;

//D:80044904
s32 D_80044904 = 0x7F7FFFFF;
//D:80044908
s32 D_80044908 = 0x7F7FFFFF;
//D:8004490C
s32 D_8004490C = 0x7F7FFFFF;
//D:80044910
s32 D_80044910 = 0xFF7FFFFF;
//D:80044914
s32 D_80044914 = 0xFF7FFFFF;
//D:80044918
s32 D_80044918 = 0xFF7FFFFF;
//D:8004491C
u32 D_8004491C = 0;
//D:80044920
u32 D_80044920 = 0;
//D:80044924
u32 D_80044924 = 0;

#if defined(VERSION_EU)
s32 eu_cdata_0x1f1c0 = 0;
s32 eu_cdata_0x1f1c4 = 0;
#endif

/*
    //###RenderMode / Combiner Look - Up - Tables
    The reason for this LUT is to dynamicly change the rendermode and combiner to
    FOG / NoFog or any other setting they might have wanted to test during development
    as it applies during runtime

//###Reminder:
    1cycle combiners repeat both cycles
    gDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA2)

    combiner macros are a list of parameters that form a mathematical sum.
                            (       -  )*     +  ,  (       -  )*     +
    G_CC_MODULATERGBA2	    COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0
*/
#if 0
//New Defines to be added to gbi.h
/*custom combiner for triangle alpha*/
#define	ModulateRGB_EnvA 	TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT
/*custom combiner for triangle alpha*/
#define	ModulateRGB_EnvA2 	COMBINED, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT
/*custom combiner for Texture*triangle alpha*/
#define	ModulateRGBA_EnvA 	TEXEL0, 0, SHADE, 0, TEXEL0, 0, ENVIRONMENT, 0
/*custom combiner for texture*triangle alpha*/
#define	ModulateRGBA_EnvA2 	COMBINED, 0, SHADE, 0, COMBINED, 0, ENVIRONMENT, 0
/*custom combiner for triangle alpha*/
#define	SHADE_EnvA 		    0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT
/*Tri-linear filter colour, flat tile alpha (for cutouts)*/
#define TLRGB_ATile1        TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, TEXEL1, 0
#endif
//D:80044928
Gfx DL_LUT_UNKNOWN[] = {
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, 0, SCALE, 0,  TEXEL1, 0, PRIM_LOD_FRAC, 0,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    0,0
};

//D:80044940 - Primary
Gfx DL_LUT_PRIMARY_ADDFOG[] = {
    //Add FOG to all rendermodes
    //Standard HiQuality Surface to Standard Fogable HiQuality Surface
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2),
    //Terrain to Fogable Terrain
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_TERR2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_TERR2),
    //Standard DECAL to FOG DECAL
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_DECAL2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_DECAL2),
    //Transparent DECAL to  FOG Transparent DECAL
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_DECAL2),
    //Transparent Surface to FOG Transparent Surface
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_SURF2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2),
    // Billboard Cut-out to FOG Billboard Cut-out - eg, Mario Tree or Depot lamp
    // See PGDLists\Transparent Textures.htm for more info
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_TEX_EDGE2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
    //Standard Z-Less OPA to Standard FOG Z-Less OPA
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_OPA_SURF2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_OPA_SURF2),
    //Z-Less OPA Terrain to Z-Less Fog OPA Terrain
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_OPA_TERR2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_OPA_TERR2),
    0x0, 0x0
};

//D:800449C8 - Secondary
Gfx DL_LUT_SECONDARY_ADDFOG[] = {
    //Add FOG to Rendermodes
    //Transparent DECAL to  FOG Transparent DECAL
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_DECAL2),
    //Transparent Surface to FOG Transparent Surface
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_SURF2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2),

    // Billboard Cut-out to FOG Billboard Cut-out - eg, Mario Tree or Depot lamp
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_TEX_EDGE2),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),

    // Swap all refrences to Shade in Alpha to Environment
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    gsDPSetCombineLERP(0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE),//gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT,  0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT), //gsDPSetCombineMode(G_CC_SHADE_EnvA, G_CC_SHADE_EnvA),
    // This one is an oddball... its extra here AND is weird using Tile1 only for Alpha
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    0x0,
    0x0
};

//D:80044AB0
// Loaded once on first time entering level, only once ever
// Swap all refrences to Shade in Alpha to Environment
Gfx DL_LUT_PRIMARY[] = {
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT,  0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT),
    0,0
};

//D:80044B58
// Loaded once on first time entering level, only once ever
// Swap all refrences to Shade in Alpha to Environment
Gfx DL_LUT_SECONDARY[] = {
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SHADE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT,  0, 0, 0, SHADE,  0, 0, 0, ENVIRONMENT),
    0,0
};

//D:80044C00
Gfx DL_LUT_BILLBOARD[] = {
    //Transparent 1Cycle to  BillBoard 1Cycle
    gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2), gsDPSetRenderMode(G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2),
    //Transparent Surface to Billboard
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_SURF2), gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_TEX_EDGE2),
    0x0,0x0
};

//D:80044C28
//water
Gfx DL_LUT_WATER[] = {
    0xB900031D, 0x00552078, 0xB900031D, 0x00502078,
    0xB900031D, 0x0C192078, 0xB900031D, 0x0C182078,
    /*
    //1 Cycle Opa to Particle
    gsDPSetRenderMode(RM_AA_ZB_OPA_SURF, RM_AA_ZB_OPA_SURF2), gDPSetRenderMode(G_RM_AA_ZB_PCL_SURF, G_RM_AA_ZB_PCL_SURF2),
    //2 cycle Opa to Particle
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2), gDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_PCL_SURF2),
    */
    0x0, 0
};

//D:80044C50
Gfx DL_LUT_CLOUD[] = {
    //Transparent to Cloud (Saves AA - Stops Jaggies from appearing behind BillBoard)
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_SURF2), gsDPSetRenderMode(G_RM_PASS, G_RM_ZB_CLD_SURF2),
    0,0
};

//D:80044C68
//(Wallet Bond - Main Menu)
Gfx DL_LUT_WALLETBOND[] = {
    gsDPSetCycleType(G_CYC_1CYCLE),
    gsDPSetCycleType(G_CYC_2CYCLE), //1Cycle --> 2Cycle
    0xB900031D, 0x00502048,
    0xB900031D, 0x08D02048,
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    /*
    //1 Cycle particle Surface to 2 Cycle colour + 1-a*Fog ???
    gsDPSetCycleType(G_CYC_2CYCLE),
    gsDPSetRenderMode(G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2), gDPSetRenderMode(AA_EN | IM_RD | CVG_DST_CLAMP | ALPHA_CVG_SEL | ZMODE_OPA | GBL_c1(G_BL_CLR_IN, G_BL_A_SHADE, G_BL_CLR_FOG, G_BL_1MA) | GBL_c2(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA)),
    gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_PASS2), gDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATERGBA2),
    */
    0x0, 0
};

//D:80044CA0
Gfx DL_LUT_FIXFOGALPHA3[] = {
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SCALE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SCALE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE,  TEXEL0, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL0, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT,  TEXEL0, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0,  TEXEL0, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL0, 0, SCALE, 0,  TEXEL0, 0, ENVIRONMENT, 0,  TEXEL0, 0, SCALE, 0,  TEXEL0, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(CENTER, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT,  CENTER, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SCALE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEI2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineMode(G_CC_TRILERP, G_CC_MODULATEIA2),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  COMBINED, 0, SCALE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SCALE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SHADE, 0,  0, 0, 0, SHADE),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, SHADE, 0),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,  1, 0, TEXEL1, 0,  COMBINED, 0, SCALE, 0,  COMBINED, 0, ENVIRONMENT, 0),
    gsDPSetCombineLERP(0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    gsDPSetCombineLERP(CENTER, 0, SCALE, 0,  0, 0, 0, ENVIRONMENT,  0, 0, 0, COMBINED,  0, 0, 0, COMBINED),
    0,0
};

//D:80044D88
Gfx *ptrDynamic_CC_RM_LUT[] = {
    &DL_LUT_UNKNOWN, &DL_LUT_PRIMARY_ADDFOG, &DL_LUT_BILLBOARD, &DL_LUT_WATER, &DL_LUT_CLOUD,
    &DL_LUT_SECONDARY_ADDFOG, &DL_LUT_PRIMARY, &DL_LUT_SECONDARY, &DL_LUT_WALLETBOND, &DL_LUT_FIXFOGALPHA3
};


s32 getMaxNumRooms(void) 
{
    return g_MaxNumRooms;
}


/*
 * Return butflags0 (confirmed u8)
 */
u8 getROOMID_isRendered(s32 roomID)
{
    return g_BgRoomInfo[roomID].room_rendered;
}


/*
 * Return butflags1 (confirmed u8)
 */
u8 getROOMID_isNeighborToRendered(s32 roomID)
{
    return g_BgRoomInfo[roomID].room_neighbor_to_rendered;
}


s32 getIndexOfPORTALID(intptr_t portalID)
{
    s32 i;

    for(i = 0; g_BgPortals[i].offset_portal != NULL; i++)
    {
        if (portalID == (intptr_t)g_BgPortals[i].offset_portal)
        {
            return i;
        }
    }
    #ifdef DEBUG
    osSyncPrintf("bg: bgPortalIndexFromPtr(): No portal found for %08x ",portalID);
    #endif
    return 0;
}


void roomsHandleStateDebugging(void)
{
    char roomstates[MAXROOMCOUNT + 1];
    s32 roomnum;

    if (debugIsRoomStateDebugEnabled())
    {
        for (roomnum = 1; roomnum < g_MaxNumRooms; roomnum++)
        {
            if (g_BgRoomInfo[roomnum].model_bin_loaded)
            {
                roomstates[roomnum] = (roomnum % 10) + '0';
            }
            else
            {
                roomstates[roomnum] = '.';
            }
        }

        roomstates[roomnum] = '\0';
    }
}


u32 bgDecompress(u8* source, u8 *target)
{
    u8 buffer[0x2100];
    return decompressdata(source, target, buffer);
}


/**
 * Address: 7F0B5FAC
 *
 * Load room's compressed vertex table from the bg file, decompress it
 * into dst, and store the resulting Vtx buffer in room.
 */
s32 bgLoadRoomVtxData(s32 roomnum, u8 *dst, s32 len)
{
    s_room_info *room;
    s32 alignedsize;
    s32 offset;
    s32 result;

    room = &g_BgRoomInfo[roomnum];
    alignedsize = (room->csize_point_index_binary + 0xf) & ~0xf;
    if (len < alignedsize + 0x20) {
        return -1;
    }

    /**
    * pPointTableBin is stored as a segment-0x0f bgdata address.
    * The seemingly unncessary " + ptr_bg_data - ptr_bg_data" likely comes from paired bgdata pointer/offset macros.
    * Adding 0xf1000000 strips the 0x0f000000 segment tag, yielding a file offset.
    */
#ifdef TARGET_N64
    offset = (((u8 *)ptr_bgdata_room_fileposition_list[roomnum].pPointTableBin + ptr_bg_data) - ptr_bg_data) + 0xf1000000;
#else
    /* same value, integer domain - the pointer+int form is UB on a host
     * pointer (the +ptr-ptr pair cancels; only the wrap matters) */
    offset = (s32)((u32)(uintptr_t)ptr_bgdata_room_fileposition_list[roomnum].pPointTableBin + 0xf1000000u);
#endif
    obLoadBGFileBytesAtOffset(levelinfotable[levelentry_index].bg_seg_filename, dst + (len - alignedsize), offset, alignedsize);
    result = bgDecompress(dst + (len - alignedsize), dst);

#ifndef TARGET_N64
    /* The decompressed point table is a byte-exact big-endian N64 image, but
     * every consumer (gfx_sp_vertex, the vtx-batch bounds builder, the
     * shot-impact bbox walker) reads Vtx fields natively, so all six s16s per
     * vertex arrived half-swapped (this caused the world-wide texture smear).
     * Swap each Vtx's s16 fields once at load; the cn[4] bytes stay as they
     * are. */
    {
        s32 nvtx = result >> 4; /* 16-byte Vtx records */
        u8 *b = dst;
        s32 vi, f;
        for (vi = 0; vi < nvtx; vi++, b += 16) {
            for (f = 0; f < 12; f += 2) {
                u8 t = b[f];
                b[f] = b[f + 1];
                b[f + 1] = t;
            }
        }
    }
    { /* GE_LIGHTDBG: census of authored vertex alpha < 255 per room. On N64
       * these bytes are moot, but the PC renderer honoured them before the
       * SHADER_OPT_ALPHA_CVG_SEL rule, so this lists exactly the geometry that
       * rule changed. */
        extern char *getenv(const char *);
        static int ldbg = -1;
        if (ldbg < 0) ldbg = getenv("GE_LIGHTDBG") != NULL;
        if (ldbg) {
            s32 nvtx = result >> 4, vi, nlow = 0, minA = 255, nzero = 0;
            u8 *b = dst;
            for (vi = 0; vi < nvtx; vi++, b += 16) {
                if (b[15] < 255) { nlow++; if (b[15] < minA) minA = b[15]; if (b[15] == 0) nzero++; }
            }
            if (nlow) fprintf(stderr, "lightdbg: room %d vtxalpha<255: %d of %d (min %d, zero %d)\n", roomnum, nlow, nvtx, minA, nzero);
        }
    }
    { /* diagnostic (GE_DUMP_BOND): first records post-conversion */
        extern char *getenv(const char *);
        static int dumped;
        if (dumped < 2 && getenv("GE_DUMP_BOND") != NULL) {
            extern void ge_dbg_gfxdump(const char *tag, void *gdl, int count);
            dumped++;
            ge_dbg_gfxdump("roomvtx", dst, 16); /* 16 rows = 8 Vtx */
        }
    }
#endif

    room->vertices = (Vtx *)dst;
    room->usize_point_index_binary = result;

    return result;
}


/**
 * Address: 7F0B609C
 *
 * Load and decompress a room's primary display list data.
 *
 * On success, roominfo->ptr_expanded_mapping_info is set to dst,
 * and roominfo->usize_primary_DL_binary is set to the returned size.
 */
s32 bgLoadRoomPrimaryGdl(s32 roomnum, u8 *dst, s32 allocsize)
{
    s_room_info *roominfo;
    s32 size;
    s32 fileoffset;
    u8 *scratch;
    s32 expanded_size;

    roominfo = &g_BgRoomInfo[roomnum];

    size = roominfo->csize_primary_DL_binary;
    size = (size + 0xf) & ~0xf; // Align to 16 bytes

    /**
     * Check if there is enough room to temporarily place the compressed data
     * at the end of the available buffer. Return -1 if there's not.
     */
    if (allocsize < size + 0x20) {
        return -1;
    }

    // Load the compressed data into the end of the buffer, starting at dst.
    scratch = dst + (allocsize - size);

#ifdef TARGET_N64
    fileoffset = (s32)((u8 *)ptr_bgdata_room_fileposition_list[roomnum].pPriMappingBin + ptr_bg_data) - ptr_bg_data;
    fileoffset += 0xf1000000;
#else
    /* integer domain (see bgLoadRoomVtxData) */
    fileoffset = (s32)((u32)(uintptr_t)ptr_bgdata_room_fileposition_list[roomnum].pPriMappingBin + 0xf1000000u);
#endif

    obLoadBGFileBytesAtOffset(levelinfotable[levelentry_index].bg_seg_filename, scratch, fileoffset, size);

    // Decompress from the end-of-buffer location at dst.
    expanded_size = bgDecompress(scratch, dst);

    /**
     * Copy the decompressed GDL back to the end of the buffer as scratch.
     * texLoadFromGdl can then read from scratch and write the final
     * texture-processed GDL/data back to dst.
     */
    scratch = dst + (allocsize - expanded_size / 8 * sizeof(Gfx));

    texCopyGdls((Gfx *)dst, (Gfx *)scratch, expanded_size);
    expanded_size = expanded_size / 8 * sizeof(Gfx);

    clear_light_fixturetable_in_room(roomnum);

#ifndef TARGET_N64
    { extern s32 g_LightDbgRoomLoad; g_LightDbgRoomLoad = 1; }
#endif
    size = texLoadFromGdl((Gfx *)scratch, expanded_size, (Gfx *)dst, NULL);
#ifndef TARGET_N64
    { extern s32 g_LightDbgRoomLoad; g_LightDbgRoomLoad = 0; }
#endif

    if (expanded_size < size) {
        expanded_size = size;
    }

#ifndef TARGET_N64
    { /* diagnostic (GE_DUMP_BOND): dump the first commands of the final
       * room GDL exactly as the renderer will walk them - the room-texture
       * smear investigation. First two rooms only. */
        extern char *getenv(const char *);
        static int dumped;
        if (dumped < 2 && getenv("GE_DUMP_BOND") != NULL) {
            extern void ge_dbg_gfxdump(const char *tag, void *gdl, int count);
            dumped++;
            ge_dbg_gfxdump("roomgdl", dst, 24);
        }
    }
#endif

    roominfo->ptr_expanded_mapping_info = dst;
    roominfo->usize_primary_DL_binary = expanded_size;

    // Return the uncompressed data size.
    return expanded_size;
}


/**
 * Address: 7F0B61DC
 *
 * Load and decompress a room's secondary display list data.
 *
 * On success, roominfo->ptr_secondary_expanded_mapping_info is set to dst,
 * and roominfo->usize_secondary_DL_binary is set to the returned size.
 */
s32 bgLoadRoomSecondaryGdl(s32 roomnum, u8 *dst, s32 allocsize)
{
    s_room_info *roominfo;
    s32 size;
    s32 fileoffset;
    u8 *scratch;
    s32 expanded_size;

    roominfo = &g_BgRoomInfo[roomnum];

    size = roominfo->csize_secondary_DL_binary;
    size = (size + 0xf) & ~0xf; // Align to 16 bytes

    /**
     * Check if there is enough room to temporarily place the compressed data
     * at the end of the available buffer. Return -1 if there's not.
     */
    if (allocsize < size + 0x20) {
        return -1;
    }

    // Load the compressed data into the end of the buffer, starting at dst.
    scratch = dst + (allocsize - size);

#ifdef TARGET_N64
    fileoffset = (s32)((u8 *)ptr_bgdata_room_fileposition_list[roomnum].pSecMappingBin + ptr_bg_data)  - ptr_bg_data;
    fileoffset += 0xf1000000;
#else
    /* integer domain (see bgLoadRoomVtxData) */
    fileoffset = (s32)((u32)(uintptr_t)ptr_bgdata_room_fileposition_list[roomnum].pSecMappingBin + 0xf1000000u);
#endif

    obLoadBGFileBytesAtOffset(levelinfotable[levelentry_index].bg_seg_filename, scratch, fileoffset, size);

    // Decompress from the end-of-buffer location at dst.
    expanded_size = bgDecompress(scratch, dst);

    /**
     * Copy the decompressed GDL back to the end of the buffer as scratch.
     * texLoadFromGdl can then read from scratch and write the final
     * texture-processed GDL/data back to dst.
     */
    scratch = dst + (allocsize - expanded_size / 8 * sizeof(Gfx));

    texCopyGdls((Gfx *)dst, (Gfx *)scratch, expanded_size);
    expanded_size = expanded_size / 8 * sizeof(Gfx);

#ifndef TARGET_N64
    { extern s32 g_LightDbgRoomLoad; g_LightDbgRoomLoad = 1; }
#endif
    size = texLoadFromGdl((Gfx *)scratch, expanded_size, (Gfx *)dst, NULL);
#ifndef TARGET_N64
    { extern s32 g_LightDbgRoomLoad; g_LightDbgRoomLoad = 0; }
#endif

    if (expanded_size < size) {
        expanded_size = size;
    }

    roominfo->ptr_secondary_expanded_mapping_info = dst;
    roominfo->usize_secondary_DL_binary = expanded_size;

    // Return the uncompressed data size.
    return expanded_size;
}


s32 bgCheckIfRoomModelNeedsLoad(s32 roomID)
{
    g_BgRoomInfo[roomID].field_35 = 1;
    if (g_BgRoomInfo[roomID].model_bin_loaded == 0)
    {
        bgLoadRoomModelData(roomID);
        return 1;
    }
    return 0;
}


#ifndef TARGET_N64
/* ==================== GE_ROOM_AUDIT (set GE_ROOM_AUDIT=1) ====================
 * Diagnostic for rooms getting corrupted when traversing portals. Rooms stream
 * in and out of the mema heap, so corruption means one of:
 *   (a) a reload outgrew its cached block and stomped the neighbouring mema block;
 *   (b) a stale writer scribbled into a loaded room's block;
 *   (c) mema bookkeeping damage (bad free or overlap) gave two owners the same bytes.
 * Every load/unload is logged with its block range and section sizes, size
 * drift and overrun are flagged, and a host-side shadow copy of each loaded
 * block is compared once per frame (portRoomAuditTick, driven from pcsched),
 * reporting any change with its offset, section and old/new bytes. mema.c adds
 * free-range validation under the same flag.
 * Expected benign trip: shooting a light fixture darkens vertex colours in
 * place (sect=vtx, small range); anything else is the bug. Output goes to
 * stderr with prefix "roomaudit:". */

#define ROOMAUDIT_MAX_ROOMS 512

static struct {
    u8 *base;
    u32 size;
    u8 *shadow;
} s_RoomAudit[ROOMAUDIT_MAX_ROOMS];
static u32 s_RoomAuditFrame;

s32 portRoomAuditEnabled(void)
{
    static s32 on = -1;
    if (on < 0) {
        on = (getenv("GE_ROOM_AUDIT") != NULL);
    }
    return on;
}

u32 portRoomAuditGetFrame(void)
{
    return s_RoomAuditFrame;
}

/* Which section of the room block an offset falls in, for triage. */
static const char *roomAuditSection(s32 roomID, u32 off)
{
    s_room_info *r = &g_BgRoomInfo[roomID];
    u8 *p = s_RoomAudit[roomID].base + off;

    if (r->vertices != NULL &&
        p >= (u8 *)r->vertices &&
        p <  (u8 *)r->vertices + r->usize_point_index_binary) {
        return "vtx";
    }
    if (r->ptr_expanded_mapping_info != NULL &&
        p >= (u8 *)r->ptr_expanded_mapping_info &&
        p <  (u8 *)r->ptr_expanded_mapping_info + r->usize_primary_DL_binary) {
        return "priGdl";
    }
    if (r->ptr_secondary_expanded_mapping_info != NULL &&
        p >= (u8 *)r->ptr_secondary_expanded_mapping_info &&
        p <  (u8 *)r->ptr_secondary_expanded_mapping_info + r->usize_secondary_DL_binary) {
        return "secGdl";
    }
    return "slack";
}

static void roomAuditDrop(s32 roomID)
{
    free(s_RoomAudit[roomID].shadow);
    s_RoomAudit[roomID].shadow = NULL;
    s_RoomAudit[roomID].base = NULL;
    s_RoomAudit[roomID].size = 0;
}

static void roomAuditArm(s32 roomID)
{
    u8 *base = (u8 *)g_BgRoomInfo[roomID].vertices;
    u32 size = (u32)g_BgRoomInfo[roomID].cur_room_totalsize;

    if (base == NULL) {
        base = (u8 *)g_BgRoomInfo[roomID].ptr_expanded_mapping_info;
    }

    roomAuditDrop(roomID);

    if (base != NULL && size != 0) {
        s_RoomAudit[roomID].shadow = (u8 *)malloc(size);
        if (s_RoomAudit[roomID].shadow != NULL) {
            memcpy(s_RoomAudit[roomID].shadow, base, size);
            s_RoomAudit[roomID].base = base;
            s_RoomAudit[roomID].size = size;
        }
    }
}

/* Compare a room's block against its shadow; report and re-arm on change. */
static void roomAuditVerify(s32 roomID, const char *when)
{
    u8 *base = s_RoomAudit[roomID].base;
    u8 *shadow = s_RoomAudit[roomID].shadow;
    u32 size = s_RoomAudit[roomID].size;
    u32 lo, hi, i, n;

    if (base == NULL || shadow == NULL || size == 0) {
        return;
    }
    if (memcmp(shadow, base, size) == 0) {
        return;
    }

    for (lo = 0; lo < size && base[lo] == shadow[lo]; lo++);
    for (hi = size; hi > 0 && base[hi - 1] == shadow[hi - 1]; hi--);

    fprintf(stderr,
            "roomaudit: *** ROOM DATA CHANGED r=%d (%s) f=%u base=%p off=%06x..%06x/%06x sect=%s..%s\n",
            roomID, when, s_RoomAuditFrame, (void *)base,
            lo, hi - 1, size, roomAuditSection(roomID, lo), roomAuditSection(roomID, hi - 1));

    n = hi - lo;
    if (n > 16) {
        n = 16;
    }
    fprintf(stderr, "roomaudit:     old:");
    for (i = 0; i < n; i++) {
        fprintf(stderr, " %02x", shadow[lo + i]);
    }
    fprintf(stderr, "\nroomaudit:     new:");
    for (i = 0; i < n; i++) {
        fprintf(stderr, " %02x", base[lo + i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);

    memcpy(shadow, base, size); /* re-arm so the next writer is also caught */
}

static void portRoomAuditOnLoad(s32 roomID, u8 *data, s32 allocsize, s32 used, s32 cachedwas)
{
    s32 total;

    if (!portRoomAuditEnabled() || roomID < 0 || roomID >= ROOMAUDIT_MAX_ROOMS) {
        return;
    }

    total = (used + 0x20) & ~0xf;

    fprintf(stderr,
            "roomaudit: load r=%d f=%u base=%p alloc=%06x total=%06x (vtx=%06x pri=%06x sec=%06x) cached_was=%06x\n",
            roomID, s_RoomAuditFrame, (void *)data, allocsize, total,
            g_BgRoomInfo[roomID].usize_point_index_binary,
            g_BgRoomInfo[roomID].usize_primary_DL_binary,
            g_BgRoomInfo[roomID].usize_secondary_DL_binary,
            cachedwas);

    if (total > allocsize) {
        fprintf(stderr,
                "roomaudit: *** BLOCK OVERRUN r=%d wrote %06x into a %06x block (+%d bytes past the end)\n",
                roomID, total, allocsize, total - allocsize);
    }
    if (cachedwas > 0 && total != cachedwas) {
        fprintf(stderr,
                "roomaudit: *** RELOAD SIZE DRIFT r=%d cached=%06x rebuilt=%06x (%+d bytes)%s\n",
                roomID, cachedwas, total, total - cachedwas,
                (total > cachedwas) ? " - neighbouring mema block was stomped" : "");
    }
    fflush(stderr);

    roomAuditArm(roomID);
}

static void portRoomAuditOnFree(s32 roomID)
{
    if (!portRoomAuditEnabled() || roomID < 0 || roomID >= ROOMAUDIT_MAX_ROOMS) {
        return;
    }

    roomAuditVerify(roomID, "at-free"); /* last integrity check before the block is recycled */

    fprintf(stderr, "roomaudit: free r=%d f=%u base=%p total=%06x\n",
            roomID, s_RoomAuditFrame, (void *)s_RoomAudit[roomID].base,
            s_RoomAudit[roomID].size);
    fflush(stderr);

    roomAuditDrop(roomID);
}

/* From memaReset: the whole heap is being recycled (stage transition) -
 * every shadow is stale, drop them before the tick can misread them. */
void portRoomAuditOnHeapReset(void)
{
    s32 i;

    for (i = 0; i < ROOMAUDIT_MAX_ROOMS; i++) {
        if (s_RoomAudit[i].shadow != NULL) {
            roomAuditDrop(i);
        }
    }
}

/* Once per frame from pcsched: sweep every loaded room's block. */
void portRoomAuditTick(void)
{
    s32 i;
    s32 max;

    if (!portRoomAuditEnabled()) {
        return;
    }

    s_RoomAuditFrame++;

    max = g_MaxNumRooms;
    if (max > ROOMAUDIT_MAX_ROOMS) {
        max = ROOMAUDIT_MAX_ROOMS;
    }

    for (i = 1; i < max; i++) {
        if (s_RoomAudit[i].shadow == NULL) {
            continue;
        }
        if (g_BgRoomInfo[i].model_bin_loaded == 0) {
            /* unloaded without delete_room_data (stage reset) - drop quietly */
            roomAuditDrop(i);
            continue;
        }
        roomAuditVerify(i, "tick");
    }

    if (max > 1) { /* a stage is up: also watch the room-matrix slot pool */
        extern void portRoomMtxAuditScan(u32 frame);
        extern void portRoomMtxDrawnFlush(u32 frame);
        portRoomMtxAuditScan(s_RoomAuditFrame);
        portRoomMtxDrawnFlush(s_RoomAuditFrame);
    }

    /* Audit of the portal-vis walk's output: g_BgNumberOfRoomsDrawn is the list
     * of rooms to draw, with their draw order and the screen bbox each is
     * scissored to. It is logged whenever the room-id set changes, since a
     * collapsed list means a walk fault and degenerate bboxes mean a
     * projection/scissor fault. */
    if (max > 1) {
        static s32 prevIds[204];
        static s32 prevN = -1;
        s32 n = g_BgNumberOfRoomsDrawn;
        s32 changed = 0;
        s32 j;

        if (n > 204) {
            n = 204;
        }
        if (n != prevN) {
            changed = 1;
        } else {
            for (j = 0; j < n; j++) {
                if (dword_CODE_bss_8007FFA0[j].roomid != prevIds[j]) {
                    changed = 1;
                    break;
                }
            }
        }

        if (changed && n > 0) {
            struct player *p = g_playerPointers[0];

            fprintf(stderr, "roomaudit: vis list cam=%d n=%d:",
                    (p != NULL) ? p->curRoomIndex : -1, n);
            for (j = 0; j < n && j < 24; j++) {
                fprintf(stderr, " r%d o%d [%.0f,%.0f..%.0f,%.0f]",
                        dword_CODE_bss_8007FFA0[j].roomid,
                        dword_CODE_bss_8007FFA0[j].unk1,
                        dword_CODE_bss_8007FFA0[j].bbox.min.x,
                        dword_CODE_bss_8007FFA0[j].bbox.min.y,
                        dword_CODE_bss_8007FFA0[j].bbox.max.x,
                        dword_CODE_bss_8007FFA0[j].bbox.max.y);
            }
            fprintf(stderr, " f=%u\n", s_RoomAuditFrame);
            fflush(stderr);
        }

        prevN = n;
        for (j = 0; j < n; j++) {
            prevIds[j] = dword_CODE_bss_8007FFA0[j].roomid;
        }
    }
}
#endif /* !TARGET_N64 */


/*
* Allocates memory for room and update its display lists
*
* When a room is first allocated, the game will pick the largest block
* available. It doesn't know the size of the decompressed asset as its
* size is not stored as part of the GZIP format. It will then shrink
* the allocated block to the correct size. The size is cached for the
* next time the room is reloaded.
*
* Address: 7F0B6368
*/
void bgLoadRoomModelData(s32 roomID)
{
    /*
     * Keep this prototype visible here for IDO codegen.
     * Without it, the memaRealloc call below schedules a2 too early.
     */
    s32 memaRealloc(intptr_t addr, u32 oldsize, u32 newsize);

    s32 allocsize;
    s32 used;
    s32 result;
    u8 *data;
#ifndef TARGET_N64
    s32 auditCachedWas; /* GE_ROOM_AUDIT: cached size before this load overwrites it */
    extern unsigned long long portProfNow(void);
    extern void portHitchNote(int cat, unsigned long long us);
    unsigned long long hitchT0 = 0; /* hitchlog.c: a room that really loaded */
#endif

    used = 0;

    // Room ID is out of range?
    if (roomID >= g_MaxNumRooms) goto end;

    // Room is already loaded?
    if (g_BgRoomInfo[roomID].model_bin_loaded) goto end;

#ifndef TARGET_N64
    auditCachedWas = g_BgRoomInfo[roomID].cur_room_totalsize;
    hitchT0 = portProfNow();
#endif

    // Get the cached file size. Is zero when the size is not yet known.
    allocsize = g_BgRoomInfo[roomID].cur_room_totalsize;

    if (allocsize > 0)
    {
        // Unknown debug code
        if (get_debug_joy2detailedit_flag())
        {
            allocsize += 0x400;
        }
    }
    else
    {
        // On a first allocation, we'll allocate the largest memory block available.
        allocsize = memaGetLongestFree();
    }

    /**
    * Allocate one contiguous block for vertices and display lists.
    */
    data = memaAlloc(allocsize);

    if (data == NULL) goto end;

    if (g_BgRoomInfo[roomID].csize_point_index_binary)
    {
        result = bgLoadRoomVtxData(roomID, data, allocsize);

        if (result >= 0)
        {
            used = result;
            redarken_lights_in_room(roomID);
        }
#ifndef TARGET_N64
        else
        {
            goto fail;
        }
#endif
    }
    else
    {
        g_BgRoomInfo[roomID].vertices = NULL;
        g_BgRoomInfo[roomID].usize_point_index_binary = 0;
    }

    /**
     * Append the primary display list after the vertex data.
     */
    if (g_BgRoomInfo[roomID].csize_primary_DL_binary)
    {
        result = bgLoadRoomPrimaryGdl(roomID, data + used, allocsize - used);

        if (result >= 0)
        {
            used += result;
        }
#ifndef TARGET_N64
        else
        {
            goto fail;
        }
#endif
    }
#ifndef TARGET_N64
    else
    {
        /* A room with no primary DL left ptr_expanded_mapping_info stale or
         * NULL (only the vertex branch nulls its pointer), and the unguarded
         * primary LUT pass below then sentinel-walked from NULL, crashing MP
         * Complex in 4P. Mirror the vertex else-branch; the census below names
         * rooms that are not legitimately empty. */
        g_BgRoomInfo[roomID].ptr_expanded_mapping_info = NULL;
        g_BgRoomInfo[roomID].usize_primary_DL_binary = 0;
        fprintf(stderr, "ge: bg: room %d has NO PRIMARY DL (csize=0) - renders empty\n", roomID);
    }
#endif

    /**
     * Append the secondary display list.
     */
    if (g_BgRoomInfo[roomID].csize_secondary_DL_binary)
    {
        result = bgLoadRoomSecondaryGdl(roomID, data + used, allocsize - used);

        if (result > 0)
        {
            used += result;
        }
#ifndef TARGET_N64
        else if (result < 0)
        {
            goto fail;
        }
#endif
    }
    else
    {
        g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info = NULL;
    }

    g_BgRoomInfo[roomID].cur_room_totalsize = ((used + 0x20) & ~0xf);
    g_BgRoomInfo[roomID].model_bin_loaded = 1;

    // If wasted space is detected, shrink allocated memory block.
    if (allocsize != ((used + 0x20) & ~0xf))
    {
        memaRealloc((intptr_t)data, allocsize, ((used + 0x20) & ~0xf));
    }

    // Same branches, only the LUT parameter changes
    if (g_FogSkyIsEnabled)
    {
#ifndef TARGET_N64
        if (g_BgRoomInfo[roomID].ptr_expanded_mapping_info)
#endif
        bgApplyDynamicCCRMLUT(
            g_BgRoomInfo[roomID].ptr_expanded_mapping_info,
            (Gfx *)((u8 *)g_BgRoomInfo[roomID].ptr_expanded_mapping_info + g_BgRoomInfo[roomID].usize_primary_DL_binary),
            1);

        if (g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info)
        {
            bgApplyDynamicCCRMLUT(
                g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info,
                (Gfx *)((u8 *)g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info + g_BgRoomInfo[roomID].usize_secondary_DL_binary),
                5);
        }
    }
    else
    {
#ifndef TARGET_N64
        if (g_BgRoomInfo[roomID].ptr_expanded_mapping_info)
#endif
        bgApplyDynamicCCRMLUT(
            g_BgRoomInfo[roomID].ptr_expanded_mapping_info,
            (Gfx *)((u8 *)g_BgRoomInfo[roomID].ptr_expanded_mapping_info + g_BgRoomInfo[roomID].usize_primary_DL_binary),
            6);

        if (g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info)
        {
            bgApplyDynamicCCRMLUT(
                g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info,
                (Gfx *)((u8 *)g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info + g_BgRoomInfo[roomID].usize_secondary_DL_binary),
                7);
        }
    }

    bgBuildRoomVtxBounds(roomID);
    roomsHandleStateDebugging();

#ifndef TARGET_N64
    portRoomAuditOnLoad(roomID, data, allocsize, used, auditCachedWas);
#endif

end:;

#ifndef TARGET_N64
    if (hitchT0 != 0)
    {
        portHitchNote(0 /* rooms */, portProfNow() - hitchT0);
    }
    return;

fail:
    portHitchNote(0 /* rooms */, portProfNow() - hitchT0);
    memaFree(data, allocsize);
    g_BgRoomInfo[roomID].vertices = NULL;
    g_BgRoomInfo[roomID].ptr_expanded_mapping_info = NULL;
    g_BgRoomInfo[roomID].ptr_secondary_expanded_mapping_info = NULL;
    g_BgRoomInfo[roomID].model_bin_loaded = 0;
    g_BgRoomInfo[roomID].cur_room_totalsize = 0;
#endif
}


/**
 * Given a room, frees all dynamically allocated data for that room and marks the room as unloaded.
 */
void delete_room_data(s32 roomID)
{
    s_room_info *room = &g_BgRoomInfo[roomID];
    s32 size;
    s32 size2;
    Vtx *pointindex;

#ifndef TARGET_N64
    portRoomAuditOnFree(roomID); /* GE_ROOM_AUDIT: verify + log before recycling */
    {
        extern void portRoomCacheRoomFreed(s32 room);
        portRoomCacheRoomFreed(roomID); /* its list and vertices are about to be recycled (roc_hooks.c) */
    }
#endif

    if (room->vtx_batch_bounds != NULL) {
        memaFree(
            room->vtx_batch_bounds,
            ((room->num_vtx_batch_bounds * sizeof(RoomVtxBatchBounds)) + 0xf) & ~0xf);

        room->vtx_batch_bounds = NULL;
    }

    if (room->cur_room_totalsize > 0) {
        size = room->cur_room_totalsize;
        pointindex = room->vertices;

        if (pointindex != NULL)
        {
            size2 = room->cur_room_totalsize;
            memaFree(pointindex, size2);
            room->vertices = NULL;
        }
        else
        {
            memaFree(room->ptr_expanded_mapping_info, size);
            room->vertices = NULL;
        }

        room->ptr_expanded_mapping_info = NULL;
        room->ptr_secondary_expanded_mapping_info = NULL;
    }

    room->model_bin_loaded = 0;
    roomsHandleStateDebugging();
}


/**
 * Immediately unload all loaded rooms.
 * Used for stage cleanup.
 */
void unload_rooms(void)
{
    s32 i;

    for(i = 1; i < g_MaxNumRooms; i++)
    {
        if (g_BgRoomInfo[i].model_bin_loaded)
        {
            delete_room_data(i);
        }
    }
}


/**
 * Address: 7F0B66E8
 *
 * Ages loaded rooms that are no longer marked active, then unloads them
 * once their unload delay expires.
 */
void bgRoomsTickUnload(void)
{
    s32 i;

    for(i = 1; i < g_MaxNumRooms; i++)
    {
        if (g_BgRoomInfo[i].field_35 == 0)
        {
            if (g_BgRoomInfo[i].model_bin_loaded == 4)
            {
                delete_room_data(i);
            }
            else if (g_BgRoomInfo[i].model_bin_loaded != 0)
            {
                g_BgRoomInfo[i].model_bin_loaded = g_BgRoomInfo[i].model_bin_loaded + 1;
            }
        }
    }
}


/**
 * Address 7F0B677C
 *
 * Render a room's primary (solid) geometry.
 * Ensures the room's bg data is loaded if budget allows, then appends its display list.
 * Also resets the age of rendered rooms so bgRoomsTickUnload won't unload it.
*/
Gfx *bgRenderRoomPrimary(Gfx *gdl, s32 room_index)
{
    if (room_index >= g_MaxNumRooms)
    {
        return gdl;
    }

    if ((D_8004485C != 0) || (D_80044858 == (room_index % 10)))
    {
        if (g_BgRoomInfo[room_index].model_bin_loaded == 0)
        {
            if (g_RoomLoadBudget > 0)
            {
                g_RoomLoadBudget--;
                bgLoadRoomModelData(room_index);
            }
        }

        if (g_BgRoomInfo[room_index].model_bin_loaded == 0)
        {
#ifndef TARGET_N64
            { /* GE_ROOM_AUDIT: room reached the draw list but its
               * geometry never loaded (possibly out of load budget), so it is invisible */
                extern s32 portRoomAuditEnabled(void);
                extern u32 portRoomAuditGetFrame(void);
                static u32 lastNoLoad;
                u32 fr;
                if (portRoomAuditEnabled()) {
                    fr = portRoomAuditGetFrame();
                    if (fr - lastNoLoad >= 30u) {
                        lastNoLoad = fr;
                        fprintf(stderr,
                                "roomaudit: *** DRAW UNLOADED room=%d p%d budget=%d f=%u\n",
                                room_index, get_cur_playernum(), g_RoomLoadBudget, fr);
                        fflush(stderr);
                    }
                }
            }
#endif
            return gdl;
        }
#ifndef TARGET_N64
        else if (g_BgRoomInfo[room_index].ptr_expanded_mapping_info == NULL)
        {
            /* no-primary-DL room: loaded but nothing to draw - emitting
             * a NULL display list would crash the interpreter */
            return gdl;
        }
#endif
        else
        {
            gdl = applyRoomMatrixToDisplayList(gdl, room_index);

            gSPSegment(gdl++, SPSEGMENT_BG_VTX, OS_K0_TO_PHYSICAL(g_BgRoomInfo[room_index].vertices));
#ifndef TARGET_N64
            { /* static room geometry cache (port/fast3d/gfx_roomcache.inl): the
               * renderer answers this OPA list from a retained copy after its
               * first run; the marker names the next G_DL (roc_hooks.c) */
                extern Gfx *portRoomCacheEmit(Gfx *gdl, s32 room);
                gdl = portRoomCacheEmit(gdl, room_index);
            }
#endif
            gSPDisplayList(gdl++, OS_K0_TO_PHYSICAL(g_BgRoomInfo[room_index].ptr_expanded_mapping_info));

            // Set the room's state to "loaded"
            g_BgRoomInfo[room_index].model_bin_loaded = 1;
        }
    }

    return gdl;
}


/**
 * Address 7F0B6898
 *
 * Render a room's secondary (transparent) geometry.
*/
Gfx *bgRenderRoomSecondary(Gfx *gdl, s32 room_index)
{
    if (room_index >= g_MaxNumRooms)
    {
        return gdl;
    }

    // Return if the room has no secondary geometry.
    if (g_BgRoomInfo[room_index].ptr_secondary_expanded_mapping_info == 0)
    {
        return gdl;
    }
    else if ((D_8004485C != 0) || (D_80044858 == (room_index % 10)))
    {
        if (g_BgRoomInfo[room_index].model_bin_loaded != 0)
        {
            gdl = applyRoomMatrixToDisplayList(gdl, room_index);

            gSPSegment(gdl++, SPSEGMENT_BG_VTX, OS_K0_TO_PHYSICAL(g_BgRoomInfo[room_index].vertices));
#ifndef TARGET_N64
            { /* the translucent list is a room of its own in the cache (room +
               * 512): its XLU triangles replay from a retained buffer through
               * the deferred sorter */
                extern Gfx *portRoomCacheEmit(Gfx *gdl, s32 room);
                gdl = portRoomCacheEmit(gdl, room_index + 512);
            }
#endif
            gSPDisplayList(gdl++, OS_K0_TO_PHYSICAL(g_BgRoomInfo[room_index].ptr_secondary_expanded_mapping_info));

            // Set the room's state to "loaded"
            g_BgRoomInfo[room_index].model_bin_loaded = 1;
        }
        else
        {
            bgLoadRoomModelData(room_index);
        }
    }

    return gdl;
}


/*
* Build world space bounds for each vertex batch loaded by the room's display list.
* Address: 7F0B6994
*/
#define ALIGN16(val) (((val) + 0xF) & ~0xF)
#define	SEGMENT_OFFSET(a)	((unsigned int)(a) & 0x00ffffff)

void bgBuildRoomVtxBounds(s32 roomID)
{
    s32 cmdindex;
    Gfx *gdl;
    Vtx *vertices;
    RoomVtxBatchBounds *points;
    s32 numpoints;
    RoomVtxBatchBounds *point;
    s32 i;
    s32 numvertices;
    Vtx *vtx;

    // Check if a cached bounding box is already present for this room
    if (g_BgRoomInfo[roomID].vtx_batch_bounds != NULL)
    {
        return;
    }

    gdl = g_BgRoomInfo[roomID].ptr_expanded_mapping_info;

    vertices = g_BgRoomInfo[roomID].vertices;

#ifndef TARGET_N64
    /* no-primary-DL room (see bgLoadRoomModelData): nothing to walk or
     * bound - leave the cache empty */
    if (gdl == NULL || vertices == NULL)
    {
        return;
    }
#endif

    cmdindex = 0;
    numpoints = 0;

    /* (s8) is load-bearing: the original signed `int cmd:8` bitfield
     * sign-extends, and F3D_OLD's G_ENDDL (G_IMMFIRST-7) is NEGATIVE -
     * an unsigned extract never equals it and the walker runs off the DL */
    while ((s8)(gdl[cmdindex].words.w0 >> 24) != G_ENDDL)
    {
        if ((s8)(gdl[cmdindex].words.w0 >> 24) == G_VTX)
        {
            numpoints++;
        }
        cmdindex++;
    }

    points = memaAlloc(ALIGN16(numpoints * sizeof(RoomVtxBatchBounds)));

    if (ALIGN16(numpoints * sizeof(RoomVtxBatchBounds))) {}

    if (points == NULL)
    {
        return;
    }

    g_BgRoomInfo[roomID].vtx_batch_bounds = points;
    g_BgRoomInfo[roomID].num_vtx_batch_bounds = numpoints;

    numpoints = 0;
    cmdindex = 0;

    /* (s8) load-bearing - see the walker above */
    while ((s8)(gdl[cmdindex].words.w0 >> 24) != G_ENDDL)
    {
        if ((s8)(gdl[cmdindex].words.w0 >> 24) == G_VTX)
        {
            point = &points[numpoints];

            points[numpoints].gdlindex = cmdindex;

            for (i = 0; i < 3; i++) 
            {
                points[numpoints].min[i] = 0x7fff;
                points[numpoints].max[i] = -0x8000;
            }

            numvertices = ((((gdl[cmdindex].words.w0 >> 16) & 0xFF) >> 4) & 0xf) + 1;

            vtx = (Vtx *)(SEGMENT_OFFSET(gdl[cmdindex].words.w1) + (uintptr_t)vertices);

#ifndef TARGET_N64
            { /* bring-up (sanitizer sweep catch #6): misaligned Vtx deref -
               * dump the inputs for the first few batches to see whether w1
               * is a segment word, a host pointer, or byte-scrambled */
                static s32 n;
                if (((uintptr_t) vtx & 3) != 0) {
                    if (n < 24) {
                        n++;
                        printf("ge: vtxbounds room=%d cmd=%d w0=%08x w1=%08x vertices=%p vtx=%p\n",
                               roomID, cmdindex, gdl[cmdindex].words.w0,
                               gdl[cmdindex].words.w1, (void *) vertices, (void *) vtx);
                    }
                    break; /* skip the batch rather than fault */
                }
            }
#endif

            for (i = 0; i < numvertices; i++)
            {
                if (points[numpoints].xmin > vtx[i].v.ob[0])
                {
                    points[numpoints].xmin = vtx[i].v.ob[0];
                }

                if (points[numpoints].ymin > vtx[i].v.ob[1])
                {
                    points[numpoints].ymin = vtx[i].v.ob[1];
                }

                if (points[numpoints].zmin > vtx[i].v.ob[2])
                {
                    points[numpoints].zmin = vtx[i].v.ob[2];
                }

                if (points[numpoints].xmax < vtx[i].v.ob[0])
                {
                    points[numpoints].xmax = vtx[i].v.ob[0];
                }

                if (points[numpoints].ymax < vtx[i].v.ob[1])
                {
                    points[numpoints].ymax = vtx[i].v.ob[1];
                }

                if (points[numpoints].zmax < vtx[i].v.ob[2])
                {
                    points[numpoints].zmax = vtx[i].v.ob[2];
                }
            }


            if (points[numpoints].xmin == points[numpoints].xmax)
            {
                points[numpoints].xmax++;
            }

            if (points[numpoints].ymin == points[numpoints].ymax)
            {
                points[numpoints].ymax++;
            }

            if (points[numpoints].zmin == points[numpoints].zmax)
            {
                points[numpoints].zmax++;
            }


            points[numpoints].xmin += (s32)ptr_bgdata_room_fileposition_list[roomID].pos.x;
            points[numpoints].ymin += (s32)ptr_bgdata_room_fileposition_list[roomID].pos.y;
            points[numpoints].zmin += (s32)ptr_bgdata_room_fileposition_list[roomID].pos.z;

            points[numpoints].xmax += (s32)ptr_bgdata_room_fileposition_list[roomID].pos.x;
            points[numpoints].ymax += (s32)ptr_bgdata_room_fileposition_list[roomID].pos.y;
            points[numpoints].zmax += (s32)ptr_bgdata_room_fileposition_list[roomID].pos.z;
            numpoints++;
        }
        cmdindex++;
    }
}

#undef ALIGN16
#undef SEGMENT_OFFSET


/**
 * Slab method ray vs axis aligned bounding box test, written to be division-free.
 */
bool bgTestRayIntersectsBbox(coord3d *origin, coord3d *dir, s32 *bbox_min, s32 *bbox_max)
{
    coord3d bbox_min_f;
    coord3d bbox_max_f;
    u32 stack[4];
    f32 f0;
    f32 f0_2;
    f32 f2;
    f32 f2_2;
    f32 f6;
    f32 f10;
    f32 sp34;
    f32 sp30;
    f32 f16;
    f32 f18;
    f32 f18_2;
    f32 sp20;
    f32 f12;
    f32 f12_2;
    f32 f14;
    f32 f14_2;

    bbox_min_f.f[0] = bbox_min[0];
    bbox_min_f.f[1] = bbox_min[1];
    bbox_min_f.f[2] = bbox_min[2];

    bbox_max_f.f[0] = bbox_max[0];
    bbox_max_f.f[1] = bbox_max[1];
    bbox_max_f.f[2] = bbox_max[2];

    // x
    f18 = dir->x;
    f16 = bbox_max_f.x - origin->x;
    f14 = bbox_min_f.x - origin->x;

    if (f18 < 0.0f)
    {
        f18 = -f18;
        f14 = -f14;
        f16 = -f16;
    }

    if (f14 < 0.0f && f16 < 0.0f)
    {
        return FALSE;
    }

    if (f16 < f14)
    {
        f32 tmp = f14;
        f14 = f16;
        f16 = tmp;
    }

    // y
    f12 = dir->y;
    f2 = bbox_max_f.y - origin->y;
    f0 = bbox_min_f.y - origin->y;

    if (f12 < 0.0f)
    {
        f12 = -f12;
        f0 = -f0;
        f2 = -f2;
    }

    if (f0 < 0.0f && f2 < 0.0f)
    {
        return FALSE;
    }

    if (f2 < f0)
    {
        sp20 = f0;
        f0 = f2;
        f2 = sp20;
    }

    f6 = f14 * f12;
    f10 = f0 * f18;

    if (f10 < f6)
    {
        if (f2 * f18 < f6)
        {
            return FALSE;
        }

        sp34 = f14;
        sp30 = f18;
    }
    else
    {
        if (f16 * f12 < f10)
        {
            return FALSE;
        }

        sp34 = f0;
        sp30 = f12;
    }

    if (f16 * f12 < f2 * f18)
    {
        f0_2 = f16;
        f14_2 = f18;
    }
    else
    {
        f0_2 = f2;
        f14_2 = f12;
    }

    // z
    f2_2 = dir->z;
    f12_2 = bbox_max_f.z - origin->z;
    f18_2 = bbox_min_f.z - origin->z;

    if (f2_2 < 0.0f)
    {
        f2_2 = -f2_2;
        f18_2 = -f18_2;
        f12_2 = -f12_2;
    }

    if (f18_2 < 0.0f && f12_2 < 0.0f)
    {
        return FALSE;
    }

    if (f12_2 < f18_2)
    {
        f32 tmp = f18_2;
        f18_2 = f12_2;
        f12_2 = tmp;
    }

    if (sp34 * f2_2 < f18_2 * sp30)
    {
        if (f0_2 * f2_2 < f18_2 * f14_2)
        {
            return FALSE;
        }
    }
    else
    {
        if (f12_2 * sp30 < sp34 * f2_2)
        {
            return FALSE;
        }
    }

    return TRUE;
}


bool bgTestRayIntersectionInRoom(coord3d *from, coord3d *to, coord3d *dir, RoomVtxBatchBounds *point, s32 roomnum, struct HitThing *hitthing)
{
    Vertex *vtxbase;
    Vertex *v;

    union {
        s32 word;
        Vertex *vertices;
        s_room_info *roominfo;
    } temp;

    s32 op;
    s32 found;
    Gfx *gdl;
    HitThing hitbuf;
    Gfx *tcmd;
    s32 bestScore;
    s32 idx[3];
    s32 vtxoff;
    BoundVec bboxMin;
    BoundVec bboxMax;
    s32 *p;
    s32 i;
    s32 s2;
    s32 texnum;
    s32 dx;
    s32 dy;
    s32 dz;
    s32 dist;
    s32 idx2[3];
    s32 score;
    BoundVec bboxMin2;
    BoundVec bboxMax2;

    struct {
        s32 unused;
        s_room_info *roominfo;
        s32 padding[6];
    } local;

    gdl = (Gfx *) g_BgRoomInfo[roomnum].ptr_expanded_mapping_info;
    gdl = &gdl[point->gdlindex];
    /* Read command words through Gfx: native commands have pointer-sized operands. */
#ifdef TARGET_N64
    vtxoff = ((u8 *) gdl)[1] & 0xf;
#else
    vtxoff = (gdl->words.w0 >> 16) & 0xf;
#endif
    temp.vertices = (Vertex *)g_BgRoomInfo[roomnum].vertices;
    vtxbase = (Vertex *)((u8 *)temp.vertices + (gdl->words.w1 & 0x00ffffff));
    temp.roominfo = &g_BgRoomInfo[roomnum];
    bestScore = 0x7FFFFFFF;
    found = 0;
    gdl++;
#ifdef TARGET_N64
    op = *((s8 *) gdl);
#else
    op = (s8) (gdl->words.w0 >> 24);
#endif

    if ((op != G_VTX) && (op != ((s8) G_ENDDL)))
    {
        local.roominfo = temp.roominfo;

        do
        {
            if (op == ((s8) G_TRI1))
            {
                bboxMin = D_80044868;
                bboxMax = D_80044874;
                score = dist;
#ifdef TARGET_N64
                idx[0] = (((u8 *) gdl)[5] / 10) - vtxoff;
                idx[1] = (((u8 *) gdl)[6] / 10) - vtxoff;
                idx[2] = (((u8 *) gdl)[7] / 10) - vtxoff;
#else
                idx[0] = (((gdl->words.w1 >> 16) & 0xff) / 10) - vtxoff;
                idx[1] = (((gdl->words.w1 >> 8) & 0xff) / 10) - vtxoff;
                idx[2] = ((gdl->words.w1 & 0xff) / 10) - vtxoff;
#endif
                i = 0;
                
                do
                {
                    v = vtxbase;
                    v += idx[i];

                    if (v->coord.x < bboxMin.x)
                    {
                        bboxMin.x = v->coord.x;
                    }
                    if (bboxMax.x < v->coord.x)
                    {
                        bboxMax.x = v->coord.x;
                    }
                    if (v->coord.y < bboxMin.y)
                    {
                        bboxMin.y = v->coord.y;
                    }
                    if (bboxMax.y < v->coord.y)
                    {
                        bboxMax.y = v->coord.y;
                    }

                    if (roomnum);

                    if (v->coord.z < bboxMin.z)
                    {
                        bboxMin.z = v->coord.z;
                    }
                    if (bboxMax.z < v->coord.z)
                    {
                        bboxMax.z = v->coord.z;
                    }
                    i++;
                } 
                while (i < 3);

                bboxMin.x += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.x;
                bboxMin.y += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.y;
                bboxMin.z += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.z;
                bboxMax.x += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.x;
                bboxMax.y += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.y;
                bboxMax.z += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.z;

                if (bgTestRayIntersectsBbox(from, dir, (s32 *) (&bboxMin), (s32 *) (&bboxMax)))
                {
                    if (intersectRayTriangle((Vertex *)((intptr_t)vtxbase - (0 - (idx[0] << 4))), (Vertex *)((intptr_t)vtxbase - (0 - (idx[1] << 4))), (Vertex *)((intptr_t)vtxbase - (0 - (idx[2] << 4))), &ptr_bgdata_room_fileposition_list[roomnum].pos, from, to, dir, &hitbuf))
                    {
                        tcmd = gdl;
                        dx = ((s32) hitbuf.hitpos.x) - ((s32) from->x);
                        dy = ((s32) hitbuf.hitpos.y) - ((s32) from->y);
                        dz = ((s32) hitbuf.hitpos.z) - ((s32) from->z);
                        dist = ((dx * dx) + (dy * dy)) + (dz * dz);
                        score = dist;
                        found = 1;

#ifdef TARGET_N64
                        if (((*((u8 *) gdl)) != G_SETTIMG) && (dist || vtxbase || 1) && (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < gdl))
                        {
                            do
                            {
                                tcmd--;
                                if ((*((u8 *) tcmd)) == G_SETTIMG)
                                {
                                    break;
                                }
                            }
                            while (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < tcmd);
                        }

                        if (tcmd == ((Gfx *) local.roominfo->ptr_expanded_mapping_info))
                        {
                            texnum = -1;
                        }
                        else
                        {
                            temp.word = ((u32 *) tcmd)[1] - 8;
                            texnum = *((u16 *) (temp.word | 0x80000000));
                        }
#else
                        /* native opcode compare; the promoted SETTIMG w1 is
                         * a host pointer whose id halfword sits 8 bytes
                         * before the pixel data (no K0 mapping on PC) */
                        if (((gdl->words.w0 >> 24) != G_SETTIMG) && (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < gdl))
                        {
                            do
                            {
                                tcmd--;
                                if ((tcmd->words.w0 >> 24) == G_SETTIMG)
                                {
                                    break;
                                }
                            }
                            while (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < tcmd);
                        }

                        if (tcmd == ((Gfx *) local.roominfo->ptr_expanded_mapping_info) || (tcmd->words.w0 >> 24) != G_SETTIMG)
                        {
                            texnum = -1;
                        }
                        else
                        {
                            /* unpromoted stragglers keep segment words -
                             * classify before dereferencing (propobj.c twin) */
                            uintptr_t timg = tcmd->words.w1;

                            if (timg >= 0x10000000u && (timg >> 16) != 0xABCDu)
                            {
                                texnum = *((u16 *) (uintptr_t) (timg - 8));
                            }
                            else
                            {
                                texnum = -1;
                            }
                        }
#endif

                        if (check_if_imageID_is_light(texnum))
                        {
                            score = dist - 4;
                        }

                        if (dist);

                        // Texture 0x4FD is used for the light shafts that come through windows in Archives.
                        if ((score < bestScore) && (texnum != 0x4FD))
                        {
                            bestScore = score;
                            hitthing->hitpos.x = hitbuf.hitpos.x;
                            hitthing->hitpos.y = hitbuf.hitpos.y;
                            hitthing->hitpos.z = hitbuf.hitpos.z;
                            hitthing->normal.x = hitbuf.normal.x;
                            hitthing->normal.y = hitbuf.normal.y;
                            hitthing->normal.z = hitbuf.normal.z;
                            hitthing->vtx0 = &vtxbase[idx[0]];
                            hitthing->vtx1 = &vtxbase[idx[1]];
                            hitthing->vtx2 = &vtxbase[idx[2]];
                            hitthing->texturenum = texnum;
                            hitthing->tricmd = gdl;
                            hitthing->unk28 = 0;
                        }
                    }
                }
            }
            else
            {
                if (op == ((s8) G_TRI4))
                {
                    // Keep this line as-is for matching.
                    s2 = 0; do
                    {
                        bboxMin2 = D_80044880;
                        bboxMax2 = D_8004488C;

#ifdef TARGET_N64
                        if (s2 == 0)
                        {
                            idx2[0] = (((u32 *) gdl)[1] & 0xf) - vtxoff;
                            idx2[1] = (((u32) ((u8 *) gdl)[7]) >> 4) - vtxoff;
                            idx2[2] = (((u32 *) gdl)[0] & 0xf) - vtxoff;
                        }
                        else if (s2 == 1)
                        {
                            idx2[0] = (((u8 *) gdl)[6] & 0xf) - vtxoff;
                            idx2[1] = (((u32) ((u16 *) gdl)[3]) >> 12) - vtxoff;
                            idx2[2] = (((u32) ((u8 *) gdl)[3]) >> 4) - vtxoff;
                        }
                        else if (s2 == 2)
                        {
                            idx2[0] = (((u16 *) gdl)[2] & 0xf) - vtxoff;
                            idx2[1] = (((u32) ((u8 *) gdl)[5]) >> 4) - vtxoff;
                            idx2[2] = (((u8 *) gdl)[2] & 0xf) - vtxoff;
                        }
                        else
                        {
                            idx2[0] = (((u8 *) gdl)[4] & 0xf) - vtxoff;
                            idx2[1] = (((u32 *) gdl)[1] >> 28) - vtxoff;
                            idx2[2] = (((u32) ((u16 *) gdl)[1]) >> 12) - vtxoff;
                        }
#else
                        /* same nibbles via word math - see propobj.c twin */
                        if (s2 == 0)
                        {
                            idx2[0] = (gdl->words.w1 & 0xf) - vtxoff;
                            idx2[1] = ((gdl->words.w1 >> 4) & 0xf) - vtxoff;
                            idx2[2] = (gdl->words.w0 & 0xf) - vtxoff;
                        }
                        else if (s2 == 1)
                        {
                            idx2[0] = ((gdl->words.w1 >> 8) & 0xf) - vtxoff;
                            idx2[1] = ((gdl->words.w1 >> 12) & 0xf) - vtxoff;
                            idx2[2] = ((gdl->words.w0 >> 4) & 0xf) - vtxoff;
                        }
                        else if (s2 == 2)
                        {
                            idx2[0] = ((gdl->words.w1 >> 16) & 0xf) - vtxoff;
                            idx2[1] = ((gdl->words.w1 >> 20) & 0xf) - vtxoff;
                            idx2[2] = ((gdl->words.w0 >> 8) & 0xf) - vtxoff;
                        }
                        else
                        {
                            idx2[0] = ((gdl->words.w1 >> 24) & 0xf) - vtxoff;
                            idx2[1] = (gdl->words.w1 >> 28) - vtxoff;
                            idx2[2] = ((gdl->words.w0 >> 12) & 0xf) - vtxoff;
                        }
#endif

                        i = 0;

                        do
                        {
                            v = vtxbase;
                            v += idx2[i];
                            if (v->coord.x < bboxMin2.x)
                            {
                                bboxMin2.x = v->coord.x;
                            }
                            if (bboxMax2.x < v->coord.x)
                            {
                                bboxMax2.x = v->coord.x;
                            }
                            if (v->coord.y < bboxMin2.y)
                            {
                                bboxMin2.y = v->coord.y;
                            }
                            if (bboxMax2.y < v->coord.y)
                            {
                                bboxMax2.y = v->coord.y;
                            }
                            if (v->coord.z < bboxMin2.z)
                            {
                                bboxMin2.z = v->coord.z;
                            }
                            if (bboxMax2.z < v->coord.z)
                            {
                                bboxMax2.z = v->coord.z;
                            }
                            i++;
                        } 
                        while (i < 3);
                        
                        bboxMin2.x += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.x;
                        bboxMin2.y += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.y;
                        bboxMin2.z += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.z;
                        bboxMax2.x += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.x;
                        bboxMax2.y += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.y;
                        bboxMax2.z += (s32) ptr_bgdata_room_fileposition_list[roomnum].pos.z;

                        if (bgTestRayIntersectsBbox(from, dir, (s32 *) (&bboxMin2), (s32 *) (&bboxMax2)))
                        {
                            if (intersectRayTriangle((Vertex *)((intptr_t)vtxbase - (0 - (idx2[0] << 4))), (Vertex *)((intptr_t)vtxbase - (0 - (idx2[1] << 4))), (Vertex *)((intptr_t)vtxbase - (0 - (idx2[2] << 4))), &ptr_bgdata_room_fileposition_list[roomnum].pos, from, to, dir, &hitbuf))
                            {
                                tcmd = gdl;
                                dx = ((s32) hitbuf.hitpos.x) - ((s32) from->x);
                                dy = ((s32) hitbuf.hitpos.y) - ((s32) from->y);
                                dz = ((s32) hitbuf.hitpos.z) - ((s32) from->z);
                                dist = ((dx * dx) + (dy * dy)) + (dz * dz);
                                score = dist;
                                found = 1;

#ifdef TARGET_N64
                                if (((*((u8 *) gdl)) != G_SETTIMG) && (dist || vtxbase || 1) && (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < gdl))
                                {
                                    do
                                    {
                                        tcmd--;
                                        if ((*((u8 *) tcmd)) == G_SETTIMG)
                                        {
                                            break;
                                        }
                                    }
                                    while (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < tcmd);
                                }

                                if (tcmd == ((Gfx *) local.roominfo->ptr_expanded_mapping_info))
                                {
                                    texnum = -1;
                                }
                                else
                                {
                                    temp.word = ((u32 *) tcmd)[1] - 8;
                                    texnum = *((u16 *) (temp.word | 0x80000000));
                                }
#else
                                /* see the G_TRI1 twin above */
                                if (((gdl->words.w0 >> 24) != G_SETTIMG) && (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < gdl))
                                {
                                    do
                                    {
                                        tcmd--;
                                        if ((tcmd->words.w0 >> 24) == G_SETTIMG)
                                        {
                                            break;
                                        }
                                    }
                                    while (((Gfx *) local.roominfo->ptr_expanded_mapping_info) < tcmd);
                                }

                                if (tcmd == ((Gfx *) local.roominfo->ptr_expanded_mapping_info) || (tcmd->words.w0 >> 24) != G_SETTIMG)
                                {
                                    texnum = -1;
                                }
                                else
                                {
                                    /* see the G_TRI1 twin above */
                                    uintptr_t timg = tcmd->words.w1;

                                    if (timg >= 0x10000000u && (timg >> 16) != 0xABCDu)
                                    {
                                        texnum = *((u16 *) (uintptr_t) (timg - 8));
                                    }
                                    else
                                    {
                                        texnum = -1;
                                    }
                                }
#endif

                                if (check_if_imageID_is_light(texnum))
                                {
                                    score = dist - 4;
                                }

                                if ((score || dist));

                                if ((score < bestScore) && (texnum != 0x4FD))
                                {
                                    bestScore = score;
                                    hitthing->hitpos.x = hitbuf.hitpos.x;
                                    hitthing->hitpos.y = hitbuf.hitpos.y;
                                    hitthing->hitpos.z = hitbuf.hitpos.z;
                                    hitthing->normal.x = hitbuf.normal.x;
                                    hitthing->normal.y = hitbuf.normal.y;
                                    hitthing->normal.z = hitbuf.normal.z;
                                    hitthing->vtx0 = &vtxbase[idx2[0]];
                                    hitthing->vtx1 = &vtxbase[idx2[1]];
                                    hitthing->vtx2 = &vtxbase[idx2[2]];
                                    hitthing->texturenum = texnum;
                                    hitthing->tricmd = gdl;
                                    hitthing->unk28 = s2 + 1;
                                }
                            }
                        }
                        s2++;
                    }
                    while (s2 != 4);
                }
            }

            gdl++;
#ifdef TARGET_N64
            op = *((s8 *) gdl);
#else
            op = (s8) (gdl->words.w0 >> 24);
#endif
        }
        while ((op != G_VTX) && (op != ((s8) G_ENDDL)));
    }

    return found;
}


/**
 * In order to make bgTestBulletHitBackground match, 4 bytes had to dropped from the regular HitThing struct.
 * There might be an alternate way of matching this that doesn't require the sub-struct.
 */
struct HitThingSub {
    coord3d hitpos;     // 0x00
    coord3d normal;     // 0x0c

    Vertex *vtx0;       // 0x18
    Vertex *vtx1;       // 0x1c
    Vertex *vtx2;       // 0x20

    Gfx *tricmd;        // 0x24 - display-list command associated with hit triangle

    s16 unk28;          // 0x28
    s16 texturenum;     // 0x2a
};


bool bgTestBulletHitBackground(coord3d *from, coord3d *to, s32 roomnum, struct HitThing *hit)
{
    RoomVtxBatchBounds *point;
    s16 numpoints;
    coord3d fromscaled;
    coord3d toscaled;
    coord3d dir;
    s32 i;
    struct HitThingSub tmp;
    f32 scale;
    s32 bestdist;
    s32 dist;
    bool found;
    s32 score;
    s32 dx;
    s32 dy;
    s32 dz;

    found = FALSE;
    bestdist = 0x7fffffff;

    scale = room_data_float1;
    fromscaled.x = from->x * scale;
    fromscaled.y = from->y * scale;
    fromscaled.z = from->z * scale;
    toscaled.x = to->x * scale;
    toscaled.y = to->y * scale;
    toscaled.z = to->z * scale;
    dir.x = toscaled.x - fromscaled.x;
    dir.y = toscaled.y - fromscaled.y;
    dir.z = toscaled.z - fromscaled.z;
    point = g_BgRoomInfo[roomnum].vtx_batch_bounds;
    point = g_BgRoomInfo[roomnum].vtx_batch_bounds;

    if (point == NULL)
    {
        return FALSE;
    }

    numpoints = g_BgRoomInfo[roomnum].num_vtx_batch_bounds;

    for (i = 0; i < numpoints; i++) 
    {
        if (!bgTestRayIntersectsBbox(&fromscaled, &dir, &point[i].xmin, &point[i].xmax)) 
        {
            continue;
        }

        if (!bgTestRayIntersectionInRoom(&fromscaled, &toscaled, &dir, &point[i], roomnum, (struct HitThing *)&tmp)) 
        {
            continue;
        }

        dx = tmp.hitpos.x - fromscaled.x;
        dy = tmp.hitpos.y - fromscaled.y;
        dz = tmp.hitpos.z - fromscaled.z;
        dist = score = dx * dx + dy * dy + dz * dz;
        found = TRUE;

        if (check_if_imageID_is_light(tmp.texturenum)) 
        {
            score = dist - 4;
        }

        if (score < bestdist) 
        {
            bestdist = score;
            hit->hitpos.x = tmp.hitpos.x;
            hit->hitpos.y = tmp.hitpos.y;
            hit->hitpos.z = tmp.hitpos.z;
            hit->normal.x = tmp.normal.x;
            hit->normal.y = tmp.normal.y;
            hit->normal.z = tmp.normal.z;
            hit->vtx0 = tmp.vtx0;
            hit->vtx1 = tmp.vtx1;
            hit->vtx2 = tmp.vtx2;
            hit->texturenum = tmp.texturenum;
            hit->tricmd = tmp.tricmd;
            hit->unk28 = tmp.unk28;
        }
    }

    if (found) 
    {
        point = (RoomVtxBatchBounds *)hit->tricmd;

        if ((((Gfx *)point)->words.w0 >> 24) != G_SETTILE)
        {
            while ((Gfx *)g_BgRoomInfo[roomnum].ptr_expanded_mapping_info < ((Gfx*)point)) 
            {
                point = (RoomVtxBatchBounds *)((Gfx *)point - 1);
                if ((((Gfx *)point)->words.w0 >> 24) == G_SETTILE)
                {
                    break;
                }
            }
        }

        if ((((Gfx *)point)->words.w0 >> 24) != G_SETTILE)
        {
            hit->tileformat = -1;
            hit->tilesize = -1;
        } 
        else 
        {
            hit->tileformat = (((Gfx *)point)->words.w0 >> 21) & 7;
            hit->tilesize = (((Gfx *)point)->words.w0 >> 19) & 3;
        }
    }

    return found;
}


/**
 * Address: 7F0B7D94
 */
void bgResetPortalQueue(void)
{
    g_BgPortalQueueWriteIndex = 0;
    g_BgPortalQueueReadIndex = 0;
}


/**
 * Address: 7F0B7DA8
 */
u8 bgIncrementRoomPortalVisitCount(s32 roomnum)
{
    s_room_info* room_info;
    u8 tmp;
    u8 count;
    u8 out;

    room_info = &g_BgRoomInfo[roomnum];
    count = room_info->portal_visit_count;
    out = count;

    if ((s32) count < 0xFF)
    {
        tmp = count + 1;
        room_info->portal_visit_count = tmp;
        out = tmp & 0xFF;
    }

    return out;
}


/**
 * Address: 7F0B7DE4
 */
#ifdef VERSION_EU
void bgQueuePortalTraversal(s32 arg0, s32 arg1, s32 portalnum, f32 *arg4)
{
    bg_queued_portal_entry *entry;
    entry = &g_BgPortalQueue[g_BgPortalQueueWriteIndex];
    if (portalnum >= 2)
    {
        if (bgIncrementRoomPortalVisitCount((g_BgPortals[arg1].connectedRoom2 ^ g_BgPortals[arg1].connectedRoom1) ^ arg0) >= 9)
        {
            return;
        }
    }
    entry->arg0 = arg0;
    entry->roomnum = arg1;
    entry->portalnum = portalnum;
    entry->sp4[0] = arg4[0];
    entry->sp4[1] = arg4[1];
    entry->sp4[2] = arg4[2];
    entry->sp4[3] = arg4[3];
    g_BgPortalQueueWriteIndex++;
    if (g_BgPortalQueueWriteIndex == BG_PORTAL_QUEUE_LEN)
    {
        g_BgPortalQueueWriteIndex = 0;
    }
    if (g_BgPortalQueueWriteIndex == g_BgPortalQueueReadIndex)
    {
        g_BgPortalQueueWriteIndex--;
    }
}
#else
void bgQueuePortalTraversal(s32 arg0, s32 arg1, s32 portalnum, s32 depth, f32 *arg4)
{
    bg_queued_portal_entry *entry;
    entry = &g_BgPortalQueue[g_BgPortalQueueWriteIndex];
    if (depth >= 2)
    {
        if (bgIncrementRoomPortalVisitCount((g_BgPortals[portalnum].connectedRoom2 ^ g_BgPortals[portalnum].connectedRoom1) ^ arg1) >= 9)
        {
            return;
        }
    }
    entry->arg0 = arg0;
    entry->roomnum = arg1;
    entry->portalnum = portalnum;
    entry->arg3 = depth;
    entry->sp10[0] = arg4[0];
    entry->sp10[1] = arg4[1];
    entry->sp10[2] = arg4[2];
    entry->sp10[3] = arg4[3];
    g_BgPortalQueueWriteIndex++;
    if (g_BgPortalQueueWriteIndex == BG_PORTAL_QUEUE_LEN)
    {
        g_BgPortalQueueWriteIndex = 0;
    }
    if (g_BgPortalQueueWriteIndex == g_BgPortalQueueReadIndex)
    {
            #ifdef DEBUG
            osSyncPrintf("bg: pstackat: Overflow ");
            #endif
        g_BgPortalQueueWriteIndex--;
    }
}
#endif


/**
 * Address: 7F0B7EE4
 */
#if defined(VERSION_EU)
bool bgProcessNextQueuedPortal(void)
{
    bg_queued_portal_entry *entry;

    if (g_BgPortalQueueReadIndex == g_BgPortalQueueWriteIndex) {
        return 0;
    }

    entry = &g_BgPortalQueue[g_BgPortalQueueReadIndex];

    sub_GAME_7F0B7F84(entry->arg0, entry->roomnum, entry->portalnum, entry->sp4);

    g_BgPortalQueueReadIndex++;

    if (g_BgPortalQueueReadIndex == BG_PORTAL_QUEUE_LEN) {
        g_BgPortalQueueReadIndex = 0;
    }

    return 1;
}
#else
bool bgProcessNextQueuedPortal(s32 *arg0)
{
    bg_queued_portal_entry *entry;
    s32 value;

    value = *arg0;

    if (g_BgPortalQueueReadIndex == g_BgPortalQueueWriteIndex) {
        return 0;
    }

    entry = &g_BgPortalQueue[g_BgPortalQueueReadIndex];

    value = sub_GAME_7F0B7F84(value, entry->roomnum, entry->portalnum, entry->arg3, entry->sp10);

    g_BgPortalQueueReadIndex++;

    if (g_BgPortalQueueReadIndex == BG_PORTAL_QUEUE_LEN) {
        g_BgPortalQueueReadIndex = 0;
    }

    *arg0 = value;

    return 1;
}
#endif


/**
 * EU version drops the value parameter and is type void unlike US/JP which returns s32.
 * 
 * bgPortalDescend
 */
#if defined(VERSION_EU)
void sub_GAME_7F0B7F84(s32 roomnum, s32 portalnum /*canonically p*/, s32 depth, bbox2d *parentbox)
{
    bbox2d screenbox;
    coord3d *playerpos;
    s32 otherroom;
    struct PortalMetric metric;
    f32 playermetric;
    f32 portalmetric;
    s32 i;
#ifndef TARGET_N64
    s32 s_PortalDbg;
#endif
 
    D_80044898++;
 
    if (depth >= 101)
    {
#ifdef DEBUG
        osSyncPrintf("bg: << Deep\n");
#endif
        return;
    }
 
    if (D_8004489C < depth)
    {
        return;
    }
 
    if (depth >= 16)
    {
        return;
    }
 
    if (depth);

#ifdef DEBUG
    assert(portalnum < PORTMAX);
    osSyncPrintf("bg: << bgPortalDescend: Inside out portal '%s' ", bgDebPrintPORTALID(portalnum));
#endif

#ifndef TARGET_N64
    /* GE_PORTALDBG=1 (diag): once per second, every portal decision
     * of the visibility walk - which test rejected it, or the recursed
     * room + surviving screenbox. Read with GE_ROOMLIST. */
    {
        extern char *getenv(const char *);
        static s32 s_PortalDbgInit = -1;
        if (s_PortalDbgInit < 0) { s_PortalDbgInit = getenv("GE_PORTALDBG") != NULL; }
        s_PortalDbg = s_PortalDbgInit && (g_GlobalTimer % 60) == 0;
    }
#define PORTALDBG_LOG(reason) \
    do { if (s_PortalDbg) printf("portaldbg: p=%d room=%d other=%d depth=%d %s\n", portalnum, roomnum, (s32) otherroom, depth, reason); } while (0)
#else
#define PORTALDBG_LOG(reason) do { } while (0)
#endif

    if (g_BgPortals[portalnum].controlbytes1 & PORTALFLAG_DISABLED)
    {
#ifndef TARGET_N64
        otherroom = -1;
        PORTALDBG_LOG("DISABLED");
#endif
        return;
    }

    i = (s32) &D_800442FC[portalnum];

    if (i);

    playerpos = bondviewGetCurrentPlayersPosition();
    sub_GAME_7F0B96CC(portalnum, &metric);
    playermetric = ((metric.normal.z * playerpos->z) + ((metric.normal.x * playerpos->x) + (metric.normal.y * playerpos->y))) * room_data_float1;
    portalmetric = sub_GAME_7F0B9990(portalnum);

    if (roomnum == g_BgPortals[portalnum].connectedRoom1)
    {
        otherroom = g_BgPortals[portalnum].connectedRoom2;

        if (metric.max <= (playermetric - portalmetric))
        {
            PORTALDBG_LOG("behind1");
            return;
        }
    }
    else
    {
        otherroom = g_BgPortals[portalnum].connectedRoom1;

        if ((playermetric + portalmetric) <= metric.min)
        {
            PORTALDBG_LOG("behind2");
            return;
        }
    }
 
    if (((metric.min - portalmetric) < playermetric) && (playermetric < (metric.max + portalmetric)))
    {
        screenbox.f[0][0] = g_CurrentPlayer->screensize.f[0][0];
        screenbox.f[0][1] = g_CurrentPlayer->screensize.f[0][1];
        screenbox.f[1][0] = g_CurrentPlayer->screensize.f[1][0];
        screenbox.f[1][1] = g_CurrentPlayer->screensize.f[1][1];
    }
    else
    {
        if (g_BgPortals[portalnum].controlbytes1 & PORTALFLAG_SPECIAL)
        {
            if (!sub_GAME_7F0B5864(portalnum, &screenbox))
            {
                PORTALDBG_LOG("project-fail(special)");
                return;
            }

            otherroom = (g_BgPortals[portalnum].connectedRoom1 ^ g_BgPortals[portalnum].connectedRoom2) ^ roomnum;

            if (!bgIsRoomOnScreen(otherroom, (struct rectbbox *) &screenbox))
            {
                PORTALDBG_LOG("roomoffscreen");
                return;
            }

            screenbox.f[0][0] = g_CurrentPlayer->screensize.f[0][0];
            screenbox.f[0][1] = g_CurrentPlayer->screensize.f[0][1];
            screenbox.f[1][0] = g_CurrentPlayer->screensize.f[1][0];
            screenbox.f[1][1] = g_CurrentPlayer->screensize.f[1][1];
        }
        else
        {
            if (!sub_GAME_7F0B5864(portalnum, &screenbox))
            {
                PORTALDBG_LOG("project-fail");
                return;
            }

#ifndef TARGET_N64
            if (s_PortalDbg)
            {
                printf("portaldbg: p=%d room=%d other=%d depth=%d projbox=%.0f,%.0f-%.0f,%.0f parent=%.0f,%.0f-%.0f,%.0f\n",
                       portalnum, roomnum, (s32) otherroom, depth,
                       screenbox.min.x, screenbox.min.y, screenbox.max.x, screenbox.max.y,
                       parentbox->min.x, parentbox->min.y, parentbox->max.x, parentbox->max.y);
            }
#endif
            bgRectIntersect(&screenbox, parentbox);
            bgRectIntersect(&screenbox, &g_CurrentPlayer->screensize);
        }

        if ((screenbox.max.x <= screenbox.min.x) || (screenbox.max.y <= screenbox.min.y))
        {
            PORTALDBG_LOG("empty-intersect");
            return;
        }
    }

    *((u8 *) i) = depth;

    if ((screenbox.min.x < screenbox.max.x) && (screenbox.min.y < screenbox.max.y))
    {
        PORTALDBG_LOG("RECURSE");
        if (sub_GAME_7F0B39BC(otherroom, depth, &screenbox, g_BgPortals[portalnum].controlbytes1 & PORTALFLAG_SPECIAL))
        {
            return;
        }
    }
    else
    {
        return;
    }
 
    for (i = 0; g_BgPortals[i].offset_portal != NULL; i++)
    {
        if (i != portalnum)
        {
            if ((otherroom == g_BgPortals[i].connectedRoom1) || (otherroom == g_BgPortals[i].connectedRoom2))
            {
                bgQueuePortalTraversal(otherroom, i, depth + 1, &screenbox);
            }
        }
    }
}
#else
s32 sub_GAME_7F0B7F84(s32 value, s32 roomnum, s32 portalnum /*canonically p*/, s32 depth, bbox2d *parentbox)
{
    bbox2d screenbox;
    coord3d *playerpos;
    s32 otherroom;
    struct PortalMetric metric;
    f32 playermetric;
    f32 portalmetric;
    intptr_t i;
#ifndef TARGET_N64
    s32 s_PortalDbg;
#endif
 
    D_80044898++;
 
    if (depth >= 101)
    {
#ifdef DEBUG
        osSyncPrintf("bg: << Deep\n");
#endif
        return value;
    }
 
    if (D_8004489C < depth)
    {
        return value;
    }
 
    if (depth >= 16)
    {
        return value;
    }
 
    if (depth);
 
#ifdef DEBUG
    assert(portalnum < PORTMAX);
    osSyncPrintf("bg: << bgPortalDescend: Inside out portal '%s' ", bgDebPrintPORTALID(portalnum));
#endif
 
#ifndef TARGET_N64
    /* GE_PORTALDBG=1 (diag): once per second, every portal decision
     * of the visibility walk - the rejecting test, or the recursed room
     * and surviving screenbox. Pair with GE_ROOMLIST. */
    {
        extern char *getenv(const char *);
        static s32 s_PortalDbgInit = -1;
        if (s_PortalDbgInit < 0) { s_PortalDbgInit = getenv("GE_PORTALDBG") != NULL; }
        s_PortalDbg = s_PortalDbgInit && (g_GlobalTimer % 60) == 0;
    }
#undef PORTALDBG_LOG
#define PORTALDBG_LOG(reason) \
    do { if (s_PortalDbg) printf("portaldbg: p=%d room=%d other=%d depth=%d %s\n", portalnum, roomnum, (s32) otherroom, depth, reason); } while (0)
#endif

    if (g_BgPortals[portalnum].controlbytes1 & PORTALFLAG_DISABLED)
    {
#ifndef TARGET_N64
        otherroom = -1;
        PORTALDBG_LOG("DISABLED");
#endif
        return value;
    }

    i = (intptr_t) &D_800442FC[portalnum];

    if (i);

    playerpos = bondviewGetCurrentPlayersPosition();
    sub_GAME_7F0B96CC(portalnum, &metric);
    playermetric = ((metric.normal.z * playerpos->z) + ((metric.normal.x * playerpos->x) + (metric.normal.y * playerpos->y))) * room_data_float1;
    portalmetric = sub_GAME_7F0B9990(portalnum);

    if (roomnum == g_BgPortals[portalnum].connectedRoom1)
    {
        otherroom = g_BgPortals[portalnum].connectedRoom2;

        if (metric.max <= (playermetric - portalmetric))
        {
            PORTALDBG_LOG("behind1");
#ifndef TARGET_N64
            { /* GE_ROOM_AUDIT: the walk collapses (n=1) from rooms
               * whose seed portal dies HERE, so log the actual inequality. */
                extern s32 portRoomAuditEnabled(void);
                extern u32 portRoomAuditGetFrame(void);
                static u32 lastlog;
                u32 fr = portRoomAuditGetFrame();
                if (portRoomAuditEnabled() && depth == 1 && fr - lastlog >= 30u) {
                    lastlog = fr;
                    fprintf(stderr,
                            "roomaudit: side-skip A portal=%d room=%d->%d pm=%.2f slack=%.2f min=%.2f max=%.2f n=(%.2f,%.2f,%.2f) ppos=(%.1f,%.1f,%.1f) f=%u\n",
                            portalnum, roomnum, otherroom, playermetric, portalmetric,
                            metric.min, metric.max,
                            metric.normal.x, metric.normal.y, metric.normal.z,
                            playerpos->x, playerpos->y, playerpos->z, fr);
                    fflush(stderr);
                }
            }
#endif
            return value;
        }
    }
    else
    {
        otherroom = g_BgPortals[portalnum].connectedRoom1;

        if ((playermetric + portalmetric) <= metric.min)
        {
            PORTALDBG_LOG("behind2");
#ifndef TARGET_N64
            { /* GE_ROOM_AUDIT: same as side-skip A above */
                extern s32 portRoomAuditEnabled(void);
                extern u32 portRoomAuditGetFrame(void);
                static u32 lastlog;
                u32 fr = portRoomAuditGetFrame();
                if (portRoomAuditEnabled() && depth == 1 && fr - lastlog >= 30u) {
                    lastlog = fr;
                    fprintf(stderr,
                            "roomaudit: side-skip B portal=%d room=%d->%d pm=%.2f slack=%.2f min=%.2f max=%.2f n=(%.2f,%.2f,%.2f) ppos=(%.1f,%.1f,%.1f) f=%u\n",
                            portalnum, roomnum, otherroom, playermetric, portalmetric,
                            metric.min, metric.max,
                            metric.normal.x, metric.normal.y, metric.normal.z,
                            playerpos->x, playerpos->y, playerpos->z, fr);
                    fflush(stderr);
                }
            }
#endif
            return value;
        }
    }
 
    if (((metric.min - portalmetric) < playermetric) && (playermetric < (metric.max + portalmetric)))
    {
        screenbox.f[0][0] = g_CurrentPlayer->screensize.f[0][0];
        screenbox.f[0][1] = g_CurrentPlayer->screensize.f[0][1];
        screenbox.f[1][0] = g_CurrentPlayer->screensize.f[1][0];
        screenbox.f[1][1] = g_CurrentPlayer->screensize.f[1][1];
    }
    else
    {
        if (g_BgPortals[portalnum].controlbytes1 & PORTALFLAG_SPECIAL)
        {
            if (!sub_GAME_7F0B5864(portalnum, &screenbox))
            {
                PORTALDBG_LOG("project-fail(special)");
                return value;
            }

            otherroom = (g_BgPortals[portalnum].connectedRoom1 ^ g_BgPortals[portalnum].connectedRoom2) ^ roomnum;

            if (!bgIsRoomOnScreen(otherroom, (struct rectbbox *) &screenbox))
            {
                PORTALDBG_LOG("roomoffscreen");
                return value;
            }

            screenbox.f[0][0] = g_CurrentPlayer->screensize.f[0][0];
            screenbox.f[0][1] = g_CurrentPlayer->screensize.f[0][1];
            screenbox.f[1][0] = g_CurrentPlayer->screensize.f[1][0];
            screenbox.f[1][1] = g_CurrentPlayer->screensize.f[1][1];
        }
        else
        {
            if (!sub_GAME_7F0B5864(portalnum, &screenbox))
            {
                PORTALDBG_LOG("project-fail");
                return value;
            }

#ifndef TARGET_N64
            if (s_PortalDbg)
            {
                printf("portaldbg: p=%d room=%d other=%d depth=%d projbox=%.0f,%.0f-%.0f,%.0f parent=%.0f,%.0f-%.0f,%.0f\n",
                       portalnum, roomnum, (s32) otherroom, depth,
                       screenbox.min.x, screenbox.min.y, screenbox.max.x, screenbox.max.y,
                       parentbox->min.x, parentbox->min.y, parentbox->max.x, parentbox->max.y);
            }
#endif
            bgRectIntersect(&screenbox, parentbox);
            bgRectIntersect(&screenbox, &g_CurrentPlayer->screensize);
        }

        if ((screenbox.max.x <= screenbox.min.x) || (screenbox.max.y <= screenbox.min.y))
        {
            PORTALDBG_LOG("empty-intersect");
            return value;
        }
    }

    *((u8 *) i) = depth;

    if ((screenbox.min.x < screenbox.max.x) && (screenbox.min.y < screenbox.max.y))
    {
        PORTALDBG_LOG("RECURSE");
        if (sub_GAME_7F0B39BC(otherroom, depth, &screenbox, g_BgPortals[portalnum].controlbytes1 & PORTALFLAG_SPECIAL))
        {
            return value;
        }
    }
    else
    {
        return value;
    }
 
    for (i = 0; g_BgPortals[i].offset_portal != NULL; i++)
    {
        if (i != portalnum)
        {
            if ((otherroom == g_BgPortals[i].connectedRoom1) || (otherroom == g_BgPortals[i].connectedRoom2))
            {
                bgQueuePortalTraversal(value, otherroom, i, depth + 1, &screenbox);
            }
        }
    }
 
    return value;
}
#endif


/**
 * Add to stack. Push and then increment position. Will wrap on overflow.
 *
 * Address 0x7F0B8374.
 */
s32 bgStackPush(s32 arg0)
{
    g_BgStack[g_BgStackCount] = arg0;
    g_BgStackCount = (s32) (g_BgStackCount + 1) % BG_STACK_SIZE;
    return arg0;
}


/**
 * Pop from stack. Decrement position and retrieve from there. Wraps on underflow.
 *
 * Address 0x7F0B83B0.
 */
s32 bgStackPop(void)
{
    s32 val;

    // ok, who thought this was a good idea
    val = g_BgStack[g_BgStackCount = (g_BgStackCount + (BG_STACK_SIZE-1)) % BG_STACK_SIZE];
    return val;
}


s32 bgStackGetNthValueFromEnd(s32 n) 
{
    return g_BgStack[((g_BgStackCount - n) + (BG_STACK_SIZE - 1)) % BG_STACK_SIZE];
}

GlobalVisCommand *parse_global_vis_command_list(GlobalVisCommand *cmd, s32 execute)
{
    static Unk80081600 dword_CODE_bss_80081600;

    GlobalVisCommand  *ret;
    s32                value;
    bbox2d             sp68;
    bbox2d             sp58;
    u8                 preload_ok = TRUE;

    dword_CODE_bss_80081600.unk10 = FALSE;

    if (cmd == NULL)
    {
        return cmd;
    }

    while (TRUE)
    {
#ifndef TARGET_N64
        { /* GE_VISDBG=1 (diag): trace every executed global-vis script op once
           * per second - current room, opcode, args, visibility flag and the
           * script's portal box (Dam sky-wedge hunt: MATCH_PORTAL_VIS 34) */
            extern char *getenv(const char *);
            static s32 vd = -1;
            if (vd < 0) vd = getenv("GE_VISDBG") != NULL;
            if (vd && (g_GlobalTimer % 60) == 0 && execute && cmd->type != VISOP_END)
            {
                printf("visdbg: cur=%d op=%d a1=%d a2=%d vis=%d box=%.0f,%.0f-%.0f,%.0f\n", g_BgCurrentRoom,
                       cmd->type, cmd[1].arg, cmd[2].arg, current_visibility,
                       dword_CODE_bss_80081600.unk0.min.x, dword_CODE_bss_80081600.unk0.min.y,
                       dword_CODE_bss_80081600.unk0.max.x, dword_CODE_bss_80081600.unk0.max.y);
            }
        }
#endif
        switch (cmd->type)
        {
            case VISOP_END:
                return cmd;

            case VISOP_PUSH:
                if (execute)
                {
                    bgStackPush(cmd->arg);
                }

                cmd += cmd->length;
                break;

            case VISOP_POP:
                if (execute)
                {
                    bgStackPop();
                }

                cmd += cmd->length;
                break;

            case VISOP_AND:
                if (execute)
                {
                    value = bgStackPop();
                    bgStackPush(bgStackPop() & value);
                }

                cmd += cmd->length;
                break;

            case VISOP_OR:
                if (execute)
                {
                    value = bgStackPop();
                    bgStackPush(bgStackPop() | value);
                }

                cmd += cmd->length;
                break;

            case VISOP_NOT:
                if (execute)
                {
                    bgStackPush(bgStackPop() == 0);
                }

                cmd += cmd->length;
                break;

            case VISOP_XOR:
                if (execute)
                {
                    value = bgStackPop();
                    bgStackPush(bgStackPop() ^ value);
                }

                cmd += cmd->length;
                break;

            case VISOP_PUSH_IF_ROOM_IN_RANGE:
                if (execute)
                {
                    s32 curroom;

                    curroom = g_BgCurrentRoom;

                    bgStackPush(cmd[1].arg <= curroom && curroom <= cmd[2].arg);
                }

                cmd += cmd->length;
                break;

            case VISOP_FORCE_VISIBLE:
                if (execute)
                {
                    dword_CODE_bss_80081600.unk0.f[0][0] = g_CurrentPlayer->screensize.f[0][0];
                    dword_CODE_bss_80081600.unk0.f[0][1] = g_CurrentPlayer->screensize.f[0][1];
                    dword_CODE_bss_80081600.unk0.f[1][0] = g_CurrentPlayer->screensize.f[1][0];
                    dword_CODE_bss_80081600.unk0.f[1][1] = g_CurrentPlayer->screensize.f[1][1];

                    current_visibility = FALSE;
                }

                cmd += cmd->length;
                break;

            case VISOP_MATCH_PORTAL_VIS:
                if (execute)
                {
                    if (sub_GAME_7F0B5864(cmd[1].arg, &dword_CODE_bss_80081600.unk0) == 0)
                    {
                        current_visibility = TRUE;
                    }
                    else if (bgRectIntersect(&dword_CODE_bss_80081600.unk0, &g_CurrentPlayer->screensize) == 0)
                    {
                        current_visibility = TRUE;
                    }
                    else
                    {
                        current_visibility = FALSE;
                    }
                }

                cmd += cmd->length;
                break;

            case VISOP_VISIBLE_IF_SEEN_THROUGH_PORTAL:
                if (execute)
                {
                    if (sub_GAME_7F0B5864(cmd[1].arg, &sp68) && bgRectIntersect(&sp68, &g_CurrentPlayer->screensize))
                    {
                        if (current_visibility)
                        {
                            bbox2dCopy(&dword_CODE_bss_80081600.unk0, &sp68);
                            current_visibility = FALSE;
                        }
                        else
                        {
                            bgRectOutersect(&dword_CODE_bss_80081600.unk0, &sp68);
                        }
                    }
                }

                cmd += cmd->length;
                break;

            case VISOP_NOT_VISIBLE_IF_SEEN_THROUGH_PORTAL:
                if (execute && !current_visibility)
                {
                    if (!sub_GAME_7F0B5864(cmd[1].arg, &sp58))
                    {
                        current_visibility = TRUE;
                    }
                    else if (!bgRectIntersect(&sp58, &g_CurrentPlayer->screensize))
                    {
                        current_visibility = TRUE;
                    }
                    else if (!bgRectIntersect(&dword_CODE_bss_80081600.unk0, &sp58))
                    {
                        current_visibility = TRUE;
                    }
                }

                cmd += cmd->length;
                break;

            case VISOP_ADD_VISIBLE_ROOM:
                if (execute && !current_visibility)
                {
                    if (bgIsRoomOnScreen(cmd[1].arg, (struct rectbbox *)&dword_CODE_bss_80081600.unk0))
                    {
                        sub_GAME_7F0B39BC(cmd[1].arg, 0, &dword_CODE_bss_80081600.unk0, 0);

                        list_visible_rooms_in_cur_global_vis_packet[num_visible_rooms_in_cur_global_vis_packet] = cmd[1].arg;

                        num_visible_rooms_in_cur_global_vis_packet++;
                    }
                }

                cmd += cmd->length;
                break;

            case VISOP_DISABLE_ROOM:
                if (execute)
                {
                    g_BgRoomInfo[cmd[1].arg].room_loaded_mask = TRUE;
                }

                cmd += cmd->length;
                break;

            case VISOP_DISABLE_ROOM_RANGE:
                if (execute)
                {
                    s32 room;

                    room = cmd[1].arg;

                    while (room <= cmd[2].arg)
                    {
                        g_BgRoomInfo[room].room_loaded_mask = TRUE;
                        room++;
                    }
                }

                cmd += cmd->length;
                break;

            case VISOP_PRELOAD_ROOM:
                if (execute && preload_ok)
                {
                    preload_ok = !bgCheckIfRoomModelNeedsLoad(cmd[1].arg);
                }

                cmd += cmd->length;
                break;

            case VISOP_PRELOAD_ROOM_RANGE:
                if (execute)
                {
                    s32 room;

                    room = cmd[1].arg;

                    while (room <= cmd[2].arg)
                    {
                        if (preload_ok)
                        {
                            preload_ok = !bgCheckIfRoomModelNeedsLoad(room);
                        }

                        room++;
                    }
                }

                cmd += cmd->length;
                break;

            case VISOP_REMOVE_VIS:
                if (execute)
                {
                    current_visibility = TRUE;
                }

                cmd += cmd->length;
                break;

            case VISOP_IF_STATEMENT:
                ret = parse_global_vis_command_list(cmd + cmd->length, execute);
                ret += ret->length;
                cmd = ret;
                break;

            case VISOP_ENDIF_CONTINUE_EXEC:
                cmd += cmd->length;
                dword_CODE_bss_80081600.unk10 = FALSE;
                return cmd;

            case VISOP_DONT_EXEC_COMMANDS_EVEN_ON_RETURN:
                cmd += cmd->length;

                if (execute)
                {
                    execute                       = FALSE;
                    dword_CODE_bss_80081600.unk10 = TRUE;
                }
                else
                {
                    execute = FALSE;
                }

                break;

            case VISOP_IF_STATEMENT_PULL_FROM_STACK:
                value = bgStackPop();

                ret = parse_global_vis_command_list(cmd + cmd->length, value & execute);
                cmd = ret;

                if (!dword_CODE_bss_80081600.unk10)
                {
                    continue;
                }

                execute = FALSE;
                break;

            case VISOP_TOGGLE_EXEC_VS_READONLY:
                execute ^= TRUE;
                cmd += cmd->length;
                break;

            case VISOP_ENDIF:
                cmd += cmd->length;
                return cmd;

            default:
                return cmd;
        }
    }
}

#ifndef VERSION_EU
struct unk_portalstruct table_for_portals[PORTMAX];
#endif

#define RS_STOP 0


// Something about portals. Void* are structs.
void *sub_GAME_7F0B8A24(s32 *pc) 
{

    current_visibility = 0;
    if (!pc)
    {
        return pc;
    }

    bgStackGetNthValueFromEnd(0);
    #ifdef DEBUG
    assert( pc->type==RS_STOP)
    #endif

    return parse_global_vis_command_list(pc, 1);
}


/**
 * Address: 7F0B8A6C
 */
void bgDetermineVisibleRooms(void) 
{
    f32 screenbounds[4];
    s32 var_s0;
    s32 temp_a0;
#if defined(LEFTOVERDEBUG)
    s32 sp44;
#endif
    s32 temp_v1;
    s32 i;

    bgUpdateCurrentPlayerScreenMinMax();

    screenbounds[0] = g_CurrentPlayer->screensize.min.x;
    screenbounds[1] = g_CurrentPlayer->screensize.min.y;
    screenbounds[2] = g_CurrentPlayer->screensize.max.x;
    screenbounds[3] = g_CurrentPlayer->screensize.max.y;

    bgResetPortalVisitCounts();

    for (i = 0; i < MAXROOMCOUNT; i++) 
    {
        g_BgRoomInfo[i].room_rendered = 0;
        g_BgRoomInfo[i].room_neighbor_to_rendered = 0;
        g_BgRoomInfo[i].room_loaded_mask = 0;
    }

    for (i = 0; i < PORTMAX; i++) 
    {
        D_800442FC[i] = 0;
    }
    
    D_80044858 = (s32) (D_80044858 + 1) % 10;

#if defined(LEFTOVERDEBUG)
    dword_CODE_bss_8007FF98 = 0;
#else
    *(s32 *) &dword_CODE_bss_8007FFA0[120] = 0;
#endif
    D_80044898 = 0;

    bgResetPortalQueue();
    sub_GAME_7F0B5168();

#ifndef TARGET_N64
    { /* diagnostic (GE_NOVISCRIPT=1): drop the global-visibility script -
       * A/B knob for isolating script-driven room visibility side effects
       * (Frigate hostage early-release hunt). */
        extern char *getenv(const char *);
        static int noviscript = -1;
        if (noviscript < 0) noviscript = getenv("GE_NOVISCRIPT") != NULL;
        if (!noviscript)
        {
            sub_GAME_7F0B8A24(dword_CODE_bss_8007FF90);
        }
    }
#else
    sub_GAME_7F0B8A24(dword_CODE_bss_8007FF90);
#endif

    if (1);

    /**
     * If the level is Cradle, or has no portals, skip the portal occlusion culling algorithm. Just add every room in the player's
     * screen bounds to the list of rooms to draw.
     */
    if ((levelentry_index == LEVEL_INDEX_CRAD) || (g_BgPortals->offset_portal == NULL)) 
    {
        if (levelentry_index == LEVEL_INDEX_CRAD) 
        {
            sub_GAME_7F0B39BC(9, 0, &g_CurrentPlayer->screensize, 1);
        }

        for (var_s0 = 1; var_s0 < g_MaxNumRooms; var_s0++) 
        {
            if (bgIsRoomOnScreen(var_s0, &g_CurrentPlayer->screensize) != 0) 
            {
                sub_GAME_7F0B39BC(var_s0, 0, &g_CurrentPlayer->screensize, 1);
            }
        }
    } 
    else 
    {
        // This can never execute since the above branch is taken if the level is Cradle.
        if (levelentry_index == LEVEL_INDEX_CRAD) 
        {
            sub_GAME_7F0B39BC(9, 0, &g_CurrentPlayer->screensize, 1);
        }

        sub_GAME_7F0B39BC(g_BgCurrentRoom, 0, &g_CurrentPlayer->screensize, 1);

        for (var_s0 = 0; g_BgPortals[var_s0].offset_portal != NULL; var_s0++) 
        {
            if ((g_BgCurrentRoom == g_BgPortals[var_s0].connectedRoom1) || (g_BgCurrentRoom == g_BgPortals[var_s0].connectedRoom2)) 
            {
#if defined(LEFTOVERDEBUG)
                bgQueuePortalTraversal(0, g_BgCurrentRoom, var_s0, 1, screenbounds);
#else
                bgQueuePortalTraversal(g_BgCurrentRoom, var_s0, 1, screenbounds);
#endif
            }
        }

#if defined(LEFTOVERDEBUG)
        sp44 = 0;

        while (bgProcessNextQueuedPortal(&sp44) != 0) 
        {
            // empty
        }

        if (1);
#else
        while (bgProcessNextQueuedPortal() != 0) 
        {
            // empty
        }
#endif
    }

#ifndef TARGET_N64
    { /* Pad the final drawn-room scissor boxes, because the authored portal
       * quads are coarser than the real openings and would clip visible
       * geometry. Portal recursion and the room set stay vanilla; keep
       * GE_PORTALPAD small (0 = off) or far rooms draw outside their opening. */
        extern char *getenv(const char *);
        static f32 scpad = -1.0f;
        if (scpad < 0.0f)
        {
            const char *e = getenv("GE_PORTALPAD");
            scpad = (e != NULL) ? (f32) atof(e) : 12.0f;
            if (scpad < 0.0f) scpad = 0.0f;
        }
        if (scpad > 0.0f)
        {
            for (i = 0; i < g_BgNumberOfRoomsDrawn; i++)
            {
                bbox2d *bb = &dword_CODE_bss_8007FFA0[i].bbox;
                bb->min.x -= scpad;
                bb->min.y -= scpad;
                bb->max.x += scpad;
                bb->max.y += scpad;
                bgRectIntersect(bb, &g_CurrentPlayer->screensize);
            }
        }
    }

    { /* GE_FORCEROOM=a,b,... (diag): append specific rooms to the
       * drawn set after the walk - bisect which room's geometry covers a
       * see-through artifact without forcing everything (GE_ALLROOMS). */
        extern char *getenv(const char *);
        static s32 nforce = -1;
        static s32 forced[16];
        if (nforce < 0)
        {
            const char *e = getenv("GE_FORCEROOM");
            nforce = 0;
            while (e != NULL && *e != '\0' && nforce < 16)
            {
                s32 v = 0;
                while (*e >= '0' && *e <= '9') { v = v * 10 + (*e - '0'); e++; }
                forced[nforce++] = v;
                if (*e == ',') { e++; } else { break; }
            }
        }
        for (i = 0; i < nforce; i++)
        {
            if (forced[i] > 0 && forced[i] < g_MaxNumRooms)
            {
                sub_GAME_7F0B39BC(forced[i], 0, &g_CurrentPlayer->screensize, 1);
            }
        }
    }
#endif

    for (var_s0 = 0; g_BgPortals[var_s0].offset_portal != NULL; var_s0++)
    {
        temp_v1 = g_BgPortals[var_s0].connectedRoom1;
        temp_a0 = g_BgPortals[var_s0].connectedRoom2;

        if ((g_BgRoomInfo[temp_v1].room_rendered != 0) && (g_BgRoomInfo[temp_a0].room_rendered == 0))
        {
            g_BgRoomInfo[temp_a0].room_neighbor_to_rendered = 1;
        }
        else if ((g_BgRoomInfo[temp_a0].room_rendered != 0) && (g_BgRoomInfo[temp_v1].room_rendered == 0)) 
        {
            g_BgRoomInfo[temp_v1].room_neighbor_to_rendered = 1;
        }
    }
}


/**
 * Address 0x7F0B8D78.
*/
Gfx *sub_GAME_7F0B8D78(Gfx *arg0)
{
    s32 i;
    if (levelentry_index == LEVEL_INDEX_DAM)
    {
        for (i=0; i<g_BgNumberOfRoomsDrawn; i++)
        {
            // The lake in dam is a single giant room, id 0x23
            if (dword_CODE_bss_8007FFA0[i].roomid == 0x23)
            {
                // speculation in discord: unk1 is probably draw order or similar,
                // this is a hack to draw the lake first.
                dword_CODE_bss_8007FFA0[i].unk1 = 0;
                break;
            }
        }
    }

    return bgScissorCurrentPlayerViewDefault(sub_GAME_7F0B3C8C(arg0));
}


/**
 * Unreferenced.
 */
s32 sub_GAME_7F0B8DF4(s32 room, s32 *portalnums, s32 max)
{
    bg_portal_data_entry *base;
    bg_portal_data_entry *portal;
    s32 count;
    s32 i;
    s32 offset;

    count = 0;
    i = 0;
    base = g_BgPortals;

    if (room);

    if (base->offset_portal != NULL)
    {
        offset = 0;
        portal = base;

        do
        {
            if ((room == portal->connectedRoom1) || (room == portal->connectedRoom2))
            {
                portalnums[count] = i;
                count++;
            }

            if (count >= max)
            {
                return count;
            }

            offset += 8;
            i++;
            portal = (bg_portal_data_entry *) (((u8 *) g_BgPortals) + offset);
        }
        while (portal->offset_portal != NULL);
    }

    return count;
}


// Copies visible rooms to a list
// Address: 0x7F0B8E98
s32 bgCopyVisibleRoomsToList(s32 *rooms, s32 max)
{
    s32 i;

    for (i = 0; (i < num_visible_rooms_in_cur_global_vis_packet) && (i < max); i++) {
        rooms[i] = list_visible_rooms_in_cur_global_vis_packet[i];
    }

    return i;
}


/**
 * PORT addition: copy the room IDs that were actually RENDERED this frame
 * (the portal/bbox walk's own output, dword_CODE_bss_8007FFA0[].roomid,
 * bounded by g_BgNumberOfRoomsDrawn - the same list bgLevelRender draws
 * from). NOT the same set as bgCopyVisibleRoomsToList, which only holds
 * rooms an authored VISOP_ADD_VISIBLE_ROOM global-vis script command
 * added (most levels never populate it). Used by chrprop.c's shoot-
 * through-portal fix so the hitscan can test every room on screen, not
 * just the stan-tile-walk room and the shooter's own room.
 */
s32 bgCopyDrawnRoomsToList(s32 *rooms, s32 max)
{
    s32 i;
    s32 n = g_BgNumberOfRoomsDrawn;

    if (n > max) {
        n = max;
    }

    for (i = 0; i < n; i++) {
        rooms[i] = dword_CODE_bss_8007FFA0[i].roomid;
    }

    return n;
}


/**
 * Create a list of rooms connected to roomIndex.
 * @param roomIndex    Room to query.
 * @param list         Output buffer for connected room indices.
 * @param max          Max number of entries to write.
 * @return             Number of rooms written to the list.
 */
s32 bgGetConnectedRooms(s32 roomIndex, s32* list, s32 max)
{
    s32 len = 0;
    s32 i;
    s32 p;
    s32 connectedRoom1;
    s32 connectedRoom2;

    for (p = 0; g_BgPortals[p].offset_portal != NULL; p++) {
        connectedRoom1 = g_BgPortals[p].connectedRoom1;
        connectedRoom2 = g_BgPortals[p].connectedRoom2;

        if (connectedRoom1 == roomIndex) {
            connectedRoom1 = connectedRoom2;
            connectedRoom2 = roomIndex;
        }

        if (connectedRoom2 == roomIndex) {
            for (i = 0; i < len; i++) {
                if (list[i] == connectedRoom1) {
                    goto end;
                }
            }

            list[len] = connectedRoom1;
            len++;

            if (len >= max) {
                return len;
            }
end:
            if (1);
        }
    }

    return len;
}


// Scan all portals to see if these rooms are connected
//
// Room data doesn't contain a list of its portals, so it goes through
// the whole list of portals which seems naive and inefficient.
//
// Address: 0x7F0B8FD0
bool bgRoomsSharePortal(s32 room1, s32 room2) 
{
    s32 i;

    for (i = 0; g_BgPortals[i].offset_portal != NULL; i++)
    {
        s32 v0 = g_BgPortals[i].connectedRoom1;
        s32 v1 = g_BgPortals[i].connectedRoom2;

        if (v0 == room1 && v1 == room2)
        {
            return TRUE;
        }

        if (v1 == room1 && v0 == room2)
        {
            return TRUE;
        }
    }
    return FALSE;
}


/**
 * Unreferenced.
 *
 * Adjusts value in bgViewRelated and returns the new value.
 *
 * @param index: index into bgViewRelated.
 * @param times: multiples value by this amount first.
 * @param add: then adds this.
 *
 * Address 0x7F0B9040.
 */
f32 bgTimesAddViewRelatedMaybe(s32 index, f32 times, f32 add)
{
    bgViewRelated[index] = (s32) (((f32) bgViewRelated[index] * times) + add);
    return (f32) bgViewRelated[index];
}





/**
 * Address 0x7F0B908C.
 */
void bgUpdateCurrentPlayerScreenMinMax(void)
{
    f32 fx;
    f32 fwidth;
    f32 fy;
    f32 fheight;

    fx = (f32) bgViewRelated[0];
    fy = (f32) bgViewRelated[1];
    fwidth = (f32) viGetX() + (f32) bgViewRelated[2];
    fheight = (f32) viGetY() + (f32) bgViewRelated[3];

#ifndef TARGET_N64
    { /* Use the full view: the vanilla one-pixel inset (a CRT overscan margin)
       * shows unrendered sky at the screen edges on PC. Demos keep the inset,
       * since this rect gates portal descent and so the recorded RNG stream, and
       * GE_EDGEINSET=1 keeps it always. */
        extern char *getenv(const char *);
        extern s32 get_is_ramrom_flag(void);
        static s32 keep = -1;

        if (keep < 0)
        {
            keep = getenv("GE_EDGEINSET") != NULL;
        }
        if (!keep && get_is_ramrom_flag() == 0)
        {
            fx = 0.0f;
            fy = 0.0f;
            fwidth = (f32) viGetX();
            fheight = (f32) viGetY();
        }
    }
#endif

    g_CurrentPlayer->screensize.min.x = (f32) viGetViewLeft();

    if (g_CurrentPlayer->screensize.min.x < fx)
    {
        g_CurrentPlayer->screensize.min.x = fx;
    }

    if (fwidth < g_CurrentPlayer->screensize.min.x)
    {
        g_CurrentPlayer->screensize.min.x = fwidth;
    }

    g_CurrentPlayer->screensize.min.y = (f32) viGetViewTop();

    if (g_CurrentPlayer->screensize.min.y < fy)
    {
        g_CurrentPlayer->screensize.min.y = fy;
    }

    if (fheight < g_CurrentPlayer->screensize.min.y)
    {
        g_CurrentPlayer->screensize.min.y = fheight;
    }

    g_CurrentPlayer->screensize.max.x = (f32) (viGetViewLeft() + viGetViewWidth());

    if (g_CurrentPlayer->screensize.max.x < fx)
    {
        g_CurrentPlayer->screensize.max.x = fx;
    }

    if (fwidth < g_CurrentPlayer->screensize.max.x)
    {
        g_CurrentPlayer->screensize.max.x = fwidth;
    }

    g_CurrentPlayer->screensize.max.y = (f32) (viGetViewTop() + viGetViewHeight());

    if (g_CurrentPlayer->screensize.max.y < fy)
    {
        g_CurrentPlayer->screensize.max.y = fy;
    }

    if (fheight < g_CurrentPlayer->screensize.max.y)
    {
        g_CurrentPlayer->screensize.max.y = fheight;
    }
}


/**
 * Address: 7F0B92B4
 */
void bgGetRoomCenter(s32 roomnum, coord3d *dst)
{
    s32 i;
    s_room_info *room = &g_BgRoomInfo[roomnum];

    for (i = 0; i < 3; i++) {
        dst->f[i] = (room->minbounds.f[i] + room->maxbounds.f[i]) * 0.5f;
    }
}


/**
 * Address: 7F0B9338
 */
void bgRoomCalcBB(s32 room) // canonical name
{
    bg_room_data *roomdata;
    Vtx *vertices = (Vtx *) &g_StanRoomBounds[0];
    s32 j = 0;
    StanRoomBounds limits;
    u8 wasloaded;

    roomdata = &ptr_bgdata_room_fileposition_list[room];

    if (roomdata->pPointTableBin == NULL)
    {
        if ((room < dword_CODE_bss_8007B9DC) && ((intptr_t) firststaninroom[room] != ((StanRoomBounds *) vertices)->min[j] % 1))
        {
            for (j = 0; j < 3; j++)
            {
                g_BgRoomInfo[room].minbounds.f[j] = g_StanRoomBounds[room].min[j];

                g_BgRoomInfo[room].maxbounds.f[j] = g_StanRoomBounds[room].max[j];

                ptr_bgdata_room_fileposition_list[room].pos.f[j] = (g_StanRoomBounds[room].min[j] + g_StanRoomBounds[room].max[j]) / 2;
            }
        }
#ifdef DEBUG
        else
        {
            osSyncPrintf("bg: bgRoomCalcBB: ROOM%d has no gfx, and no stans! Can\'t make bb & roomoffset ", room);
        }
#endif

        return;
    }

    wasloaded = g_BgRoomInfo[room].model_bin_loaded;

    if (!wasloaded)
    {
        bgLoadRoomModelData(room);
    }

    vertices = g_BgRoomInfo[room].vertices;
    roomdata = &ptr_bgdata_room_fileposition_list[room];

    limits.minX = 0x7fff;
    limits.minY = 0x7fff;
    limits.minZ = 0x7fff;
    limits.maxX = -0x7fff;
    limits.maxY = -0x7fff;
    limits.maxZ = -0x7fff;

    for (; vertices < (Vtx *) ((intptr_t) g_BgRoomInfo[room].vertices + g_BgRoomInfo[room].usize_point_index_binary); vertices++)
    {
        for (j = 0; j < 3; j++)
        {
            if (((s16 *) vertices)[j] < limits.min[j])
            {
                limits.min[j] = ((s16 *) vertices)[j];
            }

            if (limits.max[j] < ((s16 *) vertices)[j])
            {
                limits.max[j] = ((s16 *) vertices)[j];
            }
        }
    }

    g_BgRoomInfo[room].minbounds.x = roomdata->pos.x + limits.minX;
    g_BgRoomInfo[room].minbounds.y = roomdata->pos.y + limits.minY;
    g_BgRoomInfo[room].minbounds.z = roomdata->pos.z + limits.minZ;
    g_BgRoomInfo[room].maxbounds.x = roomdata->pos.x + limits.maxX;
    g_BgRoomInfo[room].maxbounds.y = roomdata->pos.y + limits.maxY;
    g_BgRoomInfo[room].maxbounds.z = roomdata->pos.z + limits.maxZ;

    if (wasloaded == 0)
    {
        delete_room_data(room);
    }
}


void sub_GAME_7F0B95D8(s32 roomID)
{
    s32 numupdated = 0;
    s32 i;
    s32 j;
    s32 k;
    f32 value;

    for (i = 0; g_BgPortals[i].offset_portal != NULL; i++)
    {
        if ((roomID == g_BgPortals[i].connectedRoom1) || (roomID == g_BgPortals[i].connectedRoom2))
        {
            for (j = 0; j < g_BgPortals[i].offset_portal->numPoints; j++)
            {
                for (k = 0; k < 3; k++)
                {
                    value = (&g_BgPortals[i].offset_portal->point)[j].f[k];

                    if (value < g_BgRoomInfo[roomID].minbounds.f[k])
                    {
                        g_BgRoomInfo[roomID].minbounds.f[k] = value;
                        numupdated++;
                    }

                    if (g_BgRoomInfo[roomID].maxbounds.f[k] < value)
                    {
                        g_BgRoomInfo[roomID].maxbounds.f[k] = value;
                        numupdated++;
                    }
                }
            }
        }
    }

    if (numupdated);
}


// This assert belongs somewhere in the function
//#ifdef
//assert(levelportals[p].p->n>=3)
//#endif
void sub_GAME_7F0B96CC(s32 portalnum, f32 *out)
{
    f32 sp6c[3];
    f32 sp60[3];
    bg_portal_entry *portal;
    f32 min;
    f32 max;
    f32 dot;
    s32 i;

    for (i = 0; i < 3; i++)
    {
        sp6c[i] = (&g_BgPortals[portalnum].offset_portal->point)[0].f[i] - (&g_BgPortals[portalnum].offset_portal->point)[1].f[i];
    }

    for (i = 0; i < 3; i++)
    {
        sp60[i] = (&g_BgPortals[portalnum].offset_portal->point)[2].f[i] - (&g_BgPortals[portalnum].offset_portal->point)[1].f[i];
    }

    out[0] = (sp6c[1] * sp60[2]) - (sp6c[2] * sp60[1]);
    out[1] = (sp6c[2] * sp60[0]) - (sp6c[0] * sp60[2]);
    out[2] = (sp6c[0] * sp60[1]) - (sp6c[1] * sp60[0]);

    dot = sqrtf(((out[0] * out[0]) + (out[1] * out[1])) + (out[2] * out[2]));

    if (dot != 0.0f)
    {
        dot = 1.0f / dot;
    }

    out[0] *= dot;
    out[1] *= dot;
    out[2] *= dot;

    portal = g_BgPortals[portalnum].offset_portal;

    min = 3.4028235e38f;
    max = -3.4028235e38f;

    for (i = 0; i < portal->numPoints; i++)
    {
        min = (((((&portal->point)[i].f[0] * out[0]) + ((&portal->point)[i].f[1] * out[1])) + ((&portal->point)[i].f[2] * out[2])) < min) ? ((((&portal->point)[i].f[0] * out[0]) + ((&portal->point)[i].f[1] * out[1])) + ((&portal->point)[i].f[2] * out[2])) : (min);
        max = (((((&portal->point)[i].f[0] * out[0]) + ((&portal->point)[i].f[1] * out[1])) + ((&portal->point)[i].f[2] * out[2])) > max) ? ((((&portal->point)[i].f[0] * out[0]) + ((&portal->point)[i].f[1] * out[1])) + ((&portal->point)[i].f[2] * out[2])) : (max);
    }

    out[3] = min;
    out[4] = max;

    if (dot);
}


/**
 * Unknown, makes use of sub_GAME_7F0B96CC.
 *
 * Address 0x7F0B993C.
 */
s32 sub_GAME_7F0B993C(s32 arg0)
{
    struct PortalMetric metric;
    s32 padding;

    sub_GAME_7F0B96CC(arg0, &metric);

    if (((metric.normal.f[0] * metric.normal.f[0]) + (metric.normal.f[2] * metric.normal.f[2])) < 0.999f)
    {
        return 0;
    }

    return 1;
}


f32 sub_GAME_7F0B9990(s32 portalnum)
{
    s32 value;
    s32 shift;
    f32 result;

    value = g_BgPortals[portalnum].controlbytes2;
    shift = (value >> 4) & 0xf;
    result = (value & 0xf) * 0.25f;

    while (shift != 0) {
        result += result;
        shift--;
    }

    return result;
}


/**
 * Unreferenced.
 *
 * Address 0x7F0B9A14.
 */
u8 bgGetDataPortalsControlBytes2(s32 p)
{
    return g_BgPortals[p].controlbytes2;
}


void sub_GAME_7F0B9A2C(s32 portalnum)
{
    u8 value;
    s32 upper;

    value = g_BgPortals[portalnum].controlbytes2;

    if (value >= 0xff)
    {
        value = 0xff;
    }
    else
    {
        value++;
        upper = (value >> 4) & 0xf;

        if (upper > 0)
        {
            value |= 8;
        }
    }

    g_BgPortals[portalnum].controlbytes2 = value;
}


void sub_GAME_7F0B9A7C(s32 portalnum)
{
    u8 value;
    s32 temp;

    value = g_BgPortals[portalnum].controlbytes2;
    temp = value;

    if (((value >> 4) & 0xf) == 0)
    {
        if (temp > 0)
        {
            value--;
        }
    }
    else
    {
        value--;

        if ((value & 0xf) < 8)
        {
            value -= 8;
        }
    }

    g_BgPortals[portalnum].controlbytes2 = value;
}


/**
 * @param index: index into portal array.
 *
 * Address 0x7F0B9AE4.
 */
s32 bgGetDataPortalsControlBytes1Bit1(s32 index)
{
    return g_BgPortals[index].controlbytes1 & 1;
}



/**
 * @param index: index into portal array.
 *
 * Address 0x7F0B9B04.
 */
s32 bgGetDataPortalsControlBytes1Bit2(s32 index)
{
    return g_BgPortals[index].controlbytes1 & 2;
}



/**
 * @param index: index into portal array.
 *
 * Address 0x7F0B9B24.
 */
void bgSetDataPortalsControlBytes1Bit2(s32 index)
{
    g_BgPortals[index].controlbytes1 |= 2;
}



/**
 * @param index: index into portal array.
 *
 * Address 0x7F0B9B44.
 */
void bgClearDataPortalsControlBytes1Low2Bits(s32 index)
{
    g_BgPortals[index].controlbytes1 &= 0xFD;
}


/**
 * Swaps connected rooms.
 *
 * @param index: index into portal array.
 *
 * Address 0x7F0B9B64.
 */
s8 bgSwapConnectedRooms(s32 index)
{
    u8 t;

    t = g_BgPortals[index].connectedRoom1;
    g_BgPortals[index].connectedRoom1 = g_BgPortals[index].connectedRoom2;
    g_BgPortals[index].connectedRoom2 = t;
    return t; /* was falling off a non-void fn (UB at -O2); no caller uses the value */
}


/**
 * Address: 7F0B9B94
 */
void bgOrderPortal(s32 portalnum) // canonical name
{
    coord3d room1centre;
    coord3d room2centre;
    struct PortalMetric metric;
    f32 tmp;
    f32 tmp2;
    s32 roomnum2;
    s32 swapped;
    s32 roomnum1;
    f32 tmp1;

    roomnum1 = g_BgPortals[portalnum].connectedRoom1;
    roomnum2 = g_BgPortals[portalnum].connectedRoom2;

    bgGetRoomCenter(roomnum1, &room1centre);
    bgGetRoomCenter(roomnum2, &room2centre);

    sub_GAME_7F0B96CC(portalnum, &metric);

    if (metric.max - metric.min < 0.1f) {
#ifdef DEBUG
        osSyncPrintf("bg: bgOrderPortal: Portal '%s' not planar by %5.2f\n", bgDebPrintPORTALID(portalnum), metric.max - metric.min);
#endif
    }

    tmp2 = metric.normal.f[2];
    tmp1 = metric.normal.f[0] * room1centre.f[0] + metric.normal.f[1] * room1centre.f[1] + tmp2 * room1centre.f[2];

    swapped = 0;

    if (tmp1 > metric.max) {
        swapped = 1;

        bgSwapConnectedRooms(portalnum);

        metric.normal.x = -metric.normal.x;
        metric.normal.y = -metric.normal.y;
        metric.normal.z = -metric.normal.z;

        tmp = metric.min;
        metric.min = -metric.max;
        metric.max = -tmp;
    }

	tmp2 = metric.normal.f[0] * room2centre.f[0] + metric.normal.f[1] * room2centre.f[1] + metric.normal.f[2] * room2centre.f[2];

	if (tmp2 <= metric.min) {
        if (swapped) {
            bgSwapConnectedRooms(portalnum);
        }
    }

    if (swapped);
}


/**
 * Address: 7F0B9CC8
 *
 */
s32 bgGetPortalBetweenRooms(s32 room1, s32 room2, coord3d *arg2, coord3d *arg3)
{
    s32 bFoundPortal = FALSE;
    s32 i;
    s32 portalIndex = -1;

    #ifndef DEBUG
#ifdef TARGET_N64
            #define osSyncPrintf(x)
        #else
            #define osSyncPrintf(...) /* PC: gcc requires variadic for arg'd calls */
        #endif
    #endif

    for (i = 0; g_BgPortals[i].offset_portal != NULL; i++)
    {
        if (((g_BgPortals[i].connectedRoom1 == room1) && (g_BgPortals[i].connectedRoom2 == room2)) ||
            ((g_BgPortals[i].connectedRoom1 == room2) && (g_BgPortals[i].connectedRoom2 == room1)))
        {
            bFoundPortal = TRUE;
            if (sub_GAME_7F0B9F14(i, arg2, arg3) != 0)
            {
                if (portalIndex >= 0) osSyncPrintf("bg: bgGetPortalBetweenRooms(): Multiple portals join room \'%s\' and \'%s\'\ n", bgDebPrintROOMID(room1), bgDebPrintROOMID(room2));
                portalIndex = i;
            }
        }
    }

    if (portalIndex == -1 && !bFoundPortal) osSyncPrintf("bg: bgGetPortalBetweenRooms(): No portal joins room \'%s\' and \'%s\'\n", bgDebPrintROOMID(room1), bgDebPrintROOMID(room2));

    return portalIndex;
    #undef osSyncPrintf
}






/**
 * Toggles control bytes 1 lowest bit, based on toggle parameter.
 *
 * @param index: index into data portals.
 * @param toggle: When zero, sets lowest bit. Otherwise, clears lowest bit.
 *
 * Address 0x7F0B9DBC.
 */
void bgToggleDataPortalsContrlBytes1Bit1(s32 portal, s32 toggle)
{
    #ifdef DEBUG
    assert(portal<PORTMAX);
    #endif
    g_BgPortals[portal].controlbytes1 = (g_BgPortals[portal].controlbytes1 | 1) ^ (toggle != 0);
}




/**
 * Debug method, called from lvl.c.
 * Something to do with portals.
 *
 * Address 0x7F0B9DE4.
 */
s32 bgDebugRemoved7F0B9DE4(s32 arg0, s32 arg1, s32 arg2)
{
#if DEBUG
    // removed
    /*
 if (arg2 == NULL)
 {
     arg2 = {0};
 }
 *arg0          = *arg1 + *arg2;
 arg0[1]        = arg1[1] + arg2[1];
 arg0[2]        = arg1[2] + arg2[2];
 *(arg0 + 3)    = 0;
 arg0[4]        = 0.0;
 arg0[5]        = 0.0;
 *(arg0 + 6)    = (param_4 & 0xcf00cf40) >> 0x18;
 *(arg0 + 0x19) = 0;
 *(arg0 + 0x1a) = (param_4 & 0xcf00cf40) >> 8;
 *(arg0 + 0x1b) = 0x40;

 */

#endif

    return arg0;
}




/**
 * Debug method, called from lvl.c.
 * Something to do with portals.
 *
 * Address 0x7F0B9DF4.
 */
void bgRemoved7F0B9DF4(s32 arg0)
{
#if DEBUG
    // removed
#endif

    return;
}

/**
 * Unreferenced.
 *
 * Address 0x7F0B9DFC.
 */
void bgRemoved7F0B9DFC(s32 p)
{
#if DEBUG
        osSyncPrintf("bg: Error: Multiple portals intersect line; \'%s\' dropped ", bgDebPrintPORTALID(p));
#endif

    return;
}


//bg_consider_window82397B18
s32 sub_GAME_7F0B9E04(coord3d *arg0, coord3d *arg1)
{
    s32 bestportalnum = -1;
    s32 count = 0;
    f32 bestthing = MAXFLOAT;
    f32 thisthing;
    s32 i;

    for (i = 0; g_BgPortals[i].offset_portal; i++)
    {
        if (sub_GAME_7F0B9F14(i, arg0, arg1) != 0)
        {
            thisthing = D_80044900;

            if (thisthing < 0)
            {
                thisthing = -thisthing;
#if DEBUG
                osSyncPrintf("bg: Portal \'%s\' briefly considered for window\n", bgDebPrintPORTALID(i));
#endif
            }

            if (thisthing < bestthing)
            {
                if (count)
                {
#if DEBUG
                    osSyncPrintf("bg: Portal \'%s\' briefly considered for window\n", bgDebPrintPORTALID(i));
#endif
                }
                if (i);
                bestportalnum = i;
                bestthing = thisthing;
                count++;
            }
        }
    }

    return bestportalnum;
}


s32 sub_GAME_7F0B9F14(s32 portalnum, coord3d *pos1, coord3d *pos2)
{
    f32 value1;
    f32 value2;
    coord3d diff;
    s32 padding;
    s32 next;
    f32 tmp;
    coord3d edge;
    struct PortalMetric metric;
    f32 plane_d[1];
    coord3d cross;
    s32 i;
    u8 seenA;
    u8 seenB;

    seenA = 0;
    seenB = 0;

    sub_GAME_7F0B96CC(portalnum, &metric);

    diff.f[0] = pos2->f[0] - pos1->f[0];
    diff.f[1] = pos2->f[1] - pos1->f[1];
    diff.f[2] = pos2->f[2] - pos1->f[2];

    value1 = (pos1->f[0] * metric.normal.f[0] + pos1->f[1] * metric.normal.f[1] + pos1->f[2] * metric.normal.f[2]) * room_data_float1;
    value2 = (pos2->f[0] * metric.normal.f[0] + pos2->f[1] * metric.normal.f[1] + pos2->f[2] * metric.normal.f[2]) * room_data_float1;

    // @bug? Using min everywhere. In PD, the second half of this statement uses max.
    if ((value1 < metric.min && value2 < metric.min) || (value1 > metric.min && value2 > metric.min))
    {
        return 0;
    }

    D_80044900 = (value1 + value2) * 0.5f - metric.min;

    for (i = 0; i < g_BgPortals[portalnum].offset_portal->numPoints; i++)
    {
        next = (i + 1) % g_BgPortals[portalnum].offset_portal->numPoints;

        edge.f[0] = (&g_BgPortals[portalnum].offset_portal->point)[next].f[0] - (&g_BgPortals[portalnum].offset_portal->point)[i].f[0];
        edge.f[1] = (&g_BgPortals[portalnum].offset_portal->point)[next].f[1] - (&g_BgPortals[portalnum].offset_portal->point)[i].f[1];
        edge.f[2] = (&g_BgPortals[portalnum].offset_portal->point)[next].f[2] - (&g_BgPortals[portalnum].offset_portal->point)[i].f[2];

        cross.f[0] = edge.f[1] * diff.f[2] - edge.f[2] * diff.f[1];
        cross.f[1] = edge.f[2] * diff.f[0] - edge.f[0] * diff.f[2];
        cross.f[2] = edge.f[0] * diff.f[1] - edge.f[1] * diff.f[0];

        tmp = cross.f[0] * cross.f[0] + cross.f[1] * cross.f[1] + cross.f[2] * cross.f[2];

        if (tmp == 0.0f)
        {
            return 0;
        }

        plane_d[0] = cross.f[0] * (&g_BgPortals[portalnum].offset_portal->point)[i].f[0]
            + cross.f[1] * (&g_BgPortals[portalnum].offset_portal->point)[i].f[1]
            + cross.f[2] * (&g_BgPortals[portalnum].offset_portal->point)[i].f[2];

        tmp = (cross.f[0] * pos1->f[0] + cross.f[1] * pos1->f[1] + cross.f[2] * pos1->f[2]) * room_data_float1;

        if (tmp < plane_d[0])
        {
            if (seenB)
            {
                return 0;
            }

            seenA = 1;
        }
        else
        {
            if (seenA)
            {
                return 0;
            }

            seenB = 1;
        }
    }

    return value1 < metric.min ? 1 : 2;
}


bool bgIsBboxOverlapping(coord3d *portalbbmin, coord3d *portalbbmax, coord3d *propbbmin, coord3d *propbbmax)
{
    s32 i;

    for (i = 0; i < 3; i++)
    {
        if (propbbmin->f[i] > portalbbmax->f[i] || propbbmax->f[i] < portalbbmin->f[i])
        {
            return FALSE;
        }
    }

    return TRUE;
}


void sub_GAME_7F0BA2D4(coord3d *bbmin, coord3d *bbmax, s32 *room_list, s32 *count, s32 max_count)
{
    bg_portal_entry *portal_pts;
    f32 v;
    s32 cur_room;
    coord3d scaled_bbmin;
    coord3d scaled_bbmax;
    s32 cur_count;
    s32 i;
    s32 j;
    s32 k;
    s32 pad;
    s32 saved_count;
    coord3d portal_min;
    coord3d portal_max;
    s32 portal_idx;
    s32 *p;
    s32 other_room;
    
    cur_count = *count;
    i = 0;
    scaled_bbmin.x = bbmin->x * room_data_float1;
    scaled_bbmin.y = bbmin->y * room_data_float1;
    scaled_bbmin.z = bbmin->z * room_data_float1;
    scaled_bbmax.x = bbmax->x * room_data_float1;
    scaled_bbmax.y = bbmax->y * room_data_float1;
    scaled_bbmax.z = bbmax->z * room_data_float1;
    saved_count = cur_count;
    
    while (1)
    {
        if (i < cur_count)
        {
            p = (s32 *)((u8 *)room_list + (i << 2)); do {
            cur_room = *p;
            portal_idx = 0;
 
            if (g_BgPortals[0].offset_portal != ((void *) 0))
            {
                do
                {
                    if ((g_BgPortals[portal_idx].controlbytes1 & 1) || ((cur_room != g_BgPortals[portal_idx].connectedRoom1) && (cur_room != g_BgPortals[portal_idx].connectedRoom2)))
                    {
                        goto next_portal;
                    }
                    
#ifdef TARGET_N64
                    portal_min = *(coord3d *) &D_80044904;
                    portal_max = *(coord3d *) &D_80044910;
#else
                    /* the six adjacent s32 globals are two packed coord3d
                     * constants (+FLT_MAX x3 / -FLT_MAX x3) - read them
                     * element-wise instead of as 12-byte objects (adjacency
                     * family, sanitizer catch #7) */
                    portal_min.f[0] = *(f32 *) &D_80044904;
                    portal_min.f[1] = *(f32 *) &D_80044908;
                    portal_min.f[2] = *(f32 *) &D_8004490C;
                    portal_max.f[0] = *(f32 *) &D_80044910;
                    portal_max.f[1] = *(f32 *) &D_80044914;
                    portal_max.f[2] = *(f32 *) &D_80044918;
#endif
                    portal_pts = g_BgPortals[portal_idx].offset_portal;
                    
                    for (j = 0; j < portal_pts->numPoints; j++)
                    {
                        for (k = 0; k < 3; k++)
                        {
                            v = (&portal_pts->point)[j].f[k];
                            
                            if (v < portal_min.f[k])
                            {
                                portal_min.f[k] = v;
                            }
                            
                            if (portal_max.f[k] < v)
                            {
                                portal_max.f[k] = v;
                            }
                            
                            portal_pts = g_BgPortals[portal_idx].offset_portal;
                        }
 
                        if (portal_pts->numPoints);
                    }
                    
                if (bgIsBboxOverlapping(&portal_min, &portal_max, &scaled_bbmin, &scaled_bbmax))
                {
                    if (cur_room == g_BgPortals[portal_idx].connectedRoom1)
                    {
                        other_room = g_BgPortals[portal_idx].connectedRoom2;
                    }
                    else
                    {
                        other_room = g_BgPortals[portal_idx].connectedRoom1;
                    }
                    
                    for (k = 0; k < cur_count; k++)
                    {
                        if (room_list[k] == other_room)
                        {
                            break;
                        }
                    }
                    
                    if (k == cur_count)
                    {
                        if (cur_count < max_count)
                        {
                            room_list[cur_count] = other_room;
                            cur_count++;
                        }
                        
                        if (cur_count >= max_count)
                        {
                            *count = cur_count;
                            return;
                        }
                    }
                }
                    
next_portal:
                portal_idx++;
                }         
                while (g_BgPortals[portal_idx].offset_portal != NULL);
            }

            i++;
            p++;
                    
            } while (i < saved_count);
        }    
 
        if (cur_count == saved_count)
        {
            break;
        }
     
        saved_count = cur_count;
    }
    *count = cur_count;
}
