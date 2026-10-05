// src/tuner/spsa_main.cpp
//
// nightwing_spsa: runs SPSA (spsa.h) over one eval table using fixed-depth
// self-play matches (spsa_eval.h), then optionally validates the tuned
// vector against the compiled-in defaults.
//
// Every argument is optional and positional (same minimal style as
// match_main.cpp):
//   nightwing_spsa [iterations] [games_per_iteration] [search_depth] [seed]
//                  [c] [r0] [validation_games] [start_scale] [target]
// start_scale multiplies every (non-anchored) default weight to form the
// starting vector (default 1 = the compiled-in defaults; a value far from 1
// is a deliberately wrong start, to check that SPSA recovers toward the
// defaults). When it is not 1 and validation_games > 0, the starting vector
// is also played against the defaults first, so the printed result shows how
// wrong the start was.
// target selects the table: 0 = eval mobility (default, 8 parameters),
// 1 = base material (8 non-anchored parameters; pawn anchored), 2 = base
// material with each piece's endgame value tied to its middlegame value
// (4 parameters: knight, bishop, rook, queen). Material is the sanity-test
// target because its effect on play is large and certain; target 2 exists
// because the endgame values received almost no signal under target 1.
// For targets 1 and 2, `c` is in material units (a value around 20 is sensible; the
// mobility default of 2 is far too small) and bounds are [0, 2000].
// Progress goes to stderr; stdout receives only `name=value` lines for the
// tuned weights followed by one validation line when validation_games > 0.
// Compute-heavy: run through GitHub Actions, never locally.

#include <cstdio>
#include <cstdlib>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "tuner/spsa_eval.h"
#include "tuner/tune.h"

namespace {

using nightwing::tuner::MatchConfig;
using nightwing::tuner::MatchResult;

void print_start_validation(double start_scale, const MatchResult& b) {
    std::printf("start_validation: scale=%.2f games=%d wins_start=%d wins_default=%d draws=%d score_start=%.4f "
                "elo_diff=%.1f\n",
                start_scale, b.games_played, b.wins_a, b.wins_b, b.draws, b.score_a(), b.elo_diff());
    std::fflush(stdout);
}

void print_validation(const MatchResult& r) {
    std::printf("validation: games=%d wins_tuned=%d wins_default=%d draws=%d score_tuned=%.4f elo_diff=%.1f\n",
                r.games_played, r.wins_a, r.wins_b, r.draws, r.score_a(), r.elo_diff());
}

void print_progress(const nightwing::tuner::SpsaResult& result) {
    for (std::size_t k = 0; k < result.plus_scores.size(); ++k) {
        std::fprintf(stderr, "iter %zu plus_score=%.3f\n", k + 1, result.plus_scores[k]);
    }
}

} // namespace

int main(int argc, char** argv) {
    using namespace nightwing;
    board::init_masks();
    board::init_magic_bitboards();
    board::init_zobrist_keys();

    tuner::SpsaConfig spsa;
    spsa.iterations = 50;
    MatchConfig match;
    match.num_games = 8;
    match.search_depth = 4;
    std::uint64_t seed = 1;
    int validation_games = 0;
    double start_scale = 1.0;
    int target = 0;
    double c = -1.0; // negative = use the target's own default

    if (argc > 1) spsa.iterations = std::atoi(argv[1]);
    if (argc > 2) match.num_games = std::atoi(argv[2]);
    if (argc > 3) match.search_depth = std::atoi(argv[3]);
    if (argc > 4) seed = static_cast<std::uint64_t>(std::strtoull(argv[4], nullptr, 10));
    if (argc > 5) c = std::atof(argv[5]);
    if (argc > 6) spsa.r0 = std::atof(argv[6]);
    if (argc > 7) validation_games = std::atoi(argv[7]);
    if (argc > 8) start_scale = std::atof(argv[8]);
    if (argc > 9) target = std::atoi(argv[9]);
    spsa.seed = seed;

    if (target < 0 || target > 2) {
        std::fprintf(stderr, "Unknown target %d (0 = mobility, 1 = material, 2 = material mg/eg tied)\n", target);
        return 1;
    }

    tuner::SpsaMobilityConfig mob;
    tuner::SpsaMaterialConfig mat;
    mob.spsa = spsa;
    mob.match = match;
    mob.start_scale = start_scale;
    mat.spsa = spsa;
    mat.match = match;
    mat.start_scale = start_scale;
    mat.tie_mg_eg = (target == 2);
    if (c >= 0.0) {
        mob.c = c;
        mat.c = c;
    }

    std::fprintf(stderr,
                 "Nightwing SPSA (%s): iterations=%d games/iter=%d depth=%d seed=%llu c=%.2f r0=%.2f "
                 "validation_games=%d start_scale=%.2f\n",
                 target == 2 ? "material-tied" : (target == 1 ? "material" : "mobility"), spsa.iterations, match.num_games, match.search_depth,
                 static_cast<unsigned long long>(seed), target >= 1 ? mat.c : mob.c, spsa.r0, validation_games,
                 start_scale);

    MatchConfig vm = match;
    vm.num_games = validation_games;

    if (target >= 1) {
        if (validation_games > 0 && start_scale != 1.0) {
            print_start_validation(start_scale,
                                   tuner::play_material_match(tuner::scaled_material_weights(start_scale),
                                                              eval::default_material_weights(),
                                                              seed + 2000003ULL, vm));
        }
        const tuner::SpsaResult result = tuner::run_spsa_material(mat);
        print_progress(result);
        const eval::MaterialWeights tuned =
            mat.tie_mg_eg ? tuner::apply_tied_material_theta(tuner::scaled_material_weights(start_scale), result.theta)
                          : tuner::apply_spsa_theta(tuner::kMaterialParameters,
                                                    tuner::scaled_material_weights(start_scale), result.theta);
        for (const auto& ref : tuner::kMaterialParameters) {
            std::printf("%s=%.1f\n", ref.name, ref.get(tuned));
        }
        if (validation_games > 0) {
            print_validation(tuner::play_material_match(tuned, eval::default_material_weights(),
                                                        seed + 1000003ULL, vm));
        }
        return 0;
    }

    if (validation_games > 0 && start_scale != 1.0) {
        print_start_validation(start_scale,
                               tuner::play_mobility_match(tuner::scaled_mobility_weights(start_scale),
                                                          eval::default_mobility_weights(), seed + 2000003ULL, vm));
    }
    const tuner::SpsaResult result = tuner::run_spsa_mobility(mob);
    print_progress(result);
    const eval::MobilityWeights tuned = tuner::apply_spsa_theta(
        tuner::kMobilityParameters, tuner::scaled_mobility_weights(start_scale), result.theta);
    for (const auto& ref : tuner::kMobilityParameters) {
        std::printf("%s=%.1f\n", ref.name, ref.get(tuned));
    }
    if (validation_games > 0) {
        print_validation(tuner::play_mobility_match(tuned, eval::default_mobility_weights(), seed + 1000003ULL, vm));
    }
    return 0;
}
