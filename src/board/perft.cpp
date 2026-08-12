// src/board/perft.cpp
//
// See perft.h.

#include "board/perft.h"

#include "board/move.h"
#include "board/movegen.h"

namespace nightwing::board {

std::uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;

    MoveList moves;
    generate_legal_moves(pos, moves);

    std::uint64_t nodes = 0;
    for (const Move& m : moves) {
        UndoInfo undo;
        make_move(pos, m, undo);
        nodes += perft(pos, depth - 1);
        unmake_move(pos, m, undo);
    }
    return nodes;
}

std::uint64_t perft_bulk(Position& pos, int depth) {
    if (depth == 0) return 1;

    MoveList moves;
    generate_legal_moves(pos, moves);

    if (depth == 1) return static_cast<std::uint64_t>(moves.size());

    std::uint64_t nodes = 0;
    for (const Move& m : moves) {
        UndoInfo undo;
        make_move(pos, m, undo);
        nodes += perft_bulk(pos, depth - 1);
        unmake_move(pos, m, undo);
    }
    return nodes;
}

} // namespace nightwing::board
