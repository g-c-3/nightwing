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
//      This is the portable path, always built.
//   4. A BMI2 PEXT-indexed table, built only when compiled with
//      NIGHTWING_ENABLE_BMI2 (see root/src CMakeLists.txt — only ever
//      defined on x86/x86_64). No search needed here: PEXT itself
//      guarantees a collision-free dense index, so the table is just
//      "for every occupancy subset, store its reference attack set at
//      pext(subset, mask)".
//   5. Runtime dispatch between the two: rook_attacks()/bishop_attacks()
//      use the PEXT table only when support::cpu_has_bmi2() confirms the
//      *running* CPU actually supports it, regardless of whether PEXT
//      code was compiled in — a BMI2 build still runs correctly on older
//      hardware.
//
// All of this runs once at startup; none of it is hot-path code, so
// clarity is favored over micro-optimization throughout this file.

#include "board/attacks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "support/cpu_features.h"
#include "support/rng.h"

#if defined(NIGHTWING_ENABLE_BMI2)
#include <immintrin.h>
#endif

namespace nightwing::board {

namespace {

/// (file delta, rank delta) pairs for a piece's movement directions.
using Deltas = std::array<std::pair<int, int>, 4>;

constexpr Deltas kRookDeltas = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
constexpr Deltas kBishopDeltas = {{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};

/// Returns a sparse random 64-bit candidate (AND of three random draws —
/// magics with fewer set bits are more likely to work and are found
/// faster this way; standard trick for this kind of search).
std::uint64_t sparse_random_u64(nightwing::support::Xorshift64Star& rng) {
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
/// this is the "slow but obviously correct" version that both the magic
/// and PEXT lookups are verified against.
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

/// Every occupancy subset of a mask, paired with its ground-truth attack
/// set, enumerated once and shared by both the magic search and the PEXT
/// table builder (both need exactly this data, indexed differently).
struct SubsetData {
    std::vector<Bitboard> occupancies;
    std::vector<Bitboard> reference;
};

/// Enumerates every subset of `mask` via the standard carry-rippler trick
/// and computes each subset's reference attack set from `sq` along `deltas`.
SubsetData enumerate_subsets(Square sq, Bitboard mask, const Deltas& deltas) {
    const int size = 1 << popcount(mask);

    SubsetData data;
    data.occupancies.resize(static_cast<std::size_t>(size));
    data.reference.resize(static_cast<std::size_t>(size));

    Bitboard subset = kEmptyBitboard;
    int count = 0;
    do {
        data.occupancies[static_cast<std::size_t>(count)] = subset;
        data.reference[static_cast<std::size_t>(count)] = attacks_on_the_fly(sq, deltas, subset);
        ++count;
        subset = (subset - mask) & mask;
    } while (subset != kEmptyBitboard);
    // count == size here: the carry-rippler trick visits every subset of
    // `mask` (including the empty subset) exactly once.

    return data;
}

/// Searches for a working magic number for `sq`/`mask` and builds its
/// attack table (indexed by `(occ * magic) >> (64 - bits)`). Tries sparse
/// random magic candidates until one produces no *harmful* collision (two
/// different occupancies mapping to the same index are fine as long as
/// they'd produce the same attack set — which happens naturally for many
/// subsets, and is why the table can be far smaller than 2^64).
std::uint64_t find_magic_for_square(Bitboard mask, const SubsetData& data,
                                     nightwing::support::Xorshift64Star& rng,
                                     std::vector<Bitboard>& table_out) {
    const int bits = popcount(mask);
    const int size = 1 << bits;

    table_out.assign(static_cast<std::size_t>(size), kEmptyBitboard);
    // NOTE: deliberately std::vector<std::uint8_t>, not std::vector<bool>.
    // std::vector<bool> is bit-packed and accessed through a proxy
    // reference type rather than a real bool&, which both the fill below
    // and the used[index] read/write in the innermost loop pay for on
    // every one of potentially thousands of candidate attempts per
    // square. GCC/Clang's -O2 optimizes this shape well enough that it
    // was invisible on Linux/macOS; MSVC's -O2 does not, and this was the
    // actual cause of Windows Debug's ~30s-per-test cost surviving the
    // attacks.cpp optimization-override fix and being unrelated to BMI2
    // (see docs/DECISIONS.md, this date, for the full trace: BMI2 was
    // confirmed present and irrelevant since the magic-number search
    // below runs unconditionally regardless of whether the PEXT table
    // ends up used at runtime). uint8_t keeps one real byte per element
    // so both compilers get plain, unpacked array codegen.
    std::vector<std::uint8_t> used(static_cast<std::size_t>(size), 0);

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

        std::fill(used.begin(), used.end(), 0);
        bool collision = false;

        for (int i = 0; i < size && !collision; ++i) {
            const std::uint64_t index = (data.occupancies[static_cast<std::size_t>(i)] * candidate) >>
                                         (64 - bits);
            if (!used[index]) {
                used[index] = true;
                table_out[index] = data.reference[static_cast<std::size_t>(i)];
            } else if (table_out[index] != data.reference[static_cast<std::size_t>(i)]) {
                collision = true;
            }
        }

        if (!collision) {
            return candidate;
        }
    }
}

#if defined(NIGHTWING_ENABLE_BMI2)
/// Builds the PEXT-indexed attack table for `mask`. No search needed:
/// PEXT deterministically maps each subset's bit pattern to a unique
/// dense index in [0, 2^bits), so this is a direct one-pass fill.
std::vector<Bitboard> build_pext_table(Bitboard mask, const SubsetData& data) {
    const int size = 1 << popcount(mask);
    std::vector<Bitboard> table(static_cast<std::size_t>(size), kEmptyBitboard);

    for (std::size_t i = 0; i < data.occupancies.size(); ++i) {
        const std::uint64_t index = _pext_u64(data.occupancies[i], mask);
        table[index] = data.reference[i];
    }
    return table;
}
#endif

std::array<Bitboard, kNumSquares> g_rook_mask{};
std::array<Bitboard, kNumSquares> g_bishop_mask{};
std::array<std::uint64_t, kNumSquares> g_rook_magic{};
std::array<std::uint64_t, kNumSquares> g_bishop_magic{};
std::array<int, kNumSquares> g_rook_bits{};
std::array<int, kNumSquares> g_bishop_bits{};
std::array<std::vector<Bitboard>, kNumSquares> g_rook_table;
std::array<std::vector<Bitboard>, kNumSquares> g_bishop_table;
bool g_initialized = false;

#if defined(NIGHTWING_ENABLE_BMI2)
std::array<std::vector<Bitboard>, kNumSquares> g_rook_pext_table;
std::array<std::vector<Bitboard>, kNumSquares> g_bishop_pext_table;

// Decided once at init: use the PEXT tables only when confirmed present
// on the running CPU. Only exists in BMI2 builds — on portable builds
// there's no PEXT path to dispatch to, so the variable itself would be
// unused there.
bool g_use_pext = false;
#endif

} // namespace

void init_magic_bitboards() {
    if (g_initialized) {
        return;
    }

    nightwing::support::detect_cpu_features();

    // Fixed seed: reproducible magics/tables across every run and platform.
    nightwing::support::Xorshift64Star rng(0x9E3779B97F4A7C15ULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        g_rook_mask[sq] = relevant_occupancy_mask(sq, kRookDeltas);
        g_rook_bits[sq] = popcount(g_rook_mask[sq]);
        const SubsetData data = enumerate_subsets(sq, g_rook_mask[sq], kRookDeltas);
        g_rook_magic[sq] = find_magic_for_square(g_rook_mask[sq], data, rng, g_rook_table[sq]);
#if defined(NIGHTWING_ENABLE_BMI2)
        g_rook_pext_table[sq] = build_pext_table(g_rook_mask[sq], data);
#endif
    }

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        g_bishop_mask[sq] = relevant_occupancy_mask(sq, kBishopDeltas);
        g_bishop_bits[sq] = popcount(g_bishop_mask[sq]);
        const SubsetData data = enumerate_subsets(sq, g_bishop_mask[sq], kBishopDeltas);
        g_bishop_magic[sq] = find_magic_for_square(g_bishop_mask[sq], data, rng, g_bishop_table[sq]);
#if defined(NIGHTWING_ENABLE_BMI2)
        g_bishop_pext_table[sq] = build_pext_table(g_bishop_mask[sq], data);
#endif
    }

#if defined(NIGHTWING_ENABLE_BMI2)
    g_use_pext = nightwing::support::cpu_has_bmi2();
#endif

