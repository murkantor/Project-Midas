/**
 * External textures: BMP files under Data/Textures/ that replace or add game
 * textures (see docs/port/external-assets.md).
 *
 * ---- Named images ----
 *   Data/Textures/Base/<name>.bmp  ->  an sImageTableEntry that texSelect /
 *                                       display_image_at_position draw exactly
 *                                       like a ROM image (e.g. the Options and
 *                                       Exit game icons)
 *
 * No renderer change is needed. texSelect (othermodemicrocode.c) accepts an
 * entry whose `index` is either a texture number (< NUM_TEXTURES) or the RAM
 * address of the pixels. For an address it reads a u16 texture number 8 bytes
 * before the pixels, and when no pool holds that number it uses the entry's own
 * format/depth and draws from the address. So a texture made here is a 16-byte
 * 0xFF header (a number no pool holds, since texturenum is 12 bits), then the
 * pixels. The entry uses a native-width address.
 *
 * Format: RGBA16 in host byte order (the renderer reads RAM textures as native
 * u16), 1-bit alpha with 128 and over opaque. File: Windows BMP, 24-bit or
 * 32-bit BGRA, uncompressed or BI_BITFIELDS with the standard masks, either row
 * order. Read with plain stdio so it works in every build with no extra
 * dependency. Size: at most 64x64, because texSelect's single gDPLoadBlock has
 * a 12-bit texel count. Names: six characters by convention; up to fifteen
 * letters, digits, - and _ are accepted, and anything with a path separator or
 * dot is refused. A missing file is not an error (the ROM image is drawn), and
 * each name is looked up once per run, hit or miss, so the file system isn't
 * polled every frame.
 *
 * ---- ROM textures by number ----
 *   Data/Textures/Base/t02618.bmp  ->  replaces ROM texture 2618 wherever drawn
 *
 * The name is 't' plus the decimal texture number that GE_TEXDUMP prints. The
 * hook is texLoad (image.c): after a texture is inflated into its pool,
 * portTexReplaceRom rewrites those same bytes from the BMP, keeping the same
 * size, format, mip levels and palette, so nothing downstream notices. Larger
 * art is resampled down, colours are snapped to the format (CI textures keep
 * the ROM palette, each pixel taking the nearest entry), and mip levels are
 * regenerated. The pool layout is re-derived from the tex record and checked
 * against the inflater's byte count; on a mismatch the texture is left alone
 * and logged.
 *
 * ---- Contexts ----
 *   Data/Textures/Base/      always
 *   Data/Textures/Options/   only while the Options folder or one of its
 *                            sheets is the current menu
 *
 * The wallet model's textures are shared by every folder, so the pool bytes are
 * swapped at each menu transition. Every ROM texture with a file in either
 * directory is registered at load with a copy of its ROM bytes and its encoded
 * bytes per context; portTexContextSet rewrites the live pool from the right
 * copy and clears the renderer's address-keyed texture cache. Options/ falls
 * back to Base/ per texture, and Base/ falls back to the ROM.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h> /* _mkdir */
#endif
#include <ultra64.h>
#include <bondtypes.h>
#include <PR/gbi.h>
#include "game/image.h"   /* struct tex, TEXFORMAT_*, PORT_TEXTURES_MAX */

extern const char *portRootPath(const char *name); /* main.c: <exe dir>/name */
extern void portConLogf(const char *fmt, ...);       /* port_console.c */
extern struct sImageTableEntry *crosshairimage;      /* image_bank.c - "crosshair colour", end of file */
void portTexCrosshairRefresh(s32 reorig);

/* The crosshair's texture number, captured while crosshairimage->index still
 * holds a number: after the first texSelect the index becomes a pixel address
 * (anything at or above 0x1000), and pool lookups by it would fail. */
static s32 sXhairNum = -1;

static s32 texCrosshairNum(void)
{
    if (crosshairimage != NULL && crosshairimage->index < 0x1000u)
    {
        sXhairNum = (s32) crosshairimage->index;
    }
    return sXhairNum;
}

#define PORT_TEX_DIR        "Data/Textures/Base"
#define PORT_TEX_DIR_OPTIONS "Data/Textures/Options"
/* the contexts, in fallback order: an Options texture stands in for a Base
 * one, a Base one for the ROM's */
enum { PORT_TEXCTX_BASE, PORT_TEXCTX_OPTIONS, PORT_TEXCTX_COUNT };
static const char *const sCtxDir[PORT_TEXCTX_COUNT] = { PORT_TEX_DIR, PORT_TEX_DIR_OPTIONS };
static s32 sCtx = PORT_TEXCTX_BASE;   /* the menu we are in, as portTexContextSet last said */
#define PORT_TEX_MAX        32      /* distinct names per run */
#define PORT_TEX_NAME_MAX   15
#define PORT_TEX_MAX_SIDE   64      /* one load block: 4096 texels */
#define PORT_TEX_HEADER     16      /* bytes before the pixels; texSelect reads a u16 at -8 */

struct portExtTex {
    char name[PORT_TEX_NAME_MAX + 1];
    s32 state;              /* 0 untried, 1 loaded, -1 missing or refused */
    u8 *blob;               /* header + pixels, allocated once, never freed */
    sImageTableEntry entry;
};

static struct portExtTex sTex[PORT_TEX_MAX];
static s32 sTexCount;

/* create <exe dir>/rel when missing, so a fresh install shows where the
 * files go - romdata.c does the same for Data itself */
static void texEnsureDir(const char *rel)
{
    const char *path = portRootPath(rel);
    struct stat st;
    int rc;

    if (stat(path, &st) == 0)
    {
        return;
    }
#ifdef _WIN32
    rc = _mkdir(path);
#else
    rc = mkdir(path, 0755);
#endif
    if (rc == 0)
    {
        portConLogf("ge: textures: created %s\n", path);
    }
}

static u32 bmpU32(const u8 *p) { return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24); }
static u16 bmpU16(const u8 *p) { return (u16) (p[0] | (p[1] << 8)); }

