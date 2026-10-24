/**
 * Animation wave (docs/port/phase2-decisions.md D9): build the runtime
 * animation segment images from the compiled-in asset data.
 *
 * On N64 two ROM segments feed the animation system:
 *  - animation_data (0xE7E0): ModelAnimation headers + root-motion bit
 *    descriptors + small bit-streams. alloc_load_expand_ani_table copies it
 *    into a memp allocation (ptr_animation_table) and expand_ani_table_entries
 *    rewrites the offset fields into pointers.
 *  - animation_entries (0x169EC0): the per-frame bit-stream data, streamed
 *    with romCopy at anim->address + frame offsets.
 *
 * The compiled-in C (assets/animationtable_data.c / animationtable_entries.c)
 * stores the segments as u32 words holding the BIG-ENDIAN word values, so on
 * a little-endian host the in-memory BYTES are word-swapped relative to the
 * N64 image. Consumers are mixed-granularity:
 *  - bit-stream decoders (model.c modelAnimReadRootMotionValue etc.) read
 *    BYTES -> need the byte-exact N64 image;
 *  - ModelAnimation headers and ModelAnimBitField descriptors are read as
 *    typed structs (u16/u8/u32 fields) -> need NATIVE field storage.
 *
 * So: copy each blob swap32'd (byte-exact N64 image), then walk every
 * animation entry (raw offsets read from animation_table_ptrs1/2 BEFORE the
 * game's fixup pass runs) and convert its 0x44-byte header and its
 * descriptor table [word2, word4) back to native field layout.
 *
 * Layout precondition: the asset TUs tile their segment contiguously in
 * declaration order (-fno-toplevel-reorder -malign-data=abi in CMakeLists).
 * The probes below abort loudly if the linker drifted.
 *
 * HONEST LIMITS: compile-verified transformation; animation PLAYBACK
 * correctness (decoder vs these bytes) is runtime-pending. ANIM_DATA_empty
 * (offset 0, 0x1C bytes) is treated as plain stream data - it is idle's
 * descriptor table, not a header.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ultra64.h>
#include "romdata.h" /* animation segments are loaded from the ROM */

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console ring (spec section 7). */
extern void portConLogf(const char *fmt, ...);

#define ANIM_DATA_SEG_SIZE    0xE7E0u
#define ANIM_ENTRIES_SEG_SIZE 0x169EC0u
/* Only the first 0x14 bytes of ModelAnimation are real packed data: entries
 * tile at 0x30-byte spacings and each entry's descriptors/stream PRECEDE its
 * header, so the struct's unk14..unk3c tail overlays neighboring data. No
 * code reads those fields (grep-verified) - the decomp struct over-declares. */
#define ANIM_HEADER_SIZE      0x14u

/* zero-filled backing arrays in segments.c; the game romCopy's from these */
extern unsigned char _animation_dataSegmentRomStart[];
extern unsigned char _animation_entriesSegmentRomStart[];

/* initanitable.c: raw PTR_ANIM_* segment offsets (0-terminated, 1 = null
 * entry) - still offsets here because expand_ani_table_entries runs later */
extern intptr_t animation_table_ptrs1[];
extern intptr_t animation_table_ptrs2[];

#ifdef GE_ROMDATA_FALLBACK
/* compiled-in segment blobs (assets/animationtable_data.c, in file order) */
extern uint32_t ANIM_DATA_empty[];             /* segment offset 0x0000 */
extern uint32_t ANIM_DATA_idle[];              /* segment offset 0x001C */
extern uint32_t ANIM_DATA_bond_eye_walk[];     /* segment offset 0x4144 */
extern uint32_t ANIM_DATA_helicopter_takeoff[];/* segment offset 0xE7C0 */

/* compiled-in frame streams (assets/animationtable_entries.c, in file order) */
extern uint32_t ANIM_ENTRY_idle[];             /* segment offset 0x000000 */
extern uint32_t ANIM_ENTRY_fire_standing[];    /* segment offset 0x002b4c */
extern uint32_t ANIM_ENTRY_helicopter_takeoff[]; /* segment offset 0x169bd0 */
#endif

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

static void portAnimProbe(const void *base, const void *sym, uint32_t expect, const char *name)
{
    uint32_t got = (uint32_t) ((const unsigned char *) sym - (const unsigned char *) base);
    if (got != expect) {
        portConLogf("ge: animdata layout drift: %s at +0x%x, expected +0x%x "
                        "(linker reordered the asset TU?)\n", name, got, expect);
        exit(1);
    }
}

