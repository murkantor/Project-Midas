#include <ultra64.h>
#include "game/cleanup_alarms.h"
#include "game/initactorpropstuff.h"
#include "game/music_0D2720.h"
#include "game/objective_status.h"
#include "game/sky.h"
#include <math.h>
#include <os_extension.h>
#include <PR/libaudio.h>
#include <assets/font_dl.h>
#include <deb.h>
#include <memp.h>
#include <music.h>
#include <tlb_manage.h>
#include <fr.h>
#include <snd.h>
#include <ramrom.h>
#include <random.h>
#include <joy.h>
#include <token.h>
#include "debugmenu_handler.h"
#include "lv.h"
#include "language.h"
#include "initcheattext.h"
#include "front.h"
#include "bondinv.h"
#include "player.h"
#include "propobj.h"
#include "cleanup_objects.h"
#include "explosion.h"
#include "chrai.h"
#include "mp_music.h"
#include "initunk_005520.h"
#include "initmttex.h"
#include "glass.h"
#include "image_bank.h"
#include "textrelated.h"
#include "initmenus.h"
#include "cheat.h"
#include "bg.h"
#include "objective.h"
#include "mpmenu.h"
#include "vtxstore.h"
#include "initunk_005450.h"
#include "initobjects.h"
#include "initguards.h"
#include "prop.h"
#include "initexplosioncasing.h"
#include "alloc_window_pieces.h"
#include "initunk_007290.h"
#include "initcheattext.h"
#include "initpathtablelinks.h"
#include "ejectedcartridges.h"
#include "inititemslots.h"
#include "initBondDATA.h"
#include "bondview.h"
#include "bondview_r.h"
#include "initBondDATAdefaults.h"
#include "viewport.h"
#include "stan.h"
#include "gun.h"
#include "debug_camera.h"
#include "mp_music.h"
#include "bgroomtrans.h"
#include "unk_092E50.h"
#include "frametiming.h"
#include "chr.h"

// bss
//CODE.bss:8008C260
u32 *ptr_font_DL;
//CODE.bss:8008C264
s32 dword_CODE_bss_8008C264;
s32 dword_CODE_bss_8008C268;
s32 dword_CODE_bss_8008C26C;
//CODE.bss:8008C270
char ramrom_data_target[0x380];
//CODE.bss:8008C5F0
s32 record_slot_num;
//CODE.bss:8008C5F4
u8 * address_demo_loaded;
//CODE.bss:8008C5F8
s32 dword_CODE_bss_8008C5F8;


// data
//D:80048360
s32 lvl_c_debug_notice_list = 0;
//D:80048364
s32 g_CurrentStageToLoad = 0;
//D:80048368
f32 D_80048368 = 1.0;
//D:8004836C
s32 musictrack1_playing = 0;
//D:80048370
s32 g_ControlsLockedFlag = 0;
//D:80048374
s32 g_ClockTimer = 0;


#if defined (BUGFIX_R1)
// addresses updated, per build\ge007.j.map
// 800483a8
f32 g_JP_GlobalTimerDelta = 0;
// 800483ac
s32 g_GlobalTimer = 0;
// 800483b0
s32 g_GlobalTickCount = 0;
// 800483b4
f32 g_GlobalTimerDelta = 0;
#else
//D:80048378
f32 g_GlobalTimerDelta = 0;

//D:8004837C
/**
 * Accumulated video field periods since stage load i.e. 60/s on NTSC and 50/s on PAL. This remains true no
 * matter how badly the frame rate dips. Freezes while the game is paused or controls are locked.
 */
s32 g_GlobalTimer = 0;

//D:80048380
/**
 * Set to 0 on stage load, increments by one tick per rendered *frame*. This means the rate it increases depends on the frame rate.
 */
s32 g_GlobalTickCount = 0;

//D:80048384
#endif

#ifndef TARGET_N64
s32 g_PortTickSerial = 0; /* GE_TICKHZ: per-tick serial, see lvlTick */
#endif
/*
* Selected difficulty mode.
* 0x80048384
*/
s32 g_SelectedDifficulty = DIFFICULTY_AGENT;

//D:80048388
s32 D_80048388 = 0;
//D:8004838C
s32 D_8004838C = 0;
//D:80048390
s32 D_80048390 = 0;
//D:80048394
s32 D_80048394 = 0;

/**
 * Address 0x80048398.
 */
s32 g_MpTime = 0x8CA0;

/**
 * Address 0x8004839C.
 */
s32 g_MpPoint = 0xA;

/**
 * Address 0x800483A0.
 */
ALSoundState * g_MpSoundStateRelated = NULL;

/**
 * Address 0x800483A4.
 */
f32 g_CurrentMultiPlayerSec = 0.0;

//D:800483A8
s32 D_800483A8 = 0;

/**
 * Address 0x800483AC.
 */
f32 g_CurrentMultiPlayerMin = 0.0;

//D:800483B0
s32 D_800483B0 = 0;

/**
 * Address 0x800483B4.
 */
f32 g_StageTimeSec = 0;

//D:800483B8
s32 D_800483B8 = 0;

/**
 * Power on time in seconds.
 * Address 0x800483BC.
 */
f32 g_PowerOnTimeSec = 0;

/**
 * Debug variable, seems to track whether user input has changed since
 * the last time the method was entered.
 *
 * Addres 0x800483C0.
 */
s32 D_800483C0 = 1;

//D:800483C4
s32 hittype = 0xFFFFFFFF;

//D:800483C8
struct LvlMpUnknown *D_800483C8 = NULL;

/**
* Debug variable, something to do with portals.
* Address 0x800483CC.
*/
s32 g_DebugPortalIndex = 0;

/**
 * Input buffer used in debug portal method.
 * Address 0x800483D0.
 */
s32 g_DebugPortalsInputBuffer = 0;
s32 g_DebugPortalsInputBuffer1 = 0;
s32 g_DebugPortalsInputBuffer2 = 0;
s32 g_DebugPortalsInputBuffer3 = 0;
s32 g_DebugPortalsInputBuffer4 = 0;

#ifdef __APPLE__
#define g_DebugPortalsInputBufferSource1 g_DebugPortalsInputBuffer1
#define g_DebugPortalsInputBufferSource2 g_DebugPortalsInputBuffer2
#define g_DebugPortalsInputBufferSource3 g_DebugPortalsInputBuffer3
#define g_DebugPortalsInputBufferSource4 g_DebugPortalsInputBuffer4
#else
extern s32 g_DebugPortalsInputBufferSource1;
extern s32 g_DebugPortalsInputBufferSource2;
extern s32 g_DebugPortalsInputBufferSource3;
extern s32 g_DebugPortalsInputBufferSource4;

#pragma weak g_DebugPortalsInputBufferSource1 = g_DebugPortalsInputBuffer1
#pragma weak g_DebugPortalsInputBufferSource2 = g_DebugPortalsInputBuffer2
#pragma weak g_DebugPortalsInputBufferSource3 = g_DebugPortalsInputBuffer3
#pragma weak g_DebugPortalsInputBufferSource4 = g_DebugPortalsInputBuffer4
#endif

/**
 * Something debug related in the MP manage method.
 * Index to play sound effect.
 * Address 0x800483E4.
 */
s16 g_DebugMpGameSoundFxIndex = 0;

// unused address padding
s16 D_800483E6 = 0;


extern u8* _fontdlSegmentRomStart;
extern u8* _fontdlSegmentRomEnd;


// forward declarations

Gfx * lvlPortalDebug7F0BDF10(Gfx * arg0);

// end forward declarations

s32 sub_GAME_7F0BD8F0(void)
{
    return D_800483C0;
}

void sub_GAME_7F0BD8FC(s32 arg0)
{
    D_800483C0 = arg0;
}

void lvInit(void)
{
    s32 size;

    debTryAdd(&lvl_c_debug_notice_list, "lv_c_debug");
    size = (intptr_t)&_fontdlSegmentRomEnd - (intptr_t)&_fontdlSegmentRomStart;
    lvl_c_debug_notice_list = 1;
    ptr_font_DL = mempAllocBytesInBank(size, MEMPOOL_PERMANENT);
    romCopy(ptr_font_DL, &_fontdlSegmentRomStart, size);
}

/**
 * Unreferenced.
 */
void lvlPlayRandomMusicTrack1(void)
{
    musictrack1_playing = randomGetNext() % 0x3dU + M_INTRO;
    musicTrack1Play(musictrack1_playing);
}

void lvlPlayMusicTrack1(MUSIC_TRACKS track)
{
    musictrack1_playing = track;
    musicTrack1Play(musictrack1_playing);
}

void lvlMusicAppendPlaySoloDeathShort(void)
{
    musictrack1_playing = (musictrack1_playing + M_SHORT_SOLO_DEATH) % NUM_MUSIC_TRACKS;

    if (musictrack1_playing == M_NONE)
    {
        musictrack1_playing = M_SHORT_SOLO_DEATH;
    }

    musicTrack1Play(musictrack1_playing);
}

void lvlMusicAppendPlayEndTheme(void)
{
    musictrack1_playing = (musictrack1_playing + M_END_SOMETHING) % NUM_MUSIC_TRACKS;

    if (musictrack1_playing == M_NONE)
    {
        musictrack1_playing = M_END_SOMETHING;
    }

    musicTrack1Play(musictrack1_playing);
}

/**
 * Unreferenced.
 */
void lvlMusicPlayStageTrackOrRandom(void)
{
    lvlPlayMusicTrack1(getmusictrack_or_randomtrack(g_CurrentStageToLoad));
}

