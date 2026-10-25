/**
 * PC libultra shim — docs/port/phase2-decisions.md D8.
 *
 * Single-threaded replacement for every os- and __os-class symbol the PC link still
 * needs (port_sweep/link_surface.txt). Design contract:
 *
 *  - Message queues are REAL bounded FIFOs operating on the game's own
 *    OSMesgQueue structs (include/PR/os.h layout — never redefined here).
 *    Non-blocking send/recv return -1 on full/empty and recv leaves *msg
 *    untouched, which is what boss.c's `while (osRecvMesg(NOBLOCK) == 0)`
 *    drain and joy.c's inverted-polarity polls depend on.
 *  - OS_MESG_BLOCK does not block: it calls portPumpEvents() (weak no-op
 *    here; the fake scheduler provides the real pump) a bounded number of
 *    times, re-checking the queue after each, then gives up with -1.
 *  - The shim MANUFACTURES the completion messages hardware used to post:
 *    osPiStartDma, osContStartReadData/Query, osSetTimer. That keeps GE's
 *    retrace-driven boss.c main loop working essentially unmodified.
 *  - Threads are state-only (nothing ever runs them); VI/SP/DP/cache/TLB
 *    calls record state or do nothing.
 *
 * Status: compile/link-surface verified; runtime behavior lands with the
 * fake scheduler (implementation order item 4).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h> /* stat / mkdir: Data/Saves for eeprom.bin */
#ifdef _WIN32
#include <direct.h>   /* _mkdir */
#endif
/* No <stdarg.h> here: the repo's include/stdarg.h wrapper defines its guard
 * during glibc stdio.h's __need___va_list probe, so a later full include is
 * skipped and va_start never appears. osSyncPrintf uses __builtin_va_* -
 * exactly what gcc's stdarg.h macros expand to. */

#include <ultra64.h>
#include "libultra/io/controller.h" /* OSPifRam + the __osCont/__osSi internal API */
#include "port_libultra.h"
#include "port_audio_stats.h"
#include "game/file.h"  /* save_data / smallSave: EEPROM import from big-endian saves */

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console ring (spec section 7). */
extern void portConLogf(const char *fmt, ...);

/* ==== data symbols the game links against ================================ */

u64 osClockRate = OS_CLOCK_RATE; /* 62.5 MHz bus clock, os.h macros divide by it */
u32 osTvType = OS_TV_NTSC;
u8 __osContLastCmd = CONT_CMD_RESET; /* "last PIF command" - anything != REQUEST_STATUS */
OSPifRam __osPfsPifRam;              /* PIF-ram mirror; stays zeroed on PC */

/* ==== event pump hook ==================================================== */

/* How many times a BLOCK recv/send re-pumps before giving up. One pump emits
 * (at most) one retrace on the scheduler side; 3 gives slack for chained
 * completions without risking an infinite stall. */
#define PORT_MQ_PUMP_TRIES 3

/**
 * Weak default: no events to pump until the fake scheduler links its strong
 * definition. With the no-op, BLOCK recv on an empty queue returns -1
 * immediately — honest "nothing will ever arrive" for the phase-2 scaffold.
 */
/* no window, no message box: a headless build has said it on stderr already */
__attribute__((weak)) void portShowErrorBox(const char *title, const char *text)
{
    (void) title;
    (void) text;
}

__attribute__((weak)) void portPumpEvents(void)
{
}

/**
 * Weak defaults for the game's widescreen hooks (headless / no video):
 * factor 1.0 = authentic 4:3, scene flag ignored. video.c overrides both.
 */
__attribute__((weak)) f32 portWideFactor(void)
{
    return 1.0f;
}

__attribute__((weak)) void portSetSceneWide(s32 wide)
{
    (void)wide;
}

__attribute__((weak)) void portSetSceneMp(s32 mp)
{
    (void)mp;
}

__attribute__((weak)) int portHitchMs(void)   /* hitchlog.c: renderer builds only */
{
    return 0;
}

__attribute__((weak)) void portHitchSetMs(int ms)
{
    (void) ms;
}

__attribute__((weak)) s32 portInputPadLookYP1(void)   /* input_pc.c: no pads headless */
{
    return 0;
}

__attribute__((weak)) s32 portWatchStatic(void)   /* video.c: [display] watch_static, vanilla on */
{
    return 1;
}

__attribute__((weak)) s32 portFastPause(void)   /* video.c: [display] fast_pause, vanilla off */
{
    return 0;
}

__attribute__((weak)) s32 portArmorHud(void)   /* video.c: [display] armor_hud, vanilla off */
{
    return 0;
}

__attribute__((weak)) s32 portWeaponFov(void)   /* video.c: [display] weapon_fov, vanilla off (Match View) */
{
    return 0;
}

__attribute__((weak)) void portConsoleSpace(s32 *w, s32 *h)   /* video.c: the console's 2D space */
{
    *w = 640;
    *h = 480;
}

__attribute__((weak)) void portSetScenePillar(s32 on)   /* video.c: the MP pillarbox frame flag */
{
    (void) on;
}

__attribute__((weak)) f32 portFrameWideFactor(void)
{
    return 1.0f;
}

/** Weak default: no per-viewport MP pin headless (quadrants keep the old
 *  anamorphic/identity aim math); video.c overrides. */
__attribute__((weak)) s32 portMpPinActive(void)
{
    return 0;
}

/** Weak default: no stick X/Y overlay headless; input_pc.c overrides. */
__attribute__((weak)) s32 portInputShowInput(void)
{
    return 0;
}

/** Weak default: no mouse headless; input_pc.c overrides (per-tick transfer
 *  of the accumulated poll motion - joy.c joyConsumeSamplesWrapper). */
__attribute__((weak)) void portInputMouseTickConsume(void)
{
}

/** Weak default: pak on every pad headless; input_pc.c overrides per player. */
__attribute__((weak)) s32 portInputPakPresent(s32 slot)
{
    (void) slot;
    return 1;
}

/** Weak default: retail LODs headless; video.c overrides from midas.ini. */
__attribute__((weak)) s32 portHighLod(void)
{
    return 0;
}

/** Weak default: no FPS digits headless; video.c overrides. */
__attribute__((weak)) s32 portVideoShowFps(void)
{
    return 0;
}

/** Weak defaults: no audio pump overlay headless; audio_pc.c overrides. The
 *  overlay draw lives in bondview2.c, so any headless target that links that
 *  file without audio_pc.c resolves here instead of failing to link. */
__attribute__((weak)) s32 portAudioPumpStatsShown(void)
{
    return 0;
}

__attribute__((weak)) void portAudioPumpStatsSet(s32 on)
{
    (void) on;
}

