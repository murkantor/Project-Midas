# Native macOS build

Install Xcode Command Line Tools and the Homebrew build dependencies:

```sh
xcode-select --install
brew install cmake ninja sdl2
```

From the repository root:

```sh
cmake -S . -B build_macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGE_VIDEO=ON -DGE_PORT_TESTS=ON
cmake --build build_macos --parallel
ctest --test-dir build_macos --output-on-failure
./build_macos/ProjectMidas --rom-file "/path/to/GoldenEye 007 (USA).z64"
```

Use an SDL2 installation built for the same architecture as the compiler. CMake defaults to the host architecture; Apple Silicon runs arm64 directly. Intel macOS uses the same native 64-bit code path. SDL opens a Cocoa window with an OpenGL core context and supplies audio and input.

The native build reads assets from the ROM at runtime. It does not need extraction scripts, a 32-bit toolchain, Docker, Rosetta, or a low-address memory reservation. `GE_NATIVE_64` defaults to `ON` on macOS; compiled asset fallback is unavailable in this mode. The existing Windows/Linux 32-bit build remains the default on those platforms.

Use the US ROM with SHA-1 `abe01e4aeb033b6c0836819f549c791b26cfde83`. Alternatively, put the image in `build_macos/Data/` following [the Data folder instructions](../tools/Data_README.txt). Settings, saves and support files are stored beside the executable, independently of the shell's working directory. Keep that directory writable and retain the support files copied there by CMake.

## Implementation

Serialized ROM offsets and display-list words remain 32-bit inputs. Model nodes, textures, setup records, room/portal tables, animation headers and audio banks are converted to native layouts at their load boundaries. Host addresses use pointer types or `intptr_t`/`uintptr_t`; vertex records retain their serialized 16-byte layout. Native model allocations are released when the stage pool resets.

Native consumers must use named fields and element-based indexing after conversion. Animation identity checks use `portAnimationFromOffset()` so they compare the same native headers stored by the model system. Keep literal byte offsets and 32-bit segment tags inside serialized-data readers; they do not describe widened runtime records.

The bank, model and prop converters include code adapted from [MGB64](https://github.com/akratch/mgb64/tree/0d1d40b4cde18302ec9789853a50f5545e679062), with attribution in [THIRD_PARTY_NOTICES.txt](../tools/THIRD_PARTY_NOTICES.txt). No ROM data is included in these converters or the tests.

## Validation

`GE_PORT_TESTS=ON` enables synthetic fixtures for native model links and display lists, all vertices of star-shaped muzzle flashes, inline texture byte order, audio banks including shared raw PCM samples, allocator operations across the 4 GiB boundary, background bullet hits through openings and solid triangles, ladder/crouch tile flags, shoulder/back aiming limits for firing poses, walk/run/sprint selection, animation-specific travel distances and hit-reaction lengths, character/door collision flags, cuff model selection, multiplayer animation identity, water display-list updates, and patrol initialization. They also run without a ROM in the macOS CI jobs for Apple Silicon and Intel. To run the fixtures with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build_native_tests -G Ninja \
  -DGE_NATIVE_64=ON -DGE_PORT_TESTS=ON -DGE_SANITIZE=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_native_tests --target native_layout_test native_collision_test native_aim_test native_stan_test native_movement_test native_prop_collision_test native_player_test
ctest --test-dir build_native_tests --output-on-failure
```

In-game testing is still necessary. Check startup logos, menu text, the Dam opening, movement and collision, enemy fire, impact-puff colors, music and weapon sounds, the pause watch, and mission exit/reload. Test additional levels and multiplayer before claiming full game compatibility; the fixtures cannot establish visual or gameplay parity.
