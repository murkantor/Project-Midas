/**
 * PC-port synthesized linker-segment symbols.
 *
 * On N64 these are ld-script exports (see build/u/ge007.u.map): *SegmentStart/
 * *SegmentEnd are VMAs, *SegmentRom* are cart offsets. On PC romCopy() is a
 * memcpy through the PI shim, so every Rom* symbol must be REAL READABLE
 * HOST MEMORY of sufficient size, and every Start/End pair must subtract to
 * the size its consumer expects. Pairs consumed by subtraction are emitted
 * from single top-level asm blocks — C gives no layout guarantee between
 * separate objects, and a wrong (even negative) difference would feed
 * mempAllocBytesInBank/romCopy directly. ELF/GAS syntax, matching the
 * WSL/clang-i686 build route in docs/port/phase2-decisions.md (gotcha 8);
 * these blocks need porting if the build ever targets PE/COFF.
 *
 * Sizes are the REAL N64 segment sizes from build/u/ge007.u.map (line refs
 * below) wherever a consumer writes into a buffer it allocates from the
 * difference — several loaders write at fixed offsets regardless of size
 * (fonts: 94 fontchar fixups; anim table: pointer fixups at real offsets),
 * so undersized backing would corrupt the heap even though the copies
 * themselves are zeros.
 *
 * Per-symbol consumer table:
 *  _bssSegmentEnd            boss.c:217  base of the general heap
 *  _codeSegmentRomStart      init.c:129  inflate_code_size = inflate-code
 *  _inflateSegmentRomStart   init.c:129/boot getters (n64_stubs.c)
 *  _alt_startSegmentRomStart/Start  init.c:132 DMA (branch never taken)
 *  _Globalimagetable*        image_bank.c:172/176 size + romCopy source
 *  _animation_entriesSegmentRomStart  initanitable.c:249 fixup base
 *  _animation_data*          initanitable.c:261/265 size + copy
 *  _fontdlSegmentRom*        lv.c:251/254 size + copy
 *  _fontbankgothic*, _fontzurichbold*  textrelated.c:111-126
 *  _rarewarelogo*            title.c:380 size + copy
 *  _efontchardata/_jfontchardata RomStart  language.c:334/345 JPN glyphs
 *  port_imagesSegmentRomStart  image.c texLoad texture blob base (POINTER, see below)
 *  port_sfxctl/sfxtbl/instrumentsctl/instrumentstbl/musicsampletbl SegmentRomStart
 *                            music.c musicSeqPlayerInit audio banks + seq table (POINTERS)
 *
 * ROM LOADING (docs/port/romdata.md): the images and
 * music segments are no longer arrays but POINTER VARIABLES (port_*) that
 * start out at the compiled-in blobs and are repointed at the user's ROM by
 * portRomdataSegsApply() when romdata.c has one. Their consumers (image.c,
 * music.c) keep the vanilla `&_xxxSegmentRomStart` spelling through a
 * `#define _xxx (*port_xxx)` in their PC arm, so `&_xxx` yields the pointer
 * value. The blobs exist only under GE_ROMDATA_FALLBACK (CMake option,
 * default ON = GE_ROMDATA=0 still runs); OFF drops them from the exe and
 * the ROM becomes mandatory.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asmsym.h" /* ASM_SYM: COFF `_` prefix on Windows, bare on ELF */
#include "romdata.h" /* romdataSeg: the ROM-resident copies */

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console's history. */
extern void portConLogf(const char *fmt, ...);

/* ---- general heap base (boss.c:217) ------------------------------------ */
/**
 * boss.c: start = PHYS_TO_K0(osVirtualToPhysical(&_bssSegmentEnd));
 *         mempCheckMemflagTokens(start, tlbmanageGetTlbAllocatedBlock()-start)
 * On N64 this was "end of .bss to top-of-RAM". Providing an 8 MB arena
 * makes the symbol a real writable region of Expansion-Pak scale.
 * KNOWN PENDING (memory-layout track, flagged in n64_stubs.c's
 * tlbmanageGetTlbAllocatedBlock note): the heap SIZE subtraction spans two
 * unrelated PC objects, and PHYS_TO_K0 (R4300.h:58) ORs 0x80000000 into
 * the pointer — boss.c:217-218 cannot work as-is on PC regardless of what
 * this symbol is. The arena is the correct link-level answer; runtime
 * heap wiring is explicitly not solved here.
 */
u8 _bssSegmentEnd[0x800000];

/* ---- boot-chain segments (init.c mainproc — never called on PC) -------- */
/**
 * port/src/main.c replaces the N64 boot chain, so init.c:129-132 is dead
 * code that only needs to LINK — but keep its arithmetic harmless anyway:
 * inflate-code = 16, far below MAXCODESIZE (0xFFFB0), so the
 * osPiRawStartDma branch that would use _alt_start* can never be taken.
 */
