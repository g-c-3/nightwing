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
// Scope: eval mobility (8 parameters) and, as a sanity-test target with a
// large and certain playing-strength effect, the base material values
// (8 non-anchored parameters; pawn stays anchored at 100) through
// play_match()'s weights_a/_b arguments. Other eval terms follow the same
// pattern through their own ParameterRef tables. Search constants
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

/// Settings for run_spsa_material(). Material is the sanity-test target
/// because a damaged material table has a large, certain effect on play
/// (unlike mobility, whose effect at fixed depth 6 is only a few Elo --
/// docs/DECISIONS.md, 2026-10-05 (6)).
struct SpsaMaterialConfig {
    SpsaConfig spsa;
    /// Per-iteration match. `num_games` is the games per SPSA iteration;
    /// eval_weights_a/_b are cleared internally (material only).
    MatchConfig match;
    /// Initial perturbation size for every non-anchored material parameter.
    /// Material values are in the hundreds, so the default is 20 (untuned).
    double c = 20.0;
    /// Bounds applied to every parameter (untuned).
    double min_value = 0.0;
    double max_value = 2000.0;
    /// Multiplier applied to every NON-ANCHORED default material value to
    /// form the starting vector (1.0 = the compiled-in defaults; pawn is
    /// anchored and always stays at its default). A value such as 0.7 is a
    /// deliberately wrong start whose deficit against the defaults is large
    /// and certain. The result is clamped to [min_value, max_value].
    double start_scale = 1.0;
    /// When true, each piece's endgame value is tied to its middlegame value
    /// and only four parameters (knight, bishop, rook, queen) are tuned.
    /// The compiled-in defaults already have mg == eg, and the endgame values
    /// received almost no signal in the first material sanity run (rook_eg
    /// clamped to 0, queen_eg 40; docs/DECISIONS.md, 2026-10-05 (8)). The
    /// starting value of each tied parameter is the scaled middlegame value.
    bool tie_mg_eg = false;
};

/// Plays material vector `a` against `b` (all other eval terms at their
/// defaults for both) through play_match() and returns the result from A's
/// side.
[[nodiscard]] MatchResult play_material_match(const eval::MaterialWeights& a,
                                              const eval::MaterialWeights& b,
                                              std::uint64_t base_seed, const MatchConfig& match);

/// Returns default_material_weights() with every non-anchored parameter
/// (knight through queen, mg and eg) multiplied by `scale` (not clamped,
/// not rounded). The anchored pawn values are left unchanged.
[[nodiscard]] eval::MaterialWeights scaled_material_weights(double scale);

/// Runs SPSA over the non-anchored eval::MaterialWeights from
/// scaled_material_weights(config.start_scale) and returns the optimizer
/// result (theta in kMaterialParameters order, anchored entries skipped; with
/// config.tie_mg_eg, theta is knight, bishop, rook, queen -- see
/// apply_tied_material_theta()).
[[nodiscard]] SpsaResult run_spsa_material(const SpsaMaterialConfig& config);

/// Writes a tied-mode theta (knight, bishop, rook, queen; extra or missing
/// entries ignored) into a copy of `base`, setting both the middlegame and
/// endgame value of each piece to the same number. Pawn is untouched.
[[nodiscard]] eval::MaterialWeights apply_tied_material_theta(eval::MaterialWeights base,
                                                              const std::vector<double>& theta);

} // namespace nightwing::tuner
