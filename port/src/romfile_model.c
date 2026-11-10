/**
 * romfile_model.c - converts model files loaded from the ROM into the host layout.
 *
 * A model file (C<name>Z / G<name>Z / P<name>Z) comes off the ROM as the N64
 * memory image: big-endian, with internal references as 0x05000000-based
 * offsets and display lists in big-endian words. This pass byte-swaps every
 * typed field IN PLACE (per the field map below) so it matches the compiled
 * asset's host layout. After that the game's normal N64 promote step
 * (sub_GAME_7F075A90 with vma=0x05000000) turns the offsets into pointers, and
 * the existing G_SETTEX / collision fixups run unchanged.
 *
 * The field map (offset and width per record type) was derived from data and
 * checked against all compiled models by scripts/port/model_probe.py, which is
 * the regression test. A vertex (render or collision) is 6 halfwords plus 4 raw
 * bytes. The spec is docs/port/romdata.md section 12.
 *
 * The header counts (numSwitches/numtextures) are NOT in the file; they come
 * from the compiled MODELFILEHEADER that load_object_fill_header passes in. The
 * file layout is:
 *   [Switches: numSwitches u32][Textures: numtextures x 12]
 *   [node tree + records + Vertex arrays + POINT_USAGE + Gfx DLs]
 *
 * In GE_ROMDATA_FALLBACK builds the converted image is memcmp'd against the
 * compiled copy: a stock ROM should read "identical", a mod reads "DIFFERS".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include "bondtypes.h"
#include "bondconstants.h"

/* port_console.c: same output as the fprintf(stderr, ...) this replaced,
 * plus a silent copy in the tilde console's history. */
extern void portConLogf(const char *fmt, ...);

/* one field to byteswap: offset within the record, width 4/2/1 */
struct mfld { u16 off; u8 w; };

#define F4(o) { (o), 4 }
#define F2(o) { (o), 2 }
/* a run of n W4 words starting at off - expanded inline where needed */

/* per-opcode rodata field maps (docs/port/romdata.md section 12, probe-verified).
 * pointer fields are ordinary W4 here - promote rebases them afterward. */
static const struct mfld fHeader[]  = { F2(0), F2(2), F4(4), F2(8), F2(0xa), F2(0xc), F2(0xe) };
static const struct mfld fGroup[]   = { F4(0), F4(4), F4(8), F2(0xc), F2(0xe), F2(0x10), F2(0x12), F4(0x14), F4(0x18) };
static const struct mfld fDL[]      = { F4(0), F4(4), F4(8), F4(0xc), F2(0x10) };  /* Type/ModelType bytes stay */
static const struct mfld fDLCol[]   = { F4(0), F4(4), F4(8), F2(0xc), F2(0xe), F4(0x10), F4(0x14), F2(0x18), F2(0x1a), F4(0x1c) };
static const struct mfld fDLPrim[]  = { F4(0), F4(4), F4(8), F4(0xc) };
static const struct mfld fLOD[]     = { F4(0), F4(4), F4(8), F2(0xc), F2(0xe) };
static const struct mfld fBSP[]     = { F4(0), F4(4), F4(8), F4(0xc), F4(0x10), F4(0x14), F4(0x18), F4(0x1c), F2(0x20), F2(0x22) };
static const struct mfld fBBox[]    = { F4(0), F4(4), F4(8), F4(0xc), F4(0x10), F4(0x14), F4(0x18) };  /* ModelNumber + bbox 6xf32 */
static const struct mfld fGunfire[] = { F4(0), F4(4), F4(8), F4(0xc), F4(0x10), F4(0x14), F4(0x18), F4(0x1c), F2(0x20), F2(0x22), F4(0x24) };
static const struct mfld fShadow[]  = { F4(0), F4(4), F4(8), F4(0xc), F4(0x10), F4(0x14), F4(0x18), F4(0x1c) };
static const struct mfld fOp17[]    = { F4(0), F4(4), F4(8), F4(0xc), F4(0x10), F4(0x14), F4(0x18), F4(0x1c) };
static const struct mfld fSwitch[]  = { F4(0), F2(4), F2(6) };
static const struct mfld fOp06[]    = { F4(0), F4(4), F4(8), F4(0xc), F4(0x10), F4(0x14) };
/* GROUPSIMPLE: coord3d Origin(3xf32) + s16 Group1 + u16 Group2 + f32 radius */
static const struct mfld fGroupSimple[] = { F4(0), F4(4), F4(8), F2(0xc), F2(0xe), F4(0x10) };
/* HEAD placeholder: u16 RwDataIndex (+2 pad) */
static const struct mfld fHeadPh[]  = { F2(0) };
/* INTERLINK: coord3d pos + coord3d pos2 + f32 Scale = 7 x f32 */
static const struct mfld fInterlink[] = { F4(0), F4(4), F4(8), F4(0xc), F4(0x10), F4(0x14), F4(0x18) };

