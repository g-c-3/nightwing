// src/tuner/tune_main.cpp
//
// Nightwing tune: reads a training-data file (tuner::write_training_data()'s
// <fen>;<result> format, tuner/selfplay.h) from stdin, runs
// tuner::tune() (tuner/tune.h) starting from eval::default_material_weights(),
// and prints the resulting tuned weights and per-iteration loss history —
// the "gradient descent" half of ROADMAP.md Phase 5's Texel/SPSA tuner
// item, mirroring bench.cpp's/selfplay_main.cpp's own "small standalone
// tool, thin main() over a real library function" shape.
//
// Typical use (a GitHub Actions workflow_dispatch job, or run by hand —
// this codebase's own "GitHub Actions handles ALL building/running of
// anything compute-heavy" convention, same as selfplay_main.cpp's own
// header comment):
//
//   nightwing_selfplay 200 1 4 8 200 > training_data.txt
//   nightwing_tune < training_data.txt
//
// Every argument is optional and positional, in this order (mirroring
// TuneConfig's own fields, selfplay_main.cpp's own "optional positional
// args with sensible defaults" convention):
//   nightwing_tune [iterations] [learning_rate] [finite_diff_epsilon]
//                  [sigmoid_scale]

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/psqt.h"
#include "tuner/selfplay.h"
#include "tuner/tune.h"

int main(int argc, char** argv) {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    nightwing::tuner::TuneConfig config;

    // Same deliberately minimal, no-dependency positional parsing as
    // selfplay_main.cpp's own — see that file's header comment for why
    // this is an acceptable choice for a hand/CI-run tool rather than a
    // UCI-protocol-facing surface.
    if (argc > 1) {
        config.iterations = std::atoi(argv[1]);
    }
    if (argc > 2) {
        config.learning_rate = std::strtod(argv[2], nullptr);
    }
    if (argc > 3) {
        config.finite_diff_epsilon = std::strtod(argv[3], nullptr);
    }
    if (argc > 4) {
        config.sigmoid_scale = std::strtod(argv[4], nullptr);
    }

    const std::vector<nightwing::tuner::SelfPlayPosition> positions =
        nightwing::tuner::read_training_data(std::cin);

    std::fprintf(stderr,
                  "Nightwing tune: %zu training positions, iterations=%d, learning_rate=%g, "
                  "finite_diff_epsilon=%g, sigmoid_scale=%g\n",
                  positions.size(), config.iterations, config.learning_rate,
                  config.finite_diff_epsilon, config.sigmoid_scale);

    if (positions.empty()) {
        std::fprintf(stderr,
                      "No training positions read from stdin -- nothing to tune. Pipe "
                      "nightwing_selfplay's output in, e.g.:\n"
                      "  nightwing_selfplay 200 1 4 8 200 | nightwing_tune\n");
        return 1;
    }

    const nightwing::eval::MaterialWeights initial_weights =
        nightwing::eval::default_material_weights();
    const nightwing::tuner::TuneResult result = nightwing::tuner::tune(positions, initial_weights, config);

    std::fprintf(stderr, "Initial loss: %.6f\nFinal loss:   %.6f\n", result.initial_loss,
                  result.final_loss);
    std::fprintf(stderr, "Loss history (iteration, loss):\n");
    for (const nightwing::tuner::TuneIteration& step : result.history) {
        std::fprintf(stderr, "  %4d  %.6f\n", step.iteration, step.loss);
    }

    std::cout << "pawn_mg=" << result.weights.pawn_mg << " pawn_eg=" << result.weights.pawn_eg
               << "\n";
    std::cout << "knight_mg=" << result.weights.knight_mg
               << " knight_eg=" << result.weights.knight_eg << "\n";
    std::cout << "bishop_mg=" << result.weights.bishop_mg
               << " bishop_eg=" << result.weights.bishop_eg << "\n";
    std::cout << "rook_mg=" << result.weights.rook_mg << " rook_eg=" << result.weights.rook_eg
               << "\n";
    std::cout << "queen_mg=" << result.weights.queen_mg << " queen_eg=" << result.weights.queen_eg
               << "\n";

    return 0;
}
