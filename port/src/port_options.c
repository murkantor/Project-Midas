/**
 * port_options.c - the Port Options table: the ONE list of midas.ini options
 * the game shows in its own menus (frontend Files -> PORT OPTIONS today, the
 * watch later). Both renderers draw from this table, so adding an option is
 * one row here plus a get/set arm on the owning side (video.c for [display]
 * keys, input_pc.c for [input], joy_port.c for its own).
 *
 * Map + plan: docs/port/port-options-menu.md.
 *
 * Row kinds: TOGGLE (ON/OFF), ENUM (a fixed value list with display text
 * and, when the ini spells it differently, ini text), INT (min..max step,
 * optional unit suffix), TENTHS (fixed-point x10 - the owning side hands
 * floats over as tenths so this file and the ini writer stay integer).
 * A step wraps for every kind (a pad has no "minus" here); left/right on
 * the page steps either way.
 *
 * NO SDL here: this file builds in every configuration (headless included);
 * the value bridges are strong on their owning sides and weak no-ops in
 * libultra_shim.c, so without a renderer the rows read "n/a" and a step
 * does nothing.
 *
 * Persistence: every successful step rewrites the table's keys in midas.ini
 * IN PLACE (portIniWriteStrs, video.c) - only those keys change.
 */
#include <stdio.h>
#include <stdlib.h> /* atexit: portOptSaveHold's quit-time flush */
#include <string.h>
#include <ultra64.h>
#include "port_options.h"

/* strong on the owning side, weak no-op in libultra_shim.c */
extern s32 portVideoOptionGet(const char *key, s32 *out);
extern s32 portVideoOptionSet(const char *key, s32 value);
extern s32 portInputOptionGet(const char *key, s32 *out);
extern s32 portInputOptionSet(const char *key, s32 value);
extern s32 portJoyPortOptionGet(const char *key, s32 *out);
extern s32 portJoyPortOptionSet(const char *key, s32 value);
extern s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n);

enum {
    PORTOPT_CAT_DISPLAY = 0,
    PORTOPT_CAT_GAMEPLAY,
    PORTOPT_CAT_MOUSE,
    PORTOPT_CAT_CONTROLLER,
    PORTOPT_CAT_AUDIO,
    PORTOPT_CAT_COUNT
};

static const char *const sCategoryLabels[PORTOPT_CAT_COUNT] = {
    "Display\n",
    "Gameplay\n",
    "Mouse\n",
    "Controller\n",
    "Audio\n",
};

enum {
    KIND_TOGGLE = 0,
    KIND_ENUM,
    KIND_INT,
    KIND_TENTHS,
    KIND_PCT      /* raw units on the owning side + in the ini; shown and stepped as a percent of `base` */
};

struct PortOpt {
    const char *key;      /* midas.ini key (the bridges dispatch on it) */
    const char *section;  /* midas.ini section the launcher expects it in */
    const char *label;    /* row text, lowercase like the cheat page, trailing \n */
    s32 category;
    s32 kind;
    s32 min;              /* INT/TENTHS range; ENUM: value count */
    s32 max;
    s32 step;
    const char *suffix;   /* INT/TENTHS: unit text after the number ("" = none) */
    s32 base;             /* PCT: the raw value that is 100% */
    const s32 *values;              /* ENUM: the values, in list order */
    const char *const *valueText;   /* ENUM: row text per value */
    const char *const *iniText;     /* ENUM: ini spelling per value, NULL = the number */
};

/* ---- enum value lists --------------------------------------------------- */

/* trilinear is bilinear plus mipmapping on every textured draw, not only
 * the G_TL_LOD ones hw_mipmaps covers - see videoApplyFiltering and gfx_pc.cpp's mip_filter. */
static const s32 sFilterVals[] = { 0, 1, 2, 3 };
static const char *const sFilterText[] = { "Point\n", "Bilinear\n", "3-Point\n", "Trilinear\n" };
static const char *const sFilterIni[]  = { "point", "bilinear", "3point", "trilinear" };

/* Display mode: windowed, "fullscreen" (a borderless window covering the display) or "exclusive"
 * (SDL_WINDOW_FULLSCREEN); the old numeric 0 / 1 values still parse with their old meaning. */
static const s32 sDisplayModeVals[] = { 0, 1, 2 };
/* shown as Windowed / Borderless / Fullscreen (Murk 2026-10-15, as menu-overhaul-spec.md
 * asked); the ini spellings stay, so existing files keep their meaning */
static const char *const sDisplayModeText[] = { "Windowed\n", "Borderless\n", "Fullscreen\n" };
static const char *const sDisplayModeIni[]  = { "windowed", "fullscreen", "exclusive" };

static const s32 sAnisoVals[] = { 0, 2, 4, 8, 16 };
static const char *const sAnisoText[] = { "OFF\n", "2x\n", "4x\n", "8x\n", "16x\n" };

/* [display] msaa: 1 = off, not 0 - that is the ini's own spelling and the
 * launcher's, and changing it would break every existing file. */
static const s32 sMsaaVals[] = { 1, 2, 4, 8 };
static const char *const sMsaaText[] = { "OFF\n", "2x\n", "4x\n", "8x\n" };

/* -1 = "hardware": the N64's own frame pacing on the 60 Hz sim (tickhz=hw in the ini) */
/* includes common extra values such as 40 and 320 */
static const s32 sMaxFpsVals[] = { -1, 0, 30, 40, 50, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240, 280, 300, 320, 360 };
static const char *const sMaxFpsText[] = { "Hardware\n", "Uncapped\n", "30\n", "40\n", "50\n", "60\n", "75\n", "90\n",
                                           "100\n", "120\n", "144\n", "165\n", "180\n", "200\n", "240\n", "280\n",
                                           "300\n", "320\n", "360\n" };

/* Widescreen: Stretch (4:3 picture stretched to the window, the old Off), Wide (the old On),
 * or 4:3 (pillarboxed to a centred 4:3 by video.c portPillarboxFrame) */
static const s32 sWideVals[] = { 0, 1, 2 };
static const char *const sWideText[] = { "Stretch\n", "Wide\n", "4:3\n" };

/* 3D resolution in lines: "hardware" is the N64's 240 (ini 240 or "hw"), and
 * Native (0, the default) is the window's own resolution, listed last so the
 * list reads in ascending order. */
static const s32 sLowResVals[] = { 240, 480, 720, 1080, 0 };
static const char *const sLowResText[] = { "Hardware\n", "480\n", "720\n", "1080\n", "Native\n" };

/* Rare's own two-way rows that are not on/off (video.c s_CfgPlayerOpt) */
/* 1 = UPRIGHT (stick up looks up), 0 = REVERSE, matching what game_options_entries[0]
 * does in play and the watch's controller diagram (options.c sub_GAME_7F0A9AB8). */
static const s32 sLookUpDownVals[] = { 1, 0 };
static const char *const sLookUpDownText[] = { "Upright\n", "Reverse\n" };
static const s32 sAimControlVals[] = { 0, 1 };
static const char *const sAimControlText[] = { "Hold\n", "Toggle\n" };
/* health_wide: a separate 4:3 / Wide setting for the health / armour arcs, so
 * ammo and messages can sit at the screen edges while health stays centred
 * (options.c healthAnchorsToEdges) */
static const s32 sHealthWideVals[] = { 0, 1 };
static const char *const sHealthWideText[] = { "4:3\n", "Wide\n" };

/* Deadzones and the trigger point (Murk 2026-10-15): 0 to 10% in ones, then to
 * 50% in fives, picked from the drop-down list. The ini keeps raw units (of
 * 32768 for a stick, 32767 for a trigger), so an older off-list value such as
 * deadzone=4096 still reads, shows as 13% and steps to its neighbour. */