static void swapFields(u8 *p, const struct mfld *f, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        u8 *w = p + f[i].off;
        if (f[i].w == 4) {
            u8 t = w[0]; w[0] = w[3]; w[3] = t;
            t = w[1]; w[1] = w[2]; w[2] = t;
        } else if (f[i].w == 2) {
            u8 t = w[0]; w[0] = w[1]; w[1] = t;
        }
    }
}

#define NF(a) ((int) (sizeof(a) / sizeof((a)[0])))

static void swap4run(u8 *p, u32 words)
{
    while (words--) {
        u8 t = p[0]; p[0] = p[3]; p[3] = t;
        t = p[1]; p[1] = p[2]; p[2] = t;
        p += 4;
    }
}

static u32 be32(const u8 *p)
{
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | (u32) p[3];
}

static u16 be16(const u8 *p)
{
    return (u16) (((u16) p[0] << 8) | p[1]);
}

/* segment-5 offset field -> file offset, or (u32)-1 if not a 0x05 ref/null */
static u32 segoff(const u8 *p)
{
    u32 v = be32(p);
    if (v == 0) {
        return 0xFFFFFFFFu;
    }
    return (v >> 24) == 0x05 ? (v & 0x00FFFFFFu) : 0xFFFFFFFFu;
}

struct mconv {
    u8 *d;
    u32 size;
    u8 *done;          /* byte map: 1 = already converted (dedup shared regions) */
    const char *name;
    u32 root;          /* rootoff: no node/data reference is ever below this */
    u32 nnodes, nverts, ndls;
    int bad;
};

static int mark(struct mconv *c, u32 off, u32 len)
{
    u32 i;
    if (off + len > c->size) {
        c->bad++;
        return 0;
    }
    for (i = off; i < off + len; i++) {
        if (c->done[i]) {
            return 0; /* already converted - skip */
        }
    }
    for (i = off; i < off + len; i++) {
        c->done[i] = 1;
    }
    return 1;
}

/* one Vertex (16B): 6 halfwords then 4 raw bytes - render s/t and collision
 * LinkedTo(.s/.t) both swap this way; the port fixup handles collision after */
static void swapVertex(u8 *p)
{
    int i;
    for (i = 0; i < 12; i += 2) {
        u8 t = p[i]; p[i] = p[i + 1]; p[i + 1] = t;
    }
}

static void swapVertices(struct mconv *c, u32 off, u32 count)
{
    u32 i;
    if (off == 0xFFFFFFFFu || count == 0 || count > 0x8000) {
        return;
    }
    if (!mark(c, off, count * 16)) {
        return;
    }
    for (i = 0; i < count; i++) {
        swapVertex(c->d + off + i * 16);
    }
    c->nverts += count;
}

static void swapPointUsage(struct mconv *c, u32 off, u32 count)
{
    u32 i;
    if (off == 0xFFFFFFFFu || count == 0 || count > 0x8000) {
        return;
    }
    if (!mark(c, off, count * 2)) {
        return;
    }
    for (i = 0; i < count; i++) {
        u8 *p = c->d + off + i * 2;
        u8 t = p[0]; p[0] = p[1]; p[1] = t;
    }
}

