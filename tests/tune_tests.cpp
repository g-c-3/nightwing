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

// --- ROADMAP.md Tier 0 "PSQT and beyond" -- Step 7, mobility (the first "beyond" term) ---

TEST_CASE("kMobilityParameters: covers exactly the 8 MobilityWeights fields, each a plain "
          "scalar entry (member set, array_member null), none anchored",
          "[tuner][tune]") {
    // Same structural shape as kMaterialParameters (plain scalar
    // entries), not kPsqtParameters (indexed-array entries) -- mobility
    // has no per-square dimension, just one mg/eg pair per piece type,
    // the same shape MaterialWeights itself has.
    REQUIRE(kMobilityParameters.size() == 8);
    for (const MobilityParameterRef& param : kMobilityParameters) {
        REQUIRE(param.member != nullptr);
        REQUIRE(param.array_member == nullptr);
        // Every mobility parameter is `anchored = false` -- see
        // kMobilityParameters' own doc comment (tune.h) for why: an
        // additive per-square bonus, like PSQT's, doesn't share
        // material's multiplicative flat-scaling degeneracy.
        REQUIRE(param.anchored == false);
    }
}

TEST_CASE("kMobilityParameters: every member pointer reaches exactly the field its name "
          "claims, and only that field",
          "[tuner][tune]") {
    // Same technique as kMaterialParameters' own equivalent test above:
    // set each field to a distinct sentinel through the table and
    // confirm exactly one field changed.
    for (std::size_t i = 0; i < kMobilityParameters.size(); ++i) {
        MobilityWeights probe = default_mobility_weights();
        probe.*(kMobilityParameters[i].member) = -1.0;
        int changed_count = 0;
        for (std::size_t j = 0; j < kMobilityParameters.size(); ++j) {
            if (probe.*(kMobilityParameters[j].member) == -1.0) {
                ++changed_count;
            }
        }
        REQUIRE(changed_count == 1);
    }
}

TEST_CASE("kMobilityParameters: get()/set() agree with default_mobility_weights() and each "
          "other's inverse, for every entry",
          "[tuner][tune]") {
    const MobilityWeights defaults = default_mobility_weights();
    const double expected[8] = {defaults.knight_mg, defaults.knight_eg, defaults.bishop_mg,
                                 defaults.bishop_eg, defaults.rook_mg,   defaults.rook_eg,
                                 defaults.queen_mg,  defaults.queen_eg};
    for (std::size_t i = 0; i < kMobilityParameters.size(); ++i) {
        REQUIRE(kMobilityParameters[i].get(defaults) == expected[i]);
        MobilityWeights w = defaults;
        kMobilityParameters[i].set(w, 12.5);
        REQUIRE(kMobilityParameters[i].get(w) == 12.5);
    }
}

TEST_CASE("compute_loss: an optional mobility_weights argument is forwarded to evaluate() "
          "exactly the same way psqt_weights already is",
          "[tuner][tune]") {
    init_all();
    // A position where mobility genuinely differs between the two
    // sides -- a single White knight with room to move, nothing else on
    // the board besides the two kings, so mobility_value()'s own knight
    // term is the only thing distinguishing this position from dead
    // equal, and perturbing kKnightMobilityBonus is guaranteed to move
    // the eval.
    const std::string fen = "4k3/8/8/8/8/8/8/1N2K3 w - - 0 1";
    const Position pos = parse_fen(fen);
    const MaterialWeights weights = default_material_weights();
    const double sigmoid_scale = 400.0;

    const int default_eval = evaluate(pos, nullptr, nullptr, &weights);
    const double default_label = sigmoid(static_cast<double>(default_eval) / sigmoid_scale);
    SelfPlayPosition position{fen, default_label};
    REQUIRE(compute_loss({position}, weights, sigmoid_scale) < 1e-12);

    MobilityWeights perturbed = default_mobility_weights();
    perturbed.knight_mg += 500.0; // wildly larger per-square knight bonus
    const double loss_with_override =
        compute_loss({position}, weights, sigmoid_scale, /*psqt_weights=*/nullptr, &perturbed);
    REQUIRE(loss_with_override > 1e-6);

    // Matches computing evaluate() directly with the same override and
    // re-deriving the loss by hand -- compute_loss() isn't doing
    // anything to mobility_weights beyond forwarding it straight
    // through to evaluate().
    const int perturbed_eval =
        evaluate(pos, nullptr, nullptr, &weights, /*psqt_weights=*/nullptr, &perturbed);
    const double perturbed_predicted =
        sigmoid(static_cast<double>(perturbed_eval) / sigmoid_scale);
    const double expected_error = perturbed_predicted - default_label;
    REQUIRE(loss_with_override == expected_error * expected_error);
}

// --- ROADMAP.md Tier 0 "PSQT and beyond" -- Step 8, space (the second "beyond" term) ---

