#include <ultra64.h>
#include "game/othermodemicrocode.h"
#include <limits.h>
#include "bg.h"
#include "bgroomtrans.h"
#include "bondview.h"
#include "dyn.h"
#include "math_atan2f.h"
#include "gbi_extension.h"
#include "glass.h"
#include "image_bank.h"
#include "lv.h"
#include "objective_status.h"
#include "random.h"

#define BULLET_SPARKS_MAX 20
#define BULLET_MOVING_SPARKS_MAX 50
#define GAUGE_BAR_VERTEX_PAIR_STRIDE (2 * sizeof(struct WatchVertex))

//D:80040960
struct rgba_u8 g_BulletSparkColors[8] = {
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xC8, 0xFF },
    { 0xFF, 0x00, 0x00, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0 },
    { 0 }
};
/* Originally a single 16-byte zero-initialized Vtx template; decomp split
 * it into 4 separate u32 words (D_80040980..D_8004098C, see glass.h) and
 * this function read all 16 bytes back through a cast to Vtx*, 12 bytes
 * past the u32's actual size (ASan-caught 2026-08-22). Typed correctly
 * here instead. */
Vtx D_80040980 = {0};


// something explosion related
// size of each item is 0x2c (see bullet_spark_create)
//CODE.bss:8007A170
s_bullet_spark g_BulletSparkArray[BULLET_SPARKS_MAX];

#ifndef VERSION_EU

//CODE.bss:8007A4E0
s_moving_bullet_spark g_MovingBulletSparkArray[BULLET_MOVING_SPARKS_MAX];

#endif


#if defined(LEFTOVERDEBUG)
/*
  Render Health Bars
  AI Comment: This function populates a radial array of HUD elements (HealthSegments) with position and color data
  based on a damage value (HealthValue) and a display mode (isArmour). It loops through 23 segments, calculating their
  screen-space coordinates and visual properties using trigonometric functions.
  @healthSegments : A pointer to an Array of 46 vertices.
  @isArmour : Armour/Health if positive/negative
  @numsegments : Not Used
  @HealthValue : amount of health/armour 0-10
  @Address: 7F0A2F30
*/
void hudMakeDamageSegments(struct damage_display_val *HealthSegments, s32 numSegments, s32 isArmour, f32 HealthValue)
{
	s32 unused;
    s32 i;
    s32 pairIndex;
    f32 angleRadians;

    HealthValue *= 8;


    //for 145.2 to 35.2 degrees, calculate health/armour
    for (i=0; i<23; i++)
    {
        //This line calculates an angle in radians, starting from 142° (cast truncated) and decreasing by 5° per iteration.
        angleRadians = ((f32) (s32)(142.5 - (i*5))* M_PI_F * 2) / 360;

        for (pairIndex = 0; pairIndex < 2; pairIndex++)
        {
            s16 radialOffsetX = (((sinf(angleRadians) * 4 * 130 * (6 - pairIndex)) / 5) * isArmour);
            s16 radialOffsetZ = (((cosf(angleRadians) * 4) * 130 * (6 - pairIndex)) / 5);

            HealthSegments->pos.x    = (radialOffsetX + 1);
            HealthSegments->pos.y    = 0;
            HealthSegments->pos.z    = -radialOffsetZ;
            HealthSegments->normal.x = 0;
            HealthSegments->normal.y = 0;
            HealthSegments->normal.z = 0;
            HealthSegments->colour.r = 255;
            HealthSegments->colour.g = 255;
            HealthSegments->colour.b = 255;

            if (isArmour >= TRUE) //armour shade
            {
                HealthSegments->colour.r = (int)(96 - (cosf(angleRadians) * 96));
                HealthSegments->colour.g = (int)(127 - (cosf(angleRadians) * 127));
                HealthSegments->colour.b = 255;
            }
            else if (isArmour < FALSE) //health shade
            {
                HealthSegments->colour.g =  (int)(127 - (cosf(angleRadians) * 127));
                HealthSegments->colour.b = (int)(32 - (cosf(angleRadians) * 32));
            }

            // segments 0-9 are single, 10-22 are doubled with single gaps
            // IF i < damage fill, if fractional, shade else no fill.
            if (i < 10) //145 to 95
            {
                //full shade
                if (i <= (((int)HealthValue * 2) - 1))
                {
                    HealthSegments->colour.a = 255;
                }
                // Fraction fill
                else if ((i < (int)(HealthValue * 2.0f)) && (i > (((int)HealthValue * 2) - 1))) //yes, it looks like one is float, the other is cast
                {
                    HealthSegments->colour.a = (int)((HealthValue - (int)HealthValue) * 207) + 48;
                }
                else //no fill
                {
                    HealthSegments->colour.a = 48;
                }
            }
            else if (i >= 10) //95 to 35
            {
                if ( i <= (9 + ((HealthValue - 5.0f) * 4)))
                {
                    HealthSegments->colour.a = 255;
                }
                else if (i <= (((int)(((HealthValue - 5.0f) * 4) + 0.5f) + 9)) && (i > (((int)(HealthValue - 5.0f) * 2) + 8)))
                {
                    HealthSegments->colour.a = (int)((HealthValue - (int)HealthValue) * 207) + 48;
                }
                else
                {
                    HealthSegments->colour.a = 48;
                }
            }
            HealthSegments++;
        }
    }
}
#endif

