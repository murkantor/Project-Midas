/**
 * port_watchpages.c - the watch's three settings pages (see
 * docs/port/menu-overhaul-spec.md sections 2, 3 and 12.2).
 *
 * Data and lookups only: src/game/options.c draws these rows and feeds them
 * input, while this file decides what the rows are, where each row's value
 * lives, and what the two presets mean. There is no display list and no SDL
 * here, so it builds in every configuration and the option bridges weak-stub
 * away in headless builds.
 *
 * The tables live here rather than in options.c to keep port data out of that
 * large decompiled Rare file; options.c only gains a drawer and an input
 * handler.
 */
#ifndef TARGET_N64

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ultra64.h>
#include <bondconstants.h>   /* PLAYER_OPTION_* - the real enum, not a copy */

#include "port_watchpages.h"

/* port_options.c */
extern s32         portOptFind(const char *key);
extern const char *portOptLabel(s32 i);
extern const char *portOptValueText(s32 i);
extern s32         portOptValueCount(s32 i);
extern const char *portOptValueTextAt(s32 i, s32 idx);
extern s32         portOptValueIndex(s32 i);
extern s32         portOptValueIndexNear(s32 i, s32 dir);
extern s32         portOptSetIndex(s32 i, s32 idx);
extern s32         portOptSetValue(s32 i, s32 v);
extern s32         portOptSetValueQuiet(s32 i, s32 v);
extern s32         portOptGet(s32 i);
extern s32         portOptAvailable(s32 i);
extern void        portOptStep(s32 i, s32 dir);
extern void        portOptSave(void);

/* ---- row tables ---------------------------------------------------------- */

struct PortWatchRowDef {
    s32 kind;
    const char *key;    /* PWROW_OPT: the midas.ini key */
    const char *label;  /* NULL = take it from the option table / the vanilla string */
    s32 arg;            /* PWROW_PLAYEROPT: PLAYER_OPTION_*; PWROW_SUBSCREEN: page */
    s32 invert;         /* PWROW_OPT toggle shown the other way round */
    s32 defer;          /* PWROW_OPT applied on Accept, not on the step */
};

/*
 * These tables mirror front.c's sheets row for row and in the same order
 * (port_sheet_rows_general / _hud / _portopts / _video / _audio / _graphics),
 * so a setting is in the same place in both menus. When a row moves there, move
 * it here too.
 *
 * Differences, because the watch is a different machine:
 *  - Rare's six options (look up/down, auto-aim, aim control, look ahead, sight and ammo
 *    on-screen) stay PWROW_PLAYEROPT. options.c game_option_select_value copies a watch change
 *    into the ini, but in a mission the live value is Rare's game_options_entries[].
 *  - "hud centering" (PLAYER_OPTION_RATIO) has no folder row, so it sits at the end of HUD.
 *  - There is no APPLY CHANGES row. Display mode and resolution defer to the A press; the rest
 *    of VIDEO is live, and classic mode forces its rows when switched on - applied by the
 *    option table's choke point (port_options.c optSet), not by this page.
 *
 * The old "preset" row (Nintendo 64 / Modern) was removed because it fought
 * classic mode, which forces some of the same rows to other values. The
 * PWROW_PRESET code below is left in place.
 */
static const struct PortWatchRowDef sGeneral[] = {
    /* The watch face rows under the two volume bars, kept where Rare put
     * them. Seven rows plus two pickers fill exactly the nine slots
     * (PORTWATCH_GENERAL_ROWS); a tenth row would make the face scroll. */
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_LOOK, 0 },
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_AUTOAIM, 0 },
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_AIM, 0 },
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_SIGHT, 0 },
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_LOOKAHEAD, 0 },
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_AMMODISPLAY, 0 },
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_SCREEN, 0 },
    { PWROW_SUBSCREEN, NULL, "Gameplay\n", PORTWATCH_GAMEPLAY, 0 },
    /* CONTROL is the folder's middle row; on the watch it is its own tab */
    { PWROW_SUBSCREEN, NULL, "Audio Visual\n", PORTWATCH_DISPLAY, 0 },   /* the folder's name */
};

/* the two pickers */
static const struct PortWatchRowDef sGameplay[] = {
    /* "general settings": Rare's four rows are on the face; the port's own are this page */
    { PWROW_SUBSCREEN, NULL, "General Settings\n", PORTWATCH_GENSETTINGS, 0 },
    { PWROW_SUBSCREEN, NULL, "HUD\n",              PORTWATCH_HUD,         0 },
    { PWROW_SUBSCREEN, NULL, "Port Options\n",     PORTWATCH_PORTOPTS,    0 },
    { PWROW_BACK,      NULL, "Back\n", 0, 0 },
};
static const struct PortWatchRowDef sDisplay[] = {
    { PWROW_SUBSCREEN, NULL, "Video\n",    PORTWATCH_VIDEO,    0 },
    { PWROW_SUBSCREEN, NULL, "Audio\n",    PORTWATCH_AUDIO,    0 },
    /* Graphics: now empty (FOV moved to Video, High Detail Models to Port Options) */
    { PWROW_BACK,      NULL, "Back\n", 0, 0 },
};

/* GAMEPLAY's pages */
static const struct PortWatchRowDef sHud[] = {
    /* sight on-screen and ammo on-screen lead and follow this row in the
     * folder; on the watch they are on the face, where Rare had them */
    { PWROW_OPT,       "always_crosshair", NULL, 0, 0 },
    /* Crosshair colour, where the folder has it. It is a STRING key, so it has its own kind: the
     * folder's named list shared through front.c portXhair*, shown under Always Crosshair. */
    { PWROW_XHAIR,     NULL, "Crosshair Colour\n", 0, 0 },
    { PWROW_OPT,       "mission_timer",    NULL, 0, 0 },
    { PWROW_OPT,       "pause_pillars",    NULL, 0, 0 },
    /* aim lock, stamina sway and steady scope moved to General Settings */
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_RATIO, 0 }, /* lodger: "hud" (romdata.c relabel) */
    { PWROW_OPT,       "health_wide",      NULL, 0, 0 },       /* its split-off twin: the health / armour arcs */
    { PWROW_OPT,       "hud_scale",        NULL, 0, 0 },       /* the HUD on a 640x480 canvas */
    { PWROW_OPT,       "show_fps",         NULL, 0, 0 },       /* the folder's HUD row too */
    { PWROW_OPT,       "armor_hud",        NULL, 0, 0 },       /* the arcs on a body armour pickup */
    { PWROW_BACK,      NULL, "Back\n", 0, 0 },
};
/* GENERAL SETTINGS: the folder's General Settings less Rare's four vanilla options, which are
 * already on the face */
static const struct PortWatchRowDef sGenSettings[] = {
    { PWROW_OPT,       "aim_lock",         NULL, 0, 0 },
    { PWROW_OPT,       "scope_steady",     NULL, 0, 0 },
    { PWROW_OPT,       "head_roll",        NULL, 0, 0 },
    { PWROW_BACK,      NULL, "Back\n", 0, 0 },
};
static const struct PortWatchRowDef sPortOpts[] = {
    { PWROW_OPT,       "unlock_all",         NULL, 0, 0 },   /* first */
    { PWROW_OPT,       "fix_shootthrough",   NULL, 0, 0 },
    { PWROW_OPT,       "distance_fading",    NULL, 0, 0 },
    { PWROW_OPT,       "true_enemy_rockets", NULL, 0, 0 },
    { PWROW_OPT,       "unlimited_explosions", NULL, 0, 0 }, /* was folder-only */
    { PWROW_OPT,       "watch_static",       NULL, 0, 0 },   /* watch static */
    { PWROW_OPT,       "fast_pause",         NULL, 0, 0 },   /* the watch at Rare's top speed */
    { PWROW_OPT,       "mouse_auto_aim",     NULL, 0, 0 },   /* the folder's row too */
    { PWROW_OPT,       "high_lod", "High Detail Models\n", 0, 0 },   /* from Graphics */
    { PWROW_OPT,       "setting_help",       NULL, 0, 0 },   /* the folder's help box; never drawn on the watch */
    { PWROW_BACK,      NULL, "Back\n", 0, 0 },
};

/* DISPLAY's pages */
static const struct PortWatchRowDef sVideo[] = {
    /* Deferred until A is pressed, like the resolution row below: stepping through `exclusive`
     * does a real mode switch per notch and could strand a player on a display
     * that cannot take it. `defer` is the last field. */
    { PWROW_OPT,        "fullscreen", NULL, 0, 0, 1 },  /* display mode 3-way */
    { PWROW_RESOLUTION, NULL,         "Resolution\n", 0, 0 },   /* the window / fullscreen size */
    { PWROW_OPT,        "vsync",      NULL, 0, 0 },
    { PWROW_OPT,        "widescreen", NULL, 0, 0 },
    /* CLASSIC MODE SITS OVER WHAT IT FORCES: every row below it is one of
     * port_options.c's classic keys, locked while it is on (rowClassicLocked).
     * A row classic mode does not own goes ABOVE it - same rule as the folder,
     * and the reason N64 AA is up here: the preset does not own the bit (its
     * classic_n64aa part decides whether it touches it), so the row stays live
     * and a player can keep N64 edges with the preset's other parts on, or drop
     * the edges and keep 240p. */
    { PWROW_OPT,        "n64_aa",     "N64 AA\n", 0, 0 },
    { PWROW_OPT,        "classic_mode", "Classic Mode\n", 0, 0 },
    { PWROW_OPT,        "hfov_wide",  NULL, 0, 0 },  /* above 3d resolution; not a classic key */
    { PWROW_OPT,        "weapon_fov", NULL, 0, 0 },  /* beside field of view; not a classic key */
    { PWROW_OPT,        "low_res",    NULL, 0, 0 },  /* 3d resolution: the level render */
    { PWROW_OPT,        "filtering",  NULL, 0, 0 },
    { PWROW_OPT,        "anisotropy", NULL, 0, 0 },
    { PWROW_OPT,        "msaa",       NULL, 0, 0 },
    { PWROW_OPT,        "filter_2d",  NULL, 0, 0 },
    { PWROW_OPT,        "maxfps",     NULL, 0, 0 },
    { PWROW_BACK,       NULL, "Back\n", 0, 0 },
};
static const struct PortWatchRowDef sAudio[] = {
    /* the same two values as the bars on the face, as rows - the folder's
     * AUDIO page has them, and both read the live volume */
    { PWROW_OPT,       "music_volume", "Music\n", 0, 0 },
    { PWROW_OPT,       "sfx_volume",   "FX\n",    0, 0 },
    { PWROW_OPT,       "stereo_pan",   NULL, 0, 0 },
    { PWROW_OPT,       "mute_unfocused", NULL, 0, 0 },
    { PWROW_BACK,      NULL, "Back\n", 0, 0 },
};
static const struct PortWatchRowDef sGraphics[] = {
    /* empty and off Audio Visual's list, but kept because PORTWATCH_GRAPHICS
     * indexes the page table */
    { PWROW_BACK,      NULL, "Back\n", 0, 0 },
};

/* ---- the Controller pages ------------------------------------------------
 *
 * These two are built on demand because their rows depend on the selected
 * device: a raphnet adapter cannot be remapped at all, and the mouse rows only
 * make sense for mouse and keyboard.
 *
 * The action list is the spec's (section 4): the N64 binds plus the Port suite,
 * named for what they do. Each row carries BOTH ini keys, because the keyboard
 * stores binds in video.c's [controls] section and a pad stores them in
 * input_pc.c's per-slot padN_<control> map.
 *
 * Limit: what a C button does depends on the control style (1.2 makes them
 * digital movement, 2.x makes them the look stick). The labels are right for
 * the port's defaults (style 1.2 on the keyboard, real sticks on a pad);
 * following the live style is the section 5 virtual-action work.
 */