TEST_CASE("kSpaceParameters: covers exactly the 2 SpaceWeights fields, each a plain scalar "
          "entry (member set, array_member null), none anchored",
          "[tuner][tune]") {
    // Same structural shape as kMaterialParameters/kMobilityParameters
    // (plain scalar entries), not kPsqtParameters (indexed-array
    // entries) -- space has no per-piece-type or per-square dimension,
    // just one mg/eg pair total, the smallest of the four tables so
    // far.
    REQUIRE(kSpaceParameters.size() == 2);
    for (const SpaceParameterRef& param : kSpaceParameters) {
        REQUIRE(param.member != nullptr);
        REQUIRE(param.array_member == nullptr);
        // Every space parameter is `anchored = false` -- see
        // kSpaceParameters' own doc comment (tune.h) for why: an
        // additive per-square bonus, like PSQT's/mobility's, doesn't
        // share material's multiplicative flat-scaling degeneracy.
        REQUIRE(param.anchored == false);
    }
}

TEST_CASE("kSpaceParameters: every member pointer reaches exactly the field its name claims, "
          "and only that field",
          "[tuner][tune]") {
    // Same technique as kMaterialParameters'/kMobilityParameters' own
    // equivalent tests above: set each field to a distinct sentinel
    // through the table and confirm exactly one field changed.
    for (std::size_t i = 0; i < kSpaceParameters.size(); ++i) {
        SpaceWeights probe = default_space_weights();
        probe.*(kSpaceParameters[i].member) = -1.0;
        int changed_count = 0;
        for (std::size_t j = 0; j < kSpaceParameters.size(); ++j) {
            if (probe.*(kSpaceParameters[j].member) == -1.0) {
                ++changed_count;
            }
        }
        REQUIRE(changed_count == 1);
    }
}

TEST_CASE("kSpaceParameters: get()/set() agree with default_space_weights() and each other's "
          "inverse, for every entry",
          "[tuner][tune]") {
    const SpaceWeights defaults = default_space_weights();
    const double expected[2] = {defaults.square_mg, defaults.square_eg};
    for (std::size_t i = 0; i < kSpaceParameters.size(); ++i) {
        REQUIRE(kSpaceParameters[i].get(defaults) == expected[i]);
        SpaceWeights w = defaults;
        kSpaceParameters[i].set(w, 12.5);
        REQUIRE(kSpaceParameters[i].get(w) == 12.5);
    }
}

TEST_CASE("compute_loss: an optional space_weights argument is forwarded to evaluate() exactly "
          "the same way psqt_weights/mobility_weights already are",
          "[tuner][tune]") {
    init_all();
    // A position where space genuinely differs between the two sides --
    // bare kings plus one Black pawn disqualifying one of Black's own
    // zone squares (the same one-square mechanism eval_tests.cpp's own
    // SpaceWeights override test uses), so space_value()'s own term is
    // the only thing distinguishing this position from dead equal, and
    // perturbing kSpaceSquareBonus is guaranteed to move the eval.
    const std::string fen = "4k3/3p4/8/8/8/8/8/4K3 w - - 0 1"; // d7 pawn, inside Black's own zone
    const Position pos = parse_fen(fen);
    const MaterialWeights weights = default_material_weights();
    const double sigmoid_scale = 400.0;

    const int default_eval = evaluate(pos, nullptr, nullptr, &weights);
    const double default_label = sigmoid(static_cast<double>(default_eval) / sigmoid_scale);
    SelfPlayPosition position{fen, default_label};
    REQUIRE(compute_loss({position}, weights, sigmoid_scale) < 1e-12);

    SpaceWeights perturbed = default_space_weights();
    // Both mg AND eg perturbed (unlike kMaterialParameters'/
    // kMobilityParameters' own equivalent tests, which only touch one
    // side of the mg/eg pair): this FEN is a bare-kings-plus-one-pawn
    // position with zero non-pawn material, so compute_phase() returns
    // 0 and taper() selects the eg term ENTIRELY -- perturbing square_mg
    // alone would be silently invisible here, not a genuine forwarding
    // failure. Perturbing both sidesteps that phase-dependence rather
    // than requiring a differently-shaped position just for this test.
    perturbed.square_mg += 500.0;
    perturbed.square_eg += 500.0;
    const double loss_with_override =
        compute_loss({position}, weights, sigmoid_scale, /*psqt_weights=*/nullptr,
                      /*mobility_weights=*/nullptr, &perturbed);
    REQUIRE(loss_with_override > 1e-6);

    // Matches computing evaluate() directly with the same override and
    // re-deriving the loss by hand -- compute_loss() isn't doing
    // anything to space_weights beyond forwarding it straight through
    // to evaluate().
    const int perturbed_eval = evaluate(pos, nullptr, nullptr, &weights,
                                         /*psqt_weights=*/nullptr,
                                         /*mobility_weights=*/nullptr, &perturbed);
    const double perturbed_predicted =
        sigmoid(static_cast<double>(perturbed_eval) / sigmoid_scale);
    const double expected_error = perturbed_predicted - default_label;
    REQUIRE(loss_with_override == expected_error * expected_error);
}

// --- ROADMAP.md Tier 0 "PSQT and beyond" -- Step 8b, threats (the third "beyond" term) ---

