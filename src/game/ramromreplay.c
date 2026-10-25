
#include <ultra64.h>
#include "game/mp_weapon.h"
#include "game/indy_comms.h"
#include "debugmenu_handler.h"
#include "lv.h"
#include "initcheattext.h"
#include "front.h"
#include "ramromreplay.h"
#include <ramrom.h>
#include <macro.h>
#include "file.h"
#include "file2.h"
#include <random.h>
#include "joy.h"
#include "bondview.h" /* struct player, for the GE_DEMOPOS diagnostic */
#include "player.h" /* g_CurrentPlayer */
#include "../../port/fast3d/gfx_api.h" /* PortHwLoad (GE_HWCAL) */
#include "frametiming.h"
//D:800483F0

struct ramrom_struct
{
    ramromfilestructure *fdata;
    s32 locked;
};

struct ramrom_blockbuf
{
    s8 stick_x;
    s8 stick_y;
    u8 button_low;
    u8 button_high;
};
struct ramrom_seed
{
    u8 speedframes;
    u8 count;
    u8 randseed;
    u8 check;
};

//move me to better home
extern u32* ramrom_Dam_1;
extern u32* ramrom_Dam_2;
extern u32* ramrom_Facility_1;
extern u32* ramrom_Facility_2;
extern u32* ramrom_Facility_3;
extern u32* ramrom_Runway_1;
extern u32* ramrom_Runway_2;
extern u32* ramrom_BunkerI_1;
extern u32* ramrom_BunkerI_2;
extern u32* ramrom_Silo_1;
extern u32* ramrom_Silo_2;
extern u32* ramrom_Frigate_1;
extern u32* ramrom_Frigate_2;
extern u32* ramrom_Train;

extern u64 g_randomSeed;
extern u64 g_chrObjRandomSeed;

struct ramrom_struct ramrom_table[] = {
    {&ramrom_Dam_1, 0},
    {&ramrom_Dam_2, 0},
    {&ramrom_Facility_1, 0},
    {&ramrom_Facility_2, 0},
    {&ramrom_Facility_3, 0},
    {&ramrom_Runway_1, 0},
    {&ramrom_Runway_2, 0},
    {&ramrom_BunkerI_1, 0},
    {&ramrom_BunkerI_2, 0},
    {&ramrom_Silo_1, 0},
    {&ramrom_Silo_2, 0},
    {&ramrom_Frigate_1, 0},
    {&ramrom_Frigate_2, 0},
    {&ramrom_Train, 0},
    {0,0}
};

//D:80048468
ramromfilestructure* ptr_active_demofile = 0;
//D:8004846C
struct ramrom_seed * ramrom_blkbuf_2 = NULL;
//D:80048470
struct ramrom_blockbuf * ramrom_blkbuf_3 = NULL;
//D:80048474
s32 is_ramrom_flag = 0;
//D:80048478
s32 ramrom_demo_related_3 = 0;
//D:8004847C
s32 g_ramromPlayBackFlag = 0;
//D:80048480
s32 recording_ramrom_flag = 0;
//D:80048484
s32 ramrom_demo_related_6 = 0;
//D:80048488
s32 g_ramromRecordFlag = 0;
//D:8004848C
//                     .align 4





void ramromFadeToTitle(void);
s32 ramrom_replay_handler(struct contsample *arg0, s32 arg1);
void record_player_input_as_packet(struct contsample *arg0, s32 arg1, s32 arg2);
void copy_current_ingame_registers_before_ramrom_playback(ramromfilestructure *state);
void copy_recorded_ramrom_registers_to_proper_place_ingame(ramromfilestructure *state);

#ifndef TARGET_N64
/* The attract demos are the original big-endian N64 recordings; only their
 * header has multi-byte fields, so playback byte-swaps the header in place and
 * reads the u8 input stream as-is. The on-disk header is always 0xE8 bytes, but
 * sizeof(ramromfilestructure) is 0xE4 on Linux i386, so never use sizeof to
 * skip a header from a .bin. */
#define RAMROM_HEADER_ROMSIZE 0xE8

static u32 port_ramrom_swap32(u32 v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v & 0xFF00) << 8) | (v << 24);
}

static u64 port_ramrom_swap64(u64 v)
{
    return ((u64) port_ramrom_swap32((u32) v) << 32) | port_ramrom_swap32((u32) (v >> 32));
}

