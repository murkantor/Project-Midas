/**
 * port_profiles.c - PLAYER PROFILES for the multiplayer setup's Player Options
 * (docs/port/input-profiles-spec.md). A profile's name replaces "P1".."P4" in
 * game, and loading one restores that person's control settings.
 *
 * A profile is a person's name plus the control settings that belong to them
 * rather than to a pad slot, in one small text file:
 *
 *     Data/Profiles/<NAME>.geprofile   (older builds used ./Profiles; moved on first use)
 *
 *     [profile]
 *     name=MURK
 *     version=1
 *     [controls]
 *     inputmode=port                   ; padN_inputmode: port / n64 (the Style row's "Port")
 *     style=auto                       ; padN_style: auto, 1.1 .. 2.4
 *     look_updown=0                    ; padN_look_updown: 1 upright, 0 reverse
 *     look_ahead=1                     ; padN_look_ahead: Rare's Look Ahead (v2)
 *     auto_aim=1                       ; padN_auto_aim: Rare's Auto-Aim, used when MP Force Auto Aim is PP (v3)
 *     crosshair_color=A500FF           ; playerN_crosshair_color: default / RRGGBB / - (follow) (v4)
 *     layout=Xbox Pro                  ; padN_layout: a bind config by NAME
 *     detonate_use_reload=1            ; padN_detonate_use_reload
 *     [multiplayer]
 *     character=12                     ; the MP setup's pick, Rare's mp_chr_setup index
 *
 * RULE: a profile holds everything on the Player Options panel, plus the
 * character. A row added to the panel needs its key added in both
 * portProfileSave and portProfileLoad.
 *
 * midas.ini stays the live state. Loading a profile into player N writes that
 * player's padN_* keys through the ordinary ini writer, and saving reads them
 * back; nothing in the input path knows a profile exists. midas.ini only records
 * who sits where ([input] playerN_profile=<NAME>) so the name survives a
 * restart; it does not re-apply the file at boot.
 *
 * Versioned: any layout change bumps PROFILE_VERSION. A missing key leaves that
 * setting alone and an unknown key is ignored, so files load across older and
 * newer builds.
 *
 * The name IS the file name; name= inside the file is only for humans. The
 * setup keyboard can only type 0-9 A-Z a-z space - ! . so its names are legal
 * on Windows and Linux; a hand-made file lists as it is spelt.
 *
 * Read when touching: the MP setup's Player Options (front.c, menu11 port
 * arm), the in-match player label (mpmenu.c), docs/port/mp-player-options.md.
 */
#ifndef _WIN32
/* 32-bit builds on filesystems with 64-bit inodes or directory offsets (WSL
 * drvfs, NFS/SMB, XFS inode64) get EOVERFLOW from plain readdir() and an empty
 * list, so enable large-file support as romdata.c does. Must precede every
 * include. */
#define _FILE_OFFSET_BITS 64
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h> /* _mkdir */
#endif
#include <ultra64.h>
void portProfileClear(s32 player);

extern const char *portRootPath(const char *name); /* main.c: <exe dir>/name */
extern void portConLogf(const char *fmt, ...);       /* port_console.c */
extern s32 portVideoIniSetKey(const char *key, const char *value);

/* input_pc.c - by pad SLOT, which is what a player is */
extern s32 portInputSlotDevice(s32 slot);
extern s32 portInputStyleRaw(s32 slot);
extern const char *portInputStyleName(s32 code);
extern s32 portInputStyleCode(const char *name);
extern void portInputDeviceStyleSet(s32 dev, s32 style);
extern s32 portInputDeviceModeIsN64(s32 dev);
extern void portInputDeviceModeSet(s32 dev, s32 isN64);
extern s32 portPlayerLookUpright(s32 player);   /* options.c: 1 upright, 0 reverse, the player's own or the global */
extern s32 portPlayerLookAhead(s32 player);     /* options.c: 1 on, 0 off, the player's own or the global */
extern s32 portPlayerAutoAim(s32 player);       /* options.c: the same for Auto-Aim */
extern s32 portPlayerCrosshairOwn(s32 player);  /* video.c: -2 none, -1 red, else 0xRRGGBB */
extern s32 portInputLayoutCount(void);
extern const char *portInputLayoutName(s32 i);
extern s32 portInputLayoutApply(s32 dev, s32 i);
extern s32 portInputLayoutFitsSlot(s32 slot, s32 i);
extern const char *portInputLayoutActive(s32 dev);
extern void portInputLayoutReconcileMode(s32 dev);
/* front.c - the MP setup's character pick per player: an index into
 * mp_chr_setup[] (Rare's 64 plus the port's outfits), -1 until the setup has
 * been opened once */
