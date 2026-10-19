#include <ultra64.h>
#include "bondconstants.h"
#include "image.h"
#include "image_bank.h"
#include <assets/image_externs.h>
#include <PR/R4300.h>
#include "ramrom.h"
#include "decompress.h"
#ifndef TARGET_N64
#include "frametiming.h" /* tickhz=hw texture decode price */
#endif


#define TEX_ALPHA_WEIGHT 961

/* PORT (texel byte order): texel and palette values stored to or read from u16/u32 texel
 * buffers go through the TEX16/TEX32 wrappers, so byte order can be flipped in
 * one place. Plain u16/u32 copies and the intermediate index buffers from
 * texInflateHuffman/Rle stay bare. */
/* The PC port uses native byte order for texels everywhere, because the compiled-in asset
 * words are native-valued. TEX16/TEX32 are therefore identity on both targets
 * and only mark where a flip would be needed if that ever changes. */
#define TEX16(v) ((u16)(v))
#define TEX32(v) ((u32)(v))

// bss
#ifdef TARGET_N64
//8008C720
struct texpool *ptr_texture_alloc_start;
//8008C724
s32 ptr_texture_alloc_end;
//8008C728
s32 ptr_next_available_space;
//8008C72C
s32 ptr_last_entry_facemapping;
#else
/* On N64 the four loose globals above are laid out back to back and are cast to struct texpool*
 * (texLoad, texLoadFromGdl, texInitPool); GCC does not keep them adjacent, so
 * PC uses a real struct. image.h aliases the legacy name onto it. */
struct texpool g_PortGlobalTexPool;
#endif
//8008C730
struct texcacheitem g_TexCacheItems[150];

#ifndef TARGET_N64
/* PORT: g_TexCacheItems is reset every stage, but textures in the persistent portModelTexPool
 * survive across stages and are then skipped by texLoad, so their per-LOD sizes
 * were missing and texGetWidthAtLod/texGetHeightAtLod returned 1 (corrupted
 * textures). This permanent table keyed by global texture number replaces only
 * that "return 1" fallback; the per-stage cache still answers first. */
#define PORT_TEXLOD_MAX 512
static struct texcacheitem s_TexLodPersist[PORT_TEXLOD_MAX];
static s32 s_TexLodPersistCount;

void portTexLodRemember(struct texcacheitem *item)
{
    s32 i;

    for (i = 0; i < s_TexLodPersistCount; i++)
    {
        if (s_TexLodPersist[i].texturenum == item->texturenum)
        {
            return;     /* dimensions are global - first answer stands */
        }
    }

    if (s_TexLodPersistCount < PORT_TEXLOD_MAX)
    {
        s_TexLodPersist[s_TexLodPersistCount++] = *item;
    }
}

/* Returns 0 when unknown, so the caller keeps its historic fallback. */
s32 portTexLodLookup(s32 texturenum, s32 lod, s32 wantheight)
{
    s32 i;

    if (lod < 1 || lod > 7)
    {
        return 0;
    }

    for (i = 0; i < s_TexLodPersistCount; i++)
    {
        if (s_TexLodPersist[i].texturenum == (s16) texturenum)
        {
            return wantheight ? (s32) s_TexLodPersist[i].heights[lod - 1]
                              : (s32) s_TexLodPersist[i].widths[lod - 1];
        }
    }

    return 0;
}
#endif
//8008D090
s32 g_TexCacheCount;
//8008D094
s32 g_TexNumToLoad;

// data
//D:80049170
u32 bytes = 0x6DDD0;
//D:80049174
u32 D_80049174 = 0;

//D:80049178 #1	#bytes in pixel data for image
s32 g_TexFormatNumChannels[] = 
{
    4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1
};
//D:800491AC #2	1=alphagrab.  Grabs 1 bit of alpha data for each pixel
s32 g_TexFormatHas1BitAlpha[] = 
{
    0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0
};
//D:800491E0 #3	#bits in 'samples', *2	-1=bitmask
s32 g_TexFormatChannelSizes[] = 
{
    0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100, 0x10, 0x100, 0x10, 0x100, 0x10
};
//D:80049214 #4	bitcount for pixel data
s32 g_TexFormatBitsPerPixel[] = 
{
     0x20, 0x10, 0x18, 0xF, 0x10, 8, 4, 8, 4, 0x10, 0x10, 0x10, 0x10, 
};
//D:80049248 #5	N64 image types (0=color, 1=YUV, 2=indexed, 3=IA, 4=I)
s32 g_TexFormatGbiMappings[] = 
{
    G_IM_FMT_RGBA, G_IM_FMT_RGBA, G_IM_FMT_RGBA, G_IM_FMT_RGBA,	
    G_IM_FMT_IA, G_IM_FMT_IA, G_IM_FMT_IA, 
    G_IM_FMT_I, G_IM_FMT_I, 
    G_IM_FMT_CI, G_IM_FMT_CI, G_IM_FMT_CI, G_IM_FMT_CI,
};
//D:8004927C #6	N64 pixel sizes (0=4bit, 1=8bit, 2=16bit, 3=32bit)
s32 g_TexFormatDepths[] = 
{
	G_IM_SIZ_32b,
    G_IM_SIZ_16b,
	G_IM_SIZ_32b,
	G_IM_SIZ_16b,
	G_IM_SIZ_16b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
};
//D:800492B0 #7	imageflip values for indexed types
s32 g_TexFormatLutModes[] = {
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_RGBA16,
	G_TT_RGBA16,
	G_TT_IA16,
	G_TT_IA16,
};
//D:800492e4
s32 D_800492E4[] = 
{
    0, 0, 0, 0, 0, 0, 0
};
#define IMAGE(NAME, SZ, HS, HT, F3, F4, F5, F6) \
    {HS, HT, SZ, F3, F4, F5, F6 },

//D:80049300
//need way to calculate size at compile time from external data
#ifdef TARGET_N64
struct image_entry g_Textures[] = {
#else
/* The user's ROM carries this table (romdataImageTableApply in
 * port/src/romdata.c) and it is copied over the compiled-in contents, so the
 * array has extra headroom before image_entries_load accumulates the sizes. */
struct image_entry g_Textures[PORT_TEXTURES_MAX] = {
#endif
    #include <assets/images.def>
    {HIT_DEFAULT, HIT_DEFAULT,0xFFFF,0,0,0,0}
};
#undef IMAGE


void nullsub_41(s32 arg0) {
    if (arg0);
}


/**
 * Inflate images (levels of detail) from a zlib-compressed texture.
 *
 * Zlib-compressed textures are always paletted and always use 16-bit colours.
 * The texture header contains palette information, then each image follows with
 * its own header and zlib compresed data.
 *
 * The texture header is:
 *
 * ffffffff nnnnnnnn [palette]
 *
 * f = pixel format (see TEXFORMAT constants) n = number of colours in the
 * palette minus 1 [palette] = 16 bits * number of colours
 *
 * Each images's header is:
 *
 * wwwwwwww hhhhhhhh [data]
 *
 * w = width in pixels h = height in pixels [data] = zlib compressed list of
 * indices into the palette
 *
 * The zlib data is prefixed with the standard 5-byte rarezip header.
 */
s32 texInflateZlib(u8 *src, u8 *dst, s32 arg2, s32 forcenumimages, struct texpool *arg4)
{
    s32 i;
    s32 imagebytesout;
    s32 numimages;
    s32 totalbytesout;
    s32 format;
    bool foundthething;
    bool writetocache;
    s32 width;
    s32 height;
    u8 *end;
    u8 *start;
    s32 numcolours;
    s32 j;
    s32 unused;
    u8 scratch2[0x800];
    u8 scratch[0x2100];
    u16 palette[0x100];

    totalbytesout = 0;
    writetocache = FALSE;

    texSetBitstring(src);

    if (arg2 && forcenumimages)
    {
        numimages = forcenumimages;
    }
    else
    {
        numimages = 1;
    }

    arg4->rightpos->maxlod = forcenumimages;
    arg4->rightpos->hasExplicitLods = arg2;

    if (arg2)
    {
        writetocache = TRUE;

        for (i = 0; i < g_TexCacheCount; i++)
        {
            if (g_TexCacheItems[i].texturenum == arg4->rightpos->texturenum)
            {
                writetocache = FALSE;
            }
        }
    }

    format = texReadBits(8);
    numcolours = texReadBits(8) + 1;

    for (i = 0; i < numcolours; i++)
    {
        palette[i] = texReadBits(16);
    }

    foundthething = FALSE;

    for (j = 0; j < numimages; j++)
    {
        width = texReadBits(8);
        height = texReadBits(8);

        if (j == 0)
        {
#ifndef TARGET_N64
            portHwTexInfo(format, width, height, 0xff, numcolours, numimages); /* tickhz=hw decode price */
#endif
            arg4->rightpos->width = width;
            arg4->rightpos->height = height;
            arg4->rightpos->unk0a = numcolours - 1;
            arg4->rightpos->gbiformat = g_TexFormatGbiMappings[format];
            arg4->rightpos->depth = g_TexFormatDepths[format];
            arg4->rightpos->lutmodeindex = g_TexFormatLutModes[format] >> G_MDSFT_TEXTLUT;
        }
        else if (writetocache)
        {
            g_TexCacheItems[g_TexCacheCount].widths[j - 1] = width;
            g_TexCacheItems[g_TexCacheCount].heights[j - 1] = height;
        }

        if ((width * height) >= 4097)
        {
            return j * 0;
        }

        decompressdata(img_curpos, &scratch2, (struct huft *)&scratch);
        imagebytesout = texAlignIndices(scratch2, width, height, format, &dst[totalbytesout]);
        texSetBitstring(rzipGetSomething());

        if ((arg2 == 1) && (forcenumimages > 0))
        {
            texSwapAltRowBytes(&dst[totalbytesout], width, height, format);
        }
        totalbytesout += imagebytesout;
    }

    if (writetocache)
    {
        g_TexCacheItems[g_TexCacheCount].texturenum = arg4->rightpos->texturenum;
#ifndef TARGET_N64
        portTexLodRemember(&g_TexCacheItems[g_TexCacheCount]);
#endif

        g_TexCacheCount++;

        if (g_TexCacheCount >= ARRAYCOUNT(g_TexCacheItems))
        {
            g_TexCacheCount = 0;
        }
    }

    if (!arg2)
    {
        if (forcenumimages >= 2)
        {
            s32 tmpwidth;
            s32 tmpheight;

            tmpwidth = width;
            tmpheight = height;

            start = dst;
            end = &dst[totalbytesout];

            for (j = 1; j < forcenumimages; j++)
            {
                imagebytesout = texShrinkPaletted(start, end, tmpwidth, tmpheight, format, palette, numcolours);

                if (totalbytesout + imagebytesout > 0x800)
                {
                    arg4->rightpos->maxlod = j;
                    break;
                }

                texSwapAltRowBytes(start, tmpwidth, tmpheight, format);

                totalbytesout += imagebytesout;

                tmpwidth = (tmpwidth + 1) >> 1;
                tmpheight = (tmpheight + 1) >> 1;

                start = end;
                end += imagebytesout;
                if (1);
            }

            texSwapAltRowBytes(start, tmpwidth, tmpheight, format);
        }
        else if (forcenumimages == 1)
        {
            texSwapAltRowBytes(dst, width, height, format);
        }
    }

    for (i = 0; i < numcolours; i++)
    {
        if ((!totalbytesout) && (!totalbytesout));
#ifdef TARGET_N64
        dst[totalbytesout + 0] = palette[i] >> 8;
        dst[totalbytesout + 1] = palette[i] & 0xff;
#else
        /* native texel convention (see TEX16 note): the TLUT reader
         * consumes native u16s, so store the palette natively too */
        dst[totalbytesout + 0] = palette[i] & 0xff;
        dst[totalbytesout + 1] = palette[i] >> 8;
#endif
        totalbytesout += 2;
    }

    totalbytesout = (totalbytesout + 7) & ~7;

    return totalbytesout;
}


/**
 * Copy a list of palette indices to the dst buffer, but ensure each row is
 * aligned to an 8 byte boundary.
 *
 * Return the number of output bytes.
 */
s32 texAlignIndices(u8 *src, s32 width, s32 height, s32 format, u8 *dst)
{
    s32 x;
    s32 y;
    u8 *outptr;
    s32 indicesperbyte;

    outptr = dst;

    if (format == TEXFORMAT_RGBA16_CI8 || format == TEXFORMAT_IA16_CI8)
    {
        indicesperbyte = 1;
    }
    else if (format == TEXFORMAT_RGBA16_CI4 || format == TEXFORMAT_IA16_CI4)
    {
        indicesperbyte = 2;
    }
    else if (indicesperbyte)
    {
    }

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x += indicesperbyte)
        {
            *outptr = *src;
            outptr++;
            src++;
        }