static void port_swap_ramrom_header(ramromfilestructure *h)
{
    s32 i;

    h->randomseed = port_ramrom_swap64(h->randomseed);
    h->randomizer = port_ramrom_swap64(h->randomizer);
    h->stagenum = (enum LEVELID) port_ramrom_swap32(h->stagenum);
    h->difficulty = (enum DIFFICULTY) port_ramrom_swap32(h->difficulty);
    h->size_cmds = port_ramrom_swap32(h->size_cmds);
    h->savefile.chksum1 = (s32) port_ramrom_swap32(h->savefile.chksum1);
    h->savefile.chksum2 = (s32) port_ramrom_swap32(h->savefile.chksum2);
    h->savefile.options = (u16) ((h->savefile.options >> 8) | (h->savefile.options << 8));
    h->totaltime_ms = (s32) port_ramrom_swap32(h->totaltime_ms);
    h->filesize = (s32) port_ramrom_swap32(h->filesize);
    h->mode = (enum GAMEMODE) port_ramrom_swap32(h->mode);
    h->slotnum = port_ramrom_swap32(h->slotnum);
    h->numplayers = port_ramrom_swap32(h->numplayers);
    h->scenario = port_ramrom_swap32(h->scenario);
    h->mpstage_sel = port_ramrom_swap32(h->mpstage_sel);
    h->gamelength = port_ramrom_swap32(h->gamelength);
    h->mp_weapon_set = port_ramrom_swap32(h->mp_weapon_set);
    h->aim_option = port_ramrom_swap32(h->aim_option);

    for (i = 0; i < 4; i++)
    {
        h->mp_char[i] = port_ramrom_swap32(h->mp_char[i]);
        h->mp_handi[i] = port_ramrom_swap32(h->mp_handi[i]);
        h->mp_contstyle[i] = port_ramrom_swap32(h->mp_contstyle[i]);
        h->mp_flags[i] = port_ramrom_swap32(h->mp_flags[i]);
    }
}

/* Demo-exit diagnostics. Five checks can end playback (terminator block,
 * totaltime, randseed desync, checksum mismatch, real input press) and all lead
 * to the same silent fade, so log which one fired, once per playback.
 * GE_DEMODIAG=1 also prints per-tick RNG draw counts (from random.c's
 * randomGetNext counter) to diff against counts rebuilt offline from the
 * recording's seed bytes. GE_RNGTRACE=<tick> traces every RNG caller for ticks
 * [tick-1, tick], to find which draws a diverging tick made. */
static s32 port_demo_ticks;
static s32 port_demo_exit_logged;
static s32 port_demo_desync_noted;
s32 g_portRandDrawCount;
s32 g_portRandTraceArmed;
static s32 port_demo_lastdraws;

static void port_demo_exit_log(const char *reason, u32 detail_a, u32 detail_b)
{
    extern int printf(const char *, ...);

    if (port_demo_exit_logged == 0)
    {
        port_demo_exit_logged = 1;
        printf("ge: demo exit: %s at tick %d, gametime %d vsyncs (~%ds), detail %x/%x\n",
               reason, port_demo_ticks, g_GlobalTimer, g_GlobalTimer / 60, detail_a, detail_b);
    }
}

/* Playback pacing for boss.c's main loop: how many vsyncs the next demo tick
 * should take, read from byte 0 (speedframes) of the next unread seed block. 0
 * (the end-of-demo terminator) is clamped to 1, for the same reason as the
 * updateFrameCounters clamp below. */
s32 portRamromPendingSpeedframes(void)
{
    u8 speed;

    if (is_ramrom_flag == 0 || address_demo_loaded == NULL)
    {
        return 1;
    }

    speed = address_demo_loaded[0];
    return (speed > 0) ? speed : 1;
}
#endif


void clear_ramrom_block_buffer_heading_ptrs(void) {
    ptr_active_demofile = 0;
    ramrom_blkbuf_2 = 0;
    ramrom_blkbuf_3 = 0;
}


s32 get_is_ramrom_flag(void) {
    return is_ramrom_flag;
}


s32 get_recording_ramrom_flag(void) {
    return recording_ramrom_flag;
}


s32 interface_menu0B_runstage(void) {
    return g_ramromPlayBackFlag;
}

