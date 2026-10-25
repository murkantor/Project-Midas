/**
 * GoldenEye PC port entry point.
 *
 * Phase 1 (docs/port/PORT-PLAN.md): prove the game code LINKS and main()
 * runs. The N64 boot chain (boot.c -> init.c mainproc) is replaced here;
 * Phase 2 wires romdata loading, the fake scheduler, and the main-loop
 * collapse per the PD port's shape (pdmain.c/pdsched.c analysis in
 * docs/port/pd-port-architecture.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <ultra64.h>
#ifdef _WIN32
#include <direct.h>   /* _mkdir: portLogPath */
#else
#include <sys/stat.h> /* mkdir: portLogPath */
#include <unistd.h>   /* readlink, dup2 */
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console ring (spec section 7). */
extern void portConLogf(const char *fmt, ...);

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <io.h> /* _dup2/_fileno: portDirectLaunchLog */

/* Windows backtrace via dbghelp (SEH catches faults signal() misses on
 * MinGW). addr2line -e build_win/ge.exe on the printed addresses.
 *
 * Rare-logo crash diagnostic (guMtxF2L +0x6a, video-only): also prints the
 * fault address + access type, an EBP-chain walk (StackWalk64 stalls without
 * symbols), the memp arena bounds, and the title-sequence matrix pointers so
 * a wild write target can be told apart from a corrupted pointer/index. */
static int portPtrReadable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const char *) p + n <= (const char *) mbi.BaseAddress + mbi.RegionSize;
}

/* Crash output tee: a launch with no console (the GUI-subsystem exe) would
 * lose the whole dump. Mirror every crash line into Data/Logs/crash.log
 * (unbuffered, as the process is dying). */
const char *portLogPath(const char *name);
static FILE *sCrashLog;

static FILE *crashLogFile(void)
{
    if (sCrashLog == NULL) {
        sCrashLog = fopen(portLogPath("crash.log"), "w");
        if (sCrashLog != NULL) {
            setvbuf(sCrashLog, NULL, _IONBF, 0);
        }
    }
    return sCrashLog;
}

static void crashPrintf(const char *fmt, ...)
{
    va_list ap;
    FILE *f = crashLogFile();

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (f != NULL) {
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
    }
}

/* Name the module that owns an address (stalls often sit under injected DLLs
 * such as gameoverlayrenderer), using VirtualQuery's AllocationBase as the
 * module handle. This is deliberately loader-lock-free: GetModuleFileNameA
 * takes the loader lock, and the watchdog calls this with the main thread
 * suspended, possibly while it holds that lock, so the name is read straight
 * from the mapped image's PE export directory (the exe is matched by its base
 * via GetModuleHandle(NULL)). Every read is bounds-checked and nothing here can
 * block; an unnamed image prints "?" with the absolute address kept as the
 * offset. */
static const char *portModuleFor(unsigned long addr, unsigned long *offset)
{
    static char name[64];
    MEMORY_BASIC_INFORMATION mbi;
    const unsigned char *base;
    unsigned long pe, expRva, nameRva, i;

    *offset = addr;
    if (!VirtualQuery((void *) (uintptr_t) addr, &mbi, sizeof(mbi)) || mbi.AllocationBase == NULL) {
        return "?";
    }
    base = (const unsigned char *) mbi.AllocationBase;
    if ((HMODULE) mbi.AllocationBase == GetModuleHandleA(NULL)) {
        *offset = addr - (unsigned long) (uintptr_t) base;
        return "ProjectMidas.exe";
    }
    if (!portPtrReadable(base, 0x40) || base[0] != 'M' || base[1] != 'Z') {
        return "?";
    }
    pe = *(const unsigned long *) (base + 0x3C);
    /* "PE\0\0" + COFF(20) + PE32 optional header: DataDirectory[EXPORT]
     * sits at pe+0x78 (32-bit image - this is a 32-bit process) */
    if (pe > 0x1000 || !portPtrReadable(base + pe, 0x80) ||
        *(const unsigned long *) (base + pe) != 0x00004550ul ||
        *(const unsigned short *) (base + pe + 0x18) != 0x10B) {
        return "?";
    }
    expRva = *(const unsigned long *) (base + pe + 0x78);
    if (expRva == 0 || !portPtrReadable(base + expRva, 0x28)) {
        return "?";
    }
    nameRva = *(const unsigned long *) (base + expRva + 0x0C);
    if (nameRva == 0 || !portPtrReadable(base + nameRva, sizeof(name))) {
        return "?";
    }
    for (i = 0; i < sizeof(name) - 1 && base[nameRva + i] != '\0'; i++) {
        name[i] = (char) base[nameRva + i];
    }
    name[i] = '\0';
    *offset = addr - (unsigned long) (uintptr_t) base;
    return name;
}

