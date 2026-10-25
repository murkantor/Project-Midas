# Building Project Midas

Project Midas has two independent build systems:

- **PC port (this document):** CMake. Three targets — native macOS, 32-bit
  Linux, 32-bit Windows (cross-compiled with MinGW).
- **N64 ROM:** the classic `Makefile` (`make VERSION=US`, `allbuild.sh`). Not
  covered here.

You need your own GoldenEye 007 (USA) cart image to *run* the game. macOS
reads it at runtime; the Linux/Windows builds also read it at build time, to
extract assets.

```
sha1: abe01e4aeb033b6c0836819f549c791b26cfde83  (12,582,912 bytes)
```

Accepted formats are `.z64` (big-endian), `.v64` (byte-swapped) and `.n64`
(word-swapped); the name does not matter, the game checks the header. See
`tools/Data_README.txt` for where the game looks for it.

---

## macOS (native 64-bit)

Requirements:

```sh
xcode-select --install          # clang, linker, SDK
brew install cmake ninja sdl2
```

SDL2 must be built for the same architecture as the compiler. On Apple Silicon
that is arm64; Intel Macs use the same native 64-bit code path as x86_64.

Configure and build:

```sh
cmake -S . -B build_macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGE_VIDEO=ON -DGE_PORT_TESTS=ON
cmake --build build_macos --parallel
```

The executable is `build_macos/ProjectMidas`. CMake copies the support files
it needs beside it (`gamecontrollerdb.txt`, `gamecontrollerdefault.txt`,
`midas_ini_help.txt`, `Data/shaders_base.txt`) as a post-build step.

Run it with your cart:

```sh
./build_macos/ProjectMidas --rom-file "/path/to/GoldenEye 007 (USA).z64"
```

or put the image in `build_macos/Data/` and run `./build_macos/ProjectMidas`.

Notes:

- `GE_NATIVE_64` defaults to `ON` on macOS; the build refuses to configure with
  it off.
- No asset extraction, 32-bit toolchain, Docker, Rosetta or low-address memory
  reservation is needed. `GE_ROMDATA_FALLBACK` must stay `OFF` (the native
  build always loads everything from the ROM).
- Settings, saves and logs live next to the executable, not in the shell's
  working directory. Keep that directory writable.

### Tests (no ROM needed)

```sh
ctest --test-dir build_macos --output-on-failure
```

Seven synthetic-fixture tests (`native_layout`, `native_collision`,
`native_aim`, `native_stan`, `native_movement`, `native_prop_collision`,
`native_player`) run without a cart. They cover model/display-list
conversion, audio-bank and texture byte order, allocator behaviour across the
4 GiB boundary, collision, aiming, movement and animation identity. They do
**not** establish visual or gameplay parity — in-game testing is still needed.

### Sanitizer build

```sh
cmake -S . -B build_native_tests -G Ninja \
  -DGE_NATIVE_64=ON -DGE_PORT_TESTS=ON -DGE_SANITIZE=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_native_tests --target \
  native_layout_test native_collision_test native_aim_test native_stan_test \
  native_movement_test native_prop_collision_test native_player_test
ctest --test-dir build_native_tests --output-on-failure
```

---

## Linux (32-bit, in Docker)

The 32-bit build needs an i686 toolchain, i686 development libraries and an
i686 SDL2. The repository's Dockerfile provides all three; the commands below
run inside a Linux shell (on Windows, use WSL).

1. Build the image once:

   ```sh
   docker build --network=host \
     --build-arg HOST_UID=$(id -u) --build-arg HOST_GID=$(id -g) \
     -t goldeneye .
   ```

2. Start a container in the repository:

   ```sh
   docker run --rm -it -v $(pwd):/home/dev/project goldeneye
   ```

3. Put `baserom.u.z64` in the repository root, then extract assets (once):

   ```sh
   ./scripts/extract_baserom.u.sh
   make convert_props convert_chrs convert_guns combine_images
   ```

4. Configure and build:

   ```sh
   cmake -B build_pc -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON \
     -DSDL2_LINUX_ROOT=$HOME/sdl2-linux-i686
   cmake --build build_pc -j$(nproc)
   ```

The executable is `build_pc/ProjectMidas`; the same support files are copied
beside it. It needs an i686 `libSDL2` on the machine that runs it.

## Windows (32-bit, cross-compiled with MinGW)

The Windows build is produced on Linux — either in the same Docker container
or on any host with `gcc-mingw-w64-i686`, `g++-mingw-w64-i686` and
`binutils-mingw-w64-i686`.