/* copy `size` bytes of compiled u32 words as the byte-exact N64 image */
static void portAnimCopySwapped(unsigned char *dst, const uint32_t *src, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size / 4; i++) {
        uint32_t w = src[i];
        dst[i * 4 + 0] = (unsigned char) (w >> 24);
        dst[i * 4 + 1] = (unsigned char) (w >> 16);
        dst[i * 4 + 2] = (unsigned char) (w >> 8);
        dst[i * 4 + 3] = (unsigned char) w;
    }
}

/* processed-byte map for the data segment: headers may be listed twice and
 * descriptor tables may be shared; converting twice would corrupt them */
static unsigned char s_AnimDone[ANIM_DATA_SEG_SIZE];

/* 0 = untouched, 1 = fully done, -1 = partial overlap (a data bug) */
static int portAnimRangeState(uint32_t start, uint32_t end)
{
    uint32_t i, done = 0;
    for (i = start; i < end; i++) {
        done += s_AnimDone[i];
    }
    if (done == 0) return 0;
    if (done == end - start) return 1;
    return -1;
}

static void portAnimMarkRange(uint32_t start, uint32_t end)
{
    memset(s_AnimDone + start, 1, end - start);
}

static void portAnimNative32(unsigned char *p)
{
    uint32_t v = be32(p);
    memcpy(p, &v, 4);
}

static void portAnimNative16(unsigned char *p)
{
    uint16_t v = be16(p);
    memcpy(p, &v, 2);
}

static void portAnimConvertEntry(uint32_t offset)
{
    unsigned char *seg = _animation_dataSegmentRomStart;
    unsigned char *hdr = seg + offset;
    uint32_t desc_start, desc_end, i;
    int state;

    if (offset + ANIM_HEADER_SIZE > ANIM_DATA_SEG_SIZE) {
        portConLogf("ge: animdata entry offset 0x%x out of segment\n", offset);
        exit(1);
    }

    state = portAnimRangeState(offset, offset + ANIM_HEADER_SIZE);
    if (state == 1) return; /* duplicate table entry */
    if (state == -1) {
        portConLogf("ge: animdata header overlap at 0x%x\n", offset);
        exit(1);
    }

    /* descriptor extents while the words are still big-endian */
    desc_start = be32(hdr + 0x08);
    desc_end   = be32(hdr + 0x10);

    /* ModelAnimation (bondtypes.h): +0 s32 address; +4 u16; +6/+7 u8;
     * +8 ptr; +C/+E u16; +10 ptr (unk14+ never stored - see ANIM_HEADER_SIZE) */
    portAnimNative32(hdr + 0x00);
    portAnimNative16(hdr + 0x04);
    portAnimNative32(hdr + 0x08);
    portAnimNative16(hdr + 0x0C);
    portAnimNative16(hdr + 0x0E);
    portAnimNative32(hdr + 0x10);
    portAnimMarkRange(offset, offset + ANIM_HEADER_SIZE);

    /* root-motion descriptor table [bitDescriptors, bitStream): stride-6
     * ModelAnimBitField {u16 bitOffset; u8 bitCount; u8 pad; u16 valueOffset} */
    if (desc_start > desc_end || desc_end > ANIM_DATA_SEG_SIZE ||
        (desc_end - desc_start) % 6 != 0) {
        portConLogf("ge: animdata entry 0x%x: bad descriptor range 0x%x..0x%x\n",
                offset, desc_start, desc_end);
        exit(1);
    }
    state = portAnimRangeState(desc_start, desc_end);
    if (state == 1) return; /* shared table, already converted */
    if (state == -1) {
        portConLogf("ge: animdata descriptor overlap at 0x%x..0x%x\n", desc_start, desc_end);
        exit(1);
    }
    for (i = desc_start; i < desc_end; i += 6) {
        portAnimNative16(seg + i);
        portAnimNative16(seg + i + 4);
    }
    portAnimMarkRange(desc_start, desc_end);
}

/* the byte-exact N64 images from the compiled arrays (the pre-ROM path,
 * and the probe reference while the arrays are still linked) */
