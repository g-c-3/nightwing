// src/eval/passed_pawn_king.cpp
//
// See passed_pawn_king.h.

#include "eval/passed_pawn_king.h"

#include <algorithm>

#include "board/bitboard.h"
#include "board/masks.h"

namespace nightwing::eval {

Score passed_pawn_king_value(const board::Position& pos) noexcept {
    using board::Bitboard;
    using board::Color;
    using board::PieceType;
    using board::Square;

    int eg_total = 0;

    for (const Color c : {Color::White, Color::Black}) {
        const Color enemy = board::opposite(c);
        const Bitboard own_king_bb = pos.pieces(c, PieceType::King);
        const Bitboard enemy_king_bb = pos.pieces(enemy, PieceType::King);
        if (own_king_bb == 0 || enemy_king_bb == 0) {
            continue; // Defensive: hand-built test positions may omit a king.
        }
        const Square own_king = board::bitscan_forward(own_king_bb);
        const Square enemy_king = board::bitscan_forward(enemy_king_bb);
        const Bitboard enemy_pawns = pos.pieces(enemy, PieceType::Pawn);

        int side_eg = 0;
        Bitboard pawns = pos.pieces(c, PieceType::Pawn);
        while (pawns != 0) {
            const Square sq = board::pop_lsb(pawns);
            if ((board::passed_pawn_mask(c, sq) & enemy_pawns) != 0) {
                continue; // Not passed.
            }
            const int rank = board::rank_of(sq);
            const int rel_rank = (c == Color::White) ? rank : 7 - rank;
            if (rel_rank < 1 || rel_rank > 6) {
                continue; // Cannot occur in a legal position; guards the stop square.
            }
            const int weight = kPassedKingRankWeight[static_cast<std::size_t>(rel_rank)];
            if (weight == 0) {
                continue;
            }
            const Square stop = (c == Color::White) ? sq + 8 : sq - 8;
            const int enemy_dist = std::min(board::chebyshev_distance(enemy_king, stop), kPassedKingDistCap);
            const int own_dist = std::min(board::chebyshev_distance(own_king, stop), kPassedKingDistCap);
            side_eg += weight * (kPassedEnemyKingDistFactor * enemy_dist - kPassedOwnKingDistFactor * own_dist);
        }
        eg_total += (c == Color::White) ? side_eg : -side_eg;
    }

    return Score{0, eg_total};
}

} // namespace nightwing::eval
