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
    // test. Since each pawn's own phalanx PARTNER is also passed (both
    // are, here), each also gets kConnectedPassedPawnBonus on top of the
    // plain connected bonus (pawns.h's own doc comment on that
    // constant). Expected: twice (kPassedPawnBonus[3] +
    // kConnectedPawnBonus + kConnectedPassedPawnBonus[3]) -- no
    // isolation penalty this time, applied identically to both pawns by
    // the position's own d4/e4 symmetry.
    const Score per_pawn = kPassedPawnBonus[3] + kConnectedPawnBonus + kConnectedPassedPawnBonus[3];
    const Score expected = per_pawn + per_pawn;
    REQUIRE(pawn_structure_value(pos) == expected);
}

TEST_CASE("pawn_structure_value: a phalanx pair where only ONE pawn is passed gets no "
          "connected-passed-pawns bonus for either",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(3, 3), Piece::WhitePawn); // d4
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    pos.place_piece(make_square(5, 5), Piece::BlackPawn); // f6 -- blocks e4's own
                                                           // passed status (f is
                                                           // adjacent to e) but NOT
                                                           // d4's (f is not adjacent
                                                           // to d)
    // kConnectedPassedPawnBonus (pawns.h's own doc comment) requires the
    // SPECIFIC defender/phalanx partner to also be passed, not merely
    // that this pawn itself is. d4 is passed but its only phalanx
    // partner (e4) is not, so d4 gets no connected-passed bonus; e4
    // isn't even passed itself, so it never reaches that check at all.
    // Both still get the plain kConnectedPawnBonus regardless (that one
    // only cares about presence, not passed status). e4's own
    // backward-pawn check is also skipped despite not being passed --
    // d4 sits in e4's own backward_support_mask (adjacent file, same
    // rank counts as "at or behind"), so `has_support` is true there.
    // e4 DOES pick up the candidate-passed-pawn bonus (Priority Fixes
    // (2026-09-08) section, kCandidatePassedPawnBonus's own doc comment)
    // though, independent of the backward check above: no Black pawn
    // stands anywhere ahead of it on its own (e) file (part (a)
    // passes), and among the adjacent d/f files, White's own d4
    // (relative rank 3, at-or-behind e4's own rank 3) matches Black's
    // f6 (relative rank 5, ahead) one-for-one (part (b)'s own tie case,
    // same as this file's dedicated candidate-passed-pawn test's first
    // scenario). The f6 Black pawn is itself evaluated too, on the
    // OTHER side of the same call: it's isolated (no Black pawn on the
    // adjacent e/g files) AND backward (no Black pawn at-or-behind it
    // on an adjacent file to support it, and its own push square, f5,
    // is attacked by the White e4 pawn) -- both penalties against
    // Black, which get SUBTRACTED into this White-relative total
    // (`score -= side_score` for Black, pawn_structure_value()'s own
    // loop), so they show up here as a net POSITIVE contribution for
    // White. f6 is NOT itself a candidate passed pawn (e4 directly
    // blocks its own file ahead of it from Black's perspective, failing
    // part (a) the same way the dedicated same-file-blocker test below
    // demonstrates).
    const Score d4_total = kPassedPawnBonus[3] + kConnectedPawnBonus;
    const Score e4_total = kConnectedPawnBonus + kCandidatePassedPawnBonus[3];
    const Score f6_penalty = kIsolatedPawnPenalty + kBackwardPawnPenalty;
    REQUIRE(pawn_structure_value(pos) == d4_total + e4_total - f6_penalty);
}

