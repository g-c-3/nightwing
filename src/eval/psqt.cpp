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
// Per DECISIONS.md (Tier 0 tuner-extension effort, 2026-09-08 (2), first
// step): every piece type now has a genuinely distinct mg/eg table pair
// -- previously only the king did (Michniewski's own original design),
// with the other five piece types sharing a single table for both
// phases. The five newly-split Eg tables below are, for NOW, exact
// duplicates of their Mg counterparts (Michniewski's baseline never
// published per-phase values for anything but the king), NOT a hand-
// guessed real split -- this session's change is purely structural
// plumbing (giving every piece the same mg/eg storage shape the king
// already had), so `ctest`/`bench` stay byte-for-byte identical before
// and after it. The point is to unblock the next Tier 0 step (a
// `PsqtWeights` runtime-mutable mirror, generalized `ParameterRef`) so
// the eventual Texel tuner can pull these Mg/Eg pairs apart into real,
// independently-learned values the same way it will for every other
// term -- exactly the same "separate fields, currently equal, so a
// tuner can later split them independently" convention psqt.h's own
// MaterialWeights struct already established for material values.

#include "eval/psqt.h"

namespace nightwing::eval {
namespace {

using board::Color;
using board::Piece;
using board::PieceType;
using board::Square;

// clang-format off

constexpr int kPawnMgTable[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// Duplicate of kPawnMgTable for now -- see file header comment (this is
// structural plumbing, not a hand-guessed real endgame split yet).
constexpr int kPawnEgTable[64] = {
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
constexpr int kKnightMgTable[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};

// Duplicate of kKnightMgTable for now -- see file header comment.
constexpr int kKnightEgTable[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};

constexpr int kBishopMgTable[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};

// Duplicate of kBishopMgTable for now -- see file header comment.
constexpr int kBishopEgTable[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};

constexpr int kRookMgTable[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// Duplicate of kRookMgTable for now -- see file header comment.
constexpr int kRookEgTable[64] = {
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
constexpr int kQueenMgTable[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};

// Duplicate of kQueenMgTable for now -- see file header comment.
constexpr int kQueenEgTable[64] = {
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
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {kPawnMgTable[idx], kPawnEgTable[idx]};
        }
        case PieceType::Knight: {
            // No color distinction for knights (see table comment) --
            // `sq` used directly, not mirrored.
            return {kKnightMgTable[sq], kKnightEgTable[sq]};
        }
        case PieceType::Bishop: {
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {kBishopMgTable[idx], kBishopEgTable[idx]};
        }
        case PieceType::Rook: {
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {kRookMgTable[idx], kRookEgTable[idx]};
        }
        case PieceType::Queen: {
            // No color distinction for queens (see table comment) --
            // `sq` used directly, not mirrored.
            return {kQueenMgTable[sq], kQueenEgTable[sq]};
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
