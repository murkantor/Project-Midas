/**
 * File roles for the portable layout (beside the executable) and GE_XDG_PATHS
 * (read-only package data plus XDG user directories; see readme.md).
 * Kept out of main.c for ROM-free tests in scripts/port/test_xdg_paths.py.
 */
#define _FILE_OFFSET_BITS 64   /* stat() on 32-bit ELF with 64-bit inodes: EOVERFLOW, as romdata.c notes */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>   /* stat, mkdir */
#ifdef _WIN32
#include <direct.h>    /* _mkdir */
#include <windows.h>   /* GetModuleFileNameA */
#else
#include <unistd.h>    /* readlink */
#endif
#include "port_paths.h"

extern void portConLogf(const char *fmt, ...);   /* port_console.c */
/* the repo's C <string.h> is a minimal N64 stub: it does not declare these */
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);

#ifndef GE_XDG_SHARE_REL
#define GE_XDG_SHARE_REL "../share/project-midas"        /* bindir=bin, datadir=share */
#endif
#ifndef GE_XDG_SHARE_GAMES_REL
#define GE_XDG_SHARE_GAMES_REL "../share/games/project-midas"   /* Debian puts game data under share/games */
#endif

static char sPortExeDir[PORT_DIR_MAX];   /* with trailing separator; "" = cwd fallback */
static char sPortLogDir[PORT_PATH_MAX];  /* Data/Logs/ under whichever base */

#ifdef GE_XDG_PATHS
/* Package layout: the prefix is read-only, so the roles split. midas.ini goes
 * to the config dir; everything else written goes to the data dir, whose
 * Windows Data/ level drops (Data/Saves/x -> Saves/x). An absolute
 * $XDG_*_HOME wins, else $HOME/<default>; without either, startup fails
 * instead of writing somewhere unexpected. */
#define PORT_EXE_DIR_UNKNOWN "/nonexistent/"   /* no exe dir: every lookup misses */
static char sPortCfgDir[PORT_DIR_MAX];
static char sPortDataDir[PORT_DIR_MAX];
static char sPortShareDir[PORT_PATH_MAX];
#endif

/* ==== small path helpers ================================================= */

#ifdef _WIN32
static void portMkdirOne(const char *path) { _mkdir(path); }
#elif defined(GE_XDG_PATHS)
/* XDG recommends private new directories; existing modes stay intact. */
static void portMkdirOne(const char *path) { mkdir(path, 0700); }
#else
static void portMkdirOne(const char *path) { mkdir(path, 0755); }
#endif

