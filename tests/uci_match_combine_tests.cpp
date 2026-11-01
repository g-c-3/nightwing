// tests/uci_match_combine_tests.cpp
//
// Unit tests for combine_uci_match_summaries() (src/tuner/uci_match.h), the
// pooling step behind sharded clock runs and `nightwing_uci_match --combine`.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "tuner/sprt.h"
#include "tuner/uci_match.h"

using nightwing::tuner::combine_uci_match_summaries;
using nightwing::tuner::compute_sprt;
using nightwing::tuner::SprtConfig;
using nightwing::tuner::SprtStatus;

namespace {

// Summaries as printed by nightwing_uci_match: the four margin-400 clock
// reruns of Session 284 (stopped at the job time cap, no forfeits).
const std::string kRun1 =
    "games=1156 candidate_wins=459 draws=281 baseline_wins=416\n"
    "illegal_moves: baseline=0 candidate=0\n"
    "time_forfeits: baseline=0 candidate=0\n"
    "candidate_elo_vs_baseline=12.9 (rough; see match.h on sample size)\n"
    "sprt: llr=1.004 status=Continue (inconclusive)\n"
    "STOPPED: termination requested after 1156 of 4000 games -- partial result, inconclusive\n";
const std::string kRun2 =
    "games=1152 candidate_wins=448 draws=290 baseline_wins=414\n"
    "illegal_moves: baseline=0 candidate=0\n"
    "time_forfeits: baseline=0 candidate=0\n";
const std::string kRun3 =
    "games=1150 candidate_wins=428 draws=304 baseline_wins=418\n"
    "illegal_moves: baseline=0 candidate=0\n"
    "time_forfeits: baseline=0 candidate=0\n";
const std::string kRun4 =
    "games=1160 candidate_wins=459 draws=278 baseline_wins=423\n"
    "illegal_moves: baseline=0 candidate=0\n"
    "time_forfeits: baseline=0 candidate=0\n";

SprtConfig bounds_0_10() {
    SprtConfig config;
    config.elo0 = 0.0;
    config.elo1 = 10.0;
    return config;
}

} // namespace

TEST_CASE("combine_uci_match_summaries pools tallies and evaluates the SPRT once",
          "[uci_match][combine]") {
    std::string error;
    const std::string out =
        combine_uci_match_summaries({kRun1, kRun2, kRun3, kRun4}, bounds_0_10(), error);
    REQUIRE(error.empty());
    REQUIRE(out.find("combined: 4 runs\n") != std::string::npos);
    REQUIRE(out.find("games=4618 candidate_wins=1794 draws=1153 baseline_wins=1671\n") !=
            std::string::npos);
    REQUIRE(out.find("illegal_moves: baseline=0 candidate=0\n") != std::string::npos);
    REQUIRE(out.find("time_forfeits: baseline=0 candidate=0\n") != std::string::npos);

    const auto expected = compute_sprt(1794, 1153, 1671, bounds_0_10());
    REQUIRE(expected.status == SprtStatus::Continue);
    char line[160];
    std::snprintf(line, sizeof line, "sprt: llr=%.3f bounds=[%.3f, %.3f] status=Continue",
                  expected.llr, expected.lower_bound, expected.upper_bound);
    REQUIRE(out.find(line) != std::string::npos);
    REQUIRE(out.find("COMBINED:") != std::string::npos);
}

TEST_CASE("combine_uci_match_summaries reports a clear pooled win as AcceptH1",
          "[uci_match][combine]") {
    // Two shards each 600 W / 200 D / 200 L: pooled 1200 / 400 / 400.
    const std::string shard = "games=1000 candidate_wins=600 draws=200 baseline_wins=200\n"
                              "illegal_moves: baseline=0 candidate=0\n";
    std::string error;
    const std::string out = combine_uci_match_summaries({shard, shard}, bounds_0_10(), error);
    REQUIRE(error.empty());
    REQUIRE(compute_sprt(1200, 400, 400, bounds_0_10()).status == SprtStatus::AcceptH1);
    REQUIRE(out.find("games=2000 candidate_wins=1200 draws=400 baseline_wins=400\n") !=
            std::string::npos);
    REQUIRE(out.find("status=AcceptH1") != std::string::npos);
    // No run printed a forfeit line, so none is invented.
    REQUIRE(out.find("time_forfeits") == std::string::npos);
}

TEST_CASE("combine_uci_match_summaries sums illegal moves and forfeits", "[uci_match][combine]") {
    const std::string a = "games=10 candidate_wins=4 draws=2 baseline_wins=4\n"
                          "illegal_moves: baseline=1 candidate=0\n"
                          "time_forfeits: baseline=2 candidate=1\n";
    const std::string b = "games=10 candidate_wins=3 draws=3 baseline_wins=4\n"
                          "illegal_moves: baseline=0 candidate=2\n"
                          "time_forfeits: baseline=0 candidate=3\n";
    std::string error;
    const std::string out = combine_uci_match_summaries({a, b}, bounds_0_10(), error);
    REQUIRE(error.empty());
    REQUIRE(out.find("illegal_moves: baseline=1 candidate=2\n") != std::string::npos);
    REQUIRE(out.find("time_forfeits: baseline=2 candidate=4\n") != std::string::npos);
}

TEST_CASE("combine_uci_match_summaries rejects a summary with no tally line",
          "[uci_match][combine]") {
    std::string error;
    const std::string out = combine_uci_match_summaries(
        {kRun1, "ABORTED: engine failed to start\n"}, bounds_0_10(), error);
    REQUIRE(out.empty());
    REQUIRE(error.find("run 2 of 2") != std::string::npos);
}

TEST_CASE("combine_uci_match_summaries rejects an empty list", "[uci_match][combine]") {
    std::string error;
    REQUIRE(combine_uci_match_summaries({}, bounds_0_10(), error).empty());
    REQUIRE_FALSE(error.empty());
}