/**
 * Decode a BMP into top-down RGBA8. 0 = not a BMP this reader takes; the
 * reason is logged so a player can fix the file rather than guess.
 */
static s32 texDecodeBmp(const char *path, const u8 *f, s32 len, s32 maxside, s32 *outw, s32 *outh, u8 **outrgba)
{
    u32 pixofs, dibsize, bpp, comp, rowbytes;
    s32 w, h, topdown, y, x, anyalpha;
    u8 *rgba;

    if (len < 54 || f[0] != 'B' || f[1] != 'M')
    {
        portConLogf("ge: textures: %s is not a BMP\n", path);
        return 0;
    }
    pixofs  = bmpU32(f + 10);
    dibsize = bmpU32(f + 14);
    w       = (s32) bmpU32(f + 18);
    h       = (s32) bmpU32(f + 22);
    bpp     = bmpU16(f + 28);
    comp    = bmpU32(f + 30);
    topdown = (h < 0);
    if (topdown) h = -h;
    if (dibsize < 40 || w <= 0 || h <= 0 || (bpp != 24 && bpp != 32) || (comp != 0 && comp != 3))
    {
        portConLogf("ge: textures: %s: need an uncompressed 24- or 32-bit BMP (got %u bpp, compression %u)\n",
                    path, (unsigned) bpp, (unsigned) comp);
        return 0;
    }
    if (comp == 3)
    {   /* BI_BITFIELDS is only decoded as plain BGR(A) bytes, so take it
         * only with exactly those masks (in the V2+ header, or the 12 bytes
         * after a 40-byte one; the alpha mask only from a V3+ header) */
        u32 mr = 0, mg = 0, mb = 0, ma = 0;

        if (bpp == 32 && len >= 66)
        {   /* only read what the file holds */
            mr = bmpU32(f + 54);
            mg = bmpU32(f + 58);
            mb = bmpU32(f + 62);
            ma = (dibsize >= 56 && len >= 70) ? bmpU32(f + 66) : 0;
        }
        if (bpp != 32 || len < 66 || mr != 0x00FF0000u || mg != 0x0000FF00u || mb != 0x000000FFu
            || (ma != 0 && ma != 0xFF000000u))
        {
            portConLogf("ge: textures: %s: BI_BITFIELDS with non-standard masks - resave as plain 32-bit BGRA\n",
                        path);
            return 0;
        }
    }
    if (w > 1024 || h > 1024 || (maxside > 0 && (w > maxside || h > maxside)))
    {
        portConLogf("ge: textures: %s is %dx%d - at most %dx%d\n", path, w, h,
                    maxside > 0 ? maxside : 1024, maxside > 0 ? maxside : 1024);
        return 0;
    }
    rowbytes = ((u32) w * (bpp / 8) + 3u) & ~3u;
    if (pixofs > (u32) len || rowbytes * (u32) h > (u32) len - pixofs)   /* no u32 wrap */
    {
        portConLogf("ge: textures: %s is truncated\n", path);
        return 0;
    }
    rgba = (u8 *) malloc((size_t) w * (size_t) h * 4);
    if (rgba == NULL)
    {
        return 0;
    }
    anyalpha = 0;
    for (y = 0; y < h; y++)
    {
        const u8 *row = f + pixofs + rowbytes * (u32) (topdown ? y : (h - 1 - y));
        u8 *out = rgba + (size_t) y * (size_t) w * 4;

        for (x = 0; x < w; x++)
        {
            const u8 *px = row + x * (bpp / 8);
            out[x * 4 + 0] = px[2];   /* BGR(A) on disk */
            out[x * 4 + 1] = px[1];
            out[x * 4 + 2] = px[0];
            out[x * 4 + 3] = (bpp == 32) ? px[3] : 0xFF;
            if (bpp == 32 && px[3] != 0)
            {
                anyalpha = 1;
            }
        }
    }
    if (bpp == 32 && !anyalpha)
    {   /* a 32-bit file whose fourth byte is all zero has no alpha channel
         * in it (BI_RGB writers leave it 0) - treat it as opaque rather than
         * drawing nothing */
        for (x = 0; x < w * h; x++)
        {
            rgba[x * 4 + 3] = 0xFF;
        }
    }
    *outw = w;
    *outh = h;
    *outrgba = rgba;
    return 1;
}

static s32 texNameOk(const char *name)
{
    s32 i;

    if (name == NULL || name[0] == '\0')
    {
        return 0;
    }
    for (i = 0; name[i] != '\0'; i++)
    {
        const char c = name[i];
        const s32 ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';

        if (!ok || i >= PORT_TEX_NAME_MAX)
        {
            return 0;
        }
    }
    return 1;
}

/**
 * Data/Textures/Base/<name>.bmp -> top-down RGBA8, malloc'd. 0 = no file
 * (silent) or a file the reader refused (logged). `maxside` caps the size;
 * 0 = no cap beyond the reader's own sanity limit.
 */
static s32 texReadBmpFile(const char *dir, const char *name, s32 maxside, s32 *w, s32 *h, u8 **rgba)
{
    char rel[128];
    const char *path;
    FILE *f;
    long len;
    u8 *file;

    snprintf(rel, sizeof(rel), "%s/%s.bmp", dir, name);
    path = portRootPath(rel);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        return 0;   /* silently: the ROM image stands */
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* room for the decoder's own limit, a 32-bit 1024x1024 (exactly
     * 4 MB of pixels), plus any header (V5, masks, an ICC profile) */
    if (len <= 0 || len > (4L << 20) + 65536L)
    {
        fclose(f);
        portConLogf("ge: textures: %s: bad size %ld (max %ld)\n", path, len, (4L << 20) + 65536L);
        return 0;
    }
    file = (u8 *) malloc((size_t) len);
    if (file == NULL || fread(file, 1, (size_t) len, f) != (size_t) len)
    {
        fclose(f);
        free(file);
        return 0;
    }
    fclose(f);
    if (!texDecodeBmp(path, file, (s32) len, maxside, w, h, rgba))
    {
        free(file);
        return 0;
    }
    free(file);
    return 1;
}

