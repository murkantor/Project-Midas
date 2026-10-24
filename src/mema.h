#ifndef _MEMA_H_
#define _MEMA_H_

#include <ultra64.h>

void memaInit(void);
void memaReset(void *heapaddr, u32 heapsize);
void memaSingleDefragPass(void);
void *memaAlloc(u32 size);
void memaFree(void *addr, s32 size);
void memaDumpPrePostMerge(void);
s32 memaGetLongestFree(void);
s32 memaRealloc(intptr_t addr, u32 oldsize, u32 newsize);

#endif