extern s32 player_char[4];
extern s32 portMpCharCount(void);
#define PROFILE_CHARACTERS portMpCharCount()
/* joy_port.c */
extern s32 portJoyPortPadOptGet(s32 pad, s32 which);
extern void portJoyPortPadOptSet(s32 pad, s32 which, s32 v);

#define PROFILE_DIR      "Data/Profiles"   /* under Data, with the saves */
#define PROFILE_DIR_OLD  "Profiles"        /* older builds: next to ge.exe */
#define PROFILE_EXT      ".geprofile"
#define PROFILE_VERSION  4   /* 2: look_ahead; 3: auto_aim; 4: crosshair_color */
#define PROFILE_MAX      32
/* TEN characters: the name replaces "P1" over the in-match KILLS and LOSSES
 * columns (mpmenu.c), where "<ten wide letters> LOSSES" is what still fits. The
 * buffer is longer so hand-named files list as spelt, and a name too long even
 * for it is left out of the list and logged, because a clipped name matches no
 * file. */
#define PROFILE_NAME_TYPED 10
#define PROFILE_NAME_MAX   24
#define PROFILE_PLAYERS    4

static char sNames[PROFILE_MAX][PROFILE_NAME_MAX];
static s32  sCount = -1;                                  /* -1 = not scanned */
static char sActive[PROFILE_PLAYERS][PROFILE_NAME_MAX];   /* "" = none */

s32 portProfileNameMax(void)
{
    return PROFILE_NAME_TYPED;
}

static void profEnsureDir(void)
{
    static const char *const dirs[] = { "Data", PROFILE_DIR };
    s32 i;

    for (i = 0; i < 2; i++)
    {
        const char *path = portRootPath(dirs[i]);
        struct stat st;

        if (stat(path, &st) == 0)
        {
            continue;
        }
#ifdef _WIN32
        if (_mkdir(path) == 0)
#else
        if (mkdir(path, 0755) == 0)
#endif
        {
            portConLogf("ge: profiles: created %s\n", path);
        }
    }
}

/**
 * Move every file in `olddir` (exe-relative) ending in ext1 or ext2 (NULL =
 * none) into `newdir`, creating it (and Data/) if needed; a name already in
 * `newdir` is left where it was, so nothing is overwritten. It lives here
 * because this file has the dirent setup (the large-file define must precede
 * every include); input_pc.c's Devices/ uses it too. Returns how many moved.
 */
s32 portDataMigrate(const char *olddir, const char *newdir, const char *ext1, const char *ext2)
{
    DIR *d;
    struct dirent *de;
    s32 moved = 0;

    d = opendir(portRootPath(olddir));
    if (d == NULL)
    {
        return 0;
    }
    {   /* Data, then newdir */
        const char *dirs[2];
        s32 i;

        dirs[0] = "Data";
        dirs[1] = newdir;
        for (i = 0; i < 2; i++)
        {
            const char *path = portRootPath(dirs[i]);
            struct stat st;

            if (stat(path, &st) != 0)
            {
#ifdef _WIN32
                _mkdir(path);
#else
                mkdir(path, 0755);
#endif
            }
        }
    }
    while ((de = readdir(d)) != NULL)
    {
        const size_t len = strlen(de->d_name);
        const size_t l1 = (ext1 != NULL) ? strlen(ext1) : 0;
        const size_t l2 = (ext2 != NULL) ? strlen(ext2) : 0;
        char from[1024], to[1024], rel[512];
        struct stat st;

        if (!((l1 > 0 && len > l1 && strcmp(de->d_name + len - l1, ext1) == 0)
              || (l2 > 0 && len > l2 && strcmp(de->d_name + len - l2, ext2) == 0)))
        {
            continue;
        }
        if (strlen(olddir) + strlen(newdir) + len + 2 >= sizeof(rel))
        {
            continue;
        }
        snprintf(rel, sizeof(rel), "%s/%s", olddir, de->d_name);
        snprintf(from, sizeof(from), "%s", portRootPath(rel));
        snprintf(rel, sizeof(rel), "%s/%s", newdir, de->d_name);
        snprintf(to, sizeof(to), "%s", portRootPath(rel));
        if (stat(to, &st) != 0 && rename(from, to) == 0)
        {
            portConLogf("ge: moved %s/%s into %s\n", olddir, de->d_name, newdir);
            moved++;
        }
    }
    closedir(d);
    return moved;
}

