// src/uci/uci.cpp
//
// Deliberately out of scope for Phase 2's "basic UCI loop" (revisit
// later phases, tracked informally here rather than duplicated across
// every function that touches it):
//   - setoption / engine options: `Threads` (added Session 74,
//     ROADMAP.md Phase 7's "Thread count UCI option" item), `Hash`,
//     `Move Overhead`, and `MultiPV` (all three added under Phase 8's
//     "Full UCI option set" item -- see handle_setoption() below) are
//     all recognized; "Full UCI option set" is now fully done. `Skill
//     Level` (Phase 8's own separate "Skill level / strength limiting"
//     item, src/search/skill.h) and `Contempt` (Phase 8's own separate
//     "Contempt / draw score adjustment" item, search.h's
//     search_iterative_deepening()'s own `contempt_cp` doc comment) are
//     also recognized. `Hash`
//     changes the size of run()'s own persistent, engine-lifetime
//     TranspositionTable (ROADMAP.md Priority Fixes, 2026-09-08,
//     "Persistent, engine-lifetime transposition table" -- see that
//     table's own declaration inside run(), below, and
//     search/tt.h's LIFETIME NOTE) by rebuilding it fresh at the new
//     size -- TranspositionTable has no in-place resize operation, so
//     a `setoption name Hash value <N>` genuinely changing the size
//     discards whatever was cached at the old size, exactly the
//     behavior a person changing `Hash` mid-session should expect.
//     `ucinewgame` clears that same persistent table (a real clear(),
//     not a fresh allocation) rather than leaving an unrelated
//     previous game's positions cached. `MultiPV` uses classic
//     root-move exclusion (search::search_iterative_deepening_multipv(),
//     search.cpp) with two deliberate first-draft simplifications, both
//     documented at that function's own definition: no aspiration
//     windows and no Lazy SMP when it genuinely takes effect. `Ponder`
//     (the option ADVERTISEMENT, distinct from pondering ITSELF, which
//     is already fully implemented below) is ROADMAP.md's own next,
//     separate Phase 8 item ("Pondering — protocol side"), not part of
//     "Full UCI option set" 's own item text -- still open.
//   - Asynchronous `go` with a working `stop`/`isready`/`quit` (ROADMAP.md
//     Priority Fixes, 2026-09-22, item 1) IS implemented -- see GoState/
//     abandon_go()/start_go()/handle_go_stop() below. An ordinary `go`
//     now runs on its own background thread (the exact same shape
//     start_pondering() already used for `go ponder`), so this loop's
//     own command-reading keeps going while a search is in flight: a
//     real `stop` reaches the search via `GoState::stop`
//     (search::search_iterative_deepening()'s own `external_stop`
//     parameter) and actually interrupts it rather than arriving too
//     late to matter, `isready` gets an immediate `readyok` without
//     waiting for the search to finish, and `quit`/end-of-input still
//     joins any outstanding search before this loop returns (run()'s own
//     tail end, mirroring finish_pondering()). `go infinite` and a bare
//     `go` (neither depth nor any usable time control) now run genuinely
//     unbounded until a `stop` arrives, same as `go ponder` already does
//     -- see compute_search_budget()'s own final `else` branch below.
//     Writes to `out` from the search thread and from this loop's own
//     main-thread command handling are serialized through a shared
//     `out_mutex` (run()'s own local) so concurrent writers can never
//     interleave mid-line.
//   - Pondering (`go ponder`, `ponderhit`, and `stop` while pondering)
//     IS implemented (ROADMAP.md Phase 7) — see start_pondering()/
//     handle_ponderhit()/handle_stop() below and docs/DECISIONS.md for
//     the full design. This is the one place a background search
//     thread and an externally-arriving `stop`/`ponderhit` genuinely
//     coexist with this loop's own synchronous command reading — the
//     bullet above extends that same coexistence to ordinary `go`/
//     `stop` too, now that both run this way.
//
// `go` now also consults src/book/book.h's small curated opening book
// FIRST, before any of the above depth/time-control logic even runs
// (start_go(), below) -- ROADMAP.md's optional "small curated opening
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
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "bench_positions.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/movegen.h"
#include "board/move.h"
#include "book/book.h"
#include "nightwing/version.h"
#include "search/search.h"
#include "search/skill.h"
#include "search/tt.h"

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
/// Returns the number of tokens actually applied (ROADMAP.md Priority
/// Fixes, 2026-09-22, finding 9) — `move_tokens.size()` on full success,
/// less than that iff a token failed to match, so a caller with a real
/// `info string` diagnostic to send (handle_position(), below) can tell
/// the two cases apart without this function itself needing to know
/// about `std::ostream` or UCI output formatting at all: it stays a
/// pure position-mutating function, same as before this return value
/// existed, just no longer silently discarding the "how far did this
/// get" information every caller of it now has for free.
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
std::size_t apply_uci_moves(Position& pos, const std::vector<std::string>& move_tokens,
                             std::vector<std::uint64_t>& history) {
    std::size_t applied = 0;
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
                ++applied;
                break;
            }
        }
        if (!matched) {
            break;
        }
    }
    return applied;
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
///
/// `out` (ROADMAP.md Priority Fixes, 2026-09-22, finding 9): used ONLY
/// to emit a single `info string` line if apply_uci_moves() reports it
/// applied fewer tokens than were given — i.e. a `moves` list with an
/// illegal or malformed entry partway through. `info string` is the
/// standard UCI channel for exactly this ("a message that will be
/// displayed by the engine", per the UCI spec — no GUI is expected to
/// parse or act on its contents, only show it) rather than inventing a
/// new one; the position itself still ends up exactly where
/// apply_uci_moves() already left it (everything up to, not including,
/// the bad token) — this is a diagnostic, not a behavior change, from
/// the silent version this replaces.
void handle_position(Position& pos, std::vector<std::uint64_t>& history,
                      const std::vector<std::string>& tokens, std::ostream& out) {
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
        const std::size_t applied = apply_uci_moves(pos, move_tokens, history);
        if (applied < move_tokens.size()) {
            out << "info string position: stopped after " << applied << " of "
                << move_tokens.size() << " moves -- '" << move_tokens[applied]
                << "' did not match a legal move in the resulting position\n";
            out.flush();
        }
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

/// Fallback search depth used ONLY for a malformed explicit `depth`
/// token (`depth 0`, a negative value, or a non-numeric one --
/// compute_search_budget()'s own `have_depth` branch below) -- a small,
/// safe, immediately-usable depth rather than kTimedSearchMaxDepth,
/// since a malformed `depth N` is a genuine caller mistake, not a
/// request for an unbounded search. Previously ALSO used as the
/// fallback for a bare `go`/`go infinite` (neither depth nor any usable
/// time control at all) on the reasoning that nothing else would ever
/// stop the search -- that reasoning is now stale: `go` runs
/// asynchronously with a real `stop` the search actually checks
/// (ROADMAP.md Priority Fixes, 2026-09-22, item 1 -- GoState/start_go()/
/// handle_go_stop() below), so that case now uses kTimedSearchMaxDepth
/// instead, same as `go ponder` already does -- see
/// compute_search_budget()'s own final `else` branch below. Empirically
/// ~127ms in a Release build on the starting position at this depth
/// (see DECISIONS.md) if this fallback ever does fire.
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

/// Bounds for the `Contempt` UCI option (ROADMAP.md Phase 8, "Contempt
/// / draw score adjustment" -- src/search/search.h's own
/// search_iterative_deepening()/search_fixed_depth() doc comments on
/// their identically-named `contempt_cp` parameter have the full
/// design). `kMaxContemptCp` (100, one pawn -- eval::MaterialWeights::
/// pawn_mg's own default, the same "keep a knob within a sensible
/// order of magnitude relative to real material values" reasoning
/// kSkillNoiseCapCp already uses, src/search/skill.h) caps how much a
/// draw's own score can be pushed away from a genuinely neutral 0 --
/// beyond a pawn's worth, contempt would start to meaningfully distort
/// ordinary positional judgement rather than just nudging draw
/// preference, which is well past what "discourage/encourage draws"
/// is meant to do. `kMinContemptCp` is simply its negation -- Contempt
/// is symmetric: a negative value means actively welcoming draws
/// (useful against a known-stronger opponent), exactly as much as a
/// positive value means avoiding them. Default 0 (no adjustment at
/// all) preserves every pre-existing caller's exact behavior until a
/// GUI/tournament manager explicitly sets this option.
constexpr int kMinContemptCp = -100;
constexpr int kMaxContemptCp = 100;

/// Result of allocate_time_ms() below: a SOFT budget (the ordinary,
/// "expected" allocation for this move -- used as the early-stop
/// threshold once the best move has stabilized, search.h's own
/// `soft_time_limit_ms` doc comment) and a HARD budget (an upper bound
/// the search may extend into if the position turns out to be
/// genuinely unstable, but never exceed). `hard_ms >= soft_ms` always.
struct TimeAllocation {
    int soft_ms = 0;
    int hard_ms = 0;
};

/// How much larger the hard cap is allowed to be than the soft budget
/// (ROADMAP.md Phase 8, "Time management" -- the best-move-stability-
/// based extension sub-item) when the position is unstable enough that
/// the soft early-stop in search_iterative_deepening() never triggers.
/// 3x is a common, reasonable starting point in other engines' own
/// time-management schemes -- like kStabilityThreshold (search.cpp),
/// explicitly NOT yet tuned for Nightwing specifically; revisit once
/// SPRT infrastructure exists (ROADMAP.md Phase 8's own still-open
/// "SPRT testing setup/process" item) to validate empirically rather
/// than by feel.
constexpr int kHardBudgetMultiplier = 3;

/// Time allocation for `go wtime/btime/winc/binc` — returns both a soft
/// (expected/normal) and a hard (maximum, extendable-into-if-unstable)
/// budget; see TimeAllocation's own doc comment just above and
/// search.h's `soft_time_limit_ms` doc comment on
/// search_iterative_deepening() for how the two are actually used.
///
/// `movestogo` (ROADMAP.md Phase 8, "Time management" -- the
/// "increment handling"/allocation-strategy sub-item this specific
/// piece addresses): when the GUI provides it (a genuine "N moves until
/// the next time control" count, standard in classical/non-increment-
/// only time controls), it replaces this function's previous, fixed
/// "assume roughly 20 moves remain" guess with the ACTUAL number of
/// moves remaining -- a materially better estimate whenever it's
/// available, since the fixed-20 guess is only ever a guess, while
/// `movestogo` is exact information the GUI already has and is
/// offering. Falls back to the same fixed-20 heuristic as before when
/// `movestogo <= 0` (not provided, or a malformed non-positive value --
/// same defensive-clamping convention as every other option/token
/// parsed in this file).
///
/// Both budgets are capped at `remaining_ms / 2` (never claim more than
/// half of what's left, regardless of `movestogo` or the hard
/// multiplier above) — the same safety invariant this function already
/// had before this session's changes, now applied to both numbers
/// rather than just one.
[[nodiscard]] TimeAllocation allocate_time_ms(int remaining_ms, int increment_ms,
                                                int movestogo) noexcept {
    if (remaining_ms <= 0) {
        return {50, 50}; // Effectively "as little as possible, but not zero," for both.
    }
    const int divisor = movestogo > 0 ? movestogo : 20;
    const int cap = remaining_ms / 2;

    int soft = remaining_ms / divisor + increment_ms;
    if (soft > cap) {
        soft = cap;
    }
    if (soft < 1) {
        soft = 1;
    }

    // `long long` for the multiply: guards against a theoretical
    // overflow at extreme `remaining_ms` values before the cap below
    // brings it back into `int` range -- cheap insurance, not a
    // response to any observed failure.
    long long hard = static_cast<long long>(soft) * kHardBudgetMultiplier;
    if (hard > cap) {
        hard = cap;
    }
    if (hard < soft) {
        hard = soft; // Never let the hard cap be smaller than the soft budget.
    }
    return {soft, static_cast<int>(hard)};
}

/// Result of parsing `go`'s own depth/time-control tokens (used both by
/// start_go() for an ordinary `go`, and by start_pondering() below to
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
    /// This is the HARD cap, passed as search_iterative_deepening()'s
    /// own `time_limit_ms` — never exceeded.
    int time_limit_ms = 0;

    /// The SOFT budget (ROADMAP.md Phase 8, "Time management" --
    /// best-move-stability-based extension), passed as
    /// search_iterative_deepening()'s own `soft_time_limit_ms`
    /// parameter. 0 means "no soft budget" — search.h's own doc comment
    /// on that parameter: the search then runs exactly as if this
    /// feature didn't exist, all the way to `time_limit_ms`/`max_depth`
    /// regardless of best-move stability. Only ever set to a genuine
    /// positive value alongside a `wtime`/`btime`-derived
    /// `time_limit_ms` below — an explicit `movetime N` is a direct,
    /// deliberate instruction for exactly how long to think (this
    /// file's own Move Overhead doc comment draws the same "movetime is
    /// deliberate, don't second-guess it" line elsewhere), so no soft
    /// early-stop applies to it; nor does a bare `depth N` with no time
    /// control, which has no time budget to be soft about at all.
    int soft_time_limit_ms = 0;

    /// True only for a genuinely unbounded search: an explicit `go
    /// infinite`, or a bare `go` with no depth/time-control tokens at
    /// all -- set exactly once, in this function's own final `else`
    /// branch below, rather than inferred later from `max_depth`/
    /// `time_limit_ms` happening to match kTimedSearchMaxDepth/0, since
    /// an explicit `depth <kTimedSearchMaxDepth>` with no time control
    /// would otherwise be indistinguishable from the true unbounded
    /// case despite meaning something different (a deliberate, if
    /// unusually deep, bounded request). Read by start_go()'s own
    /// GoState::unbounded, which this field exists specifically to
    /// drive — see that field's own doc comment.
    bool unbounded = false;

    /// UCI `go nodes <n>` (ROADMAP.md Priority Fixes, 2026-09-22,
    /// finding 9) — passed straight through as search_iterative_
    /// deepening()'s own `max_nodes` parameter (that parameter's doc
    /// comment, search.h, has the full contract: a soft target checked
    /// periodically, not a hard node-exact stop). `has_node_limit`
    /// stays false (the default) for every `go` without a `nodes`
    /// token, identical to this field not existing at all.
    bool has_node_limit = false;
    std::uint64_t max_nodes = 0;

    /// UCI `searchmoves <m1> <m2> ...` (ROADMAP.md Priority Fixes,
    /// 2026-09-22, finding 9) — already-resolved, already-confirmed-
    /// legal board::Move values (resolved against the position this
    /// budget was computed for, the same `legal[i].to_uci() == token`
    /// matching apply_uci_moves() below uses), passed straight through
    /// as search_iterative_deepening()'s own `searchmoves` parameter.
    /// Empty (the default) means "no restriction," identical to this
    /// field not existing at all.
    std::vector<board::Move> searchmoves;
};

