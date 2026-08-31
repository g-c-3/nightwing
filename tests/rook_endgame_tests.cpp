// tests/rook_endgame_tests.cpp
//
// Unit tests for src/eval/rook_endgame.h's rook_endgame_value()
// (ROADMAP.md Phase 6's "Rook endgame patterns" item). Same style as
// tests/king_pawn_endgame_tests.cpp: positions built directly via
// Position::place_piece(), squares written make_square(file, rank)
// (both 0-indexed), every scenario independently verified against the
// exact C++ branch logic via a Python simulation before being written
// here as a TEST_CASE.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/rook_endgame.h"

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

TEST_CASE("rook_endgame_value: not a RookEndgame position at all (starting position) is Score{}",
          "[eval][rook_endgame]") {
    REQUIRE(rook_endgame_value(start_position()) == Score{});
}

TEST_CASE("rook_endgame_value: bare kings and rooks, no pawns at all -- valid RookEndgame "
          "bucket, but nothing for Tarrasch's Rule or Lucena/Philidor to match -- Score{}",
          "[eval][rook_endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(7, 7), Piece::BlackRook);
    REQUIRE(rook_endgame_value(pos) == Score{});
}

TEST_CASE("rook_endgame_value: recognized Lucena position -- exact kLucenaWinBonus for the "
          "attacker (White here)",
          "[eval][rook_endgame]") {
    // White Pe7 (rel. rank 6, one push from promoting), White Kd8
    // (chebyshev(d8, e8) = 1 -- right beside the promotion square).
    // Black Ka1 (chebyshev(a1, e8) = 7 -- far too cut off to help).
    // Not a rook pawn. Every Lucena condition met.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 6), Piece::WhitePawn);
    pos.place_piece(make_square(3, 7), Piece::WhiteKing);
    pos.place_piece(make_square(0, 0), Piece::BlackKing);
    pos.place_piece(make_square(7, 0), Piece::BlackRook);
    pos.place_piece(make_square(6, 0), Piece::WhiteRook);
    REQUIRE(rook_endgame_value(pos) == kLucenaWinBonus);
}

TEST_CASE("rook_endgame_value: recognized Philidor position -- exact kPhilidorDrawPenalty "
          "against the attacker (White here)",
          "[eval][rook_endgame]") {
    // White Pe5 (rel. rank 4, hasn't crossed to the 6th yet). Black Ke8
    // (chebyshev(e8, e8) = 0 -- blockading right at the promotion
    // square). Black Ra6 (rank index 5 -- the cutting-off rank for a
    // White attacker). White Ka1, Rh1 (irrelevant, far away).
    Position pos = empty_position();
    pos.place_piece(make_square(4, 4), Piece::WhitePawn);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 5), Piece::BlackRook);
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    pos.place_piece(make_square(7, 0), Piece::WhiteRook);
    REQUIRE(rook_endgame_value(pos) == kPhilidorDrawPenalty);
}

TEST_CASE("rook_endgame_value: Tarrasch's Rule -- rook behind its own passed pawn -- exact "
          "kRookBehindOwnPassedPawnBonus, White-relative positive",
          "[eval][rook_endgame]") {
    // White Pa6 (passed -- no Black pawn on the a/b files ahead of it).
    // White Ra1: same file, rank 0 < pawn's rank 5 -- behind it (White
    // promotes upward). Two pawns total on the board (this one plus
    // Black's e5, added purely so total_pawns != 1 and the Lucena/
    // Philidor path is skipped entirely, isolating this test to
    // Tarrasch's Rule alone).
    Position pos = empty_position();
    pos.place_piece(make_square(0, 5), Piece::WhitePawn);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(7, 7), Piece::BlackRook);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 4), Piece::BlackPawn);
    REQUIRE(rook_endgame_value(pos) == kRookBehindOwnPassedPawnBonus);
}

TEST_CASE("rook_endgame_value: Tarrasch's Rule -- rook behind an ENEMY passed pawn -- exact "
          "kRookBehindEnemyPassedPawnBonus, White-relative positive (White's rook is the one "
          "behind Black's pawn here)",
          "[eval][rook_endgame]") {
    // Black Pa3 (passed for Black -- no White pawn on the a/b files
    // ahead of it, i.e. on lower ranks). White Ra8: same file, rank 7
    // > pawn's rank 2 -- behind it from BLACK's own promotion
    // direction (Black promotes downward). White's extra pawn on e5
    // again just keeps total_pawns at 2 to skip the Lucena/Philidor
    // path.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 2), Piece::BlackPawn);
    pos.place_piece(make_square(0, 7), Piece::WhiteRook);
    pos.place_piece(make_square(7, 0), Piece::BlackRook);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 4), Piece::WhitePawn);
    REQUIRE(rook_endgame_value(pos) == kRookBehindEnemyPassedPawnBonus);
}

TEST_CASE("rook_endgame_value: single pawn, but geometry matches neither Lucena nor Philidor, "
          "and no rook stands behind the pawn either -- Score{}",
          "[eval][rook_endgame]") {
    // White Pe5 (rel. rank 4 -- too early for Lucena's rel. rank 6
    // requirement). Black king far from the promotion square (a8,
    // chebyshev(a8, e8) = 4 > 1) -- fails Philidor's blockade
    // requirement too. Neither rook stands on the e-file, so Tarrasch's
    // Rule doesn't fire either.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 4), Piece::WhitePawn);
    pos.place_piece(make_square(3, 0), Piece::WhiteKing);
    pos.place_piece(make_square(0, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::BlackRook);
    pos.place_piece(make_square(7, 0), Piece::WhiteRook);
    REQUIRE(rook_endgame_value(pos) == Score{});
}
