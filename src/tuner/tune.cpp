// src/tuner/tune.cpp
//
// See tune.h.

#include "tuner/tune.h"

#include <cmath>

#include "board/fen.h"
#include "eval/eval.h"

namespace nightwing::tuner {

double sigmoid(double x) noexcept { return 1.0 / (1.0 + std::exp(-x)); }

double compute_loss(const std::vector<SelfPlayPosition>& positions,
                     const eval::MaterialWeights& weights, double sigmoid_scale) noexcept {
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
        // never consults eval_cache when a material_weights override is
        // in play.
        const int white_relative = eval::evaluate(pos, nullptr, nullptr, &weights);
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
    result.initial_loss = compute_loss(positions, result.weights, config.sigmoid_scale);
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
            const auto member = kMaterialParameters[i].member;
            const double original = result.weights.*member;

            result.weights.*member = original + config.finite_diff_epsilon;
            const double loss_plus = compute_loss(positions, result.weights, config.sigmoid_scale);

            result.weights.*member = original - config.finite_diff_epsilon;
            const double loss_minus =
                compute_loss(positions, result.weights, config.sigmoid_scale);

            result.weights.*member = original; // restore before moving to the next parameter
            gradient[i] = (loss_plus - loss_minus) / (2.0 * config.finite_diff_epsilon);
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
            const auto member = kMaterialParameters[i].member;
            result.weights.*member -= config.learning_rate * gradient[i];
        }

        const double loss_after_step = compute_loss(positions, result.weights, config.sigmoid_scale);
        result.history.push_back(TuneIteration{iteration, loss_after_step});
    }

    result.final_loss = result.history.back().loss;
    return result;
}

} // namespace nightwing::tuner