static LONG WINAPI portSehHandler(EXCEPTION_POINTERS *ep)
{
    extern int current_menu;
    extern int g_MainStageNum;
    extern unsigned int g_PortFramesPumped;
    extern Mtx *matrixBufferRareLogo0, *matrixBufferRareLogo1, *matrixBufferRareLogo2;
    extern Mtx *matrixBufferGunbarrel0, *matrixBufferGunbarrel1;
    extern Mtx *matrixBufferIntroBackdrop, *matrixBufferIntroBond;
    extern u32 D_8002A7D0;
    void *portGetMempArena(u32 *outsize);
    CONTEXT *ctx = ep->ContextRecord;
    STACKFRAME64 sf;
    HANDLE proc = GetCurrentProcess();
    HANDLE thr = GetCurrentThread();
    u32 arenasize = 0;
    void *arena;
    /* snapshot before StackWalk64 - it MUTATES the CONTEXT it walks */
    unsigned long faultEip = ctx->Eip;
    unsigned long ebp = ctx->Ebp;
    int i;

    crashPrintf( "\nge: EXCEPTION 0x%08lx (menu=%d stage=%d frame=%u)\n",
            (unsigned long) ep->ExceptionRecord->ExceptionCode,
            current_menu, g_MainStageNum, g_PortFramesPumped);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        crashPrintf( "  %s of address 0x%08lx at eip 0x%08lx\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                (unsigned long) ep->ExceptionRecord->ExceptionInformation[1],
                (unsigned long) ctx->Eip);
    }
    crashPrintf( "  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx\n",
            ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);

    arena = portGetMempArena(&arenasize);
    crashPrintf( "  memp arena: %p + 0x%08lx\n", arena, (unsigned long) arenasize);
    crashPrintf( "  D_8002A7D0=%lu rare0=%p rare1=%p rare2=%p gb0=%p gb1=%p backdrop=%p bond=%p\n",
            (unsigned long) D_8002A7D0,
            (void *) matrixBufferRareLogo0, (void *) matrixBufferRareLogo1,
            (void *) matrixBufferRareLogo2, (void *) matrixBufferGunbarrel0,
            (void *) matrixBufferGunbarrel1, (void *) matrixBufferIntroBackdrop,
            (void *) matrixBufferIntroBond);

    crashPrintf( "ge: StackWalk64 backtrace:\n");
    SymInitialize(proc, NULL, TRUE);
    memset(&sf, 0, sizeof(sf));
    sf.AddrPC.Offset = ctx->Eip;    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx->Ebp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx->Esp; sf.AddrStack.Mode = AddrModeFlat;

    for (i = 0; i < 24; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, proc, thr, &sf, ctx,
                         NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (sf.AddrPC.Offset == 0) break;
        crashPrintf( "  #%d 0x%08llx\n", i, (unsigned long long) sf.AddrPC.Offset);
    }

    /* Walk the EBP chain by hand ([ebp] = caller ebp, [ebp+4] = return
     * address), since StackWalk64 has no PDB symbols to guide it. Needs
     * -fno-omit-frame-pointer, set in CMakeLists. */
    {
        unsigned long off;
        const char *mod = portModuleFor(faultEip, &off);
        crashPrintf( "ge: EBP-chain backtrace (eip 0x%08lx first):\n  0x%08lx (%s+0x%lx)\n",
                faultEip, faultEip, mod, off);
    }
    for (i = 0; i < 32; i++) {
        unsigned long *fp = (unsigned long *) ebp;
        unsigned long off;
        const char *mod;
        if (!portPtrReadable(fp, 8)) break;
        if (fp[1] == 0) break;
        mod = portModuleFor(fp[1], &off);
        crashPrintf( "  0x%08lx (%s+0x%lx)\n", fp[1], mod, off);
        if (fp[0] <= ebp) break; /* stack grows down; next frame must be higher */
        ebp = fp[0];
    }
    fflush(NULL);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void portCrashHandler(int sig) { (void) sig; _exit(139); }

