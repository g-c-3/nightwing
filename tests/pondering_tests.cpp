// tests/pondering_tests.cpp
//
// Pondering-specific tests (ROADMAP.md Phase 7). Two layers, each
// getting its own set of cases below: search::search_iterative_
// deepening()'s new `external_stop` parameter (search.h) — the async
// stop mechanism pondering is built on — and the full UCI-level `go
// ponder` / `ponderhit` / `stop` flow (src/uci/uci.cpp). Deliberately
// separate from tests/search_tests.cpp and tests/uci_tests.cpp (the
// same reasoning tests/lazy_smp_tests.cpp's own header comment gives
// for staying out of search_tests.cpp: this is the one place besides
// Lazy SMP where a real background std::thread is genuinely spawned
// and raced against the main test thread, worth keeping visually
// distinct from the rest of the suite, which is entirely single-
// threaded and synchronous).
//
// What these tests can and can't prove: like lazy_smp_tests.cpp, the
// UCI-level cases below involve genuine background-thread timing (a
// watchdog sleeping for a real duration, a search noticing a stop flag
// only periodically). What's asserted is what holds deterministically
// regardless of scheduling — a `bestmove` eventually appears (or
// deliberately doesn't, for the "abandoned" cases) and is legal when it
// does — not exact timing or exact node/depth counts.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "search/search.h"
#include "uci/uci.h"

using namespace nightwing::board;
using namespace nightwing::search;

namespace {
/// Same per-process-test-case setup convention as every other test file
/// in this suite (see tests/search_tests.cpp's own init_all() comment).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Runs `commands` (one UCI command per element) through uci::run() and
/// returns everything written to `out` as a single string — identical
/// helper to tests/uci_tests.cpp's own run_uci(), duplicated here rather
/// than shared across files (matching this test suite's existing
/// convention throughout of small, self-contained per-file anonymous-
/// namespace helpers rather than a shared test-utility header).
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

// ---------------------------------------------------------------------
// search::search_iterative_deepening()'s `external_stop` parameter
// (search.h) — the async stop mechanism pondering is built on.
// ---------------------------------------------------------------------

TEST_CASE("search_iterative_deepening: a non-null external_stop flipped true from another "
          "thread interrupts a search well short of a generous max_depth",
          "[search][pondering]") {
    init_all();
    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    std::atomic<bool> stop{false};

    std::thread stopper([&stop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        stop.store(true, std::memory_order_relaxed);
    });

    const auto t0 = std::chrono::steady_clock::now();
    const SearchResult result =
        search_iterative_deepening(pos, /*max_depth=*/64, /*time_limit_ms=*/0, /*game_history=*/{},
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr,
                                    /*num_threads=*/1, &stop);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
    stopper.join();

    // Depth 1 always completes unconditionally regardless of
    // external_stop (search.h's own doc comment on that guarantee), so
    // there's always a legal move; a real depth-64 unbounded search on
    // the starting position would otherwise run far longer than the
    // generous margin below.
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.depth_completed < 64);
    REQUIRE(elapsed_ms < 2000);
}

TEST_CASE("search_iterative_deepening: external_stop defaulting to nullptr leaves every "
          "existing call site's behavior unchanged",
          "[search][pondering]") {
    init_all();
    Position pos = parse_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    const SearchResult default_call = search_iterative_deepening(pos, 4);
    const SearchResult explicit_null =
        search_iterative_deepening(pos, /*max_depth=*/4, /*time_limit_ms=*/0, /*game_history=*/{},
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr,
                                    /*num_threads=*/1, /*external_stop=*/nullptr);
    REQUIRE(default_call.best_move == explicit_null.best_move);
    REQUIRE(default_call.score == explicit_null.score);
    REQUIRE(default_call.nodes == explicit_null.nodes);
}

// ---------------------------------------------------------------------
// UCI-level `go ponder` / `ponderhit` / `stop` (src/uci/uci.cpp).
// ---------------------------------------------------------------------

