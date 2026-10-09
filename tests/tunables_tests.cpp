// tests/tunables_tests.cpp
//
// SPSA search-constant tuning build (ROADMAP.md "SPSA tuner" step (2b);
// docs/DECISIONS.md 2026-10-09 (3)). The same file is compiled in both
// configurations: with NIGHTWING_SEARCH_TUNING OFF (default) it verifies
// the options do not exist; with it ON it verifies the options exist, are
// clamped, and reach the search.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "board/attacks.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "search/tunables.h"
#include "uci/uci.h"

using namespace nightwing;

namespace {

void init_all() {
    board::init_masks();
    board::init_magic_bitboards();
    board::init_zobrist_keys();
}

std::string run_uci(const std::vector<std::string>& commands) {
    std::ostringstream in_builder;
    for (const std::string& cmd : commands) {
        in_builder << cmd << '\n';
    }
    std::istringstream in(in_builder.str());
    std::ostringstream out;
    uci::run(in, out);
    return out.str();
}

} // namespace

#if defined(NIGHTWING_SEARCH_TUNING)

namespace {
/// Extracts N from the `Nodes searched  : N` line of `bench` output (0 if absent).
std::uint64_t bench_nodes(const std::string& out) {
    const std::string key = "Nodes searched  : ";
    const std::size_t pos = out.find(key);
    if (pos == std::string::npos) {
        return 0;
    }
    return std::stoull(out.substr(pos + key.size()));
}
} // namespace

TEST_CASE("tunables: tuning build advertises one spin option per tunable and accepts setoption",
          "[tunables][uci]") {
    init_all();
    search::reset_search_tunables();
    const std::string out = run_uci({"uci", "quit"});
    for (const search::TunableSpec& spec : search::kSearchTunableSpecs) {
        const std::string expected = "option name " + std::string(spec.name) +
                                     " type spin default " + std::to_string(spec.default_value) +
                                     " min " + std::to_string(spec.min_value) + " max " +
                                     std::to_string(spec.max_value);
        REQUIRE(out.find(expected) != std::string::npos);
    }
}

TEST_CASE("tunables: set_search_tunable clamps to range and rejects unknown names",
          "[tunables]") {
    search::reset_search_tunables();
    REQUIRE(search::set_search_tunable("ProbCutMargin", 100000));
    REQUIRE(search::g_search_tunables.probcut_margin == 600);
    REQUIRE(search::set_search_tunable("ProbCutMargin", -5));
    REQUIRE(search::g_search_tunables.probcut_margin == 20);
    REQUIRE_FALSE(search::set_search_tunable("NoSuchOption", 3));
    search::reset_search_tunables();
    REQUIRE(search::g_search_tunables.probcut_margin == 200);
}

TEST_CASE("tunables: every advertised default equals the SearchTunables member default",
          "[tunables]") {
    const search::SearchTunables defaults{};
    for (const search::TunableSpec& spec : search::kSearchTunableSpecs) {
        REQUIRE(defaults.*(spec.field) == spec.default_value);
        REQUIRE(spec.min_value <= spec.default_value);
        REQUIRE(spec.default_value <= spec.max_value);
    }
}

TEST_CASE("tunables: setoption over UCI changes the tunable; a non-integer value is ignored",
          "[tunables][uci]") {
    init_all();
    search::reset_search_tunables();
    (void)run_uci({"setoption name NullMoveReduction value 4", "quit"});
    REQUIRE(search::g_search_tunables.null_move_reduction == 4);
    (void)run_uci({"setoption name NullMoveReduction value abc", "quit"});
    REQUIRE(search::g_search_tunables.null_move_reduction == 4);
    search::reset_search_tunables();
}

TEST_CASE("tunables: defaults reproduce bench; changing search constants changes bench",
          "[tunables][bench]") {
    init_all();
    search::reset_search_tunables();
    const std::uint64_t base = bench_nodes(run_uci({"bench", "quit"}));
    REQUIRE(base > 0);

    // Same defaults again: deterministic single-thread bench, identical total.
    REQUIRE(bench_nodes(run_uci({"bench", "quit"})) == base);

    // A much smaller ProbCut margin and a larger null-move reduction both
    // change pruning, so the node total must differ from the default.
    REQUIRE(search::set_search_tunable("ProbCutMargin", 20));
    REQUIRE(search::set_search_tunable("NullMoveReduction", 5));
    REQUIRE(bench_nodes(run_uci({"bench", "quit"})) != base);

    search::reset_search_tunables();
    REQUIRE(bench_nodes(run_uci({"bench", "quit"})) == base);
}

#else

TEST_CASE("tunables: production build advertises no tuning options and ignores their setoption",
          "[tunables][uci]") {
    init_all();
    static_assert(!search::kSearchTuningBuild);
    const std::string out = run_uci({"setoption name ProbCutMargin value 20", "uci", "quit"});
    REQUIRE(out.find("ProbCutMargin") == std::string::npos);
    REQUIRE(out.find("NullMoveReduction") == std::string::npos);
    REQUIRE(out.find("uciok") != std::string::npos);
}

#endif
