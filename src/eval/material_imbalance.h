#pragma once
// src/eval/material_imbalance.h
//
// Material imbalance table (ROADMAP.md Phase 5's "Material imbalance
// table" item, ROADMAP.md's own example: "bishop pair / knight pair
// value shifts with pawn count, per Stockfish-classic style"). The
// general "piece values change with pawn count" idea is the classic
// Kaufman's Rule refinement to static material values (see CPW's
// "Point Value" article, https://www.chessprogramming.org/Point_Value,
// and its "Material" article's own broader discussion of imbalance,
// https://www.chessprogramming.org/Material) -- knights get relatively
// WORSE and rooks/bishops get relatively BETTER as pawns leave the
// board. From-scratch implementation here, no code copied, per
// ARCHITECTURE.md's Attribution Policy.
//
// Scope, deliberately narrower than Kaufman's full per-piece-type
// table or Stockfish-classic's own full quadratic imbalance matrix
// (every own-piece-type-count times every own-OTHER-piece-type-count,
// plus the same again against the opponent's piece counts): this
// module scores exactly the two cases ROADMAP.md's own item wording
// names, bishop pair and knight pair, each scaled by how many pawns
// have left the board -- not a general per-piece-type value curve, and
// not cross-piece-type interactions (e.g. "an extra pawn is worth more
// with queens still on" is a separate, unclaimed future refinement).
// Matches this codebase's established "narrowest version the ROADMAP
// item's own wording actually asks for" precedent (most recently
// Trapped piece penalties' own knights/bishops-only scope, docs/
// DECISIONS.md 2026-08-29 (1)) rather than silently building out the
// full Stockfish-classic matrix this item's own parenthetical cites
// only as a style reference, not a literal spec.
//
// Relationship to eval/piece_bonuses.h's existing kBishopPairBonus: this
// module is a SEPARATE, ADDITIONAL, pawn-count-scaled adjustment, not a
// replacement -- piece_bonuses.h's flat bishop pair bonus (Session 41)
// stays exactly as it was; this module adds a second, independent
// bishop-pair-conditioned term on top of it that grows specifically as
// pawns disappear, mirroring the same "two bishops matter more once the
// board opens up" reasoning piece_bonuses.h's own kBishopPairBonus
// doc comment already gives for why ITS OWN eg is above its own mg --
// this module makes that same idea pawn-count-continuous instead of
// only phase-continuous. Reuses piece_bonuses.h's own bishop-pair
// definition exactly (`popcount(bishops) >= 2`, no same/opposite-
// color-square distinction) for consistency between the two terms
// rather than introducing a second, subtly different definition of
// "has the bishop pair."
//
// Why a knight-pair PENALTY, not a bonus (mirroring bishop pair's own
// sign): two knights don't complement each other the way two bishops
// on opposite colors do -- CPW's own "Redundancy" discussion (linked
// from the "Material" article above) notes this general "same-type
// piece pairs beyond the useful complementary case are somewhat
// redundant" pattern, most classically applied to knights specifically
// because, unlike bishops, there's no color-complex argument for a
// second one adding genuinely new coverage.
//
// Why the knight-pair penalty GROWS (in magnitude) as pawns disappear,
// the SAME direction as the bishop-pair bonus's own growth, not the
// opposite: a knight's real strength is maneuvering around a pawn
// chain and sitting on outposts a closed, pawn-heavy structure creates
// (CPW's own "Knight" article's general theme) -- exactly the
// structure that DISAPPEARS as pawns are traded off. So as the pawn
// count drops, knights lose the specific terrain that made a second
// one still somewhat useful, and the redundancy cost of having two
// becomes more pronounced, not less -- the mirror image of why bishops
// (whose value is a long-diagonal reach question, per piece_bonuses.h's
// own kBishopPairBonus doc comment) go the opposite way.
//
// Same "few hand-estimated constants, formula over a large tuned
// table" preference every other eval/*.h module in this phase already
// establishes -- both per-missing-pawn constants below are first-draft
// hand estimates, not yet Texel-tuned, same caveat every other eval
// term added this phase already carries.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Total pawns present at the start of a standard chess game (8 per
/// side) -- the baseline this module's "how many pawns have left the
/// board" scaling is measured against. Deliberately counts BOTH
/// sides' pawns together into one shared, global figure (not a
/// separate count per side): "how open has the game become" is a
/// property of the whole board, the same reasoning eval.cpp's own
/// compute_phase() already applies by summing both sides' non-pawn
/// material into one shared phase value rather than computing a
/// separate phase per side.
inline constexpr int kStartingTotalPawns = 16;

/// Extra bishop-pair value per pawn that has left the board (added on
/// top of eval/piece_bonuses.h's own flat kBishopPairBonus -- see this
/// file's header comment for why these are two separate, additive
/// terms rather than one). `eg` above `mg`, same direction and same
/// underlying reasoning as kBishopPairBonus's own doc comment.
inline constexpr Score kBishopPairPerMissingPawn = {2, 3};

/// Extra knight-pair PENALTY (applied as a subtraction, negative Score
/// values here) per pawn that has left the board -- see this file's
/// header comment for why this is a penalty, not a bonus, and why its
/// magnitude grows in the same direction as the bishop-pair bonus's
/// own growth rather than the opposite. `mg`/`eg` kept equal rather
/// than picking a direction between them: unlike bishop pair (whose
/// eg > mg reflects long-diagonal reach specifically mattering more
/// with less material generally on the board) or knight outposts
/// (whose own docs/DECISIONS.md entry picks a direction for a
/// different, rank-specific reason), this term's own justification is
/// purely "how many pawns are left," a quantity already directly in
/// this formula -- there's no separate, additional phase-based
/// argument here distinct from the pawn-count scaling itself, so
/// introducing an mg/eg split on top of it would be an unjustified
/// extra degree of freedom rather than a real, defensible asymmetry.
inline constexpr Score kKnightPairPerMissingPawn = {-2, -2};

/// Evaluates the pawn-count-scaled bishop-pair/knight-pair imbalance
/// terms for BOTH sides and returns a single White-relative Score
/// (positive favors White, matching every other eval/*.h term's sign
/// convention in eval.cpp).
///
/// Precondition: none beyond what every other eval/*.h term already
/// requires as part of the mandatory startup sequence -- this function
/// only counts pieces already on the board (board::popcount() over
/// board::Position::pieces()), touching no attack table of any kind,
/// so board::init_masks()/board::init_magic_bitboards() make no
/// practical difference to this function specifically (same situation
/// as eval/tempo.h's own tempo_value()).
[[nodiscard]] Score material_imbalance_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
