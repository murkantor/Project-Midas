# Standalone Android arm64-v8a toolchain for testing the native build without
# Gradle. Gradle/AGP passes its own NDK toolchain, so this file is optional.
#
#   cmake -B build_android -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/android-arm64.cmake \
#         -DGE_VIDEO=ON
#
# ANDROID_NDK_HOME (or ANDROID_NDK_ROOT) selects the NDK; the API level is the
# minimum the port targets (26 = Android 8.0, required by SDL2 2.30).
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 26)
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_STL_TYPE c++_shared)
set(_ge_ndk_env "$ENV{ANDROID_NDK_HOME}")
if(NOT _ge_ndk_env)
    set(_ge_ndk_env "$ENV{ANDROID_NDK_ROOT}")
endif()
if(NOT _ge_ndk_env)
    message(FATAL_ERROR "Set ANDROID_NDK_HOME to an Android NDK (r26+).")
endif()
file(TO_CMAKE_PATH "${_ge_ndk_env}" CMAKE_ANDROID_NDK)
unset(_ge_ndk_env)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
