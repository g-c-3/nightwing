// src/uci/uci.cpp
//
// Deliberately out of scope for Phase 2's "basic UCI loop" (revisit
// later phases, tracked informally here rather than duplicated across
// every function that touches it):
//   - setoption / Hash / any other engine option: still no options
//     exist besides `Threads` (added Session 74, ROADMAP.md Phase 7's
//     "Thread count UCI option" item -- see handle_setoption() below) --
//     no TT-size configuration yet, since the TT stays scoped per
//     top-level search call rather than a persistent global (search/
//     tt.h's own LIFETIME NOTE, tied to a still-open Phase 8 item).
//   - True asynchronous `go infinite` + `stop`: would need a background
//     search thread and a stop flag negamax() checks periodically. The
//     stop-flag PIECE of that now exists (search.h's SearchLimits,
//     since the mid-search-time-checks Priority Fix — docs/DECISIONS.md,
//     2026-08-26) and IS what makes `go movetime`/`go wtime` actually
//     respect their budget mid-iteration, but it's driven by an
//     internal deadline computed once at the start of `go`, not by an
//     externally-arriving `stop` command read from a second, concurrent
//     input source — that still needs the background-thread half of
//     this item, which doesn't exist. `go` still always runs
//     synchronously to completion (or until its own internal deadline)
//     before this loop reads its next line; `stop` is parsed but has no
//     effect, since by the time it could arrive on `in`, `go` has
//     already finished and printed `bestmove`.
//   - Pondering (`go ponder`, `ponderhit`): needs the same async
//     machinery as `stop`, plus its own logic on top; not attempted.
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

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
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
void emit_info(const search::SearchResult& result, std::ostream& out) {
    out << "info depth " << result.depth_completed << " score ";
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

/// Handles `setoption name <name...> value <value...>`. Only `Threads`
/// (ROADMAP.md Phase 7) is a recognized option name -- every other name
/// is silently ignored, matching this file's established robustness
/// convention (this file's header comment; run()'s own trailing
/// comment on unrecognized commands generally) rather than treating an
/// unknown option as an error. `name`/`value` are matched positionally
/// (the LAST `name`/`value` token pair in the line, per the UCI spec's
/// own grammar allowing either to be multi-word) rather than assuming
/// fixed token indices, even though `Threads` itself is a single word
/// -- a small amount of generality here costs nothing and avoids a
/// parser that would need revisiting the moment a second, multi-word-
/// named option is ever added. A malformed line (`name`/`value` in the
/// wrong order, `value` missing entirely, a non-integer value) is
/// ignored, leaving `num_threads` at whatever it was before -- same
/// robustness rationale as handle_position()/apply_uci_moves() above:
/// a slightly malformed setoption from a GUI or script shouldn't crash
/// the engine or corrupt otherwise-good prior state.
void handle_setoption(int& num_threads, const std::vector<std::string>& tokens) {
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
void handle_go(Position& pos, const std::vector<std::uint64_t>& game_history,
               const std::vector<std::string>& tokens, int num_threads, std::ostream& out) {
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

    int max_depth = 0;
    int time_limit_ms = 0;
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
            max_depth = next_int();
            have_depth = true;
        } else if (tok == "movetime") {
            time_limit_ms = next_int();
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
        if (max_depth < 1) {
            max_depth = kNoTimeControlDepth; // Guard against a malformed "depth 0" or negative value.
        }
        // time_limit_ms is whatever movetime gave (possibly 0, i.e. no limit).
    } else if (have_movetime) {
        max_depth = kTimedSearchMaxDepth;
        // time_limit_ms is already set from movetime above.
    } else {
        const bool white_to_move = pos.side_to_move == board::Color::White;
        const int remaining = white_to_move ? wtime : btime;
        if (remaining >= 0) {
            const int increment = white_to_move ? winc : binc;
            time_limit_ms = allocate_time_ms(remaining, increment);
            max_depth = kTimedSearchMaxDepth;
        } else {
            max_depth = kNoTimeControlDepth;
            time_limit_ms = 0;
        }
    }

    // `on_iteration` (search/search.h's IterationCallback): emits one
    // `info depth ... score ... nodes ... pv ...` line per completed
    // iteration, live, before the final `bestmove` below -- see
    // emit_info()'s own doc comment. `material_weights` stays the
    // compiled-in-constants default (nullptr) -- no UCI option exists
    // to override it (search_fixed_depth()'s own doc comment covers
    // that parameter's real use, the Texel/SPSA tuner, not this UCI
    // loop). `num_threads` is this call's own new parameter, this
    // function's own doc comment above.
    const search::SearchResult result = search::search_iterative_deepening(
        pos, max_depth, time_limit_ms, game_history,
        [&out](const search::SearchResult& iteration_result) { emit_info(iteration_result, out); },
        /*material_weights=*/nullptr, num_threads);

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
            out << "uciok\n";
            out.flush();
        } else if (cmd == "isready") {
            out << "readyok\n";
            out.flush();
        } else if (cmd == "ucinewgame") {
            pos = board::start_position();
            game_history.clear();
        } else if (cmd == "position") {
            handle_position(pos, game_history, tokens);
        } else if (cmd == "setoption") {
            handle_setoption(num_threads, tokens);
        } else if (cmd == "go") {
            handle_go(pos, game_history, tokens, num_threads, out);
        } else if (cmd == "quit") {
            break;
        }
        // stop, ponderhit, debug, register, and anything else
        // unrecognized (including a `setoption` for any name besides
        // `Threads` -- handle_setoption()'s own doc comment): silently
        // ignored, per the UCI spec's expectation that engines ignore
        // commands they don't understand — required for robustness
        // against a GUI/script sending commands ahead of what this
        // phase supports (see this file's header comment).
    }
}

} // namespace nightwing::uci
