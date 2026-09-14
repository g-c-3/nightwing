#pragma once
// src/eval/pawns.h
//
// Pawn structure evaluation: passed, isolated, doubled, backward, and
// connected pawns, plus a connected-PASSED-pawns bonus (ROADMAP.md
// Phase 5's "Pawn structure" item, extended by a later Priority Fixes
// item; see kConnectedPassedPawnBonus's own doc comment below). Concepts
// and general shape are the standard, widely-published ones described
// on the Chess Programming Wiki ("Passed Pawn", "Isolated Pawn",
// "Doubled Pawn", "Backward Pawn", "Connected Pawns") -- from-scratch
// implementation here, no code copied, per ARCHITECTURE.md's Attribution
// Policy. The specific centipawn constants in pawns.cpp are first-draft
// hand estimates, not yet Texel-tuned (ROADMAP.md Phase 5's tuner item
// lands later and is expected to revise every constant in this file).
//
// Deliberately its own translation unit, separate from eval.cpp, mainly
// for docs/DECISIONS.md's forward-looking reason: ROADMAP.md's "Pawn
// hash table" item (Phase 3) exists specifically to cache this
// function's result keyed on pawn structure alone (pawn placement is
// far less volatile move-to-move than the rest of the position), and
// that caching only makes sense wrapped around a single, clearly-scoped
// function call rather than inlined into evaluate()'s general per-square
// loop.

#include <array>

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Passed-pawn bonus indexed by the pawn's own RELATIVE rank (0 = its
/// own back rank, 6 = one step from promoting -- see pawns.cpp's
/// relative_rank()). Indices 0 and 7 never occur for a real pawn in a
/// legal position and are zeroed only so the table doesn't need a
/// separate bounds check. CPW "Passed Pawn": the general shape --
/// monotonically increasing toward promotion -- is the standard,
/// well-known one; these specific centipawn values are a first-draft
/// hand estimate, not yet Texel-tuned (ROADMAP.md Phase 5's tuner item
/// lands later and is expected to revise every constant in this file --
/// exposed here, rather than kept file-private in pawns.cpp, so that
/// tuner and tests/pawns_tests.cpp can both reach them by name).
inline constexpr std::array<Score, 8> kPassedPawnBonus = {{
    {0, 0},
    {5, 10},
    {10, 20},
    {20, 35},
    {35, 55},
    {55, 80},
    {80, 110},
    {0, 0},
}};

/// CPW "Isolated Pawn": no friendly pawn on either adjacent file,
/// anywhere -- a structural weakness regardless of game phase, but
/// slightly more exploitable in the endgame once there are fewer pieces
/// around to shield or blockade it, hence the larger `eg` magnitude.
inline constexpr Score kIsolatedPawnPenalty = {-10, -20};

/// CPW "Doubled Pawn": applied once per pawn that shares its file with
/// at least one other friendly pawn -- so two pawns on a file both incur
/// it (not just the rearmost/"extra" one). A deliberately simple,
/// common convention: it slightly over-penalizes relative to "only the
/// extra pawns beyond the first," but stays correct and well-defined
/// for tripled pawns too, and Phase 5's Texel tuner will fit the actual
/// magnitude either way once it's online.
inline constexpr Score kDoubledPawnPenalty = {-10, -20};

/// CPW "Backward Pawn": see pawn_structure_value()'s doc comment below
/// for the exact two-part test. A real, if narrower, weakness than
/// isolation -- smaller magnitude accordingly.
inline constexpr Score kBackwardPawnPenalty = {-8, -12};

/// CPW "Connected Pawns": a pawn defended by, or standing directly
/// beside (phalanx), another friendly pawn. A small per-pawn bonus
/// (mutual, so a defended/phalanx PAIR nets roughly double this) --
/// pawn chains and phalanxes are generally sturdier than lone pawns.
inline constexpr Score kConnectedPawnBonus = {5, 8};

