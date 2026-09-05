// src/uci/uci.cpp
//
// Deliberately out of scope for Phase 2's "basic UCI loop" (revisit
// later phases, tracked informally here rather than duplicated across
// every function that touches it):
//   - setoption / engine options: `Threads` (added Session 74,
//     ROADMAP.md Phase 7's "Thread count UCI option" item), `Hash`,
//     `Move Overhead`, and `MultiPV` (all three added under Phase 8's
//     "Full UCI option set" item -- see handle_setoption() below) are
//     all recognized; "Full UCI option set" is now fully done. `Hash`
//     changes the SIZE of the fresh, private TranspositionTable each
//     top-level search call still constructs for itself, not an
//     in-place resize of a persistent one -- the TT itself is still
//     scoped per top-level search call rather than a persistent global
//     (search/tt.h's own LIFETIME NOTE); making it genuinely persistent,
//     with a `ucinewgame`-triggered clear, remains a separate, not-yet-
//     started piece of that same Phase 8 item. `MultiPV` uses classic
//     root-move exclusion (search::search_iterative_deepening_multipv(),
//     search.cpp) with two deliberate first-draft simplifications, both
//     documented at that function's own definition: no aspiration
//     windows and no Lazy SMP when it genuinely takes effect. `Ponder`
//     (the option ADVERTISEMENT, distinct from pondering ITSELF, which
//     is already fully implemented below) is ROADMAP.md's own next,
//     separate Phase 8 item ("Pondering — protocol side"), not part of
//     "Full UCI option set" 's own item text -- still open.
//   - True asynchronous `go infinite` + `stop` FOR AN ORDINARY (non-
//     ponder) `go`: still not attempted. A plain `go` (with or without
//     a time control) still always runs synchronously to completion (or
//     until its own internal deadline, search.h's SearchLimits) before
//     this loop reads its next line; a `stop` sent while an ordinary
//     `go` is in flight is parsed but has no effect, since by the time
//     it could arrive on `in`, `go` has already finished and printed
//     `bestmove`. `go infinite` (no depth/time control at all) falls
//     back to this file's own kNoTimeControlDepth, same as a bare `go`
//     — it does NOT actually run unboundedly the way the UCI spec's own
//     "infinite" framing implies, since nothing besides an async `stop`
//     could ever end it, and that doesn't exist for this path.
//   - Pondering (`go ponder`, `ponderhit`, and `stop` while pondering)
//     IS implemented (ROADMAP.md Phase 7) — see start_pondering()/
//     handle_ponderhit()/handle_stop() below and docs/DECISIONS.md for
//     the full design. This is the one place a background search
//     thread and an externally-arriving `stop`/`ponderhit` genuinely
//     coexist with this loop's own synchronous command reading right
//     now — it does not generalize to ordinary `go`/`stop` above,
//     which remain deliberately out of scope per this item's own
//     narrower ROADMAP.md framing ("search side: handle `go ponder`...").
//
// `go` now also consults src/book/book.h's small curated opening book
// FIRST, before any of the above depth/time-control logic even runs
// (handle_go(), below) -- ROADMAP.md's optional "small curated opening
// book" item. This one has no `setoption`-driven toggle either (an
// "OwnBook"-style option was considered alongside `Threads` above and
// deferred -- see docs/DECISIONS.md, 2026-09-03 (4)) -- book usage stays
// simply always on; see book.h's own header comment for why an opening
// book, unlike a tablebase, doesn't need one to stay consistent with
// this project's hard no-tablebase constraint.

#include "uci/uci.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "board/board.h"
#include "board/fen.h"
#include "board/movegen.h"
#include "board/move.h"
#include "book/book.h"
#include "search/search.h"

namespace nightwing::uci {
namespace {

using board::Move;
using board::MoveList;
using board::Position;

/// Splits a whitespace-separated command line into tokens.
[[nodiscard]] std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

/// Applies each UCI long-algebraic move string in `move_tokens` (e.g.
/// "e2e4", "e7e8q") to `pos` in order, by generating the legal move list
/// at each step and matching by Move::to_uci() string — this is how
/// promotion/castling/en-passant flags end up set correctly without
/// duplicating Move's own encoding logic here. Stops silently at the
/// first token that doesn't match any legal move (rather than throwing)
/// — a GUI or script sending a slightly malformed move list shouldn't
/// crash the engine, per the UCI spec's general robustness expectation.
///
/// `history` receives `pos.zobrist_hash` immediately before each
/// successfully-applied move — i.e. on return, `history` holds every
/// ancestor position's hash strictly before the final `pos`, oldest to
/// newest, matching search::search_iterative_deepening()'s
/// `game_history` parameter exactly (search/search.h's doc comment) —
/// so repetition detection (ROADMAP.md Phase 3) sees the real game's
/// history, not just whatever the search recalculates within its own
/// tree. Caller is responsible for clearing `history` first when a
/// `position` command should start a fresh line rather than extend the
/// previous one (see handle_position() below).
void apply_uci_moves(Position& pos, const std::vector<std::string>& move_tokens,
                      std::vector<std::uint64_t>& history) {
    for (const std::string& token : move_tokens) {
        MoveList legal;
        board::generate_legal_moves(pos, legal);

        bool matched = false;
        for (int i = 0; i < legal.size(); ++i) {
            if (legal[i].to_uci() == token) {
                history.push_back(pos.zobrist_hash); // pre-move position becomes an ancestor
                board::UndoInfo undo; // discarded — the position isn't unwound afterward.
                board::make_move(pos, legal[i], undo);
                matched = true;
                break;
            }
        }
        if (!matched) {
            break;
        }
    }
}

/// Handles `position [startpos | fen <fen>] [moves <m1> <m2> ...]`.
/// Malformed input (missing startpos/fen, an unparseable FEN) is
/// ignored, leaving `pos` unchanged, rather than throwing — same
/// robustness rationale as apply_uci_moves() above.
///
/// `history` is cleared unconditionally at the top: a `position` command
/// always fully (re)specifies the game from `startpos`/`fen`, exactly
/// the same way it fully (re)specifies `pos` itself rather than
/// incrementally patching the previous one — a GUI resends the entire
/// move list on every `position` command, so accumulating history
/// across calls instead of rebuilding it here would double-count moves
/// already reflected in the resent list.
void handle_position(Position& pos, std::vector<std::uint64_t>& history,
                      const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        return;
    }

    history.clear();

    std::size_t idx = 1;
    if (tokens[idx] == "startpos") {
        pos = board::start_position();
        ++idx;
    } else if (tokens[idx] == "fen") {
        ++idx;
        std::string fen;
        while (idx < tokens.size() && tokens[idx] != "moves") {
            if (!fen.empty()) {
                fen += ' ';
            }
            fen += tokens[idx];
            ++idx;
        }
        try {
            pos = board::parse_fen(fen);
        } catch (const std::invalid_argument&) {
            return;
        }
    } else {
        return;
    }

