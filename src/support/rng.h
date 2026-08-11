#pragma once
// src/support/rng.h
//
// A small, fast, deterministic PRNG (xorshift64*), shared by anything at
// startup that needs reproducible pseudo-random values — currently the
// magic-bitboard search (board/attacks.cpp) and Zobrist key generation
// (board/zobrist.cpp). Determinism (caller-supplied fixed seed) is the
// whole point: identical output across every run and platform, so
// generated tables are reproducible and testable.
//
// Not used anywhere in the search hot path — this is purely a startup/
// init-time utility.

#include <cstdint>

namespace nightwing::support {

/// xorshift64* pseudo-random number generator. Not cryptographically
/// secure and not intended to be — only used to generate deterministic
/// startup tables (magic numbers, Zobrist keys).
class Xorshift64Star {
public:
    /// Constructs with the given seed. A zero seed is remapped to 1 (an
    /// all-zero state is a fixed point for xorshift and would generate
    /// nothing but zeros).
    explicit Xorshift64Star(std::uint64_t seed) : state_(seed != 0 ? seed : 1) {}

    /// Returns the next pseudo-random 64-bit value and advances the state.
    std::uint64_t next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

private:
    std::uint64_t state_;
};

} // namespace nightwing::support
