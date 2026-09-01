// tests/basic_mates_tests.cpp
//
// Unit tests for src/eval/basic_mates.h's basic_mate_value()
// (ROADMAP.md Phase 6's final item, "Hand-built base heuristics
// carried over" -- the KRK/KBNK clauses specifically). Same style as
// the other Phase 6 test files: positions built directly via
// Position::place_piece(), squares written make_square(file, rank)
// (both 0-indexed), every scenario independently verified against the
// exact C++ branch logic via a Python simulation before being written
// here as a TEST_CASE.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/basic_mates.h"

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

TEST_CASE("basic_mate_value: not a KRK or KBNK position at all (starting position) is Score{}",
          "[eval][basic_mates]") {
    REQUIRE(basic_mate_value(start_position()) == Score{});
}

TEST_CASE("basic_mate_value: KRK -- exact formula value for a defending king already in the "
          "corner with the attacking king nearby",
          "[eval][basic_mates]") {
    // White Kb6, Ra1, Black Ka8. edge_push_score(a8) = |0-7|+|14-7| = 14
    // (a corner, the maximum). chebyshev(b6, a8) = 2, so proximity_term
    // = 7 - 2 = 5. score = kKRKEdgePushWeight*14 + kKRKKingProximityWeight*5
    // = 6*14 + 10*5 = 134.
    Position pos = empty_position();
    pos.place_piece(make_square(1, 5), Piece::WhiteKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(0, 7), Piece::BlackKing);
    REQUIRE(basic_mate_value(pos) == Score{134, 134});
}

TEST_CASE("basic_mate_value: KRK -- a defending king pushed to the corner scores strictly "
          "higher (better for the attacker) than the same king near the center, all else equal",
          "[eval][basic_mates]") {
    Position corner_pos = empty_position();
    corner_pos.place_piece(make_square(1, 5), Piece::WhiteKing);
    corner_pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    corner_pos.place_piece(make_square(0, 7), Piece::BlackKing);

    Position center_pos = empty_position();
    center_pos.place_piece(make_square(1, 5), Piece::WhiteKing);
    center_pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    center_pos.place_piece(make_square(3, 3), Piece::BlackKing); // d4, near center
    REQUIRE(basic_mate_value(corner_pos).eg > basic_mate_value(center_pos).eg);
}

TEST_CASE("basic_mate_value: KRK -- attacker is Black produces the exact negated (White-"
          "relative) value, confirming the sign convention",
          "[eval][basic_mates]") {
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(1, 5), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::BlackRook);
    pos.place_piece(make_square(0, 7), Piece::WhiteKing);
    REQUIRE(basic_mate_value(pos) == Score{-134, -134});
}

TEST_CASE("basic_mate_value: KBNK -- exact formula value for a defending king near the corner "
          "matching the attacking bishop's own square color",
          "[eval][basic_mates]") {
    // White Bc1 (dark square), Nb1, Kf6. Black Kg7. c1's dark corners
    // are a1 and h8. chebyshev(g7, a1) = 6, chebyshev(g7, h8) = 1, so
    // dist_to_correct_corner = 1, corner_term = 7 - 1 = 6.
    // edge_push_score(g7) = |12-7|+|12-7| = 10. chebyshev(f6, g7) = 1,
    // so proximity_term = 6.
    // score = kKBNKCornerColorWeight*6 + kKBNKEdgePushWeight*10 +
    //         kKBNKKingProximityWeight*6 = 14*6 + 4*10 + 8*6 = 172.
    Position pos = empty_position();
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(5, 5), Piece::WhiteKing);
    pos.place_piece(make_square(6, 6), Piece::BlackKing);
    REQUIRE(basic_mate_value(pos) == Score{172, 172});
}

TEST_CASE("basic_mate_value: KBNK -- a defending king near the WRONG-colored corner scores "
          "strictly lower on the corner term than the same king near the correct one, all else "
          "equal -- the defining KBNK distinction",
          "[eval][basic_mates]") {
    // Same dark-squared bishop (c1) as the test above. Black king at
    // a8 (a LIGHT corner -- the wrong color for this bishop) instead
    // of g7 (near h8, a dark corner).
    Position wrong_corner_pos = empty_position();
    wrong_corner_pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    wrong_corner_pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    wrong_corner_pos.place_piece(make_square(5, 5), Piece::WhiteKing);
    wrong_corner_pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8

    Position right_corner_pos = empty_position();
    right_corner_pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    right_corner_pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    right_corner_pos.place_piece(make_square(5, 5), Piece::WhiteKing);
    right_corner_pos.place_piece(make_square(6, 6), Piece::BlackKing); // g7, near h8

    REQUIRE(basic_mate_value(right_corner_pos).eg > basic_mate_value(wrong_corner_pos).eg);
}

TEST_CASE("basic_mate_value: KBNK -- attacker is Black produces the exact negated (White-"
          "relative) value, confirming the sign convention",
          "[eval][basic_mates]") {
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8 -- light square
    pos.place_piece(make_square(1, 7), Piece::BlackKnight);
    pos.place_piece(make_square(5, 2), Piece::BlackKing);
    pos.place_piece(make_square(6, 1), Piece::WhiteKing); // g2, near h1 (light corner)
    // Mirror of the "right corner" case above across the color
    // boundary -- verifying only the sign, not re-deriving the exact
    // magnitude a third time (already established by the two tests
    // above).
    REQUIRE(basic_mate_value(pos).eg < 0);
}
