"""Exercise port_atomic_file.c without game assets or a full build.

Builds the helper and a tiny C harness with a host compiler and a *clean*
include path: only port/include is added, never the repo's shadow include/.
That keeps the helper honest about being usable outside the game.

Run with Python 3 and a host C compiler (CC, or clang/gcc on PATH).
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ATOMIC_SRC = ROOT / "port" / "src" / "port_atomic_file.c"
ATOMIC_INCLUDE = ROOT / "port" / "include"

HARNESS = r"""
#include "port_atomic_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

static int write_bytes(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    size_t wrote;

    if (f == NULL) return 0;
    wrote = fwrite(data, 1, n, f);
    if (fclose(f) != 0) return 0;
    return wrote == n;
}

static int read_bytes(const char *path, unsigned char *buf, size_t cap, size_t *out)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) return 0;
    n = fread(buf, 1, cap, f);
    if (fclose(f) != 0) return 0;
    *out = n;
    return 1;
}

static int read_text_trimmed(const char *path, char *buf, size_t cap)
{
    unsigned char raw[4096];
    size_t n = 0;
    size_t i;
    size_t o = 0;

    if (!read_bytes(path, raw, sizeof(raw), &n)) return 0;
    for (i = 0; i < n && o + 1 < cap; i++)
    {
        if (raw[i] != '\r') buf[o++] = (char) raw[i];
    }
    buf[o] = '\0';
    return 1;
}

static int exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

static void unlink_if_present(const char *path)
{
    if (exists(path)) remove(path);
}

static void check_bytes(const char *path, const void *want, size_t want_n, const char *what)
{
    unsigned char got[8192];
    size_t n = 0;

    CHECK(read_bytes(path, got, sizeof(got), &n), what);
    CHECK(n == want_n, what);
    if (n == want_n) CHECK(memcmp(got, want, want_n) == 0, what);
}

static void check_text(const char *path, const char *want, const char *what)
{
    char got[4096];

    CHECK(read_text_trimmed(path, got, sizeof(got)), what);
    CHECK(strcmp(got, want) == 0, what);
}

static int replace_text(const char *path, const char *text)
{
    PortAtomicFile af;
    FILE *f;
    int ok;

    portAtomicFileInit(&af);
    if (!portAtomicFileOpen(&af, path, 0)) return 0;
    f = portAtomicFileStream(&af);
    if (f == NULL || fputs(text, f) < 0 || ferror(f)) {
        portAtomicFileAbort(&af);
        return 0;
    }
    ok = portAtomicFileCommit(&af);
    return ok;
}

static int case_replace_text(void)
{
    unlink_if_present("config.ini");
    unlink_if_present("config.ini.tmp");
    CHECK(write_bytes("config.ini", "OLD", 3), "seed old text");
    CHECK(replace_text("config.ini", "NEW"), "replace text");
    check_bytes("config.ini", "NEW", 3, "new text present");
    CHECK(!exists("config.ini.tmp"), "temp removed after replace");
    return failures;
}

static int case_multiline_text(void)
{
    unlink_if_present("midas.ini");
    unlink_if_present("midas.ini.tmp");
    CHECK(write_bytes("midas.ini", "old=1\n", 6), "seed ini");
    CHECK(replace_text("midas.ini", "[display]\nmaxfps=60\n"), "replace multiline");
    check_text("midas.ini", "[display]\nmaxfps=60\n", "multiline text");
    CHECK(!exists("midas.ini.tmp"), "temp removed after multiline");
    return failures;
}

static int case_empty_file(void)
{
    unlink_if_present("empty.ini");
    unlink_if_present("empty.ini.tmp");
    CHECK(write_bytes("empty.ini", "old data", 8), "seed empty target");
    CHECK(replace_text("empty.ini", ""), "replace with empty");
    check_bytes("empty.ini", "", 0, "empty file");
    CHECK(!exists("empty.ini.tmp"), "temp removed after empty");
    return failures;
}

static int case_abort_preserves(void)
{
    PortAtomicFile af;
    FILE *f;

    unlink_if_present("abort.ini");
    unlink_if_present("abort.ini.tmp");
    CHECK(write_bytes("abort.ini", "ORIGINAL", 8), "seed abort target");
    portAtomicFileInit(&af);
    CHECK(portAtomicFileOpen(&af, "abort.ini", 0), "open abort temp");
    f = portAtomicFileStream(&af);
    CHECK(f != NULL, "stream");
    if (f != NULL) CHECK(fputs("PARTIAL", f) >= 0, "write partial");
    portAtomicFileAbort(&af);
    check_bytes("abort.ini", "ORIGINAL", 8, "abort keeps original");
    CHECK(!exists("abort.ini.tmp"), "temp removed after abort");
    return failures;
}

