// src/tuner/tune_main.cpp
//
// Nightwing tune: reads a training-data file (tuner::write_training_data()'s
// <fen>;<result> format, tuner/selfplay.h) from stdin, runs
// tuner::tune() (tuner/tune.h) starting from eval::default_material_weights()
// -- or, in --psqt mode (added this session, the CLI follow-up to
// ROADMAP.md Tier 0 Steps 5-6's tune_psqt()/l2_lambda), tuner::tune_psqt()
// starting from eval::default_psqt_weights() -- and prints the resulting
// tuned weights and per-iteration loss history. This is the "gradient
// descent" half of ROADMAP.md Phase 5's Texel/SPSA tuner item, mirroring
// bench.cpp's/selfplay_main.cpp's own "small standalone tool, thin main()
// over a real library function" shape.
//
// Typical use (a GitHub Actions workflow_dispatch job, or run by hand --
// this codebase's own "GitHub Actions handles ALL building/running of
// anything compute-heavy" convention, same as selfplay_main.cpp's own
// header comment):
//
//   nightwing_selfplay 200 1 4 8 200 > training_data.txt
//   nightwing_tune < training_data.txt
//   nightwing_tune --psqt < training_data.txt
//
// MATERIAL MODE (default) -- every argument optional and positional, in
// this order (mirroring TuneConfig's own fields, selfplay_main.cpp's own
// "optional positional args with sensible defaults" convention):
//   nightwing_tune [iterations] [learning_rate] [finite_diff_epsilon]
//                  [sigmoid_scale] [l2_lambda]
// `l2_lambda` (TuneConfig, ROADMAP.md Tier 0 Step 5) had no CLI exposure
// at all before this session -- added here as this mode's new trailing
// 5th positional argument, defaulting to TuneConfig's own 0.0 (no
// regularization) exactly like every other field already does when
// omitted, so every pre-existing invocation of this tool keeps behaving
// identically.
//
// PSQT MODE (--psqt as the first argument) -- runs tuner::tune_psqt()
// (ROADMAP.md Tier 0 Step 6) instead, holding material fixed at
// eval::default_material_weights() for the whole run (tune_psqt()'s own
// doc comment, tune.h, on why material is held fixed rather than
// co-tuned in the same run). Same positional argument order and slot
// numbers as material mode, specifically so switching modes never
// requires renumbering the rest of the command line -- including
// `finite_diff_epsilon`, which this mode still PARSES (for slot-number
// compatibility) but has NO EFFECT in: tune_psqt() sources its gradient
// from compute_psqt_gradient()'s closed-form analytic derivation, not
// finite differences (tune.h's own header comment on why that shortcut
// is valid specifically for PSQT), so there is nothing in this mode for
// that argument to configure. `learning_rate`'s own DEFAULT differs
// between the two modes (20000.0 material, 100.0 PSQT) -- see this
// file's own comment at that default, below, for why.
//   nightwing_tune --psqt [iterations] [learning_rate] [finite_diff_epsilon]
//                  [sigmoid_scale] [l2_lambda]
//
// OUTPUT: material mode prints each of the 5 piece types' mg/eg pair, one
// per line to stdout (unchanged from before this session). PSQT mode
// prints each of PsqtWeights' 12 fields as an 8x8 grid of round()-ed
// ints, in the exact row/column layout psqt.cpp's own kXxxMgTable/
// kXxxEgTable literals use (index 0 = a1, top-left of the printed grid --
// see psqt.cpp's own header comment for the full square-indexing
// convention) -- ready to hand-copy directly into psqt.cpp's own
// constexpr tables, which is this tool's actual eventual purpose
// (ROADMAP.md's "Tuned weights committed" item). That item is still
// gated on two things neither this tool nor this session produces: a
// materially larger self-play training corpus than tuner/selfplay.h's
// own development-sized defaults, and a real nightwing_sprt-gated match
// confirming any resulting weights are genuinely stronger before they're
// transcribed anywhere (docs/DECISIONS.md, the Tier 0 Steps 5-6 entry,
// has the full "what's not done yet, by design" account). This tool
// makes running that eventual tuning job POSSIBLE for both terms; it
// does not itself constitute having already run it at production scale.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/psqt.h"
#include "tuner/selfplay.h"
#include "tuner/tune.h"