static const s32 sDeadzoneVals[] = {
    0, 328, 655, 983, 1311, 1638, 1966, 2294, 2621, 2949,
    3277, 4915, 6554, 8192, 9830, 11469, 13107, 14746, 16384
};
static const s32 sTriggerVals[] = {
    0, 328, 655, 983, 1311, 1638, 1966, 2294, 2621, 2949,
    3277, 4915, 6553, 8192, 9830, 11468, 13107, 14745, 16384
};
static const char *const sPctText[] = {
    "0%\n", "1%\n", "2%\n", "3%\n", "4%\n", "5%\n", "6%\n", "7%\n", "8%\n", "9%\n",
    "10%\n", "15%\n", "20%\n", "25%\n", "30%\n", "35%\n", "40%\n", "45%\n", "50%\n"
};

/* Stick Sensitivity: the stick_scale multiplier, 0.5 to 3.0 in tenths */
static const s32 sStickSensVals[] = {
    5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30
};
static const char *const sStickSensText[] = {
    "0.5\n", "0.6\n", "0.7\n", "0.8\n", "0.9\n", "1.0\n", "1.1\n", "1.2\n", "1.3\n", "1.4\n",
    "1.5\n", "1.6\n", "1.7\n", "1.8\n", "1.9\n", "2.0\n", "2.1\n", "2.2\n", "2.3\n", "2.4\n",
    "2.5\n", "2.6\n", "2.7\n", "2.8\n", "2.9\n", "3.0\n"
};
/* the ini spelling: stick_scale is a float there (1.0), never the tenths */
static const char *const sStickSensIni[] = {
    "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2", "1.3", "1.4",
    "1.5", "1.6", "1.7", "1.8", "1.9", "2.0", "2.1", "2.2", "2.3", "2.4",
    "2.5", "2.6", "2.7", "2.8", "2.9", "3.0"
};


#define TOGGLE(key, sec, label, cat)                 { key, sec, label, cat, KIND_TOGGLE, 0, 1, 1, "", 0, NULL, NULL, NULL }
/* a toggle with its own words for off / on (valueText[0], [1]); still a toggle, so on still reads red */
#define TOGGLET(key, sec, label, cat, text)          { key, sec, label, cat, KIND_TOGGLE, 0, 1, 1, "", 0, NULL, text, NULL }
#define ENUMR(key, sec, label, cat, vals, text, ini)  { key, sec, label, cat, KIND_ENUM, (s32)(sizeof(vals) / sizeof(vals[0])), 0, 1, "", 0, vals, text, ini }
#define INTR(key, sec, label, cat, lo, hi, st, suf)   { key, sec, label, cat, KIND_INT, lo, hi, st, suf, 0, NULL, NULL, NULL }
#define TENTHS(key, sec, label, cat, lo, hi, st)      { key, sec, label, cat, KIND_TENTHS, lo, hi, st, "", 0, NULL, NULL, NULL }
#define PCT(key, sec, label, cat, lo, hi, st, base)   { key, sec, label, cat, KIND_PCT, lo, hi, st, "%", base, NULL, NULL, NULL }
/* an enum over raw units shown as a percent of `base`: an off-list value reads as its percent */
#define ENUMP(key, sec, label, cat, vals, text, base) { key, sec, label, cat, KIND_ENUM, (s32)(sizeof(vals) / sizeof(vals[0])), 0, 1, "%", base, vals, text, NULL }

