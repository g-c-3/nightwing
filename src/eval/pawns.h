#pragma once
// src/eval/pawns.h
//
// Pawn structure evaluation: passed, isolated, doubled, backward, and
// connected pawns (ROADMAP.md Phase 5's "Pawn structure" item). Concepts
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
/// Precondition: board::init_masks() has been called (this function
/// uses board::passed_pawn_mask()/backward_support_mask()/
/// adjacent_files_mask(), transitively requiring it).
[[nodiscard]] Score pawn_structure_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
