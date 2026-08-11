// src/board/board.cpp
//
// See board.h. Implements the standard starting position factory and the
// debug pretty-printer — everything performance-sensitive (occupied(),
// piece_at(), place_piece()) is header-only/constexpr.

#include "board/board.h"

namespace nightwing::board {

Position start_position() {
    Position pos;
    // Position's default member initializers already give White to move,
    // all castling rights, no en passant, halfmove_clock 0, fullmove 1 —
    // only piece placement needs to be filled in here.

    // Pawns: rank 2 (White) / rank 7 (Black).
    for (int file = 0; file < kNumFiles; ++file) {
        pos.place_piece(make_square(file, 1), Piece::WhitePawn);
        pos.place_piece(make_square(file, 6), Piece::BlackPawn);
    }

    // Back ranks: a/h rooks, b/g knights, c/f bishops, d queen, e king.
    struct BackRankPiece {
        int file;
        Piece white;
        Piece black;
    };
    static constexpr BackRankPiece kBackRank[8] = {
        {0, Piece::WhiteRook, Piece::BlackRook},
        {1, Piece::WhiteKnight, Piece::BlackKnight},
        {2, Piece::WhiteBishop, Piece::BlackBishop},
        {3, Piece::WhiteQueen, Piece::BlackQueen},
        {4, Piece::WhiteKing, Piece::BlackKing},
        {5, Piece::WhiteBishop, Piece::BlackBishop},
        {6, Piece::WhiteKnight, Piece::BlackKnight},
        {7, Piece::WhiteRook, Piece::BlackRook},
    };
    for (const auto& entry : kBackRank) {
        pos.place_piece(make_square(entry.file, 0), entry.white);
        pos.place_piece(make_square(entry.file, 7), entry.black);
    }

    return pos;
}

namespace {

/// Standard single-letter piece notation (uppercase White, lowercase
/// Black), matching FEN/PGN convention. Returns '.' for Piece::None.
char piece_letter(Piece p) {
    switch (p) {
        case Piece::WhitePawn: return 'P';
        case Piece::WhiteKnight: return 'N';
        case Piece::WhiteBishop: return 'B';
        case Piece::WhiteRook: return 'R';
        case Piece::WhiteQueen: return 'Q';
        case Piece::WhiteKing: return 'K';
        case Piece::BlackPawn: return 'p';
        case Piece::BlackKnight: return 'n';
        case Piece::BlackBishop: return 'b';
        case Piece::BlackRook: return 'r';
        case Piece::BlackQueen: return 'q';
        case Piece::BlackKing: return 'k';
        case Piece::None: return '.';
    }
    return '.'; // Unreachable for a valid Piece; keeps -Wreturn-type quiet.
}

} // namespace

std::string to_string(const Position& pos) {
    std::string out;
    out.reserve(kNumSquares + kNumRanks);

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < kNumFiles; ++file) {
            out += piece_letter(pos.piece_at(make_square(file, rank)));
        }
        out += '\n';
    }
    return out;
}

} // namespace nightwing::board
