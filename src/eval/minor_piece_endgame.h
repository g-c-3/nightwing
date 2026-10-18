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

/// Divisor applied to the whole static evaluation when
/// is_wrong_bishop_fortress() holds (added Session 235, external review
/// fix). The flat kWrongBishopCornerDrawPenalty above is only a few
/// tenths of a pawn once the rest of the evaluation is counted, and a
/// pawn pushed towards promotion outgrows it, so the search score of a
/// dead draw climbed with depth (+6.0 at depth 14 for 7k/8/7K/7P/8/8/8/5B2 w).
/// Dividing the full score by this keeps a small residual in favour of the
/// side with the extra material without calling the position a win.
inline constexpr int kWrongBishopFortressDivisor = 8;

/// True when the position is a recognized wrong-bishop corner fortress:
/// EndgameSignature::KBPK, every attacking pawn on one rook file, the
/// bishop NOT controlling that file's promotion-corner colour, the
/// defending king able to reach the corner in time (the same rule-of-the-
/// square test minor_piece_endgame_value() uses), and additionally the
/// defending king no farther from the corner than the attacking king
/// (an attacking king that gets to the corner first can drive the defender
/// out, so no fortress). Used by eval::evaluate() to scale the whole
/// score towards a draw. Pure function of `pos`; side-to-move independent
/// apart from the shared rule-of-the-square tempo term.
[[nodiscard]] bool is_wrong_bishop_fortress(const board::Position& pos) noexcept;

} // namespace nightwing::eval