// Address 0x7F0BFCB0 NTSC.
void finalize_ramrom_on_hw(void)
{
    u8 buffer[0x28];
    u8 *p;
    void *a1;

    p = ALIGN16_a((intptr_t)buffer);
    p[0] = 0;
    p[1] = 0;

    romWrite((void *) p, (void *) address_demo_loaded, 0x10U);
    
    address_demo_loaded += 4;

    a1 = INDY_RAMROM_DEMO_POINTER;

    ptr_active_demofile = romCopyAligned(ramrom_data_target, a1, 0xf0);
    ptr_active_demofile->totaltime_ms = g_GlobalTimer - g_ClockTimer;
    ptr_active_demofile->filesize = (intptr_t)address_demo_loaded - (s32)a1;
    romWrite(ptr_active_demofile, a1, 0xf0);
}


// Address 0x7F0BFD60 NTSC.
void save_ramrom_to_devtool(void)
{
    int i;
    char indyFileName [256];
    u32 size;
    
    for (i = 1; ; i++)
    {
        sprintf(indyFileName, "replay/demo.%d", i);
        
        if (!indycommHostCheckFileExists(indyFileName, &size))
        {
            break;
        }
    }
    
    sprintf(indyFileName, "replay/demo.%d", i);
    indycommHostSaveFile(indyFileName, INDY_RAMROM_DEMO_POINTER, ptr_active_demofile->filesize);
}





void load_ramrom_from_devtool(void)
{

    static const char strDemoFileName[] = "replay/demo.load";
    s32 size;

    if (indycommHostCheckFileExists(&strDemoFileName, &size) != 0)
    {
        indycommHostRamRomLoad(&strDemoFileName, (u8 *)INDY_RAMROM_DEMO_ADDRESS, size);
        ptr_active_demofile = romCopyAligned(&ramrom_data_target, (u8 *)INDY_RAMROM_DEMO_ADDRESS, sizeof(struct ramromfilestructure));
    }
}






// Address 0x7F0BFE5C NTSC
void record_player_input_as_packet(struct contsample *arg0, s32 arg1, s32 arg2)
{
    s32 temp_t5;
    s32 temp_t1;
    s32 var_a0;
    s32 var_a2;
    u8 var_a3;
    s32 var_t2;
    struct ramrom_blockbuf *temp_v0;
    s32 temp_s0;
    u8 t1;
    s32 others0;

    temp_t5 = ALIGN16_a((intptr_t)&ramrom_data_target[0x1f8]);
    temp_t1 = ptr_active_demofile->size_cmds;

    var_t2 = 0;
    var_a3 = 0;
    
    ramrom_blkbuf_2 = (struct ramrom_seed *)temp_t5;
    ramrom_blkbuf_3 = (struct ramrom_blockbuf *)(ramrom_blkbuf_2 + 1);

    // loop structure based on: void joyConsumeSamples(struct contdata *contdata)
    if (arg1 != arg2)
    {
        var_a2 = (s32) (arg1 + 1) % CONTSAMPLE_LEN;
        while (1)
        {
            for (var_a0 = 0; var_a0 < temp_t1; var_a0++)
            {
                temp_v0 = ramrom_blkbuf_3 + (var_t2 * temp_t1) + var_a0;

                temp_v0->stick_x = arg0->pads[var_a2*4 + var_a0].stick_x;
                temp_v0->stick_y = arg0->pads[var_a2*4 + var_a0].stick_y;
                temp_v0->button_low = arg0->pads[var_a2*4 + var_a0].button & 0xFF;
                temp_v0->button_high = arg0->pads[var_a2*4 + var_a0].button >> 8;
    
                var_a3 += (u8) (
                    (u8)temp_v0->stick_x
                    + (u8)temp_v0->stick_y
                    + temp_v0->button_low
                    + temp_v0->button_high);
            }
            var_t2++;

            if (var_a2 == arg2)
            {
                break;
            }

            var_a2 = (s32) (var_a2 + 1) % CONTSAMPLE_LEN;
        }
    }
        
    ramrom_blkbuf_2->count = var_t2;
    ramrom_blkbuf_2->speedframes = speedgraphframes;
    ramrom_blkbuf_2->randseed = g_randomSeed;
    
    var_a3 += (u8) ((u8)ramrom_blkbuf_2->speedframes + (u8)ramrom_blkbuf_2->count + ramrom_blkbuf_2->randseed);
    ramrom_blkbuf_2->check = var_a3;

    temp_s0 = (temp_t1 * 4 * var_t2) + sizeof(s32);

    romWrite((void *) ramrom_blkbuf_2, address_demo_loaded, ALIGN16_a(temp_s0));

    address_demo_loaded += align_addr_even(temp_s0 + 1);
}




