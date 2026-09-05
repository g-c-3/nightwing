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

# --- Profile-Guided Optimization (PGO), ROADMAP.md Phase 8's "PGO build
#     pipeline" item ---
#
# NIGHTWING_PGO_PHASE is a two-value cache STRING, not a plain ON/OFF
# option, because PGO is inherently a two-BUILD process, not a
# single-build setting: the same source tree must be compiled and
# linked twice, with a real training workload (`./nightwing bench`, or
# a longer self-play run for broader coverage — see
# .github/workflows/ci.yml's own `pgo-build` job) run in between to
# produce the profile data the second build consumes. There is no
# single flag combination that means "just do PGO" the way there is for
# LTO above.
#
#   - "" (empty, the default): no PGO instrumentation at all — every
#     pre-existing build (every CI matrix leg, every local build)
#     continues to compile exactly as before this option existed.
#   - "generate": adds `-fprofile-generate=<dir>` to every compile AND
#     link step (needed on the link step too — this flag also pulls in
#     libgcov, standard GCC/Clang convention), producing a binary that
#     writes profile-counter (.gcda) files to that directory when run.
#   - "use": adds `-fprofile-use=<dir> -fprofile-correction
#     -Wno-missing-profile` instead, consuming the profile data written
#     by a prior "generate" build FROM THE SAME BUILD DIRECTORY (see
#     below) to guide the optimizer.
#
# `-fprofile-correction`: GCC's own flag for tolerating minor
# inconsistencies in profile counts (e.g. from an interrupted run, or
# genuinely nondeterministic code paths — Lazy SMP's own helper-thread
# scheduling, already documented elsewhere as inherently non-
# deterministic, docs/DECISIONS.md 2026-09-04 (3)) rather than treating
# any such mismatch as a hard error.
#
# `-Wno-missing-profile`: suppresses a per-function warning when a
# specific function has no profile data at all (never executed during
# the training run). Verified directly, in a standalone sandbox
# experiment before this option existed, that this project's own actual
# training workload (`bench` plus a couple of ordinary `go` searches)
# already exercises every single .cpp file in nightwing_lib, including
# book.cpp and every eval/*.cpp term file — so this flag wasn't strictly
# NEEDED for that specific workload, but is kept anyway as a defensive
# default: a different or narrower future training workload (e.g. only
# running `bench`, which never touches book.cpp at all — bench_positions.h
# searches are never book moves) could genuinely leave some functions
# uninstrumented, and warnings-as-noise there shouldn't fail a build.
#
# CRITICAL REQUIREMENT, not enforced by CMake itself: the "generate" and
# "use" builds MUST use the SAME CMAKE_BINARY_DIR (so `<dir>` above
# resolves to the same real path both times) AND must NOT rename the
# `nightwing` executable target between the two builds — verified
# directly in a sandbox experiment that GCC's default .gcda naming
# scheme ties each profile file to the specific OUTPUT BINARY NAME that
# was active during the "generate" run; a mismatched name there (even
# with identical source and an identical build directory) silently
# produces a full set of "profile count data file not found"
# per-function warnings, which -Wno-missing-profile would then mask
# rather than surface as the real configuration mistake it is. This is
# why the CI job below (.github/workflows/ci.yml's `pgo-build`) never
# reconfigures into a fresh directory or changes the executable target
# name between its two build phases.
#
# Deliberately GCC/Clang only (`if(NOT MSVC)` below) — MSVC's own PGO
# mechanism (`/LTCG:PGInstrument` + `/LTCG:PGOptimize`, a different
# workflow entirely, requiring `pgort140.dll` at profiling-run time) was
# not attempted this session, consistent with this project's own
# established pattern of not half-implementing an MSVC-specific
# mechanism without a real MSVC environment to verify it against (the
# 2026-08-13 MSVC /fsanitize=address revert, just above in this same
# file, is exactly that same lesson already learned once). Left
# explicitly open in ROADMAP.md for whoever next has MSVC access.
set(NIGHTWING_PGO_PHASE "" CACHE STRING "PGO build phase: empty (no PGO), 'generate', or 'use'. GCC/Clang only.")
set_property(CACHE NIGHTWING_PGO_PHASE PROPERTY STRINGS "" "generate" "use")

if(NIGHTWING_PGO_PHASE STREQUAL "generate" OR NIGHTWING_PGO_PHASE STREQUAL "use")
    if(MSVC)
        message(FATAL_ERROR "Nightwing: NIGHTWING_PGO_PHASE is GCC/Clang only (see this file's own comment) — not supported with MSVC.")
    endif()
    set(NIGHTWING_PGO_DATA_DIR "${CMAKE_BINARY_DIR}/pgo-data")
    if(NIGHTWING_PGO_PHASE STREQUAL "generate")
        set(NIGHTWING_PGO_FLAGS "-fprofile-generate=${NIGHTWING_PGO_DATA_DIR}")
    else()
        set(NIGHTWING_PGO_FLAGS "-fprofile-use=${NIGHTWING_PGO_DATA_DIR} -fprofile-correction -Wno-missing-profile")
    endif()
    # Applied globally (every target, not just nightwing_lib): main.cpp
    # itself is a real translation unit too, and the link step for every
    # executable needs the same flag as the compile step (this file's
    # own comment above on why -fprofile-generate is needed at link time
    # as well).
    string(APPEND CMAKE_CXX_FLAGS " ${NIGHTWING_PGO_FLAGS}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " ${NIGHTWING_PGO_FLAGS}")
endif()

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
else()
    # CMake's own MSVC Debug default is "/MDd /Zi /Ob0 /Od /RTC1" --
    # reproduced here identically MINUS /RTC1 (runtime stack-frame and
    # uninitialized-variable checks). /RTC1 is hard-incompatible (MSVC
    # error D8016, confirmed against Microsoft's own documentation: an
    # absolute "cannot be specified together" check, not resolvable by
    # flag ordering) with any per-file optimization override above /Od,
    # such as the one applied to board/attacks.cpp below, for reasons
    # covered in that file's own comment. Rather than keep trying to
    # override/cancel /RTC1 per-file after two failed attempts (see
    # docs/DECISIONS.md's 2026-08-17/2026-08-18 entries for that full
    # history), it's removed from the Debug configuration's baseline here
    # so there's nothing to conflict with in the first place, regardless
    # of build-file generator or flag-ordering semantics.
    #
    # This is a project-wide tradeoff for MSVC Debug specifically, not a
    # per-file one: every MSVC Debug build loses /RTC1's checks, not just
    # board/attacks.cpp's compilation. Windows Debug already has zero
    # ASan/UBSan coverage as of the 2026-08-13 MSVC /fsanitize=address
    # revert above -- /RTC1 was its only remaining sanitizer-adjacent
    # check, so this narrows Windows-specific coverage further. Linux and
    # macOS Debug builds are completely unaffected and keep full ASan/UBSan
    # coverage exactly as before.
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od")
endif()

add_subdirectory(src)

if(NIGHTWING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
