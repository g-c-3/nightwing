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

#include <csignal>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// Clock mode (Session 225): UciMatchConfig::base_time_ms / increment_ms.
// ---------------------------------------------------------------------------

TEST_CASE("uci_clock_go_command: formats both clocks and both increments", "[tuner][uci_match]") {
    CHECK(uci_clock_go_command(60000, 59000, 500) ==
          "go wtime 60000 btime 59000 winc 500 binc 500");
    CHECK(uci_clock_go_command(0, 1, 0) == "go wtime 0 btime 1 winc 0 binc 0");
}

TEST_CASE("uci_go_command: clock mode beats movetime, nodes and depth", "[tuner][uci_match]") {
    UciMatchConfig config;
    config.depth = 7;
    config.nodes = 1000;
    config.movetime_ms = 50;
    CHECK(uci_go_command(config) == "go movetime 50");

    config.base_time_ms = 10000;
    config.increment_ms = 100;
    CHECK(uci_go_command(config) == "go wtime 10000 btime 10000 winc 100 binc 100");
}

TEST_CASE("clock_after_move: subtracts elapsed time and adds the increment",
          "[tuner][uci_match]") {
    long long clock = -1;
    REQUIRE(clock_after_move(1000, 300, 50, clock));
    CHECK(clock == 750);

    REQUIRE(clock_after_move(1000, 0, 0, clock));
    CHECK(clock == 1000);

    // Using exactly the whole clock is still in time; the increment follows.
    REQUIRE(clock_after_move(1000, 1000, 100, clock));
    CHECK(clock == 100);
}

TEST_CASE("clock_after_move: taking longer than the clock loses on time", "[tuner][uci_match]") {
    long long clock = 123;
    CHECK_FALSE(clock_after_move(1000, 1001, 500, clock));
    CHECK(clock == 0);
    CHECK_FALSE(clock_after_move(0, 1, 0, clock));
}

TEST_CASE("play_uci_match: a clock-mode match against the real engine finishes cleanly",
          "[tuner][uci_match]") {
    init_all();
    UciMatchConfig config = fast_config(2);
    config.base_time_ms = 3000;
    config.increment_ms = 100;
    config.max_plies = 24;

    const UciMatchResult result = play_uci_match(real_engine("A"), real_engine("B"), 1, config);

    CHECK_FALSE(result.aborted);
    CHECK(result.error.empty());
    CHECK(result.tally.games_played == 2);
    CHECK(result.illegal_moves_a == 0);
    CHECK(result.illegal_moves_b == 0);
}

TEST_CASE("play_uci_match: running out of time in clock mode loses the game, not the match",
          "[tuner][uci_match]") {
    init_all();
    UciMatchConfig config = fast_config(2);
    config.base_time_ms = 300;
    config.increment_ms = 0;

    // The fake takes 600 ms per move against a 300 ms clock. The time
    // forfeit must be judged before its (illegal) move, so it is counted as
    // a time forfeit and not an illegal move, in both colour assignments.
    const UciMatchResult result =
        play_uci_match(real_engine("A"), fake_engine("slow"), 1, config);

    CHECK_FALSE(result.aborted);
    CHECK(result.tally.games_played == 2);
    CHECK(result.time_forfeits_b == 2);
    CHECK(result.time_forfeits_a == 0);
    CHECK(result.illegal_moves_b == 0);
    CHECK(result.tally.wins_a == 2);
}

TEST_CASE("play_uci_match: a clock-mode engine that ignores stop aborts the match",
          "[tuner][uci_match]") {
    init_all();
    UciMatchConfig config = fast_config(2);
    config.base_time_ms = 200;

    const UciMatchResult result =
        play_uci_match(real_engine("A"), fake_engine("hang"), 1, config);

    CHECK(result.aborted);
    CHECK(result.error.find("stop") != std::string::npos);
    CHECK(result.tally.games_played == 0);
}


