// src/eval/king_tropism.cpp
//
// See king_tropism.h.

#include "eval/king_tropism.h"

#include "board/bitboard.h"

namespace nightwing::eval {
namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Returns the tropism weight for a piece of `pt` (only ever called
/// with Knight/Bishop/Rook/Queen -- see king_tropism.cpp's own call
/// site).
[[nodiscard]] constexpr int tropism_weight(PieceType pt) noexcept {
    switch (pt) {
        case PieceType::Knight:
            return kKnightTropismWeight;
        case PieceType::Bishop:
            return kBishopTropismWeight;
        case PieceType::Rook:
            return kRookTropismWeight;
        case PieceType::Queen:
            return kQueenTropismWeight;
        case PieceType::Pawn:
        case PieceType::King:
        case PieceType::None:
        default:
            return 0;
    }
}

} // namespace

Score king_tropism_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        const Color enemy = board::opposite(c);
        const Bitboard enemy_king_bb = pos.pieces(enemy, PieceType::King);
        if (enemy_king_bb == 0) {
            // Defensive only -- every real, legally-reached position has
            // exactly one king per side (same guard eval/king_safety.cpp's
            // own king_safety_value() already applies, for the identical
            // reason: a hand-built test position could omit one, and
            // bitscan_forward() on an empty bitboard is undefined
            // behavior this function must never risk).
            continue;
        }
        const Square enemy_king_sq = board::bitscan_forward(enemy_king_bb);

        Score side_score;

        for (const PieceType pt :
             {PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen}) {
            Bitboard pieces = pos.pieces(c, pt);
            while (pieces != 0) {
                const Square sq = board::pop_lsb(pieces);
                const int dist = board::chebyshev_distance(sq, enemy_king_sq);
                const int proximity = kTropismMaxDistance - dist;
                if (proximity <= 0) {
                    continue;
                }
                const int units = tropism_weight(pt) * proximity;
                side_score += kTropismUnitBonus * units;
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
