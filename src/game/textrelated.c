#include <ultra64.h>
#include "fr.h"
#include <memp.h>
#include "textrelated.h"
#include "bondtypes.h"
#include "game/language.h"

#define SPACE_WIDTH 5

#define M_COLOR_R(x) (u8)(x >> 0x18)
#define M_COLOR_G(x) (u8)(x >> 0x10)
#define M_COLOR_B(x) (u8)(x >> 0x08)
#define M_COLOR_A(x) (u8)(x >> 0x00)

// data
s32 D_80040E80 = 0; // Unused
s32 text_spacing = 0;
s32 text_orientation = 0;
s32 text_wordwrap = 0;
s32 overlap_correction = -1;
s32 text_bilevel_filter = FALSE;
s32 text_x = 0;
s32 text_y = 0;
s32 text_s = 0;
s32 text_t = 0;
s32 g_JpnTextTlutNeedsLoad = 0;
struct font * ptrFontBankGothic = NULL;
struct fontchar * ptrFontBankGothicChars = NULL;
struct font * ptrFontZurichBold = NULL;
struct fontchar * ptrFontZurichBoldChars = NULL;

u16 g_JpnTextPalette0[16] = {
    0x0000, 0x5555, 0xaaaa, 0xffff,
    0x0000, 0x5555, 0xaaaa, 0xffff,
    0x0000, 0x5555, 0xaaaa, 0xffff,
    0x0000, 0x5555, 0xaaaa, 0xffff
};

u16 g_JpnTextPalette1[16] = {
    0x0000, 0x0000, 0x0000, 0x0000,
    0x5555, 0x5555, 0x5555, 0x5555,
    0xAAAA, 0xAAAA, 0xAAAA, 0xAAAA,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
};

// D_80040EFC
struct fontchar g_JpnGlyphDefaults = {0, 0, 12, 11};

// D_80040F14
struct fontchar g_JpnGlyphDefaultsOutlined = {0, 0, 12, 11};

// forward declarations

Gfx *textRenderGlyph(Gfx *gdl, s32 *x, s32 *y, struct fontchar *curchar, struct fontchar *prevchar,
		struct font *font, s32 clipX, s32 clipY, s32 clipWidth, s32 clipHeight, s32 yOffset);
Gfx *textRenderGlyphOutlined(Gfx *gdl, s32 *x, s32 *y, struct fontchar *curchar, 
                struct fontchar *prevchar, struct font *font, s32 clipX, s32 clipY, 
                u32 textColour, u32 outlineColour, s32 clipWidth, s32 clipHeight, 
                s32 yOffset);
Gfx *textDrawGlyphQuad(Gfx *gdl, s32 x, s32 y, struct fontchar *curchar, s32 clipX, s32 clipY, s32 clipWidth, s32 clipHeight);

// -------------------------------------------------------------------------------------------------

void textInit(void /*, char* asset */)
{
    #ifdef DEBUG
    if (asset == 0)
    {
        osSyncPrintf("\n--- ASSERTION FAULT - %s - %s, line %d\n\n", "textInitText: NULL void* asset", ".\\text.cpp", 0x38 );
    }
    #endif
}

void setTextSpacingInverted(s32 spacing) {
    text_spacing = -spacing;
}

void setTextOrientation(s32 orientation) {
    text_orientation = orientation;
}

void setTextWordWrap(s32 flag) {
    text_wordwrap = flag;
}

void setTextOverlapCorrection(s32 flag) {
    overlap_correction = flag;
}

extern u8 _fontbankgothicSegmentEnd;
extern u8 _fontbankgothicSegmentRomStart;
extern u8 _fontzurichboldSegmentEnd;
extern u8 _fontzurichboldSegmentRomStart;
extern u8 _fontzurichboldSegmentStart;
extern u8 _fontbankgothicSegmentStart;

#ifndef TARGET_N64
/* Rare pads every glyph row by repeating its last column, which made the port's
 * half-texel glyph filtering sample stray ink (an "i" grew a second stem), so
 * the padding is cleared once per load; nothing else reads these columns. */
static void portFontClearPadding(struct fontchar *chars, s32 count)
{
    s32 i, r, c;

    for (i = 0; i < count; i++)
    {
        const s32 stride = (chars[i].width + 7) & 0xF8;

        if (chars[i].pixeldata == NULL || chars[i].width <= 0 || chars[i].height <= 0)
        {
            continue;
        }
        for (r = 0; r < chars[i].height; r++)
        {
            for (c = chars[i].width; c < stride; c++)
            {
                chars[i].pixeldata[r * stride + c] = 0;
            }
        }
    }
}
#endif

#ifdef GE_NATIVE_64
static struct font *portFontLoadNative(const u8 *source, u32 size)
{
    u8 *raw = mempAllocBytesInBank(size, MEMPOOL_STAGE);
    struct font *font = mempAllocBytesInBank(sizeof(*font), MEMPOOL_STAGE);
    memcpy(raw, source, size);
    memset(font, 0, sizeof(*font));
    memcpy(font->kerning, raw, sizeof(font->kerning));
    for (s32 i = 0; i < 94; i++) {
        const u8 *record = raw + 169 * 4 + i * 24;
        u32 offset;
        memcpy(&font->chars[i], record, 20);
        memcpy(&offset, record + 20, 4);
        font->chars[i].pixeldata = raw + offset;
    }
    return font;
}
#endif

