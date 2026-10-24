#include <ultra64.h>
#include "deb.h"
#include "game/player.h"
#include "dyn.h"
#include <token.h>
#include <str.h>
#include <memp.h>
#include <macro.h>

#ifndef TARGET_N64
#include <stdio.h> /* GE_ROOM_AUDIT dyn-pool watch (see bg.c) */
#endif

/**
 * This file handles memory usage for graphics related tasks.
 *
 * There are two pools, "gfx" and "vtx", which are used to store different data.
 *
 * The gfx pool (g_GfxBuffers) is sized based on the stage's -mgfx
 * argument. It contains only the master display list's GBI bytecode.
 * The master gdl is passed through all rendering functions in the game engine,
 * where each appends to the display list.
 *
 * The vtx pool (g_VtxBuffers) is sized based on the stage's -mvtx argument.
 * It is used for auxiliary graphics data such as vertex arrays, matrices and
 * colours.
 *
 * Both the gfx and vtx pools are split into two buffers of equal size.
 * Only one buffer is active at a time - the other is being drawn to the screen
 * while the active one is being built. Each time a frame is finished the active
 * buffer index is swapped to the other one.
 *
 * Both the gfx and vtx pools have a third element in them, but this is just a
 * marker for the end of the second element's allocation.
 */

u8 *g_GfxBuffers[3];
u8 *g_VtxBuffers[3];
u8 *g_GfxMemPos;
u8 g_GfxActiveBufferIndex;
s32 g_GfxRequestedDisplayList;
s32 D_800482E0 = 0;
s32 g_GfxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
s32 g_VtxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};

char membars_string1[] = ">>>>>>>>>>>>>>>>>>>>>>>>>";
char membars_string2[] = "=========================";
char membars_string3[] = "-------------------------";

void dynInit(void) {
    debTryAdd(&D_800482E0, "dyn_c_debug");
}

void dynInitMemory(void) {
    if (tokenFind(1, "-mgfx")) {
        g_GfxSizesByPlayerCount[getPlayerCount() - 1] = strtol(tokenFind(1, "-mgfx"), NULL, 0) * 1024;
    }
    if (tokenFind(1, "-mvtx")) {
        g_VtxSizesByPlayerCount[getPlayerCount() - 1] = strtol(tokenFind(1, "-mvtx"), NULL, 0) * 1024;
    }

#ifndef TARGET_N64
    // Double graphics buffers size on PC to avoid graphics corruption bugs
    g_GfxSizesByPlayerCount[getPlayerCount() - 1] *= 2;
    g_VtxSizesByPlayerCount[getPlayerCount() - 1] *= 2;
#endif

    g_GfxBuffers[0] = mempAllocBytesInBank(g_GfxSizesByPlayerCount[getPlayerCount() - 1] * 2, MEMPOOL_STAGE);
    g_GfxBuffers[1] = (g_GfxBuffers[0] + g_GfxSizesByPlayerCount[getPlayerCount() - 1]);
    g_GfxBuffers[2] = (g_GfxBuffers[1] + g_GfxSizesByPlayerCount[getPlayerCount() - 1]);

    g_VtxBuffers[0] = mempAllocBytesInBank(g_VtxSizesByPlayerCount[getPlayerCount() - 1] * 2, MEMPOOL_STAGE);
    g_VtxBuffers[1] = (g_VtxBuffers[0] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);
    g_VtxBuffers[2] = (g_VtxBuffers[1] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);

    g_GfxActiveBufferIndex = 0;
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[0];

#ifndef TARGET_N64
    { /* GE_ROOM_AUDIT: record the pool sizes this stage actually got; if the
       * -mgfx/-mvtx stage tokens fail to parse, the 64KB defaults apply. */
        extern s32 portRoomAuditEnabled(void);
        if (portRoomAuditEnabled()) {
            fprintf(stderr, "roomaudit: dyn init gfx=%dKB vtx=%dKB players=%d (mgfx tok %s, mvtx tok %s)\n",
                    g_GfxSizesByPlayerCount[getPlayerCount() - 1] / 1024,
                    g_VtxSizesByPlayerCount[getPlayerCount() - 1] / 1024,
                    getPlayerCount(),
                    tokenFind(1, "-mgfx") ? "found" : "MISSING",
                    tokenFind(1, "-mvtx") ? "found" : "MISSING");
            fflush(stderr);
        }
    }
#endif
}

Gfx *dynGetMasterDisplayList(void) {
    g_GfxRequestedDisplayList = TRUE;

    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex];
}

