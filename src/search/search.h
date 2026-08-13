#pragma once
// src/search/search.h
//
// Plain fixed-depth alpha-beta (negamax form) search — Phase 2's second
// ROADMAP.md item. Iterative deepening, aspiration windows, and all
// pruning/extensions are later ROADMAP.md items layered on top of this;
// this file is deliberately just "does the tree search return a legal
// best move, correctly," per ARCHITECTURE.md's phase-by-phase build order.

#include <cstdint>

#include "board/board.h"
#include "board/move.h"

namespace nightwing::search {

/// Mate/draw score constants, expressed in the same centipawn-like
/// units as eval::evaluate() so search and eval scores compose
/// directly. Mate scores encode "distance to mate in plies" by being
/// adjusted as the search unwinds (see search.cpp's negamax() header
/// comment) — the standard CPW "Mate Scores" convention (shorter mates
/// score higher, so a mate in 1 is preferred over a mate in 3); this is
/// a from-scratch implementation of that public convention.
inline constexpr int kMateScore = 32000;
inline constexpr int kDrawScore = 0;

/// Any |score| at or beyond this magnitude is a mate score, not a real
/// evaluate() output — for later callers (e.g. a UCI `info score mate N`
/// line) that need to tell the two apart. Comfortably above any
/// plausible Phase 2 eval value (material+PSQT tops out in the low
/// thousands even with several extra queens on the board).
inline constexpr int kMateThreshold = kMateScore - 1000;

/// Result of a fixed-depth root search.
struct SearchResult {
    /// The best move found. Null (Move::is_null() == true) if `pos` had
    /// no legal moves at all (checkmate or stalemate at the root).
    board::Move best_move;

    /// Score from `pos.side_to_move`'s perspective at the root: positive
    /// means the side to move is better, kDrawScore (0) is balanced, and
    /// a magnitude at or above kMateThreshold means forced mate (see
    /// kMateScore above).
    int score = 0;

    /// Total nodes visited (leaf + internal calls into negamax()), for
    /// later `bench`/NPS reporting (ROADMAP.md Phase 8) — tracked from
    /// day one since it's free to count and immediately useful for
    /// sanity-checking search behavior (e.g. depth-1 node count should
    /// exactly match the root's legal move count).
    std::uint64_t nodes = 0;
};

/// Runs a plain fixed-depth alpha-beta search from `pos` and returns the
/// best move plus its score. `pos` is left unmodified on return (every
/// make_move() during the search is paired with a matching unmake_move()).
///
/// Precondition: `depth >= 1`, and init_masks()/init_magic_bitboards()
/// have been called (movegen's precondition, transitively — see
/// board/movegen.h).
[[nodiscard]] SearchResult search_fixed_depth(board::Position& pos, int depth);

} // namespace nightwing::search
