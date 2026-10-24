/**
 * PC-port ramrom demo blobs + title.c's RLE image blob.
 *
 * == ramrom_* (14 symbols) ==
 * The REAL attract-mode demo recordings — the same assets/ramrom/*.bin
 * files the N64 build .incbin's (ROM 0x2bf5f0..0x2e6710 per
 * build/u/ge007.u.map): a ramromfilestructure header (game/ramromreplay.h,
 * 0xE8 bytes on-disk — IDO pads the struct to its u64 alignment) followed
 * by the input stream: 4-byte {speedframes,count,randseed,check} seed
 * blocks each heading count * size_cmds {stick_x,stick_y,button_lo,
 * button_hi} packets. game/ramromreplay.c:57 takes their ADDRESSES into
 * ramrom_table; playback (select_ramrom_to_play / GE_DEMO ->
 * replay_recorded_ramrom_at_address) romCopy's — a plain memcpy through
 * the PI shim — the header, then walks the stream block by block
 * (iterate_ramrom_entries_...:339).
 *
 * The files are BIG-ENDIAN, but only the header has multi-byte fields —
 * the entire input stream is u8/s8 — so replay_recorded_ramrom_at_address
 * byte-swaps the header in place right after copying it
 * (port_swap_ramrom_header, ramromreplay.c) and everything after is
 * consumed as-is. History: until that swap existed these symbols were
 * zeroed self-terminating stubs (count=0 + speedframes=0 first seed block
 * = the designed "demo over" exit), which is why attract mode used to
 * show one frame of Dam and bail back to the intro.
 *
 * == unknown2 / unknown2_end (title.c:437) ==
 * The RLE-compressed 8-bit gunbarrel-intro image (ROM 0x2a5070..0x2bf5f0 =
 * 0x1A580 bytes, "romfiles2" segment). title.c:444 romCopy's
 * ALIGN64(&unknown2_end - &unknown2) bytes then rle_expand_8bit()s the
 * copy, so BOTH symbols must exist with a controlled distance. A zeroed
 * blob is safe: rle.c:16-18 reads w=0, h=0 -> remaining = 0, writes exactly
 * one byte and returns (verified against src/game/rle.c). The pair is
 * emitted from ONE asm block so the size expression is exact (C cannot
 * guarantee adjacency of two separate objects). ELF/GAS syntax — matches
 * the WSL/clang-i686 build documented in docs/port/phase2-decisions.md.
 */
#include <ultra64.h>
#include "asmsym.h" /* ASM_SYM: COFF `_` prefix on Windows, bare on ELF */

/* Both blobs here are loaded from the user's ROM when one is present
 * (docs/port/romdata.md); the compiled-in copies exist only under
 * GE_ROMDATA_FALLBACK, otherwise the ramrom_* symbols are zero stubs (a safe
 * "demo over") and the gunbarrel pointers start NULL. */

/* Same single-asm-block .incbin technique as unknown2 below (the -I path
 * for .incbin points at assets/, set on this file in CMakeLists). */
#ifdef GE_ROMDATA_FALLBACK
#define PORT_RAMROM(name) \
    __asm__( \
        ASM_DATA \
        "    .balign 16\n" \
        "    .globl " ASM_SYM(#name) "\n" \
        ASM_SYM(#name) ":\n" \
        "    .incbin \"ramrom/" #name ".bin\"\n" \
        "    .balign 16\n" \
        "    .text\n" \
    )
#else
#define PORT_RAMROM(name) \
    __asm__( \
        ASM_DATA \
        "    .balign 16\n" \
        "    .globl " ASM_SYM(#name) "\n" \
        ASM_SYM(#name) ":\n" \
        "    .space 256\n" \
        "    .text\n" \
    )
#endif

PORT_RAMROM(ramrom_Dam_1);
PORT_RAMROM(ramrom_Dam_2);
PORT_RAMROM(ramrom_Facility_1);
PORT_RAMROM(ramrom_Facility_2);
PORT_RAMROM(ramrom_Facility_3);
PORT_RAMROM(ramrom_Runway_1);
PORT_RAMROM(ramrom_Runway_2);
PORT_RAMROM(ramrom_BunkerI_1);
PORT_RAMROM(ramrom_BunkerI_2);
PORT_RAMROM(ramrom_Silo_1);
PORT_RAMROM(ramrom_Silo_2);
PORT_RAMROM(ramrom_Frigate_1);
PORT_RAMROM(ramrom_Frigate_2);
PORT_RAMROM(ramrom_Train);

/* unknown2 .. unknown2_end: the REAL RLE-compressed gunbarrel-swirl image
 * (assets/ge007.u.2A4D50.usedby7F008DE4.bin, 0x1A580 bytes = the exact ROM
 * span, byte-exact; RLE reads bytes so no endian work). Adjacency of the
 * end label is guaranteed by the single asm block; the assembler -I path
 * for .incbin is set on this file in CMakeLists. */
#ifdef GE_ROMDATA_FALLBACK
__asm__(
    ASM_DATA
    "    .balign 16\n"
    "    .globl " ASM_SYM("port_unknown2Blob") "\n"
    ASM_SYM("port_unknown2Blob") ":\n"
    "    .incbin \"ge007.u.2A4D50.usedby7F008DE4.bin\"\n"
    "    .globl " ASM_SYM("port_unknown2BlobEnd") "\n"
    ASM_SYM("port_unknown2BlobEnd") ":\n"
    "    .balign 16\n"
    "    .text\n"
);
extern u8 port_unknown2Blob[], port_unknown2BlobEnd[];
/* title.c's PC arm spells these `unknown2` / `unknown2_end` via a
 * `#define x (*port_x)`, so its vanilla `&unknown2` is the pointer value */
u8 *port_unknown2 = port_unknown2Blob;
u8 *port_unknown2_end = port_unknown2BlobEnd;
#else
u8 *port_unknown2;
u8 *port_unknown2_end;
#endif
