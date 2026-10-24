#include <ultra64.h>
#include "game/unk_092E50.h"
#include "gbi_extension.h"
#include "lightfixture.h"
#include "lv.h"
#include "image.h"
#include "tex.h"

s32 g_TexLutMode;
s32 dword_CODE_bss_8008D144; // unused/padding
s_bss_8008D148 g_TexTileStates[8]; //[0x160]; //8 entries of 0x2c ea
s_bss_8008D2A8 g_TexTileSizes[8]; //[0xA0]; //8 entries of 0x14 ea
s32 dword_CODE_bss_8008D348; // unused/padding

//D:8004E760
u16 default_hit_sfx[] = {HIT_BULLET_STONE1_SFX, HIT_BULLET_STONE2_SFX};
//D:8004E764
u8 default_impact_types[] = {0x7, 0x0, 0x0, 0x0};
//D:8004E768
struct image_sound isnd_default = {default_hit_sfx, default_impact_types, 0x0, 0x1};

//D:8004E774
u16 stone_hit_sfx[] = {HIT_BULLET_STONE1_SFX, HIT_BULLET_STONE2_SFX};
//D:8004E778
u8 stone_impact_types[] = {0x1, 0x0, 0x0, 0x0};
//D:8004E77C
struct image_sound isnd_stone = {stone_hit_sfx, stone_impact_types, 0x2, 0x1};

//D:8004E788
u16 wood_hit_sfx[] = {HIT_BULLET_WOOD_SFX, HIT_BULLET_WOOD2_SFX};
//D:8004E78C
u8 wood_impact_types[] = {0x1, 0x0, 0x0, 0x0};
//D:8004E790
struct image_sound isnd_wood = {wood_hit_sfx, wood_impact_types, 0x2, 0x1};

//D:8004E79C
u16 metal_hit_sfx[] = {HIT_BULLET_METAL_A_SFX, HIT_BULLET_METAL_A4_SFX, HIT_BULLET_METAL_A3_SFX, 0x00};
//D:8004E7A4
u8 metal_impact_types[] = {0x7, 0x0, 0x0, 0x0};
//D:8004E7A8
struct image_sound isnd_metal = {metal_hit_sfx, metal_impact_types, 0x3, 0x1};

//D:8004E7B4
u16 glass_hit_sfx[] = {HIT_BULLET_GLASS_SFX, 0x00};
//D:8004E7B8
u8 glass_impact_types[] = {0x4, 0x5, 0x6, 0x0};
//D:8004E7BC
struct image_sound isnd_glass = {glass_hit_sfx, glass_impact_types, 0x1, 0x3};

//D:8004E7C8
u16 water_hit_sfx[] = {HIT_BULLET_WATER_SFX, 0x00};
//D:8004E7CC
u8 water_impact_types[] = {0, 0, 0, 0};
//D:8004E7D0
struct image_sound isnd_water = {water_hit_sfx, water_impact_types, 0x1, 0x0};

//D:8004E7DC
u16 snow_hit_sfx[] = {HIT_BULLET_SNOW_SFX, 0x00};
//D:8004E7E0
u8 snow_impact_types[] = {0x1, 0x0, 0x0, 0x0};
//D:8004E7E4
struct image_sound isnd_snow = {snow_hit_sfx, snow_impact_types, 0x1, 0x1};

//D:8004E7F0
u16 dirt_hit_sfx[] = {HIT_BULLET_DIRT1_SFX, HIT_BULLET_DIRT2_SFX};
//D:8004E7F4
u8 dirt_impact_types[] = {0x2, 0x0, 0x0, 0x0};
//D:8004E7F8
struct image_sound isnd_dirt = {dirt_hit_sfx, dirt_impact_types, 0x2, 0x1};

//D:8004E804
u16 mud_hit_sfx[] = {HIT_BULLET_MUD1_SFX, HIT_BULLET_MUD2_SFX, HIT_BULLET_MUD3_SFX, 0x00};
//D:8004E80C
u8 mud_impact_types[] = {0x2, 0x0, 0x0, 0x0};
//D:8004E810
struct image_sound isnd_mud = {mud_hit_sfx, mud_impact_types, 0x3, 0x1};

//D:8004E81C
u16 tile_hit_sfx[] = {HIT_BULLET_TILE_SFX, 0x00};
//D:8004E820
u8 tile_impact_types[] = {0x1, 0x0, 0x0, 0x0};
//D:8004E824
struct image_sound isnd_tile = {tile_hit_sfx, tile_impact_types, 0x1, 0x1};

//D:8004E830
u16 metal_object_hit_sfx[] = {HIT_METAL_OBJECT1_SFX, HIT_METAL_OBJECT2_SFX};
//D:8004E834
u8 metal_object_impact_types[] = {0x1, 0x7, 0x0, 0x0};
//D:8004E838
struct image_sound isnd_metalobj = {metal_object_hit_sfx, metal_object_impact_types, 0x2, 0x2};

//D:8004E844
u16 character_hit_sfx[] = {HIT_BULLET_FLESH_SFX, 0x00};
//D:8004E848
u8 character_impact_types[] = {0x2, 0x0, 0x0, 0x0};
//D:8004E84C
struct image_sound isnd_chr = {character_hit_sfx, character_impact_types, 0x1, 0x1};

