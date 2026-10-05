// tests/spsa_eval_tests.cpp
//
// Tests for src/tuner/spsa_eval.h/.cpp: the in-process SPSA-to-play_match
// binding for eval mobility weights. Matches are kept tiny (depth 2, few
// games) so these stay fast; they check plumbing and invariants, not
// strength. Each TEST_CASE initializes the board tables itself, like
// tests/match_tests.cpp.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/mobility.h"
#include "tuner/spsa_eval.h"
#include "tuner/tune.h"

using namespace nightwing;

namespace {

void init_tables() {
    board::init_masks();
    board::init_magic_bitboards();
    board::init_zobrist_keys();
}

tuner::MatchConfig tiny_match() {
    tuner::MatchConfig m;
    m.num_games = 2;
    m.search_depth = 2;
    m.random_opening_plies = 6;
    m.max_plies = 60;
    return m;
}

} // namespace

TEST_CASE("play_mobility_match: identical vectors play a complete, consistent match", "[tuner][spsa]") {
    init_tables();
    const auto w = eval::default_mobility_weights();
    const tuner::MatchResult r = tuner::play_mobility_match(w, w, 7, tiny_match());
    REQUIRE(r.games_played == 2);
    REQUIRE(r.wins_a + r.wins_b + r.draws == 2);
    REQUIRE(r.score_a() >= 0.0);
    REQUIRE(r.score_a() <= 1.0);
}

TEST_CASE("play_mobility_match: same seed reproduces the same result", "[tuner][spsa]") {
    init_tables();
    eval::MobilityWeights a = eval::default_mobility_weights();
    a.knight_mg = 12.0;
    const auto b = eval::default_mobility_weights();
    const tuner::MatchResult r1 = tuner::play_mobility_match(a, b, 11, tiny_match());
    const tuner::MatchResult r2 = tuner::play_mobility_match(a, b, 11, tiny_match());
    REQUIRE(r1.wins_a == r2.wins_a);
    REQUIRE(r1.wins_b == r2.wins_b);
    REQUIRE(r1.draws == r2.draws);
}

TEST_CASE("run_spsa_mobility: a short run returns 8 in-bounds parameters", "[tuner][spsa]") {
    init_tables();
    tuner::SpsaMobilityConfig cfg;
    cfg.spsa.iterations = 2;
    cfg.spsa.seed = 5;
    cfg.match = tiny_match();
    cfg.min_value = 0.0;
    cfg.max_value = 20.0;
    const tuner::SpsaResult r = tuner::run_spsa_mobility(cfg);
    REQUIRE(r.iterations_run == 2);
    REQUIRE(r.theta.size() == tuner::kMobilityParameters.size());
    REQUIRE(r.plus_scores.size() == 2);
    for (double v : r.theta) {
        REQUIRE(v >= 0.0);
        REQUIRE(v <= 20.0);
    }
}

TEST_CASE("scaled_mobility_weights: scale 1 is the defaults, scale 0 is all zeros", "[tuner][spsa]") {
    const auto def = eval::default_mobility_weights();
    const auto same = tuner::scaled_mobility_weights(1.0);
    const auto zero = tuner::scaled_mobility_weights(0.0);
    for (const auto& ref : tuner::kMobilityParameters) {
        REQUIRE(ref.get(same) == ref.get(def));
        REQUIRE(ref.get(zero) == 0.0);
    }
}

TEST_CASE("run_spsa_mobility: start_scale 0 starts every parameter near zero", "[tuner][spsa]") {
    init_tables();
    tuner::SpsaMobilityConfig cfg;
    cfg.spsa.iterations = 1;
    cfg.spsa.seed = 3;
    cfg.match = tiny_match();
    cfg.start_scale = 0.0;
    const tuner::SpsaResult r = tuner::run_spsa_mobility(cfg);
    REQUIRE(r.theta.size() == tuner::kMobilityParameters.size());
    for (double v : r.theta) {
        REQUIRE(v >= 0.0);
        REQUIRE(v <= 1.0); // one step of at most about 0.94 from 0 at default r0
    }
}

TEST_CASE("scaled_material_weights: scale 1 is the defaults; pawn stays anchored at any scale", "[tuner][spsa]") {
    const auto def = eval::default_material_weights();
    const auto same = tuner::scaled_material_weights(1.0);
    const auto damaged = tuner::scaled_material_weights(0.5);
    for (const auto& ref : tuner::kMaterialParameters) {
        REQUIRE(ref.get(same) == ref.get(def));
        if (ref.anchored) {
            REQUIRE(ref.get(damaged) == ref.get(def));
        } else {
            REQUIRE(ref.get(damaged) == ref.get(def) * 0.5);
        }
    }
}

TEST_CASE("play_material_match: damaged vs default plays a complete match; same seed reproduces", "[tuner][spsa]") {
    init_tables();
    const auto def = eval::default_material_weights();
    const auto damaged = tuner::scaled_material_weights(0.5);
    const tuner::MatchResult r1 = tuner::play_material_match(damaged, def, 13, tiny_match());
    const tuner::MatchResult r2 = tuner::play_material_match(damaged, def, 13, tiny_match());
    REQUIRE(r1.games_played == 2);
    REQUIRE(r1.wins_a + r1.wins_b + r1.draws == 2);
    REQUIRE(r1.wins_a == r2.wins_a);
    REQUIRE(r1.wins_b == r2.wins_b);
    REQUIRE(r1.draws == r2.draws);
}

TEST_CASE("run_spsa_material: a short run returns 8 in-bounds parameters", "[tuner][spsa]") {
    init_tables();
    tuner::SpsaMaterialConfig cfg;
    cfg.spsa.iterations = 2;
    cfg.spsa.seed = 5;
    cfg.match = tiny_match();
    cfg.start_scale = 0.7;
    const tuner::SpsaResult r = tuner::run_spsa_material(cfg);
    REQUIRE(r.iterations_run == 2);
    REQUIRE(r.theta.size() == 8); // kMaterialParameters minus the two anchored pawn entries
    REQUIRE(r.plus_scores.size() == 2);
    for (double v : r.theta) {
        REQUIRE(v >= cfg.min_value);
        REQUIRE(v <= cfg.max_value);
    }
}
