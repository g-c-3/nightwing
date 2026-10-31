// tests/closedness_minors_tests.cpp
//
// Unit tests for src/eval/closedness_minors.h.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/closedness_minors.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

Position bare_kings() {
    Position pos;
    pos.side_to_move = Color::White;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    return pos;
}

} // namespace

TEST_CASE("closedness_minors_value: starting position scores zero", "[eval][closedness]") {
    init_masks();
    REQUIRE(blocked_pawn_pairs(start_position()) == 0);
    REQUIRE(closedness_minors_value(start_position()) == Score{0, 0});
}

TEST_CASE("blocked_pawn_pairs counts each mutually blocked pair once", "[eval][closedness]") {
    init_masks();
    Position pos = bare_kings();
    pos.place_piece(make_square(3, 3), Piece::WhitePawn); // d4
    pos.place_piece(make_square(3, 4), Piece::BlackPawn); // d5
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    pos.place_piece(make_square(4, 4), Piece::BlackPawn); // e5
    pos.place_piece(make_square(0, 3), Piece::WhitePawn); // a4, a5 empty
    pos.place_piece(make_square(1, 5), Piece::BlackPawn); // b6, not in front of a White pawn
    REQUIRE(blocked_pawn_pairs(pos) == 2);
}

TEST_CASE("closedness_minors_value: a White knight in a closed position scores a bonus",
          "[eval][closedness]") {
    init_masks();
    Position pos = bare_kings();
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);
    pos.place_piece(make_square(3, 4), Piece::BlackPawn);
    pos.place_piece(make_square(4, 3), Piece::WhitePawn);
    pos.place_piece(make_square(4, 4), Piece::BlackPawn);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    REQUIRE(closedness_minors_value(pos) == kKnightPerBlockedPair * 2);
}

TEST_CASE("closedness_minors_value: a Black bishop in a closed position favors White",
          "[eval][closedness]") {
    init_masks();
    Position pos = bare_kings();
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);
    pos.place_piece(make_square(3, 4), Piece::BlackPawn);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop);
    REQUIRE(closedness_minors_value(pos) == -(kBishopPerBlockedPair * 1));
}

TEST_CASE("closedness_minors_value: no blocked pawns scores zero whatever the minors",
          "[eval][closedness]") {
    init_masks();
    Position pos = bare_kings();
    pos.place_piece(make_square(0, 3), Piece::WhitePawn);
    pos.place_piece(make_square(7, 4), Piece::BlackPawn);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop);
    REQUIRE(closedness_minors_value(pos) == Score{0, 0});
}

TEST_CASE("closedness_minors_value: equal minors cancel", "[eval][closedness]") {
    init_masks();
    Position pos = bare_kings();
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);
    pos.place_piece(make_square(3, 4), Piece::BlackPawn);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(1, 7), Piece::BlackKnight);
    REQUIRE(closedness_minors_value(pos) == Score{0, 0});
}
