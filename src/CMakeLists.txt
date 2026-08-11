# nightwing_lib holds everything except main() — linked into both the
# engine executable and the test binary, so tests can exercise engine
# internals directly.
add_library(nightwing_lib STATIC
    support/cpu_features.cpp
)

target_include_directories(nightwing_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nightwing_lib PUBLIC nightwing_warnings)
target_compile_features(nightwing_lib PUBLIC cxx_std_20)

if(NIGHTWING_ENABLE_BMI2)
    target_compile_definitions(nightwing_lib PUBLIC NIGHTWING_ENABLE_BMI2=1)
    if(NOT MSVC)
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
