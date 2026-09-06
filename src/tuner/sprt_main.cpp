// src/tuner/sprt_main.cpp
//
// Nightwing SPRT: the actual "process for validating future changes"
// ROADMAP.md Phase 8's SPRT item calls for. Reads a candidate
// MaterialWeights from stdin (identical "pawn_mg=... pawn_eg=..."
// format nightwing_tune emits and nightwing_match already reads —
// match_main.cpp's own header comment) and plays it against
// eval::default_material_weights() in BATCHES via tuner::play_match()
// (tuner/match.h), checking tuner::compute_sprt() (tuner/sprt.h) after
// each batch and stopping as soon as the evidence clears one of the
// two configured Elo bounds — rather than committing to a single fixed
// game count up front the way nightwing_match's own one-shot match
// does.
//
// Typical use (extends the existing selfplay -> tune pipeline —
// match_main.cpp's own header comment — with a principled accept/
// reject decision in place of a fixed-game match):
//
//   nightwing_selfplay 5000 1 4 8 200 > training_data.txt
//   nightwing_tune 200 < training_data.txt > tuned_weights.txt
//   nightwing_sprt < tuned_weights.txt
//
// A = eval::default_material_weights() (the baseline), B = the
// candidate weights read from stdin. H1 ("B is a real improvement, at
// least elo1") accepted means the candidate should be adopted; H0 ("B
// is no better than elo0") accepted means it should be rejected. This
// same tool works equally well as a non-regression check for a change
// that ISN'T supposed to alter strength at all (an eval/search
// refactor, a performance optimization) by setting elo0 negative and
// elo1 at or near 0 — see sprt.h's own header comment.
//
// Every argument is optional and positional, in this order:
//   nightwing_sprt [elo0] [elo1] [alpha] [beta] [batch_size]
//                  [max_games] [base_seed] [search_depth]
//                  [random_opening_plies] [max_plies]
//
// `batch_size` should be even, matching MatchConfig::num_games's own
// "even for exactly equal color alternation" comment (match.h) — each
// batch is itself a self-contained, evenly-color-alternated
// tuner::play_match() call. `max_games` is a hard safety cap: if
// neither SPRT bound is crossed by then, the run stops anyway and
// reports the inconclusive result honestly (SprtStatus::Continue)
// rather than looping forever on a genuinely borderline change.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/psqt.h"
#include "tuner/match.h"
#include "tuner/sprt.h"

namespace {

/// Identical to match_main.cpp's own parse_weights() -- duplicated
/// rather than shared, matching this project's existing per-tool
/// "small standalone executable" convention (selfplay_main.cpp/
/// tune_main.cpp/match_main.cpp are none of them linked against one
/// another).
nightwing::eval::MaterialWeights parse_weights(std::istream& in) {
    std::unordered_map<std::string, double> values;
    std::string token;
    while (in >> token) {
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = token.substr(0, equals);
        const std::string value_text = token.substr(equals + 1);
        try {
            values[key] = std::stod(value_text);
        } catch (const std::exception&) {
            continue;
        }
    }

    nightwing::eval::MaterialWeights weights = nightwing::eval::default_material_weights();
    auto apply = [&](const char* key, double& field) {
        const auto it = values.find(key);
        if (it != values.end()) {
            field = it->second;
        }
    };
    apply("pawn_mg", weights.pawn_mg);
    apply("pawn_eg", weights.pawn_eg);
    apply("knight_mg", weights.knight_mg);
    apply("knight_eg", weights.knight_eg);
    apply("bishop_mg", weights.bishop_mg);
    apply("bishop_eg", weights.bishop_eg);
    apply("rook_mg", weights.rook_mg);
    apply("rook_eg", weights.rook_eg);
    apply("queen_mg", weights.queen_mg);
    apply("queen_eg", weights.queen_eg);
    return weights;
}

const char* status_name(nightwing::tuner::SprtStatus status) {
    switch (status) {
        case nightwing::tuner::SprtStatus::AcceptH0:
            return "AcceptH0 (no improvement -- reject the change)";
        case nightwing::tuner::SprtStatus::AcceptH1:
            return "AcceptH1 (real improvement -- accept the change)";
        case nightwing::tuner::SprtStatus::Continue:
        default:
            return "Continue (inconclusive -- max_games reached)";
    }
}

} // namespace

