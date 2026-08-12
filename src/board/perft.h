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
// Two variants, both returning identical node counts for the same
// (position, depth) — see tests/perft_tests.cpp's equivalence checks:
//
//   perft()      recurses all the way to depth 0, one call per leaf.
//                This is the reference implementation: every node really
//                is visited via make_move()/unmake_move(), so it's the
//                version to trust first if the two ever disagreed.
//
//   perft_bulk() the standard "bulk counting" speed optimization: at
//                depth 1, returns the move-list size directly instead of
//                recursing one more ply to count leaves one at a time.
//                This skips the final ply's make/unmake entirely, which
//                is where most of a naive perft's time goes at low
//                depths — used as the movegen throughput / NPS baseline
//                (see src/bench.cpp), since it's a closer proxy for how
//                fast movegen alone can enumerate positions than the
//                fully-recursive version is.
//
// Both were deliberately kept separate rather than a single function
// with a runtime or template flag — see docs/DECISIONS.md.

#include <cstdint>

#include "board/board.h"

namespace nightwing::board {

/// Returns the number of leaf positions reachable from `pos` by playing
/// exactly `depth` legal plies, visiting every node via make/unmake all
/// the way to depth 0 (no bulk-counting shortcut — see perft_bulk() for
/// that). perft(pos, 0) == 1 (the position itself, counted as one leaf).
/// Mutates `pos` via make/unmake while searching but always leaves it
/// exactly as it was found on return. Precondition: init_masks(),
/// init_magic_bitboards(), and init_zobrist_keys() have all been called.
[[nodiscard]] std::uint64_t perft(Position& pos, int depth);

/// Returns the same node count as perft(pos, depth), but faster: skips
/// the final ply's make/unmake by returning the legal move count
/// directly at depth 1 (the standard "bulk counting" perft
/// optimization). Same preconditions and mutate-then-restore behavior as
/// perft().
[[nodiscard]] std::uint64_t perft_bulk(Position& pos, int depth);

} // namespace nightwing::board

