#include <ultra64.h>
void display_image_at_position(Gfx **gdlptr, f32 *position, f32 *size, s32 twidth, u32 theight, u32 flipXY, u32 flipX, u32 flipY, u32 r, u32 g, u32 b, u32 alpha, u32 mipmapped_texture, u32 highlight_mode);

/**
 * Draws a textured rectangle on the screen.
 * 
 * @param gdlptr    Pointer to the display list.
 * @param position  Position coordinates (center) of the rectangle.
 * @param size      Size (half-width and half-height) of the rectangle.
 * @param width     Texture width.
 * @param height    Texture height.
 * @param flipXY    Flip X and Y flag.
 * @param flipX     Flip X-axis flag.
 * @param flipY     Flip Y-axis flag.
 */
void draw_textured_rectangle(Gfx **gdlptr, f32 *position, f32 *size, s32 width, s32 height, s32 flipXY, s32 flipX, s32 flipY)
{
    // Only proceed if the size values are positive
    if (size[0] > 0.0f && size[1] > 0.0f)
    {
        Gfx *gdl = *gdlptr;
        s32 xl;
        s32 yl;
        s32 xh;
        s32 yh;
        s32 s = 0;
        s32 t = 0;
        s32 dsdx;
        s32 dtdy;

        // Disable texture perspective correction
        gDPSetTexturePersp(gdl++, G_TP_NONE);

        // Compute rectangle coordinates
        xl = (position[0] - size[0]) * 4.0f;
        yl = (position[1] - size[1]) * 4.0f;
        xh = (position[0] + size[0]) * 4.0f;
        yh = (position[1] + size[1]) * 4.0f;

        // Proceed if the rectangle is within screen bounds
        if (xh >= 0 && yh >= 0)
        {
            // Handle X coordinate adjustment
            if (xl < 0)
            {
                if (flipXY)
                {
                    t += ((-xl * height) << 5) / (xh - xl);
                }
                else
                {
                    s += ((-xl * width) << 5) / (xh - xl);
                }

                xl = 0;
            }

            // Handle Y coordinate adjustment
            if (yl < 0)
            {
                if (flipXY)
                {
                    s += ((-yl * width) << 5) / (yh - yl);
                }
                else
                {
                    t += ((-yl * height) << 5) / (yh - yl);
                }

                yl = 0;
            }

            // Calculate texture scaling based on flipXY flag
            if (flipXY)
            {
                dsdx = width / (2.0f * size[1]) * 1024.0f;
                dtdy = height / (2.0f * size[0]) * 1024.0f;
            }
            else
            {
                dsdx = width / (2.0f * size[0]) * 1024.0f;
                dtdy = height / (2.0f * size[1]) * 1024.0f;
            }
            
            // Flip texture horizontally if needed
            if (flipX)
            {
#ifdef TARGET_N64
                dsdx = 0x10000 - dsdx;
                s = ((width - 1) << 5) - s;
#else
                /* PORT: start the flip at the full dim<<5 rather than
                 * (dim-1)<<5, so under the port's bilinear sampling the
                 * coordinate runs dim..0 and every row lands on a texel centre.
                 * The old start clamped the last row and dropped the top one
                 * (the mine icon drew one pixel too long). */
                dsdx = 0x10000 - dsdx;
                s = (width << 5) - s;
#endif
            }

            // Flip texture vertically if needed
            if (flipY)
            {
#ifdef TARGET_N64
                dtdy = 0x10000 - dtdy;
                t = ((height - 1) << 5) - t;
#else
                dtdy = 0x10000 - dtdy;
                t = (height << 5) - t;   /* see flipX note */
#endif
            }

            // Draw the textured rectangle with optional flipping
            if (flipXY)
            {
                gSPTextureRectangleFlip(gdl++, xl, yl, xh, yh, G_TX_RENDERTILE, s, t, dsdx, dtdy);
            }
            else
            {
                gSPTextureRectangle(gdl++, xl, yl, xh, yh, G_TX_RENDERTILE, s, t, dsdx, dtdy);
            }
        }

        // Re-enable texture perspective correction
        gDPSetTexturePersp(gdl++, G_TP_PERSP);

        *gdlptr = gdl;
    }
}

#define G_CC_MODULATEIA_ENV        COMBINED, 0, ENVIRONMENT, 0, COMBINED, 0, ENVIRONMENT, 0

