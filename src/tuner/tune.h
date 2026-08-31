#pragma once
// src/tuner/tune.h
//
// Gradient-descent Texel-style tuning loop — the second of this
// module's two ROADMAP.md Phase 5 sub-parts ("Texel/SPSA tuner module
// (self-play data generation + gradient descent)"; the first,
// tuner/selfplay.h, is done — see docs/DECISIONS.md, this file's own
// introducing entry). This is a from-scratch implementation of the
// publicly documented "Texel's Tuning Method"
// (https://www.chessprogramming.org/Texel%27s_Tuning_Method) — no code
// copied from Texel or any other engine/tuner.
//
// SCOPE, AS OF THIS SESSION: tunes ONLY eval::MaterialWeights (eval/
// psqt.h) — the five base piece values (pawn/knight/bishop/rook/queen,
// mg and eg each, 10 scalars total). Every other eval term (PSQT
// tables, pawn structure, mobility, king safety, and the rest of
// eval/*.h) is still read from its own compiled-in constexpr constant
// and is NOT yet tunable through this module — see docs/DECISIONS.md
// for the full rationale on why material values are this session's
// first (not only, eventually) covered term, and eval/psqt.h's own
// MaterialWeights doc comment for the runtime-mutable-parameter-vector
// design this will extend to cover more terms in a future session.
//
// ALGORITHM: for each of `iterations` steps, computes a NUMERICAL
// (finite-difference) gradient of compute_loss() with respect to every
// NON-ANCHORED parameter in kMaterialParameters (below) — pawn_mg/
// pawn_eg are anchored (kMaterialParameters' own comment has the full
// rationale: fixing the pawn removes a flat, degenerate scaling
// direction the loss surface otherwise has) and are never perturbed or
// updated, staying exactly equal to whatever `initial_weights` passed
// in for the whole run — not an analytic gradient.
// Material's own contribution to evaluate() happens to be exactly
// linear (each weight's analytic partial derivative would just be that
// piece type's board-relative count), which would make an analytic
// gradient easy for THIS term specifically — but this module
// deliberately doesn't special-case that: treating evaluate() as an
// opaque function of its weight vector (probe two nearby points,
// estimate the slope) is what will keep working unchanged once future
// sessions extend MaterialWeights-equivalent coverage to genuinely
// non-linear terms (PSQT interpolation, mobility, king safety, ...),
// where a hand-derived analytic gradient per term would be real,
// term-specific extra work every time. TuneConfig::finite_diff_epsilon
// (below) documents a real numerical subtlety this choice runs into
// given material_value()'s int-valued (round_to_int()-rounded, eval/
// psqt.h) output.
//
// LOSS: the standard Texel's Tuning Method loss — mean squared error
// between sigmoid(white_relative_eval / TuneConfig::sigmoid_scale) and
// each sampled position's actual game result (tuner::SelfPlayPosition::
// result, selfplay.h — White's perspective, matching evaluate()'s own
// White-relative convention with no per-position sign flip needed).
// `sigmoid_scale` (default 400.0, i.e. Texel's conventional K = 1/400)
// is a fixed constant here, not itself fit from the data — CPW's own
// Texel's Tuning Method page describes fitting K as a first, separate
// step before tuning the actual eval parameters; this module starts
// from the same commonly-used fixed value other from-scratch tuning
// write-ups typically start from instead, and fitting K properly is
// left as later refinement, not attempted this session.

#include <cstddef>
#include <array>
#include <vector>

#include "eval/psqt.h"
#include "tuner/selfplay.h"

