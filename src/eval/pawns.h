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
#include "eval/psqt.h" // round_to_int()
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

/// Runtime-mutable counterpart to every constant above -- the pawn-
/// structure-term entry in the same "runtime-mutable parameter-vector
/// abstraction over eval's currently-constexpr named constants" family
/// as eval::MaterialWeights/PsqtWeights (psqt.h), eval::MobilityWeights
/// (mobility.h), eval::SpaceWeights (space.h), eval::ThreatsWeights
/// (threats.h), and eval::KingSafetyWeights (king_safety.h) already
/// establish (ROADMAP.md's Tier 0 tuner item, "PSQT and beyond" — Step
/// 8b, the fifth and final "beyond" term).
///
/// DESIGN DECISIONS (docs/DECISIONS.md has the full account for both):
///
/// (1) Array flattening: this file's FOUR 8-entry relative-rank-indexed
/// arrays (kPassedPawnBonus, kConnectedPassedPawnBonus,
/// kOutsidePassedPawnBonus, kCandidatePassedPawnBonus) are each flattened into 6 individually-named
/// scalar field pairs, indices 1 through 6 only — the exact same
/// resolution KingSafetyWeights already applied to kPawnStormPenalty's
/// own single 8-entry array (king_safety.h's own doc comment has the
/// full rationale: `tuner::ParameterRef::array_member`'s type is
/// hardcoded to `std::array<double, 64>` for PSQT's own 64-square case,
/// not generic over array size), just applied three times over instead
/// of once. Indices 0 and 7 are unreachable placeholders in every one
/// of the three arrays (pawns.cpp's own relative_rank() never produces
/// them for a real pawn) and are correspondingly not represented as
/// tunable fields at all, matching KingSafetyWeights' identical
/// treatment of kPawnStormPenalty's own indices 0/7.
///
/// (2) kOutsidePassedPawnMinFileGap (a plain `int` file-distance
/// threshold, not a `Score`) is represented as a single `double` field,
/// `outside_min_file_gap`, exactly the same way every OTHER field in
/// this struct is a `double` rather than an `int`/`Score` — rounded to
/// an `int` via round_to_int() at the point of use
/// (is_outside_passed_pawn(), pawns.cpp) the same way every mg/eg
/// `Score` field already is. This keeps `PawnsWeights` uniform (every
/// field is a plain `double Weights::* member`, matching every sibling
/// `Weights` struct's own `ParameterRef` shape, with none of them
/// needing a special integer-typed exception) at the cost of a
/// documented caveat: unlike a centipawn `Score` term, this threshold
/// enters `pawn_structure_value()` through a discrete
/// `>=`-comparison (is_outside_passed_pawn()), not a continuous linear
/// contribution to the score — its TRUE gradient with respect to the
/// evaluated position is exactly zero almost everywhere (only changing,
/// discontinuously, at each integer file-distance boundary), so a
/// naive numerical-gradient tuning pass over this specific field may
/// behave differently (noisier, or effectively untunable via small
/// finite-difference steps) than every other field in this struct. Not
/// resolved further this session — flagged here for whichever session
/// actually builds the gradient/tuning consumer for this table.
///
/// `constexpr`-constructible via default-member-initializers naming
/// the underlying constants directly, the same MaterialWeights/
/// MobilityWeights/SpaceWeights/ThreatsWeights/KingSafetyWeights
/// pattern (not PsqtWeights' out-of-line one) -- every constant
/// referenced here is `inline constexpr`, declared right in this
/// header, exactly like those five siblings' own underlying constants.
struct PawnsWeights {
    double isolated_mg = kIsolatedPawnPenalty.mg;
    double isolated_eg = kIsolatedPawnPenalty.eg;
    double doubled_mg = kDoubledPawnPenalty.mg;
    double doubled_eg = kDoubledPawnPenalty.eg;
    double backward_mg = kBackwardPawnPenalty.mg;
    double backward_eg = kBackwardPawnPenalty.eg;
    double connected_mg = kConnectedPawnBonus.mg;
    double connected_eg = kConnectedPawnBonus.eg;

