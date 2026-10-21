/**
 * joy_port.c - the "Port" input suite: actions beyond the N64 pad (Reload, Use,
 * Walk/Run, Previous/Next Weapon, Previous/Next Inventory, Crouch, ...).
 *
 * How it is built:
 *   - This file owns the action table (midas.ini [controls] keys and defaults), the
 *     per-poll edge/hold state, and the game-side meaning of every action. It has
 *     NO SDL dependency: video.c parses the key names into scancodes with the same
 *     machinery as the N64 binds and calls portJoyPortPoll() once per pad poll with
 *     one "down" byte per action.
 *   - One-shot actions are LATCHED on the press edge (poll layer) and consumed by
 *     the game tick that acts on them (portJoyPortTake). A latch older than 0.25s
 *     of real time is dropped, so a press made in a menu, while dead or during a
 *     cutscene never fires later.
 *   - Held actions (walk/run) are plain levels.
 *
 * Game semantics. Everything is per pad: the keyboard is pad 0, and a
 * controller with a launcher custom map (midas.ini padN_reload=b2 ...) feeds its
 * own slot, so player N reads pad N. All actions are gated by
 * portBondControlsLive() in bondview2.c, so nothing fires during pause, the
 * watch, death, cutscenes or locked controls.
 *   reload         attempt_reload_item_in_hand(GUNRIGHT/GUNLEFT): what a B tap does
 *                  when nothing is there to use, without the use.
 *   use            injects the game's own B tap (moveData.btap in
 *                  bondviewProcessInput), so tank enter/exit, doors and every other
 *                  B use path work, and suppresses only the lv.c "nothing to use ->
 *                  reload" fallback for that tap.
 *   walk / run     g_CurrentPlayer->speedforwards/sideways x 0.5 while "walking"
 *                  (MoveBond, next to the crouch 0.5), so it covers analog,
 *                  C-button digital steps and controllers. Always Run (default):
 *                  walk = hold Walk. Always Walk: walk unless Run is held.
 *                  walk_toggle flips the mode; midas.ini [input] always_walk=0/1 is the
 *                  boot value.
 *   prev/next weapon  moveData.weaponBackOffset / weaponForwardOffset in
 *                  bondviewProcessInput, exactly the A-tap / A+Z paths (gun.c
 *                  backstep_/advance_through_inventory), so the MP golden-gun lock
 *                  and the gadget->last-gun snap-back behave as on N64.
 *   prev/next inventory  walks the watch's own item list
 *                  (bondinvCountTotalItemsInInv / bondinvGetTextbyInvIndex) from the
 *                  equipped index to the next/previous entry in the scroll
 *                  WHITELIST (sInvScrollItems: gadgets with a real in-hand use; keys,
 *                  documents and tapes are skipped) and equips it with the animated
 *                  switch request. While a whitelisted item is in the right hand its
 *                  name is drawn in the left-hand ammo slot (portJoyPortGadgetHudName,
 *                  gunfire.c generate_ammo_total_microcode).
 *   detonate       remote mines in hand: moveData.detonating, the vanilla A+B chord
 *                  as one press (portJoyPortDetonate at the use-tap site in
 *                  bondviewProcessInput). [input] detonate_use_reload=1 (default)
 *                  lets use / reload fire it too while mines are held.
 *   crouch_toggle  press: moveData.crouchDown when standing, crouchUp when squatting
 *                  -> currentPlayerAdjustCrouchPos(-2/+2), the aim-mode + stick-down
 *                  duck without needing aim mode.
 *   crouch_hold    squat while held, stand on release (a release that lands while
 *                  controls are not live is remembered and done on the next live
 *                  tick).
 *
 * Harness: GE_JOYPORT_AUTO="<action>:<poll>[,<action>:<poll>...]" presses an
 * action for one poll at that poll count (headless proof, no keyboard);
 * GE_JOYPORT_DBG=1 logs every action with the state it changed.
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include "game/bondview.h"
#include "game/bondinv.h"
#include "game/gun.h"
#include "game/player.h"
#include "game/chrai.h"

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console ring (spec section 7). */
extern void portConLogf(const char *fmt, ...);

/* ---- action table --------------------------------------------------------- */

enum {
    PORTACT_RELOAD,
    PORTACT_USE,
    PORTACT_WALK,
    PORTACT_RUN,
    PORTACT_WALK_TOGGLE,
    PORTACT_PREV_WEAPON,
    PORTACT_NEXT_WEAPON,
    PORTACT_PREV_INV,
    PORTACT_NEXT_INV,
    PORTACT_CROUCH_TOGGLE,
    PORTACT_CROUCH_HOLD,
    PORTACT_WATCH_PREV,
    PORTACT_WATCH_NEXT,
    PORTACT_WATCH_OBJECTIVES, /* pause straight onto the Briefing (objectives) page; again = close */
    PORTACT_DETONATE,         /* remote mines: the A+B detonate as one button */
    /* Appended actions: zoom, because the C buttons that zoom on N64 are not
     * bound on a modern pad. Lean has no action on purpose; it stays on the
     * Move input in bondviewProcessInput, as on N64. */
    PORTACT_ZOOM_IN,
    PORTACT_ZOOM_OUT,
    /* Appended: menu-only Accept and Back. Nothing in play takes them, so an
     * unclaimed press expires by itself; they do the same jobs as use and the
     * gadget buttons, under their own names so a player can move them
     * separately. */
    PORTACT_MENU_ACCEPT,
    PORTACT_MENU_BACK,
    /* Mouse unlock key (appended, keyboard only). It must work on every menu,
     * so video.c reads the key directly (portVideoMouseUnlockPressed); it is
     * listed here only so the ini and rebind code treat it as a named bind. */
    PORTACT_MOUSE_UNLOCK,
    /* Classic Mode toggle (appended, keyboard only, default Tab). Like
     * mouse_unlock it skips the latch machinery:
     * video.c takes the edge off the key (portVideoClassicTogglePressed) and
     * bondview2.c acts on it in play. */
    PORTACT_CLASSIC_TOGGLE,
    /* Mod actions (port_mods.c): claimed in order, named by the mod. The
     * input_pc.c pad map has the same eight rows. */
    PORTACT_MOD_FIRST,
    PORTACT_MOD_LAST = PORTACT_MOD_FIRST + 7,
    PORTACT_COUNT
};