// Address 0x7F0C0080 NTSC.
s32 ramrom_replay_handler(struct contsample *arg0, s32 arg1)
{
    s32 padding[2];
    s32 var_a3;
    s32 var_a0;
    struct ramrom_blockbuf *temp_v0;
    u8 var_t0;
    s32 temp_a2;
    s32 temp_t2;

    var_t0 = 0;
    temp_a2 = (s32) ptr_active_demofile->size_cmds;
    temp_t2 = ramrom_blkbuf_2->count;

    for (var_a3 = 0; var_a3 < temp_t2; var_a3++)
    {
        arg1 = (s32) (arg1 + 1) % CONTSAMPLE_LEN;

        for (var_a0 = 0; var_a0 < MAXCONTROLLERS; var_a0++)
        {
            if (var_a0 < temp_a2)
            {
                temp_v0 = ramrom_blkbuf_3 + (var_a3 * temp_a2) + var_a0;

                arg0->pads[arg1 * 4 + var_a0].stick_x = temp_v0->stick_x;
                arg0->pads[arg1 * 4 + var_a0].stick_y = temp_v0->stick_y;
                arg0->pads[arg1 * 4 + var_a0].button = (temp_v0->button_high << 8) | temp_v0->button_low;

                var_t0 += (u8)((u8)temp_v0->stick_x + (u8)temp_v0->stick_y + temp_v0->button_low + temp_v0->button_high);
            }
            else
            {
                arg0->pads[arg1 * 4 + var_a0].stick_x = 0;
                arg0->pads[arg1 * 4 + var_a0].stick_y = 0;
                arg0->pads[arg1 * 4 + var_a0].button = 0;
            }
        }
    }

#ifndef TARGET_N64
    {
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        static s32 diag = -1;

        if (diag < 0)
        {
            diag = getenv("GE_DEMODIAG") != NULL;
        }
        if (diag)
        {
            printf("ge: demo tick %d draws=%d live=%02x rec=%02x p0=%d,%d p1=%d,%d b=%04x,%04x\n",
                   port_demo_ticks, g_portRandDrawCount - port_demo_lastdraws,
                   (u8) g_randomSeed, ramrom_blkbuf_2->randseed,
                   joyGetStickX(0), joyGetStickY(0), joyGetStickX(1), joyGetStickY(1),
                   joyGetButtons(0, 0xFFFF), joyGetButtons(1, 0xFFFF));
        }
        port_demo_lastdraws = g_portRandDrawCount;
    }
#endif

    if (ramrom_blkbuf_2->randseed != (u8)g_randomSeed)
    {
#ifdef TARGET_N64
        ramromFadeToTitle();
#else
        /* Float differences keep PC from staying RNG-exact with the N64
         * recordings, so keep replaying instead of fading out on a desync.
         * GE_DEMOSTRICT=1 restores the original fade. */
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        static s32 strict = -1;

        if (strict < 0)
        {
            strict = getenv("GE_DEMOSTRICT") != NULL;
        }
        if (strict)
        {
            port_demo_exit_log("RANDSEED desync (recorded/live)", ramrom_blkbuf_2->randseed, (u8)g_randomSeed);
            ramromFadeToTitle();
        }
        else if (!port_demo_desync_noted)
        {
            port_demo_desync_noted = 1;
            printf("ge: demo note: RANDSEED desync at tick %d (%x/%x) - tolerant mode, playback continues\n",
                   port_demo_ticks, ramrom_blkbuf_2->randseed, (u8)g_randomSeed);
        }
#endif
    }

    var_t0 += (u8)((u8)ramrom_blkbuf_2->speedframes + (u8)ramrom_blkbuf_2->count + ramrom_blkbuf_2->randseed);
    if (ramrom_blkbuf_2->check != var_t0)
    {
#ifndef TARGET_N64
        port_demo_exit_log("CHECKSUM mismatch (stored/computed)", ramrom_blkbuf_2->check, var_t0);
#endif
        ramromFadeToTitle();
    }

    joySetContDataIndex(0);

    if (joyGetButtonsPressedThisFrame(0, 0xFFFFU) != 0)
    {
#ifndef TARGET_N64
        port_demo_exit_log("real input press (button mask)", joyGetButtonsPressedThisFrame(0, 0xFFFFU), 0);
#endif
        ramromFadeToTitle();
        prev_keypresses = TRUE;
    }
#ifndef TARGET_N64
    {   /* PORT: the joy_port.c binds (face buttons, bumpers, several keys)
         * never reach contdata, so also exit the demo on a fresh one-shot press
         * on pad 0 (portJoyPortSkipPressed). Must run at the top of the tick
         * before the weapon/crouch steps; it touches no sim state, so demo sync
         * is unaffected. */
        extern s32 portJoyPortSkipPressed(s32 pad); /* joy_port.c */

        if (portJoyPortSkipPressed(0))
        {
            port_demo_exit_log("real input press (Port bind)", 0, 0);
            ramromFadeToTitle();
            prev_keypresses = TRUE;
        }
    }
#endif
    
    joySetContDataIndex(1);

    return arg1;
}



