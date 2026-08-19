// tests/test_smoke.cpp
//
// Phase 0 smoke test: confirms Catch2 is wired up and nightwing_lib links
// correctly. Replaced/expanded by real perft/search/eval suites in later
// phases (see docs/ARCHITECTURE.md Testing Policy) — this file's only job
// is to catch a broken build/test pipeline.

#include <catch2/catch_test_macros.hpp>

#include "support/cpu_features.h"

TEST_CASE("CPU feature detection runs without crashing", "[support]") {
    nightwing::support::detect_cpu_features();
    // CI runners vary in what they support, so we don't assert specific
    // flags here — just that detection and querying are safe to call.
    // WARN() (not INFO()) so this prints even though the test passes --
    // ctest normally only shows output for failing tests. Temporary,
    // added to investigate why board/attacks.cpp's optimization override
    // (src/CMakeLists.txt) isn't speeding up Windows Debug's test time
    // nearly as much as it does on Linux/macOS, despite being confirmed
    // (via a build.ninja inspection) to actually reach the compiler
    // there -- see docs/DECISIONS.md's 2026-08-19 entries. Remove once
    // that's resolved.
    WARN("Detected CPU features: " << nightwing::support::cpu_feature_summary());
    REQUIRE(nightwing::support::cpu_feature_summary() != nullptr);
}

TEST_CASE("Sanity: build pipeline works", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