namespace {

/// Prints one PsqtWeights field (`values`) as an 8x8 grid of round()-ed
/// ints, matching psqt.cpp's own literal table formatting exactly (index
/// 0 = a1 at the top-left of the printed grid, index 56 = a8 at the
/// bottom-left -- see psqt.cpp's own header comment for the full
/// indexing convention) -- so the printed grid can be pasted directly
/// into a `constexpr int kXxxMgTable[64] = { ... };` literal with no
/// reordering.
void print_psqt_field(const char* name, const std::array<double, 64>& values) {
    std::fprintf(stderr, "%s:\n", name);
    for (int rank = 0; rank < 8; ++rank) {
        std::fprintf(stderr, "   ");
        for (int file = 0; file < 8; ++file) {
            const int idx = rank * 8 + file;
            const int rounded = static_cast<int>(std::lround(values[idx]));
            std::fprintf(stderr, "%5d,", rounded);
        }
        std::fprintf(stderr, "\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    // --psqt, if present, must be the very first argument -- every
    // positional argument after it (iterations, learning_rate, ...)
    // keeps the exact same slot it has in material mode, so switching
    // modes never requires renumbering the rest of the command line
    // (this file's own header comment).
    bool psqt_mode = false;
    int arg_offset = 1;
    if (argc > 1 && std::strcmp(argv[1], "--psqt") == 0) {
        psqt_mode = true;
        arg_offset = 2;
    }

    nightwing::tuner::TuneConfig config;
    if (psqt_mode) {
        // TuneConfig::learning_rate's own DEFAULT (20000.0) was
        // empirically chosen against MaterialWeights' specific gradient
        // scale (tune.h's own doc comment on that field) -- PSQT's
        // analytic gradient (tune.cpp's compute_psqt_gradient()) has a
        // materially different scale (tests/tune_tests.cpp's own
        // tune_psqt() test measured 100.0 to work well there, vs.
        // material's 20000.0), so --psqt mode overrides the DEFAULT
        // here, before any user-supplied learning_rate argument below
        // gets a chance to override it again in turn.
        config.learning_rate = 100.0;
    }

    if (argc > arg_offset) {
        config.iterations = std::atoi(argv[arg_offset]);
    }
    if (argc > arg_offset + 1) {
        config.learning_rate = std::strtod(argv[arg_offset + 1], nullptr);
    }
    if (argc > arg_offset + 2) {
        // Parsed in both modes (keeps every later positional argument's
        // own slot number identical between modes -- this file's own
        // header comment), but only actually consulted by material
        // mode's finite-difference gradient below; --psqt mode's
        // tune_psqt() has no use for it at all.
        config.finite_diff_epsilon = std::strtod(argv[arg_offset + 2], nullptr);
    }
    if (argc > arg_offset + 3) {
        config.sigmoid_scale = std::strtod(argv[arg_offset + 3], nullptr);
    }
    if (argc > arg_offset + 4) {
        config.l2_lambda = std::strtod(argv[arg_offset + 4], nullptr);
    }

    if (!nightwing::tuner::l2_update_is_stable(config.learning_rate, config.l2_lambda)) {
        std::fprintf(stderr,
                      "WARNING: learning_rate=%g and l2_lambda=%g together are past the L2 "
                      "term's own stability threshold (TuneConfig::l2_lambda's doc comment, "
                      "tune.h) -- every non-anchored parameter will diverge GEOMETRICALLY, "
                      "iteration over iteration, regardless of how well the MSE term's own "
                      "gradient behaves. Reduce l2_lambda (to well under %g here) or "
                      "learning_rate before trusting this run's output.\n",
                      config.learning_rate, config.l2_lambda,
                      1.0 / (2.0 * config.learning_rate));
    }

    const std::vector<nightwing::tuner::SelfPlayPosition> positions =
        nightwing::tuner::read_training_data(std::cin);

    std::fprintf(stderr,
                  "Nightwing tune: mode=%s, %zu training positions, iterations=%d, "
                  "learning_rate=%g, finite_diff_epsilon=%g, sigmoid_scale=%g, l2_lambda=%g\n",
                  psqt_mode ? "psqt" : "material", positions.size(), config.iterations,
                  config.learning_rate, config.finite_diff_epsilon, config.sigmoid_scale,
                  config.l2_lambda);
    if (!psqt_mode) {
        std::fprintf(
            stderr,
            "pawn_mg/pawn_eg are anchored (kMaterialParameters, tuner/tune.h) -- they will "
            "stay fixed at their starting value for this whole run.\n");
    } else {
        std::fprintf(stderr,
                      "Material held fixed at eval::default_material_weights() for this whole "
                      "run (tune_psqt() does not co-tune material -- tune.h's own doc comment). "
                      "No PsqtWeights field is anchored.\n");
    }

    if (positions.empty()) {
        std::fprintf(stderr,
                      "No training positions read from stdin -- nothing to tune. Pipe "
                      "nightwing_selfplay's output in, e.g.:\n"
                      "  nightwing_selfplay 200 1 4 8 200 | nightwing_tune\n");
        return 1;
    }

    if (psqt_mode) {
        const nightwing::eval::MaterialWeights material_weights =
            nightwing::eval::default_material_weights();
        const nightwing::eval::PsqtWeights initial_psqt_weights =
            nightwing::eval::default_psqt_weights();
        const nightwing::tuner::PsqtTuneResult result =
            nightwing::tuner::tune_psqt(positions, material_weights, initial_psqt_weights, config);

        std::fprintf(stderr, "Initial loss: %.6f\nFinal loss:   %.6f\n", result.initial_loss,
                      result.final_loss);
        std::fprintf(stderr, "Loss history (iteration, loss):\n");
        for (const nightwing::tuner::TuneIteration& step : result.history) {
            std::fprintf(stderr, "  %4d  %.6f\n", step.iteration, step.loss);
        }

        // Printed to stderr, like every other diagnostic above -- stdout
        // stays reserved for the actual tuned-values output, matching
        // material mode's own stdout-only convention below, so a caller
        // can redirect stdout alone to a file and get exactly the 12
        // grids, nothing else mixed in.
        print_psqt_field("pawn_mg", result.weights.pawn_mg);
        print_psqt_field("pawn_eg", result.weights.pawn_eg);
        print_psqt_field("knight_mg", result.weights.knight_mg);
        print_psqt_field("knight_eg", result.weights.knight_eg);
        print_psqt_field("bishop_mg", result.weights.bishop_mg);
        print_psqt_field("bishop_eg", result.weights.bishop_eg);
        print_psqt_field("rook_mg", result.weights.rook_mg);
        print_psqt_field("rook_eg", result.weights.rook_eg);
        print_psqt_field("queen_mg", result.weights.queen_mg);
        print_psqt_field("queen_eg", result.weights.queen_eg);
        print_psqt_field("king_mg", result.weights.king_mg);
        print_psqt_field("king_eg", result.weights.king_eg);
        return 0;
    }

    const nightwing::eval::MaterialWeights initial_weights =
        nightwing::eval::default_material_weights();
    const nightwing::tuner::TuneResult result =
        nightwing::tuner::tune(positions, initial_weights, config);

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