/* GE_WATCH=1: hardware watchpoints (DR0-DR3) on the title-sequence matrix
 * pointers that arrive corrupted at the rare-logo crash. Logs every writer's
 * EIP (addr2line it); the stomper shows up as a write that is not
 * alloc_intro_matrices. Debug registers must be set while the target thread
 * is suspended, hence the helper thread. */
static void *s_WatchAddr[4];

static LONG WINAPI portWatchHandler(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        extern unsigned int g_PortFramesPumped;
        extern int current_menu;
        CONTEXT *c = ep->ContextRecord;
        unsigned long hits = c->Dr6 & 0xF;
        int i;
        for (i = 0; i < 4; i++) {
            if (hits & (1ul << i)) {
                portConLogf("ge: WATCH dr%d %p=0x%08lx written, eip after=0x%08lx (menu=%d frame=%u)\n",
                        i, s_WatchAddr[i], (unsigned long) *(unsigned long *) s_WatchAddr[i],
                        (unsigned long) c->Eip, current_menu, g_PortFramesPumped);
            }
        }
        fflush(stderr);
        c->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

struct PortWatchArgs { HANDLE mainThread; };

static DWORD WINAPI portWatchArmThread(LPVOID param)
{
    struct PortWatchArgs *args = (struct PortWatchArgs *) param;
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SuspendThread(args->mainThread);
    if (GetThreadContext(args->mainThread, &ctx)) {
        int i;
        ctx.Dr0 = (DWORD) (uintptr_t) s_WatchAddr[0];
        ctx.Dr1 = (DWORD) (uintptr_t) s_WatchAddr[1];
        ctx.Dr2 = (DWORD) (uintptr_t) s_WatchAddr[2];
        ctx.Dr3 = (DWORD) (uintptr_t) s_WatchAddr[3];
        ctx.Dr7 = 0;
        for (i = 0; i < 4; i++) {
            ctx.Dr7 |= 1ul << (2 * i);            /* local enable */
            ctx.Dr7 |= 1ul << (16 + 4 * i);       /* R/W = 01: break on write */
            ctx.Dr7 |= 3ul << (18 + 4 * i);       /* LEN = 11: 4 bytes */
        }
        if (!SetThreadContext(args->mainThread, &ctx)) {
            portConLogf("ge: WATCH arm failed (SetThreadContext err %lu)\n", GetLastError());
        } else {
            portConLogf("ge: WATCH armed on %p %p %p %p\n",
                    s_WatchAddr[0], s_WatchAddr[1], s_WatchAddr[2], s_WatchAddr[3]);
        }
    }
    ResumeThread(args->mainThread);
    CloseHandle(args->mainThread);
    free(args);
    return 0;
}

/**
 * First-chance AV reporter (always on): the Dam-load crash exits 0xC0000005
 * with EMPTY stderr - something (SDL installs its own unhandled filter)
 * swallows the exception before portSehHandler sees it. A vectored handler
 * runs FIRST-CHANCE on the faulting thread, before any filter chain.
 * Print-and-continue-search only; portSehHandler still does the full dump
 * when it does get called.
 */
static LONG WINAPI portFirstChanceAV(EXCEPTION_POINTERS *ep)
{
    static LONG printed = 0;

    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        InterlockedIncrement(&printed) <= 3) {
        extern unsigned int g_PortFramesPumped;
        DWORD ebp = ep->ContextRecord->Ebp;
        int d;
        crashPrintf( "ge: AV(first-chance) %s 0x%08lx eip=0x%08lx frame=%u\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                (unsigned long) ep->ExceptionRecord->ExceptionInformation[1],
                (unsigned long) ep->ContextRecord->Eip, g_PortFramesPumped);
        for (d = 0; d < 12 && portPtrReadable((void *) (uintptr_t) ebp, 8); d++) {
            crashPrintf( "   #%d 0x%08lx\n", d, (unsigned long) ((DWORD *) (uintptr_t) ebp)[1]);
            ebp = ((DWORD *) (uintptr_t) ebp)[0];
        }
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * Stall watchdog (always on, Windows): the port is single-threaded, so any
 * game-code spin freezes the window with no crash handler ever firing.
 * If the frame counter stops for ~4s, sample the main thread's EIP three
 * times (SuspendThread + GetThreadContext), walk the EBP chain, and exit -
 * every hard lock becomes a symbolizable backtrace instead of a mystery.
 */
static DWORD WINAPI portStallWatchdog(LPVOID param)
{
    HANDLE mainThread = (HANDLE) param;
    extern unsigned int g_PortFramesPumped;
    extern unsigned int g_PortGfxTasksSubmitted;
    unsigned int last = 0;
    unsigned int lastTasks = 0;
    int frozen = 0;
    int tasksFrozen = 0;
    int modalEpisode = 0;

    for (;;) {
        unsigned int now;
        unsigned int nowTasks;
        Sleep(2000);
        now = g_PortFramesPumped;
        nowTasks = g_PortGfxTasksSubmitted;
        if (nowTasks != lastTasks) {
            lastTasks = nowTasks;
            tasksFrozen = 0;
        } else {
            tasksFrozen++;
        }
        if (now != last) {
            last = now;
            frozen = 0;
            modalEpisode = 0;
            /* frames alive but NO gfx task for ~16s: the game is stuck in
             * a blocking wait (the pump keeps frames ticking through
             * osRecvMesg) - dump where it sits. Menus always submit
             * tasks, so a long task freeze is a real wedge. */
            if (tasksFrozen < 8) {
                continue;
            }
            portConLogf("ge: STALL (tasks frozen, frames alive) frame=%u tasks=%u\n", now, nowTasks);
        } else {
            frozen++;
        }
        /* Boot grace: Steam's gameoverlayrenderer.dll hooks GL context
         * creation and blocks the main thread on IPC with Steam for many
         * seconds before the FIRST frame is ever pumped (his Steam-launch
         * "crash" was this watchdog exiting at frame=0, ntdll wait under
         * 0x64xxxxxx overlay frames). Slow boot, not a wedge: allow 60s
         * until the first frame. */
        if (now == 0 && frozen < 30) {
            continue;
        }
        if (tasksFrozen < 8 && frozen < 2) {
            continue;
        }

        /* OS modal loop, not a wedge: Windows runs window drag/resize,
         * caption-button tracking and message boxes as a modal loop inside
         * DefWindowProc, so the main thread sits in the NtUser message wait
         * (called from USER32/uxtheme) until the user lets go. When the samples
         * show that, keep waiting and say so once per episode instead of
         * killing a healthy game. */
        {
            CONTEXT ctx;
            int modal = 0;
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            SuspendThread(mainThread);
            if (GetThreadContext(mainThread, &ctx)) {
                unsigned long off;
                char eipMod[64];
                const char *callerMod = "?";
                snprintf(eipMod, sizeof(eipMod), "%s", portModuleFor(ctx.Eip, &off));
                if (portPtrReadable((void *) (uintptr_t) ctx.Ebp, 8)) {
                    callerMod = portModuleFor(((DWORD *) (uintptr_t) ctx.Ebp)[1], &off);
                }
                modal = (_stricmp(eipMod, "win32u.dll") == 0 || _stricmp(eipMod, "USER32.dll") == 0)
                     && (_stricmp(callerMod, "USER32.dll") == 0 || _stricmp(callerMod, "uxtheme.dll") == 0);
            }
            ResumeThread(mainThread);
            if (modal) {
                if (modalEpisode == 0) {
                    portConLogf("ge: STALL: main thread inside a Windows modal loop "
                                    "(window drag/resize/caption/menu) at frame=%u - OS pause, not a wedge; waiting\n", now);
                    fflush(stderr);
                }
                modalEpisode++;
                frozen = 0;
                tasksFrozen = 0;
                continue;
            }
            if (modalEpisode) {
                portConLogf("ge: STALL: modal loop ended (%d checks) but frames still frozen - dumping\n", modalEpisode);
            }
            modalEpisode = 0;
        }

        {
            int s;
            for (s = 0; s < 3; s++) {
                CONTEXT ctx;
                memset(&ctx, 0, sizeof(ctx));
                ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                SuspendThread(mainThread);
                if (GetThreadContext(mainThread, &ctx)) {
                    DWORD ebp = ctx.Ebp;
                    unsigned long off;
                    const char *mod;
                    int d;
                    mod = portModuleFor(ctx.Eip, &off);
                    crashPrintf("ge: STALL sample %d: eip=0x%08lx (%s+0x%lx) ebp=0x%08lx (frame=%u)\n",
                            s, (unsigned long) ctx.Eip, mod, off, (unsigned long) ebp, now);
                    for (d = 0; d < 12 && portPtrReadable((void *) (uintptr_t) ebp, 8); d++) {
                        unsigned long ret = (unsigned long) ((DWORD *) (uintptr_t) ebp)[1];
                        mod = portModuleFor(ret, &off);
                        crashPrintf("   #%d 0x%08lx (%s+0x%lx)\n", d, ret, mod, off);
                        ebp = ((DWORD *) (uintptr_t) ebp)[0];
                    }
                }
                ResumeThread(mainThread);
                fflush(NULL);
                Sleep(300);
            }
        }
        /* If frames are moving again by the end of the dump, the OS held the
         * main thread (e.g. a QuickEdit selection blocking console writes), not
         * a wedge, so keep watching. This applies only on the frame-frozen path
         * (frozen > 0); the tasks-frozen wedge above must still exit. */
        if (frozen > 0 && g_PortFramesPumped != now) {
            crashPrintf("ge: STALL: frames resumed during the dump (frame=%u) - OS pause, not a wedge; continuing\n",
                        g_PortFramesPumped);
            fflush(NULL);
            last = g_PortFramesPumped;
            frozen = 0;
            tasksFrozen = 0;
            continue;
        }
        crashPrintf("ge: STALL: frame counter frozen - exiting after dump\n");
        fflush(NULL);
        _exit(3);
    }
}

static void portArmStallWatchdog(void)
{
    HANDLE mainThread;
    HANDLE th;

    if (getenv("GE_NOSTALLDUMP") != NULL) {
        return; /* escape hatch for debugger sessions */
    }
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                         &mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return;
    }
    th = CreateThread(NULL, 0, portStallWatchdog, mainThread, 0, NULL);
    if (th != NULL) {
        CloseHandle(th);
    }
}

static void portArmWatchpoints(void)
{
    extern Mtx *matrixBufferRareLogo0, *matrixBufferRareLogo1, *matrixBufferRareLogo2;
    extern Mtx *matrixBufferGunbarrel0;
    struct PortWatchArgs *args;
    HANDLE th;

    {
        /* GE_WATCH_ADDR=hex[,hex...]: watch up to 4 arbitrary addresses
         * (deterministic across runs here - arena layout is stable).
         * Plain GE_WATCH=1 keeps the title-matrix-pointer defaults. */
        const char *spec = getenv("GE_WATCH_ADDR");
        if (spec != NULL) {
            int n = 0;
            while (n < 4 && *spec) {
                s_WatchAddr[n++] = (void *) (uintptr_t) strtoul(spec, NULL, 16);
                spec = strchr(spec, ',');
                if (spec == NULL) break;
                spec++;
            }
            while (n < 4) s_WatchAddr[n++] = s_WatchAddr[0];
        } else if (getenv("GE_WATCH") != NULL) {
            s_WatchAddr[0] = &matrixBufferRareLogo0;
            s_WatchAddr[1] = &matrixBufferRareLogo1;
            s_WatchAddr[2] = &matrixBufferRareLogo2;
            s_WatchAddr[3] = &matrixBufferGunbarrel0;
        } else {
            return;
        }
    }

    AddVectoredExceptionHandler(1, portWatchHandler);

    args = malloc(sizeof(*args));
    if (args == NULL) return;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                         &args->mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        free(args);
        return;
    }
    th = CreateThread(NULL, 0, portWatchArmThread, args, 0, NULL);
    if (th != NULL) {
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }
}
#else
#include <execinfo.h>