void load_font_tables(void)
{
    u32 len;
	s32 i;

#ifndef TARGET_N64
    { /* Build the font segment images from the typed asset arrays once
       * (until then the segments.c placeholders are zero and no text draws). */
        extern void portFontBuildSegments(void);
        static s32 built;

        if (!built) {
            built = 1;
            portFontBuildSegments();
        }
    }
#endif

    text_spacing = 0;
    text_orientation = 0;
    text_wordwrap = 0;
    overlap_correction = -1;
    text_bilevel_filter = 0;
    text_x = 0;
    text_y = 0;
    text_s = 0;
    text_t = 0;

#ifdef GE_NATIVE_64
    len = (u8 *)&_fontbankgothicSegmentEnd - (u8 *)&_fontbankgothicSegmentStart;
    ptrFontBankGothic = portFontLoadNative((const u8 *)&_fontbankgothicSegmentRomStart, len);
    ptrFontBankGothicChars = ptrFontBankGothic->chars;
    len = (u8 *)&_fontzurichboldSegmentEnd - (u8 *)&_fontzurichboldSegmentStart;
    ptrFontZurichBold = portFontLoadNative((const u8 *)&_fontzurichboldSegmentRomStart, len);
    ptrFontZurichBoldChars = ptrFontZurichBold->chars;
#else
    len = (romptr_t)&_fontbankgothicSegmentEnd - (romptr_t)&_fontbankgothicSegmentStart;
	ptrFontBankGothic = (struct font *)mempAllocBytesInBank(len, MEMPOOL_STAGE);
	ptrFontBankGothicChars = ptrFontBankGothic->chars;

	romCopy(ptrFontBankGothic, (void *) &_fontbankgothicSegmentRomStart, len);

    // Convert pointers
	for (i = 0; i < 94; i++) {
#ifdef TARGET_N64
		ptrFontBankGothicChars[i].pixeldata += (uintptr_t)ptrFontBankGothic;
#else
		/* stored value is a segment offset, not a pointer - relocate in
		 * integer domain (pointer arithmetic on the raw offset is UB) */
		ptrFontBankGothicChars[i].pixeldata = (u8 *)
			((uintptr_t)ptrFontBankGothicChars[i].pixeldata + (uintptr_t)ptrFontBankGothic);
#endif
	}

    len = (romptr_t)&_fontzurichboldSegmentEnd - (romptr_t)&_fontzurichboldSegmentStart;
	ptrFontZurichBold = (struct font *)mempAllocBytesInBank(len, MEMPOOL_STAGE);
	ptrFontZurichBoldChars = ptrFontZurichBold->chars;

	romCopy(ptrFontZurichBold, (void *) &_fontzurichboldSegmentRomStart, len);

    // Convert pointers
	for (i = 0; i < 94; i++) {
#ifdef TARGET_N64
		ptrFontZurichBoldChars[i].pixeldata += (uintptr_t)ptrFontZurichBold;
#else
		ptrFontZurichBoldChars[i].pixeldata = (u8 *)
			((uintptr_t)ptrFontZurichBoldChars[i].pixeldata + (uintptr_t)ptrFontZurichBold);
#endif
	}
#endif
#ifndef TARGET_N64
	portFontClearPadding(ptrFontBankGothicChars, 94);
	portFontClearPadding(ptrFontZurichBoldChars, 94);
#endif
}