//D:8004E858
u16 translucent_glass_hit_sfx[] = {HIT_BULLET_GLASS_SFX, 0x00};
//D:8004E85C
u8 translucent_glass_impact_types[] = {0x11, 0x12, 0x13, 0x0};
//D:8004E860
struct image_sound isnd_glass_xlu = {translucent_glass_hit_sfx, translucent_glass_impact_types, 0x1, 0x3};

//D:8004E86C
struct image_sound *g_HitTypeSounds[] = { 
    &isnd_default,
    &isnd_stone,
    &isnd_wood,
    &isnd_metal,
    &isnd_glass,
    &isnd_water,
    &isnd_snow,
    &isnd_dirt,
    &isnd_mud,
    &isnd_tile,
    &isnd_metalobj,
    &isnd_chr,
    &isnd_glass_xlu
};

char * image_sound_names[] = {
    "HIT_DEFAULT",
    "HIT_STONE",
    "HIT_WOOD",
    "HIT_METAL",
    "HIT_GLASS",
    "HIT_WATER",
    "HIT_SNOW",
    "HIT_DIRT",
    "HIT_MUD",
    "HIT_TILE",
    "HIT_METALOBJ",
    "HIT_CHR",
    "HIT_GLASS_XLU"
};


void nullsub_47(void) {
    return;
}


void sub_GAME_7F0CC4C8(void)
{
    int i;

    for (i=0; i<8; i++)
    {
        g_TexTileStates[i].inuse = 0;
        g_TexTileSizes[i].inuse = 0;
    }
    g_TexLutMode = -1;
}


s32 unused_copy_byte_array(u8* src, s32 count, u8* dst)
{
    u8* src_itr;
    u8* dst_itr;

    src_itr = src;
    dst_itr = dst;

    while (count > 0)
    {
        *dst_itr = *src_itr;
        dst_itr++;
        src_itr++;
        count--;
    }

    return dst_itr - dst;
}


u32 texSetLutMode(s32 arg0) {
    if (arg0 == g_TexLutMode) {
        return 0;
    }
    g_TexLutMode = arg0;
    return 1;
}


//s32 texTrySetTileState(s32 index, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 argA)
s32 texTrySetTileState(s32 tile, s32 format, s32 depth, s32 line, s32 tmem, s32 smode, s32 tmode, s32 masks, s32 maskt, s32 shifts, s32 shiftt)
{
    s32 retval;

    retval = 0;
    if ((g_TexTileStates[tile].inuse == 0) || (format != g_TexTileStates[tile].unk_4) || 
     (depth != g_TexTileStates[tile].unk_8) || (line != g_TexTileStates[tile].unk_C) || 
     (tmem != g_TexTileStates[tile].unk_10) || (smode != g_TexTileStates[tile].unk_14) || 
     (tmode != g_TexTileStates[tile].unk_18) || (masks != g_TexTileStates[tile].unk_1C) || 
     (maskt != g_TexTileStates[tile].unk_20) || (shifts != g_TexTileStates[tile].unk_24) || 
     (shiftt != g_TexTileStates[tile].unk_28))
    {
        retval = 1;
        g_TexTileStates[tile].inuse = 1;
        g_TexTileStates[tile].unk_4 = format;
        g_TexTileStates[tile].unk_8 = depth;
        g_TexTileStates[tile].unk_C = line;
        g_TexTileStates[tile].unk_10 = tmem;
        g_TexTileStates[tile].unk_14 = smode;
        g_TexTileStates[tile].unk_18 = tmode;
        g_TexTileStates[tile].unk_1C = masks;
        g_TexTileStates[tile].unk_20 = maskt;
        g_TexTileStates[tile].unk_24 = shifts;
        g_TexTileStates[tile].unk_28 = shiftt;

    }
    return retval;
}


s32 texTrySetTileSize(s32 tile, s32 uls, s32 ult, s32 lrs, s32 lrt)
{
    s32 retval;

    retval = 0;
    if ((((g_TexTileSizes[tile].inuse == 0) || (uls != g_TexTileSizes[tile].unk_4)) || (ult != g_TexTileSizes[tile].unk_8)) ||
       ((lrs != g_TexTileSizes[tile].unk_C || (lrt != g_TexTileSizes[tile].unk_10))))
    {
        retval = 1;
        g_TexTileSizes[tile].inuse = 1;
        g_TexTileSizes[tile].unk_4 = uls;
        g_TexTileSizes[tile].unk_8 = ult;
        g_TexTileSizes[tile].unk_C = lrs;
        g_TexTileSizes[tile].unk_10 = lrt;
    }
    return retval;
}


s32 texGetWidthAtLod(struct tex *tex, s32 lod)
{
    s32 i;
    s32 width = tex->width;

    if (lod == 0)
    {
        return width;
    }

    if (tex->hasExplicitLods)
    {
        for (i = 0; i < g_TexCacheCount; i++)
        {
            if (tex->texturenum == g_TexCacheItems[i].texturenum)
            {
                return g_TexCacheItems[i].widths[lod - 1];
            }
        }

#ifndef TARGET_N64
        /* This miss is routine after a mission change: the per-stage cache is
         * reset but persistent model-pool records are reused, and returning 1
         * here collapsed every lod to 1x1 (tiled texture corruption). Texture
         * dimensions are global per image, so the persistent mirror in image.c
         * is always a valid answer. */
        {
            extern s32 portTexLodLookup(s32 texturenum, s32 lod, s32 wantheight);
            s32 v = portTexLodLookup(tex->texturenum, lod, 0);

            if (v > 0)
            {
                return v;
            }
        }
#endif

        return 1;
    }

    for (i = 0; i < lod; i++)
    {
        width = (width + 1) >> 1;
    }

    return width;
}


