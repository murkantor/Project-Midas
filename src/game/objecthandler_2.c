#include <ultra64.h>
#ifndef TARGET_N64
#include <stdio.h>
#endif
#include "chrobjdata.h"
#include "image.h"
#include "math_asinfacosf.h"
#include "math_ceil.h"
#include "math_floor.h"
#include "math_unk_05A9E0.h"
#include "model.h"
#include "ob.h"
#include "objecthandler.h"
#include "quaternion.h"
#include "tex.h"
#ifdef GE_NATIVE_64
#include "romfile_model64.h"
#endif

#ifndef TARGET_N64
#include <stdlib.h>

/**
 * PC arm of the model texture pass: convert ONE node display list.
 *
 * On N64 the loop below rewrites every node DL in the file's slack space,
 * expanding GE's G_SETTEX (0xC0) into real SETTIMG/SETTILE/LOADBLOCK sequences
 * via texLoadFromGdl. The renderer treats a raw G_SETTEX as a NOOP, so an
 * unconverted model draws with no texture or material state (all black).
 * Compiled-in models have no slack space, so each converted DL gets its own
 * allocation instead.
 *
 * DL length = scan to gSPEndDisplayList inclusive (node DLs are flat; embedded
 * gSPDisplayList words are copied through, as on N64). Known cost:
 * reloading a model reconverts an already-clean DL and leaks the previous
 * allocation.
 */
static Gfx *portConvertModelGdl(Gfx *src, struct texpool *pool)
{
    s32 n = 0;
    Gfx *dst;

    while ((u8)(src[n].words.w0 >> 24) != (u8)G_ENDDL)
    {
        n++;
        if (n > 0x4000)
        {
            return NULL; /* no terminator - leave the node untouched */
        }
    }
    n++; /* include the ENDDL */

    /* texLoadFromGdl expands each G_SETTEX into a bounded burst, so 4x plus
     * slack covers the worst case. Use portAllocHigh because these buffers
     * become G_DL targets, and a pointer below 0x10000000 would be misread as a
     * segment address. */
    {
        extern void *portAllocHigh(u32 size);
        dst = portAllocHigh((n * 4 + 96) * sizeof(Gfx));
    }
    if (dst == NULL)
    {
        return NULL;
    }

    texLoadFromGdl(src, n * (s32)sizeof(Gfx), dst, pool);
    return dst;
}

/* Persistent texture pool for compiled-in model files, whose texture loads and
 * G_SETTEX expansions run once per file and bake data pointers in permanently.
 * This arena must therefore never be recycled like the callers' stage/menu
 * scratch pools (see portModelTexPool). */
static struct texpool s_ModelPool;
static u8 *s_ModelArena;

struct texpool *portModelTexPool(void)
{
    if (s_ModelArena == NULL)
    {
        extern void *portAllocHigh(u32 size);
        s_ModelArena = portAllocHigh(0x400000); /* pool pointers bake into DLs */
        texInitPool(&s_ModelPool, s_ModelArena, 0x400000);
    }
    return &s_ModelPool;
}

/* NOTE: do not re-add per-stage scoping (a stage watermark) to these lookups;
 * it was tried and reverted because it regressed shadow corruption on Bunker 1
 * -> Caverns -> Bunker 1. Evidence points to one texture number resolving
 * through two different pools depending on the caller, so settle that first. */
#endif