        outptr = (u8 *)(((uintptr_t)outptr + 7) & ~7);
    }

    return outptr - dst;
}


/**
 * Shrink a paletted texture to half its size by averaging each each 2x2 group
 * of pixels.
 *
 * Return the number of bytes written.
 */
s32 texShrinkPaletted(u8 *src, u8 *dst, s32 srcwidth, s32 srcheight, s32 format, u16 *palette, s32 numcolours)
{
    s32 j;
    s32 i;
    s32 alignedsrcwidth;
    s32 aligneddstwidth;
    s32 dstheight;
    s16 colour1;
    s16 colour2;
    s16 colour3;
    s16 colour4;
    s32 r;
    s32 g;
    s32 b;
    s32 a;
    s32 nextrow;
    u8 *dst8;
    s32 nextcol;
    s32 c;
    u8 *src8;

    dst8 = dst;
    src8 = src;
    dstheight = (srcheight + 1) >> 1;

    switch (format)
    {
        case TEXFORMAT_RGBA16_CI8:
        case TEXFORMAT_IA16_CI8:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 7) & 0xff8;
            alignedsrcwidth = (srcwidth + 7) & 0xff8;
            break;

        case TEXFORMAT_RGBA16_CI4:
        case TEXFORMAT_IA16_CI4:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 15) & 0xff0;
            alignedsrcwidth = (srcwidth + 15) & 0xff0;
            break;
    }


    switch (format)
    {
        case TEXFORMAT_RGBA16_CI8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    colour1 = palette[src8[j]];
                    colour2 = palette[src8[nextcol]];
                    colour3 = palette[src8[nextrow + j]];
                    colour4 = palette[src8[nextrow + nextcol]];

                    r = ((((colour1 >> 0xB) & 0x1F) + ((colour2 >> 0xB) & 0x1F) + ((colour3 >> 0xB) & 0x1F) + ((colour4 >> 0xB) & 0x1F)) >> 2) & 0x1F;
                    g = ((((colour1 >> 6) & 0x1F) + ((colour2 >> 6) & 0x1F) + ((colour3 >> 6) & 0x1F) + ((colour4 >> 6) & 0x1F)) >> 2) & 0x1F;
                    b = ((((colour1 >> 1) & 0x1F) + ((colour2 >> 1) & 0x1F) + ((colour3 >> 1) & 0x1F) + ((colour4 >> 1) & 0x1F)) >> 2) & 0x1F;
                    a = (((colour1 & 1) + (colour2 & 1) + (colour3 & 1) + (colour4 & 1) + 2) >> 2) & 1;

                    dst8[j >> 1] = texFindClosestColourIndexRGBA(palette, numcolours, r, g, b, a);
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_IA16_CI8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    colour1 = palette[src8[j]];
                    colour2 = palette[src8[nextcol]];
                    colour3 = palette[src8[nextrow + j]];
                    colour4 = palette[src8[nextrow + nextcol]];

                    c = ((((colour1 >> 8) & 0xff) + ((colour2 >> 8) & 0xff) + ((colour3 >> 8) & 0xff) + ((colour4 >> 8) & 0xff)) >> 2) & 0xff;
                    a = ((((colour1 >> 0) & 0xff) + ((colour2 >> 0) & 0xff) + ((colour3 >> 0) & 0xff) + ((colour4 >> 0) & 0xff) + 1) >> 2) & 0xff;

                    dst8[j >> 1] = texFindClosestColourIndexIA(palette, numcolours, c, a);
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_RGBA16_CI4:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth >> 1 : 0;

                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    colour1 = palette[(src8[j >> 1] >> 4) & 0xf];
                    colour2 = palette[src8[j >> 1] >> ((j + 1 < srcwidth ? 0 : 4)) & 0xf];
                    colour3 = palette[(src8[nextrow + (j >> 1)] >> 4) & 0xf];
                    colour4 = palette[src8[nextrow + (j >> 1)] >> ((j + 1 < srcwidth ? 0 : 4)) & 0xf];

                    r = ((((colour1 >> 0xB) & 0x1F) + ((colour2 >> 0xB) & 0x1F) + ((colour3 >> 0xB) & 0x1F) + ((colour4 >> 0xB) & 0x1F)) >> 2) & 0x1F;
                    g = ((((colour1 >> 6) & 0x1F) + ((colour2 >> 6) & 0x1F) + ((colour3 >> 6) & 0x1F) + ((colour4 >> 6) & 0x1F)) >> 2) & 0x1F;
                    b = ((((colour1 >> 1) & 0x1F) + ((colour2 >> 1) & 0x1F) + ((colour3 >> 1) & 0x1F) + ((colour4 >> 1) & 0x1F)) >> 2) & 0x1F;
                    a = ((((colour1 & 1) + (colour2 & 1) + (colour3 & 1) + (colour4 & 1) + 2) >> 2) & 1);

                    dst8[j >> 2] = (texFindClosestColourIndexRGBA(palette, numcolours, r, g, b, a) * 0x10) & 0xFFFF;

                    colour1 = palette[(src8[(j + 2) >> 1] >> 4) & 0xf];
                    colour2 = palette[(src8[(j + 2) >> 1] >> (j + 3 < srcwidth ? 0 : 4)) & 0xf];
                    colour3 = palette[(src8[nextrow + ((j + 2) >> 1)] >> 4) & 0xf];
                    colour4 = palette[(src8[nextrow + ((j + 2) >> 1)] >> (j + 3 < srcwidth ? 0 : 4)) & 0xf];

                    r = ((((colour1 >> 0xB) & 0x1F) + ((colour2 >> 0xB) & 0x1F) + ((colour3 >> 0xB) & 0x1F) + ((colour4 >> 0xB) & 0x1F)) >> 2) & 0x1F;
                    g = ((((colour1 >> 6) & 0x1F) + ((colour2 >> 6) & 0x1F) + ((colour3 >> 6) & 0x1F) + ((colour4 >> 6) & 0x1F)) >> 2) & 0x1F;
                    b = ((((colour1 >> 1) & 0x1F) + ((colour2 >> 1) & 0x1F) + ((colour3 >> 1) & 0x1F) + ((colour4 >> 1) & 0x1F)) >> 2) & 0x1F;
                    a = ((((colour1 & 1) + (colour2 & 1) + (colour3 & 1) + (colour4 & 1) + 2) >> 2) & 1);

                    dst8[j >> 2] |= texFindClosestColourIndexRGBA(palette, numcolours, r, g, b, a) & 0xff;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;

        case TEXFORMAT_IA16_CI4:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth >> 1 : 0;

                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    // @bug: The brackets are wrong in colour2 and colour4, so
                    // the index shift becomes part of the ternary condition.
                    // TEXFORMAT_RGBA16_CI4 (above) does it correctly, and the
                    // buggy calculation is repeated further below.
                    colour1 = palette[(src8[j >> 1] >> 4) & 0xf];
                    colour2 = palette[(src8[j >> 1] >> (j + 1 < srcwidth) ? 0 : 4) & 0xf];
                    colour3 = palette[(src8[nextrow + (j >> 1)] >> 4) & 0xf];
                    colour4 = palette[(src8[nextrow + (j >> 1)] >> (j + 1 < srcwidth) ? 0 : 4) & 0xf];

                    c = ((((colour1 >> 8) & 0xff) + ((colour2 >> 8) & 0xff) + ((colour3 >> 8) & 0xff) + ((colour4 >> 8) & 0xff)) >> 2) & 0xff;
                    a = ((((colour1 >> 0) & 0xff) + ((colour2 >> 0) & 0xff) + ((colour3 >> 0) & 0xff) + ((colour4 >> 0) & 0xff) + 1) >> 2) & 0xff;

                    dst8[j >> 2] = (texFindClosestColourIndexIA(palette, numcolours, c, a) * 0x10) & 0xFFFF;

                    colour1 = palette[(src8[(j + 2) >> 1] >> 4) & 0xf];
                    colour2 = palette[(src8[(j + 2) >> 1] >> (j + 3 < srcwidth) ? 0 : 4) & 0xf];
                    colour3 = palette[(src8[nextrow + ((j + 2) >> 1)] >> 4) & 0xf];
                    colour4 = palette[(src8[nextrow + ((j + 2) >> 1)] >> (j + 3 < srcwidth) ? 0 : 4) & 0xf];

                    c = ((((colour1 >> 8) & 0xff) + ((colour2 >> 8) & 0xff) + ((colour3 >> 8) & 0xff) + ((colour4 >> 8) & 0xff)) >> 2) & 0xff;
                    a = ((((colour1 >> 0) & 0xff) + ((colour2 >> 0) & 0xff) + ((colour3 >> 0) & 0xff) + ((colour4 >> 0) & 0xff) + 1) >> 2) & 0xff;

                    dst8[j >> 2] |= texFindClosestColourIndexIA(palette, numcolours, c, a) & 0xff;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;
    }

    return 0;
}


