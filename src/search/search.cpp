// src/search/search.cpp

#include "search/search.h"

#include <array>
#include <cassert>
#include <chrono>
#include <span>

#include "board/movegen.h"
#include "eval/eval.h"
#include "eval/pawn_tt.h"
#include "search/ordering.h"
#include "search/quiescence.h"
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

/// Placeholder default pawn hash table size, same lifetime caveat as
/// kDefaultTTSizeMB just above -- KB, not MB, matching eval::PawnHashTable's
/// constructor (eval/pawn_tt.h's header comment on why this table is
/// sized much smaller than the main TT).
constexpr std::size_t kDefaultPawnTTSizeKB = 512;

/// Starting half-width (centipawns) of the aspiration window
/// search_iterative_deepening() centers on the previous iteration's
/// score for depth >= 2 (CPW "Aspiration Windows"; see that function's
/// comments for the full scheme). Doubled on each fail-high/fail-low
/// retry. 25 centipawns is a common, reasonable starting point in
/// other engines' implementations of this technique -- not yet tuned
/// for Nightwing specifically (ROADMAP.md's Texel/SPSA tuner, once it
/// exists, targets eval terms first; a search constant like this one
/// is a plausible later tuning target, not a claim that 25 is already
/// verified optimal here).
constexpr int kAspirationInitialDelta = 25;

/// Internal Iterative Reduction (IIR) thresholds: a node with no
/// transposition-table entry at all has no move-ordering hint (see
/// negamax()'s header comment for the full technique). Only applied
/// beyond this minimum remaining depth (below it, the effect isn't
/// meaningful and isn't worth risking), reducing the effective depth
/// searched at that node by this many plies. Both are common, modest
/// starting values in other engines' IIR implementations -- not yet
/// tuned for Nightwing specifically, same caveat as
/// kAspirationInitialDelta above.
constexpr int kIIRMinDepth = 4;
constexpr int kIIRReduction = 1;

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

