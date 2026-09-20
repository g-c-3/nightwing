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

TEST_CASE("king_safety_value: a pawn storm further advanced toward the king scores worse than "
          "an identically-semi-open one that's barely advanced",
          "[eval][king_safety]") {
    init_all();
    // ROADMAP.md's own "pawn storms" item (Priority Fixes, 2026-09-08
    // section; king_safety.h's own header comment has the full
    // rationale). King on a1 (file 0, so only files a/b are checked --
    // file -1 is off-board and skipped), same isolation trick this
    // file's own existing open/semi-open-file test above already uses:
    // an enemy pawn on the SAME file in both configs (so both are
    // equally semi-open, contributing an identical constant penalty
    // that cancels out of the comparison) but at very different ranks,
    // isolating the comparison to the storm term specifically. The
    // b-file stays pawnless in both, for the same "contributes an
    // equal constant, cancels out" reason.
    Position barely_advanced = empty_position();
    barely_advanced.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    barely_advanced.place_piece(make_square(0, 5), Piece::BlackPawn); // a6 -- Black's own
                                                                       // relative rank 2
                                                                       // (7 - rank_of(a6)=5),
                                                                       // still close to
                                                                       // Black's own side
    barely_advanced.place_piece(make_square(7, 7), Piece::BlackKing); // h8

    Position far_advanced = empty_position();
    far_advanced.place_piece(make_square(0, 0), Piece::WhiteKing);
    far_advanced.place_piece(make_square(0, 2), Piece::BlackPawn); // a3 -- Black's own
                                                                    // relative rank 5
                                                                    // (7 - rank_of(a3)=2),
                                                                    // deep into White's
                                                                    // own territory, right
                                                                    // beside the king
    far_advanced.place_piece(make_square(7, 7), Piece::BlackKing);

    REQUIRE(king_safety_value(barely_advanced).mg > king_safety_value(far_advanced).mg);
}

TEST_CASE("king_safety_value: a storming enemy pawn on the king's own file scores worse than no "
          "enemy pawn there at all, even though both leave the file merely open, not "
          "additionally weakened by the storm penalty in the open case",
          "[eval][king_safety]") {
    init_all();
    // A direct check that the storm penalty is a REAL, distinct
    // contribution, not merely a relabeling of the existing open/semi-
    // open-file penalty this file's own earlier test already covers:
    // `no_storm` has no a/b-file pawn at all (fully open, per this
    // file's own kOpenFileNearKingPenalty = {-24,-4}), while `storming`
    // has the SAME file merely semi-open (a SMALLER base penalty,
    // kSemiOpenFileNearKingPenalty = {-12,-2}) but with the enemy pawn
    // deep into White's own territory (relative rank 5, kPawnStormPenalty
    // = {-24,-4} at that rank -- comfortably more than the 12-point mg
    // gap semi-open saves versus fully-open). If the storm term
    // contributed nothing, `storming` would score BETTER than
    // `no_storm` (semi-open alone beats fully-open alone) -- this
    // confirms the storm penalty is large enough at this rank to flip
    // that comparison the other way instead.
    Position no_storm = empty_position();
    no_storm.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    no_storm.place_piece(make_square(7, 7), Piece::BlackKing); // h8, nothing on a/b at all

    Position storming = empty_position();
    storming.place_piece(make_square(0, 0), Piece::WhiteKing);
    storming.place_piece(make_square(0, 2), Piece::BlackPawn); // a3, relative rank 5 -- deep
    storming.place_piece(make_square(7, 7), Piece::BlackKing);

    REQUIRE(king_safety_value(no_storm).mg > king_safety_value(storming).mg);
}

TEST_CASE("king_safety_value: a king trapped on its own back rank by its own pawns, facing an "
          "enemy rook, is penalized more than an identical king with a rook-provided flight "
          "square",
          "[eval][king_safety]") {
    init_all();
    // King on a1 with a full a2/b2 pawn wall directly in front of it,
    // and a Black rook on the board (anywhere; king_safety_value()'s
    // own back-rank test only checks for PRESENCE, not whether that
    // specific rook currently attacks anything) to satisfy the
    // "something to actually exploit it" gate. `with_luft` moves the
    // b-pawn one square further forward (b2 -> b3) rather than removing
    // it outright -- b3 is still within shield_zone()'s own 2-rank-deep
    // zone (so the plain pawn-shield bonus, which only counts presence
    // anywhere in that zone, is completely unchanged) and the b-file
    // still has an own pawn somewhere on it (so it stays closed, not
    // open -- kOpenFileNearKingPenalty/kSemiOpenFileNearKingPenalty
    // don't change either). The ONLY thing that changes is whether b2
    // ITSELF, directly in front of the king, is empty -- which is
    // exactly what flips the back-rank term from "no luft" to "has
    // luft," isolating this comparison to precisely that one term.
    Position trapped = empty_position();
    trapped.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    trapped.place_piece(make_square(0, 1), Piece::WhitePawn); // a2
    trapped.place_piece(make_square(1, 1), Piece::WhitePawn); // b2
    trapped.place_piece(make_square(7, 7), Piece::BlackKing); // h8
    trapped.place_piece(make_square(7, 4), Piece::BlackRook); // h5, present but not
                                                                // currently attacking a1's
                                                                // zone at all

    Position with_luft = trapped;
    with_luft.remove_piece(make_square(1, 1));
    with_luft.place_piece(make_square(1, 2), Piece::WhitePawn); // b3 -- still shields, still
                                                                  // keeps the b-file closed,
                                                                  // but b2 itself is now open
    REQUIRE(king_safety_value(with_luft).mg > king_safety_value(trapped).mg);
}