__attribute__((weak)) void portAudioPumpStatsGet(struct PortAudioPumpStats *out)
{
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out)); /* nothing measured: zeroes, not junk */
    }
}

/** Weak default: nothing extra unlocked headless; video.c overrides from midas.ini. */
__attribute__((weak)) s32 portUnlockAll(void)
{
    return 0;
}

/** Weak default: pause pillarbox on (the shipped look); video.c overrides. */
/** Weak default: no mouse headless, so Auto-Aim is never blocked; video.c overrides. */
__attribute__((weak)) s32 portMouseBlocksAutoAim(s32 player)
{
    (void) player;
    return 0;
}

/** Weak defaults: every Classic Mode part on headless, as with no ini; video.c overrides. */
__attribute__((weak)) s32 portClassicPart(s32 i)
{
    (void) i;
    return 1;
}

__attribute__((weak)) s32 portClassicHitches(void)
{
    return 1;
}

/** Weak default: no classic pillars headless; video.c overrides. */
__attribute__((weak)) s32 portClassicPillars(void)
{
    return 0;
}

__attribute__((weak)) s32 portPausePillars(void)
{
    return 1;
}

/** Weak default: N64-size HUD headless (no window to measure); video.c overrides. */
__attribute__((weak)) s32 portHudScaleActive(void)
{
    return 0;
}

/** Weak default: no always-on crosshair (vanilla look); video.c overrides. */
__attribute__((weak)) s32 portAlwaysCrosshair(void)
{
    return 0;
}

/** Weak default: mission timer on (the port default); video.c overrides. */
__attribute__((weak)) s32 portMissionTimer(void)
{
    return 1;
}

/** Weak default: drop-shadow strength 100% of vanilla (headless); video.c overrides. */
__attribute__((weak)) s32 portShadowAlphaPct(void)
{
    return 100;
}

/** Weak default: stereo panning of positional sfx on (the port default);
 * video.c overrides. Headless builds still evaluate it - the null audio
 * path just never renders the pan. */
__attribute__((weak)) s32 portStereoPan(void)
{
    return 1;
}

/** Weak default: no aim lock (vanilla aiming); video.c overrides. */
__attribute__((weak)) s32 portAimLock(void)
{
    return 0;
}

/** Weak defaults: multiplayer's own aim lock off, Rare's MP "Aim" row on its
 * default (3 = sight on, auto-aim on); video.c overrides both. */
__attribute__((weak)) s32 portMpAimLock(void)
{
    return 0;
}

__attribute__((weak)) s32 portMpSightAdjust(void)
{
    return 3;
}

/** Weak default: no keyboard, so no key is holding the stick; video.c overrides. */
__attribute__((weak)) s32 portVideoKbdStickDown(void)
{
    return 0;
}

/** Weak default: no player names in the viewports; video.c overrides. */
__attribute__((weak)) s32 portMpShowNames(void)
{
    return 0;
}

/** Weak defaults: the vanilla MP spawn (no weapon, pads in turn); video.c overrides. */
__attribute__((weak)) s32 portMpSpawnWeapon(void)
{
    return 0;
}

__attribute__((weak)) s32 portMpRandomSpawns(void)
{
    return 0;
}

/** Weak default: armour pickups kept; video.c overrides. */
__attribute__((weak)) s32 portMpNoArmorActive(void)
{
    return 0;
}

/** Weak default: LTK Health off; video.c overrides. */
__attribute__((weak)) s32 portMpLtkHealthActive(void)
{
    return 0;
}

/** Weak default: friendly fire on; video.c overrides. */
__attribute__((weak)) s32 portMpFriendlyFireOff(void)
{
    return 0;
}

/** Weak default: Rare's six live explosions; video.c overrides. */
__attribute__((weak)) s32 portExplosionCap(void)
{
    return 6;
}

/** Weak default: head roll kept (vanilla camera); video.c overrides. */
__attribute__((weak)) s32 portNoHeadRoll(void)
{
    return 0;
}

/** Weak default: P1's START ends the MP match (the port default); video.c overrides. */
__attribute__((weak)) s32 portMpP1EndsMatch(void)
{
    return 1;
}

/** Weak defaults: no live option bridge headless (port_options.c rows read
 * "n/a"); video.c overrides for the [display] keys. */
__attribute__((weak)) s32 portVideoOptionGet(const char *key, s32 *out)
{
    (void) key;
    (void) out;
    return 0;
}

__attribute__((weak)) s32 portVideoOptionSet(const char *key, s32 value)
{
    (void) key;
    (void) value;
    return 0;
}

__attribute__((weak)) s32 portInputOptionGet(const char *key, s32 *out)
{
    (void) key;
    (void) out;
    return 0;
}

__attribute__((weak)) s32 portInputOptionSet(const char *key, s32 value)
{
    (void) key;
    (void) value;
    return 0;
}

extern s32 portConIsOpen(void); /* port_console.c - always linked */

/** Weak defaults: no SDL headless, so there are no input devices to pick
 * between - the watch's Controller page offers mouse and keyboard only and
 * every bind reads "n/a". input_pc.c overrides all five. */
__attribute__((weak)) s32 portInputDeviceCount(void)
{
    return 1;
}

__attribute__((weak)) const char *portInputDeviceLabel(s32 dev)
{
    (void) dev;
    return "Mouse and Keyboard\n";
}

__attribute__((weak)) s32 portInputDeviceIsRaphnet(s32 dev)
{
    (void) dev;
    return 0;
}

__attribute__((weak)) s32 portInputDeviceBindText(s32 dev, const char *name, char *buf, s32 len)
{
    (void) dev;
    (void) name;
    (void) buf;
    (void) len;
    return 0;
}

__attribute__((weak)) s32 portInputDeviceIniKey(s32 dev, const char *name, char *buf, s32 len)
{
    (void) dev;
    (void) name;
    (void) buf;
    (void) len;
    return 0;
}

/** Weak default: no keyboard headless, so Escape is never pressed;
 * input_pc.c overrides. Read by front.c (menu back) and bondview2.c (watch). */
__attribute__((weak)) s32 portEscPressed(void)
{
    return 0;
}

/** Weak default: no keyboard headless, so the classic mode key never fires;
 * video.c overrides. Read by bondview2.c. */
__attribute__((weak)) s32 portVideoClassicTogglePressed(void)
{
    return 0;
}

/** Weak defaults: no keyboard headless, so Return never holds START; video.c
 * overrides. Consumed by joy.c, read by the solo watch toggles (spec 6). */
__attribute__((weak)) void portReturnStartTickConsume(void)
{
}

__attribute__((weak)) s32 portReturnStartThisTick(void)
{
    return 0;
}