TEST_CASE("pawn_structure_value: a passed pawn defended (not phalanx) by another passed pawn "
          "gets the connected-passed-pawns bonus, asymmetrically like the plain connected bonus",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(3, 3), Piece::WhitePawn); // d4
    pos.place_piece(make_square(4, 4), Piece::WhitePawn); // e5, defended by d4
    // No Black pawns at all -- both unopposed and passed. d4 (relative
    // rank 3) is diagonally BEHIND e5, so it defends e5 (the standard
    // "defended," not phalanx, connected-pawns pattern) -- but e5 does
    // NOT defend or stand beside d4 (it's ahead, not beside or behind),
    // so the plain kConnectedPawnBonus ALREADY only ever applies to e5
    // here, not d4, matching this file's own existing asymmetric-
    // connection precedent. The new kConnectedPassedPawnBonus follows
    // that same asymmetry: only e5 (whose specific defender, d4, is
    // also passed) gets it. Neither pawn is isolated (each has an own
    // pawn on an adjacent FILE, regardless of rank).
    const Score d4_total = kPassedPawnBonus[3];
    const Score e5_total = kPassedPawnBonus[4] + kConnectedPawnBonus + kConnectedPassedPawnBonus[4];
    REQUIRE(pawn_structure_value(pos) == d4_total + e5_total);
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

TEST_CASE("pawn_structure_value: a supported, not-yet-passed pawn with a numerically-losable "
          "adjacent-file blocker is a candidate passed pawn",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1, out of the way
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8, out of the way
    pos.place_piece(make_square(2, 1), Piece::WhitePawn); // c2
    pos.place_piece(make_square(3, 3), Piece::WhitePawn); // d4
    pos.place_piece(make_square(4, 4), Piece::BlackPawn); // e5
    // d4 is not passed (Black's e5 sits on an adjacent file ahead of it,
    // inside passed_pawn_mask()'s own cone) and not backward (c2, an
    // adjacent-file pawn at-or-behind d4's own rank, supports it) --
    // but IS a candidate passed pawn: no enemy pawn stands anywhere
    // ahead of it on its own (d) file (part (a)), and among the
    // adjacent files only, White's own pawn count at-or-behind (c2,
    // counting 1) matches Black's own count of pawns ahead (e5,
    // counting 1) (part (b), a tie counts as "own pawns don't come out
    // numerically behind"). c2 itself is unopposed on its own adjacent
    // files (b/d have no Black pawn ahead of it -- e5 is on e, not
    // adjacent to c) and so is genuinely passed, not merely a
    // candidate. Black's e5 is isolated (no Black pawn on the d/f
    // files) and nothing else (not backward: its own push square e4
    // isn't attacked by any White pawn; not a candidate either: d4
    // itself directly blocks e5's own file ahead of it, failing part
    // (a) for e5 the same way it passes part (a) for d4 -- e5 is
    // "ahead" of d4 from White's perspective but "blocking" d4's own
    // file is a Black-file question, not White's, and vice versa; see
    // this test's own sibling below for the fully-blocked case).
    const Score white_total = kPassedPawnBonus[1] + kCandidatePassedPawnBonus[3];
    const Score black_penalty = kIsolatedPawnPenalty;
    REQUIRE(pawn_structure_value(pos) == white_total - black_penalty);
}

TEST_CASE("pawn_structure_value: a pawn with a direct same-file blocker is never a candidate, "
          "regardless of adjacent-file support",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1, out of the way
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8, out of the way
    pos.place_piece(make_square(2, 1), Piece::WhitePawn); // c2, otherwise a real supporter
    pos.place_piece(make_square(3, 3), Piece::WhitePawn); // d4
    pos.place_piece(make_square(3, 4), Piece::BlackPawn); // d5, directly blocks d4's own file
    // A straight-ahead enemy pawn can never be traded away (pawns don't
    // capture straight ahead), so neither d4 nor d5 can ever become a
    // candidate passed pawn here, regardless of what the adjacent files
    // look like -- this is exactly candidate-passed-pawn test's part
    // (a), independent of and stricter than part (b)'s own count
    // comparison. d4 itself: not isolated (c2 present), not passed
    // (blocked by d5 on its own file), not connected (c2 doesn't
    // defend or stand beside d4), not backward (c2 supports it),
    // and NOT a candidate (part (a) fails: d5 blocks its own file) --
    // contributes nothing. c2 itself: not isolated (d4 present), not
    // passed (d5 sits in its own passed_pawn_mask() cone via the
    // adjacent d-file), not connected, not backward (its own push
    // square c3 isn't attacked by anything), and not a candidate
    // either (part (b) fails: zero own-pawn support on the b/d files
    // at-or-behind its own rank versus Black's one blocker on d) --
    // also contributes nothing, so White's side totals exactly zero.
    // Black's d5: isolated (no Black pawn on c/e), not passed (blocked
    // by both c2 and d4), not connected, not backward (its own push
    // square d4 isn't enemy-pawn-attacked -- occupied by White's own
    // pawn, but this test, matching CPW's own convention and this
    // file's pre-existing "an unsupported pawn..." test above, checks
    // attack status only, not occupancy), and not a candidate (part (a)
    // fails: d4 blocks its own file too) -- contributes only the
    // isolation penalty.
    const Score white_total = Score{0, 0};
    const Score black_penalty = kIsolatedPawnPenalty;
    REQUIRE(pawn_structure_value(pos) == white_total - black_penalty);
}