s32 texGetHeightAtLod(struct tex *tex, s32 lod)
{
    s32 i;
    s32 height = tex->height;

    if (lod == 0)
    {
        return height;
    }

    if (tex->hasExplicitLods)
    {
        for (i = 0; i < g_TexCacheCount; i++)
        {
            if (tex->texturenum == g_TexCacheItems[i].texturenum)
            {
                return g_TexCacheItems[i].heights[lod - 1];
            }
        }

#ifndef TARGET_N64
        /* This miss is routine after a mission change: the per-stage cache is
         * reset but persistent model-pool records are reused, and returning 1
         * here collapsed every lod to 1x1 (tiled texture corruption). Texture
         * dimensions are global per image, so the persistent mirror in image.c
         * is always a valid answer. */
        {
            extern s32 portTexLodLookup(s32 texturenum, s32 lod, s32 wantheight);
            s32 v = portTexLodLookup(tex->texturenum, lod, 1);

            if (v > 0)
            {
                return v;
            }
        }
#endif

        return 1;
    }

    for (i = 0; i < lod; i++)
    {
        height = (height + 1) >> 1;
    }

    return height;
}


s32 texGetLineSizeInBytes(struct tex *tex, s32 lod)
{
    s32 depth = tex->depth;
    s32 width = texGetWidthAtLod(tex, lod);

    if (depth == G_IM_SIZ_32b)
    {
        return (width + 3) / 4;
    }

    if (depth == G_IM_SIZ_16b)
    {
        return (width + 3) / 4;
    }

    if (depth == G_IM_SIZ_8b)
    {
        return (width + 7) / 8;
    }

    return (width + 15) / 16;
}


s32 texGetSizeInBytes(struct tex *tex, s32 lod)
{
    return texGetHeightAtLod(tex, lod) * texGetLineSizeInBytes(tex, lod);
}


void texGetDepthAndSize(struct tex *tex, s32 *deptharg, s32 *lenarg)
{
    s32 depth = tex->depth;
    s32 maxlod = tex->maxlod ? tex->maxlod : 1;
    s32 lod;

    *lenarg = 0;

    if (depth == G_IM_SIZ_32b)
    {
        *deptharg = G_IM_SIZ_32b;
    }
    else if (depth == G_IM_SIZ_16b)
    {
        *deptharg = G_IM_SIZ_16b;
    }
    else if (depth == G_IM_SIZ_8b)
    {
        *deptharg = G_IM_SIZ_16b;
    }
    else
    {
        *deptharg = G_IM_SIZ_16b;
    }

    for (lod = 0; lod < maxlod; lod++) {
        *lenarg += texGetSizeInBytes(tex, lod) * 4;
    }
}


s32 texDimensionToMask(s32 arg0) {
    s32 i = 0;

    arg0--;

    while (arg0 > 0 && i < 8) {
        arg0 >>= 1;
        i++;
    }

    return i;
}





u32 texModeToGbiMode(s32 arg0) {
    if (arg0 == 1) {
        return G_TX_CLAMP;
    }
    if (arg0 == 2) {
        return G_TX_MIRROR;
    }
    return G_TX_WRAP;
}


s32 sub_GAME_7F0CCAFC(f32 arg0)
{
    return (s32)arg0 < (f32)arg0 ? (s32)arg0+1 : (s32)arg0;
}


s32 sub_GAME_7F0CCB38(s32 *arg0)
{
    s32 temp_t7;
    s32 temp_v0;

    temp_v0 = texGetLineSizeInBytes(arg0, 0U);
    temp_t7 = arg0[2] & 3;
    if (temp_t7 == 3)
    {
        return sub_GAME_7F0CCAFC((2.0f / (f32) (temp_v0 * 4)) * 2048.0f);
    }
    if (temp_t7 == 2)
    {
        return sub_GAME_7F0CCAFC((4.0f / (f32) (temp_v0 * 4)) * 2048.0f);
    }
    if (temp_t7 == 1)
    {
        return sub_GAME_7F0CCAFC((8.0f / (f32) (temp_v0 * 8)) * 2048.0f);
    }
    return sub_GAME_7F0CCAFC((16.0f / (f32) (temp_v0 * 0x10)) * 2048.0f);
}


