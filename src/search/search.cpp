// src/search/search.cpp

#include "search/search.h"

#include <cassert>
#include <chrono>

#include "board/movegen.h"
#include "eval/eval.h"
#include "search/tt.h"

namespace nightwing::search {
namespace {

using board::Color;
using board::Move;
using board::MoveList;
using board::Position;
using board::UndoInfo;

/// Alpha/beta search bound, kept well below kMateScore's already-huge
/// magnitude so that negating it (`-alpha`, `-beta` on recursive calls,
/// per the negamax convention) never risks integer overflow — a fixed,
/// generous constant rather than std::numeric_limits<int>::max(), whose
/// negation is undefined behavior.
constexpr int kInfinity = 1'000'000;

/// Placeholder default table size until the UCI `Hash` option
/// (ROADMAP.md Phase 8) makes this configurable. Modest on purpose: each
/// top-level search call currently constructs its own fresh table (see
/// tt.h's header comment on lifetime), so this gets allocated/freed
/// often rather than once for the engine's lifetime.
constexpr std::size_t kDefaultTTSizeMB = 16;

// search_iterative_deepening() (below) only checks its time budget
// *between* full-depth search_fixed_depth() calls, not mid-search --
// negamax() itself has no clock/stop-flag awareness. True mid-search
// interruption (checking a stop condition every N nodes inside
// negamax() and unwinding cleanly without corrupting alpha/best-move
// bookkeeping) is a real, standard technique, but it's more machinery
// than Phase 2's "get something playing" scope needs: without it, the
// worst case is simply that one already-started iteration finishes
// before the time budget is enforced, which is a minor, boundable
// overrun (bounded by how long a single depth takes) rather than a
// correctness problem. Revisit once real time controls (UCI `go
// wtime`/`movetime`, the very next ROADMAP.md item) make that overrun
// large enough to matter in practice.

/// Returns true if `pos.side_to_move`'s king is currently attacked —
/// used to distinguish checkmate from stalemate when
/// generate_legal_moves() returns no moves, since movegen itself
/// doesn't report that distinction directly (see board/movegen.h).
[[nodiscard]] bool in_check(const Position& pos) noexcept {
    const Color us = pos.side_to_move;
    const board::Square king_sq =
        board::bitscan_forward(pos.pieces(us, board::PieceType::King));
    return board::is_square_attacked(pos, king_sq, board::opposite(us));
}

/// Negamax alpha-beta search. Returns a score from `pos.side_to_move`'s
/// perspective (positive = good for the side to move), consistent with
/// eval::evaluate()'s White-perspective output being flipped for Black
/// at the depth-0 base case below.
///
/// `ply` is the number of plies searched so far from the root (0 at the
/// root's immediate children), used only to adjust mate scores so that
/// shorter mates are preferred: a mate delivered `ply` plies from the
/// root scores kMateScore - ply from the mated side's perspective (see
/// the `moves.empty()` branch), so the score shrinks — and therefore
/// looks less attractive to a side searching for the *fastest* mate, or
/// less bad to a side merely trying to survive as long as possible —
/// the deeper the forced mate lies. Standard CPW "Mate Scores"
/// convention; from-scratch implementation here. `tt` (search/tt.h) is
/// also keyed and mate-distance-adjusted around this same `ply`
/// convention — see tt.cpp's adjustment functions.
///
/// Transposition table integration deliberately stays simple for this
/// first pass (see docs/DECISIONS.md, 2026-08-15 TT entry): a probe
/// that doesn't immediately resolve the window (an Exact hit, or a
/// Lower/Upper bound that already fails high/low against the CALLER's
/// actual alpha/beta) is discarded rather than used to narrow alpha/
/// beta for the search that follows. Narrowing from a non-cutoff
/// bound is a real, standard optimization, but it complicates how the
/// eventual store() at the bottom of this function should classify its
/// own Exact/Lower/Upper result (that classification needs to be
/// relative to whatever window was actually searched) — skipping it
/// keeps that classification unambiguous. TT interaction only happens
/// for depth >= 1 nodes: a depth <= 0 leaf's score is a pure
/// eval::evaluate() call that never consults alpha/beta at all (see
/// the branch immediately below), so there is nothing for the TT to
/// usefully cache there yet (no quiescence search exists as of this
/// commit — ROADMAP.md's separate "Quiescence search" item).
int negamax(Position& pos, int depth, int alpha, int beta, int ply, std::uint64_t& nodes,
            TranspositionTable& tt) {
    ++nodes;

    if (depth <= 0) {
        const int white_relative_score = eval::evaluate(pos);
        return pos.side_to_move == Color::White ? white_relative_score : -white_relative_score;
    }

    const int alpha_orig = alpha;
    const std::uint64_t key = pos.zobrist_hash;
    tt.prefetch(key); // ARCHITECTURE.md: issued as early as possible, to overlap with movegen below.

    const TTProbeResult probe = tt.probe(key, ply);
    if (probe.hit && probe.depth >= depth) {
        if (probe.bound == Bound::Exact) {
            return probe.score;
        }
        if (probe.bound == Bound::Lower && probe.score >= beta) {
            return probe.score;
        }
        if (probe.bound == Bound::Upper && probe.score <= alpha) {
            return probe.score;
        }
        // Bound doesn't resolve this window: fall through to a normal
        // search using the untouched alpha/beta (see this function's
        // header comment on why this deliberately doesn't narrow them).
    }

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        // Terminal position: not stored in the TT (see this function's
        // header comment) — movegen already paid the cost of detecting
        // this, and there's no move-loop result left to cache.
        return in_check(pos) ? -(kMateScore - ply) : kDrawScore;
    }

