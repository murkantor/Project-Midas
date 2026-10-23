# Android SDL2 (pinned 2.30.11) as a subproject of the root CMake build.
#
# This file is included BEFORE the root target's compile options/definitions,
# so SDL2 is not built with GE's decompiled-code flags (-ffreestanding,
# VERSION_US, ...). third_party/SDL2 is not committed; run
# cmake/bootstrap-sdl2.sh to materialise it.
#
# The Android shell's Gradle build must upload BOTH shared libraries this
# build produces: libmain.so (the game) and libSDL2.so (SDLActivity loads
# "SDL2" before "main" in its default getLibraries()).

if(NOT ANDROID)
    return()
endif()

set(GE_SDL2_ROOT "${CMAKE_SOURCE_DIR}/third_party/SDL2"
    CACHE PATH "Pinned SDL2 2.30.11 source checkout (Android)")

if(NOT EXISTS "${GE_SDL2_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "Android build needs SDL2 2.30.11 at ${GE_SDL2_ROOT}.\n"
        "Run: sh cmake/bootstrap-sdl2.sh\n"
        "(third_party/ is git-ignored; never commit the dependency.)")
endif()

file(STRINGS "${GE_SDL2_ROOT}/include/SDL_version.h" _sdl_ver_lines
     REGEX "#define SDL_(MAJOR_VERSION|MINOR_VERSION|PATCHLEVEL)[ \t]")
string(REGEX MATCH "SDL_MAJOR_VERSION[ \t]+([0-9]+)" _m "${_sdl_ver_lines}")
set(_sdl_major "${CMAKE_MATCH_1}")
string(REGEX MATCH "SDL_MINOR_VERSION[ \t]+([0-9]+)" _m "${_sdl_ver_lines}")
set(_sdl_minor "${CMAKE_MATCH_1}")
string(REGEX MATCH "SDL_PATCHLEVEL[ \t]+([0-9]+)" _m "${_sdl_ver_lines}")
set(_sdl_patch "${CMAKE_MATCH_1}")
if(NOT _sdl_major STREQUAL "2" OR NOT _sdl_minor STREQUAL "30" OR NOT _sdl_patch STREQUAL "11")
    message(FATAL_ERROR
        "Android build pins SDL2 2.30.11, found ${_sdl_major}.${_sdl_minor}.${_sdl_patch} "
        "at ${GE_SDL2_ROOT}. Re-run cmake/bootstrap-sdl2.sh.")
endif()

# libSDL2.so + libmain.so, matching SDL's own android-project layout. The
# static library would have to be folded into libmain.so, but SDLActivity still
# System.loadLibrary("SDL2") first, so the shared build is the honest one.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
set(SDL_OPENGLES ON CACHE BOOL "" FORCE)

# SDL's Android section adds ${ANDROID_NDK}/sources/android/cpufeatures; the
# NDK toolchain sets CMAKE_ANDROID_NDK but not ANDROID_NDK, so provide it.
set(ANDROID_NDK "${CMAKE_ANDROID_NDK}")

add_subdirectory("${GE_SDL2_ROOT}" "${CMAKE_BINARY_DIR}/third_party/SDL2" EXCLUDE_FROM_ALL)

set(GE_SDL2_JAVA_DIR "${GE_SDL2_ROOT}/android-project/app/src/main/java"
    CACHE PATH "SDL Java sources the Android shell must add to its source set")
message(STATUS "Android SDL2: ${GE_SDL2_ROOT} (2.30.11), Java sources ${GE_SDL2_JAVA_DIR}")

# The game target links SDL2::SDL2; make sure its headers are visible even if
# the imported target's usage requirements change between SDL releases.
set(GE_SDL2_INCLUDE_DIR "${GE_SDL2_ROOT}/include" CACHE PATH "")
