#include <ultra64.h>
#include <os_extension.h>
#include <memp.h>
#include "spectrum.h"
#include "spectrum_hw.h"
#include <bondconstants.h>
#include <joy.h> /* explicit prototypes: implicitly declared functions can misread
                  * char/short returns on i386, so don't rely on implicit declarations. */
#include "ob.h"

// bss
u8* ptr_sectrum_monitor_data_temp_buf;
u8* ptr_sectrum_game_data_temp_buf;
u8* ptr_spectrum_roms;
u8* ptr_300alloc;
u8* ptr_6000alloc;
u8* ptr_pc_keyboard_table_alloc;
u8 spectrum_header16_15;
u8 byte_CODE_bss_8008E339;
u8 byte_CODE_bss_8008E33A;
u8 byte_CODE_bss_8008E33B;
u8 off_CODE_bss_8008E33C;
u8 byte_CODE_bss_8008E33D;
u8 byte_CODE_bss_8008E33E;
u8 byte_CODE_bss_8008E33F;
u8 off_CODE_bss_8008E340;
u8 byte_CODE_bss_8008E341;
u8 byte_CODE_bss_8008E342;
u8 byte_CODE_bss_8008E343;
u8 off_CODE_bss_8008E344;
u8 byte_CODE_bss_8008E345;
u8 byte_CODE_bss_8008E346;
u8 byte_CODE_bss_8008E347;
u8 spec_I;
u8 spec_R;
u8 spec_IFF2_lower;
u8 spec_IFF2_upper;
u8 spec_IM;
u8 spec_cur_rom_id;
u16 spec_IX;
u16 spec_IY;
u16 spec_SP;
u16 spec_PC;

void spectrum_hw_emulation(void);
s32 sub_GAME_7F0D37DC(u32 cycles, u8 specA, u8 port, u8 value);


// data
s8 D_8004EC30 = 0x0;
#ifdef __APPLE__
#define spec_keyboard_row_caps_z_x_c_v spec_keyboard_buffer[0]
#else
extern u8 spec_keyboard_row_caps_z_x_c_v;
#pragma weak spec_keyboard_row_caps_z_x_c_v = spec_keyboard_buffer
#endif
u8 spec_keyboard_buffer[] = 
{
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};

u8 spec_kempston_joystick_state = 0;

s16 D_8004EC44[] = 
{
    0x7FE, 0x3FE, 0x3FD, 0x3FB, 0x3F7, 0x3EF, 0x4F7, 0x3FD,
    0x4FD, 0x4FE, 0x4FB, 0x6FB, 0x7F7, 0x6F7, 0x8FB,  0xEF,
    0x4FE, 0x3FE, 0x3FD, 0x3FB, 0x3F7, 0x3EF, 0x4EF, 0x4F7,
    0x4FB, 0x4FD,  0xFD, 0x5FD, 0x7F7, 0x6FB, 0x8FB,  0xEF,
    0x3FD, 0x1FE, 0x7EF,  0xF7, 0x1FB, 0x2FB, 0x1F7, 0x1EF,
    0x6EF, 0x5FB, 0x6F7, 0x6FB, 0x6FD, 0x7FB, 0x7F7, 0x5FD,
    0x5FE, 0x2FE, 0x2F7, 0x1FD, 0x2EF, 0x5F7,  0xEF, 0x2FD,
     0xFB, 0x5EF,  0xFD, 0x8FF, 0x8FF, 0x8FF, 0x4EF, 0x6F7,
    0x8FF, 0x1FE, 0x7EF,  0xF7, 0x1FB, 0x2FB, 0x1F7, 0x1EF,
    0x6EF, 0x5FB, 0x6F7, 0x6FB, 0x6FD, 0x7FB, 0x7F7, 0x5FD,
    0x5FE, 0x2FE, 0x2F7, 0x1FD, 0x2EF, 0x5F7,  0xEF, 0x2FD,
     0xFB, 0x5EF,  0xFD, 0x8FF, 0x8FF, 0x8FF, 0x8FF, 0x8FF
};

u8 D_8004ED04 = 0;
u8 D_8004ED08 = 0;

s16 spec_palette[] = {
    0x0001, 0x0021, 0x8001, 0x8021, 
    0x0401, 0x0421, 0x8401, 0x8421,
    0x0001, 0x003F, 0xF801, 0xF83F, 
    0x07C1, 0x07FF, 0xFFC1, 0xFFFF
};
//
char* romnames[] = {
    "em/data/sabre.seg.rz",
    "em/data/atic.seg.rz",
    "em/data/jetpac.seg.rz",
    "em/data/jetman.seg.rz",
    "em/data/alien8.seg.rz",
    "em/data/gunfright.seg.rz",
    "em/data/under.seg.rz",
    "em/data/knightlore.seg.rz",
    "em/data/pssst.seg.rz",
    "em/data/cookie.seg.rz"
};

u8 spec_OUT_port[] = 
{
    0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00
};





void sub_GAME_7F0D28E0(u8 *spec, s32 arg1, s32 arg2, u8* arg3)
{
    s32 i;
    u8 *var_v0;
    u8 temp_t1;

    u8 var_a0;
    u8 var_v1;

    var_v0 = &spec[
        ((arg2 << 5) & 0x1800)
        |
        ((arg2 << 8) & 0x700)
        |
        ((arg2 << 2) & 0xE0)
        |
        (arg1 >> 3)
        ];

    temp_t1 = spec [0x1800 + ((arg2 << 2) & 0x3E0) + (arg1 >> 3)];

    if ((temp_t1 & 0x80) && D_8004ED04 != 0)
    {
        var_v1 = temp_t1 & 7;
        var_a0 = (temp_t1 >> 3) & 7;
    }
    else
    {
        var_a0 = temp_t1 & 7;
        var_v1 = (temp_t1 >> 3) & 7;
    }

    if (!(temp_t1 & 0x40))
    {
        var_a0 = (var_a0 + 8);
        var_v1 = (var_v1 + 8);
    }

    for (i = 0; i < 8; i++)
    {
        u8 temp_a3 = *var_v0;

        arg3[0] =
            (((temp_a3 & 0x80) ? var_a0 : var_v1) << 4)
            |
            ((temp_a3 & 0x40) ? var_a0 : var_v1)
            ;

        arg3[1] =
            (((temp_a3 & 0x20) ? var_a0 : var_v1) << 4)
            |
            ((temp_a3 & 0x10) ? var_a0 : var_v1)
            ;

        arg3[2] =
            (((temp_a3 & 0x08) ? var_a0 : var_v1) << 4)
            |
            ((temp_a3 & 0x04) ? var_a0 : var_v1) 
            ;

        arg3[3] =
            (((temp_a3 & 0x02) ? var_a0 : var_v1) << 4)
            |
            ((temp_a3 & 0x01) ? var_a0 : var_v1)
            ;

        arg3 += 0x20;
        var_v0 += 0x100;
    }
}






void sub_GAME_7F0D2A84(u8 *spec, u8 *alloc_ptr)
{
    s32 i;
    s32 sp58;
    s32 var_s7;
    s32 var_s4;
    s32 var_s0;
    u8 *var_s1;
    s32 j;
    
    D_8004ED08++;

    if (D_8004ED08 % 25 == 0)
    {
        D_8004ED08 = 0;

        for (i = 0; i < 0x300; i++)
        {
            ptr_300alloc[i] = 1;
        }

        D_8004ED04 = 1 - D_8004ED04;
    }

    for (sp58 = 0; sp58 < 0xc0; sp58 += 0x40)
    {
        for (var_s7 = 0; var_s7 < 0x100; var_s7 += 0x40)
        {
            var_s1 = &alloc_ptr[(((sp58 >> 6) << 2) + (var_s7 >> 6)) << 0xB];
            
            for (var_s4 = sp58; var_s4 < sp58 + 0x40; var_s4 += 8)
            {
                for (var_s0 = var_s7; var_s0 < var_s7 + 0x40; var_s0 += 8)
                {
                    if (ptr_300alloc[(((var_s4 >> 3) << 5) | (var_s0 >> 3))])
                    {
                        sub_GAME_7F0D28E0(spec, var_s0, var_s4, var_s1);
                    }

                    var_s1 += 4;
                }
                
                var_s1 += 0xe0;
            }
        }
    }

    for (j = 0; j < 0x300; j++)
    {
        ptr_300alloc[j] = 0;
    }
}

void spectrum_p1controller_to_kempston(void)
{
    s32 kempston_up;
    s32 kempston_down;
    s32 kempston_left;
    s32 kempston_right;
    s32 kempston_fire;
    s32 buttons;
    s32 stick_x;
    s32 stick_y;

    s32 index;

    kempston_up = 0;
    kempston_down = 0;
    kempston_left = 0;
    kempston_right = 0;
    kempston_fire = 0;

    joyConsumeSamplesWrapper();
    buttons = joyGetButtons(PLAYER_1, ANY_BUTTON);
    stick_x = joyGetStickXInRange(PLAYER_1, -3, 3);
    stick_y = joyGetStickYInRange(PLAYER_1, -3, 3);

    for (index = 1; index != 9; index += 4)
    {
        spec_keyboard_row_caps_z_x_c_v  = 0xff;
        spec_keyboard_buffer[index + 1] = 0xff;
        spec_keyboard_buffer[index + 2] = 0xff;
        spec_keyboard_buffer[index + 3] = 0xff;
        spec_keyboard_buffer[index]     = 0xff;
    }

    if (buttons & Z_TRIG)
    {
        kempston_fire = 1;
    }
    if ((buttons & (L_JPAD | L_CBUTTONS)) || (stick_x < -1))
    {
        kempston_left = 1;
    }
    if ((buttons & (R_JPAD | R_CBUTTONS)) || (stick_x >= 2))
    {
        kempston_right = 1;
    }
    if ((buttons & (U_JPAD | U_CBUTTONS)) || (stick_y >= 2))
    {
        kempston_up = 1;
    }
    if ((buttons & (D_JPAD | D_CBUTTONS)) || (stick_y < -1))
    {
        kempston_down = 1;
    }
    if ((spec_cur_rom_id == ROM_JETPAC) && (buttons & (A_BUTTON | B_BUTTON)))
    {
        kempston_up = 1;
    }
    if (((spec_cur_rom_id == ROM_ALIEN8) || (spec_cur_rom_id == ROM_KNIGHTLORE)) && (buttons & (A_BUTTON | B_BUTTON)))
    {
        kempston_down = 1;
    }
    if (((spec_cur_rom_id == ROM_SABRE) || (spec_cur_rom_id == ROM_ATIC) || (spec_cur_rom_id == ROM_UNDER) || (spec_cur_rom_id == ROM_COOKIE) || (spec_cur_rom_id == ROM_ALIEN8) || (spec_cur_rom_id == ROM_KNIGHTLORE)) && (buttons & (A_BUTTON | B_BUTTON)))
    {
        spec_keyboard_buffer[4] = (u8)(spec_keyboard_buffer[4] & 0xFE);
    }
    if (((spec_cur_rom_id == ROM_JETPAC) || (spec_cur_rom_id == ROM_PSSST)) && (buttons & (A_BUTTON | B_BUTTON)))
    {
        spec_keyboard_buffer[3] = (u8)(spec_keyboard_buffer[3] & 0xEF);
    }
    if ((spec_cur_rom_id == ROM_GUNFRIGHT) && (buttons & (A_BUTTON | B_BUTTON)))
    {
        spec_keyboard_buffer[3] = (u8)(spec_keyboard_buffer[3] & 0xFB);
    }
    if (spec_cur_rom_id == ROM_JETMAN)
    {
        if (buttons & (A_BUTTON | B_BUTTON))
        {
            spec_keyboard_buffer[4] = (u8)(spec_keyboard_buffer[4] & 0xEF);
        }
        if (buttons & A_BUTTON)
        {
            spec_keyboard_buffer[0] = (u8)(spec_keyboard_buffer[0] & 0xFD);
        }
        if (buttons & B_BUTTON)
        {
            spec_keyboard_buffer[7] = (u8)(spec_keyboard_buffer[7] & 0xFE);
        }
    }
    if (spec_cur_rom_id == ROM_UNDER)
    {
        if (buttons & A_BUTTON)
        {
            kempston_up = 1;
        }
        if (buttons & B_BUTTON)
        {
            spec_keyboard_buffer[7] = (u8)(spec_keyboard_buffer[7] & 0xFE);
        }
    }
    if (spec_cur_rom_id == ROM_ATIC)
    {
        if (buttons & (A_BUTTON | B_BUTTON))
        {
            spec_keyboard_buffer[0] = (u8)(spec_keyboard_buffer[0] & 0xFD);
        }
        if (buttons & L_JPAD)
        {
            spec_keyboard_buffer[3] = (u8)(spec_keyboard_buffer[3] & 0xF7);
        }
        if (buttons & D_JPAD)
        {
            spec_keyboard_buffer[3] = (u8)(spec_keyboard_buffer[3] & 0xEF);
        }
        if (buttons & R_JPAD)
        {
            spec_keyboard_buffer[4] = (u8)(spec_keyboard_buffer[4] & 0xEF);
        }
    }

    if (buttons & L_TRIG)
    {
        for (index = 0; index < B_BUTTON; index++)
        {
            ptr_spectrum_roms[index] = 0;
        }
    }

    spec_kempston_joystick_state = (kempston_fire << 4) | (kempston_up << 3) | (kempston_down << 2) | (kempston_left << 1) | kempston_right;
}


void init_spectrum_game(s32 arg0)
{
    s32 i;
    s32 j;
    s32 var_v1;
    u8 temp_t5_3;
    
    ptr_pc_keyboard_table_alloc = mempAllocBytesInBank(0x100, MEMPOOL_STAGE);

    for (i = 0; i < 0x100; i++)
    {
        var_v1 = 0;
        for (j = 0; j < 8; j++)
        {
            if ((i >> j) & 1)
            {
                var_v1 += 1;
            }
        }

        if (var_v1 & 1)
        {
            ptr_pc_keyboard_table_alloc[i] = 0;
        }
        else
        {
            ptr_pc_keyboard_table_alloc[i] = 4;
        }
    }

    ptr_6000alloc = mempAllocBytesInBank(0x6000, MEMPOOL_STAGE);
    ptr_300alloc = mempAllocBytesInBank(0x300, MEMPOOL_STAGE);

    for (i = 0; i < 0x300; i++)
    {
        ptr_300alloc[i] = 1;
    }

    ptr_spectrum_roms = mempAllocBytesInBank(0x10000, MEMPOOL_STAGE);
    ptr_sectrum_monitor_data_temp_buf = _fileNameLoadToBank("em/data/spec_rom.seg.rz", FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_STAGE);

    for (i = 0; i < 0x4000; i++)
    {
        ptr_spectrum_roms[i] = ptr_sectrum_monitor_data_temp_buf[i];
    }

    spec_cur_rom_id = arg0;
    if (spec_cur_rom_id >= 5)
    {
        spec_cur_rom_id = 0;
    }

    ptr_sectrum_game_data_temp_buf = _fileNameLoadToBank(romnames[spec_cur_rom_id], FILELOADMETHOD_DEFAULT, 0x100, MEMPOOL_STAGE);

    for (i = 0; i < 0xC000; i++)
    {
        *(ptr_spectrum_roms + i + 0x4000 ) = *(i + 0x1b + ptr_sectrum_game_data_temp_buf);
    }

    spec_I = ptr_sectrum_game_data_temp_buf[0];
    
    byte_CODE_bss_8008E347 = ptr_sectrum_game_data_temp_buf[1];
    byte_CODE_bss_8008E346 = ptr_sectrum_game_data_temp_buf[2];
    byte_CODE_bss_8008E345 = ptr_sectrum_game_data_temp_buf[3];
    off_CODE_bss_8008E344  = ptr_sectrum_game_data_temp_buf[4];
    
    byte_CODE_bss_8008E343 = ptr_sectrum_game_data_temp_buf[5];
    byte_CODE_bss_8008E342 = ptr_sectrum_game_data_temp_buf[6];
    byte_CODE_bss_8008E341 = ptr_sectrum_game_data_temp_buf[7];
    off_CODE_bss_8008E340  = ptr_sectrum_game_data_temp_buf[8];
    
    byte_CODE_bss_8008E33F = ptr_sectrum_game_data_temp_buf[9];
    byte_CODE_bss_8008E33E = ptr_sectrum_game_data_temp_buf[0xA];
    byte_CODE_bss_8008E33D = ptr_sectrum_game_data_temp_buf[0xB];
    off_CODE_bss_8008E33C  = ptr_sectrum_game_data_temp_buf[0xC];
    
    byte_CODE_bss_8008E33B = ptr_sectrum_game_data_temp_buf[0xD];
    byte_CODE_bss_8008E33A = ptr_sectrum_game_data_temp_buf[0xE];
    
    spec_IY = ptr_sectrum_game_data_temp_buf[0xf] + (ptr_sectrum_game_data_temp_buf[0x10] << 8);
    spec_IX = ptr_sectrum_game_data_temp_buf[0x11] + (ptr_sectrum_game_data_temp_buf[0x12] << 8);

    spec_IFF2_lower = 
        spec_IFF2_upper = (ptr_sectrum_game_data_temp_buf[0x13] >> 2) & 1;
    
    spec_R = ptr_sectrum_game_data_temp_buf[0x14];
    byte_CODE_bss_8008E339 = ptr_sectrum_game_data_temp_buf[0x15];
    spectrum_header16_15 = ptr_sectrum_game_data_temp_buf[0x16];
    spec_SP = ptr_sectrum_game_data_temp_buf[0x17] + (ptr_sectrum_game_data_temp_buf[0x18] << 8);
    
    spec_IM = ptr_sectrum_game_data_temp_buf[0x19];
    if (spec_IM > 0)
    {
        spec_IM = spec_IM + 1;
    }
    
    spec_PC = ((ptr_spectrum_roms[spec_SP + 1]) << 8) | (ptr_spectrum_roms[spec_SP]);
    
    spec_SP += 2;
}






void run_spectrum_game(void)
{
    spectrum_p1controller_to_kempston();
    spectrum_hw_emulation();
    sub_GAME_7F0D2A84(ptr_spectrum_roms + 0x4000,ptr_6000alloc);
    return;
}






Gfx* spectrum_draw_screen(Gfx *gdl)
{
    s32 i;
    s32 j;

    gDPPipeSync(gdl++);
    gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, &spec_palette);
    gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 1, 0x0100, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(gdl++);
    gDPLoadTLUTCmd(gdl++, G_TX_LOADTILE, 15);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCombineMode(gdl++, G_CC_DECALRGB, G_CC_DECALRGB);
    gDPSetTextureFilter(gdl++, G_TF_POINT);
    gSPTexture(gdl++, -1, -1, 0, G_TX_RENDERTILE, G_ON);


    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 4; j ++)
        {
            gDPPipeSync(gdl++);
            gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, &ptr_6000alloc[(i * 4 + j) * 0x800]);
            gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
            gDPLoadSync(gdl++);
            gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, 1023, 512);
            gDPPipeSync(gdl++);
            gDPSetTextureLUT(gdl++, G_TT_RGBA16);
            gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_4b, 4, 0x0000, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, 6, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, 6, G_TX_NOLOD);
            gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, 0x7e0, 0x7e0);
            gSPTextureRectangle(gdl++, 
                                (((j + 0) << 6) + 0x20) << 2,
                                (((i + 0) << 6) + 0x18) << 2,
                                (((j + 1) << 6) + 0x20) << 2,
                                (((i + 1) << 6) + 0x18) << 2,
                                G_TX_RENDERTILE,
                                0,
                                0,
                                0x400,
                                0x400);
        }
    }

    return gdl;
}






u8 spectrum_input_handling(s32 arg0, u8 arg1, u8 arg2)
{
    s32 i;
    u8 var_v1;

    if (arg2 == 0xFE)
    {
        var_v1 = 0xFF;
        
        for (i = 0; i < 8; i++)
        {
            if ((arg1 & 1) == 0)
            {
                var_v1 &= spec_keyboard_buffer[i];
            }

            arg1 >>= 1;
        }
        
        return var_v1;
    }
    
    if (arg2 == 0x1F)
    {
        return spec_kempston_joystick_state;
    }
    
    return 0xFFU;
}




void nullsub_50(void) {
    return;
}


s32 sub_GAME_7F0D37DC(u32 cycles, u8 specA, u8 port, u8 value)
{
    int temp_v0;
    if (port == 0xFE)
    {
        temp_v0 = value & 7;
        if (temp_v0 != spec_OUT_port[0])
        {
            spec_OUT_port[0] = temp_v0;
        }

        return 0;
    }

    return 0;
}





