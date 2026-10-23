"""Compile the Android native entry point with the NDK, without ROM assets.

This is a compile-only check, not a game build or a 64-bit safety check.
Set ANDROID_NDK_HOME/ANDROID_NDK_ROOT, or install an NDK under ANDROID_HOME.
"""
import os
from pathlib import Path
import platform
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def find_ndk():
    for name in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        if os.environ.get(name):
            return Path(os.environ[name])
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if not sdk and os.name == "nt":
        sdk = str(Path(os.environ["LOCALAPPDATA"]) / "Android/Sdk")
    if sdk:
        candidates = list((Path(sdk) / "ndk").glob("*/source.properties"))
        if candidates:
            return max(candidates, key=lambda p: tuple(int(n) for n in p.parent.name.split("."))).parent
    raise SystemExit("No Android NDK found. Set ANDROID_NDK_HOME to an installed NDK.")


def main():
    ndk = find_ndk()
    host = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}.get(platform.system())
    if not host:
        raise SystemExit("Unsupported NDK host: " + platform.system())
    toolchain = ndk / "toolchains/llvm/prebuilt" / host
    compiler = toolchain / "bin" / ("clang.exe" if os.name == "nt" else "clang")
    definitions = (
        "VERSION_US", "LANG_US", "REFRESH_NTSC", "LEFTOVERDEBUG", "LEFTOVERSPECTRUM",
        "BUGFIX_R0", "NONMATCHING", "AVOID_UB", "_LANGUAGE_C", "GE_VIDEO=1",
    )
    includes = (".", "include", "include/ultra64", "include/PR", "src", "src/game",
                "src/inflate", "port/include", "src/libultra")
    command = [str(compiler), "--target=aarch64-linux-android26",
               "--sysroot=" + str(toolchain / "sysroot"), "-c", "-fPIC", "-std=c11",
               "-fms-extensions", "-ffreestanding"]
    command += ["-D" + definition for definition in definitions]
    command += ["-I" + str(ROOT / include) for include in includes]
    command += [str(ROOT / "port/src/main.c")]
    with tempfile.TemporaryDirectory(prefix="midas-entry-") as temp:
        obj = Path(temp) / "main.o"
        subprocess.run(command + ["-o", str(obj)], cwd=ROOT, check=True)
        nm = toolchain / "bin" / ("llvm-nm.exe" if os.name == "nt" else "llvm-nm")
        symbols = subprocess.check_output([str(nm), "--defined-only", str(obj)], text=True)
        names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
        if "SDL_main" not in names or "main" in names:
            raise SystemExit("Android entry must export SDL_main, not a desktop main symbol.")
    print("Android ARM64 entry-point compile and SDL_main symbol check passed (not a full game build).")


if __name__ == "__main__":
    main()
