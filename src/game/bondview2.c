#ifndef TARGET_N64
#include "port_input.h"
#endif
#include <ultra64.h>
#include "game/bgfog.h"
#include "game/ob.h"
#include "game/ramromreplay.h"
#include "radar.h"
#include "game/dyn.h"
void bondviewKillCurrentPlayer(void);
#include <math.h>
#include <bondtypes.h>
#include <boss.h>
#include <fr.h>
#include <joy.h>
#include <music.h>
#include <snd.h>
#include <str.h>
#include <options.h>
#include "bg.h"
#include "bgroomtrans.h"
#include "blood_animation.h"
#include "bondhead.h"
#include "bondinv.h"
#include "bondview.h"
#include "cheat.h"
#include "chr.h"
#include "chr_b.h"
#include "chraction.h"
#include "chrai.h"
#include "debugmenu_handler.h"
#include "explosion.h"
#include "file.h"
#include "frametiming.h"
#include "front.h"
#include "glass.h"
#include "gun.h"
#include "initanitable.h"
#include "language.h"
#include "loadobjectmodel.h"
#include "lv.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "model.h"
#include "mp_music.h"
#include "mpmenu.h"
#include "objecthandler.h"
#include "objective_status.h"
#include "os_extension.h"
#include "player.h"
#include "propobj.h"
#include "quaternion.h"
#include "random.h"
#include "stan.h"
#include "stanintersection.h"
#include "textrelated.h"

#ifndef TARGET_N64
/* PORT: PC includes and widescreen aim state that this half of the split
 * bondview.c needs. g_bondviewSceneWideFactor is DEFINED in bondview.c beside
 * the pin/unpin helpers (declared in bondview.h). */
#include <stdio.h> /* stderr - port diagnostics */
#include <stdlib.h> /* getenv - port diagnostics */
extern f32 g_bondviewSceneWideFactor;
extern s32 portAdzyMode(void); /* video.c: midas.ini [display] adzy - used twice in solo_char_load */
extern s32 portReturnStartThisTick(void); /* video.c: START came from Return alone; weak 0 headless */

/* AIM LOCK (midas.ini [display] aim_lock): the crosshair stays pinned to the
 * screen centre, so in aim mode the aim input turns the view (modern ADS) and
 * outside it the manual gun-sway drift is zeroed. Auto-aim still steers and the
 * tank keeps its own turret handling. */
extern s32 portAimLock(void); /* video.c: midas.ini [display] aim_lock; weak 0 headless */
extern s32 portMpAimLock(void); /* video.c: [display] mp_aim_lock - multiplayer's own copy; weak 0 headless */
extern s32 get_is_ramrom_flag(void); /* demo playback gate for aim lock / scope
                                      * steady / no head roll */
static s32 portAimLockActive(void)
{
    /* two or more players read multiplayer's key, never the solo one, so a
     * user can want aim lock in MP but not in single player */
    /* never during demo playback - it rewrites what the recorded aim
     * stick means and where the recorded shots go, so the demo desyncs */
    return ((getPlayerCount() >= 2) ? portMpAimLock() : portAimLock()) && g_PlayerIsInTank != 1
        && get_is_ramrom_flag() == 0;
}

/* PORT: intro-camera skip needs a FRESH press - see bondviewFrozenCameraTick. */
#define PORT_INTRO_SKIP_BUTTONS (CONT_A | B_BUTTON | Z_TRIG | START_BUTTON | CONT_R | CONT_L)
static s32 s_PortIntroSkipArmed = 0;
/* PORT: fields of AI passes that have seen the Bond swirl; see its exit. */
static s32 s_PortSwirlAiFields = 0;
/* PORT: how the intro camera ended, for the log line when it advances. */
static u16 s_PortIntroSkipMask = 0;
static s32 s_PortIntroSkipPort = 0;
static u32 s_PortIntroStartCount = 0;
#endif

#ifdef VERSION_EU

    #define BONDVIEW_AUTOAIM_TIME 0x19 /* 25 */

    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_A 0x1a
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_B 0x19
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_C 0x64

    #define BONDVIEW_UPPER_TEXT_TIMER_A 0x33
    #define BONDVIEW_UPPER_TEXT_TIMER_B 0x32
    #define BONDVIEW_UPPER_TEXT_TIMER_C 0xc8

    #define EU_CAMERA_8003642C_ASPECT 1.19047617912f

#else

    #define BONDVIEW_AUTOAIM_TIME 0x1e /* 30 */

    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_A 0x1f
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_B 0x1e
    #define BONDVIEW_INTRO_CAMERA_BONDMESSCNT_C 0x78

    #define BONDVIEW_UPPER_TEXT_TIMER_A 0x3d
    #define BONDVIEW_UPPER_TEXT_TIMER_B 0x3c
    #define BONDVIEW_UPPER_TEXT_TIMER_C 0xf0

#endif

/*cannonically these are both*/
#define BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH 0x97
#define BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH 0x65
/*these*/
#define MAXTALKMESSLEN 150
#define MAXMESSAGELEN 100



#if defined(VERSION_US)
    #define BONDVIEW_2ND_FONTTABLE(_param) copy_2ndfonttable
    #define BONDVIEW_1ST_FONTTABLE(_param) copy_1stfonttable
#elif defined(VERSION_JP) || defined(VERSION_EU)
    #define BONDVIEW_2ND_FONTTABLE(_param) dword_CODE_bss_jp80079CEC[_param]
    #define BONDVIEW_1ST_FONTTABLE(_param) dword_CODE_bss_jp80079Cd8[_param]
#endif


#if defined(VERSION_US) || defined(VERSION_JP)
    #define BONDVIEW_VIEW_TOP_OFFSET_1 0x0C
    #define BONDVIEW_VIEW_TOP_OFFSET_2 0x28
    #define BONDVIEW_VIEW_TOP_OFFSET_3 0x10
#elif defined(VERSION_EU)
    #define BONDVIEW_VIEW_TOP_OFFSET_1 0x16
    #define BONDVIEW_VIEW_TOP_OFFSET_2 0x32
    #define BONDVIEW_VIEW_TOP_OFFSET_3 0x14
#endif


#if defined(VERSION_EU)
    #define TANKUPDATEROTATION_SCALE 0.904799997807f
    #define TANKTURRETVERTICALANGLERELATED_SCALE 0.928399980068f
    #define TANK_UNKD0_SCALE 0.79960000515f
    #define CHR_OBJ_ACCEL_SPEED_FACTOR 0.6f
    #define CHR_OBJ_MAXSPEED 6.0f
    #define MAX_SPEED_FACTOR 0.8f
    #define TANK_DAMAGE_PENTALTY_TICKS 75

    #define TANK_VERT_ANGLE_FACTOR 0.0716000199318f
    #define TANK_VERT_ANGLE_RAD_FACTOR 0.0952f

    #define MAX_AIMLOCK_SPEED_DEFAULT 0.8344f

    #define THREE_SECOND_TICKS 150
    #define PLAYER_TICKEXPLODE_FACTOR 12

    #define CLIPPING_CLOCK_FACTOR 0.765100002289f
    #define CLIPPING_FIELD88_FACTOR 0.234899997711f
    #define CLIPPING_FIELD8C_VALUE 12
    #define CLIPPING_FIELD90_VALUE -5.625f
#else
    #define TANKUPDATEROTATION_SCALE 0.92f
    #define TANKTURRETVERTICALANGLERELATED_SCALE 0.94f
    #define TANK_UNKD0_SCALE 0.83f
    #define CHR_OBJ_ACCEL_SPEED_FACTOR 0.5f
    #define CHR_OBJ_MAXSPEED 5.0f
    #define MAX_SPEED_FACTOR 0.8f
    #define TANK_DAMAGE_PENTALTY_TICKS 90

    #define TANK_VERT_ANGLE_FACTOR 0.0600000023842f
    #define TANK_VERT_ANGLE_RAD_FACTOR 0.0799999833107f
    #define MAX_AIMLOCK_SPEED_DEFAULT 0.86f

    #define THREE_SECOND_TICKS 180
    #define PLAYER_TICKEXPLODE_FACTOR 15

    #define CLIPPING_CLOCK_FACTOR 0.8f
    #define CLIPPING_FIELD88_FACTOR 0.19999999f
    #define CLIPPING_FIELD8C_VALUE 15
    #define CLIPPING_FIELD90_VALUE -4.5f
#endif

#define FULL_CROUCH_OFFSET -100.0f

#define SPEED_REGULAR_MAX  1.0f
#define SPEED_RUN_MAX      1.25f
#define SPEED_TICK_ADJUST  0.01f
#define TANK_MAX_SPEED     15.0f


#define FLOAT_TEN_A 10.0f
#define FLOAT_TEN_B 10.00f

#include "bondview_internal.h"

#define a8s "%8s"
#define aX4_0f "x %4.0f"
#define aY4_0f "y %4.0f"
#define aZ4_0f "z %4.0f"
#define aS3d "%s %3d"


vec3d g_ForceBondMoveOffset;

//CODE.bss:8007999C
s32 g_SurroundBondWithExplosionsTicks;
//CODE.bss:800799A0
s32 g_PlayerTickExplodeCreatePosition;
//CODE.bss:800799A4
s32 dword_CODE_bss_800799A4; // unused

//CODE.bss:800799A8
struct coord3d g_TankModelPositionOffset;

//CODE.bss:800799B4
s32 g_TankEngineSfxVolume;

/**
 * Address 0x800799B8.
 * State 0: begin.
 * State 1: Finished sitting down/turning, queue audio.
 * State 2: complete
*/
s32 g_EnterTankAudioState;

/**
 * Address 0x800799BC.
*/
f32 g_TankEnteringSitHeight;

/**
 * Address 0x800799C0.
*/
f32 g_TankEnteringSitHeightRemain;

/**
 * Address 0x800799C4.
*/
f32 g_TankEnterBondHorizAngleDeg;

/**
 * Address 0x800799C8.
*/
f32 g_TankEnterBondVertAngleDeg;

//CODE.bss:800799CC
f32 flt_CODE_bss_800799CC; // unused/padding

//CODE.bss:800799D0
struct coord3d g_EnterTankCoord;

//CODE.bss:800799DC
f32 flt_CODE_bss_800799DC; // unused/padding

//CODE.bss:800799E0
ITEM_IDS starting_weapon[2];

//CODE.bss:800799E8
struct coord3d flt_CODE_bss_800799E8;

//CODE.bss:800799F4
struct PropRecord* dword_CODE_bss_800799F4;

//CODE.bss:800799F8
PadRecord * g_CameraLookAtBondPad;
//CODE.bss:800799FC
CutsceneRecord *gBondViewCutscene;
//CODE.bss:80079A00
f32 flt_CODE_bss_80079A00;
//CODE.bss:80079A04
f32 flt_CODE_bss_80079A04;
//CODE.bss:80079A08
f32 flt_CODE_bss_80079A08;
//CODE.bss:80079A0C
f32 flt_CODE_bss_80079A0C;
//CODE.bss:80079A10
f32 flt_CODE_bss_80079A10;
//CODE.bss:80079A14
s32 dword_CODE_bss_80079A14;
//CODE.bss:80079A18
enum CAMERAMODE dword_CODE_bss_80079A18;
//CODE.bss:80079A1C
s32 dword_CODE_bss_80079A1C;
//CODE.bss:80079A20
s32 mission_timer;

#if defined(VERSION_JP) || defined(VERSION_EU)
//CODE.bss:80079A24
f32 watch_time_0;
#else
//CODE.bss:80079A24
s32 watch_time_0;
#endif

/**
 * Address 80079A28
 * EU .bss 80068508
*/
char stringbuffer_lowerleft[0x5][BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH];
char dword_CODE_bss_80079c21[0x04];

#if defined(BUGFIX_R1)
//CODE.bss:80079Cd8
s32 dword_CODE_bss_jp80079Cd8[0x05];
s32 dword_CODE_bss_jp80079CEC[0x05];
#endif

/**
 * Address 80079C28
 * EU .bss 80068738
*/
PadRecord *g_Startpad[0x10];

//CODE.bss:80079C68
s32 startpadcount;
//CODE.bss:80079C6C
s32 dword_CODE_bss_80079C6C;

#if defined LEFTOVERDEBUG
//CODE.bss:80079C70
/***/
char stringbuffer_top[0x2][BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH];
u16 dword_CODE_bss_80079d9E;
#endif

//CODE.bss:80079DA0
/**
 * EU .bss 80078780
*/
StandTilePoint *dword_CODE_bss_80079DA0;
//CODE.bss:80079DA4
StandTilePoint *dword_CODE_bss_80079DA4;

//CODE.bss:80079DA8
s32 dword_CODE_bss_80079DA8[BSS_80079DA8_LENGTH];

#ifndef VERSION_EU
//CODE.bss:80079DC8
char dword_CODE_bss_80079DC8[0x3C];
#else
char dword_CODE_bss_80079DC8[0x2][BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH];
char dword_CODE_bss_80079EF6[0x3C];
#endif

//CODE.bss:80079E04
f32 g_MpSwirlRotateSpeed;
//CODE.bss:80079E08
f32 g_MpSwirlAngleDegrees;
//CODE.bss:80079E0C
f32 g_MpSwirlForwardSpeed;
//CODE.bss:80079E10
f32 g_MpSwirlDistance;

#define ALIGN64_V3(val) (((val) | 0x3f) ^ 0x3f)

void solo_char_load(void)
{
    f32                         yaw;
    ChrRecord                  *self;
    struct texpool              pool;
    ModelFileHeader            *bodyheader;
    ModelFileHeader            *headheader;
    ModelFileHeader            *pitemheader;
    u8                         *weaponbuf0;
    u8                         *weaponbuf1;
    s32                         cursor;
    s32                         size0;
    s32                         size1;
    intptr_t                         helddst;
    WeaponObjRecord             weapon;
    struct player             **pp;
    s32                         prop;
    ITEM_IDS                    item;
    s32                         body;
    s32                         head;
    struct ItemModelFileRecord *unusedpitem;
    Model                      *model;

    yaw = bondviewGetPlayerYawRadians();
    if (g_CurrentPlayer->prop->chr == NULL)
    {
        weaponbuf0 = getPlayerWeaponBufferForHand(GUNRIGHT);
        weaponbuf1 = getPlayerWeaponBufferForHand(GUNLEFT);
        cursor     = 0;
        size0      = getSizeBufferWeaponInHand(GUNRIGHT);
        size1      = getSizeBufferWeaponInHand(GUNLEFT);
        weapon     = dummy_08_pp7_obj[0];
        item       = get_item_in_hand_or_watch_menu(GUNRIGHT);
        body       = BODY_Formal_Wear;
        head       = HEAD_Male_Brosnan_Default;
        model      = NULL;
        bodyheader = NULL;

        bondviewDeregisterPlayerRoom(g_CurrentPlayer);

        if (getPlayerCount() == 1)
        {
            helddst = fileGetBondForCurrentFolder();
            switch (g_CurrentPlayer->bondtype)
            {
                case CUFF_BLUE:
                    break;

                case CUFF_BOILER:
                    body = BODY_Special_Operations_Uniform;
                    break;

                case CUFF_JUNGLE:
                    body = BODY_Jungle_Fatigues;
                    break;

                case CUFF_SNOW:
                    body = BODY_Parka;
                    break;

                case CUFF_BROSNAN:
                    body = BODY_Brosnan_Tuxedo;
                    break;

                case CUFF_CONNERY:
                    body = BODY_Brosnan_Tuxedo;
                    break;

                case CUFF_DALTON:
                    body = BODY_Brosnan_Tuxedo;
                    break;

                case CUFF_MOORE:
                    body = BODY_Brosnan_Tuxedo;
                    break;

                case CUFF_FOLDER:
                    switch (helddst)
                    {
                        case BOND_BROSNAN:
                            body = BODY_Brosnan_Tuxedo;
                            break;

                        case BOND_CONNERY:
                            body = BODY_Brosnan_Tuxedo;
                            break;

                        case BOND_DALTON:
                            body = BODY_Brosnan_Tuxedo;
                            break;

                        case BOND_MOORE:
                            body = BODY_Brosnan_Tuxedo;
                            break;
                    }

                    break;
            }

            switch (helddst)
            {
                case BOND_BROSNAN:
                    switch (g_CurrentPlayer->bondtype)
                    {
                        case CUFF_BLUE:
                            break;

                        case CUFF_BOILER:
                            head = HEAD_Male_Brosnan_Boiler;
                            break;

                        case CUFF_JUNGLE:
                            head = HEAD_Male_Brosnan_Jungle;
                            break;

                        case CUFF_BROSNAN:
                            head = HEAD_Male_Brosnan_Tuxedo;
                            break;

                        case CUFF_CONNERY:
                            head = HEAD_Male_Brosnan_Tuxedo;
                            break;

                        case CUFF_DALTON:
                            head = HEAD_Male_Brosnan_Tuxedo;
                            break;

                        case CUFF_MOORE:
                            head = HEAD_Male_Brosnan_Tuxedo;
                            break;

                        case CUFF_FOLDER:
                            head = HEAD_Male_Brosnan_Tuxedo;
                            break;
                    }

                    break;

                case BOND_CONNERY:
                    head = HEAD_Male_Brosnan_Tuxedo;
                    break;

                case BOND_DALTON:
                    head = HEAD_Male_Brosnan_Tuxedo;
                    break;

                case BOND_MOORE:
                    head = HEAD_Male_Brosnan_Tuxedo;
                    break;
            }
        }
        else
        {
            head = get_player_mp_char_head(get_cur_playernum());
            body = get_player_mp_char_body(get_cur_playernum());
        }

#ifndef TARGET_N64
        /* midas.ini [display] adzy: Siberian Special Forces body at every solo
         * mission start (bondview_r.c forces CUFF_SNOW to match). MUST be set
         * here, before the header work below: makeonebody builds from the
         * already-loaded bodyheader, so changing `body` any later has no
         * visible effect. */
        {
            if (portAdzyMode() && getPlayerCount() == 1)
            {
                body = BODY_Siberian_Special_Forces;
            }
        }
#endif

        if (g_CameraMode == CAMERAMODE_SWIRL)
        {
            item = starting_weapon[GUNRIGHT];
        }

        if (getPlayerCount() == 1)
        {
            remove_item_in_hand(GUNLEFT);
            remove_item_in_hand(GUNRIGHT);
            texInitPool(&pool, weaponbuf1, size1);
            bodyheader  = get_ptr_itemheader_in_hand(GUNRIGHT);
            *bodyheader = *c_item_entries[body].header;
            load_object_fill_header(bodyheader, (u8 *)c_item_entries[body].filename, weaponbuf0, size0, &pool);
            cursor = get_pc_buffer_remaining_value((u8 *)c_item_entries[body].filename);

            do
            {
                cursor      = ALIGN64_V3(cursor + 0x3f);
                headheader  = (ModelFileHeader *)(weaponbuf0 + cursor);
                cursor      = ALIGN64_V3(cursor + sizeof(ModelFileHeader) + 0x3f);
                *headheader = *c_item_entries[head].header;

                if(1);

                load_object_fill_header(headheader, (u8 *)c_item_entries[head].filename, weaponbuf0 + cursor, size0 - cursor, &pool);
                cursor = ALIGN64_V3(get_pc_buffer_remaining_value((u8 *)c_item_entries[head].filename) + cursor + 0x3f);
                model  = (Model *)(weaponbuf0 + cursor);
                cursor = ALIGN64_V3(cursor + sizeof(Model) + 0x3f);
                modelCalculateRwDataLen(bodyheader);
                modelCalculateRwDataLen(headheader);

                {
                    u32 *animdata;
                    s32  nrec;
                    animdata = (u32 *)(weaponbuf0 + cursor);
                    nrec     = (bodyheader->numRecords + headheader->numRecords) + 0xa;
                    cursor   = ALIGN64_V3(cursor + nrec * sizeof(void *) + 0x3f);
                    animInit(model, bodyheader, animdata);
                    model->rwdatalen = nrec;
                }

            } while (FALSE);
        }
        else
        {
            bodyheader = c_item_entries[body].header;

            if (bodyheader->RootNode == NULL)
            {
                fileLoad(bodyheader, c_item_entries[body].filename);
            }
#ifndef VERSION_US
            if (c_item_entries[body].hasHead)
            {
                head       = -1;
                headheader = NULL;
            }
            else
#elif !defined(TARGET_N64)
            /* ADZY (PC only): VERSION_US always bolts a separate head on, which
             * suits every Bond body but gives the Siberian guard model (which
             * has its own head) a second, Brosnan head. Honour hasHead for adzy
             * only, as chraction.c does for guards, so vanilla is untouched. */
            if (portAdzyMode() && c_item_entries[body].hasHead)
            {
                head       = -1;
                headheader = NULL;
            }
            else
#endif
            {
                headheader = c_item_entries[head].header;

                if (headheader->RootNode == NULL)
                {
                    fileLoad(headheader, c_item_entries[head].filename);
                }
            }
        }

        g_CurrentPlayer->bodyModel = makeonebody(body, head, bodyheader, headheader, 0, model);
        modelSetScale(g_CurrentPlayer->bodyModel, g_CurrentPlayer->bodyModel->scale * 0.97f);
        init_GUARDdata_with_set_values(g_CurrentPlayer->prop, g_CurrentPlayer->bodyModel, &g_CurrentPlayer->prop->pos, yaw, g_CurrentPlayer->prop->stan, NULL);
        pp = &g_CurrentPlayer;
        (*pp)->prop->type = PROP_TYPE_VIEWER;
        self              = (*pp)->prop->chr;
        self->chrflags |= CHRFLAG_INIT;
        setsuboffset((*pp)->bodyModel, &(*pp)->prop->pos);
        setsubroty(g_CurrentPlayer->bodyModel, yaw);
#ifndef VERSION_US
        self->headnum = head;
        self->bodynum = body;
#endif
        prop = getPropForHeldItem(item);

        /* (s32): gcc makes enum PROP unsigned, so the -1 "no prop" sentinel
         * passed a plain >= 0 and crashed 4-player MP loads; the same fix is at
         * the other two getPropForHeldItem guards. */
        if ((s32) prop >= 0)
        {
            if (getPlayerCount() == 1)
            {
                helddst      = cursor;
                helddst      = ((intptr_t)weaponbuf0) + helddst;
                cursor       = ALIGN64_V3(cursor + sizeof(WeaponObjRecord) + 0x3f);
                pitemheader  = get_ptr_itemheader_in_hand(GUNLEFT);
                *pitemheader = *PitemZ_entries[prop].header;
                load_object_fill_header(pitemheader, (u8 *)PitemZ_entries[prop].filename, weaponbuf0 + cursor, size0 - cursor, &pool);
                get_pc_buffer_remaining_value((u8 *)PitemZ_entries[prop].filename);
                modelCalculateRwDataLen(pitemheader);
            }
            else
            {
                helddst     = 0;
                pitemheader = NULL;
            }

            something_with_generating_object(self, prop, item, 0, (WeaponObjRecord *)helddst, (ItemModelFileRecord *)pitemheader);
        }

        chrlvMergeKneelToStand(self, 0.0f);
    }
    else
    {
        self = g_CurrentPlayer->prop->chr;

        if (self->model->anim != NULL)
        {
            return;
        }

        self->chrflags |= CHRFLAG_INIT;
        chrlvMergeKneelToStand(self, 0.0f);
        setsuboffset(g_CurrentPlayer->bodyModel, &g_CurrentPlayer->prop->pos);
        setsubroty(g_CurrentPlayer->bodyModel, yaw);
    }
}


/**
 * Address 0x7F07A4A0.
 */
void bondviewRemovePlayerBody(void)
{
    if ((g_CurrentPlayer->prop->chr) && (getPlayerCount() == 1))
    {
        chrpropCleanupForRemoval(g_CurrentPlayer->prop);
        g_CurrentPlayer->prop->chr = NULL;
        g_CurrentPlayer->bodyModel = 0;
        g_bondviewForceDisarm = 1;
        bondviewUpdatePlayerRoom(g_CurrentPlayer);
    }
}


u32 bondviewGetCameraMode(void) {
    return g_CameraMode;
}


s32 pickDeathCameraAngles(PropRecord *prop1, coord3d *pos, PropRecord *prop2, coord3d *collision_pos, StandTile *tile, f32 camera_dist)
{
    s32 found;
    s32 outertries;
    f32 camclearance;
    f32 spD0;
    f32 angle;
    StandTile *spC8;
    coord3d spBC;
    coord3d spB0;
    f32 angleRange;
    f32 frac;
    f32 floorY;
    s32 lineok;
    s32 angletries;

    dword_CODE_bss_800799F4 = prop1;

    found = 0;
    outertries = 0;
    angleRange = M_TAU_F;

    while ((outertries <= 0x80) && (!found))
    {
        camclearance = g_CurrentPlayer->field_488.collision_radius;
        spD0 = 1500.0f + camclearance;

        angle = ((f32) randomGetNext()) * 2.3283064e-10f;
        angle = angle * angleRange;

        angletries = 0;

        while ((angletries < 0x10) && (!found))
        {
            angleRange = M_TAU_F;

            angle += 0.39269909f;

            if (angleRange <= angle)
            {
                angle -= angleRange;
            }

            spBC.x = sinf(angle);
            spBC.y = 0.0f;
            spBC.z = cosf(angle);

            spB0.x = (((f32 *) (&spBC))[0] * spD0) + pos->x;
            spB0.y = pos->y;
            spB0.z = (((f32 *) (&spBC))[2] * spD0) + pos->z;

            stanResetHits();

            spC8 = tile;

            sub_GAME_7F03D058(prop2, 0);

            lineok = stanTestLineUnobstructed(&spC8, collision_pos->x, collision_pos->z, spB0.x, spB0.z, 0x13, 0.0f, 1.0f, 0.0f, 1.0f);

            sub_GAME_7F03D058(prop2, 1);

            if (!lineok)
            {
                chrlvStanPointPointIntersection(collision_pos, &spBC, &spB0);

                {
                    f32 dx = spB0.x - collision_pos->x;
                    f32 dz = spB0.z - collision_pos->z;

                    spD0 = sqrtf((dx * dx) + (dz * dz));
                }
            }

            spD0 -= camclearance;

            if (camera_dist <= spD0)
            {
                frac = 1.0f;

                while ((0.0f < frac) && (!found))
                {
                    f32 sp90;
                    f32 candidateDist;

                    sp90 = spD0 - camera_dist;

                    candidateDist = ((f32) randomGetNext()) * 2.3283064e-10f;
                    candidateDist *= sp90;
                    candidateDist *= frac;
                    candidateDist += camera_dist;

                    flt_CODE_bss_800799E8.x = (spBC.x * candidateDist) + pos->x;
                    flt_CODE_bss_800799E8.y = pos->y;
                    flt_CODE_bss_800799E8.z = (spBC.z * candidateDist) + pos->z;

                    spC8 = tile;

                    sub_GAME_7F03D058(prop2, 0);

                    lineok = stanTestLineUnobstructed(&spC8, collision_pos->x, collision_pos->z, flt_CODE_bss_800799E8.x, flt_CODE_bss_800799E8.z, 0x13, 0.0f, 1.0f, 0.0f, 1.0f);

                    sub_GAME_7F03D058(prop2, 1);

                    if (lineok)
                    {
                        lineok = stanTestVolume(&spC8, flt_CODE_bss_800799E8.x, flt_CODE_bss_800799E8.z, camclearance, 0x1f, 0.0f, 1.0f);

                        if (lineok < 0)
                        {
                            floorY = stanGetPositionYValue(spC8, flt_CODE_bss_800799E8.x, flt_CODE_bss_800799E8.z);

                            flt_CODE_bss_800799E8.y = (floorY + camclearance) + ((((f32) randomGetNext()) * 2.3283064e-10f) * (185.0f - camclearance));

                            candidateDist = flt_CODE_bss_800799E8.y - pos->y;

                            if (((-1000.0f) < candidateDist) && (candidateDist < 1000.0f))
                            {
                                found = 1;
                            }
                        }
                    }

                    frac -= 0.25f;

                    if (pos);
                }
            }

            angletries++;
        }

        angleRange = M_TAU_F;

        outertries++;
    }

    return found;
}


// Address 0x7F07A9B8 NTSC.
void bondviewSetCameraMode(s32 arg0)
{
    s32 padding;
    s32 padding2;

    g_CameraMode = arg0;
    g_CameraAfterCinema = 0;

    if (g_CameraMode == CAMERAMODE_INTRO)
    {
#ifndef TARGET_N64
        s_PortIntroSkipArmed = 0;   /* re-arm on release; see the skip site */
        s_PortIntroSkipMask = 0;
        s_PortIntroSkipPort = 0;
        s_PortIntroStartCount = osGetCount();
#endif
        if ((ptr_random06cam_entry != NULL) && (get_recording_ramrom_flag() == 0) && (get_is_ramrom_flag() == 0))
        {
            camera_transition_timer = 0.0f;
            currentPlayerSetFadeColour(0, 0, 0, 1.0f);
            currentPlayerSetFadeFrac(60.0f, 0.0f);
            fogLoadLevelEnvironment(bossGetStageNum(), 1);
            g_CurrentPlayer->cameratile = NULL;
        }
        else
        {
            bondviewSetCameraMode(CAMERAMODE_SWIRL);
        }
    }
    else if (g_CameraMode == CAMERAMODE_FADESWIRL)
    {
        currentPlayerSetFadeColour(0, 0, 0, 0.0f);
        currentPlayerSetFadeFrac(60.0f, 1.0f);
    }
    else if (g_CameraMode == CAMERAMODE_MP)
    {
        g_MpSwirlRotateSpeed = 0.0f;
        g_MpSwirlAngleDegrees = -90.0f;
        g_MpSwirlForwardSpeed = 0.0f;
        g_MpSwirlDistance = 80.0f;
        fogLoadLevelEnvironment(bossGetStageNum(), 0);
    }
    else if (g_CameraMode == CAMERAMODE_SWIRL)
    {
        struct ModelAnimation *sp38;
        f32 sp78;
        f32 ftemp_3;
        f32 ftemp_1;
        struct ChrRecord *temp_v1;

        camera_fade_active = 0;
        currentPlayerSetFadeColour(0, 0, 0, 1.0f);
        currentPlayerSetFadeFrac(60.0f, 0.0f);
        fogLoadLevelEnvironment(bossGetStageNum(), 0);

        if ((g_IntroSwirl != 0) && (get_recording_ramrom_flag() == 0) && (get_is_ramrom_flag() == 0))
        {
#ifndef TARGET_N64
            s_PortSwirlAiFields = 0;
#endif
            camera_transition_timer = 0.0f;
            intro_camera_index = CAMERAMODE_INTRO;
            currentPlayerStartChrFade(0.0f, 1.0f);
            solo_char_load();

            // HACK: ptr_animation_table->data regalloc is backwards
            sp38 = (struct ModelAnimation *)((s32)stage_intro_anim_table[g_IntroAnimationIndex].anonymous_0 + (intptr_t)&ptr_animation_table->data);
            sp78 = stage_intro_anim_table[g_IntroAnimationIndex].anonymous_2;
            ftemp_1 = stage_intro_anim_table[g_IntroAnimationIndex].anonymous_1;
            ftemp_3 = stage_intro_anim_table[g_IntroAnimationIndex].anonymous_3;

            modelSetAnimation(
                g_CurrentPlayer->bodyModel,
                sp38,
                0,
                ftemp_1,
                ftemp_3,
                0.0f);

            if (sp78 > 0.0f)
            {
                modelSetAnimEndFrame(g_CurrentPlayer->bodyModel, sp78);
            }

            temp_v1 = g_CurrentPlayer->prop->chr;
            temp_v1->actiontype = ACT_BONDINTRO;
            temp_v1->sleep = 0;
            g_CurrentPlayer->cameratile = NULL;
        }
        else
        {
            bondviewSetCameraMode(CAMERAMODE_FP);
        }
    }
    else if (g_CameraMode == CAMERAMODE_FP)
    {
        if (bossGetStageNum() == LEVELID_CUBA)
        {
            currentPlayerSetFadeColour(0, 0, 0, 1.0f);
            currentPlayerSetFadeFrac(0.0f, 1.0f);
        }
        else if (camera_fade_active != 0)
        {
            currentPlayerSetFadeColour(0, 0, 0, 1.0f);
            currentPlayerSetFadeFrac(60.0f, 0.0f);
        }

        if (getPlayerCount() >= 2)
        {
            fogLoadLevelEnvironment(bossGetStageNum(), 0);
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
        {
            currentPlayerEquipWeaponWrapper(GUNLEFT, starting_weapon[GUNLEFT]);
            currentPlayerEquipWeaponWrapper(GUNRIGHT, starting_weapon[GUNRIGHT]);
        }

        stop_time_flag = 0;
    }
    else if (g_CameraMode == CAMERAMODE_DEATH_CAM_SP)
    {
        f32 var_f0;
        PropRecord *sp64;
        struct coord3d sp58;
        StandTile *var_v1;
        struct coord3d sp48;
        PropRecord *var_a2;
        struct ChrRecord *temp_v1_2;

        camera_transition_timer = 0.0f;
        intro_camera_index = CAMERAMODE_INTRO;
        currentPlayerSetFadeColour(0, 0, 0, 1.0f);
        currentPlayerSetFadeFrac(60.0f, 0.0f);

        if (g_ExplodeTankOnDeathFlag && (g_PlayerTankProp != NULL))
        {
            // removed
        }
        else
        {
            // This branch restarts Bond's death animation for his death replay
            g_PlayerIsInTank = 0;

            // struct copy
            g_CurrentPlayer->field_488 = g_CurrentPlayer->previous_collision_info;

            g_CurrentPlayer->vv_theta = g_CurrentPlayer->thetadie;
            g_CurrentPlayer->vv_verta = g_CurrentPlayer->vertadie;
            g_CurrentPlayer->prop->pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
            g_CurrentPlayer->prop->pos.f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
            g_CurrentPlayer->prop->pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2];
            g_CurrentPlayer->prop->stan = g_CurrentPlayer->field_488.current_tile_ptr;

            bondviewApplyVertaTheta();
            bondviewMoveAnimationTick(0, 0, 0);
            bondviewUpdatePlayerCollisionPositionFields();
            currentPlayerStartChrFade(0.0f, 1.0f);
            solo_char_load();

            modelSetAnimation(
                g_CurrentPlayer->bodyModel,
                objecthandlerGetModelAnim((Model *) &g_CurrentPlayer->model),
                objecthandlerGetModelGunhand(&g_CurrentPlayer->model),
                0.0f,
                0.5f,
                0.0f);

            temp_v1_2 = g_CurrentPlayer->prop->chr;
            temp_v1_2->actiontype = ACT_BONDDIE;
            temp_v1_2->sleep = 0;
            temp_v1_2->chrflags |= CHRFLAG_INIT;

            setsuboffset(g_CurrentPlayer->bodyModel, &g_CurrentPlayer->prop->pos);
            var_f0 = bondviewGetPlayerYawRadians();
            setsubroty(g_CurrentPlayer->bodyModel, var_f0);
        }

        if (g_ExplodeTankOnDeathFlag && (g_PlayerTankProp != NULL))
        {
            sp64 = g_PlayerTankProp;
            var_f0 = 500.0f; // distance to place the camera
            sp58.f[0] = g_PlayerTankProp->pos.f[0];
            sp58.f[1] = g_PlayerTankProp->pos.f[1];
            sp58.f[2] = g_PlayerTankProp->pos.f[2];
            var_a2 = g_PlayerTankProp;
            sp48.f[0] = g_PlayerTankProp->pos.f[0];
            sp48.f[1] = g_PlayerTankProp->pos.f[1];
            sp48.f[2] = g_PlayerTankProp->pos.f[2];
            var_v1 = g_PlayerTankProp->stan;
        }
        else
        {
            var_f0 = 200.0f; // distance to place the camera
            sp64 = g_CurrentPlayer->prop;
            sp58.f[0] = g_CurrentPlayer->field_3C4;
            sp58.f[1] = g_CurrentPlayer->field_3C8;
            sp58.f[2] = g_CurrentPlayer->field_3CC;
            var_a2 = g_CurrentPlayer->prop;
            sp48.f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
            sp48.f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
            sp48.f[2] = g_CurrentPlayer->field_488.collision_position.f[2];
            var_v1 = g_CurrentPlayer->field_488.current_tile_ptr;
        }

        if (pickDeathCameraAngles(sp64, &sp58, var_a2, &sp48, var_v1, var_f0) != 0)
        {
            if (camera_mode == 0)
            {
#ifdef DEBUG
                osSyncPrintf("mute\n");
#endif

                musicTrack1Play(M_INTROSWOOSH);
                sndSetScalerApplyVolumeAllSfxSlot(0.5f);
            }

            if ((g_ExplodeTankOnDeathFlag != 0) && (g_PlayerTankProp != NULL))
            {
                explosionCreate(g_PlayerTankProp, &g_PlayerTankProp->pos, g_PlayerTankProp->stan, 0xD, 0, get_cur_playernum(), g_PlayerTankProp->rooms, 0);
            }
        }
        else
        {
            // pickDeathCameraAngles has returned 0. This happens when no possible angles were found
            // to place the camera at the requested distance or when the three death replays are finished.
            bossRunTitleStage();
        }
    }
    else if (g_CameraMode == CAMERAMODE_DEATH_CAM_MP)
    {
        currentPlayerSetFadeColour(0, 0, 0, 0.0f);
        currentPlayerSetFadeFrac(60.0f, 1.0f);
    }
    else if (g_CameraMode == CAMERAMODE_POSEND)
    {
        solo_char_load();
        g_CurrentPlayer->cameratile = NULL;
    }
    else if (g_CameraMode == CAMERAMODE_FP_NOINPUT)
    {
        bondviewRemovePlayerBody();
        g_CameraMode = CAMERAMODE_FP;
    }
    else if (g_CameraMode == CAMERAMODE_FADE_TO_TITLE)
    {
        s32 var_s0;

        for (var_s0 = 0; var_s0 < getPlayerCount(); var_s0++)
        {
            set_cur_player(var_s0);
            currentPlayerSetFadeColour(0, 0, 0, 0.0f);
            currentPlayerSetFadeFrac(60.0f, 1.0f);
        }

        set_cur_player(0);
    }
}


void bondviewAdvanceCameraMode(void)
{
    enum CAMERAMODE mode = g_CameraMode;

    g_CameraMode = CAMERAMODE_NONE;
    g_CameraAfterCinema = CAMERAMODE_NONE;

    if (mode == CAMERAMODE_INTRO)
    {
#ifndef TARGET_N64
        /* Always on, so a report of a short intro (murkantor/Project-Midas#13) can say what ended it. */
        fprintf(stderr, "ge: intro: overview camera ended at %.1f fields, %u ms wall, by %s (buttons 0x%04x)\n",
                camera_transition_timer, (unsigned) ((u32) (osGetCount() - s_PortIntroStartCount) / 46875u),
                s_PortIntroSkipPort ? "a Port button" : (s_PortIntroSkipMask ? "a skip press" : "its timer"),
                (unsigned) s_PortIntroSkipMask);
#endif
        bondviewSetCameraMode(CAMERAMODE_FADESWIRL);
    }
    else if (mode == CAMERAMODE_FADESWIRL)
    {
        bondviewResetIntroCameraMessageDialogs();
        bondviewSetCameraMode(CAMERAMODE_SWIRL);
    }
    else if (mode != CAMERAMODE_MP)
    {
        if (mode == CAMERAMODE_SWIRL)
        {
#ifndef TARGET_N64
            fprintf(stderr, "ge: intro: Bond swirl ended after %d fields of AI passes\n", (int) s_PortSwirlAiFields);
#endif
            bondviewRemovePlayerBody();
            currentPlayerStartChrFade(0.0f, 1.0f); // What's the point of this call?
            bondviewSetCameraMode(CAMERAMODE_FP);
        }
        else if (mode != CAMERAMODE_FP)
        {
            if (mode == CAMERAMODE_DEATH_CAM_SP)
            {
                bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_MP);
            }
            else if (mode == CAMERAMODE_DEATH_CAM_MP)
            {
                camera_mode++;

                if (camera_mode < CAMERAMODE_SWIRL)
                {
                    bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_SP);
                }
            }
        }
    }
}


/**
 * Smoothly interpolate the camera between the points on the intro swirl path.
 */
void bondviewCalcIntroSwirlCamera(s32 index, f32 time, coord3d *pos, coord3d *lookat)
{
    struct SetupIntroSwirl *base;
    struct SetupIntroSwirl *loopbase;
    /* [12], not [10]: the loop writes indices 0..11 and
     * coord3dCubicSplineInterp reads &pointbuf[9] as a coord3d. With [10]
     * the overrun corrupts neighbouring locals on PC. */
    f32 pointbuf[12];
    struct SetupIntroSwirl *swirl;
    f32 frac;
    f32 *dst;

    base = g_IntroSwirl;
    swirl = base;
    swirl += index;
    frac = 0.0f;

    if (swirl->duration.fval > 0.0f)
    {
        frac = time / swirl->duration.fval;
    }

    {
        struct SetupIntroSwirl *entry;
        union {
            struct SetupIntroSwirl *swirl;
            struct player *player;
        } target;
        s32 i;

        loopbase = base + (u32) index;

        for (i = -1; i < 3; i++)
        {
            entry = loopbase;
            loopbase = base + (u32) index;
            target.swirl = entry + i;
            /* &pointbuf[i*3] would form a pointer before the array's start
             * for i == -1 (UB even unread); only
             * dst[3..5] are ever written/read below, so anchor dst there
             * instead and use dst[0..2] (same bytes, always in-bounds). */
            dst = &pointbuf[i * 3 + 3];

            if (i < 0)
            {
                if (target.swirl < base)
                {
                    entry = base;
                }
                else
                {
                    entry = target.swirl;
                }
            }
            else
            {
                while (entry < target.swirl)
                {
                    if (entry[1].bitflags & 1)
                    {
                        break;
                    }

                    entry++;
                }
            }

            if (entry->bitflags & 2)
            {
                target.player = g_CurrentPlayer;
                dst[0] = (entry->offsetfromBond[2].fval * target.player->field_488.theta_transform.f[0])
                    + (entry->offsetfromBond[0].fval * target.player->field_488.theta_transform.f[2]);
                dst[1] = entry->offsetfromBond[1].fval;
                dst[2] = (entry->offsetfromBond[2].fval * target.player->field_488.theta_transform.f[2])
                    - (entry->offsetfromBond[0].fval * target.player->field_488.theta_transform.f[0]);
            }
            else
            {
                dst[0] = entry->offsetfromBond[0].fval;
                dst[1] = entry->offsetfromBond[1].fval;
                dst[2] = entry->offsetfromBond[2].fval;
            }
        }
    }

    {
        f32 scale;

        base = swirl;
        scale = base->scale.fval;
        base = (void *)(index << 5);

        coord3dCubicSplineInterp((coord3d *) &pointbuf[0], (coord3d *) &pointbuf[3], (coord3d *) &pointbuf[6], (coord3d *) &pointbuf[9], frac, scale, pos);

        pos->x += g_CurrentPlayer->field_3C4;
        pos->y += g_CurrentPlayer->field_3C8;
        pos->z += g_CurrentPlayer->field_3CC;

        lookat->x = g_CurrentPlayer->field_3C4;
        lookat->y = g_CurrentPlayer->field_3C8;
        lookat->z = g_CurrentPlayer->field_3CC;

        swirl = (void *)(((uintptr_t) g_IntroSwirl) + (uintptr_t) base);

        if (!(swirl->bitflags & 4))
        {
            if (!(swirl[1].bitflags & 4))
            {
                scale = 1.0f;
            }
            else
            {
                scale = 1.0f - frac;
            }
        }
        else if (swirl[1].bitflags & 4)
        {
            scale = 0.0f;
        }
        else
        {
            scale = frac;
        }

        lookat->x += (g_CurrentPlayer->field_488.applied_view.x * 40.0f) * scale;
        lookat->y += (g_CurrentPlayer->field_488.applied_view.y * 40.0f) * scale;
        lookat->z += (g_CurrentPlayer->field_488.applied_view.z * 40.0f) * scale;
    }
}


/**
 * US address 7F07B56C.
 * JP address 7F07BB8C.
 * EU address 7F07B604.
*/
#ifndef TARGET_N64
#define PORT_SKIPHINT_SHOW 300   /* 5 s at 60 vsyncs/s, fully shown */
#define PORT_SKIPHINT_FADE 30    /* then 0.5 s to fade out */
static s32 g_portSkipHintFields = 0;   /* > 0: the credits' skip hint is up */
#endif

void bondviewFrozenCameraTick(u16 buttons, u16 oldbuttons, struct coord3d *pos, struct coord3d *pos2, struct coord3d *offset, StandTile **stan, struct coord3d *arg6)
{
    s32 i;
    f32 sp38;
    s32 i2;
    f32 sp30 = 0.0f;
    s32 padding2;
    f32 zero = 0.0f;
    void *p;
    struct PadRecord *setupPad;

#ifndef TARGET_N64
    { /* SKIP THE ENDING: START while the credits roll marks them finished,
       * so the stage's own AI runs its ending (fade, exit_level), and tells
       * front.c's interface_menu18_displaycast to skip the cast parade too. */
        extern s32 g_portCreditsSkipParade;
        extern s32 portInputAnyHeld(void);   /* input_pc.c: any pad / key / mouse button */
        static s32 anyheld = 0;
        const s32 held = portInputAnyHeld();
        const s32 fresh = (buttons & ~oldbuttons) != 0 || (held && !anyheld);

        anyheld = held;
        /* Two presses: START skips only while the hint is up; otherwise it
         * is the press that brings the hint up, like any other button. */
        if (bossGetStageNum() == LEVELID_CUBA && credits_state == 1
            && (buttons & ~oldbuttons & START_BUTTON) && g_portSkipHintFields > 0)
        {
            credits_state = 2;
            g_portCreditsSkipParade = TRUE;
            printf("ge: credits: START - ending skipped, the parade will be too\n");
        }
        /* The skip hint appears on demand: every fresh press restarts a
         * 5-second clock (vsync units, so tick-rate independent), and
         * bondviewRenderCredits draws it while it runs, fading at the end. */
        if (bossGetStageNum() == LEVELID_CUBA && credits_state == 1)
        {
            if (fresh)
            {
                g_portSkipHintFields = PORT_SKIPHINT_SHOW + PORT_SKIPHINT_FADE;
            }
            else if (g_portSkipHintFields > 0)
            {
                g_portSkipHintFields -= g_ClockTimer;
                if (g_portSkipHintFields < 0)
                {
                    g_portSkipHintFields = 0;
                }
            }
        }
        else
        {
            g_portSkipHintFields = 0;
        }
    }
#endif

    if ((g_CameraMode == CAMERAMODE_INTRO) || (g_CameraMode == CAMERAMODE_FADESWIRL))
    {
        if (g_CameraMode == CAMERAMODE_INTRO)
        {
            if ((camera_transition_timer < 120.0f) && ((camera_transition_timer + g_GlobalTimerDelta) >= 120.0f))
            {
#if defined(VERSION_US)
                setFontTables(ptrFontZurichBoldChars, ptrFontZurichBold);
                hudmsgBottomShow(ptr_random06cam_entry->lang1c.lang_ptr);
#else
                hudmsgBottomShow(ptr_random06cam_entry->lang1c.lang_ptr, ptrFontZurichBoldChars, ptrFontZurichBold);
#endif
            }

            if (ptr_random06cam_entry->lang20.lang_ptr != NULL)
            {
                if ((camera_transition_timer < 300.0f) && ((camera_transition_timer + g_GlobalTimerDelta) >= 300.0f))
                {
#if defined(VERSION_US)
                    hudmsgBottomShow(ptr_random06cam_entry->lang20.lang_ptr);
#else
                    hudmsgBottomShow(ptr_random06cam_entry->lang20.lang_ptr, ptrFontZurichBoldChars, ptrFontZurichBold);
#endif
                }

                if (camera_transition_timer > 480.0f)
                {
                    g_CameraAfterCinema = CAMERAMODE_INTRO;
                }
            }
            else if (camera_transition_timer > 300.0f)
            {
                g_CameraAfterCinema = CAMERAMODE_INTRO;
            }

            camera_transition_timer += g_GlobalTimerDelta;

#ifndef TARGET_N64
            /* PORT: require a FRESH press to skip the intro camera. Our loads
             * are so fast that a button still held from the briefing would read
             * as a new press and skip it instantly, so arming waits until the
             * skip buttons have been seen released once. */
            if (!s_PortIntroSkipArmed && (buttons & PORT_INTRO_SKIP_BUTTONS) == 0)
            {
                s_PortIntroSkipArmed = 1;
            }

            {
                /* on the built-in controller layout the face buttons are Port
                 * actions (use / reload / prev / next gadget), so a fresh press
                 * of any of them also skips, like vanilla A and B */
                extern s32 portJoyPortSkipPressed(s32 pad);
                s32 portskip = portJoyPortSkipPressed(get_cur_playernum());
                if (s_PortIntroSkipArmed
                    && (lvlGetControlsLockedFlag() == 0)
                    && ((buttons & ~oldbuttons & PORT_INTRO_SKIP_BUTTONS) || portskip))
                {
                    g_CameraAfterCinema = CAMERAMODE_INTRO;
                    s_PortIntroSkipMask = buttons & ~oldbuttons & PORT_INTRO_SKIP_BUTTONS;
                    s_PortIntroSkipPort = portskip;
                }
            }
#else
            if ((lvlGetControlsLockedFlag() == 0)
                && (buttons & ~oldbuttons & (CONT_A | B_BUTTON | Z_TRIG | START_BUTTON | CONT_R | CONT_L)))
            {
                g_CameraAfterCinema = CAMERAMODE_INTRO;
            }
#endif
        }
        else if (g_CurrentPlayer->colourfadetimemax60 < 0.0f)
        {
            g_CameraAfterCinema = CAMERAMODE_INTRO;
        }

        pos->f[0] = ptr_random06cam_entry->unk04.fval;
        pos->f[1] = ptr_random06cam_entry->unk08.fval;
        pos->f[2] = ptr_random06cam_entry->unk0C.fval;

        pos2->f[0] = pos->f[0] + (cosf(ptr_random06cam_entry->unk14.fval) * sinf(ptr_random06cam_entry->unk10.fval));
        pos2->f[1] = pos->f[1] + sinf(ptr_random06cam_entry->unk14.fval);
        pos2->f[2] = pos->f[2] - (cosf(ptr_random06cam_entry->unk14.fval) * cosf(ptr_random06cam_entry->unk10.fval));

        p = &g_CurrentSetup.pads[ptr_random06cam_entry->unk18];
        setupPad = p;
        *stan = setupPad->stan;

        arg6->f[0] = setupPad->pos.f[0];
        arg6->f[1] = setupPad->pos.f[1];
        arg6->f[2] = setupPad->pos.f[2];
    }
    else if (g_CameraMode == CAMERAMODE_MP)
    {
        /**
        * CAMERAMODE_MP: Perfect Dark method playerTickMpSwirl
        */
        if (get_player_position_in_shuffled(get_cur_playernum()) == 0)
        {
            for (i2=0; i2<g_ClockTimer; i2++)
            {
                if (g_MpSwirlAngleDegrees < 179.5f)
                {
                    if (g_MpSwirlAngleDegrees < -20.0f)
                    {
                        g_MpSwirlRotateSpeed += 0.1f;
                    }

                    if (g_MpSwirlAngleDegrees > 110.0f)
                    {
                        g_MpSwirlRotateSpeed -= 0.1f;
                    }

                    g_MpSwirlAngleDegrees += g_MpSwirlRotateSpeed;
                }
#if defined(VERSION_EU)
                if (g_MpSwirlAngleDegrees >= 179.5f)
#else
                else
#endif
                {
                    g_MpSwirlAngleDegrees = 180.0f;
                }

                if (g_MpSwirlAngleDegrees > 80.0f)
                {
                    if (g_MpSwirlDistance > 60.0f)
                    {
                        g_MpSwirlForwardSpeed -= 0.1f;
                    }
                    else
                    {
                        g_MpSwirlForwardSpeed += 0.015f;
                    }

                    g_MpSwirlDistance += g_MpSwirlForwardSpeed;

                    if (g_MpSwirlDistance < 1.0f)
                    {
                        g_MpSwirlDistance = 1.0f;
                    }
                }
            }
        }

        sp38 = ((g_MpSwirlAngleDegrees - g_CurrentPlayer->vv_theta) * M_PI_F) / 180.0f;

        pos->f[0] = g_CurrentPlayer->field_488.pos.f[0] + (sinf(sp38) * g_MpSwirlDistance);
        pos->f[1] = g_CurrentPlayer->field_488.pos.f[1] + (g_MpSwirlDistance * 0.08f);
        pos->f[2] = g_CurrentPlayer->field_488.pos.f[2] + (cosf(sp38) * g_MpSwirlDistance);

        pos2->f[0] = g_CurrentPlayer->field_488.pos.f[0];
        pos2->f[1] = g_CurrentPlayer->field_488.pos.f[1];
        pos2->f[2] = g_CurrentPlayer->field_488.pos.f[2];

        *stan = g_CurrentPlayer->prop->stan;

        arg6->f[0] = g_CurrentPlayer->field_488.pos.f[0];
        arg6->f[1] = g_CurrentPlayer->field_488.pos.f[1] + (g_MpSwirlDistance * 0.08f);
        arg6->f[2] = g_CurrentPlayer->field_488.pos.f[2];

#ifndef TARGET_N64
        { /* GE_SWIRLDBG=1: one line per player per tick of the MP intro swirl.
           * The angle and distance are shared, so a screen that stutters
           * alone must have its own input moving: the player's position or
           * facing (JUMP marks a camera step that differs from the last one by
           * more than 4 units), or a tick that did not advance (clk 0). */
            static s32 dbg = -1;
            static struct coord3d lastpos[4];
            static f32 laststep[4];
            static s32 have[4];

            if (dbg < 0)
            {
                const char *e = getenv("GE_SWIRLDBG");
                dbg = (e != NULL && *e != '\0' && *e != '0') ? 1 : 0;
            }
            if (dbg)
            {
                const s32 pn = get_cur_playernum() & 3;
                f32 step = 0.0f;

                if (have[pn])
                {
                    const f32 dx = pos->f[0] - lastpos[pn].f[0];
                    const f32 dy = pos->f[1] - lastpos[pn].f[1];
                    const f32 dz = pos->f[2] - lastpos[pn].f[2];
                    step = sqrtf(dx * dx + dy * dy + dz * dz);
                }
                printf("ge: swirl: p%d sh%d clk %d ang %.2f dist %.2f | at %.1f %.1f %.1f theta %.2f | cam step %.2f%s\n",
                       (int) pn, (int) get_player_position_in_shuffled(get_cur_playernum()), (int) g_ClockTimer,
                       g_MpSwirlAngleDegrees, g_MpSwirlDistance,
                       g_CurrentPlayer->field_488.pos.f[0], g_CurrentPlayer->field_488.pos.f[1],
                       g_CurrentPlayer->field_488.pos.f[2], g_CurrentPlayer->vv_theta, step,
                       (have[pn] && fabsf(step - laststep[pn]) > 4.0f) ? "  <-- JUMP" : "");
                lastpos[pn] = *pos;
                laststep[pn] = step;
                have[pn] = 1;
            }
        }
#endif

#if defined(VERSION_EU)
        if (((get_player_position_in_shuffled(get_cur_playernum()) + 1) == getPlayerCount()) && (g_MpSwirlDistance < 5.0f))
        {
            g_CameraAfterCinema = CAMERAMODE_INTRO;
        }
#else
        if (g_MpSwirlDistance < 5.0f)
        {
            g_CameraAfterCinema = CAMERAMODE_INTRO;
        }
#endif
    }
    else if (g_CameraMode == CAMERAMODE_SWIRL)
    {
        camera_transition_timer += g_GlobalTimerDelta;

        while (g_IntroSwirl[intro_camera_index].unk18.fval <= camera_transition_timer)
        {
            if (!(g_IntroSwirl[intro_camera_index + 3].unk04 & 1))
            {
                camera_transition_timer -= g_IntroSwirl[intro_camera_index].unk18.fval;
                intro_camera_index++;
            }
            else
            {
                camera_transition_timer = g_IntroSwirl[intro_camera_index].unk18.fval;
                g_CameraAfterCinema = CAMERAMODE_INTRO;
                break;
            }
        }

        sp30 += (g_IntroSwirl[intro_camera_index].unk18.fval - camera_transition_timer);

        for (i = intro_camera_index + 1; !(g_IntroSwirl[i+2].unk04 & 1); i++)
        {
            sp30 += g_IntroSwirl[i].unk18.fval;
        }

        // Fade player body from opaque to transparent just before the player takes control.
        if ((sp30 < 30.0f) && ((sp30 + g_GlobalTimerDelta) >= 30.0f))
        {
            currentPlayerStartChrFade(30.0f, 0.0f);
        }

        if (camera_fade_active != 0)
        {
            if (currentPlayerIsFadeComplete() != 0)
            {
                g_CameraAfterCinema = CAMERAMODE_INTRO;
            }
        }

        if ((sp30 > 60.0f) && (camera_fade_active == 0))
        {
#ifndef TARGET_N64
            /* the Bond swirl has its own skip check (only Y skips it);
             * the Port face buttons count here too */
            extern s32 portJoyPortSkipPressed(s32 pad);
            s32 portskip = portJoyPortSkipPressed(get_cur_playernum());
#else
            s32 portskip = 0;
#endif
            if ((lvlGetControlsLockedFlag() == 0)
                && ((buttons & ~oldbuttons & (A_BUTTON | B_BUTTON | Z_TRIG | START_BUTTON | L_TRIG | R_TRIG)) || portskip))
            {
                camera_fade_active = 1;
                currentPlayerSetFadeColour(0, 0, 0, g_CurrentPlayer->colourscreenfrac);

                if (currentPlayerIsFadeComplete() != 0)
                {
                    currentPlayerSetFadeFrac(60.0f, 1.0f);
                }
                else
                {
                    currentPlayerSetFadeFrac(g_CurrentPlayer->colourfadetime60, 1.0f);
                }
            }
        }

#ifndef TARGET_N64
        /* Silo's timer and Statue's guards wait for this mode, and chr AI runs every
         * 2 fields here (portAiGovDefer), so hold the exit until an AI pass saw it. */
        if (s_PortSwirlAiFields < 2)
        {
            g_CameraAfterCinema = CAMERAMODE_NONE;
        }

        s_PortSwirlAiFields += g_ClockTimer;
#endif

        bondviewCalcIntroSwirlCamera(intro_camera_index, camera_transition_timer, pos, pos2);

        if (g_IntroSwirl[intro_camera_index].unk1C >= 0)
        {
            p = &g_CurrentSetup.pads[g_IntroSwirl[intro_camera_index].unk1C];
            setupPad = p;
            *stan = setupPad->stan;

            arg6->f[0] = setupPad->pos.f[0];
            arg6->f[1] = setupPad->pos.f[1];
            arg6->f[2] = setupPad->pos.f[2];
        }
        else
        {
            *stan = g_CurrentPlayer->field_488.current_tile_ptr;
            arg6->f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
            arg6->f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
            arg6->f[2] = g_CurrentPlayer->field_488.collision_position.f[2];
        }
    }
    else if ((g_CameraMode == CAMERAMODE_DEATH_CAM_SP) || (g_CameraMode == CAMERAMODE_DEATH_CAM_MP))
    {
        if (g_CameraMode == CAMERAMODE_DEATH_CAM_SP)
        {
            camera_transition_timer += g_GlobalTimerDelta;

            if (g_CurrentPlayer->bodyModel != NULL)
            {
                if (modelGetAnimFrame((Model *) g_CurrentPlayer->bodyModel)
                    >= modelGetAnimEndFrame((Model *) g_CurrentPlayer->bodyModel))
                {
                    g_CameraAfterCinema = CAMERAMODE_INTRO;
                }
            }
            else if (camera_transition_timer >= 180.0f)
            {
                g_CameraAfterCinema = CAMERAMODE_INTRO;
            }

            if ((buttons & ~oldbuttons & (CONT_A | B_BUTTON | Z_TRIG | START_BUTTON))
                && (g_CurrentPlayer->bonddead)
                && (g_CurrentPlayer->redbloodfinished)
                && (g_CurrentPlayer->deathanimfinished))
            {
                g_CameraAfterCinema = CAMERAMODE_INTRO;
                camera_mode = CAMERAMODE_FADESWIRL;
            }
        }
        else if (g_CameraMode == CAMERAMODE_DEATH_CAM_MP)
        {
            camera_transition_timer += g_GlobalTimerDelta;

            if (g_CurrentPlayer->colourfadetimemax60 < 0.0f)
            {
                g_CameraAfterCinema = CAMERAMODE_INTRO;
            }

            if ((buttons & ~oldbuttons & (CONT_A | B_BUTTON | Z_TRIG | START_BUTTON))
                && (g_CurrentPlayer->bonddead)
                && (g_CurrentPlayer->redbloodfinished)
                && (g_CurrentPlayer->deathanimfinished))
            {
                camera_mode = CAMERAMODE_FADESWIRL;
            }
        }

        pos->f[0] = flt_CODE_bss_800799E8.f[0];
        pos->f[1] = flt_CODE_bss_800799E8.f[1];
        pos->f[2] = flt_CODE_bss_800799E8.f[2];

        if (dword_CODE_bss_800799F4 == g_CurrentPlayer->prop)
        {
            pos2->f[0] = g_CurrentPlayer->field_3C4;
            pos2->f[1] = g_CurrentPlayer->field_3C8;
            pos2->f[2] = g_CurrentPlayer->field_3CC;
        }
        else
        {
            pos2->f[0] = dword_CODE_bss_800799F4->pos.f[0];
            pos2->f[1] = dword_CODE_bss_800799F4->pos.f[1];
            pos2->f[2] = dword_CODE_bss_800799F4->pos.f[2];
        }

        *stan = g_CurrentPlayer->field_488.current_tile_ptr;

        arg6->f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
        arg6->f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
        arg6->f[2] = g_CurrentPlayer->field_488.collision_position.f[2];
    }
    else if (g_CameraMode == CAMERAMODE_POSEND)
    {
        if (g_CameraLookAtBondPad != NULL)
        {
            pos->f[0] = g_CameraLookAtBondPad->pos.f[0];
            pos->f[1] = g_CameraLookAtBondPad->pos.f[1];
            pos->f[2] = g_CameraLookAtBondPad->pos.f[2];

            pos2->f[0] = g_CurrentPlayer->field_3C4;
            pos2->f[1] = g_CurrentPlayer->field_3C8;
            pos2->f[2] = g_CurrentPlayer->field_3CC;

            *stan = g_CameraLookAtBondPad->stan;

            arg6->f[0] = g_CameraLookAtBondPad->pos.f[0];
            arg6->f[1] = g_CameraLookAtBondPad->pos.f[1];
            arg6->f[2] = g_CameraLookAtBondPad->pos.f[2];

            return;
        }

        if (gBondViewCutscene != NULL)
        {
            if (isNotBoundPad(gBondViewCutscene->pad))
            {
                p = &g_CurrentSetup.pads[gBondViewCutscene->pad];
            }
            else
            {
                p = &g_CurrentSetup.boundpads[getBoundPadNum(gBondViewCutscene->pad)];
            }

            pos->f[0] = gBondViewCutscene->pos.f[0];
            pos->f[1] = gBondViewCutscene->pos.f[1];
            pos->f[2] = gBondViewCutscene->pos.f[2];

            setupPad = p;
            *stan = setupPad->stan;

            arg6->f[0] = setupPad->pos.f[0];
            arg6->f[1] = setupPad->pos.f[1];
            arg6->f[2] = setupPad->pos.f[2];

            if (dword_CODE_bss_80079A18 == CAMERAMODE_INTRO)
            {
                pos2->f[0] = g_CurrentPlayer->field_3C4;
                pos2->f[1] = g_CurrentPlayer->field_3C8;
                pos2->f[2] = g_CurrentPlayer->field_3CC;
            }
            else
            {
                pos2->f[0] = pos->f[0] + (cosf(gBondViewCutscene->verta) * sinf(gBondViewCutscene->theta));
                pos2->f[1] = pos->f[1] + sinf(gBondViewCutscene->verta);
                pos2->f[2] = pos->f[2] - (cosf(gBondViewCutscene->verta) * cosf(gBondViewCutscene->theta));
            }

            return;
        }

        if (isNotBoundPad(dword_CODE_bss_80079A14))
        {
            setupPad = &g_CurrentSetup.pads[dword_CODE_bss_80079A14];
        }
        else
        {
            setupPad = (struct PadRecord*)&g_CurrentSetup.boundpads[getBoundPadNum(dword_CODE_bss_80079A14)];
        }

        *stan = setupPad->stan;

        arg6->f[0] = setupPad->pos.f[0];
        arg6->f[1] = setupPad->pos.f[1];
        arg6->f[2] = setupPad->pos.f[2];

        pos2->f[0] = setupPad->pos.f[0] + cosf(flt_CODE_bss_80079A00) * 0.0f;
        pos2->f[1] = setupPad->pos.f[1] + flt_CODE_bss_80079A10;
        pos2->f[2] = setupPad->pos.f[2] + sinf(flt_CODE_bss_80079A00) * 0.0f;

        pos->f[0] = setupPad->pos.f[0] + (sinf(flt_CODE_bss_80079A00) * flt_CODE_bss_80079A08) + cosf(flt_CODE_bss_80079A00) * 0.0f;
        pos->f[1] = setupPad->pos.f[1] + flt_CODE_bss_80079A10 + flt_CODE_bss_80079A0C;
        pos->f[2] = setupPad->pos.f[2] + (cosf(flt_CODE_bss_80079A00) * flt_CODE_bss_80079A08) + sinf(flt_CODE_bss_80079A00) * 0.0f;

        flt_CODE_bss_80079A00 += flt_CODE_bss_80079A04 * g_GlobalTimerDelta;

        while (flt_CODE_bss_80079A00 >= M_TAU_F)
        {
            flt_CODE_bss_80079A00 -= M_TAU_F;
        }

        while (flt_CODE_bss_80079A00 < 0.0f)
        {
            flt_CODE_bss_80079A00 += M_TAU_F;
        }
    }
}


//begin bondmove.c per pd

void sub_GAME_7F07C540(s32 arg0)
{
    g_CurrentPlayer->field_42c = arg0;
}


void currentPlayerSetLookAheadSetting(bool enabled)
{
    g_CurrentPlayer->automovecentreenabled = enabled;
}


/**
 * Unreferenced
 */
bool currentPlayerGetLookAheadSetting(void)
{
    return g_CurrentPlayer->automovecentreenabled;
}


void currentPlayerSetYAutoAimEnabled(bool enabled)
{
  g_CurrentPlayer->autoyaimenabled = enabled;
}


/**
 * Address 0x7F07C580.
 */
bool currentPlayerGetYAutoAimEnabled(void)
{
#ifndef TARGET_N64
    extern s32 portMouseBlocksAutoAim(s32 player);   /* video.c: no Auto-Aim on the mouse unless Port Options says so */

    if (portMouseBlocksAutoAim(get_cur_playernum()))
    {
        return FALSE;
    }
#endif
    if (getPlayerCount() == 1)
    {
        return g_CurrentPlayer->autoyaimenabled;
    }

    return (bool) g_playerPerm->autoaim;
}


bool currentPlayerGetYAutoAimEnabledRedirect(void)
{
    return currentPlayerGetYAutoAimEnabled();
}


bool currentPlayerGetIsAiming(void)
{
  return g_CurrentPlayer->insightaimmode;
}


/**
 * Updates autoyaimtime60 by g_ClockTimer.
 * Will update player->autoaimy if new autoyaimtime60 < 0 or autoaim_target != g_CurrentPlayer->autoaim_target_y.
 *
 * Address 0x7F07C5F0.
 */
void bondviewUpdateYAutoAimTime(struct PropRecord *autoaim_target, f32 auto_aim_y)
{
    if (g_CurrentPlayer->autoyaimtime60 >= 0)
    {
        g_CurrentPlayer->autoyaimtime60 = g_CurrentPlayer->autoyaimtime60 - g_ClockTimer;
    }

    if (autoaim_target != g_CurrentPlayer->autoaim_target_y)
    {
        if (g_CurrentPlayer->autoyaimtime60 < 0)
        {
            g_CurrentPlayer->autoyaimtime60 = BONDVIEW_AUTOAIM_TIME;
            g_CurrentPlayer->autoaim_target_y = autoaim_target;
        }
        else
        {
            return;
        }
    }

    g_CurrentPlayer->autoaimy = auto_aim_y;
}


void currentPlayerSetXAutoAimEnabled(bool enabled)
{
  g_CurrentPlayer->autoxaimenabled = enabled;

  return;
}


/**
 * Address 0x7F07C668.
 */
bool currentPlayerGetXAutoAimEnabled(void)
{
#ifndef TARGET_N64
    extern s32 portMouseBlocksAutoAim(s32 player);   /* video.c */

    if (portMouseBlocksAutoAim(get_cur_playernum()))
    {
        return FALSE;
    }
#endif
    if (getPlayerCount() == 1)
    {
        return g_CurrentPlayer->autoxaimenabled;
    }

    return (bool) g_playerPerm->autoaim;
}


bool currentPlayerGetXAutoAimEnabledRedirect(void)
{
    return currentPlayerGetXAutoAimEnabled();
}


/**
 * Updates autoxaimtime60 by g_ClockTimer.
 * Will update player->autoaimx if new autoxaimtime60 < 0 or autoaim_target_x != g_CurrentPlayer->autoaim_target_x.
 *
 * Address 0x7F07C6C8.
 */
void bondviewUpdateXAutoAimTime(struct PropRecord *autoaim_target, f32 auto_aim_x)
{
    if (g_CurrentPlayer->autoxaimtime60 >= 0)
    {
        g_CurrentPlayer->autoxaimtime60 = g_CurrentPlayer->autoxaimtime60 - g_ClockTimer;
    }

    if (autoaim_target != g_CurrentPlayer->autoaim_target_x)
    {
        if (g_CurrentPlayer->autoxaimtime60 < 0)
        {
            g_CurrentPlayer->autoxaimtime60 = BONDVIEW_AUTOAIM_TIME;
            g_CurrentPlayer->autoaim_target_x = autoaim_target;
        }
        else
        {
            return;
        }
    }

    g_CurrentPlayer->autoaimx = auto_aim_x;

}


void change_player_pos_to_target(struct collision434 *col, coord3d *pos, StandTile *stan)
{
    f32 store_x;
    f32 store_x2;
    f32 store_z;
    f32 store_y;
    col->collision_position.x = pos->x;
    store_x = col->collision_position.x;
    col->collision_position.y = pos->y;
    store_y = col->collision_position.y;
    col->collision_position.z = pos->z;
    store_z = col->collision_position.z;
    store_x2 = pos->x;
    col->current_tile_ptr = stan;
    col->current_tile_ptr_for_portals = stan;
    col->applied_view.y = 0.0f;
    col->applied_view.z = 0.0f;
    col->applied_view2.x = 0.0f;
    col->applied_view2.z = store_x2 * 0.0f;
    col->theta_transform.x = 0.0f;
    col->theta_transform.y = 0.0f;
    col->pos.x = store_x;
    col->pos3.x = store_x;
    col->applied_view.x = 1.0f;
    col->applied_view2.y = 1.0f;
    col->theta_transform.z = 1.0f;
    col->pos.y = store_y;
    col->pos3.y = store_y;
    col->pos.z = store_z;
    col->pos3.z = store_z;
    col->collision_radius = 30;
}


/**
 * US address 7F07C7B4.
*/
void bondviewTankModelRotationRelated(void) {
    struct ObjectRecord *obj;
    struct coord3d *sp68;
    struct coord3d *sp64;
    Mtxf sp24;
    ModelNode **temp_v0;

    if (g_PlayerTankProp != NULL)
    {
        obj = g_PlayerTankProp->obj;

        /// TODO: Fix Model struct Data type.
        temp_v0 = obj->model->obj->Switches;
        sp68 = (struct coord3d *)temp_v0[2]->Data;
        sp64 = (struct coord3d *)temp_v0[1]->Data;
        matrix_4x4_set_rotation_around_y(M_TAU_F - g_TankTurretOrientationAngleRad, &sp24);
        g_TankModelPositionOffset.f[0] = sp68->f[0];
        g_TankModelPositionOffset.f[1] = sp68->f[1];
        g_TankModelPositionOffset.f[2] = sp68->f[2];
        mtx4RotateVecInPlace(&sp24, g_TankModelPositionOffset.f);
        g_TankModelPositionOffset.f[0] += sp64->f[0];
        g_TankModelPositionOffset.f[1] += sp64->f[1];
        g_TankModelPositionOffset.f[2] += sp64->f[2];
    }
}


/**
 * Address 0x7F07C888.
*/
void bondviewGetTankCollisionBounds(struct rect4f *tank_collision_bounds, struct coord3d *collision_position, f32 tank_orientation_angle)
{
    ObjectRecord *sp4C;
    f32 sp48;
    f32 sp44;
    f32 sp40;
    f32 sp3C;
    f32 sp38;
    f32 sp34;
    struct ModelRoData_BoundingBoxRecord *bbox;

    #ifdef DEBUG
    assert(bondonprop2); // canonically g_PlayerTankProp is bondonprop2 - presumably because it was also motorbike code
#endif

    sp4C = g_PlayerTankProp->obj;

    bbox = chrobjGetBboxFromObjectRecord(sp4C);

    sp44 = bbox->Bounds.xmin - g_TankModelPositionOffset.f[0];
    sp40 = bbox->Bounds.xmax - g_TankModelPositionOffset.f[0];

    sp3C = bbox->Bounds.zmin - g_TankModelPositionOffset.f[2];
    sp38 = bbox->Bounds.zmax - g_TankModelPositionOffset.f[2];

    sp34 = sp4C->model->scale * cosf(tank_orientation_angle);
    sp48 = sp4C->model->scale * sinf(tank_orientation_angle);

    tank_collision_bounds->points[0].f[0] = collision_position->f[0] + (-sp3C * sp48) + (sp44 * sp34);
    tank_collision_bounds->points[0].f[1] = collision_position->f[2] + (sp3C * sp34) + (sp44 * sp48);

    tank_collision_bounds->points[1].f[0] = collision_position->f[0] + (-sp3C * sp48) + (sp40 * sp34);
    tank_collision_bounds->points[1].f[1] = collision_position->f[2] + (sp3C * sp34) + (sp40 * sp48);

    tank_collision_bounds->points[2].f[0] = collision_position->f[0] + (-sp38 * sp48) + (sp40 * sp34);
    tank_collision_bounds->points[2].f[1] = collision_position->f[2] + (sp38 * sp34) + (sp40 * sp48);

    tank_collision_bounds->points[3].f[0] = collision_position->f[0] + (-sp38 * sp48) + (sp44 * sp34);
    tank_collision_bounds->points[3].f[1] = collision_position->f[2] + (sp38 * sp34) + (sp44 * sp48);
}


/**
 * Address 0x7F07CA2C.
*/
s32 bondviewTestLineUnobstructed(StandTile **pTile, f32 p_x, f32 p_z, f32 dest_x, f32 dest_z, s32 cdtypes, struct coord3d *coord_p, struct coord3d *coord_dest)
{
    s32 temp_v0;

    temp_v0 = stanTestLineUnobstructed(pTile, p_x, p_z, dest_x, dest_z, cdtypes, 0.0f, 1.0f, 0.0f, 1.0f);
    if ((temp_v0 == 0) && (coord_p != NULL))
    {
        coord_p->f[0] = p_x;
        coord_p->f[1] = 0.0f;
        coord_p->f[2] = p_z;
        coord_dest->f[0] = dest_x;
        coord_dest->f[1] = 0.0f;
        coord_dest->f[2] = dest_z;
    }

    return temp_v0;
}


/**
 * Address 0x7F07CAC8.
*/
s32 bondviewTankCollisionStatus(struct coord3d *collision_position, StandTile *arg1, f32 tank_orientation_angle, struct coord3d *arg3, struct coord3d *arg4)
{
    StandTile *spBC;
    s32 stack_padding;
    struct rect4f tank_collision_bounds;
    s32 sp94;
    f32 temp_f0;

    Model *sp8C;

    ModelNode **switches;
    struct coord3d *temp_a1;
    struct coord3d *temp_a2;
    struct coord3d sp74;
    Mtxf sp34;
    struct coord3d *temp_v1;

    spBC = arg1;
    sp94 = 0;

    bondviewGetTankCollisionBounds(&tank_collision_bounds, collision_position, tank_orientation_angle);

    if (g_PlayerTankProp != NULL)
    {
        sub_GAME_7F03D058(g_PlayerTankProp, 0);
    }

    if ((bondviewTestLineUnobstructed(&spBC, collision_position->f[0], collision_position->f[2], tank_collision_bounds.points[0].f[0], tank_collision_bounds.points[0].f[1], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_OBJSIMMUNETOEXPLOSIONS, arg3, arg4) != 0)
        && (bondviewTestLineUnobstructed(&spBC, tank_collision_bounds.points[0].f[0], tank_collision_bounds.points[0].f[1], tank_collision_bounds.points[1].f[0], tank_collision_bounds.points[1].f[1], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_OBJSIMMUNETOEXPLOSIONS, arg3, arg4) != 0)
        && (bondviewTestLineUnobstructed(&spBC, tank_collision_bounds.points[1].f[0], tank_collision_bounds.points[1].f[1], tank_collision_bounds.points[2].f[0], tank_collision_bounds.points[2].f[1], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_OBJSIMMUNETOEXPLOSIONS, arg3, arg4) != 0)
        && (bondviewTestLineUnobstructed(&spBC, tank_collision_bounds.points[2].f[0], tank_collision_bounds.points[2].f[1], tank_collision_bounds.points[3].f[0], tank_collision_bounds.points[3].f[1], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_OBJSIMMUNETOEXPLOSIONS, arg3, arg4) != 0)
        && (bondviewTestLineUnobstructed(&spBC, tank_collision_bounds.points[3].f[0], tank_collision_bounds.points[3].f[1], tank_collision_bounds.points[0].f[0], tank_collision_bounds.points[0].f[1], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_OBJSIMMUNETOEXPLOSIONS, arg3, arg4) != 0))
    {
        sp94 = 1;

        if (g_PlayerTankProp != NULL)
        {
            ObjectRecord *obj = g_PlayerTankProp->obj;
            sp8C = obj->model;
            switches = sp8C->obj->Switches;

            temp_v1 = switches[3]->Data;
            temp_a1 = switches[4]->Data;
            temp_a2 = switches[2]->Data;

            sp74.f[0] = temp_a1->f[0] + temp_v1->f[0] - temp_a2->f[0];
            sp74.f[1] = 0.0f;
            sp74.f[2] = temp_a1->f[2] + temp_v1->f[2] - temp_a2->f[2];

            temp_f0 = tank_orientation_angle + g_TankTurretOrientationAngleRad;

            if (temp_f0 >= M_TAU_F)
            {
                temp_f0 -= M_TAU_F;
            }

            if (temp_f0 < 0.0f)
            {
                temp_f0 += M_TAU_F;
            }

            matrix_4x4_set_rotation_around_y(M_TAU_F - temp_f0, &sp34);
            mtx4RotateVecInPlace(&sp34, (f32*)&sp74);

            sp74.f[0] *= sp8C->scale;
            sp74.f[2] *= sp8C->scale;

            sp74.f[0] += collision_position->f[0];
            sp74.f[2] += collision_position->f[2];

            spBC = arg1;

            if (bondviewTestLineUnobstructed(&spBC, collision_position->f[0], collision_position->f[2], sp74.f[0], sp74.f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_OBJSIMMUNETOEXPLOSIONS, arg3, arg4) == 0)
            {
                sp94 = 0;
            }

        }
    }

    if (g_PlayerTankProp != NULL)
    {
        sub_GAME_7F03D058(g_PlayerTankProp, 1);
    }

    return sp94;
}




/**
 * Address 0x7F07CDA8.
*/
s32 bondviewCallTankCollisionStatus(struct coord3d *collision_position, StandTile *arg1, f32 tank_orientation_angle)
{
    return bondviewTankCollisionStatus(collision_position, arg1, tank_orientation_angle, NULL, NULL);
}





/**
 * Address 0x7F07CDD4.
*/
s32 sub_GAME_7F07CDD4(struct coord3d *arg0, f32 arg1, StandTile **arg2)
{
    StandTile *sp3C;
    s32 unused_padding[2];

    sp3C = g_CurrentPlayer->field_488.current_tile_ptr;

    if ((
        stanTestLineUnobstructed(
            &sp3C,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2],
            arg0->f[0],
            arg0->f[2],
            0,
            0.0f,
            1.0f,
            0.0f,
            1.0f) != 0)
        && (bondviewCallTankCollisionStatus(arg0, sp3C, arg1) != 0))
    {
        *arg2 = sp3C;
        return 1;
    }

    return 0;
}





bool isBondInTank(void)
{
    return g_PlayerIsInTank;
}






struct PropRecord *get_ptr_for_players_tank(void)
{
    if (g_PlayerIsInTank == 1)
    {
        return g_PlayerTankProp;
    }

    return 0;
}





/**
 * Sets paraameter position based on global variables g_TankOrientationAngle, g_TankTurretOrientationAngleRad, g_TankTurretVerticalAngle.
 *
 * Address 0x7F07CEB0.
 */
void bondviewSet3dCoord7F07CEB0(coord3d *arg0)
{
    f32 f;

    f = g_TankOrientationAngle + g_TankTurretOrientationAngleRad;

    if (f >= M_TAU_F)
    {
        f = f - M_TAU_F;
    }

    if (f < 0.0f)
    {
        f = f + M_TAU_F;
    }

    arg0->f[0] = -sinf(f) * cosf(g_TankTurretVerticalAngle);
    arg0->f[1] = sinf(g_TankTurretVerticalAngle);
    arg0->f[2] = cosf(f) * cosf(g_TankTurretVerticalAngle);
}






/**
 * Unreferenced.
 *
 * Returns global variable g_TankTurretVerticalAngle, which is in radians.
 *
 * Address 0x7F07CF80.
 */
f32 bondviewGet8003646CRad(void)
{
    return g_TankTurretVerticalAngle;
}



/**
 * Address 0x7F07CF8C.
*/
s32 bondviewTryMoveToStan(struct coord3d *arg0, StandTile **stan)
{
    s32 sp94;
    StandTile *sp90;
    s32 cdtypes;
    f32 height;
    f32 always_30;
    f32 collision_radius;
    s32 sp7C;
    struct TankRecord *tank;
    s32 stack_padding[11];
    struct StandTileLocusCallbackRecord sp3C;

    sp94 = 0;

    if ((g_PlayerIsInTank == 1) && (g_EnterTankAudioState != TANK_RUN_STATE_NOT_RUNNING))
    {
        sp94 = sub_GAME_7F07CDD4(arg0, g_TankOrientationAngle, stan);
    }
    else
    {
        sp90 = g_CurrentPlayer->field_488.current_tile_ptr;

#ifndef TARGET_N64
        if (sp90 == NULL)
        { /* debug (GE_DUMP_BOND): who lost Bond's tile - distinguish
           * "prop->stan died" from "current_tile_ptr cleared with prop->stan
           * intact" */
            extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
            static s32 once;
            if (once < 4)
            {
                f32 v[6];
                once++;
                v[0] = (f32) (u32) g_CurrentPlayer;
                v[1] = (f32) (u32) g_CurrentPlayer->prop;
                v[2] = (f32) (u32) (g_CurrentPlayer->prop ? g_CurrentPlayer->prop->stan : (void *) 0xdead);
                v[3] = (f32) g_CurrentPlayer->cameramode;
                v[4] = g_CurrentPlayer->field_488.collision_position.f[0];
                v[5] = g_CurrentPlayer->field_488.collision_position.f[2];
                ge_dbg_vec("nulltile plr/prop/propstan/cam/x/z", v, 6);
            }
        }
#endif

        if (obj_collision_flag)
        {
            cdtypes = CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER;
        }
        else
        {
            cdtypes = 0;
        }

        bondviewGetCollisionRadius(g_CurrentPlayer->prop, &collision_radius, &height, &always_30);

        if (g_WorldTankProp != NULL)
        {
            sub_GAME_7F03D058(g_WorldTankProp, 0);
        }

        sub_GAME_7F03D058(g_CurrentPlayer->prop, 0);
        sp7C = stanTileDistanceRelated(&sp90, arg0->f[0], arg0->f[2], collision_radius, &sp3C);

        if (stanGetLocusField0(&sp3C) != 0)
        {
            g_CurrentPlayer->autocrouchpos = CROUCH_SQUAT;
        }

        if ((stanTestLineUnobstructed(
                &sp90,
                g_CurrentPlayer->field_488.collision_position.f[0],
                g_CurrentPlayer->field_488.collision_position.f[2],
                arg0->f[0],
                arg0->f[2],
                cdtypes,
                height,
                always_30,
                0.0f,
                1.0f) != 0)
            && stanTestVolume(&sp90, arg0->f[0], arg0->f[2], collision_radius, cdtypes, height, always_30) < 0)
        {
            if (g_CurrentPlayer->ducking_height_offset == FULL_CROUCH_OFFSET || sp7C < 0)
            {
                if (stanGetLocusCount(&sp3C) == 0 && stanTestLocusEdgeAboveY(&sp90, arg0->f[0], arg0->f[2], collision_radius, g_CurrentPlayer->field_488.collision_position.f[1] + 175.0f) >= 0)
                {
                    goto block_20;
                }
                else
                {
                    *stan = sp90;
                    sp94 = 1;
                }
            }
            else
            {
                goto block_20;
            }
        }
        else
        {
block_20:
            /* I'm sorry, this is the only way I could make it match. */
            if (g_PlayerTankProp == NULL
                && (stanSavedColl_posData != NULL)
                && (stanSavedColl_posData->type == PROP_TYPE_OBJ))
            {
                tank = (struct TankRecord *)stanSavedColl_posData->obj;
                if (tank->type == PROPDEF_TANK)
                {
                    g_WorldTankProp = stanSavedColl_posData;
                }
            }
        }

        sub_GAME_7F03D058(g_CurrentPlayer->prop, 1);

        if (g_WorldTankProp != NULL)
        {
            sub_GAME_7F03D058(g_WorldTankProp, 1);
        }
    }

    return sp94;
}



/**
 * Calculates collision with current player.
 *
 * @param next_pos: 3d coordinate to attempt to move to.
 * @param collision_pt0: Out parameter. Will set {x,0,z} position of first point (from line edge) if Bond is in collision, otherwise {0}.
 * @param collision_pt1: Out parameter. Will set {x,0,z} position of second point (from line edge) if Bond is in collision, otherwise ... Bond's look angle?
 *
 * @return 1 if able to update stan and collision position, zero otherwise.
 *
 * Address 0x7F07D234.
 */
s32 bondviewTrySimpleMovePlayerCollision(coord3d *next_pos, coord3d *collision_pt0, coord3d *collision_pt1)
{
    struct StandTile *stan;

    // resets stan global collision variables
    stanResetHits();

    if (bondviewTryMoveToStan(next_pos, &stan) != 0)
    {
        g_CurrentPlayer->field_488.current_tile_ptr = stan;
        g_CurrentPlayer->field_488.collision_position.f[0] = next_pos->f[0];
        g_CurrentPlayer->field_488.collision_position.f[2] = next_pos->f[2];

        return 1;
    }

    getCollisionEdge_maybe(collision_pt0, collision_pt1);

    return 0;
}


/**
 * This is a fallback method used when bondviewTrySimpleMovePlayerCollision fails.
 * Instead of moving to the full coordinate specified by `next_pos`, it will
 * scale the position using `calculateRayToSegmentIntersectionNormalized` and try to move to that position.
 *
 * @param next_pos: 3d coordinate to attempt to move to.
 * @param collision1_pt0: Prior collision point 0.
 * @param collision1_pt1: Prior collision point 1.
 * @param collision2_pt0: Out parameter. Will set {x,0,z} position of first point (from line edge) if Bond is in collision, otherwise {0}.
 * @param collision2_pt1: Out parameter. Will set {x,0,z} position of second point (from line edge) if Bond is in collision, otherwise ... Bond's look angle?
 *
 * @return 1 if able to update stan and collision position, zero if still unable to move by failing on the same collision edge, -1 otherwise (still unable to move).
 *
 * US address 7F07D2B4.
 * Perfect Dark, see bondwalk.c bwalk0f0c47d0, bondbike.c bbike0f0d36d4.
*/
s32 bondviewTryFractionMovePlayerCollision(
    struct coord3d *next_pos,
    struct coord3d *collision1_pt0,
    struct coord3d *collision1_pt1,
    struct coord3d *collision2_pt0,
    struct coord3d *collision2_pt1)
{
    StandTile *stan;
    f32 height;
    f32 always_30;
    struct coord3d try_next_pos;
    struct coord3d delta_pos;
    struct coord3d sp50;
    struct coord2d sp48;
    struct coord2d sp40;
    struct coord2d sp38;
    f32 temp_f0;
    f32 collision_radius;

    bondviewGetCollisionRadius(g_CurrentPlayer->prop, &collision_radius, &height, &always_30);

    delta_pos.f[0] = next_pos->f[0] - g_CurrentPlayer->field_488.collision_position.f[0];
    delta_pos.f[2] = next_pos->f[2] - g_CurrentPlayer->field_488.collision_position.f[2];

    sp50.f[0] = collision_radius;
    sp50.f[1] = g_CurrentPlayer->field_488.collision_position.f[0];
    sp50.f[2] = g_CurrentPlayer->field_488.collision_position.f[2];

    sp48.f[0] = collision1_pt0->f[0];
    sp48.f[1] = collision1_pt0->f[2];

    sp40.f[0] = collision1_pt1->f[0];
    sp40.f[1] = collision1_pt1->f[2];

    sp38.f[0] = delta_pos.f[0];
    sp38.f[1] = delta_pos.f[2];

    temp_f0 = calculateRayToSegmentIntersectionNormalized(&sp50, &sp48, &sp40, &sp38);

    try_next_pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0] + (delta_pos.f[0] * temp_f0 * 0.25f);
    try_next_pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2] + (delta_pos.f[2] * temp_f0 * 0.25f);

    if (bondviewTryMoveToStan(&try_next_pos, &stan) != 0)
    {
        g_CurrentPlayer->field_488.current_tile_ptr = stan;
        g_CurrentPlayer->field_488.collision_position.f[0] = try_next_pos.f[0];
        g_CurrentPlayer->field_488.collision_position.f[2] = try_next_pos.f[2];

        return 1;
    }

    getCollisionEdge_maybe(collision2_pt0, collision2_pt1);

    if (collision2_pt0->f[0] != collision1_pt0->f[0]
        || collision2_pt0->f[1] != collision1_pt0->f[1]
        || collision2_pt0->f[2] != collision1_pt0->f[2]
        || collision2_pt1->f[0] != collision1_pt1->f[0]
        || collision2_pt1->f[1] != collision1_pt1->f[1]
        || collision2_pt1->f[2] != collision1_pt1->f[2])
    {
        return 0;
    }

    return -1;
}




/**
 * This is a fallback method used when bondviewTrySimpleMovePlayerCollision fails.
 * Instead of moving to the full coordinate specified by `next_pos`, it will
 * attempt to move along the collision edge.
 *
 * @param prior_next_pos: Prior 3d coordinate that Bond failed to move to.
 * @param collision1_pt0: Prior collision point 0.
 * @param collision1_pt1: Prior collision point 1.
 *
 * @return -1 if either x or z coordinates are the same for the collision points, 1 if able to update stan and collision position, zero otherwise.
 *
 * US address 7F07D4C0.
 */
s32 bondviewTryEdgeMovePlayerCollision(struct coord3d *prior_next_pos, struct coord3d *collision_pt0, struct coord3d *collision_pt1)
{
    struct coord3d delta_pos;
    f32 tempf;
    struct coord3d norm_collision_edge;
    struct coord3d try_next_pos;
    StandTile *stan;

    delta_pos.f[0] = prior_next_pos->f[0] - g_CurrentPlayer->field_488.collision_position.f[0];
    delta_pos.f[2] = prior_next_pos->f[2] - g_CurrentPlayer->field_488.collision_position.f[2];

    if (collision_pt0->f[0] != collision_pt1->f[0] || collision_pt0->f[2] != collision_pt1->f[2])
    {
        norm_collision_edge.f[0] = collision_pt1->f[0] - collision_pt0->f[0];
        norm_collision_edge.f[2] = collision_pt1->f[2] - collision_pt0->f[2];

        tempf = (norm_collision_edge.f[0] * norm_collision_edge.f[0]) + (norm_collision_edge.f[2] * norm_collision_edge.f[2]);
        tempf =  1.0f / sqrtf(tempf);
        norm_collision_edge.f[0] *= tempf;
        norm_collision_edge.f[2] *= tempf;

        /**
         * Normalizing gives you the direction vector of the wall, so the dot product in the assignment to
         * tempf gives you the distance moved along the direction of the wall.
         * Then try_next_pos is simply the point of the collision with the wall, plus the
         * length moved along the wall times the direction vector of the wall.
         **/
        tempf = (delta_pos.f[0] * norm_collision_edge.f[0]) + (delta_pos.f[2] * norm_collision_edge.f[2]);
        try_next_pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0] + (tempf * norm_collision_edge.f[0]);
        try_next_pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2] + (tempf * norm_collision_edge.f[2]);

        if (bondviewTryMoveToStan(&try_next_pos, &stan))
        {
            g_CurrentPlayer->field_488.current_tile_ptr = stan;
            g_CurrentPlayer->field_488.collision_position.f[0] = try_next_pos.f[0];
            g_CurrentPlayer->field_488.collision_position.f[2] = try_next_pos.f[2];

            return 1;
        }

        return 0;
    }

    return -1;
}



/**
 * This is a fallback method used when bondviewTrySimpleMovePlayerCollision fails.
 * If Bond previously failed to move because of a collision, this will check
 * if Bond is near the end point of the collision edge. If within the collision
 * radius of Bond to the edge endpoint, then allow movement.
 *
 * @param prior_next_pos: Prior 3d coordinate that Bond failed to move to.
 * @param collision1_pt0: Prior collision point 0.
 * @param collision1_pt1: Prior collision point 1.
 *
 * @return 1 if able to update stan and collision position, zero otherwise.
 *
 * US address 7F07D61C.
 *
 * Perfect Dark bwalk0f0c4a5c.
*/
s32 bondviewTryEndHopPlayerCollision(struct coord3d *prior_next_pos, struct coord3d *collision_pt0, struct coord3d *collision_pt1)
{
    struct coord3d delta_pos;
    struct coord3d sp50;
    struct coord3d try_next_pos;
    f32 height;
    f32 always_30;
    f32 tempf;
    StandTile *stan;
    f32 collision_radius;

    bondviewGetCollisionRadius(g_CurrentPlayer->prop, &collision_radius, &height, &always_30);

    delta_pos.f[0] = prior_next_pos->f[0] - g_CurrentPlayer->field_488.collision_position.f[0];
    delta_pos.f[2] = prior_next_pos->f[2] - g_CurrentPlayer->field_488.collision_position.f[2];

    sp50.f[0] = collision_pt0->f[0] - prior_next_pos->f[0];
    sp50.f[2] = collision_pt0->f[2] - prior_next_pos->f[2];

    if (((sp50.f[0] * sp50.f[0]) + (sp50.f[2] * sp50.f[2])) <= (collision_radius * collision_radius))
    {
        if (collision_pt0->f[0] != g_CurrentPlayer->field_488.collision_position.f[0] || collision_pt0->f[2] != g_CurrentPlayer->field_488.collision_position.f[2])
        {
            sp50.f[0] = -(collision_pt0->f[2] - g_CurrentPlayer->field_488.collision_position.f[2]);
            sp50.f[2] = collision_pt0->f[0] - g_CurrentPlayer->field_488.collision_position.f[0];

            tempf = (sp50.f[0] * sp50.f[0]) + (sp50.f[2] * sp50.f[2]);
            tempf =  1.0f / sqrtf(tempf);
            sp50.f[0] *= tempf;
            sp50.f[2] *= tempf;

            tempf = (delta_pos.f[0] * sp50.f[0]) + (delta_pos.f[2] * sp50.f[2]);
            sp50.f[0] *= tempf;
            sp50.f[2] *= tempf;
            try_next_pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0] + (sp50.f[0]);
            try_next_pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2] + (sp50.f[2]);

            if (bondviewTryMoveToStan(&try_next_pos, &stan))
            {
                g_CurrentPlayer->field_488.current_tile_ptr = stan;
                g_CurrentPlayer->field_488.collision_position.f[0] = try_next_pos.f[0];
                g_CurrentPlayer->field_488.collision_position.f[2] = try_next_pos.f[2];

                return 1;
            }
        }
    }
    else
    {
        sp50.f[0] = collision_pt1->f[0] - prior_next_pos->f[0];
        sp50.f[2] = collision_pt1->f[2] - prior_next_pos->f[2];

        if (((sp50.f[0] * sp50.f[0]) + (sp50.f[2] * sp50.f[2])) <= (collision_radius * collision_radius))
        {
            if (collision_pt1->f[0] != g_CurrentPlayer->field_488.collision_position.f[0] || collision_pt1->f[2] != g_CurrentPlayer->field_488.collision_position.f[2])
            {
                sp50.f[0] = -(collision_pt1->f[2] - g_CurrentPlayer->field_488.collision_position.f[2]);
                sp50.f[2] = collision_pt1->f[0] - g_CurrentPlayer->field_488.collision_position.f[0];

                tempf = (sp50.f[0] * sp50.f[0]) + (sp50.f[2] * sp50.f[2]);
                tempf =  1.0f / sqrtf(tempf);
                sp50.f[0] *= tempf;
                sp50.f[2] *= tempf;

                tempf = (delta_pos.f[0] * sp50.f[0]) + (delta_pos.f[2] * sp50.f[2]);
                sp50.f[0] *= tempf;
                sp50.f[2] *= tempf;
                try_next_pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0] + (sp50.f[0]);
                try_next_pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2] + (sp50.f[2]);

                if (bondviewTryMoveToStan(&try_next_pos, &stan))
                {
                    g_CurrentPlayer->field_488.current_tile_ptr = stan;
                    g_CurrentPlayer->field_488.collision_position.f[0] = try_next_pos.f[0];
                    g_CurrentPlayer->field_488.collision_position.f[2] = try_next_pos.f[2];

                    return 1;
                }
            }
        }
    }

    return 0;
}


/**
 * Unreferenced.
 *
 * Bitwise convert 32bit int to float.
 *
 * Address 0x7F07D954.
 */
f32 bondviewBitconvertIntToFloat(s32 arg0)
{
    return *(f32*)&arg0;
}




struct dummy_struct {
    s32 unk00;
    s32 unk04;
};

/**
 * Sets Bond bondprevpos, attempts to move by `offset`.
 *
 * @param offset: Attempt to move Bond by {x,0,z} amount.
 * @param allow_scoot: If movement causes collision, when set will allow Bond to scoot along the collision edge and to bump around corner edges. Otherwise, any collision will stop movement.
 *
 * US address 7F07D960.
 * JP address 7F07DA34 (maybe).
 */
#ifndef TARGET_N64
/* MP out-of-bounds telemetry: last known-good footing per player */
StandTile *portLastGoodTile[4];
coord3d portLastGoodPos[4];
#endif

#ifndef TARGET_N64
/* GE_BLOCKDBG=1: when Bond's plain move is rejected, print what rejected it
 * (a prop, or a stan edge = level geometry), rate-limited per player, along
 * with how far he moved so a true wedge stands apart from wall-sliding. */
#ifndef TARGET_N64
static s32 g_portBlockReported;
static void portBlockDbgOutcome(s32 allow_scoot, s32 frac)
{
    if (!g_portBlockReported) { return; }
    g_portBlockReported = 0;
    fprintf(stderr, "ge:   escape allow_scoot=%d frac=%d -> pos=(%.1f %.1f)\n",
            allow_scoot, frac,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2]);
    fflush(stderr);
}
#endif

static void portBlockDbgReport(struct coord3d *want, struct coord3d *edge0, struct coord3d *edge1)
{
    extern struct StandTile *stanSavedColl_tile;
    extern PropRecord *g_portBlockProp;
    extern s32 g_portBlockSrc;
    extern s32 g_portBlockTick;
    extern f32 g_portBlockTop;
    extern f32 g_portBlockTop, g_portBlockBot, g_portBlockLo, g_portBlockHi;
    extern s32 g_portBlockGate;
    extern f32 g_portBlockFloor;
    extern void *g_portBlockTileUsed;
    extern f32 g_portBlockTX, g_portBlockTZ;
    extern s32 g_portBlockRooms[24]; extern s32 g_portBlockRoomCount;
    extern struct ItemModelFileRecord PitemZ_entries[];
    static s32 armed = -1;
    static u32 lastreport[4];
    static struct coord3d lastpos[4];
    struct coord3d *bp;
    PropRecord *blocker;
    s32 pn;
    f32 dmove;

    if (armed < 0)
    {
        armed = (getenv("GE_BLOCKDBG") != NULL);
    }
    if (!armed)
    {
        return;
    }

    pn = get_cur_playernum() & 3;
    if ((u32) g_GlobalTimer - lastreport[pn] < 20u)
    {
        return;
    }

    bp = &g_CurrentPlayer->field_488.collision_position;
    dmove = sqrtf((bp->f[0] - lastpos[pn].f[0]) * (bp->f[0] - lastpos[pn].f[0])
                + (bp->f[2] - lastpos[pn].f[2]) * (bp->f[2] - lastpos[pn].f[2]));
    lastreport[pn] = (u32) g_GlobalTimer;
    lastpos[pn] = *bp;

    /* only trust the saved prop if THIS tick recorded it - see the
     * provenance note in stan.c; a stale pointer here reads as a
     * pane from another storey and sends the hunt the wrong way. */
    blocker = (g_portBlockTick == (s32) g_GlobalTimer) ? g_portBlockProp : NULL;
    g_portBlockReported = 1;
    if (blocker != NULL && blocker != stanSavedColl_posData)
    {
        fprintf(stderr, "ge:   note: savedColl disagrees with fresh hit\n");
    }

    fprintf(stderr, "ge: block p%d t=%d bond=(%.0f %.0f %.0f) want=(%.0f %.0f) dmove=%.1f | ",
            pn, (s32) g_GlobalTimer, bp->f[0], bp->f[1], bp->f[2],
            want->f[0], want->f[2], dmove);

    if (blocker != NULL)
    {
        ObjectRecord *obj = blocker->obj;
        const char *name = "?";
        s32 propdef = -1;
        s32 model = -1;

        if (blocker->type == PROP_TYPE_OBJ || blocker->type == PROP_TYPE_DOOR
            || blocker->type == PROP_TYPE_WEAPON)
        {
            if (obj != NULL)
            {
                propdef = obj->type;
                model = obj->obj;
                if (model >= 0 && model < 340 && PitemZ_entries[model].filename != NULL)
                {
                    name = PitemZ_entries[model].filename;
                }
                {   /* print the prop's collision bottom/top used by
                     * stanTestVolume's height gate, so a prop blocking from
                     * another storey shows up as a bounds problem */
                    struct rect4f *poly = NULL;
                    s32 nedge = 0;
                    f32 ctop = 0.0f, cbot = 0.0f;
                    f32 crad, cheight, cpad;

                    chraiGetCollisionBounds(blocker, &poly, &nedge, &ctop, &cbot);
                    bondviewGetCollisionRadius(g_CurrentPlayer->prop, &crad, &cheight, &cpad);

                    fprintf(stderr,
                            "PROP idx=%d proptype=%d propdef=%d model=%d(%s) pad=%d room=%d pos=(%.0f %.0f %.0f) flags=%08x state=%02x rt=%02x ybnd=[%.0f..%.0f] bondy=[%.0f..%.0f] edges=%d\n",
                            (s32) (blocker - g_Props), blocker->type, propdef, model, name,
                            obj->pad, (s32) blocker->rooms[0],
                            blocker->pos.f[0], blocker->pos.f[1], blocker->pos.f[2],
                            (u32) obj->flags, (u32) obj->state, (u32) obj->runtime_bitflags,
                            cbot, ctop, bp->f[1] + cpad, bp->f[1] + cheight, nedge);
                    {   /* the XZ footprint the move is tested against, plus
                     * Bond radius - a pane that reads as a fat box here is a
                     * bounds bug, a thin one means he is simply inside the
                     * radius and cannot depenetrate. */
                        s32 pi;
                        fprintf(stderr, "ge:   gate src=%d on=%d top=%.0f bot=%.0f lo=%.0f hi=%.0f\n",
                                g_portBlockSrc, g_portBlockGate, g_portBlockTop,
                                g_portBlockBot, g_portBlockLo, g_portBlockHi);
                        {   StandTile *bt = g_CurrentPlayer->field_488.current_tile_ptr;
                                StandTile *ut = (StandTile *) g_portBlockTileUsed;
                                fprintf(stderr, "ge:   floor used=%.0f fromtile=%06x room=%d | bondtile=%06x room=%d bondy=%.0f\n",
                                        g_portBlockFloor,
                                        ut ? (u32) ut->id : 0u, ut ? (s32) ut->room : -1,
                                        bt ? (u32) bt->id : 0u, bt ? (s32) bt->room : -1,
                                        bp->f[1]);
                                        fprintf(stderr, "ge:   tgt=(%.0f %.0f) inside_used=%d inside_bond=%d ybond=%.0f\n",
                                                g_portBlockTX, g_portBlockTZ,
                                                ut ? isPointInsideTriStandTileUnscaled_Maybe(ut, g_portBlockTX, g_portBlockTZ) : -1,
                                                bt ? isPointInsideTriStandTileUnscaled_Maybe(bt, g_portBlockTX, g_portBlockTZ) : -1,
                                                bt ? stanGetPositionYValue(bt, g_portBlockTX, g_portBlockTZ) : 0.0f);
                                                fprintf(stderr, "ge:   bondfloor=%.0f (own xz) eye=%.0f delta_used=%.0f\n",
                                                        bt ? stanGetPositionYValue(bt, bp->f[0], bp->f[2]) : 0.0f,
                                                        bp->f[1],
                                                        g_portBlockFloor - (bt ? stanGetPositionYValue(bt, bp->f[0], bp->f[2]) : 0.0f));
                                                        if (ut != NULL)
                                                        {   extern f32 level_scale;
                                                            s32 ci = STAN_MID_NIB(ut->tail.half, 8);
                                                            s32 di = STAN_MID_NIB(ut->tail.half, 4);
                                                            s32 ei = STAN_MID_NIB(ut->tail.half, 0);
                                                            fprintf(stderr, "ge:   tri c=%d d=%d e=%d P=(%d %d %d) Q=(%d %d %d) R=(%d %d %d) lscale=%.8f sx=%.3f sz=%.3f\n",
                                                                    ci, di, ei,
                                                                    ut->points[ci].x, ut->points[ci].y, ut->points[ci].z,
                                                                    ut->points[di].x, ut->points[di].y, ut->points[di].z,
                                                                    ut->points[ei].x, ut->points[ei].y, ut->points[ei].z,
                                                                    level_scale, g_portBlockTX * level_scale, g_portBlockTZ * level_scale);
                                                        }
                        }
                        {   s32 rr;
                            fprintf(stderr, "ge:   rooms(%d):", g_portBlockRoomCount);
                            for (rr = 0; rr < g_portBlockRoomCount; rr++) { fprintf(stderr, " %d", g_portBlockRooms[rr]); }
                            fprintf(stderr, "\n");
                        }
                        fprintf(stderr, "ge:   poly rad=%.1f", crad);
                        for (pi = 0; pi < nedge && pi < 8; pi++)
                        {
                            fprintf(stderr, " (%.0f %.0f)", poly->points[pi].f[0], poly->points[pi].f[1]);
                        }
                        fprintf(stderr, "\n");
                    }
                }
            }
            else
            {
                fprintf(stderr, "PROP idx=%d proptype=%d obj=NULL pos=(%.0f %.0f %.0f)\n",
                        (s32) (blocker - g_Props), blocker->type,
                        blocker->pos.f[0], blocker->pos.f[1], blocker->pos.f[2]);
            }
        }
        else
        {
            fprintf(stderr, "PROP idx=%d proptype=%d (chr/player) pos=(%.0f %.0f %.0f)\n",
                    (s32) (blocker - g_Props), blocker->type,
                    blocker->pos.f[0], blocker->pos.f[1], blocker->pos.f[2]);
        }
    }
    else if (stanSavedColl_tile != NULL)
    {
        fprintf(stderr, "TILE id=%06x room=%d edge=(%.0f %.0f)-(%.0f %.0f)\n",
                (u32) stanSavedColl_tile->id, (s32) stanSavedColl_tile->room,
                edge0->f[0], edge0->f[2], edge1->f[0], edge1->f[2]);
    }
    else
    {
        fprintf(stderr, "UNATTRIBUTED edge=(%.0f %.0f)-(%.0f %.0f)\n",
                edge0->f[0], edge0->f[2], edge1->f[0], edge1->f[2]);
    }

    fflush(stderr);
}
#endif


#ifndef TARGET_N64
/* PORT FIX - wedge escape.
 *
 * Landing is vertical and never tests props in XZ, so Bond can touch down
 * INSIDE a prop's collision volume (e.g. dropping from the upper Archives
 * window onto the steep exterior roof, beside the ground-floor pane). From
 * there every vanilla escape fails: the plain move is refused, the fraction
 * move returns -1, and the edge slide lands back on his own illegal position.
 *
 * The fix is a last-resort depenetration, NOT a change to any collision test:
 * only when Bond is stuck in a state normal play can't produce, step him out
 * along the blocking edge's outward normal to the nearest legal point. It never
 * runs during demo replay (so demo sync is untouched), and GE_UNWEDGE_OFF=1
 * disables it for A/B testing.
 */
/* A nudge, never a relocation: a spot that would need more than this stays
 * stuck rather than teleporting Bond through something. */
/* 40 units clears a catch right up against the pane (20 left some Archives
 * spots still illegal); the stillness, grounded and static-blocker gates keep
 * it from firing in the cases the old lower cap protected. */
#define PORT_UNWEDGE_MAX_PUSH 40.0f
/* Hold before acting, in 60Hz fields rather than ticks (ticks are slivers at
 * uncapped rates and caused wall jitter), and only while Bond is truly
 * stationary, so wall-sliding never arms it. A successful push then starts a
 * cooldown so the unwedge can never fire repeatedly. */
#define PORT_UNWEDGE_HOLD_FIELDS 2
#define PORT_UNWEDGE_MOVE_EPS 1.5f     /* units of travel that reset the hold */
#define PORT_UNWEDGE_COOLDOWN_FIELDS 2 /* min fields between successful pushes - multi-push escapes must stay unnoticeable */

/* GE_BLOCKDBG: say why the unwedge declined, rate-limited. Without this a
 * "still stuck" report cannot tell "a gate rejected it" from "no legal point
 * found". */
static void portUnwedgeWhy(const char *reason)
{
    static s32 last;

    if (getenv("GE_BLOCKDBG") == NULL)
    {
        return;
    }
    if ((s32) g_GlobalTimer - last < 60)
    {
        return;
    }
    last = (s32) g_GlobalTimer;
    fprintf(stderr, "ge:   unwedge declined: %s\n", reason);
    fflush(stderr);
}

static struct coord3d s_portBtracePre;   /* GE_BLOCKTRACE: position before the vanilla escape paths */
static s32 s_portLastBlocked, s_portLastFrac, s_portLastUnw; /* GE_CLIPDBG: what this tick's move went through */
/* What refused the PLAIN move this tick, captured before the vanilla escapes
 * run: the escapes leave their own prop hits behind, and reading those made the
 * unwedge bounce Bond off walls. */
static PropRecord *s_portBlockCauseProp;   /* NULL = the plain move was refused by a tile edge (or nothing recorded) */
static f32 s_portBlockCauseTop;

static s32 portUnwedgePlayer(struct coord3d *edge0, struct coord3d *edge1)
{
    extern s32 get_is_ramrom_flag(void);
    extern PropRecord *g_portBlockProp;
    extern s32 g_portBlockTick;
    extern f32 g_portBlockTop;
    /* Start at a size that actually clears an obstacle; tiny 1-2 unit steps
     * only moved him to another blocked spot and he got caught repeatedly. */
    static const f32 tries[7] = { 6.0f, 10.0f, 14.0f, 20.0f, 27.0f, 34.0f, PORT_UNWEDGE_MAX_PUSH };
    static s32 off = -1;
    static s32 stuckfields[4];          /* fields accumulated while qualifying AND stationary */
    static struct coord3d stuckanchor[4]; /* where the hold started */
    static u32 lastpush60[4];           /* g_GlobalTimer of the last successful push + 1; 0 = never */
    s32 pn;
    struct coord3d cur;
    struct coord3d trial;
    StandTile *stan;
    f32 ex, ez, elen;
    f32 vx, vz, proj;
    f32 nx, nz, nlen;
    s32 i;
    s32 curlegal;

    if (off < 0)
    {
        off = (getenv("GE_UNWEDGE_OFF") != NULL);
    }
    if (off || get_is_ramrom_flag() != 0)
    {
        return 0;
    }
    /* First-person play only. During intros the player is held on the spawn
     * pad, so a push would be undone and redone every few ticks and shake the
     * swirl camera. */
    if (g_CameraMode != CAMERAMODE_FP)
    {
        return 0;
    }

    pn = get_cur_playernum() & 3;
    cur = g_CurrentPlayer->field_488.collision_position;

    /* STILLNESS first, independent of every gate below: at uncapped tick
     * rates the gates flap per tick, so they only veto the instant and only
     * real movement (or a push) resets the hold. */
    {
        f32 adx = cur.f[0] - stuckanchor[pn].f[0];
        f32 adz = cur.f[2] - stuckanchor[pn].f[2];

        if (stuckfields[pn] == 0
            || (adx * adx) + (adz * adz) > PORT_UNWEDGE_MOVE_EPS * PORT_UNWEDGE_MOVE_EPS)
        {
            stuckanchor[pn] = cur;
            stuckfields[pn] = (g_ClockTimer > 0) ? g_ClockTimer : 1; /* the hold has begun */
        }
        else if (stuckfields[pn] < 1000)
        {
            stuckfields[pn] += g_ClockTimer;
        }
    }

    /* Grounded only: a wedge is a resting state, and being blocked while
     * airborne mid-fall is normal. field_70 tracks the floor once he lands. */
    if (g_CurrentPlayer->field_70 > g_CurrentPlayer->stanHeight)
    {
        portUnwedgeWhy("airborne");
        return 0;
    }

    /* Qualify when (a) Bond stands somewhere illegal, or (b) he stands legally
     * but can't advance even though he clears the blocker's top at his own
     * footing (on steep slopes the height gate samples the lower destination
     * floor, as on the Archives roof). */
    {
        static s32 probe = -1;
        if (probe < 0) { probe = (getenv("GE_UNWEDGE_PROBE") == NULL || getenv("GE_UNWEDGE_PROBE")[0] != '0'); }
        curlegal = probe ? bondviewTryMoveToStan(&cur, &stan) : 1;
    }
    if (curlegal != 0)
    {
        f32 stepover = stanGetPositionYValue(g_CurrentPlayer->field_488.current_tile_ptr,
                                             cur.f[0], cur.f[2]) + 30.0f;
        static s32 oldb = -1;

        if (oldb < 0)
        {
            oldb = (getenv("GE_UNWEDGE_OLDB") != NULL);   /* A/B: the older arm (b) */
        }

        if (oldb)
        {
            if (!(g_portBlockTick == (s32) g_GlobalTimer && g_portBlockProp != NULL
                  && g_portBlockTop < stepover))
            {
                portUnwedgeWhy("current position legal and blocker is not below the step-over line");
                return 0;
            }
        }
        /* Only the prop that refused the PLAIN move counts (see
         * s_portBlockCauseProp). Don't add a "prop above his floor" test: on
         * the Archives roof the pane sits wholly below his feet yet still
         * blocks the downhill step, which is the wedge this arm is for. */
        else if (!(s_portBlockCauseProp != NULL && s_portBlockCauseTop < stepover))
        {
            portUnwedgeWhy(s_portBlockCauseProp == NULL
                           ? "current position legal and the move was refused by a tile edge, not a prop"
                           : "current position legal and the refusing prop is not below the step-over line");
            return 0;
        }
    }

    /* the stillness hold (accumulated at the top): a moving Bond never
     * arms; a wedged one arms after ~2 fields of going nowhere */
    if (stuckfields[pn] <= PORT_UNWEDGE_HOLD_FIELDS)
    {
        portUnwedgeWhy("hold accumulating (bond recently moved)");
        return 0;
    }

    /* cooldown: one push per PORT_UNWEDGE_COOLDOWN_FIELDS at most (a push
     * breaks the anchor anyway, so this is anti-machine-gun insurance) */
    if (lastpush60[pn] != 0
        && (u32) g_GlobalTimer + 1u - lastpush60[pn] < PORT_UNWEDGE_COOLDOWN_FIELDS)
    {
        return 0;
    }

    /* Static blockers only: a chr pinning Bond will walk off on its own, but
     * geometry and scenery never move. */
    if (s_portBlockCauseProp != NULL || (g_portBlockTick == (s32) g_GlobalTimer && g_portBlockProp != NULL))
    {
        s32 bt = (s_portBlockCauseProp != NULL ? s_portBlockCauseProp : g_portBlockProp)->type;

        if (bt == PROP_TYPE_CHR || bt == PROP_TYPE_PLAYER || bt == PROP_TYPE_VIEWER)
        {
            portUnwedgeWhy("blocker is a chr");
            return 0;
        }
    }

    ex = edge1->f[0] - edge0->f[0];
    ez = edge1->f[2] - edge0->f[2];
    elen = sqrtf((ex * ex) + (ez * ez));

    if (elen < 0.001f)
    {
        portUnwedgeWhy("degenerate collision edge");
        return 0;
    }

    ex /= elen;
    ez /= elen;

    /* outward normal = the component of (bond - edge0) perpendicular to the
     * edge, i.e. the direction that increases his distance from it */
    vx = cur.f[0] - edge0->f[0];
    vz = cur.f[2] - edge0->f[2];
    proj = (vx * ex) + (vz * ez);
    nx = vx - (proj * ex);
    nz = vz - (proj * ez);
    nlen = sqrtf((nx * nx) + (nz * nz));

    if (nlen < 0.001f)
    {
        portUnwedgeWhy("bond exactly on the edge line");
        return 0;
    }

    nx /= nlen;
    nz /= nlen;

    for (i = 0; i < 7; i++)
    {
        trial.f[0] = cur.f[0] + (nx * tries[i]);
        trial.f[1] = cur.f[1];
        trial.f[2] = cur.f[2] + (nz * tries[i]);

        if (bondviewTryMoveToStan(&trial, &stan) != 0)
        {
            g_CurrentPlayer->field_488.current_tile_ptr = stan;
            g_CurrentPlayer->field_488.collision_position.f[0] = trial.f[0];
            g_CurrentPlayer->field_488.collision_position.f[2] = trial.f[2];
            stuckfields[pn] = 0;
            lastpush60[pn] = (u32) g_GlobalTimer + 1u;

            if (getenv("GE_BLOCKDBG") != NULL)
            {
                fprintf(stderr, "ge: unwedge p%d t=%d (%.0f %.0f) -> (%.0f %.0f) push=%.0f arm=%s prop=%d top=%.1f floor=%.1f held=%d cause=%s\n",
                        get_cur_playernum() & 3, (s32) g_GlobalTimer,
                        cur.f[0], cur.f[2], trial.f[0], trial.f[2], tries[i],
                        curlegal ? "b(legal,low-blocker)" : "a(illegal-here)",
                        (g_portBlockTick == (s32) g_GlobalTimer && g_portBlockProp != NULL),
                        g_portBlockTop, g_CurrentPlayer->stanHeight, stuckfields[pn],
                        s_portBlockCauseProp != NULL ? "prop" : "tile");
                fflush(stderr);
            }

            return 1;
        }
    }

    portUnwedgeWhy("no legal point within the push cap");
    return 0;
}

/* A sub-field tick moves Bond too little to clear a guard or object overlapping
 * him, so judge a prop-refused move at the whole-field step as the 60 tick would. */
static s32 portSubFieldPropMove(struct coord3d *next_pos)
{
    struct coord3d full;
    StandTile *stan;
    StandTile *tile;
    f32 *cur = g_CurrentPlayer->field_488.collision_position.f;

    if (portTickHz() == 60 || get_is_ramrom_flag() != 0 || g_PlayerIsInTank == 1
        || g_GlobalTimerDelta <= 0.0f || g_GlobalTimerDelta >= 1.0f)
    {
        return 0;
    }

    full.f[0] = cur[0] + ((next_pos->f[0] - cur[0]) / g_GlobalTimerDelta);
    full.f[1] = cur[1];
    full.f[2] = cur[2] + ((next_pos->f[2] - cur[2]) / g_GlobalTimerDelta);

    if (bondviewTryMoveToStan(&full, &stan) == 0)
    {
        return 0;
    }

    tile = g_CurrentPlayer->field_488.current_tile_ptr;

    if (walkTilesBetweenPoints_NoCallback(&tile, cur[0], cur[2], next_pos->f[0], next_pos->f[2]) == 0 || tile == NULL)
    {
        return 0;
    }

    g_CurrentPlayer->field_488.current_tile_ptr = tile;
    cur[0] = next_pos->f[0];
    cur[2] = next_pos->f[2];

    return 1;
}
#endif



void bondviewCalcUpdatePlayerCollision(struct coord3d *offset, s32 allow_scoot)
{
    struct coord3d next_pos; // spb4
    struct coord3d collision1_pt0; // spa8
    struct coord3d collision1_pt1; // sp9c
    struct rect4f *polygon; // sp98
    s32 edges; // sp94
    struct TankRecord *tank_objrecord; // no stack
    struct ObjectRecord *obj;
    f32 *farr5;
    f32 *farr6;
    f32 temp_f2; // sp80
    struct coord3d collision2_pt0;  // sp74
    struct coord3d collision2_pt1; // sp68
    StandTile *stan; // no stack
    struct coord3d collision3_pt0; // sp58
    struct coord3d collision3_pt1; // sp4c
    s32 tile_count; // sp48
    s32 i; // sp44
    s32 temp_a3; // no stack
    s32 phi_a0_3; // sp3c
    s32 temp_v0_7; // no stack


    g_CurrentPlayer->bondprevpos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
    g_CurrentPlayer->bondprevpos.f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
    g_CurrentPlayer->bondprevpos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2];

    next_pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0] + offset->f[0];
    next_pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2] + offset->f[2];

    g_BondCanEnterTank = 0;
#ifndef TARGET_N64
    s_portLastBlocked = 0; s_portLastFrac = 9; s_portLastUnw = 0;
#endif

    g_CurrentPlayer->autocrouchpos = CROUCH_STAND;

    if (g_WorldTankProp != NULL)
    {
        chraiGetCollisionBoundsWithoutY(g_WorldTankProp, &polygon, &edges);

        if ((g_PlayerIsInTank == 1)
            || (chrpropTestPointInPolygon(&g_CurrentPlayer->field_488.collision_position, polygon, edges) != 0)
            || ((chrobjTestPointPolygonCollision(&g_CurrentPlayer->field_488.collision_position, g_CurrentPlayer->field_488.collision_radius, polygon, edges) != 0)))
        {

            obj = g_WorldTankProp->obj;
            tank_objrecord = (struct TankRecord *)g_WorldTankProp->obj;

            /// TODO: replace with ModelNode structs
            farr5 = (f32*)obj->model->obj->Switches[5]->Data;
            farr6 = (f32*)obj->model->obj->Switches[6]->Data;
            g_PlayerTankProp = g_WorldTankProp;

            temp_f2 = (farr5[4] - farr5[3]) * obj->model->scale;

            if (g_PlayerIsInTank == 1
                || (chrpropTestPointInPolygon(&g_CurrentPlayer->field_488.collision_position, &tank_objrecord->rect, (intptr_t)tank_objrecord->collision) != 0))
            {
                temp_f2 += (farr6[4] - farr6[3]) * obj->model->scale;
                g_BondCanEnterTank = 1;
            }

            if ((g_PlayerIsInTank == 0) && (g_PlayerTankYOffset < temp_f2))
            {
                g_PlayerTankYOffset += (20.0f * g_GlobalTimerDelta);
                if ((temp_f2 < g_PlayerTankYOffset))
                {
                    //empty;
                }
                else
                {
                    return;
                }

                g_PlayerTankYOffset = temp_f2;
            }
            else
            {
                g_PlayerTankYOffset = temp_f2;
            }

            if (g_PlayerIsInTank == 1)
            {
                if (g_EnterTankAudioState == TANK_RUN_STATE_NOT_RUNNING)
                {
                    g_PlayerTankYOffset += -37.0f * (1.0f - g_TankEnteringSitHeightRemain);
                }
                else
                {
                    g_PlayerTankYOffset += -37.0f;
                }
            }
        }
        else
        {
            if (g_PlayerTankProp != NULL)
            {
                g_WorldTankProp = NULL;
                g_PlayerTankProp = NULL;
                g_PlayerTankYOffset = 0.0f;
            }
        }
    }

    // This `if` block looks like Perfect Dark bbike0f0d3c60
    if (bondviewTrySimpleMovePlayerCollision(&next_pos, &collision1_pt0, &collision1_pt1) == 0)
    {
#ifndef TARGET_N64
        /* GE_BLOCKDBG=1: name whatever just refused Bond's move. Must run
         * HERE, before the fraction/edge/hop fallbacks below - those call
         * back into the stan tests and overwrite stanSavedColl_*. */
        portBlockDbgReport(&next_pos, &collision1_pt0, &collision1_pt1);
        s_portBtracePre = g_CurrentPlayer->field_488.collision_position;
        { /* both prop record paths (line hit, volume hit) NULL the tile
           * record; the edge walker sets it - so "prop refused it" is
           * exactly: a prop stamped THIS tick and no tile record. */
            extern PropRecord *g_portBlockProp;
            extern s32 g_portBlockTick;
            extern f32 g_portBlockTop;
            extern struct StandTile *stanSavedColl_tile;

            s_portBlockCauseProp = (g_portBlockTick == (s32) g_GlobalTimer && g_portBlockProp != NULL
                                    && stanSavedColl_tile == NULL) ? g_portBlockProp : NULL;
            s_portBlockCauseTop = g_portBlockTop;
        }
        if (s_portBlockCauseProp != NULL && portSubFieldPropMove(&next_pos))
        {
            temp_v0_7 = 1;
            goto port_subfield_moved;
        }
#endif
        // return values are:
        //   1 if able to update stan and collision position
        //   zero if still unable to move by failing on the same collision edge
        //   -1 otherwise (still unable to move).
        temp_v0_7 = bondviewTryFractionMovePlayerCollision(&next_pos, &collision1_pt0, &collision1_pt1, &collision2_pt0, &collision2_pt1);

        if ((temp_v0_7 > 0) || (temp_v0_7 < 0))
        {
            if ((allow_scoot != 0)
                && (bondviewTryEdgeMovePlayerCollision(&next_pos, &collision1_pt0, &collision1_pt1) <= 0)
                && (bondviewTryEndHopPlayerCollision(&next_pos, &collision1_pt0, &collision1_pt1) == 0))
            {
                // empty
            }
        }
        else if (temp_v0_7 == 0)
        {
            bondviewTryFractionMovePlayerCollision(&next_pos, &collision2_pt0, &collision2_pt1, &collision3_pt0, &collision3_pt1);

            if ((allow_scoot != 0)
                && (bondviewTryEdgeMovePlayerCollision(&next_pos, &collision2_pt0, &collision2_pt1) <= 0)
                && (bondviewTryEdgeMovePlayerCollision(&next_pos, &collision1_pt0, &collision1_pt1) <= 0)
                && (bondviewTryEndHopPlayerCollision(&next_pos, &collision2_pt0, &collision2_pt1) == 0))
            {
                bondviewTryEndHopPlayerCollision(&next_pos, &collision1_pt0, &collision1_pt1);
            }
        }
#ifndef TARGET_N64
port_subfield_moved:
        /* GE_BLOCKDBG: did any escape path move him? allow_scoot gates the
         * edge-scoot and end-hop that vanilla uses to slide out of a
         * collision; frac is the fraction-move result. */
        portBlockDbgOutcome(allow_scoot, temp_v0_7);
        {
            s32 unw = portUnwedgePlayer(&collision1_pt0, &collision1_pt1);
            static s32 btrace = -1;

            s_portLastBlocked = 1; s_portLastFrac = temp_v0_7; s_portLastUnw = unw;

            if (btrace < 0)
            {
                btrace = (getenv("GE_BLOCKTRACE") != NULL);
            }
            if (btrace)
            { /* GE_BLOCKTRACE=1: EVERY blocked tick, unthrottled - who moved
               * Bond and by how much (pre = before the vanilla escapes,
               * post = after them and the unwedge) */
                fprintf(stderr, "ge: btrace t=%d ct=%d pre=(%.2f %.2f) want=(%.2f %.2f) post=(%.2f %.2f) frac=%d scoot=%d unw=%d edge=(%.0f %.0f)-(%.0f %.0f) th=%.2f spth=%.3f crouch=%d\n",
                        (s32) g_GlobalTimer, g_ClockTimer,
                        s_portBtracePre.f[0], s_portBtracePre.f[2], next_pos.f[0], next_pos.f[2],
                        g_CurrentPlayer->field_488.collision_position.f[0], g_CurrentPlayer->field_488.collision_position.f[2],
                        temp_v0_7, allow_scoot, unw,
                        collision1_pt0.f[0], collision1_pt0.f[2], collision1_pt1.f[0], collision1_pt1.f[2],
                        g_CurrentPlayer->vv_theta, g_CurrentPlayer->speedtheta, g_CurrentPlayer->autocrouchpos);
                fflush(stderr);
            }
        }
#endif
    }

    /**
     * This block seems to be some error checking code, this will only occur when Bond
     * goes out of bounds.
    */
#ifndef TARGET_N64
    { /* MP debug: remember each player's last GOOD footing so the OOB
       * report can name the tile whose exit link went bad. */
        extern StandTile *portLastGoodTile[4];
        extern coord3d portLastGoodPos[4];
        StandTile *ct = g_CurrentPlayer->field_488.current_tile_ptr;
        if (ct != NULL && ct->id != 0 &&
            stanTestPointWithinTileBoundsMaybe(ct,
                g_CurrentPlayer->field_488.collision_position.f[0],
                g_CurrentPlayer->field_488.collision_position.f[2]) != 0)
        {
            s32 pn = get_cur_playernum() & 3;
            portLastGoodTile[pn] = ct;
            portLastGoodPos[pn] = g_CurrentPlayer->field_488.collision_position;
        }
    }
#endif

    if (stanTestPointWithinTileBoundsMaybe(
            g_CurrentPlayer->field_488.current_tile_ptr,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2]) == 0)
    {
        if(1);

        stan = g_CurrentPlayer->field_488.current_tile_ptr;

#ifndef TARGET_N64
        { /* MP debug: log a player OUT OF BOUNDS (position off their tile),
           * the cause of red-void reports, with enough context to find the
           * escape hole. Rate-limited per player. */
            extern StandTile *portLastGoodTile[4];
            extern coord3d portLastGoodPos[4];
            static u32 oobcount[4];
            s32 pn = get_cur_playernum() & 3;
            if ((oobcount[pn]++ % 60u) == 0u)
            {
                StandTile *lg = portLastGoodTile[pn];
                fprintf(stderr,
                        "ge: OOB p%d pos=(%.1f %.1f %.1f) tile=%p id=%06x room=%d | lastgood id=%06x room=%d pos=(%.1f %.1f) base=%p\n",
                        pn,
                        g_CurrentPlayer->field_488.collision_position.f[0],
                        g_CurrentPlayer->field_488.collision_position.f[1],
                        g_CurrentPlayer->field_488.collision_position.f[2],
                        (void *) stan,
                        (stan != NULL) ? (u32) stan->id : 0u,
                        (stan != NULL) ? (s32) stan->room : -1,
                        (lg != NULL) ? (u32) lg->id : 0u,
                        (lg != NULL) ? (s32) lg->room : -1,
                        portLastGoodPos[pn].f[0], portLastGoodPos[pn].f[2],
                        (void *) standTileStart);
                fflush(stderr);
            }
        }
#endif

        for (tile_count=0; tile_count<5; tile_count++)
        {
            /// TODO: fix the horrible casts below.

            for (i=0, phi_a0_3 = 0; i<(STAN_PTCOUNT(stan->tail.half)); i++)
            {
                // maybe: if (( stan->points[i+1].link >> 4) != 0)
                if (( ((struct StandTilePoint*)stan) [i+1].link >> 4) != 0)
                {
                    if(1);
                    phi_a0_3++;
                }
            }

#ifndef TARGET_N64
            /* x86 traps on integer %0 where MIPS gave garbage, so a tile with
             * no linked neighbours crashed here; break leaves stan unchanged,
             * as the N64 effectively did. */
            if (phi_a0_3 == 0)
            {
                break;
            }
#endif
            temp_a3 = randomGetNext() % (u32)phi_a0_3;

            for (i=0, phi_a0_3 = 0; i<(STAN_PTCOUNT((s16) stan->tail.half)); i++)
            {
                // maybe: if (( stan->points[i+1].link >> 4) != 0)
                if (( ((struct StandTilePoint*)stan) [i+1].link >> 4) != 0)
                {
                    if (phi_a0_3 == temp_a3)
                    {
                        // note: no `>> 4`
                        // maybe: stan = &(standTileStart)[stan->points[i+1].link];
                        stan = (struct StandTile*)&((struct dummy_struct*)standTileStart)[( ((struct StandTilePoint*)stan)[i+1].link)];
                        break;
                    }

                    phi_a0_3++;
                }
            }

            if(1);

            if (stanTestPointWithinTileBoundsMaybe(
                stan,
                g_CurrentPlayer->field_488.collision_position.f[0],
                g_CurrentPlayer->field_488.collision_position.f[2]))
            {
                g_CurrentPlayer->field_488.current_tile_ptr = stan;
                break;
            }
        }
    }

    bondviewUpdatePlayerRoom(g_CurrentPlayer);

    if (g_CurrentPlayer->field_488.current_tile_ptr != NULL)
    {
        objectivestatusCheckRoomEntered(g_CurrentPlayer->field_488.current_tile_ptr->room);
    }

#ifndef TARGET_N64
    { /* GE_CLIPDBG=1: re-test Bond's FINAL position each tick against the
       * wall edges and report any keep-out penetration deeper than 1 unit, with
       * the path that produced it (stanSavedColl_* restored after). */
        static s32 clipdbg = -1;

        if (clipdbg < 0)
        {
            clipdbg = (getenv("GE_CLIPDBG") != NULL);
        }
        /* fully crouched = the crawl-under rule (bondviewTryMoveToStan
         * ignores the standing edge result), so vents read as walls here -
         * skip that state rather than flood the log */
        if (clipdbg && g_CurrentPlayer->field_488.current_tile_ptr != NULL && g_PlayerIsInTank == 0
            && g_CurrentPlayer->ducking_height_offset != FULL_CROUCH_OFFSET)
        {
            extern struct StandTile *stanSavedColl_tile;
            extern s32 stanSavedColl_pointI;
            extern f32 getShortest2dDispToInfTileEdgeUnscaled(StandTile *tile, int index, f32 x, f32 z);
            struct StandTile *savedtile = stanSavedColl_tile;
            s32 savedpt = stanSavedColl_pointI;
            PropRecord *savedprop = stanSavedColl_posData;
            StandTile *t = g_CurrentPlayer->field_488.current_tile_ptr;
            struct StandTileLocusCallbackRecord rec;
            f32 r, h, a30;
            f32 px = g_CurrentPlayer->field_488.collision_position.f[0];
            f32 pz = g_CurrentPlayer->field_488.collision_position.f[2];
            s32 res;

            bondviewGetCollisionRadius(g_CurrentPlayer->prop, &r, &h, &a30);
            res = stanTileDistanceRelated(&t, px, pz, r, &rec);

            if (res == STAN_COLLISION_FOUND && stanSavedColl_tile != NULL)
            {
                f32 d = getShortest2dDispToInfTileEdgeUnscaled(stanSavedColl_tile, stanSavedColl_pointI, px, pz);

                if (d < r - 1.0f)
                {
                    fprintf(stderr, "ge: clip p%d t=%d ct=%d depth=%.2f dist=%.2f r=%.0f tile=%06x pt=%d pos=(%.2f %.2f) prev=(%.2f %.2f) blocked=%d frac=%d unw=%d autocrouch=%d duck=%.1f\n",
                            get_cur_playernum() & 3, (s32) g_GlobalTimer, g_ClockTimer, r - d, d, r,
                            (u32) stanSavedColl_tile->id, stanSavedColl_pointI, px, pz,
                            g_CurrentPlayer->bondprevpos.f[0], g_CurrentPlayer->bondprevpos.f[2],
                            s_portLastBlocked, s_portLastFrac, s_portLastUnw,
                            g_CurrentPlayer->autocrouchpos, g_CurrentPlayer->ducking_height_offset);
                    fflush(stderr);
                }
            }

            stanSavedColl_tile = savedtile;
            stanSavedColl_pointI = savedpt;
            stanSavedColl_posData = savedprop;
        }
    }
#endif
}


/**
 * Address: 7F07DE64
 */
void bondviewDeregisterPlayerRoom(struct player *player) 
{
    chrpropDeregisterRoom(player->prop, player->registeredroom);
    player->registeredroom = -1;
}


/**
 * Address 0x7F07DE9C.
 */
void bondviewUpdatePlayerRoom(struct player *player)
{
    bondviewDeregisterPlayerRoom(player);

    if (player->prop->chr)
    {
        chrDetectRooms(player->prop->chr);
        return;
    }

    if (player->field_488.current_tile_ptr)
    {
        player->registeredroom = (s16) player->field_488.current_tile_ptr->room;

        chrpropRegisterRoom(player->prop, player->registeredroom);
    }
}


/**
 * Address: 7F07DEFC
 */
void bondviewInitPauseTransition(void)
{
    g_CurrentPlayer->pause_starting_angle = g_CurrentPlayer->vv_verta;
    g_CurrentPlayer->pause_transition_time = 0.0f;
    g_CurrentPlayer->pause_state = 0;
}


/**
 * Set the pause tilt's start and end pitch. Also calculates how long the transition should take.
 * The greater the angular distance between the player's camera pitch and the pause pitch of -40 degrees,
 * the longer it takes to pause.
 *
 * @param topause: When set, pause_target_verta will be -40.0f, otherwise g_CurrentPlayer->vv_verta.
 * @return: How long the tilt should take.
 *
 * Address 0x7F07DF28.
 */
f32 bondviewSetupPauseTransition(bool topause)
{
    f32 anglediff;
    f32 duration;

    if (topause)
    {
        g_CurrentPlayer->pause_saved_verta = g_CurrentPlayer->vv_verta;
        g_CurrentPlayer->pause_target_verta = -40.0f;
    }
    else
    {
        g_CurrentPlayer->pause_saved_verta = g_CurrentPlayer->pause_starting_angle;
        g_CurrentPlayer->pause_target_verta = g_CurrentPlayer->vv_verta;
    }

    anglediff = g_CurrentPlayer->pause_saved_verta - g_CurrentPlayer->pause_target_verta;

    if (anglediff < 0.0f)
    {
        anglediff = -anglediff;
    }

    /**
     * If the delta between pause_saved_verta and pause_target_verta is large,
     * the time penalty per degree above 60 is cut in half.
     */
    if (anglediff >= 60.0f)
    {
        duration = (((anglediff - 60.0f) * 60.0f * 0.5f) / 60.0f) + 60.0f;
    }
    else if (anglediff <= 0.0f)
    {
        duration = 0.0f;
    }
    else
    {
        // this is a different `60` than the other values above!
        duration = (anglediff * 60.f) / 60.f;
    }

    return duration;
}


void bondviewStartPauseTransition(f32 duration) {
    g_CurrentPlayer->pause_transition_time = 0.0f;
    g_CurrentPlayer->pause_transition_duration = duration;
    g_CurrentPlayer->pause_state = 1;
}


void bondviewStartUnpauseTransition(f32 duration) {
    g_CurrentPlayer->pause_transition_time = 0.0f;
    g_CurrentPlayer->pause_transition_duration = duration;
    g_CurrentPlayer->pause_state = 2;
}


bool bondViewIsPauseTransitioning(void) {
    return (g_CurrentPlayer->pause_state != 0 && g_CurrentPlayer->pause_state != 3);
}


#ifndef TARGET_N64
/* The watch's open and close rate. Port Options' Fast Pause uses 1.7, the cap that
 * trigger_solo_watch_menu grows watch_transition_time to on each open. */
#define WATCH_RATE_LEVEL_LOAD 0.9090909f   /* bondview_r.c, at every level load */
#define WATCH_RATE_CAP        1.7f
static s32 bondviewFastPause(void)
{
    extern s32 portFastPause(void);   /* video.c */

    return portFastPause();
}
static f32 bondviewWatchRate(void)
{
    return bondviewFastPause() ? WATCH_RATE_CAP : watch_transition_time;
}
#else
#define bondviewWatchRate() watch_transition_time
#endif

/**
 * Transition camera pitch from playing to watch menu (-40.0f degrees).
 * Then when the game is unpaused, transition the camera pitch back to its pitch before pausing began.
 */
void bondviewUpdatePauseTransition(void) {
    f32 prevverta;
    f32 frac;
    f32 weight;

    prevverta = g_CurrentPlayer->vv_verta;

    // Entering pause.
    if (g_CurrentPlayer->pause_state == 1) {
        g_CurrentPlayer->pause_transition_time += g_GlobalTimerDelta * bondviewWatchRate();

        if (g_CurrentPlayer->pause_transition_time < g_CurrentPlayer->pause_transition_duration) {
            // Cosine ease-in-out
            frac = g_CurrentPlayer->pause_transition_time / g_CurrentPlayer->pause_transition_duration;
            weight = (1.0f - cosf((frac * M_TAU_F) * 0.5f)) * 0.5f;

            g_CurrentPlayer->vv_verta = g_CurrentPlayer->pause_saved_verta
                + ((g_CurrentPlayer->pause_target_verta - g_CurrentPlayer->pause_saved_verta) * weight);
        } else {
            g_CurrentPlayer->vv_verta = g_CurrentPlayer->pause_target_verta;
            // Set pause state to paused.
            g_CurrentPlayer->pause_state = 3;
        }
    // Leaving pause.
    } else if (g_CurrentPlayer->pause_state == 2) {
        g_CurrentPlayer->pause_transition_time += g_GlobalTimerDelta * bondviewWatchRate();

        if (g_CurrentPlayer->pause_transition_time < g_CurrentPlayer->pause_transition_duration) {
            // Cosine ease-in-out
            frac = g_CurrentPlayer->pause_transition_time / g_CurrentPlayer->pause_transition_duration;
            weight = (1.0f - cosf((frac * M_TAU_F) * 0.5f)) * 0.5f;

            g_CurrentPlayer->vv_verta = g_CurrentPlayer->pause_target_verta
                + ((g_CurrentPlayer->pause_saved_verta - g_CurrentPlayer->pause_target_verta) * weight);
        } else {
            g_CurrentPlayer->vv_verta = g_CurrentPlayer->pause_saved_verta;
            // Set pause state to unpaused.
            g_CurrentPlayer->pause_state = 0;
        }
    }

    // Wrap vv_verta into [-180, 180)
    if (g_CurrentPlayer->vv_verta < -180.0f) {
        g_CurrentPlayer->vv_verta += 360.0f;
    } else if (g_CurrentPlayer->vv_verta >= 180.0f) {
        g_CurrentPlayer->vv_verta -= 360.0f;
    }

    /**
     * Calculate shortest angular velocity from previous frame,
     * scale it by g_GlobalTimerDelta,
     * clamp it to [-0.7, 0.7] so the pitch change is never too fast.
     */
    if (g_ClockTimer > 0) {
        g_CurrentPlayer->speedverta = g_CurrentPlayer->vv_verta - prevverta;

        if (g_CurrentPlayer->speedverta < 0.0f) {
            g_CurrentPlayer->speedverta += 360.0f;
        }

        if (g_CurrentPlayer->speedverta > 180.0f) {
            g_CurrentPlayer->speedverta -= 360.0f;
        }

        g_CurrentPlayer->speedverta /= g_GlobalTimerDelta + g_GlobalTimerDelta;

        if (g_CurrentPlayer->speedverta < -0.7f) {
            g_CurrentPlayer->speedverta = -0.7f;
        } else if (g_CurrentPlayer->speedverta > 0.7f) {
            g_CurrentPlayer->speedverta = 0.7f;
        }
    }
}


f32 bondViewGetPauseTransitionFrac(void) {

    // Entering pause
    if (g_CurrentPlayer->pause_state == 1) {
        return g_CurrentPlayer->pause_transition_time / g_CurrentPlayer->pause_transition_duration;
    }
    // Leaving pause
    if (g_CurrentPlayer->pause_state == 2) {
        return 1.0f - (g_CurrentPlayer->pause_transition_time / g_CurrentPlayer->pause_transition_duration);
    }
    // Fully paused
    if (g_CurrentPlayer->pause_state == 3) {
        return 1.0f;
    }
    // Unpaused
    return 0.0f;
}

void trigger_watch_zoom(f32 final,f32 time)
{
  g_CurrentPlayer->zoomintime = 0.00000000;
  g_CurrentPlayer->zoomintimemax = time;
  g_CurrentPlayer->zoominfovyold = g_CurrentPlayer->zoominfovy;
  g_CurrentPlayer->zoominfovynew = final;
}


f32 bondviewGetWatchZoomFovy(void) {

    if (g_CurrentPlayer->zoomintime < g_CurrentPlayer->zoomintimemax)
    {
        return g_CurrentPlayer->zoominfovynew;
    }

    return g_CurrentPlayer->zoominfovy;
}

/**
 * Triggers watch zoom if new value.
 *
 * @param zoominfovy: watch zoom fovy.
 *
 * Address 0x7F07E46C.
 */
void bondviewTriggerWatchZoom(f32 zoominfovy)
{
    if (bondviewGetWatchZoomFovy() != zoominfovy)
    {
        if (zoominfovy < g_CurrentPlayer->zoominfovy)
        {
            trigger_watch_zoom(zoominfovy, ((g_CurrentPlayer->zoominfovy - zoominfovy) * 15.0f) / 30.0f);

            return;
        }

        trigger_watch_zoom(zoominfovy, ((zoominfovy - g_CurrentPlayer->zoominfovy) * 15.0f) / 30.0f);
    }
}


#ifndef TARGET_N64
static f32 portWatchZoomSpanScale(void); /* defined below bondviewZoomToWatchOnOpen */
#endif

/**
 * Trigger watch zoom with default angle.
 *
 * Address 0x7F07E504.
 */
void bondviewTriggerWatchZoomDefault(void)
{
#ifdef TARGET_N64
    bondviewTriggerWatchZoom(GE_DEFAULT_FOV);
#else
    /* PORT: the 0.5 deg/frame rate above was tuned for a 60-degree gameplay
     * FOV; with midas.ini fov=120 the zoom back out to gameplay took twice the
     * frames. Scale the rate by the span so the trip takes the vanilla time
     * whatever the FOV (see portWatchZoomSpanScale). */
    if (bondviewGetWatchZoomFovy() != GE_DEFAULT_FOV)
    {
        f32 d = GE_DEFAULT_FOV - g_CurrentPlayer->zoominfovy;
        if (d < 0.0f)
        {
            d = -d;
        }
        trigger_watch_zoom(GE_DEFAULT_FOV, ((d * 15.0f) / 30.0f) * portWatchZoomSpanScale());
    }
#endif
}


#ifndef TARGET_N64
/* WATCH ZOOM vs FOV: the vanilla open/exit zooms take 45 frames for the
 * 60-to-watch-FOV span (the 53.9 / 54.1 constants), so a high midas.ini fov made
 * the trip, and the whole pause opening, crawl. Returns vanilla_span /
 * actual_span (1.0 at fov=60) so the trip takes 45 frames from any FOV. */
static f32 portWatchZoomSpanScale(void)
{
#if defined(VERSION_EU)
    const f32 watchfov = 6.09999990463f, vanillaspan = 53.9000015259f;
#else
    const f32 watchfov = 5.9f, vanillaspan = 54.1f;
#endif
    f32 span = GE_DEFAULT_FOV - watchfov;
    if (span < 1.0f)
    {
        span = 1.0f; /* fov is clamped to 40..120 in video.c; belt and braces */
    }
    return vanillaspan / span;
}
#endif

/**
 * Address 0x7F07E52C.
 */
void bondviewZoomToWatchOnOpen(void)
{
    f32 f;

#if defined(VERSION_EU)
    f = ((6.09999990463f - g_CurrentPlayer->zoominfovy) * 45.0f) / -53.9000015259f;
#else
    f = ((5.9f - g_CurrentPlayer->zoominfovy) * 45.0f) / -54.1f;
#endif
#ifndef TARGET_N64
    f *= portWatchZoomSpanScale(); /* 45 frames from the gameplay FOV, whatever it is */
#endif

    if (f < 0.0f)
    {
        f = -f;
    }

#if defined(VERSION_EU)
    trigger_watch_zoom(6.09999990463f, f);
#else
    trigger_watch_zoom(5.9f, f);
#endif

}



/**
 * Address 0x7F07E594.
 */
void bondviewZoomFromWatchOnExit(void)
{
    f32 f;

#if defined(VERSION_EU)
    f = ((GE_DEFAULT_FOV - g_CurrentPlayer->zoominfovy) * 45.0f) / -53.9000015259f;
#else
    f = ((GE_DEFAULT_FOV - g_CurrentPlayer->zoominfovy) * 45.0f) / -54.1f;
#endif
#ifndef TARGET_N64
    f *= portWatchZoomSpanScale(); /* 45 frames back to the gameplay FOV, whatever it is */
#endif

    if (f < 0.0f)
    {
        f = -f;
    }

    trigger_watch_zoom(GE_DEFAULT_FOV, f);
}



s32 check_watch_page_transistion_running(void)
{
    return (g_CurrentPlayer->zoomintime < g_CurrentPlayer->zoomintimemax);
}


/**
 * Address 0x7F07E62C.
 */
void bondviewUpdateWatchZoomIn(void)
{
    if (g_CurrentPlayer->zoomintime < g_CurrentPlayer->zoomintimemax)
    {
        if ((g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5) || (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xc))
        {
#if defined(BUGFIX_R1)
            g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintime + (f32) jpD_800484D0;
#else
            g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintime + (f32) speedgraphframes;
#endif
        }
        else
        {
#if defined(BUGFIX_R1)
            g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintime + (jpD_800484D0 * watch_transition_time);
#else
#ifndef TARGET_N64
            /* The aim/sniper zoom only stepped on 60Hz field ticks; advance it by
             * the true delta so it is smooth at higher tick rates. */
            if ((g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0) && portSmoothOn())
            {
                g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintime + (g_GlobalTimerDelta * watch_transition_time);
            }
            else if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0)
            {
                g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintime + (speedgraphframes * bondviewWatchRate());
            }
            else
#endif
            g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintime + (speedgraphframes * watch_transition_time);
#endif
        }

        if (g_CurrentPlayer->zoomintimemax < g_CurrentPlayer->zoomintime)
        {
            g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintimemax;
        }

        g_CurrentPlayer->zoominfovy =
            g_CurrentPlayer->zoominfovyold +
            (
                (g_CurrentPlayer->zoomintime *
                    (g_CurrentPlayer->zoominfovynew - g_CurrentPlayer->zoominfovyold)
                )
                / g_CurrentPlayer->zoomintimemax
            );
    }
    else
    {
        g_CurrentPlayer->zoomintime = g_CurrentPlayer->zoomintimemax;
        g_CurrentPlayer->zoominfovy = g_CurrentPlayer->zoominfovynew;
    }

    set_cur_player_fovy(g_CurrentPlayer->zoominfovy);
    viSetFovY(g_CurrentPlayer->zoominfovy);
}




/**
 * Address 0x7F07E740.
 */
f32 bondviewWatchAnimationRelated(void)
{
    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x4)
    {
        return ((45.0f - g_CurrentPlayer->zoomintimemax) + g_CurrentPlayer->zoomintime) / 45.0f;
    }

    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x6)
    {
        return (g_CurrentPlayer->zoomintimemax - g_CurrentPlayer->zoomintime) / 45.0f;
    }

    if ((g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5) || (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xc))
    {
        return 1.0f;
    }

    return 0.0f;
}





void sub_GAME_7F07E7CC(void) {
    ModelFileHeader *itemheader;

    itemheader = get_ptr_itemheader_in_hand(1);
    modelCalculateRwDataLen(itemheader);
#ifndef VERSION_EU
    if (0x32 < itemheader->numRecords) {
        return_null();
    }
#endif
    animInit(WATCH_MODEL(g_CurrentPlayer), itemheader, WATCH_MODEL_DATA(g_CurrentPlayer));
    modelSetScale(WATCH_MODEL(g_CurrentPlayer), c_item_entries[41].scale * 0.10000001f);
    modelSetAnimation(WATCH_MODEL(g_CurrentPlayer), (ModelAnimation *)&ptr_animation_table->data[PTR_ANIM_bond_watch], 0, 0.0f, 0.5f * bondviewWatchRate(), 0.0f);
    g_CurrentPlayer->step_in_view_watch_animation = 0;
}





/**
 * Address 0x7F07E8B0.
 */
void bondviewSetPauseWatchRelated(f32 arg0)
{
    if (g_CurrentPlayer->step_in_view_watch_animation == 0)
    {
        g_CurrentPlayer->pause_watch_related_scaled = 20.0f / arg0;
    }
    else
    {
        g_CurrentPlayer->pause_watch_related_scaled = (20.0f - WATCH_MODEL(g_CurrentPlayer)->animframe1) / arg0;
    }

    g_CurrentPlayer->step_in_view_watch_animation = 1;
    g_CurrentPlayer->pause_watch_related = arg0;
}




/**
 * Address 0x7F07E910.
 */
void bondviewSetPauseWatchRelatedAlt(f32 arg0)
{
    if (g_CurrentPlayer->step_in_view_watch_animation == 3)
    {
        g_CurrentPlayer->pause_watch_related_scaled = 20.0f / arg0;
    }
    else
    {
        g_CurrentPlayer->pause_watch_related_scaled = WATCH_MODEL(g_CurrentPlayer)->animframe1 / arg0;
    }

    g_CurrentPlayer->step_in_view_watch_animation = 2;
    g_CurrentPlayer->pause_watch_related = arg0;
}





/**
 * Address 0x7F07E964.
 */
void bondviewStepWatchAnimation(void)
{
    if ((g_CurrentPlayer->step_in_view_watch_animation != 0) && (g_CurrentPlayer->step_in_view_watch_animation != 3))
    {
        if (g_CurrentPlayer->step_in_view_watch_animation == 1)
        {
            g_CurrentPlayer->pause_animation_counter += g_GlobalTimerDelta * bondviewWatchRate() * g_CurrentPlayer->pause_watch_related_scaled;

            if (g_CurrentPlayer->pause_animation_counter > 20.0f)
            {
                g_CurrentPlayer->pause_animation_counter = 20.0f;
                g_CurrentPlayer->step_in_view_watch_animation = 3;
            }
        }
        else if (g_CurrentPlayer->step_in_view_watch_animation == 2)
        {
            g_CurrentPlayer->pause_animation_counter -= g_GlobalTimerDelta * bondviewWatchRate() * g_CurrentPlayer->pause_watch_related_scaled;

            if (g_CurrentPlayer->pause_animation_counter < 0.0f)
            {
                g_CurrentPlayer->pause_animation_counter = 0.0f;
                g_CurrentPlayer->step_in_view_watch_animation = 0;
            }
        }

        modelSetAnimFrame2(WATCH_MODEL(g_CurrentPlayer), g_CurrentPlayer->pause_animation_counter, 0.0f);
    }
}






/**
 * Address 0x7F07EA78.
 */
f32 bondviewGetPauseAnimationPercent(void)
{
    if ((g_CurrentPlayer->step_in_view_watch_animation == 1) || (g_CurrentPlayer->step_in_view_watch_animation == 2))
    {
        return g_CurrentPlayer->pause_animation_counter / 20.0f;
    }

    if (g_CurrentPlayer->step_in_view_watch_animation == 3)
    {
        return 1.0f;
    }

    return 0.0f;
}




void set_BONDdata_outside_watch_menu_flag(s32 arg0) {
    g_CurrentPlayer->outside_watch_menu = arg0;
}

s32 get_BONDdata_outside_watch_menu_flag(void) {
    return g_CurrentPlayer->outside_watch_menu;
}





void bondviewPlayerStopAudioForPause(void)
{
    struct hand *hand;
	s32 i;
    ObjectRecord *obj;
    PropRecord *prop;

    deactivate_alarm_sound_effect();
    check_deactivate_gas_sound();

    for (i = 0; i < 2; i++)
    {
        hand = &g_CurrentPlayer->hands[i];

        if (hand->audioHandle && sndGetPlayingState(hand->audioHandle) != AL_STOPPED) {
			sndDeactivate(hand->audioHandle);
		}
    }

    for (i = 0; i < 2; i++)
    {
        if (g_TankSfxState[i] && sndGetPlayingState(g_TankSfxState[i]) != AL_STOPPED) {
			sndDeactivate(g_TankSfxState[i]);
		}
    }

    for (prop = chrpropGetActiveTail(); prop; prop = prop->prev)
    {
        if (prop->type != PROP_TYPE_DOOR && prop->type == PROP_TYPE_OBJ)
        {
            obj = prop->obj;

            if (obj->type == PROPDEF_VEHICHLE)
            {
                VehichleRecord *vehicle = (VehichleRecord *)prop->obj;
                if (vehicle->Sound && sndGetPlayingState(vehicle->Sound) != AL_STOPPED) {
                    sndDeactivate(vehicle->Sound);
                }
            }
            else if (obj->type == PROPDEF_AIRCRAFT)
            {
                AircraftRecord *aircraft = (AircraftRecord *)prop->obj;
                if (aircraft->Sound && sndGetPlayingState(aircraft->Sound) != AL_STOPPED) {
                    sndDeactivate(aircraft->Sound);
                }
            }

            if(1);
        }
    }
}





/**
 * US address 7F07EC54.
 * JP address 7F07F260.
 * EU address 7F07ECF4.
*/
#ifndef TARGET_N64
/* PAUSE BOX: the pause presentation in one place.
 *
 *  raw    - linear 0..1 time. Ramps up while the watch animates in (states
 *           1-4, ~95 ticks), stays at 1 while the menu is open, holds through
 *           the zoom-out (6), then ramps down from the arm lowering to the end
 *           of the camera tilt (7-8, ~60 ticks) plus the state-0 tail.
 *  eased  - what the visuals use. Opening 1-(1-raw)^K, closing raw^K, with
 *           K = 7.21 (80% of the travel in the first 20% of the time, then a
 *           smooth slow-down); both are 1 at raw = 1, so the turnaround is
 *           continuous.
 *
 * It drives (a) the full-screen letterbox: the 240-line image is the N64's
 * 220-line image plus ten lines top and bottom, so the bars are drawn as smooth
 * quarter-pixel overlays and the real viewport snaps to 220@10 only once eased
 * >= 0.98; and (b) the pillarbox overlays at the edges of wide windows
 * (bondviewRenderWatch call site). It also owns the 60 Hz menu field gate
 * (portWatchMenuField): the watch menu's held-button, stick and easing updates
 * run once per 60 Hz field at any tick rate, while edge presses are never gated
 * so a press on a sub-tick is not lost. */
static f32 g_portPauseBoxRaw;
static f32 g_portPauseBoxEased;
static f32 g_portMenuFieldAcc;
static s32 g_portMenuField = 1;
#define PORT_PAUSEBOX_K 7.2126f
#ifndef TARGET_N64
/* WATCH MOUSE: the watch face's page tabs and volume bars are 3D geometry
 * drawn through the second-hand node matrix and the watch perspective, so
 * bondviewRenderWatch stashes that chain here each frame for options.c to
 * project them to 320x240 and hit-test the mouse. */
Mtxf g_portWatchHandMtx;
f32 g_portWatchFovy = 4.6f;
f32 g_portWatchAspect = 1.4545455f;

void portWatchProjectFace(f32 mx, f32 mz, f32 *nx, f32 *ny)
{
    f32 *m = &g_portWatchHandMtx.m[0][0];
    f32 x = mx * 0.25f;
    f32 z = mz * 0.25f;
    f32 vx = x * m[0] + z * m[8] + m[12];   /* row vector: v * M, y = 0 */
    f32 vy = x * m[1] + z * m[9] + m[13];
    f32 vz = x * m[2] + z * m[10] + m[14];
    f32 half = g_portWatchFovy * (M_PI_F / 360.0f);
    f32 cot = cosf(half) / sinf(half);          /* not tanf: this tree's math.h tanf is not libc's */
    f32 w = -vz;                                /* guPerspective: w = -z */
    f32 ndcx, ndcy;
    f32 vh = (f32) bondviewGetCurrentPlayerViewportHeight();
    f32 vu = (f32) bondviewGetCurrentPlayerViewportUly();

    /* the hand matrix is pre-scaled by watch_scale_destination (translation
     * included), so the face sits a tiny fraction of a unit from the eye:
     * no near clamp here, only a divide-by-zero guard */
    if (w < 1.0e-9f && w > -1.0e-9f)
    {
        w = 1.0e-9f;
    }
    ndcx = (vx * cot / g_portWatchAspect) / w;
    ndcy = (vy * cot) / w;
    *nx = (ndcx + 1.0f) * 0.5f * (f32) SCREEN_WIDTH;
    *ny = vu + (1.0f - ndcy) * 0.5f * vh;
}
#endif

f32 portPauseBoxProgress(void)
{
    return g_portPauseBoxEased;
}

/* CLASSIC PILLARS: Classic Mode brings the pause box's side pillars up in
 * play, a 4:3 picture, and turning it off takes them away. Same ease as the
 * pause box, on the true tick delta so it also runs while the watch is up
 * (the sim clock is 0 there). Solo only, as the pause box. */
static f32 g_portClassicPillarsRaw;
static f32 g_portClassicPillarsEased;
f32 portClassicPillarsProgress(void)
{
    return g_portClassicPillarsEased;
}
static void portClassicPillarsTick(void)
{
    extern float powf(float, float);
    extern s32 portClassicPillars(void);   /* video.c; weak 0 headless */
    const s32 up = portClassicPillars() && getPlayerCount() == 1;
    static s32 was_up = 0;
    f32 p = g_portClassicPillarsRaw;

    if (up != was_up)
    {   /* the two curves differ: remap so the drawn width stays continuous */
        f32 e = g_portClassicPillarsEased;

        p = up ? (1.0f - powf(1.0f - e, 1.0f / PORT_PAUSEBOX_K)) : powf(e, 1.0f / PORT_PAUSEBOX_K);
        was_up = up;
    }
    p += (up ? 1.0f : -1.0f) * portTickDelta / 30.0f;   /* half a second each way */
    if (p < 0.0f) { p = 0.0f; }
    if (p > 1.0f) { p = 1.0f; }
    g_portClassicPillarsRaw = p;
    g_portClassicPillarsEased = up ? (1.0f - powf(1.0f - p, PORT_PAUSEBOX_K)) : powf(p, PORT_PAUSEBOX_K);
}
s32 portWatchMenuField(void)
{
    return g_portMenuField;
}
/* the watch MENU is up (open, not raising or lowering) - frametiming.c
 * keeps tickhz=hw's console pacing off for it, as for the front end */
s32 portWatchMenuUp(void)
{
    return g_CurrentPlayer != NULL
        && (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5
         || g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xc);
}
static void portPauseBoxTick(void)
{
    extern float powf(float, float);
    s32 st = g_CurrentPlayer->watch_animation_state;
    f32 p = g_portPauseBoxRaw;
    f32 dt = g_GlobalTimerDelta;
    s32 opening;
    static s32 was_opening = 0;
#ifdef GE_VIDEO
    if (st == WATCH_ANIMATION_0x0)
    {
        extern void portWatchMouseReset(void);
        portWatchMouseReset();
    }
#endif
    opening = (st >= 1 && st <= 5) || st >= 9;
    /* Direction flip mid-travel (e.g. pause spam): the two directions use
     * different curves, so remap raw through the new curve's inverse to keep
     * the drawn value continuous. */
    if (opening != was_opening)
    {
        f32 e = g_portPauseBoxEased;
        if (e < 0.0f) { e = 0.0f; }
        if (e > 1.0f) { e = 1.0f; }
        p = opening ? (1.0f - powf(1.0f - e, 1.0f / PORT_PAUSEBOX_K)) : powf(e, 1.0f / PORT_PAUSEBOX_K);
        was_opening = opening;
    }
    /* Fast Pause: keep pace with the faster arm and tilt. The 95 and 60 tick spans
     * below were measured at the level-load rate; the ratio is against that, not
     * the still-growing watch_transition_time, so the box runs the same on every
     * pause of a level (the watch itself holds at the cap). */
    if (bondviewFastPause())
    {
        dt *= WATCH_RATE_CAP / WATCH_RATE_LEVEL_LOAD;
    }
    if (getPlayerCount() != 1)       { p = 0.0f; }
    else if (st >= 1 && st <= 4)     { p += dt / 95.0f; }   /* press -> menu loaded */
    else if (st == 5 || st >= 9)     { p += 0.1f; }         /* open: settle to 1 (g_GlobalTimerDelta is 0 while the menu is up) */
    else if (st == 6)                { /* hold through the zoom-out from the face */ }
    else if (st == 7 || st == 8)     { p -= dt / 60.0f; }   /* arm lowering (7) -> end of the camera
                                                             * tilt (8),
                                                            * ~60 ticks: the pull-back runs with the
                                                            * arm */
    else                             { p -= dt / 10.0f; }   /* play: make sure it is gone */
    if (p < 0.0f) { p = 0.0f; }
    if (p > 1.0f) { p = 1.0f; }
    g_portPauseBoxRaw = p;
    portClassicPillarsTick();
    if (get_cur_playernum() == 0)
    {
        portHudSlideTick();   /* options.c: the HUD's 4:3 / Wide slide */
    }
    /* both directions ease out: 80% of the travel in the first 20% of the
     * time, continuous through the turnaround (both are 1 at raw=1). */
    g_portPauseBoxEased = opening ? (1.0f - powf(1.0f - p, PORT_PAUSEBOX_K)) : powf(p, PORT_PAUSEBOX_K);
    /* 60 Hz menu field: accumulate the true tick delta (frametiming.h
     * portTickDelta, 1.0 at tickhz 60, 0.5 at 120, ~0.42 uncapped at 144);
     * the sim clock (g_GlobalTimerDelta) is 0 while the menu is up, so it
     * cannot be the source. */
    g_portMenuFieldAcc += portTickDelta;
    if (g_portMenuFieldAcc > 2.0f) { g_portMenuFieldAcc = 2.0f; }
    if (g_portMenuFieldAcc >= 1.0f) { g_portMenuFieldAcc -= 1.0f; g_portMenuField = 1; }
    else                           { g_portMenuField = 0; }
    { /* GE_PAUSEDBG=1: trace (1 line / 30 ticks while non-zero or animating) */
        static s32 dbg = -1; static u32 n;
        if (dbg < 0) { extern char *getenv(const char *); { const char *e = getenv("GE_PAUSEDBG"); dbg = e ? atoi(e) : 0; } }
        static s32 last = -1; extern unsigned int g_PortFramesPumped;
        if (dbg && st != last) { extern int printf(const char *, ...); printf("pausebox: STATE %d -> %d at frame %u raw=%.3f eased=%.3f pausing=%d\n", last, st, g_PortFramesPumped, p, g_portPauseBoxEased, (int) g_CurrentPlayer->pausing_flag); last = st; }
        if (dbg && (st != 0 || p > 0.0f) && (n++ % (dbg >= 2 ? 1 : 30)) == 0) { extern int printf(const char *, ...); printf("pausebox: state=%d raw=%.3f eased=%.3f dt=%.3f viewy=%d viewtop=%d field=%d\n", st, p, g_portPauseBoxEased, dt, (int) g_CurrentPlayer->viewy, (int) g_CurrentPlayer->viewtop, g_portMenuField); }
    }
}
#endif

void bondviewWatchAnimationTick(void)
{
#if defined(VERSION_EU)
    #define WATCH_VAR_LOWER 14
    #define WATCH_VAR_UPPER 29
#else
    #define WATCH_VAR_LOWER 17
    #define WATCH_VAR_UPPER 35
#endif

    s32 sp3c;
    s32 sp38;
    s32 sp34;
    f32 sp30;
    f32 sp2c;
    f32 sp28;
    f32 sp24;
    f32 sp20;

    if (g_CurrentPlayer->watch_animation_state)
    {
        sp34 = getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_TRIGGER || getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_WATCHLASER;
        sp3c = sp34;

#if defined (VERSION_US)
        sp38 = WATCH_VAR_LOWER;

        if (sp34)
        {
            sp38 = WATCH_VAR_UPPER;
        }
#else
        sp38 = (sp34) ? WATCH_VAR_UPPER : WATCH_VAR_LOWER;
#endif

        g_CurrentPlayer->timer_1C4 += g_ClockTimer;
        g_CurrentPlayer->watch_pause_time += 1;
        g_CurrentPlayer->pausing_flag = FALSE;

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x1)
        {
            g_CurrentPlayer->pausing_flag = FALSE;

            if (g_CurrentPlayer->watch_pause_time == 1)
            {
                draw_item_in_hand(GUNLEFT, ITEM_SUIT_LF_HAND);
            }
            else if (g_CurrentPlayer->watch_pause_time == 2)
            {
                if (sp34)
                {
                    draw_item_in_hand(GUNRIGHT, ITEM_UNARMED);
                }
            }
            else
            {
                if (
                    (get_item_in_hand_or_watch_menu(1) == ITEM_SUIT_LF_HAND)
                    && (Gun_hand_without_item(GUNLEFT) != ITEM_UNARMED)
                    && (
                        (sp34 == 0)
                        || (
                            (get_item_in_hand_or_watch_menu(0) == ITEM_UNARMED)
                            && (Gun_hand_without_item(GUNRIGHT) != ITEM_UNARMED)))
                    && (g_CurrentPlayer->timer_1C4 >= sp38))
                {
                    g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x2;
                    g_CurrentPlayer->watch_pause_time = 1;
                    g_CurrentPlayer->timer_1C4 = 0;
                }
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x2)
        {
            g_CurrentPlayer->pausing_flag = FALSE;

            if (g_CurrentPlayer->watch_pause_time == 1)
            {
                gunSetSightVisible(GUNAMMOREASON_DAMAGE, FALSE);
                gunSetGunAmmoVisible(GUNSIGHTREASON_NOCONTROL, FALSE);
                hudmsgsSetOff(PLAYERFLAG_LOCKCONTROLS);
                bondviewSetUpperTextDisplayFlag(PLAYERFLAG_LOCKCONTROLS);
                countdownTimerSetVisible(4, 0);

                if ((g_CurrentPlayer->pause_state == 0) || (g_CurrentPlayer->pause_state == 2) || (g_CurrentPlayer->pause_state == 3))
                {
                    sp20 = bondviewSetupPauseTransition(TRUE);

                    if (sp20 < 30.0f)
                    {
                        sp20 = 30.0f;
                    }

                    bondviewStartPauseTransition(sp20);
                }
            }

            if ((g_CurrentPlayer->pause_transition_duration - g_CurrentPlayer->pause_transition_time) < 30.0f)
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x3;
                g_CurrentPlayer->watch_pause_time = 1;
                g_CurrentPlayer->timer_1C4 = 0;

                sub_GAME_7F07E7CC();
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x3)
        {
            if (g_CurrentPlayer->watch_pause_time == 1)
            {
                sp30 = 40.0f;

                if ((g_CurrentPlayer->step_in_view_watch_animation != 0) && (g_CurrentPlayer->step_in_view_watch_animation != 3))
                {
                    sp30 = ((20.0f - WATCH_MODEL(g_CurrentPlayer)->animframe1) * 40.0f) / 20.0f;
                }

                if ((g_CurrentPlayer->pause_state == 0) || (g_CurrentPlayer->pause_state == 2) || (g_CurrentPlayer->pause_state == 3))
                {
                    sp2c = bondviewSetupPauseTransition(TRUE);
                    sp20 = sp30 - 10.0f;

                    if (sp2c < sp20)
                    {
                        sp2c = sp20;
                    }

                    bondviewStartPauseTransition(sp2c);

                    sp20 = sp2c + 10.0f;

                    if (sp30 < sp20)
                    {
                        sp30 = sp20;
                    }
                }

                bondviewSetPauseWatchRelated(sp30);
            }

            if (
                ((g_CurrentPlayer->step_in_view_watch_animation != 0) && (g_CurrentPlayer->step_in_view_watch_animation != 3))
                ||
                bondViewIsPauseTransitioning()
                )
            {
                g_CurrentPlayer->pausing_flag = TRUE;
            }
            else
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x4;
                g_CurrentPlayer->watch_pause_time = 1;
                g_CurrentPlayer->timer_1C4 = 0;
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x4)
        {
            if ((g_CurrentPlayer->watch_pause_time == 1) && (g_CurrentPlayer->field_21C != 0))
            {
                sndPlaySfx(g_musicSfxBufferPtr, WATCH_ON_SFX, NULL);
                g_CurrentPlayer->field_21C = 0;
            }

            bondviewZoomToWatchOnOpen();

            if (check_watch_page_transistion_running() != 0)
            {
                g_CurrentPlayer->pausing_flag = TRUE;
            }
            else
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x5;
                g_CurrentPlayer->watch_pause_time = 1;
                g_CurrentPlayer->timer_1C4 = 0;
                g_CurrentPlayer->field_21C = 1;
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5)
        {
            if (g_CurrentPlayer->watch_pause_time == 1)
            {
                sub_GAME_7F0C1310();
            }

            g_CurrentPlayer->pausing_flag = TRUE;
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xc)
        {
            g_CurrentPlayer->pausing_flag = TRUE;

            if (g_CurrentPlayer->watch_pause_time >= 3)
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x6;
                g_CurrentPlayer->watch_pause_time = 1;
                g_CurrentPlayer->timer_1C4 = 0;
                sndPlaySfx(g_musicSfxBufferPtr, WATCH_OFF_SFX, NULL);
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x6)
        {
            if (g_CurrentPlayer->watch_pause_time == 1)
            {
                bondviewZoomFromWatchOnExit();
                if (sp34)
                {
                    draw_item_in_hand(GUNRIGHT, ITEM_UNARMED);
                }
                else
                {
                    sub_GAME_7F05DAE4(0);
                }
            }
            if (check_watch_page_transistion_running())
            {
                g_CurrentPlayer->pausing_flag = TRUE;
            }
            else
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x7;
                g_CurrentPlayer->watch_pause_time = 1;
                g_CurrentPlayer->timer_1C4 = 0;
                g_CurrentPlayer->field_21C = 1;
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x7)
        {
            if (g_CurrentPlayer->watch_pause_time == 1)
            {
                sp28 = 40.0f;
                sp24 = bondviewSetupPauseTransition(FALSE);

                if ((g_CurrentPlayer->step_in_view_watch_animation != 0) && (g_CurrentPlayer->step_in_view_watch_animation != 3))
                {
                    sp28 = (WATCH_MODEL(g_CurrentPlayer)->animframe1 * 40.0f) / 20.0f;
                }

                sp20 = sp28 + 20.0f;

                if (sp24 < sp20)
                {
                    sp24 = sp20;
                }

                bondviewStartUnpauseTransition(sp24);
                bondviewSetPauseWatchRelatedAlt(sp28);
            }

            if ((g_CurrentPlayer->step_in_view_watch_animation != 0) && (g_CurrentPlayer->step_in_view_watch_animation != 3))
            {
                g_CurrentPlayer->pausing_flag = TRUE;
            }
            else
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x8;
                g_CurrentPlayer->watch_pause_time = 1;
                g_CurrentPlayer->timer_1C4 = 0;
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x8)
        {
            g_CurrentPlayer->pausing_flag = FALSE;

            if (g_CurrentPlayer->watch_pause_time == 1)
            {

                if ((get_item_in_hand_or_watch_menu(GUNLEFT) != getCurrentPlayerWeaponId(GUNLEFT)) && (Gun_hand_without_item(GUNLEFT) != ITEM_UNARMED))
                {
                    draw_item_in_hand(GUNLEFT, getCurrentPlayerWeaponId(GUNLEFT));
                }

                if (sp3c)
                {
                    if ((get_item_in_hand_or_watch_menu(GUNRIGHT) != getCurrentPlayerWeaponId(GUNRIGHT)) && (Gun_hand_without_item(GUNRIGHT) != ITEM_UNARMED))
                    {
                        draw_item_in_hand(GUNRIGHT, getCurrentPlayerWeaponId(GUNRIGHT));
                    }
                }
            }
            else if (!bondViewIsPauseTransitioning())
            {
                if (
                    (get_item_in_hand_or_watch_menu(GUNLEFT) == getCurrentPlayerWeaponId(GUNLEFT))
                    && (Gun_hand_without_item(GUNLEFT) != ITEM_UNARMED)
                    && (
                        (sp3c == 0)
                        || (((get_item_in_hand_or_watch_menu(GUNRIGHT) == getCurrentPlayerWeaponId(GUNRIGHT))) && (Gun_hand_without_item(GUNRIGHT) != ITEM_UNARMED))))
                {
                    sub_GAME_7F05DAE4(1);

                    if (sp3c)
                    {
                        sub_GAME_7F05DAE4(0);
                    }

                    gunSetSightVisible(GUNAMMOREASON_DAMAGE, TRUE);
                    gunSetGunAmmoVisible(GUNSIGHTREASON_NOCONTROL, TRUE);
                    hudmsgsSetOn(1);
                    bondviewClearUpperTextDisplayFlag(1);
                    countdownTimerSetVisible(4, 1);

                    g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x0;
                    g_CurrentPlayer->watch_pause_time = 0;
                    g_CurrentPlayer->timer_1C4 = 0;
                }
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x9)
        {
            g_CurrentPlayer->pausing_flag = FALSE;

            if (
                (getCurrentPlayerWeaponId(GUNLEFT) != get_item_in_hand_or_watch_menu(GUNLEFT))
                && (Gun_hand_without_item(GUNLEFT) != ITEM_UNARMED)
                && (
                    (sp3c == 0)
                    || (((getCurrentPlayerWeaponId(GUNRIGHT) != get_item_in_hand_or_watch_menu(GUNRIGHT))) && (Gun_hand_without_item(GUNRIGHT) != ITEM_UNARMED))))
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x8;
                g_CurrentPlayer->watch_pause_time = 0;
                g_CurrentPlayer->timer_1C4 = 0;
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xa)
        {
            g_CurrentPlayer->pausing_flag = FALSE;

            if (g_CurrentPlayer->watch_pause_time == 1)
            {
                bondviewStartUnpauseTransition(bondviewSetupPauseTransition(FALSE));
            }

            if (
                (getCurrentPlayerWeaponId(GUNLEFT) != get_item_in_hand_or_watch_menu(GUNLEFT))
                && (Gun_hand_without_item(GUNLEFT) != ITEM_UNARMED)
                && (
                    (sp3c == 0)
                    || (((getCurrentPlayerWeaponId(GUNRIGHT) != get_item_in_hand_or_watch_menu(GUNRIGHT))) && (Gun_hand_without_item(GUNRIGHT) != ITEM_UNARMED))))
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x8;
                g_CurrentPlayer->watch_pause_time = 0;
                g_CurrentPlayer->timer_1C4 = 0;
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xb)
        {
            g_CurrentPlayer->pausing_flag = FALSE;

            if (
                (getCurrentPlayerWeaponId(GUNLEFT) == get_item_in_hand_or_watch_menu(GUNLEFT))
                && (Gun_hand_without_item(GUNLEFT) != ITEM_UNARMED)
                && (
                    (sp3c == 0)
                    || (
                        ( (getCurrentPlayerWeaponId(GUNRIGHT) == get_item_in_hand_or_watch_menu(GUNRIGHT)))
                        && (Gun_hand_without_item(GUNRIGHT) != ITEM_UNARMED))))
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x1;
                g_CurrentPlayer->watch_pause_time = 0;
                g_CurrentPlayer->timer_1C4 = 0;
            }
        }

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xd)
        {
            g_CurrentPlayer->pausing_flag = FALSE;

            if (Gun_hand_without_item(GUNLEFT)
                && Gun_hand_without_item(GUNRIGHT))
            {
                if ((g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_LOWER)
                    && (g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_SWAP)
                    && (g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_HOLD)
                    && (g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_RAISE))
                {
                    g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x1;
                    g_CurrentPlayer->watch_pause_time = 0;
                    g_CurrentPlayer->timer_1C4 = 0;
                }
            }
        }

        bondviewUpdatePauseTransition();
        bondviewStepWatchAnimation();
        bondviewUpdateWatchZoomIn();
    }

    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5)
    {
        lvlSetControlsLockedFlag(TRUE);
        sub_GAME_7F0A6A80();
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xc)
    {
        lvlSetControlsLockedFlag(TRUE);
    }
    else
    {
        lvlSetControlsLockedFlag(FALSE);
    }

#undef WATCH_VAR_LOWER
#undef WATCH_VAR_UPPER
}


void set_open_close_solo_watch_menu_to1(void) 
{
    g_CurrentPlayer->open_close_solo_watch_menu = TRUE;
}


/**
 * US address 7F07F874.
 * EU address 7F07F918.
*/
void trigger_solo_watch_menu(s32 arg0)
{
    struct WatchVertex *ptr_a;
    Gfx *ptr_b;
    struct WatchVertex *next;
    struct WatchVertex *ptr_copy;
    s32 i;

    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
    {
        if (arg0 == 0)
        {
            watch_transition_time *= 1.1f;
            if (watch_transition_time > 1.7f)
            {
                watch_transition_time = 1.7f;
            }

            if ((Gun_hand_without_item(GUNLEFT) != ITEM_UNARMED)
                && (Gun_hand_without_item(GUNRIGHT) != ITEM_UNARMED)
                && (g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_LOWER)
                && (g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_SWAP)
                && (g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_HOLD)
                && (g_CurrentPlayer->hands[GUNLEFT].weapon_action_state != GUN_ANIM_STATE_SWITCH_RAISE))
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x1;
            }
            else
            {
                g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0xd;
            }

            g_CurrentPlayer->watch_pause_time = 0;
            g_CurrentPlayer->timer_1C4 = 0;

            bondviewInitPauseTransition();
            bondviewTriggerWatchZoomDefault();

            hudMakeDamageSegments(&g_CurrentPlayer->armor_display_values, 23*2, 1, currentPlayerGetArmor());
            buildGaugeBarDL(&g_CurrentPlayer->watch_body_armor_bar_gdl, OS_K0_TO_PHYSICAL(&g_CurrentPlayer->armor_display_values), 0x2E);

            hudMakeDamageSegments(&g_CurrentPlayer->health_display_values, 23*2, -1, currentPlayerGetHealth());
            buildGaugeBarDL(&g_CurrentPlayer->watch_health_bar_gdl, OS_K0_TO_PHYSICAL(&g_CurrentPlayer->health_display_values), 0x2E);

            sub_GAME_7F0A69A8();

            /**
             * This section is for rendering the selected screen rectangles.
            */
            ptr_b = g_CurrentPlayer->buffer_for_watch_greenbackdrop_DL; // Gfx
            ptr_a = &g_CurrentPlayer->buffer_for_watch_greenbackdrop_vertices->vtx[0]; // struct WatchRectangle

            for (i=0;
                i<(WATCH_NUMBER_SCREENS * WATCH_SCREEN_SELECT_RECTANGLE_HSTEP);
                i+=WATCH_SCREEN_SELECT_RECTANGLE_HSTEP)
            {
                // Note: colors are set here but overwritten in watch.c set_page_rectangle_colors
                ptr_copy = ptr_a;
                ptr_a = setup_watch_rectangles(ptr_a, i, 0, 0x64, 0x14, -0x12B, 0x136);
                ptr_b = sub_GAME_7F0A3B40(ptr_b, OS_K0_TO_PHYSICAL(ptr_copy));
            }

            gSPEndDisplayList(ptr_b);
            /**
             * End watch screen select rectangles.
            */

            /**
             * This section is related to rendering static on the watch menu.
             * Static is defined by a horizontal bar in the middle of the screen.
            */
            ptr_a = &g_CurrentPlayer->buffer_for_watch_static_vertices->vtx[0]; // struct WatchRectangle
            ptr_b = g_CurrentPlayer->buffer_for_watch_static_DL; // Gfx

            ptr_copy = &g_CurrentPlayer->buffer_for_watch_static_vertices->vtx[0];
            next = setup_watch_rectangles(ptr_a, 0, 0, 0x398, 0x14, -0x1CC, 0);
            ptr_b = sub_GAME_7F0A3B40(ptr_b, OS_K0_TO_PHYSICAL(ptr_copy));

            gSPEndDisplayList(ptr_b);
            /**
             * End watch static section.
            */
        }
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x1)
    {
        g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x9;
        g_CurrentPlayer->watch_pause_time = 0;
        g_CurrentPlayer->timer_1C4 = 0;
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x2)
    {
        g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0xa;
        g_CurrentPlayer->watch_pause_time = 0;
        g_CurrentPlayer->timer_1C4 = 0;
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x3)
    {
        g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x7;
        g_CurrentPlayer->watch_pause_time = 0;
        g_CurrentPlayer->timer_1C4 = 0;
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x4)
    {
        g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x6;
        g_CurrentPlayer->watch_pause_time = 0;
        g_CurrentPlayer->timer_1C4 = 0;
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5)
    {
        deleteCurrentSelectedFolder();
        sub_GAME_7F0C1340();
        g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0xc;
        g_CurrentPlayer->watch_pause_time = 0;
        g_CurrentPlayer->timer_1C4 = 0;
        g_CurrentPlayer->open_close_solo_watch_menu = FALSE;
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xc)
    {
        // removed
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x6)
    {
        if (arg0 == 0)
        {
            g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x4;
            g_CurrentPlayer->watch_pause_time = 0;
            g_CurrentPlayer->timer_1C4 = 0;
            sub_GAME_7F0A69A8();
        }
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x7)
    {
        if (arg0 == 0)
        {
            g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x3;
            g_CurrentPlayer->watch_pause_time = 0;
            g_CurrentPlayer->timer_1C4 = 0;
            sub_GAME_7F0A69A8();
        }
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x8)
    {
        if (arg0 == 0)
        {
            g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0xb;
            g_CurrentPlayer->watch_pause_time = 0;
            g_CurrentPlayer->timer_1C4 = 0;
            sub_GAME_7F0A69A8();
        }
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x9)
    {
        if (arg0 == 0)
        {
            g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x1;
            g_CurrentPlayer->watch_pause_time = 0;
            g_CurrentPlayer->timer_1C4 = 0;
        }
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xa)
    {
        if (arg0 == 0)
        {
            g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x1;
            g_CurrentPlayer->watch_pause_time = 0;
            g_CurrentPlayer->timer_1C4 = 0;
        }
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xb)
    {
        g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x8;
        g_CurrentPlayer->watch_pause_time = 0;
        g_CurrentPlayer->timer_1C4 = 0;
    }
    else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xd)
    {
        g_CurrentPlayer->watch_animation_state = WATCH_ANIMATION_0x0;
    }
}

/**
 * US address 7F07FCC4.
 * Perfect Dark bwalkUpdateSpeedSideways.
*/
void bondviewUpdateSpeedSideways(s32 arg0) {
    if (arg0 == -1) {
        g_CurrentPlayer->speedstrafe = (g_CurrentPlayer->speedstrafe - g_GlobalTimerDelta);
        if (g_CurrentPlayer->speedstrafe < -1.0f) {
            g_CurrentPlayer->speedstrafe = -1.0f;
        }
    }
    else {
        if (arg0 == 1) {
            g_CurrentPlayer->speedstrafe = (g_CurrentPlayer->speedstrafe + g_GlobalTimerDelta);
            if (1.0f < g_CurrentPlayer->speedstrafe) {
                g_CurrentPlayer->speedstrafe = 1.0f;
            }
        } else {
            if (0.0f < g_CurrentPlayer->speedstrafe) {
                g_CurrentPlayer->speedstrafe = (g_CurrentPlayer->speedstrafe - g_GlobalTimerDelta);
                if (g_CurrentPlayer->speedstrafe < 0.0f) {
                    g_CurrentPlayer->speedstrafe = 0.0f;
                }
            } else {
                g_CurrentPlayer->speedstrafe = (g_CurrentPlayer->speedstrafe + g_GlobalTimerDelta);
                if (0.0f < g_CurrentPlayer->speedstrafe) {
                    g_CurrentPlayer->speedstrafe = 0.0f;
                }
            }
        }
    }
    g_CurrentPlayer->speedsideways = g_CurrentPlayer->speedstrafe;
}

/**
 * US address 7F07FE1C.
 * Perfect Dark bwalkUpdateSpeedForwards.
*/
void bondviewUpdateSpeedForwards(s32 arg0) {
    if (arg0 == 1) {
        g_CurrentPlayer->speedgo = (g_CurrentPlayer->speedgo + g_GlobalTimerDelta);
        if (1.0f < g_CurrentPlayer->speedgo) {
            g_CurrentPlayer->speedgo = 1.0f;
        }
    } else {
        if (arg0 == -1) {
            g_CurrentPlayer->speedgo = (g_CurrentPlayer->speedgo - g_GlobalTimerDelta);
            if (g_CurrentPlayer->speedgo < -1.0f) {
                g_CurrentPlayer->speedgo = -1.0f;
            }
        } else {
            if (0.0f < g_CurrentPlayer->speedgo) {
                g_CurrentPlayer->speedgo = (g_CurrentPlayer->speedgo - g_GlobalTimerDelta);
                if (g_CurrentPlayer->speedgo < 0.0f) {
                    g_CurrentPlayer->speedgo = 0.0f;
                }
            } else {
                g_CurrentPlayer->speedgo = (g_CurrentPlayer->speedgo + g_GlobalTimerDelta);
                if (0.0f < g_CurrentPlayer->speedgo) {
                    g_CurrentPlayer->speedgo = 0.0f;
                }
            }
        }
    }
    g_CurrentPlayer->speedforwards = g_CurrentPlayer->speedgo;
}

/* LOOK SPEED vs FOV: vanilla scales look speed by viGetFovY() / 60 so zooming
 * slows the view, but a higher midas.ini fov then read as "zoomed out" and made
 * look too fast. Dividing by GE_DEFAULT_FOV (60 on N64, the ini fov on PC)
 * keeps unzoomed feel FOV-independent while a real zoom still slows it. */

/**
 * US address 7F07FF74.
 * Duplicate of sub_GAME_7F080228.
*/
f32 sub_GAME_7F07FF74(f32 value) {
    if (value > 0) {
        return (viGetFovY() * value * -0.7f) / GE_DEFAULT_FOV; /* base fov, not 60 */
    }

    if (value < 0) {
        return (viGetFovY() * -value * 0.7f) / GE_DEFAULT_FOV;
    }

    return 0;
}


/**
 * Address 0x7F080010.
 */
void bondviewCurrentPlayerUpdateSpeedVerta(f32 value)
{
    f32 mult = viGetFovY() / GE_DEFAULT_FOV; /* base fov, not 60 */
    f32 limit = sub_GAME_7F07FF74(value);

    if (value > 0.0f)
    {
        if (g_CurrentPlayer->speedverta > 0.0f)
        {
            g_CurrentPlayer->speedverta -= (0.05f * g_GlobalTimerDelta * mult);
        }
        else
        {
            g_CurrentPlayer->speedverta -= (0.0125f * g_GlobalTimerDelta * mult);
        }

        if (g_CurrentPlayer->speedverta < limit)
        {
            g_CurrentPlayer->speedverta = limit;
        }
    }
    else if (value < 0.0f)
    {
        if (g_CurrentPlayer->speedverta < 0.0f)
        {
            g_CurrentPlayer->speedverta += (0.05f * g_GlobalTimerDelta * mult);
        }
        else
        {
            g_CurrentPlayer->speedverta += (0.0125f * g_GlobalTimerDelta * mult);
        }

        if (g_CurrentPlayer->speedverta > limit)
        {
            g_CurrentPlayer->speedverta = limit;
        }
    }
    else
    {
        if (g_CurrentPlayer->speedverta > limit)
        {
            g_CurrentPlayer->speedverta -= (0.05f * g_GlobalTimerDelta * mult);

            if (g_CurrentPlayer->speedverta < limit)
            {
                g_CurrentPlayer->speedverta = limit;
            }
        }
        else
        {
            g_CurrentPlayer->speedverta += (0.05f * g_GlobalTimerDelta * mult);

            if (g_CurrentPlayer->speedverta > limit)
            {
                g_CurrentPlayer->speedverta = limit;
            }
        }
    }
}



/**
 * US address 7F080228.
 * Duplicate of sub_GAME_7F07FF74.
*/
f32 sub_GAME_7F080228(f32 arg0) {
    if (0.0f < arg0) {
        return (viGetFovY() * arg0 * -0.7f) / GE_DEFAULT_FOV; /* base fov, not 60 */
    } else if (arg0 < 0.0f) {
        return (viGetFovY() * -arg0 * 0.7f) / GE_DEFAULT_FOV;
    } else {
        return 0.0f;
    }
}

/**
 * Address 0x7F0802C4.
 */
void bondviewCurrentPlayerUpdateSpeedTheta(f32 value)
{
    f32 mult = viGetFovY() / GE_DEFAULT_FOV; /* base fov, not 60 */
    f32 limit = sub_GAME_7F080228(value);

    if (value > 0.0f)
    {
        if (g_CurrentPlayer->speedtheta > 0.0f)
        {
            g_CurrentPlayer->speedtheta -= (0.05f * g_GlobalTimerDelta * mult);
        }
        else
        {
            g_CurrentPlayer->speedtheta -= (0.0125f * g_GlobalTimerDelta * mult);
        }

        if (g_CurrentPlayer->speedtheta < limit)
        {
            g_CurrentPlayer->speedtheta = limit;
        }
    }
    else if (value < 0.0f)
    {
        if (g_CurrentPlayer->speedtheta < 0)
        {
            g_CurrentPlayer->speedtheta += (0.05f * g_GlobalTimerDelta * mult);
        }
        else
        {
            g_CurrentPlayer->speedtheta += (0.0125f * g_GlobalTimerDelta * mult);
        }

        if (limit < g_CurrentPlayer->speedtheta)
        {
            g_CurrentPlayer->speedtheta = limit;
        }
    }
    else
    {
        if (limit < g_CurrentPlayer->speedtheta)
        {
            g_CurrentPlayer->speedtheta -= (0.05f * g_GlobalTimerDelta * mult);

            if (g_CurrentPlayer->speedtheta < limit)
            {
                g_CurrentPlayer->speedtheta = limit;
            }
        }
        else
        {
            g_CurrentPlayer->speedtheta += (0.05f * g_GlobalTimerDelta * mult);

            if (limit < g_CurrentPlayer->speedtheta)
            {
                g_CurrentPlayer->speedtheta = limit;
            }
        }
    }
}


Gfx *currentPlayerDrawFade(Gfx *gdl)
{
    f32 frac = g_CurrentPlayer->colourscreenfrac;
    s32 r = g_CurrentPlayer->colourscreenred;
    s32 g = g_CurrentPlayer->colourscreengreen;
    s32 b = g_CurrentPlayer->colourscreenblue;
    if ((cameraFrameCounter1 != 0) || (cameraFrameCounter2 != 0)) {
        frac = 1.0f;
        b = 0;
        g = 0;
        r = 0;
    }
    if (frac > 0) {
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetColorDither(gdl++, G_CD_DISABLE);
        gDPSetTexturePersp(gdl++, G_TP_NONE);
        gDPSetAlphaCompare(gdl++, G_AC_NONE);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetTextureFilter(gdl++, G_TF_BILERP);
        gDPSetTextureConvert(gdl++, G_TC_FILT);
        gDPSetTextureLUT(gdl++, G_TT_NONE);
        gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
        gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetPrimColor(gdl++, 0, 0, r, g, b, (s32)(frac * 255.0f));
        gDPFillRectangle(gdl++, viGetViewLeft(), viGetViewTop(), (viGetViewLeft() + viGetViewWidth()), (viGetViewTop() + viGetViewHeight()));
        gDPPipeSync(gdl++);
        gDPSetColorDither(gdl++, G_CD_BAYER);
        gDPSetTexturePersp(gdl++, G_TP_PERSP);
        gDPSetTextureLOD(gdl++, G_TL_LOD);
    }

    return gdl;
}

void currentPlayerSetFadeColour(s32 r, s32 g, s32 b, f32 frac) {
    g_CurrentPlayer->colourscreenred = r;
    g_CurrentPlayer->colourscreengreen = g;
    g_CurrentPlayer->colourscreenblue = b;
    g_CurrentPlayer->colourscreenfrac = frac;
}

void currentPlayerAdjustFade(f32 maxfadetime, s32 r, s32 g, s32 b, f32 frac)
{
    g_CurrentPlayer->colourfadetime60    = 0;
    g_CurrentPlayer->colourfadetimemax60 = maxfadetime;
    g_CurrentPlayer->colourfaderedold    = g_CurrentPlayer->colourscreenred;
    g_CurrentPlayer->colourfaderednew    = r;
    g_CurrentPlayer->colourfadegreenold  = g_CurrentPlayer->colourscreengreen;
    g_CurrentPlayer->colourfadegreennew  = g;
    g_CurrentPlayer->colourfadeblueold   = g_CurrentPlayer->colourscreenblue;
    g_CurrentPlayer->colourfadebluenew   = b;
    g_CurrentPlayer->colourfadefracold   = g_CurrentPlayer->colourscreenfrac;
    g_CurrentPlayer->colourfadefracnew   = frac;
}

void currentPlayerSetFadeFrac(f32 maxfadetime, f32 frac)
{
    currentPlayerAdjustFade(maxfadetime, g_CurrentPlayer->colourscreenred, g_CurrentPlayer->colourscreengreen, g_CurrentPlayer->colourscreenblue, frac);
}

bool currentPlayerIsFadeComplete(void)
{
	return g_CurrentPlayer->colourfadetimemax60 < 0;
}

void currentPlayerUpdateColourScreenProperties(void)
{
    if (g_CurrentPlayer->colourfadetimemax60 >= 0)
    {
        g_CurrentPlayer->colourfadetime60 += g_GlobalTimerDelta;

        if (g_CurrentPlayer->colourfadetime60 < g_CurrentPlayer->colourfadetimemax60)
        {
            f32 mult                           = g_CurrentPlayer->colourfadetime60 / g_CurrentPlayer->colourfadetimemax60;
            g_CurrentPlayer->colourscreenfrac  = g_CurrentPlayer->colourfadefracold + (g_CurrentPlayer->colourfadefracnew - g_CurrentPlayer->colourfadefracold) * mult;
            g_CurrentPlayer->colourscreenred   = g_CurrentPlayer->colourfaderedold + (s32)((g_CurrentPlayer->colourfaderednew - g_CurrentPlayer->colourfaderedold) * mult);
            g_CurrentPlayer->colourscreengreen = g_CurrentPlayer->colourfadegreenold + (s32)((g_CurrentPlayer->colourfadegreennew - g_CurrentPlayer->colourfadegreenold) * mult);
            g_CurrentPlayer->colourscreenblue  = g_CurrentPlayer->colourfadeblueold + (s32)((g_CurrentPlayer->colourfadebluenew - g_CurrentPlayer->colourfadeblueold) * mult);
            return;
        }

        g_CurrentPlayer->colourscreenfrac    = g_CurrentPlayer->colourfadefracnew;
        g_CurrentPlayer->colourscreenred     = g_CurrentPlayer->colourfaderednew;
        g_CurrentPlayer->colourscreengreen   = g_CurrentPlayer->colourfadegreennew;
        g_CurrentPlayer->colourscreenblue    = g_CurrentPlayer->colourfadebluenew;
        g_CurrentPlayer->colourfadetimemax60 = -1;
    }
}

void currentPlayerStartChrFade(f32 duration60, f32 targetfrac)
{
    ChrRecord *chr = g_CurrentPlayer->prop->chr;

    if (chr)
    {
        g_CurrentPlayer->bondfadetime60    = 0;
        g_CurrentPlayer->bondfadetimemax60 = duration60;
        g_CurrentPlayer->bondfadefracold   = chr->fadealpha / 255.0f;
        g_CurrentPlayer->bondfadefracnew   = targetfrac;
    }
}

void currentPlayerTickChrFade(void)
{
    if (g_CurrentPlayer->bondfadetimemax60 >= 0)
    {
        ChrRecord *chr = g_CurrentPlayer->prop->chr;
        f32        frac;

        g_CurrentPlayer->bondfadetime60 += g_GlobalTimerDelta;

        if (g_CurrentPlayer->bondfadetime60 < g_CurrentPlayer->bondfadetimemax60)
        {
            frac = g_CurrentPlayer->bondfadefracold + (g_CurrentPlayer->bondfadefracnew - g_CurrentPlayer->bondfadefracold) * g_CurrentPlayer->bondfadetime60 / g_CurrentPlayer->bondfadetimemax60;
        }
        else
        {
            frac = g_CurrentPlayer->bondfadefracnew;
            g_CurrentPlayer->bondfadetimemax60 = -1;
        }

        if (chr)
        {
            chr->fadealpha = (s8)(frac * 255);
        }
    }
}

/**
 * Will apply a move animation update. The pass through call to bheadUpdate is
 * what allows Bond to move. This will also trigger the death animation once
 * Bond dies. This chooses a random death animation from g_bondviewBondDeathAnimations.
 * Address 0x7F080B34.
*/
void bondviewMoveAnimationTick(f32 speed, f32 speedforwards, f32 speedsideways)
{
    f32 percent_speed;
    Mtxf sp8C;
    Mtxf sp4C;

    percent_speed = 0.0f;

    if (g_CurrentPlayer->bonddead == 0)
    {
        bheadAdjustAnimation(speed);

        if (speed != 0.0f)
        {
            percent_speed = speedforwards / speed;
        }
        else if (speedforwards == 0.0f)
        {
            //
        }
    }
    else
    {
        if (g_CurrentPlayer->startnewbonddie)
        {
            // HACK: ptr_animation_table dereference addition is backwards.
            // this should be:
            // ptr_animation_table->data[g_bondviewBondDeathAnimations[((u32) randomGetNext() % (u32) g_bondviewBondDeathAnimationsCount)]]
            bheadStartDeathAnimation((struct ModelAnimation *) ((s32)g_bondviewBondDeathAnimations[((u32) randomGetNext() % (u32) g_bondviewBondDeathAnimationsCount)] + (intptr_t)&ptr_animation_table->data[0]), randomGetNext() & 1, 0.0f, 1.0f);
            g_CurrentPlayer->startnewbonddie = FALSE;
        }

        bheadSetSpeed(0.5f);
        speedsideways = 0.0f;
    }

    bheadUpdate(percent_speed, speedsideways);

#ifndef TARGET_N64
    { /* SCOPE STEADY: zooming changes Bond's stance and head posture, which
       * swings the camera slightly - invisible normally but a visible drift
       * ("clouds sliding along the sky") at 8x zoom. Fade the head-animation
       * basis out of the camera as the fov narrows (full head feel at >=50deg,
       * fully anchored to the aim at <=20deg). */
        /* OFF BY DEFAULT: the fade also removes head sway when zoomed, which
         * the owner prefers to keep; midas.ini [display] scope_steady=1 enables it. */
        extern s32 portVideoScopeSteady(void);
        /* off during demo playback - it moves the camera the shot
         * ray is cast from */
        f32 zoomk = (portVideoScopeSteady() && get_is_ramrom_flag() == 0) ? ((viGetFovY() - 20.0f) * (1.0f / 30.0f)) : 1.0f;
        if (zoomk < 0.0f) zoomk = 0.0f;
        if (zoomk > 1.0f) zoomk = 1.0f;
        if (zoomk < 1.0f)
        {
            g_CurrentPlayer->headlook.f[0] *= zoomk;
            g_CurrentPlayer->headlook.f[1] *= zoomk;
            g_CurrentPlayer->headlook.f[2] = (g_CurrentPlayer->headlook.f[2] * zoomk) + (1.0f - zoomk);
            g_CurrentPlayer->headup.f[0] *= zoomk;
            g_CurrentPlayer->headup.f[1] = (g_CurrentPlayer->headup.f[1] * zoomk) + (1.0f - zoomk);
            g_CurrentPlayer->headup.f[2] *= zoomk;
        }
    }

    { /* NO HEAD ROLL (midas.ini [display] no_head_roll): keep the head
       * animation's look direction (bob and sway stay) but rebuild headup from
       * world up minus its along-look part (Gram-Schmidt), so the camera never
       * tilts sideways, at any zoom; degenerate looks are left alone. */
        extern s32 portNoHeadRoll(void); /* video.c: midas.ini [display] no_head_roll; weak 0 headless */

        if (portNoHeadRoll() && get_is_ramrom_flag() == 0) /* not in demo playback */
        {
            f32 lx = g_CurrentPlayer->headlook.f[0];
            f32 ly = g_CurrentPlayer->headlook.f[1];
            f32 lz = g_CurrentPlayer->headlook.f[2];
            f32 ll = lx * lx + ly * ly + lz * lz;

            if (ll > 0.0001f)
            {
                f32 d = ly / ll; /* dot(up, look) / |look|^2 */
                f32 ux = -d * lx;
                f32 uy = 1.0f - d * ly;
                f32 uz = -d * lz;
                f32 len = sqrtf(ux * ux + uy * uy + uz * uz);

                if (len > 0.0001f)
                {
                    g_CurrentPlayer->headup.f[0] = ux / len;
                    g_CurrentPlayer->headup.f[1] = uy / len;
                    g_CurrentPlayer->headup.f[2] = uz / len;
                }
            }
        }
    }
#endif

    matrix_4x4_set_rotation_around_x((360.0f - g_CurrentPlayer->vv_verta360) * DegToRad1Fact(1), &sp8C);
    matrix_4x4_set_basis_and_position_target(&sp4C, 0.0f, 0.0f, 0.0f, -g_CurrentPlayer->headlook.f[0], -g_CurrentPlayer->headlook.f[1], -g_CurrentPlayer->headlook.f[2], g_CurrentPlayer->headup.f[0], g_CurrentPlayer->headup.f[1], g_CurrentPlayer->headup.f[2]);
    matrix_4x4_multiply_in_place(&sp4C, &sp8C);
    matrix_4x4_set_rotation_around_y((360.0f - g_CurrentPlayer->vv_theta) * DegToRad1Fact(1), &sp4C);
    matrix_4x4_multiply_in_place(&sp4C, &sp8C);

    g_CurrentPlayer->field_488.applied_view.f[0] = sp8C.m[2][0];
    g_CurrentPlayer->field_488.applied_view.f[1] = sp8C.m[2][1];
    g_CurrentPlayer->field_488.applied_view.f[2] = sp8C.m[2][2];

    g_CurrentPlayer->field_488.applied_view2.f[0] = sp8C.m[1][0];
    g_CurrentPlayer->field_488.applied_view2.f[1] = sp8C.m[1][1];
    g_CurrentPlayer->field_488.applied_view2.f[2] = sp8C.m[1][2];

    // Bug Fix: Invisibility cheat had no effect in multiplayer since there
    // are no guards to hide from - fade Bond's own model out instead, the
    // same way the BondPhase cheat does, so opponents can't see the player.
    if (getPlayerCount() >= 2 && cheatIsActive(CHEAT_INVISIBILITY))
    {
        g_CurrentPlayer->bondfadetime60 = 0.0f;
        g_CurrentPlayer->bondfadetimemax60 = 1.0f;
        g_CurrentPlayer->bondfadefracold = 0.05f;
        g_CurrentPlayer->bondfadefracnew = 0.05f;
        currentPlayerTickChrFade();
    }
}


/**
 * Address 0x7F080D60.
 */
f32 bondviewYPositionRelated(StandTile *arg0, f32 arg1, f32 arg2)
{
    f32 ret;

    if (g_PlayerTankProp != NULL)
    {
        ObjectRecord * obj = ((PropRecord *)g_PlayerTankProp)->obj;
        PropRecord *p = obj->prop;

        ret = stanGetPositionYValue(p->stan, p->pos.x, p->pos.z);

        ret += g_PlayerTankYOffset;
    }
    else
    {
        if (g_CurrentPlayer->field_2A6C)
        {
            ret = stanGetPositionYValue(g_CurrentPlayer->field_2A70, arg1, arg2);
        }
        else
        {
            ret = stanGetPositionYValue(arg0, arg1, arg2);
        }
    }

    return ret;
}



/**
 * US Address 0x7F080DF8.
 * EU Address 0x7F080E9C.
 */
void bondviewUpdatePlayerY(s32 use_stanHeight, f32 stanHeight_offset)
{
    s32 i; // sp6c
    f32 unused;
    f32 sp64;
    StandTile *stan; //sp60
    f32 collision_radius; //sp5c
    f32 height; //sp58
    f32 always_30; //sp54
    f32 temp_f0; // no stack
    f32 new_field_70; // sp4c
    f32 new_field_7c; //sp48
    f32 ftemp2;
    f32 sp40;

    if (1);

    if (g_PlayerIsInTank == 1)
    {
        g_CurrentPlayer->stanHeight = bondviewYPositionRelated(
            g_CurrentPlayer->field_488.current_tile_ptr,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2]);

        g_CurrentPlayer->field_6C = g_CurrentPlayer->field_70 / (1.0f - TANK_UNKD0_SCALE);

#ifndef TARGET_N64
        if (portSmoothOn())
        {
            portLagStep(&g_CurrentPlayer->field_6C, TANK_UNKD0_SCALE, g_CurrentPlayer->stanHeight, g_GlobalTimerDelta);
        }
        else
#endif
        for (i=0; i<g_ClockTimer; i++)
        {
            g_CurrentPlayer->field_6C = (g_CurrentPlayer->field_6C * TANK_UNKD0_SCALE) + g_CurrentPlayer->stanHeight;
        }

        g_CurrentPlayer->field_70 = g_CurrentPlayer->field_6C * (1.0f - TANK_UNKD0_SCALE);
    }
    else
    {
        if (use_stanHeight != 0)
        {
            g_CurrentPlayer->stanHeight = g_CurrentPlayer->stanHeight + stanHeight_offset;

            temp_f0 = bondviewYPositionRelated(
                g_CurrentPlayer->field_488.current_tile_ptr,
                g_CurrentPlayer->field_488.collision_position.f[0],
                g_CurrentPlayer->field_488.collision_position.f[2]);

            if (g_CurrentPlayer->stanHeight < temp_f0)
            {
                g_CurrentPlayer->stanHeight = temp_f0;
            }
        }
        else
        {
            stan = g_CurrentPlayer->field_488.current_tile_ptr;

            bondviewGetCollisionRadius(g_CurrentPlayer->prop, &collision_radius, &height, &always_30);

            sp64 = bondviewYPositionRelated(
                g_CurrentPlayer->field_488.current_tile_ptr,
                g_CurrentPlayer->field_488.collision_position.f[0],
                g_CurrentPlayer->field_488.collision_position.f[2]);

            // Another error checking block, it seems this condition is almost never triggered in the game.
            if (stanTestLocusEdgeAboveY(
                &stan,
                g_CurrentPlayer->field_488.collision_position.f[0],
                g_CurrentPlayer->field_488.collision_position.f[2],
                collision_radius,
                bondviewGetPlayerDuckingHeightRelated(g_CurrentPlayer) + sp64) >= 0)
            {
                if (sp64 < g_CurrentPlayer->stanHeight)
                {
                    sp64 = g_CurrentPlayer->stanHeight;
                }
            }

            g_CurrentPlayer->stanHeight = sp64;
        }

        if ((g_CurrentPlayer->field_2A6C != 0) && (g_CurrentPlayer->field_70 < g_CurrentPlayer->stanHeight))
        {
            g_CurrentPlayer->field_2A6C = 0;
            g_CurrentPlayer->field_488.current_tile_ptr = g_CurrentPlayer->field_2A70;
            g_CurrentPlayer->field_2A70 = NULL;
        }

        if ((g_CurrentPlayer->field_7C >= 0.0f) || (g_CurrentPlayer->field_70 < g_CurrentPlayer->stanHeight))
        {
            g_CurrentPlayer->field_6C = g_CurrentPlayer->field_70 / (1.0f - TANK_UNKD0_SCALE);

#ifndef TARGET_N64
            if (portSmoothOn())
            {
                /* STAIRS: the eye height follows the floor through this lag,
                 * which only ran on 60Hz field ticks and made the camera jitter
                 * on stairs at high refresh rates. Advance by the true delta. */
                portLagStep(&g_CurrentPlayer->field_6C, TANK_UNKD0_SCALE, g_CurrentPlayer->stanHeight, g_GlobalTimerDelta);
            }
            else
#endif
            for (i=0; i<g_ClockTimer; i++)
            {
                g_CurrentPlayer->field_6C = (g_CurrentPlayer->field_6C * TANK_UNKD0_SCALE) + g_CurrentPlayer->stanHeight;
            }

            if (g_CurrentPlayer->field_70 < g_CurrentPlayer->stanHeight)
            {
                g_CurrentPlayer->field_70 = g_CurrentPlayer->field_6C * (1.0f - TANK_UNKD0_SCALE);
            }
        }

        if (g_CurrentPlayer->stanHeight < g_CurrentPlayer->field_70)
        {
            new_field_7c = g_CurrentPlayer->field_7C;
            new_field_70 = g_CurrentPlayer->field_70;

            if ((get_debug_fast_bond_flag() != 0) && (g_ForceBondMoveOffset.f[0] == 0.0f) && (g_ForceBondMoveOffset.f[2] == 0.0f))
            {
                sp40 = 1.388889f;
            }
            else
            {
                sp40 = 0.27777779f;
            }

            ftemp2 = new_field_7c - (g_GlobalTimerDelta * sp40);
            new_field_70 += (g_GlobalTimerDelta * (new_field_7c + ftemp2) * 0.5f);
            new_field_7c = ftemp2;

            if (new_field_70 < g_CurrentPlayer->stanHeight)
            {
                new_field_70 = g_CurrentPlayer->stanHeight;
                new_field_7c = -sqrtf((g_CurrentPlayer->field_7C * g_CurrentPlayer->field_7C) + (((2.0f * (g_CurrentPlayer->field_70 - g_CurrentPlayer->stanHeight) * 0.27777779f) / 60.0f) * 60.0f));


                if (g_CurrentPlayer->field_2A6C != 0)
                {
                    g_CurrentPlayer->field_2A6C = 0;
                    g_CurrentPlayer->field_488.current_tile_ptr = g_CurrentPlayer->field_2A70;
                    g_CurrentPlayer->field_2A70 = NULL;
                }
            }

            g_CurrentPlayer->field_70 = new_field_70;
            g_CurrentPlayer->field_7C = new_field_7c;
        }

        if ((g_CurrentPlayer->field_7C < 0.0f) && (g_CurrentPlayer->field_70 <= g_CurrentPlayer->stanHeight))
        {
            if (g_CurrentPlayer->field_7C < -13.333333f)
            {
                g_CurrentPlayer->field_8C = CLIPPING_FIELD8C_VALUE;
                g_CurrentPlayer->vertical_bounce_adjust = -90.0f;
            }
            else if (g_CurrentPlayer->field_7C < -5.0f)
            {
                g_CurrentPlayer->field_8C = CLIPPING_FIELD8C_VALUE;
                g_CurrentPlayer->vertical_bounce_adjust = ((-5.0f - g_CurrentPlayer->field_7C) * -90.0f) / 8.333333f;
            }

            g_CurrentPlayer->field_7C = 0.0f;
        }

        if (g_CurrentPlayer->field_2A6C != 0)
        {
            if (g_CurrentPlayer->field_70 + bondviewGetPlayerDuckingHeightRelated(g_CurrentPlayer)
                < stanGetPositionYValue(
                    g_CurrentPlayer->field_488.current_tile_ptr,
                    g_CurrentPlayer->field_488.collision_position.f[0],
                    g_CurrentPlayer->field_488.collision_position.f[2]))
            {
                g_CurrentPlayer->field_2A6C = 0;
                g_CurrentPlayer->field_488.current_tile_ptr = g_CurrentPlayer->field_2A70;
                g_CurrentPlayer->field_2A70 = NULL;
            }
        }
    }

#ifndef TARGET_N64
    if (portSmoothOn())
    {
        /* landing bounce: same filter, advanced by the true delta. The
         * hold countdown field_8C stays in whole fields; the release decay
         * of vertical_bounce_adjust scales by the delta. */
        f32 dt = g_GlobalTimerDelta;

        if (g_CurrentPlayer->field_8C > 0)
        {
            portLagStep(&g_CurrentPlayer->field_84, CLIPPING_CLOCK_FACTOR, g_CurrentPlayer->vertical_bounce_adjust, dt);
            g_CurrentPlayer->field_8C -= g_ClockTimer;

            if (g_CurrentPlayer->field_8C < 0)
            {
                g_CurrentPlayer->field_8C = 0;
            }
        }
        else
        {
            if (g_CurrentPlayer->vertical_bounce_adjust < 0.0f)
            {
                g_CurrentPlayer->vertical_bounce_adjust -= CLIPPING_FIELD90_VALUE * dt;

                if (0.0f <= g_CurrentPlayer->vertical_bounce_adjust)
                {
                    g_CurrentPlayer->vertical_bounce_adjust = 0.0f;
                }
            }

            portLagStep(&g_CurrentPlayer->field_84, CLIPPING_CLOCK_FACTOR, g_CurrentPlayer->vertical_bounce_adjust, dt);
        }
    }
    else
#endif
    for (i=0; i<g_ClockTimer; i++)
    {
        if (g_CurrentPlayer->field_8C > 0)
        {
            g_CurrentPlayer->field_84 = (g_CurrentPlayer->field_84 * CLIPPING_CLOCK_FACTOR) + g_CurrentPlayer->vertical_bounce_adjust;
            g_CurrentPlayer->field_8C += -1;
        }
        else
        {
            if (g_CurrentPlayer->vertical_bounce_adjust < 0.0f)
            {
                g_CurrentPlayer->vertical_bounce_adjust -= CLIPPING_FIELD90_VALUE;

                if (0.0f <= g_CurrentPlayer->vertical_bounce_adjust)
                {
                    g_CurrentPlayer->vertical_bounce_adjust = 0.0f;
                }
            }

            g_CurrentPlayer->field_84 = (g_CurrentPlayer->field_84 * CLIPPING_CLOCK_FACTOR) + g_CurrentPlayer->vertical_bounce_adjust;
        }
    }

    g_CurrentPlayer->field_88 = g_CurrentPlayer->field_84 * CLIPPING_FIELD88_FACTOR;
}





/**
 * Address 0x7F081478 (NTSC).
 * Address 0x7F08151C (PAL).
*/
void bondviewUpdatePlayerCollisionPositionFields(void)
{
    f32 phi_f0;
    s32 i;
    StandTile *sp2C;
    s32 sp28;

    g_CurrentPlayer->eyeheight = (g_CurrentPlayer->headpos.f[1] * g_playerPerm->player_perspective_height) + 7.0f;

    phi_f0 = g_CurrentPlayer->eyeheight +
        ((g_CurrentPlayer->field_88 + g_CurrentPlayer->ducking_height_offset) * g_playerPerm->player_perspective_height);

    if (phi_f0 < 30.0f)
    {
        phi_f0 = 30.0f;
    }

    g_CurrentPlayer->field_488.collision_position.f[1] = g_CurrentPlayer->field_70 + phi_f0;

    if (((g_CameraMode != CAMERAMODE_DEATH_CAM_SP) && (g_CameraMode != CAMERAMODE_DEATH_CAM_MP) && (g_CameraMode != CAMERAMODE_POSEND))
        || (g_CurrentPlayer->bodyModel == 0))
    {
        g_CurrentPlayer->field_488.pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
        g_CurrentPlayer->field_488.pos.f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
        g_CurrentPlayer->field_488.pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2];
    }

    if (g_CurrentPlayer->bonddead != FALSE)
    {
        if (g_CurrentPlayer->field_29C0 > 0.0f)
        {
            g_CurrentPlayer->field_29C0 -= 0.25f;

            if (g_CurrentPlayer->field_29C0 < 0.0f)
            {
                g_CurrentPlayer->field_29C0 = 0.0f;
            }
        }
    }

    if (g_CurrentPlayer->vv_verta < 0.0f)
    {
        g_CurrentPlayer->field_488.pos.f[1] += -(1.0f - g_CurrentPlayer->vv_cosverta) * g_CurrentPlayer->field_29C0;
    }

    sp2C = g_CurrentPlayer->field_488.current_tile_ptr;
    sp28 = stanlinelog_flag;
    stanlinelog_flag = 0;

    walkTilesBetweenPoints_NoCallback(
        &sp2C,
        g_CurrentPlayer->field_488.collision_position.f[0],
        g_CurrentPlayer->field_488.collision_position.f[2],
        g_CurrentPlayer->field_488.pos.f[0],
        g_CurrentPlayer->field_488.pos.f[2]);

    stanlinelog_flag = sp28;

    g_CurrentPlayer->field_488.current_tile_ptr_for_portals = sp2C;

    g_CurrentPlayer->field_488.pos3.f[0] = g_CurrentPlayer->field_488.pos.f[0];
    g_CurrentPlayer->field_488.pos3.f[2] = g_CurrentPlayer->field_488.pos.f[2];

    g_CurrentPlayer->field_488.pos3.f[1] = bondviewYPositionRelated(
        g_CurrentPlayer->field_488.current_tile_ptr_for_portals,
        g_CurrentPlayer->field_488.pos.f[0],
        g_CurrentPlayer->field_488.pos.f[2]);

    g_CurrentPlayer->prop->stan = g_CurrentPlayer->field_488.current_tile_ptr;

    g_CurrentPlayer->prop->pos.f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
    g_CurrentPlayer->prop->pos.f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
    g_CurrentPlayer->prop->pos.f[2] = g_CurrentPlayer->field_488.collision_position.f[2];

#if defined(VERSION_EU)
#define S7F081478_FACTOR_1 0.881200015545f
#else
#define S7F081478_FACTOR_1 0.9f
#endif
#ifndef TARGET_N64
    if (portSmoothOn())
    {
        portLagStep(&g_CurrentPlayer->field_3B8.f[0], S7F081478_FACTOR_1, g_CurrentPlayer->field_488.pos.f[0], g_GlobalTimerDelta);
        portLagStep(&g_CurrentPlayer->field_3B8.f[1], S7F081478_FACTOR_1, g_CurrentPlayer->field_488.pos.f[1], g_GlobalTimerDelta);
        portLagStep(&g_CurrentPlayer->field_3B8.f[2], S7F081478_FACTOR_1, g_CurrentPlayer->field_488.pos.f[2], g_GlobalTimerDelta);
    }
    else
#endif
    for (i=0; i<g_ClockTimer; i++)
    {
        g_CurrentPlayer->field_3B8.f[0] = (S7F081478_FACTOR_1 * g_CurrentPlayer->field_3B8.f[0]) + g_CurrentPlayer->field_488.pos.f[0];
        g_CurrentPlayer->field_3B8.f[1] = (S7F081478_FACTOR_1 * g_CurrentPlayer->field_3B8.f[1]) + g_CurrentPlayer->field_488.pos.f[1];
        g_CurrentPlayer->field_3B8.f[2] = (S7F081478_FACTOR_1 * g_CurrentPlayer->field_3B8.f[2]) + g_CurrentPlayer->field_488.pos.f[2];
    }

#if defined(VERSION_EU)
#define S7F081478_FACTOR_2 0.118799984455f
#else
#define S7F081478_FACTOR_2 0.100000024f
#endif
    g_CurrentPlayer->field_3C4 = g_CurrentPlayer->field_3B8.f[0] * S7F081478_FACTOR_2;
    g_CurrentPlayer->field_3C8 = g_CurrentPlayer->field_3B8.f[1] * S7F081478_FACTOR_2;
    g_CurrentPlayer->field_3CC = g_CurrentPlayer->field_3B8.f[2] * S7F081478_FACTOR_2;
}





/**
 * Fixes vv_verta within -90 and +90.
 * Updates vv_costheta, vv_sintheta, vv_verta360, vv_cosverta, vv_sinverta, field_488.theta_transform.
 * Address 0x7F081790.
 *
 * Perfect Dark function bmoveUpdateVerta.
*/
void bondviewApplyVertaTheta(void)
{
    while (g_CurrentPlayer->vv_verta < -180.0f)
    {
        g_CurrentPlayer->vv_verta += 360.0f;
    }

    while (g_CurrentPlayer->vv_verta >= 180.0f)
    {
        g_CurrentPlayer->vv_verta -= 360.0f;
    }

    if (g_CurrentPlayer->vv_verta > 90.0f)
    {
        g_CurrentPlayer->vv_verta = 90.0f;
    }
    else if (g_CurrentPlayer->vv_verta < -90.0f)
    {
        g_CurrentPlayer->vv_verta = -90.0f;
    }

    g_CurrentPlayer->vv_costheta = cosf(g_CurrentPlayer->vv_theta * DegToRad1Fact(1));
    g_CurrentPlayer->vv_sintheta = sinf(g_CurrentPlayer->vv_theta * DegToRad1Fact(1));

    g_CurrentPlayer->vv_verta360 = g_CurrentPlayer->vv_verta;
    if (g_CurrentPlayer->vv_verta360 < 0.0f)
    {
        g_CurrentPlayer->vv_verta360 += 360.0f;
    }

    g_CurrentPlayer->vv_cosverta = cosf(g_CurrentPlayer->vv_verta360 * DegToRad1Fact(1));
    g_CurrentPlayer->vv_sinverta = sinf(g_CurrentPlayer->vv_verta360 * DegToRad1Fact(1));

    g_CurrentPlayer->field_488.theta_transform.f[0] = -g_CurrentPlayer->vv_sintheta;
    g_CurrentPlayer->field_488.theta_transform.f[1] = 0;
    g_CurrentPlayer->field_488.theta_transform.f[2] = g_CurrentPlayer->vv_costheta;
}


#ifndef TARGET_N64
/* MOUSE aim-mode crosshair offset (player 1): accumulated per tick while
 * precision-aiming, cleared on aim exit. See the KISSY handling at the foot
 * of bondviewProcessInput. */
static f32 g_bondviewMouseSwivelX;
static f32 g_bondviewMouseSwivelY;
static s32 g_bondviewAimSeeded; /* the mouse offset was seeded from the gun's sway at this aim entry */
static s32 g_bondviewSwayFromMouse; /* player 1's last look input was the mouse, so the gun's sway is the mouse's */
static s32 g_bondviewAimMouseMoved; /* the mouse has moved the crosshair this
                                     * aim session (pad aim stays vanilla) */

#ifndef TARGET_N64
/* DIRECT MOUSE, modelled on the 1964 GEPD mouse injector: the mouse delta
 * moves the camera ANGLE directly each tick with no smoothing, instead of
 * feeding the analog turn rate (which felt floaty). These carry this tick's
 * mouse movement in degrees, already gated and inverted. */
f32 g_portMouseYawDeg;
/* PAD LOOK STICK, in degrees for this tick. Kept separate from the mouse's
 * variables on purpose so pad changes never alter the mouse feel. */
f32 g_portPadLookYawDeg;
f32 g_portPadLookPitchDeg;
/* TANK hull steer, in the same units the on-foot turn produces, so the tank's
 * own lag filter treats it exactly like a stick. NOT the turret - see the
 * tank branch below for why those must stay apart. */
f32 g_portPadTankSteer;
/* AIM MODE, port-driven: the LOOK input positions the crosshair instead of
 * the Move input. Held here rather than written straight into moveData
 * because the invert-pitch step overwrites controlStickYRaw from the raw
 * pad stick. */
/**
 * AIM-MODE EDGE TURN, from the LOOK input instead of the Move input: the
 * crosshair follows the Look input here, so the edge turn must too, using the
 * same formulas, clamp and /10 curve as vanilla. Called BEFORE the invert swap,
 * as in vanilla, so Up/Down get swapped correctly.
 */
static void portAimEdgeTurnFromLook(struct MoveData *md, s8 lx, s8 ly)
{
    md->aimTurnLeftSpeed = 0;
    md->aimTurnRightSpeed = 0;
    md->speedVertaUp = 0;
    md->speedVertaDown = 0;

    if (ly > 60)
    {
        md->speedVertaDown = (f32) (ly - 60) / FLOAT_TEN_B;
        if (md->speedVertaDown > 1.0f) md->speedVertaDown = 1.0f;
    }
    else if (ly < -60)
    {
        md->speedVertaUp = (f32) (-60 - ly) / FLOAT_TEN_B;
        if (md->speedVertaUp > 1.0f) md->speedVertaUp = 1.0f;
    }
    if (lx < -60)
    {
        md->aimTurnLeftSpeed = (f32) (-60 - lx) / FLOAT_TEN_B;
        if (md->aimTurnLeftSpeed > 1.0f) md->aimTurnLeftSpeed = 1.0f;
    }
    else if (lx > 60)
    {
        md->aimTurnRightSpeed = (f32) (lx - 60) / FLOAT_TEN_B;
        if (md->aimTurnRightSpeed > 1.0f) md->aimTurnRightSpeed = 1.0f;
    }
}

static s32 port_aim_look_x;
static s32 port_aim_look_y;
static s32 port_aim_look_on;
/* Full deflection, in degrees a second. Pitch is slower than yaw because the
 * vertical range is a fraction of the horizontal one and matching them makes
 * the pitch feel wild. */
#define PORT_PADLOOK_YAW_DEG_PER_SEC   220.0f
#define PORT_PADLOOK_PITCH_DEG_PER_SEC 140.0f
/* On foot, the Look stick drives the engine's own N64 stick path;
 * GE_PADLOOK=direct brings back the degrees path above for A/B. The tank's
 * turret keeps the degrees path either way. */
static s32 portPadLookDirect(void)
{
    static s32 direct = -1;
    if (direct < 0) {
        extern char *getenv(const char *);
        const char *e = getenv("GE_PADLOOK");
        direct = (e != NULL && e[0] == 'd') ? 1 : 0;
    }
    return direct;
}
f32 g_portMousePitchDeg;
s32 g_portMouseDirect;
/* 1 while bondviewFrozenMoveBond is ticking (cutscene/end-level camera:
 * the stick is forced to 0,0 there), 0 while MoveBond runs. Mouse look
 * follows the stick - see the gate in bondviewProcessInput. */
s32 g_portBondFrozenTick;

/**
 * THE mouse-look permission: the one place that decides whether this tick's
 * mouse delta may move Bond's view, mirroring when the stick is obeyed (watch
 * closed, alive, controls unlocked, no MP menu/pause, not in a cutscene or
 * frozen camera). It is applied at the sampling site so every consumer sees
 * zero; consumers must only ROUTE the delta, never re-derive this predicate.
 */
static s32 portMouseLookPermitted(void)
{
    static int gate = -1;   /* GE_MOUSEGATE=0: the older, narrower predicate only (A/B diag) */
    if (gate < 0) {
        extern char *getenv(const char *);
        const char *e = getenv("GE_MOUSEGATE");
        gate = (e == NULL || e[0] != '0');
    }
    if (lvlGetControlsLockedFlag() != 0) return 0;
    if (g_CurrentPlayer->cameramode != 0) return 0;           /* swirl / death cam / cutscene (mode 0 = FP play) */
    { /* No mouse, Port binds or pointer capture during demo playback: the
       * demo replays a recorded pad, and live mouse input would steer and
       * desync it. Recording is untouched. */
        extern s32 get_is_ramrom_flag(void);
        if (get_is_ramrom_flag() != 0) return 0;
    }
    { /* CONSOLE OPEN: other inputs are blanked at their sources, but the
       * mouse delta must be drained here too so Bond doesn't turn while typing;
       * this also stops the Port binds and releases the pointer under smart
       * lock. */
        extern s32 portConIsOpen(void); /* port_console.c - always linked */
        if (portConIsOpen() != 0) return 0;
    }
    if (!gate) return 1;
    if (g_portBondFrozenTick != 0) return 0;                  /* bondviewFrozenMoveBond: stick forced 0,0 */
    if (checkGamePaused() != 0) return 0;                     /* MP pause */
    if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0) return 0; /* solo pause = the watch */
    if (g_CurrentPlayer->bonddead != FALSE) return 0;         /* red screen */
    if (!disablePlayerActionsWhenPausedOrInMpMenu()) return 0; /* MP: stop-play / own menu open */
    if (getPlayerCount() != 1 && (g_stopPlayFlag != 0 || g_gameOverFlag != 0)) return 0;
    return 1;
}

/* The same predicate for the Port input suite (joy_port.c) and lv.c: an
 * action fires only when the stick itself would be obeyed. */
s32 portBondControlsLive(void)
{
    return portMouseLookPermitted();
}

/* The same question asked about PLAYER 1, for callers outside a player's tick
 * (the input poll): keyboard and mouse always belong to player 1, whereas
 * g_CurrentPlayer is whoever ticked last, which made KBM look stutter while
 * another player was dead. Swapped and restored with nothing in between that
 * can yield. */
s32 portP1ControlsLive(void)
{
    extern s32 portPlayerControlsLive(s32 p);

    return portPlayerControlsLive(0);
}

/** The same for any player (input_pc.c: a port-mode pad's N64-only rows are
 * dropped while its player is in live play). Swapped and restored. */
s32 portPlayerControlsLive(s32 p)
{
    struct player *savedPlayer = g_CurrentPlayer;
    struct player_data *savedPerm = g_playerPerm;
    s32 savedNum = player_num;
    s32 r;

    if (p < 0 || p >= 4 || p >= getPlayerCount() || g_playerPointers[p] == NULL)
    {
        return 0;
    }
    set_cur_player(p);
    r = portMouseLookPermitted();
    g_CurrentPlayer = savedPlayer;
    g_playerPerm = savedPerm;
    player_num = savedNum;
    return r;
}

/**
 * SMART MOUSE LOCK: capture the pointer exactly while gameplay owns it, i.e.
 * while a mouse movement would be allowed to move Bond's view, and release it
 * for menus, the watch, MP pause, cutscenes, death and locked controls. It
 * reuses the stick's predicate plus one extra term - a stage must actually be
 * running, since g_CurrentPlayer is stale in the frontend.
 */
s32 portGameplayHasPointer(void)
{
    extern s32 portStageRunning(void); /* front.c: current_menu == MENU_RUN_STAGE */

    if (!portStageRunning())
    {
        return 0;
    }
    return portP1ControlsLive();   /* the pointer is player 1's, whoever ticked last */
}


/* joy_port.c twin stick (2.x second pad from the pad's own right stick) */
extern s32 portJoyTwinStickCapable(s32 pad);
/* this tick: the twin-stick hook fed the second pad AND that pad is the
 * MOVE stick (2.2 / 2.4, N64 mode only - lookOnPad1) - the aim-mode
 * crouch / lean override below */
static s32 sPortTwinMoveSecond;
static s32 sPortTwinFed; /* this tick: the twin-stick hook fed the second pad at all */
/* this tick a DIGITAL Move press (W/S, a D-pad) is walking forward or back.
 * The digital-move block switches canLookAhead off, and Look Ahead's arm keys
 * off that same flag - so it arms on this as it does on a full stick push. */
static s32 sPortDigitalWalk;
static f32 sPortLookAheadHold[4]; /* per player: 60 Hz fields walked with no pitch input (Look Ahead's one-second wait) */
/* crosshair travel per degree of mouse movement in aim mode - see the
 * accumulation site for the derivation from the injector's own rate */
#define AIM_CROSSHAIR_PER_DEG 0.0173f

/* TANK TURRET AIM LOCK: the aim-mode X lock and in-aim turret yaw apply only
 * while the tank's own gun is in hand (same predicate as is_firing_tank); any
 * other weapon fired from the tank aims as on foot. */
/* The pad a 2.x player's "second controller" rumble goes to, chosen as the
 * input side does: a slot marked "2.xp2", none for a twin stick, else the
 * console's fixed port, or none past the last slot. */
s32 portSecondRumbleIndex(s32 pn)
{
    extern s32 portInputSecondPadIndex(s32 playernum, s32 fam);
    s32 second = portInputSecondPadIndex(pn, cur_player_get_control_type() - CONTROLLER_CONFIG_PLENTY);

    if (second >= 0)
    {
        return (second < MAXCONTROLLERS) ? second : -1;
    }
    if (portJoyTwinStickCapable(pn) || pn + getPlayerCount() >= MAXCONTROLLERS)
    {
        return -1;
    }
    return pn + getPlayerCount();
}

s32 portTankTurretWeaponHeld(void)
{
    return g_PlayerIsInTank == 1 && getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_TANKSHELLS;
}

/* The injector scales both axes by fov/basefov, with basefov = 60 unless the
 * player runs a wide FOV override, in which case the override is the base -
 * so an override does not change unzoomed feel and only a real zoom slows the
 * mouse down. GE_DEFAULT_FOV is our midas.ini fov. */
static f32 portMouseFovRatio(void)
{
    f32 fov = viGetFovY();
    f32 basefov = (fov > 60.0f) ? (f32) GE_DEFAULT_FOV : 60.0f;

    if (basefov < 1.0f)
    {
        basefov = 60.0f;
    }
    return fov / basefov;
}

/**
 * WIDE MOUSE AIM: the aim crosshair lives in pinned 4:3 coords with the stick's
 * +/-0.65 reach, so it stops at the edge of the centred 4:3 band. When the
 * direct mouse drives it in a wide single-player window, the reach is widened
 * by the scene wide factor so the crosshair can reach the window edge.
 *
 * The 2D<->3D unpin/pin (bondviewAimUnpinX) is affine for any x, so bullets,
 * autoaim and the gun follow; the sight is drawn in the window mapping
 * (PORT_HUD_ANCHOR_SPAN), and the edge-turn band scales with the reach. Stick
 * aim, mouse_mode=0, MP and 4:3 windows keep a factor of 1.
 */
/**
 * AIM-MODE ISOTROPY: the crosshair swivel is in viewport "box" units, so one
 * degree of mouse travel moved it more pixels sideways than vertically (4/3 in
 * single player, ~3x in a 2P half-height viewport). Scale the mouse's Y swivel
 * by width/height so a hand movement is the same pixel distance either way,
 * like a desktop cursor; clamps and stick aim are unchanged, and GE_AIMISO=0
 * restores the old behaviour.
 */
static f32 portAimIsoY(void)
{
    static s32 on = -1;
    f32 w, h;

    if (on < 0)
    {
        extern char *getenv(const char *);
        const char *e = getenv("GE_AIMISO");
        on = (e == NULL || e[0] != '0') ? 1 : 0;
    }
    if (!on)
    {
        return 1.0f;
    }
    w = getPlayer_c_screenwidth();
    h = getPlayer_c_screenheight();
    if (w <= 0.0f || h <= 0.0f)
    {
        return 1.0f;
    }
    return w / h;
}

f32 portAimReachX(void)
{
    extern f32 g_bondviewSceneWideFactor;

    /* The direct mouse reaches the window edge under either HUD Centering
     * setting; the scene wide factor is 1 in 3P/4P, the watch and 4:3 windows,
     * and the player-count gate below keeps 2P at the stick's reach. The sight
     * draw (PORT_HUD_ANCHOR_SPAN) and the edge clamp key on this reach alone. */
    if (g_portMouseDirect && getPlayerCount() == 1 && g_bondviewSceneWideFactor > 1.001f)
    {
        return g_bondviewSceneWideFactor;
    }
    return 1.0f;
}
#endif

/* AIM edge-turn: the combined crosshair swivel position (stick + mouse,
 * clamped, stored by the KISSY block each aim tick), fed next tick through the
 * stick's own edge curve so a crosshair held at the box edge spins the camera
 * like a held stick. */
static f32 g_bondviewAimSwivelPosX;
static f32 g_bondviewAimSwivelPosY;

/* MOUSE gun-sway low-pass (player 1): a one-pole filter on the clamped
 * hip-fire swivel feed, because tick-to-tick mouse jitter around the clamp made
 * the pinned gun vibrate during hard swings. */
static f32 g_bondviewGunSwayLpX;
static f32 g_bondviewGunSwayLpY;

/* Per-60Hz-tick filter gain; tau ~= 1/gain ticks (0.3 -> ~55ms).
 * Raise toward 1 for snappier sway, lower for floatier. */
#define GUN_SWAY_LP_GAIN 0.3f
#endif

/**
 * US address 7F081974.
 * EU address 7F081A18.
 * Perfect Dark method bmoveProcessInput.
*/
void bondviewProcessInput(s8 stick_x, s8 stick_y, u16 buttons, u16 oldbuttons)
{
    struct MoveData moveData; // sp120

    s8 player_joyGetStickX; // sp11F
    s8 player_joyGetStickY; // sp11E
    u16 player_joyGetButtons; // sp11C
    u16 copy_prev_buttons_pressed; // sp11A
    s32 adjustedStickX;
    s32 tmpc2sticky;
    s32 sp10C;
    s32 sp108;
    s32 sp104;
    s32 sp100;
    u16 shootButtons; // spFE
    u16 aimButtons; // spFC
    u16 invButtons; // spFA
    // missing spF8
    TankRecord *spF4;
    s32 i_0; // spF0
    TankRecord *spEC;
    f32 ftemp_nostack_spE8; // unused
    f32 noiseRadius; // spE4
    f32 ftemp_nostack_spE0;
    f32 targetSpeed;
    f32 ftemp_nostack_spD8;
    f32 unadjustedTargetSpeed;
    f32 ftemp_nostack_spD0;
    f32 ftemp_nostack_spCC;
    f32 ftemp_nostack_spC8;
    f32 targetPitch;
    StandTile *spC0;
    f32 spBC;
    f32 ftemp_nostack_spB8;
    struct coord3d spAC;
    struct coord3d spA0;
    s32 stack_padding_sp9C; // unused
    f32 ftemp_nostack_sp98;
    f32 ftemp_nostack_sp94;
    f32 ftemp_nostack_sp90; // unused
    f32 ftemp_nostack_sp8C;
    f32 ftemp_nostack_sp88;
    f32 ftemp_nostack_sp84;
    f32 ftemp_nostack_sp80;
    s32 i_1;
    f32 ftemp_nostack_sp78;
    s32 canCycleWeapons; // sp74
    f32 sp70;


    moveData.canSwivelGun = 0;
    moveData.canManualAim = 0;
    moveData.triggerOn = 0;
    moveData.btap = 0;
    moveData.canLookAhead = 0;
    moveData.canTurnTank = 0;
    moveData.canNaturalTurn = 0;
    moveData.canNaturalPitch = 0;
    moveData.digitalStepForward = 0;
    moveData.digitalStepBack = 0;
    moveData.digitalStepLeft = 0;
    moveData.digitalStepRight = 0;
    moveData.tankTurnLeftSpeed = 0;
    moveData.tankTurnRightSpeed = 0;
    moveData.speedVertaDown = 0;
    moveData.speedVertaUp = 0;
    moveData.aimTurnLeftSpeed = 0;
    moveData.aimTurnRightSpeed = 0;
    moveData.weaponBackOffset = 0;
    moveData.weaponForwardOffset = 0;
    moveData.aiming = 0;
    moveData.zooming = 0;
    moveData.zoomOutFovPersec = 0;
    moveData.zoomInFovPersec = 0;
    moveData.crouchDown = 0;
    moveData.crouchUp = 0;
    moveData.rLeanLeft = 0;
    moveData.rLeanRight = 0;
    moveData.detonating = 0;
    moveData.canAutoAim = 0;
    moveData.invertPitch = get_cur_player_look_vertical_inverted() == 0;
    moveData.disableLookAhead = 0;

    if (stick_x < -5) {
		moveData.controlStickXSafe = stick_x + 5;
	} else if (stick_x > 5) {
		moveData.controlStickXSafe = stick_x - 5;
	} else {
		moveData.controlStickXSafe = 0;
	}

	if (stick_y < -5) {
		moveData.controlStickYSafe = stick_y + 5;
	} else if (stick_y > 5) {
		moveData.controlStickYSafe = stick_y - 5;
	} else {
		moveData.controlStickYSafe = 0;
	}

    moveData.controlStickXRaw = (s32)stick_x;
    moveData.controlStickYRaw = (s32)stick_y;

    moveData.analogTurn = moveData.controlStickXSafe;
    moveData.analogPitch = moveData.controlStickYSafe;
    moveData.analogStrafe = moveData.controlStickXSafe;
    moveData.analogWalk = moveData.controlStickYSafe;

#ifndef TARGET_N64
    /* The twin-stick flags are file statics only the 2.x branch sets, but the
     * aim-lock block after the style dispatch reads them for every style and
     * player, so clear them per call or a 1.x tick inherits stale flags. */
    sPortTwinFed = 0;
    sPortTwinMoveSecond = 0;
    sPortDigitalWalk = 0;
    /* MOUSE LOOK: sample the captured mouse's scaled delta once per tick,
     * player 1 only (split-screen players 2-4 are pad-only); it feeds the
     * analog look terms and, in aim mode, the crosshair. */
    moveData.freeLookDx = 0.0f;
    moveData.freeLookDy = 0.0f;
#ifdef GE_VIDEO
    if (get_cur_playernum() == 0)
    {
        extern void portInputMouseLookDelta(f32 *dx, f32 *dy);
        extern void portInputMouseLookDeltaDeg(f32 *dxdeg, f32 *dydeg);
        extern s32 portInputMouseDirectMode(void);
        portInputMouseLookDelta(&moveData.freeLookDx, &moveData.freeLookDy);
        g_portMouseDirect = portInputMouseDirectMode();
        g_portMouseYawDeg = 0.0f;
        g_portMousePitchDeg = 0.0f;
        if (g_portMouseDirect)
        {
            portInputMouseLookDeltaDeg(&g_portMouseYawDeg, &g_portMousePitchDeg);
        }
        { /* GE_INPUTREC / GE_INPUTPLAY (port/src/inputrec.c): this tick's sample */
            extern void portInputRecMouse(f32 *dx, f32 *dy, s32 *direct, f32 *yawdeg, f32 *pitchdeg);
            extern void portInputRecCheck(f32 theta, f32 x, f32 y, f32 z);
            portInputRecMouse(&moveData.freeLookDx, &moveData.freeLookDy, &g_portMouseDirect,
                              &g_portMouseYawDeg, &g_portMousePitchDeg);
            if (g_CurrentPlayer->prop != NULL)
            {
                portInputRecCheck(g_CurrentPlayer->vv_theta, g_CurrentPlayer->prop->pos.x,
                                  g_CurrentPlayer->prop->pos.y, g_CurrentPlayer->prop->pos.z);
            }
        }
        /* Drain the mouse accumulator every tick but discard the result while
         * controls are locked (e.g. cutscenes), so look deltas neither leak in
         * nor bank up and snap the camera when control returns. */
        /* GE_CUTDBG=1 debug: print what state the cutscene mouse gate
         * actually sees. */
        {
            static int cutdbg = -1;
            if (cutdbg < 0) {
                extern char *getenv(const char *);
                cutdbg = getenv("GE_CUTDBG") != NULL;
            }
            if (cutdbg && (moveData.freeLookDx != 0.0f || moveData.freeLookDy != 0.0f)) {
                extern int printf(const char *, ...);
                printf("ge: cutdbg cammode=%d gcam=%d lock=%d paused=%d frozen=%d watch=%d ok=%d dead=%d inv=%d aim=%d dy=%.2f dx=%.2f sverta=%.3f verta=%.3f theta=%.3f\n",
                       (int) g_CurrentPlayer->cameramode, (int) g_CameraMode, (int) lvlGetControlsLockedFlag(),
                       (int) checkGamePaused(), (int) g_portBondFrozenTick,
                       (int) g_CurrentPlayer->watch_animation_state, (int) portMouseLookPermitted(),
                       (int) g_CurrentPlayer->bonddead, (int) moveData.invertPitch, (int) g_CurrentPlayer->insightaimmode,
                       moveData.freeLookDy, moveData.freeLookDx, g_CurrentPlayer->speedverta,
                       g_CurrentPlayer->vv_verta, g_CurrentPlayer->vv_theta);
            }
        }
        /* Permission (see portMouseLookPermitted): drain the delta every tick
         * but discard it unless the stick could move Bond, so motion never
         * banks up; this single predicate replaces per-consumer checks that
         * kept missing cases. */
        if (!portMouseLookPermitted())
        {
            /* Controls-lock alone misses cutscene cameras that run unlocked:
             * normal first-person play is cameramode == 0 (the intro swirl is
             * 1), so mouse look is legal only in mode 0 with controls unlocked. */
            moveData.freeLookDx = 0.0f;
            moveData.freeLookDy = 0.0f;
            g_portMouseYawDeg = 0.0f;
            g_portMousePitchDeg = 0.0f;
        }
        /* PORT: the mouse-pitch sign downstream was tuned for the default
         * REVERSE setting, so negate on UPRIGHT (mouse forward looks up,
         * standard FPS) rather than on REVERSE; the stick path is untouched. */
        if (!moveData.invertPitch)
        {
            moveData.freeLookDy = -moveData.freeLookDy;
            g_portMousePitchDeg = -g_portMousePitchDeg;
        }
    }
    else
    {
        /* MP: the direct-mouse deltas are written on player 1's tick but read
         * by every player's consumers, so zero them on the pad players' ticks
         * or P1's mouse turns players 2-4. */
        g_portMouseYawDeg = 0.0f;
        g_portMousePitchDeg = 0.0f;
    }
#endif
#endif

#ifndef TARGET_N64
    { /* PAUSE TO OBJECTIVES: the Port watch_objectives bind is a START press
       * that lands on the Mission Status page, only from a closed watch in play
       * (the latch is consumed either way so it never fires later). */
        extern s32 portJoyPortWatchObjectivesPressed(s32 pad);
        extern void portWatchRequestPage(s32 page);
        extern s32 portWatchModalBack(void);
        extern s32 get_is_ramrom_flag(void);
        extern void portRamromEscapeExit(const char *reason); /* ramromreplay.c */
        /* frozen camera (intro / cutscene / end-level): leave the press for
         * bondviewFrozenCameraTick's cutscene skip, which runs after us; on
         * unskippable cameras portJoyPortTake's staleness timeout drops it */
        s32 objpressed = (g_portBondFrozenTick == 0)
            ? portJoyPortWatchObjectivesPressed(get_cur_playernum()) : 0;

        if (objpressed && get_is_ramrom_flag() != 0)
        {   /* demo playback: a latch arriving after the ramrom replay sweep
             * must still skip the demo, never open the watch over it (same gate
             * as Escape below) */
            portRamromEscapeExit("real input press (watch_objectives bind)");
            objpressed = 0;
        }
        if (objpressed && getPlayerCount() >= 2)
        { /* MP has no solo watch, so give this bind (Xbox layout: Back) the
           * nearest MP meaning: toggle this player's pause menu, as START does. */
            portJoyInjectPressed((s8) get_cur_playernum(), START_BUTTON);
        }
        if (objpressed && getPlayerCount() == 1)
        {
            if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0
                && g_CurrentPlayer->bonddead == FALSE
                && g_bondviewForceDisarm <= 0
                && lvlGetControlsLockedFlag() == 0)
            {
                /* the objectives live on the Briefing page (Mission Status is
                 * the page the watch opens on anyway) */
                portWatchRequestPage(WATCH_INDEX_MISSION_BRIEFING);
                trigger_solo_watch_menu(0);
            }
            else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5)
            {
                /* pressed again with the watch up: close it, like START does */
                if (!portWatchModalBack())
                {   /* a drop-down or an armed key capture eats the back press
                     * first, so it shuts the box rather than the whole watch */
                    set_open_close_solo_watch_menu_to1();
                }
            }
            else if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0
                     && g_CurrentPlayer->bonddead == FALSE
                     && g_bondviewForceDisarm <= 0)
            {   /* MID-ANIMATION: a second press cancels the pause, as START
                 * does on the N64 (the vanilla START test below fires for every
                 * state but 5) */
                trigger_solo_watch_menu(0);
            }
        }
    }

    { /* Escape opens and closes the watch. The keyboard is pad 0, so only
       * player 0 may consume the latch; keep the short-circuit order, or in
       * split screen whichever player runs first would eat it. */
        extern s32 portEscPressed(void); /* input_pc.c; weak 0 headless */
        extern s32 get_is_ramrom_flag(void);
        extern void portRamromEscapeExit(const char *reason); /* ramromreplay.c */

        if (get_cur_playernum() == 0 && portEscPressed())
        {
            if (get_is_ramrom_flag() != 0)
            {   /* demo playback: Escape is a PC key the replay's "any real
                 * press" exit can't see, and the watch must never open over a
                 * recording, so Escape skips the demo like any other key. */
                portRamromEscapeExit("real input press (Escape key)");
            }
            else if (getPlayerCount() >= 2)
            {   /* With the MP menu up, send B to close it, because START there
                 * confirms and could end the match for everyone. Game over
                 * keeps START so player 1 can still end the match. */
                portJoyInjectPressed((s8) 0,
                    (g_CurrentPlayer->mpmenuon && !g_gameOverFlag) ? B_BUTTON : START_BUTTON);
            }
            else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0
                     && g_CurrentPlayer->bonddead == FALSE
                     && g_bondviewForceDisarm <= 0
                     && lvlGetControlsLockedFlag() == 0
                     && g_portBondFrozenTick == 0) /* live play only: never from the intro /
                                                    * cutscene / end-level cameras, where vanilla
                                                    * zeroes START (bondviewFrozenMoveBond) */
            {
                trigger_solo_watch_menu(0);
            }
            else if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5)
            {
                if (!portWatchModalBack())
                {   /* a drop-down or an armed key capture eats the back press
                     * first, so it shuts the box rather than the whole watch */
                    set_open_close_solo_watch_menu_to1();
                }
            }
            else if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0
                     && g_CurrentPlayer->bonddead == FALSE
                     && g_bondviewForceDisarm <= 0)
            {   /* MID-ANIMATION: Escape again cancels the pause, as START does
                 * on the N64 (see the watch_objectives branch above) */
                trigger_solo_watch_menu(0);
            }
        }

        { /* CLASSIC MODE KEY ([controls] classic_toggle, default Tab - video.c):
           * flip it in play, as the watch's Classic Mode row does. The keyboard
           * is player 1, so only player 0 consumes; never over a recording. */
            extern s32 portVideoClassicTogglePressed(void); /* video.c; weak 0 headless */
            extern s32 portJoyPortClassicTogglePressed(s32 pad); /* joy_port.c: a pad's bound control */
            extern void portClassicToggle(void);            /* port_options.c */
            /* the keyboard's edge (player 1's), or this player's own pad bind */
            const s32 kb = get_cur_playernum() == 0 && portVideoClassicTogglePressed();
            const s32 pad = portJoyPortClassicTogglePressed(get_cur_playernum());

            /* not with the watch up - the press is still taken above, so it
             * cannot fire as the watch closes */
            if ((kb || pad) && get_is_ramrom_flag() == 0
                && g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
            {
                portClassicToggle();
            }
        }
    }
#endif
    if (g_CurrentPlayer->bonddead == FALSE
        && g_bondviewForceDisarm <= 0
        && (
            (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x5
                && ((buttons & ~oldbuttons) & START_BUTTON)
#ifndef TARGET_N64
                && !portReturnStartThisTick() /* spec 6: Return confirms, it does not open the watch */
#endif
            )
            ||
            (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5
                && g_CurrentPlayer->open_close_solo_watch_menu)
        )
        && (getPlayerCount() == 1))
    {
        trigger_solo_watch_menu(0);
    }

    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0
        && g_CurrentPlayer->bonddead == FALSE
        && (
            getPlayerCount() == 1
            || (
                g_stopPlayFlag == 0
                && g_gameOverFlag == 0)))
    {
#ifndef TARGET_N64
        { /* twin-stick harness: GE_TWINSTICK_AUTO="x,y,style" forces a 2.x style */
            extern s32 portJoyTwinStickAutoStyle(void);
            s32 autostyle = portJoyTwinStickAutoStyle();
            if (autostyle >= CONTROLLER_CONFIG_PLENTY && autostyle <= CONTROLLER_CONFIG_GOODHEAD
                && cur_player_get_control_type() != autostyle)
            {
                cur_player_set_control_type(autostyle);
            }
        }
#endif
        if (cur_player_get_control_type() == CONTROLLER_CONFIG_DOMINO /* 2.3 */
            || cur_player_get_control_type() == CONTROLLER_CONFIG_GOODHEAD /* 2.4 */
            || cur_player_get_control_type() == CONTROLLER_CONFIG_GALORE /* 2.2 */
            || cur_player_get_control_type() == CONTROLLER_CONFIG_PLENTY /* 2.1 */
            )
        {
#ifndef TARGET_N64
            { /* TWIN STICK (joy_port.c): the second pad is the player's own
               * controller's right stick, with LT as its Z; pads without one
               * fall back to the N64 read below. muteC = controls live: the
               * right stick must not also fire C buttons while aiming, except
               * in the watch. */
                extern s32 portJoyTwinStickRead(s32 pad, s8 *x, s8 *y, s32 muteC, s32 lookOnPad1);
                s8 tx = 0;
                s8 ty = 0;
                /* 2.2 / 2.4 read pad 1 as LOOK and the second pad as MOVE, so
                 * input_pc.c swaps the Move/Look sticks accordingly - N64 mode
                 * only, because port-driven players already read the sticks the
                 * modern way and the swap would cross them. Port-driven players
                 * are folded to 1.x anyway; this gate is a safety net. */
                extern s32 portInputPadIsN64(s32 slot);
                s32 lookOnPad1 = portInputPadIsN64(get_cur_playernum())
                              && (cur_player_get_control_type() == CONTROLLER_CONFIG_GALORE
                               || cur_player_get_control_type() == CONTROLLER_CONFIG_GOODHEAD);
                /* TWO REAL PADS: a slot marked "2.xp2" in Options > Control is
                 * this player's second controller wherever it is plugged in,
                 * beating the player's own right stick; otherwise use the right
                 * stick if any, else the console's fixed port. */
                extern s32 portInputSecondPadIndex(s32 playernum, s32 fam);
                extern s32 get_is_ramrom_flag(void);
                /* demo playback: the recording's own second pad, never a live
                 * right stick or slot - recorded input stays authoritative, and
                 * 2.1 attract demos desync without it */
                const s32 demo = get_is_ramrom_flag() != 0;
                s32 second = demo ? -1 : portInputSecondPadIndex(get_cur_playernum(),
                        cur_player_get_control_type() - CONTROLLER_CONFIG_PLENTY);

                sPortTwinMoveSecond = 0;
                sPortTwinFed = 0;
                if (!demo && second < 0
                    && portJoyTwinStickRead(get_cur_playernum(), &tx, &ty, portBondControlsLive(), lookOnPad1))
                {
                    player_joyGetStickX = tx;
                    player_joyGetStickY = ty;
                    player_joyGetButtons = (buttons & R_TRIG) ? Z_TRIG : 0;
                    sPortTwinMoveSecond = lookOnPad1;
                    sPortTwinFed = 1;
                }
                else
                {
                    if (second < 0)
                    {
                        second = get_cur_playernum() + getPlayerCount();
                    }
                    if (second >= MAXCONTROLLERS)
                    {
                        player_joyGetStickX = 0;
                        player_joyGetStickY = 0;
                        player_joyGetButtons = 0;
                    }
                    else
                    {
                        player_joyGetStickX = joyGetStickX(second);
                        player_joyGetStickY = joyGetStickY(second);
                        player_joyGetButtons = joyGetButtons(second, (u32)ANY_BUTTON);
                    }
                }
            }
#else
            player_joyGetStickX = joyGetStickX(get_cur_playernum() + getPlayerCount());
            player_joyGetStickY = joyGetStickY(get_cur_playernum() + getPlayerCount());
            player_joyGetButtons = joyGetButtons(get_cur_playernum() + getPlayerCount() , (u32)ANY_BUTTON);
#endif

            copy_prev_buttons_pressed = g_CurrentPlayer->prev_buttons_pressed;

            if (player_joyGetStickX < -5)
            {
                adjustedStickX = player_joyGetStickX + 5;
            }
            else if (player_joyGetStickX > 5)
            {
                adjustedStickX = player_joyGetStickX - 5;
            }
            else
            {
                adjustedStickX = 0;
            }

            if (player_joyGetStickY < -5)
            {
                tmpc2sticky = player_joyGetStickY + 5;
            }
            else if (player_joyGetStickY >= 6)
            {
                tmpc2sticky = player_joyGetStickY - 5;
            }
            else
            {
                tmpc2sticky = 0;
            }

            /* 2.1 and 2.3 */
            if (cur_player_get_control_type() == CONTROLLER_CONFIG_PLENTY || (cur_player_get_control_type() == CONTROLLER_CONFIG_DOMINO))
            {
                moveData.analogStrafe = adjustedStickX;
                moveData.analogPitch = tmpc2sticky;
            }
            else
            {
                if (g_PlayerIsInTank == 1 && !g_CurrentPlayer->insightaimmode)
                {
                    moveData.analogTurn = adjustedStickX;
                }
                else
                {
                    moveData.analogStrafe = adjustedStickX;
                }

                moveData.analogWalk = tmpc2sticky;
            }

            /* 2.1 and 2.2 */
            if (cur_player_get_control_type() == CONTROLLER_CONFIG_PLENTY || cur_player_get_control_type() == CONTROLLER_CONFIG_GALORE)
            {
                sp104 = (player_joyGetButtons & Z_TRIG) != 0;
                sp100 = ((player_joyGetButtons & ~copy_prev_buttons_pressed) & Z_TRIG) != 0;
                sp10C = (buttons & Z_TRIG) != 0;
                sp108 = ((buttons & ~oldbuttons) & Z_TRIG) != 0;
            }
            else
            {
                sp104 = (buttons & Z_TRIG) != 0;
                sp100 = ((buttons & ~oldbuttons) & Z_TRIG) != 0;
                sp10C = (player_joyGetButtons & Z_TRIG) != 0;
                sp108 = ((player_joyGetButtons & ~copy_prev_buttons_pressed) & Z_TRIG) != 0;
            }

            if (lvlGetControlsLockedFlag() == 0 && disablePlayerActionsWhenPausedOrInMpMenu())
            {
                if (cur_player_get_aim_control() == 0)
                {
                    g_CurrentPlayer->insightaimmode = sp104;
                }
                else if (sp100)
                {
                    g_CurrentPlayer->insightaimmode = !g_CurrentPlayer->insightaimmode;
                }

                moveData.canSwivelGun = !g_CurrentPlayer->insightaimmode;
                moveData.canAutoAim = !g_CurrentPlayer->insightaimmode;

                moveData.canManualAim = g_CurrentPlayer->insightaimmode;

                moveData.btap = (
                    (((buttons & ~oldbuttons) & B_BUTTON) != 0)
                    ||
                    ((((player_joyGetButtons & ~copy_prev_buttons_pressed) & B_BUTTON)) != 0)
                    );

                moveData.canLookAhead = !g_CurrentPlayer->insightaimmode;
                moveData.canTurnTank = 1;
                moveData.canNaturalTurn = !g_CurrentPlayer->insightaimmode;
                moveData.canNaturalPitch = !g_CurrentPlayer->insightaimmode;

                if (g_CurrentPlayer->insightaimmode && (stick_y > 60))
                {
                    moveData.speedVertaDown = (f32) (stick_y - 60) / FLOAT_TEN_B;
                    if (moveData.speedVertaDown > 1.0f)
                    {
                        moveData.speedVertaDown = 1.0f;
                    }
                }
                else
                {
                    //moveData.speedVertaDown = 0;
                }

                if (g_CurrentPlayer->insightaimmode && (stick_y < -60))
                {
                    moveData.speedVertaUp = (f32) (-60 - stick_y) / FLOAT_TEN_B;
                    if (moveData.speedVertaUp > 1.0f)
                    {
                        moveData.speedVertaUp = 1.0f;
                    }
                }
                else
                {
                    //moveData.speedVertaUp = 0;
                }


                if (g_CurrentPlayer->insightaimmode && (stick_x < -60))
                {
                    moveData.aimTurnLeftSpeed = (f32) (-60 - stick_x) / FLOAT_TEN_B;
                    if (moveData.aimTurnLeftSpeed > 1.0f)
                    {
                        moveData.aimTurnLeftSpeed = 1.0f;
                    }
                }
                else
                {
                    //moveData.aimTurnLeftSpeed = 0;
                }

                if (g_CurrentPlayer->insightaimmode && (stick_x > 60) )
                {
                    moveData.aimTurnRightSpeed = (f32) (stick_x - 60) / FLOAT_TEN_B;
                    if (moveData.aimTurnRightSpeed > 1.0f)
                    {
                        moveData.aimTurnRightSpeed = 1.0f;
                    }
                }
                else
                {
                    //moveData.aimTurnRightSpeed = 0;
                }

                moveData.weaponBackOffset = (
                        ((buttons & A_BUTTON) != 0)
                        ||
                        ((player_joyGetButtons & A_BUTTON) != 0)
                    )
                    && (sp108);

               moveData.weaponForwardOffset = (
                    (
                       (((buttons & ~oldbuttons) & A_BUTTON) != 0)
                       ||
                       (((player_joyGetButtons & ~copy_prev_buttons_pressed) & A_BUTTON) != 0)
                    ))
                    && (!sp10C);

                moveData.aiming = g_CurrentPlayer->insightaimmode;
                moveData.zooming = g_CurrentPlayer->insightaimmode;

                if ((bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH))
                    && g_CurrentPlayer->insightaimmode)
                {
                    if (tmpc2sticky < 0)
                    {
                        moveData.zoomOutFovPersec = (f32) -tmpc2sticky / 70.0f;
                        if (moveData.zoomOutFovPersec > 1.0f)
                        {
                            moveData.zoomOutFovPersec = 1.0f;
                        }

#ifndef TARGET_N64
                        /* twin stick: the N64 two-pad zoom rate is double the
                         * 1.x C-button rate, which felt too fast; on the Move
                         * stick match the 1.x rate at full deflection */
                        if (!sPortTwinFed)
#endif
                        moveData.zoomOutFovPersec *= 2.0f;
                    }

                    if (tmpc2sticky > 0)
                    {
                        moveData.zoomInFovPersec = (f32) tmpc2sticky / 70.0f;
                        if (moveData.zoomInFovPersec > 1.0f)
                        {
                            moveData.zoomInFovPersec = 1.0f;
                        }

#ifndef TARGET_N64
                        if (!sPortTwinFed)
#endif
                        moveData.zoomInFovPersec *= 2.0f;
                    }
                }

#ifndef TARGET_N64
                if (sPortTwinFed && g_CurrentPlayer->insightaimmode)
                { /* AIM MODE on a modern pad, any 2.x style: do what the 1.x
                   * styles do with the C buttons - no walking or strafing, Move
                   * stick X = lean, Y = crouch. The Move stick is the second
                   * pad in 2.2/2.4 (sPortTwinMoveSecond) and pad 1 in 2.1/2.3;
                   * gated on sPortTwinFed so a real two-controller setup keeps
                   * vanilla behaviour. */
                    s32 mvx = sPortTwinMoveSecond ? player_joyGetStickX : moveData.controlStickXSafe;

                    moveData.analogStrafe = 0;
                    moveData.analogWalk = 0;
                    moveData.rLeanLeft = mvx < -30;
                    moveData.rLeanRight = mvx > 30;
                }
#endif
                moveData.crouchDown = (bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) == 0)
                    && (g_CurrentPlayer->insightaimmode)
                    && (player_joyGetStickY < -30);

                moveData.crouchUp = (bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) == 0)
                    && (g_CurrentPlayer->insightaimmode)
                    && (player_joyGetStickY > 30);

#ifndef TARGET_N64
                if (sPortTwinFed && !sPortTwinMoveSecond && g_CurrentPlayer->insightaimmode
                    && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) == 0)
                { /* 2.1 / 2.3: the lines above read the second pad, which is the
                   * aim stick in these styles, so crouch must come from the
                   * Move stick (pad 1) instead, same as the lean. */
                    moveData.crouchDown = moveData.controlStickYSafe < -30;
                    moveData.crouchUp = moveData.controlStickYSafe > 30;
                }
#endif

                if ((
                           (((buttons & A_BUTTON) != 0) && (((buttons & ~oldbuttons) & B_BUTTON) != 0))
                        || (((buttons & B_BUTTON) != 0) && (((buttons & ~oldbuttons) & A_BUTTON) != 0))
                        || ((player_joyGetButtons & A_BUTTON) && ((player_joyGetButtons & ~copy_prev_buttons_pressed) & B_BUTTON))
                        || ((player_joyGetButtons & B_BUTTON) && ((player_joyGetButtons & ~copy_prev_buttons_pressed) & A_BUTTON)))
                    && (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_REMOTEMINE))
                {
                    moveData.detonating = 1;
                    moveData.weaponBackOffset = 0;
                    moveData.weaponForwardOffset = 0;
                    moveData.btap = 0;
                }

                if (g_PlayerIsInTank == 1 && g_CurrentPlayer->insightaimmode)
                {
                    if (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_TANKSHELLS)
                    {
                        moveData.controlStickXRaw = 0;

                        if (moveData.analogStrafe == 0)
                        {
                            moveData.analogStrafe = moveData.analogTurn;
                        }
                    }
                    else if (moveData.analogStrafe == 0)
                    {
                        if (moveData.aimTurnLeftSpeed > 0)
                        {
                            moveData.tankTurnLeftSpeed = moveData.aimTurnLeftSpeed;
                        }

                        if (moveData.aimTurnRightSpeed > 0)
                        {
                            moveData.tankTurnRightSpeed = moveData.aimTurnRightSpeed;
                        }
                    }

                    moveData.aimTurnLeftSpeed = 0;
                    moveData.aimTurnRightSpeed = 0;
                }
            }

            moveData.triggerOn = (sp10C)
                && (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
                && ((buttons & A_BUTTON) == 0)
                && ((player_joyGetButtons & A_BUTTON) == 0);

            moveData.disableLookAhead = 1;
            g_CurrentPlayer->prev_buttons_pressed = player_joyGetButtons;
        }
        else
        {
            /* 1.3 and 1.4 */
            if (cur_player_get_control_type() == CONTROLLER_CONFIG_KISSY
                || cur_player_get_control_type() == CONTROLLER_CONFIG_GOODNIGHT)
            {
                shootButtons = A_BUTTON;
                aimButtons = Z_TRIG;
                invButtons = L_TRIG | R_TRIG;
            }
            else
            {
                shootButtons = Z_TRIG;
                aimButtons = L_TRIG | R_TRIG;
                invButtons = A_BUTTON;
            }

            if (lvlGetControlsLockedFlag() == 0)
            {
                if (disablePlayerActionsWhenPausedOrInMpMenu())
                {
                    if (cur_player_get_aim_control() == 0)
                    {
                        g_CurrentPlayer->insightaimmode = (buttons & aimButtons) != 0;
                    }
                    else if ((buttons & ~oldbuttons) & aimButtons)
                    {
                        g_CurrentPlayer->insightaimmode = !g_CurrentPlayer->insightaimmode;
                    }

                    moveData.canSwivelGun = !g_CurrentPlayer->insightaimmode;
                    moveData.canAutoAim = !g_CurrentPlayer->insightaimmode;
                    moveData.btap = ((buttons & ~oldbuttons) & B_BUTTON) != 0;
                    moveData.canManualAim = g_CurrentPlayer->insightaimmode;

                    /* 1.2 and 1.4 */
                    if (cur_player_get_control_type() == CONTROLLER_CONFIG_SOLITARE
                        || cur_player_get_control_type() == CONTROLLER_CONFIG_GOODNIGHT)
                    {
                        if ((buttons & (L_JPAD | L_CBUTTONS)) != 0)
                        {
                            if (!g_CurrentPlayer->insightaimmode)
                            {
                                if (g_PlayerIsInTank == 1)
                                {
                                    moveData.aimTurnLeftSpeed = 1.0f;
                                }
                                else
                                {
                                    moveData.digitalStepLeft = 1;
                                }
                            }
                            else
                            {
                                moveData.tankTurnLeftSpeed = 1.0f;
                            }
                        }

                        if ((buttons & (R_JPAD | R_CBUTTONS)) != 0)
                        {
                            if (!g_CurrentPlayer->insightaimmode)
                            {
                                if (g_PlayerIsInTank == 1)
                                {
                                    moveData.aimTurnRightSpeed = 1.0f;
                                }
                                else
                                {
                                    moveData.digitalStepRight = 1;
                                }
                            }
                            else
                            {
                                moveData.tankTurnRightSpeed = 1.0f;
                            }
                        }

                        moveData.digitalStepForward = (!g_CurrentPlayer->insightaimmode)
                            && ((buttons & (U_JPAD | U_CBUTTONS)) );

                        moveData.digitalStepBack = (!g_CurrentPlayer->insightaimmode)
                            && ((buttons & (D_JPAD | D_CBUTTONS)));

                        moveData.canNaturalPitch = !g_CurrentPlayer->insightaimmode;

                        if (g_PlayerIsInTank == 1)
                        {
                            moveData.canTurnTank = !g_CurrentPlayer->insightaimmode;
                        }
                        else
                        {
                            moveData.canNaturalTurn = !g_CurrentPlayer->insightaimmode;
                        }
                    }
                    else
                    {
                        if ((buttons & (s32)(L_JPAD | L_CBUTTONS)) != 0)
                        {
                            moveData.tankTurnLeftSpeed = 1.0f;
                        }
                        /* optional else statement, matches with or without. */
                        else
                        {
                            moveData.tankTurnLeftSpeed = 0;
                        }

                        if ((buttons & (s32)(R_JPAD | R_CBUTTONS)) != 0)
                        {
                            moveData.tankTurnRightSpeed = 1.0f;
                        }
                        /* optional else statement, matches with or without. */
                        else
                        {
                            moveData.tankTurnRightSpeed = 0;
                        }

                        moveData.digitalStepLeft = (!g_CurrentPlayer->insightaimmode)
                            && ((buttons & (s32)(L_JPAD | L_CBUTTONS)) );

                        moveData.digitalStepRight = (!g_CurrentPlayer->insightaimmode)
                            && ((buttons & (s32)(R_JPAD | R_CBUTTONS)));

                        moveData.canLookAhead = !g_CurrentPlayer->insightaimmode;

                        if ((!g_CurrentPlayer->insightaimmode) && (buttons & (U_JPAD | U_CBUTTONS)) )
                        {
                            moveData.speedVertaDown = 1.0f;
                        }

                        if ((!g_CurrentPlayer->insightaimmode) && (buttons & (D_JPAD | D_CBUTTONS)))
                        {
                            moveData.speedVertaUp = 1.0f;
                        }

                        moveData.canNaturalTurn = !g_CurrentPlayer->insightaimmode;
                    }

                    if ((g_CurrentPlayer->insightaimmode) && (stick_y > 60))
                    {
                        moveData.speedVertaDown = (f32) (stick_y - 60) / FLOAT_TEN_B;
                        if (moveData.speedVertaDown > 1.0f)
                        {
                            moveData.speedVertaDown = 1.0f;
                        }
                    }
                    else if ((g_CurrentPlayer->insightaimmode) && (stick_y < -60))
                    {
                        moveData.speedVertaUp = (f32) (-60 - stick_y) / FLOAT_TEN_B;
                        if (moveData.speedVertaUp > 1.0f)
                        {
                            moveData.speedVertaUp = 1.0f;
                        }
                    }

                    if ((g_CurrentPlayer->insightaimmode) && (stick_x < -60))
                    {
                        moveData.aimTurnLeftSpeed = (f32) (-60 - stick_x) / FLOAT_TEN_B;
                        if (moveData.aimTurnLeftSpeed > 1.0f)
                        {
                            moveData.aimTurnLeftSpeed = 1.0f;
                        }
                    }

                    if ((g_CurrentPlayer->insightaimmode) && (stick_x > 60))
                    {
                        moveData.aimTurnRightSpeed = (f32) (stick_x - 60) / FLOAT_TEN_B;
                        if (moveData.aimTurnRightSpeed > 1.0f)
                        {
                            moveData.aimTurnRightSpeed = 1.0f;
                        }
                    }

                    moveData.weaponBackOffset =
                        ((buttons & invButtons) != 0)
                        &&
                        (((buttons & ~oldbuttons) & shootButtons) != 0)
                        ;

                    moveData.weaponForwardOffset =
                        (((buttons & ~oldbuttons) & invButtons) != 0)
                        &&
                        ((buttons & shootButtons) == 0)
                        ;

                    moveData.aiming = g_CurrentPlayer->insightaimmode;
                    moveData.zooming = g_CurrentPlayer->insightaimmode;

                    if ((bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH))
                        && g_CurrentPlayer->insightaimmode
                        )
                    {
                        /* down = 0x404 */
                        if ((buttons & (D_JPAD | D_CBUTTONS)) != 0)
                        {
                            moveData.zoomOutFovPersec = 1.0f;
                        }

                        if ((buttons & (U_JPAD | U_CBUTTONS)) != 0)
                        {
                            moveData.zoomInFovPersec = 1.0f;
                        }
                    }

                    moveData.crouchDown = (bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) == 0)
                        && (g_CurrentPlayer->insightaimmode)
                        && ((buttons & (D_JPAD | D_CBUTTONS)));

                    moveData.crouchUp = (bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) == 0)
                        && (g_CurrentPlayer->insightaimmode)
                        && ((~buttons & (U_JPAD | U_CBUTTONS)));

                    moveData.rLeanLeft = (g_CurrentPlayer->insightaimmode)
                        && ((buttons & (L_JPAD | L_CBUTTONS)));

                    moveData.rLeanRight = (g_CurrentPlayer->insightaimmode)
                        && ((buttons & (R_JPAD | R_CBUTTONS)));

                    if (
                        ((((buttons & invButtons) != 0) && (((buttons & ~oldbuttons) & B_BUTTON) != 0))
                            || ((buttons & B_BUTTON) && (((buttons & ~oldbuttons) & invButtons) != 0)))
                        && (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_REMOTEMINE))
                    {
                        moveData.detonating = 1;
                        moveData.weaponBackOffset = 0;
                        moveData.weaponForwardOffset = 0;
                        moveData.btap = 0;
                    }

                    if ((g_PlayerIsInTank == 1) && (g_CurrentPlayer->insightaimmode))
                    {
                        if (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_TANKSHELLS)
                        {
                            moveData.controlStickXRaw = 0;
                            moveData.canTurnTank = 1;
                        }
                        else if ((moveData.tankTurnLeftSpeed == 0) && (moveData.tankTurnRightSpeed == 0))
                        {
                            if (moveData.aimTurnLeftSpeed > 0)
                            {
                                moveData.tankTurnLeftSpeed = moveData.aimTurnLeftSpeed;
                            }

                            if (moveData.aimTurnRightSpeed > 0)
                            {
                                moveData.tankTurnRightSpeed = moveData.aimTurnRightSpeed;
                            }
                        }

                        moveData.aimTurnLeftSpeed = 0;
                        moveData.aimTurnRightSpeed = 0;
                    }
                }
            }

            moveData.triggerOn = ((buttons & shootButtons)  != 0)
                && (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
                && ((buttons & invButtons) == 0);

            /* 1.2 and 1.4 */
            if (cur_player_get_control_type() == CONTROLLER_CONFIG_SOLITARE || cur_player_get_control_type() == CONTROLLER_CONFIG_GOODNIGHT)
            {
                moveData.disableLookAhead = 1;
            }
        }
    }

#ifndef TARGET_N64
    /* For non-N64 devices the port drives movement and look itself: the move
     * stick walks and strafes, the look stick adds turn and pitch in degrees,
     * and the control style's own turn and step output is zeroed so nothing
     * counts twice. The mouse keeps its own path and is never touched here. */
    {
        extern s32 portInputPadIsN64(s32 slot);
        extern s32 portJoyTwinStickRead(s32 pad, s8 *x, s8 *y, s32 muteC, s32 lookOnPad1);
        extern s32 portBondControlsLive(void);
        const s32 pl = get_cur_playernum();

        g_portPadLookYawDeg = 0.0f;
        g_portPadLookPitchDeg = 0.0f;
        g_portPadTankSteer = 0.0f;
        port_aim_look_on = FALSE;
        if (!portInputPadIsN64(pl) && portBondControlsLive() && g_PlayerIsInTank == 1)
        {   /* Tank: the move stick steers and drives the hull, the look stick
             * or mouse turns the turret and pitches the gun. Steer through the
             * tank filter's own input, never canNaturalTurn, because its
             * speedtheta drives the hull and the mouse would then steer it. */
            s8 lx = 0;
            s8 ly = 0;
            f32 t = (f32) moveData.controlStickXSafe / 70.0f;
            s32 kx = 0;
            s32 ky = 0;

            {   /* READ THE LOOK FIRST: both branches below want it, and in aim
                 * mode it is what the crosshair follows instead of the Move
                 * input. Keyboard Look keys stand in for a right stick. */
                extern s32 portVideoLookDigital(s32 *x, s32 *y);

                if (pl == 0 && portVideoLookDigital(&kx, &ky)) /* the keyboard is player 1's */
                {
                    lx = (s8) kx;
                    ly = (s8) ky;
                }
                else
                {
                    (void) portJoyTwinStickRead(pl, &lx, &ly, 1, 0);
                }
            }

            if (t > 1.0f) { t = 1.0f; }
            if (t < -1.0f) { t = -1.0f; }
            /* signed square and the fov ratio: the SAME shape the on-foot
             * turn uses, so the hull answers a stick like the body does */
            t = (t >= 0.0f) ? (t * t) : -(t * t);
            g_portPadTankSteer = t * (viGetFovY() / GE_DEFAULT_FOV);

            moveData.analogWalk = moveData.controlStickYSafe;   /* drive */
            moveData.canLookAhead = 1;   /* ...and the gate that lets it drive:
                                          * 1.2 / 1.4 never set it, so the tank sat
                                          * still on those styles */
            moveData.analogStrafe = 0;   /* NOT the turret, NOT a sidestep */
            moveData.analogPitch = 0;    /* NOT the gun's pitch either: 1.2 / 1.4
                                          * would otherwise pitch the view from this
                                          * same Y (W drove AND looked up) */
            /* ...and the hull turns from the MOVE stick only: 2.2 / 2.4 would
             * write the Look stick's X here and steer the hull with it. Port-
             * driven players are folded to 1.x so this is a safety net, and for
             * other styles it changes nothing. */
            moveData.analogTurn = moveData.controlStickXSafe;
            moveData.digitalStepLeft = 0;
            moveData.digitalStepRight = 0;
            moveData.digitalStepForward = 0;
            moveData.digitalStepBack = 0;
            /* THE MOVE INPUT NEVER TURNS THE TURRET, aiming or not: in 1.1 the
             * Move keys are bound to the C buttons, which set the turret's
             * tankTurnLeft/RightSpeed, so clear them for both branches. */
            moveData.tankTurnLeftSpeed = 0;
            moveData.tankTurnRightSpeed = 0;
            if (g_CurrentPlayer->insightaimmode)
            {   /* AIMING IN A TANK: the crosshair follows the LOOK input, and
                 * the Move input's crosshair position and edge-turn values are
                 * dropped so pressing a direction doesn't snap the aim. With
                 * the tank weapon in hand there is no crosshair (aiming moves
                 * the turret), so the aim position stays at zero. */
                port_aim_look_x = portTankTurretWeaponHeld() ? 0 : lx;
                port_aim_look_y = portTankTurretWeaponHeld() ? 0 : ly;
                port_aim_look_on = TRUE;
                portAimEdgeTurnFromLook(&moveData, (s8) port_aim_look_x, (s8) port_aim_look_y);
                /* in a tank the edge turn moves the TURRET, as the vanilla
                 * tank+aim block does (aimTurn* -> tankTurn*), not the hull.
                 * With the tank weapon the aim position is 0, so both are 0. */
                moveData.tankTurnLeftSpeed = moveData.aimTurnLeftSpeed;
                moveData.tankTurnRightSpeed = moveData.aimTurnRightSpeed;
                moveData.aimTurnLeftSpeed = 0;
                moveData.aimTurnRightSpeed = 0;
            }
            else
            {   /* Outside aim mode, also clear the C-button pitch (speedVerta*)
                 * and turret turn, which the Move keys still drive in 1.1, so
                 * the Move input only steers and drives the hull; the turret
                 * and gun pitch belong to the Look input. */
                moveData.speedVertaUp = 0;
                moveData.speedVertaDown = 0;
            }

            /* The Look stick turns the turret and pitches the gun under the
             * same rule as the mouse: with the tank weapon aiming still moves
             * the turret; with any other gun aim mode belongs to the crosshair. */
            if ((!g_CurrentPlayer->insightaimmode || portTankTurretWeaponHeld())
                && (lx != 0 || ly != 0))
            {
                const f32 dead = 8.0f;
                f32 fx = (f32) lx;
                f32 fy = (f32) ly;

                fx = (fx > dead) ? (fx - dead) : ((fx < -dead) ? (fx + dead) : 0.0f);
                fy = (fy > dead) ? (fy - dead) : ((fy < -dead) ? (fy + dead) : 0.0f);
                fx = (fx * ((fx < 0.0f) ? -fx : fx)) / ((85.0f - dead) * (85.0f - dead));
                fy = (fy * ((fy < 0.0f) ? -fy : fy)) / ((85.0f - dead) * (85.0f - dead));
                g_portPadLookYawDeg = fx * PORT_PADLOOK_YAW_DEG_PER_SEC * g_GlobalTimerDelta / 60.0f;
                g_portPadLookPitchDeg = fy * PORT_PADLOOK_PITCH_DEG_PER_SEC * g_GlobalTimerDelta / 60.0f;
                /* PITCH SIGN: vv_verta counts downward, and the Look stick
                 * reports the opposite sign to the mouse, so it negates exactly
                 * where the mouse doesn't. Beware: invertPitch == 1 means the
                 * setting is UPRIGHT (it is `..._inverted() == 0`). */
                if (moveData.invertPitch)
                {
                    g_portPadLookPitchDeg = -g_portPadLookPitchDeg;
                }
            }
        }
        else if (!portInputPadIsN64(pl) && portBondControlsLive()
            && g_CurrentPlayer->insightaimmode)
        {   /* AIM MODE keeps its own handling (crosshair, edge-turn, duck), and
             * the MOVE input leans while aiming, as C-left/C-right do on a
             * console, so no separate lean bind is needed and pads without C
             * buttons can lean. Uses the twin-stick +-30 of 85 threshold and
             * ORs in, so the vanilla C-button lean still works. */
            extern s32 portJoyPortHeld(s32 act);

            if (moveData.controlStickXSafe < -30)
            {
                moveData.rLeanLeft = 1;
            }
            else if (moveData.controlStickXSafe > 30)
            {
                moveData.rLeanRight = 1;
            }
            /* ...and pulling it BACK crouches, as the 2.x Move stick does on a
             * console, for pads and keyboards alike. ORs in with the C-down
             * line, with the same -30 threshold and DISABLE_CROUCH gate as the
             * vanilla 2.x line. */
            if (moveData.controlStickYSafe < -30
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) == 0)
            {
                moveData.crouchDown = 1;
            }

            /* THE MOVE INPUT MUST NOT AIM: in aim mode the crosshair sits at the
             * stick's position, so a full-deflection movement key threw it to
             * the screen edge. For port-driven devices the Move input only
             * leans and crouches, the LOOK input places the crosshair, and the
             * edge-turn values are re-derived later from where it actually is. */
            {
                extern s32 portVideoLookDigital(s32 *x, s32 *y);
                s8 ax = 0;
                s8 ay = 0;
                s32 kx = 0;
                s32 ky = 0;

                if (pl == 0 && portVideoLookDigital(&kx, &ky)) /* the keyboard is player 1's */
                {
                    ax = (s8) kx;
                    ay = (s8) ky;
                }
                else
                {
                    (void) portJoyTwinStickRead(pl, &ax, &ay, 1, 0);
                }
                /* RECORDED, not applied here: the invert-pitch step further down
                 * re-derives controlStickYRaw from the raw pad stick and would
                 * overwrite it, so it is applied after that swap instead. */
                port_aim_look_x = ax;
                port_aim_look_y = ay;
                port_aim_look_on = TRUE;
                if (portAimLockActive())
                {   /* AIM LOCK turns the view through the natural-look terms,
                     * which still held the Move stick, so feed them the Look
                     * input instead (same gate and dead band as on-foot look);
                     * the Move input keeps only its lean, and there is no edge
                     * band because the crosshair doesn't move under the lock. */
                    s32 gx = ax;
                    s32 gy = ay;
                    s32 mx;
                    s32 my;

                    if (gx > 85) gx = 85; else if (gx < -85) gx = -85;
                    if (gy > 85) gy = 85; else if (gy < -85) gy = -85;
                    mx = (gx < 0) ? -gx : gx;
                    my = (gy < 0) ? -gy : gy;
                    if (mx + my > 130)
                    {
                        gx = gx * 130 / (mx + my);
                        gy = gy * 130 / (mx + my);
                    }
                    moveData.analogTurn = (gx < -5) ? gx + 5 : ((gx > 5) ? gx - 5 : 0);
                    moveData.analogPitch = (gy < -5) ? gy + 5 : ((gy > 5) ? gy - 5 : 0);
                    portAimEdgeTurnFromLook(&moveData, 0, 0);
                }
                else
                {
                    portAimEdgeTurnFromLook(&moveData, ax, ay);
                }
            }
        }
        else if (!portInputPadIsN64(pl) && portBondControlsLive() && g_PlayerIsInTank != 1)
        {
            s8 lx = 0;
            s8 ly = 0;

            /* MOVE: the pad-1 stick is already the Move stick (a pad's
             * move_* rows and the keyboard's analog_* binds both land in it),
             * so this only has to stop the style reading it as TURN. */
            moveData.analogStrafe = moveData.controlStickXSafe;
            moveData.analogWalk = moveData.controlStickYSafe;
            moveData.analogTurn = 0;
            moveData.analogPitch = 0;
            /* TURN ANALOG STRAFING ON: despite its name, canTurnTank gates
             * `speedsideways = analogStrafe / 70` on foot, and the 1.x styles
             * leave it clear, so without this a port-driven player could not
             * strafe. Setting it is the vanilla 2.x mechanism; on foot it has
             * no other effect. */
            moveData.canTurnTank = 1;
            /* ...AND FORWARD: `speedforwards = analogWalk / 70` is gated on
             * canLookAhead, which 1.2 / 1.4 leave clear, so set it here too or
             * those saves can't walk with the left stick. Look Ahead also keys
             * off this flag. */
            moveData.canLookAhead = 1;
            /* the C buttons keep their aim-mode jobs (lean, zoom, duck); what
             * they lose is MOVING you, which is now the Move stick's */
            moveData.digitalStepLeft = 0;
            moveData.digitalStepRight = 0;
            moveData.digitalStepForward = 0;
            moveData.digitalStepBack = 0;
            /* ...and the 1.1 / 1.3 pitch on C-up / C-down, which made W and S
             * tilt the camera instead of walking. In aim mode those buttons
             * zoom and duck, which is why this is the NOT-aiming branch. */
            moveData.speedVertaUp = 0;
            moveData.speedVertaDown = 0;

            /* DIGITAL MOVE uses the C buttons' own steps: a D-pad or Move key
             * sets the vanilla digitalStep flags (full forward plus full
             * sideways on diagonals, with the run boost) instead of a stick
             * value the octagon gate would cut down. The analog override on
             * that axis is switched off, the other axis stays analog, and a
             * real stick is untouched. */
            {
                extern s32 portInputDigitalMove(s32 slot, s32 *dx, s32 *dy);
                s32 ddx = 0;
                s32 ddy = 0;

                if (portInputDigitalMove(pl, &ddx, &ddy))
                {
                    if (ddy != 0)
                    {
                        moveData.digitalStepForward = (ddy > 0);
                        moveData.digitalStepBack = (ddy < 0);
                        moveData.canLookAhead = 0;
                        sPortDigitalWalk = 1; /* Look Ahead still arms */
                    }
                    if (ddx != 0)
                    {
                        moveData.digitalStepRight = (ddx > 0);
                        moveData.digitalStepLeft = (ddx < 0);
                        moveData.canTurnTank = 0;
                    }
                }
            }

            /* LOOK: the Look stick as degrees for this tick, unswapped
             * (lookOnPad1 = 0) and with muteC so it doesn't also fire C
             * buttons. A keyboard's Look keys (unbound by default) stand in for
             * a right stick with the same ramp and rates, ORed in with any pad. */
            {
                extern s32 portVideoLookDigital(s32 *x, s32 *y);
                s32 kx = 0;
                s32 ky = 0;

                if (pl == 0 && portVideoLookDigital(&kx, &ky)) /* the keyboard is player 1's */
                {
                    lx = (s8) kx;
                    ly = (s8) ky;
                }
            }
            if ((lx != 0 || ly != 0 || portJoyTwinStickRead(pl, &lx, &ly, 1, 0)) && !portPadLookDirect())
            {   /* Feed the look stick through the N64 stick's dead band and
                 * natural-look path (squared response, fov scaling, gun sway),
                 * the feel players preferred. The flags are forced only while
                 * the stick is deflected and the mouse is untouched. */
                static u8 sPadLookNatural[4];
                s32 gx = lx;
                s32 gy = ly;
                s32 ax;
                s32 ay;
                s32 live;

                if (gx > 85) gx = 85; else if (gx < -85) gx = -85;
                if (gy > 85) gy = 85; else if (gy < -85) gy = -85;
                ax = (gx < 0) ? -gx : gx;
                ay = (gy < 0) ? -gy : gy;
                if (ax + ay > 130)
                {
                    gx = gx * 130 / (ax + ay);
                    gy = gy * 130 / (ax + ay);
                }
                gx = (gx < -5) ? gx + 5 : ((gx > 5) ? gx - 5 : 0);
                gy = (gy < -5) ? gy + 5 : ((gy > 5) ? gy - 5 : 0);
                live = (gx != 0 || gy != 0);
                if (live || sPadLookNatural[pl & 3])
                {
                    moveData.analogTurn = gx;
                    moveData.analogPitch = gy;
                    moveData.canNaturalTurn = 1;
                    moveData.canNaturalPitch = 1;
                }
                sPadLookNatural[pl & 3] = (u8) live;
            }
            else if (lx != 0 || ly != 0)
            {   /* GE_PADLOOK=direct: the older degrees path, kept for A/B */
                const f32 dead = 8.0f;   /* of 85 */
                f32 fx = (f32) lx;
                f32 fy = (f32) ly;

                fx = (fx > dead) ? (fx - dead) : ((fx < -dead) ? (fx + dead) : 0.0f);
                fy = (fy > dead) ? (fy - dead) : ((fy < -dead) ? (fy + dead) : 0.0f);
                /* SQUARED response: a small lean aims, a full push spins.
                 * Linear felt twitchy at the centre and slow at the rim. */
                fx = (fx * ((fx < 0.0f) ? -fx : fx)) / ((85.0f - dead) * (85.0f - dead));
                fy = (fy * ((fy < 0.0f) ? -fy : fy)) / ((85.0f - dead) * (85.0f - dead));
                g_portPadLookYawDeg = fx * PORT_PADLOOK_YAW_DEG_PER_SEC * g_GlobalTimerDelta / 60.0f;
                g_portPadLookPitchDeg = fy * PORT_PADLOOK_PITCH_DEG_PER_SEC * g_GlobalTimerDelta / 60.0f;
                /* PITCH SIGN: vv_verta counts downward, and the Look stick
                 * reports the opposite sign to the mouse, so it negates exactly
                 * where the mouse doesn't. Beware: invertPitch == 1 means the
                 * setting is UPRIGHT (it is `..._inverted() == 0`). */
                if (moveData.invertPitch)
                {
                    g_portPadLookPitchDeg = -g_portPadLookPitchDeg;
                }
            }
        }
        else if (pl == 0 && portInputPadIsN64(0) && portBondControlsLive() && g_PlayerIsInTank != 1
            && !g_CurrentPlayer->insightaimmode)
        {   /* G081: the keyboard shares player 1's slot, and a raphnet there makes
             * the slot N64, so the branch above never runs for it. The Move keys
             * (W / A / S / D) walk and strafe through Rare's own C-button steps;
             * the raphnet's stick and C buttons keep their N64 meaning. */
            extern void portVideoKbdDigitalMove(s32 *dx, s32 *dy);
            s32 kx = 0;
            s32 ky = 0;

            portVideoKbdDigitalMove(&kx, &ky);
            if (ky != 0)
            {
                moveData.digitalStepForward = (ky > 0);
                moveData.digitalStepBack = (ky < 0);
                /* the raphnet's stick sets canLookAhead even at rest, and its
                 * analogWalk (0) then overwrites the step's forward speed */
                moveData.canLookAhead = 0;
            }
            if (kx != 0)
            {
                moveData.digitalStepRight = (kx > 0);
                moveData.digitalStepLeft = (kx < 0);
            }
        }
        else if (pl == 0 && portInputPadIsN64(0) && portBondControlsLive() && g_PlayerIsInTank != 1
            && g_CurrentPlayer->insightaimmode)
        {   /* While aiming, the Move keys do the C-button jobs they stand in for on
             * an N64 slot: A / D lean, S ducks, and W / S zoom a sniper or camera. */
            extern void portVideoKbdDigitalMove(s32 *dx, s32 *dy);
            const s32 nocrouch = bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) != 0;
            s32 kx = 0;
            s32 ky = 0;

            portVideoKbdDigitalMove(&kx, &ky);
            if (kx < 0)
            {
                moveData.rLeanLeft = 1;
            }
            else if (kx > 0)
            {
                moveData.rLeanRight = 1;
            }
            if (ky < 0)
            {
                if (nocrouch)
                {
                    moveData.zoomOutFovPersec = 1.0f;
                }
                else
                {
                    moveData.crouchDown = 1;
                }
            }
            else if (ky > 0 && nocrouch)
            {
                moveData.zoomInFovPersec = 1.0f;
            }
        }
        else if (!portInputPadIsN64(pl) && g_portBondFrozenTick != 0 && g_PlayerIsInTank != 1
            && g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0
            && g_CurrentPlayer->bonddead == FALSE
            && (getPlayerCount() == 1 || (g_stopPlayFlag == 0 && g_gameOverFlag == 0))
            && lvlGetControlsLockedFlag() == 0 && disablePlayerActionsWhenPausedOrInMpMenu())
        {   /* CUTSCENE RUN-UP: on 2.x Rare reads the second pad even while the camera is
             * frozen, so holding it through an intro builds speed and the run boost.
             * A port-driven player gets the same from the Move stick or keys. */
            extern s32 get_is_ramrom_flag(void);

            if (get_is_ramrom_flag() == 0)
            {
                const s32 mx = joyGetStickX(pl);
                const s32 my = joyGetStickY(pl);

                /* the same 5-unit dead zone as the 2.x second-pad read */
                moveData.analogStrafe = (mx < -5) ? mx + 5 : ((mx > 5) ? mx - 5 : 0);
                moveData.analogWalk = (my < -5) ? my + 5 : ((my > 5) ? my - 5 : 0);
                moveData.canTurnTank = 1;
                moveData.canLookAhead = !g_CurrentPlayer->insightaimmode;
                {   /* keys and D-pad as Rare's steps, as in live play: a diagonal is full
                     * forward plus full sideways and builds the run boost */
                    extern s32 portInputDigitalMove(s32 slot, s32 *dx, s32 *dy);
                    s32 ddx = 0;
                    s32 ddy = 0;

                    if (portInputDigitalMove(pl, &ddx, &ddy))
                    {
                        if (ddy != 0)
                        {
                            moveData.digitalStepForward = (ddy > 0);
                            moveData.digitalStepBack = (ddy < 0);
                            moveData.canLookAhead = 0;
                        }
                        if (ddx != 0)
                        {
                            moveData.digitalStepRight = (ddx > 0);
                            moveData.digitalStepLeft = (ddx < 0);
                            moveData.canTurnTank = 0;
                        }
                    }
                }
            }
        }

        /* zoom gets its own binds, since modern pads have no C buttons: read
         * as a LEVEL, so a stick zooms by how far it is pushed and a key
         * zooms at the 1.x C-button rate of 1.0. As in vanilla, only the
         * sniper rifle and camera zoom, and only while aiming. */
        if (portBondControlsLive() && g_PlayerIsInTank != 1 && g_CurrentPlayer->insightaimmode)
        {   /* any slot class: the keyboard rides player 1's slot even when it is N64-class,
             * and the faster of the C buttons and the bind wins */
            extern f32 portJoyPortLevel(s32 act);
            extern s32 portJoyPortActZoomIn(void);
            extern s32 portJoyPortActZoomOut(void);
            const f32 zi = portJoyPortLevel(portJoyPortActZoomIn());
            const f32 zo = portJoyPortLevel(portJoyPortActZoomOut());

            if (zi > moveData.zoomInFovPersec)
            {
                moveData.zoomInFovPersec = zi;
            }
            if (zo > moveData.zoomOutFovPersec)
            {
                moveData.zoomOutFovPersec = zo;
            }
        }
    }
#endif

    g_CurrentPlayer->field_D0 = 0;
#ifndef TARGET_N64
    { /* Port suite use (joy_port.c) = the game's own B tap: tank enter/exit
       * below, else field_D0 -> lv.c interact (its reload fallback is
       * suppressed for this tap via portJoyPortUseNoReload) */
        extern s32 portJoyPortUseTap(void);
        extern s32 portJoyPortDetonate(s32 minesInHand);
        /* DETONATE first: the Port detonate bind (and use/reload with remote
         * mines in hand) acts as the vanilla A+B detonate chord, and taking the
         * use latch here stops it also becoming a B tap below. */
        if (portJoyPortDetonate(getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_REMOTEMINE))
        {
            moveData.detonating = 1;
            moveData.weaponBackOffset = 0;
            moveData.weaponForwardOffset = 0;
            moveData.btap = 0;
        }
        else if (portJoyPortUseTap())
        {
            moveData.btap = 1;
        }
    }
#endif

    if (moveData.btap)
    {
        /* If Bond is in the tank and pressed B, then exit. */
        if (g_PlayerIsInTank == 1)
        {
            spF4 = (struct TankRecord *)g_PlayerTankProp->obj;
            spF4->unkD8 = get_ammo_count_for_weapon(ITEM_TANKSHELLS);

            add_ammo_to_weapon(ITEM_TANKSHELLS, 0);
            bondinvRemoveItemByID(ITEM_TANKSHELLS);

            if (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_TANKSHELLS)
            {
                spF4->unkD8 += get_ammo_in_hands_magazine(GUNRIGHT);
                autoadvance_on_deplete_all_ammo();
            }

            spF4->is_firing_tank = 0;
            g_PlayerIsInTank = 0;
            g_CurrentPlayer->speedsideways = 0;
            g_CurrentPlayer->speedforwards = 0;
            g_CurrentPlayer->speedtheta = 0;

            for (i_0=0; i_0<3; i_0++)
            {
                g_CurrentPlayer->bondshotspeed.f[i_0] = 0;
            }

            g_CurrentPlayer->crouchpos = CROUCH_STAND;
        }
        /* If Bond is standing on the tank and pressed B, enter the tank. */
        else if (g_PlayerTankProp != NULL
            && g_PlayerTankProp->type == PROP_TYPE_OBJ
            && g_PlayerTankProp->obj->type == PROPDEF_TANK
            && g_BondCanEnterTank)
        {
            spEC = (struct TankRecord *)g_PlayerTankProp->obj;

            bondinvAddInvItem(ITEM_TANKSHELLS);
            add_ammo_to_weapon(ITEM_TANKSHELLS, spEC->unkD8);
            spEC->unkD8 = 0;
            g_TankTurretVerticalAngle = spEC->turret_vertical_angle;
            g_TankTurretVerticalAngleRelated = g_TankTurretVerticalAngle / TANK_VERT_ANGLE_FACTOR;
            g_TankTurretAngle = spEC->turret_orientation_angle;
            g_TankTurretOrientationAngleRad = spEC->turret_orientation_angle;
            g_TankTurretOrientationAngleDeg = g_TankTurretOrientationAngleRad / TANK_VERT_ANGLE_RAD_FACTOR;
            tank_turret_turn_speed = 0;
            g_TankOrientationAngle = spEC->tank_orientation_angle;
            g_TankTurnSpeed = 0;
            g_PlayerIsInTank = 1;
            g_EnterTankAudioState = TANK_RUN_STATE_NOT_RUNNING;
            g_CurrentPlayer->speedsideways = 0;
            g_CurrentPlayer->speedforwards = 0;
            g_CurrentPlayer->speedtheta = 0;
#ifndef TARGET_N64
            /* a Look Ahead centre still running at the B press would stay
             * latched for the whole tank ride and block the gun's pitch, so
             * clear it */
            g_CurrentPlayer->docentreupdown = FALSE;
#endif
            g_CurrentPlayer->crouchpos = CROUCH_HALF;
            g_TankEnteringSitHeight = 0;
            g_TankEnteringSitHeightRemain = 1.0f;
            g_TankEnterBondHorizAngleDeg = g_CurrentPlayer->vv_theta;
            g_TankEnterBondVertAngleDeg = g_CurrentPlayer->vv_verta;
            g_EnterTankCoord.f[0] = g_CurrentPlayer->field_488.collision_position.f[0];
            g_EnterTankCoord.f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
            g_EnterTankCoord.f[2] = g_CurrentPlayer->field_488.collision_position.f[2];
            g_TankDamagePenaltyTicks = 0;

            bondviewTankModelRotationRelated();
        }
        else
        {
            g_CurrentPlayer->field_D0 = 1;
        }
    }

    if (moveData.invertPitch == 0)
    {
        f32 ftemp_nostack_spE8;

        moveData.controlStickYRaw = (s32) -stick_y;
        moveData.analogPitch = -moveData.analogPitch;
        ftemp_nostack_spE8 = moveData.speedVertaDown;
        moveData.speedVertaDown = moveData.speedVertaUp;
        moveData.speedVertaUp = ftemp_nostack_spE8;
    }

#ifndef TARGET_N64
    if (port_aim_look_on)
    {   /* the crosshair follows the LOOK input, not the Move input (see the aim
         * branch above), applied AFTER the invert swap so an upward look reads
         * as a negative raw Y, as -stick_y does on the line above. */
        moveData.controlStickXRaw = port_aim_look_x;
        moveData.controlStickYRaw = moveData.invertPitch ? port_aim_look_y : -port_aim_look_y;
    }
#endif

#ifndef TARGET_N64
    /* AIM edge-turn, mouse only: holding the crosshair at the edge of the aim
     * box rotates the camera, using last tick's combined swivel converted back
     * to stick units. Pure-stick aim keeps its native >60 feel; placed after
     * the invert swap so signs match the speedVerta convention. */
    if (g_CurrentPlayer->controldef == CONTROLLER_CONFIG_KISSY
        && moveData.canManualAim && g_PlayerIsInTank != 1
        && !portAimLockActive()) /* AIM LOCK: the view turns directly, no edge band */
    {
        /* Uses the 1964 injector's band: scrolling starts once the crosshair
         * passes 0.72 of its reach and ramps linearly to full at the edge. */
#define AIMSCROLL_ONSET 0.72f
        f32 equiv;
        f32 push;

        /* the band belongs to the mouse; a nonzero offset no longer implies
         * mouse input since it is seeded at every aim entry */
        if (get_cur_playernum() == 0 && g_bondviewAimMouseMoved && g_bondviewMouseSwivelX != 0.0f) /* MP: P1's mouse crosshair only */
        {
            /* swivel -> stick units; the wide-aim reach (portAimReachX)
             * counts as the full 80 so the band stays the outer 28% */
            equiv = g_bondviewAimSwivelPosX * (80.0f / (0.65f * portAimReachX()));
            push = (((equiv < 0.0f) ? -equiv : equiv) / 80.0f - AIMSCROLL_ONSET)
                   / (1.0f - AIMSCROLL_ONSET);
            if (push > 0.0f)
            {
                if (push > 1.0f) push = 1.0f;
                if (equiv > 0.0f)
                {
                    if (push > moveData.aimTurnRightSpeed) moveData.aimTurnRightSpeed = push;
                }
                else
                {
                    if (push > moveData.aimTurnLeftSpeed) moveData.aimTurnLeftSpeed = push;
                }
            }
        }

        if (get_cur_playernum() == 0 && g_bondviewAimMouseMoved && g_bondviewMouseSwivelY != 0.0f) /* MP: P1's mouse crosshair only, like X */
        {
            equiv = g_bondviewAimSwivelPosY * (80.0f / 0.65f);
            push = (((equiv < 0.0f) ? -equiv : equiv) / 80.0f - AIMSCROLL_ONSET)
                   / (1.0f - AIMSCROLL_ONSET);
            if (push > 0.0f)
            {
                if (push > 1.0f) push = 1.0f;
                if (equiv > 0.0f)
                {
                    if (push > moveData.speedVertaDown) moveData.speedVertaDown = push;
                }
                else
                {
                    if (push > moveData.speedVertaUp) moveData.speedVertaUp = push;
                }
            }
        }
    }
#endif

#ifndef TARGET_N64
    {   /* THE A+Z CHORD for the port binds ([input] shoot_next_prev): with
         * next weapon / next gadget held, each shoot tap steps BACK one
         * (joy_port.c) and does not fire, as A held + Z on a console */
        extern s32 portJoyPortShootChord(s32 held);
        u16 shoot = (cur_player_get_control_type() == CONTROLLER_CONFIG_KISSY
            || cur_player_get_control_type() == CONTROLLER_CONFIG_GOODNIGHT) ? A_BUTTON : Z_TRIG;

        if (portJoyPortShootChord((buttons & shoot) != 0))
        {
            moveData.triggerOn = 0;
        }
    }
#endif

    if (bondviewGetIfCurrentPlayerDamageShowTime() && getPlayerCount() == 1)
    {
        moveData.triggerOn = 0;
    }

    gunTickGameplay(moveData.triggerOn);

    if (bondviewGetVisibleToGuardsFlag()
        && (get_hands_firing_status(GUNRIGHT)
            || get_hands_firing_status(GUNLEFT)))
    {
        noiseRadius = 0;

        if (get_hands_firing_status(GUNRIGHT) && getCurrentPlayerNoise(GUNRIGHT) > noiseRadius)
        {
            noiseRadius = getCurrentPlayerNoise(GUNRIGHT);
        }

        if (get_hands_firing_status(GUNLEFT) && noiseRadius < getCurrentPlayerNoise(GUNLEFT))
        {
            noiseRadius = getCurrentPlayerNoise(GUNLEFT);
        }

        chrCheckGuardsHeardSound(noiseRadius);
    }

    gunSetSightVisible(GUNSIGHTREASON_NOTAIMING, moveData.aiming);

    if (moveData.zoomOutFovPersec > 0)
    {
        camera_sniper_zoom_out(moveData.zoomOutFovPersec);
    }

    if (moveData.zoomInFovPersec > 0)
    {
        camera_sniper_zoom_in(moveData.zoomInFovPersec);
    }

    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
    {
        ftemp_nostack_spE0 = GE_DEFAULT_FOV;

        if (moveData.zooming)
        {
            ftemp_nostack_spE0 = get_item_in_hand_zoom();

            if (ftemp_nostack_spE0 <= 0)
            {
                ftemp_nostack_spE0 = GE_DEFAULT_FOV;
            }
        }

        bondviewTriggerWatchZoom(ftemp_nostack_spE0);
        bondviewUpdateWatchZoomIn();
    }

    if (g_PlayerIsInTank == 1)
    {
        g_TankTurretTurn = 0;

        if (g_EnterTankAudioState == TANK_RUN_STATE_RUNNING)
        {
            if (moveData.tankTurnRightSpeed > 0)
            {
                g_TankTurretTurn += g_GlobalTimerDelta * moveData.tankTurnRightSpeed * DegToRad1Fact(1);
            }
            else if (moveData.tankTurnLeftSpeed > 0)
            {
                g_TankTurretTurn -= g_GlobalTimerDelta * moveData.tankTurnLeftSpeed * DegToRad1Fact(1);
            }
            else if (moveData.canTurnTank)
            {
                targetSpeed = (f32) moveData.analogStrafe / 70.0f;

                if (targetSpeed > 1.0f)
                {
                    targetSpeed = 1.0f;
                }

                if (targetSpeed < -1.0f)
                {
                    targetSpeed = -1.0f;
                }

                g_TankTurretTurn += DegToRad1Fact(1) * targetSpeed * g_GlobalTimerDelta;
            }

#ifndef TARGET_N64
            /* PORT: mouse yaw rotates the TURRET via g_TankTurretTurn (its angle
             * relative to the hull) rather than speedtheta, which would steer
             * the tank too; stick and mouse can both contribute in one tick.
             * With the tank weapon in hand this applies in aim mode too; with
             * any other gun, aim mode belongs to the crosshair. */
            if ((!g_CurrentPlayer->insightaimmode || portTankTurretWeaponHeld())
                && g_portMouseDirect && g_portMouseYawDeg != 0.0f)
            {
                /* 1964 injector model: the same degrees the on-foot camera
                 * would take, in radians and slowed by 2.5 (a turret is not a
                 * neck), accumulated into g_TankTurretAngle. */
                g_TankTurretTurn += DegToRad1Fact(g_portMouseYawDeg * portMouseFovRatio()) / 2.5f;
            }
            else if ((!g_CurrentPlayer->insightaimmode || portTankTurretWeaponHeld())
                     && !g_portMouseDirect
                     && moveData.freeLookDx != 0.0f && g_GlobalTimerDelta > 0.0f)
            {
                f32 mouserate = moveData.freeLookDx / g_GlobalTimerDelta;

                if (mouserate > 4.0f)
                {
                    mouserate = 4.0f;
                }
                else if (mouserate < -4.0f)
                {
                    mouserate = -4.0f;
                }

                g_TankTurretTurn += DegToRad1Fact(1) * mouserate * g_GlobalTimerDelta;
            }

            /* THE LOOK STICK turns the turret too, with the same / 2.5 slow-down.
             * Deliberately a standalone if AFTER the mouse's if/else-if chain,
             * so a moving stick can never suppress legacy-mode mouse yaw. */
            if (g_portPadLookYawDeg != 0.0f)
            {
                g_TankTurretTurn += DegToRad1Fact(g_portPadLookYawDeg) / 2.5f;
            }
#endif

            if (!g_CurrentPlayer->insightaimmode)
            {
                ftemp_nostack_spD8 = 0;
                targetSpeed = 1.0f;
                ftemp_nostack_spE8 = 1.0f;

                if (moveData.canLookAhead)
                {
                    ftemp_nostack_spD8 = (f32) moveData.analogWalk / 70.0f;
                }
                else if (moveData.digitalStepForward)
                {
                    ftemp_nostack_spD8 = 1.0f;
                }
                else if (moveData.digitalStepBack)
                {
                    ftemp_nostack_spD8 = -1.0f;
                }

                if (ftemp_nostack_spD8 > 1.0f)
                {
                    ftemp_nostack_spD8 = 1.0f;
                }
                else if (ftemp_nostack_spD8 < -1.0f)
                {
                    ftemp_nostack_spD8 = -1.0f;
                }

                unadjustedTargetSpeed = ftemp_nostack_spD8 * TANK_MAX_SPEED;
                targetSpeed = unadjustedTargetSpeed;

                if (g_TankDamagePenaltyTicks > 0)
                {
                    targetSpeed = unadjustedTargetSpeed * 0.5f;
                    ftemp_nostack_spE8 = 4.0f;
                    g_TankDamagePenaltyTicks -= g_ClockTimer;
                }

                if (targetSpeed != g_CurrentPlayer->speedforwards)
                {
                    if (g_CurrentPlayer->speedforwards < targetSpeed)
                    {
                        unadjustedTargetSpeed = ((((((targetSpeed - g_CurrentPlayer->speedforwards) / 4.0f) / TANK_MAX_SPEED) + 0.5f) * ftemp_nostack_spE8 * FLOAT_TEN_A) / 60.0f);

                        g_CurrentPlayer->speedforwards += (unadjustedTargetSpeed) * g_GlobalTimerDelta;

                        if (targetSpeed < g_CurrentPlayer->speedforwards)
                        {
                            g_CurrentPlayer->speedforwards = targetSpeed;
                        }
                    }
                    else if (targetSpeed < g_CurrentPlayer->speedforwards)
                    {
                        unadjustedTargetSpeed = ((((((g_CurrentPlayer->speedforwards - targetSpeed) / 4.0f) / TANK_MAX_SPEED) + 0.5f) * ftemp_nostack_spE8 * -FLOAT_TEN_A) / 60.0f);

                        g_CurrentPlayer->speedforwards += (unadjustedTargetSpeed) * g_GlobalTimerDelta;

                        if (g_CurrentPlayer->speedforwards < targetSpeed)
                        {
                            g_CurrentPlayer->speedforwards = targetSpeed;
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (moveData.digitalStepLeft)
        {
            bondviewUpdateSpeedSideways(-1);
        }
        else if (moveData.digitalStepRight)
        {
            bondviewUpdateSpeedSideways(1);
        }
        else
        {
            bondviewUpdateSpeedSideways(0);
        }

        if (moveData.canTurnTank) // ?? not sure why this tank property is used here. Is the name wrong?
        {
            g_CurrentPlayer->speedsideways = (f32) moveData.analogStrafe / 70.0f;
        }

        if (moveData.digitalStepForward)
        {
            bondviewUpdateSpeedForwards(1);
            g_CurrentPlayer->speedmaxtime60 += g_ClockTimer;
        }
        else if (moveData.digitalStepBack)
        {
            bondviewUpdateSpeedForwards(-1);
        }
        else
        {
            bondviewUpdateSpeedForwards(0);
        }

        if (moveData.canLookAhead)
        {
            g_CurrentPlayer->speedforwards = (f32) moveData.analogWalk / 70.0f;

            if (moveData.analogWalk > 60)
            {
                g_CurrentPlayer->speedmaxtime60 += g_ClockTimer;
            }
            else
            {
                g_CurrentPlayer->speedmaxtime60 = 0;
            }
        }

        if (g_CurrentPlayer->speedforwards > 1.0f)
        {
            g_CurrentPlayer->speedforwards = 1;
        }

        if (g_CurrentPlayer->speedforwards < -1.0f)
        {
            g_CurrentPlayer->speedforwards = -1.0f;
        }

        if (g_CurrentPlayer->speedsideways > 1)
        {
            g_CurrentPlayer->speedsideways = 1;
        }

        if (g_CurrentPlayer->speedsideways < -1)
        {
            g_CurrentPlayer->speedsideways = -1;
        }

        g_CurrentPlayer->speedforwards *= 1.08f;
        g_CurrentPlayer->speedforwards *= g_CurrentPlayer->speedboost;

        if ((moveData.canLookAhead == 0) && (moveData.digitalStepForward == 0))
        {
            g_CurrentPlayer->speedmaxtime60 = 0;
        }

        if (moveData.rLeanLeft)
        {
            currentPlayerSetSwayTarget(-1);
        }
        else if (moveData.rLeanRight)
        {
            currentPlayerSetSwayTarget(1);
        }
        else
        {
            currentPlayerSetSwayTarget(0);
        }

#ifndef TARGET_N64
        { /* [input] no_aim_crouch: every crouchDown above is aim + down, so
           * drop it; the crouch binds below still work. */
            extern s32 portJoyPortNoAimCrouch(void);

            if (portJoyPortNoAimCrouch())
            {
                moveData.crouchDown = 0;
            }
        }
        { /* Port suite crouch toggle/hold (joy_port.c): the aim-mode duck
           * without aim mode - stand <-> squat through the same +-2 step */
            extern s32 portJoyPortCrouchStep(void);
            s32 step = portJoyPortCrouchStep();
            if (step < 0 && g_CurrentPlayer->insightaimmode
                && (buttons & (U_JPAD | U_CBUTTONS))
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_DISABLE_CROUCH) == 0)
            {   /* An N64 pad's own stand-up (aim + C-up / D-up) ends a crouch
                 * latched from the keyboard; the every-tick -1 hid it before. */
                extern s32 portInputPadIsN64(s32 slot);
                extern s32 portJoyPortCrouchRelease(void);

                if (portInputPadIsN64(get_cur_playernum()) && portJoyPortCrouchRelease())
                {
                    step = 1;
                }
            }
            if (step < 0)
            {
                moveData.crouchDown = 1;
                moveData.crouchUp = 0;
            }
            else if (step > 0)
            {
                moveData.crouchUp = 1;
                moveData.crouchDown = 0;
            }
        }
#endif

        if (moveData.crouchDown)
        {
            currentPlayerAdjustCrouchPos(-2);
        }
        else if (moveData.crouchUp)
        {
            currentPlayerAdjustCrouchPos(2);
        }
    }

    if (g_CurrentPlayer->speedmaxtime60 >= THREE_SECOND_TICKS)
    {
        if (g_CurrentPlayer->speedboost < SPEED_RUN_MAX)
        {
            g_CurrentPlayer->speedboost += (SPEED_TICK_ADJUST * g_GlobalTimerDelta);
        }

        if (g_CurrentPlayer->speedboost > SPEED_RUN_MAX)
        {
            g_CurrentPlayer->speedboost = SPEED_RUN_MAX;
        }
    }
    else
    {
        if (g_CurrentPlayer->speedboost > SPEED_REGULAR_MAX)
        {
            g_CurrentPlayer->speedboost -= (SPEED_TICK_ADJUST * g_GlobalTimerDelta);
        }

        if (g_CurrentPlayer->speedboost < SPEED_REGULAR_MAX)
        {
            g_CurrentPlayer->speedboost = SPEED_REGULAR_MAX;
        }
    }

#ifndef TARGET_N64
    /* MOUSE LOOK: styles like 1.1 Honey leave canNaturalPitch off, so force the
     * natural-look path on whenever the mouse moved; gated on canSwivelGun so
     * cutscenes, pause, menus and aim mode stay mouse-immune. */
    if (!g_portMouseDirect
        && (moveData.freeLookDx != 0.0f || moveData.freeLookDy != 0.0f) && moveData.canSwivelGun)
    {
        /* ...EXCEPT in a tank: there canNaturalTurn's speedtheta steers the
         * hull, so forcing it would let the mouse steer the tank. Turret yaw
         * goes through g_TankTurretTurn instead; pitch stays forced on. */
        if (g_PlayerIsInTank != 1)
        {
            moveData.canNaturalTurn = 1;
        }
        moveData.canNaturalPitch = 1;
    }

    /* AIM LOCK: in aim mode the stick (and legacy-mode mouse) turn and pitch
     * the view through the natural-look path while the crosshair stays put,
     * overriding styles that leave canNaturalPitch off. */
    if (portAimLockActive() && g_CurrentPlayer->insightaimmode)
    {
        moveData.canNaturalTurn = 1;
        moveData.canNaturalPitch = 1;

        if (sPortTwinFed && !sPortTwinMoveSecond)
        { /* 2.1 / 2.3: pad 1 is the MOVE stick in these styles, so point aim
           * lock's yaw at the Look stick (the second pad) instead; 2.2 / 2.4
           * already have Look on pad 1. Only this call's 2.x pass reaches here. */
            moveData.analogTurn = adjustedStickX;
        }
    }
#endif

    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
    {
        // By default the camera is pitched slightly below the horizon
        targetPitch = -4.0f;

        // lookaheadcentreenabled is always true, so this block always executes.
        if (g_CurrentPlayer->lookaheadcentreenabled)
        {
            spC0 = g_CurrentPlayer->field_488.current_tile_ptr;
            spBC = 300.0f;

            // prop, f32 *collision_radius, f32 *height, f32 *always_30
            bondviewGetCollisionRadius(g_CurrentPlayer->prop, &spA0.f[0], &spA0.f[2], &spA0.f[1]);

            spAC.f[0] = g_CurrentPlayer->field_488.collision_position.f[0] + (g_CurrentPlayer->field_488.theta_transform.f[0] * 300.0f);
            spAC.f[1] = g_CurrentPlayer->field_488.collision_position.f[1];
            spAC.f[2] = g_CurrentPlayer->field_488.collision_position.f[2] + (g_CurrentPlayer->field_488.theta_transform.f[2] * 300.0f);

            stanResetHits();

            if (stanTestLineUnobstructed(&spC0, g_CurrentPlayer->field_488.collision_position.f[0], g_CurrentPlayer->field_488.collision_position.f[2], spAC.f[0], spAC.f[2], CDTYPE_CLOSEDDOORS, spA0.f[2], spA0.f[1], 0, 1.0f))
            {
                spAC.f[1] = bondviewYPositionRelated(spC0, spAC.f[0], spAC.f[2]);
            }
            else
            {
                chrlvStanPointPointIntersection(&g_CurrentPlayer->field_488.collision_position, &g_CurrentPlayer->field_488.theta_transform, (struct coord3d *) &spAC);
                ftemp_nostack_spD0 = spAC.f[0] - g_CurrentPlayer->field_488.collision_position.f[0];
                ftemp_nostack_spCC = spAC.f[2] - g_CurrentPlayer->field_488.collision_position.f[2];
                spBC = sqrtf((ftemp_nostack_spD0 * ftemp_nostack_spD0) + (ftemp_nostack_spCC * ftemp_nostack_spCC));
                spAC.f[1] = bondviewYPositionRelated(spC0, spAC.f[0], spAC.f[2]);
            }

            if (spBC > 0)
            {
                ftemp_nostack_spC8 = spAC.f[1] - g_CurrentPlayer->stanHeight;

                if ((ftemp_nostack_spC8 > -300.0f) && (ftemp_nostack_spC8 < 500.0f))
                {
                    targetPitch = ((atan2f(ftemp_nostack_spC8, spBC) * 360.0f) / M_TAU_F) + -4.0f;

                    if (targetPitch >= 180.0f)
                    {
                        targetPitch -= 360.0f;
                    }

                    if (targetPitch > 0)
                    {
                        targetPitch *= 0.8666667f;
                    }
                }
            }
        }

        if ((g_CurrentPlayer->movecentrerelease) && (moveData.analogWalk < 40) && (moveData.analogWalk > -40))
        {
            g_CurrentPlayer->movecentrerelease = FALSE;
        }


        if (g_PlayerIsInTank == 0)
        {
#ifndef TARGET_N64
            /* The port's pitch inputs (Look stick analogPitch, direct mouse,
             * GE_PADLOOK=direct) count as manual pitch too; otherwise Look
             * Ahead never let go and held the view near level. */
            const s32 portPitch = (moveData.canNaturalPitch && moveData.analogPitch != 0)
                || (g_portMousePitchDeg != 0.0f) || (g_portPadLookPitchDeg != 0.0f);
#endif
            /**
             * If the player is giving manual pitch inputs, stop the automatic look ahead pitch adjust.
             */
            if ((moveData.speedVertaDown > 0) || (moveData.speedVertaUp > 0)
#ifndef TARGET_N64
                /* mouse pitch counts as manual input too, else auto-centre
                 * fights the mouse while walking */
                || (moveData.freeLookDy != 0.0f && moveData.canNaturalPitch)
                || portPitch
#endif
                )
            {
                g_CurrentPlayer->docentreupdown = FALSE;
                g_CurrentPlayer->prevupdown = TRUE;
                g_CurrentPlayer->automovecentre = FALSE;
#ifndef TARGET_N64
                /* ...and hold the view where it was put, as a C-button pitch
                 * does below, so a tick with no pitch input while running
                 * doesn't re-arm the centre and yank it back. Not gated on
                 * canLookAhead, since a digital walk clears that flag. */
                if (portPitch)
                {
                    g_CurrentPlayer->movecentrerelease = TRUE;
                }
                sPortLookAheadHold[get_cur_playernum() & 3] = 0.0f;   /* pitch input: the second starts again */
#endif
            }
            else
            {
#ifndef TARGET_N64
                { /* LOOK AHEAD WAITS A SECOND of walking with no pitch input
                   * before centring may start, and then releases the pitch
                   * hold. Demo playback keeps Rare's instant rule, which the
                   * recorded aim depends on. */
                    const s32 walking = ((moveData.canLookAhead) && ((moveData.analogWalk > 60) || (moveData.analogWalk < -60)))
                        || sPortDigitalWalk;
                    f32 *hold = &sPortLookAheadHold[get_cur_playernum() & 3];

                    *hold = walking ? (*hold + g_GlobalTimerDelta) : 0.0f;
                    if (*hold >= 60.0f && get_is_ramrom_flag() == 0)
                    {
                        g_CurrentPlayer->movecentrerelease = FALSE;
                    }
                }
#endif
                if (moveData.disableLookAhead)
                {
                    g_CurrentPlayer->automovecentre = FALSE;
                }
                else if (g_CurrentPlayer->automovecentreenabled)
                {
                    if (((moveData.canLookAhead) && ((moveData.analogWalk > 60) || (moveData.analogWalk < -60)))
#ifndef TARGET_N64
                        || sPortDigitalWalk /* a digital walk, as a full stick push */
#endif
                        )
                    {
                        g_CurrentPlayer->automovecentre = TRUE;
                    }

                    /**
                     * If the player's camera pitch is 5 degrees above the target pitch or 10 degrees below the target pitch,
                     * and move centre is allowed, look ahead (docentreupdown) can be activated.
                     */
                    if ((g_CurrentPlayer->automovecentre)
                        && (( ((targetPitch + 5.0f) < g_CurrentPlayer->vv_verta)) || (g_CurrentPlayer->vv_verta < (targetPitch + -FLOAT_TEN_A)))
                        && (g_CurrentPlayer->movecentrerelease == FALSE)
#ifndef TARGET_N64
                        && (sPortLookAheadHold[get_cur_playernum() & 3] >= 60.0f || get_is_ramrom_flag() != 0)
#endif
                        )
                    {
                        g_CurrentPlayer->docentreupdown = TRUE;
                    }
                }
                /**
                 * fastmovecentreenabled is never set to true, so this block can never execute. Cut option?
                 */
                else if ((g_CurrentPlayer->fastmovecentreenabled)
                    && (moveData.canLookAhead)
                    && ((moveData.analogWalk > 60) || (moveData.analogWalk < -60))
                    && (( ((targetPitch + 5.0f) < g_CurrentPlayer->vv_verta)) || (g_CurrentPlayer->vv_verta < (targetPitch + -FLOAT_TEN_A)))
                    && (g_CurrentPlayer->movecentrerelease == FALSE))
                {
                    g_CurrentPlayer->docentreupdown = TRUE;
                }

                g_CurrentPlayer->prevupdown = FALSE;
            }
        }

#ifndef TARGET_N64
        { /* diagnostic (GE_DEMODIAG, demo playback only): trace the
           * look-ahead engagement chain. */
            extern char *getenv(const char *);
            extern int printf(const char *, ...);
            extern s32 get_is_ramrom_flag(void);
            static int diag = -1;
            static u32 calls;
            if (diag < 0) diag = getenv("GE_DEMODIAG") != NULL;
            if (diag && get_is_ramrom_flag() && (calls++ % 30) == 0)
            {
                printf("ge: lookahead: walk=%d canLA=%d amcEn=%d amc=%d docentre=%d vDn=%.2f vUp=%.2f mdy=%.3f dis=%d\n",
                       moveData.analogWalk, moveData.canLookAhead,
                       g_CurrentPlayer->automovecentreenabled, g_CurrentPlayer->automovecentre,
                       g_CurrentPlayer->docentreupdown,
                       moveData.speedVertaDown, moveData.speedVertaUp,
                       moveData.freeLookDy, moveData.disableLookAhead);
            }
        }
#endif

        /**
         * If look ahead (docentreupdown) is active, adjust the player's camera pitch to the target pitch.
         */
        if (g_CurrentPlayer->docentreupdown)
        {
            if (g_PlayerIsInTank == 0)
            {
                ftemp_nostack_spB8 = (g_CurrentPlayer->speedverta * g_CurrentPlayer->speedverta * 0.5f) / 0.05f;

                if ((targetPitch + ftemp_nostack_spB8) < g_CurrentPlayer->vv_verta)
                {
                    bondviewCurrentPlayerUpdateSpeedVerta(1.0f);
                }
                else if (g_CurrentPlayer->vv_verta < (targetPitch - ftemp_nostack_spB8))
                {
                    bondviewCurrentPlayerUpdateSpeedVerta(-1.0f);
                }
                else
                {
                    bondviewCurrentPlayerUpdateSpeedVerta(0);
                }

#ifndef TARGET_N64
                /* ...and swings back at a third of Rare's speed;
                 * demo playback keeps the full speed */
                ftemp_nostack_spB8 = g_CurrentPlayer->vv_verta
                    + (2.0f * (g_CurrentPlayer->speedverta * g_GlobalTimerDelta)) * ((get_is_ramrom_flag() != 0) ? 1.0f : (1.0f / 3.0f));
#else
                ftemp_nostack_spB8 = g_CurrentPlayer->vv_verta + (2.0f * (g_CurrentPlayer->speedverta * g_GlobalTimerDelta));
#endif

                if ((targetPitch < g_CurrentPlayer->vv_verta) && (targetPitch < ftemp_nostack_spB8))
                {
                    g_CurrentPlayer->vv_verta = ftemp_nostack_spB8;
                }
                else if ((g_CurrentPlayer->vv_verta < targetPitch) && (ftemp_nostack_spB8 < targetPitch))
                {
                    g_CurrentPlayer->vv_verta = ftemp_nostack_spB8;
                }
                else
                {
                    g_CurrentPlayer->vv_verta = targetPitch;
                    g_CurrentPlayer->speedverta = 0;

                    if (g_CurrentPlayer->prevupdown == FALSE)
                    {
                        g_CurrentPlayer->docentreupdown = FALSE;
                    }
                }
            }
        }
        else
        {
            if (moveData.canNaturalPitch)
            {
                ftemp_nostack_sp98 = viGetFovY() / GE_DEFAULT_FOV; /* aim speed scales vs the CONFIGURED base fov, not literal 60 */
                ftemp_nostack_sp94 = (f32) moveData.analogPitch / 70.0f;

                if (ftemp_nostack_sp94 > 1)
                {
                    ftemp_nostack_sp94 = 1;
                }
                else if (ftemp_nostack_sp94 < -1)
                {
                    ftemp_nostack_sp94 = -1;
                }

                if (ftemp_nostack_sp94 >= 0)
                {
                    ftemp_nostack_sp94 *= ftemp_nostack_sp94;
                }
                else
                {
                    ftemp_nostack_sp94 *= -ftemp_nostack_sp94;
                }

#ifndef TARGET_N64
                /* MOUSE LOOK pitch: dividing by the tick length cancels the
                 * consumer's * g_GlobalTimerDelta so motion maps 1:1 at any
                 * frame rate, and the FOV scale slows it when zoomed. SUBTRACT
                 * so mouse forward looks up under GE's stick convention. */
                if (!g_portMouseDirect
                    && moveData.freeLookDy != 0.0f && g_GlobalTimerDelta > 0.0f)
                {
                    ftemp_nostack_sp94 -= moveData.freeLookDy / g_GlobalTimerDelta;
                }
#endif

                g_CurrentPlayer->speedverta = -ftemp_nostack_sp94 * ftemp_nostack_sp98;
            }
            else if (moveData.speedVertaDown > 0)
            {
                bondviewCurrentPlayerUpdateSpeedVerta(moveData.speedVertaDown);

                // Bug? This is true for every value except exactly 60.
                if ((moveData.canLookAhead) && ((moveData.analogWalk > 60) || (moveData.analogWalk < 60)))
                {
                    g_CurrentPlayer->movecentrerelease = TRUE;
                }
            }
            else if (moveData.speedVertaUp > 0)
            {
                bondviewCurrentPlayerUpdateSpeedVerta(-moveData.speedVertaUp);

                if ((moveData.canLookAhead) && ((moveData.analogWalk > 60) || (moveData.analogWalk < 60)))
                {
                    g_CurrentPlayer->movecentrerelease = TRUE;
                }
            }
            else
            {
                bondviewCurrentPlayerUpdateSpeedVerta(0);
            }

            g_CurrentPlayer->vv_verta += g_CurrentPlayer->speedverta * g_GlobalTimerDelta * 3.5f;

#ifndef TARGET_N64
            /* DIRECT MOUSE pitch (1964 model): the delta IS the angle, added
             * after the engine's own integration and scaled by fov/basefov.
             * The tank's -20 floor and the engine's normal limits below still
             * apply, exactly as they do to stick pitch. */
            if (g_portMouseDirect && g_portMousePitchDeg != 0.0f
                && (!g_CurrentPlayer->insightaimmode || portAimLockActive())) /* routing only: permission is portMouseLookPermitted; AIM LOCK keeps the mouse on the view while aiming */
            {
                /* explicit aim gate, same as yaw. Deliberately a SIBLING of the
                 * canNaturalPitch chain above: that is a control-style flag
                 * that digital-pitch styles leave 0, and gating on it killed
                 * mouse pitch. */
                g_CurrentPlayer->vv_verta += g_portMousePitchDeg * portMouseFovRatio();
            }
            /* PAD LOOK pitch - a SIBLING of the mouse add above and of the
             * canNaturalPitch chain, for the same reason: 1.1 pitches
             * digitally and leaves that flag clear. Its own variable, so the
             * mouse's path is untouched. */
            if (g_portPadLookPitchDeg != 0.0f)
            {
                g_CurrentPlayer->vv_verta += g_portPadLookPitchDeg;
            }
#endif

            if ((g_PlayerIsInTank == 1) && (g_EnterTankAudioState == TANK_RUN_STATE_RUNNING) && (g_CurrentPlayer->vv_verta < -20.0f))
            {
                g_CurrentPlayer->vv_verta = -20.0f;
            }
        }
    }

    if (moveData.canNaturalTurn)
    {
        ftemp_nostack_sp8C = viGetFovY();
        ftemp_nostack_sp84 = (f32) moveData.analogTurn / 70.0f;

        if (ftemp_nostack_sp84 > 1)
        {
            ftemp_nostack_sp84 = 1;
        }
        else if (ftemp_nostack_sp84 < -1)
        {
            ftemp_nostack_sp84 = -1;
        }

        if (ftemp_nostack_sp84 >= 0)
        {
            ftemp_nostack_sp84 *= ftemp_nostack_sp84;
        }
        else
        {
            ftemp_nostack_sp84 *= -ftemp_nostack_sp84;
        }

#ifndef TARGET_N64
        /* MOUSE LOOK turn - see the pitch injection above for the scaling */
        if (!g_portMouseDirect
            && moveData.freeLookDx != 0.0f && g_GlobalTimerDelta > 0.0f)
        {
            ftemp_nostack_sp84 += moveData.freeLookDx / g_GlobalTimerDelta;
        }
#endif

        ftemp_nostack_sp88 = (ftemp_nostack_sp8C / GE_DEFAULT_FOV); /* yaw vs the CONFIGURED base fov, like
                                                                     * pitch */
        g_CurrentPlayer->speedtheta = ftemp_nostack_sp84 * ftemp_nostack_sp88;
    }
    else if (moveData.aimTurnLeftSpeed > 0)
    {
        bondviewCurrentPlayerUpdateSpeedTheta(moveData.aimTurnLeftSpeed);
    }
    else if (moveData.aimTurnRightSpeed > 0)
    {
        bondviewCurrentPlayerUpdateSpeedTheta(-moveData.aimTurnRightSpeed);
    }
    else
    {
        bondviewCurrentPlayerUpdateSpeedTheta(0);
    }

    if (g_PlayerIsInTank == 1)
    {
        if (g_EnterTankAudioState == TANK_RUN_STATE_RUNNING)
        {
            ftemp_nostack_sp80 = 0;

            if (moveData.canNaturalTurn)
            {
                ftemp_nostack_sp80 = g_CurrentPlayer->speedtheta * 0.3f;
            }
            else if (moveData.aimTurnLeftSpeed > 0)
            {
                ftemp_nostack_sp80 = sub_GAME_7F080228(1) * 0.3f;
            }
            else if (moveData.aimTurnRightSpeed > 0)
            {
                ftemp_nostack_sp80 = sub_GAME_7F080228(-1) * 0.3f;
            }
#ifndef TARGET_N64
            if (g_portPadTankSteer != 0.0f)
            {   /* THE HULL, and only the hull: injected as this filter's own
                 * input, not through canNaturalTurn (which would let the mouse
                 * steer too). The * 0.3f is the vanilla scaling, kept so the
                 * tank's weight is unchanged. */
                ftemp_nostack_sp80 = g_portPadTankSteer * 0.3f;
            }
#endif
/* PORT: don't feed the mouse in here: this accumulator becomes speedtheta,
 * which drives both the hull (g_TankOrientationAngle) and the turret, so the
 * mouse would swing them together. Turret-only input is g_TankTurretTurn. */

#ifndef TARGET_N64
            if (portSmoothOn())
            {
                portLagStep(&g_TankTurnSpeed, TANKUPDATEROTATION_SCALE, ftemp_nostack_sp80, g_GlobalTimerDelta);
            }
            else
#endif
            for (i_1=0; i_1<g_ClockTimer; i_1++)
            {
                g_TankTurnSpeed = (TANKUPDATEROTATION_SCALE * g_TankTurnSpeed) + ftemp_nostack_sp80;
            }

            g_CurrentPlayer->speedtheta = g_TankTurnSpeed * TANK_VERT_ANGLE_RAD_FACTOR;
        }
        else
        {
            g_CurrentPlayer->speedtheta = 0;
        }

#ifndef TARGET_N64
        { /* GE_TANKDBG=1: once a second in a tank, print every yaw-ish input
           * alongside the hull angle (g_TankOrientationAngle) and turret angle
           * relative to the hull (g_TankTurretAngle), to show which input moves
           * what. */
            extern char *getenv(const char *);
            extern int printf(const char *, ...);
            static s32 tankdbg = -1;
            static s32 tankdbgtick;

            if (tankdbg < 0)
            {
                tankdbg = getenv("GE_TANKDBG") != NULL;
            }
            if (tankdbg && (tankdbgtick++ % 60) == 0)
            {
                printf("ge: tank: turn=%d strafe=%d aimL=%.2f aimR=%.2f tankL=%.2f tankR=%.2f "
                       "mousedx=%+.3f | speedtheta=%+.3f turretturn=%+.4f hull=%.3f turretrel=%.3f aim=%d\n",
                       (s32) moveData.analogTurn, (s32) moveData.analogStrafe,
                       moveData.aimTurnLeftSpeed, moveData.aimTurnRightSpeed,
                       moveData.tankTurnLeftSpeed, moveData.tankTurnRightSpeed,
                       moveData.freeLookDx, g_CurrentPlayer->speedtheta, g_TankTurretTurn,
                       g_TankOrientationAngle, g_TankTurretAngle,
                       (s32) g_CurrentPlayer->insightaimmode);
            }
        }
#endif
    }

    if (moveData.detonating)
    {
        g_CurrentPlayer->hands[GUNRIGHT].weapon_action_state = GUN_ANIM_STATE_IDLE;
        g_CurrentPlayer->hands[GUNRIGHT].weapon_current_animation = 0;
        trigger_remote_mine_detonation();
    }

#ifndef TARGET_N64
    { /* Port suite prev/next weapon (joy_port.c) = the A-tap / A+Z paths */
        extern s32 portJoyPortWeaponStep(void);
        s32 step = portJoyPortWeaponStep();
        if (step < 0)
        {
            moveData.weaponBackOffset = 1;
        }
        else if (step > 0)
        {
            moveData.weaponForwardOffset = 1;
        }
    }
#endif

    canCycleWeapons = 1;

    if ((getPlayerCount() >= 2) && (get_scenario() == 2) && (bondinvIsAliveWithFlag()))
    {
        canCycleWeapons = 0;
    }

    if (canCycleWeapons)
    {
        if (moveData.weaponBackOffset)
        {
            backstep_through_inventory();
        }

        if (moveData.weaponForwardOffset)
        {
            advance_through_inventory();
        }
    }

    if (moveData.canSwivelGun)
    {
        g_CurrentPlayer->controldef = CONTROLLER_CONFIG_HONEY;
    }
    else if (moveData.canManualAim)
    {
        g_CurrentPlayer->controldef = CONTROLLER_CONFIG_KISSY;
    }

#ifndef TARGET_N64
    /* MOUSE aim-mode crosshair: the stick positions the crosshair absolutely,
     * but a mouse is relative, so accumulate its deltas into a persistent
     * offset clamped to the stick's reach and clear it when aim mode ends
     * (player 1 only). */
    if (get_cur_playernum() != 0)
    {
        /* MP: the swivel accumulators belong to player 1 (the mouse owner);
         * the pad players' ticks must not zero them through the reset arm
         * below, or P1's crosshair collapses. */
    }
    else if (g_CurrentPlayer->controldef != CONTROLLER_CONFIG_KISSY || !moveData.canManualAim
        || portAimLockActive()) /* AIM LOCK: the mouse never reaches the crosshair */
    {
        g_bondviewMouseSwivelX = 0.0f;
        g_bondviewMouseSwivelY = 0.0f;
        g_bondviewAimSwivelPosX = 0.0f;
        g_bondviewAimSwivelPosY = 0.0f;
        g_bondviewAimSeeded = 0;
        g_bondviewAimMouseMoved = 0;
        /* who swayed the gun: the mouse, or the stick / pad look stick /
         * keyboard turn (speedtheta, speedverta), whichever turned last */
        if (g_portMouseYawDeg != 0.0f || g_portMousePitchDeg != 0.0f
            || moveData.freeLookDx != 0.0f || moveData.freeLookDy != 0.0f)
        {
            g_bondviewSwayFromMouse = 1;
        }
        else if (g_CurrentPlayer->speedtheta != 0.0f || g_CurrentPlayer->speedverta != 0.0f
                 || g_portPadLookYawDeg != 0.0f || g_portPadLookPitchDeg != 0.0f)
        {
            g_bondviewSwayFromMouse = 0;
        }
    }
    else
    {
    if (!g_bondviewAimSeeded)
    {
        /* AIM ENTRY: seed the mouse offset with the turn that holds the gun
         * where it currently points (pos*(1-d), from gunfire.c's sway filter),
         * so the crosshair starts at the muzzle instead of snapping to the
         * centre. With the tank turret weapon, X belongs to the turret.
         * Only when the mouse did the looking: after a pad or keyboard turn
         * the sight glides back to the centre as in vanilla, and so does an
         * axis Auto-Aim was steering, since aiming drops the lock. */
        extern s32 get_is_ramrom_flag(void); /* a demo replays an N64 pad */

        g_bondviewAimSeeded = 1;
        if (g_portMouseDirect && g_bondviewSwayFromMouse && get_is_ramrom_flag() == 0)
        {
            f32 d = g_CurrentPlayer->guncrossdamp;
            s32 autoGun = bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM);

            if (d > 0.0f && d < 1.0f)
            {
                if (!portTankTurretWeaponHeld()
                    && !(autoGun && g_CurrentPlayer->autoaim_target_x && currentPlayerGetXAutoAimEnabledRedirect()))
                {
                    g_bondviewMouseSwivelX = g_CurrentPlayer->crosshair_x_pos * (1.0f - d);
                }
                if (!(autoGun && g_CurrentPlayer->autoaim_target_y && currentPlayerGetYAutoAimEnabledRedirect()))
                {
                    g_bondviewMouseSwivelY = g_CurrentPlayer->crosshair_y_pos * (1.0f - d);
                }
            }
        }
    }
    if (moveData.freeLookDx != 0.0f || moveData.freeLookDy != 0.0f
             || g_portMouseYawDeg != 0.0f || g_portMousePitchDeg != 0.0f)
    {
        g_bondviewAimMouseMoved = 1; /* from here on the mouse owns this aim session */

        /* AIM-MODE CROSSHAIR SPEED: matched to the 1964 injector (full reach in
         * ~376 counts) so the crosshair crosses the screen in a deliberate
         * sweep rather than pinning instantly to the edge and turning the view. */
        if (g_portMouseDirect)
        {
            /* midas.ini [input] mouse_sens_aim scales this independently of the
             * look sensitivity, since dragging a crosshair and rotating a
             * camera want different speeds. */
            extern f32 portInputMouseAimScale(void);
            f32 aimscale = AIM_CROSSHAIR_PER_DEG * portInputMouseAimScale();

            /* TANK WEAPON: mouse X belongs to the turret (g_TankTurretTurn)
             * in aim mode too, and only Y moves the aimer; any other gun held
             * in the tank aims as on foot. */
            if (!portTankTurretWeaponHeld())
            {
                g_bondviewMouseSwivelX += g_portMouseYawDeg * aimscale;
            }
            g_bondviewMouseSwivelY -= g_portMousePitchDeg * aimscale * portAimIsoY(); /* same px per degree as X */
        }
        else
        {
            if (!portTankTurretWeaponHeld())
            {
                g_bondviewMouseSwivelX += moveData.freeLookDx * 0.3f;
            }
            g_bondviewMouseSwivelY -= moveData.freeLookDy * 0.3f * portAimIsoY(); /* same px per degree as X */
        }
    }
    else if (!g_bondviewAimMouseMoved
             && (moveData.controlStickXRaw != 0 || moveData.controlStickYRaw != 0))
    {
        /* PAD AIM: the entry seed makes the offset nonzero, which would send a
         * pad down the mouse's unfiltered snap path and make it jittery. Once
         * the stick takes over without the mouse, drop the seed so the pad
         * glides as in vanilla. */
        g_bondviewMouseSwivelX = 0.0f;
        g_bondviewMouseSwivelY = 0.0f;
    }
    { /* the reach clamp covers the seed as well as the accumulation */
        f32 reachx = 0.65f * portAimReachX(); /* wide mouse aim: window-edge reach */

        if (g_bondviewMouseSwivelX >  reachx) g_bondviewMouseSwivelX =  reachx;
        if (g_bondviewMouseSwivelX < -reachx) g_bondviewMouseSwivelX = -reachx;
    }
    if (g_bondviewMouseSwivelY >  0.65f) g_bondviewMouseSwivelY =  0.65f;
    if (g_bondviewMouseSwivelY < -0.65f) g_bondviewMouseSwivelY = -0.65f;
    }
#endif

    if (g_CurrentPlayer->controldef == CONTROLLER_CONFIG_HONEY)
    {
        gunSetAimType(0);

        if (moveData.canAutoAim
            && currentPlayerGetXAutoAimEnabledRedirect()
            && g_CurrentPlayer->autoaim_target_x
            && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM))
        {
            sp70 = g_CurrentPlayer->autoaimx;
        }
        else
        {
            sp70 = g_CurrentPlayer->speedtheta * 0.3f;
        }

        if (moveData.canAutoAim
            && currentPlayerGetYAutoAimEnabledRedirect()
            && g_CurrentPlayer->autoaim_target_y
            && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM))
        {
            ftemp_nostack_sp78 = g_CurrentPlayer->autoaimy;
        }
        else
        {
            ftemp_nostack_sp78 = -g_CurrentPlayer->speedverta * 0.1f;
        }

#ifndef TARGET_N64
        /* MOUSE GUN SWAY: in direct mode the mouse doesn't touch speedtheta /
         * speedverta, which the engine derives sway from, so convert this
         * tick's applied angle back into the equivalent rate and feed that.
         * Skip any axis autoaim is steering. */
        if (g_portMouseDirect && g_GlobalTimerDelta > 0.0f)
        {
            f32 ratio = portMouseFovRatio();

            if (g_portMouseYawDeg != 0.0f && !(moveData.canAutoAim
                && currentPlayerGetXAutoAimEnabledRedirect()
                && g_CurrentPlayer->autoaim_target_x
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM)))
            {
                sp70 += ((g_portMouseYawDeg * ratio) / (g_GlobalTimerDelta * 3.5f)) * 0.3f;
            }
            if (g_portMousePitchDeg != 0.0f && !(moveData.canAutoAim
                && currentPlayerGetYAutoAimEnabledRedirect()
                && g_CurrentPlayer->autoaim_target_y
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM)))
            {
                ftemp_nostack_sp78 -= ((g_portMousePitchDeg * ratio) / (g_GlobalTimerDelta * 3.5f)) * 0.1f;
            }
        }
        /* THE LOOK STICK SWAYS THE GUN TOO, the same way as the mouse above
         * (applied angle converted back to a rate), without the fov ratio since
         * these degrees are already final. Autoaim keeps its axis. */
        if (g_GlobalTimerDelta > 0.0f)
        {
            if (g_portPadLookYawDeg != 0.0f && !(moveData.canAutoAim
                && currentPlayerGetXAutoAimEnabledRedirect()
                && g_CurrentPlayer->autoaim_target_x
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM)))
            {
                sp70 += (g_portPadLookYawDeg / (g_GlobalTimerDelta * 3.5f)) * 0.3f;
            }
            if (g_portPadLookPitchDeg != 0.0f && !(moveData.canAutoAim
                && currentPlayerGetYAutoAimEnabledRedirect()
                && g_CurrentPlayer->autoaim_target_y
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM)))
            {
                ftemp_nostack_sp78 -= (g_portPadLookPitchDeg / (g_GlobalTimerDelta * 3.5f)) * 0.1f;
            }
        }
#endif

#ifndef TARGET_N64
        { /* MOUSE gun-sway clamp, in every mode: mouse look makes speedtheta /
           * speedverta unbounded, so clamp the MANUAL feed to the stick's
           * envelope at the current FOV or fast turns swing the gun and its aim
           * ray beyond the 4:3 window. The AUTOAIM branch is bounded already
           * and must keep full authority (capping it broke auto-aim and
           * desynced a demo); only the low-pass below is legacy-only. */
            extern s32 get_is_ramrom_flag(void);
            f32 stickmax = viGetFovY() / GE_DEFAULT_FOV; /* aim reach vs the base fov */
            f32 lim;
            s32 xAuto = moveData.canAutoAim
                && currentPlayerGetXAutoAimEnabledRedirect()
                && g_CurrentPlayer->autoaim_target_x
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM);
            s32 yAuto = moveData.canAutoAim
                && currentPlayerGetYAutoAimEnabledRedirect()
                && g_CurrentPlayer->autoaim_target_y
                && bondwalkItemCheckBitflags(getCurrentPlayerWeaponId(GUNRIGHT), WEAPONSTATBITFLAG_HAS_AUTO_AIM);

            if (!xAuto)
            {
                lim = 0.3f * stickmax;
                if (sp70 >  lim) sp70 =  lim;
                if (sp70 < -lim) sp70 = -lim;
            }
            if (!yAuto)
            {
                lim = 0.1f * stickmax;
                if (ftemp_nostack_sp78 >  lim) ftemp_nostack_sp78 =  lim;
                if (ftemp_nostack_sp78 < -lim) ftemp_nostack_sp78 = -lim;
            }

            /* smooth AFTER the clamp, so jittery mouse deltas don't vibrate the
             * pinned gun. Manual feed only and never in demo playback (the lag
             * makes replayed shots trail); accumulators track the live value
             * while bypassed so handoffs don't snap. */
            if (!g_portMouseDirect
                && get_cur_playernum() == 0 && get_is_ramrom_flag() == 0)
            {
                f32 k = GUN_SWAY_LP_GAIN * g_GlobalTimerDelta;
                if (k > 1.0f)
                {
                    k = 1.0f;
                }
                if (!xAuto)
                {
                    g_bondviewGunSwayLpX += (sp70 - g_bondviewGunSwayLpX) * k;
                    sp70 = g_bondviewGunSwayLpX;
                }
                else
                {
                    g_bondviewGunSwayLpX = sp70;
                }
                if (!yAuto)
                {
                    g_bondviewGunSwayLpY += (ftemp_nostack_sp78 - g_bondviewGunSwayLpY) * k;
                    ftemp_nostack_sp78 = g_bondviewGunSwayLpY;
                }
                else
                {
                    g_bondviewGunSwayLpY = ftemp_nostack_sp78;
                }
            }

            /* AIM LOCK: no gun-sway swivel while turning - the manual term is
             * zeroed (crosshair, bullet ray and gun model all settle on the
             * centre through the same filter); auto-aim's steering is kept. */
            if (portAimLockActive())
            {
                if (!xAuto)
                {
                    sp70 = 0.0f;
                    g_bondviewGunSwayLpX = 0.0f;
                }
                if (!yAuto)
                {
                    ftemp_nostack_sp78 = 0.0f;
                    g_bondviewGunSwayLpY = 0.0f;
                }
            }
        }
#endif

        sub_GAME_7F067F58(sp70, ftemp_nostack_sp78, MAX_AIMLOCK_SPEED_DEFAULT);
    }
    else if (g_CurrentPlayer->controldef == CONTROLLER_CONFIG_KISSY)
    {
        gunSetAimType(0);
#ifdef TARGET_N64
        sub_GAME_7F067FBC(((f32) moveData.controlStickXRaw * 0.65f) / 80.0f, ((f32) moveData.controlStickYRaw * 0.65f) / 80.0f);
#else
        { /* stick position + accumulated mouse offset, same clamp
           * (MP: the mouse offset is player 1's - pad players get 0) */
            f32 mswx = (get_cur_playernum() == 0) ? g_bondviewMouseSwivelX : 0.0f;
            f32 mswy = (get_cur_playernum() == 0) ? g_bondviewMouseSwivelY : 0.0f;
            f32 swx = (((f32) moveData.controlStickXRaw * 0.65f) / 80.0f) + mswx;
            f32 swy = (((f32) moveData.controlStickYRaw * 0.65f) / 80.0f) + mswy;
            f32 reachx = 0.65f * portAimReachX(); /* wide mouse aim: window-edge reach */
            if (swx >  reachx) swx =  reachx;
            if (swx < -reachx) swx = -reachx;
            if (swy >  0.65f) swy =  0.65f;
            if (swy < -0.65f) swy = -0.65f;
            /* where the crosshair actually sits - drives the edge-turn
             * injection early next tick (player 1's crosshair only - the
             * edge-turn reader is gated the same way) */
            if (get_cur_playernum() == 0)
            {
                g_bondviewAimSwivelPosX = swx;
                g_bondviewAimSwivelPosY = swy;
            }
            /* DIRECT MOUSE: once the mouse has moved the crosshair this aim
             * session, write the swivel to the crosshair outright instead of
             * letting the CrosshairSpeed low-pass glide to it (see gunfire.c
             * g_portAimCrosshairSnap); pure-stick aim keeps its glide. */
            {
                extern s32 g_portAimCrosshairSnap;

                if (portAimLockActive())
                {
                    /* AIM LOCK: crosshair to the centre (the filter decays
                     * there in ~140 ms); the stick turned the view above */
                    swx = 0.0f;
                    swy = 0.0f;
                    g_bondviewAimSwivelPosX = 0.0f;
                    g_bondviewAimSwivelPosY = 0.0f;
                }
                /* only the mouse snaps; the entry seed alone (pad aim, or
                 * before the first mouse count) keeps the glide */
                g_portAimCrosshairSnap = g_portMouseDirect && g_bondviewAimMouseMoved
                    && (mswx != 0.0f || mswy != 0.0f);
                sub_GAME_7F067FBC(swx, swy);
                g_portAimCrosshairSnap = 0;
            }
        }
#endif
    }
}


/**
 * Perfect Dark playerTickDamageAndHealth
 *
 * NTSC address 7F083FC8.
 * NTSC-J address 7F0845D8.
 * EU address 7F08406C.
*/
void bondviewPlayerTickDamageAndHealth(void)
{
    // update damage showtime
    if (g_CurrentPlayer->damageshowtime >= 0)
    {
        // 0: This is the first frame of damage
        if (g_CurrentPlayer->damageshowtime == 0)
        {
            gunSetGunAmmoVisible(GUNAMMOREASON_DAMAGE, FALSE);
            gunSetSightVisible(GUNSIGHTREASON_DAMAGE, FALSE);
            hudmsgsSetOff(4);
            bondviewSetUpperTextDisplayFlag(PLAYERFLAG_NOTIMER);
            countdownTimerSetVisible(8, 0);

            g_CurrentPlayer->damagetype = (s32)(currentPlayerGetHealth() * 8.0f);

            if (g_CurrentPlayer->damagetype >= 8)
            {
                g_CurrentPlayer->damagetype = 7;
            }
            // Ensure we don't read out of bounds of the g_DamageTypes array.
            else if (g_CurrentPlayer->damagetype < 0)
            {
                g_CurrentPlayer->damagetype = 0;
            }
        }

#ifdef VERSION_US
        if (
            (g_DamageTypes[g_CurrentPlayer->damagetype].field_0x8 >= g_CurrentPlayer->damageshowtime)
            || (g_DamageTypes[g_CurrentPlayer->damagetype].flashEndFrame >= g_CurrentPlayer->damageshowtime))
        {
            if (!g_CurrentPlayer->bonddead)
            {
#else
        if (!g_CurrentPlayer->bonddead
            && (
                (g_DamageTypes[g_CurrentPlayer->damagetype].field_0x8 >= g_CurrentPlayer->damageshowtime)
                || (g_DamageTypes[g_CurrentPlayer->damagetype].flashEndFrame >= g_CurrentPlayer->damageshowtime)))
        {
#endif
            if (g_CurrentPlayer->damageshowtime >= g_DamageTypes[g_CurrentPlayer->damagetype].flashStartFrame
                && g_CurrentPlayer->damageshowtime <= g_DamageTypes[g_CurrentPlayer->damagetype].flashEndFrame)
            {
                f32 frac;
#ifdef VERSION_US
                s32 flashdoneframes;
                s32 totalframes;
                s32 flashfullframe;
#else
                f32 flashdoneframes;
                f32 totalframes;
                f32 flashfullframe;
#endif

                flashdoneframes = g_CurrentPlayer->damageshowtime - g_DamageTypes[g_CurrentPlayer->damagetype].flashStartFrame;
                flashfullframe = g_DamageTypes[g_CurrentPlayer->damagetype].flashFullFrame;
                totalframes = g_DamageTypes[g_CurrentPlayer->damagetype].flashEndFrame - g_DamageTypes[g_CurrentPlayer->damagetype].flashStartFrame;

                if (flashdoneframes < flashfullframe)
                {
                    frac = (g_DamageTypes[g_CurrentPlayer->damagetype].maxAlpha * (f32)flashdoneframes) / (f32)flashfullframe;
                }
                else
                {
                    frac = (g_DamageTypes[g_CurrentPlayer->damagetype].maxAlpha * (f32)(totalframes - flashdoneframes)) / (f32)(totalframes - flashfullframe);
                }

                currentPlayerSetFadeColour(
                    g_DamageTypes[g_CurrentPlayer->damagetype].red,
                    g_DamageTypes[g_CurrentPlayer->damagetype].green,
                    g_DamageTypes[g_CurrentPlayer->damagetype].blue,
                    frac);
            }
#ifdef VERSION_US
            }
#endif

            if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x0)
            {
#if defined(VERSION_US)
                g_CurrentPlayer->damageshowtime += g_ClockTimer;
#else
                g_CurrentPlayer->damageshowtime += g_GlobalTimerDelta;
#endif
            }
            else
            {
#if defined(VERSION_US)
                g_CurrentPlayer->damageshowtime += speedgraphframes;
#else
                g_CurrentPlayer->damageshowtime += jpD_800484D0;
#endif
            }
        }
        else /* (damage showtime is over) */
        {
            g_CurrentPlayer->damageshowtime = -1;
            currentPlayerSetFadeColour(0xFF, 0xFF, 0xFF, 0);

            if (!g_CurrentPlayer->bonddead)
            {
                gunSetGunAmmoVisible(GUNAMMOREASON_DAMAGE, TRUE);
                gunSetSightVisible(GUNSIGHTREASON_DAMAGE, TRUE);
                hudmsgsSetOn(4);
                bondviewClearUpperTextDisplayFlag(PLAYERFLAG_NOTIMER);
                countdownTimerSetVisible(8, 1);
            }
        }
    }

    // update health showtime
    if (g_CurrentPlayer->healthshowtime >= 0)
    {
        // 0: This is the first frame of damage
        if (g_CurrentPlayer->healthshowtime == 0)
        {
            g_CurrentPlayer->healthdamagetype = (s32)(currentPlayerGetHealth() * 8.0f);

            if (g_CurrentPlayer->healthdamagetype >= 8)
            {
                g_CurrentPlayer->healthdamagetype = 7;
            }
            else if (g_CurrentPlayer->healthdamagetype < 0)
            {
                g_CurrentPlayer->healthdamagetype = 0;
            }
        }

        if (!g_CurrentPlayer->bonddead)
        {
            if ((g_CurrentPlayer->healthshowtime >= g_HealthDisplayDurations[g_CurrentPlayer->healthdamagetype].validStartFrame)
                && (g_HealthDisplayDurations[g_CurrentPlayer->healthdamagetype].updateToRealHealthFrame >= g_CurrentPlayer->healthshowtime))
            {
                g_CurrentPlayer->apparenthealth = g_CurrentPlayer->oldhealth;
                g_CurrentPlayer->apparentarmour = g_CurrentPlayer->oldarmour;
#if defined(VERSION_US)
                g_CurrentPlayer->healthshowtime += g_ClockTimer;
#else
                g_CurrentPlayer->healthshowtime += g_GlobalTimerDelta;
#endif
            }
            else if ((g_CurrentPlayer->healthshowtime >= g_HealthDisplayDurations[g_CurrentPlayer->healthdamagetype].validStartFrame)
                && (g_HealthDisplayDurations[g_CurrentPlayer->healthdamagetype].hideHealthFrame >= g_CurrentPlayer->healthshowtime))
            {
                g_CurrentPlayer->apparenthealth = g_CurrentPlayer->bondhealth;
                g_CurrentPlayer->apparentarmour = g_CurrentPlayer->bondarmour;
#if defined(VERSION_US)
                g_CurrentPlayer->healthshowtime += g_ClockTimer;
#else
                g_CurrentPlayer->healthshowtime += g_GlobalTimerDelta;
#endif
            }
            else
            {
                g_CurrentPlayer->healthshowtime = -1;
            }
        }
        else
        {
            g_CurrentPlayer->healthshowtime = -1;
        }
    }
}

/**
 * If global flag g_SurroundBondWithExplosionsFlag is set then explosions
 * will be randomly created around Bond.
 * Perfect Dark method playerTickExplode.
 * NTSC address 7F084360.
 * EU address 7F0844A4.
*/
void bondviewPlayerTickExplode(void)
{
    g_PlayerTickExplodeCreatePosition++;

    if (g_SurroundBondWithExplosionsFlag
        && (g_PlayerInvincible == FALSE)
        && g_SurroundBondWithExplosionsTicks < g_GlobalTimer)
    {
        struct coord3d pos;

        pos.f[0] = g_CurrentPlayer->prop->pos.f[0];
        pos.f[1] = g_CurrentPlayer->prop->pos.f[1];
        pos.f[2] = g_CurrentPlayer->prop->pos.f[2];

        switch (g_PlayerTickExplodeCreatePosition % 4)
        {
            case 0: pos.x += 250.0f + 150.0f * RANDOMGETNEXT_F32(); break;
    		case 1: pos.x -= 250.0f + 150.0f * RANDOMGETNEXT_F32(); break;
    		case 2: pos.z += 250.0f + 150.0f * RANDOMGETNEXT_F32(); break;
    		case 3: pos.z -= 250.0f + 150.0f * RANDOMGETNEXT_F32(); break;
        }

        pos.y += 200.0f * RANDOMGETNEXT_F32() - 100.0f;

        explosionCreate(0, &pos, g_CurrentPlayer->prop->stan, EXPLOSION_DEF_PLAYER, 0, 0, g_CurrentPlayer->prop->rooms, 0);

        g_SurroundBondWithExplosionsTicks = (randomGetNext() % (u32)PLAYER_TICKEXPLODE_FACTOR) + g_GlobalTimer + PLAYER_TICKEXPLODE_FACTOR;
    }
}


/**
 * NTSC Address 0x7F084648.
 * NTSC-J address 0x7F084CF8.
 *
 * Met by Saint Jon The Archangel in the writhing pits of hell, the beast was
 * pinned, prone on the floor. He had taken many forms throughout the ages.
 * From the creator of man to the far reaches of the universe, his perpetual
 * reincarnations reigned throughout space and time... until now. His form was
 * weak, the opposition strong. The time for atonement had finally come.
 *
 * Battered, torn, exposed, the beast slacked his gaping maw.
 *
 * "I'm sorry, Jon" he whispered.
 *
 * The archangel raised his flaming blade.
 *
 * "You are forgiven, Garfield"
 *
 * And the beast exhaled his last.
 *
 * 10 months of off and on work to match.
 * Thanks Trevor.
 * - Bethany Burns
 */
void MoveBond(s8 stick_x, s8 stick_y, u16 buttons, u16 oldbuttons)
{
    struct coord3d move_offset;
    f32 ftemp;
    f32 stack_padding_9;
    f32 sp3A0;
    s32 i;
    f32 maxspeed;
    s32 use_stanHeight;
    f32 sp390;

#ifndef TARGET_N64
    g_portBondFrozenTick = 0;
    { /* GE_KILLBOND=n (diag): kill player 1 on the n-th MoveBond tick so a
       * headless run can exercise the death sequence (mouse gates, red
       * screen, death cam) without a guard doing it. */
        static long killat = -1;
        static u32 ticks;
        if (killat < 0) {
            extern char *getenv(const char *);
            const char *e = getenv("GE_KILLBOND");
            killat = (e != NULL) ? atol(e) : 0;
        }
        ticks++;
        if (killat > 0 && ticks == (u32) killat && get_cur_playernum() == 0) {
            bondviewKillCurrentPlayer();
        }
    }
#endif
    move_offset = g_DefaultMoveBondOffset;

    use_stanHeight = 0;
    maxspeed = 0.0f;
    sp390 = 0.0f;

    #if defined(VERSION_US) || defined(VERSION_JP)
    if (stick_x >= 100 || stick_x <= -100) return_null(); // __LINE__ __FILE__ (#6414 bondview.c) "joystick x has value %d!\n"
    if (stick_y >= 100 || stick_y <= -100) return_null(); // __LINE__ __FILE__ (#6415 bondview.c) "joystick y has value %d!\n"
    #endif

    if (g_bondviewForceDisarm > 0)
    {
        g_bondviewForceDisarm++;
        if (g_bondviewForceDisarm >= 4)
        {
            g_bondviewForceDisarm = 0;
            g_CurrentPlayer->lock_hand_model[GUNLEFT] = 0;
            g_CurrentPlayer->lock_hand_model[GUNRIGHT] = 0;
            currentPlayerUnEquipWeaponWrapper(GUNLEFT, getCurrentPlayerWeaponId(GUNLEFT));
            currentPlayerUnEquipWeaponWrapper(GUNRIGHT, getCurrentPlayerWeaponId(GUNRIGHT));
        }
    }

    currentPlayerSetCameraMode(0);
    bondviewPlayerTickDamageAndHealth();
    bondviewPlayerTickExplode();
    bondviewProcessInput(stick_x, stick_y, buttons, oldbuttons);

    if (lvlGetControlsLockedFlag())
    {
        bondviewPlayerStopAudioForPause();
    }

#ifndef TARGET_N64
    portPauseBoxTick(); /* every tick, state 0 included, so the box always settles back to 0 */
#endif
    if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0)
    {
        bondviewWatchAnimationTick();
    }

    /*
        Apply bondshotspeed vector to speedforwards scalar and speedsideways scalar.
        Crouching applies a 50% base speed reduction before applying boost.
        Bond can't be boosted while in the tank.
    */
    if (g_PlayerIsInTank == 0)
    {
#ifndef TARGET_N64
        { /* Port suite walk (joy_port.c): half speed, applied to the final
           * scalars so analog, C-button digital steps and controllers all
           * slow alike (the keyboard's WASD in style 1.2 is digital) */
            extern s32 portJoyPortWalkActive(void);
            if (portJoyPortWalkActive())
            {
                g_CurrentPlayer->speedforwards *= 0.5f;
                g_CurrentPlayer->speedsideways *= 0.5f;
            }
        }
#endif
        // This `if` block is Perfect Dark bwalkApplyCrouchSpeed.
        if (currentPlayerGetCrouchPos() == CROUCH_SQUAT)
        {
            g_CurrentPlayer->speedforwards *= 0.5f;
            g_CurrentPlayer->speedsideways *= 0.5f;
        }

        if ((g_CurrentPlayer->bondshotspeed.f[0] != 0.0f) || (g_CurrentPlayer->bondshotspeed.f[2] != 0.0f))
        {
            // boost forwards
            f32 shotboost_forward; // sp38C
            // boost sideways
            f32 shotboost_sideways; // sp388
            f32 shotboost_norm; // sp384

            // Assigning these two variables is done in Perfect Dark bmove0f0cba88.
            shotboost_forward =
                (-g_CurrentPlayer->bondshotspeed.f[0] * g_CurrentPlayer->vv_sintheta)
                + (g_CurrentPlayer->bondshotspeed.f[2] * g_CurrentPlayer->vv_costheta);
            shotboost_sideways =
                (-g_CurrentPlayer->bondshotspeed.f[0] * g_CurrentPlayer->vv_costheta)
                - (g_CurrentPlayer->bondshotspeed.f[2] * g_CurrentPlayer->vv_sintheta);

            shotboost_norm = sqrtf(
                (g_CurrentPlayer->bondshotspeed.f[0] * g_CurrentPlayer->bondshotspeed.f[0]) +
                (g_CurrentPlayer->bondshotspeed.f[2] * g_CurrentPlayer->bondshotspeed.f[2]));

            g_CurrentPlayer->speedforwards += shotboost_forward;
            g_CurrentPlayer->speedsideways += shotboost_sideways;

            // 3: x,y,z components of bondshotspeed
            for (i=0; i<3; i++)
            {
                if (g_CurrentPlayer->bondshotspeed.f[i] != 0.0f)
                {
                    if (g_CurrentPlayer->bondshotspeed.f[i] > 0.0f)
                    {
                        g_CurrentPlayer->bondshotspeed.f[i] -=
                            ((0.06666667f * g_GlobalTimerDelta * g_CurrentPlayer->bondshotspeed.f[i]) / shotboost_norm);

                        if (g_CurrentPlayer->bondshotspeed.f[i] < 0.0f)
                        {
                            g_CurrentPlayer->bondshotspeed.f[i] = 0.0f;
                        }
                    }
                    else if (g_CurrentPlayer->bondshotspeed.f[i] < 0.0f)
                    {
                        g_CurrentPlayer->bondshotspeed.f[i] -=
                            ((0.06666667f * g_GlobalTimerDelta * g_CurrentPlayer->bondshotspeed.f[i]) / shotboost_norm);

                        if (g_CurrentPlayer->bondshotspeed.f[i] > 0.0f)
                        {
                            g_CurrentPlayer->bondshotspeed.f[i] = 0.0f;
                        }
                    }
                }
            }
        }
    }

    /**
     * This section updates the tank turret horizontal position (turning left and right),
     * as well as turning the tank left and right.
    */
    if (g_PlayerIsInTank == 1)
    {
        f32 ftemp2;
        struct coord3d check_collision_p1;
        struct coord3d check_collision_p2;
        f32 stack_padding_1;
        s32 i_1;
        f32 curTankAngleRad;
        f32 tankChangeInAngle;
        f32 sp354;
        struct coord3d tank_collision_pt1;
        struct coord3d tank_collision_pt2;
        f32 tank_collision_dx;
        f32 tank_collision_dz;

        tankChangeInAngle = DegToRad1Fact(g_CurrentPlayer->speedtheta * g_GlobalTimerDelta) * 3.5f;
        curTankAngleRad = g_TankOrientationAngle + tankChangeInAngle;

        if (curTankAngleRad >= M_TAU_F)
        {
            curTankAngleRad -= M_TAU_F;
        }

        if (curTankAngleRad < 0.0f)
        {
            curTankAngleRad += M_TAU_F;
        }

        if (bondviewTankCollisionStatus(
            &g_CurrentPlayer->field_488.collision_position,
            g_CurrentPlayer->field_488.current_tile_ptr,
            curTankAngleRad,
            &check_collision_p1,
            &check_collision_p2))
        {
            g_TankOrientationAngle = curTankAngleRad;
        }
        else
        {
            f32 sp1E4; //x
            s32 stack_padding_1; //x
            f32 sp1FC; //x
            f32 sp324;
            f32 sp320;
            f32 sp31C;
            f32 sp20C; //x
            f32 tank_collision_norm; //x
            f32 sp210; //x
            f32 sp30C;
            f32 sp308;
            f32 sp304;

            sp31C = -1; //sp31C: scope within this block, used throughout
            sp304 = -1; //sp304: scope within this block, used throughout

            getCollisionEdge_maybe(&tank_collision_pt1, &tank_collision_pt2);

            tank_collision_dx = (tank_collision_pt2.f[0] - tank_collision_pt1.f[0]);
            tank_collision_dz = (tank_collision_pt2.f[2] - tank_collision_pt1.f[2]);

            tank_collision_norm = 1.0f / sqrtf((tank_collision_dx * tank_collision_dx) + (tank_collision_dz * tank_collision_dz));

            // sp320: scoped within this block, used throughout
            // sp324: scoped within this block, used throughout
            tank_collision_dx *= tank_collision_norm;
            tank_collision_dz *= tank_collision_norm;
            sp324 = tank_collision_dz;
            sp320 = -tank_collision_dx;

            // sp210: short lived variable
            sp210 =
                ((g_CurrentPlayer->field_488.collision_position.f[0] - check_collision_p2.f[0]) * sp324) +
                ((g_CurrentPlayer->field_488.collision_position.f[2] - check_collision_p2.f[2]) * sp320);

            if (sp210 < 0.0f)
            {
                sp210 = -sp210;
                sp324 = -sp324;
                sp320 = -sp320;
            }

            // sp20C: very short lived variable
            sp20C =
                ((g_CurrentPlayer->field_488.collision_position.f[0] - tank_collision_pt1.f[0]) * sp324) +
                ((g_CurrentPlayer->field_488.collision_position.f[2] - tank_collision_pt1.f[2]) * sp320);

            if (sp20C < sp210)
            {
                sp31C = sp210 - sp20C;
            }

            tank_collision_dx = check_collision_p2.f[0] - check_collision_p1.f[0];
            tank_collision_dz = check_collision_p2.f[2] - check_collision_p1.f[2];

            tank_collision_norm = 1.0f / sqrtf((tank_collision_dx * tank_collision_dx) + (tank_collision_dz * tank_collision_dz));

            // sp308: scoped within this block, used throughout
            // sp30C: scoped within this block, used throughout
            tank_collision_dx *= tank_collision_norm;
            tank_collision_dz *= tank_collision_norm;
            sp30C = tank_collision_dz;
            sp308 = -tank_collision_dx;

            // sp1F8 -> sp210: short lived variable
            sp210 =
                ((g_CurrentPlayer->field_488.collision_position.f[0] - check_collision_p2.f[0]) * sp30C) +
                ((g_CurrentPlayer->field_488.collision_position.f[2] - check_collision_p2.f[2]) * sp308);

            if (sp210 < 0.0f)
            {
                sp210 = -sp210;
                sp30C = -sp30C;
                sp308 = -sp308;
            }

            // sp1FC: very short lived variable
            // sp1E4: very short lived variable
            sp1FC =
                ((g_CurrentPlayer->field_488.collision_position.f[0] - tank_collision_pt1.f[0]) * sp30C) +
                ((g_CurrentPlayer->field_488.collision_position.f[2] - tank_collision_pt1.f[2]) * sp308);
            sp1E4 =
                ((g_CurrentPlayer->field_488.collision_position.f[0] - tank_collision_pt2.f[0]) * sp30C) +
                ((g_CurrentPlayer->field_488.collision_position.f[2] - tank_collision_pt2.f[2]) * sp308);

            if (sp1E4 < sp1FC)
            {
                sp1FC = sp1E4;
            }

            if (sp1FC < sp210)
            {
                sp304 = sp210 - sp1FC;
            }

            if ((sp304 >= 0.0f) && ((sp304 < sp31C) || (sp31C < 0.0f)))
            {
                sp324 = sp30C;
                sp320 = sp308;
                sp31C = sp304;
            }

            if (sp31C >= 0.0f)
            {
                move_offset.f[0] = sp31C * sp324 * 1.01f;
                move_offset.f[2] = sp31C * sp320 * 1.01f;

                bondviewCalcUpdatePlayerCollision(&move_offset, 1);

                move_offset.f[0] = 0.0f;
                move_offset.f[2] = 0.0f;

                if (bondviewTankCollisionStatus(
                    &g_CurrentPlayer->field_488.collision_position,
                    g_CurrentPlayer->field_488.current_tile_ptr,
                    curTankAngleRad,
                    &check_collision_p1,
                    &check_collision_p2))
                {
                    g_TankOrientationAngle = curTankAngleRad;
                }
                else
                {
                    tankChangeInAngle = 0.0f;
                }
            }
            else
            {
                tankChangeInAngle = 0.0f;
            }
        }

        sp354 = g_TankTurretOrientationAngleRad;
        g_TankTurretAngle += g_TankTurretTurn;
        if (g_TankTurretAngle >= M_TAU_F)
        {
            g_TankTurretAngle -= M_TAU_F;
        }

        if (g_TankTurretAngle < 0.0f)
        {
            g_TankTurretAngle += M_TAU_F;
        }

        ftemp = (DegToRad1Fact(g_CurrentPlayer->speedtheta * 3.5f) * 4.0f) + g_TankTurretAngle;

        if (ftemp < 0.0f)
        {
            ftemp += M_TAU_F;
        }

        if (ftemp >= M_TAU_F)
        {
            ftemp -= M_TAU_F;
        }

        if ((ftemp - g_TankTurretOrientationAngleRad) >= M_PI_F)
        {
            ftemp -= M_TAU_F;
        }
        else if ((ftemp - g_TankTurretOrientationAngleRad) < -M_PI_F)
        {
            ftemp += M_TAU_F;
        }

#ifndef TARGET_N64
        if (portSmoothOn())
        {
            portLagStep(&g_TankTurretOrientationAngleDeg, TANKUPDATEROTATION_SCALE, ftemp, g_GlobalTimerDelta);
        }
        else
#endif
        for (i_1=0; i_1<g_ClockTimer; i_1++)
        {
            g_TankTurretOrientationAngleDeg = ((TANKUPDATEROTATION_SCALE) * g_TankTurretOrientationAngleDeg) + ftemp;
        }

        g_TankTurretOrientationAngleRad = g_TankTurretOrientationAngleDeg * (1.0f - TANKUPDATEROTATION_SCALE);

        if (g_TankTurretOrientationAngleRad >= M_TAU_F)
        {
            g_TankTurretOrientationAngleRad -= M_TAU_F;
            g_TankTurretOrientationAngleDeg = g_TankTurretOrientationAngleRad / (1.0f - TANKUPDATEROTATION_SCALE);
        }

        if (g_TankTurretOrientationAngleRad < 0.0f)
        {
            g_TankTurretOrientationAngleRad += M_TAU_F;
            g_TankTurretOrientationAngleDeg = g_TankTurretOrientationAngleRad / (1.0f - TANKUPDATEROTATION_SCALE);
        }

        if (bondviewCallTankCollisionStatus(
            &g_CurrentPlayer->field_488.collision_position,
            g_CurrentPlayer->field_488.current_tile_ptr,
            g_TankOrientationAngle) == 0)
        {
            g_TankTurretOrientationAngleRad = sp354;
            g_TankTurretOrientationAngleDeg = g_TankTurretOrientationAngleRad / (1.0f - TANKUPDATEROTATION_SCALE);
            g_TankTurretAngle = sp354;
        }

        if (g_PlayerTankProp != NULL)
        {
            // sp 0x300
            struct TankRecord *temp_tank;
            struct coord3d tank_move_offset;
            Mtxf sp2B4;
            f32 stack_padding_4;

            temp_tank = (struct TankRecord *)g_PlayerTankProp->obj;

            tank_move_offset.f[1] = 0.0f;
            tank_move_offset.f[0] = g_TankModelPositionOffset.f[0];
            tank_move_offset.f[2] = g_TankModelPositionOffset.f[2];

            matrix_4x4_set_rotation_around_y(tankChangeInAngle, &sp2B4);
            mtx4RotateVecInPlace(&sp2B4, &tank_move_offset);
            bondviewTankModelRotationRelated();

            if (0) { }

            tank_move_offset.f[1] = 0.0f;
            tank_move_offset.f[0] = g_TankModelPositionOffset.f[0] - tank_move_offset.f[0];
            tank_move_offset.f[2] = g_TankModelPositionOffset.f[2] - tank_move_offset.f[2];

            matrix_4x4_set_rotation_around_y(M_TAU_F - g_TankOrientationAngle, &sp2B4);
            matrix_scalar_multiply(temp_tank->model->scale, &sp2B4);
            mtx4RotateVecInPlace(&sp2B4, &tank_move_offset);
            bondviewCalcUpdatePlayerCollision(&tank_move_offset, 1);
        }

#ifndef TARGET_N64
        if (portSmoothOn())
        {
            /* this tick's turret input (already delta-scaled) as a rate,
             * through the lag by the true delta, so sub-tick turret input isn't
             * dropped */
            if (g_GlobalTimerDelta > 0.0f)
            {
                portLagStep(&tank_turret_turn_speed, TANKUPDATEROTATION_SCALE, g_TankTurretTurn / g_GlobalTimerDelta, g_GlobalTimerDelta);
            }
        }
        else
#endif
        if (g_ClockTimer > 0) {
            for (i=0; i<g_ClockTimer; i++)
            {
                tank_turret_turn_speed = (TANKUPDATEROTATION_SCALE * tank_turret_turn_speed) + (g_TankTurretTurn / g_GlobalTimerDelta);
            }
        }

        ftemp = tank_turret_turn_speed * (1.0f - TANKUPDATEROTATION_SCALE);

        g_CurrentPlayer->vv_theta = (
            g_TankOrientationAngle +
            g_TankTurretOrientationAngleRad +
            ((DegToRad1Fact(g_CurrentPlayer->speedtheta * 3.5f)) * (4.0f)) +
            (ftemp * 4.0f)
            ) * 360.0f / M_TAU_F;

        while (g_CurrentPlayer->vv_theta < 0.0f)
        {
            g_CurrentPlayer->vv_theta += 360.0f;
        }
        while (g_CurrentPlayer->vv_theta >= 360.0f)
        {
            g_CurrentPlayer->vv_theta -= 360.0f;
        }
    }
    else
    {
        stack_padding_9 = g_CurrentPlayer->vv_theta + (g_CurrentPlayer->speedtheta * g_GlobalTimerDelta * 3.5f);

#ifndef TARGET_N64
        /* DIRECT MOUSE yaw (1964 model): the delta goes straight onto the camera
         * angle with no integration or damping; in a tank it goes to the turret
         * instead. NOT while aiming: then the mouse belongs to the crosshair
         * and the view only turns via the edge band, as in the injector. */
        if (g_portMouseDirect && g_portMouseYawDeg != 0.0f
            && (!g_CurrentPlayer->insightaimmode || portAimLockActive())) /* routing only: permission is
                                                                           * portMouseLookPermitted; AIM LOCK keeps
                                                                           * mouse yaw on the view while aiming */
        {
            stack_padding_9 += g_portMouseYawDeg * portMouseFovRatio();
        }
        /* PAD LOOK yaw - written straight to the camera angle beside the
         * mouse's, and normalised by the same while-loops below. */
        if (g_portPadLookYawDeg != 0.0f)
        {
            stack_padding_9 += g_portPadLookYawDeg;
        }

        { /* GE_AIMDBG=1 aim probe: print whether insightaimmode is set and
           * where the crosshair sits, on aim-state change plus a 1s heartbeat
           * while aiming. */
            extern char *getenv(const char *);
            extern int printf(const char *, ...);
            static s32 aimdbg = -1;
            static s32 lastaim = -1;
            static s32 lasttick;

            if (aimdbg < 0)
            {
                const char *e = getenv("GE_AIMDBG");
                aimdbg = (e != NULL) ? ((e[0] == '2') ? 2 : 1) : 0; /* 2 = every tick while aiming (flick / snap hunts) */
            }
            if (aimdbg && (g_CurrentPlayer->insightaimmode != lastaim
                           || (g_CurrentPlayer->insightaimmode
                               && (aimdbg == 2 || (s32) (g_GlobalTimer - lasttick) > 60))))
            {
                lastaim = g_CurrentPlayer->insightaimmode;
                lasttick = g_GlobalTimer;
                printf("ge: aim: insight=%d controldef=%d mousedeg=(%+.2f,%+.2f) "
                       "swivel=(%+.3f,%+.3f) pos=(%+.3f,%+.3f) speed=(%.3f,%.3f) theta=%.1f "
                       "cross=(%.1f,%.1f) gunaim=(%.1f,%.1f) dx=%+.1f dy=%+.1f\n",
                       (s32) g_CurrentPlayer->insightaimmode, (s32) g_CurrentPlayer->controldef,
                       g_portMouseYawDeg, g_portMousePitchDeg,
                       g_bondviewMouseSwivelX, g_bondviewMouseSwivelY,
                       g_bondviewAimSwivelPosX, g_bondviewAimSwivelPosY,
                       g_CurrentPlayer->speedtheta, g_CurrentPlayer->speedverta,
                       g_CurrentPlayer->vv_theta,
                       g_CurrentPlayer->crosshair_angle.f[0], g_CurrentPlayer->crosshair_angle.f[1],
                       g_CurrentPlayer->field_FFC.x, g_CurrentPlayer->field_FFC.y,
                       g_CurrentPlayer->field_FFC.x - g_CurrentPlayer->crosshair_angle.f[0],
                       g_CurrentPlayer->field_FFC.y - g_CurrentPlayer->crosshair_angle.f[1]);
            }
        }
#endif

        while (stack_padding_9 < 0.0f)
        {
            stack_padding_9 += 360.0f;
        }
        while (stack_padding_9 >= 360.0f)
        {
            stack_padding_9 -= 360.0f;
        }

        g_CurrentPlayer->vv_theta = stack_padding_9;
    }

    bondviewApplyVertaTheta();

    // Handle crouching, and animation between standing and crouching.
    // Add basic block to declare local variables at the correct stack position.
    {
        f32 sp2AC;
        f32 stack_padding_15;

        sp2AC = 0.0f;
        if (currentPlayerGetCrouchPos() == CROUCH_SQUAT)
        {
            sp2AC = FULL_CROUCH_OFFSET;
        }
        else if (currentPlayerGetCrouchPos() == CROUCH_HALF)
        {
            sp2AC = -60.0f;
        }
        else
        {
            // removed?
            currentPlayerGetCrouchPos();
        }

        if (sp2AC != g_CurrentPlayer->ducking_height_offset)
        {
            chrobjApplySpeed(
                &g_CurrentPlayer->ducking_height_offset,
                sp2AC,
                &g_CurrentPlayer->field_A4,
                CHR_OBJ_ACCEL_SPEED_FACTOR,
                CHR_OBJ_ACCEL_SPEED_FACTOR,
                CHR_OBJ_MAXSPEED);
        }

        if (sp2AC == g_CurrentPlayer->ducking_height_offset)
        {
            g_CurrentPlayer->field_A4 = 0.0f;
        }
    }

    /**
     * Update forwards/backwards movement.
    */
    if (g_PlayerIsInTank == 1)
    {
        /**
         * This section handles the forward/backwards movement of the tank.
        */

        Mtxf sp268;
        struct coord3d sp25C;
        f32 sp258;
        f32 sp254;
        s32 stack_padding_14;
        s32 i_3;
        f32 ftemp_5;
        f32 tank_engine_utilization_percent;
        struct TankRecord *tank_obj;

        /**
         * Check to see if Bond is just now entering the tank.
         * If so, initialize the tank prop.
         * This also handles spinning Bond around (if required) to face the same direction as the turret.
        */
        if (g_EnterTankAudioState == TANK_RUN_STATE_NOT_RUNNING)
        {
            if (g_PlayerTankProp != NULL)
            {
                tank_obj = g_PlayerTankProp->obj;
                matrix_4x4_set_rotation_around_y(M_TAU_F - g_TankOrientationAngle, &sp268);
                matrix_scalar_multiply(tank_obj->model->scale, &sp268);

                sp25C.f[0] = g_TankModelPositionOffset.f[0];
                sp25C.f[1] = g_TankModelPositionOffset.f[1];
                sp25C.f[2] = g_TankModelPositionOffset.f[2];
                mtx4RotateVecInPlace(&sp268, (f32*)&sp25C);

                sp25C.f[0] += tank_obj->runtime_pos.f[0];
                sp25C.f[1] += tank_obj->runtime_pos.f[1];
                sp25C.f[2] += tank_obj->runtime_pos.f[2];

                sp258 = ((g_TankOrientationAngle + g_TankTurretOrientationAngleRad) * 360.0f) / M_TAU_F;
                sp254 = g_CurrentPlayer->vv_verta;
                if (sp254 < -20.0f)
                {
                    sp254 = -20.0f;
                }

                g_TankEnteringSitHeight += g_GlobalTimerDelta / 45.0f;
                if (g_TankEnteringSitHeight >= 1.0f)
                {
                    g_TankEnteringSitHeight = 1.0f;
                }

                g_TankEnteringSitHeightRemain = (cosf(g_TankEnteringSitHeight * M_TAU_F * 0.5f) + 1.0f) * 0.5f;

                g_CurrentPlayer->vv_verta =
                    (g_TankEnteringSitHeightRemain * g_TankEnterBondVertAngleDeg)
                    + ((1.0f - g_TankEnteringSitHeightRemain) * sp254);

                ftemp_5 = sp258 - g_TankEnterBondHorizAngleDeg;
                if (ftemp_5 > 180.0f)
                {
                    sp258 -= 360.0f;
                }
                if (ftemp_5 < -180.0f)
                {
                    sp258 += 360.0f;
                }

                g_CurrentPlayer->vv_theta =
                    (g_TankEnteringSitHeightRemain * g_TankEnterBondHorizAngleDeg)
                    + ((1.0f - g_TankEnteringSitHeightRemain) * sp258);

                if (g_CurrentPlayer->vv_theta >= 360.0f)
                {
                    g_CurrentPlayer->vv_theta -= 360.0f;
                }

                if (g_CurrentPlayer->vv_theta < 0.0f)
                {
                    g_CurrentPlayer->vv_theta += 360.0f;
                }

                move_offset.f[0] = (
                        (g_TankEnteringSitHeightRemain * g_EnterTankCoord.f[0]) +
                        ((1.0f - g_TankEnteringSitHeightRemain) * sp25C.f[0])
                    ) -
                    g_CurrentPlayer->field_488.collision_position.f[0];

                move_offset.f[1] = 0.0f;

                move_offset.f[2] = (
                    (g_TankEnteringSitHeightRemain * g_EnterTankCoord.f[2]) +
                    ((1.0f - g_TankEnteringSitHeightRemain) * sp25C.f[2])
                    ) -
                    g_CurrentPlayer->field_488.collision_position.f[2];
            }

            if (!(g_TankEnteringSitHeight >= 1.0f))
            {
            }
            else
            {
                g_EnterTankAudioState = TANK_RUN_STATE_STARTING;
            }
        }
        /**
         * Else, Bond has already entered the tank.
        */
        else
        {
            /**
             * There's an initial "starting" step.
            */
            if (g_EnterTankAudioState == TANK_RUN_STATE_STARTING)
            {
                g_EnterTankAudioState = TANK_RUN_STATE_RUNNING;
                if ((g_TankSfxState[0] == NULL) && (lvlGetControlsLockedFlag() == 0))
                {
                    sndPlaySfx(g_musicSfxBufferPtr, TRUCK_START_SFX, &g_TankSfxState[0]);
                }

                sndCreatePostEvent(g_TankSfxState[0], 8, 0x61A8);
                g_TankEngineSfxVolume = 0x61A8;
            }
            /**
             * Else Bond has fully entered the tank, and the engine is running.
             * Update turret vertical angle.
             * Update engine sound effect volume based on current tank speed.
            */
            else
            {
                f32 tank_scaled_speedforwards;
                f32 tank_scaled_speedtheta;
                f32 tank_vertical_angle;

                tank_scaled_speedforwards = g_CurrentPlayer->speedforwards / TANK_MAX_SPEED;
                tank_scaled_speedtheta = g_CurrentPlayer->speedtheta / 0.3f;

                if (tank_scaled_speedforwards < 0.0f)
                {
                    tank_scaled_speedforwards = -tank_scaled_speedforwards;
                }
                if (tank_scaled_speedtheta < 0.0f)
                {
                    tank_scaled_speedtheta = -tank_scaled_speedtheta;
                }

                tank_engine_utilization_percent = tank_scaled_speedforwards;
                if (tank_scaled_speedforwards < tank_scaled_speedtheta)
                {
                    tank_engine_utilization_percent = tank_scaled_speedtheta;
                }

                if (tank_engine_utilization_percent > 0.0f)
                {
                    if (tank_engine_utilization_percent > 1.0f)
                    {
                        tank_engine_utilization_percent = 1.0f;
                    }

                    if (g_TankSfxState[1] == NULL)
                    {
                        if (lvlGetControlsLockedFlag() == 0)
                        {
                            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, TANK_SFX, &g_TankSfxState[1]);
                        }
                    }

                    if (g_TankSfxState[1] != NULL)
                    {
                        s32 phi_a2;

                        phi_a2 = 0x7FFF;
                        if (tank_engine_utilization_percent < 0.15f)
                        {
                            phi_a2 = (s32) ((tank_engine_utilization_percent * 20000.0f) / 0.15f);
                        }
                        else if (tank_engine_utilization_percent < 0.9f)
                        {
                            phi_a2 = (s32) ((((tank_engine_utilization_percent - 0.15f) * 12767.0f) / 0.75f) + 20000.0f);
                        }

                        sndCreatePostEvent(g_TankSfxState[1], 8, phi_a2);
                    }
                }
                else
                {
                    if (g_TankSfxState[1] != NULL)
                    {
                        if (sndGetPlayingState(g_TankSfxState[1]) != 0)
                        {
                            sndDeactivate(g_TankSfxState[1]);
                        }
                    }
                }

                if (g_TankSfxState[0] == NULL)
                {
                    if (lvlGetControlsLockedFlag() == 0)
                    {
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, TRUCK_RUN_SFX, &g_TankSfxState[0]);
                    }
                }

                if (g_TankSfxState[0] != NULL)
                {
                    g_TankEngineSfxVolume = 0x7FFF;
                    if (tank_engine_utilization_percent < 0.9f)
                    {
                        g_TankEngineSfxVolume = (s32) (((tank_engine_utilization_percent * 7767.0f) / 0.9f) + 25000.0f);
                    }

                    sndCreatePostEvent(g_TankSfxState[0], 8, g_TankEngineSfxVolume);
                }

                if (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_TANKSHELLS)
                {
                    tank_vertical_angle = g_CurrentPlayer->field_2A08;
                    tank_vertical_angle += 0.17453294f; /* should be DegToRad1Fact(10), but that yields 0.17453293f */
                }
                else
                {
                    tank_vertical_angle = g_TankTurretVerticalAngle;
                }

                if (tank_vertical_angle > DegToRad1Fact(25))
                {
                    tank_vertical_angle = DegToRad1Fact(25);
                }

                /* -0.087266468f should be DegToRad1Fact(-5), but that yields -0.0872664600611 */
                if (tank_vertical_angle < -0.087266468f)
                {
                    tank_vertical_angle = -0.087266468f;
                }

#ifndef TARGET_N64
                if (portSmoothOn())
                {
                    portLagStep(&g_TankTurretVerticalAngleRelated, TANKTURRETVERTICALANGLERELATED_SCALE, tank_vertical_angle, g_GlobalTimerDelta);
                }
                else
#endif
                for (i_3=0; i_3<g_ClockTimer; i_3++)
                {
                    g_TankTurretVerticalAngleRelated = (TANKTURRETVERTICALANGLERELATED_SCALE * g_TankTurretVerticalAngleRelated) + tank_vertical_angle;
                }

                g_TankTurretVerticalAngle = g_TankTurretVerticalAngleRelated * (1.0f - TANKTURRETVERTICALANGLERELATED_SCALE);
            }

        }

        g_CurrentPlayer->bondbreathing -= (0.750f * g_GlobalTimerDelta) / 2700.0f;

        if (g_CurrentPlayer->bondbreathing < 0.0f)
        {
            g_CurrentPlayer->bondbreathing = 0.0f;
        }

        bondviewMoveAnimationTick(0.0f, 0.0f, 0.0f);

        move_offset.f[0] += g_CurrentPlayer->speedforwards * sinf(M_TAU_F - g_TankOrientationAngle) * g_GlobalTimerDelta;
        move_offset.f[2] += g_CurrentPlayer->speedforwards * cosf(M_TAU_F - g_TankOrientationAngle) * g_GlobalTimerDelta;

        bondviewCalcUpdatePlayerCollision(&move_offset, 1);

        if ((g_EnterTankAudioState == TANK_RUN_STATE_RUNNING) && (g_ClockTimer > 0))
        {
            f32 calc_x;
            f32 calc_z;
            f32 calc_speedforwards;

#if defined(VERSION_EU)
            // Divide by zero check.
            if (g_GlobalTimerDelta == 0)
            {
                #if DEBUG
                    // unknown what went here.
                    return_null();
                #endif
            }
#endif

            calc_x = (g_CurrentPlayer->field_488.collision_position.f[0] - g_CurrentPlayer->bondprevpos.f[0]) / g_GlobalTimerDelta;
            calc_z = (g_CurrentPlayer->field_488.collision_position.f[2] - g_CurrentPlayer->bondprevpos.f[2]) / g_GlobalTimerDelta;
            calc_speedforwards = sqrtf((calc_x * calc_x) + (calc_z * calc_z));

            if (g_CurrentPlayer->speedforwards < 0.0f)
            {
                calc_speedforwards = -calc_speedforwards;
            }

            g_CurrentPlayer->speedforwards = calc_speedforwards;
        }
    }
    else // not in tank: g_PlayerIsInTank != 1
    {
        f32 sp220;
        f32 sp21C;
        f32 dist;
        f32 ftemp_col_x;
        f32 ftemp_col_z;
        f32 stack_padding_12;
        f32 start_collision_pos_x;
        f32 start_collision_pos_z;
        struct StandTile *sp200;
        f32 stack_padding_2;
        s32 stack_padding_11;
        f32 speedforwards;
        f32 ftemp_11;
        f32 speedsideways;
        f32 speedtheta;
        f32 stack_padding_25;
        f32 stack_padding_5;
        s32 stack_padding_6;
        f32 sp164;
        f32 sp2B0;
        f32 stack_padding_3;
        f32 stack_padding_111;
        f32 ftemp_26;
        f32 nd; // canonical name
        f32 ftemp_7;
        f32 sp240;
#ifdef TARGET_N64
        struct move_bond_temp_struct curLocus;
#else
        struct StandTileLocusCallbackRecord curLocus;
#endif
        struct move_bond_collision bondCollision;
        f32 shorten; // canonical name
        f32 headpos_x;
        f32 headpos_z;
        struct StandTile *sp174;
        struct StandTile *sp170;
        f32 sp16C;
        f32 sp168;

        if ((g_TankSfxState[0] != NULL) && (sndGetPlayingState(g_TankSfxState[0]) != 0))
        {
            #if defined(VERSION_US)
            g_TankEngineSfxVolume -= (g_ClockTimer * 1000);
            #endif

            #if defined(VERSION_JP) || defined(VERSION_EU)
            g_TankEngineSfxVolume -= (s32)(1000.0f * g_GlobalTimerDelta);
            #endif

            if (g_TankEngineSfxVolume > 0)
            {
                sndCreatePostEvent(g_TankSfxState[0], 8, g_TankEngineSfxVolume);
            }
            else
            {
                sndDeactivate(g_TankSfxState[0]);
            }
        }

        if ((g_TankSfxState[1] != NULL) && (sndGetPlayingState(g_TankSfxState[1]) != 0))
        {
            sndDeactivate(g_TankSfxState[1]);
        }


        ftemp_7 = (g_BondMoveAnimationSetup[1].speedMultiplier * 0.5f  * g_GlobalTimerDelta);
        sp3A0  = g_CurrentPlayer->speedsideways * ftemp_7;

        /*
            The following is similar to a block of Perfect Dark bwalk0f0c69b8.
        */

        ftemp_26 = -g_CurrentPlayer->swaytarget * g_CurrentPlayer->field_488.theta_transform.f[2];
        ftemp_11 = g_CurrentPlayer->swaytarget * g_CurrentPlayer->field_488.theta_transform.f[0];

        sp220 = (ftemp_26) - g_CurrentPlayer->swayoffset0;
        sp21C = (ftemp_11) - g_CurrentPlayer->swayoffset2;

        dist = (sp220 * sp220) + (sp21C * sp21C);

        if (dist >= 100.0f)
        {
#ifdef TARGET_N64
            sp220 *= 0.6f * 1.0f;
            sp21C *= 0.6f * 1.0f;
#else
            /* PORT: the 0.6 lean approach factor is applied per tick without
             * delta scaling, so the PC's faster ticks leaned ~2x too quickly;
             * delta-scale it as k = 1 - 0.4^(delta/ref) to match the console. */
            /* Reference tick length the 0.6 step was tuned at (console at full
             * speed); GE_LEANDBG diagnostics are kept. */
            /* Explicit prototypes: the tree's IDO math.h hides its float
             * declarations from GCC, and an implicit powf returns garbage (lean
             * snapped in one frame); atof needs one for the same reason. */
            extern float powf(float, float);
            /* 2.0 (the NTSC console's full-speed tick) makes a full lean take
             * 6 frames at 60fps instead of 7. */
            const f32 leanref = 2.0f;
            f32 leank = 1.0f - powf(0.4f, g_GlobalTimerDelta / leanref);
            /* GE_LEANDBG=1: print the actual inputs while leaning. */
            {
                static int leandbg = -1;
                if (leandbg < 0) {
                    extern char *getenv(const char *);
                    leandbg = getenv("GE_LEANDBG") != NULL;
                }
                if (leandbg && g_CurrentPlayer->swaytarget != 0.0f) {
                    extern int printf(const char *, ...);
                    printf("ge: lean delta=%.3f ref=%.3f k=%.3f off=(%.2f %.2f)\n",
                           g_GlobalTimerDelta, leanref, leank,
                           g_CurrentPlayer->swayoffset0, g_CurrentPlayer->swayoffset2);
                }
            }
            sp220 *= leank;
            sp21C *= leank;
#endif
        }

        speedsideways = g_CurrentPlayer->speedsideways * MAX_SPEED_FACTOR;
        speedforwards = g_CurrentPlayer->speedforwards;
        speedtheta = g_CurrentPlayer->speedtheta * MAX_SPEED_FACTOR;

        if (speedsideways < 0.0f)
        {
            speedsideways = -speedsideways;
        }
        if (speedforwards < 0.0f)
        {
            speedforwards = -speedforwards;
        }
        if (speedtheta < 0.0f)
        {
            speedtheta = -speedtheta;
        }

        maxspeed = speedforwards;

#if defined(VERSION_EU)
        if (maxspeed < speedsideways && 1)
#else
        if (maxspeed < speedsideways)
#endif
        {
            maxspeed = speedsideways;
        }
        if (maxspeed < speedtheta)
        {
            maxspeed = speedtheta;
        }

        if (dist >= 0.1f && maxspeed < MAX_SPEED_FACTOR)
        {
            maxspeed = MAX_SPEED_FACTOR;
        }

        if (maxspeed >= 0.750f)
        {
            g_CurrentPlayer->bondbreathing += (maxspeed - 0.750f) * g_GlobalTimerDelta / 900.0f ;
        }
        else
        {
            g_CurrentPlayer->bondbreathing -= (0.750f - maxspeed) * g_GlobalTimerDelta / 2700.0f;
        }

        if (g_CurrentPlayer->bondbreathing < 0.0f)
        {
            g_CurrentPlayer->bondbreathing = 0.0f;
        }
        else if (g_CurrentPlayer->bondbreathing > 1.0f)
        {
            g_CurrentPlayer->bondbreathing = 1.0f;
        }

        // perfect dark call: bmove0f0cc654
        bondviewMoveAnimationTick(maxspeed, g_CurrentPlayer->speedforwards, sp3A0);

        headpos_x = g_CurrentPlayer->headpos.f[0];
        headpos_z = g_CurrentPlayer->headpos.f[2];

        move_offset.f[0] +=
            (
                (headpos_z * g_CurrentPlayer->field_488.theta_transform.f[0]) -
                (headpos_x * g_CurrentPlayer->field_488.theta_transform.f[2])
            ) * g_GlobalTimerDelta;

        move_offset.f[2] +=
            (
                (headpos_z * g_CurrentPlayer->field_488.theta_transform.f[2]) +
                (headpos_x * g_CurrentPlayer->field_488.theta_transform.f[0])
            ) * g_GlobalTimerDelta;


        move_offset.f[0] += sp220;
        move_offset.f[2] += sp21C;

        start_collision_pos_x = g_CurrentPlayer->field_488.collision_position.f[0];
        start_collision_pos_z = g_CurrentPlayer->field_488.collision_position.f[2];
        sp200 = g_CurrentPlayer->field_488.current_tile_ptr;

        if (get_debug_fast_bond_flag())
        {
            move_offset.f[0] +=
                (
                    (g_CurrentPlayer->field_488.theta_transform.f[0] * g_CurrentPlayer->speedforwards) -
                    (g_CurrentPlayer->field_488.theta_transform.f[2] * g_CurrentPlayer->speedsideways)
                ) * g_GlobalTimerDelta * 10.0f;

            move_offset.f[2] +=
                (
                    (g_CurrentPlayer->field_488.theta_transform.f[2] * g_CurrentPlayer->speedforwards) +
                    (g_CurrentPlayer->field_488.theta_transform.f[0] * g_CurrentPlayer->speedsideways)
                ) * g_GlobalTimerDelta * 10.0f;
        }

        bondviewCalcUpdatePlayerCollision(&move_offset, (g_CurrentPlayer->swaytarget == 0.0f));

        stanTileDistanceRelated(
            &sp200,
            start_collision_pos_x,
            start_collision_pos_z,
            g_CurrentPlayer->field_488.collision_radius * 1.16f,
            &curLocus);

        /* almost never true */
        if (stanGetLocusCount(&curLocus) != 0)
        {
            use_stanHeight = 1;
        }

        stanTileDistanceRelated(
            &g_CurrentPlayer->field_488.current_tile_ptr,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2],
            g_CurrentPlayer->field_488.collision_radius * 1.01f,
            &curLocus);

        /* almost never true */
        if (stanGetLocusCount(&curLocus) != 0)
        {
            use_stanHeight = 1;
        }

        stanTileDistanceRelated(
            &g_CurrentPlayer->field_488.current_tile_ptr,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2],
            g_CurrentPlayer->field_488.collision_radius,
            &curLocus);

        /* almost always true */
        if (stanGetLocusCount(&curLocus) == 0)
        {
            stanTileDistanceRelated(
                &sp200,
                start_collision_pos_x,
                start_collision_pos_z,
                g_CurrentPlayer->field_488.collision_radius * 0.990099f,
                &curLocus);
        }

        /* almost never true */
        if (stanGetLocusCount(&curLocus))
        {
            use_stanHeight = 1;
            stanGetMoveBondCollisionTiles(&sp174, &sp170, &bondCollision);

            if (g_CurrentPlayer->stanHeight <= bondCollision.sp19C.f[1])
            {
                f32 sp2A8;
                f32 sp24C;
                f32 sp250;
                f32 sp310;
                f32 sp314;

                sp168 = bondCollision.bondCollision.f[0] - bondCollision.sp190.f[0];
                sp16C = bondCollision.sp190.f[2] - bondCollision.bondCollision.f[2];

                nd = sqrtf((sp16C * sp16C) + (sp168 * sp168));

                sp168 = sp168 / nd;
                sp16C = sp16C / nd;

                sp164 =
                    ((start_collision_pos_x - bondCollision.bondCollision.f[0]) * sp16C) +
                    ((start_collision_pos_z - bondCollision.bondCollision.f[2]) * sp168);

                sp2B0 =
                    (((start_collision_pos_x + move_offset.f[0]) - bondCollision.bondCollision.f[0]) * sp16C) +
                    (((start_collision_pos_z + move_offset.f[2]) - bondCollision.bondCollision.f[2]) * sp168);

                if ((sp164 * sp2B0) <= 0.0f)
                {
                    shorten = 0.0f;
                }
                else
                {
                    if (sp2B0 < 0.0f)
                    {
                        sp164 = -sp164;
                        sp2B0 = -sp2B0;
                    }

                    if (sp164 <= sp2B0)
                    {
                        shorten = 0.0f;
                    }
                    else if (g_CurrentPlayer->field_488.collision_radius < sp2B0)
                    {
                        shorten = 0.0f;
                    }
                    else if (sp164 < g_CurrentPlayer->field_488.collision_radius)
                    {
                        shorten = 0.0f;
                    }
                    else
                    {
                        shorten = (sp164 - g_CurrentPlayer->field_488.collision_radius) / (sp164 - sp2B0);
                    }
                }

                sp2A8 = sqrtf((move_offset.f[0] * move_offset.f[0]) + (move_offset.f[2] * move_offset.f[2]));
                if (sp2A8 > 0.0f)
                {
                    f32 sp318;

                    sp318 = (1.0f - shorten) * sp2A8;
                    sp390 = sp318 * 0.25f;

                    if (bondCollision.sp19C.f[1] <= (g_CurrentPlayer->stanHeight + sp390))
                    {
                        sp390 = (bondCollision.sp19C.f[1] - g_CurrentPlayer->stanHeight);
                        sp318 -= (sp390 / 0.25f);
                        shorten += (sp318 / sp2A8);
                    }
                }

                sp314 = bondCollision.sp19C.f[0] - bondCollision.bondCollision.f[0];
                sp310 = bondCollision.sp19C.f[2] - bondCollision.bondCollision.f[2];
                sp250 = bondCollision.sp1A8.f[0] - bondCollision.bondCollision.f[0];
                sp24C = bondCollision.sp1A8.f[2] - bondCollision.bondCollision.f[2];

                if (((sp250 * sp250) + (sp24C * sp24C)) < ((sp314 * sp314) + (sp310 * sp310)))
                {
                    sp314 = sp250;
                    sp310 = sp24C;
                }

                move_offset.f[0] = (move_offset.f[0] * shorten) + (sp314 * (sp390 / (bondCollision.sp19C.f[1] - bondCollision.bondCollision.f[1])));
                move_offset.f[2] = (move_offset.f[2] * shorten) + (sp310 * (sp390 / (bondCollision.sp19C.f[1] - bondCollision.bondCollision.f[1])));

                g_CurrentPlayer->field_488.collision_position.f[0] = start_collision_pos_x;
                g_CurrentPlayer->field_488.collision_position.f[2] = start_collision_pos_z;
                g_CurrentPlayer->field_488.current_tile_ptr = sp200;

                bondviewCalcUpdatePlayerCollision(&move_offset, (g_CurrentPlayer->swaytarget == 0.0f));
            }
        }

        ftemp_col_x = g_CurrentPlayer->field_488.collision_position.f[0] - start_collision_pos_x;
        ftemp_col_z = g_CurrentPlayer->field_488.collision_position.f[2] - start_collision_pos_z;
        sp240 = (move_offset.f[0] * move_offset.f[0]) + (move_offset.f[2] * move_offset.f[2]);
        if (sp240 != 0.0f)
        {
            sp240 = ((ftemp_col_x * ftemp_col_x) + (ftemp_col_z * ftemp_col_z)) / sp240;
        }
        sp240 = sqrtf(sp240);
        g_CurrentPlayer->swayoffset0 += sp240 * sp220;
        g_CurrentPlayer->swayoffset2 += sp240 * sp21C;
    }

    // add basic block
    {
        f32 breathing;
        f32 sp14C_temp;
        f32 weapon_speed_verta;

        sp14C_temp = g_CurrentPlayer->speedtheta;
        weapon_speed_verta =
            (g_CurrentPlayer->speedverta / 0.7f) +
            (g_CurrentPlayer->field_A4 / CHR_OBJ_MAXSPEED);

        /*
        Following matches the end of Perfect Dark bwalk0f0c69b8
        */
        breathing = bheadGetBreathingValue();

        if (weapon_speed_verta > 1.0f)
        {
            weapon_speed_verta = 1.0f;
        }
        else if (weapon_speed_verta < -1.0f)
        {
            weapon_speed_verta = -1.0f;
        }

        if (g_CurrentPlayer->headanim == 1)
        {
            breathing *= 1.2f;
        }

        // Perfect Dark call bgun0f09d8dc
        gunSetBondWeaponSway(breathing, maxspeed, weapon_speed_verta, sp14C_temp);

        // Perfect Dark call bgunSetAdjustPos
        gunSetOffsetRelated(DegToRad1Fact(g_CurrentPlayer->vv_verta360));
    }

    // end perfect dark `void bwalk0f0c69b8(void)`

    /**
     * The following section updates the TankRecord fields, and handles prop collision detection
     * with the tank. If colliding with character, play the "arrrhghhg" sound effect, or if
     * colliding with prop then set tank movement penalty and create an explosion.
    */
    if ((g_PlayerTankProp != NULL) && (g_PlayerIsInTank == 1) && (g_EnterTankAudioState == TANK_RUN_STATE_RUNNING))
    {
        struct PropRecord *prop;
        struct TankRecord *sp140_tank_as_TankRecord;
        struct ObjectRecord *sp138_tank_as_ObjectRecord;
        f32 ftemp_12;
        struct ModelNode_BoundingBoxRecord *sp130;
        Mtxf spF0;
        struct coord3d spE4;
        s32 stack_padding_13;
        s32 i_4;
        s32 stemp;
        void *stack_padding_8;
        struct rect4f spB4_tank_collision_bounds;
        // roomids
        s32 sp94[8];
        s32 stanlineret;
        s16 *lookup_index;

        sp140_tank_as_TankRecord = ((struct TankRecord *)g_PlayerTankProp->obj);
        sp138_tank_as_ObjectRecord = (struct  ObjectRecord*)g_PlayerTankProp->obj;
        sp130 = (struct ModelNode_BoundingBoxRecord *)((struct ModelNode *)sp138_tank_as_ObjectRecord->model->obj->Switches)->Child->Data;

        sp140_tank_as_TankRecord->is_firing_tank = (getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_TANKSHELLS)
            && get_hands_firing_status(GUNRIGHT);

        sp140_tank_as_TankRecord->turret_vertical_angle = g_TankTurretVerticalAngle;
        sp140_tank_as_TankRecord->turret_orientation_angle = g_TankTurretOrientationAngleRad;
        sp140_tank_as_TankRecord->tank_orientation_angle = g_TankOrientationAngle;

        matrix_4x4_set_rotation_around_y(M_TAU_F - g_TankOrientationAngle, &spF0);
        matrix_scalar_multiply(sp138_tank_as_ObjectRecord->model->scale, &spF0);

        spE4.f[0] = -g_TankModelPositionOffset.f[0];
        spE4.f[1] = -g_TankModelPositionOffset.f[1];
        spE4.f[2] = -g_TankModelPositionOffset.f[2];

        mtx4RotateVecInPlace(&spF0, &spE4);

        spE4.f[0] += g_CurrentPlayer->field_488.collision_position.f[0];
        spE4.f[2] += g_CurrentPlayer->field_488.collision_position.f[2];

        sp138_tank_as_ObjectRecord->prop->stan = g_CurrentPlayer->field_488.current_tile_ptr;

        stanlineret = walkTilesBetweenPoints_NoCallback(
            &sp138_tank_as_ObjectRecord->prop->stan,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2],
            spE4.f[0],
            spE4.f[2]);
        #ifdef DEBUG
        assert(stanlineret); // #7362
        #endif

        sp140_tank_as_TankRecord->stan_y = stanGetPositionYValue(sp138_tank_as_ObjectRecord->prop->stan, spE4.f[0], spE4.f[2]);

#ifndef TARGET_N64
        if (portSmoothOn())
        {
            portLagStep(&sp140_tank_as_TankRecord->unkD0, TANK_UNKD0_SCALE, sp140_tank_as_TankRecord->stan_y, g_GlobalTimerDelta);
        }
        else
#endif
        for (i_4=0; i_4<g_ClockTimer; i_4++)
        {
            sp140_tank_as_TankRecord->unkD0 = (sp140_tank_as_TankRecord->unkD0 * TANK_UNKD0_SCALE) + sp140_tank_as_TankRecord->stan_y;
        }

        ftemp_12 = (sp140_tank_as_TankRecord->unkD0 * (1.0f - TANK_UNKD0_SCALE));
        spE4.f[1] = ftemp_12
            - (chrpropBBOXGetYmin(sp130) * sp138_tank_as_ObjectRecord->model->scale)
            + 4.0f;

        matrix_4x4_copy(&spF0,  &sp138_tank_as_ObjectRecord->mtx);

        sp138_tank_as_ObjectRecord->runtime_pos.f[0] = sp138_tank_as_ObjectRecord->prop->pos.f[0] = spE4.f[0];
        sp138_tank_as_ObjectRecord->runtime_pos.f[1] = sp138_tank_as_ObjectRecord->prop->pos.f[1] = spE4.f[1];
        sp138_tank_as_ObjectRecord->runtime_pos.f[2] = sp138_tank_as_ObjectRecord->prop->pos.f[2] = spE4.f[2];

        setupUpdateObjectRoomPosition(sp138_tank_as_ObjectRecord);
        chrobjCollisionRelated(sp138_tank_as_ObjectRecord);
        bondviewGetTankCollisionBounds(&spB4_tank_collision_bounds, &g_CurrentPlayer->field_488.collision_position, g_TankOrientationAngle);
        chraiGetPropRoomIds(sp138_tank_as_ObjectRecord->prop, &sp94);

        // update num_obj_position_data_entries
        roomGetProps(&sp94);

        for (lookup_index=ptr_list_object_lookup_indices; *lookup_index>=0; lookup_index++)
        {
            prop = &g_Props[*lookup_index];
            if (prop != sp138_tank_as_ObjectRecord->prop)
            {
                if (prop->type == PROP_TYPE_CHR)
                {
                    s32 sp88_collision_bound_height;
                    s32 sp84_collision_bound_z;
                    f32 sp80_collision_radius;
                    s32 sp7C;
                    struct coord3d sp70;
                    struct ChrRecord *sp6C;
                    f32 stack_padding_28;

                    sp7C = 1;
                    sp6C = prop->chr;
                    chrpropGetCollisionBounds(prop, &sp80_collision_radius, &sp88_collision_bound_height, &sp84_collision_bound_z);

                    if (chrpropTestPointInPolygon(&prop->pos, &spB4_tank_collision_bounds, 4))
                    {
                        sp7C = 0;

                        if (sp6C->actiontype == ACT_DIE)
                        {
#if defined(VERSION_US)
                            /* (s32): CHRFLAG has a 0x80000000 member, so GCC makes the
                               enum unsigned and this IDO sign-bit test is always true,
                               retriggering the crush sound every tick. No-op under IDO. */
                            if (((s32)(sp6C->chrflags << 7)) >= 0)
#endif
#if defined(VERSION_JP) || defined(VERSION_EU)
                            if (((s32)(sp6C->chrflags << 7)) >= 0 && lvlGetControlsLockedFlag() == 0)
#endif
                            {
                                sp6C->chrflags |= CHRFLAG_01000000;
                                if ((g_GlobalTickCount % 3) < 2)
                                {
                                    chrobjSndCreatePostEventDefault(sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, CRUSHED_YELL_SFX, NULL), &prop->pos);
                                }
                                if ((g_GlobalTickCount % 3) > 0)
                                {
                                    chrobjSndCreatePostEventDefault(sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, TANK_CRUSH_MAN_SFX, NULL), &prop->pos);
                                }
                            }
                        }
                    }

                    if (sp7C && (chrobjTestPointPolygonCollision(&prop->pos, sp80_collision_radius, &spB4_tank_collision_bounds, 4)))
                    {
                        sp7C = 0;
                    }

                    if (sp7C == 0)
                    {
                        sp70.f[0] = sp138_tank_as_ObjectRecord->runtime_pos.f[0];
                        sp70.f[1] = prop->pos.f[1];
                        sp70.f[2] = sp138_tank_as_ObjectRecord->runtime_pos.f[2];

                        chrlvExplosionDamage(prop->chr, &sp70, 3.0f, 1);
                    }
                }
                else if (prop->type == PROP_TYPE_OBJ)
                {
                    struct rect4f *polygon;
                    s32 edges;

                    chraiGetCollisionBoundsWithoutY(prop, &polygon, &edges);
                    if ((edges > 0) && chrobjTestPolygonsTouchingOrOverlap2D(polygon, edges, &spB4_tank_collision_bounds, 4))
                    {
                        // Explode destroyable props when the tank touches them
                        maybe_detonate_object_and_its_children(prop, 10000.0f, &prop->obj->runtime_pos, 0x20, get_cur_playernum());
                        g_TankDamagePenaltyTicks = TANK_DAMAGE_PENTALTY_TICKS;
                    }
                }
            }
        }
    }

    bondviewUpdatePlayerY(use_stanHeight, sp390);
    bondviewUpdatePlayerCollisionPositionFields();
    bondviewUpdatePlayerCollisionBounds();

    if (get_debug_man_pos_flag() != 0)
    {
        f32 sp5C_out_unused;

        copy_tile_RGB_as_24bit(
            g_CurrentPlayer->field_488.current_tile_ptr,
            g_CurrentPlayer->field_488.collision_position.f[0],
            g_CurrentPlayer->field_488.collision_position.f[2],
            &sp5C_out_unused);
    }
}


/**
 * US address 7F086990.
 * EU address 7F086AB0.
*/
void bondviewFrozenMoveBond(s8 stick_x, s8 stick_y, u16 buttons, u16 oldbuttons)
{
    struct coord3d property_pos;
    struct coord3d property_pos2;
    struct coord3d property_offset;
    struct coord3d offset;
    struct StandTile *room_pointer_tile;
    struct coord3d stan_walk_start;

#ifndef TARGET_N64
    /* MoveBond ticks it in play; the cutscenes run this instead. It carries
     * the classic pillars and the HUD slide (the intro's bottom-left text
     * rides it: frozen at Wide, it snapped back under the pillars), and the
     * pause box itself, which a mission aborted from the watch left at 1:
     * its pillars and bars then held through the next level's intro. */
    portPauseBoxTick();
#endif
    property_pos = g_DefaultFrozenPlayerPos;
    property_pos2 = g_DefaultFrozenPlayerPos2;
    property_offset = g_DefaultFrozenPlayerOffset;
    offset = g_DefaultFrozenMoveOffset;

#ifndef TARGET_N64
    g_portBondFrozenTick = 1;
#endif
    bondviewPlayerTickDamageAndHealth();
    bondviewPlayerTickExplode();
    bondviewProcessInput(0, 0, 0, 0);
    bondviewApplyVertaTheta();
    bondviewMoveAnimationTick(0, 0, 0);

    if ((g_ForceBondMoveOffset.f[0] != 0.0f) || (g_ForceBondMoveOffset.f[2] != 0.0f))
    {
        offset.f[0] += g_ForceBondMoveOffset.f[0] * g_GlobalTimerDelta;
        offset.f[2] += g_ForceBondMoveOffset.f[2] * g_GlobalTimerDelta;
    }

    offset.f[0] += ((g_CurrentPlayer->headpos.f[2] * g_CurrentPlayer->field_488.theta_transform.f[0]) - (g_CurrentPlayer->headpos.f[0] * g_CurrentPlayer->field_488.theta_transform.f[2])) * g_GlobalTimerDelta;
    offset.f[2] += ((g_CurrentPlayer->headpos.f[2] * g_CurrentPlayer->field_488.theta_transform.f[2]) + (g_CurrentPlayer->headpos.f[0] * g_CurrentPlayer->field_488.theta_transform.f[0])) * g_GlobalTimerDelta;

    bondviewCalcUpdatePlayerCollision(&offset, 1);
    bondviewUpdatePlayerY(0, 0.0f);
    bondviewUpdatePlayerCollisionPositionFields();

    if ((g_CameraMode == CAMERAMODE_FP_NOINPUT) || (g_CameraMode == CAMERAMODE_FP) || (g_CameraMode == CAMERAMODE_FADE_TO_TITLE))
    {
        currentPlayerSetCameraMode(0);
        return;
    }

    bondviewFrozenCameraTick(buttons, oldbuttons, &property_pos, &property_pos2, &property_offset, &room_pointer_tile, &stan_walk_start);
    currentPlayerSetCameraMode(1);
    bondviewSetCurrentPlayerPosition(&property_pos, &property_pos2, &property_offset, room_pointer_tile, &stan_walk_start);
}


#ifndef TARGET_N64
/**
 * WIDESCREEN: keep the game's whole coordinate space 4:3 and widen ONLY the
 * projection aspect fed to guPerspectiveF (PD's model), so 2D stays put.
 *
 * Culling follows for free: c_scalex derives from the player aspect, and room
 * culling is screen-space against the 4:3 rect the wide projection compresses
 * into. The fast3d interpreter then expands wide-frame 3D to the window and
 * pins 2D texrects to a centred 4:3 band (gfx_ge_aspect_pin; video.c decides
 * per frame from portSetSceneWide).
 */
extern f32 portWideFactor(void);       /* windowAspect/(4:3), video.c; weak 1.0 headless */
extern void portSetSceneWide(s32 wide); /* this frame renders a wide 3D view */
#endif

s16 getWidth320or440(void)
{
    if (cameraBufferToggle != 0)
    {
        return SCREEN_WIDTH_440;
    }

    return SCREEN_WIDTH_320;
}


s16 getHeight330or240(void)
{
    if (cameraBufferToggle != 0)
    {
        return SCREEN_HEIGHT_330;
    }

    return SCREEN_HEIGHT;
}

#ifndef TARGET_N64
/* FULL HEIGHT: the NTSC single-player viewport is 320x220 at y=10, an
 * overscan margin a CRT hid but a PC window shows as black bands. When active,
 * the full-screen viewport becomes all 240 lines at y=0; scissor, aspect and
 * room culling derive from these two getters, and the fovy is compensated in
 * lvlRender so the horizontal FOV stays vanilla.
 *
 * VANILLA DURING DEMO PLAYBACK: the view rect gates portal room descent (more
 * rooms -> more active guards -> different RNG). GE_FULLHEIGHT=0 disables it.
 *
 * SPLIT SCREEN: the top views gain the ten lines above them (109 @ 10 -> 119 @
 * 0) and the bottom views the ten below (109 @ 121 -> 119 @ 121); the two-line
 * divider stays. Each view stays centred on itself, so it gains five lines
 * above and below its old picture. */
s32 portFullHeightActive(void)
{
    extern char *getenv(const char *);
    extern s32 get_is_ramrom_flag(void);
    static s32 want = -1;

    if (want < 0)
    {
        const char *e = getenv("GE_FULLHEIGHT");
        want = !(e != NULL && e[0] == '0');
    }
    return want && get_is_ramrom_flag() == 0;
}
#endif

s16 bondviewGetCurrentPlayerViewportWidth(void)
{
    if (getPlayerCount() >= 3)
    {
        return VIEWPORT_WIDTH_4P;
    }

    if (cameraBufferToggle != 0)
    {
        return SCREEN_WIDTH_440;
    }

    if (cur_player_get_screen_setting() == SCREEN_SIZE_WIDESCREEN)
    {
        return VIEWPORT_WIDTH_WIDESCREEN;
    }

    if (cur_player_get_screen_setting() == SCREEN_SIZE_CINEMA)
    {
        return VIEWPORT_WIDTH_CINEMA;
    }

    return VIEWPORT_WIDTH_FULLSCREEN;
}

s16 get_curplayer_viewport_ulx(void)
{
    if (2 < getPlayerCount())
    {
        if ((get_cur_playernum() == 1) || (get_cur_playernum() == 3))
        {
                return 0xa1;
        }
    }

    return 0;
}




/**
 * Address 0x7F086D24.
 */
s16 bondviewGetCurrentPlayerViewportHeight(void)
{
    f32 t;

    if (getPlayerCount() >= 2)
    {
#if !defined(TARGET_N64) && !defined(VERSION_EU)
        if (portFullHeightActive())
        {
            return (SCREEN_HEIGHT / 2) - 1; /* to the screen edge; the divider keeps its two lines */
        }
#endif
        return VIEWPORT_HEIGHT_4P;
    }

    if (cameraBufferToggle != 0)
    {
        if (cur_player_get_screen_setting() == SCREEN_SIZE_WIDESCREEN)
        {
            return VIEWPORT_HEIGHT_WIDESCREEN;
        }
        else if (cur_player_get_screen_setting() == SCREEN_SIZE_CINEMA)
        {
            return VIEWPORT_HEIGHT_CINEMA;
        }
        else
        {
            return VIEWPORT_HEIGHT_FULLSCREEN;
        }
    }

    if (cur_player_get_screen_setting() == SCREEN_SIZE_WIDESCREEN)
    {
        t = bondviewGetPauseAnimationPercent();
        return (s16) ((s32) (WIDESCREEN_SCALE_FACTOR * t) + VIEWPORT_OFFSET_HEIGHT_WIDESCREEN);
    }
    else if (cur_player_get_screen_setting() == SCREEN_SIZE_CINEMA)
    {
        t = bondviewGetPauseAnimationPercent();
        return (s16) ((s32) (CINEMA_SCALE_FACTOR * t) + VIEWPORT_OFFSET_HEIGHT_CINEMA);
    }
    else
    {
#ifndef TARGET_N64
        if (portFullHeightActive())
        {
            /* 240 NTSC in play (PAL is 272 = already full); the vanilla 220 once
             * the pause box has (nearly) closed - the bars in between are smooth
             * overlays, see portPauseBoxTick; no stepped viewport */
            return (portPauseBoxProgress() >= 0.98f) ? VIEWPORT_HEIGHT_DEFAULT : SCREEN_HEIGHT;
        }
#endif
        return VIEWPORT_HEIGHT_DEFAULT;
    }
}



/**
 * Address 0x7F086E38.
 */
s16 bondviewGetCurrentPlayerViewportUly(void)
{
    f32 t;

    if (getPlayerCount() == 2)
    {
        if (get_cur_playernum() == 0)
        {
#ifdef VERSION_EU
            return 0;
#else
#ifndef TARGET_N64
            if (portFullHeightActive())
            {
                return 0; /* pairs with the height getter's split-screen arm */
            }
#endif
            return VIEWPORT_ULY_2P_PLAYER_1;
#endif
        }

        return VIEWPORT_ULY_2P_PLAYER_2;
    }

    if (getPlayerCount() >= 3)
    {
        if (get_cur_playernum() < 2)
        {
#ifdef VERSION_EU
            return 0;
#else
#ifndef TARGET_N64
            if (portFullHeightActive())
            {
                return 0;
            }
#endif
            return VIEWPORT_ULY_4P_PLAYER_12;
#endif
        }

        return VIEWPORT_ULY_4P_PLAYER_34;
    }

    if (cameraBufferToggle != 0)
    {
        if (cur_player_get_screen_setting() == SCREEN_SIZE_WIDESCREEN)
        {
            return VIEWPORT_ULY_CAM_WIDESCREEN;
        }
        else if (cur_player_get_screen_setting() == SCREEN_SIZE_CINEMA)
        {
            return VIEWPORT_ULY_CAM_CINEMA;
        }
        else
        {
            return VIEWPORT_ULY_CAM_FULLSCREEN;
        }
    }

    if (cur_player_get_screen_setting() == SCREEN_SIZE_WIDESCREEN)
    {
        t = bondviewGetPauseAnimationPercent();
        return (s16) ((s32) (WIDESCREEN_ULY_SCALE_FACTOR * t) + VIEWPORT_ULY_WIDESCREEN_OFFSET);
    }
    else if (cur_player_get_screen_setting() == SCREEN_SIZE_CINEMA)
    {
        t = bondviewGetPauseAnimationPercent();
        return (s16) ((s32) (CINEMA_ULY_SCALE_FACTOR * t) + VIEWPORT_ULY_CINEMA_OFFSET);
    }
    else
    {
#ifdef VERSION_EU
            return 0;
#else
#ifndef TARGET_N64
            if (portFullHeightActive())
            {
                /* 0 in play; the vanilla 10 once the pause box has closed (pairs with the height getter) */
                return (portPauseBoxProgress() >= 0.98f) ? VIEWPORT_ULY_DEFAULT : 0;
            }
#endif
            return VIEWPORT_ULY_DEFAULT;
#endif
    }
}

/**
 * Sets/updates viewport for player.
 * Refreshes autoaim setting.
 * Arguments are passed into MoveBond or bondviewFrozenMoveBond.
 * Checks if necessary to call bossReturnTitleStage.
 * Set player->buttons_pressed to arg2.
 *
 * Address 0x7F086F9C (VERSION_US).
 * Address 0x7F0870BC (VERSION_EU).
 * Address 0x7F087668 (VERSION_JP).
 */
void bondviewMovePlayerUpdateViewport(s8 stick_x, s8 stick_y, u16 buttons)
{
#ifdef VERSION_EU
    f32 faspect;
#endif

    set_cur_player_fovy(FOV_Y_F);

    // This call doesn't do anything, the call viSetFovY(g_CurrentPlayer->fovy); in lvlRender
    // will actually change the field of view.
    // The call above should set g_CurrentPlayer->fovy, but it doesn't seem to affect
    // the fov....
    viSetFovY(FOV_Y_F);

    if (cameraFrameCounter1 != 0)
    {
        if ((cameraFrameCounter1 >= 4) && (resolution != 0) && (viGetFrameBuf2() == (u8*)(cfb_16[1])))
        {
            cameraBufferToggle = 1;
            cameraFrameCounter1 = 0;
        }
        else
        {
            cameraFrameCounter1 += 1;
        }
    }
    else
    {
        if (cameraFrameCounter2 != 0)
        {
            if ((cameraFrameCounter2 >= 4) && (viGetFrameBuf2() == (u8*)(cfb_16[0])))
            {
                cameraBufferToggle = 0;
                cameraFrameCounter2 = 0;
            }
            else
            {
                cameraFrameCounter2 += 1;
            }
        }
    }

    if ((cameraBufferToggle != 0) && (viGetFrameBuf2() == (u8*)(cfb_16[1])))
    {
        viSetFrameBuf2((u8 *) resolution);
    }

#ifdef VERSION_EU
    if (get_screen_ratio() == SCREEN_RATIO_16_9)
    {
        faspect = ((f32) bondviewGetCurrentPlayerViewportWidth() / (f32) bondviewGetCurrentPlayerViewportHeight()) * 0.75f * WIDESCREEN_ASPECT;
    }
    else
    {
        faspect = (f32) bondviewGetCurrentPlayerViewportWidth() / (f32) bondviewGetCurrentPlayerViewportHeight();
    }

    if (cameraBufferToggle == 0)
    {
        faspect *= EU_CAMERA_8003642C_ASPECT;
    }

    set_cur_player_aspect(faspect);
    viSetAspect(faspect);

#else

#ifdef TARGET_N64
    if (get_screen_ratio() == SCREEN_RATIO_16_9)
    {
        set_cur_player_aspect(((f32) bondviewGetCurrentPlayerViewportWidth() / (f32) bondviewGetCurrentPlayerViewportHeight()) * 0.75f * WIDESCREEN_ASPECT);
        viSetAspect(((f32) bondviewGetCurrentPlayerViewportWidth() / (f32) bondviewGetCurrentPlayerViewportHeight()) * 0.75f * WIDESCREEN_ASPECT);
    }
    else
    {
        set_cur_player_aspect((f32) bondviewGetCurrentPlayerViewportWidth() / (f32) bondviewGetCurrentPlayerViewportHeight());
        viSetAspect((f32) bondviewGetCurrentPlayerViewportWidth() / (f32) bondviewGetCurrentPlayerViewportHeight());
    }
#else
    { /* WIDESCREEN: widen the projection aspect only (the coordinate space
       * stays 4:3 and the interpreter expands the 3D to the window); watch
       * frames render wide too, with bondviewRenderWatch widening its
       * perspective by the same factor so the pinned watch 2D still lines up.
       * portSetSceneWide tells the frame pipeline which regime this frame's
       * display list was built for (self-expiring, re-asserted every frame). */
        f32 base;
        f32 wf;

        {   /* PILLARBOX (video.c portSetScenePillar): an MP gameplay frame asks
             * for the centred 4:3 - set before the wide factor, which then reads 1 */
            extern void portSetScenePillar(s32 on);
            portSetScenePillar(getPlayerCount() >= 2);
        }
        wf = portWideFactor();

        base = (f32) bondviewGetCurrentPlayerViewportWidth() / (f32) bondviewGetCurrentPlayerViewportHeight();
        /* The N64 "Ratio 16:9" anamorphic pre-squeeze (base *= 0.75 *
         * WIDESCREEN_ASPECT) is NOT applied on PC: the option is "HUD
         * Centering" here (options.h hudAnchorsToEdges) and the window
         * shape is already handled by wf. */

        set_cur_player_aspect(base * wf);
        viSetAspect(base * wf);
        portSetSceneWide(wf > 1.001f);
        {
            /* MP presentation: the 2D pin and aim helpers compress about the
             * CURRENT viewport's centre (gfx_pc.cpp s_GeViewportCenterNdc,
             * bondview.c), so 2P halves and 3P/4P quadrants all use the normal
             * wide regime without stretching. GE_MPPIN=0 restores the older
             * anamorphic fallback (portMpPinActive). */
            extern void portSetSceneMp(s32 mp);
            extern s32 portMpPinActive(void); /* video.c; weak 0 headless */
            s32 mp = getPlayerCount() >= 3 && !portMpPinActive();
            portSetSceneMp(mp);
            g_bondviewSceneWideFactor = mp ? 1.0f : wf; /* aim pin/unpin helpers follow the real presentation */
        }
    }
#endif
#endif

    set_cur_player_screen_size( bondviewGetCurrentPlayerViewportWidth(), bondviewGetCurrentPlayerViewportHeight());
    set_cur_player_viewport_size( get_curplayer_viewport_ulx(), bondviewGetCurrentPlayerViewportUly());
    viSetXY(getWidth320or440(), getHeight330or240());
    viSetBuf(getWidth320or440(), getHeight330or240());
    viSetViewSize(bondviewGetCurrentPlayerViewportWidth(), bondviewGetCurrentPlayerViewportHeight());
    viSetViewPosition(get_curplayer_viewport_ulx(), bondviewGetCurrentPlayerViewportUly());
    currentPlayerUpdateColourScreenProperties();
    currentPlayerTickChrFade();
    currentPlayerSetYAutoAimEnabled(cur_player_get_autoaim());
    currentPlayerSetXAutoAimEnabled(cur_player_get_autoaim());
    currentPlayerSetLookAheadSetting(cur_player_get_lookahead());
    gunSetGunAmmoVisible(GUNAMMOREASON_OPTION, cur_player_get_ammo_onscreen_setting());

    gunSetSightVisible(
        GUNSIGHTREASON_1,
        (getPlayerCount() == 1 && cur_player_get_sight_onscreen_control())
            || (getPlayerCount() >= 2 && g_playerPerm->sight)
    );

#if defined(VERSION_EU)
    if (1);
#endif

    if ((g_CameraMode == CAMERAMODE_NONE) || ((g_CameraMode == CAMERAMODE_FP) && (is_timer_active != 0)) || (g_CameraMode == CAMERAMODE_FADE_TO_TITLE))
    {
        if (get_cur_playernum() == 0)
        {
            mission_timer += g_ClockTimer;
        }

        MoveBond(stick_x, stick_y, buttons, (u16) g_CurrentPlayer->buttons_pressed);
    }
    else
    {
        bondviewFrozenMoveBond(stick_x, stick_y, buttons, (u16) g_CurrentPlayer->buttons_pressed);
    }

#if defined(BUGFIX_R1)
    watch_time_0 += jpD_800484D0;
#else
    // VERSION_US
    watch_time_0 += speedgraphframes;
#endif

    if (stop_time_flag != 0)
    {
        if ((lvlGetControlsLockedFlag() == 0) && ((buttons & ~(g_CurrentPlayer->buttons_pressed) & (CONT_A | B_BUTTON | Z_TRIG | START_BUTTON | CONT_R | CONT_L))))
        {
            stop_time_flag = 2;

            if (currentPlayerIsFadeComplete())
            {
                if (g_CurrentPlayer->colourscreenfrac == 0.0f)
                {
                    currentPlayerSetFadeColour(0, 0, 0, 0.0f);
                    currentPlayerSetFadeFrac(60.0f, 1.0f);
                }
            }
            else
            {
                if (g_CurrentPlayer->colourfadefracnew == 0.0f)
                {
                    currentPlayerSetFadeFrac(g_CurrentPlayer->colourfadetime60, 1.0f);
                }
            }
        }

        if ((stop_time_flag == 2) && currentPlayerIsFadeComplete() && (g_CurrentPlayer->colourscreenfrac == 1.0f))
        {
            bossReturnTitleStage();
        }
    }

    if (g_CameraAfterCinema)
    {
        bondviewAdvanceCameraMode();
    }

    if (g_CurrentPlayer->bonddead)
    {
        if (g_CurrentPlayer->redbloodfinished == FALSE)
        {
            currentPlayerEquipWeaponWrapper(GUNLEFT, 0);
            currentPlayerEquipWeaponWrapper(GUNRIGHT, 0);

            if (0)
            {
                // removed?
            };
        }

        if (g_CurrentPlayer->redbloodfinished && g_CurrentPlayer->deathanimfinished && (camera_mode >= CAMERAMODE_SWIRL))
        {
            bossRunTitleStage();
        }
    }

    if ((g_CameraMode == CAMERAMODE_FADE_TO_TITLE) && currentPlayerIsFadeComplete())
    {
        bossRunTitleStage();
    }

    g_CurrentPlayer->buttons_pressed = buttons;
}


/**
 * Address 0x7F0875E4.
 */
void bondviewUpdateCurrentRoomPosition(s32 arg0)
{
    getRoomPositionScaledByIndex(arg0, &g_CurrentPlayer->current_model_pos);
    g_CurrentPlayer->current_room_pos.f[0] = g_CurrentPlayer->current_model_pos.f[0] * get_room_data_float1();
    g_CurrentPlayer->current_room_pos.f[1] = g_CurrentPlayer->current_model_pos.f[1] * get_room_data_float1();
    g_CurrentPlayer->current_room_pos.f[2] = g_CurrentPlayer->current_model_pos.f[2] * get_room_data_float1();
    setPlayerRoomField(arg0);
}


void store_BONDdata_curpos_to_previous(void) {
    g_CurrentPlayer->previous_model_pos.f[0] = g_CurrentPlayer->current_model_pos.f[0];
    g_CurrentPlayer->previous_model_pos.f[1] = g_CurrentPlayer->current_model_pos.f[1];
    g_CurrentPlayer->previous_model_pos.f[2] = g_CurrentPlayer->current_model_pos.f[2];
    mtx4RotateVecInPlace(camGetWorldToScreenMtxf(), &g_CurrentPlayer->previous_model_pos);
}


/**
 * Address: 7F0876C4
 */
void bondviewUpdateCameraMatrices(coord3d* cam_pos, coord3d* cam_look_dir, coord3d* cam_up)
{
    Mtx sp108;
    LookAt *lookat;
    Mtxf spC4;
    coord3d clpos;
    coord3d scaledpos;
    f32 scale;
    Mtx *temp_s0;
    Mtxf *projmtx;
    Mtxf sp60;
    s32 j;
    s32 i;

    i = bondviewGetCurrentPlayersRoom();
    bondviewUpdateCurrentRoomPosition(i);

    g_CurrentPlayer->field_5C = dynAllocateMatrix();
    g_CurrentPlayer->field_60 = dynAllocateMatrix();
    g_CurrentPlayer->field_64 = dynAllocateMatrix();
    g_CurrentPlayer->field_68 = dynAllocateMatrix();

    lookat = dynAllocateLights(2);

    scale = D_800364CC;

    scaledpos.x = (cam_pos->x - g_CurrentPlayer->current_model_pos.x) * scale;
    scaledpos.y = (cam_pos->y - g_CurrentPlayer->current_model_pos.y) * scale;
    scaledpos.z = (cam_pos->z - g_CurrentPlayer->current_model_pos.z) * scale;

    clpos.f[0] = scaledpos.f[0] + cam_look_dir->f[0];
    clpos.f[1] = scaledpos.f[1] + cam_look_dir->f[1];
    clpos.f[2] = scaledpos.f[2] + cam_look_dir->f[2];

    matrix_4x4_set_lookat(&spC4,
        scaledpos.x, scaledpos.y, scaledpos.z,
        cam_look_dir->x, cam_look_dir->y, cam_look_dir->z,
        cam_up->x, cam_up->y, cam_up->z);

    guLookAtReflect(&sp108, lookat,
        scaledpos.x, scaledpos.y, scaledpos.z,
        clpos.x, clpos.y, clpos.z,
        cam_up->x, cam_up->y, cam_up->z);

    matrix_4x4_set_lookat((Mtxf*) g_CurrentPlayer->field_64,
        cam_pos->x, cam_pos->y, cam_pos->z,
        cam_look_dir->x, cam_look_dir->y, cam_look_dir->z,
        cam_up->x, cam_up->y, cam_up->z);

    matrix_4x4_set_basis_and_position((Mtxf*) g_CurrentPlayer->field_68,
        cam_pos->x, cam_pos->y, cam_pos->z,
        cam_look_dir->x, cam_look_dir->y, cam_look_dir->z,
        cam_up->x, cam_up->y, cam_up->z);

    temp_s0 = dynAllocateMatrix();

    projmtx = currentPlayerGetProjectionMatrixF();
    matrix_4x4_multiply(projmtx, &spC4, &sp60);

	for (i = 0; i < 4; i++)
    {
		for (j = 0; j < 4; j++)
        {
			if (sp60.m[i][j] > 32000.0f)
            {
				sp60.m[i][j] = 32000.0f;
			}
            else if (sp60.m[i][j] < -32000.0f)
            {
				sp60.m[i][j] = -32000.0f;
			}
		}
	}

    guMtxF2L((f32 (*)[4]) &sp60, temp_s0);
    set_BONDdata_field_10E0((intptr_t) temp_s0);

    scale = bgGetLevelVisibilityScale();

    matrix_scalar_multiply(scale, spC4.m[0]);
    guMtxF2L((f32 (*)[4]) &spC4, (Mtx* ) g_CurrentPlayer->field_5C);
    sub_GAME_7F059334((s32* ) g_CurrentPlayer->field_5C, (s32* ) g_CurrentPlayer->field_60);

    currentPlayerSetMatrix10C8((Mtx* ) g_CurrentPlayer->field_5C);
    currentPlayerSetMatrix10C4((Mtx* ) g_CurrentPlayer->field_60);
    currentPlayerSetMatrix10CC((Mtxf* ) g_CurrentPlayer->field_64);
    currentPlayerSetViewToWorldMtxf((Mtxf* ) g_CurrentPlayer->field_68);

    sub_GAME_7F078464((intptr_t) lookat);
    bondviewUpdateFrustumPlanes();
    store_BONDdata_curpos_to_previous();
}


/**
 * Address: 7F087A08
 */
Gfx *bondviewRenderDebugBondView(Gfx *gdl)
{
    coord3d cam_pos;
    coord3d cam_look;
    coord3d cam_up;
    struct collision434 *collision;
    coord3d shake;
    coord3d vec;
    coord3d zeropos;
    f32 vec_y;
    f32 horizontal_len;
    struct player *player;
    f32 angle;
    f32 vertical_rot;
    f32 ft4;

#if defined(VERSION_EU)
    if (bossGetStageNum() == LEVELID_CUBA)
    {
        if (cur_player_get_screen_setting() == SCREEN_SIZE_CINEMA)
        {
            gdl = clear_framebuffer_black(gdl);
            gdl = clear_framebuffer_black(gdl);
            gdl = clear_framebuffer_black(gdl);
            gdl = clear_framebuffer_black(gdl);
        }
        else if (cur_player_get_screen_setting() == SCREEN_SIZE_WIDESCREEN)
        {
            gdl = clear_framebuffer_black(gdl);
            gdl = clear_framebuffer_black(gdl);
            gdl = clear_framebuffer_black(gdl);
        }
        else
        {
            gdl = clear_framebuffer_black(gdl);
            gdl = clear_framebuffer_black(gdl);
        }
    }
#endif

    if (g_CurrentPlayer->cameramode == 1) {
        cam_pos.x = g_CurrentPlayer->pos.x;
        cam_pos.y = g_CurrentPlayer->pos.y;
        cam_pos.z = g_CurrentPlayer->pos.z;

        cam_look.x = g_CurrentPlayer->pos2.x - g_CurrentPlayer->pos.x;
        cam_look.y = g_CurrentPlayer->pos2.y - g_CurrentPlayer->pos.y;
        cam_look.z = g_CurrentPlayer->pos2.z - g_CurrentPlayer->pos.z;

        cam_up.x = g_CurrentPlayer->offset.x;
        cam_up.y = g_CurrentPlayer->offset.y;
        cam_up.z = g_CurrentPlayer->offset.z;
    } else {
        collision = &g_CurrentPlayer->field_488;

        shake = ZeroCoordShake;

        if (!g_CurrentPlayer->bonddead) {
            explosionScreenShake(
                &collision->pos,
                &collision->applied_view,
                &shake
            );
        } else {
            viShake(0.0f);
        }

        cam_pos.x = collision->pos.x;
        cam_pos.y = collision->pos.y;
        cam_pos.z = collision->pos.z;

        cam_look.x = collision->applied_view.x;
        cam_look.y = collision->applied_view.y;
        cam_look.z = collision->applied_view.z;

        cam_up.x = collision->applied_view2.x;
        cam_up.y = collision->applied_view2.y;
        cam_up.z = collision->applied_view2.z;
    }

    bondviewUpdateCameraMatrices(&cam_pos, &cam_look, &cam_up);
    sub_GAME_7F068190(&zeropos, &vec);


    vec_y = vec.y;
    horizontal_len = sqrtf((vec.z * vec.z) + (vec.x * vec.x));
    vertical_rot = bondviewGetPlayerPitchRadians();
    ft4 = atan2f(vec_y, horizontal_len) + vertical_rot;

    if (ft4 >= M_PI_F) {
        ft4 -= M_TAU_F;
    }
    g_CurrentPlayer->field_2A08 = ft4;

    angle = atan2f(-vec.x, -vec.z);
    if (angle >= M_PI_F) {
        angle -= M_TAU_F;
    }
    g_CurrentPlayer->field_2A0C = angle;

    return gdl;
}


void bondviewSelectCuff(Model *model, ModelFileHeader *header, s32 switchindex)
{
    s32 pad;
    s32 local;
    ModelNode **switches;
    ModelNode **base;
    ModelNode *node;
    s32 *rwdata;
    s32 index;
    s32 visible;
    s32 pad2;

    local = fileGetBondForCurrentFolder();
    switches = header->Switches;

    if (1);

    base = &switches[switchindex];

    if (base[0] != NULL)
    {
        rwdata = (s32 *) modelGetNodeRwData(model, base[0]);
        *rwdata = g_CurrentPlayer->bondtype == CUFF_BOILER;
        switches = header->Switches;
        base = &switches[switchindex];
    }

    index = switchindex + 1;

    if (base[1] != NULL)
    {
        node = switches[index];
        rwdata = (s32 *) modelGetNodeRwData(model, node);
        visible = g_CurrentPlayer->bondtype == CUFF_BROSNAN;
        if (visible == 0)
        {
            visible = g_CurrentPlayer->bondtype == CUFF_DALTON;
            if (visible == 0)
            {
                visible = g_CurrentPlayer->bondtype == CUFF_MOORE;
                if (visible == 0)
                {
                    visible = g_CurrentPlayer->bondtype == CUFF_FOLDER;
                    if (visible != 0)
                    {
                        visible = local != 1;
                    }
                }
            }
        }
        *rwdata = visible;
        switches = header->Switches;
        base = &switches[switchindex];
    }

    index = switchindex + 2;

    if (base[2] != NULL)
    {
        rwdata = (s32 *) modelGetNodeRwData(model, switches[index]);
        visible = g_CurrentPlayer->bondtype == CUFF_CONNERY;

        if (visible == 0)
        {
            visible = g_CurrentPlayer->bondtype == CUFF_FOLDER;
            if (visible != 0)
            {
                visible = local == 1;
            }
        }

        *rwdata = visible;
        switches = header->Switches;
        base = &switches[switchindex];
    }

    index = switchindex + 3;

    if (base[3] != NULL)
    {
        rwdata = (s32 *) modelGetNodeRwData(model, switches[index]);
        *rwdata = g_CurrentPlayer->bondtype == CUFF_BLUE;
        switches = header->Switches;
        base = &switches[switchindex];
    }

    index = switchindex + 4;

    if (base[4])
    {
        rwdata = (s32 *) modelGetNodeRwData(model, switches[index]);
        *rwdata = g_CurrentPlayer->bondtype == CUFF_JUNGLE;
        switches = header->Switches;
        base = &switches[switchindex];
    }

    index = switchindex + 5;

    if (base[5] != NULL)
    {
        rwdata = (s32 *) modelGetNodeRwData(model, switches[index]);
        *rwdata = g_CurrentPlayer->bondtype == CUFF_SNOW;
    }
}


/**
 * Address: 7F087E74
 */
#ifndef TARGET_N64
static s32 g_portWatchDbgArm;   /* GE_WATCHDBG overlay: bondviewRenderWatch drew the arm this frame */
#endif
Gfx *bondviewRenderWatch(Gfx *gdl)
{
    ModelRenderData renderdata;
    Mtxf watchmtx;
    coord3d watchpos;
    f32 t;
    Mtxf *matrices;
    ModelFileHeader *objheader;
    f32 *nodepos;
    union ModelRwData *rwdata;
    Mtx *perspmtx;
    u16 perspNorm;
 
    if (g_CurrentPlayer->watch_animation_state == 0)
    {
        goto end;
    }
 
    if (g_CurrentPlayer->pausing_flag == FALSE)
    {
        goto end;
    }
 
#ifndef TARGET_N64
    /* ARM AT REST: the watch arm's rest pose sits beyond the N64's 4:3 screen
     * at the bottom-left, invisible on N64 but a one-frame sleeve corner in a
     * wide window, so don't render the arm while its animation is at rest. */
    /* The lowering lands exactly on frame 0.0 a tick before the state machine
     * advances, and its exit swing passes outside the 4:3 band, so skip any
     * pose below GE_WATCHREST tenths of a frame (default 50 = frame 5.0). */
    {
        static f32 wr_thresh = -1.0f;
        if (wr_thresh < 0.0f) { const char *e = getenv("GE_WATCHREST"); wr_thresh = (e && *e) ? (f32) atoi(e) / 10.0f : 5.0f; }
        if (getPlayerCount() == 1
            && (g_CurrentPlayer->step_in_view_watch_animation == 0
                || (g_CurrentPlayer->step_in_view_watch_animation != 3 && g_CurrentPlayer->pause_animation_counter < wr_thresh)))
        {
            goto end;
        }
    }
#endif
    g_portWatchDbgArm = 1; /* GE_WATCHDBG: arm submitted this frame */
    renderdata = D_8003683C;
    watchpos = ZeroCoordWatchPos;
    objheader = get_ptr_itemheader_in_hand(GUNLEFT);
    nodepos = (f32 *) objheader->Switches[3];
    rwdata = modelGetNodeRwData(WATCH_MODEL(g_CurrentPlayer), (ModelNode *) nodepos);
    perspmtx = dynAllocateMatrix();
    {
        f32 waspect;
#if defined(VERSION_EU)
        waspect = 1.4005603f;
#else
        waspect = 1.4545455f;
#endif
#ifndef TARGET_N64
        /* WIDESCREEN: watch (pause) frames render wide like gameplay. Widening
         * the authored aspect keeps the watch the same size on screen, so the
         * pinned menu 2D still sits on the watch face. */
        waspect *= g_bondviewSceneWideFactor;
        {   /* Weapon FOV: the arm raises at the gun's FOV, ending on the face as before */
            extern f32 gunWeaponFovY(f32 fovy); /* gunfire.c */
            guPerspective(perspmtx, &perspNorm, gunWeaponFovY(g_CurrentPlayer->zoominfovy), waspect, 10.0f, 300.0f, 1.0f);
        }
#else
        guPerspective(perspmtx, &perspNorm, g_CurrentPlayer->zoominfovy, waspect, 10.0f, 300.0f, 1.0f);
#endif
    }
 
    gSPMatrix(gdl++, OS_PHYSICAL_TO_K0((uintptr_t) perspmtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPPerspNormalize(gdl++, perspNorm);

    // Keep this nested block for matching.
    {
        Mtxf targetmtx;
        coord3d currot;
        coord3d targetrot;
        quatf quat1;
        quatf quat2;
        quatf quat3;
        coord3d targetpos;
        s32 total_seconds;
        s32 seconds;
        s32 total_minutes;
        s32 minutes;
        f32 framesfrac;
        f32 secondsAngle;
        f32 minutesAngle;
        f32 hoursAngle;
        s32 time;
        f32 *nodepos2;
        Mtxf handmtx;
        Mtx *finalmtx;
 
        rwdata->Switch.visible = g_CurrentPlayer->outside_watch_menu;
    
        watchpos.x = (g_CurrentPlayer->field_488.theta_transform.x * (g_CurrentPlayer->headbodyoffset.z + (-12.0f))) + (g_CurrentPlayer->field_488.collision_position.x + (g_CurrentPlayer->headbodyoffset.x * (-g_CurrentPlayer->field_488.theta_transform.z)));
        watchpos.y = g_CurrentPlayer->headbodyoffset.y + g_CurrentPlayer->field_488.collision_position.y;
        watchpos.z = (g_CurrentPlayer->field_488.theta_transform.z * (g_CurrentPlayer->headbodyoffset.z + (-12.0f))) + (g_CurrentPlayer->field_488.collision_position.z + (g_CurrentPlayer->headbodyoffset.x * g_CurrentPlayer->field_488.theta_transform.x));
    
        matrix_4x4_set_position_and_rotation_around_y(watchpos.f, (360.0f - g_CurrentPlayer->vv_theta) * 0.017453292f, &watchmtx);
        matrix_4x4_multiply_homogeneous_in_place(camGetWorldToScreenMtxf(), &watchmtx);
        matrices = dynAllocate(objheader->numMatrices << 6);
        bondviewSelectCuff(WATCH_MODEL(g_CurrentPlayer), objheader, 4);
        renderdata.basemtx = &watchmtx;
        renderdata.mtxlist = matrices;
        subcalcmatrices(&renderdata, WATCH_MODEL(g_CurrentPlayer));
        nodepos = (f32 *) objheader->Switches[0]->Data;
        time = watch_time_0;
        t = WATCH_MODEL(g_CurrentPlayer)->animframe1 / 20.0f;
    
        if (t > 1.0f)
        {
            t = 1.0f;
        }
    
        targetpos.x = matrices->m[3][0] + (((g_CurrentPlayer->field_1D4 - (nodepos[0] * WATCH_MODEL(g_CurrentPlayer)->scale)) - matrices->m[3][0]) * t);
        targetpos.y = matrices->m[3][1] + (((g_CurrentPlayer->field_1D8 + (nodepos[2] * WATCH_MODEL(g_CurrentPlayer)->scale)) - matrices->m[3][1]) * t);
        targetpos.z = matrices->m[3][2] + (((g_CurrentPlayer->pause_watch_position - (nodepos[1] * WATCH_MODEL(g_CurrentPlayer)->scale)) - matrices->m[3][2]) * t);
    
        matrix_4x4_set_basis_and_position_target(&targetmtx, 0.0f, 0.0f, 0.0f, g_CurrentPlayer->field_1E0, g_CurrentPlayer->field_1E4, g_CurrentPlayer->field_1E8, g_CurrentPlayer->field_1EC, g_CurrentPlayer->field_1F0, g_CurrentPlayer->field_1F4);
        matrix_4x4_get_rotation_around_xyz(matrices, &currot);
        matrix_4x4_get_rotation_around_xyz(&targetmtx, &targetrot);
        quaternion_set_rotation_around_xyzf(currot.f, quat1);
        quaternion_set_rotation_around_xyzf(targetrot.f, quat2);
        quaternion_ensure_shortest_path(quat1, quat2);
        quaternion_slerp(quat1, quat2, t, quat3);
        quaternion_to_matrix(quat3, matrices->m);
        matrix_4x4_set_position(&targetpos, matrices);
        matrix_scalar_multiply(WATCH_MODEL(g_CurrentPlayer)->scale, (f32 *) matrices);
        total_seconds = time / 60;
        seconds = total_seconds % 60;
        total_minutes = total_seconds / 60;
        minutes = total_minutes % 60;
        framesfrac = ((f32) (time % 60)) / 60.0f;
        secondsAngle = ((-(((f32) seconds) + framesfrac)) * M_TAU_F) / 60.0f;
        minutesAngle = ((((-((f32) minutes)) * M_TAU_F) / 60.0f) * 1.0f) + (secondsAngle / 60.0f);
        hoursAngle = ((((-((f32) ((total_seconds / 3600) % 12))) * M_TAU_F) / 12.0f) + (minutesAngle / 12.0f)) + (secondsAngle / 720.0f);
    
        while (secondsAngle < 0.0f)
        {
            secondsAngle += M_TAU_F;
        }
    
        while (minutesAngle < 0.0f)
        {
            minutesAngle += M_TAU_F;
        }
    
        while (hoursAngle < 0.0f)
        {
            hoursAngle += M_TAU_F;
        }
    
        matrix_4x4_set_position_and_rotation_around_y((f32 *) objheader->Switches[0]->Data, hoursAngle, &matrices[1]);
        matrix_4x4_multiply_in_place(matrices, &matrices[1]);
        matrix_4x4_set_position_and_rotation_around_y((f32 *) objheader->Switches[1]->Data, minutesAngle, &matrices[2]);
        matrix_4x4_multiply_in_place(matrices, &matrices[2]);
        matrix_4x4_set_position_and_rotation_around_y((f32 *) objheader->Switches[2]->Data, secondsAngle, &matrices[3]);
        matrix_4x4_multiply_in_place(matrices, &matrices[3]);
        
        renderdata.flags = 3;
        renderdata.zbufferenabled = 0;
        renderdata.gdl = gdl;
        renderdata.PropType = PROP_TYPE_WEAPON;
    
        if ((g_CurrentPlayer->watch_animation_state == 5) || (g_CurrentPlayer->watch_animation_state == 12))
        {
            renderdata.envcolour.word = 0xcd;
        }
        else
        {
            renderdata.envcolour.word = g_CurrentPlayer->tileColor.a | (((g_CurrentPlayer->tileColor.r << 24) | (g_CurrentPlayer->tileColor.g << 16)) | (g_CurrentPlayer->tileColor.b << 8));
        }
    
        subdraw(&renderdata, WATCH_MODEL(g_CurrentPlayer));
        gdl = renderdata.gdl;
        nodepos2 = (f32 *) objheader->Switches[2]->Data;
        finalmtx = dynAllocateMatrix();
        matrix_4x4_set_identity_and_position((coord3d *) nodepos2, &handmtx);
        matrix_4x4_multiply_in_place(matrices, &handmtx);
        matrix_4x4_7F058C64();
#ifndef TARGET_N64
        g_portWatchHandMtx = handmtx;              /* portWatchProjectFace */
        g_portWatchFovy = g_CurrentPlayer->zoominfovy;
#if defined(VERSION_EU)
        g_portWatchAspect = 1.4005603f;
#else
        g_portWatchAspect = 1.4545455f;            /* authored 4:3 aspect: hit-test in band space */
#endif
#endif
        matrix_4x4_f32_to_s32(&handmtx, finalmtx);
        matrix_4x4_7F058C88();
        gdl = draw_watch_current_page(gdl, finalmtx, (g_CurrentPlayer->watch_animation_state == 5) || (g_CurrentPlayer->watch_animation_state == 12));
        matrix_4x4_7F058C64();
        bondviewTransformManyPosToViewMatrix(WATCH_MODEL(g_CurrentPlayer)->render_pos, objheader->numMatrices);
        matrix_4x4_7F058C88();
    }
 
    end:
    return gdl;
}


/**
 * Address: 7F088618
 *
 * Renders the in-game health and armor gauges.
 * The watch menu gauges are handled by trigger_solo_watch_menu().
 */
Gfx *bondviewRenderGaugeBars(Gfx *gdl)
{
    Mtx *lookatmtx;
    Mtx *orthomtx;
    Mtxf lookatmtxf;
#ifndef TARGET_N64
    Mtx *orthoRight = NULL; /* HUD Centering 16:9: armour arc's right-edge ortho */
#endif

    //Set up armor bars.
    hudMakeDamageSegments(&g_CurrentPlayer->armor_display_values[0].items[0], 0x2e, 1, g_CurrentPlayer->apparentarmour);
    buildGaugeBarDL((Gfx *)&g_CurrentPlayer->watch_body_armor_bar_gdl, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->armor_display_values[0].items[0]), 0x2e);

    // Set up health bars.
    hudMakeDamageSegments(&g_CurrentPlayer->health_display_values[0].items[0], 0x2e, -1, g_CurrentPlayer->apparenthealth);
    buildGaugeBarDL((Gfx *)&g_CurrentPlayer->watch_health_bar_gdl, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->health_display_values[0].items[0]), 0x2e);

    // Create an orthographic render state for the gauge.
    lookatmtx = dynAllocateMatrix();
    orthomtx = dynAllocateMatrix();

#ifdef TARGET_N64
    guOrtho(orthomtx, -800.0f * D_800364CC, 800.0f * D_800364CC, -600.0f * D_800364CC, 600.0f * D_800364CC, -100.0f, 1000.0f, 1.0f);
#else
    { /* WIDESCREEN: the gauge bars are triangle-based 2D under an ortho, which
       * the texrect pin can't hold, so widen the ortho's x extents by the
       * frame's presentation factor (identity on pinned frames). Under HUD
       * Centering 16:9 the armour arc uses a second ortho flush with the
       * window's right edge and the health arc one flush with the left. */
        extern f32 portFrameWideFactor(void);
        f32 owf = portFrameWideFactor();
        f32 bandfar = 800.0f * D_800364CC * (2.0f * owf - 1.0f);
        f32 gsq = 1.0f;
        s32 split = getPlayerCount() >= 2;

        if (split)
        {   /* SPLIT SCREEN: the arcs were authored round for the solo viewport
             * and 2P halves squash them, so lock the pair's shape to a virtual
             * screen aspect per the option (4:3 -> 16:9, Wide -> 21:9) at the
             * viewport's height. A floor of 1 keeps 4P quadrants and fullscreen
             * netgames at the authored look. */
            f32 lock = portHealthIsWide() ? (21.0f / 9.0f)   /* the Health row (was Rare's ratio) */
                                          : (16.0f / 9.0f);

            gsq = ((f32) bondviewGetCurrentPlayerViewportWidth()
                   / (f32) bondviewGetCurrentPlayerViewportHeight())
                / (320.0f / 220.0f);
            gsq *= (4.0f / 3.0f) / lock;
            if (gsq < 1.0f)
            {
                gsq = 1.0f;
            }
        }
        if (healthAnchorsToEdges() && getPlayerCount() != 2
            && (getPlayerCount() < 3 || portHealthIsWide()))
        {
            /* Arcs flush to the VIEWPORT's edges: solo Wide and, per quadrant,
             * 3P/4P under Wide (4:3 takes the centred branch below). 2P keeps
             * its aspect locks above instead. On the way between the two
             * (options.c portHealthSlide), each extent eases from the centred
             * ortho's to the flush one's. */
            const f32 t = portHealthSlide();
            const f32 cen = 800.0f * D_800364CC * owf;
            const f32 flush = 800.0f * D_800364CC;

            orthoRight = dynAllocateMatrix();
            guOrtho(orthomtx, -cen + (cen - flush) * t, cen + (bandfar - cen) * t, -600.0f * D_800364CC, 600.0f * D_800364CC, -100.0f, 1000.0f, 1.0f);
            guOrtho(orthoRight, -cen + (cen - bandfar) * t, cen + (flush - cen) * t, -600.0f * D_800364CC, 600.0f * D_800364CC, -100.0f, 1000.0f, 1.0f);
        }
        else
        {
            guOrtho(orthomtx, -800.0f * D_800364CC * owf * gsq, 800.0f * D_800364CC * owf * gsq, -600.0f * D_800364CC, 600.0f * D_800364CC, -100.0f, 1000.0f, 1.0f);
        }
    }
#endif

    gSPMatrix(gdl++, osVirtualToPhysical(orthomtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    matrix_4x4_set_lookat_target(
        &lookatmtxf,
        0.0f, 500.0f, 0.0f,
        0.0f, 0.0f,   0.0f,
        0.0f, 0.0f,  -1.0f
    );

    matrix_4x4_f32_to_s32(&lookatmtxf, (Mtxf *)lookatmtx);

    gSPMatrix(gdl++, osVirtualToPhysical(lookatmtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetPrimColor(gdl++, 0, 0, 0xe6, 0xe6, 0xe6, 0x00);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);

#ifndef TARGET_N64
    if (orthoRight != NULL)
    {   /* HUD Centering 16:9: armour under the right-edge ortho, health
         * under the left-edge one (orthomtx, loaded above) */
        gSPMatrix(gdl++, osVirtualToPhysical(orthoRight), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_body_armor_bar_gdl));
        gSPMatrix(gdl++, osVirtualToPhysical(orthomtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_health_bar_gdl));
    }
    else
#endif
    {
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_body_armor_bar_gdl));
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_health_bar_gdl));
    }

    gSPMatrix(gdl++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    return gdl;
}


#ifndef TARGET_N64
#include "mp_weapon.h"

/**
 * SPAWN WITH WEAPON (Multiplayer Options): gives the CURRENT player slot 1's
 * weapon (entry 0 of the match's weapon set) with six magazines of ammo in all,
 * the loaded one included and capped at the type's maximum; a weapon with no
 * magazine counts one (knives, grenades: six).
 *
 * Returns the item, or ITEM_UNARMED when there is nothing to give (option off,
 * solo, a demo, or a set whose slot 1 is no weapon, e.g. Slappers Only).
 */
s32 portMpSpawnWithWeapon(void)
{
    extern s32 portMpSpawnWeapon(void); /* video.c; weak 0 headless */
    extern s32 get_is_ramrom_flag(void);
    extern void give_cur_player_ammo(s32 ammo_type, s32 ammo_amount);
    extern s32 get_max_ammo_for_type(s32 type);
    struct s_mp_weapon_set *set;
    s32 amount;
    s32 max;

    if (getPlayerCount() < 2 || !portMpSpawnWeapon() || get_is_ramrom_flag() != 0)
    {
        return ITEM_UNARMED;
    }
    set = getPtrMPWeaponSetData();
    if (set == NULL || set[0].itemID <= ITEM_FIST)
    {
        return ITEM_UNARMED;
    }
    bondinvAddInvItem(set[0].itemID);
    amount = (s32) get_ptr_item_statistics(set[0].itemID)->MagSize;
    if (amount < 1)
    {
        amount = 1;
    }
    amount *= 6;   /* the reserve: equipping loads the first magazine out of it */
    max = get_max_ammo_for_type(get_ptr_item_statistics(set[0].itemID)->AmmoType);
    if (amount > max)
    {
        amount = max;
    }
    if (amount > 0)
    {
        give_cur_player_ammo(get_ptr_item_statistics(set[0].itemID)->AmmoType, amount);
    }
    return set[0].itemID;
}
#endif

void mp_respawn_handler(void)
{
    coord3d start_pos = ZeroCoordSpawnPos;
    f32 start_look_angle;
    StandTile *start_stan;
    s32 pad;
    f32 stan_height;
    s32 var_v0;
    s32 var_v1;
    u32 var_v0_2;
    struct SetupIntroEmpty* intro_record;

    intro_record = g_CurrentSetup.intro;

    init_player_BONDdata();
    bondviewPlayerBeginLife();

    g_CurrentPlayer->bonddead = 0;
    g_CurrentPlayer->deathanimfinished = 0;
    g_CurrentPlayer->redbloodfinished = 0;
    g_CurrentPlayer->startnewbonddie = 1;
    g_CurrentPlayer->healthdamagetype = 7;
    g_CurrentPlayer->damagetype = 7;
    g_CurrentPlayer->gunammooff = 0;
    g_CurrentPlayer->gunsightmode = 2;

    hudmsgsSetOn(-1);
    bondviewClearUpperTextDisplayFlag(-1);


    if ((getPlayerCount() >= 2) && (startpadcount > 0))
    {
        var_v1 = bondviewGetRandomSpawnPadIndex();
    }
    else
    {
        var_v1 = 0;
    }

    #ifdef DEBUG
    assert(g_Startpad[var_v1]->stan);
    #endif

    start_pos.x = g_Startpad[var_v1]->pos.x;
    start_pos.z = g_Startpad[var_v1]->pos.z;
    start_stan = g_Startpad[var_v1]->stan;

    stan_height = bondviewYPositionRelated(start_stan, start_pos.x, start_pos.z);

    start_pos.y = g_CurrentPlayer->eyeheight + stan_height;
    g_CurrentPlayer->field_70 = stan_height;

    start_look_angle = randomGetNext() * 2.3283064e-10f * 6.2831855f;

    g_CurrentPlayer->vv_theta = (f32) ((start_look_angle * 360.0f) / 6.2831855f);
    g_CurrentPlayer->stanHeight = stan_height;
#if defined(VERSION_EU)
    g_CurrentPlayer->field_6C = (f32) (stan_height / 0.2004f);
#else
    g_CurrentPlayer->field_6C = (f32) (stan_height / 0.17000002f);
#endif

    change_player_pos_to_target(&g_CurrentPlayer->field_488, &start_pos, start_stan);
#ifndef TARGET_N64
    /* That reset the collision radius to 30, undoing MP Tiny Players' halving
     * (cheat.c, applied once per stage) while this player stays tiny, so halve
     * the radius again to match. */
    if ((getPlayerCount() >= 2)
        && (g_playerPerm->player_perspective_height < get_player_mp_char_height(get_cur_playernum())))
    {
        g_CurrentPlayer->field_488.collision_radius /= 2.0f;
    }
#endif

    g_CurrentPlayer->field_488.theta_transform.x = -sinf(start_look_angle);
    g_CurrentPlayer->field_488.theta_transform.y = 0.0f;
    g_CurrentPlayer->field_488.theta_transform.z = cosf(start_look_angle);
    g_CurrentPlayer->prop->pos.x = g_CurrentPlayer->bondprevpos.x = start_pos.f[0];
    g_CurrentPlayer->prop->pos.y = g_CurrentPlayer->bondprevpos.y = start_pos.f[1];
    g_CurrentPlayer->prop->pos.z = g_CurrentPlayer->bondprevpos.z = start_pos.f[2];
    g_CurrentPlayer->prop->stan = start_stan;

#ifndef TARGET_N64
    { /* MP debug: one line per spawn (pad, stan room, position); a bad pad
       * tile shows up here as a wrong or 0 room. */
        extern s32 getTileRoom(StandTile *tile);
        fprintf(stderr,
                "ge: mp spawn p%d pad=%d (%s) stanroom=%d pos=(%.0f %.0f %.0f)\n",
                get_cur_playernum(), var_v1, g_Startpad[var_v1]->plink,
                (start_stan != 0) ? getTileRoom((StandTile *) start_stan) : -1,
                start_pos.f[0], start_pos.f[1], start_pos.f[2]);
        fflush(stderr);
    }
#endif
#if defined(VERSION_EU)
    g_CurrentPlayer->field_3B8.x = (f32) (g_CurrentPlayer->field_488.pos.x / 0.118799984f);
    g_CurrentPlayer->field_3B8.y = (f32) (g_CurrentPlayer->field_488.pos.y / 0.118799984f);
    g_CurrentPlayer->field_3B8.z = (f32) (g_CurrentPlayer->field_488.pos.z / 0.118799984f);
#else
    g_CurrentPlayer->field_3B8.x = (f32) (g_CurrentPlayer->field_488.pos.x / 0.100000024f);
    g_CurrentPlayer->field_3B8.y = (f32) (g_CurrentPlayer->field_488.pos.y / 0.100000024f);
    g_CurrentPlayer->field_3B8.z = (f32) (g_CurrentPlayer->field_488.pos.z / 0.100000024f);
#endif

    bondinvReinitInv();
    var_v0 = 0;

    while (var_v0 != 30)
    {
        g_CurrentPlayer->ammoheldarr[var_v0++] = 0;
    }

    if (intro_record != NULL) 
    {
        while (INTRO_TYPE(intro_record) != 9) // INTROTYPE_END
        {
            switch (INTRO_TYPE(intro_record))
            {
                case 0: // INTROTYPE_SPAWN
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroSpawn));
                    break;
                case 1: // INTROTYPE_ITEM
                    if (check_ramrom_flags() == ((struct SetupIntroAmmo*)intro_record)->is_demo_playback) {
                        if ( ((struct SetupIntroItem*)intro_record)->item_left >= 0) {
                            bondinvAddDoublesInvItem(((struct SetupIntroItem*)intro_record)->item_right, ((struct SetupIntroItem*)intro_record)->item_left);
                        } else {
                            bondinvAddInvItem(((struct SetupIntroItem*)intro_record)->item_right);
                        }
                    }
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroItem));
                    break;
                case 2: // INTROTYPE_AMMO
                    if (check_ramrom_flags() == ((struct SetupIntroAmmo*)intro_record)->is_demo_playback) {
                        give_cur_player_ammo(((struct SetupIntroAmmo*)intro_record)->ammo_type, ((struct SetupIntroAmmo*)intro_record)->ammo_amount);
                    }
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroAmmo));
                    break;
                case 3: // INTROTYPE_SWIRL
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroSwirl));
                    break;
                case 4: // INTROTYPE_ANIM
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroAnim));
                    break;
                case 5: // INTROTYPE_CUFF
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroCuff));
                    break;
                case 6: // INTROTYPE_CAMERA
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroCamera));
                    break;
                default: // INTROTYPE_WATCH, INTROTYPE_CREDITS
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroEmpty));
                    break;
            }
    #ifdef DEBUG
            ossyncprintf("unknown bondstart type %d!\n", var_v0_2);
    #endif
        }
    }

    g_CurrentPlayer->field_78 = 0.0f;
    g_CurrentPlayer->field_7C = -0.0001f;
    g_CurrentPlayer->field_80 = 0.0f;
#ifndef TARGET_N64
    { /* Spawn with Weapon: after Rare's inventory rebuild above, drawn */
        const s32 item = portMpSpawnWithWeapon();

        if (item != ITEM_UNARMED)
        {
            currentPlayerEquipWeaponWrapper(GUNRIGHT, item);
        }
    }
#endif
    currentPlayerStartChrFade(120.0f, 1.0f);
}


/**
 * Address: 7F088CD8
 */
Gfx *bondviewRenderCredits(Gfx *gdl)
{
    s32 frame;
    s32 start;
    s32 x;
    s32 y;
    s32 xpos1;
    s32 xpos2;
    s32 textheight;
    s32 textwidth;
    s32 i;
    s32 end;
    s32 x2;
    s32 entrysize;
    s32 align1;
    s32 align2;
    s16 viewheight;
    char *text;

    if (bossGetStageNum() == LEVELID_CUBA && credits_state == 1 && credits_pointer != NULL)
    {
        xpos1 = 0xdc;
        xpos2 = 0xdc;
        align1 = CREDITS_ALIGN_RIGHT;
        align2 = CREDITS_ALIGN_RIGHT;
#ifdef TARGET_N64
        camera_80036438++;
#else
        /* Cuba credits scroll: vanilla steps 1px per rendered frame, authored
         * for the N64's ~30fps, so at PC frame rates it scrolled far too fast.
         * Advance it from the measured field delta at GE_CREDITSHZ steps/s
         * (default 30; 0 = vanilla), carrying whole steps so no remainder is
         * dropped. */
        {
            static s32 hz = -1;
            static f32 acc = 0.0f;
            if (hz < 0) { const char *e = getenv("GE_CREDITSHZ"); hz = (e && *e) ? atoi(e) : 30; }
            if (hz <= 0)
            {
                camera_80036438++;
            }
            else
            {
                f32 per = 60.0f / (f32) hz; /* fields per scroll step */
                acc += g_GlobalTimerDelta;
                while (acc >= per) { acc -= per; camera_80036438++; }
            }
        }
#endif
        frame = camera_80036438;
        gdl = microcode_constructor(gdl);
        start = (frame - viGetViewHeight()) / 16;
        end = (frame / 16) + 1;

        if (start < 0)
        {
            start = 0;
        }

        for (i = 0; i < start; i++)
        {
            if (credits_pointer[i].TextId1 == 0 && credits_pointer[i].TextId2 == 0)
            {
                end = i;
                start = i;
                credits_state = 2;
                break;
            }

            if (credits_pointer[i].TextId1 != 0x5011)
            {
                if (credits_pointer[i].Position1 >= 0)
                {
                    xpos1 = credits_pointer[i].Position1;
                }

                if ((s16) credits_pointer[i].Alignment1 >= CREDITS_ALIGN_RIGHT)
                {
                    align1 = (s16) credits_pointer[i].Alignment1;
                }
            }

            if (credits_pointer[i].TextId2 != 0x5011)
            {
                if (credits_pointer[i].Position2 >= 0)
                {
                    xpos2 = credits_pointer[i].Position2;
                }

                if ((s16) credits_pointer[i].Alignment2
                    >= CREDITS_ALIGN_RIGHT)
                {
                    align2 = (s16) credits_pointer[i].Alignment2;
                }
            }
        }

        for (i = start; i < end && (credits_pointer[i].TextId1 || credits_pointer[i].TextId2); i++)
        {
            if ((u32) credits_pointer[i].TextId1 != 0x5011)
            {
                text = langGet(credits_pointer[i].TextId1);

                if (credits_pointer[i].Position1 >= 0)
                {
                    xpos1 = credits_pointer[i].Position1;
                }

                if ((s16) credits_pointer[i].Alignment1 >= CREDITS_ALIGN_RIGHT)
                {
                    align1 = (s16) credits_pointer[i].Alignment1;
                }

                viewheight = viGetViewHeight();
                y = ((viGetViewTop() + (i * 16)) - frame) + viewheight;
                textheight = 0;
                textwidth = 0;

                textMeasure(&textheight, &textwidth, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

                entrysize = y + textheight;

                if (align1 == CREDITS_ALIGN_LEFT)
                {
                    x = xpos1 - textwidth;
                    x2 = xpos1;
                }
                else if (align1 == CREDITS_ALIGN_CENTER)
                {
                    x = xpos1 - (textwidth >> 1);
                    x2 = x + textwidth;
                }
                else
                {
                    x = xpos1;
                    x2 = xpos1 + textwidth;
                }

                gdl = microcode_constructor_related_to_menus(gdl, x, y - 1, x2 + 1, entrysize + 1, 0);

                gdl = textRender(gdl, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
            }

            if (credits_pointer[i].TextId2 != 0x5011)
            {
                text = langGet(credits_pointer[i].TextId2);

                if (credits_pointer[i].Position2 >= 0)
                {
                    xpos2 = credits_pointer[i].Position2;
                }

                if ((s16) credits_pointer[i].Alignment2 >= CREDITS_ALIGN_RIGHT)
                {
                    align2 = (s16) credits_pointer[i].Alignment2;
                }

                viewheight = viGetViewHeight();
                y = ((viGetViewTop() + (i * 16)) - frame) + viewheight;
                textheight = 0;
                textwidth = 0;

                textMeasure(&textheight, &textwidth, text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);

                entrysize = y + textheight;

                if (align2 == CREDITS_ALIGN_LEFT)
                {
                    x = xpos2 - textwidth;
                    x2 = xpos2;
                }
                else if (align2 == CREDITS_ALIGN_CENTER)
                {
                    x = xpos2 - (textwidth >> 1);
                    x2 = x + textwidth;
                }
                else
                {
                    x = xpos2;
                    x2 = xpos2 + textwidth;
                }

                gdl = microcode_constructor_related_to_menus(gdl, x, y - 1, x2 + 1, entrysize + 1, 0);

                gdl = textRender(gdl, &x, &y, text, ptrFontZurichBoldChars, ptrFontZurichBold, -1, viGetX(), viGetY(), 0, 0);
            }
        }

#ifndef TARGET_N64
        {   /* Skip hint: "Press Start to Skip" in the bottom right while the
             * credits roll, since START skips the roll and the cast parade
             * (bondviewFrozenCameraTick). */
            /* One right-aligned red line in the small (watch) font, in the
             * letterbox bar under the picture when it fits, else inside the
             * corner. It is drawn last so it sits over the credits, and shows
             * only while a press keeps it up (g_portSkipHintFields). */
            static char *const hint = "Press Start to Skip\n";
            s32 lw = 0;
            s32 lh = 0;
            s32 right = viGetViewLeft() + viGetViewWidth() - 8;
            s32 top;
            s32 a;

            if (g_portSkipHintFields > 0)
            {
                a = (g_portSkipHintFields >= PORT_SKIPHINT_FADE)
                  ? 255 : (g_portSkipHintFields * 255) / PORT_SKIPHINT_FADE;
                textMeasure(&lh, &lw, hint, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                top = viGetViewTop() + viGetViewHeight() + 1;
                if (top + lh > viGetY() - 1)
                {
                    top = viGetViewTop() + viGetViewHeight() - lh - 4;   /* no bar: just inside the corner */
                }
                gdl = microcode_constructor_related_to_menus(gdl, right - lw - 2, top - 2, right + 2, top + lh + 2,
                                                             (u32) ((0xC0 * a) / 255));
                x = right - lw;
                y = top;
                gdl = textRender(gdl, &x, &y, hint, ptrFontBankGothicChars, ptrFontBankGothic,
                                 0xE0000000u | (u32) a, viGetX(), viGetY(), 0, 0);
            }
        }
#endif

        gdl = combiner_bayer_lod_perspective(gdl);
    }

    return gdl;
}


#ifndef TARGET_N64
/* The show_fps counter, drawn LAST in the frame by maybe_mp_interface so the
 * pause pillars can't cover it, and outside sub_GAME_7F08AAE8, whose top-text
 * branch skips its overlays and made the counter blink. */
static Gfx *portDrawFpsCounter(Gfx *gdl)
{
    { /* midas.ini [display] show_fps=1 (GE_SHOWFPS env override): rendered
       * frames per second, drawn in-window for fullscreen use, from gfx_sdl2's
       * 500ms stat window via g_PortStatFps (0 headless). */
        extern s32 portVideoShowFps(void); /* video.c; weak 0 headless */
        if (portVideoShowFps() && (getPlayerCount() == 1 || get_cur_playernum() == 0))
        {
            /* Just the number, in the small Gothic font, two units in from the
             * view's top-left so it stays above the top text box; always
             * anchored to the window's left (TPS is in the log's stats line). */
            static char fpstext[16];
            s32 fp_x = viGetViewLeft() + 2;
            s32 fp_y = viGetViewTop() + 2;

            /* Solo full height keeps the top row while paused, since the view
             * snaps 10 lines down at the end of the pause-box ease; the black
             * bar the digits sit on is drawn before them. */
            if (getPlayerCount() == 1 && portFullHeightActive())
            {
                fp_y = 2;
            }

            sprintf(fpstext, "%d", (int) g_PortStatFps);
            gdl = microcode_constructor(gdl);
            PORT_HUD_ANCHOR_FULL(gdl, PORT_HUD_ANCHOR_LEFT);   /* the FPS strip: always the window edge */
            /* OUTLINED white on 0x646464FF like the HUD's own counters
             * (textRenderOutlined), so the digits stay readable over a bright
             * sky; the one-pixel outline moves them one further in. */
            fp_x += 1;
            fp_y += 1;
            gdl = textRenderOutlined(gdl, &fp_x, &fp_y, fpstext, ptrFontBankGothicChars, ptrFontBankGothic, -1, 0x646464FFU, viGetX(), viGetY(), 0, 0);
            PORT_HUD_ANCHOR_OFF(gdl);
            gdl = combiner_bayer_lod_perspective(gdl);
        }
    }
    return gdl;
}
#endif

#ifndef TARGET_N64
/* The pause box's pillars and bars, and Classic Mode's pillars, over the
 * finished frame. Gameplay draws them last before the FPS strip and the
 * fade; the intro and outro cameras (cameramode 1) draw them too, so
 * Classic Mode keeps its 4:3 picture through the cutscenes. */
static Gfx *portPillarOverlayDraw(Gfx *gdl)
{
    { /* Pause box pillarbox and letterbox for solo play, as fill rects at the
       * true window edges. Drawn last, before the fade, so the pillars cover
       * the edge-anchored Wide HUD. */
        extern f32 portFrameWideFactor(void);
        extern s32 portPausePillars(void); /* video.c: midas.ini [display] pause_pillars; weak 1 headless */
        f32 e = portPauseBoxProgress();
        f32 ep = (portClassicPillarsProgress() > e) ? portClassicPillarsProgress() : e;   /* the pillars: pause box or classic */
        if (ep > 0.0f && getPlayerCount() == 1)
        {
            f32 wf = portFrameWideFactor();
            /* the frame's own size: the Cuba credits draw at 440x330, not 320x240 */
            const s32 sw = viGetX();
            const s32 sh = viGetY();
            const f32 ew = portPausePillars() ? ep : portClassicPillarsProgress();   /* Pause Pillars off: Classic Mode's still draw */
            s32 w4 = (wf > 1.001f) ? (s32) (ew * ((wf - 1.0f) * 0.5f * (f32) sw) * 4.0f + 0.5f) : 0;
            s32 h4 = portFullHeightActive() ? (s32) (e * (f32) VIEWPORT_ULY_DEFAULT * 4.0f + 0.5f) : 0;
            if (w4 > sw * 4) { w4 = sw * 4; } /* wf is clamped at 3 (= 440 native px per side at most); 12-bit 10.2 fields hold it */
            if (w4 >= 4 || h4 >= 4)
            {
#define PORT_FILLRECT_Q(g, ulx4, uly4, lrx4, lry4) do { \
        (g)->words.w0 = ((u32) G_FILLRECT << 24) | (((u32) (lrx4) & 0xFFF) << 12) | ((u32) (lry4) & 0xFFF); \
        (g)->words.w1 = (((u32) (ulx4) & 0xFFF) << 12) | ((u32) (uly4) & 0xFFF); \
        (g)++; } while (0)
                gDPPipeSync(gdl++);
                gDPSetCycleType(gdl++, G_CYC_FILL);
                gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);
                gDPSetFillColor(gdl++, ((GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1)));
                if (w4 >= 4)
                {
                    PORT_HUD_ANCHOR_FULL(gdl, PORT_HUD_ANCHOR_LEFT);
                    PORT_FILLRECT_Q(gdl, 0, 0, w4 - 4, sh * 4 - 1);
                    PORT_HUD_ANCHOR_FULL(gdl, PORT_HUD_ANCHOR_RIGHT);
                    PORT_FILLRECT_Q(gdl, sw * 4 - w4, 0, sw * 4 - 1, sh * 4 - 1);
                    PORT_HUD_ANCHOR_OFF(gdl);
                }
                if (h4 >= 4)
                {
                    PORT_FILLRECT_Q(gdl, 0, 0, sw * 4 - 1, h4 - 4);
                    PORT_FILLRECT_Q(gdl, 0, sh * 4 - h4, sw * 4 - 1, sh * 4 - 1);
                }
#undef PORT_FILLRECT_Q
                gDPPipeSync(gdl++);
                gDPSetCycleType(gdl++, G_CYC_1CYCLE);
            }
        }
    }
    return gdl;
}
#endif

Gfx *maybe_mp_interface(Gfx *gdl)
{
    s32 ulx;
    s32 uly;
    s32 lrx;
    s32 lry;
    s32 doblood;
    s32 scenario;
    s32 cur;
    s32 count;
    s32 i;
    s32 total;

    if (g_CurrentPlayer->cameramode == 1)
    {
        bondviewIntroCameraTextTick();
        gdl = hudmsgBottomRender(gdl);
        bondviewUpperTextWindowTimerTick();
        gdl = sub_GAME_7F08AAE8(gdl);
        gdl = countdownTimerRender(gdl);
#ifndef TARGET_N64
        gdl = portPillarOverlayDraw(gdl);   /* Classic Mode's pillars stay up in the cutscenes */
        gdl = portDrawFpsCounter(gdl);
#endif
        gdl = currentPlayerDrawFade(gdl);
        return bondviewRenderCredits(gdl);
    }

    gunUpdateAndFireBothHands();
    gunRenderCasings(&gdl);
    gunRenderFirstPersonGunModels(&gdl);
    gdl = bondviewRenderWatch(gdl);
    if (g_CurrentPlayer->mpmenuon != 0)
    {
        ulx = viGetViewLeft();
        uly = viGetViewTop();
        lrx = viGetViewLeft() + viGetViewWidth();
        lry = viGetViewTop() + viGetViewHeight();
        gdl = microcode_constructor(gdl);
        gdl = microcode_constructor_related_to_menus(gdl, ulx, uly, lrx, lry, 160);
    }

#ifndef TARGET_N64
    /* GE_HUDGAUGE=1: keep the health/armour arcs on screen (HUD layout /
     * HUD Centering captures - no damage needed). Diagnostic only. */
    {
        static s32 gaugeforce = -1;
        if (gaugeforce < 0)
        {
            extern char *getenv(const char *);
            const char *e = getenv("GE_HUDGAUGE");
            gaugeforce = (e != NULL && e[0] != '0') ? 1 : 0;
        }
        if (gaugeforce && (g_CurrentPlayer->watch_animation_state == 0) &&
            !bondviewGetIfCurrentPlayerHealthShowTime())
        {
            g_CurrentPlayer->apparenthealth = g_CurrentPlayer->bondhealth;
            g_CurrentPlayer->apparentarmour = g_CurrentPlayer->bondarmour;
            gdl = bondviewRenderGaugeBars(gdl);
        }
    }
#endif
    if (bondviewGetIfCurrentPlayerHealthShowTime() &&
        (g_CurrentPlayer->watch_animation_state == 0))
    {
        gdl = bondviewRenderGaugeBars(gdl);
    }
    else if (mpwatchShouldDisplayGauges())
    {
        gdl = bondviewRenderGaugeBars(gdl);
        if (g_CurrentPlayer->healthdisplaytime > 0)
        {
            g_CurrentPlayer->healthdisplaytime -= g_ClockTimer;
        }
        if (g_CurrentPlayer->healthdisplaytime < 0)
        {
            g_CurrentPlayer->healthdisplaytime = 0;
        }
    }

    if (getPlayerCount() == 1)
    {
        display_objective_status_text_on_status_change();
    }

    if (g_CurrentPlayer->bonddead != 0)
    {
        if (g_CurrentPlayer->deathanimfinished == 0)
        {
            doblood = 0;
            if (g_CurrentPlayer->bonddead == 1)
            {
                doblood                   = 1;
                g_CurrentPlayer->bonddead = 2;
            }
            if (doblood)
            {
                die_blood_image_routine(0);
                if (getPlayerCount() == 1)
                {
                    // This unusual comma-expression syntax is required for a byte match.
                    set_missionstate((musicStopSlot(-1), 0));
                    musicTrack1ApplySeqpVol(sub_GAME_7F0C0BF0());
                    g_musicXTrack1Fade = 0;
                    musicTrack2ApplySeqpVol(0);
                    g_musicXTrack2Fade = 0;
                    musicTrack1Play(27);
                }
                else
                {
                    set_missionstate(6);
                }
            }
            else
            {
                if (g_CurrentPlayer->redbloodfinished)
                {
                    currentPlayerSetFadeColour(150, 0, 0, 0.7058824f);
                }
                else
                {
                    if (g_ClockTimer > 0)
                    {
                        doblood = 1;
                    }
                    else
                    {
                        doblood = 2;
                    }
#ifndef TARGET_N64
                    /* PORT: this runs once per rendered frame, so the death blood
                     * animation sped up with the frame rate. portBloodFrameDue
                     * (blood_animation.c) paces it at 30fps of real time, and
                     * held frames re-present the previous image. */
                    {
                        extern s32 portBloodFrameDue(u32 *stamp); /* blood_animation.c */
                        static u32 bloodstamp[4];
                        s32 pnum = get_cur_playernum() & 3;

                        if (doblood == 1 && !portBloodFrameDue(&bloodstamp[pnum]))
                        {
                            gdl = gameplayBloodOverlayDL(gdl);
                            goto blood_frame_held;
                        }
                    }
#endif
                    if (die_blood_image_routine(doblood))
                    {
                        g_CurrentPlayer->redbloodfinished = TRUE;
                    }
                    gdl = gameplayBloodOverlayDL(gdl);
#ifndef TARGET_N64
blood_frame_held:;
#endif
                }
            }
        }
        if (modelGetAnimFrame(&g_CurrentPlayer->model) >=
            modelGetAnimEndFrame(&g_CurrentPlayer->model))
        {
            if (g_CurrentPlayer->redbloodfinished)
            {
                if (!g_CurrentPlayer->deathanimfinished)
                {
                    g_CurrentPlayer->deathanimfinished = TRUE;
                    currentPlayerAdjustFade(60.0f, 0, 0, 0, 1.0f);
                    currentPlayerStartChrFade(120.0f, 0.0f);
                }
                if (currentPlayerIsFadeComplete())
                {
                    if (getPlayerCount() == 1)
                    {
                        bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_SP);
                    }
                    else
                    {
                        scenario = get_scenario();
                        cur      = get_cur_playernum();
                        count    = getPlayerCount();
                        total    = 0;
                        for (i = 0; i < count; i++)
                        {
                            total += g_playerPlayerData[i].kill_counts[cur];
                        }
#ifndef TARGET_N64
                        /* out of lives (Limits screen; YOLT is 2): no respawn */
                        (void) scenario;
                        if ((portMpLives() == 0) || (total < portMpLives()))
#else
                        if ((scenario != SCENARIO_YOLT) || (total < 2))
#endif
                        {
#ifndef TARGET_N64
                            /* port-mode pads respawn on the Use bind too, since
                             * their A is the use bind and never raises N64 A.
                             * The press is taken so it can't also use a door on
                             * respawn. */
                            extern s32 portJoyPortUseMenuPressed(s32 pad);
                            if (joyGetButtons(get_cur_playernum(), 0xB000)
                                || portJoyPortUseMenuPressed(get_cur_playernum()))
#else
                            if (joyGetButtons(get_cur_playernum(), 0xB000))
#endif
                            {
                                mp_respawn_handler();
                            }
                        }
                    }
                }
            }
        }
    }

    bondviewIntroCameraTextTick();
    gdl = hudmsgBottomRender(gdl);
    bondviewUpperTextWindowTimerTick();
    gdl = sub_GAME_7F08AAE8(gdl);
    gunDrawSight(&gdl);
    gdl = generate_ammo_total_microcode(gdl);
    gdl = countdownTimerRender(gdl);
    gdl = display_red_blue_on_radar(gdl);
#ifndef TARGET_N64
    gdl = portPillarOverlayDraw(gdl);
    gdl = portDrawFpsCounter(gdl); /* over the pillars and bars */
#endif
#ifdef GE_VIDEO
    if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x5 || g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0xc)
    {
        /* WATCH MOUSE crosshair, last thing in the frame so the damage gauge
         * and pillars don't cover it */
        extern Gfx *portWatchMouseDraw(Gfx *gdl);
        gdl = portWatchMouseDraw(gdl);
    }
#endif
    return currentPlayerDrawFade(gdl);
}





/**
 * Address 0x7F0896C0.
 */
Gfx *write_stan_tiles_in_yellow(Gfx *gdl)
{
    if (dword_CODE_bss_80079DA0 != NULL)
    {
        gdl = sub_GAME_7F0B3024(gdl, dword_CODE_bss_80079DA0, 0xFF00FF80U);
    }

    if (dword_CODE_bss_80079DA4 != NULL)
    {
        gdl = sub_GAME_7F0B3024(gdl, dword_CODE_bss_80079DA4, 0xFF00FF80U);
    }

    return gdl;
}


void sub_GAME_7F089718(f32 arg0)
{
    f32 scalar;
    struct collision434* col;

    scalar = D_800364D0 / arg0;

    col = &g_CurrentPlayer->field_488;
    col->collision_position.x *= scalar;
    col->collision_position.z *= scalar;

    D_800364D0 = arg0;
    D_800364D4 = 1.0f / arg0;
}


void sub_GAME_7F08976C(f32 param_1)
{
  D_800364CC = param_1;
}


/**
 * Address 0x7F089778.
 */
f32 bondviewGetPlayerStanHeight(struct player *player)
{
    return player->stanHeight;
}


/**
 * Address 0x7F089780.
 */
f32 bondviewGetPlayerDuckingHeightRelated(struct player *player)
{
    return player->eyeheight + player->field_88 + player->ducking_height_offset;
}


PropRecord* getCurrentPlayerProp(void) {
    return g_CurrentPlayer->prop;
}


/**
 * Address 0x7F0897A8.
 */
void bondviewKillCurrentPlayer(void)
{
    if ((g_CurrentPlayer->cheatBondInvincible == 0) && (g_CurrentPlayer->bonddead == FALSE))
    {
        if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0)
        {
            trigger_solo_watch_menu(1);
        }

        g_isBondKIA = 1;
        g_CurrentPlayer->bonddead = 1;

        g_CurrentPlayer->previous_collision_info = g_CurrentPlayer->field_488;

        g_CurrentPlayer->thetadie = g_CurrentPlayer->vv_theta;
        g_CurrentPlayer->vertadie = g_CurrentPlayer->vv_verta;

        if (g_PlayerTankProp != NULL)
        {
            g_ExplodeTankOnDeathFlag = 1;
        }

        currentPlayerEquipWeaponWrapper(GUNLEFT, 0);
        currentPlayerEquipWeaponWrapper(GUNRIGHT, 0);

        if ((getMissiontimer() - g_CurrentPlayer->lifestarttime60) < g_playerPerm->shortest_inning)
        {
            g_playerPerm->shortest_inning = getMissiontimer() - g_CurrentPlayer->lifestarttime60;
        }

        g_CurrentPlayer->lifestarttime60 = getMissiontimer();
    }
}


/**
 * Unreferenced.
 *
 * Address 0x7F0898E8.
 */
s32 sub_GAME_7F0898E8(void)
{
    return (s32) ((joyGetStickY(0) * 8) + 0x280) / 0xA0;
}


#ifndef TARGET_N64
extern s32 portArmorHud(void); /* video.c: midas.ini [display] armor_hud; weak 0 headless */

/* Starts the health / armour arcs as damage does. If they are already up it
 * restarts at frame 1, since frame 0 hides them for a frame. */
static void portHealthShowRestart(void)
{
    if (g_CurrentPlayer->healthshowtime > 0)
    {
        s32 type = (s32)(currentPlayerGetHealth() * 8.0f);

        g_CurrentPlayer->healthdamagetype = (type >= 8) ? 7 : ((type < 0) ? 0 : type);
        g_CurrentPlayer->healthshowtime = 1;
    }
    else
    {
        g_CurrentPlayer->healthshowtime = 0;
    }
}
#endif


/**
 * @param damage_amount: damage amount
 * @param vectorx: damage source x coordinate
 * @param vectorz: damage source y coordinate
 * @param playerid: player index of player causing the damage
 * @param arg4: boolean, does the damage apply to body armor (e.g. false when gas)
 *
 * Address US 7F08991C.
 * Address EU 7F089A84.
 * Address JP 7F089FF0.
 */
void record_damage_kills(f32 damage_amount, f32 vectorx, f32 vectorz, s32 playerid, s32 affects_armor) {
    f32 damage_dealt = g_playerPerm->handicap * damage_amount;
    s32 cur_player_num;
    f32 angle;
    s32 padding;
    s32 sp2C;
    s32 sp28;

#ifndef TARGET_N64
    /* FRIENDLY FIRE off (Gameplay Options): a shot or blast from a player of
     * the same colour does nothing, in any mode - the teams set on Game
     * Limits, or in Flag Tag and the Golden Gun the holder (blue) against the
     * rest (red), as the radar shows them (front.c portMpColourSide).
     * Self-damage still counts, so suicides still score. */
    {
        extern s32 portMpFriendlyFireOff(void);
        extern s32 portMpColourSide(s32 player);
        s32 victim = get_cur_playernum();

        if (portMpFriendlyFireOff() && getPlayerCount() >= 2
            && playerid >= 0 && playerid < MAX_PLAYER_COUNT && playerid != victim
            && portMpColourSide(victim) >= 0 && portMpColourSide(playerid) == portMpColourSide(victim))
        {
            return;
        }
    }
#endif

    if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x0)
    {
        hudMakeDamageSegments(g_CurrentPlayer->armor_display_values, 0x2E, 1, currentPlayerGetArmor());
        hudMakeDamageSegments(g_CurrentPlayer->health_display_values, 0x2E, -1, currentPlayerGetHealth());
    }

    if (getPlayerCount() < 2 || (g_stopPlayFlag == 0 && g_gameOverFlag == 0))
    {
        if (g_PlayerIsInTank == 1)
        {
            damage_dealt *= 0.25f;
        }

        if (g_CurrentPlayer->bonddead == FALSE && g_CurrentPlayer->cheatBondInvincible == FALSE)
        {
            joyRumblePakStart(get_cur_playernum(), 0.25);
#ifndef TARGET_N64
            /* the pad the player's second stick really comes from (a
             * "2.xp2" slot wherever it is plugged in), not always the fixed
             * port; none for a twin stick or past the last slot */
            if (cur_player_get_control_type() >= 4 && portSecondRumbleIndex(get_cur_playernum()) >= 0)
            {
                joyRumblePakStart(portSecondRumbleIndex(get_cur_playernum()), 0.25);
            }
#else
            if (cur_player_get_control_type() >= 4)
            {
                // rumble second controller in 2.x
                joyRumblePakStart(get_cur_playernum() + getPlayerCount(), 0.25);
            }
#endif
        }

#ifndef TARGET_N64
        /* LTK HEALTH (Multiplayer Options): Licence to Kill's one-hit kills
         * in any scenario (video.c portMpLtkHealthActive) */
        extern s32 portMpLtkHealthActive(void);
        if (getPlayerCount() >= 2 && (get_scenario() == SCENARIO_LTK || portMpLtkHealthActive()))
#else
        if (getPlayerCount() >= 2 && get_scenario() == SCENARIO_LTK)
#endif
        {
            // the damage dealt is always equivalent to how much health and armor the player has
            // the result of this is to always kill the player regardless of how much damage he can sustain
            damage_dealt = (g_CurrentPlayer->bondhealth * g_CurrentPlayer->actual_health) + (g_CurrentPlayer->bondarmour * g_CurrentPlayer->actual_armor) + 1.0f;
        }

        if (g_CurrentPlayer->cheatBondInvincible == FALSE && g_CurrentPlayer->bonddead == FALSE && g_PlayerInvincible == FALSE &&
            (g_CurrentPlayer->damageshowtime < 0 || (getPlayerCount() >= 2 && g_CurrentPlayer->damageshowtime == 0)))
        {
            if (g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x5 && g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0xc)
            {
                g_CurrentPlayer->oldhealth = g_CurrentPlayer->bondhealth;
                g_CurrentPlayer->oldarmour = g_CurrentPlayer->bondarmour;

                if (getPlayerCount() >= 2)
                {
                    cur_player_num = get_cur_playernum();
                    angle = g_playerPointers[cur_player_num]->vv_theta - (360.0f - ((atan2f(vectorx, vectorz) * 180.0f) / 3.1415927f));

                    if (angle < 0.0f)
                    {
                        angle = -angle;
                    }

                    if (angle < 90.0f || angle > 270.0f)
                    {
                        // This is an extra condition to avoid a possible future bug with gas in multiplayer
                        // playerid would be -1 in multiplayer
                        if (playerid >= 0 && playerid < 4)
                        {
                            g_playerPlayerData[playerid].damage_to_backside++;
                        }
                    }
                }

                if (affects_armor && damage_dealt <= g_CurrentPlayer->bondarmour * g_CurrentPlayer->actual_armor)
                {
                    g_CurrentPlayer->bondarmour = g_CurrentPlayer->bondarmour - (damage_dealt / g_CurrentPlayer->actual_armor);
#ifndef TARGET_N64
                    if (getPlayerCount() >= 2)
                    {   /* MP stats: the armour that came off (port_mpstats.c) */
                        extern void portMpStatsDamage(s32 victim, s32 attacker, f32 bars);
                        portMpStatsDamage(get_cur_playernum(), playerid, g_CurrentPlayer->oldarmour - g_CurrentPlayer->bondarmour);
                    }
#endif
                }
                else
                {
                    if (affects_armor)
                    {
                        damage_dealt -= g_CurrentPlayer->bondarmour / g_CurrentPlayer->actual_armor;
                        g_CurrentPlayer->bondarmour = 0.0f;
                        g_CurrentPlayer->actual_armor = 1.0f;
                    }

                    g_CurrentPlayer->bondhealth = g_CurrentPlayer->bondhealth - (damage_dealt / g_CurrentPlayer->actual_health);
#ifndef TARGET_N64
                    if (getPlayerCount() >= 2)
                    {   /* MP stats: what came off - the armour that was left plus the health,
                         * never past empty (overkill on the last hit is not damage) */
                        extern void portMpStatsDamage(s32 victim, s32 attacker, f32 bars);
                        const f32 hl = g_CurrentPlayer->oldhealth - ((g_CurrentPlayer->bondhealth > 0.0f) ? g_CurrentPlayer->bondhealth : 0.0f);
                        portMpStatsDamage(get_cur_playernum(), playerid, (g_CurrentPlayer->oldarmour - g_CurrentPlayer->bondarmour) + hl);
                    }
#endif

                    if (g_CurrentPlayer->bondhealth <= 0.0f)
                    {
                        if (getPlayerCount() >= 2)
                        {
                            sp2C = get_cur_playernum();
                            sp28 = 0;

                            if (get_scenario() == 3 && bondinvHasGoldenGun())
                            {
                                sp28 = 1;
                            }

                            // Ensure inventory is dropped on suicides. For this reason, it's not part of the next "if".
                            drop_inventory();

                            if (sp2C != playerid)
                            {
                                increment_num_deaths();
                            }

                            set_cur_player(playerid);

                            if (sp2C == playerid)
                            {
                                increment_num_suicides_display_MP();
                            }
                            else
                            {
                                increment_num_kills_display_text_in_MP();

                                if (sp28 != 0)
                                {
                                    increment_num_times_killed_MwtGC();
                                }
                            }

                            set_cur_player(sp2C);

                            if(1);

                            g_playerPlayerData[playerid].kill_counts[sp2C]++;
                        }

                        bondviewKillCurrentPlayer();
                    }
                }

#if defined(VERSION_EU) || defined(VERSION_JP)
    #define ZERO_7F08991C 0.0f
#else
    #define ZERO_7F08991C 0
#endif
                if (g_CurrentPlayer->damageshowtime < ZERO_7F08991C)
                {
                    g_CurrentPlayer->bondshotspeed.x = g_CurrentPlayer->bondshotspeed.x + 2.0f * vectorx;
                    g_CurrentPlayer->bondshotspeed.z = g_CurrentPlayer->bondshotspeed.z + 2.0f * vectorz;
                }

                g_CurrentPlayer->damageshowtime = ZERO_7F08991C;
#ifndef TARGET_N64
                if (portArmorHud())
                {   /* Armor Pickup HUD: a hit during a pickup's display must not blink it */
                    portHealthShowRestart();
                }
                else
#endif
                g_CurrentPlayer->healthshowtime = ZERO_7F08991C;

#undef ZERO_7F08991C

#if defined(VERSION_EU) || defined(VERSION_JP)
                if (!lvlGetControlsLockedFlag())
                {
                    sndPlaySfx(g_musicSfxBufferPtr, BOND_GET_HIT1_SFX, 0);
                }
#else
                sndPlaySfx(g_musicSfxBufferPtr, BOND_GET_HIT1_SFX, 0);
#endif
            }
        }
    }
}


/**
 * @param damage_amount: damage amount
 * @param rad:  damage source angle
 * @param player_id: player index of player causing the damage
 * @param affects_armor: boolean, does the damage apply to body armor (e.g. false when gas)
 *
 * Address 0x7F089E4C.
 */
void bondviewCallRecordDamageKills(f32 damage_amount, f32 angle, s32 playerid, s32 affects_armor)
{
    record_damage_kills(damage_amount, sinf(angle), cosf(angle), playerid, affects_armor);
}


int bondviewGetIfCurrentPlayerDamageShowTime(void)
{
    return (g_CurrentPlayer->damageshowtime >= (s32)0);
}


int bondviewGetIfCurrentPlayerHealthShowTime(void)
{
    return (g_CurrentPlayer->healthshowtime > (s32)0);
}


f32 bondviewGetBondBreathing(void)
{
    return g_CurrentPlayer->bondbreathing;
}


/**
 * Gets the current player's heading angle, converted from degrees to radians.
 * @return Heading (Yaw) in Radians
*/
f32 bondviewGetPlayerYawRadians(void)
{
    return DegToRad(360.0f - g_CurrentPlayer->vv_theta);
}


/**
 * Gets the current player's vertical look angle, converted from degrees to radians.
 * @return Pitch in radians.
 */
f32 bondviewGetPlayerPitchRadians(void)
{
    return DegToRad(g_CurrentPlayer->vv_verta);
}


s32 bond_pressed_reload_activate(void) {
    return g_CurrentPlayer->field_D0;
}


void set_bondata_invincible_flag(u32 arg0) {
    g_CurrentPlayer->cheatBondInvincible = arg0;
}


u8 get_bondata_invincible_flag(void) {
    return g_CurrentPlayer->cheatBondInvincible;
}


/**
 * Sets g_VisibleToGuardsFlag.
 * 1 = visible, 0 = not visible.
 */
void bondviewSetVisibleToGuardsFlag(s32 param_1)
{
  g_VisibleToGuardsFlag = param_1;
}

/**
 * Gets g_VisibleToGuardsFlag.
 * 1 = visible, 0 = not visible
 */
s32 bondviewGetVisibleToGuardsFlag(void)
{
    return g_VisibleToGuardsFlag;
}

void set_obj_collision_flag(s32 flag) {
  obj_collision_flag = flag;
}

s32 get_obj_collision_flag(void) {
    return obj_collision_flag;
}






/**
 * Address 0x7F089F98.
 */
u8 bondviewGetCurrentPlayersRoom(void)
{
    if ((g_CurrentPlayer->cameramode == 1) && (g_CurrentPlayer->cameratile != 0))
    {
        return g_CurrentPlayer->cameratile->room;
    }

    return g_CurrentPlayer->field_488.current_tile_ptr_for_portals->room;
}




/**
 * Address 0x7F089FD4.
 */
coord3d *bondviewGetCurrentPlayersPosition(void)
{
    if (g_CurrentPlayer->cameramode == 1)
    {
        return &g_CurrentPlayer->pos;
    }

    return &g_CurrentPlayer->field_488.pos;
}



coord3d * bondviewGetCurrentPlayersPosition3(void)
{

    if (g_CurrentPlayer->cameramode == 1)
    {
        return &g_CurrentPlayer->pos3;
    }

    return &g_CurrentPlayer->field_488.pos3;
}

struct coord3d *getCurrentPlayerPrevPos(void)
{
    return &g_CurrentPlayer->bondprevpos;
}


/**
 * Address 0x7F08A03C.
 */
void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 flag)
{
    s32 playerIndex;

    playerIndex = getPlayerPointerIndex(prop);

    if (prop->chr != NULL)
    {
        chrSetMoving(prop->chr, flag);
    }

    if (g_PlayerTankProp != NULL)
    {
        // When commented out tank shells fired from the tank detonate immediately.
        sub_GAME_7F04F218(g_PlayerTankProp, flag);
    }

    g_playerPointers[playerIndex]->field_AC = flag;
}





/**
 * Address 0x7F08A0B0.
 */
void bondviewGetPropHeightRelatedValues(PropRecord *arg0, struct rect4f **field_B0, s32 *arg2, f32 *height_related, f32 *collision)
{
    s32 temp_v0;

    temp_v0 = getPlayerPointerIndex(arg0);
    if (g_playerPointers[temp_v0]->field_AC != 0)
    {
        // What is this doing and why is it 1 player only?
        if (getPlayerCount() == 1 || g_playerPointers[temp_v0]->bonddead == FALSE)
        {
            if (g_playerPointers[temp_v0]->cameramode != 1)
            {
                *arg2 = 4;
                *field_B0 = &g_playerPointers[temp_v0]->collision_bounds;
                *collision = g_playerPointers[temp_v0]->field_70;
                *height_related = *collision + bondviewGetPlayerDuckingHeightRelated(g_playerPointers[temp_v0]) + 10.0f;

                return;
            }
        }
    }

    *arg2 = 0;
}




/**
 * Address 0x7F08A19C.
 */
void bondviewUpdatePlayerCollisionBounds(void)
{

    if (g_PlayerIsInTank == 1)
    {
        bondviewGetTankCollisionBounds(&g_CurrentPlayer->collision_bounds, &g_CurrentPlayer->field_488.collision_position, g_TankOrientationAngle);

        return;
    }

    g_CurrentPlayer->collision_bounds.f[0] = (g_CurrentPlayer->field_488.collision_position.f[0] + g_CurrentPlayer->field_488.collision_radius);
    g_CurrentPlayer->collision_bounds.f[1] = g_CurrentPlayer->field_488.collision_position.f[2];
    g_CurrentPlayer->collision_bounds.f[2] = g_CurrentPlayer->field_488.collision_position.f[0];
    g_CurrentPlayer->collision_bounds.f[3] = (g_CurrentPlayer->field_488.collision_position.f[2] + g_CurrentPlayer->field_488.collision_radius);
    g_CurrentPlayer->collision_bounds.f[4] = (g_CurrentPlayer->field_488.collision_position.f[0] - g_CurrentPlayer->field_488.collision_radius);
    g_CurrentPlayer->collision_bounds.f[5] = g_CurrentPlayer->field_488.collision_position.f[2];
    g_CurrentPlayer->collision_bounds.f[6] = g_CurrentPlayer->field_488.collision_position.f[0];
    g_CurrentPlayer->collision_bounds.f[7] = (g_CurrentPlayer->field_488.collision_position.f[2] - g_CurrentPlayer->field_488.collision_radius);
}





/**
 * @param arg0: prop
 * @param width: out parameter, will be set to field_488.collision_radius
 * @param height: out parameter, will be set to character height - 30
 * @param always_30: out parameter, will be set to 30
 *
 * Address 0x7F08A274.
 */
void bondviewGetCollisionRadius(PropRecord* arg0, f32 *collision_radius, f32 *height, f32 *always_30)
{
    struct player **temp_v1;

    temp_v1 = &g_playerPointers[getPlayerPointerIndex(arg0)];
    *collision_radius = (*temp_v1)->field_488.collision_radius;
    *height = (bondviewGetPlayerDuckingHeightRelated(*temp_v1) + 10.0f) - 30.0f;
    *always_30 = 30.0f;
}





/**
 * Address 0x7F08A2EC.
 */
f32 currentPlayerGetHealth(void)
{
    return g_CurrentPlayer->bondhealth;
}


f32 currentPlayerGetArmor(void)
{
  return g_CurrentPlayer->bondarmour;
}





/**
 * Address 0x7F08A30C.
 */
void bondviewAddCurrentPlayerArmor(f32 arg0)
{
    g_playerPerm->body_armor_pickups += arg0;
    g_CurrentPlayer->bondarmour = arg0;
#ifndef TARGET_N64
    /* Armor Pickup HUD (XBLA): show this player's arcs with the new armor at
     * once. A hit's display still on its old values shows the new ones itself. */
    if (portArmorHud() && !g_CurrentPlayer->bonddead &&
        !(g_CurrentPlayer->healthshowtime >= 0 &&
          g_CurrentPlayer->healthshowtime <= g_HealthDisplayDurations[g_CurrentPlayer->healthdamagetype].updateToRealHealthFrame))
    {
        s32 type = (s32)(currentPlayerGetHealth() * 8.0f);

        g_CurrentPlayer->oldhealth = g_CurrentPlayer->apparenthealth = g_CurrentPlayer->bondhealth;
        g_CurrentPlayer->oldarmour = g_CurrentPlayer->apparentarmour = g_CurrentPlayer->bondarmour;
        /* Start at frame 1 with the shown values set: the pickup runs after
         * the health tick and before the arcs draw (lv.c lvlRender), so frame
         * 0, which draws nothing, would leave the pickup frame blank. */
        g_CurrentPlayer->healthdamagetype = (type >= 8) ? 7 : ((type < 0) ? 0 : type);
        g_CurrentPlayer->healthshowtime = 1;
    }
#endif
}





/**
 * Address 0x7F08A330.
 */
void bondviewResetIntroCameraMessageDialogs(void)
{
    g_CurrentPlayer->hudmessoff = FALSE;
    g_CurrentPlayer->bondmesscnt = -1;
    display_statusbar = 0;
    status_bar_text_buffer_index = 0;

#ifdef BUGFIX_R0
    copy_1stfonttable = ptrFontBankGothic;
    copy_2ndfonttable = ptrFontBankGothicChars;
#endif
}



void hudmsgsSetOn(s32 flag)
{
    g_CurrentPlayer->hudmessoff &= ~flag;
}





/**
 * Address 0x7F08A39C.
 */
void hudmsgsSetOff(s32 flags)
{
    g_CurrentPlayer->hudmessoff |= flags;
}


#ifdef VERSION_US
void setFontTables(struct fontchar *arg0, struct font *arg1)
{
    copy_2ndfonttable = arg0;
    copy_1stfonttable = arg1;
}
#endif


#ifdef BUGFIX_R1
void hudmsgBottomShow(char *string, s32 font, s32 arg2)
{
    s32 abs_index;
    s32 index;
    if (getPlayerCount() == 1)
    {
        if (display_statusbar < 5)
        {
            abs_index = status_bar_text_buffer_index + display_statusbar;
            index = abs_index % 5;
            abs_index = index;
            strncpy(stringbuffer_lowerleft[abs_index], string, (BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH-1));
            stringbuffer_lowerleft[abs_index][(BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH-1)] = 0;
            dword_CODE_bss_jp80079CEC[abs_index] = font;
            dword_CODE_bss_jp80079Cd8[abs_index] = arg2;
            display_statusbar++;
        }
    }
    else
    {
        index = get_cur_playernum();
        strncpy(stringbuffer_lowerleft[index], string, (BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH-1));
        stringbuffer_lowerleft[index][(BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH-1)] = 0;
        dword_CODE_bss_jp80079CEC[index] = font;
        dword_CODE_bss_jp80079Cd8[index] = arg2;
#if defined(VERSION_EU)
        g_CurrentPlayer->bondmesscnt = 0x64;
#elif defined(VERSION_JP)
        g_CurrentPlayer->bondmesscnt = 0x78;
#endif
    }
}

#else
#ifndef TARGET_N64
/* 1 while lv.c switches Multiplayer Options' cheats on at a match start, so
 * their "PAINTBALL MODE ON" etc. popups don't greet every player */
s32 g_PortHudMsgQuiet;
#endif
#ifdef DEBUG
void hudmsgBottomShow(char *mess, void *font)
#else
void hudmsgBottomShow(char *mess)
#endif
{
    s32 abs_index;
    s32 index;
    #ifdef DEBUG
        assert(font);
        assert(strlen(mess)<=MAXMESSAGELEN);
    #endif
#ifndef TARGET_N64
    if (g_PortHudMsgQuiet)
    {
        return;
    }
#endif
    if (getPlayerCount() == 1)
    {
        if (display_statusbar < 5)
        {
            abs_index = status_bar_text_buffer_index + display_statusbar;
            index = abs_index % 5;
            strncpy(stringbuffer_lowerleft[index], mess, MAXMESSAGELEN);
            display_statusbar++;
            stringbuffer_lowerleft[index][MAXMESSAGELEN] = 0;
        }
    }
    else
    {
        index = get_cur_playernum();
        strncpy(stringbuffer_lowerleft[index], mess, MAXMESSAGELEN);
        stringbuffer_lowerleft[index][MAXMESSAGELEN] = 0;
        g_CurrentPlayer->bondmesscnt = 0x78;
    }
}

#endif


#if defined(BUGFIX_R1)
void jp_hudmsgBottomShow(char *string)
{
    hudmsgBottomShow(string, ptrFontBankGothicChars, ptrFontBankGothic);
}
#endif


/**
 * Address 0x7F08A4E4.
 */
void bondviewIntroCameraTextTick(void)
{
    if ((g_CurrentPlayer->hudmessoff == FALSE) && (g_CurrentPlayer->mpmenuon == FALSE))
    {
        if (g_CurrentPlayer->bondmesscnt >= 0)
        {
            g_CurrentPlayer->bondmesscnt -= g_ClockTimer;

            if (getPlayerCount() == 1)
            {
                if (g_CurrentPlayer->bondmesscnt < 0)
                {
                    status_bar_text_buffer_index = (s32) (status_bar_text_buffer_index + 1) % 5;
                    display_statusbar = display_statusbar - 1;
                }
                else if ((display_statusbar >= 2) && (g_CurrentPlayer->bondmesscnt >= BONDVIEW_INTRO_CAMERA_BONDMESSCNT_A))
                {
                    g_CurrentPlayer->bondmesscnt = BONDVIEW_INTRO_CAMERA_BONDMESSCNT_B;
                }
            }
        }

        if ((getPlayerCount() == 1) && (g_CurrentPlayer->bondmesscnt < 0) && (display_statusbar > 0))
        {
            if (display_statusbar >= 2)
            {
                g_CurrentPlayer->bondmesscnt = BONDVIEW_INTRO_CAMERA_BONDMESSCNT_B;
            }
            else
            {
                g_CurrentPlayer->bondmesscnt = BONDVIEW_INTRO_CAMERA_BONDMESSCNT_C;
            }
        }
    }
}


/**
 * Address: 7F08A5FC
 */
Gfx* hudmsgBottomRender(Gfx* arg0)
{
    s32 var_v1;
    s32 view_left;
    s32 view_vert;
    s32 view_horiz;
    s32 view_top;
    s32 view_top_offset;
    s32 view_left_offset;

    if ((g_CurrentPlayer->hudmessoff == FALSE) && (g_CurrentPlayer->bondmesscnt >= 0) && (g_CurrentPlayer->mpmenuon == FALSE))
    {
        var_v1 = 0;
        if (getPlayerCount() == 1)
        {
            if ((u8) *stringbuffer_lowerleft[status_bar_text_buffer_index] != 0)
            {
                var_v1 = 1;
            }
        }
        else if (g_CurrentPlayer->bondmesscnt >= 0)
        {
            status_bar_text_buffer_index = get_cur_playernum();
            var_v1 = 1;
        }

        if (var_v1 != 0)
        {
            arg0 = microcode_constructor(arg0);
            view_left_offset = 0;
            view_top_offset = 0;
            textMeasure(&view_top_offset, &view_left_offset ,(u8* ) stringbuffer_lowerleft[status_bar_text_buffer_index], BONDVIEW_2ND_FONTTABLE(status_bar_text_buffer_index), BONDVIEW_1ST_FONTTABLE(status_bar_text_buffer_index), 0);

            if (getPlayerCount() < 3)
            {
                view_left = viGetViewLeft() + 0x1E;
            }
#ifndef TARGET_N64
            else
            {   /* the bottom-left message: every quadrant at the right column's
                 * +0xA (gunfire.c, the ammo's "same layout in every quadrant") */
                view_left = viGetViewLeft() + 0xA;
            }
#else
            else if (get_cur_playernum() & 1)
            {
                view_left = viGetViewLeft() + 0xA;
            }
            else
            {
                view_left = viGetViewLeft() + 0x1E;
            }
#endif

            view_horiz = view_left + view_left_offset;

            if (getPlayerCount() < 3)
            {
                if ((get_ammo_type_for_weapon(getCurrentPlayerWeaponId(GUNLEFT)) == 0) && (is_clock_drawn_onscreen() == 0)
#ifndef TARGET_N64
                    /* the gadget name in the left-hand ammo slot (gunfire.c
                     * generate_ammo_total_microcode) counts as that counter */
                    && !portGadgetHudNameShown()
                    && !portMissionTimerShown()
#endif
                    )
                {
                    view_top = (viGetViewTop() + viGetViewHeight()) - BONDVIEW_VIEW_TOP_OFFSET_1;
                }
                else
                {
                    view_top = (viGetViewTop() + viGetViewHeight()) - BONDVIEW_VIEW_TOP_OFFSET_2;
                }
#if !defined(VERSION_EU)
                if (get_cur_playernum() == 1)
                {
                    view_top -= 8;
                }
#endif
            }
            else
            {
                view_top = viGetViewTop()
#if defined(VERSION_JP) || defined(VERSION_EU)
                         + (((j_text_trigger != 0) && (get_cur_playernum() < 2)) ? 8 : 0)
#endif
                         + BONDVIEW_VIEW_TOP_OFFSET_3;
            }

            view_vert = view_top - view_top_offset;
#ifndef TARGET_N64
            /* HUD Centering 16:9: the bottom-left message hugs the window's left
             * edge; in 3P / 4P only under HUD Layout = Wide (options.c
             * hudLayoutWide), otherwise it stays in the quadrant's 4:3 band. */
            const s32 portMsgAnchor = hudLayoutWide();

            if (portMsgAnchor)
            {
                PORT_HUD_ANCHOR(arg0, PORT_HUD_ANCHOR_LEFT);
            }
            /* HUD Scale: the message is laid out from the view's bottom-left */
            arg0 = portHudScaleBegin(arg0, (f32) viGetViewLeft(), (f32) (viGetViewTop() + viGetViewHeight()));
#endif
            arg0 = draw_blackbox_to_screen(arg0, (intptr_t) &view_left, (intptr_t) &view_vert, (intptr_t) &view_horiz, (intptr_t) &view_top);
            arg0 = combiner_bayer_lod_perspective(textRenderOutlined(arg0, &view_left, &view_vert, stringbuffer_lowerleft[status_bar_text_buffer_index], BONDVIEW_2ND_FONTTABLE(status_bar_text_buffer_index), BONDVIEW_1ST_FONTTABLE(status_bar_text_buffer_index), -1, 0x646464FFU, (s16) (s32) viGetX(), (s16) viGetY(), 0, 0));
#ifndef TARGET_N64
            arg0 = portHudScaleEnd(arg0);
            if (portMsgAnchor)
            {
                PORT_HUD_ANCHOR_OFF(arg0);
            }
#endif
        }
    }

    return arg0;
}


void bondviewResetUpperTextDisplay(void)
{
    upper_text_window_timer = -1;
    display_upper_text_window = 0;
    upper_text_buffer_index = 0;
    g_UpperTextDisplayFlag = 0;
}


void bondviewClearUpperTextDisplayFlag(int param_1)
{
  int new_var;
  new_var = ~param_1;
  g_UpperTextDisplayFlag = g_UpperTextDisplayFlag & new_var;
}


void bondviewSetUpperTextDisplayFlag(PLAYERFLAG flag)
{
    g_UpperTextDisplayFlag |= flag;
}


void hudmsgTopShow(char* mess)
{
    s32 index;
    #ifdef DEBUG
        assert(strlen(mess)<=MAXTALKMESSLEN);
    #endif
    if (display_upper_text_window >= 2) { return; }

    index = (upper_text_buffer_index + display_upper_text_window) % 2;
#if defined(LEFTOVERDEBUG)
    strncpy(stringbuffer_top[index], mess, (BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH-1));
    display_upper_text_window += 1;
    stringbuffer_top[index][(BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH-1)] = 0;
#else
    strncpy(dword_CODE_bss_80079DC8[index], mess, (BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH-1));
    display_upper_text_window += 1;
    dword_CODE_bss_80079DC8[index][(BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH-1)] = 0;
#endif
}


/**
 * Address 0x7F08A9F8.
 */
void bondviewUpperTextWindowTimerTick(void)
{
    if ((g_UpperTextDisplayFlag == FALSE) && (g_CurrentPlayer->mpmenuon == FALSE))
    {
        if (upper_text_window_timer >= 0)
        {
            upper_text_window_timer -= g_ClockTimer;

            if (upper_text_window_timer < 0)
            {
                upper_text_buffer_index = (s32) (upper_text_buffer_index + 1) % 2;
                display_upper_text_window += -1;
            }
            else if ((display_upper_text_window >= 2) && (upper_text_window_timer >= BONDVIEW_UPPER_TEXT_TIMER_A))
            {
                upper_text_window_timer = BONDVIEW_UPPER_TEXT_TIMER_B;
            }
        }

        if ((upper_text_window_timer < 0) && (display_upper_text_window > 0))
        {
            if (display_upper_text_window >= 2)
            {
                upper_text_window_timer = BONDVIEW_UPPER_TEXT_TIMER_B;
            }
            else
            {
                upper_text_window_timer = BONDVIEW_UPPER_TEXT_TIMER_C;
            }
        }
    }

}


Gfx *sub_GAME_7F08AAE8(Gfx *gdl)
{
    TopMessageLocals msg;
    DebugTextBuffers debugtext;
    s32 debug_x;
    s32 debug_y;
    f32 theta_x;
    s32 debug_boxbottom;
    s32 pad;
    s32 *roomid;
    s32 debug_angle;
    DirectionLabels directions;

    struct
    {
        s16 screenwidth;
        s16 pad;
    } sw;

    if (g_UpperTextDisplayFlag == 0)
    {
        if (upper_text_window_timer >= 0)
        {
#if defined(LEFTOVERDEBUG)
            if (stringbuffer_top[upper_text_buffer_index][0] != '\0')
#else
            if (dword_CODE_bss_80079DC8[upper_text_buffer_index][0] != '\0')
#endif
            {
                if (g_CurrentPlayer->mpmenuon == 0)
                {
                    gdl = microcode_constructor(gdl);
                    msg.textwidth = 0;
                    msg.textheight = 0;
#if defined(LEFTOVERDEBUG)
                    textMeasure(&msg.textheight, &msg.textwidth, stringbuffer_top[upper_text_buffer_index], ptrFontZurichBoldChars, ptrFontZurichBold, 0);
#else
                    textMeasure(&msg.textheight, &msg.textwidth, dword_CODE_bss_80079DC8[upper_text_buffer_index], ptrFontZurichBoldChars, ptrFontZurichBold, 0);
#endif
                    if (cameraBufferToggle != 0)
                    {
                        msg.x = viGetViewLeft() + 0x46;
                        msg.y = viGetViewTop() + 0x10;
                        msg.y += 0x10;
                        msg.y = msg.y / 11;
                        msg.y *= 11;
                        msg.y -= 2;
                    }
                    else
                    {
                        msg.x = viGetViewLeft() + 0x1e;
#ifdef VERSION_EU
                        msg.y = viGetViewTop() + 0x10;
#else
                        msg.y = viGetViewTop() + 0xd;
#endif
                    }

                    msg.bottom = msg.y + msg.textheight;
#ifndef TARGET_N64
                    if (portHudScaleOn())
                    {   /* HUD Scale: the bar keeps the full width at the text's
                         * halved rows, and the text is laid out from the view's
                         * top-left */
                        const s32 vt = viGetViewTop();

                        gdl = microcode_constructor_related_to_menus(gdl, 0, vt + (msg.y - 2 - vt) / 2, viGetX(),
                                                                     vt + (msg.bottom - vt + 1) / 2, 0x64);
                        gdl = portHudScaleBegin(gdl, (f32) viGetViewLeft(), (f32) vt);
                    }
                    else
#endif
                    gdl = microcode_constructor_related_to_menus(gdl, 0, msg.y - 2, viGetX(), msg.bottom, 0x64);
#ifdef VERSION_US
                    sw.screenwidth = viGetX();
                    gdl = textRender(gdl, &msg.x, &msg.y, stringbuffer_top[upper_text_buffer_index], ptrFontZurichBoldChars, ptrFontZurichBold, -1, sw.screenwidth, viGetY(), 0, 0);
#else
                    if (j_text_trigger != 0)
                    {
                        sw.screenwidth = viGetX();
#if defined(LEFTOVERDEBUG)
                        gdl = textRenderOutlined(gdl, &msg.x, &msg.y, stringbuffer_top[upper_text_buffer_index], ptrFontZurichBoldChars, ptrFontZurichBold, -1, 0x646464FF, sw.screenwidth, viGetY(), 0, 0);
#else
                        gdl = textRenderOutlined(gdl, &msg.x, &msg.y, dword_CODE_bss_80079DC8[upper_text_buffer_index], ptrFontZurichBoldChars, ptrFontZurichBold, -1, 0x646464FF, sw.screenwidth, viGetY(), 0, 0);
#endif
                    }
                    else
                    {
                        sw.screenwidth = viGetX();
#if defined(LEFTOVERDEBUG)
                        gdl = textRender(gdl, &msg.x, &msg.y, stringbuffer_top[upper_text_buffer_index], ptrFontZurichBoldChars, ptrFontZurichBold, -1, sw.screenwidth, viGetY(), 0, 0);
#else
                        gdl = textRender(gdl, &msg.x, &msg.y, dword_CODE_bss_80079DC8[upper_text_buffer_index], ptrFontZurichBoldChars, ptrFontZurichBold, -1, sw.screenwidth, viGetY(), 0, 0);
#endif
                    }
#endif
#ifndef TARGET_N64
                    gdl = portHudScaleEnd(gdl);
#endif
                    gdl = combiner_bayer_lod_perspective(gdl);
                    goto end;
                }
            }
        }
    }

    if (get_debug_testingmanpos_flag())
    {
        theta_x = g_CurrentPlayer->field_488.theta_transform.x;
        debug_angle = (s32) ((atan2f(-theta_x, g_CurrentPlayer->field_488.theta_transform.z) * 180.0f) / M_PI_F);
        directions = g_DebugCompassLabels;
        roomid = bgDebPrintROOMID(g_CurrentPlayer->field_488.current_tile_ptr->room);

        sprintf(debugtext.room, a8s, roomid);
        sprintf(debugtext.x, aX4_0f, g_CurrentPlayer->field_488.collision_position.x);
        sprintf(debugtext.y, aY4_0f, g_CurrentPlayer->field_488.collision_position.y);
        sprintf(debugtext.z, aZ4_0f, g_CurrentPlayer->field_488.collision_position.z);
#ifdef TARGET_N64
        sprintf(debugtext.angle, aS3d, ((char *) (&directions)) + (((debug_angle + 0x16) / 0x2d) * 3), debug_angle);
#else
        /* test-man harness: drop the cardinal prefix so heading + pitch
         * fit on one line */
        sprintf(debugtext.angle, "H %d", debug_angle);
        (void) directions;
#endif

        debug_x = viGetViewLeft() + 0x11;
        debug_y = viGetViewTop() + 0x11;
        debug_boxbottom = debug_y + 0xa;
        gdl = microcode_constructor(gdl);
        gdl = microcode_constructor_related_to_menus(gdl, 0, debug_y, viGetX(), debug_boxbottom + 1, 0x64);
        sw.screenwidth = viGetX();
        gdl = textRender(gdl, &debug_x, &debug_y, debugtext.room, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
        debug_x = viGetViewLeft() + 0x57;
        sw.screenwidth = viGetX();
        gdl = textRender(gdl, &debug_x, &debug_y, debugtext.x, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
        debug_x = viGetViewLeft() + 0x8d;
        sw.screenwidth = viGetX();
        gdl = textRender(gdl, &debug_x, &debug_y, debugtext.y, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
        debug_x = viGetViewLeft() + 0xc3;
        sw.screenwidth = viGetX();
        gdl = textRender(gdl, &debug_x, &debug_y, debugtext.z, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
        debug_x = viGetViewLeft() + 0xf9;
        sw.screenwidth = viGetX();
        gdl = textRender(gdl, &debug_x, &debug_y, debugtext.angle, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
#ifndef TARGET_N64
        { /* test-man harness: vertical look (vv_verta deg, negative = down)
           * appended to the readout so GE_SPAWNXYZ pitch can be captured */
            static char pitchtext[16];
            sprintf(pitchtext, "P %d", (s32) g_CurrentPlayer->vv_verta);
            debug_x = viGetViewLeft() + 0x120;
            sw.screenwidth = viGetX();
            gdl = textRender(gdl, &debug_x, &debug_y, pitchtext, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
        }
#endif
        gdl = combiner_bayer_lod_perspective(gdl);
    }

#ifndef TARGET_N64
#define PORT_OVERLAY_MP_DROP 0x10 /* 3/4P: one text row down, clearing the top-left pickup message */
    { /* midas.ini [input] show_input=1: live stick X/Y as the sim reads them
       * (after assignment, custom map and stick mode), to compare adapters
       * against the real console; drawn below the testing-man overlay. */
        extern s32 portInputShowInput(void); /* input_pc.c; weak 0 headless */
        /* MP: in 3/4P the pickup message sits at the quadrant's top-left under
         * these rows, so draw them only in player 1's viewport and, in 3+P,
         * push them below the message. */
        if (portInputShowInput() && (getPlayerCount() == 1 || get_cur_playernum() == 0))
        {
            static char sticktext[24];
            s32 in_x = viGetViewLeft() + 0x11;
            s32 in_y = viGetViewTop() + (get_debug_testingmanpos_flag() ? 0x20 : 0x11)
                     + ((getPlayerCount() >= 3) ? PORT_OVERLAY_MP_DROP : 0);
            s32 in_bot = in_y + 0xa;

            sprintf(sticktext, "X %d  Y %d", joyGetStickX(0), joyGetStickY(0));
            gdl = microcode_constructor(gdl);
            gdl = microcode_constructor_related_to_menus(gdl, 0, in_y, viGetX(), in_bot + 1, 0x64);
            sw.screenwidth = viGetX();
            gdl = textRender(gdl, &in_x, &in_y, sticktext, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
            gdl = combiner_bayer_lod_perspective(gdl);
        }
    }

    /* show_fps: portDrawFpsCounter, called from maybe_mp_interface */
    { /* GE_WATCHDBG=1: per-frame watch/hand state strip (also to watchdbg.log):
       * F = rendered frame, W = watch state/step, A = anim frame x10, R = arm
       * submitted, LH = left-hand gun state. */
        static s32 wd_en = -1;
        static u32 wd_frame = 0;
        static FILE *wd_log = NULL;
        if (wd_en < 0) { const char *e = getenv("GE_WATCHDBG"); wd_en = (e && *e && *e != '0') ? 1 : 0; }
        if (wd_en)
        {
            static char wdtext[128];
            struct hand *lh = &g_CurrentPlayer->hands[GUNLEFT];
            s32 wd_rows = (get_debug_testingmanpos_flag() ? 1 : 0) + (portInputShowInput() ? 1 : 0); /* show_fps sits in the corner now, not in this stack */
            s32 wd_x = viGetViewLeft() + 0x11;
            s32 wd_y = viGetViewTop() + 0x11 + (wd_rows * 0xf);
            s32 wd_bot = wd_y + 0xa;
            if (g_CurrentPlayer->watch_animation_state == 0 && !g_portWatchDbgArm && lh->weaponnum_watchmenu < 0 && lh->weapon_current_animation == 0) { wd_frame = 0; } else { wd_frame++; }
            sprintf(wdtext, "F %d  W %d/%d A %d R %d  LH %d/%d/%d an %d v %d inv %d",
                    (int) wd_frame, (int) g_CurrentPlayer->watch_animation_state, (int) g_CurrentPlayer->step_in_view_watch_animation,
                    (int) (g_CurrentPlayer->pause_animation_counter * 10.0f), (int) g_portWatchDbgArm,
                    (int) get_item_in_hand_or_watch_menu(GUNLEFT), (int) lh->weaponnum, (int) lh->weaponnum_watchmenu,
                    (int) lh->weapon_current_animation, (int) lh->field_87F, (int) g_CurrentPlayer->hand_invisible[GUNLEFT]);
            if (!wd_log) { wd_log = fopen("watchdbg.log", "w"); }
            if (wd_log) { fprintf(wd_log, "%s pausing %d\n", wdtext, (int) g_CurrentPlayer->pausing_flag); fflush(wd_log); }
            gdl = microcode_constructor(gdl);
            gdl = microcode_constructor_related_to_menus(gdl, 0, wd_y, viGetX(), wd_bot + 1, 0x64);
            sw.screenwidth = viGetX();
            gdl = textRender(gdl, &wd_x, &wd_y, wdtext, ptrFontBankGothicChars, ptrFontBankGothic, -1, sw.screenwidth, viGetY(), 0, 0);
            gdl = combiner_bayer_lod_perspective(gdl);
        }
        g_portWatchDbgArm = 0;
    }
#endif

    end:
    return gdl;
}


/**
 * Address: 0x7F08B0F0
 */
s32 playerTick(PropRecord *prop)
{
    s32 index;
    ChrRecord *chr;
    s32 group;
    s32 ret;
    s32 sub;
    PropRecord *leftprop;
    /* matrix_4x4_multiply_homogeneous writes a full 4x4 (16 floats); the
     * decomp's [15] let m[3][3] land one slot past the array (only mtx[12..14]
     * are read) */
    f32 mtx[16];
    s32 tailret;
    ModelAnimation *anim;
    f32 angle;
    f32 frame;
    f32 local90;
    f32 local8c;
    f32 local88;
    f32 fwd;
    f32 startframe;
    struct weapon_firing_animation_table *firingtable;
    coord3d off;
    s32 i;
    s32 found;
    ModelAnimation *cur;
    PropRecord *rightprop;
    struct WeaponObjRecord *leftobj;
    struct WeaponObjRecord *rightobj;
    s32 setanim;
    struct player **ppointers;
 
    index = getPlayerPointerIndex(prop);
    chr = prop->chr;
 
    if (chr != NULL)
    {
        if (get_player_position_in_shuffled(get_cur_playernum()) == 0)
        {
            chr->hidden &= ~CHRHIDDEN_FREEZE;
        }
    }
 
    if (chr != NULL)
    {
        if ((g_playerPointers[index]->bodyModel != NULL) && (!(get_debug_render_raster() && (g_playerPointers[index]->cameramode != 1))))
        {
            g_playerPointers[index]->field_AC = 0;
            ret = chrTick(prop);
            g_playerPointers[index]->field_AC = 1;
 
            g_playerPointers[index]->field_488.collision_position.x = g_playerPointers[index]->prop->pos.x;
            g_playerPointers[index]->field_488.collision_position.y = g_playerPointers[index]->prop->pos.y;
            g_playerPointers[index]->field_488.collision_position.z = g_playerPointers[index]->prop->pos.z;
            g_playerPointers[index]->field_488.current_tile_ptr = g_playerPointers[index]->prop->stan;
            bondviewUpdatePlayerRoom(g_playerPointers[index]);
 
            if (prop->flags & PROPFLAG_ONSCREEN)
            {
                RenderPosView *rp = g_playerPointers[index]->bodyModel->render_pos;
                matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), (Mtxf *) rp, (Mtxf *) mtx);
                g_playerPointers[index]->field_488.pos.x = mtx[12] + (mtx[4] * 7.0f);
                g_playerPointers[index]->field_488.pos.y = mtx[13] + (mtx[5] * 7.0f);
                g_playerPointers[index]->field_488.pos.z = mtx[14] + (mtx[6] * 7.0f);
            }
 
            return ret;
        }
    }
 
    if (chr == NULL)
    {
        goto clear_and_return;
    }

    if (g_playerPointers[index]->bodyModel == NULL)
    {
        goto clear_and_return;
    }

    if (getPlayerCount() < 2)
    {
        goto clear_and_return;
    }

    if (get_cur_playernum() == index)
    {
        goto clear_and_return;
    }
 
    anim = 0;
    firingtable = NULL;
    local90 = -1.0f;
    frame = -1;
    leftprop = chrGetEquippedWeaponProp(chr, GUNLEFT);
    rightprop = chrGetEquippedWeaponProp(chr, GUNRIGHT);
    leftobj = NULL;
    rightobj = NULL;
    setanim = 0;
 
    if (leftprop != NULL)
    {
        leftobj = leftprop->weapon;
    }
    
    if (rightprop != NULL)
    {
        rightobj = rightprop->weapon;
    }
 
    ppointers = g_playerPointers;
 
    if (get_player_position_in_shuffled(get_cur_playernum()) == 0)
    {
        g_PlayerTickCount = g_PlayerTickCount + 1;
    }
 
    /**
     * If the player count is 1 we jump to the bottom of the function with goto clear_and_return, so this block only applies to MP.
     * g_PlayerTickCount advances once for each remote player's prop ticked during the pass of the viewport whose player is first in the shuffle order.
     * That means g_PlayerTickCount reaches 2 after one frame of a 2 player game, and part way through the first frame of a 3 or 4 player game.
     * Nothing *ever* resets g_PlayerTickCount, so we're skipping this whole block only once per boot. The reason for doing this though
     * isn't quite clear so do chime in if you have any theories.
     */
    if (g_PlayerTickCount >= 2)
    {
        local8c = ((0, ppointers[index]))->field_2A08;
        local88 = ppointers[index]->field_2A0C;
 
        if (ppointers[index]->bonddead != FALSE)
        {
            found = 0;
 
            for (i = 0; i < g_bondviewBondDeathAnimationsCount; i++)
            {
                cur = ppointers[index]->players_cur_animation;
 
                if (cur == portAnimationFromOffset(g_bondviewBondDeathAnimations[i]))
                {
                    found = 1;
                }
            }
 
            if (found)
            {
                anim = ppointers[index]->players_cur_animation;
                angle = 0.5f;
            }
            else
            {
                anim = portAnimationFromOffset(g_bondviewBondDeathAnimations[randomGetNext() % g_bondviewBondDeathAnimationsCount]);
                angle = 0.5f;
            }
 
            cur = ppointers[index]->players_cur_animation;
            local8c = 0.0f;
            local88 = 0.0f;
            goto join_768;
        }
 
        if ((leftprop != NULL) && (rightprop != NULL))
        {
            group = 3;
        }
        else if ((leftprop == NULL) && (rightprop == NULL))
        {
            group = 2;
        }
        else if ((leftobj != NULL) && (bondwalkItemCheckBitflags(leftobj->weaponnum, WEAPONSTATBITFLAG_HOLD_AS_GUN) == 0))
        {
            group = 2;
        }
        else if ((rightobj != NULL) && (bondwalkItemCheckBitflags(rightobj->weaponnum, WEAPONSTATBITFLAG_HOLD_AS_GUN) == 0))
        {
            group = 2;
        }
        else if ((leftobj != NULL) && (bondwalkItemCheckBitflags(leftobj->weaponnum, WEAPONSTATBITFLAG_ONLY_1_HANDED) != 0))
        {
            group = 0;
        }
        else if ((rightobj != NULL) && (bondwalkItemCheckBitflags(rightobj->weaponnum, WEAPONSTATBITFLAG_ONLY_1_HANDED) != 0))
        {
            group = 0;
        }
        else
        {
            group = 1;
        }
 
        if (playerGetCrouchPos(index) == 1)
        {
            goto set_crouch_lean;
        }
        else if (playerGetCrouchPos(index) == 0)
        {
set_crouch_lean:
            angle = 1.0f;
            sub = 5;
        }
        else if ((ppointers[index]->speedsideways < 0.0f) && (firing_animation_groups[group][4].pointer != NULL))
        {
            sub = 4;
            angle = -ppointers[index]->speedsideways;
 
            if (ppointers[index]->field_1280 < 90.0f)
            {
                ppointers[index]->field_1280 = ppointers[index]->field_1280 + 15.0f;
            }
        }
        else if ((ppointers[index]->speedsideways > 0.0f) && (firing_animation_groups[group][3].pointer != NULL))
        {
            sub = 3;
            angle = ppointers[index]->speedsideways;
 
            if (ppointers[index]->field_1280 > (-90.0f))
            {
                ppointers[index]->field_1280 = ppointers[index]->field_1280 - 15.0f;
            }
        }
        else
        {
            frame = ppointers[index]->speedtheta;
 
            if (frame < 0.0f)
            {
                frame = -frame;
            }
 
            fwd = ppointers[index]->speedforwards;
 
            if (fwd < -0.050000001f)
            {
                frame = -frame;
 
                if (fwd < frame)
                {
                    frame = fwd;
                }
 
                if (ppointers[index]->headanim == 0)
                {
                    goto shared_double_neg;
                }
 
                sub = 2;
 
                if (-0.40000001f < fwd)
                {
shared_double_neg:
                    angle = frame + frame;
                    sub = 1;
 
                    if (angle < (-1.0f))
                    {
                        angle = -1.0f;
                    }
                }
                else
                {
                    angle = frame;
                    goto lean_return_to_centre;
                }
            }
            else
            {
                if (0.050000001f < fwd)
                {
                    goto shared_framefwd;
                }
                else if (0.050000001f < frame)
                {
shared_framefwd:
                    if (frame < fwd)
                    {
                        frame = fwd;
                    }
                }
                else
                {
                    goto set_full_lean;
                }
 
                if (ppointers[index]->headanim == 0)
                {
                    goto shared_double_pos;
                }
 
                sub = 2;
 
                if (fwd < 0.40000001f)
                {
shared_double_pos:
                    angle = frame + frame;
                    sub = 1;
 
                    if (1.0f < angle)
                    {
                        angle = 1.0f;
                    }
                }
                else
                {
                    angle = frame;
                    goto lean_return_to_centre;
                }
            }
 
            goto lean_return_to_centre;
 
set_full_lean:
            angle = 1.0f;
            sub = 0;
 
lean_return_to_centre:
            if (0.0f < ppointers[index]->field_1280)
            {
                ppointers[index]->field_1280 = ppointers[index]->field_1280 - 15.0f;
            }
 
            if (ppointers[index]->field_1280 < 0.0f)
            {
                ppointers[index]->field_1280 = ppointers[index]->field_1280 + 15.0f;
            }
        }
 
        {
            struct firing_anim_struct *fa = &firing_animation_groups[group][sub];
 
            firingtable = fa->pointer;
 
            if (fa->anim != 0)
            {
                anim = portAnimationFromOffset(fa->anim);
            }
 
            angle *= fa->x;
            local90 = fa->z;
            frame = fa->y;
        }
 
        cur = ppointers[index]->players_cur_animation;
 
join_768:
        if ((firingtable != NULL) && (anim == 0))
        {
            anim = firingtable->anim.anim;
        }
 
        if (anim != cur)
        {
            setanim = 1;
        }
 
        if (0.0f <= frame)
        {
            if ((ppointers[index]->bodyModel->animlooping == 0) || (frame != ppointers[index]->bodyModel->animloopframe))
            {
                setanim = 1;
            }
        }
 
        if (frame < 0.0f)
        {
            if (ppointers[index]->bodyModel->animlooping)
            {
                setanim = 1;
            }
        }
 
        if (setanim != 0)
        {
            if (ppointers[index]->bodyModel->anim2 == NULL)
            {
                startframe = (0.0f <= frame) ? (frame) : (0.0f);
                modelSetAnimation(ppointers[index]->bodyModel, anim, 0, startframe, angle, 16.0f);
                ppointers[index]->players_cur_animation = anim;
                ppointers[index]->field_1288 = angle;
 
                if (0.0f <= frame)
                {
                    modelSetAnimLooping(ppointers[index]->bodyModel, frame, 16.0f);
                }
 
                if (0.0f <= local90)
                {
                    modelSetAnimEndFrame(ppointers[index]->bodyModel, local90);
                }
            }
 
            cur = ppointers[index]->players_cur_animation;
        }
        else
        {
            if (angle != ppointers[index]->field_1288)
            {
                modelSetAnimSpeed(ppointers[index]->bodyModel, angle, 1.0f);
                ppointers[index]->field_1288 = angle;
            }
 
            cur = ppointers[index]->players_cur_animation;
        }
 
        if (anim == cur)
        {
            if (firingtable != NULL)
            {
                chr->hidden &= ~CHRHIDDEN_0400;
                chrlvUpdateAimendbackShoulders(chr, firingtable, 0, 1, local8c);
            }
            else
            {
                chr->aimendrshoulder = 0.0f;
                chr->aimendlshoulder = 0.0f;
                chr->hidden |= CHRHIDDEN_0400;
                chr->aimendback = local8c;
            }
        }
 
        chr->aimendsideback = local88;
        chr->aimendcount = 10;
    }
 
    prop->pos.x = ppointers[index]->field_488.collision_position.x;
    prop->pos.y = ppointers[index]->field_488.collision_position.y;
    prop->pos.z = ppointers[index]->field_488.collision_position.z;
    prop->stan = ppointers[index]->field_488.current_tile_ptr;
 
    getsuboffset(chr->model, &off);
    off.x = prop->pos.x;
    off.z = prop->pos.z;
    setsuboffset(chr->model, &off);
    setsubroty(chr->model, (((360.0f - ppointers[index]->vv_theta) + ppointers[index]->field_1280) * M_TAU_F) / 360.0f);
 
    chr->chrflags |= CHRFLAG_INIT;
    chr->actiontype = ACT_BONDMULTI;
    chr->act_bondmulti.unk2c = (f32 *) firingtable;
 
    chrSetFiring(chr, GUNRIGHT, ppointers[index]->hands[GUNRIGHT].field_87D);
    chrSetFiring(chr, GUNLEFT, ppointers[index]->hands[GUNLEFT].field_87D);
 
    tailret = chrTick(prop);
 
    for (i = 0; i != 2; i++)
    {
        if (sub_GAME_7F02D630(chr, i, &ppointers[index]->field_2A18[i]) != 0)
        {
            (&ppointers[index]->field_2A30)[i] = g_GlobalTickCount;
        }
        else if ((&ppointers[index]->field_2A30)[i] < (g_GlobalTickCount - 1))
        {
            ppointers[index]->field_2A18[i].x = ppointers[index]->hands[i].field_B58.x;
            ppointers[index]->field_2A18[i].y = ppointers[index]->hands[i].field_B58.y;
            ppointers[index]->field_2A18[i].z = ppointers[index]->hands[i].field_B58.z;
        }
    }
 
    chr->hidden |= CHRHIDDEN_FREEZE;
 
    prop->pos.x = ppointers[index]->field_488.collision_position.x;
    prop->pos.y = ppointers[index]->field_488.collision_position.y;
    prop->pos.z = ppointers[index]->field_488.collision_position.z;
    prop->stan = ppointers[index]->field_488.current_tile_ptr;
 
    return tailret;
 
clear_and_return:
    prop->flags &= ~PROPFLAG_ONSCREEN;

    return TICKOP_NONE;
}


/**
 * Address 0x7F08BCB8.
 */
Gfx * bondviewRemoved7F08BCB8(Gfx *arg0)
{
    #ifdef DEBUG
    // removed
    #endif

    return arg0;
}


/**
 * Address 0x7F08BCC0.
 */
Gfx *bondviewRenderProp(PropRecord *arg0, Gfx *arg1, s32 arg2)
{
    if (arg0->chr != NULL)
    {
        arg1 = chrRenderProp(arg0, arg1, arg2);
    }

    return arg1;
}





/**
 * Address 0x7F08BCF4.
 */
Gfx* bondviewGfxPlayerField5cMatrix(Gfx* gdl)
{
    gSPMatrix(gdl++, g_CurrentPlayer->field_5C, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    return gdl;
}





/**
 * Unreferenced.
 *
 * Address 0x7F08BD18.
 */
void bondviewTransformPosToViewMatrix(RenderPosView *arg0)
{
    Mtxf sp18;

    matrix_4x4_copy(&arg0->pos, (Mtxf *) &sp18);
    matrix_4x4_f32_to_s32((Mtxf *) &sp18, &arg0->view);
}



/**
 * Address 0x7F08BD48.
 *
 * Notes: Similar to sub_GAME_7F08BE2C.
 *
 */
void bondviewTransformManyPosToViewMatrix(RenderPosView * arg0, s32 arg1)
{
    Mtxf mtx;
    RenderPosView* rpv_entry;
    s32 i;

    i = 0;
    if (arg1 <= 0) { return; }

    // Couldn't find a better matching loop
    rpv_entry = arg0;
    do
    {
        matrix_4x4_copy(&rpv_entry->pos, &mtx);
        matrix_4x4_f32_to_s32(&mtx, &arg0[i].pos);
        i++;
        rpv_entry++;
    } while (i != arg1);
}



/**
 * Unreferenced.
 *
 * Address 0x7F08BDC4.
 */
void sub_GAME_7F08BDC4(Mtxf *arg0)
{
    Mtxf sp20;

    matrix_4x4_copy(arg0, (Mtxf *) &sp20);
    sp20.m[3][0] -= g_CurrentPlayer->previous_model_pos.f[0];
    sp20.m[3][1] -= g_CurrentPlayer->previous_model_pos.f[1];
    sp20.m[3][2] -= g_CurrentPlayer->previous_model_pos.f[2];
    matrix_4x4_f32_to_s32((Mtxf *) &sp20, arg0);
}


/**
 * Unreferenced.
 *
 * Address 0x7F08BE2C.
 */
void sub_GAME_7F08BE2C(Mtxf *matrices, s32 count)
{
    Mtxf copy;
    s32 i;

    for (i = 0; i < count; i++)
    {
        matrix_4x4_copy((Mtxf *)((uintptr_t)matrices + i * sizeof(Mtxf)), &copy);

        copy.m[3][0] -= g_CurrentPlayer->previous_model_pos.x;
        copy.m[3][1] -= g_CurrentPlayer->previous_model_pos.y;
        copy.m[3][2] -= g_CurrentPlayer->previous_model_pos.z;

        matrix_4x4_f32_to_s32(&copy, matrices + i);
    }
}


void sub_GAME_7F08BEEC(Mtxf *matrices, s32 count)
{
    Mtxf sp40;
    s32 i;
    s32 j;

    for (i = 0, j = 0; i < count; i++, j += sizeof(Mtxf))
    {
        matrix_4x4_multiply_homogeneous(currentPlayerGetViewToWorldMtxf(), (Mtxf *)((uintptr_t)matrices + j), &sp40);

        sp40.m[3][0] -= g_CurrentPlayer->current_model_pos.f[0];
        sp40.m[3][1] -= g_CurrentPlayer->current_model_pos.f[1];
        sp40.m[3][2] -= g_CurrentPlayer->current_model_pos.f[2];

        matrix_4x4_f32_to_s32(&sp40, matrices + i);
    }
}


s32 getMissiontimer(void) {
#ifdef VERSION_EU
    return (mission_timer * 60) / 50;
#else
    return mission_timer;
#endif
}


void SurroundWithExplosions(int delay)
{
    g_SurroundBondWithExplosionsFlag = 1;
    g_SurroundBondWithExplosionsTicks = delay + g_GlobalTimer;
    g_PlayerTickExplodeCreatePosition = 0;
}
