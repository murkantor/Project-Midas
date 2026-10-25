/**
 * PC fake scheduler (docs/port/phase2-decisions.md D8).
 *
 * The N64 scheduler thread never runs on PC. Graphics tasks are submitted
 * directly (rsp.c's PC arm calls schedSubmitTask), and the shim's blocking
 * osRecvMesg calls portPumpEvents when a queue is empty - which plays the
 * role of the VI retrace interrupt: one pump = one "retrace" fanned out to
 * every registered scheduler client (boss.c's main loop blocks on exactly
 * that message).
 *
 * Null renderer for now: tasks complete instantly and their display lists
 * are only counted; Phase 3 hands them to the fast3d interpreter here.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "sched.h"
#include "game/frametiming.h"   /* portTickHz: GE_TICKHZ pump rate */

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console ring (spec section 7). */
extern void portConLogf(const char *fmt, ...);

#ifndef _WIN32
static int portSleepUntil(const struct timespec *target)
{
#ifdef __APPLE__
    struct timespec now, delay;
    clock_gettime(CLOCK_MONOTONIC, &now);
    delay.tv_sec = target->tv_sec - now.tv_sec;
    delay.tv_nsec = target->tv_nsec - now.tv_nsec;
    if (delay.tv_nsec < 0) {
        delay.tv_sec--;
        delay.tv_nsec += 1000000000L;
    }
    if (delay.tv_sec < 0) return 0;
    /* os.h reserves errno as an N64 controller field. */
    return nanosleep(&delay, NULL) == 0 ? 0 : *__error();
#else
    return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, target, NULL);
#endif
}
#endif

/**
 * Allocate zeroed memory guaranteed to sit ABOVE 0x10000000.
 *
 * Pointers that end up inside display lists or game structures must not
 * live below 0x10000000: fast3d's seg_addr resolves untagged low words as
 * segment references, and the game's address-classification tests assume
 * the N64 ranges are free. The Windows heap complies by accident; the
 * Linux 32-bit brk heap does NOT (it grows straight through 0x0E......,
 * which cost a Dam render crash when a converted-GDL pointer collided
 * with the installed BG-vertex segment 0x0E). Never free()able.
 */
