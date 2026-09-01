// src/eval/fortress.cpp
//
// See fortress.h for the overall scope and why this term deliberately
// does not consult eval::classify_endgame(). Recognition criteria,
// checked in order (any failure returns Score{} immediately):
//
// 1. No queens anywhere on the board -- queens are far too mobile and
//    tactically sharp for this simple structural heuristic to safely
//    assume a blockade is stable.
// 2. At most kFortressMaxNonPawnPieces knights/bishops/rooks combined,
//    both sides -- keeps this term endgame-scoped, the same posture
//    every other Phase 6 term in this codebase already takes, without
//    needing eval.h's own compute_phase() (this file has no dependency
//    on eval.cpp/eval.h, matching every other eval/*.cpp term's own
//    self-contained-module convention).
// 3. A nonzero material lead for one side (computed directly from
//    eval/psqt.h's own kPawnValue/kKnightValue/kBishopValue/
//    kRookValue constants -- queens excluded by construction, already
//    ruled out in step 1 -- using each constant's `eg` half only,
//    since this term is inherently an endgame-style judgment).
// 4. At least kFortressMinBlockedPawns pawns (either color, combined)
//    whose immediate push square is occupied by an opposing pawn --
//    the same "mutually blocked pawn" concept eval/
//    minor_piece_endgame.cpp's own KnightVsBishop term already uses,
//    re-derived locally here rather than shared (matching this
//    codebase's own established convention for this kind of small,
//    per-file geometric helper -- eval/pawns.cpp's, eval/
//    king_pawn_endgame.cpp's, eval/rook_endgame.cpp's, and eval/
//    minor_piece_endgame.cpp's own local relative_rank() already
//    re-derive an even more frequently needed helper the same way,
//    four separate times, without ever promoting it to a shared
//    header -- this file's blocked-pawn loop is no more deserving of
//    promotion than that one already-established precedent says it
//    is).
//
// If all four hold, the leading side's material lead is discounted --
// NOT zeroed out, and NOT flipped -- reflecting that a blocked,
// simplified, materially-imbalanced position is meaningfully more
// likely to be hard to convert than an otherwise-identical open one,
// without this term ever claiming the certainty eval/
// king_pawn_endgame.cpp's or eval/rook_endgame.cpp's own exactly-
// defined classical patterns can. The discount is proportional (half
// the material lead for `eg`, a quarter for `mg` -- reflecting lower
// confidence in this heuristic the further the position still is from
// a genuine endgame), first-draft hand estimates like every other
// constant in this codebase, not yet Texel-tuned.

#include "eval/fortress.h"

#include "eval/psqt.h"

namespace nightwing::eval {

Score fortress_value(const board::Position& pos) noexcept {
    using board::Bitboard;
    using board::Color;
    using board::PieceType;
    using board::Square;

    const int total_queens = board::popcount(pos.pieces(Color::White, PieceType::Queen)) +
                              board::popcount(pos.pieces(Color::Black, PieceType::Queen));
    if (total_queens > 0) {
        return Score{};
    }

    const int total_non_pawn_pieces =
        board::popcount(pos.pieces(Color::White, PieceType::Knight)) +
        board::popcount(pos.pieces(Color::Black, PieceType::Knight)) +
        board::popcount(pos.pieces(Color::White, PieceType::Bishop)) +
        board::popcount(pos.pieces(Color::Black, PieceType::Bishop)) +
        board::popcount(pos.pieces(Color::White, PieceType::Rook)) +
        board::popcount(pos.pieces(Color::Black, PieceType::Rook));
    if (total_non_pawn_pieces > kFortressMaxNonPawnPieces) {
        return Score{};
    }

    const int white_pawns = board::popcount(pos.pieces(Color::White, PieceType::Pawn));
    const int black_pawns = board::popcount(pos.pieces(Color::Black, PieceType::Pawn));

    const int material_lead_cp =
        (board::popcount(pos.pieces(Color::White, PieceType::Knight)) -
         board::popcount(pos.pieces(Color::Black, PieceType::Knight))) *
            kKnightValue.eg +
        (board::popcount(pos.pieces(Color::White, PieceType::Bishop)) -
         board::popcount(pos.pieces(Color::Black, PieceType::Bishop))) *
            kBishopValue.eg +
        (board::popcount(pos.pieces(Color::White, PieceType::Rook)) -
         board::popcount(pos.pieces(Color::Black, PieceType::Rook))) *
            kRookValue.eg +
        (white_pawns - black_pawns) * kPawnValue.eg;
    if (material_lead_cp == 0) {
        return Score{};
    }

    int blocked_pawns = 0;
    for (const Color c : {Color::White, Color::Black}) {
        const Color them = board::opposite(c);
        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(them, PieceType::Pawn);
        Bitboard bb = own_pawns;
        while (bb != 0) {
            const Square sq = board::pop_lsb(bb);
            const int push_rank =
                (c == Color::White) ? board::rank_of(sq) + 1 : board::rank_of(sq) - 1;
            if (push_rank < 0 || push_rank >= board::kNumRanks) {
                continue; // defensive only -- a real pawn never rests on the promotion rank
            }
            const Square push_sq = board::make_square(board::file_of(sq), push_rank);
            if ((enemy_pawns & board::square_bb(push_sq)) != 0) {
                ++blocked_pawns;
            }
        }
    }
    if (blocked_pawns < kFortressMinBlockedPawns) {
        return Score{};
    }

    const int eg_discount = material_lead_cp / 2;
    const int mg_discount = material_lead_cp / 4;
    return Score{-mg_discount, -eg_discount};
}

} // namespace nightwing::eval
