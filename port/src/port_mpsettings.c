/**
 * port_mpsettings.c - saved multiplayer match settings
 * (docs/port/mp-player-options.md, "Load / Save Settings").
 *
 *     Data/MatchSettings/<NAME>.gemp
 *
 *     [mpsettings]
 *     name=QUICK DUEL
 *     version=1
 *     scenario=0    ; MPSCENARIOS: 0 Normal .. 4 License to Kill
 *     level=3       ; multi_stage_setups index
 *     time=5        ; minutes, 0 = no time limit
 *     ...
 *     [gameplay]
 *     mp_friendly_fire=0
 *     ...
 *
 * This file only lists, reads, writes and deletes them; front.c builds and
 * reads the text (portMpSetSave / portMpSetApply), so a key added there needs
 * nothing here. A missing key keeps the setting in force and unknown keys are
 * ignored; bump the version on any change of meaning. Settings are shared by
 * all players. The list, names and deletion follow port_wsets.c, and names are
 * cleaned by portWsetCleanName.
 */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64   /* as port_profiles.c: readdir on 64-bit-inode filesystems */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ultra64.h>
#include "port_paths.h"   /* portDataPath: saved settings follow the layout */

#define MPSET_DIR      "Data/MatchSettings"
#define MPSET_EXT      ".gemp"
#define MPSET_MAX      50      /* most saved settings listed (Load Settings' drop-down scrolls) */
#define MPSET_NAME_MAX 24

extern void portConLogf(const char *fmt, ...);         /* port_console.c */

static char sNames[MPSET_MAX][MPSET_NAME_MAX];
static s32 sCount = -1;
static char sCurrent[MPSET_NAME_MAX];   /* the settings last loaded or saved ("" = none) */

static const char *mpsetPath(const char *name)
{
    char rel[MPSET_NAME_MAX + 32];

    snprintf(rel, sizeof(rel), MPSET_DIR "/%s" MPSET_EXT, name);
    return portDataPath(rel);
}

static int mpsetNameCmp(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

static void mpsetEnsureDir(void)
{
    portEnsureDir(portDataPath(MPSET_DIR));
}

void portMpSetRescan(void)
{
    const size_t extlen = strlen(MPSET_EXT);
    DIR *d;
    struct dirent *de;

    sCount = 0;
    d = opendir(portDataPath(MPSET_DIR));
    if (d == NULL)
    {
        return;
    }
    while ((de = readdir(d)) != NULL)
    {
        const size_t len = strlen(de->d_name);
        char name[MPSET_NAME_MAX];

        if (len <= extlen || strcmp(de->d_name + len - extlen, MPSET_EXT) != 0 || len - extlen >= MPSET_NAME_MAX)
        {
            continue;
        }
        snprintf(name, sizeof(name), "%.*s", (int) (len - extlen), de->d_name);
        if (sCount == MPSET_MAX)
        {   /* full: keep the alphabetically first MPSET_MAX */
            s32 last = 0;
            s32 k;

            for (k = 1; k < sCount; k++)
            {
                if (strcmp(sNames[k], sNames[last]) > 0)
                {
                    last = k;
                }
            }
            if (strcmp(name, sNames[last]) < 0)
            {
                snprintf(sNames[last], MPSET_NAME_MAX, "%s", name);
            }
            continue;
        }
        snprintf(sNames[sCount++], MPSET_NAME_MAX, "%s", name);
    }
    closedir(d);
    qsort(sNames, (size_t) sCount, MPSET_NAME_MAX, mpsetNameCmp);
}

s32 portMpSetCount(void)
{
    if (sCount < 0)
    {
        portMpSetRescan();
    }
    return sCount;
}

const char *portMpSetName(s32 i)
{
    return (i >= 0 && i < portMpSetCount()) ? sNames[i] : "";
}

s32 portMpSetFull(void)
{
    return portMpSetCount() >= MPSET_MAX;
}

const char *portMpSetCurrent(void)
{
    return sCurrent;
}

void portMpSetSetCurrent(const char *name)
{
    snprintf(sCurrent, sizeof(sCurrent), "%s", (name != NULL) ? name : "");
}

/* the whole file into buf (NUL-ended); 1 = read */
s32 portMpSetRead(const char *name, char *buf, s32 size)
{
    FILE *f;
    size_t n;

    if (size <= 0)
    {
        return 0;
    }
    buf[0] = '\0';
    f = fopen(mpsetPath(name), "r");
    if (f == NULL)
    {
        return 0;
    }
    n = fread(buf, 1, (size_t) size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 1;
}

/* 1 = written */
s32 portMpSetWrite(const char *name, const char *text)
{
    FILE *f;
    s32 bad;

    mpsetEnsureDir();
    f = fopen(mpsetPath(name), "w");
    if (f == NULL)
    {
        portConLogf("ge: mpsettings: cannot write %s\n", mpsetPath(name));
        return 0;
    }
    fputs(text, f);
    bad = ferror(f);
    if (fclose(f) != 0 || bad)
    {
        portConLogf("ge: mpsettings: cannot write %s\n", mpsetPath(name));
        return 0;
    }
    sCount = -1;
    return 1;
}

s32 portMpSetDelete(const char *name)
{
    if (remove(mpsetPath(name)) != 0)
    {
        portConLogf("ge: mpsettings: cannot delete %s\n", mpsetPath(name));
        return 0;
    }
    portConLogf("ge: mpsettings: deleted %s\n", name);
    if (strcmp(sCurrent, name) == 0)
    {
        sCurrent[0] = '\0';
    }
    sCount = -1;
    return 1;
}