/**
 * Every profile and its stats under the old ./Profiles move into Data/Profiles,
 * once, before anything reads either (profPath and port_mpstats.c's path both
 * call this), whatever touches a profile first at boot.
 */
void portProfileMigrate(void)
{
    static s32 done;

    if (!done)
    {
        done = 1;
        portDataMigrate(PROFILE_DIR_OLD, PROFILE_DIR, PROFILE_EXT, ".gestats");
    }
}

static int profNameCmp(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

/** Look in Profiles/ again. Called when the list is opened, not per frame. */
void portProfileRescan(void)
{
    DIR *d;
    struct dirent *de;
    const size_t extlen = strlen(PROFILE_EXT);
    s32 total = 0;

    portProfileMigrate();
    sCount = 0;
    d = opendir(portRootPath(PROFILE_DIR));
    if (d == NULL)
    {
        return;   /* no folder yet = no profiles; the first save makes it */
    }
    while ((de = readdir(d)) != NULL)
    {
        const size_t len = strlen(de->d_name);
        char name[PROFILE_NAME_MAX];
        s32 slot = sCount;

        if (len <= extlen || strcmp(de->d_name + len - extlen, PROFILE_EXT) != 0)
        {
            continue;
        }
        if (len - extlen >= PROFILE_NAME_MAX)
        {   /* a clipped name names no file - picking it would load nothing */
            portConLogf("ge: profiles: %s: name longer than %d, not listed\n", de->d_name, PROFILE_NAME_MAX - 1);
            continue;
        }
        snprintf(name, sizeof(name), "%.*s", (int) (len - extlen), de->d_name);
        total++;
        if (sCount == PROFILE_MAX)
        {   /* full: keep the alphabetically first PROFILE_MAX, not the first
             * ones readdir happened to hand over */
            s32 k;

            slot = 0;
            for (k = 1; k < PROFILE_MAX; k++)
            {
                if (strcmp(sNames[k], sNames[slot]) > 0)
                {
                    slot = k;
                }
            }
            if (strcmp(name, sNames[slot]) >= 0)
            {
                continue;
            }
        }
        else
        {
            sCount++;
        }
        snprintf(sNames[slot], PROFILE_NAME_MAX, "%s", name);
    }
    closedir(d);
    if (total > PROFILE_MAX)
    {
        portConLogf("ge: profiles: %d profiles in %s, only the first %d listed\n", (int) total, PROFILE_DIR, PROFILE_MAX);
    }
    /* readdir order is the filesystem's; alphabetical is what a list wants */
    qsort(sNames, (size_t) sCount, PROFILE_NAME_MAX, profNameCmp);
}

s32 portProfileCount(void)
{
    if (sCount < 0)
    {
        portProfileRescan();
    }
    return sCount;
}

const char *portProfileName(s32 i)
{
    return (i >= 0 && i < portProfileCount()) ? sNames[i] : "";
}

/** "" = this player has none. */
const char *portProfileActive(s32 player)
{
    return (player >= 0 && player < PROFILE_PLAYERS) ? sActive[player] : "";
}

/** The player's label in a match: the profile's name, or NULL = say "P<n>". */
const char *portProfilePlayerLabel(s32 player)
{
    const char *n = portProfileActive(player);

    return (n[0] != '\0') ? n : NULL;
}

static void profSetActive(s32 player, const char *name)
{
    char key[24];

    snprintf(sActive[player], PROFILE_NAME_MAX, "%s", name);
    snprintf(key, sizeof(key), "player%d_profile", (int) player + 1);
    portVideoIniSetKey(key, (name[0] != '\0') ? name : "none");
}

/* One seat per profile: two seats on one file would fight over its settings and
 * double-count its stats. Returns the seat holding NAME in any case (Windows
 * treats "Bob" and "BOB" as one file), ignoring seat `except`; -1 = nobody. */
static s32 profSeatOf(const char *name, s32 except)
{
    s32 p;

    for (p = 0; p < PROFILE_PLAYERS; p++)
    {
        const char *a = sActive[p];
        const char *b = name;

        if (p == except || a[0] == '\0')
        {
            continue;
        }
        while (*a != '\0' && *b != '\0'
               && ((*a >= 'a' && *a <= 'z') ? *a - 32 : *a) == ((*b >= 'a' && *b <= 'z') ? *b - 32 : *b))
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
        {
            return p;
        }
    }
    return -1;
}

/** front.c's list: the seat (0-3) that has NAME loaded, -1 = free. */
s32 portProfileSeatOf(const char *name)
{
    return profSeatOf(name, -1);
}

/**
 * Delete Profiles/<NAME> and its stats. Refused while ANOTHER seat has it
 * loaded; player N's own is unloaded first (their settings stay as they are). 1
 * = gone.
 */
static const char *profPath(const char *name);   /* below */

s32 portProfileDelete(s32 player, const char *name)
{
    char rel[PROFILE_NAME_MAX + 32];
    const s32 seat = (name != NULL && name[0] != '\0') ? profSeatOf(name, player) : -1;

    if (name == NULL || name[0] == '\0' || seat >= 0)
    {
        if (seat >= 0)
        {
            portConLogf("ge: profiles: %s is player %d's - not deleted\n", name, (int) seat + 1);
        }
        return 0;
    }
    if (player >= 0 && player < PROFILE_PLAYERS && profSeatOf(name, -1) == player)
    {
        portProfileClear(player);
    }
    if (remove(profPath(name)) != 0)
    {
        portConLogf("ge: profiles: cannot delete %s\n", profPath(name));
        return 0;
    }
    snprintf(rel, sizeof(rel), PROFILE_DIR "/%s.gestats", name);
    remove(portRootPath(rel));   /* its MP stats go with it (none yet is fine) */
    portConLogf("ge: profiles: deleted %s\n", name);
    sCount = -1;
    return 1;
}

void portProfileAutoSave(s32 flush);

/** Player N plays without one from here on. Their settings stay as they are. */
void portProfileClear(s32 player)
{
    if (player >= 0 && player < PROFILE_PLAYERS && sActive[player][0] != '\0')
    {
        portProfileAutoSave(1);   /* a change still settling goes to the old profile first */
        profSetActive(player, "");
    }
}

/** midas.ini [input] playerN_profile, handed over by joy_port.c's option chain.
 * Records the name only (see the header); returns 1 when the key was ours. */
s32 portProfileIniKey(const char *key, const char *value)
{
    if (strncmp(key, "player", 6) == 0 && key[6] >= '1' && key[6] <= '4' && strcmp(key + 7, "_profile") == 0)
    {
        const s32 player = key[6] - '1';
        const s32 other = profSeatOf(value, player);

        if (strcmp(value, "none") != 0 && other >= 0)
        {   /* a hand-edited ini with one name in two seats: the first keeps it */
            portConLogf("ge: profiles: %s is already player %d's - player %d starts without one\n",
                        value, (int) other + 1, (int) player + 1);
            sActive[player][0] = '\0';
            return 1;
        }
        snprintf(sActive[player], PROFILE_NAME_MAX, "%s", (strcmp(value, "none") == 0) ? "" : value);
        return 1;
    }
    return 0;
}

static const char *profPath(const char *name)
{
    char rel[PROFILE_NAME_MAX + 32];

    portProfileMigrate();
    snprintf(rel, sizeof(rel), PROFILE_DIR "/%s" PROFILE_EXT, name);
    return portRootPath(rel);
}

/* A name the keyboard could have typed, tidied: no leading or trailing space,
 * no trailing dot (Windows drops it and the file would not be found again),
 * nothing a file name cannot hold. 0 = nothing usable is left. */
/* 1 = `name` is a Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9;
 * the base up to the first '.', trailing spaces dropped, any case). Such a
 * name opens the device, not a file, so a save silently writes nothing; it
 * is refused everywhere so the Data folders stay portable. Shared with
 * port_wsets.c. */
s32 portNameIsReserved(const char *out)
{
    static const char *const reserved[] = { "con", "prn", "aux", "nul" };
    char base[8];
    s32 b = 0;
    s32 k;

    for (k = 0; out[k] != '\0' && out[k] != '.' && b < (s32) sizeof(base) - 1; k++)
    {
        base[b++] = (char) ((out[k] >= 'A' && out[k] <= 'Z') ? out[k] - 'A' + 'a' : out[k]);
    }
    if (out[k] != '\0' && out[k] != '.')
    {
        return 0;   /* longer than any device name */
    }
    while (b > 0 && base[b - 1] == ' ')
    {
        b--;
    }
    base[b] = '\0';
    for (k = 0; k < 4; k++)
    {
        if (strcmp(base, reserved[k]) == 0)
        {
            return 1;
        }
    }
    return b == 4 && (strncmp(base, "com", 3) == 0 || strncmp(base, "lpt", 3) == 0)
        && base[3] >= '0' && base[3] <= '9';
}

static s32 profCleanName(const char *in, char *out, s32 n)
{
    s32 len = 0;
    s32 i;

    while (*in == ' ')
    {
        in++;
    }
    for (i = 0; in[i] != '\0' && len < n - 1; i++)
    {
        const char c = in[i];

        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
            || c == '<' || c == '>' || c == '|' || (unsigned char) c < 0x20)
        {
            continue;
        }
        out[len++] = c;
    }
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '.'))
    {
        len--;
    }
    out[len] = '\0';
    /* midas.ini's playerN_profile=none means NO profile, so a profile with that
     * name would be forgotten at the next launch */
    if (strcmp(out, "none") == 0)
    {
        return 0;
    }
    if (portNameIsReserved(out))
    {
        return 0;
    }
    return len > 0;
}