struct PortBindDef {
    const char *label;
    const char *kbKey;   /* a video.c bind name (N64 slot or Port action), NULL = none */
    const char *padKey;  /* an input_pc.c sPadMapNames entry, NULL = none */
};

/* PORT BINDINGS - what the player does. This list works on every control style because the
 * movement rows point at the keyboard's analog_* binds rather than its C buttons. */
static const struct PortBindDef sBindsPort[] = {
    /* Sticks first, then buttons. The movement rows point at the keyboard's analog_* binds, not
     * its C buttons; the look rows have their own keys (see below). */
    { "Move Forward\n",      "analog_up",      "move_up"        },
    { "Move Back\n",         "analog_down",    "move_down"      },
    { "Strafe Left\n",       "analog_left",    "move_left"      },
    { "Strafe Right\n",      "analog_right",   "move_right"     },
    { "Turn Left\n",         "look_left",      "look_left"      },
    { "Turn Right\n",        "look_right",     "look_right"     },
    { "Look Up\n",           "look_up",        "look_up"        },
    { "Look Down\n",         "look_down",      "look_down"      },
    { "Fire\n",              "z",              "z"              },
    { "Aim\n",               "r",              "r"              },
    { "Use\n",               "use",            "use"            },
    { "Reload\n",            "reload",         "reload"         },
    { "Next Weapon\n",       "next_weapon",    "next_weapon"    },
    { "Previous Weapon\n",   "prev_weapon",    "prev_weapon"    },
    { "Next Gadget\n",       "next_inventory", "next_inventory" },
    { "Previous Gadget\n",   "prev_inventory", "prev_inventory" },
    { "Detonate\n",          "detonate",       "detonate"       },
    /* No lean row: while aiming, moving left or right leans, which is the N64's own rule */
    { "Zoom In\n",           "zoom_in",        "zoom_in"        },
    { "Zoom Out\n",          "zoom_out",       "zoom_out"       },
    { "Crouch (Hold)\n",     "crouch_hold",    "crouch_hold"    },
    { "Crouch (Toggle)\n",   "crouch_toggle",  "crouch_toggle"  },
    { "Walk\n",              "walk",           "walk"           },
    /* No Run / Walk-Run rows: they only existed to leave or flip Always Walk, which is gone */
    /* The look rows (at the top) have their own keyboard binds, so binding W to "move forward"
     * does not also bind it to "look up". */
    { "Pause\n",             "start",          "start"          },
    { "Watch Page Left\n",   "watch_prev",     "watch_prev"     },
    { "Watch Page Right\n",  "watch_next",     "watch_next"     },
    { "Watch Up\n",          "watch_up",       "watch_up"       },
    { "Watch Down\n",        "watch_down",     "watch_down"     },
    { "Objectives\n",        "watch_objectives", "watch_objectives" },
    /* the menus' own two, so they can be moved without
     * moving use or the gadget buttons, which confirm and back out as well
     * (joy_port.c portJoyPortUseMenuPressed / portJoyPortBackMenuPressed) */
    { "Menu Accept\n",       "menu_accept",    "menu_accept"    },
    { "Menu Back\n",         "menu_back",      "menu_back"      },
    /* No N64 rows here: a, b and l are only on the N64 list, so each list shows just what its own
     * mode is about. The binds themselves are untouched. */
};

/* N64 BINDINGS - the console's own controls under their own names, offered only on N64
 * hardware. Elsewhere what a C button does depends on the control style, so
 * this list would promise something it cannot deliver. */
static const struct PortBindDef sBindsN64[] = {
    /* the stick first, then the buttons */
    { "Stick Up\n",          "analog_up",      "move_up"        },
    { "Stick Down\n",        "analog_down",    "move_down"      },
    { "Stick Left\n",        "analog_left",    "move_left"      },
    { "Stick Right\n",       "analog_right",   "move_right"     },
    { "A Button\n",          "a",              "a"              },
    { "B Button\n",          "b",              "b"              },
    { "Z Trigger\n",         "z",              "z"              },
    { "L Button\n",          "l",              "l"              },
    { "R Button\n",          "r",              "r"              },
    { "Start\n",             "start",          "start"          },
    { "C Up\n",              "c_up",           "c_up"           },
    { "C Down\n",            "c_down",         "c_down"         },
    { "C Left\n",            "c_left",         "c_left"         },
    { "C Right\n",           "c_right",        "c_right"        },
    { "D-Pad Up\n",          "dpad_up",        "dpad_up"        },
    { "D-Pad Down\n",        "dpad_down",      "dpad_down"      },
    { "D-Pad Left\n",        "dpad_left",      "dpad_left"      },
    { "D-Pad Right\n",       "dpad_right",     "dpad_right"     },
};

/* front.c - the crosshair colour list the folder's sheet offers */
extern s32         portXhairCount(void);
extern const char *portXhairName(s32 i);
extern s32         portXhairIndex(void);
extern void        portXhairApply(s32 i);
/* input_pc.c - the device picker's world */
extern s32         portInputDeviceCount(void);
extern const char *portInputDeviceLabel(s32 dev);
extern s32         portInputDeviceIsRaphnet(s32 dev);
extern s32         portInputDeviceBindText(s32 dev, const char *name, char *buf, s32 len);
extern s32         portInputDeviceIniKey(s32 dev, const char *name, char *buf, s32 len);
/* video.c - the keyboard's */
extern s32         portVideoIniGetKey(const char *key, char *buf, s32 n);
/* video.c - the window / fullscreen resolution list */
/* video.c: the mouse/keyboard bind store and the watch's key capture */
extern s32         portVideoIniSetKey(const char *key, const char *value);
extern void        portVideoBindCaptureBegin(void);
extern void        portVideoBindCaptureCancel(void);
extern s32         portVideoBindCaptureTake(char *out, s32 len);

/* rebinding a pad, in game (input_pc.c) */
extern s32         portInputPadCaptureBegin(s32 dev);
extern s32         portInputPadCaptureTick(void);
extern s32         portInputPadCaptureTake(char *out, s32 len);
extern void        portInputPadCaptureCancel(void);
extern s32         portInputPadBindSet(s32 dev, const char *name, const char *token);
extern s32         portVideoResCount(void);
extern s32         portVideoResGet(s32 i, s32 *w, s32 *h);
extern s32         portVideoResCurrent(void);
extern s32         portVideoResLocked(void);
extern s32         portVideoResApply(s32 i);
/* bondview2.c - the 60 Hz menu field gate (portPauseBoxTick) */
extern s32         portWatchMenuField(void);
/* video.c - the post-capture blackout */
extern void        portBindCooldownStart(void);

/* which device the Controller pages are editing (0 = mouse and keyboard) */
static s32 sDevice;

/* Which bind list the page shows per device, and for a pad, how that pad is driven. These are one
 * choice: binding the N64 list on a pad the port drives directly would offer
 * controls that belong to a control style that is not running.
 *
 * The hardware sniff (portInputDeviceIsN64) only picks the starting position;
 * every device can move the row. A pad keeps its answer in padN_inputmode
 * (input_pc.c) so it persists and so bondview2's drive block reads the same
 * bit, while device 0 (mouse and keyboard) has no pad slot and keeps its answer
 * here. */
#define BINDMODE_MAX_DEVICES 8
extern s32  portInputDeviceIsN64(s32 dev);
extern s32  portInputDeviceModeIsN64(s32 dev);
extern void portInputDeviceModeSet(s32 dev, s32 isN64);

/* Device-parameterised because the frontend's Options > CONTROL page shares these tables, this
 * mode and the capture underneath. The watch passes its own sDevice; nothing else reads it. */
s32 portBindModeIsN64(s32 dev)
{
    extern s32 portInputPadIsN64(s32 slot);

    if (dev < 0 || dev >= BINDMODE_MAX_DEVICES)
    {
        return 0;
    }
    if (dev > 0)
    {
        return portInputDeviceModeIsN64(dev);   /* a pad: padN_inputmode */
    }
    /* Mouse and keyboard always use port bindings (raphnet always uses N64 ones). The row is not
     * offered for device 0 because there is nothing to choose. */
    return 0;
}

/** A pad's row moves; device 0 has no row (it follows player 1's slot). */
s32 portBindModeCanN64(s32 dev)
{
    return (dev > 0 && dev < BINDMODE_MAX_DEVICES);
}

void portBindModeToggle(s32 dev)
{
    if (dev <= 0 || dev >= BINDMODE_MAX_DEVICES)
    {
        return;
    }
    portInputDeviceModeSet(dev, !portInputDeviceModeIsN64(dev));
}

static s32 bindModeIsN64(void)
{
    return portBindModeIsN64(sDevice);
}

/** The list a DEVICE is showing, and its length. */
/* Keyboard-and-mouse-only rows (such as the mouse unlock key), added to the end of whichever list
 * device 0 shows. A pad has no pointer to free, so its lists never see them. */
static const struct PortBindDef sBindsKbmOnly[] = {
    { "Mouse Unlock\n",      "mouse_unlock",   NULL             },
};
#define PORTBIND_KBM_EXTRA ((s32) (sizeof(sBindsKbmOnly) / sizeof(sBindsKbmOnly[0])))

/* Every device's rows, on the end of every list (port and N64 alike): the Classic Mode toggle,
 * which is the keyboard key and pad control of one port action (joy_port.c
 * PORTACT_CLASSIC_TOGGLE). */
static const struct PortBindDef sBindsAllExtra[] = {
    { "Classic Mode\n",      "classic_toggle", "classic_toggle" }, /* keyboard default Tab */
};
#define PORTBIND_ALL_EXTRA ((s32) (sizeof(sBindsAllExtra) / sizeof(sBindsAllExtra[0])))

static const struct PortBindDef *bindTableFor(s32 dev, s32 *count)
{
    const s32 n64 = portBindModeIsN64(dev) ? 1 : 0;   /* also an index below */
    const s32 kbm = (dev == 0) ? 1 : 0;               /* ...and so is this */
    const struct PortBindDef *base = n64 ? sBindsN64 : sBindsPort;
    const s32 n = n64 ? (s32) (sizeof(sBindsN64) / sizeof(sBindsN64[0]))
                      : (s32) (sizeof(sBindsPort) / sizeof(sBindsPort[0]));
    const s32 total = n + (kbm ? PORTBIND_KBM_EXTRA : 0) + PORTBIND_ALL_EXTRA;
    /* built once per list: the base rows, the keyboard's own (device 0), then everyone's */
    static struct PortBindDef joined[2][2][64];
    static s32 built[2][2];
    s32 i;
    s32 k = 0;

    if (total > 64)
    {
        *count = n;
        return base;
    }
    if (!built[kbm][n64])
    {
        for (i = 0; i < n; i++)
        {
            joined[kbm][n64][k++] = base[i];
        }
        for (i = 0; kbm && i < PORTBIND_KBM_EXTRA; i++)
        {
            joined[kbm][n64][k++] = sBindsKbmOnly[i];
        }
        for (i = 0; i < PORTBIND_ALL_EXTRA; i++)
        {
            joined[kbm][n64][k++] = sBindsAllExtra[i];
        }
        built[kbm][n64] = 1;
    }
    *count = total;
    return joined[kbm][n64];
}

static const struct PortBindDef *bindTable(s32 *count)
{
    return bindTableFor(sDevice, count);
}

/* ---- the shared bind API: (device, index), for any screen -------------- */

s32 portBindCount(s32 dev)
{
    s32 n = 0;
    (void) bindTableFor(dev, &n);
    return n;
}

const char *portBindLabel(s32 dev, s32 i)
{
    s32 n = 0;
    const struct PortBindDef *t = bindTableFor(dev, &n);

    return (i >= 0 && i < n) ? t[i].label : "\n";
}

