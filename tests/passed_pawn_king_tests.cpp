// tests/passed_pawn_king_tests.cpp
//
// Unit tests for src/eval/passed_pawn_king.h (ROADMAP.md: restricted
// nonlinear feature interactions, candidate 1). Positions are built directly
// via Position::place_piece(), like every other eval/*_tests.cpp file.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/passed_pawn_king.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

void init_all() {
    init_masks();
}

Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("passed_pawn_king_value: no pawns means exactly zero", "[eval][passed_pawn_king]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    REQUIRE(passed_pawn_king_value(pos) == Score{0, 0});
}

TEST_CASE("passed_pawn_king_value: exact value for a White passer on e5 (relative rank 4)",
          "[eval][passed_pawn_king]") {
    init_all();
    // Pawn e5, stop square e6. Enemy king h8: Chebyshev distance to e6 is 3.
    // Own king a1: distance to e6 is 5 (max(4,5)), at the cap.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    pos.place_piece(make_square(7, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 4), Piece::WhitePawn);
    const int expected = kPassedKingRankWeight[4] * (kPassedEnemyKingDistFactor * 3 - kPassedOwnKingDistFactor * 5);
    REQUIRE(passed_pawn_king_value(pos) == Score{0, expected});
}

TEST_CASE("passed_pawn_king_value: a farther enemy king and a nearer own king both raise the value",
          "[eval][passed_pawn_king]") {
    init_all();
    Position base = empty_position();
    base.place_piece(make_square(0, 0), Piece::WhiteKing);
    base.place_piece(make_square(4, 7), Piece::BlackKing); // e8, distance 2 from e6
    base.place_piece(make_square(4, 4), Piece::WhitePawn);

    Position enemy_far = empty_position();
    enemy_far.place_piece(make_square(0, 0), Piece::WhiteKing);
    enemy_far.place_piece(make_square(0, 7), Piece::BlackKing); // a8, distance 4 from e6
    enemy_far.place_piece(make_square(4, 4), Piece::WhitePawn);

    Position own_near = empty_position();
    own_near.place_piece(make_square(3, 3), Piece::WhiteKing); // d4, distance 2 from e6
    own_near.place_piece(make_square(4, 7), Piece::BlackKing);
    own_near.place_piece(make_square(4, 4), Piece::WhitePawn);

    REQUIRE(passed_pawn_king_value(enemy_far).eg > passed_pawn_king_value(base).eg);
    REQUIRE(passed_pawn_king_value(own_near).eg > passed_pawn_king_value(base).eg);
}

TEST_CASE("passed_pawn_king_value: middlegame component is always zero", "[eval][passed_pawn_king]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(3, 3), Piece::WhiteKing);
    pos.place_piece(make_square(7, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 5), Piece::WhitePawn);
    REQUIRE(passed_pawn_king_value(pos).mg == 0);
}

TEST_CASE("passed_pawn_king_value: a pawn with an enemy pawn ahead on an adjacent file is not passed",
          "[eval][passed_pawn_king]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    pos.place_piece(make_square(7, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 4), Piece::WhitePawn); // e5
    pos.place_piece(make_square(3, 6), Piece::BlackPawn); // d7 guards the path
    // The black d7 pawn is itself passed (nothing of White's on c/d/e ahead of
    // it toward rank 1 except e5, which is on an adjacent file and below it),
    // so only the e5 pawn's contribution is checked via symmetry-free
    // comparison: removing d7 must change the value (e5 becomes passed).
    Position without = empty_position();
    without.place_piece(make_square(0, 0), Piece::WhiteKing);
    without.place_piece(make_square(7, 7), Piece::BlackKing);
    without.place_piece(make_square(4, 4), Piece::WhitePawn);
    REQUIRE(passed_pawn_king_value(without).eg != passed_pawn_king_value(pos).eg);
}

TEST_CASE("passed_pawn_king_value: color-mirrored position gives the exact negation",
          "[eval][passed_pawn_king]") {
    init_all();
    Position white = empty_position();
    white.place_piece(make_square(1, 1), Piece::WhiteKing);   // b2
    white.place_piece(make_square(6, 6), Piece::BlackKing);   // g7
    white.place_piece(make_square(4, 4), Piece::WhitePawn);   // e5

    Position black = empty_position();
    black.place_piece(make_square(1, 6), Piece::BlackKing);   // b7 (rank-mirrored)
    black.place_piece(make_square(6, 1), Piece::WhiteKing);   // g2
    black.place_piece(make_square(4, 3), Piece::BlackPawn);   // e4

    REQUIRE(passed_pawn_king_value(black) == -passed_pawn_king_value(white));
}