/* AUTOSAVE: every frontend frame each active player's file is re-derived
 * from the live state (profFormat) and compared with what was last written or
 * loaded; a difference that holds still for half a second is written, so a 30
 * Hz character scroll writes once. Because it is derived rather than hooked, a
 * new panel row only needs its key in profFormat and portProfileLoad.
 *
 * Pad keys carry over: a keyboard-and-mouse player has no pad mode, style or
 * bind config, and writing those blank would wipe the profile's pad settings.
 * Those three keys are carried from the file instead (sCarry, read at load or
 * first sight); with a pad they are written as the pad has them.
 *
 * The same profile in two seats: each seat compares with its OWN last write,
 * so they never ping-pong; the last change made wins the file. */
#define PROFILE_TEXT_MAX 2048
#define PROFILE_SETTLE_US 500000ull

struct ProfCarry
{
    char inputmode[8];
    char style[16];
    char layout[64];
    s32 character;   /* the file's character, -1 = none: stands in until MP has picked one */
};

static char sLastText[PROFILE_PLAYERS][PROFILE_TEXT_MAX];   /* "" = not seen yet */
static char sPendText[PROFILE_PLAYERS][PROFILE_TEXT_MAX];
static unsigned long long sPendSince[PROFILE_PLAYERS];
static struct ProfCarry sCarry[PROFILE_PLAYERS];