TEST_CASE("kThreatsParameters: covers exactly the 24 ThreatsWeights fields, each a plain "
          "scalar entry (member set, array_member null), none anchored",
          "[tuner][tune]") {
    // Same structural shape as kMaterialParameters/kMobilityParameters/
    // kSpaceParameters (plain scalar entries), not kPsqtParameters
    // (indexed-array entries) -- threats has no per-square dimension,
    // just 12 kXxxYyyPenalty constants x mg/eg each, more fields than
    // mobility's 8 or space's 2 but the same plain-scalar shape as
    // both.
    REQUIRE(kThreatsParameters.size() == 24);
    for (const ThreatsParameterRef& param : kThreatsParameters) {
        REQUIRE(param.member != nullptr);
        REQUIRE(param.array_member == nullptr);
        // Every threats parameter is `anchored = false` -- see
        // kThreatsParameters' own doc comment (tune.h) for why: an
        // additive per-piece penalty, like PSQT's/mobility's/space's,
        // doesn't share material's multiplicative flat-scaling
        // degeneracy.
        REQUIRE(param.anchored == false);
    }
}

TEST_CASE("kThreatsParameters: every member pointer reaches exactly the field its name "
          "claims, and only that field",
          "[tuner][tune]") {
    // Same technique as kMaterialParameters'/kMobilityParameters'/
    // kSpaceParameters' own equivalent tests above: set each field to a
    // distinct sentinel through the table and confirm exactly one field
    // changed.
    for (std::size_t i = 0; i < kThreatsParameters.size(); ++i) {
        ThreatsWeights probe = default_threats_weights();
        probe.*(kThreatsParameters[i].member) = -1.0;
        int changed_count = 0;
        for (std::size_t j = 0; j < kThreatsParameters.size(); ++j) {
            if (probe.*(kThreatsParameters[j].member) == -1.0) {
                ++changed_count;
            }
        }
        REQUIRE(changed_count == 1);
    }
}

TEST_CASE("kThreatsParameters: get()/set() agree with default_threats_weights() and each "
          "other's inverse, for every entry",
          "[tuner][tune]") {
    const ThreatsWeights defaults = default_threats_weights();
    for (std::size_t i = 0; i < kThreatsParameters.size(); ++i) {
        const double expected = kThreatsParameters[i].get(defaults);
        ThreatsWeights w = defaults;
        kThreatsParameters[i].set(w, 12.5);
        REQUIRE(kThreatsParameters[i].get(w) == 12.5);
        w = defaults;
        REQUIRE(kThreatsParameters[i].get(w) == expected);
    }
}

TEST_CASE("compute_loss: an optional threats_weights argument is forwarded to evaluate() "
          "exactly the same way psqt_weights/mobility_weights/space_weights already are",
          "[tuner][tune]") {
    init_all();
    // A position where threats genuinely differs between the two sides
    // -- a lone Black pawn attacking a defended White knight (the same
    // shape tests/threats_tests.cpp's own weights tests and this
    // session's own evaluate()-level ThreatsWeights test use), so
    // threats_value()'s own pawn-attack term is the only thing
    // distinguishing this position from dead equal, and perturbing
    // kKnightAttackedByPawnPenalty is guaranteed to move the eval.
    const std::string fen = "k7/8/4p3/3N4/8/8/8/K2R4 w - - 0 1";
    const Position pos = parse_fen(fen);
    const MaterialWeights weights = default_material_weights();
    const double sigmoid_scale = 400.0;

    const int default_eval = evaluate(pos, nullptr, nullptr, &weights);
    const double default_label = sigmoid(static_cast<double>(default_eval) / sigmoid_scale);
    SelfPlayPosition position{fen, default_label};
    REQUIRE(compute_loss({position}, weights, sigmoid_scale) < 1e-12);

    ThreatsWeights perturbed = default_threats_weights();
    perturbed.knight_pawn_mg -= 500.0; // wildly larger penalty against White
    perturbed.knight_pawn_eg -= 500.0;
    const double loss_with_override =
        compute_loss({position}, weights, sigmoid_scale, /*psqt_weights=*/nullptr,
                      /*mobility_weights=*/nullptr, /*space_weights=*/nullptr, &perturbed);
    REQUIRE(loss_with_override > 1e-6);

    // Matches computing evaluate() directly with the same override and
    // re-deriving the loss by hand -- compute_loss() isn't doing
    // anything to threats_weights beyond forwarding it straight through
    // to evaluate().
    const int perturbed_eval =
        evaluate(pos, nullptr, nullptr, &weights, /*psqt_weights=*/nullptr,
                 /*mobility_weights=*/nullptr, /*space_weights=*/nullptr, &perturbed);
    const double perturbed_predicted =
        sigmoid(static_cast<double>(perturbed_eval) / sigmoid_scale);
    const double expected_error = perturbed_predicted - default_label;
    REQUIRE(loss_with_override == expected_error * expected_error);
}

// --- ROADMAP.md Tier 0 "PSQT and beyond" -- Step 8b, king safety (the fourth "beyond" term) ---

TEST_CASE("kKingSafetyParameters: covers exactly the 22 KingSafetyWeights fields, each a "
          "plain scalar entry (member set, array_member null), none anchored",
          "[tuner][tune]") {
    // Every entry here sets only `member`, NEVER `array_member` -- see
    // KingSafetyWeights' own doc comment (eval/king_safety.h) and
    // kKingSafetyParameters' own doc comment (tune.h) for why
    // kPawnStormPenalty's 8-entry array is flattened into 6 named
    // scalar field pairs here rather than represented via this same
    // array_member mechanism kPsqtParameters uses.
    REQUIRE(kKingSafetyParameters.size() == 22);
    for (const KingSafetyParameterRef& param : kKingSafetyParameters) {
        REQUIRE(param.member != nullptr);
        REQUIRE(param.array_member == nullptr);
        // Every king-safety parameter is `anchored = false` -- see
        // kKingSafetyParameters' own doc comment (tune.h) for why: an
        // additive per-condition bonus/penalty, like PSQT's/mobility's/
        // space's/threats', doesn't share material's multiplicative
        // flat-scaling degeneracy.
        REQUIRE(param.anchored == false);
    }
}