    // kPassedPawnBonus[1..6] flattened -- see this struct's own doc
    // comment above (design decision 1) for why indices 0/7 aren't
    // represented at all.
    double passed_rank1_mg = kPassedPawnBonus[1].mg;
    double passed_rank1_eg = kPassedPawnBonus[1].eg;
    double passed_rank2_mg = kPassedPawnBonus[2].mg;
    double passed_rank2_eg = kPassedPawnBonus[2].eg;
    double passed_rank3_mg = kPassedPawnBonus[3].mg;
    double passed_rank3_eg = kPassedPawnBonus[3].eg;
    double passed_rank4_mg = kPassedPawnBonus[4].mg;
    double passed_rank4_eg = kPassedPawnBonus[4].eg;
    double passed_rank5_mg = kPassedPawnBonus[5].mg;
    double passed_rank5_eg = kPassedPawnBonus[5].eg;
    double passed_rank6_mg = kPassedPawnBonus[6].mg;
    double passed_rank6_eg = kPassedPawnBonus[6].eg;

    // kConnectedPassedPawnBonus[1..6] flattened -- same convention.
    double connected_passed_rank1_mg = kConnectedPassedPawnBonus[1].mg;
    double connected_passed_rank1_eg = kConnectedPassedPawnBonus[1].eg;
    double connected_passed_rank2_mg = kConnectedPassedPawnBonus[2].mg;
    double connected_passed_rank2_eg = kConnectedPassedPawnBonus[2].eg;
    double connected_passed_rank3_mg = kConnectedPassedPawnBonus[3].mg;
    double connected_passed_rank3_eg = kConnectedPassedPawnBonus[3].eg;
    double connected_passed_rank4_mg = kConnectedPassedPawnBonus[4].mg;
    double connected_passed_rank4_eg = kConnectedPassedPawnBonus[4].eg;
    double connected_passed_rank5_mg = kConnectedPassedPawnBonus[5].mg;
    double connected_passed_rank5_eg = kConnectedPassedPawnBonus[5].eg;
    double connected_passed_rank6_mg = kConnectedPassedPawnBonus[6].mg;
    double connected_passed_rank6_eg = kConnectedPassedPawnBonus[6].eg;

    // kOutsidePassedPawnMinFileGap flattened to a single double field --
    // see this struct's own doc comment above (design decision 2).
    double outside_min_file_gap = kOutsidePassedPawnMinFileGap;

    // kOutsidePassedPawnBonus[1..6] flattened -- same convention as
    // kPassedPawnBonus/kConnectedPassedPawnBonus above.
    double outside_passed_rank1_mg = kOutsidePassedPawnBonus[1].mg;
    double outside_passed_rank1_eg = kOutsidePassedPawnBonus[1].eg;
    double outside_passed_rank2_mg = kOutsidePassedPawnBonus[2].mg;
    double outside_passed_rank2_eg = kOutsidePassedPawnBonus[2].eg;
    double outside_passed_rank3_mg = kOutsidePassedPawnBonus[3].mg;
    double outside_passed_rank3_eg = kOutsidePassedPawnBonus[3].eg;
    double outside_passed_rank4_mg = kOutsidePassedPawnBonus[4].mg;
    double outside_passed_rank4_eg = kOutsidePassedPawnBonus[4].eg;
    double outside_passed_rank5_mg = kOutsidePassedPawnBonus[5].mg;
    double outside_passed_rank5_eg = kOutsidePassedPawnBonus[5].eg;
    double outside_passed_rank6_mg = kOutsidePassedPawnBonus[6].mg;
    double outside_passed_rank6_eg = kOutsidePassedPawnBonus[6].eg;