    int best = -kInfinity;
    Move best_move; // Move() default (null) unless overwritten below — every real position has >=1 move here.
    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        UndoInfo undo;
        board::make_move(pos, move, undo);

        int score;
        if (i == 0) {
            // First move (assumed most-promising, per PVS's usual pairing
            // with move ordering -- ordering itself is a separate,
            // still-unchecked ROADMAP.md item, so "first" here is simply
            // move-generation order for now, same as the pre-PVS code):
            // search it with the full alpha-beta window to establish a
            // real score to compare everything else against.
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt);
        } else if (depth == 1) {
            // This move's child is a leaf (depth - 1 == 0):
            // eval::evaluate() at a leaf doesn't consult alpha/beta at
            // all (see the depth <= 0 branch above), so a null-window
            // probe here can never prune anything -- it would just risk
            // a pointless re-search that doubles that leaf's node count
            // for an identical score. Go straight to a normal search
            // instead of paying for the PVS trick where it can't help.
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt);
        } else {
            // PVS: probe every later move with a null (zero-width)
            // window first -- cheap, since it only needs to prove
            // "fails low against alpha" or "fails high," not compute an
            // exact score. Only if the probe suggests this move might
            // actually beat alpha (a fail-high on the null window that's
            // still below beta) is it worth paying for a full-window
            // re-search to get its real score.
            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, nodes, tt);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt);
            }
        }

        board::unmake_move(pos, move, undo);

        if (score > best) {
            best = score;
            best_move = move;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break; // Beta cutoff: the opponent won't let us reach this line.
        }
    }

    // Classify against alpha_orig/beta (the window this call was
    // actually asked to resolve), not any narrowed value — see this
    // function's header comment. CPW "Node Types": every move tried and
    // none reached alpha_orig -> only an upper bound was proven; a
    // cutoff -> only a lower bound was proven; otherwise the search
    // ran to completion within the window and best is exact.
    const Bound bound_type = best <= alpha_orig ? Bound::Upper
                            : best >= beta       ? Bound::Lower
                                                  : Bound::Exact;
    tt.store(key, depth, best, bound_type, best_move, ply);

    return best;
}

