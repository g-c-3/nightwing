// tests/search_tests.cpp
//
// Unit tests for src/search/search.h — Phase 2's fixed-depth alpha-beta
// search and iterative deepening. Positions are built via FEN (fen.h),
// matching the style of movegen_tests.cpp / eval_tests.cpp.

#include <catch2/catch_test_macros.hpp>

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
/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation (catch_discover_tests registers each one as an individual
/// CTest test), so magic-bitboard/attack tables aren't shared across
/// cases the way they'd be in a single long-lived process — each case
/// must initialize them itself. Matches perft_tests.cpp's convention
/// exactly (see that file for why).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("search_fixed_depth: depth 1 node count matches the root's legal move count", "[search]") {
    init_all();
    // At depth 1, negamax() is called once per root move and immediately
    // hits its depth<=0 base case (no further move generation), so total
    // nodes visited should equal exactly the number of legal root moves —
    // 20 from the starting position.
    Position pos = start_position();
    const SearchResult result = search_fixed_depth(pos, 1);
    REQUIRE(result.nodes == 20);
}

TEST_CASE("search_fixed_depth: leaves the position unmodified", "[search]") {
    init_all();
    Position pos = start_position();
    const std::uint64_t hash_before = pos.zobrist_hash;
    search_fixed_depth(pos, 3);
    REQUIRE(pos.zobrist_hash == hash_before);
}

TEST_CASE("search_fixed_depth: picks a move from the actual legal move list", "[search]") {
    init_all();
    Position pos = start_position();
    MoveList legal;
    generate_legal_moves(pos, legal);
    const SearchResult result = search_fixed_depth(pos, 3);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(legal.contains(result.best_move));
}

TEST_CASE("search_fixed_depth: an already-checkmated position returns a null move and -mate", "[search]") {
    init_all();
    // Fool's mate final position (1. f3 e5 2. g4 Qh4#) -- White to move,
    // no legal moves, in check.
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.score == -kMateScore);
}

TEST_CASE("search_fixed_depth: an already-stalemated position returns a null move and a draw score", "[search]") {
    init_all();
    // Classic K+Q vs K stalemate: Black to move, no legal moves, not in
    // check.
    Position pos = parse_fen("7k/5Q2/7K/8/8/8/8/8 b - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("search_fixed_depth: finds a back-rank mate in 1", "[search]") {
    init_all();
    // White to move: Ra1-a8# is mate (Black king g8 boxed in by its own
    // f7/g7/h7 pawns, rook covers the entire back rank once the king
    // steps off it). Depth 2 is required (not 1) for the search to
    // actually discover the resulting position has no legal replies --
    // see search.h's header comment on why a mate exactly at the search
    // horizon isn't detected as a mate score.
    Position pos = parse_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.best_move.from() == make_square(0, 0)); // a1
    REQUIRE(result.best_move.to() == make_square(0, 7));   // a8
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_fixed_depth: depth_completed matches the requested depth", "[search]") {
    init_all();
    Position pos = start_position();
    const SearchResult result = search_fixed_depth(pos, 3);
    REQUIRE(result.depth_completed == 3);
}

TEST_CASE("search_fixed_depth: depth_completed stays 0 for an already-terminal position", "[search]") {
    init_all();
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.depth_completed == 0);
}

TEST_CASE("search_iterative_deepening: max_depth 1 matches search_fixed_depth(pos, 1) exactly", "[search][id]") {
    init_all();
    Position pos = start_position();
    const SearchResult fixed = search_fixed_depth(pos, 1);
    const SearchResult id = search_iterative_deepening(pos, 1);
    REQUIRE(id.best_move == fixed.best_move);
    REQUIRE(id.score == fixed.score);
    REQUIRE(id.nodes == fixed.nodes);
    REQUIRE(id.depth_completed == 1);
}

TEST_CASE("search_iterative_deepening: unlimited time reaches max_depth with the same best move/score as a direct search", "[search][id]") {
    init_all();
    // No TT/move-ordering shared between iterations yet (Phase 2), so the
    // deepest iteration's best_move/score should be bit-for-bit identical
    // to calling search_fixed_depth() directly at that same depth --
    // search_iterative_deepening() is just repeated calls to it.
    Position pos = start_position();
    const SearchResult direct = search_fixed_depth(pos, 3);
    const SearchResult id = search_iterative_deepening(pos, 3);
    REQUIRE(id.best_move == direct.best_move);
    REQUIRE(id.score == direct.score);
    REQUIRE(id.depth_completed == 3);
    // id.nodes accumulates depth 1 + depth 2 + depth 3's work, so it must
    // be strictly more than depth 3 alone.
    REQUIRE(id.nodes > direct.nodes);
}

TEST_CASE("search_iterative_deepening: an already-checkmated position returns a null move and -mate", "[search][id]") {
    init_all();
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    const SearchResult result = search_iterative_deepening(pos, 5);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.score == -kMateScore);
    REQUIRE(result.depth_completed == 0);
    REQUIRE(result.nodes == 1);
}

TEST_CASE("search_iterative_deepening: a tiny time budget stops before max_depth", "[search][id]") {
    init_all();
    // Depth 6 unpruned from the starting position is far more work than
    // any real machine finishes in ~1ms, so this is a safe, non-flaky
    // assertion regardless of how fast or slow the CI runner is -- it
    // deliberately does NOT assert which exact depth was reached (that
    // would be timing-dependent), only that the time budget actually cut
    // the search off short of max_depth.
    Position pos = start_position();
    const SearchResult result = search_iterative_deepening(pos, 6, 1);
    REQUIRE(result.depth_completed >= 1);
    REQUIRE(result.depth_completed < 6);
    REQUIRE_FALSE(result.best_move.is_null());
}

TEST_CASE("search_iterative_deepening: leaves the position unmodified", "[search][id]") {
    init_all();
    Position pos = start_position();
    const std::uint64_t hash_before = pos.zobrist_hash;
    search_iterative_deepening(pos, 3);
    REQUIRE(pos.zobrist_hash == hash_before);
}

TEST_CASE("search_iterative_deepening: picks a move from the actual legal move list", "[search][id]") {
    init_all();
    Position pos = start_position();
    MoveList legal;
    generate_legal_moves(pos, legal);
    const SearchResult result = search_iterative_deepening(pos, 3);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(legal.contains(result.best_move));
}