static int portIsDir(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* out = dir + name. Returns 0 and "" when it does not fit, so a caller's
 * open/mkdir fails visibly instead of using a truncated path. */
static int portPathCpy(char *out, size_t n, const char *dir, const char *name)
{
    int len = snprintf(out, n, "%s%s", dir, name);

    if (len >= 0 && (size_t) len < n) {
        return 1;
    }
    out[0] = '\0';
    return 0;
}

/* dir + name into a role's buffer, with the one-line warning the callers
 * cannot give (they have no size to check against) */
static const char *portJoinName(char *out, const char *dir, const char *name)
{
    if (!portPathCpy(out, PORT_PATH_MAX, dir, name)) {
        static int warned;

        if (!warned) {
            warned = 1;
            portConLogf("ge: path too long: %s%s\n", dir, name);
        }
    }
    return out;
}

static int portIsSep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* Keep a trailing separator without truncation. Backslashes are ordinary
 * filename characters on POSIX. */
static int portAddSep(char *out, size_t n)
{
    size_t len = 0;

    while (out[len] != '\0') {
        len++;
    }
    if (len > 0 && !portIsSep(out[len - 1])) {
        if (len + 1 >= n) {
            return 0;
        }
        out[len] = '/';
        out[len + 1] = '\0';
    }
    return 1;
}

int portEnsureDir(const char *path)
{
    char buf[PORT_PATH_MAX];
    size_t i = 0;

    if (path == NULL || path[0] == '\0' || !portPathCpy(buf, sizeof(buf), path, "")) {
        return 0;
    }
    if (portIsDir(buf)) {
        return 1;   /* the common case after the first run */
    }
    /* skip a drive letter and the root separator: neither is ours to create
     * (_mkdir("C:") would target the drive itself) */
#ifdef _WIN32
    if (buf[1] == ':') {
        i = 2;
    }
#endif
    if (portIsSep(buf[i])) {
        i++;
    }
    for (; buf[i] != '\0'; i++) {
        if (portIsSep(buf[i])) {
            char sep = buf[i];

            buf[i] = '\0';
            portMkdirOne(buf);   /* fails harmlessly when it exists */
            buf[i] = sep;
        }
    }
    portMkdirOne(buf);
    if (portIsDir(buf)) {
        portConLogf("ge: paths: created %s\n", buf);
        return 1;
    }
    portConLogf("ge: paths: could not create %s (read-only install dir?)\n", buf);
    return 0;
}

/* ==== the default layout: everything next to the executable ============== */

#ifndef GE_XDG_PATHS

/* The settings file was called ge.ini before the Project Midas rename: when
 * there is no midas.ini yet, rename an existing ge.ini once so players keep
 * their settings. */
static void portMigrateIni(void)
{
    char oldpath[PORT_PATH_MAX];
    FILE *f = portCfgFopen("midas.ini", "r");

    if (f != NULL) {
        fclose(f);
        return;
    }
    snprintf(oldpath, sizeof(oldpath), "%s", portCfgPath("ge.ini"));
    if (rename(oldpath, portCfgPath("midas.ini")) == 0) {
        portConLogf("ge: renamed ge.ini to midas.ini\n");
    }
}

/* one exe dir, four roles: a call site still says which file it wants */
const char *portDataPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

const char *portCfgPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

const char *portSharePath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

int portLegacyFiles(void)
{
    return 1;   /* old eeprom.bin, Profiles/, WeaponSets/, Devices/, ge.ini */
}

#else /* GE_XDG_PATHS */

/* $XDG_*_HOME when it is an absolute path (the spec says a relative value is
 * invalid and must be ignored), else $HOME/<def>; fatal without either. */
static void portXdgBase(char *out, size_t n, const char *envname, const char *def)
{
    char buf[PORT_DIR_MAX];
    const char *v = getenv(envname);
    const char *home;

    if (v != NULL && v[0] == '/') {
        if (!portPathCpy(out, n, v, "")) {
            portConLogf("ge: %s is too long for the game's path buffers\n", envname);
            exit(EXIT_FAILURE);
        }
        return;
    }
    if (v != NULL && v[0] != '\0') {
        portConLogf("ge: ignoring relative %s=%s\n", envname, v);
    }
    home = getenv("HOME");
    if (home == NULL || home[0] != '/') {
        portConLogf("ge: cannot determine %s: set it or HOME to an absolute path\n", envname);
        exit(EXIT_FAILURE);
    }
    if (!portPathCpy(buf, sizeof(buf), home, "/") || !portPathCpy(out, n, buf, def)) {
        portConLogf("ge: HOME is too long for the game's path buffers\n");
        exit(EXIT_FAILURE);
    }
}

/* <base>/project-midas/ */
static void portAppDir(char *out, size_t n, const char *base)
{
    char tmp[PORT_DIR_MAX];

    if (!portPathCpy(tmp, sizeof(tmp), base, "/") || !portPathCpy(out, n, tmp, "project-midas/")) {
        portConLogf("ge: XDG base directory is too long: %s\n", base);
        exit(EXIT_FAILURE);
    }
}

/* <prefix>/share/project-midas (or share/games) from the exe dir and the
 * relative paths CMake baked in. The exe dir itself is the fallback, so a
 * build-tree run finds the files the POST_BUILD step copies there. */
static void portFindShareDir(void)
{
    static const char *const rels[] = { GE_XDG_SHARE_REL, GE_XDG_SHARE_GAMES_REL };
    char candidate[PORT_PATH_MAX];
    int i;

    snprintf(sPortShareDir, sizeof(sPortShareDir), "%s", sPortExeDir);
    for (i = 0; i < 2; i++) {
        char *real;

        if (!portPathCpy(candidate, sizeof(candidate), sPortExeDir, rels[i])) {
            continue;
        }
        if (!portIsDir(candidate)) {
            continue;
        }
        /* log the real path, not <prefix>/bin/../share/project-midas. The NULL
         * form lets realpath size its own buffer; POSIX wants PATH_MAX, which
         * is larger than ours. */
        real = realpath(candidate, NULL);
        if (!portPathCpy(sPortShareDir, sizeof(sPortShareDir), real != NULL ? real : candidate, "")
                || !portAddSep(sPortShareDir, sizeof(sPortShareDir))) {
            portConLogf("ge: shared data directory is too long: %s\n", candidate);
            free(real);
            exit(EXIT_FAILURE);
        }
        free(real);
        return;
    }
}

/* the Windows Data/ level drops: Data/Saves/x -> <data dir>/Saves/x */
static const char *portDataName(const char *name)
{
    if (strncmp(name, "Data/", 5) == 0) {
        return name + 5;
    }
    if (strcmp(name, "Data") == 0) {
        return "";
    }
    return name;
}

/* the package layout ships its files in the prefix and has no exe-relative
 * files from an older release to migrate */
int portLegacyFiles(void)
{
    return 0;
}

const char *portDataPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortDataDir, portDataName(name));
}

const char *portCfgPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortCfgDir, name);
}

const char *portSharePath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortShareDir, name);
}

#endif /* GE_XDG_PATHS */

/* ==== roles that do not depend on the layout ============================= */

const char *portExePath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

