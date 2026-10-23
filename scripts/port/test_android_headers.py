"""Header-hygiene checks for the arm64 Android build (Phase 0).

These tests compile tiny probes rather than game TUs: the point is to prove the
host libc surface the decompiled code relies on is declared correctly under
bionic/Clang, and that the N64 preprocessor branch is untouched. The NDK tests
skip when no NDK is installed; the desktop and TARGET_N64 checks only need a
host C compiler.

Run with: python3 scripts/port/test_android_headers.py
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

INCLUDES = [
    "-I" + str(ROOT),
    "-I" + str(ROOT / "include"),
    "-I" + str(ROOT / "include/ultra64"),
    "-I" + str(ROOT / "include/PR"),
]


def _host_compiler():
    return os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")


def _ndk_root():
    for name in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        value = os.environ.get(name)
        if value and Path(value).is_dir():
            return Path(value)
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if not sdk:
        return None
    ndk_dir = Path(sdk) / "ndk"
    if not ndk_dir.is_dir():
        return None
    candidates = [p for p in ndk_dir.iterdir() if p.is_dir()]
    # r26+ is what cmake/android-arm64.cmake documents; prefer the newest.
    candidates.sort(key=lambda p: p.name, reverse=True)
    return candidates[0] if candidates else None


def _ndk_clang():
    ndk = _ndk_root()
    if ndk is None:
        return None
    if os.name == "nt":
        host = "windows-x86_64"
        exe = "clang.exe"
    elif hasattr(os, "uname") and os.uname().sysname == "Darwin":
        host = "darwin-x86_64"
        exe = "clang"
    else:
        host = "linux-x86_64"
        exe = "clang"
    path = ndk / "toolchains/llvm/prebuilt" / host / "bin" / exe
    return str(path) if path.is_file() else None


class _ProbeCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="midas-headers-")
        cls.directory = Path(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def write_probe(self, name, body):
        path = self.directory / name
        path.write_text(body, encoding="utf-8")
        return path

    def run_compiler(self, compiler, source, ndk=False, extra=()):
        if ndk:
            command = [
                compiler,
                "--target=aarch64-none-linux-android26",
                "-std=gnu11",
                "-fsyntax-only",
                "-O2",
                "-D_FORTIFY_SOURCE=2",
                "-ffreestanding",
                "-D_LANGUAGE_C",
            ]
        else:
            command = [compiler, "-std=gnu11", "-fsyntax-only", "-D_LANGUAGE_C"]
        command += INCLUDES + list(extra) + [str(source)]
        return subprocess.run(command, capture_output=True, text=True)


class AndroidHeaderTests(_ProbeCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.ndk_clang = _ndk_clang()
        if not cls.ndk_clang:
            raise unittest.SkipTest("No NDK clang found (set ANDROID_NDK_HOME)")

    def compile(self, source, extra=()):
        return self.run_compiler(self.ndk_clang, source, ndk=True, extra=extra)

    def test_limits_ssize_max_with_freestanding(self):
        """bionic's fortify unistd.h needs SSIZE_MAX even though -ffreestanding
        makes clang's own limits.h skip the hosted header."""
        probe = self.write_probe(
            "limits.c",
            "#include <limits.h>\n"
            "#include <unistd.h>\n"
            "int probe(void) { return SSIZE_MAX == LONG_MAX ? 1 : 0; }\n",
        )
        result = self.compile(probe)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_legacy_float_limits_survive_the_wrapper(self):
        """The SGI crop used to export FLT_MAX/DBL_MAX from <limits.h>; bgfog.c
        and other decomp TUs include only <limits.h>, so the host wrapper must
        still provide the <float.h> surface."""
        probe = self.write_probe(
            "floatlimits.c",
            "#include <limits.h>\n"
            "int probe(void) {\n"
            "  return (FLT_MAX > 0.0f) + (FLT_MIN > 0.0f) + (FLT_DIG > 0) +\n"
            "         (DBL_MAX > 0.0) + (DBL_MIN > 0.0) + (DBL_DIG > 0);\n"
            "}\n",
        )
        result = self.compile(probe)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_string_surface_is_declared(self):
        probe = self.write_probe(
            "strings.c",
            "#include <string.h>\n"
            "int probe(const char *a, const char *b, void *p, int n) {\n"
            "  memcpy(p, a, (size_t)n); memmove(p, a, (size_t)n);\n"
            "  memset(p, 0, (size_t)n); (void)memcmp(a, b, (size_t)n);\n"
            "  (void)memchr(a, 0, (size_t)n); (void)strcpy((char *)p, a);\n"
            "  (void)strncpy((char *)p, a, (size_t)n); (void)strcat((char *)p, a);\n"
            "  (void)strncat((char *)p, a, (size_t)n); (void)strcmp(a, b);\n"
            "  (void)strncmp(a, b, (size_t)n); (void)strlen(a);\n"
            "  (void)strchr(a, 'x'); (void)strrchr(a, 'x'); (void)strstr(a, b);\n"
            "  (void)strspn(a, b); (void)strcspn(a, b); (void)strerror(0);\n"
            "  return 0;\n"
            "}\n",
        )
        result = self.compile(probe)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_os_bcopy_bzero_are_usable(self):
        """PR/os.h must defer to bionic <strings.h> rather than redeclaring the
        int forms; calls in fr.c/ob.c/zlib.c expand the bionic macros."""
        probe = self.write_probe(
            "osstrings.c",
            "#include <ultra64.h>\n"
            "void probe(const void *s, void *d, int n) { bcopy(s, d, n); bzero(d, n); }\n",
        )
        result = self.compile(probe)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_repo_string_then_bionic_string(self):
        """SDL drags in the hosted <string.h>/<strings.h> after the repo stub;
        the two declaration sets must agree (no bcopy/bzero redeclaration)."""
        probe = self.write_probe(
            "duplicate.c",
            "#include <string.h>\n"
            "#include <strings.h>\n"
            "#include <string.h>\n"
            "int probe(const char *a, const char *b) { return strcmp(a, b); }\n",
        )
        result = self.compile(probe)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_weak_alias_files_use_clang_safe_form(self):
        """#pragma weak variable aliases do not resolve under Clang/ELF; the
        two files use a macro in that branch and keep the pragma for GCC."""
        lv = (ROOT / "src/game/lv.c").read_text(encoding="utf-8")
        spectrum = (ROOT / "src/game/spectrum.c").read_text(encoding="utf-8")
        self.assertIn("#if defined(__clang__)", lv)
        self.assertIn("#define g_DebugPortalsInputBufferSource1 g_DebugPortalsInputBuffer1", lv)
        self.assertIn("#if defined(__clang__)", spectrum)
        self.assertIn("#define spec_keyboard_row_caps_z_x_c_v spec_keyboard_buffer[0]", spectrum)


