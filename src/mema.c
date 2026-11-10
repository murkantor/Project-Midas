#include <ultra64.h>
#include "mema.h"
#include "deb.h"

#ifndef TARGET_N64
#include <stdio.h>  /* GE_ROOM_AUDIT heap validation (see bg.c) */
#include <stdlib.h>
#endif

/**
 * mema - memory (ad hoc) allocation system.
 *
 * Mema's heap is 300KB and is itself allocated out of memp's stage pool.
 * Memp resets its stage pool each time a new stage is loaded, which means mema
 * is also reset each time a stage is loaded.
 *
 * Unlike memp, mema supports freeing of individual allocations. This makes it
 * a good system to use when the allocation is somewhat temporary and should be
 * freed without having to load a new stage. It's used by the (inaccessible)
 * Perfect Head editor, file listings and room code.
 *
 * Mema tracks what has been allocated by storing references to free spaces in
 * its spaces array. The allocations themselves are not referenced. When
 * initialising the spaces array, the first element is set to the entire heap
 * and the remaining elements are set to 0.
 *
 * This creates a bit of a terminology problem. Just remember that a memaspace
 * is not an allocation; it's a free space that is available for allocation.
 *
 * Due to the ability to free individual allocations, both the heap and the
 * spaces array can become fragmented. Mema supports defragmenting the spaces
 * array: entries are ordered by address, and back to back entries are merged.
 * The data in the heap itself is never moved, as that would require updating
 * pointers throughout the game code which mema cannot do.
 */

#if defined(VERSION_EU)
#define ALLOCATIONS_LENGTH 125
#else
#define ALLOCATIONS_LENGTH 509
#endif

typedef struct memaspace {
    intptr_t addr;
    u32 size;
} memaspace;

/**
 * This structure contains dummy entries before and after the spaces array.
 * These are used as start and end markers, but could have been avoided by
 * using loop counters (eg. a typical i < numspaces loop).
 */
struct memaheap {
	u32 unk000;
    u32 unk004;
	struct memaspace start;
    //u32 unk010;
    //u32 unk014;
	struct memaspace spaces[ALLOCATIONS_LENGTH - 1];
	struct memaspace end1;
	struct memaspace end2;
};

intptr_t g_MemaHeapStart;
s32 g_MemaHeapSize;
struct memaheap g_MemoryAllocations;
void *g_MemoryAllocationDebugData = NULL;

// Swap two allocations.
void memaSwap(memaspace *a, memaspace *b) {
    intptr_t tempaddr = a->addr;
    u32 tempsize = a->size;
    a->addr = b->addr;
    a->size = b->size;
    b->addr = tempaddr;
    b->size = tempsize;
}

// Merge two allocations.
void memaMerge(memaspace *a, memaspace *b) {
    a->size = (a->size + b->size);
    b->addr = 0;
    b->size = 0;
}

// Do a single iteration over the allocations and attempt to
// merge adjacent ones. Return value indicates if there were
// any changes to the free list.
s32 memaDefragPass(struct memaheap *heap) {
    s32 any = FALSE;
	struct memaspace *prev = &heap->start;
	struct memaspace *curr = &heap->spaces[0];
	struct memaspace *last = &heap->spaces[ALLOCATIONS_LENGTH - 2];

    intptr_t addr = 0;
    while (curr <= last) {
        if (curr->size != 0) {
            if (curr->addr < addr) {
                memaSwap(curr, prev);
                addr = prev->addr;
                any = TRUE;
            }
            if ((prev->size + addr) == curr->addr) {
                memaMerge(prev, curr);
                curr = prev;
                any = TRUE;
            }
            prev = curr;
            addr = curr->addr;
        }
        curr++;
    }
    return any;
}

// Do multiple merge iterations until there are no
// mergable pairs left.
void memaDefrag(void)
{
    while (memaDefragPass(&g_MemoryAllocations));
}

/**
 * Defrag the spaces list in an attempt to free up any slot.
 *
 * If none can be found, return the smallest run of free space so it can be
 * overwritten by the caller.
 */
