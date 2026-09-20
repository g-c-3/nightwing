// src/tuner/tune.cpp
//
// See tune.h.

#include "tuner/tune.h"

#include <cmath>

#include "board/fen.h"
#include "eval/eval.h"

namespace nightwing::tuner {

namespace {

/// L2 penalty (ROADMAP.md Tier 0 Step 5): `lambda * sum(param^2)` over
/// every NON-ANCHORED entry in `params`, evaluated against `weights`.
/// Anchored parameters are skipped — see TuneConfig::l2_lambda's own doc
/// comment (tune.h) for why. Short-circuits to exactly 0.0 for
/// lambda == 0.0 without touching `params` at all, so a default
/// (unregularized) run's reported loss is bit-for-bit compute_loss()'s
/// own bare MSE, not merely "a very small number away from it."
template <typename Weights, std::size_t N>
[[nodiscard]] double l2_penalty(const std::array<ParameterRef<Weights>, N>& params,
                                 const Weights& weights, double lambda) noexcept {
    if (lambda == 0.0) {
        return 0.0;
    }
    double sum_squares = 0.0;
    for (const ParameterRef<Weights>& param : params) {
        if (param.anchored) {
            continue;
        }
        const double value = param.get(weights);
        sum_squares += value * value;
    }
    return lambda * sum_squares;
}

} // namespace

double sigmoid(double x) noexcept { return 1.0 / (1.0 + std::exp(-x)); }

double compute_loss(const std::vector<SelfPlayPosition>& positions,
                     const eval::MaterialWeights& weights, double sigmoid_scale,
                     const eval::PsqtWeights* psqt_weights,
                     const eval::MobilityWeights* mobility_weights,
                     const eval::SpaceWeights* space_weights,
                     const eval::ThreatsWeights* threats_weights,
                     const eval::KingSafetyWeights* king_safety_weights) noexcept {
    if (positions.empty()) {
        return 0.0;
    }

    double sum_squared_error = 0.0;
    for (const SelfPlayPosition& position : positions) {
        const board::Position pos = board::parse_fen(position.fen);
        // pawn_tt/eval_cache both nullptr: this is an offline tuning
        // computation, not the search hot path either cache exists to
        // speed up (eval/pawn_tt.h's/eval/eval_cache.h's own header
        // comments), and eval_cache specifically MUST stay uninvolved
        // here regardless -- see evaluate()'s own doc comment on why it
        // never consults eval_cache when a material_weights, psqt_weights,
        // mobility_weights, space_weights, threats_weights, OR
        // king_safety_weights override is in play.
        const int white_relative =
            eval::evaluate(pos, nullptr, nullptr, &weights, psqt_weights, mobility_weights,
                            space_weights, threats_weights, king_safety_weights);
        const double predicted = sigmoid(static_cast<double>(white_relative) / sigmoid_scale);
        const double error = predicted - position.result;
        sum_squared_error += error * error;
    }
    return sum_squared_error / static_cast<double>(positions.size());
}

TuneResult tune(const std::vector<SelfPlayPosition>& positions,
                 const eval::MaterialWeights& initial_weights, const TuneConfig& config) {
    TuneResult result;
    result.weights = initial_weights;
    result.initial_loss = compute_loss(positions, result.weights, config.sigmoid_scale) +
                           l2_penalty(kMaterialParameters, result.weights, config.l2_lambda);
    result.history.push_back(TuneIteration{0, result.initial_loss});

    std::array<double, kMaterialParameters.size()> gradient{};

    for (int iteration = 1; iteration <= config.iterations; ++iteration) {
        // Full-batch numerical gradient: every parameter's finite-
        // difference probe evaluates compute_loss() over the WHOLE
        // training set, not a random mini-batch -- acceptable at this
        // module's current scale (10 parameters, a self-play-sized
        // training set -- tuner/selfplay.h's own defaults produce a
        // modest number of games) where each iteration's
        // `2 * kMaterialParameters.size() + 1` compute_loss() calls stay
        // cheap; a future session covering many more eval terms with a
        // much larger corpus might need to revisit this for runtime,
        // but that's not a concern this module's first, material-only
        // version needs to solve yet.
        for (std::size_t i = 0; i < kMaterialParameters.size(); ++i) {
            // Anchored parameters (kMaterialParameters' own comment: pawn_mg/
            // pawn_eg, to remove the loss surface's flat scale-degeneracy
            // direction) never move, so there's no point spending two
            // compute_loss() passes estimating a gradient for one — leave
            // it at exactly 0.0 and skip straight to the next parameter.
            if (kMaterialParameters[i].anchored) {
                gradient[i] = 0.0;
                continue;
            }

            const MaterialParameterRef& param = kMaterialParameters[i];
            const double original = param.get(result.weights);

            param.set(result.weights, original + config.finite_diff_epsilon);
            const double loss_plus = compute_loss(positions, result.weights, config.sigmoid_scale);

            param.set(result.weights, original - config.finite_diff_epsilon);
            const double loss_minus =
                compute_loss(positions, result.weights, config.sigmoid_scale);

            param.set(result.weights, original); // restore before moving to the next parameter
            gradient[i] = (loss_plus - loss_minus) / (2.0 * config.finite_diff_epsilon);

            // ROADMAP.md Tier 0 Step 5: the L2 penalty's own gradient
            // (d/dw[lambda*w^2] = 2*lambda*w) is added ANALYTICALLY here
            // rather than folded into the finite-difference probe above —
            // it's already known in closed form, exactly, so there's no
            // reason to spend two more compute_loss() calls (or add any
            // extra finite-difference noise) discovering a slope this
            // simple. Exactly 0.0 whenever config.l2_lambda is 0.0 (the
            // default), leaving this identical to the pre-Step-5 gradient.
            gradient[i] += 2.0 * config.l2_lambda * original;
        }

        // Apply every parameter's step simultaneously (true gradient
        // descent), only after every parameter's own gradient has been
        // estimated against the SAME starting weights -- updating a
        // parameter in place before the next parameter's own finite-
        // difference probe would make each probe see a partially-
        // updated weight vector instead of a consistent one, closer to
        // coordinate descent than the gradient descent this module (and
        // ROADMAP.md's own wording) means to implement.
        for (std::size_t i = 0; i < kMaterialParameters.size(); ++i) {
            // gradient[i] is already exactly 0.0 for an anchored parameter
            // (the loop above), so this would be a no-op anyway -- skipped
            // explicitly rather than relying on that, so an anchored
            // field's value is provably never touched by this function at
            // all, not just arithmetically unchanged by it.
            if (kMaterialParameters[i].anchored) {
                continue;
            }
            const MaterialParameterRef& param = kMaterialParameters[i];
            param.set(result.weights, param.get(result.weights) - config.learning_rate * gradient[i]);
        }

        const double loss_after_step =
            compute_loss(positions, result.weights, config.sigmoid_scale) +
            l2_penalty(kMaterialParameters, result.weights, config.l2_lambda);
        result.history.push_back(TuneIteration{iteration, loss_after_step});
    }

    result.final_loss = result.history.back().loss;
    return result;
}

namespace {

/// Every PsqtWeights field paired with its own analytic-gradient
/// accumulator field, both `std::array<double, 64>*` — lets
/// compute_psqt_gradient() below apply "add this position's
/// contribution" / "divide by N" as one small loop over 12 (field,
/// gradient-field) pairs instead of 12 hand-repeated statements each.
struct PsqtFieldPair {
    detail::PsqtArrayMemberPtr field;
};

// Declaration order matches eval/psqt.cpp's own switch (Pawn, Knight,
// Bishop, Rook, Queen, King), mg immediately followed by eg for each --
// purely a readability convention, not load-bearing (each entry is
// looked up by piece type below, never by position in this array).
inline constexpr std::array<PsqtFieldPair, 12> kPsqtFieldPairs = {{
    {&eval::PsqtWeights::pawn_mg},   {&eval::PsqtWeights::pawn_eg},
    {&eval::PsqtWeights::knight_mg}, {&eval::PsqtWeights::knight_eg},
    {&eval::PsqtWeights::bishop_mg}, {&eval::PsqtWeights::bishop_eg},
    {&eval::PsqtWeights::rook_mg},   {&eval::PsqtWeights::rook_eg},
    {&eval::PsqtWeights::queen_mg},  {&eval::PsqtWeights::queen_eg},
    {&eval::PsqtWeights::king_mg},   {&eval::PsqtWeights::king_eg},
}};

/// This piece type's (mg field, eg field) pair in a PsqtWeights —
/// mirrors eval/psqt.cpp's own psqt_value() switch exactly (same field
/// per piece type), factored out here so compute_psqt_gradient() below
/// has one switch, not one per position. Returns {nullptr, nullptr} for
/// PieceType::None (never actually reached — callers already skip
/// Piece::None board squares before calling this).
///
/// Return type is `detail::PsqtArrayMemberPtr` (tune.h), not the
/// pointer-to-member-array type written out inline here a second time —
/// confirmed via GitHub Actions CI (Windows Debug/Release, run
/// 94909780687) that writing it out inline, nested directly inside this
/// std::pair<...>, hits the exact same MSVC parser limitation
/// kPsqtFields' own introducing entry (docs/DECISIONS.md, 2026-09-15
/// (4)) already found and fixed the same way, in a different file. Two
/// independent occurrences of the identical MSVC failure mode, in two
/// different declarations, is why this fix reuses that file's own
/// existing named alias here rather than introducing a second, separate
/// one purely local to this file — one alias, defined once, is less
/// surface for a THIRD occurrence to slip past unnoticed.
[[nodiscard]] std::pair<detail::PsqtArrayMemberPtr, detail::PsqtArrayMemberPtr>
psqt_field_pair(board::PieceType type) noexcept {
    switch (type) {
        case board::PieceType::Pawn:
            return {&eval::PsqtWeights::pawn_mg, &eval::PsqtWeights::pawn_eg};
        case board::PieceType::Knight:
            return {&eval::PsqtWeights::knight_mg, &eval::PsqtWeights::knight_eg};
        case board::PieceType::Bishop:
            return {&eval::PsqtWeights::bishop_mg, &eval::PsqtWeights::bishop_eg};
        case board::PieceType::Rook:
            return {&eval::PsqtWeights::rook_mg, &eval::PsqtWeights::rook_eg};
        case board::PieceType::Queen:
            return {&eval::PsqtWeights::queen_mg, &eval::PsqtWeights::queen_eg};
        case board::PieceType::King:
            return {&eval::PsqtWeights::king_mg, &eval::PsqtWeights::king_eg};
        case board::PieceType::None:
        default:
            return {nullptr, nullptr};
    }
}

} // namespace

eval::PsqtWeights compute_psqt_gradient(const std::vector<SelfPlayPosition>& positions,
                                         const eval::MaterialWeights& material_weights,
                                         const eval::PsqtWeights& psqt_weights,
                                         double sigmoid_scale) noexcept {
    eval::PsqtWeights gradient{}; // every std::array<double, 64> value-initialized to 0.0
    if (positions.empty()) {
        return gradient;
    }

    for (const SelfPlayPosition& position : positions) {
        const board::Position pos = board::parse_fen(position.fen);
        const int white_relative =
            eval::evaluate(pos, nullptr, nullptr, &material_weights, &psqt_weights);
        const double predicted = sigmoid(static_cast<double>(white_relative) / sigmoid_scale);
        const double error = predicted - position.result;
        // d(this position's squared error)/d(white_relative): MSE's own
        // outer derivative (2*error) times sigmoid's derivative
        // (predicted*(1-predicted)) times the 1/sigmoid_scale chain-rule
        // factor evaluate()'s centipawn-to-sigmoid-argument scaling
        // introduces -- the same quantity a finite-difference probe of
        // compute_loss() would approximate numerically, in closed form.
        const double outer = 2.0 * error * predicted * (1.0 - predicted) / sigmoid_scale;

        const int phase = eval::compute_phase(pos);
        const double mg_weight = static_cast<double>(phase) / eval::kMaxPhase;
        const double eg_weight = static_cast<double>(eval::kMaxPhase - phase) / eval::kMaxPhase;

        for (board::Square sq = 0; sq < board::kNumSquares; ++sq) {
            const board::Piece piece = pos.piece_at(sq);
            if (piece == board::Piece::None) {
                continue;
            }
            const board::PieceType type = board::piece_type_of(piece);
            const board::Color color = board::color_of(piece);

            // sign: this piece's own contribution to evaluate()'s White-
            // relative score is ADDED for White, SUBTRACTED for Black
            // (eval.cpp's material_value()+psqt_value() loop) -- mirrored
            // here exactly, since that sign is exactly what flows through
            // to d(white_relative)/d(cell) too.
            const double sign = (color == board::Color::White) ? 1.0 : -1.0;

            // idx: the exact PsqtWeights cell psqt_value() itself would
            // have read for this piece/square -- mirror_vertical() (sq
            // ^ 56) for Black on every color-distinct piece type, `sq`
            // directly for Knight/Queen (no color distinction -- see
            // eval/psqt.cpp's own header comment) or for White.
            const bool color_distinct =
                type != board::PieceType::Knight && type != board::PieceType::Queen;
            const int idx = (color == board::Color::White || !color_distinct) ? sq : (sq ^ 56);

            const auto [mg_field, eg_field] = psqt_field_pair(type);
            if (mg_field == nullptr) {
                continue; // defensive only -- Piece::None already skipped above
            }
            (gradient.*mg_field)[idx] += outer * sign * mg_weight;
            (gradient.*eg_field)[idx] += outer * sign * eg_weight;
        }
    }

    const double n = static_cast<double>(positions.size());
    for (const PsqtFieldPair& pair : kPsqtFieldPairs) {
        for (double& value : gradient.*pair.field) {
            value /= n;
        }
    }
    return gradient;
}

PsqtTuneResult tune_psqt(const std::vector<SelfPlayPosition>& positions,
                          const eval::MaterialWeights& material_weights,
                          const eval::PsqtWeights& initial_psqt_weights, const TuneConfig& config) {
    PsqtTuneResult result;
    result.weights = initial_psqt_weights;
    result.initial_loss =
        compute_loss(positions, material_weights, config.sigmoid_scale, &result.weights) +
        l2_penalty(kPsqtParameters, result.weights, config.l2_lambda);
    result.history.push_back(TuneIteration{0, result.initial_loss});

    for (int iteration = 1; iteration <= config.iterations; ++iteration) {
        eval::PsqtWeights gradient =
            compute_psqt_gradient(positions, material_weights, result.weights, config.sigmoid_scale);

        // Step 5's L2 term applies here exactly as it does in tune():
        // added analytically (2*lambda*value), on top of the analytic
        // MSE gradient compute_psqt_gradient() already computed above --
        // no finite differences anywhere in this function at all.
        if (config.l2_lambda != 0.0) {
            for (const PsqtParameterRef& param : kPsqtParameters) {
                // kPsqtParameters has no anchored entries (its own doc
                // comment, tune.h) -- every cell gets the penalty term.
                const double value = param.get(result.weights);
                param.set(gradient, param.get(gradient) + 2.0 * config.l2_lambda * value);
            }
        }

        for (const PsqtParameterRef& param : kPsqtParameters) {
            param.set(result.weights,
                      param.get(result.weights) - config.learning_rate * param.get(gradient));
        }

        const double loss_after_step =
            compute_loss(positions, material_weights, config.sigmoid_scale, &result.weights) +
            l2_penalty(kPsqtParameters, result.weights, config.l2_lambda);
        result.history.push_back(TuneIteration{iteration, loss_after_step});
    }

    result.final_loss = result.history.back().loss;
    return result;
}

} // namespace nightwing::tuner
