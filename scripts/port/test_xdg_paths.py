#!/usr/bin/env python3
"""Exercise the two file layouts port_paths.c implements: the default one
(everything next to the executable) and -DGE_XDG_PATHS (read-only prefix +
XDG user dirs). No ROM, no game build: the path code is its own translation
unit, so this compiles it with a stub portConLogf. When CMake is available,
staged probes also exercise the real install rules and compile definitions.

Usage:  python3 scripts/port/test_xdg_paths.py
"""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATHS_C = ROOT / "port/src/port_paths.c"
PATHS_H = ROOT / "port/include"

# What the probe prints, in order: which base each role uses is the whole point
# of port_paths.c, so every role gets its own line. The last line retains the
# saves pointer across a log lookup: another role must not clobber its buffer.
ROLES = ("cfg", "saves", "shaders", "layouts", "mappingdb", "shaderbase", "rom", "log",
         "saves_again")

PROBE = r'''
#include <stdarg.h>
#include <stdio.h>
#include "port_paths.h"

/* port_console.c in the real build */
void portConLogf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void print_roles(void)
{
    const char *saves;

    puts(portCfgPath("midas.ini"));
    puts(portDataPath("Data/Saves/eeprom.bin"));
    puts(portDataPath("Data/shaders.txt"));
    puts(portDataPath("gamecontrolleruser.txt"));
    puts(portSharePath("gamecontrollerdb.txt"));
    puts(portSharePath("Data/shaders_base.txt"));
    puts(portExePath("baserom.u.z64"));
    saves = portDataPath("Data/Saves/eeprom.bin");
    puts(portLogPath("test.log"));
    puts(saves);   /* unchanged by portLogPath */
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    if (argv[1][0] == '-') {
        portPathsInit(argv[0]);   /* the real entry point: /proc/self/exe */
    } else {
        portPathsInitFromDir(argv[1]);
    }
    print_roles();
    return 0;
}
'''


