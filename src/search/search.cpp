// src/search/search.cpp

#include "search/search.h"

#include <algorithm>
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

/// Null-move pruning (CPW "Null Move Pruning", negamax()'s NMP block
/// below) constants. Not yet tuned (ROADMAP.md Phase 5's Texel/SPSA
/// tuner is scoped for eval terms specifically; a future session
/// extending it, or a separate search-parameter tuning pass, may revise
/// these) -- a simple, well-established two-tier reduction scheme
/// common across many "classical" engines, chosen for this first-draft
/// implementation over a smoother adaptive formula (e.g. `3 + depth/6`)
/// specifically to keep the logic easy to hand-verify without a
/// compiler available.
constexpr int kNullMoveMinDepth = 3;        // don't bother below this remaining depth
constexpr int kNullMoveReduction = 2;       // R for depth < kNullMoveBigReductionDepth
constexpr int kNullMoveBigReductionDepth = 6;
constexpr int kNullMoveBigReduction = 3;    // R for depth >= kNullMoveBigReductionDepth

/// Late move reductions (CPW "Late Move Reductions", negamax()'s move
/// loop below) constants. Same "simple, hand-verifiable two-tier
/// scheme over a smoother formula" choice as the null-move-pruning
/// constants just above, and the same not-yet-tuned status.
constexpr int kLMRMinDepth = 3;        // don't bother below this remaining depth
constexpr int kLMRMinMoveIndex = 4;    // don't reduce the first few (already well-ordered) moves
constexpr int kLMRReduction = 1;       // R for depth < kLMRBigReductionDepth
constexpr int kLMRBigReductionDepth = 6;
constexpr int kLMRBigReduction = 2;    // R for depth >= kLMRBigReductionDepth

/// Late move pruning (LMP) / move-count based pruning (CPW "Move Count
/// Based Pruning", negamax()'s move loop below) constants. Only applies
/// at shallow remaining depth (kLMPMaxDepth) -- the technique's own
/// premise, "this many other candidates already failed to help, so one
/// more deep into an already-long tail almost certainly won't either,"
/// gets weaker the more plies of search remain to prove that wrong.
/// kLMPMoveCountLimits is a fixed lookup table (index 0 unused --
/// negamax() never reaches its move loop with depth <= 0, see the
/// quiescence delegation at the top of this function -- kept only so
/// kLMPMoveCountLimits[depth] reads directly, no off-by-one offset)
/// rather than a formula, matching this project's existing preference
/// (LMR/NMP's own two-tier constants above) for something exactly
/// hand-verifiable at each depth without a compiler available. Roughly
/// quadratic growth, a common shape for this technique in other
/// engines -- not yet tuned for Nightwing specifically, same caveat as
/// every other first-draft pruning constant in this file.
constexpr int kLMPMaxDepth = 8;
constexpr std::array<int, kLMPMaxDepth + 1> kLMPMoveCountLimits = {
    0, 5, 8, 13, 18, 25, 32, 41, 50,
};

/// History pruning (CPW "History Leaf Pruning" / move loop below)
/// constants: distinct from, and checked alongside, LMP -- LMP's skip
/// is purely a function of HOW MANY quiet moves have already been
/// tried (`quiets_tried`), while this is a function of a specific
/// move's own accumulated `HistoryTable` score (search/ordering.h),
/// regardless of its position in the list. `order_moves()` already
/// sorts quiet moves by descending history score, so in practice a
/// move failing this threshold tends to sit late in the quiet tail
/// anyway -- but this check can trigger independently of (and
/// potentially earlier than) LMP's own move-count threshold at very
/// shallow depth, when even an early quiet candidate's history score is
/// poor enough. kHistoryPruningThresholds is a fixed lookup table
/// (index 0 unused, same convention as kLMPMoveCountLimits above),
/// decreasing with depth (more aggressive right at the shallowest
/// depth, near-disabled by kHistoryPruningMaxDepth's own ceiling) --
/// same hand-verification-without-a-compiler reasoning as every other
/// pruning constant in this file, not yet tuned for Nightwing
/// specifically.
constexpr int kHistoryPruningMaxDepth = 3;
constexpr std::array<int, kHistoryPruningMaxDepth + 1> kHistoryPruningThresholds = {
    0, 300, 150, 50,
};

/// Futility pruning (CPW "Futility Pruning", negamax()'s move loop
/// below) constants. At shallow remaining depth, if the node's own
/// static evaluation (computed once, before the move loop -- the value
/// doesn't depend on which move is being considered) is already so far
/// below alpha that even a generous per-depth margin couldn't plausibly
/// close the gap, quiet, non-check-giving moves at this node are
/// skipped outright rather than searched -- CPW's own stated caveat
/// applies (`static_eval` is a rough proxy, not the move's real
/// post-move value, so this is a heuristic, not provably exact).
/// kFutilityMargins is a fixed lookup table (index 0 unused, same
/// reasoning as kLMPMoveCountLimits just above) rather than a formula,
/// for the same hand-verification-without-a-compiler reasoning as
/// every other pruning constant in this file. Deliberately narrow
/// (kFutilityMaxDepth = 3, CPW's own "frontier"/near-frontier range) --
/// the margin's job is bounding how much a quiet move could plausibly
/// swing the eval by remaining-depth plies of further search, and that
/// bound gets far less trustworthy the deeper the remaining search is.
/// Linear margin growth (100cp per remaining ply) rather than a
/// quadratic or exponential shape, matching the project's existing
/// preference for the simplest scheme that's still a real, documented
/// first draft -- not yet tuned for Nightwing specifically, same
/// caveat as every other constant in this file.
constexpr int kFutilityMaxDepth = 3;
constexpr std::array<int, kFutilityMaxDepth + 1> kFutilityMargins = {
    0, 100, 200, 300,
};