TEST_CASE("kKingSafetyParameters: every member pointer reaches exactly the field its name "
          "claims, and only that field",
          "[tuner][tune]") {
    // Same technique as every sibling table's own equivalent test
    // above, with one adjustment: the sentinel is -999999.0, not -1.0
    // (kMaterialParameters'/kMobilityParameters'/kThreatsParameters'
    // own equivalent tests all use -1.0 safely, since none of THEIR
    // own default field values happen to equal it) -- KingSafetyWeights
    // specifically has two DEFAULT field values that are already
    // exactly -1.0 (kAttackUnitPenalty.eg and kPawnStormPenalty[3].eg,
    // both {..., -1} in king_safety.h), so -1.0 would silently produce
    // a false failure here (3 fields reading as "-1.0" instead of the
    // intended 1) rather than a real bug -- caught on the first test
    // run as a genuine, informative FAILED assertion (changed_count ==
    // 3, not 1), not a silent miscount. -999999.0 doesn't collide with
    // any real KingSafetyWeights default.
    for (std::size_t i = 0; i < kKingSafetyParameters.size(); ++i) {
        KingSafetyWeights probe = default_king_safety_weights();
        probe.*(kKingSafetyParameters[i].member) = -999999.0;
        int changed_count = 0;
        for (std::size_t j = 0; j < kKingSafetyParameters.size(); ++j) {
            if (probe.*(kKingSafetyParameters[j].member) == -999999.0) {
                ++changed_count;
            }
        }
        REQUIRE(changed_count == 1);
    }
}

TEST_CASE("kKingSafetyParameters: get()/set() agree with default_king_safety_weights() and "
          "each other's inverse, for every entry",
          "[tuner][tune]") {
    const KingSafetyWeights defaults = default_king_safety_weights();
    for (std::size_t i = 0; i < kKingSafetyParameters.size(); ++i) {
        const double expected = kKingSafetyParameters[i].get(defaults);
        KingSafetyWeights w = defaults;
        kKingSafetyParameters[i].set(w, 12.5);
        REQUIRE(kKingSafetyParameters[i].get(w) == 12.5);
        w = defaults;
        REQUIRE(kKingSafetyParameters[i].get(w) == expected);
    }
}

TEST_CASE("compute_loss: an optional king_safety_weights argument is forwarded to evaluate() "
          "exactly the same way psqt_weights/mobility_weights/space_weights/threats_weights "
          "already are",
          "[tuner][tune]") {
    init_all();
    // A White king with an intact 3-pawn shield vs. a bare Black king
    // (the same shape tests/king_safety_tests.cpp's own weights tests
    // and this session's own evaluate()-level KingSafetyWeights test
    // use) -- king_safety_value()'s own shield term is the only thing
    // distinguishing this position from dead equal, and perturbing
    // kShieldPawnBonus is guaranteed to move the eval.
    const std::string fen = "7k/8/8/8/8/8/3PPP2/4K3 w - - 0 1";
    const Position pos = parse_fen(fen);
    const MaterialWeights weights = default_material_weights();
    const double sigmoid_scale = 400.0;

    const int default_eval = evaluate(pos, nullptr, nullptr, &weights);
    const double default_label = sigmoid(static_cast<double>(default_eval) / sigmoid_scale);
    SelfPlayPosition position{fen, default_label};
    REQUIRE(compute_loss({position}, weights, sigmoid_scale) < 1e-12);

    KingSafetyWeights perturbed = default_king_safety_weights();
    perturbed.shield_mg += 500.0; // wildly larger per-pawn shield bonus
    perturbed.shield_eg += 500.0;
    const double loss_with_override = compute_loss(
        {position}, weights, sigmoid_scale, /*psqt_weights=*/nullptr,
        /*mobility_weights=*/nullptr, /*space_weights=*/nullptr,
        /*threats_weights=*/nullptr, &perturbed);
    REQUIRE(loss_with_override > 1e-6);

    // Matches computing evaluate() directly with the same override and
    // re-deriving the loss by hand -- compute_loss() isn't doing
    // anything to king_safety_weights beyond forwarding it straight
    // through to evaluate().
    const int perturbed_eval =
        evaluate(pos, nullptr, nullptr, &weights, /*psqt_weights=*/nullptr,
                 /*mobility_weights=*/nullptr, /*space_weights=*/nullptr,
                 /*threats_weights=*/nullptr, &perturbed);
    const double perturbed_predicted =
        sigmoid(static_cast<double>(perturbed_eval) / sigmoid_scale);
    const double expected_error = perturbed_predicted - default_label;
    REQUIRE(loss_with_override == expected_error * expected_error);
}

// --- ROADMAP.md Tier 0 "PSQT and beyond" -- Step 8b, pawn structure (the fifth and final "beyond" term) ---

