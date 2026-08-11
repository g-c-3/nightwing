#pragma once
// src/board/attacks.h
//
// Sliding-piece (rook/bishop/queen) attack generation via magic bitboards —
// portable path (plain multiply-shift, no BMI2 PEXT). The BMI2 fast path is
// a separate, later roadmap item that will sit behind the same public
// query functions declared here, dispatched by support/cpu_features.h.
//
// Technique credit: magic bitboards are a well-established public technique
// (Pradyumna Kannan; documented on the Chess Programming Wiki, see
// https://www.chessprogramming.org/Magic_Bitboards and
// https://www.chessprogramming.org/Looking_for_Magics). This implementation
// — masks, attacks-on-the-fly, and the sparse-random magic search — is
// written from scratch against that public description; no magic-number
// tables or code were copied from any engine or article.

#include <cstdint>

#include "board/bitboard.h"

namespace nightwing::board {

/// Computes rook/bishop relevant-occupancy masks, searches for magic
/// numbers, and builds the rook/bishop attack lookup tables. Must be
/// called once during startup, before rook_attacks()/bishop_attacks()/
/// queen_attacks() are used. Safe to call more than once (idempotent).
/// Deterministic: uses a fixed PRNG seed, so magics (and therefore attack
/// table contents/layout) are identical across runs and platforms.
void init_magic_bitboards();

/// Returns the rook attack set from `sq` given `occupied` (all pieces on
/// the board, both colors — the caller masks out own-piece squares
/// separately when generating moves). Precondition: init_magic_bitboards()
/// has been called.
[[nodiscard]] Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept;

/// Returns the bishop attack set from `sq` given `occupied`. Precondition:
/// init_magic_bitboards() has been called.
[[nodiscard]] Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept;

/// Returns the queen attack set from `sq` given `occupied` (union of rook
/// and bishop attacks). Precondition: init_magic_bitboards() has been called.
[[nodiscard]] inline Bitboard queen_attacks(Square sq, Bitboard occupied) noexcept {
    return rook_attacks(sq, occupied) | bishop_attacks(sq, occupied);
}

} // namespace nightwing::board