/// Razoring (CPW "Razoring", negamax()'s own node-level check right
/// after the NMP block below) constants. Similar shape to futility
/// pruning's margins above -- a fixed lookup table, index 0 unused, not
/// yet tuned -- but deliberately wider at every depth: razoring is a
/// more drastic decision than futility (it drops the ENTIRE node
/// straight into quiescence search instead of running the normal move
/// loop at all, not just skipping individual late quiet moves), so it
/// needs stronger evidence -- a bigger gap between static eval and
/// alpha -- before it's worth trusting. kRazorMaxDepth (3) matches
/// kFutilityMaxDepth's own shallow-only scope, for the same reason: a
/// static eval's margin is a much weaker signal about what deeper
/// search might still find the further from the leaves it's applied.
constexpr int kRazorMaxDepth = 3;
constexpr std::array<int, kRazorMaxDepth + 1> kRazorMargins = {
    0, 300, 400, 500,
};

/// ProbCut (CPW "ProbCut", negamax()'s own node-level check just before
/// the main move loop below) constants. Opposite end of the depth
/// spectrum from futility/razoring above: those apply near the leaves
/// (shallow remaining depth, looking for a reason to fail LOW early);
/// ProbCut applies at moderate-to-high remaining depth (kProbCutMinDepth
/// and up), looking for a reason to fail HIGH early -- a cheap, reduced
/// -depth verification search against a raised window
/// (`beta + kProbCutMargin`) that, if it also fails high, is taken as
/// strong evidence a full-depth search would too, without paying for
/// one. kProbCutReduction (4) is deliberately large relative to LMR's
/// own kLMRReduction/kLMRBigReduction (1-2) above -- ProbCut's
/// verification search only needs to be roughly right about "is this
/// position winning by at least a large, specific margin," not compute
/// an exact score, so a much shallower probe is an acceptable trade for
/// this technique specifically, the same way NMP's own R (this file's
/// kNullMoveReduction) is chosen independently of LMR's. Not yet tuned
/// for Nightwing specifically, same caveat as every other constant in
/// this file.
constexpr int kProbCutMinDepth = 5;
constexpr int kProbCutMargin = 200; // centipawns
constexpr int kProbCutReduction = 4;

/// Check extensions (CPW "Check Extensions", negamax()'s move loop
/// below) constant: the FIRST extension technique added to this file
/// (every earlier Phase 4 addition was a pruning/reduction technique
/// that removes search depth, never adds it; singular extensions below
/// are this file's second) -- when a move gives check, the child search
/// below it is granted one extra ply (`depth - 1 + kCheckExtensionPly`
/// instead of the usual `depth - 1`) rather than losing one, on the
/// premise that a forced check-response sequence is exactly the kind of
/// tactical line the horizon effect (this file's own quiescence()-
/// related header comments) is most likely to misjudge if cut off one
/// ply too early -- and a position that's just been put in check has,
/// structurally, far fewer legal replies than an ordinary position, so
/// the extra ply doesn't cost nearly as much branching as it would
/// anywhere else in the tree.
constexpr int kCheckExtensionPly = 1;