/**
 * Stage load method.
 * Title screen is handled as a special case.
 * First half of method resets stage and player values (including mutliplayer values) to defaults.
 * Second part loads stage data (init guards, init guard heads, etc).
 *
 * NTSC Address: 0x7F0BDAB0.
 * NTSC-J Address: 7F0BE660.
 * PAL Address: 7F0BCE60.
 **/
#ifndef TARGET_N64
#include <stdio.h> /* goto-harness progress prints */
/* GE_CHEATS=invuln,invis,guns,turbo,rockets,dualzmg - activate cheats
 * shortly after each stage load (needs the player fully spawned). "guns" =
 * all weapons + infinite ammo. "rockets" = Enemy
 * Rockets (every armed guard's weapon becomes a rocket launcher; see
 * true_enemy_rockets in midas.ini for the missing-break fix to this cheat).
 * Uses the cheat menu's own activation entry point so flags, side effects
 * and HUD messages all behave. */
static s32 g_PortCheatCountdown = -1;
static s32 g_PortDualZmgCountdown = -1;

static s32 portStrHas(const char *hay, const char *needle)
{
    s32 i, j;
    for (i = 0; hay[i] != '\0'; i++)
    {
        for (j = 0; needle[j] != '\0' && (hay[i + j] | 0x20) == needle[j]; j++)
        {
        }
        if (needle[j] == '\0')
        {
            return 1;
        }
    }
    return 0;
}

static void portCheatTick(void)
{
    extern char *getenv(const char *);
    extern void cheatButtonHandleCheatsTurnedOn(CHEAT_ID cheat_id);
    const char *e;

    if (g_PortCheatCountdown < 0)
    {
        return;
    }
    if (g_PortCheatCountdown > 0)
    {
        g_PortCheatCountdown--;
        return;
    }
    g_PortCheatCountdown = -1;

    e = getenv("GE_CHEATS");
    if (e == NULL || e[0] == '\0')
    {
        return;
    }
    if (portStrHas(e, "invuln"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_INVINCIBILITY);
    }
    if (portStrHas(e, "invis"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_INVISIBILITY);
    }
    if (portStrHas(e, "guns"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_ALLGUNS);
        cheatButtonHandleCheatsTurnedOn(CHEAT_INFINITE_AMMO);
    }
    if (portStrHas(e, "turbo"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_TURBO_MODE);
    }
    if (portStrHas(e, "rockets"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_ENEMY_ROCKETS);
    }
    if (portStrHas(e, "linemode"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_LINEMODE);
    }
    if (portStrHas(e, "enemyff"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_PORT_ENEMY_FF);
    }
    if (portStrHas(e, "doors"))
    {
        cheatButtonHandleCheatsTurnedOn(CHEAT_PORT_UNLOCK_DOORS);
    }
    if (portStrHas(e, "dualzmg"))
    {
        g_PortDualZmgCountdown = 240; /* stages that start disarmed
                                       * (Archives interrogation) eat an
                                       * immediate equip - re-apply ~4s in */
    }
}

/* GE_CHEATS=rockets needs to be live BEFORE weaponAssignToHome/
 * proplvreset2 build level-start guard weapons, and it needs the same
 * persistent state a real cheat-menu selection sets (g_CheatActivated[]
 * + g_AppendCheatSinglePlayer) so lvlTick's per-tick reapplication below
 * keeps it active for the whole level, not just this first load pass. */
static void portCheatApplyLoadTimeSafe(void)
{
    extern char *getenv(const char *);
    extern void cheatButtonHandleCheatsTurnedOn(CHEAT_ID cheat_id);
    extern u8 g_CheatActivated[];
    extern s32 g_AppendCheatSinglePlayer;
    const char *e = getenv("GE_CHEATS");

    if (e == NULL || e[0] == '\0')
    {
        return;
    }
    if (portStrHas(e, "rockets"))
    {
        g_CheatActivated[CHEAT_ENEMY_ROCKETS] = TRUE;
        g_AppendCheatSinglePlayer = TRUE;
        cheatButtonHandleCheatsTurnedOn(CHEAT_ENEMY_ROCKETS);
    }
}

static void portDualZmgTick(void)
{
    if (g_PortDualZmgCountdown < 0)
    {
        return;
    }
    if (g_PortDualZmgCountdown > 0)
    {
        g_PortDualZmgCountdown--;
        return;
    }
    g_PortDualZmgCountdown = -1;

    /* weapon-depth test harness: dual ZMGs in hand
     * (bondinv.h / gun.h declare the give+equip API) */
    bondinvAddInvItem(ITEM_UZI);
    bondinvAddDoublesInvItem(ITEM_UZI, ITEM_UZI);
    set_max_ammo_for_cur_player();
    currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_UZI);
    currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UZI);
}

/* GE_SPAWNWEAPON=<name|modelnum>[:itemnum][,...] (texture test harness):
 * about 2s into the mission, spawn the named weapon/pickup models in a ring
 * around Bond as uncollectable free props (via gunInitProjectileObject, the
 * thrown-item path). Known names are listed below; anything else is taken as a
 * raw PROP model number.
 *
 * Known issue, work parked: the props spawn and register as on screen but draw
 * nothing, probably the same bug as other runtime-created models. */
static s32 g_PortSpawnWepCountdown = -1;
static PropRecord *g_PortSpawnWepProps[8];
static s32 g_PortSpawnWepCount = 0;

/* 1Hz tracker: where did the dropped props end up (pos, onscreen, room) */
static void portSpawnWepTrace(void)
{
    extern int printf(const char *, ...);
    static s32 tick = 0;
    s32 i;

    if (g_PortSpawnWepCount == 0 || (++tick % 300) != 0)
    {
        return;
    }
    for (i = 0; i < g_PortSpawnWepCount; i++)
    {
        extern f32 chrobjFogVisRangeRelated(PropRecord *, f32);
        extern f32 getinstsize(Model *);
        PropRecord *p = g_PortSpawnWepProps[i];
        ObjectRecord *o = p->obj;
        f32 instsize = (o != NULL && o->model != NULL) ? getinstsize(o->model) : -1.0f;
        f32 visfrac = (o != NULL && o->model != NULL) ? chrobjFogVisRangeRelated(p, instsize) : -1.0f;
        printf("ge: spawnweptrace: %d pos=(%.0f,%.0f,%.0f) flags=%x onscreen=%d room0=%d scale=%f instsize=%f visfrac=%f timetoregen=%d\n",
               i, p->pos.x, p->pos.y, p->pos.z, (int)p->flags,
               (p->flags & PROPFLAG_ONSCREEN) != 0, (int)p->rooms[0],
               (o != NULL && o->model != NULL) ? o->model->scale : -1.0f,
               instsize, visfrac, (int)p->timetoregen);
    }
}

static void portSpawnWeaponTick(void)
{
    extern char *getenv(const char *);
    static const struct { const char *name; s32 model; u8 item; } names[] = {
        { "laser",    PROP_CHRLASER, ITEM_LASER },
        { "rcp90",    PROP_CHRFNP90, ITEM_FNP90 },
        { "rcp90mag", PROP_FNP90MAG, ITEM_FNP90 },
    };
    const char *e;
    PropRecord *bondprop;

    if (g_PortSpawnWepCountdown < 0)
    {
        return;
    }
    if (g_PortSpawnWepCountdown > 0)
    {
        g_PortSpawnWepCountdown--;
        return;
    }
    g_PortSpawnWepCountdown = -1;

    e = getenv("GE_SPAWNWEAPON");
    if (e == NULL || e[0] == '\0')
    {
        return;
    }
    bondprop = getCurrentPlayerProp();
    if (bondprop == NULL || bondprop->stan == NULL)
    {
        /* no stan means this is the frontend viewer, not mission Bond, so retry
         * in 1s (dropping now would attach the items to the menu prop at the origin) */
        g_PortSpawnWepCountdown = 60;
        return;
    }

    while (*e != '\0')
    {
        s32 model = -1;
        s32 item = 0;
        s32 k;
        s32 len = 0;

        while (e[len] != '\0' && e[len] != ',')
        {
            len++;
        }
        for (k = 0; k < (s32)(sizeof(names) / sizeof(names[0])); k++)
        {
            s32 n = 0;
            while (names[k].name[n] != '\0' && n < len &&
                   (e[n] | 0x20) == names[k].name[n])
            {
                n++;
            }
            if (names[k].name[n] == '\0' && (n == len || e[n] == ':'))
            {
                model = names[k].model;
                item = names[k].item;
                break;
            }
        }
        if (model < 0 && e[0] >= '0' && e[0] <= '9')
        {
            model = 0;
            for (k = 0; k < len && e[k] >= '0' && e[k] <= '9'; k++)
            {
                model = model * 10 + (e[k] - '0');
            }
        }
        for (k = 0; k < len; k++)
        {
            if (e[k] == ':')
            {
                item = 0;
                for (k++; k < len && e[k] >= '0' && e[k] <= '9'; k++)
                {
                    item = item * 10 + (e[k] - '0');
                }
                break;
            }
        }
        if (model >= 0)
        {
            /* Spawn as a free physical prop like a thrown item
             * (gunInitProjectileObject also activates, shades and registers it
             * in a room, which the chr-drop path skipped).
             * PROPFLAG_UNCOLLECTABLE keeps them on the ground; items line up
             * along +x from Bond, 70 units apart. */
            extern int printf(const char *, ...);
            extern ObjectRecord *create_new_item_instance_of_model(PROP modelnum, s32 weaponid);
            WeaponObjRecord *w =
                (WeaponObjRecord *)create_new_item_instance_of_model(model, (u8)item);

            if (w != NULL && w->prop != NULL)
            {
                Mtxf mtx;
                Mtxf rotmtx;
                struct coord3d pos;
                struct coord3d vel;

                matrix_4x4_set_identity(&mtx);
                matrix_4x4_set_identity(&rotmtx);
                { /* ring placement: 8 slots around Bond at radius 130 so
                   * some items land in view whatever he faces */
                    extern f32 sinf(f32);
                    extern f32 cosf(f32);
                    f32 ang = (f32)g_PortSpawnWepCount * (M_TAU_F / 8.0f);
                    pos = bondprop->pos;
                    pos.x += sinf(ang) * 130.0f;
                    pos.z += cosf(ang) * 130.0f;
                    pos.y += 20.0f;
                }
                vel.x = 0.0f;
                vel.y = 0.0f;
                vel.z = 0.0f;
                w->flags |= PROPFLAG_UNCOLLECTABLE;
                gunInitProjectileObject((ObjectRecord *)w, &pos, bondprop->stan,
                                        &mtx, &vel, &rotmtx, bondprop);
                if (g_PortSpawnWepCount < 8)
                {
                    g_PortSpawnWepProps[g_PortSpawnWepCount++] = w->prop;
                }
            }
            printf("ge: spawnweapon: model %d item %d -> %s\n",
                   model, item, (w != NULL && w->prop != NULL) ? "spawned" : "FAILED");
        }
        e += len;
        if (*e == ',')
        {
            e++;
        }
    }
}