/** What that row is bound to on that device, ready to draw. */
const char *portBindValueText(s32 dev, s32 i)
{
    static char text[64];
    s32 n = 0;
    const struct PortBindDef *t = bindTableFor(dev, &n);
    const struct PortBindDef *b;
    size_t len;

    if (i < 0 || i >= n)
    {
        return "N/A\n";
    }
    b = &t[i];
    text[0] = '\0';
    if (dev == 0)
    {
        if (b->kbKey == NULL || !portVideoIniGetKey(b->kbKey, text, (s32) sizeof(text) - 1))
        {
            return "N/A\n";
        }
    }
    else if (b->padKey == NULL || !portInputDeviceBindText(dev, b->padKey, text, (s32) sizeof(text) - 1))
    {
        return "N/A\n";
    }
    if (text[0] == '\0')
    {
        snprintf(text, sizeof(text), "unbound");
    }
    len = strlen(text);   /* the trailing newline is load-bearing (textMeasure) */
    if (len + 2 < sizeof(text))
    {
        text[len] = '\n';
        text[len + 1] = '\0';
    }
    return text;
}

static s32 bindTableCount(void)
{
    s32 n = 0;
    (void) bindTable(&n);
    return n;
}

static const struct PortBindDef *bindTableRows(void)
{
    s32 n = 0;
    return bindTable(&n);
}

/* the rest of the file says sBinds[i] and PORTBIND_COUNT; both now follow
 * the live mode instead of one fixed table */
#define sBinds (bindTableRows())
#define PORTBIND_COUNT (bindTableCount())

/* The Control tab's pages, as the folder's player page has them (front.c port_sheet_rows_mousekb
 * / _pad / _ctlgeneral): the device's own page (mouse and keyboard for device
 * 0, controller for a pad) and general control options. */
static const struct PortWatchRowDef sControllerPad[] = {
    /* twin_stick and right_stick_c removed: the style row and the layout do their jobs now
     * (see port_options.c) */
    /* pad1_swap_sticks removed from the menus as unneeded; ini-only */
    { PWROW_OPT, "deadzone",            NULL, 0, 0 },
    { PWROW_OPT, "deadzone_look",       NULL, 0, 0 },
    { PWROW_OPT, "trigger_threshold",   NULL, 0, 0 },
    { PWROW_OPT, "stick_scale",         NULL, 0, 0 },
    { PWROW_OPT, "rumble",              NULL, 0, 0 },
    { PWROW_OPT, "smart_mouse_lock",    NULL, 0, 0 },   /* every device's page */
};
/* N64 mode only: B then Z exists for a real N64 pad with no spare button; in port mode
 * next_inventory is a bindable action and the Pro layouts have no B, so the row
 * would do nothing. */
static const struct PortWatchRowDef sControllerMoreN64[] = {
    { PWROW_OPT, "pad1_gadget_switch",  "B then Z = Next Gadget\n",  0, 0 },
    { PWROW_OPT, "pad1_lr_classic",     "Hold L+R = Classic Mode\n", 0, 0 }, /* classic mode shortcut */
};

static const struct PortWatchRowDef sControllerMoreMouse[] = {
    { PWROW_OPT, "mouse",            NULL, 0, 0 },
    /* mouse_mode retired: direct is the mouse, and the smoothed rate path was softer and laggier */
    { PWROW_OPT, "smart_mouse_lock", NULL, 0, 0 },
    { PWROW_OPT, "mouse_sens_x",     NULL, 0, 0 },
    { PWROW_OPT, "mouse_sens_y",     NULL, 0, 0 },
    { PWROW_OPT, "mouse_sens_aim",   NULL, 0, 0 },
    { PWROW_OPT, "p1_hybrid",        NULL, 0, 0 },
    /* aim inversion: the face's own Look Up/Down row, so both edit one value */
    { PWROW_PLAYEROPT, NULL, NULL, PLAYER_OPTION_LOOK, 0 },
};

static const struct PortWatchRowDef sCtlGeneral[] = {
    { PWROW_OPT,  "detonate_use_reload", NULL, 0, 0 },
    { PWROW_OPT,  "shoot_next_prev",     NULL, 0, 0 },
    { PWROW_OPT,  "no_aim_crouch",       NULL, 0, 0 },
    { PWROW_OPT,  "background_input",    NULL, 0, 0 },
    { PWROW_BACK, NULL, "Back\n", 0, 0 },
};

/* Rebuilt whenever the page, the selected device, or what that index RESOLVES
 * to changes. 64 is comfortably above the longest page (1 + 29 + 1 = 31). */
#define PORTWATCH_DYN_MAX 64
static struct PortWatchRowDef sDyn[PORTWATCH_DYN_MAX];
static s32 sDynCount;
static s32 sDynPage = -1;
/* the watch's Reset to Default row asks twice: the first press arms it ("Confirm Reset") */
static s32 sResetArmed;
static s32 sDynDevice = -1;
/* WHAT THAT INDEX RESOLVED TO when the list was built (bit 0 raphnet, bit 1
 * N64 bind mode). An assign or a hotplug can put a different device behind
 * the same sDevice - portWatchDeviceSet skips an unchanged index - so the
 * key has to hold what the lists below actually read */
static s32 sDynShape = -1;

static void dynPush(s32 kind, const char *key, const char *label, s32 arg)
{
    if (sDynCount >= PORTWATCH_DYN_MAX)
    {
        return;
    }
    sDyn[sDynCount].kind = kind;
    sDyn[sDynCount].key = key;
    sDyn[sDynCount].label = label;
    sDyn[sDynCount].arg = arg;
    sDyn[sDynCount].invert = 0;
    sDyn[sDynCount].defer = 0;
    sDynCount++;
}

static void dynBuild(s32 page)
{
    s32 i;
    const s32 shape = (portInputDeviceIsRaphnet(sDevice) ? 1 : 0) | (bindModeIsN64() ? 2 : 0); /* derived, never trusted */

    if (sDynPage == page && sDynDevice == sDevice && sDynShape == shape)
    {
        return;
    }
    sDynPage = page;
    sDynDevice = sDevice;
    sDynShape = shape;
    sDynCount = 0;

    if (page == PORTWATCH_CONTROLLER)
    {
        /* Compact like the folder's player page: the thirty-row bind list is its own page
         * (PORTWATCH_BINDS) so it does not push the other pages off the face.
         * The row says "input", not "controller", because a pad's own page is
         * called "controller". */
        dynPush(PWROW_DEVICE, NULL, "Input\n", 0);
        /* The device name goes on its own line below "input": a hint with no label reads the device
         * (rowLabelShared), and options.c cuts any hint wider than the face */
        dynPush(PWROW_HINT, NULL, NULL, 0);
        if (portInputDeviceIsRaphnet(sDevice))
        {
            /* Raphnet adapters keep the classic control styles and cannot be
             * remapped (section 4 item 6). options.c shows the vanilla page
             * for this device, so this is only what a caller that asks for
             * the port page anyway would see. */
            dynPush(PWROW_HINT, NULL, "Raphnet: Use the Control Styles\n", 0);
        }
        else
        {
            dynPush(PWROW_SUBSCREEN, NULL, "Rebind Inputs\n", PORTWATCH_BINDS);
        }
        /* the device's own page, named for the device, then the page every
         * device shares - the folder's player page, in its order */
        dynPush(PWROW_SUBSCREEN, NULL, (sDevice == 0) ? "Mouse and Keyboard\n" : "Controller\n",
                PORTWATCH_CONTROLLER_MORE);
        dynPush(PWROW_SUBSCREEN, NULL, "General Control Options\n", PORTWATCH_CTLGENERAL);
        return;
    }

    if (page == PORTWATCH_BINDS)
    {
        /* WHICH LIST, first, because it changes every row under it. Not
         * for device 0: its mode is player 1's slot's (portBindModeIsN64) */
        /* RESET TO DEFAULT is the first bind option, pushing everything else down */
        sResetArmed = 0;
        dynPush(PWROW_ACTION, NULL, NULL, 0);
        /* how to unbind: Delete, or any button held two seconds */
        dynPush(PWROW_HINT, NULL, "Del or Hold to Unbind\n", 0);
        if (portBindModeCanN64(sDevice))
        {
            dynPush(PWROW_BINDMODE, NULL, "Bindings\n", 0);
        }
        /* bind config: the pad's layout, as on the folder's rebind page.
         * manage config (save / delete, and the on-screen keyboard that
         * names one) is NOT BUILT on the watch yet */
        if (sDevice != 0 && !portInputDeviceIsRaphnet(sDevice))
        {
            dynPush(PWROW_LAYOUT, NULL, "Bind Config\n", 0);
        }
        for (i = 0; i < PORTBIND_COUNT; i++)
        {
            dynPush(PWROW_BIND, NULL, sBinds[i].label, i);
        }
        dynPush(PWROW_BACK, NULL, "Back\n", 0);
        return;
    }

    if (page == PORTWATCH_CTLGENERAL)
    {
        /* General control options, plus the gadget switch for a raphnet. B then Z normally lives on
         * the Controller page's N64 rows, which a raphnet does not have, so the
         * row would otherwise be unreachable. */
        for (i = 0; i < (s32)(sizeof(sCtlGeneral) / sizeof(sCtlGeneral[0])); i++)
        {
            if (sCtlGeneral[i].kind != PWROW_BACK)
            {
                sDyn[sDynCount] = sCtlGeneral[i];
                sDynCount++;
            }
        }
        if (portInputDeviceIsRaphnet(sDevice))
        {
            for (i = 0; i < (s32)(sizeof(sControllerMoreN64) / sizeof(sControllerMoreN64[0])); i++)
            {
                sDyn[sDynCount] = sControllerMoreN64[i];
                sDynCount++;
            }
        }
        dynPush(PWROW_BACK, NULL, "Back\n", 0);
        return;
    }

    /* PORTWATCH_CONTROLLER_MORE: mouse and keyboard, or controller */
    if (sDevice == 0)
    {
        for (i = 0; i < (s32)(sizeof(sControllerMoreMouse) / sizeof(sControllerMoreMouse[0])); i++)
        {
            sDyn[sDynCount] = sControllerMoreMouse[i];
            sDynCount++;
        }
    }
    else
    {
        for (i = 0; i < (s32)(sizeof(sControllerPad) / sizeof(sControllerPad[0])); i++)
        {
            sDyn[sDynCount] = sControllerPad[i];
            sDynCount++;
        }
        if (bindModeIsN64())
        {
            for (i = 0; i < (s32)(sizeof(sControllerMoreN64) / sizeof(sControllerMoreN64[0])); i++)
            {
                sDyn[sDynCount] = sControllerMoreN64[i];
                sDynCount++;
            }
        }
    }
    dynPush(PWROW_BACK, NULL, "Back\n", 0);
}

static s32 pageIsDynamic(s32 page)
{
    return page == PORTWATCH_CONTROLLER || page == PORTWATCH_CONTROLLER_MORE
        || page == PORTWATCH_BINDS || page == PORTWATCH_CTLGENERAL;
}

static const struct PortWatchPageDef {
    const struct PortWatchRowDef *rows;
    s32 count;
} sPages[PORTWATCH_PAGES] = {
#define PWPAGE(t) { t, (s32)(sizeof(t) / sizeof(t[0])) }
    [PORTWATCH_GENERAL]     = PWPAGE(sGeneral),
    [PORTWATCH_DISPLAY]     = PWPAGE(sDisplay),
    [PORTWATCH_GAMEPLAY]    = PWPAGE(sGameplay),
    [PORTWATCH_GENSETTINGS] = PWPAGE(sGenSettings),
    [PORTWATCH_HUD]         = PWPAGE(sHud),
    [PORTWATCH_PORTOPTS]    = PWPAGE(sPortOpts),
    [PORTWATCH_VIDEO]       = PWPAGE(sVideo),
    [PORTWATCH_AUDIO]       = PWPAGE(sAudio),
    [PORTWATCH_GRAPHICS]    = PWPAGE(sGraphics),
    /* PORTWATCH_CONTROLLER, _MORE and _BINDS are dynamic (dynBuild): { NULL, 0 } */
    /* PORTWATCH_CTLGENERAL too (the raphnet's gadget switch) */
#undef PWPAGE
};

