// src/search/search.cpp

#include "search/search.h"

#include <cassert>
#include <chrono>

#include "board/movegen.h"
#include "eval/eval.h"
#include "search/ordering.h"
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
///
/// Move ordering (search/ordering.h) reorders the generated move list
/// before this function's own loop below, using the TT probe's move
/// (even one too shallow to trigger a cutoff above -- still a useful
/// hint), MVV-LVA for captures, promotion value, killer moves, and the
/// history heuristic. This is also what makes the "first move" comment
/// in the loop below actually meaningful now, rather than just "first
/// in move-generation order."
int negamax(Position& pos, int depth, int alpha, int beta, int ply, std::uint64_t& nodes,
            TranspositionTable& tt, KillerTable& killers, HistoryTable& history) {
    ++nodes;

    if (depth <= 0) {
        const int white_relative_score = eval::evaluate(pos);
        return pos.side_to_move == Color::White ? white_relative_score : -white_relative_score;
    }

    const int alpha_orig = alpha;
    const Color us = pos.side_to_move;
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
    // Even a probe too shallow for a cutoff above is still worth using
    // as a move-ordering hint below -- a Move() (null) if there was no
    // hit at all, in which case order_moves() simply gives it no special
    // priority (see ordering.h).
    const Move tt_move = probe.hit ? probe.move : Move();

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        // Terminal position: not stored in the TT (see this function's
        // header comment) — movegen already paid the cost of detecting
        // this, and there's no move-loop result left to cache.
        return in_check(pos) ? -(kMateScore - ply) : kDrawScore;
    }

    order_moves(moves, pos, tt_move, killers, ply, history);

    int best = -kInfinity;
    Move best_move; // Move() default (null) unless overwritten below — every real position has >=1 move here.
    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        UndoInfo undo;
        board::make_move(pos, move, undo);

        int score;
        if (i == 0) {
            // First move -- now genuinely the highest-priority candidate
            // per order_moves() above (TT move, else best-scoring
            // capture/promotion/killer/history move), not just "first in
            // move-generation order" as it was pre-ordering: search it
            // with the full alpha-beta window to establish a real score
            // to compare everything else against.
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt, killers, history);
        } else if (depth == 1) {
            // This move's child is a leaf (depth - 1 == 0):
            // eval::evaluate() at a leaf doesn't consult alpha/beta at
            // all (see the depth <= 0 branch above), so a null-window
            // probe here can never prune anything -- it would just risk
            // a pointless re-search that doubles that leaf's node count
            // for an identical score. Go straight to a normal search
            // instead of paying for the PVS trick where it can't help.
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt, killers, history);
        } else {
            // PVS: probe every later move with a null (zero-width)
            // window first -- cheap, since it only needs to prove
            // "fails low against alpha" or "fails high," not compute an
            // exact score. Only if the probe suggests this move might
            // actually beat alpha (a fail-high on the null window that's
            // still below beta) is it worth paying for a full-window
            // re-search to get its real score.
            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, nodes, tt, killers, history);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt, killers, history);
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
            // Beta cutoff. Record it for future ordering (killers +
            // history) only for quiet, non-promotion moves -- captures
            // and promotions already order well via MVV-LVA/promotion
            // value (see ordering.h's header comment), so mixing them
            // into the killer/history scheme adds noise without adding
            // information (CPW's "Killer Heuristic"/"History Heuristic"
            // are conventionally quiet-move-only for the same reason).
            if (!move.is_capture() && !move.is_promotion()) {
                killers.update(ply, move);
                history.update(us, move, depth);
            }
            break; // The opponent won't let us reach this line.
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
/// TranspositionTable/KillerTable/HistoryTable instances to pass in
/// (see tt.h's header comment on the current per-call lifetime), not
/// this function.
SearchResult search_root(Position& pos, int depth, TranspositionTable& tt, KillerTable& killers,
                          HistoryTable& history) {
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

    // Root-level TT interaction, added this session alongside move
    // ordering (deferred last session specifically until ordering
    // existed to consume it -- see docs/DECISIONS.md, 2026-08-15 TT
    // entry, sub-decision 4). Probed only for an ordering hint below,
    // never for an early return/cutoff: the root always needs its FULL
    // legal move list reviewed to report a genuine best move (there's
    // no "skip searching, trust the cache" shortcut that would still
    // give a UCI-quality result), unlike internal negamax() nodes which
    // are free to trust a sufficiently-deep proven bound.
    const std::uint64_t root_key = pos.zobrist_hash;
    const TTProbeResult root_probe = tt.probe(root_key, 0);
    const Move tt_move = root_probe.hit ? root_probe.move : Move();

    order_moves(moves, pos, tt_move, killers, /*ply=*/0, history);

    int alpha = -kInfinity;
    const int beta = kInfinity;
    Move best_move = moves[0];

    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        UndoInfo undo;
        board::make_move(pos, move, undo);

        // Root-level PVS, mirroring negamax()'s move loop exactly (see
        // its comments for the full rationale): first move -- now the
        // ordering-selected best candidate, not just move-generation
        // order -- gets the full window, later moves at depth 1 skip
        // straight to a normal search (their children are leaves -- a
        // null-window probe can't prune there), and later moves at
        // depth >= 2 get a null-window probe first, re-searched with the
        // full window only on a fail-high that's still below beta.
        int score;
        if (i == 0 || depth == 1) {
            score = -negamax(pos, depth - 1, -beta, -alpha, 1, result.nodes, tt, killers, history);
        } else {
            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, 1, result.nodes, tt, killers, history);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, 1, result.nodes, tt, killers, history);
            }
        }

        board::unmake_move(pos, move, undo);

        if (score > alpha) {
            alpha = score;
            best_move = move;
        }
    }

    // The root always reviews every legal move under the full, open
    // (-inf, +inf) window with no early cutoff (see above) -- so
    // whatever alpha ends up as is by construction an exact score, not
    // merely a bound. Storing it gives the NEXT iterative-deepening
    // iteration's root call a TT-move ordering hint (this function's
    // own probe above), and can also help any INTERNAL node elsewhere
    // in this same top-level call that happens to transpose into this
    // exact position.
    tt.store(root_key, depth, alpha, Bound::Exact, best_move, 0);

    result.best_move = best_move;
    result.score = alpha;
    result.depth_completed = depth;
    return result;
}

} // namespace

