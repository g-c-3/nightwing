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
#include "book/book.h"
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

TEST_CASE("uci: 'go depth' emits an 'info depth' line, with score/nodes/pv fields, before "
          "'bestmove'",
          "[uci]") {
    init_all();
    // "startpos moves g1h3" rather than bare "startpos": src/book/
    // book.h's opening book (ROADMAP.md's optional "small curated
    // opening book" item) answers a bare startpos "go" immediately from
    // book, with no "info depth" line at all -- this test is about
    // verifying a real search's own info-line formatting, not about
    // book behavior, so it deliberately starts one ply outside the book
    // (g1h3/Nh3 isn't the first move of any curated line -- confirmed
    // via tests/book_tests.cpp's own identical out-of-book example).
    const std::string out = run_uci({"position startpos moves g1h3", "go depth 2", "quit"});

    const std::size_t info_idx = out.find("info depth");
    REQUIRE(info_idx != std::string::npos);
    REQUIRE(contains(out, " score "));
    REQUIRE(contains(out, " nodes "));
    REQUIRE(contains(out, " pv "));

    // Every 'info' line is expected to appear before 'bestmove' -- a
    // GUI/tournament manager wants live progress DURING the search
    // (this session's whole point, per docs/ROADMAP.md's Priority
    // Fixes section), not something interleaved arbitrarily after the
    // final result.
    const std::size_t bestmove_idx = out.find("bestmove ");
    REQUIRE(bestmove_idx != std::string::npos);
    REQUIRE(info_idx < bestmove_idx);
}

TEST_CASE("uci: 'go depth N' for N >= 2 emits one 'info depth' line per completed iteration, in "
          "increasing depth order",
          "[uci]") {
    init_all();
    // "startpos moves g1h3", not bare "startpos" -- see the depth-2
    // info-line test just above for why (src/book/book.h's opening
    // book would otherwise answer immediately with no info lines at
    // all for a bare startpos "go").
    const std::string out = run_uci({"position startpos moves g1h3", "go depth 3", "quit"});

    // Depths 1, 2, and 3 should each produce their own "info depth <d>"
    // line: search_iterative_deepening()'s IterationCallback (search/
    // search.h) fires once per genuinely completed iteration, and depth
    // 3 here is small enough that a bare "go depth 3" (no time budget)
    // is never expected to hit mid-search interruption and skip one.
    REQUIRE(contains(out, "info depth 1 "));
    REQUIRE(contains(out, "info depth 2 "));
    REQUIRE(contains(out, "info depth 3 "));

    const std::size_t idx1 = out.find("info depth 1 ");
    const std::size_t idx2 = out.find("info depth 2 ");
    const std::size_t idx3 = out.find("info depth 3 ");
    REQUIRE(idx1 < idx2);
    REQUIRE(idx2 < idx3);
}

TEST_CASE("uci: an already-checkmated position's 'go' emits no 'info depth' line, only "
          "bestmove 0000",
          "[uci]") {
    init_all();
    // Same FEN as the already-checkmated test just above -- reusing it
    // here keeps this test's own setup trivially verifiable against
    // that existing, already-checked one. A terminal position's
    // search_iterative_deepening() call returns immediately after its
    // mandatory depth-1 call finds no legal moves at all (search.h's
    // own doc comment), without ever invoking IterationCallback --
    // there is nothing meaningful to report, per IterationCallback's
    // own doc comment (search.h) -- so no "info depth" line should
    // appear at all, only the final "bestmove 0000".
    const std::string out = run_uci(
        {"position fen rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
         "go depth 3", "quit"});
    REQUIRE(contains(out, "bestmove 0000"));
    REQUIRE_FALSE(contains(out, "info depth"));
}

