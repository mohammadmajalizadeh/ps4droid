# SPDX-License-Identifier: GPL-2.0-or-later

add_library(ps4droid_platform STATIC
    probe/va_probe.cpp
    probe/va_probe.h
)
target_include_directories(ps4droid_platform PUBLIC probe)
target_compile_options(ps4droid_platform PRIVATE -Wall -Wextra)

# Standalone probe: push to /data/local/tmp and run with adb shell.
add_executable(va_probe probe/va_probe.cpp)
target_compile_definitions(va_probe PRIVATE VA_PROBE_STANDALONE)
target_compile_options(va_probe PRIVATE -Wall -Wextra)

if (BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(va_probe_test tests/va_probe_test.cpp)
    target_link_libraries(va_probe_test PRIVATE ps4droid_platform)
    add_test(NAME va_probe_test COMMAND va_probe_test)
endif()