TEST_CASE("kPawnsParameters: covers exactly the 59 PawnsWeights fields, each a plain scalar "
          "entry (member set, array_member null), none anchored",
          "[tuner][tune]") {
    // Every entry here sets only `member`, NEVER `array_member` -- see
    // PawnsWeights' own doc comment (eval/pawns.h) and
    // kPawnsParameters' own doc comment (tune.h) for why the FOUR
    // 8-entry relative-rank-indexed arrays in pawns.h are each
    // flattened into 6 named scalar field pairs here, the same
    // resolution KingSafetyWeights already applied to
    // kPawnStormPenalty's own single array.
    REQUIRE(kPawnsParameters.size() == 59);
    for (const PawnsParameterRef& param : kPawnsParameters) {
        REQUIRE(param.member != nullptr);
        REQUIRE(param.array_member == nullptr);
        // Every pawn-structure parameter is `anchored = false` -- see
        // kPawnsParameters' own doc comment (tune.h) for why: an
        // additive per-pawn or per-side bonus/penalty, like PSQT's/
        // mobility's/space's/threats'/king safety's, doesn't share
        // material's multiplicative flat-scaling degeneracy.
        REQUIRE(param.anchored == false);
    }
}

TEST_CASE("kPawnsParameters: every member pointer reaches exactly the field its name claims, "
          "and only that field",
          "[tuner][tune]") {
    // Same technique as every sibling table's own equivalent test
    // above, with the same -999999.0 sentinel adjustment
    // kKingSafetyParameters' own equivalent test needed (not -1.0):
    // PawnsWeights has several default field values in the small
    // single/low-double-digit range (e.g. kOutsidePassedPawnMinFileGap
    // == 3), so a small sentinel risks the identical false-collision
    // failure mode kKingSafetyParameters' own test already hit once
    // this session -- -999999.0 avoids it by inspection.
    for (std::size_t i = 0; i < kPawnsParameters.size(); ++i) {
        PawnsWeights probe = default_pawns_weights();
        probe.*(kPawnsParameters[i].member) = -999999.0;
        int changed_count = 0;
        for (std::size_t j = 0; j < kPawnsParameters.size(); ++j) {
            if (probe.*(kPawnsParameters[j].member) == -999999.0) {
                ++changed_count;
            }
        }
        REQUIRE(changed_count == 1);
    }
}

TEST_CASE("kPawnsParameters: get()/set() agree with default_pawns_weights() and each other's "
          "inverse, for every entry",
          "[tuner][tune]") {
    const PawnsWeights defaults = default_pawns_weights();
    for (std::size_t i = 0; i < kPawnsParameters.size(); ++i) {
        const double expected = kPawnsParameters[i].get(defaults);
        PawnsWeights w = defaults;
        kPawnsParameters[i].set(w, 12.5);
        REQUIRE(kPawnsParameters[i].get(w) == 12.5);
        w = defaults;
        REQUIRE(kPawnsParameters[i].get(w) == expected);
    }
}

TEST_CASE("compute_loss: an optional pawns_weights argument is forwarded to evaluate() "
          "exactly the same way psqt_weights/mobility_weights/space_weights/threats_weights/"
          "king_safety_weights already are",
          "[tuner][tune]") {
    init_all();
    // A lone, isolated, passed White pawn at relative rank 4 (the same
    // shape tests/pawns_tests.cpp's own weights tests and this
    // session's own evaluate()-level PawnsWeights test use) --
    // pawn_structure_value()'s own passed-pawn term is the only thing
    // distinguishing this position from dead equal, and perturbing
    // kPassedPawnBonus[4] is guaranteed to move the eval.
    const std::string fen = "4k3/8/8/4P3/8/8/8/4K3 w - - 0 1";
    const Position pos = parse_fen(fen);
    const MaterialWeights weights = default_material_weights();
    const double sigmoid_scale = 400.0;

    const int default_eval = evaluate(pos, nullptr, nullptr, &weights);
    const double default_label = sigmoid(static_cast<double>(default_eval) / sigmoid_scale);
    SelfPlayPosition position{fen, default_label};
    REQUIRE(compute_loss({position}, weights, sigmoid_scale) < 1e-12);

    PawnsWeights perturbed = default_pawns_weights();
    perturbed.passed_rank4_mg += 500.0; // wildly larger passed-pawn bonus
    perturbed.passed_rank4_eg += 500.0;
    const double loss_with_override = compute_loss(
        {position}, weights, sigmoid_scale, /*psqt_weights=*/nullptr,
        /*mobility_weights=*/nullptr, /*space_weights=*/nullptr,
        /*threats_weights=*/nullptr, /*king_safety_weights=*/nullptr, &perturbed);
    REQUIRE(loss_with_override > 1e-6);

    // Matches computing evaluate() directly with the same override and
    // re-deriving the loss by hand -- compute_loss() isn't doing
    // anything to pawns_weights beyond forwarding it straight through
    // to evaluate().
    const int perturbed_eval =
        evaluate(pos, nullptr, nullptr, &weights, /*psqt_weights=*/nullptr,
                 /*mobility_weights=*/nullptr, /*space_weights=*/nullptr,
                 /*threats_weights=*/nullptr, /*king_safety_weights=*/nullptr, &perturbed);
    const double perturbed_predicted =
        sigmoid(static_cast<double>(perturbed_eval) / sigmoid_scale);
    const double expected_error = perturbed_predicted - default_label;
    REQUIRE(loss_with_override == expected_error * expected_error);
}