__asm__(
    ASM_BSS
    "    .balign 16\n"
    "    .globl " ASM_SYM("_codeSegmentRomStart") "\n"
    ASM_SYM("_codeSegmentRomStart") ":\n"
    "    .space 16\n"
    "    .globl " ASM_SYM("_inflateSegmentRomStart") "\n"
    ASM_SYM("_inflateSegmentRomStart") ":\n"
    "    .space 16\n"
    "    .text\n"
);

u8 _alt_startSegmentRomStart[16]; /* DMA source of the never-taken branch */
u8 _alt_startSegmentStart[16];    /* DMA dest of the never-taken branch */

/* ---- global image table (image_bank.c texReset) ------------------------ */
/**
 * Size 0x1400 (map:6610-6666, VMA 0x02000000..0x02001400). texReset
 * allocates size+0x1000, romCopy's the Rom image, then parses 17
 * embedded texture-load display lists via
 *     texLoadFromDisplayList(globalbank_rdram_offset + &globalDL_0xXXX)
 * where globalbank_rdram_offset = pool + 0xFE000000. The globalDL_* are
 * ABSOLUTE 0x02xxxxxx symbols (port/src/ucode_dl_stubs.c), so the u32 sum
 * wraps to pool+offset — i.e. the parser reads THIS image's copy at each
 * DL offset. The Rom backing therefore carries a G_ENDDL opcode byte
 * (0xB8, F3DEX byte 0 in the N64 byte order the byte-wise parser at
 * image.c:2321 expects) at each of the 17 DL offsets, terminating every
 * parse immediately: texReset completes with zero textures loaded.
 * Offsets: 0x000, then 0x078 + n*0xA8 (matches map:6617-6633).
 */
__asm__(
    ASM_BSS
    "    .balign 16\n"
    "    .globl " ASM_SYM("_GlobalimagetableSegmentStart") "\n"
    ASM_SYM("_GlobalimagetableSegmentStart") ":\n"
    "    .space 0x1400\n"
    "    .globl " ASM_SYM("_GlobalimagetableSegmentEnd") "\n"
    ASM_SYM("_GlobalimagetableSegmentEnd") ":\n"
    "    .space 16\n"
    "    .text\n"
);

__asm__(
    ASM_DATA
    "    .balign 16\n"
    "    .globl " ASM_SYM("_GlobalimagetableSegmentRomStart") "\n"
    ASM_SYM("_GlobalimagetableSegmentRomStart") ":\n"
    "    .byte 0xB8\n"           /* G_ENDDL @ 0x000 (globalDL_0x000) */
    "    .space 0x77\n"
    "    .rept 16\n"             /* G_ENDDL @ 0x078,0x120,...,0xA50 */
    "    .byte 0xB8\n"
    "    .space 0xA7\n"
    "    .endr\n"
    "    .space 0x908\n"         /* pad to 0x1400 */
    "    .text\n"
);

/* ---- animation segments (initanitable.c) ------------------------------- */
/**
 * animation_data: size 0xE7E0 (map:6608). alloc_load_expand_ani_table
 * allocates exactly End-Start, copies the Rom image into it, then
 * expand_ani_table_entries WRITES pointer fixups at offsets taken from the
 * (asset-lane) animation tables — real size keeps every fixup inside the
 * allocation. Content zeros: an anim "entry" of zeros fixes up to
 * pointers at the table base; consumed only by the animation player.
 *
 * animation_entries RomStart: base address added to stored ROM offsets
 * (initanitable.c:249) that the anim streamer later romCopy's from.
 * Full real ROM span 0x169EC0 (map:6425) of zeros keeps every such read
 * in bounds. HONEST LIMIT: the anim bit-stream decoder running on zeroed
 * frames is compile-verified only; the ANIM_DATA compile-in wave
 * (phase2-decisions.md D9) replaces this path with real data.
 */
__asm__(
    ASM_BSS
    "    .balign 16\n"
    "    .globl " ASM_SYM("_animation_dataSegmentStart") "\n"
    ASM_SYM("_animation_dataSegmentStart") ":\n"
    "    .space 0xE7E0\n"
    "    .globl " ASM_SYM("_animation_dataSegmentEnd") "\n"
    ASM_SYM("_animation_dataSegmentEnd") ":\n"
    "    .space 16\n"
    "    .text\n"
);

u8 _animation_dataSegmentRomStart[0xE7E0];
u8 _animation_entriesSegmentRomStart[0x169EC0];

