/**
 * port_atomic_file.h - same-directory atomic file replacement.
 *
 * A writer opens a temp file beside the destination, streams into it, then
 * commits with fflush + fsync/_commit + fclose + atomic replace. A failure at
 * any point leaves the original file untouched. The temp name is always
 * "<path>.tmp" in the same directory, so the final replace never crosses a
 * filesystem boundary.
 *
 * This is intentionally malloc-free and uses only the C runtime plus POSIX
 * (or Windows) file APIs. It is safe to compile outside the game (the host
 * test under scripts/port/test_atomic_file.py does exactly that), so do not
 * pull in SDL, ultra64.h or the repo's shadow <string.h> here.
 *
 * Writes are serialized by the caller: like the original settings/EEPROM
 * writers, this helper assumes one thread writes a given file at a time.
 */
#ifndef PORT_ATOMIC_FILE_H
#define PORT_ATOMIC_FILE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Same capacity as main.c's portRootPath buffer. */
#define PORT_ATOMIC_PATH_MAX 1200

enum {
    PORT_ATOMIC_STAGE_NONE = 0,
    PORT_ATOMIC_STAGE_OPEN,
    PORT_ATOMIC_STAGE_WRITE,
    PORT_ATOMIC_STAGE_FLUSH,
    PORT_ATOMIC_STAGE_SYNC,
    PORT_ATOMIC_STAGE_CLOSE,
    PORT_ATOMIC_STAGE_REPLACE
};

typedef struct PortAtomicFile {
    FILE *fp;
    char path[PORT_ATOMIC_PATH_MAX];
    char temp[PORT_ATOMIC_PATH_MAX];
    int active;
    int stage;   /* PORT_ATOMIC_STAGE_*: where the last failure happened */
    int error;   /* errno-like code from the last failure, 0 on success */
} PortAtomicFile;

/** Prepare an unopened handle. Always call before Open. */
void portAtomicFileInit(PortAtomicFile *af);

/**
 * Open "<path>.tmp" for writing ("w" when binary is 0, "wb" when 1).
 * Returns 1 on success, 0 on failure (see stage/error). Does not touch path.
 */
int portAtomicFileOpen(PortAtomicFile *af, const char *path, int binary);

/** Stream to write into; NULL unless Open succeeded. */
FILE *portAtomicFileStream(PortAtomicFile *af);

/**
 * Finish the write: fflush, fsync/_commit, fclose, then replace path.
 * Returns 1 when path now holds the new contents. On 0 the original path is
 * unchanged (unless the platform's final replace partially succeeded, which
 * same-directory atomic replace exists to avoid); the temp is removed.
 */
int portAtomicFileCommit(PortAtomicFile *af);

/** Close and remove the temp without touching path. Safe to call any time. */
void portAtomicFileAbort(PortAtomicFile *af);

/** Diagnostic: last failure stage (PORT_ATOMIC_STAGE_*), NONE after success. */
int portAtomicFileStage(const PortAtomicFile *af);

/** Diagnostic: short name of a stage, for log lines ("replace", ...). */
const char *portAtomicFileStageName(int stage);

/** Diagnostic: last failure's errno-like code, 0 after success. */
int portAtomicFileError(const PortAtomicFile *af);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ATOMIC_FILE_H */