Gfx *texWriteTileFromDefinition(Gfx *gdl, struct tex *tex, s32 offset, s32 shifts, s32 shiftt, s32 arg5)
{
    struct image_entry *s0 = &g_Textures[tex->texturenum];
    s32 sp88;
    s32 sp84;
    s32 line;
    u32 stack[4];
    s32 uls;
    s32 ult;
    s32 lrs;
    s32 lrt;

    sp88 = texDimensionToMask(tex->width);
    sp84 = texDimensionToMask(tex->height);

    line = texGetLineSizeInBytes(tex, 0);

    gDPSetPrimColorViaWord(gdl++, arg5, 0, 0xffffffff);

    if (texSetLutMode(tex->lutmodeindex << G_MDSFT_TEXTLUT))
    {
        gDPSetTextureLUT(gdl++, tex->lutmodeindex << G_MDSFT_TEXTLUT);
    }

    if (texTrySetTileState(0, tex->gbiformat, tex->depth, line, s0->flag3 + line * s0->flag4, 0, 0, sp88 - s0->flag5, sp84 - s0->flag6, shifts, shiftt))
    {
        gDPSetTile(gdl++, tex->gbiformat, tex->depth, line, s0->flag3 + line * s0->flag4, 0, 0,
                texModeToGbiMode(0), sp84 - s0->flag6, shiftt,
                texModeToGbiMode(0), sp88 - s0->flag5, shifts);
    }

    uls = (offset == 2 && !tex->hasExplicitLods ? 2 : 0) + 0;
    ult = (offset == 2 && !tex->hasExplicitLods ? 2 : 0) + 0;
    lrs = (offset == 2 && !tex->hasExplicitLods ? 2 : 0) + ((tex->width - 1) << 2);
    lrt = (offset == 2 && !tex->hasExplicitLods ? 2 : 0) + ((tex->height - 1) << 2);

    if (texTrySetTileSize(0, uls, ult, lrs, lrt))
    {
        gDPSetTileSize(gdl++, 0, uls, ult, lrs, lrt);
    }

    return gdl;
}

Gfx* texWriteTextureCmd(Gfx* arg0, Gfx* arg1, struct tex* tex, s32 arg3)
{
    s32 lod = tex->maxlod ? tex->maxlod - 1 : 0;

    if (arg3)
    {
        if (arg1 != NULL)
        {
            u32 v0 = (arg1->words.w0 & ~0x3800) | (lod << 11);

            if (v0 != arg1->words.w0)
            {
                arg0->words.w0 = v0;
                arg0->words.w1 = arg1->words.w1;
                arg0++;
            }
        }
        else
        {
            gSPTexture(arg0++, 0xffff, 0xffff, lod, G_TX_RENDERTILE, G_ON);
        }
    }
    else
    {
        arg1->words.w0 &= ~0x3800;
        arg1->words.w0 |= lod << 11;
    }

    return arg0;
}


Gfx *texWriteLoadToTmemAddr(Gfx *gdl, struct tex *tex, s32 tmemoffset)
{
	s32 depth;
	s32 len;

	texGetDepthAndSize(tex, &depth, &len);

	if (tex->lutmodeindex == 0)
	{
		gDPSetTextureImage(gdl++, tex->gbiformat, depth, 1, tex->data);

		if (texTrySetTileState(7, tex->gbiformat, depth, 0, tmemoffset, 0, 0, 0, 0, 0, 0))
		{
			gDPSetTile(gdl++, tex->gbiformat, depth, 0, tmemoffset, 7, 0, 0, 0, 0, 0, 0, 0);
		}

		gDPLoadSync(gdl++);
		gDPLoadBlock(gdl++, 7, 0, 0, len - 1, 0);
		gDPPipeSync(gdl++);
	}
	else
	{
		u32 uls;
		u32 ult;

		gDPSetTextureImage(gdl++, tex->gbiformat, depth, 1, tex->data);

		if (texTrySetTileState(7, tex->gbiformat, depth, 0, tmemoffset, 0, 0, 0, 0, 0, 0))
		{
			gDPSetTile(gdl++, tex->gbiformat, depth, 0, tmemoffset, 7, 0, 0, 0, 0, 0, 0, 0);
		}

		gDPLoadSync(gdl++);
		gDPLoadBlock(gdl++, 7, 0, 0, len - 1, 0);
		gDPPipeSync(gdl++);

		uls = len;

		if ((u32)(0x3ff - tex->unk0a) < uls)
		{
			ult = 0x3ff - tex->unk0a;
		}
		else
		{
			ult = 0;
		}

		uls -= ult;

		if (texTrySetTileState(7, 0, 0, 1, 0x100, 0, 0, 0, 0, 0, 0))
		{
			gDPSetTile(gdl++, 0, 0, 1, 0x100, 7, 0, 0, 0, 0, 0, 0, 0);
		}
        
		gDPLoadSync(gdl++);
		gDPLoadTLUT07(gdl++, uls, ult, tex->unk0a + uls, ult);
		gDPPipeSync(gdl++);
	}

	return gdl;
}