/***
 * Spectrum main emulation function.
 * Should be compiled at O2, with low enough olimits that this is not optimized.
 * 
* and maybe we'll get lucky and we'll both live again
* well, I don't know, I don't know, I don't even think so.
* the more we move ahead the more we're stuck in rewind
* well, I don't mind, I don't mind, how the hell could I mind?
* - 3 weeks in October.
* Thanks Ryan
* - Bethany Burns
 * 
*/
void spectrum_hw_emulation(void)
{
// XM - macros defined with locally scoped variables
#define SPEC_XM_GET_INDEX_R() ((sp287_index_mode == 1) ? sp296_IX : sp294_IY) 
#define SPEC_XM_SET_INDEX_R(val) {if (sp287_index_mode == 1) { sp296_IX = (val); } else { sp294_IY = (val); }}
#define SPEC_XM_GET_INDEX_HI() ((sp287_index_mode == 0) ? s6_reg_H : ((sp287_index_mode == 1) ? sp296_IX >> 8 : sp294_IY >> 8))
#define SPEC_XM_GET_INDEX_LO() ((sp287_index_mode == 0) ? s7_reg_L : ((sp287_index_mode == 1) ? sp296_IX & 0xff : sp294_IY & 0xff))
#define SPEC_TICK(clock, n) do { clock += n; } while(0)

    register u8 s0_reg_A; // register A
    register u8 s1_reg_F; // register F
    register u8 s2_reg_B; // register B
    register u8 s3_reg_C; // register C
    register u8 s4_reg_D; // register D
    register u8 s5_reg_E; // register E
    register u8 s6_reg_H; // register H
    register u8 s7_reg_L; // register L

    u8 sp2A7_reg_R;
    u8 sp2A6_alt_reg_A;
    u8 sp2A5_alt_reg_F;
    u8 sp2A4_alt_reg_B;
    u8 sp2A3_alt_reg_C;
    u8 sp2A2_alt_reg_D;
    u8 sp2A1_alt_reg_E;
    u8 sp2A0_alt_reg_H;
    
    u8 sp29F_alt_reg_L;
    u8 sp29E_reg_I;
    u8 sp29D_IFF1;
    u8 sp29C_IFF2;
    u8 sp29B_IM;
    // u8 sp29a
    u16 sp298_PC; // program counter
    u16 sp296_IX;
    u16 sp294_IY;
    u16 sp292_SP; // stack pointer
    // u16 sp290

    u32 sp28C_clock; // clock
    s32 sp288_unk_R;
    u8 sp287_index_mode;
    u8 sp286_next_index_mode;
    u8 sp285_exit;
    u8 sp284_current_byte;
    u32 sp280_unk_clock;

    sp280_unk_clock = 0x11100;

    sp29B_IM = 0;
    sp29C_IFF2 = 0;
    sp29D_IFF1 = 0;
    sp2A7_reg_R = 0;
    sp29E_reg_I = 0;
    
    sp2A5_alt_reg_F = \
    sp2A6_alt_reg_A = \
    sp2A4_alt_reg_B = \
    sp2A3_alt_reg_C = \
    sp2A2_alt_reg_D = \
    sp2A1_alt_reg_E = \
    sp2A0_alt_reg_H = \
    sp29F_alt_reg_L = 0;

    sp296_IX = 
    sp294_IY = \
    sp292_SP = \
    sp298_PC = 0;
    sp287_index_mode = \
    sp286_next_index_mode = \
    sp288_unk_R = 0;
    sp28C_clock = 0;
    
    sp2A6_alt_reg_A = off_CODE_bss_8008E340;
    sp2A5_alt_reg_F = byte_CODE_bss_8008E341;
    sp2A4_alt_reg_B = byte_CODE_bss_8008E342;
    sp2A3_alt_reg_C = byte_CODE_bss_8008E343;
    sp2A2_alt_reg_D = off_CODE_bss_8008E344;
    sp2A1_alt_reg_E = byte_CODE_bss_8008E345;
    sp2A0_alt_reg_H = byte_CODE_bss_8008E346;
    sp29F_alt_reg_L = byte_CODE_bss_8008E347;
    
    sp29D_IFF1 = spec_IFF2_lower;
    sp29C_IFF2 = spec_IFF2_upper;
    sp29E_reg_I = spec_I;
    sp2A7_reg_R = spec_R;
    sp288_unk_R = sp2A7_reg_R;
    sp29B_IM = spec_IM;
    sp296_IX = spec_IX;
    sp294_IY = spec_IY;
    sp292_SP = spec_SP;
    sp298_PC = spec_PC;

    s0_reg_A = spectrum_header16_15;
    s1_reg_F = byte_CODE_bss_8008E339;
    s2_reg_B = byte_CODE_bss_8008E33A;
    s3_reg_C = byte_CODE_bss_8008E33B;
    s4_reg_D = off_CODE_bss_8008E33C;
    s5_reg_E = byte_CODE_bss_8008E33D;
    s6_reg_H = byte_CODE_bss_8008E33E;
    s7_reg_L = byte_CODE_bss_8008E33F;


    while (sp28C_clock < sp280_unk_clock || sp285_exit == 0)
    {
        sp287_index_mode = sp286_next_index_mode;
        sp286_next_index_mode = 0;
        sp285_exit = 1;

        sp284_current_byte = ptr_spectrum_roms[sp298_PC];

        sp298_PC++;
        sp288_unk_R++;

        switch (sp284_current_byte)
        {
            case SPEC_HEX_OP_NOP: // 0x00
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);
                break;
            }

            //  "ld bc,NN" operation
            /**
             * The 16-bit integer value is loaded to the BC register pair.
            */
            case SPEC_HEX_OP_LD_BC_HHLL: // 0x01
            {
                sp28C_clock += SPEC_CPU_CYCLES(10);
                
                s3_reg_C = ptr_spectrum_roms[sp298_PC];
                sp298_PC = sp298_PC + 1;
                s2_reg_B = ptr_spectrum_roms[sp298_PC];
                sp298_PC = sp298_PC + 1;
                break;
            }

            //  "ld (bc),a" operation
            /**
             * The contents of the A are loaded to the memory location
             * specified by the contents of the register pair BC.
            */
            case SPEC_HEX_OP_LD_pBCp_A: //0x02
            {

                sp28C_clock += SPEC_CPU_CYCLES(7);

                SPEC_M_MEM_WRITE(SPEC_MK_16(s2_reg_B, s3_reg_C), s0_reg_A);
                
                break;
            }

            //  "inc bc" operation
            /**
             * The contents of register pair BC are incremented.
            */
            case SPEC_HEX_OP_INC_BC: // 0x03
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);

                s3_reg_C++;
                if (s3_reg_C == 0)
                {
                    s2_reg_B++;
                }
                
                break;
            }

            //  "inc b" operation
            /**
             * Register B is incremented.
             * S is set if result is negative; otherwise, it is reset.
             * Z is set if result is 0; otherwise, it is reset.
             * H is set if carry from bit 3; otherwise, it is reset.
             * P/V is set if r was 7Fh before operation; otherwise, it is reset.
             * N is reset.
             * C is not affected.
            */
            case SPEC_HEX_OP_INC_B: // 0x04
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_INC(s2_reg_B, s1_reg_F);
                
                break;
            }

            // "dec b" operation
            /**
             * Register B is decremented.
             * S is set if result is negative; otherwise, it is reset.
             * Z is set if result is 0; otherwise, it is reset.
             * H is set if borrow from bit 4, otherwise, it is reset.
             * P/V is set if m was 80h before operation; otherwise, it is reset.
             * N is set.
             * C is not affected.
            */
            case SPEC_HEX_OP_DEC_B: // 0x05
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_DEC(s2_reg_B, s1_reg_F);
                
                
                break;
            }

            // "ld b,N" operation
            /**
             * The 8-bit integer N is loaded to B.
            */
            case SPEC_HEX_OP_LD_B_NN: // 0x06
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                s2_reg_B = ptr_spectrum_roms[sp298_PC];
                
                sp298_PC += 1;

                break;
            }

            // "rlca" operation
            /**
             * The contents of  A are rotated left 1 bit position. The
             * sign bit (bit 7) is copied to the Carry flag and also
             * to bit 0.
             * S, Z, P/V are not affected.
             * H, N are reset.
             * C is data from bit 7 of A.
            */
            case SPEC_HEX_OP_RLCA: // 0x07
            {                    
                sp28C_clock += SPEC_CPU_CYCLES(4);
                
                s0_reg_A = ((s0_reg_A << 1) | (s0_reg_A >> 7));
                s1_reg_F = ((s1_reg_F & 0xc4) | (s0_reg_A & 0x29));

                break;
            }

            // "ex af,af'" operation
            /**
             *  The 2-byte contents of the register pairs AF and AF' are exchanged.
            */
            case SPEC_HEX_OP_EX_AF_AF: // 0x8
            {                    
                u8 sp27F;
                u8 sp27E;
                
                sp27F = s0_reg_A;
                sp27E = s1_reg_F;
                s0_reg_A = sp2A6_alt_reg_A;
                s1_reg_F = sp2A5_alt_reg_F;
                sp28C_clock += SPEC_CPU_CYCLES(4);
                sp2A6_alt_reg_A = sp27F;
                sp2A5_alt_reg_F = sp27E;
                
                break;
            }

            // "add hl,bc" operation
            /***
            * The contents of BC are added to the contents of HL and
            * the result is stored in HL.
            * S, Z, P/V are not affected.
            * H is set if carry from bit 11; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 15; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_HL_BC: // 0x09
            {
                u16 sp27C;
                
                sp28C_clock += SPEC_CPU_CYCLES(0xb);
                
                if (sp287_index_mode == 0)
                {
                    SPEC_M_ED_ADD_16(sp27C, s6_reg_H, s7_reg_L, s2_reg_B, s3_reg_C, s1_reg_F);
                }
                else
                {
                    SPEC_M_ED_ADD_16_INDEXED(s2_reg_B, s3_reg_C, s1_reg_F);
                }
                
                break;
            }

            // LD A,(BC)
            // The contents of the memory location specified by BC are loaded to A.
            case SPEC_HEX_OP_LD_A_pBCp: // 0xa
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);
             
                s0_reg_A = ptr_spectrum_roms[SPEC_MK_16(s2_reg_B, s3_reg_C)];
                
                break;
            }

            // DEC BC
            // The contents of register pair BC are decremented.
            case SPEC_HEX_OP_DEC_BC: // 0xb
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);
                
                if (s3_reg_C-- == 0)
                {
                    s2_reg_B--;
                }

                break;
            }

            // INC C
            /***
            * Register C is incremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if r was 7Fh before operation; otherwise, it is reset.
            * N is reset.
            * C is not affected.
            */
            case SPEC_HEX_OP_INC_C: // 0xc
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_INC(s3_reg_C, s1_reg_F);

                break;
            }

            // DEC C
            /***
            * Register C is decremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4, otherwise, it is reset.
            * P/V is set if m was 80h before operation; otherwise, it is reset.
            * N is set.
            * C is not affected.
            */
            case SPEC_HEX_OP_DEC_C: // 0xd
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_DEC(s3_reg_C, s1_reg_F);
                
                break;
            }

            // LD C, nn
            // The 8-bit integer N is loaded to C.
            case SPEC_HEX_OP_LD_C_NN: // 0xe
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);
                
                s3_reg_C = ptr_spectrum_roms[sp298_PC];
                sp298_PC++;

                break;
            }

            // RR CA
            /***
            * The contents of A are rotated right 1 bit position. Bit 0 is
            * copied to the Carry flag and also to bit 7.
            * S, Z, P/V are not affected.
            * H, N are reset.
            * C is data from bit 0 of A.
            */
            case SPEC_HEX_OP_CA: // 0x0f
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);
                
                s1_reg_F = (s1_reg_F & 0xC4) | (s0_reg_A & 1);

                s0_reg_A = (( s0_reg_A >> 1) | (s0_reg_A << 7));
                s1_reg_F = s1_reg_F | (s0_reg_A & 0x28);

                break;
            }

            // DJNZ NN
            /**
            * This instruction is similar to the conditional jump
            * instructions except that value of B is used to determine
            * branching. B is decremented, and if a nonzero value remains,
            * the value of displacement E is added to PC. The next
            * instruction is fetched from the location designated by
            * the new contents of the PC. The jump is measured from the
            * address of the instruction op code and contains a range of
            * –126 to +129 bytes. The assembler automatically adjusts for
            * the twice incremented PC. If the result of decrementing leaves
            * B with a zero value, the next instruction executed is taken
            * from the location following this instruction.
            */
            case SPEC_HEX_OP_DJNZ_NN: //0x10
            {
                sp28C_clock += SPEC_CPU_CYCLES(8);
                
                s2_reg_B--;

                if (s2_reg_B == 0)
                {
                    sp298_PC += 1;
                }
                else
                {
                    s32 sp274;
                    
                    sp274 = (s8)ptr_spectrum_roms[sp298_PC];
                    sp298_PC += sp274 + 1;
                    
                    sp28C_clock += SPEC_CPU_CYCLES(5);
                }

                break;
            }

            // LD DE,HHLL
            //  The 16-bit integer value is loaded to the DE register pair.
            case SPEC_HEX_OP_LD_DE_HHLL: // 0x11
            {
                sp28C_clock += SPEC_CPU_CYCLES(10);

                s5_reg_E = ptr_spectrum_roms[sp298_PC];
                sp298_PC++;
                s4_reg_D = ptr_spectrum_roms[sp298_PC];
                sp298_PC++;

                break;
            }

            // LD (DE),A
            // The contents of the A are loaded to the memory location
            // specified by the contents of the register pair DE.
            case SPEC_HEX_OP_LD_pDEp_A: // 0x12
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                SPEC_M_MEM_WRITE(SPEC_MK_16(s4_reg_D, s5_reg_E), s0_reg_A);

                break;
            }

            // INC DE
            // The contents of register pair DE are incremented.
            case SPEC_HEX_OP_INC_DE: // 0x13
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);
                
                s5_reg_E++;

                if (s5_reg_E == 0)
                {
                    s4_reg_D++;
                }
                
                break;
            }

            // INC D
            /**
            * Register D is incremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if r was 7Fh before operation; otherwise, it is reset.
            * N is reset.
            * C is not affected.
            */
            case SPEC_HEX_OP_INC_D: // 0x14
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_INC(s4_reg_D, s1_reg_F);

                break;
            }

            // DEC D
            /**
            * Register D is decremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4, otherwise, it is reset.
            * P/V is set if m was 80h before operation; otherwise, it is reset.
            * N is set.
            * C is not affected.
            */
            case SPEC_HEX_OP_DEC_D: // 0x15
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_DEC(s4_reg_D, s1_reg_F);
                
                break;
            }

            // LD D, NN
            // The 8-bit integer N is loaded to D.
            case SPEC_HEX_OP_LD_D_NN: // 0x16
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);
                
                s4_reg_D = ptr_spectrum_roms[sp298_PC];
                sp298_PC += 1;
                
                break;
            }

            // RLA
            /**
            * The contents of A are rotated left 1 bit position through the
            * Carry flag. The previous contents of the Carry flag are copied
            * to bit 0.
            * S, Z, P/V are not affected.
            * H, N are reset.
            * C is data from bit 7 of A.
            */
            case SPEC_HEX_OP_RLA: // 0x17
            {
                s32 sp270;
                
                sp28C_clock += SPEC_CPU_CYCLES(4);

                sp270 = (s32) s0_reg_A >> 7;
                s0_reg_A = ((s0_reg_A << 1) | (s1_reg_F & 1));
                s1_reg_F = ((s1_reg_F & 0xC4) | (s0_reg_A & 0x28) | sp270);
                
                break;
            }

            // JR NN
            /***
            * This instruction provides for unconditional branching
            * to other segments of a program. The value of displacement E is
            * added to PC and the next instruction is fetched from the location
            * designated by the new contents of the PC. This jump is measured
            * from the address of the instruction op code and contains a range
            * of –126 to +129 bytes. The assembler automatically adjusts for
            * the twice incremented PC.
            */
            case SPEC_HEX_OP_JR_NN: // 0x18
            {
                s32 sp26C;
                
                sp28C_clock += SPEC_CPU_CYCLES(7);
                sp26C = (s8)ptr_spectrum_roms[sp298_PC];
                sp298_PC += sp26C + 1;
                if(1);
                sp28C_clock += SPEC_CPU_CYCLES(5);
             
                break;
            }

            // ADD HL, DE
            /**
            * The contents of DE are added to the contents of HL and
            * the result is stored in HL.
            * S, Z, P/V are not affected.
            * H is set if carry from bit 11; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 15; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_HL_DE: // 0x19
            {               
                struct reg16_2 sp268;
                
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                if (sp287_index_mode == 0)
                {
                    SPEC_M_ED_ADD_16_c_ALIGN32(sp268, s6_reg_H, s7_reg_L, s4_reg_D, s5_reg_E, s1_reg_F);
                }
                else
                {
                    SPEC_M_ED_ADD_16_INDEXED(s4_reg_D, s5_reg_E, s1_reg_F);
                }
                
                break;
            }

            // LD A, (DE)
            /***
            * The contents of the memory location specified by DE are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_pDEp: // 0x1A
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                s0_reg_A = ptr_spectrum_roms[SPEC_MK_16(s4_reg_D, s5_reg_E)];

                break;
            }

            // DEC DE
            /***
            * The contents of register pair DE are decremented.
            */
            case SPEC_HEX_OP_DEC_DE: // 0x1B
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);

                if (s5_reg_E-- == 0)
                {
                    s4_reg_D--;
                }

                break;
            }

            // INC E
            /***
            * Register E is incremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if r was 7Fh before operation; otherwise, it is reset.
            * N is reset.
            * C is not affected.
            */
            case SPEC_HEX_OP_INC_E: // 0x1C
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_INC(s5_reg_E, s1_reg_F);

                break;
            }

            // DEC E
            /***
            * Register E is decremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4, otherwise, it is reset.
            * P/V is set if m was 80h before operation; otherwise, it is reset.
            * N is set.
            * C is not affected.
            */
            case SPEC_HEX_OP_DEC_E: // 0x1D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_DEC(s5_reg_E, s1_reg_F);

                break;
            }

            // LD E, NN
            /***
            * The 8-bit integer N is loaded to E.
            */
            case SPEC_HEX_OP_LD_E_NN: // 0x1E
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                s5_reg_E = ptr_spectrum_roms[sp298_PC];
                sp298_PC += 1;

                break;
            }

            // RRA
            /***
            * The contents of A are rotated right 1 bit position through the
            * Carry flag. The previous contents of the Carry flag are copied
            * to bit 7.
            * S, Z, P/V are not affected.
            * H, N are reset.
            * C is data from bit 0 of A.
            */
            case SPEC_HEX_OP_RRA: // 0x1F
            {
                s32 sp260;

                sp28C_clock += SPEC_CPU_CYCLES(4);

                sp260 = s0_reg_A & 1;

                s0_reg_A = (((s32) s0_reg_A >> 1) | (s1_reg_F << 7));
                s1_reg_F = ((s1_reg_F & 0xC4) | (s0_reg_A & 0x28) | sp260);

                break;
            }

            // JR NZ, NN
            /***
            * This instruction provides for conditional branching to
            * other segments of a program depending on the results of a test
            * (Z flag is not set). If the test evaluates to *true*, the value of displacement
            * E is added to PC and the next instruction is fetched from the
            * location designated by the new contents of the PC. The jump is
            * measured from the address of the instruction op code and contains
            * a range of –126 to +129 bytes. The assembler automatically adjusts
            * for the twice incremented PC.
            */
            case SPEC_HEX_OP_JR_NZ_NN: // 0x20
            {
                s32 sp25C;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (s1_reg_F & 0x40)
                {
                    sp298_PC += 1;
                }
                else
                {
                    sp25C = (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC += sp25C + 1;
                    sp28C_clock += SPEC_CPU_CYCLES(5);
                }

                break;
            }

            // LD HL, HHLL
            /***
             * The 16-bit integer value is loaded to the HL register pair.
            */
            case SPEC_HEX_OP_LD_HL_HHLL: // 0x21
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L = ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                    s6_reg_H = ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }
                else
                {
                    if (sp287_index_mode == 1)
                    {
                        sp296_IX = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                    }
                    else
                    {
                        sp294_IY = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                    }

                    sp298_PC += 2;
                }

                break;
            }

            // LD (HHLL), HL
            /***
            * The contents of the low-order portion of HL (L) are loaded to memory
            * address (NN), and the contents of the high-order portion of HL (H)
            * are loaded to the next highest memory address(NN + 1).
            */
            case SPEC_HEX_OP_LD_pHHLLp_HL: // 0x22
            {
                u16 sp25A;

                sp28C_clock += SPEC_CPU_CYCLES(0x10);

                sp25A = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                sp298_PC += 2;

                if (sp287_index_mode == 0)
                {
                    SPEC_M_MEM_WRITE(sp25A, s7_reg_L);
                    SPEC_M_MEM_WRITE(sp25A + 1, s6_reg_H);
                }
                else if (sp287_index_mode == 1)
                {
                    SPEC_M_MEM_WRITE(sp25A, sp296_IX);
                    SPEC_M_MEM_WRITE(sp25A + 1, ((s32) sp296_IX >> 8));
                }
                else
                {
                    SPEC_M_MEM_WRITE(sp25A, sp294_IY);
                    SPEC_M_MEM_WRITE(sp25A + 1, ((s32) sp294_IY >> 8));
                }

                break;
            }

            // INC HL
            /***
            * The contents of register pair HL are incremented.
            */
            case SPEC_HEX_OP_INC_HL: // 0x23
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L++;
                    if (s7_reg_L == 0)
                    {
                        s6_reg_H++;
                    }
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX += 1;
                }
                else
                {
                    sp294_IY += 1;
                }

                break;
            }

            // INC H
            /***
            * Register H is incremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if r was 7Fh before operation; otherwise, it is reset.
            * N is reset.
            * C is not affected.
            */
            case SPEC_HEX_OP_INC_H: // 0x24
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    SPEC_M_INC(s6_reg_H, s1_reg_F);
                }
                else
                {
                    u8 sp259;

                    sp259 = SPEC_XM_GET_INDEX_R() >> 8;

                    SPEC_M_INC(sp259, s1_reg_F);

                    if (sp287_index_mode == 1)
                    {
                        sp296_IX = SPEC_MK_16b(sp259, sp296_IX);
                    }
                    else
                    {
                        sp294_IY = SPEC_MK_16b(sp259, sp294_IY);
                    }
                }

                break;
            }

            // DEC H
            /***
            * Register H is decremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4, otherwise, it is reset.
            * P/V is set if m was 80h before operation; otherwise, it is reset.
            * N is set.
            * C is not affected.
            */
            case SPEC_HEX_OP_DEC_H: // 0x25
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    SPEC_M_DEC(s6_reg_H, s1_reg_F);
                }
                else
                {
                    u8 sp258;

                    sp258 = SPEC_XM_GET_INDEX_R() >> 8;
                    
                    SPEC_M_DEC(sp258, s1_reg_F);

                    if (sp287_index_mode == 1)
                    {
                        sp296_IX = SPEC_MK_16b(sp258, sp296_IX);
                    }
                    else
                    {
                        sp294_IY = SPEC_MK_16b(sp258, sp294_IY);
                    }
                }

                break;
            }

            // LD H, NN
            /***
            * The 8-bit integer N is loaded to H.
            */
            case SPEC_HEX_OP_LD_H_NN: // 0x26
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    s6_reg_H = ptr_spectrum_roms[sp298_PC];
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (ptr_spectrum_roms[sp298_PC] << 8) | (sp296_IX & 0xFF);
                }
                else
                {
                    sp294_IY = (ptr_spectrum_roms[sp298_PC] << 8) | (sp294_IY & 0xFF);
                }

                sp298_PC += 1;

                break;
            }

            // DAA
            /***
            * This instruction conditionally adjusts A for BCD addition
            * and subtraction operations. For addition(ADD, ADC, INC) or
            * subtraction(SUB, SBC, DEC, NEG), the following table indicates
            * the operation being performed:
            * ====================================================
            * |Oper.|C before|Upper|H before|Lower|Number|C after|
            * |     |DAA     |Digit|Daa     |Digit|Added |Daa    |
            * ====================================================
            * | ADD |   0    | 9-0 |   0    | 0-9 |  00  |   0   |
            * |     |   0    | 0-8 |   0    | A-F |  06  |   0   |
            * |     |   0    | 0-9 |   1    | 0-3 |  06  |   0   |
            * |     |   0    | A-F |   0    | 0-9 |  60  |   1   |
            * ----------------------------------------------------
            * | ADC |   0    | 9-F |   0    | A-F |  66  |   1   |
            * ----------------------------------------------------
            * | INC |   0    | A-F |   1    | 0-3 |  66  |   1   |
            * |     |   1    | 0-2 |   0    | 0-9 |  60  |   1   |
            * |     |   1    | 0-2 |   0    | A-F |  66  |   1   |
            * |     |   1    | 0-3 |   1    | 0-3 |  66  |   1   |
            * ----------------------------------------------------
            * | SUB |   0    | 0-9 |   0    | 0-9 |  00  |   0   |
            * ----------------------------------------------------
            * | SBC |   0    | 0-8 |   1    | 6-F |  FA  |   0   |
            * ----------------------------------------------------
            * | DEC |   1    | 7-F |   0    | 0-9 |  A0  |   1   |
            * ----------------------------------------------------
            * | NEG |   1    | 6-7 |   1    | 6-F |  9A  |   1   |
            * ====================================================
            * S is set if most-significant bit of the A is 1 after an
            * operation; otherwise, it is reset.
            * Z is set if A is 0 after an operation; otherwise, it is reset.
            * H: see the DAA instruction table.
            * P/V is set if A is at even parity after an operation;
            * otherwise, it is reset.
            * N is not affected.
            * C: see the DAA instruction table.
            */
            case SPEC_HEX_OP_DAA: // 0x27
            {
                u8 sp257;
                u8 sp256;

                sp28C_clock += SPEC_CPU_CYCLES(4);

                sp256 = s1_reg_F & 1;
                sp257 = 0;

                if ((s1_reg_F & 0x10) || ((s0_reg_A & 0xF) >= 0xA))
                {
                    sp257 = 6;
                }

                if ((s1_reg_F & 1) || (((s32) s0_reg_A >> 4) >= 0xA))
                {
                    sp257 |= 0x60;
                }

                if (s1_reg_F & 2)
                {
                    SPEC_M_SUB(s0_reg_A, sp257, s1_reg_F);
                }
                else
                {
                    if (((s32) s0_reg_A >= 0x91) && ((s0_reg_A & 0xF) >= 0xA))
                    {
                        sp257 |= 0x60;
                    }

                    SPEC_M_ADD(s0_reg_A, sp257, s1_reg_F);
                }

                s1_reg_F = (ptr_pc_keyboard_table_alloc[s0_reg_A] | ((s1_reg_F | sp256) & 0xFB));

                break;
            }

            // JR Z, NN
            /***
            * This instruction provides for conditional branching to
            * other segments of a program depending on the results of a test
            * (Z flag is set). If the test evaluates to *true*, the value of displacement
            * E is added to PC and the next instruction is fetched from the
            * location designated by the new contents of the PC. The jump is
            * measured from the address of the instruction op code and contains
            * a range of –126 to +129 bytes. The assembler automatically adjusts
            * for the twice incremented PC.
            */
            case SPEC_HEX_OP_JR_Z_NN: // 0x28
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (s1_reg_F & 0x40)
                {
                    s32 sp248;

                    sp248 = (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC += sp248 + 1;
                    if(1);

                    sp28C_clock += SPEC_CPU_CYCLES(5);
                }
                else
                {
                    sp298_PC += 1;
                }

                break;
            }

            // ADD HL, HL
            /***
            * The contents of HL are added to the contents of HL and
            * the result is stored in HL.
            * S, Z, P/V are not affected.
            * H is set if carry from bit 11; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 15; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_HL_HL: // 0x29
            {
                u16 sp246;
                
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                if (sp287_index_mode == 0)
                {
                    if (!sp287_index_mode)
                    {
                        SPEC_M_ED_ADD_16(sp246, s6_reg_H, s7_reg_L, s6_reg_H, s7_reg_L, s1_reg_F);
                    }
                    else
                    {
                        SPEC_M_ED_ADD_16_INDEXED(s6_reg_H, s7_reg_L, s1_reg_F);
                    }
                }
                else if (sp287_index_mode == 1)
                {
                    if (!sp287_index_mode)
                    {
                        SPEC_M_ED_ADD_16_b_ALIGN32(s6_reg_H, s7_reg_L, (sp296_IX >> 8), (sp296_IX & 0xff) , s1_reg_F);
                    }
                    else
                    {
                        SPEC_M_ED_ADD_16_INDEXED( ((s32) sp296_IX >> 8), (sp296_IX & 0xff), s1_reg_F);
                    }
                }
                else
                {
                    if (!sp287_index_mode)
                    {
                        SPEC_M_ED_ADD_16_b_ALIGN32(s6_reg_H, s7_reg_L,  (sp294_IY >> 8), (sp294_IY & 0xff), s1_reg_F);
                    }
                    else
                    {
                        SPEC_M_ED_ADD_16_INDEXED(((s32) sp294_IY >> 8), (sp294_IY & 0xff), s1_reg_F);
                    }
                }

                break;
            }

            // LD HL, (HHLL)
            /***
            * The contents of memory address (NN) are loaded to the
            * low-order portion of HL (L), and the contents of the next
            * highest memory address (NN + 1) are loaded to the high-order
            * portion of HL (H).
            */
            case SPEC_HEX_OP_LD_HL_pHHLLp: // 0x2A
            {
                u16 sp22E;

                sp28C_clock += SPEC_CPU_CYCLES(0x10);

                sp22E = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                sp298_PC += 2;
                
                if (sp287_index_mode == 0)
                {
                    s7_reg_L = ptr_spectrum_roms[sp22E + 0];
                    s6_reg_H = ptr_spectrum_roms[sp22E + 1];
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = SPEC_MK_16(ptr_spectrum_roms[sp22E + 1], ptr_spectrum_roms[sp22E]);
                }
                else
                {
                    sp294_IY = SPEC_MK_16(ptr_spectrum_roms[sp22E + 1], ptr_spectrum_roms[sp22E]);
                }

                break;
            }

            // DEC HL
            /***
            * The contents of register pair HL are decremented.
            */
            case SPEC_HEX_OP_DEC_HL: // 0x2B
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);

                if (sp287_index_mode == 0)
                {
                    if (s7_reg_L-- == 0)
                    {
                        s6_reg_H--;
                    }
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX -= 1;
                }
                else
                {
                    sp294_IY -= 1;
                }

                break;
            }

            // INC L
            /***
            * Register L is incremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if r was 7Fh before operation; otherwise, it is reset.
            * N is reset.
            * C is not affected.
            */
            case SPEC_HEX_OP_INC_L: // 0x2C
            {
                u8 sp22D;

                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    SPEC_M_INC(s7_reg_L, s1_reg_F);
                }
                else
                {
                    sp22D = SPEC_XM_GET_INDEX_R();

                    SPEC_M_INC(sp22D, s1_reg_F);

                    if (sp287_index_mode == 1)
                    {
                        sp296_IX = SPEC_MK_16c(sp296_IX, sp22D);
                    }
                    else
                    {
                        sp294_IY = SPEC_MK_16c(sp294_IY, sp22D);
                    }
                }

                break;
            }

            // DEC L
            /***
            * Register L is decremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4, otherwise, it is reset.
            * P/V is set if m was 80h before operation; otherwise, it is reset.
            * N is set.
            * C is not affected.
            */
            case SPEC_HEX_OP_DEC_L: // 0x2D
            {
                u8 sp22C;

                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    SPEC_M_DEC(s7_reg_L, s1_reg_F);
                }
                else
                {
                    sp22C = SPEC_XM_GET_INDEX_R();

                    SPEC_M_DEC(sp22C, s1_reg_F);

                    if (sp287_index_mode == 1)
                    {
                        sp296_IX = SPEC_MK_16c(sp296_IX, sp22C);
                    }
                    else
                    {
                        sp294_IY = SPEC_MK_16c(sp294_IY, sp22C);
                    }
                }

                break;
            }

            // LD L, NN
            /***
            * The 8-bit integer N is loaded to H.
            */
            case SPEC_HEX_OP_LD_L_NN: // 0x2E
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L = ptr_spectrum_roms[sp298_PC];
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = ptr_spectrum_roms[sp298_PC] | (sp296_IX & 0xFF00);
                }
                else
                {
                    sp294_IY = ptr_spectrum_roms[sp298_PC] | (sp294_IY & 0xFF00);
                }

                sp298_PC += 1;

                break;
            }

            // CPL
            /***
            * The contents of A are inverted (one's complement).
            * S, Z, P/V, C are not affected.
            * H and N are set.
            */
            case SPEC_HEX_OP_CPL: // 0x2F
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = ~s0_reg_A;
                s1_reg_F = (s1_reg_F & 0xC5) | (s0_reg_A & 0x28) | 0x12;

                break;
            }

            // JR NC, NN
            /***
            * This instruction provides for conditional branching to
            * other segments of a program depending on the results of a test
            * (C flag is not set). If the test evaluates to *true*, the value of displacement
            * E is added to PC and the next instruction is fetched from the
            * location designated by the new contents of the PC. The jump is
            * measured from the address of the instruction op code and contains
            * a range of –126 to +129 bytes. The assembler automatically adjusts
            * for the twice incremented PC.
            */
            case SPEC_HEX_OP_JR_NC_NN: // 0x30
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (s1_reg_F & 1)
                {
                    sp298_PC += 1;
                }
                else
                {
                    s32 sp228 = (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC += sp228 + 1;
                    sp28C_clock += SPEC_CPU_CYCLES(5);
                }

                break;
            }

            // LD SP, HHLL
            /***
            * The 16-bit integer value is loaded to the SP register pair.
            */
            case SPEC_HEX_OP_LD_SP_HHLL: // 0x31
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                sp292_SP = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                sp298_PC += 2;

                break;
            }

            // LD (HHLL), A
            /***
            * The contents of A are loaded to the memory address specified by
            * the operand NN
            */
            case SPEC_HEX_OP_LD_pHHLLp_A: // 0x32
            {
                u16 sp226;

                sp28C_clock += SPEC_CPU_CYCLES(0xd);

                sp226 = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                sp298_PC += 2;

                SPEC_M_MEM_WRITE(sp226, s0_reg_A);
                
                break;
            }

            // INC SP
            /***
            * The contents of register pair SP are incremented.
            */
            case SPEC_HEX_OP_INC_SP: // 0x33
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);

                sp292_SP += 1;

                break;
            }

            // INC (HL)
            /***
            * The byte contained in the address specified by the contents HL
            * is incremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if (HL) was 0x7F before operation; otherwise, it is reset.
            * N is reset.
            * C is not affected.
            */
            case SPEC_HEX_OP_INC_pHLp: // 0x34
            {
                u16 sp224;
                u8 sp223;

                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                if (sp287_index_mode == 0)
                {
                    sp224 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp224 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                sp223 = ptr_spectrum_roms[sp224];
                
                SPEC_M_INC(sp223, s1_reg_F);

                SPEC_M_MEM_WRITE(sp224, sp223);

                break;
            }

            // DEC (HL)
            /***
            * The byte contained in the address specified by the contents HL
            * is decremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if (HL) was 0x80 before operation; otherwise, it is reset.
            * N is set.
            * C is not affected.
            */
            case SPEC_HEX_OP_DEC_pHLp: // 0x35
            {
                u16 sp220;
                u8 sp21F;

                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                if (sp287_index_mode == 0)
                {
                    sp220 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp220 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_DEC_FROM_MEM(sp21F, ptr_spectrum_roms[sp220], s1_reg_F);

                SPEC_M_MEM_WRITE(sp220, sp21F);

                break;
            }

            // LD (HL), NN
            /***
            * The N 8-bit value is loaded to the memory address specified by HL.
            */
            case SPEC_HEX_OP_LD_pHLp_NN: // 0x36
            {
                u16 sp21C;

                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (sp287_index_mode == 0)
                {
                    sp21C = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(5);
                    
                    sp21C = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp21C, ptr_spectrum_roms[sp298_PC]);

                sp298_PC += 1;

                break;
            }

            // SCF
            /***
            * The Carry flag in F is set.
            * Other flags are not affected.
            */
            case SPEC_HEX_OP_SCF: // 0x37
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);
                
                s1_reg_F = ((s1_reg_F & 0xC4) | 1 | (s0_reg_A & 0x28));

                break;
            }

            // JR C, NN
            /***
            * This instruction provides for conditional branching to
            * other segments of a program depending on the results of a test
            * (C flag is set). If the test evaluates to *true*, the value of displacement
            * E is added to PC and the next instruction is fetched from the
            * location designated by the new contents of the PC. The jump is
            * measured from the address of the instruction op code and contains
            * a range of –126 to +129 bytes. The assembler automatically adjusts
            * for the twice incremented PC.
            */
            case SPEC_HEX_OP_JR_C_NN: // 0x38
            {
                s32 sp218;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (s1_reg_F & 1)
                {
                    sp218 = (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC += sp218 + 1;
                    
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(5);
                }
                else
                {
                    sp298_PC++;
                }

                break;
            }

            // ADD HL, SP
            /***
            * The contents of SP are added to the contents of HL and
            * the result is stored in HL.
            * S, Z, P/V are not affected.
            * H is set if carry from bit 11; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 15; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_HL_SP: // 0x39
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                if (sp287_index_mode == 0)
                {
                    SPEC_M_ED_ADD_16_b_ALIGN32(s6_reg_H, s7_reg_L, (s32)sp292_SP >> 8, (sp292_SP & 0xff), s1_reg_F);
                }
                else
                {
                    SPEC_M_ED_ADD_16_INDEXED((s32) sp292_SP >> 8, (sp292_SP & 0xff), s1_reg_F);
                }

                break;
            }

            // LD A, (HHLL)
            /***
            * The contents of the memory location specified by the operands
            * NN are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_pHHLLp: // 0x3A
            {
                u16 sp20E;

                sp28C_clock += SPEC_CPU_CYCLES(0xd);

                sp20E = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                sp298_PC += 2;
                s0_reg_A = ptr_spectrum_roms[sp20E];

                break;
            }

            // DEC SP
            /***
            * The contents of register pair SP are decremented.
            */
            case SPEC_HEX_OP_DEC_SP: // 0x3B
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);

                sp292_SP -= 1;

                break;
            }

            // INC A
            /***
            * Register A is incremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if r was 7Fh before operation; otherwise, it is reset.
            * N is reset.
            * C is not affected.
            */
            case SPEC_HEX_OP_INC_A: // 0x3C
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_INC(s0_reg_A, s1_reg_F);

                break;
            }

            // DEC A
            /***
            * Register A is decremented.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4, otherwise, it is reset.
            * P/V is set if m was 80h before operation; otherwise, it is reset.
            * N is set.
            * C is not affected.
            */
            case SPEC_HEX_OP_DEC_A: // 0x3D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_DEC(s0_reg_A, s1_reg_F);

                break;
            }

            // LD A, NN
            /***
            * The 8-bit integer N is loaded to A.
            */
            case SPEC_HEX_OP_LD_A_NN: // 0x3E
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                s0_reg_A = ptr_spectrum_roms[sp298_PC];
                sp298_PC += 1;

                break;
            }

            // CCF
            /***
            * The Carry flag in F is inverted.
            * Other flags are not affected.
            */
            case SPEC_HEX_OP_CCF: // 0x3F
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s1_reg_F = ((s1_reg_F & 0xC4)
                    | ((s1_reg_F & 1) ^ 1)
                    | ((s1_reg_F & 1) << 4)
                    | (s0_reg_A & 0x28)
                );

                break;
            }

            // LD B, B
            /***
            *
            */
            case SPEC_HEX_OP_LD_B_B: // 0x40
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                break;
            }

            // LD B, C
            /***
            * The contents of C are loaded to B.
            */
            case SPEC_HEX_OP_LD_B_C: // 0x41
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s2_reg_B = s3_reg_C;

                break;
            }

            // LD B, D
            /***
            * The contents of D are loaded to B.
            */
            case SPEC_HEX_OP_LD_B_D: // 0x42
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s2_reg_B = s4_reg_D;

                break;
            }

            // LD B, E
            /***
            *  The contents of E are loaded to B.
            */
            case SPEC_HEX_OP_LD_B_E: // 0x43
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s2_reg_B = s5_reg_E;

                break;
            }

            // LD B, H
            /***
            * The contents of H are loaded to B.
            */
            case SPEC_HEX_OP_LD_B_H: // 0x44
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s2_reg_B = SPEC_XM_GET_INDEX_HI();

                break;
            }

            // LD B, L
            /***
            * The contents of L are loaded to B.
            */
            case SPEC_HEX_OP_LD_B_L: // 0x45
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s2_reg_B = SPEC_XM_GET_INDEX_LO();
                
                break;
            }

            // LD B, (HL)
            /***
            * The 8-bit contents of memory location (HL) are loaded to B.
            */
            case SPEC_HEX_OP_LD_B_pHLp: // 0x46
            {
                u16 sp20C;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp20C = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);
                    
                    sp20C = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s2_reg_B = ptr_spectrum_roms[sp20C];

                break;
            }

            // LD B, A
            /***
            * The contents of A are loaded to B.
            */
            case SPEC_HEX_OP_LD_B_A: // 0x47
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s2_reg_B = s0_reg_A;

                break;
            }

            // LD C, B
            /***
            * The contents of B are loaded to C.
            */
            case SPEC_HEX_OP_LD_C_B: // 0x48
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s3_reg_C = s2_reg_B;

                break;
            }

            // LD C, C
            /***
            *
            */
            case SPEC_HEX_OP_LD_C_C: // 0x49
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                break;
            }

            // LD C, D
            /***
            * The contents of D are loaded to C.
            */
            case SPEC_HEX_OP_LD_C_D: // 0x4A
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s3_reg_C = s4_reg_D;

                break;
            }

            // LD C, E
            /***
            * The contents of E are loaded to C.
            */
            case SPEC_HEX_OP_LD_C_E: // 0x4B
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s3_reg_C = s5_reg_E;

                break;
            }

            // LD C, H
            /***
            * The contents of H are loaded to C.
            */
            case SPEC_HEX_OP_LD_C_H: // 0x4C
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s3_reg_C = SPEC_XM_GET_INDEX_HI();

                break;
            }

            // LD C, L
            /***
            * The contents of L are loaded to C.
            */
            case SPEC_HEX_OP_LD_C_L: // 0x4D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s3_reg_C = SPEC_XM_GET_INDEX_LO();

                break;
            }

            // LD C, (HL)
            /***
            * The 8-bit contents of memory location (HL) are loaded to C.
            */
            case SPEC_HEX_OP_LD_C_pHLp: // 0x4E
            {
                u16 sp20A;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp20A = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp20A = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s3_reg_C = ptr_spectrum_roms[sp20A];

                break;
            }

            // LD C, A
            /***
            * The contents of A are loaded to C.
            */
            case SPEC_HEX_OP_LD_C_A: // 0x4F
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s3_reg_C = s0_reg_A;

                break;
            }

            // LD D, B
            /***
            * The contents of B are loaded to D.
            */
            case SPEC_HEX_OP_LD_D_B: // 0x50
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s4_reg_D = s2_reg_B;

                break;
            }

            // LD D, C
            /***
            * The contents of C are loaded to D.
            */
            case SPEC_HEX_OP_LD_D_C: // 0x51
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s4_reg_D = s3_reg_C;

                break;
            }

            // LD D, D
            /***
            * 
            */
            case SPEC_HEX_OP_LD_D_D: // 0x52
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                break;
            }

            // LD D, E
            /***
            * The contents of E are loaded to D.
            */
            case SPEC_HEX_OP_LD_D_E: // 0x53
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s4_reg_D = s5_reg_E;

                break;
            }

            // LD D, H
            /***
            * The contents of H are loaded to D.
            */
            case SPEC_HEX_OP_LD_D_H: // 0x54
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s4_reg_D = SPEC_XM_GET_INDEX_HI();

                break;
            }

            // LD D, L
            /***
            * The contents of L are loaded to D.
            */
            case SPEC_HEX_OP_LD_D_L: // 0x55
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s4_reg_D = SPEC_XM_GET_INDEX_LO();

                break;
            }

            // LD D, (HL)
            /***
            * The 8-bit contents of memory location (HL) are loaded to D.
            */
            case SPEC_HEX_OP_LD_D_pHLp: // 0x56
            {
                u16 sp208;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp208 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp208 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s4_reg_D = ptr_spectrum_roms[sp208];

                break;
            }

            // LD D, A
            /***
            * The contents of A are loaded to D.
            */
            case SPEC_HEX_OP_LD_D_A: // 0x57
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s4_reg_D = s0_reg_A;

                break;
            }

            // LD E, B
            /***
            * The contents of B are loaded to E.
            */
            case SPEC_HEX_OP_LD_E_B: // 0x58
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s5_reg_E = s2_reg_B;

                break;
            }

            // LD E, C
            /***
            * The contents of C are loaded to E.
            */
            case SPEC_HEX_OP_LD_E_C: // 0x59
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s5_reg_E = s3_reg_C;

                break;
            }

            // LD E, D
            /***
            * The contents of D are loaded to E.
            */
            case SPEC_HEX_OP_LD_E_D: // 0x5A
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s5_reg_E = s4_reg_D;

                break;
            }

            // LD E, E
            /***
            *
            */
            case SPEC_HEX_OP_LD_E_E: // 0x5B
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                break;
            }

            // LD E, H
            /***
            * The contents of H are loaded to E.
            */
            case SPEC_HEX_OP_LD_E_H: // 0x5C
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s5_reg_E = SPEC_XM_GET_INDEX_HI();

                break;
            }

            // LD E, L
            /***
            * The contents of L are loaded to E.
            */
            case SPEC_HEX_OP_LD_E_L: // 0x5D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s5_reg_E = SPEC_XM_GET_INDEX_LO();

                break;
            }

            // LD E, (HL)
            /***
            * The 8-bit contents of memory location (HL) are loaded to E.
            */
            case SPEC_HEX_OP_LD_E_pHLp: // 0x5E
            {
                u16 sp206;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp206 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp206 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s5_reg_E = ptr_spectrum_roms[sp206];

                break;
            }

            // LD E, A
            /***
            * The contents of A are loaded to E.
            */
            case SPEC_HEX_OP_LD_E_A: // 0x5F
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s5_reg_E = s0_reg_A;

                break;
            }

            // LD H, B
            /***
            * The contents of B are loaded to H.
            */
            case SPEC_HEX_OP_LD_H_B: // 0x60
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s6_reg_H = s2_reg_B;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF) | (s2_reg_B << 8);
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF) | (s2_reg_B << 8);
                }

                break;
            }

            // LD H, C
            /***
            * The contents of C are loaded to H.
            */
            case SPEC_HEX_OP_LD_H_C: // 0x61
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s6_reg_H = s3_reg_C;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF) | (s3_reg_C << 8);
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF) | (s3_reg_C << 8);
                }

                break;
            }

            // LD H, D
            /***
            * The contents of D are loaded to H.
            */
            case SPEC_HEX_OP_LD_H_D: // 0x62
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s6_reg_H = s4_reg_D;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF) | (s4_reg_D << 8);
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF) | (s4_reg_D << 8);
                }

                break;
            }

            // LD H, E
            /***
            * The contents of E are loaded to H.
            */
            case SPEC_HEX_OP_LD_H_E: // 0x63
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s6_reg_H = s5_reg_E;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF) | (s5_reg_E << 8);
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF) | (s5_reg_E << 8);
                }

                break;
            }

            // LD H, H
            /***
            *
            */
            case SPEC_HEX_OP_LD_H_H: // 0x64
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                break;
            }

            // LD H, L
            /***
            * The contents of L are loaded to H.
            */
            case SPEC_HEX_OP_LD_H_L: // 0x65
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                // crime against humanity here
                (sp287_index_mode == 0)
                    ? (s6_reg_H = SPEC_XM_GET_INDEX_LO())
                    : (
                    (sp287_index_mode == 1)
                        ? (sp296_IX = (SPEC_XM_GET_INDEX_LO() << 8) | (sp296_IX & 0xFF))
                        : (sp294_IY = (SPEC_XM_GET_INDEX_LO() << 8) | (sp294_IY & 0xFF)));

                break;
            }

            // LD H, (HL)
            /***
            * The 8-bit contents of memory location (HL) are loaded to H.
            */
            case SPEC_HEX_OP_LD_H_pHLp: // 0x66
            {
                u16 sp204;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp204 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp204 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s6_reg_H = ptr_spectrum_roms[sp204];

                break;
            }

            // LD H, A
            /***
            * The contents of A are loaded to H.
            */
            case SPEC_HEX_OP_LD_H_A: // 0x67
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s6_reg_H = s0_reg_A;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF) | (s0_reg_A << 8);
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF) | (s0_reg_A << 8);
                }

                break;
            }

            // LD L, B
            /***
            * The contents of B are loaded to L.
            */
            case SPEC_HEX_OP_LD_L_B: // 0x68
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L = s2_reg_B;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF00) | s2_reg_B;
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF00) | s2_reg_B;
                }

                break;
            }

            // LD L, C
            /***
            * The contents of C are loaded to L.
            */
            case SPEC_HEX_OP_LD_L_C: // 0x69
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L = s3_reg_C;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF00) | s3_reg_C;
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF00) | s3_reg_C;
                }

                break;
            }

            // LD L, D
            /***
            * The contents of D are loaded to L.
            */
            case SPEC_HEX_OP_LD_L_D: // 0x6A
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L = s4_reg_D;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF00) | s4_reg_D;
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF00) | s4_reg_D;
                }

                break;
            }

            // LD L, E
            /***
            * The contents of E are loaded to L.
            */
            case SPEC_HEX_OP_LD_L_E: // 0x6B
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                
                if (sp287_index_mode == 0)
                {
                    s7_reg_L = s5_reg_E;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF00) | s5_reg_E;
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF00) | s5_reg_E;
                }

                break;
            }

            // LD L, H
            /***
            * The contents of H are loaded to L.
            */
            case SPEC_HEX_OP_LD_L_H: // 0x6C
            {

                sp28C_clock += SPEC_CPU_CYCLES(4);

                // crime against humanity here
                (sp287_index_mode == 0)
                    ? (s7_reg_L = SPEC_XM_GET_INDEX_HI())
                    : (
                    (sp287_index_mode == 1)
                        ? (sp296_IX = SPEC_XM_GET_INDEX_HI() | (sp296_IX & 0xFF00))
                        : (sp294_IY = SPEC_XM_GET_INDEX_HI() | (sp294_IY & 0xFF00)));

                break;
            }

            // LD L, L
            /***
            *
            */
            case SPEC_HEX_OP_LD_L_L: // 0x6D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                break;
            }

            // LD L, (HL)
            /***
            * The 8-bit contents of memory location (HL) are loaded to L.
            */
            case SPEC_HEX_OP_LD_L_pHLp: // 0x6E
            {
                u16 sp202;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp202 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp202 = SPEC_XM_GET_INDEX_R() + (s8)ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s7_reg_L = ptr_spectrum_roms[sp202];

                break;
            }

            // LD L, A
            /***
            * The contents of A are loaded to L.
            */
            case SPEC_HEX_OP_LD_L_A: // 0x6F
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L = s0_reg_A;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = (sp296_IX & 0xFF00) | s0_reg_A;
                }
                else
                {
                    sp294_IY = (sp294_IY & 0xFF00) | s0_reg_A;
                }

                break;
            }

            // LD (HL), B
            /***
            * The contents of B are loaded to the memory location specified
            * by the contents of HL.
            */
            case SPEC_HEX_OP_LD_pHLp_B: // 0x70
            {
                u16 sp200;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp200 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp200 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp200, s2_reg_B);

                break;
            }

            // LD (HL), C
            /***
            * The contents of C are loaded to the memory location specified
            * by the contents of HL.
            */
            case SPEC_HEX_OP_LD_pHLp_C: // 0x71
            {
                u16 sp1FE;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1FE = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1FE = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp1FE, s3_reg_C);

                break;
            }

            // LD (HL), D
            /***
            * The contents of D are loaded to the memory location specified
            * by the contents of HL.
            */
            case SPEC_HEX_OP_LD_pHLp_D: // 0x72
            {
                u16 sp1FC;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1FC = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1FC = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp1FC, s4_reg_D);

                break;
            }

            // LD (HL), E
            /***
            * The contents of E are loaded to the memory location specified
            * by the contents of HL.
            */
            case SPEC_HEX_OP_LD_pHLp_E: // 0x73
            {
                u16 sp1FA;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1FA = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1FA = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp1FA, s5_reg_E);

                break;
            }

            // LD (HL), H
            /***
            * The contents of H are loaded to the memory location specified
            * by the contents of HL.
            */
            case SPEC_HEX_OP_LD_pHLp_H: // 0x74
            {
                u16 sp1F8;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1F8 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1F8 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp1F8, s6_reg_H);

                break;
            }

            // LD (HL), L
            /***
            * The contents of L are loaded to the memory location specified
            * by the contents of HL.
            */
            case SPEC_HEX_OP_LD_pHLp_L: // 0x75
            {
                u16 sp1F6;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1F6 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1F6 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp1F6, s7_reg_L);

                break;
            }

            // HALT
            /***
            * The HALT instruction suspends CPU operation until a subsequent
            * interrupt or reset is received.While in the HALT state,
            * the processor executes NOPs to maintain memory refresh logic.
            */
            case SPEC_HEX_OP_HALT: // 0x76
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp28C_clock < sp280_unk_clock)
                {
                    sp28C_clock += (((sp280_unk_clock - sp28C_clock) + 3) & ~3);
                }

                sp298_PC -= 1;

                break;
            }

            // LD (HL), A
            /***
            * The contents of A are loaded to the memory location specified
            * by the contents of HL.
            */
            case SPEC_HEX_OP_LD_pHLp_A: // 0x77
            {
                u16 sp1F4;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1F4 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1F4 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_MEM_WRITE(sp1F4, s0_reg_A);

                break;
            }

            // LD A, B
            /***
            * The contents of B are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_B: // 0x78
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = s2_reg_B;

                break;
            }

            // LD A, C
            /***
            * The contents of C are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_C: // 0x79
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = s3_reg_C;

                break;
            }

            // LD A, D
            /***
            * The contents of D are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_D: // 0x7A
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = s4_reg_D;

                break;
            }

            // LD A, E
            /***
            * The contents of E are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_E: // 0x7B
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = s5_reg_E;

                break;
            }

            // LD A, H
            /***
            * The contents of H are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_H: // 0x7C
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = SPEC_XM_GET_INDEX_HI();

                break;
            }

            // LD A, L
            /***
            * The contents of L are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_L: // 0x7D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = SPEC_XM_GET_INDEX_LO();

                break;
            }

            // LD A, (HL)
            /***
            * The 8-bit contents of memory location (HL) are loaded to A.
            */
            case SPEC_HEX_OP_LD_A_pHLp: // 0x7E
            {
                u16 sp1F2;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1F2 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1F2 = SPEC_XM_GET_INDEX_R() +  (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s0_reg_A = ptr_spectrum_roms[sp1F2];

                break;
            }

            // LD A, A
            /***
            *
            */
            case SPEC_HEX_OP_LD_A_A: // 0x7F
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                break;
            }

            // ADD A, B
            /***
            * The contents of B are added to the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_B: // 0x80
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADD(s0_reg_A, s2_reg_B, s1_reg_F);

                break;
            }

            // ADD A, C
            /***
            * The contents of C are added to the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_C: // 0x81
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADD(s0_reg_A, s3_reg_C, s1_reg_F);

                break;
            }

            // ADD A, D
            /***
            * The contents of D are added to the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_D: // 0x82
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADD(s0_reg_A, s4_reg_D, s1_reg_F);

                break;
            }

            // ADD A, E
            /***
            * The contents of E are added to the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_E: // 0x83
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADD(s0_reg_A, s5_reg_E, s1_reg_F);

                break;
            }

            // ADD A, H
            /***
            * The contents of H are added to the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_H: // 0x84
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADD(s0_reg_A, SPEC_XM_GET_INDEX_HI(), s1_reg_F);

                break;
            }

            // ADD A, L
            /***
            * The contents of L are added to the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_L: // 0x85
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADD(s0_reg_A, SPEC_XM_GET_INDEX_LO(), s1_reg_F);

                break;
            }

            // ADD A, (HL)
            /***
            * The byte at the memory address specified by the contents of HL
            * is added to the contents of A, and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_pHLp: // 0x86
            {
                u16 sp1D8;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1D8 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1D8 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_ADD(s0_reg_A, ptr_spectrum_roms[sp1D8], s1_reg_F);

                break;
            }

            // ADD A, A
            /***
            * The contents of A are added to the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADD_A_A: // 0x87
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADD(s0_reg_A, s0_reg_A, s1_reg_F);

                break;
            }

            // ADC A, B
            /***
            * The contents of B and the C flag are added to the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_B: // 0x88
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);
                
                SPEC_M_ADC(s0_reg_A, s2_reg_B, s1_reg_F);

                break;
            }

            // ADC A, C
            /***
            * The contents of C and the C flag are added to the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_C: // 0x89
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADC(s0_reg_A, s3_reg_C, s1_reg_F);

                break;
            }

            // ADC A, D
            /***
            * The contents of D and the C flag are added to the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_D: // 0x8A
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADC(s0_reg_A, s4_reg_D, s1_reg_F);

                break;
            }

            // ADC A, E
            /***
            * The contents of E and the C flag are added to the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_E: // 0x8B
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADC(s0_reg_A, s5_reg_E, s1_reg_F);

                break;
            }

            // ADC A, H
            /***
            * The contents of H and the C flag are added to the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_H: // 0x8C
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADC(s0_reg_A, SPEC_XM_GET_INDEX_HI(), s1_reg_F);
                
                break;
            }

            // ADC A, L
            /***
            * The contents of L and the C flag are added to the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_L: // 0x8D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADC(s0_reg_A, SPEC_XM_GET_INDEX_LO(), s1_reg_F);

                break;
            }

            // ADC A, (HL)
            /***
            * The byte at the memory address specified by the contents of HL
            * and the C flag is added to the contents of A, and the
            * result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_pHLp: // 0x8E
            {
                u16 sp1B6;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp1B6 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp1B6 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_ADC(s0_reg_A, ptr_spectrum_roms[sp1B6], s1_reg_F);

                break;
            }

            // ADC A, A
            /***
            * The contents of A and the C flag are added to the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if carry from bit 3; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is reset.
            * C is set if carry from bit 7; otherwise, it is reset.
            */
            case SPEC_HEX_OP_ADC_A_A: // 0x8F
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_ADC(s0_reg_A, s0_reg_A, s1_reg_F);

                break;
            }

            // SUB A, B
            /***
            * The contents of B are subtracted from the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_B: // 0x90
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SUB(s0_reg_A, s2_reg_B, s1_reg_F);

                break;
            }

            // SUB A, C
            /***
            * The contents of C are subtracted from the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_C: // 0x91
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SUB(s0_reg_A, s3_reg_C, s1_reg_F);

                break;
            }

            // SUB A, D
            /***
            * The contents of D are subtracted from the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_D: // 0x92
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SUB(s0_reg_A, s4_reg_D, s1_reg_F);

                break;
            }

            // SUB A, E
            /***
            * The contents of E are subtracted from the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_E: // 0x93
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SUB(s0_reg_A, s5_reg_E, s1_reg_F);

                break;
            }

            // SUB A, H
            /***
            * The contents of H are subtracted from the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_H: // 0x94
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SUB(s0_reg_A, SPEC_XM_GET_INDEX_HI(), s1_reg_F);

                break;
            }

            // SUB A, L
            /***
            * The contents of L are subtracted from the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_L: // 0x95
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SUB(s0_reg_A, SPEC_XM_GET_INDEX_LO(), s1_reg_F);

                break;
            }

            // SUB A, (HL)
            /***
            * The byte at the memory address specified by the contents of HL
            * is subtracted from the contents of A, and the
            * result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_pHLp: // 0x96
            {
                u16 sp194;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp194 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp194 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }
                
                SPEC_M_SUB(s0_reg_A, ptr_spectrum_roms[sp194], s1_reg_F);

                break;
            }

            // SUB A, A
            /***
            * The contents of A are subtracted from the contents of A, and the result is
            * stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SUB_A_A: // 0x97
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SUB(s0_reg_A, s0_reg_A, s1_reg_F);

                break;
            }

            // SBC A, B
            /***
            * The contents of B and the C flag are subtracted from the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_B: // 0x98
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SCB_ALIGN32(s0_reg_A, s2_reg_B, s1_reg_F);

                break;
            }

            // SBC A, C
            /***
            * The contents of C and the C flag are subtracted from the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_C: // 0x99
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SCB_ALIGN32(s0_reg_A, s3_reg_C, s1_reg_F);
                
                break;
            }

            // SBC A, D
            /***
            * The contents of D and the C flag are subtracted from the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_D: // 0x9A
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SCB_ALIGN32(s0_reg_A, s4_reg_D, s1_reg_F);

                break;
            }

            // SBC A, E
            /***
            * The contents of E and the C flag are subtracted from the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_E: // 0x9B
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SCB_ALIGN32(s0_reg_A, s5_reg_E, s1_reg_F);

                break;
            }

            // SBC A, H
            /***
            * The contents of H and the C flag are subtracted from the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_H: // 0x9C
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SCB_ALIGN32(s0_reg_A, SPEC_XM_GET_INDEX_HI(), s1_reg_F);

                break;
            }

            // SBC A, L
            /***
            * The contents of L and the C flag are subtracted from the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_L: // 0x9D
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SCB_ALIGN32(s0_reg_A, SPEC_XM_GET_INDEX_LO(), s1_reg_F);

                break;
            }

            // SBC A, (HL)
            /***
            * The byte at the memory address specified by the contents of HL
            * and the C flag is subtracted from the contents of A, and the
            * result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_pHLp: // 0x9E
            {
                u16 sp172;
                u16 sp170;
                u8 sp16F;

                sp28C_clock += SPEC_CPU_CYCLES(7);
                
                if (sp287_index_mode == 0)
                {
                    sp172 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp172 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_SCB(sp16F, sp170, s0_reg_A, ptr_spectrum_roms[sp172], s1_reg_F); 

                break;
            }

            // SBC A, A
            /***
            * The contents of A and the C flag are subtracted from the contents of A,
            * and the result is stored in A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_SBC_A_A: // 0x9F
            {
                u16 sp16C;
                u8 sp16B;

                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_SCB(sp16B, sp16C, s0_reg_A, s0_reg_A, s1_reg_F);

                break;
            }

            // AND B
            /***
            * A logical AND operation is performed between B and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_B: // 0xA0
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);
                
                s0_reg_A &= s2_reg_B;
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // AND C
            /***
            * A logical AND operation is performed between C and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_C: // 0xA1
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A &= s3_reg_C;
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // AND D
            /***
            * A logical AND operation is performed between D and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_D: // 0xA2
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A &= s4_reg_D;
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // AND E
            /***
            * A logical AND operation is performed between E and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_E: // 0xA3
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A &= s5_reg_E;
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // AND H
            /***
            * A logical AND operation is performed between H and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_H: // 0xA4
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A &= SPEC_XM_GET_INDEX_HI();
                
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // AND L
            /***
            * A logical AND operation is performed between L and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_L: // 0xA5
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A &= SPEC_XM_GET_INDEX_LO();
                
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // AND (HL)
            /***
            * A logical AND operation is performed between the byte at the
            * memory address specified by the contents of HL and the byte
            * contained in A; the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_pHLp: // 0xA6
            {
                u16 sp168;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp168 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp168 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                s0_reg_A &= ptr_spectrum_roms[sp168];
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // AND A
            /***
            * A logical AND operation is performed between A and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_AND_A: // 0xA7
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                s0_reg_A = s0_reg_A & s0_reg_A;
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6)
                    | 0x10);

                break;
            }

            // XOR B
            /***
            * A logical XOR operation is performed between B and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_B: // 0xA8
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_XOR(s0_reg_A, s2_reg_B, ptr_pc_keyboard_table_alloc, s1_reg_F);
                
                break;
            }

            // XOR C
            /***
            * A logical XOR operation is performed between C and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_C: // 0xA9
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_XOR(s0_reg_A, s3_reg_C, ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // XOR D
            /***
            * A logical XOR operation is performed between D and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_D: // 0xAA
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_XOR(s0_reg_A, s4_reg_D, ptr_pc_keyboard_table_alloc, s1_reg_F);               

                break;
            }

            // XOR E
            /***
            * A logical XOR operation is performed between E and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_E: // 0xAB
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_XOR(s0_reg_A, s5_reg_E, ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // XOR H
            /***
            * A logical XOR operation is performed between H and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_H: // 0xAC
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_XOR(s0_reg_A, SPEC_XM_GET_INDEX_HI(), ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // XOR L
            /***
            * A logical XOR operation is performed between L and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_L: // 0xAD
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_XOR(s0_reg_A, SPEC_XM_GET_INDEX_LO(), ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // XOR (HL)
            /***
            * A logical XOR operation is performed between the byte at the
            * memory address specified by the contents of HL and the byte
            * contained in A; the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_pHLp: // 0xAE
            {
                u16 sp166;
                
                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp166 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp166 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_XOR(s0_reg_A, ptr_spectrum_roms[sp166], ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // XOR A
            /***
            * A logical XOR operation is performed between A and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_XOR_A: // 0xAF
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_XOR(s0_reg_A, s0_reg_A, ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // OR B
            /***
            * A logical OR operation is performed between B and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_B: // 0xB0
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_OR(s0_reg_A, s2_reg_B, ptr_pc_keyboard_table_alloc, s1_reg_F);
                
                break;
            }

            // OR C
            /***
            * A logical OR operation is performed between C and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_C: // 0xB1
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_OR(s0_reg_A, s3_reg_C, ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // OR D
            /***
            * A logical OR operation is performed between D and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_D: // 0xB2
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_OR(s0_reg_A, s4_reg_D, ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // OR E
            /***
            * A logical OR operation is performed between E and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_E: // 0xB3
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_OR(s0_reg_A, s5_reg_E, ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // OR H
            /***
            * A logical OR operation is performed between H and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_H: // 0xB4
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_OR(s0_reg_A, SPEC_XM_GET_INDEX_HI(), ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // OR L
            /***
            * A logical OR operation is performed between L and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_L: // 0xB5
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_OR(s0_reg_A, SPEC_XM_GET_INDEX_LO(), ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // OR (HL)
            /***
            * A logical OR operation is performed between the byte at the
            * memory address specified by the contents of HL and the byte
            * contained in A; the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_pHLp: // 0xB6
            {
                u16 sp164;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp164 = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp164 = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_OR(s0_reg_A, ptr_spectrum_roms[sp164], ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // OR A
            /***
            * A logical OR operation is performed between A and the byte contained in A;
            * the result is stored in the Accumulator.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is reset.
            * P/V is reset if overflow; otherwise, it is reset.
            * N is reset.
            * C is reset.
            */
            case SPEC_HEX_OP_OR_A: // 0xB7
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_OR(s0_reg_A, s0_reg_A, ptr_pc_keyboard_table_alloc, s1_reg_F);

                break;
            }

            // CP B
            /***
            * The contents of B are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_B: // 0xB8
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_COMPARE(s0_reg_A, s2_reg_B, s1_reg_F);

                break;
            }

            // CP C
            /***
            * The contents of C are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_C: // 0xB9
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);
                
                SPEC_M_COMPARE(s0_reg_A, s3_reg_C, s1_reg_F);

                break;
            }

            // CP D
            /***
            * The contents of D are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_D: // 0xBA
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_COMPARE(s0_reg_A, s4_reg_D, s1_reg_F);

                break;
            }

            // CP E
            /***
            * The contents of E are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_E: // 0xBB
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_COMPARE(s0_reg_A, s5_reg_E, s1_reg_F);

                break;
            }

            // CP H
            /***
            * The contents of H are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_H: // 0xBC
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_COMPARE(s0_reg_A, SPEC_XM_GET_INDEX_HI(), s1_reg_F);

                break;
            }

            // CP L
            /***
            * The contents of L are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_L: // 0xBD
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_COMPARE(s0_reg_A, SPEC_XM_GET_INDEX_LO(), s1_reg_F);

                break;
            }

            // CP (HL)
            /***
            * The contents of the byte at the memory address specified by
            * the contents of HL are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_pHLp: // 0xBE
            {
                u16 sp14A;

                sp28C_clock += SPEC_CPU_CYCLES(7);

                if (sp287_index_mode == 0)
                {
                    sp14A = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }
                else
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp14A = SPEC_XM_GET_INDEX_R() + (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;
                }

                SPEC_M_COMPARE(s0_reg_A, ptr_spectrum_roms[sp14A], s1_reg_F);

                break;
            }

            // CP A
            /***
            * The contents of A are compared with the contents of A.
            * If there is a true compare, the Z flag is set. The execution of
            * this instruction does not affect A.
            * S is set if result is negative; otherwise, it is reset.
            * Z is set if result is 0; otherwise, it is reset.
            * H is set if borrow from bit 4; otherwise, it is reset.
            * P/V is set if overflow; otherwise, it is reset.
            * N is set.
            * C is set if borrow; otherwise, it is reset.
            */
            case SPEC_HEX_OP_CP_A: // 0xBF
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                SPEC_M_COMPARE(s0_reg_A, s0_reg_A, s1_reg_F);

                break;
            }

            // RET NZ
            /***
            * If Z flag is not set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_NZ: // 0xC0
            {
                sp28C_clock += SPEC_CPU_CYCLES(5);

                if (!(s1_reg_F & 0x40))
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(6);

                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);                    
                    sp292_SP += 2;
                }

                break;
            }

            // POP BC
            /***
            * The top two bytes of the external memory last-in, first-out (LIFO)
            * stack are popped to register pair BC. SP holds the 16-bit address
            * of the current top of the stack. This instruction first loads to
            * the low-order portion of RR, the byte at the memory location
            * corresponding to the contents of SP; then SP is incremented and
            * the contents of the corresponding adjacent memory location are
            * loaded to the high-order portion of RR and the SP is now incremented
            * again.
            */
            case SPEC_HEX_OP_POP_BC: // 0xC1
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                s3_reg_C = ptr_spectrum_roms[sp292_SP + 0];
                s2_reg_B = ptr_spectrum_roms[sp292_SP + 1];

                sp292_SP += 2;

                break;
            }

            // JP NZ, HHLL
            /***
            * If Z flag is not set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_NZ_HHLL: // 0xC2
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 0x40))
                {
                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // JP HHLL
            /***
            * Operand NN is loaded to PC. The next instruction is fetched
            * from the location designated by the new contents of the PC.
            */
            case SPEC_HEX_OP_JP_HHLL: // 0xC3
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);

                break;
            }

            // CALL NZ, HHLL
            /***
            * If flag Z is not set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_NZ_HHLL: // 0xC4
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 0x40))
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(7);

                    sp292_SP -= 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (sp298_PC + 2) >> 8);

                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // PUSH BC
            /***
            * The contents of the register pair BC are pushed to the external
            * memory last-in, first-out (LIFO) stack. SP holds the 16-bit
            * address of the current top of the Stack. This instruction first
            * decrements SP and loads the high-order byte of register pair RR
            * to the memory address specified by SP. Then SP is decremented again
            * and loads the low-order byte of RR to the memory location
            * corresponding to this new address in SP.
            */
            case SPEC_HEX_OP_PUSH_BC: // 0xC5
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, s3_reg_C);

                SPEC_M_MEM_WRITE(sp292_SP + 1, s2_reg_B);

                break;
            }

            // ADD A, NN
            /***
            *
            */
            case SPEC_HEX_OP_ADD_A_NN: // 0xC6
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                SPEC_M_ADD(s0_reg_A, ptr_spectrum_roms[sp298_PC], s1_reg_F);
                
                if(1);
                sp298_PC++;

                break;
            }

            // RST 00
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0000H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_00: // 0xC7
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) sp298_PC >> 8);
                
                sp298_PC = 0;

                break;
            }

            // RET Z
            /***
            * If Z flag is set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_Z: // 0xC8
            {
                sp28C_clock += SPEC_CPU_CYCLES(5);

                if (s1_reg_F & 0x40)
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(6);

                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);

                    sp292_SP += 2;
                }

                break;
            }

            // RET
            /***
            * The byte at the memory location specified by the contents of SP
            * is moved to the low-order eight bits of PC. SP is now incremented
            * and the byte at the memory location specified by the new contents
            * of this instruction is fetched from the memory location specified
            * by PC.
            * This instruction is normally used to return to the main line
            * program at the completion of a routine entered by a CALL
            * instruction.
            */
            case SPEC_HEX_OP_RET: // 0xC9
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);
                
                if(1);
                sp28C_clock += SPEC_CPU_CYCLES(6);

                sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);

                sp292_SP += 2;

                break;
            }

            // JP Z, HHLL
            /***
            * If Z flag is set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_Z_HHLL: // 0xCA
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (s1_reg_F & 0x40)
                {
                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // CB Opcodes
            /***
            *
            */
            case SPEC_HEX_OP_CB_Opcodes: // 0xCB
            {
                u16 sp13C;
                u8 sp13B;
                u8 sp13A;
                u8 sp139;

                sp28C_clock += SPEC_CPU_CYCLES(4);

                if (sp287_index_mode)
                {
                    sp13C = ((sp287_index_mode == 1) ? sp296_IX : sp294_IY) +
                        (s8) ptr_spectrum_roms[sp298_PC];
                    sp298_PC++;

                    sp28C_clock += SPEC_CPU_CYCLES(8);

                    sp13B = ptr_spectrum_roms[sp298_PC];
                    sp13A = sp13B & 7;
                    sp13B = (sp13B & 0xF8) | 6;
                }
                else
                {
                    sp13B = ptr_spectrum_roms[sp298_PC];
                    sp28C_clock += SPEC_CPU_CYCLES(4);
                    sp288_unk_R += 1;
                    sp13C = SPEC_MK_16(s6_reg_H, s7_reg_L);
                }

                sp298_PC += 1;

                if (sp13B < 0x40) {
                switch (sp13B)
                {
                    #define SPEC_HEX_OP_CB_RLC_B 0x00
                    #define SPEC_HEX_OP_CB_RLC_C 0x01
                    #define SPEC_HEX_OP_CB_RLC_D 0x02
                    #define SPEC_HEX_OP_CB_RLC_E 0x03
                    #define SPEC_HEX_OP_CB_RLC_H 0x04
                    #define SPEC_HEX_OP_CB_RLC_L 0x05
                    #define SPEC_HEX_OP_CB_RLC_pHLp 0x06
                    #define SPEC_HEX_OP_CB_RLC_A 0x07
                    /**
                     * "RLC (IDR + D),Q" operation
                     * 
                     * 
                     * The contents of the indexed memory address are rotated left 1 bit position. The 
                     * contents of bit 7 are copied to the Carry flag and also to bit 0. The result is
                     * stored in register Q
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 7 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_RLC_B: // 0x00
                    {
                        SPEC_M_CB_RLC(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RLC_C: // 0x01
                    {
                        SPEC_M_CB_RLC(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);
                        
                        break;
                    }
                    case SPEC_HEX_OP_CB_RLC_D: // 0x02
                    {
                        SPEC_M_CB_RLC(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);
                        
                        break;
                    }
                    case SPEC_HEX_OP_CB_RLC_E: // 0x03
                    {
                        SPEC_M_CB_RLC(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RLC_H: // 0x04
                    {
                        SPEC_M_CB_RLC(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RLC_L: // 0x05
                    {
                        SPEC_M_CB_RLC(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RLC_pHLp: // 0x06
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);

                        sp139 = ptr_spectrum_roms[sp13C];

                        SPEC_M_CB_RLC(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);

                        SPEC_M_MEM_WRITE(sp13C, sp139);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RLC_A: // 0x07
                    {
                        SPEC_M_CB_RLC(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }

                    #define SPEC_HEX_OP_CB_RRC_B 0x08
                    #define SPEC_HEX_OP_CB_RRC_C 0x09
                    #define SPEC_HEX_OP_CB_RRC_D 0x0a
                    #define SPEC_HEX_OP_CB_RRC_E 0x0b
                    #define SPEC_HEX_OP_CB_RRC_H 0x0c
                    #define SPEC_HEX_OP_CB_RRC_L 0x0d
                    #define SPEC_HEX_OP_CB_RRC_pHLp 0x0e
                    #define SPEC_HEX_OP_CB_RRC_A 0x0f
                    /**
                     * "RRC (IDR + D),Q" operation
                     *
                     * The contents of the indexed memory address are rotated right 1 bit position. The 
                     * contents of bit 0 are copied to the Carry flag and also to bit 7. The result is
                     * stored in register Q.
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 0 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_RRC_B: // 0x08
                    {
                        SPEC_M_CB_RRC(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RRC_C: // 0x09
                    {
                        SPEC_M_CB_RRC(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RRC_D: // 0x0a
                    {
                        SPEC_M_CB_RRC(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RRC_E: // 0x0b
                    {
                        SPEC_M_CB_RRC(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RRC_H: // 0x0c
                    {
                        SPEC_M_CB_RRC(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RRC_L: // 0x0d
                    {
                        SPEC_M_CB_RRC(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RRC_pHLp: // 0x0e
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);

                        sp139 = ptr_spectrum_roms[sp13C];

                        if(1);
                        SPEC_M_CB_RRC(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);
                        
                        SPEC_M_MEM_WRITE(sp13C, sp139);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RRC_A: // 0x0f
                    {
                        SPEC_M_CB_RRC(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }

                    #define SPEC_HEX_OP_CB_RL_B 0x10
                    #define SPEC_HEX_OP_CB_RL_C 0x11
                    #define SPEC_HEX_OP_CB_RL_D 0x12
                    #define SPEC_HEX_OP_CB_RL_E 0x13
                    #define SPEC_HEX_OP_CB_RL_H 0x14
                    #define SPEC_HEX_OP_CB_RL_L 0x15
                    #define SPEC_HEX_OP_CB_RL_pHLp 0x16
                    #define SPEC_HEX_OP_CB_RL_A 0x17
                    /**
                     * "RL (IDR + D),Q" operation
                     *
                     * The contents of the indexed memory address are rotated left 1 bit position. The 
                     * contents of bit 7 are copied to the Carry flag, and the previous contents of the
                     * Carry flag are copied to bit 0. The result is stored in register Q.
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 7 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_RL_B: // 0x10
                    {
                        SPEC_M_CB_RL(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RL_C: // 0x11
                    {
                        SPEC_M_CB_RL(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RL_D: // 0x12
                    {
                        SPEC_M_CB_RL(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RL_E: // 0x13
                    {
                        SPEC_M_CB_RL(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RL_H: // 0x14
                    {
                        SPEC_M_CB_RL(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);
                            
                        break;
                    }
                    case SPEC_HEX_OP_CB_RL_L: // 0x15
                    {
                        SPEC_M_CB_RL(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RL_pHLp: // 0x16
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);

                        sp139 = ptr_spectrum_roms[sp13C];
                        if(1);
                        SPEC_M_CB_RL2(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);
                        
                        SPEC_M_MEM_WRITE(sp13C, sp139);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RL_A: // 0x17
                    {
                        SPEC_M_CB_RL(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }

                    #define SPEC_HEX_OP_CB_RR_B 0x18
                    #define SPEC_HEX_OP_CB_RR_C 0x19
                    #define SPEC_HEX_OP_CB_RR_D 0x1a
                    #define SPEC_HEX_OP_CB_RR_E 0x1b
                    #define SPEC_HEX_OP_CB_RR_H 0x1c
                    #define SPEC_HEX_OP_CB_RR_L 0x1d
                    #define SPEC_HEX_OP_CB_RR_pHLp 0x1e
                    #define SPEC_HEX_OP_CB_RR_A 0x1f
                    /**
                     * "RR (IDR + D),Q" operation
                     *
                     * The contents of the indexed memory address are rotated right 1 bit position. The 
                     * contents of bit 0 are copied to the Carry flag, and the previous contents of the
                     * Carry flag are copied to bit 7. The result is stored in register Q.
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 0 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_RR_B: // 0x18
                    {
                        SPEC_M_CB_RR(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RR_C: // 0x19
                    {
                        SPEC_M_CB_RR(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RR_D: // 0x1a
                    {
                        SPEC_M_CB_RR(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RR_E: // 0x1b
                    {
                        SPEC_M_CB_RR(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RR_H: // 0x1c
                    {
                        SPEC_M_CB_RR(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RR_L: // 0x1d
                    {
                        SPEC_M_CB_RR(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RR_pHLp: // 0x1e
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);

                        sp139 = ptr_spectrum_roms[sp13C];

                        if(1);

                        SPEC_M_CB_RR2(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);

                        SPEC_M_MEM_WRITE(sp13C, sp139);

                        break;
                    }
                    case SPEC_HEX_OP_CB_RR_A: // 0x1f
                    {
                        SPEC_M_CB_RR(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }


                    #define SPEC_HEX_OP_CB_SLA_B 0x20
                    #define SPEC_HEX_OP_CB_SLA_C 0x21
                    #define SPEC_HEX_OP_CB_SLA_D 0x22
                    #define SPEC_HEX_OP_CB_SLA_E 0x23
                    #define SPEC_HEX_OP_CB_SLA_H 0x24
                    #define SPEC_HEX_OP_CB_SLA_L 0x25
                    #define SPEC_HEX_OP_CB_SLA_pHLp 0x26
                    #define SPEC_HEX_OP_CB_SLA_A 0x27
                    /**
                     * "SLA (IDR + D),Q" operation
                     *
                     * An arithmetic shift left 1 bit position is performed on the 
                     * contents of the indexed memory address. The contents of bit 7 
                     * are copied to the Carry flag. The result is stored in register Q.
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 7 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_SLA_B: // 0x20
                    {
                        SPEC_M_CB_SLA(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLA_C: // 0x21
                    {
                        SPEC_M_CB_SLA(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLA_D: // 0x22
                    {
                        SPEC_M_CB_SLA(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLA_E: // 0x23
                    {
                        SPEC_M_CB_SLA(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLA_H: // 0x24
                    {
                        SPEC_M_CB_SLA(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLA_L: // 0x25
                    {
                        SPEC_M_CB_SLA(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLA_pHLp: // 0x26
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);

                        sp139 = ptr_spectrum_roms[sp13C];

                        if(1);
                        SPEC_M_CB_SLA(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);

                        SPEC_M_MEM_WRITE(sp13C, sp139);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLA_A: // 0x27
                    {
                        SPEC_M_CB_SLA(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }

                    #define SPEC_HEX_OP_CB_SRA_B 0x28
                    #define SPEC_HEX_OP_CB_SRA_C 0x29
                    #define SPEC_HEX_OP_CB_SRA_D 0x2a
                    #define SPEC_HEX_OP_CB_SRA_E 0x2b
                    #define SPEC_HEX_OP_CB_SRA_H 0x2c
                    #define SPEC_HEX_OP_CB_SRA_L 0x2d
                    #define SPEC_HEX_OP_CB_SRA_pHLp 0x2e
                    #define SPEC_HEX_OP_CB_SRA_A 0x2f
                    /**
                     * "SRA (IDR + D),Q" operation
                     *
                     * An arithmetic shift right 1 bit position is performed on the 
                     * contents of the indexed memory address. The contents of bit 0 are 
                     * copied to the Carry flag and the previous contents of bit 7 remain
                     * unchanged. The result is stored in register Q.
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 7 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_SRA_B: // 0x28
                    {
                        SPEC_M_CB_SRA(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);
                        
                        break;
                    }
                    case SPEC_HEX_OP_CB_SRA_C: // 0x29
                    {
                        SPEC_M_CB_SRA(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRA_D: // 0x2a
                    {
                        SPEC_M_CB_SRA(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRA_E: // 0x2b
                    {
                        SPEC_M_CB_SRA(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRA_H: // 0x2c
                    {
                        SPEC_M_CB_SRA(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRA_L: // 0x2d
                    {
                        SPEC_M_CB_SRA(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRA_pHLp: // 0x2e
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);

                        sp139 = ptr_spectrum_roms[sp13C];

                        if(1);
                        SPEC_M_CB_SRA(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);

                        SPEC_M_MEM_WRITE(sp13C, sp139);
                        
                        break;
                    }
                    case SPEC_HEX_OP_CB_SRA_A: // 0x2f
                    {
                        SPEC_M_CB_SRA(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }

                    #define SPEC_HEX_OP_CB_SLS_B 0x30
                    #define SPEC_HEX_OP_CB_SLS_C 0x31
                    #define SPEC_HEX_OP_CB_SLS_D 0x32
                    #define SPEC_HEX_OP_CB_SLS_E 0x33
                    #define SPEC_HEX_OP_CB_SLS_H 0x34
                    #define SPEC_HEX_OP_CB_SLS_L 0x35
                    #define SPEC_HEX_OP_CB_SLS_pHLp 0x36
                    #define SPEC_HEX_OP_CB_SLS_A 0x37
                    /**
                     * "SLL (IDR + D),Q" operation
                     *
                     * A logic shift left 1 bit position is performed on the 
                     * contents of the indexed memory address. The contents of bit 7 
                     * are copied to the Carry flag and bit 0 is set. The result is 
                     * stored in register Q.
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 7 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_SLS_B: // 0x30
                    {
                        SPEC_M_CB_SLS(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLS_C: // 0x31
                    {
                        SPEC_M_CB_SLS(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLS_D: // 0x32
                    {
                        SPEC_M_CB_SLS(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLS_E: // 0x33
                    {
                        SPEC_M_CB_SLS(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLS_H: // 0x34
                    {
                        SPEC_M_CB_SLS(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLS_L: // 0x35
                    {
                        SPEC_M_CB_SLS(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLS_pHLp: // 0x36
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);

                        sp139 = ptr_spectrum_roms[sp13C];

                        if(1);
                        SPEC_M_CB_SLS(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);

                        SPEC_M_MEM_WRITE(sp13C, sp139);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SLS_A: // 0x37
                    {
                        SPEC_M_CB_SLS(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }

                    #define SPEC_HEX_OP_CB_SRL_B 0x38
                    #define SPEC_HEX_OP_CB_SRL_C 0x39
                    #define SPEC_HEX_OP_CB_SRL_D 0x3a
                    #define SPEC_HEX_OP_CB_SRL_E 0x3b
                    #define SPEC_HEX_OP_CB_SRL_H 0x3c
                    #define SPEC_HEX_OP_CB_SRL_L 0x3d
                    #define SPEC_HEX_OP_CB_SRL_pHLp 0x3e
                    #define SPEC_HEX_OP_CB_SRL_A 0x3f
                    /**
                     * "SRR (IDR + D),Q" operation
                     *
                     * The contents of the indexed memory address are shifted right 1 
                     * bit position. The contents of bit 0 are copied to the Carry flag, 
                     * and bit 7 is reset. The result is stored in register Q.
                     * 
                     * S is set if result is negative; otherwise, it is reset.
                     * Z is set if result is 0; otherwise, it is reset.
                     * P/V is set if parity even; otherwise, it is reset.
                     * H, N are reset.
                     * C is data from bit 0 of the source byte.
                     * 
                     * Q: 000=B, 001=C, 010=D, 011=E
                     * 100=H, 101=L, 110=N/A, 111=A
                    */
                    case SPEC_HEX_OP_CB_SRL_B: // 0x38
                    {
                        SPEC_M_CB_SRL(s2_reg_B, ptr_pc_keyboard_table_alloc[s2_reg_B], s1_reg_F);
                        
                        break;
                    }
                    case SPEC_HEX_OP_CB_SRL_C: // 0x39
                    {
                        SPEC_M_CB_SRL(s3_reg_C, ptr_pc_keyboard_table_alloc[s3_reg_C], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRL_D: // 0x3a
                    {
                        SPEC_M_CB_SRL(s4_reg_D, ptr_pc_keyboard_table_alloc[s4_reg_D], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRL_E: // 0x3b
                    {
                        SPEC_M_CB_SRL(s5_reg_E, ptr_pc_keyboard_table_alloc[s5_reg_E], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRL_H: // 0x3c
                    {
                        SPEC_M_CB_SRL(s6_reg_H, ptr_pc_keyboard_table_alloc[s6_reg_H], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRL_L: // 0x3d
                    {
                        SPEC_M_CB_SRL(s7_reg_L, ptr_pc_keyboard_table_alloc[s7_reg_L], s1_reg_F);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRL_pHLp: // 0x3e
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(7);
                        
                        sp139 = ptr_spectrum_roms[sp13C];

                        if(1);
                        SPEC_M_CB_SRL(sp139, ptr_pc_keyboard_table_alloc[sp139], s1_reg_F);

                        SPEC_M_MEM_WRITE(sp13C, sp139);

                        break;
                    }
                    case SPEC_HEX_OP_CB_SRL_A: // 0x3f
                    {
                        SPEC_M_CB_SRL(s0_reg_A, ptr_pc_keyboard_table_alloc[s0_reg_A], s1_reg_F);

                        break;
                    }

                    }
                    } else {
                    // 8654
                    
                        u8 sp100 = (sp13B >> 3) & 7;

                        switch (sp13B & 0xC7)
                        {
                            #define SPEC_HEX_OP_CB_BIT_B 0x40
                            #define SPEC_HEX_OP_CB_BIT_C 0x41
                            #define SPEC_HEX_OP_CB_BIT_D 0x42
                            #define SPEC_HEX_OP_CB_BIT_E 0x43
                            #define SPEC_HEX_OP_CB_BIT_H 0x44
                            #define SPEC_HEX_OP_CB_BIT_L 0x45
                            #define SPEC_HEX_OP_CB_BIT_pHLp 0x46
                            #define SPEC_HEX_OP_CB_BIT_A 0x47
                            /***
                             * "BIT N,(IX+D)" operation
                             * 
                             * This instruction tests bit N in the indexed memory location and 
                             * sets the Z flag accordingly.
                             * 
                             * S Set if N = 7 and tested bit is set.
                             * Z is set if specified bit is 0; otherwise, it is reset.
                             * H is set.
                             * P/V is Set just like ZF flag.
                             * N is reset.
                             * C is not affected.
                            */
                            case SPEC_HEX_OP_CB_BIT_B: // 0x40
                            {
                                s1_reg_F = (((s2_reg_B & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (s2_reg_B & 0x28));

                                break;
                            }
                            case SPEC_HEX_OP_CB_BIT_C: // 0x41
                            {
                                s1_reg_F = (((s3_reg_C & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (s3_reg_C & 0x28));

                                break;
                            }
                            case SPEC_HEX_OP_CB_BIT_D: // 0x42
                            {
                                s1_reg_F = (((s4_reg_D & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (s4_reg_D & 0x28));

                                break;
                            }
                            case SPEC_HEX_OP_CB_BIT_E: // 0x43
                            {
                                s1_reg_F = (((s5_reg_E & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (s5_reg_E & 0x28));
                                
                                break;
                            }
                            case SPEC_HEX_OP_CB_BIT_H: // 0x44
                            {
                                s1_reg_F = (((s6_reg_H & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (s6_reg_H & 0x28));
                                
                                break;
                            }
                            case SPEC_HEX_OP_CB_BIT_L: // 0x45
                            {
                                s1_reg_F = (((s7_reg_L & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (s7_reg_L & 0x28));
                                
                                break;
                            }
                            case SPEC_HEX_OP_CB_BIT_pHLp: // 0x46
                            {

                                sp28C_clock += SPEC_CPU_CYCLES(4);

                                sp139 = ptr_spectrum_roms[sp13C];

                                s1_reg_F = (((sp139 & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (sp139 & 0x28));

                                SPEC_M_MEM_WRITE(sp13C, sp139);
                                
                                break;
                            }
                            case SPEC_HEX_OP_CB_BIT_A: // 0x47
                            {
                                s1_reg_F = (((s0_reg_A & (1 << sp100)) ? 0x10 : 0x54) | (s1_reg_F & 1) | (s0_reg_A & 0x28));
                                
                                break;
                            }

                            #define SPEC_HEX_OP_CB_RES_B 0x80
                            #define SPEC_HEX_OP_CB_RES_C 0x81
                            #define SPEC_HEX_OP_CB_RES_D 0x82
                            #define SPEC_HEX_OP_CB_RES_E 0x83
                            #define SPEC_HEX_OP_CB_RES_H 0x84
                            #define SPEC_HEX_OP_CB_RES_L 0x85
                            #define SPEC_HEX_OP_CB_RES_pHLp 0x86
                            #define SPEC_HEX_OP_CB_RES_A 0x87
                            /***
                             * "RES N,(IX+D),Q" operation
                             * 
                             * Bit N of the indexed memory location addressed is reset.
                             * The result is autocopied to register Q.
                             *  Q: 000=B, 001=C, 010=D, 011=E
                             *     100=H, 101=L, 110=N/A, 111=A
                            */
                            case SPEC_HEX_OP_CB_RES_B: // 0x80
                            {
                                s2_reg_B &= ~(1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_RES_C: // 0x81
                            {
                                s3_reg_C &= ~(1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_RES_D: // 0x82
                            {
                                s4_reg_D &= ~(1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_RES_E: // 0x83
                            {
                                s5_reg_E &= ~(1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_RES_H: // 0x84
                            {
                                s6_reg_H &= ~(1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_RES_L: // 0x85
                            {
                                s7_reg_L &= ~(1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_RES_pHLp: // 0x86
                            {
                                sp28C_clock += SPEC_CPU_CYCLES(4);

                                sp139 = ptr_spectrum_roms[sp13C]; \
                                        sp139 = sp139 & ~(1 << sp100);

                                SPEC_M_MEM_WRITE(sp13C, sp139);
                                
                                break;
                            }
                            case SPEC_HEX_OP_CB_RES_A: // 0x87
                            {
                                s0_reg_A &= ~(1 << sp100);

                                break;
                            }
                            
                            #define SPEC_HEX_OP_CB_SET_B 0xC0
                            #define SPEC_HEX_OP_CB_SET_C 0xC1
                            #define SPEC_HEX_OP_CB_SET_D 0xC2
                            #define SPEC_HEX_OP_CB_SET_E 0xC3
                            #define SPEC_HEX_OP_CB_SET_H 0xC4
                            #define SPEC_HEX_OP_CB_SET_L 0xC5
                            #define SPEC_HEX_OP_CB_SET_pHLp 0xC6
                            #define SPEC_HEX_OP_CB_SET_A 0xC7
                            /***
                             * "SET N,(IX+D),Q" operation
                             * 
                             * Bit N of the indexed memory location addressed is set.
                             * The result is autocopied to register Q.
                             *  Q: 000=B, 001=C, 010=D, 011=E
                             *     100=H, 101=L, 110=N/A, 111=A
                            */
                            case SPEC_HEX_OP_CB_SET_B: // 0xc0
                            {
                                s2_reg_B |= (1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_SET_C: // 0xc1
                            {
                                s3_reg_C |= (1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_SET_D: // 0xc2
                            {
                                s4_reg_D |= (1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_SET_E: // 0xc3
                            {
                                s5_reg_E |= (1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_SET_H: // 0xc4
                            {
                                s6_reg_H |= (1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_SET_L: // 0xc5
                            {
                                s7_reg_L |= (1 << sp100);

                                break;
                            }
                            case SPEC_HEX_OP_CB_SET_pHLp: // 0xc6
                            {
                                sp28C_clock += SPEC_CPU_CYCLES(4);

                                sp139 = ptr_spectrum_roms[sp13C]; \
                                        sp139 |= (1 << sp100); 

                                SPEC_M_MEM_WRITE(sp13C, sp139);

                                break;
                            }
                            case SPEC_HEX_OP_CB_SET_A: // 0xc7
                            {
                                s0_reg_A |= (1 << sp100);

                                break;
                            }
                    } // end switch 
                } // end else sp13B < 0x40
                
                if (sp287_index_mode)
                {
                    switch (sp13A)              /* switch 6 */
                    {
                    case 0:                     /* switch 6 */
                        s2_reg_B = sp139;
                        break;
                    case 1:                     /* switch 6 */
                        s3_reg_C = sp139;
                        break;
                    case 2:                     /* switch 6 */
                        s4_reg_D = sp139;
                        break;
                    case 3:                     /* switch 6 */
                        s5_reg_E = sp139;
                        break;
                    case 4:                     /* switch 6 */
                        s6_reg_H = sp139;
                        break;
                    case 5:                     /* switch 6 */
                        s7_reg_L = sp139;
                        break;
                    case 7:                     /* switch 6 */
                        s0_reg_A = sp139;
                        break;
                    }
                }

                break;
            } // end 0xcb

            // CALL Z, HHLL
            /***
            * If flag Z is set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_Z_HHLL: // 0xCC
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (s1_reg_F & 0x40)
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(7);

                    sp292_SP -= 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);

                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // CALL HHLL
            /***
            * The current contents of PC are pushed onto the top of the
            * external memory stack. The operands NN are then loaded to PC to
            * point to the address in memory at which the first op code of a
            * subroutine is to be fetched. At the end of the subroutine, a RET
            * instruction can be used to return to the original program flow by
            * popping the top of the stack back to PC. The push is accomplished
            * by first decrementing the current contents of SP, loading the
            * high-order byte of the PC contents to the memory address now pointed
            * to by SP; then decrementing SP again, and loading the low-order
            * byte of the PC contents to the top of stack.
            */
            case SPEC_HEX_OP_CALL_HHLL: // 0xCD
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if(1);
                sp28C_clock += SPEC_CPU_CYCLES(7); \
                sp292_SP -= 2; \
                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);
                
                sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);

                break;
            }

            // ADC A, NN
            /***
            *
            */
            case SPEC_HEX_OP_ADC_A_NN: // 0xCE
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                SPEC_M_ADC(s0_reg_A, ptr_spectrum_roms[sp298_PC], s1_reg_F);
                
                if(1);
                sp298_PC = sp298_PC + 1;

                break;
            }

            // RST 08
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0008H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_08: // 0xCF
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xB);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC) >> 8);

                sp298_PC = 8;

                break;
            }

            // RET NC
            /***
            * If C flag is not set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_NC: // 0xD0
            {
                sp28C_clock += SPEC_CPU_CYCLES(5);

                if (!(s1_reg_F & 1))
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(6);
                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }

                break;
            }

            // POP DE
            /***
            * The top two bytes of the external memory last-in, first-out (LIFO)
            * stack are popped to register pair DE. SP holds the 16-bit address
            * of the current top of the stack. This instruction first loads to
            * the low-order portion of RR, the byte at the memory location
            * corresponding to the contents of SP; then SP is incremented and
            * the contents of the corresponding adjacent memory location are
            * loaded to the high-order portion of RR and the SP is now incremented
            * again.
            */
            case SPEC_HEX_OP_POP_DE: // 0xD1
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                s5_reg_E = ptr_spectrum_roms[sp292_SP + 0];
                s4_reg_D = ptr_spectrum_roms[sp292_SP + 1];
                sp292_SP += 2;

                break;
            }

            // JP NC, HHLL
            /***
            * If C flag is not set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_NC_HHLL: // 0xD2
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 1))
                {
                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // OUT (NN) A
            /***
            * The operand N is placed on the bottom half (A0 through A7) of
            * the address bus to select the I/O device at one of 256 possible
            * ports. The contents of A also appear on the top half(A8 through
            * A15) of the address bus at this time. Then the byte contained
            * in A is placed on the data bus and written to the selected
            * peripheral device.
            */
            case SPEC_HEX_OP_OUT_pNNp_A: // 0xD3
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);
                
                sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s0_reg_A, ptr_spectrum_roms[sp298_PC], s0_reg_A);
                sp298_PC++;

                break;
            }

            // CALL NC, HHLL
            /***
            * If flag C is not set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_NC_HHLL: // 0xD4
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 1))
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(0x7);

                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);

                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[1 + sp298_PC], ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;                    
                }

                break;
            }

            // PUSH DE
            /***
            * The contents of the register pair DE are pushed to the external
            * memory last-in, first-out (LIFO) stack. SP holds the 16-bit
            * address of the current top of the Stack. This instruction first
            * decrements SP and loads the high-order byte of register pair RR
            * to the memory address specified by SP. Then SP is decremented again
            * and loads the low-order byte of RR to the memory location
            * corresponding to this new address in SP.
            */
            case SPEC_HEX_OP_PUSH_DE: // 0xD5
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, s5_reg_E);

                SPEC_M_MEM_WRITE(sp292_SP + 1, s4_reg_D);

                break;
            }

            // SUB A, NN
            /***
            *
            */
            case SPEC_HEX_OP_SUB_A_NN: // 0xD6
            {
                sp28C_clock += SPEC_CPU_CYCLES(0x7);

                SPEC_M_SUB(s0_reg_A, ptr_spectrum_roms[sp298_PC], s1_reg_F);
                if(1);

                sp298_PC += 1;

                break;
            }

            // RST 10
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0010H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_10: // 0xD7
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP -= 2;

                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC) >> 8);

                sp298_PC = 0x10;

                break;
            }

            // RET C
            /***
            * If C flag is set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_C: // 0xD8
            {
                sp28C_clock += SPEC_CPU_CYCLES(5);

                if(1);
                if (s1_reg_F & 1)
                {
                    sp28C_clock += SPEC_CPU_CYCLES(6);
                    
                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }

                break;
            }

            // EXX
            /***
            * Each 2-byte value in register pairs BC, DE, and HL is exchanged
            * with the 2-byte value in BC', DE', and HL', respectively.
            */
            case SPEC_HEX_OP_EXX: // 0xD9
            {
                // <-- stack jump: from 0 to -1 -->

                u8 spF8 = s2_reg_B;
                u8 spF7 = s3_reg_C;
                u8 spF6 = s4_reg_D;
                u8 spF5 = s5_reg_E;
                u8 spF4 = s6_reg_H;
                u8 spF3 = s7_reg_L;

                sp28C_clock += SPEC_CPU_CYCLES(4);

                s2_reg_B = sp2A4_alt_reg_B;
                s3_reg_C = sp2A3_alt_reg_C;
                s4_reg_D = sp2A2_alt_reg_D;
                s5_reg_E = sp2A1_alt_reg_E;
                s6_reg_H = sp2A0_alt_reg_H;
                s7_reg_L = sp29F_alt_reg_L;

                sp2A4_alt_reg_B = spF8;
                sp2A3_alt_reg_C = spF7;
                sp2A2_alt_reg_D = spF6;
                sp2A1_alt_reg_E = spF5;
                sp2A0_alt_reg_H = spF4;
                sp29F_alt_reg_L = spF3;

                break;
            }

            // JP C, HHLL
            /***
            * If C flag is not set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_C_HHLL: // 0xDA
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xA);
                sp298_PC = (s1_reg_F & 1)
                    ? SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC])
                    : sp298_PC + 2;

                break;
            }

            // IN A, (NN)
            /***
            * The operand N is placed on the bottom half (A0 through A7) of
            * the address bus to select the I/O device at one of 256 possible
            * ports. The contents of A also appear on the top half (A8 through
            * A15) of the address bus at this time. Then one byte from the
            * selected port is placed on the data bus and written to A
            * in the CPU.
            */
            case SPEC_HEX_OP_IN_A_pNNp: // 0xDB
            {
                u16 spF0;

                sp28C_clock += SPEC_CPU_CYCLES(0xB);

                spF0 = spectrum_input_handling(sp28C_clock, s0_reg_A, ptr_spectrum_roms[sp298_PC]);
                s0_reg_A = (u8)spF0;
                sp28C_clock += SPEC_CPU_CYCLES((s32) (spF0) >> 8);
                sp298_PC += 1;

                break;
            }

            // CALL C, HHLL
            /***
            * If flag C is set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_C_HHLL: // 0xDC
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (s1_reg_F & 1)
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(7);

                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);

                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // DD Opcodes
            /***
            * The immplementation here sets a flag to evaluate the next instruction in the loop.
            */
            case SPEC_HEX_OP_DD_Opcodes: // 0xDD
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                sp286_next_index_mode = 1;
                sp285_exit = 0;

                break;
            }

            // SBC A, NN
            /***
            *
            */
            case SPEC_HEX_OP_SBC_A_NN: // 0xDE
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                SPEC_M_SCB_ALIGN32(s0_reg_A, ptr_spectrum_roms[sp298_PC], s1_reg_F);
                if(1);
                
                sp298_PC += 1;

                break;
            }

            // RST 18
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0018H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_18: // 0xDF
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xB);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC) >> 8);

                sp298_PC = 0x18;

                break;
            }

            // RET PO
            /***
            * If PV flag is not set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_PO: // 0xE0
            {
                sp28C_clock += SPEC_CPU_CYCLES(0x5);
                
                if(1);
                if (!(s1_reg_F & 4))
                {
                    sp28C_clock += SPEC_CPU_CYCLES(0x6);
                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }

                break;
            }

            // POP HL
            /***
            * The top two bytes of the external memory last-in, first-out (LIFO)
            * stack are popped to register pair HL. SP holds the 16-bit address
            * of the current top of the stack. This instruction first loads to
            * the low-order portion of RR, the byte at the memory location
            * corresponding to the contents of SP; then SP is incremented and
            * the contents of the corresponding adjacent memory location are
            * loaded to the high-order portion of RR and the SP is now incremented
            * again.
            */
            case SPEC_HEX_OP_POP_HL: // 0xE1
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (sp287_index_mode == 0)
                {
                    s7_reg_L = ptr_spectrum_roms[sp292_SP + 0];
                    s6_reg_H = ptr_spectrum_roms[sp292_SP + 1];
                    
                    sp292_SP += 2;
                }
                else if (sp287_index_mode == 1)
                {
                    sp296_IX = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }
                else
                {
                    sp294_IY = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }

                break;
            }

            // JP PO, HHLL
            /***
            * If PV flag is not set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_PO_HHLL: // 0xE2
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 4))
                {
                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp298_PC + 1], ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // EX (SP) HL
            /***
            * The low-order byte contained in HL is exchanged with the contents
            * of the memory address specified by the contents of SP, and the
            * high-order byte of HL is exchanged with the next highest memory
            * address (SP+1).
            */
            case SPEC_HEX_OP_EX_pSPp_HL: // 0xE3
            {
                sp28C_clock += SPEC_CPU_CYCLES(0x13);

                if (sp287_index_mode == 0)
                {
                    u16 spEA;
                    spEA = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    
                    if(1);
                    SPEC_M_MEM_WRITE(sp292_SP, s7_reg_L);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, s6_reg_H);

                    s6_reg_H = (s32)spEA >> 8;
                    s7_reg_L = (u8)spEA;
                }
                else if (sp287_index_mode == 1)
                {
                    u16 spE8;
                    
                    spE8 = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);

                    SPEC_M_MEM_WRITE(sp292_SP, sp296_IX);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp296_IX) >> 8);

                    sp296_IX = spE8;
                }
                else
                {
                    u16 spE6;
                    spE6 = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);

                    SPEC_M_MEM_WRITE(sp292_SP, sp294_IY);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp294_IY) >> 8);

                    sp294_IY = spE6;
                }

                break;
            }

            // CALL PO HHLL
            /***
            * If flag PV is not set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_PO_HHLL: // 0xE4
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 4))
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(0x7);

                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);

                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // PUSH HL
            /***
            * The contents of the register pair HL are pushed to the external
            * memory last-in, first-out (LIFO) stack. SP holds the 16-bit
            * address of the current top of the Stack. This instruction first
            * decrements SP and loads the high-order byte of register pair RR
            * to the memory address specified by SP. Then SP is decremented again
            * and loads the low-order byte of RR to the memory location
            * corresponding to this new address in SP.
            */
            case SPEC_HEX_OP_PUSH_HL: // 0xE5
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                if (sp287_index_mode == 0)
                {
                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, s7_reg_L);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, s6_reg_H);
                }
                else if (sp287_index_mode == 1)
                {
                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp296_IX);
                    
                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp296_IX) >> 8);
                }
                else
                {
                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp294_IY);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp294_IY) >> 8);
                }

                break;
            }

            // AND NN
            /***
            *
            */
            case SPEC_HEX_OP_AND_NN: // 0xE6
            {
                sp28C_clock += SPEC_CPU_CYCLES(0x7);

                if(1);
                s0_reg_A &= ptr_spectrum_roms[sp298_PC]; \
                s1_reg_F = (ptr_pc_keyboard_table_alloc[s0_reg_A] \
                    | ((s0_reg_A & 0xA8) \
                    | ((!s0_reg_A) << 6) \
                    | 0x10));
                if(1); \
                sp298_PC += 1;

                break;
            }

            // RST 20
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0020H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_20: // 0xE7
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC) >> 8);

                sp298_PC = 0x20;

                break;
            }

            // RET PE
            /***
            * If PV flag is not set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_PE: // 0xE8
            {
                sp28C_clock += SPEC_CPU_CYCLES(0x5);

                if(1);
                if (s1_reg_F & 4)
                {
                    sp28C_clock += SPEC_CPU_CYCLES(0x6);
                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }

                break;
            }

            // JP (HL)
            /***
            * PC is loaded with the contents of HL. The next instruction is
            * fetched from the location designated by the new contents of PC.
            */
            case SPEC_HEX_OP_JP_pHLp: // 0xE9
            {
                sp28C_clock += SPEC_CPU_CYCLES(0x4);

                sp298_PC = ((sp287_index_mode == 0) ? (SPEC_MK_16(s6_reg_H, s7_reg_L)) : ((sp287_index_mode == 1) ? sp296_IX : sp294_IY));

                break;
            }

            // JP PE, HHLL
            /***
            * If PV flag is set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_PE_HHLL: // 0xEA
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xA);

                if (s1_reg_F & 4)
                {
                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp298_PC + 1], ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;                    
                }

                break;
            }

            // EX DE, HL
            /***
            * The 2-byte contents of register pairs DE and HL are exchanged.
            */
            case SPEC_HEX_OP_EX_DE_HL: // 0xEB
            {
                u8 spE5 = s6_reg_H;
                u8 spE4 = s5_reg_E;
                sp28C_clock += SPEC_CPU_CYCLES(0x4);
                s6_reg_H = s4_reg_D;
                s5_reg_E = s7_reg_L;
                s4_reg_D = spE5;
                s7_reg_L = spE4;

                break;
            }

            // CALL Pe, HHLL
            /***
            * If flag PV is set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_Pe_HHLL: // 0xEC
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (s1_reg_F & 4)
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(0x7);
                    
                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);

                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            #define SPEC_HEX_OP_ED_LDI 0xA0
            #define SPEC_HEX_OP_ED_CPI 0xA1
            #define SPEC_HEX_OP_ED_INI 0xA2
            #define SPEC_HEX_OP_ED_OTI 0xA3
            #define SPEC_HEX_OP_ED_LDD 0xA8
            #define SPEC_HEX_OP_ED_CPD 0xA9
            #define SPEC_HEX_OP_ED_IND 0xAA
            #define SPEC_HEX_OP_ED_OTD 0xAB
            #define SPEC_HEX_OP_ED_LDIR 0xB0
            #define SPEC_HEX_OP_ED_CPIR 0xB1
            #define SPEC_HEX_OP_ED_INIR 0xB2
            #define SPEC_HEX_OP_ED_OTIR 0xB3
            #define SPEC_HEX_OP_ED_LDDR 0xB8
            #define SPEC_HEX_OP_ED_CPDR 0xB9
            #define SPEC_HEX_OP_ED_INDR 0xBA
            #define SPEC_HEX_OP_ED_OTDR 0xBB
                
            #define SPEC_HEX_OP_ED_IN_B_pCp 0x40
            #define SPEC_HEX_OP_ED_OUT_pCp_B 0x41
            #define SPEC_HEX_OP_ED_SBC_HL_BC 0x42
            #define SPEC_HEX_OP_ED_LD_pHHLLp_BC 0x43
            #define SPEC_HEX_OP_ED_NEG 0x44
            #define SPEC_HEX_OP_ED_RETN 0x45
            #define SPEC_HEX_OP_ED_IM_0 0x46
            #define SPEC_HEX_OP_ED_LD_I_A 0x47
            #define SPEC_HEX_OP_ED_IN_C_pCp 0x48
            #define SPEC_HEX_OP_ED_OUT_pCp_C 0x49
            #define SPEC_HEX_OP_ED_ADC_HL_BC 0x4A
            #define SPEC_HEX_OP_ED_LD_BC_pHHLLp 0x4B
            #define SPEC_HEX_OP_ED_4C 0x4C
            #define SPEC_HEX_OP_ED_RETI 0x4D
            #define SPEC_HEX_OP_ED_4E 0x4E
            #define SPEC_HEX_OP_ED_LD_R_A 0x4F
            #define SPEC_HEX_OP_ED_IN_D_pCp 0x50
            #define SPEC_HEX_OP_ED_OUT_pCp_D 0x51
            #define SPEC_HEX_OP_ED_SBC_HL_DE 0x52
            #define SPEC_HEX_OP_ED_LD_pHHLLp_DE 0x53
            #define SPEC_HEX_OP_ED_54 0x54
            #define SPEC_HEX_OP_ED_55 0x55
            #define SPEC_HEX_OP_ED_IM_1 0x56
            #define SPEC_HEX_OP_ED_LD_A_I 0x57
            #define SPEC_HEX_OP_ED_IN_E_pCp 0x58
            #define SPEC_HEX_OP_ED_OUT_pCp_E 0x59
            #define SPEC_HEX_OP_ED_ADC_HL_DE 0x5A
            #define SPEC_HEX_OP_ED_LD_DE_pHHLLp 0x5B
            #define SPEC_HEX_OP_ED_5C 0x5C
            #define SPEC_HEX_OP_ED_5D 0x5D
            #define SPEC_HEX_OP_ED_IM_2 0x5E
            #define SPEC_HEX_OP_ED_LD_A_R 0x5F
            #define SPEC_HEX_OP_ED_IN_H_pCp 0x60
            #define SPEC_HEX_OP_ED_OUT_pCp_H 0x61
            #define SPEC_HEX_OP_ED_SBC_HL_HL 0x62
            #define SPEC_HEX_OP_ED_LD_pHHLLp_HL 0x63
            #define SPEC_HEX_OP_ED_64 0x64
            #define SPEC_HEX_OP_ED_65 0x65
            #define SPEC_HEX_OP_ED_66 0x66
            #define SPEC_HEX_OP_ED_RRD 0x67
            #define SPEC_HEX_OP_ED_IN_L_pCp 0x68
            #define SPEC_HEX_OP_ED_OUT_pCp_L 0x69
            #define SPEC_HEX_OP_ED_ADC_HL_HL 0x6A
            #define SPEC_HEX_OP_ED_LD_HL_pHHLLp 0x6B
            #define SPEC_HEX_OP_ED_6C 0x6C
            #define SPEC_HEX_OP_ED_6D 0x6D
            #define SPEC_HEX_OP_ED_6E 0x6E
            #define SPEC_HEX_OP_ED_RLD 0x6F
            #define SPEC_HEX_OP_ED_IN_F_pCp 0x70
            #define SPEC_HEX_OP_ED_OUT_pCp_F 0x71
            #define SPEC_HEX_OP_ED_SBC_HL_SP 0x72
            #define SPEC_HEX_OP_ED_LD_pHHLLp_SP 0x73
            #define SPEC_HEX_OP_ED_74 0x74
            #define SPEC_HEX_OP_ED_75 0x75
            #define SPEC_HEX_OP_ED_76 0x76
            #define SPEC_HEX_OP_ED_77 0x77
            #define SPEC_HEX_OP_ED_IN_A_pCp 0x78
            #define SPEC_HEX_OP_ED_OUT_pCp_A 0x79
            #define SPEC_HEX_OP_ED_ADC_HL_SP 0x7A
            #define SPEC_HEX_OP_ED_LD_SP_pHHLLp 0x7B
            #define SPEC_HEX_OP_ED_OP_7C 0x7C
            #define SPEC_HEX_OP_ED_OP_7D 0x7D
            #define SPEC_HEX_OP_ED_OP_7E 0x7E

            // ED Opcodes
            /***
            *
            */
            case SPEC_HEX_OP_ED_Opcodes: // 0xED
            {
                u8 spE3;
                
                sp28C_clock += SPEC_CPU_CYCLES(0x4);

                spE3 = ptr_spectrum_roms[sp298_PC];
                sp298_PC++;
                sp288_unk_R += 1;
                
                switch (spE3)
                {
                    // IN B,(C)
                    case SPEC_HEX_OP_ED_IN_B_pCp: // 0x40
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, s2_reg_B, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),B
                    case SPEC_HEX_OP_ED_OUT_pCp_B: // 0x41
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x8);
                        
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, s2_reg_B);

                        break;
                    }

                    // SBC HL,BC
                    case SPEC_HEX_OP_ED_SBC_HL_BC: // 0x42
                    {
                        u16 spDE;
                        // <-- stack jump: from 0 to -2 -->
                        struct reg16_2 spD8;                        
                        
                        sp28C_clock += SPEC_CPU_CYCLES(0xb);
                        SPEC_M_ED_SBC(spDE, spD8, s6_reg_H, s7_reg_L, s2_reg_B, s3_reg_C, s1_reg_F);
                        
                        break;
                    }

                    // LD (HHLL),BC
                    case SPEC_HEX_OP_ED_LD_pHHLLp_BC: // 0x43
                    {
                        u16 spD6; // 0xce ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        spD6 = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                        sp298_PC += 2;

                        SPEC_M_MEM_WRITE(spD6, s3_reg_C);
                        
                        SPEC_M_MEM_WRITE(spD6 + 1, s2_reg_B);
                        
                        break;
                    }

                    // NEG
                    case SPEC_HEX_OP_ED_NEG: // 0x44
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);
                        
                        SPEC_M_NEG(s0_reg_A, s1_reg_F);

                        break;
                    }

                    // RETN
                    /**
                    * This instruction is used at the end of a nonmaskable interrupts 
                    * service routine to restore the contents of PC. The state of IFF2
                    * is copied back to IFF1 so that maskable interrupts are enabled 
                    * immediately following the RETN if they were enabled before the 
                    * nonmaskable interrupt.
                    */
                    case SPEC_HEX_OP_ED_RETN: // 0x45
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);

                        if(1);
                        sp29D_IFF1 = sp29C_IFF2;
                        sp28C_clock += SPEC_CPU_CYCLES(0x6);
                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // IM 0
                    case SPEC_HEX_OP_ED_IM_0: // 0x46
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);
                        
                        sp29B_IM = 0;

                        break;
                    }

                    // LD I,A
                    case SPEC_HEX_OP_ED_LD_I_A: // 0x47
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x5);
                        
                        sp29E_reg_I = s0_reg_A;

                        break;
                    }

                    // IN C,(C)
                    case SPEC_HEX_OP_ED_IN_C_pCp: // 0x48
                    {
                        //struct reg16 spD4;
                        //u8 tt;

                        sp28C_clock += SPEC_CPU_CYCLES(0x8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, s3_reg_C, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),C
                    case SPEC_HEX_OP_ED_OUT_pCp_C: // 0x49
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x8);

                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, s3_reg_C);
                        
                        break;
                    }

                    // ADC HL,BC
                    case SPEC_HEX_OP_ED_ADC_HL_BC: // 0x4A
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0xb);

                        SPEC_M_ED_ADC(s6_reg_H, s7_reg_L, s2_reg_B, s3_reg_C, s1_reg_F);

                        break;
                    }

                    // LD BC,(HHLL)
                    case SPEC_HEX_OP_ED_LD_BC_pHHLLp: // 0x4B
                    {
                        u16 spCA;

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        spCA = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                        sp298_PC += 2;
                        s3_reg_C = ptr_spectrum_roms[spCA + 0];
                        s2_reg_B = ptr_spectrum_roms[spCA + 1];

                        break;
                    }

                    // 4C
                    case SPEC_HEX_OP_ED_4C: // 0x4C
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        SPEC_M_NEG(s0_reg_A, s1_reg_F);

                        break;
                    }

                    // RETI
                    case SPEC_HEX_OP_ED_RETI: // 0x4D
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        if(1);
                        sp28C_clock += SPEC_CPU_CYCLES(6);
                        
                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // 4E
                    case SPEC_HEX_OP_ED_4E: // 0x4E
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);
                        
                        sp29B_IM = 1;

                        break;
                    }

                    // LD R,A
                    case SPEC_HEX_OP_ED_LD_R_A: // 0x4F
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(5);

                        sp2A7_reg_R = s0_reg_A;
                        sp288_unk_R = s0_reg_A & 0xFF;

                        break;
                    }

                    // IN D,(C)
                    case SPEC_HEX_OP_ED_IN_D_pCp: // 0x50
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, s4_reg_D, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),D
                    case SPEC_HEX_OP_ED_OUT_pCp_D: // 0x51
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);
                        
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, s4_reg_D);

                        break;
                    }

                    // SBC HL,DE
                    case SPEC_HEX_OP_ED_SBC_HL_DE: // 0x52
                    {
                        u16 spC6;
                        struct reg16_2 spC0;

                        sp28C_clock += SPEC_CPU_CYCLES(0xb);

                        SPEC_M_ED_SBC(spC6, spC0, s6_reg_H, s7_reg_L, s4_reg_D, s5_reg_E, s1_reg_F);

                        break;
                    }

                    // LD (HHLL),DE
                    case SPEC_HEX_OP_ED_LD_pHHLLp_DE: // 0x53
                    {
                        u16 spBE;

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        spBE = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                        sp298_PC += 2;

                        SPEC_M_MEM_WRITE(spBE, s5_reg_E);
                        
                        SPEC_M_MEM_WRITE(spBE + 1, s4_reg_D);

                        break;
                    }

                    // 54
                    case SPEC_HEX_OP_ED_54: // 0x54
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        SPEC_M_NEG(s0_reg_A, s1_reg_F);

                        break;
                    }

                    // 55
                    case SPEC_HEX_OP_ED_55: // 0x55
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        if(1);
                        sp28C_clock += SPEC_CPU_CYCLES(6);

                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // IM 1
                    case SPEC_HEX_OP_ED_IM_1: // 0x56
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);
                        
                        sp29B_IM = 2;

                        break;
                    }

                    // LD A,I
                    case SPEC_HEX_OP_ED_LD_A_I: // 0x57
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(5);
                        
                        s0_reg_A = sp29E_reg_I;
                        s1_reg_F = ((s1_reg_F & 1)
                            | (s0_reg_A & 0xA8)
                            | ((!s0_reg_A) << 6)
                            | (sp29C_IFF2 << 2)
                        );

                        break;
                    }

                    // IN E,(C)
                    case SPEC_HEX_OP_ED_IN_E_pCp: // 0x58
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, s5_reg_E, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),E
                    case SPEC_HEX_OP_ED_OUT_pCp_E: // 0x59
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);
                        
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, s5_reg_E);

                        break;
                    }

                    // ADC HL,DE
                    case SPEC_HEX_OP_ED_ADC_HL_DE: // 0x5A
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0xb);

                        SPEC_M_ED_ADC(s6_reg_H, s7_reg_L, s4_reg_D, s5_reg_E, s1_reg_F);

                        break;
                    }

                    // LD DE,(HHLL)
                    case SPEC_HEX_OP_ED_LD_DE_pHHLLp: // 0x5B
                    {
                        u16 spB2;

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        spB2 = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                        sp298_PC += 2;
                        s5_reg_E = ptr_spectrum_roms[spB2 + 0];
                        s4_reg_D = ptr_spectrum_roms[spB2 + 1];

                        break;
                    }

                    // 5C
                    case SPEC_HEX_OP_ED_5C: // 0x5C
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        SPEC_M_NEG(s0_reg_A, s1_reg_F);

                        break;
                    }

                    // 5D
                    case SPEC_HEX_OP_ED_5D: // 0x5D
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        if(1);
                        sp28C_clock += SPEC_CPU_CYCLES(6);
                        
                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // IM 2
                    case SPEC_HEX_OP_ED_IM_2: // 0x5E
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);
                        
                        sp29B_IM = 3;

                        break;
                    }

                    // LD A,R
                    case SPEC_HEX_OP_ED_LD_A_R: // 0x5F
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(5);

                        if(1);
                        
                        sp2A7_reg_R = (sp2A7_reg_R & 0x80) | (sp288_unk_R & 0x7F); 
                        s0_reg_A = sp2A7_reg_R; 
                        s1_reg_F = (s1_reg_F & 1) 
                            | (s0_reg_A & 0xA8) 
                            | (((!s0_reg_A)) << 6) 
                            | (sp29C_IFF2 << 2) 
                        ;

                        break;
                    }

                    // IN H,(C)
                    case SPEC_HEX_OP_ED_IN_H_pCp: // 0x60
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, s6_reg_H, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),H
                    case SPEC_HEX_OP_ED_OUT_pCp_H: // 0x61
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);
                        
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, s6_reg_H);

                        break;
                    }

                    // SBC HL,HL
                    case SPEC_HEX_OP_ED_SBC_HL_HL: // 0x62
                    {
                        u16 spAE;
                        struct reg16_2 spA8;

                        sp28C_clock += SPEC_CPU_CYCLES(0xb);

                        SPEC_M_ED_SBC(spAE, spA8, s6_reg_H, s7_reg_L, s6_reg_H, s7_reg_L, s1_reg_F);

                        break;
                    }

                    // LD (HHLL),HL
                    case SPEC_HEX_OP_ED_LD_pHHLLp_HL: // 0x63
                    {
                        u16 spA6;

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        spA6 = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);

                        sp298_PC += 2;
                        
                        SPEC_M_MEM_WRITE(spA6, s7_reg_L);

                        SPEC_M_MEM_WRITE(spA6 + 1, s6_reg_H);
                        
                        break;
                    }

                    // 64
                    case SPEC_HEX_OP_ED_64: // 0x64
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);
                        
                        s0_reg_A = -s0_reg_A;
                        s1_reg_F = ((s0_reg_A & 0xA8) | ((!s0_reg_A) << 6) | (((s0_reg_A & 0xF) > 0) << 4) 
                            | ((s0_reg_A == 0x80) << 2) | 2 | (s0_reg_A > 0));

                        break;
                    }

                    // 65
                    case SPEC_HEX_OP_ED_65: // 0x65
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        if(1);
                        sp28C_clock += SPEC_CPU_CYCLES(6);
                        
                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // 66
                    case SPEC_HEX_OP_ED_66: // 0x66
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);
                        sp29B_IM = 0;

                        break;
                    }

                    // RRD
                    case SPEC_HEX_OP_ED_RRD: // 0x67
                    {
                        u8 spA5;
                        u8 spA4;

                        sp28C_clock += SPEC_CPU_CYCLES(0xe);
                        
                        spA5 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];
                        spA4 = (s0_reg_A << 4) | (spA5 >> 4);
                        s0_reg_A = ((s0_reg_A & 0xF0) | (spA5 & 0xF));

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s6_reg_H, s7_reg_L), spA4);

                        s1_reg_F = (ptr_pc_keyboard_table_alloc[s0_reg_A]
                            | ((s1_reg_F & 1)
                            | (s0_reg_A & 0xA8)
                            | ((!s0_reg_A) << 6))
                        );

                        break;
                    }

                    // IN L,(C)
                    case SPEC_HEX_OP_ED_IN_L_pCp: // 0x68
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, s7_reg_L, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),L
                    case SPEC_HEX_OP_ED_OUT_pCp_L: // 0x69
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);
                        
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, s7_reg_L);

                        break;
                    }

                    // ADC HL,HL
                    case SPEC_HEX_OP_ED_ADC_HL_HL: // 0x6A
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0xb);

                        SPEC_M_ED_ADC(s6_reg_H, s7_reg_L, s6_reg_H, s7_reg_L, s1_reg_F);

                        break;
                    }

                    // LD HL,(HHLL)
                    case SPEC_HEX_OP_ED_LD_HL_pHHLLp: // 0x6B
                    {
                        u16 sp9A; // 0x92 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        sp9A = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                        sp298_PC += 2;
                        s7_reg_L = ptr_spectrum_roms[sp9A + 0];
                        s6_reg_H = ptr_spectrum_roms[sp9A + 1];

                        break;
                    }

                    // 6C
                    case SPEC_HEX_OP_ED_6C: // 0x6C
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        SPEC_M_NEG(s0_reg_A, s1_reg_F);

                        break;
                    }

                    // 6D
                    case SPEC_HEX_OP_ED_6D: // 0x6D
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);

                        if(1);
                        sp28C_clock += SPEC_CPU_CYCLES(0x6);

                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // 6E
                    case SPEC_HEX_OP_ED_6E: // 0x6E
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);
                        
                        sp29B_IM = 1;

                        break;
                    }

                    // RLD
                    case SPEC_HEX_OP_ED_RLD: // 0x6F
                    {
                        u8 sp99;
                        u8 sp98;

                        sp28C_clock += SPEC_CPU_CYCLES(0x5);

                        sp99 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];
                        sp98 = (s0_reg_A & 0xF) | (sp99 << 4);
                        s0_reg_A = ((s0_reg_A & 0xF0) | ((s32) sp99 >> 4));

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s6_reg_H, s7_reg_L), sp98);

                        s1_reg_F = (ptr_pc_keyboard_table_alloc[s0_reg_A]
                            | ((s1_reg_F & 1)
                            | (s0_reg_A & 0xA8)
                            | ((!s0_reg_A) << 6))
                        );

                        break;
                    }

                    // IN F,(C)
                    case SPEC_HEX_OP_ED_IN_F_pCp: // 0x70
                    {
                        u8 sp97;

                        sp28C_clock += SPEC_CPU_CYCLES(8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, sp97, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),F
                    case SPEC_HEX_OP_ED_OUT_pCp_F: // 0x71
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);
                        
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, 0U);

                        break;
                    }

                    // SBC HL,SP
                    case SPEC_HEX_OP_ED_SBC_HL_SP: // 0x72
                    {
                        u16 sp92;
                        struct reg16_2 sp8C;

                        sp28C_clock += SPEC_CPU_CYCLES(0xb);

                        SPEC_M_ED_SBC(sp92, sp8C, s6_reg_H, s7_reg_L, 0, sp292_SP, s1_reg_F);

                        break;
                    }

                    // LD (HHLL),SP
                    case SPEC_HEX_OP_ED_LD_pHHLLp_SP: // 0x73
                    {
                        u16 sp8A;

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        sp8A = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                        sp298_PC += 2;

                        SPEC_M_MEM_WRITE(sp8A, sp292_SP);

                        SPEC_M_MEM_WRITE(sp8A + 1, (s32) sp292_SP >> 8);

                        break;
                    }

                    // 74
                    case SPEC_HEX_OP_ED_74: // 0x74
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);
                        
                        s0_reg_A = -(s32) s0_reg_A;
                        s1_reg_F = ((s0_reg_A & 0xA8) | ((!s0_reg_A) << 6) | (((s0_reg_A & 0xF) > 0) << 4) 
                            | ((s0_reg_A == 0x80) << 2) | 2 | ((s32) s0_reg_A > 0));

                        break;
                    }

                    // 75
                    case SPEC_HEX_OP_ED_75: // 0x75
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);

                        if(1);
                        sp28C_clock += SPEC_CPU_CYCLES(0x6);
                        
                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // 76
                    case SPEC_HEX_OP_ED_76: // 0x76
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);
                        sp29B_IM = 2;

                        break;
                    }

                    // no 0x77

                    // IN A,(C)
                    case SPEC_HEX_OP_ED_IN_A_pCp: // 0x78
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x8);

                        SPEC_M_ED_IN(s2_reg_B, s3_reg_C, s0_reg_A, sp28C_clock, s1_reg_F);

                        break;
                    }

                    // OUT (C),A
                    case SPEC_HEX_OP_ED_OUT_pCp_A: // 0x79
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(8);
                        
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, s0_reg_A);

                        break;
                    }

                    // ADC HL,SP
                    case SPEC_HEX_OP_ED_ADC_HL_SP: // 0x7A
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0xb);

                        SPEC_M_ED_ADC(s6_reg_H, s7_reg_L, 0, sp292_SP, s1_reg_F);

                        break;
                    }

                    // LD SP,(HHLL)
                    case SPEC_HEX_OP_ED_LD_SP_pHHLLp: // 0x7B
                    {
                        u16 sp7E; // 0x76 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0x10);
                        
                        sp7E = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                        sp298_PC += 2;
                        sp292_SP = SPEC_MK_16(ptr_spectrum_roms[sp7E + 1], ptr_spectrum_roms[sp7E]);

                        break;
                    }

                    // OP_7C
                    case SPEC_HEX_OP_ED_OP_7C: // 0x7C
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(4);

                        SPEC_M_NEG(s0_reg_A, s1_reg_F);

                        break;
                    }

                    // OP_7D
                    case SPEC_HEX_OP_ED_OP_7D: // 0x7D
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);

                        if(1);
                        sp28C_clock += SPEC_CPU_CYCLES(0x6);
                        
                        sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                        sp292_SP += 2;

                        break;
                    }

                    // OP_7E
                    case SPEC_HEX_OP_ED_OP_7E: // 0x7E
                    {
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);
                        
                        sp29B_IM = 3;

                        break;
                    }

                    // LDI
                    case SPEC_HEX_OP_ED_LDI: // 0xA0
                    {
                        u8 sp7D;

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);

                        sp7D = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s4_reg_D, s5_reg_E), sp7D);

                        s7_reg_L = (s7_reg_L + 1);
                        if (s7_reg_L == 0)
                        {
                            s6_reg_H++;
                        }

                        s5_reg_E++;
                        if (s5_reg_E == 0)
                        {
                            s4_reg_D++;
                        }

                        if (s3_reg_C-- == 0)
                        {
                            s2_reg_B--;
                        }

                        s1_reg_F = ((s1_reg_F & 0xC1)
                            | (sp7D & 0x28)
                            | (((s2_reg_B | s3_reg_C) > 0) << 2)
                        );

                        break;
                    }

                    // CPI
                    case SPEC_HEX_OP_ED_CPI: // 0xA1
                    {
                        u8 sp7C;
                        u16 sp7A;
                        u8 sp79;

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);

                        sp7C = s1_reg_F & 1;
                        sp79 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];

                        
                        sp7A = (s0_reg_A - sp79) & 0x1FF;  \
                        s1_reg_F = \
                            (sp7A & (SPEC_FLAG_SIGN | SPEC_FLAG_UNUSED_5 | SPEC_FLAG_UNUSED_3)) \
                            | (sp7A >> 8) \
                            | (((s0_reg_A & 0xF) < (sp79 & 0xF)) << 4) \
                            | (((s0_reg_A ^ sp79) & 0x80 & (sp7A ^ s0_reg_A)) >> 5) \
                            | SPEC_FLAG_ADD_SUB \
                            | ((!(sp7A)) << 6) \
                        ;
                        
                        if (++s7_reg_L == 0) { s6_reg_H++; }
                        if (s3_reg_C-- == 0) { s2_reg_B--; }
                        
                        s1_reg_F = (s1_reg_F & 0xFA) \
                            | sp7C \
                            | (((s2_reg_B | s3_reg_C) > 0) << 2) \
                        ;

                        break;
                    }

                    // INI
                    case SPEC_HEX_OP_ED_INI: // 0xA2
                    {
                        u16 sp76;

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp76 = spectrum_input_handling(sp28C_clock, s2_reg_B, s3_reg_C);

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s6_reg_H, s7_reg_L), sp76);

                        sp28C_clock += (s32) sp76 >> 8;
                        if (++s7_reg_L == 0)
                        {
                            s6_reg_H++;
                        }

                        s2_reg_B--;
                        s1_reg_F = (((ptr_pc_keyboard_table_alloc[s2_reg_B] ^ s3_reg_C) & 4)
                            | (s2_reg_B & 0xA8
                            | ((s2_reg_B > 0) << 6)
                            | 2)
                        );

                        break;
                    }

                    // OTI
                    case SPEC_HEX_OP_ED_OTI: // 0xA3
                    {
                        u8 sp75;

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp75 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, sp75);
                        s7_reg_L = (s7_reg_L + 1);

                        if (s7_reg_L == 0)
                        {
                            s6_reg_H++;
                        }

                        s2_reg_B--;
                        s1_reg_F = ((s1_reg_F & 1)
                            | 0x12
                            | (s2_reg_B & 0xA8)
                            | ((s2_reg_B == 0) << 6)
                        );

                        break;
                    }

                    // LDD
                    case SPEC_HEX_OP_ED_LDD: // 0xA8
                    {
                        u8 sp74;

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp74 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s4_reg_D, s5_reg_E), sp74);

                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }

                        if (s5_reg_E-- == 0)
                        {
                            s4_reg_D--;
                        }

                        if (s3_reg_C-- == 0)
                        {
                            s2_reg_B--;
                        }

                        s1_reg_F = ((s1_reg_F & 0xC1)
                            | (sp74 & 0x28)
                            | (((s2_reg_B | s3_reg_C) > 0) << 2)
                        );

                        break;
                    }

                    // CPD
                    case SPEC_HEX_OP_ED_CPD: // 0xA9
                    {
                        u8 sp73; // 0x6b ( -8)
                        u16 sp70; // 0x68 ( -8)
                        u8 sp6F; // 0x67 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp73 = s1_reg_F & 1;
                        sp6F = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];

                        sp70 = (s0_reg_A - sp6F) & 0x1FF;  \
                        s1_reg_F = \
                            (sp70 & (SPEC_FLAG_SIGN | SPEC_FLAG_UNUSED_5 | SPEC_FLAG_UNUSED_3)) \
                            | (sp70 >> 8) \
                            | (((s0_reg_A & 0xF) < (sp6F & 0xF)) << 4) \
                            | (((s0_reg_A ^ sp6F) & 0x80 & (sp70 ^ s0_reg_A)) >> 5) \
                            | SPEC_FLAG_ADD_SUB \
                            | ((!(sp70)) << 6) \
                        ;
                        
                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }
                        
                        if (s3_reg_C-- == 0)
                        {
                            s2_reg_B--;
                        }
                        
                        s1_reg_F = (s1_reg_F & 0xFA)
                            | sp73 \
                            | (((s2_reg_B | s3_reg_C) > 0) << 2) \
                        ;

                        break;
                    }

                    // IND
                    case SPEC_HEX_OP_ED_IND: // 0xAA
                    {
                        u16 sp6C; // 0x64 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp6C = spectrum_input_handling(sp28C_clock, s2_reg_B, s3_reg_C);

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s6_reg_H, s7_reg_L), sp6C);

                        sp28C_clock += SPEC_CPU_CYCLES(sp6C >> 8);
                        
                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }

                        s2_reg_B--;
                        s1_reg_F = (((ptr_pc_keyboard_table_alloc[(s2_reg_B)] ^ s3_reg_C ^ 4) & 4)
                            | ((s2_reg_B & 0xa8)
                            | (((s2_reg_B) > 0) << 6)
                            | 2));

                        break;
                    }

                    // OTD
                    case SPEC_HEX_OP_ED_OTD: // 0xAB
                    {
                        u8 sp6B; // 0x63 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp6B = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, sp6B);

                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }

                        s2_reg_B--;
                        s1_reg_F = (s1_reg_F & 1)
                            | 0x12 
                            | ((s2_reg_B) & 0xA8)
                            | (((s2_reg_B == 0)) << 6) ; // don't use ! here

                        break;
                    }

                    // LDIR
                    case SPEC_HEX_OP_ED_LDIR: // 0xB0
                    {
                        u8 sp6A; // 0x62 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp6A = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s4_reg_D, s5_reg_E), sp6A);

                        s7_reg_L++;
                        if (s7_reg_L == 0)
                        {
                            s6_reg_H++;
                        }

                        s5_reg_E++;
                        if (s5_reg_E == 0)
                        {
                            s4_reg_D++;
                        }

                        if (s3_reg_C-- == 0)
                        {
                            s2_reg_B--;
                        }

                        s1_reg_F = ((s1_reg_F & 0xC1)
                            | (sp6A & 0x28)
                            | (((s2_reg_B | s3_reg_C) > 0) << 2)
                        );

                        if ((s2_reg_B | s3_reg_C))
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }

                        break;
                    }

                    // CPIR
                    case SPEC_HEX_OP_ED_CPIR: // 0xB1
                    {
                        u8 sp69; // 0x61 ( -8)
                        u16 sp66; // 0x5e ( -8)
                        u8 sp65; // 0x5d (-8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);

                        sp69 = s1_reg_F & 1;
                        sp65 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];
                        
                        
                        sp66 = (s0_reg_A - sp65) & 0x1FF;  \
                        s1_reg_F = \
                            (sp66 & (SPEC_FLAG_SIGN | SPEC_FLAG_UNUSED_5 | SPEC_FLAG_UNUSED_3)) \
                            | (sp66 >> 8) \
                            | (((s0_reg_A & 0xF) < (sp65 & 0xF)) << 4) \
                            | (((s0_reg_A ^ sp65) & 0x80 & (sp66 ^ s0_reg_A)) >> 5) \
                            | SPEC_FLAG_ADD_SUB \
                            | ((!(sp66)) << 6) \
                        ;

                        if (++s7_reg_L == 0)
                        {
                            s6_reg_H++;
                        }
                        
                        if (s3_reg_C-- == 0)
                        {
                            s2_reg_B--;
                        }
                        
                        s1_reg_F = (s1_reg_F & 0xFA)
                            | sp69 \
                            | (((s2_reg_B | s3_reg_C) > 0) << 2) \
                        ;

                        if ((s1_reg_F & 0x44) == 4)
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }


                        break;
                    }

                    // INIR
                    case SPEC_HEX_OP_ED_INIR: // 0xB2
                    {
                        u16 sp62; // 0x5a ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp62 = spectrum_input_handling(sp28C_clock, s2_reg_B, s3_reg_C);

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s6_reg_H, s7_reg_L), sp62);

                        sp28C_clock += SPEC_CPU_CYCLES(sp62 >> 8);
                        
                        if (++s7_reg_L == 0)
                        {
                            s6_reg_H++;
                        }

                        s2_reg_B--;

                        s1_reg_F = (((ptr_pc_keyboard_table_alloc[s2_reg_B] ^ s3_reg_C) & 4)
                            | ((s2_reg_B & 0xA8)
                            | ((s2_reg_B > 0) << 6)
                            | 2)
                        );

                        if (s2_reg_B != 0)
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }

                        break;
                    }

                    // OTIR
                    case SPEC_HEX_OP_ED_OTIR: // 0xB3
                    {
                        u8 sp61; // 0x59 (-8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp61 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, sp61);

                        s7_reg_L++;
                        if (s7_reg_L == 0)
                        {
                            s6_reg_H++;
                        }

                        s2_reg_B--;
                        s1_reg_F = ((s1_reg_F & 1)
                            | 0x12
                            | (s2_reg_B & 0xA8)
                            | ((s2_reg_B == 0) << 6)
                        );

                        if (s2_reg_B != 0)
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }

                        break;
                    }

                    // LDDR
                    case SPEC_HEX_OP_ED_LDDR: // 0xB8
                    {
                        u8 sp60; // 0x58 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp60 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s4_reg_D, s5_reg_E), sp60);

                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }

                        if (s5_reg_E-- == 0)
                        {
                            s4_reg_D--;
                        }

                        if (s3_reg_C-- == 0)
                        {
                            s2_reg_B--;
                        }

                        s1_reg_F = ((s1_reg_F & 0xC1)
                            | (sp60 & 0x28)
                            | (((s2_reg_B | s3_reg_C) > 0) << 2)
                        );

                        if ((s2_reg_B | s3_reg_C) != 0)
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }

                        break;
                    }

                    // CPDR
                    case SPEC_HEX_OP_ED_CPDR: // 0xB9
                    {
                        u8 sp5F; // 0x57 ( -8)
                        u16 sp5C; // 0x54 ( -8)
                        u8 sp5B; // 0x53 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);

                        sp5F = s1_reg_F & 1;
                        sp5B = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];

                        sp5C = (s0_reg_A - sp5B) & 0x1FF;  \
                        s1_reg_F = \
                            (sp5C & (SPEC_FLAG_SIGN | SPEC_FLAG_UNUSED_5 | SPEC_FLAG_UNUSED_3)) \
                            | (sp5C >> 8) \
                            | (((s0_reg_A & 0xF) < (sp5B & 0xF)) << 4) \
                            | (((s0_reg_A ^ sp5B) & 0x80 & (sp5C ^ s0_reg_A)) >> 5) \
                            | SPEC_FLAG_ADD_SUB \
                            | ((!(sp5C)) << 6) \
                        ;

                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }
                        
                        if (s3_reg_C-- == 0)
                        {
                            s2_reg_B--;
                        }
                        
                        s1_reg_F = (s1_reg_F & 0xFA) \
                            | sp5F \
                            | (((s2_reg_B | s3_reg_C) > 0) << 2) \
                        ;

                        if ((s1_reg_F & 0x44) == 4)
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }

                        break;
                    }

                    // INDR
                    case SPEC_HEX_OP_ED_INDR: // 0xBA
                    {
                        u16 sp58; // 0x50 ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp58 = spectrum_input_handling(sp28C_clock, s2_reg_B, s3_reg_C);

                        SPEC_M_MEM_WRITE(SPEC_MK_16(s6_reg_H, s7_reg_L), sp58);

                        sp28C_clock += (s32) sp58 >> 8;
                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }

                        s2_reg_B--;
                        s1_reg_F = (((ptr_pc_keyboard_table_alloc[s2_reg_B] ^ s3_reg_C ^ 4) & 4)
                            | ((s2_reg_B & 0xA8)
                            | ((s2_reg_B > 0) << 6)
                            | 2)
                        );

                        if (s2_reg_B != 0)
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }

                        break;
                    }

                    // OTDR
                    case SPEC_HEX_OP_ED_OTDR: // 0xBB
                    {
                        u8 sp57; // --> 0x4f ( -8)

                        sp28C_clock += SPEC_CPU_CYCLES(0xc);
                        
                        sp57 = ptr_spectrum_roms[SPEC_MK_16(s6_reg_H, s7_reg_L)];
                        sp28C_clock += sub_GAME_7F0D37DC(sp28C_clock, s2_reg_B, s3_reg_C, sp57);

                        if (s7_reg_L-- == 0)
                        {
                            s6_reg_H--;
                        }

                        s2_reg_B--;
                        s1_reg_F = ((s1_reg_F & 1)
                            | 0x12
                            | (s2_reg_B & 0xA8)
                            | ((s2_reg_B == 0) << 6)
                        );

                        if (s2_reg_B != 0)
                        {
                            sp298_PC -= 2; \
                            sp28C_clock += SPEC_CPU_CYCLES(5);
                        }

                        break;
                    }

                    default:
                        sp28C_clock += SPEC_CPU_CYCLES(0x4);
                        break;

                } // end switch spE3 = ptr_spectrum_roms[sp298_PC]

                break;

            } // end ED opcode

            // XOR NN
            /***
            *
            */
            case SPEC_HEX_OP_XOR_NN: // 0xEE
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                s0_reg_A ^= ptr_spectrum_roms[sp298_PC];
                if(1);
                s1_reg_F = ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6));
                sp298_PC += 1;

                break;
            }

            // RST 28
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0028H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_28: // 0xEF
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) sp298_PC >> 8);

                sp298_PC = 0x28;

                break;
            }

            // RET P
            /***
            * If S flag is not set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_P: // 0xF0
            {
                sp28C_clock += SPEC_CPU_CYCLES(5);

                if(1);
                if (!(s1_reg_F & 0x80))
                {
                    sp28C_clock += SPEC_CPU_CYCLES(6);

                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }

                break;
            }

            // POP AF
            /***
            * The top two bytes of the external memory last-in, first-out (LIFO)
            * stack are popped to register pair AF. SP holds the 16-bit address
            * of the current top of the stack. This instruction first loads to
            * the low-order portion of RR, the byte at the memory location
            * corresponding to the contents of SP; then SP is incremented and
            * the contents of the corresponding adjacent memory location are
            * loaded to the high-order portion of RR and the SP is now incremented
            * again.
            */
            case SPEC_HEX_OP_POP_AF: // 0xF1
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                s1_reg_F = ptr_spectrum_roms[sp292_SP + 0];
                s0_reg_A = ptr_spectrum_roms[sp292_SP + 1];
                sp292_SP += 2;

                break;
            }

            // JP P, HHLL
            /***
            * If S flag is not set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_P_HHLL: // 0xF2
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 0x80))
                {
                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // DI
            /***
            * Disables the maskable interrupt by resetting the interrupt
            * enable flip-flops (IFF1 and IFF2).
            */
            case SPEC_HEX_OP_DI: // 0xF3
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                sp29C_IFF2 = 0;
                sp29D_IFF1 = 0;
                sp285_exit = 0;

                break;
            }

            // CALL P, HHLL
            /***
            * If flag S is not set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_P_HHLL: // 0xF4
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (!(s1_reg_F & 0x80))
                {
                    if(1);
                    sp28C_clock += SPEC_CPU_CYCLES(7);
                    
                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC + 2);
                    
                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);
                    
                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // PUSH AF
            /***
            * The contents of the register pair BC are pushed to the external
            * memory last-in, first-out (LIFO) stack. SP holds the 16-bit
            * address of the current top of the Stack. This instruction first
            * decrements SP and loads the high-order byte of register pair RR
            * to the memory address specified by SP. Then SP is decremented again
            * and loads the low-order byte of RR to the memory location
            * corresponding to this new address in SP.
            */
            case SPEC_HEX_OP_PUSH_AF: // 0xF5
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, s1_reg_F);
                
                SPEC_M_MEM_WRITE(sp292_SP + 1, s0_reg_A);
                
                break;
            }

            // OR NN
            /***
            *
            */
            case SPEC_HEX_OP_OR_NN: // 0xF6
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                s0_reg_A |= ptr_spectrum_roms[sp298_PC];
                if(1);
                s1_reg_F = 
                    ptr_pc_keyboard_table_alloc[s0_reg_A]
                    | ((s0_reg_A & 0xA8)
                    | ((!s0_reg_A) << 6));
                
                sp298_PC += 1;

                break;
            }

            // RST 30
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0030H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_30: // 0xF7
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;

                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);
                
                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) sp298_PC >> 8);

                sp298_PC = 0x30;

                break;
            }

            // RET M
            /***
            * If S flag is set, the byte at the memory location specified
            * by the contents of SP is moved to the low-order 8 bits of PC.
            * SP is incremented and the byte at the memory location specified by
            * the new contents of the SP are moved to the high-order eight bits of
            * PC.The SP is incremented again. The next op code following this
            * instruction is fetched from the memory location specified by the PC.
            * This instruction is normally used to return to the main line program at
            * the completion of a routine entered by a CALL instruction.
            * If condition X is false, PC is simply incremented as usual, and the
            * program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_RET_M: // 0xF8
            {
                sp28C_clock += SPEC_CPU_CYCLES(5);

                if(1);
                if (s1_reg_F & 0x80)
                {
                    sp28C_clock += SPEC_CPU_CYCLES(6);
                    sp298_PC = SPEC_MK_16(ptr_spectrum_roms[sp292_SP + 1], ptr_spectrum_roms[sp292_SP]);
                    sp292_SP += 2;
                }

                break;
            }

            // LD SP, HL
            /***
            * The contents of HL are loaded to SP.
            */
            case SPEC_HEX_OP_LD_SP_HL: // 0xF9
            {
                sp28C_clock += SPEC_CPU_CYCLES(6);

                sp292_SP = ((sp287_index_mode == 0) ? (SPEC_MK_16(s6_reg_H, s7_reg_L)) : ((sp287_index_mode == 1) ? sp296_IX : sp294_IY));

                break;
            }

            // JP M, HHLL
            /***
            * If S flag is set, the instruction loads operand NN
            * to PC, and the program continues with the instruction
            * beginning at address NN.
            * If condition X is false, PC is incremented as usual, and
            * the program continues with the next sequential instruction.
            */
            case SPEC_HEX_OP_JP_M_HHLL: // 0xFA
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (s1_reg_F & 0x80)
                {
                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // EI
            /***
            * Sets both interrupt enable flip flops (IFFI and IFF2) to a
            * logic 1 value, allowing recognition of any maskable interrupt.
            */
            case SPEC_HEX_OP_EI: // 0xFB
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                sp29D_IFF1 =  sp29C_IFF2 = 1;
                sp285_exit = 0;

                break;
            }

            // CALL M, HHLL
            /***
            * If flag S is set, this instruction pushes the current
            * contents of PC onto the top of the external memory stack, then
            * loads the operands NN to PC to point to the address in memory
            * at which the first op code of a subroutine is to be fetched.
            * At the end of the subroutine, a RET instruction can be used to
            * return to the original program flow by popping the top of the
            * stack back to PC. If condition X is false, PC is incremented as
            * usual, and the program continues with the next sequential
            * instruction. The stack push is accomplished by first decrementing
            * the current contents of SP, loading the high-order byte of the PC
            * contents to the memory address now pointed to by SP; then
            * decrementing SP again, and loading the low-order byte of the PC
            * contents to the top of the stack.
            */
            case SPEC_HEX_OP_CALL_M_HHLL: // 0xFC
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xa);

                if (s1_reg_F & 0x80)
                {
                    if(1);
                    
                    sp28C_clock += SPEC_CPU_CYCLES(7);

                    sp292_SP = sp292_SP - 2;

                    SPEC_M_MEM_WRITE(sp292_SP, (sp298_PC + 2));
                    
                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC + 2) >> 8);
                    
                    sp298_PC = SPEC_MK_16((ptr_spectrum_roms[sp298_PC + 1]), ptr_spectrum_roms[sp298_PC]);
                }
                else
                {
                    sp298_PC += 2;
                }

                break;
            }

            // FD Opcodes
            /***
            *
            */
            case SPEC_HEX_OP_FD_Opcodes: // 0xFD
            {
                sp28C_clock += SPEC_CPU_CYCLES(4);

                sp286_next_index_mode = 2;
                sp285_exit = 0;

                break;
            }

            // CP NN
            /***
            *
            */
            case SPEC_HEX_OP_CP_NN: // 0xFE
            {
                sp28C_clock += SPEC_CPU_CYCLES(7);

                SPEC_M_COMPARE(s0_reg_A, ptr_spectrum_roms[sp298_PC], s1_reg_F);

                if(1);
                sp298_PC += 1;

                break;
            }

            // RST 38
            /***
            * The current PC contents are pushed onto the external memory stack,
            * and the Page 0 memory location assigned by operand N is loaded to
            * PC. Program execution then begins with the op code in the address
            * now pointed to by PC. The push is performed by first decrementing
            * the contents of SP, loading the high-order byte of PC to the
            * memory address now pointed to by SP, decrementing SP again, and
            * loading the low-order byte of PC to the address now pointed to by
            * SP. The Restart instruction allows for a jump to address 0038H.
            * Because all addresses are stored in Page 0 of memory, the high-order
            * byte of PC is loaded with 0x00.
            */
            case SPEC_HEX_OP_RST_38: // 0xFF
            {
                sp28C_clock += SPEC_CPU_CYCLES(0xb);

                sp292_SP = sp292_SP - 2;
                
                SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC) >> 8);
                
                sp298_PC = 0x38;

                break;
            }
        }
    }

    if (sp28C_clock >= sp280_unk_clock && sp285_exit != 0)
    {
        sp28C_clock -= sp280_unk_clock;
        
        if (sp29D_IFF1 != 0)
        {
            if (ptr_spectrum_roms[sp298_PC] == 0x76)
            {
                sp298_PC += 1;
            }

            sp29C_IFF2 = 0;
            sp29D_IFF1 = 0;
            
            sp28C_clock += SPEC_CPU_CYCLES(5);
            
            switch (sp29B_IM)
            {
                case 0:
                case 1:
                case 2:
                {
                    sp28C_clock += SPEC_CPU_CYCLES(8);
                    sp292_SP -= 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC) >> 8);

                    sp298_PC = 0x38;

                    break;
                }

                case 3:
                {
                    s32 sp4C;
        
                    sp28C_clock += SPEC_CPU_CYCLES(0xe);
                    
                    sp4C = (ptr_spectrum_roms[SPEC_MK_16(sp29E_reg_I, 0xFF) + 1] << 8) | ptr_spectrum_roms[SPEC_MK_16(sp29E_reg_I, 0xFF)];
                    sp292_SP -= 2;

                    SPEC_M_MEM_WRITE(sp292_SP, sp298_PC);

                    SPEC_M_MEM_WRITE(sp292_SP + 1, (s32) (sp298_PC) >> 8);

                    sp298_PC = sp4C;
                    
                    break;
                }
            }
        }
    }

    spectrum_header16_15 = s0_reg_A;
    byte_CODE_bss_8008E339 = s1_reg_F;
    byte_CODE_bss_8008E33A = s2_reg_B;
    byte_CODE_bss_8008E33B = s3_reg_C;
    off_CODE_bss_8008E33C = s4_reg_D;
    byte_CODE_bss_8008E33D = s5_reg_E;
    byte_CODE_bss_8008E33E = s6_reg_H;
    byte_CODE_bss_8008E33F = s7_reg_L;
    off_CODE_bss_8008E340 = sp2A6_alt_reg_A;
    byte_CODE_bss_8008E341 = sp2A5_alt_reg_F;
    byte_CODE_bss_8008E342 = sp2A4_alt_reg_B;
    byte_CODE_bss_8008E343 = sp2A3_alt_reg_C;
    off_CODE_bss_8008E344 = sp2A2_alt_reg_D;
    byte_CODE_bss_8008E345 = sp2A1_alt_reg_E;
    byte_CODE_bss_8008E346 = sp2A0_alt_reg_H;
    byte_CODE_bss_8008E347 = sp29F_alt_reg_L;
    spec_IFF2_lower = sp29D_IFF1;
    spec_IFF2_upper = sp29C_IFF2;
    spec_I = sp29E_reg_I;
    spec_R = sp2A7_reg_R;
    sp2A7_reg_R = sp288_unk_R;
    spec_IM = sp29B_IM;
    spec_IX = sp296_IX;
    spec_IY = sp294_IY;
    spec_SP = sp292_SP;
    spec_PC = sp298_PC;

#undef SPEC_XM_SET_INDEX_R
#undef SPEC_XM_GET_INDEX_R
}