#if !defined(LEFTOVERDEBUG)
void hudMakeDamageSegments(struct damage_display_val *HealthSegments, s32 numSegments, s32 isArmour, f32 HealthValue)
{
	s32 new_var2;
	f32 new_var;
	s32 pairIndex;
	s32 i;
	s16 temp_s1;
	s32 sp80;
	f32 temp_f18;
	f32 temp_f4;
	f32 sp74;
	f32 angleRadians;
	HealthValue *= 8.0f;
	sp80 = 0;

	for (i = 0; i < 23; i++)
	{
		temp_f18 = (f32) ((s32) (142.5 - ((f64) sp80)));
		angleRadians = ((temp_f18 * M_PI_F) * ((f32) 2)) / 360.0f;
		for (pairIndex = 0; pairIndex < 2; pairIndex++)
		{
	        sp74 = (f32) isArmour;
			temp_s1 = (s16) ((s32) (((((sinf(angleRadians) * 4.0f) * 130.0f) * ((f32) (6 - pairIndex))) / 5.0f) * sp74));
			temp_f4 = cosf(angleRadians) * 4.0f;
			HealthSegments->pos.x = temp_s1 + 1;
			HealthSegments->pos.y = 0;
			HealthSegments->normal.x = 0;
			HealthSegments->normal.y = 0;
			new_var = HealthValue - 5.0f;
			HealthSegments->normal.z = 0;
			HealthSegments->colour.r = 0xFF;
			HealthSegments->colour.g = 0xFF;
			temp_s1 = ((temp_f4 * 130.0f) * ((f32) (6 - pairIndex))) / 5;
			HealthSegments->colour.b = 0xFF;
			HealthSegments->pos.z = (s16) (-((s32) temp_s1));
			if (isArmour > 0)
			{
				HealthSegments->colour.r = (s8) ((s32) (96.0f - (cosf(angleRadians) * 96.0f)));
				HealthSegments->colour.g = (s8) ((s32) (127.0f - (cosf(angleRadians) * 127.0f)));
				HealthSegments->colour.b = 0xFF;
			}
			else if (isArmour < 0)
			{
				HealthSegments->colour.g = (s8) ((s32) (127.0f - (cosf(angleRadians) * 127.0f)));
				HealthSegments->colour.b = (s8) ((s32) (32.0f - (cosf(angleRadians) * 32.0f)));
			}
			if (i < 10)
			{
				if (((((s32) HealthValue) * 2) - 1) >= i)
				{
					HealthSegments->colour.a = 0xFF;
				}
				else if ((i < ((s32) (2.0f * HealthValue))) && (((((s32) HealthValue) * 2) - 1) < i))
				{
					HealthSegments->colour.a = (s8) (((s32) ((HealthValue - ((f32) ((s32) HealthValue))) * 207.0f)) + 0x30);
				}
				else
				{
					HealthSegments->colour.a = 0x30;
				}
			}
			else if (i >= 10)
			{
				if (((f32) i) <= (9.0f + ((HealthValue - 5.0f) * 4.0f)))
				{
					HealthSegments->colour.a = 0xFF;
				}
				else
				{
					new_var2 = i;
					if (((((s32) ((new_var * 4.0f) + 0.5f)) + 9) >= new_var2) && (((((s32) (HealthValue - 5.0f)) * 2) + 8) < new_var2))
					{
						HealthSegments->colour.a = (s8) (((s32) ((HealthValue - ((f32) ((s32) HealthValue))) * 207.0f)) + 0x30);
					}
					else
					{
						HealthSegments->colour.a = 0x30;
					}
				}
			}
			HealthSegments += 1;
		}
		sp80 += 5;
	}
}
#endif