extern unsigned long long portProfNow(void);   /* pcsched.c */

static void profCarryRead(s32 player, const char *name)
{
    struct ProfCarry *c = &sCarry[player];
    char line[256];
    FILE *f;

    memset(c, 0, sizeof(*c));
    c->character = -1;
    f = fopen(profPath(name), "r");
    if (f == NULL)
    {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *eq;
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        {
            line[--n] = '\0';
        }
        if (line[0] == ';' || line[0] == '[' || (eq = strchr(line, '=')) == NULL)
        {
            continue;
        }
        *eq = '\0';
        if (strcmp(line, "inputmode") == 0)
        {
            snprintf(c->inputmode, sizeof(c->inputmode), "%s", eq + 1);
        }
        else if (strcmp(line, "style") == 0)
        {
            snprintf(c->style, sizeof(c->style), "%s", eq + 1);
        }
        else if (strcmp(line, "layout") == 0)
        {
            snprintf(c->layout, sizeof(c->layout), "%s", eq + 1);
        }
        else if (strcmp(line, "character") == 0)
        {
            const s32 ch = (s32) atoi(eq + 1);

            c->character = (ch >= 0 && ch < PROFILE_CHARACTERS) ? ch : -1;
        }
    }
    fclose(f);
}

/* player N's profile file as their settings stand right now */
static void profFormat(s32 player, const char *clean, char *out, size_t size)
{
    const char *layout = "";
    size_t len = 0;
    s32 dev;

#define PROF_PUT(...) do { if (len < size) { const int w_ = snprintf(out + len, size - len, __VA_ARGS__); if (w_ > 0) len += (size_t) w_; } } while (0)
    out[0] = '\0';
    dev = portInputSlotDevice(player);
    if (dev > 0)
    {
        layout = portInputLayoutActive(dev);
    }
    PROF_PUT("[profile]\n");
    PROF_PUT("name=%s\n", clean);
    PROF_PUT("version=%d\n", PROFILE_VERSION);
    PROF_PUT("[controls]\n");
    if (dev > 0)
    {   /* the panel's Style row is the mode AND the style; no pad, no mode to record */
        /* Only record a mode the player CHOSE. A guessed one (a raphnet, an
         * N64-named pad) written as explicit would force it onto whatever pad
         * loads the profile next. */
        extern s32 portInputDeviceModeRaw(s32 dev);   /* input_pc.c: 0 auto / 1 port / 2 n64 */
        const s32 rawmode = portInputDeviceModeRaw(dev);

        if (rawmode != 0)
        {
            PROF_PUT("inputmode=%s\n", (rawmode == 2) ? "n64" : "port");
        }
        /* Record the style only when the Style row shows one (an N64-mode pad).
         * Otherwise leave the slot's hidden padN_style out, since loading it
         * onto a raphnet would switch it on. */
        if (portInputDeviceModeIsN64(dev))
        {
            PROF_PUT("style=%s\n", portInputStyleName(portInputStyleRaw(player)));
        }
        PROF_PUT("layout=%s\n", (layout != NULL) ? layout : "");
    }
    else
    {   /* no pad: the file's own pad keys, as they were (see AUTOSAVE above) */
        if (sCarry[player].inputmode[0] != '\0')
        {
            PROF_PUT("inputmode=%s\n", sCarry[player].inputmode);
        }
        if (sCarry[player].style[0] != '\0')
        {
            PROF_PUT("style=%s\n", sCarry[player].style);
        }
        PROF_PUT("layout=%s\n", sCarry[player].layout);
    }
    PROF_PUT("look_updown=%d\n", (int) portPlayerLookUpright(player));
    PROF_PUT("look_ahead=%d\n", (int) portPlayerLookAhead(player)); /* v2 */
    PROF_PUT("auto_aim=%d\n", (int) portPlayerAutoAim(player));     /* v3 */
    {   /* v4: the player's own crosshair - "-" = follow crosshair_color */
        const s32 xc = portPlayerCrosshairOwn(player);

        if (xc < -1)
        {
            PROF_PUT("crosshair_color=-\n");
        }
        else if (xc < 0)
        {
            PROF_PUT("crosshair_color=default\n");
        }
        else
        {
            PROF_PUT("crosshair_color=%06X\n", (unsigned) xc);
        }
    }
    /* always_walk is retired: no longer written; an old profile's line is read and ignored */
    PROF_PUT("detonate_use_reload=%d\n", (int) portJoyPortPadOptGet(player, 1));
    PROF_PUT("[multiplayer]\n");
    /* Save the character the MP setup picked. Before it has picked one
     * (player_char -1: fresh boot, solo play), keep the file's own, so changing
     * another setting doesn't overwrite the saved character with the default
     * lineup. */
    PROF_PUT("character=%d\n", (int) ((player_char[player] >= 0) ? player_char[player]
                                      : (sCarry[player].character >= 0) ? sCarry[player].character : player));
#undef PROF_PUT
}