Gfx *texWriteTileLods(Gfx *gdl, struct tex *tex, s32 smode, s32 tmode, s32 offset, s32 basetile, s32 tmemaddr)
{
    s32 tmem;
    s32 masks;
    s32 tile;
    s32 maskt;
    s32 line;
    s32 ult;
    s32 lrs;
    s32 lrt;
    s32 lod;
    s32 uls;
    bool hasExplicitLods;
    s32 end;
    s32 pad;
    s32 size;
    s32 pad2;

    tmem = tmemaddr;
    end = tex->maxlod;
    end = end + basetile;

    for (tile = basetile; tile < (end ^ 0); tile++)
    {
        pad2 = basetile;
        lod = tile - pad2;
        masks = texDimensionToMask(texGetWidthAtLod(tex, lod));
        maskt = texDimensionToMask(texGetHeightAtLod(tex, lod));
        line = texGetLineSizeInBytes(tex, lod);
        size = texGetSizeInBytes(tex, lod);
        hasExplicitLods = tex->hasExplicitLods;

        if (texSetLutMode(tex->lutmodeindex << 14))
        {
            gDPSetTextureLUT(gdl++, tex->lutmodeindex << G_MDSFT_TEXTLUT);
        }

        if (texTrySetTileState(tile, tex->gbiformat, tex->depth, line, tmem, smode, tmode, masks, maskt, tile - pad2, tile - pad2))
        {
            gDPSetTile(gdl++, tex->gbiformat, tex->depth, line, tmem, tile, 0, texModeToGbiMode(tmode), maskt, lod, texModeToGbiMode(smode), masks, tile - basetile);
        }

        uls = (((offset == 2) && (hasExplicitLods == 0)) ? (2) : (0)) + 0;
        ult = (((offset == 2) && (hasExplicitLods == 0)) ? (2) : (0)) + 0;
        lrs = ((texGetWidthAtLod(tex, lod) - 1) << 2) + (((offset == 2) && (hasExplicitLods == 0)) ? (2) : (0));
        lrt = ((texGetHeightAtLod(tex, lod) - 1) << 2) + (((offset == 2) && (hasExplicitLods == 0)) ? (2) : (0));

        if (texTrySetTileSize(tile, uls, ult, lrs, lrt))
        {
            gDPSetTileSize(gdl++, tile, uls, ult, lrs, lrt);
        }

        tmem += size;
    }

    lod = tile - pad2;
    
    return gdl;
}


Gfx *texWriteLoadToTmemZero(Gfx *gdl, struct tex *tex)
{
	s32 depth;
	s32 len;
	s32 dxt;

	dxt = sub_GAME_7F0CCB38((s32 *)tex);
	texGetDepthAndSize(tex, &depth, &len);

	if (tex->lutmodeindex == 0)
	{
		gDPSetTextureImage(gdl++, tex->gbiformat, depth, 1, tex->data);
		if (texTrySetTileState(7, tex->gbiformat, depth, 0, 0, 0, 0, 0, 0, 0, 0))
		{
			gDPSetTile(gdl++, tex->gbiformat, depth, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0);
		}
		gDPLoadSync(gdl++);
		gDPLoadBlock(gdl++, 7, 0, 0, len - 1, dxt);
		gDPPipeSync(gdl++);
	}
	else
	{
		u32 uls;
		u32 ult;

		gDPSetTextureImage(gdl++, tex->gbiformat, depth, 1, tex->data);
		if (texTrySetTileState(7, tex->gbiformat, depth, 0, 0, 0, 0, 0, 0, 0, 0))
		{
			gDPSetTile(gdl++, tex->gbiformat, depth, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0);
		}
		gDPLoadSync(gdl++);
		gDPLoadBlock(gdl++, 7, 0, 0, len - 1, dxt);
		gDPPipeSync(gdl++);

		uls = len;
		if ((u32)(0x3ff - tex->unk0a) < uls)
		{
			ult = 0x3ff - tex->unk0a;
		}
		else
		{
			ult = 0;
		}
		uls -= ult;
		if (texTrySetTileState(7, 0, 0, 1, 0x100, 0, 0, 0, 0, 0, 0))
		{
			gDPSetTile(gdl++, 0, 0, 1, 0x100, 7, 0, 0, 0, 0, 0, 0, 0);
		}
		gDPLoadSync(gdl++);
		gDPLoadTLUT07(gdl++, uls, ult, tex->unk0a + uls, ult);
		gDPPipeSync(gdl++);
	}

	return gdl;
}


Gfx *texWriteTile(Gfx *gdl, struct tex *tex, s32 arg2, s32 arg3, s32 arg4, s32 tile)
{
    s32 masks;
    s32 maskt;
    s32 line;
    s32 uls;
    s32 ult;
    s32 lrs;
    s32 lrt;
    s32 sp50;

    masks = texDimensionToMask(tex->width);
    maskt = texDimensionToMask(tex->height);
    line = texGetLineSizeInBytes(tex, 0);
    sp50 = tex->hasExplicitLods;

    if (texSetLutMode(tex->lutmodeindex << G_MDSFT_TEXTLUT))
    {
        gDPSetTextureLUT(gdl++, tex->lutmodeindex << G_MDSFT_TEXTLUT);
    }

    if (texTrySetTileState(tile, tex->gbiformat, tex->depth, line, 0, arg2, arg3, masks, maskt, 0, 0))
    {
        gDPSetTile(gdl++, tex->gbiformat, tex->depth, line, 0x0000, tile, 0,
                texModeToGbiMode(arg3), maskt, G_TX_NOLOD,
                texModeToGbiMode(arg2), masks, G_TX_NOLOD);
    }

    uls = (arg4 == 2 && sp50 == 0 ? 2 : 0) + 0;
    ult = (arg4 == 2 && sp50 == 0 ? 2 : 0) + 0;
    lrs = (arg4 == 2 && sp50 == 0 ? 2 : 0) + ((tex->width - 1) << 2);
    lrt = (arg4 == 2 && sp50 == 0 ? 2 : 0) + ((tex->height - 1) << 2);

    if (texTrySetTileSize(tile, uls, ult, lrs, lrt))
    {
        gDPSetTileSize(gdl++, tile, uls, ult, lrs, lrt);
    }

    return gdl;
}