/**
 * Find the palette entry closest to the target colour and return its index.
 * Used for texture LOD generation/shrinking a colour-indexed texture (see
 * texShrinkPaletted): the 2x2 box filter averages four palette entries, and the
 * average may not be a colour the palette already holds, so it has to be
 * requantized to the nearest entry. This allows the LOD textures to use
 * existing palettes instead of creating new ones.
 * 
 * The design of the algorithm is to:
 * 1) Return an exact match in the palette for the target colour if there is one.
 * 2) Failing that, a binary search uses cheap scalar ordering to find a
 * promising region in the palette.
 * 3) Perform a more meaningful RGBA comparison on only a small section of the palette.
 * 
 * Stage 2 is an approximation so this function isn't necessarily guaranteed to
 * return the closest matching palette index for a target colour.
 * 
 * Used by TEXFORMAT_RGBA16_CI8, TEXFORMAT_RGBA16_CI4
 *
 * @param palette    Palette to search, RGBA5551, assumed ordered by the stage 2 key
 * @param numcolours Number of entries in palette
 * @param r          Target red, 0..31
 * @param g          Target green, 0..31
 * @param b          Target blue, 0..31
 * @param a          Target alpha, 0..1
 * @return Index of the closest entry, or 0 if numcolours is not positive
 */
s32 texFindClosestColourIndexRGBA(u16 *palette, s32 numcolours, s32 r, s32 g, s32 b, s32 a)
{
    s32 low;
    s32 high;
    s32 i;
    u16 targetcolour;
    s32 targetmagnitude;
 
    // Cursor into the palette: the midpoint in stage 2, the scan position in stage 3.
    s32 paletteidx;
 
    u16 colour;
    s32 red;
    s32 green;
    s32 blue;
    s32 alpha;
    s32 magnitude;
 
    s32 diffr;
    s32 diffg;
    s32 diffb;
    s32 alphapenalty;
    s32 distance;
 
    s32 bestindex;
    s32 bestvalue;
 
    // Stage 1: scan the whole palette for a matching colour and return its index if one is found.
    targetcolour = ((r << 11) | (g << 6) | (b << 1) | a);
 
    for (i = 0; i < numcolours; i++)
    {
        if (targetcolour == palette[i])
        {
            return i;
        }
    }
 
    /** 
     * Stage 2: Find a promising palette neighborhood.
     */
    low = 0;
    high = numcolours - 1;
    // TEX_ALPHA_WEIGHT = 31^2, the maximum possible squared difference in one
    // five-bit color channel.
    targetmagnitude = (r * r) + (g * g) + (b * b) + (a * TEX_ALPHA_WEIGHT);
 
    while (high - low >= 2)
    {
        paletteidx = (high + low) >> 1;
 
        colour = palette[paletteidx];
        red = (colour >> 11) & 0x1F;
        green = (colour >> 6) & 0x1F;
        blue = (colour >> 1) & 0x1F;
        alpha = colour & 1;
 
        magnitude = (red * red) + (green * green) + (blue * blue) + (alpha * TEX_ALPHA_WEIGHT);
 
        if (magnitude < targetmagnitude)
        {
            low = paletteidx;
            continue;
        }
 
        if (targetmagnitude < magnitude)
        {
            high = paletteidx;
        }
        else
        {
            high = paletteidx;
            low = paletteidx;
        }
    }
 
    // Stage 3: Search the nearby palette entries accurately. 
    low = high - 4;
 
    if (low < 0)
    {
        low = 0;
    }
 
    high += 4;
 
    if (high >= numcolours)
    {
        high = numcolours - 1;
    }
 
    bestindex = 0;
    bestvalue = 999999;
 
    for (paletteidx = low; paletteidx <= high; paletteidx++)
    {
        colour = palette[paletteidx];
        diffr = ((colour >> 11) & 0x1F) - r;
        diffg = ((colour >> 6) & 0x1F) - g;
        diffb = ((colour >> 1) & 0x1F) - b;
 
        // An alpha mismatch costs 961, strongly discouraging a palette entry
        // with the wrong transparency.
        alphapenalty = (a == (colour & 1)) ? 0 : TEX_ALPHA_WEIGHT;
 
        distance = alphapenalty;
        distance += diffr * diffr;
        distance += diffg * diffg;
        distance += diffb * diffb;
 
        if (distance < bestvalue)
        {
            bestindex = paletteidx;
            bestvalue = distance;
        }
    }
 
    return bestindex;
}


/**
 * Find the palette entry closest to the given intensity/alpha pair and return its index.
 *
 * Used by TEXFORMAT_IA16_CI8 and TEXFORMAT_IA16_CI4 to requantize box-filtered
 * averages back onto the palette.
 *
 * Colours are IA16, packed iiiiiiiiaaaaaaaa, intensity and alpha both 0..255.
 *
 * @param palette    Palette to search, IA16, assumed ordered by the stage 2 key
 * @param numcolours Number of entries in palette
 * @param intensity  Target intensity, 0..255
 * @param alpha      Target alpha, 0..255
 * @return Index of the closest entry, or 0 if numcolours is not positive
 */
s32 texFindClosestColourIndexIA(u16 *palette, s32 numcolours, s32 intensity, s32 alpha)
{
    s32 scanstart;
    s32 high;
    s32 i;
    s32 scanidx;
    s32 bestindex;
    s32 bestvalue;
    s32 low;
    s32 targetcolour;
    s32 targetmagnitude;
    s32 colour;
    s32 entryintensity;
    s32 entryalpha;
    s32 magnitude;
    s32 diffi;
    s32 diffa;
    s32 distance;
 
    // Stage 1: scan the whole palette for a matching intensity and return its
    // index if one is found.
    targetcolour = (intensity << 8) | alpha;
 
    for (i = 0; i < numcolours; i++)
    {
        if ((u16)targetcolour == palette[i])
        {
            return i;
        }
    }
 
    // Stage 2: binary search by squared magnitude.
    low = 0;
    high = numcolours - 1;
    targetmagnitude = (intensity * intensity) + (alpha * alpha);
 
    while (high - low >= 2)
    {
        s32 mid;
 
        mid = (high + low) >> 1;
        colour = palette[mid];
 
        entryintensity = (colour >> 8) & 0xFF;
        entryalpha = colour & 0xFF;
        magnitude = (entryintensity * entryintensity) + (entryalpha * entryalpha);
 
        if (magnitude < targetmagnitude)
        {
            low = mid;
            continue;
        }
 
        if (targetmagnitude < magnitude)
        {
            high = mid;
        }
        else
        {
            // Equal magnitude. Collapse the window to end the search here.
            low = mid;
            high = mid;
        }
    }
 
    // Stage 3: widen the window by four entries either side and clamp it.
    scanstart = high - 4;
    scanidx = scanstart;
    high += 4;

    if (scanidx < 0)
    {
        scanstart = 0;
    }
 
    if (high >= numcolours)
    {
        high = numcolours - 1;
    }

    /* 999999 is an unreachable sentinel: the worst possible distance is
     * 2 * 255 * 255 = 130050. */
    bestindex = 0;
    bestvalue = 999999;

    scanidx = scanstart;
 
    if (scanidx <= high)
    {
        s32 scanend;

        for (;;)
        {
            colour = palette[scanidx];
 
            diffi = ((colour >> 8) & 0xff) - intensity;
            diffa = (colour & 0xff) - alpha;
            distance = (diffi * diffi) + (diffa * diffa);
            scanend = high + 1;
 
            if (distance < bestvalue)
            {
                bestindex = scanidx;
                bestvalue = distance;
            }

            scanidx++;

            if (scanend == scanidx)
            {
                break;
            }
        }
    }
 
    return bestindex;
}


/**
 * Inflate images (levels of detail) from a non-zlib texture.
 *
 * Each image can have a different compression method and pixel format, which is
 * described in a three byte header per image:
 *
 * ffffwwww wwwwhhhh hhhhcccc
 *
 * f = pixel format (see TEXFORMAT constants) w = width in pixels h = height in
 * pixels c = compression method (see TEXCOMPMETHOD constants)
 */