/// Returns true if `pos` (whose Zobrist hash is `key`, passed separately
/// since every caller already has it on hand) should be scored as an
/// immediate draw for search purposes, checking two independent rules:
///
/// 50-move rule (CPW "50-move Rule"): `pos.halfmove_clock >= 100` (100
/// plies = 50 full moves since the last pawn move or capture) is treated
/// as an automatic draw, the near-universal engine convention — real
/// FIDE play requires a player to *claim* the draw, but essentially
/// every UCI engine scores this as an unconditional draw during search
/// rather than modeling the claim itself, and this implementation does
/// the same.
///
/// Repetition (CPW "Repetition Detection"): scored as a draw on a
/// position's SECOND occurrence within reach of this node, not
/// waiting for an actual third — standard engine practice, since once a
/// position has recurred once, a side can force a real threefold simply
/// by repeating it again, so the second occurrence is already
/// draw-equivalent information for search purposes. Only ever looks
/// back up to `pos.halfmove_clock` plies: a position from before the
/// most recent pawn move or capture can never recur (any recurrence
/// would itself have to cross that same irreversible move, which is
/// impossible), so this bounds the lookback naturally without needing
/// to track exactly how far `game_history`/`path` extend.
///
/// `game_history` is every ancestor position's hash strictly before the
/// search root (see search.h's header comment) — NOT including the
/// root. `path[0]` is the root's own hash (written once by
/// search_root(), see its comments) and `path[p]` for p in [1, ply) is
/// the hash negamax() itself recorded at every shallower ply of the
/// current root-to-here search path (see negamax()'s header comment) —
/// together, `game_history` followed by `path[0..ply]` form one
/// continuous, chronologically-ordered sequence ending at this node,
/// walked backward here without ever materializing it as a single
/// combined array.
[[nodiscard]] bool is_draw_by_rule(const Position& pos, std::uint64_t key, int ply,
                                    std::span<const std::uint64_t> game_history,
                                    const std::array<std::uint64_t, kMaxPly>& path) noexcept {
    if (pos.halfmove_clock >= 100) {
        return true;
    }
    if (ply >= kMaxPly) {
        // Comfortably beyond any depth this engine reaches in practice
        // today (search/ordering.h's kMaxPly header comment) -- skip
        // repetition tracking here rather than risk an out-of-bounds
        // `path` access below; the 50-move check above still applies
        // regardless.
        return false;
    }

    const int history_len = static_cast<int>(game_history.size());
    const int cur = history_len + ply; // this node's own index in the conceptual combined sequence
    const int lookback = pos.halfmove_clock;

    for (int steps = 1; steps <= lookback; ++steps) {
        const int idx = cur - steps;
        if (idx < 0) {
            break; // Walked past all available history -- nothing further to check.
        }
        const std::uint64_t candidate = idx >= history_len
                                             ? path[static_cast<std::size_t>(idx - history_len)]
                                             : game_history[static_cast<std::size_t>(idx)];
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

/// Repetition and 50-move-rule detection (see is_draw_by_rule() just
/// above) is checked immediately after the node-count increment, before
/// even mate distance pruning's clamp — deliberately the very first
/// thing this function does at a real (depth >= 1) node. Two reasons for
/// going first: it's the cheapest possible check (no movegen, no TT
/// probe), and — more importantly — a position that's a draw by
/// repetition was very possibly reached through a *different* path last
/// time the search (or a previous iterative-deepening iteration) visited
/// this same Zobrist key, so any cached TT entry for it isn't safe to
/// trust here (the well-known "Graph History Interaction" problem: a TT
/// entry doesn't know which path reached it). Checking and returning
/// before the TT probe below means a draw-by-repetition/50-move score is
/// never looked up from a stale, wrong-context TT entry — and, since
/// this function returns immediately when it applies, such a score is
/// also never itself stored into the TT (the store happens at the
/// bottom of this function, which this early return skips entirely),
/// so a real, path-independent evaluation of this same key from a
/// different path is never at risk of being overwritten by a
/// path-dependent draw score either. `game_history`/`path` are passed
/// through unchanged to every recursive call below, and `path[ply]` is
/// written right after `key` is computed (see below) so a DEEPER node's
/// own is_draw_by_rule() call can see this node as part of its walk
/// back — this reuses `path` as a single flat, per-ply array the same
/// way search/ordering.h's KillerTable does (ARCHITECTURE.md "Memory &
/// Cache": no heap allocation for per-node search state), overwritten
/// as the search backtracks and descends a different line, which is
/// correct precisely because only the currently-active root-to-here
/// path is ever read backward from, never a stale sibling's leftover
/// entry at the same ply.
///
/// Negamax alpha-beta search. Returns a score from `pos.side_to_move`'s
/// perspective (positive = good for the side to move). At depth <= 0
/// this hands off entirely to quiescence search (search/quiescence.h)
/// rather than returning a raw eval::evaluate() call directly — see
/// that module's header comment for why (the short version: avoiding
/// the horizon effect by resolving in-flight tactics before trusting a
/// static eval).
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
/// Mate distance pruning (CPW "Mate Distance Pruning") is applied right
/// at the top of this function, before the TT probe or movegen: alpha/
/// beta are clamped to the best/worst scores actually reachable from
/// this node given `ply` (the same -(kMateScore - ply) formula the
/// `moves.empty()` branch below uses for "mated right here," and its
/// mirror kMateScore - ply - 1 for "deliver mate as fast as possible
/// from here"), and if that clamping alone collapses alpha >= beta, the
/// node returns immediately with no movegen/TT/ordering work at all.
/// This never changes what any node returns relative to plain alpha-
/// beta -- it only short-circuits nodes whose window was already
/// unreachable because some shallower ancestor has already found a
/// forced mate shorter than anything this node could possibly still
/// improve on.
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
///
/// The PVS loop below no longer special-cases depth == 1 (it did, prior
/// to this session): that special case existed because a depth-1 move's
/// child used to be a depth-0 leaf resolved by a raw eval::evaluate()
/// call that never consulted alpha/beta at all, so a null-window probe
/// there genuinely could never prune anything -- just wasted work.
/// Now that depth <= 0 hands off to quiescence search (search/
/// quiescence.h) instead, which DOES meaningfully respond to whatever
/// window it's given (its own stand-pat comparison and cutoff logic),
/// that's no longer true -- a null-window probe at depth == 1 can
/// genuinely fail low and get skipped, same as at any deeper depth, so
/// it's folded into the general PVS branch below rather than kept as a
/// stale special case.
///
/// Internal Iterative Reduction (IIR): a node with no TT entry at all
/// (probe.hit == false) has no move-ordering hint to search efficiently
/// with -- CPW's modern alternative to Internal Iterative Deepening,
/// which spent extra search effort *at this same node* up front trying
/// to manufacture a move-ordering hint before the real search. IIR
/// instead just accepts a slightly shallower search here (reducing the
/// effective `depth` by kIIRReduction, only when the unreduced `depth`
/// is already at least kIIRMinDepth), trusting iterative deepening's
/// own outer loop to naturally revisit this node with a real TT-move
/// hint on a later, less-reduced iteration -- exactly why this is safe
/// in the way PVS/TT/aspiration windows were NOT: those were provably
/// exact (same best move/score as plain full-window alpha-beta,
/// regardless of how they were called); IIR is a genuine heuristic
/// approximation, like the pruning/reduction techniques ROADMAP.md's
/// remaining Phase 3/4 items add, whose safety net is iterative
/// deepening's own self-correction across iterations, not per-call
/// exactness -- a single search_fixed_depth() call with no shallower
/// iteration to have already populated the TT has no such correction
/// available, so it's a real (if generally small) accuracy/depth
/// trade at any one node, same as it is in every other engine that
/// uses this technique. `depth` is reassigned in place (not a separate
/// variable) so every downstream use in this function -- the move
/// loop's recursive calls and the TT store at the bottom -- correctly
/// reflects the depth actually searched, not the depth originally
/// requested.
int negamax(Position& pos, int depth, int alpha, int beta, int ply, std::uint64_t& nodes,
            TranspositionTable& tt, KillerTable& killers, HistoryTable& history,
            std::span<const std::uint64_t> game_history, std::array<std::uint64_t, kMaxPly>& path,
            eval::PawnHashTable& pawn_tt) {
    if (depth <= 0) {
        // Quiescence search (search/quiescence.h) rather than a raw
        // eval::evaluate() call: resolves in-flight capture/check
        // sequences right at the horizon instead of trusting a static
        // eval that might be mid-exchange (the "horizon effect," CPW
        // "Quiescence Search"). `nodes` is passed through and
        // incremented by quiescence()'s own counting, not incremented
        // again here first -- this negamax() call itself does no
        // "node work" of its own at depth <= 0, it's a pure delegation,
        // so counting it separately here would double-count the same
        // conceptual node.
        return quiescence(pos, alpha, beta, ply, nodes, /*include_checks=*/true, &pawn_tt);
    }

    ++nodes;

    const std::uint64_t key = pos.zobrist_hash;
    if (ply < kMaxPly) {
        // Record this node's own hash before any recursive call below
        // can need to see it (see negamax()'s header comment on why
        // `path` is written this way, mirroring KillerTable's per-ply
        // array reuse).
        path[static_cast<std::size_t>(ply)] = key;
    }
    if (is_draw_by_rule(pos, key, ply, game_history, path)) {
        // Not stored in the TT -- see negamax()'s header comment on why
        // this check deliberately runs before the TT probe below (the
        // Graph History Interaction problem).
        return kDrawScore;
    }

    // Mate distance pruning (CPW "Mate Distance Pruning"): regardless of
    // what the rest of this node finds, the best score achievable here
    // is capped at kMateScore - ply - 1 (delivering mate one ply deeper
    // than this node -- the fastest mate reachable from here), and the
    // worst score achievable is floored at -(kMateScore - ply) (getting
    // mated at this very node -- the identical formula the
    // `moves.empty()` branch below already uses). Clamping alpha/beta to
    // those bounds before doing any real work lets a window that's
    // already outside what's reachable from this node fail immediately:
    // if a shallower ancestor has already found a shorter forced mate
    // than anything achievable here, alpha/beta collapse and there's
    // nothing left to search for. This is a pure efficiency win, not a
    // change in what any node returns -- a node whose window wasn't
    // already unreachable in this way is completely unaffected, since
    // clamping alpha/beta to bounds that are still looser than the
    // caller's own window is a no-op.
    if (alpha < -(kMateScore - ply)) {
        alpha = -(kMateScore - ply);
    }
    if (beta > kMateScore - ply - 1) {
        beta = kMateScore - ply - 1;
    }
    if (alpha >= beta) {
        return alpha;
    }

    const int alpha_orig = alpha;
    const Color us = pos.side_to_move;
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

    // IIR (see this function's header comment): no TT entry at all for
    // this position, and deep enough that a shallower search here still
    // does something useful. Reassigning `depth` in place is
    // deliberate -- everything below (the move loop's recursive calls,
    // the TT store at the bottom) must reflect what was ACTUALLY
    // searched, not the depth this call was originally asked for.
    if (!probe.hit && depth >= kIIRMinDepth) {
        depth -= kIIRReduction;
    }

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
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt, killers, history,
                              game_history, path, pawn_tt);
        } else {
            // PVS: probe every later move with a null (zero-width)
            // window first -- cheap, since it only needs to prove
            // "fails low against alpha" or "fails high," not compute an
            // exact score. Only if the probe suggests this move might
            // actually beat alpha (a fail-high on the null window that's
            // still below beta) is it worth paying for a full-window
            // re-search to get its real score.
            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, nodes, tt, killers, history,
                              game_history, path, pawn_tt);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes, tt, killers, history,
                                  game_history, path, pawn_tt);
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
/// this function. Deliberately does NOT apply Internal Iterative
/// Reduction (negamax()'s header comment) to its own `depth`: the root
/// is the exact position a caller asked to search to a specific depth
/// (via search_fixed_depth()'s parameter, or the current iteration of
/// search_iterative_deepening()'s loop) -- silently searching it any
/// shallower than requested would misreport SearchResult::depth_completed
/// and give a UCI caller a less-deep analysis than it asked for. IIR
/// only ever reduces *internal* nodes, reached through negamax()'s own
/// recursion below the root.
///
/// `aspiration_alpha`/`aspiration_beta` is the search window to use --
/// search_fixed_depth() always passes the full (-kInfinity, kInfinity)
/// window (no previous iteration's score to aspirate around);
/// search_iterative_deepening() narrows it for depth >= 2 (see its own
/// comments and docs/DECISIONS.md's aspiration-windows entry). Callers
/// must check the RETURNED result.score against the window they passed
/// in: if it's <= aspiration_alpha or >= aspiration_beta, the search
/// only proved a BOUND, not an exact score (CPW "Aspiration Windows" --
/// same fail-low/fail-high concept as negamax()'s own TT-bound
/// classification, just applied to the whole root call instead of one
/// node) -- best_move in that case is not to be trusted as final either
/// (see below), and the caller is expected to re-search with a wider
/// window rather than accept the result as-is.
/// `game_history`/`path`: threaded straight through to every negamax()
/// call below unchanged -- see negamax()'s header comment and
/// is_draw_by_rule() for what they're for. `path[0]` is set to this
/// call's own root_key right after it's computed, below, since the root
/// itself is never passed through negamax() (it has no ply of its own
/// in that sense) but its hash still needs to be part of the sequence a
/// deeper node's repetition check can walk back through.
/// `pawn_tt`: threaded straight through to every negamax() call below
/// unchanged, which threads it further into quiescence()/eval::evaluate()
/// -- see eval/eval.h's doc comment on evaluate()'s own `pawn_tt`
/// parameter for what it's for.
SearchResult search_root(Position& pos, int depth, int aspiration_alpha, int aspiration_beta,
                          TranspositionTable& tt, KillerTable& killers, HistoryTable& history,
                          std::span<const std::uint64_t> game_history,
                          std::array<std::uint64_t, kMaxPly>& path, eval::PawnHashTable& pawn_tt) {
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

    // Root-level TT interaction, added last session alongside move
    // ordering (deferred the session before specifically until ordering
    // existed to consume it -- see docs/DECISIONS.md, 2026-08-15 TT
    // entry, sub-decision 4). Probed only for an ordering hint below,
    // never for an early return/cutoff: the root always needs its FULL
    // legal move list reviewed to report a genuine best move (there's
    // no "skip searching, trust the cache" shortcut that would still
    // give a UCI-quality result), unlike internal negamax() nodes which
    // are free to trust a sufficiently-deep proven bound.
    const std::uint64_t root_key = pos.zobrist_hash;
    path[0] = root_key; // See this function's header comment.
    const TTProbeResult root_probe = tt.probe(root_key, 0);
    const Move tt_move = root_probe.hit ? root_probe.move : Move();

    order_moves(moves, pos, tt_move, killers, /*ply=*/0, history);

    int alpha = aspiration_alpha;
    const int beta = aspiration_beta;
    Move best_move = moves[0];

    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        UndoInfo undo;
        board::make_move(pos, move, undo);

        // Root-level PVS, mirroring negamax()'s move loop (see its
        // comments for the full rationale) with one deliberate
        // difference: first move -- now the ordering-selected best
        // candidate, not just move-generation order -- gets the full
        // [alpha, beta] window (the aspiration window when one is in
        // effect, not necessarily the full (-inf,+inf) range -- see
        // this function's header comment), and later moves at depth >= 2
        // get a null-window probe first, re-searched with the full
        // window only on a fail-high that's still below beta -- but
        // later moves specifically at depth == 1 still skip straight to
        // a full-window search here, unlike negamax()'s own loop (which
        // dropped this special case this session, now that quiescence
        // search genuinely responds to a narrow window -- see negamax()'s
        // header comment). The difference is deliberate, not a stale
        // leftover: a depth-1 REQUEST only ever reaches this branch via
        // search_fixed_depth(pos, 1) or iterative deepening's mandatory
        // first iteration, both of which always pass the full
        // (-inf, +inf) window in the first place (no previous score to
        // aspirate around yet -- search_iterative_deepening()'s own
        // comments), so `beta` here is always kInfinity when depth == 1
        // specifically, and a null-window probe against an
        // already-infinite beta has nothing to gain from skipping to.
        int score;
        if (i == 0 || depth == 1) {
            score = -negamax(pos, depth - 1, -beta, -alpha, 1, result.nodes, tt, killers, history,
                              game_history, path, pawn_tt);
        } else {
            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, 1, result.nodes, tt, killers, history,
                              game_history, path, pawn_tt);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, 1, result.nodes, tt, killers, history,
                                  game_history, path, pawn_tt);
            }
        }

        board::unmake_move(pos, move, undo);

        if (score > alpha) {
            alpha = score;
            best_move = move;
        }
        if (alpha >= beta) {
            // Fail-high against the (possibly narrow, aspiration) window
            // -- with a full (-inf,+inf) window this can never trigger
            // (beta == kInfinity, no real score reaches it), so this is
            // a no-op for search_fixed_depth(). With a narrow aspiration
            // window it means this iteration's result is only a bound,
            // not exact -- searching the remaining moves under the same
            // too-narrow window can't produce a trustworthy result
            // either, so stop immediately rather than waste nodes; the
            // caller (search_iterative_deepening()) is expected to
            // detect this (result.score >= aspiration_beta) and re-
            // search with a wider window.
            break;
        }
    }

    // Whether `alpha` here is an exact score, or only a bound, depends
    // on how the loop above ended relative to the window passed in --
    // see this function's header comment for what the caller is
    // expected to check. Only store an Exact TT entry when the result
    // genuinely is one (CPW "Node Types" -- storing an unproven exact
    // bound as Exact would let a later probe trust a value that isn't
    // actually exact): a fail-high (the break above) only proves a
    // LOWER bound, and a fail-low (no move ever beat aspiration_alpha,
    // so `alpha` here still equals the original aspiration_alpha
    // exactly) only proves an UPPER bound -- both mirror negamax()'s own
    // Exact/Lower/Upper classification at the bottom of that function,
    // just evaluated over the whole root call instead of one node.
    const Bound bound_type = alpha <= aspiration_alpha ? Bound::Upper
                            : alpha >= aspiration_beta  ? Bound::Lower
                                                         : Bound::Exact;
    tt.store(root_key, depth, alpha, bound_type, best_move, 0);

    result.best_move = best_move;
    result.score = alpha;
    result.depth_completed = depth;
    return result;
}

} // namespace

