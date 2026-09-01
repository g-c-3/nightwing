#pragma once
// src/eval/basic_mates.h
//
// Algorithmic "basic checkmate" technique heuristics (ROADMAP.md Phase
// 6's final item: "Hand-built base heuristics carried over: KPK, KRK,
// KBNK exact-play rules (algorithmic, not lookup-table), draw
// detection refinement (insufficient material)"). This file covers
// the KRK and KBNK clauses specifically:
//
// - KPK already has real algorithmic theory from Phase 6's earlier
//   "King+pawn theory" item (eval/king_pawn_endgame.h, Session 65) --
//   Rule of the Square, Key Squares, and direct Opposition. That is
//   this project's actual answer to KPK "exact-play rules": those
//   three techniques, combined with full search over a position that
//   (by construction, in a KPK endgame) has an extremely small
//   branching factor, are what make the search converge on correct
//   play, the same way this file's own KRK/KBNK terms are designed to
//   do below -- not a separate, second KPK mechanism.
// - Draw detection refinement (insufficient material) lives in
//   search/search.cpp, alongside the pre-existing 50-move-rule and
//   repetition checks it's a natural sibling of -- see that file's own
//   is_insufficient_material() for the exact rule and why it's
//   deliberately narrower than it might first appear it should be.
//
// KRK and KBNK ("King and Rook vs. King" and "King, Bishop, and Knight
// vs. King", CPW's own terms -- the latter is famously "the hardest of
// the basic mates") are both provably-forced wins with zero drawing
// chances for the defender under optimal play. This project has no
// tablebase (a hard constraint, not a placeholder) and does not build
// a dedicated move-selection override or solver for either endgame --
// consistent with every other Phase 6 item, "algorithmic, not
// lookup-table" is satisfied here the same way it is for eval/
// king_pawn_endgame.h, eval/rook_endgame.h, and eval/
// minor_piece_endgame.h: real formulas over the actual king/piece
// squares, shaping the existing search toward the well-known mating
// technique for each endgame, rather than either a table of specific
// positions or a special search bypass. With material this lopsided
// (a whole rook, or a bishop and knight, against a bare king), ordinary
// search combined with correctly-shaped eval terms reliably converges
// on the standard technique -- this is how virtually every classical,
// non-tablebase engine has always handled these two endgames.
//
// See basic_mates.cpp's own header comment for the exact formulas used
// for KRK and KBNK and the classical technique each one encodes.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Weight for how strongly the defending king being pushed toward the
/// board's edge (any edge, not a specific corner) is rewarded in a KRK
/// position -- CPW "King and Rook vs King": confining the defending
/// king to a shrinking rectangle is the technique's first phase,
/// before the attacking king approaches for mate.
inline constexpr Score kKRKEdgePushWeight{6, 6};

/// Weight for how strongly the two kings being close together (the
/// attacking king approaching to help confine/mate) is rewarded in a
/// KRK position -- the technique's second phase.
inline constexpr Score kKRKKingProximityWeight{10, 10};

/// Weight for how strongly the defending king being close to a corner
/// matching the attacking side's BISHOP's own square color is rewarded
/// in a KBNK position -- CPW "King, Bishop and Knight vs King": unlike
/// KRK, only the two corners the bishop actually controls can host the
/// final mate; driving the king to the OTHER pair of corners
/// accomplishes nothing, which is exactly why this endgame has the
/// reputation of being "the hardest of the basic mates." Weighted more
/// heavily than kKBNKEdgePushWeight below, since getting the corner
/// color right is this endgame's own defining, distinguishing
/// technique -- generic edge-pushing alone, the way KRK's own term
/// works, is not sufficient here.
inline constexpr Score kKBNKCornerColorWeight{14, 14};

/// Weight for a KBNK position's own generic edge-push term, the same
/// concept as kKRKEdgePushWeight above but for the KBNK case --
/// secondary to kKBNKCornerColorWeight, since any edge is a reasonable
/// intermediate step on the way to a correctly-colored corner, but only
/// the corner-color term actually distinguishes a winning approach from
/// a hopeless one.
inline constexpr Score kKBNKEdgePushWeight{4, 4};

/// Weight for how strongly the two kings being close together is
/// rewarded in a KBNK position -- same concept as
/// kKRKKingProximityWeight above.
inline constexpr Score kKBNKKingProximityWeight{8, 8};

/// Returns a White-relative Score adjustment for `pos`'s KRK and KBNK
/// basic-checkmate technique, or Score{} if `pos` doesn't match either
/// EndgameSignature::KRK or EndgameSignature::KBNK (eval/endgame.h) --
/// calls classify_endgame() internally as its own first check, matching
/// every other Phase 6 eval term's own established convention.
/// eval::evaluate() (eval.cpp) calls it unconditionally as one more
/// additive term.
[[nodiscard]] Score basic_mate_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