/* ---- fonts (textrelated.c load_font_tables, lv.c lvInit) --------------- */
/**
 * Real sizes are load-bearing: load_font_tables allocates End-Start and
 * then unconditionally fixes up chars[0..93].pixeldata inside the copy —
 * struct font is 0xB74 bytes (bondtypes.h:4211), so anything smaller than
 * the real segment (gothic 0x24B0 map:6766, zurichbold 0x3540 map:6778)
 * would write past the allocation. Zeroed glyph data renders nothing —
 * fine under the null renderer.
 * fontdl: 0xC0 (map:5686), copied by lvInit into ptr_font_DL; content is
 * RDP-only (never software-parsed), zeros are fine.
 */
__asm__(
    ASM_BSS
    "    .balign 16\n"
    "    .globl " ASM_SYM("_fontbankgothicSegmentStart") "\n"
    ASM_SYM("_fontbankgothicSegmentStart") ":\n"
    "    .space 0x24B0\n"
    "    .globl " ASM_SYM("_fontbankgothicSegmentEnd") "\n"
    ASM_SYM("_fontbankgothicSegmentEnd") ":\n"
    "    .space 16\n"
    "    .balign 16\n"
    "    .globl " ASM_SYM("_fontzurichboldSegmentStart") "\n"
    ASM_SYM("_fontzurichboldSegmentStart") ":\n"
    "    .space 0x3540\n"
    "    .globl " ASM_SYM("_fontzurichboldSegmentEnd") "\n"
    ASM_SYM("_fontzurichboldSegmentEnd") ":\n"
    "    .space 16\n"
    "    .balign 16\n"
    "    .globl " ASM_SYM("_fontdlSegmentRomStart") "\n"
    ASM_SYM("_fontdlSegmentRomStart") ":\n"
    "    .space 0xC0\n"
    "    .globl " ASM_SYM("_fontdlSegmentRomEnd") "\n"
    ASM_SYM("_fontdlSegmentRomEnd") ":\n"
    "    .space 16\n"
    "    .text\n"
);

u8 _fontbankgothicSegmentRomStart[0x24B0];
u8 _fontzurichboldSegmentRomStart[0x3540];

/**
 * TEXT WAVE: compose the two Latin font segment images from the typed
 * asset arrays (assets/font/*.c). Original segment layout, verified to
 * pack byte-exact (gothic: 676+2256+6460 == 0x24B0):
 *   [ s32 kerning[13*13] ][ struct fontchar chars[94] ][ glyph bytes ]
 * The kerning + chartable values are NATIVE (typed C; load_font_tables
 * promotes each char's pixeldata offset by the copy base). The glyph
 * region is a u32-carried BIG-ENDIAN byte blob (12-byte 0xB8-marker
 * headers, same convention as the inline model textures) rendered as
 * G_IM_FMT_I 8-bit = a BYTE format, so it gets the full word swap.
 * fontZurichBold's kerning table was never extracted from ROM (its .c
 * has no kerning array); zeros = no kerning adjustments, spacing runs
 * marginally looser until it is - honest limit, revisit if visible.
 * Called once from load_font_tables' PC arm.
 */
#define PORT_FONT_NATIVE_WORDS (169 + 94 * 6) /* kerning + fontchar table: every field is a 32-bit int */
#define PORT_FONTDL_SIZE 0xC0
#define PORT_JFONT_SIZE 0xB700
#define PORT_EFONT_SIZE 0x1A80

#ifdef GE_ROMDATA_FALLBACK
/* builds the font segments from the compiled-in data into caller buffers
 * (the segment arrays or probe scratch); see the comment above for the layout */
static void portFontComposeCompiled(u8 *gothic, u8 *zurich, u8 *fontdl)
{
    extern u32 fontBankGothic_kerning[];       /* 169 words            */
    extern u32 fontBankGothic_fontchartable[]; /* 94 * 6 words         */
    extern u32 fontBankGothic_fontbytes[];     /* 1615 words           */
    extern u32 fontZurichBold_fontchartable[]; /* 94 * 6 words         */
    extern u32 fontZurichBold_fontbytes[];     /* 2675 words           */
    u32 *dst;
    u32 w;

    dst = (u32 *) gothic;
    for (w = 0; w < 169; w++) {
        dst[w] = fontBankGothic_kerning[w];
    }
    for (w = 0; w < 94 * 6; w++) {
        dst[169 + w] = fontBankGothic_fontchartable[w];
    }
    for (w = 0; w < 1615; w++) {
        u32 v = fontBankGothic_fontbytes[w];

        dst[169 + 94 * 6 + w] = (v >> 24) | ((v >> 8) & 0xFF00u) |
                                ((v & 0xFF00u) << 8) | (v << 24);
    }

    dst = (u32 *) zurich;
    /* kerning[0..168] stays zero (see note above) */
    for (w = 0; w < 94 * 6; w++) {
        dst[169 + w] = fontZurichBold_fontchartable[w];
    }
    for (w = 0; w < 2675; w++) {
        u32 v = fontZurichBold_fontbytes[w];

        dst[169 + 94 * 6 + w] = (v >> 24) | ((v >> 8) & 0xFF00u) |
                                ((v & 0xFF00u) << 8) | (v << 24);
    }

    /* Build fontdl from the native Gfx assets, packed in their N64 layout
     * order, because HUD commands call its DLs through the segment base
     * (lv.c:677). A zeroed segment makes the renderer run off the end into
     * other data and hit an unknown opcode. */
    {
        extern Gfx dlBasicGeometry[4];
        extern Gfx dlZBufferGeometry[4];
        extern Gfx dlFastPipelineSetup[15];
        Gfx *fd = (Gfx *) fontdl;

        for (w = 0; w < 4; w++) {
            fd[w] = dlBasicGeometry[w];
        }
        for (w = 0; w < 4; w++) {
            fd[4 + w] = dlZBufferGeometry[w];
        }
        for (w = 0; w < 15; w++) {
            fd[8 + w] = dlFastPipelineSetup[w];
        }
    }
}

