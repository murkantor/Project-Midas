/**
 * SDL2 game controller + mouse backend (GE_VIDEO builds only), modeled on PD's port/src/input.c.
 *
 *  - Pads: up to four SDL game controllers map 1:1 onto N64 pad slots 0-3, with hotplug via an
 *    SDL event watch. Slot 0 also merges the keyboard pad video.c builds (buttons OR together;
 *    the controller stick wins when deflected).
 *  - Twin stick: in a 2.x control style, the game's "second controller" read (bondview2.c) comes
 *    from the same pad's right stick via joy_port.c portJoyTwinStickRead, with LT as that pad's Z.
 *    Pads without a right stick fall back to the real second-pad read. While the game uses the
 *    stick as analog, right-stick C emulation is muted (s_PadRightCMute, self-expiring).
 *  - Swap sticks (padN_swap_sticks, per pad, default off): Move and Look sticks trade places at
 *    the axis read, so Move/Look rows, raw/calibrated modes and C emulation all follow together.
 *  - Built-in layout (seeded per device from its SDL bindings, inputDefaultTokens): left stick
 *    move, right stick look, D-pad digital move, RT fire, LT aim, A use/confirm, X reload,
 *    Y/B prev/next gadget, LB/RB prev/next weapon and watch page, LS+RS click crouch toggle,
 *    Start pause. N64 A/B, C buttons and N64 D-pad are unbound (rebindable in the launcher).
 *    The fixed-map code path only serves pads whose SDL bindings failed to seed.
 *  - Analog: deadzone-rescaled (PD inputAxisScale), then scaled to the N64's +/-80 range, NOT
 *    PD's +/-127, because GE tuning constants (e.g. /70 saturation) assume real stick reach.
 *  - Mouse: relative-mode look; click to capture, Escape to release. Deltas are handed to
 *    bondviewProcessInput through portInputMouseLookDelta() with PD's scaling
 *    (sens * raw/3.5 * 0.022). Mouse buttons are bindable from midas.ini ("Mouse Left" etc).
 *  - Rumble: motor_stub.c routes osMotorStart/Stop here; fires on any pad that supports it.
 *
 * Settings arrive via portInputSetOption() from video.c's midas.ini parser.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ultra64.h>
#include "port_input.h"

#include <SDL.h>
#include "port_console.h" /* the tilde console: fixed keys feed it below */
static const char *inputDevRecPath(const char *id);   /* per-device records, below */

#define INPUT_MAX_PADS 4

/* ---- settings (midas.ini via portInputSetOption) --------------------------- */

static int   s_OptMouse = 1;            /* mouse look master switch */
/* mouse_mode is retired: mouse deltas always move the camera angle directly
 * (like the 1964 GEPD injector) instead of feeding the engine's smoothed turn
 * rate. The key is read and ignored; the old rate path stays behind this
 * constant for manual A/B tests. */
static const int s_OptMouseMode = 1;
static float s_OptMouseSensX = 2.5f;
static float s_OptMouseSensY = 2.5f;
/* Separate sensitivity for the aim-mode crosshair, since dragging a cursor
 * across a fixed screen box wants a different speed than free-look camera
 * rotation. 1.0 = reference rate (full deflection in ~376 counts at
 * mouse_sens_x 2.5); higher is faster. */
static float s_OptMouseSensAim = 1.0f;
/* right_stick_c and twin_stick are no longer options: the layout decides
 * whether a mapped pad's right stick is C or Look, and the raw fallback always
 * uses it as C. Both ini keys are still accepted and ignored so older midas.ini
 * files don't log errors. */
static int   s_OptDeadzone = 3277;      /* of 32768, 10%: the Move stick ([input] deadzone) */
static int   s_OptDeadzoneLook = 3277;  /* of 32768, 10%: the Look stick ([input] deadzone_look) */
static int   s_OptTrigThreshold = 3276; /* trigger "pressed" point: 10% of the pull
                                         * in the menu's own units
                                         * (10 * 32767 / 100, port_options.c PCT) */
static float s_OptStickScale = 1.0f;    /* extra stick sensitivity */
static int   s_OptPads = 4;             /* N64 pads reported connected */
static int   s_OptRumble = 1;
static int   s_OptShowInput = 0;        /* on-screen stick X/Y readout */
static int   s_OptSmartLock = 1;        /* [input] smart_mouse_lock: capture the pointer exactly
                                         * while gameplay owns it, instead of click-to-capture */
/* [input] p1_hybrid (default on): player 1 takes keyboard, mouse and pad
 * together, e.g. aim with the mouse and move with a pad. Off stops
 * keyboard/mouse driving player 1 while a pad owns it during gameplay only;
 * menus always take the keyboard so you can get back. */
static int   s_OptP1Hybrid = 1;
/* [input] p1_view: which device player 1's settings page shows and binds (0 =
 * mouse and keyboard, 1 = the pad in slot 0), set by pressing a key or button
 * on the page's "input device" row; with p1_hybrid both still drive player 1. */
static int   s_OptP1View = 0;
/* [input] p1_settings_for: whose Devices/ record the pad1_* keys currently
 * belong to ("kbm", a GUID or raphnet<chan>; "" = unknown). It never decides
 * who sits in slot 0, only where settings get saved (see the PER-DEVICE
 * SETTINGS block). */
static char  s_P1SettingsFor[48];
static int   s_OptDInput = 0;          /* dinput=1 re-enables SDL's DirectInput
                                         * backend (deadlocks under Steam) */
static int   s_PadRumbleOn[INPUT_MAX_PADS] = { 1, 1, 1, 1 }; /* padN_rumble:
                                         * per-player "rumble pak inserted" */
static int   s_PadSwapOpt[INPUT_MAX_PADS];  /* padN_swap_sticks: this pad's Move and
                                             * Look sticks trade places (default off) */
/* GADGET SWITCH (padN_gadget_switch=1): a real N64 pad has no spare button for
 * "next gadget", so Z pressed while B is held becomes next_inventory and is
 * masked out of the pad word until released (applied in portInputPadGadgetChord
 * for every backend). */
static int   s_PadGadgetSwitch[INPUT_MAX_PADS];
/* L+R CLASSIC MODE (padN_lr_classic=1, off by default since R is aim): holding
 * L and R together on an N64-mode pad for PADCHORD_LR_MS raises that slot's
 * classic_toggle action once per hold (portInputPadGadgetChord). */
static int   s_PadLrClassic[INPUT_MAX_PADS];
static Uint32 s_PadLrSince[INPUT_MAX_PADS];   /* SDL ticks both went down, 0 = not both */
static int   s_PadLrFired[INPUT_MAX_PADS];    /* this hold already toggled */
#define PADCHORD_LR_MS 500
/* Hotplug diagnostics: every hotplug event logs a slot table (inputLogSlots)
 * and each poll checks for a handle SDL detached without a REMOVED event
 * (s_PadZombieLogged); [input] rawinput=0 (or GE_RAWINPUT=0) leaves Xbox pads
 * to XInput/WGI. */
static int   s_OptRawInput = 1;
static int   s_PadZombieLogged[INPUT_MAX_PADS];
static u16   s_ChordPrevBtn[INPUT_MAX_PADS];
static int   s_ChordZMasked[INPUT_MAX_PADS];

/* Per-slot device assignment (midas.ini pad1..pad4): "auto"/"" = connect order,
 * "none" = keep the slot free, else an SDL joystick GUID with optional "#n"
 * picking the n-th identical device; GUIDs are stable per model+connection, so
 * assignments survive replugs. */
static char s_OptPadAssign[INPUT_MAX_PADS][48];
/* midas.ini padN_layout=<name>: which layout this slot uses, preferred over "first
 * matching layout" once a controller has several (e.g. Standard and Pro). Empty
 * = no pick yet, so first-match decides (Pro, as the shipped file lists it
 * first). */
static char s_PadLayoutWant[INPUT_MAX_PADS][64];
/* midas.ini padN_layout_user: 1 = the name is the player's own saved layout, 0 = a
 * shipped one, -1 = not recorded (older ini). Needed because a user layout can
 * share a shipped layout's name, and the name alone used to resolve to the
 * wrong copy. */
static int  s_PadLayoutUser[INPUT_MAX_PADS] = { -1, -1, -1, -1 };

/* Per-slot custom controller map (midas.ini padN_<control>=<token>, from the
 * launcher rebind UI): bN = button, aN+/aN- = axis direction, hNu/d/l/r = hat.
 * Any entry makes the device open raw and the custom map replace the built-in
 * layout wholesale. */
enum
{
    PM_A, PM_B, PM_Z, PM_START, PM_L, PM_R,
    PM_CUP, PM_CDOWN, PM_CLEFT, PM_CRIGHT,
    PM_DUP, PM_DDOWN, PM_DLEFT, PM_DRIGHT,
    PM_STICK_X, PM_STICK_Y,   /* the MOVE stick (launcher label) */
    PM_LOOK_X, PM_LOOK_Y,     /* TWIN STICK: the LOOK stick = the 2.x second pad (default: right stick) */
    /* DIGITAL movement: a button / hat / axis direction that drives the Move or
     * Look vector at full deflection (+/-85), like the keyboard's
     * analog_up/down/left/right. Unbound by default. */
    PM_MOVE_UP, PM_MOVE_DOWN, PM_MOVE_LEFT, PM_MOVE_RIGHT,
    PM_LOOK_UP, PM_LOOK_DOWN, PM_LOOK_LEFT, PM_LOOK_RIGHT,
    /* Port suite actions on this controller (joy_port.c PORTACT_* - SAME
     * ORDER, checked at init); unbound by default, evaluated like buttons */
    PM_PORT_FIRST,
    /* +19 mouse_unlock (never bound on a pad, but the order must match
     * joy_port's, checked at init),
     * +20 classic_toggle (Classic Mode bindable on a pad, as Tab is on the keyboard) */
    PM_PORT_LAST = PM_PORT_FIRST + 20,
    PM_COUNT
};
#define PM_PORT_COUNT (PM_PORT_LAST - PM_PORT_FIRST + 1)
#define PM_UNBOUND_FIRST PM_MOVE_UP /* first entry with no built-in default */
static const char *sPadMapNames[PM_COUNT] =
{
    "a", "b", "z", "start", "l", "r",
    "c_up", "c_down", "c_left", "c_right",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    "stick_x", "stick_y",
    "look_x", "look_y",
    "move_up", "move_down", "move_left", "move_right",
    "look_up", "look_down", "look_left", "look_right",
    "reload", "use", "walk", "run", "walk_toggle",
    "prev_weapon", "next_weapon", "prev_inventory", "next_inventory",
    "crouch_toggle", "crouch_hold", "watch_prev", "watch_next", "watch_objectives",
    "detonate",
    "zoom_in", "zoom_out",
    "menu_accept", "menu_back",
    "mouse_unlock", "classic_toggle",
};
/* The raw (unmapped DInput) fallback layout of portInputPadMerge expressed as
 * map tokens, so the launcher, first-rebind seed and bind rows can describe it.
 * Keep in step with the merge's fixed reads; from PM_UNBOUND_FIRST on it binds
 * nothing. */
static const char *const sPadRawFallback[PM_UNBOUND_FIRST] =
{
    "b0", "b1", "b2", "b3", "b4", "b5",
    "b6", "b7", "b8", "b9",
    "h0u", "h0d", "h0l", "h0r",
    "a0+", "a1-",
    "a2+", "a3-",
};
extern const char *portJoyPortBindName(s32 i); /* joy_port.c action names, for the order check */
extern void portJoyPortPadDown(s32 pad, s32 act);
#define PM_PORT_NEXT_INV 8 /* joy_port.c PORTACT_NEXT_INV - "next gadget", also DEF_PORT(8) below */
#define PM_PORT_CLASSIC_TOGGLE 20 /* joy_port.c PORTACT_CLASSIC_TOGGLE (order checked at init) */

/* A port-mode pad's N64-only rows (A, B, L, C buttons, D-pad) press nothing,
 * everywhere including menus, so one button never does both an N64 and a port
 * action; Z, R and START stay because the port's Fire, Aim and Pause rows are
 * those entries. */
static u16 inputPortModeN64Drop(int idx)
{
    extern s32 portInputPadIsN64(s32 slot);     /* below */

    if (idx < 0 || idx >= INPUT_MAX_PADS || portInputPadIsN64(idx))
    {
        return 0;
    }
    return (u16) (CONT_A | CONT_B | CONT_L | CONT_E | CONT_D | CONT_C | CONT_F
                  | CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT);
}
static const u16 sPadMapMask[PM_STICK_X] =
{
    CONT_A, CONT_B, CONT_G, CONT_START, CONT_L, CONT_R,
    CONT_E, CONT_D, CONT_C, CONT_F,
    CONT_UP, CONT_DOWN, CONT_LEFT, CONT_RIGHT,
};
typedef struct
{
    char kind;  /* 0 = unset, 'b' button, 'a' axis, 'h' hat */
    s16 idx;
    s16 dir;    /* axis: +1/-1; hat: SDL_HAT_* mask */
} PadMapEntry;
static PadMapEntry s_PadMap[INPUT_MAX_PADS][PM_COUNT];
static int s_PadMapCustom[INPUT_MAX_PADS];
/* ALTERNATE inputs per control: padN_<control>=tok1,tok2[,tok3,tok4]; the first
 * token is s_PadMap (sticks/look use only it), the rest live here and OR into
 * the digital rows (N64 buttons, digital movement, Port actions). */
#define PM_MAX_ALT 3
static PadMapEntry s_PadMapAlt[INPUT_MAX_PADS][PM_COUNT][PM_MAX_ALT];

/* First value ever polled from a slot's axis = its REST position (see
 * inputEntryDown). Cleared when the slot's device changes. */
static s16 s_PadAxisRest[INPUT_MAX_PADS][32];
static u32 s_PadAxisRestSeen[INPUT_MAX_PADS];

static void inputAxisRestForget(int idx)
{
    s_PadAxisRestSeen[idx] = 0;
}

static int inputAxisTriggerByMapping(int idx, int axis); /* -1 unknown, 0 stick, 1 trigger */
static int inputAxisTriggerHalf(int idx, int axis);      /* 1 = a trigger on HALF of this axis */

static s32 inputAxisRest(int idx, int axis, s32 now)
{
    if (idx < 0 || idx >= INPUT_MAX_PADS || axis < 0 || axis >= 32)
    {
        return 0; /* unknown: behave like a stick */
    }
    if ((s_PadAxisRestSeen[idx] & (1u << axis)) == 0)
    {
        /* Ask the SDL mapping first: a mapped pad knows which axes are
         * triggers, so sticks rest at 0 and triggers at their released end
         * regardless of the first reading (a stick held at first poll was
         * otherwise misfiled as a trigger); the first-poll guess is for
         * unmapped pads. */
        const int trig = inputAxisTriggerByMapping(idx, axis);

        if (trig == 0)
        {
            now = 0;
        }
        else if (trig == 1 && now > -24000 && now < 24000 && !inputAxisTriggerHalf(idx, axis))
        {   /* a half-axis or shared trigger ("+a2" / "-a2") rests at the
             * centre: forcing -32768 read it as half pulled at rest */
            now = -32768;
        }
        s_PadAxisRest[idx][axis] = (s16) now;
        s_PadAxisRestSeen[idx] |= 1u << axis;
        if (now <= -24000 || now >= 24000)
        {
            /* say so in the run log: which axes the trigger point governs
               is the one thing that cannot be read off the config */
            portConLogf("ge: input: player %d axis %d rests at %d - trigger, "
                            "gates at trigger_threshold %d (%d%% of the pull)\n",
                    idx + 1, axis, now, s_OptTrigThreshold,
                    (s_OptTrigThreshold * 100 + 16383) / 32767);
        }
    }
    return s_PadAxisRest[idx][axis];
}

/* say so in the run log: swap_sticks is an escape hatch, so "did it take?"
 * is the first question asked when the sticks still feel wrong */
static void inputSwapLog(int slot)
{
    portConLogf("ge: input: player %d swap_sticks=%d - Move is the %s stick, "
                    "Look (with its C buttons) the %s\n",
            slot + 1, s_PadSwapOpt[slot],
            s_PadSwapOpt[slot] ? "RIGHT" : "left",
            s_PadSwapOpt[slot] ? "LEFT" : "right");
}

/* SWAP STICKS ([input] padN_swap_sticks; the launcher's Controls tab, and the "swap sticks p1"
 * Extended Options row for slot 0): per pad, the Move and Look sticks trade places.
 *
 * Applied at the AXIS READ, not on the finished OSContPad, so one decision
 * moves the Move rows, the Look rows, the raw/calibrated stick modes and the
 * right-stick C emulation together (the C stick must follow the Look stick, or
 * the watch would page-flip from the stick driving its cursor). The
 * control-style swap (s_PadSwapSticks, 2.2 / 2.4) is separate, later, and
 * composes with this.
 *
 * Map path: the swap exchanges axis indices between padN_stick_x/y and
 * padN_look_x/y. A pad with no axis-bound Look stick (raphnet, 8BitDo 64) is
 * left alone; other axes (triggers, hats) pass through. Each row keeps its own
 * direction sign, so an inverted-axis binding stays with its role.
 */
static int inputMapAxis(s32 idx, int axis)
{
    const PadMapEntry *sx;
    const PadMapEntry *sy;
    const PadMapEntry *lx;
    const PadMapEntry *ly;

    if (!s_PadSwapOpt[idx])
    {
        return axis;
    }
    sx = &s_PadMap[idx][PM_STICK_X];
    sy = &s_PadMap[idx][PM_STICK_Y];
    lx = &s_PadMap[idx][PM_LOOK_X];
    ly = &s_PadMap[idx][PM_LOOK_Y];
    if (sx->kind != 'a' || sy->kind != 'a' || lx->kind != 'a' || ly->kind != 'a')
    {
        return axis;
    }
    if (axis == sx->idx) return lx->idx;
    if (axis == lx->idx) return sx->idx;
    if (axis == sy->idx) return ly->idx;
    if (axis == ly->idx) return sy->idx;
    return axis;
}

/* One button / axis-direction / hat token held right now? Axes whose first poll
 * sat at an extreme are triggers and gate on trigger_threshold measured from
 * their rest end; sticks and other axes gate at 0x4000. */
static int inputEntryDown(SDL_Joystick *joy, int idx, const PadMapEntry *e)
{
    s32 v, rest;
    int ax;
    switch (e->kind)
    {
    case 'b':
        return SDL_JoystickGetButton(joy, e->idx);
    case 'a':
        ax = inputMapAxis(idx, e->idx);   /* swap_sticks */
        v = SDL_JoystickGetAxis(joy, ax);
        rest = inputAxisRest(idx, ax, v);
        if (rest <= -24000 && e->dir > 0)
        {
            return (v - rest) * 32767 / (32767 - rest) > s_OptTrigThreshold;
        }
        if (rest >= 24000 && e->dir < 0)
        {
            return (rest - v) * 32767 / (rest + 32768) > s_OptTrigThreshold;
        }
        return (e->dir > 0) ? (v > 0x4000) : (v < -0x4000);
    case 'h':
        return (SDL_JoystickGetHat(joy, e->idx) & e->dir) != 0;
    default:
        return 0;
    }
}

/* A mapped SDL controller with no custom map gets s_PadMap seeded from its own
 * SDL bindings (inputDefaultTokens), so the default layout lands on the right
 * physical buttons on any brand; seeded maps are not "custom" and use the same
 * custom-map path. */
static int s_PadMapSeeded[INPUT_MAX_PADS];
static const char *inputDefaultTokens(SDL_GameController *c, char tok[][16]);
static const char *inputDefaultTokensForSlot(int slot, SDL_GameController *c, char tok[][16]);
static void inputLayoutTagSlot(int slot);
static void inputSetMapList(int slot, int i, const char *value, int markCustom);
static int inputParseMapToken(const char *v, PadMapEntry *e);
/* what the button is called on the pad in your hands: ONE table, defined far
 * below next to the bind list that displays it, used by the launcher probe
 * up here as well - see the block comment on inputPadStyle */
enum { PADSTYLE_XBOX, PADSTYLE_PS, PADSTYLE_NINTENDO };
static int inputPadStyle(SDL_GameController *gc);
static const char *inputPadButtonName(int style, SDL_GameControllerButton b);
static const char *inputPadAxisName(int style, SDL_GameControllerAxis a, int dir);

/* "tok1,tok2,...": first = primary, the rest = alternates; "-" = unbound.
 * markCustom = from midas.ini (the launcher wrote it) vs the built-in seed. */
static void inputSetMapList(int slot, int i, const char *value, int markCustom)
{
    char buf[64];
    char *tok;
    char *next;
    int n = 0;
    int a;

    SDL_strlcpy(buf, value, sizeof(buf));
    s_PadMap[slot][i].kind = 0;
    for (a = 0; a < PM_MAX_ALT; a++)
    {
        s_PadMapAlt[slot][i][a].kind = 0;
    }
    for (tok = buf; tok != NULL; tok = next)
    {
        PadMapEntry e;
        next = SDL_strchr(tok, ',');
        if (next != NULL)
        {
            *next++ = '\0';
        }
        while (*tok == ' ') tok++;
        if (*tok == '\0')
        {
            continue;
        }
        if (inputParseMapToken(tok, &e))
        {
            if (n == 0)
            {
                s_PadMap[slot][i] = e;
            }
            else if (n - 1 < PM_MAX_ALT)
            {
                s_PadMapAlt[slot][i][n - 1] = e;
            }
            n++;
            if (markCustom) s_PadMapCustom[slot] = 1;
        }
        else if (tok[0] == '-')
        {
            /* explicitly unbound (launcher Delete / "Unbind all"): still a
             * custom map - an all-"-" slot must NOT fall back to the
             * built-in layout */
            if (markCustom) s_PadMapCustom[slot] = 1;
        }
    }
}

/* the control's primary token OR any of its alternates */
static int inputControlDown(SDL_Joystick *joy, int idx, int i)
{
    int a;
    if (inputEntryDown(joy, idx, &s_PadMap[idx][i]))
    {
        return 1;
    }
    for (a = 0; a < PM_MAX_ALT; a++)
    {
        if (inputEntryDown(joy, idx, &s_PadMapAlt[idx][i][a]))
        {
            return 1;
        }
    }
    return 0;
}

/* Per-slot stick calibration (midas.ini padN_calib = 16 ints from the launcher
 * wizard): 8 measured raw full deflections (E,NE,N,NW,W,SW,S,SE) map
 * piecewise-linearly onto the ideal N64 octagon (85 cardinal, 65/65 diagonal),
 * solved per sector. */
static s16 s_PadCalib[INPUT_MAX_PADS][8][2];
static int s_PadCalibSet[INPUT_MAX_PADS];

/* Per-slot stick mode (midas.ini padN_stickmode):
 *   scaled     (default) deadzone-rescale to 85 cardinal - generic pads
 *   raw        1:1 passthrough, axis/256 - raphnet-class N64 adapters report
 *              the real stick in N64 units; no deadzone, diagonals stay true
 *   calibrated the 8-point octagon wizard map (needs padN_calib) */
/* Stick mode is derived from the device, not user-selectable: raphnet-class
 * adapters (VID 0x289B) report true N64 units and run raw 1:1, other pads run
 * scaled, and calibration data selects calibrated. The raw paths clamp so no
 * device can wrap the s8. */
enum { STICKMODE_SCALED, STICKMODE_RAW, STICKMODE_CALIBRATED };
static int s_PadStickMode[INPUT_MAX_PADS];
static const char *const sStickModeName[] = { "scaled", "raw 1:1", "calibrated" };
static const s16 sCalibIdeal[8][2] =
{
    { 85, 0 }, { 65, 65 }, { 0, 85 }, { -65, 65 },
    { -85, 0 }, { -65, -65 }, { 0, -85 }, { 65, -65 },
};

static void inputCalibApply(int idx, s32 rx, s32 ry, OSContPad *pad)
{
    int i;

    if ((float) rx * rx + (float) ry * ry
        < (float) s_OptDeadzone * s_OptDeadzone)
    {
        return; /* radial deadzone: keep the merged keyboard value */
    }

    for (i = 0; i < 8; i++)
    {
        s32 c1x = s_PadCalib[idx][i][0];
        s32 c1y = s_PadCalib[idx][i][1];
        s32 c2x = s_PadCalib[idx][(i + 1) & 7][0];
        s32 c2y = s_PadCalib[idx][(i + 1) & 7][1];
        float det = (float) c1x * c2y - (float) c1y * c2x;
        float a, b, s;

        if (det > -1.0f && det < 1.0f)
        {
            continue; /* degenerate sector (bad measurement) */
        }
        a = ((float) rx * c2y - (float) ry * c2x) / det;
        b = ((float) ry * c1x - (float) rx * c1y) / det;
        if (a < 0.0f || b < 0.0f)
        {
            continue; /* not this sector */
        }
        s = a + b;
        if (s > 1.0f)
        {
            a /= s;
            b /= s;
        }
        pad->stick_x = (s8)(a * sCalibIdeal[i][0] + b * sCalibIdeal[(i + 1) & 7][0]);
        pad->stick_y = (s8)(a * sCalibIdeal[i][1] + b * sCalibIdeal[(i + 1) & 7][1]);
        return;
    }
}

static int inputParseMapToken(const char *v, PadMapEntry *e)
{
    e->kind = 0;
    e->dir = 0;   /* a button has no direction: defined, so the rebind dedupe can compare it */
    if (v[0] == 'b')
    {
        e->kind = 'b';
        e->idx = (s16) SDL_atoi(v + 1);
        return 1;
    }
    if (v[0] == 'a')
    {
        size_t len = SDL_strlen(v);
        e->kind = 'a';
        e->idx = (s16) SDL_atoi(v + 1);
        e->dir = (len > 1 && v[len - 1] == '-') ? -1 : 1;
        return 1;
    }
    if (v[0] == 'h')
    {
        char d = v[SDL_strlen(v) - 1];
        e->kind = 'h';
        e->idx = (s16) SDL_atoi(v + 1);
        e->dir = (d == 'u') ? SDL_HAT_UP : (d == 'd') ? SDL_HAT_DOWN
               : (d == 'l') ? SDL_HAT_LEFT : SDL_HAT_RIGHT;
        return 1;
    }
    return 0;
}

/* ---- state -------------------------------------------------------------- */

/* raphnet.c: the adapter's vendor-HID RAW path (true N64 poll data,
 * bypassing the OS joystick stack + its calibration entirely) */
extern s32 portRaphnetAvailable(void);
extern const char *portRaphnetName(void);
extern s32 portRaphnetPoll(s32 chan, u16 *btn, s8 *x, s8 *y);
extern s32 portRaphnetPeek(s32 chan);   /* the same answer without making the channel hot */
extern void portRaphnetProbeMode(void); /* a launcher probe: never suspend/resume the adapter */

static SDL_GameController *s_Pads[INPUT_MAX_PADS];
static SDL_Joystick *s_Joys[INPUT_MAX_PADS]; /* raw fallback: DInput sticks
                                              * without a controller mapping */
static int s_PadRaphnet[INPUT_MAX_PADS];     /* 0 = no, else RAW channel+1 */
static int s_PadHasRumble[INPUT_MAX_PADS];
/* Which physical device each slot holds, so hotplug only closes the slot whose
 * device vanished and other players keep their pads mid-match. Valid only while
 * the slot is occupied. */
static SDL_JoystickID s_PadInstance[INPUT_MAX_PADS];

static int inputSlotBusy(int slot)
{
    return s_Pads[slot] != NULL || s_Joys[slot] != NULL || s_PadRaphnet[slot] != 0;
}

/* inputAxisRest: is raw joystick axis `axis` one of this pad's TRIGGERS
 * according to its SDL mapping? 1 yes, 0 no (a stick or anything else the
 * mapping names), -1 no mapping to ask - a raw joystick, or an axis the
 * mapping does not mention at all. */
static int inputAxisTriggerByMapping(int idx, int axis)
{
    SDL_GameController *gc = (idx >= 0 && idx < INPUT_MAX_PADS) ? s_Pads[idx] : NULL;
    int a;
    int named = 0;

    if (gc == NULL)
    {
        return -1;
    }
    for (a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++)
    {
        SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForAxis(gc, (SDL_GameControllerAxis) a);

        if (b.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && b.value.axis == axis)
        {
            if (a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
            {
                return 1;
            }
            named = 1;
        }
    }
    return named ? 0 : -1;
}

/* inputAxisRest: 1 = the mapping puts a trigger on HALF of raw axis `axis`
 * ("lefttrigger:+a2,righttrigger:-a2", one centred axis for both), which
 * rests at 0, not at an end. The bind API carries no half, so ask the
 * mapping string. */
static int inputAxisTriggerHalf(int idx, int axis)
{
    static const char *const keys[2] = { ",lefttrigger:", ",righttrigger:" };
    SDL_GameController *gc = (idx >= 0 && idx < INPUT_MAX_PADS) ? s_Pads[idx] : NULL;
    char *map = (gc != NULL) ? SDL_GameControllerMapping(gc) : NULL;
    int half = 0;
    int k;

    for (k = 0; map != NULL && k < 2 && !half; k++)
    {
        const char *p = SDL_strstr(map, keys[k]);

        if (p != NULL)
        {
            p += SDL_strlen(keys[k]);
            half = (p[0] == '+' || p[0] == '-') && p[1] == 'a' && p[2] >= '0' && p[2] <= '9'
                && SDL_atoi(p + 2) == axis;
        }
    }
    SDL_free(map);
    return half;
}

/* raphnet-class N64 adapter seen through SDL: GUID chars 8-11 = VID 0x289B
 * little-endian (the same test the auto pass uses to skip the RAW path's
 * twin), or a name that says so */
static int inputDeviceIsRaphnetClass(int jidx)
{
    char g[40];
    const char *nm = SDL_JoystickNameForIndex(jidx);

    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(jidx), g, sizeof(g));
    if (SDL_strlen(g) >= 12 && SDL_strncasecmp(g + 8, "9b28", 4) == 0)
    {
        return 1;
    }
    if (nm != NULL)
    {
        char low[128];
        int i;
        SDL_strlcpy(low, nm, sizeof(low));
        for (i = 0; low[i] != '\0'; i++)
        {
            low[i] = (char) SDL_tolower((unsigned char) low[i]);
        }
        if (SDL_strstr(low, "raphnet") != NULL)
        {
            return 1;
        }
    }
    return 0;
}

/* the one place a slot's stick mode is decided (see STICKMODE_*) */
static void inputDeriveStickMode(int slot, int raphnetClass)
{
    s_PadStickMode[slot] = s_PadCalibSet[slot] ? STICKMODE_CALIBRATED
                         : raphnetClass        ? STICKMODE_RAW
                                               : STICKMODE_SCALED;
    portConLogf("ge: input: player %d stick mode = %s (%s)\n", slot + 1,
            sStickModeName[s_PadStickMode[slot]],
            s_PadCalibSet[slot] ? "padN_calib present" : raphnetClass ? "raphnet-class adapter" : "generic controller");
}

/* raw 1:1: the adapter reports N64 units x256 across the SDL range - /256
 * recovers them; clamp so a full-range device can never wrap the s8 */
static s8 inputRawUnits(s32 axis)
{
    s32 v = axis / 256;
    if (v > 127) v = 127;
    if (v < -127) v = -127;
    return (s8) v;
}

static int inputSlotForInstance(SDL_JoystickID id)
{
    int slot;

    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        if ((s_Pads[slot] != NULL || s_Joys[slot] != NULL) && s_PadInstance[slot] == id)
        {
            return slot;
        }
    }
    return -1;
}

/* GE_ALLOWANYPAD=1: give a player slot to ANY device SDL lists, as the
 * port originally did (see inputDeviceIsGameInput). */
static int inputAllowAnyPad(void)
{
    static int e = -1;

    if (e < 0)
    {
        const char *v = SDL_getenv("GE_ALLOWANYPAD");
        e = (v != NULL && v[0] != '0');
    }
    return e;
}

/* Is this device actually game input? A mapped controller always is, an
 * unmapped one must report a known joystick type (so e.g. Hall-effect keyboards
 * exposing a HID gamepad don't steal a slot); an explicit padN=<guid> pin still
 * forces any device. */
static int inputDeviceIsGameInput(int jidx)
{
    if (SDL_IsGameController(jidx))
    {
        return 1;
    }
#if SDL_VERSION_ATLEAST(2, 0, 6)
    return SDL_JoystickGetDeviceType(jidx) != SDL_JOYSTICK_TYPE_UNKNOWN;
#else
    return 1;
#endif
}
/* TWIN STICK: the right stick in N64 units, handed to the game's 2.x second-pad
 * read through joy_port.c. The mute and swap latches are time-based deadlines
 * re-armed every tick the game uses the stick, so they expire on their own in
 * menus at any tick rate. */
#define PORT_TWIN_LATCH_MS 150 /* ~9 ticks at 60Hz - the old 8-poll feel */
static s8  s_PadRightX[INPUT_MAX_PADS];    /* the SECOND-PAD stick (Look, or Move when swapped) */
static s8  s_PadRightY[INPUT_MAX_PADS];
static int s_PadHasRight[INPUT_MAX_PADS];  /* 1 = the last merge saw an analog right stick */
static Uint32 s_PadRightCMute[INPUT_MAX_PADS];  /* deadline: C emulation muted until then */
static Uint32 s_PadSwapSticks[INPUT_MAX_PADS];  /* deadline: Look on pad 1 / Move as the second pad (2.2 / 2.4) */

/** 1 while a twin-stick latch deadline is still in the future (0 = never armed). */
static int inputLatchArmed(Uint32 deadline)
{
    return deadline != 0 && (Sint32) (deadline - SDL_GetTicks()) > 0;
}

/* Move / Look in N64 units for one merge; the style swap is applied once,
 * at the end, for every backend (mapped / custom / DInput). */
static void inputTwinCommit(s32 idx, OSContPad *pad, s32 mx, s32 my, s32 lx, s32 ly, int stickWritten)
{
    if (inputLatchArmed(s_PadSwapSticks[idx]))
    {
        s32 t;
        t = mx; mx = lx; lx = t;
        t = my; my = ly; ly = t;
    }
    if (!stickWritten)
    {
        if (mx != 0) pad->stick_x = (s8) mx;
        if (my != 0) pad->stick_y = (s8) my;
    }
    else if (inputLatchArmed(s_PadSwapSticks[idx]))
    {
        /* raw / calibrated Move modes wrote the stick already; the swap
         * still puts the Look stick on pad 1 */
        pad->stick_x = (s8) mx;
        pad->stick_y = (s8) my;
    }
    s_PadRightX[idx] = (s8) lx;
    s_PadRightY[idx] = (s8) ly;
    s_PadHasRight[idx] = 1;
}

/* A blanked pad's Look stick reads neutral too, otherwise the last deflection
 * keeps turning Bond for as long as the blank lasts. s_PadHasRight and the
 * latches are left alone, as portInputPadNeutral leaves them. */
static void inputPadBlankNeutral(s32 idx)
{
    s_PadRightX[idx] = 0;
    s_PadRightY[idx] = 0;
}
static bool s_InputInitDone = false;

static int s_MouseLocked = 0;
static int s_MouseFreed = 0;  /* smart lock: Escape handed the pointer back - hold off until a click */
static int s_ClickIsTouch = 0; /* the last mouse press was a touch tap */
/* FREE-POINTER WARP CHECK: on platforms that ignore warps of a free pointer
 * (Wayland, gamescope on the Deck) the menu cursor stays pinned at the clamp
 * edge, so detect an ignored warp and drive the free cursor from OS deltas from
 * then on. */
static int s_WarpPending = 0;
static int s_WarpFromX, s_WarpFromY, s_WarpToX, s_WarpToY;
static int s_FreeDelta = 0;   /* 1 = free pointer tracked by deltas (warps ignored here) */
static int s_FreeLastX, s_FreeLastY, s_FreeLastValid = 0;
static int s_MouseDX, s_MouseDY;     /* motion the CURRENT SIM TICK sees (portInputMouseTickConsume) */
/* Mouse motion accumulated across polls until the next sim tick consumes it,
 * since several polls can land between ticks in render-thread mode and keeping
 * only the last one made the mouse laggy. Nested pumps skip the poll, so loads
 * can't bank a jump. */
static int s_MouseAccDX, s_MouseAccDY;
static u32 s_MouseButtons;
static int s_MouseVX, s_MouseVY;     /* virtual absolute pos, window px:
                                      * OS cursor when free, integrated
                                      * deltas while captured (menus) */
static int s_MouseMoved;
static int s_MouseMovedAcc;   /* any poll since the last sim tick saw motion */
static int s_MouseMovedTick;  /* ...latched for the whole tick, see portInputMouseMovedThisTick */
/* Focus and park gates so the mouse lock isn't too aggressive: losing focus
 * releases capture, free-pointer warps need focus and the pointer over the
 * window, and the band-edge park only fires on movement back toward the band,
 * so the pointer can leave. */
static int s_PollX, s_PollY, s_PollPrevX, s_PollPrevY, s_PollValid;

/* ---- controller open/close/hotplug -------------------------------------- */

static void inputOpenController(int cidx, int jidx)
{
    s_Pads[cidx] = SDL_GameControllerOpen(jidx);
    if (s_Pads[cidx] == NULL)
    {
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 18)
    s_PadHasRumble[cidx] = SDL_GameControllerHasRumble(s_Pads[cidx]);
#else
    /* Windows: some pads report no haptics but rumble fine; assume typed
     * controllers can (PD does the same). */
    s_PadHasRumble[cidx] = SDL_GameControllerGetType(s_Pads[cidx]) != SDL_CONTROLLER_TYPE_UNKNOWN;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_GameControllerSetPlayerIndex(s_Pads[cidx], cidx);
#endif
    s_PadInstance[cidx] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_Pads[cidx]));
    portConLogf("ge: input: controller '%s' -> player %d (rumble %d)\n",
            SDL_GameControllerName(s_Pads[cidx]), cidx + 1, s_PadHasRumble[cidx]);

    if (!s_PadMapCustom[cidx])
    {
        /* seed the built-in layout from this device's own SDL bindings */
        char tok[PM_COUNT][16];
        int i;
        inputDefaultTokensForSlot(cidx, s_Pads[cidx], tok);
        for (i = 0; i < PM_COUNT; i++)
        {
            inputSetMapList(cidx, i, tok[i], 0);
        }
        s_PadMapSeeded[cidx] = 1;
    }
    else
    {
        /* a custom map never reaches the seed, so the n64 tag comes from the
         * layout the slot records (padN_layout) - and every applied layout IS
         * a custom map */
        inputLayoutTagSlot(cidx);
    }
}

static int s_PadIsN64[INPUT_MAX_PADS];   /* declared with the layouts below; the close clears it */

static void inputCloseController(int cidx)
{
    inputAxisRestForget(cidx); /* the next occupant's axes rest where they rest */
    if (s_Pads[cidx] != NULL)
    {
        portConLogf("ge: input: controller removed from player %d\n", cidx + 1);
        SDL_GameControllerClose(s_Pads[cidx]);
        s_Pads[cidx] = NULL;
        s_PadHasRumble[cidx] = 0;
    }
    if (s_PadMapSeeded[cidx])
    {
        /* the seeded layout was this device's; the next occupant reseeds */
        int i;
        for (i = 0; i < PM_COUNT; i++)
        {
            inputSetMapList(cidx, i, "-", 0);
        }
        s_PadMapSeeded[cidx] = 0;
        /* ...and so was the seed's n64 tag: a raw stick opening next never
         * reseeds, and would inherit the last pad's N64 class. A custom
         * map keeps its tag - it stays with the map it describes. */
        s_PadIsN64[cidx] = 0;
    }
    if (s_Joys[cidx] != NULL)
    {
        SDL_JoystickClose(s_Joys[cidx]);
        s_Joys[cidx] = NULL;
        s_PadHasRumble[cidx] = 0;
    }
    s_PadRaphnet[cidx] = 0;
    s_PadStickMode[cidx] = STICKMODE_SCALED; /* the next occupant derives its own */
}

/* Open device jidx into slot through the GameController layer when it has a
 * mapping, else as a raw joystick with the fixed fallback layout. A custom map
 * must not force the raw path: shipped layouts and bind-row names are resolved
 * through s_Pads. */
static void inputOpenDevice(int slot, int jidx)
{
    inputAxisRestForget(slot);
    inputDeriveStickMode(slot, inputDeviceIsRaphnetClass(jidx));
    if (SDL_IsGameController(jidx))
    {
        inputOpenController(slot, jidx);
        return;
    }
    s_Joys[slot] = SDL_JoystickOpen(jidx);
    if (s_Joys[slot] != NULL)
    {
        /* a custom-mapped pad lands here too (raw button/axis tokens) -
         * it still rumbles, through the joystick handle (portInputRumble) */
#if SDL_VERSION_ATLEAST(2, 0, 18)
        s_PadHasRumble[slot] = SDL_JoystickHasRumble(s_Joys[slot]);
#else
        s_PadHasRumble[slot] = 1;
#endif
        s_PadInstance[slot] = SDL_JoystickInstanceID(s_Joys[slot]);
        portConLogf("ge: input: raw joystick '%s' -> player %d (rumble %d; %s)\n",
                SDL_JoystickName(s_Joys[slot]), slot + 1, s_PadHasRumble[slot],
                s_PadMapCustom[slot] ? "custom layout"
                                     : "no mapping; fallback layout - add it to gamecontrollerdb.txt for full control");
        if (s_PadMapCustom[slot])
        {
            inputLayoutTagSlot(slot);   /* as inputOpenController: the tag an apply
                                         * set survives the restart */
        }
    }
}

static void inputAssignSlotsEx(int full);   /* the claim below asks for a reassign */

/* ---- WHO IS PLAYER 1 ------------------------------------------------------
 *
 * Keyboard and mouse are always player 1: videoGetPad fills pad 0 from the
 * midas.ini binds every poll and the slot-0 controller is merged on top, so the
 * keyboard keeps working in every menu. That matters for a player whose
 * controller the defaults don't map: they can still reach rebinding.
 *
 * What this decides is WHICH PAD is player 1 when several are connected. The
 * first pad to press anything during the intro (the press that skips a logo)
 * takes slot 0; other pads keep their slots and the previous player-1 pad drops
 * to the first free slot (normally a swap).
 *
 *  - It is a RAW button scan, not the mapped N64 mask, so an exotic pad with no usable map can
 *    still claim player 1 and go rebind.
 *  - The move is done by requesting a reassign (pass 0 of inputAssignSlotsEx pins the claimer),
 *    not by swapping per-slot arrays. Identical controllers are told apart by SDL instance id.
 *  - The reassign runs from portInputP1ClaimWindow on the frontend tick, NEVER from the pad poll:
 *    reopening controllers under portInputPadMerge would pull its handle out from under it.
 *
 * Once per run, and never when midas.ini pins pad1. The intro is the only moment
 * with no match in progress, which is why slots are never renumbered later.
 */
static int s_P1Window;                      /* the intro is up: a press may claim slot 0 */
static int s_P1Claimed;                     /* settled for this run */
static int s_P1Device;                      /* 0 = keyboard and mouse, else pad slot + 1 */
static SDL_JoystickID s_P1Instance = -1;    /* the claimer, for pass 0 of the assignment */
static SDL_JoystickID s_P1Pending = -1;     /* claimed in the poll, reassigned on the menu tick */
static int s_P1PendingRaphnet;              /* ...or a RAW channel + 1 did (s_P1Pending = -2 then) */
static int s_RaphnetClaimChan = -1;         /* pass 1b hands this channel out first, once */
/* inputRaphnetRecheck: the next look, and how many more
 * probes of an adapter that is NOT open yet (a HID enumeration each) we
 * spend before waiting for the next hotplug ADDED */
static unsigned s_RaphnetRecheckAt;
static int s_RaphnetOpenTries = 5;

/** 0 = keyboard and mouse is Player 1; otherwise the pad slot that claimed it, + 1. */
s32 portInputP1Device(void)
{
    return s_P1Device;
}

/**
 * 1 = the keyboard and mouse must NOT drive player 1 right now: only when
 * p1_hybrid is off AND a pad actually owns player 1, so the only input device
 * is never muted. The caller (video.c) asks only while gameplay is live.
 */
s32 portInputP1KbmMuted(void)
{
    /* and the pad is still THERE: an unplugged claimer leaves s_P1Device set
     * with slot 0 empty, and that must not mute the only input left */
    return !s_OptP1Hybrid && s_P1Device != 0 && inputSlotBusy(0);
}

/** Anything at all held on this slot's device - buttons, hat, triggers. */
static int inputSlotAnyButtonDown(int slot)
{
    int i;

    if (slot < 0 || slot >= INPUT_MAX_PADS)
    {
        return 0;
    }
    if (s_PadRaphnet[slot] != 0)
    {   /* a RAW channel: the adapter's own poll, so raphnet buttons
             * can skip intros too */
        u16 btn = 0;

        return portRaphnetPoll(s_PadRaphnet[slot] - 1, &btn, NULL, NULL) == 1 && btn != 0;
    }
    if (s_Pads[slot] != NULL)
    {
        for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        {
            if (SDL_GameControllerGetButton(s_Pads[slot], (SDL_GameControllerButton) i))
            {
                return 1;
            }
        }
        if (SDL_GameControllerGetAxis(s_Pads[slot], SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384
            || SDL_GameControllerGetAxis(s_Pads[slot], SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384)
        {
            return 1;
        }
        return 0;
    }
    if (s_Joys[slot] != NULL)
    {
        const int n = SDL_JoystickNumButtons(s_Joys[slot]);

        for (i = 0; i < n; i++)
        {
            if (SDL_JoystickGetButton(s_Joys[slot], i))
            {
                return 1;
            }
        }
        for (i = 0; i < SDL_JoystickNumHats(s_Joys[slot]); i++)
        {
            if (SDL_JoystickGetHat(s_Joys[slot], i) != SDL_HAT_CENTERED)
            {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * Called from the pad poll for each slot. Only RECORDS the claim - see the
 * block comment for why the reassign cannot happen here.
 */
static void inputP1ClaimPoll(int slot)
{
    if (s_P1Claimed || !s_P1Window || s_P1Pending != -1)
    {
        return;
    }
    if (!inputSlotAnyButtonDown(slot))
    {
        return;
    }
    /* a RAW channel has no SDL instance: remember the channel instead, and
     * mark the pending claim with a value that is not -1 */
    s_P1PendingRaphnet = s_PadRaphnet[slot];
    s_P1Pending = s_PadRaphnet[slot] ? -2 : s_PadInstance[slot];
    portConLogf("ge: input: player 1 = the pad in slot %d (it pressed something during the intro)\n", slot + 1);
}

/**
 * The frontend says whether the intro is on screen, and this is where a claim
 * recorded by the poll is acted on. Closing the window settles the question:
 * nobody touched a pad, so player 1 is the keyboard and mouse it already was.
 */
static int inputSlotOpenToAuto(int slot, const u8 *claimed, int njoys); /* below: a pin to an ABSENT pad is open */

void portInputP1ClaimWindow(s32 open)
{
    s_P1Window = (open != 0);

    if (s_P1Pending != -1 && !s_P1Claimed)
    {
        const SDL_JoystickID want = s_P1Pending;
        const int rn = s_P1PendingRaphnet;
        int slot;

        s_P1Pending = -1;
        s_P1PendingRaphnet = 0;
        s_P1Claimed = 1;
        /* the pad that spoke IS player 1 for this session: the watch's
         * Control tab and the folder's sheet show it (p1_view), where they
         * would otherwise show the keyboard it shares slot 1 with. Live only,
         * like every pin the game makes. */
        s_OptP1View = 1;

        if (rn != 0)
        {   /* a RAW channel claimed: it lands in slot 0 by being handed out
             * FIRST in pass 1b of the reassign (s_RaphnetClaimChan), unless
             * slot 0 is pinned to something else */
            for (slot = 0; slot < INPUT_MAX_PADS; slot++)
            {
                if (s_PadRaphnet[slot] != rn)
                {
                    continue;
                }
                if (slot == 0)
                {
                    s_P1Device = 1;
                    return;
                }
                if (!inputSlotOpenToAuto(0, NULL, SDL_NumJoysticks()))
                {
                    portConLogf("ge: input: midas.ini pins pad1=%s, so the claiming pad stays in slot %d\n",
                            s_OptPadAssign[0], slot + 1);
                    return;
                }
                if (SDL_strncasecmp(s_OptPadAssign[slot], "raphnet", 7) == 0
                    && SDL_atoi(s_OptPadAssign[slot] + 7) == rn - 1)
                {   /* pinned to ITS slot: the reassign's pass 1 puts it straight
                                     * back there, so do not claim (or log) a move */
                    portConLogf("ge: input: pad%d=%s pins the claiming pad, so it stays in slot %d\n",
                            slot + 1, s_OptPadAssign[slot], slot + 1);
                    return;
                }
                s_RaphnetClaimChan = rn - 1;
                inputAssignSlotsEx(1);
                s_P1Device = 1;
                portConLogf("ge: input: moved raphnet ch%d to player 1\n", rn - 1);
                return;
            }
            return;
        }

        for (slot = 0; slot < INPUT_MAX_PADS; slot++)
        {
            /* an empty or raphnet slot keeps a stale (or zero) instance id
             * that a real pad's id can equal: occupied SDL slots only */
            if ((s_Pads[slot] == NULL && s_Joys[slot] == NULL) || s_PadInstance[slot] != want)
            {
                continue;
            }
            if (slot == 0)
            {
                s_P1Device = 1;
                return;   /* already player 1: nothing to move */
            }
            if (!inputSlotOpenToAuto(0, NULL, SDL_NumJoysticks()))
            {
                portConLogf("ge: input: midas.ini pins pad1=%s, so the claiming pad stays in slot %d\n",
                        s_OptPadAssign[0], slot + 1);
                return;
            }
            s_P1Instance = want;
            inputAssignSlotsEx(1);
            s_P1Device = 1;
            portConLogf("ge: input: moved it to player 1; the pad that was player 1 takes the first free slot (normally the one it left), the rest stay put\n");
            return;
        }
        return;
    }
    if (!s_P1Window && !s_P1Claimed)
    {
        s_P1Claimed = 1;   /* the intro is over and no pad spoke up */
        portConLogf("ge: input: player 1 = keyboard and mouse (no pad pressed anything during the intro)\n");
    }
}

/**
 * (Re)assign every device to an N64 pad slot per midas.ini pad1..pad4: GUID pins
 * claim first, "auto" slots take the rest in system order, "none" slots stay
 * free. Runs at init and on every hotplug add/remove.
 */
#define INPUT_MAX_DEVICES 32

/* A pin to a device that is not connected must not lock the slot: such a slot
 * is open to the auto passes and the intro claim (marked in s_PadPinFallback),
 * and the fallback is evicted when the pinned device arrives; "none" is never
 * open. */
static u8 s_PadPinFallback[INPUT_MAX_PADS];

static int inputPinIsAuto(int slot)
{
    return s_OptPadAssign[slot][0] == '\0' || SDL_strcasecmp(s_OptPadAssign[slot], "auto") == 0;
}

/* With DirectInput and WGI both off (WGI defaults off under the Steam
 * overlay) no Windows backend lists the raphnet's generic HID face, so "SDL
 * has shown the adapter" can never be true: probe the RAW path anyway
 * (raphnet.c throttles a miss, RN_RETRY_MS). */
static int inputRaphnetBlind(void)
{
    int blind = 0;

#ifdef _WIN32
    blind = !SDL_GetHintBoolean(SDL_HINT_DIRECTINPUT_ENABLED, SDL_TRUE)
#ifdef SDL_HINT_JOYSTICK_WGI
            && !SDL_GetHintBoolean(SDL_HINT_JOYSTICK_WGI, SDL_TRUE)
#endif
            ;
#endif
    return blind;
}

/** Is the device padN= names connected and free right now? `claimed` may be NULL. */
static int inputPinPresent(int slot, const u8 *claimed, int njoys)
{
    char want[48];
    char *hash;
    int jidx;
    int occ = 0;
    int total = 0;
    int avail = 0;

    SDL_strlcpy(want, s_OptPadAssign[slot], sizeof(want));
    if (SDL_strncasecmp(want, "raphnet", 7) == 0)
    {
        int seen = 0;

        /* the RAW probe costs a HID enumeration: only once SDL has shown the adapter */
        for (jidx = 0; jidx < njoys; jidx++)
        {
            if (inputDeviceIsRaphnetClass(jidx))
            {
                seen = 1;
            }
        }
        /* blind as pass 1b is: under Steam a pin pass 1 honoured read as
         * absent, and the intro claim took its slot */
        return (seen || inputRaphnetBlind()) && portRaphnetAvailable()
            && portRaphnetPoll(SDL_atoi(want + 7), NULL, NULL, NULL) == 1;
    }
    hash = SDL_strchr(want, '#');
    if (hash != NULL)
    {
        occ = SDL_atoi(hash + 1);
        *hash = '\0';
    }
    for (jidx = 0; jidx < njoys; jidx++)
    {
        char g[40];

        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(jidx), g, sizeof(g));
        if (SDL_strcasecmp(g, want) == 0)
        {
            total++;
            avail |= (claimed == NULL || !claimed[jidx]);
        }
    }
    /* "guid#n" with n or fewer of that model connected: the full pass 1
     * honours only the n-th, so the pin cannot be met and the slot must stay
     * open, not locked empty */
    return avail && total > occ;
}

/** 1 = an auto device (or the intro claim) may have this slot: no pin, or a pin to nothing. */
static int inputSlotOpenToAuto(int slot, const u8 *claimed, int njoys)
{
    if (inputPinIsAuto(slot))
    {
        return 1;
    }
    if (SDL_strcasecmp(s_OptPadAssign[slot], "none") == 0)
    {
        return 0;
    }
    return !inputPinPresent(slot, claimed, njoys);
}

/** An auto device just took `slot`: say so once if it is somebody's pinned slot. */
static void inputNoteFallback(int slot)
{
    if (!inputPinIsAuto(slot))
    {
        s_PadPinFallback[slot] = 1;
        portConLogf("ge: input: pad%d=%s is not connected - player %d is open to the other pads until it is\n",
                slot + 1, s_OptPadAssign[slot], slot + 1);
    }
}

/* full = 1: close every slot and reassign from scratch (init).
 * full = 0: leave occupied slots alone, only fill empty ones (hotplug). */
/**
 * 1 = device jidx is the Windows.Gaming.Input "shadow" of a pad another SDL
 * driver also presents (a hotplug race can list one pad twice). Detected via
 * the GUID's driver byte 14 ('w' = WGI) with matching vendor/product; real
 * identical pads share one driver, so never match.
 */
static int inputDeviceIsWgiShadow(int jidx, int njoys)
{
    const SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(jidx);
    const Uint16 vid = SDL_JoystickGetDeviceVendor(jidx);
    const Uint16 pid = SDL_JoystickGetDeviceProduct(jidx);
    int k;

    if (g.data[14] != 'w' || (vid == 0 && pid == 0))
    {
        return 0;
    }
    for (k = 0; k < njoys; k++)
    {
        if (k != jidx && SDL_JoystickGetDeviceGUID(k).data[14] != 'w'
            && SDL_JoystickGetDeviceVendor(k) == vid && SDL_JoystickGetDeviceProduct(k) == pid)
        {
            return 1;
        }
    }
    return 0;
}

/* the slot inputAssignPin just pinned: its old occupant must NOT be kept */
static int s_PinFreshSlot = -1;

static void inputAssignSlotsEx(int full)
{
    u8 claimed[INPUT_MAX_DEVICES] = {0};
    int njoys = SDL_NumJoysticks();
    int slot;
    int jidx;
    int tier;
    /* Remember who held each slot before a full pass, so unpinned slots aren't
     * re-dealt in SDL device order (which moved player 1's pad and swapped
     * identical pads' "#n" pins after a reconnect). */
    SDL_JoystickID prevInst[INPUT_MAX_PADS];
    int prevRaph[INPUT_MAX_PADS];

    if (njoys > INPUT_MAX_DEVICES)
    {
        njoys = INPUT_MAX_DEVICES;
    }
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        const int keep = full && slot != s_PinFreshSlot;

        prevInst[slot] = (keep && (s_Pads[slot] != NULL || s_Joys[slot] != NULL)) ? s_PadInstance[slot] : -1;
        prevRaph[slot] = keep ? s_PadRaphnet[slot] : 0;
    }

    /* a WGI shadow (inputDeviceIsWgiShadow) is nobody's controller: put out
     * the slot that took it before its real twin arrived, and keep every
     * pass below from handing it out */
    for (jidx = 0; jidx < njoys; jidx++)
    {
        if (!inputDeviceIsWgiShadow(jidx, njoys))
        {
            continue;
        }
        claimed[jidx] = 1;
        for (slot = 0; !full && slot < INPUT_MAX_PADS; slot++)
        {
            if ((s_Pads[slot] != NULL || s_Joys[slot] != NULL)
                && s_PadInstance[slot] == SDL_JoystickGetDeviceInstanceID(jidx))
            {
                portConLogf("ge: input: '%s' on player %d is the same pad seen twice (Windows.Gaming.Input beside another driver) - dropped\n",
                            SDL_JoystickNameForIndex(jidx) ? SDL_JoystickNameForIndex(jidx) : "?", slot + 1);
                inputCloseController(slot);
            }
        }
    }

    if (full)
    {
        for (slot = 0; slot < INPUT_MAX_PADS; slot++)
        {
            inputCloseController(slot);
            s_PadPinFallback[slot] = 0;
        }
    }
    else
    {
        /* whatever the surviving slots already hold is off the table */
        for (slot = 0; slot < INPUT_MAX_PADS; slot++)
        {
            if (s_Pads[slot] == NULL && s_Joys[slot] == NULL)
            {
                continue;
            }
            for (jidx = 0; jidx < njoys; jidx++)
            {
                if (SDL_JoystickGetDeviceInstanceID(jidx) == s_PadInstance[slot])
                {
                    claimed[jidx] = 1;
                    break;
                }
            }
        }
    }

    /* pass 0: the pad that claimed player 1 during the intro takes slot 0.
     * Before the GUID pins on purpose - it only runs when slot 0 is on
     * "auto", so an explicit pad1= in midas.ini still wins. */
    if (s_P1Instance != -1 && !inputSlotBusy(0) && inputSlotOpenToAuto(0, claimed, njoys))
    {
        for (jidx = 0; jidx < njoys; jidx++)
        {
            if (!claimed[jidx] && SDL_JoystickGetDeviceInstanceID(jidx) == s_P1Instance)
            {
                claimed[jidx] = 1;
                inputNoteFallback(0);   /* a pinned-but-absent slot 0: the pin
                                         * evicts it on arrival */
                inputOpenDevice(0, jidx);
                break;
            }
        }
    }

    /* pass 1: explicit GUID pins */
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        char want[48];
        char *hash;
        int occ = 0;
        int seen = 0;

        if (inputSlotBusy(slot))
        {
            if (s_PadPinFallback[slot] && inputPinPresent(slot, claimed, njoys))
            {   /* the pad this slot is pinned to has arrived, and an auto
                 * device is sitting in it: the pin wins. From scratch, because
                 * the evicted device needs a slot of its own too. */
                portConLogf("ge: input: pad%d=%s is connected now - reassigning so it takes player %d\n",
                        slot + 1, s_OptPadAssign[slot], slot + 1);
                inputAssignSlotsEx(1);
                return;
            }
            continue; /* incremental pass: this slot is already live */
        }
        s_PadPinFallback[slot] = 0;
        SDL_strlcpy(want, s_OptPadAssign[slot], sizeof(want));
        if (want[0] == '\0' || SDL_strcasecmp(want, "auto") == 0 || SDL_strcasecmp(want, "none") == 0)
        {
            continue;
        }
        if (SDL_strncasecmp(want, "raphnet", 7) == 0)
        {
            /* padN=raphnet<chan>: the vendor-HID RAW path, no SDL device */
            if (portRaphnetAvailable())
            {
                s_PadRaphnet[slot] = SDL_atoi(want + 7) + 1;
                portConLogf("ge: input: raphnet RAW ch%d -> player %d\n",
                        s_PadRaphnet[slot] - 1, slot + 1); /* 0-based, as the auto line and padN=raphnet<chan> */
                inputDeriveStickMode(slot, 1); /* native N64 units: raw (or calibrated) */
            }
            continue;
        }
        hash = SDL_strchr(want, '#');
        if (hash != NULL)
        {
            occ = SDL_atoi(hash + 1);
            *hash = '\0';
        }
        for (jidx = 0; prevInst[slot] != -1 && jidx < njoys; jidx++)
        {   /* the pad that held this pin a moment ago still matches it: it
                     * keeps the slot, whatever position the list gives it now */
            char g[40];

            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(jidx), g, sizeof(g));
            if (!claimed[jidx] && SDL_JoystickGetDeviceInstanceID(jidx) == prevInst[slot]
                && SDL_strcasecmp(g, want) == 0)
            {
                claimed[jidx] = 1;
                inputOpenDevice(slot, jidx);
                break;
            }
        }
        if (inputSlotBusy(slot))
        {
            continue;
        }
        for (jidx = 0; jidx < njoys; jidx++)
        {
            char g[40];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(jidx), g, sizeof(g));
            if (SDL_strcasecmp(g, want) != 0)
            {
                continue;
            }
            if (!full)
            {
                /* Incremental (hotplug): the device list has been renumbered
                 * around surviving pads, so the empty pinned slot takes the
                 * first UNCLAIMED match (the pad that just came back) rather
                 * than the n-th occurrence. */
                if (!claimed[jidx])
                {
                    claimed[jidx] = 1;
                    inputOpenDevice(slot, jidx);
                    break;
                }
                continue;
            }
            if (seen++ == occ)
            {
                if (!claimed[jidx])
                {
                    claimed[jidx] = 1;
                    inputOpenDevice(slot, jidx);
                }
                break;
            }
        }
    }

    /* pass 1a (full only): every open auto slot takes back the pad it
     * held before, so a pin re-deals only the device it displaced. Not
     * raphnet faces (pass 1b owns those), and not slot 0 while an intro
     * claim is moving a raphnet channel into it. */
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        if (prevInst[slot] == -1 || inputSlotBusy(slot) || !inputSlotOpenToAuto(slot, claimed, njoys)
            || (slot == 0 && s_RaphnetClaimChan >= 0))
        {
            continue;
        }
        for (jidx = 0; jidx < njoys; jidx++)
        {
            if (!claimed[jidx] && SDL_JoystickGetDeviceInstanceID(jidx) == prevInst[slot]
                && !inputDeviceIsRaphnetClass(jidx))
            {
                inputNoteFallback(slot);
                claimed[jidx] = 1;
                inputOpenDevice(slot, jidx);
                break;
            }
        }
    }

    /* pass 1b: the raphnet RAW vendor-HID path, before the auto tiers, so an
     * unpinned adapter is not silently dropped as "not a game controller". The
     * HID probe only runs once SDL has shown a 0x289B device; one slot per
     * channel with a controller in it. */
    {
        int rseen = 0;

        for (jidx = 0; jidx < njoys; jidx++)
        {
            if (!claimed[jidx] && inputDeviceIsRaphnetClass(jidx))
            {
                rseen = 1;
            }
        }
        const int blind = inputRaphnetBlind();   /* DirectInput and WGI both off */

        if ((rseen || blind) && portRaphnetAvailable())
        {
            int chan;

            /* the RAW path owns the hardware: the joystick interfaces are off
             * the table, and are not reported as "ignoring" either */
            for (jidx = 0; jidx < njoys; jidx++)
            {
                if (inputDeviceIsRaphnetClass(jidx))
                {
                    claimed[jidx] = 1;
                }
            }
            for (chan = 0; chan < 2; chan++)
            {
                int pinned = 0;
                int c = chan;

                if (s_RaphnetClaimChan >= 0)
                {   /* the channel that claimed player 1 during the intro goes
                     * first, so it takes slot 0; the other follows */
                    c = (chan == 0) ? s_RaphnetClaimChan : (1 - s_RaphnetClaimChan);
                    if (chan == 1)
                    {
                        s_RaphnetClaimChan = -1;   /* once */
                    }
                }
                for (slot = 0; slot < INPUT_MAX_PADS; slot++)
                {
                    if (s_PadRaphnet[slot] == c + 1)
                    {
                        pinned = 1; /* an explicit padN=raphnet<chan> already has it */
                    }
                }
                if (pinned || portRaphnetPoll(c, NULL, NULL, NULL) != 1)
                {
                    continue;
                }
                {   /* the slot this channel held before a full pass, unless
                                     * it is the intro claimer on its way to slot 0 */
                    int prev = -1;

                    for (slot = 0; slot < INPUT_MAX_PADS && !(s_RaphnetClaimChan >= 0 && chan == 0); slot++)
                    {
                        if (prevRaph[slot] == c + 1 && !inputSlotBusy(slot) && inputSlotOpenToAuto(slot, claimed, njoys))
                        {
                            prev = slot;
                            break;
                        }
                    }
                    if (prev >= 0)
                    {
                        inputNoteFallback(prev);
                        s_PadRaphnet[prev] = c + 1;
                        inputDeriveStickMode(prev, 1); /* native N64 units */
                        portConLogf("ge: input: raphnet RAW ch%d -> player %d (auto, kept, '%s')\n",
                                c, prev + 1, portRaphnetName());
                        continue;
                    }
                }
                for (slot = 0; slot < INPUT_MAX_PADS; slot++)
                {
                    if (!inputSlotBusy(slot) && inputSlotOpenToAuto(slot, claimed, njoys))
                    {
                        inputNoteFallback(slot);
                        s_PadRaphnet[slot] = c + 1;
                        inputDeriveStickMode(slot, 1); /* native N64 units */
                        portConLogf("ge: input: raphnet RAW ch%d -> player %d (auto, '%s')\n",
                                c, slot + 1, portRaphnetName());
                        break;
                    }
                }
            }
        }
        else if (rseen)
        {
            /* Deliberately do NOT claim the SDL face when RAW didn't answer: on
             * Linux / Deck without the hidraw udev rule the DB-mapped face is
             * the only way the adapter works at all. */
            /* Log it, so a dropped adapter is visible in the log. */
            portConLogf("ge: input: a raphnet-class adapter is on the bus but its RAW vendor "
                    "interface did not answer - it cannot be used as an N64 pad. See the "
                    "'ge: raphnet:' lines above for the interfaces that were tried.\n");
        }
    }

    /* pass 2: auto slots take what is left, in system order (a raphnet's
     * joystick interface was already claimed by pass 1b). Tier 0 = real game
     * input, tier 1 = everything else (GE_ALLOWANYPAD only), so e.g. a
     * keyboard's HID gamepad face can't push a real pad down. */
    for (tier = 0; tier < 2; tier++)
    {
        if (tier == 1 && !inputAllowAnyPad())
        {
            break;
        }
        for (jidx = 0; jidx < njoys; jidx++)
        {
            if (claimed[jidx] || (inputDeviceIsGameInput(jidx) ? 0 : 1) != tier)
            {
                continue;
            }
            for (slot = 0; slot < INPUT_MAX_PADS; slot++)
            {
                if (s_Pads[slot] == NULL && s_Joys[slot] == NULL && s_PadRaphnet[slot] == 0
                    && inputSlotOpenToAuto(slot, claimed, njoys))
                {
                    inputNoteFallback(slot);
                    claimed[jidx] = 1;
                    inputOpenDevice(slot, jidx);
                    break;
                }
            }
        }
    }

    /* say what was skipped, with the midas.ini line that would force it in */
    if (!inputAllowAnyPad())
    {
        for (jidx = 0; jidx < njoys; jidx++)
        {
            if (!claimed[jidx] && !inputDeviceIsGameInput(jidx))
            {
                char g[40];
                SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(jidx), g, sizeof(g));
                portConLogf("ge: input: ignoring '%s' - not a game controller "
                                "(padN=%s in midas.ini [input], or GE_ALLOWANYPAD=1, forces it)\n",
                        SDL_JoystickNameForIndex(jidx), g);
            }
        }
    }
}

/* one line per topology change: who holds which slot right now */
static void inputLogSlots(const char *why)
{
    char line[512];
    int slot;

    SDL_snprintf(line, sizeof(line), "ge: input: slots after %s (%d SDL devices):", why, SDL_NumJoysticks());
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        char part[96];
        const char *nm = "-";
        const char *att = "";

        if (s_PadRaphnet[slot] != 0)
        {
            SDL_snprintf(part, sizeof(part), " p%d=raphnet%d", slot + 1, s_PadRaphnet[slot] - 1);
        }
        else
        {
            if (s_Pads[slot] != NULL)
            {
                nm = SDL_GameControllerName(s_Pads[slot]);
                att = SDL_GameControllerGetAttached(s_Pads[slot]) ? "" : " DETACHED";
            }
            else if (s_Joys[slot] != NULL)
            {
                nm = SDL_JoystickName(s_Joys[slot]);
                att = SDL_JoystickGetAttached(s_Joys[slot]) ? "" : " DETACHED";
            }
            if (nm == NULL)
            {
                nm = "?";
            }
            SDL_snprintf(part, sizeof(part), " p%d=%s%s%s", slot + 1,
                         (s_Pads[slot] != NULL || s_Joys[slot] != NULL) ? "'" : "", nm,
                         (s_Pads[slot] != NULL || s_Joys[slot] != NULL) ? "'" : "");
            SDL_strlcat(part, att, sizeof(part));
        }
        SDL_strlcat(line, part, sizeof(line));
    }
    /* THE PINS, on the same line: "why is my pad player 2" could not be
     * answered from a log because what midas.ini asked for was never printed */
    /* "pins", not "midas.ini": a press-a-button assign pins for the session and
     * is never saved (inputAssignPin), so these can differ from the file */
    SDL_strlcat(line, " | pins", sizeof(line));
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        char part[64];

        SDL_snprintf(part, sizeof(part), " pad%d=%s%s", slot + 1,
                     s_OptPadAssign[slot][0] ? s_OptPadAssign[slot] : "auto",
                     s_PadPinFallback[slot] ? "(absent)" : "");
        SDL_strlcat(line, part, sizeof(line));
    }
    fprintf(stderr, "%s\n", line);
}

/* ---- fixed PC keys (docs/port/menu-overhaul-spec.md 6) ------------------- */

/* Fixed PC keys: not remappable and never in the bind tables. Handled on the EVENT side because
 * they are edge-triggered (a held key polled at pad rate would fire many times).
 *
 *   Esc  in play: open the watch; watch up: close it; frontend: back (B). Read once per tick
 *        via portEscPressed() by src/game/front.c (menu_init) and src/game/bondview2.c.
 *   F1   mouse grab on/off - only the DEFAULT of [controls] mouse_unlock: wired here only while
 *        that bind is empty, or while the console / on-screen keyboard is typing
 *        (see inputFixedKeyDown).
 *   `    console open/close (port/src/port_console.c).
 */

static volatile s32 sEscLatched;
static volatile s32 sGrabToggleLatched;
/* The console key types a character too (see inputFixedKeyDown). Only
 * inputEventWatch touches these, on the one thread that pumps SDL events. */
static s32 sConSwallowAny;         /* 1 = no other key since the grave press: the next text is its own */
static SDL_Keycode sConSwallowKey; /* that key's unshifted ASCII char on this layout, 0 = off */

/** TRUE once per Escape press; consumed by the reader. */
s32 portEscPressed(void)
{
    s32 r = sEscLatched;
    sEscLatched = 0;
    return r;
}

static s32 inputGrabTogglePressed(void)
{
    /* the player's own key ([controls] mouse_unlock, default F1 - video.c), OR
     * the hard-wired F1 that only exists while that bind is empty, the
     * console is open or the on-screen keyboard is typing */
    extern s32 portVideoMouseUnlockPressed(void);
    s32 r = sGrabToggleLatched;

    sGrabToggleLatched = 0;
    if (portVideoMouseUnlockPressed())
    {
        r = 1;
    }
    return r;
}

/** Console open/close, including the SDL text-input session it needs. */
static void inputConsoleToggle(void)
{
    portConToggle();
    if (portConIsOpen())
    {
        SDL_StartTextInput();
    }
    else
    {
        SDL_StopTextInput();
    }
}

/** 1 = the event was a fixed key and must not travel any further. */
static int inputFixedKeyDown(const SDL_KeyboardEvent *key)
{
    const SDL_Scancode sc = key->keysym.scancode;
    const int repeat = key->repeat != 0;

    if (sc == SDL_SCANCODE_GRAVE)
    {
        {   /* Where this key types a digit or letter (Hungarian layout) it
             * only opens the console, so it can still be typed, and Escape
             * closes it. It never opens over the on-screen keyboard. */
            extern s32 portTextInputActive(void);   /* video.c */
            const SDL_Keycode gk = SDL_GetKeyFromScancode(SDL_SCANCODE_GRAVE);
            const int typesChar = (gk >= '0' && gk <= '9') || (gk >= 'a' && gk <= 'z');

            if (typesChar && portConIsOpen())
            {
                sConSwallowAny = 0;
                sConSwallowKey = 0;
                return 1;
            }
            if (typesChar && portTextInputActive())
            {
                sConSwallowAny = 0;
                return 0;
            }
        }
        /* Grave key only: the ISO key left of Z is an ordinary bindable key on
         * those layouts, so stealing it would break binds. A layout with no
         * grave key has no console; that is the lesser harm. */
        if (!repeat)
        {
            inputConsoleToggle();
        }
        /* Its character follows as the next SDL_TEXTINPUT ('`' US, '\' Italian,
         * '^' German, dead keys with the key after it), so swallow this key's
         * own text rather than a US character; repeats re-arm. */
        if (portConIsOpen())
        {
            const SDL_Keycode k = SDL_GetKeyFromScancode(SDL_SCANCODE_GRAVE);
            sConSwallowAny = 1;
            sConSwallowKey = (k > 0x20 && k < 0x7f) ? k : 0; /* non-ASCII: portConTextChar drops it anyway */
        }
        return 1;
    }
    sConSwallowAny = 0; /* another key went down: the next text is that key's */

    if (portConIsOpen())
    {
        switch (sc)
        {
        case SDL_SCANCODE_ESCAPE:    inputConsoleToggle();                return 1;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:  portConKey(PORTCON_KEY_ENTER);       return 1;
        case SDL_SCANCODE_BACKSPACE: portConKey(PORTCON_KEY_BACKSPACE);   return 1;
        case SDL_SCANCODE_PAGEUP:    portConKey(PORTCON_KEY_PAGEUP);      return 1;
        case SDL_SCANCODE_PAGEDOWN:  portConKey(PORTCON_KEY_PAGEDOWN);    return 1;
        case SDL_SCANCODE_HOME:      portConKey(PORTCON_KEY_HOME);        return 1;
        case SDL_SCANCODE_END:       portConKey(PORTCON_KEY_END);         return 1;
        case SDL_SCANCODE_UP:        portConKey(PORTCON_KEY_UP);          return 1;
        case SDL_SCANCODE_DOWN:      portConKey(PORTCON_KEY_DOWN);        return 1;
        case SDL_SCANCODE_F1:
            /* F1 frees / re-grabs the pointer with the console up whatever
             * [controls] mouse_unlock holds, because that bind's keys are
             * blanked with the keyboard while typing and F1 types nothing */
            if (!repeat)
            {
                sGrabToggleLatched = 1;
            }
            return 1;
        default: break;
        }
        return 1; /* console open: the keyboard is the prompt's, not the game's */
    }

    if (sc == SDL_SCANCODE_F1)
    {
        /* F1 is only the default mouse-unlock key: while [controls]
         * mouse_unlock holds a key, F1 is an ordinary bindable key. With that
         * bind empty F1 stays wired, so there is always a way to free the
         * pointer. */
        extern s32 portVideoMouseUnlockBound(void);
        extern s32 portTextInputActive(void);   /* video.c: the on-screen keyboard is typing */

        /* ...EXCEPT while the on-screen keyboard types: videoGetPad blanks the
         * keys then, this bind's with them, so F1 is the fixed toggle again -
         * the console-open rule above (F1 types nothing) */
        if (portVideoMouseUnlockBound() && !portTextInputActive())
        {
            return 0;
        }
        if (!repeat)
        {
            sGrabToggleLatched = 1;
        }
        return 1;
    }
    if (sc == SDL_SCANCODE_ESCAPE)
    {
        if (!repeat)
        {
            sEscLatched = 1;
        }
        return 1;
    }
    return 0;
}

/**
 * Hotplug: the fast3d frame pump (gfx_sdl_handle_events -> SDL_PollEvent)
 * generates these; a watch sees every event before that loop eats them.
 */
static int inputEventWatch(void *userdata, SDL_Event *event)
{
    (void)userdata;

    /* JOYDEVICE* fires for every stick (CONTROLLERDEVICE* only for mapped
     * ones), so it drives the slot reassign. ADDED also fires for devices
     * present at init; the reassign is idempotent,
     * so that is harmless. */
    switch (event->type)
    {
    case SDL_JOYDEVICEADDED:
        /* fill EMPTY slots only - a device arriving (or waking) must never
         * renumber players already in a match */
        {
            const char *nm = SDL_JoystickNameForIndex(event->jdevice.which); /* device INDEX here */
            portConLogf("ge: input: hotplug ADDED index %d '%s'\n", (int) event->jdevice.which, nm ? nm : "?");
        }
        s_RaphnetOpenTries = 5;   /* new hardware: the recheck may probe again */
        inputAssignSlotsEx(0);
        inputLogSlots("ADDED");
        break;
    case SDL_JOYDEVICEREMOVED:
        { /* jdevice.which is the INSTANCE id here: close just that slot,
           * then let an unclaimed device fill the hole it left */
            int gone = inputSlotForInstance((SDL_JoystickID) event->jdevice.which);

            portConLogf("ge: input: hotplug REMOVED instance %d -> %s\n", (int) event->jdevice.which,
                    gone >= 0 ? "player slot below" : "no slot held it");
            if (gone >= 0)
            {
                inputCloseController(gone);
            }
            inputAssignSlotsEx(0);
            inputLogSlots("REMOVED");
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        /* a tap is a left press SDL synthesises with no motion, and in
         * relative mode it never moves the cursor - so a tap must not capture
         * the pointer (portInputMouseUpdate). Whichever device pressed last. */
        s_ClickIsTouch = (event->button.which == SDL_TOUCH_MOUSEID);
        break;
    case SDL_MOUSEWHEEL:
        { /* "Mouse Wheel Up/Down" binds (video.c) */
            extern void videoMouseWheel(int dy);
            videoMouseWheel(event->wheel.y);
        }
        break;
    case SDL_KEYDOWN:
        inputFixedKeyDown(&event->key);
        break;
    case SDL_TEXTINPUT:
        if (portConIsOpen())
        {
            const char *t = event->text.text;
            if (sConSwallowAny)
            {
                t = ""; /* the grave key's own text, whatever the layout or modifier */
                sConSwallowAny = 0;
            }
            /* a dead grave key (German '^') types its char with the next key,
             * or twice on a quick close/reopen: drop it until another char */
            while (sConSwallowKey != 0 && *t != '\0')
            {
                if ((unsigned char) *t != sConSwallowKey)
                {
                    sConSwallowKey = 0;
                    break;
                }
                t++;
            }
            while (*t != '\0')
            {
                portConTextChar((unsigned char) *t++);
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

/* ---- init --------------------------------------------------------------- */

/* DirectInput is off by default ([input] dinput=1 restores it) because Steam's
 * input hook deadlocks dinput8 at startup, and other backends cover everything
 * we use. Must run BEFORE any SDL joystick init, probes included, so the
 * launcher's list matches. */
/* Per-backend switches for isolating backend hangs (e.g. under Steam Input):
 * midas.ini [input] hidapi / rawinput / wgi / xinput = 0|1, overridden by env
 * GE_HIDAPI / GE_RAWINPUT / GE_WGI / GE_XINPUT, which the launcher passes to
 * probe runs before the ini is parsed. */
static int s_OptHidapi = 1;
static int s_OptWgi = 1;
static int s_OptWgiExplicit = 0;   /* ini wgi= or GE_WGI given: the Steam default-off below yields */
static int s_OptXInput = 1;

#ifdef _WIN32
/* kernel32 without <windows.h> (its BOOL/u8-class names collide with
 * ultra64.h here). GetModuleHandle loads nothing - a presence test. */
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *name);
#endif

static int inputEnvOverride(const char *name, int cur)
{
    const char *e = getenv(name);
    return (e != NULL && e[0] != '\0') ? (e[0] != '0') : cur;
}

static void inputBackendHints(void)
{
    /* GE_DINPUT=1: the launcher's probe / capture / calibrate / rumble-test
     * runs happen BEFORE main.c parses midas.ini (otherwise e.g. an 8BitDo N64 in
     * DInput mode is invisible to GE Options) - the launcher passes its
     * DirectInput checkbox through the environment instead. */
    {
        const char *e = getenv("GE_DINPUT");
        if (e != NULL && e[0] != '\0' && e[0] != '0')
        {
            s_OptDInput = 1;
        }
    }
    if (!s_OptDInput)
    {
        SDL_SetHint(SDL_HINT_DIRECTINPUT_ENABLED, "0");
    }

    /* the other four backends (hidapi = PS4/PS5/Switch class, rawinput =
     * Xbox-class on SDL 2.30, wgi = Windows.Gaming.Input trigger rumble,
     * xinput). Defaults all ON = SDL's own; ini first, env last. */
    s_OptHidapi = inputEnvOverride("GE_HIDAPI", s_OptHidapi);
    s_OptRawInput = inputEnvOverride("GE_RAWINPUT", s_OptRawInput);
    if (getenv("GE_WGI") != NULL && getenv("GE_WGI")[0] != '\0')
    {
        s_OptWgiExplicit = 1;
    }
    s_OptWgi = inputEnvOverride("GE_WGI", s_OptWgi);
    s_OptXInput = inputEnvOverride("GE_XINPUT", s_OptXInput);
#ifdef _WIN32
    /* Steam's injected input hook deadlocks Windows.Gaming.Input init, and SDL
     * only uses WGI for Xbox trigger rumble, so WGI defaults OFF when the Steam
     * overlay is loaded; an explicit ini wgi= / GE_WGI wins. */
    if (!s_OptWgiExplicit && s_OptWgi && GetModuleHandleA("gameoverlayrenderer.dll") != NULL)
    {
        static int said;
        s_OptWgi = 0;
        if (!said)
        {
            said = 1;
            portConLogf("ge: input: steam overlay injected - Windows.Gaming.Input OFF (its init deadlocks under Steam's hook, F67; [input] wgi=1 forces it)\n");
        }
    }
#endif
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, s_OptHidapi ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, s_OptRawInput ? "1" : "0");
#ifdef SDL_HINT_JOYSTICK_WGI
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, s_OptWgi ? "1" : "0");
#endif
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, s_OptXInput ? "1" : "0");
}

/* SDL_GameControllerAddMappingsFromFile decodes its path as UTF-8 but
 * portRootPath is ANSI, so non-ASCII install folders silently loaded nothing;
 * open the file the way midas.ini is opened and hand SDL the bytes (-2 = no file,
 * else SDL's answer, -1 = parse failure). */
static int inputLoadMappingDb(void)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    FILE *f = portRootFopen("gamecontrollerdb.txt", "rb");
    char *buf = NULL;
    long len = 0;
    int n = -1;

    if (f == NULL)
    {
        return -2;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0)
    {
        buf = (char *) SDL_malloc((size_t) len);
    }
    if (buf != NULL && fread(buf, 1, (size_t) len, f) == (size_t) len)
    {
        n = SDL_GameControllerAddMappingsFromRW(SDL_RWFromConstMem(buf, (int) len), 1);
    }
    SDL_free(buf);
    fclose(f);
    return n;
}

void portInputInit(void)
{
    int i;

    if (s_InputInitDone)
    {
        return;
    }
    s_InputInitDone = true;

    /* the padN_<port action> rows must line up with joy_port.c's action
     * table (PM_PORT_FIRST + i <-> PORTACT i) - shout if someone reorders one */
    for (i = 0; i < PM_PORT_COUNT; i++)
    {
        const char *n = portJoyPortBindName(i);
        if (n == NULL || SDL_strcmp(n, sPadMapNames[PM_PORT_FIRST + i]) != 0)
        {
            portConLogf("ge: input: port action %d is \"%s\" in joy_port.c but \"%s\" in the pad map - fix the order\n",
                    i, n ? n : "(none)", sPadMapNames[PM_PORT_FIRST + i]);
        }
    }

    /* every backend hint (hidapi/rawinput/wgi/xinput/dinput) - one place,
     * shared with the launcher's probe runs */
    inputBackendHints();
    if (!s_OptRawInput)
    {
        portConLogf("ge: input: rawinput=0 - Xbox-class pads via XInput/WGI (hotplug A/B)\n");
    }

    /* Mouse: guarantee unaccelerated deltas by forcing raw-input relative mode
     * (never the warp fallback, which picks up "Enhance pointer precision") and
     * no system speed curve; our own scaling is strictly linear. */
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
#ifdef SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
#endif

    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
    {
        inputBackendHints();
        /* boot phase line (see video.c's window/GL pair): names the
         * backends in play so a frozen Steam Input log says which init
         * never returned */
        portConLogf("ge: input: controller backends starting (hidapi=%d rawinput=%d wgi=%d xinput=%d dinput=%d)\n",
                s_OptHidapi ? 1 : 0, s_OptRawInput ? 1 : 0, s_OptWgi ? 1 : 0, s_OptXInput ? 1 : 0, s_OptDInput ? 1 : 0);
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        {
            portConLogf("ge: input: SDL controller init failed: %s\n", SDL_GetError());
            return;
        }
        portConLogf("ge: input: controller backends up\n");
    }
    /* the device records' first path: moves an old ./Devices under Data/ now,
     * at boot, not whenever player 1's device first changes */
    (void) inputDevRecPath("kbm");

    /* Optional community mapping database next to the exe. */
    {
        int n = inputLoadMappingDb();   /* (UTF-8-safe path, see inputLoadMappingDb) */
        if (n > 0)
        {
            portConLogf("ge: input: %d mappings from gamecontrollerdb.txt\n", n);
        }
        else if (n == -1)
        {
            portConLogf("ge: input: gamecontrollerdb.txt not loaded (%s)\n", SDL_GetError());
        }
    }

    SDL_GameControllerUpdate();
    inputAssignSlotsEx(1);

    SDL_AddEventWatch(inputEventWatch, NULL);
}

/**
 * GE_LISTPADS=1 (GELauncher's device probe): prints every joystick SDL can see
 * (index, GUID, mapped/raw, name), then exits before any window opens. Called
 * from main() once the root dir is known, since the mappings DB lives next to
 * the exe.
 */
static void inputPrintDefaultMap(int jidx);
static void inputPrintPadNames(int jidx);
static void inputPrintPadSdl(int jidx);

void portInputListPadsAndExit(void)
{
    int jidx;
    int n;

    portRaphnetProbeMode();   /* a running game shares the adapter */
    inputBackendHints();
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("ge: pads 0 (SDL: %s)\n", SDL_GetError());
        exit(0);
    }
    inputLoadMappingDb();   /* (UTF-8-safe path, see inputLoadMappingDb) */
    SDL_GameControllerUpdate();

    n = SDL_NumJoysticks();
    printf("ge: pads %d\n", n);
    for (jidx = 0; jidx < n; jidx++)
    {
        char g[40];
        const char *name;

        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(jidx), g, sizeof(g));
        name = SDL_IsGameController(jidx) ? SDL_GameControllerNameForIndex(jidx)
                                          : SDL_JoystickNameForIndex(jidx);
        printf("ge: pad %d guid=%s %s name=%s\n",
               jidx, g, SDL_IsGameController(jidx) ? "mapped" : "raw",
               (name != NULL) ? name : "?");
        inputPrintDefaultMap(jidx);
        inputPrintPadNames(jidx);
        inputPrintPadSdl(jidx);
    }
    if (portRaphnetAvailable())
    {
        int chan;
        for (chan = 0; chan < 2; chan++)
        {
            s32 r = portRaphnetPoll(chan, NULL, NULL, NULL);
            printf("ge: rpad %d %s name=%s\n", chan,
                   (r == 1) ? "present" : "empty", portRaphnetName());
        }
    }
    fflush(stdout);
    exit(0);
}

/* Token for an SDL controller bind; axisdir picks the half for axis binds. */
static void inputBindToken(char *out, size_t outn, SDL_GameControllerButtonBind b, int axisdir)
{
    switch (b.bindType)
    {
    case SDL_CONTROLLER_BINDTYPE_BUTTON:
        SDL_snprintf(out, outn, "b%d", b.value.button);
        break;
    case SDL_CONTROLLER_BINDTYPE_AXIS:
        SDL_snprintf(out, outn, "a%d%c", b.value.axis, (axisdir < 0) ? '-' : '+');
        break;
    case SDL_CONTROLLER_BINDTYPE_HAT:
        SDL_snprintf(out, outn, "h%d%c", b.value.hat.hat,
                     (b.value.hat.hat_mask & SDL_HAT_UP) ? 'u'
                     : (b.value.hat.hat_mask & SDL_HAT_DOWN) ? 'd'
                     : (b.value.hat.hat_mask & SDL_HAT_LEFT) ? 'l' : 'r');
        break;
    default:
        SDL_strlcpy(out, "-", outn);
        break;
    }
}

/* Token for ONE DIRECTION of an SDL axis, read from the mapping string because
 * SDL_GameControllerGetBindForAxis loses half-axis ("+righty:b7,-righty:b6")
 * and inversion ("lefty:a1~") info; falls back to the bind when the string says
 * nothing more. */
static void inputAxisToken(SDL_GameController *c, SDL_GameControllerAxis a, int dir, char *out, size_t outn)
{
    const char *an = SDL_GameControllerGetStringForAxis(a);
    char *map = (an != NULL) ? SDL_GameControllerMapping(c) : NULL;
    const char *p = NULL;
    char key[24];
    int half = 0;

    if (map != NULL)
    {
        SDL_snprintf(key, sizeof(key), ",%c%s:", (dir < 0) ? '-' : '+', an);
        p = SDL_strstr(map, key);
        half = (p != NULL);
        if (p == NULL)
        {
            SDL_snprintf(key, sizeof(key), ",%s:", an);
            p = SDL_strstr(map, key);
        }
    }
    if (p != NULL)
    {
        p += SDL_strlen(key);
        if (p[1] < '0' || p[1] > '9')
        {
            p = NULL;   /* "+a2" / "-a2" half inputs etc.: the bind's answer */
        }
        else if (half && p[0] == 'b')
        {
            SDL_snprintf(out, outn, "b%d", SDL_atoi(p + 1));
        }
        else if (half && p[0] == 'h' && SDL_strchr(p, '.') != NULL)
        {
            const int m = SDL_atoi(SDL_strchr(p, '.') + 1);

            SDL_snprintf(out, outn, "h%d%c", SDL_atoi(p + 1),
                         (m & SDL_HAT_UP) ? 'u' : (m & SDL_HAT_DOWN) ? 'd' : (m & SDL_HAT_LEFT) ? 'l' : 'r');
        }
        else if (!half && p[0] == 'a')
        {
            const char *q = p + 1;

            while (*q >= '0' && *q <= '9')
            {
                q++;
            }
            /* '~' = the raw axis runs the other way round */
            SDL_snprintf(out, outn, "a%d%c", SDL_atoi(p + 1), ((dir < 0) != (*q == '~')) ? '-' : '+');
        }
        else
        {
            p = NULL;
        }
    }
    if (p == NULL)
    {
        inputBindToken(out, outn, SDL_GameControllerGetBindForAxis(c, a), dir);
    }
    SDL_free(map);
}

/* 1 = SDL output axis `a` is a full raw axis marked '~' (inverted) in this
 * pad's mapping, e.g. "lefty:a1~". Reverse lookups (raw token -> SDL name) must
 * flip direction for it, or a saved "lefty+" re-applies as the other end. */
static int inputAxisInverted(SDL_GameController *c, SDL_GameControllerAxis a)
{
    const char *an = SDL_GameControllerGetStringForAxis(a);
    char *map = (an != NULL) ? SDL_GameControllerMapping(c) : NULL;
    char key[24];
    const char *p;
    int inv = 0;

    if (map == NULL)
    {
        return 0;
    }
    SDL_snprintf(key, sizeof(key), ",%s:", an);
    p = SDL_strstr(map, key);
    if (p != NULL)
    {
        p += SDL_strlen(key);
        if (p[0] == 'a' && p[1] >= '0' && p[1] <= '9')
        {
            p++;
            while (*p >= '0' && *p <= '9')
            {
                p++;
            }
            inv = (*p == '~');
        }
    }
    SDL_free(map);
    return inv;
}

/**
 * THE BUILT-IN LAYOUT for every mapped non-N64 SDL controller, as raw tokens of
 * THIS device's own SDL bindings, so DualSense, 8BitDo, Switch Pro etc. get the
 * same roles on their A/B/X/Y equivalents:
 *   Move = left stick, Look = right stick, D-pad = digital move, RT fire, LT
 *   aim, Y previous gadget,
 *   B next gadget, X reload, A use (= confirm in menus), Start pause, LB / RB
 *   previous / next weapon
 *   and watch page, LS + RS click crouch toggle.
 *   Unbound: N64 A and B, the C buttons, the N64 D-pad, L, walk / run / toggle,
 *   crouch hold, look_* buttons.
 *   (N64 A and B would double-act with the gadget buttons, e.g. stepping back a
 *   gadget would also cycle the gun.)
 */
/* An N64-shaped pad (8BitDo 64, USB N64 adapters: "64" in the name and no X/Y
 * face buttons) gets the N64 pass-through layout, same as the launcher's "N64
 * Controller" preset: A/B, Z on either trigger, L/R, C stick = C buttons,
 * D-pad, stick, no Port actions. */
static int inputIsN64Pad(SDL_GameController *c)
{
    const char *name = SDL_GameControllerName(c);
    int hasXY = SDL_GameControllerGetBindForButton(c, SDL_CONTROLLER_BUTTON_X).bindType != SDL_CONTROLLER_BINDTYPE_NONE
             || SDL_GameControllerGetBindForButton(c, SDL_CONTROLLER_BUTTON_Y).bindType != SDL_CONTROLLER_BINDTYPE_NONE;
    if (name == NULL)
    {
        return 0;
    }
    /* "N64" in the name is decisive (the NSO "Nintendo N64 Controller" has
     * X/Y in SDL's table - its C buttons - and would otherwise be seeded Xbox);
     * a bare "64" only counts without X/Y (8BitDo 64) */
    if (SDL_strstr(name, "N64") != NULL)
    {
        return 1;
    }
    return !hasXY && SDL_strstr(name, "64") != NULL;
}

/* COMPILED FALLBACK, used only when gamecontrollerdefault.txt is missing
 * (the file next to ge.exe is the real source of every layout now - see
 * inputDefaultTokens below). */
static void inputDefaultTokensBuiltin(SDL_GameController *c, char tok[][16])
{
    char t2[16];
    int i;

    for (i = 0; i < PM_COUNT; i++)
    {
        SDL_strlcpy(tok[i], "-", 16);
    }
#define DEF_BTN(pm, b)    inputBindToken(tok[pm], 16, SDL_GameControllerGetBindForButton(c, b), 1)
#define DEF_AXS(pm, a, d) inputAxisToken(c, a, d, tok[pm], 16)
#define DEF_PORT(n)       (PM_PORT_FIRST + (n)) /* joy_port.c PORTACT_* order */
    if (inputIsN64Pad(c))
    {
        DEF_BTN(PM_A, SDL_CONTROLLER_BUTTON_A);
        DEF_BTN(PM_B, SDL_CONTROLLER_BUTTON_B);
        DEF_AXS(PM_Z, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
        inputAxisToken(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1, t2, 16);
        if (t2[0] != '-')
        {
            if (tok[PM_Z][0] == '-') SDL_strlcpy(tok[PM_Z], t2, 16);
            else { SDL_strlcat(tok[PM_Z], ",", 16); SDL_strlcat(tok[PM_Z], t2, 16); }
        }
        DEF_BTN(PM_START, SDL_CONTROLLER_BUTTON_START);
        DEF_BTN(PM_L, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        DEF_BTN(PM_R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        DEF_AXS(PM_CUP, SDL_CONTROLLER_AXIS_RIGHTY, -1);
        DEF_AXS(PM_CDOWN, SDL_CONTROLLER_AXIS_RIGHTY, 1);
        DEF_AXS(PM_CLEFT, SDL_CONTROLLER_AXIS_RIGHTX, -1);
        DEF_AXS(PM_CRIGHT, SDL_CONTROLLER_AXIS_RIGHTX, 1);
        if (SDL_strcmp(SDL_GameControllerName(c), "Nintendo N64 Controller") == 0)
        {
            /* NSO N64 pad over Bluetooth (SDL HIDAPI): SDL puts C-Down on the RIGHT
             * trigger axis, so Z is the left trigger only; C-Up = Y, C-Left =
             * X, C-Right = the Back button. */
            DEF_AXS(PM_Z, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
            DEF_BTN(PM_CUP, SDL_CONTROLLER_BUTTON_Y);
            DEF_AXS(PM_CDOWN, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
            DEF_BTN(PM_CLEFT, SDL_CONTROLLER_BUTTON_X);
            DEF_BTN(PM_CRIGHT, SDL_CONTROLLER_BUTTON_BACK);
        }
        else if (SDL_strcmp(SDL_GameControllerName(c), "8BitDo 64") == 0)
        {
            /* 8BitDo N64 defaults: its C buttons are raw buttons 15/16/18/19 in
             * DInput mode, which the SDL mapping does not name (it only maps
             * the C stick as right-stick axes), so use the buttons */
            SDL_strlcpy(tok[PM_CUP], "b15", 16);
            SDL_strlcpy(tok[PM_CDOWN], "b16", 16);
            SDL_strlcpy(tok[PM_CLEFT], "b18", 16);
            SDL_strlcpy(tok[PM_CRIGHT], "b19", 16);
        }
        DEF_BTN(PM_DUP, SDL_CONTROLLER_BUTTON_DPAD_UP);
        DEF_BTN(PM_DDOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        DEF_BTN(PM_DLEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        DEF_BTN(PM_DRIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        DEF_AXS(PM_STICK_X, SDL_CONTROLLER_AXIS_LEFTX, 1);
        DEF_AXS(PM_STICK_Y, SDL_CONTROLLER_AXIS_LEFTY, -1);
        return;
    }
    {
        /* PlayStation pads: same as the Xbox map except Pause-to-Objectives on
         * Share (Back). This compiled fallback, used only when
         * gamecontrollerdefault.txt is missing, doesn't follow that file (which
         * puts it on Back for both). */
        int ps = 0;
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_GameControllerType t = SDL_GameControllerGetType(c);
        ps = (t == SDL_CONTROLLER_TYPE_PS3 || t == SDL_CONTROLLER_TYPE_PS4
#if SDL_VERSION_ATLEAST(2, 0, 14)
              || t == SDL_CONTROLLER_TYPE_PS5
#endif
              );
#endif
        if (ps)
        {
            DEF_BTN(DEF_PORT(13), SDL_CONTROLLER_BUTTON_BACK); /* watch_objectives = Share */
        }
    }
    /* N64 B, the C buttons and the N64 D-pad stay UNBOUND: B would double-act
     * with next gadget, the C stick is the Look stick, the hat is digital move. */
    DEF_AXS(PM_Z, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1); /* fire */
    DEF_BTN(PM_START, SDL_CONTROLLER_BUTTON_START);
    DEF_AXS(PM_R, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);  /* aim */
    DEF_AXS(PM_STICK_X, SDL_CONTROLLER_AXIS_LEFTX, 1);
    DEF_AXS(PM_STICK_Y, SDL_CONTROLLER_AXIS_LEFTY, -1);
    DEF_AXS(PM_LOOK_X, SDL_CONTROLLER_AXIS_RIGHTX, 1);
    DEF_AXS(PM_LOOK_Y, SDL_CONTROLLER_AXIS_RIGHTY, -1);
    DEF_BTN(PM_MOVE_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
    DEF_BTN(PM_MOVE_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    DEF_BTN(PM_MOVE_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    DEF_BTN(PM_MOVE_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    DEF_BTN(DEF_PORT(0), SDL_CONTROLLER_BUTTON_X);             /* reload */
    DEF_BTN(DEF_PORT(1), SDL_CONTROLLER_BUTTON_A);             /* use */
    DEF_BTN(DEF_PORT(5), SDL_CONTROLLER_BUTTON_LEFTSHOULDER);  /* prev weapon */
    DEF_BTN(DEF_PORT(6), SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); /* next weapon */
    DEF_BTN(DEF_PORT(7), SDL_CONTROLLER_BUTTON_Y);             /* prev gadget */
    DEF_BTN(DEF_PORT(8), SDL_CONTROLLER_BUTTON_B);             /* next gadget */
    DEF_BTN(DEF_PORT(9), SDL_CONTROLLER_BUTTON_LEFTSTICK);     /* crouch toggle: LS click ... */
    inputBindToken(t2, 16, SDL_GameControllerGetBindForButton(c, SDL_CONTROLLER_BUTTON_RIGHTSTICK), 1);
    if (t2[0] != '-')                                          /* ... or RS click */
    {
        if (tok[DEF_PORT(9)][0] == '-')
        {
            SDL_strlcpy(tok[DEF_PORT(9)], t2, 16);
        }
        else
        {
            SDL_strlcat(tok[DEF_PORT(9)], ",", 16);
            SDL_strlcat(tok[DEF_PORT(9)], t2, 16);
        }
    }
    DEF_BTN(DEF_PORT(11), SDL_CONTROLLER_BUTTON_LEFTSHOULDER);  /* watch prev page */
    DEF_BTN(DEF_PORT(12), SDL_CONTROLLER_BUTTON_RIGHTSHOULDER); /* watch next page */
    {   /* zoom in / out = Move stick up / down and the D-pad, as the shipped
             * Xbox Pro line: a port-driven pad zooms through these rows only,
             * so without the file it could not zoom at all */
        static const struct { int port; int dir; SDL_GameControllerButton dp; } zoom[2] = {
            { 15, -1, SDL_CONTROLLER_BUTTON_DPAD_UP },     /* zoom_in */
            { 16,  1, SDL_CONTROLLER_BUTTON_DPAD_DOWN },   /* zoom_out */
        };

        for (i = 0; i < 2; i++)
        {
            DEF_AXS(DEF_PORT(zoom[i].port), SDL_CONTROLLER_AXIS_LEFTY, zoom[i].dir);
            inputBindToken(t2, 16, SDL_GameControllerGetBindForButton(c, zoom[i].dp), 1);
            if (t2[0] != '-')
            {
                if (tok[DEF_PORT(zoom[i].port)][0] == '-')
                {
                    SDL_strlcpy(tok[DEF_PORT(zoom[i].port)], t2, 16);
                }
                else
                {
                    SDL_strlcat(tok[DEF_PORT(zoom[i].port)], ",", 16);
                    SDL_strlcat(tok[DEF_PORT(zoom[i].port)], t2, 16);
                }
            }
        }
    }
#undef DEF_BTN
#undef DEF_AXS
#undef DEF_PORT
}

/* ---- gamecontrollerdefault.txt ------------------------------------------ */
/*
 * gamecontrollerdefault.txt (next to ge.exe) holds every shipped layout so
 * users can add and share their own, one per line:
 *     Name | match terms | key=value; key=value; ...
 * Match terms (comma separated, all must hold): type=xbox|ps|switch|any,
 * name=<exact SDL name>, name~<substring>, noxy (no X/Y buttons). The FIRST
 * layout that matches seeds a new pad (inputOpenController) and is what "ge:
 * padmap" prints; the launcher lists them all. Values are SDL control names (a,
 * leftshoulder, leftx+, righttrigger ...) resolved through THIS pad's mapping,
 * or raw tokens (b8, a4+, h0u); "x,y" = one action on several inputs; unlisted
 * keys are unbound.
 */
#define LAYOUT_MAX 48
/* saved layouts load first, so they must not fill the whole list and push
 * out the shipped presets: they keep LAYOUT_MAX - LAYOUT_USER_MAX for the presets. */
#define LAYOUT_USER_MAX 32
/* the save writes a control's primary plus three alternates by SDL name, up
 * to val[96] - a smaller buffer cuts the fourth item, or "b12" to "b1" */
#define LAYOUT_VAL 96
typedef struct
{
    char name[64];
    char mtype[8];      /* "" / xbox / ps / switch / any */
    char mname[64];     /* exact SDL name, "" = don't care */
    char msub[64];      /* substring of the name, "" = don't care */
    int  noxy;
    /* This pad is an N64 controller or derivative (match term "n64"), declared
     * in the layout file so new derivatives need no rebuild. It decides whether
     * N64 control styles and bindings are offered
     * (docs/port/port-inputs-vs-n64-inputs.md). */
    int  isN64;
    char val[PM_COUNT][LAYOUT_VAL];
} PadLayout;
static PadLayout s_Layouts[LAYOUT_MAX];
/* per slot: the layout this slot is on declared "n64" - set by the seed, by
 * portInputLayoutApply, and from padN_layout on a custom map */
static int s_PadIsN64[INPUT_MAX_PADS];
/* [input] padN_inputmode - who decides how this player's device is driven:
 *
 *   0 auto  guess from the hardware (the default)
 *   1 port  the port drives it: Move stick strafes and walks, Look stick
 *           turns and pitches, movement is bound per direction
 *   2 n64   the vanilla control styles (1.1 Honey ... 2.4 Goodhead)
 *
 * An explicit choice always wins over the guess, so devices the sniff misses (a
 * raphnet opened as a plain joystick, an 8BitDo, a ModRetro M64) can still be
 * set; the Options > CONTROL > player page carries the row. */
static int s_PadInputMode[INPUT_MAX_PADS];
/* [input] padN_style - which x.x control style this player is on, -1 = auto (no
 * change: saved style and twin-stick promotion apply as before), so e.g. a
 * raphnet player can pick 1.1 Honey ... 2.4 Goodhead from Options before
 * starting. A chosen style overrides both in cur_player_get_control_type(); it
 * is port-side only and must never rewrite the save file. */
static int s_PadStyle[INPUT_MAX_PADS] = { -1, -1, -1, -1 };
/* [input] raphnet<chan>_style: a raphnet's style is keyed by adapter channel,
 * not slot, so a style picked by another pad in that slot doesn't leak onto the
 * N64 pad. Every style read and write goes through inputStyleCell. */
static int s_RaphnetStyle[2] = { -1, -1 };

static int *inputStyleCell(int slot)
{
    static int none = -1;

    if (slot < 0 || slot >= INPUT_MAX_PADS)
    {
        none = -1;
        return &none;
    }
    if (s_PadRaphnet[slot] >= 1 && s_PadRaphnet[slot] <= 2)
    {
        return &s_RaphnetStyle[s_PadRaphnet[slot] - 1];
    }
    return &s_PadStyle[slot];
}

/* midas.ini spelling for padN_style: index 0 = 1.1 Honey ... 7 = 2.4 Goodhead.
 *
 * 8..11 = "2.1p2" .. "2.4p2": this pad is CONTROLLER 2 of a two-controller
 * style. The console reads the second pad from a fixed port; here a P2 slot can
 * be any slot, and portInputSecondPadIndex hands it to the player whose pad is
 * on the P1 side of the same style. A P2 slot reads as auto in
 * cur_player_get_control_type: it is not a player. */
#define PAD_STYLE_COUNT 12
#define PAD_STYLE_P2_FIRST 8
static const char *const sPadStyleNames[PAD_STYLE_COUNT] = {
    "1.1", "1.2", "1.3", "1.4", "2.1", "2.2", "2.3", "2.4",
    "2.1p2", "2.2p2", "2.3p2", "2.4p2"
};
static int s_LayoutCount = -1; /* -1 = not loaded yet */
/* 1 = this one came from gamecontrolleruser.txt, so it may be overwritten or
 * deleted from the menu; a shipped layout may only be applied. */
static int s_LayoutIsUser[LAYOUT_MAX];
#define LAYOUT_USER_FILE "gamecontrolleruser.txt"

static char *inputTrim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    e = s + SDL_strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

/*
 * Saved bindings and layout presets are the same thing - a named set of
 * bindings applied to a device - so they share this format, list and resolver;
 * the only difference is which file a line came from, and so whether the player
 * may overwrite it.
 *
 * User layouts are read FIRST, and the first match seeds a new pad, so a layout
 * saved for your own controller beats the shipped default. Deleting it brings
 * the shipped one back.
 */
static void inputLoadLayoutFile(const char *path, int isUser);

static void inputLoadLayouts(void)
{
    extern const char *portRootPath(const char *name);

    s_LayoutCount = 0;
    inputLoadLayoutFile(portRootPath(LAYOUT_USER_FILE), 1);
    if (s_LayoutCount >= LAYOUT_USER_MAX)
    {
        portConLogf("ge: input: %s: only the first %d saved layouts are loaded\n", LAYOUT_USER_FILE, LAYOUT_USER_MAX);
    }
    inputLoadLayoutFile(portRootPath("gamecontrollerdefault.txt"), 0);
    portConLogf("ge: input: %d layout(s) available\n", s_LayoutCount);
}

static void inputLoadLayoutFile(const char *path, int isUser)
{
    FILE *f;
    char line[1024];

    f = fopen(path, "r");
    if (f == NULL)
    {
        if (!isUser)
        {
            portConLogf("ge: input: no gamecontrollerdefault.txt next to the exe - built-in Xbox layout only\n");
        }
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL && s_LayoutCount < (isUser ? LAYOUT_USER_MAX : LAYOUT_MAX))
    {
        PadLayout *L = &s_Layouts[s_LayoutCount];
        char *p = inputTrim(line);

        s_LayoutIsUser[s_LayoutCount] = isUser;
        char *bar1, *bar2, *term, *kv, *next;

        if (*p == '#' || *p == '\0')
        {
            continue;
        }
        bar1 = SDL_strchr(p, '|');
        if (bar1 == NULL) continue;
        *bar1++ = '\0';
        bar2 = SDL_strchr(bar1, '|');
        if (bar2 == NULL) continue;
        *bar2++ = '\0';
        SDL_memset(L, 0, sizeof(*L));
        SDL_strlcpy(L->name, inputTrim(p), sizeof(L->name));
        for (term = bar1; term != NULL; term = next)
        {
            next = SDL_strchr(term, ',');
            if (next != NULL) *next++ = '\0';
            term = inputTrim(term);
            if (*term == '\0') continue;
            if (SDL_strncmp(term, "type=", 5) == 0)      SDL_strlcpy(L->mtype, term + 5, sizeof(L->mtype));
            else if (SDL_strncmp(term, "name=", 5) == 0) SDL_strlcpy(L->mname, term + 5, sizeof(L->mname));
            else if (SDL_strncmp(term, "name~", 5) == 0) SDL_strlcpy(L->msub, term + 5, sizeof(L->msub));
            else if (SDL_strcmp(term, "noxy") == 0)      L->noxy = 1;
            else if (SDL_strcmp(term, "n64") == 0)       L->isN64 = 1;
            else if (SDL_strcmp(term, "any") == 0)       SDL_strlcpy(L->mtype, "any", sizeof(L->mtype));
            else portConLogf("ge: input: gamecontrollerdefault.txt \"%s\": unknown match term \"%s\"\n", L->name, term);
        }
        for (kv = bar2; kv != NULL; kv = next)
        {
            char *eq;
            int i;
            next = SDL_strchr(kv, ';');
            if (next != NULL) *next++ = '\0';
            kv = inputTrim(kv);
            eq = SDL_strchr(kv, '=');
            if (*kv == '\0' || eq == NULL) continue;
            *eq++ = '\0';
            kv = inputTrim(kv);
            eq = inputTrim(eq);
            for (i = 0; i < PM_COUNT; i++)
            {
                if (SDL_strcmp(kv, sPadMapNames[i]) == 0)
                {
                    SDL_strlcpy(L->val[i], eq, LAYOUT_VAL);
                    break;
                }
            }
            if (i == PM_COUNT)
            {
                portConLogf("ge: input: gamecontrollerdefault.txt \"%s\": unknown key \"%s\"\n", L->name, kv);
            }
        }
        s_LayoutCount++;
    }
    fclose(f);
}

static int inputLayoutMatches(const PadLayout *L, SDL_GameController *c)
{
    const char *name = SDL_GameControllerName(c);
    if (name == NULL) name = "";
    if (L->mname[0] != '\0' && SDL_strcasecmp(L->mname, name) != 0) return 0;
    if (L->msub[0] != '\0' && SDL_strstr(name, L->msub) == NULL) return 0;
    if (L->noxy
        && (SDL_GameControllerGetBindForButton(c, SDL_CONTROLLER_BUTTON_X).bindType != SDL_CONTROLLER_BINDTYPE_NONE
            || SDL_GameControllerGetBindForButton(c, SDL_CONTROLLER_BUTTON_Y).bindType != SDL_CONTROLLER_BINDTYPE_NONE))
    {
        return 0;
    }
    if (L->mtype[0] != '\0' && SDL_strcmp(L->mtype, "any") != 0)
    {
        const char *tn = "other";
#if SDL_VERSION_ATLEAST(2, 0, 12)
        switch (SDL_GameControllerGetType(c))
        {
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE:             tn = "xbox"; break;
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
#if SDL_VERSION_ATLEAST(2, 0, 14)
        case SDL_CONTROLLER_TYPE_PS5:
#endif
                                                      tn = "ps"; break;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: tn = "switch"; break;
        default: break;
        }
#endif
        if (SDL_strcmp(L->mtype, tn) != 0) return 0;
    }
    return 1;
}

/* one layout value ("leftshoulder", "leftx+", "b15", "leftstick,rightstick")
 * -> raw token list for THIS pad; "-" when nothing resolves */
static void inputLayoutResolve(SDL_GameController *c, const char *val, char *out, size_t outn)
{
    char buf[LAYOUT_VAL];
    char *item, *next;

    out[0] = '\0';
    SDL_strlcpy(buf, val, sizeof(buf));
    for (item = buf; item != NULL; item = next)
    {
        char tok[16];
        char nm[32];
        size_t len;
        int dir = 1;
        SDL_GameControllerButton b;
        SDL_GameControllerAxis a;

        next = SDL_strchr(item, ',');
        if (next != NULL) *next++ = '\0';
        item = inputTrim(item);
        if (*item == '\0') continue;
        tok[0] = '-';
        tok[1] = '\0';
        SDL_strlcpy(nm, item, sizeof(nm));
        len = SDL_strlen(nm);
        if (len > 1 && (nm[len - 1] == '+' || nm[len - 1] == '-'))
        {
            dir = (nm[len - 1] == '-') ? -1 : 1;
            nm[len - 1] = '\0';
        }
        b = SDL_GameControllerGetButtonFromString(item);
        a = SDL_GameControllerGetAxisFromString(nm);
        if (b != SDL_CONTROLLER_BUTTON_INVALID)
        {
            inputBindToken(tok, sizeof(tok), SDL_GameControllerGetBindForButton(c, b), 1);
        }
        else if (a != SDL_CONTROLLER_AXIS_INVALID)
        {
            inputAxisToken(c, a, dir, tok, sizeof(tok));
        }
        else if ((item[0] == 'b' || item[0] == 'a' || item[0] == 'h') && item[1] >= '0' && item[1] <= '9')
        {
            SDL_strlcpy(tok, item, sizeof(tok)); /* raw token, as written */
        }
        if (tok[0] == '-') continue;
        if (out[0] != '\0') SDL_strlcat(out, ",", outn);
        SDL_strlcat(out, tok, outn);
    }
    if (out[0] == '\0')
    {
        SDL_strlcpy(out, "-", outn);
    }
}

/* The layout entry padN_layout means: by name, and among same-named entries
 * the shipped or user copy padN_layout_user recorded. Not recorded = the first
 * of that name. -1 = no layout of that name. */
static int inputLayoutWantIndex(int slot)
{
    int first = -1;
    int k;

    for (k = 0; k < s_LayoutCount; k++)
    {
        if (SDL_strcasecmp(s_Layouts[k].name, s_PadLayoutWant[slot]) != 0)
        {
            continue;
        }
        if (s_PadLayoutUser[slot] < 0 || s_LayoutIsUser[k] == s_PadLayoutUser[slot])
        {
            return k;
        }
        if (first < 0)
        {
            first = k;
        }
    }
    return first;
}

/* The layout that seeds this pad: the slot's padN_layout, else the first match
 * in the layout files, else the compiled Xbox fallback. Returns the layout
 * name; `slot` < 0 means no slot, so no preference. */
static const char *inputDefaultTokensForSlot(int slot, SDL_GameController *c, char tok[][16])
{
    int i, k;
    int want = -1;

    if (s_LayoutCount < 0)
    {
        inputLoadLayouts();
    }
    if (slot >= 0 && slot < INPUT_MAX_PADS && s_PadLayoutWant[slot][0] != '\0')
    {
        want = inputLayoutWantIndex(slot);
        if (want < 0)
        {
            portConLogf("ge: input: player %d asked for layout '%s' and there is no such layout - using the first match\n",
                    slot + 1, s_PadLayoutWant[slot]);
        }
    }
    for (k = (want >= 0) ? want : 0; k < s_LayoutCount; k++)
    {
        if (want < 0 && !inputLayoutMatches(&s_Layouts[k], c))
        {
            continue;
        }
        for (i = 0; i < PM_COUNT; i++)
        {
            if (s_Layouts[k].val[i][0] != '\0')
            {
                inputLayoutResolve(c, s_Layouts[k].val[i], tok[i], 16);
            }
            else
            {
                SDL_strlcpy(tok[i], "-", 16);
            }
        }
        if (slot >= 0 && slot < INPUT_MAX_PADS)
        {
            s_PadIsN64[slot] = s_Layouts[k].isN64;
        }
        return s_Layouts[k].name;
    }
    if (slot >= 0 && slot < INPUT_MAX_PADS)
    {
        s_PadIsN64[slot] = 0;   /* the compiled Xbox fallback is never N64 */
    }
    inputDefaultTokensBuiltin(c, tok);
    return "built-in Xbox layout";
}

static const char *inputDefaultTokens(SDL_GameController *c, char tok[][16])
{
    return inputDefaultTokensForSlot(-1, c, tok);
}

/* The n64 tag for a slot with a CUSTOM map (the seed never runs), read from the
 * slot's padN_layout, since applying a layout writes a custom map; without it
 * an Xbox pad on "Xbox Standard" booted port-driven. Nothing recorded = leave
 * the tag alone. */
static void inputLayoutTagSlot(int slot)
{
    int k;

    if (s_PadLayoutWant[slot][0] == '\0')
    {
        return;
    }
    if (s_LayoutCount < 0)
    {
        inputLoadLayouts();
    }
    k = inputLayoutWantIndex(slot);
    /* a recorded name that no longer exists declares nothing */
    s_PadIsN64[slot] = (k >= 0) ? s_Layouts[k].isN64 : 0;
}

/**
 * "ge: padnames <jidx> b0=A|b1=B|a4+=LT|h0u=D-Pad Up|...": the device's raw
 * tokens named through its SDL mapping, so the launcher shows real button names
 * instead of "button 3". Names follow the controller type (PlayStation: Cross,
 * Circle, L1, Share...), else Xbox-style; '|' separates pairs because names
 * contain spaces.
 */
static void inputPrintPadNames(int jidx)
{
    SDL_GameController *c;
    char tok[16];
    int ps = 0;
    int b;
    int a;

    if (!SDL_IsGameController(jidx))
    {
        return;
    }
    c = SDL_GameControllerOpen(jidx);
    if (c == NULL)
    {
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    {
        SDL_GameControllerType t = SDL_GameControllerGetType(c);
        ps = (t == SDL_CONTROLLER_TYPE_PS3 || t == SDL_CONTROLLER_TYPE_PS4
#if SDL_VERSION_ATLEAST(2, 0, 14)
              || t == SDL_CONTROLLER_TYPE_PS5
#endif
              );
    }
#endif
    printf("ge: padnames %d", jidx);
    for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
    {
        const char *nm;
        inputBindToken(tok, sizeof(tok), SDL_GameControllerGetBindForButton(c, (SDL_GameControllerButton) b), 1);
        if (tok[0] == '-')
        {
            continue;
        }
        nm = inputPadButtonName(ps ? PADSTYLE_PS : inputPadStyle(c), (SDL_GameControllerButton) b);
        if (nm == NULL)
        {
            continue;
        }
        printf("|%s=%s", tok, nm);
    }
    for (a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++)
    {
        if (a <= SDL_CONTROLLER_AXIS_RIGHTY)
        {
            int d;
            for (d = -1; d <= 1; d += 2)
            {
                inputAxisToken(c, (SDL_GameControllerAxis) a, d, tok, sizeof(tok));
                if (tok[0] != '-')
                {
                    printf("|%s=%s", tok, inputPadAxisName(ps ? PADSTYLE_PS : inputPadStyle(c), (SDL_GameControllerAxis) a, d));
                }
            }
        }
        else
        {
            inputAxisToken(c, (SDL_GameControllerAxis) a, 1, tok, sizeof(tok));
            if (tok[0] != '-')
            {
                printf("|%s=%s", tok, inputPadAxisName(ps ? PADSTYLE_PS : inputPadStyle(c), (SDL_GameControllerAxis) a, 1));
            }
        }
    }
    printf("\n");
    SDL_GameControllerClose(c);
}

/**
 * "ge: padsdl <jidx> a=b0 b=b1 ... dpup=h0u leftx-=a0- ... righttrigger=a5+":
 * the device's SDL control names -> raw tokens. The launcher's layout presets
 * are written in these names, so one preset applies to any mapped pad.
 */
static void inputPrintPadSdl(int jidx)
{
    SDL_GameController *c;
    char tok[16];
    int b;
    int a;

    if (!SDL_IsGameController(jidx))
    {
        return;
    }
    c = SDL_GameControllerOpen(jidx);
    if (c == NULL)
    {
        return;
    }
    printf("ge: padsdl %d", jidx);
    for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
    {
        const char *nm = SDL_GameControllerGetStringForButton((SDL_GameControllerButton) b);
        inputBindToken(tok, sizeof(tok), SDL_GameControllerGetBindForButton(c, (SDL_GameControllerButton) b), 1);
        if (nm != NULL && tok[0] != '-')
        {
            printf(" %s=%s", nm, tok);
        }
    }
    for (a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++)
    {
        const char *nm = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis) a);
        if (nm == NULL)
        {
            continue;
        }
        if (a <= SDL_CONTROLLER_AXIS_RIGHTY)
        {
            inputAxisToken(c, (SDL_GameControllerAxis) a, -1, tok, sizeof(tok));
            if (tok[0] != '-') printf(" %s-=%s", nm, tok);
            inputAxisToken(c, (SDL_GameControllerAxis) a, 1, tok, sizeof(tok));
            if (tok[0] != '-') printf(" %s+=%s", nm, tok);
        }
        else
        {
            inputAxisToken(c, (SDL_GameControllerAxis) a, 1, tok, sizeof(tok));
            if (tok[0] != '-') printf(" %s=%s", nm, tok);
        }
    }
    printf("\n");
    SDL_GameControllerClose(c);
}

/**
 * "ge: padmap <jidx> a=b0 z=a5+ ... stick_y=a1-": the device's DEFAULT
 * layout as raw tokens - the launcher seeds its rebind rows from this so
 * a custom map starts as a faithful copy of what the port would do.
 */
static void inputPrintDefaultMap(int jidx)
{
    char tok[PM_COUNT][16];
    int i;

    if (SDL_IsGameController(jidx))
    {
        SDL_GameController *c = SDL_GameControllerOpen(jidx);
        if (c == NULL)
        {
            return;
        }
        const char *layout = inputDefaultTokens(c, tok);
        printf("ge: padlayout %d %s\n", jidx, layout); /* which gamecontrollerdefault.txt line seeds it */
        SDL_GameControllerClose(c);
    }
    else
    {
        /* the raw fallback layout, spelled out */
        for (i = 0; i < PM_UNBOUND_FIRST; i++)
        {
            SDL_strlcpy(tok[i], sPadRawFallback[i], 16);
        }
        for (i = PM_UNBOUND_FIRST; i < PM_COUNT; i++)
        {
            SDL_strlcpy(tok[i], "-", 16); /* raw fallback: digital movement + Port actions unbound */
        }
    }

    printf("ge: padmap %d", jidx);
    for (i = 0; i < PM_COUNT; i++)
    {
        printf(" %s=%s", sPadMapNames[i], tok[i]);
    }
    printf("\n");
}

/**
 * GE_CAPTUREPAD=<jidx> (GELauncher rebind): open device jidx, wait up to
 * 10s for a button press / axis push / hat direction relative to the
 * at-rest baseline, print "ge: capture <token>" (or "none") and exit.
 */
void portInputCapturePadAndExit(const char *arg)
{
    SDL_Joystick *joy;
    SDL_GameController *gc = NULL;
    s16 baseAxis[32];
    s16 gcBase[SDL_CONTROLLER_AXIS_MAX];
    u8 baseBtn[64];
    u8 baseHat[8];
    int nb, na, nh;
    int i;
    int waited;

    inputBackendHints();
    /* SDL on Windows drives XInput-class pads through RawInput, whose reports
     * arrive as window messages, so a joystick-only init never sees them: init
     * video too (no window) and pump every iteration, falling back to
     * joystick-only if video refuses. */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0
        && SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("ge: capture none\n");
        exit(0);
    }
    inputLoadMappingDb();   /* (UTF-8-safe path, see inputLoadMappingDb) */
    SDL_PumpEvents();
    SDL_JoystickUpdate();
    joy = SDL_JoystickOpen(SDL_atoi(arg));
    if (joy == NULL)
    {
        printf("ge: capture none\n");
        fflush(stdout);
        exit(0);
    }
    /* Mapped pads: read AXES through the GameController API, which rests
     * triggers at 0 and sticks at centre, so a delta is always a real push (raw
     * trigger axes "move" on a pad's first report). Buttons / hats stay raw so
     * buttons SDL doesn't name still capture. */
    if (SDL_IsGameController(SDL_atoi(arg)))
    {
        gc = SDL_GameControllerOpen(SDL_atoi(arg));
    }

    /* let the first reports land before the at-rest baseline is taken
     * (an XInput trigger sits at -32768 once reported, 0 before) */
    for (i = 0; i < 10; i++)
    {
        SDL_PumpEvents();
        SDL_JoystickUpdate();
        SDL_Delay(10);
    }
    nb = SDL_JoystickNumButtons(joy); if (nb > 64) nb = 64;
    na = SDL_JoystickNumAxes(joy);    if (na > 32) na = 32;
    nh = SDL_JoystickNumHats(joy);    if (nh > 8)  nh = 8;
    for (i = 0; i < nb; i++) baseBtn[i] = SDL_JoystickGetButton(joy, i);
    for (i = 0; i < na; i++) baseAxis[i] = SDL_JoystickGetAxis(joy, i);
    for (i = 0; i < nh; i++) baseHat[i] = SDL_JoystickGetHat(joy, i);
    for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) gcBase[i] = gc ? SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis) i) : 0;
    portConLogf("ge: capture: device %s: %d buttons %d axes %d hats%s, waiting\n",
            SDL_JoystickName(joy), nb, na, nh, gc ? " (mapped: axes via GameController)" : "");

    /* Some pads send no report until pressed, so the first report can make a
     * trigger axis "move" alongside the real button. So a button/hat change
     * beats axes in the same poll, endpoint jumps from a zero baseline (or
     * several axes at once) re-baseline, and every change is logged to stderr. */
    for (waited = 0; waited < 10000; waited += 10)
    {
        int btnHit = -1, hatHit = -1, axisHit = -1, axisDir = 0;
        int axisMoved = 0, axisEndpoint = 0;

        SDL_PumpEvents();
        SDL_JoystickUpdate();
        for (i = 0; i < nb; i++)
        {
            if (!baseBtn[i] && SDL_JoystickGetButton(joy, i))
            {
                if (btnHit < 0) btnHit = i;
                portConLogf("ge: capture: button %d down\n", i);
            }
        }
        for (i = 0; i < nh; i++)
        {
            u8 h = SDL_JoystickGetHat(joy, i);
            if (h != baseHat[i] && h != SDL_HAT_CENTERED)
            {
                if (hatHit < 0) hatHit = i;
                portConLogf("ge: capture: hat %d = %d\n", i, h);
            }
        }
        if (gc != NULL)
        {
            /* mapped: SDL axes (triggers rest at 0 here) -> raw token via the mapping */
            for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
            {
                s32 v = SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis) i);
                s32 d = v - gcBase[i];
                if (d > 16000 || d < -16000)
                {
                    char tok[16];
                    portConLogf("ge: capture: sdl axis %s %d -> %d\n",
                            SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis) i), gcBase[i], v);
                    inputAxisToken(gc, (SDL_GameControllerAxis) i, (d < 0) ? -1 : 1, tok, sizeof(tok));
                    if (tok[0] != '-' && btnHit < 0 && hatHit < 0)
                    {
                        printf("ge: capture %s\n", tok);
                        fflush(stdout);
                        exit(0);
                    }
                }
            }
        }
        else
        for (i = 0; i < na; i++)
        {
            s32 v = SDL_JoystickGetAxis(joy, i);
            s32 d = v - baseAxis[i];
            if (d > 16000 || d < -16000)
            {
                portConLogf("ge: capture: axis %d %d -> %d\n", i, baseAxis[i], v);
                axisMoved++;
                if (baseAxis[i] == 0 && (v == -32768 || v == 32767))
                {
                    axisEndpoint++;
                }
                if (axisHit < 0)
                {
                    axisHit = i;
                    axisDir = (d < 0) ? -1 : 1;
                }
            }
        }
        if (btnHit >= 0)
        {
            printf("ge: capture b%d\n", btnHit);
            fflush(stdout);
            exit(0);
        }
        if (hatHit >= 0)
        {
            u8 h = SDL_JoystickGetHat(joy, hatHit);
            printf("ge: capture h%d%c\n", hatHit,
                   (h & SDL_HAT_UP) ? 'u' : (h & SDL_HAT_DOWN) ? 'd'
                   : (h & SDL_HAT_LEFT) ? 'l' : 'r');
            fflush(stdout);
            exit(0);
        }
        if (axisMoved > 0)
        {
            if (axisMoved >= 2 || axisEndpoint > 0)
            {
                /* first report / trigger settling, not a press: re-baseline */
                portConLogf("ge: capture: %d axes settled - re-baselining, keep waiting\n", axisMoved);
                for (i = 0; i < nb; i++) baseBtn[i] = SDL_JoystickGetButton(joy, i);
                for (i = 0; i < na; i++) baseAxis[i] = SDL_JoystickGetAxis(joy, i);
                for (i = 0; i < nh; i++) baseHat[i] = SDL_JoystickGetHat(joy, i);
            }
            else
            {
                printf("ge: capture a%d%c\n", axisHit, (axisDir < 0) ? '-' : '+');
                fflush(stdout);
                exit(0);
            }
        }
        SDL_Delay(10);
    }
    printf("ge: capture none\n");
    fflush(stdout);
    exit(0);
}

/**
 * GE_CALIBRATE=<jidx> (launcher raphnet wizard): guide the user through the 8
 * stick extremes, each confirmed by any button (60s per step, else "ge:
 * calibrate none"). Samples axes 0/1 and prints "ge:
 * calibrate step <n> <NAME>", "ge: calibrate got ..." per sample, then "ge:
 * calibrate done <16 csv ints>" in padN_calib order E,NE,N,NW,W,SW,S,SE.
 */
void portInputCalibratePadAndExit(const char *arg)
{
    static const char *stepname[8] =
    {
        "RIGHT", "UP-RIGHT", "UP", "UP-LEFT",
        "LEFT", "DOWN-LEFT", "DOWN", "DOWN-RIGHT",
    };
    SDL_Joystick *joy;
    s16 vals[8][2];
    int nb;
    int step;
    int i;

    if (arg[0] == 'r')
    {
        /* raphnet RAW calibration (GE_CALIBRATE=r<chan>): sample the true N64
         * stick through the vendor HID path, stored x256 so padN_calib and the
         * deadzone share one scale across backends; approval = any N64 button
         * with the stick deflected. */
        int chan = SDL_atoi(arg + 1);
        s16 rvals[8][2];
        int rstep;
        static const char *rname[8] =
        {
            "RIGHT", "UP-RIGHT", "UP", "UP-LEFT",
            "LEFT", "DOWN-LEFT", "DOWN", "DOWN-RIGHT",
        };

        portRaphnetProbeMode();   /* a running game shares the adapter */
        if (!portRaphnetAvailable())
        {
            printf("ge: calibrate none\n");
            fflush(stdout);
            exit(0);
        }
        for (rstep = 0; rstep < 8; rstep++)
        {
            int waited;
            int prevheld = 1; /* require a release before each approve */

            printf("ge: calibrate step %d %s\n", rstep, rname[rstep]);
            fflush(stdout);
            for (waited = 0; waited < 60000; waited += 10)
            {
                u16 rb;
                s8 rx8, ry8;

                if (portRaphnetPoll(chan, &rb, &rx8, &ry8) == 1)
                {
                    int held = (rb != 0);
                    if (!held)
                    {
                        prevheld = 0;
                    }
                    else if (!prevheld
                             && (rx8 > 15 || rx8 < -15 || ry8 > 15 || ry8 < -15))
                    {
                        rvals[rstep][0] = (s16)(rx8 * 256);
                        rvals[rstep][1] = (s16)(ry8 * 256);
                        printf("ge: calibrate got %d %d %d\n", rstep,
                               rvals[rstep][0], rvals[rstep][1]);
                        fflush(stdout);
                        break;
                    }
                }
                SDL_Delay(10);
            }
            if (waited >= 60000)
            {
                printf("ge: calibrate none\n");
                fflush(stdout);
                exit(0);
            }
        }
        printf("ge: calibrate done ");
        for (rstep = 0; rstep < 8; rstep++)
        {
            printf("%d,%d%s", rvals[rstep][0], rvals[rstep][1], (rstep < 7) ? "," : "\n");
        }
        fflush(stdout);
        exit(0);
    }

    inputBackendHints();
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0 && SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
    {
        printf("ge: calibrate none\n");
        exit(0);
    }
    SDL_PumpEvents(); SDL_JoystickUpdate();
    joy = SDL_JoystickOpen(SDL_atoi(arg));
    if (joy == NULL)
    {
        printf("ge: calibrate none\n");
        fflush(stdout);
        exit(0);
    }
    nb = SDL_JoystickNumButtons(joy);
    if (nb > 64)
    {
        nb = 64;
    }

    for (step = 0; step < 8; step++)
    {
        int waited;
        int pressed;

        printf("ge: calibrate step %d %s\n", step, stepname[step]);
        fflush(stdout);

        /* wait for all buttons released, then a press with the stick
         * meaningfully deflected (>= ~20%% so a stray tap at centre
         * can't record a dead point) */
        for (waited = 0; waited < 60000; waited += 10)
        {
            SDL_PumpEvents(); SDL_JoystickUpdate();
            pressed = 0;
            for (i = 0; i < nb; i++)
            {
                if (SDL_JoystickGetButton(joy, i))
                {
                    pressed = 1;
                    break;
                }
            }
            if (!pressed)
            {
                break;
            }
            SDL_Delay(10);
        }
        for (; waited < 60000; waited += 10)
        {
            s32 x, y;

            SDL_PumpEvents(); SDL_JoystickUpdate();
            x = SDL_JoystickGetAxis(joy, 0);
            y = SDL_JoystickGetAxis(joy, 1);
            pressed = 0;
            for (i = 0; i < nb; i++)
            {
                if (SDL_JoystickGetButton(joy, i))
                {
                    pressed = 1;
                    break;
                }
            }
            if (pressed && ((x > 6000 || x < -6000) || (y > 6000 || y < -6000)))
            {
                vals[step][0] = (s16) x;
                vals[step][1] = (s16) y;
                printf("ge: calibrate got %d %d %d\n", step, x, y);
                fflush(stdout);
                break;
            }
            SDL_Delay(10);
        }
        if (waited >= 60000)
        {
            printf("ge: calibrate none\n");
            fflush(stdout);
            exit(0);
        }
    }

    printf("ge: calibrate done ");
    for (step = 0; step < 8; step++)
    {
        printf("%d,%d%s", vals[step][0], vals[step][1], (step < 7) ? "," : "\n");
    }
    fflush(stdout);
    exit(0);
}

/* ---- midas.ini settings ---------------------------------------------------- */

/**
 * video.c's ini parser hands unrecognized [controls]/[input] keys here.
 * Returns 1 if the key was consumed.
 */
s32 portInputSetOption(const char *key, const char *value)
{
    if (SDL_strcmp(key, "mouse") == 0)              { s_OptMouse = SDL_atoi(value); return 1; }
    if (SDL_strcmp(key, "mouse_mode") == 0)         { return 1; } /* retired: direct only, accepted and
                                                                   * ignored */
    if (SDL_strcmp(key, "smart_mouse_lock") == 0)   { s_OptSmartLock = SDL_atoi(value); return 1; }
    if (SDL_strcmp(key, "p1_hybrid") == 0)          { s_OptP1Hybrid = SDL_atoi(value); return 1; }
    if (SDL_strcmp(key, "p1_view") == 0)            { s_OptP1View = (SDL_strcasecmp(value, "pad") == 0); return 1; }
    if (SDL_strcmp(key, "p1_settings_for") == 0)    { SDL_strlcpy(s_P1SettingsFor, value, sizeof(s_P1SettingsFor)); return 1; }
    if (SDL_strcmp(key, "mouse_sens_x") == 0)       { s_OptMouseSensX = (float)SDL_atof(value); return 1; }
    if (SDL_strcmp(key, "mouse_sens_y") == 0)       { s_OptMouseSensY = (float)SDL_atof(value); return 1; }
    if (SDL_strcmp(key, "mouse_sens_aim") == 0)     { s_OptMouseSensAim = (float)SDL_atof(value); return 1; }
    if (SDL_strcmp(key, "right_stick_c") == 0)      { return 1; } /* retired, accepted and ignored */
    if (SDL_strcmp(key, "deadzone") == 0)           { s_OptDeadzone = SDL_clamp(SDL_atoi(value), 0, 32767); return 1; } /* 32768 would divide by zero */
    if (SDL_strcmp(key, "deadzone_look") == 0)      { s_OptDeadzoneLook = SDL_clamp(SDL_atoi(value), 0, 32767); return 1; }
    if (SDL_strcmp(key, "trigger_threshold") == 0)  { s_OptTrigThreshold = SDL_atoi(value); return 1; }
    if (SDL_strcmp(key, "stick_scale") == 0)        { s_OptStickScale = (float)SDL_atof(value); return 1; }
    if (SDL_strcmp(key, "twin_stick") == 0)         { return 1; } /* retired, accepted and ignored */
    if (SDL_strcmp(key, "pads") == 0)               { s_OptPads = SDL_atoi(value); return 1; }
    if (SDL_strcmp(key, "rawinput") == 0)           { s_OptRawInput = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcmp(key, "hidapi") == 0)             { s_OptHidapi = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcmp(key, "wgi") == 0)                { s_OptWgi = SDL_atoi(value) != 0; s_OptWgiExplicit = 1; return 1; }
    if (SDL_strcmp(key, "xinput") == 0)             { s_OptXInput = SDL_atoi(value) != 0; return 1; }
    if (SDL_strcmp(key, "rumble") == 0)             { s_OptRumble = SDL_atoi(value); return 1; }
    if (SDL_strcmp(key, "show_input") == 0)         { s_OptShowInput = SDL_atoi(value); return 1; }
    if (SDL_strncmp(key, "raphnet", 7) == 0 && (key[7] == '0' || key[7] == '1')
        && SDL_strcmp(key + 8, "_style") == 0)
    {   /* raphnet<chan>_style: auto, 1.1 .. 2.4, 2.1p2 .. 2.4p2 (see s_RaphnetStyle) */
        int st;

        s_RaphnetStyle[key[7] - '0'] = -1;
        for (st = 0; st < PAD_STYLE_COUNT; st++)
        {
            if (SDL_strcasecmp(value, sPadStyleNames[st]) == 0)
            {
                s_RaphnetStyle[key[7] - '0'] = st;
                break;
            }
        }
        return 1;
    }
    if (SDL_strcmp(key, "dinput") == 0)             { s_OptDInput = SDL_atoi(value); return 1; }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '\0')
    {
        SDL_strlcpy(s_OptPadAssign[key[3] - '1'], value, sizeof(s_OptPadAssign[0]));
        if (SDL_strlen(value) >= 12 && SDL_strncasecmp(value + 8, "9b28", 4) == 0)
        {   /* a pin to the raphnet's plain joystick face (VID 0x289B): that
                     * always means the RAW path, channel 0 unless the ini says
                     * raphnet<n> itself */
            SDL_strlcpy(s_OptPadAssign[key[3] - '1'], "raphnet0", sizeof(s_OptPadAssign[0]));
            portConLogf("ge: input: %s=%s is a raphnet's joystick face - taken as raphnet0 (the RAW path)\n", key, value);
        }
        return 1;
    }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_')
    {
        int slot = key[3] - '1';
        int i;
        if (SDL_strcmp(key + 5, "rumble") == 0)
        {
            s_PadRumbleOn[slot] = SDL_atoi(value);
            return 1;
        }
        if (SDL_strcmp(key + 5, "swap_sticks") == 0)
        {
            s_PadSwapOpt[slot] = SDL_atoi(value) != 0;
            inputSwapLog(slot);
            return 1;
        }
        if (SDL_strcmp(key + 5, "lr_classic") == 0)
        {
            s_PadLrClassic[slot] = SDL_atoi(value) != 0;
            return 1;
        }
        if (SDL_strcmp(key + 5, "gadget_switch") == 0)
        {
            s_PadGadgetSwitch[slot] = SDL_atoi(value) != 0;
            if (s_PadGadgetSwitch[slot])
            {
                portConLogf("ge: input: player %d gadget_switch=1 - B then Z = next gadget\n", slot + 1);
            }
            return 1;
        }
        if (SDL_strcmp(key + 5, "stickmode") == 0)
        {
            /* accepted and IGNORED (old inis): the mode is derived from the
             * device now - see STICKMODE_*; calibration = padN_calib */
            if (SDL_strcasecmp(value, "calibrated") != 0)
            {
                portConLogf("ge: input: pad%d_stickmode=%s ignored - raphnet-class adapters are raw 1:1, "
                                "every other controller is scaled (padN_calib = calibrated)\n", slot + 1, value);
            }
            return 1;
        }
        if (SDL_strcmp(key + 5, "calib") == 0)
        {
            const char *p = value;
            for (i = 0; i < 16 && *p != '\0'; i++)
            {
                s_PadCalib[slot][i / 2][i & 1] = (s16) SDL_atoi(p);
                p = SDL_strchr(p, ',');
                if (p == NULL)
                {
                    break;
                }
                p++;
            }
            s_PadCalibSet[slot] = (i >= 15); /* all 16 values present -> calibrated (inputDeriveStickMode) */
            return 1;
        }
        if (SDL_strcmp(key + 5, "style") == 0)
        {   /* auto, or 1.1 .. 2.4, or 2.1p2 .. 2.4p2 */
            s_PadStyle[slot] = -1;
            for (i = 0; i < PAD_STYLE_COUNT; i++)
            {
                if (SDL_strcasecmp(value, sPadStyleNames[i]) == 0)
                {
                    s_PadStyle[slot] = i;
                    break;
                }
            }
            return 1;
        }
        if (SDL_strcmp(key + 5, "inputmode") == 0)
        {   /* auto (guess from the hardware) / port / n64 */
            s_PadInputMode[slot] = (SDL_strcasecmp(value, "port") == 0) ? 1
                                 : (SDL_strcasecmp(value, "n64") == 0) ? 2 : 0;
            return 1;
        }
        if (SDL_strcmp(key + 5, "layout") == 0)
        {   /* which of the layouts that match this pad it is on */
            SDL_strlcpy(s_PadLayoutWant[slot], value, sizeof(s_PadLayoutWant[0]));
            return 1;
        }
        if (SDL_strcmp(key + 5, "layout_user") == 0)
        {   /* which copy of that name: 1 the player's, 0 shipped, -1 unknown */
            s_PadLayoutUser[slot] = (value[0] == '\0') ? -1 : SDL_atoi(value);
            return 1;
        }
        for (i = 0; i < PM_COUNT; i++)
        {
            if (SDL_strcmp(key + 5, sPadMapNames[i]) == 0)
            {
                inputSetMapList(slot, i, value, 1);
                return 1;
            }
        }
        return 0;
    }
    return 0;
}


/* ---- device picker + bind readback -------------------------------------
 *
 * The watch's Control Options tab starts with "controller: <device>" and lists
 * what that device's actions are bound to; these functions expose the per-slot
 * map, raphnet flag and SDL handles to the game side.
 *
 * Device numbering, as the menu sees it:
 *   0            mouse and keyboard (the midas.ini [controls] binds, video.c)
 *   1 .. n       the CONNECTED pad slots, in slot order
 * So a device index is a position in a list that shrinks when a pad is
 * unplugged; the menu must not index by slot, since pad slots are deliberately
 * never renumbered mid-match (see inputAssignSlotsEx).
 */

/** Pad slot behind menu device `dev` (dev >= 1), or -1. */
s32 portInputDeviceSlot(s32 dev)
{
    int slot;
    int seen = 0;

    if (dev <= 0)
    {
        return -1;
    }
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        if (inputSlotBusy(slot))
        {
            seen++;
            if (seen == dev)
            {
                return slot;
            }
        }
    }
    return -1;
}

/** 1 (mouse and keyboard) plus every connected pad. */
s32 portInputDeviceCount(void)
{
    int slot;
    s32 n = 1;

    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        if (inputSlotBusy(slot))
        {
            n++;
        }
    }
    return n;
}

/** What the device row shows. Never NULL. */
const char *portInputDeviceLabel(s32 dev)
{
    static char label[64];
    s32 slot;

    if (dev <= 0)
    {
        return "Mouse and Keyboard\n";
    }
    slot = portInputDeviceSlot(dev);
    if (slot < 0)
    {
        return "?\n";
    }
    if (s_PadRaphnet[slot] != 0)
    {
        SDL_snprintf(label, sizeof(label), "Raphnet Ch%d\n", s_PadRaphnet[slot]);
        return label;
    }
    if (s_Pads[slot] != NULL)
    {
        SDL_snprintf(label, sizeof(label), "%s\n", SDL_GameControllerName(s_Pads[slot]));
    }
    else if (s_Joys[slot] != NULL)
    {
        SDL_snprintf(label, sizeof(label), "%s\n", SDL_JoystickName(s_Joys[slot]));
    }
    else
    {
        SDL_snprintf(label, sizeof(label), "Player %d\n", slot + 1);
    }
    {   /* ASCII only: the font draws nothing for UTF-8 but measures
             * it wide, which clipped the name off the watch row. One '?' per
             * character, and the '\n' kept even when a long name was cut. */
        char *r = label;
        char *w = label;

        for (; *r != '\0' && *r != '\n'; r++)
        {
            const unsigned char ch = (unsigned char) *r;

            if (ch >= 0x80 && ch < 0xC0)
            {
                continue;   /* a continuation byte: its lead already became '?' */
            }
            *w++ = (ch >= 0x80) ? '?' : (char) ch;
        }
        if (w > label + sizeof(label) - 2)
        {
            w = label + sizeof(label) - 2;
        }
        *w++ = '\n';
        *w = '\0';
    }
    return label;
}

/** Raphnet adapters cannot be remapped; they keep the N64 control styles. */
s32 portInputDeviceIsRaphnet(s32 dev)
{
    s32 slot = portInputDeviceSlot(dev);

    return (slot >= 0) && (s_PadRaphnet[slot] != 0);
}

/**
 * 1 = this device is an N64 controller or derivative, so the N64 control styles
 * and bindings mean something on it.
 *
 * A raphnet adapter always is. Anything else must say so with the "n64" match
 * term on its layout line (shipped for NSO N64, 8BitDo 64, ModRetro M64 and the
 * two Standard layouts, whose C buttons sit on the right stick with no Look
 * stick). Last resort is inputIsN64Pad's name test, so an untagged pad that
 * calls itself N64 isn't mis-classed. Device 0 (keyboard and mouse) is never
 * N64. See docs/port/port-inputs-vs-n64-inputs.md.
 */
/* Can port mode turn this MAPPED pad at all? Port mode turns only from the Look
 * stick's X or the look_left / look_right rows, so a pad with no right stick
 * (single Joy-Con, SNES-style) would be unable to turn if the auto guess made
 * it port-driven. */
static int inputSlotCanTurn(int slot)
{
    return s_PadMap[slot][PM_LOOK_X].kind == 'a'
        || s_PadMap[slot][PM_LOOK_LEFT].kind != 0 || s_PadMap[slot][PM_LOOK_RIGHT].kind != 0;
}

/** The same question by PAD SLOT, which is what the game side has. */
s32 portInputPadIsN64(s32 slot)
{
    s32 why;

    if (slot < 0 || slot >= INPUT_MAX_PADS)
    {
        return 0;
    }
    /* An explicit n64 counts only while something occupies the slot (a leftover
     * setting must not make a keyboard-only player an N64 pad), and a raphnet
     * RAW slot is ALWAYS N64 regardless of padN_inputmode; saved values are
     * kept for the next device in the slot. */
    why = (s_PadRaphnet[slot] != 0) ? 1
        : (s_PadInputMode[slot] == 1) ? 4
        : (s_PadInputMode[slot] == 2 && inputSlotBusy(slot)) ? 5
        : (s_PadIsN64[slot] != 0 && inputSlotBusy(slot)) ? 2   /* the same occupancy rule: the tag
                                                                * outlives an unplug */
        : ((s_Pads[slot] != NULL) && inputIsN64Pad(s_Pads[slot])) ? 3
        : ((s_Pads[slot] != NULL) && !inputSlotCanTurn(slot)) ? 6 : 0;

    { /* Log once per slot, with the reason, whether this pad is N64-styled or
       * port-driven: the routes (raphnet pin, "n64" layout tag, name test) are
       * not equivalent, and landing on the wrong side looks like "my pad no
       * longer works". */
        static s32 said[INPUT_MAX_PADS];   /* why + 1, so a change re-logs */
        static const char *const reason[7] = {
            "PORT-DRIVEN (guessed: no raphnet flag, no n64 layout tag, name has no 64)",
            "N64-class via the raphnet RAW flag (padN=raphnet<chan>; always N64, padN_inputmode ignored)",
            "N64-class via its layout's n64 tag",
            "N64-class via its SDL name",
            "PORT-DRIVEN because the player chose it (padN_inputmode=port)",
            "N64 control styles because the player chose it (padN_inputmode=n64)",
            "N64 control styles (guessed: no Look stick or look left/right bound, port mode could not turn)"
        };

        if (said[slot] != why + 1)
        {
            said[slot] = why + 1;
            portConLogf("ge: input: player %d is %s\n", (int) slot + 1, reason[why]);
        }
    }
    return (why != 0) && (why != 4);
}

/** -1 = auto (the save and the promotion decide), else 0..7 = 1.1 .. 2.4.
 * A P2 slot answers -1 here: it is somebody's second controller, not a
 * player with a style (portInputStyleRaw has the full code). */
s32 portInputStyleGet(s32 slot)
{
    const s32 s = *inputStyleCell(slot);

    return (s >= PAD_STYLE_P2_FIRST) ? -1 : s;
}

/** The stored code, -1 or 0..11, P2 variants included - for the menu. */
s32 portInputStyleRaw(s32 slot)
{
    return *inputStyleCell(slot);
}

/**
 * bondview2.c, the 2.x second-pad read: which pad index is player `playernum`'s
 * second controller for 2.x style family `fam` (0 = 2.1 .. 3 = 2.4)? The k-th
 * slot marked "2.xp2" for that family serves the k-th player on it, in slot
 * order, skipping the player's own slot; -1 = none marked, so use the console's
 * fixed port. */
s32 portInputSecondPadIndex(s32 playernum, s32 fam)
{
    extern s32 getPlayerCount(void);
    extern u32 get_player_control_style(s32 playernum);
    const s32 count = getPlayerCount();
    int slot;
    int k = 0;
    int rank = 0;
    int p;

    /* "the k-th player" counts only players ON this 2.x family (their own pick,
     * else the MP setup's style), not the raw player number, otherwise player
     * 2's only marked pad served nobody unless player 1 also had one. */
    for (p = 0; p < playernum && p < INPUT_MAX_PADS; p++)
    {
        s32 st = portInputStyleGet(p);

        if (st < 0 && count >= 2)
        {
            st = (s32) get_player_control_style(p);
        }
        if (st == (PAD_STYLE_P2_FIRST - 4) + fam)   /* 4 = 2.1, the first 2.x */
        {
            rank++;
        }
    }
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        /* never a SEATED player's pad: in MP slots 0..count-1 drive their
         * own Bonds, and a marked one would drive two players at once */
        if (slot >= count && slot != playernum && *inputStyleCell(slot) == PAD_STYLE_P2_FIRST + fam
            && inputSlotBusy(slot))
        {
            if (k == rank)
            {
                return slot;
            }
            k++;
        }
    }
    return -1;
}

/** The style's ini spelling: "auto" for -1, else "1.1" .. "2.4p2". */
const char *portInputStyleName(s32 code)
{
    return (code >= 0 && code < PAD_STYLE_COUNT) ? sPadStyleNames[code] : "auto";
}

/** ...and back: -1 for "auto" or anything unknown. */
s32 portInputStyleCode(const char *name)
{
    int i;

    for (i = 0; name != NULL && i < PAD_STYLE_COUNT; i++)
    {
        if (SDL_strcasecmp(name, sPadStyleNames[i]) == 0)
        {
            return i;
        }
    }
    return -1;
}

/** The menu DEVICE index of the pad in this slot, or -1 when empty (never 0:
  * the keyboard has no slot of its own).
 * MP setup and profiles think in slots, layout and style calls take devices;
 * don't keep the answer, it shifts on hotplug. */
s32 portInputSlotDevice(s32 slot)
{
    s32 d;

    for (d = 1; d < portInputDeviceCount(); d++)
    {
        if (portInputDeviceSlot(d) == slot)
        {
            return d;
        }
    }
    return -1;
}

/** By DEVICE index, for the menu: the full code, P2 variants included. */
s32 portInputDeviceStyleGet(s32 dev)
{
    return portInputStyleRaw(portInputDeviceSlot(dev));
}

/* by SLOT, for the device records too: the style cell and its own ini key */
static void inputSlotStyleSet(int slot, int style)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);
    char key[20];

    *inputStyleCell(slot) = style;
    if (s_PadRaphnet[slot] >= 1 && s_PadRaphnet[slot] <= 2)
    {   /* the adapter's own key - the slot's padN_style stays the other device's */
        SDL_snprintf(key, sizeof(key), "raphnet%d_style", s_PadRaphnet[slot] - 1);
    }
    else
    {
        SDL_snprintf(key, sizeof(key), "pad%d_style", (int) slot + 1);
    }
    portVideoIniSetKey(key, (style < 0) ? "auto" : sPadStyleNames[style]);
}

void portInputDeviceStyleSet(s32 dev, s32 style)
{
    const s32 slot = portInputDeviceSlot(dev);

    if (slot < 0 || style < -1 || style >= PAD_STYLE_COUNT)
    {
        return;
    }
    inputSlotStyleSet(slot, style);
}

/**
 * The device picker's view of the choice above: 1 = this device is driven
 * by the N64 control styles right now, whether that was chosen or guessed.
 * dev 0 (mouse and keyboard) has no pad slot and is always port-driven.
 */
s32 portInputDeviceModeIsN64(s32 dev)
{
    const s32 slot = portInputDeviceSlot(dev);

    return (slot < 0) ? 0 : portInputPadIsN64(slot);
}

/**
 * The CHOICE alone, not the guess: padN_inputmode as the player set
 * it - 0 auto, 1 port, 2 n64. 0 for device 0 and for a raphnet, which is
 * always N64 whatever the key says. A profile stores only this.
 */
s32 portInputDeviceModeRaw(s32 dev)
{
    const s32 slot = portInputDeviceSlot(dev);

    return (slot < 0 || s_PadRaphnet[slot] != 0) ? 0 : s_PadInputMode[slot];
}

/** Set it, and remember it: padN_inputmode in midas.ini [input]. */
void portInputDeviceModeSet(s32 dev, s32 isN64)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);
    const s32 slot = portInputDeviceSlot(dev);
    char key[24];

    if (slot < 0)
    {
        return;
    }
    if (s_PadRaphnet[slot] != 0)
    {
        return;   /* a raphnet is always N64 (portInputPadIsN64) - the row cannot move it */
    }
    /* Always written as an EXPLICIT port/n64, never back to auto: the player
     * touching this row is the whole point - the guess does not get to
     * change its mind on the next launch. */
    s_PadInputMode[slot] = isN64 ? 2 : 1;
    SDL_snprintf(key, sizeof(key), "pad%d_inputmode", (int) slot + 1);
    portVideoIniSetKey(key, isN64 ? "n64" : "port");
}

s32 portInputDeviceIsN64(s32 dev)
{
    s32 slot = portInputDeviceSlot(dev);

    if (slot < 0)
    {
        return 0;
    }
    if (s_PadRaphnet[slot] != 0 || s_PadIsN64[slot] != 0)
    {
        return 1;
    }
    return (s_Pads[slot] != NULL) && inputIsN64Pad(s_Pads[slot]);
}

/**
 * A map entry back into its midas.ini token ("b0", "a2+", "h0u", "-").
 * The inverse of inputParseMapToken; keep the two in step.
 */
static void inputMapTokenText(const PadMapEntry *e, char *buf, int len)
{
    switch (e->kind)
    {
    case 'b':
        SDL_snprintf(buf, (size_t) len, "b%d", (int) e->idx);
        break;
    case 'a':
        SDL_snprintf(buf, (size_t) len, "a%d%c", (int) e->idx, (e->dir < 0) ? '-' : '+');
        break;
    case 'h':
        SDL_snprintf(buf, (size_t) len, "h%d%c", (int) e->idx,
                     (e->dir & SDL_HAT_UP) ? 'u' : (e->dir & SDL_HAT_DOWN) ? 'd'
                   : (e->dir & SDL_HAT_LEFT) ? 'l' : 'r');
        break;
    default:
        SDL_strlcpy(buf, "-", (size_t) len);
        break;
    }
}

/* ---- WHAT THE BUTTON IS CALLED ON THE PAD IN YOUR HANDS -------------------
 *
 * Bind rows show the name printed on the pad (A, Cross, Triangle), not SDL's
 * developer strings ("a", "leftshoulder") or raw indices ("b0"). The name
 * depends on the pad's FAMILY: SDL's "a" is A on Xbox, Cross on PlayStation,
 * and B on Nintendo (A and B are mirrored), and naming the wrong button is
 * worse than showing "b0".
 *
 * This is the single copy of the table; the launcher probe's printer calls it
 * too, so the two can't drift.
 */
static int inputPadStyle(SDL_GameController *gc)
{
    SDL_GameControllerType t = gc ? SDL_GameControllerGetType(gc) : SDL_CONTROLLER_TYPE_UNKNOWN;

    if (t == SDL_CONTROLLER_TYPE_PS3 || t == SDL_CONTROLLER_TYPE_PS4
#if SDL_VERSION_ATLEAST(2, 0, 14)
        || t == SDL_CONTROLLER_TYPE_PS5
#endif
        )
    {
        return PADSTYLE_PS;
    }
    if (t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO
#if SDL_VERSION_ATLEAST(2, 24, 0)
        || t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT
        || t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT
        || t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR
#endif
        )
    {
        return PADSTYLE_NINTENDO;
    }
    return PADSTYLE_XBOX;
}

/** What this pad calls that button, or NULL when even SDL has no name. */
static const char *inputPadButtonName(int style, SDL_GameControllerButton b)
{
    /* The A/B mirror is only right while SDL reports Nintendo buttons by
     * POSITION; SDL's default is by LABEL, so mirror only when that hint says
     * positional (otherwise Use read "B" on a Switch Pro whose A worked it). */
    if (style == PADSTYLE_NINTENDO
        && SDL_GetHintBoolean(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, SDL_TRUE)
        && (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_B
            || b == SDL_CONTROLLER_BUTTON_X || b == SDL_CONTROLLER_BUTTON_Y))
    {
        return (b == SDL_CONTROLLER_BUTTON_A) ? "A" : (b == SDL_CONTROLLER_BUTTON_B) ? "B"
             : (b == SDL_CONTROLLER_BUTTON_X) ? "X" : "Y";
    }
    switch (b)
    {
    case SDL_CONTROLLER_BUTTON_A:
        return (style == PADSTYLE_PS) ? "Cross" : (style == PADSTYLE_NINTENDO) ? "B" : "A";
    case SDL_CONTROLLER_BUTTON_B:
        return (style == PADSTYLE_PS) ? "Circle" : (style == PADSTYLE_NINTENDO) ? "A" : "B";
    case SDL_CONTROLLER_BUTTON_X:
        return (style == PADSTYLE_PS) ? "Square" : (style == PADSTYLE_NINTENDO) ? "Y" : "X";
    case SDL_CONTROLLER_BUTTON_Y:
        return (style == PADSTYLE_PS) ? "Triangle" : (style == PADSTYLE_NINTENDO) ? "X" : "Y";
    case SDL_CONTROLLER_BUTTON_BACK:
        return (style == PADSTYLE_PS) ? "Share" : (style == PADSTYLE_NINTENDO) ? "Minus" : "Back";
    case SDL_CONTROLLER_BUTTON_GUIDE:
        return (style == PADSTYLE_PS) ? "PS" : (style == PADSTYLE_NINTENDO) ? "Home" : "Guide";
    case SDL_CONTROLLER_BUTTON_START:
        return (style == PADSTYLE_PS) ? "Options" : (style == PADSTYLE_NINTENDO) ? "Plus" : "Start";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        return (style == PADSTYLE_PS) ? "L3" : "L stick click";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        return (style == PADSTYLE_PS) ? "R3" : "R stick click";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return (style == PADSTYLE_PS) ? "L1" : (style == PADSTYLE_NINTENDO) ? "L" : "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return (style == PADSTYLE_PS) ? "R1" : (style == PADSTYLE_NINTENDO) ? "R" : "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    return "D-Pad Up";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return "D-Pad Down";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return "D-Pad Left";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-Pad Right";
#if SDL_VERSION_ATLEAST(2, 0, 14)
    /* not SDL's developer strings "misc1" / "paddle1" / "touchpad".
     * PADDLE1..4 are the Elite's P1, P3, P2, P4 in SDL's enum order. */
    case SDL_CONTROLLER_BUTTON_MISC1:
        return (style == PADSTYLE_PS) ? "Mic" : (style == PADSTYLE_NINTENDO) ? "Capture" : "Share";
    case SDL_CONTROLLER_BUTTON_PADDLE1:  return "P1";
    case SDL_CONTROLLER_BUTTON_PADDLE2:  return "P3";
    case SDL_CONTROLLER_BUTTON_PADDLE3:  return "P2";
    case SDL_CONTROLLER_BUTTON_PADDLE4:  return "P4";
    case SDL_CONTROLLER_BUTTON_TOUCHPAD: return "Touchpad";
#endif
    default:
        return SDL_GameControllerGetStringForButton(b);
    }
}

/** What this pad calls that axis pushed that way. `dir` < 0 = left / up. */
static const char *inputPadAxisName(int style, SDL_GameControllerAxis a, int dir)
{
    switch (a)
    {
    case SDL_CONTROLLER_AXIS_LEFTX:  return (dir < 0) ? "L stick Left" : "L stick Right";
    case SDL_CONTROLLER_AXIS_LEFTY:  return (dir < 0) ? "L stick Up"   : "L stick Down";
    case SDL_CONTROLLER_AXIS_RIGHTX: return (dir < 0) ? "R stick Left" : "R stick Right";
    case SDL_CONTROLLER_AXIS_RIGHTY: return (dir < 0) ? "R stick Up"   : "R stick Down";
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        return (style == PADSTYLE_PS) ? "L2" : (style == PADSTYLE_NINTENDO) ? "ZL" : "LT";
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        return (style == PADSTYLE_PS) ? "R2" : (style == PADSTYLE_NINTENDO) ? "ZR" : "RT";
    default:
        return SDL_GameControllerGetStringForAxis(a);
    }
}

/**
 * The SDL control name for a map entry ("a", "leftshoulder", "lefttrigger+"):
 * the machine form, used so a saved layout can be resolved onto a DIFFERENT pad
 * of the same family (raw tokens would pin it to one device). 0 = no SDL name;
 * the caller writes the raw token.
 */
static int inputMapEntrySdlName(int slot, const PadMapEntry *e, char *buf, int len)
{
    SDL_GameController *gc = s_Pads[slot];
    int i;

    if (gc == NULL || e->kind == 0)
    {
        return 0;
    }
    if (e->kind == 'b')
    {
        for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        {
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForButton(gc, (SDL_GameControllerButton) i);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && b.value.button == e->idx)
            {
                const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton) i);
                if (n != NULL)
                {
                    SDL_strlcpy(buf, n, (size_t) len);
                    return 1;
                }
            }
        }
        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
        {   /* a trigger SDL maps to a raw BUTTON ("lefttrigger:b8"): save it by
                     * name, or a b8 lands on whatever b8 is on the next pad */
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForAxis(gc, (SDL_GameControllerAxis) i);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && b.value.button == e->idx)
            {
                const char *n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis) i);
                if (n != NULL)
                {
                    SDL_snprintf(buf, (size_t) len, "%s+", n);
                    return 1;
                }
            }
        }
        return 0;
    }
    if (e->kind == 'a')
    {
        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
        {
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForAxis(gc, (SDL_GameControllerAxis) i);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && b.value.axis == e->idx)
            {
                const char *n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis) i);
                const int d = inputAxisInverted(gc, (SDL_GameControllerAxis) i) ? -e->dir : e->dir; /* honour '~' inverted axes */
                if (n != NULL)
                {
                    SDL_snprintf(buf, (size_t) len, "%s%c", n, (d < 0) ? '-' : '+');
                    return 1;
                }
            }
        }
        return 0;
    }
    if (e->kind == 'h')
    {   /* a hat d-pad by its SDL name: a saved "h0u" did nothing on a pad
             * whose d-pad is buttons (e.g. a DualSense under HIDAPI) */
        for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        {
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForButton(gc, (SDL_GameControllerButton) i);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_HAT && b.value.hat.hat == e->idx
                && (b.value.hat.hat_mask & e->dir) != 0)
            {
                const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton) i);
                if (n != NULL)
                {
                    SDL_strlcpy(buf, n, (size_t) len);
                    return 1;
                }
            }
        }
        return 0;
    }
    return 0;
}

/**
 * The friendly name of a map entry, walked back from its raw joystick index
 * through the pad's SDL mapping ("a", "leftshoulder"...), so players don't read
 * "b0". Returns 0 when no name is available and the caller should fall back to
 * the raw token.
 */
static int inputMapEntryFriendly(int slot, const PadMapEntry *e, char *buf, int len)
{
    SDL_GameController *gc = s_Pads[slot];
    int i;

    if (gc == NULL || e->kind == 0)
    {
        return 0;
    }
    if (e->kind == 'b')
    {
        for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        {
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForButton(gc, (SDL_GameControllerButton) i);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && b.value.button == e->idx)
            {
                const char *n = inputPadButtonName(inputPadStyle(gc), (SDL_GameControllerButton) i);
                if (n != NULL)
                {
                    SDL_strlcpy(buf, n, (size_t) len);
                    return 1;
                }
            }
        }
        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
        {   /* a trigger SDL maps to a raw button (8BitDo N64 / SN30 Pro on
                     * Linux, "lefttrigger:b8"): RT / ZR, not "b8" */
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForAxis(gc, (SDL_GameControllerAxis) i);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && b.value.button == e->idx)
            {
                const char *n = inputPadAxisName(inputPadStyle(gc), (SDL_GameControllerAxis) i, 1);
                if (n != NULL)
                {
                    SDL_strlcpy(buf, n, (size_t) len);
                    return 1;
                }
            }
        }
        return 0;
    }
    if (e->kind == 'a')
    {
        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
        {
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForAxis(gc, (SDL_GameControllerAxis) i);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && b.value.axis == e->idx)
            {   /* the NAME already says which way ("L stick Up"), so no +/-;
                             * an inverted ('~') axis names the other end */
                const int d = inputAxisInverted(gc, (SDL_GameControllerAxis) i) ? -e->dir : e->dir;
                const char *n = inputPadAxisName(inputPadStyle(gc), (SDL_GameControllerAxis) i, d);
                if (n != NULL)
                {
                    SDL_strlcpy(buf, n, (size_t) len);
                    return 1;
                }
            }
        }
        return 0;
    }
    if (e->kind == 'h')
    {   /* a hat IS the D-pad on every pad that has one */
        SDL_snprintf(buf, (size_t) len, "D-Pad %s",
                (e->dir & SDL_HAT_UP) ? "Up" : (e->dir & SDL_HAT_DOWN) ? "Down"
              : (e->dir & SDL_HAT_LEFT) ? "Left" : "Right");
        return 1;
    }
    return 0;
}

/**
 * What device `dev` has bound to the control named `name` (an sPadMapNames
 * entry), as display text. 0 = there is no such control for this device.
 * Alternates are appended, so "b0 b4" means either button fires it.
 */
/* The digital move_* / look_* rows and the analog stick_x/y / look_x/y rows are
 * one control to the player: capturing an AXIS on a movement row binds the
 * stick row with that direction (so the stick stays analog), while a button or
 * hat stays digital. */
static int inputDigitalRowStick(const char *name, int *stickPm, int *flip)
{
    static const struct { const char *row; int pm; int flip; } tab[] = {
        { "move_up",    PM_STICK_Y, 0 }, { "move_down",  PM_STICK_Y, 1 },
        { "move_right", PM_STICK_X, 0 }, { "move_left",  PM_STICK_X, 1 },
        { "look_up",    PM_LOOK_Y,  0 }, { "look_down",  PM_LOOK_Y,  1 },
        { "look_right", PM_LOOK_X,  0 }, { "look_left",  PM_LOOK_X,  1 },
    };
    size_t i;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
    {
        if (SDL_strcmp(name, tab[i].row) == 0)
        {
            *stickPm = tab[i].pm;
            *flip = tab[i].flip;
            return 1;
        }
    }
    return 0;
}

/* sPadMapNames index of a control name, -1 if there is no such row */
static int inputPmIndex(const char *name)
{
    int i;

    for (i = 0; i < PM_COUNT; i++)
    {
        if (SDL_strcmp(name, sPadMapNames[i]) == 0)
        {
            return i;
        }
    }
    return -1;
}

/* "a3-" -> "a3+" and back; anything that is not an axis token is left alone */
static void inputAxisTokenFlip(char *tok)
{
    size_t n = SDL_strlen(tok);

    if (n > 1 && tok[0] == 'a' && (tok[n - 1] == '+' || tok[n - 1] == '-'))
    {
        tok[n - 1] = (tok[n - 1] == '+') ? '-' : '+';
    }
}

/* 1 = raw-fallback entry i does nothing on this device (a button, hat or axis
 * it lacks; axes 2/3 are Look only with four axes). The readback and seed both
 * drop these, so a sparse pad shows no phantom rows and the text doesn't change
 * after the first rebind. */
static int inputRawFallbackLacks(int slot, SDL_Joystick *joy, int i)
{
    PadMapEntry e = { 0, 0, 0 };

    inputParseMapToken(sPadRawFallback[i], &e);
    return (e.kind == 'b' && e.idx >= SDL_JoystickNumButtons(joy))
        || (e.kind == 'h' && e.idx >= SDL_JoystickNumHats(joy))
        || (e.kind == 'a' && e.idx >= SDL_JoystickNumAxes(joy))
        || ((i == PM_LOOK_X || i == PM_LOOK_Y) && SDL_JoystickNumAxes(joy) < 4)
        /* the fallback's calibrated and raw stick modes return before the
         * Look stick is committed: axes 2/3 are C buttons only there, and
         * seeding a2+/a3- would make the first rebind twin-stick */
        || ((i == PM_LOOK_X || i == PM_LOOK_Y)
            && ((s_PadStickMode[slot] == STICKMODE_CALIBRATED && s_PadCalibSet[slot])
                || s_PadStickMode[slot] == STICKMODE_RAW));
}

s32 portInputDeviceBindText(s32 dev, const char *name, char *buf, s32 len)
{
    s32 slot = portInputDeviceSlot(dev);
    int i;
    int a;
    int used = 0;
    PadMapEntry fb[PM_COUNT];
    const PadMapEntry *map;

    if (slot < 0 || name == NULL || buf == NULL || len <= 0)
    {
        return 0;
    }
    map = s_PadMap[slot];
    if (s_PadRaphnet[slot] == 0 && s_Pads[slot] == NULL && s_Joys[slot] != NULL
        && !s_PadMapCustom[slot])
    {
        /* no SDL mapping and no padN_ keys: s_PadMap is empty and
         * portInputPadMerge drives this device through its fixed raw
         * fallback, so read THAT back - what the launcher's padmap line
         * prints and what a first rebind would seed */
        SDL_memset(fb, 0, sizeof(fb));
        for (i = 0; i < PM_UNBOUND_FIRST; i++)
        {
            if (!inputRawFallbackLacks(slot, s_Joys[slot], i))
            {
                inputParseMapToken(sPadRawFallback[i], &fb[i]);
            }
        }
        map = fb;
    }
    buf[0] = '\0';
    for (i = 0; i < PM_COUNT; i++)
    {
        char tok[32];
        int spm, flip;

        if (SDL_strcmp(name, sPadMapNames[i]) != 0)
        {
            continue;
        }
        if (map[i].kind != 0)
        {
            if (!inputMapEntryFriendly(slot, &map[i], tok, sizeof(tok)))
            {
                inputMapTokenText(&map[i], tok, sizeof(tok));
            }
            used += SDL_snprintf(buf + used, (size_t) (len - used), "%s", tok);
        }
        for (a = 0; a < PM_MAX_ALT && used < len - 1; a++)
        {
            if (s_PadMapAlt[slot][i][a].kind == 0)
            {
                continue;
            }
            if (!inputMapEntryFriendly(slot, &s_PadMapAlt[slot][i][a], tok, sizeof(tok)))
            {
                inputMapTokenText(&s_PadMapAlt[slot][i][a], tok, sizeof(tok));
            }
            used += SDL_snprintf(buf + used, (size_t) (len - used), " %s", tok);
        }
        if (inputDigitalRowStick(name, &spm, &flip) && map[spm].kind == 'a' && used < len - 1)
        {   /* the stick moves you on this row too (or instead): show it
                     * after the digital tokens, so both binds are displayed */
            PadMapEntry e = map[spm];

            if (flip)
            {
                e.dir = -e.dir;
            }
            if (!inputMapEntryFriendly(slot, &e, tok, sizeof(tok)))
            {
                inputMapTokenText(&e, tok, sizeof(tok));
            }
            used += SDL_snprintf(buf + used, (size_t) (len - used), "%s%s", (used > 0) ? " " : "", tok);
        }
        if (buf[0] == '\0')
        {
            SDL_strlcpy(buf, "unbound", (size_t) len);
        }
        return 1;
    }
    return 0;
}

/* ---- REBINDING A PAD, IN GAME ------------------------------------------
 *
 * The launcher is a dev tool that doesn't ship, so the game rebinds pads itself. Three pieces:
 *
 * 1. SEEDING. Any padN_<control> key makes the slot's custom map replace the
 * built-in layout wholesale, so writing ONE key
 *    would silently unbind everything else. portInputPadSeedMap copies the
 *    device's default map first, and every write goes
 *    through it - get this wrong and "I rebound one button and the pad went dead".
 *
 * 2. CAPTURE. Same rules as portInputCapturePadAndExit (the CLI probe): a
 * button or hat in a poll beats an axis in that poll,
 *    and an axis jumping straight to an endpoint from a zero baseline is a
 *    first report, so it re-baselines. Unlike the probe,
 *    this runs one poll a frame on the slot's already-open device and keeps its
 *    baseline between frames.
 *
 * 3. BLANKING. While a capture is armed the slot contributes NOTHING to pad
 * data (portInputPadMerge returns early), or the
 *    button being captured would also confirm the menu row underneath it.
 */
/* WALL CLOCK, not calls: the Options folder's sheet ticks this at tickhz,
 * so a frame count would time out at different speeds per tick rate */
#define PADCAP_SETTLE_MS  100    /* for the first reports to land before the baseline */
#define PADCAP_TIMEOUT_MS 10000  /* then it gives up like the probe does */
#define PADCAP_AXIS    16000  /* an axis has to move this far to count as a push */

static struct {
    int  slot;                /* -1 = not armed */
    int  settle;
    Uint32 armedAt;           /* SDL ticks at the arm */
    int  done;                /* 1 = finished this arm (token may be empty = nothing) */
    char token[16];
    s16  baseAxis[32];
    s16  gcBase[SDL_CONTROLLER_AXIS_MAX];
    u8   baseBtn[64];
    u8   baseHat[8];
    int  nb, na, nh;
    int  anyBaseDown;         /* a button that was down at the baseline still is */
    Uint32 holdSince;         /* SDL ticks when that hold began, 0 = no hold */
    int  pendBtn;             /* a fresh button press waiting for its release (capture) or a long hold (cancel); -1 none */
    Uint32 pendSince;
} s_PadCap = { -1 };
#define PADCAP_HOLD_MS 2000   /* hold a button this long while armed = unbind */
/* (PADCAP_CANCEL_HOLD_MS is retired: a NEW button held is an unbind
 * too, at PADCAP_HOLD_MS like the arming button; the popup says "Del or Hold to
 * Unbind") */

/* The control a capture just took (or the button a hold-unbind was done
 * with), which the merge keeps off the game until it reads up (see
 * portInputPadMerge). slot -1 = nothing held over. */
static struct { int slot; PadMapEntry entry; } s_PadCapHold = { -1, { 0, 0, 0 } };

s32 portInputPadCaptureActive(void)
{
    return s_PadCap.slot >= 0 && !s_PadCap.done;
}

/**
 * 1 once a button that was down when the capture armed (in practice the A that
 * armed it) has stayed down for PADCAP_HOLD_MS; the caller unbinds the row and
 * cancels. The button is then held over like a captured one, or the blackout
 * would lift under the still-down A and re-arm the row it just cleared.
 */
s32 portInputPadCaptureHoldLong(void)
{
    int i;

    if (s_PadCap.slot >= 0 && !s_PadCap.done && s_PadCap.pendBtn >= 0
        && (Sint32) (SDL_GetTicks() - s_PadCap.pendSince) >= PADCAP_HOLD_MS)
    {   /* A NEW BUTTON HELD = UNBIND. It is still down
             * (portInputPadCaptureTick takes it, and clears pendBtn, on its
             * release); its release is kept off the game like the arming
             * button's. */
        s_PadCapHold.slot = s_PadCap.slot;
        s_PadCapHold.entry.kind = 'b';
        s_PadCapHold.entry.idx = (s16) s_PadCap.pendBtn;
        s_PadCapHold.entry.dir = 0;
        return 1;
    }

    if (!(s_PadCap.slot >= 0 && !s_PadCap.done && s_PadCap.holdSince != 0
          && (Sint32) (SDL_GetTicks() - s_PadCap.holdSince) >= PADCAP_HOLD_MS))
    {
        return 0;
    }
    for (i = 0; i < s_PadCap.nb; i++)
    {
        if (s_PadCap.baseBtn[i])
        {
            s_PadCapHold.slot = s_PadCap.slot;
            s_PadCapHold.entry.kind = 'b';
            s_PadCapHold.entry.idx = (s16) i;
            s_PadCapHold.entry.dir = 0;
            break;
        }
    }
    return 1;
}

/** 1 = this slot is mid-capture, so its input must not reach the game. */
static int inputPadCaptureBlanks(int slot)
{
    return s_PadCap.slot == slot && !s_PadCap.done;
}

void portInputPadCaptureCancel(void)
{
    s_PadCap.slot = -1;
    s_PadCap.done = 0;
    s_PadCap.token[0] = '\0';
}

/**
 * Write every control of this slot into midas.ini as it stands now, unless the
 * slot already has a custom map; without this the first rebind unbinds
 * everything else on the pad (see point 1 of the block comment). One ini write
 * for the lot.
 */
/**
 * Write a whole map to a slot: live, and into midas.ini in ONE pass rather than
 * forty-one in-place rewrites. Shared by the seed below and by applying a
 * layout, because they are the same operation with different values.
 */
static void inputPadWriteVals(int slot, const char *const *in)
{
    extern s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n);
    static char keybuf[PM_COUNT][24];
    static char valbuf[PM_COUNT][64];
    const char *keys[PM_COUNT];
    const char *vals[PM_COUNT];
    int i;

    for (i = 0; i < PM_COUNT; i++)
    {
        SDL_snprintf(keybuf[i], sizeof(keybuf[i]), "pad%d_%s", slot + 1, sPadMapNames[i]);
        SDL_strlcpy(valbuf[i], (in[i] != NULL && in[i][0] != '\0') ? in[i] : "-", sizeof(valbuf[i]));
        keys[i] = keybuf[i];
        vals[i] = valbuf[i];
        inputSetMapList(slot, i, valbuf[i], 1);
    }
    /* The map now belongs to the ini, not the seed: leaving the seeded flag set
     * let inputCloseController wipe every row on a hotplug drop, and the reopen
     * skips seeding a custom map, leaving the pad with nothing bound. */
    s_PadMapSeeded[slot] = 0;
    portIniWriteStrs("[input]", keys, vals, PM_COUNT);
}

static void inputPadWriteAll(int slot, char tok[][16])
{
    const char *vals[PM_COUNT];
    int i;

    for (i = 0; i < PM_COUNT; i++)
    {
        vals[i] = tok[i];
    }
    inputPadWriteVals(slot, vals);
}

/* raw device: the tokens that spell out what inputPrintDefaultMap calls the
 * fallback, less what this device cannot deliver (as the readback shows it).
 * The seed writes them out, and a save of a never-rebound raw pad stores them. */
static void inputRawFallbackTokens(int slot, char tok[][16])
{
    int i;

    for (i = 0; i < PM_UNBOUND_FIRST; i++)
    {
        SDL_strlcpy(tok[i], (s_Joys[slot] != NULL && inputRawFallbackLacks(slot, s_Joys[slot], i)) ? "-" : sPadRawFallback[i], 16);
    }
    for (i = PM_UNBOUND_FIRST; i < PM_COUNT; i++) SDL_strlcpy(tok[i], "-", 16);
    if (s_Joys[slot] != NULL && SDL_JoystickNumAxes(s_Joys[slot]) >= 4)
    {   /* the fallback ALSO presses C from axes 2/3 (portInputPadMerge);
             * without these the first rebind would cost the C stick its C buttons */
        static const char *const cAxis[4] = { "a3-", "a3+", "a2-", "a2+" };

        for (i = 0; i < 4; i++)
        {
            if (tok[PM_CUP + i][0] == '-')
            {
                SDL_strlcpy(tok[PM_CUP + i], cAxis[i], 16);
            }
            else
            {
                SDL_strlcat(tok[PM_CUP + i], ",", 16);
                SDL_strlcat(tok[PM_CUP + i], cAxis[i], 16);
            }
        }
    }
}

s32 portInputPadSeedMap(s32 dev)
{
    char tok[PM_COUNT][16];
    s32 slot = portInputDeviceSlot(dev);
    int i;

    if (slot < 0)
    {
        return 0;
    }
    if (s_PadMapCustom[slot])
    {
        return 1;   /* already every key's own master: nothing to protect */
    }
    if (s_Pads[slot] != NULL && s_PadMapSeeded[slot])
    {   /* the LIVE seeded map, as it stands: re-resolving would pick whatever
             * layout matches first NOW (maybe another player's same-model save)
             * and rewrite this pad from it, n64 class and all */
        static char live[PM_COUNT][64];
        const char *vals[PM_COUNT];
        int a;

        for (i = 0; i < PM_COUNT; i++)
        {
            inputMapTokenText(&s_PadMap[slot][i], live[i], sizeof(live[i]));
            for (a = 0; a < PM_MAX_ALT; a++)
            {
                char one[16];

                if (s_PadMapAlt[slot][i][a].kind == 0)
                {
                    continue;
                }
                inputMapTokenText(&s_PadMapAlt[slot][i][a], one, sizeof(one));
                if (SDL_strcmp(live[i], "-") == 0)
                {
                    SDL_strlcpy(live[i], one, sizeof(live[i]));
                }
                else
                {
                    SDL_strlcat(live[i], ",", sizeof(live[i]));
                    SDL_strlcat(live[i], one, sizeof(live[i]));
                }
            }
            vals[i] = live[i];
        }
        inputPadWriteVals(slot, vals);
        portConLogf("ge: input: player %d had the built-in layout; wrote it out as %d keys so a rebind cannot unbind the rest\n",
                (int) slot + 1, (int) PM_COUNT);
        return 1;
    }
    if (s_Pads[slot] != NULL)
    {   /* ForSlot, NOT the slot-less one: this is the path that WRITES midas.ini, so
         * using the plain lookup here quietly ignored padN_layout and seeded the
         * first match instead of the layout the player had chosen. */
        inputDefaultTokensForSlot(slot, s_Pads[slot], tok);
    }
    else
    {
        inputRawFallbackTokens(slot, tok);
    }
    inputPadWriteAll(slot, tok);
    portConLogf("ge: input: player %d had the built-in layout; wrote it out as %d keys so a rebind cannot unbind the rest\n",
            (int) slot + 1, (int) PM_COUNT);
    return 1;
}

/**
 * Called by video.c's live setter (e.g. the console's `set`) BEFORE writing one
 * padN_<control> key, so an occupied slot is seeded first like an in-game
 * rebind instead of going custom with that key alone. Never from the boot ini
 * parse; 0 = not a pad-control key or no pad.
 */
s32 portInputPadSeedForKey(const char *key)
{
    int slot;
    int i;

    if (key == NULL || SDL_strncmp(key, "pad", 3) != 0 || key[3] < '1' || key[3] > '0' + INPUT_MAX_PADS
        || key[4] != '_')
    {
        return 0;
    }
    slot = key[3] - '1';
    for (i = 0; i < PM_COUNT; i++)
    {
        if (SDL_strcmp(key + 5, sPadMapNames[i]) == 0)
        {
            const s32 dev = portInputSlotDevice(slot);

            return (dev > 0) ? portInputPadSeedMap(dev) : 0;
        }
    }
    return 0;
}

/* ---- LAYOUTS: the saved-bindings side ------------------------------------
 *
 * The list, and applying / saving / deleting one. See the block comment on
 * inputLoadLayouts for why saved bindings and shipped presets are one thing.
 */
s32 portInputLayoutDelete(s32 i);   /* saving replaces a same-named user layout */

s32 portInputLayoutCount(void)
{
    if (s_LayoutCount < 0)
    {
        inputLoadLayouts();
    }
    return s_LayoutCount;
}

const char *portInputLayoutName(s32 i)
{
    if (i < 0 || i >= portInputLayoutCount())
    {
        return "";
    }
    return s_Layouts[i].name;
}

/** 1 = the player saved this one, so it may be overwritten or deleted. */
s32 portInputLayoutIsUser(s32 i)
{
    return (i >= 0 && i < portInputLayoutCount()) ? s_LayoutIsUser[i] : 0;
}

/* An explicit port mode plus a layout with no Look stick can never turn, so
 * when a layout is applied (or a profile loaded) hand the class back to the
 * layout's n64 tag; a mode row picked afterwards still sticks. */
static void inputLayoutReconcileMode(int slot)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);
    char key[24];

    if (s_PadIsN64[slot] && s_PadInputMode[slot] == 1 && s_PadRaphnet[slot] == 0)
    {
        s_PadInputMode[slot] = 0;
        SDL_snprintf(key, sizeof(key), "pad%d_inputmode", (int) slot + 1);
        portVideoIniSetKey(key, "auto");
    }
}

/** port_profiles.c, after a profile load: the same reconcile. */
void portInputLayoutReconcileMode(s32 dev)
{
    const s32 slot = portInputDeviceSlot(dev);

    if (slot >= 0)
    {
        inputLayoutReconcileMode(slot);
    }
}

/** Put layout `i` on device `dev`: every control at once, live and saved. */
s32 portInputLayoutApply(s32 dev, s32 i)
{
    char tok[PM_COUNT][16];
    s32 slot = portInputDeviceSlot(dev);
    int c;

    if (slot < 0 || i < 0 || i >= portInputLayoutCount())
    {
        return 0;
    }
    for (c = 0; c < PM_COUNT; c++)
    {
        if (s_Layouts[i].val[c][0] == '\0')
        {
            SDL_strlcpy(tok[c], "-", 16);   /* unlisted = unbound, as the file's own rule says */
        }
        else if (s_Pads[slot] != NULL)
        {
            inputLayoutResolve(s_Pads[slot], s_Layouts[i].val[c], tok[c], 16);
        }
        else
        {   /* no SDL mapping to resolve through: only raw tokens can land.
             * STRICTLY raw - the parser reads "a" as axis 0 and "b" / "back"
             * as button 0, so an SDL name copied through here does not
             * unbind, it binds the wrong control. */
            const char *v = s_Layouts[i].val[c];

            if ((v[0] == 'b' || v[0] == 'a' || v[0] == 'h') && v[1] >= '0' && v[1] <= '9')
            {
                SDL_strlcpy(tok[c], v, 16);
            }
            else
            {
                SDL_strlcpy(tok[c], "-", 16);
            }
        }
    }
    inputPadWriteAll(slot, tok);
    {   /* remember WHICH one, so the menu can say so and a later reseed agrees */
        extern s32 portVideoIniSetKey(const char *key, const char *value);
        char key[24];

        SDL_strlcpy(s_PadLayoutWant[slot], s_Layouts[i].name, sizeof(s_PadLayoutWant[0]));
        /* and its CLASS: Standard (n64) plays the N64 styles, Pro the port
         * block. Without this, picking Standard left the pad port-driven with
         * no Look stick to turn with */
        s_PadIsN64[slot] = s_Layouts[i].isN64;
        inputLayoutReconcileMode(slot);
        SDL_snprintf(key, sizeof(key), "pad%d_layout", (int) slot + 1);
        portVideoIniSetKey(key, s_Layouts[i].name);
        /* and WHICH copy of that name, or a same-named user layout shadows
         * a shipped one after a restart */
        s_PadLayoutUser[slot] = s_LayoutIsUser[i];
        SDL_snprintf(key, sizeof(key), "pad%d_layout_user", (int) slot + 1);
        portVideoIniSetKey(key, s_LayoutIsUser[i] ? "1" : "0");
    }
    portConLogf("ge: input: layout '%s' applied to player %d\n", s_Layouts[i].name, (int) slot + 1);
    return 1;
}

/**
 * 1 = layout i is written for the pad in this slot (its match terms accept it).
 * MP setup only offers these so a wrong-family layout can't strand a player;
 * the CONTROL sheet offers all. Raw joysticks fit nothing.
 */
s32 portInputLayoutFitsSlot(s32 slot, s32 i)
{
    if (slot < 0 || slot >= INPUT_MAX_PADS || i < 0 || i >= portInputLayoutCount() || s_Pads[slot] == NULL)
    {
        return 0;
    }
    return inputLayoutMatches(&s_Layouts[i], s_Pads[slot]);
}

/** The layout this device is on, or "" when nobody has picked one. */
const char *portInputLayoutActive(s32 dev)
{
    s32 slot = portInputDeviceSlot(dev);

    return (slot >= 0) ? s_PadLayoutWant[slot] : "";
}

/** The bind config's name as a menu shows it: the one picked, else the one the pad
 * was seeded with (a fresh install has picked none), else "". */
const char *portInputLayoutShown(s32 dev)
{
    s32 portInputLayoutInUse(s32 dev);
    const char *a = portInputLayoutActive(dev);
    s32 k;

    if (a != NULL && a[0] != '\0')
    {
        return a;
    }
    k = portInputLayoutInUse(dev);
    return (k >= 0) ? portInputLayoutName(k) : "";
}

/**
 * The INDEX of the layout device `dev` is on, for the Bind Config list's
 * highlight: the one padN_layout / padN_layout_user name (as the seed picks
 * it), else the first matching layout, else -1 (no device, a raw joystick, or
 * nothing matches).
 */
s32 portInputLayoutInUse(s32 dev)
{
    s32 slot = portInputDeviceSlot(dev);
    int k;

    if (slot < 0 || slot >= INPUT_MAX_PADS)
    {
        return -1;
    }
    if (s_LayoutCount < 0)
    {
        inputLoadLayouts();
    }
    if (s_PadLayoutWant[slot][0] != '\0')
    {
        k = inputLayoutWantIndex(slot);
        if (k >= 0)
        {
            return k;
        }
    }
    for (k = 0; s_Pads[slot] != NULL && k < s_LayoutCount; k++)
    {
        if (inputLayoutMatches(&s_Layouts[k], s_Pads[slot]))
        {
            return k;
        }
    }
    return -1;
}

/* ---- PER-DEVICE SETTINGS (player 1) --------------------------------------
 *
 * Remembers player 1's settings per input device, so swapping controllers loads
 * the right binds, stick tuning and look up/down. The menus key everything by
 * SLOT (padN_*), so without this a new pad in slot 0 inherited the last pad's
 * binds.
 *
 * midas.ini holds the LIVE state for the device [input] p1_settings_for names.
 * When a different device becomes player 1, the live settings are saved to the
 * OUTGOING device's record and the incoming device's record is loaded through
 * the ordinary setters; a never-seen device gets defaults (its own layout
 * seeded, style and mode auto, toggles off, shared values kept).
 *
 *     Data/Devices/<id>.gedevice  id = kbm, raphnet<chan>, or the SDL GUID
 *                                 (old ./Devices records are moved here on first use)
 *
 * Checked once per poll (portInputPollTick, main thread), which covers every
 * way slot 0 changes (hotplug, intro claim, press-to-assign, raphnet passes).
 * The outgoing values are still in the slot arrays then: a close only wipes a
 * SEEDED map, which the record writes as "binds=seeded" anyway.
 *
 * Limits: player 1 only (MP has profiles); two pads of one model share a GUID
 * and so a record. See docs/port/per-device-settings.md.
 */
#define DEVREC_DIR     "Data/Devices"   /* under Data, with the saves and profiles */
#define DEVREC_DIR_OLD "Devices"
#define DEVREC_EXT     ".gedevice"
#define DEVREC_VERSION 4   /* 2: layout_user, n64tag; 3: lr_classic; 4: auto_aim, deadzone_look */

static const char *const sDevRecShared[] = { "look_updown", "auto_aim", "deadzone", "deadzone_look", "trigger_threshold", "stick_scale" };
#define DEVREC_SHARED ((int) (sizeof(sDevRecShared) / sizeof(sDevRecShared[0])))

/* the scalar pad keys a record carries, without their padN_ */
static const char *const sDevRecPadKeys[] = { "swap_sticks", "gadget_switch", "rumble", "calib", "lr_classic" };
#define DEVREC_PADKEYS ((int) (sizeof(sDevRecPadKeys) / sizeof(sDevRecPadKeys[0])))

static int inputDevRecWritable(void)
{   /* the same gate as portIniWriteStrs: a harness run leaves the user's files alone */
    return getenv("GE_WINDOW") == NULL && getenv("GE_NOINISAVE") == NULL;
}

static const char *inputDevRecPath(const char *id)
{
    extern const char *portRootPath(const char *path);
    extern s32 portDataMigrate(const char *olddir, const char *newdir, const char *ext1, const char *ext2);
    static int s_Migrated;
    char rel[96];

    if (!s_Migrated)
    {   /* the old ./Devices records move under Data/ before the first read (port_profiles.c) */
        s_Migrated = 1;
        portDataMigrate(DEVREC_DIR_OLD, DEVREC_DIR, DEVREC_EXT, NULL);
    }

    SDL_snprintf(rel, sizeof(rel), DEVREC_DIR "/%s" DEVREC_EXT, id);
    return portRootPath(rel);
}

/* 1 = the slot holds a device, id and name filled; 0 = it is empty, and
 * player 1 is the keyboard and mouse (id "kbm") */
static int inputSlotDeviceId(int slot, char *id, int n, const char **name)
{
    SDL_Joystick *j = (s_Pads[slot] != NULL) ? SDL_GameControllerGetJoystick(s_Pads[slot]) : s_Joys[slot];

    if (s_PadRaphnet[slot] != 0)
    {
        SDL_snprintf(id, (size_t) n, "raphnet%d", s_PadRaphnet[slot] - 1);
        *name = portRaphnetName();
        return 1;
    }
    if (j == NULL)
    {
        SDL_strlcpy(id, "kbm", (size_t) n);
        *name = "keyboard and mouse";
        return 0;
    }
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), id, n);
    *name = SDL_JoystickName(j);
    return 1;
}

static void inputDevRecSharedGet(const char *key, char *buf, int n)
{
    extern s32 portVideoIniGetKey(const char *key, char *buf, s32 n);

    if (SDL_strcmp(key, "look_updown") == 0)
    {   /* Only record look-reverse if a value was actually set:
         * portVideoIniGetKey returns the table default while none is
             * established, and recording that would later impose an inversion
             * the player never chose. */
        extern s32 portPlayerOptCfg(s32 which); /* video.c: -1 = none established */
        const s32 v = portPlayerOptCfg(0);      /* PLAYER_OPTION_LOOK */

        if (v >= 0) SDL_snprintf(buf, (size_t) n, "%d", (int) v);
        else        buf[0] = '\0';
    }
    else if (SDL_strcmp(key, "auto_aim") == 0)
    {   /* as look_updown: only a value that was actually set */
        extern s32 portPlayerOptCfg(s32 which); /* video.c: -1 = none established */
        const s32 v = portPlayerOptCfg(1);      /* PLAYER_OPTION_AUTOAIM */

        if (v >= 0) SDL_snprintf(buf, (size_t) n, "%d", (int) v);
        else        buf[0] = '\0';
    }
    else if (SDL_strcmp(key, "deadzone") == 0)          SDL_snprintf(buf, (size_t) n, "%d", s_OptDeadzone);
    else if (SDL_strcmp(key, "deadzone_look") == 0)     SDL_snprintf(buf, (size_t) n, "%d", s_OptDeadzoneLook);
    else if (SDL_strcmp(key, "trigger_threshold") == 0) SDL_snprintf(buf, (size_t) n, "%d", s_OptTrigThreshold);
    else if (SDL_strcmp(key, "stick_scale") == 0)       SDL_snprintf(buf, (size_t) n, "%g", (double) s_OptStickScale);
    else if (!portVideoIniGetKey(key, buf, n))          buf[0] = '\0';
}

static void inputDevRecPadGet(int slot, const char *key, char *buf, int n)
{
    int i;

    buf[0] = '\0';
    if (SDL_strcmp(key, "swap_sticks") == 0)        SDL_snprintf(buf, (size_t) n, "%d", s_PadSwapOpt[slot]);
    else if (SDL_strcmp(key, "gadget_switch") == 0) SDL_snprintf(buf, (size_t) n, "%d", s_PadGadgetSwitch[slot]);
    else if (SDL_strcmp(key, "lr_classic") == 0)    SDL_snprintf(buf, (size_t) n, "%d", s_PadLrClassic[slot]);
    else if (SDL_strcmp(key, "rumble") == 0)        SDL_snprintf(buf, (size_t) n, "%d", s_PadRumbleOn[slot]);
    else if (SDL_strcmp(key, "calib") == 0 && s_PadCalibSet[slot])
    {   /* as padN_calib spells it: 16 values, [i / 2][i & 1] */
        for (i = 0; i < 16; i++)
        {
            const size_t len = SDL_strlen(buf);

            SDL_snprintf(buf + len, (size_t) n - len, (i == 0) ? "%d" : ",%d", (int) s_PadCalib[slot][i / 2][i & 1]);
        }
    }
}

/* declared here, not included: sys/stat.h drags the C library's string.h in
 * over the port's own (include/string.h) and the build stops */
#ifdef _WIN32
extern int _mkdir(const char *path);
#else
extern int mkdir(const char *path, unsigned int mode);
#endif

static void inputDevRecEnsureDir(void)
{   /* fails harmlessly once it exists, so it says so only the first time */
    extern const char *portRootPath(const char *path);
    const char *path;

#ifdef _WIN32
    _mkdir(portRootPath("Data"));
#else
    mkdir(portRootPath("Data"), 0755);
#endif
    path = portRootPath(DEVREC_DIR);
#ifdef _WIN32
    if (_mkdir(path) == 0)
#else
    if (mkdir(path, 0755) == 0)
#endif
    {
        portConLogf("ge: devices: created %s\n", path);
    }
}

/* Write the live settings as device `id`'s record. Called for the OUTGOING
 * device, so the slot's current occupant does not decide what is written:
 * isPad and raphnet come from the id. */
static void inputDevRecSave(int slot, const char *id)
{
    const int isPad = SDL_strcmp(id, "kbm") != 0;
    const int raph = SDL_strncmp(id, "raphnet", 7) == 0;
    char val[160];
    FILE *f;
    int i, a;

    if (!inputDevRecWritable())
    {
        return;
    }
    inputDevRecEnsureDir();
    f = fopen(inputDevRecPath(id), "w");
    if (f == NULL)
    {
        portConLogf("ge: devices: cannot write %s\n", inputDevRecPath(id));
        return;
    }
    fprintf(f, "[device]\nid=%s\nversion=%d\n", id, DEVREC_VERSION);
    fprintf(f, "[shared]\n");
    for (i = 0; i < DEVREC_SHARED; i++)
    {
        inputDevRecSharedGet(sDevRecShared[i], val, sizeof(val));
        if (val[0] != '\0')
        {
            fprintf(f, "%s=%s\n", sDevRecShared[i], val);
        }
    }
    if (isPad)
    {
        fprintf(f, "[pad]\n");
        /* the style cell by the id, not by the slot: a raphnet arriving has
         * already pointed the slot's cell at its own channel */
        fprintf(f, "style=%s\n", portInputStyleName(raph ? s_RaphnetStyle[(id[7] == '1') ? 1 : 0] : s_PadStyle[slot]));
        for (i = 0; i < DEVREC_PADKEYS; i++)
        {
            inputDevRecPadGet(slot, sDevRecPadKeys[i], val, sizeof(val));
            fprintf(f, "%s=%s\n", sDevRecPadKeys[i], val);
        }
        if (!raph)
        {   /* a raphnet is always N64 and reads no SDL map */
            fprintf(f, "inputmode=%s\n", (s_PadInputMode[slot] == 1) ? "port" : (s_PadInputMode[slot] == 2) ? "n64" : "auto");
            fprintf(f, "layout=%s\n", s_PadLayoutWant[slot]);
            fprintf(f, "layout_user=%d\n", s_PadLayoutUser[slot]);
            fprintf(f, "n64tag=%d\n", s_PadIsN64[slot]);   /* a custom map's class */
            fprintf(f, "binds=%s\n", s_PadMapCustom[slot] ? "custom" : "seeded");
            for (i = 0; s_PadMapCustom[slot] && i < PM_COUNT; i++)
            {
                inputMapTokenText(&s_PadMap[slot][i], val, sizeof(val));
                for (a = 0; a < PM_MAX_ALT && s_PadMapAlt[slot][i][a].kind != 0; a++)
                {
                    const size_t len = SDL_strlen(val);

                    val[len] = ',';
                    inputMapTokenText(&s_PadMapAlt[slot][i][a], val + len + 1, (int) (sizeof(val) - len - 1));
                }
                fprintf(f, "bind_%s=%s\n", sPadMapNames[i], val);
            }
        }
    }
    else
    {   /* the keyboard's record also holds the keyboard and mouse binds
         * (video.c), which this rewrite must not lose */
        extern void portVideoKbmBindsAppend(FILE *f);

        portVideoKbmBindsAppend(f);
    }
    fclose(f);
    portConLogf("ge: devices: saved player 1's settings as %s\n", id);
}

/* For video.c, which keeps the keyboard and mouse binds in the keyboard's
 * record: the record's path (moving an old ./Devices folder first) with the
 * folder made, and the header a new record starts with. */
const char *portInputKbmRecPath(void)
{
    (void) inputDevRecPath("kbm");   /* runs the old-folder move first */
    inputDevRecEnsureDir();
    return inputDevRecPath("kbm");   /* last: both share portRootPath's buffer */
}

void portInputDevRecHeader(FILE *f, const char *id)
{
    fprintf(f, "[device]\nid=%s\nversion=%d\n", id, DEVREC_VERSION);
}

/* the slot's own map again, from its layout (or the first that matches):
 * spelled out into midas.ini, so the file holds this device's map and not the
 * last one's */
static void inputDevRecReseed(int slot)
{
    const s32 dev = portInputSlotDevice(slot);

    s_PadMapCustom[slot] = 0;
    if (dev > 0)
    {
        portInputPadSeedMap(dev);
    }
}

/** RESET TO DEFAULT for a pad: re-derive the slot's map from its layout
  * (padN_layout, else the first match), live and in midas.ini,
 *  never copying the live map. Not for a raphnet, which can't be rebound; 1 = reset. */
s32 portInputPadResetDefaults(s32 dev)
{
    const s32 slot = portInputDeviceSlot(dev);

    if (slot < 0 || slot >= INPUT_MAX_PADS || s_PadRaphnet[slot])
    {
        return 0;
    }
    s_PadMapCustom[slot] = 0;
    s_PadMapSeeded[slot] = 0;
    portInputPadSeedMap(dev);
    return 1;
}

/* the stick mode reads the calibration: derive it again after a load */
static void inputDevRecRederive(int slot)
{
    int raphClass = s_PadRaphnet[slot] != 0;
    int jidx;

    for (jidx = 0; !raphClass && jidx < SDL_NumJoysticks(); jidx++)
    {
        if (SDL_JoystickGetDeviceInstanceID(jidx) == s_PadInstance[slot])
        {
            raphClass = inputDeviceIsRaphnetClass(jidx);
        }
    }
    inputDeriveStickMode(slot, raphClass);
}

/**
 * Put device `id`'s settings into the slot (no record = the defaults, see the
 * block comment), live and in midas.ini at once via portVideoIniSetKey. Returns 1
 * when a record was read.
 */
static int inputDevRecLoad(int slot, const char *id, int isPad)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);
    static char binds[PM_COUNT][64];
    const char *bindv[PM_COUNT];
    const int raph = s_PadRaphnet[slot] != 0;
    char layout[64] = "";
    int layoutUser = -1;
    int n64tag = 0;
    char key[48];
    char line[256];
    int custom = 0;
    int found;
    int i;
    FILE *f = fopen(inputDevRecPath(id), "r");

    found = (f != NULL);
    for (i = 0; i < PM_COUNT; i++)
    {
        binds[i][0] = '\0';
        bindv[i] = binds[i];
    }
    if (isPad)
    {   /* the defaults first: a key the record lacks (an older file) is the
         * default, never the last device's value */
        inputSlotStyleSet(slot, -1);
        for (i = 0; i < DEVREC_PADKEYS; i++)
        {
            SDL_snprintf(key, sizeof(key), "pad%d_%s", slot + 1, sDevRecPadKeys[i]);
            portVideoIniSetKey(key, (SDL_strcmp(sDevRecPadKeys[i], "rumble") == 0) ? "1"
                                  : (SDL_strcmp(sDevRecPadKeys[i], "calib") == 0) ? "" : "0");
        }
        if (!raph)
        {
            SDL_snprintf(key, sizeof(key), "pad%d_inputmode", slot + 1);
            portVideoIniSetKey(key, "auto");
        }
    }
    while (found && fgets(line, sizeof(line), f) != NULL)
    {
        char *eq;
        char *val;
        size_t n = SDL_strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        {
            line[--n] = '\0';
        }
        if (line[0] == ';' || line[0] == '[' || (eq = SDL_strchr(line, '=')) == NULL)
        {
            continue;
        }
        *eq = '\0';
        val = eq + 1;
        for (i = 0; i < DEVREC_SHARED; i++)
        {
            if (SDL_strcmp(line, sDevRecShared[i]) == 0 && val[0] != '\0')
            {
                portVideoIniSetKey(line, val);
            }
        }
        if (!isPad)
        {
            continue;
        }
        for (i = 0; i < DEVREC_PADKEYS; i++)
        {
            if (SDL_strcmp(line, sDevRecPadKeys[i]) == 0)
            {
                SDL_snprintf(key, sizeof(key), "pad%d_%s", slot + 1, line);
                portVideoIniSetKey(key, val);
            }
        }
        if (SDL_strcmp(line, "style") == 0)
        {
            inputSlotStyleSet(slot, portInputStyleCode(val));
        }
        else if (SDL_strcmp(line, "inputmode") == 0 && !raph)
        {
            SDL_snprintf(key, sizeof(key), "pad%d_inputmode", slot + 1);
            portVideoIniSetKey(key, val);
        }
        else if (SDL_strcmp(line, "layout") == 0)
        {
            SDL_strlcpy(layout, val, sizeof(layout));
        }
        else if (SDL_strcmp(line, "layout_user") == 0)
        {
            layoutUser = SDL_atoi(val);
        }
        else if (SDL_strcmp(line, "n64tag") == 0)
        {
            n64tag = SDL_atoi(val) != 0;
        }
        else if (SDL_strcmp(line, "binds") == 0)
        {
            custom = SDL_strcmp(val, "custom") == 0;
        }
        else if (SDL_strncmp(line, "bind_", 5) == 0)
        {
            for (i = 0; i < PM_COUNT; i++)
            {
                if (SDL_strcmp(line + 5, sPadMapNames[i]) == 0)
                {
                    SDL_strlcpy(binds[i], val, sizeof(binds[i]));
                }
            }
        }
    }
    if (f != NULL)
    {
        fclose(f);
    }
    if (!found)
    {   /* a never-seen device: Auto-Aim by its kind (Murk 2026-10-15), on for a
         * pad, off for the keyboard and mouse; look up/down stays shared */
        portVideoIniSetKey("auto_aim", isPad ? "1" : "0");
    }
    if (isPad && !raph)
    {
        const int sameLayout = SDL_strcmp(layout, s_PadLayoutWant[slot]) == 0
                            && layoutUser == s_PadLayoutUser[slot];

        SDL_snprintf(key, sizeof(key), "pad%d_layout", slot + 1);
        portVideoIniSetKey(key, layout);
        SDL_snprintf(key, sizeof(key), "pad%d_layout_user", slot + 1);
        SDL_snprintf(line, sizeof(line), "%d", layoutUser);
        portVideoIniSetKey(key, (layoutUser < 0) ? "" : line);
        if (custom)
        {
            inputPadWriteVals(slot, bindv);
            s_PadIsN64[slot] = n64tag;   /* the map's own class, not the last pad's */
            inputLayoutTagSlot(slot);    /* a named layout still decides */
        }
        else if (s_PadMapCustom[slot] || !sameLayout)
        {   /* a seeded map that is already this device's own is left alone */
            s_PadIsN64[slot] = 0;        /* the seed sets it again; a raw stick's fallback is never N64 */
            inputDevRecReseed(slot);
        }
    }
    if (isPad)
    {
        inputDevRecRederive(slot);
    }
    return found;
}

static void inputP1SettingsForSet(const char *id)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);

    SDL_strlcpy(s_P1SettingsFor, id, sizeof(s_P1SettingsFor));
    portVideoIniSetKey("p1_settings_for", id);
}

/** Once per poll, sim thread: has a different device become player 1? */
static void inputP1DeviceSettingsCheck(void)
{
    extern s32 portVideoIniFresh(void);   /* video.c: no midas.ini at boot */
    static SDL_JoystickID lastInst = -2;
    static int lastRaph = -1;
    const SDL_JoystickID inst = (s_Pads[0] != NULL || s_Joys[0] != NULL) ? s_PadInstance[0] : -1;
    const char *name = "";
    char id[48];
    int isPad;
    int loaded;

    if (inst == lastInst && s_PadRaphnet[0] == lastRaph)
    {
        return;
    }
    lastInst = inst;
    lastRaph = s_PadRaphnet[0];
    isPad = inputSlotDeviceId(0, id, sizeof(id), &name);
    if (SDL_strcasecmp(id, s_P1SettingsFor) == 0)
    {
        return;
    }
    if (s_P1SettingsFor[0] == '\0' && portVideoIniFresh())
    {   /* a fresh install: player 1's device starts its record now, with the
         * defaults for its kind, so the next device (a pad plugged in later)
         * gets its own instead of inheriting these */
        extern s32 portVideoIniSetKey(const char *key, const char *value);

        portVideoIniSetKey("auto_aim", isPad ? "1" : "0");
        inputP1SettingsForSet(id);
        portConLogf("ge: devices: fresh install, player 1 starts as %s ('%s')\n", id, name ? name : "?");
        return;
    }
    if (s_P1SettingsFor[0] == '\0')
    {   /* an ini from before the records: what it holds is the first PAD's -
         * the keyboard alone has no pad keys to own, so wait for one */
        if (isPad)
        {
            portConLogf("ge: devices: player 1's settings in midas.ini are taken as %s's ('%s')\n", id, name ? name : "?");
            inputP1SettingsForSet(id);
        }
        return;
    }
    inputDevRecSave(0, s_P1SettingsFor);
    loaded = inputDevRecLoad(0, id, isPad);
    inputP1SettingsForSet(id);
    portConLogf("ge: devices: player 1 is now %s ('%s') - %s\n", id, name ? name : "?",
                loaded ? "its saved settings loaded" : isPad ? "first time seen, defaults" : "shared settings kept");
}

/**
 * Save what device `dev` is bound to now as a user layout.
 *
 * Written as SDL control names wherever possible, so it applies to other pads
 * of the same family (raw tokens only for inputs SDL can't name). The match
 * term is this device's exact SDL name, plus "n64" when saved from an
 * N64-driven pad.
 *
 * `name` NULL uses the device's own name, with a number appended if taken
 * (there is no text entry in the frontend yet). Saving over a USER layout of
 * the same name replaces it; a shipped layout is never touched.
 */
s32 portInputLayoutSaveCurrent(s32 dev, const char *name)
{
    extern const char *portRootPath(const char *path);
    char want[64];
    char line[1024];
    s32 slot = portInputDeviceSlot(dev);
    const char *devname;
    FILE *f;
    int c, used, n;
    u8 wanted[INPUT_MAX_PADS] = {0};

    if (slot < 0)
    {
        return 0;
    }
    devname = (s_Pads[slot] != NULL) ? SDL_GameControllerName(s_Pads[slot])
            : (s_Joys[slot] != NULL) ? SDL_JoystickName(s_Joys[slot]) : NULL;
    if (devname == NULL)
    {
        devname = "controller";
    }
    if (name != NULL && name[0] != '\0')
    {
        SDL_strlcpy(want, name, sizeof(want));
    }
    else
    {
        SDL_strlcpy(want, devname, sizeof(want));
        for (n = 2; n < 100; n++)
        {
            int taken = 0;
            for (c = 0; c < portInputLayoutCount(); c++)
            {
                if (SDL_strcasecmp(s_Layouts[c].name, want) == 0) { taken = 1; break; }
            }
            if (!taken)
            {
                break;
            }
            SDL_snprintf(want, sizeof(want), "%s %d", devname, n);
        }
    }
    {   /* the bar is the format's own separator, and a leading '#' makes the
             * line a comment: refuse both, or the save is lost or invisible */
        char *p;
        char *s = want;

        for (p = want; *p != '\0'; p++)
        {
            if (*p == '|')
            {
                *p = '/';
            }
        }
        while (*s == ' ' || *s == '\t' || *s == '#')
        {
            s++;
        }
        SDL_memmove(want, s, SDL_strlen(s) + 1);
        if (want[0] == '\0')
        {
            return 0;
        }
    }
    {   /* a NEW name with the saved layouts' share of the list already full
             * would be written but never loaded: refuse it and say so */
        int users = 0;
        int same = 0;

        for (c = 0; c < portInputLayoutCount(); c++)
        {
            users += s_LayoutIsUser[c];
            same |= (s_LayoutIsUser[c] && SDL_strcasecmp(s_Layouts[c].name, want) == 0);
        }
        if (!same && users >= LAYOUT_USER_MAX)
        {
            portConLogf("ge: input: %d saved layouts already - delete one before saving '%s'\n", users, want);
            return 0;
        }
    }
    for (c = 0; c < portInputLayoutCount(); c++)
    {
        if (s_LayoutIsUser[c] && SDL_strcasecmp(s_Layouts[c].name, want) == 0)
        {
            /* a REPLACE, not a delete: the slots on it stay on it (a delete
             * clears their padN_layout, so saving over your own config with the
             * offered name would lose the record) */
            for (n = 0; n < INPUT_MAX_PADS; n++)
            {
                wanted[n] = (SDL_strcasecmp(s_PadLayoutWant[n], want) == 0);
            }
            portInputLayoutDelete(c);
            break;
        }
    }

    /* the drive mode decided which list was bound (N64 or Port), so the copy
     * carries the class too - an untagged copy of Standard cannot turn */
    used = SDL_snprintf(line, sizeof(line), "%s | name=%s%s | ", want, devname, portInputPadIsN64(slot) ? ", n64" : "");
    {   /* a raw pad never rebound has an EMPTY s_PadMap - the merge drives it
         * through its fixed fallback, and the rows show that: save that, not
         * nothing (applying an empty save would unbind every control) */
        static char fbtok[PM_COUNT][16];
        const int fallback = s_PadRaphnet[slot] == 0 && s_Pads[slot] == NULL && s_Joys[slot] != NULL
                          && !s_PadMapCustom[slot];

        if (fallback)
        {
            inputRawFallbackTokens(slot, fbtok);
        }
        for (c = 0; fallback && c < PM_COUNT && used < (int) sizeof(line) - 64; c++)
        {
            if (SDL_strcmp(fbtok[c], "-") != 0)
            {
                used += SDL_snprintf(line + used, sizeof(line) - used, "%s=%s; ", sPadMapNames[c], fbtok[c]);
            }
        }
        c = fallback ? PM_COUNT : 0;   /* fallback: skip the s_PadMap walk below */
    }
    for (; c < PM_COUNT && used < (int) sizeof(line) - 64; c++)
    {
        char val[96];
        char one[32];
        int vused = 0;
        int a;

        val[0] = '\0';
        if (s_PadMap[slot][c].kind != 0)
        {
            if (!inputMapEntrySdlName(slot, &s_PadMap[slot][c], one, sizeof(one)))
            {
                inputMapTokenText(&s_PadMap[slot][c], one, sizeof(one));
            }
            vused += SDL_snprintf(val + vused, sizeof(val) - vused, "%s", one);
        }
        for (a = 0; a < PM_MAX_ALT && vused < (int) sizeof(val) - 32; a++)
        {
            if (s_PadMapAlt[slot][c][a].kind == 0)
            {
                continue;
            }
            if (!inputMapEntrySdlName(slot, &s_PadMapAlt[slot][c][a], one, sizeof(one)))
            {
                inputMapTokenText(&s_PadMapAlt[slot][c][a], one, sizeof(one));
            }
            vused += SDL_snprintf(val + vused, sizeof(val) - vused, "%s%s", (vused > 0) ? "," : "", one);
        }
        if (val[0] == '\0' || SDL_strcmp(val, "-") == 0)
        {
            continue;   /* unlisted IS unbound in this format */
        }
        used += SDL_snprintf(line + used, sizeof(line) - used, "%s=%s; ", sPadMapNames[c], val);
    }

    {   /* a hand-edited file whose last line has no newline would have this
             * record glued onto it, losing it and giving its bindings to that line */
        int last = '\n';

        f = fopen(portRootPath(LAYOUT_USER_FILE), "rb");
        if (f != NULL)
        {
            if (fseek(f, -1, SEEK_END) == 0)
            {
                last = fgetc(f);
            }
            fclose(f);
        }
        f = fopen(portRootPath(LAYOUT_USER_FILE), "a");
        if (f == NULL)
        {
            portConLogf("ge: input: could not write %s\n", LAYOUT_USER_FILE);
            return 0;
        }
        if (last != '\n' && last != EOF)
        {
            fputc('\n', f);
        }
    }
    fprintf(f, "%s\n", line);
    fclose(f);
    s_LayoutCount = -1;   /* reload: the new one has to appear in the list */
    {   /* the saver is ON the layout it just saved, as after an apply, and
             * any slot that was on a replaced one still is */
        extern s32 portVideoIniSetKey(const char *key, const char *value);
        char key[24];

        wanted[slot] = 1;
        for (n = 0; n < INPUT_MAX_PADS; n++)
        {
            if (!wanted[n])
            {
                continue;
            }
            SDL_strlcpy(s_PadLayoutWant[n], want, sizeof(s_PadLayoutWant[0]));
            SDL_snprintf(key, sizeof(key), "pad%d_layout", n + 1);
            portVideoIniSetKey(key, want);
            s_PadLayoutUser[n] = 1;   /* the player's own copy */
            SDL_snprintf(key, sizeof(key), "pad%d_layout_user", n + 1);
            portVideoIniSetKey(key, "1");
        }
    }
    portConLogf("ge: input: saved layout '%s' from player %d\n", want, (int) slot + 1);
    return 1;
}

/**
 * Remove user layout `i` (a shipped one is refused; i < 0 is a no-op, used by
 * the save path before checking for a name clash). Rewrites the whole file
 * without that line rather than editing in place, so other layouts can't be
 * lost.
 */
s32 portInputLayoutDelete(s32 i)
{
    extern const char *portRootPath(const char *path);
    char want[64];
    char *buf = NULL;
    char *line;
    char *next;
    long len = 0;
    FILE *f;

    if (i < 0 || i >= portInputLayoutCount() || !s_LayoutIsUser[i])
    {
        return 0;
    }
    SDL_strlcpy(want, s_Layouts[i].name, sizeof(want));
    /* the WHOLE file, comments and blank lines included: a fixed-size line
     * buffer would count those too and drop every layout after them */
    f = fopen(portRootPath(LAYOUT_USER_FILE), "rb");
    if (f == NULL)
    {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0)
    {
        buf = (char *) SDL_malloc((size_t) len + 1);
    }
    if (buf == NULL || fread(buf, 1, (size_t) len, f) != (size_t) len)
    {
        SDL_free(buf);
        fclose(f);
        return 0;
    }
    buf[len] = '\0';
    fclose(f);
    f = fopen(portRootPath(LAYOUT_USER_FILE), "wb");
    if (f == NULL)
    {
        SDL_free(buf);
        return 0;
    }
    for (line = buf; *line != '\0'; line = next)
    {
        char copy[1024];
        char *bar;

        next = SDL_strchr(line, '\n');
        next = (next != NULL) ? next + 1 : line + SDL_strlen(line);
        SDL_strlcpy(copy, line, ((size_t) (next - line) < sizeof(copy)) ? (size_t) (next - line) + 1 : sizeof(copy));
        bar = SDL_strchr(copy, '|');
        if (bar != NULL)
        {
            *bar = '\0';
            if (SDL_strcasecmp(inputTrim(copy), want) == 0)
            {
                continue;   /* this is the one going */
            }
        }
        fwrite(line, 1, (size_t) (next - line), f);
    }
    fclose(f);
    SDL_free(buf);
    s_LayoutCount = -1;
    {   /* Any slot that wanted this layout now wants nothing, so midas.ini doesn't
         * keep naming a deleted layout
             * (which the CONTROL sheet would read back as the pad's active layout). */
        extern s32 portVideoIniSetKey(const char *key, const char *value);
        s32 slot;

        for (slot = 0; slot < INPUT_MAX_PADS; slot++)
        {
            if (SDL_strcasecmp(s_PadLayoutWant[slot], want) == 0)
            {
                char key[24];

                s_PadLayoutWant[slot][0] = '\0';
                SDL_snprintf(key, sizeof(key), "pad%d_layout", (int) slot + 1);
                portVideoIniSetKey(key, "");
            }
        }
    }
    portConLogf("ge: input: deleted saved layout '%s'\n", want);
    return 1;
}

/** Arm a capture on this device. 0 = it has no device to listen to. */
s32 portInputPadCaptureBegin(s32 dev)
{
    s32 slot = portInputDeviceSlot(dev);
    int i;

    portInputPadCaptureCancel();
    if (slot < 0 || (s_Pads[slot] == NULL && s_Joys[slot] == NULL))
    {
        return 0;
    }
    s_PadCap.slot = slot;
    s_PadCap.anyBaseDown = 0;
    s_PadCap.holdSince = 0;
    s_PadCap.settle = 1;
    s_PadCap.armedAt = SDL_GetTicks();
    s_PadCap.pendBtn = -1;
    s_PadCap.done = 0;
    s_PadCap.token[0] = '\0';
    for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
    {
        s_PadCap.gcBase[i] = 0;
    }
    return 1;
}

/** The joystick handle behind a slot, mapped or raw. */
static SDL_Joystick *inputSlotJoystick(int slot)
{
    if (s_Pads[slot] != NULL)
    {
        return SDL_GameControllerGetJoystick(s_Pads[slot]);
    }
    return s_Joys[slot];
}

/* 1 = button/hat entry e is what control pm of this slot answers to now:
 * the live map (primary or alternate), or the raw fallback on an unmapped
 * pad that has no map of its own */
static int inputCapIsControl(int slot, int pm, const PadMapEntry *e)
{
    PadMapEntry fb;
    int a;

    if (s_PadMap[slot][pm].kind == e->kind && s_PadMap[slot][pm].idx == e->idx
        && (e->kind != 'h' || (s_PadMap[slot][pm].dir & e->dir) != 0))
    {
        return 1;
    }
    for (a = 0; a < PM_MAX_ALT; a++)
    {
        const PadMapEntry *x = &s_PadMapAlt[slot][pm][a];

        if (x->kind == e->kind && x->idx == e->idx && (e->kind != 'h' || (x->dir & e->dir) != 0))
        {
            return 1;
        }
    }
    if (s_Pads[slot] == NULL && !s_PadMapCustom[slot] && pm < PM_UNBOUND_FIRST
        && inputParseMapToken(sPadRawFallback[pm], &fb))
    {
        return fb.kind == e->kind && fb.idx == e->idx && (e->kind != 'h' || (fb.dir & e->dir) != 0);
    }
    return 0;
}

/**
 * One frame of an armed capture. 1 when it has finished, after which
 * portInputPadCaptureTake hands over the token ("" = gave up).
 */
s32 portInputPadCaptureTick(void)
{
    SDL_Joystick *joy;
    int slot = s_PadCap.slot;
    int btnHit = -1, hatHit = -1, hatVal = 0;
    int i;

    if (slot < 0 || s_PadCap.done)
    {
        return s_PadCap.slot >= 0 && s_PadCap.done;
    }
    joy = inputSlotJoystick(slot);
    if (joy == NULL)
    {
        s_PadCap.done = 1;   /* device went away mid-capture */
        return 1;
    }
    if (s_PadCap.settle > 0)
    {
        if ((Sint32) (SDL_GetTicks() - s_PadCap.armedAt) >= PADCAP_SETTLE_MS)
        {   /* take the at-rest baseline once the pad has reported something */
            s_PadCap.settle = 0;
            s_PadCap.nb = SDL_JoystickNumButtons(joy); if (s_PadCap.nb > 64) s_PadCap.nb = 64;
            s_PadCap.na = SDL_JoystickNumAxes(joy);    if (s_PadCap.na > 32) s_PadCap.na = 32;
            s_PadCap.nh = SDL_JoystickNumHats(joy);    if (s_PadCap.nh > 8)  s_PadCap.nh = 8;
            for (i = 0; i < s_PadCap.nb; i++) s_PadCap.baseBtn[i] = SDL_JoystickGetButton(joy, i);
            for (i = 0; i < s_PadCap.na; i++) s_PadCap.baseAxis[i] = SDL_JoystickGetAxis(joy, i);
            for (i = 0; i < s_PadCap.nh; i++) s_PadCap.baseHat[i] = SDL_JoystickGetHat(joy, i);
            for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
            {
                s_PadCap.gcBase[i] = s_Pads[slot] ? SDL_GameControllerGetAxis(s_Pads[slot], (SDL_GameControllerAxis) i) : 0;
            }
        }
        return 0;
    }
    if ((Sint32) (SDL_GetTicks() - s_PadCap.armedAt) > PADCAP_TIMEOUT_MS)
    {
        s_PadCap.done = 1;
        s_PadCap.token[0] = '\0';
        return 1;
    }

    /* The baseline follows a release: the A that armed the row is in the
     * baseline so it isn't captured, but once seen UP it is struck out, so
     * pressing A again captures it (hats too); a baseline button still held
     * long enough is hold-to-unbind (portInputPadCaptureHoldLong). */
    s_PadCap.anyBaseDown = 0;
    for (i = 0; i < s_PadCap.nb; i++)
    {
        if (s_PadCap.baseBtn[i])
        {
            if (!SDL_JoystickGetButton(joy, i))
            {
                s_PadCap.baseBtn[i] = 0;
            }
            else
            {
                s_PadCap.anyBaseDown = 1;
            }
        }
    }
    for (i = 0; i < s_PadCap.nh; i++)
    {
        if (s_PadCap.baseHat[i] != SDL_HAT_CENTERED && SDL_JoystickGetHat(joy, i) == SDL_HAT_CENTERED)
        {
            s_PadCap.baseHat[i] = SDL_HAT_CENTERED;
        }
    }
    if (!s_PadCap.anyBaseDown)
    {
        s_PadCap.holdSince = 0;
    }
    else if (s_PadCap.holdSince == 0)
    {
        s_PadCap.holdSince = SDL_GetTicks();
    }

    /* A button is captured on its RELEASE, and a hold to PADCAP_HOLD_MS unbinds
     * instead, so no button is reserved as "cancel" and every button is
     * bindable. Hats and axes still capture on the push. */
    if (s_PadCap.pendBtn >= 0)
    {
        if (SDL_JoystickGetButton(joy, s_PadCap.pendBtn))
        {
            return 0;
        }
        SDL_snprintf(s_PadCap.token, sizeof(s_PadCap.token), "b%d", s_PadCap.pendBtn);
        s_PadCap.done = 1;
        portConLogf("ge: input: captured %s on player %d\n", s_PadCap.token, slot + 1);
        return 1;
    }

    /* BUTTONS AND HATS FIRST: a pad whose first report arrives with the press
     * moves its trigger axes in the same poll, and the button is what the
     * player meant (probe rule 1). */
    for (i = 0; i < s_PadCap.nb; i++)
    {
        if (!s_PadCap.baseBtn[i] && SDL_JoystickGetButton(joy, i))
        {
            btnHit = i;
            break;
        }
    }
    if (btnHit >= 0)
    {
        s_PadCap.pendBtn = btnHit;   /* see above: decided on release / after a hold */
        s_PadCap.pendSince = SDL_GetTicks();
        return 0;
    }
    for (i = 0; btnHit < 0 && i < s_PadCap.nh; i++)
    {
        u8 h = SDL_JoystickGetHat(joy, i);

        if (h != s_PadCap.baseHat[i] && h != SDL_HAT_CENTERED)
        {
            hatHit = i;
            hatVal = h;
            break;
        }
    }
    if (btnHit >= 0)
    {
        SDL_snprintf(s_PadCap.token, sizeof(s_PadCap.token), "b%d", btnHit);
    }
    else if (hatHit >= 0)
    {
        const char d = (hatVal & SDL_HAT_UP) ? 'u' : (hatVal & SDL_HAT_DOWN) ? 'd'
                     : (hatVal & SDL_HAT_LEFT) ? 'l' : 'r';
        SDL_snprintf(s_PadCap.token, sizeof(s_PadCap.token), "h%d%c", hatHit, d);
    }
    else if (s_Pads[slot] != NULL)
    {
        /* MAPPED pad: read axes through the GameController API, where a
         * trigger rests at 0, and turn the SDL axis back into a raw token
         * through the device's own mapping (probe's note). */
        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
        {
            s32 v = SDL_GameControllerGetAxis(s_Pads[slot], (SDL_GameControllerAxis) i);
            s32 d = v - s_PadCap.gcBase[i];

            /* A release is not a press: every axis rests at 0 through this API,
             * so a move ending nearer 0 than it started is the control being
             * let go (e.g. a trigger held at arm time). Re-baseline and keep
             * waiting for a real push. */
            if ((d > PADCAP_AXIS || d < -PADCAP_AXIS) && SDL_abs(v) < SDL_abs(s_PadCap.gcBase[i]))
            {
                s_PadCap.gcBase[i] = v;
                continue;
            }
            if (d > PADCAP_AXIS || d < -PADCAP_AXIS)
            {
                inputAxisToken(s_Pads[slot], (SDL_GameControllerAxis) i, (d < 0) ? -1 : 1,
                        s_PadCap.token, sizeof(s_PadCap.token));
                if (s_PadCap.token[0] == '-')
                {
                    s_PadCap.token[0] = '\0';   /* SDL names it but the device cannot spell it */
                }
                break;
            }
        }
    }
    else
    {
        /* RAW device: an axis that lands exactly on an endpoint from a zero
         * baseline is the first report, not a push - re-baseline (probe rule 2) */
        for (i = 0; i < s_PadCap.na; i++)
        {
            s16 v = SDL_JoystickGetAxis(joy, i);
            s32 d = (s32) v - (s32) s_PadCap.baseAxis[i];

            if (d <= PADCAP_AXIS && d >= -PADCAP_AXIS)
            {
                continue;
            }
            if (s_PadCap.baseAxis[i] == 0 && (v == -32768 || v == 32767))
            {
                s_PadCap.baseAxis[i] = v;
                continue;
            }
            {   /* A RELEASE IS NOT A PRESS here either (the mapped rule above):
                             * a trigger pulled when the row armed would bind
                             * its release, which then reads held at rest and
                             * the hold-over never lets go */
                const s32 seen = (i < 32 && (s_PadAxisRestSeen[slot] & (1u << i)) != 0);
                const s32 rest = seen ? s_PadAxisRest[slot][i] : 0;

                /* only with the rest LEARNED: with 0 assumed for a never-read
                 * raw trigger, its first light squeeze would look like a
                 * release and bind the let-go direction */
                if (seen && SDL_abs((s32) v - rest) < SDL_abs((s32) s_PadCap.baseAxis[i] - rest))
                {
                    s_PadCap.baseAxis[i] = v;
                    continue;
                }
            }
            SDL_snprintf(s_PadCap.token, sizeof(s_PadCap.token), "a%d%c", i, (d < 0) ? '-' : '+');
            break;
        }
    }

    if (s_PadCap.token[0] != '\0')
    {
        s_PadCap.done = 1;
        portConLogf("ge: input: captured %s on player %d\n", s_PadCap.token, slot + 1);
        return 1;
    }
    return 0;
}

/** The captured token, or "" when the capture gave up. Ends the arm. */
s32 portInputPadCaptureTake(char *out, s32 len)
{
    const int got = (s_PadCap.slot >= 0 && s_PadCap.done && s_PadCap.token[0] != '\0');

    if (out != NULL && len > 0)
    {
        SDL_strlcpy(out, got ? s_PadCap.token : "", (size_t) len);
    }
    if (got && inputParseMapToken(s_PadCap.token, &s_PadCapHold.entry))
    {
        s_PadCapHold.slot = s_PadCap.slot;
    }
    portInputPadCaptureCancel();
    return got;
}

/* ---- ASSIGN A DEVICE TO A PLAYER BY PRESSING A BUTTON ON IT --------------
 *
 * The front end for the slot pins ([input] padN=<guid>[#n], padN=raphnet<chan>)
 * that inputAssignSlotsEx honours first. Arm it for a player and press a button
 * on ANY pad: that pad is pinned to the player's slot, unpinned from any other
 * (one controller, one player), and every slot is reassigned. For player 1 a
 * keyboard key means "mouse and keyboard":
 * the slot-0 pad stays (p1_hybrid) and only the page's view changes, remembered in [input] p1_view.
 *
 * Every SDL joystick is opened while armed so unassigned pads can be heard; SDL
 * refcounts handles, so slot pads are unaffected.
 */
/* Real time, not ticks: the frontend tick follows max fps, so tick counts made
 * the arm time out too early at high fps (MP seating asks several players in a
 * row). */
#define ASSIGN_SETTLE_MS  100
#define ASSIGN_TIMEOUT_MS 10000
static struct {
    int slot;                         /* -1 = not armed */
    int settle, done;                 /* settle: 1 until the baseline has been taken */
    Uint32 t0;                        /* SDL_GetTicks at the arm */
    int result;                       /* -1 nothing / cancelled, 0 keyboard, 1 a pad was pinned, 2 player 1 skipped it */
    int exclude;                      /* bit s = slot s's device cannot be taken (portInputAssignBeginEx) */
    int seating;                      /* the MP setup's arm: a key or a click skips the player too */
    int nprobe;
    SDL_Joystick *probe[INPUT_MAX_DEVICES];
    u8  baseBtn[INPUT_MAX_DEVICES][64];
    u8  baseHat[INPUT_MAX_DEVICES][8];
    s16 baseAxis[INPUT_MAX_DEVICES][32];
    u16 rnBase[2];
    int rnOk;                         /* portRaphnetAvailable() at the baseline - once per arm, not per frame */
    u8  keyBase[SDL_NUM_SCANCODES];
    u32 mouseBase;                    /* SDL_GetMouseState buttons at the baseline */
} s_Assign = { -1 };

s32 portInputP1ViewPad(void)
{
    return s_OptP1View;
}

static void inputAssignCloseProbes(void)
{
    int i;

    for (i = 0; i < s_Assign.nprobe; i++)
    {
        if (s_Assign.probe[i] != NULL)
        {
            SDL_JoystickClose(s_Assign.probe[i]);
            s_Assign.probe[i] = NULL;
        }
    }
    s_Assign.nprobe = 0;
}

void portInputAssignCancel(void)
{
    inputAssignCloseProbes();
    s_Assign.slot = -1;
    s_Assign.done = 0;
}

s32 portInputAssignActive(void)
{
    return s_Assign.slot >= 0 && !s_Assign.done;
}

s32 portInputAssignBegin(s32 slot)
{
    int n = SDL_NumJoysticks();
    int i;

    portInputAssignCancel();
    if (slot < 0 || slot >= INPUT_MAX_PADS)
    {
        return 0;
    }
    if (n > INPUT_MAX_DEVICES)
    {
        n = INPUT_MAX_DEVICES;
    }
    for (i = 0; i < n; i++)
    {
        s_Assign.probe[i] = SDL_JoystickOpen(i);
    }
    s_Assign.nprobe = n;
    s_Assign.slot = slot;
    s_Assign.settle = 1;
    s_Assign.t0 = SDL_GetTicks();
    s_Assign.done = 0;
    s_Assign.result = -1;
    s_Assign.exclude = 0;
    s_Assign.seating = 0;
    return 1;
}

/**
 * The MULTIPLAYER setup's arm (front.c Player Options), which seats players in
 * turn from P2, so nobody can take an already-seated player's pad.
 * `excludeMask` bit s = slot s's device is not up for grabs: a press on a
 * masked slot other than 0 is ignored, and a press on masked slot 0 (player 1,
 * running the menu) SKIPS this player - portInputAssignTake answers -2. Every
 * pad is blanked while armed, so that's the only way player 1's pad can say
 * anything.
 */
s32 portInputAssignBeginEx(s32 slot, s32 excludeMask)
{
    if (!portInputAssignBegin(slot))
    {
        return 0;
    }
    s_Assign.exclude = excludeMask & ~(1 << slot);
    s_Assign.seating = 1;
    return 1;
}

/* which masked slot holds probe `jidx` (SDL device index), + 1; 0 = none */
static int inputAssignProbeExcluded(int jidx)
{
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(jidx);
    int s;

    for (s = 0; s < INPUT_MAX_PADS; s++)
    {
        if ((s_Assign.exclude & (1 << s)) != 0 && s_PadRaphnet[s] == 0
            && (s_Pads[s] != NULL || s_Joys[s] != NULL) && s_PadInstance[s] == id)
        {
            return s + 1;
        }
    }
    return 0;
}

/* the same for a raphnet RAW channel */
static int inputAssignChanExcluded(int chan)
{
    int s;

    for (s = 0; s < INPUT_MAX_PADS; s++)
    {
        if ((s_Assign.exclude & (1 << s)) != 0 && s_PadRaphnet[s] == chan + 1)
        {
            return s + 1;
        }
    }
    return 0;
}

/* A blank must not end in a press (pad half; video.c videoGetPad has the
 * keyboard/mouse half). Pads blanked during an assign stay blanked until they
 * read let go (no button, hat, trigger or map-bound button held; sticks don't
 * count), giving up after five seconds so a stuck button can't kill its own
 * pad. */
#define PAD_BLANK_HOLD_MS 5000
static int s_PadBlankHold[INPUT_MAX_PADS];
static Uint32 s_PadBlankSince[INPUT_MAX_PADS];

static void inputPadBlankBegin(int idx)
{
    s_PadBlankHold[idx] = 1;
    s_PadBlankSince[idx] = SDL_GetTicks();   /* restarts every blanked poll: the five seconds run from the blank's END */
}

/* 1 = map entry e is an axis that this pad's Move or Look stick uses */
static int inputAxisIsStick(int idx, const PadMapEntry *e)
{
    static const int sticks[4] = { PM_STICK_X, PM_STICK_Y, PM_LOOK_X, PM_LOOK_Y };
    int k;

    for (k = 0; e->kind == 'a' && k < 4; k++)
    {
        if (s_PadMap[idx][sticks[k]].kind == 'a' && s_PadMap[idx][sticks[k]].idx == e->idx)
        {
            return 1;
        }
    }
    return 0;
}

/* 1 = still held over: the caller drives nothing from this pad this poll */
static int inputPadBlankHeld(int idx)
{
    SDL_Joystick *joy;
    int down;
    int i, k;

    if (!s_PadBlankHold[idx])
    {
        return 0;
    }
    down = inputSlotAnyButtonDown(idx);
    joy = (s_Joys[idx] != NULL) ? s_Joys[idx]
        : (s_Pads[idx] != NULL) ? SDL_GameControllerGetJoystick(s_Pads[idx]) : NULL;
    for (i = 0; joy != NULL && !down && i < PM_COUNT; i++)
    {
        if (i >= PM_STICK_X && i < PM_PORT_FIRST)
        {
            continue;   /* the sticks and their directions */
        }
        /* ...and a stick direction bound to any other row: the Pro layouts'
         * zoom on lefty-/+, Standard's C buttons on the right stick. A pushed
         * Move stick would otherwise hold the whole pad dead. */
        down = !inputAxisIsStick(idx, &s_PadMap[idx][i]) && inputEntryDown(joy, idx, &s_PadMap[idx][i]);
        for (k = 0; !down && k < PM_MAX_ALT; k++)
        {
            down = !inputAxisIsStick(idx, &s_PadMapAlt[idx][i][k])
                && inputEntryDown(joy, idx, &s_PadMapAlt[idx][i][k]);
        }
    }
    if (down && SDL_GetTicks() - s_PadBlankSince[idx] < PAD_BLANK_HOLD_MS)
    {
        return 1;
    }
    if (down)
    {
        portConLogf("ge: input: player %d still reads a button down %d s after an input blank - letting the pad through\n",
                    idx + 1, PAD_BLANK_HOLD_MS / 1000);
    }
    s_PadBlankHold[idx] = 0;
    return 0;
}

/** Anything held on slot's device right now - the MP setup asks this of slot 0
 * on the press that opened it, to learn whether player 1 is on a pad or the
 * keyboard. */
s32 portInputSlotAnyButtonDown(s32 slot)
{
    return inputSlotAnyButtonDown(slot);
}

/** Anything held on ANY device: every pad slot (all buttons, the hat, the
  * triggers), any keyboard key, any mouse button.
 * The credits' skip hint asks this because a port-driven pad's face buttons
 * never reach the N64 button mask; GE_NOINPUT reads nothing, so harness runs
 * stay deterministic. */
s32 portInputAnyHeld(void)
{
    static int noinput = -1;
    const Uint8 *keys;
    int n;
    int i;

    if (noinput < 0)
    {
        noinput = getenv("GE_NOINPUT") != NULL;
    }
    if (noinput)
    {
        return 0;
    }
    for (i = 0; i < INPUT_MAX_PADS; i++)
    {
        if (inputSlotAnyButtonDown(i))
        {
            return 1;
        }
    }
    keys = SDL_GetKeyboardState(&n);
    for (i = 0; keys != NULL && i < n; i++)
    {
        if (keys[i])
        {
            return 1;
        }
    }
    return SDL_GetMouseState(NULL, NULL) != 0;
}

/* Pin `want` to this slot and to no other, then hand every slot out again. The
 * pin is for the SESSION only and NEVER written to midas.ini, since a saved pin
 * outlives who holds the pad today; a padN=<guid> line typed into midas.ini is
 * still honoured. */
static void inputAssignPin(int slot, const char *want)
{
    char key[8];
    int s;

    for (s = 0; s < INPUT_MAX_PADS; s++)
    {
        if (s != slot && SDL_strcasecmp(s_OptPadAssign[s], want) == 0)
        {
            SDL_snprintf(key, sizeof(key), "pad%d", s + 1);
            portInputSetOption(key, "auto");
        }
    }
    SDL_snprintf(key, sizeof(key), "pad%d", slot + 1);
    portInputSetOption(key, want);
    portConLogf("ge: input: player %d <- %s (assigned by pressing a button on it, for this session)\n", slot + 1, want);
    inputAssignCloseProbes();   /* before the reassign closes and reopens the slots */
    s_PinFreshSlot = slot;      /* every OTHER slot keeps its pad */
    inputAssignSlotsEx(1);
    s_PinFreshSlot = -1;
}

/** One frame of an armed assign. 1 when it has finished; then Take. */
s32 portInputAssignTick(void)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);
    const Uint8 *keys;
    int i, b;

    if (s_Assign.slot < 0 || s_Assign.done)
    {
        return s_Assign.slot >= 0 && s_Assign.done;
    }
    if (s_Assign.settle > 0)
    {
        if (SDL_GetTicks() - s_Assign.t0 >= ASSIGN_SETTLE_MS)
        {
            s_Assign.settle = 0;
            for (i = 0; i < s_Assign.nprobe; i++)
            {
                SDL_Joystick *j = s_Assign.probe[i];
                int nb = (j != NULL) ? SDL_JoystickNumButtons(j) : 0;
                int nh = (j != NULL) ? SDL_JoystickNumHats(j) : 0;
                int na = (j != NULL) ? SDL_JoystickNumAxes(j) : 0;

                if (nb > 64) nb = 64;
                if (nh > 8) nh = 8;
                if (na > 32) na = 32;
                for (b = 0; b < nb; b++) s_Assign.baseBtn[i][b] = SDL_JoystickGetButton(j, b);
                for (b = 0; b < nh; b++) s_Assign.baseHat[i][b] = SDL_JoystickGetHat(j, b);
                for (b = 0; b < na; b++) s_Assign.baseAxis[i][b] = SDL_JoystickGetAxis(j, b);
            }
            /* Probe the raphnet RAW path once per arm, and only on raphnet
             * evidence (a RAW slot, a 0x289B face in SDL, or an adapter an
             * earlier arm found) since it costs a HID enumeration; otherwise
             * only the session's first arm probes. */
            {
                static int rnMemo;   /* 0 = never probed, 1 = found nothing, 2 = found an adapter */
                int k, ev = (rnMemo != 1);

                for (k = 0; k < INPUT_MAX_PADS && !ev; k++)
                {
                    ev = (s_PadRaphnet[k] != 0);
                }
                for (k = 0; k < SDL_NumJoysticks() && !ev; k++)
                {
                    ev = inputDeviceIsRaphnetClass(k);
                }
                s_Assign.rnOk = ev && portRaphnetAvailable();
                if (s_Assign.rnOk)
                {
                    rnMemo = 2;
                }
                else if (rnMemo == 0)
                {
                    rnMemo = 1;
                }
            }
            for (i = 0; i < 2; i++)
            {
                u16 btn = 0;
                s_Assign.rnBase[i] = (s_Assign.rnOk && portRaphnetPoll(i, &btn, NULL, NULL) == 1) ? btn : 0;
            }
            keys = SDL_GetKeyboardState(NULL);
            SDL_memcpy(s_Assign.keyBase, keys, SDL_NUM_SCANCODES);
            s_Assign.mouseBase = SDL_GetMouseState(NULL, NULL);
        }
        return 0;
    }
    if (SDL_GetTicks() - s_Assign.t0 > ASSIGN_TIMEOUT_MS)
    {
        s_Assign.done = 1;
        return 1;
    }

    /* a PAD: the first new button or hat press on any joystick */
    for (i = 0; i < s_Assign.nprobe; i++)
    {
        SDL_Joystick *j = s_Assign.probe[i];
        int nb = (j != NULL) ? SDL_JoystickNumButtons(j) : 0;
        int nh = (j != NULL) ? SDL_JoystickNumHats(j) : 0;
        int hit = 0;
        int hitBtn = -1;
        /* WHICH PAD this handle is NOW: the press is read from the handle
         * opened at the arm, but a hotplug during the prompt renumbers the
         * device list, so don't trust the index. Gone = not this probe's press. */
        int di = i;

        if (j != NULL && (di >= SDL_NumJoysticks() || SDL_JoystickGetDeviceInstanceID(di) != SDL_JoystickInstanceID(j)))
        {
            int k;

            di = -1;
            for (k = 0; k < SDL_NumJoysticks(); k++)
            {
                if (SDL_JoystickGetDeviceInstanceID(k) == SDL_JoystickInstanceID(j))
                {
                    di = k;
                    break;
                }
            }
            if (di < 0)
            {
                continue;
            }
        }
        const int excl = (s_Assign.exclude != 0) ? inputAssignProbeExcluded(di) : 0;

        if (inputDeviceIsWgiShadow(di, (j != NULL && di != i) ? SDL_NumJoysticks() : s_Assign.nprobe))
        {   /* one pad seen twice: a press must pin the GUID startup will see */
            continue;
        }
        if (excl > 1)
        {   /* an earlier player's pad (portInputAssignBeginEx): deaf to it */
            continue;
        }
        if (nb > 64) nb = 64;
        if (nh > 8) nh = 8;
        for (b = 0; b < nb && !hit; b++)
        {
            const int down = SDL_JoystickGetButton(j, b);

            if (!s_Assign.baseBtn[i][b] && down)
            {
                hit = 1;
                hitBtn = b;
                /* held over: see portInputPadMerge. On player 1's own pad
                 * (a skip) it is player 1's press that must not leak on. */
                s_PadCapHold.slot = (excl == 1) ? 0 : s_Assign.slot;
                s_PadCapHold.entry.kind = 'b';
                s_PadCapHold.entry.idx = (s16) b;
                s_PadCapHold.entry.dir = 0;
            }
            else if (s_Assign.baseBtn[i][b] && !down)
            {
                s_Assign.baseBtn[i][b] = 0;   /* released: a fresh press now counts */
            }
        }
        for (b = 0; b < nh && !hit; b++)
        {
            const u8 h = SDL_JoystickGetHat(j, b);

            if (h != s_Assign.baseHat[i][b] && h != SDL_HAT_CENTERED)
            {
                hit = 1;
            }
            else if (h == SDL_HAT_CENTERED)
            {
                s_Assign.baseHat[i][b] = SDL_HAT_CENTERED;
            }
        }
        {   /* A trigger pull counts as a press too: a near-full-travel move
             * from the axis's rest (so a resting hand doesn't pick a pad),
                     * except an exact endpoint jump from a zero baseline, which
                     * is the pad's first report. */
            int na = (j != NULL) ? SDL_JoystickNumAxes(j) : 0;

            if (na > 32) na = 32;
            for (b = 0; b < na && !hit; b++)
            {
                const s32 v = SDL_JoystickGetAxis(j, b);
                const s32 base = s_Assign.baseAxis[i][b];

                if (base == 0 && (v == -32768 || v == 32767))
                {
                    s_Assign.baseAxis[i][b] = (s16) v;
                    continue;
                }
                /* a trigger from either rest end, or a CENTRED stick at near-
                 * full travel any way: half a push right or down must not pass
                 * as a trigger pull and seat an idle player */
                if ((base < -16000 && v - base > 16000 && v > 8000)
                    || (base > 16000 && base - v > 16000 && v < -8000)
                    || (base > -8000 && base < 8000 && (v < -24000 || v > 24000)))
                {
                    hit = 1;
                }
                else if (SDL_abs(v - base) < 4000)
                {
                    s_Assign.baseAxis[i][b] = (s16) v;   /* drifted home: track it */
                }
            }
        }
        if (hit && excl == 1)
        {   /* player 1, on the pad that runs the menu: skip this player */
            s_Assign.done = 1;
            s_Assign.result = 2;
            return 1;
        }
        if (hitBtn >= 0 && !s_Assign.seating && s_Assign.slot != 0
            && (s_Pads[0] != NULL || s_Joys[0] != NULL) && s_PadRaphnet[0] == 0
            && s_PadInstance[0] == SDL_JoystickGetDeviceInstanceID(di))
        {   /* B ON PLAYER 1'S PAD CANCELS: player 1
                     * runs this menu, and B meant "back", not "give my pad to
                     * the player being assigned". Any other button still
                     * assigns it. */
            const PadMapEntry e = { 'b', (s16) hitBtn, 0 };
            const int mb = inputPmIndex("menu_back");

            if (inputCapIsControl(0, PM_B, &e) || (mb >= 0 && inputCapIsControl(0, mb, &e)))
            {
                s_PadCapHold.slot = 0;   /* its release is player 1's, not a menu B */
                s_Assign.done = 1;       /* result -1 = cancelled, like Escape */
                return 1;
            }
        }
        if (hit)
        {
            char g[40];
            char want[48];
            int occ = 0;
            int k;

            if (inputDeviceIsRaphnetClass(di))
            {   /* A raphnet is always the RAW path: this press came through the
                 * adapter's plain joystick face (vendor interface momentarily
                 * down),
                             * so pin the RAW channel (the one with a button
                             * down, else channel 0) and let raphnet.c's
                             * reconnect bring it up. */
                int chan = 0;
                int c;
                u16 newBits = 0;   /* the RAW bits pressed on `chan` since the arm */

                if (portRaphnetAvailable())
                {
                    int heard = 0;

                    for (c = 0; c < 2; c++)
                    {
                        u16 btn = 0;

                        if (portRaphnetPoll(c, &btn, NULL, NULL) == 1 && (btn & ~s_Assign.rnBase[c]) != 0)
                        {
                            chan = c;
                            heard = 1;
                            newBits = (u16) (btn & ~s_Assign.rnBase[c]);
                        }
                    }
                    if (!heard)
                    {   /* the RAW path is up and none of ITS channels moved: a second adapter
                                             * or the poller not caught up yet. Don't
                                             * pin raphnet0 (it may be player 1's pad);
                                             * wait - a held button is heard again. */
                        continue;
                    }
                }
                if (!s_Assign.seating && s_Assign.slot != 0 && s_PadRaphnet[0] == chan + 1
                    && (newBits & CONT_B) != 0)
                {   /* B on player 1's N64 pad, heard here through the
                                     * adapter's HID face before the RAW loop
                                     * below: cancel, never hand player 1's pad
                                     * over */
                    s_Assign.done = 1;
                    return 1;
                }
                {
                    const int cex = (s_Assign.exclude != 0) ? inputAssignChanExcluded(chan) : 0;

                    if (cex > 1)
                    {
                        continue;   /* an earlier player's channel */
                    }
                    if (cex == 1)
                    {
                        s_Assign.done = 1;
                        s_Assign.result = 2;   /* player 1 skipped this player */
                        return 1;
                    }
                }
                SDL_snprintf(want, sizeof(want), "raphnet%d", chan);
                s_Assign.done = 1;
                s_Assign.result = 1;
                if (s_Assign.slot == 0)
                {
                    s_OptP1View = 1;
                    portVideoIniSetKey("p1_view", "pad");
                    s_P1Device = 1;   /* pad owns player 1 */
                }
                inputAssignPin(s_Assign.slot, want);
                return 1;
            }
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(di), g, sizeof(g));
            for (k = 0; k < di; k++)
            {   /* identical pads are told apart by order: guid#n */
                char g2[40];

                SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(k), g2, sizeof(g2));
                if (SDL_strcasecmp(g2, g) == 0)
                {
                    occ++;
                }
            }
            if (occ > 0)
            {
                SDL_snprintf(want, sizeof(want), "%s#%d", g, occ);
            }
            else
            {
                SDL_strlcpy(want, g, sizeof(want));
            }
            s_Assign.done = 1;
            s_Assign.result = 1;
            if (s_Assign.slot == 0)
            {
                s_OptP1View = 1;
                portVideoIniSetKey("p1_view", "pad");
                s_P1Device = 1;   /* picked for player 1: p1_hybrid=0 applies */
            }
            else if (SDL_JoystickGetDeviceInstanceID(di) == s_P1Instance)
            {   /* the intro claimer, sent elsewhere on purpose: pass 0 must
                             * not pull it back to player 1 */
                s_P1Instance = -1;
            }
            inputAssignPin(s_Assign.slot, want);
            return 1;
        }
    }
    /* a RAPHNET channel: a button on the N64 pad plugged into it */
    if (s_Assign.rnOk)
    {
        for (i = 0; i < 2; i++)
        {
            u16 btn = 0;

            if (portRaphnetPoll(i, &btn, NULL, NULL) == 1 && (btn & ~s_Assign.rnBase[i] & CONT_B) != 0
                && !s_Assign.seating && s_Assign.slot != 0 && s_PadRaphnet[0] == i + 1)
            {   /* B on player 1's N64 pad cancels, as on an SDL pad */
                s_Assign.done = 1;
                return 1;
            }
            if (portRaphnetPoll(i, &btn, NULL, NULL) == 1 && (btn & ~s_Assign.rnBase[i]) != 0)
            {
                char want[16];
                const int cex = (s_Assign.exclude != 0) ? inputAssignChanExcluded(i) : 0;

                if (cex > 1)
                {   /* an earlier player's channel (portInputAssignBeginEx) */
                    s_Assign.rnBase[i] = btn;
                    continue;
                }
                if (cex == 1)
                {   /* player 1 skipped this player */
                    s_Assign.done = 1;
                    s_Assign.result = 2;
                    return 1;
                }
                SDL_snprintf(want, sizeof(want), "raphnet%d", i);
                s_Assign.done = 1;
                s_Assign.result = 1;
                if (s_Assign.slot == 0)
                {
                    s_OptP1View = 1;
                    portVideoIniSetKey("p1_view", "pad");
                    s_P1Device = 1;   /* pad owns player 1 */
                }
                inputAssignPin(s_Assign.slot, want);
                return 1;
            }
            s_Assign.rnBase[i] &= btn;   /* released bits count again */
        }
    }
    /* the KEYBOARD OR THE MOUSE, for player 1 only: any key or click =
     * "mouse and keyboard". Escape cancels on every player. */
    {   /* While the console is open (or on the poll after it closes) the keys
         * belong to the prompt, so the ` and Esc must not answer
             * the arm: only re-base. The pads above and the timeout keep running. */
        static int sConWasOpen;
        const int conNow = portConIsOpen() != 0;
        const int skip = conNow || sConWasOpen;

        sConWasOpen = conNow;
        if (skip)
        {
            SDL_memcpy(s_Assign.keyBase, SDL_GetKeyboardState(NULL), SDL_NUM_SCANCODES);
            s_Assign.mouseBase = SDL_GetMouseState(NULL, NULL);
            return 0;
        }
    }
    keys = SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_ESCAPE] && !s_Assign.keyBase[SDL_SCANCODE_ESCAPE])
    {
        s_Assign.done = 1;
        return 1;
    }
    {
        const u32 mb = SDL_GetMouseState(NULL, NULL);

        if ((mb & ~s_Assign.mouseBase) != 0 && s_Assign.slot == 0)
        {
            s_Assign.done = 1;
            s_Assign.result = 0;
            s_OptP1View = 0;
            portVideoIniSetKey("p1_view", "kbm");
            s_P1Device = 0;   /* player 1 is the keyboard and mouse again */
            return 1;
        }
        if ((mb & ~s_Assign.mouseBase) != 0 && s_Assign.seating)
        {   /* player 1 on the mouse, seating the others: skip this player */
            s_Assign.done = 1;
            s_Assign.result = 2;
            return 1;
        }
        s_Assign.mouseBase &= mb;   /* a released button counts again */
    }
    for (i = 0; i < SDL_NUM_SCANCODES; i++)
    {
        if (keys[i] && !s_Assign.keyBase[i])
        {
            if (s_Assign.slot == 0)
            {
                s_Assign.done = 1;
                s_Assign.result = 0;
                s_OptP1View = 0;
                portVideoIniSetKey("p1_view", "kbm");
                s_P1Device = 0;   /* keyboard and mouse own player 1 */
                return 1;
            }
            if (s_Assign.seating)
            {   /* ...and on the keyboard */
                s_Assign.done = 1;
                s_Assign.result = 2;
                return 1;
            }
        }
        else if (!keys[i])
        {
            s_Assign.keyBase[i] = 0;
        }
    }
    return 0;
}

/**
 * After Tick said 1: the DEVICE index the page should now show - the pad
 * that landed in the slot, or 0 for the keyboard. -1 = nothing happened
 * (timed out or Escape). -2 = player 1 skipped this player (only from
 * portInputAssignBeginEx). Ends the arm.
 */
s32 portInputAssignTake(void)
{
    extern void portBindCooldownStart(void);
    const int slot = s_Assign.slot;
    const int result = s_Assign.result;
    s32 dev = -1;
    s32 d;

    if (slot < 0)
    {
        return -1;
    }
    if (result == 0)
    {
        dev = 0;
    }
    else if (result == 1)
    {
        dev = 0;
        for (d = 1; d < portInputDeviceCount(); d++)
        {
            if (portInputDeviceSlot(d) == slot)
            {
                dev = d;
                break;
            }
        }
    }
    else if (result == 2)
    {
        dev = -2;   /* portInputAssignBeginEx: player 1 skipped this player */
    }
    if (result >= 0)
    {
        portBindCooldownStart();   /* the press that assigned must not also confirm the row */
    }
    portInputAssignCancel();
    return dev;
}

/**
 * Bind (or with token NULL / "-", unbind) one control on a pad. Seeds the
 * slot's map first - see point 1 above - then writes the key live and to
 * midas.ini through the ordinary setter.
 */
s32 portInputPadBindSet(s32 dev, const char *name, const char *token)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);
    char key[32];

    if (name == NULL || !portInputDeviceIniKey(dev, name, key, (s32) sizeof(key)))
    {
        return 0;
    }
    if (!portInputPadSeedMap(dev))
    {
        return 0;
    }
    {   /* an AXIS on a movement / look row is the stick, not a button - see
         * inputDigitalRowStick. The digital row is cleared only if it held
         * an axis itself (the old on/off binding); a d-pad on it stays. */
        int spm, flip;
        const s32 slot = portInputDeviceSlot(dev);
        int trig = 0;

        if (token != NULL && token[0] == 'a' && token[1] >= '0' && token[1] <= '9' && slot >= 0)
        {   /* NOT a trigger: it rests at one end, and as the Move stick it
                     * would walk Bond backwards non-stop. A trigger stays a
                     * digital row (gated on trigger_threshold from its rest). */
            const int ax = SDL_atoi(token + 1);

            trig = inputAxisTriggerByMapping(slot, ax);
            if (trig < 0)
            {   /* raw pad: its learned rest, if it has one */
                trig = (ax < 32 && (s_PadAxisRestSeen[slot] & (1u << ax)) != 0
                        && (s_PadAxisRest[slot][ax] <= -24000 || s_PadAxisRest[slot][ax] >= 24000));
            }
        }
        if (token != NULL && token[0] == 'a' && token[1] >= '0' && token[1] <= '9'
            && slot >= 0 && !trig && inputDigitalRowStick(name, &spm, &flip))
        {
            char stok[16];
            char skey[32];

            SDL_strlcpy(stok, token, sizeof(stok));
            if (flip)
            {
                inputAxisTokenFlip(stok);
            }
            {
                const int dpm = inputPmIndex(name);
                char keep[96];
                int dropped = 0;
                int a2;

                /* EVERY axis on the row goes, primary or alternate (an older
                 * capture could ADD "h0u,a1-"), and its d-pad entries stay */
                keep[0] = '\0';
                for (a2 = -1; dpm >= 0 && a2 < PM_MAX_ALT; a2++)
                {
                    const PadMapEntry *de = (a2 < 0) ? &s_PadMap[slot][dpm] : &s_PadMapAlt[slot][dpm][a2];
                    char t[16];

                    if (de->kind == 'a')
                    {
                        dropped = 1;
                    }
                    if (de->kind == 0 || de->kind == 'a')
                    {
                        continue;
                    }
                    inputMapTokenText(de, t, sizeof(t));
                    if (keep[0] != '\0')
                    {
                        SDL_strlcat(keep, ",", sizeof(keep));
                    }
                    SDL_strlcat(keep, t, sizeof(keep));
                }
                if (dropped)
                {
                    portVideoIniSetKey(key, (keep[0] != '\0') ? keep : "-");
                }
            }
            if (!portInputDeviceIniKey(dev, sPadMapNames[spm], skey, (s32) sizeof(skey)))
            {
                return 0;
            }
            return portVideoIniSetKey(skey, stok);
        }
        /* UNBINDING a row that is showing the stick: the stick is what the
         * player is looking at, so that is what goes */
        if ((token == NULL || token[0] == '\0' || token[0] == '-')
            && slot >= 0 && inputDigitalRowStick(name, &spm, &flip))
        {
            const int dpm = inputPmIndex(name);

            if (dpm >= 0 && s_PadMap[slot][dpm].kind == 0 && s_PadMap[slot][spm].kind == 'a')
            {
                char skey[32];

                if (portInputDeviceIniKey(dev, sPadMapNames[spm], skey, (s32) sizeof(skey)))
                {
                    portVideoIniSetKey(skey, "-");
                }
            }
        }
    }
    if (token != NULL && token[0] != '\0' && token[0] != '-')
    {   /* A capture ADDS to the row like the keyboard's (e.g. zoom on both
         * D-pad and stick): primary plus up to PM_MAX_ALT
             * alternates, oldest dropped when full, no duplicates. Unbinding
             * (the "-" path below) still clears the whole row. */
        const s32 slot = portInputDeviceSlot(dev);
        const int pm = inputPmIndex(name);
        PadMapEntry cur[1 + PM_MAX_ALT];
        PadMapEntry add;
        char list[96];
        int n = 0;
        int a, k;

        if (slot >= 0 && pm >= 0 && inputParseMapToken(token, &add))
        {
            if (s_PadMap[slot][pm].kind != 0)
            {
                cur[n++] = s_PadMap[slot][pm];
            }
            for (a = 0; a < PM_MAX_ALT; a++)
            {
                if (s_PadMapAlt[slot][pm][a].kind != 0)
                {
                    cur[n++] = s_PadMapAlt[slot][pm][a];
                }
            }
            for (k = 0; k < n; k++)
            {
                if (cur[k].kind == add.kind && cur[k].idx == add.idx && cur[k].dir == add.dir)
                {
                    return 1;   /* already on the row */
                }
            }
            if (n == 1 + PM_MAX_ALT)
            {   /* full: the oldest goes */
                for (k = 1; k < n; k++)
                {
                    cur[k - 1] = cur[k];
                }
                n--;
            }
            cur[n++] = add;
            list[0] = '\0';
            for (k = 0; k < n; k++)
            {
                char t[16];

                inputMapTokenText(&cur[k], t, sizeof(t));
                if (k > 0)
                {
                    SDL_strlcat(list, ",", sizeof(list));
                }
                SDL_strlcat(list, t, sizeof(list));
            }
            return portVideoIniSetKey(key, list);
        }
    }
    return portVideoIniSetKey(key, (token != NULL && token[0] != '\0') ? token : "-");
}

/**
 * GE_PADBIND=<control>=<token>: bind it on player 1 once, a few polls in. A
 * headless test of everything after a capture (seed before write, live + midas.ini
 * write, readback naming) without needing a finger. Never set in midas.ini.
 */
static void inputPadBindHarness(void)
{
    static int state = -1;
    static u32 polls;
    static char ctl[32], tok[16];

    if (state == 0)
    {
        return;
    }
    if (state < 0)
    {
        const char *e = getenv("GE_PADBIND");
        const char *eq = (e != NULL) ? SDL_strchr(e, '=') : NULL;

        state = 0;
        if (eq == NULL || eq == e)
        {
            return;
        }
        SDL_strlcpy(ctl, e, (size_t) ((eq - e) + 1 > (int) sizeof(ctl) ? (int) sizeof(ctl) : (eq - e) + 1));
        SDL_strlcpy(tok, eq + 1, sizeof(tok));
        state = 1;
        polls = 0;
        return;
    }
    if (++polls < 30)
    {
        return;   /* let the slots settle first */
    }
    state = 0;
    if (!inputSlotBusy(0))
    {   /* device 1 is the first OCCUPIED slot, player 1 only when slot 0
             * holds a pad */
        portConLogf("ge: input: GE_PADBIND refused (no pad in player 1)\n");
        return;
    }
    portConLogf("ge: input: GE_PADBIND %s = %s on player 1\n", ctl, tok);
    if (!portInputPadBindSet(1, ctl, tok))
    {
        portConLogf("ge: input: GE_PADBIND refused (no pad in player 1, or no such control)\n");
        return;
    }
    {
        char text[64];

        if (portInputDeviceBindText(1, ctl, text, (s32) sizeof(text)))
        {
            portConLogf("ge: input: %s now reads '%s'\n", ctl, text);
        }
    }
}

/**
 * GE_PADLAYOUT=list | save | apply=<name> | delete=<name>: do it once on player
 * 1, a few polls in, to test whole named layouts the way the bind harness above
 * tests one control. Never set in midas.ini.
 */
static void inputPadLayoutHarness(void)
{
    static int state = -1;
    static u32 polls;
    static char op[16], arg[64];
    int i;

    if (state == 0)
    {
        return;
    }
    if (state < 0)
    {
        const char *e = getenv("GE_PADLAYOUT");
        const char *eq;

        state = 0;
        if (e == NULL || e[0] == '\0')
        {
            return;
        }
        eq = SDL_strchr(e, '=');
        if (eq != NULL)
        {
            int n = (int) (eq - e) + 1;
            SDL_strlcpy(op, e, (size_t) (n > (int) sizeof(op) ? (int) sizeof(op) : n));
            SDL_strlcpy(arg, eq + 1, sizeof(arg));
        }
        else
        {
            SDL_strlcpy(op, e, sizeof(op));
            arg[0] = '\0';
        }
        state = 1;
        polls = 0;
        return;
    }
    if (++polls < 40)
    {
        return;   /* after the bind harness, so save sees a rebound pad */
    }
    state = 0;
    if (SDL_strcasecmp(op, "list") == 0)
    {
        for (i = 0; i < portInputLayoutCount(); i++)
        {
            portConLogf("ge: input: layout %d '%s'%s\n", i, portInputLayoutName(i),
                    portInputLayoutIsUser(i) ? " (saved)" : "");
        }
        return;
    }
    if ((SDL_strcasecmp(op, "save") == 0 || SDL_strcasecmp(op, "apply") == 0) && !inputSlotBusy(0))
    {   /* device 1 is the first OCCUPIED slot: with player 1 on the keyboard
             * it is player 2's pad */
        portConLogf("ge: input: GE_PADLAYOUT %s refused (no pad in player 1)\n", op);
        return;
    }
    if (SDL_strcasecmp(op, "save") == 0)
    {
        portInputLayoutSaveCurrent(1, (arg[0] != '\0') ? arg : NULL);
        return;
    }
    for (i = 0; i < portInputLayoutCount(); i++)
    {
        if (SDL_strcasecmp(portInputLayoutName(i), arg) != 0)
        {
            continue;
        }
        if (SDL_strcasecmp(op, "apply") == 0)
        {
            portInputLayoutApply(1, i);
        }
        else if (SDL_strcasecmp(op, "delete") == 0)
        {
            portInputLayoutDelete(i);
        }
        return;
    }
    portConLogf("ge: input: GE_PADLAYOUT: no layout called '%s'\n", arg);
}

/** midas.ini key for a device's control: "padN_<name>". 0 = not a pad device. */
s32 portInputDeviceIniKey(s32 dev, const char *name, char *buf, s32 len)
{
    s32 slot = portInputDeviceSlot(dev);

    if (slot < 0 || name == NULL || buf == NULL || len <= 0)
    {
        return 0;
    }
    SDL_snprintf(buf, (size_t) len, "pad%d_%s", slot + 1, name);
    return 1;
}

/* ---- Port Options bridge (port_options.c, in-game menus) ---------------
 *
 * Integer view of the [input] scalars: floats travel as TENTHS (2.5 -> 25);
 * deadzone / trigger_threshold stay in RAW ini units (the menu shows them as a
 * percent of 32768 / 32767), because the launcher and parser expect the raw
 * number. Each is read per poll, so a set is live. Pad assignment / binds /
 * dinput are launcher-only and not listed.
 */
s32 portInputOptionGet(const char *key, s32 *out)
{
    if (SDL_strcmp(key, "mouse") == 0)             { *out = s_OptMouse; return 1; }
    if (SDL_strcmp(key, "smart_mouse_lock") == 0)  { *out = s_OptSmartLock; return 1; }
    if (SDL_strcmp(key, "p1_hybrid") == 0)         { *out = s_OptP1Hybrid; return 1; }
    if (SDL_strcmp(key, "mouse_sens_x") == 0)      { *out = (s32)(s_OptMouseSensX * 10.0f + 0.5f); return 1; }
    if (SDL_strcmp(key, "mouse_sens_y") == 0)      { *out = (s32)(s_OptMouseSensY * 10.0f + 0.5f); return 1; }
    if (SDL_strcmp(key, "mouse_sens_aim") == 0)    { *out = (s32)(s_OptMouseSensAim * 10.0f + 0.5f); return 1; }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_'
        && SDL_strcmp(key + 5, "swap_sticks") == 0)
    {
        *out = s_PadSwapOpt[key[3] - '1'];
        return 1;
    }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_'
        && SDL_strcmp(key + 5, "gadget_switch") == 0)
    {
        *out = s_PadGadgetSwitch[key[3] - '1'];
        return 1;
    }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_'
        && SDL_strcmp(key + 5, "lr_classic") == 0)
    {
        *out = s_PadLrClassic[key[3] - '1'];
        return 1;
    }
    if (SDL_strcmp(key, "deadzone") == 0)          { *out = s_OptDeadzone; return 1; }
    if (SDL_strcmp(key, "deadzone_look") == 0)     { *out = s_OptDeadzoneLook; return 1; }
    if (SDL_strcmp(key, "trigger_threshold") == 0) { *out = s_OptTrigThreshold; return 1; }
    if (SDL_strcmp(key, "stick_scale") == 0)       { *out = (s32)(s_OptStickScale * 10.0f + 0.5f); return 1; }
    if (SDL_strcmp(key, "pads") == 0)              { *out = s_OptPads; return 1; }
    if (SDL_strcmp(key, "rumble") == 0)            { *out = s_OptRumble; return 1; }
    if (SDL_strcmp(key, "show_input") == 0)        { *out = s_OptShowInput; return 1; }
    return 0;
}

s32 portInputOptionSet(const char *key, s32 v)
{
    if (SDL_strcmp(key, "mouse") == 0)             { s_OptMouse = v != 0; return 1; }
    if (SDL_strcmp(key, "smart_mouse_lock") == 0)  { s_OptSmartLock = v != 0; return 1; }
    if (SDL_strcmp(key, "p1_hybrid") == 0)         { s_OptP1Hybrid = v != 0; return 1; }
    if (SDL_strcmp(key, "mouse_sens_x") == 0)      { s_OptMouseSensX = (float) v / 10.0f; return 1; }
    if (SDL_strcmp(key, "mouse_sens_y") == 0)      { s_OptMouseSensY = (float) v / 10.0f; return 1; }
    if (SDL_strcmp(key, "mouse_sens_aim") == 0)    { s_OptMouseSensAim = (float) v / 10.0f; return 1; }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_'
        && SDL_strcmp(key + 5, "swap_sticks") == 0)
    {
        s_PadSwapOpt[key[3] - '1'] = v != 0;
        inputSwapLog(key[3] - '1');
        return 1;
    }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_'
        && SDL_strcmp(key + 5, "gadget_switch") == 0)
    {
        s_PadGadgetSwitch[key[3] - '1'] = v != 0;
        return 1;
    }
    if (SDL_strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_'
        && SDL_strcmp(key + 5, "lr_classic") == 0)
    {
        s_PadLrClassic[key[3] - '1'] = v != 0;
        return 1;
    }
    if (SDL_strcmp(key, "deadzone") == 0)          { s_OptDeadzone = SDL_clamp(v, 0, 32767); return 1; }
    if (SDL_strcmp(key, "deadzone_look") == 0)     { s_OptDeadzoneLook = SDL_clamp(v, 0, 32767); return 1; }
    if (SDL_strcmp(key, "trigger_threshold") == 0) { s_OptTrigThreshold = v; return 1; }
    if (SDL_strcmp(key, "stick_scale") == 0)       { s_OptStickScale = (float) v / 10.0f; return 1; }
    if (SDL_strcmp(key, "pads") == 0)              { s_OptPads = (v < 1) ? 1 : (v > 4) ? 4 : v; return 1; }
    if (SDL_strcmp(key, "rumble") == 0)            { s_OptRumble = v != 0; return 1; }
    if (SDL_strcmp(key, "show_input") == 0)        { s_OptShowInput = v != 0; return 1; }
    return 0;
}

/* ---- pad reads ---------------------------------------------------------- */

/**
 * Which N64 pad slots report "controller present". Slot 0 always (keyboard);
 * midas.ini pads=N forces the first N connected so MP setup always offers them
 * (a dead pad just reads neutral - same as a plugged-in idle controller).
 */
u32 portInputPadMask(void)
{
    u32 mask = 1;
    int i;
    int forced = s_OptPads;

    if (forced < 1) forced = 1;
    if (forced > INPUT_MAX_PADS) forced = INPUT_MAX_PADS;
    mask = (u32)((1 << forced) - 1);

    for (i = 0; i < INPUT_MAX_PADS; i++)
    {
        if (s_Pads[i] != NULL || s_Joys[i] != NULL || s_PadRaphnet[i] != 0)
        {
            mask |= (u32)(1 << i);
        }
    }
    return mask;
}

/* Round stick onto the N64 octagon: a modern stick's circular housing reaches
 * only ~(60, 60) on a diagonal versus the N64's (65, 65), so each direction's
 * full reach is stretched to the octagon edge (cardinals unchanged, half a push
 * stays half); scaled pads' Move stick only. */
static void inputCircleToOctagon(s32 *x, s32 *y)
{
    const double ax = (*x < 0) ? -(double) *x : (double) *x;
    const double ay = (*y < 0) ? -(double) *y : (double) *y;
    const double r = SDL_sqrt(ax * ax + ay * ay);
    double edge;
    double f;

    if (r < 1.0)
    {
        return;
    }
    edge = 130.0 * r / (ax + ay);                            /* the octagon's diagonal faces */
    f = 85.0 * r / ((ax > ay) ? ax : ay);                    /* ...and its cardinal faces */
    if (f < edge)
    {
        edge = f;
    }
    f = edge / 85.0;                                         /* 1.0 on a cardinal, ~1.08 at 45 deg */
    *x = (s32) ((double) *x * f + ((*x < 0) ? -0.5 : 0.5));
    *y = (s32) ((double) *y * f + ((*y < 0) ? -0.5 : 0.5));
}

/** PD inputAxisScale: deadzone-rescale one raw axis, keep sign, clamp. */
static s32 inputAxisScaleDz(s32 x, s32 dz)
{
    if (abs(x) < dz || dz >= 32768)   /* never divide by zero */
    {
        return 0;
    }
    if (x < 0)
    {
        x += dz;
    }
    else
    {
        x -= dz;
    }
    x = x * 32768 / (32768 - dz);
    x = (s32)(x * s_OptStickScale);
    return (x > 32767) ? 32767 : ((x < -32768) ? -32768 : x);
}

/* the Move stick's deadzone ([input] deadzone), and the Look stick's (deadzone_look) */
static s32 inputAxisScale(s32 x)
{
    return inputAxisScaleDz(x, s_OptDeadzone);
}

static s32 inputAxisScaleLook(s32 x)
{
    return inputAxisScaleDz(x, s_OptDeadzoneLook);
}

/* HOW FAR a control is held, 0..1, for a port action that wants a level rather
 * than a switch (e.g. analog zoom on a stick). Button/hat = 0 or 1; axis =
 * deflection past the dead zone, or for a trigger the fraction of the pull past
 * trigger_threshold. */
static f32 inputEntryLevel(SDL_Joystick *joy, int idx, const PadMapEntry *e)
{
    s32 v, rest, s;
    int ax;

    switch (e->kind)
    {
    case 'b':
    case 'h':
        return inputEntryDown(joy, idx, e) ? 1.0f : 0.0f;
    case 'a':
        ax = inputMapAxis(idx, e->idx);
        v = SDL_JoystickGetAxis(joy, ax);
        rest = inputAxisRest(idx, ax, v);
        /* a trigger: the fraction of the pull PAST trigger_threshold, from 0,
         * not jumping straight to the Trigger Point's own fraction */
        if (rest <= -24000 && e->dir > 0)
        {
            s = (v - rest) * 32767 / (32767 - rest);
            if (s <= s_OptTrigThreshold)
            {
                return 0.0f;
            }
            return SDL_min((f32) (s - s_OptTrigThreshold) / (f32) (32767 - s_OptTrigThreshold), 1.0f);
        }
        if (rest >= 24000 && e->dir < 0)
        {
            s = (rest - v) * 32767 / (rest + 32768);
            if (s <= s_OptTrigThreshold)
            {
                return 0.0f;
            }
            return SDL_min((f32) (s - s_OptTrigThreshold) / (f32) (32767 - s_OptTrigThreshold), 1.0f);
        }
        s = inputAxisScale(v);
        if (e->dir < 0)
        {
            s = -s;
        }
        return (s > 0) ? (f32) s / 32767.0f : 0.0f;
    default:
        return 0.0f;
    }
}

/** joy_port.c: the level of port action `act` on pad `slot`, 0..1, over the
 * primary token and every alternate. 0 for a slot with no SDL handle. */
f32 portInputPadActionLevel(s32 slot, s32 act)
{
    SDL_Joystick *joy;
    const int i = PM_PORT_FIRST + act;
    f32 best = 0.0f;
    f32 l;
    int a;

    if (slot < 0 || slot >= INPUT_MAX_PADS || act < 0 || i > PM_PORT_LAST)
    {
        return 0.0f;
    }
    joy = (s_Joys[slot] != NULL) ? s_Joys[slot]
        : (s_Pads[slot] != NULL) ? SDL_GameControllerGetJoystick(s_Pads[slot]) : NULL;
    if (joy == NULL)
    {
        return 0.0f;
    }
    best = inputEntryLevel(joy, slot, &s_PadMap[slot][i]);
    for (a = 0; a < PM_MAX_ALT; a++)
    {
        l = inputEntryLevel(joy, slot, &s_PadMapAlt[slot][i][a]);
        if (l > best)
        {
            best = l;
        }
    }
    return best;
}

/**
 * Merge SDL controller `idx` into an already-filled pad (keyboard for slot 0,
 * neutral for 1-3). Buttons OR in; the stick overrides only when deflected,
 * so keyboard analog keys still work with a controller connected.
 */
/* Digital move per slot this poll: -1/0/+1 on x (right) and y (up) from the
 * pad's move_* rows (a D-pad by default). bondviewProcessInput turns these into
 * the C buttons' own digital steps in port mode (see portInputDigitalMove). */
static s8 s_PadDigMove[INPUT_MAX_PADS][2];

/** bondview2.c, the port drive block: this player's digital Move, pad and
 * (player 1) keyboard together. 1 when either axis is pressed. */
s32 portInputDigitalMove(s32 slot, s32 *dx, s32 *dy)
{
    extern void portVideoKbdDigitalMove(s32 *dx, s32 *dy);
    s32 x = 0;
    s32 y = 0;

    if (slot >= 0 && slot < INPUT_MAX_PADS)
    {
        x = s_PadDigMove[slot][0];
        y = s_PadDigMove[slot][1];
    }
    if (slot == 0)
    {
        s32 kx = 0;
        s32 ky = 0;

        portVideoKbdDigitalMove(&kx, &ky);
        x = (x != 0) ? x : kx;
        y = (y != 0) ? y : ky;
        { /* GE_INPUTREC / GE_INPUTPLAY (inputrec.c) */
            extern void portInputRecDigMove(s32 *dx, s32 *dy);
            portInputRecDigMove(&x, &y);
        }
    }
    *dx = x;
    *dy = y;
    return x != 0 || y != 0;
}

/** front.c menus: player 1's PAD digital Move alone (-1 / 0 / +1, y up), no
  * keyboard. On a Pro / Standard layout that is the D-pad,
 * which the merge also writes into the stick, so menus use this to step lists
 * instead of moving the crosshair. */
/** front.c / options.c drop-downs: player 1's LOOK stick, up/down, in N64
 *  units (+/-85, up positive), used to quick-scroll a box; 0 with no look stick this poll */
s32 portInputPadLookYP1(void)
{
    return s_PadHasRight[0] ? (s32) s_PadRightY[0] : 0;
}

s32 portInputPadDigMoveP1(s32 *dx, s32 *dy)
{
    *dx = s_PadDigMove[0][0];
    *dy = s_PadDigMove[0][1];
    return *dx != 0 || *dy != 0;
}

/**
 * Give a player to any raphnet channel that has a pad on it and no slot,
 * catching pads that pass 1b (hotplug/menu assigns only) missed. Runs about
 * once a second from portInputPollTick and only PEEKS, since reading an empty
 * channel keeps it hot and starves the pad in play. Probes of an unopened
 * adapter are capped (s_RaphnetOpenTries) until the next hotplug ADDED, and
 * 1100 ms sits just past raphnet.c's 1000 ms reopen throttle.
 */
static void inputRaphnetRecheck(void)
{
    const unsigned now = SDL_GetTicks();
    int ev = 0;
    int held = 0;
    int open = 0;
    int c, slot;

    if (s_RaphnetRecheckAt != 0 && (int) (now - s_RaphnetRecheckAt) < 0)
    {
        return;
    }
    s_RaphnetRecheckAt = now + 1100;
    for (slot = 0; slot < INPUT_MAX_PADS; slot++)
    {
        held |= (s_PadRaphnet[slot] != 0);   /* a RAW slot = the adapter is open: looks are cheap */
        open |= (!inputSlotBusy(slot)
                 && (s_OptPadAssign[slot][0] == '\0' || SDL_strcasecmp(s_OptPadAssign[slot], "auto") == 0));
    }
    ev = held;
    for (c = 0; c < SDL_NumJoysticks() && !ev; c++)
    {
        ev = inputDeviceIsRaphnetClass(c);
    }
    if (!ev || !open || (!held && s_RaphnetOpenTries <= 0))
    {
        return;
    }
    if (!portRaphnetAvailable())
    {
        if (s_RaphnetOpenTries > 0 && --s_RaphnetOpenTries == 0)
        {
            portConLogf("ge: input: raphnet still not answering - next look on the next hotplug or press-to-assign\n");
        }
        return;
    }
    for (c = 0; c < 2; c++)
    {
        int taken = 0;

        for (slot = 0; slot < INPUT_MAX_PADS; slot++)
        {
            taken |= (s_PadRaphnet[slot] == c + 1);
        }
        if (!taken && portRaphnetPeek(c) == 1)
        {
            portConLogf("ge: input: raphnet ch%d has a pad and no player - assigning\n", c);
            inputAssignSlotsEx(0);
            inputLogSlots("raphnet recheck");
            return;
        }
    }
}

/**
 * Once per pad poll, from libultra_shim.c BEFORE the per-pad merges: slot
 * housekeeping that may open a device or rewrite slot settings, so it must not
 * run inside portInputPadMerge (see WHO IS PLAYER 1). Runs even with the
 * console open or GE_NOINPUT.
 */
void portInputPollTick(void)
{
    inputRaphnetRecheck();          /* a raphnet pad with no player gets one */
    inputP1DeviceSettingsCheck();   /* a different device is player 1: its own settings */
}

void portInputPadMerge(s32 idx, OSContPad *pad)
{
    SDL_GameController *ctrl;
    u16 buttons = 0;

    if (idx >= 0 && idx < INPUT_MAX_PADS)
    {
        s_PadDigMove[idx][0] = 0;   /* this poll's answer, set below if a move row is down */
        s_PadDigMove[idx][1] = 0;
    }
    s32 lx, ly, rx, ry;
    SDL_GameControllerAxis mvx, mvy, lkx, lky; /* swap_sticks: which physical stick is Move / Look */

    if (idx < 0 || idx >= INPUT_MAX_PADS || pad == NULL)
    {
        return;
    }
    {   /* A stall (window drag, Alt+Enter, GPU hitch) must not expire the twin
         * latches, since the tick that re-arms them never ran. A latch live at
         * the last merge ages at most 50 ms per merge, never beyond one fresh
         * arm. */
        static Uint32 s_LastMerge[INPUT_MAX_PADS];
        const Uint32 now = SDL_GetTicks();

        if (s_LastMerge[idx] != 0 && (Sint32) (now - s_LastMerge[idx]) > 50)
        {
            Uint32 *const latch[2] = { &s_PadRightCMute[idx], &s_PadSwapSticks[idx] };
            const Uint32 slip = now - s_LastMerge[idx] - 50;
            int k;

            for (k = 0; k < 2; k++)
            {
                if (*latch[k] != 0 && (Sint32) (*latch[k] - s_LastMerge[idx]) > 0)
                {
                    *latch[k] += slip;
                    if ((Sint32) (*latch[k] - now) > PORT_TWIN_LATCH_MS)
                    {
                        *latch[k] = now + PORT_TWIN_LATCH_MS;
                    }
                }
            }
        }
        s_LastMerge[idx] = now;
    }
    inputP1ClaimPoll(idx);   /* intro only, and it records rather than acts */
    if (idx == 0)
    {
        inputPadBindHarness();     /* GE_PADBIND, a no-op without it */
        inputPadLayoutHarness();   /* GE_PADLAYOUT, likewise */
    }

    if (inputPadCaptureBlanks(idx) || portInputAssignActive())
    {   /* While rebinding this pad, or while any player is being assigned a
         * device by a button press, it drives nothing, or the button being
         * captured would also confirm the menu row underneath. */
        inputPadBlankBegin(idx);
        inputPadBlankNeutral(idx);
        return;
    }
    {   /* ...and for a moment AFTER, because the button or trigger that was
         * just bound is still held and now means what it was bound to. A
         * trigger held through the capture would otherwise fire its new
         * action on every poll. */
        extern s32 portBindCooldownActive(void);

        if (portBindCooldownActive())
        {
            inputPadBlankBegin(idx);
            inputPadBlankNeutral(idx);
            return;
        }
    }
    if (inputPadBlankHeld(idx))
    {
        inputPadBlankNeutral(idx);
        return;
    }
    if (s_PadCapHold.slot == idx)
    {   /* After a rebind capture, nothing from this pad reaches the game until
         * the captured control reads up. Otherwise a held trigger becomes
         * Fire/menu-confirm, re-arms the same row, and its release gets bound
         * as a second binding. */
        SDL_Joystick *hj = (s_Joys[idx] != NULL) ? s_Joys[idx]
                         : (s_Pads[idx] != NULL) ? SDL_GameControllerGetJoystick(s_Pads[idx]) : NULL;

        if (hj != NULL && inputEntryDown(hj, idx, &s_PadCapHold.entry))
        {
            inputPadBlankNeutral(idx);
            return;
        }
        s_PadCapHold.slot = -1;
    }
    { /* hotplug hunt (see s_OptRawInput): a handle SDL considers gone while
       * we still hold it means a device left WITHOUT a REMOVED event
       * reaching us - the slot reads neutral forever. Say so once. */
        int dead = (s_Pads[idx] != NULL && !SDL_GameControllerGetAttached(s_Pads[idx]))
                || (s_Joys[idx] != NULL && !SDL_JoystickGetAttached(s_Joys[idx]));
        if (dead && !s_PadZombieLogged[idx])
        {
            s_PadZombieLogged[idx] = 1;
            portConLogf("ge: input: player %d handle DETACHED with no REMOVED event - slot is dead until a hotplug\n", idx + 1);
        }
        else if (!dead)
        {
            s_PadZombieLogged[idx] = 0;
        }
    }
    s_PadHasRight[idx] = 0; /* set again below by whichever path has a right stick */
    /* the twin-stick latches are time-based deadlines now - nothing to
     * decrement per poll (see PORT_TWIN_LATCH_MS) */

    { /* GE_MPDRIVE=1 (4-player soak harness): every pad holds run-forward plus
       * clockwise turn, connected or not; pair with GE_MP=temple GE_MPPLAYERS=4
       * for a hands-off MP repro. Real input layers on top for slots that have
       * controllers. */
        static int mpdrive = -1;
        static int spdrive = -1;
        static int s_SkipIntro = 0;
        if (mpdrive < 0)
        {
            const char *e = getenv("GE_MPDRIVE");
            mpdrive = (e != NULL) && (e[0] != '0');
            /* GE_DRIVE: scripted solo locomotion for hands-off runs
             * (screenshot sweeps, SP soaks). 1 = wander (forward + turn
             * bursts), 2 = pan in place. Slot 0 only, no buttons. */
            e = getenv("GE_DRIVE");
            spdrive = (e != NULL) ? atoi(e) : 0;
            e = getenv("GE_SKIPINTRO");
            s_SkipIntro = (e != NULL) && (e[0] != '0');
        }
        { /* GE_GOTO steering (lv.c portGotoTick) overrides slot-0 sticks */
            extern s32 g_PortGotoActive, g_PortGotoStickX, g_PortGotoStickY;
            if (idx == 0 && g_PortGotoActive)
            {
                pad->stick_x = (s8) g_PortGotoStickX;
                pad->stick_y = (s8) g_PortGotoStickY;
            }
        }
        if (s_SkipIntro && idx == 0)
        {   /* GE_SKIPINTRO=1: tap A through the mission intro cameras
             * (2-tick presses, one per ~2/3s, first 15s only - stops
             * before it could cycle weapons in gameplay) */
            static u32 sitick;
            u32 t = sitick++;
            if (t > 60u && t < 900u && (t % 40u) < 2u)
            {
                pad->button |= CONT_A;
            }
        }
        if (spdrive && idx == 0 && !mpdrive)
        {
            static u32 sptick;
            u32 phase = sptick++ % 300u;
            if (spdrive == 2)
            {
                pad->stick_x = 55;
            }
            else if (spdrive == 3)
            {   /* GE_DRIVE=3: stand still, hold fire 2s of every 3s */
                if (phase % 180u < 120u)
                {
                    pad->button |= CONT_G;
                }
            }
            else if (spdrive == 4)
            {   /* GE_DRIVE=4: stand still, then back straight up (facing kept) -
                 * GE_DRIVEBACK="from,len" in polls, default 600,240 */
                static s32 from = -1, len;
                if (from < 0)
                {
                    const char *b = getenv("GE_DRIVEBACK");
                    from = 600; len = 240;
                    if (b != NULL) sscanf(b, "%d,%d", &from, &len);
                }
                if (sptick > (u32) from && sptick <= (u32) (from + len))
                {
                    pad->stick_y = -70;
                }
            }
            else
            {
                pad->stick_y = 70;
                pad->stick_x = (phase < 40u) ? 35 : 0;
            }
        }
        if (mpdrive)
        {
            /* Wander: hold forward and let wall-sliding funnel players through
             * doorways, with brief per-pad turn bursts (random direction,
             * deterministic LCG) so players spread out and keep crossing
             * portals. Fire is held for the first 3s of each 5s cycle, and one
             * A tap and one B tap land per 10s cycle (weapon cycling,
             * reload/door churn). */
            static u32 tick[INPUT_MAX_PADS];
            static s8 turndir[INPUT_MAX_PADS];
            static u32 rng = 0x12345675u;
            u32 t = tick[idx]++ + (u32) idx * 137u;
            u32 phase = t % 300u;
            u32 phase10 = t % 600u;
            if (phase == 0u || turndir[idx] == 0)
            {                                           /* new cycle: 50/50 left/right */
                rng = rng * 1664525u + 1013904223u;
                turndir[idx] = ((rng >> 16) & 1u) ? 45 : -45;
            }
            pad->stick_y = 70;                          /* run forward */
            pad->stick_x = (phase < 70u) ? turndir[idx] : 0; /* turn burst */
            if (phase < 180u)
            {
                pad->button |= CONT_G;                  /* Z: hold fire 3s of every 5s */
            }
            if (phase10 < 3u)
            {
                pad->button |= CONT_A;                  /* A tap, once per 10s */
            }
            else if (phase10 >= 300u && phase10 < 303u)
            {
                pad->button |= CONT_B;                  /* B tap, 5s after the A tap */
            }
        }
    }

    if (s_PadRaphnet[idx] != 0)
    {
        /* raphnet RAW: the adapter returns the real N64 poll response (CONT_*
         * button word, signed N64-unit stick), so there is nothing to
         * translate. Calibrated mode may still remap the octagon on top (values
         * x256, same convention as padN_calib). */
        u16 rbtn;
        s8 rx8, ry8;

        if (portRaphnetPoll(s_PadRaphnet[idx] - 1, &rbtn, &rx8, &ry8) == 1)
        {
            pad->button |= rbtn;
            if (s_PadStickMode[idx] == STICKMODE_CALIBRATED && s_PadCalibSet[idx])
            {
                inputCalibApply(idx, (s32) rx8 * 256, (s32) ry8 * 256, pad);
            }
            else
            {
                if (rx8 != 0) pad->stick_x = rx8;
                if (ry8 != 0) pad->stick_y = ry8;
            }
        }
        return;
    }

    if ((s_PadMapCustom[idx] || s_PadMapSeeded[idx])
        && (s_Joys[idx] != NULL || s_Pads[idx] != NULL))
    {
        /* Custom per-slot map (midas.ini padN_* from the launcher), or the
         * built-in layout seeded from the controller's SDL bindings
         * (inputDefaultTokens): evaluated raw through the joystick, and a
         * mapped pad with a custom map reads the same way. */
        SDL_Joystick *joy = (s_Joys[idx] != NULL) ? s_Joys[idx] : SDL_GameControllerGetJoystick(s_Pads[idx]);
        u16 jb = 0;
        s32 v;
        int i;
        s32 clx = 0, cly = 0;  /* the Look stick, N64 units */
        int chaslook = 0;
        s32 dmove[2] = { 0, 0 }; /* digital move_* / look_* rows, +/-85 or 0 */
        s32 dlook[2] = { 0, 0 };

        { /* Twin stick: the Look stick is padN_look_x/y (default right stick).
           * An unbound Look stick means "not twin-stick capable", so the N64
           * Controller layout (C stick bound to C buttons) stays a digital-C
           * 1.x pad. */
            PadMapEntry *ex = &s_PadMap[idx][PM_LOOK_X];
            PadMapEntry *ey = &s_PadMap[idx][PM_LOOK_Y];
            /* each axis on its own: unbinding Look Up/Down clears look_y
             * only, and the pair test then lost Turn as well */
            /* ...and only an axis the device HAS: an old launcher wrote the
             * fallback's a2+/a3- for a 2-axis adapter, which then counted as
             * twin-stick and was promoted to 2.2 with nothing to turn */
            if (ex->kind == 'a' && inputMapAxis(idx, ex->idx) < SDL_JoystickNumAxes(joy))
            {
                s32 ax = SDL_JoystickGetAxis(joy, inputMapAxis(idx, ex->idx)) * (ex->dir < 0 ? -1 : 1);
                /* captured direction = positive N64: look_x RIGHT, look_y UP */
                clx = inputAxisScaleLook(ax) * 85 / 32768;
                chaslook = 1;
            }
            if (ey->kind == 'a' && inputMapAxis(idx, ey->idx) < SDL_JoystickNumAxes(joy))
            {
                s32 ay = SDL_JoystickGetAxis(joy, inputMapAxis(idx, ey->idx)) * (ey->dir < 0 ? -1 : 1);
                cly = inputAxisScaleLook(ay) * 85 / 32768;
                chaslook = 1;
            }
        }

        for (i = 0; i < PM_STICK_X; i++)
        {
            PadMapEntry *e = &s_PadMap[idx][i];
            if (inputLatchArmed(s_PadRightCMute[idx]) && i >= PM_CUP && i <= PM_CRIGHT)
            {   /* the game is reading the right stick as analog this tick:
                 * no axis entry presses C, primary OR alternate (the
                 * raw seed puts the C stick on the C rows as alternates) */
                int a2;
                int down = (e->kind != 'a') && inputEntryDown(joy, idx, e);

                for (a2 = 0; !down && a2 < PM_MAX_ALT; a2++)
                {
                    down = (s_PadMapAlt[idx][i][a2].kind != 'a') && inputEntryDown(joy, idx, &s_PadMapAlt[idx][i][a2]);
                }
                if (down)
                {
                    jb |= sPadMapMask[i];
                }
                continue;
            }
            if (inputControlDown(joy, idx, i))
            {
                jb |= sPadMapMask[i];
            }
        }
        jb &= (u16) ~inputPortModeN64Drop(idx);   /* port mode: Port binds only */
        pad->button |= jb;

        for (i = PM_MOVE_UP; i <= PM_PORT_LAST; i++)
        { /* digital movement + Port suite actions bound on this controller
           * (primary token or any alternate) */
            if (!inputControlDown(joy, idx, i))
            {
                continue;
            }
            switch (i)
            {
            case PM_MOVE_UP:    dmove[1] = 85;  break;
            case PM_MOVE_DOWN:  dmove[1] = -85; break;
            case PM_MOVE_LEFT:  dmove[0] = -85; break;
            case PM_MOVE_RIGHT: dmove[0] = 85;  break;
            case PM_LOOK_UP:    dlook[1] = 85;  break;
            case PM_LOOK_DOWN:  dlook[1] = -85; break;
            case PM_LOOK_LEFT:  dlook[0] = -85; break;
            case PM_LOOK_RIGHT: dlook[0] = 85;  break;
            default:
                if (i >= PM_PORT_FIRST)
                {
                    portJoyPortPadDown(idx, i - PM_PORT_FIRST); /* -> joy_port.c, this slot */
                }
                break;
            }
        }
        /* a digital Look press overrides the Look stick on that axis */
        if (dlook[0] != 0 || dlook[1] != 0)
        {
            if (dlook[0] != 0) clx = dlook[0];
            if (dlook[1] != 0) cly = dlook[1];
            chaslook = 1;
        }

        if (s_PadMap[idx][PM_STICK_X].kind == 'a'
            && s_PadMap[idx][PM_STICK_Y].kind == 'a')
        {
            if (s_PadStickMode[idx] == STICKMODE_CALIBRATED && s_PadCalibSet[idx])
            {
                /* calibrated: raw axis pair through the measured octagon -
                 * orientation lives in the measurements, tokens pick axes */
                inputCalibApply(idx,
                                SDL_JoystickGetAxis(joy, inputMapAxis(idx, s_PadMap[idx][PM_STICK_X].idx)),
                                SDL_JoystickGetAxis(joy, inputMapAxis(idx, s_PadMap[idx][PM_STICK_Y].idx)),
                                pad);
                if (chaslook)
                {
                    inputTwinCommit(idx, pad, pad->stick_x, pad->stick_y, clx, cly, 1);
                }
                return;
            }
            if (s_PadStickMode[idx] == STICKMODE_RAW)
            {
                /* raw 1:1 (raphnet-class): the adapter reports true N64
                 * units across the SDL range - /256 recovers them exactly;
                 * no deadzone, no reshaping, 45-degree diagonals kept */
                s32 rvx = SDL_JoystickGetAxis(joy, inputMapAxis(idx, s_PadMap[idx][PM_STICK_X].idx));
                s32 rvy = SDL_JoystickGetAxis(joy, inputMapAxis(idx, s_PadMap[idx][PM_STICK_Y].idx));
                if (s_PadMap[idx][PM_STICK_X].dir < 0) rvx = -rvx;
                if (s_PadMap[idx][PM_STICK_Y].dir < 0) rvy = -rvy;
                rvx = inputRawUnits(rvx); /* clamped: -32768 would wrap the s8 */
                rvy = inputRawUnits(rvy);
                if (rvx != 0) pad->stick_x = (s8) rvx;
                if (rvy != 0) pad->stick_y = (s8) rvy;
                if (chaslook)
                {
                    inputTwinCommit(idx, pad, rvx, rvy, clx, cly, 1);
                }
                return;
            }
        }

        {
            s32 mv[2] = { 0, 0 }; /* the Move stick, N64 units */
            for (i = PM_STICK_X; i <= PM_STICK_Y; i++)
            {
                PadMapEntry *e = &s_PadMap[idx][i];
                if (e->kind != 'a')
                {
                    continue;
                }
                v = SDL_JoystickGetAxis(joy, inputMapAxis(idx, e->idx));
                if (e->dir < 0)
                {
                    v = -v; /* captured direction = positive N64 (up/right) */
                }
                mv[i - PM_STICK_X] = inputAxisScale(v) * 85 / 32768;
            }
            inputCircleToOctagon(&mv[0], &mv[1]);   /* a round stick's full diagonal reaches the N64 corner */
            /* a digital Move press overrides the Move stick on that axis
             * (raw / calibrated stick modes above ignore these rows) */
            if (dmove[0] != 0) mv[0] = dmove[0];
            if (dmove[1] != 0) mv[1] = dmove[1];
            s_PadDigMove[idx][0] = (s8) ((dmove[0] > 0) - (dmove[0] < 0));
            s_PadDigMove[idx][1] = (s8) ((dmove[1] > 0) - (dmove[1] < 0));
            if (chaslook)
            {
                inputTwinCommit(idx, pad, mv[0], mv[1], clx, cly, 0);
            }
            else
            {
                if (mv[0] != 0) pad->stick_x = (s8) mv[0];
                if (mv[1] != 0) pad->stick_y = (s8) mv[1];
            }
        }
        return;
    }

    if (s_Pads[idx] == NULL)
    {
        /* Raw (unmapped DInput) fallback, fixed layout: axes 0/1 = stick, axes
         * 2/3 -> C buttons, hat 0 = D-pad, buttons 0..9 = A B Z Start L R C-up
         * C-down C-left C-right. Keep in step with sPadRawFallback, which
         * spells it out for the readback and seed. */
        SDL_Joystick *joy = s_Joys[idx];
        u16 jb = 0;
        s32 jlx, jly;
        s32 jlookx = 0, jlooky = 0; /* twin stick: axes 2/3 as the Look stick */
        int jhaslook = 0;
        u8 hat;
        int jsw; /* swap_sticks: axes 2/3 are the Move stick, 0/1 the Look stick */

        if (joy == NULL)
        {
            return;
        }
        jsw = (s_PadSwapOpt[idx] && SDL_JoystickNumAxes(joy) >= 4);

        if (SDL_JoystickGetButton(joy, 0)) jb |= CONT_A;
        if (SDL_JoystickGetButton(joy, 1)) jb |= CONT_B;
        if (SDL_JoystickGetButton(joy, 2)) jb |= CONT_G;     /* Z: fire */
        if (SDL_JoystickGetButton(joy, 3)) jb |= CONT_START;
        if (SDL_JoystickGetButton(joy, 4)) jb |= CONT_L;
        if (SDL_JoystickGetButton(joy, 5)) jb |= CONT_R;
        if (SDL_JoystickGetButton(joy, 6)) jb |= CONT_E;     /* C up */
        if (SDL_JoystickGetButton(joy, 7)) jb |= CONT_D;     /* C down */
        if (SDL_JoystickGetButton(joy, 8)) jb |= CONT_C;     /* C left */
        if (SDL_JoystickGetButton(joy, 9)) jb |= CONT_F;     /* C right */

        hat = SDL_JoystickGetHat(joy, 0);
        if (hat & SDL_HAT_UP)    jb |= CONT_UP;
        if (hat & SDL_HAT_DOWN)  jb |= CONT_DOWN;
        if (hat & SDL_HAT_LEFT)  jb |= CONT_LEFT;
        if (hat & SDL_HAT_RIGHT) jb |= CONT_RIGHT;

        if (SDL_JoystickNumAxes(joy) >= 4)
        {
            s32 jrx = SDL_JoystickGetAxis(joy, jsw ? 0 : 2);
            s32 jry = SDL_JoystickGetAxis(joy, jsw ? 1 : 3);
            /* twin stick: axes 2/3 double as the analog Look stick */
            jlookx = inputAxisScaleLook(jrx) * 85 / 32768;
            jlooky = -inputAxisScaleLook(jry) * 85 / 32768;
            jhaslook = 1;
            if (!inputLatchArmed(s_PadRightCMute[idx]))
            {
                if (jrx < -0x4000) jb |= CONT_C;
                if (jrx > +0x4000) jb |= CONT_F;
                if (jry < -0x4000) jb |= CONT_E;
                if (jry > +0x4000) jb |= CONT_D;
            }
        }

        pad->button |= jb;

        if (s_PadStickMode[idx] == STICKMODE_CALIBRATED && s_PadCalibSet[idx])
        {
            inputCalibApply(idx, SDL_JoystickGetAxis(joy, jsw ? 2 : 0),
                            SDL_JoystickGetAxis(joy, jsw ? 3 : 1), pad);
            return;
        }
        if (s_PadStickMode[idx] == STICKMODE_RAW)
        {
            s32 rvx = inputRawUnits(SDL_JoystickGetAxis(joy, jsw ? 2 : 0));
            s32 rvy = inputRawUnits(-(s32) SDL_JoystickGetAxis(joy, jsw ? 3 : 1));
            if (rvx != 0) pad->stick_x = (s8) rvx;
            if (rvy != 0) pad->stick_y = (s8) rvy;
            return;
        }

        jlx = inputAxisScale(SDL_JoystickGetAxis(joy, jsw ? 2 : 0));
        jly = inputAxisScale(SDL_JoystickGetAxis(joy, jsw ? 3 : 1));
        {   /* round stick -> N64 octagon, as the seeded-map path does: the
             * same push walked diagonals ~8% slower until any rebind */
            s32 mx = jlx * 85 / 32768;
            s32 my = -jly * 85 / 32768;

            inputCircleToOctagon(&mx, &my);
            if (jhaslook)
            {
                inputTwinCommit(idx, pad, mx, my, jlookx, jlooky, 0);
            }
            else
            {
                if (mx != 0)
                {
                    pad->stick_x = (s8) mx;
                }
                if (my != 0)
                {
                    pad->stick_y = (s8) my;
                }
            }
        }
        return;
    }
    ctrl = s_Pads[idx];

    /* buttons: fixed N64-flavored map (see file header) */
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_A))             buttons |= CONT_A;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_X))             buttons |= CONT_A;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_B))             buttons |= CONT_B;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_Y))             buttons |= CONT_B;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_START))         buttons |= CONT_START;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  buttons |= CONT_L;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons |= CONT_R;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_UP))       buttons |= CONT_UP;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_DOWN))     buttons |= CONT_DOWN;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_LEFT))     buttons |= CONT_LEFT;
    if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    buttons |= CONT_RIGHT;

    if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > s_OptTrigThreshold) buttons |= CONT_G; /* Z: fire */
    if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > s_OptTrigThreshold) buttons |= CONT_R; /* aim */

    /* swap_sticks: the Move stick becomes the right one and the Look stick -
     * with the C emulation that rides on it - the left one */
    mvx = s_PadSwapOpt[idx] ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX;
    mvy = s_PadSwapOpt[idx] ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY;
    lkx = s_PadSwapOpt[idx] ? SDL_CONTROLLER_AXIS_LEFTX  : SDL_CONTROLLER_AXIS_RIGHTX;
    lky = s_PadSwapOpt[idx] ? SDL_CONTROLLER_AXIS_LEFTY  : SDL_CONTROLLER_AXIS_RIGHTY;

    lx = inputAxisScale(SDL_GameControllerGetAxis(ctrl, mvx));
    ly = inputAxisScale(SDL_GameControllerGetAxis(ctrl, mvy));
    rx = SDL_GameControllerGetAxis(ctrl, lkx);
    ry = SDL_GameControllerGetAxis(ctrl, lky);

    if (!inputLatchArmed(s_PadRightCMute[idx]))
    {
        if (rx < -0x4000) buttons |= CONT_C;  /* C left */
        if (rx > +0x4000) buttons |= CONT_F;  /* C right */
        if (ry < -0x4000) buttons |= CONT_E;  /* C up */
        if (ry > +0x4000) buttons |= CONT_D;  /* C down */
    }

    pad->button |= buttons;

    if (s_PadStickMode[idx] == STICKMODE_RAW)
    {
        /* raw 1:1 (raphnet-class adapter that happens to be mapped) */
        s32 rvx = inputRawUnits(SDL_GameControllerGetAxis(ctrl, mvx));
        s32 rvy = inputRawUnits(-(s32) SDL_GameControllerGetAxis(ctrl, mvy));
        if (rvx != 0) pad->stick_x = (s8) rvx;
        if (rvy != 0) pad->stick_y = (s8) rvy;
        inputTwinCommit(idx, pad, rvx, rvy,
                        inputAxisScaleLook(rx) * 85 / 32768, -inputAxisScaleLook(ry) * 85 / 32768, 1);
        return;
    }

    /* Scale 32767 to the N64's +/-85 cardinal reach (SDL Y+ is down);
     * portInputStickGate bounds diagonals afterwards. Move is the left stick,
     * Look the right stick; the style swap happens in inputTwinCommit. */
    {
        s32 mx = lx * 85 / 32768;
        s32 my = -ly * 85 / 32768;

        inputCircleToOctagon(&mx, &my);   /* a round stick's full diagonal reaches the N64 corner */
        inputTwinCommit(idx, pad, mx, my,
                        inputAxisScaleLook(rx) * 85 / 32768, -inputAxisScaleLook(ry) * 85 / 32768, 0);
    }
}

/**
 * GADGET SWITCH chord (see s_PadGadgetSwitch): called for every slot right
 * after portInputPadMerge and before the joy_port commit, on the merged pad
 * word, so it covers every backend and the next_inventory press latches on this
 * poll.
 */
void portInputPadGadgetChord(s32 idx, OSContPad *pad)
{
    u16 b, prev;

    if (idx < 0 || idx >= INPUT_MAX_PADS || pad == NULL)
    {
        return;
    }
    b = pad->button;
    prev = s_ChordPrevBtn[idx];
    s_ChordPrevBtn[idx] = b;
    if (s_PadLrClassic[idx] && portInputPadIsN64(idx))
    {   /* L+R held half a second = classic_toggle, once per hold (see
         * s_PadLrClassic); the buttons themselves still reach the game */
        if ((b & CONT_L) && (b & CONT_R))
        {
            if (s_PadLrSince[idx] == 0)
            {
                s_PadLrSince[idx] = SDL_GetTicks() | 1u;
                s_PadLrFired[idx] = 0;
            }
            else if (!s_PadLrFired[idx] && (Sint32) (SDL_GetTicks() - s_PadLrSince[idx]) >= PADCHORD_LR_MS)
            {
                s_PadLrFired[idx] = 1;
                portJoyPortPadDown(idx, PM_PORT_CLASSIC_TOGGLE);
            }
        }
        else
        {
            s_PadLrSince[idx] = 0;
        }
    }
    /* the same gate the menus use to show the row: in Port mode it is hidden,
     * so it must not act either - B+Z ate the shot with no way to turn it
     * off in game */
    if (!s_PadGadgetSwitch[idx] || !portInputPadIsN64(idx))
    {
        s_ChordZMasked[idx] = 0;
        return;
    }
    if (s_ChordZMasked[idx])
    {
        /* the Z that switched stays swallowed until it comes back up */
        if (b & CONT_G)
        {
            pad->button &= ~CONT_G;
        }
        else
        {
            s_ChordZMasked[idx] = 0;
        }
        return;
    }
    if ((b & CONT_B) && (b & CONT_G) && !(prev & CONT_G))
    {
        /* B held, Z just landed: next gadget, no shot */
        s_ChordZMasked[idx] = 1;
        pad->button &= ~CONT_G;
        portJoyPortPadDown(idx, PM_PORT_NEXT_INV);
    }
}

/* ---- twin stick (joy_port.c portJoyTwinStick*) ------------------------- */

/** 1 = pad idx has an analog right stick and twin_stick=1 (raphnet / absent pads: 0). */
s32 portInputPadTwinStickCapable(s32 idx)
{
    if (idx < 0 || idx >= INPUT_MAX_PADS)
    {
        return 0;
    }
    /* A pad with a right stick is always twin-stick capable. What that means is
     * the mode's call: the Look stick in port mode, or the control-style row
     * deciding whether 1.x is promoted to 2.2 in n64 mode. */
    return s_PadHasRight[idx];
}

/**
 * The right stick of pad idx in N64 units (+/-85, up/right positive) as the 2.x
 * "second controller" stick. muteC = 1 while the game consumes it as analog; it
 * holds right-stick C emulation off and expires on its own once the game stops
 * asking.
 */
s32 portInputPadTwinStick(s32 idx, s8 *x, s8 *y, s32 muteC, s32 lookOnPad1)
{
    if (!portInputPadTwinStickCapable(idx))
    {
        return 0;
    }
    *x = s_PadRightX[idx];
    *y = s_PadRightY[idx];
    if (muteC)
    {
        s_PadRightCMute[idx] = SDL_GetTicks() + PORT_TWIN_LATCH_MS; /* time-based: poll budgets broke at uncapped tick/fps */
        if (lookOnPad1)
        {
            /* 2.2 / 2.4: pad 1 is the LOOK pad - feed its stick from the Look
             * stick and hand the Move stick back as the second pad (next
             * merge; same self-expiring deadline, so menus keep Move on pad 1) */
            s_PadSwapSticks[idx] = SDL_GetTicks() + PORT_TWIN_LATCH_MS;
        }
    }
    return 1;
}

/**
 * Console open (osContGetReadData skips portInputPadMerge): zero the caches the
 * game reads beside the pad (second-pad/Look stick, digital Move) so the last
 * deflection doesn't keep steering Bond. The swap/C-mute latches are held as
 * they stood.
 */
void portInputPadNeutral(s32 idx)
{
    if (idx < 0 || idx >= INPUT_MAX_PADS)
    {
        return;
    }
    /* the console is a blank like the others: a button held across the
     * close must read let go before it counts, or it lands as a fresh
     * press - player 2's held START opened their pause menu */
    inputPadBlankBegin(idx);
    s_PadRightX[idx] = 0;
    s_PadRightY[idx] = 0;
    s_PadDigMove[idx][0] = 0;
    s_PadDigMove[idx][1] = 0;
    /* Hold the twin latches as they stood when the prompt opened, since nothing
     * re-arms them while the console is up and an expired swap would put Move
     * on the Look pad. One that had already run out stays out. */
    if (inputLatchArmed(s_PadRightCMute[idx])) s_PadRightCMute[idx] = SDL_GetTicks() + PORT_TWIN_LATCH_MS;
    if (inputLatchArmed(s_PadSwapSticks[idx])) s_PadSwapSticks[idx] = SDL_GetTicks() + PORT_TWIN_LATCH_MS;
}

/**
 * N64 stick gate: a real N64 stick tops out at 85 on cardinals and 65 per axis
 * on diagonals, while PC sources are square-gate (keyboard corners hit 85,85).
 * Clamps each axis to +/-85, then scales the vector so |x|+|y| <= 130 (the
 * diagonal vertex 65,65). Applied to the final merged pad in osContGetReadData;
 * demo playback bypasses it because recorded N64 data is already gate-shaped.
 */
void portInputStickGate(OSContPad *pad, s32 slot)
{
    s32 x = pad->stick_x;

    /* raw and calibrated slots bypass the gate: raw IS the real stick
     * (clipping it would defeat the point), calibrated output is already
     * inside the ideal octagon by construction. */
    if (slot >= 0 && slot < INPUT_MAX_PADS
        && s_PadStickMode[slot] != STICKMODE_SCALED)
    {
        return;
    }

    s32 y = pad->stick_y;
    s32 ax = (x < 0) ? -x : x;
    s32 ay = (y < 0) ? -y : y;
    s32 sum;

    if (ax > 85)
    {
        x = (x < 0) ? -85 : 85;
        ax = 85;
    }
    if (ay > 85)
    {
        y = (y < 0) ? -85 : 85;
        ay = 85;
    }

    sum = ax + ay;
    if (sum > 130)
    {
        x = x * 130 / sum;
        y = y * 130 / sum;
    }

    pad->stick_x = (s8) x;
    pad->stick_y = (s8) y;
}

/* ---- mouse -------------------------------------------------------------- */

/**
 * Per-poll mouse upkeep, called from videoGetPad (once per joy poll):
 * lock on click inside the window, unlock on Escape, sample the relative
 * motion. Overwrite - NOT accumulate - so time spent in menus/pause can't
 * bank a huge first-frame camera jump.
 */
/* Capture/release in one place. The OS arrow is never shown over the window
 * while the mouse feature is on (the game draws its own cursor); this only
 * toggles relative mode. portInputMouseUpdate re-asserts both every poll, so
 * anything that re-shows the arrow is corrected. */
static void portMouseApplyCapture(int on)
{
    if (on)
    {
        s_WarpPending = 0;   /* a warp issued as the lock flipped must not be judged later */
    }
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(s_OptMouse ? SDL_DISABLE : SDL_ENABLE);
}

void portInputMouseUpdate(void)
{
    /* read ONCE per poll: the latch is one-shot, so testing it separately in
     * the two lock branches below would swallow every other press */
    const int grabtoggle = inputGrabTogglePressed();
    int dx = 0, dy = 0;
    int ax = 0, ay = 0;

    s_MouseButtons = SDL_GetMouseState(&ax, &ay);
    if (ax != s_PollX || ay != s_PollY || !s_PollValid)
    {
        /* previous DISTINCT position for the park's direction test (s_PollX) */
        s_PollPrevX = s_PollValid ? s_PollX : ax;
        s_PollPrevY = s_PollValid ? s_PollY : ay;
        s_PollX = ax;
        s_PollY = ay;
        s_PollValid = 1;
    }

    { /* per-poll re-assert: the arrow stays hidden over the window for the
       * life of the mouse feature (and returns if it is turned off) */
        int wantshow = s_OptMouse ? SDL_DISABLE : SDL_ENABLE;
        if (SDL_ShowCursor(SDL_QUERY) != wantshow)
        {
            SDL_ShowCursor(wantshow);
        }
    }

    if (!s_OptMouse)
    {
        if (s_MouseLocked)
        {
            s_MouseLocked = 0;
            portMouseApplyCapture(0);
        }
        s_MouseDX = s_MouseDY = 0;
        s_MouseAccDX = s_MouseAccDY = 0;
        s_MouseMoved = 0;
        s_MouseMovedAcc = s_MouseMovedTick = 0;
        return;
    }

    {
        /* NOT SDL_GL_GetCurrentWindow(): that answers for THIS thread's GL
         * context, which is NULL in render-thread mode (the GL context
         * lives on the render thread) - focus read as 0 forever and the
         * mouse never captured. gfx_sdl_get_window() is the window itself. */
        extern SDL_Window *gfx_sdl_get_window(void);
        int focus = (SDL_GetWindowFlags(gfx_sdl_get_window()) & SDL_WINDOW_INPUT_FOCUS) != 0;
        int click = (s_MouseButtons & SDL_BUTTON_LMASK) != 0;

        if (s_MouseLocked && !focus)
        {
            /* alt-tab / click on another window: hand the pointer back at
             * once (see s_PollX). Both lock modes; the re-capture branches
             * below are already gated on focus. */
            s_MouseLocked = 0;
            portMouseApplyCapture(0);
            portConLogf("ge: input: mouse released (window lost focus)\n");
        }

        if (s_OptSmartLock)
        {
            /* SMART MOUSE LOCK: hold the pointer exactly while gameplay owns it
             * (bondview2.c portGameplayHasPointer, the stick's own permission
             * predicate) and hand it back for menus, the watch, pause and
             * cutscenes. Escape frees the pointer until the player clicks back
             * in, even across menus, so auto-lock doesn't retake it on the next
             * poll. */
            extern s32 portGameplayHasPointer(void); /* bondview2.c */
            int want = portGameplayHasPointer();

            /* F1, not Escape (spec section 6): Escape is the watch key now. */
            if (s_MouseLocked && grabtoggle)
            {
                s_MouseLocked = 0;
                s_MouseFreed = 1;
                portMouseApplyCapture(0);
                portConLogf("ge: input: mouse released (F1) - F1 or a click takes it back\n");
            }
            else if (s_MouseFreed && focus && (click || grabtoggle))
            {
                s_MouseFreed = 0;
            }

            /* transitions only - if this pair ever spams, the predicate is
             * flickering and THAT is the bug, not the logging */
            if (s_MouseLocked && !want)
            {
                s_MouseLocked = 0;
                portMouseApplyCapture(0);
                portConLogf("ge: input: mouse released (smart lock: gameplay does not own the pointer)\n");
            }
            else if (!s_MouseLocked && want && focus && !s_MouseFreed)
            {
                s_MouseLocked = 1;
                portMouseApplyCapture(1);
                SDL_GetRelativeMouseState(NULL, NULL); /* drop the pre-lock jump */
                portConLogf("ge: input: mouse captured (smart lock: gameplay)\n");
            }
        }
        else if (!s_MouseLocked && ((click && !s_ClickIsTouch) || grabtoggle) && focus)
        {   /* not on a touch tap: captured, every later tap pressed what
             * the crosshair was already on, never the tapped item */
            s_MouseLocked = 1;
            portMouseApplyCapture(1);
            SDL_GetRelativeMouseState(NULL, NULL); /* drop the pre-lock jump */
            if (grabtoggle)
            {
                portConLogf("ge: input: mouse captured (F1)\n");
            }
        }
        else if (s_MouseLocked && grabtoggle)
        {
            s_MouseLocked = 0;
            portMouseApplyCapture(0);
            portConLogf("ge: input: mouse released (F1)\n");
        }
    }

    if (s_MouseLocked)
    {
        int ww = 0, wh = 0;
        /* re-assert relative mode per poll while gameplay owns the pointer
         * (the cursor-visibility re-assert above covers the arrow) */
        if (!SDL_GetRelativeMouseMode())
        {
            portMouseApplyCapture(1);
        }
        SDL_GetRelativeMouseState(&dx, &dy);
        /* keep the virtual cursor tracking while captured, so menus opened
         * from gameplay (watch) can still be mouse-driven */
        {
            extern SDL_Window *gfx_sdl_get_window(void); /* see focus note above */
            SDL_GetWindowSize(gfx_sdl_get_window(), &ww, &wh);
        }
        s_MouseVX += dx;
        s_MouseVY += dy;
        if (s_MouseVX < 0) s_MouseVX = 0;
        if (s_MouseVY < 0) s_MouseVY = 0;
        if (ww > 0 && s_MouseVX >= ww) s_MouseVX = ww - 1;
        if (wh > 0 && s_MouseVY >= wh) s_MouseVY = wh - 1;
        s_MouseMoved = (dx != 0 || dy != 0);
    }
    else
    {
        if (s_WarpPending)
        {
            s_WarpPending = 0;
            if (!s_FreeDelta && ax == s_WarpFromX && ay == s_WarpFromY
                && (s_WarpToX != s_WarpFromX || s_WarpToY != s_WarpFromY))
            {
                s_FreeDelta = 1;
                portConLogf("ge: input: pointer warp ignored by the platform - menu cursor now tracks mouse deltas\n");
            }
            /* the poll after a warp carries the warp's own jump (or, when
             * ignored, none) - never a hand movement to apply */
            s_FreeLastX = ax;
            s_FreeLastY = ay;
            s_FreeLastValid = 1;
            s_MouseMoved = 0;
        }
        else if (s_FreeDelta)
        {
            int fdx = s_FreeLastValid ? ax - s_FreeLastX : 0;
            int fdy = s_FreeLastValid ? ay - s_FreeLastY : 0;
            int ww = 0, wh = 0;
            extern SDL_Window *gfx_sdl_get_window(void);

            s_FreeLastX = ax;
            s_FreeLastY = ay;
            s_FreeLastValid = 1;
            SDL_GetWindowSize(gfx_sdl_get_window(), &ww, &wh);
            s_MouseVX += fdx;
            s_MouseVY += fdy;
            if (s_MouseVX < 0) s_MouseVX = 0;
            if (s_MouseVY < 0) s_MouseVY = 0;
            if (ww > 0 && s_MouseVX >= ww) s_MouseVX = ww - 1;
            if (wh > 0 && s_MouseVY >= wh) s_MouseVY = wh - 1;
            s_MouseMoved = (fdx != 0 || fdy != 0);
        }
        else
        {
            s_MouseMoved = (ax != s_MouseVX || ay != s_MouseVY);
            s_MouseVX = ax;
            s_MouseVY = ay;
        }
    }
    s_MouseAccDX += dx; /* consumed (and zeroed) once per sim tick, see above */
    s_MouseAccDY += dy;
    s_MouseMovedAcc |= s_MouseMoved;

    { /* GE_FAKEMOUSE=dx,dy,after[,until]: inject a constant relative motion of
       * (dx,dy) counts per poll from poll `after` (to `until`), as if captured,
       * so a headless run can exercise the mouse-look gates. Diagnostic only;
       * never set in midas.ini. */
        static long fdx, fdy, fafter = -1, funtil;
        static u32 fpolls;
        if (fafter == -1)
        {
            const char *e = getenv("GE_FAKEMOUSE");
            fafter = 0;
            funtil = 0x7FFFFFFF;
            if (e != NULL && sscanf(e, "%ld,%ld,%ld,%ld", &fdx, &fdy, &fafter, &funtil) < 3)
            {
                fafter = 0;
            }
        }
        fpolls++;
        if (fafter > 0 && fpolls >= (u32) fafter && fpolls <= (u32) funtil)
        {
            s_MouseLocked = 1;
            /* SET (not add): the per-tick value stays "fdx per tick" at any
             * poll:tick ratio, keeping the headless recipes' rates stable */
            s_MouseAccDX = (int) fdx;
            s_MouseAccDY = (int) fdy;
        }
    }
}

/**
 * Once per SIM TICK (joy.c joyConsumeSamplesWrapper): hand the sim the
 * motion of every poll since its last tick, then start the next window.
 * Every reader (portInputMouseLookDelta / ...Deg / the aim path) keeps
 * seeing one stable s_MouseDX/DY for the whole tick.
 */
void portInputMouseTickConsume(void)
{
    s_MouseDX = s_MouseAccDX;
    s_MouseDY = s_MouseAccDY;
    s_MouseAccDX = s_MouseAccDY = 0;
    s_MouseMovedTick = s_MouseMovedAcc;
    s_MouseMovedAcc = 0;
    { /* GE_INPUTREC / GE_INPUTPLAY key every record to the sim tick (inputrec.c) */
        extern void portInputRecTick(void);
        portInputRecTick();
    }
}

/**
 * 1 = some poll since the last sim tick saw the pointer move. At uncapped tick
 * rates the pump polls many times per tick, so the per-poll flag was usually 0
 * on the last poll and the menu cursor lagged; every reader within one tick now
 * agrees on this.
 */
s32 portInputMouseMovedThisTick(void)
{
    return s_MouseMovedTick;
}

/** Virtual absolute position (window px) + whether it moved this poll. */
void portInputMouseWindowPos(s32 *x, s32 *y, s32 *moved)
{
    if (x) *x = s_MouseVX;
    if (y) *y = s_MouseVY;
    if (moved) *moved = s_MouseMoved;
}

/**
 * Re-seat the mouse at a window position after the menu warps its cursor.
 * Captured: just move the virtual cursor. Free: also warp the real pointer,
 * since the virtual cursor is re-read from the OS pointer every poll.
 */
static void mouseWarpFree(s32 x, s32 y, s32 parkaxes)
{
    extern SDL_Window *gfx_sdl_get_window(void); /* see focus note above */
    SDL_Window *w = gfx_sdl_get_window();
    Uint32 fl;
    int ox = 0, oy = 0;

    if (w == NULL)
    {
        return;
    }
    fl = SDL_GetWindowFlags(w);
    if (!(fl & SDL_WINDOW_INPUT_FOCUS) || !(fl & SDL_WINDOW_MOUSE_FOCUS))
    {
        /* another app's pointer, or ours but off the window: never yank
         * it back (alt-tab, second monitor) - see s_PollX */
        return;
    }
    SDL_GetMouseState(&ox, &oy);
    if (parkaxes != 0)
    {
        /* clamp park: only on a move back toward the band along a clamped
         * axis; outward travel and rest leave the pointer where it is */
        int mx = s_PollValid ? s_PollX - s_PollPrevX : 0;
        int my = s_PollValid ? s_PollY - s_PollPrevY : 0;
        int inward = 0;

        if ((parkaxes & 1) && x != ox && mx != 0 && ((x > ox) == (mx > 0))) inward = 1;
        if ((parkaxes & 2) && y != oy && my != 0 && ((y > oy) == (my > 0))) inward = 1;
        if (!inward)
        {
            return;
        }
    }
    s_WarpFromX = ox;
    s_WarpFromY = oy;
    s_WarpToX = x;
    s_WarpToY = y;
    s_WarpPending = 1;   /* judged on the next poll, see s_FreeDelta */
    SDL_WarpMouseInWindow(w, x, y);
}

void portInputMouseWarp(s32 x, s32 y)
{
    s_MouseVX = x;
    s_MouseVY = y;
    if (!s_MouseLocked)
    {
        mouseWarpFree(x, y, 0);
    }
    s_MouseMoved = 0;
    s_MouseMovedAcc = s_MouseMovedTick = 0;   /* the warp swallows this tick's motion, as before */
}

/**
 * Clamp PARK: portInputMouseWarp for a cursor the menu clamped to its box.
 * axes bit 1 = x was clamped, bit 2 = y was clamped. The virtual cursor
 * always takes the clamp; the OS pointer is only pulled to it on a move
 * back toward the box along a clamped axis (see s_PollX).
 */
void portInputMousePark(s32 x, s32 y, s32 axes)
{
    s_MouseVX = x;
    s_MouseVY = y;
    if (!s_MouseLocked)
    {
        mouseWarpFree(x, y, axes);
    }
    s_MouseMoved = 0;
}

/** midas.ini show_input: the bondview stick X/Y overlay reads this. */
s32 portInputShowInput(void)
{
    return s_OptShowInput;
}

u32 portInputMouseButtons(void)
{
    /* buttons only count while captured - clicking around an unlocked
     * window (or the lock-click itself... which does land; acceptable,
     * PD behaves the same) shouldn't fire wildly. Keep it simple: report
     * always; binds only matter in-game where the mouse is locked. */
    return s_MouseButtons;
}

s32 portInputMouseLocked(void)
{
    return s_MouseLocked;
}

/**
 * Consumed once per game tick by bondviewProcessInput (player 1 only).
 * PD's exact scaling: sens * (raw / 3.5) * 0.022 -> a stick-normalized
 * [-1..1]-ish term the caller adds before the FOV scale.
 */
void portInputMouseLookDelta(f32 *dx, f32 *dy)
{
    f32 mx = 0.0f, my = 0.0f;

    /* p1_hybrid=0 with a pad owning player 1: a bumped mouse must not turn
     * Bond either - only the keys were muted. Gameplay is the only
     * reader of these deltas. */
    if (s_OptMouse && s_MouseLocked && !portInputP1KbmMuted())
    {
        mx = s_OptMouseSensX * ((f32)s_MouseDX / 3.5f) * 0.022f;
        my = s_OptMouseSensY * ((f32)s_MouseDY / 3.5f) * 0.022f;
    }
    if (dx) *dx = mx;
    if (dy) *dy = my;
}

/**
 * DIRECT-mode deltas, in degrees of camera rotation before FOV scaling,
 * following the 1964 GEPD mouse injector (GPL-2.0) but reimplemented against
 * our own state.
 *
 * Degrees per mouse count = counts * SENSITIVITY / 400, with the FOV ratio (fov
 * / basefov) applied by the caller. midas.ini's mouse_sens_x/y sit where the
 * injector's SENSITIVITY/40 does. There is no smoothing, accumulation or
 * acceleration (no mouse acceleration, by design).
 */
void portInputMouseLookDeltaDeg(f32 *dxdeg, f32 *dydeg)
{
    f32 mx = 0.0f, my = 0.0f;

    if (s_OptMouse && s_MouseLocked && !portInputP1KbmMuted())
    {
        /* /2.5 normalises midas.ini's existing default (mouse_sens_x = 2.5,
         * tuned for the old rate path) onto the injector's own default of
         * SENSITIVITY 40 - so a stock midas.ini gives stock 1964 feel and the
         * knob scales from there instead of meaning something new. */
        mx = ((f32) s_MouseDX / 10.0f) * (s_OptMouseSensX / 2.5f);
        my = ((f32) s_MouseDY / 10.0f) * (s_OptMouseSensY / 2.5f);
    }
    if (dxdeg) *dxdeg = mx;
    if (dydeg) *dydeg = my;
}

/** midas.ini [input] mouse_mode - 1 = direct angle (default), 0 = legacy rate. */
s32 portInputMouseDirectMode(void)
{
    return s_OptMouseMode != 0;
}

/** midas.ini [input] mouse_sens_aim - crosshair speed multiplier in aim mode. */
f32 portInputMouseAimScale(void)
{
    return (s_OptMouseSensAim > 0.0f) ? s_OptMouseSensAim : 1.0f;
}

/**
 * GE_RUMBLETEST=<sdl index> | r<chan> (the launcher's "Test rumble"): buzzes
 * the pad 1s on / off, twice, printing each step. r<chan> arms the Rumble Pak
 * through the raphnet RAW path; a plain index rumbles an SDL pad.
 */
void portInputRumbleTestAndExit(const char *arg)
{
    extern s32 portRaphnetRumble(s32 chan, s32 rumble_on);
    int chan = (arg[0] == 'r') ? SDL_atoi(arg + 1) : SDL_atoi(arg);
    int i;

    if (arg[0] != 'r')
    {
        /* SDL flavor: rumble the pad at the launcher's device index (same as
         * the GE_LISTPADS probe). SDL's Windows RawInput driver refuses rumble
         * until the pad is correlated with an XInput slot (needs a button
         * press), so re-init with RawInput off, which brings the same device up
         * on the XInput driver, and find it again by VID/PID (indices can
         * shift). */
        SDL_Joystick *j;
        Uint16 want_vid = 0, want_pid = 0;
        int pick = chan;
        int n;

        inputBackendHints();
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        {
            printf("ge: rumbletest: SDL init failed: %s\n", SDL_GetError());
            fflush(stdout);
            exit(0);
        }
#if SDL_VERSION_ATLEAST(2, 26, 0)
        if (chan >= 0 && chan < SDL_NumJoysticks())
        {
            SDL_GetJoystickGUIDInfo(SDL_JoystickGetDeviceGUID(chan), &want_vid, &want_pid, NULL, NULL);
        }
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        {
            printf("ge: rumbletest: SDL re-init failed: %s\n", SDL_GetError());
            fflush(stdout);
            exit(0);
        }
        n = SDL_NumJoysticks();
        if (want_vid != 0 || want_pid != 0)
        {
            Uint16 vid, pid;

            /* the launcher's index first, so two identical pads keep
             * their order; then the first VID/PID match anywhere */
            pick = -1;
            for (i = -1; i < n && pick < 0; i++)
            {
                int cand = (i < 0) ? chan : i;
                if (cand < 0 || cand >= n || (i >= 0 && cand == chan))
                {
                    continue;
                }
                SDL_GetJoystickGUIDInfo(SDL_JoystickGetDeviceGUID(cand), &vid, &pid, NULL, NULL);
                if (vid == want_vid && pid == want_pid)
                {
                    pick = cand;
                }
            }
            if (pick < 0)
            {
                printf("ge: rumbletest: pad %d (vid %04x pid %04x) not found with RawInput off - using index\n",
                       chan, want_vid, want_pid);
                pick = chan;
            }
        }
#else
        (void)want_vid; (void)want_pid; (void)n;
#endif
        if ((j = SDL_JoystickOpen(pick)) == NULL)
        {
            printf("ge: rumbletest: no controller at index %d (%s)\n", pick, SDL_GetError());
            fflush(stdout);
            exit(0);
        }
        {
            char g[40];
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), g, sizeof(g));
            printf("ge: rumbletest: pad %d '%s' guid=%s\n", pick, SDL_JoystickName(j), g);
        }
        for (i = 0; i < 2; i++)
        {
            int r = SDL_JoystickRumble(j, 0xC000, 0xC000, 1100);
            printf("ge: rumbletest: ON  -> %d%s%s\n", r, r ? " " : "", r ? SDL_GetError() : "");
            fflush(stdout);
            SDL_Delay(1000);
            SDL_JoystickRumble(j, 0, 0, 0);
            printf("ge: rumbletest: OFF\n");
            fflush(stdout);
            SDL_Delay(400);
        }
        printf("ge: rumbletest: done\n");
        fflush(stdout);
        exit(0);
    }

    if (!portRaphnetAvailable())
    {
        printf("ge: rumbletest: no raphnet adapter\n");
        exit(0);
    }
    /* the threaded poller writes rumble only on channels the game has read:
     * one poll makes this one active, or the motor was never asked */
    portRaphnetPoll(chan, NULL, NULL, NULL);
    for (i = 0; i < 2; i++)
    {
        printf("ge: rumbletest: ON  -> %d\n", portRaphnetRumble(chan, 1));
        fflush(stdout);
        SDL_Delay(1000);
        printf("ge: rumbletest: OFF -> %d\n", portRaphnetRumble(chan, 0));
        fflush(stdout);
        SDL_Delay(400);
    }
    printf("ge: rumbletest: done (0 = sent, or queued for the poller; -1 = no pak/error)\n");
    fflush(stdout);
    exit(0);
}

/* ---- rumble (motor_stub.c routes here) ---------------------------------- */

/** Per-slot "rumble pak inserted" (midas.ini padN_rumble, launcher checkbox):
 * the shim reports CONT_CARD_ON from this, which is what arms joy.c's
 * rumble state machine for that player. */
s32 portInputPakPresent(s32 slot)
{
    if (slot < 0 || slot >= INPUT_MAX_PADS)
    {
        return 0;
    }
    return s_OptRumble && s_PadRumbleOn[slot];
}

void portInputRumble(s32 channel, s32 on)
{
    if (channel < 0 || channel >= INPUT_MAX_PADS || !s_OptRumble
        || !s_PadRumbleOn[channel])
    {
        return;
    }
    if (s_PadRaphnet[channel] != 0)
    {
        /* raphnet RAW: drive the REAL Rumble Pak in the controller - the
         * adapter's SET_VIBRATION command does the pak writes in firmware,
         * and the game's own start/stop pulses become the authentic pak
         * waveform. */
        extern s32 portRaphnetRumble(s32 chan, s32 rumble_on);
        portRaphnetRumble(s_PadRaphnet[channel] - 1, on);
        return;
    }
    if (!s_PadHasRumble[channel])
    {
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 9)
    /* GE pulses start/stop for intensity; a generous burst per start, cut off
     * by stop, reproduces the N64 pak feel. A custom-mapped pad is opened as a
     * raw joystick, so rumble through whichever handle the slot holds. */
    if (s_Pads[channel] != NULL)
    {
        SDL_GameControllerRumble(s_Pads[channel], on ? 0xC000 : 0, on ? 0xC000 : 0, on ? 500 : 0);
    }
    else if (s_Joys[channel] != NULL)
    {
        SDL_JoystickRumble(s_Joys[channel], on ? 0xC000 : 0, on ? 0xC000 : 0, on ? 500 : 0);
    }
#endif
}