/* GE_DEBUGPOS=1: the leftover "testing man pos" debug overlay (room id,
 * x/y/z, compass aim angle across the top of the view). */
static void portDebugPosApply(void)
{
    extern char *getenv(const char *);
    extern void set_debug_testingmanpos_flag(s32 flag);
    const char *e = getenv("GE_DEBUGPOS");
    if (e != NULL && e[0] != '\0' && e[0] != '0')
    {
        set_debug_testingmanpos_flag(TRUE);
    }
}

/* GE_GOTO="x,y,z" (test harness): steer Bond in a straight line to a position,
 * turning toward it, walking when roughly facing it, and stopping inside the
 * arrival radius. The stick values set here are injected on pad 0 by
 * input_pc.c's portInputPadMerge; indoor routes would need the guard pad graph
 * instead. */
s32 g_PortGotoActive = 0;
s32 g_PortGotoStickX = 0;
s32 g_PortGotoStickY = 0;

static void portGotoTick(void)
{
    extern char *getenv(const char *);
    extern PropRecord *getCurrentPlayerProp(void);
    extern float sqrtf(float);
    extern float atan2f(float, float);
    static s32 state = -2; /* -2 unparsed, -1 off, 0 steering, 1 arrived */
    static f32 tx, ty, tz;
    static s32 dbgtick;
    PropRecord *p;
    f32 dx, dz, dist, want, err;

    if (state == -2)
    {
        const char *e = getenv("GE_GOTO");
        state = -1;
        if (e != NULL && e[0] != '\0' && sscanf(e, "%f,%f,%f", &tx, &ty, &tz) == 3)
        {
            state = 0;
        }
    }
    if (state != 0)
    {
        return;
    }

    p = getCurrentPlayerProp();
    if (p == NULL)
    {
        return;
    }

    dx = tx - p->pos.x;
    dz = tz - p->pos.z;
    dist = sqrtf(dx * dx + dz * dz);

    if (dist < 50.0f)
    {
        state = 1;
        g_PortGotoActive = 0;
        g_PortGotoStickX = 0;
        g_PortGotoStickY = 0;
        fprintf(stderr, "ge: goto ARRIVED t=%d wall=%.0f (%.0f %.0f %.0f) dist=%.1f\n",
                g_GlobalTimer, (f32) osGetCount() / 46875.0f,
                p->pos.x, p->pos.y, p->pos.z, dist);
        fflush(stderr);
        return;
    }

    /* same angle convention as the spawn look: theta_deg = (TAU - atan2(x,z)) * 360/TAU */
    want = 360.0f - (atan2f(dx, dz) * 360.0f / 6.2831855f);
    err = want - g_CurrentPlayer->vv_theta;
    while (err > 180.0f)  { err -= 360.0f; }
    while (err < -180.0f) { err += 360.0f; }

    g_PortGotoStickX = (s32) (err * 2.0f);
    if (g_PortGotoStickX > 60)  { g_PortGotoStickX = 60;  }
    if (g_PortGotoStickX < -60) { g_PortGotoStickX = -60; }
    g_PortGotoStickY = (err < 30.0f && err > -30.0f) ? 70 : 0;
    g_PortGotoActive = 1;

    if ((dbgtick++ % 60) == 0)
    {
        fprintf(stderr, "ge: goto dist=%.0f err=%.0f pos=(%.0f %.0f)\n",
                dist, err, p->pos.x, p->pos.z);
        fflush(stderr);
    }
}
#endif

void lvlStageLoad(s32 stage)
{
    s32 i;
    struct player_data *player_data;

    g_CurrentStageToLoad = stage;

#ifndef TARGET_N64
    g_PortCheatCountdown = 60; /* apply GE_CHEATS ~1s into the mission */
    g_PortSpawnWepCountdown = 120; /* GE_SPAWNWEAPON drops ~2s in */
    portDebugPosApply();
    portCheatApplyLoadTimeSafe(); /* GE_CHEATS=rockets must be live before
                                   * proplvreset2/weaponAssignToHome below */
#endif

    // this if block pushes where g_CurrentStageToLoad gets loaded to the
    // top of the method. Maybe a debug log about which level is loaded.
    if(0)
    {
        #ifdef DEBUG
        // removed
        #endif
    }

    D_800483C0 = 1;
    g_ControlsLockedFlag = 0;
    g_ClockTimer = 1;

#ifdef VERSION_US
    g_GlobalTimerDelta = 1.0f;
#endif
#if defined(VERSION_JP) || defined(VERSION_EU)
    g_JP_GlobalTimerDelta = 1.0f;
#endif

    g_GlobalTickCount = 0;
    g_GlobalTimer = 0;

#if defined(VERSION_JP)
    g_GlobalTimerDelta = 1.f;
#endif
#if defined(VERSION_EU)
    g_GlobalTimerDelta = 1.20000004768f;
#endif

    D_80048388 = 0;
    D_8004838C = 0;
    D_80048390 = 0;
    D_80048394 = 0;
    g_CurrentMultiPlayerSec = 0.0f;
    D_800483B0 = 0;
    g_StageTimeSec = 0.0f;
    g_MpSoundStateRelated = 0;

    sndSetScalerApplyVolumeAllSfxSlot(1.0f);
    musicTrack1ApplySeqpVol(VOLUME_MAX);
    musicTrack2ApplySeqpVol(VOLUME_MAX);
    musicTrack3ApplySeqpVol(VOLUME_MAX);
    sub_GAME_7F0C1364();
    modelmgrSetLevelResetting(TRUE);
    set_mt_tex_alloc();
#ifdef VERSION_EU
    bullet_moving_sparks_reset();
#else
    bullet_sparks_reset_all();
#endif
    texReset();
    load_font_tables();

    /* If title screen, initialize screen and folder setup.
    * Otherwise:
    * - enable cheats for player
    * - init watch
    * - reset some player values, like view height
    * - reset multiplayer stats
    */
    if (stage == LEVELID_TITLE)
    {
#ifdef DEBUG
        fogLoadLevelEnvironment(bossGetStageNum(), 0);
#endif
        init_menus_or_reset();
    }
    else
    {
        g_NewCheatUnlocked = 0;

        if ((g_CurrentStageToLoad != LEVELID_TITLE) && (D_80048394 == 0) && (g_ClockTimer > 0))
        {
            if (g_AppendCheatSinglePlayer != 0)
            {
                s32 s0 = 1;

                for (s0 = 1; s0 != CHEAT_INVALID; s0++)
                {
                    if (g_CheatActivated[s0] && cheatIsEnemyRockets(s0))
                    {
                        cheatButtonTurnOnCheatForPlayers(s0);
                    }
#ifdef DEBUG
                    else
                    {
                        cheatButtonHandleCheatsTurnedOff(s0);
                    }
#endif
                }
            }
        }

        load_bg_file(g_CurrentStageToLoad);
        skySetStageNum(g_CurrentStageToLoad);

        init_watch_at_start_of_stage();
#ifndef TARGET_N64
        {   /* MP stats: this stage's match end may record (port_mpstats.c) */
            extern void portMpStatsStageStart(void);
            portMpStatsStageStart();
        }
        {   /* Random / Random Set weapons: this match's roll, before proplvreset2 places the pads */
            extern void portMpWeaponRoll(void);
            portMpWeaponRoll();
        }
#endif

        sub_GAME_7F0C11FC(stage);

        for (i=0; i<4; i++)
        {
            s32 s3;
            player_data = (struct player_data *)&g_playerPlayerData[i];

            if (getPlayerCount() == 1)
            {
                // s4 variable
                player_data->autoaim = 0;
                player_data->sight = 0;
                player_data->handicap = 1.0f;
                player_data->player_perspective_height = 1.0f;
            }
            else
            {

                // why is this looping from g_playerPlayerData again, this inner block
                // gets executed 16 times in multiplayer.
                for (s3 = 0; s3 < 4; s3++)
                {
#ifndef TARGET_N64
                    extern s32 portMpLtkHealthActive(void); /* video.c: Multiplayer Options LTK Health */
                    if (get_scenario() == SCENARIO_LTK || portMpLtkHealthActive())
#else
                    if (get_scenario() == SCENARIO_LTK)
#endif
                    {
                        g_playerPlayerData[s3].handicap = 200.0f;
                    }
                    else
                    {
                        g_playerPlayerData[s3].handicap = get_player_mp_handicap(s3);
                    }

                    g_playerPlayerData[s3].player_perspective_height = get_player_mp_char_height(s3);
                }

                lvlSetMpTime(get_mp_timelimit());
                lvlSetMpPoint(get_mp_pointlimit());
                copy_aim_settings_to_playerdata();
            }

            // g_playerPlayerData s4 variable
            player_data->time_other_players_on_screen = 0;
            player_data->damage_to_backside = 0;
            player_data->min_time_between_kills = S32_MAX;
            player_data->max_time_between_kills = 0;
            player_data->most_killed_one_life = 0;
            player_data->most_killed_one_time = 0;
            player_data->longest_inning = 0;
            player_data->shortest_inning = S32_MAX;
            player_data->order_out_in_yolt = 0;
            player_data->flag_counter = 0;
            player_data->distance_traveled = 0.0f; // one kind of float zero
            player_data->body_armor_pickups = 0.f; // a different kind of float zero

            // g_playerPlayerData s2, different than above
            for (s3 = 0; s3 < 4; s3++)
            {
                player_data->kill_counts[s3] = 0;
            }
        }
    }

    something_with_stage_objectives();
    mpwatchUnpauseGame();
    sub_GAME_7F09B820();
    initModelHitEntryFreeList();
    modelmgrResetSlotCounts();
    init_load_objpos_table();
    reinit_between_menus();
    init_sound_effects_registers();
    init_guards();
    bodiesReset(stage);
    proplvreset2(stage);
    alloc_explosion_smoke_casing_scorch_impact_buffers();
    alloc_shattered_window_pieces();
    sub_GAME_7F007290();
    initCheatTextBuffer();

    if (g_CurrentStageToLoad == LEVELID_TITLE)
    {
        disableOnscreenCheatText();
    }
    else
    {
        s32 s0;

        init_path_table_links();
        init_ejected_cartridges();

        for (s0 = 0; s0 < getPlayerCount(); s0++)
        {
            set_cur_player(s0);
            reinit_gunheld_totaltime();
            init_player_BONDdata_stats();
            init_player_BONDdata();
            bondviewLoadSetupIntroSection();
            bondviewPlayerBeginLife();
            sets_a_bunch_of_BONDdata_values_to_default();
            disableOnscreenCheatText();
        }

        set_cur_player(0);
    }

    /**
     * Leave stage load allocation mode.
     * From this point on, model creation should try to reuse existing slots.
     */
    modelmgrSetLevelResetting(FALSE);

    zbufDeallocate();
    viSetVideoMode(MD_NORMAL);
    D_80048368 = 1.0f;
    lvlSetControlsLockedFlag(0);
#ifndef TARGET_N64
    { /* the model-part cache's keys are memp addresses this load reused (roc_hooks.c) */
        extern void portPartCacheStageLoaded(s32 stage);
        extern void portHwLoadStageLoaded(void);
        extern void portVehicleCarryReset(void);
        portPartCacheStageLoaded(stage);
        portVehicleCarryReset(); /* vehicle rounding-carry slots keyed by setup address (propobj.c) */
        portHwLoadStageLoaded(); /* the stage's own loads are the load time, not a hitch (boss.c holds it) */
    }
#endif
}