/* ROM-derived vs compiled, both in the host layout the game reads */
static void portFontProbe(const char *name, const u8 *rom, const u8 *built, u32 size, u32 kerningbytes)
{
    u32 i;
    for (i = 0; i < size && rom[i] == built[i]; i++) {
    }
    if (i == size) {
        portConLogf("ge: romdata: seg %-18s ROM-derived image identical to the compiled-in one (%u)\n", name, size);
    } else if (kerningbytes != 0 && i < kerningbytes && memcmp(rom + kerningbytes, built + kerningbytes, size - kerningbytes) == 0) {
        portConLogf("ge: romdata: seg %-18s ROM-derived image identical past the kerning table; kerning DIFFERS from byte %u (the compiled copy never had it - the ROM's is now used)\n",
                name, i);
    } else {
        portConLogf("ge: romdata: seg %-18s ROM-derived image DIFFERS from the compiled-in one at byte %u of %u (a mod - expected - or a relocated segment)\n",
                name, i, size);
    }
}
#endif

/* a BE ROM span -> the host layout: the first `nativewords` u32 become
 * native (kerning + the all-s32 fontchar table; every Gfx word of fontdl),
 * the rest (glyph bytes, a BYTE format) stays as it is */
static void portFontFromRom(u8 *dst, const u8 *rom, u32 size, u32 nativewords)
{
    u32 w;
    memcpy(dst, rom, size);
    for (w = 0; w < nativewords; w++) {
        u32 v = ((u32) rom[w * 4] << 24) | ((u32) rom[w * 4 + 1] << 16) | ((u32) rom[w * 4 + 2] << 8) | rom[w * 4 + 3];
        memcpy(dst + w * 4, &v, 4);
    }
}

