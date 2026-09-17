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
// table. This mirroring applies to EVERY piece type, including knight
// and queen — Michniewski's "one table for both colors" convention
// means each of the six tables below is stored only ONCE (not one per
// color), but that is a separate question from whether the LOOKUP into
// that single table still needs to flip the rank for Black, which it
// always does. For pawn/bishop/rook/king this happens to be invisible
// either way for four of those tables specifically because every one of
// Michniewski's per-rank rows for those types is ALSO left-right
// symmetric — but knight and queen's own rows are not row-for-row
// rank-mirror-symmetric (e.g. the knight table's rank-2 row is
// {-40,-20,0,0,0,0,-20,-40} while its rank-7 "mirror" row is
// {-40,-20,0,5,5,0,-20,-40} — visibly different), so skipping the
// mirror for those two specifically was a real, silent bug: Black's
// knights and queens were being scored against raw White-perspective
// table rows instead of the color-flipped lookup every other piece type
// already got. Fixed (docs/DECISIONS.md has the full bug account,
// including a fuzzer-based before/after measurement) by applying the
// exact same `color == Color::White ? sq : mirror_vertical(sq)` rule
// psqt_value() below already used for pawn/bishop/rook/king to knight
// and queen too — every table stays a single array either way, only the
// LOOKUP changed.
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

#include <algorithm>

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

// One stored table for both colors, per Michniewski's original — but
// Black's LOOKUP into it still mirrors the rank, same as every other
// piece type (see file header comment for the bug this fixes: this
// table's own rows are NOT rank-mirror-symmetric, e.g. row 1 vs. row 6
// below, so skipping the mirror was a real scoring bug for Black,
// unlike for pawn/bishop/rook/king where it happened to be invisible).
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

// Duplicate of kKnightMgTable for now -- see that table's own comment
// just above (mg vs. eg split) and the file header comment (mirroring
// bug fix) for what this table's own lookup rule is.
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

// One stored table for both colors, per Michniewski's original — but
// Black's LOOKUP into it still mirrors the rank via sq^56, same as
// every other piece type (see file header comment for the bug this
// fixes). This table is NOT row-for-row rank-mirror-symmetric (unlike
// pawn/bishop/rook/king), which is exactly why skipping the mirror for
// this table specifically was a real, visible scoring bug for Black,
// not a harmless simplification the way it happened to be for the
// symmetric tables.
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

// Duplicate of kQueenMgTable for now -- see that table's own comment
// just above (mg vs. eg split) and the file header comment (mirroring
// bug fix) for what this table's own lookup rule is.
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

PsqtWeights default_psqt_weights() noexcept {
    PsqtWeights w;
    std::copy(std::begin(kPawnMgTable), std::end(kPawnMgTable), w.pawn_mg.begin());
    std::copy(std::begin(kPawnEgTable), std::end(kPawnEgTable), w.pawn_eg.begin());
    std::copy(std::begin(kKnightMgTable), std::end(kKnightMgTable), w.knight_mg.begin());
    std::copy(std::begin(kKnightEgTable), std::end(kKnightEgTable), w.knight_eg.begin());
    std::copy(std::begin(kBishopMgTable), std::end(kBishopMgTable), w.bishop_mg.begin());
    std::copy(std::begin(kBishopEgTable), std::end(kBishopEgTable), w.bishop_eg.begin());
    std::copy(std::begin(kRookMgTable), std::end(kRookMgTable), w.rook_mg.begin());
    std::copy(std::begin(kRookEgTable), std::end(kRookEgTable), w.rook_eg.begin());
    std::copy(std::begin(kQueenMgTable), std::end(kQueenMgTable), w.queen_mg.begin());
    std::copy(std::begin(kQueenEgTable), std::end(kQueenEgTable), w.queen_eg.begin());
    std::copy(std::begin(kKingMgTable), std::end(kKingMgTable), w.king_mg.begin());
    std::copy(std::begin(kKingEgTable), std::end(kKingEgTable), w.king_eg.begin());
    return w;
}

Score psqt_value(Piece piece, Square sq, const PsqtWeights* weights) noexcept {
    const PieceType type = board::piece_type_of(piece);
    const Color color = board::color_of(piece);

    if (weights == nullptr) {
        switch (type) {
            case PieceType::Pawn: {
                const int idx = color == Color::White ? sq : mirror_vertical(sq);
                return {kPawnMgTable[idx], kPawnEgTable[idx]};
            }
            case PieceType::Knight: {
                // Same color/mirroring rule as every other piece type
                // below (docs/DECISIONS.md has the full account of the
                // bug this fixes -- this table is stored once, shared by
                // both colors, but Black's LOOKUP into it still needs to
                // mirror the rank, same as pawn/bishop/rook/king; only
                // the STORAGE is shared, not the lookup).
                const int idx = color == Color::White ? sq : mirror_vertical(sq);
                return {kKnightMgTable[idx], kKnightEgTable[idx]};
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
                // Same fix as Knight above -- see that case's comment.
                const int idx = color == Color::White ? sq : mirror_vertical(sq);
                return {kQueenMgTable[idx], kQueenEgTable[idx]};
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

    // `weights`-supplied path — same per-piece color/mirroring rules as
    // above, but reading (and round_to_int()-rounding, the same
    // convention material_value()'s own `weights` path uses) from the
    // caller-supplied PsqtWeights instead of the constexpr tables.
    switch (type) {
        case PieceType::Pawn: {
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {round_to_int(weights->pawn_mg[idx]), round_to_int(weights->pawn_eg[idx])};
        }
        case PieceType::Knight: {
            // Same fix, same bug, as the constexpr-table path above --
            // this second switch statement duplicated the missing-
            // mirror bug independently (its own copy of the lookup
            // logic, not shared code with the branch above), so it
            // needed the identical fix: any future PSQT tuning run
            // would otherwise have been fit against the same biased
            // signal for Black's knights/queens.
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {round_to_int(weights->knight_mg[idx]), round_to_int(weights->knight_eg[idx])};
        }
        case PieceType::Bishop: {
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {round_to_int(weights->bishop_mg[idx]), round_to_int(weights->bishop_eg[idx])};
        }
        case PieceType::Rook: {
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {round_to_int(weights->rook_mg[idx]), round_to_int(weights->rook_eg[idx])};
        }
        case PieceType::Queen: {
            // Same fix as Knight above -- see that case's comment.
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {round_to_int(weights->queen_mg[idx]), round_to_int(weights->queen_eg[idx])};
        }
        case PieceType::King: {
            const int idx = color == Color::White ? sq : mirror_vertical(sq);
            return {round_to_int(weights->king_mg[idx]), round_to_int(weights->king_eg[idx])};
        }
        case PieceType::None:
        default:
            return {0, 0};
    }
}

} // namespace nightwing::eval