Gfx *texHandleType2(Gfx *gdl, struct tex *tex, s32 smode, s32 tmode, s32 offset)
{
    gdl = texWriteLoadToTmemAddr(gdl,tex,0);
    gdl = texWriteTileLods(gdl,tex,smode,tmode,offset,0,0);
    if (tex->maxlod == 1) {
        gdl = texWriteTileLods(gdl,tex,smode,tmode,offset,1,0);
    }
    return gdl;
}


Gfx * texHandleType1(Gfx *gdl,struct tex *tex1,s32 smode,s32 tmode,s32 offset,struct tex *tex2,u32 shifts,u32 shiftt,u32 min)
{
    u32 size;

    size = texGetSizeInBytes(tex2,0);
    gdl = texWriteLoadToTmemZero(gdl,tex2);
    gDPTileSync(gdl++);
    gdl = texWriteLoadToTmemAddr(gdl,tex1,size);
    gdl = texWriteTileFromDefinition(gdl,tex2,offset,shifts,shiftt,min);
    gdl = texWriteTileLods(gdl,tex1,smode,tmode,offset,1,size);
    if (tex1->maxlod == 1) {
        gdl = texWriteTileLods(gdl,tex1,smode,tmode,offset,2,size);
    }
    return gdl;
}


Gfx * texHandleType0(Gfx *gdl,struct tex *tex,s32 smode,s32 tmode,s32 offset,u32 shifts,u32 shiftt,u32 min)
{
    gdl = texWriteLoadToTmemAddr(gdl,tex,0);
    gdl = texWriteTileFromDefinition(gdl,tex,offset,shifts,shiftt,min);
    gdl = texWriteTileLods(gdl,tex,smode,tmode,offset,1,0);
    if (tex->maxlod == 1) {
        gdl = texWriteTileLods(gdl,tex,smode,tmode,offset,2,0);
    }
    return gdl;
}


Gfx * texHandleType4(Gfx *DL,u32 *arg1,s32 arg2,s32 arg3,s32 arg4)
{
    DL = texWriteLoadToTmemZero(DL,arg1);
    DL = texWriteTile(DL,arg1,arg2,arg3,arg4,0);
    return DL;
}


Gfx * texHandleType3(Gfx *DL,u32 *arg1,s32 arg2,s32 arg3,s32 arg4)
{
    DL = texWriteLoadToTmemZero(DL,arg1);
    DL = texWriteTile(DL,arg1,arg2,arg3,arg4,0);
    DL = texWriteTile(DL,arg1,arg2,arg3,arg4,1);
    return DL;
}





const char PADDINGHACKING[4] = "";

