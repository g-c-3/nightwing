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

            if (board::rank_of(sq) == seventh_rank) {
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
