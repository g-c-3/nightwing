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
    REQUIRE(nightwing::support::cpu_feature_summary() != nullptr);
}

TEST_CASE("Sanity: build pipeline works", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