/* One reservation attempt at a hinted high address. Returns NULL on miss. */
static void *portAllocHighRaw(void *hint, u32 size)
{
    void *p;
#ifdef _WIN32
    p = VirtualAlloc(hint, size, 0x3000 /* MEM_COMMIT|MEM_RESERVE */, 0x04 /* PAGE_READWRITE */);
#else
    p = mmap(hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
    {
        p = NULL;
    }
    else if ((uintptr_t) p < 0x10000000u)
    {
        munmap(p, size);
        p = NULL;
    }
#endif
    if (p != NULL && (uintptr_t) p < 0x10000000u)
    {
        p = NULL; /* Windows granted a low address - unusable here (leak it; never-freed by design) */
    }
    return p; /* VirtualAlloc/anonymous mmap memory arrives zeroed */
}

#ifdef _WIN32
/* Address-space scan: find a free 64 MB range between 0x10000000 and the 2 GB line.
 * The game is a 2 GB 32-bit process, so only five fixed 64 MB hint bases are
 * usable, and a 32-bit GL driver DLL or an injected overlay can occupy all of
 * them. When that happens portAllocHigh returns NULL, the model display list
 * stays unconverted, and textures draw as garbage. So we VirtualQuery every
 * region, take the highest free range that fits (the CRT heap grows up from low
 * addresses), and retry lower if a race steals it. Same >= 0x10000000 / <
 * 0x80000000 contract as the hinted path. */
static void *portAllocHighScan(u32 size)
{
    uintptr_t limit = 0x7FFF0000u;
    int attempt;

    for (attempt = 0; attempt < 8; attempt++)
    {
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t addr = 0x10000000u;
        uintptr_t best = 0;
        void *p;

        while (addr < limit && VirtualQuery((void *) addr, &mbi, sizeof(mbi)) == sizeof(mbi))
        {
            uintptr_t rbase = (uintptr_t) mbi.BaseAddress;
            uintptr_t rend = rbase + mbi.RegionSize;

            if (rend <= addr)
            {
                break; /* no forward progress - stop rather than spin */
            }
            if (mbi.State == MEM_FREE)
            {
                uintptr_t lo = rbase > 0x10000000u ? rbase : 0x10000000u;
                uintptr_t top = rend < limit ? rend : limit;

                lo = (lo + 0xFFFFu) & ~(uintptr_t) 0xFFFFu; /* 64 KB granule */
                if (top > lo && top - lo >= size)
                {
                    best = (top - size) & ~(uintptr_t) 0xFFFFu; /* highest fit, scanned upward */
                }
            }
            addr = rend;
        }
        if (best == 0)
        {
            return NULL;
        }
        p = VirtualAlloc((void *) best, size, 0x3000 /* MEM_COMMIT|MEM_RESERVE */, 0x04 /* PAGE_READWRITE */);
        if (p != NULL)
        {
            return p;
        }
        limit = best; /* taken between the query and the alloc: look lower */
    }
    return NULL;
}
#endif

/* GE_HIGHSCAN=1: skip the fixed hints and go straight to the scan - lets a
 * machine whose hints are free exercise the path the unlucky machine hits.
 * Diagnostic only; never set in midas.ini. */
static int portAllocHighSkipHints(void)
{
    static int skip = -1;
    if (skip < 0)
    {
        const char *e = getenv("GE_HIGHSCAN");
        skip = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return skip;
}

/* Bump-allocate small requests from one reserved high arena instead of one
 * hinted VirtualAlloc per call, which used up the limited hints on stage
 * reloads. Oversize requests still walk the hints individually. */
#define PORT_HIGH_ARENA_SIZE (64u * 1024u * 1024u)
static u8 *s_HighArena;
static u32 s_HighArenaUsed;
static int s_HighArenaTried;

/* Reserve the arena. main() calls this at boot, before SDL/GL/input load DLLs
 * and fragment the 2 GB address space, so the reservation cannot lose.
 * Idempotent; the lazy path still calls it for any caller that runs earlier. */
void portAllocHighReserve(void)
{
    int attempt;

    if (s_HighArenaTried)
    {
        return;
    }
    s_HighArenaTried = 1;

    for (attempt = 0; attempt < 8 && s_HighArena == NULL && !portAllocHighSkipHints(); attempt++)
    {
        s_HighArena = portAllocHighRaw((void *) (0x30000000u + 0x10000000u * (u32) attempt), PORT_HIGH_ARENA_SIZE);
    }
#ifdef _WIN32
    if (s_HighArena == NULL)
    {
        s_HighArena = (u8 *) portAllocHighScan(PORT_HIGH_ARENA_SIZE);
    }
#endif
    if (s_HighArena != NULL)
    {
        /* always-on: a user log then shows WHERE the arena sits,
         * and whether the hints or the scan placed it */
        portConLogf("ge: portAllocHigh: %u MB arena at %p\n",
                (u32) (PORT_HIGH_ARENA_SIZE >> 20), (void *) s_HighArena);
    }
    else
    {
        portConLogf("ge: portAllocHigh: no %u MB arena anywhere above 0x10000000 - props will draw with unconverted textures\n",
                (u32) (PORT_HIGH_ARENA_SIZE >> 20));
#ifdef _WIN32
        /* the Windows error separates "no contiguous range" (87 /
         * ERROR_INVALID_ADDRESS) from "commit limit" (1455) in a user log */
        portConLogf("ge: portAllocHigh: last Windows error %lu\n", (unsigned long) GetLastError());
#endif
    }
}

void *portAllocHigh(u32 size)
{
    u8 *arena;
    int attempt;

    size = (size + 15u) & ~15u;

    if (size <= PORT_HIGH_ARENA_SIZE)
    {
        portAllocHighReserve();
        arena = s_HighArena;

        if (arena != NULL && s_HighArenaUsed + size <= PORT_HIGH_ARENA_SIZE)
        {
            void *p = arena + s_HighArenaUsed;
            s_HighArenaUsed += size;
            { /* Early warning: this arena is never freed, so a slow leak
               * shows up as a crash MUCH later (e.g. a NULL stan pack,
               * then a CCTV with no model). Announce each quarter used so
               * the log says "running out" before something dies. */
                static u32 nextmark = PORT_HIGH_ARENA_SIZE / 4;
                if (s_HighArenaUsed >= nextmark)
                {
                    portConLogf("ge: portAllocHigh: %u MB of %u MB used\n",
                            s_HighArenaUsed >> 20, (u32) (PORT_HIGH_ARENA_SIZE >> 20));
                    nextmark += PORT_HIGH_ARENA_SIZE / 4;
                }
            }
            return p;
        }
    }

    for (attempt = 0; attempt < 8 && !portAllocHighSkipHints(); attempt++)
    {
        void *p = portAllocHighRaw((void *) (0x50000000u + 0x08000000u * (u32) attempt), size);
        if (p != NULL)
        {
            return p;
        }
    }
#ifdef _WIN32
    {
        void *p = portAllocHighScan(size);
        if (p != NULL)
        {
            return p;
        }
    }
#endif

    portConLogf("ge: portAllocHigh(%u) found no high address\n", size);
#ifdef _WIN32
    portConLogf("ge: portAllocHigh: last Windows error %lu\n", (unsigned long) GetLastError());
#endif
    return NULL;
}

/* One dedicated high mapping, never the shared arena (the 12 MB ROM image would
 * eat a fifth of it), using the same hint ladder and scan as an oversize
 * request. It must stay >= 0x10000000 so the ROM buffer fails fast3d's
 * segment-reference test like every other asset; NULL means the caller keeps
 * its heap copy. */
void *portAllocHighDedicated(u32 size)
{
    int attempt;

    size = (size + 0xFFFFu) & ~0xFFFFu;
    for (attempt = 0; attempt < 8 && !portAllocHighSkipHints(); attempt++)
    {
        void *p = portAllocHighRaw((void *) (0x50000000u + 0x08000000u * (u32) attempt), size);
        if (p != NULL)
        {
            return p;
        }
    }
#ifdef _WIN32
    return portAllocHighScan(size);
#else
    return NULL;
#endif
}

u32 g_PortGfxTasksSubmitted;
u32 g_PortFramesPumped;

/* Microseconds spent parked in the pump's pacing wait. The title bar turns
 * this into 'idle%' - the one number that separates "the loop has no slack"
 * from "the loop is waiting and something else sets the rate". */
unsigned long long g_PortPumpSleepUs;

/* Microseconds inside portAudioPump - the N64 audio synthesiser running
 * scalar on the CPU (alAudioFrame + portAcmdRun), up to 8 audio frames per
 * pump. Fixed work per second of AUDIO, so it does not shrink when the
 * frame rate rises - which is exactly why it is worth measuring here. */
unsigned long long g_PortAudioUs;

/* joyPoll (controller read) and the whole of portPumpEvents. The pump is
 * the last unmeasured region big enough to hold the missing 10.4ms. */
unsigned long long g_PortJoyUs;
unsigned long long g_PortPumpUs;

/**
 * True once per VIDEO FIELD (1/60s), whatever the pump rate.
 *
 * The pump replaced the N64's retrace interrupt, and several duties Rare
 * hung off that interrupt are authored per FIELD, not per loop iteration:
 * the controller poll and the music fade state machine. While a pump was
 * always a field this distinction did not exist. GE_TICKHZ broke that, and
 * the controller poll is the expensive one - a raphnet exchange is a
 * synchronous USB round trip (HidD_SetFeature then a GetFeature retry loop
 * with Sleep(1) between attempts, raphnet.c:341), several milliseconds
 * each. Running it once per tick instead of once per field is what drops an
 * uncapped 550fps to 140: the frame time becomes the USB round trip.
 *
 * Carries the remainder so the cadence is exactly 60Hz with no drift, and
 * is unconditionally true at tickhz=60 so classic is untouched.
 */
static int portRetraceDutyDue(void)
{
    static u32 acc;
    static u32 last;
    static int primed;
    u32 now;
    u32 dt;

    if (portTickHz() == 60)
    {
        return 1;
    }

    now = osGetCount();
    if (!primed)
    {
        primed = 1;
        last = now;
        return 1;
    }

    dt = now - last;
    last = now;

    /* A stage load can leave a multi-second gap; do not then fire a burst. */
    if (dt > 46875000u)
    {
        dt = 46875000u;
    }

    acc += dt;
    if (acc >= 781250u)
    {
        acc -= 781250u;
        if (acc >= 781250u)
        {
            acc = 0;   /* fell far behind: resync rather than catch up */
        }
        return 1;
    }

    return 0;
}

unsigned long long portProfNow(void)
{
#ifdef _WIN32
    static LARGE_INTEGER f;
    LARGE_INTEGER n;

    if (f.QuadPart == 0)
    {
        QueryPerformanceFrequency(&f);
    }
    QueryPerformanceCounter(&n);
    return (unsigned long long) (n.QuadPart / f.QuadPart) * 1000000ull
         + (unsigned long long) ((n.QuadPart % f.QuadPart) * 1000000 / f.QuadPart);
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long) ts.tv_sec * 1000000ull + (unsigned long long) ts.tv_nsec / 1000ull;
#endif
}

/* The game's memp arena on PC (see boss.c's PC arm). 64MB: the N64 had ~3MB
 * here; headroom is free on host and keeps every "pool exhausted" hang path
 * unreachable during bring-up. */
#define PORT_MEMP_ARENA_SIZE (64u * 1024u * 1024u)

void *portGetMempArena(u32 *outsize)
{
    static void *arena;
    if (arena == NULL)
    {
        /* Pin the arena ABOVE the N64 segment-word range (< 0x10000000):
         * arena-backed pointers get embedded into display lists, and
         * fast3d's seg_addr resolves untagged low-range words as segment
         * references (see gfx_pc.cpp). 0x20000000 keeps them disjoint. */
#ifdef _WIN32
        arena = VirtualAlloc((void *) 0x20000000u, PORT_MEMP_ARENA_SIZE,
                             0x3000 /* MEM_COMMIT|MEM_RESERVE */, 0x04 /* PAGE_READWRITE */);
        if (arena == NULL)
        {
            /* the hint is taken on this machine: scan for a high range
             * before settling for wherever Windows puts it (a low arena
             * trips the segment-word WARNING below) */
            arena = portAllocHighScan(PORT_MEMP_ARENA_SIZE);
        }
        if (arena == NULL)
        {
            arena = VirtualAlloc(NULL, PORT_MEMP_ARENA_SIZE, 0x3000, 0x04);
        }
#else
        /* glibc happens to mmap allocations this large (high addresses),
         * but make the >= 0x10000000 guarantee explicit. */
        arena = portAllocHigh(PORT_MEMP_ARENA_SIZE);
#endif
        if (arena == NULL)
        {
            portConLogf("ge: memp arena allocation failed\n");
            exit(1);
        }
        if ((uintptr_t) arena < 0x10000000u)
        {
            portConLogf("ge: WARNING: memp arena at %p is inside the N64 "
                            "segment-word range; segment resolution may misfire\n", arena);
        }
        /* always-on, paired with the portAllocHigh line: a user log then
         * shows both fixed-hint reservations and whether either moved */
        portConLogf("ge: memp arena: %u MB at %p\n", (u32) (PORT_MEMP_ARENA_SIZE >> 20), arena);
    }
    *outsize = PORT_MEMP_ARENA_SIZE;
    return arena;
}

static OSScMsg s_PortRetraceMsg = { OS_SC_RETRACE_MSG };

/* Post the task's own reply message (type OS_SC_DONE_MSG) to its reply queue,
 * as sched.c's __scTaskComplete did. It must never drop, or boss.c's
 * mission-start drain loop (pendingGfx != 0) waits forever; if the queue is
 * full we evict one expendable retrace message, since blocking would deadlock
 * the pump. */
static void schedPostDone(OSMesgQueue *q, OSMesg m)
{
    if (q == NULL)
    {
        return;
    }
    if (osSendMesg(q, m, OS_MESG_NOBLOCK) == -1)
    {
        OSMesg scratch;
        portConLogf("ge: sched: reply queue full at task %u - evicting one retrace for the DONE msg\n",
                g_PortGfxTasksSubmitted);
        osRecvMesg(q, &scratch, OS_MESG_NOBLOCK);
        osSendMesg(q, m, OS_MESG_NOBLOCK);
    }
}

#ifdef GE_VIDEO
/* Render-thread mode: tasks are queued to the render thread and their DONE
 * messages are posted from the pump on completion, keeping every osSendMesg on
 * the sim thread. At most 2 tasks are in flight (boss.c's pendingGfx < 2 gate),
 * in FIFO order, so 8 slots is pure margin. */
static struct { OSMesgQueue *q; OSMesg m; } s_DonePending[8];
static u32 s_DoneHead, s_DoneTail;
#endif

void schedSubmitTask(OSScTask *t)
{
    g_PortGfxTasksSubmitted++;

#ifdef GE_VIDEO
    /* Phase 3: hand the display list to the fast3d interpreter. GE submits
     * one gfx task per rendered frame; synchronously this brackets the
     * task with start/end frame and presents it immediately, in
     * render-thread mode it is queued and the DONE follows completion. */
    {
        extern s32 portRenderAsyncActive(void);
        extern void portRenderThreadFollowTick(void);
        extern void videoSubmitAsync(Gfx *cmds);
        extern void videoStartFrame(void);
        extern void videoSubmitCommands(Gfx *cmds);
        extern void videoEndFrame(void);

        portRenderThreadFollowTick(); /* between frames: a live tick change starts/stops the render thread (video.c) */
        if (portRenderAsyncActive())
        {
            s_DonePending[s_DoneTail % 8].q = t->msgQ;
            s_DonePending[s_DoneTail % 8].m = t->msg;
            s_DoneTail++;
            videoSubmitAsync((Gfx *) t->list.t.data_ptr);
            return; /* DONE posted by the pump once the frame really finishes */
        }
        videoStartFrame();
        videoSubmitCommands((Gfx *) t->list.t.data_ptr);
        videoEndFrame();
    }
#endif

    /* Complete immediately (synchronous path). */
    schedPostDone(t->msgQ, t->msg);
}

/* Vsync lock: with the render thread on and the swap waiting for vblank, the
 * pump's tick grid and the display clock otherwise run free and beat against
 * each other (uneven frame pacing).
 * - The grid runs 2% above the display (never below tickhz), so the pipeline
 * fills and the swap, i.e. the display, is what holds the sim.
 * - While the pipeline is full (2 frames in flight) the grid point is skipped,
 * the pump wakes the moment a frame finishes (not before boss.c's half-tick
 * gate could pass),
 *   and the grid re-anchors there: one tick per vblank. Waking early only when
 *   full matters; waking on every finished frame serialised sim and render.
 * Only active while the display is at most 1.35x the tick rate and tickhz is
 * not 60 (whose gate is the vanilla constant). GE_VSYNCLOCK=0 turns it off,
 * GE_VSYNCLOCKDBG=1 traces it. */
static s32 portVsyncLock(long long tickhz, long long *pacehz)
{
#ifdef GE_VIDEO
    extern s32 portRenderAsyncActive(void);
    extern int gfx_vsync_pacing(void);
    extern int gfx_display_refresh_hz(void);
    extern s32 portRenderCanWaitCompletion(void);
    static s32 s_Env = -1;
    static s32 s_Last = -1;
    s32 hz = 0;
    s32 on;

    if (s_Env < 0)
    {
        const char *e = getenv("GE_VSYNCLOCK");
        s_Env = (e != NULL && e[0] == '0') ? 0 : 1;
    }
    on = s_Env && tickhz > 0 && tickhz != 60 && portRenderAsyncActive() && gfx_vsync_pacing()
      && portRenderCanWaitCompletion();
    if (on)
    {
        hz = gfx_display_refresh_hz();
        on = hz > 0 && (long long) hz * 100 <= tickhz * 135;
    }
    if (on)
    {
        const long long over = (long long) hz + (hz + 49) / 50; /* +2%, at least +1 */

        *pacehz = (over > tickhz) ? over : tickhz;
    }
    if (on != s_Last)
    {
        printf("ge: video: vsync lock %s (display %d Hz, tickhz %d, pump %d Hz, render thread %s, vsync %s%s)\n",
               on ? "ON - frames paced by the swap" : "off",
               on ? hz : gfx_display_refresh_hz(), (int) tickhz, (int) *pacehz,
               portRenderAsyncActive() ? "on" : "off", gfx_vsync_pacing() ? "on" : "off",
               s_Env ? "" : ", GE_VSYNCLOCK=0");
        fflush(stdout);
        s_Last = on;
    }
    return on;
#else
    (void) tickhz;
    (void) pacehz;
    return 0;
#endif
}

/* vsync lock: is the pipeline full - both frames boss.c allows in flight
 * still unfinished, so the sim cannot tick until one is? */
static s32 portVsyncLockFull(void)
{
#ifdef GE_VIDEO
    return (s_DoneTail - s_DoneHead) >= 2u;
#else
    return 0;
#endif
}

/* vsync lock: 1 = a frame finished (already, or within ms) */
static s32 portVsyncLockWait(u32 ms)
{
#ifdef GE_VIDEO
    extern s32 portRenderWaitCompletion(u32 ms);
    return portRenderWaitCompletion(ms);
#else
    (void) ms;
    return 0;
#endif
}

/* GE_VSYNCLOCKDBG=1: about once a second, how many pumps ran, how many found
 * the pipeline full and how many of those a finished frame woke. Locked and
 * healthy = all three equal the tick rate; pumps above it = wasted wakes. */
static u32 s_VlockWoke;
static void portVsyncLockDbg(s32 vlock, s32 vwait, long long pacehz)
{
    static s32 s_Dbg = -1;
    static u32 s_Pumps, s_Full;

    if (s_Dbg < 0)
    {
        s_Dbg = (getenv("GE_VSYNCLOCKDBG") != NULL);
    }
    if (!s_Dbg)
    {
        return;
    }
    s_Pumps++;
    s_Full += (vwait != 0);
    if (s_Pumps >= (u32) pacehz)
    {
        printf("ge: vsynclockdbg: lock %s, %u pumps, %u with the pipeline full, %u woken by a finished frame, gfx tasks %u\n",
               vlock ? "on" : "off", s_Pumps, s_Full, s_VlockWoke, g_PortGfxTasksSubmitted);
        s_Pumps = s_Full = s_VlockWoke = 0;
    }
}

#ifdef _WIN32
/* The pacer sleeps on a high-resolution waitable timer (Windows 10 1803+, ~0.5
 * ms) because plain Sleep() can wake ~16 ms late when Windows ignores the 1 ms
 * timer-resolution request of a power-throttled process. The process also opts
 * out of power throttling; plain Sleep() is the fallback on older Windows. */
static HANDLE s_PaceTimer;
static int s_PaceTimerTried;

static void paceSleepSetup(void)
{
    typedef BOOL (WINAPI *SetProcInfoFn)(HANDLE, int, LPVOID, DWORD);
    struct {
        ULONG Version, ControlMask, StateMask;
    } thr;
    SetProcInfoFn setinfo;

    s_PaceTimerTried = 1;
    s_PaceTimer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                         TIMER_ALL_ACCESS);
    setinfo = (SetProcInfoFn) (void *) GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation");
    thr.Version = 1;                  /* PROCESS_POWER_THROTTLING_CURRENT_VERSION */
    thr.ControlMask = 0x1 | 0x4;      /* EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION */
    thr.StateMask = 0;                /* = never throttle either */
    printf("ge: sys: pacer sleep = %s, power throttling opt-out %s\n",
           s_PaceTimer != NULL ? "high-resolution waitable timer" : "Sleep() (no high-resolution timer on this Windows)",
           (setinfo != NULL && setinfo(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &thr, sizeof(thr)))
               ? "on" : "unavailable");
    fflush(stdout);
}

