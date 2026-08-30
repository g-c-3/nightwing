// tests/selfplay_tests.cpp
//
// Unit tests for src/tuner/selfplay.h/.cpp — the "self-play data
// generation" half of ROADMAP.md Phase 5's Texel/SPSA tuner item (see
// selfplay.h's own header comment for the full design).
//
// Every SelfPlayConfig used below is deliberately much smaller
// (search_depth, random_opening_plies, max_plies) than selfplay.h's own
// production defaults — this file cares about CORRECTNESS of the
// generation/sampling/serialization logic, not about producing a
// realistic training corpus, and small values keep this file's own
// runtime (each TEST_CASE is its own process, repeatedly calling
// search_fixed_depth() — tests/bench_tests.cpp's own header comment on
// why Debug/ASan runtime matters here) fast across every CI platform/
// config.

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "tuner/selfplay.h"

using namespace nightwing::board;
using namespace nightwing::tuner;

namespace {

// Every Catch2 TEST_CASE below runs as its own separate process
// invocation (same reasoning as every other test file's own init_all()
// — e.g. bench_tests.cpp, search_tests.cpp).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Small config shared by most cases below — real search-driven play
/// (not all-random-opening), but shallow/short enough to run fast. See
/// this file's own header comment.
SelfPlayConfig small_config() {
    SelfPlayConfig config;
    config.search_depth = 2;
    config.random_opening_plies = 4;
    config.max_plies = 30;
    return config;
}

/// True if `fen`'s side to move is in check — duplicated locally rather
/// than exposing selfplay.cpp's own file-local in_check() helper
/// publicly, matching how small, single-purpose predicates are commonly
/// re-verified independently in a test file rather than promoted to a
/// shared header purely to make one test possible (this codebase's own
/// established preference for small, focused headers over convenience
/// exports — e.g. board/movegen.h's own is_square_attacked() being the
/// exposed primitive, with no separate in_check() wrapper, is the same
/// call).
bool fen_is_in_check(const std::string& fen) {
    const Position pos = parse_fen(fen);
    const Bitboard king_bb = pos.pieces(pos.side_to_move, PieceType::King);
    const Square king_sq = bitscan_forward(king_bb);
    return is_square_attacked(pos, king_sq, opposite(pos.side_to_move));
}

} // namespace

TEST_CASE("play_one_game: terminates within max_plies and returns a valid result",
          "[tuner][selfplay]") {
    init_all();
    const SelfPlayGame game = play_one_game(1, small_config());

    REQUIRE(game.ply_count <= small_config().max_plies);
    REQUIRE(game.ply_count >= 0);
    REQUIRE((game.result == 0.0 || game.result == 0.5 || game.result == 1.0));

    // Every sampled position carries the game's own final result.
    for (const SelfPlayPosition& position : game.positions) {
        REQUIRE(position.result == game.result);
    }
}

TEST_CASE("play_one_game: the same seed always reproduces the identical game",
          "[tuner][selfplay]") {
    init_all();
    const SelfPlayGame game_a = play_one_game(777, small_config());
    const SelfPlayGame game_b = play_one_game(777, small_config());

    REQUIRE(game_a.result == game_b.result);
    REQUIRE(game_a.ply_count == game_b.ply_count);
    REQUIRE(game_a.positions.size() == game_b.positions.size());
    for (std::size_t i = 0; i < game_a.positions.size(); ++i) {
        REQUIRE(game_a.positions[i].fen == game_b.positions[i].fen);
        REQUIRE(game_a.positions[i].result == game_b.positions[i].result);
    }
}

TEST_CASE("play_one_game: different seeds diverge during the random opening",
          "[tuner][selfplay]") {
    init_all();
    // Empirically confirmed (not just assumed): these two seeds produce
    // different opening lines under small_config()'s
    // random_opening_plies=4 -- see docs/DECISIONS.md's introducing
    // entry for this file for how this was checked, the same "hand-
    // verified fixture" spirit as e.g. pawn_tt_tests.cpp's own reused
    // doubled-pawns FEN.
    const SelfPlayGame game_a = play_one_game(1, small_config());
    const SelfPlayGame game_b = play_one_game(2, small_config());

    REQUIRE_FALSE(game_a.positions.empty());
    REQUIRE_FALSE(game_b.positions.empty());
    REQUIRE(game_a.positions.front().fen != game_b.positions.front().fen);
}

