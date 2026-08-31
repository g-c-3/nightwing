#pragma once
// src/eval/minor_piece_endgame.h
//
// Algorithmic minor-piece endgame theory (ROADMAP.md Phase 6's "Minor
// piece endgames" item: "wrong-bishop-corner draw detection,
// opposite-colored bishop fortress/drawish-tendency eval adjustment,
// knight vs. bishop endings weighted by pawn structure (open vs.
// closed)"). Phase 6's third item to consult eval/endgame.h's
// classify_endgame() classifier, and the first to consult THREE
// different buckets from a single eval term: EndgameSignature::KBPK
// (added this same session -- see endgame.h's own doc comment on that
// bucket for why the original six-bucket set from Session 64 didn't
// already have it), EndgameSignature::OppositeColoredBishops, and
// EndgameSignature::KnightVsBishop -- one bucket per clause in this
// item's own ROADMAP.md wording, in the same order.
//
// See minor_piece_endgame.cpp's own header comment for the exact
// recognition criteria and formulas used for each of the three
// sub-patterns and every simplification made.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Penalty applied to the attacking side (the one with the bishop and
/// pawn(s)) for a recognized "wrong bishop corner" fortress: every
/// attacking pawn on a single rook file (a- or h-file), the bishop
/// does NOT control that file's promotion-square color, and the
/// defending king can reach the drawing corner in time (this file's
/// own rule-of-the-square check, the identical technique eval/
/// king_pawn_endgame.h's own KPK term uses for the same purpose).
/// Large in magnitude -- CPW "Wrong Bishop": this is a well-established,
/// near-total fortress draw, not merely a "somewhat drawish" tendency
/// the way opposite-colored-bishop endgames more generally are (see
/// kOCBDrawishPenaltyPerExtraPawn below for that softer case).
inline constexpr Score kWrongBishopCornerDrawPenalty{-350, -400};

/// Per-extra-pawn drawish discount applied against whichever side has
/// MORE pawns in an opposite-colored-bishop endgame (CPW "Opposite
/// Colored Bishops": these endgames are famously more drawish than
/// their raw material balance suggests, since the side down material
/// can often blockade on squares the opponent's own bishop can never
/// contest). Deliberately proportional only to the pawn-count
/// difference already present, not a flat bonus applied regardless of
/// material balance -- see minor_piece_endgame.cpp's own header
/// comment for why a flat, always-on bonus was rejected.
inline constexpr Score kOCBDrawishPenaltyPerExtraPawn{-15, -25};

/// Per-blocked-pawn bonus for the side holding the KNIGHT in a
/// KnightVsBishop endgame (CPW "Knight vs. Bishop": closed, blocked
/// pawn structures favor the knight, which can hop over the blockade a
/// bishop cannot cross).
inline constexpr Score kKnightClosedPositionBonusPerBlockedPawn{8, 12};

/// Per-open-pawn bonus for the side holding the BISHOP in a
/// KnightVsBishop endgame (the complementary CPW principle: open,
/// unblocked pawn structures favor the bishop's long-range diagonal
/// mobility over the knight's short hops).
inline constexpr Score kBishopOpenPositionBonusPerOpenPawn{6, 10};

/// Returns a White-relative Score adjustment for `pos`'s minor-piece
/// endgame theory, or Score{} (no adjustment) if `pos` doesn't match
/// any of EndgameSignature::KBPK, ::OppositeColoredBishops, or
/// ::KnightVsBishop (eval/endgame.h) -- calls classify_endgame()
/// internally as its own first check, matching eval/
/// king_pawn_endgame.h's and eval/rook_endgame.h's own established
/// convention for the identical situation. eval::evaluate() (eval.cpp)
/// calls it unconditionally as one more additive term.
[[nodiscard]] Score minor_piece_endgame_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
