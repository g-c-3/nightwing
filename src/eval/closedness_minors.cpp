// src/eval/closedness_minors.cpp
//
// See closedness_minors.h.

#include "eval/closedness_minors.h"

#include "board/bitboard.h"

namespace nightwing::eval {

int blocked_pawn_pairs(const board::Position& pos) noexcept {
    using board::Color;
    using board::PieceType;
    // Square index grows with rank, so a White pawn's push square is the
    // pawn bitboard shifted left by one rank (8 squares).
    const board::Bitboard white_pawns = pos.pieces(Color::White, PieceType::Pawn);
    const board::Bitboard black_pawns = pos.pieces(Color::Black, PieceType::Pawn);
    return board::popcount((white_pawns << 8) & black_pawns);
}

Score closedness_minors_value(const board::Position& pos) noexcept {
    using board::Color;
    using board::PieceType;

    const int blocked = blocked_pawn_pairs(pos);
    if (blocked == 0) {
        return Score{};
    }
    const int knights = board::popcount(pos.pieces(Color::White, PieceType::Knight)) -
                        board::popcount(pos.pieces(Color::Black, PieceType::Knight));
    const int bishops = board::popcount(pos.pieces(Color::White, PieceType::Bishop)) -
                        board::popcount(pos.pieces(Color::Black, PieceType::Bishop));
    return kKnightPerBlockedPair * (knights * blocked) +
           kBishopPerBlockedPair * (bishops * blocked);
}

} // namespace nightwing::eval