int main(int argc, char** argv) {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    nightwing::tuner::SprtConfig sprt_config;
    int batch_size = 20;
    int max_games = 2000;
    std::uint64_t base_seed = 1;
    nightwing::tuner::MatchConfig batch_template;

    if (argc > 1) {
        sprt_config.elo0 = std::atof(argv[1]);
    }
    if (argc > 2) {
        sprt_config.elo1 = std::atof(argv[2]);
    }
    if (argc > 3) {
        sprt_config.alpha = std::atof(argv[3]);
    }
    if (argc > 4) {
        sprt_config.beta = std::atof(argv[4]);
    }
    if (argc > 5) {
        batch_size = std::atoi(argv[5]);
    }
    if (argc > 6) {
        max_games = std::atoi(argv[6]);
    }
    if (argc > 7) {
        base_seed = static_cast<std::uint64_t>(std::strtoull(argv[7], nullptr, 10));
    }
    if (argc > 8) {
        batch_template.search_depth = std::atoi(argv[8]);
    }
    if (argc > 9) {
        batch_template.random_opening_plies = std::atoi(argv[9]);
    }
    if (argc > 10) {
        batch_template.max_plies = std::atoi(argv[10]);
    }
    batch_template.num_games = batch_size;

    const nightwing::eval::MaterialWeights weights_a = nightwing::eval::default_material_weights();
    const nightwing::eval::MaterialWeights weights_b = parse_weights(std::cin);

    std::fprintf(stderr,
                 "Nightwing SPRT: A = default_material_weights() (baseline), B = weights read "
                 "from stdin (candidate). elo0=%.2f elo1=%.2f alpha=%.3f beta=%.3f "
                 "batch_size=%d max_games=%d base_seed=%llu search_depth=%d "
                 "random_opening_plies=%d max_plies=%d\n",
                 sprt_config.elo0, sprt_config.elo1, sprt_config.alpha, sprt_config.beta, batch_size,
                 max_games, static_cast<unsigned long long>(base_seed), batch_template.search_depth,
                 batch_template.random_opening_plies, batch_template.max_plies);

    int wins_a = 0; // baseline (A) wins
    int wins_b = 0; // candidate (B) wins
    int draws = 0;
    int total_games = 0;
    nightwing::tuner::SprtState state;

    while (total_games < max_games) {
        const std::uint64_t batch_seed = base_seed + static_cast<std::uint64_t>(total_games);
        const nightwing::tuner::MatchResult batch_result =
            nightwing::tuner::play_match(weights_a, weights_b, batch_seed, batch_template);

        wins_a += batch_result.wins_a;
        wins_b += batch_result.wins_b;
        draws += batch_result.draws;
        total_games += batch_result.games_played;

        // SPRT's own "win" is B (the candidate) beating A (the
        // baseline) -- this file's own header comment on why A/B keep
        // nightwing_match's existing baseline/candidate convention.
        state = nightwing::tuner::compute_sprt(wins_b, draws, wins_a, sprt_config);

        std::fprintf(stderr,
                     "games=%d wins_b=%d draws=%d wins_a=%d llr=%.3f bounds=[%.3f, %.3f] status=%s\n",
                     total_games, wins_b, draws, wins_a, state.llr, state.lower_bound,
                     state.upper_bound, status_name(state.status));

        if (state.status != nightwing::tuner::SprtStatus::Continue) {
            break;
        }
    }

    const nightwing::tuner::MatchResult final_result{wins_a, wins_b, draws, total_games};

    std::printf("games_played=%d wins_a=%d wins_b=%d draws=%d score_b=%.4f elo_diff_b_minus_a=%.1f "
                "llr=%.3f status=%s\n",
                final_result.games_played, final_result.wins_a, final_result.wins_b, final_result.draws,
                1.0 - final_result.score_a(), -final_result.elo_diff(), state.llr,
                status_name(state.status));

    return 0;
}
