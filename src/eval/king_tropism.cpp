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

/// Returns the Chebyshev (king-move) distance between `a` and `b`: the
/// number of king moves it would take to travel from one square to the
/// other, i.e. max(|file difference|, |rank difference|) -- the
/// standard distance metric for this concept (CPW's own "Distance"
/// article: https://www.chessprogramming.org/Distance). Written as a
/// small local helper rather than added to board/bitboard.h: no other
/// eval/*.cpp file in this codebase currently needs a general
/// square-to-square distance function, so promoting it to a shared
/// header for a single caller isn't warranted yet -- the same
/// "not worth promoting to a shared table/function for one caller"
/// reasoning eval/king_safety.cpp's own shield_zone() and eval/
/// space.cpp's own space_zone() already establish.
[[nodiscard]] constexpr int chebyshev_distance(Square a, Square b) noexcept {
    const int file_diff = board::file_of(a) - board::file_of(b);
    const int rank_diff = board::rank_of(a) - board::rank_of(b);
    const int abs_file_diff = file_diff < 0 ? -file_diff : file_diff;
    const int abs_rank_diff = rank_diff < 0 ? -rank_diff : rank_diff;
    return abs_file_diff > abs_rank_diff ? abs_file_diff : abs_rank_diff;
}

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
                const int dist = chebyshev_distance(sq, enemy_king_sq);
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