/* Bring-up crash handler: print a raw backtrace (addresses decode with
 * addr2line -e build_pc/ge) so crashes are diagnosable without gdb. */
static void portCrashHandler(int sig)
{
    void *frames[24];
    int n = backtrace(frames, 24);
    fprintf(stderr, "\nge: fatal signal %d, backtrace (%d frames):\n", sig, n);
    backtrace_symbols_fd(frames, n, 2);
    fflush(NULL);
    _exit(128 + sig);
}
#endif

/* init.c's mainproc: thread/scheduler setup then bossEntry (the game).
 * On PC nothing else runs threads, so calling it IS the game. The N64-only
 * front half of init.c (cache/TLB/FPU/boot-segment DMA) is skipped - the
 * shim's osInitialize covers what remains. */
void mainproc(void *args);

/**
 * Bring-up dump helper for game-side code (which can't reach stdio through
 * the shadow headers reliably): prints tag + n floats to stderr when
 * GE_DUMP_BOND is set. Callers throttle themselves.
 */
void ge_dbg_vec(const char *tag, const float *v, int n)
{
    static int en = -1;
    char line[1024];
    int len;
    int i;

    if (en < 0) {
        en = getenv("GE_DUMP_BOND") != NULL;
    }
    if (!en) {
        return;
    }
    /* One portConLogf per line, since the console ring only ends a row on '\n'.
     * 48 bytes is wider than any " %.2f" float (-FLT_MAX is 44 chars) plus NUL,
     * so snprintf never truncates. */
    len = snprintf(line, sizeof(line), "ge: %.255s", tag);
    for (i = 0; i < n && len <= (int) sizeof(line) - 48; i++) {
        len += snprintf(line + len, sizeof(line) - len, " %.2f", v[i]);
    }
    portConLogf("%s\n", line);
    fflush(stderr); /* dumps precede crashes; unflushed tails vanish */
}