static const struct PortWatchRowDef *rowDef(s32 page, s32 row)
{
    if (page < 0 || page >= PORTWATCH_PAGES || row < 0)
    {
        return NULL;
    }
    if (pageIsDynamic(page))
    {
        dynBuild(page);
        return (row < sDynCount) ? &sDyn[row] : NULL;
    }
    if (row >= sPages[page].count)
    {
        return NULL;
    }
    return &sPages[page].rows[row];
}

/**
 * The page's TRUE length. options.c scrolls a PORTWATCH_VISROWS window over
 * it, so this is deliberately not clamped - a Controller page is thirty-odd
 * rows long.
 */
s32 portWatchPageRows(s32 page)
{
    if (page < 0 || page >= PORTWATCH_PAGES)
    {
        return 0;
    }
    if (pageIsDynamic(page))
    {
        dynBuild(page);
        return sDynCount;
    }
    return sPages[page].count;
}

/* ---- device selection ---------------------------------------------------- */

s32 portWatchDevice(void)
{
    return sDevice;
}

/**
 * Step the device picker. Clamped to what is plugged in RIGHT NOW: a pad
 * unplugged while this page is open must not leave the menu editing a slot
 * that no longer has a device in it.
 */
void portWatchDeviceStep(s32 dir)
{
    const s32 n = portInputDeviceCount();

    if (dir == 0 || n <= 0)
    {
        return;
    }
    sDevice += (dir > 0) ? 1 : -1;
    if (sDevice >= n)
    {
        sDevice = 0;
    }
    if (sDevice < 0)
    {
        sDevice = n - 1;
    }
    sDynPage = -1; /* the row list depends on the device */
}

void portWatchDeviceSet(s32 dev)
{
    if (dev >= 0 && dev < portInputDeviceCount() && dev != sDevice)
    {
        sDevice = dev;
        sDynPage = -1; /* the row list depends on the device */
    }
}

void portWatchDeviceValidate(void)
{
    const s32 n = portInputDeviceCount();

    if (sDevice >= n || sDevice < 0)
    {
        sDevice = 0;
        sDynPage = -1;
    }
}

/** 1 = nothing but mouse and keyboard is plugged in (the spec's hint case). */
s32 portWatchDeviceNoneConnected(void)
{
    return portInputDeviceCount() <= 1;
}

/** Raphnet adapters keep the vanilla page (spec section 4 item 6). options.c
 *  asks through here so it never has to know about input_pc.c's device list. */
s32 portWatchDeviceIsRaphnet(s32 dev)
{
    return portInputDeviceIsRaphnet(dev);
}

/* The watch keeps its all-lowercase look. The option table, bind labels and device names are
 * shared with the folder's sheets, which now use Title Case, but the watch's
 * Gothic font draws a lowercase letter as a small cap and a capital as a full
 * one, so Title Case would mix heights. Shared strings are lowercased on the
 * way out; strings with no lowercase (ON, OFF, N/A) and bind values are left
 * alone. A ring of buffers is used because options.c holds a label and a value
 * at once. */
static const char *pwWatchCase(const char *s)
{
    static char ring[8][64];
    static s32 at;
    char *out;
    s32 i;
    s32 lower = 0;

    if (s == NULL)
    {
        return NULL;
    }
    for (i = 0; s[i] != '\0'; i++)
    {
        if (s[i] >= 'a' && s[i] <= 'z' && s[i] != 'x')
        {
            lower = 1;
        }
    }
    if (!lower)
    {
        return s;
    }
    out = ring[at];
    at = (at + 1) & 7;
    for (i = 0; s[i] != '\0' && i < 63; i++)
    {
        out[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char) (s[i] + ('a' - 'A')) : s[i];
    }
    out[i] = '\0';
    return out;
}

s32 portWatchRowKind(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    return (d != NULL) ? d->kind : PWROW_BACK;
}

s32 portWatchRowPlayerOption(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    return (d != NULL && d->kind == PWROW_PLAYEROPT) ? d->arg : -1;
}

s32 portWatchRowSubPage(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    return (d != NULL && d->kind == PWROW_SUBSCREEN) ? d->arg : PORTWATCH_GENERAL;
}

s32 portWatchRowOpt(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);

    if (d == NULL || d->kind != PWROW_OPT || d->key == NULL)
    {
        return -1;
    }
    /* Per-pad rows: the tables name pad1, but a pad device edits its own slot's key. This is
     * resolved live here, not in dynBuild, because sDyn is cached on the device
     * index and an unplug can move that index to another slot. */
    if (pageIsDynamic(page) && sDevice > 0 && strncmp(d->key, "pad1_", 5) == 0)
    {
        char padKey[32];

        if (!portInputDeviceIniKey(sDevice, d->key + 5, padKey, (s32) sizeof(padKey)))
        {
            return -1; /* the pad just went away: edit nothing, never pad 1 */
        }
        return portOptFind(padKey);
    }
    return portOptFind(d->key);
}

/** The Controller page's device, as words: its name, or the spec's
 *  "connect game controllers to your PC to configure them" wording when
 *  nothing is plugged in. The line under Input, and the raphnet page's. */
const char *portWatchDeviceText(void)
{
    return portWatchDeviceNoneConnected() ? "Connect a Controller\n" : portInputDeviceLabel(sDevice);
}

static const char *rowLabelShared(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt;

    if (d == NULL)
    {
        return "?\n";
    }
    if (d->label != NULL)
    {
        return d->label;
    }
    if (d->kind == PWROW_HINT)
    {   /* the line under Input: the device's name */
        return portWatchDeviceText();
    }
    if (d->kind == PWROW_ACTION)
    {
        return sResetArmed ? "Confirm Reset\n" : "Reset to Default\n";
    }
    if (d->kind == PWROW_PLAYEROPT)
    {
        return NULL; /* the caller draws the vanilla langGet string */
    }
    opt = portWatchRowOpt(page, row);
    return (opt >= 0) ? portOptLabel(opt) : "?\n";
}

/* Classic mode on the watch. port_options.c owns the list
 * of rows it forces; the folder's VIDEO sheet pends them for APPLY CHANGES,
 * but the watch has no apply row, so the switch acts at once - which now means
 * nothing more than writing classic_mode, because the option table's choke point
 * (optSet) applies the mode for every surface. */
extern s32  portClassicOwns(const char *key);

/** 1 = classic mode is on and owns this row: drawn dim, takes no input. */
s32 portWatchRowLocked(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    const s32 mode = portOptFind("classic_mode");

    return d != NULL && d->kind == PWROW_OPT && portClassicOwns(d->key)
        && mode >= 0 && portOptGet(mode) != 0;
}

/**
 * options.c portWatchGotoSubPage: a page was just opened. If classic mode is
 * already on but its rows are not at the forced values (an older or hand-edited
 * ini), put them right here, since the rows are locked and the watch has no
 * apply row. The remembered originals (classic_saved) are kept, except when
 * none exist (classic_mode=1 from the launcher or a pre-overhaul ini): then the
 * player's current values are remembered first so turning classic mode off has
 * something to restore.
 */
void portWatchPageEntered(s32 page)
{
    extern s32         portClassicCount(void);
    extern const char *portClassicKey(s32 i);
    extern s32         portClassicVal(s32 i);
    /* video.c; absent headless, as in portClassicSetLive */
    extern s32  portVideoClassicSavedGet(s32 *v) __attribute__((weak));
    extern void portVideoClassicSavedSet(const s32 *v) __attribute__((weak));
    const s32 mode = portOptFind("classic_mode");
    s32 wrote = 0;
    s32 i;

    if (page != PORTWATCH_VIDEO || mode < 0 || portOptGet(mode) == 0)
    {
        return;
    }
    if (portVideoClassicSavedGet != NULL && portVideoClassicSavedSet != NULL && portClassicCount() <= 16)
    {
        s32 v[16];

        if (portVideoClassicSavedGet(v) == 0)
        {
            for (i = 0; i < portClassicCount(); i++)
            {
                v[i] = portOptGet(portOptFind(portClassicKey(i)));
            }
            portVideoClassicSavedSet(v);
        }
    }
    for (i = 0; i < portClassicCount(); i++)
    {
        extern s32 portClassicPartOn(s32 i);   /* port_options.c: the CLASSIC sheet's rows */
        const s32 opt = portOptFind(portClassicKey(i));

        if (opt >= 0 && portClassicPartOn(i) && portOptAvailable(opt) && portOptGet(opt) != portClassicVal(i))
        {
            wrote += portOptSetValueQuiet(opt, portClassicVal(i)) ? 1 : 0;
        }
    }
    if (wrote > 0)
    {
        portOptSave();
    }
}

s32 portWatchRowAvailable(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt;

    if (d == NULL)
    {
        return 0;
    }
    if (d->kind != PWROW_OPT)
    {
        return 1; /* player options, the preset and the buttons always work */
    }
    opt = portWatchRowOpt(page, row);
    return (opt >= 0) && portOptAvailable(opt);
}

/** A hint row is text, not a setting: the highlight must skip over it. */
s32 portWatchRowSelectable(s32 page, s32 row)
{
    return portWatchRowKind(page, row) != PWROW_HINT;
}

/** Where a back button goes. Sub-pages know their own parent; nothing else
 *  should have to hold that map. */
s32 portWatchParentPage(s32 page)
{
    switch (page)
    {
        case PORTWATCH_GENSETTINGS:
        case PORTWATCH_HUD:
        case PORTWATCH_PORTOPTS:
            return PORTWATCH_GAMEPLAY;
        case PORTWATCH_VIDEO:
        case PORTWATCH_AUDIO:
        case PORTWATCH_GRAPHICS:
            return PORTWATCH_DISPLAY;
        case PORTWATCH_CONTROLLER_MORE:
        case PORTWATCH_CTLGENERAL:
        case PORTWATCH_BINDS:
            return PORTWATCH_CONTROLLER;
    }
    return PORTWATCH_GENERAL;
}

/**
 * What the selected device has bound to a PWROW_BIND row. Mouse and keyboard
 * read the [controls] binds through video.c's ini bridge (the same one the
 * console's `get` uses), and a pad reads its own padN_ map.
 */
static const char *bindValueText(s32 page, s32 row)
{
    static char text[80];
    const struct PortWatchRowDef *d = rowDef(page, row);
    const struct PortBindDef *b;

    if (d == NULL || d->kind != PWROW_BIND || d->arg < 0 || d->arg >= PORTBIND_COUNT)
    {
        return "";
    }
    b = &sBinds[d->arg];
    text[0] = '\0';

    if (sDevice == 0)
    {
        if (b->kbKey == NULL || !portVideoIniGetKey(b->kbKey, text, (s32) sizeof(text) - 1))
        {
            return "N/A\n";
        }
    }
    else
    {
        if (b->padKey == NULL || !portInputDeviceBindText(sDevice, b->padKey, text, (s32) sizeof(text) - 1))
        {
            return "N/A\n";
        }
    }
    if (text[0] == '\0')
    {
        snprintf(text, sizeof(text), "unbound");
    }
    /* the trailing newline is load-bearing: textMeasure only advances
     * textheight on one, and a zero-tall string is clipped away entirely
     * (see docs/port/watch-settings-pages.md) */
    {
        size_t n = strlen(text);
        if (n + 2 < sizeof(text))
        {
            text[n] = '\n';
            text[n + 1] = '\0';
        }
    }
    return text;
}

/* ---- values -------------------------------------------------------------- */

static const char *const sPresetText[] = { "Custom\n", "Nintendo 64\n", "Modern\n" };

/* PWROW_LAYOUT: the selected pad's bind config, the folder's "Bind Config"
 * row (front.c SHROW_LAYOUT). Every layout is offered, as there. */
extern s32 portInputLayoutCount(void);
extern const char *portInputLayoutName(s32 i);
extern s32 portInputLayoutApply(s32 dev, s32 i);
extern const char *portInputLayoutShown(s32 dev);