s32 texInflateNonZlib(u8 *src, u8 *dst, s32 arg2, s32 forcenumimages, struct texpool *arg4)
{
    u8 scratch[0x2000];
    u8 lookup[0x1000];
    u32 stack;
    s32 i;
    s32 numimages;
    s32 width;
    s32 height;
    s32 compmethod;
    s32 j;
    s32 totalbytesout = 0;
    s32 imagebytesout;
    s32 format;
    s32 value;
    u8 *start;
    u8 *end;
    s32 writetocache = FALSE;

    texSetBitstring(src);

    numimages = arg2 && forcenumimages ? forcenumimages : 1;

    arg4->rightpos->maxlod = forcenumimages;
    arg4->rightpos->hasExplicitLods = arg2;

    if (arg2)
    {
        writetocache = TRUE;

        for (i = 0; i < g_TexCacheCount; i++)
        {
            if (g_TexCacheItems[i].texturenum == arg4->rightpos->texturenum)
            {
                writetocache = FALSE;
            }
        }
    }

    for (i = 0; i < numimages; i++)
    {
        format = texReadBits(4);
        width = texReadBits(8);
        height = texReadBits(8);
        compmethod = texReadBits(4);

#ifndef TARGET_N64
        { /* bring-up (GE_DUMP_BOND): name the image whose decode runs away */
            extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
            f32 v[6];
            v[0] = (f32)g_TexNumToLoad; v[1] = (f32)i; v[2] = (f32)format;
            v[3] = (f32)width; v[4] = (f32)height; v[5] = (f32)compmethod;
            ge_dbg_vec("texinf num/lod/fmt/w/h/method", v, 6);
        }
#endif

        if (i == 0)
        {
            arg4->rightpos->width = width;
            arg4->rightpos->height = height;
            arg4->rightpos->gbiformat = g_TexFormatGbiMappings[format];
            arg4->rightpos->depth = g_TexFormatDepths[format];
            arg4->rightpos->lutmodeindex = g_TexFormatLutModes[format] >> G_MDSFT_TEXTLUT;
        }
        else if (writetocache)
        {
            g_TexCacheItems[g_TexCacheCount].widths[i - 1] = width;
            g_TexCacheItems[g_TexCacheCount].heights[i - 1] = height;
        }

        if (width * height > 0x2000)
        {
            return 0;
        }

        switch (compmethod)
        {
            case TEXCOMPMETHOD_UNCOMPRESSED0:
            case TEXCOMPMETHOD_UNCOMPRESSED1:
                imagebytesout = texReadUncompressed(&dst[totalbytesout], width, height, format);
                break;

            case TEXCOMPMETHOD_HUFFMAN:
                texInflateHuffman(scratch, g_TexFormatNumChannels[format] * width * height, g_TexFormatChannelSizes[format]);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_HUFFMANPERHCHANNEL:
                for (j = 0; j < g_TexFormatNumChannels[format]; j++)
                {
                    if (1);
                    texInflateHuffman(&scratch[width * height * j], width * height, g_TexFormatChannelSizes[format]);
                }

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_RLE:
                texInflateRle(scratch, g_TexFormatNumChannels[format] * width * height);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_LOOKUP:
                value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
                imagebytesout = texInflateLookup(width, height, &dst[totalbytesout], lookup, value, format);
                break;

            case TEXCOMPMETHOD_HUFFMANLOOKUP:
                value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
                texInflateHuffman(scratch, width * height, value);
                imagebytesout = texInflateLookupFromBuffer(scratch, width, height, &dst[totalbytesout], lookup, value, format);
                break;

            case TEXCOMPMETHOD_RLELOOKUP:
                value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
                texInflateRle(scratch, width * height);
                imagebytesout = texInflateLookupFromBuffer(scratch, width, height, &dst[totalbytesout], lookup, value, format);
                break;

            case TEXCOMPMETHOD_HUFFMANBLUR:
                stack = texReadBits(3);
                texInflateHuffman(scratch, g_TexFormatNumChannels[format] * width * height, g_TexFormatChannelSizes[format]);
                texBlur(scratch, width, g_TexFormatNumChannels[format] * height, stack, g_TexFormatChannelSizes[format]);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_RLEBLUR:
                stack = texReadBits(3);
                texInflateRle(scratch, g_TexFormatNumChannels[format] * width * height);
                texBlur(scratch, width, g_TexFormatNumChannels[format] * height, stack, g_TexFormatChannelSizes[format]);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            default:
                while (TRUE)
                {
                    // Hang forever!
                };
        }

#ifndef TARGET_N64
        if (i == 0)
        {
            /* tickhz=hw decode price; value is the colour count, set by the lookup methods only */
            portHwTexInfo(format, width, height, compmethod,
                          (compmethod == TEXCOMPMETHOD_LOOKUP || compmethod == TEXCOMPMETHOD_HUFFMANLOOKUP
                           || compmethod == TEXCOMPMETHOD_RLELOOKUP) ? value : 0, numimages);
        }
#endif
        if (arg2 == 1 && forcenumimages > 0)
        {
            texSwapAltRowBytes(&dst[totalbytesout], width, height, format);
        }

        imagebytesout = (imagebytesout + 7) & ~7;
        totalbytesout += imagebytesout;

        if (img_bitcount == 0)
        {
            img_curpos++;
        }
        else
        {
            img_bitcount = 0;
        }
    }

    if (writetocache)
    {
        g_TexCacheItems[g_TexCacheCount].texturenum = arg4->rightpos->texturenum;
#ifndef TARGET_N64
        portTexLodRemember(&g_TexCacheItems[g_TexCacheCount]);
#endif

        g_TexCacheCount++;

        // Resetting this to 0 suggests g_TexCacheItems is circular, but the
        // loop earlier in this function iterates up to g_TexCacheCount. Could
        // be a @bug, or maybe they intended to reset the cache every time it
        // fills up.
        if (g_TexCacheCount >= ARRAYCOUNT(g_TexCacheItems))
        {
            g_TexCacheCount = 0;
        }
    }

    if (!arg2)
    {
        if (forcenumimages >= 2)
        {
            s32 tmpwidth = width;
            s32 tmpheight = height;

            start = dst;
            end = &dst[totalbytesout];

            for (i = 1; i < forcenumimages; i++)
            {
                imagebytesout = texShrinkNonPaletted(start, end, tmpwidth, tmpheight, format);

                texSwapAltRowBytes(start, tmpwidth, tmpheight, format);

                totalbytesout += imagebytesout;

                tmpwidth = (tmpwidth + 1) >> 1;
                tmpheight = (tmpheight + 1) >> 1;

                start = end;
                end += imagebytesout;
            }

            texSwapAltRowBytes(start, tmpwidth, tmpheight, format);
        }
        else if (forcenumimages == 1)
        {
            texSwapAltRowBytes(dst, width, height, format);
        }
    }

    return totalbytesout;
}


/**
 * Shrink a non-paletted texture to half its size by averaging each each 2x2
 * group of pixels.
 *
 * Return the number of bytes written.
 *
 * If the source width is an odd number, the destination's final column is
 * calculated by sampling the final source column twice. Likewise for the
 * height.
 */

