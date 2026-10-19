#if !defined(TARGET_N64)
/* Host builds use libc declarations, including macOS fortified string macros. */
#include_next <string.h>
#else

#ifndef _STRING_H_
#define _STRING_H_
#include <PR/ultratypes.h>

extern void *memcpy(void *, const void *, size_t);
extern unsigned char *strchr(const unsigned char *, int);
extern size_t strlen(const unsigned char *);

#endif
#endif /* TARGET_N64 */
