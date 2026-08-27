#pragma once
// src/eval/king_safety.h
//
// King safety evaluation (ROADMAP.md Phase 5's "King safety" item):
// rewards a king with an intact pawn shield and penalizes open/
// semi-open files near it and enemy pieces bearing down on its
// immediate surroundings — the standard CPW "King Safety" concept
// (https://www.chessprogramming.org/King_Safety) — from-scratch
// implementation here, no code copied, per ARCHITECTURE.md's
// Attribution Policy.
//
// Three components, each a simple, hand-estimated additive term (same
// "few constants, not a large tuned table" preference eval/pawns.h and
// eval/mobility.h both already establish, ahead of ROADMAP.md Phase 5's
// eventual Texel tuner):
//   - Pawn shield: a flat per-pawn bonus for each own pawn found in the
//     3-file-wide, 2-rank-deep zone directly in front of the king.
//   - Open/semi-open files near the king: a penalty for each of the
//     king's own file and its two adjacent files that has no own pawn
//     on it (worse still if it has no pawn of EITHER color — "fully
//     open" — than if the enemy still has a pawn there — "semi-open").
//   - Attacker weighting: a penalty scaled by how many enemy
//     knights/bishops/rooks/queens attack at least one square in the
//     king's immediate zone (the king's own square plus every square a
//     king there could move to), weighted more heavily for more
//     valuable attacking piece types.
//
// Deliberately MG-heavy, EG-light in every constant below (mobility.cpp
// mirrors this same "eg at or above mg" preference for the OPPOSITE
// reason — mobility matters more, not less, as pieces come off the
// board; king safety is the other way around: with queens and rooks
// traded off, there's usually far less to actually attack a king WITH,
// and an exposed king often becomes an asset (see eval/psqt.h's own
// king centralization terms) rather than a liability). This lets
// eval::taper() (eval/score.h) fade this whole term out naturally as
// the game phase drops, the same mechanism every other tapered term in
// this codebase already relies on, rather than this file needing any
// special-cased "only apply in the middlegame" logic of its own.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Flat bonus per own pawn found in the king's pawn-shield zone (see
/// this file's header comment). Capped implicitly at 6 (the shield
/// zone's own maximum size — 3 files × 2 ranks, clipped further near
/// the board edge), never explicitly capped in king_safety.cpp itself.
inline constexpr Score kShieldPawnBonus = {6, 1};

/// Penalty per fully open file (no pawn of EITHER color) among the
/// king's own file and its two neighbors. The larger-magnitude of the
/// two file penalties: a fully open file is the most dangerous case, no
/// pawn of any color to block or trade off an attacking rook/queen
/// before it reaches the back rank.
inline constexpr Score kOpenFileNearKingPenalty = {-24, -4};

/// Penalty per semi-open file (no OWN pawn, but an enemy pawn still
/// present) among the king's own file and its two neighbors — real, but
/// smaller than kOpenFileNearKingPenalty: an enemy pawn there is still
/// itself an obstacle an attacking piece has to get past or trade off
/// first.
inline constexpr Score kSemiOpenFileNearKingPenalty = {-12, -2};

/// Penalty per weighted "attack unit" (see king_safety.cpp's own
/// per-piece-type weights) among enemy knights/bishops/rooks/queens that
/// attack at least one square of the king's immediate zone.
inline constexpr Score kAttackUnitPenalty = {-6, -1};

/// Evaluates king safety for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp). See this file's header
/// comment for the three components summed into each side's own
/// contribution before being combined White-minus-Black.
///
/// Precondition: board::init_masks() and board::init_magic_bitboards()
/// have both been called (this function uses board::king_attacks() and,
/// via the same attacker-weighting logic eval/mobility.h's
/// mobility_value() already relies on, board::bishop_attacks()/
/// rook_attacks()/queen_attacks() too).
[[nodiscard]] Score king_safety_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