// Address 0x7F0C0268 NTSC.
void iterate_ramrom_entries_handle_camera_out(void)
{
    s32 temp_v1;
    s32 var_a3;
    
#ifndef TARGET_N64
    port_demo_ticks++;
    { /* GE_HWCAL=1: log the N64's recorded timing for this tick beside the port's estimate of the
       * previous frame's RDP workload, to calibrate hardware pacing
       * (docs/port/n64-hw-pacing.md, "The render model") */
        static s32 hc = -1;
        if (hc < 0) { extern char *getenv(const char *); hc = getenv("GE_HWCAL") != NULL; }
        if (hc) {
            extern int printf(const char *, ...);
            extern u32 g_PortHwChrsLast;
            extern s32 g_NumParticleEntries;
            const struct PortHwLoad *h = &g_PortHwLoadLast;
            printf("ge: hwcal: t=%d fields=%d f1=%u f2=%u fx=%u fr=%u tris=%u loads=%u lbytes=%u draws=%u cmds=%u chrs=%u parts=%d frame=%u\n",
                   port_demo_ticks, portRamromPendingSpeedframes(), h->fill1, h->fill2, h->fillx, h->fillr, h->tris,
                   h->loads, h->lbytes, h->draws, h->cmds, g_PortHwChrsLast, g_NumParticleEntries, h->frame);
        }
    }
    { /* GE_DEMOPOS=1: Bond's position per demo tick, for a route comparison across builds */
        static s32 dp = -1;
        if (dp < 0) { extern char *getenv(const char *); dp = getenv("GE_DEMOPOS") != NULL; }
        if (dp) {
            extern int printf(const char *, ...);
            printf("ge: demo pos: %d %.3f %.3f %.3f\n", port_demo_ticks,
                   g_CurrentPlayer->field_488.collision_position.x, g_CurrentPlayer->field_488.collision_position.y,
                   g_CurrentPlayer->field_488.collision_position.z);
        }
    }

    { /* GE_RNGTRACE=<tick>: print every rng caller for ticks [tick-1, tick]
       * (the draws the seed check at <tick> judges are tick-1's sim). */
        extern char *getenv(const char *);
        static s32 tracetick = -2; /* -2 unparsed, -1 off */

        if (tracetick == -2)
        {
            const char *e = getenv("GE_RNGTRACE");
            tracetick = -1;
            if (e != NULL && e[0] >= '0' && e[0] <= '9')
            {
                s32 i;
                tracetick = 0;
                for (i = 0; e[i] >= '0' && e[i] <= '9'; i++)
                {
                    tracetick = tracetick * 10 + (e[i] - '0');
                }
            }
        }
        g_portRandTraceArmed = (tracetick >= 0 &&
                                port_demo_ticks >= tracetick - 1 &&
                                port_demo_ticks <= tracetick);
    }
#endif

    ramrom_blkbuf_2 = romCopyAligned(ramrom_data_target + 0x1F8, address_demo_loaded, sizeof(struct ramrom_seed));

    var_a3 = ramrom_blkbuf_2->count;
    if (var_a3 > 0)
    {
        ramrom_blkbuf_3 = romCopyAligned(
            ramrom_data_target + 0x21E,
            address_demo_loaded + 4,
            ptr_active_demofile->size_cmds * sizeof(struct ramrom_blockbuf) * ramrom_blkbuf_2->count);
    }

    var_a3 = ramrom_blkbuf_2->count;
    if (var_a3 == 0 && ramrom_blkbuf_2->speedframes == 0)
    {
#ifndef TARGET_N64
        port_demo_exit_log("clean end (terminator block)", 0, 0);
#endif
        ramromFadeToTitle();
    }
    else
    {
        // 5 is ??
        address_demo_loaded += align_addr_even((ptr_active_demofile->size_cmds * sizeof(struct ramrom_blockbuf) * ramrom_blkbuf_2->count) + 5);
    }

#ifdef TARGET_N64
    updateFrameCounters(ramrom_blkbuf_2->speedframes);
#else
    /* The end-of-demo terminator has speedframes 0, which freezes game time so
     * the fade back to the title never finishes (a soft-lock). Clamp to 1; real
     * recorded blocks always have speedframes >= 1 and keep their pacing. */
    updateFrameCounters(ramrom_blkbuf_2->speedframes > 0 ? ramrom_blkbuf_2->speedframes : 1);
#endif

    // BUG? Does this need to be adjusted for PAL?
    temp_v1 = ptr_active_demofile->totaltime_ms - 0x3C;
    if(0);
    if ((g_GlobalTimer >= temp_v1) && ((g_GlobalTimer - g_ClockTimer) < temp_v1))
    {
#ifndef TARGET_N64
        port_demo_exit_log("totaltime end (timer/limit)", g_GlobalTimer, temp_v1);
#endif
        ramromFadeToTitle();
    }
}

