#if !defined(TARGET_N64) && defined(__cplusplus)
/* PC port: this header shadows the toolchain's <string.h>; C++ TUs (fast3d)
 * defer to the real one, which libstdc++ needs. C TUs keep the explicit
 * declarations below, because the hosted header's bcopy/bzero/bcmp forms
 * conflict with PR/os.h. */
#include_next <string.h>
#else

#ifndef _STRING_H_
#define _STRING_H_
#include <PR/ultratypes.h>

#ifdef TARGET_N64
/* libultra's minimal surface (the N64 build links src/libultra/libc/string.c). */
extern void *memcpy(void *, const void *, size_t);
extern unsigned char *strchr(const unsigned char *, int);
extern size_t strlen(const unsigned char *);
#else
/* Hosted C TUs. Do not include_next here: PR/os.h owns bcopy/bcmp/bzero (the
 * N64 int forms are replaced by <strings.h> on glibc/bionic), and a second
 * <string.h> declaration of those names is what broke the old header.
 * Declare the size_t C surface the decompiled code actually calls so Clang
 * does not treat every call as an implicit int declaration. */
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int   memcmp(const void *, const void *, size_t);
extern void *memchr(const void *, int, size_t);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, size_t);
extern char *strcat(char *, const char *);
extern char *strncat(char *, const char *, size_t);
extern int   strcmp(const char *, const char *);
extern int   strncmp(const char *, const char *, size_t);
extern size_t strlen(const char *);
extern char *strchr(const char *, int);
extern char *strrchr(const char *, int);
extern char *strstr(const char *, const char *);
extern char *strdup(const char *);
extern size_t strspn(const char *, const char *);
extern size_t strcspn(const char *, const char *);
extern char *strerror(int);
#endif /* TARGET_N64 */

#endif
#endif /* TARGET_N64 */
