#pragma once
// src/eval/closedness_minors.h
//
// Evaluation weights chosen by pawn structure: the knight/bishop balance
// shifts with how closed the pawn structure is (CPW "Knight",
// https://www.chessprogramming.org/Knight, and "Bishop",
// https://www.chessprogramming.org/Bishop: a closed position, with many
// mutually blocked pawns, favors knights, which hop over the blockade;
// an open one favors bishops). From-scratch implementation, no code copied
// (ARCHITECTURE.md Attribution Policy).
//
// Scope: one idea only -- a per-knight bonus and a per-bishop penalty,
// each scaled by the number of mutually blocked pawn pairs. minor_piece_
// endgame.h applies the same principle only inside classified knight-vs-
// bishop endings; this term applies it in every position. The constants
// are hand estimates, untuned.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Bonus per own knight, per mutually blocked pawn pair (a White pawn with
/// a Black pawn directly in front of it). Hand estimate, untuned.
inline constexpr Score kKnightPerBlockedPair = {3, 2};

/// Penalty (negative) per own bishop, per mutually blocked pawn pair.
/// Hand estimate, untuned.
inline constexpr Score kBishopPerBlockedPair = {-3, -2};

/// Returns the number of mutually blocked pawn pairs in `pos`: White pawns
/// whose next square holds a Black pawn (0 to 8).
[[nodiscard]] int blocked_pawn_pairs(const board::Position& pos) noexcept;

/// Returns the White-relative closedness score: for each side, the knight
/// count times `kKnightPerBlockedPair` plus the bishop count times
/// `kBishopPerBlockedPair`, each times `blocked_pawn_pairs(pos)`, White
/// minus Black. Zero when no pawns are blocked.
[[nodiscard]] Score closedness_minors_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
