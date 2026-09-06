// tests/sprt_tests.cpp
//
// Unit tests for src/tuner/sprt.h/.cpp — ROADMAP.md Phase 8's "SPRT
// testing setup/process for validating future changes" item (see
// sprt.h's own header comment for the full design and attribution).
//
// This module has no dependency on board/search at all (pure
// statistics over win/draw/loss counts), so every test here runs
// essentially instantly, unlike tests/match_tests.cpp's own
// real-search-driven cases.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "tuner/sprt.h"

using namespace nightwing::tuner;

namespace {
constexpr double kTol = 1e-6;
}

TEST_CASE("elo_to_score: zero Elo difference is an even 0.5", "[tuner][sprt]") {
    REQUIRE(std::abs(elo_to_score(0.0) - 0.5) < kTol);
}

TEST_CASE("elo_to_score: +400 Elo matches the standard 10/11 logistic value", "[tuner][sprt]") {
    // 1 / (1 + 10^(-400/400)) == 1 / (1 + 0.1) == 10/11.
    REQUIRE(std::abs(elo_to_score(400.0) - (10.0 / 11.0)) < kTol);
}

TEST_CASE("elo_to_score: negative Elo mirrors the positive value around 0.5", "[tuner][sprt]") {
    const double up = elo_to_score(37.0);
    const double down = elo_to_score(-37.0);
    REQUIRE(std::abs((up - 0.5) - (0.5 - down)) < kTol);
}

TEST_CASE("compute_sprt: zero games is Continue with llr exactly 0", "[tuner][sprt]") {
    const SprtState state = compute_sprt(0, 0, 0);
    REQUIRE(state.status == SprtStatus::Continue);
    REQUIRE(state.llr == 0.0);
}

TEST_CASE("compute_sprt: default alpha=beta=0.05 bounds match ln(0.05/0.95) / ln(0.95/0.05)",
          "[tuner][sprt]") {
    const SprtState state = compute_sprt(0, 0, 0);
    const double expected_lower = std::log(0.05 / 0.95);
    const double expected_upper = std::log(0.95 / 0.05);
    REQUIRE(std::abs(state.lower_bound - expected_lower) < kTol);
    REQUIRE(std::abs(state.upper_bound - expected_upper) < kTol);
    // Symmetric error rates -> symmetric bounds around zero.
    REQUIRE(std::abs(state.lower_bound + state.upper_bound) < kTol);
}

TEST_CASE("compute_sprt: an all-draws record has zero variance and stays Continue",
          "[tuner][sprt]") {
    // Every game identical (a draw) -- the LLR formula's denominator
    // would be zero; this must be treated as "not enough information
    // yet", not a crash or a spurious decision (sprt.h's own doc
    // comment on this exact case).
    const SprtState state = compute_sprt(0, 50, 0);
    REQUIRE(state.status == SprtStatus::Continue);
    REQUIRE(state.llr == 0.0);
}

TEST_CASE("compute_sprt: a decisively winning record accepts H1", "[tuner][sprt]") {
    // Hand-derived (sprt.h/.cpp's own introducing DECISIONS.md entry):
    // wins=120, draws=0, losses=80 (score 0.6) under elo0=0, elo1=50
    // gives llr ~= 3.82, above the default upper bound (~2.944).
    SprtConfig config;
    config.elo0 = 0.0;
    config.elo1 = 50.0;
    const SprtState state = compute_sprt(120, 0, 80, config);
    REQUIRE(state.status == SprtStatus::AcceptH1);
    REQUIRE(state.llr > state.upper_bound);
}

TEST_CASE("compute_sprt: a decisively losing record accepts H0", "[tuner][sprt]") {
    // Mirror image of the previous case: wins=80, losses=120 (score
    // 0.4) under the same elo0/elo1 gives a strongly negative llr,
    // well below the default lower bound.
    SprtConfig config;
    config.elo0 = 0.0;
    config.elo1 = 50.0;
    const SprtState state = compute_sprt(80, 0, 120, config);
    REQUIRE(state.status == SprtStatus::AcceptH0);
    REQUIRE(state.llr < state.lower_bound);
}

TEST_CASE("compute_sprt: a small sample under close hypotheses stays Continue", "[tuner][sprt]") {
    // wins=6, losses=4 (score 0.6) out of only 10 games, under the
    // default elo0=0/elo1=5 (a close, hard-to-distinguish pair of
    // hypotheses) -- nowhere near enough evidence yet.
    const SprtState state = compute_sprt(6, 0, 4);
    REQUIRE(state.status == SprtStatus::Continue);
    REQUIRE(state.llr > state.lower_bound);
    REQUIRE(state.llr < state.upper_bound);
}

TEST_CASE("compute_sprt: more games in the same direction never flips a decision back to Continue",
          "[tuner][sprt]") {
    // Once decisively in AcceptH1 territory, adding more games with the
    // same win rate should keep (or strengthen) that decision, not
    // wobble back to Continue -- a basic sanity/monotonicity check on
    // the LLR formula's own behavior as n grows with a fixed score.
    SprtConfig config;
    config.elo0 = 0.0;
    config.elo1 = 50.0;
    const SprtState smaller = compute_sprt(120, 0, 80, config);
    const SprtState larger = compute_sprt(240, 0, 160, config);
    REQUIRE(smaller.status == SprtStatus::AcceptH1);
    REQUIRE(larger.status == SprtStatus::AcceptH1);
    REQUIRE(larger.llr > smaller.llr);
}

TEST_CASE("compute_sprt: tighter alpha/beta bounds widen the decision thresholds",
          "[tuner][sprt]") {
    SprtConfig loose;
    loose.alpha = 0.05;
    loose.beta = 0.05;
    SprtConfig tight;
    tight.alpha = 0.01;
    tight.beta = 0.01;

    const SprtState loose_state = compute_sprt(0, 0, 0, loose);
    const SprtState tight_state = compute_sprt(0, 0, 0, tight);
    REQUIRE(tight_state.upper_bound > loose_state.upper_bound);
    REQUIRE(tight_state.lower_bound < loose_state.lower_bound);
}
