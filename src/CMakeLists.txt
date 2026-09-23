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
    eval/endgame.cpp
    eval/king_pawn_endgame.cpp
    eval/rook_endgame.cpp
    eval/minor_piece_endgame.cpp
    eval/fortress.cpp
    eval/basic_mates.cpp
    eval/eval_cache.cpp
    eval/incremental.cpp
    eval/eval.cpp
    search/ordering.cpp
    search/quiescence.cpp
    search/search.cpp
    search/see.cpp
    search/skill.cpp
    search/tt.cpp
    book/book.cpp
    uci/uci.cpp
    tuner/selfplay.cpp
    tuner/tune.cpp
    tuner/match.cpp
    tuner/sprt.cpp
)

target_include_directories(nightwing_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nightwing_lib PUBLIC nightwing_warnings)

# Lazy SMP (ROADMAP.md Phase 7) uses std::thread/std::mutex directly in
# src/search/search.cpp and src/search/tt.cpp -- on platforms where the
# threading support library isn't linked in implicitly (notably glibc
# Linux, where -pthread is required rather than optional), std::thread's
# constructor throws/aborts at runtime without this. PUBLIC so every
# executable target below (nightwing, nightwing_bench, etc.), which all
# link nightwing_lib, gets it transitively.
find_package(Threads REQUIRED)
target_link_libraries(nightwing_lib PUBLIC Threads::Threads)
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
    # NOTE: this block deliberately does NOT add -mbmi2/-mpopcnt here
    # (ROADMAP.md Priority Fixes, 2026-09-22, finding 3 — fixed Session
    # 126; see docs/DECISIONS.md, this dated entry, for the full account).
    # Applying either flag to the WHOLE nightwing_lib target, as this
    # block used to, does two separately dangerous things at once: (1) it
    # lets the compiler auto-generate BMI2/POPCNT hardware instructions
    # ANYWHERE in the library's ~40 .cpp files, including the ordinary,
    # supposedly-portable magic-bitboard fallback path and every plain
    # std::popcount() call throughout eval/search (bitboard.h's own doc
    # comment on popcount() promises a portable software fallback on CPUs
    # without hardware POPCNT — a promise that -mpopcnt applied library-
    # wide silently broke, since std::popcount() then always lowers to
    # the hardware instruction with no runtime check anywhere); (2) it
    # made board/attacks.cpp's OWN, deliberately unconditional
    # build_pext_table() call the exact bug finding 3 describes: an
    # illegal instruction on any pre-Haswell x86 CPU regardless of the
    # runtime detection machinery that already existed alongside it.
    # Neither flag is needed here anymore: board/attacks.cpp isolates the
    # actual PEXT instruction to a single function (pext_u64(), marked
    # with a GCC/Clang __attribute__((target("bmi2"))) — see that file's
    # own "BMI2 CODE ISOLATION" header comment), called only after
    # support::cpu_has_bmi2() has confirmed the running CPU supports it;
    # every other function in the library, popcount() included, now
    # compiles under this target's own ordinary flags and gets whatever
    # portable (or, on MSVC, always-available-regardless-of-target-flags
    # per this block's own next comment) codegen the compiler judges
    # correct for a CPU that hasn't been runtime-confirmed as anything
    # more capable than baseline x86_64.
    # MSVC needs no equivalent flag here at all: its <immintrin.h>
    # intrinsics are available unconditionally on x86_64 regardless of
    # /arch: flags, gated by the same runtime check before use rather
    # than by any compiler target setting — see board/attacks.cpp's own
    # NIGHTWING_BMI2_TARGET macro, which expands to nothing at all on
    # MSVC for exactly this reason.
endif()

add_executable(nightwing main.cpp)
target_link_libraries(nightwing PRIVATE nightwing_lib)

add_executable(nightwing_bench bench.cpp)
target_link_libraries(nightwing_bench PRIVATE nightwing_lib)

add_executable(nightwing_selfplay tuner/selfplay_main.cpp)
target_link_libraries(nightwing_selfplay PRIVATE nightwing_lib)

add_executable(nightwing_tune tuner/tune_main.cpp)
target_link_libraries(nightwing_tune PRIVATE nightwing_lib)

add_executable(nightwing_match tuner/match_main.cpp)
target_link_libraries(nightwing_match PRIVATE nightwing_lib)

add_executable(nightwing_sprt tuner/sprt_main.cpp)
target_link_libraries(nightwing_sprt PRIVATE nightwing_lib)