    if (idx < tokens.size() && tokens[idx] == "moves") {
        ++idx;
        const std::vector<std::string> move_tokens(tokens.begin() + static_cast<long>(idx),
                                                     tokens.end());
        apply_uci_moves(pos, move_tokens, history);
    }
}

/// Search-depth ceiling used only when a real time budget (movetime, or
/// wtime/btime) is what's actually expected to stop the search — high
/// enough that Phase 2's un-pruned negamax will never realistically
/// reach it (branching factor ~35 with no move ordering means each
/// additional ply is roughly an order of magnitude slower — see
/// DECISIONS.md's empirical timings), so this is a safety ceiling, not
/// a target.
constexpr int kTimedSearchMaxDepth = 64;

/// Fallback search depth when `go` specifies neither an explicit depth
/// nor any usable time control (bare `go`, `go infinite`, or anything
/// else this loop doesn't recognize) — deliberately small and fixed
/// rather than kTimedSearchMaxDepth, precisely because nothing would
/// otherwise stop the search: Phase 2 has no pruning yet, so an
/// unbounded depth ceiling with no time limit backing it up would hang
/// the engine on a bare `go`. Empirically ~127ms in a Release build on
/// the starting position (see DECISIONS.md) — fast enough to never be
/// the bottleneck in practice, and low enough to stay fast even under a
/// much slower Debug/sanitizer build.
constexpr int kNoTimeControlDepth = 5;

/// Bounds for the `Threads` UCI option (ROADMAP.md Phase 7, "Thread
/// count UCI option") -- passed straight through as
/// search::search_iterative_deepening()'s own `num_threads` parameter
/// (search/search.h's doc comment on that parameter has the full Lazy
/// SMP contract; search.cpp Sessions 71-73 for the implementation and a
/// macOS-specific stack-overflow bugfix along the way). `kMinThreads`
/// (1) matches search_iterative_deepening()'s own default and its
/// "values <= 1 behave exactly as before this parameter existed"
/// guarantee -- setting `Threads` to 1 (or never touching it at all)
/// is bit-for-bit today's single-threaded behavior. `kMaxThreads`
/// (1024) isn't a technical ceiling this engine's own implementation
/// imposes (Lazy SMP helper threads are plain, unpooled std::thread
/// instances, so nothing internally caps their count) -- it mirrors
/// the same widely-used `Threads` option upper bound published UCI
/// engines (e.g. Stockfish) commonly ship, purely as a sanity guard
/// against a malformed or accidental `setoption ... value 999999999`
/// spawning a number of threads no real machine could usefully run.
constexpr int kMinThreads = 1;
constexpr int kMaxThreads = 1024;

/// Bounds for the `Hash` UCI option (ROADMAP.md Phase 8, "Full UCI
/// option set"), in megabytes -- passed straight through as
/// search::search_iterative_deepening()'s/search::search_fixed_depth()'s
/// own `hash_size_mb` parameter (search/search.h's doc comments on that
/// parameter), which sizes the fresh, private TranspositionTable each
/// top-level search call still constructs for itself (tt.h's own
/// LIFETIME NOTE -- this option changes the size of each such table,
/// not an in-place resize of a persistent one, since that part of
/// tt.h's own eventual design is still a separate, unstarted piece of
/// work). `kMinHashMB` (1) matches TranspositionTable's own constructor
/// doc comment ("`size_mb` too small for even one bucket constructs a
/// minimum 1-bucket table... rather than an unusable empty one" --
/// tt.h), so 1 is a genuinely usable floor, not merely the smallest
/// integer accepted.
///
/// `kMaxHashMB` (2048, i.e. 2 GiB) was originally 65536 (64 GiB) --
/// lowered after real CI evidence (GitHub Actions run 91820797115)
/// showed that a large-but-in-bounds value doesn't just risk an
/// allocation that's slow or wasteful, it can fail in ways this
/// codebase has NO way to recover from at all, regardless of how
/// careful the recovery code is: on Linux Debug, ASan's allocator
/// defaults to `allocator_may_return_null=0`, so a failed allocation
/// under ASan calls its own `Die()` and aborts the process directly --
/// it never throws std::bad_alloc, so search.cpp's own
/// make_transposition_table() fallback (which only catches
/// std::bad_alloc) never even gets a chance to run. On macOS
/// Debug/Release, the request instead triggered the OS's own
/// out-of-memory killer, sending an uncatchable SIGKILL, 157 sec and
/// 32 sec respectively after the search started -- also never a
/// catchable C++ exception. Both failure modes are a direct
/// consequence of `kMaxHashMB` having been set to a value with no
/// real relationship to what any actual machine (least of all a
/// shared CI runner also running ASan's own shadow-memory overhead)
/// can reliably satisfy -- "sanity ceiling, not a real technical
/// limit" (this constant's own framing before this fix, mirrored from
/// kMaxThreads below) was true of the NUMBER chosen but not of its
/// actual real-world safety. 2048 MB is comfortably below the RAM on
/// every GitHub Actions runner class this project's CI matrix uses
/// (.github/workflows/ci.yml), even accounting for ASan's shadow-
/// memory overhead (roughly +12.5%) and normal OS/other-process
/// headroom, while still being generous for actual engine use --
/// most classical (non-NNUE) engines run comfortably with a small
/// fraction of this even at serious tournament time controls.
/// search.cpp's make_transposition_table() fallback is kept as
/// defense-in-depth for a genuinely std::bad_alloc-throwing failure
/// (a real, if now much rarer, possibility e.g. on a small embedded
/// target or a Release/non-ASan build where malloc failure is
/// reported normally) -- but per the CI evidence above, it is NOT a
/// substitute for keeping this ceiling itself realistic, since under
/// ASan specifically it cannot run at all.
constexpr int kMinHashMB = 1;
constexpr int kMaxHashMB = 2048;

/// Bounds for the `Move Overhead` UCI option (ROADMAP.md Phase 8, "Full
/// UCI option set"), in milliseconds -- a fixed safety margin subtracted
/// from every positive computed time budget (compute_search_budget()
/// below) before it's handed to the search, so that GUI/network/
/// protocol latency between this engine deciding to stop and its
/// `bestmove` actually reaching the GUI doesn't itself cause a real
/// clock overrun on a tightly-timed game. Applied uniformly to both an
/// explicit `movetime` and a `wtime`/`btime`-derived budget (see
/// compute_search_budget()'s own comment for why this is simpler, and
/// no less defensible, than the narrower "wtime/btime only" convention
/// some published engines use) -- deliberately NOT applied when there's
/// no real time budget at all (`time_limit_ms == 0`, i.e. a bare `go`/
/// `go depth N` with no time control), since there's nothing to trim a
/// margin off of there. Default 0 (kMinMoveOverheadMs) preserves every
/// pre-existing caller's exact behavior until a GUI/tournament manager
/// explicitly requests a margin. `kMaxMoveOverheadMs` (5000ms) is a
/// sanity ceiling, same rationale as kMaxThreads/kMaxHashMB above.
constexpr int kMinMoveOverheadMs = 0;
constexpr int kMaxMoveOverheadMs = 5000;

/// Bounds for the `MultiPV` UCI option (ROADMAP.md Phase 8, "Full UCI
/// option set" -- the sub-item this fixes, completing that bullet's
/// checklist entirely). `kMinMultiPV` (1) is the ordinary, single-line
/// behavior every pre-existing `go` already used before this option
/// existed -- passed straight through as
/// search::search_iterative_deepening()'s own `multi_pv` parameter
/// (search/search.h's doc comment on that parameter has the full
/// contract, including that `multi_pv <= 1` runs the exact same
/// single-line code path as before this option existed, byte for byte).
/// `kMaxMultiPV` (256) is a sanity ceiling, same "guard against a
/// malformed/oversized request, not a real technical limit" framing as
/// kMaxThreads/kMaxHashMB above -- chosen well above any position's
/// realistic legal-root-move count (chess has at most 218 legal moves
/// from any single position, a well-known figure -- CPW "Chess Position
/// with the most legal moves" -- so 256 already exceeds every position
/// that could ever occur) while still being small enough that
/// requesting the max never risks the kind of large-allocation problem
/// `kMaxHashMB` ran into (docs/DECISIONS.md, 2026-09-05): MultiPV's own
/// cost scales with the number of LINES searched, not a memory
/// allocation size, and search_iterative_deepening() itself already
/// caps the actually-used line count at the root's own real legal move
/// count regardless of what this option requests (that function's own
/// doc comment, search.h), so a value above the true legal-move count
/// is inherently harmless, just wasted -- 256 is simply a courtesy
/// ceiling against an obviously-malformed request, not a value chosen
/// to prevent any specific failure mode the way kMaxHashMB was.
constexpr int kMinMultiPV = 1;
constexpr int kMaxMultiPV = 256;

/// Simple time allocation for `go wtime/btime/winc/binc` — no
/// movestogo-aware tuning yet (see DECISIONS.md): budgets
/// remaining_ms / 20 plus the increment, capped at remaining_ms / 2 so
/// a single move can never claim more than half of what's left (a
/// guard against starving later moves when remaining_ms is already
/// low).
[[nodiscard]] int allocate_time_ms(int remaining_ms, int increment_ms) noexcept {
    if (remaining_ms <= 0) {
        return 50; // Effectively "as little as possible, but not zero."
    }
    int budget = remaining_ms / 20 + increment_ms;
    const int cap = remaining_ms / 2;
    if (budget > cap) {
        budget = cap;
    }
    if (budget < 1) {
        budget = 1;
    }
    return budget;
}

/// Result of parsing `go`'s own depth/time-control tokens (used both by
/// handle_go() for an ordinary `go`, and by start_pondering() below to
/// save the SAME budget a non-ponder `go` from this position would have
/// used, for later reuse once `ponderhit` arrives — see
/// start_pondering()'s own doc comment).
struct SearchBudget {
    int max_depth = 0;
    /// 0 means "no real time budget" (an explicit `depth N` with no
    /// `movetime`, or no time control/depth at all — kNoTimeControlDepth
    /// applies to `max_depth` in that case instead). A positive value is
    /// a genuine millisecond budget, whether from `movetime` directly or
    /// derived from `wtime`/`btime`/`winc`/`binc` via allocate_time_ms().
    int time_limit_ms = 0;
};

/// Parses `go`'s own `[depth N] [movetime N] [wtime W btime B [winc I]
/// [binc I]]` tokens (any combination; unrecognized sub-options like
/// `movestogo`/`infinite`/`ponder`/`mate`/`nodes` are ignored here too,
/// same as handle_go()'s own former inline version of this logic) into
/// a concrete depth ceiling and millisecond budget — extracted into its
/// own function so start_pondering() can compute and SAVE the same
/// budget an ordinary (non-ponder) `go` from this exact position would
/// have used, without actually applying it until `ponderhit` (see that
/// function's own doc comment for why the budget can't just be applied
/// immediately the way it is here).
///
/// `move_overhead_ms` (ROADMAP.md Phase 8, "Full UCI option set" --
/// `Move Overhead`, this file's own kMinMoveOverheadMs/kMaxMoveOverheadMs
/// doc comment above has the full rationale): subtracted from whatever
/// positive `time_limit_ms` this function would otherwise return, for
/// BOTH the `movetime` branch and the `wtime`/`btime`-derived branch --
/// floored at 1ms (never 0 or negative, which would mean "no limit" to
/// every caller of this budget, the opposite of what a safety margin is
/// for) so an overhead value close to or exceeding the raw computed
/// budget still leaves the search a minimal, real amount of time rather
/// than silently reverting to unbounded. Left untouched when
/// `time_limit_ms` is already 0 (no real time budget at all -- nothing
/// to trim a margin off of).
[[nodiscard]] SearchBudget compute_search_budget(const Position& pos,
                                                   const std::vector<std::string>& tokens,
                                                   int move_overhead_ms = 0) {
    SearchBudget budget;
    bool have_depth = false;
    bool have_movetime = false;
    int wtime = -1;
    int btime = -1;
    int winc = 0;
    int binc = 0;

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& tok = tokens[i];

        auto next_int = [&]() -> int {
            if (i + 1 < tokens.size()) {
                try {
                    return std::stoi(tokens[++i]);
                } catch (const std::exception&) {
                    return 0;
                }
            }
            return 0;
        };

        if (tok == "depth") {
            budget.max_depth = next_int();
            have_depth = true;
        } else if (tok == "movetime") {
            budget.time_limit_ms = next_int();
            have_movetime = true;
        } else if (tok == "wtime") {
            wtime = next_int();
        } else if (tok == "btime") {
            btime = next_int();
        } else if (tok == "winc") {
            winc = next_int();
        } else if (tok == "binc") {
            binc = next_int();
        }
    }

