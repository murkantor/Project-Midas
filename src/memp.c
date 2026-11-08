#include <ultra64.h>
#include <deb.h>
#include "memp.h"
#include "game/language.h"

/**
 * EU .data, offset from start of data_seg : 0x3640
*/

/**
 * @file memp.c
 * This file contains code for memp.
 */

//bss
MemoryPool g_mempPools[MEMPOOL_COUNT];

//data
void *ptr_memp_c_debug_debug_notice_list = 0;
s32 needmemallocation = 0;
s32 D_80024408 = 0;
s32 D_8002440C = 0;
s32 D_80024410 = 0;

//overloaded
struct s_mempMVALS sdefaultmvals = {
    MEMPOOL_MF + 1,    0,  // MEMPOOL_MF
    MEMPOOL_ML + 1,    82, // MEMPOOL_ML
    MEMPOOL_ME + 1,    15, // MEMPOOL_ME
    0,                 0   // MEMPOOL_END
};

void mempInit(void)
{
    debTryAdd(&ptr_memp_c_debug_debug_notice_list, "memp_c_debug");
}

const char *tokenFind(s32 arg0, const char *arg1);
long int strtol(const char *str, char **endptr, int base);
void mempCheckMemflagTokens(uintptr_t poolAreaStart, s32 poolAreaSize)
{
    s_mempMVALS poolSizes;

    //set pool 0 to what boss wants (room_model_buffer)
    //pool 0 = TotalPoolArea
    g_mempPools[MEMPOOL_TOTAL].start = poolAreaStart;
    g_mempPools[MEMPOOL_TOTAL].end = poolAreaStart + poolAreaSize;

    poolSizes = sdefaultmvals;

    if (tokenFind(1, "-mf"))
    {
        poolSizes.mf = strtol(tokenFind(1, "-mf"), NULL, 0);
    }
    if (tokenFind(1, "-ml"))
    {
        poolSizes.ml = strtol(tokenFind(1, "-ml"), NULL, 0);
    }
    if (tokenFind(1, "-me"))
    {
        poolSizes.me = strtol(tokenFind(1, "-me"), NULL, 0);
    }
    if (poolSizes.me == 0)
    {
        poolSizes.mf = 0;
        poolSizes.me = ((j_text_trigger ? 308 : 296) * 1024);
        poolSizes.ml = poolAreaSize - poolSizes.me;
    }

    mempSetBankStarts((s32*)&poolSizes);
}

void mempSetBankStarts(s32 poolSizes[MEMPOOL_COUNT+1])
{
    s32 i;
    s32 bankstarts[MEMPOOL_COUNT] = {0};
    s32 mempLen;
    s32 mempRequested;
    uintptr_t mempStart;

    //set MF, ML, ME first
    i = 0;
    do
    {
        // assign the "xxxIndex" the value of xxx+1 then skip "Indices", 0=2=mf, 2=4=ml, 4=6=me, 6=8=end
        bankstarts[poolSizes[i]] = poolSizes[i+1];
        i += 2;
    } while (poolSizes[i] != 0); //while sizes not = 0 (bank 7 = 0)
    //  0 1 2 3            4           5     6
    // {0,0,0,0,poolAreaSize - 303104, 0, 303104}

    //for each bankstart, add current to next
    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT - 1; i++)
    {
        bankstarts[i + 1] += bankstarts[i];
    }
    // {0,0,0,0,poolAreaSize - 303104, poolAreaSize - 303104, poolAreaSize}


    mempRequested = bankstarts[MEMPOOL_COUNT - 1]; //total accumulated size of banks = poolAreaSize
    mempLen  = (g_mempPools[MEMPOOL_TOTAL].end - g_mempPools[MEMPOOL_TOTAL].start);

    //for each bankstart, multiply by total pool size, then divide by size of banks 1-7
    //spread each bank evenly
    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT; i++)
    {
        bankstarts[i] = ((s64)bankstarts[i] * mempLen) / mempRequested;
    }
    // {0,0,0,0,poolAreaSize - 303104, poolAreaSize - 303104, poolAreaSize}

    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT; i++)
    {
        bankstarts[i] = ALIGN16_b(bankstarts[i]);
    }
    // {0,0,0,0,poolAreaSize - 303104, poolAreaSize - 303104, poolAreaSize}


    mempStart = g_mempPools[MEMPOOL_TOTAL].start;
    //for each bank 1-7, add new start position
    for (i = MEMPOOL_TOTAL; i < MEMPOOL_COUNT - 1; i++)
    {
        g_mempPools[i + 1].start = bankstarts[i] + mempStart;
        g_mempPools[i + 1].pos   = 0;
        g_mempPools[i + 1].end   = bankstarts[i + 1] + mempStart;
    }
    /*
                           rel-start              size
    g_memPools[TOTAL]      0                      poolArea
    g_memPools[MF]         0                      0
    g_memPools[2]          0                      0
    g_memPools[ML]         0                      0
    g_memPools[STAGE]      0                      poolAreaSize - 303104
    g_memPools[ME]         poolAreaSize - 303104  0
    g_memPools[PERMANENT]  poolAreaSize - 303104  303104
    */
}