memaspace *memaSearch(struct memaheap *heap)
{
    struct memaspace *curr = &heap->spaces[0];
	struct memaspace *best;

    u32 min;
    s32 i;

    // Do 124 passes over the list. This ensures the list is in order by the
	// end. Though in most cases it's roughly in order anyway, and the excessive
	// looping is just wasting CPU cycles. In reality this situation probably
	// never occurs.
    for (i = 0; i < (ALLOCATIONS_LENGTH - 1); i++) {
        while (curr <= &heap->spaces[ALLOCATIONS_LENGTH - 2]) {
            if (curr->size == 0) {
                return curr;
            }

            if ((intptr_t)curr[1].addr < (intptr_t)curr[0].addr) {
                memaSwap(&curr[0], &curr[1]);
            }

            if (curr[1].addr == (curr[0].size + curr[0].addr)) {
                // Found two that can be merged
                curr[0].size += curr[1].size;
                curr[1].addr = 0;
                curr[1].size = 0;
                return &curr[1];
            }

            curr++;
        }

        curr = &heap->spaces[0];
    }

    // If this code is reached then the spaces list is so badly and unrepairably
	// fragmented that we can't find any slot to record the free space in.
	// Find the smallest run of free space and use that instead.
	// The caller will overwrite it with its own free allocation, causing the
	// original run of free space to be unusable until the mema heap is reset.
    min = 0xFFFFFFFF;
    best = curr;
    while (curr <= &heap->spaces[ALLOCATIONS_LENGTH - 2]) {
        if (curr->size < min) {            
            best = curr;
            min = curr->size;
        }

        curr++;
    }

    return best;
}

#ifndef TARGET_N64
/* GE_ROOM_AUDIT (shared with bg.c): check each free against the free-space list
 * before recording it. A free outside the heap or overlapping already-free
 * space (double free / wrong size) would silently give two owners the same
 * bytes. */
extern s32 portRoomAuditEnabled(void);

static void memaAuditFree(intptr_t addr, s32 size)
{
    s32 i;

    if (!portRoomAuditEnabled()) {
        return;
    }

    fprintf(stderr, "roomaudit: mema free  %08x +%06x\n", addr, size);

    if (addr < g_MemaHeapStart || (addr + size) > (g_MemaHeapStart + g_MemaHeapSize)) {
        fprintf(stderr,
                "roomaudit: *** MEMA FREE OUT OF HEAP %08x +%06x (heap %08x +%06x)\n",
                addr, size, g_MemaHeapStart, g_MemaHeapSize);
    }

    for (i = 0; i < ALLOCATIONS_LENGTH - 1; i++) {
        struct memaspace *s = &g_MemoryAllocations.spaces[i];

        if (s->size == 0) {
            continue;
        }
        if (addr < (intptr_t)(s->addr + s->size) && s->addr < (intptr_t)(addr + size)) {
            fprintf(stderr,
                    "roomaudit: *** MEMA DOUBLE/BAD FREE %08x +%06x overlaps free space %08x +%06x\n",
                    addr, size, s->addr, s->size);
        }
    }
    fflush(stderr);
}
#endif /* !TARGET_N64 */

void _memaFree(intptr_t addr, s32 size)
{
#ifndef TARGET_N64
    memaAuditFree(addr, size);
#endif
    // Choose an index in the spaces array which we'll mark a space as free,
	// based on how far into the heap the allocation is. This is a rough
	// estimate and doesn't need to be any particular index, but the defrag
	// function tries to order the spaces by address so the closer we get to it
	// the less work the defrag function will have to do should it be called.
    s32 index = ((addr - g_MemaHeapStart) * (ALLOCATIONS_LENGTH-1)) / g_MemaHeapSize;
    struct memaspace *curr = &g_MemoryAllocations.spaces[index];

    // If the entry is taken, keep moving forward until a zero is found.
    while (curr->size != 0) {
        curr++;
    }

    // If we reached the end of the spaces list, go backwards instead
    if (curr->addr == -1) {
        curr = &g_MemoryAllocations.spaces[index];

        while (curr->size != 0) {
            curr--;
        }

        if (curr->addr == 0) {
            curr = memaSearch(&g_MemoryAllocations);
        }
    }

    // Mark this space as free
    curr->addr = addr;
    curr->size = size;
}

// Initialize the (removed) debug features.
void memaInit(void)
{
    debTryAdd(&g_MemoryAllocationDebugData, "mema_c_debug");
}

