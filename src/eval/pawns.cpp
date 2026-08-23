// src/eval/pawns.cpp
//
// See pawns.h.

#include "eval/pawns.h"

#include <array>
#include <cstddef>

#include "board/masks.h"

namespace nightwing::eval {
namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Relative rank of `sq` from `c`'s own perspective: 0 = `c`'s own back
/// rank, 7 = the opposite back rank (where `c` promotes). Used only to
/// index kPassedPawnBonus (pawns.h) -- a real pawn's relative rank is
/// always in [1, 6] in any legal position (pawns never rest on rank 1
/// or 8).
[[nodiscard]] constexpr int relative_rank(Color c, Square sq) noexcept {
    const int rank = board::rank_of(sq);
    return c == Color::White ? rank : 7 - rank;
}

} // namespace

Score pawn_structure_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        const Color them = c == Color::White ? Color::Black : Color::White;
        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(them, PieceType::Pawn);

        // Per-file own-pawn counts, computed once up front: the doubled-
        // pawn check below needs "how many of my own pawns share this
        // file" for every pawn on it, and recomputing that via a fresh
        // mask-and-popcount per pawn would redo the same file's count
        // once for every pawn standing on it.
        std::array<int, board::kNumFiles> file_counts{};
        for (int f = 0; f < board::kNumFiles; ++f) {
            file_counts[static_cast<std::size_t>(f)] =
                board::popcount(own_pawns & board::file_mask(f));
        }

        Score side_score;
        Bitboard bb = own_pawns;
        while (bb != 0) {
            const Square sq = board::pop_lsb(bb);
            const int file = board::file_of(sq);

            const bool isolated = (own_pawns & board::adjacent_files_mask(file)) == 0;
            if (isolated) {
                side_score += kIsolatedPawnPenalty;
            }

            if (file_counts[static_cast<std::size_t>(file)] >= 2) {
                side_score += kDoubledPawnPenalty;
            }

            const bool passed = (enemy_pawns & board::passed_pawn_mask(c, sq)) == 0;
            if (passed) {
                side_score += kPassedPawnBonus[static_cast<std::size_t>(relative_rank(c, sq))];
            }

            // Connected: defended by, or standing beside (phalanx), a
            // friendly pawn. The general reverse-pawn-attack trick is
            // "square X is attacked by a pawn of color K exactly when
            // pawn_attacks(!K, X) intersects K's pawns" (pawn attack
            // patterns are symmetric under the rank-reflection that
            // swaps "attacks from" and "is defended from"). Defended-by-
            // friendly-pawn is "is `sq` attacked by a pawn of color
            // `c`," so K=c and this needs pawn_attacks(them, sq) --
            // NOT pawn_attacks(c, sq), which instead gives the squares
            // an ENEMY pawn would need to stand on to attack `sq` (that
            // form is what the backward-pawn push-square check below
            // uses, where K=them is exactly what's wanted there).
            // Phalanx (same-rank neighbor) is checked separately since
            // pawn_attacks() only ever covers the two diagonal squares,
            // never the same-rank ones a phalanx partner stands on.
            const bool defended = (board::pawn_attacks(them, sq) & own_pawns) != 0;
            const Bitboard phalanx_mask =
                board::adjacent_files_mask(file) & board::rank_mask(board::rank_of(sq));
            const bool phalanx = (own_pawns & phalanx_mask) != 0;
            if (defended || phalanx) {
                side_score += kConnectedPawnBonus;
            }

            // Backward: only meaningful for a pawn that isn't already
            // passed -- a passed pawn has no enemy pawns anywhere ahead
            // of it (by definition), so its push square can never be
            // enemy-pawn-attacked in the first place, making the second
            // half of this test vacuous for it anyway; skipping passed
            // pawns here is purely an efficiency short-circuit, not a
            // behavior change.
            if (!passed) {
                const bool has_support = (own_pawns & board::backward_support_mask(c, sq)) != 0;
                if (!has_support) {
                    const int push_rank =
                        c == Color::White ? board::rank_of(sq) + 1 : board::rank_of(sq) - 1;
                    // A pawn one push from promoting has no "push
                    // square" left within the board to check -- and per
                    // board.h, a real position never has a pawn resting
                    // on the promotion rank anyway (it would already
                    // have promoted), so this is defensive only.
                    if (push_rank >= 0 && push_rank < board::kNumRanks) {
                        const Square push_sq = board::make_square(file, push_rank);
                        const bool push_attacked =
                            (board::pawn_attacks(c, push_sq) & enemy_pawns) != 0;
                        if (push_attacked) {
                            side_score += kBackwardPawnPenalty;
                        }
                    }
                }
            }
        }

        if (c == Color::White) {
            score += side_score;
        } else {
            score -= side_score;
        }
    }

    return score;
}

} // namespace nightwing::eval
