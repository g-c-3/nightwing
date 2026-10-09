#pragma once
// src/tuner/uci_match.h
//
// Two-process UCI-vs-UCI match runner — ROADMAP.md Priority Fixes
// (2026-09-22), "Engine-vs-engine match infrastructure for search-code
// changes", the larger-lift option.
//
// WHAT THIS ADDS THAT tuner::play_match() (match.h) CANNOT DO: play_match()
// compares two eval-weight vectors (or two thread counts) inside ONE
// process running ONE compiled copy of the search code. It cannot compare
// two different versions of the search code itself (for example the
// pre- and post-Step-2b `negamax()` of the staged-move-generation item).
// This module spawns two SEPARATE engine executables, speaks UCI to each
// over stdin/stdout pipes, and referees games between them, so any two
// commits built separately (or any two UCI engines at all) can be
// compared with no new toggle inside `negamax()`.
//
// REFEREE: this module, not either engine, owns the game state. It keeps
// its own Position, validates every reported `bestmove` against its own
// legal move generator (an illegal or unparsable move forfeits that game
// for the engine that sent it), and adjudicates checkmate, stalemate, the
// 50-move rule, threefold repetition, insufficient material (bare kings,
// or a lone minor piece against a bare king) and a `max_plies` safety cap,
// the same end conditions as tuner::play_match() (match.h) plus
// insufficient material.
//
// OPENINGS: each game starts with `random_opening_plies` uniformly random
// legal plies (same mechanism and reasoning as match.h's `MatchConfig`),
// then both engines are asked to play on. Colors alternate every game
// (game 0: engine A plays White), for the reason given in match.h.
//
// SCORING: results are reported in the same MatchResult (match.h) that
// play_match() returns, from engine A's perspective, so the existing
// score/Elo helpers and tuner::compute_sprt() (sprt.h) apply unchanged.
//
// FAILURE POLICY: an illegal move forfeits that single game and the match
// continues. A crash, closed pipe, or timeout leaves the engine in an
// unknown protocol state, so the match is aborted instead (`aborted`,
// `error`); games already finished stay counted.
//
// PLATFORMS: POSIX (fork/exec/poll) and Windows (CreateProcess, pipes).
// This file and uci_match.cpp are compiled into a separate library that
// the wasm build never builds (no process spawning under Emscripten).
//
// From-scratch implementation. The UCI wire protocol is the public
// specification only; no code was taken from cutechess-cli, fastchess,
// OpenBench, or any other tool.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tuner/match.h"

namespace nightwing::tuner {

/// Describes one engine process to launch and configure.
struct UciEngineSpec {
    /// Label used in diagnostics only.
    std::string name = "engine";

    /// Executable path (resolved through PATH if it has no directory part).
    std::string command;

    /// Extra command-line arguments passed to the executable.
    std::vector<std::string> args;

    /// `setoption name <first> value <second>` pairs sent after `uciok`,
    /// in order (for example {"Threads", "1"}, {"Hash", "64"}).
    std::vector<std::pair<std::string, std::string>> options;

    /// Option names that must appear in the engine's `uci` response (as
    /// `option name <name> type ...`). When any is missing the session
    /// fails to start with an error naming it. Empty (the default)
    /// performs no check. Needed because an engine ignores `setoption` for
    /// an unknown name, so a tuning run pointed at a binary without the
    /// tuned options would otherwise look like a null result
    /// (docs/DECISIONS.md, 2026-10-09 (4)).
    std::vector<std::string> required_options;
};

/// Match parameters shared by both engines.
struct UciMatchConfig {
    /// Number of games `play_uci_match()` plays. Ignored by
    /// UciMatchSession, whose caller chooses how many games to request.
    int num_games = 20;

    /// Fixed search depth sent as `go depth N`. Used when both
    /// `movetime_ms` and `nodes` are 0. Fixed depth keeps a match
    /// reproducible and machine-speed independent, the same reasoning as
    /// MatchConfig::search_depth.
    int depth = 4;

    /// When greater than 0, `go movetime N` is used. Takes precedence
    /// over `nodes` and `depth`.
    int movetime_ms = 0;

