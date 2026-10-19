/**
 * romdata.c - the runtime ROM loader.
 *
 * The exe does not ship Rare's data: each user supplies their own cart image
 * and the game data is read from it at runtime, one asset class at a time
 * (docs/port/rom-asset-loading.md is the overview, docs/port/romdata.md the
 * implementation notes and gotchas). Modelled on the Perfect Dark port's
 * romdata.c, and sits underneath ob.c rather than beside it.
 *
 * This file finds, validates and normalises the ROM, inflates its data segment
 * with the game's own 1172 inflater, parses the 727-entry file directory
 * (file_resource_table), and checks the ROM-segment table generated from
 * scripts/filelist.u.csv. Modded-ROM support lives on the mod-loader branch,
 * not here. GE_ROMDATA=0 skips the ROM entirely and uses the compiled-in
 * assets.
 *
 * ROM resolution order (first that loads wins, always logged):
 *   1. argv[1] not starting with '-'   (drag-and-drop onto ge.exe)
 *   2. --rom-file <path> / --rom-file=<path>
 *   3. GE_ROM=<path>                    (env var, e.g. the launcher's env box)
 *   4. midas.ini [data] rom=<path>         (relative = next to the exe)
 *   5. <exe dir>/Data/*                 (alphabetical, first valid file)
 *   6. <exe dir>/baserom.u.z64, <exe dir>/../baserom.u.z64  (dev tree)
 *
 * Formats: .z64 (80 37 12 40), .v64 byte-swapped (37 80 40 12) and .n64
 * word-swapped (40 12 37 80), detected from the first four bytes (never the
 * extension) and normalised to z64 order in memory.
 *
 * Stock cart only: the size must be at least a stock cart's, the header must
 * read GOLDENEYE / NGE (a renamed header only warns when the file was named
 * explicitly), the file table must parse, and the whole-image CRC32 must match
 * the retail (U) cart (B6330846). Modded ROMs, other regions and bad dumps are
 * rejected with a message saying so; the CIC-6102 CRC is only reported.
 *
 * WHERE THE BUFFER LIVES IS LOAD-BEARING: fast3d's seg_addr() treats any
 * word with top byte 1..15 that fails PORT_IS_HOST_ADDR() as a segment
 * reference, so a ROM mapped below 0x10000000 would break display lists that
 * point into it. portAllocHighDedicated (pcsched.c) maps it high.
 *
 * Knobs: [data] rom= (midas.ini), GE_ROM, GE_ROMDATA=0, GE_ROMDBG=1 (per-file and
 * per-segment lines; scripts/port/romtable_lint.py diffs them against
 * scripts/filelist.u.csv).
 */
#ifndef _WIN32
/* 32-bit ELF (linux/deck) on a filesystem with 64-bit inodes (WSL drvfs,
 * btrfs, NTFS mounts): plain stat() fails with EOVERFLOW and Data looked
 * missing AND empty in the first Linux smoke. Must precede every include. */
#define _FILE_OFFSET_BITS 64
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef _WIN32
#include <direct.h> /* _mkdir */
#endif
#include <ultra64.h>
#include "decompress.h" /* decompressdata: the game's own 1172 inflater */
#include "ob.h"         /* fileentry / resource_lookup_data_entry: the compiled file table */
#include "image.h"      /* g_Textures / PORT_TEXTURES_MAX: the texture directory */
#include "assets/obseg/file_resource_id_enums.h" /* OBJ_INDEX_MAX */
#include "romdata.h"
#include "../generated/romseg_list.h"

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console ring (spec section 7). */
extern void portConLogf(const char *fmt, ...);

/* include/PR/os.h #undef's errno (an OSMesg field carries that name);
 * this is libc's own expansion, brought back under a port name */
#ifdef _WIN32
#define port_errno (*_errno())
#elif defined(__APPLE__)
#define port_errno (*__error())
#else
#define port_errno (*__errno_location())
#endif

extern const char *portRootPath(const char *name); /* main.c: <exe dir>/name */
extern FILE *portRootFopen(const char *name, const char *mode);
extern void *portAllocHighDedicated(u32 size);      /* pcsched.c */
extern void portRomdataSegsApply(void);             /* segments.c: point segment consumers at
                                                     * the ROM */

/* ---- the US cart, first-hand (docs/port/rom-asset-loading.md section 1) */
#define ROMDATA_ROM_SIZE        0xC00000u   /* 12,582,912: a stock cart */
#define ROMDATA_ROM_MAX         (64u << 20) /* sanity cap for a "ROM" */
#define ROMDATA_CDATA_ROMOFS    0x21990u    /* _cdataSegmentRomStart, 1172 blob */
#define ROMDATA_CDATA_SIZE      0x3C550u    /* inflates to 247,120 bytes */
#define ROMDATA_CDATA_ALLOC     (1u << 20)  /* the inflater has no output bound */
#define ROMDATA_CDATA_VADDR     0x80020D90u /* where the N64 loads it */
#define ROMDATA_FILETABLE_OFS   0x252C4u    /* file_resource_table[] in cdata (RAM 0x80046054) */
#define ROMDATA_FILEENTRY_SIZE  12          /* {s32 index; char *filename; u8 *hw_address;} BE */
#define ROMDATA_MAX_FILES       2048        /* 727 + headroom for a larger directory */
#define ROMDATA_STOCK_CRC1      0xDCBC50D1u /* GoldenEye 007 (U) header CRCs */
#define ROMDATA_STOCK_CRC2      0x09FD1AA3u
#define ROMDATA_STOCK_CRC32     0xB6330846u /* zlib crc32 of the whole stock (U) image */

#ifdef _WIN32
#define PATHSEP "\\"
#else
#define PATHSEP "/"
#endif
#define ROMDATA_DATA_DIR "Data"

static u8 *sRom;            /* normalised z64 bytes, high address */
static u32 sRomSize;
static char sRomPath[1200];
static enum romsource sRomSource;
static u8 *sCdata;          /* the inflated data segment: it IS the directory */
static u32 sCdataSize;
static struct romfile sFiles[ROMDATA_MAX_FILES];
static s32 sFileCount;
static s32 sLoaded;
static int sDebug;          /* GE_ROMDBG */

/* midas.ini [data] */
static char sIniRom[1024];
static int sArgLangJp;      /* --lang jp (romdataLanguageJp) */

/* the non-file-table ROM regions, from scripts/filelist.u.csv */
struct romseg {
    const char *name;
    u32 romofs;
    u32 size;
    const char *source;
};
#define ROMSEG(n, ofs, sz, src) { #n, ofs, sz, src },
static const struct romseg sSegs[] = { ROMSEG_LIST() };
#undef ROMSEG

static u32 be32(const u8 *p)
{
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | (u32) p[3];
}

static const char *romdataSourceName(enum romsource s)
{
    switch (s) {
    case ROMSRC_ARGV:    return "argv[1] (drag-and-drop)";
    case ROMSRC_ARGFLAG: return "--rom-file";
    case ROMSRC_ENV:     return "GE_ROM";
    case ROMSRC_INI:     return "midas.ini [data] rom=";
    case ROMSRC_DATADIR: return "./" ROMDATA_DATA_DIR " scan";
    case ROMSRC_LEGACY:  return "legacy repo baserom (dev)";
    default:             return "none";
    }
}