void portFontBuildSegments(void)
{
    extern u8 _fontdlSegmentRomStart[];
    extern u8 _jfontchardataSegmentRomStart[];
    extern u8 _efontchardataSegmentRomStart[];
    u32 gsz = 0, zsz = 0, dsz = 0, jsz = 0, esz = 0;
    const u8 *g = romdataSeg("fontbankgothic", &gsz);
    const u8 *z = romdataSeg("fontzurichbold", &zsz);
    const u8 *d = romdataSeg("fontdl", &dsz);
    const u8 *j = romdataSeg("jfontchardata", &jsz);
    const u8 *e = romdataSeg("efontchardata", &esz);

    /* Load the five font segments from the ROM: the Latin fonts' kerning and
     * fontchar tables and the fontdl Gfx words get word-swapped. Glyph bytes
     * and the Japanese glyph banks are raw bytes and are copied as-is. */
    if (g != NULL && z != NULL && d != NULL && gsz == 0x24B0 && zsz == 0x3540 && dsz == PORT_FONTDL_SIZE) {
#ifdef GE_ROMDATA_FALLBACK
        {
            static u8 gothic[0x24B0], zurich[0x3540], fontdl[PORT_FONTDL_SIZE];
            portFontComposeCompiled(gothic, zurich, fontdl);
            portFontFromRom(_fontbankgothicSegmentRomStart, g, gsz, PORT_FONT_NATIVE_WORDS);
            portFontFromRom(_fontzurichboldSegmentRomStart, z, zsz, PORT_FONT_NATIVE_WORDS);
            portFontFromRom(_fontdlSegmentRomStart, d, dsz, PORT_FONTDL_SIZE / 4);
            portFontProbe("fontbankgothic", _fontbankgothicSegmentRomStart, gothic, gsz, 169 * 4);
            portFontProbe("fontzurichbold", _fontzurichboldSegmentRomStart, zurich, zsz, 169 * 4);
            portFontProbe("fontdl", _fontdlSegmentRomStart, fontdl, dsz, 0);
            /* GE_ROMFONT_KERNING=0: zero ZurichBold's kerning again, as the
             * compiled copy had it (a debug A/B switch for pixel diffs) */
            {
                const char *k = getenv("GE_ROMFONT_KERNING");
                if (k != NULL && k[0] == '0') {
                    memset(_fontzurichboldSegmentRomStart, 0, 169 * 4);
                    portConLogf("ge: romdata: GE_ROMFONT_KERNING=0: ZurichBold kerning zeroed (A/B)\n");
                }
            }
        }
#else
        portFontFromRom(_fontbankgothicSegmentRomStart, g, gsz, PORT_FONT_NATIVE_WORDS);
        portFontFromRom(_fontzurichboldSegmentRomStart, z, zsz, PORT_FONT_NATIVE_WORDS);
        portFontFromRom(_fontdlSegmentRomStart, d, dsz, PORT_FONTDL_SIZE / 4);
        portConLogf("ge: romdata: seg fonts (gothic, zurichbold, fontdl) -> ROM\n");
#endif
        if (j != NULL && jsz == PORT_JFONT_SIZE && e != NULL && esz == PORT_EFONT_SIZE) {
            memcpy(_jfontchardataSegmentRomStart, j, jsz);
            memcpy(_efontchardataSegmentRomStart, e, esz);
            portConLogf("ge: romdata: seg jfontchardata/efontchardata -> ROM (JPN glyphs, %u + %u bytes; were zeros)\n", jsz, esz);
        }
        return;
    }
#ifdef GE_ROMDATA_FALLBACK
    if (g != NULL || z != NULL || d != NULL) {
        portConLogf("ge: romdata: font segments unusable (sizes %u/%u/%u) - compiled-in copies\n", gsz, zsz, dsz);
    }
    portFontComposeCompiled(_fontbankgothicSegmentRomStart, _fontzurichboldSegmentRomStart, _fontdlSegmentRomStart);
#else
    portConLogf("ge: romdata: no font segments in the ROM and no compiled-in copies (GE_ROMDATA_FALLBACK=OFF) - exiting\n");
    exit(3);
#endif
}

/* ---- rareware logo (title.c:380) --------------------------------------- */
/**
 * Size 0x67F0 (map:6721). title.c copies ALIGN64(End-Start) into a caller
 * buffer that held the same amount on N64; the copied bytes are logo
 * model/texture data referenced only through the (null) renderer via the
 * absolute D_02004xxx symbols. Zeros -> black logo, no crash.
 */
__asm__(
    ASM_BSS
    "    .balign 16\n"
    "    .globl " ASM_SYM("_rarewarelogoSegmentStart") "\n"
    ASM_SYM("_rarewarelogoSegmentStart") ":\n"
    "    .space 0x67F0\n"
    "    .globl " ASM_SYM("_rarewarelogoSegmentEnd") "\n"
    ASM_SYM("_rarewarelogoSegmentEnd") ":\n"
    "    .space 64\n"             /* ALIGN64 slack for the copy length */
    "    .text\n"
);

u8 _rarewarelogoSegmentRomStart[0x6800]; /* 0x67F0 rounded to ALIGN64 */

/**
 * STUBBED-WALK FOLLOW-UP: the logo segment copy above is VESTIGIAL on PC -
 * assets/rarewarelogo.c has ZERO raw 0x02xxxxxx words; every gsSPVertex/
 * SETTIMG/DisplayList reference is a typed host symbol, so the renderer
 * reads the compiled-in data directly and the zero-filled copy buffer is
 * never consulted. What IS broken: the six RGBA16 texture blobs are
 * u32-carried BE word images; fast3d reads 16-bit texels as NATIVE u16s,
 * so each u32 needs the HALFWORD EXCHANGE (the settled 16-bit texel rule -
 * full swap32 turns RGBA16 purple; see the legal-screen fix). One-shot:
 * compiled-in data persists across reloads.
 */