/** Weak defaults: no midas.ini key bridge headless (the console's `set`/`get`
 * for the bind half of the key space); video.c overrides. */
__attribute__((weak)) s32 portVideoIniSetKey(const char *key, const char *value)
{
    (void) key;
    (void) value;
    return 0;
}

__attribute__((weak)) s32 portVideoIniGetKey(const char *key, char *buf, s32 n)
{
    (void) key;
    (void) buf;
    (void) n;
    return 0;
}

/** Weak defaults for the menu overhaul's video.c half when running headless: no
  * window or resolution list ("locked" is the row's no-choice state),
 * no keyboard or mouse (key capture never takes anything), and no midas.ini (every
 * setting reads video.c's compiled-in default). */
__attribute__((weak)) s32 portVideoResCount(void)
{
    return 0;
}

__attribute__((weak)) s32 portVideoResGet(s32 i, s32 *w, s32 *h)
{
    (void) i;
    (void) w;
    (void) h;
    return 0;
}

__attribute__((weak)) s32 portVideoResCurrent(void)
{
    return -1;
}

__attribute__((weak)) s32 portVideoResLocked(void)
{
    return 1;
}

__attribute__((weak)) s32 portVideoResApply(s32 i)
{
    (void) i;
    return 0;
}

__attribute__((weak)) void portVideoBindCaptureBegin(void)
{
}

__attribute__((weak)) void portVideoBindCaptureCancel(void)
{
}

__attribute__((weak)) s32 portVideoBindCaptureTake(char *out, s32 len)
{
    (void) out;
    (void) len;
    return 0;
}

__attribute__((weak)) s32 portVideoDeleteEdge(void)
{
    return 0;
}

__attribute__((weak)) void portBindCooldownStart(void)
{
}

__attribute__((weak)) s32 portMouseClickPressed(void)
{
    return 0;
}

__attribute__((weak)) s32 portMouseMenuPos(f32 *gx, f32 *gy, f32 left, f32 top, f32 width, f32 height)
{
    (void) gx;
    (void) gy;
    (void) left;
    (void) top;
    (void) width;
    (void) height;
    return 0;
}

__attribute__((weak)) s32 portVideoLookDigital(s32 *x, s32 *y)
{
    if (x != NULL) *x = 0;
    if (y != NULL) *y = 0;
    return 0;
}

__attribute__((weak)) s32 portMpCheatSetting(s32 which)
{
    (void) which;
    return 0;
}

__attribute__((weak)) s32 portPlayerOptCfg(s32 which)
{
    (void) which;
    return -1;   /* no global set: the file's own values stand */
}

__attribute__((weak)) void portPlayerOptsAdopt(const s32 *vals, s32 fromload)
{
    (void) vals;
    (void) fromload;
}

__attribute__((weak)) s32 portVideoClassicSavedGet(s32 *v)
{
    (void) v;
    return 0;
}

__attribute__((weak)) void portVideoClassicSavedSet(const s32 *v)
{
    (void) v;
}

/** Weak defaults for input_pc.c's device, style, layout, capture and assign
  * calls, matching what input_pc.c gives a machine with no pads:
 * player 1 on the keyboard's empty slot, style auto, no layouts, and nothing to
 * capture or assign. */
__attribute__((weak)) s32 portInputPadIsN64(s32 slot)
{
    (void) slot;
    return 0;
}

__attribute__((weak)) s32 portInputSecondPadIndex(s32 playernum, s32 fam)
{
    (void) playernum;
    (void) fam;
    return -1;
}

__attribute__((weak)) s32 portInputDigitalMove(s32 slot, s32 *dx, s32 *dy)
{
    (void) slot;
    if (dx != NULL) *dx = 0;
    if (dy != NULL) *dy = 0;
    return 0;
}

__attribute__((weak)) s32 portInputStyleGet(s32 slot)
{
    (void) slot;
    return -1;
}

__attribute__((weak)) s32 portInputStyleRaw(s32 slot)
{
    (void) slot;
    return -1;
}

__attribute__((weak)) const char *portInputStyleName(s32 code)
{
    (void) code;
    return "auto";
}

__attribute__((weak)) s32 portInputStyleCode(const char *name)
{
    (void) name;
    return -1;
}

__attribute__((weak)) s32 portInputSlotDevice(s32 slot)
{
    (void) slot;
    return -1;
}

__attribute__((weak)) s32 portInputDeviceSlot(s32 dev)
{
    (void) dev;
    return -1;
}

__attribute__((weak)) s32 portInputDeviceStyleGet(s32 dev)
{
    (void) dev;
    return -1;
}

__attribute__((weak)) void portInputDeviceStyleSet(s32 dev, s32 style)
{
    (void) dev;
    (void) style;
}

__attribute__((weak)) s32 portInputDeviceModeIsN64(s32 dev)
{
    (void) dev;
    return 0;
}

__attribute__((weak)) void portInputDeviceModeSet(s32 dev, s32 isN64)
{
    (void) dev;
    (void) isN64;
}

__attribute__((weak)) s32 portInputP1ViewPad(void)
{
    return 0;
}

__attribute__((weak)) s32 portInputSlotAnyButtonDown(s32 slot)
{
    (void) slot;
    return 0;
}

__attribute__((weak)) s32 portInputAnyHeld(void)
{
    return 0;
}

__attribute__((weak)) s32 portVideoHfovWideTrueTenths(void)
{
    return 0;
}

__attribute__((weak)) s32 portInputLayoutCount(void)
{
    return 0;
}

__attribute__((weak)) const char *portInputLayoutName(s32 i)
{
    (void) i;
    return "";
}

__attribute__((weak)) s32 portInputLayoutIsUser(s32 i)
{
    (void) i;
    return 0;
}

__attribute__((weak)) s32 portInputLayoutApply(s32 dev, s32 i)
{
    (void) dev;
    (void) i;
    return 0;
}

__attribute__((weak)) s32 portInputLayoutFitsSlot(s32 slot, s32 i)
{
    (void) slot;
    (void) i;
    return 0;
}

__attribute__((weak)) const char *portInputLayoutActive(s32 dev)
{
    (void) dev;
    return "";
}

__attribute__((weak)) void portInputLayoutReconcileMode(s32 dev)
{
    (void) dev;
}

__attribute__((weak)) s32 portInputLayoutSaveCurrent(s32 dev, const char *name)
{
    (void) dev;
    (void) name;
    return 0;
}

__attribute__((weak)) s32 portInputLayoutDelete(s32 i)
{
    (void) i;
    return 0;
}

__attribute__((weak)) s32 portInputPadBindSet(s32 dev, const char *name, const char *token)
{
    (void) dev;
    (void) name;
    (void) token;
    return 0;
}

