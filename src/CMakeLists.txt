# nightwing_lib holds everything except main() — linked into both the
# engine executable and the test binary, so tests can exercise engine
# internals directly.
add_library(nightwing_lib STATIC
    support/cpu_features.cpp
    board/bitboard.cpp
    board/attacks.cpp
)

target_include_directories(nightwing_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nightwing_lib PUBLIC nightwing_warnings)
target_compile_features(nightwing_lib PUBLIC cxx_std_20)

if(NIGHTWING_ENABLE_BMI2)
    target_compile_definitions(nightwing_lib PUBLIC NIGHTWING_ENABLE_BMI2=1)
    # -mbmi2/-mpopcnt are x86 ISA extensions — meaningless (and rejected by
    # the compiler) on non-x86 targets such as Apple Silicon (arm64) macOS
    # runners. Only apply them when actually building for x86/x86_64.
    if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86|x86)$")
        # Configure with -DNIGHTWING_ENABLE_BMI2=OFF for a portable build
        # that never emits BMI2/POPCNT instructions (e.g. older hardware).
        # This is compile-time gating; support/cpu_features.h additionally
        # provides *runtime* detection so a BMI2-enabled binary can still
        # check what the actual host CPU supports before using the fast path
        # (magic-bitboard fallback path lands in Phase 1).
        target_compile_options(nightwing_lib PUBLIC -mbmi2 -mpopcnt)
    endif()
endif()

add_executable(nightwing main.cpp)
target_link_libraries(nightwing PRIVATE nightwing_lib)
