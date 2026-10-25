#include <ultra64.h>
#include "game/cheat.h"
#include "math_atan2f.h"
#include "textrelated.h"
#include "front.h"
#include "fr.h"
#include "player.h"
#include "othermodemicrocode.h"
#include "image_bank.h"


/**
 * NTSC address 0x7F0C6090.
 * PAL address 0x7F0C55A0.
*/

Gfx *display_red_blue_on_radar(Gfx *DL)
{
    s32 padding[4];
    s32 player_count;
    s32 cur_playernum;
    enum MPSCENARIOS current_scenario;
    
    s32 start_left;
    s32 start_top;
    s32 dl_color_1;
    s32 i;
    PropRecord *other_player_prop;
    PropRecord *player_prop;

    f32 temp_f20;
    f32 temp_f22;
    s32 temp_f24;
    f32 temp_f28;
    f32 temp_f2;
    s32 dl_color_2;
    s32 loop_start_left;
    s32 loop_start_top;
    f32 temp_f16;

#if defined(VERSION_EU)
    #define RADAR_TOP_OFFSET 0x1d
    #define RADAR_RECT1_OFFSET 0x13
    #define RADAR_RECT1_D 0x355
    #define RADAR_VERT_SCALE 1.2f
#else
    #define RADAR_TOP_OFFSET 0x1a
    #define RADAR_RECT1_OFFSET 0x10
    #define RADAR_RECT1_D 0x400
    #define RADAR_VERT_SCALE 1.0f
#endif
    
    current_scenario = get_scenario();
    cur_playernum = get_cur_playernum();
    player_count = getPlayerCount();
    
    if (player_count == 1)
    {
        return DL;
    }
    
    if ((g_CurrentPlayer->mpmenuon != FALSE) || (g_CurrentPlayer->bonddead != FALSE))
    {
        return DL;
    }
    
    if (cheatIsActive(CHEAT_NO_RADAR_MP) != 0)
    {
        return DL;
    }

    start_left = (viGetViewLeft() + viGetViewWidth()) - 0x29;
    start_top = viGetViewTop() + RADAR_TOP_OFFSET;
    
#ifndef TARGET_N64
    if (player_count >= 3)
    {   /* Use the same layout in every quadrant: the original nudged the HUD
         * per column to compensate for CRT overscan. With no overscan on PC
         * that just looks misaligned, so every quadrant takes the inner-edge
         * (uncompensated) value. */
        start_left += 0xF;
    }
#else
    if ((player_count >= 3) && !(cur_playernum & 1))
    {
        start_left += 0xF;
    }
#endif
    
#ifndef TARGET_N64
    {   /* HUD Layout Wide: the radar hugs its viewport's right edge (options.c hudLayoutWide) */
        extern s32 hudLayoutWide(void);

        if (hudLayoutWide())
        {
            DL->words.w0 = ((u32) 0x3a << 24) | (0x00FFFFFF & ~(u32) 0x70);   /* PORT_HUD_ANCHOR RIGHT (options.h) */
            DL->words.w1 = 0x20 | portHudAnchorFrac();   /* with the HUD slide */
            DL++;
        }
    }
    {   /* HUD Scale (options.c): the radar is laid out from the view's top-right */
        extern Gfx *portHudScaleBegin(Gfx *gdl, f32 x, f32 y);

        DL = portHudScaleBegin(DL, (f32) (viGetViewLeft() + viGetViewWidth()), (f32) viGetViewTop());
    }
#endif
    texSelect(&DL, mpradarimages, 2, 0, 2);

    DL = microcode_constructor(DL);

    gDPSetCombineLERP(DL++, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, TEXEL0, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, TEXEL0, 0);
    gDPSetPrimColor(DL++, 0, 0, 0x00, 0x00, 0x00, 0xA0);

    gSPTextureRectangle(
        DL++,
        (start_left - 0x10) << 2,
        (start_top - RADAR_RECT1_OFFSET) << 2,
        (start_left + 0x10) << 2,
        (start_top + RADAR_RECT1_OFFSET) << 2,
        G_TX_RENDERTILE,
        0x10,
        0x10,
        0x400,
        RADAR_RECT1_D);
    
    DL = microcode_constructor_related_to_menus(DL, start_left - 2, start_top - 2, start_left + 2, start_top + 2, 0x40);
    
#ifndef TARGET_N64
    /* PORT: teams in any scenario (front.c portMpColourSide): the team's colour,
     * else Flag Tag / Golden Gun's holder colour as Rare's */
    if (portMpColourSide(cur_playernum) >= 0)
    {
        if (portMpColourSide(cur_playernum) == 0)
#else
    if ((current_scenario == SCENARIO_2v2)
        || (current_scenario == SCENARIO_3v1)
        || (current_scenario == SCENARIO_2v1)
        || (current_scenario == SCENARIO_TLD)
        || (current_scenario == SCENARIO_MWTGG))
    {
        if (g_playerPlayerData[cur_playernum].have_token_or_goldengun == 0)
#endif
        {
            dl_color_1 = 0xFF7777FF;
        }
        else
        {
            dl_color_1 = 0x8888FFFF;
        }
        
        DL = microcode_constructor_related_to_menus(DL, start_left - 1, start_top - 1, start_left + 1, start_top + 1, dl_color_1);
    }
    else
    {
        DL = microcode_constructor_related_to_menus(DL, start_left - 1, start_top - 1, start_left + 1, start_top + 1, -0x60);
    }

    for (i = 0; i < player_count; i++)
    {
        if (i != cur_playernum)
        {
            if (g_playerPointers[i]->bonddead == FALSE)
            {
                f32 tt1;
                other_player_prop = g_playerPointers[i]->prop;
                player_prop = g_CurrentPlayer->prop;

                temp_f20 = other_player_prop->pos.f[0] - player_prop->pos.f[0];
                temp_f22 = other_player_prop->pos.f[2] - player_prop->pos.f[2];
                
                temp_f28 = ((atan2f(temp_f20, temp_f22) * 180.0f) / M_PI_F) + g_CurrentPlayer->vv_theta + 180.0f;
                
                temp_f24 = 16;
                temp_f16 = 4000;

                tt1 = (temp_f24 / temp_f16);
                temp_f2 = sqrtf((temp_f20 * temp_f20) + (temp_f22 * temp_f22)) * tt1;
            
#ifndef TARGET_N64
                if (portMpColourSide(i) >= 0)
#else
                if ((current_scenario == SCENARIO_2v2)
                    || (current_scenario == SCENARIO_3v1)
                    || (current_scenario == SCENARIO_2v1)
                    || (current_scenario == SCENARIO_TLD)
                    || (current_scenario == SCENARIO_MWTGG))
#endif
                {
                    if (temp_f2 < temp_f24)
                    {
#ifndef TARGET_N64
                        if (portMpColourSide(i) == 0)
#else
                        if (g_playerPlayerData[i].have_token_or_goldengun == 0)
#endif
                        {
                            dl_color_2 = 0xFF0000A0;
                        }
                        else
                        {
                            dl_color_2 = 0x2828FFFF;
                        }
                    }
                    else
                    {
                        temp_f2 = temp_f24;
#ifndef TARGET_N64
                        if (portMpColourSide(i) == 0)
#else
                        if (g_playerPlayerData[i].have_token_or_goldengun == 0)
#endif
                        {
                            dl_color_2 = 0xFF000060;
                        }
                        else
                        {
                            dl_color_2 = 0x2828FFB0;
                        }
                    }
                }
                else
                {
                    dl_color_2 = 0xFFFF0060;
                    if (temp_f2 < temp_f24)
                    {
                        dl_color_2 = 0xFFFF00A0;
                    }
                    else
                    {
                        temp_f2 = temp_f24;
                    }
                }

                // 0.017453292f = DegToRad(1)
                loop_start_left = (s32) (sinf(temp_f28 * 0.017453292f) * temp_f2) + start_left;
                loop_start_top = (s32) (cosf(temp_f28 * 0.017453292f) * temp_f2 * RADAR_VERT_SCALE) + start_top;
                
                DL = microcode_constructor_related_to_menus(DL, loop_start_left - 2, loop_start_top - 2, loop_start_left + 2, loop_start_top + 2, 0x40);
                DL = microcode_constructor_related_to_menus(DL, loop_start_left - 1, loop_start_top - 1, loop_start_left + 1, loop_start_top + 1, dl_color_2);
            }
        }
    }

#ifndef TARGET_N64
    {
        extern s32 hudLayoutWide(void);
        extern Gfx *portHudScaleEnd(Gfx *gdl);

        DL = portHudScaleEnd(DL);
        if (hudLayoutWide())
        {
            DL->words.w0 = ((u32) 0x3a << 24) | (0x00FFFFFF & ~(u32) 0x70);   /* PORT_HUD_ANCHOR_OFF */
            DL->words.w1 = 0;
            DL++;
        }
    }
#endif
    return combiner_bayer_lod_perspective(DL);

    #undef RADAR_TOP_OFFSET
    #undef RADAR_RECT1_OFFSET
    #undef RADAR_RECT1_D
    #undef RADAR_VERT_SCALE
}