void memaReset(void *heapaddr, u32 heapsize)
{
   struct memaspace *space;

    g_MemoryAllocations.unk000 = 0;
    g_MemoryAllocations.unk004 = 0;

	g_MemoryAllocations.start.addr = 0;
	g_MemoryAllocations.start.size = 0;

	g_MemoryAllocations.end1.addr = -1;
	g_MemoryAllocations.end1.size = 0;
	g_MemoryAllocations.end2.addr = -1;
	g_MemoryAllocations.end2.size = 0xffffffff;

	for (space = &g_MemoryAllocations.spaces[0]; space <= &g_MemoryAllocations.spaces[ALLOCATIONS_LENGTH - 2]; space++) {
		space->addr = 0;
		space->size = 0;
	}

	g_MemoryAllocations.spaces[0].addr = g_MemaHeapStart = (intptr_t) heapaddr;
	g_MemoryAllocations.spaces[0].size = g_MemaHeapSize = heapsize;

#ifndef TARGET_N64
    {
        extern void portRoomAuditOnHeapReset(void);
        if (portRoomAuditEnabled()) {
            fprintf(stderr, "roomaudit: mema reset %08x +%06x\n", g_MemaHeapStart, g_MemaHeapSize);
            fflush(stderr);
        }
        portRoomAuditOnHeapReset(); /* all shadows are stale now */
    }
#endif
}

void memaSingleDefragPass(void)
{
    memaDefragPass(&g_MemoryAllocations);
}


// Attempt to free up some memory. Start by looking through the first 16 allocations
// for a suitable one. If none is found, then look through the rest for any that are
// large enough. If this also fails, then do 8 merge iterations and then look through
// entire buffer again. If successful, return the address to the freed memory, otherwise 0.
void *memaAlloc(u32 amount) {
    intptr_t addr;
    u32 diff;
    s32 i;

    struct memaspace *curr;
	u32 bestdiff;
	struct memaspace *best;

    if(1);

	curr = &g_MemoryAllocations.spaces[0];
	bestdiff = 0xffffffff;
	best = NULL;

    for (i = 0; i < 16; i++, curr++) {
        if (curr->size < amount) {
            continue;
        }

        if (curr->addr == -1) {
            break;
        }

        diff = (curr->size - amount);

        if (diff < bestdiff) {
            bestdiff = diff;
            best = curr;
            if ((diff < 64) || (diff < (amount / 4))) {
                break;
            }
        }
    }

    if (best == NULL) {
        while (curr->size < amount) {
            curr++;
        }

        if (curr->addr == -1) {
            for (i = 0; i < 8; i++) {
                memaDefragPass(&g_MemoryAllocations);
            }

            curr = &g_MemoryAllocations.spaces[0];

            while (curr->size < amount) {
                curr++;
            }

            if (curr->addr == -1) {
#ifndef TARGET_N64
                if (portRoomAuditEnabled()) {
                    fprintf(stderr, "roomaudit: *** MEMA ALLOC FAILED +%06x\n", amount);
                    fflush(stderr);
                }
#endif
                return NULL;
            }
        }

        best = curr;
    }

    addr = best->addr;
    best->addr += amount;
    best->size -= amount;

    if (best->size == 0) {
        best->addr = 0;
    }

#ifndef TARGET_N64
    if (portRoomAuditEnabled()) {
        fprintf(stderr, "roomaudit: mema alloc %08x +%06x\n", addr, amount);
        fflush(stderr);
    }
#endif

    return (void*)addr;
}

// Find the memaspace of the given address and reduce its size by the given
// amount. If successful, return the same address, otherwise 0.
intptr_t memaGrow(intptr_t addr, u32 amount)
{
    u32        size = 0;
    intptr_t        new_addr;
    memaspace *curr;
    short      __unused_1 = addr + 3;
    u32        __unused_2;
    s32        __unused_3;
    short      __unused_4;
    short      __unused_5;

    if (addr) {}
    if (__unused_1) {}

    curr     = &g_MemoryAllocations.spaces[0];
    new_addr = addr;
    __unused_2 = !new_addr;

    while (curr->addr != -1)
    {
        __unused_1 = __unused_2;
        if (__unused_1) {}

        __unused_3 = __unused_2 + 1;
        if ((double)__unused_3) {}

        if (curr->addr == new_addr && curr->size >= amount)
        {
            goto found;
        }

        if (__unused_3) {}
        __unused_4 = new_addr + 2;
        if ((double)__unused_4) {}
        __unused_5 = new_addr + 3;
        if ((double)__unused_5) {}

        curr++;
    }

    return 0;

found:
    curr->addr += amount;
    curr->size -= amount;

    size = curr->size;

    if (size == 0)
    {
        curr->addr = 0;
    }

    return new_addr;
}

void memaFree(void *addr, s32 size)
{
	_memaFree((intptr_t) addr, size);
}