/* Swap a display list's 8-byte commands up to the end command (0xB8 or 0xDF),
 * recursing into gsSPDisplayList (0xDE/0x06) sub-lists, which must be swapped
 * too or the game crashes. Segment-5 vertex/texture references stay as offsets
 * for fast3d to resolve; recursion is shallow and deduped via `done`. */
static void swapDL(struct mconv *c, u32 off, int depth)
{
    u32 o = off;
    u32 guard = 0;
    if (off == 0xFFFFFFFFu || depth > 32) {
        return;
    }
    for (;;) {
        u8 op;
        u32 w1;
        if (o + 8 > c->size || guard++ > 0x8000) {
            c->bad++;
            return;
        }
        if (!mark(c, o, 8)) {
            return; /* shared/already converted */
        }
        op = c->d[o];              /* w0 high byte, still BE = the opcode */
        w1 = be32(c->d + o + 4);    /* the DL/segment operand, still BE */
        swap4run(c->d + o, 2);
        o += 8;
        if (op == 0xDE || op == 0x06) {
            /* gsSPDisplayList: recurse into the branched list */
            if ((w1 >> 24) == 0x05) {
                swapDL(c, w1 & 0x00FFFFFFu, depth + 1);
            }
            if (op == 0xDE) {
                /* 0xDE with the no-push bit set is a branch (list ends);
                 * without, a call (continues). GE uses 0xB8 to end, so keep
                 * walking - a stray branch is bounded by ENDDL/guard. */
            }
        }
        if (op == 0xB8 || op == 0xDF) {
            break;
        }
    }
    c->ndls++;
}

/* swap one node's rodata by opcode, and any vertex/DL/pointusage it owns.
 * counts are read AFTER swapping the scalar that holds them. */
