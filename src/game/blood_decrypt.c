#include "blood_decrypt.h"

// address 0x7F01CAE0 NTSC
u8 *decrypt_bleeding_animation_data(u8 *arg0, u8 arg1, u8 arg2, u8 *arg3, u8 *arg4)
{
    u8 var_a2;
    u8 var_a3;
    u8 temp_v0;
    u8 var_v1;
    u8 next;

    temp_v0 = *arg0++;
    *arg4 = temp_v0;

    do
    {
        s32 unused;
        var_v1 = *arg0++;
        unused = var_v1;
        
        var_a2 = 0xFF;

        if (var_v1 == 0xFF)
        {
            var_a3 = 0;
            for (var_v1 = *arg0++; var_v1 != 0xFF; var_a2 ^= 0xff, var_v1 = *arg0++ )
            {
                var_a3 += var_v1;

                while (var_v1-- > 0)
                {
                    *arg3++ = var_a2;
                }
            }
            
            while (var_a3++ < arg1)
            {
                *arg3++ = var_a2;
            }
            
            arg2--;
        }
        else
        {
            var_a3 = temp_v0 + (var_v1 & 0x1f);
            var_v1 = (var_v1 >> 5) + 1;
            arg2 -= var_v1;
            
            do
            {
                var_a2 = var_a3;
                while (var_a2-- > 0)
                {
                    *arg3++ = 0xff;
                }
                
                var_a2 = arg1 - var_a3;
                while (var_a2-- > 0)
                {
                    *arg3++ = 0;
                }
            } while (--var_v1 > 0);
        }
    } while (arg2 > 0);
    
    return arg0;
}


void sub_GAME_7F01CC94(u8* arg0, u16 arg1, u8* arg2)
{
    while (arg1-- > 0)
    {
        *arg2++ = (arg0[0] & 0xF0) | (arg0[1] >> 4);
        arg0 += 2;
    }
}


// Address 0x7F01CCEC NTSC
void sub_GAME_7F01CCEC(u8 *arg0, u8 arg1, u8 arg2, u8 *arg3, u8 arg4)
{
    s16 i;
    s16 j;

    u32 var_t0;
    u32 var_t1;
    
    s16 var_t5;

    s16 var_s4;

    u8 tempt9;

    for (i = 0; i < arg2; i++)
    {
        for (j = 0; j < arg1; j++)
        {
            var_t0 = 0;
            var_t1 = 0;
            
            var_t5 = ((i - arg4) < 0) ? 0 : (i - arg4);

            while ((((arg2 - 1) < (i + arg4)) ? (arg2 - 1) : (i + arg4)) >= var_t5)
            {
                var_s4 = ((j - arg4) < 0) ? 0 : (j - arg4);
                
                while ((((arg1 - 1) < (j + arg4)) ? (arg1 - 1) : (j + arg4)) >= var_s4)
                {
                    var_t1 += arg0[var_s4 + (var_t5 * arg1)];
                    
                    var_s4 += 1;
                    var_t0 += 1;
                }

                var_t5 += 1;
            }

            tempt9  = (u8) ((u32) ((var_t0 >> 1) + var_t1) / var_t0);
            arg3[j + (i * arg1)] = tempt9;
        }
    }
}


// Averages 4 pixel data, ending on "second" row.
// Address 0x7F01CEEC NTSC
void sub_GAME_7F01CEEC(u8 *arg0, s32 arg1, u8 *arg2)
{
    s32 i;
    s32 j;

    arg0 += 0x61;
    arg2 += 0x61;

    for (i = 1; i < arg1 - 1; i++, arg2 += 2, arg0 += 2) {
        for (j = 1; j < 0x5f; j++, arg2++, arg0++) {
            *arg2 = (arg0[-1] + arg0[0] + arg0[-0x61] + arg0[-0x60] + 2) >> 2;
        }
    }
}


// Averages 4 pixel data, ending on "first" row.
// Address 0x7F01D02C NTSC
void sub_GAME_7F01D02C(u8 *arg0, s32 arg1, u8 *arg2)
{
    s32 i;
    s32 j;

    arg0 += 0x61;
    arg2 += 0x61;

    for (i = 1; i < arg1 - 1; i++, arg2 += 2, arg0 += 2) {
        for (j = 1; j < 0x5f; j++, arg2++, arg0++) {
            *arg2 = (arg0[1] + arg0[0] + arg0[0x61] + arg0[0x60] + 2) >> 2;
        }
    }
}


/**
 * Address: 7F01D16C
 * 
 * Converts the decoded blood frame from column-major to row-major order.
 */
void bloodImgTranspose(u8 *src, s32 srcwidth, s32 srcheight, u8 *dst)
{
    s32 pixelcount;
    uintptr_t rowend;
    uintptr_t var_t2;
    u8 *var_t0;
    u8 *var_v1;
    uintptr_t t1;

    pixelcount = srcwidth * srcheight;
    var_v1 = src;
    var_t0 = dst;
    t1 = (uintptr_t)(src + pixelcount);
    var_t2 = (uintptr_t)(src + srcwidth);

    do
    {
        rowend = var_t2;

        do
        {
            *var_t0 = *var_v1++;
            var_t2 += 1;
            var_t0 += srcheight;
            
        } while ((uintptr_t) var_v1 < rowend);

        var_t0 = (var_t0 - (pixelcount)) + 1;
        
    } while ((uintptr_t) var_v1 < t1);
}


u8 *sub_GAME_7F01D1C0(u8 *arg0, s32 arg1, s32 arg2, u8 *arg3);