static const struct {
    const char *name;    /* midas.ini key */
    const char *defkeys; /* default value (same syntax as the N64 binds) */
    s32 oneshot;         /* 1 = latched press, 0 = held level */
} sPortActs[PORTACT_COUNT] = {
    /* Defaults are the owner's keyboard layout: use on E replaces B in play,
     * the wheel replaces A; the N64 a/b/analog/dpad slots ship unbound (video.c
     * sBindInfo). */
    [PORTACT_RELOAD]      = { "reload",         "R",                1 },
    [PORTACT_USE]         = { "use",            "E",                1 },
    [PORTACT_WALK]        = { "walk",           "Left Shift",       0 },
    [PORTACT_RUN]         = { "run",            "",                 0 }, /* retired with Always Walk; slot kept
                                                                          * because the table is indexed */
    [PORTACT_WALK_TOGGLE] = { "walk_toggle",    "",                 1 }, /* retired (was CapsLock); flips nothing */
    /* wheel UP = previous, wheel DOWN = next */
    [PORTACT_PREV_WEAPON] = { "prev_weapon",    "Mouse Wheel Up",   1 },
    [PORTACT_NEXT_WEAPON] = { "next_weapon",    "Mouse Wheel Down", 1 },
    [PORTACT_PREV_INV]    = { "prev_inventory", "[",                1 },
    [PORTACT_NEXT_INV]    = { "next_inventory", "Q",                1 },
    [PORTACT_CROUCH_TOGGLE] = { "crouch_toggle", "",               1 },
    [PORTACT_CROUCH_HOLD]   = { "crouch_hold",   "Left Ctrl",      0 },
    /* watch (pause) menu page left / right - the L / R trigger role, as a Port
     * bind because the mouse-bound aim (R) is muted while the watch is up */
    [PORTACT_WATCH_PREV]    = { "watch_prev",    "Comma",          1 }, /* SDL's "," is the list separator (video.c sSepKeyNames) */
    [PORTACT_WATCH_NEXT]    = { "watch_next",    ".",              1 },
    /* pause straight onto the watch's Briefing (objectives) page */
    [PORTACT_WATCH_OBJECTIVES] = { "watch_objectives", "",         1 },
    /* Detonate remote mines (A+B on N64); use / reload also trigger it while
     * mines are held (sDetonateUseReload). Append-only: the input_pc.c pad-map
     * rows and launcher BindDefs index this table by position. */
    [PORTACT_DETONATE]         = { "detonate",         "",         1 },
    /* HELD levels, not taps: you zoom for as long as you hold them. Unbound
     * by default - the C buttons still zoom on the shipped keyboard layout,
     * so binding these over the top would give one key two jobs. */
    [PORTACT_ZOOM_IN]          = { "zoom_in",          "",         0 },
    [PORTACT_ZOOM_OUT]         = { "zoom_out",         "",         0 },
    /* unbound: use and the gadget buttons already do both, and a default here
     * would hand one key two jobs */
    [PORTACT_MENU_ACCEPT]      = { "menu_accept",      "",         1 },
    [PORTACT_MENU_BACK]        = { "menu_back",        "",         1 },
    /* F1 has always been this key; it is now just a default */
    [PORTACT_MOUSE_UNLOCK]     = { "mouse_unlock",     "F1",       1 },
    [PORTACT_CLASSIC_TOGGLE]   = { "classic_toggle",   "Tab",      1 },
    /* one-shots, so a mod gets both the latch and the live level (sPrevDown);
     * name and default come from port_mods.c */
    [PORTACT_MOD_FIRST ... PORTACT_MOD_LAST] = { NULL, "", 1 },
};

#include "port_mods.h"

/** sPortActs[i].name, or the mod's name for a claimed mod slot (NULL unclaimed). */
static const char *joyPortActName(s32 i)
{
    if (i >= PORTACT_MOD_FIRST && i <= PORTACT_MOD_LAST) {
        return portModsActionName(i - PORTACT_MOD_FIRST);
    }
    return (i >= 0 && i < PORTACT_COUNT) ? sPortActs[i].name : NULL;
}

/* [input] detonate_use_reload (default ON): with remote mines in the right
 * hand a use or reload press DETONATES instead of using / reloading - the
 * two keys a mine has no other job for. Off = only the detonate bind. */
static s32 sDetonateUseReload = 1;

/* [input] shoot_next_prev (default ON): the N64's A+Z chord for the port
 * binds. While next_weapon (or next_inventory) is held, each shoot tap steps
 * BACK one weapon (gadget) and does not fire, so a layout with no
 * prev_weapon / prev_inventory bind (Standard) can still go back.
 * bondviewProcessInput reports shoot each tick (portJoyPortShootChord). */
static s32 sShootNextPrev = 1;
static s32 sShootPrev[4];   /* shoot was down last tick */
static s32 sShootTap[4];    /* shoot went down this tick */
static s32 joyPortPad(void);

/* Per pad: the keyboard feeds pad 0 (video.c portJoyPortPoll) and a controller
 * with a launcher custom map feeds its own slot (input_pc.c
 * portJoyPortPadDown), both OR-ed into sPending. libultra_shim commits once per
 * pad poll (portJoyPortPollCommit), and consumers index by get_cur_playernum(),
 * so player N is pad N. */
#define PORT_PADS 4
static u8  sPending[PORT_PADS][PORTACT_COUNT];  /* "down" bits gathered this poll */
static u8  sPrevDown[PORT_PADS][PORTACT_COUNT];
static u8  sHeld[PORT_PADS][PORTACT_COUNT];
static u32 sLatchPoll[PORT_PADS][PORTACT_COUNT]; /* poll count of the latched press; 0 = none */
/* osGetTime at the latched press. Staleness is judged in real time rather than
 * poll count, because the render-thread pump free-spins and would expire
 * presses before any tick could take them. It is the full 64-bit osGetTime: the
 * u32 osGetCount wraps every 91.6s and would make a stale latch look fresh
 * again. */
static OSTime sLatchCount[PORT_PADS][PORTACT_COUNT];
/* Presses outstanding per one-shot: every edge increments and portJoyPortTake
 * hands them out one per call, so a fast wheel flick becomes several weapon
 * steps instead of losing presses between 60 Hz ticks. Capped so a stuck pump
 * cannot bank a burst; staleness is judged from the newest edge. */
#define PORT_LATCH_MAX_PENDING 8
static u8  sLatchN[PORT_PADS][PORTACT_COUNT];
static u32 sPollCount;
static s32 sAlwaysWalk[PORT_PADS];    /* per pad, so one player's Walk Toggle
                                       * cannot flip it for all.
                                       * 0 = Always Run (walk = hold Walk), 1 = Always Walk */
/* Per-player overrides of the global settings: [input] padN_always_walk /
 * padN_detonate_use_reload, where -1 = no key (follow the global) and 0 / 1 =
 * this player's own answer. It is a tri-state so the order of keys in midas.ini
 * cannot matter: the global key skips any pad that has its own. */
static s32 sPadAlwaysWalkOwn[PORT_PADS] = { -1, -1, -1, -1 };
static s32 sPadDetonateOwn[PORT_PADS]   = { -1, -1, -1, -1 };
/* padN_look_updown, same sense as the global [display] look_updown (1 =
 * upright, 0 = reverse). Read in options.c (portPlayerLookUpright) with two or
 * more players only, so a multiplayer pick never turns a solo game upside down. */
static s32 sPadLookOwn[PORT_PADS]       = { -1, -1, -1, -1 };
/* padN_look_ahead: Rare's Look Ahead per player, read in options.c
 * (portPlayerLookAhead) with two or more players only, like look_updown above.
 * -1 = follow the global. */
static s32 sPadLookAheadOwn[PORT_PADS]  = { -1, -1, -1, -1 };
/* padN_auto_aim: Rare's Auto-Aim per player, read by front.c
 * copy_aim_settings_to_playerdata when the Multiplayer Options row is PP. */
static s32 sPadAutoAimOwn[PORT_PADS]    = { -1, -1, -1, -1 };
static s32 sDbg = -1;

static s32 joyPortDbg(void)
{
    if (sDbg < 0) {
        sDbg = getenv("GE_JOYPORT_DBG") != NULL;
    }
    return sDbg;
}

/* unclaimed mod slots are not binds at all: no ini key, no rebind row */
s32 portJoyPortBindCount(void)
{
    return PORTACT_MOD_FIRST + portModsActionCount();
}

const char *portJoyPortBindName(s32 i)
{
    return (i < portJoyPortBindCount()) ? joyPortActName(i) : NULL;
}

const char *portJoyPortBindDefault(s32 i)
{
    if (i >= PORTACT_MOD_FIRST && i < portJoyPortBindCount()) {
        return portModsActionDefault(i - PORTACT_MOD_FIRST);
    }
    return (i >= 0 && i < PORTACT_MOD_FIRST) ? sPortActs[i].defkeys : "";
}

s32 portJoyPortAlwaysWalk(void)
{
    return sAlwaysWalk[0]; /* ini / Port Options value = pad 0's; the bind is per pad */
}

/** Always Walk is retired: walk is just the Walk bind held. sAlwaysWalk stays
  * 0, the always_walk / padN_always_walk
 * keys from older midas.ini files or profiles are accepted and ignored, and the
 * PORTACT_* entries stay because the table is append-only. */
static void joyPortSetAlwaysWalkAll(s32 v)
{
    (void) v;
    (void) sPadAlwaysWalkOwn;
}

/* "auto" (or anything not a number) = no answer of their own */
static s32 joyPortOwnValue(const char *value)
{
    return (value[0] >= '0' && value[0] <= '9') ? (atoi(value) != 0) : -1;
}