void portRarewareLogoFixTexels(void)
{
    extern u32 imgRAre_0x0020[];  /* 0x0020..0x0AD8 */
    extern u32 img_raRE_0x0AE0[]; /* 0x0AE0..0x1598 */
    extern u32 imgWAre_0x15A0[];  /* 0x15A0..0x2058 */
    extern u32 imgwaRE_0x2060[];  /* 0x2060..0x2B18 */
    extern u32 D_02004FE8[];      /* 0x4FE8..0x5FE8 */
    extern u32 D_02005FF0[];      /* 0x5FF0..0x67F0 */
    static const struct { u32 *data; u32 words; } blobs[] = {
        { imgRAre_0x0020,  0x0AB8 / 4 },
        { img_raRE_0x0AE0, 0x0AB8 / 4 },
        { imgWAre_0x15A0,  0x0AB8 / 4 },
        { imgwaRE_0x2060,  0x0AB8 / 4 },
        { D_02004FE8,      0x1000 / 4 },
        { D_02005FF0,      0x0800 / 4 },
    };
    static int done;
    u32 i, w;

    if (done) {
        return;
    }
    done = 1;

    for (i = 0; i < sizeof(blobs) / sizeof(blobs[0]); i++) {
        for (w = 0; w < blobs[i].words; w++) {
            u32 v = blobs[i].data[w];
            blobs[i].data[w] = (v << 16) | (v >> 16);
        }
    }

    /* The four RAREWARE text mip chains were stored with each odd row's
     * u32 pairs pre-swapped for the N64 TMEM (loaded with dxt=0), and fast3d
     * samples linearly, so swap them back per mip level here. The badge
     * textures (D_02004FE8/D_02005FF0) use a real dxt and are already linear. */
    {
        static const struct { u32 off; u32 rowbytes; u32 rows; } lvl[] = {
            { 0,    64, 32 },
            { 2048, 32, 16 },
            { 2560, 16, 8 },
            { 2688, 8,  4 },
        };
        u32 t, L, r, g;
        u32 *texs[4];

        texs[0] = imgRAre_0x0020;
        texs[1] = img_raRE_0x0AE0;
        texs[2] = imgWAre_0x15A0;
        texs[3] = imgwaRE_0x2060;

        for (t = 0; t < 4; t++) {
            for (L = 0; L < sizeof(lvl) / sizeof(lvl[0]); L++) {
                for (r = 1; r < lvl[L].rows; r += 2) {
                    u32 *row = texs[t] + (lvl[L].off + r * lvl[L].rowbytes) / 4;
                    for (g = 0; g + 1 < lvl[L].rowbytes / 4; g += 2) {
                        u32 a = row[g];
                        row[g] = row[g + 1];
                        row[g + 1] = a;
                    }
                }
            }
        }
    }
}

/* ---- JPN font pixel banks (language.c) --------------------------------- */
/**
 * language.c:334/345 romCopy's 0x60/0x80-byte glyph cells from
 * base + (codepoint >> 1) * cellsize. Real ROM sizes (jfont 0xB700
 * map:6182, efont 0x1A80 map:6243) keep every in-range codepoint's read
 * in bounds. US build (LANG_US) never renders JPN text, so this is
 * defense in depth; zeroed glyphs would draw blank if it ever ran.
 */
u8 _jfontchardataSegmentRomStart[0xB700];
u8 _efontchardataSegmentRomStart[0x1A80];

/* ---- texture blob (image.c texLoad) ------------------------------------ */
/**
 * texLoad romCopy's from base + offset where offsets come from the
 * (asset-lane) g_Textures table and span the full N64 .images segment:
 * 0x2EEF20 bytes (map:7599/7607, ROM 0x8f8110..0xbe7030).
 *
 * TEXTURE WAVE: the segment is embedded byte-exact from
 * assets/images/combined/combined.bin (same 0x2EEF20 bytes, extracted in
 * ROM order - see assets/images/readme.md). No endian work: both GE's
 * 1172/zlib decompressors and fast3d's import_* helpers read BYTES.
 * The assembler include path for .incbin is set on this file in
 * CMakeLists (-Wa,-I.../assets/images/combined).
 */
#ifdef GE_ROMDATA_FALLBACK
__asm__(
    ASM_DATA
    "    .balign 16\n"
    "    .globl " ASM_SYM("port_imagesBlob") "\n"
    ASM_SYM("port_imagesBlob") ":\n"
    "    .incbin \"combined.bin\"\n"
    "    .balign 16\n"
    "    .text\n"
);
extern u8 port_imagesBlob[];
u8 *port_imagesSegmentRomStart = port_imagesBlob;
#else
u8 *port_imagesSegmentRomStart; /* romdata fills it, or the build refuses to run without a ROM */
#endif

