// tests/bishop_pair_closedness_tests.cpp
//
// Unit tests for src/eval/bishop_pair_closedness.h (ROADMAP.md
// "Restricted nonlinear feature interactions", candidate 2). Positions
// are built directly via Position::place_piece(), matching every other
// eval/*_tests.cpp file's style, to isolate this one term.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/bishop_pair_closedness.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

Position empty_position() {
    Position pos;
    pos.side_to_move = Color::White;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    return pos;
}

void give_white_pair(Position& pos) {
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    pos.place_piece(make_square(5, 0), Piece::WhiteBishop); // f1
}

void give_black_pair(Position& pos) {
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8
    pos.place_piece(make_square(5, 7), Piece::BlackBishop); // f8
}

void lock(Position& pos, int file, int white_rank) {
    pos.place_piece(make_square(file, white_rank), Piece::WhitePawn);
    pos.place_piece(make_square(file, white_rank + 1), Piece::BlackPawn);
}

} // namespace

TEST_CASE("bishop_pair_closedness: start position scores exactly zero",
          "[eval][bishop_pair_closedness]") {
    const Position pos = start_position();
    REQUIRE(locked_central_pawn_pairs(pos) == 0);
    REQUIRE(bishop_pair_closedness_value(pos) == Score{0, 0});
}

TEST_CASE("bishop_pair_closedness: one locked central pair, White pair -> exactly one constant",
          "[eval][bishop_pair_closedness]") {
    Position pos = empty_position();
    give_white_pair(pos);
    lock(pos, 4, 3); // e4 vs e5
    REQUIRE(locked_central_pawn_pairs(pos) == 1);
    REQUIRE(bishop_pair_closedness_value(pos) == kBishopPairPerLockedCentralPair);
    REQUIRE(bishop_pair_closedness_value(pos).mg < 0);
}

TEST_CASE("bishop_pair_closedness: scaling is linear in locked central pairs",
          "[eval][bishop_pair_closedness]") {
    Position pos = empty_position();
    give_white_pair(pos);
    lock(pos, 3, 3); // d4 vs d5
    lock(pos, 4, 3); // e4 vs e5
    lock(pos, 2, 2); // c3 vs c4
    REQUIRE(locked_central_pawn_pairs(pos) == 3);
    REQUIRE(bishop_pair_closedness_value(pos) == kBishopPairPerLockedCentralPair * 3);
}

TEST_CASE("bishop_pair_closedness: a single bishop is not a pair -> zero",
          "[eval][bishop_pair_closedness]") {
    Position pos = empty_position();
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    lock(pos, 4, 3);
    REQUIRE(bishop_pair_closedness_value(pos) == Score{0, 0});
}

TEST_CASE("bishop_pair_closedness: locked flank pawns (a, b, g, h files) are not counted",
          "[eval][bishop_pair_closedness]") {
    Position pos = empty_position();
    give_white_pair(pos);
    lock(pos, 0, 3);
    lock(pos, 1, 3);
    lock(pos, 6, 3);
    lock(pos, 7, 3);
    REQUIRE(locked_central_pawn_pairs(pos) == 0);
    REQUIRE(bishop_pair_closedness_value(pos) == Score{0, 0});
}

TEST_CASE("bishop_pair_closedness: diagonally adjacent opposing pawns are not locked",
          "[eval][bishop_pair_closedness]") {
    Position pos = empty_position();
    give_white_pair(pos);
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    pos.place_piece(make_square(3, 4), Piece::BlackPawn); // d5
    REQUIRE(locked_central_pawn_pairs(pos) == 0);
    REQUIRE(bishop_pair_closedness_value(pos) == Score{0, 0});
}

TEST_CASE("bishop_pair_closedness: Black-only pair gets the mirror (negated) score",
          "[eval][bishop_pair_closedness]") {
    Position white_case = empty_position();
    give_white_pair(white_case);
    lock(white_case, 4, 3);

    Position black_case = empty_position();
    give_black_pair(black_case);
    lock(black_case, 4, 3);

    REQUIRE(bishop_pair_closedness_value(black_case) ==
            Score{0, 0} - bishop_pair_closedness_value(white_case));
}

TEST_CASE("bishop_pair_closedness: both sides with the pair cancel exactly",
          "[eval][bishop_pair_closedness]") {
    Position pos = empty_position();
    give_white_pair(pos);
    give_black_pair(pos);
    lock(pos, 3, 3);
    lock(pos, 4, 3);
    REQUIRE(locked_central_pawn_pairs(pos) == 2);
    REQUIRE(bishop_pair_closedness_value(pos) == Score{0, 0});
}
