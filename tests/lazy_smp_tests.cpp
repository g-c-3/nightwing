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

#include <atomic>
#include <chrono>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "search/thread_group.h"
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
                                                             /*material_weights=*/nullptr, /*eval_weights=*/nullptr,
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
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr, /*eval_weights=*/nullptr,
                                    test_thread_count());
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: num_threads > 1 leaves the position unmodified", "[search][smp]") {
    init_all();
    Position pos = parse_fen("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    const Position before = pos;
    (void)search_iterative_deepening(pos, /*max_depth=*/5, /*time_limit_ms=*/0, /*game_history=*/{},
                                      /*on_iteration=*/nullptr, /*material_weights=*/nullptr, /*eval_weights=*/nullptr,
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
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr, /*eval_weights=*/nullptr,
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
                                    /*on_iteration=*/nullptr, /*material_weights=*/nullptr, /*eval_weights=*/nullptr,
                                    /*num_threads=*/1);
    REQUIRE(default_call.best_move == explicit_one.best_move);
    REQUIRE(default_call.score == explicit_one.score);
    REQUIRE(default_call.nodes == explicit_one.nodes);
}

TEST_CASE("search_iterative_deepening: helper thread diversification (staggered starting "
          "depth, occasional depth skip, tie-break jitter) still finds the same forced "
          "mate-in-3 across several distinct thread counts",
          "[search][smp]") {
    init_all();
    // ROADMAP.md's "Lazy SMP helper thread diversification" item
    // (Priority Fixes, 2026-09-08 section; run_lazy_smp_helper()'s own
    // header comment, search.cpp, has the full design). Distinct from
    // this file's own earlier "num_threads > 1 still finds the same
    // forced mate-in-3" test above (written before diversification
    // existed, and still passing with it now that helper_id feeds a
    // staggered start depth/depth skip/tie-break variant instead of
    // every helper running an identical loop) -- this test specifically
    // sweeps thread counts 2 through 5 to exercise several DISTINCT
    // `helper_id` values (0 through up to 4) in one run, since each id
    // gets a different starting depth (kLazySmpStartDepthSpread wraps
    // at 4) and a different tie-break variant, and a bug specific to
    // one particular id wouldn't necessarily show up at just one thread
    // count. `search_fixed_depth()`, not `search_iterative_deepening()`,
    // since it takes a thread count directly without needing a time
    // budget.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    for (int threads = 2; threads <= 5; ++threads) {
        const SearchResult result = search_fixed_depth(pos, /*depth=*/6, /*game_history=*/{},
                                                         /*material_weights=*/nullptr, /*eval_weights=*/nullptr,
                                                         /*num_threads=*/threads);
        REQUIRE(result.score >= kMateThreshold);
    }
}

// ---------------------------------------------------------------------------
// JoiningThreadGroup (search/thread_group.h): helper threads are always
// stopped and joined, even when thread creation fails or an exception
// unwinds through the owning scope (external review, 2026-10-22). With a
// plain std::vector<std::thread> both cases called std::terminate().
// ---------------------------------------------------------------------------

namespace {

/// Stand-in for std::thread that can be told to fail on a chosen
/// construction and that counts joins.
struct FakeThread {
    static inline int constructed = 0;
    static inline int fail_on = -1;      // zero-based construction index that throws
    static inline bool fail_with_bad_alloc = false;
    static inline int joined = 0;
    static void reset() {
        constructed = 0;
        fail_on = -1;
        fail_with_bad_alloc = false;
        joined = 0;
    }

    template <typename... Args>
    explicit FakeThread(Args&&...) {
        if (constructed++ == fail_on) {
            if (fail_with_bad_alloc) {
                throw std::bad_alloc();
            }
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
        }
    }
    FakeThread(FakeThread&& other) noexcept : joinable_(std::exchange(other.joinable_, false)) {}
    FakeThread(const FakeThread&) = delete;
    [[nodiscard]] bool joinable() const noexcept { return joinable_; }
    void join() {
        ++joined;
        joinable_ = false;
    }

  private:
    bool joinable_ = true;
};

} // namespace

TEST_CASE("thread group: a failed thread creation is reported and the group keeps the earlier workers",
          "[lazy_smp][thread_group]") {
    FakeThread::reset();
    FakeThread::fail_on = 2;
    std::atomic<bool> stop{false};
    {
        nightwing::search::BasicJoiningThreadGroup<FakeThread> group(stop);
        REQUIRE(group.try_spawn(0));
        REQUIRE(group.try_spawn(1));
        REQUIRE_FALSE(group.try_spawn(2)); // system_error from the OS, not an exception to the caller
        REQUIRE(group.size() == 2);
        REQUIRE_FALSE(stop.load());
    }
    // Leaving the scope stopped and joined exactly the two that started.
    REQUIRE(stop.load());
    REQUIRE(FakeThread::joined == 2);
}

TEST_CASE("thread group: bad_alloc during creation is also reported as a failed spawn",
          "[lazy_smp][thread_group]") {
    FakeThread::reset();
    FakeThread::fail_on = 0;
    FakeThread::fail_with_bad_alloc = true;
    std::atomic<bool> stop{false};
    nightwing::search::BasicJoiningThreadGroup<FakeThread> group(stop);
    REQUIRE_FALSE(group.try_spawn(0));
    REQUIRE(group.size() == 0);
}

TEST_CASE("thread group: stop_and_join is idempotent", "[lazy_smp][thread_group]") {
    FakeThread::reset();
    std::atomic<bool> stop{false};
    {
        nightwing::search::BasicJoiningThreadGroup<FakeThread> group(stop);
        REQUIRE(group.try_spawn(0));
        REQUIRE(group.try_spawn(1));
        group.stop_and_join();
        REQUIRE(FakeThread::joined == 2);
        group.stop_and_join(); // explicit call, then again from the destructor
    }
    REQUIRE(FakeThread::joined == 2);
}

TEST_CASE("thread group: an exception unwinding through the owner stops and joins real threads",
          "[lazy_smp][thread_group]") {
    std::atomic<bool> stop{false};
    std::atomic<int> running{0};
    const auto worker = [&stop, &running]() {
        running.fetch_add(1);
        while (!stop.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
        running.fetch_sub(1);
    };

    bool caught = false;
    try {
        nightwing::search::JoiningThreadGroup group(stop);
        REQUIRE(group.try_spawn(worker));
        REQUIRE(group.try_spawn(worker));
        REQUIRE(group.try_spawn(worker));
        // Wait until all three are really running, then fail the "main
        // search" the way a std::bad_alloc inside it would.
        while (running.load() < 3) {
            std::this_thread::yield();
        }
        throw std::runtime_error("main search failed");
    } catch (const std::runtime_error&) {
        caught = true;
    }
    // Reaching this line at all is the point: a joinable std::thread
    // destroyed during unwinding would have called std::terminate().
    REQUIRE(caught);
    REQUIRE(stop.load());
    REQUIRE(running.load() == 0);
}