    if (have_depth) {
        if (budget.max_depth < 1) {
            budget.max_depth = kNoTimeControlDepth; // Guard against a malformed "depth 0"/negative value.
        }
        // time_limit_ms is whatever movetime gave (possibly 0, i.e. no limit).
    } else if (have_movetime) {
        budget.max_depth = kTimedSearchMaxDepth;
        // time_limit_ms is already set from movetime above.
    } else {
        const bool white_to_move = pos.side_to_move == board::Color::White;
        const int remaining = white_to_move ? wtime : btime;
        if (remaining >= 0) {
            const int increment = white_to_move ? winc : binc;
            budget.time_limit_ms = allocate_time_ms(remaining, increment);
            budget.max_depth = kTimedSearchMaxDepth;
        } else {
            budget.max_depth = kNoTimeControlDepth;
            budget.time_limit_ms = 0;
        }
    }

    if (budget.time_limit_ms > 0) {
        budget.time_limit_ms -= move_overhead_ms;
        if (budget.time_limit_ms < 1) {
            budget.time_limit_ms = 1;
        }
    }
    return budget;
}

/// Formats and writes one `info depth ... score cp/mate ... nodes ...
/// pv ...` line to `out` for one completed search::search_iterative_
/// deepening() iteration -- meant to be passed as that function's
/// IterationCallback (search/search.h) so a GUI/tournament manager sees
/// live progress per iteration, not just the final `bestmove` (the
/// external code review's second Priority Fix -- docs/ROADMAP.md,
/// docs/DECISIONS.md 2026-08-26).
///
/// Score is reported as `mate <N>` (N = full moves to mate, this
/// engine's own perspective -- positive means THIS engine delivers it,
/// negative means it gets mated) rather than `cp <N>` whenever
/// |result.score| reaches search::kMateThreshold, per the UCI spec's
/// own distinction between the two -- search.h's kMateThreshold doc
/// comment already anticipates exactly this use. The plies-to-moves
/// conversion (`(plies_to_mate + 1) / 2`) is the standard UCI rounding
/// (CPW/common engine practice): a mate deliverable on the very next
/// move (1 ply from the root) reports as `mate 1`, not `mate 0`.
///
/// `pv` is emitted move-by-move via Move::to_uci(). An empty
/// `result.pv` (SearchResult's own doc comment: can happen if a TT
/// entry needed to extend it was evicted before this call, though
/// `best_move` itself is never null for a genuinely completed
/// iteration) still emits a `pv` field containing exactly `best_move`
/// alone, so every `info` line names at least the one move its score
/// applies to, never a bare `pv` with nothing after it.
///
/// `multipv <result.multipv_index>` (ROADMAP.md Phase 8, "Full UCI
/// option set" -- the `MultiPV` sub-item) is ALWAYS included, right
/// after `depth`, even when the `MultiPV` option is left at its
/// default of 1 -- `multipv_index` itself already defaults to 1 for
/// every ordinary, non-MultiPV SearchResult (search.h's own doc
/// comment on that field), so this costs nothing to always emit and
/// matches the convention several established engines (Stockfish among
/// them) already follow of always including the token rather than only
/// when MultiPV > 1. handle_go() (below) calls this function once per
/// line reported by a MultiPV search's `on_iteration` (each with its
/// own correct `multipv_index` already set by
/// search_iterative_deepening_multipv(), search.cpp) exactly as it
/// already calls it once per depth for an ordinary single-line search
/// -- this function itself needs no MultiPV-specific branching at all,
/// since every field it reads already carries the right per-line
/// values regardless of which path produced them.
void emit_info(const search::SearchResult& result, std::ostream& out) {
    out << "info depth " << result.depth_completed << " multipv " << result.multipv_index
        << " score ";
    if (result.score >= search::kMateThreshold) {
        const int plies_to_mate = search::kMateScore - result.score;
        out << "mate " << (plies_to_mate + 1) / 2;
    } else if (result.score <= -search::kMateThreshold) {
        const int plies_to_mate = search::kMateScore + result.score;
        out << "mate " << -((plies_to_mate + 1) / 2);
    } else {
        out << "cp " << result.score;
    }
    out << " nodes " << result.nodes << " pv";
    if (result.pv.empty()) {
        out << ' ' << result.best_move.to_uci();
    } else {
        for (const Move& move : result.pv) {
            out << ' ' << move.to_uci();
        }
    }
    out << '\n';
    out.flush();
}

