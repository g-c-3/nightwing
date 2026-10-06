// src/eval/bishop_pair_closedness.cpp
//
// See bishop_pair_closedness.h.

#include "eval/bishop_pair_closedness.h"

#include "board/bitboard.h"

namespace nightwing::eval {

namespace {

/// Files c, d, e, f (file indices 2..5) on every rank.
inline constexpr board::Bitboard kCentralFiles = 0x3C3C3C3C3C3C3C3CULL;

} // namespace

int locked_central_pawn_pairs(const board::Position& pos) noexcept {
    using board::Color;
    using board::PieceType;

    const board::Bitboard white_pawns = pos.pieces(Color::White, PieceType::Pawn);
    const board::Bitboard black_pawns = pos.pieces(Color::Black, PieceType::Pawn);

    // LERF mapping: "in front of" a White pawn is one rank up (+8). Shifting
    // Black's pawns down one rank puts each on the square directly BEHIND
    // it from White's view, so the intersection is the White pawns that are
    // directly blocked by a Black pawn -- one bit per locked pair.
    const board::Bitboard locked = white_pawns & (black_pawns >> 8) & kCentralFiles;
    return board::popcount(locked);
}

Score bishop_pair_closedness_value(const board::Position& pos) noexcept {
    using board::Color;
    using board::PieceType;

    const bool white_pair = board::popcount(pos.pieces(Color::White, PieceType::Bishop)) >= 2;
    const bool black_pair = board::popcount(pos.pieces(Color::Black, PieceType::Bishop)) >= 2;
    if (!white_pair && !black_pair) {
        return Score{0, 0};
    }

    const Score per_side = kBishopPairPerLockedCentralPair * locked_central_pawn_pairs(pos);

    Score score;
    if (white_pair) {
        score += per_side;
    }
    if (black_pair) {
        score -= per_side;
    }
    return score;
}

} // namespace nightwing::eval
