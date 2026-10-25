/**
 * PC-port RSP microcode symbols, segment-addressed display-list symbols,
 * and boot-chain straggler functions.
 *
 * == microcode (rsp.c:206-246, audi.c:48-556) ==
 * The fake scheduler never runs RSP code; task structs are filled in and
 * (for graphics) rewritten into videoSubmitCommands per D8. rsp.c computes
 * ucode_boot_size = rspbootTextEnd - rspbootTextStart and audi.c computes
 * gsp3DTextStart - rspbootTextStart, so the whole family is emitted as ONE
 * adjacency chain: every pointer is non-NULL and every pairwise difference
 * is a small positive constant (32 per hop).
 *
 * == display-list symbols ==
 * These are SEGMENT-ADDRESSED N64 data (segment 1 = fontdl VMA 0x01000000,
 * segment 2 = Globalimagetable / rarewarelogo VMA 0x02000000). They are
 * defined here as ABSOLUTE symbols with their REAL N64 addresses
 * (build/u/ge007.u.map:5683-5685, 6617-6633, 6714-6720) rather than PC
 * arrays, because their one software consumer does segment arithmetic:
 * image_bank.c:212 parses globalbank_rdram_offset + &globalDL_0xXXX where
 * globalbank_rdram_offset = pool + 0xFE000000 — with the absolute value
 * 0x02000000+off this wraps (u32) to pool+off, landing on the G_ENDDL
 * bytes planted in _GlobalimagetableSegmentRomStart (port/src/segments.c),
 * so texReset parses and terminates exactly like on N64. Every other
 * reference (gSPDisplayList/gDPLoadTextureBlock in explosion.c, front.c,
 * lv.c, title.c) only EMBEDS the address into command lists that the null
 * renderer never dereferences — same as N64, where the RSP resolved these
 * via its segment table.
 * NOTE: absolute-address data symbols require a non-PIE link (the port
 * links -m32/no-pie per the phase-2 build route). If the asset lane later
 * lands gotcha-5 (zeroing globalbank_rdram_offset) these symbols must be
 * revisited together with that change.
 *
 * == boot stragglers ==
 * init.c is compiled on PC but its mainproc is replaced by
 * port/src/main.c and never called; these two only need to link.
 */
#include <ultra64.h>
#include "asmsym.h" /* ASM_SYM: COFF `_` prefix on Windows, bare on ELF */

/* ---- microcode chain (zeros; .bss) ------------------------------------- */
__asm__(
    ASM_BSS
    "    .balign 16\n"
    "    .globl " ASM_SYM("rspbootTextStart") "\n"
    ASM_SYM("rspbootTextStart") ":\n"
    "    .space 32\n"
    "    .globl " ASM_SYM("rspbootTextEnd") "\n" /* boot size = 32 (rsp.c:238) */
    ASM_SYM("rspbootTextEnd") ":\n"
    "    .globl " ASM_SYM("gsp3DTextStart") "\n" /* audi.c:552 diff = 32 */
    ASM_SYM("gsp3DTextStart") ":\n"
    "    .space 32\n"
    "    .globl " ASM_SYM("gsp3DDataStart") "\n"
    ASM_SYM("gsp3DDataStart") ":\n"
    "    .space 32\n"
    "    .globl " ASM_SYM("aspMainTextStart") "\n"
    ASM_SYM("aspMainTextStart") ":\n"
    "    .space 32\n"
    "    .globl " ASM_SYM("aspMainDataStart") "\n"
    ASM_SYM("aspMainDataStart") ":\n"
    "    .space 32\n"
    "    .text\n"
);

/* fontdl / Globalimagetable / rarewarelogo DL and texture symbols:
 * ORIGINALLY absolute N64-VMA stand-ins here, REMOVED when the asset
 * compile-in wave linked the real font_dl.c / oddtextures.c /
 * rarewarelogo.c data (duplicate definitions). With image_bank.c's
 * globalbank_rdram_offset zeroed on PC, `0 + &globalDL_x` is the real
 * native pointer and the parse walks genuine Gfx commands. */

/* ---- ob segment end marker --------------------------------------------- */
/**
 * Referenced by the (asset-lane) ob file-address table as the
 * end-of-segment sentinel; ob.c obInit computes the last file's size as
 * next_rom_addr - this_rom_addr (phase2-decisions.md D9 gotcha 4). Real
 * N64 ROM address (map:7595) keeps that arithmetic consistent with
 * whatever ROM addresses the table carries until the compile-in wave
 * replaces the size scheme.
 */
__asm__(
    "    .globl " ASM_SYM("ob__ob_end_seg") "\n"
    "    .set " ASM_SYM("ob__ob_end_seg") ", 0x008f8100\n"
);

/* ---- boot-chain function stragglers ------------------------------------ */

/**
 * tlb_hardwire.s export (tlb_hardwire is ENGINE_EXCLUDEd). Sole caller is
 * init.c:141 in the N64 boot path that port/src/main.c replaces; on N64
 * it primed TLB context registers. Nothing to prime on PC.
 */
void initTLBPrepareContext(void)
{
}

/**
 * boot.c asm export (boot.c is ENGINE_EXCLUDEd): jump into the boot
 * decompressor. Sole caller init.c:126 (dead on PC). Returns the
 * decompressed length on N64; 0 = "nothing decompressed" keeps the dead
 * caller's arithmetic inert if it ever ran.
 */
u32 jump_decompressfile(u32 source, u32 target, u32 buffer)
{
    (void)source;
    (void)target;
    (void)buffer;
    return 0;
}
