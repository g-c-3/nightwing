// src/tuner/spsa_search.cpp
//
// See spsa_search.h for scope and design.

#include "tuner/spsa_search.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nightwing::tuner {
namespace {

/// Rounds `value` to the nearest integer and clamps it to [lo, hi].
[[nodiscard]] long long round_clamped(double value, double lo, double hi) {
    const double clamped = std::min(std::max(value, lo), hi);
    return std::llround(clamped);
}

} // namespace

const std::vector<SearchTunableInfo>& search_tunable_table() {
    // Mirrors search/tunables.h kSearchTunableSpecs; default_c is untuned.
    static const std::vector<SearchTunableInfo> table = {
        {"NullMoveReduction", 2, 1, 6, 1},
        {"NullMoveBigReduction", 3, 1, 8, 1},
        {"ProbCutMargin", 200, 20, 600, 40},
        {"IIRMinDepth", 4, 2, 12, 1},
        {"SingularMarginPerPly", 2, 1, 8, 1},
        {"ImprovingFutilityDelta", 60, 0, 200, 15},
        {"AspirationInitialDelta", 25, 5, 100, 6},
    };
    return table;
}

std::vector<SpsaParam> make_search_spsa_params(double c_scale, double start_scale) {
    std::vector<SpsaParam> out;
    for (const SearchTunableInfo& info : search_tunable_table()) {
        SpsaParam p;
        p.name = info.name;
        p.start = static_cast<double>(
            round_clamped(info.default_value * start_scale, info.min_value, info.max_value));
        p.c = std::max(1.0, info.default_c * c_scale);
        p.min = info.min_value;
        p.max = info.max_value;
        out.push_back(p);
    }
    return out;
}

UciEngineSpec make_search_engine_spec(const std::string& name, const std::string& command,
                                      const std::vector<SpsaParam>& params,
                                      const std::vector<double>& theta) {
    UciEngineSpec spec;
    spec.name = name;
    spec.command = command;
    spec.options.emplace_back("Threads", "1");
    spec.options.emplace_back("Hash", "16");
    for (std::size_t i = 0; i < params.size(); ++i) {
        const double value = (i < theta.size()) ? theta[i] : params[i].start;
        spec.options.emplace_back(
            params[i].name, std::to_string(round_clamped(value, params[i].min, params[i].max)));
        spec.required_options.push_back(params[i].name);
    }
    return spec;
}

UciMatchResult play_search_match(const std::string& command, const std::vector<SpsaParam>& params,
                                 const std::vector<double>& theta_a,
                                 const std::vector<double>& theta_b, std::uint64_t base_seed,
                                 const UciMatchConfig& match) {
    UciMatchConfig config = match;
    if (config.movetime_ms <= 0 && config.nodes <= 0) {
        config.nodes = kDefaultSearchMatchNodes;
    }
    UciMatchSession session(make_search_engine_spec("plus", command, params, theta_a),
                            make_search_engine_spec("minus", command, params, theta_b), config);
    if (session.start()) {
        for (int i = 0; i < config.num_games; ++i) {
            if (!session.play_game(i, base_seed + static_cast<std::uint64_t>(i))) {
                break;
            }
        }
    }
    UciMatchResult result = session.result();
    session.stop();
    return result;
}

SpsaResult run_spsa_search(const SpsaSearchConfig& config) {
    const std::vector<SpsaParam> params =
        make_search_spsa_params(config.c_scale, config.start_scale);

    const SpsaMatchFn match_fn = [&](const std::vector<double>& plus,
                                     const std::vector<double>& minus, std::uint64_t game_seed) {
        const UciMatchResult r =
            play_search_match(config.engine_command, params, plus, minus, game_seed, config.match);
        if (r.aborted) {
            throw std::runtime_error("SPSA search match aborted: " + r.error);
        }
        return r.tally.score_a();
    };
    return run_spsa(params, config.spsa, match_fn);
}

} // namespace nightwing::tuner
