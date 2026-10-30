// src/tuner/uci_match_main.cpp
//
// Command-line entry point for the two-process UCI-vs-UCI match runner
// (uci_match.h): `nightwing_uci_match`.
//
// Engine A is the BASELINE and engine B is the CANDIDATE, the same
// convention nightwing_sprt (sprt_main.cpp) uses, so the SPRT statistic
// is computed from the candidate's (B's) point of view.
//
// Usage:
//   nightwing_uci_match <engine_a> <engine_b> [options]
//
//   --games N            maximum games to play (default 100)
//   --depth N            fixed search depth, `go depth N` (default 4)
//   --movetime MS        use `go movetime MS` instead of a fixed depth
//   --nodes N            use `go nodes N` instead of a fixed depth (ignored
//                        when --movetime is given)
//   --base-time MS       play on a game clock: each side starts every game
//                        with MS milliseconds and `go` carries wtime/btime/
//                        winc/binc; running out of time loses the game.
//                        Overrides --movetime, --nodes and --depth
//   --increment MS       per-move increment for --base-time (default 0)
//   --opening-plies N    random opening plies per game (default 8)
//   --max-plies N        game-length cap in plies (default 300)
//   --seed S             base seed for the random openings (default 1)
//   --opt-a Name=Value   setoption for engine A (repeatable)
//   --opt-b Name=Value   setoption for engine B (repeatable)
//   --sprt E0 E1 [ALPHA BETA]
//                        stop early once the SPRT (sprt.h) accepts H0 or
//                        H1 for B-minus-A; checked after every 2 games
//   --keep-own-book      do not send `OwnBook false` to the engines
//   --concurrency N      play N colour pairs at once, each worker with its
//                        own engine processes (default 1 = the serial loop).
//                        Results and the SPRT verdict follow pair order, so
//                        they match a serial run for deterministic engines.
//                        Use at most the physical core count; clock mode
//                        wants fewer workers than fixed-node play
//
// Stop request: SIGTERM or SIGINT (for example `timeout` at a workflow's
// wall-clock limit) sets a flag that is polled before each game. The game
// in progress finishes, no further game starts, and the normal summary --
// including `time_forfeits` in clock mode -- is printed with a `STOPPED`
// line, so a cut-off run still carries its full tally.
//
// Exit status: 0 = finished (or stopped by SIGTERM/SIGINT after printing
// the summary), 2 = bad usage, 3 = an engine failed to start or aborted
// the match.
//
// From-scratch implementation.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "tuner/sprt.h"
#include "tuner/uci_match.h"

namespace {

using nightwing::tuner::SprtConfig;
using nightwing::tuner::UciEngineSpec;
using nightwing::tuner::UciMatchConfig;
using nightwing::tuner::UciMatchRunOptions;
using nightwing::tuner::UciMatchSession;

/// Set by the signal handler; polled by run_uci_match_loop() before each game.
volatile std::sig_atomic_t g_stop_requested = 0;

extern "C" void handle_stop_signal(int) {
    g_stop_requested = 1;
}

void print_usage() {
    std::fprintf(stderr,
                 "usage: nightwing_uci_match <engine_a> <engine_b> [--games N] [--depth N] "
                 "[--movetime MS] [--nodes N] [--base-time MS] [--increment MS]\n"
                 "         [--opening-plies N] [--max-plies N] [--seed S] [--opt-a Name=Value]... "
                 "[--opt-b Name=Value]...\n"
                 "         [--sprt E0 E1 [ALPHA BETA]] [--keep-own-book] [--concurrency N]\n"
                 "Engine A is the baseline, engine B the candidate.\n");
}

/// Parses "Name=Value" into a setoption pair; false on a missing '='.
bool parse_option(const std::string& text, UciEngineSpec& spec) {
    const std::size_t eq = text.find('=');
    if (eq == std::string::npos || eq == 0) {
        return false;
    }
    spec.options.emplace_back(text.substr(0, eq), text.substr(eq + 1));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 2;
    }

    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    UciEngineSpec spec_a;
    UciEngineSpec spec_b;
    spec_a.name = "A(baseline)";
    spec_a.command = argv[1];
    spec_b.name = "B(candidate)";
    spec_b.command = argv[2];