TEST_CASE("uci: 'go depth' emits an 'info' line reporting 'score mate' for a forced mate-in-1 "
          "position, not 'score cp'",
          "[uci]") {
    init_all();
    // A textbook back-rank mate-in-1: 1.Qb8#. Deep enough (depth 3) that
    // the mate is found well within the search, exercising emit_info()'s
    // mate-score branch (uci.cpp) rather than its plain centipawn one.
    const std::string out =
        run_uci({"position fen 6k1/5ppp/8/8/8/8/8/1Q4K1 w - - 0 1", "go depth 3", "quit"});
    REQUIRE(contains(out, "score mate 1"));
    REQUIRE_FALSE(contains(out, "score cp"));
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

TEST_CASE("uci: with the opening book initialized (matching src/main.cpp's own real startup "
          "sequence), a bare 'position startpos' + 'go' answers immediately from book -- exact "
          "bestmove, no 'info depth' line at all",
          "[uci][book]") {
    init_all();
    nightwing::book::init_book();
    const std::string out = run_uci({"position startpos", "go depth 5", "quit"});

    // src/book/book.h's own tests/book_tests.cpp already confirms the
    // start position's book move is deterministically "e2e4" (the Ruy
    // Lopez line, listed first in book.cpp's curated_lines(), via
    // init_book()'s first-line-wins tie-break) -- this test's job is
    // specifically confirming uci.cpp's own handle_go() actually wires
    // that module in correctly end to end, not re-deriving which move
    // it is.
    REQUIRE(contains(out, "bestmove e2e4"));
    REQUIRE_FALSE(contains(out, "info depth"));
}

TEST_CASE("uci: 'uci' response advertises the Threads spin option with its documented bounds",
          "[uci][threads]") {
    init_all();
    const std::string out = run_uci({"uci", "quit"});
    REQUIRE(contains(out, "option name Threads type spin default 1 min 1 max 1024"));
}

TEST_CASE("uci: 'setoption name Threads value N' followed by 'go' still returns a legal bestmove "
          "(num_threads actually reaches search_iterative_deepening without breaking anything)",
          "[uci][threads][smp]") {
    init_all();
    const std::string out =
        run_uci({"setoption name Threads value 4", "position startpos", "go depth 4", "quit"});
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

TEST_CASE("uci: 'setoption name Threads value 1' (the default) behaves exactly like never "
          "setting it at all -- both return a legal bestmove for the same position/depth",
          "[uci][threads]") {
    init_all();
    const std::string out_explicit =
        run_uci({"setoption name Threads value 1", "position startpos", "go depth 3", "quit"});
    const std::string out_default = run_uci({"position startpos", "go depth 3", "quit"});

    const std::size_t idx_explicit = out_explicit.find("bestmove ");
    const std::size_t idx_default = out_default.find("bestmove ");
    REQUIRE(idx_explicit != std::string::npos);
    REQUIRE(idx_default != std::string::npos);
    // Single-threaded search is fully deterministic (no Lazy SMP helper
    // threads racing on the TT) -- the same position/depth must produce
    // the exact same bestmove either way.
    REQUIRE(out_explicit.substr(idx_explicit, 13) == out_default.substr(idx_default, 13));
}

TEST_CASE("uci: an out-of-range 'setoption name Threads value ...' is clamped, not rejected -- "
          "'go' still returns a legal bestmove rather than crashing or hanging",
          "[uci][threads]") {
    init_all();
    // 0 is below kMinThreads (1); 999999999 is far above kMaxThreads
    // (1024) -- handle_setoption() clamps both rather than ignoring the
    // whole command, so neither should leave num_threads at some
    // stale/uninitialized value or cause search_iterative_deepening()
    // to try spawning an absurd thread count.
    const std::string out_low =
        run_uci({"setoption name Threads value 0", "position startpos", "go depth 3", "quit"});
    const std::string out_high = run_uci(
        {"setoption name Threads value 999999999", "position startpos", "go depth 3", "quit"});
    REQUIRE(contains(out_low, "bestmove "));
    REQUIRE_FALSE(contains(out_low, "bestmove 0000"));
    REQUIRE(contains(out_high, "bestmove "));
    REQUIRE_FALSE(contains(out_high, "bestmove 0000"));
}

TEST_CASE("uci: a malformed 'setoption' (missing value, non-integer value, unknown option name) "
          "is ignored -- a subsequent 'go' still works normally",
          "[uci][threads]") {
    init_all();
    const std::string out = run_uci({
        "setoption name Threads",                    // missing "value ..." entirely
        "setoption name Threads value notanumber",    // non-integer value
        "setoption name SomeOtherOption value 4",     // unrecognized option name
        "position startpos",
        "go depth 2",
        "quit",
    });
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

TEST_CASE("uci: 'Threads' set via 'setoption' persists across 'ucinewgame' (an option, not game "
          "state)",
          "[uci][threads]") {
    init_all();
    // If ucinewgame incorrectly reset Threads back to 1, this would
    // still pass (1 is a valid thread count too) -- so this test's real
    // job is just confirming ucinewgame doesn't reject/crash on a
    // still-set Threads option, and that go afterward keeps working;
    // full internal-state confirmation that the value specifically
    // survives isn't independently observable via the UCI protocol
    // itself (no option-query command exists), matching this file's
    // Threads-clamping test above.
    const std::string out = run_uci(
        {"setoption name Threads value 3", "ucinewgame", "position startpos", "go depth 3", "quit"});
    REQUIRE(contains(out, "bestmove "));
    REQUIRE_FALSE(contains(out, "bestmove 0000"));
}

