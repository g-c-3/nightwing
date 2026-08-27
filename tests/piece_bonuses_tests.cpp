// tests/piece_bonuses_tests.cpp
//
// Unit tests for src/eval/piece_bonuses.h (ROADMAP.md Phase 5's
// "Bishop pair, rook on open/semi-open file, rook on 7th rank" item).
// Positions are built directly via Position::place_piece(), matching
// mobility_tests.cpp's/king_safety_tests.cpp's style, specifically so
// each test isolates piece_bonus_value() itself rather than relying on
// a real game position where every eval term interacts at once
// (eval_tests.cpp's job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/piece_bonuses.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so table-init state isn't shared across cases -- each
/// must initialize what it needs itself. Matches king_safety_tests.cpp's
/// convention. Only init_masks() is needed here (piece_bonus_value()
/// never touches a sliding-piece attack table -- see piece_bonuses.h's
/// own precondition comment), unlike king_safety_tests.cpp/
/// mobility_tests.cpp which also need init_magic_bitboards().
void init_all() {
    init_masks();
}

/// Returns a fully empty position (no pieces, given side to move) --
/// same helper pattern as mobility_tests.cpp/king_safety_tests.cpp.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("piece_bonus_value: starting position is exactly balanced", "[eval][piece_bonuses]") {
    init_all();
    // Mirror-symmetric between colors: both sides have an intact bishop
    // pair (cancels), both a1/h1-style rooks sit behind their own
    // b/g-pawns on the a/h files (closed, no open/semi-open bonus) and
    // on rank 1/rank 8 respectively, neither of which is either side's
    // own relative 7th rank.
    REQUIRE(piece_bonus_value(start_position()) == Score{0, 0});
}

TEST_CASE("piece_bonus_value: bishop pair outscores a single bishop", "[eval][piece_bonuses]") {
    init_all();
    Position pair = empty_position();
    pair.place_piece(make_square(4, 0), Piece::WhiteKing);   // e1
    pair.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    pair.place_piece(make_square(5, 0), Piece::WhiteBishop); // f1
    pair.place_piece(make_square(4, 7), Piece::BlackKing);   // e8

    Position single = empty_position();
    single.place_piece(make_square(4, 0), Piece::WhiteKing);   // e1
    single.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    single.place_piece(make_square(4, 7), Piece::BlackKing);   // e8

    REQUIRE(piece_bonus_value(pair).mg > piece_bonus_value(single).mg);
    REQUIRE(piece_bonus_value(pair).eg > piece_bonus_value(single).eg);
}

TEST_CASE("piece_bonus_value: a rook on an open file outscores one on a semi-open file, "
          "which outscores one on a closed file",
          "[eval][piece_bonuses]") {
    init_all();
    // Rook fixed on a1 (file 0) in every config -- only the a-file's own
    // pawn occupancy varies, isolating the file-openness component.
    // Neither king is placed on a rook-relevant file/rank combination
    // that would introduce a 7th-rank bonus for this comparison.
    Position closed = empty_position();
    closed.place_piece(make_square(0, 0), Piece::WhiteRook); // a1
    closed.place_piece(make_square(0, 1), Piece::WhitePawn); // a2 -- own pawn
    closed.place_piece(make_square(4, 0), Piece::WhiteKing);
    closed.place_piece(make_square(4, 7), Piece::BlackKing);

    Position semi_open = empty_position();
    semi_open.place_piece(make_square(0, 0), Piece::WhiteRook); // a1
    semi_open.place_piece(make_square(0, 5), Piece::BlackPawn); // a6 -- enemy pawn only
    semi_open.place_piece(make_square(4, 0), Piece::WhiteKing);
    semi_open.place_piece(make_square(4, 7), Piece::BlackKing);

    Position fully_open = empty_position();
    fully_open.place_piece(make_square(0, 0), Piece::WhiteRook); // a1, no a-file pawn at all
    fully_open.place_piece(make_square(4, 0), Piece::WhiteKing);
    fully_open.place_piece(make_square(4, 7), Piece::BlackKing);

    const int closed_mg = piece_bonus_value(closed).mg;
    const int semi_open_mg = piece_bonus_value(semi_open).mg;
    const int fully_open_mg = piece_bonus_value(fully_open).mg;

    REQUIRE(fully_open_mg > semi_open_mg);
    REQUIRE(semi_open_mg > closed_mg);
}

TEST_CASE("piece_bonus_value: a rook on the 7th rank is rewarded", "[eval][piece_bonuses]") {
    init_all();
    Position on_seventh = empty_position();
    on_seventh.place_piece(make_square(0, 6), Piece::WhiteRook); // a7
    on_seventh.place_piece(make_square(4, 0), Piece::WhiteKing);
    on_seventh.place_piece(make_square(4, 7), Piece::BlackKing);
    on_seventh.place_piece(make_square(3, 6), Piece::BlackPawn); // d7 -- off the a-file, so the
                                                                  // a-file stays fully open in
                                                                  // both configs below, isolating
                                                                  // the 7th-rank component alone.

    Position off_seventh = empty_position();
    off_seventh.place_piece(make_square(0, 3), Piece::WhiteRook); // a4 -- same file, not 7th rank
    off_seventh.place_piece(make_square(4, 0), Piece::WhiteKing);
    off_seventh.place_piece(make_square(4, 7), Piece::BlackKing);
    off_seventh.place_piece(make_square(3, 6), Piece::BlackPawn); // d7

    REQUIRE(piece_bonus_value(on_seventh).mg > piece_bonus_value(off_seventh).mg);
    REQUIRE(piece_bonus_value(on_seventh).eg > piece_bonus_value(off_seventh).eg);
}

TEST_CASE("piece_bonus_value: open-file and 7th-rank bonuses stack additively for the same rook",
          "[eval][piece_bonuses]") {
    init_all();
    // A single rook that is BOTH on an open file AND on the 7th rank at
    // once should score the sum of both bonuses, not just the larger of
    // the two -- confirms the two components are independent additive
    // terms within the same rook's own contribution, not a max()-style
    // combination.
    Position both = empty_position();
    both.place_piece(make_square(0, 6), Piece::WhiteRook); // a7 -- open a-file, 7th rank
    both.place_piece(make_square(4, 0), Piece::WhiteKing);
    both.place_piece(make_square(4, 7), Piece::BlackKing);

    Position open_only = empty_position();
    open_only.place_piece(make_square(0, 3), Piece::WhiteRook); // a4 -- open a-file, not 7th rank
    open_only.place_piece(make_square(4, 0), Piece::WhiteKing);
    open_only.place_piece(make_square(4, 7), Piece::BlackKing);

    const Score both_score = piece_bonus_value(both);
    const Score open_only_score = piece_bonus_value(open_only);
    const Score expected = open_only_score + kRookOnSeventhBonus;

    REQUIRE(both_score == expected);
}
