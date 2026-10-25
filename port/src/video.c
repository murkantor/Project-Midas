/**
 * Video glue: the fake scheduler's task path into the fast3d interpreter
 * (see docs/port/PORT-PLAN.md D7). Modeled on PD's port/src/video.c,
 * cut to the minimum GE needs: init SDL2+OpenGL backends, run display
 * lists, present.
 *
 * Compiled only when GE_VIDEO is enabled in CMake; the null-renderer path
 * in pcsched.c remains the fallback (and the headless soak mode).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ultra64.h>
#include "port_options.h"
#include "boot_flags.h"   /* the -n64_aa / -audio_pump / -audio_log parse */
#include "classic_split.h" /* the one-time n64_aa -> classic_mode migration */
#include "video_env.h"     /* the env overrides that can contradict a mode */

#ifdef GE_XBOX
/* No SDL on the console: the string/keyboard/thread stand-ins live in the
 * shim, the window + renderer are the pbkit/NV2A pair (gfx_nxdk.cpp). */
#include "xbox_sdl_shim.h"
#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_nv2a.h"
#define gfx_sdl gfx_xbox_wm
#define gfx_opengl_api gfx_nv2a_api
#else
#include <SDL.h>

#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_sdl.h"
#include "../fast3d/gfx_opengl.h"
#endif
#include "game/frametiming.h"   /* portTickSetHz: [display] tickhz */
#include "tick_stats.h"         /* portTickIniValue: the tickhz text this file holds */
#include "romdata.h"            /* romdataIsIniKey: [data] keys are not "unknown controls" */

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console ring (spec section 7). */
extern void portConLogf(const char *fmt, ...);

static bool s_VideoInitDone = false;

static void portRenderThreadStart(void); /* item 15 render thread, defined below */

/* ==== [display] settings (midas.ini) ======================================== */

static int s_CfgWindowW = 640;
static int s_CfgWindowH = 480;
/* [display] window_x/window_y/window_display: the last used window position
 * and monitor, written back into midas.ini at exit (see videoSaveWindowIni).
 * VIDEO_POS_UNSET = never saved -> SDL picks (100,100 / centred on the
 * remembered monitor). */
#define VIDEO_POS_UNSET (-100000)
static int s_CfgWindowX = VIDEO_POS_UNSET;
static int s_CfgWindowY = VIDEO_POS_UNSET;
static int s_CfgWindowDisplay = -1;
static int s_CfgWindowSaveArmed = 0;  /* atexit registered */
/* the resolution row picked an EXCLUSIVE mode this session and wrote it to
 * the ini (portVideoResApply); the exit saver must not put the last
 * WINDOWED size back over it */
static int s_CfgExclusiveResPicked = 0;
/* [display] exclusive_width/height is the exclusive-fullscreen mode's own size, and
 * window_width/height belongs to the window alone. 0 = never picked: exclusive
 * uses the desktop mode (or the old window_* values from an older ini, see
 * videoInit). */
static int s_CfgExclW = 0;
static int s_CfgExclH = 0;
/* [display] fullscreen: 0 windowed, 1 borderless window at display bounds, 2 exclusive
 * (SDL_WINDOW_FULLSCREEN). Default is 1; legacy 0/1 values keep their meaning. */
#define VIDEO_FS_WINDOWED  0
#define VIDEO_FS_BORDERLESS 1
#define VIDEO_FS_EXCLUSIVE 2
static int s_CfgFullscreen = VIDEO_FS_BORDERLESS;
static int s_CfgWidescreen = 1;       /* 0 Stretch, 1 Wide (widen FOV to the window aspect), 2 4:3 (pillarbox, portPillarboxFrame) */
/* [display] render_thread: -1 auto (on when tickhz != 60), 0 off, 1 on; GE_RENDERTHREAD
 * overrides. Re-applied at the next frame boundary when it or the tick changes
 * (see portRenderThreadFollowTick). */
static int s_CfgRenderThread = -1;
static int s_CfgAdzy = 0;        /* [display] adzy: Siberian Special Forces + snow cuff */
static int s_CfgUnlockAll = 0;   /* [display] unlock_all: every mission/difficulty/cheat available */
static int s_CfgPausePillars = 0; /* [display] pause_pillars: animated 4:3 pillarbox while the watch is up */
static int s_CfgHudScale = 0;     /* [display] hud_scale: 1 = the in-game HUD on a 640x480 canvas (half size) */
static int s_CfgHealthWide = 0;   /* [display] health_wide: the health / armour arcs at the edges (1) or centred (0) */
static int s_CfgHudWide = -1;     /* [display] hud_wide: Rare's ratio option kept global (-1 = not set yet: the save's stands) */
static int s_CfgAlwaysCrosshair = 0; /* [display] always_crosshair: PD "Show Target" plus while not aiming (gunfire.c gunDrawSight) */
static int s_CfgShadowAlpha = 65;    /* [display] shadow_alpha: character/prop drop-shadow strength, % of vanilla (model.c) */
static int s_CfgMissionTimer = 0; /* [display] mission_timer: elapsed MM:SS:MS bottom-left in solo (gunfire.c portDrawMissionTimer) */
static int s_CfgStereoPan = 1;    /* [display] stereo_pan: pan positional sfx by where they are, solo only (propobj.c portSndApplyPan) */
/* 1 while a keyboard key is holding player 1's stick over (the analog_* binds, WASD by default).
 * Those keys are also the C buttons, so menus use this to tell a key from a
 * thumb and avoid stepping twice. */
static int s_KbdStickDown = 0;
/* the keyboard's Move keys this poll as -1 / 0 / +1 (x right, y up) - input_pc.c
 * portInputDigitalMove hands them to the port drive block as C-button steps */
static int s_KbdDigX = 0;
static int s_KbdDigY = 0;

void portVideoKbdDigitalMove(s32 *dx, s32 *dy)
{
    *dx = s_KbdDigX;
    *dy = s_KbdDigY;
}

s32 portVideoKbdStickDown(void)
{
    return s_KbdStickDown;
}

/* Multiplayer's own copies of the solo options (e.g. aim lock); they are read only with two or
 * more players, and are set on the MP setup's Multiplayer Options sheet
 * (front.c PORT_SHEET_MPOPTS). */
static int s_CfgMpAimLock = 0;       /* [display] mp_aim_lock */
static int s_CfgMpSight = 1;         /* [display] mp_sight_on_screen: Rare's MP "Aim" row, the sight half */
static int s_CfgMpAutoAim = 1;       /* auto-aim allowed (per player): [display] mp_no_auto_aim=0; the old mp_auto_aim=1 */
static int s_CfgMpNoAutoAimSeen = 0; /* an mp_no_auto_aim line was read: an old mp_auto_aim no longer counts */
static int s_CfgMpShowNames = 1;     /* [display] mp_show_names: each player's name in their viewport's corner (gunfire.c) */
/* Multiplayer-only cheats and two gameplay fixes; solo never reads these. lv.c applies the six
 * cheats at MP stage start in place of the solo cheat menu's picks (portMpCheatOwned). */
static int s_CfgMpInvisibility = 0;  /* [display] mp_invisibility  = CHEAT_INVISIBILITY ("Bond Invisible") */
static int s_CfgMpDkMode = 0;        /* [display] mp_dk_mode       = CHEAT_DK_MODE */
static int s_CfgMpTinyPlayers = 0;   /* [display] mp_tiny_players  = CHEAT_TINY_BOND ("Tiny Bond") */
static int s_CfgMpPaintball = 0;     /* [display] mp_paintball     = CHEAT_PAINTBALL */
static int s_CfgMpNoRadar = 0;       /* [display] mp_no_radar      = CHEAT_NO_RADAR_MP */
static int s_CfgMpTurbo = 0;         /* [display] mp_turbo_mode    = CHEAT_TURBO_MODE */
static int s_CfgMpUnlimitedAmmo = 0; /* [display] mp_unlimited_ammo = CHEAT_INFINITE_AMMO */
static int s_CfgMpHudWide = 0;       /* [display] mp_hud_wide: MP's own HUD Layout 4:3 / Wide (options.c hudAnchorsToEdges) */
static int s_CfgMpPillarbox = 0;     /* [display] mp_pillarbox: MP gameplay in a centred 4:3, black side bars (portPillarboxFrame) */
static int s_CfgMpAlwaysCrosshair = 0; /* [display] mp_always_crosshair: always_crosshair, 2+ players */
static int s_CfgMpFixShootThrough = 1; /* [display] mp_fix_shootthrough: fix_shootthrough, 2+ players */
/* Three more Multiplayer Options rows. */
static int s_CfgMpSpawnWeapon = 0;  /* [display] mp_spawn_weapon: spawn with the weapon set's slot 1 weapon, 4x its ammo (bondview2.c) */
static int s_CfgMpRandomSpawns = 0; /* [display] mp_random_spawns: each spawn search starts at a random pad (bondview.c) */
static int s_CfgMpHeadRoll = 1;     /* [display] mp_head_roll: the camera rolls with the head, 2+ players (no_head_roll is solo's) */
static int s_CfgMpUnlimitedExplosions = 1; /* [display] mp_unlimited_explosions: 100 live explosions, not 6, 2+ players (explosion.c) */
static int s_CfgUnlimitedExplosions = 1;   /* [display] unlimited_explosions: the same, solo (Port Options, default ON) */
static int s_CfgWatchStatic = 1;
static int s_CfgFastPause = 0;     /* [display] fast_pause: the watch opens and closes at Rare's top speed (bondview2.c) */
static int s_CfgMouseAutoAim = 0;   /* [display] mouse_auto_aim: 1 lets a mouse player use Auto-Aim (default: never) */
static int s_CfgMuteUnfocused = 0; /* [display] mute_unfocused: audio_pc.c plays silence while the window has no focus */
static int s_CfgArmorHud = 0;       /* [display] armor_hud: the health / armour arcs show on a body armour pickup (bondview2.c) */
static int s_CfgWeaponFov = 0;      /* [display] weapon_fov: 1 = the gun draws at the default FOV (gunfire.c), 0 = Match View */
static int s_CfgSettingHelp = 1;   /* [display] setting_help: the folder sheets' help box (front.c), default on */
/* Classic Mode's NTSC composite filter (gfx_opengl.cpp vi_prog[3]) is a Classic
 * Mode part ([display] classic_composite): it runs wherever Classic Mode's VI
 * resolve does while the part is on. GE_NOCOMPOSITE=1 turns it off. */
uint32_t gfx_composite = 1;
/* Classic Mode's N64 AA part ([display] classic_n64aa): 0 = the VI resolve
 * skips its edge blend, so the other parts keep their passes. */
uint32_t gfx_vi_blend = 1;
static int s_CfgMpLtkHealth = 0;           /* [display] mp_ltk_health: Licence to Kill's one-hit kills in any scenario (lv.c, bondview2.c) */
static int s_CfgMpFriendlyFire = 1;       /* [display] mp_friendly_fire: team-mates can hurt each other in team scenarios (bondview2.c) */
static int s_CfgMpNoArmor = 0;             /* [display] mp_no_armor: no body armour pickups on MP maps (prop.c) */
static int s_CfgAimLock = 0;        /* [display] aim_lock: crosshair pinned to centre, aim input turns the view (bondview2.c) */
static int s_CfgNoHeadRoll = 0;      /* [display] head_roll=0 (was no_head_roll=1): camera never tilts with Bond's head animation (bondview2.c) */
static int s_CfgHeadRollKeySeen = 0; /* a head_roll line was read: an old no_head_roll line no longer counts */
static int s_CfgMpP1EndsMatch = 1;   /* [display] p1_ends_match: P1's START at the MP endscreen exits for everyone (mpmenu.c) */
static int s_CfgScopeSteady = 0; /* [display] scope_steady: 1 = anchor the head
                                  * basis as the fov narrows (kills the sniper-zoom
                                  * cloud slide, but also kills the head sway while
                                  * scoped). 0 = authentic sway at every zoom. */
static int s_CfgMsaa = 1;             /* 1 = off; 2/4/8 = MSAA samples */
static int s_CfgMaxFps = 120;         /* render pace cap; 0 = uncapped */
static int s_CfgVsync = 1;            /* 0 = never wait for vblank */
static int s_CfgAnisotropy = 0;       /* 0/1 = off; 2..16 samples */
/* [display] filter_2d: 0 = point-sample HUD/menu texrects. The menu overhaul
 * made OFF the default (spec 2.1) - crisp 2D is what the art was drawn for;
 * the Nintendo 64 preset turns it back on. */
static int s_CfgFilter2D = 0;
static int s_CfgHighLod = 1;          /* 0 = use normal distance-based model LODs */
static int s_CfgLowRes = 0;           /* [display] low_res: the level render's
                                       * line count - 0 off, 240 = "HW" (legacy
                                       * value 1), 480, 720, 1080; scaled to the
                                       * window. */
#ifdef GE_XBOX
static int s_CfgRoomCache = 2;        /* [display] room_cache: 0 off, 1 CPU
                                       * replay, 2 GPU-retained rooms + model
                                       * parts (gfx_roomcache.inl) */
#else
static int s_CfgRoomCache = 0;        /* PC: 2 = the GL retained path
                                       * (gfx_opengl.cpp retained_*), off by
                                       * default on PC */
#endif
#ifdef GE_XBOX
static int s_CfgXboxMode = 0;         /* [display] xbox_mode: 0 = 480p, 1 = 720p, 2 = 1080i (output raster; test knob) */
extern int gfx_xbox_wm_request_mode(int mode);
#endif
static int s_CfgTriHoist = 1;         /* [display] tri_hoist: gfx_sp_tri1 derives draw state per state change, not per triangle (0 = A/B) */
extern uint32_t gfx_tri_hoist;        /* gfx_pc.cpp */
extern void portRoomCacheSetMode(s32 mode); /* roc_hooks.c: the sim-side gate + the render-side clear/mode, in order */
#ifdef GE_XBOX
static int s_CfgXboxClipXy = 0;       /* [display] xbox_clip_xy: 1 = NV2A backend
                                       * also clips the four side planes on the
                                       * CPU; 0 = near/far only (kill switch if
                                       * edges misdraw). */
extern uint32_t gfx_xbox_clip_xy;     /* gfx_nv2a.cpp */
static int s_CfgXboxClipNear = 0;     /* [display] xbox_clip_near: 1 = CPU clips
                                       * near and far; 0 = no CPU clipping
                                       * (experiment: let the NV2A clip). */
extern uint32_t gfx_xbox_clip_near;   /* gfx_nv2a.cpp */
static int s_CfgXboxGpuCull = 2;      /* [display] xbox_gpu_cull: GPU-retained rooms' backface cull - 0 off, 1 front = CW
                                       * in screen space, 2 front = CCW (default) */
extern uint32_t gfx_xbox_gpu_cull;    /* gfx_nv2a.cpp */
static int s_CfgXboxVpLegacy = 0;     /* [display] xbox_vp_legacy: 1 = the older
                                       * vertex program (bisect knob; GPU rooms
                                       * off) */
extern uint32_t gfx_xbox_vp_legacy;   /* gfx_nv2a.cpp */
static int s_CfgXboxGpuFullConst = 1; /* [display] xbox_gpu_fullconst: 1 = whole constant block per room change (default), 0 = partial bursts */
static int s_CfgXboxGpuViaStream = 0; /* [display] xbox_gpu_via_stream: 1 = room draws go through the stream ring (bisect) */
extern uint32_t gfx_xbox_gpu_fullconst, gfx_xbox_gpu_via_stream; /* gfx_nv2a.cpp */
#endif
/* knobs of the SHARED interpreter / room cache (gfx_pc.cpp, gfx_roomcache.inl): every
 * platform; the names keep the xbox_ prefix they were born with */
static int s_CfgXboxGpuLit = 1;       /* [display] xbox_gpu_lit: 0 = GPU-retained parts skip the program's lighting (bisect) */
extern uint32_t gfx_xbox_gpu_lit;     /* gfx_roomcache.inl */
static int s_CfgXboxPartSkipTex = 1;  /* [display] xbox_part_skiptex: 0 = a walked part list still runs its texture-load chain (bisect) */
extern uint32_t gfx_xbox_part_skiptex; /* gfx_pc.cpp */
static int s_CfgXboxXluVisitsMax = 8000; /* [display] xbox_xlu_visits_max: XLU plane-pass pair visits a frame before the key order stands (0 = unlimited) */
extern uint32_t gfx_xbox_xlu_visits_max; /* gfx_pc.cpp */
static int s_CfgXboxXluBatch = 1;     /* [display] xbox_xlu_batch: 0 = no state-run merging of the sorted XLU list (bisect) */
extern uint32_t gfx_xbox_xlu_batch;   /* gfx_pc.cpp */
static int s_CfgXboxXluRetained = 1;  /* [display] xbox_xlu_retained: 0 = XLU triangles of retained lists stay on the interpreter (bisect) */
extern uint32_t gfx_xbox_xlu_retained; /* gfx_pc.cpp */
static int s_CfgXboxXluPlanar = 1;    /* [display] xbox_xlu_planar_units: 0 = retained XLU triangles are one sorter unit each (bisect) */
extern uint32_t gfx_xbox_xlu_planar_units; /* gfx_pc.cpp */
static int s_CfgN64Aa = 0;            /* [display] n64_aa: the VI AA bit ONLY - the level frame
                                       * resolved the way the N64's VI filtered it (needs MSAA:
                                       * floors it at 4x while on) - gfx_n64_aa. The Classic Mode
                                       * PRESET is [display] classic_mode, the next static. */
static int s_CfgClassicMode = 0;      /* [display] classic_mode: the Classic Mode PRESET is on.
                                       * Owns the seven forced values (port_options.c
                                       * sClassicKeys), the pillars, the hitches, the composite
                                       * filter, and every "is the mode on" question. Its
                                       * classic_n64aa part is the ONLY link to the AA bit above
                                       * (portClassicSetsN64Aa): the preset sets the bit on entry
                                       * and clears it on exit, and nothing else couples them. */
extern uint32_t gfx_n64_aa;           /* gfx_pc.cpp */
extern void gfx_set_aa(uint32_t msaa_level, uint32_t n64_aa); /* gfx_pc.cpp: latched for the render thread's next frame when it runs */
static int videoClassicViOn(void); /* with the Classic parts table below */
static void videoApplyAa(void)
{
    /* The BIT decides whether the VI resolve runs at all - it is a render
     * setting, and a player who wants N64 edges without 240p gets exactly this
     * and nothing else. The MSAA FLOOR is the preset's: Classic Mode is the mode
     * that promises a 4x resolve, so a bit-only player keeps whatever MSAA they
     * chose (with videoApplyAa's own 2x minimum). That is why the two read
     * different statics here, and it is the whole of the split inside this
     * function. */
    const int vi = s_CfgN64Aa;
    const uint32_t floor_level = videoClassicViOn() ? 4u : 2u;
    uint32_t level = (s_CfgMsaa == 2 || s_CfgMsaa == 4 || s_CfgMsaa == 8) ? (uint32_t) s_CfgMsaa : 1u;
    if (vi && level < floor_level)
    {
        level = floor_level;
    }
    gfx_set_aa(level, vi ? 1u : 0u); /* never a direct store: with the render
                                      * thread on it lands mid-frame */
}
static int s_CfgMipmaps = 0;          /* [display] hw_mipmaps: legacy key
                                       * (nonzero = on; default off, Murk
                                       * 2026-10-15). No longer drives the
                                       * render - Classic Mode does
                                       * (videoMipmapMode); kept for the
                                       * classic_saved slot. */
static int s_CfgFixShootThrough = 1;  /* 0 = restore the vanilla shoot-through-portal
                                       * room-selection bug (chrprop.c
                                       * chraiDefaultWeaponFireHandler) */
static int s_CfgDistanceFading = 0;    /* 1 = enable distance-based prop/character
                                       * transparency fade/cull (bgfog.c
                                       * fogGetPropDistColor); 0 = always fully
                                       * opaque regardless of distance */
static int s_CfgTrueEnemyRockets = 1; /* 0 = restore the vanilla Enemy Rockets
                                       * cheat bug (chrai.c AI_TRYGiveMeItem
                                       * is missing a break, so it re-arms
                                       * the guard with their original
                                       * weapon right after giving them the
                                       * rocket launcher) */
static int s_CfgShowFps = 0;          /* 1 = in-game FPS digits */
/* what midas.ini literally contained, so the derived value can be written back
 * only when the file disagrees */
static int s_CfgTickSeenInIni = 0;
static int s_CfgTickAsWritten = 60;
/* 0 while midas.ini is still being read: the coupling runs as each `maxfps=` line
 * is parsed, and a `tickhz=` line further down has not been seen yet, so
 * writing back mid-parse would record a value from a half-read file. */
static int s_CfgTickLoadDone = 0;
static char s_CfgTickBuf[PORT_TICK_INI_BUF];
static int s_CfgTickHz = 60;          /* the ini's tickhz as written (frametiming.c holds the
                                       * effective rate, which GE_TICKHZ can override - the
                                       * Port Options page must show and save the ini value) */
static char s_CfgFiltering[16] = "3point";   /* point | bilinear | 3point | trilinear */
static float s_CfgFov = 60.0f;        /* 3D viewport FOV, 40..120 (60 = vanilla) */
/* GLOBAL VOLUMES: vanilla keeps Sound / Music volume in the
 * SAVE FILE, and the frontend forces both to full - so the intros and menus
 * ignore whatever the player set, and a fresh boot has no volume at all until
 * a file is loaded. These two hold the last values a save file was seen with
 * (mirrored by file2.c on every settings load AND save) so they can be applied
 * from boot, everywhere. Percent, 0..100 - readable in the ini and in the
 * launcher; the raw 0..0x7FFF the sound code wants is derived at the apply. */
/* CROSSHAIR COLOUR: [display]
 * crosshair_color=RRGGBB recolours BOTH crosshairs - the aim-mode sight
 * sprite and the always-on target. -1 = not set = the vanilla red, drawn the
 * vanilla way. */
static int   s_CfgCrosshairColor = -1;
/* [display] playerN_crosshair_color: that player's own, -2 = follow
 * crosshair_color, -1 = vanilla red (2+ players only - portPlayerCrosshairColor) */
static int   s_CfgPlayerXhair[4] = { -2, -2, -2, -2 };
static int   s_CfgMusicVolume = 70;   /* [display] music_volume */
static int   s_CfgSfxVolume = 70;     /* [display] sfx_volume */
/* Has a global setting been established yet? Until it has, the first save
 * file whose settings are loaded SEEDS it ("the last used save file's volumes,
 * applied globally"). After that the global setting is the MASTER and a file
 * load must not clobber it - otherwise an Extended Options change made in the
 * frontend would silently revert the moment a mission started. */
static int   s_CfgVolumesSet = 0;

/* Rare's six player options (look up/down, auto-aim, aim control, sight on screen, look ahead, ammo
 * on screen) are kept globally here in midas.ini [display] and applied over
 * whatever a save file says. Indexed by PLAYER_OPTION_*; values match
 * game_options_entries[]; defaults = DEFAULT_OPTIONS (file2.h). */
#define PLAYEROPT_COUNT 6
static int   s_CfgPlayerOpt[PLAYEROPT_COUNT] = { 1, 1, 0, 1, 1, 1 };   /* look Upright (Murk 2026-10-15) */
/* one bit per key - a single row set before any folder load must not
 * make the other five "established" at their defaults */
#define PLAYEROPT_ALL ((1 << PLAYEROPT_COUNT) - 1)
static int   s_CfgPlayerOptsMask = 0;
static const char *const s_PlayerOptKeys[PLAYEROPT_COUNT] = {
    "look_updown", "auto_aim", "aim_control", "sight_on_screen", "look_ahead", "ammo_on_screen",
};

