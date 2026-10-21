/**
 * port_mods.c - the native C mod host (API: port/include/midas_mod.h).
 *
 * At boot (main.c, after the arenas are pinned and before midas.ini is read)
 * every Data/Mods/<dir>/ holding a mod.ini and <dir>.so / <dir>.dll is loaded
 * in `order` (then name) and its midas_mod_init called. Mods only register
 * things during init; the hook runners below call them from then on.
 *
 * mod.ini (all optional):
 *   order=50      lower loads (and hooks at equal priority run) first
 *   enabled=1     0 skips the mod unless a load list names it
 *
 * Which mods load: GE_MODS=<list>, else midas.ini [mods] load=<list>, else
 * every enabled one. <list> is names separated by commas or spaces (exactly
 * those load, in mod.ini order), all / * (every enabled mod), or none / empty.
 * GE_NOMODS=1 skips loading altogether. GE_MODDBG=1 logs every registration.
 * Mods are never unloaded: hooks, options and binds hold pointers into them.
 */
/* first: winsock's ntohl prototype must come before bondconstants.h's macro */
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ultra64.h>
#include <bondconstants.h>

#include "midas_mod.h"
#include "port_mods.h"

extern void portConLogf(const char *fmt, ...);
extern void portConPrintf(s32 showmsg, const char *fmt, ...);
extern const char *portRootPath(const char *name);
extern void *portAllocHigh(u32 size);
/* input_pc.c; absent in headless builds */
extern void portInputModActionName(s32 slot, const char *name) __attribute__((weak));
extern s32 portJoyPortModTake(s32 slot);
extern s32 portJoyPortModDown(s32 slot);

#define MODS_MAX        32
#define MODS_HOOKS_MAX  16
#define MODS_OPTS_MAX   32
#define MODS_ACTS_MAX   8   /* joy_port.c PORTACT_MOD_FIRST..LAST */
#define MODS_CMDS_MAX   32
#define MODS_STRS_MAX   256 /* langGet bank 45's 10-bit slot */
#define MODS_BANK       45

typedef struct
{
    char name[32];
    s32 order;
    void *handle;
} Mod;

typedef struct
{
    void *fn;
    s32 prio;
    s32 mod;
} Hook;

typedef struct
{
    char key[32];
    char label[40];   /* row text, with the menus' trailing "\n" */
    s32 isint, lo, hi, step;
    s32 value;
} ModOpt;

typedef struct
{
    char name[24];
    char label[40];
    char defkeys[40];
} ModAct;

typedef struct
{
    char name[24];
    char help[96];
    s32 (*fn)(const char *args);
    s32 mod;
} ModCmd;

static Mod sMods[MODS_MAX];
static s32 sModCount;
static s32 sCurMod = -1;   /* whose code is running: init, a hook or a command */

static Hook sHooks[MIDAS_HOOK_COUNT][MODS_HOOKS_MAX];
static s32 sHookN[MIDAS_HOOK_COUNT];

static ModOpt sOpts[MODS_OPTS_MAX];
static s32 sOptN;
static ModAct sActs[MODS_ACTS_MAX];
static s32 sActN;
static ModCmd sCmds[MODS_CMDS_MAX];
static s32 sCmdN;
static const char *sStrs[MODS_STRS_MAX];
static s32 sStrN;

/* Items no level places, handed to mod weapons. The asset tables stay
 * N64-exact; a mod re-points the gitem_structs row it gets. */
static const s32 sFreeItems[] = { ITEM_SILVERWPPK };
#define MODS_ITEMS ((s32) (sizeof(sFreeItems) / sizeof(sFreeItems[0])))
static char sItemOwner[MODS_ITEMS][32];
static char sItemDir[ITEM_IDS_MAX][32];

static s32 modDbg(void)
{
    static s32 dbg = -1;

    if (dbg < 0)
    {
        dbg = getenv("GE_MODDBG") != NULL;
    }
    return dbg;
}

static const char *curName(void)
{
    return (sCurMod >= 0) ? sMods[sCurMod].name : "mods";
}

