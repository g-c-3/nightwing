#pragma once
// src/search/ordering.h
//
// Move ordering: reorders a MoveList in place so that alpha-beta (and
// the transposition table's cutoffs) prune as many nodes as possible.
// Priority, following ROADMAP.md's Phase 3 "Move ordering" item as its
// starting point, since extended (this file's own per-class header
// comments below have the full account of each later addition):
//   1. The transposition-table move for this position, if any (search/tt.h)
//   2. Captures (including en passant and capture-promotions), scored
//      by MVV-LVA -- Most Valuable Victim, Least Valuable Attacker
//      (CPW "MVV-LVA") -- PLUS capture history (CaptureHistoryTable
//      below, this file's own header comment on it), a from-scratch
//      finer-grained tiebreak within MVV-LVA's own victim-value bands
//      (docs/DECISIONS.md has the external-review provenance for this
//      addition). NOTE: unlike its own header comment's original Phase
//      3 wording might suggest, plain Static Exchange Evaluation
//      (search/see.h) does NOT feed into ordering here -- SEE is used
//      by search.cpp's negamax() for shallow-depth bad-capture PRUNING
//      (skipping a losing capture outright, this file's own header
//      comment can't cover that -- see negamax()'s own header comment
//      in search.cpp) and by quiescence.cpp for its own bad-capture
//      pruning, but ordering here still relies on MVV-LVA + capture
//      history alone, not a genuine SEE score per move (which would
//      cost meaningfully more per node than this file's other, cheap
//      scoring signals, for a benefit ProbCut/the pruning use already
//      captures more directly).
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
///
/// Carries a MALUS as well as a bonus (CPW "History Heuristic" /
/// "Relative History Heuristic" literature's common "history gravity"
/// extension): every quiet move searched-and-rejected at a node that
/// later fails high on a DIFFERENT move is not just "no evidence
/// either way" -- it is active evidence this specific move was, at
/// least this once, worse than the move that did cut off. Without a
/// malus, `update()` alone can only ever ratchet a score upward, so a
/// move that hit near `kHistoryMax` once from a single lucky cutoff
/// stays there indefinitely regardless of how many later searches
/// reject it -- update()-only history rewards but never *corrects*,
/// which is exactly what malus() exists to fix. A score can therefore
/// now be negative; `score()`'s callers (ordering.cpp's score_move(),
/// search.cpp's history-pruning check) already treat it as a plain
/// signed int with no assumption it's non-negative, so no caller-side
/// change was needed for this.
class HistoryTable {
public:
    /// Adds a depth-weighted bonus for `move` (by `color`) having
    /// caused a beta cutoff at `depth`. Clamped at kHistoryMax so no
    /// single move's score can grow unbounded across a long search.
    void update(board::Color color, board::Move move, int depth) noexcept;

    /// Subtracts a depth-weighted penalty for `move` (by `color`)
    /// having been searched, but NOT caused the beta cutoff, at a node
    /// where some other move at the same depth did. Floored at
    /// `-kHistoryMax` (the mirror image of `update()`'s own ceiling) so
    /// a move that's repeatedly rejected can't drive its score to an
    /// unbounded negative that would then take an equally unbounded
    /// number of future cutoffs to recover from.
    void malus(board::Color color, board::Move move, int depth) noexcept;

    /// Returns the current history score for `move` by `color` (0 if
    /// never recorded; may be negative -- see this class's own header
    /// comment on malus()).
    [[nodiscard]] int score(board::Color color, board::Move move) const noexcept;

private:
    /// Symmetric upper/lower bound on any single table entry --
    /// prevents overflow in both directions and keeps one very-
    /// frequently-cutting-off (or very-frequently-rejected) move from
    /// permanently swamping ordering ahead of other, also-relevant
    /// moves found later in the same search. Chosen to sit comfortably
    /// below the killer-move score band in ordering.cpp's score_move()
    /// so history alone can never accidentally outrank a killer.
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

    /// Subtracts a depth-weighted penalty for `move` (`piece` moved to
    /// `to`) having been searched, but NOT caused the beta cutoff, as a
    /// reply to `prev_piece` moving to `prev_to`, at a node where some
    /// other move at the same depth did -- the same "malus alongside
    /// bonus" rationale as HistoryTable::malus() (ordering.h), applied
    /// to this table's own [prev_piece][prev_to][piece][to] key instead
    /// of HistoryTable's [color][from][to]. No-op if `prev_piece` is
    /// `board::PieceType::None`, same as update().
    void malus(board::PieceType prev_piece, board::Square prev_to, board::PieceType piece,
               board::Square to, int depth) noexcept;