static const struct PortOpt sOpts[] = {
    /* display - [display], video.c */
    ENUMR ("fullscreen",    "[display]", "Display Mode\n",    PORTOPT_CAT_DISPLAY, sDisplayModeVals, sDisplayModeText, sDisplayModeIni),
    ENUMR ("widescreen",    "[display]", "Widescreen\n",      PORTOPT_CAT_DISPLAY, sWideVals, sWideText, NULL),
    TOGGLE("vsync",         "[display]", "VSync\n",           PORTOPT_CAT_DISPLAY),
    ENUMR ("maxfps",        "[display]", "Max FPS\n",         PORTOPT_CAT_DISPLAY, sMaxFpsVals, sMaxFpsText, NULL),
    ENUMR ("filtering",     "[display]", "Texture Filter\n",  PORTOPT_CAT_DISPLAY, sFilterVals, sFilterText, sFilterIni),
    ENUMR ("anisotropy",    "[display]", "Anisotropy\n",      PORTOPT_CAT_DISPLAY, sAnisoVals, sAnisoText, NULL),
    ENUMR ("msaa",          "[display]", "MSAA\n",            PORTOPT_CAT_DISPLAY, sMsaaVals, sMsaaText, NULL),
    TOGGLE("filter_2d",     "[display]", "2D Filtering\n",    PORTOPT_CAT_DISPLAY),
    TOGGLE("high_lod",      "[display]", "High Detail\n",     PORTOPT_CAT_DISPLAY),
    ENUMR ("low_res",       "[display]", "3D Resolution\n",   PORTOPT_CAT_DISPLAY, sLowResVals, sLowResText, NULL),
    TOGGLE("mipmaps",       "[display]", "Mipmaps\n",         PORTOPT_CAT_DISPLAY),
    /* Shown as "Classic Mode" in the menus (ini key n64_aa). The VIDEO sheet also applies the
     * other classic settings (front.c portSheetClassicPreset), but on the
     * console this sets the AA alone. */
    TOGGLE("n64_aa",        "[display]", "Classic Mode\n",    PORTOPT_CAT_DISPLAY),
    /* which parts Classic Mode applies (the CLASSIC sheet); video.c holds them.
     * The first seven follow sClassicKeys below. */
    TOGGLE("classic_res",      "[display]", "3D Resolution\n",  PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_filter",   "[display]", "Texture Filter\n", PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_aniso",    "[display]", "Anisotropy\n",     PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_pacing",   "[display]", "Max FPS\n",        PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_msaa",     "[display]", "MSAA\n",           PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_mipmaps",  "[display]", "Mipmaps\n",        PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_filter2d", "[display]", "2D Filtering\n",   PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_pillars",  "[display]", "4:3 Pillars\n",    PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_hitches",  "[display]", "Load Hitches\n",   PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_n64aa",    "[display]", "N64 AA\n",         PORTOPT_CAT_DISPLAY),
    TOGGLE("classic_composite", "[display]", "Composite Video\n", PORTOPT_CAT_DISPLAY),
    /* Horizontal FOV on a 16:9 picture; 90 is vanilla's 60 vertical (really
     * 91.5, rounded to sit on the grid). The older ini keys hfov= (4:3) and
     * fov= (vertical) are still read (video.c "hfov_wide"). */
    INTR  ("hfov_wide",     "[display]", "Field of View\n",   PORTOPT_CAT_DISPLAY, 70, 140, 5, ""),
    /* show_fps is ini-only now - video.c still reads the key */
    /* mission_timer lives on the gameplay page: the display page is full
     * at PORTOPT_ROWS_MAX 13 */
    TOGGLE("mission_timer", "[display]", "Mission Timer\n",   PORTOPT_CAT_GAMEPLAY),
    /* pause_pillars lives on the gameplay page: the display page is full at 13 */
    TOGGLE("pause_pillars", "[display]", "Pause Pillars\n",   PORTOPT_CAT_GAMEPLAY),
    ENUMR("health_wide",        "[display]", "Health\n",             PORTOPT_CAT_GAMEPLAY, sHealthWideVals, sHealthWideText, NULL),
    /* the folder's HUD row: Rare's ratio option, same as the watch's "hud" row */
    ENUMR("hud_wide",           "[display]", "HUD\n",                PORTOPT_CAT_GAMEPLAY, sHealthWideVals, sHealthWideText, NULL),

    /* gameplay - [display] keys too (that is where the parser + launcher keep them) */
    TOGGLE("scope_steady",       "[display]", "Steady Scope\n",       PORTOPT_CAT_GAMEPLAY),
    TOGGLE("distance_fading",    "[display]", "Distance Fading\n",    PORTOPT_CAT_GAMEPLAY),
    TOGGLE("fix_shootthrough",   "[display]", "Shoot-Through Fix\n",  PORTOPT_CAT_GAMEPLAY),
    TOGGLE("true_enemy_rockets", "[display]", "True Enemy Rockets\n", PORTOPT_CAT_GAMEPLAY),
    /* adzy (play as ssf character) is ini-only */
    TOGGLE("unlock_all",         "[display]", "Unlock All\n",         PORTOPT_CAT_GAMEPLAY),
    TOGGLE("always_crosshair",   "[display]", "Always Crosshair\n",   PORTOPT_CAT_GAMEPLAY),
    /* shadow_alpha is ini-only - video.c still reads it */
    TOGGLE("aim_lock",           "[display]", "Aim Lock\n",           PORTOPT_CAT_GAMEPLAY),
    TOGGLE("head_roll",          "[display]", "Stamina Sway\n",       PORTOPT_CAT_GAMEPLAY), /* replaces the inverted no_head_roll;
                                                                                              * formerly labelled "Head Roll" */
    /* Multiplayer-only copies, used with two or more players and set on the MP Options sheet.
     * The first two split Rare's four-way MP "Aim" row into its two halves. */
    TOGGLE("mp_sight_on_screen", "[display]", "Sight on Screen\n",    PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_no_auto_aim",     "[display]", "No Auto Aim\n",        PORTOPT_CAT_GAMEPLAY), /* On = off for everyone, Off = per player */
    TOGGLE("mp_aim_lock",        "[display]", "Aim Lock\n",           PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_show_names",      "[display]", "Player Names\n",       PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_always_crosshair","[display]", "Always Crosshair\n",   PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_fix_shootthrough","[display]", "Shoot-Through Fix\n",  PORTOPT_CAT_GAMEPLAY),
    /* port-added multiplayer options */
    TOGGLE("mp_spawn_weapon",    "[display]", "Spawn with Weapon\n",  PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_random_spawns",   "[display]", "Random Spawns\n",      PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_head_roll",       "[display]", "Stamina Sway\n",       PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_unlimited_explosions", "[display]", "Unlimited Explosions\n", PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_ltk_health",      "[display]", "LTK Health\n",         PORTOPT_CAT_GAMEPLAY), /* one-hit kills, any scenario */
    TOGGLE("mp_no_armor",        "[display]", "No Armor\n",           PORTOPT_CAT_GAMEPLAY), /* no body armour pickups */
    TOGGLE("mp_friendly_fire",   "[display]", "Friendly Fire\n",      PORTOPT_CAT_GAMEPLAY), /* team-mates can hurt each other */
    TOGGLE("unlimited_explosions", "[display]", "Unlimited Explosions\n", PORTOPT_CAT_GAMEPLAY), /* Port Options; 100 live, not 6 */
    TOGGLE("mouse_auto_aim",     "[display]", "Mouse Auto Aim\n",     PORTOPT_CAT_GAMEPLAY), /* Port Options; off = a mouse player never gets
                                                                                              * Auto-Aim, whatever else says (video.c) */
    TOGGLE("watch_static",       "[display]", "Watch Static\n",       PORTOPT_CAT_GAMEPLAY), /* Port Options; the watch screen's static
                                                                                              * effect */
    /* multiplayer-only cheats, unlocked from the start and never applied in solo (lv.c);
     * renamed from Bond Invisible / Tiny Bond to Invisibility / Tiny Players */
    TOGGLE("mp_invisibility",    "[display]", "Invisibility\n",       PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_dk_mode",         "[display]", "DK Mode\n",            PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_tiny_players",    "[display]", "Tiny Players\n",       PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_paintball",       "[display]", "Paintball Mode\n",     PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_no_radar",        "[display]", "No Radar\n",           PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_turbo_mode",      "[display]", "Turbo Mode\n",         PORTOPT_CAT_GAMEPLAY),
    TOGGLE("mp_unlimited_ammo",  "[display]", "Unlimited Ammo\n",     PORTOPT_CAT_GAMEPLAY),
    ENUMR("mp_hud_wide",         "[display]", "HUD Layout\n",         PORTOPT_CAT_GAMEPLAY, sHealthWideVals, sHealthWideText, NULL),
    TOGGLE("mp_pillarbox",       "[display]", "Pillarbox\n",          PORTOPT_CAT_GAMEPLAY),
    /* p1_ends_match is ini-only, default ON */
    /* Rare's six player options, global on the port (ini keys behind game_options_entries[0..5],
     * applied by options.c over every save-file load), worded like Rare's watch rows. */
    ENUMR ("look_updown",        "[display]", "Look Up/Down\n",       PORTOPT_CAT_GAMEPLAY, sLookUpDownVals, sLookUpDownText, NULL),
    TOGGLE("auto_aim",           "[display]", "Auto-Aim\n",           PORTOPT_CAT_GAMEPLAY),
    ENUMR ("aim_control",        "[display]", "Aim Control\n",        PORTOPT_CAT_GAMEPLAY, sAimControlVals, sAimControlText, NULL),
    TOGGLE("sight_on_screen",    "[display]", "Sight on Screen\n",    PORTOPT_CAT_GAMEPLAY),
    TOGGLE("look_ahead",         "[display]", "Look Ahead\n",         PORTOPT_CAT_GAMEPLAY),
    TOGGLE("ammo_on_screen",     "[display]", "Ammo on Screen\n",     PORTOPT_CAT_GAMEPLAY),

    /* mouse - [input], input_pc.c */
    TOGGLE("mouse",          "[input]", "Mouse Look\n",      PORTOPT_CAT_MOUSE),
    TOGGLE("smart_mouse_lock","[input]", "Smart Mouse Lock\n", PORTOPT_CAT_MOUSE),
    /* p1_hybrid (default ON): keyboard+mouse and the pad both drive player 1. Off gives
     * player 1 to the pad alone during gameplay; menus always take keys. */
    TOGGLE("p1_hybrid",      "[input]", "P1 Hybrid Input\n", PORTOPT_CAT_MOUSE),
    /* mouse_mode is retired (direct mouse input only; the smoothed mode felt laggy);
     * the key is still parsed as a no-op. */
    TENTHS("mouse_sens_x",   "[input]", "Sensitivity X\n",   PORTOPT_CAT_MOUSE, 5, 100, 5),
    TENTHS("mouse_sens_y",   "[input]", "Sensitivity Y\n",   PORTOPT_CAT_MOUSE, 5, 100, 5),
    TENTHS("mouse_sens_aim", "[input]", "Aim Sensitivity\n", PORTOPT_CAT_MOUSE, 2, 30, 2),

    /* controller - [input], input_pc.c + joy_port.c */
    /* twin_stick and right_stick_c were removed: the control style and pad layout rows cover them.
     * input_pc.c still parses both keys as no-ops so an older midas.ini doesn't log "no such key". */
    /* pad1_swap_sticks is ini-only */
    ENUMP ("deadzone",          "[input]", "Move Deadzone\n",  PORTOPT_CAT_CONTROLLER, sDeadzoneVals, sPctText, 32768),
    ENUMP ("deadzone_look",     "[input]", "Look Deadzone\n",  PORTOPT_CAT_CONTROLLER, sDeadzoneVals, sPctText, 32768),
    ENUMP ("trigger_threshold", "[input]", "Trigger Point\n",  PORTOPT_CAT_CONTROLLER, sTriggerVals, sPctText, 32767),
    ENUMR ("stick_scale",       "[input]", "Stick Sensitivity\n", PORTOPT_CAT_CONTROLLER, sStickSensVals, sStickSensText, sStickSensIni),
    INTR  ("pads",              "[input]", "Controllers\n",    PORTOPT_CAT_CONTROLLER, 1, 4, 1, ""),
    TOGGLE("rumble",            "[input]", "Rumble\n",         PORTOPT_CAT_CONTROLLER),
    TOGGLE("detonate_use_reload","[input]", "Use+Rld Detonate\n", PORTOPT_CAT_CONTROLLER),
    TOGGLE("shoot_next_prev",   "[input]", "Shoot+Next = Prev\n", PORTOPT_CAT_CONTROLLER),
    /* no player number in the label: the row always applies to the selected pad */
    TOGGLE("pad1_gadget_switch","[input]", "Gadget Switch\n", PORTOPT_CAT_CONTROLLER),
    /* per device: the watch and the CONTROL sheet name pad1 in their tables
     * and resolve the selected pad's own slot (portInputDeviceIniKey) */
    TOGGLE("pad2_gadget_switch","[input]", "Gadget Switch\n", PORTOPT_CAT_CONTROLLER),
    TOGGLE("pad3_gadget_switch","[input]", "Gadget Switch\n", PORTOPT_CAT_CONTROLLER),
    TOGGLE("pad4_gadget_switch","[input]", "Gadget Switch\n", PORTOPT_CAT_CONTROLLER),
    /* L+R held half a second = Classic Mode, N64-mode pads (input_pc.c s_PadLrClassic) */
    TOGGLE("pad1_lr_classic",   "[input]", "L+R Classic Mode\n", PORTOPT_CAT_CONTROLLER),
    TOGGLE("pad2_lr_classic",   "[input]", "L+R Classic Mode\n", PORTOPT_CAT_CONTROLLER),
    TOGGLE("pad3_lr_classic",   "[input]", "L+R Classic Mode\n", PORTOPT_CAT_CONTROLLER),
    TOGGLE("pad4_lr_classic",   "[input]", "L+R Classic Mode\n", PORTOPT_CAT_CONTROLLER),
    /* show_input is ini-only: a calibration readout, not a
     * player setting (the launcher still offers it) */

    /* audio - [display] keys (that is where the parser and the launcher keep
     * every non-[input] port key). Percent; options.c applies them live. */
    INTR  ("music_volume",      "[display]", "Music Volume\n",  PORTOPT_CAT_AUDIO, 0, 100, 5, "%"),
    INTR  ("sfx_volume",        "[display]", "Sound Volume\n",  PORTOPT_CAT_AUDIO, 0, 100, 5, "%"),
    TOGGLE("stereo_pan",        "[display]", "Stereo Pan\n",    PORTOPT_CAT_AUDIO),
};

#define PORTOPT_COUNT ((s32)(sizeof(sOpts) / sizeof(sOpts[0])))

/* every section named in the table, in save order */
static const char *const sSections[] = { "[display]", "[input]" };

void portOptSave(void);

/* ---- bridges ------------------------------------------------------------ */

static s32 optGet(const char *key, s32 *out)
{
    return portVideoOptionGet(key, out) || portInputOptionGet(key, out) || portJoyPortOptionGet(key, out);
}

static void classicPartChanged(const char *key, s32 on);   /* "classic mode" below */

static s32 optSet(const char *key, s32 v)
{
    if (portVideoOptionSet(key, v) || portInputOptionSet(key, v) || portJoyPortOptionSet(key, v))
    {
        classicPartChanged(key, v != 0);
        return 1;
    }
    return 0;
}

/** ENUM: list index of a value, -1 when the ini holds something off-list. */
static s32 enumIndex(const struct PortOpt *o, s32 v)
{
    s32 i;

    for (i = 0; i < o->min; i++)
    {
        if (o->values[i] == v)
        {
            return i;
        }
    }
    return -1;
}

/* For a value not in an enum row's list (e.g. maxfps=165), finds its neighbour
 * among the positive entries:
 * dir > 0 = at or below, dir < 0 = at or above, 0 = nearest, and -1 for a value
 * <= 0 (sentinels like Native 0 are skipped). */
static s32 enumIndexNear(const struct PortOpt *o, s32 v, s32 dir)
{
    s32 k;
    s32 below = -1, above = -1, first = -1, last = -1;

    if (v <= 0)
    {
        return -1;
    }
    for (k = 0; k < o->min; k++)
    {
        const s32 e = o->values[k];

        if (e <= 0)
        {
            continue;
        }
        if (first < 0)
        {
            first = k;
        }
        last = k;
        if (e <= v && (below < 0 || e > o->values[below]))
        {
            below = k;
        }
        if (e >= v && (above < 0 || e < o->values[above]))
        {
            above = k;
        }
    }
    if (first < 0)
    {
        return -1;
    }
    if (dir > 0)
    {
        return (below >= 0) ? below : first - 1;
    }
    if (dir < 0)
    {
        return (above >= 0) ? above : last + 1;
    }
    if (below < 0 || above < 0)
    {
        return (below < 0) ? above : below;
    }
    return (v - o->values[below] <= o->values[above] - v) ? below : above;
}

/* ---- table API (front.c menu1A, later the watch) ------------------------ */

s32 portOptCount(void)
{
    return PORTOPT_COUNT;
}

s32 portOptCategoryCount(void)
{
    return PORTOPT_CAT_COUNT;
}

/** Which category a row is in (the console groups its listing by it). */
s32 portOptCategoryOf(s32 i)
{
    return (i < 0 || i >= PORTOPT_COUNT) ? -1 : sOpts[i].category;
}

const char *portOptCategoryLabel(s32 cat)
{
    if (cat < 0 || cat >= PORTOPT_CAT_COUNT)
    {
        return "?\n";
    }
    return sCategoryLabels[cat];
}

const char *portOptLabel(s32 i)
{
    if (i < 0 || i >= PORTOPT_COUNT)
    {
        return "?\n";
    }
    return sOpts[i].label;
}

/** 1 when the owning side answers for this key (a renderer is linked). */
s32 portOptAvailable(s32 i)
{
    s32 v;

    if (i < 0 || i >= PORTOPT_COUNT)
    {
        return 0;
    }
    return optGet(sOpts[i].key, &v);
}

/** 1 = a plain on/off row, so a screen can colour it by its state. */
s32 portOptIsToggle(s32 i)
{
    return (i >= 0 && i < PORTOPT_COUNT) && (sOpts[i].kind == KIND_TOGGLE);
}

s32 portOptGet(s32 i)
{
    s32 v = 0;

    if (i < 0 || i >= PORTOPT_COUNT)
    {
        return 0;
    }
    if (!optGet(sOpts[i].key, &v))
    {
        return 0;
    }
    return v;
}

/** Current value as row text ("ON\n" / "OFF\n" / enum text / number). */
const char *portOptValueText(s32 i)
{
    static char sNum[24];
    const struct PortOpt *o;
    s32 v, idx;

    if (i < 0 || i >= PORTOPT_COUNT)
    {
        return "?\n";
    }
    o = &sOpts[i];
    if (!optGet(o->key, &v))
    {
        return "N/A\n";
    }
    switch (o->kind)
    {
    case KIND_TOGGLE:
        if (o->valueText != NULL)
        {
            return o->valueText[v ? 1 : 0];
        }
        return v ? "ON\n" : "OFF\n";
    case KIND_ENUM:
        idx = enumIndex(o, v);
        if (idx >= 0)
        {
            return o->valueText[idx];
        }
        if (o->base > 0)
        {
            v = (v * 100 + o->base / 2) / o->base;   /* ENUMP: deadzone=4096 reads 13% */
        }
        break;
    case KIND_TENTHS:
        sprintf(sNum, "%s%d.%d%s\n", v < 0 ? "-" : "", (v < 0 ? -v : v) / 10, (v < 0 ? -v : v) % 10, o->suffix);
        return sNum;
    case KIND_PCT:
        v = (v * 100 + o->base / 2) / o->base;
        break;
    default:
        break;
    }
    sprintf(sNum, "%d%s\n", v, o->suffix);
    return sNum;
}


/**
 * Move the value one step in `dir` (wraps for every kind). Applies LIVE
 * through the owning side's setter (the same code path as the midas.ini
 * parse) and persists the table.
 */
void portOptStep(s32 i, s32 dir)
{
    const struct PortOpt *o;
    s32 v, idx, off;

    if (i < 0 || i >= PORTOPT_COUNT || dir == 0)
    {
        return;
    }
    o = &sOpts[i];
    if (!optGet(o->key, &v))
    {
        return;
    }
    dir = (dir > 0) ? 1 : -1;
    switch (o->kind)
    {
    case KIND_TOGGLE:
        v = !v;
        break;
    case KIND_ENUM:
        idx = enumIndex(o, v);
        if (idx < 0)
        {   /* off-list ini value (maxfps=100): step from its neighbour that
             * way, so 100 goes to 120 or 60; with no neighbour by
             * number, enter the list at its end */
            idx = enumIndexNear(o, v, dir);
            if (idx < 0 && dir < 0)
            {
                idx = o->min;
            }
        }
        idx += dir;
        if (idx >= o->min) idx = 0;
        if (idx < 0) idx = o->min - 1;
        v = o->values[idx];
        break;
    default:
        /* Move one grid notch in the arrow's direction; an off-grid value (e.g.
         * 13%) goes to its neighbour that way (15 right, 10 left), and a value
         * outside min..max enters at min or max (PCT rows step in percent). */
        if (o->kind == KIND_PCT)
        {
            v = (v * 100 + o->base / 2) / o->base;
        }
        if (v < o->min || v > o->max)
        {
            v = (dir > 0) ? o->min : o->max;
        }
        else
        {
            off = (v - o->min) % o->step;   /* >= 0: v >= min here */
            v += (off == 0) ? dir * o->step : (dir > 0) ? o->step - off : -off;
            if (v > o->max) v = o->min;
            if (v < o->min) v = o->max;
        }
        if (o->kind == KIND_PCT)
        {
            v = v * o->base / 100;
        }
        break;
    }
    if (optSet(o->key, v))
    {
        portOptSave();
    }
}

/** ini spelling of a row's current value into buf; 0 when unreadable. */
static s32 optIniText(const struct PortOpt *o, char *buf, s32 len)
{
    s32 v, idx;

    if (!optGet(o->key, &v))
    {
        return 0;
    }
    if (o->kind == KIND_ENUM && o->iniText != NULL)
    {
        idx = enumIndex(o, v);
        if (idx >= 0)
        {
            snprintf(buf, len, "%s", o->iniText[idx]);
            return 1;
        }
    }
    if (o->kind == KIND_TENTHS)
    {
        snprintf(buf, len, "%s%d.%d", v < 0 ? "-" : "", (v < 0 ? -v : v) / 10, (v < 0 ? -v : v) % 10);
        return 1;
    }
    snprintf(buf, len, "%d", v);
    return 1;
}

/* Set by portOptSaveHold (below): while it is on, portOptSave writes nothing.
 * Declared up here because portOptSave is the first thing that reads it. */
static s32 sSaveHold;

/**
 * Persist the whole table to midas.ini in place (one rewrite per section,
 * only the table's keys change). Called after every successful step, so a
 * crash later never loses a setting. Skipped by the writer under GE_WINDOW /
 * GE_NOINISAVE (harness runs) and headless (weak stub). Keys the owning
 * side cannot read (no renderer) are left out, never written as 0.
 */
void portOptSave(void)
{
    static char bufs[PORTOPT_COUNT][24];
    const char *keys[PORTOPT_COUNT];
    const char *vals[PORTOPT_COUNT];
    s32 sec, i, n;

    if (sSaveHold)
    {
        return;   /* a held analog ramp: one write when the stick is let go */
    }

    for (sec = 0; sec < (s32)(sizeof(sSections) / sizeof(sSections[0])); sec++)
    {
        n = 0;
        for (i = 0; i < PORTOPT_COUNT; i++)
        {
            if (strcmp(sOpts[i].section, sSections[sec]) == 0 && optIniText(&sOpts[i], bufs[n], sizeof(bufs[n])))
            {
                keys[n] = sOpts[i].key;
                vals[n] = bufs[n];
                n++;
            }
        }
        if (n > 0)
        {
            portIniWriteStrs(sSections[sec], keys, vals, n);
        }
    }
}

/* ---- by-key / by-text API (the tilde console, spec section 7) ------------ */

/* The console addresses options by ini key and sets arbitrary values as text.
 * Everything below goes through the same optSet + portOptSave as the menus, so
 * both save identically. */

static s32 optStrEqI(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
        {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/** Row text compare that ignores the table's trailing newline and case. */
static s32 optRowTextEqI(const char *rowText, const char *s)
{
    s32 i = 0;

    while (rowText[i] != '\0' && rowText[i] != '\n' && s[i] != '\0')
    {
        char ca = rowText[i], cb = s[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
        {
            return 0;
        }
        i++;
    }
    return (rowText[i] == '\0' || rowText[i] == '\n') && s[i] == '\0';
}

/** Table index for an ini key, -1 when it is not an option row. */
s32 portOptFind(const char *key)
{
    s32 i;

    if (key == NULL)
    {
        return -1;
    }
    /* hw_mipmaps is the mipmaps row's old name (video.c still loads it); map it to the row
     * so `set hw_mipmaps on` parses on/off properly instead of a raw ini write */
    if (optStrEqI(key, "hw_mipmaps"))
    {
        key = "mipmaps";
    }
    for (i = 0; i < PORTOPT_COUNT; i++)
    {
        if (optStrEqI(sOpts[i].key, key))
        {
            return i;
        }
    }
    return -1;
}

const char *portOptKey(s32 i)
{
    return (i < 0 || i >= PORTOPT_COUNT) ? NULL : sOpts[i].key;
}

const char *portOptSection(s32 i)
{
    return (i < 0 || i >= PORTOPT_COUNT) ? "" : sOpts[i].section;
}

/**
 * Current value as console text: the ini spelling, except KIND_PCT rows which
 * read in percent (midas.ini keeps raw units via optIniText). Returns 0 when the
 * owning side can't read it.
 */
s32 portOptValueIni(s32 i, char *buf, s32 n)
{
    s32 v;

    if (i < 0 || i >= PORTOPT_COUNT || buf == NULL || n <= 0)
    {
        return 0;
    }
    if (sOpts[i].kind == KIND_PCT)
    {
        if (!optGet(sOpts[i].key, &v))
        {
            return 0;
        }
        /* same rounding as portOptValueText / portOptStep */
        snprintf(buf, n, "%d%%", (v * 100 + sOpts[i].base / 2) / sOpts[i].base);
        return 1;
    }
    return optIniText(&sOpts[i], buf, n);
}

/** What the row accepts, for an error message ("one of: ..." / "40..120"). */
s32 portOptRangeText(s32 i, char *buf, s32 n)
{
    const struct PortOpt *o;
    s32 k, used;

    if (i < 0 || i >= PORTOPT_COUNT || buf == NULL || n <= 0)
    {
        return 0;
    }
    buf[0] = '\0';
    o = &sOpts[i];
    switch (o->kind)
    {
    case KIND_TOGGLE:
        snprintf(buf, n, "on | off");
        return 1;
    case KIND_ENUM:
        used = snprintf(buf, n, "one of:");
        for (k = 0; k < o->min && used > 0 && used < n; k++)
        {
            const char *t = (o->iniText != NULL) ? o->iniText[k] : NULL;
            char tmp[24];

            if (t == NULL)
            {
                snprintf(tmp, sizeof(tmp), "%d", o->values[k]);
                t = tmp;
            }
            used += snprintf(buf + used, n - used, " %s", t);
        }
        return 1;
    case KIND_TENTHS:
        snprintf(buf, n, "%d.%d..%d.%d", o->min / 10, o->min % 10, o->max / 10, o->max % 10);
        return 1;
    case KIND_PCT:
        snprintf(buf, n, "%d..%d%%", o->min, o->max);
        return 1;
    default:
        snprintf(buf, n, "%d..%d step %d", o->min, o->max, o->step);
        return 1;
    }
}

/** "12", "-3", "2.5" -> tenths. 0 = not a number. */
static s32 optParseTenths(const char *s, s32 *out)
{
    s32 sign = 1;
    s32 whole = 0;
    s32 frac = 0;
    s32 digits = 0;

    if (s == NULL || *s == '\0')
    {
        return 0;
    }
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9')
    {
        whole = whole * 10 + (*s - '0');
        s++;
        digits++;
    }
    if (*s == '.')
    {
        s++;
        if (*s >= '0' && *s <= '9')
        {
            frac = *s - '0';
            s++;
            digits++;
        }
        while (*s >= '0' && *s <= '9') s++; /* extra decimals are dropped */
    }
    if (digits == 0)
    {
        return 0;
    }
    if (*s == '%') s++;                      /* "50%" reads as 50 */
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '\0')
    {
        return 0;
    }
    *out = sign * (whole * 10 + frac);
    return 1;
}

/* Legacy ini words the video.c parsers still accept. This is an explicit table rather than calling
 * those parsers, because they map any unknown word to a value and a console
 * typo would silently change the setting. */
static const struct
{
    const char *key;
    const char *text;
    s32 value;
} sEnumAliases[] = {
    { "low_res",    "hw",          240 },
    { "low_res",    "off",         0 },
    { "fullscreen", "borderless",  1 },  /* VIDEO_FS_BORDERLESS */
    { "filtering",  "threepoint",  2 },
    { "filtering",  "three-point", 2 },
};

/**
 * Set a row from text, apply it live and save; returns 0 if the text didn't
 * parse (caller prints portOptRangeText). ENUM rows also accept any raw number,
 * deliberately not limited to the list because midas.ini may hold off-list values.
 */
s32 portOptSetIni(s32 i, const char *text)
{
    const struct PortOpt *o;
    s32 v = 0;
    s32 k;

    if (i < 0 || i >= PORTOPT_COUNT || text == NULL || text[0] == '\0')
    {
        return 0;
    }
    o = &sOpts[i];

    switch (o->kind)
    {
    case KIND_TOGGLE:
        if (optStrEqI(text, "on") || optStrEqI(text, "1") || optStrEqI(text, "true")
            || optStrEqI(text, "yes"))
        {
            v = 1;
        }
        else if (optStrEqI(text, "off") || optStrEqI(text, "0") || optStrEqI(text, "false")
                 || optStrEqI(text, "no"))
        {
            v = 0;
        }
        else
        {
            return 0;
        }
        break;

    case KIND_ENUM:
        for (k = 0; k < o->min; k++)
        {
            if (o->iniText != NULL && optStrEqI(o->iniText[k], text))
            {
                v = o->values[k];
                goto have_value;
            }
        }
        for (k = 0; k < o->min; k++)
        {
            if (optRowTextEqI(o->valueText[k], text))
            {
                v = o->values[k];
                goto have_value;
            }
        }
        for (k = 0; k < (s32)(sizeof(sEnumAliases) / sizeof(sEnumAliases[0])); k++)
        {
            if (optStrEqI(sEnumAliases[k].key, o->key) && optStrEqI(sEnumAliases[k].text, text))
            {
                v = sEnumAliases[k].value;
                goto have_value;
            }
        }
        if (!optParseTenths(text, &v))
        {
            return 0;
        }
        v /= 10; /* enums are whole numbers */
        break;

    case KIND_TENTHS:
        if (!optParseTenths(text, &v))
        {
            return 0;
        }
        if (v < o->min || v > o->max)
        {
            return 0;
        }
        break;

    case KIND_PCT:
        if (!optParseTenths(text, &v))
        {
            return 0;
        }
        v /= 10;
        if (v < o->min || v > o->max)
        {
            return 0;
        }
        v = v * o->base / 100;
        break;

    default: /* KIND_INT */
        if (!optParseTenths(text, &v))
        {
            return 0;
        }
        v /= 10;
        if (v < o->min || v > o->max)
        {
            return 0;
        }
        break;
    }

have_value:
    if (!optSet(o->key, v))
    {
        return 0;
    }
    portOptSave();
    return 1;
}

/* ---- value-list API (the watch pages) ------------------------------------ */

/* The watch shows rows with three or fewer values all on one line and longer
 * ones as one value with arrows, so it needs the full list; a count of 0 means
 * a ranged row that is stepped instead. */

/**
 * A ranged row's values as a list (min, min+step, ... max), so every ranged row
 * such as FOV gets a drop-down. KIND_PCT grids are in percent, matching how
 * portOptStep steps them.
 */
static s32 rangeCount(const struct PortOpt *o)
{
    if (o->step <= 0 || o->max < o->min)
    {
        return 0;
    }
    return (o->max - o->min) / o->step + 1;
}

static s32 rangeValueAt(const struct PortOpt *o, s32 idx)
{
    return o->min + idx * o->step;
}

/** A grid value as row text. `v` is already in the row's STEPPED units, which
 *  for KIND_PCT means percent - the same units portOptValueText prints. */
static const char *rangeText(const struct PortOpt *o, s32 v)
{
    static char text[24];

    if (o->kind == KIND_TENTHS)
    {
        sprintf(text, "%s%d.%d%s\n", v < 0 ? "-" : "", (v < 0 ? -v : v) / 10,
                (v < 0 ? -v : v) % 10, o->suffix);
        return text;
    }
    sprintf(text, "%d%s\n", v, o->suffix);
    return text;
}

/** 1 when this row is a min/max/step row rather than a value list. */
static s32 kindIsRanged(s32 kind)
{
    return kind != KIND_TOGGLE && kind != KIND_ENUM;
}

/**
 * 1 = the row's value is a number on a scale (sensitivity, deadzone, FOV...)
 * rather than a named choice; the option sheets let the analog stick ramp these
 * (docs/port/files-screen-buttons.md, "the analog ramp").
 */
s32 portOptIsRanged(s32 i)
{
    return (i >= 0 && i < PORTOPT_COUNT) ? kindIsRanged(sOpts[i].kind) : 0;
}

/**
 * Hold ini writes (1) and release them (0, writing once), so an analog-stick
 * ramp saves once on release instead of rewriting midas.ini up to twenty times a
 * second. Anything that can end a ramp must release the hold.
 */
/* Quitting (close box, Alt+F4, Exit Game) calls exit(0) mid-hold, so any held
 * value is written here at exit instead. */
void portOptSaveHold(s32 on);

static void portOptSaveFlushAtExit(void)
{
    portOptSaveHold(0);
}

void portOptSaveHold(s32 on)
{
    if (on)
    {
        static s32 armed;

        if (!armed)
        {
            armed = 1;
            atexit(portOptSaveFlushAtExit);
        }
        sSaveHold = 1;
        return;
    }
    if (sSaveHold)
    {
        sSaveHold = 0;
        portOptSave();
    }
}

s32 portOptValueCount(s32 i)
{
    if (i < 0 || i >= PORTOPT_COUNT)
    {
        return 0;
    }
    switch (sOpts[i].kind)
    {
    case KIND_TOGGLE: return 2;
    case KIND_ENUM:   return sOpts[i].min; /* ENUM keeps its count in min */
    default:          return rangeCount(&sOpts[i]);
    }
}

const char *portOptValueTextAt(s32 i, s32 idx)
{
    if (i < 0 || i >= PORTOPT_COUNT || idx < 0)
    {
        return "?\n";
    }
    if (sOpts[i].kind == KIND_TOGGLE)
    {
        if (sOpts[i].valueText != NULL)
        {
            return sOpts[i].valueText[idx ? 1 : 0];
        }
        return idx ? "ON\n" : "OFF\n";
    }
    if (sOpts[i].kind == KIND_ENUM && idx < sOpts[i].min)
    {
        return sOpts[i].valueText[idx];
    }
    if (kindIsRanged(sOpts[i].kind) && idx < rangeCount(&sOpts[i]))
    {
        /* the SAME formatter portOptValueText uses for the live value, so a
         * list entry and the row itself can never disagree about what 90
         * looks like - and KIND_TENTHS still reads "1.5", not "15". */
        return rangeText(&sOpts[i], rangeValueAt(&sOpts[i], idx));
    }
    return "?\n";
}

/** Index of the live value in the list; -1 = the ini holds something off-list. */
s32 portOptValueIndex(s32 i)
{
    s32 v;

    if (i < 0 || i >= PORTOPT_COUNT || !optGet(sOpts[i].key, &v))
    {
        return -1;
    }
    if (sOpts[i].kind == KIND_TOGGLE)
    {
        return v ? 1 : 0;
    }
    if (sOpts[i].kind == KIND_ENUM)
    {
        return enumIndex(&sOpts[i], v);
    }
    if (kindIsRanged(sOpts[i].kind))
    {
        const struct PortOpt *o = &sOpts[i];
        s32 idx;

        if (o->kind == KIND_PCT)
        {
            v = (v * 100 + o->base / 2) / o->base;
        }
        if (o->step <= 0)
        {
            return -1;
        }
        /* Exact grid members only: an off-grid value returns -1 so the row shows its real value.
         * v < min is tested first because C division truncates toward zero. */
        if (v < o->min || (v - o->min) % o->step != 0)
        {
            return -1;
        }
        idx = (v - o->min) / o->step;
        if (idx >= rangeCount(o))
        {
            return -1;
        }
        return idx;
    }
    return -1;
}

/**
 * Starting list index for a value that may be off the grid (portOptValueIndex
 * returns -1 for those).
 *   dir 0  = the nearest entry, clamped into the list (where a drop-down
 *            opens its highlight).
 *   dir >0 = the entry at or below the value, so +1 lands on the next one up.
 *   dir <0 = the entry at or above it, so -1 lands on the next one down.
 * The last two can return -1 or the count for a value outside min..max; the
 * caller clamps or wraps. An on-grid value returns its own index for any dir.
 */
s32 portOptValueIndexNear(s32 i, s32 dir)
{
    const struct PortOpt *o;
    s32 v, n, idx, off;

    idx = portOptValueIndex(i);
    if (idx < 0 && i >= 0 && i < PORTOPT_COUNT && sOpts[i].kind == KIND_ENUM && optGet(sOpts[i].key, &v))
    {
        return enumIndexNear(&sOpts[i], v, dir);   /* e.g. 165 opens on 180, steps to 144 / 180 */
    }
    if (idx >= 0 || i < 0 || i >= PORTOPT_COUNT || !kindIsRanged(sOpts[i].kind)
        || !optGet(sOpts[i].key, &v))
    {
        return idx;
    }
    o = &sOpts[i];
    n = rangeCount(o);
    if (n <= 0)
    {
        return -1;
    }
    if (o->kind == KIND_PCT)
    {
        v = (v * 100 + o->base / 2) / o->base;
    }
    if (v < o->min)
    {
        return (dir > 0) ? -1 : 0;
    }
    idx = (v - o->min) / o->step;   /* the entry at or below: v >= min, no truncation trap */
    off = (v - o->min) % o->step;   /* non-zero here: an exact value returned above */
    if (idx >= n)
    {
        return (dir < 0) ? n : n - 1;
    }
    if (dir < 0 || (dir == 0 && off * 2 >= o->step))
    {
        idx++;                      /* the entry above; dir 0 rounds like the old nearest */
    }
    return (dir == 0 && idx > n - 1) ? n - 1 : idx;
}

/** Where raw value v sits in the row's list; -1 = not on it. For the
 * deferred video sheet's presets (Classic Mode picks other rows' values). */
s32 portOptIndexOfValue(s32 i, s32 v)
{
    if (i < 0 || i >= PORTOPT_COUNT)
    {
        return -1;
    }
    if (sOpts[i].kind == KIND_TOGGLE)
    {
        return v ? 1 : 0;
    }
    if (sOpts[i].kind == KIND_ENUM)
    {
        return enumIndex(&sOpts[i], v);
    }
    if (kindIsRanged(sOpts[i].kind))
    {
        const struct PortOpt *o = &sOpts[i];
        s32 idx;

        if (o->kind == KIND_PCT)
        {
            v = (v * 100 + o->base / 2) / o->base;
        }
        if (o->step <= 0)
        {
            return -1;
        }
        idx = (v - o->min + o->step / 2) / o->step;
        return (idx >= 0 && idx < rangeCount(o)) ? idx : -1;
    }
    return -1;
}

/** Pick a list value outright (a click on a value column). 0 = refused. */
s32 portOptSetIndex(s32 i, s32 idx)
{
    s32 v;

    if (i < 0 || i >= PORTOPT_COUNT || idx < 0 || idx >= portOptValueCount(i))
    {
        return 0;
    }
    if (kindIsRanged(sOpts[i].kind))
    {
        v = rangeValueAt(&sOpts[i], idx);
        if (sOpts[i].kind == KIND_PCT)
        {
            v = v * sOpts[i].base / 100;   /* back to the owning side's units */
        }
    }
    else
    {
        v = (sOpts[i].kind == KIND_TOGGLE) ? (idx != 0) : sOpts[i].values[idx];
    }
    if (!optSet(sOpts[i].key, v))
    {
        return 0;
    }
    portOptSave();
    return 1;
}

/** Set by raw value - the preset applier; no list membership required. */
s32 portOptSetValue(s32 i, s32 v)
{
    if (i < 0 || i >= PORTOPT_COUNT || !optSet(sOpts[i].key, v))
    {
        return 0;
    }
    portOptSave();
    return 1;
}

/* ---- reset all ----------------------------------------------------------
 *
 * Every row's code default, captured by video.c just before midas.ini is
 * read (portOptCaptureDefaults), so Reset All needs no second copy of the
 * defaults to keep in step. Binds are not in this table and are never reset. */
static s32 sOptDefault[PORTOPT_COUNT];
static u8 sOptDefaultHave[PORTOPT_COUNT];

void portOptCaptureDefaults(void)
{
    s32 i;

    for (i = 0; i < PORTOPT_COUNT; i++)
    {
        sOptDefaultHave[i] = (u8) optGet(sOpts[i].key, &sOptDefault[i]);
    }
}

/** Puts every row back to its default, live, in one ini write. */
void portOptResetDefaults(void)
{
    const s32 classic = portOptFind("n64_aa");
    s32 i;

    /* Classic Mode first and raw (not portClassicSetLive), so its remembered
     * values can't come back over the defaults set below. */
    if (classic >= 0 && sOptDefaultHave[classic])
    {
        optSet(sOpts[classic].key, sOptDefault[classic]);
    }
    for (i = 0; i < PORTOPT_COUNT; i++)
    {
        if (i != classic && sOptDefaultHave[i])
        {
            optSet(sOpts[i].key, sOptDefault[i]);
        }
    }
    portOptSave();
}

/** Set without saving - for a preset that writes many rows in one go. */
s32 portOptSetValueQuiet(s32 i, s32 v)
{
    if (i < 0 || i >= PORTOPT_COUNT)
    {
        return 0;
    }
    return optSet(sOpts[i].key, v);
}

/* ---- classic mode --------------------------------------------------------
 *
 * The settings classic mode forces and their values, in one table shared by
 * both menus so they agree: the VIDEO sheet (front.c) applies them on APPLY
 * CHANGES, and the watch (port_watchpages.c) sets them live through
 * portClassicSetLive.
 *
 * The order matches the ini's classic_saved field order (video.c) and is
 * append-only. */
static const char *const sClassicKeys[] = {
    "low_res", "filtering", "anisotropy", "maxfps", "msaa", "mipmaps", "filter_2d",
};
static const s32 sClassicVals[] = {
    240,    /* 3d resolution: hardware */
    2,      /* texture filter: 3-point */
    8,      /* anisotropy 8x */
    -1,     /* max fps: hardware pacing */
    2,      /* msaa 2x */
    1,      /* mipmaps on */
    1,      /* 2d filtering on */
};
#define PORTCLASSIC_COUNT ((s32) (sizeof(sClassicKeys) / sizeof(sClassicKeys[0])))

s32 portClassicCount(void)
{
    return PORTCLASSIC_COUNT;
}

const char *portClassicKey(s32 i)
{
    return (i >= 0 && i < PORTCLASSIC_COUNT) ? sClassicKeys[i] : "";
}

s32 portClassicVal(s32 i)
{
    return (i >= 0 && i < PORTCLASSIC_COUNT) ? sClassicVals[i] : 0;
}

/* The CLASSIC sheet's rows: classic_<part> on = Classic Mode forces
 * sClassicKeys[part]. video.c keeps them; same order as sClassicKeys. */
static const char *const sClassicPartKeys[] = {
    "classic_res", "classic_filter", "classic_aniso", "classic_pacing", "classic_msaa",
    "classic_mipmaps", "classic_filter2d",
};

/** 1 = Classic Mode forces setting `i` (its CLASSIC sheet row is on). */
s32 portClassicPartOn(s32 i)
{
    extern s32 portClassicPart(s32 i);   /* video.c; weak 1 headless */

    return i >= 0 && i < PORTCLASSIC_COUNT && portClassicPart(i);
}

/** 1 = `key` is one of the rows classic mode forces (and the menus lock while it is on). */
s32 portClassicOwns(const char *key)
{
    s32 i;

    for (i = 0; key != NULL && i < PORTCLASSIC_COUNT; i++)
    {
        if (strcmp(key, sClassicKeys[i]) == 0)
        {
            return portClassicPartOn(i);
        }
    }
    return 0;
}

/**
 * A CLASSIC sheet row changed while Classic Mode is on: a part turned on is
 * remembered (classic_saved) and forced, a part turned off gets its remembered
 * value back. With Classic Mode off there is nothing to do; the part only
 * matters the next time it turns on.
 */
static void classicPartChanged(const char *key, s32 on)
{
    extern s32  portVideoClassicSavedGet(s32 *v) __attribute__((weak));
    extern void portVideoClassicSavedSet(const s32 *v) __attribute__((weak));
    s32 v[PORTCLASSIC_COUNT];
    s32 n = 0;
    s32 p, i;
    s32 aa;

    for (p = 0; p < PORTCLASSIC_COUNT; p++)
    {
        if (strcmp(key, sClassicPartKeys[p]) == 0)
        {
            break;
        }
    }
    aa = portOptFind("n64_aa");
    if (p >= PORTCLASSIC_COUNT || aa < 0 || portOptGet(aa) == 0
        || portVideoClassicSavedGet == NULL || portVideoClassicSavedSet == NULL)
    {
        return;
    }
    n = portVideoClassicSavedGet(v);
    if (on)
    {
        for (i = n; i < PORTCLASSIC_COUNT; i++)
        {   /* an older 4-field snapshot: the rest are what they are now */
            v[i] = portOptGet(portOptFind(sClassicKeys[i]));
        }
        v[p] = portOptGet(portOptFind(sClassicKeys[p]));
        portVideoClassicSavedSet(v);
        optSet(sClassicKeys[p], sClassicVals[p]);
    }
    else if (p < n)
    {
        optSet(sClassicKeys[p], v[p]);
    }
}

/**
 * The watch's classic mode row changed: turning on saves the current values
 * (classic_saved) and forces the classic ones, turning off restores whatever
 * was saved (4 or 7 fields depending on ini age); all in one ini write.
 */
void portClassicSetLive(s32 on)
{
    /* video.c; absent headless, where there is no ini to remember them in */
    extern s32  portVideoClassicSavedGet(s32 *v) __attribute__((weak));
    extern void portVideoClassicSavedSet(const s32 *v) __attribute__((weak));
    s32 v[PORTCLASSIC_COUNT];
    s32 n = 0;
    s32 i;

    if (on)
    {
        for (i = 0; i < PORTCLASSIC_COUNT; i++)
        {
            v[i] = portOptGet(portOptFind(sClassicKeys[i]));
        }
        if (portVideoClassicSavedSet != NULL)
        {
            portVideoClassicSavedSet(v);
        }
        for (i = 0; i < PORTCLASSIC_COUNT; i++)
        {
            if (portClassicPartOn(i))   /* a part switched off on the CLASSIC sheet stays the player's */
            {
                portOptSetValueQuiet(portOptFind(sClassicKeys[i]), sClassicVals[i]);
            }
        }
    }
    else
    {
        if (portVideoClassicSavedGet != NULL)
        {
            n = portVideoClassicSavedGet(v);
        }
        for (i = 0; i < n && i < PORTCLASSIC_COUNT; i++)
        {
            if (portClassicPartOn(i))   /* only what Classic Mode forced goes back */
            {
                portOptSetValueQuiet(portOptFind(sClassicKeys[i]), v[i]);
            }
        }
    }
    portOptSave();
}

/**
 * The classic_toggle key (default Tab): flips classic mode in play exactly as
 * the watch's Classic Mode row does, with no on-screen message.
 */
void portClassicToggle(void)
{
    const s32 opt = portOptFind("n64_aa");
    s32 now;

    if (opt < 0)
    {
        return;
    }
    now = portOptGet(opt) == 0;
    if (portOptSetValue(opt, now))
    {
        portClassicSetLive(now);
    }
}