static void copyStr(char *dst, s32 n, const char *src)
{
    snprintf(dst, (size_t) n, "%s", (src != NULL) ? src : "");
}

/* ---- host services -------------------------------------------------------- */

static void hostLog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    portConLogf("ge: %s: %s", curName(), buf);
}

static s32 hostHook(MidasHookId id, void *fn, s32 priority)
{
    s32 n, i;

    if ((u32) id >= MIDAS_HOOK_COUNT || fn == NULL || sHookN[id] >= MODS_HOOKS_MAX)
    {
        portConLogf("ge: %s: hook %d refused\n", curName(), (int) id);
        return 0;
    }
    /* sorted by priority, load order breaking ties (insertion is stable) */
    n = sHookN[id];
    for (i = n; i > 0 && sHooks[id][i - 1].prio > priority; i--)
    {
        sHooks[id][i] = sHooks[id][i - 1];
    }
    sHooks[id][i].fn = fn;
    sHooks[id][i].prio = priority;
    sHooks[id][i].mod = sCurMod;
    sHookN[id]++;
    if (modDbg())
    {
        portConLogf("ge: %s: hook %d priority %d\n", curName(), (int) id, (int) priority);
    }
    return 1;
}

static ModOpt *optFind(const char *key)
{
    s32 i;

    for (i = 0; key != NULL && i < sOptN; i++)
    {
        if (strcmp(sOpts[i].key, key) == 0)
        {
            return &sOpts[i];
        }
    }
    return NULL;
}

static const s32 *optAdd(const char *key, const char *label, s32 def, s32 isint, s32 lo, s32 hi, s32 step)
{
    ModOpt *o = optFind(key);

    if (o != NULL)
    {
        return &o->value;
    }
    if (key == NULL || key[0] == '\0' || sOptN >= MODS_OPTS_MAX)
    {
        portConLogf("ge: %s: option \"%s\" refused\n", curName(), key ? key : "");
        return NULL;
    }
    o = &sOpts[sOptN++];
    copyStr(o->key, sizeof(o->key), key);
    snprintf(o->label, sizeof(o->label), "%s\n", (label != NULL) ? label : key);
    o->isint = isint;
    o->lo = lo;
    o->hi = hi;
    o->step = (step > 0) ? step : 1;
    o->value = def;
    if (modDbg())
    {
        portConLogf("ge: %s: option %s=%d\n", curName(), o->key, (int) def);
    }
    return &o->value;
}

static const s32 *hostOptionBool(const char *key, const char *label, s32 def)
{
    return optAdd(key, label, def != 0, 0, 0, 1, 1);
}

static const s32 *hostOptionInt(const char *key, const char *label, s32 def, s32 lo, s32 hi, s32 step)
{
    return optAdd(key, label, def, 1, lo, hi, step);
}

static s32 hostAction(const char *name, const char *label, const char *defkeys)
{
    s32 i;

    for (i = 0; name != NULL && i < sActN; i++)
    {
        if (strcmp(sActs[i].name, name) == 0)
        {
            return i;
        }
    }
    if (name == NULL || name[0] == '\0' || sActN >= MODS_ACTS_MAX)
    {
        portConLogf("ge: %s: action \"%s\" refused (%d slots)\n", curName(), name ? name : "", MODS_ACTS_MAX);
        return -1;
    }
    copyStr(sActs[sActN].name, sizeof(sActs[sActN].name), name);
    snprintf(sActs[sActN].label, sizeof(sActs[sActN].label), "%s\n", (label != NULL) ? label : name);
    copyStr(sActs[sActN].defkeys, sizeof(sActs[sActN].defkeys), defkeys);
    if (portInputModActionName != NULL)
    {
        portInputModActionName(sActN, sActs[sActN].name);
    }
    return sActN++;
}

static s32 hostActionPressed(s32 h)
{
    return (h >= 0 && h < sActN) ? portJoyPortModTake(h) : 0;
}

static s32 hostActionHeld(s32 h)
{
    return (h >= 0 && h < sActN) ? portJoyPortModDown(h) : 0;
}

