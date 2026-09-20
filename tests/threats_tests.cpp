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

// ---------------------------------------------------------------------
// ROADMAP.md Tier 0 "PSQT and beyond" -- Step 8b, threats (the third
// "beyond" term, after mobility and space): ThreatsWeights /
// default_threats_weights() / threats_value()'s new nullable-override
// parameter.
// ---------------------------------------------------------------------

TEST_CASE("default_threats_weights: matches all 12 kXxxYyyPenalty constants exactly",
          "[eval][threats][tuner]") {
    const ThreatsWeights defaults = default_threats_weights();
    REQUIRE(defaults.knight_pawn_mg == kKnightAttackedByPawnPenalty.mg);
    REQUIRE(defaults.knight_pawn_eg == kKnightAttackedByPawnPenalty.eg);
    REQUIRE(defaults.bishop_pawn_mg == kBishopAttackedByPawnPenalty.mg);
    REQUIRE(defaults.bishop_pawn_eg == kBishopAttackedByPawnPenalty.eg);
    REQUIRE(defaults.rook_pawn_mg == kRookAttackedByPawnPenalty.mg);
    REQUIRE(defaults.rook_pawn_eg == kRookAttackedByPawnPenalty.eg);
    REQUIRE(defaults.queen_pawn_mg == kQueenAttackedByPawnPenalty.mg);
    REQUIRE(defaults.queen_pawn_eg == kQueenAttackedByPawnPenalty.eg);

    REQUIRE(defaults.knight_hanging_mg == kKnightHangingPenalty.mg);
    REQUIRE(defaults.knight_hanging_eg == kKnightHangingPenalty.eg);
    REQUIRE(defaults.bishop_hanging_mg == kBishopHangingPenalty.mg);
    REQUIRE(defaults.bishop_hanging_eg == kBishopHangingPenalty.eg);
    REQUIRE(defaults.rook_hanging_mg == kRookHangingPenalty.mg);
    REQUIRE(defaults.rook_hanging_eg == kRookHangingPenalty.eg);
    REQUIRE(defaults.queen_hanging_mg == kQueenHangingPenalty.mg);
    REQUIRE(defaults.queen_hanging_eg == kQueenHangingPenalty.eg);

    REQUIRE(defaults.knight_overloaded_mg == kKnightOverloadedPenalty.mg);
    REQUIRE(defaults.knight_overloaded_eg == kKnightOverloadedPenalty.eg);
    REQUIRE(defaults.bishop_overloaded_mg == kBishopOverloadedPenalty.mg);
    REQUIRE(defaults.bishop_overloaded_eg == kBishopOverloadedPenalty.eg);
    REQUIRE(defaults.rook_overloaded_mg == kRookOverloadedPenalty.mg);
    REQUIRE(defaults.rook_overloaded_eg == kRookOverloadedPenalty.eg);
    REQUIRE(defaults.queen_overloaded_mg == kQueenOverloadedPenalty.mg);
    REQUIRE(defaults.queen_overloaded_eg == kQueenOverloadedPenalty.eg);
}

TEST_CASE("threats_value: passing default_threats_weights() as an explicit override "
          "reproduces the no-override result exactly, for pawn-attack, hanging, and "
          "overloaded penalties alike",
          "[eval][threats][tuner]") {
    init_all();
    const ThreatsWeights defaults = default_threats_weights();

    // Pawn-attack case: same d5-knight-attacked-by-e6-pawn setup as
    // this file's own "a pawn-attacked, otherwise-defended knight"
    // test above.
    Position pawn_attacked = empty_position();
    pawn_attacked.place_piece(make_square(0, 0), Piece::WhiteKing);
    pawn_attacked.place_piece(make_square(0, 7), Piece::BlackKing);
    pawn_attacked.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5
    pawn_attacked.place_piece(make_square(3, 0), Piece::WhiteRook);   // d1, defends d5
    pawn_attacked.place_piece(make_square(4, 5), Piece::BlackPawn);   // e6, attacks d5
    REQUIRE(threats_value(pawn_attacked, &defaults) == threats_value(pawn_attacked));
    REQUIRE(threats_value(pawn_attacked).mg != 0); // not a vacuous 0 == 0

    // Overloaded case: same Rd1-defends-two-targets setup as this
    // file's own "a rook that is the SOLE defender of two
    // separately-attacked pieces is overloaded" test above.
    Position overloaded = empty_position();
    overloaded.place_piece(make_square(0, 0), Piece::WhiteKing);
    overloaded.place_piece(make_square(0, 7), Piece::BlackKing);
    overloaded.place_piece(make_square(3, 0), Piece::WhiteRook);
    overloaded.place_piece(make_square(3, 4), Piece::WhiteKnight);
    overloaded.place_piece(make_square(7, 0), Piece::WhiteRook);
    overloaded.place_piece(make_square(3, 7), Piece::BlackRook);
    overloaded.place_piece(make_square(7, 7), Piece::BlackRook);
    REQUIRE(threats_value(overloaded, &defaults) == threats_value(overloaded));
    REQUIRE(threats_value(overloaded).mg != 0);
}

TEST_CASE("threats_value: a modified ThreatsWeights changes only the ONE penalty category "
          "it targets, leaving the others untouched",
          "[eval][threats][tuner]") {
    init_all();
    // Same pawn-attacked-knight setup as above -- isolates the delta
    // entirely to the knight_pawn_* fields.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing);
    pos.place_piece(make_square(0, 7), Piece::BlackKing);
    pos.place_piece(make_square(3, 4), Piece::WhiteKnight); // d5
    pos.place_piece(make_square(3, 0), Piece::WhiteRook);   // d1, defends d5
    pos.place_piece(make_square(4, 5), Piece::BlackPawn);   // e6, attacks d5
    const Score baseline = threats_value(pos);
    REQUIRE(baseline == kKnightAttackedByPawnPenalty);

    ThreatsWeights perturbed = default_threats_weights();
    perturbed.knight_pawn_mg += 100.0;
    perturbed.knight_pawn_eg += 50.0;
    // Deliberately also perturb an UNRELATED field (queen hanging) to
    // confirm it has no effect on a position with no queen threat at
    // all -- the override genuinely only moves the term(s) actually in
    // play for this position, not every field indiscriminately.
    perturbed.queen_hanging_mg += 9999.0;
    perturbed.queen_hanging_eg += 9999.0;

    const Score boosted = threats_value(pos, &perturbed);
    REQUIRE(boosted.mg == baseline.mg + 100);
    REQUIRE(boosted.eg == baseline.eg + 50);
}