/** Load one, into slot t. 1 = t->entry is drawable. */
static s32 texLoadSlot(struct portExtTex *t)
{
    char rel[128];
    u8 *rgba = NULL;
    s32 w = 0, h = 0, i, stride;
    u16 *px;
    extern void *portAllocHigh(u32 size); /* pcsched.c */

    if (!texReadBmpFile(PORT_TEX_DIR, t->name, PORT_TEX_MAX_SIDE, &w, &h, &rgba))
    {
        return 0;
    }
    snprintf(rel, sizeof(rel), PORT_TEX_DIR "/%s.bmp", t->name);

    /* Rows are padded to 4 texels to match how texSelect loads 16-bit images.
     * Allocated from the high arena, not the CRT heap, because fast3d's
     * seg_addr reads addresses below 0x10000000 as segment references. */
    stride = (w + 3) & ~3;
    t->blob = (u8 *) portAllocHigh(PORT_TEX_HEADER + (u32) stride * (u32) h * 2);
    if (t->blob == NULL)
    {
        free(rgba);
        return 0;
    }
    memset(t->blob, 0xFF, PORT_TEX_HEADER);   /* aa[-4] = 0xFFFF: no pool texture has this number */
    px = (u16 *) (t->blob + PORT_TEX_HEADER);
    memset(px, 0, (size_t) stride * (size_t) h * 2);   /* the padding texels: transparent */
    for (i = 0; i < w * h; i++)
    {   /* RGBA16 rrrrrgggggbbbbba, stored as the host's own u16 (see above) */
        const u8 r = rgba[i * 4 + 0] >> 3, g = rgba[i * 4 + 1] >> 3, b = rgba[i * 4 + 2] >> 3;
        const u8 a = (rgba[i * 4 + 3] >= 128) ? 1 : 0;

        /* bottom-up, like texRomPicture - the Files bar draws it with
         * flipY, so row 0 is the BOTTOM of the picture as drawn */
        px[(h - 1 - i / w) * stride + (i % w)] = (u16) ((r << 11) | (g << 6) | (b << 1) | a);
    }
    free(rgba);

    memset(&t->entry, 0, sizeof(t->entry));
    t->entry.index  = (uintptr_t) px;
    t->entry.width  = (u8) w;
    t->entry.height = (u8) h;
    t->entry.level  = 0;
    t->entry.format = G_IM_FMT_RGBA;
    t->entry.depth  = G_IM_SIZ_16b;
    t->entry.flagsS = G_TX_NOMIRROR | G_TX_CLAMP;
    t->entry.flagsT = G_TX_NOMIRROR | G_TX_CLAMP;
    portConLogf("ge: textures: %s %dx%d loaded\n", rel, w, h);
    return 1;
}

/**
 * The external texture called `name`, or NULL if there is none (or the name
 * is refused). Looked up once per name per run.
 */
struct sImageTableEntry *portTexExternal(const char *name)
{
    struct portExtTex *t;
    s32 i;

    if (!texNameOk(name))
    {
        return NULL;
    }
    if (sTexCount == 0)
    {
        texEnsureDir("Data/Textures");
        texEnsureDir(PORT_TEX_DIR);
        texEnsureDir(PORT_TEX_DIR_OPTIONS);
    }
    for (i = 0; i < sTexCount; i++)
    {
        if (strcmp(sTex[i].name, name) == 0)
        {
            return (sTex[i].state > 0) ? &sTex[i].entry : NULL;
        }
    }
    if (sTexCount >= PORT_TEX_MAX)
    {
        return NULL;
    }
    t = &sTex[sTexCount++];
    memset(t, 0, sizeof(*t));
    strncpy(t->name, name, PORT_TEX_NAME_MAX);
    t->state = texLoadSlot(t) ? 1 : -1;
    return (t->state > 0) ? &t->entry : NULL;
}

/* ---- ROM textures by number ------------------------------------------- */

/* The registry of every ROM texture with a file in some context: `orig` holds the
 * ROM's pool bytes and `enc[c]` the encoded bytes for context c (NULL if none),
 * all `bytes` long, refreshed each time the texture loads. */
#define PORT_TEX_ROM_MAX 128   /* the first allocation; it grows */
struct portRomTex {
    s32 num;
    s32 bytes;
    u8 *orig;
    u8 *enc[PORT_TEXCTX_COUNT];
};
static struct portRomTex *sRom;
static s32 sRomCount, sRomCap;

/* one bit per texture number: "looked in every directory, there is none" -
 * so a stage load with 2000 textures costs one fopen per number per
 * directory per RUN, not per load */
static u8 sRomMiss[PORT_TEXTURES_MAX / 8];

/** Area-average resample of top-down RGBA8 src (sw x sh) into dst (dw x dh). */
static void texResample(const u8 *src, s32 sw, s32 sh, u8 *dst, s32 dw, s32 dh)
{
    s32 x, y, c;

    for (y = 0; y < dh; y++)
    {
        const s32 y0 = y * sh / dh;
        s32 y1 = (y + 1) * sh / dh;

        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < dw; x++)
        {
            const s32 x0 = x * sw / dw;
            s32 x1 = (x + 1) * sw / dw;
            s32 sum[4] = { 0, 0, 0, 0 };
            s32 n = 0, sy, sx;

            if (x1 <= x0) x1 = x0 + 1;
            for (sy = y0; sy < y1 && sy < sh; sy++)
            {
                for (sx = x0; sx < x1 && sx < sw; sx++)
                {
                    const u8 *p = src + ((size_t) sy * sw + sx) * 4;
                    for (c = 0; c < 4; c++) sum[c] += p[c];
                    n++;
                }
            }
            for (c = 0; c < 4; c++)
            {
                dst[((size_t) y * dw + x) * 4 + c] = (u8) (n ? sum[c] / n : 0);
            }
        }
    }
}

