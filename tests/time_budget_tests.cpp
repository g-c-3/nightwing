// tests/time_budget_tests.cpp
//
// Unit tests for src/uci/time_budget.h (Session 222 time-management
// review): the pure budget arithmetic behind `go wtime/btime/winc/binc/
// movestogo` and `Move Overhead`, previously reachable only through a full
// `go` round trip. Also pins the Session 222 single-clock fix.

#include <catch2/catch_test_macros.hpp>

#include <climits>

#include "uci/time_budget.h"

using nightwing::uci::allocate_time_ms;
using nightwing::uci::apply_move_overhead;
using nightwing::uci::kDefaultIncrementPercent;
using nightwing::uci::kEngineIncrementPercent;
using nightwing::uci::kHardBudgetMultiplier;
using nightwing::uci::kMaxIncrementPercent;
using nightwing::uci::kMinIncrementPercent;
using nightwing::uci::select_own_clock_ms;
using nightwing::uci::TimeAllocation;

TEST_CASE("time_budget: default allocation is remaining/20, hard is 3x soft",
          "[uci][time_management]") {
    const TimeAllocation a = allocate_time_ms(60000, 0, 0);
    REQUIRE(a.soft_ms == 3000);
    REQUIRE(a.hard_ms == 3000 * kHardBudgetMultiplier);
}

TEST_CASE("time_budget: increment is added to the soft budget", "[uci][time_management]") {
    const TimeAllocation a = allocate_time_ms(60000, 1000, 0);
    REQUIRE(a.soft_ms == 4000);
    REQUIRE(a.hard_ms == 12000);
}

TEST_CASE("time_budget: a positive movestogo replaces the fixed 20-move guess",
          "[uci][time_management]") {
    const TimeAllocation a = allocate_time_ms(60000, 0, 10);
    REQUIRE(a.soft_ms == 6000);
    REQUIRE(a.hard_ms == 18000);
}

TEST_CASE("time_budget: movestogo <= 0 falls back to the default divisor",
          "[uci][time_management]") {
    const TimeAllocation def = allocate_time_ms(60000, 0, 0);
    for (int mtg : {0, -1, -5, INT_MIN}) {
        const TimeAllocation a = allocate_time_ms(60000, 0, mtg);
        REQUIRE(a.soft_ms == def.soft_ms);
        REQUIRE(a.hard_ms == def.hard_ms);
    }
}

TEST_CASE("time_budget: neither budget ever exceeds half the remaining clock",
          "[uci][time_management]") {
    // movestogo 1 would otherwise claim the whole clock.
    const TimeAllocation one = allocate_time_ms(10000, 0, 1);
    REQUIRE(one.soft_ms == 5000);
    REQUIRE(one.hard_ms == 5000);

    // A huge increment relative to the clock is also capped.
    const TimeAllocation inc = allocate_time_ms(1000, 5000, 0);
    REQUIRE(inc.soft_ms == 500);
    REQUIRE(inc.hard_ms == 500);
}

TEST_CASE("time_budget: a non-positive remaining clock yields the 50 ms minimum",
          "[uci][time_management]") {
    for (int rem : {0, -1, -1000, INT_MIN}) {
        const TimeAllocation a = allocate_time_ms(rem, 100, 0);
        REQUIRE(a.soft_ms == 50);
        REQUIRE(a.hard_ms == 50);
    }
}

TEST_CASE("time_budget: soft <= hard <= remaining/2 over a parameter sweep",
          "[uci][time_management]") {
    for (int rem : {2, 3, 10, 99, 100, 1000, 12345, 60000, 3600000}) {
        for (int inc : {0, 1, 50, 1000, 30000}) {
            for (int mtg : {0, 1, 2, 7, 20, 40, 100}) {
                const TimeAllocation a = allocate_time_ms(rem, inc, mtg);
                REQUIRE(a.soft_ms >= 1);
                REQUIRE(a.soft_ms <= a.hard_ms);
                REQUIRE(a.hard_ms <= rem / 2 + (rem / 2 < 1 ? 1 : 0));
            }
        }
    }
}

TEST_CASE("time_budget: extreme clock plus increment does not overflow int",
          "[uci][time_management]") {
    // remaining/1 + increment would exceed INT_MAX in plain `int` arithmetic.
    const TimeAllocation a = allocate_time_ms(INT_MAX, 1000000, 1);
    REQUIRE(a.soft_ms == INT_MAX / 2);
    REQUIRE(a.hard_ms == INT_MAX / 2);
}

TEST_CASE("time_budget: select_own_clock_ms prefers the side to move's own clock",
          "[uci][time_management]") {
    REQUIRE(select_own_clock_ms(5000, 7000, true) == 5000);
    REQUIRE(select_own_clock_ms(5000, 7000, false) == 7000);
    // A clock of exactly 0 is a real value, not "absent".
    REQUIRE(select_own_clock_ms(0, 7000, true) == 0);
    REQUIRE(select_own_clock_ms(5000, 0, false) == 0);
}

TEST_CASE("time_budget: a missing own clock falls back to the opponent's (Session 222 fix)",
          "[uci][time_management]") {
    // `go btime 6000` with White to move used to search unbounded.
    REQUIRE(select_own_clock_ms(-1, 6000, true) == 6000);
    REQUIRE(select_own_clock_ms(6000, -1, false) == 6000);
}