void *mempAllocBytesInBank(u32 bytes, u8 poolnum)
{
    /*
     * Retain this address expression. Using
     * &g_mempPools[poolnum] changes regalloc.
     */
    MemoryPool *pool = (MemoryPool *)(((u8 **)g_mempPools) + ((poolnum * 2) << 1));
    u8 *allocation = pool->pos;

#ifdef DEBUG
    if ((poolnum < 0) || (4 < poolnum))
    {
        osSyncPrintf("mempAllocBytesInBank from invalid heap %d!", poolnum);
    }
#endif

    if (pool->pos == NULL)
    {
        while (1);
    }

    if (pool->pos > pool->end)
    {
        nulled_mempLoopAllMemBanks();

        while (1);
    }

    if (pool->pos + bytes > pool->end)
    {
        if (g_mempPools[MEMPOOL_PERMANENT].pos + bytes <= g_mempPools[MEMPOOL_PERMANENT].end)
        {
            /*
             * There was probably debug code in the original that got mostly
             * stripped, but it still perturbs register allocation. These
             * statements fill t1/t3/t4 so the registers match.
             */
            if (needmemallocation);
            if (&D_8002440C == &D_80024408);
            if (needmemallocation);
            if (&D_80024410 == &D_80024408);
            if (!needmemallocation);

            needmemallocation = TRUE;

            return mempAllocBytesInBank(bytes, MEMPOOL_PERMANENT);
        }

        nulled_mempLoopAllMemBanks();

        while (1);
    }

    pool->pos += bytes;
    pool->prevpos = allocation;

    if (needmemallocation);

    return allocation;
}


/**
 * Resize the most recent allocation in a pool without moving it.
 */
MEMP_ADD_ENTRY_RESULT mempAddEntryOfSizeToBank(void *allocation, s32 newsize, u8 poolnum)
{
    MemoryPool *pool;
    s32 origsize;
    s32 growsize;

    if (needmemallocation && allocation == g_mempPools[MEMPOOL_PERMANENT].prevpos)
    {
        poolnum = MEMPOOL_PERMANENT;
    }

    allocation = (void *)(u64)allocation;
    pool = &g_mempPools[poolnum];

    if (pool->pos == 0)
    {
        while (TRUE);
    }

    if (allocation != pool->prevpos)
    {
        return MEMP_ADD_ENTRY_NOT_LAST_ALLOCATION;
    }

    origsize = pool->pos - pool->prevpos;
    growsize = newsize - origsize;

    if (growsize <= 0)
    {
        pool->pos += growsize;
        return MEMP_ADD_ENTRY_SUCCESS;
    }

    if (pool->pos > pool->end)
    {
        nulled_mempLoopAllMemBanks();
        while (TRUE);
    }

    if (pool->pos + growsize > pool->end)
    {
        nulled_mempLoopAllMemBanks();
        while (TRUE);
    }

    pool->pos += growsize;
    return MEMP_ADD_ENTRY_SUCCESS;
}

void nulled_mempLoopAllMemBanks(void) {
    u8 bank;
    for (bank = MEMPOOL_MF; bank < MEMPOOL_COUNT; bank++)
    {
    }
}

s32 mempGetBankSizeLeft(u8 bank) {
    if (needmemallocation) {
        bank = MEMPOOL_PERMANENT;
    }

    if ((bank == MEMPOOL_STAGE) && (g_mempPools[MEMPOOL_STAGE].start == g_mempPools[MEMPOOL_STAGE].end))
    {
        bank = MEMPOOL_PERMANENT;
    }

    return g_mempPools[bank].end - g_mempPools[bank].pos;
}

// Last three bits contains the bank, the rest contains the size.
uintptr_t mempAllocPackedBytesInBank(u32 sizeandbank) {
    return mempAllocBytesInBank((sizeandbank >> 3), (sizeandbank & 7));
}

void mempResetBank(u8 bank) {
#ifdef GE_NATIVE_64
    if (bank == MEMPOOL_STAGE) {
        extern void modelConvertFreeAll(void);
        modelConvertFreeAll();
    }
#endif
#ifndef TARGET_N64
    { /* the frontend's crosshair copy lives here; crosshair_color must forget it */
        extern void portTexCrosshairForget(const void *lo, const void *hi);

        portTexCrosshairForget(g_mempPools[bank].start, g_mempPools[bank].end);
    }
#endif
    g_mempPools[bank].prevpos = 0;
    g_mempPools[bank].pos = g_mempPools[bank].start;
}

void mempNullNextEntryInBank(u8 bank) {
    nulled_mempLoopAllMemBanks();
    if (g_mempPools[bank].pos != 0) {
        g_mempPools[bank].pos = 0;
    }
}
