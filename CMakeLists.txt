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

# --- Debug: sanitizers. ASan+UBSan on non-MSVC; MSVC gets ASan only via
#     its own native /fsanitize=address (MSVC has no UBSan equivalent).
#     MSVC's default Debug flags include /RTC1 (runtime checks), which is
#     documented as incompatible with /fsanitize=address, so -- like the
#     non-MSVC branch below -- this is a full override, not an append:
#     /RTC1 is deliberately dropped rather than left alongside the
#     sanitizer. Incremental linking is also documented as incompatible,
#     so it's explicitly disabled on the linker flags. See
#     docs/DECISIONS.md for the coverage tradeoffs this leaves. ---
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od /fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG /INCREMENTAL:NO")
endif()

add_subdirectory(src)

if(NIGHTWING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