static int case_original_visible_before_commit(void)
{
    PortAtomicFile af;
    FILE *f;

    unlink_if_present("pending.ini");
    unlink_if_present("pending.ini.tmp");
    CHECK(write_bytes("pending.ini", "OLD", 3), "seed pending target");
    portAtomicFileInit(&af);
    CHECK(portAtomicFileOpen(&af, "pending.ini", 0), "open pending temp");
    f = portAtomicFileStream(&af);
    CHECK(f != NULL, "pending stream");
    if (f != NULL) CHECK(fputs("NEW", f) >= 0, "write pending");
    check_bytes("pending.ini", "OLD", 3, "destination untouched before commit");
    portAtomicFileAbort(&af);
    check_bytes("pending.ini", "OLD", 3, "destination still untouched after abort");
    return failures;
}

static int case_temp_reuse(void)
{
    unlink_if_present("reuse.ini");
    unlink_if_present("reuse.ini.tmp");
    CHECK(write_bytes("reuse.ini", "OLD", 3), "seed reuse target");
    CHECK(write_bytes("reuse.ini.tmp", "junk", 4), "seed stale temp");
    CHECK(replace_text("reuse.ini", "NEW"), "replace over stale temp");
    check_bytes("reuse.ini", "NEW", 3, "stale temp replaced");
    CHECK(!exists("reuse.ini.tmp"), "temp gone after reuse");
    return failures;
}

static int case_binary_512(void)
{
    unsigned char data[512];
    unsigned char got[512];
    size_t n = 0;
    PortAtomicFile af;
    FILE *f;
    int i;

    for (i = 0; i < 512; i++) data[i] = (unsigned char) (i * 37 + 11);
    unlink_if_present("eeprom.bin");
    unlink_if_present("eeprom.bin.tmp");
    CHECK(write_bytes("eeprom.bin", "old-block", 9), "seed eeprom");
    portAtomicFileInit(&af);
    CHECK(portAtomicFileOpen(&af, "eeprom.bin", 1), "open eeprom temp");
    f = portAtomicFileStream(&af);
    CHECK(f != NULL, "eeprom stream");
    if (f != NULL) CHECK(fwrite(data, 1, sizeof(data), f) == sizeof(data), "write eeprom");
    CHECK(portAtomicFileCommit(&af), "commit eeprom");
    CHECK(read_bytes("eeprom.bin", got, sizeof(got), &n), "read eeprom");
    CHECK(n == sizeof(data), "eeprom size");
    if (n == sizeof(data)) CHECK(memcmp(got, data, sizeof(data)) == 0, "eeprom bytes");
    CHECK(!exists("eeprom.bin.tmp"), "eeprom temp gone");
    return failures;
}

static int case_open_failure(void)
{
    PortAtomicFile af;

    unlink_if_present("missing-dir");
    portAtomicFileInit(&af);
    CHECK(!portAtomicFileOpen(&af, "missing-dir/config.ini", 0), "missing dir refused");
    CHECK(portAtomicFileStage(&af) == PORT_ATOMIC_STAGE_OPEN, "missing dir stage");
    CHECK(portAtomicFileError(&af) != 0, "missing dir error");
    CHECK(strcmp(portAtomicFileStageName(portAtomicFileStage(&af)), "open") == 0, "stage name");
    portAtomicFileAbort(&af);
    return failures;
}

static int case_long_path(void)
{
    char path[PORT_ATOMIC_PATH_MAX + 32];
    PortAtomicFile af;
    int i;

    for (i = 0; i < (int) sizeof(path) - 1; i++) path[i] = 'a';
    path[sizeof(path) - 1] = '\0';
    portAtomicFileInit(&af);
    CHECK(!portAtomicFileOpen(&af, path, 0), "long path refused");
    CHECK(portAtomicFileError(&af) != 0, "long path error");
    return failures;
}

#ifndef _WIN32
static int case_replace_failure_preserves(void)
{
    PortAtomicFile af;
    FILE *f = NULL;
    int opened;
    int committed;

    unlink_if_present("locked");
    remove("locked/config.ini");
    remove("locked/config.ini.tmp");
    rmdir("locked");
    CHECK(mkdir("locked", 0700) == 0, "mkdir locked");
    CHECK(write_bytes("locked/config.ini", "ORIGINAL", 8), "seed locked target");

    portAtomicFileInit(&af);
    opened = portAtomicFileOpen(&af, "locked/config.ini", 0);
    CHECK(opened, "open locked temp");
    if (opened) {
        f = portAtomicFileStream(&af);
        CHECK(f != NULL, "locked stream");
        if (f != NULL) CHECK(fputs("NEW", f) >= 0, "write locked");
        CHECK(chmod("locked", 0500) == 0, "chmod locked dir");
        committed = portAtomicFileCommit(&af);
        CHECK(chmod("locked", 0700) == 0, "chmod locked dir back");
        CHECK(!committed, "commit refused in read-only dir");
        CHECK(portAtomicFileStage(&af) == PORT_ATOMIC_STAGE_REPLACE, "replace stage");
        check_bytes("locked/config.ini", "ORIGINAL", 8, "failed replace keeps original");
        CHECK(!exists("locked/config.ini.tmp"), "failed replace removes temp");
        portAtomicFileAbort(&af);
    }
    return failures;
}
#else
/* Hold the destination without FILE_SHARE_DELETE so MoveFileEx cannot replace
 * it. The original must still be there after the failed commit. */