__attribute__((weak)) s32 portInputPadCaptureBegin(s32 dev)
{
    (void) dev;
    return 0;   /* "it has no device to listen to" */
}

__attribute__((weak)) s32 portInputPadCaptureTick(void)
{
    return 0;
}

__attribute__((weak)) s32 portInputPadCaptureTake(char *out, s32 len)
{
    if (out != NULL && len > 0) out[0] = '\0';
    return 0;
}

__attribute__((weak)) void portInputPadCaptureCancel(void)
{
}

__attribute__((weak)) s32 portInputPadCaptureHoldLong(void)
{
    return 0;
}

__attribute__((weak)) s32 portInputAssignBegin(s32 slot)
{
    (void) slot;
    return 0;
}

__attribute__((weak)) s32 portInputAssignBeginEx(s32 slot, s32 excludeMask)
{
    (void) slot;
    (void) excludeMask;
    return 0;
}

__attribute__((weak)) s32 portInputAssignActive(void)
{
    return 0;
}

__attribute__((weak)) s32 portInputAssignTick(void)
{
    return 0;
}

__attribute__((weak)) s32 portInputAssignTake(void)
{
    return -1;
}

__attribute__((weak)) void portInputAssignCancel(void)
{
}

/** Weak default: no midas.ini writer headless; video.c overrides. */
__attribute__((weak)) s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n)
{
    (void) section;
    (void) keys;
    (void) vals;
    (void) n;
    return 0;
}

/** Weak default for the midas.ini fov (GE_DEFAULT_FOV); video.c overrides. */
/* CROSSHAIR COLOUR: headless has no midas.ini, so the vanilla red. */
__attribute__((weak)) s32 portCrosshairColor(void)
{
    return -1;
}

/* GLOBAL VOLUMES: headless has no midas.ini, so full volume - the vanilla
 * behaviour these replace. options.c / front.c call them every menu init. */
__attribute__((weak)) s32 portMusicVolumeRaw(void)
{
    return 0x7FFF;
}

__attribute__((weak)) s32 portSfxVolumeRaw(void)
{
    return 0x7FFF;
}

__attribute__((weak)) void portAdoptSaveVolumes(s32 musicraw, s32 sfxraw, s32 fromload)
{
    (void) musicraw;
    (void) sfxraw;
    (void) fromload;
}

__attribute__((weak)) f32 portFovDefault(void)
{
    return 60.0f;
}

/* ==== time =============================================================== */

/*
 * Host monotonic microseconds scaled to the N64 counter rate (46.875 MHz =
 * OS_CPU_COUNTER). Same formula as PD's libultra.c:
 * count = us * (46875000/1000) / (1000000/1000) = us * 46875 / 1000.
 */
#ifdef _WIN32
/* Lean: only QueryPerformance* is used, and the full header pulls in
 * winsock.h, whose ntohl/ntohs prototypes are mangled by the macros of the
 * same name in bondconstants.h (via game/file.h above). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static u64 portHostMicroseconds(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (u64)((now.QuadPart * 1000000ull) / (u64)freq.QuadPart);
}

/* BSD string functions: provided by libc on Unix, implemented here for MinGW. */
void bcopy(const void *src, void *dst, size_t len) { memmove(dst, src, (size_t)len); }
void bzero(void *dst, size_t len) { memset(dst, 0, (size_t)len); }
#else
static u64 portHostMicroseconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000ull + (u64)ts.tv_nsec / 1000ull;
}
#endif

OSTime osGetTime(void)
{
    return (portHostMicroseconds() * 46875ull) / 1000ull;
}

u32 osGetCount(void)
{
    return (u32)osGetTime();
}

/* ==== threads (state-only; nothing runs them) ============================ */

void osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri)
{
    (void)entry; (void)arg; (void)sp;
    t->id = id;
    t->priority = pri;
    t->state = OS_STATE_STOPPED;
}

/* GE sometimes passes a THREAD ID where libultra takes a pointer
 * (init.c:274/277 pass RMON_THREAD_ID); on N64, small values reach
 * osSetThreadPri-family functions that treat NULL/current specially.
 * Guard against anything that isn't a plausible host pointer. */
#define PORT_THREAD_PTR_OK(t) ((uintptr_t)(t) >= 0x10000)

void osStartThread(OSThread *t)
{
    if (PORT_THREAD_PTR_OK(t)) t->state = OS_STATE_RUNNING;
}

void osStopThread(OSThread *t)
{
    if (PORT_THREAD_PTR_OK(t)) t->state = OS_STATE_STOPPED;
}

void osSetThreadPri(OSThread *t, OSPri pri)
{
    if (PORT_THREAD_PTR_OK(t)) t->priority = pri;
}

/* ==== message queues (real bounded FIFO, non-blocking core) ============== */

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msgBuf, s32 count)
{
    /* no blocked threads and no pending messages; the rest is zero */
    *mq = (OSMesgQueue){ .msgCount = count, .msg = msgBuf };
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    if (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_BLOCK) {
            s32 tries;
            for (tries = 0; tries < PORT_MQ_PUMP_TRIES && mq->validCount >= mq->msgCount; tries++) {
                portPumpEvents();
            }
        }
        if (mq->validCount >= mq->msgCount) {
            return -1;
        }
    }

    mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    mq->validCount++;
    return 0;
}

/**
 * Jam = enqueue at the FRONT (next recv gets it first).
 */
s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    if (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_BLOCK) {
            s32 tries;
            for (tries = 0; tries < PORT_MQ_PUMP_TRIES && mq->validCount >= mq->msgCount; tries++) {
                portPumpEvents();
            }
        }
        if (mq->validCount >= mq->msgCount) {
            return -1;
        }
    }

    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    return 0;
}

/**
 * On empty: NOBLOCK returns -1 with *msg UNTOUCHED (joy.c's polls and
 * boss.c:448's drain loop rely on both halves of that). BLOCK pumps host
 * events up to PORT_MQ_PUMP_TRIES times before giving up — this is what
 * backs boss.c:458's blocking recv single-threaded.
 * msg may be NULL (ramrom.c romReceiveMesg, joy.c joyCheckStatus).
 */