/* the pool's row stride in bytes for a level of width w, and the level's
 * size - image.c texChannelsToPixels / texAlignIndices, mirrored */
static s32 texRowStride(s32 gbifmt, s32 depth, s32 w)
{
    if (depth == G_IM_SIZ_32b) return ((w + 3) & ~3) * 4;
    if (depth == G_IM_SIZ_16b) return ((w + 3) & ~3) * 2;
    if (depth == G_IM_SIZ_8b)  return (w + 7) & ~7;
    if (gbifmt == G_IM_FMT_CI) return ((w + 1) / 2 + 7) & ~7;   /* texAlignIndices: indices, then 8-align */
    return ((w + 15) & ~15) / 2;                                 /* I4 / IA4 */
}

static u8 texLuma(const u8 *p)
{
    return (u8) ((p[0] * 77 + p[1] * 151 + p[2] * 28) >> 8);
}

/** Nearest palette entry to an RGBA8 pixel, for a CI texture. */
static s32 texNearestPal(const u16 *pal, s32 ncol, s32 isia, const u8 *p)
{
    s32 best = 0, bestd = 0x7FFFFFFF, i;

    for (i = 0; i < ncol; i++)
    {
        s32 d;

        if (isia)
        {
            const s32 di = (pal[i] >> 8) - texLuma(p);
            const s32 da = (pal[i] & 0xFF) - p[3];
            d = di * di + da * da;
        }
        else
        {
            const s32 dr = (((pal[i] >> 11) & 31) << 3) - p[0];
            const s32 dg = (((pal[i] >> 6) & 31) << 3) - p[1];
            const s32 db = (((pal[i] >> 1) & 31) << 3) - p[2];
            const s32 da = ((pal[i] & 1) ? 255 : 0) - (p[3] >= 128 ? 255 : 0);
            d = dr * dr + dg * dg + db * db + da * da * 4;
        }
        if (d < bestd)
        {
            bestd = d;
            best = i;
        }
    }
    return best;
}

/** Write one level's pixels in the pool's own format. */
static void texWriteLevel(u8 *dst, s32 gbifmt, s32 depth, s32 w, s32 h, const u8 *rgba,
                          const u16 *pal, s32 ncol, s32 isia)
{
    const s32 stride = texRowStride(gbifmt, depth, w);
    s32 x, y;

    for (y = 0; y < h; y++)
    {
        u8 *row = dst + (size_t) y * stride;

        for (x = 0; x < w; x++)
        {
            const u8 *p = rgba + ((size_t) y * w + x) * 4;

            if (gbifmt == G_IM_FMT_CI)
            {
                const s32 idx = texNearestPal(pal, ncol, isia, p);

                if (depth == G_IM_SIZ_8b)      row[x] = (u8) idx;
                else if ((x & 1) == 0)         row[x >> 1] = (u8) (idx << 4);
                else                           row[x >> 1] |= (u8) idx;
            }
            else if (gbifmt == G_IM_FMT_RGBA && depth == G_IM_SIZ_32b)
            {
                ((u32 *) row)[x] = ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
            }
            else if (gbifmt == G_IM_FMT_RGBA)
            {
                ((u16 *) row)[x] = (u16) ((p[0] >> 3) << 11 | (p[1] >> 3) << 6 | (p[2] >> 3) << 1 | (p[3] >= 128));
            }
            else if (gbifmt == G_IM_FMT_IA && depth == G_IM_SIZ_16b)
            {
                ((u16 *) row)[x] = (u16) (texLuma(p) << 8 | p[3]);
            }
            else if (gbifmt == G_IM_FMT_IA && depth == G_IM_SIZ_8b)
            {
                row[x] = (u8) ((texLuma(p) & 0xF0) | (p[3] >> 4));
            }
            else if (gbifmt == G_IM_FMT_IA)
            {   /* IA4: iii a */
                const u8 v = (u8) (((texLuma(p) >> 5) << 1) | (p[3] >= 128));
                if ((x & 1) == 0) row[x >> 1] = (u8) (v << 4); else row[x >> 1] |= v;
            }
            else if (depth == G_IM_SIZ_8b)
            {
                row[x] = texLuma(p);
            }
            else
            {   /* I4 */
                const u8 v = texLuma(p) >> 4;
                if ((x & 1) == 0) row[x >> 1] = (u8) (v << 4); else row[x >> 1] |= v;
            }
        }
    }
}

/* Groups: one picture the ROM stores as several textures. The folder's Bond photo
 * is four 65x65 I4 tiles (2602 top-left, 2603 top-right, 2604 bottom-left, 2605
 * bottom-right) sharing a one-texel border, so one bondph.bmp is split across
 * them; a per-number t02602.bmp still overrides a single tile. */
struct portTexGroup {
    const char *name;
    s32 cols, rows, tile, overlap;
    s32 nums[4];
};
static const struct portTexGroup sGroups[] = {
    { "bondph", 2, 2, 65, 1, { 2602, 2603, 2604, 2605 } },
};
#define PORT_TEX_GROUPS ((s32) (sizeof(sGroups) / sizeof(sGroups[0])))
static u8 sGroupMiss[PORT_TEXCTX_COUNT][PORT_TEX_GROUPS];

