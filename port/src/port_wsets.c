/**
 * port_wsets.c - custom multiplayer weapon sets saved on disk
 * (docs/port/mp-player-options.md, "Custom weapon sets").
 *
 *     Data/WeaponSets/<NAME>.gewset   (sets in the old ./WeaponSets are moved here)
 *
 *     [weaponset]
 *     name=SNIPERS ONLY
 *     version=1
 *     slot1=17                     ; ITEM_IDS, slot 1 = weakest, 8 = strongest
 *     ...
 *     slot8=26
 *
 * A slot stores only the weapon: mp_weapon.c copies the rest of the slot from
 * the first of Rare's sets that has that weapon. A missing slot reads as
 * Slappers, unknown keys are ignored, and the version must be bumped on any
 * layout change. Sets are shared by all players, not stored per profile.
 *
 * Related code: front.c's Weapons pick screen (the CUSTOM tab, set list and
 * editor) and mp_weapon.c (PORT_MP_WEAPONSET_CUSTOM).
 */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64   /* as port_profiles.c: readdir on 64-bit-inode filesystems */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ultra64.h>
#include <bondconstants.h>   /* ITEM_UNARMED - the real enum, not a copy */
#include "port_paths.h"   /* portDataPath: saved sets follow the layout */

#define WSET_DIR      "Data/WeaponSets"   /* formerly ./WeaponSets; old sets are moved here */
#define WSET_DIR_OLD  "WeaponSets"
#define WSET_EXT      ".gewset"
#define WSET_VERSION  1
#define WSET_MAX      50      /* most saved sets listed (Load Set's drop-down scrolls) */
#define WSET_NAME_MAX 24

extern void portConLogf(const char *fmt, ...);         /* port_console.c */

static char sNames[WSET_MAX][WSET_NAME_MAX];
static s32 sCount = -1;
static char sCurrent[WSET_NAME_MAX];   /* the custom set in force ("" = a built-in one) */

static const char *wsetPath(const char *name)
{
    char rel[WSET_NAME_MAX + 32];

    snprintf(rel, sizeof(rel), WSET_DIR "/%s" WSET_EXT, name);
    return portDataPath(rel);
}