/// Handles `setoption name <name...> value <value...>`. Recognized
/// option names with real behavioral effect, as of ROADMAP.md Phase 8's
/// "Full UCI option set" item (now complete -- `MultiPV` was the last
/// piece): `Threads` (ROADMAP.md Phase 7, unchanged from Session 74),
/// `Hash`, `Move Overhead`, and `MultiPV` (kMinHashMB/kMaxHashMB,
/// kMinMoveOverheadMs/kMaxMoveOverheadMs, and kMinMultiPV/kMaxMultiPV's
/// own doc comments above have each option's full rationale). `Ponder`
/// (ROADMAP.md Phase 8, "Pondering — protocol side") is also
/// RECOGNIZED, in the sense that it's advertised in the `uci` response
/// above and accepted here without complaint, but deliberately has NO
/// behavioral branch of its own below -- see that option's own
/// advertisement comment above for why: pondering support is
/// unconditional (already fully implemented, Session 75/76), so there's
/// nothing for this specific value to gate. Every OTHER, truly
/// unrecognized name is silently ignored, matching this file's
/// established robustness convention (this file's
/// header comment; run()'s own trailing comment on unrecognized
/// commands generally) rather than treating an unknown option as an
/// error. `name`/`value` are matched positionally (the LAST `name`/
/// `value` token pair in the line, per the UCI spec's own grammar
/// allowing either to be multi-word) rather than assuming fixed token
/// indices -- `Move Overhead` is itself the first genuinely multi-word
/// option name this function has needed to recognize, so this generality
/// (already present before this item, for forward-compatibility) now
/// has a real, exercised use, not just a hypothetical one. A malformed
/// line (`name`/`value` in the wrong order, `value` missing entirely, a
/// non-integer value) is ignored, leaving every option at whatever it
/// was before -- same robustness rationale as handle_position()/
/// apply_uci_moves() above: a slightly malformed setoption from a
/// GUI or script shouldn't crash the engine or corrupt otherwise-good
/// prior state.
void handle_setoption(int& num_threads, std::size_t& hash_size_mb, int& move_overhead_ms,
                       int& multi_pv, const std::vector<std::string>& tokens) {
    std::size_t name_start = 0;
    std::size_t name_end = 0;
    std::size_t value_start = 0;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i] == "name") {
            name_start = i + 1;
        } else if (tokens[i] == "value") {
            name_end = i;
            value_start = i + 1;
        }
    }
    if (name_start == 0 || value_start == 0 || name_end <= name_start ||
        value_start >= tokens.size()) {
        return; // Malformed -- no name, no value, or value before name -- ignore.
    }

    std::string name;
    for (std::size_t i = name_start; i < name_end; ++i) {
        if (!name.empty()) {
            name += ' ';
        }
        name += tokens[i];
    }

    if (name == "Threads") {
        try {
            int value = std::stoi(tokens[value_start]);
            if (value < kMinThreads) {
                value = kMinThreads;
            } else if (value > kMaxThreads) {
                value = kMaxThreads;
            }
            num_threads = value;
        } catch (const std::exception&) {
            // Non-integer value -- ignore, leaving num_threads unchanged.
        }
    } else if (name == "Hash") {
        try {
            long long value = std::stoll(tokens[value_start]);
            if (value < kMinHashMB) {
                value = kMinHashMB;
            } else if (value > kMaxHashMB) {
                value = kMaxHashMB;
            }
            hash_size_mb = static_cast<std::size_t>(value);
        } catch (const std::exception&) {
            // Non-integer value -- ignore, leaving hash_size_mb unchanged.
        }
    } else if (name == "Move Overhead") {
        try {
            int value = std::stoi(tokens[value_start]);
            if (value < kMinMoveOverheadMs) {
                value = kMinMoveOverheadMs;
            } else if (value > kMaxMoveOverheadMs) {
                value = kMaxMoveOverheadMs;
            }
            move_overhead_ms = value;
        } catch (const std::exception&) {
            // Non-integer value -- ignore, leaving move_overhead_ms unchanged.
        }
    } else if (name == "MultiPV") {
        try {
            int value = std::stoi(tokens[value_start]);
            if (value < kMinMultiPV) {
                value = kMinMultiPV;
            } else if (value > kMaxMultiPV) {
                value = kMaxMultiPV;
            }
            multi_pv = value;
        } catch (const std::exception&) {
            // Non-integer value -- ignore, leaving multi_pv unchanged.
        }
    }
    // Any other option name: silently ignored (this function's own doc comment).
}

