// tests/uci_match_tests.cpp
//
// Tests for tuner::UciMatchSession / play_uci_match() (uci_match.h): the
// two-process UCI-vs-UCI match runner. Each TEST_CASE is its own
// process (this repo's isolation convention, see tests/match_tests.cpp),
// so each one initializes the board layer itself.
//
// The real engine binary (NIGHTWING_ENGINE_PATH) and a scripted
// misbehaving engine (NIGHTWING_FAKE_ENGINE_PATH, tests/fake_uci_engine.cpp)
// are both launched as genuine child processes.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "tuner/uci_match.h"

using namespace nightwing::tuner;

namespace {

void init_all() {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();
}

UciEngineSpec real_engine(const char* name) {
    UciEngineSpec spec;
    spec.name = name;
    spec.command = NIGHTWING_ENGINE_PATH;
    return spec;
}

UciEngineSpec fake_engine(const char* mode) {
    UciEngineSpec spec;
    spec.name = std::string("fake-") + mode;
    spec.command = NIGHTWING_FAKE_ENGINE_PATH;
    spec.args = {mode};
    return spec;
}

UciMatchConfig fast_config(int games) {
    UciMatchConfig config;
    config.num_games = games;
    config.depth = 1;
    config.random_opening_plies = 2;
    config.max_plies = 40;
    return config;
}

} // namespace

TEST_CASE("parse_bestmove_line: extracts the move token", "[tuner][uci_match]") {
    CHECK(parse_bestmove_line("bestmove e2e4") == "e2e4");
    CHECK(parse_bestmove_line("bestmove e2e4 ponder e7e5") == "e2e4");
    CHECK(parse_bestmove_line("bestmove 0000") == "0000");
    CHECK(parse_bestmove_line("bestmove") == "");
    CHECK(parse_bestmove_line("info depth 3 score cp 10") == "");
    CHECK(parse_bestmove_line("") == "");
}

TEST_CASE("uci_go_command: movetime beats nodes beats depth", "[tuner][uci_match]") {
    UciMatchConfig config;
    config.depth = 7;
    CHECK(uci_go_command(config) == "go depth 7");

    config.nodes = 5000;
    CHECK(uci_go_command(config) == "go nodes 5000");

    config.movetime_ms = 100;
    CHECK(uci_go_command(config) == "go movetime 100");

    config.movetime_ms = 0;
    config.nodes = 0;
    CHECK(uci_go_command(config) == "go depth 7");
}

TEST_CASE("play_uci_match: a node-limited match against the real engine finishes cleanly",
          "[tuner][uci_match]") {
    init_all();
    UciMatchConfig config = fast_config(2);
    config.nodes = 2000;
    const UciMatchResult result = play_uci_match(real_engine("A"), real_engine("B"), 1, config);

    CHECK_FALSE(result.aborted);
    CHECK(result.error.empty());
    CHECK(result.tally.games_played == 2);
    CHECK(result.illegal_moves_a == 0);
    CHECK(result.illegal_moves_b == 0);
}

TEST_CASE("play_uci_match: real engine vs real engine finishes cleanly", "[tuner][uci_match]") {
    init_all();
    const UciMatchResult result =
        play_uci_match(real_engine("A"), real_engine("B"), 1, fast_config(2));

    CHECK_FALSE(result.aborted);
    CHECK(result.error.empty());
    CHECK(result.tally.games_played == 2);
    CHECK(result.tally.wins_a + result.tally.wins_b + result.tally.draws == 2);
    CHECK(result.illegal_moves_a == 0);
    CHECK(result.illegal_moves_b == 0);
}

TEST_CASE("play_uci_match: a missing executable aborts with an error, not a crash",
          "[tuner][uci_match]") {
    init_all();
    UciEngineSpec missing;
    missing.name = "missing";
    missing.command = "/nonexistent/path/to/no_such_engine";
    UciMatchConfig config = fast_config(2);
    config.handshake_timeout_ms = 3000;

    const UciMatchResult result = play_uci_match(missing, real_engine("B"), 1, config);

    CHECK(result.aborted);
    CHECK_FALSE(result.error.empty());
    CHECK(result.tally.games_played == 0);
}

TEST_CASE("play_uci_match: an illegal bestmove forfeits the game, match continues, colors alternate",
          "[tuner][uci_match]") {
    init_all();
    // Both colors are exercised: in game 0 the fake plays Black, in game 1 White.
    const UciMatchResult result =
        play_uci_match(real_engine("A"), fake_engine("illegal"), 1, fast_config(2));

    CHECK_FALSE(result.aborted);
    CHECK(result.tally.games_played == 2);
    CHECK(result.tally.wins_a == 2);
    CHECK(result.tally.wins_b == 0);
    CHECK(result.illegal_moves_b == 2);
    CHECK(result.illegal_moves_a == 0);
}

TEST_CASE("play_uci_match: an engine that crashes mid-game aborts the match", "[tuner][uci_match]") {
    init_all();
    const UciMatchResult result =
        play_uci_match(real_engine("A"), fake_engine("crash"), 1, fast_config(2));

    CHECK(result.aborted);
    CHECK_FALSE(result.error.empty());
    CHECK(result.tally.games_played == 0);
}

TEST_CASE("play_uci_match: an engine that never answers go times out and aborts the match",
          "[tuner][uci_match]") {
    init_all();
    UciMatchConfig config = fast_config(2);
    config.move_timeout_ms = 500;

    const UciMatchResult result =
        play_uci_match(real_engine("A"), fake_engine("hang"), 1, config);

    CHECK(result.aborted);
    CHECK(result.error.find("timed out") != std::string::npos);
    CHECK(result.tally.games_played == 0);
}