/** The tile of a group picture in `dir` that texture `num` is; 0 = not in a group or no file. */
static s32 texGroupTile(s32 ctx, s32 num, s32 *w, s32 *h, u8 **rgba)
{
    s32 g, k;

    for (g = 0; g < PORT_TEX_GROUPS; g++)
    {
        const struct portTexGroup *gr = &sGroups[g];

        for (k = 0; k < gr->cols * gr->rows; k++)
        {
            u8 *whole = NULL;
            s32 ww = 0, wh = 0, col, row, x0, y0, x1, y1, tw, th, y;
            u8 *tile;

            if (gr->nums[k] != num || sGroupMiss[ctx][g])
            {
                continue;
            }
            if (!texReadBmpFile(sCtxDir[ctx], gr->name, 0, &ww, &wh, &whole))
            {
                sGroupMiss[ctx][g] = 1;
                return 0;
            }
            /* the grid is cols*tile - (cols-1)*overlap texels across; map
             * the tile's texel span onto the picture */
            col = k % gr->cols;
            row = k / gr->cols;
            {
                const s32 gw = gr->cols * gr->tile - (gr->cols - 1) * gr->overlap;
                const s32 gh = gr->rows * gr->tile - (gr->rows - 1) * gr->overlap;
                x0 = col * (gr->tile - gr->overlap) * ww / gw;
                x1 = (col * (gr->tile - gr->overlap) + gr->tile) * ww / gw;
                y0 = row * (gr->tile - gr->overlap) * wh / gh;
                y1 = (row * (gr->tile - gr->overlap) + gr->tile) * wh / gh;
            }
            if (x1 > ww) x1 = ww;
            if (y1 > wh) y1 = wh;
            tw = x1 - x0;
            th = y1 - y0;
            if (tw <= 0 || th <= 0)
            {
                free(whole);
                return 0;
            }
            tile = (u8 *) malloc((size_t) tw * th * 4);
            if (tile == NULL)
            {
                free(whole);
                return 0;
            }
            for (y = 0; y < th; y++)
            {
                memcpy(tile + (size_t) y * tw * 4, whole + ((size_t) (y0 + y) * ww + x0) * 4, (size_t) tw * 4);
            }
            free(whole);
            *w = tw;
            *h = th;
            *rgba = tile;
            return 1;
        }
    }
    return 0;
}

/**
 * The picture for texture `num` in context `ctx` (t<num>.bmp, else its group's
 * picture) as top-down RGBA8 already flipped for the pool, whose row 0 is the
 * bottom of the picture as drawn. 0 = no file.
 */
static s32 texRomPicture(s32 ctx, s32 num, s32 *bw, s32 *bh, u8 **rgba)
{
    char name[16];
    u8 *tmp;
    s32 y;

    snprintf(name, sizeof(name), "t%05d", num);
    if (!texReadBmpFile(sCtxDir[ctx], name, 0, bw, bh, rgba)
        && !texGroupTile(ctx, num, bw, bh, rgba))
    {
        return 0;
    }
    tmp = (u8 *) malloc((size_t) *bw * 4);
    if (tmp != NULL)
    {
        for (y = 0; y < *bh / 2; y++)
        {
            u8 *a = *rgba + (size_t) y * *bw * 4;
            u8 *b = *rgba + (size_t) (*bh - 1 - y) * *bw * 4;
            memcpy(tmp, a, (size_t) *bw * 4);
            memcpy(a, b, (size_t) *bw * 4);
            memcpy(b, tmp, (size_t) *bw * 4);
        }
        free(tmp);
    }
    return 1;
}

/**
 * Encode a picture into `blob`, which holds a copy of the texture's ROM pool
 * bytes (`bytes` long) and keeps its palette. 0 = the pool layout this file
 * derives does not match what the inflater wrote, blob untouched.
 */
static s32 texRomEncode(const struct tex *tex, u8 *blob, s32 bytes, const u8 *rgba, s32 bw, s32 bh)
{
    extern s32 portTexLodLookup(s32 texturenum, s32 lod, s32 wantheight); /* image.c */
    const s32 num = tex->texturenum;
    const s32 gbifmt = tex->gbiformat;
    const s32 depth = tex->depth;
    const s32 isci = (gbifmt == G_IM_FMT_CI);
    const s32 isia = (tex->lutmodeindex == (G_TT_IA16 >> G_MDSFT_TEXTLUT));
    const s32 ncol = isci ? (s32) tex->unk0a + 1 : 0;
    /* Levels: an explicit-lod texture has its own level sizes (recorded by the
     * inflater), while any other texture with maxlod > 0 gets a chain where
     * each level is (w+1)/2 x (h+1)/2 of the last. */
    const s32 nlevels = (tex->maxlod > 0) ? (s32) tex->maxlod : 1;
    s32 lw[8], lh[8], lofs[8];
    s32 total = 0, i;
    u16 pal[256];

    lw[0] = tex->width;
    lh[0] = tex->height;
    for (i = 1; i < nlevels; i++)
    {
        if (tex->hasExplicitLods)
        {
            lw[i] = portTexLodLookup(num, i, 0);
            lh[i] = portTexLodLookup(num, i, 1);
        }
        else
        {
            lw[i] = (lw[i - 1] + 1) >> 1;
            lh[i] = (lh[i - 1] + 1) >> 1;
        }
        if (lw[i] <= 0 || lh[i] <= 0)
        {
            portConLogf("ge: textures: t%05d: level %d size unknown, not replaced\n", num, i);
            return 0;
        }
    }
    for (i = 0; i < nlevels; i++)
    {
        lofs[i] = total;
        total += texRowStride(gbifmt, depth, lw[i]) * lh[i];
    }
    if (((total + ncol * 2 + 7) & ~7) != bytes)
    {
        portConLogf("ge: textures: t%05d: pool layout %d bytes but inflater wrote %d (fmt %d depth %d %dx%d lods %d ncol %d) - not replaced\n",
                    num, (total + ncol * 2 + 7) & ~7, bytes, gbifmt, depth, lw[0], lh[0], nlevels, ncol);
        return 0;
    }
    if (isci)
    {
        memcpy(pal, blob + total, (size_t) ncol * 2);   /* native u16s, image.c writes them so */
    }
    for (i = 0; i < nlevels; i++)
    {
        u8 *lv = (u8 *) malloc((size_t) lw[i] * lh[i] * 4);

        if (lv == NULL)
        {
            return 0;
        }
        texResample(rgba, bw, bh, lv, lw[i], lh[i]);
        memset(blob + lofs[i], 0, (size_t) texRowStride(gbifmt, depth, lw[i]) * lh[i]);
        texWriteLevel(blob + lofs[i], gbifmt, depth, lw[i], lh[i], lv, pal, ncol, isia);
        free(lv);
    }
    portConLogf("ge: textures: t%05d.bmp %dx%d -> ROM texture %d (%dx%d, fmt %d depth %d, %d level%s%s)\n",
                num, bw, bh, num, lw[0], lh[0], gbifmt, depth, nlevels, nlevels == 1 ? "" : "s",
                isci ? ", palette kept" : "");
    return 1;
}

