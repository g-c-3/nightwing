// tests/bitboard_tests.cpp
//
// Unit tests for src/board/bitboard.h primitives. Covers set/clear/test,
// popcount, bitscan (forward/reverse), pop_lsb, and the debug printer.
// Perft-style movegen correctness tests land separately once movegen
// exists (see docs/ARCHITECTURE.md Testing Policy).

#include <catch2/catch_test_macros.hpp>

#include "board/bitboard.h"

using nightwing::board::Bitboard;
using namespace nightwing::board;

TEST_CASE("square_bb sets exactly one bit", "[bitboard]") {
    REQUIRE(square_bb(0) == 0x1ULL);          // a1
    REQUIRE(square_bb(7) == 0x80ULL);         // h1
    REQUIRE(square_bb(56) == 0x0100000000000000ULL); // a8
    REQUIRE(square_bb(63) == 0x8000000000000000ULL); // h8
}

TEST_CASE("set_bit / clear_bit / test_bit round-trip", "[bitboard]") {
    Bitboard bb = kEmptyBitboard;
    REQUIRE_FALSE(test_bit(bb, 27));

    set_bit(bb, 27);
    REQUIRE(test_bit(bb, 27));
    REQUIRE(bb == square_bb(27));

    // Setting an already-set bit is idempotent.
    set_bit(bb, 27);
    REQUIRE(bb == square_bb(27));

    clear_bit(bb, 27);
    REQUIRE_FALSE(test_bit(bb, 27));
    REQUIRE(bb == kEmptyBitboard);

    // Clearing an already-clear bit is a safe no-op.
    clear_bit(bb, 27);
    REQUIRE(bb == kEmptyBitboard);
}

TEST_CASE("toggle_bit flips bit state", "[bitboard]") {
    Bitboard bb = kEmptyBitboard;
    toggle_bit(bb, 10);
    REQUIRE(test_bit(bb, 10));
    toggle_bit(bb, 10);
    REQUIRE_FALSE(test_bit(bb, 10));
}

TEST_CASE("popcount counts set bits", "[bitboard]") {
    REQUIRE(popcount(kEmptyBitboard) == 0);
    REQUIRE(popcount(kFullBitboard) == 64);
    REQUIRE(popcount(square_bb(5)) == 1);

    Bitboard bb = kEmptyBitboard;
    set_bit(bb, 0);
    set_bit(bb, 15);
    set_bit(bb, 63);
    REQUIRE(popcount(bb) == 3);
}

TEST_CASE("bitscan_forward finds the least significant set bit", "[bitboard]") {
    REQUIRE(bitscan_forward(square_bb(0)) == 0);
    REQUIRE(bitscan_forward(square_bb(63)) == 63);
    REQUIRE(bitscan_forward(square_bb(12) | square_bb(40)) == 12);
}

TEST_CASE("bitscan_reverse finds the most significant set bit", "[bitboard]") {
    REQUIRE(bitscan_reverse(square_bb(0)) == 0);
    REQUIRE(bitscan_reverse(square_bb(63)) == 63);
    REQUIRE(bitscan_reverse(square_bb(12) | square_bb(40)) == 40);
}

TEST_CASE("pop_lsb clears and returns the least significant set bit", "[bitboard]") {
    Bitboard bb = square_bb(3) | square_bb(20) | square_bb(60);

    int first = pop_lsb(bb);
    REQUIRE(first == 3);
    REQUIRE_FALSE(test_bit(bb, 3));
    REQUIRE(popcount(bb) == 2);

    int second = pop_lsb(bb);
    REQUIRE(second == 20);

    int third = pop_lsb(bb);
    REQUIRE(third == 60);
    REQUIRE(bb == kEmptyBitboard);
}

TEST_CASE("pop_lsb fully drains a bitboard in ascending square order", "[bitboard]") {
    Bitboard bb = kFullBitboard;
    int count = 0;
    int last = -1;
    while (bb) {
        int sq = pop_lsb(bb);
        REQUIRE(sq > last);
        last = sq;
        ++count;
    }
    REQUIRE(count == 64);
}

TEST_CASE("to_string renders an 8x8 board with rank 8 on top", "[bitboard]") {
    Bitboard bb = kEmptyBitboard;
    set_bit(bb, 0);  // a1 -> bottom-left of the printed board
    set_bit(bb, 63); // h8 -> top-right of the printed board

    std::string s = to_string(bb);

    // 8 ranks * (8 chars + newline) = 72 characters.
    REQUIRE(s.size() == 72);

    // Each row is 9 chars (8 squares + '\n'). Row 0 is rank 8 (top),
    // row 7 is rank 1 (bottom); within a row, file a is first, file h is last.
    std::string top_row = s.substr(0, 8);      // rank 8: h8 set -> last char '1'
    std::string bottom_row = s.substr(63, 8);  // rank 1: a1 set -> first char '1'

    REQUIRE(top_row == ".......1");
    REQUIRE(bottom_row == "1.......");
}