class HostBranchTests(_ProbeCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.compiler = _host_compiler()
        if not cls.compiler:
            raise unittest.SkipTest("A host C compiler is required")

    def test_n64_branch_still_defines_limits(self):
        """TARGET_N64 must keep the SGI crop: no include_next, LONG_MAX from
        _MIPS_SZLONG, exactly as the N64 Makefile build expects. -nostdinc
        proves the header does not depend on a hosted limits.h."""
        probe = self.write_probe(
            "n64_limits.c",
            "#include <limits.h>\n"
            "#ifndef LONG_MAX\n#error LONG_MAX missing\n#endif\n"
            "#ifndef ULONG_MAX\n#error ULONG_MAX missing\n#endif\n",
        )
        result = subprocess.run(
            [self.compiler, "-std=gnu11", "-E", "-dM", "-nostdinc",
             "-DTARGET_N64", "-D_MIPS_SZLONG=32", "-D_LANGUAGE_C",
             "-I" + str(ROOT / "include"), str(probe)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("#define LONG_MAX 2147483647", result.stdout)

    def test_desktop_headers_compile(self):
        """The x86 desktop build also uses -ffreestanding; limits.h must still
        provide the legacy float surface and the string declarations."""
        probe = self.write_probe(
            "host.c",
            "#include <limits.h>\n"
            "#include <string.h>\n"
            "int probe(const char *a, const char *b, void *p, int n) {\n"
            "  memset(p, 0, (size_t)n); memmove(p, a, (size_t)n);\n"
            "  return strcmp(a, b) + (int)strlen(a) + (FLT_MAX > 0.0f ? 0 : 1) +\n"
            "         (LONG_MAX > 0 ? 0 : 1);\n"
            "}\n",
        )
        result = self.run_compiler(self.compiler, probe, extra=("-ffreestanding",))
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
