// src/eval/rook_files.h
//
// Rook activity scaled by board-wide file openness (candidate 3 of
// ROADMAP.md's "Restricted nonlinear feature interactions" item).
//
// This is an INTERACTION term: the bonus is (rook count) x (number of
// files of a given kind on the whole board), a product of two features,
// not a sum of independent ones. It deliberately measures a quantity the
// existing rook terms do not:
//   * eval/piece_bonuses.h rewards a rook standing ON an open or
//     semi-open file (the rook's own file only).
//   * eval/mobility.h rewards the squares a rook attacks.
//   * This term ignores which file the rook stands on. It counts how many
//     files are fully open (no pawns of either side) and how many are
//     semi-open FOR THIS SIDE (no own pawn, at least one enemy pawn), and
//     scales by the number of rooks (capped at 2) that could use them.
//     The idea is that rooks are worth more on an open board than on a
//     closed one, wherever they currently stand.
//
// Idea source: CPW (Chess Programming Wiki), "Evaluation of Pieces",
// rook on open file and board openness discussion. Formula and constants
// are original; the constants are untuned starting values to be Texel
// fitted before the SPRT (docs/DECISIONS.md 2026-10-06 (5)).
//
// Shape follows eval/space.h (plain double fields, constexpr defaults,
// nullable override) so the tuner plumbing in src/tuner/tune.h can reuse
// the Tier 0 Step 8a pattern.

#pragma once

#include "board/board.h"
#include "eval/psqt.h" // round_to_int()
#include "eval/score.h"

namespace nightwing::eval {

/// Bonus per rook (at most 2 rooks counted) per fully open file on the
/// board. Untuned starting value.
inline constexpr Score kRookOpenFilesBonus = {2, 3};

/// Bonus per rook (at most 2 rooks counted) per file that is semi-open
/// for the rook's side (no own pawn, at least one enemy pawn). Untuned.
inline constexpr Score kRookSemiOpenFilesBonus = {1, 2};

/// Maximum number of rooks per side that contribute to this term.
inline constexpr int kRookFilesMaxRooks = 2;

/// Runtime-overridable weights for rook_files_value(), for the tuner.
/// Field defaults name the constants above directly.
struct RookFilesWeights {
    double open_mg = kRookOpenFilesBonus.mg;
    double open_eg = kRookOpenFilesBonus.eg;
    double semi_open_mg = kRookSemiOpenFilesBonus.mg;
    double semi_open_eg = kRookSemiOpenFilesBonus.eg;
};

/// Returns the compiled-in defaults as a RookFilesWeights.
[[nodiscard]] constexpr RookFilesWeights default_rook_files_weights() noexcept {
    return RookFilesWeights{};
}

/// Returns the White-perspective score (White minus Black) of this term
/// for `pos`. `weights`, if non-null, replaces the compiled-in constants
/// (each field rounded with round_to_int()); nullptr uses the constants.
[[nodiscard]] Score rook_files_value(const board::Position& pos,
                                      const RookFilesWeights* weights = nullptr) noexcept;

} // namespace nightwing::eval
