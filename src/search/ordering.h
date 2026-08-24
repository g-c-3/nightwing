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
//      "History Heuristic") PLUS continuation history (CPW
//      "Continuation History", the 1-ply "counter-move history" case of
//      Stockfish's own generalized scheme) -- the plain history table's
//      per-[color][from][to] score, added to a separate score for how
//      often this move (by piece type and destination) has caused a
//      cutoff specifically as a reply to the immediately preceding move
//      (also by piece type and destination) -- both weighted by the
//      depth at which they did. Search.cpp's negamax() move loop is
//      responsible for threading the "immediately preceding move"
//      context down through each level of recursion; ordering.cpp
//      itself has no notion of search history beyond what's passed in.
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

/// Continuation history: how often a move (by piece type and
/// destination square) has caused a beta cutoff GIVEN a specific
/// immediately-preceding move (also by piece type and destination
/// square) played by the opponent (CPW "Continuation History" / the
/// 1-ply "counter-move history" case of Stockfish's own generalized
/// scheme), weighted by the depth at which it did -- same depth-squared
/// weighting as HistoryTable::update() above, for the same reason.
/// Distinct from HistoryTable (which only looks at [color][from][to]
/// with no notion of what came immediately before it): the same move
/// can be a strong reply to one preceding move and a poor one to
/// another, and this table is the piece of state that lets ordering
/// distinguish those two cases rather than averaging them together into
/// one score.
///
/// Indexed by PIECE TYPE and destination square only -- not from-square,
/// not color -- for both the preceding move and the current one. Not
/// color-indexed: a continuation's usefulness (this shape of reply to
/// that shape of preceding move) is treated as symmetric between White
/// and Black for this first draft, a simplifying assumption common to
/// this specific table even in engines that otherwise track plain
/// history per color (unlike HistoryTable above, where the side to
/// move genuinely does change which table cell a from/to pair belongs
/// to).
///
/// `board::PieceType::None` (board/board.h's own sentinel, not a
/// genuine piece type) represents "there was no preceding move to
/// condition on" -- the true root of a search, or the position
/// immediately after a null move (search.cpp's NMP block) -- and is
/// never actually stored into or read from this table's own array
/// (sized for `board::kNumPieceTypes`, not `kNumPieceTypes + 1`):
/// update() and score() both treat a `board::PieceType::None`
/// `prev_piece` as a no-op/zero rather than indexing with it, since
/// there is nothing meaningful to record or look up in that case.
/// Scoped like HistoryTable (this file's own header comment): one
/// instance per top-level search call.
class ContinuationHistoryTable {
public:
    /// Adds a depth-weighted bonus for `move` (`piece` moved to `to`)
    /// having caused a beta cutoff when it directly followed
    /// `prev_piece` moving to `prev_to`. No-op if `prev_piece` is
    /// `board::PieceType::None` (no real preceding move to condition
    /// on -- see this class's header comment).
    void update(board::PieceType prev_piece, board::Square prev_to, board::PieceType piece,
                board::Square to, int depth) noexcept;

    /// Returns the current continuation-history score, or 0 if
    /// `prev_piece` is `board::PieceType::None` or the combination has
    /// never been recorded.
    [[nodiscard]] int score(board::PieceType prev_piece, board::Square prev_to,
                             board::PieceType piece, board::Square to) const noexcept;

private:
    /// Same cap, and the same rationale, as HistoryTable::kHistoryMax
    /// above -- comfortably below the killer-move score band in
    /// ordering.cpp's score_move() even after being added to a plain
    /// history score there.
    static constexpr int kContinuationHistoryMax = 8192;

    std::array<std::array<std::array<std::array<int, board::kNumSquares>, board::kNumPieceTypes>,
                          board::kNumSquares>,
               board::kNumPieceTypes>
        table_{};
};

/// MVV-LVA (Most Valuable Victim, Least Valuable Attacker): favors
/// capturing the most valuable piece with the least valuable attacker.
/// `move` must be a genuine capture (is_capture() == true) of `pos`,
/// which must not have had `move` applied yet (victim/attacker are read
/// directly off the board). En passant is special-cased since the
/// captured pawn isn't on `move.to()`, unlike every other capture type.
/// Public (not just an order_moves() implementation detail) so other
/// capture-ordering needs -- e.g. quiescence search's own, simpler
/// capture ordering, search/quiescence.h -- can reuse the exact same
/// scoring rather than a second, potentially-drifting copy.
[[nodiscard]] int mvv_lva_score(const board::Position& pos, board::Move move) noexcept;

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
/// move, respectively. `cont_history`/`prev_piece`/`prev_to` describe
/// the move immediately preceding this node (the move the caller made
/// to reach `pos`) -- pass `board::PieceType::None` for `prev_piece`
/// when there isn't one (the true search root, or immediately after a
/// null move; see ContinuationHistoryTable's own header comment), which
/// makes continuation history contribute nothing to this call's
/// scoring, same as if the table were empty.
void order_moves(board::MoveList& moves, const board::Position& pos, board::Move tt_move,
                  const KillerTable& killers, int ply, const HistoryTable& history,
                  const ContinuationHistoryTable& cont_history, board::PieceType prev_piece,
                  board::Square prev_to) noexcept;

} // namespace nightwing::search