/** midas.ini keys this module owns besides the binds; returns 1 when consumed. */
s32 portJoyPortSetOption(const char *key, const char *value)
{
    if (strcmp(key, "always_walk") == 0) {
        joyPortSetAlwaysWalkAll(atoi(value) != 0);
        return 1;
    }
    if (strcmp(key, "detonate_use_reload") == 0) {
        sDetonateUseReload = atoi(value) != 0;
        return 1;
    }
    if (strcmp(key, "shoot_next_prev") == 0) {
        sShootNextPrev = atoi(value) != 0;
        return 1;
    }
    /* padN_<the same two>: input_pc.c's padN_ arm is asked first and turns
     * these down (they are not pad controls), so they arrive here */
    if (strncmp(key, "pad", 3) == 0 && key[3] >= '1' && key[3] <= '4' && key[4] == '_') {
        const s32 pad = key[3] - '1';

        if (strcmp(key + 5, "always_walk") == 0) {
            return 1;   /* retired with always_walk - accepted, ignored */
        }
        if (strcmp(key + 5, "detonate_use_reload") == 0) {
            sPadDetonateOwn[pad] = joyPortOwnValue(value);
            return 1;
        }
        if (strcmp(key + 5, "look_updown") == 0) {
            sPadLookOwn[pad] = joyPortOwnValue(value);
            return 1;
        }
        if (strcmp(key + 5, "look_ahead") == 0) {
            sPadLookAheadOwn[pad] = joyPortOwnValue(value);
            return 1;
        }
        if (strcmp(key + 5, "auto_aim") == 0) {
            sPadAutoAimOwn[pad] = joyPortOwnValue(value);
            return 1;
        }
    }
    if (portModsSetOption(key, value)) {
        return 1;
    }
    {   /* [input] playerN_profile: who is sitting where (port_profiles.c) */
        extern s32 portProfileIniKey(const char *key, const char *value);

        return portProfileIniKey(key, value);
    }
}

s32 portJoyPortDetonateUseReload(void)
{
    return sDetonateUseReload;
}

s32 portJoyPortShootNextPrev(void)
{
    return sShootNextPrev;
}

/** bondviewProcessInput, every tick before the trigger is used: `held` = this
 * player's shoot button is down. Returns 1 while next weapon or next gadget
 * is held with the chord on, so the caller holds fire (vanilla drops the
 * trigger while A is held). */
s32 portJoyPortShootChord(s32 held)
{
    s32 pad = joyPortPad();

    if (pad < 0) {
        return 0;
    }
    sShootTap[pad] = held && !sShootPrev[pad];
    sShootPrev[pad] = held;
    /* sPrevDown, not sHeld: next_weapon / next_inventory are one-shots, and
     * sHeld is only kept for held actions (portJoyPortPollCommit) */
    return sShootNextPrev && (sPrevDown[pad][PORTACT_NEXT_WEAPON] || sPrevDown[pad][PORTACT_NEXT_INV]);
}

/* the chord: a shoot tap while this next action is held = one step back */
static s32 joyPortChordBack(s32 pad, s32 act)
{
    return sShootNextPrev && sShootTap[pad] && sPrevDown[pad][act];
}

/** What is in force for one player: `which` 0 = always walk, 1 = use+reload
  * detonates, 2 = look up/down, 3 = look
 * ahead. Values 2 and 3 answer the player's OWN value or -1 for none; the
 * global they fall back to lives in options.c (portPlayerLookUpright /
 * portPlayerLookAhead). */
s32 portJoyPortPadOptGet(s32 pad, s32 which)
{
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    if (which == 0) {
        return sAlwaysWalk[pad];
    }
    if (which == 2) {
        return sPadLookOwn[pad];
    }
    if (which == 3) {
        return sPadLookAheadOwn[pad];
    }
    if (which == 4) {
        return sPadAutoAimOwn[pad];
    }
    return (sPadDetonateOwn[pad] >= 0) ? sPadDetonateOwn[pad] : sDetonateUseReload;
}

/** ...and set it as that player's OWN answer: live, and [input] padN_<key>
 * in midas.ini (portVideoIniSetKey comes back in through portJoyPortSetOption). */
void portJoyPortPadOptSet(s32 pad, s32 which, s32 v)
{
    extern s32 portVideoIniSetKey(const char *key, const char *value);
    char key[40];

    if (pad < 0 || pad >= PORT_PADS) {
        return;
    }
    snprintf(key, sizeof(key), "pad%d_%s", (int) pad + 1,
             (which == 0) ? "always_walk" : (which == 2) ? "look_updown"
             : (which == 3) ? "look_ahead" : (which == 4) ? "auto_aim" : "detonate_use_reload");
    portVideoIniSetKey(key, v ? "1" : "0");
}

/** Port Options bridge (port_options.c): the same keys as integers. */
s32 portJoyPortOptionGet(const char *key, s32 *out)
{
    if (strcmp(key, "detonate_use_reload") == 0) {
        *out = sDetonateUseReload;
        return 1;
    }
    if (strcmp(key, "shoot_next_prev") == 0) {
        *out = sShootNextPrev;
        return 1;
    }
    return portModsOptionGet(key, out);
}

s32 portJoyPortOptionSet(const char *key, s32 v)
{
    if (strcmp(key, "always_walk") == 0) {
        joyPortSetAlwaysWalkAll(v != 0);
        return 1;
    }
    if (strcmp(key, "detonate_use_reload") == 0) {
        sDetonateUseReload = v != 0;
        return 1;
    }
    if (strcmp(key, "shoot_next_prev") == 0) {
        sShootNextPrev = v != 0;
        return 1;
    }
    return portModsOptionSet(key, v);
}

/* ---- harness: GE_JOYPORT_AUTO ------------------------------------------- */

#define AUTO_MAX 16
static s32 sAutoAct[AUTO_MAX];
static u32 sAutoPoll[AUTO_MAX];
static s32 sAutoCount = -1;

static void joyPortAutoParse(void)
{
    const char *e;
    sAutoCount = 0;
    e = getenv("GE_JOYPORT_AUTO");
    while (e != NULL && *e != '\0' && sAutoCount < AUTO_MAX) {
        char name[32];
        s32 n = 0, i;
        while (*e != '\0' && *e != ':' && *e != ',' && n < (s32) sizeof(name) - 1) {
            name[n++] = *e++;
        }
        name[n] = '\0';
        if (*e == ':') {
            e++;
            for (i = 0; i < PORTACT_COUNT; i++) {
                if (joyPortActName(i) != NULL && strcmp(name, joyPortActName(i)) == 0) {
                    sAutoAct[sAutoCount] = i;
                    sAutoPoll[sAutoCount] = (u32) atoi(e);
                    sAutoCount++;
                    break;
                }
            }
            if (i == PORTACT_COUNT) {
                portConLogf("ge: GE_JOYPORT_AUTO: unknown action \"%s\"\n", name);
            }
            while (*e != '\0' && *e != ',') e++;
        }
        if (*e == ',') e++;
    }
}

/* ---- poll layer (called by video.c once per pad poll) ------------------- */

/**
 * video.c, keyboard: down[i] = 1 if any key bound to action i is held this
 * poll. Gathered into pad 0's pending set; the edge/latch work happens in
 * portJoyPortPollCommit.
 */
void portJoyPortPoll(const u8 *down)
{
    s32 i;
    for (i = 0; i < PORTACT_COUNT; i++) {
        if (down[i]) {
            sPending[0][i] = 1;
        }
    }
}

/** input_pc.c, a controller's custom map: action `act` is held on pad `pad` this poll. */
void portJoyPortPadDown(s32 pad, s32 act)
{
    if (pad >= 0 && pad < PORT_PADS && act >= 0 && act < PORTACT_COUNT) {
        sPending[pad][act] = 1;
    }
}

/**
 * libultra_shim osContGetReadData, once per pad poll after every source
 * has merged: edge-latches the one-shots, tracks the held levels per pad,
 * flips the walk mode, fires GE_JOYPORT_AUTO (pad 0), clears the pending
 * sets. Runs under GE_NOINPUT too (the harness must still fire).
 */