/** The bytes texture `r` should show in context `ctx`: that context's, else Base's, else the ROM's. */
static const u8 *texRomBytesFor(const struct portRomTex *r, s32 ctx)
{
    if (ctx == PORT_TEXCTX_OPTIONS && r->enc[PORT_TEXCTX_OPTIONS] != NULL)
    {
        return r->enc[PORT_TEXCTX_OPTIONS];
    }
    return (r->enc[PORT_TEXCTX_BASE] != NULL) ? r->enc[PORT_TEXCTX_BASE] : r->orig;
}

/**
 * texLoad (image.c): `tex` was just inflated into the pool at `data`, `bytes`
 * long. If Base/ or Options/ has a file for it, register it and write the
 * current context's bytes; returns 1 when the pool was rewritten.
 */
s32 portTexReplaceRom(struct tex *tex, u8 *data, s32 bytes)
{
    const s32 num = tex->texturenum;
    struct portRomTex *r = NULL;
    s32 i, c, any = 0;

    if (num < 0 || num >= PORT_TEXTURES_MAX || data == NULL || bytes <= 0)
    {
        return 0;
    }
    if (sRomMiss[num >> 3] & (1u << (num & 7)))
    {
        return 0;
    }
    for (i = 0; i < sRomCount; i++)
    {
        if (sRom[i].num == num)
        {
            r = &sRom[i];   /* loaded again after a pool reset: refresh it */
            break;
        }
    }
    if (r == NULL)
    {
        if (sRomCount >= sRomCap)
        {   /* grow: entries are never dropped, so a fixed table would fill
             * up after a few stages and later files would silently do nothing */
            const s32 cap = (sRomCap > 0) ? sRomCap * 2 : PORT_TEX_ROM_MAX;
            struct portRomTex *n = (struct portRomTex *) realloc(sRom, (size_t) cap * sizeof(*sRom));

            if (n == NULL)
            {
                static s32 said;

                if (!said)
                {
                    said = 1;
                    portConLogf("ge: textures: registry full (%d), t%05d.bmp not applied\n", sRomCount, num);
                }
                return 0;
            }
            sRom = n;
            sRomCap = cap;
        }
        r = &sRom[sRomCount];
        memset(r, 0, sizeof(*r));
        r->num = num;
    }
    free(r->orig);
    r->orig = NULL;
    for (c = 0; c < PORT_TEXCTX_COUNT; c++)
    {
        u8 *rgba = NULL;
        s32 bw = 0, bh = 0;

        free(r->enc[c]);
        r->enc[c] = NULL;
        if (!texRomPicture(c, num, &bw, &bh, &rgba))
        {
            continue;
        }
        r->enc[c] = (u8 *) malloc((size_t) bytes);
        if (r->enc[c] != NULL)
        {
            memcpy(r->enc[c], data, (size_t) bytes);   /* the ROM's bytes: the palette comes from here */
            if (texRomEncode(tex, r->enc[c], bytes, rgba, bw, bh))
            {
                any = 1;
            }
            else
            {
                free(r->enc[c]);
                r->enc[c] = NULL;
            }
        }
        free(rgba);
    }
    if (!any)
    {
        sRomMiss[num >> 3] |= (u8) (1u << (num & 7));
        return 0;
    }
    r->orig = (u8 *) malloc((size_t) bytes);
    if (r->orig == NULL)
    {
        return 0;
    }
    memcpy(r->orig, data, (size_t) bytes);
    r->bytes = bytes;
    if (r == &sRom[sRomCount])
    {
        sRomCount++;
    }
    memcpy(data, texRomBytesFor(r, sCtx), (size_t) bytes);
    return 1;
}

/**
 * front.c: the menu changed (`options` = the Options folder or a sheet is up).
 * Rewrites every live registered texture from the right copy, then clears the
 * renderer's address-keyed texture cache once so it drops the old pixels.
 */
void portTexContextSet(s32 options)
{
    extern struct tex *texFindInPool(s32 texturenum, struct texpool *pool); /* image.c */
    extern struct texpool *portModelTexPool(void);
    extern void gfx_texture_cache_clear_c(void);
    const s32 ctx = options ? PORT_TEXCTX_OPTIONS : PORT_TEXCTX_BASE;
    s32 i, swapped = 0;

    if (ctx == sCtx)
    {
        return;
    }
    sCtx = ctx;
    for (i = 0; i < sRomCount; i++)
    {
        const struct portRomTex *r = &sRom[i];
        /* Every live copy, not just the first found: the wallet draws from the
         * model pool, but MP character select leaves a copy in the global pool */
        struct texpool *pools[2];
        s32 p, here = 0;

        pools[0] = NULL;
        pools[1] = portModelTexPool();
        for (p = 0; p < 2; p++)
        {
            struct tex *tex = texFindInPool(r->num, pools[p]);

            if (tex == NULL || tex->data == NULL || r->orig == NULL)
            {
                continue;   /* not loaded right now; the next load writes the right one */
            }
            memcpy(tex->data, texRomBytesFor(r, ctx), (size_t) r->bytes);
            swapped++;
            here = 1;
        }
        if (here && crosshairimage != NULL && r->num == texCrosshairNum())
        {   /* the crosshair's own BMP was swapped in untinted: those bytes
             * are the new original, and they want crosshair_color again.
             * ONLY then - any other swap leaves the pool's crosshair tinted,
             * and taking THAT as the original would tint a tint. */
            portTexCrosshairRefresh(1);
        }
    }
    if (swapped > 0)
    {
        gfx_texture_cache_clear_c();
        portConLogf("ge: textures: %s context - %d texture%s swapped\n",
                    options ? "options" : "base", swapped, swapped == 1 ? "" : "s");
    }
}