static int videoPlayerOptIndex(const char *key)
{
    int i;

    for (i = 0; i < PLAYEROPT_COUNT; i++)
    {
        if (SDL_strcasecmp(key, s_PlayerOptKeys[i]) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * midas.ini [display] fov: base field of view for the 3D viewport. The whole
 * FOV/zoom chain in the game (player init, sniper-zoom bounds, watch-zoom
 * return targets, aim-speed scaling) uses GE_DEFAULT_FOV (bondtypes.h) =
 * this value, so zoom still animates through the vanilla pipeline. Weapon
 * models compensate in gunRenderFirstPersonGunModels so they keep their
 * vanilla-FOV proportions. Weak 60.0 fallback for headless builds lives in
 * libultra_shim.c.
 */
f32 portFovDefault(void)
{
    return s_CfgFov;
}

/* hfov <-> vfov. The C library's own tan / atan, declared here: the tree's
 * include/math.h shadows the system header and leaves tanf / atanf
 * undeclared on PC, where an implicit int return read the angle as 573. */
extern double tan(double);
extern double atan(double);
#define VIDEO_DEG2RAD (3.14159265358979323846 / 180.0)

/** degrees of field of view across one axis, from the other axis's (ratio = this / that) */
static double videoFovAcross(double deg, double ratio)
{
    return 2.0 * atan(tan(deg * VIDEO_DEG2RAD * 0.5) * ratio) / VIDEO_DEG2RAD;
}

/** [display] mute_unfocused: audio_pc.c portAudioPump silences its output while unfocused */
s32 portVideoMuteUnfocused(void)
{
    return s_CfgMuteUnfocused;
}

/** [display] watch_static: options.c sub_GAME_7F0A51D8 starts a fuzz burst only while on */
s32 portWatchStatic(void)
{
    return s_CfgWatchStatic;
}

/** [display] fast_pause: bondview2.c runs the watch's open and close at 1.7, the cap Rare's
 * watch_transition_time grows to. Never in demo playback. */
s32 portFastPause(void)
{
    extern s32 get_is_ramrom_flag(void);

    return s_CfgFastPause && get_is_ramrom_flag() == 0;
}

/** [display] armor_hud: bondview2.c bondviewAddCurrentPlayerArmor shows the arcs on a pickup */
s32 portArmorHud(void)
{
    return s_CfgArmorHud;
}

/** [display] weapon_fov: gunfire.c gunWeaponFovY draws the gun at the default FOV, not the view's */
s32 portWeaponFov(void)
{
    return s_CfgWeaponFov;
}

/** The console's own 2D space (port_console.c, gfx_pc.cpp G_CONSOLE2D_EXT): 480
 *  lines, as wide as the window's shape - "640x480", full width */
void portConsoleSpace(s32 *w, s32 *h)
{
    const f32 a = (gfx_current_window_dimensions.height > 0)
                ? (f32) gfx_current_window_dimensions.width / (f32) gfx_current_window_dimensions.height : 4.0f / 3.0f;
    s32 cw = (s32) (480.0f * a + 0.5f);

    if (cw < 160)  { cw = 160; }   /* a tall window keeps the glyph shape and wraps more (not squeezed into 480) */
    if (cw > 4096) { cw = 4096; }
    *w = cw;
    *h = 480;
}

/** [display] hfov: the vertical s_CfgFov as horizontal degrees on the 4:3 picture, rounded */
static s32 videoHfov(void)
{
    return (s32) (videoFovAcross(s_CfgFov, 4.0 / 3.0) + 0.5);
}

/** [display] hfov_wide: the vertical s_CfgFov as horizontal degrees on a 16:9
 * picture, rounded - the menus' number. Vanilla's 60 vertical reads 91. */
static s32 s_FovWideRead = 0; /* hfov_wide seen: the older hfov= / fov= lines the
                                * ini keeps beside it no longer apply, in any order */

static s32 videoHfovWide(void)
{
    /* Vanilla reads 90: the true 16:9 width of 60 vertical is 91.5, off the
     * 5-step grid, so the dial calls it 90. Every other value is its true
     * width. */
    if (s_CfgFov == 60.0f)
    {
        return 90;
    }
    return (s32) (videoFovAcross(s_CfgFov, 16.0 / 9.0) + 0.5);
}

/** The TRUE 16:9 width behind the hfov_wide number, x10 (vanilla: 915 while
 * the dial says 90) - the console prints it beside the value. */
s32 portVideoHfovWideTrueTenths(void)
{
    return (s32) (videoFovAcross(s_CfgFov, 16.0 / 9.0) * 10.0 + 0.5);
}

/**
 * WIDESCREEN presentation state. The game keeps its whole coordinate space
 * authentically 4:3; only the projection aspect is widened (bondview.c
 * multiplies by portWideFactor()). Per frame the game declares whether it
 * built a wide 3D view (portSetSceneWide from bondviewMovePlayerUpdate-
 * Viewport - mission ticks, watch/pause included), and videoSubmitCommands
 * turns that into the interpreter's pin mode: wide frame -> 3D fills the
 * window, 2D texrects pin to a centered 4:3 band; non-wide frame
 * (frontend, cutscene swirls) -> the whole frame pins.
 * The declaration is SELF-EXPIRING: it only counts if re-asserted within
 * the last couple of presented frames. Mission ticks re-assert it every
 * frame; frontend/cutscene frames never do and decay to pinned on their
 * own - no explicit clear needed anywhere (the old frontDrawCursor clear
 * is gone; it used to clobber watch frames, which now render wide, so the pause
 * menu does not pillarbox in and out).
 * Weak fallbacks for headless builds live in libultra_shim.c.
 */
static s32 s_SceneWide = 0;
static u32 s_SceneWideFrame = 0; /* s_FrameNum when last asserted wide */
static u32 s_FrameNum = 2;       /* starts past the freshness window */
static u32 s_SceneMissionFrame = 0; /* s_FrameNum of the last mission tick's
                                     * declaration; only mission ticks call
                                     * portSetSceneWide, so this means "a level
                                     * is rendering" (Low Res keys on it). */

void portSetSceneWide(s32 wide)
{
    s_SceneMissionFrame = s_FrameNum; /* any call = a mission frame */
    s_SceneWide = wide;
    if (wide)
    {
        s_SceneWideFrame = s_FrameNum;
    }
}

static s32 videoSceneMissionFresh(void);

/* The render's line count (gfx_low_res) for this frame. Low Res applies on
 * mission frames only; Classic Mode also covers menus, rendering them at the
 * VI's own line count (e.g. 330 or 480) instead of squeezing to 240.
 *
 * DECIDED (Phase 3, the split's one genuinely ambiguous site): this reads the
 * PRESET, not the AA bit. Rendering menus at the VI's line count is a promise
 * of Classic Mode - the "classic" part is that the whole session, menus
 * included, runs at N64 native resolution - and it is not something a player
 * asking for edge blending has asked for. Gating it on the bit would have
 * changed menu resolution for exactly the new player the split creates (bit on,
 * preset off), which is a behaviour change invented by a refactor. Also kept
 * deliberately: this preserves the OLD observable result exactly, because the
 * preset is what the old key meant to every caller that asked this question. */
static u32 videoRenderLines(s32 viy)
{
    if (s_CfgLowRes && videoSceneMissionFresh())
    {
        return (u32) s_CfgLowRes;
    }
    if (s_CfgClassicMode && viy > 0)
    {
        return (u32) viy;
    }
    return 0u;
}

static s32 videoSceneMissionFresh(void)
{
    return s_SceneMissionFrame != 0 && (s_FrameNum - s_SceneMissionFrame) <= 2;
}

/* MP frames present ANAMORPHIC (triage F8.2): splitscreen viewports tile
 * the native screen, so pinning 2D to the centered 4:3 band drags every
 * player's HUD toward the divider. Instead the whole MP frame stretches to
 * the window - N64 16:9 style - with each viewport's 3D projection
 * pre-widened so geometry stays aspect-correct while 2D lands exactly over
 * its own viewport. Self-expiring like the wide assertion. */
static s32 s_SceneMp = 0;
static u32 s_SceneMpFrame = 0;

void portSetSceneMp(s32 mp)
{
    s_SceneMp = mp;
    if (mp)
    {
        s_SceneMpFrame = s_FrameNum;
    }
}

static s32 videoSceneMpFresh(void)
{
    return s_SceneMp && ((s_FrameNum - s_SceneMpFrame) <= 2);
}

static s32 videoSceneWideFresh(void)
{
    return s_SceneWide && ((s_FrameNum - s_SceneWideFrame) <= 2);
}

/* Pillarbox: an MP gameplay frame asserts it (bondview2.c, beside
 * portSetSceneMp), self-expiring like the other assertions. While it holds,
 * gfx_start_frame treats the window as a centred 4:3 and the frame clear leaves
 * black side bars. */
static s32 s_ScenePillar = 0;
static u32 s_ScenePillarFrame = 0;

void portSetScenePillar(s32 on)
{
    s_ScenePillar = on;
    if (on)
    {
        s_ScenePillarFrame = s_FrameNum;
    }
}

/** gfx_pc.cpp gfx_start_frame / portWideFactor: this frame is pillarboxed */
s32 portPillarboxFrame(void)
{
    return s_CfgWidescreen == 2   /* Widescreen 4:3: every frame */
        || (s_CfgMpPillarbox && s_ScenePillar && ((s_FrameNum - s_ScenePillarFrame) <= 2));
}

/* the mouse is in REAL window pixels; a pillarbox narrows the logical window
 * (gfx_current_window_dimensions) to its 4:3, so the menu maps measure this */
extern uint32_t gfx_real_window_width;   /* gfx_pc.cpp */

static f32 videoRealWinW(void)
{
    return (f32) ((gfx_real_window_width != 0) ? gfx_real_window_width : gfx_current_window_dimensions.width);
}

/** midas.ini high_lod: model.c modelUpdateDistanceRelations reads this. */
s32 portHighLod(void)
{
    return s_CfgHighLod;
}

/** midas.ini [display] fix_shootthrough: chrprop.c chraiDefaultWeaponFireHandler
 * reads this (the shoot-through-portal room-selection bug fix). */
s32 portFixShootThrough(void)
{
    extern s32 getPlayerCount(void);

    /* MP has its own switch; the solo one keeps deciding solo. */
    return (getPlayerCount() >= 2) ? s_CfgMpFixShootThrough : s_CfgFixShootThrough;
}

/** lv.c, an MP stage's cheat pass: multiplayer's own seven by INDEX - 0
 * invisibility, 1 DK mode, 2 tiny players, 3 paintball, 4 no radar, 5 turbo,
 * 6 unlimited ammo (lv.c maps them onto the CHEAT_* enum, which this file
 * does not include). */
s32 portMpCheatSetting(s32 which)
{
    switch (which)
    {
        case 6: return s_CfgMpUnlimitedAmmo != 0;
        case 0: return s_CfgMpInvisibility != 0;
        case 1: return s_CfgMpDkMode != 0;
        case 2: return s_CfgMpTinyPlayers != 0;
        case 3: return s_CfgMpPaintball != 0;
        case 4: return s_CfgMpNoRadar != 0;
        case 5: return s_CfgMpTurbo != 0;
    }
    return 0;
}

/** midas.ini [display] mp_hud_wide: MP's own HUD Layout (options.c hudAnchorsToEdges, 2+ players) */
s32 portMpHudWide(void)
{
    return s_CfgMpHudWide != 0;
}

/** midas.ini [display] distance_fading: bgfog.c fogGetPropDistColor reads this
 * (distance-based prop/character transparency fade/cull). Off by default. */
s32 portDistanceFadingEnabled(void)
{
    return s_CfgDistanceFading;
}

/** midas.ini [display] true_enemy_rockets: "True Enemy Rockets" fix */
s32 portTrueEnemyRocketsEnabled(void)
{
    return s_CfgTrueEnemyRockets;
}

/** midas.ini [display] show_fps: the bondview FPS digits read this.
 * GE_SHOWFPS env overrides either way (headless A/B, capture runs). */
s32 portVideoShowFps(void)
{
    static int env = -1;

    if (env < 0)
    {
        const char *e = SDL_getenv("GE_SHOWFPS");
        env = (e != NULL) ? (SDL_atoi(e) != 0 ? 1 : 2) : 0;
    }
    if (env == 1)
    {
        return 1;
    }
    if (env == 2)
    {
        return 0;
    }
    return s_CfgShowFps;
}

f32 portWideFactor(void); /* defined below */

/**
 * Wide factor of the frame being BUILT right now: portWideFactor() when a
 * mission tick freshly asserted a wide 3D view, 1.0 otherwise (frontend
 * and cutscene frames present pinned). For game code that must widen an
 * auxiliary projection (watch-page item/controller perspectives, gauge
 * ortho) to keep its tri-based 2D inside the 4:3 band - the interpreter
 * pins texrects only.
 */
f32 portFrameWideFactor(void)
{
    return videoSceneWideFresh() ? portWideFactor() : 1.0f;
}

/** Per-viewport MP pin: the interpreter's CENTER pin compresses 2D about the current 3D viewport's
 * centre (gfx_pc.cpp s_GeViewportCenterNdc), so 3P/4P quadrants need no
 * anamorphic fallback. bondview2.c consults this when declaring MP-ness;
 * GE_MPPIN=0 restores the old anamorphic quadrants. The weak 0 in
 * libultra_shim.c keeps headless sims on the old identity math. */
s32 portMpPinActive(void)
{
    extern char *getenv(const char *);
    static s32 want = -1;

    if (want < 0)
    {
        const char *e = getenv("GE_MPPIN");
        want = !(e != NULL && e[0] == '0');
    }
    return want;
}

/** How much wider than 4:3 the window is (1.0 = not wider / disabled). */
f32 portWideFactor(void)
{
    f32 f;

    if (!s_CfgWidescreen || !s_VideoInitDone || gfx_current_window_dimensions.height <= 0
        || portPillarboxFrame())   /* a pillarboxed frame IS a 4:3 window */
    {
        return 1.0f;
    }
    f = ((f32)gfx_current_window_dimensions.width / (f32)gfx_current_window_dimensions.height) * 0.75f;
    if (f < 1.0f)
    {
        f = 1.0f; /* window narrower than 4:3: keep authentic */
    }
    if (f > 6.0f)
    {   /* Sanity cap only (8:1): a lower cap clamped the watch / gauge projections but
         * not the texrect pin or mouse band, so the watch's hit boxes drifted
         * off what was drawn. */
        f = 6.0f;
    }
    return f;
}

/**
 * Map the mouse (virtual absolute, window px) into menu-cursor coordinates.
 * Menus render pinned to a centered 4:3 band in widescreen, so the map goes
 * through that band. Returns 0 when the mouse hasn't moved (stick keeps
 * control) or input is unavailable.
 */
s32 portMouseMenuPos(f32 *gx, f32 *gy, f32 left, f32 top, f32 width, f32 height)
{
    extern void portInputMouseWindowPos(s32 *x, s32 *y, s32 *moved);
    extern s32 portInputMouseMovedThisTick(void);
    s32 mx, my, moved;
    f32 winw, winh, bandw, bandl;

    if (!s_VideoInitDone)
    {
        return 0;
    }
    portInputMouseWindowPos(&mx, &my, &moved);
    if (!moved && !portInputMouseMovedThisTick())
    {
        return 0;
    }
    winw = videoRealWinW();
    winh = (f32)gfx_current_window_dimensions.height;
    if (winw <= 0.0f || winh <= 0.0f)
    {
        return 0;
    }
    bandw = winw;
    bandl = 0.0f;
    if (s_CfgWidescreen)
    {
        bandw = winh * (4.0f / 3.0f);
        if (bandw > winw)
        {
            bandw = winw;
        }
        bandl = (winw - bandw) * 0.5f;
    }
    if (gx) *gx = left + ((f32)mx - bandl) * width / bandw;
    if (gy) *gy = top + (f32)my * height / winh;
    return 1;
}

/**
 * Watch-menu cursor: the virtual mouse in NATIVE 320x240 band coordinates
 * (the same centred-4:3 map the frontend cursor uses, so it lines up with
 * the pinned menu 2D), reported every poll whether or not it moved.
 * Returns 0 when video/input is not up.
 */
static int sMouseMovedLatch; /* motion since portMouseWatchPos last read it (polls outrun the ticks) */

s32 portMouseWatchPos(f32 *nx, f32 *ny, s32 *moved)
{
    extern void portInputMouseWindowPos(s32 *x, s32 *y, s32 *moved);
    s32 mx, my, mv;
    f32 winw, winh, bandw, bandl;

    if (!s_VideoInitDone)
    {
        return 0;
    }
    portInputMouseWindowPos(&mx, &my, &mv);
    winw = videoRealWinW();
    winh = (f32)gfx_current_window_dimensions.height;
    if (winw <= 0.0f || winh <= 0.0f)
    {
        return 0;
    }
    bandw = winw;
    bandl = 0.0f;
    if (s_CfgWidescreen)
    {
        bandw = winh * (4.0f / 3.0f);
        if (bandw > winw)
        {
            bandw = winw;
        }
        bandl = (winw - bandw) * 0.5f;
    }
    if (nx) *nx = ((f32)mx - bandl) * 320.0f / bandw;
    if (ny) *ny = (f32)my * 240.0f / winh;
    if (moved)
    {
        *moved = mv || sMouseMovedLatch;
        sMouseMovedLatch = 0;
    }
    return 1;
}

/**
 * Inverse of portMouseMenuPos: park the pointer at menu-cursor position
 * (gx,gy) so subsequent mouse motion continues from a cursor the game
 * snapped somewhere. Same band math, run backwards.
 */
static int videoMenuToWindow(f32 gx, f32 gy, f32 left, f32 top, f32 width, f32 height, s32 *px, s32 *py)
{
    f32 winw, winh, bandw, bandl;

    if (!s_VideoInitDone || width <= 0.0f || height <= 0.0f)
    {
        return 0;
    }
    winw = videoRealWinW();
    winh = (f32)gfx_current_window_dimensions.height;
    if (winw <= 0.0f || winh <= 0.0f)
    {
        return 0;
    }
    bandw = winw;
    bandl = 0.0f;
    if (s_CfgWidescreen)
    {
        bandw = winh * (4.0f / 3.0f);
        if (bandw > winw)
        {
            bandw = winw;
        }
        bandl = (winw - bandw) * 0.5f;
    }
    *px = (s32)(bandl + (gx - left) * bandw / width);
    *py = (s32)((gy - top) * winh / height);
    return 1;
}

void portMouseMenuWarp(f32 gx, f32 gy, f32 left, f32 top, f32 width, f32 height)
{
    extern void portInputMouseWarp(s32 x, s32 y);
    s32 px, py;

    if (videoMenuToWindow(gx, gy, left, top, width, height, &px, &py))
    {
        portInputMouseWarp(px, py);
    }
}

/**
 * Clamp park: the menu clamped its cursor from (gx,gy) to (cx,cy). The pointer
 * is only pulled to the clamp on a move back toward the box along a clamped
 * axis (see input_pc.c s_PollX), so the mouse can still leave the window.
 */
void portMouseMenuPark(f32 gx, f32 gy, f32 cx, f32 cy, f32 left, f32 top, f32 width, f32 height)
{
    extern void portInputMousePark(s32 x, s32 y, s32 axes);
    s32 px, py;
    s32 axes = (cx != gx ? 1 : 0) | (cy != gy ? 2 : 0);

    if (videoMenuToWindow(cx, cy, left, top, width, height, &px, &py))
    {
        portInputMousePark(px, py, axes);
    }
}

/** [display] keys; returns 1 when consumed (called from the ini loop). */
static s32 videoClampPct(s32 v);   /* volume percent guard, defined with the accessors */
static s32 videoParseHexRGB(const char *v);
static void videoApplyVolumes(void);

/** The sim tick follows the frame cap: one rate knob, so uncapped fps means an uncapped tick and a
 * 120 fps cap means a 120 Hz tick.
 *
 * Two things this deliberately will not do:
 *  - Take the tick below 60. The engine's authored rate is 60, and the raised-tick work
 *    (docs/port/tick-and-field-accounting.md 13) only goes up; a 30 fps cap still gets 60 Hz.
 *  - Override tickhz=hw, the console's own frame pacing (an expert ini-only mode). */
static void videoPersistDerivedTick(void);
static int videoSetDisplayOption(const char *key, const char *value);

/* the room_cache that hw pacing switched off, put back when hw is left
 * (it is not a table row, so nothing else would until the next launch).
 * 0 = nothing saved. */
static int s_RoomCacheBeforeHw = 0;

static void videoRoomCacheAfterHw(void)
{
    if (s_RoomCacheBeforeHw != 0 && s_CfgTickHz != -1)
    {
        s_CfgRoomCache = s_RoomCacheBeforeHw;
        s_RoomCacheBeforeHw = 0;
        portConLogf("ge: video: hw pacing off - room_cache %d back on\n", s_CfgRoomCache);
        portRoomCacheSetMode(s_CfgRoomCache);
    }
}

static void videoCoupleTickToMaxFps(void)
{
    s32 want;

#ifdef GE_XBOX
    /* Not on the console: it runs the N64's one-frame-per-vblank cadence, and this coupling reaches
     * portTickSetHz without the tickhz handler's clamp to 60. tickhz=hw would
     * also force room_cache to 0. */
    if (s_CfgTickHz != 60)
    {
        s_CfgTickHz = 60;
        portTickSetHwPace(0);
        portTickSetHz(60);
    }
    return;
#endif

    if (s_CfgMaxFps == -1)
    {   /* maxfps=-1 = "hardware" on the menu: exactly what tickhz=hw does,
         * through the same setter. */
        if (s_CfgTickHz != -1)
        {
            videoSetDisplayOption("tickhz", "hw");
        }
        if (s_CfgTickLoadDone)
        {
            videoPersistDerivedTick(); /* a live change records itself at once, hw included */
        }
        return;
    }
    if (s_CfgTickHz == -1)
    {   /* hw pacing from an ini tickhz=hw: not ours to touch... unless the
         * menu just picked a real Max FPS, which is the player leaving it */
        if (s_CfgTickLoadDone)
        {
            portTickSetHwPace(0);
            s_CfgTickHz = -2;  /* no `want` equals it, so the coupling below always runs:
                                * 0 matched Uncapped and left hw's 60 Hz live */
            videoRoomCacheAfterHw();
        }
        else
        {
            return;
        }
    }
    want = (s_CfgMaxFps <= 0) ? 0 : (s_CfgMaxFps < 60) ? 60 : s_CfgMaxFps;
    if (want == s_CfgTickHz)
    {
        return;
    }
    s_CfgTickHz = want;
    portTickSetHwPace(0);
    portTickSetHz(s_CfgTickHz);
    if (s_CfgTickLoadDone)
    {
        videoPersistDerivedTick(); /* a live change records itself at once */
    }
}

/** Write the derived tick back once when an existing midas.ini disagrees, so a stale hand-set
 * tickhz does not mislead readers. A file without a tickhz line gets none.
 *
 * This includes writing the literal `hw` when maxfps is -1. It did not, until F3-A: the
 * function returned early on the resolved -1, so a session that ran hw pacing left whatever
 * numeric tickhz the file already had, and the file described a different mode from the one
 * that ran. That is the invariant docs/port/tick-and-field-accounting.md §11.3 states - the
 * writer must never emit a pair the loader resolves differently from what was running - and
 * it is what the loader's own tickhz=hw arm records as written (-1). */
static void videoPersistDerivedTick(void)
{
    char wasbuf[PORT_TICK_INI_BUF];
    const char *was = portTickIniValue(s_CfgTickAsWritten, wasbuf, sizeof(wasbuf));
    const char *val = portTickIniValue(s_CfgTickHz, s_CfgTickBuf, sizeof(s_CfgTickBuf));
    const char *keys[1];
    const char *vals[1];

    if (!s_CfgTickSeenInIni || s_CfgTickHz == s_CfgTickAsWritten || val == NULL)
    {
        return; /* no tickhz line to correct, already in step, or no text for this state */
    }
    keys[0] = "tickhz";
    vals[0] = val;
    /* Only claim the file was corrected if it was: portIniWriteStrs no-ops under GE_WINDOW /
     * GE_NOINISAVE, and recording the value anyway would leave the file stale for the rest of
     * the session with nothing that retries it. */
    if (portIniWriteStrs("[display]", keys, vals, 1))
    {
        portConLogf("ge: video: tickhz follows maxfps - midas.ini %s -> %s\n", was != NULL ? was : "(none)", val);
        s_CfgTickAsWritten = s_CfgTickHz;
    }
}

/* [display] classic_res .. classic_hitches: which parts Classic Mode applies
 * (the CLASSIC sheet, opened from VIDEO's middle tab). Bits 0..6 follow
 * port_options.c sClassicKeys (the settings it forces), 7 is the 4:3 pillars,
 * 8 the load hitches, 9 the composite filter, 10 the N64 AA (VI) edge
 * blend. All on by default = Classic
 * Mode as it always was. */
static const char *const s_ClassicPartKeys[] = {
    "classic_res", "classic_filter", "classic_aniso", "classic_pacing", "classic_msaa",
    "classic_mipmaps", "classic_filter2d", "classic_pillars", "classic_hitches",
    "classic_composite", "classic_n64aa",
};
#define CLASSIC_PART_COUNT ((int) (sizeof(s_ClassicPartKeys) / sizeof(s_ClassicPartKeys[0])))
#define CLASSIC_PART_MIPMAPS 5
#define CLASSIC_PART_PILLARS 7
#define CLASSIC_PART_HITCHES 8
#define CLASSIC_PART_COMPOSITE 9
#define CLASSIC_PART_N64AA 10
static int s_CfgClassicParts = (1 << CLASSIC_PART_COUNT) - 1;

static int videoClassicPartIndex(const char *key)
{
    int i;

    for (i = 0; i < CLASSIC_PART_COUNT; i++)
    {
        if (SDL_strcasecmp(key, s_ClassicPartKeys[i]) == 0)
        {
            return i;
        }
    }
    return -1;
}

/** 1 = Classic Mode applies part `i` (port_options.c, frametiming.c). */
s32 portClassicPart(s32 i)
{
    return (i >= 0 && i < CLASSIC_PART_COUNT) ? ((s_CfgClassicParts >> i) & 1) : 0;
}

/* Classic Mode is on and its N64 AA part too: the VI edge blend runs at the
 * mode's 4x floor (videoApplyAa).
 *
 * DECIDED (Phase 3): the PRESET here, not the bit. This answers "does the mode
 * promise the full N64 resolve", and the mode is the preset; the bit answers
 * "run the resolve", in videoApplyAa above. Reading the bit here would have
 * made the floor follow a render setting the preset may not even own (the
 * classic_n64aa part can be off), and the old observable result - preset on,
 * part on => 4x - is what this preserves. The helper survives the split for
 * that reason: it is the conjunction of two things that are now separate keys,
 * and naming it keeps videoApplyAa readable. */
static int videoClassicViOn(void)
{
    return s_CfgClassicMode && portClassicPart(CLASSIC_PART_N64AA);
}

/** 1 = the PRESET owns the AA bit: its classic_n64aa part is on, so turning
 *  Classic Mode on sets [display] n64_aa and turning it off clears it.
 *  port_options.c's classicSyncAaBit is the only caller, and this part is the
 *  only link between the two keys - the bit itself never touches the preset. */
s32 portClassicSetsN64Aa(void)
{
    return portClassicPart(CLASSIC_PART_N64AA);
}

/** 1 while Classic Mode is on ([display] classic_mode, the classic_toggle key's state).
 *  Read by the -n64_aa boot flag, which reports whether its apply took, and by
 *  the GE_TICKHZ clash report. Renamed from portVideoN64AaOn in the Phase 3
 *  split: the old name said "the AA is on" and returned the preset, which is
 *  how a render setting came to answer a mode question. */
s32 portClassicModeOn(void)
{
    return s_CfgClassicMode;
}

/** Load hitches are a Classic part; they still only happen under hardware pacing. */
s32 portClassicHitches(void)
{
    return portClassicPart(CLASSIC_PART_HITCHES);
}

/* [display] classic_saved=<low_res>,<filtering>,<anisotropy>,<maxfps>,<msaa>,<mipmaps>,<filter_2d>:
 * the values from before Classic Mode forced them, so turning it off restores
 * them (written by front.c portSheetApply; order = port_sheet_classic_keys).
 * Append-only: an older four-field line still reads. */
#define CLASSIC_SAVED_MAX 7
static s32 s_CfgClassicSaved[CLASSIC_SAVED_MAX];
static int s_CfgClassicSavedSet = 0;  /* how many fields are held, 0 = none */

/** Fills v[0..n) and returns n, the number of values held (0, 4 or 7). */
s32 portVideoClassicSavedGet(s32 *v)
{
    int i;

    for (i = 0; i < s_CfgClassicSavedSet; i++)
    {
        v[i] = s_CfgClassicSaved[i];
    }
    return s_CfgClassicSavedSet;
}

void portVideoClassicSavedSet(const s32 *v)
{
    char buf[96];
    const char *keys[1];
    const char *vals[1];
    int i;

    for (i = 0; i < CLASSIC_SAVED_MAX; i++)
    {
        s_CfgClassicSaved[i] = v[i];
    }
    s_CfgClassicSavedSet = CLASSIC_SAVED_MAX;
    SDL_snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d,%d", (int) v[0], (int) v[1], (int) v[2], (int) v[3],
                 (int) v[4], (int) v[5], (int) v[6]);
    keys[0] = "classic_saved";
    vals[0] = buf;
    portIniWriteStrs("[display]", keys, vals, 1);
}

/* [display] fullscreen: the 3-way display mode. Words or the legacy numbers;
 * anything unrecognised but non-zero reads as fullscreen, which is what the
 * old atoi did for every truthy value. */
static int videoDisplayMode(const char *value)
{
    if (SDL_strcasecmp(value, "windowed") == 0)  return VIDEO_FS_WINDOWED;
    if (SDL_strcasecmp(value, "fullscreen") == 0) return VIDEO_FS_BORDERLESS;
    if (SDL_strcasecmp(value, "borderless") == 0) return VIDEO_FS_BORDERLESS;
    if (SDL_strcasecmp(value, "exclusive") == 0) return VIDEO_FS_EXCLUSIVE;
    {
        int v = SDL_atoi(value);
        if (v <= 0) return VIDEO_FS_WINDOWED;
        if (v >= VIDEO_FS_EXCLUSIVE) return VIDEO_FS_EXCLUSIVE;
        return VIDEO_FS_BORDERLESS;
    }
}

/* [display] low_res: 0 off, 1 or "hw" = 240 (the original setting), else a
 * line count snapped to 240 / 480 / 720 / 1080 */
static int videoLowResLines(const char *value)
{
    int n;
    if (SDL_strcasecmp(value, "hw") == 0) return 240;
    if (SDL_strcasecmp(value, "off") == 0) return 0;
    n = SDL_atoi(value);
    if (n <= 0) return 0;
    if (n == 1 || n <= 240) return 240;
    if (n <= 480) return 480;
    if (n <= 720) return 720;
    return 1080;
}

/** The tick rate a `tickhz` text denotes, as written: -1 for hw pacing, else the number.
 *
 * One rule, used by both sides that need it - the handler below decides on it, and the ini
 * parse loop records the FILE's claim with it. They must agree: the writer compares the
 * recorded claim against the running rate, so a second spelling of "is this hw" would let them
 * drift and silently stop correcting the file. */
static int videoTickTextValue(const char *text)
{
    return (SDL_strcasecmp(text, "hw") == 0 || SDL_atoi(text) == -1) ? -1 : SDL_atoi(text);
}

static int videoSetDisplayOption(const char *key, const char *value)
{
    if (SDL_strcasecmp(key, "window_width") == 0)  { s_CfgWindowW = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "window_height") == 0) { s_CfgWindowH = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "window_x") == 0)       { s_CfgWindowX = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "window_y") == 0)       { s_CfgWindowY = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "window_display") == 0) { s_CfgWindowDisplay = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "exclusive_width") == 0)  { s_CfgExclW = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "exclusive_height") == 0) { s_CfgExclH = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "fullscreen") == 0)    { s_CfgFullscreen = videoDisplayMode(value); return 1; }
    if (SDL_strcasecmp(key, "widescreen") == 0)    { s_CfgWidescreen = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "scope_steady") == 0)  { s_CfgScopeSteady = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "msaa") == 0)          { s_CfgMsaa = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "anisotropy") == 0)    { s_CfgAnisotropy = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "filter_2d") == 0)     { s_CfgFilter2D = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "high_lod") == 0)      { s_CfgHighLod = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "low_res") == 0)       { s_CfgLowRes = videoLowResLines(value); return 1; }
    /* "mipmaps" is the key the menu overhaul settled on (spec 2.1);
     * "hw_mipmaps" is the older name and stays readable. */
    if (SDL_strcasecmp(key, "mipmaps") == 0
        || SDL_strcasecmp(key, "hw_mipmaps") == 0) { s_CfgMipmaps = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "n64_aa") == 0)        { s_CfgN64Aa = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "classic_mode") == 0)  { s_CfgClassicMode = SDL_atoi(value) != 0; return 1; }
    /* the one-time n64_aa -> classic_mode split marker: what it MEANS is
     * classic_split.h's decision, and videoLoadConfig is the only reader. It is
     * recognised here so an already-migrated file is not reported as an
     * unknown control on every boot. */
    if (SDL_strcasecmp(key, "classic_split") == 0) { return 1; }
    if (SDL_strcasecmp(key, "room_cache") == 0)    { s_CfgRoomCache = SDL_atoi(value); if (s_CfgRoomCache < 0 || s_CfgRoomCache > 2) s_CfgRoomCache = 0; if (s_CfgRoomCache == 0) { s_RoomCacheBeforeHw = 0; /* room_cache: an explicit 0 is not put back */ } if (s_CfgTickHz == -1 && s_CfgRoomCache != 0) { portConLogf("ge: video: tickhz=hw - room_cache forced 0\n"); s_RoomCacheBeforeHw = s_CfgRoomCache; s_CfgRoomCache = 0; } portRoomCacheSetMode(s_CfgRoomCache); return 1; }
#ifdef GE_XBOX
    if (SDL_strcasecmp(key, "xbox_mode") == 0)
    {
        if (SDL_strcasecmp(value, "480p") == 0)       s_CfgXboxMode = 0;
        else if (SDL_strcasecmp(value, "720p") == 0)  s_CfgXboxMode = 1;
        else if (SDL_strcasecmp(value, "1080i") == 0) s_CfgXboxMode = 2;
        else { s_CfgXboxMode = SDL_atoi(value); if (s_CfgXboxMode < 0 || s_CfgXboxMode > 2) s_CfgXboxMode = 0; }
        return 1;
    }
#endif
    if (SDL_strcasecmp(key, "tri_hoist") == 0)     { s_CfgTriHoist = SDL_atoi(value) != 0; gfx_tri_hoist = s_CfgTriHoist ? 1u : 0u; return 1; }
#ifdef GE_XBOX
    if (SDL_strcasecmp(key, "xbox_clip_xy") == 0)  { s_CfgXboxClipXy = SDL_atoi(value) != 0; gfx_xbox_clip_xy = s_CfgXboxClipXy ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_clip_near") == 0) { s_CfgXboxClipNear = SDL_atoi(value) != 0; gfx_xbox_clip_near = s_CfgXboxClipNear ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_gpu_cull") == 0)  { s_CfgXboxGpuCull = SDL_atoi(value); if (s_CfgXboxGpuCull < 0 || s_CfgXboxGpuCull > 2) s_CfgXboxGpuCull = 2; gfx_xbox_gpu_cull = (uint32_t)s_CfgXboxGpuCull; return 1; }
    if (SDL_strcasecmp(key, "xbox_vp_legacy") == 0) { s_CfgXboxVpLegacy = SDL_atoi(value) != 0; gfx_xbox_vp_legacy = s_CfgXboxVpLegacy ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_gpu_fullconst") == 0)  { s_CfgXboxGpuFullConst = SDL_atoi(value) != 0; gfx_xbox_gpu_fullconst = s_CfgXboxGpuFullConst ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_gpu_via_stream") == 0) { s_CfgXboxGpuViaStream = SDL_atoi(value) != 0; gfx_xbox_gpu_via_stream = s_CfgXboxGpuViaStream ? 1u : 0u; return 1; }
#endif
    if (SDL_strcasecmp(key, "xbox_gpu_lit") == 0)  { s_CfgXboxGpuLit = SDL_atoi(value) != 0; gfx_xbox_gpu_lit = s_CfgXboxGpuLit ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_part_skiptex") == 0) { s_CfgXboxPartSkipTex = SDL_atoi(value) != 0; gfx_xbox_part_skiptex = s_CfgXboxPartSkipTex ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_visits_max") == 0) { s_CfgXboxXluVisitsMax = SDL_atoi(value); if (s_CfgXboxXluVisitsMax < 0) s_CfgXboxXluVisitsMax = 0; gfx_xbox_xlu_visits_max = (uint32_t)s_CfgXboxXluVisitsMax; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_batch") == 0) { s_CfgXboxXluBatch = SDL_atoi(value) != 0; gfx_xbox_xlu_batch = s_CfgXboxXluBatch ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_retained") == 0) { s_CfgXboxXluRetained = SDL_atoi(value) != 0; gfx_xbox_xlu_retained = s_CfgXboxXluRetained ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_planar_units") == 0) { s_CfgXboxXluPlanar = SDL_atoi(value) != 0; gfx_xbox_xlu_planar_units = s_CfgXboxXluPlanar ? 1u : 0u; return 1; }
    if (SDL_strcasecmp(key, "fix_shootthrough") == 0) { s_CfgFixShootThrough = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "distance_fading") == 0) { s_CfgDistanceFading = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "true_enemy_rockets") == 0) { s_CfgTrueEnemyRockets = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "show_fps") == 0)      { s_CfgShowFps = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "classic_saved") == 0)
    {
        int f[CLASSIC_SAVED_MAX];
        int n = SDL_sscanf(value, "%d,%d,%d,%d,%d,%d,%d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]);
        int i;

        if (n == 4 || n == CLASSIC_SAVED_MAX)
        {
            for (i = 0; i < n; i++)
            {
                s_CfgClassicSaved[i] = f[i];
            }
            s_CfgClassicSavedSet = n;
        }
        return 1;
    }
    if (SDL_strcasecmp(key, "maxfps") == 0)        { s_CfgMaxFps = SDL_atoi(value); videoCoupleTickToMaxFps(); return 1; }
    if (SDL_strcasecmp(key, "vsync") == 0)         { s_CfgVsync = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "tickhz") == 0)        {
        /* "hw" / -1: the console's frame pacing on the 60 Hz sim
         * (frametiming.c portTickHwPace); room_cache is forced off with it
         * below - the fill model reads the interpreter's triangles.
         *
         * This handler does NOT record what midas.ini said (s_CfgTickAsWritten):
         * videoCoupleTickToMaxFps calls it with "hw" while the file is still
         * being read, so a recording here would claim the file said hw when it
         * said a number. The ini parse loop records the file's own line. */
        if (videoTickTextValue(value) == -1)
        {
#ifdef GE_XBOX
            /* the console runs the N64's 60 Hz cadence and never hw
             * pacing (videoCoupleTickToMaxFps) - and hw would switch the
             * retained path off for good. Like a number: clamp to 60, and
             * record it so the file's hw line is rewritten. */
            portConLogf("ge: xbox: tickhz=hw ignored - the console runs at 60 (one frame per vblank)\n");
            s_CfgTickHz = 60;
            portTickSetHwPace(0);
            portTickSetHz(60);
            return 1;
#endif
            s_CfgTickHz = -1;
            portTickSetHz(60);
            portTickSetHwPace(1);
            if (s_CfgRoomCache != 0)
            {
                portConLogf("ge: video: tickhz=hw - room_cache forced 0 (the fill model reads the interpreter's triangles)\n");
                s_RoomCacheBeforeHw = s_CfgRoomCache;
                s_CfgRoomCache = 0;
                portRoomCacheSetMode(0);
            }
            return 1;
        }
        portTickSetHwPace(0);
        s_CfgTickHz = SDL_atoi(value);
#ifdef GE_XBOX
        /* More than one gfx task per vblank queues two flips in pbkit's ring
         * and tears the display, so the console runs the N64's
         * one-frame-per-vblank 60 Hz cadence. */
        if (s_CfgTickHz != 60) {
            portConLogf("ge: xbox: tickhz=%d ignored - the console runs at 60 (one frame per vblank)\n", s_CfgTickHz);
            s_CfgTickHz = 60;
        }
#endif
        portTickSetHz(s_CfgTickHz);
        return 1;
    }
    if (SDL_strcasecmp(key, "render_thread") == 0) { s_CfgRenderThread = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "adzy") == 0)          { s_CfgAdzy = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "unlock_all") == 0)    { s_CfgUnlockAll = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "pause_pillars") == 0) { s_CfgPausePillars = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "hud_scale") == 0)     { s_CfgHudScale = SDL_atoi(value) != 0; return 1; }
    if (videoClassicPartIndex(key) >= 0)
    {
        const int bit = 1 << videoClassicPartIndex(key);

        s_CfgClassicParts = (SDL_atoi(value) != 0) ? (s_CfgClassicParts | bit) : (s_CfgClassicParts & ~bit);
        gfx_composite = (uint32_t) portClassicPart(CLASSIC_PART_COMPOSITE);
        gfx_vi_blend = (uint32_t) portClassicPart(CLASSIC_PART_N64AA);
        return 1;
    }
    if (SDL_strcasecmp(key, "health_wide") == 0)   { s_CfgHealthWide = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "hud_wide") == 0)
    {   /* the folder's HUD row = Rare's ratio option, the watch's "hud" row.
         * Global, like the six player options: every stage start loads the
         * save's ratio bit (file2.c), so this copy is laid back over it there,
         * or a folder change was lost at the first level */
        extern void set_screen_ratio(s32 ratio);   /* options.c: SCREEN_RATIO_NORMAL 0 / SCREEN_RATIO_16_9 1 */
        s_CfgHudWide = SDL_atoi(value) != 0 ? 1 : 0;
        set_screen_ratio(s_CfgHudWide);
        return 1;
    }
    if (SDL_strcasecmp(key, "always_crosshair") == 0) { s_CfgAlwaysCrosshair = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "shadow_alpha") == 0)
    {
        s_CfgShadowAlpha = SDL_atoi(value);
        if (s_CfgShadowAlpha < 0)   { s_CfgShadowAlpha = 0; }
        if (s_CfgShadowAlpha > 200) { s_CfgShadowAlpha = 200; }
        return 1;
    }
    if (SDL_strcasecmp(key, "mission_timer") == 0) { s_CfgMissionTimer = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "stereo_pan") == 0)    { s_CfgStereoPan = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "aim_lock") == 0)      { s_CfgAimLock = SDL_atoi(value); return 1; }
    if (SDL_strcasecmp(key, "mp_aim_lock") == 0)   { s_CfgMpAimLock = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_sight_on_screen") == 0) { s_CfgMpSight = SDL_atoi(value) != 0; return 1; }
    /* The key is mp_no_auto_aim now (On = no auto aim for all players); an
     * older mp_auto_aim (1 = allowed) is still read unless an mp_no_auto_aim
     * line has been seen. */
    if (SDL_strcasecmp(key, "mp_no_auto_aim") == 0) { s_CfgMpAutoAim = SDL_atoi(value) == 0; s_CfgMpNoAutoAimSeen = 1; return 1; }
    if (SDL_strcasecmp(key, "mp_auto_aim") == 0)   { if (!s_CfgMpNoAutoAimSeen) s_CfgMpAutoAim = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_show_names") == 0) { s_CfgMpShowNames = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_invisibility") == 0) { s_CfgMpInvisibility = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_dk_mode") == 0)    { s_CfgMpDkMode = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_tiny_players") == 0) { s_CfgMpTinyPlayers = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_paintball") == 0)  { s_CfgMpPaintball = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_no_radar") == 0)   { s_CfgMpNoRadar = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_turbo_mode") == 0) { s_CfgMpTurbo = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_unlimited_ammo") == 0) { s_CfgMpUnlimitedAmmo = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_hud_wide") == 0)   { s_CfgMpHudWide = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_pillarbox") == 0)  { s_CfgMpPillarbox = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_always_crosshair") == 0) { s_CfgMpAlwaysCrosshair = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_fix_shootthrough") == 0) { s_CfgMpFixShootThrough = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_spawn_weapon") == 0) { s_CfgMpSpawnWeapon = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_random_spawns") == 0) { s_CfgMpRandomSpawns = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_head_roll") == 0)  { s_CfgMpHeadRoll = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_unlimited_explosions") == 0) { s_CfgMpUnlimitedExplosions = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "unlimited_explosions") == 0) { s_CfgUnlimitedExplosions = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "watch_static") == 0) { s_CfgWatchStatic = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "fast_pause") == 0)   { s_CfgFastPause = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "armor_hud") == 0)    { s_CfgArmorHud = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "weapon_fov") == 0)   { s_CfgWeaponFov = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "setting_help") == 0) { s_CfgSettingHelp = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mouse_auto_aim") == 0) { s_CfgMouseAutoAim = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mute_unfocused") == 0) { s_CfgMuteUnfocused = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "composite") == 0)    { (void) value; return 1; }   /* a day-one ini line: Classic Mode owns it now */
    if (SDL_strcasecmp(key, "mp_ltk_health") == 0) { s_CfgMpLtkHealth = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_no_armor") == 0)   { s_CfgMpNoArmor = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "mp_friendly_fire") == 0) { s_CfgMpFriendlyFire = SDL_atoi(value) != 0; return 1; }
    /* head_roll is the key now (1 = vanilla). An older ini's no_head_roll is
     * honoured inverted, but a head_roll line always wins. */
    if (SDL_strcasecmp(key, "head_roll") == 0)     { s_CfgNoHeadRoll = SDL_atoi(value) == 0; s_CfgHeadRollKeySeen = 1; return 1; }
    if (SDL_strcasecmp(key, "no_head_roll") == 0)  { if (!s_CfgHeadRollKeySeen) s_CfgNoHeadRoll = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcasecmp(key, "p1_ends_match") == 0) { s_CfgMpP1EndsMatch = SDL_atoi(value); return 1; }
    /* store only - this function IS the midas.ini parser and runs long before
     * the sound system exists; portVideoOptionSet does the live apply */
    if (SDL_strncasecmp(key, "player", 6) == 0 && key[6] >= '1' && key[6] <= '4'
        && SDL_strcasecmp(key + 7, "_crosshair_color") == 0)
    {   /* a player's own crosshair (Player Options / profiles):
         * "-" or empty = follow crosshair_color, "default" = the vanilla red */
        const int p = key[6] - '1';

        s_CfgPlayerXhair[p] = (value[0] == '\0' || value[0] == '-') ? -2
                            : (SDL_strcasecmp(value, "default") == 0) ? -1 : videoParseHexRGB(value);
        if (s_CfgPlayerXhair[p] < -1)
        {
            s_CfgPlayerXhair[p] = -2;
        }
        return 1;
    }
    if (SDL_strcasecmp(key, "crosshair_color") == 0)
    {
        extern void portTexCrosshairRefresh(s32 reorig); /* port_textures.c: the colour lives in the sprite's texels */

        s_CfgCrosshairColor = videoParseHexRGB(value);
        portTexCrosshairRefresh(0);   /* no-op until the sprite has been loaded once (boot) */
        return 1;
    }
    if (SDL_strcasecmp(key, "music_volume") == 0)  { s_CfgMusicVolume = videoClampPct(SDL_atoi(value)); s_CfgVolumesSet = 1; return 1; }
    {
        const int po = videoPlayerOptIndex(key);
        if (po >= 0) { s_CfgPlayerOpt[po] = SDL_atoi(value) != 0; s_CfgPlayerOptsMask |= 1 << po; return 1; }
    }
    if (SDL_strcasecmp(key, "sfx_volume") == 0)    { s_CfgSfxVolume = videoClampPct(SDL_atoi(value)); s_CfgVolumesSet = 1; return 1; }
    if (SDL_strcasecmp(key, "hfov_wide") == 0)
    {   /* HORIZONTAL degrees on a 16:9 picture - the menus'
         * number and midas.ini's key. 90 is vanilla's 60 (so are 91 / 92). */
        float v = (float) videoFovAcross(SDL_atof(value), 9.0 / 16.0);

        if (SDL_atoi(value) == 90)  { v = 60.0f; }   /* 90 IS vanilla (videoHfovWide) */
        if (v > 59.5f && v < 60.5f) { v = 60.0f; }   /* 91 / 92: an ini from before 90 took the name */
        if (v < 40.0f)  { v = 40.0f; }
        if (v > 120.0f) { v = 120.0f; }
        s_CfgFov = v;
        s_FovWideRead = 1;
        return 1;
    }
    if (SDL_strcasecmp(key, "hfov") == 0 && s_FovWideRead)
    {
        return 1;   /* superseded by hfov_wide */
    }
    if (SDL_strcasecmp(key, "fov") == 0 && s_FovWideRead)
    {
        return 1;   /* superseded by hfov_wide */
    }
    if (SDL_strcasecmp(key, "hfov") == 0)
    {   /* older 4:3 key: HORIZONTAL degrees on the 4:3 picture, stored as the
         * vertical angle the projection wants (75 = vanilla's 60). */
        float v = (float) videoFovAcross(SDL_atof(value), 0.75);

        if (v > 59.5f && v < 60.5f) { v = 60.0f; }
        if (v < 40.0f)  { v = 40.0f; }
        if (v > 120.0f) { v = 120.0f; }
        s_CfgFov = v;
        return 1;
    }
    if (SDL_strcasecmp(key, "fov") == 0)
    {   /* the old key, VERTICAL degrees - an older midas.ini still reads */
        s_CfgFov = (float) SDL_atof(value);
        if (s_CfgFov < 40.0f)  { s_CfgFov = 40.0f; }
        if (s_CfgFov > 120.0f) { s_CfgFov = 120.0f; }
        return 1;
    }
    if (SDL_strcasecmp(key, "filtering") == 0)
    {
        SDL_strlcpy(s_CfgFiltering, value, sizeof(s_CfgFiltering));
        return 1;
    }
    return 0;
}

