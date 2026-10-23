#ifndef FAST3D_SYSTEM_SHIM_H
#define FAST3D_SYSTEM_SHIM_H

/**
 * Minimal stand-in for PD's port/include/system.h - only what the vendored
 * fast3d sources use. Implementations in port/src/sysstubs.c.
 */

#include <stdint.h>

typedef int32_t s32;
typedef long long s64; /* PR/ultratypes.h spells it this way (int64_t is 'long' on arm64) */
typedef uint32_t u32;

#define LOGFLAG_SHOWMSG 0x100

enum logLevel {
    LOG_NOTE = 0,
    LOG_CHAT = 0 | LOGFLAG_SHOWMSG,
    LOG_WARNING = 1 | LOGFLAG_SHOWMSG,
    LOG_ERROR = 2 | LOGFLAG_SHOWMSG,
};

#ifdef __cplusplus
extern "C" {
#endif

s32 sysArgCheck(const char *arg);
const char *sysArgGetString(const char *arg);
void sysFatalError(const char *fmt, ...) __attribute__((noreturn));
void sysLogPrintf(s32 level, const char *fmt, ...);
void sysSleep(const s64 hns);
void sysCpuRelax(void);
void sysLogSystemInfo(void);     /* `ge: sys:` boot lines - build, CPU, RAM, OS, timer, GE_* env, power */
void sysLogPowerIfChanged(void); /* AC / battery / battery saver, only when it changed */

#ifdef __cplusplus
}
#endif

#endif
