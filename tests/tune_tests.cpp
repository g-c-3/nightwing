// tests/tune_tests.cpp
//
// Unit tests for src/tuner/tune.h/.cpp — the "gradient descent" half of
// ROADMAP.md Phase 5's Texel/SPSA tuner item (see tune.h's own header
// comment for the full design; tuner/selfplay.h, the "self-play data
// generation" half, has its own dedicated tests/selfplay_tests.cpp).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <string>

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

TEST_CASE("kMaterialParameters: pawn_mg/pawn_eg are anchored, every other field is not",
          "[tuner][tune]") {
    // Structural check on the table itself, independent of tune()'s own
    // behavior (covered separately below) — this file's own "anchored"
    // doc comment (tune.h) is only meaningful if these two specific
    // fields, and only these two, are actually marked that way.
    for (const MaterialParameterRef& param : kMaterialParameters) {
        const bool should_be_anchored =
            (param.member == &MaterialWeights::pawn_mg) || (param.member == &MaterialWeights::pawn_eg);
        REQUIRE(param.anchored == should_be_anchored);
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

TEST_CASE("compute_loss: an optional psqt_weights argument is forwarded to evaluate() exactly "
          "the same way the required material `weights` already is -- Tier 0 Step 4 (docs/"
          "DECISIONS.md, this entry's own dated wiring)",
          "[tuner][tune]") {
    init_all();
    const std::string fen = "4k3/8/8/8/8/8/8/1N2K3 w - - 0 1"; // lone White knight on b1
    const Position pos = parse_fen(fen);
    const MaterialWeights weights = default_material_weights();
    const double sigmoid_scale = 400.0;

    // Default (nullptr psqt_weights): compute_loss()'s own no-argument
    // default path.
    const int default_eval = evaluate(pos, nullptr, nullptr, &weights);
    const double default_label = sigmoid(static_cast<double>(default_eval) / sigmoid_scale);
    SelfPlayPosition position{fen, default_label};
    REQUIRE(compute_loss({position}, weights, sigmoid_scale) < 1e-12);

    // A perturbed psqt_weights: compute_loss() must now disagree with
    // the SAME label (still computed against the default table), since
    // the underlying evaluate() call it makes has changed -- confirming
    // psqt_weights is actually reaching evaluate(), not silently
    // ignored.
    PsqtWeights perturbed = default_psqt_weights();
    perturbed.knight_mg[make_square(1, 0)] += 200.0; // b1
    const double loss_with_override =
        compute_loss({position}, weights, sigmoid_scale, &perturbed);
    REQUIRE(loss_with_override > 1e-6);

    // And that loss must exactly match computing evaluate() directly
    // with the same override and re-deriving the loss by hand --
    // compute_loss() isn't doing anything to psqt_weights beyond
    // forwarding it straight through to evaluate().
    const int perturbed_eval = evaluate(pos, nullptr, nullptr, &weights, &perturbed);
    const double perturbed_predicted =
        sigmoid(static_cast<double>(perturbed_eval) / sigmoid_scale);
    const double expected_error = perturbed_predicted - default_label;
    REQUIRE(loss_with_override == expected_error * expected_error);
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

TEST_CASE("tune: pawn_mg/pawn_eg never move, even under training data that strongly disagrees "
          "with the current pawn value, while a non-anchored weight in the same run still does",
          "[tuner][tune]") {
    init_all();
    // White is up a knight AND two pawns, but every position is labeled
    // a draw (0.5) -- a strong, consistent "your pawn value (and your
    // knight value) are both too high" signal for BOTH kinds of
    // material. Without anchoring, this exact setup is the kind of
    // training signal that could plausibly justify moving pawn_mg/
    // pawn_eg for real -- so this specifically confirms anchoring wins
    // out over gradient signal, not just that it holds when there'd be
    // no gradient anyway (unlike the bare-kings "all-neutral" test
    // above, which can't distinguish "correctly anchored" from
    // "coincidentally zero gradient").
    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(0, 1), Piece::WhitePawn);
    pos.place_piece(make_square(2, 1), Piece::WhitePawn);
    const std::string fen = to_fen(pos);

    std::vector<SelfPlayPosition> positions;
    for (int i = 0; i < 8; ++i) {
        positions.push_back(SelfPlayPosition{fen, 0.5});
    }

    TuneConfig config; // production defaults
    config.iterations = 30;
    const MaterialWeights defaults = default_material_weights();
    const TuneResult result = tune(positions, defaults, config);

    // Anchored: exactly unchanged, not just "close to" -- tune() never
    // touches these fields at all (tune.cpp's own comment on why the
    // update step is skipped explicitly, not just left as a 0-gradient
    // no-op).
    REQUIRE(result.weights.pawn_mg == defaults.pawn_mg);
    REQUIRE(result.weights.pawn_eg == defaults.pawn_eg);

    // Not anchored: this same run's real gradient signal should still
    // move knight_eg, same reasoning as the pre-existing
    // knight-imbalance test above (low game phase here too -- one
    // minor and two pawns is still well under a typical middlegame's
    // non-pawn material).
    REQUIRE(result.weights.knight_eg < defaults.knight_eg);
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

TEST_CASE("ParameterRef::get/set: a scalar (MaterialParameterRef) entry reads/writes the exact "
          "field its name and member pointer claim -- Tier 0 Step 3 (docs/DECISIONS.md, this "
          "entry's own date) generalized MaterialParameterRef into ParameterRef<Weights>, and "
          "this pins get()/set() agreeing with the raw `.*member` access every existing test "
          "above already exercises",
          "[tuner][tune]") {
    MaterialWeights w = default_material_weights();
    const MaterialParameterRef& knight_mg = kMaterialParameters[2]; // declaration-order: knight_mg
    REQUIRE(std::string(knight_mg.name) == "knight_mg");
    REQUIRE(knight_mg.get(w) == w.knight_mg);

    knight_mg.set(w, 321.0);
    REQUIRE(w.knight_mg == 321.0);
    REQUIRE(knight_mg.get(w) == 321.0);
    REQUIRE(w.knight_eg == default_material_weights().knight_eg); // untouched
}

TEST_CASE("kPsqtParameters: has exactly 768 entries (12 PsqtWeights array fields * 64 squares "
          "each), every one an indexed-array entry (array_member set, member null)",
          "[tuner][tune]") {
    REQUIRE(kPsqtParameters.size() == 768);
    for (const PsqtParameterRef& param : kPsqtParameters) {
        REQUIRE(param.array_member != nullptr);
        REQUIRE(param.member == nullptr);
        REQUIRE(param.index >= 0);
        REQUIRE(param.index < 64);
        REQUIRE(param.anchored == false); // this session's decision -- see kPsqtParameters'
                                           // own doc comment (tune.h) for why
    }
}

TEST_CASE("kPsqtParameters: get() agrees with default_psqt_weights()/psqt_value() at a spot-"
          "checked entry from each of the 12 fields, and set() perturbs only that exact "
          "piece/phase/square -- confirms ParameterRef<PsqtWeights>'s array_member/index "
          "indexing is wired correctly, not just structurally present",
          "[tuner][tune]") {
    const PsqtWeights defaults = default_psqt_weights();

    // kPsqtParameters is laid out as 12 consecutive 64-entry blocks, in
    // kPsqtFields' own declaration order (pawn_mg, pawn_eg, knight_mg,
    // knight_eg, bishop_mg, bishop_eg, rook_mg, rook_eg, queen_mg,
    // queen_eg, king_mg, king_eg) -- index 0 is pawn_mg[a1], index 64 is
    // pawn_eg[a1], etc. Spot-checking the first entry of each block
    // (square a1, index 0 within its own block) against psqt.cpp's own
    // known a1-corner values (already hand-verified once in
    // eval_tests.cpp's own psqt spot-check test) is enough to confirm
    // the 12-blocks-of-64 layout is correct without re-deriving all 768
    // values by hand again here.
    struct Expected {
        std::size_t param_index; // block_index * 64 + 0 (square a1)
        const char* name;
        double a1_value;
    };
    const Expected expectations[] = {
        {0 * 64, "pawn_mg", 0.0},     {1 * 64, "pawn_eg", 0.0},     {2 * 64, "knight_mg", -50.0},
        {3 * 64, "knight_eg", -50.0}, {4 * 64, "bishop_mg", -20.0}, {5 * 64, "bishop_eg", -20.0},
        {6 * 64, "rook_mg", 0.0},     {7 * 64, "rook_eg", 0.0},     {8 * 64, "queen_mg", -20.0},
        {9 * 64, "queen_eg", -20.0},  {10 * 64, "king_mg", 20.0},   {11 * 64, "king_eg", -50.0},
    };
    for (const Expected& e : expectations) {
        const PsqtParameterRef& param = kPsqtParameters[e.param_index];
        REQUIRE(std::string(param.name) == e.name);
        REQUIRE(param.index == 0); // square a1
        REQUIRE(param.get(defaults) == e.a1_value);
    }

    // set() perturbation: touching knight_mg's a1 entry must not affect
    // knight_mg's a2 entry, knight_eg's a1 entry, or any other field.
    PsqtWeights w = defaults;
    const PsqtParameterRef& knight_mg_a1 = kPsqtParameters[2 * 64 + 0];
    const PsqtParameterRef& knight_mg_a2 = kPsqtParameters[2 * 64 + 8]; // a2 = square index 8
    const PsqtParameterRef& knight_eg_a1 = kPsqtParameters[3 * 64 + 0];
    knight_mg_a1.set(w, -999.0);
    REQUIRE(knight_mg_a1.get(w) == -999.0);
    REQUIRE(knight_mg_a2.get(w) == defaults.knight_mg[8]);
    REQUIRE(knight_eg_a1.get(w) == defaults.knight_eg[0]);
}

