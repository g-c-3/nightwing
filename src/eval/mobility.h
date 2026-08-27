#pragma once
// src/eval/mobility.h
//
// Mobility evaluation (ROADMAP.md Phase 5's "Mobility eval" item):
// rewards a piece for having more squares available to move to, the
// standard CPW "Mobility" concept
// (https://www.chessprogramming.org/Mobility) -- from-scratch
// implementation here, no code copied, per ARCHITECTURE.md's
// Attribution Policy. Knights, bishops, rooks, and queens are counted;
// pawns are already scored via eval/pawns.h and material_value()
// (eval/psqt.h), and the king is deliberately excluded -- king
// activity/safety is Phase 5's own separate, dedicated item (planned:
// eval/king_safety.h, not yet implemented), not folded into this
// generic mobility term.
//
// The specific per-square centipawn bonuses in mobility.cpp are
// first-draft hand estimates, not yet Texel-tuned (ROADMAP.md Phase 5's
// tuner item lands later and is expected to revise every constant in
// this file, same caveat as eval/pawns.h's own header comment) -- and
// deliberately a flat "per attacked square" bonus rather than a
// diminishing-returns lookup table indexed by mobility count (the more
// elaborate approach some engines use): matches this codebase's
// existing pawn-structure eval's own preference for a handful of simple
// additive constants over a larger tuned table as the right first cut
// before a real tuner exists -- see docs/DECISIONS.md.
//
// Deliberately its own translation unit, separate from eval.cpp, same
// organizational rationale as eval/pawns.h (one clearly-scoped eval term
// per file) even though mobility -- unlike pawn structure -- has no
// caching motivation of its own (piece mobility changes essentially
// every move, so there's no analogous "cache it, most positions don't
// change it" case the way eval/pawn_tt.h makes for pawn structure).

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Per-square mobility bonus for each piece type that counts toward this
/// term -- knight, bishop, rook, queen only (see this file's header
/// comment for why pawns/king are excluded). Larger magnitude for
/// pieces whose activity swings more decisively (a bishop or rook with
/// many open lines is a bigger deal than a knight with a few extra
/// hops; a queen's bonus is kept smallest of all despite it usually
/// having the most raw attacked squares, precisely BECAUSE it usually
/// has the most -- an unscaled per-square bonus at knight/bishop
/// magnitude would let queen mobility alone dominate this whole term).
/// `eg` at or above `mg` throughout: piece activity generally matters
/// at least as much, often more, once there are fewer pieces around to
/// block lines and contest outposts (the same general CPW "Mobility"
/// observation other engines' published weights reflect, not a value
/// copied from any of them).
inline constexpr Score kKnightMobilityBonus = {4, 4};
inline constexpr Score kBishopMobilityBonus = {5, 5};
inline constexpr Score kRookMobilityBonus = {2, 4};
inline constexpr Score kQueenMobilityBonus = {1, 2};

/// Evaluates mobility for BOTH sides and returns a single White-relative
/// Score (positive favors White, matching
/// material_value()/psqt_value()/pawn_structure_value()'s sign
/// convention in eval.cpp/eval/pawns.h). For each knight/bishop/rook/
/// queen on the board, counts the squares it attacks that aren't
/// occupied by a piece of its OWN color (CPW's basic "pseudo-mobility"
/// definition -- a square occupied by an ENEMY piece still counts, even
/// though moving there would be a capture rather than free movement;
/// deliberately not narrowed to only empty squares, nor to
/// Stockfish-style "safe mobility" that also excludes squares attacked
/// by an enemy pawn -- a reasonable first-cut simplification, revisit
/// once the Texel tuner (ROADMAP.md Phase 5) can show whether a more
/// refined definition is worth its added complexity), multiplied by
/// that piece type's own per-square bonus above.
///
/// Precondition: board::init_masks() and board::init_magic_bitboards()
/// have both been called (this function uses board::knight_attacks()/
/// board::bishop_attacks()/board::rook_attacks()/board::queen_attacks(),
/// transitively requiring both).
[[nodiscard]] Score mobility_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