Gfx *microcode_constructor(Gfx *gdl) //fontGfxSetup
{
	gDPPipeSync(gdl++);
	gDPSetCycleType(gdl++, G_CYC_1CYCLE);
	gDPSetColorDither(gdl++, G_CD_DISABLE);
	gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
	gDPSetCombineLERP(gdl++,
			0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0,
			0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
	gDPSetTexturePersp(gdl++, G_TP_NONE);
	gDPSetAlphaCompare(gdl++, G_AC_NONE);
	gDPSetTextureLOD(gdl++, G_TL_TILE);
	gDPSetTextureConvert(gdl++, G_TC_FILT);
	gDPSetTextureLUT(gdl++, G_TT_NONE);

	if (text_bilevel_filter) {
		gDPSetTextureFilter(gdl++, G_TF_AVERAGE);
	} else {
		gDPSetTextureFilter(gdl++, G_TF_BILERP);
	}

	return gdl;
}

Gfx *combiner_bayer_lod_perspective(Gfx *gdl)
{
	gDPPipeSync(gdl++);
	gDPSetColorDither(gdl++, G_CD_BAYER);
	gDPSetTexturePersp(gdl++, G_TP_PERSP);
	gDPSetTextureLOD(gdl++, G_TL_LOD);

	return gdl;
}

Gfx* draw_blackbox_to_screen(Gfx *glist, s32 *ulx, s32 *uly, s32 *lrx, s32 *lry)
{
    gDPSetRenderMode(glist++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(glist++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(glist++, 0, 0, 0x00, 0x00, 0x00, 0x00);
    gDPFillRectangle(glist++, *ulx, *uly, *lrx, *lry);
    gDPSetRenderMode(glist++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineLERP(glist++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    
    return glist;
}

Gfx * microcode_constructor_related_to_menus(Gfx *gdl, s32 ulx, s32 uly, s32 lrx, s32 lry, u32 color)
{
    u32 r = (u8)(color >> 0x18);
    u32 g = (u8)(color >> 0x10);
    u32 b = (u8)(color >> 0x8);
    u32 a = (u8)(color >> 0);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
    gDPFillRectangle(gdl++, ulx, uly, lrx, lry);
    gDPSetCombineLERP(gdl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    return gdl;
}

/**
 * Render a single character with no outline.
 */
#ifndef TARGET_N64
/* PORT: glyph scale (see textrelated.h), applied to the plain glyph path and
 * textMeasure; the glyph is loaded at full size and only the rect shrinks while
 * the texel step grows. */
static f32 port_text_scale = 1.0f;
/* Rare's glyphs are cropped tight to the ink, so the renderer grows tagged glyph
 * rects by half a texel with a transparent border to stop filtering clipping
 * them. Only glyphs may be tagged (a tagged G_NOOP, 0x47 in the top byte),
 * since tiled pictures would open seams. */
#define PORT_TEXT_GLYPHS(gdl, on) gDPNoOpTag((gdl)++, 0x47000000u | ((on) ? 1u : 0u))

void portTextScale(f32 scale)
{
    port_text_scale = (scale > 0.0f) ? scale : 1.0f;
}

/* Scaled letter spacing is rounded (gap to nearest pixel, advance rounded up)
 * rather than truncated, so scaled text keeps its gaps and never overlaps; both
 * are exact at 1.0. */
static s32 portTextScaledGap(s32 kerningOffsetMinus1)
{
    const f32 g = (f32) kerningOffsetMinus1 * port_text_scale;

    return (s32) ((g < 0.0f) ? g - 0.5f : g + 0.5f);
}

static s32 portTextScaledAdvance(s32 width)
{
    return ((s32) (width * 4 * port_text_scale) + 3) >> 2;
}
#endif

Gfx *textRenderGlyph(Gfx *gdl, s32 *x, s32 *y, struct fontchar *curchar, struct fontchar *prevchar,
		struct font *font, s32 clipX, s32 clipY, s32 clipWidth, s32 clipHeight, s32 yOffset)
{
    s32 stack;
    s32 stack2;
	s32 kerningOffset;
    s32 drawY;

	drawY = *y + yOffset;
	kerningOffset = font->kerning[prevchar->kerningindex * 13 + curchar->kerningindex] + text_spacing;
#ifndef TARGET_N64
    *x -= portTextScaledGap(kerningOffset - 1);
#else
    *x -= (kerningOffset - 1);
#endif

    if (text_orientation || (*x > 0 && *x <= viGetX() && drawY + curchar->baseline <= viGetY()))
    {
        if (clipX + clipWidth >= *x
				&& clipY + clipHeight >= curchar->baseline + drawY
				&& *x >= clipX
				&& curchar->baseline + drawY + curchar->height >= clipY)
        {
            if (curchar->index < 0x80)
            {
                gDPSetTextureLUT(gdl++, G_TT_NONE);
#ifndef TARGET_N64
                { /* GE_GLYPHDUMP=1: dump the raw rows (padding included, " | "
                   * marks the glyph width) of the first few distinct glyphs. */
                    extern char *getenv(const char *);
                    extern int printf(const char *, ...);
                    static s32 on = -1;
                    static u32 seen[8];
                    static s32 n;

                    static s32 only = -1;   /* GE_GLYPHDUMP=<index above 1>: that glyph alone */

                    if (on < 0)
                    {
                        const char *e = getenv("GE_GLYPHDUMP");
                        extern int atoi(const char *);

                        on = e != NULL;
                        only = (e != NULL && atoi(e) > 1) ? atoi(e) : -1;
                    }
                    if (on && n < 24 && curchar->index >= 0 && curchar->index < 256
                        && (only < 0 || curchar->index == only)
                        && !(seen[curchar->index >> 5] & (1u << (curchar->index & 31))))
                    {
                        const s32 stride = (curchar->width + 7) & 0xF8;
                        s32 r, c;

                        seen[curchar->index >> 5] |= 1u << (curchar->index & 31);
                        n++;
                        printf("ge: glyph %d w=%d h=%d stride=%d\n", (int) curchar->index, (int) curchar->width,
                               (int) curchar->height, (int) stride);
                        for (r = 0; r < curchar->height; r++)
                        {
                            printf("ge: glyph   ");
                            for (c = 0; c < stride; c++)
                            {
                                printf("%02x%s", curchar->pixeldata[r * stride + c], (c == curchar->width - 1) ? " | " : " ");
                            }
                            printf("\n");
                        }
                    }
                }
#endif

                /*
                gDPSetTextureImage(gdl++, G_IM_FMT_I, G_IM_SIZ_16b, 1, curchar->pixeldata);
                gDPSetTile(gdl++, G_IM_FMT_I, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                gDPLoadSync(gdl++);
                gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, (curchar->unkc * curchar->height), 0);
                gDPPipeSync(gdl++);                
                gDPSetTile(gdl++,G_IM_FMT_I,G_IM_SIZ_8b,0,0,G_TX_RENDERTILE,0,G_TX_NOMIRROR | G_TX_WRAP,G_TX_NOMASK,G_TX_NOLOD,G_TX_NOMIRROR | G_TX_WRAP,G_TX_NOMASK,G_TX_NOLOD);
                gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, (curchar->unkc - 1) << 2, (curchar->height - 1) << 2);
                */
                gDPLoadTextureBlock(gdl++, curchar->pixeldata, G_IM_FMT_I, G_IM_SIZ_8b, ((curchar->width + 7) & 0xF8), curchar->height, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, 0, 0);
            }
            else
            {
                gDPPipeSync(gdl++);
                gDPSetTextureLUT(gdl++, G_TT_IA16);

                if (g_JpnTextTlutNeedsLoad)
                {
                    g_JpnTextTlutNeedsLoad = 0;
                    
                    // FD100000 --------: gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, osVirtualToPhysical(D_80040EBC));
                    // E8000000 00000000: gDPTileSync(gdl++);
                    // F5000100 07000000: gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 0x0100, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                    // E6000000 00000000: gDPLoadSync(gdl++);
                    // F0000000 0703C000: gDPLoadTLUTCmd(gdl++, G_TX_LOADTILE, 15);
                    // E7000000 00000000: gDPPipeSync(gdl++);
                    gDPLoadTLUT_pal16(gdl++, 0, osVirtualToPhysical(g_JpnTextPalette0));

                    gDPLoadTLUT_pal16(gdl++, 1, osVirtualToPhysical(&g_JpnTextPalette1));
                }

                // FD500000 00000000: gDPSetTextureImage(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 1, 0x00000000);
                // F5500000 07000000: gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                // E6000000 00000000: gDPLoadSync(gdl++);
                // F3000000 07000000: gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, 0, 0);
                // E7000000 00000000: gDPPipeSync(gdl++);
                // F5400200 00080000: gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_4b, 1, 0x0000, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                // F2000000 0003C000: gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, qu102(15), 0);
                //
                // is this a call to gDPLoadTextureBlock ???
                
                gDPSetTextureImage(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 1,  osVirtualToPhysical((void *) curchar->pixeldata));
                gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
                gDPLoadSync(gdl++);
                gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, (curchar->height << 2) - 1, 0x800);
                gDPPipeSync(gdl++);
                gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_4b, 1, 0x0000, G_TX_RENDERTILE, (curchar->index & 1), G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD, 2, 0, 0);
                gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, 0x3c, (curchar->height - 1) << 2);
            }

            if ((*x + curchar->width) <= clipX + clipWidth)
            {
				if (clipY <= curchar->baseline + drawY)
                {
					if (curchar->baseline + drawY + curchar->height <= clipY + clipHeight)
                    {
                        if (text_orientation)
                        {
                            // E5000000 --------:
                            // B4000000 --------:
                            // B3000000 0400FC00:
                            gSPTextureRectangleFlip(gdl++,
                                /* xl */ (((drawY - curchar->baseline) - curchar->height) * 4) + text_y,
                                /* yl */ ((*x * 4) + text_x),
                                /* xh */ ((drawY - curchar->baseline) * 4) + text_y,
                                /* yh */ ((*x + curchar->width) * 4) + text_x ,
                                /* tile */  G_TX_RENDERTILE,
                                /* s */ text_t,
                                /* t */ ((curchar->height - 1) << 5) + text_s,
                                /* dsdx */ 0x400,
                                /* dsdy */ 0xfc00);
                        }
                        else
                        {
                            // E4000000 --------:
                            // B4000000 --------:
                            // B3000000 04000400:
#ifndef TARGET_N64
                            if (port_text_scale != 1.0f)
                            {   /* PORT: scaled glyph - the rect shrinks, the texel step grows */
                                s32 sw = (s32) (curchar->width * 4 * port_text_scale);
                                s32 sb = (s32) (curchar->baseline * 4 * port_text_scale);
                                s32 sh = (s32) (curchar->height * 4 * port_text_scale);
                                s32 step = (s32) (0x400 / port_text_scale);
                                gSPTextureRectangle(gdl++,
                                    (*x * 4) + text_x,
                                    (drawY * 4) + sb + text_y,
                                    (*x * 4) + sw + text_x,
                                    (drawY * 4) + sb + sh + text_y,
                                    G_TX_RENDERTILE, text_s, text_t, step, step);
                            }
                            else
#endif
                            gSPTextureRectangle(gdl++,
                                /* xl */ ((*x * 4) + text_x),
                                /* yl */ ((drawY + curchar->baseline) * 4) + text_y,
                                /* xh */ ((*x + curchar->width) * 4) + text_x ,
                                /* yh */ ((curchar->baseline + drawY + curchar->height) * 4) + text_y,
                                /* tile */  G_TX_RENDERTILE,
                                /* s */ text_s,
                                /* t */ text_t,
                                /* dsdx */ 0x400,
                                /* dsdy */ 0x400);
                        }

                    }
                    else if (curchar->baseline + drawY <= clipY + clipHeight)
                    {
                        // E4000000 --------:
                        // B4000000 --------:
                        // B3000000 04000400:
                        gSPTextureRectangle(gdl++,
                            /* xl */ ((*x * 4) + text_x),
                            /* yl */ ((drawY + curchar->baseline) * 4) + text_y,
                            /* xh */ ((*x + curchar->width) * 4) + text_x ,
                            /* yh */ clipY + clipHeight + text_y,
                            /* tile */  G_TX_RENDERTILE,
                            /* s */ text_s,
                            /* t */ text_t,
                            /* dsdx */ 0x400,
                            /* dsdy */ 0x400);
                    }
                }
                else
                {
                    if (curchar->baseline + drawY + curchar->height >= clipY)
                    {
                        // E4000000 --------:
                        // B4000000 --------:
                        // B3000000 04000400:
                        gSPTextureRectangle(gdl++,
                            /* xl */ ((*x * 4) + text_x),
                            /* yl */ (clipY * 4) + text_y,
                            /* xh */ ((*x + curchar->width) * 4) + text_x ,
                            /* yh */ ((curchar->baseline + drawY + curchar->height) * 4) + text_y,
                            /* tile */  G_TX_RENDERTILE,
                            /* s */ text_s,
                            /* t */ (((clipY - drawY) - curchar->baseline) << 5) + text_t,
                            /* dsdx */ 0x400,
                            /* dsdy */ 0x400);
                    }
                }
            }
        }
    }
    
#ifndef TARGET_N64
    *x += portTextScaledAdvance(curchar->width);
#else
    *x += curchar->width;
#endif
    return gdl;
}

/**
 * Render plain text with no outline e.g. briefings and watch menu text.
 */
Gfx *textRender(Gfx *gdl, s32 *x, s32 *y, char *text,
                struct fontchar *chars, struct font *font, u32 colour,
                s32 width, s32 height, u32 yOffset, s32 lineheight)
{
    s32 savedx;
	s32 savedy;
    s32 prevchar;
    s32 stack = 1;
    s32 stack2;
    s32 stack3;
    s32 stack4;
    //s32 stack5;
    
    g_JpnTextTlutNeedsLoad = 1;
#ifndef TARGET_N64
    PORT_TEXT_GLYPHS(gdl, 1);   /* every texrect until the matching 0 is a font glyph */
#endif

    savedx = *x;
	savedy = *y;
    prevchar = 'H';

	if (lineheight == 0) {
		lineheight = chars['['].height + chars['['].baseline;
	}

	if (j_text_trigger != 0 && lineheight < 14) {
		lineheight = 14;
	}

    stack = colour; // ????????????
    gDPSetPrimColor(gdl++, 0, 0, M_COLOR_R(colour), M_COLOR_G(colour), M_COLOR_B(colour), M_COLOR_A(colour));
    
    while (*text != '\0')
    {
		if (*text == ' '
#ifndef TARGET_N64
		    || (*text < 0x21 && *text != '\n') || *text == 0x7F /* no glyph for this char (chars[] is
		                                                         * 0x21..0x7E): draw a space, never index
		                                                         * outside the table */
#endif
		    ) {
#ifndef TARGET_N64
			*x += (s32) (SPACE_WIDTH * port_text_scale); /* PORT: the space follows portTextScale */
#else
			*x += 5;
#endif
			prevchar = 'H';
			text++;
		} else if (*text == '\n') {
			prevchar = 'H';

            if ((overlap_correction >= 0) && (savedx == *x))
            {
                *y += overlap_correction;
            }
            else
            {
                *y += lineheight;
            }
            
			text++;
			*x = savedx;
		} else if (*text < 0x80) {
			gdl = textRenderGlyph(gdl, x, y, &chars[*text - 0x21], &chars[prevchar - 0x21], font, savedx, savedy, width, height, yOffset);
			prevchar = *text;
			text++;
		} else {
#ifndef TARGET_N64
			/* High-bit bytes are the two-byte JPN glyph codes. In English the
			 * glyph cache is not allocated (langInit), so skip them as gaps; in
			 * Japanese ([data] language=jp) take Rare's path below. */
			if (!j_text_trigger) {
				text += 2;
				if (text[-1] == '\0') {
					break; /* garbled trailing byte: don't walk past the NUL */
				}
				continue;
			}
			if (text[1] == '\0') {
				break; /* garbled trailing byte */
			}
#endif
		  {
			u16 codepoint = ((*text & 0x7f) << 7) | (text[1] & 0x7f);
			struct fontchar sp74 = g_JpnGlyphDefaults;

			if (codepoint & 0x2000) {
				sp74.width = 15;
				sp74.height = 16;
			}

            // perfect dark: 0x3c8
#if defined(VERSION_EU) || defined(VERSION_JP)
            if ((codepoint & 0x1fff) >= 0x3c8)
#else
			if ((codepoint & 0x1fff) >= 0x3c7)
#endif
            {
				codepoint = 2;
			}

			sp74.index = codepoint + 0x80;
			sp74.pixeldata = (void *)langGetJpnCharPixels(codepoint);

			gdl = textRenderGlyph(gdl, x, y, &sp74, &sp74, font, savedx, savedy, width, height, yOffset);

			text += 2;
		  }
		}
	}
#ifndef TARGET_N64
    PORT_TEXT_GLYPHS(gdl, 0);
#endif
    return gdl;
}

/**
 * Draw individual characters with an outline.
 */
Gfx *textRenderGlyphOutlined(Gfx *gdl, s32 *x, s32 *y, struct fontchar *curchar, 
                       /*sp70*/ struct fontchar *prevchar, struct font *font, s32 clipX, s32 clipY, 
                       /*sp80*/u32 textColour, u32 outlineColour, s32 clipWidth, s32 clipHeight, 
                       /*sp90*/ s32 yOffset)
{
    s32 offsetX;
    s32 offsetY;
    s32 kerningOffset;
    s32 drawY;
    s32 tmpx;
    s32 tmpy;
    u32 rgba;

    drawY = *y + yOffset;

    kerningOffset = font->kerning[prevchar->kerningindex * 13 + curchar->kerningindex] + text_spacing;
    *x -= (kerningOffset - 1);

    if (*x > 0
            && *x <= viGetX()
            && drawY + curchar->baseline <= viGetY()
            && *x <= clipX + clipWidth
            && drawY + curchar->baseline <= clipY + clipHeight
            && *x >= clipX
            && drawY + curchar->baseline + curchar->height >= clipY) {
        if (curchar->index < 0x80) {
            gDPSetTextureLUT(gdl++, G_TT_NONE);
            gDPLoadTextureBlock(gdl++, curchar->pixeldata, G_IM_FMT_I, G_IM_SIZ_8b, ((curchar->width + 7) & 0xF8), curchar->height, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, 0, 0);
        } else {
            gDPPipeSync(gdl++);
            gDPSetTextureLUT(gdl++, G_TT_IA16);

            if (g_JpnTextTlutNeedsLoad) {
                g_JpnTextTlutNeedsLoad = 0;

                gDPLoadTLUT_pal16(gdl++, 0, osVirtualToPhysical(&g_JpnTextPalette0));
                gDPLoadTLUT_pal16(gdl++, 1, osVirtualToPhysical(&g_JpnTextPalette1));
            }

            gDPSetTextureImage(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 1,  osVirtualToPhysical((void *) curchar->pixeldata));
            gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
            gDPLoadSync(gdl++);
            gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, (curchar->height << 2) - 1, 0x800);
            gDPPipeSync(gdl++);
            gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_4b, 1, 0x0000, G_TX_RENDERTILE, (curchar->index & 1), G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD, 2, 0, 0);
            gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, 0x3c, (curchar->height - 1) << 2);
        }

        /**
         * Render outlines.
         * This is done by rendering the character 8 times in diffent offsets around the origin:
         * left/right/up/down and the four diagonals.
         */
        rgba = outlineColour;
        gDPSetPrimColor(gdl++, 0, 0, M_COLOR_R(rgba), M_COLOR_G(rgba), M_COLOR_B(rgba), M_COLOR_A(rgba));

        for (offsetX = -1; offsetX < 2; offsetX++) {
            for (offsetY = -1; offsetY < 2; offsetY++) {
                tmpx = *x + offsetX;
                tmpy = drawY + offsetY;

                if (offsetX != 0 || offsetY != 0) { // Skip the center since that will be covered by the main character anyway.
                    gdl = textDrawGlyphQuad(gdl, tmpx, tmpy, curchar, clipX, clipY, clipWidth, clipHeight);
                }
            }
        }

        // Render main character.
        tmpx = *x;
        tmpy = drawY;
        rgba = textColour;
        gDPSetPrimColor(gdl++, 0, 0, M_COLOR_R(rgba), M_COLOR_G(rgba), M_COLOR_B(rgba), M_COLOR_A(rgba));
        gdl = textDrawGlyphQuad(gdl, tmpx, tmpy, curchar, clipX, clipY, clipWidth, clipHeight);
    }

    *x += curchar->width;

    return gdl;
}