void portJoyPortPollCommit(void)
{
    s32 pad, i;

    sPollCount++;
    if (sAutoCount < 0) {
        joyPortAutoParse();
    }
    for (i = 0; i < sAutoCount; i++) {
        if (sAutoPoll[i] == sPollCount) {
            sPending[0][sAutoAct[i]] = 1;
            if (joyPortDbg()) {
                portConLogf("ge: joyport: AUTO press %s at poll %u\n", joyPortActName(sAutoAct[i]), sPollCount);
            }
        }
    }
    { /* GE_INPUTREC / GE_INPUTPLAY (inputrec.c): pad 0's actions down this poll */
        extern void portInputRecActions(u8 *down, s32 count);
        portInputRecActions(sPending[0], PORTACT_COUNT);
    }
    for (pad = 0; pad < PORT_PADS; pad++) {
        for (i = 0; i < PORTACT_COUNT; i++) {
            u8 d = sPending[pad][i];
            if (sPortActs[i].oneshot) {
                if (d && !sPrevDown[pad][i]) {
                    sLatchPoll[pad][i] = sPollCount;
                    sLatchCount[pad][i] = osGetTime();
                    if (sLatchN[pad][i] < PORT_LATCH_MAX_PENDING) {
                        sLatchN[pad][i]++;
                    }
                }
            } else {
                sHeld[pad][i] = d;
            }
            sPrevDown[pad][i] = d;
            sPending[pad][i] = 0;
        }
        /* walk_toggle is retired with Always Walk: its latch is drained so it
         * cannot bank, and flips nothing */
        if (sLatchPoll[pad][PORTACT_WALK_TOGGLE] != 0) {
            sLatchPoll[pad][PORTACT_WALK_TOGGLE] = 0;
            sLatchN[pad][PORTACT_WALK_TOGGLE] = 0;
        }
    }
}

/** Consume a fresh latched press of a one-shot action on a pad (1 = fired). */
static s32 portJoyPortTake(s32 pad, s32 act)
{
    u32 p = sLatchPoll[pad][act];
    if (p == 0 || sLatchN[pad][act] == 0) {
        sLatchPoll[pad][act] = 0;
        sLatchN[pad][act] = 0;
        return 0;
    }
    /* REAL-TIME staleness (see sLatchCount): 0.25s at 46875 counts/ms.
     * 64-bit, so an old latch never wraps back to fresh. Judged
     * from the newest edge: a burst is either fresh or dropped whole. */
    if (osGetTime() - sLatchCount[pad][act] > 250ull * 46875ull) {
        sLatchPoll[pad][act] = 0;
        sLatchN[pad][act] = 0;
        return 0; /* stale: pressed while nobody could act on it */
    }
    if (--sLatchN[pad][act] == 0) {
        sLatchPoll[pad][act] = 0; /* drained: one press per call */
    }
    return 1;
}

/** Drop every banked press of a menu-only action. In play nothing takes
  * menu_accept / menu_back, but some layouts
 * share buttons with use / next_inventory, so a press must not wait and then
 * confirm or close the MP pause menu that opens a moment later. */
static void joyPortDrop(s32 pad, s32 act)
{
    sLatchPoll[pad][act] = 0;
    sLatchN[pad][act] = 0;
}

/* ---- game side ------------------------------------------------------------ */

extern s32 portBondControlsLive(void);  /* bondview2.c: the mouse-look permission predicate */
extern int bondinvGetCurEquippedItem(void);
extern void bondinvSetCurEquippedItem(int current_item);
extern s32 bondinvGetTextbyInvIndex(s32 index);

/** The pad of the player whose tick this is (player N = pad N), -1 if none. */
static s32 joyPortPad(void)
{
    s32 p = get_cur_playernum();
    return (p >= 0 && p < PORT_PADS) ? p : -1;
}

static s32 joyPortPermitted(void)
{
    return portBondControlsLive();
}

/**
 * options.c watch mouse tick: -1 = watch_prev, +1 = watch_next, 0 = none.
 * NOT gated by joyPortPermitted (the watch is up = controls are not live);
 * the caller injects the matching D-pad edge so the vanilla page code runs.
 */
s32 portJoyPortWatchStep(void)
{
    s32 prev, next;
    s32 pad = joyPortPad();
    if (pad < 0) {
        return 0;
    }
    prev = portJoyPortTake(pad, PORTACT_WATCH_PREV);
    next = portJoyPortTake(pad, PORTACT_WATCH_NEXT);
    return next ? 1 : (prev ? -1 : 0);
}

/** port_mods.c action_pressed: a latched press of mod slot `slot`, taken. */
s32 portJoyPortModTake(s32 slot)
{
    s32 pad = joyPortPad();

    if (pad < 0 || slot < 0 || slot > PORTACT_MOD_LAST - PORTACT_MOD_FIRST) {
        return 0;
    }
    return portJoyPortTake(pad, PORTACT_MOD_FIRST + slot) && joyPortPermitted();
}

/** port_mods.c action_held: the live level (a one-shot's is sPrevDown). */
s32 portJoyPortModDown(s32 slot)
{
    s32 pad = joyPortPad();

    if (pad < 0 || slot < 0 || slot > PORTACT_MOD_LAST - PORTACT_MOD_FIRST) {
        return 0;
    }
    return sPrevDown[pad][PORTACT_MOD_FIRST + slot] && joyPortPermitted();
}

/**
 * bondviewProcessInput, aim mode: 1 while the player is asking to lean or
 * zoom with a PORT binding rather than a C button. `act` is a PORTACT_*.
 */
s32 portJoyPortHeld(s32 act)
{
    s32 pad = joyPortPad();

    if (pad < 0 || act < 0 || act >= PORTACT_COUNT)
    {
        return 0;
    }
    return sHeld[pad][act] != 0;
}

/**
 * The same question as a 0..1 level: a stick or trigger gives its deflection
 * from the dead zone up, and a key or button is 1 while held. This lets a port
 * binding zoom by stick magnitude, as the 2.x control styles do.
 */
f32 portJoyPortLevel(s32 act)
{
    s32 pad = joyPortPad();

    if (pad < 0 || act < 0 || act >= PORTACT_COUNT)
    {
        return 0.0f;
    }
#ifdef GE_VIDEO
    {
        extern f32 portInputPadActionLevel(s32 slot, s32 act);
        const f32 l = portInputPadActionLevel(pad, act);

        if (l > 0.0f)
        {
            return l;
        }
    }
#endif
    return sHeld[pad][act] ? 1.0f : 0.0f;
}

/** video.c: which bind index is the mouse unlock key. */
s32 portJoyPortActMouseUnlock(void) { return PORTACT_MOUSE_UNLOCK; }
s32 portJoyPortActClassicToggle(void) { return PORTACT_CLASSIC_TOGGLE; }

/** A PAD's classic_toggle press (input_pc.c padN_classic_toggle), taken. The
 * keyboard's Tab never reaches this latch: video.c zeroes it and keeps its own
 * edge (portVideoClassicTogglePressed). */
s32 portJoyPortClassicTogglePressed(s32 pad)
{
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    return portJoyPortTake(pad, PORTACT_CLASSIC_TOGGLE);
}

/** The PORTACT_* ids the game side needs by name, so it needs no enum. */
s32 portJoyPortActZoomIn(void)    { return PORTACT_ZOOM_IN; }
s32 portJoyPortActZoomOut(void)   { return PORTACT_ZOOM_OUT; }

/** MoveBond: 1 while Bond should move at half speed. */
s32 portJoyPortWalkActive(void)
{
    s32 pad = joyPortPad();
    if (pad < 0) {
        return 0;
    }
    return sAlwaysWalk[pad] ? !sHeld[pad][PORTACT_RUN] : sHeld[pad][PORTACT_WALK];
}

/**
 * bondviewProcessInput, weapon cycle: -1 = previous, +1 = next, 0 = none.
 * Consumes the latches even when not permitted (never fires later).
 */