s32 texShrinkNonPaletted(u8 *src, u8 *dst, s32 srcwidth, s32 srcheight, s32 format)
{
    s32 i;
    s32 j;
    s32 alignedsrcwidth;
    s32 aligneddstwidth;
    u32 *dst32 = (u32 *) dst;
    u16 *dst16 = (u16 *) dst;
    u8 *dst8 = dst;
    u32 *src32 = (u32 *) src;
    u16 *src16 = (u16 *) src;
    u8 *src8 = src;
    s32 dstheight = (srcheight + 1) >> 1;
    s32 r;
    s32 g;
    s32 b;
    s32 a;
    s32 c;
    u32 tl32;
    u32 tr32;
    u32 bl32;
    u32 br32;
    u16 tl16;
    u16 tr16;
    u16 bl16;
    u16 br16;
    u8 tl8;
    u8 tr8;
    u8 bl8;
    u8 br8;
    s32 nextrow;
    s32 nextcol;

    switch (format)
    {
        case TEXFORMAT_RGBA32:
        case TEXFORMAT_RGB24:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 3) & 0xffc;
            alignedsrcwidth = (srcwidth + 3) & 0xffc;
            break;
        case TEXFORMAT_RGBA16:
        case TEXFORMAT_RGB15:
        case TEXFORMAT_IA16:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 3) & 0xffc;
            alignedsrcwidth = (srcwidth + 3) & 0xffc;
            break;
        case TEXFORMAT_IA8:
        case TEXFORMAT_I8:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 7) & 0xff8;
            alignedsrcwidth = (srcwidth + 7) & 0xff8;
            break;
        case TEXFORMAT_IA4:
        case TEXFORMAT_I4:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 15) & 0xff0;
            alignedsrcwidth = (srcwidth + 15) & 0xff0;
            break;
    }

    switch (format)
    {
        case TEXFORMAT_RGBA32:
        case TEXFORMAT_RGB24:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl32 = TEX32(src32[j]);
                    tr32 = TEX32(src32[nextcol]);
                    bl32 = TEX32(src32[nextrow + j]);
                    br32 = TEX32(src32[nextrow + nextcol]);

                    r = ((((tl32 >> 24) & 0xff) + ((tr32 >> 24) & 0xff) + ((bl32 >> 24) & 0xff) + ((br32 >> 24) & 0xff)) >> 2) & 0xff;
                    g = ((((tl32 >> 16) & 0xff) + ((tr32 >> 16) & 0xff) + ((bl32 >> 16) & 0xff) + ((br32 >> 16) & 0xff)) >> 2) & 0xff;
                    b = ((((tl32 >>  8) & 0xff) + ((tr32 >>  8) & 0xff) + ((bl32 >>  8) & 0xff) + ((br32 >>  8) & 0xff)) >> 2) & 0xff;
                    a = ((((tl32 >>  0) & 0xff) + ((tr32 >>  0) & 0xff) + ((bl32 >>  0) & 0xff) + ((br32 >>  0) & 0xff) + 1) >> 2) & 0xff;

                    dst32[j >> 1] = TEX32(r << 24 | g << 16 | b << 8 | a);
                }

                dst32 += aligneddstwidth;
                src32 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth * 4;

        case TEXFORMAT_RGBA16:
        case TEXFORMAT_RGB15:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl16 = TEX16(src16[j]);
                    tr16 = TEX16(src16[nextcol]);
                    bl16 = TEX16(src16[nextrow + j]);
                    br16 = TEX16(src16[nextrow + nextcol]);

                    r = ((((tl16 >> 11) & 0x1f) + ((tr16 >> 11) & 0x1f) + ((bl16 >> 11) & 0x1f) + ((br16 >> 11) & 0x1f)) >> 2) & 0x1f;
                    g = ((((tl16 >>  6) & 0x1f) + ((tr16 >>  6) & 0x1f) + ((bl16 >>  6) & 0x1f) + ((br16 >>  6) & 0x1f)) >> 2) & 0x1f;
                    b = ((((tl16 >>  1) & 0x1f) + ((tr16 >>  1) & 0x1f) + ((bl16 >>  1) & 0x1f) + ((br16 >>  1) & 0x1f)) >> 2) & 0x1f;
                    a = ((((tl16 >>  0) & 0x01) + ((tr16 >>  0) & 0x01) + ((bl16 >>  0) & 0x01) + ((br16 >>  0) & 0x01) + 2) >> 2) & 0x01;

                    dst16[j >> 1] = TEX16(r << 11 | g << 6 | b << 1 | a);
                }

                dst16 += aligneddstwidth;
                src16 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth * 2;

        case TEXFORMAT_IA16:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl16 = TEX16(src16[j]);
                    tr16 = TEX16(src16[nextcol]);
                    bl16 = TEX16(src16[nextrow + j]);
                    br16 = TEX16(src16[nextrow + nextcol]);

                    c = (((tl16 >> 8) & 0xff) + ((tr16 >> 8) & 0xff) + ((bl16 >> 8) & 0xff) + ((br16 >> 8) & 0xff)) >> 2;
                    a = ((tl16 & 0xff) + (tr16 & 0xff) + (bl16 & 0xff) + (br16 & 0xff) + 1) >> 2;

                    dst16[j >> 1] = TEX16(((u8)c << 8) | (a & 0xFF));
                }

                dst16 += aligneddstwidth;
                src16 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth * 2;

        case TEXFORMAT_IA8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl8 = src8[j];
                    tr8 = src8[nextcol];
                    bl8 = src8[nextrow + j];
                    br8 = src8[nextrow + nextcol];

                    c = ((((tl8 >> 4) & 0xf) + ((tr8 >> 4) & 0xf) + ((bl8 >> 4) & 0xf) + ((br8 >> 4) & 0xf)) << 2) & 0xF0;
                    a = (((tl8 & 0xf) + (tr8 & 0xf) + (bl8 & 0xf) + (br8 & 0xf) + 1) >> 2) & 0xF;

                    dst8[j >> 1] = c | a;
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_I8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl8 = src8[j];
                    tr8 = src8[nextcol];
                    bl8 = src8[nextrow + j];
                    br8 = src8[nextrow + nextcol];

                    c = (u16)((tl8 + tr8 + bl8 + br8 + 1) >> 2);

                    dst8[j >> 1] = c;
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_IA4:
            for (i = 0; i < srcheight; i += 2)
            {
                nextcol = i + 1;

                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    tl8 = src8[j >> 1];
                    tr8 = src8[(nextcol < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1)];
                    bl8 = src8[(j >> 1) + 1];
                    br8 = src8[(nextcol < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1) + 1];

                    c = (((((tl8 >> 5) & 7) + ((tl8 >> 1) & 7) + ((tr8 >> 5) & 7) + ((tr8 >> 1) & 7)) << 3) & 0xe0)
                        | (((((bl8 >> 5) & 7) + ((bl8 >> 1) & 7) + ((br8 >> 5) & 7) + ((br8 >> 1) & 7)) >> 1) & 0xe);

                    a = (((((tl8 >> 4) & 1) + (tl8 & 1) + ((tr8 >> 4) & 1) + (tr8 & 1) + 1) << 2) & 0x10)
                        | (((((bl8 >> 4) & 1) + (bl8 & 1) + ((br8 >> 4) & 1) + (br8 & 1) + 1) >> 2) & 1);

                    dst8[j >> 2] = c | a;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;

        case TEXFORMAT_I4:
            for (i = 0; i < srcheight; i += 2)
            {
                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    tl8 = src8[j >> 1];
                    tr8 = src8[(i + 1 < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1)];
                    bl8 = src8[(j >> 1) + 1];
                    br8 = src8[(i + 1 < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1) + 1];

                    c = ((((tl8 >> 4) & 0xf) + (tl8 & 0xf) + ((tr8 >> 4) & 0xf) + (tr8 & 0xf)) << 2) & 0xf0;
                    a = ((((bl8 >> 4) & 0xf) + (bl8 & 0xf) + ((br8 >> 4) & 0xf) + (br8 & 0xf)) >> 2) & 0xf;

                    dst8[j >> 2] = c | a;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;
    }

    return 0;
}


/**
 * Inflate Huffman data.
 *
 * This function operates on single channels rather than whole colours. For
 * example, for an RGBA32 image this function may be called once for each
 * channel with chansize = 256. This means the resulting data is in the format
 * RRR...GGG...BBB...AAA..., and the caller must convert it into a proper pixel
 * format.
 *
 * A typical Huffman implementation stores a tree, where each node contains the
 * lookup value and its frequency (number of uses). However, Rare's
 * implementation only stores a list of frequencies. It uses the chansize to
 * know how many values there are.
 */
void texInflateHuffman(u8 *dst, s32 numiterations, s32 chansize)
{
	u16 frequencies[2048];
	s16 nodes[2048][2];
	s32 i;
	s32 rootindex;
	s32 sum;
	u16 minfreq1;
	u16 minfreq2;
	s32 minindex1; // 5c
	s32 minindex2; // 58
	s32  done = 0;

	// Read the frequencies list
	for (i = 0; i < chansize; i++) {
		frequencies[i] = texReadBits(8);
	}

	// Initialise the tree
	for (i = 0; i < 2048; i++) {
		nodes[i][0] = -1;
		nodes[i][1] = -1;
	}

	// Find the two smallest frequencies
	minfreq1 = 9999;
	minfreq2 = 9999;

	for (i = 0; i < chansize; i++) {
		if (frequencies[i] < minfreq1) {
			if (minfreq2 < minfreq1) {
				minfreq1 = frequencies[i];
				minindex1 = i;
			} else {
				minfreq2 = frequencies[i];
				minindex2 = i;
			}
		} else if (frequencies[i] < minfreq2) {
			minfreq2 = frequencies[i];
			minindex2 = i;
		}
	}

	// Build the tree. For each node in tree, a branch value < 10000 means this
	// branch leads to another node, and the value is the target node's index. A
	// branch value >= 10000 means the branch is a leaf node, and the value is
	// the channel value + 10000.
	while (!done) {
		sum = frequencies[minindex1] + frequencies[minindex2];

		if (sum == 0) {
			sum = 1;
		}

		frequencies[minindex1] = 9999;
		frequencies[minindex2] = 9999;

		if (nodes[minindex1][0] < 0 && nodes[minindex1][1] < 0) {
			nodes[minindex1][0] = minindex1 + 10000;
			rootindex = minindex1;
			frequencies[minindex1] = sum;

			if (nodes[minindex2][0] < 0 && nodes[minindex2][1] < 0) {
				nodes[minindex1][1] = minindex2 + 10000;
			} else {
				nodes[minindex1][1] = minindex2;
			}
		} else if (nodes[minindex2][0] < 0 && nodes[minindex2][1] < 0) {
			nodes[minindex2][0] = minindex2 + 10000;
			rootindex = minindex2;
			frequencies[minindex2] = sum;

			if (nodes[minindex1][0] < 0 && nodes[minindex1][1] < 0) {
				nodes[minindex2][1] = minindex1 + 10000;
			} else {
				nodes[minindex2][1] = minindex1;
			}
		} else {
			for (rootindex = 0; nodes[rootindex][0] >= 0 || nodes[rootindex][1] >= 0 || frequencies[rootindex] < 9999; rootindex++);

			frequencies[rootindex] = sum;
			nodes[rootindex][0] = minindex1;
			nodes[rootindex][1] = minindex2;
		}

		// Find the two smallest frequencies again for the next iteration
		minfreq1 = 9999;
		minfreq2 = 9999;

		for (i = 0; i < chansize; i++) {
			if (frequencies[i] < minfreq1) {
				if (minfreq1 > minfreq2) {
					minfreq1 = frequencies[i];
					minindex1 = i;
				} else {
					minfreq2 = frequencies[i];
					minindex2 = i;
				}
			} else if (frequencies[i] < minfreq2) {
				minfreq2 = frequencies[i];
				minindex2 = i;
			}
		}

		if (minfreq1 == 9999 || minfreq2 == 9999) {
			done = 1;
		}
	}

	// Read bits off the bitstring, traverse the tree and write the channel
	// values to dst
	for (i = 0; i < numiterations; i++) {
		s32 indexorvalue = rootindex;

		while (indexorvalue < 10000) {
			indexorvalue = nodes[indexorvalue][texReadBits(1)];
		}

		if (chansize <= 256) {
			dst[i] = indexorvalue - 10000;
		} else {
			u16 *tmp = (u16 *)dst;
			tmp[i] = indexorvalue - 10000;
		}
	}
}




/**
 * Inflate runlength-encoded data.
 *
 * This data consists of a 10 bit header followed by a list of directives, where
 * each directive can either be a literal block or a repeat (run) of blocks
 * within a sliding window.
 *
 * The header format is:
 *
 * 3 bits btfieldsize: The size in bits of the backtrack distance fields 3 bits
 * rlfieldsize: The size in bits of the runlen fields 4 bits blocksize: The size
 * in bits of each block of data
 *
 * In the data, the first bit is 0 if it's a literal block or 1 if it's a run.
 *
 * For literal blocks, the next <blocksize> bits should be read and appended to
 * the output stream.
 *
 * For runs, the next <btfieldsize> bits are the backtrack length (in blocks)
 * plus one, and the next <rlfieldsize> bits are the run length (in blocks)
 * minus a calculated fudge value.
 *
 * The fudge value is calculated based on the field sizes. For small runs it is
 * more space efficient to use multiple literal directives rather than a run
 * directive. Because of this, smaller runs are not used and the run lengths in
 * the data can be offset accordingly - this offset is the fudge value.
 *
 * Every run must be followed by a literal block without the 1-bit marker. The
 * algorithm does not support back to back runs.
 */
