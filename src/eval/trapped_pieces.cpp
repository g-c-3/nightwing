// src/eval/trapped_pieces.cpp
//
// See trapped_pieces.h.

#include "eval/trapped_pieces.h"

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

/// Returns the union of every square an enemy (i.e. `board::opposite(c)`)
/// pawn attacks -- computed once per side per trapped_piece_value() call
/// and reused for every own knight/bishop's safe-mobility check, the
/// same "compute once, reuse across every relevant check" precompute
/// eval/threats.cpp's own attacks_by_side() already establishes for a
/// related purpose (there, every piece type; here, deliberately pawns
/// only -- see trapped_pieces.h's header comment for why).
[[nodiscard]] Bitboard enemy_pawn_attacks(const Position& pos, Color c) noexcept {
    const Color enemy = board::opposite(c);
    Bitboard attacks = board::kEmptyBitboard;

    Bitboard pawns = pos.pieces(enemy, PieceType::Pawn);
    while (pawns != 0) {
        const Square sq = board::pop_lsb(pawns);
        attacks |= board::pawn_attacks(enemy, sq);
    }

    return attacks;
}

} // namespace

Score trapped_piece_value(const Position& pos) noexcept {
    Score score;
    const Bitboard occupied = pos.occupied();

    for (const Color c : {Color::White, Color::Black}) {
        const Bitboard own = pos.occupancy[static_cast<std::size_t>(c)];
        const Bitboard hostile_pawn_attacks = enemy_pawn_attacks(pos, c);

        Score side_score;

        Bitboard knights = pos.pieces(c, PieceType::Knight);
        while (knights != 0) {
            const Square sq = board::pop_lsb(knights);
            const Bitboard safe = board::knight_attacks(sq) & ~own & ~hostile_pawn_attacks;
            if (safe == 0) {
                side_score += kKnightTrappedPenalty;
            }
        }

        Bitboard bishops = pos.pieces(c, PieceType::Bishop);
        while (bishops != 0) {
            const Square sq = board::pop_lsb(bishops);
            const Bitboard safe =
                board::bishop_attacks(sq, occupied) & ~own & ~hostile_pawn_attacks;
            if (safe == 0) {
                side_score += kBishopTrappedPenalty;
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