s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
    if (mq->validCount <= 0) {
        if (flag == OS_MESG_BLOCK) {
            s32 tries;
            for (tries = 0; tries < PORT_MQ_PUMP_TRIES && mq->validCount <= 0; tries++) {
                portPumpEvents();
            }
        }
        if (mq->validCount <= 0) {
            return -1;
        }
    }

    if (msg != NULL) {
        *msg = mq->msg[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    return 0;
}

/* ==== OS events ========================================================== */

/* _FINALROM is not defined in the PC build, so OS_NUM_EVENTS == 23. */
static struct {
    OSMesgQueue *mq;
    OSMesg msg;
} sEventTable[OS_NUM_EVENTS];

/**
 * Registrations kept code makes: OS_EVENT_SI (joy.c:133), OS_EVENT_SP/DP/
 * PRENMI (sched.c:178-180). The fake scheduler reads them back through
 * portOsGetEventMesg to post the interrupts hardware used to raise.
 */
void osSetEventMesg(OSEvent event, OSMesgQueue *mq, OSMesg msg)
{
    if (event < OS_NUM_EVENTS) {
        sEventTable[event].mq = mq;
        sEventTable[event].msg = msg;
    }
}

void portOsGetEventMesg(OSEvent event, OSMesgQueue **mq, OSMesg *msg)
{
    if (mq != NULL) {
        *mq = (event < OS_NUM_EVENTS) ? sEventTable[event].mq : NULL;
    }
    if (msg != NULL) {
        *msg = (event < OS_NUM_EVENTS) ? sEventTable[event].msg : NULL;
    }
}

/* ==== timer ============================================================== */

/**
 * The timer "fires" immediately: post msg to mq right now and return 0.
 * Sole kept caller is boss.c:187-188's controller settle loop
 * (osSetTimer(100ms) + BLOCK recv, four times) — with an immediate post the
 * loop just runs through; the 400ms of hardware settle time buys nothing on
 * PC. Repeating timers (interval != 0) would need the scheduler; no kept
 * caller uses one.
 */
s32 osSetTimer(OSTimer *timer, OSTime countdown, OSTime interval, OSMesgQueue *mq, OSMesg msg)
{
    timer->next = NULL;
    timer->prev = NULL;
    timer->interval = interval;
    timer->value = countdown;
    timer->mq = mq;
    timer->msg = msg;

    osSendMesg(mq, msg, OS_MESG_NOBLOCK);
    return 0;
}

/* ==== PI (cart bus) — "DMA" is memcpy, completion posted immediately ===== */

/**
 * "ROM addresses" on PC are host pointers (romdata is loaded/compiled in),
 * so DMA collapses to memcpy in the direction the caller asked for.
 * The real PI manager posts (OSMesg)mb to mq on completion; ramrom.c:44 and
 * audi.c:782 recv exactly one message per started DMA, so post it here.
 */
s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 direction, uintptr_t devAddr, void *vAddr, u32 nbytes, OSMesgQueue *mq)
{
    (void)pri;

    if (direction == OS_READ) {
        memcpy(vAddr, (const void *)devAddr, nbytes);
    } else {
        memcpy((void *)devAddr, vAddr, nbytes);
    }

    if (mb != NULL) {
        mb->hdr.retQueue = mq;
        mb->hdr.status = 0;
        mb->dramAddr = vAddr;
        mb->devAddr = devAddr;
        mb->size = nbytes;
    }

    osSendMesg(mq, (OSMesg)mb, OS_MESG_NOBLOCK);
    return 0;
}

/**
 * Raw variant: same memcpy, no completion message (the real one posts
 * nothing either — callers poll osPiGetStatus, which reports idle).
 */
s32 osPiRawStartDma(s32 direction, uintptr_t devAddr, void *dramAddr, u32 size)
{
    if (direction == OS_READ) {
        memcpy(dramAddr, (const void *)devAddr, size);
    } else {
        memcpy((void *)devAddr, dramAddr, size);
    }
    return 0;
}

/**
 * IO reads hit PI-bus registers/carts that don't exist on PC. Report 0 data
 * (token.c:59 / usb.c:290 treat that as "nothing there").
 */
s32 osPiReadIo(u32 devAddr, u32 *data)
{
    (void)devAddr;
    if (data != NULL) {
        *data = 0;
    }
    return 0;
}

/** PI bus is never busy (init.c:133 busy-wait exits immediately). */
u32 osPiGetStatus(void)
{
    return 0;
}

/**
 * The real one spawns the PI manager thread AND initializes the command
 * queue it was handed (pi.c:8 passes &piCmdQ). Keep the queue init so the
 * struct is valid if anything ever touches it; skip the thread.
 */
void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt)
{
    (void)pri;
    osCreateMesgQueue(cmdQ, cmdBuf, cmdMsgCnt);
}

/** No TLB, no KSEG0: virtual == physical (65 call sites, hottest os symbol). */
uintptr_t osVirtualToPhysical(void *addr)
{
    return (uintptr_t)addr;
}

/* ==== VI ================================================================= */

/*
 * Dummy framebuffer sized for the largest mode GE selects (640x480 16-bit,
 * hi-res HAF1). Early derefs of osViGet*Framebuffer() before the game hands
 * over real buffers land here instead of faulting.
 */
static u64 sViDummyFramebuffer[(640 * 480 * 2) / sizeof(u64)];

static struct {
    OSMesgQueue *eventMq;  /* osViSetEvent registration (sched.c retrace queue) */
    OSMesg eventMsg;
    u32 retraceCount;
    OSViMode *mode;
    f32 xScale;
    f32 yScale;
    u32 specialFeatures;
    u8 black;
    u8 repeatLine;
    void *swapBuffer;      /* last osViSwapBuffer pointer */
} sViState = { NULL, NULL, 0, NULL, 1.0f, 1.0f, 0, 0, 0, NULL };

void osViSetMode(OSViMode *mode)
{
    sViState.mode = mode;
}

/**
 * sched.c registers its retrace queue+msg here; the fake scheduler retrieves
 * it via portOsViGetEvent to post OS_SC_RETRACE-driving messages per frame.
 */
void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount)
{
    sViState.eventMq = mq;
    sViState.eventMsg = msg;
    sViState.retraceCount = retraceCount;
}

void portOsViGetEvent(OSMesgQueue **mq, OSMesg *msg, u32 *retraceCount)
{
    if (mq != NULL) {
        *mq = sViState.eventMq;
    }
    if (msg != NULL) {
        *msg = sViState.eventMsg;
    }
    if (retraceCount != NULL) {
        *retraceCount = sViState.retraceCount;
    }
}

void osViSetXScale(f32 value)
{
    sViState.xScale = value;
}

void osViSetYScale(f32 value)
{
    sViState.yScale = value;
}

void osViSetSpecialFeatures(u32 func)
{
    sViState.specialFeatures = func;
}

void osViRepeatLine(u8 active)
{
    sViState.repeatLine = active;
}

void osViBlack(u8 active)
{
    sViState.black = active;
}

