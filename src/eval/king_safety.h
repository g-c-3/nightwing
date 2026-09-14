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
// Four components, each a simple, hand-estimated additive term (same
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
//   - Pawn storms (a later Priority Fixes item, ROADMAP.md 2026-09-08 —
//     the AGGRESSIVE complement to the 3 defensive terms above): a
//     penalty, on the king-owner's own side of the ledger, for the
//     ENEMY's own most-advanced pawn on each of the king's file and its
//     2 neighbors, scaled by how far that pawn has advanced. A pawn
//     storm rolling toward the king is a real, distinct threat even on
//     a file that isn't yet fully open — the open/semi-open check above
//     only measures whether the KING'S OWN structure has already broken
//     down, not whether the enemy's is actively advancing to break it
//     down.
//   - Back-rank weakness (another Priority Fixes item, ROADMAP.md
//     2026-09-08): a penalty when the king sits on its own back rank
//     with every existing square directly in front of it (up to 3 --
//     fewer on the a/h files) occupied by an own pawn, leaving no
//     pawn-created "luft" square to step up to off the back rank, AND
//     the enemy still has at least one rook or queen on the board (the
//     specific piece types that actually deliver a back-rank check/mate
//     along an open rank or file -- without one, the weakness is purely
//     theoretical, nothing left on the board to exploit it). See
//     king_safety.cpp's own doc comment on the exact test. Distinct from
//     the pawn-shield component above: a full 3-pawn shield still
//     directly in front of the king is exactly the condition that
//     traps it here, so this term and the shield bonus can and often do
//     both fire on the very same king at once, for opposite reasons
//     (the shield blocks approach from the front; back-rank weakness is
//     about having nowhere to run along the back rank itself).
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

#include <array>

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

/// Pawn storm penalty (this file's own header comment has the full
/// rationale), indexed by the storming ENEMY pawn's own relative rank
/// from ITS OWN side's perspective (0 = its own back rank, 7 =
/// promotion) -- the identical relative-rank convention eval/pawns.h's
/// own kPassedPawnBonus uses, so index N here means "this enemy pawn
/// has advanced exactly as far as a friendly passed pawn at relative
/// rank N would have," a directly comparable notion of "how far
/// advanced." Indices 0 and 7 never occur for a real pawn and are
/// zeroed for the identical reason kPassedPawnBonus's own comment
/// gives. Monotonically growing in magnitude — a storming pawn barely
/// off its own back rank is a distant, largely theoretical threat; one
/// approaching the 5th/6th rank is close enough to trade off the king's
/// own shelter pawns or open lines imminently. Deliberately MG-heavy,
/// EG-light — same rationale and roughly the same magnitude ratio as
/// kOpenFileNearKingPenalty/kAttackUnitPenalty above (this file's own
/// header comment on why every term here fades out via eval::taper()
/// rather than needing its own phase-detection logic). First-draft hand
/// estimate, not yet Texel-tuned, same caveat as every other constant
/// in this file.
inline constexpr std::array<Score, 8> kPawnStormPenalty = {{
    {0, 0},
    {0, 0},
    {-2, 0},
    {-6, -1},
    {-14, -2},
    {-24, -4},
    {-36, -6},
    {0, 0},
}};

/// Back-rank weakness penalty (this file's own header comment has the
/// full rationale) -- a single flat Score, applied at most ONCE per
/// side per call (unlike kPawnStormPenalty, this isn't indexed by
/// anything; the king either has the weakness or it doesn't, there's no
/// notion of "how far advanced" the way a storming pawn has). Real, but
/// smaller in magnitude than kOpenFileNearKingPenalty -- being trapped
/// on the back rank only matters if the enemy can actually mount a
/// rank/file attack there (this file's own header comment covers the
/// major-piece-presence gate that makes this conditional in the first
/// place; kOpenFileNearKingPenalty and kAttackUnitPenalty above already
/// capture most of the danger of an exposed king more generally, so
/// this term is a smaller, additional nudge specifically for the
/// "trapped with no flight square" shape, not a wholesale re-scoring of
/// king danger from scratch). Deliberately MG-heavy, EG-light like
/// every other constant in this file (this file's own header comment
/// has the shared rationale) -- doubly so here, since major pieces
/// (the entire precondition for this term firing at all) are
/// specifically what tends to be traded off by the time a real
/// endgame arrives, at which point eval::taper() (eval/score.h) fades
/// this out same as everything else here. First-draft hand estimate,
/// not yet Texel-tuned, same caveat as every other constant in this
/// file.
inline constexpr Score kBackRankWeaknessPenalty = {-14, -2};

/// Evaluates king safety for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp). See this file's header
/// comment for the five components summed into each side's own
/// contribution before being combined White-minus-Black.
///
/// Back-rank-weakness test (this file's own header comment, used for
/// kBackRankWeaknessPenalty above), spelled out here since that
/// constant's own comment refers back to this one: the king must be on
/// its own back rank (rank 0 for White, rank 7 for Black), EVERY
/// existing square directly one rank in front of it (same file, and
/// each adjacent file that exists on the board -- up to 3, fewer on the
/// a/h files) must be occupied by an OWN pawn (a single open front
/// square is enough to rule this out -- the king could step up there),
/// AND the enemy must have at least one rook or queen currently on the
/// board. All three conditions are required together; this is
/// deliberately NOT scaled by how many rooks/queens the enemy has (a
/// flat penalty either applies or it doesn't) or by anything about the
/// king's own file being open/closed (that's kOpenFileNearKingPenalty/
/// kSemiOpenFileNearKingPenalty's own job, checked completely
/// independently above).
///
/// Precondition: board::init_masks() and board::init_magic_bitboards()
/// have both been called (this function uses board::king_attacks() and,
/// via the same attacker-weighting logic eval/mobility.h's
/// mobility_value() already relies on, board::bishop_attacks()/
/// rook_attacks()/queen_attacks() too).
[[nodiscard]] Score king_safety_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
