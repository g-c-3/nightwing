// src/board/attacks.cpp
//
// See attacks.h. Implements:
//   1. Relevant-occupancy mask computation for rook/bishop (ray-cast,
//      excluding the far edge square in each direction).
//   2. Brute-force "attacks on the fly" ray casting, used only at init
//      time to build reference attack sets for every occupancy subset.
//   3. A from-scratch magic-number search (sparse random candidates,
//      verified by full occupancy-subset enumeration via the standard
//      carry-rippler trick) — technique per Chess Programming Wiki,
//      "Looking for Magics" (see attacks.h header comment for links);
//      no magic-number tables or search code copied from any source.
//
// All of this runs once at startup; none of it is hot-path code, so
// clarity is favored over micro-optimization throughout this file.

#include "board/attacks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace nightwing::board {

namespace {

/// (file delta, rank delta) pairs for a piece's movement directions.
using Deltas = std::array<std::pair<int, int>, 4>;

constexpr Deltas kRookDeltas = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
constexpr Deltas kBishopDeltas = {{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};

/// Small, fast, deterministic PRNG (xorshift64*) used only for the magic
/// search below. Determinism (fixed seed) is intentional: magics — and
/// therefore attack table contents — must be identical across runs and
/// platforms so behavior is reproducible and testable.
class Xorshift64Star {
public:
    explicit Xorshift64Star(std::uint64_t seed) : state_(seed != 0 ? seed : 1) {}

    std::uint64_t next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

private:
    std::uint64_t state_;
};

/// Returns a sparse random 64-bit candidate (AND of three random draws —
/// magics with fewer set bits are more likely to work and are found
/// faster this way; standard trick for this kind of search).
std::uint64_t sparse_random_u64(Xorshift64Star& rng) {
    return rng.next() & rng.next() & rng.next();
}

/// Returns the relevant-occupancy mask for `sq` along `deltas`: every
/// square a blocker could occupy that actually changes the attack set,
/// which excludes the far edge square in each ray direction (a piece
/// sliding into the edge square stops there regardless of what, if
/// anything, lies beyond the board).
Bitboard relevant_occupancy_mask(Square sq, const Deltas& deltas) {
    Bitboard mask = kEmptyBitboard;
    for (const auto& [df, dr] : deltas) {
        int f = file_of(sq);
        int r = rank_of(sq);
        for (;;) {
            const int nf = f + df;
            const int nr = r + dr;
            if (!on_board(nf, nr)) {
                break;
            }
            const int nnf = nf + df;
            const int nnr = nr + dr;
            if (!on_board(nnf, nnr)) {
                break; // (nf, nr) is the edge square in this direction; exclude it.
            }
            set_bit(mask, make_square(nf, nr));
            f = nf;
            r = nr;
        }
    }
    return mask;
}

/// Brute-force ray-cast attack set from `sq` along `deltas` given
/// `occupied`, stopping (inclusively) at the first blocker in each
/// direction. Used only at init time to build reference attack sets —
/// this is the "slow but obviously correct" version that the magic
/// lookup is verified against.
Bitboard attacks_on_the_fly(Square sq, const Deltas& deltas, Bitboard occupied) {
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
                break; // Blocker: ray stops here, but this square is still attacked.
            }
        }
    }
    return attacks;
}

/// Result of a magic search for one square: the magic multiplier and its
/// fully-populated attack lookup table (indexed by `(occ * magic) >>
/// (64 - bits)`).
struct SquareMagic {
    std::uint64_t magic = 0;
    std::vector<Bitboard> table;
};

