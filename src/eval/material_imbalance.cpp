// src/eval/material_imbalance.cpp
//
// See material_imbalance.h.

#include "eval/material_imbalance.h"

#include "board/bitboard.h"

namespace nightwing::eval {

Score material_imbalance_value(const board::Position& pos) noexcept {
    using board::Color;
    using board::PieceType;

    const int total_pawns = board::popcount(pos.pieces(Color::White, PieceType::Pawn)) +
                             board::popcount(pos.pieces(Color::Black, PieceType::Pawn));
    const int missing_pawns = kStartingTotalPawns - total_pawns;

    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        Score side_score;

        if (board::popcount(pos.pieces(c, PieceType::Bishop)) >= 2) {
            side_score += kBishopPairPerMissingPawn * missing_pawns;
        }

        if (board::popcount(pos.pieces(c, PieceType::Knight)) >= 2) {
            side_score += kKnightPairPerMissingPawn * missing_pawns;
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