void osViSwapBuffer(void *frameBufPtr)
{
    sViState.swapBuffer = frameBufPtr;
}

/**
 * Without retrace latching, "current" and "next" are both simply the last
 * swapped buffer (dummy until the first swap).
 */
void *osViGetCurrentFramebuffer(void)
{
    return (sViState.swapBuffer != NULL) ? sViState.swapBuffer : (void *)sViDummyFramebuffer;
}

void *osViGetNextFramebuffer(void)
{
    return (sViState.swapBuffer != NULL) ? sViState.swapBuffer : (void *)sViDummyFramebuffer;
}

/** VI manager thread never exists; retrace comes from the fake scheduler. */
void osCreateViManager(OSPri pri)
{
    (void)pri;
}

/* osViModeTable[42] comes from the repo's own src/libultrare/io/vitbl.c,
 * compiled into the PC build (see CMakeLists) — real register data for the
 * fields kept code reads (fr.c/sched.c: comRegs.hStart/width/xScale,
 * fldRegs[].vStart/yScale/origin). */

/* ==== AI (audio DMA) — real backend lands with the audio track =========== */

/** 0 bytes buffered = "DMA drained"; audi.c then schedules a full frame. */
u32 osAiGetLength(void)
{
    return 0;
}

/** Any frequency is fine on PC; echo it back (audi.c:340 stores the result). */
s32 osAiSetFrequency(u32 frequency)
{
    return (s32)frequency;
}

s32 osAiSetNextBuffer(void *bufPtr, u32 nbytes)
{
    (void)bufPtr; (void)nbytes;
    return 0;
}

/* ==== controllers ======================================================== */

/**
 * Pad presence. Headless: controller 0 only. Video builds: slots come from
 * the SDL input backend (midas.ini pads=N forces the first N connected -
 * default all 4, so MP setup always offers 4 players; a pad with no
 * physical controller reads neutral, same as an idle plugged-in stick).
 * joy.c derives its connected-slots mask from errno & CONT_NO_RESPONSE_ERROR.
 * A connected pad reports a controller pak (CONT_CARD_ON) when the
 * per-player rumble toggle says so (midas.ini padN_rumble via
 * portInputPakPresent): joy.c's rumble state machine then runs osPfsInit
 * (shim: "wrong device" = a rumble pak, not a mempak) -> osMotorInit
 * (stub: ok) -> RUMBLEPAKINITSTATE_READY, so in-game rumble reaches
 * portInputRumble for exactly the players that want it. GE saves via
 * EEPROM, so no mempak code path cares.
 */
static u32 contConnectedMask(void)
{
#ifdef GE_VIDEO
    extern u32 portInputPadMask(void);
    return portInputPadMask();
#else
    return 1;
#endif
}

void osContGetQuery(OSContStatus *data)
{
    u32 mask = contConnectedMask();
    s32 i;

    for (i = 0; i < MAXCONTROLLERS; i++) {
        data[i].type = 0;
        data[i].status = 0;
        if (mask & (1u << i)) {
            extern s32 portInputPakPresent(s32 slot);
            data[i].type = CONT_TYPE_NORMAL;
            data[i].status = portInputPakPresent(i) ? CONT_CARD_ON : 0;
            data[i].errno = 0;
        } else {
            data[i].errno = CONT_NO_RESPONSE_ERROR;
        }
    }
}

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *data)
{
    if (bitpattern != NULL) {
        *bitpattern = (u8)contConnectedMask();
    }
    if (data != NULL) {
        osContGetQuery(data);
    }
    /* Prime the SI queue: on N64 the init transaction posts an SI event.
     * joyPoll's read path is a NOBLOCK recv that only samples when a
     * completion message is pending - without this first message the
     * read/start-read cycle never begins. */
    if (mq != NULL) {
        osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    }
    return 0;
}

/**
 * The SI would raise an interrupt when the read completes; post the
 * completion message NOW so joy.c's recv (BLOCK at joy.c:224, NOBLOCK poll
 * at joy.c:463) sees exactly one message per started read.
 */