/* ==== paths ============================================================ */

static int romdataPathIsAbsolute(const char *p)
{
    if (p[0] == '/' || p[0] == '\\') {
        return 1;
    }
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
        return 1;
    }
    return 0;
}

/* <exe dir>/rel unless rel is already absolute */
static void romdataResolve(char *out, size_t n, const char *rel)
{
    if (romdataPathIsAbsolute(rel)) {
        snprintf(out, n, "%s", rel);
    } else {
        snprintf(out, n, "%s", portRootPath(rel));
    }
}

/* a stat failure that is not "no such file" is named (rate-limited): the
 * EOVERFLOW above was invisible as "Data is empty" */
static int romdataStat(const char *path, struct stat *st)
{
    if (stat(path, st) == 0) {
        return 1;
    }
    if (port_errno != ENOENT) {
        static int budget = 4;
        if (budget > 0) {
            budget--;
            portConLogf("ge: romdata: stat(%s): %s\n", path, strerror(port_errno));
        }
    }
    return 0;
}

static int romdataIsDir(const char *path)
{
    struct stat st;
    return romdataStat(path, &st) && S_ISDIR(st.st_mode);
}

static int romdataIsFile(const char *path)
{
    struct stat st;
    return romdataStat(path, &st) && S_ISREG(st.st_mode);
}

/* create <exe dir>/name when missing (for Data, a fresh install then shows
 * the user where the ROM goes) */
static void romdataEnsureDir(const char *name)
{
    const char *path = portRootPath(name);
    int rc;

    if (romdataIsDir(path)) {
        return;
    }
#ifdef _WIN32
    rc = _mkdir(path);
#else
    rc = mkdir(path, 0755);
#endif
    if (rc == 0) {
        portConLogf("ge: romdata: created %s\n", path);
    } else {
        portConLogf("ge: romdata: could not create %s (read-only install dir?)\n", path);
    }
}