static s32 hostCommand(const char *name, s32 (*fn)(const char *args), const char *help)
{
    if (name == NULL || fn == NULL || sCmdN >= MODS_CMDS_MAX)
    {
        return 0;
    }
    copyStr(sCmds[sCmdN].name, sizeof(sCmds[sCmdN].name), name);
    copyStr(sCmds[sCmdN].help, sizeof(sCmds[sCmdN].help), help);
    sCmds[sCmdN].fn = fn;
    sCmds[sCmdN].mod = sCurMod;
    sCmdN++;
    return 1;
}

static u16 hostString(const char *text)
{
    s32 i;

    for (i = 0; i < sStrN; i++)
    {
        if (sStrs[i] == text || strcmp(sStrs[i], text) == 0)
        {
            return (u16) ((MODS_BANK << 10) | i);
        }
    }
    if (sStrN >= MODS_STRS_MAX)
    {
        portConLogf("ge: %s: string table full\n", curName());
        return (u16) ((MODS_BANK << 10) | 0);
    }
    sStrs[sStrN] = text;
    return (u16) ((MODS_BANK << 10) | sStrN++);
}

static s32 hostClaimItem(const char *name)
{
    s32 i;

    for (i = 0; name != NULL && i < MODS_ITEMS; i++)
    {
        if (strcmp(sItemOwner[i], name) == 0)
        {
            return sFreeItems[i];
        }
    }
    for (i = 0; name != NULL && i < MODS_ITEMS; i++)
    {
        if (sItemOwner[i][0] == '\0')
        {
            copyStr(sItemOwner[i], sizeof(sItemOwner[i]), name);
            if (modDbg())
            {
                portConLogf("ge: %s: item %s = %d\n", curName(), name, (int) sFreeItems[i]);
            }
            return sFreeItems[i];
        }
    }
    portConLogf("ge: %s: no free item for \"%s\"\n", curName(), name ? name : "");
    return -1;
}

static void hostItemModelDir(s32 item, const char *dir)
{
    if (item >= 0 && item < ITEM_IDS_MAX)
    {
        copyStr(sItemDir[item], sizeof(sItemDir[item]), dir);
    }
}

static void *modSym(void *handle, const char *name)
{
#ifdef _WIN32
    return (void *) GetProcAddress((HMODULE) handle, name);
#else
    return dlsym(handle, name);
#endif
}

static void *hostSymbol(const char *mod, const char *name)
{
    s32 i;

    for (i = 0; mod != NULL && name != NULL && i < sModCount; i++)
    {
        if (strcmp(sMods[i].name, mod) == 0)
        {
            return modSym(sMods[i].handle, name);
        }
    }
    return NULL;
}

static void *hostAllocHigh(u32 size)
{
    return portAllocHigh(size);
}

static const MidasHost sHost = {
    MIDAS_ABI,
    sizeof(MidasHost),
    hostLog,
    hostHook,
    hostOptionBool,
    hostOptionInt,
    hostAction,
    hostActionPressed,
    hostActionHeld,
    hostCommand,
    hostString,
    hostClaimItem,
    hostItemModelDir,
    hostSymbol,
    hostAllocHigh,
};

/* ---- loading -------------------------------------------------------------- */

typedef struct
{
    char dir[32];
    s32 order;
} ModFound;

/* ---- which mods: GE_MODS, else midas.ini [mods] load= ---------------------- */

enum { SEL_ALL, SEL_LIST, SEL_NONE };
static s32 sSel = SEL_ALL;
static char sSelList[512];
static const char *sSelSrc = "default";

static char *modTrim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t')
    {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
    {
        *--e = '\0';
    }
    return s;
}

/* The next name in a comma / space separated list; 0 at the end. */
static s32 selNext(const char **p, char *tok, s32 n)
{
    s32 k = 0;

    while (**p == ',' || **p == ' ' || **p == '\t')
    {
        (*p)++;
    }
    if (**p == '\0')
    {
        return 0;
    }
    while (**p != '\0' && **p != ',' && **p != ' ' && **p != '\t')
    {
        if (k < n - 1)
        {
            tok[k++] = **p;
        }
        (*p)++;
    }
    tok[k] = '\0';
    return 1;
}

