"""Exercise main.c's storage-root logic without linking game code or ROM assets.

Run with Python 3 and a host C compiler (CC, or clang/gcc on PATH).
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AndroidRootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            raise unittest.SkipTest("A host C compiler is required")
        source = (ROOT / "port/src/main.c").read_text(encoding="utf-8")
        start = source.index("static void portInitRootDir(")
        end = source.index("FILE *portRootFopen(", start)
        cls.tmp = tempfile.TemporaryDirectory(prefix="midas-root-")
        directory = Path(cls.tmp.name)
        test = directory / "root.c"
        test.write_text(
            '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
            '#define __ANDROID__ 1\n'
            'static char sPortRootDir[1024];\n'
            'static const char *storage;\n'
            'const char *SDL_AndroidGetInternalStoragePath(void) { return storage; }\n'
            '#define portConLogf(...) ((void) 0)\n'
            + source[start:end]
            + '\nint main(int argc, char **argv) {\n'
            ' storage = argc > 1 ? argv[1] : NULL;\n'
            ' portInitRootDir("/system/bin/app_process64");\n'
            ' puts(portRootPath("midas.ini"));\n'
            ' puts(portRootPath("Data/Saves/eeprom.bin"));\n'
            ' return 0;\n}\n',
            encoding="utf-8",
        )
        cls.binary = directory / ("root.exe" if os.name == "nt" else "root")
        subprocess.run([compiler, "-std=c11", str(test), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_root(self, *args):
        return subprocess.run([str(self.binary), *args], capture_output=True, text=True)

    def test_private_storage_not_app_process_directory(self):
        result = self.run_root("/data/user/0/org.midas.android/files")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.splitlines(), [
            "/data/user/0/org.midas.android/files/midas.ini",
            "/data/user/0/org.midas.android/files/Data/Saves/eeprom.bin",
        ])

    def test_null_empty_relative_and_overlong_paths_fail_closed(self):
        for args in [(), ("",), ("relative",), ("/" + "x" * 1024,)]:
            with self.subTest(args=args):
                result = self.run_root(*args)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")

    def test_spaces_and_unicode_are_preserved(self):
        result = self.run_root("/data/game files/é")
        self.assertEqual(result.returncode, 0)
        self.assertIn("/data/game files/é/midas.ini", result.stdout)


if __name__ == "__main__":
    unittest.main()
