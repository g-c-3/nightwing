// tests/space_tests.cpp
//
// Unit tests for src/eval/space.h (ROADMAP.md Phase 5's "Space
// evaluation" item). Positions are built directly via
// Position::place_piece(), matching every other eval/*_tests.cpp
// file's style, specifically so each test isolates space_value() itself
// rather than relying on a real game position where every eval term
// interacts at once (eval_tests.cpp's job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/space.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so table-init state isn't shared across cases -- each
/// must initialize what it needs itself. Only init_masks() is needed
/// here (space_value() never touches a sliding-piece attack table --
/// see space.h's own precondition comment).
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

TEST_CASE("space_value: a bare board (kings only) is exactly balanced", "[eval][space]") {
    init_all();
    // With no pawns anywhere, every one of the 12 squares (c/d/e/f
    // files x 3 relative ranks) in each side's own space zone is both
    // pawn-free and unattacked -- both sides score the maximum 12 *
    // kSpaceSquareBonus, which cancels exactly White-minus-Black.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8

    REQUIRE(space_value(pos) == Score{0, 0});
}

TEST_CASE("space_value: starting position is exactly balanced", "[eval][space]") {
    init_all();
    // Each side's own back-rank-adjacent zone rank (White's rank 2,
    // Black's rank 7) is fully occupied by that side's own pawns --
    // 4 of the zone's 12 squares (the c/d/e/f files on that rank) are
    // disqualified by own-pawn occupancy, leaving 8 safe squares per
    // side (no pawn is yet far enough advanced to attack into the
    // other side's zone) -- 8 == 8 cancels exactly, same as the
    // bare-board case above, just via a different route.
    REQUIRE(space_value(start_position()) == Score{0, 0});
}

TEST_CASE("space_value: a pawn occupying a space-zone square removes exactly one square from "
          "that side's own count",
          "[eval][space]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(3, 2), Piece::WhitePawn); // d3 -- inside White's own zone

    // White's own count drops from 12 to 11 (d3 itself, no other
    // square -- a pawn only occupies the one square it stands on);
    // Black's count is untouched (d3 doesn't lie within Black's own
    // zone, and pawn occupancy alone doesn't attack anything).
    REQUIRE(space_value(pos) == Score{-2, 0});
}

TEST_CASE("space_value: an enemy pawn attacking an EMPTY space-zone square still disqualifies "
          "it",
          "[eval][space]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    // b3 (file 1) is OUTSIDE both sides' own space zones (zones only
    // span the c-f files) -- placing the attacking pawn here isolates
    // this test to the attack mechanism alone, with no side-effect on
    // Black's own occupancy count the way a same-zone attacker would
    // have. b3 attacks a2 (outside White's zone entirely, file a) and
    // c2 (file 2, rank index 1 -- inside White's zone), so exactly one
    // White zone square is disqualified.
    pos.place_piece(make_square(1, 2), Piece::BlackPawn); // b3

    REQUIRE(space_value(pos) == Score{-2, 0});
}

TEST_CASE("space_value: an own-pawn-occupied square and an enemy-attacked square stack "
          "additively",
          "[eval][space]") {
    init_all();
    // Combines the previous two tests' two independent mechanisms in
    // one position -- confirms the two effects add rather than
    // interact or double-count.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(3, 2), Piece::WhitePawn); // d3
    pos.place_piece(make_square(1, 2), Piece::BlackPawn); // b3, attacks c2

    REQUIRE(space_value(pos) == Score{-4, 0});
}