/***
 * Perfect Dark:
 * void modeldef0f1a7560(struct modeldef *modeldef, u16 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5)
 *
 * NTSC address 0x7F0762E0.
*/
void sub_GAME_7F0762E0(ModelFileHeader *objheader, u8 *name, u8 *dst, struct texpool *buffer)
{
    ModelNode *node;
    s32 romremaining;
    Gfx *gdl;
    s32 pcremaining;
    u32 replacementgdl;
    ModelNode *curnode;
    Gfx *curgdl;
    s32 delta;
    s32 filedata;
    s32 filenum;

#ifndef TARGET_N64
    /* Compiled-in models already hold real host pointers, so skip the N64
     * segment-offset rebase and instead load textures into a persistent pool
     * (caller scratch pools get reused and left baked pointers dangling,
     * turning repeat weapons black) and convert each node's G_SETTEX into its
     * own allocation. The iterator cursor MUST advance before the node is
     * repointed, because the Secondary lookup compares against the
     * pre-replacement pointer. */
    texLoadFromModelFileHeader(objheader, portModelTexPool());
    buffer = portModelTexPool();

    node = NULL;
    gdl = NULL;
    modelIterateDisplayLists(objheader, &node, &gdl);

    while (node != NULL && gdl != NULL)
    {
        Gfx *replacement;

        curnode = node;
        curgdl = gdl;
        modelIterateDisplayLists(objheader, &node, &gdl);

        replacement = portConvertModelGdl(curgdl, buffer);
        if (replacement != NULL)
        {
            modelNodeReplaceGdl((uintptr_t) objheader, curnode, curgdl, replacement);
        }
    }
    return;
#endif

    filedata = (intptr_t) objheader->Switches;
    filenum = fileGetIndex((char *) name);

    romremaining = get_rom_remaining_buffer_for_index(filenum);
    pcremaining = get_pc_remaining_buffer_for_index(filenum);
    node = 0;
    modelIterateDisplayLists(objheader, &node, &gdl);

    if (gdl != 0)
    {
        name = (u8 *) ((pcremaining - ((intptr_t) (((u8 *) objheader->Switches) + (((uintptr_t) gdl) & 0x00ffffff)))) + ((s32) filedata));
        
        /* The signed lvalue cast is required for the compiler to choose the target registers. */
        replacementgdl = (u32)*(s32 *)&gdl;
        
        delta = ((s32) ((romremaining + filedata) - (intptr_t) name)) - ((intptr_t) (((u8 *) objheader->Switches) + (((uintptr_t) gdl) & 0x00ffffff)));
        
        texCopyGdls((Gfx *) (((u8 *) objheader->Switches) + (((uintptr_t) gdl) & 0x00ffffff)), (Gfx *) ((romremaining + filedata) - (intptr_t) name), (intptr_t) name);

        texLoadFromModelFileHeader(objheader, buffer);

        if (node != 0)
        {
            do
            {
                curnode = node;
                curgdl = gdl;
                modelIterateDisplayLists(objheader, &node, &gdl);
                
                if (gdl != 0)
                {
                    name = (u8 *) (((intptr_t) gdl) - ((intptr_t) curgdl));
                }
                else
                {
                    name = (u8 *) ((((s32) (filedata + pcremaining)) - ((intptr_t) objheader->Switches)) - (((uintptr_t) curgdl) & 0x00ffffff));
                }
                
                modelNodeReplaceGdl((uintptr_t) objheader, curnode, curgdl, (Gfx *) replacementgdl);
                
                replacementgdl += texLoadFromGdl( (Gfx *) ((((u8 *) objheader->Switches) + (((uintptr_t) curgdl) & 0x00ffffff)) + delta), (intptr_t) name, (Gfx *) (((u8 *) objheader->Switches) + (replacementgdl & 0x00ffffff)), buffer);
            } 
            while (node != 0);
        }

        name = (u8 *) (((intptr_t) (((u8 *) objheader->Switches) + (replacementgdl & 0x00ffffff))) - filedata);

        fileSetSize(filenum, (u8 *) filedata, (((intptr_t) name + 0xf) & (~0xf)), dst == 0);
    }
}


#ifndef TARGET_N64
/* Three menu models (goldeneyelogo, legalpage, nintendologo) embed textures
 * inside the model file, referenced by raw gsDPSetTextureImage(0x05xxxxxx), and
 * on PC those big-endian u32 words come out byte-reversed. Swap each texture
 * back to the N64 byte image ONCE per file (a registry stops a double swap on
 * repeat menu visits); length = W*H*(4<<RenderDepth)/8. */

#define INLINE_TEX_SWAP_MAX 16

static void *s_InlineTexSwapped[INLINE_TEX_SWAP_MAX];