s32 dynGetFreeGfx2(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

/**
 * Address: 7F0BD6C4
 */
Vtx *dynAllocateVertices(s32 count) 
{
    void *ptr = g_GfxMemPos;
	g_GfxMemPos += count * sizeof(Vtx);
	return ptr;
}

Mtx *dynAllocateMatrix(void)
{
	void *ptr = g_GfxMemPos;
	g_GfxMemPos += sizeof(Mtx);
	return ptr;
}

/**
 * Address: 7F0BD6F8
 */
Light *dynAllocateLights(s32 count)
{
    void *ptr = g_GfxMemPos;
    g_GfxMemPos += count * sizeof(Light);
    return ptr;
}

void *dynAllocate(s32 size) {
    void *ptr = g_GfxMemPos;
	size = ALIGN16_a(size);
	g_GfxMemPos += size;
	return ptr;
}

#ifndef TARGET_N64
/* port: is p inside the per-frame dynamic vertex arena (both halves)? The
 * GPU-retained part cache (gfx_roomcache.inl) must not key on such a
 * pointer - the front end and glass rebuild those vertices every frame. */
s32 portIsDynVertexPointer(const void *p)
{
    return (const u8 *)p >= (const u8 *)g_VtxBuffers[0] && (const u8 *)p < (const u8 *)g_VtxBuffers[2];
}
#endif
void dynSwapBuffers(void) {
    g_GfxActiveBufferIndex = (g_GfxActiveBufferIndex ^ 1);
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[g_GfxActiveBufferIndex];
}

void dynRemovedFunc(Gfx *gdl) {
}

s32 dynGetFreeGfx(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

s32 dynGetFreeVtx(void) {
	return g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos;
}

// Address 0x7F0BD7CC NTSC
void dynCalculateMembarLength(const char* arg0, f32 arg1, f32 arg2)
{
    s32 len;
    f32 zero = 0;
    
    len = strlen(arg0);
    
    arg1 /= arg2;
    
    if(zero);
    
    if (arg1 < zero && len > 1)
    {
        if (len > 1)
        {
            
        }
    }
}

#ifndef TARGET_N64
/* GE_ROOM_AUDIT: frame-end check of gfx/vtx pool usage, called from boss.c
 * with the master display list's end pointer BEFORE dynSwapBuffers. Both pools
 * are unchecked bump allocators that grow with the visible-room set, so an
 * overflow silently corrupts whatever memp allocated next. */
void portDynAuditFrameEnd(Gfx *gdlEnd)
{
    extern s32 portRoomAuditEnabled(void);
    static s32 gfxHigh, vtxHigh;
    static u32 frames;
    s32 cap, used, vcap, vused;

    if (!portRoomAuditEnabled() || g_GfxBuffers[0] == NULL) {
        return;
    }

    cap   = g_GfxSizesByPlayerCount[getPlayerCount() - 1];
    used  = (u8 *)gdlEnd - g_GfxBuffers[g_GfxActiveBufferIndex];
    vcap  = g_VtxSizesByPlayerCount[getPlayerCount() - 1];
    vused = g_GfxMemPos - g_VtxBuffers[g_GfxActiveBufferIndex];

    frames++;
    if (used > gfxHigh) {
        gfxHigh = used;
    }
    if (vused > vtxHigh) {
        vtxHigh = vused;
    }

    if (used > cap || vused > vcap) {
        fprintf(stderr,
                "roomaudit: *** DYN POOL OVERFLOW gfx=%d/%d vtx=%d/%d f=%u - frame data spilled into the neighbouring allocation\n",
                used, cap, vused, vcap, frames);
        fflush(stderr);
    } else if (used > cap - 0x400 || vused > vcap - 0x400) {
        fprintf(stderr, "roomaudit: *** DYN POOL NEAR-FULL gfx=%d/%d vtx=%d/%d f=%u\n",
                used, cap, vused, vcap, frames);
        fflush(stderr);
    }

    if ((frames % 60u) == 0u) {
        fprintf(stderr, "roomaudit: dyn usage gfx=%d/%d (hi %d) vtx=%d/%d (hi %d)\n",
                used, cap, gfxHigh, vused, vcap, vtxHigh);
        fflush(stderr);
    }
}
#endif /* !TARGET_N64 */


void dynDrawMembars(Gfx *gdl) {
    dynCalculateMembarLength(membars_string2, ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl), ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex]));
    dynCalculateMembarLength(membars_string2, (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos), (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_VtxBuffers[g_GfxActiveBufferIndex]));
}
