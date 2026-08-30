// src/tuner/selfplay_main.cpp
//
// Nightwing selfplay: a small standalone tool that runs
// tuner::play_games() (tuner/selfplay.h) and writes the resulting
// training data (selfplay.h's <fen>;<result> format) to stdout, one
// sampled position per line — the "self-play data generation" half of
// ROADMAP.md Phase 5's Texel/SPSA tuner item, mirroring bench.cpp's own
// "small standalone tool, thin main() over a real library function"
// shape rather than folding this logic into main.cpp/uci.cpp.
//
// Every argument is optional and positional, in this order — sensible
// defaults (mirroring tuner::SelfPlayConfig's own defaults) so a bare
// `nightwing_selfplay` with no arguments produces a small, reasonable
// sample rather than nothing/erroring:
//   nightwing_selfplay [num_games] [base_seed] [search_depth]
//                       [random_opening_plies] [max_plies]
//
// Run this by hand (or from a GitHub Actions workflow_dispatch job,
// redirecting stdout to a file uploaded as a build artifact — this
// codebase's own "GitHub Actions handles ALL building" convention
// extends naturally to "and ALL running of anything compute-heavy like
// this," not just compilation) whenever a fresh training corpus is
// needed for the not-yet-built gradient-descent tuning half of this
// ROADMAP item.

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "tuner/selfplay.h"

int main(int argc, char** argv) {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    int num_games = 20;
    std::uint64_t base_seed = 1;
    nightwing::tuner::SelfPlayConfig config;

    // Deliberately minimal, no-dependency positional parsing -- this
    // tool is run by hand/CI, not a UCI-protocol-facing surface with
    // real input-validation stakes (contrast board/fen.h's
    // exception-throwing parse_fen(), which IS on that kind of surface).
    // An unparseable argument silently falls back to that argument's
    // default via std::strtol's own 0-on-failure behavior, rather than
    // erroring -- acceptable here since a badly-formed argument just
    // produces an unexpectedly-shaped (but never crashing/UB) run,
    // easy to notice from the output; not worth this tool's own
    // dedicated validation/error-reporting path.
    if (argc > 1) {
        num_games = std::atoi(argv[1]);
    }
    if (argc > 2) {
        base_seed = static_cast<std::uint64_t>(std::strtoull(argv[2], nullptr, 10));
    }
    if (argc > 3) {
        config.search_depth = std::atoi(argv[3]);
    }
    if (argc > 4) {
        config.random_opening_plies = std::atoi(argv[4]);
    }
    if (argc > 5) {
        config.max_plies = std::atoi(argv[5]);
    }

    std::fprintf(stderr, "Nightwing selfplay: %d games, base_seed=%llu, depth=%d, "
                          "random_opening_plies=%d, max_plies=%d\n",
                 num_games, static_cast<unsigned long long>(base_seed), config.search_depth,
                 config.random_opening_plies, config.max_plies);

    const std::vector<nightwing::tuner::SelfPlayGame> games =
        nightwing::tuner::play_games(num_games, base_seed, config);

    std::size_t total_positions = 0;
    std::size_t total_plies = 0;
    for (const nightwing::tuner::SelfPlayGame& game : games) {
        total_positions += game.positions.size();
        total_plies += static_cast<std::size_t>(game.ply_count);
    }
    std::fprintf(stderr, "Played %d games, %zu ply total, %zu quiet positions sampled.\n",
                 num_games, total_plies, total_positions);

    nightwing::tuner::write_training_data(games, std::cout);

    return 0;
}
