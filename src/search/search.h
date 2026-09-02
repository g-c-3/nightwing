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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "board/board.h"
#include "board/move.h"
#include "eval/psqt.h"

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

/// Shared state for mid-search time-budget interruption (ROADMAP.md
/// Priority Fix, "Mid-search time checks" — promoted once its own
/// documented revisit trigger, real `wtime`/`btime`/`movetime` UCI
/// parsing, had been met for some time without the revisit happening;
/// see docs/DECISIONS.md, the 2026-08-13 iterative-deepening entry and
/// the external-code-review entry that followed it). A single instance
/// is constructed per search_iterative_deepening() depth iteration
/// (search.cpp) and threaded by pointer through every negamax()/
/// quiescence() call for that one iteration, the same way `nodes`/
/// `tt`/`killers`/etc. are already threaded — every caller that
/// doesn't want a time budget at all (search_fixed_depth(), every
/// existing test/bench call, and iterative deepening's own mandatory,
/// always-uninterrupted depth-1 iteration) simply omits it, since it
/// defaults to `nullptr` throughout.
///
/// `stopped` is checked at the top of every negamax()/quiescence()
/// call (search.cpp, quiescence.cpp) before any real per-node work —
/// once true, that call returns immediately (cheap, O(1) — no TT
/// probe, no movegen) rather than continuing to search, and its own
/// caller, one level up, does the same on its very next check rather
/// than trusting the just-returned score to update its own alpha/
/// best-move bookkeeping or TT store. `stopped` is set at most once
/// per iteration, the moment some call happens to notice the deadline
/// has passed (checked only periodically, by node count, not on every
/// single node — see search.cpp/quiescence.cpp's kTimeCheckNodeInterval
/// for why), and is never reset within that iteration — a fresh
/// SearchLimits is constructed for the next one instead.
///
/// This is a bounded, not a surgical, unwind: a node that was mid-way
/// through a null-move/ProbCut/singular-extension probe (search.cpp)
/// when `stopped` became true can briefly treat a now-meaningless
/// truncated-subtree score as a real cutoff before ITS OWN next
/// negamax() call re-checks `stopped` and short-circuits in turn — but
/// this costs at most a small, bounded number of additional node
/// visits along the current search path (each of which re-checks
/// `stopped` first), not continued exploration of the remaining tree,
/// and it never reaches a TT store or an iteration's own reported
/// best_move: negamax()'s and search_root()'s own TT stores are both
/// skipped once `stopped` is observed, and the entire iteration's
/// SearchResult is discarded wholesale by search_iterative_deepening()
/// in favor of the previous, fully-completed iteration's — see its own
/// comments in search.cpp.
struct SearchLimits {
    /// Absolute wall-clock deadline, meaningful only when `has_deadline`
    /// is true.
    std::chrono::steady_clock::time_point deadline;

    /// False means "no time budget for this call" — negamax()/
    /// quiescence() skip both the periodic clock check and the
    /// `stopped` fast-path check entirely in that case (a null
    /// SearchLimits* has the same effect; this flag exists for the
    /// case where a SearchLimits instance is threaded through but this
    /// particular iteration/call still shouldn't be interrupted, e.g.
    /// iterative deepening's own always-complete depth-1 iteration if
    /// it were ever threaded this way instead of passing nullptr).
    bool has_deadline = false;

    /// Set true the moment a periodic check (search.cpp/quiescence.cpp)
    /// notices `deadline` has passed. Never reset within one
    /// SearchLimits instance's lifetime.
    bool stopped = false;

