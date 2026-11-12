# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.24)
project(ps4droid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PS4DROID_BUILD_CORE "Configure the shadPS4 core (core/shadPS4)" OFF)

# Android 15+ devices may use 16 KiB pages; every shared object must be aligned for it.
if (ANDROID)
    add_link_options(-Wl,-z,max-page-size=16384)
endif()

include(CTest)
add_subdirectory(platform/android)

if (PS4DROID_BUILD_CORE)
    # Options that keep desktop-only pieces out of the Android configure.
    set(ENABLE_DISCORD_RPC OFF CACHE BOOL "" FORCE)
    set(ENABLE_UPDATER OFF CACHE BOOL "" FORCE)
    # spdlog otherwise downloads fmt at configure time; use the vendored checkout.
    set(FETCHCONTENT_SOURCE_DIR_FMT "${CMAKE_CURRENT_SOURCE_DIR}/core/shadPS4/externals/fmt"
        CACHE PATH "" FORCE)
    add_subdirectory(core/shadPS4 EXCLUDE_FROM_ALL)
endif()