def compile_probe(source, binary, flags):
    command = [*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-Wall", "-Wextra",
               "-Werror", "-Wno-unused-parameter", f"-I{PATHS_H}", *flags,
               str(source), str(PATHS_C), "-o", str(binary)]
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{shlex.join(command)}\n{result.stdout}{result.stderr}")


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux only: /proc/self/exe")
class LayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        build = Path(cls.build.name)
        cls.source = build / "probe.c"
        cls.source.write_text(PROBE)
        cls.probes = {}
        # the baked share paths are CMake's defaults, plus a variant for a
        # packager whose bindir is not <prefix>/bin
        variants = {
            "default": [],
            "xdg": ["-DGE_XDG_PATHS"],
            "xdg_libexec": [
                "-DGE_XDG_PATHS",
                '-DGE_XDG_SHARE_REL="../../../share/project-midas"',
                '-DGE_XDG_SHARE_GAMES_REL="../../../share/games/project-midas"',
            ],
        }
        for layout, flags in variants.items():
            binary = build / f"probe_{layout}"
            # no -w, and warnings are errors: the layout code stays warning-free
            compile_probe(cls.source, binary, flags)
            cls.probes[layout] = binary

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "install/bin"
        self.root.mkdir(parents=True)
        self.home = self.base / "home"
        self.bases = {
            "XDG_CONFIG_HOME": self.base / "config",
            "XDG_DATA_HOME": self.base / "data",
            "XDG_STATE_HOME": self.base / "state",
        }

    # -- helpers ---------------------------------------------------------
    def env(self, overrides=None):
        values = dict(self.bases)
        values["HOME"] = self.home
        values.update(overrides or {})
        run_env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_")}
        run_env.pop("HOME", None)
        for key, value in values.items():
            if value is None:
                run_env.pop(key, None)
            else:
                run_env[key] = str(value)
        return run_env

    def run_probe(self, layout="xdg", exe_dir=None, env=None):
        return subprocess.run([str(self.probes[layout]), str(exe_dir or self.root)],
                              env=self.env(env), cwd=self.base, umask=0,
                              text=True, capture_output=True)

    def paths(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), len(ROLES), lines)
        got = dict(zip(ROLES, lines))
        self.assertEqual(got.pop("saves_again"), got["saves"],
                         "portLogPath must not clobber portDataPath's buffer")
        return got

    def xdg(self, name, *parts):
        return str(self.bases[name] / "project-midas" / Path(*parts))

    # -- the package layout ---------------------------------------------
    def test_xdg_layout_roles(self):
        got = self.paths(self.run_probe())
        self.assertEqual(got, {
            "cfg": self.xdg("XDG_CONFIG_HOME", "midas.ini"),
            "saves": self.xdg("XDG_DATA_HOME", "Saves/eeprom.bin"),
            "shaders": self.xdg("XDG_DATA_HOME", "shaders.txt"),
            "layouts": self.xdg("XDG_DATA_HOME", "gamecontrolleruser.txt"),
            # no share/ next to this exe: the exe dir is the read-only fallback
            "mappingdb": str(self.root / "gamecontrollerdb.txt"),
            "shaderbase": str(self.root / "Data/shaders_base.txt"),
            "rom": str(self.root / "baserom.u.z64"),
            "log": self.xdg("XDG_DATA_HOME", "Logs/test.log"),
        })

    def test_xdg_creates_the_dirs_it_writes_to(self):
        # startup makes the two app dirs; the rest appears on first use, which
        # here means portLogPath("test.log")
        result = self.run_probe()
        got = self.paths(result)
        self.assertTrue(self.bases["XDG_CONFIG_HOME"].joinpath("project-midas").is_dir())
        self.assertTrue(self.bases["XDG_DATA_HOME"].joinpath("project-midas").is_dir())
        self.assertTrue(Path(got["log"]).parent.is_dir())
        self.assertIn("created", result.stderr,
                      "a created directory is reported once")
        self.assertNotIn("created", self.run_probe().stderr,
                         "and not again on the next run")
        self.assertFalse(self.bases["XDG_STATE_HOME"].exists(),
                         "the state dir is no longer used")

    def test_new_xdg_directories_are_private(self):
        self.paths(self.run_probe())
        directories = [self.bases["XDG_CONFIG_HOME"], self.bases["XDG_DATA_HOME"],
                       self.bases["XDG_CONFIG_HOME"] / "project-midas",
                       self.bases["XDG_DATA_HOME"] / "project-midas/Logs"]
        for directory in directories:
            with self.subTest(directory=directory):
                self.assertEqual(directory.stat().st_mode & 0o777, 0o700)

    def test_existing_xdg_directory_permissions_are_preserved(self):
        config = self.bases["XDG_CONFIG_HOME"] / "project-midas"
        config.mkdir(parents=True)
        config.chmod(0o750)
        self.paths(self.run_probe())
        self.assertEqual(config.stat().st_mode & 0o777, 0o750)

    def test_nested_dir_is_made_when_the_xdg_base_is_missing(self):
        # mkdir -p, not one level: nothing under $XDG_DATA_HOME exists yet
        got = self.paths(self.run_probe())
        self.assertTrue(self.bases["XDG_DATA_HOME"].joinpath("project-midas/Logs").is_dir())
        self.assertFalse(Path(got["saves"]).parent.exists(),
                         "Saves/ appears on the first save, not at startup")

    def test_share_dir_is_found_from_the_prefix(self):
        share = self.base / "install/share/project-midas"
        (share / "Data").mkdir(parents=True)
        got = self.paths(self.run_probe())
        self.assertEqual(got["mappingdb"], str(share / "gamecontrollerdb.txt"))
        self.assertEqual(got["shaderbase"], str(share / "Data/shaders_base.txt"))
        self.assertEqual(got["cfg"], self.xdg("XDG_CONFIG_HOME", "midas.ini"))

    def test_share_games_dir_is_the_second_choice(self):
        games = self.base / "install/share/games/project-midas"
        games.mkdir(parents=True)
        got = self.paths(self.run_probe())
        self.assertEqual(got["mappingdb"], str(games / "gamecontrollerdb.txt"))

    def test_share_dir_wins_over_share_games(self):
        share = self.base / "install/share/project-midas"
        (self.base / "install/share/games/project-midas").mkdir(parents=True)
        share.mkdir()
        got = self.paths(self.run_probe())
        self.assertEqual(got["mappingdb"], str(share / "gamecontrollerdb.txt"))

    def test_share_dir_of_a_deeper_bindir(self):
        # bindir=games: the exe sits in <prefix>/games, the data in share/games
        root = self.base / "usr/games"
        root.mkdir(parents=True)
        games = self.base / "usr/share/games/project-midas"
        games.mkdir(parents=True)
        got = self.paths(self.run_probe(exe_dir=root))
        self.assertEqual(got["mappingdb"], str(games / "gamecontrollerdb.txt"))

    def test_the_baked_share_path_is_used_wherever_the_exe_sits(self):
        # -DGE_XDG_SHARE_REL: the packager's bindir is not <prefix>/bin, so the
        # relative path is longer and the default one would not match
        bin = self.base / "usr/lib/games/midas"
        bin.mkdir(parents=True)
        share = self.base / "usr/share/project-midas"
        share.mkdir(parents=True)
        got = self.paths(self.run_probe(layout="xdg_libexec", exe_dir=bin))
        self.assertEqual(got["mappingdb"], str(share / "gamecontrollerdb.txt"))

    def test_startup_finds_the_exe_dir_on_its_own(self):
        # portPathsInit (the real entry point) reads /proc/self/exe, so move the
        # probe into a prefix and run it from anywhere
        probe = self.root / "probe"
        probe.write_bytes(self.probes["xdg"].read_bytes())
        probe.chmod(0o755)
        share = self.base / "install/share/project-midas"
        share.mkdir(parents=True)
        result = subprocess.run([str(probe), "-"], env=self.env(), cwd="/",
                                text=True, capture_output=True)
        got = self.paths(result)
        self.assertEqual(got["mappingdb"], str(share / "gamecontrollerdb.txt"))
        self.assertEqual(got["cfg"], self.xdg("XDG_CONFIG_HOME", "midas.ini"))
        self.assertIn(f"read-only={share}", result.stderr)

    def test_startup_follows_symlinks_and_path(self):
        probe = self.root / "probe\\name"
        probe.write_bytes(self.probes["xdg"].read_bytes())
        probe.chmod(0o755)
        links = self.base / "links"
        links.mkdir()
        link = links / "midas-probe"
        link.symlink_to(probe)
        share = self.base / "install/share/project-midas"
        share.mkdir(parents=True)
        for command in (str(link), link.name):
            with self.subTest(command=command):
                result = subprocess.run([command, "-"], env=self.env({"PATH": links}),
                                        cwd="/", text=True, capture_output=True)
                got = self.paths(result)
                self.assertEqual(got["mappingdb"], str(share / "gamecontrollerdb.txt"))
                self.assertEqual(got["rom"], str(self.root / "baserom.u.z64"))

    def test_overlong_resolved_share_directory_fails_loudly(self):
        target = self.base / "long"
        while len(str(target)) < 1250:
            target /= "a" * 200
        target.mkdir(parents=True)
        share = self.base / "install/share/project-midas"
        share.parent.mkdir()
        share.symlink_to(target, target_is_directory=True)
        result = self.run_probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shared data directory is too long", result.stderr)
        self.assertFalse(self.bases["XDG_CONFIG_HOME"].exists())

    def test_relative_exe_dir_falls_back_to_a_dead_path(self):
        result = self.run_probe(exe_dir="relative/bin")
        got = self.paths(result)
        self.assertEqual(got["mappingdb"], "/nonexistent/gamecontrollerdb.txt")
        self.assertIn("is relative", result.stderr)

    def test_posix_backslash_is_part_of_the_exe_directory(self):
        root = self.base / "portable\\"
        root.mkdir()
        for layout in ("default", "xdg"):
            with self.subTest(layout=layout):
                got = self.paths(self.run_probe(layout, exe_dir=root))
                self.assertEqual(got["rom"], str(root / "baserom.u.z64"))
                self.assertEqual(got["mappingdb"], str(root / "gamecontrollerdb.txt"))
        self.assertFalse((self.base / "portable").exists())

    def test_overlong_exe_directory_is_not_silently_truncated(self):
        for length in (1023, 1100):
            with self.subTest(length=length):
                result = self.run_probe(exe_dir="/" + "a" * (length - 1))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("too long", result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertFalse(self.bases["XDG_CONFIG_HOME"].exists())

    # -- XDG base directory rules ---------------------------------------
    def test_absolute_xdg_bases_win_and_need_no_home(self):
        got = self.paths(self.run_probe(env={"HOME": None}))
        self.assertEqual(got["cfg"], self.xdg("XDG_CONFIG_HOME", "midas.ini"))
        self.assertEqual(got["saves"], self.xdg("XDG_DATA_HOME", "Saves/eeprom.bin"))

    def test_relative_or_empty_bases_fall_back_to_home(self):
        for value in (None, "", "relative"):
            with self.subTest(value=value):
                got = self.paths(self.run_probe(env={"XDG_CONFIG_HOME": value}))
                self.assertEqual(got["cfg"],
                                 str(self.home / ".config/project-midas/midas.ini"))

    def test_relative_base_is_reported(self):
        result = self.run_probe(env={"XDG_DATA_HOME": "relative"})
        self.paths(result)
        self.assertIn("ignoring relative XDG_DATA_HOME", result.stderr)

    def test_no_home_and_no_xdg_base_fails_before_writing(self):
        result = self.run_probe(env={"HOME": None, "XDG_CONFIG_HOME": None})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("XDG_CONFIG_HOME", result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertFalse(self.bases["XDG_DATA_HOME"].exists(),
                         "nothing may be created when startup fails")

    def test_overlong_base_fails_loudly(self):
        result = self.run_probe(env={"XDG_CONFIG_HOME": "/" + "a" * 1100})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("too long", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_posix_backslash_does_not_create_extra_directories(self):
        config = self.base / "config\\home"
        data = self.base / "data\\home"
        got = self.paths(self.run_probe(env={"XDG_CONFIG_HOME": config, "XDG_DATA_HOME": data}))
        self.assertEqual(got["cfg"], str(config / "project-midas/midas.ini"))
        self.assertTrue((config / "project-midas").is_dir())
        self.assertTrue((data / "project-midas/Logs").is_dir())
        self.assertFalse((self.base / "config").exists())
        self.assertFalse((self.base / "data").exists())

    # -- CMake's real install rules and baked paths, still without a ROM --
    def check_cmake_install(self, options, bindir, datadir, docdir, destdir=False):
        build = self.base / "build"
        command = ["cmake", "-S", str(ROOT), "-B", str(build), "-DGE_HEADLESS=ON",
                   "-DGE_XDG_PATHS=ON", "-DCMAKE_INSTALL_PREFIX=/usr",
                   "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", *options]
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        commands = json.loads((build / "compile_commands.json").read_text())
        entry = next(c for c in commands if Path(c["file"]) == PATHS_C)
        args = entry.get("arguments") or shlex.split(entry["command"])
        flags = [arg for arg in args if arg.startswith("-DGE_XDG_")]
        self.assertIn("-DGE_XDG_PATHS=1", flags)
        # Install a path probe in place of the game: the generated CMake rules
        # and compile definitions are real, but no assets or game build are needed.
        compile_probe(self.source, build / "ProjectMidas", flags)
        prefix = self.base / "package"
        install_env = dict(os.environ)
        install_env.pop("DESTDIR", None)
        command = ["cmake", "--install", str(build)]
        if destdir:
            install_env["DESTDIR"] = str(prefix)
        else:
            command += ["--prefix", str(prefix)]
        result = subprocess.run(command, env=install_env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        binary = prefix / bindir / "ProjectMidas"
        result = subprocess.run([str(binary), "-"], env=self.env(), cwd="/",
                                text=True, capture_output=True)
        got = self.paths(result)
        for role, name in (("mappingdb", "gamecontrollerdb.txt"),
                           ("shaderbase", "Data/shaders_base.txt")):
            expected = prefix / datadir / "project-midas" / name
            self.assertEqual(got[role], str(expected))
            self.assertTrue(expected.is_file())
        self.assertEqual(got["cfg"], self.xdg("XDG_CONFIG_HOME", "midas.ini"))
        self.assertTrue((prefix / docdir / "midas_ini_help.txt").is_file())
        self.assertTrue((prefix / datadir / "project-midas/gamecontrollerdefault.txt").is_file())

    @unittest.skipUnless(shutil.which("cmake"), "CMake not installed")
    def test_cmake_default_install_is_relocatable(self):
        self.check_cmake_install([], "bin", "share", "share/doc/project-midas")

    @unittest.skipUnless(shutil.which("cmake"), "CMake not installed")
    def test_cmake_respects_custom_install_directories(self):
        self.check_cmake_install(["-DCMAKE_INSTALL_BINDIR=libexec/project-midas",
                                  "-DCMAKE_INSTALL_DATADIR=share/games",
                                  "-DCMAKE_INSTALL_DOCDIR=share/doc/custom-midas"],
                                 "libexec/project-midas", "share/games", "share/doc/custom-midas")

    @unittest.skipUnless(shutil.which("cmake"), "CMake not installed")
    def test_cmake_absolute_install_directories_with_destdir(self):
        self.check_cmake_install(["-DCMAKE_INSTALL_BINDIR=/usr/games",
                                  "-DCMAKE_INSTALL_DATADIR=/usr/share/games",
                                  "-DCMAKE_INSTALL_DOCDIR=/usr/share/doc/custom-midas"],
                                 "usr/games", "usr/share/games", "usr/share/doc/custom-midas",
                                 destdir=True)

    # -- no exe-relative migration --------------------------------------
    def test_exe_relative_files_are_not_migrated(self):
        (self.root / "ge.ini").write_text("stale")
        self.paths(self.run_probe())
        self.assertEqual((self.root / "ge.ini").read_text(), "stale")

    # -- the default layout ---------------------------------------------
    def test_default_layout_keeps_everything_next_to_the_exe(self):
        (self.root / "Data").mkdir()
        got = self.paths(self.run_probe("default"))
        self.assertEqual(got, {
            "cfg": str(self.root / "midas.ini"),
            "saves": str(self.root / "Data/Saves/eeprom.bin"),
            "shaders": str(self.root / "Data/shaders.txt"),
            "layouts": str(self.root / "gamecontrolleruser.txt"),
            "mappingdb": str(self.root / "gamecontrollerdb.txt"),
            "shaderbase": str(self.root / "Data/shaders_base.txt"),
            "rom": str(self.root / "baserom.u.z64"),
            "log": str(self.root / "Data/Logs/test.log"),
        })

    def test_default_layout_creates_the_log_folder(self):
        got = self.paths(self.run_probe("default"))
        directory = Path(got["log"]).parent
        self.assertTrue(directory.is_dir(), "Data/Logs/ appears on first use")
        self.assertEqual(directory.stat().st_mode & 0o777, 0o755)

    def test_default_layout_ignores_the_xdg_variables(self):
        got = self.paths(self.run_probe("default", env={"HOME": None}))
        self.assertEqual(got["cfg"], str(self.root / "midas.ini"))
        self.assertTrue(all(not path.exists() for path in self.bases.values()))

    def test_default_layout_renames_an_old_ge_ini(self):
        (self.root / "ge.ini").write_text("old settings")
        self.paths(self.run_probe("default"))
        self.assertEqual((self.root / "midas.ini").read_text(), "old settings")
        self.assertFalse((self.root / "ge.ini").exists())

    def test_default_layout_keeps_an_existing_midas_ini(self):
        (self.root / "midas.ini").write_text("current")
        (self.root / "ge.ini").write_text("old")
        self.paths(self.run_probe("default"))
        self.assertEqual((self.root / "midas.ini").read_text(), "current")
        self.assertTrue((self.root / "ge.ini").exists())


if __name__ == "__main__":
    unittest.main()