static s32 profWriteText(const char *clean, const char *text)
{
    FILE *f;

    profEnsureDir();
    f = fopen(profPath(clean), "w");
    if (f == NULL)
    {
        portConLogf("ge: profiles: cannot write %s\n", profPath(clean));
        return 0;
    }
    fputs(text, f);
    fclose(f);
    return 1;
}

/**
 * front.c init_mp_options_for_scenario: the character player N's profile
 * holds, for a seat the MP setup has not picked one for yet, so players get
 * their saved character instead of the default lineup. -1 = no profile (or none
 * in it).
 */
s32 portProfileCharacter(s32 player)
{
    if (player < 0 || player >= PROFILE_PLAYERS || sActive[player][0] == '\0')
    {
        return -1;
    }
    profCarryRead(player, sActive[player]);
    return sCarry[player].character;
}

/**
 * Write player N's settings, as they are right now, to Profiles/<name>, and
 * make it theirs - New Profile's OK. An existing file of that name is
 * replaced. 1 = written.
 */
s32 portProfileSave(s32 player, const char *name)
{
    char clean[PROFILE_NAME_MAX];

    if (player < 0 || player >= PROFILE_PLAYERS || name == NULL || !profCleanName(name, clean, sizeof(clean)))
    {
        return 0;
    }
    {   /* a NEW name past a full list would be written and made active but
         * could never be loaded from the list */
        struct stat st;

        if (stat(profPath(clean), &st) != 0 && portProfileCount() >= PROFILE_MAX)
        {
            portConLogf("ge: profiles: list full (%d) - %s not saved\n", PROFILE_MAX, clean);
            return 0;
        }
    }
    if (profSeatOf(clean, player) >= 0)
    {
        portConLogf("ge: profiles: %s is player %d's - not saved for player %d\n",
                    clean, (int) profSeatOf(clean, player) + 1, (int) player + 1);
        return 0;
    }
    if (strcmp(sActive[player], clean) != 0)
    {   /* a new owner of this name: the file's pad keys, if it has any */
        profCarryRead(player, clean);
    }
    profFormat(player, clean, sLastText[player], sizeof(sLastText[player]));
    sPendText[player][0] = '\0';
    if (!profWriteText(clean, sLastText[player]))
    {
        sLastText[player][0] = '\0';
        return 0;
    }
    portConLogf("ge: profiles: player %d saved as %s\n", (int) player + 1, clean);
    sCount = -1;   /* the list has a new name in it */
    profSetActive(player, clean);
    return 1;
}

