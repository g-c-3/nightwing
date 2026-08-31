// tests/king_pawn_endgame_tests.cpp
//
// Unit tests for src/eval/king_pawn_endgame.h's king_pawn_endgame_value()
// (ROADMAP.md Phase 6's "King+pawn theory" item). Positions are built
// directly via Position::place_piece(), matching tests/endgame_tests.cpp's
// own style and its own empty_position() helper (re-declared locally
// here, per that file's own established per-file-not-shared convention
// for this exact helper).
//
// Squares below are written make_square(file, rank), both 0-indexed
// (file 0=a..7=h, rank 0=rank1..7=rank8) -- e.g. make_square(4, 4) is
// e5. Every position is hand-verified against classical KPK theory
// (CPW's own "Square Rule"/"Key Square"/"Opposition" articles) before
// being written as a TEST_CASE, not assumed to produce the "obviously
// expected" sign without checking the actual formula this file's own
// king_pawn_endgame.cpp implements.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/king_pawn_endgame.h"

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

TEST_CASE("king_pawn_endgame_value: not a KPK position at all (starting position) is Score{}",
          "[eval][king_pawn_endgame]") {
    REQUIRE(king_pawn_endgame_value(start_position()) == Score{});
}

TEST_CASE("king_pawn_endgame_value: bare king vs. bare king (no pawn at all) is Score{}",
          "[eval][king_pawn_endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    REQUIRE(king_pawn_endgame_value(pos) == Score{});
}

TEST_CASE("king_pawn_endgame_value: unstoppable pawn -- defending king far outside the square "
          "-- exact kUnstoppablePawnBonus for the attacker (White here)",
          "[eval][king_pawn_endgame]") {
    // White Ka1, Pe6 (rel. rank 5, not on the starting rank -- 2 pushes
    // to promote: e7, e8). Black Ka8, Black to move.
    // king_moves_to_promotion_sq = chebyshev(a8, e8) = 4.
    // defender_moves_available = 2 - 0 (Black, the defender, is to move
    // -- no attacker head-start subtraction) = 2. 4 > 2: king does not
    // catch the pawn -- unstoppable.
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 5), Piece::WhitePawn);
    pos.place_piece(make_square(0, 7), Piece::BlackKing);
    REQUIRE(king_pawn_endgame_value(pos) == kUnstoppablePawnBonus);
}

TEST_CASE("king_pawn_endgame_value: unstoppable pawn for BLACK produces the exact negated "
          "(White-relative) bonus -- confirms the sign convention",
          "[eval][king_pawn_endgame]") {
    // Mirror of the case above across the color boundary (ranks
    // flipped, colors swapped).
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(0, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 2), Piece::BlackPawn);
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    REQUIRE(king_pawn_endgame_value(pos) == -kUnstoppablePawnBonus);
}

TEST_CASE("king_pawn_endgame_value: rook pawn -- defending king catches per the rule of the "
          "square, but the a-file draws it regardless -- exact kRookPawnDrawishPenalty",
          "[eval][king_pawn_endgame]") {
    // White Ph1 (irrelevant, far away), Pa5 (rel. rank 4, not starting
    // rank -- 3 pushes to promote: a6,a7,a8). Black Kc8, White to move.
    // king_moves_to_promotion_sq = chebyshev(c8, a8) = 2.
    // defender_moves_available = 3 - 1 (White, the attacker, is to
    // move) = 2. 2 <= 2: king catches. Pawn is on the a-file: rook pawn.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(7, 0), Piece::WhiteKing);
    pos.place_piece(make_square(0, 4), Piece::WhitePawn);
    pos.place_piece(make_square(2, 7), Piece::BlackKing);
    REQUIRE(king_pawn_endgame_value(pos) == kRookPawnDrawishPenalty);
}

