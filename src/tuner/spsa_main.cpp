// src/tuner/spsa_main.cpp
//
// nightwing_spsa: runs SPSA (spsa.h) over the eval mobility weights using
// fixed-depth self-play matches (spsa_eval.h), then optionally validates
// the tuned vector against the compiled-in defaults.
//
// Every argument is optional and positional (same minimal style as
// match_main.cpp):
//   nightwing_spsa [iterations] [games_per_iteration] [search_depth] [seed]
//                  [c] [r0] [validation_games]
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

int main(int argc, char** argv) {
    using namespace nightwing;
    board::init_masks();
    board::init_magic_bitboards();
    board::init_zobrist_keys();

    tuner::SpsaMobilityConfig cfg;
    cfg.spsa.iterations = 50;
    cfg.match.num_games = 8;
    cfg.match.search_depth = 4;
    std::uint64_t seed = 1;
    int validation_games = 0;

    if (argc > 1) cfg.spsa.iterations = std::atoi(argv[1]);
    if (argc > 2) cfg.match.num_games = std::atoi(argv[2]);
    if (argc > 3) cfg.match.search_depth = std::atoi(argv[3]);
    if (argc > 4) seed = static_cast<std::uint64_t>(std::strtoull(argv[4], nullptr, 10));
    if (argc > 5) cfg.c = std::atof(argv[5]);
    if (argc > 6) cfg.spsa.r0 = std::atof(argv[6]);
    if (argc > 7) validation_games = std::atoi(argv[7]);
    cfg.spsa.seed = seed;

    std::fprintf(stderr,
                 "Nightwing SPSA (mobility): iterations=%d games/iter=%d depth=%d seed=%llu c=%.2f r0=%.2f "
                 "validation_games=%d\n",
                 cfg.spsa.iterations, cfg.match.num_games, cfg.match.search_depth,
                 static_cast<unsigned long long>(seed), cfg.c, cfg.spsa.r0, validation_games);

    const tuner::SpsaResult result = tuner::run_spsa_mobility(cfg);
    for (std::size_t k = 0; k < result.plus_scores.size(); ++k) {
        std::fprintf(stderr, "iter %zu plus_score=%.3f\n", k + 1, result.plus_scores[k]);
    }

    const eval::MobilityWeights tuned =
        tuner::apply_spsa_theta(tuner::kMobilityParameters, eval::default_mobility_weights(), result.theta);
    for (const auto& ref : tuner::kMobilityParameters) {
        std::printf("%s=%.1f\n", ref.name, ref.get(tuned));
    }

    if (validation_games > 0) {
        tuner::MatchConfig vm = cfg.match;
        vm.num_games = validation_games;
        const tuner::MatchResult r =
            tuner::play_mobility_match(tuned, eval::default_mobility_weights(), seed + 1000003ULL, vm);
        std::printf("validation: games=%d wins_tuned=%d wins_default=%d draws=%d score_tuned=%.4f elo_diff=%.1f\n",
                    r.games_played, r.wins_a, r.wins_b, r.draws, r.score_a(), r.elo_diff());
    }
    return 0;
}