static void portSwapInlineTextures(struct ModelFileHeader *objheader, u8 *filedata)
{
    s32 i;
    s32 slot;

    for (slot = 0; slot < INLINE_TEX_SWAP_MAX; slot++)
    {
        if (s_InlineTexSwapped[slot] == filedata)
        {
            return;
        }
        if (s_InlineTexSwapped[slot] == NULL)
        {
            break;
        }
    }

    for (i = 0; i < objheader->numtextures; i++)
    {
        u32 id = objheader->Textures[i].TextureID;

        if ((id >> 24) == 0x05)
        {
            u32 *p = (u32 *) (filedata + (id & 0x00FFFFFFu));
            u32 depth = objheader->Textures[i].RenderDepth;
            u32 len = (((u32) objheader->Textures[i].Width *
                        objheader->Textures[i].Height) << depth) >> 1;
            u32 w;

            len = (len + 3) & ~3u;
            for (w = 0; w < len; w += 4)
            {
                u32 v = p[w >> 2];

                if (depth == 2)
                {
                    /* 16-bit texels (RGBA16/IA16): the importer reads
                     * NATIVE u16s (gfx_pc "GE texel convention"), so only
                     * the texel ORDER within each word flips */
                    p[w >> 2] = (v >> 16) | (v << 16);
                }
                else if (depth == 3)
                {
                    /* 32-bit texels: native u32s already in order */
                }
                else
                {
                    /* 4/8-bit formats are BYTE streams: restore the N64
                     * byte image with a full word swap */
                    p[w >> 2] = (v >> 24) | ((v >> 8) & 0xFF00u) |
                                ((v & 0xFF00u) << 8) | (v << 24);
                }
            }
        }
    }

    if (slot < INLINE_TEX_SWAP_MAX)
    {
        s_InlineTexSwapped[slot] = filedata;
    }
}
#endif