/**
 * front.c, every frontend frame: write each active profile whose settings
 * changed and have held still for PROFILE_SETTLE_US; flush = write now (a
 * stage is starting). Harness runs (GE_NOINISAVE / GE_WINDOW) never write,
 * like midas.ini.
 */
void portProfileAutoSave(s32 flush)
{
    static s32 s_Allowed = -1;
    char text[PROFILE_TEXT_MAX];
    s32 p;

    if (s_Allowed < 0)
    {
        s_Allowed = (getenv("GE_NOINISAVE") == NULL && getenv("GE_WINDOW") == NULL) ? 1 : 0;
    }
    for (p = 0; p < PROFILE_PLAYERS; p++)
    {
        if (sActive[p][0] == '\0')
        {
            sLastText[p][0] = '\0';
            sPendText[p][0] = '\0';
            continue;
        }
        if (sLastText[p][0] == '\0')
        {   /* first sight (named by midas.ini at boot): this is the baseline */
            profCarryRead(p, sActive[p]);
            profFormat(p, sActive[p], sLastText[p], sizeof(sLastText[p]));
            continue;
        }
        profFormat(p, sActive[p], text, sizeof(text));
        if (strcmp(text, sLastText[p]) == 0)
        {
            sPendText[p][0] = '\0';
            continue;
        }
        if (strcmp(text, sPendText[p]) != 0)
        {   /* still changing: (re)start the settle clock */
            snprintf(sPendText[p], sizeof(sPendText[p]), "%s", text);
            sPendSince[p] = portProfNow();
        }
        if (!flush && portProfNow() - sPendSince[p] < PROFILE_SETTLE_US)
        {
            continue;
        }
        {
            const s32 ok = !s_Allowed || profWriteText(sActive[p], text);

            /* a failed write is not retried (and logged) every frame: it is
             * tried again when the settings next change */
            snprintf(sLastText[p], sizeof(sLastText[p]), "%s", text);
            sPendText[p][0] = '\0';
            if (s_Allowed && ok)
            {
                portConLogf("ge: profiles: player %d autosaved %s\n", (int) p + 1, sActive[p]);
            }
        }
    }
}

/**
 * Load Profiles/<list entry i> into player N: write their padN_* keys, make
 * it theirs. A setting the file does not name is left alone; a bind config it
 * names that is not installed here is left alone and said so in the log.
 * 1 = loaded.
 */