/// Searches for a working magic number for `sq`/`mask` and builds its
/// attack table. Enumerates every occupancy subset of `mask` (via the
/// carry-rippler trick) as both the index input and the source of the
/// ground-truth attack set, then tries sparse random magic candidates
/// until one produces no *harmful* collision (two different occupancies
/// mapping to the same index are fine as long as they'd produce the same
/// attack set — which happens naturally for many subsets, and is why the
/// table can be far smaller than 2^64).
SquareMagic find_magic_for_square(Square sq, Bitboard mask, const Deltas& deltas,
                                   Xorshift64Star& rng) {
    const int bits = popcount(mask);
    const int size = 1 << bits;

    std::vector<Bitboard> occupancies(static_cast<std::size_t>(size));
    std::vector<Bitboard> reference(static_cast<std::size_t>(size));

    Bitboard subset = kEmptyBitboard;
    int count = 0;
    do {
        occupancies[static_cast<std::size_t>(count)] = subset;
        reference[static_cast<std::size_t>(count)] = attacks_on_the_fly(sq, deltas, subset);
        ++count;
        subset = (subset - mask) & mask;
    } while (subset != kEmptyBitboard);
    // count == size here: the carry-rippler trick visits every subset of
    // `mask` (including the empty subset) exactly once.

    std::vector<Bitboard> table(static_cast<std::size_t>(size), kEmptyBitboard);
    std::vector<bool> used(static_cast<std::size_t>(size), false);

    for (;;) {
        const std::uint64_t candidate = sparse_random_u64(rng);

        // Cheap pre-filter: a working magic almost always spreads the
        // mask's bits widely across the top byte after multiplication.
        // This just skips obviously-bad candidates faster; it never
        // rejects a candidate that the full verification below would
        // have accepted as a *different* result — correctness comes
        // entirely from the collision check, this is a speed heuristic.
        if (popcount((mask * candidate) & 0xFF00000000000000ULL) < 6) {
            continue;
        }

        std::fill(used.begin(), used.end(), false);
        bool collision = false;

        for (int i = 0; i < size && !collision; ++i) {
            const std::uint64_t index = (occupancies[static_cast<std::size_t>(i)] * candidate) >>
                                         (64 - bits);
            if (!used[index]) {
                used[index] = true;
                table[index] = reference[static_cast<std::size_t>(i)];
            } else if (table[index] != reference[static_cast<std::size_t>(i)]) {
                collision = true;
            }
        }

        if (!collision) {
            return SquareMagic{candidate, std::move(table)};
        }
    }
}

std::array<Bitboard, kNumSquares> g_rook_mask{};
std::array<Bitboard, kNumSquares> g_bishop_mask{};
std::array<std::uint64_t, kNumSquares> g_rook_magic{};
std::array<std::uint64_t, kNumSquares> g_bishop_magic{};
std::array<int, kNumSquares> g_rook_bits{};
std::array<int, kNumSquares> g_bishop_bits{};
std::array<std::vector<Bitboard>, kNumSquares> g_rook_table;
std::array<std::vector<Bitboard>, kNumSquares> g_bishop_table;
bool g_initialized = false;

} // namespace

void init_magic_bitboards() {
    if (g_initialized) {
        return;
    }

    // Fixed seed: reproducible magics/tables across every run and platform.
    Xorshift64Star rng(0x9E3779B97F4A7C15ULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        g_rook_mask[sq] = relevant_occupancy_mask(sq, kRookDeltas);
        g_rook_bits[sq] = popcount(g_rook_mask[sq]);
        SquareMagic result = find_magic_for_square(sq, g_rook_mask[sq], kRookDeltas, rng);
        g_rook_magic[sq] = result.magic;
        g_rook_table[sq] = std::move(result.table);
    }

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        g_bishop_mask[sq] = relevant_occupancy_mask(sq, kBishopDeltas);
        g_bishop_bits[sq] = popcount(g_bishop_mask[sq]);
        SquareMagic result = find_magic_for_square(sq, g_bishop_mask[sq], kBishopDeltas, rng);
        g_bishop_magic[sq] = result.magic;
        g_bishop_table[sq] = std::move(result.table);
    }

    g_initialized = true;
}

Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_rook_mask[sq];
    const std::uint64_t index = (relevant * g_rook_magic[sq]) >> (64 - g_rook_bits[sq]);
    return g_rook_table[sq][index];
}

Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_bishop_mask[sq];
    const std::uint64_t index = (relevant * g_bishop_magic[sq]) >> (64 - g_bishop_bits[sq]);
    return g_bishop_table[sq][index];
}

} // namespace nightwing::board
