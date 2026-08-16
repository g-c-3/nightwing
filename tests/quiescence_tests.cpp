// tests/quiescence_tests.cpp
//
// Unit tests for src/search/quiescence.h, calling quiescence() directly
// (not through the full negamax() search) to isolate its own behavior:
// stand-pat, capture search, SEE pruning's actual effect on node count
// (not just "the search doesn't play a bad move," which alpha-beta
// alone would already guarantee -- these tests specifically check that
// a losing capture is skipped rather than searched and rejected),
// include_checks, in-check evasion handling, and terminal detection.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "search/quiescence.h"
#include "search/search.h"

using namespace nightwing::board;
using namespace nightwing::eval;
using namespace nightwing::search;

namespace {
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("quiescence: a quiet position with no captures returns exactly the stand-pat eval",
          "[quiescence]") {
    init_all();
    Position pos = start_position();
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/true);

    const int white_relative = evaluate(pos);
    const int expected = pos.side_to_move == Color::White ? white_relative : -white_relative;
    REQUIRE(score == expected);
    REQUIRE(nodes == 1); // just the top-level call -- nothing to search further
}

TEST_CASE("quiescence: SEE-pruning actually skips a losing capture rather than searching it",
          "[quiescence]") {
    init_all();
    // White queen d1 can capture a black pawn on d5 that's defended by a
    // black pawn on c6 -- a clearly losing capture (see the SEE tests,
    // see_tests.cpp, for the same position confirming SEE == -800).
    // This position also happens to have two quiet checking moves
    // available (Qh5+, Qe2+) -- irrelevant to what THIS test checks, but
    // real, so include_checks=false isolates pure capture-pruning
    // behavior specifically (a separate test below already covers
    // include_checks on its own). With SEE-pruning working, the losing
    // capture is skipped entirely: no recursive quiescence call happens
    // for it, so nodes stays at 1 (only the top-level call itself).
    Position pos = parse_fen("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1");
    std::uint64_t nodes = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/false);
    REQUIRE(nodes == 1);
}

TEST_CASE("quiescence: a genuinely good (non-pruned) capture is actually searched", "[quiescence]") {
    init_all();
    // Same shape as the pruning test above, but the pawn on d5 is
    // undefended this time -- a good capture (SEE > 0), so it must NOT
    // be pruned: at least one recursive call happens for it.
    // include_checks=false again isolates pure capture behavior (this
    // position also has quiet checks available, same as the pruning
    // test above) -- without it, nodes > 1 would hold even if Qxd5 were
    // wrongly pruned, since the checks alone would still push nodes up,
    // silently defeating what this test is actually meant to verify.
    Position pos = parse_fen("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1");
    std::uint64_t nodes = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/false);
    REQUIRE(nodes > 1);
}

TEST_CASE("quiescence: include_checks controls whether a non-capture checking move is explored",
          "[quiescence]") {
    init_all();
    // White rook a1 can play Ra8+ (check, not a capture) -- nothing else
    // tactical is available (no captures exist at all on this board).
    Position pos = parse_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");

    std::uint64_t nodes_with_checks = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes_with_checks, /*include_checks=*/true);
    REQUIRE(nodes_with_checks > 1); // Ra8+ (and its evasion replies) get explored

    std::uint64_t nodes_without_checks = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes_without_checks, /*include_checks=*/false);
    REQUIRE(nodes_without_checks == 1); // no captures exist, and checks are ignored -- immediate stand-pat
}

TEST_CASE("quiescence: when in check, every legal evasion is tried, not just captures", "[quiescence]") {
    init_all();
    // Black king e8 in check from a white rook on e1 (open e-file) --
    // Black's only legal moves are king steps off the e-file/rank (no
    // blocker or capture available). None of Black's evasions are
    // captures, so if quiescence only ever considered captures while in
    // check, it would find zero candidates and (incorrectly) report
    // this position as if it had no legal response -- it must instead
    // find a real evasion and a real (non-mate, non-draw) score.
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4RK2 b - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/true);
    REQUIRE(nodes > 1); // at least one evasion was actually explored
    REQUIRE(score > -kMateScore); // not reported as checkmate -- a real evasion exists
}

TEST_CASE("quiescence: detects checkmate with a correctly ply-adjusted score", "[quiescence]") {
    init_all();
    // Already-delivered back-rank mate: white rook a8, black king g8
    // boxed in by its own pawns, black to move with zero legal moves.
    Position pos = parse_fen("R5k1/5ppp/8/8/8/8/8/6K1 b - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, /*ply=*/3, nodes, true);
    REQUIRE(score == -(kMateScore - 3)); // mated at ply 3, from the mated side's perspective
}

TEST_CASE("quiescence: detects stalemate as a draw, not a loss", "[quiescence]") {
    init_all();
    // Black king h8, white queen g6 controls g7/g8/h7 without checking
    // h8 itself, white king a1 -- verified stalemate (0 legal moves, not
    // in check).
    Position pos = parse_fen("7k/8/6Q1/8/8/8/8/K7 b - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, true);
    REQUIRE(score == kDrawScore);
}

TEST_CASE("quiescence: leaves the position completely unmodified", "[quiescence]") {
    init_all();
    Position pos = parse_fen("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1");
    const std::uint64_t hash_before = pos.zobrist_hash;
    std::uint64_t nodes = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes, true);
    REQUIRE(pos.zobrist_hash == hash_before);
}