static s32 layoutIndex(void)
{
    const char *shown = portInputLayoutShown(sDevice);
    s32 i;

    for (i = 0; shown != NULL && shown[0] != '\0' && i < portInputLayoutCount(); i++)
    {
        if (strcmp(portInputLayoutName(i), shown) == 0)
        {
            return i;
        }
    }
    return -1;
}

static const char *layoutText(s32 i)
{
    static char text[72];

    snprintf(text, sizeof(text), "%s\n", (i >= 0) ? portInputLayoutName(i) : "Default");
    return text;
}

s32 portWatchRowValueCount(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt;

    if (d == NULL)
    {
        return 0;
    }
    if (d->kind == PWROW_PRESET)
    {
        /* three TEXTS but only two selectable values - "custom" is a report,
         * not a choice - so it is drawn as a single value with arrows */
        return 0;
    }
    if (d->kind == PWROW_RESOLUTION)
    {
        /* the resolution list is the one that is genuinely LONG (25 modes on
         * this machine), so it is the only row whose drop-down scrolls - and
         * the reason the more-above / more-below arrows exist at all. */
        return portVideoResLocked() ? 0 : portVideoResCount();
    }
    if (d->kind == PWROW_XHAIR)
    {
        return portXhairCount();
    }
    if (d->kind == PWROW_LAYOUT)
    {
        return portInputLayoutCount();
    }
    if (d->kind != PWROW_OPT)
    {
        return 0;
    }
    opt = portWatchRowOpt(page, row);
    return (opt >= 0) ? portOptValueCount(opt) : 0;
}

/* the deferred-edit slot lives further down, next to the resolution one, but
 * the row accessors below all have to consult it */
static s32  deferDbg(void);
static s32  deferPending(s32 page, s32 row);
static void deferStep(s32 page, s32 row, s32 dir);
static void deferSetIndex(s32 page, s32 row, s32 idx);
static void rowSetIndexLive(s32 page, s32 row, s32 idx);

/**
 * `idx` is a display index and nothing is flipped here: `invert` only applies
 * to toggles, whose texts ("OFF", "ON") are positional. The flip lives in the
 * index<->value conversions below.
 */
static const char *rowValueTextAtShared(s32 page, s32 row, s32 idx)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt;

    if (d != NULL && d->kind == PWROW_RESOLUTION)
    {
        static char text[24];
        s32 w = 0, h = 0;

        if (!portVideoResGet(idx, &w, &h))
        {
            return "";
        }
        snprintf(text, sizeof(text), "%dx%d\n", (int) w, (int) h);
        return text;
    }
    if (d != NULL && d->kind == PWROW_XHAIR)
    {
        return portXhairName(idx);
    }
    if (d != NULL && d->kind == PWROW_LAYOUT)
    {
        return layoutText(idx);
    }
    opt = portWatchRowOpt(page, row);
    if (opt < 0)
    {
        return "";
    }
    return portOptValueTextAt(opt, idx);
}

/** What the KEY actually holds, as a display index. */
static s32 rowLiveIndex(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt;
    s32 idx;

    if (d != NULL && d->kind == PWROW_RESOLUTION)
    {
        return portVideoResCurrent();
    }
    if (d != NULL && d->kind == PWROW_XHAIR)
    {
        return portXhairIndex();
    }
    if (d != NULL && d->kind == PWROW_LAYOUT)
    {
        return layoutIndex();
    }
    opt = portWatchRowOpt(page, row);
    if (opt < 0 || d == NULL)
    {
        return -1;
    }
    idx = portOptValueIndex(opt);
    if (d->invert && idx >= 0)
    {
        idx = idx ? 0 : 1;
    }
    return idx;
}

/**
 * What the row SHOWS: the candidate while a deferred row is being edited, the
 * live value otherwise. Everything that draws or compares a row's value goes
 * through here, which is what makes a deferred row look like it changed while
 * nothing has been applied yet.
 */
s32 portWatchRowValueIndex(s32 page, s32 row)
{
    const s32 pend = deferPending(page, row);

    return (pend >= 0) ? pend : rowLiveIndex(page, row);
}

static const char *rowValueTextShared(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt;

    if (d == NULL)
    {
        return "";
    }
    if (d->kind == PWROW_PRESET)
    {
        return sPresetText[portWatchPresetActive()];
    }
    if (d->kind == PWROW_DEVICE)
    {
        extern s32 portInputAssignActive(void); /* input_pc.c */

        if (portInputAssignActive())
        {   /* armed (options.c portWatchAssignBegin). Shorter than the
             * folder's "press a button on the device": the raphnet page draws
             * this left-aligned from x 0xAA and the face ends near 300. */
            return "Press a Button\n";
        }
        return "\n";   /* the name is the line under it (the hint after this row) */
    }
    if (d->kind == PWROW_BINDMODE)
    {   /* port = the port drives this device and you bind movement, look and
         * the action suite; n64 = the vanilla control styles drive it and you
         * bind the N64 pad. Any device can be either. */
        return bindModeIsN64() ? "N64\n" : "Port\n";
    }
    if (d->kind == PWROW_BIND)
    {
        if (portWatchBindCapturing(page, row))
        {
            return "Press a Key\n";
        }
        return bindValueText(page, row);
    }
    if (d->kind == PWROW_RESOLUTION)
    {
        return portWatchResText();
    }
    if (d->kind == PWROW_XHAIR)
    {
        return portXhairName(portXhairIndex());   /* "Custom" for a hand-typed hex */
    }
    if (d->kind == PWROW_LAYOUT)
    {
        return layoutText(layoutIndex());
    }
    if (d->kind != PWROW_OPT)
    {
        return ""; /* buttons, hints and player options carry no value */
    }
    opt = portWatchRowOpt(page, row);
    if (opt < 0)
    {
        return "N/A\n";
    }
    if (d->invert || deferPending(page, row) >= 0)
    {
        /* the DISPLAY slot, not the key's own - see portWatchRowValueTextAt.
         * A deferred row joins it so the row reads as the CANDIDATE while one
         * is pending; portOptValueText would report what is still live. */
        s32 idx = portWatchRowValueIndex(page, row);
        if (idx >= 0)
        {
            return portOptValueTextAt(opt, idx);
        }
    }
    return portOptValueText(opt);
}

void portWatchRowStep(s32 page, s32 row, s32 dir)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt;

    if (d == NULL || dir == 0)
    {
        return;
    }
    if (d->kind == PWROW_PRESET)
    {
        /* two presets, so either direction is "the other one"; from custom,
         * forward picks Nintendo 64 and back picks Modern */
        s32 now = portWatchPresetActive();
        s32 want = (now == 1) ? 2 : (now == 2) ? 1 : (dir > 0 ? 1 : 2);
        portWatchPresetApply(want);
        return;
    }
    if (d->kind == PWROW_DEVICE)
    {
        /* Not a stepper, as on the folder's player page: A / Z / a click arms the assign in
         * options.c, and left / right do nothing here. */
        return;
    }
    if (d->kind == PWROW_BINDMODE)
    {
        if (portBindModeCanN64(sDevice))
        {
            portBindModeToggle(sDevice);
            sDynPage = -1;   /* the list under it is a different list now */
        }
        return;
    }
    if (d->kind == PWROW_BIND)
    {
        /* a bind is not stepped: A / Z or a click arms the capture (options.c
         * portWatchActivateRow); left / right do nothing here. */
        return;
    }
    if (d->kind == PWROW_RESOLUTION)
    {
        portWatchResStep(dir); /* candidate only - Accept applies it */
        return;
    }
    if (d->kind == PWROW_XHAIR)
    {   /* live, like the folder: the sprite recolours at once */
        const s32 n = portXhairCount();
        const s32 cur = portXhairIndex();

        portXhairApply(((cur < 0 ? 0 : cur) + ((dir > 0) ? 1 : n - 1)) % n);
        return;
    }
    if (d->kind == PWROW_LAYOUT)
    {   /* live, like the folder: the binds under it change at once */
        const s32 n = portInputLayoutCount();
        const s32 cur = layoutIndex();

        if (n > 0)
        {
            portInputLayoutApply(sDevice, (cur < 0) ? ((dir > 0) ? 0 : n - 1)
                                                    : (cur + ((dir > 0) ? 1 : n - 1)) % n);
        }
        return;
    }
    if (d->kind != PWROW_OPT || portWatchRowLocked(page, row))
    {
        return;
    }
    opt = portWatchRowOpt(page, row);
    if (opt < 0)
    {
        return;
    }
    if (d->defer)
    {
        /* candidate only - Accept applies it. Steps in DISPLAY space, so the
         * invert flip below is already accounted for. */
        deferStep(page, row, dir);
        return;
    }
    {
        /* an inverted row's arrows must still move the way the SCREEN reads,
         * so flip the direction with the presentation.
         *
         * No classic-mode sync here any more: portOptStep ends in the option
         * table's choke point, which applies the mode when classic_mode changes. */
        portOptStep(opt, d->invert ? -dir : dir);
    }
}

/** Write a display index straight through to the key. The ONLY path that
 *  applies a value; a deferred row reaches it just once, from the commit. */
static void rowSetIndexLive(s32 page, s32 row, s32 idx)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    s32 opt = portWatchRowOpt(page, row);

    if (d == NULL || opt < 0 || idx < 0)
    {
        return;
    }
    if (d->invert)
    {
        idx = idx ? 0 : 1;
    }
    /* portOptSetIndex ends in the choke point, which applies the mode when
     * classic_mode changes - the watch needs no sync of its own. */
    portOptSetIndex(opt, idx);
}

void portWatchRowSetIndex(s32 page, s32 row, s32 idx)
{
    const struct PortWatchRowDef *d = rowDef(page, row);

    if (d == NULL || idx < 0 || portWatchRowLocked(page, row))
    {
        return;
    }
    if (d->kind == PWROW_RESOLUTION)
    {
        /* No candidate step here: reaching this function means a value was picked from the list
         * or a column, which is the Accept. portWatchResStep/Commit still serve the arrows. */
        portWatchResCancel();
        portVideoResApply(idx);
        return;
    }
    if (d->kind == PWROW_XHAIR)
    {
        portXhairApply(idx);
        return;
    }
    if (d->kind == PWROW_LAYOUT)
    {
        portInputLayoutApply(sDevice, idx);
        return;
    }
    if (d->defer)
    {
        /* clicking a value column on a deferred row picks a CANDIDATE; only
         * the Accept applies it. Everything outside this file calls THIS
         * function, so there is no way to set a deferred key by accident. */
        deferSetIndex(page, row, idx);
        return;
    }
    rowSetIndexLive(page, row, idx);
}

/* ---- rebinding a key from the watch ----------------------------------------
 *
 * A / Z on a bind row arms a capture, and the next key or mouse button pressed
 * becomes that action's binding.
 *
 * Scope: mouse and keyboard only. A pad bind still needs the launcher, because
 * capturing a pad button means driving the SDL joystick layer (as
 * portInputCapturePadAndExit does) and that has its own at-rest baseline
 * problem; the row says so instead of arming.
 *
 * A capture adds to the binding rather than replacing it, up to the four slots
 * a bind holds, and binding the same input twice is a no-op. Unbinding is a
 * separate gesture on the row (hold B, or press DELETE), so it never competes
 * with binding those inputs themselves.
 */
static s32 sBindCapPage = -1;
static s32 sBindCapRow = -1;

s32 portWatchBindCapturing(s32 page, s32 row)
{
    return (sBindCapPage == page && sBindCapRow == row);
}

s32 portWatchBindCaptureActive(void)
{
    return sBindCapPage >= 0;
}

/* the device the live arm belongs to: 0 = keyboard and mouse, else a pad.
 * Recorded at arm time rather than read from sDevice, so an arm can never
 * write its capture to a different device than the one it listened to. */
