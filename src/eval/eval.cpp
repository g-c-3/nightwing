// src/eval/eval.cpp

#include "eval/eval.h"

#include "eval/pawns.h"
#include "eval/psqt.h"
#include "eval/score.h"

namespace nightwing::eval {
namespace {

/// Computes the current game phase in [0, kMaxPhase] from remaining
/// non-pawn material on the board — the standard CPW "Tapered Eval"
/// technique (see score.h's header comment for the citation); this is
/// a from-scratch implementation of that general approach, not copied
/// code. kMaxPhase (full starting non-pawn material) means "fully
/// middlegame," 0 means "fully endgame."
[[nodiscard]] int compute_phase(const board::Position& pos) noexcept {
    using board::Color;
    using board::PieceType;

    int phase = kMaxPhase;
    for (Color c : {Color::White, Color::Black}) {
        phase -= board::popcount(pos.pieces(c, PieceType::Knight)) * kKnightPhase;
        phase -= board::popcount(pos.pieces(c, PieceType::Bishop)) * kBishopPhase;
        phase -= board::popcount(pos.pieces(c, PieceType::Rook)) * kRookPhase;
        phase -= board::popcount(pos.pieces(c, PieceType::Queen)) * kQueenPhase;
    }
    // Defensive only: a legal position can't exceed starting non-pawn
    // material, but promotions (once search reaches positions with
    // extra queens on the board) could in principle push this negative
    // without this clamp.
    return phase < 0 ? 0 : phase;
}

} // namespace

int evaluate(const board::Position& pos) noexcept {
    using board::Color;
    using board::Piece;
    using board::PieceType;
    using board::Square;

    Score score;

    for (Square sq = 0; sq < board::kNumSquares; ++sq) {
        const Piece piece = pos.piece_at(sq);
        if (piece == Piece::None) {
            continue;
        }

        const PieceType type = board::piece_type_of(piece);
        const Color color = board::color_of(piece);
        const Score term = material_value(type) + psqt_value(piece, sq);

        if (color == Color::White) {
            score += term;
        } else {
            score -= term;
        }
    }

    return taper(score + pawn_structure_value(pos), compute_phase(pos));
}

} // namespace nightwing::eval
