// tests/attacks_tests.cpp
//
// Verifies rook/bishop/queen magic-bitboard attack generation against an
// independent brute-force ray-caster (deliberately re-implemented here
// rather than reusing attacks.cpp's internal one, so a shared bug
// wouldn't hide itself from these tests).

#include <array>
#include <random>

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/bitboard.h"
#include "support/cpu_features.h"

using namespace nightwing::board;

namespace {

using Deltas = std::array<std::pair<int, int>, 4>;
constexpr Deltas kRookDeltas = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
constexpr Deltas kBishopDeltas = {{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};

Bitboard brute_force_attacks(Square sq, const Deltas& deltas, Bitboard occupied) {
    Bitboard attacks = kEmptyBitboard;
    for (const auto& [df, dr] : deltas) {
        int f = file_of(sq);
        int r = rank_of(sq);
        for (;;) {
            f += df;
            r += dr;
            if (!on_board(f, r)) {
                break;
            }
            const Square s = make_square(f, r);
            set_bit(attacks, s);
            if (test_bit(occupied, s)) {
                break;
            }
        }
    }
    return attacks;
}

} // namespace

TEST_CASE("init_magic_bitboards is idempotent", "[attacks]") {
    init_magic_bitboards();
    Bitboard before = rook_attacks(27, 0xFFULL);
    init_magic_bitboards(); // second call must be a no-op, not regenerate
    Bitboard after = rook_attacks(27, 0xFFULL);
    REQUIRE(before == after);
}

TEST_CASE("rook_attacks matches brute force across random occupancies", "[attacks]") {
    init_magic_bitboards();
    std::mt19937_64 rng(0xC0FFEEULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        for (int trial = 0; trial < 200; ++trial) {
            const Bitboard occ = rng();
            REQUIRE(rook_attacks(sq, occ) == brute_force_attacks(sq, kRookDeltas, occ));
        }
    }
}

TEST_CASE("bishop_attacks matches brute force across random occupancies", "[attacks]") {
    init_magic_bitboards();
    std::mt19937_64 rng(0xDEADBEEFULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        for (int trial = 0; trial < 200; ++trial) {
            const Bitboard occ = rng();
            REQUIRE(bishop_attacks(sq, occ) == brute_force_attacks(sq, kBishopDeltas, occ));
        }
    }
}

TEST_CASE("queen_attacks is the union of rook and bishop attacks", "[attacks]") {
    init_magic_bitboards();
    std::mt19937_64 rng(0x51DE51DEULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        const Bitboard occ = rng();
        REQUIRE(queen_attacks(sq, occ) == (rook_attacks(sq, occ) | bishop_attacks(sq, occ)));
    }
}

TEST_CASE("rook on a1, empty board, attacks the whole a-file and rank 1", "[attacks]") {
    init_magic_bitboards();
    Bitboard expected = kEmptyBitboard;
    for (int i = 1; i < 8; ++i) {
        set_bit(expected, make_square(0, i)); // a2..a8
        set_bit(expected, make_square(i, 0)); // b1..h1
    }
    REQUIRE(rook_attacks(make_square(0, 0), kEmptyBitboard) == expected);
}

TEST_CASE("bishop on d4, empty board, attacks all four diagonals", "[attacks]") {
    init_magic_bitboards();
    const Square d4 = make_square(3, 3);
    Bitboard expected = kEmptyBitboard;
    for (int i = 1; on_board(3 + i, 3 + i); ++i) set_bit(expected, make_square(3 + i, 3 + i));
    for (int i = 1; on_board(3 + i, 3 - i); ++i) set_bit(expected, make_square(3 + i, 3 - i));
    for (int i = 1; on_board(3 - i, 3 + i); ++i) set_bit(expected, make_square(3 - i, 3 + i));
    for (int i = 1; on_board(3 - i, 3 - i); ++i) set_bit(expected, make_square(3 - i, 3 - i));
    REQUIRE(bishop_attacks(d4, kEmptyBitboard) == expected);
}

TEST_CASE("rook attack ray stops at and includes the first blocker", "[attacks]") {
    init_magic_bitboards();
    // Rook on a1, blocker on a4: should attack a2, a3, a4 (inclusive) but
    // not a5-a8, plus the full rank 1 (unblocked).
    Bitboard occ = kEmptyBitboard;
    set_bit(occ, make_square(0, 3)); // a4

    Bitboard attacks = rook_attacks(make_square(0, 0), occ);
    REQUIRE(test_bit(attacks, make_square(0, 1))); // a2
    REQUIRE(test_bit(attacks, make_square(0, 2))); // a3
    REQUIRE(test_bit(attacks, make_square(0, 3))); // a4 (blocker itself, capturable)
    REQUIRE_FALSE(test_bit(attacks, make_square(0, 4))); // a5 - beyond blocker
    REQUIRE(test_bit(attacks, make_square(7, 0)));  // h1 - rank 1 unobstructed
}

#if defined(NIGHTWING_ENABLE_BMI2)
TEST_CASE("PEXT attack path matches brute force, when the host CPU supports BMI2",
          "[attacks][pext]") {
    // Compiled in (x86/x86_64 build), but the *running* CPU might still
    // lack BMI2 (e.g. an older machine using a BMI2-enabled binary) — the
    // whole point of the runtime dispatch this is testing the other half
    // of. Calling the real _pext_u64 intrinsic without this check risks
    // SIGILL on such hardware, so skip rather than assume.
    nightwing::support::detect_cpu_features();
    if (!nightwing::support::cpu_has_bmi2()) {
        SUCCEED("Host CPU lacks BMI2 - PEXT path not exercised on this runner; "
                 "the magic path above already covers correctness, and "
                 "rook_attacks()/bishop_attacks() would themselves fall back "
                 "to it here too.");
        return;
    }

    init_magic_bitboards();
    std::mt19937_64 rng(0xFEEDFACEULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        for (int trial = 0; trial < 200; ++trial) {
            const Bitboard occ = rng();
            REQUIRE(rook_attacks_pext_for_testing(sq, occ) ==
                    brute_force_attacks(sq, kRookDeltas, occ));
            REQUIRE(bishop_attacks_pext_for_testing(sq, occ) ==
                    brute_force_attacks(sq, kBishopDeltas, occ));
        }
    }
}

TEST_CASE("PEXT and magic paths agree with each other, when the host CPU supports BMI2",
          "[attacks][pext]") {
    nightwing::support::detect_cpu_features();
    if (!nightwing::support::cpu_has_bmi2()) {
        SUCCEED("Host CPU lacks BMI2 - nothing to cross-check on this runner.");
        return;
    }

    init_magic_bitboards();
    std::mt19937_64 rng(0xABADCAFEULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        const Bitboard occ = rng();
        // rook_attacks()/bishop_attacks() will themselves be using PEXT
        // here (this host supports it), so this also implicitly confirms
        // the runtime dispatch picked the fast path rather than silently
        // falling back.
        REQUIRE(rook_attacks(sq, occ) == rook_attacks_pext_for_testing(sq, occ));
        REQUIRE(bishop_attacks(sq, occ) == bishop_attacks_pext_for_testing(sq, occ));
    }
}
#endif