/* folder names are case-blind on Windows, so the list is everywhere */
static s32 selEqI(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        char ca = *a++, cb = *b++;

        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
        {
            return 0;
        }
    }
    return *a == *b;
}

static s32 selHas(const char *dir)
{
    const char *p = sSelList;
    char tok[32];

    while (selNext(&p, tok, (s32) sizeof(tok)))
    {
        if (selEqI(tok, dir))
        {
            return 1;
        }
    }
    return 0;
}

/* "" / none = no mods, all / * = every enabled one, else exactly the names */
static void selSet(const char *value, const char *src)
{
    char tmp[sizeof(sSelList)];
    char *v;

    snprintf(tmp, sizeof(tmp), "%s", value);
    v = modTrim(tmp);
    sSelSrc = src;
    if (v[0] == '\0' || selEqI(v, "none"))
    {
        sSel = SEL_NONE;
    }
    else if (selEqI(v, "all") || strcmp(v, "*") == 0)
    {
        sSel = SEL_ALL;
    }
    else
    {
        sSel = SEL_LIST;
        snprintf(sSelList, sizeof(sSelList), "%s", v);
    }
}

/* Mods load before video.c reads midas.ini (and its reader is section-blind),
 * so [mods] has its own section-aware reader, like romdata.c's [data]. */
static void selReadIni(void)
{
    FILE *f = fopen(portRootPath("midas.ini"), "r");
    char line[600];
    s32 inmods = 0;

    if (f == NULL)
    {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *s = modTrim(line);
        char *eq;

        if (s[0] == '[')
        {
            inmods = (strncmp(s, "[mods]", 6) == 0);
            continue;
        }
        if (!inmods || (eq = strchr(s, '=')) == NULL)
        {
            continue;
        }
        *eq = '\0';
        if (selEqI(modTrim(s), "load"))
        {
            selSet(eq + 1, "midas.ini [mods] load=");
        }
    }
    fclose(f);
}

/** video.c's parser: [mods] keys are read here, not "unknown controls". */
s32 portModsIsIniKey(const char *key)
{
    return selEqI(key, "load");
}

/** Data/Mods/<dir>/mod.ini; 0 = no mod here. */
static s32 modReadIni(const char *dir, s32 *order, s32 *enabledOut)
{
    char rel[128];
    char line[160];
    s32 enabled = 1;
    FILE *f;

    snprintf(rel, sizeof(rel), "Data/Mods/%s/mod.ini", dir);
    f = fopen(portRootPath(rel), "r");
    if (f == NULL)
    {
        return 0;
    }
    *order = 50;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (strncmp(line, "order=", 6) == 0)
        {
            *order = atoi(line + 6);
        }
        else if (strncmp(line, "enabled=", 8) == 0)
        {
            enabled = atoi(line + 8);
        }
    }
    fclose(f);
    *enabledOut = enabled != 0;
    return 1;
}

static s32 modFoundCmp(const void *a, const void *b)
{
    const ModFound *x = (const ModFound *) a;
    const ModFound *y = (const ModFound *) b;

    return (x->order != y->order) ? (x->order - y->order) : strcmp(x->dir, y->dir);
}

static void modConsider(ModFound *found, s32 *n, const char *dir)
{
    s32 order;
    s32 enabled;

    if (dir[0] == '.' || *n >= MODS_MAX || strlen(dir) >= sizeof(found[0].dir))
    {
        return;
    }
    if (!modReadIni(dir, &order, &enabled))
    {
        return;
    }
    /* a list loads what it names, enabled=0 or not; otherwise mod.ini decides */
    if ((sSel == SEL_LIST) ? selHas(dir) : enabled)
    {
        copyStr(found[*n].dir, sizeof(found[*n].dir), dir);
        found[*n].order = order;
        (*n)++;
    }
}

