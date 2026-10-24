#include <stdarg.h>
#include "libultra/libc/xstdio.h"
#include <ultra64.h>
/*#include <stddef.h>*/



u8 *proutSprintf(u8 *dst, const u8 *src, size_t count) {
    return (u8 *)memcpy(dst, src, count) + count;
}

int sprintf(char *dst, const char *fmt, ...) {
    s32 written;
    va_list args;
    va_start(args, fmt);
    written = _Printf(proutSprintf, dst, fmt, args);
    if (written >= 0) {
        dst[written] = 0;
    }
    return written-1;
}