/* GE_DUMP_BOND diagnostic: hex-dump Gfx words so DL contents can be read
 * off a run log (room-texture investigation and successors). */
void ge_dbg_gfxdump(const char *tag, void *gdl, int count)
{
    unsigned int *w = (unsigned int *) gdl;
    int i;
    portConLogf("ge: %s at %p:\n", tag, gdl);
    for (i = 0; i < count; i++) {
        fprintf(stderr, "  %2d: %08x %08x\n", i, w[i * 2], w[i * 2 + 1]);
    }
    fflush(stderr);
}

/* ==== exe-relative files (Data/Saves/eeprom.bin, midas.ini) =================
 * Consumers: libultra_shim.c (EEPROM persistence), video.c (input config).
 * The directory holding the executable, NOT the cwd, because the game may be
 * launched from Explorer or a shell with any working directory. */
static char sPortRootDir[1024]; /* with trailing separator; "" = cwd fallback */

static void portInitRootDir(const char *argv0)
{
    int last = -1;
    int i;
#ifdef _WIN32
    /* authoritative on Windows regardless of how we were launched */
    DWORD n = GetModuleFileNameA(NULL, sPortRootDir, sizeof(sPortRootDir) - 1);
    if (n == 0 || n >= sizeof(sPortRootDir) - 1) {
        sPortRootDir[0] = '\0';
        return;
    }
    (void) argv0;
#elif defined(__APPLE__)
    uint32_t size = sizeof(sPortRootDir);
    if (_NSGetExecutablePath(sPortRootDir, &size) != 0) {
        sPortRootDir[0] = '\0';
        return;
    }
#else
    /* argv[0] is not authoritative on Linux (PATH launches have no slash,
     * launchers/symlinks point elsewhere), and a cwd fallback scattered
     * eeprom.bin/midas.ini across directories. So use /proc/self/exe, the Linux
     * equivalent of GetModuleFileNameA, and keep argv[0] only as the fallback
     * for other POSIX. */
    ssize_t n = readlink("/proc/self/exe", sPortRootDir, sizeof(sPortRootDir) - 1);
    if (n > 0 && (size_t) n < sizeof(sPortRootDir) - 1) {
        sPortRootDir[n] = '\0';
    } else if (argv0 == NULL ||
        (size_t) snprintf(sPortRootDir, sizeof(sPortRootDir), "%s", argv0) >= sizeof(sPortRootDir)) {
        sPortRootDir[0] = '\0'; /* no argv0 / overlong: cwd fallback */
    }
#endif
    for (i = 0; sPortRootDir[i] != '\0'; i++) {
        if (sPortRootDir[i] == '/' || sPortRootDir[i] == '\\') {
            last = i;
        }
    }
    /* keep the separator; drop the basename. No separator = bare name on
     * PATH/cwd - fall back to cwd-relative files. */
    sPortRootDir[last + 1] = '\0';

    /* always-on: log where eeprom.bin/midas.ini actually live this run */
    portConLogf("ge: root: %s\n", sPortRootDir[0] != '\0' ? sPortRootDir : "(cwd)");
}

