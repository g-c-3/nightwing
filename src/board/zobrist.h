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
// search node, per ARCHITECTURE.md "Incremental Updates") is implemented
// in board.cpp's make_move()/unmake_move(), using the per-key accessors
// below. compute_hash() itself remains O(64) and is used to give a
// freshly-built Position (e.g. start_position()) its correct initial
// hash, and in tests to cross-check that the incremental update in
// make_move() stays correct against a full recompute.

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

/// Returns the Zobrist key for a single (piece, square) pair — the XOR
/// term make_move()/unmake_move() use to incrementally update
/// Position::zobrist_hash without a full recompute. Precondition:
/// init_zobrist_keys() called, p != Piece::None.
[[nodiscard]] std::uint64_t piece_square_key(Piece p, Square sq) noexcept;

/// Returns the Zobrist key XORed into the hash whenever it's Black to
/// move. Toggle this unconditionally on every side-to-move flip in
/// make_move()/unmake_move() — XOR is its own inverse, so applying the
/// same key again on unmake exactly removes what make_move added.
/// Precondition: init_zobrist_keys() called.
[[nodiscard]] std::uint64_t side_to_move_key() noexcept;

/// Returns the Zobrist key for a single castling-rights flag (exactly one
/// of the castling::k* constants — not a combined mask). Precondition:
/// init_zobrist_keys() called, `right` is one of the four castling::k*
/// values.
[[nodiscard]] std::uint64_t castling_right_key(std::uint8_t right) noexcept;

/// Returns the Zobrist key for en passant file `file` (0 = a-file .. 7 =
/// h-file). Precondition: init_zobrist_keys() called, 0 <= file < 8.
[[nodiscard]] std::uint64_t en_passant_file_key(int file) noexcept;

/// Computes a hash of PAWN PLACEMENT ONLY (both colors' pawns' squares --
/// no side-to-move, castling, en passant, or non-pawn pieces), for use
/// as the pawn hash table's key (eval/pawn_tt.h). O(number of pawns on
/// the board, <= 16) -- computed fresh on demand rather than maintained
/// incrementally in Position alongside the main zobrist_hash (see docs/
/// DECISIONS.md, 2026-08-21 pawn hash table entry, for why: Position is
/// already at its documented cache-line budget, board.h, and
/// incrementally updating a second hash through every make_move()/
/// unmake_move() code path -- captures, promotions, en passant -- is
/// real correctness risk this deliberately avoided without a compiler
/// available to verify it). Uses the identical piece_square_key() terms
/// compute_hash() does, just XORing only the pawn ones -- so a position
/// with the same pawn placement but a different side to move, castling
/// rights, or en passant square correctly produces the SAME pawn hash
/// (exactly the key-space collapsing the pawn hash table exists for).
/// Precondition: init_zobrist_keys() has been called.
[[nodiscard]] std::uint64_t compute_pawn_hash(const Position& pos) noexcept;

} // namespace nightwing::board

