// tests/king_safety_tests.cpp
//
// Unit tests for src/eval/king_safety.h (ROADMAP.md Phase 5's "King
// safety" item). Positions are built directly via
// Position::place_piece(), matching mobility_tests.cpp's/
// pawns_tests.cpp's style, specifically so each test isolates
// king_safety_value() itself rather than relying on a real game
// position where every eval term interacts at once (eval_tests.cpp's
// job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "eval/king_safety.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so magic-bitboard/attack tables aren't shared across
/// cases -- each must initialize them itself. Matches mobility_tests.cpp's/
/// search_tests.cpp's convention exactly (see either for why).
void init_all() {
    init_masks();
    init_magic_bitboards();
}

/// Returns a fully empty position (no pieces, given side to move) --
/// same helper pattern as mobility_tests.cpp/pawns_tests.cpp.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("king_safety_value: starting position is exactly balanced", "[eval][king_safety]") {
    init_all();
    // Mirror-symmetric between colors, same reasoning as
    // mobility_value()'s own starting-position test (mobility_tests.cpp)
    // -- both kings have an identical, intact 3-pawn shield (d/e/f
    // pawns each still on their own second rank), identical file
    // openness (none of d/e/f is open OR semi-open, since both sides
    // still have all three pawns), and no piece on the board is close
    // enough yet to attack either king's zone at all.
    REQUIRE(king_safety_value(start_position()) == Score{0, 0});
}

TEST_CASE("king_safety_value: an intact pawn shield outscores an otherwise-identical bare king",
          "[eval][king_safety]") {
    init_all();
    Position shielded = empty_position();
    shielded.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    shielded.place_piece(make_square(3, 1), Piece::WhitePawn); // d2
    shielded.place_piece(make_square(4, 1), Piece::WhitePawn); // e2
    shielded.place_piece(make_square(5, 1), Piece::WhitePawn); // f2
    shielded.place_piece(make_square(7, 7), Piece::BlackKing); // h8, out of the way

    Position bare = empty_position();
    bare.place_piece(make_square(4, 0), Piece::WhiteKing); // e1, no pawns anywhere
    bare.place_piece(make_square(7, 7), Piece::BlackKing); // h8

    // `shielded` additionally avoids the open-file penalty on d/e/f that
    // `bare` incurs (own pawns present -- see king_safety.h's own header
    // comment on how the two components interact), so this comparison
    // is expected to be a large, unambiguous margin, not a close call.
    REQUIRE(king_safety_value(shielded).mg > king_safety_value(bare).mg);
}

TEST_CASE("king_safety_value: a fully open file near the king scores worse than a semi-open one, "
          "which scores worse than a closed one",
          "[eval][king_safety]") {
    init_all();
    // King on a1 (file 0) so only files a and b are checked (file -1 is
    // off-board and skipped) -- the a-pawn placed on a4, well outside
    // the 2-rank-deep shield zone (a2/a3), isolates the file-openness
    // component from the shield-bonus component cleanly: it affects
    // ONLY whether the a-file counts as open/semi-open/closed, not the
    // shield bonus. The b-file is left empty of pawns in every config
    // below, so it contributes an equal, constant open-file penalty
    // every time and cancels out of the comparison. Black's king sits
    // on h8 with nothing nearby in every config too, for the same
    // reason mobility_tests.cpp's own comparison tests keep one side
    // fixed while varying the other.
    Position closed = empty_position();
    closed.place_piece(make_square(0, 0), Piece::WhiteKing);  // a1
    closed.place_piece(make_square(0, 3), Piece::WhitePawn);  // a4 -- own pawn on the a-file
    closed.place_piece(make_square(7, 7), Piece::BlackKing);  // h8

    Position semi_open = empty_position();
    semi_open.place_piece(make_square(0, 0), Piece::WhiteKing);
    semi_open.place_piece(make_square(0, 4), Piece::BlackPawn); // a5 -- enemy pawn only
    semi_open.place_piece(make_square(7, 7), Piece::BlackKing);

    Position fully_open = empty_position();
    fully_open.place_piece(make_square(0, 0), Piece::WhiteKing);
    fully_open.place_piece(make_square(7, 7), Piece::BlackKing); // no a-file pawn at all

    const int closed_mg = king_safety_value(closed).mg;
    const int semi_open_mg = king_safety_value(semi_open).mg;
    const int fully_open_mg = king_safety_value(fully_open).mg;

    REQUIRE(closed_mg > semi_open_mg);
    REQUIRE(semi_open_mg > fully_open_mg);
}

TEST_CASE("king_safety_value: an enemy queen bearing down on the king zone is penalized",
          "[eval][king_safety]") {
    init_all();
    // Both configs keep the White king bare (no pawns at all, so d/e/f
    // are equally fully-open in both, contributing an identical
    // constant) and the Black king fixed on a8 with nothing else nearby
    // (so Black's own king_safety contribution is identical in both too)
    // -- isolating the comparison to whether the Black queen's placement
    // reaches the White king's zone at all.
    Position threatened = empty_position();
    threatened.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    threatened.place_piece(make_square(0, 7), Piece::BlackKing); // a8
    threatened.place_piece(make_square(4, 4), Piece::BlackQueen); // e5 -- open e-file straight to e1

    Position safe = empty_position();
    safe.place_piece(make_square(4, 0), Piece::WhiteKing);
    safe.place_piece(make_square(0, 7), Piece::BlackKing);
    safe.place_piece(make_square(7, 7), Piece::BlackQueen); // h8 -- doesn't reach e1's zone

    REQUIRE(king_safety_value(safe).mg > king_safety_value(threatened).mg);
}

TEST_CASE("king_safety_value: a king with no legal position (defensive: missing king entirely) "
          "contributes nothing rather than crashing",
          "[eval][king_safety]") {
    init_all();
    // Never reachable from a real game (board/fen.h's parser and
    // board/movegen.h's legality checking both guarantee exactly one
    // king per side), but king_safety.cpp's own defensive check against
    // bitscan_forward()'s undefined-behavior-on-empty precondition
    // deserves its own direct test rather than trusting it by
    // inspection alone.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1 -- White has one
    // Black has no king at all in this deliberately-malformed position.
    REQUIRE(king_safety_value(pos).mg != 0); // White's own contribution still applies
}
