#include <ultra64.h>
#include "str.h"
#include "token.h"
#include <memp.h>
#include "image.h"
#include "initmttex.h"

void set_mt_tex_alloc(void)
{  
    g_TexCacheCount = 0;

    if (tokenFind(1, "-mt"))
    {
        bytes = strtol(tokenFind(1, "-mt"), 0x0, 0) * 1024; //get KB
    }

#ifndef TARGET_N64
    // Double texture memory on PC to avoid texture corruption bugs
    bytes *= 2;
#endif

    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
}