void copy_current_ingame_registers_before_ramrom_playback(ramromfilestructure *state)
{
    state->randomseed = g_randomSeed;
    state->randomizer = g_chrObjRandomSeed;
    state->mode = gamemode;
    state->numplayers = selected_num_players;
    state->scenario = scenario;
    state->mpstage_sel = MP_stage_selected;
    state->gamelength = game_length;
    state->mp_weapon_set = getMPWeaponSet();
    state->mp_char[0] = player_char[0];
    state->mp_char[1] = player_char[1];
    state->mp_char[2] = player_char[2];
    state->mp_char[3] = player_char[3];
    state->mp_handi[0] = player_handicap[0];
    state->mp_handi[1] = player_handicap[1];
    state->mp_handi[2] = player_handicap[2];
    state->mp_handi[3] = player_handicap[3];
    state->mp_contstyle[0] = controlstyle_player[0];
    state->mp_contstyle[1] = controlstyle_player[1];
    state->mp_contstyle[2] = controlstyle_player[2];
    state->mp_contstyle[3] = controlstyle_player[3];
    state->aim_option = aim_sight_adjustment;
    state->mp_flags[0] = get_players_team_or_scenario_item_flag(0);
    state->mp_flags[1] = get_players_team_or_scenario_item_flag(1);
    state->mp_flags[2] = get_players_team_or_scenario_item_flag(2);
    state->mp_flags[3] = get_players_team_or_scenario_item_flag(3);
#ifndef TARGET_N64
    {   /* teams set on Limits: a Normal match records as Rare's team scenario of
         * the same sides, which plays back the same; other scenarios' teams are
         * not recorded (a demo format change) */
        extern s32 portMpTeamsOn(void);
        extern s32 portMpTeamScenario(void);
        extern s32 portMpTeamOf(s32 player);

        if (portMpTeamsOn() && !portMpTeamScenario())
        {
            s32 i, blue = 0;

            for (i = 0; i < MAX_PLAYER_COUNT; i++)
            {
                state->mp_flags[i] = portMpTeamOf(i);
                blue += (i < selected_num_players) && portMpTeamOf(i);
            }
            if (scenario == SCENARIO_NORMAL)
            {
                state->scenario = (selected_num_players == 3) ? SCENARIO_2v1
                                : (blue == 2) ? SCENARIO_2v2 : SCENARIO_3v1;
            }
        }
    }
#endif
}

