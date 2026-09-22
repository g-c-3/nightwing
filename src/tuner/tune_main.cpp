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
//   nightwing_tune --mobility < training_data.txt
//   nightwing_tune --space < training_data.txt
//   nightwing_tune --threats < training_data.txt
//   nightwing_tune --king-safety < training_data.txt
//   nightwing_tune --pawns < training_data.txt
//
// The five new "--term" modes (ROADMAP.md's "Generalize tune() to the 5
// remaining 'beyond PSQT' tables" item — docs/DECISIONS.md, 2026-09-21
// (2), corrects the earlier "materially larger self-play corpus"
// framing) each run tuner::tune_mobility()/tune_space()/tune_threats()/
// tune_king_safety()/tune_pawns() (tuner/tune.h) starting from that
// term's own eval::default_XXX_weights(), holding material fixed at
// eval::default_material_weights() — same "tune one term, hold the
// others fixed" convention --psqt already established. Same positional
// argument slots as material/--psqt mode (this file's own comment
// below has the shared parsing rationale); output is a flat
// name=value list (this mode's own print_term_weights() below), not
// --psqt's 8x8-grid format — none of these five tables has PSQT's
// per-square dimension, so kMobilityParameters/kSpaceParameters/
// kThreatsParameters/kKingSafetyParameters/kPawnsParameters' own
// `name` fields (tune.h) are already exactly the right output labels.
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
// between the two modes (20000.0 material, 200000.0 PSQT) -- see this
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