/* ---- audio bank segments (music.c musicSeqPlayerInit) ------------------ */
/**
 * One asm block => exact adjacency for both size subtractions:
 *   sfx  bank image size  = _sfxtbl - _sfxctl  = 0x40
 *   inst bank image size  = _instrumentstbl - _instrumentsctl = 0x40
 * Each ctl image is a native-endian ALBankFile {revision=0, bankCount=1,
 * bankArray[0]=&portPcmBank} — a HOST pointer to the fake silent bank in
 * port/src/audio_stubs.c, which is why alBnkfNew can be a no-op: there
 * are no offsets left to patch. music.c copies the image and reads
 * bankArray[0] out of the copy (music.c:670/680); the pointer survives
 * memcpy. Real N64 sizes (0x5BC0/0x43A0) are NOT used: the game only
 * reaches bank contents through bankArray[0], and small images leave the
 * 0x2E000 music heap with more headroom than N64 had.
 *
 * _musicsampletbl is a native-endian RareALSeqBankFile (music.h:36):
 *   seqCount=64 (> the 63 track ids), 64 entries {addr=0x210, ulen=16,
 *   clen=16}, then at +0x210 a REAL mini stream: 11 72 (Rare 1172 magic,
 *   decompress.c:44) + 03 00 (deflate: final fixed-Huffman block, EOB) —
 *   inflates to 0 bytes and terminates.
 * Why crafted and not zeroed: musicSeqFileNew only rebases entries
 * i < seqCount; a zeroed table (seqCount=0) leaves address=0 for every
 * track, and musicTrackXPlay's `address < 0x10000` guard then recurses
 * into musicTrackXPlay(M_SHORT_SOLO_DEATH) forever -> stack overflow.
 * With seqCount=64 every address rebases to this blob (host address,
 * always >= 0x10000): the play path romCopy's 16 bytes, "decompresses"
 * the empty stream, and hands the empty seq to the no-op CSP stubs —
 * every music trigger is a safe silent no-op. GE's inflate MUST get this
 * valid stream: it cannot digest zeros (see _imagesSegmentRomStart note).
 * Layout: 4 + 64*8 = 516 used, pad to 0x210, 4 stream bytes, pad 12
 * (=> 0x220 total; the two romCopy's read ALIGN16(516)=528 and 16 bytes).
 */
/**
 * AUDIO PHASE 1: the ctl/tbl images are the REAL banks from assets/music
 * (byte-exact BIG-ENDIAN SGI .ctl format, revision 'B1'; sfx.ctl 0x5BC0,
 * instruments.ctl 0x43A0 - the exact N64 sizes). music.c still measures
 * each ctl image by symbol adjacency (_sfxtbl - _sfxctl), so the four
 * symbols stay in ROM order inside one asm block. alBnkfNew (now a real
 * BE-aware patcher in audio_stubs.c) converts the copied image's fields
 * to native and its offsets to pointers; the crafted portPcmBank fake is
 * no longer referenced from here.
 */
#ifdef GE_ROMDATA_FALLBACK
__asm__(
    ASM_DATA
    "    .balign 16\n"
    "    .globl " ASM_SYM("port_sfxctlBlob") "\n"
    ASM_SYM("port_sfxctlBlob") ":\n"
    "    .incbin \"sfx.ctl\"\n"
    "    .balign 16\n"
    "    .globl " ASM_SYM("port_sfxtblBlob") "\n"
    ASM_SYM("port_sfxtblBlob") ":\n"
    "    .incbin \"sfx.tbl\"\n"
    "    .balign 16\n"
    "    .globl " ASM_SYM("port_instrumentsctlBlob") "\n"
    ASM_SYM("port_instrumentsctlBlob") ":\n"
    "    .incbin \"instruments.ctl\"\n"
    "    .balign 16\n"
    "    .globl " ASM_SYM("port_instrumentstblBlob") "\n"
    ASM_SYM("port_instrumentstblBlob") ":\n"
    "    .incbin \"instruments.tbl\"\n"
    "    .balign 16\n"
    /* MUSIC PHASE: the REAL sequence bank (assets/music/music.sbk =
     * byte-exact BE segment image: u16 seqCount=63, u16 pad, 63 entries
     * {u32 offset, u16 ulen, u16 clen}, then the 1172-compressed midi
     * streams packed from 0x1FC - entry offsets chain exactly).
     * musicSeqPlayerInit's PC arm byteswaps the header it copies out;
     * the streams themselves are byte data for the 1172 inflater. */
    "    .globl " ASM_SYM("port_musicsampletblBlob") "\n"
    ASM_SYM("port_musicsampletblBlob") ":\n"
    "    .incbin \"music.sbk\"\n"
    "    .balign 16\n"
    "    .text\n"
);
extern u8 port_sfxctlBlob[], port_sfxtblBlob[], port_instrumentsctlBlob[], port_instrumentstblBlob[], port_musicsampletblBlob[];
u8 *port_sfxctlSegmentRomStart = port_sfxctlBlob;
u8 *port_sfxtblSegmentRomStart = port_sfxtblBlob;
u8 *port_instrumentsctlSegmentRomStart = port_instrumentsctlBlob;
u8 *port_instrumentstblSegmentRomStart = port_instrumentstblBlob;
u8 *port_musicsampletblSegmentRomStart = port_musicsampletblBlob;
#else
u8 *port_sfxctlSegmentRomStart;
u8 *port_sfxtblSegmentRomStart;
u8 *port_instrumentsctlSegmentRomStart;
u8 *port_instrumentstblSegmentRomStart;
u8 *port_musicsampletblSegmentRomStart;
#endif