s32 osContStartReadData(OSMesgQueue *mq)
{
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

s32 osContStartQuery(OSMesgQueue *mq)
{
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

/**
 * errno mirrors osContGetQuery. With video built, pad 0 = keyboard per
 * midas.ini bindings (videoGetPad) merged with SDL controller 0; pads 1-3 =
 * SDL controllers (input_pc.c). Headless builds keep neutral pads.
 */
void osContGetReadData(OSContPad *data)
{
    u32 mask = contConnectedMask();
    s32 i;

    { /* bring-up (GE_DUMP_BOND): is the joy poll pump alive at all? */
        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
        static u32 calls;
        calls++;
        if (calls == 1 || (calls % 300) == 0) {
            f32 v[1];
            v[0] = (f32)calls;
            ge_dbg_vec("contread calls", v, 1);
        }
    }

    for (i = 0; i < MAXCONTROLLERS; i++) {
        data[i].button = 0;
        data[i].stick_x = 0;
        data[i].stick_y = 0;
        data[i].errno = (mask & (1u << i)) ? 0 : CONT_NO_RESPONSE_ERROR;
    }

#ifdef GE_VIDEO
    {
        extern void videoGetPad(OSContPad *pad);
        extern void portInputPadMerge(s32 idx, OSContPad *pad);
        extern void portInputPadGadgetChord(s32 idx, OSContPad *pad);
        extern void portInputPadNeutral(s32 idx);
        extern void portInputPollTick(void);
        static int noinput = -1;

        if (noinput < 0) {
            noinput = getenv("GE_NOINPUT") != NULL;
        }

        videoGetPad(&data[0]); /* keyboard + capture-run harness knobs */
        portInputPollTick();   /* slot housekeeping, BEFORE any merge reads a pad handle */

        /* GE_MPDRIVE must run even under GE_NOINPUT (the soak recipe sets
         * both) - noinput exists to block STRAY real input, but skipping
         * the merge also skipped the synthetic drive: every "drive" soak
         * to date ran with four stationary players and traversal was
         * never exercised. */
        {
            static int mpdrive = -1;
            if (mpdrive < 0) {
                const char *e = getenv("GE_MPDRIVE");
                mpdrive = (e != NULL) && (e[0] != '0');
                /* same rule for the other synthetic drives that live in
                 * the merge: GE_SKIPINTRO's A-tap never fired under
                 * GE_NOINPUT, so a "no stray input" run sat in the intro
                 * swirl forever. */
                e = getenv("GE_SKIPINTRO");
                if ((e != NULL) && (e[0] != '0')) mpdrive = 1;
                e = getenv("GE_DRIVE");
                if ((e != NULL) && (e[0] != '0')) mpdrive = 1;
            }
            if (portConIsOpen()) {
                /* Console open: every pad reads neutral, not just the keyboard,
                 * because a controller reaches joy_port through
                 * portInputPadMerge and a held stick would steer Bond while
                 * typing. The commit below still runs with all-zero downs so
                 * held Port actions release cleanly. */
                for (i = 0; i < MAXCONTROLLERS; i++) {
                    data[i].button = 0;
                    data[i].stick_x = 0;
                    data[i].stick_y = 0;
                    portInputPadNeutral(i); /* the twin-stick / digital-move caches the game reads beside data[] */
                }
            } else if (!noinput || mpdrive) {
                for (i = 0; i < MAXCONTROLLERS; i++) {
                    portInputPadMerge(i, &data[i]);
                    portInputPadGadgetChord(i, &data[i]); /* padN_gadget_switch: B then Z = next gadget */
                }
            }
        }

        { /* Port suite (joy_port.c): every source has ORed its "down" bits
           * (keyboard -> pad 0 in videoGetPad, controller custom maps -> their
           * slot in portInputPadMerge) - commit = edge/latch per pad, once per
           * poll, GE_NOINPUT included (GE_JOYPORT_AUTO must still fire). */
            extern void portJoyPortPollCommit(void);
            portJoyPortPollCommit();
        }

        { /* N64 octagonal stick gate: 85 cardinals, 65/axis diagonals -
           * every source (keyboard, controller, harness) funnels through
           * here, so nothing can exceed the real stick's envelope. */
            extern void portInputStickGate(OSContPad *pad, s32 slot);
            for (i = 0; i < MAXCONTROLLERS; i++) {
                portInputStickGate(&data[i], i);
            }
        }
        { /* GE_INPUTREC / GE_INPUTPLAY (inputrec.c): pad 0 as the game reads it */
            extern void portInputRecPad(OSContPad *pad);
            portInputRecPad(&data[0]);
        }
    }
#endif
}

/* ==== SI / controller-pak internals (referenced by src/motor.c) ========== */

/** SI access arbitration between threads — meaningless single-threaded. */
void __osSiGetAccess(void)
{
}

void __osSiRelAccess(void)
{
}

/** PIF-ram DMA: nothing to transfer; __osPfsPifRam stays zeroed. */
s32 __osSiRawStartDma(s32 direction, void *dramAddr)
{
    (void)direction; (void)dramAddr;
    return 0;
}

/** No controller pak on PC: every pak-ram access reports "no pack". */
s32 __osContRamRead(OSMesgQueue *mq, int channel, u16 address, u8 *buffer)
{
    (void)mq; (void)channel; (void)address; (void)buffer;
    return PFS_ERR_NOPACK;
}

s32 __osContRamWrite(OSMesgQueue *mq, int channel, u16 address, u8 *buffer, int force)
{
    (void)mq; (void)channel; (void)address; (void)buffer; (void)force;
    return PFS_ERR_NOPACK;
}

u8 __osContAddressCrc(u16 addr)
{
    (void)addr;
    return 0;
}

/* ==== EEPROM (GE cart is 4 kbit: 64 x 8-byte blocks = 512 bytes) ========= */

#define PORT_EEPROM_BLOCKS EEPROM_MAXBLOCKS /* 64 — the 4k part, NOT PD's 16k */
#define PORT_EEPROM_SIZE (PORT_EEPROM_BLOCKS * EEPROM_BLOCK_SIZE)

/* Persisted to Data/Saves/eeprom.bin: loaded lazily on first access and
 * rewritten whole (512 bytes) after every write. A missing or short file
 * behaves as a blank cart (the save checksum fails and defaults are
 * re-initialized); an old ./eeprom.bin is moved into Data/Saves on the first
 * load that finds none there. */
#define PORT_EEPROM_DIR  "Data/Saves"
#define PORT_EEPROM_FILE PORT_EEPROM_DIR "/eeprom.bin"
#define PORT_EEPROM_OLD  "eeprom.bin"   /* where older builds kept it (next to the exe) */

static u8 sEeprom[PORT_EEPROM_SIZE];

void fileGenerateCRC(u8 *addressA, u8 *addressB, save_data *retval); /* game/crc.c */

static s32 eepromGetBE32(const u8 *p)
{
    return (s32)(((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]);
}

/* One header or folder record: keep it if its checksums pass as stored by the
 * port (host order), else convert it if they pass as big-endian cart data. */
static s32 eepromImportRecord(u8 *rec, u32 len, s32 optionsoff)
{
    save_data crc;
    s32 host[2];

    memcpy(host, rec, sizeof(host));
    fileGenerateCRC(rec + 8, rec + len, &crc);
    if (crc.chksum1 == host[0] && crc.chksum2 == host[1]) {
        return 0;
    }
    if (crc.chksum1 != eepromGetBE32(rec) || crc.chksum2 != eepromGetBE32(rec + 4)) {
        return 0;
    }
    if (optionsoff >= 0) {
        u16 options = (u16)((rec[optionsoff] << 8) | rec[optionsoff + 1]);

        memcpy(rec + optionsoff, &options, sizeof(options));
        fileGenerateCRC(rec + 8, rec + len, &crc);
    }
    memcpy(rec, &crc.chksum1, sizeof(s32));
    memcpy(rec + 4, &crc.chksum2, sizeof(s32));
    return 1;
}

/* Project64 .eep files and real carts hold GE's save structs big-endian; the
 * port stores them in host order. Convert records that only validate big-endian. */
static void eepromImportN64(void)
{
    s32 converted = 0;
    s32 i;

    converted += eepromImportRecord(sEeprom, sizeof(smallSave), -1);
    for (i = 0; i < 5; i++) {
        converted += eepromImportRecord(sEeprom + sizeof(smallSave) + i * sizeof(save_data),
                sizeof(save_data), offsetof(save_data, options));
    }
    if (converted) {
        portConLogf("ge: eeprom: imported %d big-endian (Project64/cart) records\n", converted);
    }
}

static void eepromEnsureDir(void)
{
    extern const char *portRootPath(const char *name);
    static const char *const dirs[] = { "Data", PORT_EEPROM_DIR };
    int i;

    for (i = 0; i < 2; i++) {
        char path[1024];
        struct stat st;

        snprintf(path, sizeof(path), "%s", portRootPath(dirs[i]));
        if (stat(path, &st) != 0) {
#ifdef _WIN32
            _mkdir(path);
#else
            mkdir(path, 0755);
#endif
        }
    }
}

static void eepromLoad(void)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    extern const char *portRootPath(const char *name);
    static int loaded;
    FILE *f;

    if (loaded) {
        return;
    }
    loaded = 1;
    f = portRootFopen(PORT_EEPROM_FILE, "rb");
    if (f == NULL && (f = portRootFopen(PORT_EEPROM_OLD, "rb")) != NULL) {
        /* the pre-Data/Saves file: move it, so the next save lands beside it */
        char from[1024];

        fclose(f);
        eepromEnsureDir();
        snprintf(from, sizeof(from), "%s", portRootPath(PORT_EEPROM_OLD));
        if (rename(from, portRootPath(PORT_EEPROM_FILE)) == 0) {
            portConLogf("ge: eeprom: moved %s to %s\n", from, portRootPath(PORT_EEPROM_FILE));
            f = portRootFopen(PORT_EEPROM_FILE, "rb");
        } else {
            portConLogf("ge: eeprom: could not move %s into %s - reading it there\n", from, PORT_EEPROM_DIR);
            f = portRootFopen(PORT_EEPROM_OLD, "rb");   /* the first save then writes the new place */
        }
    }
    if (f != NULL) {
        size_t n = fread(sEeprom, 1, sizeof(sEeprom), f);
        fclose(f);
        portConLogf("ge: eeprom.bin loaded (%u bytes)\n", (unsigned)n);
        if (n == sizeof(sEeprom)) {
            eepromImportN64();
        }
    }
}

static void eepromFlush(void)
{
    extern FILE *portRootFopen(const char *name, const char *mode);
    extern const char *portRootPath(const char *name);
    FILE *f;

    eepromEnsureDir();
    f = portRootFopen(PORT_EEPROM_FILE, "wb");
    if (f == NULL) {
        static int warned;
        if (!warned) {
            warned = 1;
            portConLogf("ge: cannot write %s - saves are memory-only\n",
                    portRootPath(PORT_EEPROM_FILE));
        }
        return;
    }
    fwrite(sEeprom, 1, sizeof(sEeprom), f);
    fclose(f);
}

s32 osEepromProbe(OSMesgQueue *mq)
{
    (void)mq;
    eepromLoad();
    return EEPROM_TYPE_4K;
}

s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer)
{
    (void)mq;
    if (address >= PORT_EEPROM_BLOCKS) {
        return -1;
    }
    eepromLoad();
    memcpy(buffer, sEeprom + (u32)address * EEPROM_BLOCK_SIZE, EEPROM_BLOCK_SIZE);
    return 0;
}

s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer)
{
    (void)mq;
    if (address >= PORT_EEPROM_BLOCKS) {
        return -1;
    }
    eepromLoad();
    memcpy(sEeprom + (u32)address * EEPROM_BLOCK_SIZE, buffer, EEPROM_BLOCK_SIZE);
    eepromFlush();
    return 0;
}

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
    (void)mq;
    if ((u32)address * EEPROM_BLOCK_SIZE + (u32)nbytes > PORT_EEPROM_SIZE) {
        return -1;
    }
    eepromLoad();
    memcpy(buffer, sEeprom + (u32)address * EEPROM_BLOCK_SIZE, nbytes);
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
    (void)mq;
    if ((u32)address * EEPROM_BLOCK_SIZE + (u32)nbytes > PORT_EEPROM_SIZE) {
        return -1;
    }
    eepromLoad();
    memcpy(sEeprom + (u32)address * EEPROM_BLOCK_SIZE, buffer, nbytes);
    eepromFlush();
    return 0;
}

