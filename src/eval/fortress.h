#pragma once
// src/eval/fortress.h
//
// Algorithmic fortress pattern detection (ROADMAP.md Phase 6's
// "Fortress pattern detection" item: "structural, not tabulated —
// recognize blocked/closed positions where material advantage can't
// be converted").
//
// UNLIKE eval/king_pawn_endgame.h, eval/rook_endgame.h, and eval/
// minor_piece_endgame.h -- Phase 6's first three terms, each gated on
// one or more eval/endgame.h EndgameSignature buckets -- this term
// deliberately does NOT consult eval::classify_endgame() at all. A
// general fortress is not tied to any one material shape the way KPK,
// a rook ending, or a same-bishop-color imbalance are: a blocked,
// unconvertible position can arise from almost any material
// combination the classifier's seven buckets don't (and structurally
// can't, without becoming an unbounded list) enumerate. This matches
// the item's own "structural, not tabulated" wording directly: rather
// than adding an eighth, ninth, tenth... bucket per fortress shape,
// this term works from structural features of `pos` itself (blocked
// pawns, remaining piece count) that apply across material
// configurations. See docs/DECISIONS.md for the full reasoning behind
// this deliberate departure from the previous three Phase 6 terms'
// shared pattern.
//
// SCOPE: general, provably-correct fortress recognition is a genuinely
// hard, open problem even for much stronger engines than this one --
// this term does NOT attempt to prove a position is drawn. It applies
// a proportional discount to whichever side holds a material lead,
// when the position also looks sufficiently blocked and simplified by
// two structural proxies (see fortress.cpp for the exact criteria) --
// nudging eval away from over-trusting a raw material count in
// positions where conversion is genuinely likely to be difficult,
// without ever claiming certainty the way eval/king_pawn_endgame.h's
// own Rule-of-the-Square or Key-Square checks do for their much
// narrower, exactly-defined cases.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// If more non-pawn, non-king pieces than this remain on the board
/// (either side, combined), fortress_value() returns Score{}
/// unconditionally -- too much material still in play for this simple
/// structural heuristic to be a trustworthy signal. Queens are handled
/// separately and more strictly, in fortress.cpp directly: ANY queen
/// on the board disqualifies the position outright, not merely a queen
/// count over some threshold the way this constant works for
/// knights/bishops/rooks.
inline constexpr int kFortressMaxNonPawnPieces = 6;

/// The position must have at least this many mutually-blocked pawns
/// (this file's own local definition, matching eval/
/// minor_piece_endgame.cpp's own identical concept) before
/// fortress_value() considers the structure "locked" enough to apply
/// any discount at all.
inline constexpr int kFortressMinBlockedPawns = 4;

/// Returns a White-relative Score adjustment for `pos`'s general
/// fortress/conversion-difficulty structure, or Score{} if `pos`
/// doesn't meet this term's own structural criteria (see fortress.cpp)
/// -- safe and correct to call on ANY position. eval::evaluate()
/// (eval.cpp) calls it unconditionally as one more additive term.
[[nodiscard]] Score fortress_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