/// Connected PASSED pawns (ROADMAP.md's own item, Priority Fixes
/// (2026-09-08) section) -- an ADDITIONAL bonus, on top of both
/// kPassedPawnBonus and kConnectedPawnBonus above (each already applies
/// independently to a pawn that happens to be both), for the specific
/// case where a passed pawn is ALSO defended-by-or-phalanx-with ANOTHER
/// passed pawn specifically -- not just any friendly pawn. CPW doesn't
/// have a single dedicated article for this exact combination, but
/// describes the underlying idea across "Passed Pawn" and "Connected
/// Pawns": a mutually-defending passed PAIR is harder to stop than
/// either pawn would be alone (the defender can recapture if the enemy
/// king or a piece takes on the advancer, and vice versa), so it's
/// worth more than the sum of two individually-scored passers. Indexed
/// by the SAME relative-rank convention as kPassedPawnBonus (0 = own
/// back rank, 7 = promotion -- indices 0 and 7 never occur for a real
/// pawn and are zeroed for the identical reason kPassedPawnBonus's own
/// comment gives), and applied ONCE PER PAWN in the pair (so, like
/// kConnectedPawnBonus, a genuine pair nets roughly double this).
/// First-draft hand estimate, scaled to roughly half of
/// kPassedPawnBonus's own magnitude at each rank -- a meaningful but
/// deliberately secondary bonus, not a replacement for it -- not yet
/// Texel-tuned, same caveat as every other constant in this file.
inline constexpr std::array<Score, 8> kConnectedPassedPawnBonus = {{
    {0, 0},
    {3, 5},
    {5, 10},
    {10, 18},
    {18, 28},
    {28, 40},
    {40, 55},
    {0, 0},
}};

/// Candidate passed pawn (CPW "Candidate Passed Pawn", Priority Fixes
/// (2026-09-08) section) -- a pawn that is NOT yet passed, but would
/// become passed after a plausible, forceable sequence of pawn trades.
/// See pawn_structure_value()'s own doc comment for the exact two-part
/// test this project uses (a deliberately simplified, from-scratch
/// formulation of the general CPW idea, not a literal search over every
/// possible trade sequence). Indexed by the SAME relative-rank
/// convention as kPassedPawnBonus (0 = own back rank, 7 = promotion --
/// indices 0 and 7 zeroed for the identical reason). Scaled to roughly
/// 40% of kPassedPawnBonus's own magnitude at each rank -- real, but
/// deliberately smaller than an ALREADY-passed pawn's own bonus, since
/// becoming passed here is conditional on a trade actually happening,
/// not yet a certainty. First-draft hand estimate, not yet Texel-tuned,
/// same caveat as every other constant in this file.
inline constexpr std::array<Score, 8> kCandidatePassedPawnBonus = {{
    {0, 0},
    {2, 4},
    {4, 8},
    {8, 14},
    {14, 22},
    {22, 32},
    {32, 44},
    {0, 0},
}};

/// Minimum file-distance (see is_outside_passed_pawn(), pawns.cpp) a
/// passed pawn must have from every OTHER pawn on the board (either
/// color) to count as "outside" -- see kOutsidePassedPawnBonus's own
/// doc comment below for why 3. First-draft hand estimate, not yet
/// Texel-tuned/SPRT-validated, same caveat as every other constant in
/// this file (though this one is a distance threshold, not a
/// centipawn value, so a future tuner pass may need to treat it
/// differently from the Score-valued constants around it).
inline constexpr int kOutsidePassedPawnMinFileGap = 3;

/// Outside passed pawn (CPW "Outside Passed Pawn", Priority Fixes
/// (2026-09-08) section) -- an ADDITIONAL bonus, on top of
/// kPassedPawnBonus above (which already applies to any passed pawn
/// regardless of location), for a passed pawn that sits meaningfully
/// separated from the rest of the pawns on the board (either color).
/// The practical value CPW describes: such a pawn can decoy the
/// defending king far away to stop it, letting the attacking king
/// infiltrate and win material elsewhere -- a real endgame technique,
/// not merely "a passed pawn is nice." See
/// is_outside_passed_pawn()'s own doc comment (pawns.cpp) for the exact
/// test: the minimum file-distance from this pawn to every OTHER pawn
/// on the board must be at least kOutsidePassedPawnMinFileGap (3) --
/// wide enough that no other pawn's own natural advance could ever
/// interact with this one, matching the "so far away it's decisive"
/// spirit of the CPW concept, without attempting the fuller "count the
/// actual king-race tempo" analysis a truly precise version would need.
/// Indexed by the SAME relative-rank convention as kPassedPawnBonus (0
/// = own back rank, 7 = promotion -- indices 0 and 7 zeroed for the
/// identical reason), scaled to roughly 30% of kPassedPawnBonus's own
/// magnitude at each rank -- smaller than kConnectedPassedPawnBonus's
/// own 50%, since being merely far away is a weaker, more situational
/// asset than having a genuine mutual defender. First-draft hand
/// estimate, not yet Texel-tuned, same caveat as every other constant
/// in this file.
inline constexpr std::array<Score, 8> kOutsidePassedPawnBonus = {{
    {0, 0},
    {2, 3},
    {3, 6},
    {6, 11},
    {11, 17},
    {17, 24},
    {24, 33},
    {0, 0},
}};