s32 portJoyPortWeaponStep(void)
{
    s32 prev, next;
    s32 pad = joyPortPad();
    if (pad < 0) {
        return 0;
    }
    prev = portJoyPortTake(pad, PORTACT_PREV_WEAPON);
    next = portJoyPortTake(pad, PORTACT_NEXT_WEAPON);
    if (!joyPortPermitted()) {
        return 0;
    }
    if (joyPortDbg() && (prev || next)) {
        portConLogf("ge: joyport: weapon %s (right=%d left=%d before)\n", next ? "NEXT" : "PREV",
                getCurrentPlayerWeaponId(GUNRIGHT), getCurrentPlayerWeaponId(GUNLEFT));
    }
    if (joyPortChordBack(pad, PORTACT_NEXT_WEAPON)) {
        sShootTap[pad] = 0;   /* one step per tap */
        return -1;
    }
    return next ? 1 : (prev ? -1 : 0);
}

static s32 sUseNoReload[PORT_PADS]; /* this tick's btap came from the Port use key */

/**
 * bondviewProcessInput, right before "if (moveData.btap)": 1 = the Port use
 * key was pressed -> the caller sets moveData.btap (the game then handles
 * tank enter/exit, or arms field_D0 for lv.c's interact).
 */
s32 portJoyPortUseTap(void)
{
    s32 p;
    s32 pad = joyPortPad();
    if (pad < 0) {
        return 0;
    }
    /* permission FIRST: while the watch is up the latch must survive for
     * portJoyPortUseMenuPressed (use = Confirm there); a press nobody
     * takes expires by itself (PORT_LATCH_MAX_POLLS) */
    if (!joyPortPermitted()) {
        return 0;
    }
    joyPortDrop(pad, PORTACT_MENU_ACCEPT); /* menu-only: drop banked presses */
    p = portJoyPortTake(pad, PORTACT_USE);
    if (!p) {
        return 0;
    }
    sUseNoReload[pad] = 1;
    if (joyPortDbg()) {
        portConLogf("ge: joyport: pad %d use -> B tap (tank=%d)\n", pad, g_PlayerIsInTank);
    }
    return 1;
}

/**
 * Detonate hook, called by bondview2.c at the Port use-tap site BEFORE the btap
 * block (the same ordering the vanilla A+B check has: it clears btap so the tap
 * that detonated does not also use). Returns 1 = set moveData.detonating;
 * `minesInHand` (ITEM_REMOTEMINE in the right hand) is required, otherwise the
 * press is dropped and use / reload keep their own paths. With
 * detonate_use_reload on, USE and RELOAD pressed together detonate, and that
 * press is taken here so lv.c never sees the reload and no B tap is injected.
 */
s32 portJoyPortDetonate(s32 minesInHand)
{
    extern s32 getPlayerCount(void);
    s32 pad = joyPortPad();
    s32 fired = 0;
    if (pad < 0) {
        return 0;
    }
    /* The flag describes THIS tick's tap only. This runs first at the use-tap
     * site every tick, so a use that opened a door or worked the tank (lv.c
     * never consumes the flag then) cannot eat a later B reload. */
    sUseNoReload[pad] = 0;
    if (!joyPortPermitted()) {
        return 0;
    }
    if (portJoyPortTake(pad, PORTACT_DETONATE)) {
        fired = minesInHand;
    }
    /* a player's OWN value is a multiplayer pick: in solo the global row
     * rules, as for look_updown */
    if (minesInHand && ((getPlayerCount() >= 2) ? portJoyPortPadOptGet(pad, 1) : sDetonateUseReload)) {
        /* A chord, not either key alone: the second of use / reload is pressed
         * while the first is still down (sPrevDown is the level for a one-shot
         * action). The second press is taken so it does not also reload or use;
         * a lone press does what it always does. */
        const s32 useDown = sPrevDown[pad][PORTACT_USE] != 0;
        const s32 relDown = sPrevDown[pad][PORTACT_RELOAD] != 0;

        if (relDown && portJoyPortTake(pad, PORTACT_USE)) {
            fired = 1;
        }
        if (useDown && portJoyPortTake(pad, PORTACT_RELOAD)) {
            fired = 1;
        }
    }
    if (fired && joyPortDbg()) {
        portConLogf("ge: joyport: pad %d detonate (remote mines)\n", pad);
    }
    return fired;
}

/**
 * USE = CONFIRM in menus: the frontend tick (front.c menu_init, every pad) and
 * the watch tick (options.c, player 1) take a fresh use press and inject A for
 * that pad. Not gated on controls-live, because these callers ARE the not-live
 * contexts (in play portJoyPortUseTap takes the latch first, so a use tap never
 * doubles as A).
 */
s32 portJoyPortUseMenuPressed(s32 pad)
{
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    { /* both taken, so one press never fires twice */
        const s32 use = portJoyPortTake(pad, PORTACT_USE);
        const s32 acc = portJoyPortTake(pad, PORTACT_MENU_ACCEPT);
        if (!use && !acc) {
            return 0;
        }
    }
    if (joyPortDbg()) {
        portConLogf("ge: joyport: pad %d use / menu_accept -> A (menu confirm)\n", pad);
    }
    return 1;
}

/**
 * USE HELD = A HELD in the frontend: the 007-mode bars drag while A/Z is held
 * (front.c interface_menu09_007options), so the held level has to travel too,
 * not just the one-tick confirm edge. Plain level of the last poll; no latch,
 * nothing taken.
 */
s32 portJoyPortUseHeld(s32 pad)
{
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    return sPrevDown[pad][PORTACT_USE] || sPrevDown[pad][PORTACT_MENU_ACCEPT];
}

/**
 * INVENTORY = BACK in menus: front.c menu_init takes a fresh prev/next
 * inventory press and injects B for that pad. Both are taken so one press never
 * fires twice; in play portJoyPortPlayerTick takes them first.
 */
s32 portJoyPortBackMenuPressed(s32 pad)
{
    s32 a, b;
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    a = portJoyPortTake(pad, PORTACT_PREV_INV);
    b = portJoyPortTake(pad, PORTACT_NEXT_INV);
    if (portJoyPortTake(pad, PORTACT_MENU_BACK)) {
        a = 1;
    }
    if (!a && !b) {
        return 0;
    }
    if (joyPortDbg()) {
        portConLogf("ge: joyport: pad %d inventory / menu_back -> B (menu back)\n", pad);
    }
    return 1;
}

/**
 * MENU BACK alone, for the WATCH. The gadget buttons deliberately do NOT back
 * out there (they never have), so the watch cannot share the call above.
 */
s32 portJoyPortMenuBackPressed(s32 pad)
{
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    return portJoyPortTake(pad, PORTACT_MENU_BACK);
}

/**
 * CUTSCENE SKIP: a fresh use / reload / prev / next inventory press (the four
 * face buttons on the built-in layout) counts as a skip at bondview2.c's two
 * camera sites (intro camera, Bond swirl), which ask only while the camera is
 * up. The demo exit (ramromreplay.c ramrom_replay_handler, pad 0) asks too,
 * where a Port press is a real input like any N64 button.
 */
s32 portJoyPortSkipPressed(s32 pad)
{
    s32 hit = 0;
    s32 i;
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    /* Every one-shot Port action counts as a skip (reload, use, walk toggle,
     * weapon and gadget cycling, crouch toggle, watch pages, objectives); held
     * movement modifiers (walk / run / crouch hold) and the sticks do not. */
    for (i = 0; i < PORTACT_COUNT; i++) {
        if (sPortActs[i].oneshot && portJoyPortTake(pad, i)) {
            hit = 1;
        }
    }
    if (hit && joyPortDbg()) {
        portConLogf("ge: joyport: pad %d port button -> skip press (cutscene / demo exit)\n", pad);
    }
    return hit;
}

/**
 * PAUSE TO OBJECTIVES (options.c, the watch's START site): a fresh
 * watch_objectives press = open the watch on the Briefing (objectives) page, or close it.
 */