/// Singular extensions (CPW "Singular Extensions", negamax()'s move
/// loop below, evaluated only for the TT move specifically) constants.
/// Unlike check extensions (a cheap, purely local decision -- just look
/// at whether the move gives check), this technique pays for a real,
/// reduced-depth VERIFICATION search of every OTHER legal move at the
/// node, all under a narrow window built from the TT's own previously-
/// stored score, before deciding whether the TT move is "singular" --
/// so much better than every alternative that none of them can even
/// approach a score just below it. If none can, the TT move is
/// considered forced/critical enough to deserve an extra ply of its own
/// child search, on the premise that a position with only one genuinely
/// good move is exactly where a shallow search is most likely to
/// misjudge how forced the line actually is. Deliberately expensive
/// (an entire extra search per eligible node), so gated behind several
/// guards: `kSingularMinDepth` (8) -- shallow nodes can't afford to pay
/// for a verification search at all; `kSingularTTDepthMargin` (3) -- the
/// TT entry itself must be from a search deep enough to trust (`probe.
/// depth >= depth - kSingularTTDepthMargin`), or its stored score isn't
/// a reliable enough baseline to build a verification window from;
/// `kSingularMarginPerPly` (2) -- `singular_beta = probe.score -
/// kSingularMarginPerPly * depth`, a margin that widens with depth
/// (rather than a fixed lookup table the way most of this file's other
/// margins are -- depth here can range from kSingularMinDepth up
/// through however deep iterative deepening goes, too wide a range for
/// a hand-verifiable table the way kFutilityMargins/kRazorMargins could
/// afford with their own narrow, capped depth ranges); `kSingularDepth
/// Divisor` (2) -- the verification search itself runs at `(depth - 1)
/// / kSingularDepthDivisor`, roughly half the remaining depth, cheap
/// enough to be worth paying for while still deep enough to mean
/// something. `kSingularExtensionPly` (1) -- same size as check
/// extensions' own bonus; combined via `std::max()`, not summed, with
/// any check-extension bonus the same move might also separately
/// qualify for (this function's move loop), so a move never gets
/// double-extended for two different reasons at once.
constexpr int kSingularMinDepth = 8;
constexpr int kSingularTTDepthMargin = 3;
constexpr int kSingularMarginPerPly = 2;
constexpr int kSingularDepthDivisor = 2;
constexpr int kSingularExtensionPly = 1;

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
/// Check extensions (CPW "Check Extensions", the move loop below) are
/// the FIRST DEPTH-ADDING technique in this file -- every earlier
/// Phase 4 addition (LMP, futility, razoring, history pruning, ProbCut,
/// LMR itself) removes search depth, never adds it; singular extensions
/// just below are this file's second. A move that gives check gets its
/// own child search granted one extra ply (kCheckExtensionPly) rather
/// than losing the usual one, since a forced check-response sequence is
/// exactly the shape of tactical line the horizon effect is most likely
/// to misjudge if cut off one ply too early, and a position that's just
/// been put in check structurally has far fewer legal replies than an
/// ordinary one, so the extra ply costs much less branching than it
/// would elsewhere. Guarded by `ply + 1 < kMaxPly` -- not just a
/// niceness check, a genuine correctness requirement: `path` and the
/// killer/PV bookkeeping this function relies on are fixed-size arrays
/// sized by kMaxPly (search/ordering.h), so an unbounded chain of check
/// extensions pushing `ply` past that bound would be an out-of-bounds
/// write, not just a wasted search. `move_gives_check` (computed once
/// per move, right after make_move() -- shared by futility/LMP/history-
/// pruning's own checks above AND this) is what decides both the
/// extension here AND LMR's own eligibility just below: a checking move
/// is never LMR-reduced (previously an oversight -- LMR's own guard
/// excluded captures/promotions but not checks, even though a checking
/// move is exactly as tactical/forcing as either of those and CPW's own
/// LMR guidance excludes them for the identical reason NMP/LMR already
/// exclude captures) -- so extension and reduction are naturally
/// mutually exclusive per move under this design: a move is either
/// forcing enough to extend, or ordinary enough to consider reducing,
/// never plausibly both.
///
/// Singular extensions (CPW "Singular Extensions", also the move loop
/// below, but evaluated ONLY for the TT move -- see this file's
/// kSingular* constants for the full guard list and rationale) are this
/// file's second, more expensive depth-adding technique: when the TT's
/// own previously-stored score for this node's TT move indicates it
/// caused a beta cutoff before, and every OTHER legal move here fails a
/// cheap, reduced-depth verification search against a window built just
/// below that stored score, the TT move is judged "singular" (the only
/// genuinely good option at this node) and its own child search gets
/// the same one-ply bonus check extensions grant -- combined via
/// `std::max()` with any check-extension bonus, never summed, so a
/// checking TT move that's ALSO judged singular still only gets
/// extended once. Unlike check extensions (a free, purely local
/// look-at-the-move decision), this pays for a genuine extra search per
/// eligible node, which is exactly why it's gated behind kSingularMin
/// Depth and the TT-entry-freshness guard, not applied unconditionally
/// the way check extensions are.
///
/// Late move reductions (CPW "Late Move Reductions", the move loop
/// below) reduce depth for LATE, QUIET moves specifically -- ones far
/// enough into the already-ordered move list (order_moves(), search/
/// ordering.h) that they're unlikely to be the best move here, so a
/// cheap reduced-depth probe first is worth the risk of occasionally
/// needing a full-depth re-verification. This slots into the existing
/// PVS null-window structure as one MORE fallback step before it, not a
/// separate mechanism: reduced null-window probe -> (only if it beats
/// alpha) full-DEPTH null-window re-check -> (only if THAT still beats
/// alpha and stays below beta) the existing full-WINDOW re-search. A
/// move never gets reduced at all when in check (few, forced-feeling
/// replies -- not a good candidate for a shortcut), when IT ITSELF
/// gives check (see this file's check-extensions section just above --
/// it gets extended instead), when it's a capture or promotion
/// (tactical moves need full-depth verification, the same reasoning
/// NMP's own guards lean on), or when it's not late enough in the list
/// yet (kLMRMinMoveIndex) -- see this file's kLMR* constants for the
/// exact thresholds.
///
/// Late move pruning (CPW "Move Count Based Pruning", the move loop
/// below) goes one step further than LMR for a strict SUBSET of what
/// LMR would otherwise reduce: once enough quiet, non-check-giving
/// moves have already been tried at this node (this function's own
/// `quiets_tried` counter, not the raw move index -- LMP's premise is
/// specifically about how many quiet ALTERNATIVES have already failed
/// to help, not where a move happens to sit in a list that also
/// contains captures/promotions ahead of it) without any of them
/// raising alpha, the remaining quiet tail is skipped outright --
/// never even a reduced probe -- rather than searched at all. Only
/// applies at shallow remaining depth (kLMPMaxDepth) and never when
/// this node itself is in check (few, forced-feeling replies -- the
/// same reasoning LMR excludes in-check nodes for) or when alpha is
/// already in mate-score range (a position this close to a proven mate
/// needs every candidate reply actually checked, not skipped on a
/// move-count heuristic). "Gives check" is determined the same way
/// as everywhere else in this codebase without a dedicated move flag:
/// in_check(pos) read immediately after board::make_move() applies the
/// move, since side-to-move has already flipped to the opponent by
/// then -- a true result means this move gives check. See this file's
/// kLMP* constants for the exact per-depth thresholds.
///
/// History pruning (CPW "History Leaf Pruning", the move loop below) is
/// checked alongside LMP, using the same quiet/non-check-giving move
/// restriction, but a different signal: not how many quiet alternatives
/// have already been tried (LMP's `quiets_tried`), but this SPECIFIC
/// move's own accumulated `HistoryTable` score (search/ordering.h) --
/// skipped outright when that score is below a per-depth threshold, on
/// the premise that a quiet move which has rarely-if-ever caused a beta
/// cutoff elsewhere in this search is a poor bet to spend a full search
/// on this late in the game tree. Independent of LMP's own check (both
/// run, either can trigger the skip on its own) rather than folded into
/// one combined condition, since they measure genuinely different
/// things and either one failing is already sufficient reason to skip.
/// See this file's kHistoryPruning* constants for the exact per-depth
/// thresholds.
///
/// Futility pruning (CPW "Futility Pruning", the move loop below) is a
/// third, node-level check alongside LMP, evaluated once per node (not
/// once per move -- unlike LMP's `quiets_tried` counter, the underlying
/// condition, "is this node's static eval already too far below alpha
/// for a quiet move to plausibly close the gap," doesn't change as the
/// move loop progresses, only the move being considered does) using
/// `eval::evaluate()` computed before the move loop starts. Only
/// computed at all when it could actually matter (shallow remaining
/// depth, not in check, alpha not already in mate range) to avoid
/// paying eval's cost at nodes where futility could never apply anyway.
/// Same quiet/non-check-giving move restriction as LMP, for the same
/// reason (a capture or a check can swing the position's real value
/// well past a static margin's estimate). See this file's kFutility*
/// constants for the exact per-depth margins.
///
/// Razoring (CPW "Razoring", the node-level check right after the NMP
/// block below, before movegen) is a more drastic cousin of futility
/// pruning: instead of skipping individual late quiet moves inside the
/// move loop, it can skip the ENTIRE move loop for this node when the
/// static eval is so far below alpha (a wider margin than futility's
/// own, see kRazor* constants) that no move here plausibly recovers.
/// Rather than trusting that verdict outright, it drops into
/// quiescence search (which still correctly resolves the position's
/// own captures/checks/terminal status -- see quiescence()'s own
/// header comment) and only returns early if the quiescence result
/// ITSELF independently confirms the same conclusion (still <= alpha)
/// -- CPW's "razoring with verification," safer than trusting a wide
/// static-eval margin alone. If quiescence comes back ABOVE alpha, the
/// static eval's pessimism was wrong, and this falls through to the
/// normal move loop below rather than returning a bad, unverified
/// score. Self-contained (its own `in_check()`/`eval::evaluate()`
/// calls, not sharing state with futility pruning's own later,
/// separately-computed static eval) for the same reason NMP's own
/// block is self-contained -- it runs at a different point in this
/// function (before movegen even happens) than futility does (after
/// order_moves(), inside the move loop's own preamble).
///
/// ProbCut (CPW "ProbCut," the node-level check just before the main
/// move loop, right after order_moves() -- see kProbCut* constants
/// above) is razoring's mirror image: instead of looking for a reason
/// to fail LOW early at shallow depth, it looks for a reason to fail
/// HIGH early at moderate-to-high depth. It reuses the SAME already-
/// ordered `moves` list order_moves() just produced (no second movegen
/// or ordering pass) rather than being self-contained the way razoring
/// is -- razoring runs before movegen even happens, so it has nothing
/// to reuse yet, while ProbCut deliberately runs after, specifically so
/// its own verification search can walk captures/promotions in their
/// already-best-first MVV-LVA order rather than redo that work. For
/// each capture or promotion in `moves` (quiet moves are skipped with
/// `continue`, not `break` -- the TT move, which can be quiet, is
/// always tried first regardless of type, so a quiet move at index 0
/// doesn't mean everything after it is quiet too), a reduced-depth
/// search against a raised, null (`beta + kProbCutMargin`) window
/// checks whether this one move alone can already prove the position
/// is winning by at least that inflated margin; if it can, that's
/// taken as strong enough evidence a full-depth search of the whole
/// node would also fail high that this function returns immediately
/// (fail-soft: the verification score itself, not just `beta` --
/// mirroring NMP's own fail-soft return just below, including the same
/// mate-range clamp and for the identical reason: a raw score from a
/// REDUCED search shouldn't be trusted as an exact mate distance).
///
/// Null-move pruning (see the NMP block right after IIR, below) needs
/// one more piece of state IIR/IID's own logic never did: whether a
/// null move is even allowed at this node. `allow_null_move` defaults
/// to true for every ordinary call (every existing call site is
/// unaffected and doesn't need updating); the ONE place this function
/// ever passes false explicitly is its own recursive call inside the
/// NMP block, for the single child that call makes -- CPW's "no two
/// null moves in a row" rule (skipping straight past every real move
/// at a node by permitting the very next node to also just pass would
/// prove nothing about the actual position). This is deliberately NOT
/// a "was there a null move anywhere among this node's ancestors"
/// check -- only immediate adjacency matters.
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
/// convention; from-scratch implementation here. `cont_history`,
/// `prev_piece`, and `prev_to` describe the move immediately preceding
/// this call (the move the caller just made to reach `pos`) -- threaded
/// through so this node's own order_moves() call and its own
/// killer/history-style update on a beta cutoff (this function's move
/// loop, below) can both consult/update continuation history (see
/// search/ordering.h's ContinuationHistoryTable for the full
/// rationale). `prev_piece` is `board::PieceType::None` when there
/// isn't a real preceding move to condition on -- immediately after a
/// null move (this function's own NMP block skips passing continuation
/// context to its null-move child for exactly that reason) -- which
/// makes continuation history contribute nothing at that node, same as
/// if the table were empty. `tt` (search/tt.h) is
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
            ContinuationHistoryTable& cont_history, board::PieceType prev_piece,
            board::Square prev_to, std::span<const std::uint64_t> game_history,
            std::array<std::uint64_t, kMaxPly>& path, eval::PawnHashTable& pawn_tt,
            bool allow_null_move = true) {
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

    // Null-move pruning (CPW "Null Move Pruning"): if we're not in
    // check and it's still our move even after "passing" (giving the
    // opponent a free tempo -- searched at reduced depth, since this is
    // just a cheap plausibility probe, not a real line worth full
    // depth), and that STILL isn't enough to bring the score down to
    // beta, then a real move (which can only do at least as well as
    // passing) would certainly also reach beta -- so this node can be
    // pruned outright without searching any of its real moves.
    //
    // Guards, each corresponding to a real unsoundness risk otherwise:
    // in check (board::make_null_move()'s own precondition -- a null
    // move can't escape check, not a legal chess outcome, and would
    // corrupt the search); two null moves in a row (`allow_null_move`,
    // see this function's header comment); too shallow (kNullMoveMinDepth
    // -- not worth the reduced re-search's own cost below a minimum);
    // zugzwang risk (CPW's own caveat -- skipped whenever the side to
    // move has no non-pawn, non-king material, the classic
    // king-and-pawn-endgame case where "passing" can be a strictly
    // WORSE option than any real move, making the technique unsound
    // there); and a mate-range beta (a reduced-depth null-move probe
    // stumbling onto what looks like a mate score isn't a trustworthy
    // claim of an actual forced mate at full depth).
    const bool non_pawn_material =
        (pos.pieces(us, board::PieceType::Knight) | pos.pieces(us, board::PieceType::Bishop) |
         pos.pieces(us, board::PieceType::Rook) | pos.pieces(us, board::PieceType::Queen)) != 0;
    if (allow_null_move && depth >= kNullMoveMinDepth && beta < kMateThreshold && non_pawn_material &&
        !in_check(pos)) {
        const int reduction =
            depth >= kNullMoveBigReductionDepth ? kNullMoveBigReduction : kNullMoveReduction;
        UndoInfo null_undo;
        board::make_null_move(pos, null_undo);
        const int null_score = -negamax(pos, depth - 1 - reduction, -beta, -beta + 1, ply + 1, nodes,
                                         tt, killers, history, cont_history,
                                         /*prev_piece=*/board::PieceType::None, /*prev_to=*/0,
                                         game_history, path, pawn_tt,
                                         /*allow_null_move=*/false);
        board::unmake_null_move(pos, null_undo);
        if (null_score >= beta) {
            // Don't hand back a mate-range score verbatim from a
            // reduced, unverified probe (CPW's own caution) -- clamp to
            // beta instead of trusting an unconfirmed "mate" claim.
            return null_score >= kMateThreshold ? beta : null_score;
        }
    }

    // Razoring (CPW "Razoring", this function's header comment): only
    // at shallow remaining depth, not in check, and only when this
    // node's own static eval is so far below alpha that even a wide
    // margin makes it implausible any move here recovers. Rather than
    // trusting that verdict outright and returning the static eval
    // directly, drop into quiescence search (which still correctly
    // resolves in-flight captures/checks and this position's own
    // terminal status) and only return early if THAT result
    // independently confirms the same conclusion.
    if (!in_check(pos) && depth <= kRazorMaxDepth && alpha < kMateThreshold) {
        const int white_relative = eval::evaluate(pos, &pawn_tt);
        const int razor_static_eval = us == Color::White ? white_relative : -white_relative;
        if (razor_static_eval + kRazorMargins[static_cast<std::size_t>(depth)] <= alpha) {
            const int razor_score =
                quiescence(pos, alpha, beta, ply, nodes, /*include_checks=*/true, &pawn_tt);
            if (razor_score <= alpha) {
                return razor_score;
            }
            // Quiescence disagreed with the static eval's pessimism --
            // fall through to the normal move loop below rather than
            // trusting the unverified margin.
        }
    }

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        // Terminal position: not stored in the TT (see this function's
        // header comment) — movegen already paid the cost of detecting
        // this, and there's no move-loop result left to cache.
        return in_check(pos) ? -(kMateScore - ply) : kDrawScore;
    }

    order_moves(moves, pos, tt_move, killers, ply, history, cont_history, prev_piece, prev_to);

    // Computed once, reused by LMR's eligibility check below (moves
    // themselves don't change whether the position THEY'RE PLAYED FROM
    // was in check) rather than re-derived per move.
    const bool us_in_check = in_check(pos);

    // ProbCut (CPW "ProbCut", this function's header comment): only at
    // moderate-to-high remaining depth, not in check, and only when
    // beta itself isn't already mate-range (a raised, inflated window
    // built from a mate score would be meaningless -- same reasoning as
    // NMP's own beta guard just below in this file). Walks the SAME
    // already-ordered `moves` list order_moves() just produced, trying
    // only captures/promotions (quiet moves are `continue`d past, not a
    // `break`, since the TT move -- always tried first regardless of
    // type -- can itself be quiet without that meaning everything after
    // it is quiet too).
    if (!us_in_check && depth >= kProbCutMinDepth && beta < kMateThreshold) {
        const int probcut_beta = beta + kProbCutMargin;
        for (int i = 0; i < moves.size(); ++i) {
            const Move probcut_move = moves[i];
            if (!probcut_move.is_capture() && !probcut_move.is_promotion()) {
                continue;
            }
            const board::PieceType probcut_moved_piece =
                board::piece_type_of(pos.piece_at(probcut_move.from()));
            UndoInfo probcut_undo;
            board::make_move(pos, probcut_move, probcut_undo);
            const int probcut_score =
                -negamax(pos, depth - kProbCutReduction, -probcut_beta, -probcut_beta + 1, ply + 1,
                         nodes, tt, killers, history, cont_history, probcut_moved_piece,
                         probcut_move.to(), game_history, path, pawn_tt);
            board::unmake_move(pos, probcut_move, probcut_undo);
            if (probcut_score >= probcut_beta) {
                // This one move alone, even searched shallower than the
                // rest of this node will be, already proves the
                // position is winning by more than a normal beta cutoff
                // would need -- strong enough evidence that a full-depth
                // search of the whole node would also fail high that
                // it's not worth paying for one. Fail-soft (the
                // verification score itself, not just `beta`), clamped
                // the same way NMP's own null-move probe is just below:
                // a REDUCED search's score shouldn't be trusted as an
                // exact mate distance.
                return probcut_score >= kMateThreshold ? beta : probcut_score;
            }
        }
    }

    // Futility pruning's static eval (this function's header comment):
    // computed once per node, before the move loop, since the
    // condition it feeds doesn't depend on which move is being
    // considered -- only on the position BEFORE any of them are
    // played. Only computed when it could actually be used (shallow
    // remaining depth, not in check, alpha not already mate-range) to
    // avoid paying eval::evaluate()'s cost at nodes where futility
    // could never apply anyway.
    const bool futility_may_apply =
        !us_in_check && depth <= kFutilityMaxDepth && alpha < kMateThreshold;
    int static_eval = 0;
    if (futility_may_apply) {
        const int white_relative = eval::evaluate(pos, &pawn_tt);
        static_eval = us == Color::White ? white_relative : -white_relative;
    }
    const bool futility_prune_node =
        futility_may_apply &&
        static_eval + kFutilityMargins[static_cast<std::size_t>(depth)] <= alpha;

    int best = -kInfinity;
    Move best_move; // Move() default (null) unless overwritten below — every real position has >=1 move here.
    // Count of quiet (non-capture, non-promotion) moves already tried at
    // this node, incremented once per move below regardless of which
    // path that move took (searched, LMR-reduced, or LMP-skipped) --
    // late move pruning's own threshold check (kLMP* constants above,
    // this function's header comment) is keyed on this, not the raw
    // move index `i`, since LMP's premise is specifically about how many
    // quiet ALTERNATIVES have already failed to help, not where a move
    // sits in a list that also contains captures/promotions ahead of it.
    int quiets_tried = 0;
    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        const bool move_is_quiet = !move.is_capture() && !move.is_promotion();
        // The piece making this move, read BEFORE make_move() below
        // vacates its from-square -- threaded into every recursive
        // negamax() call as that child's own `prev_piece`/`prev_to` (see
        // this function's header comment on continuation history).
        // Computed uniformly for every move, including captures and
        // promotions, matching mvv_lva_score()'s own convention
        // (search/ordering.cpp) of reading the attacker's piece type off
        // the from-square the same way regardless of move type.
        const board::PieceType moved_piece = board::piece_type_of(pos.piece_at(move.from()));

        // Singular extensions (this function's header comment): only
        // evaluated for the TT move itself (order_moves() places it
        // first whenever `tt_move` is present and legal, so this is
        // scoped to `i == 0`), and using `pos` as it stands BEFORE
        // `move` is played -- the verification search below tries every
        // OTHER legal move from this same position, so it has to run
        // before this iteration's own make_move() changes it.
        int singular_extension = 0;
        if (i == 0 && probe.hit && move == tt_move && probe.bound == Bound::Lower &&
            depth >= kSingularMinDepth && probe.depth >= depth - kSingularTTDepthMargin &&
            probe.score > -kMateThreshold && probe.score < kMateThreshold) {
            const int singular_beta = probe.score - kSingularMarginPerPly * depth;
            const int singular_depth = (depth - 1) / kSingularDepthDivisor;
            bool any_alternative_matched = false;
            for (int j = 0; j < moves.size() && !any_alternative_matched; ++j) {
                if (moves[j] == tt_move) {
                    continue;
                }
                const Move alt_move = moves[j];
                const board::PieceType alt_moved_piece =
                    board::piece_type_of(pos.piece_at(alt_move.from()));
                UndoInfo alt_undo;
                board::make_move(pos, alt_move, alt_undo);
                const int alt_score =
                    -negamax(pos, singular_depth, -singular_beta, -singular_beta + 1, ply + 1, nodes,
                             tt, killers, history, cont_history, alt_moved_piece, alt_move.to(),
                             game_history, path, pawn_tt);
                board::unmake_move(pos, alt_move, alt_undo);
                if (alt_score >= singular_beta) {
                    // Some other move can already reach almost as high a
                    // score as the TT move's own previous cutoff --
                    // disproves singularity (the TT move isn't the ONLY
                    // good option here), so no extension.
                    any_alternative_matched = true;
                }
            }
            if (!any_alternative_matched) {
                singular_extension = kSingularExtensionPly;
            }
        }

        UndoInfo undo;
        board::make_move(pos, move, undo);

        // "Gives check" is computed once here, right after the move is
        // already applied -- no dedicated move flag exists in this
        // codebase for it (see this function's header comment) -- and
        // shared by every check below that needs it, including the
        // first move (i == 0): check extensions (this function's own
        // header comment) apply to ANY checking move, not just late
        // ones, so this can no longer be computed only inside the
        // i != 0 branch the way it used to be.
        const bool move_gives_check = in_check(pos);

        // Check extensions (this function's header comment): a checking
        // move's own child search gets one extra ply rather than losing
        // the usual one -- guarded by `ply + 1 < kMaxPly`, a genuine
        // correctness requirement (see the header comment) rather than
        // just a niceness check, since path/killer bookkeeping is
        // sized by kMaxPly. Combined with any singular-extension bonus
        // computed just above via std::max(), not summed (this
        // function's header comment) -- a move never gets extended
        // twice for two different reasons at once.
        const int check_extension = (move_gives_check && ply + 1 < kMaxPly) ? kCheckExtensionPly : 0;
        const int extension = std::max(check_extension, singular_extension);

        int score;
        if (i == 0) {
            // First move -- now genuinely the highest-priority candidate
            // per order_moves() above (TT move, else best-scoring
            // capture/promotion/killer/history move), not just "first in
            // move-generation order" as it was pre-ordering: search it
            // with the full alpha-beta window to establish a real score
            // to compare everything else against. `+ extension` applies
            // even to this first move -- a checking or singular move
            // deserves the extra ply regardless of its position in the
            // ordering.
            score = -negamax(pos, depth - 1 + extension, -beta, -alpha, ply + 1, nodes, tt, killers,
                              history, cont_history, moved_piece, move.to(), game_history, path,
                              pawn_tt);
        } else {
            // Futility pruning (CPW "Futility Pruning", this function's
            // header comment): a node-level condition -- computed once,
            // before the move loop, since it doesn't depend on which
            // move is being tried -- checked first among this branch's
            // three cascading checks since, like LMP, it can skip the
            // move outright at zero search cost. Same quiet/non-check
            // restriction as LMP, for the same reason (a capture or
            // check can swing the real value well past a static
            // margin's estimate).
            if (futility_prune_node && move_is_quiet && !move_gives_check) {
                board::unmake_move(pos, move, undo);
                ++quiets_tried;
                continue;
            }

            // Late move pruning (CPW "Move Count Based Pruning", this
            // function's header comment): a strict subset of LMR's own
            // eligibility, checked first since it can skip the move
            // entirely -- no reduced probe, no PVS null-window search at
            // all.
            if (!us_in_check && move_is_quiet && !move_gives_check && depth <= kLMPMaxDepth &&
                quiets_tried >= kLMPMoveCountLimits[static_cast<std::size_t>(depth)] &&
                alpha > -kMateThreshold) {
                board::unmake_move(pos, move, undo);
                ++quiets_tried;
                continue;
            }

            // History pruning (CPW "History Leaf Pruning", this
            // function's header comment): independent of LMP's own
            // check just above -- a different signal (this move's own
            // HistoryTable score, not how many quiet alternatives have
            // already been tried), either sufficient on its own to skip
            // the move. Same not-in-check/quiet/non-check-giving/
            // mate-range guards as LMP, for the same reasons.
            if (!us_in_check && move_is_quiet && !move_gives_check &&
                depth <= kHistoryPruningMaxDepth && alpha > -kMateThreshold &&
                history.score(us, move) < kHistoryPruningThresholds[static_cast<std::size_t>(depth)]) {
                board::unmake_move(pos, move, undo);
                ++quiets_tried;
                continue;
            }

            // Late move reductions (CPW "Late Move Reductions", this
            // function's header comment): only quiet, non-check-evading,
            // non-check-GIVING (see this file's check-extensions header
            // comment -- a checking move is extended instead, never
            // reduced, as of this session) moves late enough in
            // order_moves()'s ranking get reduced -- see kLMR* constants
            // above for the exact thresholds.
            const bool eligible_for_lmr = !us_in_check && depth >= kLMRMinDepth &&
                                           i >= kLMRMinMoveIndex && !move.is_capture() &&
                                           !move.is_promotion() && !move_gives_check;
            const int reduction = eligible_for_lmr ? (depth >= kLMRBigReductionDepth
                                                            ? kLMRBigReduction
                                                            : kLMRReduction)
                                                     : 0;

            // PVS: probe every later move with a null (zero-width)
            // window first -- cheap, since it only needs to prove
            // "fails low against alpha" or "fails high," not compute an
            // exact score. Only if the probe suggests this move might
            // actually beat alpha (a fail-high on the null window that's
            // still below beta) is it worth paying for a full-window
            // re-search to get its real score. `+ extension - reduction`
            // are naturally mutually exclusive per move under this
            // session's design (see this function's header comment) --
            // see it for how that interacts with the two fallback steps
            // below.
            score = -negamax(pos, depth - 1 + extension - reduction, -alpha - 1, -alpha, ply + 1,
                              nodes, tt, killers, history, cont_history, moved_piece, move.to(),
                              game_history, path, pawn_tt);
            if (reduction > 0 && score > alpha) {
                // The reduced probe suggested this move might actually
                // be good -- not trustworthy on its own (a shallower
                // search can overstate a quiet move's value), so
                // re-verify at full depth (still a null window -- this
                // is still just a probe) before deciding whether the
                // full-window re-search below is warranted. `extension`
                // is always 0 here (reduction > 0 implies eligible_for_lmr
                // was true, which requires !move_gives_check, which is
                // the only thing that ever sets extension > 0).
                score = -negamax(pos, depth - 1 + extension, -alpha - 1, -alpha, ply + 1, nodes, tt,
                                  killers, history, cont_history, moved_piece, move.to(), game_history,
                                  path, pawn_tt);
            }
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1 + extension, -beta, -alpha, ply + 1, nodes, tt,
                                  killers, history, cont_history, moved_piece, move.to(), game_history,
                                  path, pawn_tt);
            }
        }

        board::unmake_move(pos, move, undo);
        if (move_is_quiet) {
            ++quiets_tried;
        }

        if (score > best) {
            best = score;
            best_move = move;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            // Beta cutoff. Record it for future ordering (killers +
            // history + continuation history) only for quiet,
            // non-promotion moves -- captures and promotions already
            // order well via MVV-LVA/promotion value (see ordering.h's
            // header comment), so mixing them into the killer/history
            // scheme adds noise without adding information (CPW's
            // "Killer Heuristic"/"History Heuristic" are conventionally
            // quiet-move-only for the same reason).
            if (!move.is_capture() && !move.is_promotion()) {
                killers.update(ply, move);
                history.update(us, move, depth);
                // No-op if prev_piece is board::PieceType::None (no real
                // preceding move at this node -- see this function's
                // header comment and ContinuationHistoryTable's own).
                cont_history.update(prev_piece, prev_to, moved_piece, move.to(), depth);
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
/// parameter for what it's for. `cont_history`: threaded through to
/// every negamax() call below alongside killers/history (search/
/// ordering.h's ContinuationHistoryTable) -- but this function's OWN
/// order_moves() call (below) always passes board::PieceType::None for
/// `prev_piece`, not a real piece: the root has no preceding move of
/// its own to condition on (the game move that led to this exact
/// position isn't threaded into search_fixed_depth()/
/// search_iterative_deepening() today, only game_history's hashes are
/// -- a deliberate first-draft scope limit, not an oversight; revisit
/// if/when a real move, not just a hash, is threaded that far). Each
/// root MOVE's own piece/destination, by contrast, genuinely is known
/// (computed fresh per iteration of the loop below) and IS passed as
/// `prev_piece`/`prev_to` into that move's negamax() children, exactly
/// as negamax()'s own move loop does for its own children.
SearchResult search_root(Position& pos, int depth, int aspiration_alpha, int aspiration_beta,
                          TranspositionTable& tt, KillerTable& killers, HistoryTable& history,
                          ContinuationHistoryTable& cont_history,
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

    order_moves(moves, pos, tt_move, killers, /*ply=*/0, history, cont_history,
                /*prev_piece=*/board::PieceType::None, /*prev_to=*/0);

    int alpha = aspiration_alpha;
    const int beta = aspiration_beta;
    Move best_move = moves[0];

    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        // Same rationale as negamax()'s own move loop (its header
        // comment): read before make_move() vacates the from-square,
        // threaded into this move's own negamax() children below as
        // their `prev_piece`/`prev_to`.
        const board::PieceType moved_piece = board::piece_type_of(pos.piece_at(move.from()));
        UndoInfo undo;
        board::make_move(pos, move, undo);

        // Check extensions (negamax()'s own header comment) apply
        // symmetrically at the root: a root move that gives check
        // deserves the same extra ply any other checking move in the
        // tree gets. `ply + 1 < kMaxPly` is always true here (ply is 0
        // at the root, `kMaxPly` far larger), but the guard is kept
        // identical to negamax()'s own for a single, consistently-
        // applied rule rather than a root-specific shortcut.
        const bool move_gives_check = in_check(pos);
        const int extension = (move_gives_check && 1 < kMaxPly) ? kCheckExtensionPly : 0;

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
            score = -negamax(pos, depth - 1 + extension, -beta, -alpha, 1, result.nodes, tt, killers,
                              history, cont_history, moved_piece, move.to(), game_history, path,
                              pawn_tt);
        } else {
            score = -negamax(pos, depth - 1 + extension, -alpha - 1, -alpha, 1, result.nodes, tt,
                              killers, history, cont_history, moved_piece, move.to(), game_history,
                              path, pawn_tt);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1 + extension, -beta, -alpha, 1, result.nodes, tt,
                                  killers, history, cont_history, moved_piece, move.to(), game_history,
                                  path, pawn_tt);
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
    // comment, which applies equally to KillerTable/HistoryTable/
    // ContinuationHistoryTable -- search/ordering.h). kDefaultTTSizeMB
    // is a placeholder until the UCI `Hash` option (ROADMAP.md Phase 8)
    // makes table size configurable and the tables themselves
    // persistent across calls.
    TranspositionTable tt(kDefaultTTSizeMB);
    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
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
    return search_root(pos, depth, -kInfinity, kInfinity, tt, killers, history, cont_history,
                        game_history, path, pawn_tt);
}

SearchResult search_iterative_deepening(Position& pos, int max_depth, int time_limit_ms,
                                         std::span<const std::uint64_t> game_history) {
    assert(max_depth >= 1 && "search_iterative_deepening: max_depth must be at least 1");

    const auto start_time = std::chrono::steady_clock::now();

    // Tables shared across every iteration of THIS call (see tt.h's
    // header comment) -- this cross-iteration sharing is where the
    // ordering/TT machinery's real value comes from right now, since
    // search_fixed_depth() on its own always starts from empty tables.
    // Unlike the TT (aged via new_search() each iteration), killers,
    // history, and continuation history are NOT reset between
    // iterations on purpose: a killer or a historically-good quiet move
    // (plain or continuation) from a shallower iteration is still a
    // reasonable ordering bet for the next, deeper one, and letting them
    // persist is exactly how real engines use iterative deepening to
    // make each successive iteration cheaper.
    TranspositionTable tt(kDefaultTTSizeMB);
    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
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
                                       cont_history, game_history, path, pawn_tt);
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
                                    cont_history, game_history, path, pawn_tt);

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
            next = search_root(pos, depth, -kInfinity, kInfinity, tt, killers, history, cont_history,
                                game_history, path, pawn_tt);
        }

        total_nodes += next.nodes;
        result = next;
    }

    result.nodes = total_nodes;
    return result;
}

} // namespace nightwing::search