s32 lvlGetCurrentStageToLoad(void)
{
    return g_CurrentStageToLoad;
}


 /**
 * Address: 7F0BDF10
 *
 * Debug method. Something to do with portals. Button press
 * on controller 1 and 2 are used for control flow.
 */
Gfx *lvlPortalDebug7F0BDF10(Gfx *gdl)
{
    s32 temp_v1;
    bool portalDebugStateChanged; // This value has no observable use in the final assembly but the variable definitely existed in the orignal code.
    s32 *selectedPortal = &g_DebugPortalIndex;

    portalDebugStateChanged = FALSE;

    if (gdl)
    {
        gdl = bgDebugRemoved7F0B9DE4(gdl, g_DebugPortalIndex, -1);

        g_DebugPortalsInputBuffer = g_DebugPortalsInputBufferSource1;
        g_DebugPortalsInputBuffer1 = g_DebugPortalsInputBufferSource2;
        temp_v1 = g_DebugPortalsInputBufferSource4;
        g_DebugPortalsInputBuffer2 = g_DebugPortalsInputBufferSource3;
        g_DebugPortalsInputBuffer3 = temp_v1;

        temp_v1 = joyGetButtons(PLAYER_1, A_BUTTON) | joyGetButtons(PLAYER_2, A_BUTTON);

        if ((temp_v1 == g_DebugPortalsInputBuffer3) == FALSE)
        {
            D_800483C0 ^= 1;
        }

        if ((g_DebugPortalsInputBuffer1 == g_DebugPortalsInputBuffer) == FALSE)
        {
            D_800483C0 ^= 1;
        }

        g_DebugPortalsInputBuffer4 = temp_v1;

        bgRemoved7F0B9DF4(temp_v1 ? g_DebugPortalIndex : -1);

        return gdl;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD) |
        joyGetButtonsPressedThisFrame(PLAYER_2, L_JPAD))
    {
        g_DebugPortalIndex = *selectedPortal - 1;

        if (portalDebugStateChanged = TRUE, *selectedPortal < 0)
        {
            g_DebugPortalIndex = 0;
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD) |
        joyGetButtonsPressedThisFrame(PLAYER_2, R_JPAD))
    {
        g_DebugPortalIndex = *selectedPortal + 1;
        portalDebugStateChanged = TRUE;
    }

    if ((joyGetButtons(PLAYER_1, R_TRIG) | joyGetButtons(PLAYER_2, R_TRIG)) &&
        (joyGetButtons(PLAYER_1, L_TRIG) | joyGetButtons(PLAYER_2, L_TRIG)))
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD))
        {
            bgSwapConnectedRooms(*selectedPortal);
        }
    }
    else if (joyGetButtons(PLAYER_1, R_TRIG) | joyGetButtons(PLAYER_2, R_TRIG))
    {
        if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, D_JPAD)) &&
            (bgGetDataPortalsControlBytes1Bit1(g_DebugPortalIndex) == 0))
        {
            bgToggleDataPortalsContrlBytes1Bit1(g_DebugPortalIndex, 0);
            portalDebugStateChanged = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, U_JPAD)) &&
            (bgGetDataPortalsControlBytes1Bit1(g_DebugPortalIndex) != 0))
        {
            bgToggleDataPortalsContrlBytes1Bit1(g_DebugPortalIndex, 1);
            portalDebugStateChanged = TRUE;
        }
    }
    else if (joyGetButtons(PLAYER_1, L_TRIG) | joyGetButtons(PLAYER_2, L_TRIG))
    {
        if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, D_JPAD)) &&
            bgGetDataPortalsControlBytes1Bit2(g_DebugPortalIndex))
        {
            bgClearDataPortalsControlBytes1Low2Bits(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, U_JPAD)) &&
            (bgGetDataPortalsControlBytes1Bit2(g_DebugPortalIndex) == 0))
        {
            bgSetDataPortalsControlBytes1Bit2(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }
    }
    else
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD) |
            joyGetButtonsPressedThisFrame(PLAYER_2, D_JPAD))
        {
            sub_GAME_7F0B9A7C(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }

        if (joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD) |
            joyGetButtonsPressedThisFrame(PLAYER_2, U_JPAD))
        {
            sub_GAME_7F0B9A2C(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }
    }

    if (portalDebugStateChanged)
    {
        // Removed
    }

    return NULL;
}


/**
 * Graphics render method.
 * Also sets player max ammo if infinite ammo cheat is enabled.
 *
 * Address 0x7F0BE30C (VERSION_US).
 */