TEST_CASE("format_uci_match_summary: reports forfeits and a STOPPED line for a cut-off run",
          "[tuner][uci_match]") {
    UciMatchConfig config;
    config.base_time_ms = 10000;
    UciMatchResult result;
    result.tally.games_played = 10;
    result.tally.wins_a = 3;
    result.tally.wins_b = 5;
    result.tally.draws = 2;
    result.time_forfeits_a = 1;
    result.time_forfeits_b = 2;
    UciMatchRunOptions options;
    options.max_games = 100;
    options.use_sprt = true;
    UciMatchRunOutcome outcome;
    outcome.stopped_by_request = true;

    const std::string text = format_uci_match_summary(config, result, options, outcome);

    CHECK(text.find("games=10 candidate_wins=5 draws=2 baseline_wins=3\n") != std::string::npos);
    CHECK(text.find("time_forfeits: baseline=1 candidate=2\n") != std::string::npos);
    CHECK(text.find("sprt: llr=") != std::string::npos);
    CHECK(text.find("STOPPED: termination requested after 10 of 100 games") != std::string::npos);
    CHECK(text.find("ABORTED") == std::string::npos);
    CHECK(text.back() == '\n');
}

TEST_CASE("format_uci_match_summary: no time_forfeits line outside clock mode, ABORTED when aborted",
          "[tuner][uci_match]") {
    UciMatchConfig config; // base_time_ms == 0
    UciMatchResult result;
    result.aborted = true;
    result.error = "engine B closed its pipe";
    UciMatchRunOptions options;
    UciMatchRunOutcome outcome;

    const std::string text = format_uci_match_summary(config, result, options, outcome);

    CHECK(text.find("time_forfeits") == std::string::npos);
    CHECK(text.find("sprt:") == std::string::npos);
    CHECK(text.find("STOPPED") == std::string::npos);
    CHECK(text.find("ABORTED: engine B closed its pipe\n") != std::string::npos);
}

TEST_CASE("run_uci_match_loop: a stop flag set before the first game plays nothing",
          "[tuner][uci_match]") {
    init_all();
    UciMatchSession session(real_engine("A"), real_engine("B"), fast_config(4));
    REQUIRE(session.start());

    volatile std::sig_atomic_t stop = 1;
    UciMatchRunOptions options;
    options.max_games = 4;
    options.stop_flag = &stop;
    const UciMatchRunOutcome outcome = run_uci_match_loop(session, options);

    CHECK(outcome.stopped_by_request);
    CHECK(session.result().tally.games_played == 0);
    CHECK_FALSE(session.result().aborted);
    session.stop();
}

TEST_CASE("run_uci_match_loop: a stop request between games keeps the finished games only",
          "[tuner][uci_match]") {
    init_all();
    UciMatchSession session(real_engine("A"), real_engine("B"), fast_config(4));
    REQUIRE(session.start());

    volatile std::sig_atomic_t stop = 0;
    std::vector<std::string> lines;
    UciMatchRunOptions options;
    options.max_games = 4;
    options.stop_flag = &stop;
    options.progress = [&](const std::string& line) {
        lines.push_back(line);
        stop = 1; // stop requested right after the first game is reported
    };
    const UciMatchRunOutcome outcome = run_uci_match_loop(session, options);

    CHECK(outcome.stopped_by_request);
    CHECK(session.result().tally.games_played == 1);
    CHECK_FALSE(session.result().aborted);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].rfind("game 1 done:", 0) == 0);

    const std::string text =
        format_uci_match_summary(fast_config(4), session.result(), options, outcome);
    CHECK(text.find("STOPPED: termination requested after 1 of 4 games") != std::string::npos);
    session.stop();
}

TEST_CASE("run_uci_match_loop: without a stop request all games are played and not flagged stopped",
          "[tuner][uci_match]") {
    init_all();
    UciMatchSession session(real_engine("A"), real_engine("B"), fast_config(2));
    REQUIRE(session.start());

    volatile std::sig_atomic_t stop = 0;
    UciMatchRunOptions options;
    options.max_games = 2;
    options.stop_flag = &stop;
    const UciMatchRunOutcome outcome = run_uci_match_loop(session, options);

    CHECK_FALSE(outcome.stopped_by_request);
    CHECK(session.result().tally.games_played == 2);
    session.stop();
}
