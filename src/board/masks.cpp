// src/board/masks.cpp
//
// See masks.h. Straightforward per-square delta walks, same style as the
// mask/attacks-on-the-fly helpers in attacks.cpp — clarity over
// micro-optimization, since this only ever runs once at startup.

#include "board/masks.h"

#include <array>
#include <utility>

namespace nightwing::board {

namespace {

using Deltas8 = std::array<std::pair<int, int>, 8>;

constexpr Deltas8 kKnightDeltas = {{
    {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
}};

constexpr Deltas8 kKingDeltas = {{
    {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
}};

std::array<Bitboard, kNumSquares> g_knight_attacks{};
std::array<Bitboard, kNumSquares> g_king_attacks{};
std::array<std::array<Bitboard, kNumSquares>, kNumColors> g_pawn_attacks{};
std::array<std::array<Bitboard, kNumSquares>, kNumColors> g_passed_pawn_mask{};
std::array<std::array<Bitboard, kNumSquares>, kNumColors> g_backward_support_mask{};
bool g_initialized = false;

} // namespace

void init_masks() {
    if (g_initialized) {
        return;
    }

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        const int file = file_of(sq);
        const int rank = rank_of(sq);

        Bitboard knight_bb = kEmptyBitboard;
        for (const auto& [df, dr] : kKnightDeltas) {
            const int f = file + df;
            const int r = rank + dr;
            if (on_board(f, r)) {
                set_bit(knight_bb, make_square(f, r));
            }
        }
        g_knight_attacks[sq] = knight_bb;

        Bitboard king_bb = kEmptyBitboard;
        for (const auto& [df, dr] : kKingDeltas) {
            const int f = file + df;
            const int r = rank + dr;
            if (on_board(f, r)) {
                set_bit(king_bb, make_square(f, r));
            }
        }
        g_king_attacks[sq] = king_bb;

        // White captures toward higher ranks, Black toward lower ranks.
        // Off-board results (e.g. a "white pawn" on rank 8) simply produce
        // an empty attack set rather than needing special-casing — no
        // real position ever has a pawn there (it would have promoted),
        // but the table stays well-defined regardless.
        Bitboard white_pawn_bb = kEmptyBitboard;
        for (int df : {-1, 1}) {
            const int f = file + df;
            const int r = rank + 1;
            if (on_board(f, r)) {
                set_bit(white_pawn_bb, make_square(f, r));
            }
        }
        g_pawn_attacks[static_cast<std::size_t>(Color::White)][sq] = white_pawn_bb;

        Bitboard black_pawn_bb = kEmptyBitboard;
        for (int df : {-1, 1}) {
            const int f = file + df;
            const int r = rank - 1;
            if (on_board(f, r)) {
                set_bit(black_pawn_bb, make_square(f, r));
            }
        }
        g_pawn_attacks[static_cast<std::size_t>(Color::Black)][sq] = black_pawn_bb;

        // Passed-pawn span and backward-support span (masks.h's doc
        // comments on passed_pawn_mask()/backward_support_mask()): own
        // file + adjacent files strictly ahead, and adjacent files only
        // at-or-behind, respectively, from each color's own advancing
        // direction (White toward higher ranks, Black toward lower).
        Bitboard white_passed_bb = kEmptyBitboard;
        Bitboard black_passed_bb = kEmptyBitboard;
        Bitboard white_backward_bb = kEmptyBitboard;
        Bitboard black_backward_bb = kEmptyBitboard;
        for (int f = file - 1; f <= file + 1; ++f) {
            if (f < 0 || f >= kNumFiles) {
                continue;
            }
            for (int r = 0; r < kNumRanks; ++r) {
                if (r > rank) {
                    set_bit(white_passed_bb, make_square(f, r));
                }
                if (r < rank) {
                    set_bit(black_passed_bb, make_square(f, r));
                }
                if (f != file) {
                    if (r <= rank) {
                        set_bit(white_backward_bb, make_square(f, r));
                    }
                    if (r >= rank) {
                        set_bit(black_backward_bb, make_square(f, r));
                    }
                }
            }
        }
        g_passed_pawn_mask[static_cast<std::size_t>(Color::White)][sq] = white_passed_bb;
        g_passed_pawn_mask[static_cast<std::size_t>(Color::Black)][sq] = black_passed_bb;
        g_backward_support_mask[static_cast<std::size_t>(Color::White)][sq] = white_backward_bb;
        g_backward_support_mask[static_cast<std::size_t>(Color::Black)][sq] = black_backward_bb;
    }

    g_initialized = true;
}

Bitboard knight_attacks(Square sq) noexcept {
    return g_knight_attacks[static_cast<std::size_t>(sq)];
}

Bitboard king_attacks(Square sq) noexcept {
    return g_king_attacks[static_cast<std::size_t>(sq)];
}

Bitboard pawn_attacks(Color c, Square sq) noexcept {
    return g_pawn_attacks[static_cast<std::size_t>(c)][static_cast<std::size_t>(sq)];
}

Bitboard passed_pawn_mask(Color c, Square sq) noexcept {
    return g_passed_pawn_mask[static_cast<std::size_t>(c)][static_cast<std::size_t>(sq)];
}

Bitboard backward_support_mask(Color c, Square sq) noexcept {
    return g_backward_support_mask[static_cast<std::size_t>(c)][static_cast<std::size_t>(sq)];
}

} // namespace nightwing::board