s32 texLoadFromGdl(Gfx *src, s32 srcsize, Gfx *dst, void *texpool)
{
    s32         texnum;
    struct tex *tex;
    Gfx        *saved = NULL;
    s32         count;
    s32         valid = FALSE;
    s32         texnum2;
    s32         syncEmitted = FALSE;
    s32         smode;
    s32         tmode;
    s32         offset;
    s32         shifts;
    s32         shiftt;
    s32         min;
    s32         pad;
    s32         writeTexFlag = TRUE;
    s32         lightPending = FALSE;
    Gfx        *in           = src;
    Gfx        *out          = dst;
#ifndef TARGET_N64
    /* debug (GE_DUMP_BOND): per-conversion G_SETTEX accounting */
    s32 dbgSeen = 0, dbgFound = 0, dbgMissed = 0, dbgFirstMiss = -1;
#endif

    count = srcsize / sizeof(Gfx);

    sub_GAME_7F0CC4C8();

    if (texpool == NULL)
    {
        texpool = &ptr_texture_alloc_start;
    }

    while (count > 0)
    {
#ifdef TARGET_N64
        switch (*(u8 *)in)
#else
        /* PC port: byte 0 of a Gfx is only the w0 opcode on big-endian;
         * extract it from the word (same rule as the bg.c/lightfixture
         * walkers). */
        switch ((u8)(in->words.w0 >> 24))
#endif
        {
            case G_NOOP:
                if (!syncEmitted)
                {
                    gDPPipeSync(out++);
                    syncEmitted = TRUE;
                }

                if (lightPending)
                {
                    lightFixtureEntryEnd(out);
                    lightPending = FALSE;
                }

                texnum = in->words.w1 & 0xfff;

                if ((texnum == hittype) && (D_800483C8 != NULL))
                {
                    if (D_800483C8[texnum].unk_0_0 < 0xffU)
                    {
                        valid = TRUE;
                    }
                    else
                    {
                        valid = FALSE;
                    }
                }
                else
                {
                    valid = FALSE;
                }

                texLoadFromTextureNum(texnum, texpool);
                tex = texFindInPool(texnum, texpool);

#ifndef TARGET_N64
                { /* GE_TEXPAIR=1 debug: remember the data pointer and geometry
                   * each texnum resolves to, and report when the same texnum
                   * later resolves to a different record (a stale pool entry
                   * surviving a mission change). */
                    extern char *getenv(const char *);
                    extern int printf(const char *, ...);
                    static int pairdbg = -1;
                    static struct { s16 num; u8 *data; u8 w, h, fmt; } seen[512];
                    static s32 nseen;

                    if (pairdbg < 0)
                    {
                        pairdbg = getenv("GE_TEXPAIR") != NULL;
                    }
                    if (pairdbg && tex != NULL)
                    {
                        s32 k;
                        for (k = 0; k < nseen; k++)
                        {
                            if (seen[k].num == (s16) texnum)
                            {
                                if (seen[k].data != tex->data)
                                {
                                    printf("ge: TEXPAIR texnum %d CHANGED: %p (%dx%d fmt%d) -> %p (%dx%d fmt%d)\n",
                                           texnum, seen[k].data, seen[k].w, seen[k].h, seen[k].fmt,
                                           tex->data, tex->width, tex->height, tex->gbiformat);
                                    seen[k].data = tex->data;
                                    seen[k].w = tex->width;
                                    seen[k].h = tex->height;
                                    seen[k].fmt = tex->gbiformat;
                                }
                                break;
                            }
                        }
                        if (k == nseen && nseen < 512)
                        {
                            seen[nseen].num = (s16) texnum;
                            seen[nseen].data = tex->data;
                            seen[nseen].w = tex->width;
                            seen[nseen].h = tex->height;
                            seen[nseen].fmt = tex->gbiformat;
                            nseen++;
                        }
                    }
                }
#endif

#ifndef TARGET_N64
                dbgSeen++;
                if (tex != NULL) {
                    dbgFound++;
                } else {
                    dbgMissed++;
                    if (dbgFirstMiss < 0) dbgFirstMiss = texnum;
                }
#endif

                if (tex != NULL)
                {
                    out          = texWriteTextureCmd(out, saved, tex, writeTexFlag);
                    writeTexFlag = FALSE;

                    switch (in->words.w0 & 7)
                    {
                        case TEXTURETYPE_LOD:
                            min    = (in->words.w1 >> 24) & 0xff;
                            smode  = (in->words.w0 >> 22) & 3;
                            tmode  = (in->words.w0 >> 20) & 3;
                            offset = (in->words.w0 >> 18) & 3;
                            shifts = (in->words.w0 >> 14) & 0xf;
                            shiftt = (in->words.w0 >> 10) & 0xf;

                            if ((D_800483C8 != NULL) && (D_800483C8[texnum].unk_0_0 == 0xff))
                            {
                                do
                                {
                                    if (TRUE) {}
                                } while (FALSE);

                                D_800483C8[texnum].unk_0_0 = min;
                                D_800483C8[texnum].unk_1_3 = 15 - shifts;
                                D_800483C8[texnum].unk_1_0 = 15 - shiftt;
                            }

                            if (valid)
                            {
                                out = texHandleType0(
                                    out,
                                    tex,
                                    smode,
                                    tmode,
                                    offset,
                                    15 - D_800483C8[texnum].unk_1_3,
                                    15 - D_800483C8[texnum].unk_1_0,
                                    D_800483C8[texnum].unk_0_0);

                                gDPSetCycleType(out++, G_CYC_2CYCLE);
                                gDPSetTextureLOD(out++, G_TL_LOD);
                                gDPSetTextureDetail(out++, G_TD_DETAIL);
                            }
                            else
                            {
                                out = texHandleType0(out, tex, smode, tmode, offset, shifts, shiftt, min);
                            }
                            break;

                        case TEXTURETYPE_DETAIL:
                        {
                            struct tex *tex2;

                            texnum2 = (in->words.w1 >> 12) & 0xfff;

                            texLoadFromTextureNum(texnum2, texpool);
                            tex2 = texFindInPool(texnum2, texpool);

                            if (tex2 != NULL)
                            {
                                min    = ((*in).words.w1 >> 24) & 0xff;
                                smode  = (in->words.w0 >> 22) & 3;
                                tmode  = (in->words.w0 >> 20) & 3;
                                offset = (in->words.w0 >> 18) & 3;
                                shifts = (in->words.w0 >> 14) & 0xf;
                                shiftt = (in->words.w0 >> 10) & 0xf;

                                out = texHandleType1(out, tex, smode, tmode, offset, tex2, shifts, shiftt, min);
                            }
                            break;
                        }

                        case TEXTURETYPE_MIPMAP:
                            smode  = (in->words.w0 >> 22) & 3;
                            tmode  = (in->words.w0 >> 20) & 3;
                            offset = (in->words.w0 >> 18) & 3;

                            if (valid)
                            {
                                out = texHandleType0(
                                    out,
                                    tex,
                                    smode,
                                    tmode,
                                    offset,
                                    15 - D_800483C8[texnum].unk_1_3,
                                    15 - D_800483C8[texnum].unk_1_0,
                                    D_800483C8[texnum].unk_0_0);

                                gDPSetCycleType(out++, G_CYC_2CYCLE);
                                gDPSetTextureLOD(out++, G_TL_LOD);
                                gDPSetTextureDetail(out++, G_TD_DETAIL);
                            }
                            else
                            {
                                out = texHandleType2(out, tex, smode, tmode, offset);
                            }
                            break;

                        case TEXTURETYPE_TILE:
                            smode  = (in->words.w0 >> 22) & 3;
                            tmode  = (in->words.w0 >> 20) & 3;
                            offset = (in->words.w0 >> 18) & 3;
                            out    = texHandleType3(out, tex, smode, tmode, offset);
                            break;

                        case TEXTURETYPE_TILE_PRESWAPPED:
                            smode  = (in->words.w0 >> 22) & 3;
                            tmode  = (in->words.w0 >> 20) & 3;
                            offset = (in->words.w0 >> 18) & 3;
                            out    = texHandleType4(out, tex, smode, tmode, offset);
                            break;
                    }

                    // Caverns water
                    if (texnum == 1508)
                    {
                        out = sub_GAME_7F09343C(out, TRUE);
                        sub_GAME_7F0CC4C8();
                    }

                    /**
                     * This texture is used on the water in Dam and Complex. You can't actually
                     * see the texture in Complex because it's at the bottom of the pits and the
                     * vertex coloring is pitch black. You can still tell it's water by
                     * the splash sound it makes when shot.
                     */
                    if (texnum == 1511)
                    {
                        out = sub_GAME_7F09365C(out, TRUE);
                        sub_GAME_7F0CC4C8();
                    }

                    if (check_if_imageID_is_light(texnum))
                    {
#ifndef TARGET_N64
                        { extern s32 g_LightDbgTexnum; g_LightDbgTexnum = texnum; }
#endif
                        lightFixtureEntryBegin(out);
                        lightPending = TRUE;
                    }
                }

                in++;
                break;

            case G_RDPPIPESYNC:
                syncEmitted = TRUE;
                *(out++)    = *(in++);
                break;

            case 0xb1:
            case 0xbf:
                writeTexFlag = TRUE;
                syncEmitted  = FALSE;
                *(out++)     = *(in++);
                break;

            case 0xbb:
                saved        = out;
                writeTexFlag = FALSE;
                *(out++)     = *(in++);
                break;

            case 0xba:
                if (valid)
                {
#ifdef TARGET_N64
                    if ((((*(((s8 *)in) + 2)) == 17) || ((*(((s8 *)in) + 2)) == 20)) || ((*(((s8 *)in) + 2)) == 16))
#else
                    /* PC port: byte 2 of w0 (the SETOTHERMODE_H shift field)
                     * must be read from the word on little-endian. */
                    if ((((s8)((in->words.w0 >> 8) & 0xFF)) == 17) || (((s8)((in->words.w0 >> 8) & 0xFF)) == 20)
                        || (((s8)((in->words.w0 >> 8) & 0xFF)) == 16))
#endif
                    {
                        in++;
                    }
                    else
                    {
                        *(out++) = *(in++);
                    }
                }
                else
                {
                    *(out++) = *(in++);
                }
                break;

            default:
                *(out++) = *(in++);
                break;
        }

        count--;
    }

#ifndef TARGET_N64
    if (dbgSeen != 0) {
        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
        f32 v[5];
        v[0] = (f32)dbgSeen; v[1] = (f32)dbgFound; v[2] = (f32)dbgMissed;
        v[3] = (f32)dbgFirstMiss; v[4] = (f32)srcsize;
        ge_dbg_vec("texgdl seen/found/miss/first/srcsz", v, 5);
    }
#endif

    if (lightPending)
    {
        lightFixtureEntryEnd(out);
    }

    return ((intptr_t)out) - ((intptr_t)dst);
}