/// Core root-move-loop logic shared by both public entry points below.
/// Kept as a private, TT-taking function rather than exposed directly:
/// each public entry point is responsible for deciding *which*
/// TranspositionTable instance to pass in (see tt.h's header comment on
/// the current per-call lifetime), not this function.
SearchResult search_root(Position& pos, int depth, TranspositionTable& tt) {
    SearchResult result;

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        // Nothing to play: report the terminal score with a null
        // best_move (Move::is_null()) rather than an arbitrary one.
        result.score = in_check(pos) ? -kMateScore : kDrawScore;
        result.nodes = 1;
        return result;
    }

    int alpha = -kInfinity;
    const int beta = kInfinity;
    Move best_move = moves[0];

    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        UndoInfo undo;
        board::make_move(pos, move, undo);

        // Root-level PVS, mirroring negamax()'s move loop exactly (see
        // its comments for the full rationale): first move gets the full
        // window, later moves at depth 1 skip straight to a normal
        // search (their children are leaves -- a null-window probe can't
        // prune there), and later moves at depth >= 2 get a null-window
        // probe first, re-searched with the full window only on a
        // fail-high that's still below beta. No TT probe/store for the
        // root position itself in this pass -- see docs/DECISIONS.md,
        // 2026-08-15 TT entry: without move ordering (a separate,
        // still-unchecked ROADMAP.md item) yet using a TT-move hint,
        // root-level TT interaction has no payoff to justify its own
        // complexity today.
        int score;
        if (i == 0 || depth == 1) {
            score = -negamax(pos, depth - 1, -beta, -alpha, 1, result.nodes, tt);
        } else {
            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, 1, result.nodes, tt);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, 1, result.nodes, tt);
            }
        }

        board::unmake_move(pos, move, undo);

        if (score > alpha) {
            alpha = score;
            best_move = move;
        }
    }

    result.best_move = best_move;
    result.score = alpha;
    result.depth_completed = depth;
    return result;
}

} // namespace

SearchResult search_fixed_depth(Position& pos, int depth) {
    assert(depth >= 1 && "search_fixed_depth: depth must be at least 1");

    // A fresh, private table for this one call (see tt.h's header
    // comment) -- kDefaultTTSizeMB is a placeholder until the UCI `Hash`
    // option (ROADMAP.md Phase 8) makes this configurable and the table
    // itself persistent across calls.
    TranspositionTable tt(kDefaultTTSizeMB);
    return search_root(pos, depth, tt);
}

SearchResult search_iterative_deepening(Position& pos, int max_depth, int time_limit_ms) {
    assert(max_depth >= 1 && "search_iterative_deepening: max_depth must be at least 1");

    const auto start_time = std::chrono::steady_clock::now();

    // One table shared across every iteration of THIS call (see tt.h's
    // header comment) -- this is where the TT's cross-iteration value
    // actually comes from right now, since search_fixed_depth() on its
    // own always starts from an empty table.
    TranspositionTable tt(kDefaultTTSizeMB);

    // Depth 1 always runs unconditionally, before any time check, so
    // there's always a legal best_move to fall back on (see search.h's
    // header comment).
    tt.new_search();
    SearchResult result = search_root(pos, 1, tt);
    std::uint64_t total_nodes = result.nodes;

    // Position already over (checkmate/stalemate at the root): every
    // deeper iteration would just regenerate the same empty move list
    // and return the same terminal result, so stop immediately instead
    // of wastefully repeating it.
    if (result.best_move.is_null()) {
        return result;
    }

    for (int depth = 2; depth <= max_depth; ++depth) {
        if (time_limit_ms > 0) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start_time)
                                         .count();
            if (elapsed_ms >= time_limit_ms) {
                break;
            }
        }

        tt.new_search();
        SearchResult next = search_root(pos, depth, tt);
        total_nodes += next.nodes;
        result = next;
    }

    result.nodes = total_nodes;
    return result;
}

} // namespace nightwing::search