    /// Second, independent way to request interruption, alongside (not
    /// instead of) `deadline`/`has_deadline` above -- added for Lazy SMP
    /// (ROADMAP.md Phase 7, search.cpp's search_iterative_deepening()):
    /// a helper thread's own SearchLimits has no fixed `deadline` of its
    /// own to search against (it's bounded by `max_depth` and by the
    /// main thread finishing, not by wall-clock time), so it's told to
    /// stop by the main thread flipping this shared atomic to `true`
    /// once the main thread's own iterative-deepening loop is done. The
    /// SAME periodic check (search.cpp/quiescence.cpp, gated by node
    /// count exactly like the deadline check) also checks this, when
    /// non-null, and sets `stopped` exactly the same way a passed
    /// deadline would -- everything downstream of `stopped` (the
    /// bounded, non-surgical unwind described above) is unaffected by
    /// *which* of the two conditions triggered it. `nullptr` (the
    /// default) means "no external stop signal for this call" -- every
    /// existing caller (none of which pass a non-null value) is
    /// unaffected. Relaxed atomic: this is a cooperative, best-effort
    /// "stop soon" signal checked only periodically (kTimeCheckNodeInterval,
    /// search.cpp), not a value anything else's correctness depends on
    /// ordering against.
    std::atomic<bool>* external_stop = nullptr;
};

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

    /// Principal variation: `best_move` first, then each subsequent
    /// position's own best-known continuation, reconstructed by walking
    /// the transposition table (search.cpp's extract_pv()) rather than
    /// tracked incrementally during the search itself (CPW "Triangular
    /// PV Table" is the other standard technique — deliberately not
    /// used here; a TT walk needed no extra per-node bookkeeping
    /// threaded through negamax()'s own recursion, at the cost of a PV
    /// that can come back shorter than `depth_completed` if the TT
    /// replacement scheme (tt.h) has since evicted a needed entry, or
    /// stop early at a node whose stored bound isn't Exact — both
    /// accepted, documented imprecisions of the TT-walk approach, not
    /// bugs). Empty when `best_move` is null (nothing to walk from).
    /// Populated for search_fixed_depth() too, not just iterative
    /// deepening — same reconstruction, just from that one call's own
    /// (fresh, private) TT.
    std::vector<board::Move> pv;
};

/// Optional callback invoked by search_iterative_deepening() once after
/// each iteration that genuinely COMPLETES — including the mandatory
/// depth-1 iteration, but excluding any iteration discarded because it
/// was interrupted mid-search (SearchLimits, this file's own comments
/// above) — passing that iteration's own SearchResult (best_move,
/// score, depth_completed, and pv all reflect THIS iteration; `nodes`
/// is the CUMULATIVE total across every iteration so far, matching the
/// conventional meaning of a UCI `info nodes` line, not just this one
/// iteration's own count — see search.cpp's own comment at the call
/// site for why). Exists so a caller like uci.cpp can emit `info depth
/// ... score cp ... nodes ... pv ...` live, once per completed
/// iteration, rather than only learning the final result after the
/// whole call returns. Defaults to nullptr (no-op) — every existing
/// test/bench call site is unaffected.
using IterationCallback = std::function<void(const SearchResult&)>;

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
/// `material_weights`, if non-null, is forwarded to every
/// eval::evaluate()/quiescence() call this search makes, at every node,
/// INSTEAD OF the compiled-in material constants (eval/psqt.h's
/// MaterialWeights, and eval::evaluate()'s own doc comment on this
/// parameter) — the entry point tuner::match (src/tuner/match.h) uses
/// to have one side of a match search under a different weight vector
/// than the other, and the not-yet-fully-built gradient-descent tuner
/// itself never needed (tuner::tune's own compute_loss() calls
/// eval::evaluate() directly, bypassing search entirely — a raw static
/// eval at the labeled position is exactly what Texel's Tuning Method
/// wants, not a searched score). Defaults to nullptr, meaning "use the
/// compiled-in constants" — every existing caller is entirely
/// unaffected.
///
/// Precondition: `depth >= 1`, and init_masks()/init_magic_bitboards()
/// have been called (movegen's precondition, transitively — see
/// board/movegen.h).
[[nodiscard]] SearchResult search_fixed_depth(board::Position& pos, int depth,
                                               std::span<const std::uint64_t> game_history = {},
                                               const eval::MaterialWeights* material_weights =
                                                   nullptr);