/**
 * Draw a quad for the current glyph, clipping it inside the region defined by clipX, clipY, clipWidth, and clipHeight.
 */
Gfx *textDrawGlyphQuad(Gfx *gdl, s32 x, s32 y, struct fontchar *curchar, s32 clipX, s32 clipY, s32 clipWidth, s32 clipHeight)
{
    if (clipX + clipWidth >= curchar->width + x) // Check if the glyph's right edge is inside or touching the clipping rect's right edge.
    {
        if (curchar->baseline + y >= clipY) // Check if the glyph's top is below the clip top.
        {
            if (clipY + clipHeight >= curchar->baseline + y + curchar->height) // Check if the glyph's bottom is above the clip bottom.
            {
                if (text_orientation) // Check if vertical text. Not sure if ever used for outlined text?
                {
                    gSPTextureRectangleFlip(gdl++, 
                        /* xl */ ((y - curchar->baseline) - curchar->height) << 2, 
                        /* yl */ x << 2, 
                        /* xh */ (y - curchar->baseline) << 2, 
                        /* yh */ (x + curchar->width) << 2, 
                        /* tile */ G_TX_RENDERTILE, 
                        /* s */ 0, 
                        /* t */ (curchar->height - 1) << 5, 
                        /* dsdx */ 0x400, 
                        /* dsdy */ 0xFC00);
                }
                else
                {
                    gSPTextureRectangle(gdl++, 
                        /* xl */ x << 2, 
                        /* yl */ (y + curchar->baseline) << 2, 
                        /* xh */ (x + curchar->width << 2), 
                        /* yh */ (curchar->baseline + y + curchar->height) << 2, 
                        /* tile */ G_TX_RENDERTILE, 
                        /* s */ 0, 
                        /* t */ 0, 
                        /* dsdx */ 0x400, 
                        /* dsdy */ 0x400);
                }
            }
            else if (clipY + clipHeight >= curchar->baseline + y) // Glyph bottom is below the clipping rectangle.
            {
                // Bottom coordinates clipped to savedy + height.
                gSPTextureRectangle(gdl++, 
                    /* xl */ x << 2, 
                    /* yl */ (y + curchar->baseline) << 2, 
                    /* xh */ (x + curchar->width) << 2, 
                    /* yh */ (clipY + clipHeight << 2), 
                    /* tile */ G_TX_RENDERTILE, 
                    /* s */ 0, 
                    /* t */ 0, 
                    /* dsdx */ 0x400, 
                    /* dsdy */ 0x400); 
            }
        }
        else if (curchar->baseline + y + curchar->height >= clipY) // Glyph top is above the clipping rectangle.
        {
            gSPTextureRectangle(gdl++, // Top clipped to fit.
                /* xl */ x << 2, 
                /* yl */ clipY<< 2, 
                /* xh */ (x + curchar->width) << 2, 
                /* yh */ (curchar->baseline + y + curchar->height) << 2, 
                /* tile */ G_TX_RENDERTILE, 
                /* s */ 0, 
                /* t */ ((clipY - curchar->baseline) - y) << 5, 
                /* dsdx */ 0x400, 
                /* dsdy */ 0x400);
        }
    }

    return gdl;
}

