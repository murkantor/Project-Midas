#ifndef _TEXTRELATED_H_
#define _TEXTRELATED_H_
#include <ultra64.h>
#include "bondtypes.h"

enum TEXT_ORIENTATION {
    ROT_NORMAL = 0,
    ROT_90CW
};

extern struct font * ptrFontBankGothic;
extern struct fontchar * ptrFontBankGothicChars;
extern struct font * ptrFontZurichBold;
extern struct fontchar * ptrFontZurichBoldChars;

void textInit(void);
void load_font_tables(void);


Gfx * microcode_constructor_related_to_menus(Gfx *gdl, s32 ulx, s32 uly, s32 lrx, s32 lry, u32 color);
void textMeasure(s32 *textheight, s32 *textwidth, char *text, struct fontchar *font1, struct font *font2, s32 lineheight);
#ifndef TARGET_N64
/* PORT: glyph scale for following plain textRender / textMeasure calls (1.0 = vanilla).
 * It is a global, so always reset it to 1.0f right after drawing (see
 * docs/port/files-screen-buttons.md). */
void portTextScale(f32 scale);
#endif

Gfx *microcode_constructor(Gfx *gdl);
Gfx *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars, struct font *font, u32 colour, s32 width, s32 height, u32 yOffset, s32 lineheight);
Gfx *textRenderOutlined(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars, struct font *font, u32 colour, u32 colour2, s32 width, s32 height, s32 yOffset, s32 lineheight);

Gfx *combiner_bayer_lod_perspective(Gfx *gdl);
void setTextSpacingInverted(s32 spacing);
void setTextWordWrap(s32 flag);
void textWrap(s32 wrapwidth, char *src, char *dst, struct fontchar *chars, struct font *font);
void setTextOverlapCorrection(s32 flag);

Gfx* draw_blackbox_to_screen(Gfx *glist, s32 *ulx, s32 *uly, s32 *lrx, s32 *lry);


void setTextOrientation(s32 orientation);

#endif