/* ---- ROM loading: repoint the segment data at the ROM image ------ */
/**
 * Called by romdata.c once the ROM is parsed. Repoints every segment pointer
 * above, the gunbarrel pair (ramrom_data.c) and the 14 attract-demo table slots
 * (ramromreplay.c portRamromRebase) at the ROM buffer. This is safe because
 * readers only romCopy (memcpy) out of segments and never write them, which is
 * how the N64 worked too.
 *
 * music.c sizes the sound banks by pointer subtraction (tbl - ctl), so the ROM
 * must keep the same adjacency the asm blocks had (romseg_list.h).
 *
 * In GE_ROMDATA_FALLBACK builds each ROM span is first memcmp'd against the
 * compiled-in copy: "identical" on a stock ROM confirms the spans are exact,
 * while "DIFFERS" is expected on a mod and shows where it changed.
 */
extern u8 *port_unknown2, *port_unknown2_end;      /* ramrom_data.c: gunbarrel RLE image */
extern void *portRamromFdata(s32 index);          /* ramromreplay.c: the demo table */
extern void portRamromRebase(s32 index, void *fdata);

static void portRomdataSegProbe(const char *name, const u8 *old, const u8 *rom, u32 size)
{
#ifdef GE_ROMDATA_FALLBACK
    if (old != NULL && rom != NULL) {
        u32 i;
        for (i = 0; i < size && old[i] == rom[i]; i++) {
        }
        if (i == size) {
            portConLogf("ge: romdata: seg %-18s ROM bytes identical to the compiled-in copy (%u)\n", name, size);
        } else {
            portConLogf("ge: romdata: seg %-18s ROM DIFFERS from the compiled-in copy at byte %u of %u (a mod - expected - or a relocated segment)\n",
                    name, i, size);
        }
        return;
    }
#endif
    (void) old;
    (void) rom;
    portConLogf("ge: romdata: seg %-18s -> ROM (%u bytes)\n", name, size);
}

void portRomdataSegsApply(void)
{
    static const struct {
        const char *name;
        u8 **var;
    } segs[] = {
        { "images",         &port_imagesSegmentRomStart },
        { "sfxctl",         &port_sfxctlSegmentRomStart },
        { "sfxtbl",         &port_sfxtblSegmentRomStart },
        { "instrumentsctl", &port_instrumentsctlSegmentRomStart },
        { "instrumentstbl", &port_instrumentstblSegmentRomStart },
        { "musicsampletbl", &port_musicsampletblSegmentRomStart },
        { "gunbarrel",      &port_unknown2 },
    };
    static const char *const ramrom[] = {
        "ramrom_Dam_1", "ramrom_Dam_2", "ramrom_Facility_1", "ramrom_Facility_2", "ramrom_Facility_3",
        "ramrom_Runway_1", "ramrom_Runway_2", "ramrom_BunkerI_1", "ramrom_BunkerI_2", "ramrom_Silo_1",
        "ramrom_Silo_2", "ramrom_Frigate_1", "ramrom_Frigate_2", "ramrom_Train",
    };
    size_t i;
    int missing = 0;

    for (i = 0; i < sizeof(segs) / sizeof(segs[0]); i++) {
        u32 size = 0;
        const u8 *p = romdataSeg(segs[i].name, &size);
        if (p == NULL) {
            portConLogf("ge: romdata: seg %-18s NOT in this ROM - keeping the compiled-in copy%s\n", segs[i].name,
                    *segs[i].var == NULL ? " (NONE IN THIS BUILD - will crash)" : "");
            missing++;
            continue;
        }
        portRomdataSegProbe(segs[i].name, *segs[i].var, p, size);
        *segs[i].var = (u8 *) p;
        if (segs[i].var == &port_unknown2) {
            port_unknown2_end = (u8 *) p + size; /* title.c sizes the copy by end - start */
        }
    }
    for (i = 0; i < sizeof(ramrom) / sizeof(ramrom[0]); i++) {
        u32 size = 0;
        const u8 *p = romdataSeg(ramrom[i], &size);
        if (p == NULL) {
            portConLogf("ge: romdata: seg %-18s NOT in this ROM - keeping the compiled-in copy\n", ramrom[i]);
            missing++;
            continue;
        }
        portRomdataSegProbe(ramrom[i], (const u8 *) portRamromFdata((s32) i), p, size);
        portRamromRebase((s32) i, (void *) p);
    }
    portConLogf("ge: romdata: Phase B consumers on the ROM: images, music (5 segments), ramrom (14), gunbarrel%s\n",
            missing ? " - SOME MISSING, see above" : "");
}
