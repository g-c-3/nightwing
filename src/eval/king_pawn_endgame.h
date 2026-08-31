#pragma once
// src/eval/king_pawn_endgame.h
//
// Algorithmic King+pawn (KPK) endgame theory (ROADMAP.md Phase 6's
// "King+pawn theory" item: "opposition, key squares, corresponding
// squares, the rule of the square, generalized to any K+P
// configuration (not case-tabulated)"). This is Phase 6's first item
// to actually consume eval/endgame.h's classify_endgame() classifier
// (built and left unwired in Session 64 -- docs/DECISIONS.md, 2026-08-31
// (4)) -- king_pawn_endgame_value() below is the "specialized endgame
// reasoning" that classifier's own header comment said didn't exist
// yet for the EndgameSignature::KPK bucket.
//
// SCOPE: this term applies ONLY to positions matching
// EndgameSignature::KPK -- king and exactly one pawn (either side)
// vs. bare king, no other material anywhere. It is NOT a general
// "pawn endgame" evaluator (multiple pawns, or a pawn plus other
// pieces, are out of scope for this item -- ROADMAP.md's own "King+pawn
// theory" wording is read here the same way eval/material_imbalance.h's
// and eval/endgame.h's own header comments already establish this
// codebase's "narrowest version the item's own wording actually asks
// for" precedent: the single-pawn KPK bucket is the one classify_endgame()
// already recognizes, and it's exactly what CPW's own "King and Pawn
// vs King" article and the classical theory below are about). The
// ROADMAP item's "generalized to any K+P configuration (not
// case-tabulated)" phrasing is satisfied by every check below being a
// genuine formula over the pawn's/kings' actual squares -- valid for
// any file/rank the single pawn happens to be on -- rather than a
// lookup table of specific, hand-verified positions the way a
// tablebase (explicitly out of scope for this whole project) would be.
//
// Three classical KPK techniques, each a real CPW article, are
// implemented algorithmically:
// - Rule of the Square (https://www.chessprogramming.org/Square_Rule):
//   does the defending king have enough king-moves to reach the
//   pawn's promotion square before the pawn gets there, accounting for
//   whose move it is and the pawn's own first-move double-step option.
// - Key squares (https://www.chessprogramming.org/Key_Square): squares
//   the attacking king wants to control to force promotion even once
//   the defending king has caught up per the rule of the square.
// - Opposition (https://www.chessprogramming.org/Opposition): direct
//   (orthogonal, one square between) opposition specifically -- the
//   simplest and most common form; distant/diagonal opposition and the
//   fuller "corresponding squares" theory ROADMAP.md's item wording
//   also names are deliberately NOT attempted here -- see
//   king_pawn_endgame.cpp's own header comment for why.
//
// See king_pawn_endgame.cpp's own header comment for the exact
// formulas and every simplification made, and why each is a
// defensible, documented first cut rather than a silently-incomplete
// implementation.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// A clearly winning, unopposed pawn (the rule of the square says the
/// defending king cannot possibly catch it) -- see
/// king_pawn_endgame.cpp's own header comment for the full rationale
/// on this and every other constant below, including why `mg` is set
/// equal to `eg` even though a real KPK position's game phase is
/// always 0 (so only `eg` is ever actually selected by taper()).
inline constexpr Score kUnstoppablePawnBonus{900, 900};

/// Applied when the defending king catches the pawn (per the rule of
/// the square) but the pawn is on the a- or h-file -- CPW's "Rook
/// pawn" drawing exception.
inline constexpr Score kRookPawnDrawishPenalty{-90, -90};

/// Applied when the defending king catches the pawn (non-rook-pawn
/// case) but the attacking king already occupies one of the pawn's own
/// key squares (CPW "Key Square").
inline constexpr Score kKeySquareBonus{60, 60};

/// Applied when the defending king catches the pawn, stands directly
/// in front of it, and currently holds the direct opposition (CPW
/// "Opposition") -- the classical drawing blockade.
inline constexpr Score kOppositionDrawishPenalty{-70, -70};

/// Returns a White-relative Score adjustment for `pos`'s King+pawn
/// endgame theory, or Score{} (no adjustment) if `pos` doesn't match
/// EndgameSignature::KPK (eval/endgame.h) -- calls classify_endgame()
/// internally as its own first check, so this function is safe and
/// correct to call on ANY position, not just ones already known to be
/// KPK; eval::evaluate() (eval.cpp) calls it unconditionally as one
/// more additive term, the same way every other Phase 5/6 eval term
/// does its own internal applicability check rather than requiring the
/// caller to pre-filter.
///
/// Precondition: none beyond what every other eval/*.h term already
/// requires as part of the mandatory startup sequence -- this function
/// only counts pieces already on the board and reads their squares (no
/// sliding-piece attack table lookups), the same situation as
/// eval::classify_endgame() itself.
[[nodiscard]] Score king_pawn_endgame_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