/**
 * Address: 7F0A3330
 *
 * Creates the display list for HUD and watch health and armor bars.
 */
Gfx *buildGaugeBarDL(Gfx *gdl, uintptr_t vtxaddr, s32 numvertices)
{
    s8 i;

    for (i = 0; i <= (numvertices / 2 - 2); i++) 
    {
        gSPVertex(gdl++, vtxaddr, 4, 0);

        if (i >= 9) 
        {
            if ((i + 3) % 4) 
            {
                gSP2Triangles(gdl++, 0, 1, 2, 0, 1, 2, 3, 0);
            }
        } 
        else if (i < 9) 
        {
            if ((i & 1) == 0) 
            {
                gSP2Triangles(gdl++, 0, 1, 2, 0, 1, 2, 3, 0);
            }
        }

        vtxaddr += GAUGE_BAR_VERTEX_PAIR_STRIDE;
    }

    gSPEndDisplayList(gdl++);

    return gdl;
}


/**
 * Address: 7F0A33F8
 */
void sub_GAME_7F0A33F8(struct WatchVertex *vtx, s32 numverts, f32 scale, s32 arg3)
{
    f32 angle;
    s32 i;
    s16 sinval;
    s16 cosval;

    if (arg3)
    {
        vtx->coord1.x = 1;
        vtx->coord1.y = 0;
        vtx->coord1.z = 0;
        vtx->coord2.x = 0;
        vtx->coord2.y = 0;
        vtx->coord2.z = 0;
        vtx->color.r = 0;
        vtx->color.g = 0x2c;
        vtx->color.b = 0;
        vtx->color.a = 0xb0;

        vtx++;
    }

    for (i = 7; i <= (numverts - 7); i += 2)
    {
        angle = ((f32)i * M_PI_F) / numverts;
        sinval = sinf(angle) * 520.0f * scale;
        cosval = cosf(angle) * 520.0f * scale;

        vtx->coord1.x = 1 + sinval;
        vtx->coord1.y = 0;
        vtx->coord1.z = -cosval;
        vtx->coord2.x = 0;
        vtx->coord2.y = 0;
        vtx->coord2.z = 0;
        vtx->color.r = 0 - (cosf(angle) * 0);
        vtx->color.g = 44.0f - (cosf(angle) * 20.0f);
        vtx->color.b = 0 - (cosf(angle) * 0);
        vtx->color.a = 0xb0;

        vtx++;

        if ((i != 0) && (i < numverts))
        {
            vtx->coord1.x = 1 + -sinval;
            vtx->coord1.y = 0;
            vtx->coord1.z = -cosval;
            vtx->coord2.x = 0;
            vtx->coord2.y = 0;
            vtx->coord2.z = 0;

            vtx->color.r = 0xFF;
            vtx->color.g = 0xFF;
            vtx->color.b = 0xFF;

            vtx->color.r = 0 - (cosf(angle) * 0);
            vtx->color.g = 44.0f - (cosf(angle) * 20.0f);
            vtx->color.b = 0 - (cosf(angle) * 0);
            vtx->color.a = 0xb0;

            vtx++;
        }
    }
}


/**
 * Address: 7F0A3978
 */