static int case_replace_failure_preserves_win(void)
{
    PortAtomicFile af;
    FILE *f = NULL;
    HANDLE hold;
    int opened;
    int committed;

    unlink_if_present("locked.ini");
    unlink_if_present("locked.ini.tmp");
    CHECK(write_bytes("locked.ini", "ORIGINAL", 8), "seed locked target");

    portAtomicFileInit(&af);
    opened = portAtomicFileOpen(&af, "locked.ini", 0);
    CHECK(opened, "open locked temp");
    if (opened) {
        f = portAtomicFileStream(&af);
        CHECK(f != NULL, "locked stream");
        if (f != NULL) CHECK(fputs("NEW", f) >= 0, "write locked");
        hold = CreateFileA("locked.ini", GENERIC_READ, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(hold != INVALID_HANDLE_VALUE, "hold locked destination");
        committed = portAtomicFileCommit(&af);
        if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
        CHECK(!committed, "commit refused while destination is held");
        CHECK(portAtomicFileStage(&af) == PORT_ATOMIC_STAGE_REPLACE, "replace stage");
        check_bytes("locked.ini", "ORIGINAL", 8, "failed replace keeps original");
        CHECK(!exists("locked.ini.tmp"), "failed replace removes temp");
        portAtomicFileAbort(&af);
    }
    return failures;
}
#endif

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "replace_text") == 0)              case_replace_text();
    else if (strcmp(argv[1], "multiline_text") == 0)       case_multiline_text();
    else if (strcmp(argv[1], "empty_file") == 0)           case_empty_file();
    else if (strcmp(argv[1], "abort_preserves") == 0)      case_abort_preserves();
    else if (strcmp(argv[1], "visible_before_commit") == 0) case_original_visible_before_commit();
    else if (strcmp(argv[1], "temp_reuse") == 0)           case_temp_reuse();
    else if (strcmp(argv[1], "binary_512") == 0)            case_binary_512();
    else if (strcmp(argv[1], "open_failure") == 0)          case_open_failure();
    else if (strcmp(argv[1], "long_path") == 0)             case_long_path();
#ifndef _WIN32
    else if (strcmp(argv[1], "replace_failure") == 0)       case_replace_failure_preserves();
#else
    else if (strcmp(argv[1], "replace_failure_win") == 0)   case_replace_failure_preserves_win();
#endif
    else {
        fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 2;
    }

    return failures == 0 ? 0 : 1;
}
"""


def compiler_candidates():
    candidates = []
    cc = os.environ.get("CC")
    if cc:
        candidates.append(shlex.split(cc))
    for name in (("gcc", "clang", "cc") if os.name == "nt" else ("clang", "gcc", "cc")):
        found = shutil.which(name)
        if found:
            candidates.append([found])
    return candidates


class AtomicFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="midas-atomic-")
        work = Path(cls.tmp.name)
        harness = work / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        binary = work / ("atomic_test.exe" if os.name == "nt" else "atomic_test")

        errors = []
        cls.binary = None
        for compiler in compiler_candidates():
            try:
                proc = subprocess.run(
                    compiler + [
                        "-std=c11",
                        "-I", str(ATOMIC_INCLUDE),
                        str(ATOMIC_SRC),
                        str(harness),
                        "-o", str(binary),
                    ],
                    capture_output=True,
                    text=True,
                )
            except OSError as exc:
                errors.append(f"{compiler[0]}: {exc}")
                continue
            if proc.returncode == 0:
                cls.binary = binary
                break
            errors.append(f"{compiler[0]}: {proc.stderr.strip() or proc.stdout.strip()}")

        if cls.binary is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest("could not compile port_atomic_file.c: " + " | ".join(errors))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, name):
        case_dir = Path(tempfile.mkdtemp(prefix=name + "-", dir=self.tmp.name))
        result = subprocess.run(
            [str(self.binary), name],
            cwd=case_dir,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"{name} failed\nstdout: {result.stdout}\nstderr: {result.stderr}",
        )

    def test_replace_text(self):
        self.run_case("replace_text")

    def test_multiline_text(self):
        self.run_case("multiline_text")

    def test_empty_replacement(self):
        self.run_case("empty_file")

    def test_abort_keeps_original(self):
        self.run_case("abort_preserves")

    def test_original_visible_until_commit(self):
        self.run_case("visible_before_commit")

    def test_stale_temp_is_reused(self):
        self.run_case("temp_reuse")

    def test_binary_eeprom_roundtrip(self):
        self.run_case("binary_512")

    def test_open_failure(self):
        self.run_case("open_failure")

    def test_long_path_refused(self):
        self.run_case("long_path")

    def test_replace_failure_keeps_original(self):
        if os.name == "nt":
            self.run_case("replace_failure_win")
            return
        if hasattr(os, "geteuid") and os.geteuid() == 0:
            self.skipTest("root ignores the read-only directory permission")
        self.run_case("replace_failure")


if __name__ == "__main__":
    unittest.main()
