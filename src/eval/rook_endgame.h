#pragma once
// src/eval/rook_endgame.h
//
// Algorithmic rook-endgame pattern theory (ROADMAP.md Phase 6's "Rook
// endgame patterns" item: "Lucena position recognition (winning
// technique), Philidor position recognition (drawing technique),
// Vancura position, rook behind passed pawn heuristic"). Phase 6's
// second item to consult eval/endgame.h's classify_endgame()
// classifier, applying whenever it returns EndgameSignature::RookEndgame
// (both sides have exactly one rook each, any pawn count including
// zero -- see endgame.h's own header comment for that bucket's exact
// definition).
//
// SCOPE: three of this item's four named patterns are implemented here
// -- Tarrasch's Rule (rook behind passed pawn), Lucena position
// recognition, and Philidor position recognition. Vancura position
// recognition is deliberately DEFERRED, not attempted in this file --
// see docs/DECISIONS.md for the specific rationale (in short: Vancura's
// own recognition criteria -- a non-rook-file pawn plus a specific
// lateral-checking rook geometry distinct from both Lucena's and
// Philidor's -- are meaningfully different from, and no easier than,
// the two patterns implemented here, and a hastily-encoded, unverified
// fourth pattern risked a wrong eval nudge more than it risked being
// merely incomplete). This mirrors the same documented-partial-scope
// precedent eval/king_pawn_endgame.h's own header comment already
// established for distant/diagonal opposition within Phase 6's
// King+pawn theory item.
//
// Lucena and Philidor recognition BOTH additionally narrow
// EndgameSignature::RookEndgame's own broad "any pawn count" bucket
// down to the specific case both patterns are classically about: a
// SINGLE pawn total on the entire board (the textbook "rook + pawn vs.
// rook" scenario -- CPW's own "Lucena Position" and "Philidor
// Position" articles are both framed this way). A general multi-pawn
// rook endgame is a different, much less geometrically clean-cut
// problem that this file does not attempt to pattern-match --
// Tarrasch's Rule (the fourth pattern) is the one check in this file
// that DOES apply generally, across any pawn count, since it needs no
// such narrowing to stay well-scoped (see this file's own .cpp for why:
// it's a per-pawn, per-rook geometric check, not a whole-position
// pattern).
//
// See rook_endgame.cpp's own header comment for the exact recognition
// criteria used for each of the three implemented patterns and every
// simplification made.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Bonus for a rook standing behind (on the same file, on the side away
/// from that pawn's own promotion square) one of its own side's passed
/// pawns -- Tarrasch's Rule, the "support" half. CPW: "Rook Endings" /
/// "Tarrasch Rule."
inline constexpr Score kRookBehindOwnPassedPawnBonus{20, 35};

/// Bonus for a rook standing behind (same definition as above) an
/// ENEMY passed pawn -- Tarrasch's Rule, the "restraint" half: a rook
/// behind an enemy passed pawn hampers its advance the same way a rook
/// behind a friendly one supports it.
inline constexpr Score kRookBehindEnemyPassedPawnBonus{15, 30};

/// Bonus for a recognized Lucena position (CPW "Lucena Position") --
/// see rook_endgame.cpp for the exact recognition criteria. Sized
/// smaller than eval/king_pawn_endgame.h's own kUnstoppablePawnBonus:
/// a bare KPK unopposed pawn is mathematically certain to promote,
/// while Lucena is a "known, standard winning technique" the attacking
/// side still has to actually execute (build the bridge) -- a real,
/// decisive advantage, but with a rook still on the board for the
/// defender to create complications with, unlike KPK's simplest case.
inline constexpr Score kLucenaWinBonus{600, 600};

/// Penalty (applied to the attacking side) for a recognized Philidor
/// position (CPW "Philidor Position") -- the classical drawing setup:
/// defending rook holding the cutting-off rank before the attacking
/// pawn can cross it. Sized similarly to eval/king_pawn_endgame.h's
/// own kRookPawnDrawishPenalty and kOppositionDrawishPenalty, for the
/// same reason -- roughly cancelling material_value()'s own flat
/// +1-pawn count for the attacker's extra pawn, so the position reads
/// close to balanced (correctly drawish) rather than "up a clean pawn."
inline constexpr Score kPhilidorDrawPenalty{-70, -70};

/// Returns a White-relative Score adjustment for `pos`'s rook-endgame
/// theory, or Score{} (no adjustment) if `pos` doesn't match
/// EndgameSignature::RookEndgame (eval/endgame.h) -- calls
/// classify_endgame() internally as its own first check, so this
/// function is safe and correct to call on ANY position, matching
/// eval/king_pawn_endgame.h's own king_pawn_endgame_value() convention.
/// eval::evaluate() (eval.cpp) calls it unconditionally as one more
/// additive term.
[[nodiscard]] Score rook_endgame_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