/// Handles `go [depth N] [movetime N] [wtime W btime B [winc I] [binc I]]`
/// (any combination; unrecognized sub-options like `movestogo`/
/// `infinite`/`ponder`/`mate`/`nodes` are accepted but ignored — see
/// this file's header comment): runs the search, writing one `info
/// depth ... score ... nodes ... pv ...` line per completed iteration
/// (emit_info(), above) as it goes, then writes `bestmove <uci>` to
/// `out` once the search returns.
///
/// `game_history` is passed straight through to
/// search::search_iterative_deepening() (search/search.h's doc
/// comment) so repetition detection (ROADMAP.md Phase 3) is aware of
/// the real game's history, not just whatever the search recalculates
/// within its own tree — see handle_position()/apply_uci_moves() above
/// for how it's built.
///
/// `num_threads` is likewise passed straight through as
/// search_iterative_deepening()'s own `num_threads` parameter
/// (search/search.h's doc comment has the full Lazy SMP contract) --
/// run()'s own session-lifetime state, set via `setoption name Threads
/// value <N>` (handle_setoption() above) and defaulting to 1 (today's
/// pre-Lazy-SMP, single-threaded behavior) until a GUI/script
/// explicitly requests more.
///
/// `hash_size_mb`/`move_overhead_ms` (ROADMAP.md Phase 8, "Full UCI
/// option set"): same run()-owned, `setoption`-driven session-lifetime
/// state as `num_threads` -- the former is passed straight through as
/// search_iterative_deepening()'s own `hash_size_mb` parameter
/// (search/search.h), the latter is consumed by compute_search_budget()
/// below (that function's own doc comment) before the search even
/// starts.
///
/// `multi_pv` (ROADMAP.md Phase 8, "Full UCI option set" -- the
/// `MultiPV` sub-item): same run()-owned, `setoption`-driven session-
/// lifetime state as the three parameters above, passed straight
/// through as search_iterative_deepening()'s own `multi_pv` parameter
/// (search/search.h's doc comment has the full contract). No branching
/// needed here at all for the multi-line case: `on_iteration` below
/// already fires once per line, each with the right `multipv_index`
/// already set (search_iterative_deepening_multipv()'s own doc comment,
/// search.cpp), and emit_info() already emits the right `multipv N`
/// token per call (that function's own doc comment) -- so this
/// function's existing single on_iteration lambda, and its existing
/// single `bestmove` line at the end (SearchResult::multipv_lines' own
/// doc comment guarantees the top-level `result.best_move` always
/// mirrors the best line), both already do exactly the right thing
/// whether `multi_pv` is 1 or 20.
void handle_go(Position& pos, const std::vector<std::uint64_t>& game_history,
               const std::vector<std::string>& tokens, int num_threads, std::size_t hash_size_mb,
               int move_overhead_ms, int multi_pv, std::ostream& out) {
    // Opening book (src/book/book.h, ROADMAP.md's optional "small
    // curated opening book" item): consulted first, unconditionally --
    // no setoption/UCI-options infrastructure exists yet to gate this
    // behind an "OwnBook"-style toggle (this file's own header comment
    // already notes setoption is accepted but ignored entirely). A book
    // hit skips search entirely and answers immediately -- no `info
    // depth ...` line is emitted for it, since no depth was actually
    // searched; a `bestmove` alone is a fully valid UCI response.
    const std::optional<std::string> book_move = book::book_move(pos);
    if (book_move.has_value()) {
        out << "bestmove " << *book_move << '\n';
        out.flush();
        return;
    }

    const SearchBudget budget = compute_search_budget(pos, tokens, move_overhead_ms);

    // `on_iteration` (search/search.h's IterationCallback): emits one
    // `info depth ... score ... nodes ... pv ...` line per completed
    // iteration, live, before the final `bestmove` below -- see
    // emit_info()'s own doc comment. `material_weights` stays the
    // compiled-in-constants default (nullptr) -- no UCI option exists
    // to override it (search_fixed_depth()'s own doc comment covers
    // that parameter's real use, the Texel/SPSA tuner, not this UCI
    // loop). `num_threads`/`hash_size_mb` are this call's own
    // parameters, this function's own doc comment above; `external_stop`
    // stays nullptr -- an ordinary (non-ponder) `go` has no external
    // interruption source (this file's own header comment).
    const search::SearchResult result = search::search_iterative_deepening(
        pos, budget.max_depth, budget.time_limit_ms, game_history,
        [&out](const search::SearchResult& iteration_result) { emit_info(iteration_result, out); },
        /*material_weights=*/nullptr, num_threads, /*external_stop=*/nullptr, hash_size_mb,
        multi_pv);

    out << "bestmove ";
    if (result.best_move.is_null()) {
        // No legal move (checkmate/stalemate at the root) -- "0000" is
        // the conventional UCI null-move token GUIs recognize; there's
        // no other clean way to say "no move" via bestmove.
        out << "0000";
    } else {
        out << result.best_move.to_uci();
    }
    out << '\n';
    out.flush();
}

/// True if `target` appears anywhere in `tokens` after index 0 (the
/// command word itself) — used to detect `go ... ponder ...` amid
/// `go`'s other, order-independent sub-options.
[[nodiscard]] bool has_token(const std::vector<std::string>& tokens, const std::string& target) {
    return std::find(tokens.begin() + 1, tokens.end(), target) != tokens.end();
}

/// All state for one in-flight `go ponder` search — a single instance
/// lives for run()'s whole lifetime (ROADMAP.md Phase 7, "Pondering").
/// Deliberately not copyable/movable (std::thread and std::atomic both
/// aren't) — always held by reference, never returned or stored
/// elsewhere.
struct PonderState {
    /// The background thread running the actual pondering search (see
    /// start_pondering() below). Non-joinable when no ponder search is
    /// in flight — every function here uses `thread.joinable()` as the
    /// "is a search actually running" check, rather than a separate
    /// bool, so there's exactly one source of truth.
    std::thread thread;