/* ==== Port Options bridge (port_options.c, in-game menus) ================
 *
 * Read / write a [display] key by name at runtime. The SET path is the
 * ini parser's own setter (videoSetDisplayOption) followed by whatever the
 * init-time code did with the value, so a menu change == a midas.ini edit +
 * restart for these keys. Only keys that are per-frame reads on the
 * consumer side are listed (MVP-A: show_fps, widescreen, filter_2d); a key
 * not listed returns 0 = "not available" and the row greys out.
 * See docs/port/port-options-menu.md.
 */
static s32 videoFilteringIndex(void)
{
    if (SDL_strcasecmp(s_CfgFiltering, "point") == 0)
    {
        return 0;
    }
    if (SDL_strcasecmp(s_CfgFiltering, "3point") == 0 || SDL_strcasecmp(s_CfgFiltering, "threepoint") == 0
        || SDL_strcasecmp(s_CfgFiltering, "three-point") == 0)
    {
        return 2;
    }
    if (SDL_strcasecmp(s_CfgFiltering, "trilinear") == 0)
    {
        return 3;
    }
    return 1;
}

/* gfx_mipmaps: 0 = no mip chains, 1 = chains on G_TL_LOD draws, 2 = chains on
 * every textured draw except 2D rects, the sky and non-LOD cut-outs
 * ("trilinear"). Mipmaps are on exactly while Classic Mode is; the ini's
 * mipmaps= no longer counts, and GE_HWMIPMAPS=0/1 overrides. */
static int s_MipmapsEnv = -1;   /* GE_HWMIPMAPS: -1 unset, else 0/1 */

static u32 videoMipmapMode(void)
{
    const int on = (s_MipmapsEnv >= 0) ? s_MipmapsEnv : (s_CfgClassicMode && portClassicPart(CLASSIC_PART_MIPMAPS));

    if (!on)
    {
        return 0u;
    }
    return (videoFilteringIndex() == 3) ? 2u : 1u;
}

/* live: the sampler latch re-sends on the next draw whose mip wish changed;
 * chains are generated on demand, no cache clear */
static void videoApplyMipmaps(void)
{
    const uint32_t mm = videoMipmapMode();

    if (mm != gfx_mipmaps && s_CfgRoomCache != 0)
    {   /* retained rooms / parts replay the mip state they were
         * captured with - void them (render thread, next frame) */
        portRoomCacheSetMode(s_CfgRoomCache);
    }
    gfx_mipmaps = mm;
}

static void videoApplyFiltering(void)
{
    switch (videoFilteringIndex())
    {
    case 0:  gfx_set_texture_filter(FILTER_NONE); break;
    case 2:  gfx_set_texture_filter(FILTER_THREE_POINT); break;
    default: gfx_set_texture_filter(FILTER_LINEAR); break; /* bilinear and trilinear */
    }
    gfx_mipmaps = videoMipmapMode();
}

/* Alt+Enter (gfx_sdl2) changes the window without touching the ini value;
 * mirror it so the page shows the truth and the next save records it. */
static void videoOnFullscreenChanged(bool on)
{
    /* keep whichever fullscreen KIND the ini asked for; Alt+Enter only says
     * on or off, and reading it as "borderless" would silently demote a
     * player who chose exclusive */
    if (!on)
    {
        s_CfgFullscreen = VIDEO_FS_WINDOWED;
    }
    else
    {
        /* gfx_sdl2's fullscreen_flag is the truth: set_fullscreen just used it
         * to pick exclusive vs borderless, and Alt+Enter never changes it, so
         * the kind survives the windowed state recorded above */
        s_CfgFullscreen = (gfx_sdl.get_fullscreen_flag_mode() == 1) ? VIDEO_FS_EXCLUSIVE : VIDEO_FS_BORDERLESS;
    }
}

#ifdef GE_XBOX
int portVideoXboxMode(void)
{
    return s_CfgXboxMode;
}
#endif

s32 portVideoOptionGet(const char *key, s32 *out)
{
    if (SDL_strcasecmp(key, "fullscreen") == 0)    { *out = s_CfgFullscreen;   return 1; }
    if (SDL_strcasecmp(key, "widescreen") == 0)    { *out = s_CfgWidescreen;   return 1; }
    if (SDL_strcasecmp(key, "vsync") == 0)         { *out = s_CfgVsync;        return 1; }
    if (SDL_strcasecmp(key, "maxfps") == 0)        { *out = s_CfgMaxFps;       return 1; }
    if (SDL_strcasecmp(key, "tickhz") == 0)        { *out = s_CfgTickHz;       return 1; }
    if (SDL_strcasecmp(key, "render_thread") == 0) { *out = s_CfgRenderThread; return 1; }
    if (SDL_strcasecmp(key, "filtering") == 0)     { *out = videoFilteringIndex(); return 1; }
    if (SDL_strcasecmp(key, "anisotropy") == 0)    { *out = (s_CfgAnisotropy >= 2) ? s_CfgAnisotropy : 0; return 1; }
    if (SDL_strcasecmp(key, "filter_2d") == 0)     { *out = s_CfgFilter2D;     return 1; }
    if (SDL_strcasecmp(key, "high_lod") == 0)      { *out = s_CfgHighLod;      return 1; }
    if (SDL_strcasecmp(key, "low_res") == 0)       { *out = s_CfgLowRes;       return 1; }
    if (SDL_strcasecmp(key, "mipmaps") == 0
        || SDL_strcasecmp(key, "hw_mipmaps") == 0)    { *out = s_CfgMipmaps;      return 1; }
    if (SDL_strcasecmp(key, "n64_aa") == 0)        { *out = s_CfgN64Aa;        return 1; }
    if (SDL_strcasecmp(key, "classic_mode") == 0)  { *out = s_CfgClassicMode;   return 1; }
    /* the CHOSEN sample count, not the effective one: n64_aa floors it at 4x
     * (videoApplyAa) and a row that silently showed 4 after you picked 2
     * would look broken. The n64 aa row above it says it needs MSAA. */
    if (SDL_strcasecmp(key, "msaa") == 0)          { *out = s_CfgMsaa;         return 1; }
    if (SDL_strcasecmp(key, "room_cache") == 0)    { *out = s_CfgRoomCache;    return 1; }
#ifdef GE_XBOX
    if (SDL_strcasecmp(key, "xbox_mode") == 0)     { *out = s_CfgXboxMode;     return 1; }
#endif
    if (SDL_strcasecmp(key, "tri_hoist") == 0)     { *out = s_CfgTriHoist;     return 1; }
#ifdef GE_XBOX
    if (SDL_strcasecmp(key, "xbox_clip_xy") == 0)  { *out = s_CfgXboxClipXy;   return 1; }
    if (SDL_strcasecmp(key, "xbox_clip_near") == 0) { *out = s_CfgXboxClipNear; return 1; }
    if (SDL_strcasecmp(key, "xbox_gpu_cull") == 0)  { *out = s_CfgXboxGpuCull;  return 1; }
    if (SDL_strcasecmp(key, "xbox_vp_legacy") == 0) { *out = s_CfgXboxVpLegacy; return 1; }
    if (SDL_strcasecmp(key, "xbox_gpu_fullconst") == 0)  { *out = s_CfgXboxGpuFullConst; return 1; }
    if (SDL_strcasecmp(key, "xbox_gpu_via_stream") == 0) { *out = s_CfgXboxGpuViaStream; return 1; }
#endif
    if (SDL_strcasecmp(key, "xbox_gpu_lit") == 0)  { *out = s_CfgXboxGpuLit; return 1; }
    if (SDL_strcasecmp(key, "xbox_part_skiptex") == 0) { *out = s_CfgXboxPartSkipTex; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_visits_max") == 0) { *out = s_CfgXboxXluVisitsMax; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_batch") == 0) { *out = s_CfgXboxXluBatch; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_retained") == 0) { *out = s_CfgXboxXluRetained; return 1; }
    if (SDL_strcasecmp(key, "xbox_xlu_planar_units") == 0) { *out = s_CfgXboxXluPlanar; return 1; }
    if (SDL_strcasecmp(key, "fov") == 0)           { *out = (s32)(s_CfgFov + 0.5f); return 1; }
    if (SDL_strcasecmp(key, "hfov") == 0)          { *out = videoHfov(); return 1; }
    if (SDL_strcasecmp(key, "hfov_wide") == 0)     { *out = videoHfovWide(); return 1; }
    if (SDL_strcasecmp(key, "music_volume") == 0)  { *out = s_CfgMusicVolume;  return 1; }
    if (SDL_strcasecmp(key, "sfx_volume") == 0)    { *out = s_CfgSfxVolume;    return 1; }
    if (SDL_strcasecmp(key, "show_fps") == 0)      { *out = s_CfgShowFps;      return 1; }
    if (SDL_strcasecmp(key, "pause_pillars") == 0) { *out = s_CfgPausePillars; return 1; }
    if (SDL_strcasecmp(key, "hud_scale") == 0)     { *out = s_CfgHudScale; return 1; }
    if (videoClassicPartIndex(key) >= 0)           { *out = portClassicPart(videoClassicPartIndex(key)); return 1; }
    if (SDL_strcasecmp(key, "health_wide") == 0)   { *out = s_CfgHealthWide; return 1; }
    if (SDL_strcasecmp(key, "hud_wide") == 0)      { extern s32 get_screen_ratio(void); *out = (s_CfgHudWide >= 0) ? s_CfgHudWide : get_screen_ratio() != 0; return 1; }
    if (SDL_strcasecmp(key, "always_crosshair") == 0) { *out = s_CfgAlwaysCrosshair; return 1; }
    if (SDL_strcasecmp(key, "shadow_alpha") == 0)     { *out = s_CfgShadowAlpha; return 1; }
    if (SDL_strcasecmp(key, "mission_timer") == 0) { *out = s_CfgMissionTimer; return 1; }
    if (SDL_strcasecmp(key, "stereo_pan") == 0)    { *out = s_CfgStereoPan;    return 1; }
    if (SDL_strcasecmp(key, "aim_lock") == 0)      { *out = s_CfgAimLock; return 1; }
    if (SDL_strcasecmp(key, "mp_aim_lock") == 0)   { *out = s_CfgMpAimLock; return 1; }
    if (SDL_strcasecmp(key, "mp_sight_on_screen") == 0) { *out = s_CfgMpSight; return 1; }
    if (SDL_strcasecmp(key, "mp_no_auto_aim") == 0) { *out = s_CfgMpAutoAim == 0; return 1; }
    if (SDL_strcasecmp(key, "mp_auto_aim") == 0)   { *out = s_CfgMpAutoAim; return 1; }
    if (SDL_strcasecmp(key, "mp_show_names") == 0) { *out = s_CfgMpShowNames; return 1; }
    if (SDL_strcasecmp(key, "mp_invisibility") == 0) { *out = s_CfgMpInvisibility; return 1; }
    if (SDL_strcasecmp(key, "mp_dk_mode") == 0)    { *out = s_CfgMpDkMode; return 1; }
    if (SDL_strcasecmp(key, "mp_tiny_players") == 0) { *out = s_CfgMpTinyPlayers; return 1; }
    if (SDL_strcasecmp(key, "mp_paintball") == 0)  { *out = s_CfgMpPaintball; return 1; }
    if (SDL_strcasecmp(key, "mp_no_radar") == 0)   { *out = s_CfgMpNoRadar; return 1; }
    if (SDL_strcasecmp(key, "mp_turbo_mode") == 0) { *out = s_CfgMpTurbo; return 1; }
    if (SDL_strcasecmp(key, "mp_unlimited_ammo") == 0) { *out = s_CfgMpUnlimitedAmmo; return 1; }
    if (SDL_strcasecmp(key, "mp_hud_wide") == 0)   { *out = s_CfgMpHudWide; return 1; }
    if (SDL_strcasecmp(key, "mp_pillarbox") == 0)  { *out = s_CfgMpPillarbox; return 1; }
    if (SDL_strcasecmp(key, "mp_always_crosshair") == 0) { *out = s_CfgMpAlwaysCrosshair; return 1; }
    if (SDL_strcasecmp(key, "mp_fix_shootthrough") == 0) { *out = s_CfgMpFixShootThrough; return 1; }
    if (SDL_strcasecmp(key, "mp_spawn_weapon") == 0) { *out = s_CfgMpSpawnWeapon; return 1; }
    if (SDL_strcasecmp(key, "mp_random_spawns") == 0) { *out = s_CfgMpRandomSpawns; return 1; }
    if (SDL_strcasecmp(key, "mp_head_roll") == 0)  { *out = s_CfgMpHeadRoll; return 1; }
    if (SDL_strcasecmp(key, "mp_unlimited_explosions") == 0) { *out = s_CfgMpUnlimitedExplosions; return 1; }
    if (SDL_strcasecmp(key, "unlimited_explosions") == 0) { *out = s_CfgUnlimitedExplosions; return 1; }
    if (SDL_strcasecmp(key, "watch_static") == 0) { *out = s_CfgWatchStatic; return 1; }
    if (SDL_strcasecmp(key, "fast_pause") == 0)   { *out = s_CfgFastPause; return 1; }
    if (SDL_strcasecmp(key, "armor_hud") == 0)    { *out = s_CfgArmorHud; return 1; }
    if (SDL_strcasecmp(key, "weapon_fov") == 0)   { *out = s_CfgWeaponFov; return 1; }
    if (SDL_strcasecmp(key, "setting_help") == 0) { *out = s_CfgSettingHelp; return 1; }
    if (SDL_strcasecmp(key, "mouse_auto_aim") == 0) { *out = s_CfgMouseAutoAim; return 1; }
    if (SDL_strcasecmp(key, "mute_unfocused") == 0) { *out = s_CfgMuteUnfocused; return 1; }
    if (SDL_strcasecmp(key, "mp_ltk_health") == 0) { *out = s_CfgMpLtkHealth; return 1; }
    if (SDL_strcasecmp(key, "mp_no_armor") == 0)   { *out = s_CfgMpNoArmor; return 1; }
    if (SDL_strcasecmp(key, "mp_friendly_fire") == 0) { *out = s_CfgMpFriendlyFire; return 1; }
    if (SDL_strcasecmp(key, "head_roll") == 0)     { *out = s_CfgNoHeadRoll == 0; return 1; }
    if (SDL_strcasecmp(key, "no_head_roll") == 0)  { *out = s_CfgNoHeadRoll; return 1; }
    if (SDL_strcasecmp(key, "p1_ends_match") == 0) { *out = s_CfgMpP1EndsMatch; return 1; }
    if (SDL_strcasecmp(key, "scope_steady") == 0)  { *out = s_CfgScopeSteady;  return 1; }
    {
        const int po = videoPlayerOptIndex(key);
        if (po >= 0) { *out = s_CfgPlayerOpt[po]; return 1; }
    }
    if (SDL_strcasecmp(key, "distance_fading") == 0) { *out = s_CfgDistanceFading; return 1; }
    if (SDL_strcasecmp(key, "fix_shootthrough") == 0) { *out = s_CfgFixShootThrough; return 1; }
    if (SDL_strcasecmp(key, "true_enemy_rockets") == 0) { *out = s_CfgTrueEnemyRockets; return 1; }
    if (SDL_strcasecmp(key, "adzy") == 0)          { *out = s_CfgAdzy;         return 1; }
    if (SDL_strcasecmp(key, "unlock_all") == 0)    { *out = s_CfgUnlockAll;    return 1; }
    return 0;
}

/* exclusive's own mode, live and in midas.ini (window_* stays the window's) */
static void videoExclusiveSizeSet(int w, int h)
{
    static const char *const keys[] = { "exclusive_width", "exclusive_height" };
    char ws[16], hs[16];
    const char *vals[2];

    s_CfgExclW = w;
    s_CfgExclH = h;
    snprintf(ws, sizeof(ws), "%d", w);
    snprintf(hs, sizeof(hs), "%d", h);
    vals[0] = ws;
    vals[1] = hs;
    portIniWriteStrs("[display]", keys, vals, 2);
    s_CfgExclusiveResPicked = 1;
}

s32 portVideoOptionSet(const char *key, s32 value)
{
    if (SDL_strcasecmp(key, "shadow_alpha") == 0)     { s_CfgShadowAlpha = (value < 0) ? 0 : (value > 200) ? 200 : value; return 1; }
    char buf[16];
    s32 dummy;

    if (!portVideoOptionGet(key, &dummy))
    {
        return 0; /* not a live key */
    }
    if (SDL_strcasecmp(key, "filtering") == 0)
    {
        SDL_strlcpy(buf, (value == 0) ? "point" : (value == 2) ? "3point"
                       : (value == 3) ? "trilinear" : "bilinear", sizeof(buf));
    }
    else
    {
        SDL_snprintf(buf, sizeof(buf), "%d", (int) value);
    }
    if (!videoSetDisplayOption(key, buf))
    {
        return 0;
    }
    if (SDL_strcasecmp(key, "music_volume") == 0 || SDL_strcasecmp(key, "sfx_volume") == 0)
    {
        videoApplyVolumes(); /* heard immediately; only ever called in-game */
        return 1;
    }
    if (videoPlayerOptIndex(key) >= 0)
    {   /* a menu row changed one: the live table follows at once */
        extern void portPlayerOptsApply(void); /* options.c */

        portPlayerOptsApply();
        return 1;
    }
    if (!s_VideoInitDone)
    {
        return 1; /* gfx_init picks the statics up */
    }
    /* init-time consequences, replayed live (mirrors the gfx_init block) */
    if (SDL_strcasecmp(key, "filter_2d") == 0)
    {
        gfx_filter_2d = s_CfgFilter2D != 0;
    }
    else if (SDL_strcasecmp(key, "low_res") == 0)
    {
        /* nothing live to do: videoStartFrame gates gfx_low_res per frame
         * from s_CfgLowRes (levels only, never the menus) */
    }
    else if (SDL_strcasecmp(key, "tri_hoist") == 0)
    {
        gfx_tri_hoist = s_CfgTriHoist ? 1u : 0u;
    }
#ifdef GE_XBOX
    else if (SDL_strcasecmp(key, "xbox_clip_xy") == 0)
    {
        gfx_xbox_clip_xy = s_CfgXboxClipXy ? 1u : 0u;
    }
    else if (SDL_strcasecmp(key, "xbox_clip_near") == 0)
    {
        gfx_xbox_clip_near = s_CfgXboxClipNear ? 1u : 0u;
    }
    else if (SDL_strcasecmp(key, "xbox_gpu_cull") == 0)
    {
        gfx_xbox_gpu_cull = (uint32_t)s_CfgXboxGpuCull;
    }
#endif
#ifdef GE_XBOX
    else if (SDL_strcasecmp(key, "xbox_mode") == 0)
    {
        gfx_xbox_wm_request_mode(s_CfgXboxMode); /* applied between frames by the window manager */
    }
#endif
    else if (SDL_strcasecmp(key, "room_cache") == 0)
    {
        portRoomCacheSetMode(s_CfgRoomCache); /* live: the render thread clears + switches at its next frame */
    }
    else if (SDL_strcasecmp(key, "n64_aa") == 0 || SDL_strcasecmp(key, "msaa") == 0)
    {
        videoApplyAa(); /* live: gfx_start_frame re-allocates the level framebuffer on the change (render thread: at its next frame start) */
    }
    else if (SDL_strcasecmp(key, "classic_mode") == 0)
    {
        /* The preset owns the MSAA floor (videoApplyAa) AND the mipmap mode
         * (videoMipmapMode), so one key here covers both. The bit's own arm
         * above stopped refreshing the mipmaps at the split: they no longer
         * depend on it, and the choke point writes classic_mode before the bit
         * it syncs, so the mipmaps are already right by the time n64_aa lands. */
        videoApplyAa();
        videoApplyMipmaps();
    }
    else if (videoClassicPartIndex(key) == CLASSIC_PART_N64AA)
    {
        videoApplyAa();
    }
    else if (videoClassicPartIndex(key) == CLASSIC_PART_MIPMAPS)
    {
        videoApplyMipmaps();
    }
    else if (SDL_strcasecmp(key, "filtering") == 0)
    {
        videoApplyFiltering();
    }
    else if (SDL_strcasecmp(key, "anisotropy") == 0)
    {
        extern void gfx_texture_cache_clear_c(void); /* gfx_pc.cpp: deferred to the render thread when it runs */

        gfx_anisotropy = (s_CfgAnisotropy >= 2) ? ((s_CfgAnisotropy > 16) ? 16u : (uint32_t) s_CfgAnisotropy) : 1u;
        gfx_texture_cache_clear_c(); /* the sampler is baked at upload */
    }
    else if (SDL_strcasecmp(key, "vsync") == 0)
    {
        gfx_set_vsync(s_CfgVsync);
    }
    else if (SDL_strcasecmp(key, "maxfps") == 0 || SDL_strcasecmp(key, "tickhz") == 0)
    {
        gfx_set_target_fps((portTickHz() != 60 || s_CfgMaxFps < 0) ? 0 : s_CfgMaxFps); /* -1 = hw pacing paces itself */
    }
    else if (SDL_strcasecmp(key, "fullscreen") == 0)
    {
        /* Order matters: the flag decides which path set_fullscreen takes, so
         * pick it first. Flipping it while fullscreen makes gfx_sdl2 re-enter
         * and write WINDOWED over the request, so hold the request and put it
         * back. */
        const int want = s_CfgFullscreen;
        /* each mode keeps its own size. Read before the switch: in a
         * window or borderless the current display mode IS the desktop's. */
        const int wasExclusive = gfx_sdl.get_fullscreen_state() && gfx_sdl.get_fullscreen_flag_mode() != 0;
        int deskw = 0, deskh = 0;
        const int haveDesk = gfx_sdl.get_current_display_mode(&deskw, &deskh) && deskw > 0 && deskh > 0;

        gfx_sdl.set_fullscreen_exclusive(want == VIDEO_FS_EXCLUSIVE);
        s_CfgFullscreen = want;
        gfx_sdl.set_fullscreen(want != VIDEO_FS_WINDOWED);
        if (want == VIDEO_FS_EXCLUSIVE && !wasExclusive)
        {
            if (s_CfgExclW > 0 && s_CfgExclH > 0)
            {   /* exclusive's own pick (SDL would otherwise keep whatever mode
                 * it last had, or the desktop one from borderless) */
                gfx_sdl.set_closest_resolution(s_CfgExclW, s_CfgExclH, true);
                gfx_sdl.set_fullscreen(true);
            }
            else if (haveDesk)
            {   /* never picked: the desktop mode, remembered as its own */
                gfx_sdl.set_closest_resolution(deskw, deskh, true);
                gfx_sdl.set_fullscreen(true);
                videoExclusiveSizeSet(deskw, deskh);
            }
        }
        else if (want == VIDEO_FS_WINDOWED && wasExclusive && s_CfgWindowW > 0 && s_CfgWindowH > 0)
        {   /* back to the WINDOW's own size, not the mode exclusive was in */
            int32_t x = 0, y = 0;

            gfx_sdl.get_centered_positions(s_CfgWindowW, s_CfgWindowH, &x, &y);
            gfx_sdl.set_dimensions((uint32_t) s_CfgWindowW, (uint32_t) s_CfgWindowH, x, y);
        }
    }
    return 1;
}

/* ==== keyboard -> N64 pad (midas.ini) =======================================
 *
 * Bindings live in midas.ini next to the exe, section [controls]: one line per
 * N64 control, value = up to 4 comma-separated SDL key names (the strings
 * SDL_GetScancodeName returns: "W", "Up", "Space", "Return", "Keypad Enter",
 * "Left Shift"...; the comma key is "Comma" / "Keypad Comma", sSepKeyNames).
 * Scancodes, not keycodes - WASD stays the same physical
 * cluster on any keyboard layout. A missing midas.ini is written out with the
 * defaults below so the user has a file to edit; a present-but-empty value
 * unbinds that control. Mouse left button is always START (bring-up remote
 * control; not configurable yet).
 *
 * The analog stick is 4 digital slots at full deflection +/-80 (hardware
 * OEM sticks max out around there); opposite directions cancel to 0.
 */
#define BIND_MAX_KEYS 4

/* Mouse buttons ride in the scancode bind slots at codes SDL will never
 * produce: SDL_NUM_SCANCODES + button index. midas.ini names them "Mouse Left",
 * "Mouse Right", "Mouse Middle", "Mouse X1", "Mouse X2". */
#define BIND_MOUSE_BASE  SDL_NUM_SCANCODES
#define BIND_MOUSE_COUNT 7
#define BIND_MOUSE_BUTTONS 5   /* the first five are held buttons */
#define BIND_WHEEL_UP   5      /* "Mouse Wheel Up"/"Down": one-poll pulses */
#define BIND_WHEEL_DOWN 6
static const char *sMouseBindNames[BIND_MOUSE_COUNT] = {
    "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse X1", "Mouse X2",
    "Mouse Wheel Up", "Mouse Wheel Down"
}; /* index order matches SDL_BUTTON_LEFT..X2 (1..5), then the wheel */

/* The two SDL key names containing the midas.ini list separator ("," and "Keypad ,") are spelled out
 * instead; videoBindToText writes them and videoParseBindValue reads them
 * (GELauncher.cs KeyMap matches). */
static const struct { SDL_Scancode sc; const char *name; } sSepKeyNames[] = {
    { SDL_SCANCODE_COMMA,    "Comma" },
    { SDL_SCANCODE_KP_COMMA, "Keypad Comma" },
    /* keys SDL2 leaves UNNAMED (SDL_GetScancodeName gives ""), so a
     * capture of one read as the Escape cancel and no spelling existed: the
     * ISO key left of Z, and the JIS / Korean / ABNT extra keys */
#ifndef GE_XBOX /* no keyboard on the console */
    { SDL_SCANCODE_NONUSBACKSLASH, "NonUS Backslash" },
    { SDL_SCANCODE_INTERNATIONAL1, "International1" },
    { SDL_SCANCODE_INTERNATIONAL2, "International2" },
    { SDL_SCANCODE_INTERNATIONAL3, "International3" },
    { SDL_SCANCODE_INTERNATIONAL4, "International4" },
    { SDL_SCANCODE_INTERNATIONAL5, "International5" },
    { SDL_SCANCODE_INTERNATIONAL6, "International6" },
    { SDL_SCANCODE_INTERNATIONAL7, "International7" },
    { SDL_SCANCODE_INTERNATIONAL8, "International8" },
    { SDL_SCANCODE_INTERNATIONAL9, "International9" },
    { SDL_SCANCODE_LANG1, "Lang1" },
    { SDL_SCANCODE_LANG2, "Lang2" },
    { SDL_SCANCODE_LANG3, "Lang3" },
    { SDL_SCANCODE_LANG4, "Lang4" },
    { SDL_SCANCODE_LANG5, "Lang5" },
    { SDL_SCANCODE_LANG6, "Lang6" },
    { SDL_SCANCODE_LANG7, "Lang7" },
    { SDL_SCANCODE_LANG8, "Lang8" },
    { SDL_SCANCODE_LANG9, "Lang9" },
#endif
};
#define BIND_SEPKEY_COUNT ((int) (sizeof(sSepKeyNames) / sizeof(sSepKeyNames[0])))

/* Wheel notches arrive as SDL_MOUSEWHEEL events (input_pc.c's event watch
 * forwards them here); each notch is "down" for exactly ONE pad poll so
 * the game sees a clean press edge (joyGetButtonsPressedThisFrame) - two
 * quick notches = two presses on two polls. */
