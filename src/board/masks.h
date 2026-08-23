#pragma once
// src/board/masks.h
//
// Precomputed non-sliding-piece attack tables (knight, king, pawn) and
// basic file/rank utility masks. The knight/king/pawn tables are
// populated once via init_masks() — the first step of the mandatory
// startup sequence documented in ARCHITECTURE.md ("Startup Sequence"):
// init_masks() -> init_magic_bitboards() -> init_zobrist_keys().
//
// These attack sets could equally be computed at compile time (a leaper's
// attack set never depends on board occupancy, unlike a slider's) —
// they're generated at runtime here instead, for consistency with the
// other two startup steps, which do have a genuine runtime dependency
// (init_magic_bitboards()'s magic search, init_zobrist_keys()'s
// PRNG-generated keys). One uniform "populate once at startup, then
// query" pattern across all board-setup data is simpler than mixing
// compile-time and runtime approaches depending on which table happens
// to allow it.

#include "board/bitboard.h"
#include "board/board.h" // for Color

namespace nightwing::board {

/// Populates the knight/king/pawn attack tables. Must be called once
/// during startup, before knight_attacks()/king_attacks()/pawn_attacks()
/// are used — and per the mandatory startup order, before
/// init_magic_bitboards() and init_zobrist_keys(). Safe to call more than
/// once (idempotent).
void init_masks();

/// Returns the knight attack set from `sq` (every square a knight there
/// could move to on an otherwise-empty board — knights aren't blocked by
/// intervening pieces, so this needs no occupancy argument).
/// Precondition: init_masks() has been called.
[[nodiscard]] Bitboard knight_attacks(Square sq) noexcept;

/// Returns the king attack set from `sq` (the 8 surrounding squares,
/// clipped to the board edge). Precondition: init_masks() has been called.
[[nodiscard]] Bitboard king_attacks(Square sq) noexcept;

/// Returns the pawn *capture* attack set for a pawn of color `c` on `sq`
/// (the one or two diagonal squares it could capture on — not its
/// forward push, which isn't a capture and is handled separately by move
/// generation). Precondition: init_masks() has been called.
[[nodiscard]] Bitboard pawn_attacks(Color c, Square sq) noexcept;

/// Returns every square on `file` (0=a..7=h).
[[nodiscard]] constexpr Bitboard file_mask(int file) noexcept {
    Bitboard bb = kEmptyBitboard;
    for (int rank = 0; rank < kNumRanks; ++rank) {
        set_bit(bb, make_square(file, rank));
    }
    return bb;
}

/// Returns every square on `rank` (0=rank1..7=rank8).
[[nodiscard]] constexpr Bitboard rank_mask(int rank) noexcept {
    Bitboard bb = kEmptyBitboard;
    for (int file = 0; file < kNumFiles; ++file) {
        set_bit(bb, make_square(file, rank));
    }
    return bb;
}

/// Returns every square on the file(s) adjacent to `file` (file-1 and
/// file+1; only one if `file` is the a- or h-file). Used for
/// isolated/backward pawn detection (eval/pawns.cpp: CPW "Isolated
/// Pawn", "Backward Pawn").
[[nodiscard]] constexpr Bitboard adjacent_files_mask(int file) noexcept {
    Bitboard bb = kEmptyBitboard;
    if (file - 1 >= 0) {
        bb |= file_mask(file - 1);
    }
    if (file + 1 < kNumFiles) {
        bb |= file_mask(file + 1);
    }
    return bb;
}

/// Returns the "passed pawn span" for a pawn of color `c` on `sq`: every
/// square on `sq`'s own file and both adjacent files, strictly ahead of
/// `sq` from `c`'s perspective (toward promotion). A pawn on `sq` is
/// passed exactly when this span contains no enemy pawns -- no enemy
/// pawn can ever block or capture it on the way to promotion (eval/
/// pawns.cpp: CPW "Passed Pawn"). Precondition: init_masks() has been
/// called.
[[nodiscard]] Bitboard passed_pawn_mask(Color c, Square sq) noexcept;

/// Returns the "backward pawn support span" for a pawn of color `c` on
/// `sq`: every square on the two adjacent files ONLY (not `sq`'s own
/// file), at `sq`'s rank or further back toward `c`'s own side. A
/// friendly pawn anywhere in this span could, by advancing, eventually
/// stand beside or defend a pawn on `sq` -- its absence is one half of
/// CPW's "Backward Pawn" test (the other half, whether `sq`'s push
/// square is attacked by an enemy pawn, doesn't need a dedicated mask --
/// see eval/pawns.cpp for how it reuses pawn_attacks() directly, the
/// standard reverse-pawn-attack trick). Precondition: init_masks() has
/// been called.
[[nodiscard]] Bitboard backward_support_mask(Color c, Square sq) noexcept;

} // namespace nightwing::board
