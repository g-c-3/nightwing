// tests/iteration_control_tests.cpp
//
// Unit tests for src/search/iteration_control.h (Session 224): the pure rule
// deciding whether iterative deepening starts another depth under a
// soft/hard time budget.

#include <catch2/catch_test_macros.hpp>

#include "search/iteration_control.h"

using nightwing::search::should_start_next_iteration;

TEST_CASE("iteration_control: no soft budget always continues", "[search][time_management]") {
    // Fixed-depth, movetime and infinite callers pass soft = 0.
    REQUIRE(should_start_next_iteration(0, 0, 0, 0, false));
    REQUIRE(should_start_next_iteration(1000000, 500000, 0, 100, true));
    REQUIRE(should_start_next_iteration(5000, 2000, -1, 1000, false));
}

TEST_CASE("iteration_control: a stable best move stops at 3/5 of the soft budget",
          "[search][time_management]") {
    // soft 1000, hard 3000: 3/5 of soft is 600 ms.
    REQUIRE(should_start_next_iteration(599, 50, 1000, 3000, true));
    REQUIRE_FALSE(should_start_next_iteration(600, 50, 1000, 3000, true));
    REQUIRE_FALSE(should_start_next_iteration(900, 50, 1000, 3000, true));
}

TEST_CASE("iteration_control: an unstable best move continues until the whole soft budget",
          "[search][time_management]") {
    REQUIRE(should_start_next_iteration(600, 50, 1000, 3000, false));
    REQUIRE(should_start_next_iteration(999, 50, 1000, 3000, false));
    REQUIRE_FALSE(should_start_next_iteration(1000, 50, 1000, 3000, false));
}

TEST_CASE("iteration_control: unstable gets strictly more time than stable",
          "[search][time_management]") {
    for (int elapsed = 0; elapsed <= 1200; elapsed += 10) {
        if (should_start_next_iteration(elapsed, 0, 1000, 3000, true)) {
            REQUIRE(should_start_next_iteration(elapsed, 0, 1000, 3000, false));
        }
    }
}

TEST_CASE("iteration_control: an iteration predicted to pass the hard cap is not started",
          "[search][time_management]") {
    // elapsed 400 + 3 * 400 = 1600 > hard 1000, though still under soft.
    REQUIRE_FALSE(should_start_next_iteration(400, 400, 5000, 1000, false));
    // Exactly at the cap (400 + 3 * 200 = 1000) is allowed; one more ms is not.
    REQUIRE(should_start_next_iteration(400, 200, 5000, 1000, false));
    REQUIRE_FALSE(should_start_next_iteration(401, 200, 5000, 1000, false));
}

TEST_CASE("iteration_control: hard <= 0 disables only the prediction, not the soft stop",
          "[search][time_management]") {
    REQUIRE(should_start_next_iteration(400, 100000, 1000, 0, false));
    REQUIRE_FALSE(should_start_next_iteration(1000, 0, 1000, 0, false));
}

TEST_CASE("iteration_control: tiny budgets and large values do not overflow",
          "[search][time_management]") {
    REQUIRE_FALSE(should_start_next_iteration(1, 1, 1, 1, false));
    REQUIRE_FALSE(should_start_next_iteration(2000000000LL, 1000000000LL, 2000000000, 2000000000,
                                              true));
    REQUIRE(should_start_next_iteration(0, 0, 2000000000, 2000000000, true));
}