/**
 * Draw strings of text with an outline.
 * Used for ammo counter, bottom left HUD messages, countdown timers.
 * Also used for rendering outlines on folder menu text in JP version.
 */
Gfx *textRenderOutlined(Gfx *gdl, s32 *x, s32 *y, 
                    char *text, struct fontchar *chars, struct font *font, 
                    u32 colour, u32 colour2, s32 width, s32 height, 
                    s32 yOffset, s32 lineheight)
{
    u16 codepoint;
	s32 savedy;
    s32 savedx;
    struct fontchar sp74;
    s32 prevchar;
    
    g_JpnTextTlutNeedsLoad = 1;
#ifndef TARGET_N64
    PORT_TEXT_GLYPHS(gdl, 1);   /* every texrect until the matching 0 is a font glyph */
#endif

    savedx = *x;
	savedy = *y;
    prevchar = 'H';

	if (lineheight == 0) {
		lineheight = chars['['].height + chars['['].baseline;
	}

	if (j_text_trigger != 0 && lineheight < 14) {
		lineheight = 14;
	}
    
    while (*text != '\0')
    {
		if (*text == ' '
#ifndef TARGET_N64
		    || (*text < 0x21 && *text != '\n') || *text == 0x7F /* no glyph for this char (chars[] is
		                                                         * 0x21..0x7E): draw a space, never index
		                                                         * outside the table */
#endif
		    ) {
			*x += SPACE_WIDTH;
			prevchar = 'H';
			text++;
		} else if (*text == '\n') {
			prevchar = 'H';
			*x = savedx;
            *y += lineheight;
			text++;
		} else if (*text < 0x80) {
            // Render individual characters with outline.
            gdl = textRenderGlyphOutlined(gdl, x, y, &chars[*text - 0x21], &chars[prevchar - 0x21], font, savedx, savedy, colour, colour2, width, height, yOffset);
			prevchar = *text;
			text++;
		} else {
#ifndef TARGET_N64
			/* see the non-outlined renderer: English skips two-byte codes,
			 * Japanese takes Rare's path */
			if (!j_text_trigger) {
				text += 2;
				if (text[-1] == '\0') {
					break;
				}
				continue;
			}
			if (text[1] == '\0') {
				break;
			}
#endif
            codepoint = ((*text & 0x7f) << 7) | (text[1] & 0x7f);
            sp74 = g_JpnGlyphDefaultsOutlined;

			if (codepoint & 0x2000) {
				sp74.width = 15;
				sp74.height = 16;
			}

#if defined(VERSION_EU) || defined(VERSION_JP)
            if ((codepoint & 0x1fff) >= 0x3c8)
#else
			if ((codepoint & 0x1fff) >= 0x3c7)
#endif
            {
				codepoint = 2;
			}

			sp74.index = codepoint + 0x80;
			sp74.pixeldata = (void *)langGetJpnCharPixels(codepoint);

            // Render Japanese characters with outline.
            gdl = textRenderGlyphOutlined(gdl, x, y, &sp74, &sp74, font, savedx, savedy, colour, colour2, width, height, yOffset);

			text += 2;
		}
	}

#ifndef TARGET_N64
    PORT_TEXT_GLYPHS(gdl, 0);
#endif
    return gdl;
}