TEST_CASE("pawn_structure_value: a passed pawn far separated from every other pawn on the "
          "board gets the outside-passed-pawn bonus",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1, out of the way
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8, out of the way
    pos.place_piece(make_square(0, 4), Piece::WhitePawn); // a5, the target pawn
    pos.place_piece(make_square(4, 1), Piece::WhitePawn); // e2, unrelated background pawn
    pos.place_piece(make_square(4, 4), Piece::BlackPawn); // e5, blocks e2 (not a5)
    // a5 is passed (no Black pawn on the a/b files at all) AND outside:
    // its only other pawns on the board, e2 and e5, both sit on the
    // e-file, 4 files away -- comfortably past kOutsidePassedPawnMinFileGap
    // (3) -- so it picks up kOutsidePassedPawnBonus on top of the plain
    // kPassedPawnBonus. a5 is also isolated (no White pawn on the
    // adjacent b-file). e2 itself is NOT passed (Black's e5 sits
    // directly ahead of it on the same file) and, having a direct
    // same-file blocker, is also not a candidate (part (a) of that
    // test) -- it contributes only its own isolation penalty (no White
    // pawn on the adjacent d/f files either). Black's e5 is likewise
    // not passed (White's e2 sits directly ahead of it, from Black's
    // own advancing direction) and not a candidate for the identical
    // same-file-blocker reason -- it also contributes only its own
    // isolation penalty (no Black pawn on the adjacent d/f files). The
    // two background pawns' isolation penalties are on OPPOSITE sides
    // of the final White-minus-Black subtraction and are numerically
    // identical, so they cancel exactly. White's own pawns, though, now
    // form TWO separate pawn islands (a5 alone, then a gap of 3 empty
    // files, then e2) -- kPawnIslandPenalty's own doc comment, pawns.h
    // -- charged once (2 islands - 1); Black has only e5, a single
    // island, charged nothing. That one island charge is the only term
    // left standing alongside a5's own passed/outside bonuses.
    const Score expected = kIsolatedPawnPenalty + kPassedPawnBonus[4] +
                            kOutsidePassedPawnBonus[4] + kPawnIslandPenalty;
    REQUIRE(pawn_structure_value(pos) == expected);
}

TEST_CASE("pawn_structure_value: a passed pawn only 2 files from its nearest neighbor is NOT "
          "outside, regardless of being unopposed",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1, out of the way
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8, out of the way
    pos.place_piece(make_square(3, 4), Piece::WhitePawn); // d5
    pos.place_piece(make_square(1, 5), Piece::BlackPawn); // b6
    // Neither pawn blocks the other's passed status (b is not adjacent
    // to d, and d is not adjacent to b) -- both are unopposed and
    // passed. But d5's only other pawn on the board, b6, is just 2
    // files away -- short of kOutsidePassedPawnMinFileGap (3) -- so
    // NEITHER pawn gets the outside bonus (checked symmetrically: b6's
    // own nearest other pawn, d5, is the identical 2 files away). Both
    // pawns ARE isolated (no OWN-color pawn stands on either one's
    // adjacent files -- isolation only cares about friendly pawns, and
    // each side has exactly one), but kIsolatedPawnPenalty is the same
    // fixed value regardless of rank or file, so it cancels exactly
    // across the final White-minus-Black subtraction, leaving a clean
    // difference of the two plain passed-pawn bonuses with no other
    // term involved.
    const Score expected = kPassedPawnBonus[4] - kPassedPawnBonus[2];
    REQUIRE(pawn_structure_value(pos) == expected);
}

