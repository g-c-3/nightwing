// tests/see_tests.cpp
//
// Unit tests for src/search/see.h -- static_exchange_evaluation()
// exercised against small, hand-verified exchange positions. Each
// expected value is computed independently in this file's comments
// (not just re-derived from the implementation) so these tests actually
// catch a wrong swap-off calculation, not just a code-matches-itself
// tautology.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/move.h"
#include "board/zobrist.h"
#include "search/see.h"

using namespace nightwing::board;
using namespace nightwing::search;

namespace {
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("SEE: an undefended capture nets exactly the victim's value", "[see]") {
    init_all();
    // White pawn e4, black pawn d5, nothing else nearby to recapture.
    Position pos = parse_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    const Move exd5(make_square(4, 3), make_square(3, 4), MoveFlag::Capture);
    REQUIRE(static_exchange_evaluation(pos, exd5) == 100); // pawn value, eval/psqt.h
}

TEST_CASE("SEE: capturing a pawn defended by another pawn with a queen is a clear loss", "[see]") {
    init_all();
    // White queen d1 captures black pawn d5, recaptured by black pawn c6.
    Position pos = parse_fen("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1");
    const Move qxd5(make_square(3, 0), make_square(3, 4), MoveFlag::Capture);
    // Queen (900) wins a pawn (100) but is then recaptured: 100 - 900 = -800.
    REQUIRE(static_exchange_evaluation(pos, qxd5) == -800);
}

TEST_CASE("SEE: knight takes knight, pawn recaptures -- a fair (net-zero) trade", "[see]") {
    init_all();
    // White knight c3 captures black knight d5, recaptured by black pawn c6.
    Position pos = parse_fen("4k3/8/2p5/3n4/8/2N5/8/4K3 w - - 0 1");
    const Move nxd5(make_square(2, 2), make_square(3, 4), MoveFlag::Capture);
    // Knight (320) for knight (320), pawn recapture doesn't change the net: 0.
    REQUIRE(static_exchange_evaluation(pos, nxd5) == 0);
}

TEST_CASE("SEE: pawn takes pawn, pawn recaptures -- also a fair trade", "[see]") {
    init_all();
    Position pos = parse_fen("4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1");
    const Move exd5(make_square(4, 3), make_square(3, 4), MoveFlag::Capture);
    REQUIRE(static_exchange_evaluation(pos, exd5) == 0);
}

TEST_CASE("SEE: rook takes an otherwise-undefended pawn that's defended by a rook is a clear loss",
          "[see]") {
    init_all();
    // White rook d1 captures black pawn d5, recaptured by black rook d8 (same file, clear).
    Position pos = parse_fen("3rk3/8/8/3p4/8/8/8/3RK3 w - - 0 1");
    const Move rxd5(make_square(3, 0), make_square(3, 4), MoveFlag::Capture);
    // Rook (500) wins a pawn (100) but is recaptured: 100 - 500 = -400.
    REQUIRE(static_exchange_evaluation(pos, rxd5) == -400);
}

TEST_CASE("SEE: a defended piece is still a good capture when the attacker is cheap enough", "[see]") {
    init_all();
    // White pawn e4 captures black knight d5 (a pawn beats a knight
    // outright), recaptured by black pawn c6.
    Position pos = parse_fen("4k3/8/2p5/3n4/4P3/8/8/4K3 w - - 0 1");
    const Move exd5(make_square(4, 3), make_square(3, 4), MoveFlag::Capture);
    // Pawn (100) wins a knight (320): even after the pawn is recaptured,
    // White is still net +320 - 100 = +220 ahead versus not capturing.
    REQUIRE(static_exchange_evaluation(pos, exd5) == 220);
}

TEST_CASE("SEE: en passant treats the victim as a pawn and clears the correct occupancy square",
          "[see]") {
    init_all();
    // White pawn e5, black pawn d5 (just double-pushed from d7, en
    // passant target d6), nothing positioned to recapture on d6.
    Position pos = parse_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    const Move ep(make_square(4, 4), make_square(3, 5), MoveFlag::EnPassant);
    REQUIRE(static_exchange_evaluation(pos, ep) == 100); // pawn value, undefended
}

TEST_CASE("SEE: leaves the position completely unmodified", "[see]") {
    init_all();
    Position pos = parse_fen("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1");
    const std::uint64_t hash_before = pos.zobrist_hash;
    const Move qxd5(make_square(3, 0), make_square(3, 4), MoveFlag::Capture);
    (void)static_exchange_evaluation(pos, qxd5);
    REQUIRE(pos.zobrist_hash == hash_before);
    REQUIRE(pos.piece_at(make_square(3, 0)) != Piece::None); // queen still on d1
    REQUIRE(pos.piece_at(make_square(3, 4)) != Piece::None); // pawn still on d5
}