    /// Returns the current continuation-history score, or 0 if
    /// `prev_piece` is `board::PieceType::None` or the combination has
    /// never been recorded. May be negative -- see malus()'s own
    /// comment above.
    [[nodiscard]] int score(board::PieceType prev_piece, board::Square prev_to,
                             board::PieceType piece, board::Square to) const noexcept;

private:
    /// Same symmetric cap, and the same rationale, as
    /// HistoryTable::kHistoryMax above -- comfortably below the
    /// killer-move score band in ordering.cpp's score_move() even after
    /// being added to a plain history score there.
    static constexpr int kContinuationHistoryMax = 8192;

    std::array<std::array<std::array<std::array<int, board::kNumSquares>, board::kNumPieceTypes>,
                          board::kNumSquares>,
               board::kNumPieceTypes>
        table_{};
};

/// Capture history: how often a CAPTURE (by attacking piece type and
/// captured/victim piece type -- NOT square, color, or specific move)
/// has caused a beta cutoff, weighted by the depth at which it did --
/// same depth-squared weighting, and the same bonus-plus-malus
/// ("history gravity") design, as HistoryTable/ContinuationHistoryTable
/// above, applied to captures instead of quiet moves. External-review
/// provenance (docs/DECISIONS.md, 2026-09-08): "a separate capture
/// history table (keyed on capturing/captured piece type) for finer-
/// grained capture ordering beyond MVV-LVA/SEE."
///
/// Distinct from, and additive with, MVV-LVA (ordering.cpp's
/// mvv_lva_score()): MVV-LVA is a fixed, purely material-value-based
/// formula -- it can never learn that, say, "Knight captures Bishop"
/// has tended to work out better THIS SEARCH than "Bishop captures
/// Knight" despite both trading equal material (MVV-LVA scores them
/// identically, since victim/attacker values alone don't distinguish
/// them the way piece-type-specific outcomes might). Capture history
/// is the same kind of empirical, search-local correction for captures
/// that HistoryTable already provides for quiet moves -- ordering.cpp's
/// score_move() adds this table's score on top of the MVV-LVA base for
/// every capture, the same "two independent signals, summed" pattern
/// already used for quiet moves (plain history plus continuation
/// history).
///
/// Indexed by ATTACKER and VICTIM piece type only -- not square, not
/// color, not the specific move -- deliberately coarser than
/// HistoryTable's own [color][from][to] granularity: capture quality is
/// primarily a function of WHAT was captured by WHAT, far more than
/// where on the board it happened, and a coarser key means far more
/// searches contribute evidence to the same cell rather than the table
/// staying mostly empty at typical search-tree sizes. En passant is
/// scored as a Pawn-attacker-Pawn-victim capture -- the same special-
/// case mvv_lva_score() (ordering.cpp) already applies, since the
/// captured pawn isn't the piece actually sitting on the move's own
/// `to` square. Scoped like every other per-search table above: one
/// instance per top-level search call.
class CaptureHistoryTable {
public:
    /// Adds a depth-weighted bonus for an `attacker`-captures-`victim`
    /// move having caused a beta cutoff at `depth`. Clamped at
    /// kCaptureHistoryMax, the same rationale as HistoryTable's own
    /// ceiling.
    void update(board::PieceType attacker, board::PieceType victim, int depth) noexcept;

    /// Subtracts a depth-weighted penalty for an `attacker`-captures-
    /// `victim` move having been searched, but NOT caused the beta
    /// cutoff, at a node where some other capture at the same depth
    /// did. Floored at `-kCaptureHistoryMax` -- same rationale as
    /// HistoryTable::malus().
    void malus(board::PieceType attacker, board::PieceType victim, int depth) noexcept;

    /// Returns the current capture-history score for this
    /// attacker/victim pair (0 if never recorded; may be negative --
    /// see malus()'s own comment above).
    [[nodiscard]] int score(board::PieceType attacker, board::PieceType victim) const noexcept;

private:
    /// Same symmetric cap, and the same rationale, as
    /// HistoryTable::kHistoryMax -- chosen to sit comfortably within
    /// the capture score band's own headroom in ordering.cpp's
    /// score_move() (that band's own comment has the exact numbers) so
    /// this can meaningfully re-rank captures that share an MVV-LVA
    /// score without ever letting a bad-victim capture outrank a
    /// good-victim one purely on accumulated capture history.
    static constexpr int kCaptureHistoryMax = 8192;

    std::array<std::array<int, board::kNumPieceTypes>, board::kNumPieceTypes> table_{};
};