Gfx *draw_watch_background(Gfx *gdl, struct WatchVertex *watch_verts, s32 unused_arg2, s32 drawFan)
{
    s8 i;
    struct WatchVertex *orig;

    if (drawFan) 
    {
        struct WatchVertex *vtx;

        orig = watch_verts;
        watch_verts++;
        vtx = watch_verts;

        i = 7;

        gSPVertex(gdl++, &vtx[14], 4, 0);
        gSPVertex(gdl++, orig, 1, 4);
        gSP2Triangles(gdl++, 2, 4, 3, 0, 0, 0, 0, 0);

        for (; i >= 0; i--) {
            gSPVertex(gdl++, &vtx[2 * i], 4, 0);
            gSPVertex(gdl++, orig, 1, 4);
            gSP2Triangles(gdl++, 0, 4, 2, 0, 1, 3, 4, 0);
        }

        gSP2Triangles(gdl++, 0, 1, 4, 0, 0, 0, 0, 0);
    } 
    else 
    {
        for (i = 0; i < 8; i++)
        {
            gSPVertex(gdl++, watch_verts, 4, 0);
            gSP2Triangles(gdl++, 0, 1, 2, 0, 1, 2, 3, 0);
            watch_verts += 2;
        }
    }

    gSPEndDisplayList(gdl++);

    return gdl;
}


/**
 * Setup watch rectangles in the usual manner.
 * This is called to setup the screen select rectangles, but note
 * that the colors are overwritten in set_page_rectangle_colors.
 * Also used to initialize watch static.
 * @param vtx: Pointer to first vertex in a {@code struct WatchRectangle}.
 * @param startx:
 * @param startz:
 * @param width:
 * @param height:
 * @param horizontal_offset:
 * @param vertical_offset:
*/
struct WatchVertex *setup_watch_rectangles(struct WatchVertex *vtx, s32 startx, s32 startz, s32 width, s32 height, s32 horizontal_offset, s32 vertical_offset)
{
    s32 i;
    s32 j;
    s32 xval;
    s32 zval;

    i = 0;
    j = 0;
    xval = startx + horizontal_offset;

    if (i);
    if (j);
    if (vtx);
    if (width);
    if (height);

    for (i = 0; i < 2; i++, xval += width)
    {
        startz ^= 0;
    
        if (vertical_offset);
        if (height);

        for (j = 0, zval = startz + vertical_offset; j < 2; j++, zval += height)
        {
            vtx->coord1.AsArray[0] = xval;
            vtx->coord1.AsArray[1] = 0;
            vtx->coord1.AsArray[2] = zval;

            vtx->coord2.AsArray[0] = 0;
            vtx->coord2.AsArray[1] = 0;
            vtx->coord2.AsArray[2] = 0;

            vtx->color.rgba[0] = 0x20;
            vtx->color.rgba[1] = 0x70;
            vtx->color.rgba[2] = 0x20;
            vtx->color.rgba[3] = 0xF0;

            vtx++;
        }
    }

    return vtx;
}


Gfx *sub_GAME_7F0A3B40(Gfx *gdl, s32 *arg1)
{
    gSPVertex(gdl++, arg1, 4, 0);

    // gfxdis can't parse this, but maybe?: gSPModifyVertex(gdl++, 16, 0, 0x2110);
    // manual specification:
    {								\
        Gfx *_g = (Gfx *)(gdl++);		\
        _g->words.w0 = 0xB1000032;	\
        _g->words.w1 = 0x2110;		\
    }

    return gdl;
}


// unreferenced
void unused_7F0A3B70(s32 arg0, struct rgba_u8 *arg1)
{
    arg1->r = g_BulletSparkColors[arg0].r;
    arg1->g = g_BulletSparkColors[arg0].g;
    arg1->b = g_BulletSparkColors[arg0].b;
    arg1->a = g_BulletSparkColors[arg0].a;
}


// unreferenced
void unused_7F0A3BA4(s32 arg0, struct rgba_u8 *arg1)
{
    g_BulletSparkColors[arg0].r = arg1->r;
    g_BulletSparkColors[arg0].g = arg1->g;
    g_BulletSparkColors[arg0].b = arg1->b;
    g_BulletSparkColors[arg0].a = arg1->a;
}


/**
 * Address: 7F0A3BD8
 */
void bullet_sparks_reset(void)
{
    s32 i;
    s32 start_index;

    if (1) { start_index = 0; }

    for (i = start_index; (i < BULLET_SPARKS_MAX) ^ 0; i++)
    {
        g_BulletSparkArray[i].unk0C = 0;
        g_BulletSparkArray[i].lifetime = 0;
        g_BulletSparkArray[i].age = 0;
    }
}


/**
 * Address: 7F0A3C08
 */