void texInflateRle(u8 *dst, s32 blockstotal)
{
	s32 btfieldsize = texReadBits(3);
	s32 rlfieldsize = texReadBits(3);
	s32 blocksize = texReadBits(4);
	s32 cost;
	s32 fudge;
	s32 blocksdone;
	s32 i;

	// Calculate the fudge value
	cost = btfieldsize + rlfieldsize + blocksize + 1;
	fudge = 0;

	while (cost > 0) {
		cost = cost - blocksize - 1;
		fudge++;
	}

	blocksdone = 0;

	while (blocksdone < blockstotal) {
		if (texReadBits(1) == 0) {
			// Found a literal directive
			if (blocksize <= 8) {
				dst[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			} else {
				u16 *tmp = (u16 *)dst;
				tmp[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			}
		} else {
			// Found a run directive
			s32 startblockindex = blocksdone - texReadBits(btfieldsize) - 1;
			s32 runnumblocks = texReadBits(rlfieldsize) + fudge;

			if (blocksize <= 8) {
				for (i = startblockindex; i < startblockindex + runnumblocks; i++) {
					dst[blocksdone] = dst[i];
					blocksdone++;
				}

				// The next instruction must be a literal
				dst[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			} else {
				u16 *tmp = (u16 *)dst;

				for (i = startblockindex; i < startblockindex + runnumblocks; i++) {
					tmp[blocksdone] = tmp[i];
					blocksdone++;
				}

				// The next instruction must be a literal
				tmp[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			}
		}
	}
}




/**
 * Populate a lookup table by reading it out of the bit string.
 *
 * The first 11 bits denote the number of colours in the lookup table. The data
 * following this is a list of colours, where each colour is sized according to
 * the texture's format.
 *
 * This function does NOT work with pixel formats of 8 bits or less.
 */
s32 texBuildLookup(s8 *lookup, s32 bitsperpixel)
{
    s32 numcolors = texReadBits(11);
    s32 i;
    
    if (bitsperpixel <= 16)
    {
        u16 *image = (u16*)lookup;
        for (i = 0; i < numcolors; i++)
        {
            image[i] = TEX16(texReadBits(bitsperpixel));
        }
    }
    else if (bitsperpixel <= 24)
    {
        u32 *image = (u32*)lookup;
        for (i = 0; i < numcolors; i++)
        {
            image[i] = TEX32(texReadBits(bitsperpixel));
        }
    }
    else
    {
        u32 *image = (u32*)lookup;
        for (i = 0; i < numcolors; i++)
        {
            image[i] = TEX32(texReadBits(24) << 8 | texReadBits(bitsperpixel - 24));
        }
    }
    return numcolors;
}






s32 texGetBitSize(s32 decimal)
{
	s32 count = 0;

	decimal--;

	while (decimal > 0) {
		decimal >>= 1;
		count++;
	}

	return count;
}



void texReadAlphaBits(u8 *image,s32 count)
{
  int i;
  
    for(i = 0; i < count; i++)
    {
          image[i] = texReadBits(1);
    }
}



/**
 * Read pixel data from the bitstream and write to dst, ensuring each row is
 * aligned according to the pixel format.
 *
 * Return the number of output bytes.
 */
s32 texReadUncompressed(u8 *dst, s32 width, s32 height, s32 format)
{
	u32 *dst32 = (u32 *)(((uintptr_t)dst + 0xf) & ~0xf);
	u16 *dst16 = (u16 *)(((uintptr_t)dst + 7) & ~7);
	u8 *dst8 = (u8 *)(((uintptr_t)dst + 7) & ~7);
	s32 x;
	s32 y;

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = TEX32(texReadBits(16) << 16);
				dst32[x] = TEX32(TEX32(dst32[x]) | texReadBits(16));
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = TEX32(texReadBits(24) << 8 | 0xff);
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = TEX16(texReadBits(16));
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = TEX16(texReadBits(15) << 1 | 1);
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst8[x] = texReadBits(8);
			}

			dst8 += (width + 7) & 0xff8;
		}

		return ((width + 7) & 0xff8) * height;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = texReadBits(8);
			}

			dst8 += ((width + 15) & 0xff0) >> 1;
		}

		return (((width + 15) & 0xff0) >> 1) * height;
	}

	return 0;
}


s32 texChannelsToPixels(u8 *src, s32 width, s32 height, u8 *dst, s32 format)
{
    u32 *dst32;
    u16 *dst16;
    u8 *dst8;
    s32 x;
    s32 y;
    s32 pos;
    s32 mult;
    s32 rgb_width;

    dst32 = (u32 *)dst;
    dst16 = (u16 *)dst;
    dst8  = (u8 *)dst;

    pos = 0;
    mult = width * height;
    rgb_width = (width + 3) & 0xffc;

    switch (format)
    {
        case TEXFORMAT_RGBA32:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst32[x] = src[pos] << 24 | src[pos + mult] << 16 | src[pos + mult * 2] << 8 | src[pos + mult * 3];
                    pos++;
                }

                dst32 += rgb_width;
            }

            return (rgb_width) * height * 4;

        case TEXFORMAT_RGB24:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst32[x] = TEX32(src[pos] << 24 | src[pos + mult] << 16 | src[pos + mult * 2] << 8 | 0xff);
                    pos++;
                }

                dst32 += rgb_width;
            }

            return (rgb_width) * height * 4;

        case TEXFORMAT_RGBA16:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst16[x] = TEX16(src[pos] << 11 | src[pos + mult] << 6 | src[pos + mult * 2] << 1 | src[pos + mult * 3]);
                    pos++;
                }

                dst16 += rgb_width;
            }

            return (rgb_width) * height * 2;

        case TEXFORMAT_IA16:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst16[x] = TEX16(src[pos] << 8 | src[pos + mult]);
                    pos++;
                }

                dst16 += rgb_width;
            }

            return (rgb_width) * height * 2;

        case TEXFORMAT_RGB15:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst16[x] = TEX16(src[pos] << 11 | src[pos + mult] << 6 | src[pos + mult * 2] << 1 | 1);
                    pos++;
                }

                dst16 += rgb_width;
            }

            return (rgb_width) * height * 2;

        case TEXFORMAT_IA8:
            for (y = 0; y < height; y++)
            {
                if ((width + 7) & 0xff8);

                for (x = 0; x < width; x++)
                {
                    dst8[x] = src[pos] << 4 | src[pos + mult];
                    pos++;
                }

                dst8 += (width + 7) & 0xff8;
            }

            return ((width + 7) & 0xff8) * height;

        case TEXFORMAT_I8:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst8[x] = src[pos];
                    pos++;
                }

                dst8 += (width + 7) & 0xff8;
            }

            return ((width + 7) & 0xff8) * height;

        case TEXFORMAT_IA4:
            for (y = 0; y < height; y++)
            {
                if ((width + 15) & 0xff0);

                for (x = 0; x < width; x += 2)
                {
                    dst8[x >> 1] = src[pos] << 5 | src[pos + mult * 3] << 4 | src[pos + 1] << 1 | src[pos + mult * 3 + 1];
                    pos += 2;
                }

                if (width & 1) { pos--; }

                dst8 += (width + 15) & 0xff0;
            }

            return (((width + 15) & 0xff0) >> 1) * height;

        case TEXFORMAT_I4:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x += 2)
                {
                    dst8[x >> 1] = src[pos] << 4 | src[pos + 1];
                    pos += 2;
                }

                if (width & 1) { pos--; }

                dst8 += ((width + 15) & 0xff0) >> 1;
            }

            return (((width + 15) & 0xff0) >> 1) * height;
    }

    return 0;
}


/**
 * Inflate a texture using the provided lookup table.
 *
 * The lookup table is a bitstring of colours in the pixel format described by
 * the format argument. The number of colours in the lookup table is given by
 * the numcolours argument.
 *
 * The data in the global source bitstring is expected to be a tightly packed
 * list of indices into the lookup table. The number of bits for each index is
 * calculated based on the number of colours in the lookup table. For example,
 * if the lookup table contains 8 colours then the indices will be 0-7, which
 * requires 3 bits per index.
 *
 * Return the number of bytes written to dst.
 */
s32 texInflateLookup(s32 width, s32 height, u8 *dst, u8 *lookup, s32 numcolours, s32 format)
{
	u32 *lookup32 = (u32 *)lookup;
	u16 *lookup16 = (u16 *)lookup;
	u32 *dst32 = (u32 *)dst;
	u16 *dst16 = (u16 *)dst;
	u8 *dst8 = (u8 *)dst;
	s32 x;
	s32 y;
	s32 bitspercolour = texGetBitSize(numcolours);

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = lookup32[texReadBits(bitspercolour)];
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = TEX32(TEX32(lookup32[texReadBits(bitspercolour)]) << 8);
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = lookup16[texReadBits(bitspercolour)];
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = TEX16(TEX16(lookup16[texReadBits(bitspercolour)]) << 1 | 1);
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst8[x] = TEX16(lookup16[texReadBits(bitspercolour)]);
			}

			dst8 += (width + 7) & 0xff8;
		}

		return ((width + 7) & 0xff8) * height;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = TEX16(lookup16[texReadBits(bitspercolour)]) << 4;

				if (x + 1 < width) {
					dst8[x >> 1] |= lookup[(texReadBits(bitspercolour) * 2) + 1];
				}
			}

			dst8 += ((width + 15) & 0xff0) >> 1;
		}

		return (((width + 15) & 0xff0) >> 1) * height;
	}

	return 0;
}


/**
 * Like texInflateLookup, but the indices are provided in the src argument as
 * u8s or u16s rather than read from the global bitstring as tightly packed
 * bits.
 *
 * Whether u8s or u16s are expected depends on whether the number of colours in
 * the lookup table. If there are more than 256 colours then it must use u16s,
 * otherwise it expects u8s.
 */

