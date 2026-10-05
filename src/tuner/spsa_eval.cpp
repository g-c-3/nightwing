// src/tuner/spsa_eval.cpp
//
// See spsa_eval.h for scope and design.

#include "tuner/spsa_eval.h"

#include "eval/eval.h"
#include "eval/psqt.h"
#include "tuner/tune.h"

namespace nightwing::tuner {

MatchResult play_mobility_match(const eval::MobilityWeights& a, const eval::MobilityWeights& b,
                                std::uint64_t base_seed, const MatchConfig& match) {
    eval::EvalWeightsOverride override_a;
    override_a.mobility = &a;
    eval::EvalWeightsOverride override_b;
    override_b.mobility = &b;

    MatchConfig config = match;
    config.eval_weights_a = &override_a;
    config.eval_weights_b = &override_b;

    const eval::MaterialWeights material = eval::default_material_weights();
    return play_match(material, material, base_seed, config);
}

eval::MobilityWeights scaled_mobility_weights(double scale) {
    eval::MobilityWeights w = eval::default_mobility_weights();
    for (const auto& ref : kMobilityParameters) {
        ref.set(w, ref.get(w) * scale);
    }
    return w;
}

SpsaResult run_spsa_mobility(const SpsaMobilityConfig& config) {
    const eval::MobilityWeights base = scaled_mobility_weights(config.start_scale);
    const std::vector<SpsaParam> params =
        make_spsa_params(kMobilityParameters, base, config.c, config.min_value, config.max_value);

    const SpsaMatchFn match_fn = [&](const std::vector<double>& plus, const std::vector<double>& minus,
                                     std::uint64_t game_seed) {
        const eval::MobilityWeights wp = apply_spsa_theta(kMobilityParameters, base, plus);
        const eval::MobilityWeights wm = apply_spsa_theta(kMobilityParameters, base, minus);
        return play_mobility_match(wp, wm, game_seed, config.match).score_a();
    };

    return run_spsa(params, config.spsa, match_fn);
}

MatchResult play_material_match(const eval::MaterialWeights& a, const eval::MaterialWeights& b,
                                std::uint64_t base_seed, const MatchConfig& match) {
    MatchConfig config = match;
    config.eval_weights_a = nullptr;
    config.eval_weights_b = nullptr;
    return play_match(a, b, base_seed, config);
}

eval::MaterialWeights scaled_material_weights(double scale) {
    eval::MaterialWeights w = eval::default_material_weights();
    for (const auto& ref : kMaterialParameters) {
        if (ref.anchored) {
            continue;
        }
        ref.set(w, ref.get(w) * scale);
    }
    return w;
}

eval::MaterialWeights apply_tied_material_theta(eval::MaterialWeights base, const std::vector<double>& theta) {
    if (theta.size() > 0) { base.knight_mg = base.knight_eg = theta[0]; }
    if (theta.size() > 1) { base.bishop_mg = base.bishop_eg = theta[1]; }
    if (theta.size() > 2) { base.rook_mg = base.rook_eg = theta[2]; }
    if (theta.size() > 3) { base.queen_mg = base.queen_eg = theta[3]; }
    return base;
}

SpsaResult run_spsa_material(const SpsaMaterialConfig& config) {
    const eval::MaterialWeights base = scaled_material_weights(config.start_scale);

    if (config.tie_mg_eg) {
        const std::vector<SpsaParam> tied = {
            {"knight", base.knight_mg, config.c, config.min_value, config.max_value},
            {"bishop", base.bishop_mg, config.c, config.min_value, config.max_value},
            {"rook", base.rook_mg, config.c, config.min_value, config.max_value},
            {"queen", base.queen_mg, config.c, config.min_value, config.max_value},
        };
        const SpsaMatchFn tied_fn = [&](const std::vector<double>& plus, const std::vector<double>& minus,
                                        std::uint64_t game_seed) {
            return play_material_match(apply_tied_material_theta(base, plus),
                                       apply_tied_material_theta(base, minus), game_seed, config.match)
                .score_a();
        };
        return run_spsa(tied, config.spsa, tied_fn);
    }
    const std::vector<SpsaParam> params =
        make_spsa_params(kMaterialParameters, base, config.c, config.min_value, config.max_value);

    const SpsaMatchFn match_fn = [&](const std::vector<double>& plus, const std::vector<double>& minus,
                                     std::uint64_t game_seed) {
        const eval::MaterialWeights wp = apply_spsa_theta(kMaterialParameters, base, plus);
        const eval::MaterialWeights wm = apply_spsa_theta(kMaterialParameters, base, minus);
        return play_material_match(wp, wm, game_seed, config.match).score_a();
    };

    return run_spsa(params, config.spsa, match_fn);
}

} // namespace nightwing::tuner
