/**
 * port_atomic_file.c - same-directory atomic replacement.
 *
 * See port_atomic_file.h for the contract. No SDL and no repo shadow headers:
 * this TU must also compile with a host compiler for the test harness.
 */
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS /* fopen/snprintf are the portable API here */
#endif

#include "port_atomic_file.h"

#include <errno.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static void atomicReset(PortAtomicFile *af)
{
    af->fp = NULL;
    af->path[0] = '\0';
    af->temp[0] = '\0';
    af->active = 0;
    af->stage = PORT_ATOMIC_STAGE_NONE;
    af->error = 0;
}

/**
 * Close (when still open), remove the temp and record the failure. The
 * destination is deliberately not touched, so the previous contents stay.
 */
static int atomicFail(PortAtomicFile *af, int stage, int error)
{
    if (af->fp != NULL)
    {
        fclose(af->fp);
        af->fp = NULL;
    }
    if (af->temp[0] != '\0')
    {
        remove(af->temp);
    }
    af->active = 0;
    af->stage = stage;
    af->error = (error != 0) ? error : EIO;
    return 0;
}

#ifndef _WIN32
/* Best-effort durability for the rename itself. Failing to fsync the parent
 * directory does not invalidate the already-completed replace. */
static void atomicSyncParent(const char *path)
{
    char dir[PORT_ATOMIC_PATH_MAX];
    const char *p;
    const char *last = NULL;
    int fd;

    for (p = path; *p != '\0'; p++)
    {
        if (*p == '/')
        {
            last = p;
        }
    }
    if (last == NULL)
    {
        snprintf(dir, sizeof(dir), ".");
    }
    else
    {
        int n = (int) (last - path);

        if (n < 1)
        {
            n = 1; /* "/file" -> "/" */
        }
        snprintf(dir, sizeof(dir), "%.*s", n, path);
    }
#ifdef O_DIRECTORY
    fd = open(dir, O_RDONLY | O_DIRECTORY);
#else
    fd = open(dir, O_RDONLY);
#endif
    if (fd >= 0)
    {
        (void) fsync(fd);
        (void) close(fd);
    }
}
#endif

void portAtomicFileInit(PortAtomicFile *af)
{
    if (af != NULL)
    {
        atomicReset(af);
    }
}

int portAtomicFileOpen(PortAtomicFile *af, const char *path, int binary)
{
    int n;

    if (af == NULL || path == NULL || path[0] == '\0' || (binary != 0 && binary != 1))
    {
        if (af != NULL)
        {
            af->stage = PORT_ATOMIC_STAGE_OPEN;
            af->error = EINVAL;
        }
        return 0;
    }
    if (af->active || af->fp != NULL)
    {
        af->stage = PORT_ATOMIC_STAGE_OPEN;
        af->error = EBUSY;
        return 0;
    }

    n = snprintf(af->path, sizeof(af->path), "%s", path);
    if (n < 0 || n >= (int) sizeof(af->path))
    {
        af->stage = PORT_ATOMIC_STAGE_OPEN;
        af->error = ENAMETOOLONG;
        return 0;
    }
    n = snprintf(af->temp, sizeof(af->temp), "%s.tmp", path);
    if (n < 0 || n >= (int) sizeof(af->temp))
    {
        af->stage = PORT_ATOMIC_STAGE_OPEN;
        af->error = ENAMETOOLONG;
        return 0;
    }

    af->fp = fopen(af->temp, binary ? "wb" : "w");
    if (af->fp == NULL)
    {
        af->stage = PORT_ATOMIC_STAGE_OPEN;
        af->error = errno;
        af->active = 0;
        return 0;
    }
    af->active = 1;
    af->stage = PORT_ATOMIC_STAGE_NONE;
    af->error = 0;
    return 1;
}

FILE *portAtomicFileStream(PortAtomicFile *af)
{
    return (af != NULL && af->active) ? af->fp : NULL;
}

int portAtomicFileCommit(PortAtomicFile *af)
{
    if (af == NULL || !af->active || af->fp == NULL)
    {
        if (af != NULL)
        {
            af->stage = PORT_ATOMIC_STAGE_OPEN;
            af->error = EINVAL;
        }
        return 0;
    }

    if (ferror(af->fp))
    {
        return atomicFail(af, PORT_ATOMIC_STAGE_WRITE, EIO);
    }
    if (fflush(af->fp) != 0)
    {
        return atomicFail(af, PORT_ATOMIC_STAGE_FLUSH, errno);
    }

    /* The bytes, not just the stdio buffer, must reach the storage before the
     * name switch; otherwise a crash could publish a file with missing data. */
#ifdef _WIN32
    if (_commit(_fileno(af->fp)) != 0)
    {
        return atomicFail(af, PORT_ATOMIC_STAGE_SYNC, errno);
    }
#else
    if (fsync(fileno(af->fp)) != 0)
    {
        return atomicFail(af, PORT_ATOMIC_STAGE_SYNC, errno);
    }
#endif

    if (fclose(af->fp) != 0)
    {
        af->fp = NULL; /* fclose closes on failure too; never double-close */
        return atomicFail(af, PORT_ATOMIC_STAGE_CLOSE, errno);
    }
    af->fp = NULL;

#ifdef _WIN32
    if (!MoveFileExA(af->temp, af->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return atomicFail(af, PORT_ATOMIC_STAGE_REPLACE, EIO);
    }
#else
    {   /* Keep the old file's permission bits (the replace is a new inode). */
        struct stat st;

        if (stat(af->path, &st) == 0)
        {
            (void) chmod(af->temp, st.st_mode & 07777);
        }
    }
    if (rename(af->temp, af->path) != 0)
    {
        return atomicFail(af, PORT_ATOMIC_STAGE_REPLACE, errno);
    }
    atomicSyncParent(af->path);
#endif

    af->active = 0;
    af->stage = PORT_ATOMIC_STAGE_NONE;
    af->error = 0;
    af->path[0] = '\0';
    af->temp[0] = '\0';
    return 1;
}

void portAtomicFileAbort(PortAtomicFile *af)
{
    if (af == NULL)
    {
        return;
    }
    if (af->fp != NULL)
    {
        fclose(af->fp);
        af->fp = NULL;
    }
    if (af->temp[0] != '\0')
    {
        remove(af->temp);
    }
    af->active = 0;
    af->stage = PORT_ATOMIC_STAGE_NONE;
    af->error = 0;
}

int portAtomicFileStage(const PortAtomicFile *af)
{
    return (af != NULL) ? af->stage : PORT_ATOMIC_STAGE_NONE;
}

const char *portAtomicFileStageName(int stage)
{
    switch (stage)
    {
    case PORT_ATOMIC_STAGE_OPEN:    return "open";
    case PORT_ATOMIC_STAGE_WRITE:   return "write";
    case PORT_ATOMIC_STAGE_FLUSH:   return "flush";
    case PORT_ATOMIC_STAGE_SYNC:    return "sync";
    case PORT_ATOMIC_STAGE_CLOSE:   return "close";
    case PORT_ATOMIC_STAGE_REPLACE: return "replace";
    default:                        return "none";
    }
}

int portAtomicFileError(const PortAtomicFile *af)
{
    return (af != NULL) ? af->error : 0;
}
