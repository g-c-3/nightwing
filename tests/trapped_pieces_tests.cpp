// tests/trapped_pieces_tests.cpp
//
// Unit tests for src/eval/trapped_pieces.h (ROADMAP.md Phase 5's
// "Trapped piece penalties" item). Positions are built directly via
// Position::place_piece(), matching every other eval/*_tests.cpp
// file's style, specifically so each test isolates
// trapped_piece_value() itself rather than relying on a real game
// position where every eval term interacts at once (eval_tests.cpp's
// job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "eval/score.h"
#include "eval/trapped_pieces.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so table-init state isn't shared across cases -- each
/// must initialize what it needs itself. trapped_piece_value() needs
/// both init_masks() (pawn_attacks()/knight_attacks()) and
/// init_magic_bitboards() (bishop_attacks()) -- see trapped_pieces.h's
/// own precondition comment.
void init_all() {
    init_masks();
    init_magic_bitboards();
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

TEST_CASE("trapped_piece_value: starting position is exactly balanced (in fact zero -- every "
          "minor piece has ordinary knight/bishop development squares available)",
          "[eval][trapped_pieces]") {
    init_all();
    REQUIRE(trapped_piece_value(start_position()) == Score{0, 0});
}

TEST_CASE("trapped_piece_value: a knight with zero safe squares scores exactly the knight "
          "penalty",
          "[eval][trapped_pieces]") {
    init_all();
    // White knight on a8 (corner): its only pseudo-attacks are b6 and
    // c7 (both empty, so ordinary mobility is nonzero). A black pawn on
    // a7 attacks b6 (pawn_attacks(White, b6) = {a7, c7}, so a black
    // pawn on a7 covers b6); a black pawn on d8 attacks c7
    // (pawn_attacks(White, c7) = {b8, d8}, so a black pawn on d8 covers
    // c7). With both of the knight's only two squares pawn-covered,
    // safe mobility is exactly zero.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 3), Piece::WhiteKing); // e4, out of the way
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8, out of the way
    pos.place_piece(make_square(0, 7), Piece::WhiteKnight); // a8
    pos.place_piece(make_square(0, 6), Piece::BlackPawn);   // a7
    pos.place_piece(make_square(3, 7), Piece::BlackPawn);   // d8

    REQUIRE(trapped_piece_value(pos) == kKnightTrappedPenalty);
}

TEST_CASE("trapped_piece_value: a bishop with zero safe squares scores exactly the bishop "
          "penalty, distinct from the knight penalty",
          "[eval][trapped_pieces]") {
    init_all();
    // White bishop on a8 (corner), sole diagonal b7-h1. A white pawn on
    // b7 blocks the diagonal entirely one square out -- ordinary
    // mobility is nonzero (b7 itself is occupied by an OWN pawn, so
    // it's excluded from pseudo-mobility already: bishop_attacks(a8,
    // occupied) with a blocker on b7 returns only {b7}, and {b7} minus
    // own-occupied is empty). That alone would already be zero safe
    // squares without needing any enemy pawn at all -- deliberately
    // using a same-color-pawn wall (not an enemy one) confirms the term
    // treats "no safe square" as "no safe square" regardless of WHY a
    // square is unavailable (own-occupied vs. enemy-pawn-attacked),
    // exactly as its own header comment's "safe mobility" definition
    // implies (attacks & ~own & ~hostile_pawn_attacks).
    Position pos = empty_position();
    pos.place_piece(make_square(4, 3), Piece::WhiteKing); // e4, out of the way
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8, out of the way
    pos.place_piece(make_square(0, 7), Piece::WhiteBishop); // a8
    pos.place_piece(make_square(1, 6), Piece::WhitePawn);   // b7, own pawn walls it in

    REQUIRE(trapped_piece_value(pos) == kBishopTrappedPenalty);
    REQUIRE(kBishopTrappedPenalty != kKnightTrappedPenalty);
}

TEST_CASE("trapped_piece_value: a knight with exactly one safe square scores exactly zero -- "
          "the boundary between trapped and merely restricted",
          "[eval][trapped_pieces]") {
    init_all();
    // Same a8-knight setup as the "zero safe squares" test, but with
    // the d8 black pawn removed -- c7 is no longer pawn-covered, so the
    // knight has exactly one safe square (c7) instead of zero. This is
    // deliberately NOT trapped under this term's "exactly zero" rule
    // (see trapped_pieces.h's header comment for why a nonzero-but-low
    // threshold was rejected in favor of the unambiguous zero case).
    Position pos = empty_position();
    pos.place_piece(make_square(4, 3), Piece::WhiteKing); // e4, out of the way
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8, out of the way
    pos.place_piece(make_square(0, 7), Piece::WhiteKnight); // a8
    pos.place_piece(make_square(0, 6), Piece::BlackPawn);   // a7, covers b6 only

    REQUIRE(trapped_piece_value(pos) == Score{0, 0});
}

TEST_CASE("trapped_piece_value: a trapped piece for Black scores the negated (White-relative) "
          "penalty",
          "[eval][trapped_pieces]") {
    init_all();
    // Mirror of the knight test across the board's centre, with colors
    // swapped: Black knight on a1 (corner), White pawns on a2 and d1
    // covering its only two squares (b3, c2). Confirms the White/Black
    // sign convention independent of the "zero safe squares" detection
    // itself, which the earlier tests already isolate.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 4), Piece::WhiteKing); // e5, out of the way
    pos.place_piece(make_square(4, 0), Piece::BlackKing); // e1, out of the way
    pos.place_piece(make_square(0, 0), Piece::BlackKnight); // a1
    pos.place_piece(make_square(0, 1), Piece::WhitePawn);   // a2, covers b3
    pos.place_piece(make_square(3, 0), Piece::WhitePawn);   // d1, covers c2

    REQUIRE(trapped_piece_value(pos) == -kKnightTrappedPenalty);
}