SearchResult search_fixed_depth(Position& pos, int depth, std::span<const std::uint64_t> game_history) {
    assert(depth >= 1 && "search_fixed_depth: depth must be at least 1");

    // Fresh, private tables for this one call (see tt.h's header
    // comment, which applies equally to KillerTable/HistoryTable --
    // search/ordering.h). kDefaultTTSizeMB is a placeholder until the
    // UCI `Hash` option (ROADMAP.md Phase 8) makes table size
    // configurable and the tables themselves persistent across calls.
    TranspositionTable tt(kDefaultTTSizeMB);
    KillerTable killers;
    HistoryTable history;
    // Fresh, zero-initialized per-ply hash record for this one call (see
    // negamax()'s header comment and is_draw_by_rule()) -- a fixed-size
    // stack array, not heap-allocated (ARCHITECTURE.md "Memory & Cache"),
    // sized via search/ordering.h's existing kMaxPly rather than a new
    // constant.
    std::array<std::uint64_t, kMaxPly> path{};
    // Fresh, private pawn hash table for this one call (same lifetime
    // scoping/rationale as tt/killers/history just above -- see
    // eval/pawn_tt.h's header comment).
    eval::PawnHashTable pawn_tt(kDefaultPawnTTSizeKB);
    // No previous iteration's score to aspirate around (see
    // search_iterative_deepening() below and docs/DECISIONS.md's
    // aspiration-windows entry) -- always the full window.
    return search_root(pos, depth, -kInfinity, kInfinity, tt, killers, history, game_history, path,
                        pawn_tt);
}