    g_initialized = true;
}

Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_rook_mask[sq];
#if defined(NIGHTWING_ENABLE_BMI2)
    if (g_use_pext) {
        return g_rook_pext_table[sq][_pext_u64(relevant, g_rook_mask[sq])];
    }
#endif
    const std::uint64_t index = (relevant * g_rook_magic[sq]) >> (64 - g_rook_bits[sq]);
    return g_rook_table[sq][index];
}

Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_bishop_mask[sq];
#if defined(NIGHTWING_ENABLE_BMI2)
    if (g_use_pext) {
        return g_bishop_pext_table[sq][_pext_u64(relevant, g_bishop_mask[sq])];
    }
#endif
    const std::uint64_t index = (relevant * g_bishop_magic[sq]) >> (64 - g_bishop_bits[sq]);
    return g_bishop_table[sq][index];
}

#if defined(NIGHTWING_ENABLE_BMI2)
Bitboard rook_attacks_pext_for_testing(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_rook_mask[sq];
    return g_rook_pext_table[sq][_pext_u64(relevant, g_rook_mask[sq])];
}

Bitboard bishop_attacks_pext_for_testing(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_bishop_mask[sq];
    return g_bishop_pext_table[sq][_pext_u64(relevant, g_bishop_mask[sq])];
}
#endif

} // namespace nightwing::board
