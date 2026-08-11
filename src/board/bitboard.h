#pragma once
// src/board/bitboard.h
//
// Core bitboard type and primitive operations (set/clear/test bit, popcount,
// bitscan). All operations are implemented via compiler intrinsics, per
// project performance-engineering standards (ARCHITECTURE.md) — no manual
// bit-twiddling loops in the hot path. Square indices are 0..63, little-
// endian rank-file (LERF) mapping: square = rank * 8 + file, a1 = 0, h8 = 63.

#include <bit>
#include <cassert>
#include <cstdint>
#include <string>

namespace nightwing::board {

/// A chess board as a 64-bit set of squares, one bit per square (LERF
/// mapping: bit 0 = a1, bit 7 = h1, bit 56 = a8, bit 63 = h8).
using Bitboard = std::uint64_t;

/// Square index, 0..63 (LERF mapping). Kept as a plain enum-free integer
/// type for now; a strong `Square` enum can be layered on in a later pass
/// once movegen shapes its usage patterns.
using Square = int;

inline constexpr Bitboard kEmptyBitboard = 0ULL;
inline constexpr Bitboard kFullBitboard = ~0ULL;
inline constexpr int kNumSquares = 64;
inline constexpr int kNumFiles = 8;
inline constexpr int kNumRanks = 8;

/// Returns the file (0=a .. 7=h) of `sq`.
[[nodiscard]] constexpr int file_of(Square sq) noexcept {
    assert(sq >= 0 && sq < kNumSquares);
    return sq & 7;
}

/// Returns the rank (0=rank1 .. 7=rank8) of `sq`.
[[nodiscard]] constexpr int rank_of(Square sq) noexcept {
    assert(sq >= 0 && sq < kNumSquares);
    return sq >> 3;
}

/// Returns the square index for a given file (0..7) and rank (0..7).
[[nodiscard]] constexpr Square make_square(int file, int rank) noexcept {
    assert(file >= 0 && file < kNumFiles);
    assert(rank >= 0 && rank < kNumRanks);
    return rank * 8 + file;
}

/// Returns true if (file, rank) is within the board — useful when walking
/// rays outward from a square, since off-board coordinates go negative or
/// exceed 7 well before wrapping in the packed `Square` representation.
[[nodiscard]] constexpr bool on_board(int file, int rank) noexcept {
    return file >= 0 && file < kNumFiles && rank >= 0 && rank < kNumRanks;
}

/// Returns the bitboard with only `sq` set.
[[nodiscard]] constexpr Bitboard square_bb(Square sq) noexcept {
    assert(sq >= 0 && sq < kNumSquares);
    return Bitboard{1} << sq;
}

/// Returns true if `sq` is set in `bb`.
[[nodiscard]] constexpr bool test_bit(Bitboard bb, Square sq) noexcept {
    assert(sq >= 0 && sq < kNumSquares);
    return (bb & square_bb(sq)) != 0;
}

/// Sets `sq` in `bb` (in place).
constexpr void set_bit(Bitboard& bb, Square sq) noexcept {
    assert(sq >= 0 && sq < kNumSquares);
    bb |= square_bb(sq);
}

/// Clears `sq` in `bb` (in place). Safe to call on an already-clear square.
constexpr void clear_bit(Bitboard& bb, Square sq) noexcept {
    assert(sq >= 0 && sq < kNumSquares);
    bb &= ~square_bb(sq);
}

/// Toggles `sq` in `bb` (in place).
constexpr void toggle_bit(Bitboard& bb, Square sq) noexcept {
    assert(sq >= 0 && sq < kNumSquares);
    bb ^= square_bb(sq);
}

/// Returns the number of set bits in `bb`. Backed by the POPCNT instruction
/// where available (std::popcount lowers to it on supporting targets; on
/// targets without hardware POPCNT it falls back to a portable software
/// count, so this is always correct, just not always maximally fast — see
/// support/cpu_features.h for the runtime capability check used by hot
/// paths that need to choose a strategy rather than just get a correct count).
[[nodiscard]] constexpr int popcount(Bitboard bb) noexcept {
    return std::popcount(bb);
}

/// Returns the index (0..63) of the least significant set bit of `bb`.
/// Precondition: `bb != 0` (undefined for an empty bitboard — callers must
/// check emptiness first, which they almost always already do as a loop
/// condition; this mirrors the underlying hardware instruction's contract).
[[nodiscard]] constexpr int bitscan_forward(Bitboard bb) noexcept {
    assert(bb != 0 && "bitscan_forward: bitboard must be non-empty");
    return std::countr_zero(bb);
}

/// Returns the index (0..63) of the most significant set bit of `bb`.
/// Precondition: `bb != 0`.
[[nodiscard]] constexpr int bitscan_reverse(Bitboard bb) noexcept {
    assert(bb != 0 && "bitscan_reverse: bitboard must be non-empty");
    return 63 - std::countl_zero(bb);
}

/// Returns the index of the least significant set bit and clears it in
/// `bb` (in place). The standard "pop LSB" idiom used to iterate a
/// bitboard's set squares one at a time without a separate mask variable.
/// Precondition: `bb != 0`.
constexpr int pop_lsb(Bitboard& bb) noexcept {
    assert(bb != 0 && "pop_lsb: bitboard must be non-empty");
    const int sq = bitscan_forward(bb);
    bb &= bb - 1; // clears the least significant set bit
    return sq;
}

/// Returns an 8x8 ASCII-art rendering of `bb` (rank 8 at top, per
/// convention), '1' for set squares and '.' for empty ones. Debug/test
/// tool only — not used in the hot path.
[[nodiscard]] std::string to_string(Bitboard bb);

} // namespace nightwing::board
