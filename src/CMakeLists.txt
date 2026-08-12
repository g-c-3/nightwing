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
)

target_include_directories(nightwing_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nightwing_lib PUBLIC nightwing_warnings)
target_compile_features(nightwing_lib PUBLIC cxx_std_20)

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
