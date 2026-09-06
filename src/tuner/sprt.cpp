// src/tuner/sprt.cpp
//
// See sprt.h.

#include "tuner/sprt.h"

#include <cmath>

namespace nightwing::tuner {

double elo_to_score(double elo) noexcept { return 1.0 / (1.0 + std::pow(10.0, -elo / 400.0)); }

SprtState compute_sprt(int wins, int draws, int losses, const SprtConfig& config) noexcept {
    SprtState state;
    state.lower_bound = std::log(config.beta / (1.0 - config.alpha));
    state.upper_bound = std::log((1.0 - config.beta) / config.alpha);

    const int n = wins + draws + losses;
    if (n <= 0) {
        return state; // No games yet -- Continue, llr = 0.
    }

    const double dn = static_cast<double>(n);
    const double score = (static_cast<double>(wins) + 0.5 * static_cast<double>(draws)) / dn;

    // Sample variance of a single game's score (0, 0.5, or 1) about the
    // observed mean -- the GSPRT formulation this file's own header
    // comment (sprt.h) attributes.
    const double var = (static_cast<double>(wins) * (1.0 - score) * (1.0 - score) +
                         static_cast<double>(draws) * (0.5 - score) * (0.5 - score) +
                         static_cast<double>(losses) * (0.0 - score) * (0.0 - score)) /
                        dn;

    constexpr double kMinVariance = 1e-9;
    if (var < kMinVariance) {
        // Every game so far had an identical outcome (most commonly:
        // every game so far drawn) -- the LLR formula's denominator
        // would blow up here. This is honestly "not enough information
        // yet", not a decision either way -- return Continue rather
        // than a spurious accept/reject.
        return state;
    }

    const double p0 = elo_to_score(config.elo0);
    const double p1 = elo_to_score(config.elo1);

    state.llr = dn * (score - (p0 + p1) / 2.0) * (p1 - p0) / var;

    if (state.llr <= state.lower_bound) {
        state.status = SprtStatus::AcceptH0;
    } else if (state.llr >= state.upper_bound) {
        state.status = SprtStatus::AcceptH1;
    }

    return state;
}

} // namespace nightwing::tuner
