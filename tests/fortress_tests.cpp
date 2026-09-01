// tests/fortress_tests.cpp
//
// Unit tests for src/eval/fortress.h's fortress_value() (ROADMAP.md
// Phase 6's "Fortress pattern detection" item). Same style as the
// other Phase 6 test files: positions built directly via
// Position::place_piece(), squares written make_square(file, rank)
// (both 0-indexed), every scenario independently verified against the
// exact C++ branch logic via a Python simulation before being written
// here as a TEST_CASE.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/fortress.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("fortress_value: starting position (no material lead, no blockade) is Score{}",
          "[eval][fortress]") {
    REQUIRE(fortress_value(start_position()) == Score{});
}

TEST_CASE("fortress_value: White up a whole rook, but heavily blocked (8 mutually-blocked "
          "pawns) and few enough pieces remain -- a real, nonzero discount applied against "
          "White",
          "[eval][fortress]") {
    // White pawns a4/b4/c4/d4, Black pawns a5/b5/c5/d5 -- every one of
    // the 8 pawns is mutually blocked by its counterpart. White has an
    // extra rook (500cp, the only non-pawn material on the board, well
    // under kFortressMaxNonPawnPieces). material_lead_cp = 500 -> mg
    // discount = 500/4 = 125, eg discount = 500/2 = 250.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(0, 3), Piece::WhitePawn);
    pos.place_piece(make_square(1, 3), Piece::WhitePawn);
    pos.place_piece(make_square(2, 3), Piece::WhitePawn);
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);
    pos.place_piece(make_square(0, 4), Piece::BlackPawn);
    pos.place_piece(make_square(1, 4), Piece::BlackPawn);
    pos.place_piece(make_square(2, 4), Piece::BlackPawn);
    pos.place_piece(make_square(3, 4), Piece::BlackPawn);
    REQUIRE(fortress_value(pos) == Score{-125, -250});
}

TEST_CASE("fortress_value: same material lead as above, but the pawns are NOT mutually "
          "blocked -- no adjustment (Score{})",
          "[eval][fortress]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(0, 3), Piece::WhitePawn);
    pos.place_piece(make_square(4, 4), Piece::BlackPawn); // far away, not blocking anything
    REQUIRE(fortress_value(pos) == Score{});
}

TEST_CASE("fortress_value: equal material, even with a heavily blocked structure -- no "
          "adjustment (Score{}), nothing to discount",
          "[eval][fortress]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 3), Piece::WhitePawn);
    pos.place_piece(make_square(1, 3), Piece::WhitePawn);
    pos.place_piece(make_square(2, 3), Piece::WhitePawn);
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);
    pos.place_piece(make_square(0, 4), Piece::BlackPawn);
    pos.place_piece(make_square(1, 4), Piece::BlackPawn);
    pos.place_piece(make_square(2, 4), Piece::BlackPawn);
    pos.place_piece(make_square(3, 4), Piece::BlackPawn);
    REQUIRE(fortress_value(pos) == Score{});
}

TEST_CASE("fortress_value: a queen anywhere on the board disqualifies the position outright, "
          "even with an otherwise-matching heavily-blocked, material-imbalanced structure",
          "[eval][fortress]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(3, 0), Piece::WhiteQueen);
    pos.place_piece(make_square(0, 3), Piece::WhitePawn);
    pos.place_piece(make_square(1, 3), Piece::WhitePawn);
    pos.place_piece(make_square(2, 3), Piece::WhitePawn);
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);
    pos.place_piece(make_square(0, 4), Piece::BlackPawn);
    pos.place_piece(make_square(1, 4), Piece::BlackPawn);
    pos.place_piece(make_square(2, 4), Piece::BlackPawn);
    pos.place_piece(make_square(3, 4), Piece::BlackPawn);
    REQUIRE(fortress_value(pos) == Score{});
}

TEST_CASE("fortress_value: too many remaining non-pawn pieces (over "
          "kFortressMaxNonPawnPieces) disqualifies the position outright, even with an "
          "otherwise-matching blocked structure and material lead",
          "[eval][fortress]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(2, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(3, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(5, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(6, 0), Piece::BlackKnight);
    pos.place_piece(make_square(7, 0), Piece::BlackBishop);
    pos.place_piece(make_square(0, 3), Piece::WhitePawn);
    pos.place_piece(make_square(1, 3), Piece::WhitePawn);
    pos.place_piece(make_square(2, 3), Piece::WhitePawn);
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);
    pos.place_piece(make_square(0, 4), Piece::BlackPawn);
    pos.place_piece(make_square(1, 4), Piece::BlackPawn);
    pos.place_piece(make_square(2, 4), Piece::BlackPawn);
    pos.place_piece(make_square(3, 4), Piece::BlackPawn);
    REQUIRE(fortress_value(pos) == Score{});
}
