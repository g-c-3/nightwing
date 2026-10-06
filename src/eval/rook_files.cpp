// src/eval/rook_files.cpp
//
// See rook_files.h.

#include "eval/rook_files.h"

#include "board/bitboard.h"
#include "board/masks.h"

#include <algorithm>

namespace nightwing::eval {

Score rook_files_value(const board::Position& pos, const RookFilesWeights* weights) noexcept {
    using board::Bitboard;
    using board::Color;
    using board::PieceType;

    const Score open_bonus =
        weights == nullptr ? kRookOpenFilesBonus
                           : Score{round_to_int(weights->open_mg), round_to_int(weights->open_eg)};
    const Score semi_bonus =
        weights == nullptr
            ? kRookSemiOpenFilesBonus
            : Score{round_to_int(weights->semi_open_mg), round_to_int(weights->semi_open_eg)};

    const Bitboard white_pawns = pos.pieces(Color::White, PieceType::Pawn);
    const Bitboard black_pawns = pos.pieces(Color::Black, PieceType::Pawn);

    int open_files = 0;
    int white_semi = 0; // no White pawn, at least one Black pawn
    int black_semi = 0; // no Black pawn, at least one White pawn
    for (int file = 0; file < 8; ++file) {
        const Bitboard file_bb = board::file_mask(file);
        const bool has_white = (white_pawns & file_bb) != 0;
        const bool has_black = (black_pawns & file_bb) != 0;
        if (!has_white && !has_black) {
            ++open_files;
        } else if (!has_white) {
            ++white_semi;
        } else if (!has_black) {
            ++black_semi;
        }
    }

    const int white_rooks =
        std::min(board::popcount(pos.pieces(Color::White, PieceType::Rook)), kRookFilesMaxRooks);
    const int black_rooks =
        std::min(board::popcount(pos.pieces(Color::Black, PieceType::Rook)), kRookFilesMaxRooks);

    Score score;
    score += (open_bonus * open_files + semi_bonus * white_semi) * white_rooks;
    score -= (open_bonus * open_files + semi_bonus * black_semi) * black_rooks;
    return score;
}

} // namespace nightwing::eval