/// Correction history (ROADMAP.md's "Correction history" item, Priority
/// Fixes (2026-09-08) section; a newer technique with no CPW article of
/// its own -- popularized by Stockfish's own more recent search code,
/// described there and on its own public wiki rather than CPW; this is
/// a from-scratch, deliberately simpler implementation, no code
/// copied): a running, per-pawn-structure estimate of how far off
/// eval::evaluate()'s own static eval tends to be from this node's own
/// REAL (searched, not just statically evaluated) result, used to nudge
/// static eval toward the historically more-accurate value before
/// search.cpp's own static-eval-driven pruning (RFP, razoring,
/// futility) and its "improving" flag use it.
///
/// Indexed by [color][pawn-only Zobrist key, masked] -- board::
/// compute_pawn_hash() (board/zobrist.h), the SAME key eval/pawn_tt.h's
/// own PawnHashTable uses, for the identical reason given there: pawn
/// structure changes far less often move-to-move than the rest of the
/// position, so a correction keyed on it is meaningfully reusable
/// across many search nodes that share a pawn structure even when
/// nothing else about the position matches. Indexed by color too, not
/// just pawn key, matching HistoryTable's own convention above -- even
/// though eval::evaluate()'s own White/Black symmetry SHOULD make a
/// shared, color-independent table mathematically sound on its own,
/// indexing by color anyway costs one array dimension and guards
/// against any residual eval asymmetry (tempo, contempt) silently
/// corrupting the correction for one side using data really only
/// observed for the other.
///
/// Deliberately simpler than a full Stockfish-style correction history
/// (no separate non-pawn/material/minor-piece tables, no per-move-count
/// decay weighting) -- one pawn-structure table only, matching
/// ROADMAP.md's own item text ("optionally per-piece-type" -- left for
/// a future session to add if warranted). A plain bounded exponential
/// moving average (kCorrectionWeight below), the same "simplest
/// defensible first draft, not yet tuned" level of sophistication as
/// this file's own HistoryTable/ContinuationHistoryTable/
/// CaptureHistoryTable above, rather than Stockfish's own more elaborate
/// gravity-style formula. Scoped like every other per-search table
/// above: one instance per top-level search call.
class CorrectionHistoryTable {
public:
    /// Nudges this pawn structure's own stored correction toward
    /// `error` (a node's own real searched result minus its static
    /// eval, both side-to-move-relative -- search.cpp's own call site
    /// has the exact values used) by 1/kCorrectionWeight of the
    /// remaining distance -- a standard bounded exponential moving
    /// average, not a full replacement, so one unusual node can't
    /// overwrite many prior samples' worth of signal in a single
    /// update. `error` is clamped to +/-kCorrectionMax BEFORE the
    /// update (not just the stored result after) so a single wild
    /// outlier (a missed tactic, far outside normal positional eval
    /// error) can't drag the moving average itself toward an equally
    /// wild value even temporarily -- and, as a consequence of clamping
    /// the input to every update rather than the output of just one,
    /// the stored correction itself can never leave that same range
    /// either (a weighted average of values already within
    /// +/-kCorrectionMax can't land outside it).
    void update(board::Color us, std::uint64_t pawn_key, int error) noexcept;

    /// Returns the current correction for this pawn structure (0 if
    /// never recorded) -- ADD this to a raw static eval, don't replace
    /// it; this table only ever tracks a small nudge, not a substitute
    /// evaluation.
    [[nodiscard]] int correction(board::Color us, std::uint64_t pawn_key) const noexcept;

private:
    /// Power-of-2 so `& (kCorrectionTableSize - 1)` is a valid, cheap
    /// mask -- see search/tt.h's identical sizing convention. Fixed at
    /// compile time (not a constructor `size_kb` parameter like
    /// TranspositionTable/PawnHashTable) -- matching HistoryTable/
    /// CaptureHistoryTable's own "heuristic ordering/pruning-adjustment
    /// table, not a position cache" sizing convention above, not the
    /// "large, tuned runtime capacity" convention search/tt.h and
    /// eval/pawn_tt.h both use for genuine position caches.
    static constexpr std::size_t kCorrectionTableSize = 16384;
    /// Symmetric bound on both a single update's own `error` input AND
    /// (as a consequence -- see update()'s own doc comment) the stored
    /// correction itself: kept modest (a fraction of a pawn) since this
    /// is meant to be a small nudge to an already-reasonable eval, not
    /// a second evaluation function running in parallel.
    static constexpr int kCorrectionMax = 256;
    /// Divisor for update()'s own moving-average step -- how many
    /// roughly-equal-error samples it takes to converge close to a new
    /// steady-state correction. Not yet tuned, same caveat as every
    /// other not-yet-SPRT-validated constant in this file.
    static constexpr int kCorrectionWeight = 32;

    std::array<std::array<int, kCorrectionTableSize>, board::kNumColors> table_{};
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
/// scoring, same as if the table were empty. `capture_history` is
/// looked up by each capture's own attacker/victim piece type (this
/// file's own CaptureHistoryTable header comment) -- unlike
/// `cont_history`, it needs no null-context sentinel, since every
/// capture always has a genuine attacker and victim to key on.
void order_moves(board::MoveList& moves, const board::Position& pos, board::Move tt_move,
                  const KillerTable& killers, int ply, const HistoryTable& history,
                  const ContinuationHistoryTable& cont_history,
                  const CaptureHistoryTable& capture_history, board::PieceType prev_piece,
                  board::Square prev_to) noexcept;

} // namespace nightwing::search
