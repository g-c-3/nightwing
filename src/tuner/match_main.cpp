// src/tuner/match_main.cpp
//
// Nightwing match: reads a tuned MaterialWeights from stdin (in
// tuner::tune_main's own "pawn_mg=... pawn_eg=..." output format,
// tune_main.cpp) and plays it in a match against
// eval::default_material_weights() (the current compiled-in baseline)
// via tuner::play_match() (tuner/match.h) — the "strength comparison"
// half of ROADMAP.md Phase 5's final item, mirroring
// selfplay_main.cpp's/tune_main.cpp's own "small standalone tool, thin
// main() over a real library function" shape.
//
// Typical use (chained onto the rest of the pipeline, same "GitHub
// Actions handles ALL building/running of anything compute-heavy"
// convention as selfplay_main.cpp's/tune_main.cpp's own header
// comments):
//
//   nightwing_selfplay 200 1 4 8 200 > training_data.txt
//   nightwing_tune 100 < training_data.txt > tuned_weights.txt
//   nightwing_match < tuned_weights.txt
//
// (nightwing_tune's own progress/loss-curve output goes to stderr, not
// stdout — tuned_weights.txt above only ever receives the five
// "pawn_mg=... pawn_eg=..." lines this tool expects.)
//
// Every argument is optional and positional, in this order (mirroring
// MatchConfig's own fields):
//   nightwing_match [num_games] [base_seed] [search_depth]
//                   [random_opening_plies] [max_plies]

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

namespace {

/// Parses tuner::tune_main's own stdout format (tune_main.cpp) --
/// `key=value` pairs, whitespace-separated, possibly spread across
/// several lines -- into a MaterialWeights. Unrecognized keys are
/// silently ignored (forward-compatible with a future session adding
/// more fields to this format without breaking this parser); any of
/// the 10 expected keys simply absent from the input leaves that field
/// at eval::default_material_weights()'s own value, so a caller who
/// only cares about, say, the knight values can hand-edit a training
/// file down to just those two keys and still get a sensible result
/// for everything else.
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
            continue; // malformed value -- leave this key unset, same
                      // tolerant-of-real-world-mess spirit as tuner::
                      // read_training_data() (selfplay.cpp)
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

} // namespace

int main(int argc, char** argv) {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    nightwing::tuner::MatchConfig config;

    // Same deliberately minimal, no-dependency positional parsing as
    // selfplay_main.cpp's/tune_main.cpp's own.
    if (argc > 1) {
        config.num_games = std::atoi(argv[1]);
    }
    std::uint64_t base_seed = 1;
    if (argc > 2) {
        base_seed = static_cast<std::uint64_t>(std::strtoull(argv[2], nullptr, 10));
    }
    if (argc > 3) {
        config.search_depth = std::atoi(argv[3]);
    }
    if (argc > 4) {
        config.random_opening_plies = std::atoi(argv[4]);
    }
    if (argc > 5) {
        config.max_plies = std::atoi(argv[5]);
    }

    const nightwing::eval::MaterialWeights weights_a = nightwing::eval::default_material_weights();
    const nightwing::eval::MaterialWeights weights_b = parse_weights(std::cin);

    std::fprintf(stderr,
                  "Nightwing match: A = default_material_weights(), B = weights read from "
                  "stdin. num_games=%d, base_seed=%llu, search_depth=%d, "
                  "random_opening_plies=%d, max_plies=%d\n",
                  config.num_games, static_cast<unsigned long long>(base_seed), config.search_depth,
                  config.random_opening_plies, config.max_plies);
    std::fprintf(stderr, "B: pawn=(%.1f,%.1f) knight=(%.1f,%.1f) bishop=(%.1f,%.1f) "
                          "rook=(%.1f,%.1f) queen=(%.1f,%.1f)\n",
                  weights_b.pawn_mg, weights_b.pawn_eg, weights_b.knight_mg, weights_b.knight_eg,
                  weights_b.bishop_mg, weights_b.bishop_eg, weights_b.rook_mg, weights_b.rook_eg,
                  weights_b.queen_mg, weights_b.queen_eg);

    const nightwing::tuner::MatchResult result =
        nightwing::tuner::play_match(weights_a, weights_b, base_seed, config);

    std::printf("games_played=%d wins_a=%d wins_b=%d draws=%d score_a=%.4f elo_diff=%.1f\n",
                result.games_played, result.wins_a, result.wins_b, result.draws, result.score_a(),
                result.elo_diff());

    return 0;
}
