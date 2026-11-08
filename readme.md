![Project Midas](port/include/presskit/PM%20thin%201%20-%20full.png)

# Project Midas

This is a video game about Jimothy's presumed-dead best friend Jarvis turns out to be alive, and in possession of a space laser he intends to point at a city - so Jimothy goes to the Cuban jungle and drops Jarvis off a satellite dish.

Project Midas is a work-in-progress PC port of GE, built from the N64 decompilation. It runs on Windows, Linux and macOS and reads its game data from your own copy of the US cart.

## AI Disclaimer

Claude was used in the making of this project. If you don't agree with this, please do not download either the executables or the source.

## Playing

1. Put your GE (USA) cart image in the `Data` folder next to the executable. See `Data/README.txt` for the accepted formats and other ways to point the game at it.
2. Run `ProjectMidas.exe` (Windows) or `ProjectMidas` (Linux/macOS).

Files next to the executable:

| Path | What it is |
| --- | --- |
| `midas.ini` | Settings. Written on first run; delete it to restore the defaults. See `midas_ini_help.txt`. |
| `Data/` | Your cart image goes here. |
| `Data/Saves/eeprom.bin` | Save file. |
| `Data/Profiles/` | Player profiles (`.geprofile`) and lifetime multiplayer stats (`.gestats`). |
| `Data/WeaponSets/` | Custom multiplayer weapon sets (`.gewset`). |
| `Data/MatchSettings/` | Saved multiplayer settings (`.gemp`), from Load / Save Settings. |
| `Data/Devices/` | Per-device settings (`.gedevice`). Keyboard and mouse binds are in `kbm.gedevice`. |
| `gamecontrollerdb.txt` | SDL controller mappings. |
| `gamecontrollerdefault.txt` | Built-in controller layouts. |
| `gamecontrolleruser.txt` | Your saved controller layouts. |
| `Data/Logs/ge_direct.log` | The run log on Windows (the game opens no console window). |
| `Data/Logs/crash.log` | Written if the game crashes. |
| `Mods/` | Reserved for modded carts. This build loads the stock cart only. |

An older `ge.ini` is renamed to `midas.ini` automatically on first run.

## Building

For a native macOS build, see [the macOS instructions](docs/macos.md). Building on macOS does not require a ROM or extracted assets; playing requires your own US cart image.

The Windows and Linux builds below run in Docker, which has their tools and libraries. You also need the GE (USA) cart as `baserom.u.z64` in the repository root:

```
sha1: abe01e4aeb033b6c0836819f549c791b26cfde83
```

### On Windows: use WSL

The commands below are for a Linux shell. On Windows, run them in WSL:

1. Install WSL with Ubuntu (`wsl --install` in PowerShell), and turn on WSL integration in Docker Desktop (Settings > Resources > WSL integration).
2. Open the Ubuntu terminal and clone the repository inside WSL, not under `/mnt/c` (much faster):
   ```
   cd ~
   git clone https://github.com/murkantor/Project-Midas.git
   cd Project-Midas
   ```
3. Copy your cart in, for example from your Windows Downloads folder:
   ```
   cp /mnt/c/Users/<you>/Downloads/baserom.u.z64 .
   ```

### Build steps

1. Build the image (once):
   ```
   docker build --network=host --build-arg HOST_UID=$(id -u) --build-arg HOST_GID=$(id -g) -t goldeneye .
   ```
2. Start a container in the repository:
   ```
   docker run --rm -it -v $(pwd):/home/dev/project goldeneye
   ```
3. Extract the assets (once):
   ```
   ./scripts/extract_baserom.u.sh
   make convert_props convert_chrs convert_guns combine_images
   ```
4. Build for Windows (x86):
   ```
   cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON -DSDL2_MINGW_ROOT=$HOME/sdl2-mingw/SDL2-2.30.11/i686-w64-mingw32
   cmake --build build_win -j$(nproc)
   ```
5. Build for Linux:
   ```
   cmake -B build_pc -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON -DSDL2_LINUX_ROOT=$HOME/sdl2-linux-i686
   cmake --build build_pc -j$(nproc)
   ```

| Build | Game | Next to it |
| --- | --- | --- |
| Windows | `build_win/ProjectMidas.exe` | `SDL2.dll`, the controller files, `Data/` |
| Linux | `build_pc/ProjectMidas` | the controller files, `Data/` |

