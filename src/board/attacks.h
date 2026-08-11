#pragma once
// src/board/attacks.h
//
// Sliding-piece (rook/bishop/queen) attack generation via magic bitboards.
// Two indexing strategies are built at startup and dispatched between at
// runtime by rook_attacks()/bishop_attacks():
//   - Portable path: plain multiply-shift magic bitboards (always built).
//   - BMI2 fast path: PEXT-indexed tables (built only when compiled with
//     NIGHTWING_ENABLE_BMI2, and only ever *used* when the running CPU
//     actually supports BMI2, per support/cpu_features.h). PEXT needs no
//     magic-number search at all — the instruction itself guarantees a
//     collision-free mapping from (occupancy & mask) to a dense index.
//
// Technique credit: magic bitboards are a well-established public technique
// (Pradyumna Kannan; documented on the Chess Programming Wiki, see
// https://www.chessprogramming.org/Magic_Bitboards and
// https://www.chessprogramming.org/Looking_for_Magics). The PEXT indexing
// approach is likewise standard practice, documented at
// https://www.chessprogramming.org/BMI2#PEXTBitboards. This implementation
// — masks, attacks-on-the-fly, the sparse-random magic search, and the
// PEXT table builder — is written from scratch against those public
// descriptions; no magic-number tables or code were copied from any
// engine or article.

#include <cstdint>

#include "board/bitboard.h"

namespace nightwing::board {

/// Computes rook/bishop relevant-occupancy masks, searches for magic
/// numbers, and builds the rook/bishop attack lookup tables (both the
/// portable magic-indexed table and, when compiled with
/// NIGHTWING_ENABLE_BMI2, the PEXT-indexed table). Also runs CPU feature
/// detection so the runtime dispatch in rook_attacks()/bishop_attacks()
/// knows which table to use. Must be called once during startup, before
/// rook_attacks()/bishop_attacks()/queen_attacks() are used. Safe to call
/// more than once (idempotent). Deterministic: uses a fixed PRNG seed, so
/// magics (and therefore the magic-table contents/layout) are identical
/// across runs and platforms.
void init_magic_bitboards();

/// Returns the rook attack set from `sq` given `occupied` (all pieces on
/// the board, both colors — the caller masks out own-piece squares
/// separately when generating moves). Transparently uses the PEXT fast
/// path when available. Precondition: init_magic_bitboards() has been called.
[[nodiscard]] Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept;

/// Returns the bishop attack set from `sq` given `occupied`. Precondition:
/// init_magic_bitboards() has been called.
[[nodiscard]] Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept;

/// Returns the queen attack set from `sq` given `occupied` (union of rook
/// and bishop attacks). Precondition: init_magic_bitboards() has been called.
[[nodiscard]] inline Bitboard queen_attacks(Square sq, Bitboard occupied) noexcept {
    return rook_attacks(sq, occupied) | bishop_attacks(sq, occupied);
}

#if defined(NIGHTWING_ENABLE_BMI2)
// Test-only hooks: force the PEXT-indexed path regardless of the runtime
// dispatch decision, so tests can verify the PEXT tables directly rather
// than only whichever path the current host happens to select. These
// execute a real PEXT instruction, so callers MUST check
// support::cpu_has_bmi2() first — never call these from engine logic,
// only from tests running on hardware already confirmed to support BMI2.

/// Test-only: rook attacks via the PEXT table, bypassing dispatch.
[[nodiscard]] Bitboard rook_attacks_pext_for_testing(Square sq, Bitboard occupied) noexcept;

/// Test-only: bishop attacks via the PEXT table, bypassing dispatch.
[[nodiscard]] Bitboard bishop_attacks_pext_for_testing(Square sq, Bitboard occupied) noexcept;
#endif

} // namespace nightwing::board
