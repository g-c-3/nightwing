// tests/king_exposure_tests.cpp
//
// Unit tests for src/eval/king_exposure.h (ROADMAP.md, restricted
// nonlinear feature interactions, candidate 4). Positions are built
// directly via Position::place_piece(), matching the other
// eval/*_tests.cpp files, so each test isolates king_exposure_value().
// Expected values are hand-computed from the attack-unit weights
// (knight 1, bishop 1, rook 2, queen 4) and the default weight
// kExposureAttackPenalty = {-1, 0}.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "eval/king_exposure.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

void init_all() {
    init_masks();
    init_magic_bitboards();
}

Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

/// White king g1, pawns on g2/h2 (f-file shelter missing), Black king
/// e8, Black queen d4 attacking g1 along d4-e3-f2-g1. Exposure of White
/// = 1 (f-file), units = 4.
Position white_missing_f_pawn() {
    Position pos = empty_position();
    pos.place_piece(make_square(6, 0), Piece::WhiteKing);  // g1
    pos.place_piece(make_square(6, 1), Piece::WhitePawn);  // g2
    pos.place_piece(make_square(7, 1), Piece::WhitePawn);  // h2
    pos.place_piece(make_square(4, 7), Piece::BlackKing);  // e8
    pos.place_piece(make_square(3, 3), Piece::BlackQueen); // d4
    return pos;
}

} // namespace

TEST_CASE("king_exposure_value: starting position is exactly balanced", "[eval][king_exposure]") {
    init_all();
    REQUIRE(king_exposure_value(start_position()) == Score{0, 0});
    REQUIRE(king_exposure_level(start_position(), Color::White) == 0);
    REQUIRE(king_exposure_level(start_position(), Color::Black) == 0);
}

TEST_CASE("king_exposure_level: counts shelter files with no pawn in the 2-rank zone",
          "[eval][king_exposure]") {
    init_all();

    Position bare = empty_position();
    bare.place_piece(make_square(6, 0), Piece::WhiteKing); // g1
    bare.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    REQUIRE(king_exposure_level(bare, Color::White) == 3);

    // f2 and h2 present, g-pawn advanced to g4 (outside the 2-rank
    // zone): only the g-file is counted as missing.
    Position advanced = bare;
    advanced.place_piece(make_square(5, 1), Piece::WhitePawn); // f2
    advanced.place_piece(make_square(7, 1), Piece::WhitePawn); // h2
    advanced.place_piece(make_square(6, 3), Piece::WhitePawn); // g4
    REQUIRE(king_exposure_level(advanced, Color::White) == 1);

    // Pawn on the 3rd rank is still inside the zone.
    Position third_rank = bare;
    third_rank.place_piece(make_square(5, 2), Piece::WhitePawn); // f3
    REQUIRE(king_exposure_level(third_rank, Color::White) == 2);

    // Edge king: only 2 files exist (g, h).
    Position edge = empty_position();
    edge.place_piece(make_square(7, 0), Piece::WhiteKing); // h1
    edge.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    REQUIRE(king_exposure_level(edge, Color::White) == 2);

    // Black is measured from its own side: king g8, pawn on g7.
    Position black = empty_position();
    black.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    black.place_piece(make_square(6, 7), Piece::BlackKing); // g8
    black.place_piece(make_square(6, 6), Piece::BlackPawn); // g7
    REQUIRE(king_exposure_level(black, Color::Black) == 2);
}

TEST_CASE("king_exposure_value: an intact shelter is not charged even under attack",
          "[eval][king_exposure]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(6, 0), Piece::WhiteKing);  // g1
    pos.place_piece(make_square(5, 1), Piece::WhitePawn);  // f2
    pos.place_piece(make_square(6, 1), Piece::WhitePawn);  // g2
    pos.place_piece(make_square(7, 1), Piece::WhitePawn);  // h2
    pos.place_piece(make_square(4, 7), Piece::BlackKing);  // e8
    pos.place_piece(make_square(3, 3), Piece::BlackQueen); // d4, attacks f2
    REQUIRE(king_exposure_level(pos, Color::White) == 0);
    REQUIRE(king_exposure_value(pos) == Score{0, 0});
}