static s32 modScan(ModFound *found)
{
    s32 n = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(portRootPath("Data/Mods/*"), &fd);

    if (h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            modConsider(found, &n, fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(portRootPath("Data/Mods"));
    struct dirent *e;

    if (d == NULL)
    {
        return 0;
    }
    while ((e = readdir(d)) != NULL)
    {
        modConsider(found, &n, e->d_name);
    }
    closedir(d);
#endif
    qsort(found, (size_t) n, sizeof(found[0]), modFoundCmp);
    return n;
}

static void *modOpen(const char *dir, char *err, s32 errn)
{
    char rel[160];
    void *h;

#ifdef _WIN32
    snprintf(rel, sizeof(rel), "Data/Mods/%s/%s.dll", dir, dir);
    h = (void *) LoadLibraryA(portRootPath(rel));
    if (h == NULL)
    {
        snprintf(err, (size_t) errn, "LoadLibrary failed (error %lu)", (unsigned long) GetLastError());
        return NULL;
    }
    /* an HMODULE is the image base: below 0x10000000 its data would read as
     * N64 segment words in a display list (port_hostptr.h) */
    if ((uintptr_t) h < 0x10000000u)
    {
        FreeLibrary((HMODULE) h);
        snprintf(err, (size_t) errn, "loaded below 0x10000000 (rebuild with midas_add_mod's image base)");
        return NULL;
    }
#else
    snprintf(rel, sizeof(rel), "Data/Mods/%s/%s.so", dir, dir);
    h = dlopen(portRootPath(rel), RTLD_NOW | RTLD_LOCAL);
    if (h == NULL)
    {
        snprintf(err, (size_t) errn, "%s", dlerror());
        return NULL;
    }
#endif
    return h;
}

static void modClose(void *h)
{
#ifdef _WIN32
    FreeLibrary((HMODULE) h);
#else
    dlclose(h);
#endif
}

void portModsLoad(void)
{
    ModFound found[MODS_MAX];
    s32 n, i;

    if (getenv("GE_NOMODS") != NULL)
    {
        portConLogf("ge: mods: GE_NOMODS set, none loaded\n");
        return;
    }
    selReadIni();
    if (getenv("GE_MODS") != NULL)
    {
        selSet(getenv("GE_MODS"), "GE_MODS");
    }
    if (sSel == SEL_NONE)
    {
        portConLogf("ge: mods: none selected (%s)\n", sSelSrc);
        return;
    }
    n = modScan(found);
    if (sSel == SEL_LIST)
    {
        const char *p = sSelList;
        char tok[32];

        portConLogf("ge: mods: loading only %s (%s)\n", sSelList, sSelSrc);
        while (selNext(&p, tok, (s32) sizeof(tok)))
        {
            for (i = 0; i < n && !selEqI(found[i].dir, tok); i++)
            {
            }
            if (i == n)
            {
                portConLogf("ge: mods: %s: no Data/Mods/%s/mod.ini\n", tok, tok);
            }
        }
    }
    for (i = 0; i < n; i++)
    {
        char err[256];
        void *h = modOpen(found[i].dir, err, (s32) sizeof(err));
        const u32 *abi;
        MidasModInit init;
        Mod *m;

        if (h == NULL)
        {
            portConLogf("ge: mods: %s: %s\n", found[i].dir, err);
            continue;
        }
        abi = (const u32 *) modSym(h, "midas_mod_abi");
        init = (MidasModInit) modSym(h, "midas_mod_init");
        if (abi == NULL || init == NULL)
        {
            portConLogf("ge: mods: %s: not a mod (needs MIDAS_MOD_ABI_EXPORT and midas_mod_init)\n", found[i].dir);
            modClose(h);
            continue;
        }
        if (*abi != MIDAS_ABI)
        {
            portConLogf("ge: mods: %s: built for mod ABI %u, this game is %u - rebuild it\n",
                        found[i].dir, (unsigned) *abi, (unsigned) MIDAS_ABI);
            modClose(h);
            continue;
        }
        m = &sMods[sModCount];
        copyStr(m->name, sizeof(m->name), found[i].dir);
        m->order = found[i].order;
        m->handle = h;
        sCurMod = sModCount++;
        /* a refusing mod keeps what it registered and stays mapped: the
         * registries may already point into it */
        if (!init(&sHost))
        {
            portConLogf("ge: mods: %s: init declined\n", m->name);
        }
        else
        {
            portConLogf("ge: mods: %s loaded (order %d)\n", m->name, (int) m->order);
        }
        sCurMod = -1;
    }
    if (sModCount == 0)
    {
        portConLogf("ge: mods: none in Data/Mods\n");
    }
}

/* ---- hook runners --------------------------------------------------------- */

#define EACH_HOOK(id, h) \
    for (Hook *h = sHooks[id]; h < sHooks[id] + sHookN[id] && ((sCurMod = h->mod), 1); h++)

void portModsLevelStart(void)
{
    EACH_HOOK(MIDAS_HOOK_LEVEL_START, h)
    {
        ((void (*)(void)) h->fn)();
    }
    sCurMod = -1;
}

void portModsPlayerMove(struct coord3d *move, f32 walkx, f32 walkz)
{
    EACH_HOOK(MIDAS_HOOK_PLAYER_MOVE, h)
    {
        ((void (*)(struct coord3d *, f32, f32)) h->fn)(move, walkx, walkz);
    }
    sCurMod = -1;
}

void portModsPlayerMoved(f32 dx, f32 dz)
{
    EACH_HOOK(MIDAS_HOOK_PLAYER_MOVED, h)
    {
        ((void (*)(f32, f32)) h->fn)(dx, dz);
    }
    sCurMod = -1;
}

void portModsPlayerStepWindow(struct StandTile *floor, f32 *height, f32 *always_30)
{
    EACH_HOOK(MIDAS_HOOK_PLAYER_STEP_WINDOW, h)
    {
        ((void (*)(struct StandTile *, f32 *, f32 *)) h->fn)(floor, height, always_30);
    }
    sCurMod = -1;
}

s32 portModsPlayerBlocked(struct coord3d *next_pos)
{
    s32 done = 0;

    EACH_HOOK(MIDAS_HOOK_PLAYER_BLOCKED, h)
    {
        if ((done = ((s32 (*)(struct coord3d *)) h->fn)(next_pos)) != 0)
        {
            break;
        }
    }
    sCurMod = -1;
    return done;
}

f32 portModsPlayerFloor(f32 floor)
{
    EACH_HOOK(MIDAS_HOOK_PLAYER_FLOOR, h)
    {
        floor = ((f32 (*)(f32)) h->fn)(floor);
    }
    sCurMod = -1;
    return floor;
}

void portModsPlayerY(s32 use_stanHeight)
{
    EACH_HOOK(MIDAS_HOOK_PLAYER_Y, h)
    {
        ((void (*)(s32)) h->fn)(use_stanHeight);
    }
    sCurMod = -1;
}

s32 portModsWeaponFire(s32 hand, s32 item)
{
    s32 done = 0;

    EACH_HOOK(MIDAS_HOOK_WEAPON_FIRE, h)
    {
        if ((done = ((s32 (*)(s32, s32)) h->fn)(hand, item)) != 0)
        {
            break;
        }
    }
    sCurMod = -1;
    return done;
}

s32 portModsWeaponAim(s32 pressed)
{
    s32 done = 0;

    EACH_HOOK(MIDAS_HOOK_WEAPON_AIM, h)
    {
        if ((done = ((s32 (*)(s32)) h->fn)(pressed)) != 0)
        {
            break;
        }
    }
    sCurMod = -1;
    return done;
}

Gfx *portModsRenderWorld(Gfx *gdl)
{
    EACH_HOOK(MIDAS_HOOK_RENDER_WORLD, h)
    {
        gdl = ((Gfx *(*)(Gfx *)) h->fn)(gdl);
    }
    sCurMod = -1;
    return gdl;
}

Gfx *portModsRenderHud(Gfx *gdl)
{
    EACH_HOOK(MIDAS_HOOK_RENDER_HUD, h)
    {
        gdl = ((Gfx *(*)(Gfx *)) h->fn)(gdl);
    }
    sCurMod = -1;
    return gdl;
}

/* ---- registries the engine reads ------------------------------------------ */

s32 portModsOptionCount(void)
{
    return sOptN;
}

const char *portModsOptionKey(s32 i)
{
    return (i >= 0 && i < sOptN) ? sOpts[i].key : NULL;
}

const char *portModsOptionLabel(s32 i)
{
    return (i >= 0 && i < sOptN) ? sOpts[i].label : "?\n";
}

s32 portModsOptionIsInt(s32 i, s32 *lo, s32 *hi, s32 *step)
{
    if (i < 0 || i >= sOptN || !sOpts[i].isint)
    {
        return 0;
    }
    *lo = sOpts[i].lo;
    *hi = sOpts[i].hi;
    *step = sOpts[i].step;
    return 1;
}

/** midas.ini parse (joy_port.c portJoyPortSetOption falls through to here). */
s32 portModsSetOption(const char *key, const char *value)
{
    ModOpt *o = optFind(key);

    if (o == NULL)
    {
        return 0;
    }
    o->value = o->isint ? atoi(value) : (atoi(value) != 0);
    return 1;
}

s32 portModsOptionGet(const char *key, s32 *out)
{
    ModOpt *o = optFind(key);

    if (o == NULL)
    {
        return 0;
    }
    *out = o->value;
    return 1;
}

s32 portModsOptionSet(const char *key, s32 v)
{
    ModOpt *o = optFind(key);

    if (o == NULL)
    {
        return 0;
    }
    o->value = o->isint ? v : (v != 0);
    return 1;
}

s32 portModsActionCount(void)
{
    return sActN;
}

const char *portModsActionName(s32 i)
{
    return (i >= 0 && i < sActN) ? sActs[i].name : NULL;
}

const char *portModsActionLabel(s32 i)
{
    return (i >= 0 && i < sActN) ? sActs[i].label : "?\n";
}

const char *portModsActionDefault(s32 i)
{
    return (i >= 0 && i < sActN) ? sActs[i].defkeys : "";
}

s32 portModsCommand(const char *cmd, const char *args)
{
    s32 i;

    for (i = 0; cmd != NULL && i < sCmdN; i++)
    {
        if (strcmp(sCmds[i].name, cmd) == 0)
        {
            s32 ok;

            sCurMod = sCmds[i].mod;
            ok = sCmds[i].fn((args != NULL) ? args : "");
            sCurMod = -1;
            if (!ok)
            {
                portConPrintf(0, "usage: %s\n", sCmds[i].help);
            }
            return 1;
        }
    }
    return 0;
}

void portModsCommandHelp(void (*println)(const char *line))
{
    char line[160];
    s32 i;

    for (i = 0; i < sCmdN; i++)
    {
        snprintf(line, sizeof(line), "  %-21s (%s)", sCmds[i].help, sMods[sCmds[i].mod].name);
        println(line);
    }
}

void portModsList(void (*println)(const char *line))
{
    char line[160];
    s32 i, k, hooks;

    snprintf(line, sizeof(line), "selection: %s (%s)",
             (sSel == SEL_LIST) ? sSelList : (sSel == SEL_NONE) ? "none" : "all enabled", sSelSrc);
    println(line);
    if (sModCount == 0)
    {
        println("no mods loaded (Data/Mods/<name>/<name>.so|.dll + mod.ini)");
        return;
    }
    for (i = 0; i < sModCount; i++)
    {
        hooks = 0;
        for (k = 0; k < MIDAS_HOOK_COUNT; k++)
        {
            s32 j;

            for (j = 0; j < sHookN[k]; j++)
            {
                hooks += (sHooks[k][j].mod == i);
            }
        }
        snprintf(line, sizeof(line), "  %-16s order %d, %d hook%s", sMods[i].name, (int) sMods[i].order,
                 (int) hooks, (hooks == 1) ? "" : "s");
        println(line);
    }
}

const char *portModsString(s32 slot)
{
    return (slot >= 0 && slot < sStrN) ? sStrs[slot] : "\n";
}

const char *portModsItemModelDir(s32 item)
{
    return (item >= 0 && item < ITEM_IDS_MAX && sItemDir[item][0] != '\0') ? sItemDir[item] : NULL;
}