TEST_CASE("king_safety_value: a king trapped on its own back rank scores IDENTICALLY whether "
          "or not the enemy has a rook or queen, unless one is actually present",
          "[eval][king_safety]") {
    init_all();
    // The same fully-walled-in a1 king as the test above, but this time
    // Black has NO rook or queen anywhere on the board at all -- only a
    // king and a lone knight (present specifically to confirm the gate
    // checks piece TYPE, not merely "does Black have any piece besides
    // its king"). king_safety_value()'s own doc comment: the back-rank
    // penalty requires an enemy rook OR queen specifically, since
    // neither a knight nor a bishop can deliver the along-the-rank/file
    // check this term is modeling. Adding an actual Black rook should
    // make the position WORSE for White (the two configs are otherwise
    // identical -- same a1/a2/b2 wall, same Black king square), and by
    // exactly kBackRankWeaknessPenalty's own mg magnitude, isolating
    // the comparison to precisely this one term rather than merely
    // asserting an inequality the way most of this file's own
    // comparison tests do.
    Position no_major_piece = empty_position();
    no_major_piece.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    no_major_piece.place_piece(make_square(0, 1), Piece::WhitePawn); // a2
    no_major_piece.place_piece(make_square(1, 1), Piece::WhitePawn); // b2
    no_major_piece.place_piece(make_square(7, 7), Piece::BlackKing); // h8
    no_major_piece.place_piece(make_square(6, 7), Piece::BlackKnight); // g8, not a rook/queen

    Position with_rook = no_major_piece;
    with_rook.remove_piece(make_square(6, 7));
    with_rook.place_piece(make_square(6, 7), Piece::BlackRook); // g8, same square, now a
                                                                  // qualifying piece type

    const Score without = king_safety_value(no_major_piece);
    const Score with = king_safety_value(with_rook);
    REQUIRE(without.mg - with.mg == -kBackRankWeaknessPenalty.mg);
    REQUIRE(without.eg - with.eg == -kBackRankWeaknessPenalty.eg);
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

// ---------------------------------------------------------------------
// ROADMAP.md Tier 0 "PSQT and beyond" -- Step 8b, king safety (the
// fourth "beyond" term, after mobility, space, and threats):
// KingSafetyWeights / default_king_safety_weights() /
// king_safety_value()'s new nullable-override parameter.
// ---------------------------------------------------------------------

TEST_CASE("default_king_safety_weights: matches every underlying constant exactly, including "
          "all 6 flattened pawn-storm rank fields",
          "[eval][king_safety][tuner]") {
    const KingSafetyWeights defaults = default_king_safety_weights();
    REQUIRE(defaults.shield_mg == kShieldPawnBonus.mg);
    REQUIRE(defaults.shield_eg == kShieldPawnBonus.eg);
    REQUIRE(defaults.open_file_mg == kOpenFileNearKingPenalty.mg);
    REQUIRE(defaults.open_file_eg == kOpenFileNearKingPenalty.eg);
    REQUIRE(defaults.semi_open_file_mg == kSemiOpenFileNearKingPenalty.mg);
    REQUIRE(defaults.semi_open_file_eg == kSemiOpenFileNearKingPenalty.eg);
    REQUIRE(defaults.attack_unit_mg == kAttackUnitPenalty.mg);
    REQUIRE(defaults.attack_unit_eg == kAttackUnitPenalty.eg);
    REQUIRE(defaults.back_rank_mg == kBackRankWeaknessPenalty.mg);
    REQUIRE(defaults.back_rank_eg == kBackRankWeaknessPenalty.eg);

    REQUIRE(defaults.pawn_storm_rank1_mg == kPawnStormPenalty[1].mg);
    REQUIRE(defaults.pawn_storm_rank1_eg == kPawnStormPenalty[1].eg);
    REQUIRE(defaults.pawn_storm_rank2_mg == kPawnStormPenalty[2].mg);
    REQUIRE(defaults.pawn_storm_rank2_eg == kPawnStormPenalty[2].eg);
    REQUIRE(defaults.pawn_storm_rank3_mg == kPawnStormPenalty[3].mg);
    REQUIRE(defaults.pawn_storm_rank3_eg == kPawnStormPenalty[3].eg);
    REQUIRE(defaults.pawn_storm_rank4_mg == kPawnStormPenalty[4].mg);
    REQUIRE(defaults.pawn_storm_rank4_eg == kPawnStormPenalty[4].eg);
    REQUIRE(defaults.pawn_storm_rank5_mg == kPawnStormPenalty[5].mg);
    REQUIRE(defaults.pawn_storm_rank5_eg == kPawnStormPenalty[5].eg);
    REQUIRE(defaults.pawn_storm_rank6_mg == kPawnStormPenalty[6].mg);
    REQUIRE(defaults.pawn_storm_rank6_eg == kPawnStormPenalty[6].eg);
}

TEST_CASE("king_safety_value: passing default_king_safety_weights() as an explicit override "
          "reproduces the no-override result exactly, for both the plain-scalar terms and the "
          "flattened pawn-storm terms",
          "[eval][king_safety][tuner]") {
    init_all();
    const KingSafetyWeights defaults = default_king_safety_weights();

    // Plain-scalar case: same shielded-vs-bare asymmetric setup as this
    // file's own "an intact pawn shield" test above (deliberately
    // asymmetric, not mirror-symmetric, so a bug that silently zeroed a
    // weight couldn't still pass by symmetry).
    Position shielded = empty_position();
    shielded.place_piece(make_square(4, 0), Piece::WhiteKing);
    shielded.place_piece(make_square(3, 1), Piece::WhitePawn);
    shielded.place_piece(make_square(4, 1), Piece::WhitePawn);
    shielded.place_piece(make_square(5, 1), Piece::WhitePawn);
    shielded.place_piece(make_square(7, 7), Piece::BlackKing);
    REQUIRE(king_safety_value(shielded, &defaults) == king_safety_value(shielded));
    REQUIRE(king_safety_value(shielded).mg != 0);

    // Pawn-storm case: same far-advanced setup as this file's own "a
    // pawn storm further advanced" test above.
    Position storm = empty_position();
    storm.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    storm.place_piece(make_square(0, 2), Piece::BlackPawn); // a3 -- relative rank 5
    storm.place_piece(make_square(7, 7), Piece::BlackKing);
    REQUIRE(king_safety_value(storm, &defaults) == king_safety_value(storm));
    REQUIRE(king_safety_value(storm).mg != 0);
}

TEST_CASE("king_safety_value: a modified pawn_storm_rank5 field changes only a rank-5 storm's "
          "own penalty, leaving an unrelated rank untouched",
          "[eval][king_safety][tuner]") {
    init_all();
    // Same far-advanced (relative rank 5) setup as this file's own "a
    // pawn storm further advanced" test and the override-parity test
    // just above.
    Position pos = empty_position();
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    pos.place_piece(make_square(0, 2), Piece::BlackPawn); // a3 -- relative rank 5
    pos.place_piece(make_square(7, 7), Piece::BlackKing);

    // This position is semi-open on the a-file (an enemy pawn present,
    // no own pawn) -- isolate the storm term specifically by comparing
    // deltas rather than raw totals, the same technique this file's own
    // pawn-storm comparison tests already use.
    const Score baseline = king_safety_value(pos);

    KingSafetyWeights perturbed = default_king_safety_weights();
    perturbed.pawn_storm_rank5_mg += 100.0;
    perturbed.pawn_storm_rank5_eg += 50.0;
    // Deliberately also perturb an UNRELATED rank (rank 2) to confirm
    // it has no effect on a position whose only storming pawn is at
    // rank 5 -- the override genuinely only moves the term(s) actually
    // in play, not every pawn-storm field indiscriminately.
    perturbed.pawn_storm_rank2_mg += 9999.0;
    perturbed.pawn_storm_rank2_eg += 9999.0;

    const Score boosted = king_safety_value(pos, &perturbed);
    // The storm penalty is charged against White (the king's own side)
    // -- boosting kPawnStormPenalty[5]'s OWN magnitude by +100/+50 makes
    // it a smaller penalty (less negative), raising White's score by
    // exactly that amount.
    REQUIRE(boosted.mg == baseline.mg + 100);
    REQUIRE(boosted.eg == baseline.eg + 50);
}