void mema7000A040(void)
{
    s32 i;
    struct memaspace *curr;

    for (i=0; i<ALLOCATIONS_LENGTH - 1; i++)
    {
        curr = &g_MemoryAllocations.spaces[i];

        if (curr->addr)
        {
            // removed
        }
    }
}


// Calculate the ratio between the sum of all allocations minus
// the largest one, and the sum of all allocations.
f32 memaCalculateNonLargestToTotalRatio(void) {
    u32 tot = 0;
    u32 max = 0;
    memaspace *curr = &g_MemoryAllocations.spaces[0];
    while (curr->addr != -1) {
        tot += curr->size;
        if (max < curr->size) {
            max = curr->size;
        }
        curr++;
    }
    if (tot == 0) {
        return 0.0f;
    }
    return ((f32)(tot - max) / tot);
}

// Print a list of allocations, in descending size order. Sizes are specified in
// kilobytes, rounded to nearest integer. Up to 200 allocations can be listed.
void memaDump(void)
{
    char *pos;
    u32 tot = 0;
    s32 lim = (1 << 31);
    s32 count;
    memaspace *curr;
    char buffer[4096];
    u32 max;
    u32 acc;

    count = 0;

    for (curr = &g_MemoryAllocations.spaces[0], tot = 0; curr->addr != -1; curr++)
    {
        tot += curr->size;
    }

    pos = buffer;

    for (max = 0, acc = 0; 1; lim = max, max = 0)
    {
        for (curr = &g_MemoryAllocations.spaces[0]; curr->addr != -1; curr++)
        {
            if ((curr->size < lim) && (curr->size > max))
            {
                max = curr->size;
                acc++;
            }
        }
        
        if (acc == 0)
        {
            break;
        }

        
        lim = max;
        
        for (curr = &g_MemoryAllocations.spaces[0],acc=0; curr->addr != -1; curr++)
        {
            if (curr->size == lim)
            {
                if (count < 200)
                {
                    pos += sprintf(pos, "%d ", ((curr->size + 512) / 1024));
                }
                else if (count == 200)
                {
                    pos += sprintf(pos, "...");
                }
                
                count++;
            }
        }
    }
    
    if (count > 200)
    {
        sprintf(pos, "[%d]", count);
    }
}

// Dump a list of allocations before and after a full
// merge pass.
void memaDumpPrePostMerge(void) {
    s32 i;    
    memaDump();
    for (i = 0; i < (ALLOCATIONS_LENGTH-1); i++) {
        memaDefragPass(&g_MemoryAllocations);
    }
    memaDump();
}

void mema7000A2F8(void (*func)(u32, memaspace*)) {
    memaspace *curr = &g_MemoryAllocations.spaces[0];
    while (curr->addr != -1) {
        func((curr->addr + curr->size), curr);
        curr++;
    }
}

/**
 * Find and return the largest amount of contiguous free space in the pool.
 * ie. the biggest allocation that mema can currently make.
 */
s32 memaGetLongestFree(void)
{
	struct memaspace *curr;
	s32 biggest = 0;

	memaDefrag();

	curr = &g_MemoryAllocations.spaces[0];

	while (curr->addr != -1) {
		if (curr->size > biggest) {
			biggest = curr->size;
		}

		curr++;
	}

	if (biggest) {
		return biggest;
	}

	return 0;
}

/**
 * Resize an existing memaspace. Either by shrinking the old one, or
 * by registering a new memaspace containing the remaining bytes.
 * 
 * US address 7000A3DC.
*/
s32 memaRealloc(intptr_t addr, u32 oldsize, u32 newsize)
{
    if ((newsize > oldsize))
    {
        if (memaGrow(addr + oldsize, newsize - oldsize) == 0)
        {
			return 0;
		}
        else
        {
            return 1;
        }
	}

    /**
     * Hack: the following few `if` statements are probably not what was originally
     * here, but it shifts the temp registers enough to generate a match.
     * https://decomp.me/scratch/RoPAF 99.62%
    */

    if (addr + 1) {}
    else if (addr + 2) {}
    else if (addr + 3) {}

    if (oldsize + 1) {}
    else if (oldsize + 2) {}
    else if (oldsize + 3) {}

    if (newsize + 1) {}
    else if (newsize + 2) {}
    else if (newsize + 3) {}

    if ((oldsize > newsize))
    {
        memaFree((void *)(addr + newsize), oldsize - newsize);
    }
    
	return 1;
}