    /// When greater than 0 (and `movetime_ms` is 0), `go nodes N` is used
    /// instead of `depth`. A node budget is reproducible and machine-speed
    /// independent like fixed depth, but unlike fixed depth it credits a
    /// pruning or reduction parameter for the nodes it saves, which makes
    /// it the appropriate currency when tuning search constants
    /// (docs/DECISIONS.md, 2026-10-09). The engine treats `go nodes` as a
    /// soft limit applied from depth 2 onward, so games stay deterministic
    /// for a deterministic engine but node counts are approximate.
    int nodes = 0;

    /// Random opening plies at the start of each game (see match.h).
    int random_opening_plies = 8;

    /// Hard game-length cap in plies; a game reaching it is a draw.
    int max_plies = 300;

    /// Time allowed for the `uci`/`uciok` and `isready`/`readyok`
    /// handshakes, in milliseconds.
    int handshake_timeout_ms = 15000;

    /// Time allowed for one `go` to produce `bestmove`. 0 selects an
    /// automatic value: `movetime_ms` + 5000 for timed play, and 120000
    /// for fixed-depth play.
    int move_timeout_ms = 0;

    /// When true, `setoption name OwnBook value false` is sent to both
    /// engines so the opening book never replaces real search (an
    /// engine without that option ignores it, per the UCI specification).
    bool disable_own_book = true;
};

/// Outcome of a UciMatchSession or play_uci_match() run.
struct UciMatchResult {
    /// Win/draw/loss tally from engine A's perspective (match.h).
    MatchResult tally;

    /// Games lost by an illegal or unparsable `bestmove` (already
    /// included in `tally` as a loss for the offending engine).
    int illegal_moves_a = 0;
    int illegal_moves_b = 0;

    /// True when the match stopped early because an engine crashed,
    /// closed its pipe, or timed out; `error` then says which.
    bool aborted = false;
    std::string error;
};

/// A running pair of engine processes that plays games one at a time.
///
/// Engines are launched once by start() and reused for every game
/// (`ucinewgame` + `isready` between games), so a caller that needs
/// batch-by-batch decisions (for example an SPRT loop) pays process
/// start-up cost once, not per batch.
class UciMatchSession {
public:
    /// Stores the specs and config. Does not launch anything yet.
    UciMatchSession(UciEngineSpec spec_a, UciEngineSpec spec_b, UciMatchConfig config);
    ~UciMatchSession();

    UciMatchSession(const UciMatchSession&) = delete;
    UciMatchSession& operator=(const UciMatchSession&) = delete;

    /// Launches both engines and completes the UCI handshake and option
    /// setup. Returns false (and sets result().error) on any failure.
    /// Precondition: init_masks()/init_magic_bitboards()/
    /// init_zobrist_keys() have been called (this module's own referee
    /// uses the board layer).
    [[nodiscard]] bool start();

    /// Plays one game. `game_index` decides colors (even: A is White) and
    /// `seed` drives the random opening. Returns false when the session
    /// is aborted (result().aborted); a finished game, including one
    /// lost by an illegal move, returns true.
    [[nodiscard]] bool play_game(int game_index, std::uint64_t seed);

    /// Cumulative result so far.
    [[nodiscard]] const UciMatchResult& result() const noexcept;

    /// Asks both engines to quit and reaps them. Called by the
    /// destructor; safe to call more than once.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Convenience wrapper: starts a session, plays `config.num_games` games
/// seeded `base_seed`, `base_seed + 1`, ..., stops it, and returns the
/// cumulative result.
[[nodiscard]] UciMatchResult play_uci_match(const UciEngineSpec& spec_a,
                                            const UciEngineSpec& spec_b,
                                            std::uint64_t base_seed,
                                            const UciMatchConfig& config = {});

/// Builds the `go` command line a match sends for `config`: `go movetime N`
/// when `movetime_ms` > 0, else `go nodes N` when `nodes` > 0, else
/// `go depth N`. Exposed for tests.
[[nodiscard]] std::string uci_go_command(const UciMatchConfig& config);

/// Extracts the move token from a UCI `bestmove` line. Returns an empty
/// string if `line` is not a `bestmove` line or carries no move token.
/// Exposed for tests.
[[nodiscard]] std::string parse_bestmove_line(const std::string& line);

} // namespace nightwing::tuner
