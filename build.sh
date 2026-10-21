#!/bin/sh
# Builds the Linux PC port in Docker (readme steps 1-5), skipping steps that
# are already done: the image, asset extraction, the cmake configure.
#   ./build.sh          incremental build of build_pc
#   ./build.sh clean    reconfigure build_pc from scratch first
set -e
cd "$(dirname "$0")"
# a DOCKER_HOST pointing at a dead socket (an unstarted rootless daemon)
# falls back to the system daemon
case "$DOCKER_HOST" in
    unix://*) [ -S "${DOCKER_HOST#unix://}" ] || export DOCKER_HOST=unix:///var/run/docker.sock ;;
esac

if ! docker image inspect goldeneye >/dev/null 2>&1; then
    docker build --network=host --build-arg HOST_UID="$(id -u)" --build-arg HOST_GID="$(id -g)" -t goldeneye .
fi

[ "$1" = clean ] && rm -rf build_pc/CMakeCache.txt build_pc/CMakeFiles

docker run --rm -v "$(pwd)":/home/dev/project goldeneye bash -lc '
    set -e
    cd /home/dev/project
    if [ ! -d assets/obseg ]; then
        ./scripts/extract_baserom.u.sh
        make convert_props convert_chrs convert_guns combine_images
    fi
    if [ ! -f build_pc/CMakeCache.txt ]; then
        cmake -B build_pc -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON -DSDL2_LINUX_ROOT=$HOME/sdl2-linux-i686
    fi
    cmake --build build_pc -j$(nproc)
    # run.sh loads SDL2 from build_pc, so the host needs no 32-bit SDL2
    [ -f build_pc/libSDL2-2.0.so.0 ] || cp -L $HOME/sdl2-linux-i686/lib/i386-linux-gnu/libSDL2-2.0.so.0 build_pc/ 2>/dev/null \
        || cp -L $(find $HOME/sdl2-linux-i686 -name "libSDL2-2.0.so.0" | head -1) build_pc/
'
echo "built: build_pc/ProjectMidas  (run with ./run.sh)"
