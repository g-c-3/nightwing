// src/eval/king_exposure.cpp
//
// See king_exposure.h.

#include "eval/king_exposure.h"

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

constexpr int kKnightAttackUnits = 1;
constexpr int kBishopAttackUnits = 1;
constexpr int kRookAttackUnits = 2;
constexpr int kQueenAttackUnits = 4;

/// Returns the squares of the 2-rank-deep strip directly in front of a
/// king of color `c` on file `f` (empty if `f` is off the board).
/// File-private duplicate of the shield-zone geometry in king_safety.cpp
/// (this project's convention for small file-private geometry helpers).
[[nodiscard]] Bitboard shield_file_zone(Color c, int f, int king_rank) noexcept {
    Bitboard zone = board::kEmptyBitboard;
    if (f < 0 || f >= board::kNumFiles) {
        return zone;
    }
    const int direction = (c == Color::White) ? 1 : -1;
    for (int dr = 1; dr <= 2; ++dr) {
        const int r = king_rank + direction * dr;
        if (r < 0 || r >= board::kNumRanks) {
            continue;
        }
        board::set_bit(zone, board::make_square(f, r));
    }
    return zone;
}

/// Weighted count of `attacker`'s knights/bishops/rooks/queens attacking
/// at least one square of `zone`. Same weights and rule as
/// king_safety.cpp's attack_units_on().
[[nodiscard]] int attack_units_on(const Position& pos, Color attacker, Bitboard zone) noexcept {
    const Bitboard occupied = pos.occupied();
    int units = 0;

    Bitboard knights = pos.pieces(attacker, PieceType::Knight);
    while (knights != 0) {
        const Square sq = board::pop_lsb(knights);
        if ((board::knight_attacks(sq) & zone) != 0) {
            units += kKnightAttackUnits;
        }
    }
    Bitboard bishops = pos.pieces(attacker, PieceType::Bishop);
    while (bishops != 0) {
        const Square sq = board::pop_lsb(bishops);
        if ((board::bishop_attacks(sq, occupied) & zone) != 0) {
            units += kBishopAttackUnits;
        }
    }
    Bitboard rooks = pos.pieces(attacker, PieceType::Rook);
    while (rooks != 0) {
        const Square sq = board::pop_lsb(rooks);
        if ((board::rook_attacks(sq, occupied) & zone) != 0) {
            units += kRookAttackUnits;
        }
    }
    Bitboard queens = pos.pieces(attacker, PieceType::Queen);
    while (queens != 0) {
        const Square sq = board::pop_lsb(queens);
        if ((board::queen_attacks(sq, occupied) & zone) != 0) {
            units += kQueenAttackUnits;
        }
    }
    return units;
}

} // namespace

int king_exposure_level(const Position& pos, Color side) noexcept {
    const Bitboard king_bb = pos.pieces(side, PieceType::King);
    if (king_bb == 0) {
        return 0;
    }
    const Square king_sq = board::bitscan_forward(king_bb);
    const int king_file = board::file_of(king_sq);
    const int king_rank = board::rank_of(king_sq);
    const Bitboard own_pawns = pos.pieces(side, PieceType::Pawn);

    int exposed = 0;
    for (int df = -1; df <= 1; ++df) {
        const int f = king_file + df;
        if (f < 0 || f >= board::kNumFiles) {
            continue;
        }
        if ((own_pawns & shield_file_zone(side, f, king_rank)) == 0) {
            ++exposed;
        }
    }
    return exposed;
}

Score king_exposure_value(const Position& pos, const KingExposureWeights* weights) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        const Bitboard king_bb = pos.pieces(c, PieceType::King);
        if (king_bb == 0) {
            continue; // Defensive: hand-built test positions may omit a king.
        }

        const int exposure = king_exposure_level(pos, c);
        if (exposure == 0) {
            continue; // Intact shelter: the interaction term is zero.
        }

        const Square king_sq = board::bitscan_forward(king_bb);
        const Bitboard king_zone = board::king_attacks(king_sq) | king_bb;
        const int units = attack_units_on(pos, board::opposite(c), king_zone);
        const int product = units * exposure;

        const Score side_score =
            weights == nullptr
                ? kExposureAttackPenalty * product
                : Score{round_to_int(weights->attack_exposure_mg * product),
                        round_to_int(weights->attack_exposure_eg * product)};

        if (c == Color::White) {
            score += side_score;
        } else {
            score -= side_score;
        }
    }

    return score;
}

} // namespace nightwing::eval
