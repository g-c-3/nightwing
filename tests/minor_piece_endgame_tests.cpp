// tests/minor_piece_endgame_tests.cpp
//
// Unit tests for src/eval/minor_piece_endgame.h's
// minor_piece_endgame_value() (ROADMAP.md Phase 6's "Minor piece
// endgames" item). Same style as tests/king_pawn_endgame_tests.cpp and
// tests/rook_endgame_tests.cpp: positions built directly via
// Position::place_piece(), squares written make_square(file, rank)
// (both 0-indexed), every scenario independently verified against the
// exact C++ branch logic via a Python simulation before being written
// here as a TEST_CASE. A reminder worth stating explicitly, since it
// tripped up this file's own first draft: a1 and h8 are BOTH dark
// squares (opposite corners always share a color on a real board);
// a8 and h1 are both light.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/minor_piece_endgame.h"

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

TEST_CASE("minor_piece_endgame_value: not a recognized bucket at all (starting position) is "
          "Score{}",
          "[eval][minor_piece_endgame]") {
    REQUIRE(minor_piece_endgame_value(start_position()) == Score{});
}

TEST_CASE("minor_piece_endgame_value: KBPK -- wrong (light-squared) bishop for the dark h8 "
          "corner, defending king close enough to reach it -- exact "
          "kWrongBishopCornerDrawPenalty against the attacker (White here)",
          "[eval][minor_piece_endgame]") {
    // White Ph6 (rel. rank 5), White Bb1 (light square -- h8 is dark,
    // so this is the WRONG bishop). Black Kg8 (chebyshev(g8, h8) = 1 --
    // close enough to reach the corner in time per the rule of the
    // square). White to move.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(7, 5), Piece::WhitePawn);
    pos.place_piece(make_square(1, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(6, 7), Piece::BlackKing);
    REQUIRE(minor_piece_endgame_value(pos) == kWrongBishopCornerDrawPenalty);
}

TEST_CASE("minor_piece_endgame_value: KBPK -- RIGHT (dark-squared) bishop for the dark h8 "
          "corner -- no adjustment (Score{}) regardless of the defending king's position",
          "[eval][minor_piece_endgame]") {
    // Identical to the test above except White's bishop is on c1 (dark
    // square -- matches h8) instead of b1.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(7, 5), Piece::WhitePawn);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(6, 7), Piece::BlackKing);
    REQUIRE(minor_piece_endgame_value(pos) == Score{});
}

TEST_CASE("minor_piece_endgame_value: KBPK -- wrong bishop, but the defending king is too far "
          "to reach the corner in time -- no adjustment (Score{}), an ordinary win",
          "[eval][minor_piece_endgame]") {
    // Same wrong (light) bishop as the first test, but Black King on
    // a1 instead of g8 -- chebyshev(a1, h8) = 7, far too slow.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(7, 5), Piece::WhitePawn);
    pos.place_piece(make_square(1, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(0, 0), Piece::BlackKing);
    REQUIRE(minor_piece_endgame_value(pos) == Score{});
}

TEST_CASE("minor_piece_endgame_value: KBPK -- wrong bishop for BLACK's a-file pawn(s) (a1 is "
          "also dark), defending White king close enough -- exact NEGATED "
          "kWrongBishopCornerDrawPenalty (White-relative, since Black is the attacker here)",
          "[eval][minor_piece_endgame]") {
    // Black Pa3 (rel. rank, from Black's perspective, 5), Black Bc8
    // (light square -- a1 is dark, so this is the WRONG bishop). White
    // Kb1 (chebyshev(b1, a1) = 1 -- close enough). Black to move.
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 2), Piece::BlackPawn);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop);
    pos.place_piece(make_square(1, 0), Piece::WhiteKing);
    REQUIRE(minor_piece_endgame_value(pos) == -kWrongBishopCornerDrawPenalty);
}

TEST_CASE("minor_piece_endgame_value: KBPK -- a SECOND pawn on a different (non-rook) file "
          "rules the fortress out entirely -- no adjustment (Score{}) even with an otherwise "
          "wrong bishop and a close defending king",
          "[eval][minor_piece_endgame]") {
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(7, 5), Piece::WhitePawn);
    pos.place_piece(make_square(3, 5), Piece::WhitePawn); // e6 -- not a rook file
    pos.place_piece(make_square(1, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(6, 7), Piece::BlackKing);
    REQUIRE(minor_piece_endgame_value(pos) == Score{});
}

TEST_CASE("minor_piece_endgame_value: OppositeColoredBishops -- White two pawns up -- exact "
          "kOCBDrawishPenaltyPerExtraPawn times 2, against White",
          "[eval][minor_piece_endgame]") {
    // White 4 pawns, Black 2 pawns -- diff = 2.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1 -- dark
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8 -- light
    pos.place_piece(make_square(0, 1), Piece::WhitePawn);
    pos.place_piece(make_square(1, 1), Piece::WhitePawn);
    pos.place_piece(make_square(5, 1), Piece::WhitePawn);
    pos.place_piece(make_square(6, 1), Piece::WhitePawn);
    pos.place_piece(make_square(0, 6), Piece::BlackPawn);
    pos.place_piece(make_square(1, 6), Piece::BlackPawn);
    REQUIRE(minor_piece_endgame_value(pos) == kOCBDrawishPenaltyPerExtraPawn * 2);
}

TEST_CASE("minor_piece_endgame_value: OppositeColoredBishops -- equal pawn counts -- no "
          "adjustment (Score{}), not a flat always-on bonus",
          "[eval][minor_piece_endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1 -- dark
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8 -- light
    pos.place_piece(make_square(0, 1), Piece::WhitePawn);
    pos.place_piece(make_square(0, 6), Piece::BlackPawn);
    REQUIRE(minor_piece_endgame_value(pos) == Score{});
}

TEST_CASE("minor_piece_endgame_value: KnightVsBishop -- a mutually blocked pawn pair favors "
          "White's knight -- exact kKnightClosedPositionBonusPerBlockedPawn times 2 (both pawns "
          "in the blocked pair count), White-relative positive",
          "[eval][minor_piece_endgame]") {
    // White Pe4, Black Pe5 -- White's e4 is blocked by Black's e5, and
    // Black's e5 is blocked by White's e4: both count, blocked_pawns
    // == 2, open_pawns == 0 (total 2 pawns, both blocked).
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop);
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    pos.place_piece(make_square(4, 4), Piece::BlackPawn); // e5
    REQUIRE(minor_piece_endgame_value(pos) == kKnightClosedPositionBonusPerBlockedPawn * 2);
}

TEST_CASE("minor_piece_endgame_value: KnightVsBishop -- unblocked pawns favor White's bishop -- "
          "exact kBishopOpenPositionBonusPerOpenPawn times 2, White-relative positive",
          "[eval][minor_piece_endgame]") {
    // White Pe4, Black Pd5 -- neither blocks the other (different
    // files): blocked_pawns == 0, open_pawns == 2.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(1, 7), Piece::BlackKnight);
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    pos.place_piece(make_square(3, 4), Piece::BlackPawn); // d5
    REQUIRE(minor_piece_endgame_value(pos) == kBishopOpenPositionBonusPerOpenPawn * 2);
}