SearchResult search_iterative_deepening(Position& pos, int max_depth, int time_limit_ms,
                                         std::span<const std::uint64_t> game_history) {
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
    // Shared across every iteration of this call too, same rationale as
    // tt/killers/history just above (see negamax()'s header comment and
    // is_draw_by_rule()) -- a later, deeper iteration re-deriving the
    // same repetition information from scratch would be wasted work,
    // and more importantly wouldn't change anything: `path` is
    // overwritten ply-by-ply as each iteration's own root-to-here search
    // proceeds, the same reuse pattern as within a single negamax() call.
    std::array<std::uint64_t, kMaxPly> path{};
    // Shared across every iteration too -- see eval/pawn_tt.h's header
    // comment for the same lifetime rationale as tt/killers/history.
    eval::PawnHashTable pawn_tt(kDefaultPawnTTSizeKB);

    // Depth 1 always runs unconditionally, before any time check, so
    // there's always a legal best_move to fall back on (see search.h's
    // header comment). Full window: there's no previous iteration yet
    // to aspirate around (see the depth-2-onward loop below).
    tt.new_search();
    SearchResult result = search_root(pos, 1, -kInfinity, kInfinity, tt, killers, history,
                                       game_history, path, pawn_tt);
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
        SearchResult next;

        // Aspiration windows (CPW "Aspiration Windows"): the previous
        // iteration's score is usually a good estimate of this
        // iteration's score too (positions rarely swing wildly between
        // one ply of search depth and the next), so start this
        // iteration with a narrow window centered on it instead of the
        // full (-inf,+inf) range -- a narrower window means more beta
        // cutoffs happen sooner throughout the tree, at the cost of an
        // occasional fail-high/fail-low needing a wider re-search when
        // the estimate turns out wrong. Skipped when the previous
        // score is already in mate-score territory (see kMateThreshold,
        // search.h): mate scores are a different regime entirely, far
        // outside any small centipawn-sized window, so aspirating
        // around one would just guarantee a wasted first attempt for no
        // benefit -- go straight to the full window instead.
        if (result.score > -kMateThreshold && result.score < kMateThreshold) {
            int delta = kAspirationInitialDelta;
            int window_alpha = result.score - delta;
            int window_beta = result.score + delta;
            if (window_alpha < -kInfinity) {
                window_alpha = -kInfinity;
            }
            if (window_beta > kInfinity) {
                window_beta = kInfinity;
            }

            for (;;) {
                next = search_root(pos, depth, window_alpha, window_beta, tt, killers, history,
                                    game_history, path, pawn_tt);

                if (next.score <= window_alpha && window_alpha > -kInfinity) {
                    // Fail low: the true score is <= window_alpha, exact
                    // value unknown -- search_root()'s own best_move for
                    // this attempt isn't trustworthy either (see its
                    // header comment), so this attempt's result is
                    // discarded entirely by the next loop iteration.
                    // Widen downward and retry.
                    delta *= 2;
                    window_alpha = result.score - delta;
                    if (window_alpha < -kInfinity) {
                        window_alpha = -kInfinity;
                    }
                } else if (next.score >= window_beta && window_beta < kInfinity) {
                    // Fail high: symmetric case, widen upward and retry.
                    delta *= 2;
                    window_beta = result.score + delta;
                    if (window_beta > kInfinity) {
                        window_beta = kInfinity;
                    }
                } else {
                    // Either the score landed strictly inside the window
                    // (an exact result -- CPW "Aspiration Windows"), or
                    // the window has already widened out to the full
                    // (-inf,+inf) range, which is unconditionally exact
                    // by construction (same as search_fixed_depth()'s
                    // own always-full-window search) -- either way,
                    // done.
                    break;
                }
            }
        } else {
            next = search_root(pos, depth, -kInfinity, kInfinity, tt, killers, history, game_history,
                                path, pawn_tt);
        }

        total_nodes += next.nodes;
        result = next;
    }

    result.nodes = total_nodes;
    return result;
}

} // namespace nightwing::search
