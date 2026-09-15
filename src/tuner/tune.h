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
// SCOPE, AS OF THIS SESSION: `tune()` below still only tunes
// `kMaterialParameters` — only the 5 base material weights (pawn/
// knight/bishop/rook/queen, mg and eg each, 10 scalars total) are
// actually Texel-tuned by calling tune() today. Tier 0 Step 3 (docs/
// DECISIONS.md, this file's own ParameterRef<Weights> entry)
// generalized the enumerable-parameter-list ABSTRACTION itself
// (MaterialParameterRef/kMaterialParameters below) to also cover
// PsqtWeights (kPsqtParameters, further down this file); Tier 0 Step 4
// (docs/DECISIONS.md, this file's own compute_loss() `psqt_weights`
// entry) then wired `eval::PsqtWeights` all the way through
// `compute_loss()`/`eval::evaluate()`'s own nullable-override
// convention — `compute_loss()` below now accepts an OPTIONAL
// `psqt_weights` parameter alongside its required material `weights`,
// forwarding both independently to `evaluate()`. `tune()` itself,
// however, still only enumerates/updates `kMaterialParameters` — it
// does not yet pass a non-null `psqt_weights` through to
// `compute_loss()`, so a real PSQT-tuning run isn't possible by
// calling `tune()` yet, only by calling `compute_loss()` directly at a
// hand-picked PSQT vector (which is exactly what this session's own
// new tests below do, to verify the wiring). Generalizing `tune()`
// itself to actually run a `kPsqtParameters`-driven tuning job is a
// separate, later step — see ROADMAP.md Tier 0's remaining steps.
// Every other eval term (mobility, king safety, pawn structure, space,
// threats, and the rest of eval/*.h) is still read from its own
// compiled-in constexpr constant and has no ParameterRef table at all
// yet — see docs/DECISIONS.md for the full rationale on why material
// values were this module's first covered term, and eval/psqt.h's own
// MaterialWeights/PsqtWeights doc comments for the runtime-mutable-
// parameter-vector design those two terms already have.
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
#include <utility>
#include <vector>

#include "eval/psqt.h"
#include "tuner/selfplay.h"

namespace nightwing::tuner {

/// One entry in an enumerable parameter list — a human-readable name
/// paired with EITHER a plain scalar member-pointer (`member`, for a
/// `double Weights::*` field like MaterialWeights' own 10) OR an
/// indexed array member-pointer (`array_member` + `index`, for a
/// `std::array<double,64> Weights::*` field like PsqtWeights' own 12)
/// — so compute_gradient()/tune() (tune.cpp) can read and perturb every
/// field generically, in a loop, through get()/set() below, regardless
/// of which of the two shapes a given `Weights` type's fields happen to
/// be. Originally just `MaterialParameterRef` (this struct's own prior,
/// material-only version, ROADMAP.md Tier 0 Step 1-and-earlier), now
/// generalized as `ParameterRef<Weights>` — a template rather than a
/// second, PSQT-specific struct — per Tier 0 Step 3 (docs/DECISIONS.md,
/// this entry's own date): kMaterialParameters and kPsqtParameters
/// below are both `std::array<ParameterRef<...>, N>` instantiated over
/// their own `Weights` type, sharing one generic get()/set()
/// implementation and one generic tune()/compute_gradient() consumer
/// (tune.cpp), rather than parallel material-specific and PSQT-specific
/// code paths.
///
/// Precondition: exactly one of `member`/`array_member` is non-null for
/// any given entry — `get()`/`set()` below branch on `array_member`
/// being non-null, so a (never intentionally constructed) entry with
/// BOTH non-null would silently ignore `member` entirely, and an entry
/// with NEITHER set would dereference a null member pointer. Every
/// entry in kMaterialParameters/kPsqtParameters below satisfies this by
/// construction (kMaterialParameters sets only `member`; kPsqtParameters'
/// own generator, further down this file, sets only `array_member`+
/// `index`), so this is a structural invariant of how those two tables
/// are BUILT, not something a caller needs to check per lookup.
///
/// Field order is deliberate: `name`, `member`, `anchored` are first,
/// in that exact order, matching this struct's own pre-generalization
/// shape exactly — kMaterialParameters' existing 3-and-2-positional-
/// argument brace-init entries below (`{"pawn_mg", &..., true}`,
/// `{"knight_mg", &...}`) and existing tests (tests/tune_tests.cpp's
/// own direct `.member`/positional-init usage) both keep compiling
/// completely unchanged this way — the two new fields (`array_member`,
/// `index`) are appended at the end specifically so nothing already
/// relying on this struct's original 3-field positional shape needed
/// to be touched by this generalization.
///
/// `anchored`: if true, tune() (tune.cpp) never estimates a gradient
/// for or updates this field — it stays exactly equal to whatever
/// `initial_weights` passed it, for the entire run. See kMaterialParameters'
/// own comment below for why pawn_mg/pawn_eg specifically are marked
/// this way; kPsqtParameters' own comment below has this session's
/// decision on why no PSQT parameter is anchored (yet).
template <typename Weights>
struct ParameterRef {
    const char* name;
    double Weights::* member = nullptr;
    bool anchored = false;
    std::array<double, 64> Weights::* array_member = nullptr;
    int index = 0;

