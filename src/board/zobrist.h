#pragma once
// src/board/zobrist.h
//
// Zobrist hashing: random keys for piece-square pairs, side to move,
// castling rights, and en passant file, used to compute a Position's
// hash. Technique per Chess Programming Wiki
// (https://www.chessprogramming.org/Zobrist_Hashing); keys here are
// generated from scratch by a deterministic PRNG (support/rng.h) with a
// fixed seed — no borrowed key tables.
//
// Incremental XOR-update on make/unmake (never a full recompute per
// search node, per ARCHITECTURE.md "Incremental Updates") is a later
// roadmap item, once make/unmake move exists. What's here — the key
// tables and compute_hash() — is what that update logic will XOR against
// per move; for now it's used to give a freshly-built Position (e.g.
// start_position()) a correct initial hash, and in tests to verify future
// incremental updates stay correct by cross-checking against a full
// recompute.

#include <cstdint>

#include "board/board.h"

namespace nightwing::board {

/// Generates the Zobrist key tables. Must be called once during startup,
/// before compute_hash() is used. Safe to call more than once (idempotent).
/// Deterministic: fixed PRNG seed, so keys — and therefore every hash
/// value derived from them — are identical across runs and platforms.
void init_zobrist_keys();

/// Computes `pos`'s Zobrist hash from scratch: XORs together the keys for
/// every occupied square's (piece, square) pair, the side-to-move key (if
/// Black to move), each currently-held castling right, and the en
/// passant file (if set). O(64) — intended for initializing a freshly
/// built Position or verifying an incrementally-maintained hash in tests,
/// not for repeated use in the search hot path once make/unmake exists.
/// Precondition: init_zobrist_keys() has been called.
[[nodiscard]] std::uint64_t compute_hash(const Position& pos);

} // namespace nightwing::board
