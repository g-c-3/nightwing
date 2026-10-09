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
//   --opening-plies N    random opening plies per game (default 8)
//   --max-plies N        game-length cap in plies (default 300)
//   --seed S             base seed for the random openings (default 1)
//   --opt-a Name=Value   setoption for engine A (repeatable)
//   --opt-b Name=Value   setoption for engine B (repeatable)
//   --sprt E0 E1 [ALPHA BETA]
//                        stop early once the SPRT (sprt.h) accepts H0 or
//                        H1 for B-minus-A; checked after every 2 games
//   --keep-own-book      do not send `OwnBook false` to the engines
//
// Exit status: 0 = finished, 2 = bad usage, 3 = an engine failed to
// start or aborted the match.
//
// From-scratch implementation.

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
using nightwing::tuner::SprtState;
using nightwing::tuner::SprtStatus;
using nightwing::tuner::UciEngineSpec;
using nightwing::tuner::UciMatchConfig;
using nightwing::tuner::UciMatchSession;

void print_usage() {
    std::fprintf(stderr,
                 "usage: nightwing_uci_match <engine_a> <engine_b> [--games N] [--depth N] "
                 "[--movetime MS] [--nodes N]\n"
                 "         [--opening-plies N] [--max-plies N] [--seed S] [--opt-a Name=Value]... "
                 "[--opt-b Name=Value]...\n"
                 "         [--sprt E0 E1 [ALPHA BETA]] [--keep-own-book]\n"
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

const char* status_name(SprtStatus status) {
    switch (status) {
        case SprtStatus::AcceptH0:
            return "AcceptH0 (no improvement -- reject the change)";
        case SprtStatus::AcceptH1:
            return "AcceptH1 (real improvement -- accept the change)";
        case SprtStatus::Continue:
        default:
            return "Continue (inconclusive)";
    }
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
                 "opening_plies=%d max_plies=%d seed=%llu%s\n",
                 spec_a.command.c_str(), spec_b.command.c_str(), max_games,
                 uci_go_command(config).c_str(),
                 config.random_opening_plies, config.max_plies,
                 static_cast<unsigned long long>(base_seed),
                 use_sprt ? " (SPRT enabled)" : "");

    UciMatchSession session(spec_a, spec_b, config);
    if (!session.start()) {
        std::fprintf(stderr, "failed to start engines: %s\n", session.result().error.c_str());
        return 3;
    }

    SprtState state;
    for (int game = 0; game < max_games; ++game) {
        if (!session.play_game(game, base_seed + static_cast<std::uint64_t>(game))) {
            break;
        }
        const auto& t = session.result().tally;
        std::fprintf(stderr, "game %d done: B wins=%d draws=%d A wins=%d\n", t.games_played,
                     t.wins_b, t.draws, t.wins_a);

        // Check the SPRT after each completed colour pair.
        if (use_sprt && t.games_played % 2 == 0) {
            state = nightwing::tuner::compute_sprt(t.wins_b, t.draws, t.wins_a, sprt_config);
            std::fprintf(stderr, "  llr=%.3f bounds=[%.3f, %.3f] status=%s\n", state.llr,
                         state.lower_bound, state.upper_bound, status_name(state.status));
            if (state.status != SprtStatus::Continue) {
                break;
            }
        }
    }

    const nightwing::tuner::UciMatchResult& result = session.result();
    const auto& t = result.tally;
    std::printf("games=%d candidate_wins=%d draws=%d baseline_wins=%d\n", t.games_played, t.wins_b,
                t.draws, t.wins_a);
    std::printf("illegal_moves: baseline=%d candidate=%d\n", result.illegal_moves_a,
                result.illegal_moves_b);
    // MatchResult::elo_diff() is A minus B; the candidate's view is the negation.
    std::printf("candidate_elo_vs_baseline=%.1f (rough; see match.h on sample size)\n",
                -t.elo_diff());
    if (use_sprt) {
        std::printf("sprt: llr=%.3f status=%s\n", state.llr, status_name(state.status));
    }
    if (result.aborted) {
        std::printf("ABORTED: %s\n", result.error.c_str());
        session.stop();
        return 3;
    }
    session.stop();
    return 0;
}
