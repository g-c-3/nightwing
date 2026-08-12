// tests/perft_tests.cpp
//
// Perft node-count tests against the standard reference positions and
// depths from the Chess Programming Wiki's "Perft Results" page
// (https://www.chessprogramming.org/Perft_Results) — the conventional
// movegen/make-unmake correctness benchmark. Positions are the six
// well-known ones ("Kiwipete" through "Position 6"), each tested to a
// depth chosen to stay fast in CI (a few hundred ms at most in Release;
// see docs/SESSIONS.md for the deeper depths — startpos to depth 6,
// Kiwipete to depth 5 — that were checked by hand during development and
// can be promoted into this suite later if CI budget allows).
//
// Every mismatch this suite would have caught during development was
// cross-checked against an independent, deliberately naive movegen
// (pseudo-legal generation + make/unmake + king-attacked filter, sharing
// no code with movegen.cpp's pin/check-mask machinery) to bisect the
// exact divergent position — see docs/DECISIONS.md for the two bugs that
// process found and fixed before this suite was written.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/perft.h"
#include "board/zobrist.h"

using namespace nightwing::board;

namespace {
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("perft: startpos", "[perft]") {
    init_all();
    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    REQUIRE(perft(pos, 1) == 20);
    REQUIRE(perft(pos, 2) == 400);
    REQUIRE(perft(pos, 3) == 8902);
    REQUIRE(perft(pos, 4) == 197281);
    REQUIRE(perft(pos, 5) == 4865609);
}

TEST_CASE("perft: Kiwipete (position 2) - heavy on captures/checks/castling/promotions",
          "[perft]") {
    init_all();
    Position pos = parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    REQUIRE(perft(pos, 1) == 48);
    REQUIRE(perft(pos, 2) == 2039);
    REQUIRE(perft(pos, 3) == 97862);
    REQUIRE(perft(pos, 4) == 4085603);
}

TEST_CASE("perft: position 3 - endgame, heavy on en passant/pins", "[perft]") {
    init_all();
    Position pos = parse_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    REQUIRE(perft(pos, 1) == 14);
    REQUIRE(perft(pos, 2) == 191);
    REQUIRE(perft(pos, 3) == 2812);
    REQUIRE(perft(pos, 4) == 43238);
    REQUIRE(perft(pos, 5) == 674624);
}

TEST_CASE("perft: position 4 - asymmetric castling/promotion stress position", "[perft]") {
    init_all();
    Position pos = parse_fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    REQUIRE(perft(pos, 1) == 6);
    REQUIRE(perft(pos, 2) == 264);
    REQUIRE(perft(pos, 3) == 9467);
    REQUIRE(perft(pos, 4) == 422333);
}

TEST_CASE("perft: position 4 mirrored - same as position 4 with colors flipped", "[perft]") {
    // Same node counts as position 4 by mirror symmetry; kept as a
    // separate test since it exercises the same stress cases from
    // Black's perspective, which is a real (if unlikely) place for a
    // white/black-asymmetric bug to hide.
    init_all();
    Position pos = parse_fen("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1");
    REQUIRE(perft(pos, 1) == 6);
    REQUIRE(perft(pos, 2) == 264);
    REQUIRE(perft(pos, 3) == 9467);
    REQUIRE(perft(pos, 4) == 422333);
}

TEST_CASE("perft: position 5 - short, tactically sharp middlegame", "[perft]") {
    init_all();
    Position pos = parse_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
    REQUIRE(perft(pos, 1) == 44);
    REQUIRE(perft(pos, 2) == 1486);
    REQUIRE(perft(pos, 3) == 62379);
    REQUIRE(perft(pos, 4) == 2103487);
}

TEST_CASE("perft: position 6 - complex late-middlegame position", "[perft]") {
    init_all();
    Position pos = parse_fen("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
    REQUIRE(perft(pos, 1) == 46);
    REQUIRE(perft(pos, 2) == 2079);
    REQUIRE(perft(pos, 3) == 89890);
    REQUIRE(perft(pos, 4) == 3894594);
}