TEST_CASE("uci: 'go ponder' followed by 'ponderhit' with a real time control eventually "
          "produces a legal bestmove",
          "[uci][pondering]") {
    init_all();
    // wtime/btime 1000 -> allocate_time_ms(1000, 0) = 50ms -- the
    // background search's own watchdog (handle_ponderhit(), uci.cpp)
    // fires 50ms after 'ponderhit', a short, deterministic wait for a
    // test. run()'s own final abandon_pondering() call (its doc
    // comment) blocks on this before returning, so by the time run_uci()
    // returns, the watchdog has already fired and the ponder thread has
    // already printed 'bestmove'.
    const std::string out = run_uci({"position startpos moves g1h3",
                                      "go ponder wtime 1000 btime 1000 winc 0 binc 0", "ponderhit",
                                      "quit"});
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: 'go ponder' with no time control at all stops promptly on 'ponderhit' (the "
          "documented immediate-stop fallback) rather than running unbounded",
          "[uci][pondering]") {
    init_all();
    const std::string out =
        run_uci({"position startpos moves g1h3", "go ponder", "ponderhit", "quit"});
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: 'go ponder' followed directly by 'stop' (no 'ponderhit' -- the opponent played "
          "something else) still produces a bestmove, per the UCI spec's own requirement, even "
          "though a real GUI is expected to discard it",
          "[uci][pondering]") {
    init_all();
    const std::string out =
        run_uci({"position startpos moves g1h3",
                  "go ponder wtime 5000 btime 5000 winc 0 binc 0", "stop", "quit"});
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: 'go ponder' abandoned by 'quit' with neither 'ponderhit' nor 'stop' ever "
          "arriving produces NO bestmove -- the defensive abandon_pondering() path, not the "
          "UCI-spec-required stop/ponderhit response",
          "[uci][pondering]") {
    init_all();
    const std::string out =
        run_uci({"position startpos moves g1h3",
                  "go ponder wtime 5000 btime 5000 winc 0 binc 0", "quit"});
    REQUIRE_FALSE(contains(out, "bestmove"));
}

TEST_CASE("uci: 'ponderhit' with no pondering search active is a safe no-op", "[uci][pondering]") {
    init_all();
    const std::string out = run_uci({"ponderhit", "isready", "quit"});
    REQUIRE(contains(out, "readyok"));
    REQUIRE_FALSE(contains(out, "bestmove"));
}

TEST_CASE("uci: 'stop' with no pondering search active is a safe no-op", "[uci][pondering]") {
    init_all();
    const std::string out = run_uci({"stop", "isready", "quit"});
    REQUIRE(contains(out, "readyok"));
    REQUIRE_FALSE(contains(out, "bestmove"));
}

TEST_CASE("uci: a 'position' command arriving mid-ponder (out-of-protocol -- a compliant GUI "
          "always sends 'ponderhit'/'stop' first) is handled defensively: the stale ponder "
          "search is abandoned with no stray bestmove, and the engine keeps working correctly "
          "on the newly specified position afterward",
          "[uci][pondering]") {
    init_all();
    const std::string out = run_uci({"position startpos moves g1h3",
                                      "go ponder wtime 5000 btime 5000 winc 0 binc 0",
                                      "position startpos moves e2e4 e7e5", "go depth 1", "quit"});
    // Exactly one bestmove: the abandoned ponder search's own result is
    // suppressed (abandon_pondering()'s own doc comment), so only the
    // explicit 'go depth 1' below should have produced one.
    std::size_t count = 0;
    std::size_t idx = 0;
    while ((idx = out.find("bestmove ", idx)) != std::string::npos) {
        ++count;
        idx += 9;
    }
    REQUIRE(count == 1);

    // And that one bestmove should be legal from the position AFTER
    // 1.e4 e5, not the abandoned ponder search's own (different) position.
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

TEST_CASE("uci: a second 'go ponder' arriving while one is already active abandons the first "
          "(no stray bestmove from it) and starts the second cleanly",
          "[uci][pondering]") {
    init_all();
    const std::string out = run_uci({"position startpos moves g1h3",
                                      "go ponder wtime 5000 btime 5000 winc 0 binc 0",
                                      "go ponder wtime 1000 btime 1000 winc 0 binc 0", "ponderhit",
                                      "quit"});
    // Only the SECOND ponder search should ever produce a bestmove.
    std::size_t count = 0;
    std::size_t idx = 0;
    while ((idx = out.find("bestmove ", idx)) != std::string::npos) {
        ++count;
        idx += 9;
    }
    REQUIRE(count == 1);
}