static void convNode(struct mconv *c, u32 nodeoff, u32 dataoff, u32 opcode)
{
    u8 *rd;
    if (dataoff == 0xFFFFFFFFu || dataoff >= c->size) {
        return;
    }
    rd = c->d + dataoff;

    switch (opcode) {
    case MODELNODE_OPCODE_HEADER:
    case MODELNODE_OPCODE_OP20:
        if (mark(c, dataoff, 16)) swapFields(rd, fHeader, NF(fHeader));
        break;
    case MODELNODE_OPCODE_GROUP:
    case MODELNODE_OPCODE_OP03:
    case MODELNODE_OPCODE_OP17:
        if (mark(c, dataoff, 28)) swapFields(rd, fGroup, NF(fGroup));
        break;
    case MODELNODE_OPCODE_DL: {
        /* read the offsets/counts while the rodata is STILL big-endian, THEN
         * swap the fields (swapping first makes segoff/be16 read native bytes and return null) */
        u32 pri = segoff(rd + 0x0), sec = segoff(rd + 0x4);
        u32 vtx = segoff(rd + 0xc), cnt = be16(rd + 0x10);
        if (mark(c, dataoff, 20)) swapFields(rd, fDL, NF(fDL));
        swapDL(c, pri, 0);
        swapDL(c, sec, 0);
        swapVertices(c, vtx, cnt);
        break;
    }
    case MODELNODE_OPCODE_DLCOLLISION: {
        u32 pri = segoff(rd + 0x0), sec = segoff(rd + 0x4);
        u32 vtx = segoff(rd + 0x8), cvtx = segoff(rd + 0x10), pu = segoff(rd + 0x14);
        u32 nv = be16(rd + 0xc), ncv = be16(rd + 0xe);
        if (mark(c, dataoff, 32)) swapFields(rd, fDLCol, NF(fDLCol));
        swapDL(c, pri, 0);
        swapDL(c, sec, 0);
        swapVertices(c, vtx, nv);
        swapVertices(c, cvtx, ncv);
        swapPointUsage(c, pu, nv);
        break;
    }
    case MODELNODE_OPCODE_DLPRIMARY: {
        u32 cnt = be32(rd + 0x0), vtx = segoff(rd + 0x4), pri = segoff(rd + 0x8);
        if (mark(c, dataoff, 16)) swapFields(rd, fDLPrim, NF(fDLPrim));
        swapDL(c, pri, 0);
        /* numVertices counts QUADS: dorottex (model.c star gunfire) reads
         * numVertices * 4 vertices, so swap all of them (else muzzle flashes break). */
        swapVertices(c, vtx, cnt * 4);
        break;
    }
    case MODELNODE_OPCODE_LOD:
        if (mark(c, dataoff, 16)) swapFields(rd, fLOD, NF(fLOD));
        break;
    case MODELNODE_OPCODE_BSP:
        if (mark(c, dataoff, 36)) swapFields(rd, fBSP, NF(fBSP));
        break;
    case MODELNODE_OPCODE_BBOX:
        if (mark(c, dataoff, 28)) swapFields(rd, fBBox, NF(fBBox));
        break;
    case MODELNODE_OPCODE_GUNFIRE:
        if (mark(c, dataoff, 40)) swapFields(rd, fGunfire, NF(fGunfire));
        break;
    case MODELNODE_OPCODE_SHADOW:
        if (mark(c, dataoff, 32)) swapFields(rd, fShadow, NF(fShadow));
        break;
    case MODELNODE_OPCODE_SWITCH:
        if (mark(c, dataoff, 8)) swapFields(rd, fSwitch, NF(fSwitch));
        break;
    case MODELNODE_OPCODE_OP06:
        if (mark(c, dataoff, 24)) swapFields(rd, fOp06, NF(fOp06));
        break;
    case MODELNODE_OPCODE_GROUPSIMPLE:
        if (mark(c, dataoff, 20)) swapFields(rd, fGroupSimple, NF(fGroupSimple));
        break;
    case MODELNODE_OPCODE_HEAD:
        if (mark(c, dataoff, 4)) swapFields(rd, fHeadPh, NF(fHeadPh));
        break;
    case MODELNODE_OPCODE_INTERLINK:
        if (mark(c, dataoff, 28)) swapFields(rd, fInterlink, NF(fInterlink));
        break;
    case MODELNODE_OPCODE_OP11:
        if (mark(c, dataoff, 76)) swap4run(rd, 19);
        break;
    case MODELNODE_OPCODE_OP05:
        if (mark(c, dataoff, 424)) swap4run(rd, 424 / 4);
        break;
    case MODELNODE_OPCODE_OP07:
        if (mark(c, dataoff, 432)) { swap4run(rd, 6); swap4run(rd + 0x18, 100); swapFields(rd + 0x1a8, (const struct mfld[]){ F2(0), F2(2), F4(4) }, 3); }
        break;
    default:
        break; /* NULL / OP14 / OP16 / OP19 carry no swept rodata fields */
    }
    (void) nodeoff;
}

/* the tree walk: iterative over an explicit offset stack, deduped by c->done
 * on the node's own 24 bytes. mirrors modelPromoteNodeOffsetsToPointers'
 * reachability (Child / Next, with LOD->Affects and SWITCH->Controls). */
#define MSTACK 4096
/* The walker reads some records before convNode can validate them. */
static u32 modelRecordSize(u32 opcode)
{
    switch (opcode) {
    case MODELNODE_OPCODE_HEADER: case MODELNODE_OPCODE_OP20: return 16;
    case MODELNODE_OPCODE_GROUP: case MODELNODE_OPCODE_OP03: case MODELNODE_OPCODE_OP17: return 28;
    case MODELNODE_OPCODE_DL: return 20;
    case MODELNODE_OPCODE_DLCOLLISION: return 32;
    case MODELNODE_OPCODE_DLPRIMARY: case MODELNODE_OPCODE_LOD: return 16;
    case MODELNODE_OPCODE_BSP: return 36;
    case MODELNODE_OPCODE_BBOX: case MODELNODE_OPCODE_INTERLINK: return 28;
    case MODELNODE_OPCODE_GUNFIRE: return 40;
    case MODELNODE_OPCODE_SHADOW: return 32;
    case MODELNODE_OPCODE_SWITCH: return 8;
    case MODELNODE_OPCODE_OP06: return 24;
    case MODELNODE_OPCODE_GROUPSIMPLE: return 20;
    case MODELNODE_OPCODE_HEAD: return 4;
    case MODELNODE_OPCODE_OP11: return 76;
    case MODELNODE_OPCODE_OP05: return 424;
    case MODELNODE_OPCODE_OP07: return 432;
    default: return 0;
    }
}

