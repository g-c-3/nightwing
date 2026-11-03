// src/eval/piece_bonuses.cpp
//
// See piece_bonuses.h.

#include "eval/piece_bonuses.h"

#include "board/bitboard.h"
#include "board/masks.h"
#include "eval/psqt.h" // round_to_int()

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

PieceBonusWeights default_piece_bonus_weights() noexcept {
    return PieceBonusWeights{};
}

Score piece_bonus_value(const Position& pos, const PieceBonusWeights* weights) noexcept {
    // Resolve the four bonuses once per call: the compiled-in constants by
    // default, or the caller's rounded override (DECISIONS.md 2026-11-03 (7)).
    const Score bishop_pair = (weights == nullptr)
                                  ? kBishopPairBonus
                                  : Score{round_to_int(weights->bishop_pair_mg),
                                          round_to_int(weights->bishop_pair_eg)};
    const Score rook_open = (weights == nullptr)
                                ? kRookOpenFileBonus
                                : Score{round_to_int(weights->rook_open_file_mg),
                                        round_to_int(weights->rook_open_file_eg)};
    const Score rook_semi_open = (weights == nullptr)
                                     ? kRookSemiOpenFileBonus
                                     : Score{round_to_int(weights->rook_semi_open_file_mg),
                                             round_to_int(weights->rook_semi_open_file_eg)};
    const Score rook_seventh = (weights == nullptr)
                                   ? kRookOnSeventhBonus
                                   : Score{round_to_int(weights->rook_on_seventh_mg),
                                           round_to_int(weights->rook_on_seventh_eg)};

    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        Score side_score;

        // Bishop pair.
        if (board::popcount(pos.pieces(c, PieceType::Bishop)) >= 2) {
            side_score += bishop_pair;
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
                    side_score += rook_open;
                } else {
                    side_score += rook_semi_open;
                }
            }

            if (board::rank_of(sq) == seventh_rank) {
                side_score += rook_seventh;
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
