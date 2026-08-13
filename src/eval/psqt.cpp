// src/eval/psqt.cpp
//
// Attribution: all table values below are Tomasz Michniewski's
// "Simplified Evaluation Function" piece-square tables, originally
// posted to the Polish chess-programming mailing list and published on
// the Chess Programming Wiki:
// https://www.chessprogramming.org/Simplified_Evaluation_Function
//
// Each table here is indexed by our own LERF Square convention (0 = a1,
// 63 = h8, square = rank*8 + file — see board/bitboard.h) with values
// taken from White's perspective; Black's value for a given square is
// looked up via a vertical mirror (`sq ^ 56`, which flips the rank and
// keeps the file — see psqt_value() below) rather than a second stored
// table, since every one of Michniewski's per-rank rows for pawn/
// bishop/rook/king is left-right symmetric, which is exactly the
// condition that makes "reverse the 64-entry array" (his own documented
// approach) equivalent to "mirror the rank and reuse the same row."
// Knight and queen use one table for both colors, matching Michniewski's
// original (no color distinction for those two piece types).
//
// These numbers were cross-checked against two independently published
// transcriptions before being typed in here (the Chess Programming Wiki
// page's own inline table excerpts, and a from-scratch C# port by Adam
// Berent) after a third transcription (an unrelated public GitHub
// Python port) turned out to contain two sign-flip typos in the king
// middlegame/endgame tables (a1-file rank-1/rank-8 corner entries shown
// as positive instead of negative). Those typos are NOT reproduced
// here — the values below are the corrected/cross-verified ones.
//
// Per DECISIONS.md: only the king gets a genuinely distinct mg/eg table
// pair (Michniewski's original design). The other five piece types
// reuse their single table for both phases via material_value()/
// psqt_value() until Phase 5's Texel tuner has enough terms to learn
// real per-phase splits instead of us hand-guessing them.

#include "eval/psqt.h"

namespace nightwing::eval {
namespace {

using board::Color;
using board::Piece;
using board::PieceType;
using board::Square;

// clang-format off

constexpr int kPawnTable[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// Same table for both colors, per Michniewski's original (see file
// header comment).
constexpr int kKnightTable[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};

constexpr int kBishopTable[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};

constexpr int kRookTable[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// Same table for both colors, per Michniewski's original (see file
// header comment) — note this table is not rank-mirror-symmetric, so
// unlike pawn/bishop/rook/king it genuinely couldn't be derived via the
// sq^56 trick even if we wanted a per-color version.
constexpr int kQueenTable[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};

constexpr int kKingMgTable[64] = {
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
};

constexpr int kKingEgTable[64] = {
    -50,-30,-30,-30,-30,-30,-30,-50,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -50,-40,-30,-20,-20,-30,-40,-50,
};

// clang-format on

/// Vertically mirrors a square (flips rank, keeps file) — used to turn
/// a White-perspective table lookup into a Black one for the piece
/// types whose rows are left-right symmetric (see file header comment).
[[nodiscard]] constexpr Square mirror_vertical(Square sq) noexcept {
    return sq ^ 56;
}

} // namespace

Score psqt_value(Piece piece, Square sq) noexcept {
    const PieceType type = board::piece_type_of(piece);
    const Color color = board::color_of(piece);

    switch (type) {
        case PieceType::Pawn: {
            const int v = kPawnTable[color == Color::White ? sq : mirror_vertical(sq)];
            return {v, v};
        }
        case PieceType::Knight: {
            const int v = kKnightTable[sq];
            return {v, v};
        }
        case PieceType::Bishop: {
            const int v = kBishopTable[color == Color::White ? sq : mirror_vertical(sq)];
            return {v, v};
        }
        case PieceType::Rook: {
            const int v = kRookTable[color == Color::White ? sq : mirror_vertical(sq)];
            return {v, v};
        }
        case PieceType::Queen: {
            const int v = kQueenTable[sq];
            return {v, v};
        }
        case PieceType::King: {
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {kKingMgTable[idx], kKingEgTable[idx]};
        }
        case PieceType::None:
        default:
            return {0, 0};
    }
}

} // namespace nightwing::eval
