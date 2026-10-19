/**
 * PC-port _Printf — the libultra libc formatted-print core.
 *
 * On N64 this lives in src/libultrare/libc/xprintf.c (declared in
 * src/libultra/libc/xstdio.h:42). The only kept caller is src/sprintf.c:
 *
 *     written = _Printf(proutSprintf, dst, fmt, args);
 *     if (written >= 0) dst[written] = 0;
 *     return written - 1;                    // GE quirk, preserved as-is
 *
 * (rmon.c's two call sites are excluded from the PC build by the CMake
 * ENGINE_EXCLUDE list.)
 *
 * Contract implemented (matches libultra semantics as sprintf.c consumes
 * them):
 *  - format the string, hand the bytes to prout(arg, bytes, count),
 *  - return the number of characters produced (EXCLUDING any NUL), or a
 *    negative value on error. sprintf then writes the terminator itself at
 *    dst[written] and returns written-1 — same observable behavior as N64.
 *
 * Implementation: vsnprintf into a stack buffer, one prout call. GE format
 * strings audited via sprintf call sites are small menu/debug/filename
 * strings (%s %d %x %c %f etc., all standard); 4 KB covers every site with
 * a wide margin, and truncation (should it ever happen) is clamped so the
 * prout'd byte count and the returned count stay consistent — sprintf's
 * dst[written] = 0 then lands exactly one past the copied bytes.
 *
 * Headers: <stdarg.h> resolves through the repo wrapper (include/stdarg.h)
 * which include_next's the compiler's real stdarg for host builds.
 */
#include <ultra64.h>
#include <stdarg.h>
#include <stdio.h>

int _Printf(u8 *(*prout)(u8 *, const u8 *, __SIZE_TYPE__), u8 *arg,
            const u8 *fmt, va_list args)
{
    char buf[4096];
    int n;

    n = vsnprintf(buf, sizeof(buf), (const char *)fmt, args);

    if (n < 0) {
        return -1;
    }

    if (n > (int)sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 1; /* clamp to what was actually produced */
    }

    if (prout(arg, (const u8 *)buf, (__SIZE_TYPE__)n) == NULL) {
        return -1;
    }

    return n;
}