const char *portRootPath(const char *name)
{
    static char path[1200];
    snprintf(path, sizeof(path), "%s%s", sPortRootDir, name);
    return path;
}

FILE *portRootFopen(const char *name, const char *mode)
{
    return fopen(portRootPath(name), mode);
}

/* Data/Logs/<name>, made on first use: the run log (ge_direct.log) and
 * crash.log live there, beside the saves and profiles, not next to the exe. */
const char *portLogPath(const char *name)
{
    static const char *const dirs[] = { "Data", "Data/Logs" };
    static char path[1200];
    int i;

    for (i = 0; i < 2; i++) {
#ifdef _WIN32
        _mkdir(portRootPath(dirs[i]));   /* fails harmlessly when it exists */
#else
        mkdir(portRootPath(dirs[i]), 0755);
#endif
    }
    snprintf(path, sizeof(path), "%sData/Logs/%s", sPortRootDir, name);
    return path;
}

/* The settings file was called ge.ini before the Project Midas rename: when
 * there is no midas.ini yet, rename an existing ge.ini once so players keep
 * their settings. */
static void portMigrateIni(void)
{
    char oldpath[1200];
    FILE *f = portRootFopen("midas.ini", "r");

    if (f != NULL) {
        fclose(f);
        return;
    }
    snprintf(oldpath, sizeof(oldpath), "%s", portRootPath("ge.ini"));
    if (rename(oldpath, portRootPath("midas.ini")) == 0) {
        portConLogf("ge: renamed ge.ini to midas.ini\n");
    }
}