void bullet_sparks_init(s_bullet_spark *spark, coord3d *arg1, s32 arg2, f32 arg3, s16 arg4)
{
    f32 angle;

    angle = randomGetNext();
    angle *= (1.0f / M_U32_MAX_VALUE_F);
    angle *= M_TAU_F;

    spark->age = 0;
    spark->unk06 = arg4;

    if (arg2 == 4)
    {
        spark->lifetime = 1;
        spark->unk08 = 1.0f;
        spark->unk0C = flareimage2;
    }
    else if (arg2 == 1)
    {
        spark->lifetime = 11;
        spark->unk08 = 0.5f;
        spark->unk0C = explosion_smokeimages;
    }
    else if (arg2 == 3)
    {
        spark->lifetime = 9;
        spark->unk08 = 0.5f;
        spark->unk0C = scattered_explosions;
    }
    else if (arg2 == 6)
    {
        spark->lifetime = 100;
        spark->unk08 = 0.0f;
        spark->unk0C = flareimage2;
    }
    else
    {
        spark->lifetime = 11;
        spark->unk08 = 0.5f;
        spark->unk0C = explosion_smokeimages;
    }

    spark->unk28 = g_BulletSparkColors[arg2].r;
    spark->unk29 = g_BulletSparkColors[arg2].g;
    spark->unk2A = g_BulletSparkColors[arg2].b;
    spark->unk2B = g_BulletSparkColors[arg2].a;

    spark->unk10 = arg1->x;
    spark->unk14 = arg1->y;
    spark->unk18 = arg1->z;

    arg3 *= 1.0f + ((f32)randomGetNext() * (1.0f / M_U32_MAX_VALUE_F) * 0.25f);
    arg3 *= M_SQRT2_F;
    spark->unk24 = arg3;

    spark->unk1c = cosf(angle) * arg3;
    spark->unk20 = sinf(angle) * arg3;
}


/**
 * Address: 7F0A3E1C
 */
s_bullet_spark *bullet_spark_create(coord3d *arg0, s32 arg1, f32 arg2, s16 arg3)
{
    s_bullet_spark *ptr;

    for (ptr = &g_BulletSparkArray[0]; ptr < &g_BulletSparkArray[BULLET_SPARKS_MAX]; ptr++)
    {
        if (ptr->lifetime == 0)
        {
            bullet_sparks_init(ptr, arg0, arg1, arg2, arg3);
            return ptr;
        }
    }

    return NULL;
}


/**
 * Address: 7F0A3EA0
 */
void bullet_sparks_update(void)
{
    s_bullet_spark *thing = &g_BulletSparkArray[0]; \
    s_bullet_spark *end = g_BulletSparkArray + BULLET_SPARKS_MAX;

    for (; thing < end; thing++)
    {
        if (thing->lifetime > 0)
        {
            thing->age += g_ClockTimer;

            if (thing->age >= 0 && thing->age >= thing->lifetime)
            {
                thing->lifetime = 0;
            } 
        }
    }
}


/**
 * Address: 7F0A3F04
 */
