#pragma once
// src/search/quiescence.h
//
// Quiescence search: extends the main search past its nominal depth
// limit, searching only "noisy" moves (captures, and check-giving quiet
// moves at the very first quiescence ply -- see quiescence.cpp for why
// only the first) until a "quiet" (no immediate tactics) position is
// reached, instead of trusting a raw static eval right at the horizon.
// This is what negamax()'s depth <= 0 base case calls instead of
// eval::evaluate() directly (search.cpp) -- it exists specifically to
// avoid the "horizon effect": a plain fixed-depth search stopping mid-
// capture-sequence and badly misjudging the position because it never
// looked one ply further to see the recapture. CPW "Quiescence Search";
// SEE-based pruning of clearly-losing captures (search/see.h) is CPW
// "Static Exchange Evaluation" applied here per ROADMAP.md's "with SEE
// pruning" wording. From-scratch implementation of these public,
// well-documented techniques; no code copied.

#include <cstdint>

#include "board/board.h"

namespace nightwing::search {

/// Quiescence search from `pos`, returning a score from
/// `pos.side_to_move`'s perspective -- same sign convention as
/// negamax() (search.cpp), which is this function's only caller. `pos`
/// is left unmodified on return (every make_move() is paired with a
/// matching unmake_move()).
///
/// `alpha`/`beta` is the search window, exactly as in negamax() (this
/// function participates in the same alpha-beta framework, just with a
/// restricted set of moves considered). `ply` is the absolute ply count
/// from the MAIN search's root (not reset on entry to quiescence) --
/// needed so any checkmate discovered inside quiescence itself still
/// gets a correctly ply-adjusted mate score (search.h's kMateScore
/// convention), consistent with negamax()'s own mate-score handling.
/// `nodes` is shared with the calling negamax() search, incremented
/// here exactly like negamax() increments its own.
///
/// `include_checks` controls whether non-capture, check-giving moves
/// are also searched (in addition to captures) at THIS call -- negamax()
/// always passes true on its initial call into quiescence, and every
/// recursive call this function makes into itself passes false. Checks
/// are deliberately not extended at every quiescence ply the way
/// captures are: unlike captures (which strictly deplete the board's
/// remaining material each time, guaranteeing eventual termination),
/// non-capturing checks don't reduce anything, so including them at
/// every recursive level risks a check-evade-check-evade chain running
/// far deeper than a horizon-effect fix is meant to cost -- especially
/// since Nightwing doesn't yet have repetition detection wired into
/// search (a separate, still-unchecked ROADMAP.md item) to recognize
/// and cut off a repeating pattern on its own. Bounding checks to the
/// first quiescence ply only keeps the useful case (spotting an
/// immediate tactical check right at the horizon) without that risk.
///
/// Precondition: same as negamax()'s own -- init_masks()/
/// init_magic_bitboards() have been called.
[[nodiscard]] int quiescence(board::Position& pos, int alpha, int beta, int ply,
                              std::uint64_t& nodes, bool include_checks) noexcept;

} // namespace nightwing::search