// --- ROADMAP.md Tier 0 Step 5 -- L2 regularization (TuneConfig::l2_lambda) ---

TEST_CASE("tune: l2_lambda == 0.0 (the default) leaves TuneResult::initial_loss identical to "
          "bare compute_loss() -- a default (unregularized) run is unaffected by Step 5's new "
          "field at all",
          "[tuner][tune]") {
    init_all();
    std::vector<SelfPlayPosition> positions{SelfPlayPosition{"4k3/8/8/8/8/8/8/4K3 w - - 0 1", 0.5}};
    TuneConfig config;
    config.iterations = 0;
    const MaterialWeights weights = default_material_weights();
    const TuneResult result = tune(positions, weights, config);
    REQUIRE(result.initial_loss == compute_loss(positions, weights, config.sigmoid_scale));
}

TEST_CASE("tune: l2_lambda > 0.0 adds exactly lambda * sum(non-anchored weight^2) to the "
          "reported loss, on top of compute_loss()'s own bare MSE",
          "[tuner][tune]") {
    init_all();
    std::vector<SelfPlayPosition> positions{SelfPlayPosition{"4k3/8/8/8/8/8/8/4K3 w - - 0 1", 0.5}};
    TuneConfig config;
    config.iterations = 0;
    config.l2_lambda = 0.001;
    const MaterialWeights weights = default_material_weights();
    const TuneResult result = tune(positions, weights, config);

    // Hand-rolled sum over every NON-ANCHORED kMaterialParameters entry
    // (pawn_mg/pawn_eg excluded) -- independent of l2_penalty()'s own
    // (internal, untested-directly) implementation in tune.cpp.
    double sum_squares = 0.0;
    for (const MaterialParameterRef& param : kMaterialParameters) {
        if (param.anchored) {
            continue;
        }
        const double value = param.get(weights);
        sum_squares += value * value;
    }
    const double expected =
        compute_loss(positions, weights, config.sigmoid_scale) + config.l2_lambda * sum_squares;
    // Epsilon comparison, not exact `==` -- this test has the identical
    // structural shape (compute_loss() plus a hand-rolled sum, compared
    // against tune()'s own internal computation of the same quantity)
    // as the kPsqtParameters version of this test further down this
    // file, which a real GitHub Actions CI run (macOS Debug/Release,
    // run 94909780687) found DOES differ at the last representable bit
    // on that platform/compiler, even though Linux/GCC produced an
    // exact match for that same run. This test's own sum here is only 8
    // terms (kMaterialParameters' non-anchored entries), not 768, so it
    // has not been observed to actually diverge on any tested platform
    // -- but the underlying risk (floating-point addition is not
    // associative, and different compilers can reduce even a short
    // summation loop into a different instruction order) is identical
    // in kind, just with a currently-lower chance of the gap actually
    // reaching a representable bit. Fixed preemptively, at the same
    // 1e-9 absolute tolerance as that test, rather than waiting for a
    // future compiler version or optimization flag change to make it
    // fail here too.
    REQUIRE(std::fabs(result.initial_loss - expected) < 1e-9);
}

TEST_CASE("tune: a non-zero l2_lambda adds exactly the closed-form 2*lambda*value gradient "
          "contribution -- confirmed over a single iteration by comparing against an otherwise-"
          "identical unregularized run that shares the exact same finite-difference MSE "
          "gradient (both start from the same weights)",
          "[tuner][tune]") {
    init_all();
    // Same knight-imbalance fixture as the pre-existing "reduces loss"
    // test above.
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

    TuneConfig unregularized;
    unregularized.iterations = 1;
    const MaterialWeights start = default_material_weights();
    const TuneResult without_l2 = tune(positions, start, unregularized);

    TuneConfig regularized = unregularized;
    regularized.l2_lambda = 0.01;
    const TuneResult with_l2 = tune(positions, start, regularized);

    // Both runs computed their finite-difference MSE gradient at the
    // exact same starting weights (one iteration each) -- the only
    // difference in knight_eg's resulting value is the L2 term's own
    // extra, closed-form 2*lambda*knight_eg contribution to the
    // regularized run's gradient (tune.cpp's own comment at that line).
    const double expected_knight_eg =
        without_l2.weights.knight_eg -
        regularized.learning_rate * 2.0 * regularized.l2_lambda * start.knight_eg;
    const double diff = with_l2.weights.knight_eg - expected_knight_eg;
    REQUIRE(diff < 1e-6);
    REQUIRE(diff > -1e-6);
    REQUIRE(with_l2.weights.knight_eg != without_l2.weights.knight_eg);
}

// --- Session 106 -- l2_update_is_stable(), added after discovering the
// L2 term's own multiplicative interaction with learning_rate could
// silently diverge a real tuning run when exposed via the CLI ---

TEST_CASE("l2_update_is_stable: always true when l2_lambda == 0.0, regardless of learning_rate",
          "[tuner][tune]") {
    REQUIRE(l2_update_is_stable(20000.0, 0.0));
    REQUIRE(l2_update_is_stable(100.0, 0.0));
    REQUIRE(l2_update_is_stable(1e9, 0.0));   // even a wildly large learning_rate
    REQUIRE(l2_update_is_stable(-5.0, 0.0));  // even a nonsensical negative one
}