/***
 * NTSC addres 0x7F0764A4.
*/
void load_object_fill_header(struct ModelFileHeader *objheader, u8 *name, u8* dst, s32 size, struct texpool * buffer)
{
    void *filedata;

#ifndef TARGET_N64
    extern unsigned long long portProfNow(void);
    extern void portHitchNote(int cat, unsigned long long us);
    const unsigned long long hitchT0 = portProfNow(); /* hitchlog.c */

    /* Load-hitch accounting (frametiming.c): on N64 a model with its own pool
     * (the hands) re-inflates every texture, while the port's persistent pool
     * skips ones it already has. Charge them all from the header below and keep
     * texLoad quiet meanwhile. */
    extern void portHwLoadSuppress(s32 on);
    if (buffer != NULL)
    {
        portHwLoadSuppress(1);
    }
#endif
    if (dst != 0)
    {
        filedata = _fileNameLoadToAddr(name, 0, dst, size);
    }
    else
    {
        filedata = _fileNameLoadToBank(name, 0, 0x100, 4);
    }
    
    objheader->Switches = (struct ModelNode **)filedata;
    
    // hmmmmmmmmmmmm
    objheader->Textures = (struct ModelFileTextures *)&((s32*)filedata)[objheader->numSwitches];
    
    objheader->RootNode = (struct ModelNode *)&objheader->Textures[objheader->numtextures];
    
#ifdef TARGET_N64
    sub_GAME_7F075A90(objheader, 0x5000000, filedata);
#elif defined(GE_NATIVE_64)
    {
        s32 idx = fileGetIndex(name);
        u32 bytes = get_pc_remaining_buffer_for_index(idx);
        modelConvertN64Binary(objheader, filedata, bytes);
        fileSetSize(idx, filedata, (bytes + 15) & ~15u, dst == NULL);
    }
#else
    {
        extern s32 romdataFileServed(s32 index);
        extern const u8 *romdataFileCompiled(s32 index);
        extern s32 romfileModelConvert(u8 *data, u32 size, s32 numSwitches, s32 numtextures, const char *name, const u8 *compiled);
        extern resource_lookup_data_entry resource_lookup_data_array[];
        s32 idx = fileGetIndex(name);

        if (romdataFileServed(idx))
        {
            /* The served file is the N64 model image (big-endian, 0x05000000
             * offsets): convert it to host layout, then take the N64 promote
             * path (offsets to real pointers). The inline-texture swap,
             * collision fixup and G_SETTEX expansion below then run on the
             * converted image unchanged. */
            extern void fileSetSize(s32 filenum, u8 *ptr, u32 size, s32 reallocate);
            const u8 *comp = romdataFileCompiled(idx);
            u32 poolsz = resource_lookup_data_array[idx].poolRemaining;
            const unsigned long long convT0 = portProfNow();
            s32 converted = romfileModelConvert((u8 *) filedata, poolsz,
                                                objheader->numSwitches, objheader->numtextures,
                                                (const char *) name, comp);
            {   /* GE_MODELTIME=1: where a model load's time goes */
                static s32 mt = -1;
                if (mt < 0) { mt = getenv("GE_MODELTIME") != NULL; }
                if (mt)
                {
                    fprintf(stderr, "ge: modeltime: %-16s load %.2f ms convert %.2f ms (%u bytes)\n", (const char *) name,
                            (double) (convT0 - hitchT0) / 1000.0, (double) (portProfNow() - convT0) / 1000.0, (unsigned) poolsz);
                }
            }

            /* A served model loads with FILELOADMETHOD_EXTRAMEM, which grabs
             * all remaining STAGE memory, and the PC texture path returns
             * before the N64-only fileSetSize that would reclaim it, so STAGE
             * fills after a few models. Commit the model's real STAGE footprint
             * now (filedata is still STAGE's last allocation); compiled-in
             * files never allocated STAGE and skip this. */
            if (poolsz > 0)
            {
                fileSetSize(idx, (u8 *) filedata, (poolsz + 0xf) & ~0xfu, dst == 0);
            }

            if (converted)
            {
                sub_GAME_7F075A90(objheader, 0x5000000, (u32) filedata);
            }
            else
            {
                /* The served file did not convert cleanly (romfile_model.c
                 * printed why), so point the model at a static empty node that
                 * renders nothing rather than promoting a garbage graph. Never
                 * taken on the stock cart. */
                static const struct ModelNode s_portEmptyModelNode; /* zero = NULL opcode, no children, no rodata */
                objheader->RootNode = (struct ModelNode *) &s_portEmptyModelNode;
                fprintf(stderr, "ge: romdata: model %s could not be converted - rendering nothing\n",
                        (const char *) name);
            }
        }
        else
        {
            /* PORT: compiled-in model data already holds real pointers, so vma
             * == fileramaddr makes the promote a no-op. */
            sub_GAME_7F075A90(objheader, (s32) filedata, (u32) filedata);
        }
    }
    portSwapInlineTextures(objheader, filedata);
    /* PORT: GE_COLAUDIT=1 proves the collision-vertex byte-order fixups
     * stayed exact across reloads of this shared compiled-in blob. */
    portModelCollisionAudit(objheader->RootNode, (const char *) name);
#endif
#ifndef TARGET_N64
    const unsigned long long texT0 = portProfNow();
#endif
    sub_GAME_7F0762E0(objheader, name, dst, buffer);
#ifndef TARGET_N64
    {   /* GE_MODELTIME=1: the model's textures */
        static s32 mt = -1;
        if (mt < 0) { mt = getenv("GE_MODELTIME") != NULL; }
        if (mt)
        {
            fprintf(stderr, "ge: modeltime: %-16s textures %.2f ms (%d, own pool %d)\n", (const char *) name,
                    (double) (portProfNow() - texT0) / 1000.0, (int) objheader->numtextures, buffer != NULL);
        }
    }
    if (buffer != NULL)
    {
        extern void portHwNoteTexture(s32 texnum, u32 romBytes);
        extern u32 portTexRomBytes(s32 texnum);
        extern s32 portTickHwPace(void);
        s32 t;

        portHwLoadSuppress(0);
        for (t = 0; portTickHwPace() && t < objheader->numtextures; t++)
        {
            /* texLoad replaced TextureID with the data pointer; the texture
             * number sits 8 bytes before it (portGunTexAudit, gun.c) */
            uintptr_t id = objheader->Textures[t].TextureID;
            s32 texnum = (id >= (u32) MAX_TEXTURES) ? *(s16 *) ((u8 *) id - 8) : (s32) id;

            portHwNoteTexture(texnum, portTexRomBytes(texnum)); /* cart read + last-priced decode */
        }
    }
    portHitchNote(1 /* models */, portProfNow() - hitchT0);
#endif
}




void fileLoad(struct ModelFileHeader *header,char *name)
{
   load_object_fill_header(header,name,0,0,0);
   return;
}


void load_object_into_memory_unused_maybe(struct ModelFileHeader *header,int *recallstring,int *targetloc,int sizeleft)
{
   load_object_fill_header(header,recallstring,targetloc,sizeleft,0);
   return;
}