#ifdef GE_ROMDATA_FALLBACK
static void portAnimBuildCompiled(void)
{
    portAnimProbe(ANIM_DATA_empty, ANIM_DATA_idle, 0x1C, "ANIM_DATA_idle");
    portAnimProbe(ANIM_DATA_empty, ANIM_DATA_bond_eye_walk, 0x4144, "ANIM_DATA_bond_eye_walk");
    portAnimProbe(ANIM_DATA_empty, ANIM_DATA_helicopter_takeoff, 0xE7C0, "ANIM_DATA_helicopter_takeoff");
    portAnimProbe(ANIM_ENTRY_idle, ANIM_ENTRY_fire_standing, 0x2b4c, "ANIM_ENTRY_fire_standing");
    portAnimProbe(ANIM_ENTRY_idle, ANIM_ENTRY_helicopter_takeoff, 0x169bd0, "ANIM_ENTRY_helicopter_takeoff");

    portAnimCopySwapped(_animation_dataSegmentRomStart, ANIM_DATA_empty, ANIM_DATA_SEG_SIZE);
    /* NOTE: the compiled entries blob may end slightly before the full ROM
     * span (last entry tail); the copy then reads adjacent .data, which is
     * harmless - no animation addresses past the last real stream. */
    if (getenv("GE_ANIM_ZERO") == NULL) {
        /* GE_ANIM_ZERO=1: bring-up A/B switch - leave the frame streams
         * zeroed (pre-anim-wave behavior) to isolate whether a rendering
         * bug lives in the frame decode or elsewhere. */
        portAnimCopySwapped(_animation_entriesSegmentRomStart, ANIM_ENTRY_idle, ANIM_ENTRIES_SEG_SIZE);
    }
}

/* ROM vs compiled, before the switch: where the first difference is, if any */
static void portAnimProbeRom(const char *name, const unsigned char *built, const unsigned char *rom, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size && built[i] == rom[i]; i++) {
    }
    if (i == size) {
        portConLogf("ge: romdata: seg %-18s ROM bytes identical to the compiled-in copy (%u)\n", name, size);
    } else {
        portConLogf("ge: romdata: seg %-18s ROM DIFFERS from the compiled-in copy at byte %u of %u (a mod - expected - or a relocated segment)\n",
                name, i, size);
    }
}
#endif

void portAnimDataInit(void)
{
    intptr_t *tables[2];
    int t;
    u32 datasize = 0, entsize = 0;
    const u8 *romdata = romdataSeg("animation_data", &datasize);
    const u8 *roment = romdataSeg("animation_entries", &entsize);

    /* Both segments are byte-exact ROM spans (as on N64), and the header and
     * descriptor conversion below is the same either way; the compiled arrays
     * remain only for GE_ROMDATA_FALLBACK builds and the probe comparison. */
    if (romdata != NULL && roment != NULL && datasize == ANIM_DATA_SEG_SIZE && entsize == ANIM_ENTRIES_SEG_SIZE) {
#ifdef GE_ROMDATA_FALLBACK
        portAnimBuildCompiled();
        portAnimProbeRom("animation_data", _animation_dataSegmentRomStart, romdata, ANIM_DATA_SEG_SIZE);
        portAnimProbeRom("animation_entries", _animation_entriesSegmentRomStart, roment, ANIM_ENTRIES_SEG_SIZE);
#else
        portConLogf("ge: romdata: seg animation_data     -> ROM (%u bytes)\n", datasize);
        portConLogf("ge: romdata: seg animation_entries  -> ROM (%u bytes)\n", entsize);
#endif
        memcpy(_animation_dataSegmentRomStart, romdata, ANIM_DATA_SEG_SIZE);
        memcpy(_animation_entriesSegmentRomStart, roment, ANIM_ENTRIES_SEG_SIZE);
    } else {
#ifdef GE_ROMDATA_FALLBACK
        if (romdata != NULL || roment != NULL) {
            portConLogf("ge: romdata: animation segments unusable (sizes %u/%u) - compiled-in copies\n", datasize, entsize);
        }
        portAnimBuildCompiled();
#else
        portConLogf("ge: romdata: no animation segments in the ROM and no compiled-in copies (GE_ROMDATA_FALLBACK=OFF) - exiting\n");
        exit(3);
#endif
    }

    tables[0] = animation_table_ptrs1;
    tables[1] = animation_table_ptrs2;
    for (t = 0; t < 2; t++) {
        intptr_t *p;
        for (p = tables[t]; *p != 0; p++) {
            if (*p != 1) {
                portAnimConvertEntry((uint32_t) *p);
            }
        }
    }
}
