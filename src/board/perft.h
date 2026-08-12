#pragma once
// src/board/perft.h
//
// Perft ("performance test"): recursively counts leaf nodes reached by
// playing every legal move to a fixed depth from a position. The
// standard movegen/make-unmake correctness check (Chess Programming
// Wiki, https://www.chessprogramming.org/Perft) — reference node counts
// for well-known positions catch nearly every class of movegen bug
// (missed promotions, castling edge cases, pin/check mask errors, etc.)
// that isolated unit tests can miss, because they exercise the full
// combinatorics of legal play rather than hand-picked cases.
//
// This is the plain, non-bulk-counting version: it recurses all the way
// to depth 0 rather than returning the move-list size at depth 1 (the
// standard "bulk counting" speed optimization). Bulk counting is a
// deliberately separate, later roadmap item (see docs/ROADMAP.md,
// "Perft bulk-counting mode ... movegen throughput baseline", and
// docs/DECISIONS.md for why it wasn't folded in here) — the numbers this
// function produces are the reference truth that bulk-counting mode will
// later be checked against.

#include <cstdint>

#include "board/board.h"

namespace nightwing::board {

/// Returns the number of leaf positions reachable from `pos` by playing
/// exactly `depth` legal plies. perft(pos, 0) == 1 (the position itself,
/// counted as one leaf). Mutates `pos` via make/unmake while searching
/// but always leaves it exactly as it was found on return. Precondition:
/// init_masks(), init_magic_bitboards(), and init_zobrist_keys() have all
/// been called.
[[nodiscard]] std::uint64_t perft(Position& pos, int depth);

} // namespace nightwing::board