static void walk(struct mconv *c, u32 root)
{
    u32 stack[MSTACK];
    int sp = 0;
    stack[sp++] = root;
    while (sp > 0) {
        u32 off = stack[--sp];
        u32 opcode, dataoff, child, nxt;
        if (off == 0xFFFFFFFFu) {
            continue;
        }
        /* A node reference below rootoff means the compiled header's
         * switch/texture counts don't match this file (a mod changed them).
         * Flag it so the caller uses the compiled copy instead of a mis-parsed
         * graph; a mod must never be fatal. */
        if (off < c->root) {
            c->bad++;
            continue;
        }
        if (off > c->size || 24 > c->size - off) {
            c->bad++;
            continue;
        }
        if (c->done[off]) {
            continue;
        }
        opcode = be16(c->d + off) & 0xff;
        dataoff = segoff(c->d + off + 0x4);   /* Data */
        nxt = segoff(c->d + off + 0xc);       /* Next */
        child = segoff(c->d + off + 0x14);    /* Child */
        if (dataoff != 0xFFFFFFFFu &&
                (dataoff > c->size || modelRecordSize(opcode) > c->size - dataoff)) {
            c->bad++;
            continue;
        }
        /* LOD/SWITCH descend through the rodata target, not Child */
        if (opcode == MODELNODE_OPCODE_LOD && dataoff != 0xFFFFFFFFu) {
            child = segoff(c->d + dataoff + 0x8);   /* Affects */
        } else if (opcode == MODELNODE_OPCODE_SWITCH && dataoff != 0xFFFFFFFFu) {
            child = segoff(c->d + dataoff + 0x0);   /* Controls */
        } else if (opcode == MODELNODE_OPCODE_BSP && dataoff != 0xFFFFFFFFu) {
            /* two children */
            if (sp < MSTACK) stack[sp++] = segoff(c->d + dataoff + 0x18);
            if (sp < MSTACK) stack[sp++] = segoff(c->d + dataoff + 0x1c);
        }
        /* convert this node's rodata + owned data BEFORE swapping the node
         * pointers (dataoff etc. are read as raw offsets above) */
        convNode(c, off, dataoff, opcode);
        /* swap the ModelNode itself: Opcode u16, pad u16, 5 pointer words */
        c->done[off] = 1;
        {
            u8 *p = c->d + off;
            u8 t = p[0]; p[0] = p[1]; p[1] = t;   /* Opcode */
            t = p[2]; p[2] = p[3]; p[3] = t;       /* pad */
            swap4run(p + 4, 5);                    /* Data,Parent,Next,Prev,Child */
        }
        c->nnodes++;
        if (sp < MSTACK) stack[sp++] = nxt;
        if (sp < MSTACK) stack[sp++] = child;
    }
}

