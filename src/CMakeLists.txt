# nightwing_lib holds everything except main() — linked into both the
# engine executable and the test binary, so tests can exercise engine
# internals directly.
add_library(nightwing_lib STATIC
    support/cpu_features.cpp
    board/bitboard.cpp
    board/attacks.cpp
    board/board.cpp
    board/zobrist.cpp
    board/masks.cpp
    board/move.cpp
    board/movegen.cpp
    board/fen.cpp
    board/perft.cpp
    eval/psqt.cpp
    eval/pawns.cpp
    eval/pawn_tt.cpp
    eval/mobility.cpp
    eval/king_safety.cpp
    eval/piece_bonuses.cpp
    eval/knight_outposts.cpp
    eval/space.cpp
    eval/threats.cpp
    eval/king_tropism.cpp
    eval/trapped_pieces.cpp
    eval/tempo.cpp
    eval/material_imbalance.cpp
    eval/eval.cpp
    search/ordering.cpp
    search/quiescence.cpp
    search/search.cpp
    search/see.cpp
    search/tt.cpp
    uci/uci.cpp
)

target_include_directories(nightwing_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nightwing_lib PUBLIC nightwing_warnings)
target_compile_features(nightwing_lib PUBLIC cxx_std_20)

# board/attacks.cpp's magic bitboard table generation (init_magic_bitboards())
# is expensive bit-twiddling setup code that becomes pathologically slow when
# fully unoptimized -- measured (docs/DECISIONS.md, CI test-time investigation)
# at ~33.6s under the Debug build's -O0 + ASan/UBSan combination on this
# codebase's actual init routine. It's called once per test PROCESS by every
# test that touches board/movegen functionality (this codebase deliberately
# runs each Catch2 TEST_CASE as its own process for isolation -- see
# tests/*.cpp's own comments), which is what made CI's Debug jobs take 1+
# hour despite the actual test logic in each case being fast. Compiling just
# this one file at a higher optimization level, even inside an otherwise -O0
# Debug build, brings the same call down to ~4.0s with ASan/UBSan still fully
# active on it (measured) -- full sanitizer coverage retained on this file,
# only the optimization level changes. The $<CONFIG:Debug> guard keeps this a
# no-op in Release builds, which are already -O3/-O2 and would otherwise have
# this quietly downgrade them for this one file (compilers take the last -O
# flag specified on the command line as authoritative, and per-source
# COMPILE_OPTIONS are appended after the build type's global flags).
#
# Applies to both GCC/Clang and MSVC. MSVC needed two earlier, more involved
# attempts here (docs/DECISIONS.md's 2026-08-17 and 2026-08-18 entries) that
# both failed with MSVC error D8016, "'/O2' and '/RTC1' command-line options
# are incompatible" -- a hard, order-independent conflict, confirmed against
# Microsoft's own documentation, not resolvable by appending a counter-flag
# here. The actual fix lives in the top-level CMakeLists.txt: /RTC1 is no
# longer part of MSVC's Debug baseline at all (see its own comment there for
# the coverage tradeoff that involves), and the CI workflow now builds
# Windows with the Ninja generator instead of Visual Studio's, so this
# override reaches the compiler as a plain, literal trailing flag the same
# way it already does for GCC/Clang -- rather than as a structured per-file
# MSBuild XML property, which is what made the two earlier attempts
# unreliable to reason about.
if(MSVC)
    set(NIGHTWING_INIT_OPT_FLAG "/O2")
else()
    set(NIGHTWING_INIT_OPT_FLAG "-O2")
endif()
set_source_files_properties(board/attacks.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Debug>:${NIGHTWING_INIT_OPT_FLAG}>"
)

if(NIGHTWING_ENABLE_BMI2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86|x86|X86)$")
    # NIGHTWING_ENABLE_BMI2 must only ever be defined on x86/x86_64 — code
    # gated behind it uses <immintrin.h> PEXT intrinsics, which don't exist
    # on other architectures (e.g. Apple Silicon macOS runners). This is
    # compile-time gating only; support/cpu_features.h additionally
    # provides *runtime* detection so a binary built with this ON still
    # checks the actual host CPU before ever executing a PEXT instruction
    # (see board/attacks.cpp for the runtime dispatch).
    target_compile_definitions(nightwing_lib PUBLIC NIGHTWING_ENABLE_BMI2=1)
    if(NOT MSVC)
        # MSVC doesn't need an equivalent flag: its <immintrin.h> intrinsics
        # are available unconditionally on x86_64, gated by our own runtime
        # check before use rather than by a compiler target flag.
        target_compile_options(nightwing_lib PUBLIC -mbmi2 -mpopcnt)
    endif()
endif()

add_executable(nightwing main.cpp)
target_link_libraries(nightwing PRIVATE nightwing_lib)

add_executable(nightwing_bench bench.cpp)
target_link_libraries(nightwing_bench PRIVATE nightwing_lib)