TEST_CASE("play_one_game: no positions are sampled while every ply is still a random "
          "opening ply",
          "[tuner][selfplay]") {
    init_all();
    SelfPlayConfig config = small_config();
    config.random_opening_plies = config.max_plies; // every ply this game plays is random

    const SelfPlayGame game = play_one_game(3, config);
    REQUIRE(game.positions.empty());
}

TEST_CASE("play_one_game: every sampled position has the side to move NOT in check",
          "[tuner][selfplay]") {
    init_all();
    // A larger sample (several seeds) to exercise the filter across more
    // than one game's worth of positions.
    for (std::uint64_t seed = 10; seed < 15; ++seed) {
        const SelfPlayGame game = play_one_game(seed, small_config());
        for (const SelfPlayPosition& position : game.positions) {
            REQUIRE_FALSE(fen_is_in_check(position.fen));
        }
    }
}

TEST_CASE("play_games: plays exactly num_games games, seeded consecutively from base_seed",
          "[tuner][selfplay]") {
    init_all();
    const std::vector<SelfPlayGame> games = play_games(3, 500, small_config());
    REQUIRE(games.size() == 3);

    for (int i = 0; i < 3; ++i) {
        const SelfPlayGame expected =
            play_one_game(500 + static_cast<std::uint64_t>(i), small_config());
        REQUIRE(games[static_cast<std::size_t>(i)].result == expected.result);
        REQUIRE(games[static_cast<std::size_t>(i)].ply_count == expected.ply_count);
        REQUIRE(games[static_cast<std::size_t>(i)].positions.size() == expected.positions.size());
    }
}

TEST_CASE("write_training_data / read_training_data: round-trips every sampled position",
          "[tuner][selfplay]") {
    init_all();
    const std::vector<SelfPlayGame> games = play_games(2, 900, small_config());

    std::ostringstream out;
    write_training_data(games, out);

    std::istringstream in(out.str());
    const std::vector<SelfPlayPosition> read_back = read_training_data(in);

    std::size_t expected_count = 0;
    for (const SelfPlayGame& game : games) {
        expected_count += game.positions.size();
    }
    REQUIRE(read_back.size() == expected_count);

    std::size_t index = 0;
    for (const SelfPlayGame& game : games) {
        for (const SelfPlayPosition& position : game.positions) {
            REQUIRE(read_back[index].fen == position.fen);
            REQUIRE(read_back[index].result == position.result);
            ++index;
        }
    }
}

TEST_CASE("write_training_data: uses a semicolon to separate the FEN from the result",
          "[tuner][selfplay]") {
    init_all();
    SelfPlayGame game;
    game.result = 1.0;
    game.positions.push_back(
        SelfPlayPosition{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1.0});

    std::ostringstream out;
    write_training_data({game}, out);

    const std::string expected =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1;1.0\n";
    REQUIRE(out.str() == expected);
}

TEST_CASE("read_training_data: skips malformed lines (missing separator, non-numeric result) "
          "rather than throwing",
          "[tuner][selfplay]") {
    std::istringstream in(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1;0.5\n"
        "this line has no separator at all\n"
        "8/8/8/8/8/8/8/8 w - - 0 1;not_a_number\n"
        "4k3/8/8/8/8/8/8/4K3 w - - 0 1;0.0\n");

    const std::vector<SelfPlayPosition> positions = read_training_data(in);
    REQUIRE(positions.size() == 2);
    REQUIRE(positions[0].fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    REQUIRE(positions[0].result == 0.5);
    REQUIRE(positions[1].fen == "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    REQUIRE(positions[1].result == 0.0);
}