s32 portJoyPortWatchObjectivesPressed(s32 pad)
{
    if (pad < 0 || pad >= PORT_PADS) {
        return 0;
    }
    if (!portJoyPortTake(pad, PORTACT_WATCH_OBJECTIVES)) {
        return 0;
    }
    if (joyPortDbg()) {
        portConLogf("ge: joyport: pad %d watch_objectives -> START + Briefing (objectives) page / close\n", pad);
    }
    return 1;
}

/** lv.c: after bond_interact_object() found nothing - skip the reload
 *  fallback when the tap was the Port use key (consumes the flag). */
s32 portJoyPortUseNoReload(void)
{
    s32 r;
    s32 pad = joyPortPad();
    if (pad < 0) {
        return 0;
    }
    r = sUseNoReload[pad];
    sUseNoReload[pad] = 0;
    return r;
}

/**
 * bondviewProcessInput: crouch step this tick: -1 = go down (squat), +1 = go up
 * (stand), 0 = nothing. A toggle press latches "crouch wanted"; the hold key
 * wants it while held.
 *
 * The Port crouch is a LEVEL, pushed down EVERY tick while wanted. In aim mode
 * the game sets moveData.crouchUp every tick that C-down/D-down is not held, so
 * a one-tick crouchDown would be stood straight back up. If the game resets the
 * crouch under us (death, tank, level change) the toggle latch is dropped so
 * Bond does not respawn crouched.
 *
 * The stand-up is a level too: a stand is owed until crouchpos actually reads
 * CROUCH_STAND, so it survives ticks where the movement block did not run,
 * another source asserted crouchDown (the game tests that first), or controls
 * were not live. It ends by itself, since adjust(+2) clears the whole
 * SQUAT..STAND range in one go; crouching again cancels it.
 *
 * Why the vanilla stand-up can stop working: a keyboard player is control style
 * 1.1, where crouchUp is just "insightaimmode && weapon allows crouch" (the
 * D-pad/C-button term is always true with no D-pad bound), so aiming stands
 * Bond up. That fails only if this function returns -1, because the caller then
 * overwrites crouchUp with crouchDown. -1 is asserted every tick while the
 * toggle latch is set, and only another crouch_toggle press cleared it, which
 * the shipped keyboard binds lack (crouch_hold is Left Ctrl; the pad has the
 * toggle on left-stick click, and with p1_hybrid both drive player 1). So the
 * hold key's press now clears the latch.
 *
 * Reasoned from the code, not reproduced. GE_JOYPORTDBG=1 logs every crouch
 * transition, including the "cleared the toggle latch" line.
 */
typedef struct {
    s32 wanted;       /* toggle latch */
    s32 holdWas;      /* hold key was down on the previous tick */
    s32 standPending; /* stand-up owed (toggle off / hold release) */
    s32 wasSquat;     /* we have seen crouchpos == SQUAT while active */
    s32 holdEaten;    /* this hold press was spent clearing the latch, not crouching */
} JoyPortCrouch;
static JoyPortCrouch sCrouch[PORT_PADS];

s32 portJoyPortCrouchStep(void)
{
    JoyPortCrouch *c;
    s32 toggle;
    s32 hold;
    s32 active;
    s32 step = 0;
    s32 pad = joyPortPad();

    if (pad < 0) {
        return 0;
    }
    c = &sCrouch[pad];
    toggle = portJoyPortTake(pad, PORTACT_CROUCH_TOGGLE);
    hold = sHeld[pad][PORTACT_CROUCH_HOLD];
    if (g_PlayerIsInTank) {
        c->wanted = 0;
        c->wasSquat = 0;
        c->holdWas = hold;
        return 0;
    }
    if (toggle && joyPortPermitted()) {
        c->wanted = !c->wanted;
        if (joyPortDbg()) {
            portConLogf("ge: joyport: pad %d crouch toggle -> %s (crouchpos %d)\n", pad, c->wanted ? "CROUCH" : "STAND",
                    g_CurrentPlayer->crouchpos);
        }
        if (!c->wanted) {
            c->standPending = 1;
        }
    }
    /* The hold key's press clears the toggle latch and asks for a stand, and
     * that press is eaten so keeping the key down does not immediately
     * re-crouch. Without this, a latch set by a stray pad stick-click could not
     * be undone from the keyboard, and the every-tick crouchDown kept
     * overriding the vanilla hold-aim stand-up (see the header above). */
    if (hold && !c->holdWas && c->wanted) {
        c->wanted = 0;
        c->standPending = 1;
        c->holdEaten = 1;
        if (joyPortDbg()) {
            portConLogf("ge: joyport: pad %d crouch key cleared the toggle latch -> STAND\n", pad);
        }
    }
    if (!hold) {
        c->holdEaten = 0;
    }
    if (c->holdEaten) {
        hold = 0;
    }
    if (c->holdWas && !hold && !c->wanted) {
        c->standPending = 1; /* release edge */
    }
    c->holdWas = hold;
    active = c->wanted || hold;

    /* the game reset the crouch while we held it down: drop the latch */
    if (active && c->wasSquat && g_CurrentPlayer->crouchpos == CROUCH_STAND && !hold) {
        if (joyPortDbg()) {
            portConLogf("ge: joyport: crouch latch dropped (game reset crouchpos)\n");
        }
        c->wanted = 0;
        active = 0;
        c->standPending = 0;
    }
    if (!active) {
        c->wasSquat = 0;
    }
    if (!joyPortPermitted()) {
        /* The tilde console blanks the buttons but leaves Bond's movement block
         * running, so a Toggle aim's auto stand-up would get through and the
         * check above would drop the latch as a game
         * reset. Keep holding him down while it is open. */
        extern s32 portConIsOpen(void);

        if (active && portConIsOpen()) {
            return -1;
        }
        return 0;
    }
    if (active) {
        c->standPending = 0;
        if (g_CurrentPlayer->crouchpos == CROUCH_SQUAT) {
            c->wasSquat = 1;
        }
        step = -1; /* every tick: beats the aim-mode auto stand-up */
        if (joyPortDbg()) {
            static u32 t;
            if ((++t % 120u) == 0u) {
                portConLogf("ge: joyport: crouch held down (crouchpos %d, aim=%d)\n",
                        g_CurrentPlayer->crouchpos, g_CurrentPlayer->insightaimmode);
            }
        }
    } else if (c->standPending) {
        if (g_CurrentPlayer->crouchpos != CROUCH_STAND) {
            step = 1; /* EVERY tick until he is actually up - see the header */
            if (joyPortDbg()) {
                portConLogf("ge: joyport: crouch UP owed (crouchpos %d)\n", g_CurrentPlayer->crouchpos);
            }
        } else {
            c->standPending = 0; /* he is up: the debt is paid */
            if (joyPortDbg()) {
                portConLogf("ge: joyport: crouch UP done\n");
            }
        }
    }
    return step;
}

/**
 * bondviewProcessInput: an N64 pad stood Bond up (aim + C-up / D-up) while a
 * keyboard crouch was latched. Drops the toggle latch and owes a stand, the
 * same as a second crouch_toggle press. A held crouch_hold key still wins:
 * returns 1 only when nothing keeps him down.
 */
s32 portJoyPortCrouchRelease(void)
{
    JoyPortCrouch *c;
    s32 pad = joyPortPad();

    if (pad < 0) {
        return 0;
    }
    c = &sCrouch[pad];
    if (c->wanted) {
        c->wanted = 0;
        c->standPending = 1;
        if (joyPortDbg()) {
            portConLogf("ge: joyport: pad %d N64 stand-up cleared the toggle latch -> STAND\n", pad);
        }
    }
    return !sHeld[pad][PORTACT_CROUCH_HOLD];
}

