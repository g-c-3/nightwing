#pragma once
// src/search/see.h
//
// Static Exchange Evaluation (SEE): given a capture, estimates the net
// material result of the full exchange sequence on that square (every
// side recapturing with its least valuable attacker, each side stopping
// only when continuing would lose material) WITHOUT doing a real
// minimax search -- just a fast, material-only simulation. Used by
// quiescence search (search/quiescence.h) to prune captures that lose
// material even after all reasonable recaptures, so qsearch doesn't
// waste time fully searching an obviously-bad trade.
//
// Technique: the classic "SEE swap algorithm" (Chess Programming Wiki,
// "Static Exchange Evaluation" and "SEE - The Swap Algorithm") --
// iteratively find the least valuable attacker of the target square,
// alternating sides, tracking a gain array, then resolve it backward
// with each side choosing to stop capturing if continuing wouldn't
// help. From-scratch implementation of this public, well-documented
// algorithm; no code copied.

#include "board/board.h"
#include "board/move.h"

namespace nightwing::search {

/// Estimates the net material result (in the same centipawn units as
/// eval::material_value(), positive = good for the side making `move`)
/// of the full capture/recapture sequence on `move.to()`, without a
/// real search. `pos` must not have had `move` applied yet.
/// Precondition: `move.is_capture()` -- SEE is only meaningful for
/// captures; calling it on a quiet move is a logic error in the caller,
/// not something this function tries to detect or handle gracefully.
///
/// Deliberately ignores whether intermediate hypothetical recaptures in
/// the simulated sequence would themselves be legal (e.g. a recapture
/// that would expose the recapturing side's own king to check) -- this
/// is a well-known, universally-accepted simplification of the
/// technique (CPW), not an oversight: checking full legality at every
/// hypothetical step would turn a cheap material estimate into a real
/// search, defeating its purpose as a fast pruning heuristic.
[[nodiscard]] int static_exchange_evaluation(const board::Position& pos, board::Move move) noexcept;

} // namespace nightwing::search