/**
 * Displays an image at a specific on-screen position with color tinting and optional texture flips.
 * 
 * @param gdl       Pointer to the display list.
 * @param position  Position of the image.
 * @param size      Size (half-width and half-height) of the image.
 * @param twidth    Texture width.
 * @param theight   Texture height.
 * @param flipXY    Flip X and Y flag.
 * @param flipX     Flip X-axis flag.
 * @param flipY     Flip Y-axis flag.
 * @param r         Red tint value.
 * @param g         Green tint value.
 * @param b         Blue tint value.
 * @param alpha     Alpha (transparency) value.
 * @param mipmapped_texture  Mipmapped rendering mode flag.
 * @param highlight_mode     Highlight rendering mode flag.
 */
#ifndef TARGET_N64
/**
 * CROSSHAIR COLOUR (midas.ini [display] crosshair_color). This is the one place
 * the crosshair sprite is drawn for all three users: the aim sight (gunfire.c),
 * the frontend cursor (front.c frontDrawCursor) and the pause-watch mouse
 * cursor (options.c).
 *
 * With no colour set this is exactly the vanilla display_image_at_position
 * call, so the default look is unchanged.
 *
 * With a colour set, a plain tint cannot work: the sprite is red in the texture
 * and every combiner multiplies the texel, so green times red is black. The
 * sprite's shape is in its alpha channel, so take the colour flat from the
 * environment register and the shape from the texel alpha (losing the faint
 * white core).
 */
void portDisplayCrosshairImage(Gfx **gdlptr, f32 *position, f32 *size, s32 twidth, s32 theight,
                               u32 alpha, u32 mipmapped_texture)
{
    { /* GE_XHAIRDBG: checks whether a blurry crosshair is really this sprite by
       * printing whenever the mip flag (which picks the TRILERP combiner) or
       * the size changes. */
        extern char *getenv(const char *);
        extern int printf(const char *, ...);
        static s32 dbg = -1;
        static s32 lastmip = -1;
        static s32 lastw = -1;

        if (dbg < 0)
        {
            const char *e = getenv("GE_XHAIRDBG");
            dbg = (e != NULL && e[0] != '0');
        }
        if (dbg && ((s32) mipmapped_texture != lastmip || (s32) size[0] != lastw))
        {
            lastmip = (s32) mipmapped_texture;
            lastw = (s32) size[0];
            printf("ge: xhairdbg: SPRITE mip=%d halfw=%d halfh=%d alpha=%u\n",
                   (int) mipmapped_texture, (int) size[0], (int) size[1], (unsigned) alpha);
        }
    }
    extern s32 portCrosshairColor(void); /* video.c; weak -1 headless */
    extern s32 portTexCrosshairTinted(void); /* port_textures.c */
    s32 c = portCrosshairColor();

    /* port_textures.c recolours the sprite's texels (colour times the red part,
     * plus the white core), so a set colour normally draws the vanilla way with
     * shading kept. The flat draw below is only the fallback when the texels
     * could not be tinted. */
    if (c < 0 || portTexCrosshairTinted())
    {
        display_image_at_position(gdlptr, position, size, twidth, theight, 0, 0, 1,
                                  0xFF, 0xFF, 0xFF, alpha, mipmapped_texture, 0);
        return;
    }
    if (size[0] > 0.0f && size[1] > 0.0f)
    {
        Gfx *gdl = *gdlptr;

        gDPSetEnvColor(gdl++, (u8) ((c >> 16) & 0xFF), (u8) ((c >> 8) & 0xFF),
                       (u8) (c & 0xFF), (u8) alpha);
        gDPSetCombineLERP(gdl++, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, ENVIRONMENT, 0,
                                 0, 0, 0, ENVIRONMENT, TEXEL0, 0, ENVIRONMENT, 0);
        *gdlptr = gdl;
        draw_textured_rectangle(gdlptr, position, size, twidth, theight, 0, 0, 1);
    }
}
#endif

void display_image_at_position(Gfx **gdlptr, f32 *position, f32 *size, s32 twidth, u32 theight, u32 flipXY, u32 flipX, u32 flipY, u32 r, u32 g, u32 b, u32 alpha, u32 mipmapped_texture, u32 highlight_mode)
{
    
    if (size[0] > 0.0f && size[1] > 0.0f)
    {
        Gfx *gdl = *gdlptr;

        gDPSetEnvColor(gdl++, r, g, b, alpha);

        if (mipmapped_texture)
        {
            gDPSetCombineMode(gdl++, G_CC_TRILERP, G_CC_MODULATEIA_ENV);
        }
        else if (highlight_mode)
        {   
            gDPSetCombineMode(gdl++, G_CC_FADEA, G_CC_PASS2);
        }
        else
        {
            gDPSetCombineMode(gdl++, G_CC_FADEA, G_CC_FADEA);
        }

        *gdlptr = gdl;

        draw_textured_rectangle(gdlptr, position, size, twidth, theight, flipXY, flipX, flipY);
    }
}