static s32 sBindCapDev;

void portWatchBindCaptureCancel(void)
{
    if (sBindCapPage >= 0)
    {
        portBindCooldownStart();
        if (sBindCapDev != 0)
        {
            portInputPadCaptureCancel();
        }
        else
        {
            portVideoBindCaptureCancel();
        }
    }
    sBindCapPage = -1;
    sBindCapRow = -1;
    sBindCapDev = 0;
}

/** 1 = armed. 0 = this row has nothing this device could bind. */
s32 portWatchBindCaptureBegin(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    const struct PortBindDef *b;

    if (d == NULL || d->kind != PWROW_BIND || d->arg < 0 || d->arg >= PORTBIND_COUNT)
    {
        return 0;
    }
    b = &sBinds[d->arg];
    if (sDevice != 0)
    {   /* A pad: the game listens to the pad itself (input_pc.c). A row with no padKey has no pad
         * control to bind (the mouse-only rows). */
        if (b->padKey == NULL || !portInputPadCaptureBegin(sDevice))
        {
            return 0;
        }
        sBindCapDev = sDevice;
    }
    else
    {
        if (b->kbKey == NULL)
        {
            return 0;
        }
        sBindCapDev = 0;
        portVideoBindCaptureBegin();
    }
    sBindCapPage = page;
    sBindCapRow = row;
    return 1;
}

/** Is `name` already one of this bind's inputs? Compares whole names, so
 *  "Up" does not match inside "Keypad Up". */
static s32 bindHasKey(const char *list, const char *name)
{
    const s32 n = (s32) strlen(name);
    const char *p = list;

    while (*p != '\0')
    {
        const char *comma = strchr(p, ',');
        const s32 len = (comma != NULL) ? (s32) (comma - p) : (s32) strlen(p);

        if (len == n && strncmp(p, name, (size_t) n) == 0)
        {
            return 1;
        }
        if (comma == NULL)
        {
            break;
        }
        p = comma + 1;
    }
    return 0;
}

/** The keyboard's Move keys are bound twice by default: analog_* (the Move stick) and c_* (the C
 *  buttons, kept so aim mode can still lean, zoom and duck - video.c sBindInfo). Copy a rebound
 *  Move row's value onto its C button so the pair stays one control, since no in-game list shows
 *  c_*. */
static void bindMirrorMoveToC(const char *key)
{
    static const char *const pairs[4][2] = {
        { "analog_up",    "c_up"    },
        { "analog_down",  "c_down"  },
        { "analog_left",  "c_left"  },
        { "analog_right", "c_right" },
    };
    char cur[80];
    s32 i;

    for (i = 0; i < 4; i++)
    {
        if (strcmp(key, pairs[i][0]) == 0)
        {
            cur[0] = '\0';
            portVideoIniGetKey(key, cur, (s32) sizeof(cur) - 1);
            if (strcmp(cur, "(unbound)") == 0)
            {   /* display text, not a value (see bindAddKey) */
                cur[0] = '\0';
            }
            portVideoIniSetKey(pairs[i][1], cur);
            return;
        }
    }
}

/** Append an input to a bind, up to the four slots midas.ini holds. */
static void bindAddKey(const char *key, const char *name)
{
    char cur[80];
    char next[96];
    s32 slots = 0;
    const char *p;

    cur[0] = '\0';
    portVideoIniGetKey(key, cur, (s32) sizeof(cur) - 1);
    if (strcmp(cur, "(unbound)") == 0)
    {   /* Display text, not a value: portVideoIniGetKey returns "(unbound)" for an empty bind, and
         * appending to that would write the literal "(unbound),W" into midas.ini. */
        cur[0] = '\0';
    }
    if (bindHasKey(cur, name))
    {
        return;   /* already bound here: adding it again would be a duplicate */
    }
    for (p = cur; *p != '\0'; p++)
    {
        if (*p == ',')
        {
            slots++;
        }
    }
    if (cur[0] != '\0')
    {
        slots++;
    }
    if (slots >= 4)
    {
        /* midas.ini holds four inputs per bind (BIND_MAX_KEYS). Drop the OLDEST
         * rather than refusing, so the row keeps responding to the player. */
        const char *comma = strchr(cur, ',');
        if (comma != NULL)
        {
            memmove(cur, comma + 1, strlen(comma + 1) + 1);
        }
        else
        {
            cur[0] = '\0';
        }
    }
    if (cur[0] == '\0')
    {
        snprintf(next, sizeof(next), "%s", name);
    }
    else
    {
        snprintf(next, sizeof(next), "%s,%s", cur, name);
    }
    {
        const s32 ok = portVideoIniSetKey(key, next);

        bindMirrorMoveToC(key);
        if (deferDbg())
        {
            char back[80];
            back[0] = '\0';
            portVideoIniGetKey(key, back, (s32) sizeof(back) - 1);
            fprintf(stderr, "ge: bind: %s += %s -> \"%s\" (set %d, reads back \"%s\")\n",
                    key, name, next, (int) ok, back);
        }
    }
}

/** Clear every input from the highlighted bind row. */
/* ---- capture and unbind by (device, index) ---------------------------- *
 *
 * The frontend's Options > CONTROL page binds through these, with its own arm
 * state separate from the watch's so neither screen can cancel the other's arm.
 * The tables, mode, video.c key capture and input_pc.c pad capture are shared,
 * so a binding made on one screen shows on the other.
 */
static s32 sSheetCapDev = -1;
static s32 sSheetCapIdx = -1;

s32 portBindCaptureActive(void)
{
    return sSheetCapDev >= 0;
}

s32 portBindCapturingRow(s32 i)
{
    return sSheetCapDev >= 0 && sSheetCapIdx == i;
}

extern void portBindCooldownStart(void); /* video.c - the post-capture blackout */

/** RESET TO DEFAULT: every bind of device dev, both the
 *  Port and N64 lists (they share each device's store). Live and saved. */
void portBindResetDefaults(s32 dev)
{
    extern void portBindCaptureCancel(void);         /* below */
    extern void portVideoBindsResetDefaults(void);   /* video.c */
    extern s32 portInputPadResetDefaults(s32 dev);   /* input_pc.c */

    portBindCaptureCancel();
    portWatchBindCaptureCancel();
    if (dev == 0)
    {
        portVideoBindsResetDefaults();
    }
    else
    {
        portInputPadResetDefaults(dev);
    }
}

/** A press on a PWROW_ACTION row (options.c portWatchActivateRow). 1 = handled. */
s32 portWatchRowAction(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);

    if (d == NULL || d->kind != PWROW_ACTION)
    {
        return 0;
    }
    if (!sResetArmed)
    {
        sResetArmed = 1;
        return 1;
    }
    sResetArmed = 0;
    portBindResetDefaults(sDevice);
    sDynPage = -1;   /* the rows read the new binds */
    return 1;
}

void portBindCaptureCancel(void)
{
    if (sSheetCapDev >= 0)
    {
        portBindCooldownStart();
    }
    if (sSheetCapDev > 0)
    {
        portInputPadCaptureCancel();
    }
    else if (sSheetCapDev == 0)
    {
        portVideoBindCaptureCancel();
    }
    sSheetCapDev = -1;
    sSheetCapIdx = -1;
}

/** 1 = armed. 0 = this row has nothing this device could bind. */
s32 portBindCaptureBegin(s32 dev, s32 i)
{
    s32 n = 0;
    const struct PortBindDef *t = bindTableFor(dev, &n);

    portBindCaptureCancel();
    if (i < 0 || i >= n)
    {
        return 0;
    }
    if (dev != 0)
    {
        if (t[i].padKey == NULL || !portInputPadCaptureBegin(dev))
        {
            return 0;
        }
    }
    else
    {
        if (t[i].kbKey == NULL)
        {
            return 0;
        }
        portVideoBindCaptureBegin();
    }
    sSheetCapDev = dev;
    sSheetCapIdx = i;
    return 1;
}

/** One tick. 1 when it finished, bound or given up. */
s32 portBindCaptureTick(void)
{
    s32 n = 0;
    const struct PortBindDef *t;
    char buf[32];

    if (sSheetCapDev < 0)
    {
        return 0;
    }
    t = bindTableFor(sSheetCapDev, &n);
    if (sSheetCapIdx < 0 || sSheetCapIdx >= n)
    {
        portBindCaptureCancel();
        return 1;
    }
    if (sSheetCapDev != 0)
    {
        extern s32 portInputPadCaptureHoldLong(void);

        if (portInputPadCaptureHoldLong())
        {   /* the button that armed the row, held two seconds = UNBIND (the pad is blanked
             * while armed, so only the capture engine can see a hold) */
            if (t[sSheetCapIdx].padKey != NULL)
            {
                portInputPadBindSet(sSheetCapDev, t[sSheetCapIdx].padKey, "-");
            }
            portBindCaptureCancel();
            return 1;
        }
        if (!portInputPadCaptureTick())
        {
            return 0;
        }
        if (portInputPadCaptureTake(buf, (s32) sizeof(buf)) && t[sSheetCapIdx].padKey != NULL)
        {
            portInputPadBindSet(sSheetCapDev, t[sSheetCapIdx].padKey, buf);
        }
        portBindCaptureCancel();
        return 1;
    }
    buf[0] = '\0';
    if (!portVideoBindCaptureTake(buf, (s32) sizeof(buf)))
    {   /* GE_BINDKEY=<midas.ini key name>: pretend that key was pressed on the next armed tick, so a
         * headless run (no keyboard) can exercise arm -> write -> redisplay.
         * One shot, because the row is re-armed every frame by a harness press;
         * never set in midas.ini. */
        extern char *getenv(const char *);
        static s32 fired;
        const char *e = getenv("GE_BINDKEY");

        if (e == NULL || e[0] == '\0' || fired)
        {
            return 0;
        }
        fired = 1;
        snprintf(buf, sizeof(buf), "%s", e);
        portVideoBindCaptureCancel();
    }
    if (buf[0] == '\0')
    {
        portBindCaptureCancel();
        return 1;
    }
    if (buf[0] != '\0' && t[sSheetCapIdx].kbKey != NULL)
    {
        bindAddKey(t[sSheetCapIdx].kbKey, buf);
    }
    portBindCaptureCancel();
    return 1;
}

/** Clear the row on that device. */
s32 portBindUnbind(s32 dev, s32 i)
{
    s32 n = 0;
    const struct PortBindDef *t = bindTableFor(dev, &n);

    if (i < 0 || i >= n)
    {
        return 0;
    }
    if (dev != 0)
    {   /* "-" is how midas.ini spells an unbound pad control, and it still
         * counts as a custom map, so it does NOT fall back to the layout */
        return (t[i].padKey != NULL) && portInputPadBindSet(dev, t[i].padKey, "-");
    }
    if (t[i].kbKey == NULL)
    {
        return 0;
    }
    portVideoIniSetKey(t[i].kbKey, "");
    bindMirrorMoveToC(t[i].kbKey);
    return 1;
}

s32 portWatchBindUnbind(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);
    const struct PortBindDef *b;

    if (d == NULL || d->kind != PWROW_BIND || d->arg < 0 || d->arg >= PORTBIND_COUNT)
    {
        return 0;
    }
    b = &sBinds[d->arg];
    if (sDevice != 0)
    {   /* the pad's own unbind: "-" is how midas.ini spells it, and it still
         * counts as a custom map, so the control does NOT fall back to the
         * built-in layout (see inputSetMapList) */
        if (b->padKey == NULL)
        {
            return 0;
        }
        return portInputPadBindSet(sDevice, b->padKey, "-");
    }
    if (b->kbKey == NULL)
    {
        return 0;
    }
    portVideoIniSetKey(b->kbKey, "");
    bindMirrorMoveToC(b->kbKey);
    if (deferDbg())
    {
        fprintf(stderr, "ge: bind: %s UNBOUND\n", b->kbKey);
    }
    return 1;
}