TEST_CASE("king_pawn_endgame_value: king catches, non-rook pawn, attacker holds a key square -- "
          "exact kKeySquareBonus regardless of opposition",
          "[eval][king_pawn_endgame]") {
    // White Ke6 (a key square -- see below), Pe4 (rel. rank 3, <= 4, so
    // key squares are 2 ranks ahead: rel. rank 5 = rank index 5 = rank
    // 6, files d/e/f -- e6 qualifies). Black Kg8, Black to move.
    // king_moves_to_promotion_sq = chebyshev(g8, e8) = 2.
    // defender_moves_available = (7-3) - 0 = 4 (Black, the defender, is
    // to move -- no subtraction). 2 <= 4: king catches. Not a rook
    // pawn. White king on e6 is exactly a key square.
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(4, 5), Piece::WhiteKing);
    pos.place_piece(make_square(4, 3), Piece::WhitePawn);
    pos.place_piece(make_square(6, 7), Piece::BlackKing);
    REQUIRE(king_pawn_endgame_value(pos) == kKeySquareBonus);
}

TEST_CASE("king_pawn_endgame_value: king catches, non-rook pawn, no key square held, defending "
          "king blockades in front with the opposition -- exact kOppositionDrawishPenalty",
          "[eval][king_pawn_endgame]") {
    // White Ke7 (NOT a key square for this pawn -- key squares are
    // d6/e6/f6), Pe4 (rel. rank 3). Black Ke5, White to move.
    // king_moves_to_promotion_sq = chebyshev(e5, e8) = 3.
    // defender_moves_available = (7-3) - 1 (White, the attacker, is to
    // move) = 3. 3 <= 3: king catches. Not a rook pawn. White king e7
    // isn't a key square (rank index 6, not 5). Black king e5 is
    // directly in front of the pawn (same file, ahead of it from
    // White's perspective) and stands in direct opposition to White's
    // king (same file, 2 ranks apart: e7/e5), with White (the
    // attacker) to move -- Black holds the opposition.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 6), Piece::WhiteKing);
    pos.place_piece(make_square(4, 3), Piece::WhitePawn);
    pos.place_piece(make_square(4, 4), Piece::BlackKing);
    REQUIRE(king_pawn_endgame_value(pos) == kOppositionDrawishPenalty);
}

TEST_CASE("king_pawn_endgame_value: king catches, non-rook pawn, no key square, defending king "
          "in front but WITHOUT the opposition (wrong side to move) -- no adjustment (Score{})",
          "[eval][king_pawn_endgame]") {
    // Identical squares to the opposition test above, but Black (the
    // defender) to move instead of White -- has_direct_opposition()'s
    // own "defender holds it only when the ATTACKER is to move"
    // condition now fails, so neither the key-square nor the
    // opposition branch fires; left as Score{}, deliberately not
    // guessing a direction (see king_pawn_endgame.cpp's own comment on
    // this fallthrough case).
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(4, 6), Piece::WhiteKing);
    pos.place_piece(make_square(4, 3), Piece::WhitePawn);
    pos.place_piece(make_square(4, 4), Piece::BlackKing);
    REQUIRE(king_pawn_endgame_value(pos) == Score{});
}

TEST_CASE("king_pawn_endgame_value: the starting-rank double-step adjustment is exactly what "
          "tips this position to unstoppable -- without it, the same defending king distance "
          "would still catch the pawn",
          "[eval][king_pawn_endgame]") {
    // White Ke1, Pe2 (rel. rank 1, the starting rank), White to move.
    // Black Ka3.
    // WITH the double-step adjustment (this file's own code):
    //   pawn_moves_to_promote = (7-1) - 1 = 5.
    //   defender_moves_available = 5 - 1 (White, the attacker, is to
    //   move) = 4.
    //   king_moves_to_promotion_sq = chebyshev(a3, e8) = max(4, 5) = 5.
    //   5 > 4: king does NOT catch -- unstoppable.
    // WITHOUT the adjustment (pawn_moves_to_promote would be 6):
    //   defender_moves_available would be 6 - 1 = 5, and 5 <= 5 WOULD
    //   catch -- the opposite classification. This exact boundary
    //   (king distance == 5) is what makes the test meaningful: it
    //   only passes if the double-step adjustment is genuinely applied,
    //   not merely present as dead code.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 1), Piece::WhitePawn);
    pos.place_piece(make_square(0, 2), Piece::BlackKing);
    REQUIRE(king_pawn_endgame_value(pos) == kUnstoppablePawnBonus);
}