#ifdef _WIN32
/* Direct-launch log: the exe is a GUI-subsystem program (no console window,
 * CMakeLists.txt), so a double-click, Steam or shell launch has nowhere to put
 * the `ge:` lines. Then stdout+stderr go to Data/Logs/ge_direct.log,
 * unbuffered like crash.log. Runs whose stderr is already a file or a pipe
 * (`> file`, the harness, a launcher) keep their output as is, and so does a
 * console build run from a shell's own console. The tilde console shows the
 * same lines in game. */
static void portDirectLaunchLog(void)
{
    DWORD pids[2];
    const char *path;
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const DWORD type = (err != NULL && err != INVALID_HANDLE_VALUE) ? GetFileType(err) : FILE_TYPE_UNKNOWN;

    if (getenv("GE_NODIRECTLOG") != NULL) {
        return;
    }
    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) {
        return;   /* redirected: keep it */
    }
    if (type == FILE_TYPE_CHAR && !(GetConsoleWindow() != NULL && GetConsoleProcessList(pids, 2) == 1)) {
        return;   /* a shell's console that outlives us */
    }
    path = portLogPath("ge_direct.log");
    /* A failed freopen CLOSES stderr (CRT contract), losing every later ge:
     * line and the console copy of crash dumps, so check the file is writable
     * before redirecting. */
    {
        FILE *t = fopen(path, "a");
        if (t == NULL) {
            portConLogf("ge: cannot write %s - output stays in this window\n", path);
            return;
        }
        fclose(t);
    }
    /* the console goes silent from here - say where the lines went, so a
     * blank window is not mistaken for a dead process */
    portConLogf("ge: output goes to %s (this window stays blank)\n", path);
    fflush(stderr);
    if (freopen(path, "w", stderr) == NULL) {
        /* lost the race after the test open: put stderr back on the console */
        freopen("CONOUT$", "w", stderr);
        portConLogf("ge: cannot write %s - output stays in this window\n", path);
        return;
    }
    setvbuf(stderr, NULL, _IONBF, 0);
    /* stdout SHARES stderr's descriptor (one file offset): a second
     * freopen of the same path gave two offsets and the streams overwrote
     * each other's lines. A GUI-subsystem launch starts stdout with no
     * descriptor (_fileno -2), which _dup2 refuses, and every printf line
     * (hitch lines, pacer banner) was lost - so open it on NUL first. */
    if (_fileno(stdout) < 0) {
        freopen("NUL", "w", stdout);
    }
    if (_fileno(stdout) >= 0 && _dup2(_fileno(stderr), _fileno(stdout)) == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
    }
    portConLogf("ge: direct launch (no console): output -> %s\n", path);
    portConLogf("ge: root: %s\n", sPortRootDir[0] != '\0' ? sPortRootDir : "(cwd)");
}
#endif