namespace nightwing::tuner {

/// One entry in the enumerable material-weights parameter list — a
/// human-readable name paired with a pointer-to-member so
/// compute_gradient()/tune() (tune.cpp) can read and perturb every
/// field generically, in a loop, rather than ten hand-written per-field
/// lines — the "enumerable" half of the "enumerable, runtime-mutable
/// weights" abstraction docs/DECISIONS.md called for. A future session
/// extending coverage to another eval module's terms would add that
/// module's own fields to its own such table the same way, not change
/// this one's shape.
///
/// `anchored`: if true, tune() (tune.cpp) never estimates a gradient
/// for or updates this field — it stays exactly equal to whatever
/// `initial_weights` passed it, for the entire run. See kMaterialParameters'
/// own comment below for why pawn_mg/pawn_eg specifically are marked
/// this way.
struct MaterialParameterRef {
    const char* name;
    double eval::MaterialWeights::*member;
    bool anchored = false;
};

/// Every MaterialWeights field, in declaration order — see
/// MaterialParameterRef's own comment above. pawn_mg/pawn_eg are
/// `anchored = true`: Texel's Tuning Method (this file's own header
/// comment) fits a weight vector against sigmoid(eval / sigmoid_scale)
/// predictions, and since sigmoid_scale is a FIXED constant here, not
/// itself fit from the data, the loss surface has a genuine flat
/// direction along "scale every material weight down/up together" —
/// scaling the whole vector barely changes any prediction as long as
/// sigmoid_scale doesn't move to compensate, so gradient descent can
/// drift the entire vector toward zero (or away from it) without
/// actually improving the fit to real relative piece values. This was
/// observed directly, not just theorized: a 2026-08-31 production run
/// (5000 self-play games, 200 iterations) came back with pawn_mg fallen
/// to ~21% of its starting value while every other piece fell only
/// 10-20%, and the resulting weights scored no better in a 400-game
/// match against the untuned defaults (docs/DECISIONS.md, this entry's
/// own dated decision) — the textbook signature of this exact
/// degeneracy, not a genuine finding about pawns being overvalued.
/// Fixing pawn_mg/pawn_eg removes that flat direction entirely: every
/// other weight is now implicitly expressed AS a multiple of the pawn,
/// which is both the standard convention (piece values are
/// conventionally quoted "in pawns") and, more importantly here, a
/// hard anchor the optimizer can't drift. This is a cheap, standard fix
/// for this well-known issue — the alternative (also fitting
/// sigmoid_scale, CPW's own two-step recipe: fit K first, then the
/// weights) is a reasonable future refinement but a strictly larger
/// change than this session's scope called for.
inline constexpr std::array<MaterialParameterRef, 10> kMaterialParameters = {{
    {"pawn_mg", &eval::MaterialWeights::pawn_mg, /*anchored=*/true},
    {"pawn_eg", &eval::MaterialWeights::pawn_eg, /*anchored=*/true},
    {"knight_mg", &eval::MaterialWeights::knight_mg},
    {"knight_eg", &eval::MaterialWeights::knight_eg},
    {"bishop_mg", &eval::MaterialWeights::bishop_mg},
    {"bishop_eg", &eval::MaterialWeights::bishop_eg},
    {"rook_mg", &eval::MaterialWeights::rook_mg},
    {"rook_eg", &eval::MaterialWeights::rook_eg},
    {"queen_mg", &eval::MaterialWeights::queen_mg},
    {"queen_eg", &eval::MaterialWeights::queen_eg},
}};

/// Tunable knobs for the tuning run itself (distinct from
/// eval::MaterialWeights, the values BEING tuned).
struct TuneConfig {
    /// Number of gradient-descent steps to run. Each step costs
    /// `2 * kMaterialParameters.size() + 1` full passes over the
    /// training set (two for every parameter's finite-difference probe,
    /// plus one to record that step's resulting loss) — see tune.cpp's
    /// own comment at the loop for why full-batch (not
    /// mini-batch/stochastic) gradient descent was judged acceptable at
    /// this parameter count.
    int iterations = 100;

    /// Step size: each parameter moves `-learning_rate * gradient`
    /// every iteration. Empirically chosen (not a priori guessed) against
    /// this exact loss formulation (this file's own header comment): a
    /// sigmoid(eval / sigmoid_scale) squared-error loss produces
    /// inherently SMALL-magnitude gradients with respect to a weight
    /// measured in centipawn-like units — sigmoid's own slope is at
    /// most 0.25, scaled down again by the 1/sigmoid_scale (1/400)
    /// chain-rule factor from evaluate()'s own centipawn scale, so a
    /// one-centipawn change in a weight typically moves the loss by
    /// only on the order of 1e-4 to 1e-3. A learning rate near 1.0 (a
    /// plausible-LOOKING but untested first guess) was tried during this
    /// module's own development and left every material weight
    /// essentially frozen — the accumulated per-iteration step was too
    /// small to ever cross the integer rounding boundary
    /// `finite_diff_epsilon`'s own doc comment describes, so the loss
    /// never visibly changed even though gradient descent was, in a
    /// literal sense, "working." 20000 was chosen after directly
    /// measuring this scenario (a hand-built imbalanced test position,
    /// tests/tune_tests.cpp) at several candidate values: it produces
    /// steady, strictly monotonic loss reduction with no oscillation
    /// even after dozens of iterations, while smaller values (100–5000)
    /// made comparatively little progress in a realistic iteration
    /// budget. Still just a reasonable starting point for THIS small
    /// scale of test/development data, the same caveat as this file's
    /// own note on `iterations` — the actual production tuning pass
    /// (ROADMAP.md's next item) should re-check this against its own,
    /// much larger, real corpus.
    double learning_rate = 20000.0;

