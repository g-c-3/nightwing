// tests/production_path_tests.cpp
//
// ROADMAP.md "Audit fixed-depth vs. production-path test coverage"
// (report finding 12). tests/endgame_suite_tests.cpp validates the full
// engine's endgame judgment, but every one of its cases calls
// search_fixed_depth(), which passes `limits == nullptr` into negamax().
// The real UCI `go` path is search_iterative_deepening(), where depth >= 2
// iterations run with a non-null `limits` and therefore:
//   - fire Internal Iterative Reduction (search.cpp, `!probe.hit &&
//     depth >= kIIRMinDepth && limits != nullptr`), the only
//     `limits`-gated behavior that changes the search TREE (every other
//     `limits` use only matters once a search has been stopped);
//   - reuse a warm TT across iterations; and
//   - search under aspiration windows.
// None of that is exercised by the fixed-depth suite, so an endgame
// regression visible only on the production path would slip through.
//
// This file re-runs the same positions and the same deliberately loose
// assertions as endgame_suite_tests.cpp through
// search_iterative_deepening() with no time limit (time_limit_ms = 0), so
// results are deterministic (single thread, no deadline). Measured on the
// current engine before these assertions were written (Release, GCC 13):
//   KPK unstoppable -962, KPK rook pawn 0, KBPK wrong bishop 0,
//   KNK 0, KRK 741, KBNK 909, Lucena 581 (g1c1), Philidor 135,
//   opposite-colored bishops 222 vs same-colored 310 (depth 7).
// Exact scores are NOT asserted (same policy as the fixed-depth suite).
//
// Balance policy going forward: a new test whose subject is search
// behavior or full-engine judgment should use search_iterative_deepening()
// unless it specifically needs fixed-depth semantics (cold TT, no IIR,
// reproducibility against a single `depth`). search_fixed_depth() remains
// right for isolating a pruning/extension technique, for tests that
// compare cold-versus-warm TT behavior, and for UCI-independent unit tests.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "search/search.h"

using namespace nightwing::board;
using namespace nightwing::search;

namespace {
/// Initializes the magic-bitboard, mask and Zobrist tables. Matches the
/// identical local helper in tests/endgame_suite_tests.cpp (each test file
/// keeps its own copy; none is exported).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Runs the production search path to exactly `depth` plies with no time
/// limit, from a FEN. `search_iterative_deepening()` with `time_limit_ms =
/// 0` is deterministic for a single thread, which these assertions rely on.
SearchResult search_production(const char* fen, int depth) {
    Position pos = parse_fen(fen);
    return search_iterative_deepening(pos, depth, /*time_limit_ms=*/0);
}
} // namespace

TEST_CASE("production path: KPK -- an unstoppable pawn is a decisive loss for the side to move",
          "[production_path][endgame_suite][kpk]") {
    init_all();
    const SearchResult result = search_production("k7/8/4P3/8/8/8/8/K7 b - - 0 1", 8);
    REQUIRE(result.score < -400);
}

TEST_CASE("production path: KPK -- a rook pawn the defending king can catch stays near balanced",
          "[production_path][endgame_suite][kpk]") {
    init_all();
    const SearchResult result = search_production("2k5/8/8/P7/8/8/8/7K w - - 0 1", 10);
    REQUIRE(result.score >= -60);
    REQUIRE(result.score <= 60);
}

TEST_CASE("production path: KBPK -- the wrong bishop for a rook pawn's corner stays near balanced",
          "[production_path][endgame_suite][kbpk]") {
    init_all();
    const SearchResult result = search_production("6k1/8/7P/8/8/8/2b5/6K1 w - - 0 1", 6);
    REQUIRE(result.score >= -80);
    REQUIRE(result.score <= 80);
}

TEST_CASE("production path: insufficient material -- king and knight vs. king is an exact draw",
          "[production_path][endgame_suite][insufficient_material]") {
    init_all();
    const SearchResult result = search_production("7k/8/8/8/8/8/8/N6K w - - 0 1", 4);
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("production path: KRK -- a decisive advantage well beyond the rook's raw value",
          "[production_path][endgame_suite][krk]") {
    init_all();
    const SearchResult result = search_production("7k/8/8/8/8/8/8/R3K3 w - - 0 1", 8);
    REQUIRE(result.score > 400);
}

TEST_CASE("production path: KBNK -- still clearly winning, not merely balanced material",
          "[production_path][endgame_suite][kbnk]") {
    init_all();
    const SearchResult result = search_production("k7/8/8/8/8/8/8/B1N1K3 w - - 0 1", 8);
    REQUIRE(result.score > 400);
}

TEST_CASE("production path: the canonical Lucena position -- decisive, with 1.Rc1 as best move",
          "[production_path][endgame_suite][lucena]") {
    init_all();
    // Same sourced FEN as tests/endgame_suite_tests.cpp (English Wikipedia
    // "Lucena position"; main line begins 1.Rc1).
    const SearchResult result = search_production("4K3/2k1P3/8/8/8/8/5r2/6R1 w - - 0 1", 8);
    REQUIRE(result.score > 400);
    REQUIRE(result.best_move.from() == make_square(6, 0)); // g1
    REQUIRE(result.best_move.to() == make_square(2, 0));   // c1
}

TEST_CASE("production path: a Philidor-pattern position stays well short of the Lucena score",
          "[production_path][endgame_suite][philidor]") {
    init_all();
    const SearchResult result = search_production("4k3/8/r7/4P3/8/8/8/K6R w - - 0 1", 8);
    REQUIRE(result.score < 250);
}

TEST_CASE("production path: opposite-colored bishops score lower than same-colored bishops",
          "[production_path][endgame_suite][opposite_colored_bishops]") {
    init_all();
    // Depth 7 on purpose: on THIS path the pair gives 222 vs 310 (margin
    // +88). Depth 8 flips it (312 vs 276, margin -36), the same non-monotone
    // alpha-beta artifact tests/endgame_suite_tests.cpp documents for the
    // fixed-depth path (DECISIONS.md 2026-09-23 (3)); depths 5, 9 and 10 also
    // keep the expected order, so 8 is an unlucky pick, not a fragile term.
    const SearchResult opposite = search_production("6k1/3b4/8/4p3/2P1PP2/8/3B4/6K1 w - - 0 1", 7);
    const SearchResult same = search_production("6k1/4b3/8/4p3/2P1PP2/8/3B4/6K1 w - - 0 1", 7);
    REQUIRE(opposite.score < same.score);
}

TEST_CASE("production path: repeated identical searches are deterministic",
          "[production_path][determinism]") {
    init_all();
    // The assertions above are only meaningful as regression tests if the
    // production path is reproducible. Three runs of one position at a depth
    // past kIIRMinDepth must agree on score, best move and node count.
    const char* fen = "4K3/2k1P3/8/8/8/8/5r2/6R1 w - - 0 1";
    const SearchResult first = search_production(fen, 8);
    for (int run = 0; run < 2; ++run) {
        const SearchResult again = search_production(fen, 8);
        REQUIRE(again.score == first.score);
        REQUIRE(again.best_move == first.best_move);
        REQUIRE(again.nodes == first.nodes);
    }
}
