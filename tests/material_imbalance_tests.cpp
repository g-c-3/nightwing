// tests/material_imbalance_tests.cpp
//
// Unit tests for src/eval/material_imbalance.h (ROADMAP.md Phase 5's
// "Material imbalance table" item). Positions are built directly via
// Position::place_piece(), matching every other eval/*_tests.cpp
// file's style, specifically so each test isolates
// material_imbalance_value() itself rather than relying on a real game
// position where every eval term interacts at once.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/material_imbalance.h"
#include "eval/score.h"

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

TEST_CASE("material_imbalance_value: starting position scores exactly zero -- full pawn count "
          "means zero pawn-count scaling regardless of either side's bishop/knight pair",
          "[eval][material_imbalance]") {
    REQUIRE(material_imbalance_value(start_position()) == Score{0, 0});
}

TEST_CASE("material_imbalance_value: White's bishop pair with every pawn off the board scores "
          "exactly 16 * kBishopPairPerMissingPawn",
          "[eval][material_imbalance]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    pos.place_piece(make_square(5, 0), Piece::WhiteBishop); // f1

    REQUIRE(material_imbalance_value(pos) == kBishopPairPerMissingPawn * kStartingTotalPawns);
}

TEST_CASE("material_imbalance_value: White's knight pair with every pawn off the board scores "
          "exactly 16 * kKnightPairPerMissingPawn (a penalty -- the constant itself is "
          "negative)",
          "[eval][material_imbalance]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight); // b1
    pos.place_piece(make_square(6, 0), Piece::WhiteKnight); // g1

    REQUIRE(material_imbalance_value(pos) == kKnightPairPerMissingPawn * kStartingTotalPawns);
    REQUIRE(material_imbalance_value(pos).mg < 0);
    REQUIRE(material_imbalance_value(pos).eg < 0);
}

TEST_CASE("material_imbalance_value: a single bishop (not a pair) scores exactly zero no matter "
          "how many pawns are missing",
          "[eval][material_imbalance]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1, alone -- not a pair

    REQUIRE(material_imbalance_value(pos) == Score{0, 0});
}

TEST_CASE("material_imbalance_value: scaling is linear in missing pawns -- half the pawns off "
          "the board scores exactly half of the all-pawns-off bishop-pair value",
          "[eval][material_imbalance]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    pos.place_piece(make_square(5, 0), Piece::WhiteBishop); // f1
    // 8 White pawns still on their starting rank, 0 Black pawns:
    // total pawns on board = 8, so missing_pawns = 16 - 8 = 8, exactly
    // half of the all-pawns-off case above.
    for (int file = 0; file < 8; ++file) {
        pos.place_piece(make_square(file, 1), Piece::WhitePawn);
    }

    REQUIRE(material_imbalance_value(pos) == kBishopPairPerMissingPawn * 8);
}

TEST_CASE("material_imbalance_value: Black's bishop pair with every pawn off the board scores "
          "the exact negation of the equivalent White case",
          "[eval][material_imbalance]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8
    pos.place_piece(make_square(5, 7), Piece::BlackBishop); // f8

    REQUIRE(material_imbalance_value(pos) == -(kBishopPairPerMissingPawn * kStartingTotalPawns));
}