static int sWheelUpNotches, sWheelDownNotches;
static int sWheelUpNow, sWheelDownNow;
static int sWheelUpPrev, sWheelDownPrev; /* last poll's Now: a notch queue must ALTERNATE to make edges */
static int sMouseAimPrev, sMouseAimLatched; /* portMouseAimPressed */
static int sMouseClickPrev, sMouseClickLatched, sMouseClickHeld; /* portMouseClick* (raw left button) */
static int sWheelLatch; /* portMouseWheelSteps: +up / -down notches since last read */
static u16 sMouseBindMask; /* pad buttons this poll that came from MOUSE codes (portMouseBindMask) */
static int sReturnStartPolled; /* a poll since the last tick consume saw START held by Return ALONE */
static int sReturnStartTick;   /* ...frozen for the tick now running (portReturnStartThisTick) */

void videoMouseWheel(int dy)
{
    if (dy > 0) sWheelUpNotches++;
    else if (dy < 0) sWheelDownNotches++;
}

/* Port suite binds (joy_port.c owns names/defaults/semantics; this file
 * only parses the key names and reports "down" per action each poll). */
/* Bigger than PORTACT_COUNT with room to append; the poll loop silently clips to it, so
 * videoBindCountCheck below complains if it is ever too small. */
#define PORT_BIND_MAX 32
static SDL_Scancode sPortBindings[PORT_BIND_MAX][BIND_MAX_KEYS];

/* [controls] mouse_unlock (default F1): the key that frees / re-grabs the
 * pointer. Edge taken in videoGetPad's bind poll. */
static int sMouseUnlockPrev;
static volatile int sMouseUnlockLatched;

/** 1 once per press of the mouse unlock key; consumed by the reader (input_pc.c). */
s32 portVideoMouseUnlockPressed(void)
{
    const s32 r = sMouseUnlockLatched;

    sMouseUnlockLatched = 0;
    return r;
}

/* [controls] classic_toggle (default Tab): flips Classic Mode in play. Edge
 * taken in videoGetPad's bind poll beside mouse_unlock; bondview2.c consumes. */
static int sClassicTogglePrev;
static volatile int sClassicToggleLatched;

/** 1 once per press of the classic mode toggle key; consumed by the reader. */
s32 portVideoClassicTogglePressed(void)
{
    const s32 r = sClassicToggleLatched;

    sClassicToggleLatched = 0;
    return r;
}

/** 1 = the mouse unlock bind has at least one key, so F1 is NOT a fixed key. */
s32 portVideoMouseUnlockBound(void)
{
    extern s32 portJoyPortActMouseUnlock(void);
    const int mu = (int) portJoyPortActMouseUnlock();
    int k;

    if (mu < 0 || mu >= PORT_BIND_MAX)
    {
        return 0;
    }
    for (k = 0; k < BIND_MAX_KEYS; k++)
    {
        if (sPortBindings[mu][k] != 0)
        {
            return 1;
        }
    }
    return 0;
}
extern s32 portJoyPortBindCount(void);

/** Say so LOUDLY if the action table has outgrown the bind array. */
static void videoBindCountCheck(void)
{
    static int done;

    if (!done && portJoyPortBindCount() > PORT_BIND_MAX)
    {
        done = 1;
        portConLogf("ge: input: %d port actions but PORT_BIND_MAX is %d - the last %d CANNOT be bound "
                "to a key; raise PORT_BIND_MAX in video.c\n",
                (int) portJoyPortBindCount(), (int) PORT_BIND_MAX,
                (int) (portJoyPortBindCount() - PORT_BIND_MAX));
    }
}
extern const char *portJoyPortBindName(s32 i);
extern const char *portJoyPortBindDefault(s32 i);
extern s32 portJoyPortSetOption(const char *key, const char *value);
extern s32 portInputOptionGet(const char *key, s32 *out);
extern s32 portJoyPortOptionGet(const char *key, s32 *out);
extern s32 portJoyPortAlwaysWalk(void);
extern void portJoyPortPoll(const u8 *down);
extern s32 portConIsOpen(void); /* port_console.c - the tilde console */
extern s32 portInputP1KbmMuted(void); /* input_pc.c - p1_hybrid off, a pad owns player 1 */

/* the keyboard's Look keys, as a stick in N64 units. Zero unless the player
 * bound them - the mouse is the default way a keyboard looks around. */
static s32 s_LookDigitalX;
static s32 s_LookDigitalY;

s32 portVideoLookDigital(s32 *x, s32 *y)
{
    if (x != NULL) *x = s_LookDigitalX;
    if (y != NULL) *y = s_LookDigitalY;
    return (s_LookDigitalX != 0) || (s_LookDigitalY != 0);
}

/* Blackout after a bind capture: the captured input is usually still held, so
 * it would fire at once (and repeat on a trigger or auto-repeating key). 120 ms
 * is about seven frames - long enough to let go, too short to feel like lag. */
#define PORT_BIND_COOLDOWN_MS 120
static Uint32 s_BindCooldownStart; /* start + flag, compared as a wrap-safe difference */
static int    s_BindCooldownOn;
/* what was down while the keyboard and mouse were blanked, kept blanked until
 * it is let go - videoGetPad, "A BLANK MUST NOT END IN A PRESS" */
static Uint8 sHeldOverKeys[SDL_NUM_SCANCODES];
static Uint8 sFiltKeys[SDL_NUM_SCANCODES];
static u32 sHeldOverMouse;
static int sHeldOverAny;

/** keep one key blanked until it is let go - gfx_sdl2.cpp's Alt+Enter,
 * whose Return is the shortcut's, not START / confirm. */
void portVideoHoldOverKey(s32 sc)
{
    if (sc > 0 && sc < SDL_NUM_SCANCODES)
    {
        sHeldOverKeys[sc] = 1;
        sHeldOverAny = 1;
    }
}

void portBindCooldownStart(void)
{
    s_BindCooldownStart = SDL_GetTicks();
    s_BindCooldownOn = 1;
}

s32 portBindCooldownActive(void)
{
    if (s_BindCooldownOn && (Uint32) (SDL_GetTicks() - s_BindCooldownStart) >= PORT_BIND_COOLDOWN_MS)
    {
        s_BindCooldownOn = 0;
    }
    return s_BindCooldownOn;
}
extern s32 portBondControlsLive(void); /* gameplay owns the input (not a menu, not the watch) */
extern s32 portP1ControlsLive(void);   /* ...asked about player 1 - the poll runs
                                        * outside any player's tick */
extern s32 portStageRunning(void);     /* front.c: the title stage's player 1
                                        * reads "live" over the menus */

/* one bind code held this poll? (keys = SDL keyboard state, mbuttons = mouse) */
static int videoCodeDown(SDL_Scancode sc, const Uint8 *keys, u32 mbuttons)
{
    if (sc == SDL_SCANCODE_UNKNOWN) return 0;
    if (sc >= BIND_MOUSE_BASE)
    {
        int m = (int) sc - BIND_MOUSE_BASE;
        if (m < BIND_MOUSE_BUTTONS) return (mbuttons & SDL_BUTTON(m + 1)) != 0;
        if (m == BIND_WHEEL_UP) return sWheelUpNow;
        if (m == BIND_WHEEL_DOWN) return sWheelDownNow;
        return 0;
    }
    return keys[sc] != 0;
}

/* Menu "mouse back": TRUE once per press of a MOUSE code
 * bound to the aim action (BIND_R, "Mouse Right" by default). Latched in
 * videoGetPad, cleared on read. Keyboard/pad R never set it - the frontend
 * turns this into a B press (front.c menu_init), the pad's own R is untouched. */
s32 portMouseAimPressed(void)
{
    s32 r = sMouseAimLatched;
    sMouseAimLatched = 0;
    return r;
}

/* Watch-menu mouse: raw LEFT button edge / level and the wheel notches, independent of
 * the midas.ini binds (the same click still lands as its bound button - the
 * watch layer in options.c eats the Z|A it would otherwise toggle). */
s32 portMouseClickPressed(void)
{
    s32 r = sMouseClickLatched;
    sMouseClickLatched = 0;
    return r;
}

s32 portMouseClickHeld(void)
{
    return sMouseClickHeld;
}

s32 portMouseWheelSteps(void)
{
    s32 r = sWheelLatch;
    sWheelLatch = 0;
    return r;
}

/* Pad button bits of the LAST poll that were driven by a mouse code (left /
 * right / middle / X1 / X2 / wheel binds). The watch mouse layer masks these
 * out of joyGetButtonsPressedThisFrame to detect pad/keyboard presses. */
u16 portMouseBindMask(void)
{
    return sMouseBindMask;
}

/* spec 6: Return keeps its start bind (confirm everywhere) but the two solo
 * watch toggles drop a START that Return alone produced. ORed over the same
 * poll window joyConsumeSamples ORs into buttonspressed, frozen once per tick
 * by joy.c joyConsumeSamplesWrapper. */
void portReturnStartTickConsume(void)
{
    sReturnStartTick = sReturnStartPolled;
    sReturnStartPolled = 0;
}

/** 1 = some sample of this tick had START held by the Return key alone. */
s32 portReturnStartThisTick(void)
{
    return sReturnStartTick;
}

/* [controls] marker - this file has been through the one-time Move-stick
 * upgrade (videoLoadConfig), so four empty analog_* are the player's choice */
#define VIDEO_MOVE_UPGRADE_KEY "move_stick_upgraded"

enum {
    BIND_A, BIND_B, BIND_Z, BIND_L, BIND_R, BIND_START,
    BIND_C_UP, BIND_C_DOWN, BIND_C_LEFT, BIND_C_RIGHT,
    BIND_DPAD_UP, BIND_DPAD_DOWN, BIND_DPAD_LEFT, BIND_DPAD_RIGHT,
    BIND_ANALOG_UP, BIND_ANALOG_DOWN, BIND_ANALOG_LEFT, BIND_ANALOG_RIGHT,
    /* The keyboard's own look stick. Look and movement were the same four ini
     * keys, so binding W to move forward also bound it to look up; a pad has
     * separate move_* and look_* rows, and now the keyboard does too. */
    BIND_LOOK_UP, BIND_LOOK_DOWN, BIND_LOOK_LEFT, BIND_LOOK_RIGHT,
    BIND_COUNT
};

static const struct {
    const char *name;    /* midas.ini key */
    u16 mask;            /* CONT_* bit; 0 = analog slot */
    const char *defkeys; /* default midas.ini value */
} sBindInfo[BIND_COUNT] = {
    /* defaults: A and B unbound in
     * favour of the Port suite (wheel = weapons, E = use), mouse fires/aims,
     * WASD on the C buttons (style 1.2 = digital movement), stick unbound
     * (mouse look). Port suite defaults live in joy_port.c sPortActs. */
    [BIND_A]            = { "a",            CONT_A,     "" },
    [BIND_B]            = { "b",            CONT_B,     "" },
    [BIND_Z]            = { "z",            CONT_G,     "Mouse Left" },
    [BIND_L]            = { "l",            CONT_L,     "" },
    [BIND_R]            = { "r",            CONT_R,     "Mouse Right" },
    [BIND_START]        = { "start",        CONT_START, "Return" },
    [BIND_C_UP]         = { "c_up",         CONT_E,     "W" },
    [BIND_C_DOWN]       = { "c_down",       CONT_D,     "S" },
    [BIND_C_LEFT]       = { "c_left",       CONT_C,     "A" },
    [BIND_C_RIGHT]      = { "c_right",      CONT_F,     "D" },
    [BIND_DPAD_UP]      = { "dpad_up",      CONT_UP,    "" },
    [BIND_DPAD_DOWN]    = { "dpad_down",    CONT_DOWN,  "" },
    [BIND_DPAD_LEFT]    = { "dpad_left",    CONT_LEFT,  "" },
    [BIND_DPAD_RIGHT]   = { "dpad_right",   CONT_RIGHT, "" },
    /* Movement is the Move stick: WASD reaches it directly
     * (bondviewProcessInput reads it for any non-N64 device), so movement works
     * on every control style. They stay on the C buttons too, because aim mode
     * still uses C buttons to lean and zoom; the port block clears their
     * movement and pitch outside aim mode so nothing counts twice. */
    [BIND_ANALOG_UP]    = { "analog_up",    0,          "W" },
    [BIND_ANALOG_DOWN]  = { "analog_down",  0,          "S" },
    [BIND_ANALOG_LEFT]  = { "analog_left",  0,          "A" },
    [BIND_ANALOG_RIGHT] = { "analog_right", 0,          "D" },
    /* Unbound by default: the mouse is how a keyboard player looks, and these
     * let someone look with the arrow keys without taking WASD. */
    [BIND_LOOK_UP]      = { "look_up",      0,          "" },
    [BIND_LOOK_DOWN]    = { "look_down",    0,          "" },
    [BIND_LOOK_LEFT]    = { "look_left",    0,          "" },
    [BIND_LOOK_RIGHT]   = { "look_right",   0,          "" },
};

static SDL_Scancode sBindings[BIND_COUNT][BIND_MAX_KEYS];

/* 1 = the key is one of the Move keys (analog_up / down / left / right) */
static int videoKeyIsMoveKey(SDL_Scancode sc)
{
    int b, k;

    if (sc == SDL_SCANCODE_UNKNOWN)
    {
        return 0;
    }
    for (b = BIND_ANALOG_UP; b <= BIND_ANALOG_RIGHT; b++)
    {
        for (k = 0; k < BIND_MAX_KEYS; k++)
        {
            if (sBindings[b][k] == sc)
            {
                return 1;
            }
        }
    }
    return 0;
}

/* trim leading/trailing spaces and tabs in place, return start */
static char *iniTrim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') s++;
    end = s + SDL_strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = '\0';
    return s;
}

/* "Return,Keypad Enter" -> up to BIND_MAX_KEYS scancodes (bindname = for the warning).
 * Returns how many names it could not read (the console's `set` refuses on >0). */
static int videoParseBindValue(char *value, SDL_Scancode *out, const char *bindname)
{
    int nkeys = 0;
    int nbad = 0;
    int blanks = 0; /* empty fields in a row: an old "," key name splits into TWO */
    char *tok = value;

    SDL_memset(out, 0, sizeof(SDL_Scancode) * BIND_MAX_KEYS); /* UNKNOWN = 0 */

    while (tok != NULL && nkeys < BIND_MAX_KEYS)
    {
        char *comma = SDL_strchr(tok, ',');
        char *name;
        if (comma != NULL) *comma = '\0';
        name = iniTrim(tok);
        if (name[0] == '\0')
        {   /* LEGACY: the comma key written as "," (every midas.ini's old
             * watch_prev=, default, launcher captures, "W,," from the watch)
             * always leaves exactly two empty fields side by side; a stray
             * single comma leaves one and still binds nothing. */
            if (++blanks == 2)
            {
                out[nkeys++] = SDL_SCANCODE_COMMA;
                blanks = 0;
            }
        }
        else
        {
            blanks = 0;
        }
        if (SDL_strcmp(name, "(unbound)") == 0)
        {   /* Self-healing: "(unbound)" is menu display text that an old bug
             * appended to real binds (e.g. analog_up=(unbound),W); skip it
             * silently, the next write drops it. */
            name[0] = '\0';
        }
        if (name[0] != '\0')
        {
            SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
            int m;
            for (m = 0; m < BIND_MOUSE_COUNT; m++)
            {
                if (SDL_strcasecmp(name, sMouseBindNames[m]) == 0)
                {
                    sc = (SDL_Scancode)(BIND_MOUSE_BASE + m);
                    break;
                }
            }
            if (sc == SDL_SCANCODE_UNKNOWN)
            {
                for (m = 0; m < BIND_SEPKEY_COUNT; m++)
                {
                    if (SDL_strcasecmp(name, sSepKeyNames[m].name) == 0)
                    {
                        sc = sSepKeyNames[m].sc;
                        break;
                    }
                }
            }
            if (sc == SDL_SCANCODE_UNKNOWN)
            {
                sc = SDL_GetScancodeFromName(name);
            }
            if (sc == SDL_SCANCODE_UNKNOWN)
            {
#ifndef GE_XBOX /* no keyboard on the console: every key name is unknown, by design */
                nbad++;
                portConLogf("ge: midas.ini: unknown key name \"%s\" for %s\n",
                        name, bindname);
#endif
            }
            else if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_GRAVE)
            {
                /* Esc and the console key cannot be bound, since a bind would
                 * fire alongside their fixed job. Counted bad so the console's
                 * set refuses them too. */
                nbad++;
                portConLogf("ge: midas.ini: \"%s\" is a fixed PC key and cannot be bound (%s), ignored\n",
                        name, bindname);
            }
            else if ((sc == (SDL_Scancode) (BIND_MOUSE_BASE + BIND_WHEEL_UP)
                      || sc == (SDL_Scancode) (BIND_MOUSE_BASE + BIND_WHEEL_DOWN))
                     && bindname != NULL
                     && (SDL_strcasecmp(bindname, "zoom_in") == 0 || SDL_strcasecmp(bindname, "zoom_out") == 0
                         || SDL_strcasecmp(bindname, "walk") == 0 || SDL_strcasecmp(bindname, "crouch_hold") == 0))
            {
                /* A wheel notch is down for one poll, but these actions are
                 * held (joy_port.c sPortActs oneshot 0), so they would do
                 * nothing useful; refused like the fixed keys, so the capture
                 * and console set refuse too. */
                nbad++;
                portConLogf("ge: midas.ini: the mouse wheel cannot hold %s (one-poll pulse), ignored\n", bindname);
            }
            else
            {
                out[nkeys++] = sc;
            }
        }
        tok = (comma != NULL) ? comma + 1 : NULL;
    }
    return nbad;
}

/* "Return,Keypad Enter" -> scancodes for one N64 control slot */
static void iniParseKeys(int bind, char *value)
{
    videoParseBindValue(value, sBindings[bind], sBindInfo[bind].name);
}

static void videoWriteDefaultIni(void)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    FILE *f = portRootFopen("midas.ini", "w");

    if (f == NULL)
    {
        return; /* read-only install dir: code defaults still apply */
    }
    /* no [controls]: keyboard and mouse binds live in Data/Devices/kbm.gedevice */
    fprintf(f, "[input]\n");
    /* always_walk is retired (binds replace it):
     * no longer written; an older midas.ini's line is read and ignored */
    (void) portJoyPortAlwaysWalk;
    {
        extern s32 portJoyPortDetonateUseReload(void);
        fprintf(f, "detonate_use_reload=%d\n", portJoyPortDetonateUseReload());
    }
    {
        extern s32 portJoyPortShootNextPrev(void);
        fprintf(f, "shoot_next_prev=%d\n", portJoyPortShootNextPrev());
    }
    {
        extern s32 portJoyPortNoAimCrouch(void);
        fprintf(f, "no_aim_crouch=%d\n", portJoyPortNoAimCrouch());
    }
    fprintf(f, "background_input=0\n");
    fprintf(f, "mouse=1\n");
    {   /* the live value, so the file can never drift from input_pc.c's
         * default again (it wrote 1 here after the default went to 0) */
        s32 v = 0;
        portInputOptionGet("smart_mouse_lock", &v);
        fprintf(f, "smart_mouse_lock=%d\n", (int) v);
    }
    fprintf(f, "p1_hybrid=1\n");
    /* mouse_mode no longer written (retired, direct only) */
    fprintf(f, "mouse_sens_x=2.5\n");
    fprintf(f, "mouse_sens_y=2.5\n");
    fprintf(f, "mouse_sens_aim=1.0\n");
    /* right_stick_c and twin_stick are no longer written (retired);
     * an older file's lines are read and ignored */
    fprintf(f, "deadzone=3277\n");        /* 10%: the Move stick */
    fprintf(f, "deadzone_look=3277\n");   /* 10%: the Look stick */
    fprintf(f, "trigger_threshold=3276\n"); /* 10% of the pull */
    fprintf(f, "stick_scale=1.0\n");
    fprintf(f, "pads=4\n");
    fprintf(f, "rumble=1\n");
    fprintf(f, "rawinput=1\n");
    fprintf(f, "pad1=auto\n");
    fprintf(f, "pad2=auto\n");
    fprintf(f, "pad3=auto\n");
    fprintf(f, "pad4=auto\n");
    fprintf(f, "dinput=0\n");
    fprintf(f, "\n");
    fprintf(f, "[display]\n");
    fprintf(f, "window_width=%d\n", s_CfgWindowW);
    fprintf(f, "window_height=%d\n", s_CfgWindowH);
    if (s_CfgWindowX != VIDEO_POS_UNSET && s_CfgWindowY != VIDEO_POS_UNSET)
    {
        fprintf(f, "window_x=%d\n", s_CfgWindowX);
        fprintf(f, "window_y=%d\n", s_CfgWindowY);
    }
    if (s_CfgWindowDisplay >= 0)
    {
        fprintf(f, "window_display=%d\n", s_CfgWindowDisplay);
    }
    fprintf(f, "fullscreen=%s\n", (s_CfgFullscreen == VIDEO_FS_WINDOWED) ? "windowed"
                                 : (s_CfgFullscreen == VIDEO_FS_EXCLUSIVE) ? "exclusive" : "fullscreen");
    fprintf(f, "widescreen=%d\n", s_CfgWidescreen);
    fprintf(f, "scope_steady=%d\n", s_CfgScopeSteady);
    {
        int i;
        for (i = 0; i < PLAYEROPT_COUNT; i++)
        {
            fprintf(f, "%s=%d\n", s_PlayerOptKeys[i], s_CfgPlayerOpt[i]);
        }
    }
    fprintf(f, "msaa=%d\n", s_CfgMsaa);
    fprintf(f, "filtering=%s\n", s_CfgFiltering);
    fprintf(f, "anisotropy=%d\n", s_CfgAnisotropy);
    fprintf(f, "filter_2d=%d\n", s_CfgFilter2D);
    fprintf(f, "low_res=%d\n", s_CfgLowRes);
    fprintf(f, "n64_aa=%d\n", s_CfgN64Aa ? 1 : 0);
    fprintf(f, "classic_mode=%d\n", s_CfgClassicMode ? 1 : 0);
    fprintf(f, "room_cache=%d\n", s_CfgRoomCache);
    fprintf(f, "tri_hoist=%d\n", s_CfgTriHoist ? 1 : 0);
    fprintf(f, "fix_shootthrough=%d\n", s_CfgFixShootThrough);
    fprintf(f, "distance_fading=%d\n", s_CfgDistanceFading);
    fprintf(f, "true_enemy_rockets=%d\n", s_CfgTrueEnemyRockets);
    fprintf(f, "hfov_wide=%d\n", (int) videoHfovWide());
    fprintf(f, "weapon_fov=%d\n", s_CfgWeaponFov);
    fprintf(f, "crosshair_color=default\n");
    fprintf(f, "music_volume=%d\n", s_CfgMusicVolume);
    fprintf(f, "sfx_volume=%d\n", s_CfgSfxVolume);
    fprintf(f, "maxfps=%d\n", s_CfgMaxFps);
    fprintf(f, "vsync=%d\n", s_CfgVsync);
    if (s_CfgTickHz == -1)
    {
        fprintf(f, "tickhz=hw\n");
        /* the file now HAS a tickhz line, exactly as the numeric arm records it, or
         * videoPersistDerivedTick would rewrite it on the first live maxfps change */
        s_CfgTickSeenInIni = 1;
        s_CfgTickAsWritten = -1;
    }
    else
    {
        fprintf(f, "tickhz=%d\n", s_CfgTickHz); /* derived from maxfps, not a literal 60 */
        /* the file now HAS a tickhz line: record it the way the parser would,
         * or videoPersistDerivedTick skips every live maxfps change this session */
        s_CfgTickSeenInIni = 1;
        s_CfgTickAsWritten = s_CfgTickHz;
    }
    fprintf(f, "render_thread=%d\n", s_CfgRenderThread);
    fprintf(f, "adzy=%d\n", s_CfgAdzy);
    fprintf(f, "unlock_all=%d\n", s_CfgUnlockAll);
    fprintf(f, "pause_pillars=%d\n", s_CfgPausePillars);
    fprintf(f, "hud_scale=%d\n", s_CfgHudScale);
    fprintf(f, "health_wide=%d\n", s_CfgHealthWide);
    fprintf(f, "shadow_alpha=%d\n", s_CfgShadowAlpha);
    fprintf(f, "always_crosshair=%d\n", s_CfgAlwaysCrosshair);
    fprintf(f, "mission_timer=%d\n", s_CfgMissionTimer);
    fprintf(f, "stereo_pan=%d\n", s_CfgStereoPan);
    fprintf(f, "aim_lock=%d\n", s_CfgAimLock);
    fprintf(f, "mp_aim_lock=%d\n", s_CfgMpAimLock);
    fprintf(f, "mp_sight_on_screen=%d\n", s_CfgMpSight);
    fprintf(f, "mp_no_auto_aim=%d\n", s_CfgMpAutoAim == 0);
    fprintf(f, "mp_show_names=%d\n", s_CfgMpShowNames);
    fprintf(f, "mp_invisibility=%d\n", s_CfgMpInvisibility);
    fprintf(f, "mp_dk_mode=%d\n", s_CfgMpDkMode);
    fprintf(f, "mp_tiny_players=%d\n", s_CfgMpTinyPlayers);
    fprintf(f, "mp_paintball=%d\n", s_CfgMpPaintball);
    fprintf(f, "mp_no_radar=%d\n", s_CfgMpNoRadar);
    fprintf(f, "mp_turbo_mode=%d\n", s_CfgMpTurbo);
    fprintf(f, "mp_unlimited_ammo=%d\n", s_CfgMpUnlimitedAmmo);
    fprintf(f, "mp_hud_wide=%d\n", s_CfgMpHudWide);
    fprintf(f, "mp_pillarbox=%d\n", s_CfgMpPillarbox);
    fprintf(f, "mp_always_crosshair=%d\n", s_CfgMpAlwaysCrosshair);
    fprintf(f, "mp_fix_shootthrough=%d\n", s_CfgMpFixShootThrough);
    fprintf(f, "mp_spawn_weapon=%d\n", s_CfgMpSpawnWeapon);
    fprintf(f, "mp_random_spawns=%d\n", s_CfgMpRandomSpawns);
    fprintf(f, "mp_head_roll=%d\n", s_CfgMpHeadRoll);
    fprintf(f, "unlimited_explosions=%d\n", s_CfgUnlimitedExplosions);
    fprintf(f, "mp_unlimited_explosions=%d\n", s_CfgMpUnlimitedExplosions);
    fprintf(f, "watch_static=%d\n", s_CfgWatchStatic);
    fprintf(f, "fast_pause=%d\n", s_CfgFastPause);
    fprintf(f, "armor_hud=%d\n", s_CfgArmorHud);
    fprintf(f, "setting_help=%d\n", s_CfgSettingHelp);
    fprintf(f, "mouse_auto_aim=%d\n", s_CfgMouseAutoAim);
    fprintf(f, "mute_unfocused=%d\n", s_CfgMuteUnfocused);
    fprintf(f, "mp_ltk_health=%d\n", s_CfgMpLtkHealth);
    fprintf(f, "mp_no_armor=%d\n", s_CfgMpNoArmor);
    fprintf(f, "mp_friendly_fire=%d\n", s_CfgMpFriendlyFire);
    fprintf(f, "head_roll=%d\n", s_CfgNoHeadRoll == 0);
    fprintf(f, "p1_ends_match=%d\n", s_CfgMpP1EndsMatch);
    fclose(f);
    printf("ge: wrote default midas.ini\n");
}

/** A modal error box for failures before there is a game to show them (today
  * only "no usable ROM", romdata.c).
 * SDL allows this before SDL_Init, which matters because romdataInit runs
 * before videoInit. Never shown in automated runs (GE_FRAMES, or GE_NOMSGBOX),
 * since the modal box would hang the harness. */
/* ---- Typing, for the on-screen keyboard ----
 *
 * The on-screen keyboard (front.c) is for pad players; anyone with a keyboard
 * can just type. This uses SDL_TEXTINPUT, the only source that gets layouts,
 * shift and dead keys right; it arrives on the fast3d event pump. Backspace,
 * Return and Escape are not text, so they come through the key latch beside it.
 *
 * Input is an ordered queue, not one char and one key, because rollover, a
 * frame hitch or an IME commit can deliver several in one pump and a char then
 * Backspace must apply in order. The OSK takes one entry per tick. */
#define PORT_TEXT_QUEUE 32
static s32  s_TextQueue[PORT_TEXT_QUEUE]; /* > 0 a character, < 0 -PORTTEXT_* */
static s32  s_TextHead, s_TextCount;
static s32  s_TextActive;
/* keys already down when typing started (the Return or E that pressed
 * TYPE): their auto-repeat is not typing, so nothing is taken until they are
 * let go */
static Uint8 s_TextStartHeld[SDL_NUM_SCANCODES];
static s32  s_TextStartAny;

static void portTextQueuePush(s32 v)
{
    if (s_TextCount < PORT_TEXT_QUEUE)
    {
        s_TextQueue[(s_TextHead + s_TextCount) % PORT_TEXT_QUEUE] = v;
        s_TextCount++;
    }
}

/** 1 = a key that was down when typing started is still down. */
static s32 portTextStartStillHeld(void)
{
    const Uint8 *k;
    s32 i, any = 0;

    if (!s_TextStartAny)
    {
        return 0;
    }
    k = SDL_GetKeyboardState(NULL);
    for (i = 0; i < SDL_NUM_SCANCODES; i++)
    {
        if (s_TextStartHeld[i])
        {
            if (k != NULL && k[i]) any = 1;
            else s_TextStartHeld[i] = 0;
        }
    }
    s_TextStartAny = any;
    return any;
}

/** 1 = a capital would be typed right now (shift held, or caps lock on). */
s32 portVideoCapsState(void)
{
    const SDL_Keymod m = SDL_GetModState();
    s32 caps = (m & KMOD_CAPS) ? 1 : 0;

    if (m & KMOD_SHIFT)
    {
        caps = !caps;
    }
    return caps;
}

void portTextInputStart(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);

    s_TextCount = 0;
    s_TextActive = 1;
    if (k != NULL)
    {   /* remember keys already down when typing started */
        SDL_memcpy(s_TextStartHeld, k, SDL_NUM_SCANCODES);
        s_TextStartAny = 1;
    }
    SDL_StartTextInput();
}

void portTextInputStop(void)
{
    if (s_TextActive)
    {   /* Whatever is down as typing stops (the accepting Return, a held
         * letter) was text: hold it over (videoGetPad, "a blank must not end in
         * a press") so it cannot become a fresh START/use/C press. Needed here
         * too because the frame pump can run after the last blanked poll. */
        const Uint8 *k = SDL_GetKeyboardState(NULL);

        if (k != NULL)
        {
            SDL_memcpy(sHeldOverKeys, k, SDL_NUM_SCANCODES);
            sHeldOverAny = 1;
        }
    }
    s_TextActive = 0;
    s_TextCount = 0;
    s_TextStartAny = 0;
    if (!portConIsOpen())
    {   /* one SDL text session for both - the open console's prompt
         * still needs it */
        SDL_StopTextInput();
    }
}

s32 portTextInputActive(void)
{
    return s_TextActive;
}

/** 1 = SDL is delivering text. Closing the console turns it off under typing. */
static s32 portTextSdlActive(void)
{
#ifdef GE_XBOX
    return 1; /* the shim has no text input to lose (xbox_sdl_shim.h) */
#else
    return SDL_IsTextInputActive() ? 1 : 0;
#endif
}

/** From the event pump. Only while typing is on and the tilde console is
  * closed: the console's event watch sees the
 * same events first and cannot consume them, so its keystrokes (and the Return
 * that ran a command) would reach the name. */
void portTextInputPushChar(char c)
{
    if (s_TextActive && !portConIsOpen() && c >= 0x20 && c < 0x7F && !portTextStartStillHeld())
    {
        portTextQueuePush((s32) (unsigned char) c);
    }
}

void portTextInputPushKey(s32 which)
{
    /* SDL text input off under typing = the console just closed and stopped it
     * (inputConsoleToggle; portTextInputPoll takes it back): the watch ran
     * first, so the Escape that closed it, or a Return the same pump drained,
     * was the prompt's - not a cancel or an OK for the name */
    if (s_TextActive && !portConIsOpen() && portTextSdlActive() && which > 0 && !portTextStartStillHeld())
    {
        portTextQueuePush(-which);
    }
}

/**
 * Take the next thing typed, oldest first: a character in *chr (0 = none) or
 * one of PORTTEXT_* in *key - one of the two per call. It is taken, so
 * a caller that ignores the result does not see it again a frame later.
 */
void portTextInputPoll(char *chr, s32 *key)
{
    s32 v = 0;

    if (s_TextActive && !portConIsOpen() && !portTextSdlActive())
    {   /* Closing the console stops SDL text input under a still-typing
         * keyboard; restart it here, after the pump that closed it, which also
         * keeps the closing grave's own text out of the name. */
        SDL_StartTextInput();
    }
    if (s_TextCount > 0)
    {   /* one entry per call, in typed order */
        v = s_TextQueue[s_TextHead];
        s_TextHead = (s_TextHead + 1) % PORT_TEXT_QUEUE;
        s_TextCount--;
    }
    if (chr != NULL)
    {
        *chr = (v > 0) ? (char) v : '\0';
    }
    if (key != NULL)
    {
        *key = (v < 0) ? -v : 0;
    }
}

void portShowErrorBox(const char *title, const char *text)
{
    if (getenv("GE_FRAMES") != NULL || getenv("GE_NOMSGBOX") != NULL)
    {
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, NULL);
}

/* The Files screen's EXIT GAME button (front.c): pushes SDL_QUIT so the quit
 * takes the close box's exact path (the event pump's exit(0), then the atexit
 * ini saver). SDL_PushEvent is thread-safe; see
 * docs/port/files-screen-buttons.md. */
void portRequestQuit(void)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
}