void textMeasure(s32 *textheight, s32 *textwidth, char *text, struct fontchar *font1, struct font *font2, s32 lineheight)
{
    char prevchar;
	s32 longest;
	s32 tmp;

	prevchar = 'H';
    tmp = 0;
	longest = 0;
	*textheight = 0;
	*textwidth = 0;

    if (lineheight == 0)
    {
		lineheight = font1['['].baseline + font1['['].height;
    }
    
    if ((j_text_trigger) && (lineheight < 14))
    {
        lineheight = 14;
    }

    while (*text != '\0')
    {
        if (*text == ' '
#ifndef TARGET_N64
            || (*text < 0x21 && *text != '\n') || *text == 0x7F /* no glyph for this char (chars[] is
                                                                 * 0x21..0x7E): draw a space, never index
                                                                 * outside the table */
#endif
            )
        {
            // Space
            if (text[1] != '\n') {
#ifndef TARGET_N64
                *textwidth += (s32) (SPACE_WIDTH * port_text_scale); /* matches what textRender adds */
#else
                *textwidth += SPACE_WIDTH;
#endif
            }

            prevchar = 'H';
            text++;
        }
        else if (*text == '\n')
        {
            // Line break
            if (*textwidth > longest) {
                longest = *textwidth;
            }

            *textwidth = 0;
            *textheight += lineheight;
            text++;
        }
        else if (*text < 0x80)
        {
            // Normal single-byte character
            tmp = font2->kerning[font1[prevchar - 0x21].kerningindex * 13 + font1[*text - 0x21].kerningindex] + text_spacing - 1;
#ifndef TARGET_N64
            /* the same rounded terms textRenderGlyph moves the pen by */
            *textwidth = portTextScaledAdvance(font1[*text - 0x21].width) + *textwidth - portTextScaledGap(tmp);
#else
            *textwidth = font1[*text - 0x21].width + *textwidth - tmp;
#endif

            prevchar = *text;
            text++;
        }
        else if (*text < 0xC0)
        {
            // Multi-byte character
            tmp = font2->kerning[0] + text_spacing - 1;
            *textwidth = *textwidth - tmp + 11;
            text += 2;
        }
        else
        {
            // Multi-byte character
            tmp = font2->kerning[0] + text_spacing - 1;
            *textwidth = *textwidth - tmp + 15;
            text += 2;
        }
    };
    
    if (*textwidth < longest)
    {
        *textwidth = longest;
    }
#ifndef TARGET_N64
    if (port_text_scale != 1.0f)
    {   /* the width is summed in scaled pixels above */
        *textheight = (s32) (*textheight * port_text_scale);
    }
#endif
}

