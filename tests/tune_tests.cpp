// tests/tune_tests.cpp
//
// Unit tests for src/tuner/tune.h/.cpp — the "gradient descent" half of
// ROADMAP.md Phase 5's Texel/SPSA tuner item (see tune.h's own header
// comment for the full design; tuner/selfplay.h, the "self-play data
// generation" half, has its own dedicated tests/selfplay_tests.cpp).

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "eval/psqt.h"
#include "tuner/selfplay.h"
#include "tuner/tune.h"

using namespace nightwing::board;
using namespace nightwing::eval;
using namespace nightwing::tuner;

namespace {

// Same per-process-init requirement as every other test file touching
// eval::evaluate() (see e.g. eval_tests.cpp's own init_all() comment).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

} // namespace

TEST_CASE("sigmoid: 0 maps to exactly 0.5", "[tuner][tune]") {
    REQUIRE(sigmoid(0.0) == 0.5);
}

TEST_CASE("sigmoid: large positive/negative inputs approach 1/0 respectively",
          "[tuner][tune]") {
    REQUIRE(sigmoid(50.0) > 0.999999);
    REQUIRE(sigmoid(-50.0) < 0.000001);
}

TEST_CASE("sigmoid: monotonically increasing", "[tuner][tune]") {
    REQUIRE(sigmoid(-1.0) < sigmoid(0.0));
    REQUIRE(sigmoid(0.0) < sigmoid(1.0));
    REQUIRE(sigmoid(1.0) < sigmoid(2.0));
}

TEST_CASE("kMaterialParameters: covers exactly the 10 MaterialWeights fields, each reachable "
          "through its member pointer",
          "[tuner][tune]") {
    REQUIRE(kMaterialParameters.size() == 10);

    // Every member pointer actually reaches the field its name claims —
    // set each one to a distinct sentinel through the table and confirm
    // it (and only it) changed.
    for (std::size_t i = 0; i < kMaterialParameters.size(); ++i) {
        MaterialWeights probe = default_material_weights();
        probe.*(kMaterialParameters[i].member) = -1.0;
        int changed_count = 0;
        for (std::size_t j = 0; j < kMaterialParameters.size(); ++j) {
            if (probe.*(kMaterialParameters[j].member) == -1.0) {
                ++changed_count;
            }
        }
        REQUIRE(changed_count == 1); // only field i changed, even if some other field's
                                      // default also happened to equal -1.0 (none do, but
                                      // this confirms it structurally, not by inspection)
    }
}

TEST_CASE("compute_loss: an empty position list returns 0.0 rather than dividing by zero",
          "[tuner][tune]") {
    const MaterialWeights weights = default_material_weights();
    REQUIRE(compute_loss({}, weights, 400.0) == 0.0);
}

TEST_CASE("compute_loss: a position whose label exactly matches its predicted win probability "
          "has zero loss",
          "[tuner][tune]") {
    init_all();
    // Bare kings: evaluate() here is small but NOT exactly 0 (a modest
    // tempo bonus, eval/tempo.h -- eval_tests.cpp's own "starting
    // position is balanced apart from the tempo bonus" test already
    // establishes this isn't unique to the real starting position).
    // Rather than assuming a round-number label like 0.5 happens to
    // match evaluate()'s actual value closely enough, this test computes
    // the position's real evaluate() result first and constructs a
    // label that matches its predicted win probability EXACTLY --
    // guaranteeing zero loss by construction, not by coincidence.
    const std::string fen = "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
    const MaterialWeights weights = default_material_weights();
    const Position pos = parse_fen(fen);
    const double sigmoid_scale = 400.0;
    const int eval_score = evaluate(pos, nullptr, nullptr, &weights);
    const double exact_label = sigmoid(static_cast<double>(eval_score) / sigmoid_scale);

    SelfPlayPosition position{fen, exact_label};
    const double loss = compute_loss({position}, weights, sigmoid_scale);
    REQUIRE(loss < 1e-12);
}

TEST_CASE("compute_loss: a position whose evaluate() strongly disagrees with its label has "
          "high loss",
          "[tuner][tune]") {
    init_all();
    // White has an extra queen -- evaluate() strongly favors White --
    // but labeled as a Black win (0.0), a strong disagreement.
    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(3, 0), Piece::WhiteQueen);

    SelfPlayPosition position{to_fen(pos), 0.0};
    const MaterialWeights weights = default_material_weights();
    const double loss = compute_loss({position}, weights, 400.0);
    REQUIRE(loss > 0.5); // predicted win probability for White is high, label says 0 -- big error
}

