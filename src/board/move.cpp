// src/board/move.cpp
//
// See move.h. Only non-trivial function is to_uci(), a debug/UCI-I-O
// convenience — not used in the search hot path.

#include "board/move.h"

namespace nightwing::board {

namespace {

/// Appends the file/rank algebraic label for `sq` (e.g. "e4") to `out`.
void append_square(std::string& out, Square sq) {
    out += static_cast<char>('a' + file_of(sq));
    out += static_cast<char>('1' + rank_of(sq));
}

} // namespace

std::string Move::to_uci() const {
    std::string out;
    out.reserve(5);
    append_square(out, from());
    append_square(out, to());
    if (is_promotion()) {
        switch (promotion_piece_type()) {
            case PieceType::Knight: out += 'n'; break;
            case PieceType::Bishop: out += 'b'; break;
            case PieceType::Rook: out += 'r'; break;
            case PieceType::Queen: out += 'q'; break;
            default: break; // unreachable for a promotion move
        }
    }
    return out;
}

} // namespace nightwing::board
