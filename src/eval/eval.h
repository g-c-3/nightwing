#pragma once
// src/eval/eval.h
//
// Top-level static evaluation. Phase 2 baseline: material + tapered
// piece-square tables only (ARCHITECTURE.md's planned mobility/king
// safety/pawn structure/etc. terms land incrementally in Phase 5).

#include "board/board.h"

namespace nightwing::eval {

/// Evaluates `pos` and returns a centipawn score from White's
/// perspective: positive means White stands better, negative means
/// Black stands better, 0 is balanced. Search code (Phase 2's alpha-beta,
/// once it exists) is responsible for negating this for Black-to-move
/// nodes if it wants a side-to-move-relative score — evaluate() itself
/// always answers "how good is this position for White," which keeps
/// the function trivially testable independent of whose turn it is.
///
/// Currently a full from-scratch recomputation each call (scans all 64
/// squares) rather than an incremental accumulator updated on
/// make/unmake — ARCHITECTURE.md's "eval on the fly" accumulator
/// pattern is real but deliberately deferred; see DECISIONS.md for why
/// this is the right tradeoff for Phase 2's "get something playing"
/// goal specifically, and revisit once eval has enough terms and a
/// profiled hot path to justify the accumulator's extra bookkeeping.
[[nodiscard]] int evaluate(const board::Position& pos) noexcept;

} // namespace nightwing::eval
