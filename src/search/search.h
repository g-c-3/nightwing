#pragma once
// src/search/search.h
//
// Fixed-depth alpha-beta search (negamax form, using PVS -- Principal
// Variation Search -- null-window probes on later moves with full-
// window re-search on a fail-high, backed by a transposition table for
// cutoffs on repeated/transposed positions, reordering each node's move
// list (search/ordering.h: TT move, MVV-LVA captures, promotions,
// killer moves, history heuristic) before searching it, resolving the
// horizon with quiescence search (search/quiescence.h: captures, checks
// at the first quiescence ply, SEE-pruned via search/see.h) instead of
// a raw static eval at the depth cutoff, and reducing the search depth
// at internal nodes with no transposition-table entry at all (Internal
// Iterative Reduction, CPW's modern replacement for Internal Iterative
// Deepening); see search.cpp's negamax() comments for all of the
// above), plus iterative deepening on top of it, which for depth >= 2
// now searches each iteration through a narrow aspiration window
// centered on the previous iteration's score (CPW "Aspiration
// Windows"), widening and re-searching on a fail-high/fail-low -- see
// search.cpp's search_iterative_deepening() comments. Remaining
// pruning/extensions are later ROADMAP.md items layered on top of this;
// this file is deliberately just "does the tree search return a legal
// best move, correctly, and can it be asked to deepen incrementally
// under a time budget," per ARCHITECTURE.md's phase-by-phase build
// order. PVS, the transposition table, and aspiration windows are all
// exact techniques (same best move/score as plain full-window alpha-
// beta, regardless of how they're called) -- IIR is different: it's a
// genuine heuristic approximation whose safety depends on iterative
// deepening's own self-correction across iterations, not per-call
// exactness (see negamax()'s header comment for why this matters for
// interpreting a single search_fixed_depth() call at real depth). See
// tt.h's header comment on the TT's (and KillerTable/HistoryTable's,
// search/ordering.h) current per-call, not yet persistent-global,
// lifetime.

#include <cstdint>
#include <span>

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

/// Result of a search — either a single fixed-depth call or a full
/// iterative-deepening run.
struct SearchResult {
    /// The best move found. Null (Move::is_null() == true) if `pos` had
    /// no legal moves at all (checkmate or stalemate at the root).
    board::Move best_move;

    /// Score from `pos.side_to_move`'s perspective at the root: positive
    /// means the side to move is better, kDrawScore (0) is balanced, and
    /// a magnitude at or above kMateThreshold means forced mate (see
    /// kMateScore above).
    int score = 0;

    /// Nodes visited (leaf + internal calls into negamax()). For
    /// search_fixed_depth(), this is that one call's node count. For
    /// search_iterative_deepening(), this is the *total* across every
    /// completed iteration (depth 1, 2, 3, ... up to whatever finished),
    /// since all of that work genuinely happened and took real wall-clock
    /// time — the right denominator for later `bench`/NPS reporting
    /// (ROADMAP.md Phase 8), not just the deepest iteration's count.
    std::uint64_t nodes = 0;

    /// The search depth actually completed. For search_fixed_depth(),
    /// this always equals the requested `depth` — except when `pos` had
    /// no legal moves at all, where it stays 0 (nothing was meaningfully
    /// "searched" beyond confirming the position is already over; see
    /// best_move above). For search_iterative_deepening(), this is the
    /// deepest iteration that finished before the loop stopped (by
    /// reaching `max_depth` or running out of its time budget).
    int depth_completed = 0;
};

/// Runs a plain fixed-depth alpha-beta search from `pos` and returns the
/// best move plus its score. `pos` is left unmodified on return (every
/// make_move() during the search is paired with a matching unmake_move()).
///
/// `game_history` is the Zobrist hash (board::Position::zobrist_hash) of
/// every ancestor position strictly BEFORE `pos` in the actual game,
/// oldest to newest — NOT including `pos` itself. Used for repetition
/// detection (search.cpp's negamax() header comment): a position found
/// during the search that also occurred earlier in the real game is
/// recognized and scored as a draw, not just repetitions the search
/// discovers entirely within its own calculated lines. Defaults to empty
/// (no known game history — e.g. a one-off analysis of an arbitrary
/// position with no real game behind it), in which case repetition
/// detection still works fully within the search tree itself, just
/// without awareness of anything that happened before `pos`.
///
/// Precondition: `depth >= 1`, and init_masks()/init_magic_bitboards()
/// have been called (movegen's precondition, transitively — see
/// board/movegen.h).
[[nodiscard]] SearchResult search_fixed_depth(board::Position& pos, int depth,
                                               std::span<const std::uint64_t> game_history = {});

/// Runs iterative deepening: calls search_fixed_depth() at depth = 1,
/// 2, 3, ... up to `max_depth`, keeping the most recently *completed*
/// iteration's result. If `time_limit_ms` is positive, the loop also
/// stops (before starting the next iteration, not mid-iteration — see
/// search.cpp's header comment on why true mid-search interruption is
/// deferred) once that many milliseconds of wall-clock time have
/// elapsed since the call began; pass 0 (the default) for no time limit,
/// i.e. always search all the way to `max_depth`.
///
/// Depth 1 always runs unconditionally before any time check, so the
/// result always has a legal best_move (when `pos` has one at all) even
/// under an extremely tight time budget. If `pos` has no legal moves at
/// all, returns immediately after the depth-1 call (see
/// SearchResult::depth_completed) rather than wastefully repeating the
/// same terminal result at deeper depths.
///
/// Precondition: same as search_fixed_depth() — `max_depth >= 1`, and
/// init_masks()/init_magic_bitboards() must already have been called.
///
/// `game_history`: same meaning and default as search_fixed_depth()'s
/// parameter of the same name — see that function's doc comment. Shared
/// across every depth iteration of this one call, same as `pos` itself.
[[nodiscard]] SearchResult search_iterative_deepening(
    board::Position& pos, int max_depth, int time_limit_ms = 0,
    std::span<const std::uint64_t> game_history = {});

} // namespace nightwing::search