void textWrap(s32 wrapwidth, char *src, char *dst, struct fontchar *chars, struct font *font)
{
	s32 curlinewidth = 0;
	bool itfits;
	s32 wordlen;
	s32 wordwidth;
	s32 wordheight = 0;
	s32 more = 1;
	s32 v1;
	s32 i;
	u32 stack;
	char curword[32];

    while (more == 1) {
		// Load the next word
		wordwidth = 0;
		wordlen = 0;
		v1 = 0;

        // Read one word into the curword buffer.
        while (*src > ' ')
        {
            curword[wordlen] = *src;
			v1 += chars[*src - 0x21].width;
			src++;
			wordlen++;

            if (curword[wordlen - 1] >= 0x80)
            {
                curword[wordlen] = *src;
                v1 += chars[*src - 0x21].width;
                src++;
                wordlen++;
            }
        }
        
        // Null terminate the word
        curword[wordlen] = '\0';

        textMeasure(&wordheight, &wordwidth, curword, chars, font, 0);

        // Track the current line's pixel length
        curlinewidth += wordwidth;

        if (curlinewidth <= wrapwidth) {
            itfits = 1;
        } else {
            itfits = 0;
        }

        if (*src == '\n')
        {
            // Write a new line and indent
			if (!itfits) {
				*dst = '\n';
				dst++;

				for (i = 0; i < text_wordwrap; i++) {
					*dst = ' ';
					dst++;
				}
			}

			curlinewidth = 0;

			// Write the current word
			for (i = 0; i < wordlen; i++) {
				*dst = curword[i];
				dst++;
			}

			// Write the original new line that was in src
			*dst = '\n';
			dst++;
        }
        else if (*src == ' ')
        {
            if (!itfits)
            {
				// Write a new line and indent
				*dst = '\n';
				dst++;

				for (i = 0; i < text_wordwrap; i++)
                {
					*dst = ' ';
					dst++;
				}

				curlinewidth = text_wordwrap * SPACE_WIDTH + wordwidth;
			}

			curlinewidth += SPACE_WIDTH;

			// Write the current word
			for (i = 0; i < wordlen; i++) {
				*dst = curword[i];
				dst++;
			}

			// Write the trailing space
			*dst = ' ';
			dst++;
        }
        else if (*src == '\0')
        {
            more = 0;

			if (!itfits) {
				// Write a new line and indent
				*dst = '\n';
				dst++;

				for (i = 0; i < text_wordwrap; i++) {
					*dst = ' ';
					dst++;
				}
			}

			// Write the current word
			for (i = 0; i < wordlen; i++) {
				*dst = curword[i];
				dst++;
			}

			// Write the null terminator
			*dst = '\0';
        }

        src++;
    }
}

// Unreferenced
void sub_GAME_7F0AEF0C(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {

}

// Unreferenced
u32 sub_GAME_7F0AEF20(u32 param_1,u32 param_2){
  return param_1;
}