    // kCandidatePassedPawnBonus[1..6] flattened -- same convention as
    // kPassedPawnBonus/kConnectedPassedPawnBonus/kOutsidePassedPawnBonus
    // above.
    double candidate_passed_rank1_mg = kCandidatePassedPawnBonus[1].mg;
    double candidate_passed_rank1_eg = kCandidatePassedPawnBonus[1].eg;
    double candidate_passed_rank2_mg = kCandidatePassedPawnBonus[2].mg;
    double candidate_passed_rank2_eg = kCandidatePassedPawnBonus[2].eg;
    double candidate_passed_rank3_mg = kCandidatePassedPawnBonus[3].mg;
    double candidate_passed_rank3_eg = kCandidatePassedPawnBonus[3].eg;
    double candidate_passed_rank4_mg = kCandidatePassedPawnBonus[4].mg;
    double candidate_passed_rank4_eg = kCandidatePassedPawnBonus[4].eg;
    double candidate_passed_rank5_mg = kCandidatePassedPawnBonus[5].mg;
    double candidate_passed_rank5_eg = kCandidatePassedPawnBonus[5].eg;
    double candidate_passed_rank6_mg = kCandidatePassedPawnBonus[6].mg;
    double candidate_passed_rank6_eg = kCandidatePassedPawnBonus[6].eg;

    double island_mg = kPawnIslandPenalty.mg;
    double island_eg = kPawnIslandPenalty.eg;
};

