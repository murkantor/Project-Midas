#!/bin/sh
# package_macos.sh - assemble the macOS release zip under ./releases
#
# Usage (from the repo root):  scripts/port/package_macos.sh <build dir> <name>
#   e.g. scripts/port/package_macos.sh build_release v1.0.3
#   gives releases/v1.0.3/ProjectMidas-v1.0.3-macos.zip
#
# Same layout as the linux zip from package_release.ps1, with SDL2 bundled
# beside the exe. RUNTIME payload only: never a ROM, midas.ini, eeprom.bin,
# gamecontrolleruser.txt or logs. Build first - this does not compile. The
# build must link a self-built SDL2 (see the package-macos CI job): Homebrew's
# sdl2 is sdl2-compat, which loads SDL3 at runtime and cannot travel alone.
set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <build dir> <name>" >&2
    exit 1
fi
root=$(cd "$(dirname "$0")/../.." && pwd)
build=$(cd "$1" && pwd)
name=$2
exe=$build/ProjectMidas
[ -f "$exe" ] || { echo "missing $exe - build first" >&2; exit 1; }

# the SDL2 the exe links against, resolving @rpath through the exe's rpaths
sdlref=$(otool -L "$exe" | awk '/libSDL2/ { print $1; exit }')
sdl=$sdlref
case $sdlref in
@rpath/*)
    sdl=
    for rp in $(otool -l "$exe" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }'); do
        if [ -f "$rp/${sdlref#@rpath/}" ]; then sdl=$rp/${sdlref#@rpath/}; break; fi
    done
    ;;
esac
[ -n "$sdl" ] && [ -f "$sdl" ] || { echo "cannot find $sdlref for $exe" >&2; exit 1; }
case $sdl in
/opt/homebrew/* | /usr/local/*)
    echo "$exe links Homebrew's SDL2 ($sdl), which needs SDL3 beside it - rebuild against a self-built SDL2" >&2
    exit 1
    ;;
esac

# stamp from the commit the binary was built at, not HEAD
commit=$(sed -n 's/#define PORT_BUILD_COMMIT "\([0-9a-f]*\)"/\1/p' "$build/generated/port_buildinfo.h")
stamp="built from ${commit:-unknown} on $(date '+%Y-%m-%d %H:%M')"

base=ProjectMidas-$name-macos
out=$root/releases/$name
dir=$out/$base
rm -rf "$dir" "$out/$base.zip"
mkdir -p "$dir/Data/Textures/Base" "$dir/Data/Textures/Options" "$dir/Mods"

cp "$exe" "$dir/ProjectMidas"
cp "$sdl" "$dir/libSDL2-2.0.0.dylib"
chmod u+w "$dir/libSDL2-2.0.0.dylib"
# point the exe at the bundled SDL2, then re-sign: install_name_tool voids the signature
install_name_tool -id @executable_path/libSDL2-2.0.0.dylib "$dir/libSDL2-2.0.0.dylib"
install_name_tool -change "$sdlref" @executable_path/libSDL2-2.0.0.dylib "$dir/ProjectMidas"
# sort -u: a universal binary lists each rpath once per architecture
for rp in $(otool -l "$dir/ProjectMidas" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' | sort -u); do
    install_name_tool -delete_rpath "$rp" "$dir/ProjectMidas"
done
codesign --force --sign - "$dir/libSDL2-2.0.0.dylib"
codesign --force --sign - "$dir/ProjectMidas"

cp "$root/tools/gamecontrollerdb.txt" "$root/tools/gamecontrollerdefault.txt" "$root/tools/midas_ini_help.txt" \
   "$root/tools/THIRD_PARTY_NOTICES.txt" "$dir/"
cp "$root/tools/Data_README.txt" "$dir/Data/README.txt"
cp "$root/tools/shaders_base.txt" "$dir/Data/shaders_base.txt"
cp "$root/tools/Mods_README.txt" "$dir/Mods/README.txt"
cp "$root/LICENSE" "$dir/LICENSE.txt"
cat > "$dir/README.txt" <<EOF
Project Midas - $name
$stamp

Project Midas is a PC port of GoldenEye 007. It needs your own GoldenEye 007 (USA) cart image.

1. Put the cart image in the Data folder (see Data/README.txt).
2. Run the game.

Settings are in midas.ini, written on first run. Delete it to restore the defaults. See midas_ini_help.txt for every setting.

LICENSE.txt covers the Project Midas code. Third-party code keeps its own license; see THIRD_PARTY_NOTICES.txt.
Needs macOS 11 or newer, on Apple Silicon or Intel. SDL2 is bundled.
This build is not notarized. If macOS says it cannot be opened, run this once in Terminal from this folder:
    xattr -dr com.apple.quarantine .
EOF

# ditto keeps the exec bit, which the other platforms' zips lose; no xattrs,
# or every file gets a ._ AppleDouble twin
(cd "$out" && ditto -c -k --norsrc --noextattr --keepParent "$base" "$base.zip")
ls -l "$out/$base.zip"
