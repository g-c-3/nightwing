// src/eval/piece_bonuses.cpp
//
// See piece_bonuses.h.

#include "eval/piece_bonuses.h"

#include "board/bitboard.h"
#include "board/masks.h"

namespace nightwing::eval {
namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Returns the relative-7th-rank index (0..7, LERF convention: 0=rank1
/// .. 7=rank8) for color `c` -- rank index 6 (rank7) for White, 1
/// (rank2) for Black. See piece_bonuses.h's kRookOnSeventhBonus.
[[nodiscard]] constexpr int seventh_rank_for(Color c) noexcept {
    return (c == Color::White) ? 6 : 1;
}

/// Returns the relative-8th-rank index (0..7) for color `c` -- rank index 7
/// (rank8) for White, 0 (rank1) for Black. Used to ask whether the ENEMY
/// king still sits on its own back rank.
[[nodiscard]] constexpr int eighth_rank_for(Color c) noexcept {
    return (c == Color::White) ? 7 : 0;
}

} // namespace

Score piece_bonus_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        Score side_score;

        // Bishop pair.
        if (board::popcount(pos.pieces(c, PieceType::Bishop)) >= 2) {
            side_score += kBishopPairBonus;
        }

        // Rooks: open/semi-open file and 7th rank.
        const Color enemy = board::opposite(c);
        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(enemy, PieceType::Pawn);
        const int seventh_rank = seventh_rank_for(c);

        // Rook-on-7th gate (CPW "Rook on Seventh Rank"): the bonus is only
        // earned when the rook has something to attack -- an enemy pawn
        // still on that rank, or the enemy king still confined to its own
        // back rank. Otherwise a rook on the 7th rank is just a rook on a
        // rank. This turns the flat bonus into a conjunction of "rook on
        // 7th" and "target present" (Session 231, ROADMAP.md restricted
        // nonlinear interactions, candidate 5); it adds no new constants.
        const Bitboard enemy_king_bb = pos.pieces(enemy, PieceType::King);
        const bool enemy_king_on_back_rank =
            enemy_king_bb != 0 &&
            board::rank_of(board::bitscan_forward(enemy_king_bb)) == eighth_rank_for(c);
        const bool seventh_has_target =
            enemy_king_on_back_rank || (enemy_pawns & board::rank_mask(seventh_rank)) != 0;

        Bitboard rooks = pos.pieces(c, PieceType::Rook);
        while (rooks != 0) {
            const Square sq = board::pop_lsb(rooks);
            const int file = board::file_of(sq);
            const Bitboard file_bb = board::file_mask(file);

            if ((own_pawns & file_bb) == 0) {
                if ((enemy_pawns & file_bb) == 0) {
                    side_score += kRookOpenFileBonus;
                } else {
                    side_score += kRookSemiOpenFileBonus;
                }
            }

            if (seventh_has_target && board::rank_of(sq) == seventh_rank) {
                side_score += kRookOnSeventhBonus;
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