    /// Reads this parameter's current value out of `w` — `w.*array_member
    /// [index]` if this is an indexed-array entry, `w.*member` otherwise.
    [[nodiscard]] double get(const Weights& w) const noexcept {
        return array_member != nullptr ? (w.*array_member)[index] : w.*member;
    }

    /// Writes `value` into this parameter's field in `w` — the exact
    /// inverse of get() above, same branch condition.
    void set(Weights& w, double value) const noexcept {
        if (array_member != nullptr) {
            (w.*array_member)[index] = value;
        } else {
            w.*member = value;
        }
    }
};

/// Backward-compatible name for `ParameterRef<eval::MaterialWeights>` —
/// this struct's own pre-generalization name (Tier 0 Step 1-and-earlier),
/// kept as an alias rather than renamed at every existing call site
/// (tune.cpp, tests/tune_tests.cpp) purely to minimize this session's
/// own diff; a future session has no obligation to keep this alias if
/// it ever becomes confusing to have two names for the same type.
using MaterialParameterRef = ParameterRef<eval::MaterialWeights>;

/// `ParameterRef<eval::PsqtWeights>` — the PSQT-side counterpart to
/// MaterialParameterRef above, introduced this session (Tier 0 Step 3)
/// alongside kPsqtParameters below.
using PsqtParameterRef = ParameterRef<eval::PsqtWeights>;

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

namespace detail {

// MSVC-only parsing issue, confirmed via GitHub Actions CI (Windows
// Debug/Release, 2026-09-15, both failing identically): the pointer-
// to-member-array type below (`std::array<double,64> eval::PsqtWeights
// ::*`), when written directly inline as a nested `std::pair<...>`
// template argument the way kPsqtFields originally declared it, fails
// to parse under MSVC's cl.exe -- "error C3083: 'nightwing': the
// symbol to the left of a '::' must be a type", cascading into ~10
// follow-on parse errors on the same declaration -- even though the
// exact same declaration compiled cleanly under GCC 13.3.0 (this
// session's own earlier sandbox verification) and under GCC/Clang on
// the CI's own Linux/macOS runners (all 4 of those jobs passed
// completely, same CI run, docs/SESSIONS.md's own dated entry). This
// is a known class of MSVC limitation: a pointer-to-member type whose
// pointee is itself a template instantiation (`std::array<double,64>`
// here), written inline nested inside ANOTHER template's argument
// list, confuses MSVC's parser into not recognizing the pointee type's
// namespace-qualified owning class (`eval::PsqtWeights`) as a type at
// all. Naming the pointer-to-member type on its OWN line first, as a
// plain type alias, then using that alias (not the inline expression)
// as the nested template argument, is the standard, portable
// workaround -- confirmed fixed against the same MSVC toolchain via a
// follow-up CI run (docs/DECISIONS.md, this entry's own dated
// decision) -- and, independent of the MSVC issue, arguably clearer to
// read regardless.
using PsqtArrayMemberPtr = std::array<double, 64> eval::PsqtWeights::*;

/// One PsqtWeights array field's name paired with its member pointer —
/// the 12 entries kPsqtParameters' own generator (below) expands into
/// 64 indexed ParameterRef entries apiece (12 * 64 = 768 total). Kept
/// as its own small private (detail-namespace) table, separate from
/// kPsqtParameters itself, purely so the 12-vs-768 distinction is
/// visible in the source rather than requiring a reader to count array
/// literal entries.
inline constexpr std::array<std::pair<const char*, PsqtArrayMemberPtr>, 12> kPsqtFields = {{
        {"pawn_mg", &eval::PsqtWeights::pawn_mg},
        {"pawn_eg", &eval::PsqtWeights::pawn_eg},
        {"knight_mg", &eval::PsqtWeights::knight_mg},
        {"knight_eg", &eval::PsqtWeights::knight_eg},
        {"bishop_mg", &eval::PsqtWeights::bishop_mg},
        {"bishop_eg", &eval::PsqtWeights::bishop_eg},
        {"rook_mg", &eval::PsqtWeights::rook_mg},
        {"rook_eg", &eval::PsqtWeights::rook_eg},
        {"queen_mg", &eval::PsqtWeights::queen_mg},
        {"queen_eg", &eval::PsqtWeights::queen_eg},
        {"king_mg", &eval::PsqtWeights::king_mg},
        {"king_eg", &eval::PsqtWeights::king_eg},
    }};

/// Expands kPsqtFields' 12 array-field descriptors into the full
/// 768-entry kPsqtParameters table below (one PsqtParameterRef per
/// piece/phase/square) — a `constexpr` loop rather than 768 hand-typed
/// initializer lines, both because typing 768 lines by hand invites
/// transcription errors this table's own entries have no independent
/// source to cross-check against (unlike psqt.cpp's own table VALUES,
/// which were cross-checked against two published transcriptions -- see
/// psqt.cpp's header comment), and because the whole point of
/// generalizing ParameterRef this session was to make exactly this kind
/// of enumeration mechanical rather than manual.
[[nodiscard]] constexpr std::array<PsqtParameterRef, 768> make_psqt_parameters() noexcept {
    std::array<PsqtParameterRef, 768> result{};
    std::size_t out = 0;
    for (const auto& field : kPsqtFields) {
        for (int sq = 0; sq < 64; ++sq) {
            result[out] = PsqtParameterRef{.name = field.first, .array_member = field.second,
                                            .index = sq};
            ++out;
        }
    }
    return result;
}

} // namespace detail

/// Every PsqtWeights array field, every square, in kPsqtFields' own
/// declaration order (pawn_mg[0..63], pawn_eg[0..63], ..., king_eg
/// [0..63]) — the PSQT-side counterpart to kMaterialParameters above,
/// introduced this session (Tier 0 Step 3, docs/DECISIONS.md, this
/// entry's own date) specifically so a future PSQT-aware tune() call
/// can enumerate every PSQT cell the same uniform, loop-driven way
/// tune()/compute_gradient() (tune.cpp) already enumerate
/// kMaterialParameters' 10 scalars, once Tier 0 Step 4 (wiring
/// PsqtWeights through compute_loss()/eval::evaluate()'s own nullable-
/// override convention, ROADMAP.md) makes a PSQT-aware compute_loss()
/// actually possible to call.
///
/// NOT YET CONSUMED by tune()/compute_loss() below, which as of this
/// session still operate on eval::MaterialWeights specifically, not a
/// `Weights`-templated Weights parameter — this table exists and is
/// independently correct/tested (tests/tune_tests.cpp) ahead of that
/// wiring, not as a half-finished dependency of it. Every PSQT
/// parameter here has `anchored = false` (the field's own default):
/// unlike kMaterialParameters' pawn_mg/pawn_eg, PSQT terms don't share
/// material's specific flat-scaling-direction degeneracy (docs/
/// DECISIONS.md, kMaterialParameters' own comment above) -- ADDING a
/// per-square PSQT bonus doesn't multiplicatively rescale every other
/// term the way MULTIPLYING every material weight together does, so
/// there is no known equivalent degenerate direction here to anchor
/// against yet. A real production PSQT tuning run (once Steps 4-6 land)
/// may surface a different, PSQT-specific degeneracy worth anchoring
/// against -- revisit this decision then, against real data, rather
/// than guessing an anchor now the same way kMaterialParameters' own
/// comment above describes an untested learning_rate=1.0 guess having
/// gone wrong for a different parameter.
inline constexpr std::array<PsqtParameterRef, 768> kPsqtParameters = detail::make_psqt_parameters();

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
/// `psqt_weights`, if non-null, is forwarded to eval::evaluate() the
/// same way `weights` (material) already is — the PSQT-side
/// counterpart introduced this session (Tier 0 Step 4, docs/
/// DECISIONS.md, this parameter's own dated entry) specifically so a
/// call site can compute this loss at a candidate PSQT vector, holding
/// material at `weights`, without needing a second copy of this
/// function. Defaults to nullptr (compiled-in PSQT constants) — every
/// existing caller (tune() below, tests/tune_tests.cpp) is unaffected.
/// NOTE: tune() itself (below) does NOT yet pass a non-null
/// `psqt_weights` through to this parameter — it still only tunes
/// `kMaterialParameters` — see tune()'s own doc comment and ROADMAP.md
/// Tier 0's remaining steps for why that generalization is deliberately
/// a separate, later step from this one.
///
/// Precondition: same as eval::evaluate()'s own — init_masks()/
/// init_magic_bitboards() have been called (this function parses each
/// position's FEN and evaluates it, both of which are transitively
/// movegen-adjacent — board::parse_fen() itself has no such
/// precondition, but eval::evaluate() does).
[[nodiscard]] double compute_loss(const std::vector<SelfPlayPosition>& positions,
                                   const eval::MaterialWeights& weights, double sigmoid_scale,
                                   const eval::PsqtWeights* psqt_weights = nullptr) noexcept;

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