TEST_CASE("king_exposure_value: a broken shelter under queen attack is charged units x exposure",
          "[eval][king_exposure]") {
    init_all();
    // exposure 1, units 4 -> product 4 -> -4 mg. Black's own king (bare,
    // exposure 3) faces no White attackers, so contributes 0.
    REQUIRE(king_exposure_value(white_missing_f_pawn()) == Score{-4, 0});
}

TEST_CASE("king_exposure_value: the charge scales linearly with exposure and with units",
          "[eval][king_exposure]") {
    init_all();

    // Exposure 2 (only h2 left), same queen: product 2 x 4 = 8.
    Position two_files = empty_position();
    two_files.place_piece(make_square(6, 0), Piece::WhiteKing);
    two_files.place_piece(make_square(7, 1), Piece::WhitePawn);  // h2
    two_files.place_piece(make_square(4, 7), Piece::BlackKing);
    two_files.place_piece(make_square(3, 3), Piece::BlackQueen); // d4
    REQUIRE(king_exposure_value(two_files) == Score{-8, 0});

    // Exposure 1, queen (4) + rook f8 on the open f-file (2): 6 x 1.
    Position more_units = white_missing_f_pawn();
    more_units.place_piece(make_square(5, 7), Piece::BlackRook); // f8
    REQUIRE(king_exposure_value(more_units) == Score{-6, 0});
}

TEST_CASE("king_exposure_value: no enemy attackers means no charge even with a bare king",
          "[eval][king_exposure]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(6, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    REQUIRE(king_exposure_value(pos) == Score{0, 0});
}

TEST_CASE("king_exposure_value: color-mirrored positions give negated scores",
          "[eval][king_exposure]") {
    init_all();
    // Black king g8, pawns g7/h7, f7 missing; White queen d5 attacks g8
    // via d5-e6-f7-g8. Black exposure 1, units 4 -> White-relative +4.
    Position pos = empty_position();
    pos.place_piece(make_square(6, 7), Piece::BlackKing);  // g8
    pos.place_piece(make_square(6, 6), Piece::BlackPawn);  // g7
    pos.place_piece(make_square(7, 6), Piece::BlackPawn);  // h7
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);  // e1
    pos.place_piece(make_square(3, 4), Piece::WhiteQueen); // d5
    REQUIRE(king_exposure_value(pos) == Score{4, 0});
}

TEST_CASE("king_exposure_value: a missing king is tolerated", "[eval][king_exposure]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(6, 0), Piece::WhiteKing);
    REQUIRE(king_exposure_value(pos) == Score{0, 0});
    REQUIRE(king_exposure_level(pos, Color::Black) == 0);
}

TEST_CASE("default_king_exposure_weights: matches the constant, and an explicit default "
          "override is identical to nullptr",
          "[eval][king_exposure]") {
    init_all();
    const KingExposureWeights w = default_king_exposure_weights();
    REQUIRE(w.attack_exposure_mg == kExposureAttackPenalty.mg);
    REQUIRE(w.attack_exposure_eg == kExposureAttackPenalty.eg);

    const Position pos = white_missing_f_pawn();
    REQUIRE(king_exposure_value(pos, &w) == king_exposure_value(pos));
}

TEST_CASE("king_exposure_value: overrides change the result, and fractional weights round once "
          "on the product",
          "[eval][king_exposure]") {
    init_all();
    const Position pos = white_missing_f_pawn(); // product = 4

    KingExposureWeights w;
    w.attack_exposure_mg = -2.0;
    w.attack_exposure_eg = -1.0;
    REQUIRE(king_exposure_value(pos, &w) == Score{-8, -4});

    // -0.4 * 4 = -1.6 -> rounds to -2 (rounding the weight first would
    // give 0, so this pins the round-the-product rule).
    w.attack_exposure_mg = -0.4;
    w.attack_exposure_eg = 0.0;
    REQUIRE(king_exposure_value(pos, &w) == Score{-2, 0});
}