TEST_CASE("l2_update_is_stable: false exactly when 2*learning_rate*l2_lambda >= 1.0 -- the "
          "material CLI default (learning_rate=20000.0) with a modest-looking l2_lambda=0.001 "
          "is a real example that was NOT caught by this file's own pre-existing single-"
          "iteration L2 gradient test above, since that test never iterates far enough to "
          "observe divergence",
          "[tuner][tune]") {
    // Material default, l2_lambda=0.001: 2*20000*0.001 = 40 >= 1.0 -- unstable.
    // (Directly reproduces what a real `nightwing_tune 5 20000 1.0 400 0.001`
    // invocation was observed to do: loss went from 0.0066 to ~2e19 in 4
    // iterations.)
    REQUIRE_FALSE(l2_update_is_stable(20000.0, 0.001));

    // Same learning_rate, a safely small l2_lambda (well under the
    // 1/(2*20000) = 0.000025 threshold) -- stable.
    REQUIRE(l2_update_is_stable(20000.0, 0.00001));

    // PSQT's own CLI default (learning_rate=100.0): threshold is
    // 1/(2*100) = 0.005. The pre-existing tune_psqt() L2 test above used
    // 0.0001, safely under it (hence why that test never revealed this
    // either) -- 0.01 is over it.
    REQUIRE(l2_update_is_stable(100.0, 0.0001));
    REQUIRE_FALSE(l2_update_is_stable(100.0, 0.01));

    // Exactly at the boundary (2*learning_rate*l2_lambda == 1.0 exactly):
    // the per-iteration multiplier's magnitude is exactly 1 (a parameter
    // flips sign every iteration but never grows or shrinks) -- still
    // classified unstable (the strict "< 1.0" check, not "<= "), since a
    // parameter oscillating forever without converging is not a usable
    // tuning outcome either, even though it technically never diverges.
    REQUIRE_FALSE(l2_update_is_stable(100.0, 0.005));
}

TEST_CASE("tune: an l2_lambda past l2_update_is_stable()'s own threshold really does diverge "
          "geometrically over several iterations -- confirms the stability function's "
          "prediction against tune()'s actual behavior, not just the closed-form formula in "
          "isolation",
          "[tuner][tune]") {
    init_all();
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

    TuneConfig config;
    config.iterations = 5;
    config.l2_lambda = 0.001; // past the default learning_rate's own threshold
    REQUIRE_FALSE(l2_update_is_stable(config.learning_rate, config.l2_lambda));

    const TuneResult result = tune(positions, default_material_weights(), config);

    // knight_mg (non-anchored) must have blown up to a wildly larger
    // magnitude than any sane piece value -- confirms this is a real,
    // observable divergence in tune()'s actual output, not merely a
    // property of the closed-form formula considered on paper.
    REQUIRE(std::fabs(result.weights.knight_mg) > 1e6);

    // pawn_mg (anchored) is NEVER touched by tune() regardless of
    // l2_lambda or its own stability -- confirms the divergence is
    // specific to parameters the descent actually moves, not a blanket
    // corruption of every field.
    REQUIRE(result.weights.pawn_mg == default_material_weights().pawn_mg);
}


TEST_CASE("compute_psqt_gradient: an empty position list returns an all-zero PsqtWeights, "
          "matching compute_loss()'s own 0.0-for-empty convention",
          "[tuner][tune]") {
    const PsqtWeights gradient =
        compute_psqt_gradient({}, default_material_weights(), default_psqt_weights(), 400.0);
    for (double v : gradient.pawn_mg) {
        REQUIRE(v == 0.0);
    }
    for (double v : gradient.king_eg) {
        REQUIRE(v == 0.0);
    }
}

TEST_CASE("compute_psqt_gradient: agrees with a hand-rolled finite-difference probe of "
          "compute_loss() at a spot-checked cell -- an independent cross-check of the analytic "
          "derivation against the exact numerical method tune()'s own material gradient already "
          "relies on",
          "[tuner][tune]") {
    init_all();
    // A lone White knight on b1, otherwise bare kings -- same fixture
    // shape as this file's existing Tier 0 Step 4 test above.
    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    const std::string fen = to_fen(pos);
    const std::vector<SelfPlayPosition> positions{SelfPlayPosition{fen, 0.5}};

    const MaterialWeights material = default_material_weights();
    const PsqtWeights psqt = default_psqt_weights();
    const double sigmoid_scale = 400.0;
    const int b1 = make_square(1, 0);

    const PsqtWeights analytic = compute_psqt_gradient(positions, material, psqt, sigmoid_scale);

    const double eps = 1.0;
    PsqtWeights plus = psqt;
    plus.knight_mg[b1] += eps;
    const double loss_plus = compute_loss(positions, material, sigmoid_scale, &plus);
    PsqtWeights minus = psqt;
    minus.knight_mg[b1] -= eps;
    const double loss_minus = compute_loss(positions, material, sigmoid_scale, &minus);
    const double numeric_gradient = (loss_plus - loss_minus) / (2.0 * eps);

    const double diff = analytic.knight_mg[b1] - numeric_gradient;
    // A central finite difference at eps=1.0 (the smallest value that
    // reliably crosses psqt_value()'s own round_to_int() boundary in
    // both directions -- tune.h's own finite_diff_epsilon doc comment)
    // carries real O(eps^2) truncation error against compute_loss()'s
    // genuinely nonlinear (sigmoid-of-eval) shape, so this is a looser
    // tolerance than the two values' underlying agreement actually is --
    // loose enough to pass on a correct implementation, tight enough
    // that a wrong scaling factor or sign error (order-of-magnitude or
    // larger) would still fail it.
    REQUIRE(diff < 5e-5);
    REQUIRE(diff > -5e-5);

    // No piece of any OTHER field/square combination exists on this
    // board (only a knight on b1 and two kings on e1/e8) -- every other
    // cell's gradient must come out exactly 0.0, not just small.
    REQUIRE(analytic.pawn_mg[b1] == 0.0);
    REQUIRE(analytic.queen_mg[b1] == 0.0);
    // This exact fixture has zero non-pawn material other than the one
    // knight (compute_phase() near 0), so the mg-side gradient above is
    // near its largest possible magnitude while knight_eg's own gradient
    // (same cell, eg component) is comparatively small but still
    // generally non-zero at this phase -- not independently asserted
    // here, since this test's purpose is the mg-side numeric cross-check
    // above, not a full accounting of every field.
}