TEST_CASE("tune: an all-neutral (bare kings, 0.5 result) training set leaves material weights "
          "exactly unchanged",
          "[tuner][tune]") {
    init_all();
    // Bare kings: no pawn/knight/bishop/rook/queen exists on the board
    // for either side, so material_value()'s weights literally cannot
    // affect evaluate()'s result here (King/None always return {0, 0}
    // regardless of `weights` -- material_value()'s own doc comment) --
    // every one of kMaterialParameters' gradients must come out EXACTLY
    // 0, not just small, so weights should move by exactly nothing, no
    // matter how many iterations or how large a learning rate. (The
    // loss itself is not exactly 0 -- a small, constant residual from
    // the tempo bonus, eval/tempo.h, unrelated to material weights at
    // all -- see eval_tests.cpp's own "starting position is balanced
    // apart from the tempo bonus" test for that established fact.)
    std::vector<SelfPlayPosition> positions;
    for (int i = 0; i < 5; ++i) {
        positions.push_back(SelfPlayPosition{"4k3/8/8/8/8/8/8/4K3 w - - 0 1", 0.5});
    }

    TuneConfig config;
    config.iterations = 5;
    const TuneResult result = tune(positions, default_material_weights(), config);

    REQUIRE(result.history.size() == static_cast<std::size_t>(config.iterations + 1));
    REQUIRE(result.final_loss == result.initial_loss); // gradient is exactly 0 for every
                                                         // parameter -- no drift at all expected
    const MaterialWeights defaults = default_material_weights();
    REQUIRE(result.weights.pawn_mg == defaults.pawn_mg);
    REQUIRE(result.weights.knight_eg == defaults.knight_eg);
    REQUIRE(result.weights.queen_mg == defaults.queen_mg);
}

TEST_CASE("tune: a training set that consistently disagrees with the starting weights reduces "
          "loss over the course of the run",
          "[tuner][tune]") {
    init_all();
    // Every position has White up a knight but is labeled as a DRAW
    // (0.5) rather than a White-favoring result -- consistently telling
    // the tuner "White's material edge here is worth less than the
    // current knight value says." A real gradient-descent run should
    // reduce the loss below its starting point (even if it doesn't
    // reach a global optimum in a handful of iterations).
    //
    // This exact position (bare kings + one White knight) has a very
    // low game phase (compute_phase() close to 0 out of kMaxPhase --
    // only one minor piece's worth of non-pawn material on the board),
    // so taper() weights the EG term far more heavily than MG here --
    // confirmed directly against this session's own build before
    // writing this test (docs/DECISIONS.md, this file's introducing
    // entry) -- which is why this test checks knight_eg specifically,
    // not knight_mg (whose gradient is close enough to zero at this
    // exact phase that it may not move at all, correctly).
    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    const std::string fen = to_fen(pos);

    std::vector<SelfPlayPosition> positions;
    for (int i = 0; i < 8; ++i) {
        positions.push_back(SelfPlayPosition{fen, 0.5});
    }

    TuneConfig config; // production defaults, including learning_rate
    config.iterations = 30;
    const TuneResult result = tune(positions, default_material_weights(), config);

    REQUIRE(result.history.size() == static_cast<std::size_t>(config.iterations + 1));
    REQUIRE(result.final_loss < result.initial_loss);
    // The loss curve should be monotonically non-increasing step to
    // step too, not just lower at the very end -- true gradient descent
    // on a loss this smooth (relative to a single fixed, small
    // finite_diff_epsilon) shouldn't oscillate.
    for (std::size_t i = 1; i < result.history.size(); ++i) {
        REQUIRE(result.history[i].loss <= result.history[i - 1].loss);
    }
    // knight_eg specifically should have moved down (see this test's
    // own comment above on why eg, not mg, carries the real signal at
    // this near-zero game phase).
    REQUIRE(result.weights.knight_eg < default_material_weights().knight_eg);
}

TEST_CASE("tune: TuneResult::initial_loss/final_loss match history.front()/history.back()",
          "[tuner][tune]") {
    init_all();
    std::vector<SelfPlayPosition> positions{SelfPlayPosition{"4k3/8/8/8/8/8/8/4K3 w - - 0 1", 0.5}};

    TuneConfig config;
    config.iterations = 3;
    const TuneResult result = tune(positions, default_material_weights(), config);

    REQUIRE(result.initial_loss == result.history.front().loss);
    REQUIRE(result.final_loss == result.history.back().loss);
    REQUIRE(result.history.front().iteration == 0);
    REQUIRE(result.history.back().iteration == config.iterations);
}