/**
 * Exit hook: write the last windowed geometry + monitor back into midas.ini
 * IN PLACE (only the five [display] keys change; everything else - the
 * user's bindings, comments, the launcher's layout - is preserved byte for
 * byte). Missing keys are inserted at the end of [display]. Skipped under
 * GE_WINDOW (capture/harness runs must never clobber the user's ini) and
 * GE_NOINISAVE=1. Registered with atexit() after gfx_init, so every normal
 * quit path (close box, quit key, SDL_QUIT) saves; crashes and the stall
 * watchdog's _exit do not.
 */
/**
 * In-place midas.ini rewrite of integer keys in one section: only the listed
 * keys change, every other line (bindings, the launcher's layout) is
 * preserved byte for byte except ';' note lines, which are dropped (midas.ini
 * holds settings only; older builds wrote notes); keys the section lacks are inserted at its
 * end. Returns 1 when the file was written. `create` = append the section
 * when the ini has none (the parser ignores headers, so that is harmless);
 * without it a missing section leaves the file alone (the window saver's
 * choice for hand-edited inis). Refuses (0) rather than truncating a file
 * longer than the line cap, or a key=value line longer than the 256-byte
 * read buffer. Shared by the exit-time window saver and the
 * in-game Port Options page (portIniWriteInts, port_options.c).
 */
#define VIDEO_INI_MAX_LINES 2000
/* Per-section write cap. Port Options' [display] table passed the old 64 and
 * every [display] save was refused, so keep this well above the table. */
#define VIDEO_INI_MAX_KEYS  256
static int s_IniSeen; /* midas.ini existed (read or written) this session */
static int s_IniFresh; /* there was no midas.ini at boot */

/** input_pc.c: 1 when this run started with no midas.ini (a fresh install). */
s32 portVideoIniFresh(void)
{
    return s_IniFresh;
}

static int videoIniWriteKeys(const char *section, const char *const *keys, const char *const *vals, int n, int create)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    int seen[VIDEO_INI_MAX_KEYS];
    char **lines;
    int nlines = 0;
    int i;
    int insection = 0;
    int hadsection = 0;
    int seclen;
    int overflow = 0;
    int written = 0;
    char line[256];
    FILE *f;

    if (n <= 0)
    {
        return 0;
    }
    if (n > VIDEO_INI_MAX_KEYS)
    {
        portConLogf("ge: midas.ini: %d %s keys is over the %d-key cap, not saving them\n", n, section,
                    VIDEO_INI_MAX_KEYS);
        return 0;
    }
    for (i = 0; i < n; i++)
    {
        /* every line written must read back whole through the same 256-byte
         * fgets (here and videoLoadConfig): refuse, never truncate - a cut
         * line loses its '\n' and swallows the next line of the file */
        if (SDL_strlen(keys[i]) + SDL_strlen(vals[i]) + 2 >= sizeof(line))
        {
            portConLogf("ge: midas.ini: %s is too long to save (%d chars, a line holds %d), not rewriting it\n",
                        keys[i], (int) (SDL_strlen(keys[i]) + 1 + SDL_strlen(vals[i])), (int) sizeof(line) - 2);
            return 0;
        }
    }
    for (i = 0; i < n; i++)
    {
        seen[i] = 0;
    }
    seclen = (int) SDL_strlen(section);
    lines = (char **) SDL_calloc(VIDEO_INI_MAX_LINES + VIDEO_INI_MAX_KEYS + 4, sizeof(char *));
    if (lines == NULL)
    {
        return 0;
    }

    f = portRootFopen("midas.ini", "r");
    if (f == NULL)
    {
        /* only a file that never existed may be created from scratch -
         * once this session has read one, a failed read (a sharing
         * violation, out of handles) would have replaced the whole midas.ini
         * with this one section */
        if (!create || s_IniSeen)
        {
            if (create)
            {
                portConLogf("ge: midas.ini: could not read it, not rewriting it\n");
            }
            SDL_free(lines);
            return 0;
        }
    }
    else
    {
        while (fgets(line, sizeof(line), f) != NULL)
        {
            char *s = line;
            char *out = NULL;
            int drop = 0;
            if (nlines >= VIDEO_INI_MAX_LINES)
            {
                overflow = 1;
                break;
            }
            while (*s == ' ' || *s == '\t') s++;
            if (s[0] == ';')
            {
                continue; /* drop the notes older builds wrote: midas.ini is settings only */
            }
            if (s[0] == '[')
            {
                if (insection)
                {
                    /* leaving the section: add any key it did not have */
                    for (i = 0; i < n; i++)
                    {
                        if (!seen[i])
                        {
                            char add[sizeof(line)];
                            snprintf(add, sizeof(add), "%s=%s\n", keys[i], vals[i]);
                            lines[nlines++] = SDL_strdup(add);
                            seen[i] = 1;
                        }
                    }
                }
                insection = (SDL_strncasecmp(s, section, seclen) == 0);
                hadsection |= insection;
            }
            else if (insection && s[0] != ';' && s[0] != '#')
            {
                char *eq = SDL_strchr(s, '=');
                if (eq != NULL)
                {
                    char key[64];
                    int klen = (int)(eq - s);
                    if (klen > 0 && klen < (int)sizeof(key))
                    {
                        char *e;
                        SDL_memcpy(key, s, klen);
                        key[klen] = '\0';
                        e = key + klen;
                        while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
                        for (i = 0; i < n; i++)
                        {
                            if (SDL_strcasecmp(key, keys[i]) == 0)
                            {
                                char rep[sizeof(line)];
                                snprintf(rep, sizeof(rep), "%s=%s\n", keys[i], vals[i]);
                                out = SDL_strdup(rep);
                                seen[i] = 1;
                                break;
                            }
                            /* hw_mipmaps is mipmaps' old name and the loader
                             * takes both (last line wins), so writing mipmaps
                             * drops the old line to avoid a stale one undoing
                             * it. */
                            if (SDL_strcasecmp(key, "hw_mipmaps") == 0 && SDL_strcasecmp(keys[i], "mipmaps") == 0)
                            {
                                drop = 1;
                                break;
                            }
                        }
                    }
                }
            }
            if (drop)
            {
                continue;
            }
            lines[nlines++] = out != NULL ? out : SDL_strdup(line);
        }
        fclose(f);
    }
    if (overflow)
    {
        portConLogf("ge: midas.ini: more than %d lines, not rewriting it\n", VIDEO_INI_MAX_LINES);
    }
    else
    {
        if (!hadsection && create)
        {
            /* no such section: start one at the end */
            if (nlines > 0 && lines[nlines - 1][0] != '\0' &&
                lines[nlines - 1][SDL_strlen(lines[nlines - 1]) - 1] != '\n')
            {
                lines[nlines++] = SDL_strdup("\n");
            }
            lines[nlines++] = SDL_strdup("\n");
            snprintf(line, sizeof(line), "%s\n", section);
            lines[nlines++] = SDL_strdup(line);
            insection = 1;
            hadsection = 1;
        }
        if (insection)
        {
            /* the section was the last one: append what it did not have */
            if (nlines > 0 && lines[nlines - 1][0] != '\0' &&
                lines[nlines - 1][SDL_strlen(lines[nlines - 1]) - 1] != '\n')
            {
                lines[nlines++] = SDL_strdup("\n");
            }
            for (i = 0; i < n; i++)
            {
                if (!seen[i])
                {
                    char add[sizeof(line)];
                    snprintf(add, sizeof(add), "%s=%s\n", keys[i], vals[i]);
                    lines[nlines++] = SDL_strdup(add);
                    seen[i] = 1;
                }
            }
        }
        if (hadsection)
        {
            f = portRootFopen("midas.ini", "w");
            if (f != NULL)
            {
                int ok = 1;

                for (i = 0; i < nlines; i++)
                {
                    if (fputs(lines[i], f) < 0)
                    {
                        ok = 0;
                    }
                }
                if (ferror(f))
                {
                    ok = 0;
                }
                if (fclose(f) != 0)
                {
                    ok = 0;
                }
                /* a failed write (full disk) is not "saved" */
                written = ok;
                s_IniSeen = 1;
                if (!ok)
                {
                    portConLogf("ge: midas.ini: WRITE FAILED - the file may be incomplete\n");
                }
            }
        }
        /* no section and !create (hand-edited ini): leave the file alone */
    }
    for (i = 0; i < nlines; i++)
    {
        SDL_free(lines[i]);
    }
    SDL_free(lines);
    return written;
}

/**
 * Port Options page (port_options.c): persist integer keys of one section
 * in place, right when the player changes them. Same harness guard as the
 * window saver: GE_WINDOW / GE_NOINISAVE runs never touch the user's ini
 * (returns 0). Weak no-op in libultra_shim.c for builds without video.
 */
s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n)
{
    static int allowed = -1;

    if (allowed < 0)
    {
        allowed = (getenv("GE_WINDOW") == NULL && getenv("GE_NOINISAVE") == NULL) ? 1 : 0;
    }
    if (!allowed)
    {
        return 0;
    }
    if (videoIniWriteKeys(section, keys, vals, (int) n, 1))
    {
        printf("ge: midas.ini: saved %d %s option(s)\n", (int) n, section);
        return 1;
    }
    return 0;
}

/* ---- Keyboard and mouse binds: Data/Devices/kbm.gedevice [binds] ----------
 *
 * The keyboard's binds live in its device record, like a pad's, so midas.ini
 * holds settings only. The [binds] section is written whole from the live
 * tables; the record's other sections belong to input_pc.c, whose own rewrite
 * of the record appends this section too (portVideoKbmBindsAppend). An older
 * midas.ini [controls] section is moved here once (videoLoadConfig). */
static void videoBindToText(const SDL_Scancode *codes, char *buf, int len); /* below */
extern const char *portInputKbmRecPath(void);               /* input_pc.c */
extern void portInputDevRecHeader(FILE *f, const char *id); /* input_pc.c */

#define VIDEO_KBM_MAX_LINES 256

/* Port actions whose line was in the loaded binds (videoApplyBindLine), so an
 * action added since the file was written can be told from one the player set. */
static u8 sPortBindSeen[PORT_BIND_MAX];

void portVideoKbmBindsAppend(FILE *f)
{
    char buf[256];
    int i;

    fprintf(f, "[binds]\n");
    for (i = 0; i < BIND_COUNT; i++)
    {
        videoBindToText(sBindings[i], buf, (int) sizeof(buf));
        fprintf(f, "%s=%s\n", sBindInfo[i].name, buf);
    }
    for (i = 0; i < portJoyPortBindCount() && i < PORT_BIND_MAX; i++)
    {
        videoBindToText(sPortBindings[i], buf, (int) sizeof(buf));
        fprintf(f, "%s=%s\n", portJoyPortBindName(i), buf);
    }
    fprintf(f, "%s=1\n", VIDEO_MOVE_UPGRADE_KEY);
}

/** Rewrites the record's [binds] section from the live binds, keeping every
 * other line; a missing record is started with its [device] header. 1 = the
 * binds are on disk. */
static int videoKbmBindsSave(void)
{
    char path[1200];
    char line[256];
    char *lines[VIDEO_KBM_MAX_LINES];
    int nlines = 0;
    int inbinds = 0;
    int i;
    FILE *f;

    if (getenv("GE_WINDOW") != NULL || getenv("GE_NOINISAVE") != NULL)
    {
        return 0; /* a harness run leaves the user's files alone */
    }
    SDL_strlcpy(path, portInputKbmRecPath(), sizeof(path));
    f = fopen(path, "r");
    if (f != NULL)
    {
        while (fgets(line, sizeof(line), f) != NULL)
        {
            const char *s = line;

            while (*s == ' ' || *s == '\t') s++;
            if (s[0] == '[')
            {
                inbinds = (SDL_strncasecmp(s, "[binds]", 7) == 0);
            }
            if (inbinds)
            {
                continue;
            }
            if (nlines >= VIDEO_KBM_MAX_LINES)
            {
                portConLogf("ge: devices: %s is too long, binds not saved\n", path);
                fclose(f);
                for (i = 0; i < nlines; i++) SDL_free(lines[i]);
                return 0;
            }
            lines[nlines++] = SDL_strdup(line);
        }
        fclose(f);
    }
    f = fopen(path, "w");
    if (f == NULL)
    {
        portConLogf("ge: devices: cannot write %s\n", path);
        for (i = 0; i < nlines; i++) SDL_free(lines[i]);
        return 0;
    }
    if (nlines == 0)
    {
        portInputDevRecHeader(f, "kbm");
    }
    for (i = 0; i < nlines; i++)
    {
        fputs(lines[i], f);
        SDL_free(lines[i]);
    }
    portVideoKbmBindsAppend(f);
    if (ferror(f) | fclose(f))
    {
        portConLogf("ge: devices: writing %s failed\n", path);
        return 0;
    }
    return 1;
}

/** Applies one bind line (an N64 control or a Port action); 0 = not a bind.
 * The move-upgrade marker counts as a bind line. */
static int videoApplyBindLine(const char *key, char *value, int *moveUpgraded)
{
    int i;

    if (SDL_strcasecmp(key, VIDEO_MOVE_UPGRADE_KEY) == 0)
    {
        *moveUpgraded = 1;
        return 1;
    }
    for (i = 0; i < BIND_COUNT; i++)
    {
        if (SDL_strcasecmp(key, sBindInfo[i].name) == 0)
        {
            iniParseKeys(i, value);
            return 1;
        }
    }
    for (i = 0; i < portJoyPortBindCount() && i < PORT_BIND_MAX; i++)
    {
        if (SDL_strcasecmp(key, portJoyPortBindName(i)) == 0)
        {
            videoParseBindValue(value, sPortBindings[i], portJoyPortBindName(i));
            sPortBindSeen[i] = 1;
            return 1;
        }
    }
    return 0;
}

/** Reads the record's [binds] section over the live binds. 1 = it had one. */
static int videoKbmBindsLoad(int *moveUpgraded)
{
    char path[1200];
    char line[256];
    int inbinds = 0;
    int found = 0;
    FILE *f;

    SDL_strlcpy(path, portInputKbmRecPath(), sizeof(path));
    f = fopen(path, "r");
    if (f == NULL)
    {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *s = iniTrim(line);
        char *eq;

        if (s[0] == '[')
        {
            inbinds = (SDL_strncasecmp(s, "[binds]", 7) == 0);
            found |= inbinds;
            continue;
        }
        if (!inbinds || (eq = SDL_strchr(s, '=')) == NULL)
        {
            continue;
        }
        *eq = '\0';
        videoApplyBindLine(iniTrim(s), eq + 1, moveUpgraded);
    }
    fclose(f);
    return found;
}

/** A Port action missing from the loaded binds (added since they were saved)
 * keeps its default keys, except a key the player already has on another bind:
 * that key is dropped, so a new default never doubles up a custom bind.
 * 1 = something was dropped. */
static int videoPortBindNewDefaults(void)
{
    int changed = 0;
    int i, j, k, m;

    for (i = 0; i < portJoyPortBindCount() && i < PORT_BIND_MAX; i++)
    {
        if (sPortBindSeen[i])
        {
            continue;
        }
        for (k = 0; k < BIND_MAX_KEYS; k++)
        {
            const SDL_Scancode sc = sPortBindings[i][k];
            const char *other = NULL;

            if (sc == SDL_SCANCODE_UNKNOWN)
            {
                continue;
            }
            for (j = 0; j < BIND_COUNT && other == NULL; j++)
            {
                for (m = 0; m < BIND_MAX_KEYS; m++)
                {
                    if (sBindings[j][m] == sc)
                    {
                        other = sBindInfo[j].name;
                        break;
                    }
                }
            }
            for (j = 0; j < portJoyPortBindCount() && j < PORT_BIND_MAX && other == NULL; j++)
            {
                for (m = 0; j != i && m < BIND_MAX_KEYS; m++)
                {
                    if (sPortBindings[j][m] == sc)
                    {
                        other = portJoyPortBindName(j);
                        break;
                    }
                }
            }
            if (other != NULL)
            {
                SDL_Scancode one[BIND_MAX_KEYS] = { 0 };
                char name[64];

                one[0] = sc;
                videoBindToText(one, name, (int) sizeof(name));
                portConLogf("ge: input: new bind %s left without its default %s, already on %s\n",
                        portJoyPortBindName(i), name, other);
                for (m = k; m + 1 < BIND_MAX_KEYS; m++)
                {
                    sPortBindings[i][m] = sPortBindings[i][m + 1];
                }
                sPortBindings[i][BIND_MAX_KEYS - 1] = SDL_SCANCODE_UNKNOWN;
                k--;
                changed = 1;
            }
        }
    }
    return changed;
}

/** Removes a whole section (header to the next header) from midas.ini. */
static void videoIniDropSection(const char *section)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    char line[256];
    char **lines;
    int nlines = 0;
    int insection = 0;
    int i;
    FILE *f;

    if (getenv("GE_WINDOW") != NULL || getenv("GE_NOINISAVE") != NULL)
    {
        return;
    }
    lines = (char **) SDL_calloc(VIDEO_INI_MAX_LINES, sizeof(char *));
    if (lines == NULL || (f = portRootFopen("midas.ini", "r")) == NULL)
    {
        SDL_free(lines);
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        const char *s = line;

        while (*s == ' ' || *s == '\t') s++;
        if (s[0] == '[')
        {
            insection = (SDL_strncasecmp(s, section, SDL_strlen(section)) == 0);
        }
        if (insection)
        {
            continue;
        }
        if (nlines >= VIDEO_INI_MAX_LINES)
        {   /* never truncate the file: leave it as it is */
            fclose(f);
            for (i = 0; i < nlines; i++) SDL_free(lines[i]);
            SDL_free(lines);
            return;
        }
        lines[nlines++] = SDL_strdup(line);
    }
    fclose(f);
    while (nlines > 0 && (lines[0][0] == '\n' || lines[0][0] == '\r'))
    {   /* the blank line that separated the dropped section */
        SDL_free(lines[0]);
        SDL_memmove(lines, lines + 1, (size_t) --nlines * sizeof(char *));
    }
    f = portRootFopen("midas.ini", "w");
    if (f != NULL)
    {
        for (i = 0; i < nlines; i++)
        {
            fputs(lines[i], f);
        }
        fclose(f);
    }
    for (i = 0; i < nlines; i++) SDL_free(lines[i]);
    SDL_free(lines);
}

static void videoSaveWindowIni(void)
{
    extern int gfx_sdl_get_saved_geometry(int *x, int *y, int *w, int *h, int *display);
    static const char *const keys[] = { "window_width", "window_height", "window_x", "window_y", "window_display" };
    int vals[5];
    char strs[5][16];
    const char *vptr[5];
    int x, y, w, h, d, i;
    int changed;

    if (!gfx_sdl_get_saved_geometry(&x, &y, &w, &h, &d) || w <= 0 || h <= 0)
    {
        return;
    }
    if (s_CfgExclusiveResPicked && s_CfgFullscreen == VIDEO_FS_EXCLUSIVE)
    {
        /* gfx_sdl2 remembers only WINDOWED geometry; in exclusive the size
         * is the mode the player picked (already in the ini). Position and
         * monitor still save. */
        w = s_CfgWindowW;
        h = s_CfgWindowH;
    }
    changed = (w != s_CfgWindowW) || (h != s_CfgWindowH) || (x != s_CfgWindowX) ||
              (y != s_CfgWindowY) || (d != s_CfgWindowDisplay);
    if (!changed)
    {
        return;
    }
    vals[0] = w; vals[1] = h; vals[2] = x; vals[3] = y; vals[4] = d;
    for (i = 0; i < 5; i++)
    {
        snprintf(strs[i], sizeof(strs[i]), "%d", vals[i]);
        vptr[i] = strs[i];
    }
    if (videoIniWriteKeys("[display]", keys, vptr, 5, 0))
    {
        printf("ge: midas.ini: saved window %dx%d at (%d,%d) display %d\n", w, h, x, y, d);
    }
}

static void videoBindToText(const SDL_Scancode *codes, char *buf, int len); /* below */

static void videoLoadConfig(void)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    FILE *f;
    char line[256];
    char defbuf[64];
    int i;
    int moveUpgraded = 0; /* the binds carry VIDEO_MOVE_UPGRADE_KEY */
    int iniHadBinds = 0;  /* midas.ini still has bind lines (an older file) */
    int recBinds;         /* the keyboard's record had a [binds] section */
    /* the one-time n64_aa -> classic_mode split (classic_split.h): what THIS
     * FILE carried. Set while parsing, because only a file can answer them - a
     * live set of the same key must not look like a written line. */
    int iniHadClassicMode = 0;
    int iniHadSplitMarker = 0;

    videoBindCountCheck();
    SDL_memset(sPortBindSeen, 0, sizeof(sPortBindSeen));

    /* code defaults first: a missing/partial/broken ini leaves these live */
    for (i = 0; i < BIND_COUNT; i++)
    {
        SDL_strlcpy(defbuf, sBindInfo[i].defkeys, sizeof(defbuf));
        iniParseKeys(i, defbuf);
    }
    for (i = 0; i < portJoyPortBindCount() && i < PORT_BIND_MAX; i++)
    {
        SDL_strlcpy(defbuf, portJoyPortBindDefault(i), sizeof(defbuf));
        videoParseBindValue(defbuf, sPortBindings[i], portJoyPortBindName(i));
    }

    f = portRootFopen("midas.ini", "r");
    if (f == NULL)
    {
        s_IniFresh = 1;   /* a fresh install (input_pc.c starts player 1's device records) */
        /* ...whose six player options are the code defaults (look Upright), not
         * whatever the first save folder loaded says (Rare's default is Reverse) */
        s_CfgPlayerOptsMask = PLAYEROPT_ALL;
        /* ...and whose volumes are the code defaults (70%), not the first save
         * folder's */
        s_CfgVolumesSet = 1;
        /* Deleting midas.ini resets settings, not binds: those are in the
         * keyboard's record. */
        if (videoKbmBindsLoad(&moveUpgraded) && videoPortBindNewDefaults())
        {
            videoKbmBindsSave();
        }
        /* No midas.ini yet: the defaults must still agree with each other (maxfps
         * vs tickhz) before being written, since the normal path couples only
         * after reading a file. */
        videoCoupleTickToMaxFps();
        videoWriteDefaultIni();
        s_CfgTickLoadDone = 1;
        return;
    }
    s_IniSeen = 1;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *eq;
        char *key;
        char *s = iniTrim(line);
        if (s[0] == '\0' || s[0] == ';' || s[0] == '#' || s[0] == '[')
        {
            continue; /* single [controls] section so far - headers skipped */
        }
        eq = SDL_strchr(s, '=');
        if (eq == NULL)
        {
            continue;
        }
        *eq = '\0';
        key = iniTrim(s);
        if (videoApplyBindLine(key, eq + 1, &moveUpgraded))
        {   /* an older midas.ini's [controls]: moved to the keyboard's record below */
            iniHadBinds = 1;
        }
        else
        {
            extern s32 portInputSetOption(const char *key, const char *value);
            char *val = iniTrim(eq + 1);

            if (!videoSetDisplayOption(key, val) && !portInputSetOption(key, val) && !portJoyPortSetOption(key, val)
                && !romdataIsIniKey(key) /* [data] rom=: romdata.c reads it itself, section-aware */)
            {
                portConLogf("ge: midas.ini: unknown control \"%s\"\n", key);
            }
            else if (SDL_strcasecmp(key, "tickhz") == 0)
            {
                /* What THIS FILE says, recorded here and nowhere else. The tickhz handler in
                 * videoSetDisplayOption cannot do it: videoCoupleTickToMaxFps calls that handler
                 * with "hw" while the file is still being read, and recording that would claim the
                 * file said hw when it said a number - which is how F3-A stayed invisible, and
                 * then made the first fix order-dependent (it worked only when maxfps came
                 * first). This is the value as written, before any clamp: if the file's number is
                 * not what runs, the writer has to correct it. */
                s_CfgTickSeenInIni = 1;
                s_CfgTickAsWritten = videoTickTextValue(val);
            }
        }
        /* The presence of these two, and of the marker the split writes, is what
         * decides whether the migration below runs. Recorded here and not in a
         * setter for the reason the tickhz note above gives: only the FILE may
         * answer "did this one already go through the split?". */
        if (SDL_strcasecmp(key, "classic_mode") == 0)
        {
            iniHadClassicMode = 1;
        }
        else if (SDL_strcasecmp(key, "classic_split") == 0)
        {
            iniHadSplitMarker = 1;
        }
    }
    fclose(f);

    { /* The one-time n64_aa -> classic_mode split, once per file. An old
       * `n64_aa=1` meant BOTH "Classic Mode is on" and "the AA bit is set", so
       * it becomes both keys; the mapping and its guard are classic_split.c's.
       * The marker lands in the same write, so this cannot run twice - and a
       * player who later turns the preset off must not have it switched back on
       * by a migration that thought it knew better. A no-ini start never
       * reaches here: videoWriteDefaultIni already emits both keys. */
        PortClassicSplitIn in;
        PortClassicSplitOut out;

        in.n64_aa = s_CfgN64Aa;
        in.classic_mode = s_CfgClassicMode;
        in.had_classic_mode = iniHadClassicMode;
        in.had_split_marker = iniHadSplitMarker;
        portClassicSplit(&in, &out);
        s_CfgClassicMode = out.classic_mode;
        if (out.migrate)
        {
            char bit[8];
            char mode[8];
            const char *keys[2] = { "classic_mode", "classic_split" };
            const char *vals[2] = { mode, "1" };

            SDL_snprintf(bit, sizeof(bit), "%d", s_CfgN64Aa ? 1 : 0);
            SDL_snprintf(mode, sizeof(mode), "%d", s_CfgClassicMode ? 1 : 0);
            if (portIniWriteStrs("[display]", keys, vals, 2))
            {
                portConLogf("ge: midas.ini: n64_aa split - classic_mode=%s (the preset) beside "
                            "n64_aa=%s (the AA bit)\n", mode, bit);
            }
        }
    }

    /* The keyboard's record wins over any old [controls] lines; a record
     * without binds takes them over, once, and midas.ini drops the section. */
    recBinds = videoKbmBindsLoad(&moveUpgraded);
    if ((recBinds || iniHadBinds) && videoPortBindNewDefaults() && recBinds)
    {
        videoKbmBindsSave(); /* settled once; the move below saves an old [controls] */
    }
    if (!recBinds && iniHadBinds)
    {
        if (videoKbmBindsSave())
        {
            portConLogf("ge: midas.ini: keyboard and mouse binds moved to Data/Devices/kbm.gedevice\n");
        }
        else
        {
            iniHadBinds = 0; /* not saved: keep them in midas.ini, try again next boot */
        }
    }
    if (iniHadBinds)
    {
        videoIniDropSection("[controls]");
    }

    { /* One-time upgrade for an older midas.ini whose four analog_* binds
       * are empty, which would leave the player unable to move: restore the
       * defaults. A marker key makes it run once, since a player may empty
       * them on purpose; skipped on the console. */
        int allempty = 1;
        int b;

        for (b = BIND_ANALOG_UP; b <= BIND_ANALOG_RIGHT; b++)
        {
            if (sBindings[b][0] != SDL_SCANCODE_UNKNOWN)
            {
                allempty = 0;
            }
        }
#ifdef GE_XBOX
        allempty = 0;
        moveUpgraded = 1;
#endif
        if (allempty && !moveUpgraded)
        {
            char defbuf2[64];

            for (b = BIND_ANALOG_UP; b <= BIND_ANALOG_RIGHT; b++)
            {
                const int c = BIND_C_UP + (b - BIND_ANALOG_UP); /* same order (enum above) */

                if (sBindings[c][0] != SDL_SCANCODE_UNKNOWN)
                {
                    SDL_memcpy(sBindings[b], sBindings[c], sizeof(sBindings[b]));
                    videoBindToText(sBindings[b], defbuf2, (int) sizeof(defbuf2));
                }
                else
                {
                    SDL_strlcpy(defbuf2, sBindInfo[b].defkeys, sizeof(defbuf2));
                    iniParseKeys(b, defbuf2);
                    SDL_strlcpy(defbuf2, sBindInfo[b].defkeys, sizeof(defbuf2));
                }
                portVideoIniSetKey(sBindInfo[b].name, defbuf2);
            }
            portConLogf("ge: midas.ini: movement had no Move-stick binding - gave it the C-button movement keys "
                    "(analog_up/down/left/right); the C buttons no longer move you\n");
        }
        if (!moveUpgraded)
        {
            videoKbmBindsSave(); /* the record always carries the marker */
        }
    }

    /* AFTER the whole file, not per key: an existing midas.ini carries BOTH
     * maxfps and tickhz, and coupling as maxfps is parsed would let a later
     * `tickhz=` line put them back out of step. Order-independent here. */
    videoCoupleTickToMaxFps();
    videoPersistDerivedTick();
    s_CfgTickLoadDone = 1;
}

/* ---- Window / fullscreen resolution list ----
 *
 * The list is the display's own modes (SDL, via gfx_sdl.get_display_mode),
 * de-duplicated by width x height and sorted smallest first (SDL enumerates
 * largest first, which made right-presses shrink the picture). It is sorted
 * here once so every caller indexes the same order. The live window size is
 * added if it is not a display mode, so the row can show the truth.
 *
 * What "resolution" means depends on the display mode:
 *   windowed    the window's size; applying resizes the window.
 *   exclusive   a real display mode; applying sets it and re-asserts fullscreen.
 *   fullscreen the borderless window takes the display's bounds by definition,
 *   so the row reports the size and refuses. */
#define VIDEO_RES_MAX 48

static struct { s16 w, h; } s_ResList[VIDEO_RES_MAX];
static s32 s_ResCount = -1;
/* What the list was built from. The window size and display can change after
 * the first build (drag, maximise, another monitor), so the list is rebuilt
 * whenever either differs from the last build. */
static s32 s_ResKeyOk = 0;
static s32 s_ResKeyModes, s_ResKeyDispW, s_ResKeyDispH; /* the display: mode count + current mode */
static uint32_t s_ResKeyW, s_ResKeyH;                    /* the live drawable size */

static void videoResBuild(void)
{
    s32 n, i;
    uint32_t cw = 0, ch = 0;
    int32_t cx = 0, cy = 0;
    int dw = 0, dh = 0;

    if (!s_VideoInitDone)
    {
        s_ResCount = 0; /* no window yet: an empty list, and ask again later */
        s_ResKeyOk = 0;
        return;
    }

    n = gfx_sdl.get_num_display_modes();
    gfx_sdl.get_dimensions(&cw, &ch, &cx, &cy);
    if (!gfx_sdl.get_current_display_mode(&dw, &dh))
    {
        dw = 0;
        dh = 0;
    }
    if (s_ResKeyOk && n == s_ResKeyModes && dw == s_ResKeyDispW && dh == s_ResKeyDispH
        && cw == s_ResKeyW && ch == s_ResKeyH)
    {
        return; /* built from exactly these inputs: still the truth */
    }
    s_ResKeyOk = 1;
    s_ResKeyModes = n;
    s_ResKeyDispW = dw;
    s_ResKeyDispH = dh;
    s_ResKeyW = cw;
    s_ResKeyH = ch;
    s_ResCount = 0;

    /* VIDEO_RES_MAX - 1: one slot is always left for the live size below */
    for (i = 0; i < n && s_ResCount < VIDEO_RES_MAX - 1; i++)
    {
        int w = 0, h = 0;
        s32 k;
        s32 dup = 0;

        if (!gfx_sdl.get_display_mode(i, &w, &h) || w <= 0 || h <= 0)
        {
            continue;
        }
        for (k = 0; k < s_ResCount; k++)
        {
            if (s_ResList[k].w == (s16) w && s_ResList[k].h == (s16) h)
            {
                dup = 1; /* the same size at another refresh rate */
                break;
            }
        }
        if (!dup)
        {
            s_ResList[s_ResCount].w = (s16) w;
            s_ResList[s_ResCount].h = (s16) h;
            s_ResCount++;
        }
    }

    { /* the size the window is at right now, if the display never offers it
       * (a dragged window corner, or a hand-edited midas.ini). cw/ch are the size
       * the key above was taken at, so the entry and the key always agree. */
        s32 k;
        s32 have = 0;
        /* not in borderless: that window is the display's size plus the extra
         * pixel row (gfx_sdl2 borderless_extra_height), never a size to offer */
        if (cw > 0 && ch > 0 && s_ResCount < VIDEO_RES_MAX && s_CfgFullscreen != VIDEO_FS_BORDERLESS)
        {
            for (k = 0; k < s_ResCount; k++)
            {
                if (s_ResList[k].w == (s16) cw && s_ResList[k].h == (s16) ch)
                {
                    have = 1;
                    break;
                }
            }
            if (!have)
            {
                s_ResList[s_ResCount].w = (s16) cw;
                s_ResList[s_ResCount].h = (s16) ch;
                s_ResCount++;
            }
        }
    }

    { /* smallest first, by pixel count then width, so stepping the row right
       * always means a bigger picture. Insertion sort: the list is at most
       * VIDEO_RES_MAX entries and this runs only when the list is rebuilt. */
        s32 i, j;

        for (i = 1; i < s_ResCount; i++)
        {
            const s16 w = s_ResList[i].w;
            const s16 h = s_ResList[i].h;
            const s32 area = (s32) w * (s32) h;

            for (j = i - 1; j >= 0; j--)
            {
                const s32 jarea = (s32) s_ResList[j].w * (s32) s_ResList[j].h;

                if (jarea < area || (jarea == area && s_ResList[j].w <= w))
                {
                    break;
                }
                s_ResList[j + 1] = s_ResList[j];
            }
            s_ResList[j + 1].w = w;
            s_ResList[j + 1].h = h;
        }
    }

    if (SDL_getenv("GE_RESDBG") != NULL)
    { /* the gate on the sort: the row steps this list in order, so it has to
       * read smallest first or right means smaller (see the header above) */
        s32 i;
        for (i = 0; i < s_ResCount; i++)
        {
            portConLogf("ge: video: resolution %2d: %dx%d\n", (int) i, (int) s_ResList[i].w, (int) s_ResList[i].h);
        }
    }
}

