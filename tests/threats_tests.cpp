// tests/threats_tests.cpp
//
// Unit tests for src/eval/threats.h (ROADMAP.md Phase 5's "Threats
// evaluation (hanging/attacked pieces, pieces attacked by pawns)"
// item). Positions are built directly via Position::place_piece(),
// matching every other eval/*_tests.cpp file's style, specifically so
// each test isolates threats_value() itself rather than relying on a
// real game position where every eval term interacts at once
// (eval_tests.cpp's job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "eval/threats.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so table-init state isn't shared across cases -- each
/// must initialize what it needs itself. Unlike eval/piece_bonuses.h's/
/// eval/knight_outposts.h's/eval/space.h's own tests, threats_value()
/// DOES need board::init_magic_bitboards() (see threats.h's own
/// precondition comment) -- it computes sliding-piece attack bitboards
/// to determine attacked/defended status.
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

TEST_CASE("threats_value: starting position has no threats", "[eval][threats]") {
    init_all();
    // Every minor/major piece sits on its own back rank, well out of
    // reach of any pawn's attack pattern and not attacked by anything
    // at all at move 0.
    REQUIRE(threats_value(start_position()) == Score{0, 0});
}

TEST_CASE("threats_value: a pawn-attacked, otherwise-defended knight incurs only the "
          "pawn-attack penalty",
          "[eval][threats]") {
    init_all();
    // d5 knight, defended by a rook on d1 (clear file), attacked by a
    // Black pawn on e6. The rook's own defense means "attacked by
    // anything, undefended" (the hanging condition) never triggers in
    // EITHER config below -- isolating the delta entirely to the
    // pawn-attack penalty, which is applied unconditionally regardless
    // of defense.
    Position with_pawn = empty_position();
    with_pawn.place_piece(make_square(0, 0), Piece::WhiteKing);   // a1
    with_pawn.place_piece(make_square(0, 7), Piece::BlackKing);   // a8
    with_pawn.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5
    with_pawn.place_piece(make_square(3, 0), Piece::WhiteRook);   // d1, defends d5
    with_pawn.place_piece(make_square(4, 5), Piece::BlackPawn);   // e6, attacks d5

    Position without_pawn = empty_position();
    without_pawn.place_piece(make_square(0, 0), Piece::WhiteKing);
    without_pawn.place_piece(make_square(0, 7), Piece::BlackKing);
    without_pawn.place_piece(make_square(3, 4), Piece::WhiteKnight);
    without_pawn.place_piece(make_square(3, 0), Piece::WhiteRook);

    REQUIRE(threats_value(with_pawn) == kKnightAttackedByPawnPenalty);
    REQUIRE(threats_value(without_pawn) == Score{0, 0});
}

TEST_CASE("threats_value: an undefended knight attacked by a non-pawn piece incurs only the "
          "hanging penalty",
          "[eval][threats]") {
    init_all();
    // d5 knight, no White defender anywhere, attacked by a Black rook
    // on d8 (clear file). No Black pawns exist in this position at
    // all, so the pawn-attack penalty can never trigger here --
    // isolating the delta entirely to the hanging penalty.
    Position with_rook = empty_position();
    with_rook.place_piece(make_square(0, 0), Piece::WhiteKing);   // a1
    with_rook.place_piece(make_square(0, 7), Piece::BlackKing);   // a8
    with_rook.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5
    with_rook.place_piece(make_square(3, 7), Piece::BlackRook);   // d8, attacks d5

    Position without_rook = empty_position();
    without_rook.place_piece(make_square(0, 0), Piece::WhiteKing);
    without_rook.place_piece(make_square(0, 7), Piece::BlackKing);
    without_rook.place_piece(make_square(3, 4), Piece::WhiteKnight);

    REQUIRE(threats_value(with_rook) == kKnightHangingPenalty);
    REQUIRE(threats_value(without_rook) == Score{0, 0});
}

TEST_CASE("threats_value: a pawn-attacked, undefended knight stacks both penalties",
          "[eval][threats]") {
    init_all();
    // Same d5 knight, attacked by a Black pawn on e6, with no White
    // defender anywhere -- both the pawn-attack condition and the
    // hanging condition (attacked by the SAME pawn, since a pawn's
    // attack also counts toward the general "attacked by anything"
    // check) are simultaneously true, confirming the two penalties add
    // rather than one suppressing the other.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);   // a1
    pos.place_piece(make_square(0, 7), Piece::BlackKing);   // a8
    pos.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5
    pos.place_piece(make_square(4, 5), Piece::BlackPawn);   // e6, attacks d5

    REQUIRE(threats_value(pos) == kKnightAttackedByPawnPenalty + kKnightHangingPenalty);
}

