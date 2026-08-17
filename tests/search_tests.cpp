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
    (void)search_fixed_depth(pos, 3);
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
    // Correctness (best_move/score/depth_completed) must still match a
    // direct search at the same depth exactly: PVS, the transposition
    // table, and move ordering (see docs/DECISIONS.md, 2026-08-15 TT and
    // move-ordering entries) are all exact techniques -- none of them
    // are allowed to change the final best move or score, only how
    // quickly/cheaply the search gets there.
    //
    // Node count is a different story. This test's comment used to
    // assert id.nodes > direct.nodes on the theory that id necessarily
    // does strictly more total work (depth 1 + depth 2 + depth 3, vs.
    // depth 3 alone). That was true back when nothing was shared between
    // iterative-deepening's own iterations (Phase 2). As of this
    // session, search_iterative_deepening() deliberately shares one
    // TranspositionTable/KillerTable/HistoryTable across all of its own
    // depth iterations (search.cpp) specifically so each deeper
    // iteration benefits from the previous one's TT-move hints, killers,
    // and history -- which is real, working, and can make id's total
    // node count LOWER than a single cold-start direct search at the
    // final depth, exactly as seen here. That's the intended payoff of
    // this session's work, not a regression -- see docs/DECISIONS.md,
    // 2026-08-15 move-ordering entry, for the specific numbers observed
    // and the reasoning for updating this assertion instead of chasing a
    // fixed inequality that a working optimization is expected to break.
    Position pos = start_position();
    const SearchResult direct = search_fixed_depth(pos, 3);
    const SearchResult id = search_iterative_deepening(pos, 3);
    REQUIRE(id.best_move == direct.best_move);
    REQUIRE(id.score == direct.score);
    REQUIRE(id.depth_completed == 3);
    // Sanity bound only: id must have visited at least as many nodes as
    // its own depth-1 pass alone (a fixed, always-unpruned 20 for the
    // start position -- see the depth-1 exact-node-count test above),
    // since that pass always runs in full before any deeper iteration
    // begins.
    REQUIRE(id.nodes >= 20);
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
    (void)search_iterative_deepening(pos, 3);
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

TEST_CASE("search_iterative_deepening: finds a genuine mate-in-3 correctly with IIR active",
          "[search][iir]") {
    init_all();
    // Q+R vs lone king, verified by independent exhaustive search
    // (python-chess, not this engine) to be forced mate in exactly 3
    // full moves for White -- specifically NOT mate in 1 or 2, so this
    // genuinely requires looking 5 plies ahead to find with certainty.
    // At max_depth=6, negamax()'s internal recursion routinely reaches
    // remaining depth >= kIIRMinDepth (4) with no TT entry yet during
    // the earlier, shallower iterative-deepening iterations, so this
    // exercises Internal Iterative Reduction for real, not just at a
    // depth where it can never trigger -- and specifically relies on
    // IIR's actual safety net (iterative deepening's own
    // self-correction across iterations, not per-call exactness -- see
    // negamax()'s header comment in search.cpp) rather than coincidence,
    // since IIR is not a provably-exact technique the way PVS/TT/
    // aspiration windows are.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_fixed_depth: completes and returns a legal move at a depth where IIR engages",
          "[search][iir]") {
    init_all();
    // Coarse regression/safety-net check: a depth request comfortably
    // beyond kIIRMinDepth (4) from the start position doesn't hang,
    // crash, or return anything nonsensical. Doesn't assert an exact
    // best_move/score (IIR is a heuristic, not exact -- see negamax()'s
    // header comment -- so unlike the depth-1/depth-2 tests above, an
    // exact expected value isn't something to hand-verify here).
    Position pos = start_position();
    const SearchResult result = search_fixed_depth(pos, 5);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.depth_completed == 5);
    REQUIRE(result.score > -kMateThreshold);
    REQUIRE(result.score < kMateThreshold);
}
