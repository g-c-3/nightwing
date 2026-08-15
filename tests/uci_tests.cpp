// tests/uci_tests.cpp
//
// Unit tests for src/uci/uci.h — Phase 2's basic UCI loop. Drives
// uci::run() with std::istringstream/std::ostringstream instead of real
// stdin/stdout (see uci.h's header comment on why run() takes streams
// explicitly), so these are ordinary, fast, deterministic unit tests —
// no actual process I/O involved.

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "uci/uci.h"

using namespace nightwing::board;

namespace {
/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation (catch_discover_tests registers each one as an individual
/// CTest test), so magic-bitboard/attack tables aren't shared across
/// cases — each case must initialize them itself. Matches
/// perft_tests.cpp / search_tests.cpp's convention exactly.
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Runs `commands` (one UCI command per element) through uci::run() and
/// returns everything written to `out` as a single string.
std::string run_uci(const std::vector<std::string>& commands) {
    std::ostringstream in_builder;
    for (const std::string& cmd : commands) {
        in_builder << cmd << '\n';
    }
    std::istringstream in(in_builder.str());
    std::ostringstream out;
    nightwing::uci::run(in, out);
    return out.str();
}

/// Returns true if `haystack` contains `needle` as a substring.
bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
} // namespace

TEST_CASE("uci: 'uci' command responds with id and uciok", "[uci]") {
    init_all();
    const std::string out = run_uci({"uci", "quit"});
    REQUIRE(contains(out, "id name Nightwing"));
    REQUIRE(contains(out, "id author"));
    REQUIRE(contains(out, "uciok"));
}

TEST_CASE("uci: 'isready' responds with readyok", "[uci]") {
    init_all();
    const std::string out = run_uci({"isready", "quit"});
    REQUIRE(contains(out, "readyok"));
}

TEST_CASE("uci: 'quit' stops the loop -- later commands are never processed", "[uci]") {
    init_all();
    const std::string out = run_uci({"quit", "isready"});
    REQUIRE_FALSE(contains(out, "readyok"));
}

TEST_CASE("uci: bare 'go' from the default (start) position returns a legal bestmove", "[uci]") {
    init_all();
    const std::string out = run_uci({"go", "quit"});
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: 'position startpos' + 'go depth 1' returns a legal bestmove", "[uci]") {
    init_all();
    const std::string out = run_uci({"position startpos", "go depth 1", "quit"});

    // Extract the token after "bestmove " and confirm it's one of the
    // starting position's actual legal moves.
    const std::size_t pos_idx = out.find("bestmove ");
    REQUIRE(pos_idx != std::string::npos);
    const std::string move_str = out.substr(pos_idx + 9, 4);

    Position start = start_position();
    MoveList legal;
    generate_legal_moves(start, legal);
    bool found = false;
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].to_uci().substr(0, 4) == move_str) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("uci: 'position startpos moves ...' applies the moves before searching", "[uci]") {
    init_all();
    const std::string out =
        run_uci({"position startpos moves e2e4 e7e5", "go depth 1", "quit"});

    Position pos = start_position();
    MoveList legal;
    generate_legal_moves(pos, legal);
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].to_uci() == "e2e4") {
            UndoInfo undo;
            make_move(pos, legal[i], undo);
            break;
        }
    }
    generate_legal_moves(pos, legal);
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].to_uci() == "e7e5") {
            UndoInfo undo;
            make_move(pos, legal[i], undo);
            break;
        }
    }
    // pos is now the position after 1. e4 e5 -- confirm the engine's
    // bestmove is legal *there*, not from the starting position (which
    // would be a sign "moves" wasn't applied).
    generate_legal_moves(pos, legal);

    const std::size_t pos_idx = out.find("bestmove ");
    REQUIRE(pos_idx != std::string::npos);
    const std::string move_str = out.substr(pos_idx + 9, 4);
    bool found = false;
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].to_uci().substr(0, 4) == move_str) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("uci: 'position fen ...' for an already-checkmated position returns bestmove 0000", "[uci]") {
    init_all();
    const std::string out = run_uci(
        {"position fen rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
         "go depth 3", "quit"});
    REQUIRE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: 'go movetime' returns promptly with a legal bestmove", "[uci]") {
    init_all();
    const std::string out = run_uci({"position startpos", "go movetime 50", "quit"});
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: 'go wtime/btime' returns a legal bestmove", "[uci]") {
    init_all();
    const std::string out =
        run_uci({"position startpos", "go wtime 5000 btime 5000 winc 0 binc 0", "quit"});
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: a malformed FEN is ignored, leaving the position at its prior value", "[uci]") {
    init_all();
    const std::string out =
        run_uci({"position fen not a real fen at all", "go depth 1", "quit"});
    // Malformed FEN is silently ignored, so the position stays at
    // run()'s initial default (the start position) -- bestmove should
    // still be a legal start-position move, not a crash or garbage output.
    const std::size_t pos_idx = out.find("bestmove ");
    REQUIRE(pos_idx != std::string::npos);
    const std::string move_str = out.substr(pos_idx + 9, 4);

    Position start = start_position();
    MoveList legal;
    generate_legal_moves(start, legal);
    bool found = false;
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].to_uci().substr(0, 4) == move_str) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("uci: unrecognized commands are silently ignored, not fatal", "[uci]") {
    init_all();
    const std::string out =
        run_uci({"thisisnotarealcommand", "setoption name Hash value 16", "isready", "quit"});
    REQUIRE(contains(out, "readyok"));
}

TEST_CASE("uci: 'ucinewgame' resets to the starting position", "[uci]") {
    init_all();
    const std::string out = run_uci(
        {"position startpos moves e2e4", "ucinewgame", "go depth 1", "quit"});
    const std::size_t pos_idx = out.find("bestmove ");
    REQUIRE(pos_idx != std::string::npos);
    const std::string move_str = out.substr(pos_idx + 9, 4);

    Position start = start_position();
    MoveList legal;
    generate_legal_moves(start, legal);
    bool found = false;
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].to_uci().substr(0, 4) == move_str) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("uci: a full self-play-style exchange (uci/isready/position/go, twice) works end to end", "[uci]") {
    init_all();
    const std::string out = run_uci({
        "uci",
        "isready",
        "ucinewgame",
        "position startpos",
        "go depth 1",
        "position startpos moves e2e4",
        "go depth 1",
        "quit",
    });
    REQUIRE(contains(out, "uciok"));
    REQUIRE(contains(out, "readyok"));
    // Two "go depth 1" calls should produce two "bestmove " lines.
    std::size_t count = 0;
    std::size_t idx = 0;
    while ((idx = out.find("bestmove ", idx)) != std::string::npos) {
        ++count;
        idx += 9;
    }
    REQUIRE(count == 2);
}