    /// Set to request the background search stop — checked both between
    /// iterations and mid-iteration by search::search_iterative_
    /// deepening()'s own `external_stop` parameter (search.h's doc
    /// comment on that parameter has the full contract). Reset to
    /// `false` at the start of every new start_pondering() call.
    std::atomic<bool> stop{false};

    /// When true, the background thread's own completion (see
    /// start_pondering()'s lambda) skips writing `bestmove` to `out`
    /// entirely — used only for the defensive "abandon a still-running
    /// ponder search because an out-of-protocol command arrived"
    /// path (abandon_pondering() below), never for a genuine
    /// `ponderhit`/`stop`, both of which DO still produce a `bestmove`
    /// per the UCI spec's own requirement that `stop` always yields one
    /// (docs/DECISIONS.md has the full rationale for why `stop` still
    /// prints even though the GUI is expected to discard it).
    std::atomic<bool> suppress_output{false};

    /// True from the moment start_pondering() launches the background
    /// search until either handle_ponderhit() or abandon_pondering()
    /// consumes it — used only to make handle_ponderhit() a safe no-op
    /// if it arrives with no pondering search actually in flight (an
    /// out-of-protocol `ponderhit`), since `thread.joinable()` alone
    /// stays true even after ponderhit until the search eventually
    /// finishes.
    bool active = false;

    /// The millisecond time budget an ORDINARY (non-ponder) `go` from
    /// the same position would have used — computed once, up front, by
    /// start_pondering() via compute_search_budget(), and consulted
    /// only later, by handle_ponderhit() — see that function's own doc
    /// comment for how it's applied. 0 means "no real time budget"
    /// (compute_search_budget()'s own doc comment).
    int saved_time_limit_ms = 0;
};

/// Unconditionally stops and joins any in-flight pondering search,
/// discarding its result (never writing `bestmove`) — the defensive
/// path for an out-of-protocol command arriving while `go ponder` is
/// still running (a compliant GUI always sends `ponderhit` or `stop`
/// first), matching this file's established convention of degrading
/// gracefully rather than crashing or corrupting state on a malformed
/// command sequence (this file's header comment; handle_position()'s/
/// apply_uci_moves()'s own doc comments). A no-op if nothing is
/// running. Called from run() ahead of `position`/`ucinewgame`/`go`
/// (a second `go ponder` arriving while one is already active) and
/// once, unconditionally, right before run() returns (covers `quit`
/// and end-of-input alike) — std::thread's destructor calls
/// std::terminate() on a still-joinable thread, so this is a hard
/// correctness requirement, not just tidiness.
void abandon_pondering(PonderState& ponder) {
    if (!ponder.thread.joinable()) {
        return;
    }
    ponder.suppress_output.store(true, std::memory_order_relaxed);
    ponder.stop.store(true, std::memory_order_relaxed);
    ponder.thread.join();
    ponder.active = false;
}

/// Starts a `go ponder ...` search in the background (ROADMAP.md Phase
/// 7, "Pondering — search side: handle `go ponder`"). Unlike an
/// ordinary `go` (handle_go(), fully synchronous), this launches
/// `ponder.thread` and returns immediately, so run()'s own command loop
/// keeps reading further lines (the whole point — a later `ponderhit`
/// or `stop` needs to reach handle_ponderhit()/handle_stop() while the
/// search is still running, not after).
///
/// The background search itself runs with `time_limit_ms = 0` (no
/// deadline at all) and a high `max_depth` (kTimedSearchMaxDepth) — it
/// searches as deep as it profitably can for as long as it's allowed
/// to run, which is exactly what pondering during the opponent's own
/// thinking time is FOR — stopped only via `ponder.stop`
/// (search::search_iterative_deepening()'s new `external_stop`
/// parameter, search.h). Deliberately does NOT consult the opening
/// book (src/book/book.h) the way handle_go() does — pondering on a
/// book-covered position would have nothing to actually search, and
/// this project's book has no toggle to check first without also
/// gating this call's own behavior on it; see docs/DECISIONS.md for
/// the full rationale.
///
/// The REAL time budget for the eventual answer — what an ordinary
/// `go` from this exact position would have used — is computed once,
/// up front, via compute_search_budget(), and saved into
/// `ponder.saved_time_limit_ms` for handle_ponderhit() to apply later;
/// it is NOT applied to the background search itself (see that
/// function's own doc comment for exactly how/when it's used).
///
/// `pos` and `game_history` are both copied into the background
/// thread's own closure rather than captured by reference — the same
/// reason search.cpp's Lazy SMP helper threads (run_lazy_smp_helper())
/// each get their own private Position copy: `pos`/`game_history` are
/// run()'s own locals, which could in principle be touched again by a
/// later command on the main thread (an out-of-protocol `position`
/// while still pondering, handled by abandon_pondering() above) while
/// this search is still running on its own thread — a private copy
/// sidesteps that race entirely rather than depending on the caller
/// always behaving.
///
/// `hash_size_mb`/`move_overhead_ms` (ROADMAP.md Phase 8, "Full UCI
/// option set"): same run()-owned, `setoption`-driven session-lifetime
/// state handle_go() consumes — `hash_size_mb` is passed straight
/// through to this call's own search_iterative_deepening() the same
/// way; `move_overhead_ms` feeds into compute_search_budget() below
/// when computing the REAL move's saved budget for handle_ponderhit()
/// to apply later, exactly as it would for an ordinary `go` from this
/// same position.
void start_pondering(Position& pos, const std::vector<std::uint64_t>& game_history,
                      const std::vector<std::string>& tokens, int num_threads,
                      std::size_t hash_size_mb, int move_overhead_ms, std::ostream& out,
                      PonderState& ponder) {
    abandon_pondering(ponder); // Defensive: see this function's own doc comment above.

    const SearchBudget budget = compute_search_budget(pos, tokens, move_overhead_ms);
    ponder.saved_time_limit_ms = budget.time_limit_ms;
    ponder.stop.store(false, std::memory_order_relaxed);
    ponder.suppress_output.store(false, std::memory_order_relaxed);
    ponder.active = true;

    Position ponder_pos = pos;
    std::vector<std::uint64_t> ponder_history = game_history;
    std::atomic<bool>* stop_ptr = &ponder.stop;
    std::atomic<bool>* suppress_ptr = &ponder.suppress_output;

    ponder.thread = std::thread([&out, num_threads, hash_size_mb, ponder_pos, ponder_history,
                                  stop_ptr, suppress_ptr]() mutable {
        const search::SearchResult result = search::search_iterative_deepening(
            ponder_pos, kTimedSearchMaxDepth, /*time_limit_ms=*/0, ponder_history,
            /*on_iteration=*/nullptr, /*material_weights=*/nullptr, num_threads, stop_ptr,
            hash_size_mb);
        if (suppress_ptr->load(std::memory_order_relaxed)) {
            return;
        }
        out << "bestmove ";
        if (result.best_move.is_null()) {
            out << "0000";
        } else {
            out << result.best_move.to_uci();
        }
        out << '\n';
        out.flush();
    });
}