Gfx* lvlRender(Gfx* DL)
{
    gSPSegment(DL++, SPSEGMENT_PHYSICAL, NULL);
    gSPSegment(DL++, SPSEGMENT_UNKNOWN, osVirtualToPhysical(ptr_font_DL));

    gSPDisplayList(DL++, &dlFastPipelineSetup);
    gSPDisplayList(DL++, &dlZBufferGeometry);

    if (g_CurrentStageToLoad == LEVELID_TITLE)
    {
        DL = viClearZBufCurrentPlayer(DL);
        DL = viSetupCurrentPlayerView(DL);
        gDPSetScissor(DL++, G_SC_NON_INTERLACE, 0, 0, (s16)viGetX(), (s16)viGetY());
        DL = menu_jump_constructor_handler(DL);
#ifndef TARGET_N64
        /* The cl_audio_pump block, in the menus too. It used to be drawn only
         * from maybe_mp_interface, which the front end never reaches, so
         * `cl_audio_pump 1` showed nothing until a stage had loaded. Last in the
         * frame, as in a level, so no menu panel can cover it. */
        DL = portDrawAudioPumpOverlay(DL);
#endif
    }
    else
    {
        s32 i;
        s32 pcount;

        pcount = getPlayerCount();

        gSPClipRatio(DL++, FRUSTRATIO_2);

        for(i = 0; i < pcount; i++)
        {
            set_cur_player(get_nth_player_from_shuffled(i));

            viSetViewSize(g_CurrentPlayer->viewx, g_CurrentPlayer->viewy);
            viSetViewPosition(g_CurrentPlayer->viewleft, g_CurrentPlayer->viewtop);
#ifndef TARGET_N64
            /* the vanilla height of this view: 220 solo, 109 in a split (whose
             * views are 119 under full height - same compensation, same reason) */
            if (g_CurrentPlayer->viewy > ((getPlayerCount() == 1) ? VIEWPORT_HEIGHT_DEFAULT : VIEWPORT_HEIGHT_4P))
            {
                const f32 vanillah = (f32) ((getPlayerCount() == 1) ? VIEWPORT_HEIGHT_DEFAULT : VIEWPORT_HEIGHT_4P);
                /* Full height (bondview2.c portFullHeightActive) makes the view
                 * 240 lines instead of 220, so widen fovy in tan space by
                 * 240/220 to keep the horizontal FOV vanilla. The math
                 * functions are prototyped explicitly because IDO's math.h
                 * declares none under GCC. */
                extern float tanf(float);
                extern float atanf(float);
                f32 half = g_CurrentPlayer->fovy * (M_PI_F / 360.0f);
                /* any height above 220 (full height 240, or the pause-box ease between) */
                viSetFovY(atanf(tanf(half) * ((f32)g_CurrentPlayer->viewy / vanillah)) * (360.0f / M_PI_F));
            }
            else
#endif
            viSetFovY(g_CurrentPlayer->fovy);
            viSetAspect(g_CurrentPlayer->aspect);

            DL = viClearZBufCurrentPlayer(DL);
            DL = viSetupCurrentPlayerView(DL);

            if (get_debug_render_raster() == DEB_MOVE_VIEW)
            {
                DL = sub_GAME_7F091580(DL);
            }

            if (get_debug_render_raster() == DEB_STAN_VIEW)
            {
                DL = stanRenderDebugStanView(DL);
            }

            if (get_debug_render_raster() == DEB_BOND_VIEW)
            {
                DL = bondviewRenderDebugBondView(DL);
            }

            DL = viSetupScreensForNumPlayers(DL);
            DL = skyRender(DL);
            bgRoomVisibilityRelated();
#ifndef TARGET_N64
            { /* Random Spawns (bondview.c, safest-spawn picker): record which start
               * pads this player can see, refreshed while the data is fresh */
                extern void portStartpadVisibilityUpdate(void);
                portStartpadVisibilityUpdate();
            }
#endif
            propsTick();
            chraiUpdateOnscreenPropCount();
            chrpropUpdateAutoaimTarget();
            chraiCheckUseHeldItems();

            if (bond_pressed_reload_activate() && bond_interact_object())
            {
#ifndef TARGET_N64
                extern s32 portJoyPortUseNoReload(void);
                if (!portJoyPortUseNoReload()) /* Port use key: no reload fallback */
#endif
                {
                    attempt_reload_item_in_hand(GUNRIGHT);
                    attempt_reload_item_in_hand(GUNLEFT);
                }
            }
#ifndef TARGET_N64
            { /* Port suite (joy_port.c): reload / prev+next inventory */
                extern void portJoyPortPlayerTick(void);
                portJoyPortPlayerTick();
            }
#endif

            propsTickPlayer();
            DL = bgLevelRender(DL);

            if (get_debug_portal_flag())
            {
                DL = lvlPortalDebug7F0BDF10(DL);
            }

            if (get_debug_stan_problems_flag())
            {
                DL = sub_GAME_7F0B303C(DL);
            }

            if (get_debug_stanhit_flag())
            {
                DL = sub_GAME_7F0B3034(DL);
                DL = write_stan_tiles_in_yellow(DL);
            }

            if (get_debug_stanregion_flag())
            {
                DL = sub_GAME_7F0B3034(DL);
                DL = sub_GAME_7F0B312C(DL, -0x7FC0);
            }

            if (tokenFind(1, "-stanshow_"))
            {
                StandTilePoint *tile1 = stanMatchTileName(tokenFind(1, "-stanshow_"));
                if (tile1)
                {
                    DL = sub_GAME_7F0B3034(DL);
                    DL = sub_GAME_7F0B3024(DL, tile1, 0xFF0000FF);
                }
            }

            if (tokenFind(2, "-stanshow_"))
            {
                StandTilePoint *tile2 = stanMatchTileName(tokenFind(2, "-stanshow_"));
                if (tile2)
                {
                    DL = sub_GAME_7F0B3034(DL);
                    DL = sub_GAME_7F0B3024(DL, tile2, 0xFF00FF);
                }
            }

            if (tokenFind(3, "-stanshow_"))
            {
                StandTilePoint *tile3 = stanMatchTileName(tokenFind(3, "-stanshow_"));
                if (tile3)
                {
                    DL = sub_GAME_7F0B3034(DL);
                    DL = sub_GAME_7F0B3024(DL, tile3, 0xFFFF);
                }
            }

            setanimationdebugflag(getDebugMode() == DEB_SELANIM);
            DL = weaponRenderTracers(DL);

#if defined(VERSION_EU)
            bullet_moving_sparks_update(&DL, ZBUF_SURFACE);
#else /* VERSION_US, VERSION_JP, unspecified */
            bullet_sparks_render_all(&DL, ZBUF_SURFACE);
#endif
            DL = glassRenderShards(DL);
            DL = explosionRenderFlyingParticles(DL);

#ifndef TARGET_N64
            portCheatTick(); /* GE_CHEATS one-shot, player context live here */
            portDualZmgTick();
            portSpawnWeaponTick(); /* GE_SPAWNWEAPON pickup drops */
            portSpawnWepTrace();
            portGotoTick();  /* GE_GOTO test-man steering */
#endif

            if (

#if defined(BUGFIX_R1)
                cheatIsActive(CHEAT_INFINITE_AMMO) != 0
                && (
                    (getCurrentPlayerWeaponId(GUNRIGHT) != ITEM_WATCHLASER)
                    || (g_CurrentPlayer->trigger_down == 0)
                )
#else /* VERSION_US */
                cheatIsActive(CHEAT_INFINITE_AMMO) != 0
#endif
                )
            {
                set_max_ammo_for_cur_player();
            }

#ifndef TARGET_N64
            /* Marker (0x47 = G_DXLU_WORLDEND_EXT): the world is done, so the
             * renderer flushes its sorted transparent triangles here, before
             * the gun. The gun depth-tests, so glass flushed after it would
             * paint over it. */
            DL->words.w0 = (u32)0x47 << 24;
            DL->words.w1 = 0;
            DL++;
#endif
            if (get_debug_render_raster() == DEB_BOND_VIEW)
            {
                DL = maybe_mp_interface(DL);
            }
            else
            {
                DL = bondviewRemoved7F08BCB8(DL);
            }

            DL = mp_watch_menu_display(DL);
        }
    }

    gDPSetScissor(DL++, G_SC_NON_INTERLACE, 0, 0, viGetX(), viGetY());

    return DL;
}


/**
 * Sets the modifier values for the level being loaded.
 * This covers the enemy accuracy, reaction speed, and similar values.
 *
 * address 0x7F0BE8D0
 */
void lvlSetMultipliersForDifficulty(void)
{
    if (g_SelectedDifficulty == DIFFICULTY_AGENT)
    {
        f32 armorDiff = currentPlayerGetHealth() + currentPlayerGetArmor();
        f32 damageMultiplier = 1.0f;

        if (armorDiff <= 0.125f)
        {
            damageMultiplier = 0.5f;
        }
        else if (armorDiff <= 0.6f)
        {
            damageMultiplier = (((armorDiff - 0.125f) * 0.5f) / 0.47500002f) + 0.5f;
        }

        F_80030B14 = 2.0f;
        F_80030B18 = 2.0f;
        g_AutogunPendingDamageTick = (0.5f * damageMultiplier);
        g_AutogunDamageScalar = (0.5f * damageMultiplier);
        F_80030B24 = 2.0f;
        g_AiAccuracyModifier = DEFAULT_AGENT_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = (DEFAULT_AGENT_AI_DAMAGE_MODIFIER * damageMultiplier);
        g_AiHealthModifier = 2.0f;
        g_SpExplosionDamageMult = (f32) (0.25f * damageMultiplier);
        difficulty = 1.5f;
        g_SoloAmmoMultiplier = DEFAULT_AGENT_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_AGENT_AI_REACTION_SPEED;
    }
    else if (g_SelectedDifficulty == DIFFICULTY_SECRET)
    {
        F_80030B14 = 1.0f;
        F_80030B18 = 1.0f;
        g_AutogunPendingDamageTick = 0.75f;
        g_AutogunDamageScalar = 0.75f;
        F_80030B24 = 1.0f;
        g_AiAccuracyModifier = DEFAULT_SECRET_AGENT_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = DEFAULT_SECRET_AGENT_AI_DAMAGE_MODIFIER;
        g_AiHealthModifier = 1.0f;
        g_SpExplosionDamageMult = 0.75f;

#if defined(BUGFIX_R1)
        if (j_text_trigger)
        {
            difficulty = 1.1f;
        }
        else
        {
            difficulty = 0.75f;
        }
#else
        // VERSION_US
        difficulty = 0.75f;
#endif

        g_SoloAmmoMultiplier = DEFAULT_SECRET_AGENT_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_SECRET_AGENT_AI_REACTION_SPEED;
    }
    else if (g_SelectedDifficulty == DIFFICULTY_00)
    {
        F_80030B14 = 1.0f;
        F_80030B18 = 1.0f;
        g_AutogunPendingDamageTick = 1.0f;
        g_AutogunDamageScalar = 1.0f;
        F_80030B24 = 1.0f;
        g_AiAccuracyModifier = DEFAULT_00_AGENT_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = DEFAULT_00_AGENT_AI_DAMAGE_MODIFIER;
        g_AiHealthModifier = 1.0f;
        g_SpExplosionDamageMult = 1.0f;

#if defined(BUGFIX_R1)
        if (j_text_trigger)
        {
            difficulty = 0.75f;
        }
        else
        {
            difficulty = 0.2f;
        }
#else
        // VERSION_US
        difficulty = 0.2f;
#endif

        g_SoloAmmoMultiplier = DEFAULT_00_AGENT_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_00_AGENT_AI_REACTION_SPEED;
    }
    else if (g_SelectedDifficulty == DIFFICULTY_007)
    {
        F_80030B14 = 1.0f;
        F_80030B18 = 1.0f;
        g_AutogunPendingDamageTick = 1.0f;
        g_AutogunDamageScalar = 1.0f;
        F_80030B24 = 1.0f;
        g_AiAccuracyModifier = DEFAULT_007_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = DEFAULT_007_AI_DAMAGE_MODIFIER;
        g_AiHealthModifier = 1.0f;
        g_SpExplosionDamageMult = 1.0f;
        difficulty = 1.0f;
        g_SoloAmmoMultiplier = DEFAULT_007_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_007_AI_REACTION_SPEED;
    }
}