int main(int argc, char **argv)
{
    { /* before the first log line, so the tilde console's ring holds the
       * whole run from `ge: root:` onward (spec section 7) */
        extern void portConInit(void);
        portConInit();
    }
    portInitRootDir(argv[0]);
    portMigrateIni();

#ifdef GE_VIDEO
    /* GE_LISTPADS=1: GELauncher's controller probe - print SDL's device
     * list (index/GUID/mapped-or-raw/name + default-map tokens) and exit
     * before any window. GE_CAPTUREPAD=<idx>: wait for a press on that
     * device, print its token, exit - the launcher's rebind capture. */
    if (getenv("GE_LISTPADS") != NULL)
    {
        extern void portInputListPadsAndExit(void);
        portInputListPadsAndExit();
    }
    if (getenv("GE_CAPTUREPAD") != NULL)
    {
        extern void portInputCapturePadAndExit(const char *arg);
        portInputCapturePadAndExit(getenv("GE_CAPTUREPAD"));
    }
    if (getenv("GE_CALIBRATE") != NULL)
    {
        extern void portInputCalibratePadAndExit(const char *arg);
        portInputCalibratePadAndExit(getenv("GE_CALIBRATE"));
    }
    if (getenv("GE_RUMBLETEST") != NULL)
    {
        extern void portInputRumbleTestAndExit(const char *arg);
        portInputRumbleTestAndExit(getenv("GE_RUMBLETEST"));
    }
#endif

#ifdef _WIN32
    portDirectLaunchLog();
#endif

    /* Crash handlers and the stall watchdog are armed first, because everything
     * below (arena reservation, overlay check, SDL/GL, controller init) can
     * hang in a hooked process. The watchdog's 60 s boot grace covers all of
     * it. */
    signal(SIGSEGV, portCrashHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(portSehHandler);
    portArmWatchpoints(); /* GE_WATCH=1: see comment above */
    portArmStallWatchdog(); /* always on: hangs become backtraces */
    AddVectoredExceptionHandler(1, portFirstChanceAV); /* crashes report even if a filter swallows them */
#else
    signal(SIGBUS, portCrashHandler);
#endif
    signal(SIGFPE, portCrashHandler);
    signal(SIGILL, portCrashHandler);
#ifdef _WIN32
    /* GE_STALLTEST=1: park the main thread here forever so the stall
     * watchdog's boot-grace path + loader-lock-free module naming can be
     * exercised on demand (expect STALL samples naming ntdll/KERNELBASE
     * and an exit 3 about 60 s in). Diagnostic only. */
    if (getenv("GE_STALLTEST") != NULL) {
        portConLogf("ge: GE_STALLTEST: parking the main thread\n");
        for (;;) {
            Sleep(1000);
        }
    }
#endif

    /* Pin both 64 MB arenas NOW (needed for Steam launches), while the
     * 2 GB space is still empty - see portAllocHighReserve (pcsched.c).
     * Same order as the lazy path used to produce (memp 0x20000000 hint
     * first, then the high arena from 0x30000000). */
    {
        void *portGetMempArena(u32 *outsize);
        extern void portAllocHighReserve(void);
        u32 arenasize;

        (void) portGetMempArena(&arenasize);
        portAllocHighReserve();
    }

#ifdef _WIN32
    /* always-on: Steam's injected overlay is a prime suspect in crash
     * reports - say whether it is inside this process. GetModuleHandle
     * does not load anything. */
    portConLogf("ge: steam overlay: %s\n",
            GetModuleHandleA("gameoverlayrenderer.dll") != NULL ? "INJECTED (gameoverlayrenderer.dll)" : "no");
#endif

    printf("ge007 pc port: booting (GE_FRAMES=n limits the run)\n");
    fflush(stdout);

    /* The user's ROM (port/src/romdata.c): argv[1] drag-and-drop, --rom-file,
     * GE_ROM, midas.ini [data] rom=, or ./Data. Runs after the arena reservations
     * (the ROM takes its own high mapping next) and before anything that
     * consumes it. */
    {
        extern s32 romdataInit(int argc, char **argv);
        romdataInit(argc, argv);
    }

    osInitialize();

    /* build the animation segment images from the compiled-in blobs BEFORE
     * the game's alloc_load_expand_ani_table copies/fixes them up */
    {
        extern void portAnimDataInit(void);
        portAnimDataInit();
    }

#ifdef GE_VIDEO
    {
        extern s32 videoInit(void);
        extern void videoParseBootArgs(int argc, char **argv);

        /* -n64_aa / -audio_pump / -audio_log: read before init so videoInit can
         * apply them ahead of the renderer (Classic Mode forces msaa). */
        videoParseBootArgs(argc, argv);
        videoInit();
    }
#endif

    mainproc(NULL);

    /* mainproc only returns if the game's main loop exits */
    printf("ge007 pc port: game loop returned - clean exit\n");
    return 0;
}