/// Handles `ponderhit` — "continue as real search on `ponderhit`"
/// (ROADMAP.md's own wording for this item). The background search
/// started by start_pondering() keeps running exactly as it already
/// was (no restart, no wasted work) — this function's only job is to
/// hand it the REAL move's time budget (`ponder.saved_time_limit_ms`,
/// computed up front by start_pondering() via compute_search_budget())
/// so it eventually stops at a sensible point instead of continuing to
/// chase kTimedSearchMaxDepth unbounded.
///
/// Because search::search_iterative_deepening()'s own deadline (when it
/// has one at all) is fixed at the moment that call starts and can't be
/// adjusted on an already-in-flight call, the budget is applied via a
/// small, short-lived, detached watchdog thread that sleeps for the
/// budgeted duration and then raises `ponder.stop` — the exact same
/// flag the background search is already checking. This is an accepted
/// simplification (see docs/DECISIONS.md for the full writeup): the
/// budget applied is the one `go ponder`'s own `wtime`/`btime`/
/// `movetime` tokens implied WHEN THE PONDER SEARCH STARTED, not a
/// clock re-synced against however much of the opponent's own time has
/// actually elapsed since then (this loop has no live channel for the
/// GUI to resend updated clock values at `ponderhit` — the UCI spec's
/// own `ponderhit` command carries no parameters at all).
///
/// If `go ponder` carried no real time budget at all (an explicit
/// `depth N` with no `movetime`, or neither depth nor any time control
/// — compute_search_budget()'s own doc comment, `saved_time_limit_ms ==
/// 0`), there's no duration to hand off — `ponderhit` instead requests
/// an immediate stop, taking whatever depth the background search has
/// already reached rather than continuing toward kTimedSearchMaxDepth
/// unconstrained (also documented in docs/DECISIONS.md as an accepted,
/// narrower fallback rather than a fuller re-derivation of what a bare
/// `go depth N`/`go` would have wanted).
///
/// A no-op if no pondering search is actually active (an out-of-
/// protocol `ponderhit` with nothing running) — same defensive
/// robustness convention as every other command handler in this file.
void handle_ponderhit(PonderState& ponder) {
    if (!ponder.active || !ponder.thread.joinable()) {
        return;
    }
    ponder.active = false;

    if (ponder.saved_time_limit_ms <= 0) {
        ponder.stop.store(true, std::memory_order_relaxed);
        return;
    }

    std::atomic<bool>* stop_ptr = &ponder.stop;
    const int delay_ms = ponder.saved_time_limit_ms;
    std::thread watchdog([stop_ptr, delay_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        stop_ptr->store(true, std::memory_order_relaxed);
    });
    // Detached, not joined: this thread's only job is a timed sleep
    // followed by one atomic store, touching nothing that isn't kept
    // alive for run()'s whole lifetime (`ponder.stop` itself) — and
    // by the time it actually fires, the background search it signals
    // is guaranteed to still be running (that's what makes it stop),
    // which in turn is what run()'s own final abandon_pondering() call
    // blocks on joining before run() can return — so this watchdog is
    // always long gone (its own store already happened, its thread
    // already exited) before `ponder.stop` could ever be destroyed.
    watchdog.detach();
}

/// Handles `stop` while a pondering search is in flight — "discard and
/// restart on `stop` + actual move" (ROADMAP.md's own wording): this
/// function itself performs the "discard" half (stopping the search and
/// letting its own thread still print the resulting `bestmove`, which
/// the UCI spec requires even here — see PonderState::suppress_output's
/// own doc comment for why this path does NOT suppress it, unlike
/// abandon_pondering()'s defensive path). The "restart" half is simply
/// the natural consequence of the UCI protocol from here: the GUI is
/// expected to follow this with a fresh `position` (now including the
/// opponent's REAL move, not the one this ponder search guessed) and a
/// fresh `go` — ordinary, synchronous handle_go(), unaffected by
/// anything in this function — once it's ready, no special handling
/// needed on this file's side for that second half at all.
///
/// A no-op if no pondering search is actually active (`stop` arriving
/// with nothing running, or after a search has already naturally
/// finished/been ponderhit-then-completed) — same defensive robustness
/// convention as every other command handler in this file. Blocks until
/// the background thread has actually stopped and printed its
/// `bestmove` (`thread.join()`), matching this file's existing
/// synchronous, one-command-fully-handled-before-the-next-line-is-read
/// convention for every OTHER command besides `go ponder` itself.
void handle_stop(PonderState& ponder) {
    if (!ponder.thread.joinable()) {
        return;
    }
    ponder.active = false;
    ponder.stop.store(true, std::memory_order_relaxed);
    ponder.thread.join();
}

/// Waits out whatever pondering search is still running when run() is
/// about to return, WITHOUT discarding a result that was already
/// properly earned via `ponderhit`/`stop` — the tail-end counterpart to
/// abandon_pondering() above, and NOT interchangeable with it. The two
/// cases:
///   - `ponder.active` still true: no `ponderhit`/`stop` ever arrived
///     for this search at all (e.g. `go ponder` immediately followed by
///     `quit`, or `in` simply running out of lines) — this is a genuine
///     abandonment, so abandon_pondering() (suppressed output, forced
///     stop) is the right call.
///   - `ponder.active` already false: `handle_ponderhit()` or
///     `handle_stop()` already ran for this search — `handle_stop()`
///     itself already joins synchronously, so `ponder.thread` is only
///     still joinable here in the `ponderhit` case, where the
///     background search may simply not have finished yet (its own
///     watchdog thread, handle_ponderhit(), hasn't fired, or the search
///     itself hasn't noticed `ponder.stop` on its next periodic check).
///     That's a properly-earned result already in flight, on its way to
///     printing `bestmove` on its own — an ordinary, UNFORCED join is
///     the right call here, not abandon_pondering()'s suppress-and-
///     force-stop, which would silently swallow a `ponderhit`-triggered
///     `bestmove` the caller specifically asked for and is entitled to.
/// A no-op either way if nothing is running.
void finish_pondering(PonderState& ponder) {
    if (ponder.active) {
        abandon_pondering(ponder);
    } else if (ponder.thread.joinable()) {
        ponder.thread.join();
    }
}

} // namespace