    UciMatchConfig config;
    int max_games = 100;
    std::uint64_t base_seed = 1;
    bool use_sprt = false;
    SprtConfig sprt_config;
    int concurrency = 1;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", what);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--games") {
            const char* v = next("--games");
            if (v == nullptr) return 2;
            max_games = std::atoi(v);
        } else if (arg == "--depth") {
            const char* v = next("--depth");
            if (v == nullptr) return 2;
            config.depth = std::atoi(v);
        } else if (arg == "--movetime") {
            const char* v = next("--movetime");
            if (v == nullptr) return 2;
            config.movetime_ms = std::atoi(v);
        } else if (arg == "--nodes") {
            const char* v = next("--nodes");
            if (v == nullptr) return 2;
            config.nodes = std::atoi(v);
        } else if (arg == "--base-time") {
            const char* v = next("--base-time");
            if (v == nullptr) return 2;
            config.base_time_ms = std::atoi(v);
        } else if (arg == "--increment") {
            const char* v = next("--increment");
            if (v == nullptr) return 2;
            config.increment_ms = std::atoi(v);
        } else if (arg == "--opening-plies") {
            const char* v = next("--opening-plies");
            if (v == nullptr) return 2;
            config.random_opening_plies = std::atoi(v);
        } else if (arg == "--max-plies") {
            const char* v = next("--max-plies");
            if (v == nullptr) return 2;
            config.max_plies = std::atoi(v);
        } else if (arg == "--seed") {
            const char* v = next("--seed");
            if (v == nullptr) return 2;
            base_seed = static_cast<std::uint64_t>(std::strtoull(v, nullptr, 10));
        } else if (arg == "--opt-a" || arg == "--opt-b") {
            const char* v = next(arg.c_str());
            if (v == nullptr) return 2;
            if (!parse_option(v, arg == "--opt-a" ? spec_a : spec_b)) {
                std::fprintf(stderr, "bad option '%s' (expected Name=Value)\n", v);
                return 2;
            }
        } else if (arg == "--sprt") {
            const char* e0 = next("--sprt");
            const char* e1 = (e0 != nullptr) ? next("--sprt") : nullptr;
            if (e0 == nullptr || e1 == nullptr) return 2;
            use_sprt = true;
            sprt_config.elo0 = std::atof(e0);
            sprt_config.elo1 = std::atof(e1);
            // Optional ALPHA BETA: consumed only when the next two
            // arguments are not flags.
            if (i + 2 < argc && argv[i + 1][0] != '-' && argv[i + 2][0] != '-') {
                sprt_config.alpha = std::atof(argv[++i]);
                sprt_config.beta = std::atof(argv[++i]);
            }
        } else if (arg == "--concurrency") {
            const char* v = next("--concurrency");
            if (v == nullptr) return 2;
            concurrency = std::atoi(v);
            if (concurrency < 1) {
                std::fprintf(stderr, "--concurrency must be at least 1\n");
                return 2;
            }
        } else if (arg == "--keep-own-book") {
            config.disable_own_book = false;
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", arg.c_str());
            print_usage();
            return 2;
        }
    }

    std::fprintf(stderr,
                 "Nightwing UCI match: A=%s (baseline) vs B=%s (candidate); max_games=%d %s "
                 "opening_plies=%d max_plies=%d seed=%llu%s concurrency=%d\n",
                 spec_a.command.c_str(), spec_b.command.c_str(), max_games,
                 uci_go_command(config).c_str(),
                 config.random_opening_plies, config.max_plies,
                 static_cast<unsigned long long>(base_seed),
                 use_sprt ? " (SPRT enabled)" : "", concurrency);

    // Installed before the engines start so a stop request during start-up is
    // also honoured at the first game boundary.
    std::signal(SIGTERM, handle_stop_signal);
    std::signal(SIGINT, handle_stop_signal);

    UciMatchRunOptions options;
    options.max_games = max_games;
    options.base_seed = base_seed;
    options.use_sprt = use_sprt;
    options.sprt = sprt_config;
    options.stop_flag = &g_stop_requested;
    options.progress = [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); };

    nightwing::tuner::UciMatchResult result;
    nightwing::tuner::UciMatchRunOutcome outcome;

    if (concurrency > 1) {
        nightwing::tuner::UciParallelOutcome parallel = nightwing::tuner::run_uci_match_parallel(
            spec_a, spec_b, config, options, concurrency);
        result = parallel.result;
        outcome = parallel.run;
        if (result.aborted && result.tally.games_played == 0 && !outcome.stopped_by_request) {
            std::fprintf(stderr, "failed to start engines or play: %s\n", result.error.c_str());
            return 3;
        }
    } else {
        UciMatchSession session(spec_a, spec_b, config);
        if (!session.start()) {
            std::fprintf(stderr, "failed to start engines: %s\n", session.result().error.c_str());
            return 3;
        }
        outcome = nightwing::tuner::run_uci_match_loop(session, options);
        result = session.result();
        session.stop();
    }

    const std::string summary =
        nightwing::tuner::format_uci_match_summary(config, result, options, outcome);
    std::fputs(summary.c_str(), stdout);
    std::fflush(stdout);
    return (result.aborted && !outcome.stopped_by_request) ? 3 : 0;
}