/// Shared output for every "--term" mode below (mobility/space/threats/
/// king-safety/pawns) — one `name=value` line per entry in `params`
/// (kMobilityParameters/etc., tuner/tune.h), read out of `weights` via
/// each entry's own get(). Unlike print_psqt_field() above, none of
/// these five tables has a per-square dimension to lay out as a grid,
/// so `params[i].name` is already exactly the right label — this is
/// deliberately generic over `Weights` (a template, not five copy-
/// pasted printers) the same way tuner::tune_term() (tune.cpp) is
/// generic over the tuning loop itself. Printed to stdout, matching
/// material mode's own stdout-only convention (see this file's header
/// comment) so a caller can redirect stdout alone to a file and get
/// exactly the tuned values, nothing else mixed in.
template <typename Weights, std::size_t N>
void print_term_weights(const std::array<nightwing::tuner::ParameterRef<Weights>, N>& params,
                         const Weights& weights) {
    for (const nightwing::tuner::ParameterRef<Weights>& param : params) {
        std::cout << param.name << "=" << param.get(weights) << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    // --psqt/--mobility/--space/--threats/--king-safety/--pawns, if
    // present, must be the very first argument -- every positional
    // argument after it (iterations, learning_rate, ...) keeps the
    // exact same slot it has in material mode, so switching modes
    // never requires renumbering the rest of the command line (this
    // file's own header comment).
    enum class Mode { Material, Psqt, Mobility, Space, Threats, KingSafety, Pawns };
    Mode mode = Mode::Material;
    int arg_offset = 1;
    if (argc > 1 && std::strcmp(argv[1], "--psqt") == 0) {
        mode = Mode::Psqt;
        arg_offset = 2;
    } else if (argc > 1 && std::strcmp(argv[1], "--mobility") == 0) {
        mode = Mode::Mobility;
        arg_offset = 2;
    } else if (argc > 1 && std::strcmp(argv[1], "--space") == 0) {
        mode = Mode::Space;
        arg_offset = 2;
    } else if (argc > 1 && std::strcmp(argv[1], "--threats") == 0) {
        mode = Mode::Threats;
        arg_offset = 2;
    } else if (argc > 1 && std::strcmp(argv[1], "--king-safety") == 0) {
        mode = Mode::KingSafety;
        arg_offset = 2;
    } else if (argc > 1 && std::strcmp(argv[1], "--pawns") == 0) {
        mode = Mode::Pawns;
        arg_offset = 2;
    }
    const bool psqt_mode = (mode == Mode::Psqt);

    nightwing::tuner::TuneConfig config;
    if (psqt_mode) {
        // TuneConfig::learning_rate's own DEFAULT (20000.0) was
        // empirically chosen against MaterialWeights' specific gradient
        // scale (tune.h's own doc comment on that field) -- PSQT's
        // analytic gradient (tune.cpp's compute_psqt_gradient()) has a
        // materially different scale, so --psqt mode overrides the
        // DEFAULT here, before any user-supplied learning_rate argument
        // below gets a chance to override it again in turn.
        //
        // 200000.0, NOT 100.0 -- ROADMAP.md's "Fix tune_psqt()'s zero-
        // movement bug" (Session 122/124): 100.0 was calibrated ONLY
        // against tests/tune_tests.cpp's own narrow, deliberately-
        // engineered single-cell scenario (one FEN, repeated 8x, one
        // knight, a maximally strong and uncontested training signal) --
        // never independently re-verified against real, diverse self-
        // play data the way this comment's own discipline (see the five
        // "--term" modes' own comment below) already calls for. Measured
        // directly this session (a real 8104-position self-play corpus,
        // nightwing_selfplay 200 1 4 8 200, and a small standalone
        // harness printing compute_psqt_gradient()'s own raw output):
        // real per-cell analytic gradient magnitude on genuine, varied
        // game data averages ~1e-5, roughly three orders of magnitude
        // smaller than the toy test's own single-direction, uncontested
        // signal -- because a real PSQT cell's gradient AVERAGES over
        // many different games/contexts that mostly pull against each
        // other, while the toy test's 8 identical, unanimous positions
        // never do. At the OLD 100.0 rate, that real-data gradient
        // produces a per-iteration step of order 1e-3 -- so small that
        // no cell's underlying double value ever crosses psqt_value()'s
        // own round_to_int() boundary within a normal-length run (even
        // 200 iterations only accumulates ~0.2 if every step happened to
        // agree in sign, well under the 0.5 needed) -- which is exactly
        // why a real production run (Session 122, 208,360 positions, 200
        // iterations) came back with loss EXACTLY flat to six decimal
        // places at every single iteration and every "tuned" table
        // byte-for-byte identical to its own compiled-in default: not
        // because the analytic gradient was wrong (independently cross-
        // checked against a hand-rolled finite-difference probe,
        // tests/tune_tests.cpp's own "agrees with a hand-rolled finite-
        // difference probe" test, which still passes unchanged), but
        // because a learning rate calibrated for a signal ~1000x
        // stronger than real data's own was never going to move a
        // rounded integer table at all. 200000.0 was chosen the same
        // "measure, don't guess" way -- tried directly, on the same real
        // corpus, at several candidate values: loss decreases smoothly
        // and monotonically from 100 up through at least 1,000,000
        // (200 iterations: 0.040473 -> 0.025713 at 1,000,000, a real,
        // substantial reduction, not noise), with the first signs of
        // instability (loss ticking back UP late in a run rather than
        // still falling) only appearing around 2,000,000. 200000.0 sits
        // an order of magnitude below that observed instability
        // threshold while still producing a real, meaningful loss
        // reduction (0.040473 -> 0.029312 over 200 iterations on the
        // same corpus) -- see docs/DECISIONS.md, this entry's own dated
        // account, for the full measured table across candidate values
        // and a spot-check of the resulting king_mg/king_eg tables
        // (genuinely moved, and sane: the five most-played back ranks
        // converge close to the existing compiled-in table, exactly as
        // expected from real games broadly validating an already-
        // reasonable default, while the less-visited back rank shows
        // more real variation -- the shape a healthy tuner run should
        // produce at this corpus size, not garbage).
        config.learning_rate = 200000.0;
    }
    // The five --mobility/--space/--threats/--king-safety/--pawns modes
    // deliberately do NOT override learning_rate the way --psqt does
    // above: unlike tune_psqt()'s analytic gradient (a different
    // algorithm with its own, separately-measured scale), tune_mobility()/
    // etc. all use the exact same finite-difference probe (same
    // finite_diff_epsilon default) tune()'s own material mode does, so
    // TuneConfig's own default learning_rate (20000.0) is the same
    // starting point that's already known to work for a finite-
    // difference gradient at this general centipawn scale -- not a
    // guess, but also not independently re-measured against these five
    // terms' own real production data yet (this session's own scope:
    // make the run possible and correct, not claim its hyperparameters
    // are already tuned for a real corpus -- same honesty convention
    // --psqt's own introducing session already established, tune.h's
    // TuneConfig::iterations/learning_rate doc comments).

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

    const char* mode_name = "material";
    switch (mode) {
        case Mode::Material: mode_name = "material"; break;
        case Mode::Psqt: mode_name = "psqt"; break;
        case Mode::Mobility: mode_name = "mobility"; break;
        case Mode::Space: mode_name = "space"; break;
        case Mode::Threats: mode_name = "threats"; break;
        case Mode::KingSafety: mode_name = "king-safety"; break;
        case Mode::Pawns: mode_name = "pawns"; break;
    }

    const std::vector<nightwing::tuner::SelfPlayPosition> positions =
        nightwing::tuner::read_training_data(std::cin);

    std::fprintf(stderr,
                  "Nightwing tune: mode=%s, %zu training positions, iterations=%d, "
                  "learning_rate=%g, finite_diff_epsilon=%g, sigmoid_scale=%g, l2_lambda=%g\n",
                  mode_name, positions.size(), config.iterations, config.learning_rate,
                  config.finite_diff_epsilon, config.sigmoid_scale, config.l2_lambda);
    if (mode == Mode::Material) {
        std::fprintf(
            stderr,
            "pawn_mg/pawn_eg are anchored (kMaterialParameters, tuner/tune.h) -- they will "
            "stay fixed at their starting value for this whole run.\n");
    } else if (mode == Mode::Psqt) {
        std::fprintf(stderr,
                      "Material held fixed at eval::default_material_weights() for this whole "
                      "run (tune_psqt() does not co-tune material -- tune.h's own doc comment). "
                      "No PsqtWeights field is anchored.\n");
    } else {
        // The five term modes (mobility/space/threats/king-safety/pawns)
        // all share this same convention -- see tune.h's own
        // "Generalize tune()" section header comment for why none of
        // their own kXxxParameters tables has an anchored entry today.
        std::fprintf(stderr,
                      "Material held fixed at eval::default_material_weights() for this whole "
                      "run (tune_%s() does not co-tune material, same convention as tune_psqt() "
                      "-- tune.h's own doc comment). No %s field is anchored.\n",
                      mode_name, mode_name);
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

    // The five term modes below all follow the identical shape: hold
    // material fixed at its own compiled-in defaults, tune this one
    // term's own Weights struct starting from ITS compiled-in defaults,
    // print the loss history to stderr, print `name=value` per tunable
    // field (print_term_weights() above) to stdout. Each is a thin
    // wrapper around its own tuner::tune_XXX() (tune.h) -- the mode
    // dispatch here is the only place this file's own logic differs
    // between the five.
    if (mode == Mode::Mobility) {
        const nightwing::eval::MaterialWeights material_weights =
            nightwing::eval::default_material_weights();
        const nightwing::tuner::TermTuneResult<nightwing::eval::MobilityWeights> result =
            nightwing::tuner::tune_mobility(positions, material_weights,
                                             nightwing::eval::default_mobility_weights(), config);
        std::fprintf(stderr, "Initial loss: %.6f\nFinal loss:   %.6f\n", result.initial_loss,
                      result.final_loss);
        std::fprintf(stderr, "Loss history (iteration, loss):\n");
        for (const nightwing::tuner::TuneIteration& step : result.history) {
            std::fprintf(stderr, "  %4d  %.6f\n", step.iteration, step.loss);
        }
        print_term_weights(nightwing::tuner::kMobilityParameters, result.weights);
        return 0;
    }
    if (mode == Mode::Space) {
        const nightwing::eval::MaterialWeights material_weights =
            nightwing::eval::default_material_weights();
        const nightwing::tuner::TermTuneResult<nightwing::eval::SpaceWeights> result =
            nightwing::tuner::tune_space(positions, material_weights,
                                          nightwing::eval::default_space_weights(), config);
        std::fprintf(stderr, "Initial loss: %.6f\nFinal loss:   %.6f\n", result.initial_loss,
                      result.final_loss);
        std::fprintf(stderr, "Loss history (iteration, loss):\n");
        for (const nightwing::tuner::TuneIteration& step : result.history) {
            std::fprintf(stderr, "  %4d  %.6f\n", step.iteration, step.loss);
        }
        print_term_weights(nightwing::tuner::kSpaceParameters, result.weights);
        return 0;
    }
    if (mode == Mode::Threats) {
        const nightwing::eval::MaterialWeights material_weights =
            nightwing::eval::default_material_weights();
        const nightwing::tuner::TermTuneResult<nightwing::eval::ThreatsWeights> result =
            nightwing::tuner::tune_threats(positions, material_weights,
                                            nightwing::eval::default_threats_weights(), config);
        std::fprintf(stderr, "Initial loss: %.6f\nFinal loss:   %.6f\n", result.initial_loss,
                      result.final_loss);
        std::fprintf(stderr, "Loss history (iteration, loss):\n");
        for (const nightwing::tuner::TuneIteration& step : result.history) {
            std::fprintf(stderr, "  %4d  %.6f\n", step.iteration, step.loss);
        }
        print_term_weights(nightwing::tuner::kThreatsParameters, result.weights);
        return 0;
    }
    if (mode == Mode::KingSafety) {
        const nightwing::eval::MaterialWeights material_weights =
            nightwing::eval::default_material_weights();
        const nightwing::tuner::TermTuneResult<nightwing::eval::KingSafetyWeights> result =
            nightwing::tuner::tune_king_safety(
                positions, material_weights, nightwing::eval::default_king_safety_weights(), config);
        std::fprintf(stderr, "Initial loss: %.6f\nFinal loss:   %.6f\n", result.initial_loss,
                      result.final_loss);
        std::fprintf(stderr, "Loss history (iteration, loss):\n");
        for (const nightwing::tuner::TuneIteration& step : result.history) {
            std::fprintf(stderr, "  %4d  %.6f\n", step.iteration, step.loss);
        }
        print_term_weights(nightwing::tuner::kKingSafetyParameters, result.weights);
        return 0;
    }
    if (mode == Mode::Pawns) {
        const nightwing::eval::MaterialWeights material_weights =
            nightwing::eval::default_material_weights();
        const nightwing::tuner::TermTuneResult<nightwing::eval::PawnsWeights> result =
            nightwing::tuner::tune_pawns(positions, material_weights,
                                          nightwing::eval::default_pawns_weights(), config);
        std::fprintf(stderr, "Initial loss: %.6f\nFinal loss:   %.6f\n", result.initial_loss,
                      result.final_loss);
        std::fprintf(stderr, "Loss history (iteration, loss):\n");
        for (const nightwing::tuner::TuneIteration& step : result.history) {
            std::fprintf(stderr, "  %4d  %.6f\n", step.iteration, step.loss);
        }
        print_term_weights(nightwing::tuner::kPawnsParameters, result.weights);
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