TEST_CASE("tune_psqt: a training signal that consistently disagrees with a deliberately-bad "
          "PSQT cell reduces loss over the course of the run",
          "[tuner][tune]") {
    init_all();
    // A lone White knight parked on a1 (kKnightMgTable's own worst
    // square, -50) but every position is labeled a clear White win
    // (1.0) -- a strong, consistent "this square is worth more than the
    // current table says" signal, the PSQT-side counterpart to this
    // file's existing material-side "reduces loss" test above.
    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteKnight); // a1
    const std::string fen = to_fen(pos);

    std::vector<SelfPlayPosition> positions;
    for (int i = 0; i < 8; ++i) {
        positions.push_back(SelfPlayPosition{fen, 1.0});
    }

    TuneConfig config;
    config.iterations = 20;
    // A much smaller learning rate than tune()'s own production default
    // (20000.0, tune.h's own doc comment: empirically chosen against
    // MaterialWeights' specific gradient scale) -- PSQT's per-position
    // gradient touches every one of 768 cells at once rather than a
    // single term's worth of 10, and this test's own scale was checked
    // directly against a couple of candidate values before landing here,
    // the same "measure before hand-picking a step size" discipline
    // tune.h's own learning_rate doc comment describes.
    config.learning_rate = 100.0;
    const PsqtTuneResult result = tune_psqt(positions, default_material_weights(),
                                             default_psqt_weights(), config);

    REQUIRE(result.history.size() == static_cast<std::size_t>(config.iterations + 1));
    REQUIRE(result.final_loss < result.initial_loss);
    // The a1 knight_mg cell specifically should have moved UP (less
    // negative) -- the direction that makes evaluate() agree more with
    // this training set's consistent "White is winning here" label.
    REQUIRE(result.weights.knight_mg[make_square(0, 0)] >
            default_psqt_weights().knight_mg[make_square(0, 0)]);
}

TEST_CASE("tune_psqt: TuneConfig::l2_lambda applies the same closed-form penalty it does in "
          "tune(), against kPsqtParameters instead of kMaterialParameters",
          "[tuner][tune]") {
    init_all();
    std::vector<SelfPlayPosition> positions{SelfPlayPosition{"4k3/8/8/8/8/8/8/4K3 w - - 0 1", 0.5}};
    TuneConfig config;
    config.iterations = 0;
    config.l2_lambda = 0.0001;
    const PsqtWeights psqt = default_psqt_weights();
    const PsqtTuneResult result =
        tune_psqt(positions, default_material_weights(), psqt, config);

    double sum_squares = 0.0;
    for (const PsqtParameterRef& param : kPsqtParameters) {
        const double value = param.get(psqt); // no PSQT parameter is anchored (its own doc
                                               // comment, tune.h)
        sum_squares += value * value;
    }
    const double expected =
        compute_loss(positions, default_material_weights(), config.sigmoid_scale, &psqt) +
        config.l2_lambda * sum_squares;
    // Epsilon comparison, not exact `==` -- confirmed via GitHub Actions CI
    // (macOS Debug/Release, run 94909780687) that this test's own two
    // independently-summed 768-term sum_squares accumulations (this
    // loop here vs. whatever summation order tune_psqt()'s own internal
    // l2_penalty() helper uses) can legitimately differ in their very
    // last representable bit across compilers -- observed as
    // 27.82503905843135428 (Linux/GCC) vs. 27.82503905843135072
    // (macOS/Clang) for the exact same source code and inputs, a
    // difference of about 3.6e-15 in an ~28-magnitude value, i.e. right
    // at IEEE 754 double precision's own limit, not a real algorithmic
    // discrepancy -- floating-point addition is not associative, and
    // different compilers reduce a summation loop like this one into
    // SIMD lanes differently, which changes the order additions
    // actually happen in at the hardware level even though the SOURCE
    // CODE'S mathematical meaning is identical. A 1e-9 absolute
    // tolerance is many orders of magnitude looser than that ~3.6e-15
    // gap while still tight enough to catch any genuine algorithmic
    // disagreement between this test's own hand-rolled sum and
    // tune_psqt()'s real implementation.
    REQUIRE(std::fabs(result.initial_loss - expected) < 1e-9);
}

