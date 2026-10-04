#pragma once
// src/tuner/spsa.h
//
// SPSA (Simultaneous Perturbation Stochastic Approximation) optimizer core
// -- ROADMAP.md, "SPSA tuner" item (added 2026-10-02).
//
// WHAT THIS IS: a small, engine-agnostic optimizer. Each iteration it draws a
// random +/-1 vector (delta), builds two parameter vectors theta+ and theta-
// (theta +/- c_k * delta, per-parameter), asks a caller-supplied callback to
// play them against each other and report theta+'s match score, and moves
// theta along the observed score difference. Strength is measured by play,
// not by a labeled-position loss surface, which is what lets this approach
// sidestep the label-fit versus playing-strength gap that the Texel tuner
// (tune.h) has and the open EG-term identifiability item.
//
// WHAT THIS IS NOT (yet): there is no CLI and no wiring to tuner::play_match()
// or the two-process UCI runner in this file. The callback boundary is the
// seam: an in-process eval-constant binding (ParameterRef + play_match) and a
// UCI-option search-constant binding are separate, later steps. Any value this
// produces still requires SPRT gating before being committed, as for every
// tuner output.
//
// GAIN SCHEDULES (Spall's standard forms, CPW "SPSA"):
//     c_k   = c_i / (k + 1)^gamma                 (perturbation size, per parameter i)
//     R_k   = r0  / (A + k + 1)^alpha             (step gain)
//     step  = R_k * c_k * (y+ - y-) * delta_i / 2 (per parameter)
// The c_k^2 step scaling (so that large-range parameters move proportionally
// further) follows the convention popularized by Stockfish's fishtest SPSA
// tuner; the algorithm itself is Spall's. alpha = 0.602 and gamma = 0.101 are
// Spall's recommended practical exponents. r0 and A are untuned starting
// values, flagged as such like every other margin constant in this codebase.
//
// y+ and y- are the same match seen from both sides: y+ is the callback's
// score for theta+ (1 win, 0.5 draw, 0 loss, averaged), and y- = 1 - y+.
// So (y+ - y-) = 2 * y+ - 1, and a drawn match (0.5) leaves theta unchanged.
//
// ROUNDING: the evaluator rounds most weights to integers (docs/DECISIONS.md,
// 2026-09-22 (4)), so a perturbation smaller than about 1 can be invisible to
// play. Callers should choose each SpsaParam::c accordingly (at least 1 for
// integer-rounded eval terms); this module does not guess.
//
// No NNUE, no neural nets, no external dependencies. From-scratch
// implementation of the published algorithm; no code copied from any tool.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightwing::tuner {

/// One tunable parameter's starting value, perturbation size and bounds.
struct SpsaParam {
    std::string name;
    double start = 0.0;
    /// Initial perturbation size c_i (the k = 0 value; it decays with k).
    double c = 1.0;
    double min = -1.0e9;
    double max = 1.0e9;
};

/// Optimizer settings. Defaults are untuned starting values.
struct SpsaConfig {
    int iterations = 100;
    /// Step gain numerator R_0 scale. See the schedule in the file comment.
    double r0 = 1.0;
    /// Stability constant A in the gain denominator. Negative selects the
    /// conventional 10% of `iterations`.
    double stability = -1.0;
    double alpha = 0.602;
    double gamma = 0.101;
    std::uint64_t seed = 1;
};

/// Plays theta+ against theta- and returns theta+'s score in [0, 1]
/// (win = 1, draw = 0.5, loss = 0). `game_seed` is distinct per iteration so
/// an implementation can derive reproducible openings from it.
using SpsaMatchFn = std::function<double(const std::vector<double>& plus,
                                         const std::vector<double>& minus,
                                         std::uint64_t game_seed)>;

/// Result of run_spsa().
struct SpsaResult {
    std::vector<double> theta;          ///< Final parameter vector, same order as the input.
    int iterations_run = 0;
    std::vector<double> plus_scores;    ///< theta+'s score per iteration (progress trace).
};

/// Runs SPSA from each parameter's `start`, calling `match` once per
/// iteration. Every returned and proposed value is clamped to [min, max].
/// A score outside [0, 1] from the callback is clamped. Deterministic for a
/// fixed `config.seed` and a deterministic callback. An empty parameter list
/// returns an empty result with iterations_run == 0.
[[nodiscard]] SpsaResult run_spsa(const std::vector<SpsaParam>& params, const SpsaConfig& config,
                                  const SpsaMatchFn& match);

/// Builds an SpsaParam list from a ParameterRef table (tune.h) and a Weights
/// instance: `start` is read with ParameterRef::get(), every parameter gets
/// perturbation `c` and the shared bounds. Anchored entries are skipped, the
/// same convention tune() uses. `Table` is any indexable container of
/// ParameterRef<Weights> (for example kMobilityParameters).
template <typename Weights, typename Table>
[[nodiscard]] std::vector<SpsaParam> make_spsa_params(const Table& table, const Weights& w, double c,
                                                      double min_value, double max_value) {
    std::vector<SpsaParam> out;
    for (const auto& ref : table) {
        if (ref.anchored) {
            continue;
        }
        out.push_back({ref.name, ref.get(w), c, min_value, max_value});
    }
    return out;
}

/// Writes `theta` (same order and filtering as make_spsa_params()) back into
/// a copy of `base` and returns it. Extra or missing entries are ignored
/// rather than read out of bounds.
template <typename Weights, typename Table>
[[nodiscard]] Weights apply_spsa_theta(const Table& table, Weights base, const std::vector<double>& theta) {
    std::size_t i = 0;
    for (const auto& ref : table) {
        if (ref.anchored) {
            continue;
        }
        if (i >= theta.size()) {
            break;
        }
        ref.set(base, theta[i]);
        ++i;
    }
    return base;
}

} // namespace nightwing::tuner