SearchResult search_fixed_depth(Position& pos, int depth) {
    assert(depth >= 1 && "search_fixed_depth: depth must be at least 1");

    // Fresh, private tables for this one call (see tt.h's header
    // comment, which applies equally to KillerTable/HistoryTable --
    // search/ordering.h). kDefaultTTSizeMB is a placeholder until the
    // UCI `Hash` option (ROADMAP.md Phase 8) makes table size
    // configurable and the tables themselves persistent across calls.
    TranspositionTable tt(kDefaultTTSizeMB);
    KillerTable killers;
    HistoryTable history;
    return search_root(pos, depth, tt, killers, history);
}

SearchResult search_iterative_deepening(Position& pos, int max_depth, int time_limit_ms) {
    assert(max_depth >= 1 && "search_iterative_deepening: max_depth must be at least 1");

    const auto start_time = std::chrono::steady_clock::now();

    // Tables shared across every iteration of THIS call (see tt.h's
    // header comment) -- this cross-iteration sharing is where the
    // ordering/TT machinery's real value comes from right now, since
    // search_fixed_depth() on its own always starts from empty tables.
    // Unlike the TT (aged via new_search() each iteration), killers and
    // history are NOT reset between iterations on purpose: a killer or
    // a historically-good quiet move from a shallower iteration is
    // still a reasonable ordering bet for the next, deeper one, and
    // letting them persist is exactly how real engines use iterative
    // deepening to make each successive iteration cheaper.
    TranspositionTable tt(kDefaultTTSizeMB);
    KillerTable killers;
    HistoryTable history;

    // Depth 1 always runs unconditionally, before any time check, so
    // there's always a legal best_move to fall back on (see search.h's
    // header comment).
    tt.new_search();
    SearchResult result = search_root(pos, 1, tt, killers, history);
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
        SearchResult next = search_root(pos, depth, tt, killers, history);
        total_nodes += next.nodes;
        result = next;
    }

    result.nodes = total_nodes;
    return result;
}

} // namespace nightwing::search