1. Get the SDL2 MinGW development package (version 2.30.11 as used by CI) and
   unpack it to `third_party/`, or pass its location as `SDL2_MINGW_ROOT`:

   ```sh
   wget -O sdl2-mingw.tar.gz \
     https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-devel-2.30.11-mingw.tar.gz
   mkdir -p third_party && tar -xzf sdl2-mingw.tar.gz -C third_party
   ```

2. Extract assets as in the Linux steps, then:

   ```sh
   cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
     -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON
   cmake --build build_win -j$(nproc)
   ```

The result is `build_win/ProjectMidas.exe`. Copy it next to `SDL2.dll`, the
controller files and `Data/` to run it.

---

## Clean build

CMake and Ninja keep everything in the build directory, so the reliable clean
is to delete it. The build directories in this repository are:

```
build_macos/    native macOS
build_pc/       32-bit Linux
build_win/      32-bit Windows cross-build
build_native_tests/   sanitizer test build
```

**Clean and rebuild one target** (use the directory you configured):

```sh
rm -rf build_macos
cmake -S . -B build_macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGE_VIDEO=ON -DGE_PORT_TESTS=ON
cmake --build build_macos --parallel
ctest --test-dir build_macos --output-on-failure
```

**Build-system clean only** (keeps the CMake cache and configuration):

```sh
cmake --build build_macos --target clean
cmake --build build_macos --parallel
```

**Clean everything generated in the tree:**

```sh
rm -rf build_macos build_pc build_win build_native_tests
```

This does not touch extracted assets (`assets/`, generated by
`scripts/extract_baserom.u.sh` and the `make convert_*` targets). Remove those
with the classic Makefile's target, or a fresh `git clean -xdf` if you want a
pristine checkout (that deletes your ROM copies too, so use `git clean -nd`
first to see what would go).

Note that `git clean -xdf` also removes `Data/` next to a build directory, which
holds saves and `midas.ini`. Back those up first if you care about them.

---

## CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `GE_VIDEO` | `OFF` | Build the fast3d renderer (SDL2 + OpenGL). Required to actually play. |
| `GE_NATIVE_64` | `ON` on macOS, `OFF` elsewhere | Native 64-bit pointers. Mandatory on macOS; rejected on Windows. |
| `GE_PORT_TESTS` | `OFF` | ROM-free tests; requires `GE_NATIVE_64`. |
| `GE_SANITIZE` | `OFF` | ASan + UBSan (implies headless unless `GE_VIDEO` is set). |
| `GE_HEADLESS` | `OFF` | No SDL at all: null renderer, silent audio. |
| `GE_UNLOCK_ALL` | `OFF` | All missions/difficulties selectable (testing). |
| `GE_ROMDATA_FALLBACK` | `OFF` | Keep compiled-in asset copies as a no-ROM fallback. Not allowed with `GE_NATIVE_64`. |
| `SDL2_MINGW_ROOT` | `third_party/SDL2-...` | Windows cross-build SDL2 location. |
| `SDL2_LINUX_ROOT` | system SDL2 | 32-bit Linux SDL2 prefix. |

---

## Troubleshooting

- **`call to undeclared function ...` right after a merge.** A merge conflict
  was resolved by dropping one side's hunk. Check the conflicted files against
  each parent (`git diff <parent> HEAD -- <file>`) before building.
- **`Found package configuration file ... SDL2` missing.** Install SDL2
  (`brew install sdl2`) or pass `CMAKE_PREFIX_PATH`. For the 32-bit Linux build
  use a self-built i686 prefix via `SDL2_LINUX_ROOT`.
- **Wrong architecture / `mach-o file, but is an incompatible architecture`.**
  The SDL2 you linked and the compiler disagree. Reinstall SDL2 for the host
  architecture and configure a fresh build directory.
- **`No GoldenEye 007 ROM found`.** The ROM is missing or is not the stock (U)
  cart. Check the log line starting `ge: romdata: ROM =`.
- **Stale or confusing build state.** Delete the build directory. Incremental
  Ninja builds do not recover from a changed toolchain or a moved SDL2 prefix.
- **Where are the logs?** Next to the executable: `Data/Logs/ge_direct.log`
  (Windows), and `crash.log` if the game crashed. On macOS/Linux the same
  output goes to the terminal.

---

## CI

`.github/workflows/main.yml` builds and tests:

- macOS on `macos-15` (arm64) and `macos-15-intel`, with `GE_VIDEO` and
  `GE_PORT_TESTS`, then the sanitizer test build;
- Linux i686 with assets extracted from the cart, packaged as
  `ProjectMidas-linux-x86.tar`;
- Windows i686 cross-build, uploaded as `ProjectMidas-windows-x86`.

CI does not run the game, so a green pipeline means the code compiles and the
fixtures pass, not that gameplay matches the N64 original.
