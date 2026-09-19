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
#include "eval/psqt.h" // round_to_int()
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

/// Runtime-mutable counterpart to kSpaceSquareBonus above -- the
/// space-term entry in the same "runtime-mutable parameter-vector
/// abstraction over eval's currently-constexpr named constants" family
/// as eval::MaterialWeights/PsqtWeights (psqt.h) and
/// eval::MobilityWeights (mobility.h) already establish (ROADMAP.md's
/// Tier 0 tuner item, "PSQT and beyond" -- Step 8, the second "beyond"
/// term after mobility). Space has only ONE constant in the first
/// place (unlike mobility's 4 piece-type-scoped ones), so this struct
/// is the smallest of the four so far -- a single mg/eg pair, no
/// per-piece-type or per-square dimension at all.
///
/// `constexpr`-constructible via default-member-initializers naming
/// kSpaceSquareBonus directly, the same MaterialWeights/MobilityWeights
/// pattern (not PsqtWeights' out-of-line one) -- mechanical, not a
/// judgment call, since kSpaceSquareBonus is an `inline constexpr`
/// value declared right here in this header, not hidden in space.cpp's
/// own anonymous namespace the way psqt.cpp's per-square tables are
/// (see docs/DECISIONS.md, mobility's own introducing entry, for the
/// identical reasoning applied there).
struct SpaceWeights {
    double square_mg = kSpaceSquareBonus.mg;
    double square_eg = kSpaceSquareBonus.eg;
};

/// Returns a SpaceWeights matching kSpaceSquareBonus exactly -- the
/// natural starting point for a space-aware tuning run
/// (tuner::tune()-style, src/tuner/tune.h), and the value
/// SpaceWeights{}'s own default-member-initializer is already,
/// redundantly initialized to (kept in sync by hand, matching
/// default_material_weights()'s/default_mobility_weights()'s own doc
/// comment rationale for why: SpaceWeights{}'s defaults stay
/// self-contained and don't require calling a function just to
/// default-construct one -- this function exists for callers that want
/// that mapping made explicit/named, and for tests confirming the two
/// really do agree).
[[nodiscard]] constexpr SpaceWeights default_space_weights() noexcept {
    return SpaceWeights{
        /*square_mg=*/kSpaceSquareBonus.mg,
        /*square_eg=*/kSpaceSquareBonus.eg,
    };
}

/// Evaluates the space term for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp).
///
/// `weights`, if non-null, is used INSTEAD OF kSpaceSquareBonus for
/// this call only -- the same nullable-override convention
/// material_value()/psqt_value()/mobility_value() already establish,
/// threaded here for eval::evaluate()'s own `space_weights` parameter
/// (eval.h) so a future space-aware tuning run can probe candidate
/// space weight vectors the same uniform way it already can for
/// material, PSQT, and mobility. `weights->square_mg`/`square_eg` are
/// `double` (SpaceWeights' own field type); Score's own fields are
/// `int` (score.h), so the override is rounded via the same
/// round_to_int() helper psqt_value()/mobility_value() already use,
/// reused here rather than duplicated.
///
/// Precondition: board::init_masks() has been called (this function
/// uses board::pawn_attacks(), a masks.h function). Like eval/
/// piece_bonuses.h's piece_bonus_value() and eval/knight_outposts.h's
/// knight_outpost_value(), and unlike eval/mobility.h's mobility_value()
/// /eval/king_safety.h's king_safety_value(), this function does NOT
/// need board::init_magic_bitboards() -- it never calls a
/// sliding-piece attack function.
[[nodiscard]] Score space_value(const board::Position& pos,
                                 const SpaceWeights* weights = nullptr) noexcept;

} // namespace nightwing::eval