TEST_CASE("pawn_structure_value: two separate pawn groups on one side cost exactly one "
          "pawn-island charge",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1, out of the way
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8, out of the way
    pos.place_piece(make_square(0, 1), Piece::WhitePawn); // a2
    pos.place_piece(make_square(1, 1), Piece::WhitePawn); // b2, contiguous with a2
    pos.place_piece(make_square(5, 1), Piece::WhitePawn); // f2, a separate group (c/d/e empty)
    pos.place_piece(make_square(7, 6), Piece::BlackPawn); // h7, unrelated background pawn
    // White's a2/b2 form one island (contiguous, and a mutual passed
    // phalanx pair, same shape as this file's own earlier "phalanx
    // pair" test); f2 is a second, separate island -- 2 islands total,
    // charged ONCE (kPawnIslandPenalty's own doc comment: per island
    // BEYOND the first). f2 itself and Black's own h7 are individually
    // isolated, passed (h7 is far enough from f2 -- 2 files -- that it
    // doesn't block it, and vice versa), but NOT outside (each only 2
    // files from the other, short of kOutsidePassedPawnMinFileGap) --
    // their kIsolatedPawnPenalty and kPassedPawnBonus[1] terms are
    // numerically identical and land on opposite sides of the final
    // White-minus-Black subtraction, so they cancel exactly, leaving a
    // clean result: twice the a2/b2 phalanx-passed-pair total (this
    // file's own "phalanx pair" test derives that exact per-pawn
    // formula) plus one single pawn-island charge. Black's own h7 is a
    // single-file, single-island side -- charged nothing.
    const Score per_phalanx_pawn =
        kPassedPawnBonus[1] + kConnectedPawnBonus + kConnectedPassedPawnBonus[1];
    const Score expected = per_phalanx_pawn + per_phalanx_pawn + kPawnIslandPenalty;
    REQUIRE(pawn_structure_value(pos) == expected);
}

TEST_CASE("pawn_structure_value: three separate single pawns cost two pawn-island charges, "
          "scaling with (islands - 1)",
          "[eval][pawns]") {
    init_masks();
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1, out of the way
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8, out of the way
    pos.place_piece(make_square(0, 1), Piece::WhitePawn); // a2, its own island
    pos.place_piece(make_square(2, 1), Piece::WhitePawn); // c2, its own island
    pos.place_piece(make_square(4, 1), Piece::WhitePawn); // e2, its own island
    // No Black pawns at all, so all three are trivially passed (nothing
    // anywhere to block them) and, being 2 files apart from their
    // nearest neighbor each way, none is isolated-from-everything...
    // rather each one IS isolated in the structural sense (no FRIENDLY
    // pawn on either adjacent file -- b, d, and the a/c-or-c/e pairs
    // respectively) and none is outside (2 files, short of the 3-file
    // threshold, from its nearest neighbor either way) or connected
    // (no adjacent-file own pawn to defend or stand beside). 3 separate
    // one-file islands -- charged TWICE (3 - 1), demonstrating the
    // penalty scales with island count rather than being a flat
    // per-side charge. Black has no pawns at all, so its own file_counts
    // are all zero, correctly yielding 0 islands and 0 charge, not some
    // vacuous/undefined case.
    const Score per_pawn = kIsolatedPawnPenalty + kPassedPawnBonus[1];
    const Score expected = per_pawn + per_pawn + per_pawn + kPawnIslandPenalty * 2;
    REQUIRE(pawn_structure_value(pos) == expected);
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