/* ---- crosshair colour --------------------------------------------------- */

/* Crosshair colour that keeps the sprite's shading.
 *
 * The sight (crosshairimage, 32x32 RGBA32) is red plus a white core: every
 * texel is (p + w, w, w), with p the red part and w the white part. No N64
 * combiner can move p into green or blue, so the texels themselves are
 * recoloured once in the pool as out = colour * p + w per channel (the same
 * split as gunfire.c portDrawTargetPlus), and everything draws the sprite the
 * vanilla way. FF0000 gives back the ROM's own bytes. For byte order see
 * texCrosshairWrite.
 *
 * sXhairOrig is what the pool held before tinting (ROM texels or an external
 * t<num>.bmp), so a second colour never compounds on the first. */
extern struct sImageTableEntry *crosshairimage; /* image_bank.c */
extern s32 portCrosshairColor(void);            /* video.c; weak -1 headless */

static u8 *sXhairOrig;
static s32 sXhairBytes;
static s32 sXhairTinted;   /* the pool's crosshair texels carry a colour right now */

/* Every pool copy the loader tinted is remembered, since texLoad tints the sight
 * in whichever pool loads it (the frontend's included); a colour change
 * rewrites all of them, after checking each entry is still the sight at that
 * address, since pools are reused between stages. */
#define XHAIR_COPIES 4
static struct tex *sXhairTex[XHAIR_COPIES];
static u8 *sXhairData[XHAIR_COPIES];

static s32 texCrosshairCopyLive(s32 k)
{
    return sXhairTex[k] != NULL && crosshairimage != NULL
        && sXhairTex[k]->texturenum == texCrosshairNum()
        && (u8 *) sXhairTex[k]->data == sXhairData[k];
}

static void texCrosshairRemember(struct tex *tex, u8 *data)
{
    s32 k;
    s32 slot = -1;

    for (k = 0; k < XHAIR_COPIES; k++)
    {
        if (sXhairTex[k] == tex || sXhairData[k] == data || !texCrosshairCopyLive(k))
        {   /* the same copy again, or a stale one: take its place */
            slot = k;
            break;
        }
    }
    if (slot < 0)
    {
        slot = 0;   /* four live copies is more than the game makes; recycle */
    }
    sXhairTex[slot] = tex;
    sXhairData[slot] = data;
}

/* When a pool or bank is reset (texInitPool, mempResetBank), drop every remembered
 * copy whose entry or pixels lie in that range: the stale entry can still look
 * valid, and rewriting it would corrupt whatever level texture now lives there.
 * The persistent model pool is never reset, so its copy is kept. */
void portTexCrosshairForget(const void *lo, const void *hi)
{
    const u8 *a = (const u8 *) lo;
    const u8 *b = (const u8 *) hi;
    s32 k;

    for (k = 0; k < XHAIR_COPIES; k++)
    {
        const u8 *t = (const u8 *) sXhairTex[k];
        const u8 *d = sXhairData[k];

        if ((t != NULL && t >= a && t < b) || (d != NULL && d >= a && d < b))
        {
            sXhairTex[k] = NULL;
            sXhairData[k] = NULL;
        }
    }
}

/* Per-player crosshair colours: a player with their own colour draws from a copy
 * of the texels tinted for it (gunfire.c repoints the sight's texture image
 * there). One copy per colour, made on first use and only re-tinted on a
 * texture-pack refresh, so a copy never goes stale. */
#define XHAIR_TINTS 16
static struct { s32 rgb; u8 *buf; } sXhairTints[XHAIR_TINTS];
static s32 sXhairTintCount;

static void texCrosshairTintTo(u8 *data, s32 c);

static void texCrosshairWrite(u8 *data)
{
    const s32 c = portCrosshairColor();

    sXhairTinted = (c >= 0);
    texCrosshairTintTo(data, c);
}

/** A copy of the crosshair's texels tinted `rgb` (-1 = the ROM's own), or NULL. */
const void *portTexCrosshairCopyFor(s32 rgb)
{
    s32 k;
    u8 *buf;

    if (sXhairOrig == NULL || sXhairBytes <= 0)
    {
        return NULL;
    }
    for (k = 0; k < sXhairTintCount; k++)
    {
        if (sXhairTints[k].rgb == rgb)
        {
            return sXhairTints[k].buf;
        }
    }
    if (sXhairTintCount >= XHAIR_TINTS)
    {
        return NULL;
    }
    {   /* From the high arena, not the CRT heap: this address goes into the
         * display list, and below 0x10000000 fast3d's seg_addr reads it as a
         * segment reference and draws the wrong bytes. Never freed. */
        extern void *portAllocHigh(u32 size); /* pcsched.c */

        buf = (u8 *) portAllocHigh((u32) sXhairBytes);
    }
    if (buf == NULL)
    {
        return NULL;
    }
    texCrosshairTintTo(buf, rgb);
    sXhairTints[sXhairTintCount].rgb = rgb;
    sXhairTints[sXhairTintCount].buf = buf;
    sXhairTintCount++;
    return buf;
}

