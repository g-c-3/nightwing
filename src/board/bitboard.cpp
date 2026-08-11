// src/board/bitboard.cpp
//
// See bitboard.h. Everything performance-sensitive is header-only
// (constexpr, intrinsic-backed); this file only holds the debug/test
// pretty-printer, which is never called from a hot path.

#include "board/bitboard.h"

namespace nightwing::board {

std::string to_string(Bitboard bb) {
    std::string out;
    out.reserve(kNumSquares + 8); // 64 squares + 8 newlines

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            const Square sq = rank * 8 + file;
            out += test_bit(bb, sq) ? '1' : '.';
        }
        out += '\n';
    }
    return out;
}

} // namespace nightwing::board