/**
 * Inventory scroll whitelist: the items prev/next inventory step through. Only
 * items that do something in the right hand are listed (keys, documents, tapes
 * and beta leftovers are skipped); the rest are still on the watch's Inventory
 * page, they just take no scroll step.
 *   - placed / thrown by the hand (gunfire.c): bomb case, plastique, bug, micro
 *   camera, GoldenEye key
 *   - "use on the prop in front" (gunfire.c GUN_ANIM_STATE_USE_ITEM): door
 *   decoder, bomb defuser, data thief,
 *     explosive floppy, DAT tape
 *   - used in place: key analyser case, weapon case, camera, watch magnet (attract)
 * The order here is irrelevant (the scroll follows the watch list order).
 */
static const s16 sInvScrollItems[] = {
    ITEM_BOMBCASE,
    ITEM_PLASTIQUE,
    ITEM_BUG,
    ITEM_MICROCAMERA,
    ITEM_GOLDENEYEKEY,
    ITEM_DOORDECODER,
    ITEM_BOMBDEFUSER,
    ITEM_DATATHIEF,
    ITEM_EXPLOSIVEFLOPPY,
    ITEM_DATTAPE,
    ITEM_KEYANALYSERCASE,
    ITEM_WEAPONCASE,
    ITEM_CAMERA,
    ITEM_WATCHMAGNETATTRACT,
};

/** 1 = item id takes an inventory-scroll step (and shows its name on the HUD). */
s32 portJoyPortInvScrollItem(s32 id)
{
    size_t i;
    for (i = 0; i < sizeof(sInvScrollItems) / sizeof(sInvScrollItems[0]); i++) {
        if (sInvScrollItems[i] == id) {
            return 1;
        }
    }
    return 0;
}

/**
 * gunfire.c generate_ammo_total_microcode: the name to draw in the left-hand
 * ammo slot, or NULL. The name sits where the dual-wield ammo counter goes for
 * as long as a whitelisted item is in the right hand, however it was equipped
 * (scroll keys or the watch page). It is the short watch name
 * (bondinvGetNameByIndex), long name as fallback, with EXACTLY ONE trailing
 * newline: textMeasure only accumulates height on '\n', so without it the name
 * measures 0 high and HUDVALIGN_MIDDLE draws it half a line under the ammo
 * digits (format "%d\n"). Resolved every frame, nothing cached across loads,
 * since text overrides are per-level state.
 */
const char *portJoyPortGadgetHudName(void)
{
    extern u16 *bondinvGetNameByIndex(s32 index);
    extern u16 *bondinvGetLongNameByIndex(s32 index);
    static char sName[64];
    s32 right = getCurrentPlayerWeaponId(GUNRIGHT);
    s32 n, idx;

    if (!portJoyPortInvScrollItem(right)) {
        return NULL;
    }
    n = bondinvCountTotalItemsInInv();
    for (idx = 0; idx < n; idx++) {
        if (bondinvGetTextbyInvIndex(idx) == right) {
            const char *name = (const char *) bondinvGetNameByIndex(idx);
            size_t len;
            if (name == NULL || name[0] == '\0') {
                name = (const char *) bondinvGetLongNameByIndex(idx);
            }
            if (name == NULL || name[0] == '\0') {
                return NULL;
            }
            len = strlen(name);
            if (len >= sizeof(sName) - 1) {
                len = sizeof(sName) - 2;
            }
            memcpy(sName, name, len);
            sName[len] = '\0';
            while (len > 0 && (sName[len - 1] == '\n' || sName[len - 1] == '\r')) {
                sName[--len] = '\0';
            }
            if (len == 0) {
                return NULL;
            }
            sName[len++] = '\n'; /* one line high for textMeasure, like the digits */
            sName[len] = '\0';
            return sName;
        }
    }
    return NULL;
}

/**
 * Next/previous whitelisted inventory item from the equipped watch index.
 * Walk direction dir (+1/-1) through the watch list (wrapping), first entry
 * in sInvScrollItems wins; nothing happens with no such item.
 */
static void joyPortCycleInventory(s32 dir)
{
    s32 n = bondinvCountTotalItemsInInv();
    s32 cur = bondinvGetCurEquippedItem();
    s32 k;

    if (n <= 0) {
        return;
    }
    if (cur < 0 || cur >= n) {
        cur = (dir > 0) ? n - 1 : 0; /* unknown: start from the edge so the first step lands on index 0 / n-1 */
    }
    for (k = 1; k <= n; k++) {
        s32 idx = ((cur + dir * k) % n + n) % n;
        s32 id = bondinvGetTextbyInvIndex(idx);
        if (portJoyPortInvScrollItem(id)) {
            if (joyPortDbg()) {
                portConLogf("ge: joyport: inventory %s -> watch index %d item %d (was index %d, right=%d)\n",
                        dir > 0 ? "NEXT" : "PREV", idx, id, cur, getCurrentPlayerWeaponId(GUNRIGHT));
            }
            if (getCurrentPlayerWeaponId(GUNRIGHT) == id && idx == cur) {
                return;
            }
            /* Animated switch (lower / swap / raise) exactly like the A-tap
             * cycle, because the watch's instant hard-set does not lower the
             * weapon. The swap records previous_weapon, so the A / prev / next
             * cycle snaps back to the last real gun from any gadget (gun.c
             * advance_/backstep_through_inventory); the item name is drawn by
             * generate_ammo_total_microcode (portJoyPortGadgetHudName) while it
             * is held. */
            {
                extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);
                gunRequestHandWeaponChange(GUNRIGHT, id, dir);
                gunRequestHandWeaponChange(GUNLEFT, ITEM_UNARMED, dir);
            }
            return;
        }
    }
    if (joyPortDbg()) {
        portConLogf("ge: joyport: inventory %s: no non-gun item (%d entries)\n", dir > 0 ? "NEXT" : "PREV", n);
    }
}

/**
 * lv.c per-player render tick, right after the vanilla "B: use, else
 * reload" block - reload / inventory cycling live here because that is
 * where the game itself runs the reloads (use is a btap, see above).
 */
/**
 * Gadget equip cue: plays the mine-placing sound at double speed whenever a gadget is equipped.
 *
 * In this enum the sound is ATTACH_MINE_SFX = 241 = 0xF1; 0xF0 is
 * HIT_BULLET_METAL_A4_SFX. (Entry 0 of the bank is NOTHING_SFX, a reserved slot
 * sndPlaySfx rejects, so a tool listing only real sounds sits one below this
 * table.)
 *
 * It fires off the equipped item rather than the equip paths: the right hand
 * becoming a gadget is the one fact shared by the Port prev/next binds, the
 * watch inventory page and the vanilla A-tap cycle. It is per player, because
 * split-screen runs this once per player per frame.
 */
static void joyPortGadgetCueTick(void)
{
    extern void *portSndPlaySfxPitched(void *bank, s16 id, f32 pitch); /* snd.c */
    extern void *g_musicSfxBufferPtr;                                  /* music.h ALBank * */
    static s16 lastitem[PORT_PADS];
    s32 p = get_cur_playernum();
    s32 right;

    if (p < 0 || p >= PORT_PADS) {
        return;
    }
    right = getCurrentPlayerWeaponId(GUNRIGHT);
    if ((s16) right == lastitem[p]) {
        return;
    }
    lastitem[p] = (s16) right;
    if (portJoyPortInvScrollItem(right)) {
        void *st = portSndPlaySfxPitched(g_musicSfxBufferPtr, ATTACH_MINE_SFX, 2.0f);

        if (joyPortDbg()) {
            portConLogf("ge: joyport: gadget cue - sfx %d ATTACH_MINE x2 speed on item %d, state %s\n",
                    ATTACH_MINE_SFX, right, st ? "ok" : "NULL");
        }
    }
}