s32 texInflateLookupFromBuffer(u8 *src, s32 width, s32 height, u8 *dst, u8 *lookup, s32 numcolours, s32 format)
{
    s32 x;
    s32 y;
    u32 *lookup32;
    u16 *lookup16;
    u8 *src8;
    u16 *src16;
    u32 *dst32;
    u16 *dst16;
    u8 *dst8;
    u32 basic_and_val;

    lookup32 = (u32 *)lookup;
    lookup16 = (u16 *)lookup;

    basic_and_val = 0xffc;

    dst32 = (u32 *)dst;
    dst16 = (u16 *)dst;
    dst8 = (u8 *)dst;

    if (numcolours <= 256)
    {
        src8 = (u8 *)src;
    }
    else
    {
        src16 = (u16 *)src;
    }

    switch (format)
    {
        case TEXFORMAT_RGBA32:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst32[x] = lookup32[src8[x]];
                    }
                    else
                    {
                        dst32[x] = lookup32[src16[x]];
                    }
                }

                dst32 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 4;

        case TEXFORMAT_RGB24:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst32[x] = TEX32(TEX32(lookup32[src8[x]]) << 8 | 0xff);
                    }
                    else
                    {
                        dst32[x] = TEX32(TEX32(lookup32[src16[x]]) << 8 | 0xff);
                    }
                }

                dst32 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 4;

        case TEXFORMAT_RGBA16:
        case TEXFORMAT_IA16:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst16[x] = lookup16[src8[x]];
                    }
                    else
                    {
                        dst16[x] = lookup16[src16[x]];
                    }
                }

                dst16 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 2;

        case TEXFORMAT_RGB15:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst16[x] = TEX16(TEX16(lookup16[src8[x]]) << 1 | 1);
                    }
                    else
                    {
                        dst16[x] = TEX16(TEX16(lookup16[src16[x]]) << 1 | 1);
                    }
                }

                dst16 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 2;

        case TEXFORMAT_IA8:
        case TEXFORMAT_I8:
            for (y = 0; y < height; y++)
            {
                if ((width + 7) & 0xff8);

                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst8[x] = TEX16(lookup16[src8[x]]);
                    }
                    else
                    {
                        dst8[x] = TEX16(lookup16[src16[x]]);
                    }
                }

                dst8 += (width + 7) & 0xff8;
                src8 += width;
                src16 += width;
            }

            return ((width + 7) & 0xff8) * height;

        case TEXFORMAT_IA4:
        case TEXFORMAT_I4:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x += 2)
                {
                    if (numcolours <= 256)
                    {
                        dst8[x >> 1] = TEX16(lookup16[src8[x]]) << 4 | TEX16(lookup16[src8[x + 1]]);
                    }
                    else
                    {
                        dst8[x >> 1] = TEX16(lookup16[src16[x]]) << 4 | TEX16(lookup16[src16[x + 1]]);
                    }
                }

                dst8 += ((width + 15) & 0xff0) >> 1;
                src8 += width;
                src16 += width;
            }

            return (((width + 15) & 0xff0) >> 1) * height;
    }

    return 0;
}


/**
 * For every second row, swap the bytes within that row.
 *
 * For textures with 32-bit colour values (in GBI format), swap every pair
 * within each word. For all other textures, swap every byte within each pair.
 */
void texSwapAltRowBytes(u8 *dst, s32 width, s32 height, s32 format)
{
	s32 x;
	s32 y;
	s32 alignedwidth;
	u32 *row = (u32 *)dst;
	s32 tmp;

#ifndef TARGET_N64
	/* PORT: this pre-swaps ODD TEXTURE ROWS to counter the N64 TMEM
	 * odd-row word swizzle. fast3d reads textures linearly - no TMEM - so the
	 * pre-swap garbles every second line (the gunbarrel-swirl "missing
	 * alternate lines" artifact). No-op on PC. */
	return;
#endif

	switch (format) {
	case TEXFORMAT_RGBA32:
	case TEXFORMAT_RGB24:
		alignedwidth = (width + 3) & 0xffc;
		break;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_RGB15:
	case TEXFORMAT_IA16:
		alignedwidth = ((width + 3) & 0xffc) >> 1;
		break;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
	case TEXFORMAT_RGBA16_CI8:
	case TEXFORMAT_IA16_CI8:
		alignedwidth = ((width + 7) & 0xff8) >> 2;
		break;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
	case TEXFORMAT_RGBA16_CI4:
	case TEXFORMAT_IA16_CI4:
		alignedwidth = ((width + 0xf) & 0xff0) >> 3;
		break;
	}

	row += alignedwidth;

	if (format == TEXFORMAT_RGBA32 || format == TEXFORMAT_RGB24) {
		for (y = 1; y < height; y += 2) {
			for (x = 0; x < alignedwidth; x += 4) {
				tmp = row[x + 0];
				row[x + 0] = row[x + 2];
				row[x + 2] = tmp;

				tmp = row[x + 1];
				row[x + 1] = row[x + 3];
				row[x + 3] = tmp;
			}

			row += alignedwidth * 2;
		}
	} else {
		for (y = 1; y < height; y += 2) {
			for (x = 0; x < alignedwidth; x += 2) {
				tmp = row[x + 0];
				row[x + 0] = row[x + 1];
				row[x + 1] = tmp;
			}

			row += alignedwidth * 2;
		}
	}
}






/**
 * Blur the pixels in the image with the surrounding pixels.
 */
void texBlur(u8 *pixels, s32 width, s32 height, s32 method, s32 chansize)
{
	s32 x;
	s32 y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			s32 cur = pixels[y * width + x] + chansize * 2;
			s32 left = x > 0 ? pixels[y * width + x - 1] : 0;
			s32 above = y > 0 ? pixels[(y - 1) * width + x] : 0;
			s32 aboveleft = x > 0 && y > 0 ? pixels[(y - 1) * width + x - 1] : 0;

			switch (method) {
			case 0:
				pixels[y * width + x] = (cur + left) % chansize;
				break;
			case 1:
				pixels[y * width + x] = (cur + above) % chansize;
				break;
			case 2:
				pixels[y * width + x] = (cur + aboveleft) % chansize;
				break;
			case 3:
				pixels[y * width + x] = (cur + (left + above - aboveleft)) % chansize;
				break;
			case 4:
				pixels[y * width + x] = (cur + ((above - aboveleft) / 2 + left)) % chansize;
				break;
			case 5:
				pixels[y * width + x] = (cur + ((left - aboveleft) / 2 + above)) % chansize;
				break;
			case 6:
				pixels[y * width + x] = (cur + ((left + above) / 2)) % chansize;
				break;
			}
		}
	}
}


void texInitPool(struct texpool *pool, u8 *start, s32 len)
{
#ifndef TARGET_N64
    { /* the old entries stay in memory: crosshair_color must stop trusting them */
        extern void portTexCrosshairForget(const void *lo, const void *hi);

        portTexCrosshairForget(start, start + len);
    }
#endif
    pool->start    = start;
    pool->end      = (struct tex *)(start + len);
    pool->leftpos  = start;
    pool->rightpos = (struct tex *)(start + len);
}


struct tex *texFindInPool(s32 texturenum, struct texpool *arg1)
{
    struct tex *end;
    struct tex *cur;
    s32 i;

    if (arg1 == NULL)
    {
        arg1 = &ptr_texture_alloc_start;
    }

    end = arg1->end;
    cur = arg1->rightpos;

    while (cur < end)
    {
        if (cur->texturenum == texturenum)
        {
            return cur;
        }

        cur++;
    }

    return NULL;
}


s32 texFreeBytesInBuffer(struct texpool *arg0)
{
	return (uintptr_t)arg0->rightpos - (uintptr_t)arg0->leftpos;
}


void texLoadFromDisplayList(Gfx *gdl, struct texpool *arg1)
{
#ifdef TARGET_N64
    u8 *bytes = (u8 *)gdl;

    while (bytes[0] != (u8)G_ENDDL)
    {
        // Look for GBI sequence: fd...... abcd....
        if (bytes[0] == G_SETTIMG && bytes[4] == 0xab && bytes[5] == 0xcd)
        {
            texLoad((u32 *)((s32)bytes + 4), arg1);
        }

        bytes += 8;
    }
#else
    /* The byte positions above assume the N64 big-endian word layout, but the
     * compiled-in display lists (oddtextures.c globalDL_*) hold native words.
     * Reading words natively instead keeps IMAGESEG SETTIMG commands from
     * reaching the renderer. */
    Gfx *cmd = gdl;

    while ((cmd->words.w0 >> 24) != (u8)G_ENDDL)
    {
        if ((cmd->words.w0 >> 24) == (u8)G_SETTIMG &&
            (cmd->words.w1 >> 16) == 0xABCDu)
        {
            texLoad(&cmd->words.w1, arg1);
        }

        cmd++;
    }
#endif
}


#ifdef TARGET_N64
extern u8 _imagesSegmentRomStart;
#else
/* PORT (ROM loading Phase B, port/src/segments.c): a POINTER VARIABLE -
 * the compiled-in blob until romdata.c repoints it at the user's cart;
 * `&_imagesSegmentRomStart` below = `&(*port_...)` = the pointer value. */
extern u8 *port_imagesSegmentRomStart;
#define _imagesSegmentRomStart (*port_imagesSegmentRomStart)
#endif

/**
 * Load and decompress a texture from ROM.
 *
 * The given pointer points to a word which determines what to load. The formats
 * of the word are:
 *
 *     abcdxxxx -> load texture number xxxx
 *     0000xxxx -> load texture number xxxx
 *     (memory address) -> the texture is already loaded, so do nothing
 *
 * After loading and decompressing the texture, the value that's pointed to is
 * changed to be a pointer to... something.
 *
 * There are two types of textures:
 *
 * - Zlib-compressed textures, which are always paletted
 * - Non-zlib textures, which use a variety of (non-zlib) compression methods
 *   and are sometimes paletted
 *
 * Both types have support for multiple levels of detail (ie. multiple images of
 * varying size) within each texture. There are enough bits in the header byte
 * to support 64 levels of detail, but this function caps it to 5. Some textures
 * actually specify up to 7 levels of detail. However testing suggests that the
 * additional levels of detail are not even read.
 *
 * This function reads the above information from the first byte of texture
 * data, then calls the texInflateZlib or texInflateNonZlib to inflate the
 * images.
 *
 * The format of the first byte is:
 * uzllllll
 *
 * u = unknown z = texture is compressed with zlib l = number of levels of
 * detail within the texture
 */