/* ==== controller pak (PFS) =============================================== */

/**
 * No pak. joy.c:186 treats anything except PFS_ERR_ID_FATAL/PFS_ERR_DEVICE
 * as "not a rumble pak" and leaves the slot alone. (Rumble arrives later via
 * PD-style osMotorProbe against the input backend.)
 */
s32 osPfsInit(OSMesgQueue *mq, OSPfs *pfs, int channel)
{
    /* "wrong device type": the inserted pak is a RUMBLE pak, not a
     * mempak - exactly the answer joy.c's rumble state machine needs to
     * proceed to osMotorInit (NOPACK made it skip, which is why in-game
     * rumble never armed while the launcher's direct test buzzed). */
    (void)mq; (void)pfs; (void)channel;
    return PFS_ERR_DEVICE;
}

/* ==== SP / DP (RSP+RDP belong to the fake scheduler / renderer) ========== */

void osSpTaskLoad(OSTask *task)
{
    (void)task;
}

void osSpTaskStartGo(OSTask *task)
{
    (void)task;
}

void osSpTaskYield(void)
{
}

/** 0 = "task did not yield" — sched.c:348 then treats the task as done. */
OSYieldResult osSpTaskYielded(OSTask *task)
{
    (void)task;
    return 0;
}

void osDpSetStatus(u32 data)
{
    (void)data;
}

/** Perf counters (clock/cmd/pipe/tmem); sched.c passes u32[4]. All idle. */
void osDpGetCounters(u32 *counters)
{
    counters[0] = 0;
    counters[1] = 0;
    counters[2] = 0;
    counters[3] = 0;
}

s32 osDpSetNextBuffer(void *bufPtr, u64 nbytes)
{
    (void)bufPtr; (void)nbytes;
    return 0;
}

/* ==== CPU housekeeping: cache / TLB / FPU / interrupts =================== */

void osInvalDCache(void *vaddr, s32 nbytes)
{
    (void)vaddr; (void)nbytes;
}

void osInvalICache(void *vaddr, s32 nbytes)
{
    (void)vaddr; (void)nbytes;
}

void osWritebackDCache(void *vaddr, s32 nbytes)
{
    (void)vaddr; (void)nbytes;
}

void osWritebackDCacheAll(void)
{
}

void osUnmapTLB(s32 index)
{
    (void)index;
}

/** init.c:162 reads, ors FS|EV, writes back — flush-to-zero is host default. */
u32 __osGetFpcCsr(void)
{
    return 0;
}

u32 __osSetFpcCsr(u32 value)
{
    (void)value;
    return 0;
}

/** Track the mask so callers see a consistent previous value; nothing masks. */
OSIntMask osSetIntMask(OSIntMask mask)
{
    static OSIntMask sCurrentMask = OS_IM_ALL;
    OSIntMask prev = sCurrentMask;
    sCurrentMask = mask;
    return prev;
}

void osInitialize(void)
{
}

/* ==== host IO (Indy dev-station link — no host on PC) ==================== */

void osReadHost(void *dramAddr, u32 nbytes)
{
    (void)dramAddr; (void)nbytes;
}

void osWriteHost(void *dramAddr, u32 nbytes)
{
    (void)dramAddr; (void)nbytes;
}

/* ==== debug printf ======================================================= */

void osSyncPrintf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    __builtin_va_end(args);
}
