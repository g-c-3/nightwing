// src/eval/mobility.cpp
//
// See mobility.h.

#include "eval/mobility.h"

#include <cstddef>

#include "board/attacks.h"
#include "board/bitboard.h"
#include "board/masks.h"

namespace nightwing::eval {
namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

} // namespace

Score mobility_value(const Position& pos, const MobilityWeights* weights) noexcept {
    Score score;
    const Bitboard occupied = pos.occupied();

    const Score knight_bonus = weights == nullptr
                                    ? kKnightMobilityBonus
                                    : Score{round_to_int(weights->knight_mg), round_to_int(weights->knight_eg)};
    const Score bishop_bonus = weights == nullptr
                                    ? kBishopMobilityBonus
                                    : Score{round_to_int(weights->bishop_mg), round_to_int(weights->bishop_eg)};
    const Score rook_bonus = weights == nullptr
                                  ? kRookMobilityBonus
                                  : Score{round_to_int(weights->rook_mg), round_to_int(weights->rook_eg)};
    const Score queen_bonus = weights == nullptr
                                   ? kQueenMobilityBonus
                                   : Score{round_to_int(weights->queen_mg), round_to_int(weights->queen_eg)};

    for (const Color c : {Color::White, Color::Black}) {
        const Bitboard own = pos.occupancy[static_cast<std::size_t>(c)];
        Score side_score;

        // Knights: attacks() alone -- a leaper isn't blocked by
        // intervening pieces, so `occupied` never enters the picture,
        // only `own` (to exclude squares this same knight couldn't
        // actually move to).
        Bitboard knights = pos.pieces(c, PieceType::Knight);
        while (knights != 0) {
            const Square sq = board::pop_lsb(knights);
            const Bitboard attacks = board::knight_attacks(sq) & ~own;
            side_score += knight_bonus * board::popcount(attacks);
        }

        Bitboard bishops = pos.pieces(c, PieceType::Bishop);
        while (bishops != 0) {
            const Square sq = board::pop_lsb(bishops);
            const Bitboard attacks = board::bishop_attacks(sq, occupied) & ~own;
            side_score += bishop_bonus * board::popcount(attacks);
        }

        Bitboard rooks = pos.pieces(c, PieceType::Rook);
        while (rooks != 0) {
            const Square sq = board::pop_lsb(rooks);
            const Bitboard attacks = board::rook_attacks(sq, occupied) & ~own;
            side_score += rook_bonus * board::popcount(attacks);
        }

        Bitboard queens = pos.pieces(c, PieceType::Queen);
        while (queens != 0) {
            const Square sq = board::pop_lsb(queens);
            const Bitboard attacks = board::queen_attacks(sq, occupied) & ~own;
            side_score += queen_bonus * board::popcount(attacks);
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