/**
 * Address: 0x7F0BEB88 (NTSC).
 * Address: 0x7F0BF7AC (NTSC-J).
 * Address: 0x7F0BDFAC (PAL).
 *
 * Per-frame game tick called once per frame from the main loop in boss.c
 * Advances the clock variables the game runs on: g_ClockTimer, g_GlobalTimerDelta, g_GlobalTimer, and g_GlobalTickCount.
 * Updates characters, sky, bullet casings, bullet sparks, explosion debris.
 * Handles multiplayer states (timers, end of match warning alarm, and concluding the match)
 * Also a fair amount of leftover debug code.
 */
void lvlTick(void)
{
#ifndef TARGET_N64
    s32 portRunning = 1;    /* GE_TICKHZ: clock running (not paused/locked) */
#endif

    tlbmanageResetCurrentEntriesCount();

#ifndef TARGET_N64
    /* GE_TICKHZ: a per-TICK serial (sub-ticks included, paused included).
     * g_GlobalTimer only moves on field ticks, so "did this already run
     * this tick" stamps compared against it admit every sub-tick. */
    g_PortTickSerial++;
#endif

    if (g_ControlsLockedFlag != 0)
    {
        g_ClockTimer = 0;
#ifndef TARGET_N64
        portRunning = 0;
#endif
    }
    else if (checkGamePaused() != 0)
    {
        g_ClockTimer = 0;
#ifndef TARGET_N64
        portRunning = 0;
#endif
    }
    else
    {
        g_ClockTimer = speedgraphframes;
#ifndef TARGET_N64
        /* GE_TICKHZ: g_GlobalTickCount must count 60Hz fields, not ticks,
         * because half- and third-rate work (chraction.c `& 1`, bondview2.c `%
         * 3`) keys off it. Always true at GE_TICKHZ=60. */
        if (g_ClockTimer > 0)
#endif
        {
            g_GlobalTickCount += 1;
#ifndef TARGET_N64
            { /* deliver positional volumes deferred on sub-ticks (see
               * sndCreatePostEvent) now that a field tick arrived */
                extern void sndFlushDeferredVolumes(void);
                sndFlushDeferredVolumes();
            }
            { /* Re-pan live sounds as the listener turns. Run after the
               * flush so these posts are newest, and once per field so a
               * higher tick rate doesn't multiply the traffic. */
                portSndPanTick();
            }
#endif
        }
    }

#ifndef TARGET_N64
    /* GE_TICKHZ: g_ClockTimer stays the INTEGER field step (so every
     * discrete subsystem keeps vanilla 60Hz cadence via its existing
     * `> 0` guards); the float delta carries the true fractional step so
     * continuous motion integrates at the raised tick rate. */
#define PORT_TICK_DELTAF (portRunning ? portTickDelta : 0.0f)
#else
#define PORT_TICK_DELTAF (f32) g_ClockTimer     /* expands byte-identical to vanilla */
#endif

#ifdef VERSION_US
    g_GlobalTimerDelta = PORT_TICK_DELTAF;
#else
    g_JP_GlobalTimerDelta = PORT_TICK_DELTAF;
#ifdef VERSION_EU
    g_GlobalTimerDelta = g_JP_GlobalTimerDelta * 1.2f;
#else
    g_GlobalTimerDelta = g_JP_GlobalTimerDelta;
#endif
#endif

#undef PORT_TICK_DELTAF

    g_GlobalTimer += g_ClockTimer;
#ifndef TARGET_N64
    /* The stat pass's f/s counter: the fields the SIMULATION was actually
     * credited with. Advanced HERE, not in updateFrameCounters, because
     * g_ClockTimer is the number lvlTick decided to apply - it is zeroed by the
     * pause and controls-lock checks above, and those fields really are
     * discarded. Counting where they are produced instead made f/s read a
     * healthy 60.0 during every pause. This way g_PortFieldsTicked and
     * g_GlobalTimer are the same sum by construction.
     * Relaxed atomic: the stat pass and the hitch log read it from the render
     * thread when render_thread=1. */
    if (g_ClockTimer > 0)
    {
        extern u32 g_PortFieldsTicked;
        __atomic_fetch_add(&g_PortFieldsTicked, (u32) g_ClockTimer, __ATOMIC_RELAXED);
    }
#endif

    if ((g_CurrentStageToLoad != LEVELID_TITLE) && (D_80048394 == 0) && (g_ClockTimer > 0))
    {
#ifndef TARGET_N64
        /* Multiplayer has its own cheats: with 2+ players these six come from
         * the Multiplayer Options sheet (video.c portMpCheatSetting), not the
         * solo cheat menu, and solo never reads the sheet. This pass runs even
         * when no solo cheat is on; stage end still clears them all
         * (cheatDisableAllCheats). */
        if (g_AppendCheatSinglePlayer != 0 || getPlayerCount() >= 2)
        {
            extern s32 portMpCheatSetting(s32 which);
            extern s32 portTrueEnemyRocketsEnabled(void);
            static const s32 mpOwn[7] = { CHEAT_INVISIBILITY, CHEAT_DK_MODE, CHEAT_TINY_BOND,
                                          CHEAT_PAINTBALL, CHEAT_NO_RADAR_MP, CHEAT_TURBO_MODE,
                                          CHEAT_INFINITE_AMMO };
            s32 i;

            for (i = 1; i != CHEAT_INVALID; i++)
            {
                s32 on = g_CheatActivated[i] && (!cheatIsEnemyRockets(i) || portTrueEnemyRocketsEnabled());

                if (getPlayerCount() >= 2)
                {
                    s32 w;

                    for (w = 0; w < 7; w++)
                    {
                        if (mpOwn[w] == i)
                        {
                            on = portMpCheatSetting(w);
                            break;
                        }
                    }
                }
                if (on)
                {
                    extern s32 g_PortHudMsgQuiet; /* bondview2.c hudmsgBottomShow */

                    g_PortHudMsgQuiet = getPlayerCount() >= 2;   /* no "PAINTBALL MODE ON" popup at an MP start */
                    cheatButtonTurnOnCheatForPlayers(i);
                    g_PortHudMsgQuiet = 0;
                }
            }
            if (getPlayerCount() >= 2)
            {   /* log which are on, once per MP stage, so they show up
                 * in a user's log */
                extern int printf(const char *, ...);
                static const char *const mpName[7] = { "Invisibility", "DK Mode", "Tiny Players",
                                                       "Paintball Mode", "No Radar", "Turbo Mode",
                                                       "Unlimited Ammo" };
                s32 w;
                s32 any = 0;

                printf("ge: mp: cheats from Multiplayer Options:");
                for (w = 0; w < 7; w++)
                {
                    if (portMpCheatSetting(w))
                    {
                        printf("%s %s", any ? "," : "", mpName[w]);
                        any = 1;
                    }
                }
                printf("%s\n", any ? "" : " none");
            }
        }
#else
        if (g_AppendCheatSinglePlayer != 0)
        {
            s32 i;
            for (i = 1; i != CHEAT_INVALID; i++)
            {
#ifndef TARGET_N64
                extern s32 portTrueEnemyRocketsEnabled(void);
                if (g_CheatActivated[i] && (!cheatIsEnemyRockets(i) || portTrueEnemyRocketsEnabled()))
#else
                if (g_CheatActivated[i] && !cheatIsEnemyRockets(i))
#endif
                {
                    cheatButtonTurnOnCheatForPlayers(i);
                }
            }
        }
#endif
    }

    if ((getPlayerCount() >= 2) && (g_CurrentStageToLoad != LEVELID_TITLE))
    {
        if (get_mission_state() == MISSION_STATE_6)
        {
            s32 i;
            s32 mp_alive_count;
            s32 mp_player_field424_count;

            mp_alive_count = 0;
            mp_player_field424_count = 0;

            for (i=0; i<getPlayerCount(); i++)
            {
                if (g_playerPointers[i]->bonddead != FALSE)
                {
                    mp_alive_count++;
                    if (g_playerPointers[i]->redbloodfinished)
                    {
                        mp_player_field424_count++;
                    }
                }
            }

            if ((mp_alive_count > 0) && (mp_alive_count == mp_player_field424_count))
            {
                set_missionstate(MISSION_STATE_1);
            }
        }

        if (g_MpTime > 0)
        {
            s32 current_time;
            s32 sp180;
            s32 i;
            current_time = D_80048394;
            sp180 = g_ClockTimer + D_80048394;

#ifdef VERSION_EU
            if ((D_80048394 < (g_MpTime - 0xBB8)) && (sp180 >= (g_MpTime - 0xBB8)))
#else
            if ((D_80048394 < (g_MpTime - 0xE10)) && (sp180 >= (g_MpTime - 0xE10)))
#endif
            {
                for (i = 0; i < getPlayerCount(); i++)
                {
                    set_cur_player(i);
#ifdef VERSION_US
                    HUDMESSAGEBOTTOM("One minute left");
#else
                    HUDMESSAGEBOTTOM(langGet(0xB044));
#endif
                }
            }

            // sound alarm when game is about to end (10 seconds before end)
#ifdef VERSION_EU
            if ((sp180 >= (g_MpTime - 0x1F4)) && (g_MpSoundStateRelated == 0) && (lvlGetControlsLockedFlag() == 0))
#else
            if ((sp180 >= (g_MpTime - 0x258)) && (g_MpSoundStateRelated == 0) && (lvlGetControlsLockedFlag() == 0))
#endif
            {
#line 886
                sndPlaySfx(g_musicSfxBufferPtr, ALARM1_SFX, &g_MpSoundStateRelated);
            }

            // stop alarm
            if (lvlGetControlsLockedFlag() != 0)
            {
                if ((g_MpSoundStateRelated != NULL) && (sndGetPlayingState(g_MpSoundStateRelated) != 0))
                {
                    sndDeactivate(g_MpSoundStateRelated);
                }
            }

            if ((current_time < g_MpTime) && (sp180 >= g_MpTime))
            {
                mpCalculateAwards(FALSE);
            }
        }

        // when playing with a kill limit, g_MpPoint is not zero
        if ((g_MpPoint > 0) && (g_ClockTimer != 0))
        {
            s32 var_player_count1;
            s32 i;
            s32 mp_player_currently_in_dying_animation;
            s32 mp_players_over_point_limit;

            var_player_count1 = getPlayerCount();
            mp_player_currently_in_dying_animation = 0;
            mp_players_over_point_limit = 0;

            for (i = 0; i < var_player_count1; i++)
            {
                if (g_playerPointers[i]->bonddead != FALSE &&
                    (g_playerPointers[i]->redbloodfinished == FALSE || g_playerPointers[i]->deathanimfinished == FALSE || g_playerPointers[i]->colourfadetimemax60 >= 0.0f))
                {
                    mp_player_currently_in_dying_animation++;
                }

                if (get_points_for_mp_player(i) >= g_MpPoint)
                {
                    // counts players over kill limit
                    mp_players_over_point_limit++;
                }
            }

            if (mp_players_over_point_limit > 0)
            {
                if (mp_player_currently_in_dying_animation == 0)
                {
                    // end game after dying players are finished dying
                    mpCalculateAwards(FALSE);
                }
                else
                {
                    // this will cause the game to freeze players, to stop them from moving once game ended
                    mpwatchSetStopPlayFlag();
                }
            }
        }


        // YOLT scenario: end-of-game tracking.
#ifndef TARGET_N64
        /* PORT: the same tracking for the Limits screen's Lives in any
         * scenario (front.c portMpLives: YOLT is 2). In a team scenario the
         * match ends when one team is left. */
        if ((portMpLives() > 0) && (g_ClockTimer != 0))
#else
        if ((get_scenario() == SCENARIO_YOLT) && (g_ClockTimer != 0))
#endif
        {
            s32 player_count;
            s32 killed_count;
            s32 not_dead_count;
            s32 fully_dead_total;
            s32 killed_total;
            s32 i;
            s32 j;

            player_count = getPlayerCount();
            killed_total = 0;
            fully_dead_total = 0;

            for (i = 0; i < player_count; i++)
            {
                killed_count = 0;
                not_dead_count = 0;

                for (j = 0; j < player_count; j++)
                {
                    if (g_playerPointers[j]->bonddead == 0)
                    {
                        not_dead_count++;
                    }
                    killed_count += g_playerPlayerData[j].kill_counts[i];
                }

#ifndef TARGET_N64
                if (killed_count >= portMpLives())
#else
                if (killed_count >= 2)
#endif
                {
                    if (g_playerPlayerData[i].order_out_in_yolt == 0)
                    {
                        g_playerPlayerData[i].order_out_in_yolt = (u8) (not_dead_count + 1);
                    }

                    killed_total++;

                    if (g_playerPointers[i]->redbloodfinished
                        && g_playerPointers[i]->deathanimfinished
                        && g_playerPointers[i]->colourfadetimemax60 < 0.0f)
                    {
                        fully_dead_total++;
                    }
                }
            }

#ifndef TARGET_N64
            if (portMpTeamsOn())
            {   /* teams: over once a single team has anyone with lives left */
                s32 teams_left = 0;
                s32 all_fully_dead = TRUE;
                s32 t;

                for (t = 0; t < 2; t++)
                {
                    for (i = 0; i < player_count; i++)
                    {
                        if (portMpTeamOf(i) == t && g_playerPlayerData[i].order_out_in_yolt == 0)
                        {
                            teams_left++;
                            break;
                        }
                    }
                }
                for (i = 0; i < player_count; i++)
                {
                    if (g_playerPlayerData[i].order_out_in_yolt != 0
                        && !(g_playerPointers[i]->redbloodfinished && g_playerPointers[i]->deathanimfinished
                             && g_playerPointers[i]->colourfadetimemax60 < 0.0f))
                    {
                        all_fully_dead = FALSE;
                    }
                }
                if (teams_left <= 1)
                {
                    if (all_fully_dead)
                    {
                        mpCalculateAwards(FALSE);
                    }
                    else
                    {
                        mpwatchSetStopPlayFlag();
                    }
                }
            }
            else
#endif
            if (fully_dead_total >= player_count - 1)
            {
                mpCalculateAwards(FALSE);
            }
            else if (killed_total >= player_count - 1)
            {
                mpwatchSetStopPlayFlag();
            }
        } // end YOLT

        if (0)
        {
            char debug_buf[268];
            sprintf(debug_buf, "setdetail %d %d %d %d %d %d %d %d %d", 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }

    D_80048394 = D_80048394 + g_ClockTimer;
#ifdef VERSION_EU
    g_CurrentMultiPlayerSec = (f32) (D_80048394) / 50.0f;
#else
    g_CurrentMultiPlayerSec = (f32) (D_80048394) / 60.0f;
#endif
    D_800483A8 = D_800483A8 + g_ClockTimer;
#ifdef VERSION_EU
    g_CurrentMultiPlayerMin = (f32) (D_800483A8) / 50.0f;
#else
    g_CurrentMultiPlayerMin = (f32) (D_800483A8) / 60.0f;
#endif

    if (joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON))
    {
        D_80048388 = 0;
        D_80048390 = 0;
    }
    else
    {
        D_80048390 = D_80048390 + g_ClockTimer;

#ifdef VERSION_EU
        if (D_80048390 >= 0x5DC)
#else
        if (D_80048390 >= 0x708)
#endif
        {
            D_80048388 = 1;
        }
    }

    if (D_80048388 != 0)
    {
        D_8004838C += g_ClockTimer;
    }
    else
    {
        D_800483B0 = D_800483B0 + g_ClockTimer;
#ifdef VERSION_EU
        g_StageTimeSec = (f32) (D_800483B0) / 50.0f;
#else
        g_StageTimeSec = (f32) (D_800483B0) / 60.0f;
#endif
        D_800483B8 = D_800483B8 + g_ClockTimer;
#ifdef VERSION_EU
        g_PowerOnTimeSec = (f32) (D_800483B8) / 50.0f;
#else
        g_PowerOnTimeSec = (f32) (D_800483B8) / 60.0f;
#endif
    }

    viSetUseZBuf(1);

    if (g_CurrentStageToLoad == LEVELID_TITLE)
    {
        cheatButtonSampleInput();
        menu_init();
        langTick();
    }
    else
    {
        sub_GAME_7F09BBBC();
        lvlSetMultipliersForDifficulty();
        updateRoomStatusFlags();

#ifndef TARGET_N64
        { /* GE_INVIS=1: hold the Bond-invisible cheat flag every tick, so
           * observation runs see cameras in their idle sweep, not tracking the player. */
            extern char *getenv(const char *);
            extern void bondviewSetVisibleToGuardsFlag(s32);
            static int invis = -1;
            if (invis < 0) invis = getenv("GE_INVIS") != NULL;
            if (invis) bondviewSetVisibleToGuardsFlag(FALSE);
        }
#endif
        sub_GAME_7F092E50();
        skyTick();
#ifdef VERSION_EU
        bullet_moving_spark_create();
#else
        bullet_sparks_update_all();
#endif
        update_bullet_casings();
        update_broken_windows();
        explosionUpdateFlyingParticles();
        chrpropTick();
        reset_all_music_slots();
        langTick();

        if ((get_debug_joy2detailedit_flag() != 0) && (D_800483C8 == 0))
        {
            s32 i;
            D_800483C8 = (struct LvlMpUnknown *)mempAllocBytesInBank(0x3000, MEMPOOL_STAGE);
            if (D_800483C8 != 0)
            {
                for (i=0; i<3000; i++)
                {
                    D_800483C8[i].unk_0 = 0xff;
                    D_800483C8[i].unk_1 = (D_800483C8[i].unk_1 & 0xFF1F) | 0x20;
                    D_800483C8[i].unk_1 = (D_800483C8[i].unk_1 & 0xFFE3) | 4;
                }
            }
        }

        if (get_debug_portal_flag() != 0)
        {
            lvlPortalDebug7F0BDF10(0);
        }

        switch (getDebugMode())
        {
            case 4:
            {
                if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS))
                {
                    sub_GAME_7F0AF630(-1);
                    debugStanView(0, 0, 0);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS))
                {
                    sub_GAME_7F0AF630(1);
                    debugStanView(0, 0, 0);
                }
            }
            break;

            case 8:
            {
                if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS))
                {
                    chrDecrementAnimationTablePointerCount();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS))
                {
                    chrIncrementAnimationTablePointerCount();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, L_TRIG))
                {
                    chrToggleD_8002C90C();
                }

                sub_GAME_7F022EE0(joyGetButtons(PLAYER_1, R_TRIG) != 0);
            }
            break;
        }

    }
    {
        struct ALBank * sfx;
        s16 sound_index;
        s16 *sound_index_ptr;
        switch (getDebugMode())
        {
            case 0x38:
            {
                s32 sp30;
                s32 sp2C;
                sp30 = viGetHorizontalOffset();
                sp2C = viGet800232A0();
                if (joyGetButtons(PLAYER_1, D_CBUTTONS))
                {
                    sp2C += 1;
                }
                if (joyGetButtons(PLAYER_1, U_CBUTTONS))
                {
                    sp2C += -1;
                }
                if (joyGetButtons(PLAYER_1, R_CBUTTONS))
                {
                    sp30 += 1;
                }
                if (joyGetButtons(PLAYER_1, L_CBUTTONS))
                {
                    sp30 += -1;
                }
                viSetHorizontalOffset(sp30);
                viSet800232A0(sp2C);
            }
            break;

            case 0xc:
            {
                if (joyGetButtonsPressedThisFrame(PLAYER_1, (L_JPAD | L_CBUTTONS)))
                {
                    lvlMusicAppendPlayEndTheme();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (R_JPAD | R_CBUTTONS)))
                {
                    lvlMusicAppendPlaySoloDeathShort();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD))
                {
                    musicTrack1Stop();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
                {
                    musicTrack1SaveCurrentVolumeAsTrackDefault();
                }
            }
            break;
            case 0xd:
            {
#ifdef VERSION_US
                sound_index_ptr = &g_DebugMpGameSoundFxIndex;

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (D_JPAD | L_JPAD | L_TRIG | L_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex - 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (U_JPAD | R_JPAD | R_TRIG | R_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex + 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS))
                {
                    sndDeactivateAllSfxByFlag_1();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS))
                {
                    sound_index = g_DebugMpGameSoundFxIndex;\
                    sfx = g_musicSfxBufferPtr;
                    sndPlaySfx(sfx, sound_index, NULL);
                }
#else
                sound_index_ptr = &g_DebugMpGameSoundFxIndex;

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (D_JPAD | L_JPAD | L_TRIG | L_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex - 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (U_JPAD | R_JPAD | R_TRIG | R_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex + 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS))
                {
                    sndDeactivateAllSfxByFlag_1();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS))
                {
                    sound_index = g_DebugMpGameSoundFxIndex;\
                    sfx = g_musicSfxBufferPtr;
                    sndPlaySfx(sfx, sound_index, NULL);
                }