/**
 * One tick of an armed capture. 1 when it finished (bound or cancelled).
 *
 * An EMPTY captured name is the player pressing Escape, which cancels without
 * binding - the one key that must never become a binding, or there would be no
 * way out of this mode.
 */
s32 portWatchBindCaptureTick(void)
{
    char name[64];
    const struct PortWatchRowDef *d;
    const struct PortBindDef *b;

    if (sBindCapPage < 0)
    {
        return 0;
    }
    if (sBindCapDev != 0)
    {   /* a pad: input_pc.c is watching the device this arm named */
        extern s32 portInputPadCaptureHoldLong(void);
        char token[16];

        if (portInputPadCaptureHoldLong())
        {   /* the arming button held two seconds = unbind; the watch's own
             * any-button hold below cannot see it, the pad is blanked */
            d = rowDef(sBindCapPage, sBindCapRow);
            b = (d != NULL && d->kind == PWROW_BIND && d->arg >= 0 && d->arg < PORTBIND_COUNT)
                ? &sBinds[d->arg] : NULL;
            if (b != NULL && b->padKey != NULL)
            {
                portInputPadBindSet(sBindCapDev, b->padKey, "-");
            }
            portWatchBindCaptureCancel();
            return 1;
        }
        if (!portInputPadCaptureTick())
        {
            return 0;
        }
        d = rowDef(sBindCapPage, sBindCapRow);
        b = (d != NULL && d->kind == PWROW_BIND && d->arg >= 0 && d->arg < PORTBIND_COUNT)
            ? &sBinds[d->arg] : NULL;
        if (portInputPadCaptureTake(token, (s32) sizeof(token)) && b != NULL && b->padKey != NULL)
        {
            portInputPadBindSet(sBindCapDev, b->padKey, token);
        }
        else if (deferDbg())
        {
            fprintf(stderr, "ge: bind: pad capture gave up\n");
        }
        portWatchBindCaptureCancel();
        return 1;
    }
    if (!portVideoBindCaptureTake(name, (s32) sizeof(name)))
    {
        /* GE_WATCHBINDKEY=<midas.ini key name>: pretend that key was pressed on the next armed tick,
         * so a headless run can exercise the arm -> write -> redisplay loop.
         * Never set in midas.ini. */
        extern char *getenv(const char *);
        static s32 fired;
        const char *e = getenv("GE_WATCHBINDKEY");

        if (e == NULL || e[0] == 0 || fired)
        {
            return 0;   /* one shot: the harness re-arms every frame, so firing on each
                         * re-arm would not test that a real capture fires once */
        }
        fired = 1;
        snprintf(name, sizeof(name), "%s", e);
        portVideoBindCaptureCancel();
    }
    d = rowDef(sBindCapPage, sBindCapRow);
    b = (d != NULL && d->kind == PWROW_BIND && d->arg >= 0 && d->arg < PORTBIND_COUNT)
        ? &sBinds[d->arg] : NULL;
    if (name[0] != '\0' && b != NULL && b->kbKey != NULL)
    {
        bindAddKey(b->kbKey, name);
    }
    else if (deferDbg())
    {
        fprintf(stderr, "ge: bind: cancelled\n");
    }
    portWatchBindCaptureCancel();
    return 1;
}


/* ---- the drop-down list ---------------------------------------------------
 *
 * A row with more values than fit across the watch face used to show one value
 * between < > arrows, so you stepped blind. The drop-down shows the whole list
 * vertically at once and you pick from it, like Perfect Dark's own vertical
 * menus.
 *
 * Which rows get it is decided by measurement, not a count: a row gets the list
 * exactly when its values do not all fit on the line (portWatchRowDropdown in
 * options.c). Two-value rows and three-value rows that fit keep the vanilla
 * look.
 *
 * Nothing is applied while the list is open: moving the highlight changes only
 * the highlight, the choice applies on confirm, and closing with B applies
 * nothing. That also gives the deferred rows (display mode, resolution) their
 * confirm step for free. Only one list can be open, because only one row can be
 * highlighted.
 */
static s32 sDropPage = -1;
static s32 sDropRow = -1;
static s32 sDropIdx = 0;     /* the entry under the highlight */
static s32 sDropScroll = 0;  /* first visible entry, for a list taller than the face */
static s32 sDropTop = 0;     /* screen y of the first visible entry, FIXED at open */

/* The CRT-style open animation: one scalar covers both phases, 0 -> 1 is a horizontal line growing
 * out from the centre and 1 -> 2 is that line opening vertically into the
 * panel. Closing runs it backwards.
 *
 * It advances on the 60 Hz menu field (portWatchMenuField(), the gate
 * watch_adjust_volume_slider uses), not per tick, so the speed does not change
 * with tickhz. */
/* how many entries stay visible beyond the highlight while the list scrolls */
#define PORTWATCH_DROP_MARGIN 2
#define PORTWATCH_DROP_ANIM_W 0.30f   /* ~4 fields for the line  */
#define PORTWATCH_DROP_ANIM_H 0.22f   /* ~5 fields for the open  */
static f32 sDropAnim;        /* 0 .. 2 */
static s32 sDropClosing;
static s32 sDropOpens;     /* portWatchDropOpens */

/** Logically open: taking input. False while it is animating shut. */
s32 portWatchDropIsOpen(void)
{
    return sDropPage >= 0 && !sDropClosing;
}

/** On screen: still drawn through the closing animation. */
s32 portWatchDropIsVisible(void)
{
    return sDropPage >= 0;
}

/** 0..1 how far the horizontal line has grown. */
f32 portWatchDropAnimW(void)
{
    f32 a = sDropAnim;

    if (a < 0.0f) a = 0.0f;
    return (a > 1.0f) ? 1.0f : a;
}

/** 0..1 how far the panel has opened vertically. */
f32 portWatchDropAnimH(void)
{
    f32 a = sDropAnim - 1.0f;

    if (a < 0.0f) a = 0.0f;
    return (a > 1.0f) ? 1.0f : a;
}

/** Entries are only drawn once the panel is all the way open. */
s32 portWatchDropAnimDone(void)
{
    return sDropAnim >= 2.0f;
}

/** One menu field of the open / close animation. */
void portWatchDropAnimTick(void)
{
    if (sDropPage < 0 || !portWatchMenuField())
    {
        return;
    }
    if (sDropClosing)
    {
        sDropAnim -= (sDropAnim > 1.0f) ? PORTWATCH_DROP_ANIM_H : PORTWATCH_DROP_ANIM_W;
        if (deferDbg())
        {
            fprintf(stderr, "ge: drop: anim %.2f (closing)\n", (double) sDropAnim);
        }
        if (sDropAnim <= 0.0f)
        {
            /* the animation is what kept the row alive; now let it go */
            sDropPage = -1;
            sDropRow = -1;
            sDropIdx = 0;
            sDropScroll = 0;
            sDropAnim = 0.0f;
            sDropClosing = 0;
        }
        return;
    }
    sDropAnim += (sDropAnim < 1.0f) ? PORTWATCH_DROP_ANIM_W : PORTWATCH_DROP_ANIM_H;
    if (sDropAnim > 2.0f)
    {
        sDropAnim = 2.0f;
    }
    if (deferDbg())
    {
        fprintf(stderr, "ge: drop: anim %.2f (%s)\n", (double) sDropAnim,
                (sDropAnim < 1.0f) ? "widening" : (sDropAnim < 2.0f) ? "opening" : "open");
    }
}

s32 portWatchDropPage(void)
{
    return sDropPage;
}

s32 portWatchDropRow(void)
{
    return sDropRow;
}

s32 portWatchDropIndex(void)
{
    return sDropIdx;
}

s32 portWatchDropScroll(void)
{
    return sDropScroll;
}

s32 portWatchDropOpenOn(s32 page, s32 row)
{
    return (sDropPage == page && sDropRow == row);
}

/** Start closing. The row stays alive until the animation has run back. */
void portWatchDropClose(void)
{
    if (sDropPage >= 0)
    {
        sDropClosing = 1;
        if (deferDbg())
        {
            fprintf(stderr, "ge: drop: close requested (page %d row %d)\n",
                    (int) sDropPage, (int) sDropRow);
        }
    }
}

/** Gone this instant, no animation: leaving the page or the tab entirely. */
void portWatchDropCloseNow(void)
{
    sDropPage = -1;
    sDropRow = -1;
    sDropIdx = 0;
    sDropScroll = 0;
    sDropAnim = 0.0f;
    sDropClosing = 0;
}

/** Open on a row, with the highlight on the value that is live. */
void portWatchDropOpen(s32 page, s32 row)
{
    s32 idx;

    if (portWatchRowValueCount(page, row) <= 0 || portWatchRowLocked(page, row))
    {
        return;
    }
    sDropPage = page;
    sDropRow = row;
    idx = portWatchRowValueIndex(page, row);
    if (idx < 0 && portWatchRowOpt(page, row) >= 0)
    {
        /* An off-grid ranged value (default deadzone=4096 is 13%) has no live entry, so open on the
         * nearest one rather than the top; otherwise an immediate A could zero the setting. */
        idx = portOptValueIndexNear(portWatchRowOpt(page, row), 0);
    }
    sDropIdx = (idx >= 0) ? idx : 0;
    sDropScroll = 0;
    sDropAnim = 0.0f;
    sDropClosing = 0;
    sDropOpens++;
}

/** How many times a list has opened: the watch's mouse layer uses it to tell a fresh open from
 *  the one it has been pointing at. */
s32 portWatchDropOpens(void)
{
    return sDropOpens;
}

/** Keep the highlight inside the visible window. */
void portWatchDropScrollFollow(s32 vis)
{
    const s32 n = portWatchDropCount();

    if (vis <= 0 || n <= vis)
    {
        sDropScroll = 0;
        return;
    }
    /* Keep two entries in hand either side of the highlight so there is always something to scroll
     * towards. The clamp below keeps those two selectable at the real ends of the list. */
    if (sDropIdx - PORTWATCH_DROP_MARGIN < sDropScroll)
    {
        sDropScroll = sDropIdx - PORTWATCH_DROP_MARGIN;
    }
    else if (sDropIdx + PORTWATCH_DROP_MARGIN >= sDropScroll + vis)
    {
        sDropScroll = sDropIdx + PORTWATCH_DROP_MARGIN - vis + 1;
    }
    if (sDropScroll > n - vis)
    {
        sDropScroll = n - vis;
    }
    if (sDropScroll < 0)
    {
        sDropScroll = 0;
    }
}

s32 portWatchDropCount(void)
{
    return portWatchDropIsVisible() ? portWatchRowValueCount(sDropPage, sDropRow) : 0;
}

const char *portWatchDropTextAt(s32 i)
{
    return portWatchDropIsVisible() ? portWatchRowValueTextAt(sDropPage, sDropRow, i) : "";
}

/** Which entry is the LIVE one, so the list can mark it. */
s32 portWatchDropLiveIndex(void)
{
    return portWatchDropIsVisible() ? rowLiveIndex(sDropPage, sDropRow) : -1;
}

void portWatchDropMove(s32 dir)
{
    const s32 n = portWatchDropCount();

    if (n <= 0 || dir == 0)
    {
        return;
    }
    sDropIdx += (dir > 0) ? 1 : -1;
    if (sDropIdx >= n)
    {
        sDropIdx = 0;
    }
    if (sDropIdx < 0)
    {
        sDropIdx = n - 1;
    }
}

/**
 * Where the panel sits, decided once at open and then left alone. A drop-down
 * is a fixed window that the contents move inside, so the box must not slide as
 * the list scrolls past the ends.
 */
void portWatchDropSetTop(s32 y)
{
    sDropTop = y;
}

s32 portWatchDropTop(void)
{
    return sDropTop;
}