void run(std::istream& in, std::ostream& out) {
    Position pos = board::start_position();
    // Ancestors of `pos`, oldest to newest, NOT including `pos` itself —
    // see apply_uci_moves()/handle_position() above for how this is
    // built and search/search.h's `game_history` doc comment for what
    // it's used for.
    std::vector<std::uint64_t> game_history;
    // `Threads` UCI option's current value (ROADMAP.md Phase 7) --
    // session-lifetime state, like `pos`/`game_history` above: set via
    // `setoption` (handle_setoption()), read by every subsequent `go`
    // (handle_go()), and -- unlike `pos`/`game_history` -- NOT reset by
    // `ucinewgame` below, matching the UCI convention that engine
    // OPTIONS persist across games within one session while game STATE
    // does not.
    int num_threads = kMinThreads;
    // `Hash`/`Move Overhead` UCI options' current values (ROADMAP.md
    // Phase 8, "Full UCI option set") -- session-lifetime state, exactly
    // like `num_threads` above: set via `setoption` (handle_setoption()),
    // read by every subsequent `go` (handle_go()/start_pondering()), and
    // NOT reset by `ucinewgame` below, same "options persist, game state
    // doesn't" convention `num_threads` already follows.
    std::size_t hash_size_mb = search::kDefaultTTSizeMB;
    int move_overhead_ms = kMinMoveOverheadMs;
    // `MultiPV` (ROADMAP.md Phase 8, "Full UCI option set" -- the last
    // sub-item, completing this bullet): same session-lifetime,
    // `setoption`-driven, not-reset-by-`ucinewgame` convention as
    // `Hash`/`Move Overhead` above. Deliberately NOT threaded into
    // start_pondering() below -- pondering never emits `info` lines at
    // all (its own search call already passes `on_iteration=nullptr`,
    // this file's existing code, unchanged by this item) and its
    // eventual `bestmove` always already reports the single best line
    // regardless of how many lines were computed
    // (SearchResult::multipv_lines' own doc comment, search.h -- the
    // top-level result always mirrors the best line), so computing
    // extra MultiPV lines during a ponder search would cost real time
    // for zero observable UCI effect -- pondering's own search call
    // stays at the implicit default of 1 line.
    int multi_pv = kMinMultiPV;
    // Pondering state (ROADMAP.md Phase 7) — session-lifetime, like
    // `num_threads` above, though its own contents (the background
    // thread, the stop flag) are reset per `go ponder` by
    // start_pondering() itself, not by `ucinewgame` here (a pondering
    // search is tied to one specific `go ponder` call, not the whole
    // session the way `Threads` is) — see PonderState's own doc comment.
    PonderState ponder;
    std::string line;

    while (std::getline(in, line)) {
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) {
            continue;
        }
        const std::string& cmd = tokens[0];

        if (cmd == "uci") {
            out << "id name Nightwing\n";
            out << "id author g-c-3\n";
            // `option name Threads type spin default <D> min <MIN> max <MAX>`:
            // standard UCI `spin` option syntax -- kMinThreads/kMaxThreads's
            // own doc comment above has the bounds' rationale.
            out << "option name Threads type spin default " << kMinThreads << " min " << kMinThreads
                << " max " << kMaxThreads << '\n';
            // `Hash`/`Move Overhead` (ROADMAP.md Phase 8, "Full UCI
            // option set") -- same standard `spin` syntax as `Threads`
            // just above; kMinHashMB/kMaxHashMB and
            // kMinMoveOverheadMs/kMaxMoveOverheadMs's own doc comments
            // have each option's bounds rationale.
            out << "option name Hash type spin default " << search::kDefaultTTSizeMB << " min "
                << kMinHashMB << " max " << kMaxHashMB << '\n';
            out << "option name Move Overhead type spin default " << kMinMoveOverheadMs << " min "
                << kMinMoveOverheadMs << " max " << kMaxMoveOverheadMs << '\n';
            // `MultiPV` (ROADMAP.md Phase 8, "Full UCI option set" --
            // the last sub-item) -- same standard `spin` syntax;
            // kMinMultiPV/kMaxMultiPV's own doc comment above has the
            // bounds rationale.
            out << "option name MultiPV type spin default " << kMinMultiPV << " min " << kMinMultiPV
                << " max " << kMaxMultiPV << '\n';
            // `Ponder` (ROADMAP.md Phase 8, "Pondering — protocol
            // side"): a `check` (boolean), not a `spin` -- standard UCI
            // convention for this specific option (every compliant GUI
            // recognizes it as the signal "this engine supports `go
            // ponder`"). Default `true`: pondering itself (`go ponder`/
            // `ponderhit`/`stop`, handle_ponderhit()/start_pondering()
            // below) has been fully implemented since Session 75/76,
            // entirely independently of this option's own value --
            // advertising `true` here is simply telling the GUI that
            // capability exists so IT can decide whether to use it, not
            // gating any of this engine's own behavior. `setoption name
            // Ponder value <true|false>` is accepted (handle_setoption()
            // below) but deliberately has NO behavioral effect: a
            // compliant GUI only ever uses this option to decide whether
            // to SEND `go ponder` in the first place, never to tell the
            // engine to stop supporting it once already advertised, so
            // there is nothing for this engine to gate on either way --
            // pondering support is unconditional, matching how many
            // established engines implement this specific option.
            out << "option name Ponder type check default true\n";
            out << "uciok\n";
            out.flush();
        } else if (cmd == "isready") {
            out << "readyok\n";
            out.flush();
        } else if (cmd == "ucinewgame") {
            abandon_pondering(ponder); // A new game starting mid-ponder is out-of-protocol; degrade gracefully.
            pos = board::start_position();
            game_history.clear();
        } else if (cmd == "position") {
            abandon_pondering(ponder); // Same rationale as ucinewgame above.
            handle_position(pos, game_history, tokens);
        } else if (cmd == "setoption") {
            handle_setoption(num_threads, hash_size_mb, move_overhead_ms, multi_pv, tokens);
        } else if (cmd == "go") {
            if (has_token(tokens, "ponder")) {
                start_pondering(pos, game_history, tokens, num_threads, hash_size_mb,
                                 move_overhead_ms, out, ponder);
            } else {
                handle_go(pos, game_history, tokens, num_threads, hash_size_mb, move_overhead_ms,
                          multi_pv, out);
            }
        } else if (cmd == "ponderhit") {
            handle_ponderhit(ponder);
        } else if (cmd == "stop") {
            handle_stop(ponder);
        } else if (cmd == "quit") {
            break;
        }
        // debug, register, and anything else unrecognized (including a
        // `setoption` for any name besides `Threads` --
        // handle_setoption()'s own doc comment): silently ignored, per
        // the UCI spec's expectation that engines ignore commands they
        // don't understand — required for robustness against a
        // GUI/script sending commands ahead of what this phase supports
        // (see this file's header comment).
    }

    // Ensures no pondering search outlives run() itself -- std::thread's
    // destructor calls std::terminate() on a still-joinable thread, so
    // this is a hard correctness requirement covering every exit path
    // (`quit`, or `in` simply running out of lines) uniformly, not just
    // tidiness. Uses finish_pondering(), NOT abandon_pondering()
    // directly -- see finish_pondering()'s own doc comment for why: a
    // `ponderhit` that already properly earned its `bestmove` (just not
    // finished printing it yet by the time `quit`/end-of-input arrives)
    // must not be silently suppressed here the way a genuinely
    // abandoned (never `ponderhit`/`stop`ped) search should be. A no-op
    // on the overwhelmingly common path where nothing was pondering
    // when the loop ended.
    finish_pondering(ponder);
}

} // namespace nightwing::uci