static void paceSleepMs(DWORD ms)
{
    if (!s_PaceTimerTried)
    {
        paceSleepSetup();
    }
    if (s_PaceTimer != NULL && ms > 0)
    {
        LARGE_INTEGER due;

        due.QuadPart = -(LONGLONG) ms * 10000;   /* relative, 100 ns units */
        if (SetWaitableTimer(s_PaceTimer, &due, 0, NULL, NULL, FALSE))
        {
            WaitForSingleObject(s_PaceTimer, INFINITE);
            return;
        }
    }
    Sleep(ms);
}
#endif

void portPumpEvents(void)
{
    OSScClient *c;
    static u32 framelimit;
    static s32 framelimit_read;
    static s32 s_PumpDepth;
    static unsigned long long s_PumpEntryUs;

    static unsigned long long s_PumpExitUs, s_PumpExitSleepUs;

    if (s_PumpDepth++ == 0)
    {
        s_PumpEntryUs = portProfNow();
        if (s_PumpExitUs != 0)
        {   /* hitchlog.c: the game code since the last pump, less any
             * schedule sleep a nested pump took inside it */
            extern void portHitchSimSpan(unsigned long long us);
            const unsigned long long span = s_PumpEntryUs - s_PumpExitUs;
            const unsigned long long slept = g_PortPumpSleepUs - s_PumpExitSleepUs;

            portHitchSimSpan(span > slept ? span - slept : 0);
        }
    }

    g_PortFramesPumped++;

    /* Bring-up harness: GE_FRAMES=n runs n frames then prints stats and
     * exits cleanly, so the prove-out is scriptable. Unset = run forever. */
    if (!framelimit_read)
    {
        const char *env = getenv("GE_FRAMES");
        framelimit = (env != NULL) ? (u32) strtoul(env, NULL, 0) : 0;
        framelimit_read = 1;
    }
    if (framelimit != 0 && g_PortFramesPumped >= framelimit)
    {
        printf("ge: frame limit reached: frames=%u gfxtasks=%u\n",
               g_PortFramesPumped, g_PortGfxTasksSubmitted);
        exit(0);
    }

    /* Pace the fake retrace to absolute wall-clock targets, resyncing if more
     * than two frames behind; a relative sleep drifts and makes motion judder.
     * GE_NOSLEEP=1 turns pacing off for headless soaks. */
    if (getenv("GE_NOSLEEP") == NULL)
    {
        /* The pump is the tick source, so GE_TICKHZ sets the interval of the
         * same absolute schedule; tickhz=0 (uncapped) skips pacing. */
        long long pacehz = (long long) portTickHz();

        if (pacehz <= 0)
        {
            goto pace_done;
        }
        /* vsync lock: may raise pacehz to just above the display; vwait =
         * the pipeline is full, so a finished frame ends the sleep */
        const long long gatehz = pacehz; /* boss.c's tick gate stays at tickhz */
        const s32 vlock = portVsyncLock(pacehz, &pacehz);
        const s32 vwait = vlock && portVsyncLockFull();
        portVsyncLockDbg(vlock, vwait, pacehz);
#ifdef _WIN32
        static LARGE_INTEGER s_PaceFreq;
        static LONGLONG s_PaceTarget;
        static LONGLONG s_PaceWake;  /* when the last pump woke ~ the last tick */
        LARGE_INTEGER now;

        if (s_PaceFreq.QuadPart == 0)
        {
            QueryPerformanceFrequency(&s_PaceFreq);
            QueryPerformanceCounter(&now);
            s_PaceTarget = now.QuadPart;
        }

        s_PaceTarget += s_PaceFreq.QuadPart / pacehz;
        QueryPerformanceCounter(&now);

        if (now.QuadPart > s_PaceTarget + (2 * s_PaceFreq.QuadPart / pacehz))
        {
            s_PaceTarget = now.QuadPart; /* fell behind: resync, no catch-up burst */
        }
        if (vwait)
        {
            /* pipeline full: this grid point cannot tick, and waking for it
             * just before the frame lands doubled the pumps (240/s at 120
             * ticks) - give the frame one more period, the timeout only a
             * backstop for a stalled swap */
            s_PaceTarget += s_PaceFreq.QuadPart / pacehz;
        }

        {
            LONGLONG idle_start = now.QuadPart;

            while (now.QuadPart < s_PaceTarget)
            {
                LONGLONG remain_ms = ((s_PaceTarget - now.QuadPart) * 1000) / s_PaceFreq.QuadPart;

                if (vwait)
                {
                    /* the same wait, but a finished frame ends it early (not
                     * before boss.c's half-tick gate could pass), since waking
                     * later with a non-vblank swap (VRR) was a pump the gate
                     * refused */
                    const LONGLONG gate_left = (s_PaceWake + s_PaceFreq.QuadPart / (2 * gatehz)) - now.QuadPart;

                    if (gate_left > 0)
                    {
                        LONGLONG ms = gate_left * 1000 / s_PaceFreq.QuadPart;

                        ms = (ms < remain_ms) ? ms : remain_ms;
                        if (ms > 2)
                        {
                            paceSleepMs((DWORD) (ms - 1));
                        }
                        else
                        {
                            Sleep(0);
                        }
                    }
                    else if (portVsyncLockWait((remain_ms > 2) ? (u32) (remain_ms - 1) : 0u))
                    {
                        /* tick now, and the grid follows the display */
                        QueryPerformanceCounter(&now);
                        s_PaceTarget = now.QuadPart;
                        s_VlockWoke++;
                        break;
                    }
                    else if (remain_ms <= 2)
                    {
                        Sleep(0);
                    }
                }
                else if (remain_ms > 2)
                {
                    paceSleepMs((DWORD)(remain_ms - 1)); /* high-resolution timer (paceSleepSetup), else Sleep */
                }
                else
                {
                    Sleep(0); /* final stretch: yield-spin to the exact target */
                }
                QueryPerformanceCounter(&now);
            }
            s_PaceWake = now.QuadPart;

            /* Title-bar 'idle%': how much of each second the pump spends
             * waiting for its schedule. ~0 means the loop genuinely has no
             * slack (real work); a healthy number means the pacer IS
             * sleeping and something else sets the rate (sleep overshoot,
             * granularity). Settles pacer-vs-work without a profiler. */
            g_PortPumpSleepUs += (unsigned long long)
                ((now.QuadPart - idle_start) * 1000000 / s_PaceFreq.QuadPart);
        }
#else
        /* Same absolute schedule as the Windows branch, so frame work eats
         * into the interval instead of adding to a fixed sleep. A target
         * already in the past returns immediately. */
        static struct timespec s_PaceTarget;
        static struct timespec s_PaceWake;  /* when the last pump woke ~ the last tick */
        struct timespec now;
        long long behind;

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (s_PaceTarget.tv_sec == 0 && s_PaceTarget.tv_nsec == 0)
        {
            s_PaceTarget = now;
        }

        s_PaceTarget.tv_nsec += (long) (1000000000LL / pacehz);
        if (s_PaceTarget.tv_nsec >= 1000000000L)
        {
            s_PaceTarget.tv_nsec -= 1000000000L;
            s_PaceTarget.tv_sec += 1;
        }

        /* fell behind by more than two frames: resync, no catch-up burst */
        behind = (long long) (now.tv_sec - s_PaceTarget.tv_sec) * 1000000000LL
               + (now.tv_nsec - s_PaceTarget.tv_nsec);
        if (behind > (2 * 1000000000LL / pacehz))
        {
            s_PaceTarget = now;
        }
        if (vwait)
        {   /* see the Windows branch: pipeline full, one more period */
            s_PaceTarget.tv_nsec += (long) (1000000000LL / pacehz);
            if (s_PaceTarget.tv_nsec >= 1000000000L)
            {
                s_PaceTarget.tv_nsec -= 1000000000L;
                s_PaceTarget.tv_sec += 1;
            }
        }

        if (vwait)
        {   /* see the Windows branch: wait for the target, but a finished
             * frame ends it early and the grid follows the display */
            for (;;)
            {
                struct timespec t;
                long long remain;

                clock_gettime(CLOCK_MONOTONIC, &t);
                remain = (long long) (s_PaceTarget.tv_sec - t.tv_sec) * 1000000000LL
                       + (s_PaceTarget.tv_nsec - t.tv_nsec);
                if (remain <= 0)
                {
                    break;
                }
                {   /* not before boss.c's half-tick gate could pass (Windows branch) */
                    const long long gate_left = (long long) (s_PaceWake.tv_sec - t.tv_sec) * 1000000000LL
                                              + (s_PaceWake.tv_nsec - t.tv_nsec) + 1000000000LL / (2 * gatehz);

                    if (gate_left > 0)
                    {
                        const long long ns = (gate_left < remain) ? gate_left : remain;
                        struct timespec d;

                        d.tv_sec = (time_t) (ns / 1000000000LL);
                        d.tv_nsec = (long) (ns % 1000000000LL);
                        nanosleep(&d, NULL);
                        continue;
                    }
                }
                if (portVsyncLockWait((remain > 2000000LL) ? (u32) (remain / 1000000LL - 1) : 0u))
                {
                    clock_gettime(CLOCK_MONOTONIC, &s_PaceTarget);
                    s_VlockWoke++;
                    break;
                }
                if (remain <= 2000000LL)
                {
                    while (portSleepUntil(&s_PaceTarget) == EINTR)
                    {
                    }
                    break;
                }
            }
        }
        else
        {
            while (portSleepUntil(&s_PaceTarget) == EINTR)
            {
                /* interrupted by a signal: retry to the same absolute target */
            }
        }

        {   /* see the Windows branch: title-bar 'idle%' */
            struct timespec after;
            clock_gettime(CLOCK_MONOTONIC, &after);
            s_PaceWake = after;
            {
                long long slept = (long long) (after.tv_sec - now.tv_sec) * 1000000000LL
                                + (after.tv_nsec - now.tv_nsec);
                if (slept > 0) { g_PortPumpSleepUs += (unsigned long long) (slept / 1000); }
            }
        }
#endif
pace_done:
        ;   /* tickhz=0 (uncapped) jumps here: no pacing at all */
    }

    /* Visible progress heartbeat: once a second on the console. */
    if ((g_PortFramesPumped % 60u) == 0u)
    {
        printf("ge: frame %u | gfx tasks submitted %u\n",
               g_PortFramesPumped, g_PortGfxTasksSubmitted);
        fflush(stdout);
    }

    { /* GE_GUN_AUDIT[=frame#]: one-shot gun-model texture audit (gun.c
       * portGunTexAudit) once menus are up - values < 10 mean "use the
       * default frame". Diagnostic for the white-texture weapons. */
        static s32 auditFrame = -2;

        if (auditFrame == -2)
        {
            const char *e = getenv("GE_GUN_AUDIT");
            auditFrame = -1;
            if (e != NULL)
            {
                auditFrame = (s32) strtoul(e, NULL, 0);
                if (auditFrame < 10)
                {
                    auditFrame = 300;
                }
            }
        }
        if (auditFrame > 0 && g_PortFramesPumped == (u32) auditFrame)
        {
            extern void portGunTexAudit(void);
            portGunTexAudit();
        }
    }

    { /* GE_ROOM_AUDIT=1: per-frame room-block integrity sweep (bg.c) -
       * diagnostic for portal-traversal room corruption. The function
       * checks the env flag itself and is a no-op when unset. */
        extern void portRoomAuditTick(void);
        portRoomAuditTick();
    }

    /* Per-frame duties run only in the outermost pump. A pump nested inside
     * an audio-side osRecvMesg would otherwise run musicFadeTick mid-way
     * through sndHandleEvent's list walk and hang it in an endless loop. */
    if (s_PumpDepth == 1)
    {

#ifdef GE_VIDEO
    { /* Render-thread mode: SDL window events must pump on this (window-owning)
       * thread, before joyPoll so fresh device/window state precedes the input
       * read. */
        extern void gfx_poll_events(void);
        gfx_poll_events();
    }
#endif

    /* Stand-in for the N64 retrace handler's joyPoll, without which no input
     * reaches the game. It must run every pump, not at 60Hz: each tick drains
     * the sample ring, and ticks with no sample lose button edges. */
    {
        extern s32 g_ContQueuesCreated;
        extern void joyPoll(void);
        unsigned long long ge_joy_t = portProfNow();

        if (g_ContQueuesCreated)
        {
            joyPoll();
        }
        g_PortJoyUs += portProfNow() - ge_joy_t;
    }

    /* The retrace handler's other duty (sched.c:322): advance the music
     * fades. Their steps are authored per retrace, so this one is gated to
     * the retrace rate or fades would run too fast at higher tick rates. */
    if (portRetraceDutyDue())
    {
        extern void musicFadeTick(void);

        musicFadeTick();
    }

    { /* AUDIO PHASE 1b: the N64 audio thread never runs on PC; one pump
       * per message-queue pass drives the real synthesizer and feeds SDL
       * (audio_pc.c no-ops until alInit has run and a player is registered).
       * The pass is what delivers the retrace the main loop is waiting for, so
       * the pump call rate and the loop's spin rate are the same number. */
        extern void portAudioPump(void);

#ifdef _WIN32
        LARGE_INTEGER a0, a1, af;
        QueryPerformanceFrequency(&af);
        QueryPerformanceCounter(&a0);
        portAudioPump();
        QueryPerformanceCounter(&a1);
        g_PortAudioUs += (unsigned long long) ((a1.QuadPart - a0.QuadPart) * 1000000 / af.QuadPart);
#else
        struct timespec a0, a1;
        clock_gettime(CLOCK_MONOTONIC, &a0);
        portAudioPump();
        clock_gettime(CLOCK_MONOTONIC, &a1);
        g_PortAudioUs += (unsigned long long)
            (((long long) (a1.tv_sec - a0.tv_sec) * 1000000000LL + (a1.tv_nsec - a0.tv_nsec)) / 1000);
#endif
    }

    }   /* end depth-0 duty block */

#ifdef GE_VIDEO
    /* Render-thread mode, at ANY depth: post the DONE message for each
     * frame the render thread finished. Any depth because the boss loop's
     * blocking osRecvMesg for exactly this DONE re-enters the pump - a
     * depth-0-only drain would deadlock the mission-start drain loop. */
    {
        extern s32 portRenderDrainCompletions(void);
        s32 n = portRenderDrainCompletions();

        while (n-- > 0 && s_DoneHead != s_DoneTail)
        {
            schedPostDone(s_DonePending[s_DoneHead % 8].q, s_DonePending[s_DoneHead % 8].m);
            s_DoneHead++;
        }
    }
#endif

    /* Always fanned out, at any depth: this message is what the blocking
     * wait that re-entered us is waiting for. */
    for (c = os_scheduler.clientList; c != NULL; c = c->next)
    {
        if (c->msgQ != NULL)
        {
            osSendMesg(c->msgQ, (OSMesg) &s_PortRetraceMsg, OS_MESG_NOBLOCK);
        }
    }

    /* Depth-guarded: osRecvMesg re-enters the pump from inside blocking
     * waits, and nested intervals would be counted twice. */
    if (--s_PumpDepth == 0)
    {
        s_PumpExitUs = portProfNow();
        s_PumpExitSleepUs = g_PortPumpSleepUs;
        g_PortPumpUs += s_PumpExitUs - s_PumpEntryUs;
    }
}
