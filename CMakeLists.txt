cmake_minimum_required(VERSION 3.20)
project(nightwing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if no build type specified (single-config generators only).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

option(NIGHTWING_ENABLE_LTO "Enable link-time optimization in Release builds" ON)
option(NIGHTWING_ENABLE_BMI2 "Compile with BMI2/POPCNT instructions enabled (fast path). Disable for a portable fallback build." ON)
option(NIGHTWING_BUILD_TESTS "Build the Catch2 test suite" ON)

# --- Compiler warnings, applied to all first-party targets ---
add_library(nightwing_warnings INTERFACE)
if(MSVC)
    target_compile_options(nightwing_warnings INTERFACE /W4)
else()
    target_compile_options(nightwing_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# --- Release: -O3 + LTO ---
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
endif()

if(NIGHTWING_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NIGHTWING_IPO_SUPPORTED OUTPUT NIGHTWING_IPO_ERROR)
    if(NIGHTWING_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "Nightwing: LTO not supported by this toolchain (${NIGHTWING_IPO_ERROR}), skipping.")
    endif()
endif()

# --- Debug: sanitizers (ASan/UBSan). MSVC uses a different sanitizer model
#     and different flag syntax, so it is skipped here rather than faked.
#     (MSVC's native /fsanitize=address was tried on 2026-08-13 and reverted
#     the same day -- it hung the smoke test to its 25-minute CTest timeout
#     on windows-latest CI, apparently interacting badly with cpu_features.cpp's
#     CPUID intrinsics or the runner's virtualization layer. See
#     docs/DECISIONS.md for the observed failure and why this wasn't pursued
#     further from a sandbox with no MSVC toolchain to debug it live.) ---
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")
endif()

add_subdirectory(src)

if(NIGHTWING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