/// Returns a PawnsWeights matching every one of the constants above
/// exactly -- the natural starting point for a pawn-structure-aware
/// tuning run (tuner::tune()-style, src/tuner/tune.h), and the values
/// every field above is already separately, redundantly initialized to
/// (kept in sync by hand, matching every sibling Weights struct's own
/// doc comment rationale for why: PawnsWeights{}'s own default-member-
/// initializers stay self-contained and don't require calling a
/// function just to default-construct one).
[[nodiscard]] constexpr PawnsWeights default_pawns_weights() noexcept {
    return PawnsWeights{
        /*isolated_mg=*/kIsolatedPawnPenalty.mg,
        /*isolated_eg=*/kIsolatedPawnPenalty.eg,
        /*doubled_mg=*/kDoubledPawnPenalty.mg,
        /*doubled_eg=*/kDoubledPawnPenalty.eg,
        /*backward_mg=*/kBackwardPawnPenalty.mg,
        /*backward_eg=*/kBackwardPawnPenalty.eg,
        /*connected_mg=*/kConnectedPawnBonus.mg,
        /*connected_eg=*/kConnectedPawnBonus.eg,

        /*passed_rank1_mg=*/kPassedPawnBonus[1].mg,
        /*passed_rank1_eg=*/kPassedPawnBonus[1].eg,
        /*passed_rank2_mg=*/kPassedPawnBonus[2].mg,
        /*passed_rank2_eg=*/kPassedPawnBonus[2].eg,
        /*passed_rank3_mg=*/kPassedPawnBonus[3].mg,
        /*passed_rank3_eg=*/kPassedPawnBonus[3].eg,
        /*passed_rank4_mg=*/kPassedPawnBonus[4].mg,
        /*passed_rank4_eg=*/kPassedPawnBonus[4].eg,
        /*passed_rank5_mg=*/kPassedPawnBonus[5].mg,
        /*passed_rank5_eg=*/kPassedPawnBonus[5].eg,
        /*passed_rank6_mg=*/kPassedPawnBonus[6].mg,
        /*passed_rank6_eg=*/kPassedPawnBonus[6].eg,

        /*connected_passed_rank1_mg=*/kConnectedPassedPawnBonus[1].mg,
        /*connected_passed_rank1_eg=*/kConnectedPassedPawnBonus[1].eg,
        /*connected_passed_rank2_mg=*/kConnectedPassedPawnBonus[2].mg,
        /*connected_passed_rank2_eg=*/kConnectedPassedPawnBonus[2].eg,
        /*connected_passed_rank3_mg=*/kConnectedPassedPawnBonus[3].mg,
        /*connected_passed_rank3_eg=*/kConnectedPassedPawnBonus[3].eg,
        /*connected_passed_rank4_mg=*/kConnectedPassedPawnBonus[4].mg,
        /*connected_passed_rank4_eg=*/kConnectedPassedPawnBonus[4].eg,
        /*connected_passed_rank5_mg=*/kConnectedPassedPawnBonus[5].mg,
        /*connected_passed_rank5_eg=*/kConnectedPassedPawnBonus[5].eg,
        /*connected_passed_rank6_mg=*/kConnectedPassedPawnBonus[6].mg,
        /*connected_passed_rank6_eg=*/kConnectedPassedPawnBonus[6].eg,

        /*outside_min_file_gap=*/kOutsidePassedPawnMinFileGap,

        /*outside_passed_rank1_mg=*/kOutsidePassedPawnBonus[1].mg,
        /*outside_passed_rank1_eg=*/kOutsidePassedPawnBonus[1].eg,
        /*outside_passed_rank2_mg=*/kOutsidePassedPawnBonus[2].mg,
        /*outside_passed_rank2_eg=*/kOutsidePassedPawnBonus[2].eg,
        /*outside_passed_rank3_mg=*/kOutsidePassedPawnBonus[3].mg,
        /*outside_passed_rank3_eg=*/kOutsidePassedPawnBonus[3].eg,
        /*outside_passed_rank4_mg=*/kOutsidePassedPawnBonus[4].mg,
        /*outside_passed_rank4_eg=*/kOutsidePassedPawnBonus[4].eg,
        /*outside_passed_rank5_mg=*/kOutsidePassedPawnBonus[5].mg,
        /*outside_passed_rank5_eg=*/kOutsidePassedPawnBonus[5].eg,
        /*outside_passed_rank6_mg=*/kOutsidePassedPawnBonus[6].mg,
        /*outside_passed_rank6_eg=*/kOutsidePassedPawnBonus[6].eg,

        /*candidate_passed_rank1_mg=*/kCandidatePassedPawnBonus[1].mg,
        /*candidate_passed_rank1_eg=*/kCandidatePassedPawnBonus[1].eg,
        /*candidate_passed_rank2_mg=*/kCandidatePassedPawnBonus[2].mg,
        /*candidate_passed_rank2_eg=*/kCandidatePassedPawnBonus[2].eg,
        /*candidate_passed_rank3_mg=*/kCandidatePassedPawnBonus[3].mg,
        /*candidate_passed_rank3_eg=*/kCandidatePassedPawnBonus[3].eg,
        /*candidate_passed_rank4_mg=*/kCandidatePassedPawnBonus[4].mg,
        /*candidate_passed_rank4_eg=*/kCandidatePassedPawnBonus[4].eg,
        /*candidate_passed_rank5_mg=*/kCandidatePassedPawnBonus[5].mg,
        /*candidate_passed_rank5_eg=*/kCandidatePassedPawnBonus[5].eg,
        /*candidate_passed_rank6_mg=*/kCandidatePassedPawnBonus[6].mg,
        /*candidate_passed_rank6_eg=*/kCandidatePassedPawnBonus[6].eg,

        /*island_mg=*/kPawnIslandPenalty.mg,
        /*island_eg=*/kPawnIslandPenalty.eg,
    };
}

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
///
/// `weights`, if non-null, is used INSTEAD OF the constants above for
/// this call only -- the same nullable-override convention
/// material_value()/psqt_value()/mobility_value()/space_value()/
/// threats_value()/king_safety_value() already establish, threaded
/// here for eval::evaluate()'s own `pawns_weights` parameter (eval.h)
/// so a future pawn-structure-aware tuning run can probe candidate
/// pawn-structure weight vectors the same uniform way it already can
/// for the other five terms. See PawnsWeights' own doc comment (above)
/// for why the 3 relative-rank-indexed arrays are individually named
/// rather than array-indexed, and why the one non-Score constant
/// (kOutsidePassedPawnMinFileGap) is represented as a plain `double`
/// field the same as every other field, rounded via round_to_int() at
/// the point of use.
[[nodiscard]] Score pawn_structure_value(const board::Position& pos,
                                          const PawnsWeights* weights = nullptr) noexcept;

} // namespace nightwing::eval