#endif
            }
            break;

            default:
            break;
        }
    }
}



/**
 * Assumes a debug mode is present, and handles debug edit intro, debug stan edit, debug bond "view."
 * By default, the DEB_BOND_VIEW path is chosen without debug info.
 * This updates the player viewport(s), and handles player movement.
 *
 * Multiplayer:
 * Updates distance_traveled and possibly (depending on scenario) have_token_or_goldengun.
 *
 * US Address 0x7F0BF800.
 * EU address 7F0BEC44.
 */
void lvlViewMoveTick(void)
{
    s8 local_player_number;
    s32 padding;
    f32 temp_f0;
    f32 temp_f2;

    local_player_number = get_cur_playernum();
    cheatButtonSampleInput();

    switch (get_debug_freeze_processing())
    {
        case DEB_MOVE_VIEW:
        {
            if ((getDebugMode() == DEB_MOVE_VIEW) || ((getDebugMode() == DEB_INTRO_EDIT) && (D_80036ABC < 0)))
            {
                debugFreeCamera(joyGetStickX(local_player_number), joyGetStickY(local_player_number), joyGetButtons(local_player_number, ANY_BUTTON));
            }
            else
            {
                debugFreeCamera(joyGetStickX(local_player_number), joyGetStickY(local_player_number), 0);
            }
        }
        break;

        case DEB_STAN_VIEW:
        {
            if (getDebugMode() == DEB_STAN_VIEW)
            {
                debugStanView(joyGetStickX(local_player_number), joyGetStickY(local_player_number), joyGetButtons(local_player_number, ANY_BUTTON));
            }
            else
            {
                debugStanView(joyGetStickX(local_player_number), joyGetStickY(local_player_number), 0);
            }
        }
        break;

        case DEB_BOND_VIEW:
        {
            if (getDebugMode() == DEB_BOND_VIEW)
            {
                bondviewMovePlayerUpdateViewport(joyGetStickX(local_player_number), joyGetStickY(local_player_number), joyGetButtons(local_player_number, ANY_BUTTON));
            }
            else
            {
                bondviewMovePlayerUpdateViewport(joyGetStickX(local_player_number), joyGetStickY(local_player_number), 0);
            }

            mpwatchMenuTick();
        }
        break;
    }

    temp_f0 = g_CurrentPlayer->prop->pos.x - g_CurrentPlayer->bondprevpos.x;
    temp_f2 = g_CurrentPlayer->prop->pos.z - g_CurrentPlayer->bondprevpos.z;

    g_playerPerm->distance_traveled += sqrtf((temp_f0 * temp_f0) + (temp_f2 * temp_f2));

    if (get_scenario() == SCENARIO_TLD)
    {
        if (bondinvIsAliveWithFlag())
        {
            if (getCurrentPlayerWeaponId(GUNRIGHT) != ITEM_TOKEN)
            {
                currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_TOKEN);

                if(1);

                if (g_CurrentPlayer->hands[GUNRIGHT].weapon_action_state == GUN_ANIM_STATE_FIRE)
                {
                    g_CurrentPlayer->hands[GUNRIGHT].weapon_action_state = GUN_ANIM_STATE_SWITCH_LOWER;
                }
            }

            g_playerPerm->flag_counter += g_ClockTimer;
            g_playerPerm->have_token_or_goldengun = 1;
        }
        else
        {
            g_playerPerm->have_token_or_goldengun = 0;
        }

        return;
    }

    if (get_scenario() == SCENARIO_MWTGG)
    {
        if (bondinvHasGoldenGun())
        {
            g_playerPerm->have_token_or_goldengun = 1;
        }
        else
        {
            g_playerPerm->have_token_or_goldengun = 0;
        }

        return;
    }
}





