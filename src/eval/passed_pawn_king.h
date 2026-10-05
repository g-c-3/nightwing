#pragma once
// src/eval/passed_pawn_king.h
//
// Passed-pawn king proximity: the first "restricted nonlinear feature
// interaction" (ROADMAP.md, added 2026-10-02). A passed pawn's value
// depends on where the two kings stand relative to its stop square (the
// square directly in front of it): a far enemy king cannot catch it, a near
// own king can escort it. The static passed-pawn bonus in eval/pawns.h
// knows nothing about the kings (and, being cached by pawn hash, cannot),
// so this term multiplies a per-rank weight by the king-distance difference.
//
// Idea: CPW (Chess Programming Wiki) "Passed Pawn" (king distance to the
// stop square) and the same king-proximity idea used in Stockfish-classic's
// passed-pawn evaluation. From-scratch implementation: the constants and
// formula below are original first-draft estimates, not copied, and are not
// yet tuned.
//
// Endgame-only (mg = 0): in the middlegame the kings are rarely near passed
// pawns and the distance carries little meaning.
//
// Per ROADMAP.md, this interaction is accepted only after an SPRT win; if it
// fails, it is removed rather than kept as dead code.

#include <array>

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Per-RELATIVE-rank weight of the king-distance difference (0 = the pawn's
/// own back rank, 6 = one step from promoting; same convention as
/// kPassedPawnBonus in eval/pawns.h). Ranks below 3 are zeroed: a far-back
/// passer is rarely decided by king distance. Index 7 never occurs.
inline constexpr std::array<int, 8> kPassedKingRankWeight = {0, 0, 0, 1, 2, 3, 5, 0};

/// Multiplier on the ENEMY king's Chebyshev distance to the stop square
/// (a farther enemy king is better for the pawn's owner).
inline constexpr int kPassedEnemyKingDistFactor = 2;

/// Multiplier on the OWN king's Chebyshev distance to the stop square
/// (a nearer own king is better for the pawn's owner).
inline constexpr int kPassedOwnKingDistFactor = 1;

/// Distances are capped at this value before weighting, so a king already
/// far away stops mattering (a pawn far from both kings is unaffected by
/// further distance).
inline constexpr int kPassedKingDistCap = 5;

/// Returns the White-relative passed-pawn king-proximity Score (positive
/// favors White; mg is always 0). For each passed pawn the endgame value is
///   kPassedKingRankWeight[rel_rank] *
///     (kPassedEnemyKingDistFactor * min(enemy_king_dist, cap) -
///      kPassedOwnKingDistFactor   * min(own_king_dist,   cap))
/// where the distances are Chebyshev distances to the stop square.
///
/// Precondition: board::init_masks() has been called (passed_pawn_mask()).
/// Does not need the sliding-piece attack tables.
[[nodiscard]] Score passed_pawn_king_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