FILE *portDataFopen(const char *name, const char *mode)
{
    return fopen(portDataPath(name), mode);
}

FILE *portCfgFopen(const char *name, const char *mode)
{
    return fopen(portCfgPath(name), mode);
}

/* Data/Logs/<name>, made on first use: the run log and crash.log live there,
 * beside the saves and profiles, not next to the exe. Its folder has its own
 * buffer, so asking for a log path never clobbers portDataPath's. */
const char *portLogPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    portEnsureDir(sPortLogDir);
    return portJoinName(path, sPortLogDir, name);
}

/* ==== startup =========================================================== */

/* always-on: log where each class of file lives this run */
void portPathsLogDirs(void)
{
#ifdef GE_XDG_PATHS
    portConLogf("ge: root: config=%s data=%s read-only=%s\n", sPortCfgDir, sPortDataDir, sPortShareDir);
#else
    portConLogf("ge: root: %s\n", sPortExeDir[0] != '\0' ? sPortExeDir : "(cwd)");
#endif
}

static void portPathsFinish(void)
{
#ifdef GE_XDG_PATHS
    char base[PORT_DIR_MAX];

    portFindShareDir();
    portXdgBase(base, sizeof(base), "XDG_CONFIG_HOME", ".config");
    portAppDir(sPortCfgDir, sizeof(sPortCfgDir), base);
    portXdgBase(base, sizeof(base), "XDG_DATA_HOME", ".local/share");
    portAppDir(sPortDataDir, sizeof(sPortDataDir), base);
    /* before the first midas.ini or save write: a fresh account has no ~/.config */
    portEnsureDir(sPortCfgDir);
    portEnsureDir(sPortDataDir);
    snprintf(sPortLogDir, sizeof(sPortLogDir), "%sLogs/", sPortDataDir);
    portPathsLogDirs();
#else
    snprintf(sPortLogDir, sizeof(sPortLogDir), "%sData/Logs/", sPortExeDir);
    portPathsLogDirs();
    portMigrateIni();
#endif
}

/* 1 when the exe dir is one the roles can use. The package layout reads the
 * shipped files from it, so a relative one would resolve against the cwd. */
static int portExeDirUsable(void)
{
#ifdef GE_XDG_PATHS
    return sPortExeDir[0] == '/';
#else
    return sPortExeDir[0] != '\0';
#endif
}

void portPathsInitFromDir(const char *dir)
{
    if (!portPathCpy(sPortExeDir, sizeof(sPortExeDir), dir != NULL ? dir : "", "")
            || !portAddSep(sPortExeDir, sizeof(sPortExeDir))) {
        portConLogf("ge: executable directory is too long\n");
#ifdef GE_XDG_PATHS
        exit(EXIT_FAILURE);
#else
        sPortExeDir[0] = '\0';   /* cwd fallback, as for an overlong argv[0] */
#endif
    }
    if (!portExeDirUsable()) {
#ifdef GE_XDG_PATHS
        /* nothing to read the prefix from: fail every lookup cleanly */
        portConLogf("ge: cannot tell where the executable is: %s is relative\n", sPortExeDir);
        snprintf(sPortExeDir, sizeof(sPortExeDir), "%s", PORT_EXE_DIR_UNKNOWN);
#else
        sPortExeDir[0] = '\0';   /* cwd-relative files, as before */
#endif
    }
    portPathsFinish();
}

void portPathsInit(const char *argv0)
{
    char file[PORT_DIR_MAX];
    char dir[PORT_DIR_MAX];
    int last = -1;
    int i;

#ifdef _WIN32
    /* authoritative on Windows regardless of how we were launched */
    DWORD n;

    (void) argv0;
    n = GetModuleFileNameA(NULL, file, sizeof(file) - 1);
    if (n == 0 || n >= sizeof(file) - 1) {
        file[0] = '\0';
    } else {
        file[n] = '\0';
    }
#else
    /* argv[0] is not authoritative on Linux (a PATH launch has no slash, and
     * launchers/symlinks point elsewhere), so use the /proc equivalent of
     * GetModuleFileNameA and keep argv[0] for other POSIX only. */
    ssize_t n = readlink("/proc/self/exe", file, sizeof(file) - 1);

    if (n > 0 && (size_t) n < sizeof(file) - 1) {
        file[n] = '\0';
    } else if (argv0 == NULL || snprintf(file, sizeof(file), "%s", argv0) >= (int) sizeof(file)) {
        file[0] = '\0';
    }
#endif
    /* keep the separator, drop the basename */
    for (i = 0; file[i] != '\0'; i++) {
        if (portIsSep(file[i])) {
            last = i;
        }
    }
    if (last < 0) {
        dir[0] = '\0';   /* no separator: no directory to use */
    } else {
        snprintf(dir, sizeof(dir), "%.*s", last + 1, file);
    }
    portPathsInitFromDir(dir);
}