/// Pawn islands (CPW "Pawn Islands", Priority Fixes (2026-09-08)
/// section) -- unlike every other constant in this file, this is a
/// PER-SIDE structural penalty, not a per-pawn one: a "pawn island" is
/// a maximal run of consecutive files that each contain at least one
/// own pawn, separated from the next island by at least one
/// completely-pawnless file in between. One island (all own pawns
/// occupying a single contiguous file-range, however wide) is the
/// baseline, healthiest shape and gets no penalty at all; this constant
/// is charged ONCE PER ISLAND BEYOND THE FIRST (so 2 islands costs one
/// charge, 3 islands costs two, etc.) -- CPW's own general point: more
/// islands means more separately-defensible pawn groups, which in
/// practice correlates with more isolated pawns and more squares a
/// king or piece has to cover to protect them all, even though this
/// specific constant is deliberately blind to WHICH files are involved
/// or how many pawns are in each island, just the count of islands
/// itself. First-draft hand estimate, not yet Texel-tuned, same caveat
/// as every other constant in this file.
inline constexpr Score kPawnIslandPenalty = {-4, -6};

/// Evaluates pawn structure for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching
/// material_value()/psqt_value()'s sign convention in eval.cpp).
///
/// Backward-pawn test (CPW "Backward Pawn"), spelled out here since
/// kBackwardPawnPenalty's own comment refers back to this one: a pawn
/// is backward when BOTH (a) no friendly pawn stands on an adjacent
/// file at its rank or further back (nothing could ever advance to
/// shoulder up beside or defend it), AND (b) the square directly ahead
/// of it is attacked by an enemy pawn (advancing there would just lose
/// the pawn) -- CPW's standard two-part definition. Only ever checked
/// for a pawn that isn't already passed (pawns.cpp's loop skips it as a
/// pure efficiency short-circuit -- a passed pawn has no enemy pawn
/// anywhere ahead of it by definition, so part (b) can never hold for
/// one anyway).
///
/// Candidate-passed-pawn test (CPW "Candidate Passed Pawn", used for
/// kCandidatePassedPawnBonus above), also only ever checked for a pawn
/// that isn't already passed: (a) no enemy pawn stands anywhere ahead
/// of it on its OWN file -- a straight-ahead blocker can never be
/// removed by a pawn trade (pawns never capture straight ahead), so a
/// pawn failing this can never become passed via trades alone,
/// regardless of the adjacent-file count below; AND (b) among the up to
/// two ADJACENT files only, the number of own pawns standing at or
/// behind this pawn's own rank is >= the number of enemy pawns standing
/// ahead of it -- a simple, symmetric "trade count" argument: if own
/// pawns numerically match or outnumber the enemy pawns capable of
/// blocking/capturing on those diagonals, a forced sequence of
/// exchanges there can clear them without this side ending up
/// outnumbered on those files. Deliberately simpler than a literal
/// search over every capture sequence (see kCandidatePassedPawnBonus's
/// own doc comment, pawns.h) -- a first-draft heuristic, not a
/// guaranteed-correct trade resolver.
///
/// Outside-passed-pawn test (CPW "Outside Passed Pawn", used for
/// kOutsidePassedPawnBonus above), only ever checked for a pawn that IS
/// already passed (the opposite gating from the backward/candidate
/// checks just above): the minimum file-distance from this pawn to
/// every OTHER pawn on the board, either color, must be at least
/// kOutsidePassedPawnMinFileGap. See is_outside_passed_pawn()'s own doc
/// comment, pawns.cpp, for the exact bitboard mechanics.
///
/// Pawn islands (CPW "Pawn Islands", used for kPawnIslandPenalty
/// above) -- the ONLY per-SIDE (not per-pawn) term in this file: a
/// scan across all 8 files, once per side, counting maximal runs of
/// consecutive own-pawn-occupied files. See kPawnIslandPenalty's own
/// doc comment (pawns.h) for exactly how the resulting count is
/// charged.
///
/// Precondition: board::init_masks() has been called (this function
/// uses board::passed_pawn_mask()/backward_support_mask()/
/// adjacent_files_mask(), transitively requiring it).
[[nodiscard]] Score pawn_structure_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