s32 romfileModelConvert(u8 *data, u32 size, s32 numSwitches, s32 numtextures, const char *name, const u8 *compiled)
{
    struct mconv c;
    u32 rootoff, i;

    if (numSwitches < 0 || numtextures < 0) {
        return 0;
    }
    rootoff = (u32) numSwitches * 4 + (u32) numtextures * 12;
    if (rootoff + 24 > size) {
        portConLogf("ge: romdata: model %s: root 0x%X past the %u-byte file\n", name, rootoff, size);
        return 0;
    }
    memset(&c, 0, sizeof(c));
    c.d = data;
    c.size = size;
    c.name = name;
    c.root = rootoff;
    c.done = (u8 *) calloc(1, size);
    if (c.done == NULL) {
        return 0;
    }

    /* Switches[]: node offsets */
    for (i = 0; i < (u32) numSwitches; i++) {
        if (mark(&c, i * 4, 4)) swap4run(data + i * 4, 1);
    }
    /* Textures[]: TextureID u32 + 7 flag bytes + pad */
    for (i = 0; i < (u32) numtextures; i++) {
        u32 o = (u32) numSwitches * 4 + i * 12;
        if (mark(&c, o, 12)) swap4run(data + o, 1);
    }
    walk(&c, rootoff);

    free(c.done);

    if (compiled != NULL) {
        u32 diff = 0, first = 0;
        /* The compiled copy's pointers are relocated to real addresses while
         * the converted ROM still holds 0x05000000+offset, so a 0x05xxxxxx word
         * matching (compiled - base), or identical in both, is not a
         * difference. Any other mismatch is a real difference (a mod);
         * model_probe.py is the byte-exact regression test. */
        u32 base = (u32) (uintptr_t) compiled;
        /* The decompressed ROM file has trailing zero padding the compiled copy
         * lacks, so trim trailing all-zero words before comparing to avoid
         * reading past the compiled blob. They are never model content (an
         * ENDDL's zero word follows a non-zero opcode word). */
        u32 cmpEnd = size;
        while (cmpEnd >= 4 && data[cmpEnd - 4] == 0 && data[cmpEnd - 3] == 0 &&
               data[cmpEnd - 2] == 0 && data[cmpEnd - 1] == 0) {
            cmpEnd -= 4;
        }
        for (i = 0; i + 4 <= cmpEnd; ) {
            u32 a = ((u32) data[i] << 24) | ((u32) data[i+1] << 16) | ((u32) data[i+2] << 8) | data[i+3];
            u32 b = ((u32) compiled[i] << 24) | ((u32) compiled[i+1] << 16) | ((u32) compiled[i+2] << 8) | compiled[i+3];
            if (a != b) {
                /* little-endian words: read them LE for the pointer test */
                u32 la = data[i] | (data[i+1] << 8) | (data[i+2] << 16) | ((u32) data[i+3] << 24);
                u32 lb = compiled[i] | (compiled[i+1] << 8) | (compiled[i+2] << 16) | ((u32) compiled[i+3] << 24);
                if ((la & 0xFF000000u) == 0x05000000u &&
                    ((la & 0x00FFFFFFu) == lb || (la & 0x00FFFFFFu) == (lb - base))) {
                    i += 4;
                    continue; /* correct segment pointer (bare or relocated) */
                }
                if (diff == 0) first = i;
                diff++;
            }
            i += 4;
        }
        if (diff == 0) {
            portConLogf("ge: romdata: model %-16s identical to the compiled-in object (%u nodes, %u verts, %u DLs)\n",
                    name, c.nnodes, c.nverts, c.ndls);
        } else {
            portConLogf("ge: romdata: model %-16s DIFFERS: %u bytes (first 0x%X) [nSw=%d nTex=%d size=%u nodes=%u verts=%u dls=%u] (a mod, or a converter bug: model_probe.py)\n",
                    name, diff, first, numSwitches, numtextures, size, c.nnodes, c.nverts, c.ndls);
        }
    } else {
        portConLogf("ge: romdata: model %-16s converted (%u nodes, %u verts, %u DLs)%s\n",
                name, c.nnodes, c.nverts, c.ndls, c.bad ? " - BOUNDS ISSUES" : "");
    }
    /* Catch a mod whose header counts differ from the compiled one, which puts
     * rootoff in the wrong place: the root opcode must match the compiled
     * model's and some geometry must exist. On failure use the compiled copy
     * instead, since a bad mod must never be fatal. */
    if (compiled != NULL &&
        (data[c.root] != compiled[c.root] || data[c.root + 1] != compiled[c.root + 1])) {
        portConLogf("ge: romdata: model %-16s root opcode differs from the compiled header - serving the compiled copy (mod count mismatch?)\n", name);
        return 0;
    }
    if (c.nverts == 0 && c.ndls == 0) {
        portConLogf("ge: romdata: model %-16s has no geometry after convert - serving the compiled copy (mod header/count mismatch?)\n", name);
        return 0;
    }
    return c.bad == 0;
}
