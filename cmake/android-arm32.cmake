# EXPERIMENTAL Android armeabi-v7a toolchain (32-bit bring-up).
# The primary target stays arm64-v8a (cmake/android-arm64.cmake). This exists
# to exercise real gameplay paths on 32-bit-capable devices while the arm64
# guest-pointer audit is in progress; it is not a release ABI.
#
#   cmake -B build_android32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/android-arm32.cmake \
#         -DGE_VIDEO=ON -DGE_ANDROID_ARM32_RELAXED_TYPES=ON
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 26)
set(CMAKE_ANDROID_ARCH_ABI armeabi-v7a)
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