To play, put a copy of `baserom.u.z64` in that build's `Data/` folder. On Windows, copy the whole `build_win` folder somewhere in Windows (from WSL: `explorer.exe build_win`) and run `ProjectMidas.exe` there. The Linux build needs 32-bit SDL2 and OpenGL on the machine that runs it. On Ubuntu or Debian:

```
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install libsdl2-2.0-0:i386 libgl1:i386
```

## Known issues

- On some Linux systems, levels draw as a few large blank triangles. Starting the game with these environment variables works around it:
  ```
  GE_FULLHEIGHT=0 GE_EDGEINSET=1 ./ProjectMidas
  ```

## Contributing

Co-op, netplay and mod loading are already in progress. Please contribute here rather than forking; see [CONTRIBUTING.md](CONTRIBUTING.md) for what a good pull request looks like, including the rules for AI-assisted changes.

## License

The Project Midas port code is MIT licensed; see [LICENSE](LICENSE). The license does not cover the decompiled game code, GE itself or its assets. Bundled third-party code keeps its own license; see `tools/THIRD_PARTY_NOTICES.txt`.

## Acknowledgements

Project Midas builds on the work of many people and projects.

### Projects and references

- [GoldenEye decompilation](https://github.com/n64decomp/007) by KholdFuzion and contributors
- [GoldenEye docs](https://github.com/kholdfuzion/goldeneye_docs) by KholdFuzion and contributors
- [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark) by Ryan Dwyer and contributors
- [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark) by fgsfds and contributors
- [Perfect Dark netplay](https://github.com/murkantor/perfect_dark_netplay) by murkantor, a fork of fgsfds' `port-net`
- [GE Plus](https://github.com/Joshua-1248/GoldenEye-007-Plus) by Joshua
- [The DOOM III Network Architecture](https://mrelusive.com/publications/papers/The-DOOM-III-Network-Architecture.pdf) by J.M.P. van Waveren (id Software, 2006)
- [ENet](https://github.com/lsalzman/enet) by Lee Salzman
- [xdelta3](https://github.com/jmacd/xdelta) by Josh MacDonald
- [qemu-irix](https://github.com/n64decomp/qemu-irix) by the n64decomp contributors
- [pj64raphnetraw](https://github.com/raphnet/pj64raphnetraw) by Raphael Assenat ([raphnet-tech](https://www.raphnet-tech.com)), the reference for the raphnet adapter protocol
- The 1964 emulator and its GE/Perfect Dark mouse injector

### Bundled third-party code

Each keeps its own license. The full license texts are in [`tools/THIRD_PARTY_NOTICES.txt`](tools/THIRD_PARTY_NOTICES.txt).

- [Fast3D renderer](https://github.com/Emill/n64-fast3d-engine) by Emill and MaikelChan (MIT), in `port/fast3d`
- [glad](https://github.com/Dav1dde/glad) by David Herberth, with the Khronos platform header by The Khronos Group (MIT-style), in `port/fast3d/glad`
- [SDL2](https://github.com/libsdl-org/SDL) by Sam Lantinga and contributors (zlib), linked at runtime
- [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) by Gabriel Jacobo and contributors (zlib), in `tools/gamecontrollerdb.txt`
- [SpectNetIDE](https://github.com/Dotneteer/spectnetide) by Istvan Novak (MIT), Z80 opcode comments in `src/game/spectrum_hw.h`
- [inflate.c](https://www.gnu.org/software/gzip/) by Mark Adler (public domain), in `src/inflate` and `src/game/zlib.c`
- [gzip 1.2.4](https://www.gnu.org/software/gzip/) by Jean-loup Gailly (GPL-2.0+), build tool in `tools/gzipsrc`
- [puff](https://github.com/madler/zlib/tree/develop/contrib/puff) by Mark Adler (zlib), build tool in `tools/extractor`
- [armips](https://github.com/Kingcom/armips) by Kingcom (MIT), build tool in `tools/armips.cpp`
- [rabbitizer](https://github.com/Decompollaborate/rabbitizer) by Decompollaborate (MIT), build tool in `tools/ido5.3_recomp/rabbitizer`
- [ido-static-recomp](https://github.com/decompals/ido-static-recomp) by Emill and decompals (no license published), build tool in `tools/ido5.3_recomp`

GE is owned by Rare, Nintendo and the Bond rights holders. This project contains no game assets.