void lvlUnloadStageTextData(void)
{
    if (g_MpSoundStateRelated != NULL)
    {
        if (sndGetPlayingState(g_MpSoundStateRelated) != AL_STOPPED)
        {
            sndDeactivate(g_MpSoundStateRelated);
        }
    }

    if (g_CurrentStageToLoad != LEVELID_TITLE)
    {
        langClearBank(langGetLangBankIndexFromStagenum(g_CurrentStageToLoad));
        set_favorite_weapon_for_every_player();
    }

    cheatDisableAllCheats();
    cleanupGuardData();
    cleanupObjectSounds();
    cleanupExplosions();
    cleanup_window_pieces();
    cleanup_REMOVED_();
    cleanupAlarms();
    cleanupObjects(g_CurrentStageToLoad);
    cleanupObjectives();
    cleanupSFXRelated();
    cleanupplayersoundrelated();
    set_missionstate_zero();
#ifndef TARGET_N64
    /* Stage unload (every restart / return to title): the state change
     * above can be a no-op, so stop any music player the state machine lost
     * track of, or it would play into the next stage (music.c
     * portMusicKillAll). */
    portMusicKillAll();
    { /* The cleanups above only post their stop events, which a full event
       * queue silently drops (the tank engine kept playing). Kill every live
       * sound synchronously here (snd.c), and always log it so user logs show
       * what unload had to clean up. */
        s32 voices = 0;
        s32 killed = portSndKillAllSfx(&voices);
        fprintf(stderr, "ge: snd: stage unload - %d live sfx state(s) killed (%d with a voice)\n", killed, voices);
    }
#endif
    cleanup_rooms();
}


void lvlSetControlsLockedFlag(s32 arg0)
{
    #if defined(BUGFIX_R1)
    if ((arg0 != 0) && (g_ControlsLockedFlag == 0))
    {
        joyRumblePakStop();
    }
    #endif

    g_ControlsLockedFlag = arg0;
}


s32 lvlGetControlsLockedFlag(void)
{
    return g_ControlsLockedFlag;
}


DIFFICULTY lvlGetSelectedDifficulty(void)
{
    return g_SelectedDifficulty;
}


void lvlSetSelectedDifficulty(DIFFICULTY arg0)
{
    g_SelectedDifficulty = arg0;
}

void lvlSetMpTime(s32 arg0)
{
    g_MpTime = arg0;
}


void lvlSetMpPoint(s32 arg0)
{
    g_MpPoint = arg0;
}


f32 lvlGetCurrentMultiPlayerSec(void)
{
    return g_CurrentMultiPlayerSec;
}


f32 lvlGetCurrentMultiPlayerMin(void)
{
    return g_CurrentMultiPlayerMin;
}


/**
 * Unreferenced.
 */
f32 lvlGetStageTimeSec(void)
{
    return g_StageTimeSec;
}


/**
 * Unreferenced.
 */
f32 lvlGetPowerOnTimeSec(void)
{
    return g_PowerOnTimeSec;
}