s32 portProfileLoad(s32 player, s32 i)
{
    char name[PROFILE_NAME_MAX];
    char line[256];
    s32 dev;
    FILE *f;

    if (player < 0 || player >= PROFILE_PLAYERS || i < 0 || i >= portProfileCount())
    {
        return 0;
    }
    snprintf(name, sizeof(name), "%s", sNames[i]);   /* a save below may rescan */
    if (profSeatOf(name, player) >= 0)
    {
        portConLogf("ge: profiles: %s is player %d's - not loaded for player %d\n",
                    name, (int) profSeatOf(name, player) + 1, (int) player + 1);
        return 0;
    }
    if (strcmp(sActive[player], name) == 0)
    {   /* reloading their own profile is an undo: its unsaved changes are
         * dropped (made the baseline), not flushed into the file about to be read */
        profFormat(player, name, sLastText[player], sizeof(sLastText[player]));
        sPendText[player][0] = '\0';
    }
    /* a change still settling goes to the profile being left, BEFORE the file
     * is opened, never into a file that is open for reading */
    portProfileAutoSave(1);
    f = fopen(profPath(name), "r");
    if (f == NULL)
    {
        portConLogf("ge: profiles: cannot read %s\n", profPath(name));
        return 0;
    }
    dev = portInputSlotDevice(player);
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *eq;
        char *val;
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        {
            line[--n] = '\0';
        }
        if (line[0] == ';' || line[0] == '#' || line[0] == '[' || (eq = strchr(line, '=')) == NULL)
        {
            continue;
        }
        *eq = '\0';
        val = eq + 1;
        if (strcmp(line, "version") == 0)
        {
            if (atoi(val) > PROFILE_VERSION)
            {
                portConLogf("ge: profiles: %s is version %s, newer than this build's %d - loading what it knows\n",
                            name, val, PROFILE_VERSION);
            }
        }
        else if (strcmp(line, "inputmode") == 0)
        {
            if (dev > 0)
            {
                portInputDeviceModeSet(dev, strcmp(val, "n64") == 0);
            }
        }
        else if (strcmp(line, "look_updown") == 0)
        {
            portJoyPortPadOptSet(player, 2, atoi(val) != 0);
        }
        else if (strcmp(line, "look_ahead") == 0)
        {
            portJoyPortPadOptSet(player, 3, atoi(val) != 0);
        }
        else if (strcmp(line, "auto_aim") == 0)
        {
            portJoyPortPadOptSet(player, 4, atoi(val) != 0);
        }
        else if (strcmp(line, "crosshair_color") == 0)
        {   /* through midas.ini's own parser, so "default" / hex / "-" mean what they mean there */
            char key[40];

            snprintf(key, sizeof(key), "player%d_crosshair_color", (int) player + 1);
            portVideoIniSetKey(key, val);
        }
        else if (strcmp(line, "style") == 0)
        {
            if (dev > 0)
            {
                portInputDeviceStyleSet(dev, portInputStyleCode(val));
            }
        }
        else if (strcmp(line, "layout") == 0)
        {
            /* applied even when the pad already shows this name: a rebind since
             * keeps the name, and the load is what resets it */
            if (dev > 0 && val[0] != '\0')
            {
                s32 c;

                for (c = 0; c < portInputLayoutCount(); c++)
                {
                    if (strcmp(portInputLayoutName(c), val) == 0)
                    {
                        break;
                    }
                }
                /* only one made for this pad: the player has nothing but the
                 * pad to get back with (input_pc.c portInputLayoutFitsSlot) */
                if (c >= portInputLayoutCount() || !portInputLayoutFitsSlot(player, c)
                    || !portInputLayoutApply(dev, c))
                {
                    portConLogf("ge: profiles: %s wants bind config \"%s\", which is not installed or does not fit player %d's pad - left as it is\n",
                                name, val, (int) player + 1);
                }
            }
        }
        else if (strcmp(line, "always_walk") == 0)
        {
            (void) val;   /* retired with Always Walk: an old profile loads, the key does nothing */
        }
        else if (strcmp(line, "detonate_use_reload") == 0)
        {
            portJoyPortPadOptSet(player, 1, atoi(val) != 0);
        }
        else if (strcmp(line, "character") == 0)
        {   /* every character is open on the port (front.c num_chars_selectable_mp) */
            const int c = atoi(val);

            if (c >= 0 && c < PROFILE_CHARACTERS)
            {
                player_char[player] = c;
            }
        }
    }
    fclose(f);
    if (dev > 0)
    {   /* inputmode=port from an older profile on an n64 layout that was
         * already on: the layout step above was skipped, and so was the reset
         * that hands the class back to the tag */
        portInputLayoutReconcileMode(dev);
    }
    portConLogf("ge: profiles: player %d loaded %s\n", (int) player + 1, name);
    profSetActive(player, name);
    /* AUTOSAVE baseline: the state as loaded, so a load writes nothing */
    profCarryRead(player, name);
    profFormat(player, name, sLastText[player], sizeof(sLastText[player]));
    sPendText[player][0] = '\0';
    return 1;
}
