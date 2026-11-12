# Stand-in toolchain used only where the Android NDK is unavailable.
# It targets generic AArch64 Linux (glibc), so it finds CPU-architecture
# breakages but says nothing about bionic.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   "${CMAKE_CURRENT_LIST_DIR}/zig-cc")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-c++")
set(CMAKE_ASM_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cc")
set(CMAKE_AR     "${CMAKE_CURRENT_LIST_DIR}/zig-ar"     CACHE FILEPATH "")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-ranlib" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
