#pragma once
// src/eval/space.h
//
// Space evaluation (ROADMAP.md Phase 5's "Space evaluation" item): the
// standard CPW "Space" concept (https://www.chessprogramming.org/Space)
// — rewards controlling more of the board's central squares, on the
// theory that a side with more safe room to maneuver its pieces in has
// a real, if hard-to-quantify-precisely, positional edge over a
// cramped opponent. From-scratch implementation here, no code copied,
// per ARCHITECTURE.md's Attribution Policy.
//
// A square counts toward a side's own space total if it's within that
// side's own "space zone" (see space_zone() in space.cpp: the c/d/e/f
// files, on the three ranks just ahead of that side's own back rank —
// CPW's own standard "central squares in one's own territory or just
// past it" scope for this concept), isn't occupied by that side's own
// pawn (a pawn standing there is already scored via material/PSQT/pawn
// structure -- this term is about additional room BEYOND the pawns
// themselves), and isn't attacked by an enemy pawn (an attacked square
// isn't safely controlled space, the same "safe" qualifier Stockfish-
// style "safe mobility" uses, though eval/mobility.h's own
// mobility_value() deliberately skips that refinement for pseudo-
// mobility -- see mobility.h's own header comment for why -- this term
// applies it directly since it's central to what "space" even means
// here, not an optional refinement layered on top).
//
// Deliberately NOT narrowed further to "only counts if also standing
// behind a friendly pawn" (a real additional Stockfish-style
// refinement, giving extra weight specifically to squares shielded by
// an own pawn one or two ranks back) -- matches this codebase's
// established preference for the simpler of two reasonable options at
// this stage (see eval/mobility.h's own "Why a flat per-square bonus"
// entry for the parallel reasoning): every zone square that's safe and
// pawn-free counts equally, rather than some counting more than others.
//
// Same "few hand-estimated constants" preference every other eval/*.h
// module in this phase already establishes -- kSpaceSquareBonus below
// is a first-draft hand estimate, not yet Texel-tuned, same caveat
// every other eval term added this phase already carries.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Flat bonus per safely-controlled space-zone square (see this file's
/// header comment for the exact qualification test). `eg` deliberately
/// zero, unlike every other tapered term added this phase (which are
/// all *smaller*, not *absent*, in one direction) -- CPW's own "Space"
/// article frames this specifically as a middlegame concept: room to
/// maneuver pieces matters when there are pieces left to maneuver and
/// squares worth fighting over, and with most pieces traded off in the
/// endgame there's little left for controlling an extra central square
/// to actually cramp. Some engines instead scale space by the current
/// non-pawn piece count directly (a continuous version of the same
/// idea); `eval::taper()`'s existing mg/eg blend already achieves the
/// same practical fade-out here without a second, separate scaling
/// factor of its own -- see docs/DECISIONS.md for why this was judged
/// the simpler equivalent for a first cut.
inline constexpr Score kSpaceSquareBonus = {2, 0};

/// Evaluates the space term for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp).
///
/// Precondition: board::init_masks() has been called (this function
/// uses board::pawn_attacks(), a masks.h function). Like eval/
/// piece_bonuses.h's piece_bonus_value() and eval/knight_outposts.h's
/// knight_outpost_value(), and unlike eval/mobility.h's mobility_value()
/// /eval/king_safety.h's king_safety_value(), this function does NOT
/// need board::init_magic_bitboards() -- it never calls a
/// sliding-piece attack function.
[[nodiscard]] Score space_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
