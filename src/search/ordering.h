#pragma once
// src/search/ordering.h
//
// Move ordering: reorders a MoveList in place so that alpha-beta (and
// the transposition table's cutoffs) prune as many nodes as possible.
// Priority, following ROADMAP.md's Phase 3 "Move ordering" item exactly
// (SEE and counter-moves are ARCHITECTURE.md's eventual fuller scheme,
// but not this item -- separate, still-unchecked pieces of it):
//   1. The transposition-table move for this position, if any (search/tt.h)
//   2. Captures (including en passant and capture-promotions), scored
//      by MVV-LVA -- Most Valuable Victim, Least Valuable Attacker
//      (CPW "MVV-LVA")
//   3. Non-capture promotions, by promoted piece value (not in CPW's
//      MVV-LVA article specifically, but the same "try the forcing,
//      probably-strong moves first" logic applies -- a from-scratch
//      extension of the general move-ordering idea, not a separate
//      named technique)
//   4. Killer moves: up to 2 quiet moves per ply that caused a beta
//      cutoff in a SIBLING node at the same ply (CPW "Killer Heuristic")
//   5. Remaining quiet moves, scored by the history heuristic (CPW
//      "History Heuristic") -- a per-[color][from][to] table of how
//      often a quiet move has caused a beta cutoff, weighted by the
//      depth at which it did
//   6. Everything else (untried quiets with no history), left in
//      move-generation order (std::stable_sort preserves this as the
//      tiebreak for equal-scored moves)
//
// From-scratch implementation of these public techniques; no code
// copied.

#include <array>
#include <cstdint>

#include "board/board.h"
#include "board/move.h"

namespace nightwing::search {

/// Maximum search ply this module's per-ply tables (killers) are sized
/// for. A defensive ceiling, not a real search depth limit -- ply
/// beyond this simply doesn't get killer-move tracking (KillerTable
/// bounds-checks both update() and get() against it) rather than
/// risking an out-of-bounds access. 128 is comfortably beyond any depth
/// this engine can reach in practice today (Phase 3 has no pruning/
/// extensions yet to push effective search depth anywhere near it) --
/// revisit if/when check extensions or other later ROADMAP.md items
/// change that.
inline constexpr int kMaxPly = 128;

/// Killer-move table: up to 2 quiet moves per ply that most recently
/// caused a beta cutoff at that ply (CPW "Killer Heuristic"). Indexed
/// by ply directly -- a flat, pre-allocated array (ARCHITECTURE.md
/// "Memory & Cache": per-ply search state shouldn't be heap-allocated
/// per node). Scoped like TranspositionTable currently is (search/tt.h's
/// header comment): one instance per top-level search call, shared
/// across that call's own iterative-deepening iterations, not yet a
/// persistent global.
class KillerTable {
public:
    /// Records `move` as the newest killer at `ply`, bumping the
    /// previous first killer down to second (a small fixed-size
    /// most-recently-used list of 2). No-op if `move` is already this
    /// ply's first killer (avoids a duplicate) or if `ply` is outside
    /// [0, kMaxPly).
    void update(int ply, board::Move move) noexcept;

    /// Returns killer slot `index` (0 or 1) at `ply`, or a null Move
    /// (board::Move::is_null()) if none has been recorded there yet, or
    /// `ply`/`index` is out of range.
    [[nodiscard]] board::Move get(int ply, int index) const noexcept;

private:
    std::array<std::array<board::Move, 2>, kMaxPly> killers_{};
};

/// History heuristic table: how often a quiet move (by color, from, to
/// -- NOT the specific piece type, matching the classic
/// [side][from][to] formulation) has caused a beta cutoff, weighted by
/// the depth at which it did (CPW "History Heuristic") -- deeper
/// cutoffs are stronger evidence a move is generally good, so they're
/// weighted more heavily. Unlike killers, this isn't ply-indexed: a
/// move that's cut off well at one point in the search is a reasonable
/// bet to try early anywhere, including at the root. Scoped like
/// TranspositionTable/KillerTable (search/tt.h's header comment): one
/// instance per top-level search call for now.
class HistoryTable {
public:
    /// Adds a depth-weighted bonus for `move` (by `color`) having
    /// caused a beta cutoff at `depth`. Clamped at kHistoryMax so no
    /// single move's score can grow unbounded across a long search.
    void update(board::Color color, board::Move move, int depth) noexcept;

    /// Returns the current history score for `move` by `color` (0 if
    /// never recorded).
    [[nodiscard]] int score(board::Color color, board::Move move) const noexcept;

private:
    /// Upper bound on any single table entry -- prevents overflow and
    /// keeps one very-frequently-cutting-off move from permanently
    /// swamping ordering ahead of other, also-good moves found later in
    /// the same search. Chosen to sit comfortably below the killer-move
    /// score band in ordering.cpp's score_move() so history alone can
    /// never accidentally outrank a killer.
    static constexpr int kHistoryMax = 8192;

    std::array<std::array<std::array<int, board::kNumSquares>, board::kNumSquares>,
               board::kNumColors>
        table_{};
};

/// Reorders `moves` in place (highest-priority move first) using the
/// scheme in this file's header comment. `pos` must be the position the
/// moves were generated FROM (used to classify captures and look up
/// victim/attacker piece types for MVV-LVA) -- do not call this after
/// any of `moves` has actually been made. `tt_move` is the move from a
/// TT probe at this node (a null Move if there wasn't one) -- treated
/// as maximum priority when it's genuinely present in `moves` (a stale
/// or foreign entry's move might not be; silently given no special
/// priority if so, rather than trusted blindly -- it'll simply be
/// scored like any other move it happens to match, since Move equality
/// only depends on the packed from/to/flag bits). `killers` and
/// `history` are looked up using this node's `ply` and `pos`'s side to
/// move, respectively.
void order_moves(board::MoveList& moves, const board::Position& pos, board::Move tt_move,
                  const KillerTable& killers, int ply, const HistoryTable& history) noexcept;

} // namespace nightwing::search
