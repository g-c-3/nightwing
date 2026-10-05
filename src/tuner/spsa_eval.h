#pragma once
// src/tuner/spsa_eval.h
//
// In-process binding of the SPSA optimizer core (spsa.h) to the eval
// mobility weights, using tuner::play_match() (match.h) as the match
// callback -- ROADMAP.md, "SPSA tuner" item, remaining step (1).
//
// Each SPSA iteration converts theta+ and theta- into two MobilityWeights,
// hands them to play_match() through MatchConfig::eval_weights_a/_b (the
// EvalWeightsOverride path), with material held at the compiled-in
// defaults for both sides, and returns side A's score. Colors alternate
// inside play_match(), so neither vector gets a tempo advantage.
//
// Scope: eval mobility only (8 parameters). Other eval terms follow the
// same pattern through their own ParameterRef tables. Search constants
// need the UCI-option binding (separate, later step). Fixed-depth games
// make runs reproducible and machine-speed independent (match.h), at the
// cost of not measuring time-managed play; any tuned value still needs
// SPRT gating before being committed.
//
// Requires board::init_masks(), init_magic_bitboards() and
// init_zobrist_keys() to have been called, as for play_match().
//
// From-scratch; algorithm credit is in spsa.h.

#include <cstdint>

#include "eval/mobility.h"
#include "tuner/match.h"
#include "tuner/spsa.h"

namespace nightwing::tuner {

/// Settings for run_spsa_mobility().
struct SpsaMobilityConfig {
    SpsaConfig spsa;
    /// Per-iteration match. `num_games` is the games per SPSA iteration;
    /// the eval_weights_a/_b pointers are overwritten internally.
    MatchConfig match;
    /// Initial perturbation size for every mobility parameter. The eval
    /// rounds mobility weights to integers (mobility.cpp), so values
    /// below about 1 are invisible to play; the default is 2 (untuned).
    double c = 2.0;
    /// Bounds applied to every parameter (untuned; defaults keep the
    /// bonuses non-negative and well below material scale).
    double min_value = 0.0;
    double max_value = 20.0;
    /// Multiplier applied to every default mobility weight to form the
    /// starting vector (1.0 = the compiled-in defaults). A value far from
    /// 1 (for example 0) is a deliberately wrong start, used to check that
    /// the optimizer recovers toward the defaults -- a sanity test of the
    /// tuner itself. The result is clamped to [min_value, max_value].
    double start_scale = 1.0;
};

/// Plays mobility vector `a` against `b` (material at defaults for both)
/// through play_match() and returns the result from A's side. Used by
/// run_spsa_mobility() and for validating a tuned vector against the
/// compiled-in defaults.
[[nodiscard]] MatchResult play_mobility_match(const eval::MobilityWeights& a,
                                              const eval::MobilityWeights& b,
                                              std::uint64_t base_seed, const MatchConfig& match);

/// Returns default_mobility_weights() with every parameter multiplied by
/// `scale` (not clamped, not rounded).
[[nodiscard]] eval::MobilityWeights scaled_mobility_weights(double scale);

/// Runs SPSA over eval::MobilityWeights from scaled_mobility_weights(
/// config.start_scale) and returns the optimizer result (theta in
/// kMobilityParameters order).
[[nodiscard]] SpsaResult run_spsa_mobility(const SpsaMobilityConfig& config);

} // namespace nightwing::tuner
