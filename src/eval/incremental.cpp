// src/eval/incremental.cpp

#include "eval/incremental.h"

namespace nightwing::eval {

Score compute_material_psqt(const board::Position& pos, const MaterialWeights* material_weights,
                             const PsqtWeights* psqt_weights) noexcept {
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
        const Score term =
            material_value(type, material_weights) + psqt_value(piece, sq, psqt_weights);
        if (color == Color::White) {
            score += term;
        } else {
            score -= term;
        }
    }
    return score;
}

Score material_psqt_delta(board::Color mover, board::PieceType moved_piece_type,
                           board::Move move, const board::UndoInfo& undo) noexcept {
    using board::Color;
    using board::Piece;
    using board::PieceType;
    using board::Square;

    // The mover's own contribution: remove its pre-move (piece, from-
    // square) term, add its post-move (piece, to-square) term -- for a
    // promotion, the post-move piece type is the promoted-to type, not
    // `moved_piece_type` (still Pawn), exactly as move.is_promotion()'s
    // own header comment describes.
    const PieceType to_type = move.is_promotion() ? move.promotion_piece_type() : moved_piece_type;
    const Piece from_piece = board::make_piece(mover, moved_piece_type);
    const Piece to_piece = board::make_piece(mover, to_type);

    const Score own_delta = (material_value(to_type) + psqt_value(to_piece, move.to())) -
                             (material_value(moved_piece_type) + psqt_value(from_piece, move.from()));

    Score delta = (mover == Color::White) ? own_delta : -own_delta;

    // A captured piece (regular capture or en passant) loses its own
    // material+PSQT contribution entirely -- removed from the square it
    // actually stood on, which is move.to() for a regular capture but
    // the en-passant-specific square (same file as `to`, same rank as
    // `from`) for en passant, since the captured pawn was never
    // standing on `to` at all.
    if (undo.captured_piece != Piece::None) {
        const Color captured_color = board::opposite(mover);
        const Square captured_sq =
            move.is_en_passant()
                ? board::make_square(board::file_of(move.to()), board::rank_of(move.from()))
                : move.to();
        const Score removed_captured = material_value(board::piece_type_of(undo.captured_piece)) +
                                        psqt_value(undo.captured_piece, captured_sq);
        delta += (captured_color == Color::White) ? -removed_captured : removed_captured;
    }

    // Castling's secondary rook relocation: PSQT-only (material doesn't
    // change -- the rook is still a rook). Corner/landing squares are
    // the standard fixed castling geometry, hardcoded rather than
    // derived, since they never vary: White kingside h1->f1, White
    // queenside a1->d1, Black kingside h8->f8, Black queenside a8->d8.
    if (move.is_castle()) {
        Square rook_from;
        Square rook_to;
        if (mover == Color::White) {
            if (move.flag() == board::MoveFlag::KingCastle) {
                rook_from = 7;
                rook_to = 5;
            } else {
                rook_from = 0;
                rook_to = 3;
            }
        } else {
            if (move.flag() == board::MoveFlag::KingCastle) {
                rook_from = 63;
                rook_to = 61;
            } else {
                rook_from = 56;
                rook_to = 59;
            }
        }
        const Piece rook = board::make_piece(mover, PieceType::Rook);
        const Score rook_delta = psqt_value(rook, rook_to) - psqt_value(rook, rook_from);
        delta += (mover == Color::White) ? rook_delta : -rook_delta;
    }

    return delta;
}

} // namespace nightwing::eval
