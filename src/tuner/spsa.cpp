// src/tuner/spsa.cpp
//
// SPSA optimizer core. See spsa.h for the algorithm, gain schedules,
// attribution and scope.

#include "tuner/spsa.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace nightwing::tuner {

SpsaResult run_spsa(const std::vector<SpsaParam>& params, const SpsaConfig& config,
                    const SpsaMatchFn& match) {
    SpsaResult result;
    const std::size_t n = params.size();
    if (n == 0) {
        return result;
    }

    std::vector<double> theta(n);
    for (std::size_t i = 0; i < n; ++i) {
        theta[i] = std::clamp(params[i].start, params[i].min, params[i].max);
    }

    const double a_const = config.stability >= 0.0 ? config.stability : 0.1 * config.iterations;
    std::mt19937_64 rng(config.seed);
    std::vector<double> plus(n);
    std::vector<double> minus(n);
    std::vector<double> delta(n);

    for (int k = 0; k < config.iterations; ++k) {
        const double ck_scale = 1.0 / std::pow(static_cast<double>(k) + 1.0, config.gamma);
        const double rk = config.r0 / std::pow(a_const + static_cast<double>(k) + 1.0, config.alpha);

        for (std::size_t i = 0; i < n; ++i) {
            delta[i] = (rng() & 1ULL) != 0ULL ? 1.0 : -1.0;
            const double ck = params[i].c * ck_scale;
            plus[i] = std::clamp(theta[i] + ck * delta[i], params[i].min, params[i].max);
            minus[i] = std::clamp(theta[i] - ck * delta[i], params[i].min, params[i].max);
        }

        const double y_plus = std::clamp(match(plus, minus, config.seed + static_cast<std::uint64_t>(k)), 0.0, 1.0);
        result.plus_scores.push_back(y_plus);
        const double diff = 2.0 * y_plus - 1.0; // y+ - y-, with y- = 1 - y+

        for (std::size_t i = 0; i < n; ++i) {
            const double ck = params[i].c * ck_scale;
            theta[i] = std::clamp(theta[i] + rk * ck * diff * delta[i] * 0.5, params[i].min, params[i].max);
        }
        ++result.iterations_run;
    }

    result.theta = std::move(theta);
    return result;
}

} // namespace nightwing::tuner