/**
 * Scroll the window by n entries without touching the highlight; this is what
 * the mouse wheel does. If the wheel moved the highlight, the window would
 * scroll, hover would snap the highlight onto whatever slid under the
 * stationary cursor, and the window would follow again in a feedback loop. So
 * the wheel moves the view, hover picks the entry under the pointer, and
 * neither chases the other.
 */
void portWatchDropScrollBy(s32 n, s32 vis)
{
    const s32 count = portWatchDropCount();
    s32 max = count - vis;

    if (vis <= 0 || max <= 0)
    {
        return;   /* the whole list is on screen: nothing to scroll */
    }
    {
        const s32 was = sDropScroll;

        sDropScroll += n;
        if (sDropScroll > max) sDropScroll = max;
        if (sDropScroll < 0) sDropScroll = 0;
        if (deferDbg())
        {
            fprintf(stderr, "ge: drop: wheel %+d -> scroll %d..%d (of %d, max %d)\n",
                    (int) n, (int) was, (int) sDropScroll, (int) count, (int) max);
        }
    }
}

/** Park the window so the live entry lands on the row's own line (the open
 *  geometry decides where that is; options.c owns the layout). */
void portWatchDropSetScroll(s32 i)
{
    const s32 n = portWatchDropCount();

    if (i < 0) i = 0;
    if (i > n - 1) i = n - 1;
    if (i < 0) i = 0;
    sDropScroll = i;
}

void portWatchDropSetIndex(s32 i)
{
    if (portWatchDropIsVisible() && i >= 0 && i < portWatchDropCount())
    {
        sDropIdx = i;
    }
}

/** Confirm: apply the highlighted entry and close. */
void portWatchDropChoose(void)
{
    const s32 page = sDropPage;
    const s32 row = sDropRow;
    const s32 idx = sDropIdx;

    if (page < 0)
    {
        return;
    }
    /* Goes through the public setter, so a deferred row parks it as the candidate and the commit
     * below is the Accept; a plain row sets the key outright and the commit is a no-op. */
    portWatchRowSetIndex(page, row, idx);
    portWatchDeferCommit();
    /* animate shut AFTER applying, so the live band slides onto the entry that
     * was just chosen while the panel closes over it */
    portWatchDropClose();
}

/* ---- a deferred OPT row's pending choice ---------------------------------
 *
 * The same contract as the resolution row below, for any PWROW_OPT row whose
 * table entry sets `defer`: stepping moves a candidate and changes only what
 * the row says, Accept applies it, and moving off the row throws it away. There
 * is one slot because only one row can be highlighted, and moving off must
 * cancel so a stale candidate cannot apply on a later A press elsewhere.
 *
 * The index is the row's display index (the space portWatchRowValueIndex
 * reports and portWatchRowSetIndex takes), so `invert` is handled once at the
 * edges.
 */
static s32 sDeferPage = -1;
static s32 sDeferRow = -1;
static s32 sDeferIdx = -1;

/** 1 when this row applies only on Accept. */
s32 portWatchRowDeferred(s32 page, s32 row)
{
    const struct PortWatchRowDef *d = rowDef(page, row);

    return (d != NULL && d->kind == PWROW_OPT && d->defer) ? 1 : 0;
}

/** The candidate on this row, or -1 when there is none. */
static s32 deferPending(s32 page, s32 row)
{
    return (sDeferPage == page && sDeferRow == row) ? sDeferIdx : -1;
}

static s32 deferDbg(void)
{
    static s32 dbg = -1;
    extern char *getenv(const char *);
    if (dbg < 0)
    {
        dbg = getenv("GE_WATCHNAVDBG") != NULL;
    }
    return dbg;
}

void portWatchDeferCancel(void)
{
    if (deferDbg() && sDeferPage >= 0)
    {
        fprintf(stderr, "ge: defer: cancel (page %d row %d idx %d)\n", (int) sDeferPage, (int) sDeferRow, (int) sDeferIdx);
    }
    sDeferPage = -1;
    sDeferRow = -1;
    sDeferIdx = -1;
}

/** Entering the row: seed the candidate from what is live. */
void portWatchDeferBegin(s32 page, s32 row)
{
    if (!portWatchRowDeferred(page, row))
    {
        return;
    }
    sDeferPage = page;
    sDeferRow = row;
    sDeferIdx = rowLiveIndex(page, row);
    if (sDeferIdx < 0)
    {
        sDeferIdx = 0;
    }
}

/** Accept: apply the candidate, if there is one, and forget it. */
void portWatchDeferCommit(void)
{
    const s32 page = sDeferPage;
    const s32 row = sDeferRow;
    const s32 idx = sDeferIdx;

    portWatchDeferCancel();
    if (page >= 0 && row >= 0 && idx >= 0)
    {
        const s32 was = rowLiveIndex(page, row);
        rowSetIndexLive(page, row, idx);
        if (deferDbg())
        {
            fprintf(stderr, "ge: defer: commit page %d row %d: live %d -> %d (wanted %d)\n",
                    (int) page, (int) row, (int) was, (int) rowLiveIndex(page, row), (int) idx);
        }
    }
}

/** Move the candidate, seeding it first if the row was stepped without an A. */
static void deferStep(s32 page, s32 row, s32 dir)
{
    const s32 n = portWatchRowValueCount(page, row);

    if (n <= 0)
    {
        return;
    }
    if (deferPending(page, row) < 0)
    {
        portWatchDeferBegin(page, row);
        if (deferPending(page, row) < 0)
        {
            return; /* not a deferred row after all: nothing to move */
        }
    }
    sDeferIdx += (dir > 0) ? 1 : -1;
    if (sDeferIdx >= n)
    {
        sDeferIdx = 0;
    }
    if (sDeferIdx < 0)
    {
        sDeferIdx = n - 1;
    }
    if (deferDbg())
    {
        fprintf(stderr, "ge: defer: step page %d row %d: candidate %d, live still %d\n",
                (int) page, (int) row, (int) sDeferIdx, (int) rowLiveIndex(page, row));
    }
}

/** Set the candidate outright (a mouse click straight onto a value column). */
static void deferSetIndex(s32 page, s32 row, s32 idx)
{
    const s32 n = portWatchRowValueCount(page, row);

    if (idx < 0 || idx >= n)
    {
        return;
    }
    sDeferPage = page;
    sDeferRow = row;
    sDeferIdx = idx;
}

/* ---- the resolution row's pending choice ---------------------------------
 *
 * The row carries a candidate: stepping moves the candidate and changes only
 * what the row says, Accept applies it, and moving off the row throws it away.
 * This keeps anyone from being stranded in a mode the monitor cannot display.
 */
static s32 sResPending = -1;

void portWatchResBegin(void)
{
    sResPending = portVideoResCurrent();
    if (sResPending < 0 && portVideoResCount() > 0)
    {
        sResPending = 0;
    }
}

void portWatchResCancel(void)
{
    sResPending = -1;
}

void portWatchResCommit(void)
{
    if (sResPending >= 0)
    {
        portVideoResApply(sResPending);
    }
    sResPending = -1;
}

void portWatchResStep(s32 dir)
{
    const s32 n = portVideoResCount();

    if (n <= 0 || dir == 0 || portVideoResLocked())
    {
        return;
    }
    if (sResPending < 0)
    {
        portWatchResBegin();
    }
    sResPending += (dir > 0) ? 1 : -1;
    if (sResPending >= n)
    {
        sResPending = 0;
    }
    if (sResPending < 0)
    {
        sResPending = n - 1;
    }
}

/**
 * What the row shows: the pending choice while one is being made, the live
 * window size otherwise. Borderless fullscreen has no choice to make - the
 * window IS the display - so it says so rather than offering a list.
 */
const char *portWatchResText(void)
{
    static char text[32];
    s32 i = (sResPending >= 0) ? sResPending : portVideoResCurrent();
    s32 w = 0, h = 0;

    if (portVideoResLocked())
    {
        return "display\n";
    }
    if (i < 0 || !portVideoResGet(i, &w, &h))
    {
        return "?\n";
    }
    snprintf(text, sizeof(text), "%dx%d\n", (int) w, (int) h);
    return text;
}

/* ---- presets ------------------------------------------------------------- */

/*
 * Section 12.2 of the spec is this table. Both presets set the same key list,
 * so "which preset am I on" is a plain comparison; hw_mipmaps is in the list
 * although it is INI-only, because the Nintendo 64 look depends on it and two
 * machines with different inis would otherwise differ.
 */
struct PortPresetEntry {
    const char *key;
    s32 n64;
    s32 modern;
};

static const struct PortPresetEntry sPreset[] = {
    /* key              Nintendo 64   Modern */
    { "low_res",        240,          0    },  /* 240 lines vs the window's own */
    /* The look presets speak for themselves rather than for the classic parts:
     * they name BOTH keys. classic_mode is what forces the seven (the choke
     * point runs the transition, so the preset is a real preset and not just
     * eight writes); n64_aa is named beside it so the Nintendo 64 look keeps
     * its edges even if the player switched the classic_n64aa part off. This is
     * the vestigial half the comment above admits to - the table predates
     * Classic Mode and still disagrees with it (it asks for bilinear where the
     * mode forces 3-point), so a match here is not a claim the mode is
     * consistent. */
    { "classic_mode",   1,            0    },
    { "n64_aa",         1,            0    },
    { "filtering",      1,            2    },  /* bilinear vs 3-point (the N64's own filter, Murk 2026-10-15) */
    { "anisotropy",     0,            16   },
    { "filter_2d",      1,            0    },
    { "high_lod",       1,            1    },
    { "hfov_wide",      90,           90   },  /* 90 = vertical 60, vanilla (videoHfovWide) */
    { "maxfps",         60,           120  },
};

#define PORTPRESET_COUNT ((s32)(sizeof(sPreset) / sizeof(sPreset[0])))

static s32 presetMatches(s32 which)
{
    s32 i;
    s32 checked = 0;

    for (i = 0; i < PORTPRESET_COUNT; i++)
    {
        s32 opt = portOptFind(sPreset[i].key);
        s32 want = (which == 1) ? sPreset[i].n64 : sPreset[i].modern;

        if (opt < 0 || !portOptAvailable(opt))
        {
            continue; /* no renderer linked: that key cannot disagree */
        }
        checked++;
        if (portOptGet(opt) != want)
        {
            return 0;
        }
    }
    /* nothing readable at all is not a match - otherwise a headless build
     * would report both presets active at once */
    return checked > 0;
}

s32 portWatchPresetActive(void)
{
    if (presetMatches(1))
    {
        return 1;
    }
    if (presetMatches(2))
    {
        return 2;
    }
    return 0;
}

void portWatchPresetApply(s32 which)
{
    s32 i;
    s32 wrote = 0;

    if (which != 1 && which != 2)
    {
        return;
    }
    for (i = 0; i < PORTPRESET_COUNT; i++)
    {
        s32 opt = portOptFind(sPreset[i].key);
        s32 want = (which == 1) ? sPreset[i].n64 : sPreset[i].modern;

        if (opt < 0)
        {
            continue;
        }
        /* quiet: ONE save at the end, not ten rewrites of midas.ini */
        if (portOptSetValueQuiet(opt, want))
        {
            wrote++;
        }
    }
    if (wrote > 0)
    {
        portOptSave();
    }
}

/* ---- what options.c is handed: the shared strings, in the watch's case -----
 * See pwWatchCase. A PLAYEROPT label is NULL (Rare's own string) and passes
 * through; a bind row's VALUE is a key name and is left as typed. */
const char *portWatchRowLabel(s32 page, s32 row)
{
    return pwWatchCase(rowLabelShared(page, row));
}

const char *portWatchRowValueTextAt(s32 page, s32 row, s32 idx)
{
    return pwWatchCase(rowValueTextAtShared(page, row, idx));
}

const char *portWatchRowValueText(s32 page, s32 row)
{
    const char *t = rowValueTextShared(page, row);

    return (portWatchRowKind(page, row) == PWROW_BIND) ? t : pwWatchCase(t);
}

#endif /* TARGET_N64 */
