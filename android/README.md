# Midas Android — development port

This directory is the Gradle shell around the shared native port. It is a
scaffold, not a finished Android release, based on
[Project Midas](https://github.com/murkantor/project-midas). Neither Android ABI
has a verified playable build yet. Do not distribute this as a finished port.

The ARM64 engine still mixes native pointers with 32-bit game addresses and
overlays ROM data with native structs. For example, `Gfx` becomes 16 bytes even
though ROM display-list commands are 8 bytes. Models, animations and audio banks
need explicit ROM-to-host representations; silencing conversion errors is not a
fix. ARM32 is only a separate bring-up experiment for devices that support it,
not a substitute for the ARM64 work required by 64-bit-only phones.

## Requirements

- JDK 17 (AGP 8.5.2 does not support JDK 25 as its launcher).
- Android SDK platform 34 and build-tools 34.0.0.
- NDK 26.2.11394342 and CMake 3.22.1 (both SDK-managed).
- An SDL2 source checkout at `third_party/SDL2` (the native build worker's
  `cmake/bootstrap-sdl2.sh` fetches the pinned revision). SDL is not vendored.
- `android/local.properties` with `sdk.dir=...` (ignored by git).

Gradle is pinned to 8.7 through the wrapper, and AGP to 8.5.2. The default ABI
is `arm64-v8a`; an earlier 32-bit device can be tested with:

```sh
JAVA_HOME=/path/to/jdk-17 ./gradlew -PmidasAbis=armeabi-v7a :app:assembleDebug
```

The game and SDL2 both come from the root `CMakeLists.txt`, and `GE_VIDEO=ON`
is set by `app/build.gradle`. `midasAbis` only selects the Gradle/CMake ABI; it
does not add a second ABI flag.

The APK ships only the small runtime text tables and notices (controller
mappings, `Data/README`, `Mods/README`, shader list, ini help, license and
third-party notices). `GameData` copies them into `getFilesDir()` before native
entry and uses a SHA-256 manifest to keep user-edited copies across updates. No
ROM, save, or `midas.ini` is packaged. On first native entry the shell adds
Android defaults (`mouse=0`, `p1_hybrid=0`, `pads=1`, borderless fullscreen,
`maxfps=60`) only where the user has not already set the key; all other native
options remain untouched.

## Build

```sh
cd android
JAVA_HOME=/path/to/jdk-17 ./gradlew :app:testDebugUnitTest
JAVA_HOME=/path/to/jdk-17 ./gradlew :app:assembleDebug
```

The APK is written to `app/build/outputs/apk/debug/`.

## ROM handling

`RomActivity` is the launcher. On first run it opens `ACTION_OPEN_DOCUMENT` and
takes a persistent read permission. The selected document is read once, checked
for an exact 12 MiB (0xC00000) size, converted from `.z64`/`.v64`/`.n64` order to
`.z64`, and compared against the stock USA SHA-1
`abe01e4aeb033b6c0836819f549c791b26cfde83`. Only then is it atomically installed
as `getFilesDir()/Data/baserom.u.z64`; a rejected or interrupted import never
replaces an existing ROM. `midas.ini` gets `[data] rom = Data/baserom.u.z64`
without changing other sections, and the source URI is kept in private
SharedPreferences for re-imports after a restart.

`MidasActivity` starts SDL with `--rom-file <filesDir>/Data/baserom.u.z64` and
adds the touch overlay. No storage permission is declared or requested.

## Checks without building the game

```sh
python -m unittest discover -s scripts/port -p 'test_*.py' -v
python scripts/port/check_android_entry.py
```

Run the Python commands from the repository root. Set `ANDROID_NDK_HOME` to
exercise the Android header probes as well. These checks cover platform helpers,
not the full engine or gameplay. Java tests and lint can run separately:

```sh
cd android
./gradlew :app:testDebugUnitTest :app:lintDebug
```

Optionally set `MIDAS_TEST_ROM_PATH` to your own local, stock USA `.z64` file and
use `--rerun-tasks` to exercise real ROM validation and all byte-order conversions.
The file is read locally and is never bundled or uploaded.

The touch overlay has a persistent HIDE/PAD toggle and long-press opacity control.
Its input is released on hide, resize, focus loss and pause. A virtual SDL pad
feeds the existing controller backend; touch supplements player 1 rather than
displacing a physical controller. Device input, graphics, audio and all PC options
still require end-to-end verification after the native engine builds.
