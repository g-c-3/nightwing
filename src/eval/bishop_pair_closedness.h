#pragma once
// src/eval/bishop_pair_closedness.h
//
// Bishop pair scaled by how LOCKED the centre is -- candidate 2 of
// ROADMAP.md's "Restricted nonlinear feature interactions in the
// evaluator" item ("bishop pair scaled by openness of the position").
//
// Why this is a separate term from eval/material_imbalance.h's
// kBishopPairPerMissingPawn: that term already grows the bishop pair's
// value as pawns leave the board, using the TOTAL pawn count as its
// openness proxy. Total pawn count does not distinguish an open
// position from a closed one with the same number of pawns (e.g. a
// locked French/King's Indian centre keeps its pawns on the board but
// shuts the bishops' diagonals). This term measures a different
// quantity: the number of central pawn pairs that are directly
// blocking each other. It is additive to, not a replacement for,
// both kBishopPairBonus (eval/piece_bonuses.h) and
// kBishopPairPerMissingPawn.
//
// Formula: locked = number of White pawns on files c-f that have a
// Black pawn directly in front of them (each such pair counted once).
// Each side that owns two or more bishops receives
// kBishopPairPerLockedCentralPair * locked (a penalty, negative
// constants), so the two bishops' combined advantage shrinks as the
// centre closes. Flank files (a, b, g, h) are excluded on purpose:
// locked wing pawns do not shut the central diagonals the pair relies
// on, and including them would blur what the term measures.
//
// Same "few hand-estimated constants, plain formula" approach as every
// other eval/*.h module: the constants are first-draft estimates, not
// yet Texel-tuned. No code copied from any engine; the underlying idea
// (bishops dislike closed centres) is standard, see CPW's "Bishop Pair"
// (https://www.chessprogramming.org/Bishop_Pair) and "Bad Bishop"
// (https://www.chessprogramming.org/Bad_Bishop) articles.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Penalty (negative Score values) applied, per locked central pawn
/// pair, to each side that owns the bishop pair. `mg` slightly larger
/// in magnitude than `eg`: locked centres occur mostly in the
/// middlegame, and in the endgame fewer pawns are on the board to
/// remain locked in the first place.
inline constexpr Score kBishopPairPerLockedCentralPair = {-4, -3};

/// Counts central (files c-f) pawn pairs that directly block each
/// other: a White pawn with a Black pawn on the square immediately in
/// front of it. Each pair is counted once. Exposed for tests.
[[nodiscard]] int locked_central_pawn_pairs(const board::Position& pos) noexcept;

/// Evaluates the locked-centre scaling of the bishop pair for BOTH
/// sides and returns a single White-relative Score (positive favors
/// White, matching every other eval/*.h term's sign convention).
///
/// Precondition: none beyond the mandatory startup sequence; this
/// function only counts pieces and shifts pawn bitboards, touching no
/// attack table.
[[nodiscard]] Score bishop_pair_closedness_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