static void texCrosshairTintTo(u8 *data, s32 c)
{
    const s32 cr = (c >> 16) & 0xFF, cg = (c >> 8) & 0xFF, cb = c & 0xFF;
    s32 i;

    memcpy(data, sXhairOrig, (size_t) sXhairBytes);
    /* Byte order: the pool holds RGBA32 as native u32 words with R in the top
     * byte, not as R,G,B,A bytes (indexing bytes on little-endian writes over alpha). */
    for (i = 0; c >= 0 && i + 3 < sXhairBytes; i += 4)
    {
        u32 *px = (u32 *) (data + i);
        const s32 tr = (s32) ((*px >> 24) & 0xFF), tg = (s32) ((*px >> 16) & 0xFF), tb = (s32) ((*px >> 8) & 0xFF);
        /* Split by chroma, not by red: the ROM's (p + w, w, w) texels give the
         * same p and w, and a replacement BMP of any hue also takes the colour */
        const s32 mx = (tr > tg) ? ((tr > tb) ? tr : tb) : ((tg > tb) ? tg : tb);
        const s32 w = (tr < tg) ? ((tr < tb) ? tr : tb) : ((tg < tb) ? tg : tb);
        const s32 p = mx - w;
        s32 r = (cr * p) / 255 + w, g = (cg * p) / 255 + w, b = (cb * p) / 255 + w;

        if (r > 255) { r = 255; }
        if (g > 255) { g = 255; }
        if (b > 255) { b = 255; }
        *px = ((u32) r << 24) | ((u32) g << 16) | ((u32) b << 8) | (*px & 0xFFu);
    }
}

/** texLoad (image.c), after portTexReplaceRom: the crosshair just landed in a pool. */
void portTexCrosshairLoaded(struct tex *tex, u8 *data, s32 bytes)
{
    if (crosshairimage == NULL || tex->texturenum != texCrosshairNum()
        || data == NULL || bytes < 4)
    {
        return;
    }
    if (bytes != sXhairBytes)
    {
        free(sXhairOrig);
        sXhairOrig = (u8 *) malloc((size_t) bytes);
        sXhairBytes = (sXhairOrig != NULL) ? bytes : 0;
        sXhairTintCount = 0;   /* the copies were another size: start over (never freed - an address must not come back with other pixels) */
    }
    if (sXhairOrig == NULL)
    {
        return;
    }
    memcpy(sXhairOrig, data, (size_t) bytes);
    if (bytes != (s32) crosshairimage->width * (s32) crosshairimage->height * 4)
    {   /* not one RGBA32 level after all: leave it alone, the flat draw still colours it */
        static s32 said;

        if (!said)
        {
            said = 1;
            portConLogf("ge: textures: crosshair is %d bytes, not %dx%d RGBA32 - crosshair_color falls back to the flat tint\n",
                        (int) bytes, (int) crosshairimage->width, (int) crosshairimage->height);
        }
        free(sXhairOrig);
        sXhairOrig = NULL;
        sXhairBytes = 0;
        sXhairTinted = 0;
        return;
    }
    texCrosshairWrite(data);
    texCrosshairRemember(tex, data);
    if (getenv("GE_XHAIRDBG") != NULL)
    {
        portConLogf("ge: xhairdbg: LOADED tex=%p data=%p bytes=%d orig0=%08X colour=%d\n",
                    (void *) tex, (void *) data, (int) bytes, (unsigned) *(u32 *) sXhairOrig,
                    (int) portCrosshairColor());
    }
}

/** 1 = the texels carry crosshair_color, so the sprite is drawn the vanilla way (bondwalk2.c). */
s32 portTexCrosshairTinted(void)
{
    return sXhairTinted;
}

/**
 * The colour changed (video.c), or a context swap rewrote the pool under us
 * (`reorig` = take the pool's bytes as the new untinted original first).
 * No-op until the crosshair has been loaded once.
 */
void portTexCrosshairRefresh(s32 reorig)
{
    extern struct tex *texFindInPool(s32 texturenum, struct texpool *pool); /* image.c */
    extern void gfx_texture_cache_clear_c(void);
    struct tex *tex;
    s32 k;
    s32 any = 0;

    if (sXhairOrig == NULL || crosshairimage == NULL)
    {
        return;
    }
    /* the two pools a context swap works on, as before - and whatever they
     * hold joins the remembered copies */
    tex = texFindInPool(texCrosshairNum(), NULL);
    if (tex == NULL)
    {
        extern struct texpool *portModelTexPool(void);

        tex = texFindInPool(texCrosshairNum(), portModelTexPool());
    }
    if (getenv("GE_XHAIRDBG") != NULL)
    {
        portConLogf("ge: xhairdbg: REFRESH reorig=%d pooltex=%p pooldata=%p copies=%p/%p %p/%p live=%d%d orig0=%08X colour=%d\n",
                    (int) reorig, (void *) tex, (tex != NULL) ? (void *) tex->data : NULL,
                    (void *) sXhairTex[0], (void *) sXhairData[0], (void *) sXhairTex[1], (void *) sXhairData[1],
                    (int) texCrosshairCopyLive(0), (int) texCrosshairCopyLive(1),
                    (unsigned) *(u32 *) sXhairOrig, (int) portCrosshairColor());
        if (sXhairTex[0] != NULL)
        {
            portConLogf("ge: xhairdbg: copy0 now num=%d data=%p (want num=%d)\n",
                        (int) sXhairTex[0]->texturenum, (void *) sXhairTex[0]->data, (int) texCrosshairNum());
        }
    }
    if (tex != NULL && tex->data != NULL)
    {
        if (reorig)
        {
            memcpy(sXhairOrig, tex->data, (size_t) sXhairBytes);
            for (k = 0; k < sXhairTintCount; k++)
            {   /* a new original: the per-player copies follow (the cache clear below covers them) */
                texCrosshairTintTo(sXhairTints[k].buf, sXhairTints[k].rgb);
            }
        }
        texCrosshairRemember(tex, (u8 *) tex->data);
    }
    /* ...then EVERY live copy, the frontend's included */
    for (k = 0; k < XHAIR_COPIES; k++)
    {
        if (texCrosshairCopyLive(k))
        {
            texCrosshairWrite(sXhairData[k]);
            any = 1;
        }
        else
        {
            sXhairTex[k] = NULL;
            sXhairData[k] = NULL;
        }
    }
    if (!any)
    {
        sXhairTinted = 0;   /* not in any pool right now; the next load tints it */
        return;
    }
    gfx_texture_cache_clear_c();   /* keyed by address: it would keep the old pixels */
}
