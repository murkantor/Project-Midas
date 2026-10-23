#!/bin/sh
# Fetch the pinned SDL2 source tree used by the Android build.
#
#   sh cmake/bootstrap-sdl2.sh          # clone release-2.30.11 into third_party/SDL2
#   sh cmake/bootstrap-sdl2.sh --force  # replace an existing checkout
#
# third_party/ is git-ignored: this script is reproducible, the dependency is
# not committed. The Android shell's Gradle file adds the Java sources from
# third_party/SDL2/android-project/app/src/main/java.
set -eu

VERSION="release-2.30.11"
COMMIT="fa24d868ac2f8fd558e4e914c9863411245db8fd"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="$ROOT/third_party/SDL2"
TMP="$ROOT/third_party/.SDL2-bootstrap-$$"

force=0
if [ "${1:-}" = "--force" ]; then
    force=1
elif [ "$#" -gt 0 ]; then
    echo "usage: $0 [--force]" >&2
    exit 2
fi

if [ -f "$DEST/include/SDL_version.h" ]; then
    major=$(sed -n 's/^#define SDL_MAJOR_VERSION[[:space:]]*//p' "$DEST/include/SDL_version.h" | head -n 1)
    minor=$(sed -n 's/^#define SDL_MINOR_VERSION[[:space:]]*//p' "$DEST/include/SDL_version.h" | head -n 1)
    patch=$(sed -n 's/^#define SDL_PATCHLEVEL[[:space:]]*//p' "$DEST/include/SDL_version.h" | head -n 1)
    if [ "$major.$minor.$patch" = "2.30.11" ] && [ "$force" -eq 0 ] &&
       [ -f "$DEST/src/video/yuv2rgb/yuv_rgb.h" ] &&
       [ "$(git -C "$DEST" rev-parse HEAD 2>/dev/null || true)" = "$COMMIT" ]; then
        echo "SDL2 2.30.11 already present at $DEST"
        exit 0
    fi
fi

if [ -e "$DEST" ] && [ "$force" -eq 0 ]; then
    echo "$DEST is not a complete pinned SDL2 checkout; --force preserves it as a backup" >&2
    exit 1
fi

command -v git >/dev/null 2>&1 || { echo "git is required" >&2; exit 1; }
mkdir -p "$ROOT/third_party"

echo "Cloning SDL2 $VERSION ..."
# SDL's src/video/yuv2rgb is a submodule; without it SDL_yuv.c does not build.
git clone --depth 1 --branch "$VERSION" --recurse-submodules --shallow-submodules \
    https://github.com/libsdl-org/SDL.git "$TMP"
# Belt and braces: the tag must be the one the Android build compiles.
got=$(git -C "$TMP" describe --tags --exact-match 2>/dev/null || true)
if [ "$got" != "$VERSION" ]; then
    echo "expected tag $VERSION, got '$got'" >&2
    exit 1
fi
if [ "$(git -C "$TMP" rev-parse HEAD)" != "$COMMIT" ]; then
    echo "SDL tag does not match the pinned commit; checkout retained at $TMP" >&2
    exit 1
fi
if [ -e "$DEST" ]; then
    BACKUP="$ROOT/third_party/SDL2-backup-$$"
    mv "$DEST" "$BACKUP"
    echo "Existing SDL checkout preserved at $BACKUP"
fi
printf '%s\n' "$VERSION" > "$TMP/.midas-sdl2-version"
mv "$TMP" "$DEST"
echo "SDL2 $VERSION -> $DEST"
echo "Java sources: $DEST/android-project/app/src/main/java"
