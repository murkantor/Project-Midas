#include <ultra64.h>
#include "game/lv.h"
#include "game/player.h"
#include <bondconstants.h>
#include <memp.h>
#include "alloc_window_pieces.h"
#include "glass.h"



void alloc_shattered_window_pieces(void)
{
    s32 i;
    s32 level = lvlGetCurrentStageToLoad();

    SHATTERED_WINDOW_PIECES_BUFFER_LEN = (200 / getPlayerCount());
    if ((level == LEVELID_STREETS) || (level == LEVELID_DEPOT))
    {
        SHATTERED_WINDOW_PIECES_BUFFER_LEN = (SHATTERED_WINDOW_PIECES_BUFFER_LEN >> 1);
    }
#ifdef DEBUG
    osSyncPrintf("Allocating %d bytes for glass data (%d bits)\n", SHATTERED_WINDOW_PIECES_BUFFER_LEN * sizeof(s_shattered_window_piece), SHATTERED_WINDOW_PIECES_BUFFER_LEN);
#endif

    ptr_shattered_window_pieces = mempAllocBytesInBank(((SHATTERED_WINDOW_PIECES_BUFFER_LEN * sizeof(s_shattered_window_piece)) + 0xF) & ~0xF, MEMPOOL_STAGE);

    for(i=0; i<SHATTERED_WINDOW_PIECES_BUFFER_LEN; i++)
    {
        ptr_shattered_window_pieces[i].active = 0;
    }

    g_NextShardNum = 0;
}