static int romdataCmpStr(const void *a, const void *b)
{
    return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/* sorted names of a directory's entries (regular files when wantdirs=0,
 * directories otherwise); returns the count, names in a caller buffer */
#define ROMDATA_LIST_MAX 256
static int romdataListDir(const char *dirpath, int wantdirs, char names[][256], int max)
{
    DIR *d = opendir(dirpath);
    struct dirent *de;
    const char *ptrs[ROMDATA_LIST_MAX];
    char tmp[ROMDATA_LIST_MAX][256];
    int n = 0;
    int i;

    if (d == NULL) {
        return 0;
    }
    if (max > ROMDATA_LIST_MAX) {
        max = ROMDATA_LIST_MAX;
    }
    /* Read the whole directory and keep the first `max` names in alphabetical
     * order, not readdir order (which is hash order on some filesystems).
     * Otherwise the ROM could be left out of a crowded Data folder. */
    while ((de = readdir(d)) != NULL) {
        char full[1500];
        int slot = n;
        if (de->d_name[0] == '.') {
            continue; /* ., .., dotfiles */
        }
        if (n >= max) {
            int j;
            if (max <= 0) {
                break;
            }
            slot = 0;
            for (j = 1; j < n; j++) {
                if (strcmp(tmp[j], tmp[slot]) > 0) {
                    slot = j;
                }
            }
            if (strcmp(de->d_name, tmp[slot]) >= 0) {
                continue;
            }
        }
        snprintf(full, sizeof(full), "%s" PATHSEP "%s", dirpath, de->d_name);
        if (wantdirs ? !romdataIsDir(full) : !romdataIsFile(full)) {
            continue;
        }
        snprintf(tmp[slot], sizeof(tmp[slot]), "%s", de->d_name);
        ptrs[slot] = tmp[slot];
        if (slot == n) {
            n++;
        }
    }
    closedir(d);
    /* readdir order is filesystem-dependent; alphabetical is what the
     * doc promises */
    qsort(ptrs, (size_t) n, sizeof(ptrs[0]), romdataCmpStr);
    for (i = 0; i < n; i++) {
        snprintf(names[i], 256, "%s", ptrs[i]);
    }
    return n;
}

/* ==== midas.ini [data] ==================================================== */

static char *romdataTrim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

/* video.c's reader is section-blind and video.c is not in headless
 * builds, so [data] has its own (section-aware) reader here */
static void romdataReadIni(void)
{
    FILE *f = portRootFopen("midas.ini", "r");
    char line[1200];
    int indata = 0;

    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *s = romdataTrim(line);
        char *eq;
        if (s[0] == '\0' || s[0] == ';' || s[0] == '#') {
            continue;
        }
        if (s[0] == '[') {
            indata = (strncmp(s, "[data]", 6) == 0);
            continue;
        }
        if (!indata) {
            continue;
        }
        eq = strchr(s, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        s = romdataTrim(s);
        if (strcmp(s, "rom") == 0) {
            snprintf(sIniRom, sizeof(sIniRom), "%s", romdataTrim(eq + 1));
        }
    }
    fclose(f);
}

s32 romdataIsIniKey(const char *key)
{
    return strcmp(key, "rom") == 0;
}

/* Which of the ROM's two text bank sets (L*E or L*J) the game loads: the
 * command line's --lang jp (romdataInit), or GE_LANG=en|jp, which wins. Not a
 * midas.ini key yet (1.1 adds a menu row; docs/port/japanese-language.md).
 * boss.c reads it once, before langInit, into j_text_trigger. */
s32 romdataLanguageJp(void)
{
    const char *e = getenv("GE_LANG");

    if (e != NULL && e[0] != '\0') {
        return e[0] == 'j' || e[0] == 'J';
    }
    return sArgLangJp;
}

/* ==== byte order ======================================================= */

enum romorder { ORDER_UNKNOWN = 0, ORDER_Z64, ORDER_V64, ORDER_N64 };

static enum romorder romdataDetectOrder(const u8 *p)
{
    if (p[0] == 0x80 && p[1] == 0x37 && p[2] == 0x12 && p[3] == 0x40) return ORDER_Z64;
    if (p[0] == 0x37 && p[1] == 0x80 && p[2] == 0x40 && p[3] == 0x12) return ORDER_V64;
    if (p[0] == 0x40 && p[1] == 0x12 && p[2] == 0x37 && p[3] == 0x80) return ORDER_N64;
    return ORDER_UNKNOWN;
}

static void romdataNormalise(u8 *rom, u32 size, enum romorder order)
{
    u32 i;
    if (order == ORDER_V64) {
        for (i = 0; i + 1 < size; i += 2) {
            u8 t = rom[i];
            rom[i] = rom[i + 1];
            rom[i + 1] = t;
        }
    } else if (order == ORDER_N64) {
        for (i = 0; i + 3 < size; i += 4) {
            u8 t0 = rom[i], t1 = rom[i + 1];
            rom[i] = rom[i + 3];
            rom[i + 1] = rom[i + 2];
            rom[i + 2] = t1;
            rom[i + 3] = t0;
        }
    }
}

/* ==== CIC-6102 CRC (informational) ===================================== */

#define ROL(i, b) ((b) ? (((i) << (b)) | ((i) >> (32 - (b)))) : (i))

static void romdataCrc6102(const u8 *rom, u32 *crc1, u32 *crc2)
{
    const u32 seed = 0xF8CA4DDCu;
    u32 t1 = seed, t2 = seed, t3 = seed, t4 = seed, t5 = seed, t6 = seed;
    u32 i;

    for (i = 0x1000; i < 0x1000 + 0x100000; i += 4) {
        u32 d = be32(rom + i);
        u32 r;
        if ((t6 + d) < t6) {
            t4++;
        }
        t6 += d;
        t3 ^= d;
        r = ROL(d, (d & 0x1F));
        t5 += r;
        if (t2 > d) {
            t2 ^= r;
        } else {
            t2 ^= t6 ^ d;
        }
        t1 += t5 ^ d;
    }
    *crc1 = t6 ^ t4 ^ t3;
    *crc2 = t5 ^ t2 ^ t1;
}

/* zlib-compatible CRC-32 of the whole image: the "is this the stock
 * cart" answer (the CIC CRC above stops 1 MB in - a setup/model mod
 * leaves it intact). ~12 M table steps; a blink even in a Debug build. */
static u32 romdataCrc32(const u8 *p, u32 n)
{
    static u32 table[256];
    u32 c;
    u32 i;

    if (table[1] == 0) {
        for (i = 0; i < 256; i++) {
            u32 k;
            c = i;
            for (k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
    }
    c = 0xFFFFFFFFu;
    for (i = 0; i < n; i++) {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* ==== the directory ==================================================== */

/* inflate cdata and walk file_resource_table into sFiles. 0 = not a
 * usable GoldenEye image (reason logged). */
static int romdataParseTables(const u8 *rom, u32 romsize)
{
    const u8 *src = rom + ROMDATA_CDATA_ROMOFS;
    u32 n;
    s32 i;
    s32 ncomp = 0;
    s32 nalias = 0;

    if (ROMDATA_CDATA_ROMOFS + 2 > romsize || src[0] != 0x11 || src[1] != 0x72) {
        portConLogf("ge: romdata: no 1172 stream at 0x%X (found %02X %02X) - not a GoldenEye (U) image, or the code segment was moved\n",
                ROMDATA_CDATA_ROMOFS, src[0], src[1]);
        return 0;
    }
    sCdata = (u8 *) malloc(ROMDATA_CDATA_ALLOC);
    if (sCdata == NULL) {
        portConLogf("ge: romdata: out of memory for cdata\n");
        return 0;
    }
    /* decompressdata's PC arm ignores the huft arg (it owns a static
     * workspace); src is read-only here - the inflater never writes src */
    n = decompressdata((u8 *) src, sCdata, NULL);
    sCdataSize = n;
    if (n != ROMDATA_CDATA_SIZE) {
        portConLogf("ge: romdata: cdata inflated to %u bytes, stock is %u - data segment layout differs (JP/EU cart, or a code mod)\n",
                n, ROMDATA_CDATA_SIZE);
    }
    if (n < ROMDATA_FILETABLE_OFS + 3 * ROMDATA_FILEENTRY_SIZE) {
        portConLogf("ge: romdata: cdata too small to hold the file table\n");
        free(sCdata);
        sCdata = NULL;
        return 0;
    }

    /* walk until the self-index stops counting (the table is followed
     * by zeros: entry "727" reads index 0 and stops the walk) */
    for (i = 0; i < ROMDATA_MAX_FILES; i++) {
        const u8 *e = sCdata + ROMDATA_FILETABLE_OFS + (u32) i * ROMDATA_FILEENTRY_SIZE;
        u32 idx, nameptr, hw;
        if (ROMDATA_FILETABLE_OFS + (u32) (i + 1) * ROMDATA_FILEENTRY_SIZE > n) {
            break;
        }
        idx = be32(e);
        nameptr = be32(e + 4);
        hw = be32(e + 8);
        if (idx != (u32) i) {
            break;
        }
        if (nameptr < ROMDATA_CDATA_VADDR || nameptr >= ROMDATA_CDATA_VADDR + n) {
            if (i == 0) {
                sFiles[0].name = "";
            } else {
                break;
            }
        } else {
            /* the padded pool the table points at (there is a SECOND
             * copy of the strings in cdata - resolving against the wrong
             * one yields truncated names; this one is the pointer's) */
            const char *name = (const char *) sCdata + (nameptr - ROMDATA_CDATA_VADDR);
            if (memchr(name, '\0', n - (nameptr - ROMDATA_CDATA_VADDR)) == NULL) {
                break; /* unterminated: not a string pool */
            }
            sFiles[i].name = name;
        }
        sFiles[i].romofs = hw;
        sFiles[i].size = 0;
        sFiles[i].compressed = 0;
    }
    sFileCount = i;
    if (sFileCount < 3) {
        portConLogf("ge: romdata: file table at cdata+0x%X did not parse (%d entries)\n", ROMDATA_FILETABLE_OFS, sFileCount);
        free(sCdata);
        sCdata = NULL;
        sFileCount = 0;
        return 0;
    }

    /* size = next - this (ob.c obInit), so the table must be
     * address-sorted and inside the image */
    for (i = 1; i < sFileCount; i++) {
        if (sFiles[i].romofs > romsize) {
            portConLogf("ge: romdata: file %d \"%s\" at 0x%X is beyond the %u-byte image\n",
                    i, sFiles[i].name, sFiles[i].romofs, romsize);
            goto bad;
        }
        if (i + 1 < sFileCount) {
            if (sFiles[i + 1].romofs < sFiles[i].romofs) {
                portConLogf("ge: romdata: file table not address-sorted at %d (\"%s\" 0x%X > \"%s\" 0x%X)\n",
                        i, sFiles[i].name, sFiles[i].romofs, sFiles[i + 1].name, sFiles[i + 1].romofs);
                goto bad;
            }
            sFiles[i].size = sFiles[i + 1].romofs - sFiles[i].romofs;
        }
        if (sFiles[i].size == 0) {
            if (i + 1 < sFileCount) {
                nalias++; /* the last entry (ob/ob_end.seg) only bounds the one before */
            }
        } else if (sFiles[i].romofs + 2 <= romsize && rom[sFiles[i].romofs] == 0x11 && rom[sFiles[i].romofs + 1] == 0x72) {
            sFiles[i].compressed = 1;
            ncomp++;
        }
    }
    if (strcmp(sFiles[sFileCount - 1].name, "ob/ob_end.seg") != 0) {
        portConLogf("ge: romdata: last file is \"%s\", expected ob/ob_end.seg (the size bound) - a mod that appended files?\n",
                sFiles[sFileCount - 1].name);
    }
    portConLogf("ge: romdata: cdata inflated %u bytes; file table %d entries (%d compressed, %d zero-length aliases), obseg 0x%X..0x%X\n",
            n, sFileCount, ncomp, nalias, sFiles[1].romofs, sFiles[sFileCount - 1].romofs);
    if (sDebug) {
        for (i = 0; i < sFileCount; i++) {
            portConLogf("ge: romfile %4d %-28s 0x%06X %7u %s\n", i, sFiles[i].name, sFiles[i].romofs, sFiles[i].size,
                    sFiles[i].compressed ? "1172" : (sFiles[i].size == 0 ? "alias" : "raw"));
        }
    }
    return 1;

bad:
    free(sCdata);
    sCdata = NULL;
    sFileCount = 0;
    return 0;
}

/* every ROMSEG must lie inside the image; a mod that shrank the cart
 * below a region would otherwise read past the buffer later */
static int romdataCheckSegs(u32 romsize)
{
    int bad = 0;
    size_t i;
    for (i = 0; i < sizeof(sSegs) / sizeof(sSegs[0]); i++) {
        int ok = sSegs[i].romofs + sSegs[i].size <= romsize;
        if (!ok) {
            bad++;
        }
        if (sDebug || !ok) {
            portConLogf("ge: romseg %-20s 0x%06X 0x%06X %s\n", sSegs[i].name, sSegs[i].romofs, sSegs[i].size,
                    ok ? "ok" : "OUT OF RANGE");
        }
    }
    portConLogf("ge: romdata: %u ROM segments (romseg_list.h)%s\n", (unsigned) i, bad ? " - SOME OUT OF RANGE" : " in range");
    return bad == 0;
}

/* ==== loading one candidate ============================================ */

/* strict: a ./Data scan candidate must pass the header check; an
 * explicitly named ROM only warns (a mod may have renamed the cart). */
static int romdataTryLoad(const char *path, enum romsource source, int strict)
{
    FILE *f;
    long len;
    u8 *buf;
    u8 *high;
    enum romorder order;
    int titleok, idok;
    u32 crc1, crc2, hcrc1, hcrc2, crc32;
    char title[21];
    int tl;

    f = fopen(path, "rb");
    if (f == NULL) {
        portConLogf("ge: romdata: %s: cannot open (%s)\n", path, romdataSourceName(source));
        return 0;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < (long) ROMDATA_ROM_SIZE) {
        portConLogf("ge: romdata: %s: %ld bytes, a GoldenEye cart is %u - not a ROM (%s)\n", path, len, ROMDATA_ROM_SIZE,
                romdataSourceName(source));
        fclose(f);
        return 0;
    }
    if (len > (long) ROMDATA_ROM_MAX) {
        portConLogf("ge: romdata: %s: %ld bytes is too big for a cart image (%s)\n", path, len, romdataSourceName(source));
        fclose(f);
        return 0;
    }
    buf = (u8 *) malloc((size_t) len);
    if (buf == NULL || fread(buf, 1, (size_t) len, f) != (size_t) len) {
        portConLogf("ge: romdata: %s: read failed\n", path);
        fclose(f);
        free(buf);
        return 0;
    }
    fclose(f);

    order = romdataDetectOrder(buf);
    if (order == ORDER_UNKNOWN) {
        portConLogf("ge: romdata: %s: first bytes %02X %02X %02X %02X are not an N64 image (.z64 80 37 12 40 / .v64 37 80 40 12 / .n64 40 12 37 80)\n",
                path, buf[0], buf[1], buf[2], buf[3]);
        free(buf);
        return 0;
    }
    romdataNormalise(buf, (u32) len, order);

    memcpy(title, buf + 0x20, 20);
    title[20] = '\0';
    for (tl = 20; tl > 0 && title[tl - 1] == ' '; tl--) {
        title[tl - 1] = '\0'; /* the header pads with spaces */
    }
    titleok = memcmp(buf + 0x20, "GOLDENEYE", 9) == 0;
    idok = memcmp(buf + 0x3B, "NGE", 3) == 0;
    if (!titleok && !idok) {
        portConLogf("ge: romdata: %s: header title \"%s\" id %.4s is not GOLDENEYE/NGEE%s\n", path, title, (const char *) buf + 0x3B,
                strict ? " - skipped" : " - named explicitly, trying it anyway");
        if (strict) {
            free(buf);
            return 0;
        }
    }
    if (!romdataParseTables(buf, (u32) len)) {
        free(buf);
        return 0;
    }

    /* Stock cart only: reject any image whose whole-image CRC32 isn't the
     * retail (U) cart's, before it takes the high mapping, since this build
     * lacks the handling mods need and would fail later in confusing ways. An
     * explicitly named file is rejected too; only the header check above is
     * lenient. */
    crc32 = romdataCrc32(buf, (u32) len);
    if (crc32 != ROMDATA_STOCK_CRC32) {
        portConLogf("ge: romdata: %s: image crc32 %08X is not the stock GoldenEye 007 (U) cart (%08X) - a modded ROM, another region, or a bad dump; this build loads the stock cart only (modded ROMs need the mod-loader build) - %s\n",
                path, crc32, ROMDATA_STOCK_CRC32, strict ? "skipped" : "NOT loaded");
        free(buf);
        return 0;
    }

    /* it is the stock GoldenEye image: move it high (see the file comment) */
    high = (u8 *) portAllocHighDedicated((u32) len);
    if (high == NULL) {
        portConLogf("ge: romdata: no high address for the %ld-byte ROM - keeping the heap copy (segment refs into it would misresolve in fast3d)\n", len);
        high = buf;
    } else {
        memcpy(high, buf, (size_t) len);
        free(buf);
    }
    sRom = high;
    sRomSize = (u32) len;
    sRomSource = source;
    snprintf(sRomPath, sizeof(sRomPath), "%s", path);

    /* always-on: which candidate won and what it was */
    portConLogf("ge: romdata: ROM = %s (%s) %u bytes, %s\n", path, romdataSourceName(source), sRomSize,
            order == ORDER_Z64 ? "z64 order" : order == ORDER_V64 ? ".v64 byte-swapped, normalised" : ".n64 word-swapped, normalised");
    hcrc1 = be32(sRom + 0x10);
    hcrc2 = be32(sRom + 0x14);
    romdataCrc6102(sRom, &crc1, &crc2);
    portConLogf("ge: romdata: header \"%s\" %.4s cic-crc %08X/%08X (computed %08X/%08X, %s), image crc32 %08X = stock GoldenEye 007 (U)\n",
            title, (const char *) sRom + 0x3B, hcrc1, hcrc2, crc1, crc2,
            (crc1 == hcrc1 && crc2 == hcrc2) ? "consistent" : "STALE - emulators may refuse it; the port does not care",
            crc32);
    return 1;
}

/* ==== file-table classes (text banks) =================================== */

extern fileentry file_resource_table[];                       /* ob.c (compiled, name + linked data) */
extern resource_lookup_data_entry resource_lookup_data_array[]; /* ob.c (rom_size etc.) */
extern s32 file_entry_max;

enum romfileclass { CLASS_NONE = 0, CLASS_TEXT, CLASS_BRIEF, CLASS_SETUP, CLASS_STAN, CLASS_MODEL, CLASS_BG };
static const char *const sClassName[] = { "none", "text", "brief", "setup", "stan", "model", "bg" };

extern s32 romfileSetupConvert(u8 *data, u32 size, const char *name, const u8 *compiled); /* romfile_setup.c */
extern s32 romfileStanConvert(u8 *data, u32 size, const char *name, const u8 *compiled);  /* romfile_stan.c */

/* GE_ROMTEXT / GE_ROMBRIEF / GE_ROMSETUP / GE_ROMSTAN = 0: that class stays
 * compiled-in with the ROM loaded - the per-class A/B knobs */
static int romdataClassEnabled(enum romfileclass cls)
{
    static int cache[7] = { -1, -1, -1, -1, -1, -1, -1 };
    static const char *const env[7] = { NULL, "GE_ROMTEXT", "GE_ROMBRIEF", "GE_ROMSETUP", "GE_ROMSTAN", "GE_ROMMODEL", "GE_ROMBG" };
    if (cache[cls] < 0) {
        const char *e = env[cls] ? getenv(env[cls]) : NULL;
        cache[cls] = !(e != NULL && e[0] == '0');
        if (!cache[cls]) {
            portConLogf("ge: romdata: %s=0: %s files stay compiled-in (A/B)\n", env[cls], sClassName[cls]);
        }
    }
    return cache[cls];
}

static u8 sServed[OBJ_INDEX_MAX];          /* 1 = this compiled index now points at the ROM */
static u8 *sCompiledData[OBJ_INDEX_MAX];   /* the linked data it replaced (probe), or NULL */

/* the class of a file from its ROM name (the prefix grammar in
 * docs/port/rom-asset-loading.md section 1): only the classes a phase has
 * a preprocess pass for are served */
static enum romfileclass romdataClassOf(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return CLASS_NONE;
    }
    if (name[0] == 'L' && strchr(name, '/') == NULL) {
        return CLASS_TEXT; /* L<lvl>E, L<lvl>J */
    }
    if (strncmp(name, "Ubrief", 6) == 0) {
        return CLASS_BRIEF; /* Ubrief<lvl>Z */
    }
    if (strncmp(name, "Usetup", 6) == 0 || strncmp(name, "Ump_setup", 9) == 0) {
        return CLASS_SETUP; /* Usetup<lvl>Z, Ump_setup<lvl>Z */
    }
    if (strncmp(name, "Tbg_", 4) == 0) {
        return CLASS_STAN; /* Tbg_<lvl>_all_p_stanZ */
    }
    if (strncmp(name, "bg/", 3) == 0) {
        return CLASS_BG; /* bg/bg_<lvl>_all_p.seg - converted in load_bg_file */
    }
    {
        size_t l = strlen(name);
        if (l > 2 && name[l - 1] == 'Z' && (name[0] == 'C' || name[0] == 'G' || name[0] == 'P') && strchr(name, '/') == NULL) {
            /* the three inline-texture menu models embed segment-5 texture
             * blobs (objecthandler_2.c portSwapInlineTextures) that the model
             * converter does not yet handle - keep them compiled for now */
#ifndef GE_NATIVE_64
            if (strcmp(name, "PlegalpageZ") == 0 || strcmp(name, "PnintendologoZ") == 0 || strcmp(name, "PgoldeneyelogoZ") == 0) {
                return CLASS_NONE;
            }
#endif
            return CLASS_MODEL; /* C<name>Z chr, G<name>Z gun, P<name>Z prop - converted in load_object_fill_header */
        }
    }
    return CLASS_NONE;
}

/* Called by obInit after its size loop: for each compiled-in entry of a served
 * class that the ROM's directory also has (matched by name), point hw_address
 * at the ROM bytes and set rom_size. The vanilla fileIndexLoadTo* path then
 * loads it from the ROM. */
void romdataFilesApply(void)
{
    s32 i;
    s32 served = 0, missing = 0;
    const char *e;

    if (!sLoaded) {
        return;
    }
    (void) e;
    for (i = 1; i < file_entry_max && i < OBJ_INDEX_MAX; i++) {
        const char *name = file_resource_table[i].filename;
        s32 r;
        enum romfileclass cls = romdataClassOf(name);
        if (cls == CLASS_NONE || !romdataClassEnabled(cls)) {
            continue;
        }
        r = romdataFileIndex(name);
        if (r < 0 || sFiles[r].size == 0) {
            portConLogf("ge: romdata: file %-20s not in this ROM's directory - keeping the compiled-in copy\n", name);
            missing++;
            continue;
        }
        sCompiledData[i] = file_resource_table[i].hw_address;
        file_resource_table[i].hw_address = sRom + sFiles[r].romofs;
        resource_lookup_data_array[i].rom_size = (u32) sFiles[r].size;
        sServed[i] = 1;
        served++;
        if (sDebug) {
            portConLogf("ge: romdata: file %-20s served from ROM 0x%06X (%u bytes%s)\n", name, sFiles[r].romofs,
                    sFiles[r].size, sFiles[r].compressed ? ", 1172" : "");
        }
    }
    portConLogf("ge: romdata: file table: %d files served from the ROM (text, briefings, setups, stans)%s\n", served,
            missing ? " (some missing - compiled copies kept)" : "");

    /* GE_ROMPROBE=1: run every served file through the vanilla load path and
     * its preprocess+probe at boot, comparing all setups, stans, briefings and
     * text banks against their compiled-in copies. This is the regression
     * check, since a stage load only exercises its own files. */
    e = getenv("GE_ROMPROBE");
    if (e != NULL && e[0] != '\0' && e[0] != '0') {
        u32 cap = 2u << 20;
        u8 *scratch = (u8 *) malloc(cap);
        s32 n = 0;
        if (scratch != NULL) {
            for (i = 1; i < file_entry_max && i < OBJ_INDEX_MAX; i++) {
                if (sServed[i] && romdataClassOf(file_resource_table[i].filename) != CLASS_BG) {
                    /* bg files are used in place (obGetBGFileData), never loaded
                     * through a bank, and can exceed the scratch buffer - skip */
                    /* the load path writes poolRemaining / rom_remaining for the
                     * entry - the real stage load must see them untouched */
                    resource_lookup_data_entry saved = resource_lookup_data_array[i];
                    fileIndexLoadToAddr(i, FILELOADMETHOD_DEFAULT, scratch, (s32) cap);
                    resource_lookup_data_array[i] = saved;
                    n++;
                }
            }
            free(scratch);
        }
        portConLogf("ge: romdata: GE_ROMPROBE: %d served files loaded and probed\n", n);
    }
}

s32 romdataFileServed(s32 index)
{
    return (index > 0 && index < OBJ_INDEX_MAX) ? sServed[index] : 0;
}

/* the pristine compiled-in blob a served file replaced (or NULL) - the model
 * converter's runtime probe compares its output against it */
const u8 *romdataFileCompiled(s32 index)
{
    return (index > 0 && index < OBJ_INDEX_MAX) ? sCompiledData[index] : NULL;
}

/* TEXT BANKS: the ROM file is a table of big-endian, bank-relative offsets
 * followed by strings, while langGet's PC code expects absolute pointers like
 * the compiled-in banks, so rebase in place. The table size is the smallest
 * non-zero offset / 4; when the compiled bank is linked, the probe strcmp's
 * every slot. */
/* PORT STRING PATCHES: the port relabelled a few vanilla strings, so they are
 * applied by bank + slot after the rebase (the probe compares after patching,
 * so a stock cart still reads "identical"). Append-only; keep each string's
 * trailing '\n'. */
static const struct {
    const char *bank;
    u32 slot;
    const char *text;
} sTextPatches[] = {
    { "LoptionsE", 24, "hud\n" },           /* was "ratio": Wide pushes the HUD to the window edges (options.h);
                                             * formerly "hud centering", before Health
                                             * got its own option */
    { "LoptionsE", 34, "4:3\n" },           /* was "normal" */
    { "LoptionsE", 35, "wide\n" },          /* was "16:9" */
};

static void romdataTextPreprocess(s32 index, u8 *data, u32 size)
{
    u32 n = 0;
    u32 first = 0xFFFFFFFFu;
    u32 i;
    u32 differ = 0;
    u32 firstdiff = 0;
    u32 patched = 0;
    const char *bank = file_resource_table[index].filename;
    const char *const *compiled = (const char *const *) sCompiledData[index];

    if (size >= 2 && data[0] == 0x11 && data[1] == 0x72) {
        portConLogf("ge: romdata: text %s arrived still 1172-compressed (raw load) - not rebased\n",
                file_resource_table[index].filename);
        return;
    }
    for (i = 0; i + 4 <= size; i += 4) {
        u32 off = be32(data + i);
        if (i >= first) {
            break; /* reached the string area */
        }
        if (off != 0 && off < first) {
            first = off;
        }
        n++;
    }
    if (first == 0xFFFFFFFFu || first > size || (first & 3) != 0) {
        /* the cut stages' banks are 32 zero bytes: an empty table, nothing to rebase */
        for (i = 0; i < size && data[i] == 0; i++) {
        }
        if (i == size) {
            if (sDebug) {
                portConLogf("ge: romdata: text %-8s empty bank (%u zero bytes)\n", bank, size);
            }
            return;
        }
        portConLogf("ge: romdata: text %s: no offset table found (size %u) - left as is\n", bank, size);
        return;
    }
    n = first / 4;
    for (i = 0; i < n; i++) {
        u32 off = be32(data + i * 4);
        u32 v = 0;
        if (off != 0) {
            if (off >= size) {
                portConLogf("ge: romdata: text %s: slot %u offset 0x%X beyond the %u-byte bank - zeroed\n",
                        file_resource_table[index].filename, i, off, size);
            } else {
#ifdef GE_NATIVE_64
                v = off;
#else
                v = (u32) (uintptr_t) (data + off);
#endif
            }
        }
        {
            size_t p;
            for (p = 0; p < sizeof(sTextPatches) / sizeof(sTextPatches[0]); p++) {
                if (sTextPatches[p].slot == i && strcmp(sTextPatches[p].bank, bank) == 0) {
#ifdef GE_NATIVE_64
                    if (v != 0 && strlen(sTextPatches[p].text) <= strlen((char *)data + v)) {
                        strcpy((char *)data + v, sTextPatches[p].text);
                    }
#else
                    v = (u32) (uintptr_t) sTextPatches[p].text;
#endif
                    patched++;
                }
            }
        }
        if (compiled != NULL && v != 0) {
            /* a NULL ROM slot is not compared: the generated char *LxE[]
             * arrays drop trailing NULL slots, so compiled[i] may not exist */
            const char *c = compiled[i];
#ifdef GE_NATIVE_64
            const char *r = (const char *)data + v;
#else
            const char *r = (const char *) (uintptr_t) v;
#endif
            if (c == NULL || strcmp(c, r) != 0) {
                if (differ == 0) {
                    firstdiff = i;
                }
                differ++;
            }
        }
        memcpy(data + i * 4, &v, 4);
    }
    if (compiled != NULL) {
        if (differ == 0) {
            portConLogf("ge: romdata: text %-8s %3u slots, identical to the compiled-in bank%s\n", bank, n,
                    patched ? " (port relabels applied)" : "");
        } else {
            portConLogf("ge: romdata: text %-8s %3u slots, %u DIFFER from the compiled-in bank (first slot %u) - a mod, or a compiled bank that was patched\n",
                    bank, n, differ, firstdiff);
        }
    } else {
        portConLogf("ge: romdata: text %-8s %3u slots from the ROM%s\n", bank, n, patched ? " (port relabels applied)" : "");
    }
}

void romdataFilePreprocess(s32 index, u8 *data, u32 size)
{
    if (!romdataFileServed(index) || data == NULL) {
        return;
    }
    if (size == 0) {
        portConLogf("ge: romdata: file %s: load produced 0 bytes (buffer too small for the compressed image?)\n",
                file_resource_table[index].filename);
        return;
    }
    switch (romdataClassOf(file_resource_table[index].filename)) {
    case CLASS_TEXT:
        romdataTextPreprocess(index, data, size);
        break;
    case CLASS_BRIEF:
        /* struct BriefStruct: u16 brief[4] + 10 x {u16 text, u16 difficulty} = 24 halfwords */
        if (size >= 48) {
            u32 i;
            for (i = 0; i < 48; i += 2) {
                u8 t = data[i];
                data[i] = data[i + 1];
                data[i + 1] = t;
            }
            if (sCompiledData[index] != NULL) {
                portConLogf("ge: romdata: brief %-16s %s the compiled-in file\n", file_resource_table[index].filename,
                        memcmp(sCompiledData[index], data, 48) == 0 ? "identical to" : "DIFFERS from");
            }
        } else {
            portConLogf("ge: romdata: brief %s: %u bytes, expected 48\n", file_resource_table[index].filename, size);
        }
        break;
    case CLASS_SETUP:
#ifndef GE_NATIVE_64
        romfileSetupConvert(data, size, file_resource_table[index].filename, sCompiledData[index]);
#endif
        break;
    case CLASS_STAN:
        romfileStanConvert(data, size, file_resource_table[index].filename, sCompiledData[index]);
        break;
    case CLASS_MODEL:
        /* models are converted in load_object_fill_header (objecthandler_2.c),
         * where the compiled header carries numSwitches/numtextures - the ROM
         * bytes are just served here */
        break;
    case CLASS_BG:
        /* bg files are used in place from hw_address (obGetBGFileData), not
         * loaded through a bank, so the conversion is triggered in load_bg_file
         * (bg.c) via romfileBgConvert - nothing to do on this path */
        break;
    default:
        break;
    }
}

/* ==== the texture directory (g_Textures) and the image bank base ========= */

/* Load the texture table (g_Textures) and texture bank base from this cart:
 * walk the table, copy it over the compiled g_Textures before
 * image_entries_load sums the sizes, and take the bank base from the file
 * table. A table that doesn't reach its sentinel inside the ROM keeps the
 * compiled-in directory. */
#define ROMDATA_IMAGETABLE_OFS    0x28570u
#define ROMDATA_IMAGETABLE_STRIDE 8u
#define ROMDATA_IMAGETABLE_MASK   0x00FFFFFFu

static u32 sImagesBase, sImagesSize; /* the bank, from the file table; 0 = static romseg_list entry */

/* Dry-run walk of the texture table: report whether it looks valid (sentinel
 * found, >= 100 entries, bank fits in the ROM) and return its entry count and
 * summed size. g_Textures is never half-written by a table that fails. */
static int romdataTexTableFits(u32 stride, u32 mask, u32 *outn, u32 *outtot)
{
    u32 base = sFiles[sFileCount - 1].romofs + 16;
    u32 o, n = 0, total = 0;

    for (o = ROMDATA_IMAGETABLE_OFS; o + stride <= sCdataSize && n < PORT_TEXTURES_MAX - 1; o += stride, n++) {
        u32 size = be32(sCdata + o) & mask;
        if (size == 0xFFFFu) {
            *outn = n;
            *outtot = total;
            return n >= 100 && base + total <= sRomSize;
        }
        if (size > sRomSize) {
            return 0; /* a single entry larger than the whole cart */
        }
        total += size;
        if (base + total > sRomSize) {
            return 0; /* the bank ran off the end */
        }
    }
    return 0; /* no sentinel */
}

static void romdataImageTableApply(void)
{
    extern struct image_entry g_Textures[];
    const u32 mask = ROMDATA_IMAGETABLE_MASK, stride = ROMDATA_IMAGETABLE_STRIDE;
    u32 o, n = 0, total = 0, differ = 0, i;
    u32 tn, tt;

    if (sCdataSize < ROMDATA_IMAGETABLE_OFS + 8) {
        return;
    }
    if (!romdataTexTableFits(stride, mask, &tn, &tt)) {
        portConLogf("ge: romdata: texture table at cdata+0x%X does not fit the ROM (stock layout) - compiled-in table kept; textures may be wrong\n",
                ROMDATA_IMAGETABLE_OFS);
        return;
    }

    for (o = ROMDATA_IMAGETABLE_OFS; o + stride <= sCdataSize && n < PORT_TEXTURES_MAX - 1; o += stride, n++) {
        u32 w0 = be32(sCdata + o);
        u32 w1 = be32(sCdata + o + 4);
        u32 size = w0 & mask;
        if (size == 0xFFFFu) {
            break; /* the sentinel */
        }
        if (n < PORT_TEXTURES_MAX && g_Textures[n].dataoffset != size) {
            differ++;
        }
        g_Textures[n].hitSound = (w0 >> 28) & 0xF;
        g_Textures[n].hitTexture = (w0 >> 24) & 0xF;
        g_Textures[n].dataoffset = size;
        g_Textures[n].flag3 = (w1 >> 28) & 0xF;
        g_Textures[n].flag4 = (w1 >> 24) & 0xF;
        g_Textures[n].flag5 = (w1 >> 20) & 0xF;
        g_Textures[n].flag6 = (w1 >> 16) & 0xF;
        total += size;
    }
    if (n < 100 || o + stride > sCdataSize) {
        portConLogf("ge: romdata: texture table at cdata+0x%X did not parse (%u entries) - compiled-in table kept\n",
                ROMDATA_IMAGETABLE_OFS, n);
        return;
    }
    /* the sentinel, and anything past it */
    for (i = n; i < PORT_TEXTURES_MAX; i++) {
        g_Textures[i].hitSound = 0;
        g_Textures[i].hitTexture = 0;
        g_Textures[i].dataoffset = (i == n) ? 0xFFFFu : 0;
        g_Textures[i].flag3 = g_Textures[i].flag4 = g_Textures[i].flag5 = g_Textures[i].flag6 = 0;
    }

    /* the bank starts right after the last file (the 16-byte ob_end) */
    sImagesBase = sFiles[sFileCount - 1].romofs + 16;
    sImagesSize = total;
    if (sImagesBase + total > sRomSize) {
        portConLogf("ge: romdata: image bank 0x%X + %u bytes runs past the %u-byte image - textures will be garbage\n",
                sImagesBase, total, sRomSize);
    }
    portConLogf("ge: romdata: texture table: %u entries, %s the compiled-in table; image bank at 0x%X, %u bytes%s\n",
            n, differ ? "DIFFERS from" : "identical to", sImagesBase, total,
            sImagesBase == 0x8F7DF0u && total == 0x2EEF18u ? " (stock)" : " (non-stock layout)");
}

/* ==== accessors ======================================================== */

s32 romdataLoaded(void)
{
    return sLoaded;
}

/* the inflated data segment ("21990 cdata"): compiled tables the ROM carries
 * but no directory lists (fog/environment, model headers, ...) live here at
 * their N64 RAM offset minus ROMDATA_CDATA_VADDR. NULL until a ROM is loaded. */
const u8 *romdataCdata(u32 *size)
{
    if (size != NULL) {
        *size = sLoaded ? sCdataSize : 0;
    }
    return sLoaded ? sCdata : NULL;
}

u32 romdataCdataVaddr(void)
{
    return ROMDATA_CDATA_VADDR;
}

const u8 *romdataRom(u32 *size)
{
    if (size != NULL) {
        *size = sRomSize;
    }
    return sRom;
}

s32 romdataFileCount(void)
{
    return sFileCount;
}

const struct romfile *romdataFile(s32 index)
{
    return (index >= 0 && index < sFileCount) ? &sFiles[index] : NULL;
}

s32 romdataFileIndex(const char *name)
{
    s32 i;
    for (i = 1; i < sFileCount; i++) {
        if (strcmp(sFiles[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

const u8 *romdataSeg(const char *name, u32 *size)
{
    size_t i;
    if (sRom == NULL) {
        return NULL;
    }
    if (sImagesBase != 0 && strcmp(name, "images") == 0) {
        /* the bank as the file table places it, not the stock offset */
        if (size != NULL) {
            *size = sImagesSize;
        }
        return sImagesBase + sImagesSize <= sRomSize ? sRom + sImagesBase : NULL;
    }
    for (i = 0; i < sizeof(sSegs) / sizeof(sSegs[0]); i++) {
        if (strcmp(sSegs[i].name, name) == 0 && sSegs[i].romofs + sSegs[i].size <= sRomSize) {
            if (size != NULL) {
                *size = sSegs[i].size;
            }
            return sRom + sSegs[i].romofs;
        }
    }
    return NULL;
}

/* ==== boot ============================================================= */

/* Why the candidate files were refused, for the missing-ROM message box: wrong
 * cart, ROM still inside a .zip, or no ROM at all. Files like Data's own
 * README.txt, desktop.ini and Thumbs.db don't count as ROM attempts. */
static int sRefusedCarts;    /* N64 images, or cart-sized files, that were refused */
static int sRefusedArchives; /* a .zip/.7z/.rar/.gz: the ROM was never extracted */

static void romdataNoteRefusal(const char *path)
{
    FILE *f = fopen(path, "rb");
    u8 m[4];
    long len;

    if (f == NULL) {
        return;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fread(m, 1, 4, f) != 4) {
        fclose(f);
        return;
    }
    fclose(f);
    if ((m[0] == 0x50 && m[1] == 0x4B && m[2] == 0x03 && m[3] == 0x04)     /* zip */
        || (m[0] == 0x37 && m[1] == 0x7A && m[2] == 0xBC && m[3] == 0xAF)  /* 7z */
        || (m[0] == 0x52 && m[1] == 0x61 && m[2] == 0x72 && m[3] == 0x21)  /* rar */
        || (m[0] == 0x1F && m[1] == 0x8B)) {                               /* gzip */
        sRefusedArchives++;
    } else if (romdataDetectOrder(m) != ORDER_UNKNOWN || len >= (long) (ROMDATA_ROM_SIZE / 2)) {
        sRefusedCarts++;
    }
}

s32 romdataInit(int argc, char **argv)
{
    extern void portShowErrorBox(const char *title, const char *text); /* video.c; weak no-op headless */
    const char *e;
    const char *argvrom = NULL;
    const char *flagrom = NULL;
    int i;

    e = getenv("GE_ROMDBG");
    sDebug = (e != NULL && e[0] != '\0' && e[0] != '0');

    romdataEnsureDir(ROMDATA_DATA_DIR);
    romdataReadIni();

    e = getenv("GE_ROMDATA");
    if (e != NULL && e[0] == '0') {
#ifdef GE_ROMDATA_FALLBACK
        portConLogf("ge: romdata: disabled (GE_ROMDATA=0) - compiled-in assets\n");
        return 0;
#else
        portConLogf("ge: romdata: GE_ROMDATA=0 ignored - this build has no compiled-in copies (GE_ROMDATA_FALLBACK=OFF)\n");
#endif
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom-file") == 0 && i + 1 < argc) {
            flagrom = argv[++i];
        } else if (strncmp(argv[i], "--rom-file=", 11) == 0) {
            flagrom = argv[i] + 11;
        } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            sArgLangJp = (v[0] == 'j' || v[0] == 'J');
        } else if (strncmp(argv[i], "--lang=", 7) == 0) {
            sArgLangJp = (argv[i][7] == 'j' || argv[i][7] == 'J');
        } else if (i == 1 && argv[i][0] != '-') {
            argvrom = argv[i]; /* Explorer drop: the dropped file's full path */
        }
    }

    if (argvrom != NULL) {
        if (romdataTryLoad(argvrom, ROMSRC_ARGV, 0)) goto done;
        romdataNoteRefusal(argvrom);
    }
    if (flagrom != NULL) {
        if (romdataTryLoad(flagrom, ROMSRC_ARGFLAG, 0)) goto done;
        romdataNoteRefusal(flagrom);
    }
    e = getenv("GE_ROM");
    if (e != NULL && e[0] != '\0') {
        if (romdataTryLoad(e, ROMSRC_ENV, 0)) goto done;
        romdataNoteRefusal(e);
    }
    if (sIniRom[0] != '\0') {
        char p[1200];
        romdataResolve(p, sizeof(p), sIniRom);
        if (romdataTryLoad(p, ROMSRC_INI, 0)) goto done;
        romdataNoteRefusal(p);
    }
    {
        static char names[ROMDATA_LIST_MAX][256];
        char datadir[1200];
        int n;
        snprintf(datadir, sizeof(datadir), "%s", portRootPath(ROMDATA_DATA_DIR));
        n = romdataListDir(datadir, 0, names, ROMDATA_LIST_MAX);
        for (i = 0; i < n; i++) {
            char p[1500];
            snprintf(p, sizeof(p), "%s" PATHSEP "%s", datadir, names[i]);
            if (romdataTryLoad(p, ROMSRC_DATADIR, 1)) goto done;
            romdataNoteRefusal(p); /* an EMPTY folder, a packed ROM and the
                                    * WRONG rom are different mistakes and get
                                    * different words */
        }
        if (n == 0) {
            portConLogf("ge: romdata: %s is empty\n", datadir);
        }
    }
    {
        char p[1200];
        snprintf(p, sizeof(p), "%s", portRootPath("baserom.u.z64"));
        if (romdataIsFile(p) && romdataTryLoad(p, ROMSRC_LEGACY, 0)) goto done;
        snprintf(p, sizeof(p), "%s", portRootPath(".." PATHSEP "baserom.u.z64"));
        if (romdataIsFile(p) && romdataTryLoad(p, ROMSRC_LEGACY, 0)) goto done;
    }

    portConLogf("ge: romdata: NO ROM - tried argv[1], --rom-file, GE_ROM, midas.ini [data] rom=, %s, baserom.u.z64\n",
            portRootPath(ROMDATA_DATA_DIR));
#ifdef GE_ROMDATA_FALLBACK
    portConLogf("ge: romdata: put your GoldenEye 007 (U) cart image (.z64/.v64/.n64) in %s or drop it onto the exe; running on the compiled-in assets\n",
            portRootPath(ROMDATA_DATA_DIR));
    return 0;
#else
    {
        /* Show a message box, since someone who double-clicked the exe never
         * sees the console; an empty folder and a folder of refused files get
         * different messages. Only real attempts count (N64 images or
         * cart-sized files), and a packed ROM is named as such so a README or
         * .zip is never called "a modded ROM". */
        char msg[1700];
        const char *dir = portRootPath(ROMDATA_DATA_DIR);

        if (sRefusedCarts > 0) {
            snprintf(msg, sizeof(msg),
                     "No usable GoldenEye 007 ROM found in ./%s.\n\n"
                     "%d cart image(s) were found, but none of them is a stock GoldenEye 007 (U) cart: "
                     "a modded ROM, another region, or a bad dump.\n"
                     "Please provide your ROM into this folder and try again.\n\n"
                     "Looked in:\n%s",
                     ROMDATA_DATA_DIR, sRefusedCarts, dir);
        } else if (sRefusedArchives > 0) {
            snprintf(msg, sizeof(msg),
                     "No GoldenEye 007 ROM found in ./%s.\n\n"
                     "The ROM is still packed in an archive (.zip/.7z/...). "
                     "Extract the .z64, .v64 or .n64 file from it into this folder and try again.\n\n"
                     "Looked in:\n%s",
                     ROMDATA_DATA_DIR, dir);
        } else {
            snprintf(msg, sizeof(msg),
                     "No GoldenEye 007 ROM found in ./%s.\n"
                     "Please provide your ROM into this folder and try again.\n\n"
                     "Looked in:\n%s\n\n"
                     "The ROM may be .z64, .v64 or .n64. You can also drag one onto the game.",
                     ROMDATA_DATA_DIR, dir);
        }
        portConLogf("ge: romdata: %s\n", msg);
        fflush(stderr);
        portShowErrorBox("GoldenEye 007", msg);
        exit(3);
    }
#endif

done:
    romdataCheckSegs(sRomSize);
    sLoaded = 1;
    /* the texture directory + bank base from THIS cart, before the images
     * consumer is pointed at the bank */
    romdataImageTableApply();
    /* images, music, ramrom and gunbarrel are read from the cart */
    portRomdataSegsApply();
    /* the fog / environment tables (bgfog.c fog_tables / fog_tables2) are a
     * compiled data table the cart carries in cdata but no directory lists;
     * convert this ROM's copy over the compiled one so a mod's custom fog,
     * sky and water import */
    {
        extern void portFogServeFromRom(void);
        portFogServeFromRom();
    }
    /* animation + fonts are filled from the ROM by their own
     * builders (animdata.c, segments.c portFontBuildSegments) when they
     * run; text banks are served through the file table (romdataFilesApply
     * from obInit, later in boot) */
#ifdef GE_ROMDATA_FALLBACK
    portConLogf("ge: romdata: loaded; all asset classes served from the ROM (Phases A-F); compiled-in copies kept for the GE_ROMDATA=0 fallback\n");
#else
    portConLogf("ge: romdata: loaded; all asset classes served from the ROM (Phases A-F); no compiled-in copies (GE_ROMDATA_FALLBACK=OFF)\n");
#endif
    return 1;
}