s32 portVideoResCount(void)
{
    videoResBuild();
    return s_ResCount;
}

s32 portVideoResGet(s32 i, s32 *w, s32 *h)
{
    videoResBuild();
    if (i < 0 || i >= s_ResCount)
    {
        return 0;
    }
    if (w != NULL) *w = s_ResList[i].w;
    if (h != NULL) *h = s_ResList[i].h;
    return 1;
}

/** The list entry the window is showing, or -1. */
s32 portVideoResCurrent(void)
{
    uint32_t cw = 0, ch = 0;
    int32_t cx = 0, cy = 0;
    s32 i;

    videoResBuild();
    if (!s_VideoInitDone)
    {
        return -1;
    }
    gfx_sdl.get_dimensions(&cw, &ch, &cx, &cy);
    for (i = 0; i < s_ResCount; i++)
    {
        if (s_ResList[i].w == (s16) cw && s_ResList[i].h == (s16) ch)
        {
            return i;
        }
    }
    return -1;
}

/**
 * 1 = the display owns the resolution and the row must not offer a choice.
 * That is the port's borderless "fullscreen": the window IS the display's
 * bounds, so a picker there would either lie or fight set_borderless_fullscreen.
 */
s32 portVideoResLocked(void)
{
    return s_CfgFullscreen == VIDEO_FS_BORDERLESS;
}

/**
 * Apply list entry `i`. Deliberately separate from stepping the row: the menu
 * only calls this when the player presses A, so scrolling past a resolution
 * the monitor cannot show never strands them in it.
 */
s32 portVideoResApply(s32 i)
{
    s32 w = 0, h = 0;

    if (!s_VideoInitDone || portVideoResLocked() || !portVideoResGet(i, &w, &h))
    {
        return 0;
    }
    if (s_CfgFullscreen != VIDEO_FS_EXCLUSIVE && i == portVideoResCurrent())
    {
        /* Windowed and already this size: nothing to apply, because a re-apply
         * re-centres the window and loses where the player dragged it (which
         * videoSaveWindowIni writes at exit). Exclusive still re-applies, since
         * its Accept writes window_width/height and s_CfgExclusiveResPicked. */
        portConLogf("ge: video: resolution %dx%d unchanged\n", w, h);
        return 1;
    }
    if (s_CfgFullscreen == VIDEO_FS_EXCLUSIVE)
    {
        /* a real mode switch: pick the closest the display advertises, then
         * re-assert fullscreen so SDL takes the new mode */
        gfx_sdl.set_closest_resolution(w, h, true);
        gfx_sdl.set_fullscreen_exclusive(true);
        gfx_sdl.set_fullscreen(true);
    }
    else
    {
        int32_t x = 0, y = 0;
        if (gfx_sdl.get_maximized_state())
        {   /* a maximized window resized in place stays "maximized",
             * so its geometry was never recorded and the exit saver wrote
             * the pre-maximize size back - restore it first */
            gfx_sdl.set_maximize(false);
        }
        gfx_sdl.get_centered_positions(w, h, &x, &y);
        gfx_sdl.set_dimensions((uint32_t) w, (uint32_t) h, x, y);
    }
    if (s_CfgFullscreen == VIDEO_FS_EXCLUSIVE)
    {
        /* exclusive: its OWN size, the mode the next boot and the
         * next switch to exclusive ask SDL for. gfx_sdl2 never records
         * fullscreen geometry, so the exit saver cannot carry this - persist
         * it now. The window's size is left alone. */
        videoExclusiveSizeSet((int) w, (int) h);
    }
    else
    {
        /* keep the ini value honest at once; videoSaveWindowIni writes the
         * live geometry at exit and would otherwise see a "changed" it did
         * not cause */
        s_CfgWindowW = w;
        s_CfgWindowH = h;
    }
    portConLogf("ge: video: resolution -> %dx%d (%s)\n", w, h,
                (s_CfgFullscreen == VIDEO_FS_EXCLUSIVE) ? "exclusive" : "window");
    return 1;
}

/* ---- ini key bridge for the tilde console (spec section 7) --------------- */

/** The console's set/get over the keys that are not option-table rows - the N64
  * keyboard binds and Port action binds.
 * Table rows go through port_options.c (portOptSetIni); dispatch order matches
 * videoLoadConfig: N64 binds, Port binds, then the three option setters.
 * Returns 1 = applied and saved, 0 = unknown key, -1 = a bind value naming a
 * key SDL does not know (refused whole). */

/** Join one bind slot's scancodes back into midas.ini text ("W,Up"). */
static void videoBindToText(const SDL_Scancode *codes, char *buf, int len)
{
    int k;
    int used = 0;

    buf[0] = '\0';
    for (k = 0; k < BIND_MAX_KEYS; k++)
    {
        const char *name;

        if (codes[k] == SDL_SCANCODE_UNKNOWN)
        {
            continue;
        }
        if (codes[k] >= BIND_MOUSE_BASE)
        {
            int m = (int) codes[k] - BIND_MOUSE_BASE;
            name = (m >= 0 && m < BIND_MOUSE_COUNT) ? sMouseBindNames[m] : NULL;
        }
        else
        {
            int a;
            name = SDL_GetScancodeName(codes[k]);
            for (a = 0; a < BIND_SEPKEY_COUNT; a++)
            {
                if (codes[k] == sSepKeyNames[a].sc)
                {
                    name = sSepKeyNames[a].name; /* SDL's own name contains the ',' separator */
                    break;
                }
            }
        }
        if (name == NULL || name[0] == '\0')
        {
            continue;
        }
        used += SDL_snprintf(buf + used, (size_t) (len - used), "%s%s", used ? "," : "", name);
        if (used >= len - 1)
        {
            break;
        }
    }
}

/* ---- Rebinding from the watch: capture the next key or mouse button ----
 *
 * Arm it, and the next key or mouse button that goes down is latched and handed
 * back as the midas.ini spelling the bind tables use ("W", "Space", "Mouse Left").
 * It watches the SDL keyboard state edge in videoPollPad because that is where
 * the port already reads the keyboard, so there is one definition of "down".
 *
 * Refused keys: the fixed PC keys of spec section 6 are not bindable, and
 * Escape cancels the capture. Binding the console key away would leave no way
 * to open the console, and binding Escape away no way to cancel, so they are
 * refused here once. */
static int s_BindCapArmed;
static SDL_Scancode s_BindCapCode;
static Uint8 s_BindCapPrev[SDL_NUM_SCANCODES];
static u32 s_BindCapPrevMouse;
/* a modifier edge waits for its own release (focused, nothing else
 * pressed meanwhile) - Alt+Tab / Alt+F4 / Alt+Enter / the Windows key start
 * with a modifier that reaches SDL before the window goes */
static SDL_Scancode s_BindCapPendingMod;

static int videoBindCaptureRefused(SDL_Scancode sc)
{
    /* F1 is not refused: it is the mouse_unlock default, an ordinary key (but
     * with mouse_unlock unbound it is the fixed grab toggle, so
     * portVideoIniSetKey refuses it then). Return is allowed too, since it is
     * START's default and would otherwise be impossible to restore. */
    return sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_GRAVE;
}

static int videoBindCapIsModifier(int sc)
{
    return sc >= SDL_SCANCODE_LCTRL && sc <= SDL_SCANCODE_RGUI;
}

/** 1 = the game window has keyboard focus. */
static int videoHasFocus(void)
{
#ifdef GE_XBOX
    return 1;
#else
    return SDL_GetKeyboardFocus() != NULL;
#endif
}

/** A Delete-key press edge, latched so one press is reported once. It is read
  * on the row, not inside a capture, so it never competes with binding Delete
  * itself. */
static int s_DelPrev;
static int s_DelEdge;

s32 portVideoDeleteEdge(void)
{
    const s32 e = s_DelEdge;

    s_DelEdge = 0;
    return e;
}

static void videoDeleteScan(const Uint8 *keys)
{
    const int down = keys[SDL_SCANCODE_DELETE] != 0;

    if (down && !s_DelPrev)
    {
        s_DelEdge = 1;
    }
    s_DelPrev = down;
}

/** Arm the capture. The key held when it is armed does not count. */
void portVideoBindCaptureBegin(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    s_BindCapArmed = 1;
    s_BindCapCode = SDL_SCANCODE_UNKNOWN;
    s_BindCapPendingMod = SDL_SCANCODE_UNKNOWN;
    if (keys != NULL)
    {
        SDL_memcpy(s_BindCapPrev, keys, sizeof(s_BindCapPrev));
    }
    else
    {
        SDL_memset(s_BindCapPrev, 0, sizeof(s_BindCapPrev));
    }
    { /* and neither does the mouse button that opened the row */
        extern u32 portInputMouseButtons(void);
        s_BindCapPrevMouse = portInputMouseButtons();
    }
    /* ...nor wheel notches still queued from BEFORE arming: the wheel drain releases a
     * flick one notch per two polls (videoWheelDrain), so the tail of the scroll that reached
     * this row would otherwise be captured the instant it is armed */
    sWheelUpNotches = 0;
    sWheelDownNotches = 0;
}

void portVideoBindCaptureCancel(void)
{
    s_BindCapArmed = 0;
    s_BindCapCode = SDL_SCANCODE_UNKNOWN;
    s_BindCapPendingMod = SDL_SCANCODE_UNKNOWN;
}

s32 portVideoBindCaptureArmed(void)
{
    return s_BindCapArmed;
}

/** 1 when something was captured, with its midas.ini name in `out`; disarms. An
  * empty `out` means Escape: cancel, do not bind. */
s32 portVideoBindCaptureTake(char *out, s32 len)
{
    SDL_Scancode codes[BIND_MAX_KEYS];
    s32 i;

    if (!s_BindCapArmed || s_BindCapCode == SDL_SCANCODE_UNKNOWN)
    {
        return 0;
    }
    if (s_BindCapCode == SDL_SCANCODE_ESCAPE)
    {
        portVideoBindCaptureCancel();
        if (len > 0)
        {
            out[0] = '\0';
        }
        return 1;
    }
    for (i = 0; i < BIND_MAX_KEYS; i++)
    {
        codes[i] = SDL_SCANCODE_UNKNOWN;
    }
    codes[0] = s_BindCapCode;
    videoBindToText(codes, out, (int) len);
    portVideoBindCaptureCancel();
    return 1;
}

/** Called from videoPollPad while armed: latch the first new press. */
static void videoBindCaptureScan(const Uint8 *keys, u32 mbuttons)
{
    int i;

    if (!s_BindCapArmed || s_BindCapCode != SDL_SCANCODE_UNKNOWN)
    {
        return;
    }
    for (i = 0; i < SDL_NUM_SCANCODES; i++)
    {
        if (keys[i] && !s_BindCapPrev[i])
        {
            if (i == SDL_SCANCODE_ESCAPE)
            {
                s_BindCapCode = SDL_SCANCODE_ESCAPE;   /* the cancel */
                return;
            }
            if (videoBindCapIsModifier(i))
            {   /* not yet - see after the loop */
                if (s_BindCapPendingMod == SDL_SCANCODE_UNKNOWN)
                {
                    s_BindCapPendingMod = (SDL_Scancode) i;
                }
                continue;
            }
            s_BindCapPendingMod = SDL_SCANCODE_UNKNOWN; /* a chord: the modifier was not the answer */
            if (keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT]
                || keys[SDL_SCANCODE_LGUI] || keys[SDL_SCANCODE_RGUI])
            {
                continue;   /* Alt+Enter / Alt+F4 / Win+x are shortcuts, not binds */
            }
            if (!videoBindCaptureRefused((SDL_Scancode) i))
            {
                s_BindCapCode = (SDL_Scancode) i;
                return;
            }
        }
    }
    if (s_BindCapPendingMod != SDL_SCANCODE_UNKNOWN && !keys[s_BindCapPendingMod])
    {   /* a modifier pressed and let go on its own, with the window
         * still focused, is the answer; one released by a focus loss
         * (SDL_ResetKeyboard after Alt+Tab / the Windows key) is not */
        const SDL_Scancode m = s_BindCapPendingMod;

        s_BindCapPendingMod = SDL_SCANCODE_UNKNOWN;
        if (videoHasFocus())
        {
            s_BindCapCode = m;
            return;
        }
    }
    /* SDL mask bit i = SDL button i+1: bits 0..4 are Left..X2 (the held bind
     * codes); bits 5+ are X11 side buttons, never the wheel, which
     * videoBindCaptureWheel captures. */
    for (i = 0; i < BIND_MOUSE_BUTTONS; i++)
    {
        const u32 bit = 1u << i;
        if ((mbuttons & bit) && !(s_BindCapPrevMouse & bit))
        {
            s_BindCapCode = (SDL_Scancode) (BIND_MOUSE_BASE + i);
            return;
        }
    }
    SDL_memcpy(s_BindCapPrev, keys, sizeof(s_BindCapPrev));
    s_BindCapPrevMouse = mbuttons;
}

/** Called from videoGetPad after the wheel drain, while armed: a notch is
  * captured as "Mouse Wheel Up"/"Down" and consumed with the rest
 * of its flick, so it neither moves the watch highlight nor fires the old wheel bind. */
static void videoBindCaptureWheel(void)
{
    if (!s_BindCapArmed || s_BindCapCode != SDL_SCANCODE_UNKNOWN)
    {
        return;
    }
    if (!sWheelUpNow && !sWheelDownNow)
    {
        return;
    }
    s_BindCapCode = (SDL_Scancode) (BIND_MOUSE_BASE
                                    + (sWheelUpNow ? BIND_WHEEL_UP : BIND_WHEEL_DOWN));
    sWheelUpNow = 0;
    sWheelDownNow = 0;
    sWheelUpNotches = 0;
    sWheelDownNotches = 0;
}

/** -1 = not a bind name; else the slot, with *isport saying which table. */
static int videoFindBindSlot(const char *key, int *isport)
{
    int i;

    for (i = 0; i < BIND_COUNT; i++)
    {
        if (SDL_strcasecmp(key, sBindInfo[i].name) == 0)
        {
            *isport = 0;
            return i;
        }
    }
    for (i = 0; i < portJoyPortBindCount() && i < PORT_BIND_MAX; i++)
    {
        if (SDL_strcasecmp(key, portJoyPortBindName(i)) == 0)
        {
            *isport = 1;
            return i;
        }
    }
    return -1;
}

s32 portVideoIniGetKey(const char *key, char *buf, s32 n)
{
    int isport = 0;
    int slot;
    s32 v;

    if (key == NULL || buf == NULL || n <= 0)
    {
        return 0;
    }
    slot = videoFindBindSlot(key, &isport);
    if (slot >= 0)
    {
        videoBindToText(isport ? sPortBindings[slot] : sBindings[slot], buf, (int) n);
        if (buf[0] == '\0')
        {
            SDL_snprintf(buf, (size_t) n, "(unbound)");
        }
        return 1;
    }
    if (portVideoOptionGet(key, &v) || portInputOptionGet(key, &v) || portJoyPortOptionGet(key, &v))
    {
        SDL_snprintf(buf, (size_t) n, "%d", (int) v);
        return 1;
    }
    {   /* a text key `set` accepts and saves (crosshair_color, window_*,
         * padN_style / layout / inputmode, p1_view, playerN_profile, the
         * backend switches...) has no integer getter - answer with the line
         * midas.ini holds, which is what `set` just wrote */
        extern FILE *portRootFopen(const char *name, const char *mode);
        FILE *f = portRootFopen("midas.ini", "r");
        char line[256];
        s32 found = 0;

        if (f == NULL)
        {
            return 0;
        }
        while (!found && fgets(line, sizeof(line), f) != NULL)
        {
            char *s = iniTrim(line);
            char *eq;

            if (s[0] == ';' || s[0] == '#' || s[0] == '[' || (eq = SDL_strchr(s, '=')) == NULL)
            {
                continue;
            }
            *eq = '\0';
            if (SDL_strcasecmp(iniTrim(s), key) == 0)
            {
                SDL_strlcpy(buf, iniTrim(eq + 1), (size_t) n);
                found = 1;
            }
        }
        fclose(f);
        return found;
    }
}

/** RESET TO DEFAULT for mouse and keyboard: every bind -
 *  the port suite's and the N64 slots' - back to its default, live and in
 *  midas.ini (portVideoIniSetKey writes each). The suite first: mouse_unlock
 *  takes F1 back before any row could be refused it. */
void portVideoBindsResetDefaults(void)
{
    s32 i;

    for (i = 0; i < portJoyPortBindCount() && i < PORT_BIND_MAX; i++)
    {
        portVideoIniSetKey(portJoyPortBindName(i), portJoyPortBindDefault(i));
    }
    for (i = 0; i < BIND_COUNT; i++)
    {
        portVideoIniSetKey(sBindInfo[i].name, sBindInfo[i].defkeys);
    }
}

s32 portVideoIniSetKey(const char *key, const char *value)
{
    char work[256];
    char section[16];
    const char *keys[1];
    const char *vals[1];
    int isport = 0;
    int slot;

    if (key == NULL || value == NULL)
    {
        return 0;
    }
    SDL_strlcpy(work, value, sizeof(work));

    slot = videoFindBindSlot(key, &isport);
    if (slot >= 0)
    {
        SDL_Scancode tmp[BIND_MAX_KEYS];

        /* parse aside first: one name SDL does not know refuses the whole
         * value (-1) and leaves the live bind and midas.ini alone - it used to
         * clear the bind and save the typo. "" stays the explicit unbind. */
        if (videoParseBindValue(work, tmp, key) > 0)
        {
            return -1;
        }
        if (!portVideoMouseUnlockBound())
        {   /* F1 is the fixed grab toggle while mouse_unlock is empty;
             * on any other row it would be a bind that never fires */
            extern s32 portJoyPortActMouseUnlock(void);
            int k;

            for (k = 0; k < BIND_MAX_KEYS; k++)
            {
                if (tmp[k] == SDL_SCANCODE_F1 && !(isport && slot == (int) portJoyPortActMouseUnlock()))
                {
                    portConLogf("ge: midas.ini: F1 is the mouse unlock key while mouse_unlock is unbound - not bound to %s\n", key);
                    return -1;
                }
            }
        }
        /* live at once: videoGetPad reads sBindings/sPortBindings per poll */
        SDL_memcpy(isport ? sPortBindings[slot] : sBindings[slot], tmp, sizeof(tmp));
        /* saved as live, in SDL's spelling, to the keyboard's record */
        videoKbmBindsSave();
        return 1;
    }

    /* tickhz is derived from maxfps, so a live `set` must not store a number
     * (the sim would drift from the frame cap and the file revert). Only hw /
     * -1 (expert pacing) goes through; any other value just leaves an ini
     * tickhz=hw and maxfps decides the rate. */
    if (SDL_strcasecmp(key, "tickhz") == 0
        && SDL_strcasecmp(value, "hw") != 0 && SDL_atoi(value) != -1)
    {
        if (s_CfgTickHz != -1 || s_CfgMaxFps == -1)
        {   /* derived already, or hw is Max FPS "hardware": maxfps owns it */
            portConLogf("ge: video: tickhz follows maxfps - set maxfps instead (hardware = N64 pacing), or tickhz hw\n");
            return 1;
        }
        portTickSetHwPace(0);          /* off hw at the authored rate (the hw arm left the tick at 60)... */
        s_CfgTickHz = 60;
        portTickSetHz(60);
        videoRoomCacheAfterHw();       /* restore room_cache if hw pacing had turned it off */
        s_CfgTickSeenInIni = 1;        /* ...and the file's hw line is what gets replaced */
        s_CfgTickAsWritten = -1;
        videoCoupleTickToMaxFps();     /* maxfps decides the rate (persists when it moves it) */
        videoPersistDerivedTick();     /* and when it did not (maxfps 1..60): no-op otherwise */
        if (s_VideoInitDone)
        {
            gfx_set_target_fps((portTickHz() != 60 || s_CfgMaxFps < 0) ? 0 : s_CfgMaxFps); /* portVideoOptionSet's tail */
        }
        return 1;
    }

    /* Keys outside the option table parse numbers with SDL_atoi, so on/true/yes
     * read as 0; toggle words become their number here. Only for keys with an
     * integer getter, so text values like a profile or layout named "On" stay
     * text. */
    {
        s32 dummy;

        if (!portVideoOptionGet(key, &dummy) && !portInputOptionGet(key, &dummy)
            && !portJoyPortOptionGet(key, &dummy))
        {
            goto notoggle;
        }
    }
    if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "true") == 0 || SDL_strcasecmp(value, "yes") == 0)
    {
        value = "1";
    }
    else if (SDL_strcasecmp(value, "off") == 0 || SDL_strcasecmp(value, "false") == 0 || SDL_strcasecmp(value, "no") == 0)
    {
        value = "0";
    }
    else if (SDL_strcasecmp(key, "render_thread") == 0 && SDL_strcasecmp(value, "auto") == 0)
    {
        value = "-1";
    }
notoggle:
    SDL_strlcpy(work, value, sizeof(work));

    if (videoSetDisplayOption(key, work))
    {
        SDL_strlcpy(section, "[display]", sizeof(section));
    }
    else
    {
        extern s32 portInputSetOption(const char *key, const char *value);
        extern s32 portInputPadSeedForKey(const char *key);
        SDL_strlcpy(work, value, sizeof(work)); /* the setters may have chewed it */
        /* a live padN_<control> write (console `set`) seeds the slot's map
         * first, or the slot turns custom holding this one bind */
        portInputPadSeedForKey(key);
        if (portInputSetOption(key, work) || portJoyPortSetOption(key, work))
        {
            SDL_strlcpy(section, "[input]", sizeof(section));
        }
        else
        {
            return 0;
        }
    }
    keys[0] = key;
    vals[0] = value;
    portIniWriteStrs(section, keys, vals, 1);
    if (SDL_strcasecmp(key, "tickhz") == 0)
    {   /* only hw / -1 reaches here now: the file holds that line, so leaving
         * hw by Max FPS must rewrite it (videoPersistDerivedTick) */
        s_CfgTickSeenInIni = 1;
        s_CfgTickAsWritten = -1;
    }
    return 1;
}

/**
 * Fill pad 0 from the keyboard map + mouse (left button = START, so menus
 * can be advanced remotely). SDL_GetKeyboardState/SDL_GetMouseState return
 * state cached by the frame pump - no extra event handling needed.
 */
/* Wheel notch queue -> per-poll levels (sWheelUpNow/DownNow). Shared by the
 * live poll and the GE_NOINPUT harness path. */
static void videoWheelDrain(void)
{
    /* Wheel: one notch is down for one poll only. A flick queues several
     * notches, so a notch is emitted only when the previous poll was up, giving
     * N edges over 2N polls instead of one held press. */
    { /* Diagnostics only: GE_AUTOWHEEL=n,poll queues n wheel-down notches on
       * that poll, and GE_WHEELALT=0 drains one notch per poll. */
        static long awn = -1, awpoll, awalt = 1;
        static u32 awpolls;
        if (awn < 0)
        {
            const char *e = getenv("GE_AUTOWHEEL");
            const char *a = getenv("GE_WHEELALT");
            awn = 0;
            if (e != NULL && sscanf(e, "%ld,%ld", &awn, &awpoll) != 2) awn = 0;
            if (a != NULL && a[0] == '0') awalt = 0;
        }
        awpolls++;
        if (awn > 0 && awpolls == (u32) awpoll)
        {
            long i;
            for (i = 0; i < awn; i++) videoMouseWheel(-1);
        }
        if (!awalt)
        {
            sWheelUpPrev = 0;
            sWheelDownPrev = 0;
        }
    }
    sWheelUpNow = (sWheelUpNotches > 0) && !sWheelUpPrev;
    sWheelDownNow = (sWheelDownNotches > 0) && !sWheelDownPrev;
    sWheelUpPrev = sWheelUpNow;
    sWheelDownPrev = sWheelDownNow;
    if (sWheelUpNow) sWheelUpNotches--;
    if (sWheelDownNow) sWheelDownNotches--;
}