void copy_recorded_ramrom_registers_to_proper_place_ingame(ramromfilestructure *state)
{
    g_randomSeed = state->randomseed;
    g_chrObjRandomSeed = state->randomizer;
    gamemode = state->mode;
    selected_num_players = state->numplayers;
    scenario = state->scenario;
    MP_stage_selected = state->mpstage_sel;
    game_length = state->gamelength;
    setMPWeaponSet(state->mp_weapon_set);
    player_char[0] = state->mp_char[0];
    player_char[1] = state->mp_char[1];
    player_char[2] = state->mp_char[2];
    player_char[3] = state->mp_char[3];
    player_handicap[0] = state->mp_handi[0];
    player_handicap[1] = state->mp_handi[1];
    player_handicap[2] = state->mp_handi[2];
    player_handicap[3] = state->mp_handi[3];
    controlstyle_player[0] = state->mp_contstyle[0];
    controlstyle_player[1] = state->mp_contstyle[1];
    controlstyle_player[2] = state->mp_contstyle[2];
    controlstyle_player[3] = state->mp_contstyle[3];
    aim_sight_adjustment = state->aim_option;
    set_players_team_or_scenario_item_flag(0, state->mp_flags[0]);
    set_players_team_or_scenario_item_flag(1, state->mp_flags[1]);
    set_players_team_or_scenario_item_flag(2, state->mp_flags[2]);
    set_players_team_or_scenario_item_flag(3, state->mp_flags[3]);
}

// Address 0x7F0C0640 NTSC
void test_if_recording_demos_this_stage_load(enum LEVELID arg0, enum DIFFICULTY arg1)
{
    if (g_ramromRecordFlag != 0)
    {
        ptr_active_demofile = (ramromfilestructure *) ALIGN16_a((intptr_t)ramrom_data_target);
        dword_CODE_bss_8008C5F8 = 0;
        ptr_active_demofile->stagenum = arg0;
        ptr_active_demofile->difficulty = arg1;
        ptr_active_demofile->size_cmds = joyGetControllerCount();
        ptr_active_demofile->slotnum = record_slot_num;
        sub_GAME_7F01D61C(&ptr_active_demofile->savefile);
        copy_current_ingame_registers_before_ramrom_playback(ptr_active_demofile);
        recording_ramrom_flag = 1;
        ramrom_demo_related_6 = 1;
        joySetRecordFunc(record_player_input_as_packet);
        address_demo_loaded = INDY_RAMROM_DEMO_POINTER;
        romWrite(ptr_active_demofile, address_demo_loaded, 0xF0U);
        address_demo_loaded += sizeof(struct ramromfilestructure);
        g_ramromRecordFlag = 0;
        
        return;
    }
    
    if (g_ramromPlayBackFlag != 0)
    {
        dword_CODE_bss_8008C5F8 = 0;
        set_selected_difficulty(ptr_active_demofile->difficulty);
        set_solo_and_ptr_briefing(ptr_active_demofile->stagenum);
        set_selected_foldernum_and_copy_demo_eeprom(&ptr_active_demofile->savefile);
        copy_current_ingame_registers_before_ramrom_playback((ramromfilestructure *) (ramrom_data_target + 0x110));
        copy_recorded_ramrom_registers_to_proper_place_ingame(ptr_active_demofile);
#ifndef TARGET_N64
        /* baseline for the per-tick draw counter: the seed was just
         * restored to the recording's start value, so tick 1's printed
         * delta = draws during stage load (comparable to the offline
         * reconstruction's first-block count). */
        port_demo_lastdraws = g_portRandDrawCount;
#endif
        is_ramrom_flag = 1;
        ramrom_demo_related_3 = 1;
        joySetPlaybackFunc(ramrom_replay_handler, ptr_active_demofile->size_cmds);
        joySetContDataIndex(1);
        g_ramromPlayBackFlag = 0;
    }
}





void setRamRomRecordSlot(s32 arg0)
{
    g_ramromRecordFlag = 1;
    record_slot_num = arg0;
}

void stop_recording_ramrom(void)
{
    if (ramrom_demo_related_6 != 0)
    {
        finalize_ramrom_on_hw();
        joySetRecordFunc(0);
        ramrom_demo_related_6 = 0;
        recording_ramrom_flag = 0;
    }
}