void portJoyPortPlayerTick(void)
{
    s32 reload, previnv, nextinv;
    s32 pad = joyPortPad();

    /* Before the pad and permission gates: equipping is a game-state edge, not
     * a bind, so the cue must not depend on which pad this player has or on the
     * controls being live this tick. */
    joyPortGadgetCueTick();

    if (pad < 0) {
        return;
    }
    /* permission FIRST (like use): while a cutscene / menu is up these
     * latches belong to the skip / menu-back paths; unclaimed presses
     * expire by themselves */
    if (!joyPortPermitted()) {
        return;
    }
    joyPortDrop(pad, PORTACT_MENU_BACK); /* menu-only: drop banked presses */
    reload = portJoyPortTake(pad, PORTACT_RELOAD);
    previnv = portJoyPortTake(pad, PORTACT_PREV_INV);
    nextinv = portJoyPortTake(pad, PORTACT_NEXT_INV);
    if (g_PlayerIsInTank) {
        /* Only the tank's own gun has nothing to reload (its "reload" animation
         * would run on the tank hand); any other weapon in hand reloads as on
         * foot. Inventory cycling stays out of the tank. */
        if (reload && getCurrentPlayerWeaponId(GUNRIGHT) != ITEM_TANKSHELLS) {
            if (joyPortDbg()) {
                portConLogf("ge: joyport: reload in tank (right=%d)\n", getCurrentPlayerWeaponId(GUNRIGHT));
            }
            attempt_reload_item_in_hand(GUNRIGHT);
            attempt_reload_item_in_hand(GUNLEFT);
        }
        return;
    }
    if (joyPortDbg()) { /* hands state once a second: what is really held */
        static u32 t;
        if ((++t % 60u) == 0u) {
            portConLogf("ge: joyport: hands right=%d (state %d next %d prev %d) left=%d (state %d next %d prev %d) equipidx=%d\n",
                    g_CurrentPlayer->hands[GUNRIGHT].weaponnum, g_CurrentPlayer->hands[GUNRIGHT].weapon_action_state,
                    g_CurrentPlayer->hands[GUNRIGHT].weapon_next_weapon, g_CurrentPlayer->hands[GUNRIGHT].previous_weapon,
                    g_CurrentPlayer->hands[GUNLEFT].weaponnum, g_CurrentPlayer->hands[GUNLEFT].weapon_action_state,
                    g_CurrentPlayer->hands[GUNLEFT].weapon_next_weapon, g_CurrentPlayer->hands[GUNLEFT].previous_weapon,
                    bondinvGetCurEquippedItem());
        }
    }
    if (reload) {
        if (joyPortDbg()) {
            portConLogf("ge: joyport: reload (right=%d anim=%d)\n", getCurrentPlayerWeaponId(GUNRIGHT),
                    g_CurrentPlayer->hands[GUNRIGHT].weapon_current_animation);
        }
        attempt_reload_item_in_hand(GUNRIGHT);
        attempt_reload_item_in_hand(GUNLEFT);
    }
    if (joyPortChordBack(pad, PORTACT_NEXT_INV)) {
        sShootTap[pad] = 0;   /* one step per tap */
        previnv = 1;
    }
    if (previnv) {
        joyPortCycleInventory(-1);
    }
    if (nextinv) {
        joyPortCycleInventory(1);
    }
}

/* ---- twin stick ----------------------------------------------------------- */
/*
 * Twin stick. GE's 2.x control styles read a SECOND N64 pad (index playernum +
 * playerCount, bondview2.c bondviewProcessInput) for the other stick and its Z,
 * which caps 2.x at two players with two physical pads each. These hooks hand
 * the game the player's OWN controller's right stick instead (input_pc.c
 * portInputPadTwinStick: SDL game controllers, DInput axes 2/3, launcher custom
 * maps), with LT as the second pad's Z, so all four players can use 2.x with
 * one controller each. Pads without a right stick (raphnet N64 adapters, empty
 * slots) report "not capable" and the game takes its vanilla second-pad path.
 *
 * Harness: GE_TWINSTICK_AUTO="x,y[,style]" fakes a right stick for pad 0 (N64
 * units) and, with the optional style (4..7 = 2.1..2.4), forces that control
 * style at the hook - headless proof without a controller.
 */
#ifdef GE_VIDEO
extern s32 portInputPadTwinStickCapable(s32 idx);
extern s32 portInputPadTwinStick(s32 idx, s8 *x, s8 *y, s32 muteC, s32 lookOnPad1);
#endif

static s32 sTwinAuto = -1;      /* -1 unparsed, 0 off, 1 on */
static s32 sTwinAutoX, sTwinAutoY, sTwinAutoStyle = -1;
static s32 sTwinAutoPads = 1;   /* GE_TWINSTICK_AUTO_PADS=N: fake it on pads 0..N-1 (MP proof) */

static void joyPortTwinAutoParse(void)
{
    const char *e = getenv("GE_TWINSTICK_AUTO");
    const char *p = getenv("GE_TWINSTICK_AUTO_PADS");
    sTwinAuto = 0;
    if (p != NULL) {
        sTwinAutoPads = atoi(p);
        if (sTwinAutoPads < 1) sTwinAutoPads = 1;
        if (sTwinAutoPads > PORT_PADS) sTwinAutoPads = PORT_PADS;
    }
    if (e != NULL && *e != '\0') {
        sTwinAuto = 1;
        sTwinAutoX = atoi(e);
        while (*e != '\0' && *e != ',') e++;
        if (*e == ',') {
            sTwinAutoY = atoi(++e);
            while (*e != '\0' && *e != ',') e++;
            if (*e == ',') {
                sTwinAutoStyle = atoi(++e);
            }
        }
        portConLogf("ge: joyport: GE_TWINSTICK_AUTO pad0 right=(%d,%d) style=%d\n",
                sTwinAutoX, sTwinAutoY, sTwinAutoStyle);
    }
}

/** 1 = pad has a right stick to twin-stick with (menus: 2.x allowed for that player). */
s32 portJoyTwinStickCapable(s32 pad)
{
    if (sTwinAuto < 0) {
        joyPortTwinAutoParse();
    }
    if (sTwinAuto && pad >= 0 && pad < sTwinAutoPads) {
        return 1;
    }
#ifdef GE_VIDEO
    return portInputPadTwinStickCapable(pad);
#else
    return 0;
#endif
}

/**
 * bondview2.c 2.x block: the second pad's stick for player `pad` from its own
 * controller. muteC = controls live (the right stick must not also fire C
 * buttons while it aims; in the watch it must). 0 = not capable, take the
 * vanilla second-pad read.
 *
 * Move / Look: the launcher rows "Move stick" (padN_stick_x/y) and "Look stick"
 * (padN_look_x/y) default to left / right, and the control STYLE decides which
 * N64 pad each one feeds. Styles 2.1 / 2.3 read pad 1 as move and the second
 * pad as look; 2.2 / 2.4 are the reverse (pad 1 = look, second pad = move, the
 * modern layout), so lookOnPad1 = 1 tells input_pc.c to feed pad 1 from the
 * Look stick and hand the Move stick back here as the second pad, while
 * controls are live only (menus and the watch keep Move on pad 1).
 */
s32 portJoyTwinStickRead(s32 pad, s8 *x, s8 *y, s32 muteC, s32 lookOnPad1)
{
    s32 ok = 0;
    if (sTwinAuto < 0) {
        joyPortTwinAutoParse();
    }
    if (sTwinAuto && pad >= 0 && pad < sTwinAutoPads) {
        *x = (s8) sTwinAutoX;
        *y = (s8) sTwinAutoY;
        ok = 1;
    }
#ifdef GE_VIDEO
    else {
        ok = portInputPadTwinStick(pad, x, y, muteC, lookOnPad1);
    }
#endif
    if (ok && joyPortDbg()) {
        static u32 t;
        if ((++t % 60u) == 0u) {
            portConLogf("ge: joyport: twin stick pad %d second=(%d,%d) muteC=%d lookOnPad1=%d\n",
                    pad, *x, *y, muteC, lookOnPad1);
        }
    }
    return ok;
}

/** Harness only: the control style GE_TWINSTICK_AUTO asked for, or -1. */
s32 portJoyTwinStickAutoStyle(void)
{
    if (sTwinAuto < 0) {
        joyPortTwinAutoParse();
    }
    return sTwinAuto ? sTwinAutoStyle : -1;
}

#endif /* !TARGET_N64 */