TEST_CASE("threats_value: a defended knight is never counted as hanging even when attacked",
          "[eval][threats]") {
    init_all();
    // Same d5-knight-attacked-by-a-Black-rook-on-d8 setup as the
    // isolated hanging-penalty test above, but with a White pawn added
    // on c4 defending d5 -- the hanging penalty no longer applies (the
    // piece is attacked, but no longer undefended), and no Black pawn
    // exists to trigger the pawn-attack penalty either, so the whole
    // term reads exactly zero despite the knight visibly being under
    // attack.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);   // a1
    pos.place_piece(make_square(0, 7), Piece::BlackKing);   // a8
    pos.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5
    pos.place_piece(make_square(3, 7), Piece::BlackRook);   // d8, attacks d5
    pos.place_piece(make_square(2, 3), Piece::WhitePawn);   // c4, defends d5

    REQUIRE(threats_value(pos) == Score{0, 0});
}

TEST_CASE("threats_value: a rook that is the SOLE defender of two separately-attacked pieces "
          "is overloaded",
          "[eval][threats]") {
    init_all();
    // White Rd1 defends two different pieces along its own two
    // perpendicular lines: the d5 knight (up the d-file) and the h1
    // rook (along rank 1) -- both currently attacked by a Black rook
    // each (Rd8 down the d-file, stopping at d5; Rh8 down the h-file,
    // stopping at h1), and Rd1 is each one's own ONLY White defender
    // (neither the d5 knight's own knight-move attacks nor the h1
    // rook's own file/rank attacks reach back to defend the other).
    // Black's own two rooks incidentally defend EACH OTHER along rank
    // 8 (Rd8 <-> Rh8, a clear rank with nothing between them) -- kept
    // deliberately, not avoided, to also confirm Black's own
    // overloaded-piece check correctly does NOT fire here: each Black
    // rook only ever over-defends at most one thing (itself, from the
    // other), never two, so Black's own side of the ledger stays
    // exactly zero and every term below is attributable to White's
    // single overloaded Rd1 alone.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8
    pos.place_piece(make_square(3, 0), Piece::WhiteRook); // d1 -- the overloaded piece
    pos.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5 -- target 1
    pos.place_piece(make_square(7, 0), Piece::WhiteRook);   // h1 -- target 2
    pos.place_piece(make_square(3, 7), Piece::BlackRook);   // d8, attacks d5
    pos.place_piece(make_square(7, 7), Piece::BlackRook);   // h8, attacks h1

    REQUIRE(threats_value(pos) == kRookOverloadedPenalty);
}

TEST_CASE("threats_value: adding a second defender to ONE of the two targets removes the "
          "overload penalty entirely, even though the other target is still solely defended",
          "[eval][threats]") {
    init_all();
    // Identical to the overload test just above, with one addition: a
    // White bishop on b3 that ALSO defends the d5 knight via the
    // b3-c4-d5 diagonal. d5 now has TWO White defenders (Rd1 and the
    // new bishop), so it no longer counts as anyone's SOLE
    // responsibility -- Rd1 is left over-defending only ONE thing
    // (the h1 rook), which isn't enough to be overloaded (the test
    // requires 2 or more). The bishop itself picks up no penalty of
    // its own either: it isn't attacked by anything, pawn or
    // otherwise, in this position.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);   // a1
    pos.place_piece(make_square(0, 7), Piece::BlackKing);   // a8
    pos.place_piece(make_square(3, 0), Piece::WhiteRook);   // d1
    pos.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5
    pos.place_piece(make_square(7, 0), Piece::WhiteRook);   // h1
    pos.place_piece(make_square(1, 2), Piece::WhiteBishop); // b3, a SECOND defender of d5
    pos.place_piece(make_square(3, 7), Piece::BlackRook);   // d8, attacks d5
    pos.place_piece(make_square(7, 7), Piece::BlackRook);   // h8, attacks h1

    REQUIRE(threats_value(pos) == Score{0, 0});
}