void replay_recorded_ramrom_at_address(ramromfilestructure *demofile)
{
    address_demo_loaded = demofile;
    ptr_active_demofile = romCopyAligned(&ramrom_data_target, address_demo_loaded, sizeof(struct ramromfilestructure));
#ifdef TARGET_N64
    address_demo_loaded += sizeof(ramromfilestructure);
#else
    port_swap_ramrom_header(ptr_active_demofile);
    address_demo_loaded += RAMROM_HEADER_ROMSIZE;
    port_demo_ticks = 0;
    port_demo_exit_logged = 0;
    port_demo_desync_noted = 0;
    {
        extern int printf(const char *, ...);
        printf("ge: demo start: stage=%d diff=%d pads=%d totaltime=%d vsyncs (~%ds)\n",
               ptr_active_demofile->stagenum, ptr_active_demofile->difficulty,
               ptr_active_demofile->size_cmds, ptr_active_demofile->totaltime_ms,
               ptr_active_demofile->totaltime_ms / 60);
    }
#endif
    g_ramromPlayBackFlag = 1;
    set_solo_and_ptr_briefing(ptr_active_demofile->stagenum);
    set_selected_difficulty(ptr_active_demofile->difficulty);
    frontChangeMenu(MENU_RUN_STAGE,1);
}

void replay_recorded_ramrom_from_indy(void)
{
    replay_recorded_ramrom_at_address(INDY_RAMROM_DEMO_ADDRESS);
}

void ramromFadeToTitle(void)
{
    if (bondviewGetCameraMode() != CAMERAMODE_FADE_TO_TITLE)
    {
        bondviewSetCameraMode(CAMERAMODE_FADE_TO_TITLE);
    }
}

void stop_demo_playback(void)
{
    if (ramrom_demo_related_6 != 0)
    {
        stop_recording_ramrom();
        return;
    }
    if (ramrom_demo_related_3 != 0)
    {
        copy_recorded_ramrom_registers_to_proper_place_ingame(ramrom_data_target + 0x110);
        joySetPlaybackFunc(0, -1);
        joySetContDataIndex(0);
        ramrom_demo_related_3 = 0;
        is_ramrom_flag = 0;
    }
}



// Address 0x7F0C0970 NTSC.
void select_ramrom_to_play(void)
{
    s32 i;
    s32 temp_v0;

    temp_v0 = fileGetHighestStageUnlockedAnyFolder();

    for (i = 0; ramrom_table[i].fdata != NULL && temp_v0 >= ramrom_table[i].locked; i++)
    {}

    replay_recorded_ramrom_at_address(ramrom_table[randomGetNext() % i].fdata);
}


#ifndef TARGET_N64
/* ROM loading (segments.c portRomdataSegsApply): repoint a demo table slot at
 * the ROM's copy at boot. Table order is ROM order (Dam_1 .. Train); an
 * out-of-range index is ignored. */
void *portRamromFdata(s32 index)
{
    s32 count = 0;

    while (ramrom_table[count].fdata != NULL && count < index)
    {
        count++;
    }
    return (count == index) ? (void *) ramrom_table[index].fdata : NULL;
}

void portRamromRebase(s32 index, void *fdata)
{
    if (index >= 0 && index < (s32) (sizeof(ramrom_table) / sizeof(ramrom_table[0])) - 1)
    {
        ramrom_table[index].fdata = (ramromfilestructure *) fdata;
    }
}

/* GE_DEMO support (front.c portDirectLaunchDemoTick): play attract
 * recording ramrom_table[index] (0..13, table order) immediately. */
void portPlayRamromDemo(s32 index)
{
    s32 count = 0;

    while (ramrom_table[count].fdata != NULL)
    {
        count++;
    }

    if (index < 0 || index >= count)
    {
        index = 0;
    }

    replay_recorded_ramrom_at_address(ramrom_table[index].fdata);
}

/* Escape (or the watch_objectives bind) during playback never reaches contdata,
 * so take the same exit as a real input press: fade and go to file select, not
 * back to the attract loop. Called from bondview2.c bondviewProcessInput; the
 * caller names the input in `reason` for the log. */
void portRamromEscapeExit(const char *reason)
{
    port_demo_exit_log(reason, 0, 0);
    ramromFadeToTitle();
    prev_keypresses = TRUE;
}
#endif





u32 check_ramrom_flags(void)
{
    if ((get_is_ramrom_flag() != 0) || (get_recording_ramrom_flag() != 0))
    {
        return ptr_active_demofile->slotnum;

    }
    return 0;
}



