// src/eval/threats.cpp
//
// See threats.h.

#include "eval/threats.h"

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

/// Returns the union of every square a `c`-colored piece (of any type,
/// pawns and king included) currently attacks -- the same "attacks by
/// side" building block used to determine both which enemy pieces are
/// attacked and which own pieces are defended, computed once per side
/// per threats_value() call rather than recomputed per piece.
[[nodiscard]] Bitboard attacks_by_side(const Position& pos, Color c) noexcept {
    const Bitboard occupied = pos.occupied();
    Bitboard attacks = board::kEmptyBitboard;

    Bitboard pawns = pos.pieces(c, PieceType::Pawn);
    while (pawns != 0) {
        const Square sq = board::pop_lsb(pawns);
        attacks |= board::pawn_attacks(c, sq);
    }

    Bitboard knights = pos.pieces(c, PieceType::Knight);
    while (knights != 0) {
        const Square sq = board::pop_lsb(knights);
        attacks |= board::knight_attacks(sq);
    }

    Bitboard bishops = pos.pieces(c, PieceType::Bishop);
    while (bishops != 0) {
        const Square sq = board::pop_lsb(bishops);
        attacks |= board::bishop_attacks(sq, occupied);
    }

    Bitboard rooks = pos.pieces(c, PieceType::Rook);
    while (rooks != 0) {
        const Square sq = board::pop_lsb(rooks);
        attacks |= board::rook_attacks(sq, occupied);
    }

    Bitboard queens = pos.pieces(c, PieceType::Queen);
    while (queens != 0) {
        const Square sq = board::pop_lsb(queens);
        attacks |= board::queen_attacks(sq, occupied);
    }

    // Exactly one king per side in any real Position -- read
    // non-destructively via bitscan_forward(), same pattern
    // eval/king_safety.cpp's own king-square extraction already uses,
    // rather than pop_lsb() (which would require a mutable copy for a
    // single, already-known-present bit).
    const Bitboard king_bb = pos.pieces(c, PieceType::King);
    attacks |= board::king_attacks(board::bitscan_forward(king_bb));

    return attacks;
}

/// Returns the attacked-by-an-enemy-pawn penalty for a piece of `pt`
/// (only ever called with Knight/Bishop/Rook/Queen -- see threats.cpp's
/// own call site).
[[nodiscard]] constexpr Score pawn_threat_penalty(PieceType pt) noexcept {
    switch (pt) {
        case PieceType::Knight:
            return kKnightAttackedByPawnPenalty;
        case PieceType::Bishop:
            return kBishopAttackedByPawnPenalty;
        case PieceType::Rook:
            return kRookAttackedByPawnPenalty;
        case PieceType::Queen:
            return kQueenAttackedByPawnPenalty;
        case PieceType::Pawn:
        case PieceType::King:
        case PieceType::None:
        default:
            return {0, 0};
    }
}

/// Returns the hanging-piece penalty for a piece of `pt` (only ever
/// called with Knight/Bishop/Rook/Queen -- see threats.cpp's own call
/// site).
[[nodiscard]] constexpr Score hanging_penalty(PieceType pt) noexcept {
    switch (pt) {
        case PieceType::Knight:
            return kKnightHangingPenalty;
        case PieceType::Bishop:
            return kBishopHangingPenalty;
        case PieceType::Rook:
            return kRookHangingPenalty;
        case PieceType::Queen:
            return kQueenHangingPenalty;
        case PieceType::Pawn:
        case PieceType::King:
        case PieceType::None:
        default:
            return {0, 0};
    }
}

} // namespace

Score threats_value(const Position& pos) noexcept {
    Score score;

    // Computed once per side, reused for every piece below rather than
    // recomputed per piece.
    const Bitboard white_attacks = attacks_by_side(pos, Color::White);
    const Bitboard black_attacks = attacks_by_side(pos, Color::Black);

    for (const Color c : {Color::White, Color::Black}) {
        const Color enemy = board::opposite(c);
        const Bitboard enemy_pawns = pos.pieces(enemy, PieceType::Pawn);
        const Bitboard& enemy_attacks = (c == Color::White) ? black_attacks : white_attacks;
        const Bitboard& own_attacks = (c == Color::White) ? white_attacks : black_attacks;

        Score side_score;

        for (const PieceType pt :
             {PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen}) {
            Bitboard pieces = pos.pieces(c, pt);
            while (pieces != 0) {
                const Square sq = board::pop_lsb(pieces);

                // Reverse-pawn-attack trick (eval/pawns.cpp's own
                // "Connected" check establishes the pattern, reused
                // identically in eval/space.cpp): `sq` is attacked by
                // an enemy pawn exactly when pawn_attacks(c, sq)
                // intersects the enemy's own pawns.
                const bool attacked_by_pawn = (board::pawn_attacks(c, sq) & enemy_pawns) != 0;
                if (attacked_by_pawn) {
                    side_score += pawn_threat_penalty(pt);
                }

                const bool attacked = board::test_bit(enemy_attacks, sq);
                const bool defended = board::test_bit(own_attacks, sq);
                if (attacked && !defended) {
                    side_score += hanging_penalty(pt);
                }
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
