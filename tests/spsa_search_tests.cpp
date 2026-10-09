// tests/spsa_search_tests.cpp
//
// Tests for src/tuner/spsa_search.h/.cpp (SPSA binding for search constants,
// ROADMAP.md "SPSA tuner" step (2c); docs/DECISIONS.md 2026-10-09 (4)) and
// for UciEngineSpec::required_options. The real engine under test
// (NIGHTWING_ENGINE_PATH) is the production build by default and the tuning
// build when NIGHTWING_SEARCH_TUNING is ON; cases that depend on that are
// split with #if.

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "board/attacks.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "search/tunables.h"
#include "tuner/spsa_search.h"
#include "tuner/uci_match.h"

using namespace nightwing;

namespace {

void init_all() {
    board::init_masks();
    board::init_magic_bitboards();
    board::init_zobrist_keys();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

tuner::UciMatchConfig tiny_match(int games) {
    tuner::UciMatchConfig config;
    config.num_games = games;
    config.nodes = 2000;
    config.random_opening_plies = 2;
    config.max_plies = 40;
    return config;
}

} // namespace

TEST_CASE("spsa_search: parameter table has seven in-range entries with c >= 1", "[tuner][spsa_search]") {
    const auto params = tuner::make_search_spsa_params(1.0, 1.0);
    REQUIRE(params.size() == 7);
    for (const auto& p : params) {
        REQUIRE(p.c >= 1.0);
        REQUIRE(p.min <= p.start);
        REQUIRE(p.start <= p.max);
    }
    // c_scale far below 1 still leaves every c at 1 or more; start_scale clamps and rounds.
    for (const auto& p : tuner::make_search_spsa_params(0.001, 1.0)) {
        REQUIRE(p.c == 1.0);
    }
    for (const auto& p : tuner::make_search_spsa_params(1.0, 100.0)) {
        REQUIRE(p.start == p.max);
    }
    for (const auto& p : tuner::make_search_spsa_params(1.0, 0.0)) {
        REQUIRE(p.start == p.min);
    }
}

TEST_CASE("spsa_search: engine spec rounds and clamps theta and lists required options",
          "[tuner][spsa_search]") {
    const auto params = tuner::make_search_spsa_params(1.0, 1.0);
    std::vector<double> theta;
    for (const auto& p : params) {
        theta.push_back(p.start);
    }
    theta[0] = 3.6;      // NullMoveReduction -> 4
    theta[2] = 10000.0;  // ProbCutMargin -> clamped to 600
    theta[3] = -7.0;     // IIRMinDepth -> clamped to 2
    const tuner::UciEngineSpec spec = tuner::make_search_engine_spec("x", "engine", params, theta);

    REQUIRE(spec.command == "engine");
    REQUIRE(spec.required_options.size() == 7);
    REQUIRE(spec.options.size() == 9); // Threads, Hash, then seven tunables
    REQUIRE(spec.options[0] == std::make_pair(std::string("Threads"), std::string("1")));
    REQUIRE(spec.options[2] == std::make_pair(std::string("NullMoveReduction"), std::string("4")));
    REQUIRE(spec.options[4] == std::make_pair(std::string("ProbCutMargin"), std::string("600")));
    REQUIRE(spec.options[5] == std::make_pair(std::string("IIRMinDepth"), std::string("2")));
    // A short theta falls back to each parameter's start.
    const tuner::UciEngineSpec shorter = tuner::make_search_engine_spec("x", "engine", params, {});
    REQUIRE(shorter.options[4] == std::make_pair(std::string("ProbCutMargin"), std::string("200")));
}

TEST_CASE("uci_match: a missing required option aborts start() with an error naming it",
          "[tuner][uci_match][spsa_search]") {
    init_all();
    tuner::UciEngineSpec a;
    a.command = NIGHTWING_ENGINE_PATH;
    a.required_options = {"NoSuchTunableOption"};
    tuner::UciEngineSpec b;
    b.command = NIGHTWING_ENGINE_PATH;
    tuner::UciMatchSession session(a, b, tiny_match(1));
    REQUIRE_FALSE(session.start());
    REQUIRE(session.result().aborted);
    REQUIRE(contains(session.result().error, "NoSuchTunableOption"));
}

TEST_CASE("uci_match: an advertised required option lets the match run", "[tuner][uci_match][spsa_search]") {
    init_all();
    tuner::UciEngineSpec a;
    a.command = NIGHTWING_ENGINE_PATH;
    a.required_options = {"Hash", "Move Overhead"}; // multi-word names are matched whole
    tuner::UciEngineSpec b = a;
    const tuner::UciMatchResult r = tuner::play_uci_match(a, b, 1, tiny_match(1));
    REQUIRE_FALSE(r.aborted);
    REQUIRE(r.tally.games_played == 1);
}

#if defined(NIGHTWING_SEARCH_TUNING)

TEST_CASE("spsa_search: table mirrors search/tunables.h exactly", "[tuner][spsa_search][tunables]") {
    const auto& table = tuner::search_tunable_table();
    REQUIRE(table.size() == search::kSearchTunableSpecs.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& spec = search::kSearchTunableSpecs[i];
        REQUIRE(std::string(table[i].name) == std::string(spec.name));
        REQUIRE(table[i].default_value == spec.default_value);
        REQUIRE(table[i].min_value == spec.min_value);
        REQUIRE(table[i].max_value == spec.max_value);
    }
}

TEST_CASE("spsa_search: node-limited match between two theta vectors completes against the tuning engine",
          "[tuner][spsa_search]") {
    init_all();
    const auto params = tuner::make_search_spsa_params(1.0, 1.0);
    std::vector<double> a;
    std::vector<double> b;
    for (const auto& p : params) {
        a.push_back(p.start);
        b.push_back(p.start);
    }
    b[0] = 4; // different null-move reduction on side B
    const tuner::UciMatchResult r =
        tuner::play_search_match(NIGHTWING_ENGINE_PATH, params, a, b, 5, tiny_match(2));
    REQUIRE_FALSE(r.aborted);
    REQUIRE(r.tally.games_played == 2);
    REQUIRE(r.illegal_moves_a == 0);
    REQUIRE(r.illegal_moves_b == 0);
}

TEST_CASE("spsa_search: a tiny SPSA run finishes with theta inside the bounds", "[tuner][spsa_search]") {
    init_all();
    tuner::SpsaSearchConfig cfg;
    cfg.spsa.iterations = 2;
    cfg.match = tiny_match(2);
    cfg.engine_command = NIGHTWING_ENGINE_PATH;
    const tuner::SpsaResult r = tuner::run_spsa_search(cfg);
    const auto params = tuner::make_search_spsa_params(1.0, 1.0);
    REQUIRE(r.iterations_run == 2);
    REQUIRE(r.theta.size() == params.size());
    for (std::size_t i = 0; i < params.size(); ++i) {
        REQUIRE(r.theta[i] >= params[i].min);
        REQUIRE(r.theta[i] <= params[i].max);
    }
}

#else

TEST_CASE("spsa_search: against a production engine the match aborts naming the tuning option",
          "[tuner][spsa_search]") {
    init_all();
    const auto params = tuner::make_search_spsa_params(1.0, 1.0);
    std::vector<double> theta;
    for (const auto& p : params) {
        theta.push_back(p.start);
    }
    const tuner::UciMatchResult r =
        tuner::play_search_match(NIGHTWING_ENGINE_PATH, params, theta, theta, 1, tiny_match(1));
    REQUIRE(r.aborted);
    REQUIRE(contains(r.error, "NIGHTWING_SEARCH_TUNING"));
}

TEST_CASE("spsa_search: run_spsa_search throws instead of returning a null result on a production engine",
          "[tuner][spsa_search]") {
    init_all();
    tuner::SpsaSearchConfig cfg;
    cfg.spsa.iterations = 1;
    cfg.match = tiny_match(1);
    cfg.engine_command = NIGHTWING_ENGINE_PATH;
    REQUIRE_THROWS_AS(tuner::run_spsa_search(cfg), std::runtime_error);
}

#endif