static int wsetNameCmp(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

static void wsetEnsureDir(void)
{
    portEnsureDir(portDataPath(WSET_DIR));
}

/* sets saved under the old ./WeaponSets folder move into Data/WeaponSets */
static void wsetMigrate(void)
{
    static s32 done;
    const size_t extlen = strlen(WSET_EXT);
    DIR *d;
    struct dirent *de;

    if (done)
    {
        return;
    }
    done = 1;
    if (!portLegacyFiles())
    {
        return;   /* a package install: no ./WeaponSets from an older release */
    }
    d = opendir(portDataPath(WSET_DIR_OLD));
    if (d == NULL)
    {
        return;
    }
    wsetEnsureDir();
    while ((de = readdir(d)) != NULL)
    {
        const size_t len = strlen(de->d_name);
        char from[PORT_PATH_MAX], to[PORT_PATH_MAX], rel[WSET_NAME_MAX + 48];
        struct stat st;

        if (len <= extlen || strcmp(de->d_name + len - extlen, WSET_EXT) != 0)
        {
            continue;
        }
        snprintf(rel, sizeof(rel), WSET_DIR_OLD "/%s", de->d_name);
        snprintf(from, sizeof(from), "%s", portDataPath(rel));
        snprintf(rel, sizeof(rel), WSET_DIR "/%s", de->d_name);
        snprintf(to, sizeof(to), "%s", portDataPath(rel));
        if (stat(to, &st) != 0 && rename(from, to) == 0)
        {
            portConLogf("ge: wsets: moved %s into %s\n", de->d_name, WSET_DIR);
        }
    }
    closedir(d);
}

void portWsetRescan(void)
{
    const size_t extlen = strlen(WSET_EXT);
    DIR *d;
    struct dirent *de;
    s32 warned = 0;

    wsetMigrate();
    sCount = 0;
    d = opendir(portDataPath(WSET_DIR));
    if (d == NULL)
    {
        return;
    }
    while ((de = readdir(d)) != NULL)
    {
        const size_t len = strlen(de->d_name);

        if (len <= extlen || strcmp(de->d_name + len - extlen, WSET_EXT) != 0 || len - extlen >= WSET_NAME_MAX)
        {
            continue;
        }
        if (sCount == WSET_MAX)
        {   /* full: keep the alphabetically first WSET_MAX, whatever order
             * the folder lists them in (the profile list's rule) */
            char name[WSET_NAME_MAX];
            s32 last = 0;
            s32 k;

            if (!warned)
            {
                portConLogf("ge: wsets: more than %d sets in %s - only the first %d alphabetically are listed\n",
                            WSET_MAX, WSET_DIR, WSET_MAX);
                warned = 1;
            }
            snprintf(name, sizeof(name), "%.*s", (int) (len - extlen), de->d_name);
            for (k = 1; k < sCount; k++)
            {
                if (strcmp(sNames[k], sNames[last]) > 0)
                {
                    last = k;
                }
            }
            if (strcmp(name, sNames[last]) < 0)
            {
                snprintf(sNames[last], WSET_NAME_MAX, "%s", name);
            }
            continue;
        }
        snprintf(sNames[sCount++], WSET_NAME_MAX, "%.*s", (int) (len - extlen), de->d_name);
    }
    closedir(d);
    qsort(sNames, (size_t) sCount, WSET_NAME_MAX, wsetNameCmp);
}

s32 portWsetCount(void)
{
    if (sCount < 0)
    {
        portWsetRescan();
    }
    return sCount;
}

const char *portWsetName(s32 i)
{
    return (i >= 0 && i < portWsetCount()) ? sNames[i] : "";
}

s32 portWsetFull(void)
{
    return portWsetCount() >= WSET_MAX;
}

const char *portWsetCurrent(void)
{
    return sCurrent;
}

void portWsetSetCurrent(const char *name)
{
    snprintf(sCurrent, sizeof(sCurrent), "%s", (name != NULL) ? name : "");
}

/* A name a file can carry: no leading / trailing space, no trailing dot, none
 * of \ / : * ? " < > | (the keyboard can type '?'). 0 = nothing usable left. */
s32 portWsetCleanName(const char *in, char *out, s32 n)
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
    {
        extern s32 portNameIsReserved(const char *name);   /* port_profiles.c */

        if (portNameIsReserved(out))
        {
            return 0;   /* CON, NUL... would open a device, not a file */
        }
    }
    return len > 0;
}

/* 1 = read; a missing slot is Slappers (ITEM_UNARMED) */
s32 portWsetLoad(const char *name, s32 items[8])
{
    char line[128];
    FILE *f;
    s32 k;

    for (k = 0; k < 8; k++)
    {
        items[k] = ITEM_UNARMED;
    }
    f = fopen(wsetPath(name), "r");
    if (f == NULL)
    {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (strncmp(line, "slot", 4) == 0 && line[4] >= '1' && line[4] <= '8' && line[5] == '=')
        {
            items[line[4] - '1'] = (s32) strtol(line + 6, NULL, 10);
        }
    }
    fclose(f);
    return 1;
}

/* 1 = written */
s32 portWsetSave(const char *name, const s32 items[8])
{
    FILE *f;
    s32 k;

    wsetEnsureDir();
    f = fopen(wsetPath(name), "w");
    if (f == NULL)
    {
        portConLogf("ge: wsets: cannot write %s\n", wsetPath(name));
        return 0;
    }
    fprintf(f, "[weaponset]\n");
    fprintf(f, "name=%s\n", name);
    fprintf(f, "version=%d\n", WSET_VERSION);
    for (k = 0; k < 8; k++)
    {
        fprintf(f, "slot%d=%d\n", (int) k + 1, (int) items[k]);
    }
    fclose(f);
    sCount = -1;
    return 1;
}

s32 portWsetDelete(const char *name)
{
    if (remove(wsetPath(name)) != 0)
    {
        portConLogf("ge: wsets: cannot delete %s\n", wsetPath(name));
        return 0;
    }
    portConLogf("ge: wsets: deleted %s\n", name);
    if (strcmp(sCurrent, name) == 0)
    {
        sCurrent[0] = '\0';
    }
    sCount = -1;
    return 1;
}
