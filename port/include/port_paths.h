#ifndef PORT_PATHS_H
#define PORT_PATHS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* port_paths.c: where the game reads and writes its files. The default layout
 * keeps everything next to the executable; -DGE_XDG_PATHS (the Linux package
 * build) reads the shipped files from the prefix and writes the rest to the
 * XDG base directories. Each role has its own buffer, so a path stays valid
 * across another role's call; the next call to the SAME function reuses it. */

/* a base directory with its trailing separator, and dir + file name */
#define PORT_DIR_MAX  1024
#define PORT_PATH_MAX 1200

/* once, before the first lookup (main.c): finds the exe dir from argv[0] */
void portPathsInit(const char *argv0);
/* the same, from a known directory (tests, probes) */
void portPathsInitFromDir(const char *dir);
/* the always-on "ge: root: ..." line: repeated into ge_direct.log, which a
 * GUI-subsystem Windows launch opens after startup */
void portPathsLogDirs(void);

/* user files: saves, profiles, weapon sets, match settings, textures, the
 * cart image, shaders.txt, gamecontrolleruser.txt */
const char *portDataPath(const char *name);
FILE *portDataFopen(const char *name, const char *mode);
/* midas.ini */
const char *portCfgPath(const char *name);
FILE *portCfgFopen(const char *name, const char *mode);
/* the read-only files the package ships: gamecontrollerdb.txt,
 * gamecontrollerdefault.txt, Data/shaders_base.txt */
const char *portSharePath(const char *name);
/* the exe directory itself (the dev tree's ROM fallback) */
const char *portExePath(const char *name);
/* Data/Logs/<name>, created on first use */
const char *portLogPath(const char *name);

/* 1 when this layout can still hold files from an older release next to the
 * exe, 0 for a package install: the one-time migrations ask, so their callers
 * need no #ifdef of their own */
int portLegacyFiles(void);

/* mkdir -p one directory, logging what it makes. 1 once path is a directory,
 * 0 when it could not be made (it says why); creates nothing for a NULL or
 * empty path. */
int portEnsureDir(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PATHS_H */
