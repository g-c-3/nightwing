// tests/king_tropism_tests.cpp
//
// Unit tests for src/eval/king_tropism.h (ROADMAP.md Phase 5's "King
// tropism (piece proximity to enemy king in the attack)" item).
// Positions are built directly via Position::place_piece(), matching
// every other eval/*_tests.cpp file's style, specifically so each test
// isolates king_tropism_value() itself rather than relying on a real
// game position where every eval term interacts at once
// (eval_tests.cpp's job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/king_tropism.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so table-init state isn't shared across cases -- each
/// must initialize what it needs itself. Only init_masks() is needed
/// here (king_tropism_value() never touches a sliding-piece attack
/// table -- see king_tropism.h's own precondition comment).
void init_all() {
    init_masks();
}

/// Returns a fully empty position (no pieces, given side to move) --
/// same helper pattern as every other eval/*_tests.cpp file.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("king_tropism_value: starting position is exactly balanced", "[eval][king_tropism]") {
    init_all();
    // Full board mirror symmetry (same files, reflected ranks, same
    // piece types on each side) means every White piece's distance to
    // the Black king exactly matches its mirrored Black counterpart's
    // distance to the White king -- the whole term cancels regardless
    // of the exact constants involved.
    REQUIRE(king_tropism_value(start_position()) == Score{0, 0});
}

TEST_CASE("king_tropism_value: a queen closer to the enemy king scores more than the same "
          "queen further away",
          "[eval][king_tropism]") {
    init_all();
    // Black king on e8. White queen on e5 is a Chebyshev distance of 3
    // away (same file, |7-4|=3 ranks); White queen on e1 is a distance
    // of 7 away (same file, |7-0|=7 ranks -- the maximum possible, and
    // exactly kTropismMaxDistance, so it contributes zero).
    Position close = empty_position();
    close.place_piece(make_square(0, 0), Piece::WhiteKing);  // a1
    close.place_piece(make_square(4, 7), Piece::BlackKing);  // e8
    close.place_piece(make_square(4, 4), Piece::WhiteQueen); // e5, distance 3

    Position far = empty_position();
    far.place_piece(make_square(0, 0), Piece::WhiteKing);
    far.place_piece(make_square(4, 7), Piece::BlackKing);
    far.place_piece(make_square(4, 0), Piece::WhiteQueen); // e1, distance 7

    // proximity = kTropismMaxDistance(7) - distance(3) = 4;
    // units = kQueenTropismWeight(4) * 4 = 16.
    REQUIRE(king_tropism_value(close) == kTropismUnitBonus * 16);
    REQUIRE(king_tropism_value(far) == Score{0, 0});
}

TEST_CASE("king_tropism_value: a queen at a given distance scores exactly 4x what a knight at "
          "the same distance scores",
          "[eval][king_tropism]") {
    init_all();
    // Same e5-square, same distance-3-from-e8 setup as the previous
    // test, but with a knight instead of a queen -- isolates the
    // per-piece-type weight ratio (Queen=4, Knight=1) directly.
    Position knight_pos = empty_position();
    knight_pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    knight_pos.place_piece(make_square(4, 7), Piece::BlackKing);
    knight_pos.place_piece(make_square(4, 4), Piece::WhiteKnight); // e5, distance 3

    Position queen_pos = empty_position();
    queen_pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    queen_pos.place_piece(make_square(4, 7), Piece::BlackKing);
    queen_pos.place_piece(make_square(4, 4), Piece::WhiteQueen); // e5, distance 3

    REQUIRE(king_tropism_value(queen_pos) == king_tropism_value(knight_pos) * 4);
}

TEST_CASE("king_tropism_value: a piece at the maximum possible distance (opposite board "
          "corners) contributes exactly zero",
          "[eval][king_tropism]") {
    init_all();
    // a1 to h8 is a Chebyshev distance of exactly 7 -- both the file
    // and rank differences are 7 -- the maximum possible on an 8x8
    // board, and exactly kTropismMaxDistance, so proximity is exactly
    // zero.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKnight); // a1
    pos.place_piece(make_square(4, 3), Piece::WhiteKing);   // e4, out of the way
    pos.place_piece(make_square(7, 7), Piece::BlackKing);   // h8

    REQUIRE(king_tropism_value(pos) == Score{0, 0});
}

TEST_CASE("king_tropism_value: two different pieces' contributions stack additively",
          "[eval][king_tropism]") {
    init_all();
    // Black king on e8. White knight on e5 (distance 3, as in the
    // earlier tests) plus a White rook on d6 (file 3, rank index 5 --
    // |4-3|=1 file, |7-5|=2 ranks, Chebyshev distance 2) together --
    // confirms the total equals the sum of each piece's own individual
    // contribution rather than double-counting or interacting.
    Position knight_only = empty_position();
    knight_only.place_piece(make_square(0, 0), Piece::WhiteKing);
    knight_only.place_piece(make_square(4, 7), Piece::BlackKing);
    knight_only.place_piece(make_square(4, 4), Piece::WhiteKnight); // e5, distance 3

    Position both = empty_position();
    both.place_piece(make_square(0, 0), Piece::WhiteKing);
    both.place_piece(make_square(4, 7), Piece::BlackKing);
    both.place_piece(make_square(4, 4), Piece::WhiteKnight); // e5, distance 3
    both.place_piece(make_square(3, 5), Piece::WhiteRook);   // d6, distance 2

    // Knight: proximity = 7-3 = 4, units = 1*4 = 4.
    // Rook: proximity = 7-2 = 5, units = 2*5 = 10.
    const Score expected_knight_only = kTropismUnitBonus * 4;
    const Score expected_rook_only = kTropismUnitBonus * 10;

    REQUIRE(king_tropism_value(knight_only) == expected_knight_only);
    REQUIRE(king_tropism_value(both) == expected_knight_only + expected_rook_only);
}