/***
 * NTSC address 0x7F0CE794.
*/
void texCopyGdls(Gfx *arg0, Gfx *arg1, s32 arg2)
{
#ifdef GE_NATIVE_64
    const u32 *raw = (const u32 *)arg0;
    for (s32 i = arg2 / 8 - 1; i >= 0; i--) {
        u32 w0 = raw[i * 2], w1 = raw[i * 2 + 1];
        arg1[i].words.w0 = __builtin_bswap32(w0);
        arg1[i].words.w1 = __builtin_bswap32(w1);
    }
    return;
#endif
    arg2 = (arg2 >> 3);
    arg0 = arg0 + (arg2 - 1);
    arg1 = arg1 + (arg2 - 1);

    while (arg2--)
    {
#ifdef TARGET_N64
        arg1->force_structure_alignment = arg0->force_structure_alignment;
#else
        /* PC: the only callers are the room loaders (bg.c), staging the
         * decompressed room GDL, whose words are big-endian. Convert to native
         * words during the staging copy, since every walker reads words
         * natively and raw BE commands would otherwise pass through. */
        {
            const u32 *s = (const u32 *) arg0;
            u32 *d = (u32 *) arg1;
            u32 w0 = s[0];
            u32 w1 = s[1];

            d[0] = (w0 >> 24) | ((w0 >> 8) & 0xFF00u) |
                   ((w0 & 0xFF00u) << 8) | (w0 << 24);
            d[1] = (w1 >> 24) | ((w1 >> 8) & 0xFF00u) |
                   ((w1 & 0xFF00u) << 8) | (w1 << 24);
        }
#endif
        arg1--;
        arg0--;
    }
}
