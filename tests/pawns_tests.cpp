// tests/pawns_tests.cpp
//
// Unit tests for src/eval/pawns.h — passed, isolated, doubled, backward,
// and connected pawn evaluation (ROADMAP.md Phase 5's "Pawn structure"
// item). Positions are built directly via Position::place_piece(),
// matching eval_tests.cpp's style, specifically so each test can be
// hand-traced term-by-term against pawns.cpp's exact logic rather than
// relying on a real game position where every term interacts at once.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/pawns.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Returns a fully empty position (no pieces, given side to move) --
/// same helper pattern as eval_tests.cpp/movegen_tests.cpp.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("pawn_structure_value: starting position is exactly balanced", "[eval][pawns]") {
    init_masks();
    REQUIRE(pawn_structure_value(start_position()) == Score{0, 0});
    // Every starting pawn has a same-rank phalanx neighbor (a2/h2 have
    // exactly one, b2..g2 have two) and nothing else applies (none are
    // isolated, doubled, passed, or backward -- each side's own file is
    // blocked one square ahead of the enemy's mirror pawn well within
    // passed_pawn_mask()'s reach) -- so every term that fires is a
    // connected-pawn bonus, identical in magnitude for both colors by
    // the position's left-right, White-Black symmetry, and cancels
    // exactly in the final White-minus-Black sum.
}

TEST_CASE("pawn_structure_value: a lone unopposed pawn is isolated but passed", "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4, alone
    // No adjacent-file White pawn (isolated) and no Black pawns anywhere
    // (passed, and nothing to make its push square backward-unsafe --
    // that check doesn't even run for a passed pawn). Not doubled (only
    // one pawn on its file) and not connected (no other White pawn to
    // defend or stand beside it). Expected: kIsolatedPawnPenalty +
    // kPassedPawnBonus[3] (e4's relative rank from White is 3) exactly.
    const Score expected = kIsolatedPawnPenalty + kPassedPawnBonus[3];
    REQUIRE(pawn_structure_value(pos) == expected);
}

TEST_CASE("pawn_structure_value: a phalanx pair is neither isolated, and both get the "
          "connected bonus",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(3, 3), Piece::WhitePawn); // d4
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    // d4 and e4 stand beside each other (same rank, adjacent files) --
    // each removes the other's isolation and grants the other a
    // phalanx connected bonus, while both remain passed and unopposed
    // (no Black pawns at all) at the same relative rank as the previous
    // test. Expected: twice (kPassedPawnBonus[3] + kConnectedPawnBonus)
    // -- no isolation penalty this time, applied identically to both
    // pawns by the position's own d4/e4 symmetry.
    const Score per_pawn = kPassedPawnBonus[3] + kConnectedPawnBonus;
    const Score expected = per_pawn + per_pawn;
    REQUIRE(pawn_structure_value(pos) == expected);
}

TEST_CASE("pawn_structure_value: two pawns on the same file are both doubled and isolated, "
          "despite each being passed",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(4, 1), Piece::WhitePawn); // e2
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    // Both on the e-file, no d/f-file White pawn anywhere -- both
    // isolated AND both doubled (pawns.h's kDoubledPawnPenalty doc
    // comment: applied per pawn sharing a file, not just the "extra"
    // one). No Black pawns, so both remain passed at their own relative
    // ranks (e2 -> 1, e4 -> 3). Neither defends nor stands beside the
    // other (not adjacent files), so no connected bonus either way.
    const Score e2_total = kIsolatedPawnPenalty + kDoubledPawnPenalty + kPassedPawnBonus[1];
    const Score e4_total = kIsolatedPawnPenalty + kDoubledPawnPenalty + kPassedPawnBonus[3];
    REQUIRE(pawn_structure_value(pos) == e2_total + e4_total);
}

TEST_CASE("pawn_structure_value: an unsupported pawn facing a controlling enemy pawn is "
          "backward; adding real support removes the penalty",
          "[eval][pawns]") {
    init_masks();

    // Position A: White d2 alone, Black e4 alone. d2 has no adjacent-
    // file White pawn at or behind it (backward_support_mask()'s doc
    // comment) and its push square d3 is attacked by Black's e4 pawn
    // (the standard reverse-pawn-attack trick pawns.cpp uses) -- so d2
    // is backward (and, separately, isolated: no adjacent-file White
    // pawn at all). By the exact same geometry mirrored the other way,
    // Black's e4 is *also* backward and isolated relative to White's
    // d2 (its own push square e3 is attacked by White's d2) -- both
    // sides' identical penalty totals cancel exactly in the final sum.
    Position pos_a = empty_position();
    pos_a.place_piece(make_square(0, 0), Piece::WhiteKing); // a1, out of the way
    pos_a.place_piece(make_square(0, 7), Piece::BlackKing); // a8, out of the way
    pos_a.place_piece(make_square(3, 1), Piece::WhitePawn); // d2
    pos_a.place_piece(make_square(4, 3), Piece::BlackPawn); // e4
    REQUIRE(pawn_structure_value(pos_a) == Score{0, 0});

    // Position B: same as A, plus a White pawn on e1. e1 sits on a file
    // adjacent to d2, at or behind d2's rank -- exactly what
    // backward_support_mask() looks for -- so d2 is no longer backward,
    // no longer isolated (e1 is now an adjacent-file neighbor), and
    // gains a connected bonus (e1 defends d2 via a normal diagonal pawn
    // capture pattern). e1 itself contributes nothing on its own (not
    // isolated: d2 is adjacent; not passed: Black's e4 still blocks it;
    // not connected: no White pawn defends or stands beside e1; not
    // backward: its own push square e2 isn't attacked by Black's e4).
    // Black's e4 total is completely unchanged by e1's addition (d2
    // alone already made e4 backward/isolated in A; e1 doesn't touch
    // any of e4's own checks), so it cancels out of the A-vs-B
    // comparison below regardless of its exact value -- only White's
    // side needs to have actually improved, which this asserts
    // directly rather than via a fully re-derived absolute total.
    Position pos_b = pos_a;
    pos_b.place_piece(make_square(4, 0), Piece::WhitePawn); // e1, now free (king moved to a1)
    const Score value_a = pawn_structure_value(pos_a);
    const Score value_b = pawn_structure_value(pos_b);
    REQUIRE(value_b.mg > value_a.mg);
    REQUIRE(value_b.eg > value_a.eg);
}