    /// Finite-difference perturbation size, in the same centipawn-like
    /// units as a MaterialWeights field. MUST be >= 1.0: material_value()
    /// (eval/psqt.h) rounds every MaterialWeights field to the nearest
    /// int (round_to_int()) before it ever reaches evaluate()'s actual
    /// scoring — a perturbation smaller than 1.0 can round back to the
    /// SAME int on both the +epsilon and -epsilon probe, which would
    /// silently estimate a zero gradient for that parameter even though
    /// its true (unrounded) slope isn't zero. 1.0 (the default) is the
    /// smallest value guaranteed to always cross an integer boundary in
    /// both directions.
    double finite_diff_epsilon = 1.0;

    /// Texel's Tuning Method's "K" constant, expressed as a divisor
    /// (`eval / sigmoid_scale`) rather than a multiplier, so its default
    /// (400.0) reads directly as "400 centipawns" — see this file's own
    /// header comment on why this is a fixed default, not fit from data,
    /// in this first version.
    double sigmoid_scale = 400.0;
};

/// One entry in TuneResult::history below — a single iteration's
/// resulting loss, for plotting/logging a tuning run's own convergence
/// (or lack of it) rather than only ever seeing the final number.
struct TuneIteration {
    int iteration; // 0 is the starting point, before any gradient step
    double loss;
};

/// Result of one tune() call.
struct TuneResult {
    /// The tuned weights after every iteration — the actual output a
    /// human (or ROADMAP.md's next item, "Tuned weights committed")
    /// would hand-transcribe back into eval/psqt.h's kPawnValue/.../
    /// kQueenValue constants, rounded via the same round_to_int() a
    /// tuning run's own loss computation already used internally.
    eval::MaterialWeights weights;

    /// history.front().loss (iteration 0, before any gradient step) —
    /// convenience accessor for "did tuning even help," without needing
    /// to separately compute compute_loss() at the original weights.
    double initial_loss = 0.0;

    /// history.back().loss (the final iteration) — same convenience
    /// reasoning as `initial_loss` above, for "where did it end up."
    double final_loss = 0.0;

    /// One entry per iteration, 0 (the starting point) through
    /// `TuneConfig::iterations` inclusive — `iterations + 1` entries
    /// total.
    std::vector<TuneIteration> history;
};

/// The logistic function `1 / (1 + e^-x)`, mapping any real `x` to
/// (0, 1) — used to convert a centipawn-scale evaluate() score into a
/// predicted win probability for compute_loss() below. A small enough
/// building block to test directly (tests/tune_tests.cpp) rather than
/// only indirectly through compute_loss()'s own behavior.
[[nodiscard]] double sigmoid(double x) noexcept;

/// Texel's Tuning Method loss (this file's own header comment) for
/// `weights` against every position in `positions` — the mean, over
/// every position, of the squared difference between
/// sigmoid(evaluate(position, weights) / sigmoid_scale) and that
/// position's own labeled result. Returns 0.0 for an empty
/// `positions` (rather than dividing by zero) — an edge case a caller
/// (tune(), below, or a test) might reasonably hit with a tiny or
/// empty training set; 0.0 ("no error observed because nothing was
/// checked") is a more sensible sentinel here than NaN.
///
/// Precondition: same as eval::evaluate()'s own — init_masks()/
/// init_magic_bitboards() have been called (this function parses each
/// position's FEN and evaluates it, both of which are transitively
/// movegen-adjacent — board::parse_fen() itself has no such
/// precondition, but eval::evaluate() does).
[[nodiscard]] double compute_loss(const std::vector<SelfPlayPosition>& positions,
                                   const eval::MaterialWeights& weights,
                                   double sigmoid_scale) noexcept;

/// Runs `config.iterations` steps of finite-difference gradient descent
/// (this file's own header comment for the full algorithm) starting
/// from `initial_weights` (defaults to eval::default_material_weights()
/// — the engine's current compiled-in values, the natural starting
/// point for a real tuning run) against `positions`, and returns the
/// result.
///
/// Precondition: same as compute_loss()'s own.
[[nodiscard]] TuneResult tune(const std::vector<SelfPlayPosition>& positions,
                               const eval::MaterialWeights& initial_weights =
                                   eval::default_material_weights(),
                               const TuneConfig& config = {});

} // namespace nightwing::tuner
