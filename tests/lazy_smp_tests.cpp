// tests/lazy_smp_tests.cpp
//
// Lazy SMP-specific tests (ROADMAP.md Phase 7, search.h's
// search_iterative_deepening() doc comment on its `num_threads`
// parameter). Deliberately separate from tests/search_tests.cpp (which
// stays entirely single-threaded, `num_threads` defaulted to 1) --
// these are the only cases in the suite that spawn real std::thread
// helpers, so keeping them isolated makes it obvious at a glance which
// tests are exercising genuine concurrency versus everything else,
// which is not.
//
// What these tests can and can't prove: Lazy SMP is fundamentally
// non-deterministic (helper threads' exact timing, hence exactly which
// TT entries they've populated by the time the main thread checks, is
// not reproducible run to run) -- so these tests do NOT assert exact
// node counts, exact depth reached, or bit-for-bit identical scores
// against the single-threaded path. What IS asserted, and does hold
// deterministically regardless of thread scheduling: num_threads > 1
// never crashes, never hangs (every test here has an unconditional
// wall-clock or max_depth bound), never returns an illegal move, and
// still finds the objectively correct answer on positions where the
// correct answer is unambiguous (a forced mate someone can check by
// hand). Genuine multi-threaded correctness (no data races) is a
// property these tests can gesture at (running the same case with
// ASan/UBSan already active in CI, per docs/ARCHITECTURE.md's Testing
// row, at least exercises the code under real concurrent load many
// times over many CI runs) but cannot prove outright -- true race
// detection needs ThreadSanitizer, which is not yet part of this
// project's sanitizer matrix (see docs/DECISIONS.md's Lazy SMP entry
// for why striped TT locking was chosen specifically to make that gap
// low-risk in the meantime).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "search/search.h"

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

/// How many helper threads to ask for in tests below: the hardware's
/// own concurrency, clamped to [2, 4] so this test suite doesn't spawn
/// dozens of threads on a big CI/dev machine just to exercise the same
/// code path a handful would already cover, while still guaranteeing at
/// least 1 real helper thread (num_threads >= 2) even on a single-core
/// CI runner where hardware_concurrency() can legally return 0 or 1.
int test_thread_count() {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        hw = 4;
    }
    if (hw < 2) {
        hw = 2;
    }
    if (hw > 4) {
        hw = 4;
    }
    return static_cast<int>(hw);
}
} // namespace

TEST_CASE("search_iterative_deepening: num_threads > 1 finds a legal move and doesn't hang, "
          "starting position, generous depth",
          "[search][smp]") {
    init_all();
    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const SearchResult result = search_iterative_deepening(pos, /*max_depth=*/6, /*time_limit_ms=*/0,
                                                             /*game_history=*/{}, /*on_iteration=*/nullptr,
                                                             /*material_weights=*/nullptr,
                                                             test_thread_count());
    REQUIRE_FALSE(result.best_move.is_null());

    MoveList legal;
    generate_legal_moves(pos, legal);
    bool found = false;
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i] == result.best_move) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("search_iterative_deepening: num_threads > 1 still finds the same forced mate-in-3 as "
          "the single-threaded path",
          "[search][smp]") {
    init_all();
    // Same fixture as tests/search_tests.cpp's IIR/NMP mate-in-3 cases
    // (Q+R vs lone king, independently verified forced mate in exactly
    // 3 full moves) -- reused here specifically because its correct
    // answer (a mating score) is unambiguous and checkable, unlike a
    // "which quiet move is objectively best" position, so this is a
    // genuine correctness check on the multi-threaded path, not just a
    // no-crash smoke test.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result =
        search_iterative_deepening(pos, /*max_depth=*/6, /*time_limit_ms=*/0, /*game_history=*/{},
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr,
                                    test_thread_count());
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: num_threads > 1 leaves the position unmodified", "[search][smp]") {
    init_all();
    Position pos = parse_fen("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    const Position before = pos;
    (void)search_iterative_deepening(pos, /*max_depth=*/5, /*time_limit_ms=*/0, /*game_history=*/{},
                                      /*on_iteration=*/nullptr, /*material_weights=*/nullptr,
                                      test_thread_count());
    REQUIRE(pos.zobrist_hash == before.zobrist_hash);
}

TEST_CASE("search_iterative_deepening: num_threads > 1 respects a small time budget (helper "
          "threads don't keep the call from returning promptly)",
          "[search][smp]") {
    init_all();
    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const auto t0 = std::chrono::steady_clock::now();
    const SearchResult result =
        search_iterative_deepening(pos, /*max_depth=*/64, /*time_limit_ms=*/100, /*game_history=*/{},
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr,
                                    test_thread_count());
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
    REQUIRE_FALSE(result.best_move.is_null());
    // Generous margin above the 100ms budget: this is bounding "did
    // helper-thread join() hang / take unreasonably long", not
    // asserting tight scheduling precision -- CI runners can be slow
    // and oversubscribed. The main thread's own deadline check already
    // has its own tighter-margin coverage in tests/search_tests.cpp's
    // single-threaded time-budget tests; this test's only job is
    // confirming that joining however many helper threads were spawned
    // doesn't itself blow the budget open.
    REQUIRE(elapsed_ms < 2000);
}

TEST_CASE("search_iterative_deepening: num_threads == 1 is unaffected by the parameter's mere "
          "existence (default-argument call site still compiles and behaves identically)",
          "[search][smp]") {
    init_all();
    Position pos = parse_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    const SearchResult default_call = search_iterative_deepening(pos, 4);
    const SearchResult explicit_one =
        search_iterative_deepening(pos, /*max_depth=*/4, /*time_limit_ms=*/0, /*game_history=*/{},
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr,
                                    /*num_threads=*/1);
    REQUIRE(default_call.best_move == explicit_one.best_move);
    REQUIRE(default_call.score == explicit_one.score);
    REQUIRE(default_call.nodes == explicit_one.nodes);
}
