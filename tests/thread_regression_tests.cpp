// tests/thread_regression_tests.cpp
//
// Structural tests for the two small capabilities this session added
// specifically to support ROADMAP.md Phase 7's last item, "Verify no
// strength regression vs. single-threaded at equal single-thread
// depth": search::search_fixed_depth()'s new `num_threads` parameter
// (search.h), and tuner::MatchConfig's new `threads_a`/`threads_b`
// fields (tuner/match.h) that thread it through a real head-to-head
// match. These are structural/regression tests only — confirming the
// plumbing works end to end and doesn't crash — NOT the actual
// large-scale strength verification this item calls for, which was run
// separately (docs/DECISIONS.md, 2026-09-04 (3), has the real result:
// number of games, configuration, and the score_a/elo_diff verdict).
// Same split as tests/match_tests.cpp's own relationship to Session
// 61/62's real production tuning-comparison runs — a unit test suite
// is the wrong place for a several-hundred-game, minutes-long dispatched
// run, but it IS the right place to confirm the mechanism that run
// depends on actually works.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/psqt.h"
#include "search/search.h"
#include "tuner/match.h"

using namespace nightwing::board;
using namespace nightwing::eval;
using namespace nightwing::search;
using namespace nightwing::tuner;

namespace {

void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Same small-and-fast philosophy as tests/match_tests.cpp's own
/// small_config() (that file's own header comment) — real search-driven
/// play, shallow/short enough to run quickly in CI.
MatchConfig small_config() {
    MatchConfig config;
    config.num_games = 8;
    config.search_depth = 2;
    config.random_opening_plies = 4;
    config.max_plies = 40;
    return config;
}

} // namespace

TEST_CASE("search_fixed_depth: num_threads > 1 still returns a legal move at exactly the "
          "requested depth, same invariants as the num_threads = 1 default",
          "[search][thread_regression]") {
    init_all();
    Position pos = parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const SearchResult result =
        search_fixed_depth(pos, /*depth=*/4, /*game_history=*/{}, /*material_weights=*/nullptr,
                            /*num_threads=*/4);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.depth_completed == 4);
    REQUIRE(result.nodes > 0);
}

TEST_CASE("search_fixed_depth: num_threads defaulting to 1 leaves every existing call site's "
          "behavior unchanged",
          "[search][thread_regression]") {
    init_all();
    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const SearchResult default_call = search_fixed_depth(pos, 4);
    const SearchResult explicit_one =
        search_fixed_depth(pos, /*depth=*/4, /*game_history=*/{}, /*material_weights=*/nullptr,
                            /*num_threads=*/1);
    REQUIRE(default_call.best_move == explicit_one.best_move);
    REQUIRE(default_call.score == explicit_one.score);
    REQUIRE(default_call.nodes == explicit_one.nodes);
}

TEST_CASE("search_fixed_depth: node counts fold in every helper thread's own contribution, not "
          "just the main search_root() call's",
          "[search][thread_regression]") {
    init_all();
    Position pos_1 = parse_fen("r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 0 1");
    Position pos_4 = pos_1;
    const SearchResult with_1_thread = search_fixed_depth(pos_1, 4, {}, nullptr, 1);
    const SearchResult with_4_threads = search_fixed_depth(pos_4, 4, {}, nullptr, 4);
    // 3 extra helper threads each doing real work (not zero) should
    // push the reported total node count well above the single-thread
    // figure -- confirms helper_nodes are actually being folded in
    // (search.cpp's own search_fixed_depth(), the join/fold-in block),
    // not silently dropped.
    REQUIRE(with_4_threads.nodes > with_1_thread.nodes);
}

TEST_CASE("MatchConfig::threads_a/threads_b: a match with different thread counts per side, "
          "same weights on both, runs to completion with the expected number of games and no "
          "crash",
          "[tuner][match][thread_regression]") {
    init_all();
    const MaterialWeights defaults = default_material_weights();
    MatchConfig config = small_config();
    config.threads_a = 1;
    config.threads_b = 4;

    const MatchResult result = play_match(defaults, defaults, 1, config);

    REQUIRE(result.games_played == config.num_games);
    REQUIRE(result.wins_a + result.wins_b + result.draws == result.games_played);
}

TEST_CASE("MatchConfig::threads_a/threads_b: defaulting to 1/1 leaves play_match()'s behavior "
          "identical to before these fields existed",
          "[tuner][match][thread_regression]") {
    init_all();
    const MaterialWeights defaults = default_material_weights();
    MaterialWeights other = defaults;
    other.knight_mg -= 15.0;
    other.knight_eg -= 15.0;

    MatchConfig explicit_default_threads = small_config();
    explicit_default_threads.threads_a = 1;
    explicit_default_threads.threads_b = 1;

    const MatchResult with_implicit_defaults = play_match(defaults, other, 42, small_config());
    const MatchResult with_explicit_ones = play_match(defaults, other, 42, explicit_default_threads);

    REQUIRE(with_implicit_defaults.wins_a == with_explicit_ones.wins_a);
    REQUIRE(with_implicit_defaults.wins_b == with_explicit_ones.wins_b);
    REQUIRE(with_implicit_defaults.draws == with_explicit_ones.draws);
}