/// Runs iterative deepening: searches at depth = 1, 2, 3, ... up to
/// `max_depth`, keeping the most recently *completed* iteration's
/// result. If `time_limit_ms` is positive, the loop stops in two ways:
/// between iterations (before starting the next one, once the elapsed
/// time already exceeds the budget — the original, coarser check), and
/// now also mid-iteration, from depth 2 onward (see search.h's
/// SearchLimits and search.cpp's own comments): each iteration from
/// depth 2 on is given a SearchLimits sharing the same deadline, and
/// negamax()/quiescence() periodically check it as they search. An
/// iteration interrupted this way is discarded wholesale — its
/// (necessarily incomplete) result is never used to update the
/// returned SearchResult, which keeps the previous, fully-completed
/// iteration's best_move/score instead — so "completed" above means
/// both "the call returned" and "it wasn't interrupted partway."
/// Depth 1 always runs unconditionally, with no deadline at all (not
/// just no check), before either kind of check, so the result always
/// has a legal best_move (when `pos` has one at all) even under an
/// extremely tight time budget — see SearchLimits::has_deadline. Pass
/// `time_limit_ms = 0` (the default) for no time limit at all, i.e.
/// always search all the way to `max_depth`.
///
/// If `pos` has no legal moves at all, returns immediately after the
/// depth-1 call (see SearchResult::depth_completed) rather than
/// wastefully repeating the same terminal result at deeper depths.
///
/// Precondition: same as search_fixed_depth() — `max_depth >= 1`, and
/// init_masks()/init_magic_bitboards() must already have been called.
///
/// `game_history`: same meaning and default as search_fixed_depth()'s
/// parameter of the same name — see that function's doc comment. Shared
/// across every depth iteration of this one call, same as `pos` itself.
///
/// `on_iteration`, if non-null, is invoked once per genuinely completed
/// iteration — see IterationCallback's own doc comment above for the
/// full contract (including why the depth-1 iteration always fires it,
/// and why an interrupted iteration never does).
/// `material_weights`: same meaning and default as search_fixed_depth()'s
/// parameter of the same name — see that function's doc comment. Shared
/// across every depth iteration of this one call, same as `pos` itself.
///
/// `num_threads` (ROADMAP.md Phase 7, "Lazy SMP implementation"):
/// values <= 1 (the default) behave EXACTLY as before this parameter
/// existed — no thread is spawned, the whole call runs single-threaded
/// on the calling thread, byte-for-byte the same code path this
/// function always ran. Values > 1 spawn `num_threads - 1` additional
/// "helper" threads once the mandatory, always-unconditional depth-1
/// iteration above has produced a real `best_move` (search_root() on
/// `pos` itself, still on the calling thread) — the calling thread then
/// continues running its OWN iterative-deepening loop (identical logic
/// to the single-threaded path: aspiration windows, `on_iteration`,
/// `time_limit_ms`) exactly as it always has, while each helper thread
/// searches the SAME root position, from its own PRIVATE copy of `pos`
/// (board::Position is a value type; a stack copy is taken on the
/// calling thread, one per helper, before that helper starts running —
/// see search.cpp for why the copy must happen there and not inside the
/// helper's own thread function) and its own private killers/history/
/// continuation-history/pawn-hash/eval-cache tables (all per-thread —
/// see this file's own comments on why those tables persist across
/// iterations even single-threaded; sharing them across threads instead
/// would need their own thread-safety story with no clear benefit, since
/// their whole value is ordering hints for a search that's already
/// running), through a plain (no aspiration window) depth 1..max_depth
/// loop of their own, deliberately simplified relative to the calling
/// thread's own loop (Lazy SMP's classic "helper threads just widen the
/// tree explored, not replicate the primary search's every refinement"
/// framing — see docs/DECISIONS.md). The one thing every thread
/// genuinely SHARES is the transposition table `tt` (search/tt.h,
/// TranspositionTable — safe for exactly this concurrent use, see its
/// own THREAD-SAFETY NOTE): a helper thread's own discoveries land in
/// `tt` where the calling thread's own search can (and, that's the
/// whole point of Lazy SMP, often does) benefit from them via ordinary
/// TT probes, without either thread's search logic needing to know
/// helpers exist at all. Helper threads are signaled to stop (a shared
/// `std::atomic<bool>`, checked via SearchLimits::external_stop) once
/// the calling thread's own loop finishes, then joined, before this
/// function returns — the returned SearchResult's `best_move`/`score`/
/// `pv` are always the CALLING thread's own result (helpers never
/// influence them directly, only indirectly via `tt`), while `nodes` is
/// the sum of every thread's node count (calling thread's own cumulative
/// total, matching the `num_threads <= 1` convention, PLUS every helper
/// thread's own total) — the right denominator for `bench`/NPS
/// reporting (ROADMAP.md Phase 8) once multiple threads are genuinely
/// doing the reported work. If `pos` has no legal moves at all (the
/// depth-1 call's own early return, above), no helper thread is ever
/// spawned — there is nothing for one to search either.
///
/// No UCI `Threads` option exists yet to let a user configure this —
/// that's ROADMAP.md Phase 7's own, separate "Thread count UCI option"
/// item; every current caller (src/uci/uci.cpp, the tuner) passes the
/// default (1) and is unaffected by this parameter's addition.
[[nodiscard]] SearchResult search_iterative_deepening(
    board::Position& pos, int max_depth, int time_limit_ms = 0,
    std::span<const std::uint64_t> game_history = {}, IterationCallback on_iteration = nullptr,
    const eval::MaterialWeights* material_weights = nullptr, int num_threads = 1);

} // namespace nightwing::search
