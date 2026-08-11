// tests/masks_tests.cpp
//
// Unit tests for src/board/masks.h — knight/king/pawn attack table
// correctness (cross-checked against independent brute-force deltas,
// same pattern as attacks_tests.cpp), edge/corner clipping, and
// file_mask()/rank_mask().

#include <array>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"

using namespace nightwing::board;

namespace {

Bitboard brute_force_leaper(Square sq, const std::array<std::pair<int, int>, 8>& deltas) {
    Bitboard bb = kEmptyBitboard;
    for (const auto& [df, dr] : deltas) {
        const int f = file_of(sq) + df;
        const int r = rank_of(sq) + dr;
        if (on_board(f, r)) {
            set_bit(bb, make_square(f, r));
        }
    }
    return bb;
}

constexpr std::array<std::pair<int, int>, 8> kKnightDeltas = {{
    {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
}};

constexpr std::array<std::pair<int, int>, 8> kKingDeltas = {{
    {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
}};

} // namespace

TEST_CASE("init_masks is idempotent", "[masks]") {
    init_masks();
    Bitboard before = knight_attacks(27);
    init_masks();
    Bitboard after = knight_attacks(27);
    REQUIRE(before == after);
}

TEST_CASE("knight_attacks matches brute force on every square", "[masks]") {
    init_masks();
    for (Square sq = 0; sq < kNumSquares; ++sq) {
        REQUIRE(knight_attacks(sq) == brute_force_leaper(sq, kKnightDeltas));
    }
}

TEST_CASE("king_attacks matches brute force on every square", "[masks]") {
    init_masks();
    for (Square sq = 0; sq < kNumSquares; ++sq) {
        REQUIRE(king_attacks(sq) == brute_force_leaper(sq, kKingDeltas));
    }
}

TEST_CASE("knight on b1 (start position square) attacks exactly 3 squares", "[masks]") {
    init_masks();
    REQUIRE(popcount(knight_attacks(make_square(1, 0))) == 3); // a3, c3, d2
}

TEST_CASE("knight in the center attacks 8 squares", "[masks]") {
    init_masks();
    REQUIRE(popcount(knight_attacks(make_square(4, 4))) == 8); // e5
}

TEST_CASE("king in a corner attacks exactly 3 squares", "[masks]") {
    init_masks();
    REQUIRE(popcount(king_attacks(make_square(0, 0))) == 3); // a1
    REQUIRE(popcount(king_attacks(make_square(7, 7))) == 3); // h8
}

TEST_CASE("king not on an edge attacks exactly 8 squares", "[masks]") {
    init_masks();
    REQUIRE(popcount(king_attacks(make_square(4, 4))) == 8); // e5
}

TEST_CASE("white pawn attacks diagonally forward (toward higher ranks)", "[masks]") {
    init_masks();
    Bitboard attacks = pawn_attacks(Color::White, make_square(4, 1)); // e2
    Bitboard expected = kEmptyBitboard;
    set_bit(expected, make_square(3, 2)); // d3
    set_bit(expected, make_square(5, 2)); // f3
    REQUIRE(attacks == expected);
}

TEST_CASE("black pawn attacks diagonally forward (toward lower ranks)", "[masks]") {
    init_masks();
    Bitboard attacks = pawn_attacks(Color::Black, make_square(4, 6)); // e7
    Bitboard expected = kEmptyBitboard;
    set_bit(expected, make_square(3, 5)); // d6
    set_bit(expected, make_square(5, 5)); // f6
    REQUIRE(attacks == expected);
}

TEST_CASE("pawn on the a-file or h-file has only one attack square", "[masks]") {
    init_masks();
    REQUIRE(popcount(pawn_attacks(Color::White, make_square(0, 1))) == 1); // a2
    REQUIRE(popcount(pawn_attacks(Color::White, make_square(7, 1))) == 1); // h2
}

TEST_CASE("file_mask covers exactly one file, 8 squares", "[masks]") {
    Bitboard a_file = file_mask(0);
    REQUIRE(popcount(a_file) == 8);
    for (int rank = 0; rank < kNumRanks; ++rank) {
        REQUIRE(test_bit(a_file, make_square(0, rank)));
    }
    REQUIRE_FALSE(test_bit(a_file, make_square(1, 0)));
}

TEST_CASE("rank_mask covers exactly one rank, 8 squares", "[masks]") {
    Bitboard rank1 = rank_mask(0);
    REQUIRE(popcount(rank1) == 8);
    for (int file = 0; file < kNumFiles; ++file) {
        REQUIRE(test_bit(rank1, make_square(file, 0)));
    }
    REQUIRE_FALSE(test_bit(rank1, make_square(0, 1)));
}

TEST_CASE("all 8 file masks together cover the whole board exactly once", "[masks]") {
    Bitboard total = kEmptyBitboard;
    for (int file = 0; file < kNumFiles; ++file) {
        Bitboard fm = file_mask(file);
        REQUIRE((total & fm) == kEmptyBitboard); // no overlap yet
        total |= fm;
    }
    REQUIRE(total == kFullBitboard);
}