void bullet_spark_render(s_bullet_spark *thing, Gfx **gdlarg, s32 zbufferMode)
{
    Vtx vtx;
    Mtxf *mtx;
    Gfx *gdl;
    Vtx *vertices;
    f32 z;
    f32 y;
    f32 x;
    f32 s0[3];
    f32 s1[3];
    f32 s2[3];
    f32 s3[3];
    s32 frame;
    s32 room;
    struct coord3d *roompos;
    
    if (thing->lifetime <= 0)
    {
        return;
    }
    
    if (thing->age < 0)
    {
        return;
    }
    
    if (!camIsPosInScreen((coord3d *) (&thing->unk10), *((f32 *) (&thing->unk24))))
    {
        return;
    }

    vtx = D_80040980;
    mtx = currentPlayerGetViewToWorldMtxf();
    gdl = *gdlarg;
    vertices = dynAllocateVertices(4);
    room = thing->unk06;
    roompos = getRoomPositionByIndex(room);
    vtx.v.cn[0] = thing->unk28;
    vtx.v.cn[1] = thing->unk29;
    vtx.v.cn[2] = thing->unk2A;
    vtx.v.cn[3] = thing->unk2B;
    frame = (s32) (((f32) thing->age) * (*(&thing->unk08)));
    
    x = *((f32 *) (&thing->unk10));
    y = *((f32 *) (&thing->unk14));
    z = *((f32 *) (&thing->unk18));
    
    s0[0] = mtx->m[0][0] * thing->unk1c;
    s0[1] = mtx->m[0][1] * thing->unk1c;
    s0[2] = mtx->m[0][2] * thing->unk1c;
    s1[0] = mtx->m[0][0] * thing->unk20;
    s1[1] = mtx->m[0][1] * thing->unk20;
    s1[2] = mtx->m[0][2] * thing->unk20;
    s2[0] = mtx->m[1][0] * thing->unk1c;
    s2[1] = mtx->m[1][1] * thing->unk1c;
    s2[2] = mtx->m[1][2] * thing->unk1c;
    s3[0] = mtx->m[1][0] * thing->unk20;
    s3[1] = mtx->m[1][1] * thing->unk20;
    s3[2] = mtx->m[1][2] * thing->unk20;

    vertices[0] = vtx;
    vertices[1] = vtx;
    vertices[2] = vtx;
    vertices[3] = vtx;
    vertices[0].v.ob[0] = (((x - s0[0]) - s3[0]) * get_room_data_float1()) - roompos->f[0];
    vertices[0].v.ob[1] = (((y - s0[1]) - s3[1]) * get_room_data_float1()) - roompos->f[1];
    vertices[0].v.ob[2] = (((z - s0[2]) - s3[2]) * get_room_data_float1()) - roompos->f[2];

    // Matching hack.
    if ((roompos->f && roompos->f));

    vertices[0].v.tc[0] = ((sImageTableEntry *)thing->unk0C + frame)->width << 5;
    vertices[0].v.tc[1] = 0;
    vertices[1].v.ob[0] = (((x + s1[0]) - s2[0]) * get_room_data_float1()) - roompos->f[0];
    vertices[1].v.ob[1] = (((y + s1[1]) - s2[1]) * get_room_data_float1()) - roompos->f[1];
    vertices[1].v.ob[2] = (((z + s1[2]) - s2[2]) * get_room_data_float1()) - roompos->f[2];
    vertices[1].v.tc[0] = 0;
    vertices[1].v.tc[1] = 0;
    vertices[2].v.ob[0] = (((x + s0[0]) + s3[0]) * get_room_data_float1()) - roompos->f[0];
    vertices[2].v.ob[1] = (((y + s0[1]) + s3[1]) * get_room_data_float1()) - roompos->f[1];
    vertices[2].v.ob[2] = (((z + s0[2]) + s3[2]) * get_room_data_float1()) - roompos->f[2];
    vertices[2].v.tc[0] = 0;
    vertices[2].v.tc[1] = ((sImageTableEntry *)thing->unk0C + frame)->height << 5;
    vertices[3].v.ob[0] = (((x - s1[0]) + s2[0]) * get_room_data_float1()) - roompos->f[0];
    vertices[3].v.ob[1] = (((y - s1[1]) + s2[1]) * get_room_data_float1()) - roompos->f[1];
    vertices[3].v.ob[2] = (((z - s1[2]) + s2[2]) * get_room_data_float1()) - roompos->f[2];
    vertices[3].v.tc[0] = ((sImageTableEntry *)thing->unk0C + frame)->width << 5;
    vertices[3].v.tc[1] = ((sImageTableEntry *)thing->unk0C + frame)->height << 5;
    
    gSPSetGeometryMode(gdl++, G_CULL_BACK);
    gSPMatrix(gdl++, osVirtualToPhysical((void *) get_BONDdata_field_10E0()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gdl = applyRoomMatrixToDisplayList(gdl, room);
    texSelect(&gdl, (sImageTableEntry *)thing->unk0C + frame, 4, zbufferMode, 2);
    gSPVertex(gdl++, osVirtualToPhysical(vertices), 4, 0);
    gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
    gSPMatrix(gdl++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    *gdlarg = gdl;
}


/**
 * Address: 7F0A4528
 */
void bullet_sparks_render(Gfx **gdl, s32 zbufferMode)
{

    s_bullet_spark *thing = &g_BulletSparkArray[0]; \
    s_bullet_spark *end = g_BulletSparkArray + BULLET_SPARKS_MAX;

    for (; (thing < end); thing++)
    {
        bullet_spark_render(thing, gdl, zbufferMode);
    }
}


/**
 * Address: 7F0A4594
 */
f32 bullet_spark_get_depth(s_bullet_spark* spark)
{
    coord3d tempVec;

    tempVec.x = spark->unk10;
    tempVec.y = spark->unk14;
    tempVec.z = spark->unk18;

    mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &tempVec);

    return -tempVec.z;
}


/**
 * Address: 7F0A45D8
 */
#ifndef VERSION_EU
void bullet_moving_sparks_reset(void)
{
    s_moving_bullet_spark *ptr;

    ptr = g_MovingBulletSparkArray;

    for (ptr = &g_MovingBulletSparkArray[0]; ptr < &g_MovingBulletSparkArray[BULLET_MOVING_SPARKS_MAX]; ptr++)
    {
        ptr->unk00.lifetime = 0;
    }
}
#else
void bullet_moving_sparks_reset(void)
{
    bullet_sparks_reset();
}
#endif


/**
 * Address: 7F0A4600
 */
#ifndef VERSION_EU
s_moving_bullet_spark *bullet_moving_spark_create(coord3d *arg0, coord3d *velocity, s32 arg2, f32 arg3, f32 arg4, s16 arg5)
{
    s_moving_bullet_spark *ptr;

    for (ptr = &g_MovingBulletSparkArray[0]; ptr < &g_MovingBulletSparkArray[BULLET_MOVING_SPARKS_MAX]; ptr++)
    {
        if (ptr->unk00.lifetime == 0)
        {
            bullet_sparks_init(&ptr->unk00, arg0, arg2, arg3, arg5);

            ptr->velocity.x = velocity->x;
            ptr->velocity.y = velocity->y;
            ptr->velocity.z = velocity->z;
            ptr->unk38 = arg4;

            return ptr;
        }
    }

    return NULL;
}
#else
void bullet_moving_spark_create(void)
{
    bullet_sparks_update();
}
#endif


/**
 * Address: 7F0A46A0
 */
#ifndef VERSION_EU
void bullet_moving_sparks_update(void)
{
    s_moving_bullet_spark *ptr;
    s_moving_bullet_spark *end;

    ptr = &g_MovingBulletSparkArray[0]; end = &g_MovingBulletSparkArray[BULLET_MOVING_SPARKS_MAX];

    while (ptr < end)
    {
        if (ptr->unk00.lifetime > 0)
        {
            ptr->unk00.age += g_ClockTimer;
            if (ptr->unk00.age >= 0)
            {
                if (ptr->unk00.lifetime > ptr->unk00.age)
                {
                    sub_GAME_7F057D88(&ptr->unk00.unk10, &ptr->velocity, g_GlobalTimerDelta);
                    if (ptr->unk00.unk14 < ptr->unk38)
                    {
                        ptr->unk00.lifetime = 0;
                    }
                }
                else
                {
                    ptr->unk00.lifetime = 0;
                }
            }
        }
        ptr++;
    }
}
#else
void bullet_moving_sparks_update(Gfx **arg0, s32 zbufferMode)
{
    bullet_sparks_render(arg0, zbufferMode);
}
#endif


/**
 * Address: 7F0A4768
 */
#ifndef VERSION_EU

void bullet_moving_sparks_render_all(Gfx **arg0, s32 zbufferMode)
{
    s32 max_index;
    s_moving_bullet_spark *ptr;

    max_index = BULLET_MOVING_SPARKS_MAX;

    for (ptr = &g_MovingBulletSparkArray[0]; ptr < (&g_MovingBulletSparkArray[max_index]); ptr++)
    {
        bullet_spark_render(&ptr->unk00, arg0, zbufferMode);
    }

}


/**
 * Address: 7F0A47D4
 */
void bullet_sparks_reset_all(void)
{
    bullet_sparks_reset();
    bullet_moving_sparks_reset();
}


void bullet_sparks_update_all(void)
{
    bullet_sparks_update();

    // responsible for updating bullet sparks and dust clouds that spawn when shooting at other players
    // these are 2D and always facing the camera
    bullet_moving_sparks_update();
}


/**
 * Address: 7F0A4824
 */
void bullet_sparks_render_all(Gfx **arg0, s32 zbufferMode)
{
    bullet_sparks_render(arg0, zbufferMode);
    bullet_moving_sparks_render_all(arg0, zbufferMode);
}


#endif