/// The set of `go` sub-option keywords compute_search_budget()'s own
/// token loop below recognizes — used ONLY to know where a
/// `searchmoves` move list ends (the UCI spec places `searchmoves` last
/// among `go`'s sub-options, but doesn't forbid something else
/// following it, and this engine has never required strict token
/// ordering for any other `go` sub-option either, so `searchmoves`
/// shouldn't be the one exception that silently swallows a keyword
/// typed after it by mistake into what looks like an extra, nonsense
/// "move").
[[nodiscard]] bool is_go_keyword(const std::string& tok) {
    static const std::unordered_set<std::string> kKeywords{
        "depth",     "movetime", "wtime", "btime",     "winc",
        "binc",      "movestogo", "nodes", "mate",     "infinite",
        "ponder",    "searchmoves"};
    return kKeywords.contains(tok);
}

/// Parses `go`'s own `[depth N] [movetime N] [wtime W btime B [winc I]
/// [binc I] [movestogo N]]` tokens (any combination; `movestogo` feeds
/// allocate_time_ms() above -- see that function's own doc comment;
/// other unrecognized sub-options like `infinite`/`ponder`/`mate`/
/// `nodes` are still ignored here, same as handle_go()'s own former
/// inline version of this logic) into
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
    int movestogo = 0;

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
        } else if (tok == "movestogo") {
            movestogo = next_int();
        } else if (tok == "nodes") {
            // std::stoi, not next_int() (which returns a signed int) --
            // a node count can genuinely exceed INT_MAX on a long think,
            // and max_nodes is std::uint64_t (search.h's own
            // SearchLimits::max_nodes doc comment) specifically so it
            // doesn't silently wrap for exactly this reason.
            if (i + 1 < tokens.size()) {
                try {
                    const long long parsed = std::stoll(tokens[++i]);
                    if (parsed > 0) {
                        budget.max_nodes = static_cast<std::uint64_t>(parsed);
                        budget.has_node_limit = true;
                    }
                } catch (const std::exception&) {
                    // Malformed value: same "just ignore it" tolerance
                    // next_int() already gives every other numeric
                    // sub-option (this function's own header comment on
                    // "a GUI or script sending a slightly malformed ...
                    // shouldn't crash the engine").
                }
            }
        } else if (tok == "mate") {
            // `go mate <n>`: no dedicated mate-search mode (ROADMAP.md
            // item 5b's own scoping note on this exact design
            // question) -- treated as a depth ceiling of 2*n plies,
            // enough for iterative deepening's own existing mate-
            // distance handling (kMateThreshold/kMateScore, search.h)
            // to find and report a mate in `n` moves if one exists at
            // that depth, the same minimal treatment most classical
            // engines give this option rather than a dedicated search
            // mode. Combining `mate` with `wtime`/`btime` behaves
            // exactly like combining `depth` with them already does
            // (the `have_depth` branch immediately below takes
            // priority, ignoring wtime/btime's own time budget
            // entirely) -- consistent with, not a new exception to,
            // that existing precedent.
            const int mate_in_n = next_int();
            if (mate_in_n > 0) {
                budget.max_depth = 2 * mate_in_n;
                have_depth = true;
            }
        } else if (tok == "searchmoves") {
            // Consumes every following token up to the next recognized
            // `go` keyword or the end of the command (is_go_keyword()'s
            // own doc comment above) -- each one resolved against `pos`
            // the same way apply_uci_moves() resolves a `position ...
            // moves` token: matched by Move::to_uci() against the
            // position's own legal move list, generated once before
            // this inner loop rather than once per token. A token that
            // doesn't match any legal move is silently skipped (same
            // per-token tolerance apply_uci_moves() itself uses for a
            // malformed move list, this function's own header comment)
            // rather than aborting the rest of the `searchmoves` list
            // over one bad entry.
            board::MoveList legal;
            board::generate_legal_moves(pos, legal);
            while (i + 1 < tokens.size() && !is_go_keyword(tokens[i + 1])) {
                ++i;
                for (const board::Move& m : legal) {
                    if (m.to_uci() == tokens[i]) {
                        budget.searchmoves.push_back(m);
                        break;
                    }
                }
            }
        }
    }

    if (have_depth) {
        if (budget.max_depth < 1) {
            budget.max_depth = kNoTimeControlDepth; // Guard against a malformed "depth 0"/negative value.
        }
        // time_limit_ms is whatever movetime gave (possibly 0, i.e. no limit).
        // soft_time_limit_ms stays 0 -- SearchBudget's own doc comment on why.
    } else if (have_movetime) {
        budget.max_depth = kTimedSearchMaxDepth;
        // time_limit_ms is already set from movetime above.
        // soft_time_limit_ms stays 0 -- SearchBudget's own doc comment on why.
    } else {
        const bool white_to_move = pos.side_to_move == board::Color::White;
        const int remaining = white_to_move ? wtime : btime;
        if (remaining >= 0) {
            const int increment = white_to_move ? winc : binc;
            const TimeAllocation alloc = allocate_time_ms(remaining, increment, movestogo);
            budget.soft_time_limit_ms = alloc.soft_ms;
            budget.time_limit_ms = alloc.hard_ms;
            budget.max_depth = kTimedSearchMaxDepth;
        } else {
            // Neither `depth` nor `movetime` nor a usable `wtime`/
            // `btime` -- covers both an explicit `go infinite` and a
            // bare `go` with no sub-options at all. Runs genuinely
            // unbounded (kTimedSearchMaxDepth ceiling, no time budget)
            // until an async `stop` arrives, same as `go ponder`
            // already does -- see kNoTimeControlDepth's own doc comment
            // above for why this no longer falls back to a shallow
            // fixed depth the way it did before `go` ran asynchronously.
            budget.max_depth = kTimedSearchMaxDepth;
            budget.time_limit_ms = 0;
            budget.unbounded = true;
        }
    }

    if (budget.time_limit_ms > 0) {
        budget.time_limit_ms -= move_overhead_ms;
        if (budget.time_limit_ms < 1) {
            budget.time_limit_ms = 1;
        }
    }
    if (budget.soft_time_limit_ms > 0) {
        budget.soft_time_limit_ms -= move_overhead_ms;
        if (budget.soft_time_limit_ms < 1) {
            budget.soft_time_limit_ms = 1;
        }
        // Move Overhead could, in principle, be large enough relative
        // to a tiny soft budget to push soft above hard after both were
        // independently clamped to >= 1 above -- keep the invariant
        // "soft <= hard" (SearchBudget's/TimeAllocation's own doc
        // comments) true after overhead is applied too, not just before.
        if (budget.soft_time_limit_ms > budget.time_limit_ms) {
            budget.soft_time_limit_ms = budget.time_limit_ms;
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
/// when MultiPV > 1. start_go() (below) calls this function once per
/// line reported by a MultiPV search's `on_iteration` (each with its
/// own correct `multipv_index` already set by
/// search_iterative_deepening_multipv(), search.cpp) exactly as it
/// already calls it once per depth for an ordinary single-line search
/// -- this function itself needs no MultiPV-specific branching at all,
/// since every field it reads already carries the right per-line
/// values regardless of which path produced them.
/// `seldepth`/`time`/`nps`/`hashfull` (ROADMAP.md Priority Fixes,
/// 2026-09-22, finding 9): added alongside the fields above, in the
/// same relative position Stockfish and most other UCI engines place
/// them (`depth seldepth ... time ... nodes ... nps ... hashfull ...
/// pv`), since a GUI's own `info`-line parser is generally tolerant of
/// field order but real tooling (cutechess and similar) that greps for
/// a specific field by name benefits from a conventional layout, not a
/// novel one. `seldepth` falls back to `result.depth_completed` for a
/// SearchResult produced without seldepth tracking
/// (SearchResult::seldepth's own doc comment covers exactly when that
/// happens: only the mandatory depth-1 iteration today), rather than
/// emitting a misleading `seldepth 0`. `nps` is derived here, not
/// stored on SearchResult itself, from `nodes`/`elapsed_ms` --
/// `elapsed_ms == 0` (possible for a depth-1 result that completes
/// within the same millisecond it started, on a fast position) omits
/// `nps` entirely rather than dividing by zero or reporting a
/// meaningless value. `tt`: `nullptr` (the default) omits `hashfull`
/// entirely rather than reporting a meaningless 0 -- every real call
/// site in this file has a live TranspositionTable to pass; the
/// default only exists so test code calling this function directly
/// (tests/uci_tests.cpp) isn't forced to construct one just to check
/// the other fields.
void emit_info(const search::SearchResult& result, std::ostream& out,
               const search::TranspositionTable* tt = nullptr) {
    const int seldepth = result.seldepth > 0 ? result.seldepth : result.depth_completed;
    out << "info depth " << result.depth_completed << " seldepth " << seldepth << " multipv "
        << result.multipv_index << " score ";
    if (result.score >= search::kMateThreshold) {
        const int plies_to_mate = search::kMateScore - result.score;
        out << "mate " << (plies_to_mate + 1) / 2;
    } else if (result.score <= -search::kMateThreshold) {
        const int plies_to_mate = search::kMateScore + result.score;
        out << "mate " << -((plies_to_mate + 1) / 2);
    } else {
        out << "cp " << result.score;
    }
    out << " nodes " << result.nodes;
    if (result.elapsed_ms > 0) {
        out << " nps " << (result.nodes * 1000ULL) / result.elapsed_ms;
    }
    out << " time " << result.elapsed_ms;
    if (tt != nullptr) {
        out << " hashfull " << tt->hashfull();
    }
    out << " pv";
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

/// Picks the ponder move to advertise alongside `bestmove` — ROADMAP.md
/// Priority Fixes (2026-09-22), item 3 (finding 4): both `bestmove`
/// call sites below previously wrote `bestmove <move>` alone, with no
/// `ponder <move>` token at all, making the advertised `Ponder` UCI
/// option (`option name Ponder`, handle_setoption() above) unusable by
/// any real GUI — a GUI can only start pondering on a move THIS engine
/// names, never one it guesses itself.
///
/// `result`'s own `multipv_lines` (SearchResult's own doc comment) is
/// searched for the line whose `best_move` equals `move_to_play`.
/// Ordinarily that's just `result` itself (`multi_pv == 1`, or skill
/// limiting off, in which case `multipv_lines` is empty and the search
/// below never runs) — but `search::pick_skill_move()` (search/skill.h)
/// can choose a move belonging to a DIFFERENT MultiPV line than
/// `result.best_move`/`result.pv` describes, and that other line's own
/// `pv` — not `result.pv` — is the one whose second entry is the
/// genuine, actually-searched reply to `move_to_play` specifically.
/// Falls back to `result.pv` itself whenever `multipv_lines` is empty
/// or (defensively — shouldn't happen for any real caller) no line's
/// own `best_move` matches.
///
/// Returns a null move (`Move::is_null() == true`) whenever the chosen
/// line's own `pv` has fewer than 2 entries — e.g. `move_to_play`
/// itself delivers checkmate, so there's no reply to ponder on, or the
/// TT-walk PV reconstruction (SearchResult::pv's own doc comment) came
/// back too short to have a second move at all. Both `bestmove` call
/// sites below treat a null return as "omit the `ponder` token
/// entirely," never as `ponder 0000` — the UCI spec's `ponder` token
/// always names a real move, unlike `bestmove`'s own `0000` convention
/// for "no move."
[[nodiscard]] Move ponder_move_for(const search::SearchResult& result, const Move& move_to_play) {
    const std::vector<Move>* pv = &result.pv;
    for (const search::SearchResult& line : result.multipv_lines) {
        if (line.best_move == move_to_play) {
            pv = &line.pv;
            break;
        }
    }
    if (pv->size() < 2) {
        return Move();
    }
    return (*pv)[1];
}

/// Fills `tt` with a TranspositionTable sized to `requested_mb`, with
/// the same graceful, halve-and-retry std::bad_alloc fallback
/// search::make_transposition_table() gives every top-level search
/// call's own private table (search/search.h's own doc comment on that
/// function) -- constructing directly via std::optional::emplace()'s
/// plain std::size_t argument rather than move-constructing from an
/// already-built temporary, since TranspositionTable (tt.h's own
/// atomic-member layout) is neither copyable nor movable. Deliberately
/// a small, self-contained duplicate of search.cpp's own identically-
/// shaped internal helper rather than a shared one exposed across
/// files -- matching this project's own established convention of
/// duplicating small, stable helpers instead of coupling two files
/// together over them (e.g. contempt_draw_score(), duplicated verbatim
/// between search.cpp and quiescence.cpp). Used both to build run()'s
/// own persistent, engine-lifetime TranspositionTable (ROADMAP.md
/// Priority Fixes, 2026-09-08, "Persistent, engine-lifetime
/// transposition table") at startup and to rebuild it fresh whenever
/// `setoption name Hash value <N>` changes its size -- see run()'s own
/// use of this function, below.
void emplace_persistent_tt(std::optional<search::TranspositionTable>& tt,
                            std::size_t requested_mb) {
    std::size_t size_mb = requested_mb < 1 ? 1 : requested_mb;
    while (true) {
        try {
            tt.emplace(size_mb);
            return;
        } catch (const std::bad_alloc&) {
            if (size_mb <= 1) {
                throw; // Nothing smaller left to try -- let it propagate.
            }
            size_mb /= 2;
            if (size_mb < 1) {
                size_mb = 1;
            }
        }
    }
}

/// Handles `setoption name <name...> value <value...>`. Recognized
/// option names with real behavioral effect: `Threads` (ROADMAP.md
/// Phase 7, unchanged from Session 74), `Hash`, `Move Overhead`, and
/// `MultiPV` (ROADMAP.md Phase 8's "Full UCI option set" item --
/// kMinHashMB/kMaxHashMB, kMinMoveOverheadMs/kMaxMoveOverheadMs, and
/// kMinMultiPV/kMaxMultiPV's own doc comments above have each option's
/// full rationale), and `Skill Level` (ROADMAP.md Phase 8's own
/// separate "Skill level / strength limiting" item -- src/search/
/// skill.h's own header comment has the full design; kMinSkillLevel/
/// kMaxSkillLevel bound it the same way the options above bound
/// themselves), and `Contempt` (ROADMAP.md Phase 8's own separate
/// "Contempt / draw score adjustment" item -- src/search/search.h's
/// own search_iterative_deepening()/search_fixed_depth() doc comments
/// on their `contempt_cp` parameter have the full design;
/// kMinContemptCp/kMaxContemptCp bound it the same way). `Ponder`
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
                       int& multi_pv, int& skill_level, int& contempt_cp,
                       const std::vector<std::string>& tokens) {
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
    } else if (name == "Skill Level") {
        try {
            int value = std::stoi(tokens[value_start]);
            if (value < search::kMinSkillLevel) {
                value = search::kMinSkillLevel;
            } else if (value > search::kMaxSkillLevel) {
                value = search::kMaxSkillLevel;
            }
            skill_level = value;
        } catch (const std::exception&) {
            // Non-integer value -- ignore, leaving skill_level unchanged.
        }
    } else if (name == "Contempt") {
        try {
            int value = std::stoi(tokens[value_start]);
            if (value < kMinContemptCp) {
                value = kMinContemptCp;
            } else if (value > kMaxContemptCp) {
                value = kMaxContemptCp;
            }
            contempt_cp = value;
        } catch (const std::exception&) {
            // Non-integer value -- ignore, leaving contempt_cp unchanged.
        }
    }
    // Any other option name: silently ignored (this function's own doc comment).
}

/// All state for one in-flight ordinary (non-ponder) `go` search,
/// running asynchronously on its own thread (ROADMAP.md Priority Fixes,
/// 2026-09-22, item 1 -- "Asynchronous `go` with working `stop`/
/// `isready`/`quit`"). Mirrors PonderState's own shape closely below
/// (thread/stop/suppress_output, joinable()-means-in-flight convention)
/// -- deliberately a SEPARATE struct rather than folded into
/// PonderState itself, since an ordinary `go` and `go ponder` differ in
/// enough ways (an ordinary `go` always emits `info` lines via
/// on_iteration; consults the opening book first; has no
/// ponderhit-driven saved-budget handoff, so needs no `active`/
/// `saved_time_limit_ms` fields at all) that sharing one struct would
/// mean threading an `is_ponder` bool through every function below to
/// re-derive behavior that stays unambiguous when the two are kept
/// separate. Only one of GoState/PonderState is ever active at a time
/// in practice (a compliant GUI never sends `go` while `go ponder` is
/// still outstanding, or vice versa) -- run()'s own dispatch below still
/// defensively abandons whichever one might be running before starting
/// the other, exactly as it already abandons pondering before
/// `position`/`ucinewgame`.
struct GoState {
    /// See PonderState::thread's own doc comment above -- identical
    /// role and `joinable()`-means-"a search is in flight" convention.
    std::thread thread;

    /// See PonderState::stop's own doc comment above -- identical role:
    /// checked by search::search_iterative_deepening()'s own
    /// `external_stop` parameter, both between and mid-iteration
    /// (search.h's doc comment on that parameter). This is the actual
    /// fix for finding 1's "`stop` sent while an ordinary `go` is in
    /// flight is parsed but has no effect" -- a real `stop` now reaches
    /// a real, checked flag instead of arriving after `go` already
    /// returned.
    std::atomic<bool> stop{false};

    /// See PonderState::suppress_output's own doc comment above --
    /// identical role: true only for abandon_go()'s own defensive path
    /// below (an out-of-protocol command arriving mid-search), never
    /// for a genuine `stop` (handle_go_stop() below), which -- per the
    /// UCI spec's own requirement, same as pondering's `stop` path --
    /// must still produce a `bestmove`.
    std::atomic<bool> suppress_output{false};

    /// True only for a genuinely unbounded search -- `go infinite` or a
    /// bare `go` with no usable depth/time control at all
    /// (compute_search_budget()'s own final `else` branch above) -- set
    /// by start_go() from that budget and read only by finish_go()
    /// below, to decide what "the input stream simply ended, or `quit`
    /// arrived, with this `go` still running and no `stop` ever sent"
    /// should mean. For every BOUNDED search (an explicit `depth`,
    /// `movetime`, or `wtime`/`btime`), that situation means "let it
    /// finish naturally and report the `bestmove` it would have found
    /// anyway" -- byte-for-byte the same observable behavior `go` had
    /// before this item, when it ran synchronously and unconditionally
    /// ran to completion before this loop could read `quit`/hit
    /// end-of-input at all. For a genuinely UNBOUNDED search, joining
    /// unconditionally the same way would mean `quit`/end-of-input could
    /// hang forever waiting on a search nothing will ever stop --
    /// exactly the pre-existing, accepted trade-off
    /// finish_pondering()/abandon_pondering() already make for an
    /// unbounded ponder search that never received a `ponderhit`/`stop`
    /// either.
    bool unbounded = false;
};

/// Unconditionally stops and joins any in-flight ordinary `go` search,
/// discarding its result (never writing `bestmove` or any further
/// `info` line) -- the GoState counterpart of abandon_pondering() above,
/// same rationale: a compliant GUI always sends `stop` before issuing
/// `position`/`ucinewgame`/another `go`/`go ponder` while a search is
/// still running, so this path only ever fires on an out-of-protocol
/// command sequence, handled by discarding gracefully rather than
/// crashing or leaving two searches writing to `out` concurrently. A
/// no-op if nothing is running. Called from run() ahead of
/// `position`/`ucinewgame`/`setoption name Hash`/a fresh `go ponder`
/// (mirroring abandon_pondering()'s own call sites for the reverse
/// direction), from start_go() itself (defensive, mirroring
/// start_pondering()'s own leading abandon_pondering() call), and once,
/// unconditionally, right before run() returns -- std::thread's
/// destructor calls std::terminate() on a still-joinable thread, so
/// this is a hard correctness requirement, not just tidiness.
void abandon_go(GoState& go) {
    if (!go.thread.joinable()) {
        return;
    }
    go.suppress_output.store(true, std::memory_order_relaxed);
    go.stop.store(true, std::memory_order_relaxed);
    go.thread.join();
}

/// Handles `go [depth N] [movetime N] [wtime W btime B [winc I]
/// [binc I] [movestogo N]]` (any combination; unrecognized sub-options
/// like `infinite`/`ponder`/`mate`/`nodes` are accepted but ignored --
/// see this file's header comment) by launching the search on
/// `go.thread` and returning immediately -- ROADMAP.md Priority Fixes,
/// 2026-09-22, item 1. run()'s own command loop keeps reading further
/// lines while the search runs, exactly as it already does for `go
/// ponder` (start_pondering() above) -- the whole point being that a
/// later `stop`/`isready` genuinely reaches this loop while the search
/// is still in flight, rather than only being read once `go` has
/// already finished and printed `bestmove` (this file's own header
/// comment has the full before/after).
///
/// The background thread writes one `info depth ... score ... nodes ...
/// pv ...` line per completed iteration (emit_info(), above, via
/// `on_iteration`) as it goes, then writes `bestmove <uci>` once the
/// search returns -- byte-for-byte the same output an equivalent
/// synchronous call would have produced, just no longer blocking this
/// loop's own command reading while it happens. Every write to `out`,
/// on this thread and on the main thread alike, is serialized through
/// `out_mutex` (run()'s own local, passed down by reference here and
/// into start_pondering() the same way) so a `readyok`/`uciok`/etc.
/// written by the main thread while this search's own `info`/`bestmove`
/// write is still in progress can never interleave mid-line with it.
///
/// `pos` and `game_history` are copied into the background thread's own
/// closure rather than captured by reference -- the identical reason
/// start_pondering() already copies `pos`/`game_history` into ITS
/// closure (that function's own doc comment): both are run()'s own
/// locals, which could in principle be touched again by a later
/// out-of-protocol command on the main thread (handled defensively by
/// abandon_go() at those call sites) while this search is still
/// running on its own thread.
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
/// lifetime state as the three parameters above -- this function's own
/// USER-FACING MultiPV behavior (which `info multipv N` lines get
/// reported) always matches this value exactly, but the number of
/// lines actually REQUESTED from search_iterative_deepening() below may
/// be silently raised above it when skill limiting is active -- see
/// `skill_level`'s own doc comment just below for why, and
/// search::skill_search_multipv()'s own doc comment (search/skill.h)
/// for the exact rule. When skill limiting is OFF (the default), this
/// function's behavior for any `multi_pv` value is completely
/// unaffected by that mechanism: `on_iteration` below fires once per
/// line exactly as before, each with the right `multipv_index` already
/// set (search_iterative_deepening_multipv()'s own doc comment,
/// search.cpp), and emit_info() already emits the right `multipv N`
/// token per call (that function's own doc comment).
///
/// `skill_level`/`skill_rng` (ROADMAP.md Phase 8, "Skill level /
/// strength limiting" -- src/search/skill.h's own header comment has
/// the full design): `skill_level` is run()'s own session-lifetime
/// state, set via `setoption name Skill Level value <N>`
/// (handle_setoption() above) and defaulting to search::kMaxSkillLevel
/// (no limiting at all). `skill_rng` is a single generator owned by
/// run() for its whole session lifetime (seeded once from a genuine
/// entropy source, not reset by `ucinewgame` — a real game's own move
/// choices should keep drawing from one advancing stream, not restart
/// predictably every new game) — passed down here by pointer and
/// captured into the background thread's own closure, since a
/// reference can't be captured across a detached-lifetime std::thread
/// the way start_pondering() never needed to for the same field
/// (pondering never calls pick_skill_move() -- start_pondering()'s own
/// doc comment). Safe because abandon_go() always joins any previous
/// `go` thread before a new one starts (this function's own leading
/// call), so no two background threads ever touch `skill_rng`
/// concurrently, and nothing on the main thread touches it while one is
/// in flight either. At the default skill level, this function's
/// behavior — including `skill_rng`'s own state — is completely
/// unaffected by either parameter's existence: search::
/// skill_search_multipv() returns `multi_pv` unchanged, and search::
/// pick_skill_move() (called below) draws nothing from `skill_rng` at
/// all in that case (both functions' own doc comments, search/skill.h).
///
/// `contempt_cp` (ROADMAP.md Phase 8, "Contempt / draw score
/// adjustment"): run()'s own session-lifetime state, set via
/// `setoption name Contempt value <N>` (handle_setoption() above) and
/// defaulting to 0 (no adjustment). Passed straight through as
/// search::search_iterative_deepening()'s own identically-named
/// parameter (search/search.h's doc comment has the full contract) --
/// at the default value, this function's behavior is completely
/// unaffected, exactly as if this parameter didn't exist.
///
/// `persistent_tt` (ROADMAP.md Priority Fixes, 2026-09-08, "Persistent,
/// engine-lifetime transposition table"): run()'s own single
/// TranspositionTable, constructed once at startup and rebuilt only
/// when `setoption name Hash` genuinely changes its size (run()'s own
/// comments at that table's declaration) -- a pointer to it, not the
/// table itself, is captured by value into the background thread's own
/// closure below (the identical reason start_pondering() already does
/// this for the same object -- that function's own doc comment). Safe
/// under concurrent access by construction (tt.h's own THREAD-SAFETY
/// NOTE), same guarantee Lazy SMP and pondering already rely on. The one
/// genuine hazard -- `setoption name Hash` rebuilding this object out
/// from under a still-running `go` -- is handled at run()'s own
/// `setoption` dispatch by calling abandon_go() (alongside
/// abandon_pondering()) before the rebuild, exactly as it already does
/// for pondering.
void start_go(Position& pos, const std::vector<std::uint64_t>& game_history,
              const std::vector<std::string>& tokens, int num_threads, std::size_t hash_size_mb,
              int move_overhead_ms, int multi_pv, int skill_level, std::mt19937_64& skill_rng,
              int contempt_cp, search::TranspositionTable& persistent_tt, std::ostream& out,
              std::mutex& out_mutex, GoState& go) {
    abandon_go(go); // Defensive: see this function's own doc comment above.

    // Opening book (src/book/book.h, ROADMAP.md's optional "small
    // curated opening book" item): consulted synchronously, on the
    // calling (main) thread, before any background thread is even
    // created -- a book hit answers immediately, with no search and no
    // `info` line, exactly matching this function's pre-async behavior
    // for this case, and avoids spinning up a thread that would do
    // nothing anyway.
    const std::optional<std::string> book_move = book::book_move(pos);
    if (book_move.has_value()) {
        std::lock_guard<std::mutex> lock(out_mutex);
        out << "bestmove " << *book_move << '\n';
        out.flush();
        return;
    }

    const SearchBudget budget = compute_search_budget(pos, tokens, move_overhead_ms);

    // `search::skill_search_multipv()` (search/skill.h): returns
    // `multi_pv` unchanged when skill limiting is off (the default) --
    // see this function's own doc comment above.
    const int search_multi_pv = search::skill_search_multipv(skill_level, multi_pv);

    go.stop.store(false, std::memory_order_relaxed);
    go.suppress_output.store(false, std::memory_order_relaxed);
    // See GoState::unbounded's own doc comment above.
    go.unbounded = budget.unbounded;

    Position go_pos = pos;
    std::vector<std::uint64_t> go_history = game_history;
    std::atomic<bool>* stop_ptr = &go.stop;
    std::atomic<bool>* suppress_ptr = &go.suppress_output;
    search::TranspositionTable* tt_ptr = &persistent_tt;
    std::mt19937_64* skill_rng_ptr = &skill_rng;
    std::mutex* out_mutex_ptr = &out_mutex;

    go.thread = std::thread([&out, out_mutex_ptr, go_pos, go_history, num_threads, hash_size_mb,
                              search_multi_pv, budget, skill_level, contempt_cp, stop_ptr,
                              suppress_ptr, tt_ptr, skill_rng_ptr]() mutable {
        // `material_weights`/`eval_weights` stay the compiled-in-
        // constants default (nullptr) -- no UCI option exists to
        // override either (search_fixed_depth()'s own doc comment
        // covers their real use, the Texel/SPSA tuner, not this UCI
        // loop). `external_stop=stop_ptr` is the actual finding-1 fix
        // -- see GoState::stop's own doc comment above.
        const search::SearchResult result = search::search_iterative_deepening(
            go_pos, budget.max_depth, budget.time_limit_ms, go_history,
            [&out, out_mutex_ptr, suppress_ptr, tt_ptr](const search::SearchResult& iteration_result) {
                // Skip a stale `info` line for a search abandon_go()
                // already discarded -- the same suppression
                // start_pondering()'s own lambda applies to its final
                // `bestmove`, extended here to every intermediate line
                // too, since an ordinary `go` (unlike pondering) emits
                // them.
                if (suppress_ptr->load(std::memory_order_relaxed)) {
                    return;
                }
                std::lock_guard<std::mutex> lock(*out_mutex_ptr);
                emit_info(iteration_result, out, tt_ptr);
            },
            /*material_weights=*/nullptr, /*eval_weights=*/nullptr, num_threads, stop_ptr,
            hash_size_mb, search_multi_pv, budget.soft_time_limit_ms, contempt_cp, tt_ptr,
            budget.has_node_limit ? budget.max_nodes : 0, budget.searchmoves);

        if (suppress_ptr->load(std::memory_order_relaxed)) {
            return;
        }

        // `search::pick_skill_move()` (search/skill.h): returns
        // `result.best_move` unchanged, drawing nothing from
        // `*skill_rng_ptr`, whenever skill limiting is off or
        // `result.multipv_lines` is empty (the latter happening
        // whenever the root position had one or zero legal moves
        // regardless of `search_multi_pv` -- SearchResult::
        // multipv_lines' own doc comment, search.h) -- see this
        // function's own doc comment above. The `info` lines already
        // emitted above always report the engine's own genuine,
        // full-strength analysis of every line regardless of which one
        // ends up chosen here -- only the final `bestmove` below is
        // ever affected by skill limiting (src/search/skill.h's own
        // header comment on this exact point).
        const board::Move move_to_play =
            result.multipv_lines.empty()
                ? result.best_move
                : search::pick_skill_move(result.multipv_lines, skill_level, *skill_rng_ptr);

        std::lock_guard<std::mutex> lock(*out_mutex_ptr);
        out << "bestmove ";
        if (move_to_play.is_null()) {
            // No legal move (checkmate/stalemate at the root) -- "0000"
            // is the conventional UCI null-move token GUIs recognize;
            // there's no other clean way to say "no move" via bestmove.
            out << "0000";
        } else {
            out << move_to_play.to_uci();
            // See ponder_move_for()'s own doc comment above -- omitted
            // entirely (not `ponder 0000`) whenever there's no genuine
            // second move to offer.
            const Move ponder_move = ponder_move_for(result, move_to_play);
            if (!ponder_move.is_null()) {
                out << " ponder " << ponder_move.to_uci();
            }
        }
        out << '\n';
        out.flush();
    });
}

/// Handles `stop` while an ordinary (non-ponder) `go` is in flight --
/// the GoState counterpart of handle_stop() above, same "discard and
/// restart on `stop` + actual move" shape: this function performs the
/// "discard" half (stopping the search and letting its own thread still
/// print the `bestmove` the UCI spec requires even on `stop`, matching
/// handle_stop()'s own rationale for pondering); the "restart" half is
/// simply the GUI's own next `go`, unaffected by anything here.
///
/// A no-op if no ordinary `go` search is actually active (`stop`
/// arriving with nothing running, or one already finished on its own --
/// e.g. it hit its own time/depth limit before `stop` arrived) -- same
/// defensive robustness convention as every other command handler in
/// this file. Blocks until the background thread has actually stopped
/// and printed its `bestmove` (`thread.join()`) -- matching
/// handle_stop()'s own synchronous behavior for pondering, and this
/// file's existing one-command-fully-handled-before-the-next-line-is-
/// read convention for every command besides `go`/`go ponder`
/// themselves. This block is expected to be brief: the search checks
/// `go.stop` at least every 2048 nodes (search.h's own SearchLimits doc
/// comment), so `stop` reaching an actually-running search resolves
/// near-instantly.
void handle_go_stop(GoState& go) {
    if (!go.thread.joinable()) {
        return;
    }
    go.stop.store(true, std::memory_order_relaxed);
    go.thread.join();
}

/// Handles run()'s own tail end -- `quit`, or `in` simply running out of
/// lines -- with an ordinary `go` possibly still in flight and never
/// explicitly `stop`ped. The GoState counterpart of finish_pondering()
/// above, same two-case shape, keyed off GoState::unbounded instead of
/// PonderState::active (that field's own doc comment has the full
/// rationale): a BOUNDED search (explicit `depth`/`movetime`/
/// `wtime`/`btime`) is joined -- let it finish naturally and print its
/// `bestmove` -- reproducing, byte-for-byte, this exact scenario's
/// observable behavior from before this item, when `go` ran
/// synchronously and always ran to completion before this loop could
/// even read `quit`/reach end-of-input. A genuinely UNBOUNDED search
/// (`go infinite`/a bare `go`, never `stop`ped) is abandoned instead --
/// joining unconditionally here would mean `quit`/end-of-input could
/// hang forever on a search nothing will ever stop, the exact hazard
/// abandon_pondering()'s own unbounded-ponder-search case already
/// avoids the identical way. A no-op if no ordinary `go` is actually in
/// flight when this runs.
void finish_go(GoState& go) {
    if (!go.thread.joinable()) {
        return;
    }
    if (go.unbounded) {
        abandon_go(go);
    } else {
        go.thread.join();
    }
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
/// ordinary `go` before this fix (formerly fully synchronous), this launches
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
/// book (src/book/book.h) the way start_go() does — pondering on a
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
/// state start_go() consumes -- `hash_size_mb` is passed straight
/// through to this call's own search_iterative_deepening() the same
/// way, though it's ignored whenever `persistent_tt` below is supplied
/// (search_iterative_deepening()'s own `external_tt`-vs-`hash_size_mb`
/// contract, search.h); `move_overhead_ms` feeds into
/// compute_search_budget() below when computing the REAL move's saved
/// budget for handle_ponderhit() to apply later, exactly as it would
/// for an ordinary `go` from this same position.
///
/// `persistent_tt` (ROADMAP.md Priority Fixes, 2026-09-08, "Persistent,
/// engine-lifetime transposition table"): run()'s own single
/// TranspositionTable, the SAME object start_go() uses for an
/// ordinary `go` -- a pointer to it, not the table itself, is captured
/// by value into the background thread's own closure below (a raw
/// pointer copies trivially and is exactly what
/// search_iterative_deepening()'s own `external_tt` parameter expects;
/// TranspositionTable itself is neither copyable nor movable, tt.h's
/// own atomic-member layout, so capturing anything other than a
/// pointer/reference to it wouldn't compile). Sharing this one object
/// between the pondering thread and whatever real `go` eventually
/// follows is the whole point of pondering finding anything useful at
/// all -- entries the ponder search stores land in the SAME table an
/// ordinary `go` right after `ponderhit` will probe. Safe under
/// concurrent access by construction (tt.h's own THREAD-SAFETY NOTE --
/// the exact same lock-free guarantee Lazy SMP's helper threads already
/// rely on to share one table with the main search thread). The one
/// genuine hazard -- a `setoption name Hash` REBUILDING this object out
/// from under a still-running ponder thread -- is handled at run()'s
/// own `setoption` dispatch, not here: see that dispatch's own comment
/// for why it calls abandon_pondering() first whenever `Hash` actually
/// changes size.
///
/// `budget.soft_time_limit_ms` (ROADMAP.md Phase 8, "Time management")
/// is computed by compute_search_budget() below (same as start_go()'s
/// own call) but deliberately NOT threaded into this function's own
/// actual background search_iterative_deepening() call just below --
/// that call already passes `time_limit_ms=0` (unbounded) and relies
/// entirely on `external_stop`, itself only ever set by a real `stop`
/// command or handle_ponderhit()'s own fixed-delay watchdog thread
/// (that function's own doc comment) — an entirely different stopping
/// mechanism than the iterative-deepening loop's own soft/hard-budget
/// check (search.h's `soft_time_limit_ms` doc comment), which measures
/// elapsed time from THIS call's own start, not from whenever
/// `ponderhit` eventually arrives. Reconciling the two -- e.g. letting
/// a ponder search stop itself early once stable, ahead of the
/// watchdog's own fixed delay -- is real, legitimate future work, not
/// something to improvise here; `budget.soft_time_limit_ms` is
/// discarded (unused) at this specific call site for now, an accepted
/// scope limit.
void start_pondering(Position& pos, const std::vector<std::uint64_t>& game_history,
                      const std::vector<std::string>& tokens, int num_threads,
                      std::size_t hash_size_mb, int move_overhead_ms,
                      search::TranspositionTable& persistent_tt, std::ostream& out,
                      std::mutex& out_mutex, PonderState& ponder) {
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
    search::TranspositionTable* tt_ptr = &persistent_tt;
    std::mutex* out_mutex_ptr = &out_mutex;

    ponder.thread = std::thread([&out, out_mutex_ptr, num_threads, hash_size_mb, ponder_pos,
                                  ponder_history, stop_ptr, suppress_ptr, tt_ptr]() mutable {
        // `budget`'s own `has_node_limit`/`max_nodes`/`searchmoves`
        // (ROADMAP.md Priority Fixes, 2026-09-22, finding 9) are NOT
        // forwarded to this background search -- pondering already
        // deliberately ignores `budget.max_depth`/`time_limit_ms` too
        // (kTimedSearchMaxDepth/0 just below, genuinely unbounded by
        // design until `ponderhit` applies the SAVED budget -- this
        // function's own header comment), and no test or real GUI usage
        // combines `go ponder` with `nodes`/`searchmoves` today; a
        // deliberate scope limit, matching search_iterative_deepening()'s
        // own MultiPV one, not an oversight.
        const search::SearchResult result = search::search_iterative_deepening(
            ponder_pos, kTimedSearchMaxDepth, /*time_limit_ms=*/0, ponder_history,
            /*on_iteration=*/nullptr, /*material_weights=*/nullptr, /*eval_weights=*/nullptr,
            num_threads, stop_ptr, hash_size_mb, /*multi_pv=*/1, /*soft_time_limit_ms=*/0,
            /*contempt_cp=*/0, tt_ptr);
        if (suppress_ptr->load(std::memory_order_relaxed)) {
            return;
        }
        // Guarded by the same `out_mutex` start_go() now uses (ROADMAP.md
        // Priority Fixes, 2026-09-22, item 1) -- an ordinary `go`'s own
        // `info`/`bestmove` writes and this pondering thread's own
        // `bestmove` write can now genuinely race against each other and
        // against the main thread's own writes (e.g. `readyok`) in ways
        // they couldn't before `go` ran asynchronously; this lock keeps
        // every write to `out` a single, uninterleaved line regardless
        // of which thread produces it.
        std::lock_guard<std::mutex> lock(*out_mutex_ptr);
        out << "bestmove ";
        if (result.best_move.is_null()) {
            out << "0000";
        } else {
            out << result.best_move.to_uci();
            // See ponder_move_for()'s own doc comment above -- this
            // search always runs at multi_pv=1 (this function's own
            // call into search_iterative_deepening() a few lines up),
            // so multipv_lines is always empty here and the fallback to
            // result.pv is the only path ever taken -- still routed
            // through the shared helper rather than duplicated, so both
            // bestmove sites stay in sync if that ever changes.
            const Move ponder_move = ponder_move_for(result, result.best_move);
            if (!ponder_move.is_null()) {
                out << " ponder " << ponder_move.to_uci();
            }
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
/// fresh `go` — ordinary, now-asynchronous start_go(), unaffected by
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

void run_bench(std::ostream& out) {
    std::uint64_t total_nodes = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (const auto& bench_pos : bench::kBenchPositions) {
        Position pos = board::parse_fen(bench_pos.fen);
        // Deliberately the plain, all-defaults call — single-threaded,
        // default Hash size — this function's own doc comment (uci.h)
        // has the full reproducibility rationale for why neither is
        // configurable here.
        const search::SearchResult result = search::search_fixed_depth(pos, bench::kBenchDepth);
        total_nodes += result.nodes;
        out << bench_pos.name << ": depth " << bench::kBenchDepth << " nodes " << result.nodes
            << " score " << result.score << " bestmove " << result.best_move.to_uci() << '\n';
    }

    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    // Guard against a division by zero on an implausibly fast run
    // (elapsed_ms == 0) -- reports 0 Nodes/second rather than crashing
    // or reporting a nonsensical infinite rate.
    const std::uint64_t nps =
        elapsed_ms > 0
            ? static_cast<std::uint64_t>(total_nodes) * 1000ULL / static_cast<std::uint64_t>(elapsed_ms)
            : 0;

    // Standard fishtest/OpenBench-parseable format (matches the
    // convention several established engines already use): external
    // tooling greps specifically for a "Nodes searched" line to extract
    // the total, so this exact label/spacing matters for real
    // interoperability, not just cosmetic — this could not be verified
    // against actual OpenBench/fishtest infrastructure from this
    // development environment (no access to either), so treat this
    // format as believed-correct-by-convention rather than confirmed
    // working end-to-end; see docs/ROADMAP.md's own note on this.
    out << "===========================\n";
    out << "Total time (ms) : " << elapsed_ms << '\n';
    out << "Nodes searched  : " << total_nodes << '\n';
    out << "Nodes/second    : " << nps << '\n';
    out.flush();
}

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
    // (start_go()), and -- unlike `pos`/`game_history` -- NOT reset by
    // `ucinewgame` below, matching the UCI convention that engine
    // OPTIONS persist across games within one session while game STATE
    // does not.
    int num_threads = kMinThreads;
    // `Hash`/`Move Overhead` UCI options' current values (ROADMAP.md
    // Phase 8, "Full UCI option set") -- session-lifetime state, exactly
    // like `num_threads` above: set via `setoption` (handle_setoption()),
    // read by every subsequent `go` (start_go()/start_pondering()), and
    // NOT reset by `ucinewgame` below, same "options persist, game state
    // doesn't" convention `num_threads` already follows.
    std::size_t hash_size_mb = search::kDefaultTTSizeMB;
    // Persistent, engine-lifetime transposition table (ROADMAP.md
    // Priority Fixes, 2026-09-08, "Persistent, engine-lifetime
    // transposition table" -- search/tt.h's own LIFETIME NOTE has the
    // full background on why every top-level search call previously
    // constructed its own fresh, private table instead). Constructed
    // once here, at whatever `hash_size_mb` starts at, and lives for
    // this whole run() call -- every ordinary `go` (start_go()) and
    // every `go ponder` (start_pondering()) below shares this SAME
    // object (a pointer to it, passed as search_iterative_deepening()'s
    // own `external_tt` parameter), so hash information genuinely
    // carries over from one move to the next within a real game, the
    // way a UCI engine playing an actual timed game is expected to --
    // rather than starting from an empty table on every single `go`.
    // `std::optional` (not a plain value) specifically because
    // TranspositionTable can't be reassigned in place (no copy/move
    // assignment -- tt.h's own atomic-member layout): `setoption name
    // Hash value <N>` below rebuilds this via emplace_persistent_tt()
    // (a fresh construction, replacing the old object outright) rather
    // than resizing anything in place, since TranspositionTable itself
    // has no such operation. `ucinewgame` below calls clear() on it
    // instead (a real clear, not a rebuild) -- a persistent table
    // carrying over an unrelated PREVIOUS game's positions into a new
    // one is exactly the stale-information case a UCI `ucinewgame`
    // exists to prevent, even though the option/table itself is
    // otherwise treated as session-lifetime state that `ucinewgame`
    // doesn't touch (same convention `num_threads`/`hash_size_mb`
    // themselves already follow just above).
    std::optional<search::TranspositionTable> persistent_tt;
    emplace_persistent_tt(persistent_tt, hash_size_mb);
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
    // `Skill Level` (ROADMAP.md Phase 8, "Skill level / strength
    // limiting" -- src/search/skill.h's own header comment has the full
    // design): same session-lifetime, `setoption`-driven,
    // not-reset-by-`ucinewgame` convention as `Hash`/`Move Overhead`/
    // `MultiPV` above. Defaults to search::kMaxSkillLevel -- "no
    // limiting at all" -- so a session that never sends `setoption name
    // Skill Level` sees zero behavioral change from before this option
    // existed. `skill_rng`: a single generator for this option's own
    // move-selection randomness (search::pick_skill_move(), called from
    // start_go() below), seeded once here from a genuine entropy
    // source and then left to advance move after move for the rest of
    // this session -- deliberately NOT reseeded by `ucinewgame` (a
    // fresh game restarting the SAME pseudo-random sequence every time
    // would make a "weak" opponent's own weaknesses eerily, unrealistically
    // repeatable move-for-move across games) and NOT threaded into
    // start_pondering() below, for the identical reason `multi_pv`
    // itself already isn't (comment just above): pondering's own
    // eventual `bestmove` is written by a background thread whose
    // result this file doesn't currently reconcile with skill limiting
    // at all, an accepted scope limit matching this same function's
    // existing MultiPV-during-pondering one.
    int skill_level = search::kMaxSkillLevel;
    std::mt19937_64 skill_rng(std::random_device{}());
    // `Contempt` (ROADMAP.md Phase 8, "Contempt / draw score adjustment"
    // -- src/search/search.h's own doc comments on `contempt_cp` have
    // the full design): same session-lifetime, `setoption`-driven,
    // not-reset-by-`ucinewgame` convention as every option above.
    // Defaults to 0 -- no adjustment -- so a session that never sends
    // `setoption name Contempt` sees zero behavioral change from before
    // this option existed. NOT threaded into start_pondering() below,
    // for the identical reason `skill_level`/`multi_pv` above already
    // aren't (those two parameters' own comments, just above).
    int contempt_cp = 0;
    // Pondering state (ROADMAP.md Phase 7) — session-lifetime, like
    // `num_threads` above, though its own contents (the background
    // thread, the stop flag) are reset per `go ponder` by
    // start_pondering() itself, not by `ucinewgame` here (a pondering
    // search is tied to one specific `go ponder` call, not the whole
    // session the way `Threads` is) — see PonderState's own doc comment.
    PonderState ponder;
    // Ordinary (non-ponder) `go` state (ROADMAP.md Priority Fixes,
    // 2026-09-22, item 1) -- session-lifetime, like `ponder` above,
    // though its own contents (the background thread, the stop flag)
    // are likewise reset per `go` by start_go() itself, not by
    // `ucinewgame` here — see GoState's own doc comment.
    GoState go;
    // Serializes every write to `out`, from this loop's own main-thread
    // command handling AND from `go`'s/`ponder`'s own background
    // threads (start_go()/start_pondering() above) -- necessary now
    // that an ordinary `go` runs asynchronously and can write `info`/
    // `bestmove` lines at arbitrary times relative to this loop's own
    // writes (e.g. `readyok` for an `isready` arriving mid-search);
    // without this, two concurrent writers to the same std::ostream
    // could interleave mid-line. A no-op in cost on the overwhelmingly
    // common single-writer-at-a-time path (an uncontended
    // std::mutex::lock() is cheap), and never contended across TWO
    // background threads at once, since abandon_go()/abandon_pondering()
    // always join whichever one might be running before the other
    // starts (this file's own established convention, extended here).
    std::mutex out_mutex;
    std::string line;

    while (std::getline(in, line)) {
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) {
            continue;
        }
        const std::string& cmd = tokens[0];

        if (cmd == "uci") {
            std::lock_guard<std::mutex> lock(out_mutex);
            // `NIGHTWING_VERSION_STRING` (ROADMAP.md Phase 8, "engine
            // info (name/author via `uci`)"): CMake-generated
            // (nightwing/version.h, top-level CMakeLists.txt's own
            // configure_file() comment) from this project's own
            // `project(nightwing VERSION ...)` declaration -- the
            // single source of truth for this number lives there, not
            // here.
            out << "id name Nightwing " << NIGHTWING_VERSION_STRING << '\n';
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
            // `Skill Level` (ROADMAP.md Phase 8, "Skill level / strength
            // limiting") -- same standard `spin` syntax; defaults to
            // search::kMaxSkillLevel (no limiting), the same value this
            // engine has effectively always played at before this
            // option existed, so a GUI/script that never touches it
            // sees no behavioral change. src/search/skill.h's own
            // header comment has the full design and, in particular,
            // why no accompanying UCI_Elo/UCI_LimitStrength-style
            // option is offered alongside it.
            out << "option name Skill Level type spin default " << search::kMaxSkillLevel
                << " min " << search::kMinSkillLevel << " max " << search::kMaxSkillLevel << '\n';
            // `Contempt` (ROADMAP.md Phase 8, "Contempt / draw score
            // adjustment") -- same standard `spin` syntax; defaults to
            // 0 (no adjustment), the same value this engine has
            // effectively always played at before this option existed.
            // kMinContemptCp/kMaxContemptCp's own doc comment above has
            // the full range rationale.
            out << "option name Contempt type spin default 0 min " << kMinContemptCp << " max "
                << kMaxContemptCp << '\n';
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
            // Answered immediately, without waiting for any in-flight
            // `go`/`go ponder` to finish -- ROADMAP.md Priority Fixes,
            // 2026-09-22, item 1's own `isready` fix. Both now run on
            // their own background thread (start_go()/start_pondering()
            // above), so this loop's own command reading is never
            // blocked by a search in progress the way an ordinary `go`
            // used to block it before this item.
            std::lock_guard<std::mutex> lock(out_mutex);
            out << "readyok\n";
            out.flush();
        } else if (cmd == "ucinewgame") {
            // A new game starting mid-search is out-of-protocol;
            // degrade gracefully by discarding whichever of `go`/`go
            // ponder` might be running -- see abandon_go()'s/
            // abandon_pondering()'s own doc comments. Only one is ever
            // actually in flight in practice, but both calls are cheap
            // no-ops when their own thread isn't joinable, so there's no
            // need to check which one first.
            abandon_go(go);
            abandon_pondering(ponder);
            pos = board::start_position();
            game_history.clear();
            // A real clear() (ROADMAP.md Priority Fixes, 2026-09-08,
            // "Persistent, engine-lifetime transposition table"), not a
            // rebuild -- see persistent_tt's own declaration comment
            // above for why a new game clears it while `setoption`
            // changes are otherwise treated as surviving `ucinewgame`.
            persistent_tt->clear();
        } else if (cmd == "position") {
            abandon_go(go);         // Same rationale as ucinewgame above.
            abandon_pondering(ponder);
            handle_position(pos, game_history, tokens, out);
        } else if (cmd == "setoption") {
            const std::size_t previous_hash_size_mb = hash_size_mb;
            handle_setoption(num_threads, hash_size_mb, move_overhead_ms, multi_pv, skill_level,
                              contempt_cp, tokens);
            if (hash_size_mb != previous_hash_size_mb) {
                // Rebuilding persistent_tt below destroys the old
                // object outright and replaces it with a fresh one at
                // the new size (TranspositionTable has no in-place
                // resize -- persistent_tt's own declaration comment,
                // above). If a `go ponder` search is still running
                // against the OLD object (start_pondering() shares this
                // same table with its background thread), destroying it
                // out from under that thread would be a genuine use-
                // after-free, not just a lost cache -- so, exactly like
                // `position`/`ucinewgame`/a second `go ponder` arriving
                // mid-ponder, this is treated as an out-of-protocol
                // situation handled defensively via abandon_pondering()
                // (which stops and JOINS that thread before returning,
                // guaranteeing no concurrent access remains) before the
                // rebuild -- a real GUI pauses and resumes pondering
                // around option changes, never resizes Hash mid-think,
                // so discarding that one ponder search here is an
                // acceptable, safe response, not a real compromise. Same
                // now applies to an in-flight ordinary `go`
                // (abandon_go()) -- rebuilding persistent_tt out from
                // under it would be the identical use-after-free hazard.
                abandon_go(go);
                abandon_pondering(ponder);
                emplace_persistent_tt(persistent_tt, hash_size_mb);
            }
        } else if (cmd == "go") {
            if (has_token(tokens, "ponder")) {
                abandon_go(go); // Out-of-protocol otherwise; see abandon_go()'s own doc comment.
                start_pondering(pos, game_history, tokens, num_threads, hash_size_mb,
                                 move_overhead_ms, *persistent_tt, out, out_mutex, ponder);
            } else {
                abandon_pondering(ponder); // Out-of-protocol otherwise; see abandon_pondering()'s own doc comment.
                start_go(pos, game_history, tokens, num_threads, hash_size_mb, move_overhead_ms,
                         multi_pv, skill_level, skill_rng, contempt_cp, *persistent_tt, out,
                         out_mutex, go);
            }
        } else if (cmd == "ponderhit") {
            handle_ponderhit(ponder);
        } else if (cmd == "stop") {
            // Both are cheap no-ops if their own thread isn't joinable
            // -- only one of `go`/`ponder` is ever actually in flight in
            // practice (this file's own established convention), so
            // calling both unconditionally covers whichever one a
            // genuine `stop` is meant for without needing to track which
            // command started it.
            handle_stop(ponder);
            handle_go_stop(go);
        } else if (cmd == "bench") {
            // ROADMAP.md Phase 8, "`bench` command": recognized as an
            // ordinary typed UCI command too, not just the `./nightwing
            // bench` CLI-argument path (main.cpp) — some fishtest/
            // OpenBench-style tooling drives engines purely over UCI
            // stdin/stdout rather than CLI arguments, so both entry
            // points call the exact same run_bench() (this file, below)
            // for byte-for-byte identical output either way.
            //
            // Abandons any in-flight `go`/`go ponder` first (out-of-
            // protocol otherwise, same rationale as `position`/
            // `ucinewgame` above) -- previously not a concern since an
            // ordinary `go` ran synchronously (nothing else could be
            // in flight when `bench` was read at all besides a
            // pondering search), but now that `go` is async this gap
            // widens to cover it too: without this, `bench`'s own
            // single-threaded run_bench() writes to `out` could
            // interleave with a still-running search thread's own
            // writes.
            abandon_go(go);
            abandon_pondering(ponder);
            run_bench(out);
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

    // Same requirement for an in-flight ordinary `go` (ROADMAP.md
    // Priority Fixes, 2026-09-22, item 1). Uses finish_go(), NOT
    // abandon_go() directly -- see finish_go()'s own doc comment for
    // why: a BOUNDED search still in flight when `quit`/end-of-input
    // arrives must be allowed to finish and print its `bestmove` (byte-
    // for-byte the same observable behavior `go` had before this item),
    // while a genuinely UNBOUNDED one that was never `stop`ped is
    // discarded instead, to avoid hanging here forever. A no-op on the
    // overwhelmingly common path where nothing was searching when the
    // loop ended.
    finish_go(go);
}

} // namespace nightwing::uci