void videoGetPad(OSContPad *pad)
{
    /* GE_NOINPUT=1: automated capture runs must not pick up stray clicks
     * (they steer the attract into menus mid-capture). */
    static int noinput = -1;
    u16 buttons = 0;

    if (noinput < 0)
    {
        noinput = getenv("GE_NOINPUT") != NULL;
    }

    if (s_VideoInitDone && noinput)
    {
        u8 pdown[PORT_BIND_MAX];
        SDL_memset(pdown, 0, sizeof(pdown));
        if (getenv("GE_AUTOWHEEL") != NULL)
        {
            /* Wheel-flick test harness under GE_NOINPUT: run the wheel drain (synthetic
             * notches only - no SDL wheel events reach a headless run) and
             * scan just the WHEEL codes of the Port binds, so the flick
             * repro works while real keys/buttons stay ignored. */
            int i, k;
            int n = portJoyPortBindCount();
            if (n > PORT_BIND_MAX) n = PORT_BIND_MAX;
            videoWheelDrain();
            for (i = 0; i < n; i++)
            {
                for (k = 0; k < BIND_MAX_KEYS; k++)
                {
                    SDL_Scancode m = sPortBindings[i][k];
                    /* wheel codes only (mbuttons 0, keys never read for them) */
                    if (m >= BIND_MOUSE_BASE && videoCodeDown(m, NULL, 0))
                    {
                        pdown[i] = 1;
                        break;
                    }
                }
            }
        }
        portJoyPortPoll(pdown); /* nothing held; GE_JOYPORT_AUTO still fires */
    }
    if (s_VideoInitDone && noinput && getenv("GE_FAKEMOUSE") != NULL)
    {
        /* GE_NOINPUT + GE_FAKEMOUSE: keep the synthetic mouse alive (it lives
         * in portInputMouseUpdate) while real keys/buttons stay ignored - a
         * headless run on a desktop where someone else is typing. */
        extern void portInputMouseUpdate(void);
        portInputMouseUpdate();
    }
    if (s_VideoInitDone && !noinput)
    {
        extern void portInputMouseUpdate(void);
        extern u32 portInputMouseButtons(void);
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        u32 mbuttons;
        int stickx = 0;
        int sticky = 0;
        int slotN64 = 0; /* player 1's slot is N64-driven: analog_* stay off the stick */
        int i, k;
        const Uint8 *rawKeys = keys; /* unblanked, for the mouse unlock bind */
        u32 rawMb;
        int kbmOnly;                 /* blanked by p1_hybrid=0 alone */

        portInputMouseUpdate(); /* capture/release + sample look delta */
        mbuttons = portInputMouseButtons();
        rawMb = mbuttons;
        { /* Watch/sheet rebinding (when armed) and its Delete unbind. While the
           * console is open the keyboard and mouse are the prompt's, so these
           * scanners only copy the real state into their previous-state
           * snapshots (also on the poll after close), and an armed capture
           * takes the first new press after. */
            static int sConWasOpen;
            static int sFocusWas = 1;
            const int conNow = portConIsOpen() != 0;
            const int focusNow = videoHasFocus();

            /* nor while the window is unfocused, and not on the first
             * poll back - the click that returns to the game is not a bind */
            if (conNow || sConWasOpen || !focusNow || !sFocusWas)
            {
                SDL_memcpy(s_BindCapPrev, keys, sizeof(s_BindCapPrev));
                s_BindCapPrevMouse = mbuttons;
                s_DelPrev = keys[SDL_SCANCODE_DELETE] != 0;
                if (!focusNow)
                {
                    s_BindCapPendingMod = SDL_SCANCODE_UNKNOWN; /* its release is the focus loss's */
                }
            }
            else
            {
                videoBindCaptureScan(keys, mbuttons); /* watch rebinding, when armed */
                videoDeleteScan(keys);                /* ...and its unbind key */
            }
            sConWasOpen = conNow;
            sFocusWas = focusNow;
        }
        kbmOnly = !portConIsOpen() && !s_BindCapArmed && !portBindCooldownActive()
                  && portInputP1KbmMuted() && portStageRunning() && portP1ControlsLive();
        if (portConIsOpen() || (portInputP1KbmMuted() && portStageRunning() && portP1ControlsLive())
            || s_BindCapArmed || portBindCooldownActive())
        {
            /* Armed, or just finished: the keyboard belongs to the capture.
             * videoBindCaptureScan has already seen the real keys, so the bind
             * still lands; everything else those keys would do is blocked (e.g.
             * WASD moving the watch highlight).
             *
             * Also zeroed with P1 hybrid off (a pad owns player 1 alone,
             * input_pc.c portInputP1KbmMuted; gated on controls being live so
             * the frontend keeps the keyboard) and while the console is open
             * (the sources are zeroed rather than skipping the block, so the
             * wheel still drains and portInputMouseUpdate, F1 and window focus
             * keep working). */
            static const Uint8 sNoKeys[SDL_NUM_SCANCODES];

            /* WHAT IS DOWN NOW IS HELD OVER when the blank lifts - see below */
            SDL_memcpy(sHeldOverKeys, keys, SDL_NUM_SCANCODES);
            sHeldOverMouse = mbuttons;
            sHeldOverAny = 1;
            keys = sNoKeys;
            mbuttons = 0;
            s_LookDigitalX = 0;
            s_LookDigitalY = 0;
        }
        else if (s_TextActive)
        {   /* Typing on the on-screen keyboard (front.c portOskTick): keys are
             * text, reaching the name through SDL_TEXTINPUT and the pump's
             * Backspace/Return/Escape latch, never the binds (else WASD would
             * walk the grid and Return add a character). Keys only: the mouse
             * still clicks cells, and keys are held over like the arm above so
             * nothing still down when typing stops lands as a press. */
            static const Uint8 sNoKeys[SDL_NUM_SCANCODES];

            SDL_memcpy(sHeldOverKeys, keys, SDL_NUM_SCANCODES);
            sHeldOverAny = 1;
            keys = sNoKeys;
            sHeldOverMouse &= mbuttons;
            mbuttons &= ~sHeldOverMouse;
        }
        else if (sHeldOverAny)
        {   /* A blank must not end in a press: the blank above zeroes keyboard
             * and mouse (bind-capture blackout, console, p1_hybrid off), so a
             * button still down when it lifted looked like a fresh press and
             * confirmed menus under it. Whatever was down during the blank
             * stays blanked until really released (the pad's s_PadCapHold rule,
             * input_pc.c). */
            int any = 0;

            for (i = 0; i < SDL_NUM_SCANCODES; i++)
            {
                if (sHeldOverKeys[i])
                {
                    if (keys[i]) any = 1;
                    else sHeldOverKeys[i] = 0;
                }
            }
            sHeldOverMouse &= mbuttons;
            if (sHeldOverMouse != 0) any = 1;
            if (any)
            {
                for (i = 0; i < SDL_NUM_SCANCODES; i++)
                {
                    sFiltKeys[i] = (Uint8) (keys[i] && !sHeldOverKeys[i]);
                }
                keys = sFiltKeys;
                mbuttons &= ~sHeldOverMouse;
            }
            else
            {
                sHeldOverAny = 0;
            }
        }
        if (keys[SDL_SCANCODE_F1] && !portVideoMouseUnlockBound())
        {   /* mouse_unlock EMPTY: F1 is the fixed grab toggle again
             * (input_pc.c inputFixedKeyDown), so a bind elsewhere must not
             * fire alongside it. Bound, F1 is an ordinary key and stays. */
            if (keys != sFiltKeys)
            {
                SDL_memcpy(sFiltKeys, keys, SDL_NUM_SCANCODES);
            }
            sFiltKeys[SDL_SCANCODE_F1] = 0;
            keys = sFiltKeys;
        }
        { /* `moved` is per poll and several polls can run between one tick and the next mouse
           * report, so latch it until the watch layer reads it (else the
           * crosshair shows only after a click). */
            extern void portInputMouseWindowPos(s32 *x, s32 *y, s32 *moved);
            s32 mv;
            portInputMouseWindowPos(NULL, NULL, &mv);
            if (mv) sMouseMovedLatch = 1;
        }

        videoWheelDrain(); /* one EDGE per notch (helper above) */
        if (portConIsOpen() || (portInputP1KbmMuted() && portStageRunning() && portP1ControlsLive()))
        {
            /* the notch is drained (never banked) but nothing sees it: the
             * wheel levels are what videoCodeDown reads for the WHEEL bind
             * codes, which keys/mbuttons cannot mask. p1_hybrid=0 with a pad
             * as player 1 mutes it in play like the keys */
            sWheelUpNow = 0;
            sWheelDownNow = 0;
        }
        videoBindCaptureWheel();              /* watch rebinding: the wheel, when armed */
        if (s_BindCapArmed || portBindCooldownActive())
        {   /* the blackout covers the wheel too - the rest of a flick
             * after a wheel capture must not fire the bind just made */
            sWheelUpNow = 0;
            sWheelDownNow = 0;
        }
        if (sWheelUpNow) sWheelLatch++;       /* portMouseWheelSteps */
        if (sWheelDownNow) sWheelLatch--;
        { /* raw left button edge/level -> portMouseClick* */
            int down = (mbuttons & SDL_BUTTON_LMASK) != 0;
            if (down && !sMouseClickPrev) sMouseClickLatched = 1;
            sMouseClickPrev = down;
            sMouseClickHeld = down;
        }

        { /* Port suite (joy_port.c): one "down" per action, then its poll */
            u8 pdown[PORT_BIND_MAX];
            int n = portJoyPortBindCount();
            if (n > PORT_BIND_MAX) n = PORT_BIND_MAX;
            SDL_memset(pdown, 0, sizeof(pdown));
            for (i = 0; i < n; i++)
            {
                for (k = 0; k < BIND_MAX_KEYS; k++)
                {
                    if (videoCodeDown(sPortBindings[i][k], keys, mbuttons))
                    {
                        pdown[i] = 1;
                        break;
                    }
                }
            }
            {   /* Mouse unlock: a press edge straight off the key (not
                 * joy_port's per-player latch) so it toggles the grab on every
                 * menu, via portVideoMouseUnlockPressed (joy_port.c
                 * PORTACT_MOUSE_UNLOCK). */
                extern s32 portJoyPortActMouseUnlock(void);
                const int mu = (int) portJoyPortActMouseUnlock();
                int down = (mu >= 0 && mu < n) ? pdown[mu] : 0;

                if (kbmOnly && mu >= 0 && mu < n)
                {   /* p1_hybrid=0 blanks the keyboard for the whole of
                     * play, but the pointer stays grabbed - the key that frees
                     * it must still work, so read it off the real state */
                    for (k = 0; k < BIND_MAX_KEYS && !down; k++)
                    {
                        down = videoCodeDown(sPortBindings[mu][k], rawKeys, rawMb) != 0;
                    }
                }

                if (down && !sMouseUnlockPrev)
                {
                    sMouseUnlockLatched = 1;
                }
                sMouseUnlockPrev = down;
                if (mu >= 0 && mu < n)
                {
                    pdown[mu] = 0;   /* nothing in joy_port consumes it: do not leave a press latched there */
                }
            }
            {   /* CLASSIC MODE TOGGLE: the same straight-off-the-key edge
                 * (joy_port.c PORTACT_CLASSIC_TOGGLE); keys are already blank
                 * here with the console open or during a bind capture */
                extern s32 portJoyPortActClassicToggle(void);
                const int ct = (int) portJoyPortActClassicToggle();
                const int down = (ct >= 0 && ct < n) ? pdown[ct] : 0;

                if (down && !sClassicTogglePrev && portStageRunning())
                {   /* in a stage only: a press on a menu must not wait in the
                     * latch and flip it at the next mission start */
                    sClassicToggleLatched = 1;
                }
                sClassicTogglePrev = down;
                if (ct >= 0 && ct < n)
                {
                    pdown[ct] = 0;
                }
            }
            portJoyPortPoll(pdown);
        }

        { /* mouse-originated aim edge -> portMouseAimPressed() */
            int now = 0;
            for (k = 0; k < BIND_MAX_KEYS; k++)
            {
                if (sBindings[BIND_R][k] >= BIND_MOUSE_BASE
                    && videoCodeDown(sBindings[BIND_R][k], keys, mbuttons))
                {
                    now = 1;
                    break;
                }
            }
            if (now && !sMouseAimPrev) sMouseAimLatched = 1;
            sMouseAimPrev = now;
        }

        { /* spec section 6: is this poll's START held by Return ALONE? Another
           * key on the start bind still toggles the watch (a deliberate
           * rebind); keys is the blanked table while the console is open. */
            int viaret = 0;
            int viaother = 0;
            for (k = 0; k < BIND_MAX_KEYS; k++)
            {
                if (videoCodeDown(sBindings[BIND_START][k], keys, mbuttons))
                {
                    if (sBindings[BIND_START][k] == SDL_SCANCODE_RETURN) viaret = 1;
                    else viaother = 1;
                }
            }
            if (viaret && !viaother)
            {
                sReturnStartPolled = 1;
            }
        }

        sMouseBindMask = 0;
        /* The keyboard shares player 1's slot, and the game judges that slot by
         * the pad's input mode. With pad1_inputmode=n64 the port block that
         * cancels WASD's C-button half never runs, so in n64 mode the analog_*
         * binds stay off the stick. */
        {
            extern s32 portInputPadIsN64(s32 slot);
            slotN64 = portInputPadIsN64(0);
        }
        for (i = 0; i < BIND_COUNT; i++)
        {
            int down = 0;
            int viamouse = 0;
            for (k = 0; k < BIND_MAX_KEYS; k++)
            {
                if (videoCodeDown(sBindings[i][k], keys, mbuttons))
                {
                    down = 1;
                    if (sBindings[i][k] >= BIND_MOUSE_BASE)
                    {
                        viamouse = 1;
                    }
                    break;
                }
            }
            if (!down)
            {
                continue;
            }
            { /* watch (pause) menu up: a MOUSE-driven L / R (aim = right
               * click) must not flip pages or the abort choice - the mouse
               * layer owns those; the watch_prev/watch_next Port binds carry
               * the trigger role (joy_port.c). Keyboard/pad L / R untouched. */
                extern s32 g_portWatchMouseOpen;
                if (viamouse && g_portWatchMouseOpen && (i == BIND_L || i == BIND_R))
                {
                    continue;
                }
            }
            if (sBindInfo[i].mask != 0)
            {
                if (slotN64 && i >= BIND_C_UP && i <= BIND_C_RIGHT && videoKeyIsMoveKey(sBindings[i][k]))
                {   /* G081: a raphnet (or pad1_inputmode=n64) as player 1 made W
                     * also C-up, which tilts the view on 1.1 / 1.3; the key walks
                     * instead (bondview2.c reads it through portVideoKbdDigitalMove) */
                    continue;
                }
                buttons |= sBindInfo[i].mask;
                if (viamouse)
                {
                    sMouseBindMask |= sBindInfo[i].mask;
                }
            }
            else
            {
                switch (i)
                {
                case BIND_ANALOG_UP:    sticky += 85; break;
                case BIND_ANALOG_DOWN:  sticky -= 85; break;
                case BIND_ANALOG_LEFT:  stickx -= 85; break;
                /* Left and right held together: left wins, as Rare's C-left /
                 * C-right steps do on 1.2. LEFT is read first (bind order). */
                case BIND_ANALOG_RIGHT: if (stickx >= 0) { stickx += 85; } break;
                }
            }
        }
        /* the same keys as DIGITAL steps for the port drive block
         * (portVideoKbdDigitalMove): stickx / sticky only ever hold the
         * analog_* keys' +-85, so their signs are the key state */
        s_KbdDigX = (stickx > 0) - (stickx < 0);
        s_KbdDigY = (sticky > 0) - (sticky < 0);
        if (slotN64)
        {   /* an N64 slot reads the stick by Rare's styles (turn / look on
             * 1.2 / 1.4): the Move keys reach the game only as steps */
            stickx = 0;
            sticky = 0;
        }
        pad->stick_x = (s8) stickx;
        pad->stick_y = (s8) sticky;
        s_KbdStickDown = (stickx != 0 || sticky != 0);   /* portVideoKbdStickDown */

        {   /* the LOOK keys are not N64 controls and must not touch the pad -
             * bondviewProcessInput reads them through portVideoLookDigital */
            s32 lx = 0;
            s32 ly = 0;
            s32 b;

            for (b = BIND_LOOK_UP; b <= BIND_LOOK_RIGHT; b++)
            {
                for (k = 0; k < BIND_MAX_KEYS; k++)
                {
                    if (videoCodeDown(sBindings[b][k], keys, mbuttons))
                    {
                        if (b == BIND_LOOK_UP)         ly += 85;
                        else if (b == BIND_LOOK_DOWN)  ly -= 85;
                        else if (b == BIND_LOOK_LEFT)  lx -= 85;
                        else                           lx += 85;
                        break;
                    }
                }
            }
            s_LookDigitalX = lx;
            s_LookDigitalY = ly;
        }

        { /* GE_DUMP_BOND: 1Hz input probe - names the dead link when keys
           * fail on a platform (raw scancode states vs mapped buttons). */
            static int dump = -1;
            static u32 probepolls;
            if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
            probepolls++;
            if (dump && (probepolls % 60) == 0)
            {
                fprintf(stderr, "in: btn=%04x sx=%d sy=%d raw q=%d e=%d spc=%d ret=%d up=%d mouse=%d\n",
                        buttons, stickx, sticky,
                        keys[SDL_SCANCODE_Q], keys[SDL_SCANCODE_E], keys[SDL_SCANCODE_SPACE],
                        keys[SDL_SCANCODE_RETURN], keys[SDL_SCANCODE_UP],
                        (int)(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK));
                fflush(stderr);
            }
        }
    }

    {
        /* GE_AUTOSTART=1 (bring-up): press START for 10 polls every 120
         * (START confirms every screen through to mission start), so a headless
         * run follows the human path to the Dam load.
         * GE_AUTOSTART_AFTER=n / GE_AUTOSTART_UNTIL=n bound the spam to a
         * poll window - AFTER lets the attract demo play first (reproduces
         * the second stage load of a manual run), UNTIL stops the
         * in-mission pause cycling so GE_AUTOFIRE=n (hold Z in bursts from
         * poll n) can exercise the gunfire path. */
        static int autostart = -1;
        static long startafter, startuntil, fireafter, startperiod = 120; /* GE_AUTOSTART_PERIOD=n: press cadence (pause-spam tests) */
        static u32 polls;
        if (autostart < 0)
        {
            const char *e;
            autostart = getenv("GE_AUTOSTART") != NULL;
            e = getenv("GE_AUTOSTART_AFTER");
            startafter = (e != NULL) ? atol(e) : 0;
            e = getenv("GE_AUTOSTART_UNTIL");
            startuntil = (e != NULL) ? atol(e) : 0x7FFFFFFF;
            e = getenv("GE_AUTOFIRE");
            fireafter = (e != NULL) ? atol(e) : 0;
            e = getenv("GE_AUTOSTART_PERIOD");
            if (e != NULL && atol(e) >= 12) { startperiod = atol(e); }
        }
        polls++;
        if (autostart && polls >= (u32) startafter && polls <= (u32) startuntil)
        {
            if ((polls % (u32) startperiod) == (u32) startperiod - 1)
            {
                /* capture what this menu shows one frame before START lands */
                extern int ge_shot_force;
                ge_shot_force = 1;
            }
            if ((polls % (u32) startperiod) < 10)
            {
                buttons |= CONT_START;
            }
        }
        if (fireafter > 0 && polls >= (u32) fireafter && (polls % 120) < 30)
        {
            buttons |= CONT_G; /* Z trigger: fire */
        }
        {
            /* GE_AUTOLOOK=n: hold full stick-down (look up in GE's default
             * control style) from poll n - reproduces sky-overhead views in
             * automated runs. Negative n = stick-up instead. */
            static long lookafter = -12345;
            if (lookafter == -12345)
            {
                const char *e = getenv("GE_AUTOLOOK");
                lookafter = (e != NULL) ? atol(e) : 0;
            }
            if (lookafter > 0 && polls >= (u32) lookafter)
            {
                pad->stick_y = -80;
            }
            else if (lookafter < 0 && polls >= (u32) (-lookafter))
            {
                pad->stick_y = 80;
            }
        }
        {
            /* GE_XHAIRAT=poll:value - set crosshair_color ONCE at that poll,
             * live, no ini write (GE_CONCMD fires before the sprite is loaded). */
            static long xpoll = -1;
            static char xval[16];

            if (xpoll == -1)
            {
                const char *e = getenv("GE_XHAIRAT");

                xpoll = 0;
                if (e != NULL && sscanf(e, "%ld:%15s", &xpoll, xval) != 2)
                {
                    xpoll = 0;
                }
            }
            if (xpoll > 0 && polls == (u32) xpoll)
            {
                portConLogf("ge: xhairat: crosshair_color=%s at poll %ld\n", xval, xpoll);
                videoSetDisplayOption("crosshair_color", xval);
            }
        }
        {
            /* GE_AUTOSTICK=x,y,after,until: hold stick (x,y) for polls
             * [after, until] - drives menus/cursor synthetically at the
             * exact point keyboard analog lands, for automated repros of
             * stick-value handling (menu-reversal hunt). */
            static long asx, asy, asafter = -1, asuntil;
            if (asafter == -1)
            {
                const char *e = getenv("GE_AUTOSTICK");
                asafter = 0;
                if (e != NULL)
                {
                    if (sscanf(e, "%ld,%ld,%ld,%ld", &asx, &asy, &asafter, &asuntil) != 4)
                    {
                        asafter = 0;
                    }
                }
            }
            if (asafter > 0 && polls >= (u32) asafter && polls <= (u32) asuntil)
            {
                pad->stick_x = (s8) asx;
                pad->stick_y = (s8) asy;
            }
        }
        {
            /* GE_AUTOBTN=mask,after,until[,pulse]: OR a button mask (strtol
             * base 0, so 0x10 = R trigger) into the pad for polls
             * [after, until] - the button-side companion to GE_AUTOSTICK
             * (built for the F6.3 lean-snap repro). pulse=1 taps the mask
             * (8 polls on / 8 off) for edge-triggered actions like B
             * skipping intro cameras. GE_AUTOBTN2 = an independent second
             * phase with the same syntax. */
            static long abmask[2], abafter[2] = {-1, -1}, abuntil[2], abpulse[2];
            int abi;
            for (abi = 0; abi < 2; abi++)
            {
                if (abafter[abi] == -1)
                {
                    const char *e = getenv(abi ? "GE_AUTOBTN2" : "GE_AUTOBTN");
                    abafter[abi] = 0;
                    if (e != NULL)
                    {
                        char *end;
                        abmask[abi] = strtol(e, &end, 0);
                        if (sscanf(end, ",%ld,%ld,%ld", &abafter[abi], &abuntil[abi], &abpulse[abi]) < 2)
                        {
                            abafter[abi] = 0;
                        }
                    }
                }
                if (abafter[abi] > 0 && polls >= (u32) abafter[abi] && polls <= (u32) abuntil[abi]
                    && (!abpulse[abi] || (polls % 16) < 8))
                {
                    buttons |= (u16) abmask[abi];
                }
            }
        }
    }
    pad->button = buttons;
}

/* Settings save to midas.ini the moment they change, so a folder Windows will not
 * let the game write loses every change without a word. Say so once at start. */
static void videoIniWritableCheck(void)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    FILE *f;

    if (getenv("GE_WINDOW") != NULL || getenv("GE_NOINISAVE") != NULL)
    {
        return; /* harness runs never write the ini */
    }
    f = portRootFopen("midas.ini", "a");
    if (f != NULL)
    {
        fclose(f);
        return;
    }
    portConLogf("ge: midas.ini: the game folder is not writable, settings will not be saved\n");
    portShowErrorBox("Project Midas: settings will not be saved",
                     "Project Midas cannot write midas.ini in its own folder, so any settings you change "
                     "will be lost when you quit.\n\n"
                     "Move the game folder somewhere you can write to, such as C:\\Games\\ProjectMidas. "
                     "Avoid Program Files. If the folder is under Documents or Desktop, Windows "
                     "Controlled Folder Access may be blocking it.");
}

/* ---- boot flags: the three things a diagnostic launch always wants --------
 *
 * `ProjectMidas -classic -audio_pump -audio_log` starts in Classic Mode with the
 * pump block and the pump log already on, so a capture run needs no console and
 * no ini edit. They are ordinary argument flags, mix-and-match, and each is
 * independent of the others. `-n64_aa` is the same flag as `-classic`: the mode
 * was named after the key it used to share with the AA bit, the split gave it
 * its own, and the old spelling stays as a silent alias so capture scripts and
 * documented commands keep working.
 *
 * WHY NOT ENV VARS: `-audio_pump`/`-audio_log` are console settings, not ini
 * keys, and the only existing way to set them was GE_CONCMD (a console line at
 * the first rendered frame). Flags are the same information, reachable before
 * the first frame, and named after the console verbs so the two agree.
 *
 * The matching rules - dashes, separators, case, and why a bare word is not a
 * flag - live in port/include/boot_flags.h with a test under ctest, because a
 * flag that is silently ignored leaves no trace otherwise. */
static PortBootFlags s_BootFlags;

/* 1 when the flag was given, and the thing it asks for is not already true. */
s32 videoBootClassicWanted(void)
{
    return s_BootFlags.classic && s_CfgClassicMode == 0;
}

s32 videoBootAudioPump(void)
{
    return s_BootFlags.audio_pump;
}

s32 videoBootAudioLog(void)
{
    return s_BootFlags.audio_log;
}

/* Read the flags, and say what was understood. An argument that starts with '-'
 * and matches nothing KNOWN is named in the log: the whole point of a boot flag
 * is that a run is self-documenting, and a flag that does nothing silently is the
 * failure this branch keeps finding. Not an error - the rest of this command
 * line ignores unknown flags too.
 *
 * The test is portBootFlagKnown(), not a local list. The first cut re-matched
 * the names here and reported `--rom-file=/x.n64` and `--lang=jp` as ignored -
 * the documented spelling of two flags that work - because a hand-written list
 * forgets the `=value` form. */
void videoParseBootArgs(int argc, char **argv)
{
    int i;

    portBootFlagsParse(argc, argv, &s_BootFlags);

    for (i = 1; i < argc; i++)
    {
        const char *a = argv[i];

        if (a == NULL || a[0] != '-')
        {
            continue;   /* a bare word is the ROM path or a value, not a flag */
        }
        if (a[1] == '\0')
        {
            continue;   /* a lone "-" is a stdin convention, not a typo */
        }
        if (!portBootFlagKnown(a))
        {
            portConLogf("ge: video: ignoring argument '%s' (not a boot flag)\n", a);
        }
    }
}

/* Apply the boot flags that must land BEFORE the renderer is created.
 *
 * -audio_pump / -audio_log are plain setters and safe anywhere.
 *
 * -n64_aa goes through portClassicToggle(), the same entry point as Tab, so a
 * flag launch and a keypress cannot drift apart: it is Classic Mode, all of it
 * (240p, maxfps=-1, MSAA, the pillars, the filter), not just the AA bit - which
 * since the Phase 3 split is a separate key it happens to reach through the
 * preset's classic_n64aa part. The GUARD matters: entering the mode saves the
 * current values into classic_saved before forcing its own, so entering when
 * Classic Mode is already on would overwrite the pre-classic snapshot with
 * classic values and a later Tab-off would have nothing to restore.
 * `!s_CfgClassicMode` makes it idempotent - and the option table's choke point
 * (optSet) refuses a same-value set for the same reason, so this is belt and
 * braces rather than the only defence.
 *
 * Consequence, stated rather than discovered: this WRITES midas.ini, exactly as
 * pressing Tab does, so the mode persists after the process exits. That is the
 * behaviour a player asked for when they pressed the key; a launch that must
 * leave the file alone sets GE_NOINISAVE (the parts still apply, in memory). */
static void videoApplyBootArgs(void)
{
    extern void portClassicToggle(void);    /* port_options.c; weak absent headless */
    extern void portAudioPumpStatsSet(s32 on);  /* audio_pc.c */
    extern void portAudioPumpLogSet(s32 on);    /* audio_pc.c */
    extern char *getenv(const char *);

    if (videoBootClassicWanted())
    {
        portClassicToggle();
        portConLogf("ge: video: %s -> Classic Mode on (%s)\n",
                    (s_BootFlags.classic_spelling != NULL) ? s_BootFlags.classic_spelling : "-classic",
                    portClassicModeOn() ? "applied" : "did not apply");
    }
    /* GE_TICKHZ wins the tick rate (portTickHz reads it before everything),
     * while Classic Mode sets up hardware pacing - and portTickHwPace() does not
     * look at the rate, so the hold is still priced. The result is a raised tick
     * under a hw hold, which is not a documented mode. It is left permitted
     * rather than refused - both knobs are diagnostics and a reader may be
     * measuring exactly that - but it is never silent, because a mode that
     * exists only by accident is the defect class this branch keeps finding.
     *
     * The test is the RESULTING state, not the boot flag: gating it on
     * videoBootClassicWanted() left the identical combination silent whenever
     * Classic Mode was already on - from n64_aa=1 in midas.ini, or from a Tab
     * press in a previous session - which is the case a reader is least likely
     * to have in mind. */
    if (portEnvTickClash(portClassicModeOn(), getenv("GE_TICKHZ") != NULL))
    {
        portConLogf("ge: video: Classic Mode with GE_TICKHZ set - the tick rate is "
                    "GE_TICKHZ's, not hw pacing's 60, and holds are still priced.\n");
    }
    if (s_BootFlags.audio_pump)
    {
        portAudioPumpStatsSet(1);
        portConLogf("ge: video: -audio_pump -> the pump block is on\n");
    }
    if (s_BootFlags.audio_log)
    {
        portAudioPumpLogSet(1);
        portConLogf("ge: video: -audio_log -> Data/Logs/audio_pump.log\n");
    }
    if (s_BootFlags.skip_screens != 0)
    {
        portConLogf("ge: video: -skip -> %s\n",
                    (s_BootFlags.skip_screens < 0) ? "every boot screen, landing on the file select"
                                                   : "the first boot screens");
    }
    if (s_BootFlags.stage != NULL)
    {
        portConLogf("ge: video: -stage %s -> straight into the mission\n", s_BootFlags.stage);
    }
}

/** -skip [N]: boot screens to skip, 0 = none, -1 = all of them. Read once per
 *  frontend tick by front.c's boot skip, so the chain lives in one place. */
s32 videoBootSkipScreens(void)
{
    return (s32) s_BootFlags.skip_screens;
}

/** -stage <name>: the mission to launch directly, or NULL. The same string
 *  GE_STAGE takes; front.c prefers this over the environment so a script can
 *  write the flag instead of an env assignment. */
const char *videoBootStage(void)
{
    return s_BootFlags.stage;
}

