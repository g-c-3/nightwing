#pragma once
// src/eval/king_tropism.h
//
// King tropism (ROADMAP.md Phase 5's "King tropism (piece proximity to
// enemy king in the attack)" item): the standard CPW "King Tropism"
// concept (https://www.chessprogramming.org/King_Safety#Tropism) —
// rewards a side's own knights/bishops/rooks/queens for simply standing
// CLOSE to the enemy king, independent of whether any of them currently
// has a clear attacking line into the king's zone. From-scratch
// implementation here, no code copied, per ARCHITECTURE.md's
// Attribution Policy.
//
// Why this doesn't duplicate eval/king_safety.h's own "attacker
// weighting" component (kAttackUnitPenalty/attack_units_on()): that
// component counts pieces that ALREADY attack at least one square of
// the immediate king zone right now -- a piece two ranks away with a
// rook or bishop blocking its path scores nothing there, however close
// it stands. Tropism is deliberately the opposite kind of signal: a
// purely geometric Chebyshev-distance measure that ignores blockers and
// lines of attack entirely, as a proxy for how easily a piece could be
// redeployed toward an attack on the enemy king even when it isn't
// directly attacking anything there yet. The two terms measure
// genuinely different things and are meant to coexist, not replace one
// another.
//
// Same per-piece-type weighting scheme eval/king_safety.h's own
// kKnightAttackUnits/kBishopAttackUnits/kRookAttackUnits/
// kQueenAttackUnits already establishes (1/1/2/4 -- a queen's
// attacking potential from a given proximity outweighs a knight's the
// same way it does for that term) is intentionally reused here for
// consistency between the two related king-danger terms, not
// independently re-derived.
//
// Same "few hand-estimated constants, formula over a large tuned
// table" preference every other eval/*.h module in this phase already
// establishes -- both the per-piece-type weights and
// kTropismUnitBonus below are first-draft hand estimates, not yet
// Texel-tuned, same caveat every other eval term added this phase
// already carries.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Per-piece-type tropism weight, reused verbatim from eval/
/// king_safety.h's own attack-unit weighting scheme (see this file's
/// header comment for why): Knight/Bishop = 1, Rook = 2, Queen = 4.
inline constexpr int kKnightTropismWeight = 1;
inline constexpr int kBishopTropismWeight = 1;
inline constexpr int kRookTropismWeight = 2;
inline constexpr int kQueenTropismWeight = 4;

/// The largest Chebyshev (king-move) distance possible between two
/// squares on an 8x8 board -- a1 to h8 is 7 -- used as the falloff
/// ceiling: a piece exactly `kTropismMaxDistance` squares away (the
/// farthest possible) contributes nothing, and a piece one square away
/// gets the largest possible per-unit multiplier (`kTropismMaxDistance
/// - 1 = 6`). See king_tropism.cpp's own chebyshev_distance() for the
/// distance computation itself.
inline constexpr int kTropismMaxDistance = 7;

/// Per-"tropism unit" value -- one unit is one piece-type weight point
/// at one square of proximity (i.e. a Knight/Bishop one square closer
/// to the enemy king contributes exactly one unit; a Queen one square
/// closer contributes four). `mg` above `eg`: proximity to the enemy
/// king is a proxy for attacking potential, and an attack needs other
/// pieces and open lines still on the board to actually convert into
/// something -- the same general "matters most while there's still a
/// real attack to build" reasoning eval/king_safety.h's own
/// mg-heavier constants already follow for the analogous reason on the
/// defending side of the same relationship.
inline constexpr Score kTropismUnitBonus = {2, 1};

/// Evaluates the king-tropism term for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp).
///
/// Precondition: board::init_masks() has been called. Unlike eval/
/// mobility.h's mobility_value()/eval/king_safety.h's
/// king_safety_value()/eval/threats.h's threats_value(), this function
/// does NOT need board::init_magic_bitboards() -- it never calls a
/// sliding-piece attack function, only plain file/rank arithmetic on
/// each piece's own square and the enemy king's square.
[[nodiscard]] Score king_tropism_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