void texLoad(uintptr_t *updateword, struct texpool *pool)
{
    u8 compbuffer[4000];
    u8 *compptr;
    s32 sp14a8;
    s32 iszlib;
    s32 lod;
    struct tex *tex;
    u8 *alignedcompbuffer;
    s32 thisoffset;
    s32 nextoffset;
    s16 *texnumptr;
    s32 bytesout;

    if (pool == NULL)
    {
        pool = (struct texpool*) &ptr_texture_alloc_start;
    }

    g_TexNumToLoad = *updateword & 0xffff;
    tex = texFindInPool(g_TexNumToLoad, pool);

    if (tex == NULL)
    {
#ifndef TARGET_N64
        extern unsigned long long portProfNow(void);
        extern void portHitchNote(int cat, unsigned long long us);
        const unsigned long long hitchT0 = portProfNow(); /* hitchlog.c */
#endif
        alignedcompbuffer = (u8 *) (((uintptr_t)compbuffer + 0xF) >> 4 << 4);

        if (alignedcompbuffer);
        if (tex);

        osWritebackDCacheAll();
        osInvalDCache(alignedcompbuffer, DCACHE_SIZE);

#ifdef TARGET_N64
        thisoffset = *((s32*)&g_Textures[g_TexNumToLoad]) & 0xFFFFFF;
        nextoffset = (*((s32 *) (&g_Textures[g_TexNumToLoad + 1]))) & ((unsigned long) 0xFFFFFF);
#else
        /* The original raw-word read relies on IDO's MSB-first bitfield
         * packing, which GCC does not use, so use the layout-independent
         * bitfield accessor. The images segment is embedded byte-exact
         * (port/src/segments.c), so the decompressors below run unmodified. */
        thisoffset = g_Textures[g_TexNumToLoad].dataoffset;
        nextoffset = g_Textures[g_TexNumToLoad + 1].dataoffset;
#endif

#ifndef TARGET_N64
        { /* bring-up (GE_DUMP_BOND): the file-select walletbond load dies in
           * texInflateNonZlib with a runaway source - which texture number and
           * offsets feed it? */
            extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
            f32 v[4];
            v[0] = (f32)g_TexNumToLoad;
            v[1] = (f32)thisoffset;
            v[2] = (f32)nextoffset;
            v[3] = (f32)(nextoffset - thisoffset);
            ge_dbg_vec("texload num/this/next/size", v, 4);
        }
#endif

        if (TRUE)
        {
            // Copy the compressed texture to RAM
            romCopy(alignedcompbuffer,
                    (uintptr_t) &_imagesSegmentRomStart + (thisoffset & 0xfffffff8),
                    ((u32) (nextoffset - thisoffset) + 0x1f) >> 4 << 4);
#ifndef TARGET_N64
            { /* tickhz=hw load hitch: the texture's cart read; its decode is priced
               * after the decoder runs (portHwTexDecoded, frametiming.c) */
                extern void portHwNoteLoad(u32 romBytes, s32 compressed);
                portHwNoteLoad((u32) (nextoffset - thisoffset), 0);
            }
#endif

            compptr = (u8 *) alignedcompbuffer + (thisoffset & 7);
            thisoffset = 0;
            sp14a8 = (*compptr & 0x80) >> 7;
            iszlib = (*compptr & 0x40) >> 6;
            lod = *compptr & 0x3f;
            compptr++;

            // If there's not enough memory, point the texture at the start of
            // the pool: it is garbage data, but the only other option is a
            // crash. GBI commands hold texture IDs that must be replaced with
            // pointers.
            if ((!iszlib && (texFreeBytesInBuffer(pool) < 0x10CC)) || (iszlib && texFreeBytesInBuffer(pool) < 0xA28)) {
#ifndef TARGET_N64
                /* This garbage-texture path is silent on N64, so on PC log
                 * (rate limited) which pool and texture did not fit and how
                 * much room was left. */
                {
                    static s32 warned;
                    if (warned < 12)
                    {
                        warned++;
                        printf("ge: TEXOOM: texnum %d cannot fit - pointing at pool start (pool %p, %d bytes free)\n",
                               (s32) g_TexNumToLoad, (void *) pool, texFreeBytesInBuffer(pool));
                    }
                }
#endif
                *updateword = osVirtualToPhysical(pool->start);
                return;
            }

            // Write the texturenum into the allocation
            texnumptr = (s16 *) pool->leftpos;
            *texnumptr = g_TexNumToLoad;
            pool->leftpos += 8;

            // Write a tex into the allocation
            pool->rightpos--;
            tex = pool->rightpos;
            tex->texturenum = g_TexNumToLoad;
            tex->data = pool->leftpos;

            // Extract the texture data to the allocation (pool->leftpos)
            if (iszlib) {
                bytesout = texInflateZlib(compptr, pool->leftpos, sp14a8, lod, pool);
            } else {
                bytesout = texInflateNonZlib(compptr, pool->leftpos, sp14a8, lod, pool);
            }
#ifndef TARGET_N64
            portHwTexDecoded(g_TexNumToLoad, iszlib, lod, sp14a8); /* tickhz=hw: the console's decode time */
#endif

#ifndef TARGET_N64
            { /* diagnostic (GE_TEXDUMP=num): write one texture's decoded
               * pool bytes + record fields to tex_<num>.bin - offline decode
               * splits loader-vs-renderer for corrupt textures (F2.5 blue Depot
               * floor hunt). */
                extern char *getenv(const char *);
                extern void *portRootFopen(const char *name, const char *mode);
                extern int sprintf(char *, const char *, ...);
                static s32 wantnum = -2;
                if (wantnum == -2)
                {
                    const char *e = getenv("GE_TEXDUMP");
                    wantnum = -1;
                    if (e != NULL && e[0] == 'a')
                    {
                        wantnum = -3;   /* GE_TEXDUMP=all: every texture, one file each */
                    }
                    else if (e != NULL && e[0] >= '0' && e[0] <= '9')
                    {
                        s32 k;
                        wantnum = 0;
                        for (k = 0; e[k] >= '0' && e[k] <= '9'; k++)
                        {
                            wantnum = wantnum * 10 + (e[k] - '0');
                        }
                    }
                }
                if (wantnum == -3 || (wantnum >= 0 && g_TexNumToLoad == wantnum))
                {
                    char name[64];
                    void *f;
                    sprintf(name, "tex_%d.bin", (s32) g_TexNumToLoad);
                    f = portRootFopen(name, "wb");
                    if (f != NULL)
                    {
                        fwrite(pool->leftpos, 1, bytesout, f);
                        fclose(f);
                    }
                    printf("ge: texdump: num=%d bytes=%d w=%d h=%d gbifmt=%d depth=%d lut=%d maxlod=%d explicit=%d ncol=%d data=%p\n",
                           (s32) g_TexNumToLoad, bytesout,
                           tex->width, tex->height, tex->gbiformat, tex->depth,
                           tex->lutmodeindex, tex->maxlod, tex->hasExplicitLods,
                           (s32) tex->unk0a + 1, (void *) tex->data);
                }
            }
#endif

#ifndef TARGET_N64
            { /* EXTERNAL TEXTURES BY NUMBER: a
               * Data/Textures/Base/t<num>.bmp rewrites the bytes just inflated,
               * in place, same layout - port_textures.c */
                extern s32 portTexReplaceRom(struct tex *t, u8 *data, s32 bytes);
                extern void portTexCrosshairLoaded(struct tex *t, u8 *data, s32 bytes);

                portTexReplaceRom(tex, pool->leftpos, bytesout);
                portTexCrosshairLoaded(tex, pool->leftpos, bytesout);   /* crosshair_color, shading kept */
            }
#endif

            pool->leftpos += bytesout;
        }

        texFreeBytesInBuffer(pool);
#ifndef TARGET_N64
        portHitchNote(2 /* tex */, portProfNow() - hitchT0);
#endif
    }

    *updateword = osVirtualToPhysical(tex->data);
}


void texLoadFromModelFileHeader(ModelFileHeader* arg0, struct texpool* arg1)
{
    s32 i;
    ModelFileTextures* textures;

    textures = arg0->Textures;

    for (i = 0; i < arg0->numtextures; i++)
    {
#ifdef TARGET_N64
        if ((s32)textures[i].TextureID < (s32)MAX_TEXTURES)
#else
        /* PORT: texLoad overwrites TextureID with the data address, and the
         * original signed compare guards against already-converted entries. A
         * host pointer can be >= 0x80000000 (negative as s32), so use an
         * unsigned compare to reject converted values on both platforms. */
        if (textures[i].TextureID < (u32)MAX_TEXTURES)
#endif
        {
            texLoad(&textures[i].TextureID, arg1);
        }
    }
}


#ifndef TARGET_N64
/* tickhz=hw load hitches (frametiming.c): one texture's span in the images
 * segment, 0 for a number outside the table */
u32 portTexRomBytes(s32 texnum)
{
    s32 span;

    if (texnum < 0 || texnum + 1 >= (s32) PORT_TEXTURES_MAX)
    {
        return 0;
    }
    span = (s32) g_Textures[texnum + 1].dataoffset - (s32) g_Textures[texnum].dataoffset;
    return (span > 0 && span < 0x10000) ? (u32) span : 0; /* past the table's end the entries are 0 */
}
#endif


void texLoadFromTextureNum(s32 texturenum, struct texpool *arg1)
{
    uintptr_t texturenumcopy;
    texturenumcopy = texturenum;
    texLoad(&texturenumcopy, arg1);
}