s32 videoInit(void)
{
    struct GfxInitSettings set = {0}; /* zero the fields we don't set */

    {
        extern void portOptCaptureDefaults(void); /* port_options.c: Reset All's defaults */

        portOptCaptureDefaults(); /* before the ini overwrites them */
    }
    videoLoadConfig(); /* midas.ini (needs no SDL init - scancode names are static) */
    videoIniWritableCheck();

    /* ENV OVERRIDES FIRST, THEN THE BOOT FLAGS. The precedence is deliberate and
     * it is command line over environment, the usual one: the flags are the more
     * specific request and they are what a script is written with.
     *
     * It also removes a half-applied mode. These two used to be applied BELOW,
     * after videoApplyBootArgs() - so `GE_N64AA=0 -n64_aa` ran the whole Classic
     * Mode apply (forcing low_res=240, maxfps=-1, msaa) and then cleared the flag
     * underneath it, leaving Classic Mode's settings in force with Classic Mode
     * reported off. Reading the env first means the flag always sees the value it
     * is overriding. */
    { /* [display] n64_aa (the AA BIT): GE_N64AA=0/1 overrides the ini for A/B.
       *
       * DECIDED (Phase 3): it sets the BIT and only the bit. The name says AA,
       * and the preset now has its own key - so `GE_N64AA=1` no longer forces
       * Classic Mode the way it did while one key meant both. That is a
       * behaviour change, stated in CLI.md rather than left to be discovered by
       * someone's A/B script silently measuring something else.
       *
       * No GE_CLASSICMODE is added beside it, deliberately: the preset has a key
       * and a boot flag, and a preset applied from an environment variable
       * would have to run the transition - which snapshots the player's values
       * and WRITES midas.ini. The GE_* knobs in videoInit are values, not
       * actions; -n64_aa already exists for the action, and it says what it
       * writes. */
        extern char *getenv(const char *);
        const char *e = getenv("GE_N64AA");
        if (e != NULL)
        {
            s_CfgN64Aa = (e[0] != '0');
        }
    }
    { /* [display] low_res: GE_LOWRES=1/0 overrides the ini for A/B. */
        extern char *getenv(const char *);
        const char *e = getenv("GE_LOWRES"); /* 0 / 1 (= 240) / 240 / 480 / 720 / 1080 */
        if (e != NULL)
        {
            s_CfgLowRes = videoLowResLines(e);
        }
    }

    { /* [display] room_cache / tri_hoist (interpreter work from the Xbox port):
       * GE_ROOMCACHE=0/1/2 and GE_TRIHOIST=0/1 override
       * the ini for A/B (the shot harness runs both ways).
       *
       * BEFORE the boot flags, with the other env overrides: Classic Mode sets
       * maxfps=-1, and hw pacing forces room_cache to 0 (the fill model reads
       * the interpreter's triangles). Applied after the flags, GE_ROOMCACHE=2
       * put room_cache back ON under hardware pacing, silently - the same
       * half-applied mode the GE_N64AA block above was moved to remove. */
        extern char *getenv(const char *);
        const char *e = getenv("GE_ROOMCACHE");
        {
            const PortEnvRoomCache rc = portEnvRoomCache(e != NULL, (e != NULL) ? SDL_atoi(e) : 0,
                                                         s_CfgTickHz == -1);

            if (rc.touched)
            {
                if (rc.refused)
                {
                    portConLogf("ge: video: GE_ROOMCACHE=%s ignored - tickhz=hw forces room_cache 0 "
                                "(the fill model reads the interpreter's triangles)\n", e);
                }
                s_CfgRoomCache = rc.value;
                portRoomCacheSetMode(s_CfgRoomCache);
            }
        }
        e = getenv("GE_TRIHOIST");
        if (e != NULL)
        {
            s_CfgTriHoist = (e[0] != '0');
            gfx_tri_hoist = s_CfgTriHoist ? 1u : 0u;
        }
        portConLogf("ge: video: room_cache=%d tri_hoist=%d\n", s_CfgRoomCache, s_CfgTriHoist);
    }

    /* The boot flags, after the ini and after the env overrides (so they win),
     * and before the renderer is created below - Classic Mode forces msaa, which
     * has to be in place before gfx_init. */
    videoApplyBootArgs();

    /* Classic Mode reported on, but its settings are not the classic ones.
     *
     * Three doors reach this state and none of them runs the preset: a
     * hand-edited n64_aa=1, GE_N64AA=1, and an env override applied while the
     * mode was already on. The -n64_aa flag cannot repair any of them - its
     * videoBootClassicWanted() guard declines to act on a mode that is already
     * on, which is correct, because re-entering would overwrite the player's
     * saved values with the forced ones. So the half-state is named here rather
     * than silently run, and `cl_classic fix` is the repair.
     *
     * This is the only place both the ini value and the env override are
     * visible, which is why the check lives here and not in the loader. */
    {
        extern s32 portClassicMismatchParts(s32 *idx, s32 max);
        extern const char *portClassicKey(s32 i);
        s32 idx[16];
        s32 n;
        s32 i;

        n = portClassicMismatchParts(idx, (s32) (sizeof(idx) / sizeof(idx[0])));
        if (n > 0)
        {
            char names[192];
            size_t at = 0;

            names[0] = '\0';
            for (i = 0; i < n && i < (s32) (sizeof(idx) / sizeof(idx[0])); i++)
            {
                const char *k = portClassicKey(idx[i]);
                const int wrote = snprintf(names + at, sizeof(names) - at, "%s%s",
                                           (at > 0) ? ", " : "", (k != NULL) ? k : "?");

                if (wrote <= 0 || (size_t) wrote >= sizeof(names) - at)
                {
                    break;   /* truncated: the count is still right */
                }
                at += (size_t) wrote;
            }
            portConLogf("ge: video: Classic Mode is on but %d of its settings are not classic (%s)"
                        " - `cl_classic fix` repairs it\n", (int) n, names);
        }
    }

    { /* GE_WINDOW=WxH: override midas.ini window size (automated capture runs
       * verify widescreen without editing the user's config) */
        const char *e = getenv("GE_WINDOW");
        if (e != NULL)
        {
            int w = 0, h = 0;
            if (sscanf(e, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
            {
                s_CfgWindowW = w;
                s_CfgWindowH = h;
            }
        }
    }

    set.wapi = &gfx_sdl;
    set.rapi = &gfx_opengl_api;
    {   /* "Project Midas - <branch> - Build <count>-<date>" (sysstubs.c) */
        extern const char *portPlatformName(void);
        extern const char *portBuildString(void);
        static char title[128];

        snprintf(title, sizeof(title), "Project Midas - %s - Build %s", portPlatformName(), portBuildString());
        set.window_settings.title = title;
    }
    set.window_settings.width = (s_CfgWindowW > 0) ? s_CfgWindowW : 640;
    set.window_settings.height = (s_CfgWindowH > 0) ? s_CfgWindowH : 480;
    if (s_CfgFullscreen == VIDEO_FS_EXCLUSIVE)
    {   /* an exclusive boot opens straight at EXCLUSIVE's own mode. An
         * ini from before the split kept that mode in window_*, so with no
         * exclusive_* yet, window_* is taken as it (in memory only - the
         * next pick writes exclusive_*). */
        if (s_CfgExclW <= 0 || s_CfgExclH <= 0)
        {
            s_CfgExclW = set.window_settings.width;
            s_CfgExclH = set.window_settings.height;
        }
        set.window_settings.width = s_CfgExclW;
        set.window_settings.height = s_CfgExclH;
    }
    set.window_settings.fullscreen = s_CfgFullscreen != VIDEO_FS_WINDOWED;
    set.window_settings.fullscreen_is_exclusive = s_CfgFullscreen == VIDEO_FS_EXCLUSIVE;
    set.window_settings.maximized = false;
    set.window_settings.x = 100;
    set.window_settings.y = 100;
    set.window_settings.display = s_CfgWindowDisplay;
    if (s_CfgWindowX != VIDEO_POS_UNSET && s_CfgWindowY != VIDEO_POS_UNSET)
    {
        set.window_settings.x = s_CfgWindowX;
        set.window_settings.y = s_CfgWindowY;
        set.window_settings.has_position = true;
    }

    gfx_current_native_viewport.width = 320;
    gfx_current_native_viewport.height = 240;
    gfx_current_native_aspect = 320.0f / 240.0f;

    /* render-quality knobs ([display] in midas.ini) - msaa/aniso/2d before
     * init (they gate framebuffer + sampler setup), filter mode after
     * (it needs the rendering api selected) */
    if (s_CfgMsaa != 1 && s_CfgMsaa != 0 && s_CfgMsaa != 2 && s_CfgMsaa != 4 && s_CfgMsaa != 8)
    {
        portConLogf("ge: midas.ini: msaa=%d not supported (use 1/2/4/8)\n", s_CfgMsaa);
    }
    { /* [display] n64_aa: already applied above, before the boot flags, so the
       * flags take precedence over it. Kept here as a note, not a second read:
       * reading it again would undo -n64_aa. */
    }
    videoApplyAa();
    if (s_CfgAnisotropy >= 2)
    {
        gfx_anisotropy = (s_CfgAnisotropy > 16) ? 16u : (uint32_t)s_CfgAnisotropy;
    }
    gfx_filter_2d = s_CfgFilter2D != 0;
    { /* gfx_low_res itself is gated per frame in videoStartFrame -
       * levels only, never the menus. GE_LOWRES was applied above, before the
       * boot flags, so -n64_aa's forced 240 wins over it. */
    }
    { /* [display] hw_mipmaps: N64 mip chains on G_TL_LOD draws (reduces Low Res
       * moire). GE_HWMIPMAPS=0/1 overrides the ini for A/B. */
        extern char *getenv(const char *);
        const char *e = getenv("GE_HWMIPMAPS");
        if (e != NULL)
        {
            s_MipmapsEnv = SDL_atoi(e) != 0;
            /* Mipmaps belong to Classic Mode, so this knob can contradict a mode
             * that reports itself on. It still wins - it is a diagnostic - but it
             * does not win silently. Nothing else logs the mipmap mode, so this
             * line is the only way to see the contradiction. */
            if (portEnvMipmapClash(s_CfgClassicMode, portClassicPart(CLASSIC_PART_MIPMAPS), s_MipmapsEnv))
            {
                portConLogf("ge: video: GE_HWMIPMAPS=0 with Classic Mode on - mipmaps off "
                            "for this run, though Classic Mode turns them on\n");
            }
        }
        gfx_mipmaps = videoMipmapMode();
    }

    /* Boot phase lines: always on, so a launcher log shows where a freeze
     * happens between the banner and the first `ge: input:` line (window + GL
     * context, controller backends). */
    portConLogf("ge: video: creating window + GL context\n");
    gfx_init(&set);
    portConLogf("ge: video: window + GL context up\n");
    gfx_sdl.set_fullscreen_changed_callback(videoOnFullscreenChanged); /* Port Options: keep the ini value honest after Alt+Enter */

    if (!s_CfgWindowSaveArmed && getenv("GE_WINDOW") == NULL && getenv("GE_NOINISAVE") == NULL)
    {
        s_CfgWindowSaveArmed = 1;
        atexit(videoSaveWindowIni);
    }

    /* [display] maxfps / vsync (0 and 0 = uncapped). At any tickhz other
     * than 60 the gfx pacer is turned off, because portPumpEvents already
     * paces the loop and two pacers on separate clocks lose frames. */
    gfx_set_target_fps((portTickHz() != 60 || s_CfgMaxFps < 0) ? 0 : s_CfgMaxFps); /* -1 = hw pacing paces itself */
    gfx_set_vsync(s_CfgVsync);

    /* The effective timing/display settings in one line, since the ini may
     * differ after coupling and GE_* overrides; swap = the interval the driver
     * really granted. */
    {
        extern void sysLogSystemInfo(void);
        sysLogSystemInfo();
        portConLogf("ge: video: config: maxfps=%d vsync=%d (swap interval %d) tickhz=%d limiter=%d fullscreen=%d msaa=%d low_res=%d filtering=%s render_thread=%d\n",
                s_CfgMaxFps, s_CfgVsync, SDL_GL_GetSwapInterval(), (int) portTickHz(),
                (portTickHz() != 60 || s_CfgMaxFps < 0) ? 0 : s_CfgMaxFps,
                s_CfgFullscreen, s_CfgMsaa, s_CfgLowRes, s_CfgFiltering, s_CfgRenderThread);
    }

    /* one place decides the filter + mip pair; "trilinear" is bilinear plus
     * gfx_mipmaps=2, and a third transcription of the name list here is
     * exactly how the two would drift apart */
    videoApplyFiltering();

    {
        extern void portInputInit(void);
        portInputInit(); /* SDL controllers + mouse (input_pc.c) */
    }

    SDL_StopTextInput();

    portRenderThreadStart(); /* item 15: must be the LAST init step - it moves GL off this thread */

    s_VideoInitDone = true;
    return 0;
}

void videoStartFrame(void)
{
    if (s_VideoInitDone)
    {
        /* GE switches VI resolution at runtime (fr.c g_viRuntimeScreenWidths:
         * 320x240 gameplay, 640x480 hi-res frontend menus). The interpreter's
         * RATIO_X/RATIO_Y scale game coordinates by window/native, so native
         * must track the game's CURRENT mode - a hardcoded 320x240 made every
         * hi-res menu frame render 2x too big, clipped to the top-left. */
        extern s16 viGetX(void);
        extern s16 viGetY(void);
        s16 x = viGetX();
        s16 y = viGetY();
        if (x > 0 && y > 0)
        {
            gfx_current_native_viewport.width = x;
            gfx_current_native_viewport.height = y;
            gfx_current_native_aspect = (f32)x / (f32)y;
        }
        /* Low Res gates on mission frames only: portSetSceneWide is asserted by
         * mission ticks alone, so its self-expiring stamp says "a level is
         * rendering". Menu and cutscene frames render at window res (the
         * 640x480 menu mode must never drop to 240 lines). */
        gfx_low_res = videoRenderLines(y);
        gfx_start_frame();
    }
}

void videoSubmitCommands(Gfx *cmds)
{
    if (s_VideoInitDone)
    {
        /* Presentation regime for this frame's DL (built just now, so a
         * mission tick's portSetSceneWide is fresh; frames nobody asserted
         * decay to pinned): see gfx_ge_aspect_pin. */
        extern uint32_t gfx_ge_aspect_pin;
        s32 wideFresh = videoSceneWideFresh();
        gfx_ge_aspect_pin = (portWideFactor() > 1.001f) ? (wideFresh ? 1u : 2u) : 0u;
        if (gfx_ge_aspect_pin == 1u && videoSceneMpFresh())
        {
            gfx_ge_aspect_pin = 0u;   /* MP wide frame: fully anamorphic */
        }
        s_FrameNum++;

        { /* bring-up probe (GE_DUMP_BOND): which presentation regime */
            static int dump = -1;
            static unsigned n;
            if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
            if (dump && (n++ % 120) == 0)
            {
                fprintf(stderr, "pin=%u wf=%.3f scenewide=%d win=%ux%u\n",
                        gfx_ge_aspect_pin, portWideFactor(), s_SceneWide,
                        gfx_current_window_dimensions.width, gfx_current_window_dimensions.height);
            }
        }

        gfx_run(cmds);
    }
}

void videoEndFrame(void)
{
    if (s_VideoInitDone)
    {
        gfx_end_frame();
    }
}

/* ==== Render-submit pipelining (optimisation-inventory item 15) ====
 *
 * A dedicated thread owns the GL context and runs the whole GL frame
 * (start_frame / gfx_run / end_frame / swap) while the sim thread hands it the
 * finished display list and simulates the next tick. fps still equals tps; no
 * frame is invented.
 *
 * Why it is safe:
 *  - the master DL / dyn heap is double-buffered (dynSwapBuffers) and boss.c
 *  admits a new frame only at pendingGfx < 2;
 *  - stage teardown waits for pendingGfx == 0, so loads never change segments
 *  under an in-flight frame;
 *  - game state read by videoStartFrame/videoSubmitCommands (viGetX/viGetY,
 *  aspect-pin inputs) is snapshotted at submit time;
 *  - sim-thread GL entry points (texture-cache invalidation, vsync) are
 *  deferred to the render thread (gfx_pc.cpp gfx_async_*);
 *  - SDL window events keep pumping on the main thread, which owns the window.
 *
 * The render thread never touches OS message queues (the shim is single-thread
 * cooperative): it bumps a counter and portPumpEvents posts the DONE messages
 * on the sim thread (pcsched.c), preserving pendingGfx accounting.
 *
 * Known edges: fullscreen toggles happen on the main thread while the render
 * thread may be mid-frame (a transient artifact at worst), and the multi-GPU
 * glGetString probe in handle_events reads "?" in this mode. */

struct PortRtTask
{
    Gfx *cmds;
    s16 viX, viY;   /* viGetX/viGetY at submit - native viewport for this frame */
    u32 pin;        /* gfx_ge_aspect_pin for this frame */
    u32 lowRes;     /* gfx_low_res for this frame (Low Res gate, mission frames
                     * only); snapshotted at submit like the pin, since the
                     * render thread otherwise never sees the sync-path gate. */
};

static SDL_Thread *s_RtThread;
static SDL_mutex *s_RtMtx;
static SDL_cond *s_RtCondTask;   /* task placed in the slot (or quit) */
static SDL_cond *s_RtCondSlot;   /* slot emptied - submitter may refill */
static SDL_cond *s_RtCondDone;   /* a frame finished (swap returned) - wakes the pump's vsync lock */
static struct PortRtTask s_RtTask;
static s32 s_RtTaskPending;
static s32 s_RtCompleted;        /* finished frames not yet drained by the pump */
static s32 s_RtQuit;
static s32 s_RtActive;
static s32 s_RtSeenTickHz = -2; /* s_CfgTickHz / s_CfgRenderThread the last */
static s32 s_RtSeenMode = -2;   /* decision saw (portRenderThreadFollowTick) */
static s32 s_RtAtexitArmed;     /* atexit(portRenderThreadStop) once per process */

s32 portRenderAsyncActive(void)
{
    return s_RtActive;
}

static int portRenderThreadProc(void *arg)
{
    extern void gfx_sdl_gl_make_current(void);
    extern void gfx_sdl_gl_release_current(void);
    extern void gfx_async_apply_pending(void);
    extern uint32_t gfx_ge_aspect_pin;

    (void) arg;
    gfx_sdl_gl_make_current();

    for (;;)
    {
        struct PortRtTask t;

        SDL_LockMutex(s_RtMtx);
        while (!s_RtTaskPending && !s_RtQuit)
        {
            SDL_CondWait(s_RtCondTask, s_RtMtx);
        }
        if (!s_RtTaskPending)
        {
            SDL_UnlockMutex(s_RtMtx);
            break; /* quit, nothing queued */
        }
        t = s_RtTask;
        s_RtTaskPending = 0;
        SDL_CondSignal(s_RtCondSlot);
        SDL_UnlockMutex(s_RtMtx);

        /* the render-side halves of videoStartFrame/videoSubmitCommands,
         * fed from the task snapshot instead of live game state */
        if (t.viX > 0 && t.viY > 0)
        {
            gfx_current_native_viewport.width = t.viX;
            gfx_current_native_viewport.height = t.viY;
            gfx_current_native_aspect = (f32) t.viX / (f32) t.viY;
        }
        gfx_ge_aspect_pin = t.pin;
        gfx_low_res = t.lowRes; /* render-thread half of the Low Res gate */
        gfx_async_apply_pending(); /* deferred cache invalidations + vsync */
        gfx_start_frame();
        gfx_run(t.cmds);
        gfx_end_frame();

        SDL_LockMutex(s_RtMtx);
        s_RtCompleted++;
        if (s_RtCondDone != NULL)
        {
            SDL_CondSignal(s_RtCondDone);
        }
        SDL_UnlockMutex(s_RtMtx);
    }
    /* let go of GL: a live stop binds it on the sim thread next, and GLX/EGL
     * refuse a context still current on another thread - even a dead one */
    gfx_sdl_gl_release_current();
    return 0;
}

/* sim thread (schedSubmitTask): snapshot the frame's game-state inputs and
 * queue it. Blocks only if the slot is still full, i.e. the render thread
 * has not yet PICKED UP the previous frame - with pendingGfx < 2 that wait
 * is the pipeline's natural backpressure and stays sub-frame. */
void videoSubmitAsync(Gfx *cmds)
{
    extern s16 viGetX(void);
    extern s16 viGetY(void);
    struct PortRtTask t;

    t.cmds = cmds;
    t.viX = viGetX();
    t.viY = viGetY();
    t.pin = (portWideFactor() > 1.001f) ? (videoSceneWideFresh() ? 1u : 2u) : 0u;
    if (t.pin == 1u && videoSceneMpFresh())
    {
        t.pin = 0u; /* MP wide frame: fully anamorphic (mirror videoSubmitCommands) */
    }
    t.lowRes = videoRenderLines(t.viY); /* mirror videoStartFrame */
    s_FrameNum++;

    SDL_LockMutex(s_RtMtx);
    while (s_RtTaskPending)
    {
        SDL_CondWait(s_RtCondSlot, s_RtMtx);
    }
    s_RtTask = t;
    s_RtTaskPending = 1;
    SDL_CondSignal(s_RtCondTask);
    SDL_UnlockMutex(s_RtMtx);
}

/* sim thread (portPumpEvents): how many frames finished since last asked -
 * the pump posts one DONE message per count. */
s32 portRenderDrainCompletions(void)
{
    s32 n;

    if (s_RtMtx == NULL) /* never started; after a live stop the pump must still
                          * post the DONEs of the (up to 2) frames it finished */
    {
        return 0;
    }
    SDL_LockMutex(s_RtMtx);
    n = s_RtCompleted;
    s_RtCompleted = 0;
    SDL_UnlockMutex(s_RtMtx);
    return n;
}

/* vsync lock: the wait below can sleep (its condvar exists) - without it the
 * pump would spin instead of sleeping, so no lock */
s32 portRenderCanWaitCompletion(void)
{
    return s_RtMtx != NULL && s_RtCondDone != NULL;
}

/* sim thread (portPumpEvents, vsync lock): 1 as soon as a finished frame is
 * waiting to be drained - already, or within `ms` - else 0 on timeout. The
 * frame finishing is the swap returning, i.e. the display's own clock. */
s32 portRenderWaitCompletion(u32 ms)
{
    s32 done;

    if (s_RtMtx == NULL || s_RtCondDone == NULL)
    {
        return 0;
    }
    SDL_LockMutex(s_RtMtx);
    if (s_RtCompleted == 0 && ms != 0)
    {
        SDL_CondWaitTimeout(s_RtCondDone, s_RtMtx, ms);
    }
    done = (s_RtCompleted != 0);
    SDL_UnlockMutex(s_RtMtx);
    return done;
}

/* atexit: registered after videoSaveWindowIni so it runs before it, joining the
 * render thread before teardown. Never join ourselves: a render-side
 * sysFatalError is ge-render's own exit(1), and on Windows that wait never
 * returns and hangs the watchdog's _exit(3) and the close box. */
static void portRenderThreadStop(void)
{
    if (s_RtThread == NULL)
    {
        return;
    }
#ifndef GE_XBOX /* no render thread on the console, and its SDL shim has no thread ids */
    if (SDL_ThreadID() == SDL_GetThreadID(s_RtThread))
    {
        return; /* exit() from the render thread itself: let the teardown run on */
    }
#endif
    SDL_LockMutex(s_RtMtx);
    s_RtQuit = 1;
    SDL_CondSignal(s_RtCondTask);
    SDL_UnlockMutex(s_RtMtx);
    SDL_WaitThread(s_RtThread, NULL);
    s_RtThread = NULL;
    s_RtActive = 0;
}

/* the [display] render_thread rule, one copy: -1 auto = ON when tickhz != 60 */
static s32 portRenderThreadWanted(void)
{
    const char *e = getenv("GE_RENDERTHREAD");
    s32 mode = (e != NULL) ? atoi(e) : s_CfgRenderThread;

    return (mode == 1) || (mode != 0 && portTickHz() != 60);
}

/* called from the tail of videoInit, after every init-time GL call - and
 * from portRenderThreadFollowTick between frames, on a live tick change */
static void portRenderThreadStart(void)
{
    extern void gfx_sdl_gl_release_current(void);
    extern void gfx_sdl_gl_make_current(void);
    extern void gfx_set_async_mode(int on);
    const char *e = getenv("GE_RENDERTHREAD");

    s_RtSeenTickHz = s_CfgTickHz;   /* the inputs this decision saw */
    s_RtSeenMode = s_CfgRenderThread;
    s_RtActive = portRenderThreadWanted();
    if (!s_RtActive)
    {
        portConLogf("ge: video: render thread OFF (%s; tickhz=%d)\n",
                (e != NULL) ? "GE_RENDERTHREAD" : (s_CfgRenderThread >= 0) ? "midas.ini render_thread" : "auto",
                (int) portTickHz());
        return;
    }

    /* once per process: a live restart reuses them, and the drain reads s_RtMtx */
    if (s_RtMtx == NULL)      { s_RtMtx = SDL_CreateMutex(); }
    if (s_RtCondTask == NULL) { s_RtCondTask = SDL_CreateCond(); }
    if (s_RtCondSlot == NULL) { s_RtCondSlot = SDL_CreateCond(); }
    if (s_RtCondDone == NULL) { s_RtCondDone = SDL_CreateCond(); } /* optional: NULL = no vsync lock */
    if (s_RtMtx == NULL || s_RtCondTask == NULL || s_RtCondSlot == NULL)
    {
        portConLogf("ge: video: render thread primitives failed - staying synchronous\n");
        s_RtActive = 0;
        return;
    }

    s_RtQuit = 0; /* a live restart after portRenderThreadStop */
    gfx_set_async_mode(1);
    gfx_sdl_gl_release_current();
    s_RtThread = SDL_CreateThread(portRenderThreadProc, "ge-render", NULL);
    if (s_RtThread == NULL)
    {
        portConLogf("ge: video: render thread failed (%s) - staying synchronous\n", SDL_GetError());
        gfx_set_async_mode(0);
        gfx_sdl_gl_make_current();
        s_RtActive = 0;
        return;
    }
    if (!s_RtAtexitArmed) { s_RtAtexitArmed = 1; atexit(portRenderThreadStop); }
    portConLogf("ge: video: render thread ON (%s; tickhz=%d)\n",
            (e != NULL) ? "GE_RENDERTHREAD" : (s_CfgRenderThread >= 0) ? "midas.ini render_thread" : "auto",
            (int) portTickHz());
}

/** pcsched.c schedSubmitTask, once per frame before choosing sync or async:
  * re-apply the render_thread rule when the tick or key moved.
 * Only here, never from the option setter, because the console runs from the
 * SDL event watch, which in synchronous mode fires inside gfx_start_frame. */
void portRenderThreadFollowTick(void)
{
    extern void gfx_sdl_gl_make_current(void);
    extern void gfx_set_async_mode(int on);
    extern void gfx_async_apply_pending(void);
    s32 want;

    if (!s_VideoInitDone || (s_CfgTickHz == s_RtSeenTickHz && s_CfgRenderThread == s_RtSeenMode))
    {
        return;
    }
    s_RtSeenTickHz = s_CfgTickHz;
    s_RtSeenMode = s_CfgRenderThread;
    want = portRenderThreadWanted();
    if (want == s_RtActive)
    {
        return; /* 120 -> 240, 60 -> hw, forced modes: same answer */
    }
    if (want)
    {
        portRenderThreadStart(); /* the boot handoff, between frames; logs ON */
        return;
    }
    portRenderThreadStop();    /* joins: queued frames finish, the thread releases GL */
    gfx_sdl_gl_make_current();
    gfx_set_async_mode(0);
    gfx_async_apply_pending(); /* texture/room-cache voids + vsync latched since its last frame */
    portConLogf("ge: video: render thread OFF (%s; tickhz=%d)\n",
            (s_CfgRenderThread >= 0) ? "midas.ini render_thread" : "auto", (int) portTickHz());
}

/**
 * midas.ini [display] adzy (env GE_ADZY overrides, as the other knobs do).
 *
 * Bond's mission-start appearance comes from two places: the level's setup
 * intro record supplies the CUFF (bondview_r.c) and solo_char_load switches
 * on that cuff to pick the BODY (bondview2.c). Adzy mode overrides BOTH -
 * CUFF_SNOW plus BODY_Siberian_Special_Forces - so it is the Siberian model
 * wearing the snow cuff, not the parka that the snow cuff normally implies.
 */
/* env override for the knobs below: 0/1 = forced, 2 = no env (read the ini
 * value LIVE - the Port Options page changes it; the old static cache of
 * the resolved value made these the only non-live [display] keys). */
static s32 videoEnvKnob(const char *name, s32 *cache)
{
    if (*cache < 0)
    {
        const char *env = SDL_getenv(name);

        *cache = (env != NULL) ? (SDL_atoi(env) != 0 ? 1 : 0) : 2;
    }
    return *cache;
}

s32 portAdzyMode(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_ADZY", &env);

    return (e == 2) ? (s_CfgAdzy != 0) : e;
}

/**
 * midas.ini [display] unlock_all (env GE_UNLOCKALL overrides, as the other
 * knobs do). Runtime twin of the GE_UNLOCK_ALL compile option: file2.c's
 * three unlock choke points (stage status, cheat bits, 007 flag) return
 * "unlocked" when set. Progression logic untouched - saves still record.
 */
s32 portUnlockAll(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_UNLOCKALL", &env);

    return (e == 2) ? (s_CfgUnlockAll != 0) : e;
}

/**
 * midas.ini [display] pause_pillars (env GE_PAUSEPILLARS overrides). 0 skips
 * the animated 4:3 SIDE pillars of the pause box (bondview2.c overlay
 * block); the watch letterbox and the 220@10 viewport snap stay - they fix
 * the stretched watch face and are not part of this knob.
 */
/**
 * MOUSE AUTO AIM: a player aiming with the mouse never gets Auto-Aim unless
 * Port Options' Mouse Auto Aim (midas.ini [display] mouse_auto_aim) is on.
 * Above every other Auto-Aim setting: the solo option, a player's own
 * Multiplayer Player Options row and the Gameplay Options aim rule all feed
 * bondview2.c's auto-aim getters, which ask this first. The mouse only ever
 * drives player 1, and only while mouse look is on and no controller has
 * taken player 1 (input_pc.c portInputP1Device / portInputP1KbmMuted).
 */
s32 portMouseBlocksAutoAim(s32 player)
{
    extern s32 portInputP1Device(void);     /* input_pc.c: 0 = keyboard and mouse own player 1 */
    extern s32 portInputP1KbmMuted(void);
    extern s32 portInputOptionGet(const char *key, s32 *out);
    extern s32 get_is_ramrom_flag(void);    /* demo playback */
    s32 mouse = 1;

    /* a demo replays a recorded N64 pad, so it keeps the Auto-Aim it was
     * recorded with, whatever the live player 1 device is */
    if (s_CfgMouseAutoAim || player != 0 || get_is_ramrom_flag() != 0)
    {
        return 0;
    }
    portInputOptionGet("mouse", &mouse);
    return mouse && portInputP1Device() == 0 && !portInputP1KbmMuted();
}

/** midas.ini [display] health_wide: the health / armour arcs at the edges (options.c healthAnchorsToEdges) */
s32 portHealthWide(void)
{
    return s_CfgHealthWide != 0;
}

/**
 * midas.ini [display] hud_scale (env GE_HUDSCALE overrides): the in-game HUD
 * on a 640x480 canvas (options.c portHudScaleBegin). It needs 480 rendered
 * lines, so Low Res 240 and Classic Mode's 240-line render keep the N64 size.
 */
s32 portHudScaleActive(void)
{
    extern s16 viGetY(void);
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_HUDSCALE", &env);
    s32 lines;

    if (!((e == 2) ? (s_CfgHudScale != 0) : e) || !s_VideoInitDone)
    {
        return 0;
    }
    lines = (s32) gfx_current_window_dimensions.height;
    {
        const s32 r = (s32) videoRenderLines(viGetY());

        if (r > 0 && r < lines)
        {
            lines = r;
        }
    }
    return lines >= 480;
}

s32 portPausePillars(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_PAUSEPILLARS", &env);

    return (e == 2) ? (s_CfgPausePillars != 0) : e;
}

/** Classic Mode's pillars (bondview2.c portClassicPillarsTick): the pause
 * box's side pillars held up in play while Classic Mode (n64_aa) is on. Part
 * of Classic Mode, not of the Pause Pillars option. */
s32 portClassicPillars(void)
{
    return s_CfgClassicMode && portClassicPart(CLASSIC_PART_PILLARS);
}

/** midas.ini [display] always_crosshair (GE_ALWAYSCROSSHAIR=0/1 forces) - the
 * PD "Show Target" plus in gunfire.c gunDrawSight. Read live per frame. */
s32 portAlwaysCrosshair(void)
{
    extern s32 getPlayerCount(void);
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_ALWAYSCROSSHAIR", &env);

    /* 2+ players read multiplayer's own key */
    return (e == 2) ? (((getPlayerCount() >= 2) ? s_CfgMpAlwaysCrosshair : s_CfgAlwaysCrosshair) != 0) : e;
}

/** midas.ini [display] shadow_alpha (GE_SHADOWALPHA=N forces) - percent of the
 * vanilla drop-shadow vertex alpha, applied at model.c sub_GAME_7F073FC8 (the
 * one setter every shadow intensity goes through). Read live. */
s32 portShadowAlphaPct(void)
{
    static s32 env = -2;
    if (env == -2)
    {
        const char *e = getenv("GE_SHADOWALPHA");
        env = (e != NULL && e[0] != '\0') ? SDL_atoi(e) : -1;
    }
    return (env >= 0) ? env : s_CfgShadowAlpha;
}

/** midas.ini [display] mission_timer (GE_MISSIONTIMER=0/1 forces): the solo
  * elapsed-time readout (gunfire.c portDrawMissionTimer). Read live; default
  * on. */
s32 portMissionTimer(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_MISSIONTIMER", &env);

    return (e == 2) ? (s_CfgMissionTimer != 0) : e;
}

/** midas.ini [display] stereo_pan (GE_STEREOPAN=0/1 forces) - positional sfx
 * panning, propobj.c portSndApplyPan. Read live per sound post, so the
 * Extended Options toggle applies to the next sound either way. Solo is
 * enforced separately, in portSndSetListener. Default ON. */
s32 portStereoPan(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_STEREOPAN", &env);

    return (e == 2) ? (s_CfgStereoPan != 0) : e;
}

/**
 * "RRGGBB" / "#RRGGBB" -> 0xRRGGBB; anything else (empty, "default", "-", a
 * short or malformed value) -> -1 = leave the crosshair alone. Deliberately
 * strict: a typo falls back to vanilla rather than to a half-parsed colour.
 */
static s32 videoParseHexRGB(const char *v)
{
    s32 out = 0;
    s32 i;

    if (v == NULL)
    {
        return -1;
    }
    while (*v == ' ' || *v == '#')
    {
        v++;
    }
    for (i = 0; i < 6; i++)
    {
        s32 c = (u8) v[i];

        if (c >= '0' && c <= '9')      { c -= '0'; }
        else if (c >= 'a' && c <= 'f') { c = c - 'a' + 10; }
        else if (c >= 'A' && c <= 'F') { c = c - 'A' + 10; }
        else                           { return -1; }
        out = (out << 4) | c;
    }
    if (v[6] != 0 && v[6] != ' ' && v[6] != 13 && v[6] != 10)
    {
        return -1; /* trailing junk: not a colour */
    }
    return out;
}

/** gunfire.c: 0xRRGGBB for both crosshairs, or -1 for the vanilla red. */
s32 portCrosshairColor(void)
{
    return s_CfgCrosshairColor;
}

/** A player's own crosshair: -2 = none (follows crosshair_color), -1 = red, else 0xRRGGBB. */
s32 portPlayerCrosshairOwn(s32 player)
{
    return (player >= 0 && player < 4) ? s_CfgPlayerXhair[player] : -2;
}

/** The crosshair player `player` sees: their own with 2+ players, else the global. */
s32 portPlayerCrosshairColor(s32 player)
{
    extern s32 getPlayerCount(void);
    const s32 own = portPlayerCrosshairOwn(player);

    return (getPlayerCount() >= 2 && own >= -1) ? own : s_CfgCrosshairColor;
}

/** Set a player's own crosshair (-2 = follow the global) - live and in midas.ini. */
void portPlayerCrosshairSet(s32 player, s32 rgb)
{
    char key[40];
    char val[16];

    if (player < 0 || player >= 4)
    {
        return;
    }
    s_CfgPlayerXhair[player] = (rgb < -1) ? -2 : rgb;
    SDL_snprintf(key, sizeof(key), "player%d_crosshair_color", (int) player + 1);
    if (rgb < -1)
    {
        SDL_strlcpy(val, "-", sizeof(val));
    }
    else if (rgb < 0)
    {
        SDL_strlcpy(val, "default", sizeof(val));
    }
    else
    {
        SDL_snprintf(val, sizeof(val), "%06X", (unsigned) rgb);
    }
    portVideoIniSetKey(key, val);
}

/* 0..100, whatever the ini or a menu row hands us */
static s32 videoClampPct(s32 v)
{
    if (v < 0)   { v = 0; }
    if (v > 100) { v = 100; }
    return v;
}

/** Volume percent -> the raw 0..0x7FFF the sound code takes. */
static s32 videoVolRaw(s32 pct)
{
    return pct * 0x7FFF / 100;
}

/** Push both volumes into the running game (options.c). ONLY call this once
 *  the sound system is up: it reaches sndApplyVolumeAllSfxSlot and
 *  musicTrack1ApplySeqpVol, which dereference audio state that is still NULL
 *  while midas.ini is being parsed (an access violation at boot). The ini
 *  parser therefore stores only, and the frontend applies at the same points
 *  vanilla forced full volume. */
static void videoApplyVolumes(void)
{
    extern void portApplyGlobalVolumes(s32 musicraw, s32 sfxraw); /* options.c */

    portApplyGlobalVolumes(videoVolRaw(s_CfgMusicVolume), videoVolRaw(s_CfgSfxVolume));
}

/** file2.c / options.c: hud_wide, 0 / 1; -1 = not set yet. */
s32 portHudWideCfg(void)
{
    return s_CfgHudWide;
}

/** The live ratio changed (a watch row) or a save seeds it: adopt it as the global value. */
void portHudWideAdopt(s32 wide)
{
    extern void portOptSave(void); /* port_options.c: rewrite the ini */

    wide = (wide != 0);
    if (wide != s_CfgHudWide)
    {
        s_CfgHudWide = wide;
        portOptSave();
    }
}

/** options.c: one of the six, by PLAYER_OPTION_* index; -1 = none established yet. */
s32 portPlayerOptCfg(s32 which)
{
    if (which < 0 || which >= PLAYEROPT_COUNT || !(s_CfgPlayerOptsMask & (1 << which)))
    {
        return -1;
    }
    return s_CfgPlayerOpt[which];
}

/** options.c / file2.c: the live table changed - adopt it as the global set.
  * `fromload` (a file load) only seeds: once a global set exists,
 * a load must not clobber it, as portAdoptSaveVolumes does. */
void portPlayerOptsAdopt(const s32 *vals, s32 fromload)
{
    extern void portOptSave(void); /* port_options.c: rewrite the ini */
    int i;
    int changed = 0;

    if (fromload && s_CfgPlayerOptsMask == PLAYEROPT_ALL)
    {
        return;
    }
    for (i = 0; i < PLAYEROPT_COUNT; i++)
    {
        const int v = vals[i] != 0;
        if (fromload && (s_CfgPlayerOptsMask & (1 << i)))
        {
            continue;   /* a load seeds only the keys not set yet */
        }
        if (v != s_CfgPlayerOpt[i])
        {
            s_CfgPlayerOpt[i] = v;
            changed = 1;
        }
    }
    s_CfgPlayerOptsMask = PLAYEROPT_ALL;
    if (changed)
    {
        portOptSave();
    }
}

/** options.c / file2.c: the raw volumes to use anywhere vanilla forced full. */
s32 portMusicVolumeRaw(void)
{
    return videoVolRaw(s_CfgMusicVolume);
}

s32 portSfxVolumeRaw(void)
{
    return videoVolRaw(s_CfgSfxVolume);
}

/**
 * file2.c: a save file's settings were just loaded or saved. Adopt its volumes
 * as THE global pair and persist them, so the next boot's intro and menus use
 * what the player last played with. Raw 0..0x7FFF in, percent stored; midas.ini
 * is rewritten only when a value actually changed.
 *
 * fromload = this came from LOADING a file rather than saving one. A load only
 * ever SEEDS: once a global setting exists (midas.ini carried one, or an earlier
 * load seeded it) the global setting is the master and a file must not
 * overwrite it, or an Extended Options change made in the frontend would be
 * silently undone the moment a mission started. Saving always adopts - that is
 * the player moving the vanilla watch sliders, which is a deliberate change.
 */
void portAdoptSaveVolumes(s32 musicraw, s32 sfxraw, s32 fromload)
{
    extern void portOptSave(void); /* port_options.c: rewrite the ini */
    s32 m = videoClampPct((musicraw * 100 + 0x3FFF) / 0x7FFF);
    s32 x = videoClampPct((sfxraw * 100 + 0x3FFF) / 0x7FFF);

    if (fromload && s_CfgVolumesSet)
    {
        return;
    }
    s_CfgVolumesSet = 1;
    if (m == s_CfgMusicVolume && x == s_CfgSfxVolume)
    {
        return;
    }
    s_CfgMusicVolume = m;
    s_CfgSfxVolume = x;
    portOptSave();
}

/** midas.ini [display] aim_lock (GE_AIMLOCK=0/1 forces) - bondview2.c AIM LOCK.
 * Read live per tick. */
s32 portAimLock(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_AIMLOCK", &env);

    return (e == 2) ? (s_CfgAimLock != 0) : e;
}

/** midas.ini [display] mp_aim_lock - what portAimLock is with two or more
 * players (bondview2.c portAimLockActive picks). GE_AIMLOCK forces both. */
s32 portMpAimLock(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_AIMLOCK", &env);

    return (e == 2) ? (s_CfgMpAimLock != 0) : e;
}

/** midas.ini [display] mp_show_names - gunfire.c draws the viewport's player name. */
s32 portMpShowNames(void)
{
    return s_CfgMpShowNames != 0;
}

/** midas.ini [display] mp_sight_on_screen / mp_auto_aim as Rare's MP "Aim" row
 * index (front.c mp_sight_adjust_table: +1 = sight, +2 = auto-aim). */
s32 portMpSightAdjust(void)
{
    return (s_CfgMpSight ? 1 : 0) + (s_CfgMpAutoAim ? 2 : 0);
}

/** midas.ini [display] no_head_roll (GE_NOHEADROLL=0/1 forces) - bondview2.c
 * bondviewMoveAnimationTick. Read live per tick. */
s32 portNoHeadRoll(void)
{
    extern s32 getPlayerCount(void);
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_NOHEADROLL", &env);

    /* 2+ players read multiplayer's own Head Roll row (default On = vanilla);
     * no_head_roll stays solo's. */
    if (e == 2 && getPlayerCount() >= 2)
    {
        return s_CfgMpHeadRoll == 0;
    }
    return (e == 2) ? (s_CfgNoHeadRoll != 0) : e;
}

/** How many explosions explosion.c's explosionCreate may have live at once:
 * EXPLOSION_BUFFER_LEN (100) with [display] unlimited_explosions (solo) /
 * mp_unlimited_explosions (2+ players) on, Rare's 6 off - and ALWAYS 6 in
 * demo playback, which replays the vanilla world. Read per explosion. */
s32 portExplosionCap(void)
{
    extern s32 getPlayerCount(void);
    extern s32 get_is_ramrom_flag(void);
    const s32 on = (getPlayerCount() >= 2) ? s_CfgMpUnlimitedExplosions : s_CfgUnlimitedExplosions;

    return (on && get_is_ramrom_flag() == 0) ? 100 : 6;
}

/** midas.ini [display] mp_friendly_fire: with it off, bondview2.c drops damage
 * one team-mate deals another in 2v2, 3v1 and 2v1. Never in demo playback. */
s32 portMpFriendlyFireOff(void)
{
    extern s32 get_is_ramrom_flag(void);

    return !s_CfgMpFriendlyFire && get_is_ramrom_flag() == 0;
}

/** midas.ini [display] mp_ltk_health: Licence to Kill's one-hit kills in any
 * scenario - lv.c's 200 handicap at match start and bondview2.c's
 * health-plus-armour damage. 2+ players only, never in demo playback. */
s32 portMpLtkHealthActive(void)
{
    extern s32 getPlayerCount(void);
    extern s32 get_is_ramrom_flag(void);

    return s_CfgMpLtkHealth && getPlayerCount() >= 2 && get_is_ramrom_flag() == 0;
}

/** midas.ini [display] mp_no_armor: prop.c leaves the body armour pickups out of
 * an MP stage's setup. 2+ players only, never in demo playback. */
s32 portMpNoArmorActive(void)
{
    extern s32 getPlayerCount(void);
    extern s32 get_is_ramrom_flag(void);

    return s_CfgMpNoArmor && getPlayerCount() >= 2 && get_is_ramrom_flag() == 0;
}

/** midas.ini [display] mp_spawn_weapon - bondview2.c portMpSpawnWithWeapon, 2+ players. */
s32 portMpSpawnWeapon(void)
{
    return s_CfgMpSpawnWeapon != 0;
}

/** midas.ini [display] mp_random_spawns - bondview.c bondviewGetRandomSpawnPadIndex, 2+ players. */
s32 portMpRandomSpawns(void)
{
    return s_CfgMpRandomSpawns != 0;
}

/** midas.ini [display] p1_ends_match (GE_P1ENDSMATCH=0/1 forces): player 1's START
  * at the MP endscreen finishes every player. Read live; default on. */
s32 portMpP1EndsMatch(void)
{
    static s32 env = -1;
    s32 e = videoEnvKnob("GE_P1ENDSMATCH", &env);

    return (e == 2) ? (s_CfgMpP1EndsMatch != 0) : e;
}

/** midas.ini [display] scope_steady - see the SCOPE STEADY block in bondview2.c. */
s32 portVideoScopeSteady(void)
{
    return s_CfgScopeSteady;
}