TEST_CASE("time_budget: no clock at all stays 'no clock-based time control'",
          "[uci][time_management]") {
    REQUIRE(select_own_clock_ms(-1, -1, true) == -1);
    REQUIRE(select_own_clock_ms(-1, -1, false) == -1);
}

TEST_CASE("time_budget: apply_move_overhead subtracts from both budgets and floors at 1 ms",
          "[uci][time_management]") {
    int hard = 900;
    int soft = 300;
    apply_move_overhead(hard, soft, 100);
    REQUIRE(hard == 800);
    REQUIRE(soft == 200);

    hard = 50;
    soft = 20;
    apply_move_overhead(hard, soft, 500);
    REQUIRE(hard == 1);
    REQUIRE(soft == 1);
}

TEST_CASE("time_budget: apply_move_overhead leaves absent (0) budgets untouched",
          "[uci][time_management]") {
    int hard = 0;
    int soft = 0;
    apply_move_overhead(hard, soft, 100);
    REQUIRE(hard == 0);
    REQUIRE(soft == 0);

    // movetime: hard only, no soft.
    hard = 1000;
    soft = 0;
    apply_move_overhead(hard, soft, 100);
    REQUIRE(hard == 900);
    REQUIRE(soft == 0);
}

TEST_CASE("time_budget: apply_move_overhead with zero overhead is the identity",
          "[uci][time_management]") {
    int hard = 900;
    int soft = 300;
    apply_move_overhead(hard, soft, 0);
    REQUIRE(hard == 900);
    REQUIRE(soft == 300);
}

TEST_CASE("time_budget: apply_move_overhead keeps soft <= hard", "[uci][time_management]") {
    for (int overhead : {0, 1, 10, 99, 100, 1000}) {
        int hard = 100;
        int soft = 100;
        apply_move_overhead(hard, soft, overhead);
        REQUIRE(soft <= hard);
        REQUIRE(hard >= 1);
        REQUIRE(soft >= 1);
    }
}

// --- IncrementPercent (Session 226) ---

TEST_CASE("time_budget: the default increment share is 100 and equals the two-argument form",
          "[uci][time_management]") {
    REQUIRE(kDefaultIncrementPercent == 100);
    for (int inc : {0, 100, 1000, 5000}) {
        const TimeAllocation a = allocate_time_ms(60000, inc, 0);
        const TimeAllocation b = allocate_time_ms(60000, inc, 0, 100);
        REQUIRE(a.soft_ms == b.soft_ms);
        REQUIRE(a.hard_ms == b.hard_ms);
    }
}

TEST_CASE("time_budget: a lower increment percent adds proportionally less to the soft budget",
          "[uci][time_management]") {
    // 60000 / 20 = 3000 base; increment 1000.
    REQUIRE(allocate_time_ms(60000, 1000, 0, 75).soft_ms == 3750);
    REQUIRE(allocate_time_ms(60000, 1000, 0, 50).soft_ms == 3500);
    REQUIRE(allocate_time_ms(60000, 1000, 0, 0).soft_ms == 3000);
    REQUIRE(allocate_time_ms(60000, 1000, 0, 75).hard_ms == 3750 * kHardBudgetMultiplier);
}

TEST_CASE("time_budget: an out-of-range increment percent is clamped, never over- or under-spends",
          "[uci][time_management]") {
    const TimeAllocation full = allocate_time_ms(60000, 1000, 0, kMaxIncrementPercent);
    const TimeAllocation none = allocate_time_ms(60000, 1000, 0, kMinIncrementPercent);
    for (int pct : {101, 1000, INT_MAX}) {
        REQUIRE(allocate_time_ms(60000, 1000, 0, pct).soft_ms == full.soft_ms);
    }
    for (int pct : {-1, -100, INT_MIN}) {
        REQUIRE(allocate_time_ms(60000, 1000, 0, pct).soft_ms == none.soft_ms);
    }
}

TEST_CASE("time_budget: increment percent keeps the half-clock cap and survives a huge increment",
          "[uci][time_management]") {
    const TimeAllocation a = allocate_time_ms(1000, 5000, 0, 75);
    REQUIRE(a.soft_ms == 500);
    REQUIRE(a.hard_ms == 500);
    // increment * percent must not overflow int.
    const TimeAllocation big = allocate_time_ms(INT_MAX, INT_MAX, 0, 75);
    REQUIRE(big.soft_ms == INT_MAX / 2);
    REQUIRE(big.hard_ms == INT_MAX / 2);
}

TEST_CASE("time_budget: the engine's IncrementPercent default is 75 and within the option range",
          "[uci][time_management]") {
    REQUIRE(kEngineIncrementPercent == 75);
    REQUIRE(kEngineIncrementPercent >= kMinIncrementPercent);
    REQUIRE(kEngineIncrementPercent <= kMaxIncrementPercent);
    // 60000 / 20 = 3000 base; increment 1000 -> 3000 + 750.
    REQUIRE(allocate_time_ms(60000, 1000, 0, kEngineIncrementPercent).soft_ms == 3750);
}
