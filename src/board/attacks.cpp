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
//      defined on x86/x86_64) AND, since ROADMAP.md Priority Fixes
//      (2026-09-22, finding 3), only when the *running* CPU actually
//      supports BMI2 (support::cpu_has_bmi2(), checked up front in
//      init_magic_bitboards() below, before any PEXT code runs at all —
//      see that function's own comment for the bug this fixes and why).
//      No search needed here regardless: PEXT itself guarantees a
//      collision-free dense index, so the table is just "for every
//      occupancy subset, store its reference attack set at
//      pext(subset, mask)".
//   5. Runtime dispatch between the two: rook_attacks()/bishop_attacks()
//      use the PEXT table only when support::cpu_has_bmi2() confirms the
//      *running* CPU actually supports it, regardless of whether PEXT
//      code was compiled in — a BMI2 build still runs correctly on older
//      hardware.
//
// BMI2 CODE ISOLATION (ROADMAP.md Priority Fixes, 2026-09-22, finding 3):
// src/CMakeLists.txt no longer applies -mbmi2/-mpopcnt to nightwing_lib
// as a whole — only pext_u64() below, the single, minimal wrapper around
// the actual _pext_u64 intrinsic, carries a GCC/Clang
// __attribute__((target("bmi2"))) (NIGHTWING_BMI2_TARGET below). Every
// other function in this file — including build_pext_table(),
// rook_attacks()/bishop_attacks() THEMSELVES, and the portable
// magic-multiply fallback path inside them — compiles under this TU's
// ordinary, non-BMI2 target and calls pext_u64() as a perfectly ordinary
// function call (calling a target-attributed function imposes no target
// requirement on the caller — this is standard GCC/Clang "function
// multiversioning" practice, confirmed against this project's own
// compiler (GCC 13.3, matching CI) with a real -O2 and a real -O3 -flto
// build: the PEXT instruction appears ONLY inside pext_u64()'s own
// generated code, nowhere else). This is deliberately NARROWER than the
// external review's own literally-suggested fix (a wholly separate
// translation unit for the PEXT table-BUILDING code) — see
// docs/DECISIONS.md, this dated entry, for why: splitting
// rook_attacks()/bishop_attacks() into a second TU would either (a)
// leave their own portable-fallback branch compiled under -mbmi2 too if
// naively whole-function-isolated (letting the compiler auto-generate a
// BMI2 instruction into the very fallback path meant to run on non-BMI2
// hardware — reintroducing a subtler version of the identical bug this
// item exists to fix), or (b) require moving rook_attacks()/
// bishop_attacks() themselves out of this file, giving up same-TU
// inlining for a genuinely hot path (called on nearly every search
// node) in exchange for LTO's less certain cross-TU guarantee. Isolating
// down to the single, tiny intrinsic-wrapping function avoids both.
//
// All of this runs once at startup; none of it is hot-path code, so
// clarity is favored over micro-optimization throughout this file. The
// one exception is pext_u64() itself, called from rook_attacks()/
// bishop_attacks()'s own hot path — but it's a single-instruction
// wrapper, so there's no clarity/speed tradeoff to weigh there either.

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

#if defined(NIGHTWING_ENABLE_BMI2)
namespace {

// GCC/Clang function-multiversioning target attribute (see this file's
// own header comment, "BMI2 CODE ISOLATION", for the full rationale) —
// MSVC needs no equivalent: its <immintrin.h> intrinsics are available
// unconditionally on x86_64 regardless of /arch: flags (src/CMakeLists.txt's
// own comment at the BMI2 option block), so pext_u64() below needs no
// special annotation there at all, just the runtime check its own
// caller already performs.
#if defined(__GNUC__) || defined(__clang__)
#define NIGHTWING_BMI2_TARGET __attribute__((target("bmi2")))
#else
#define NIGHTWING_BMI2_TARGET
#endif

/// The ONLY function in this file (indeed, in this codebase) whose own
/// compiled machine code can contain a PEXT instruction — every caller
/// below (build_pext_table(), rook_attacks()/bishop_attacks(), the
/// test-only PEXT hooks) calls this as an ordinary function, imposing no
/// target requirement on itself in turn. Precondition, enforced entirely
/// by this file's OWN callers, not by anything inside this function:
/// support::cpu_has_bmi2() must have already confirmed the running CPU
/// supports BMI2 before this is ever reached — calling it on hardware
/// that doesn't raises SIGILL, the exact bug this file's own "BMI2 CODE
/// ISOLATION" header comment and ROADMAP.md Priority Fixes (2026-09-22,
/// finding 3) describe. This function itself cannot check that
/// precondition — support::cpu_has_bmi2() is an ordinary, non-BMI2-
/// requiring function, deliberately kept as the single source of truth
/// callers consult BEFORE reaching here, rather than duplicating that
/// check on every call to what needs to stay a trivial, fast wrapper on
/// rook_attacks()'s/bishop_attacks()'s own hot path.
NIGHTWING_BMI2_TARGET
[[nodiscard]] std::uint64_t pext_u64(std::uint64_t value, std::uint64_t mask) noexcept {
    return _pext_u64(value, mask);
}

} // namespace
#endif

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
    // Generation-stamp collision tracking, replacing an earlier
    // std::vector<bool>/std::vector<uint8_t> "used" array that was reset
    // with std::fill() on every one of potentially thousands of candidate
    // attempts per square. That O(size) reset (not the element type) was
    // always the dominant cost of this loop; two different element-type
    // fixes each helped one platform and hurt another (see
    // docs/DECISIONS.md, this date, for the measured numbers: uint8_t
    // fixed MSVC's poor std::vector<bool> codegen but doubled per-test
    // cost on Apple Silicon/Clang, where vector<bool> was already fine
    // and uint8_t's 8x larger footprint just added cache pressure to the
    // same repeated fill). This scheme needs no reset at all: an index is
    // "used this attempt" only when stamp_at[index] equals the current
    // attempt's stamp, so a fresh attempt is just one increment.
    std::vector<std::uint32_t> stamp_at(static_cast<std::size_t>(size), 0);
    std::uint32_t stamp = 0;

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

        ++stamp;
        bool collision = false;

        for (int i = 0; i < size && !collision; ++i) {
            const std::uint64_t index = (data.occupancies[static_cast<std::size_t>(i)] * candidate) >>
                                         (64 - bits);
            if (stamp_at[index] != stamp) {
                stamp_at[index] = stamp;
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
///
/// Precondition: support::cpu_has_bmi2() has confirmed the running CPU
/// actually supports BMI2 (this function itself carries no target
/// attribute and cannot execute a PEXT instruction directly — see
/// pext_u64()'s own doc comment above for why that check lives entirely
/// in this function's own callers, init_magic_bitboards() below being
/// the one that matters here).
std::vector<Bitboard> build_pext_table(Bitboard mask, const SubsetData& data) {
    const int size = 1 << popcount(mask);
    std::vector<Bitboard> table(static_cast<std::size_t>(size), kEmptyBitboard);

    for (std::size_t i = 0; i < data.occupancies.size(); ++i) {
        const std::uint64_t index = pext_u64(data.occupancies[i], mask);
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

#if defined(NIGHTWING_ENABLE_BMI2)
    // THE FIX (ROADMAP.md Priority Fixes, 2026-09-22, finding 3): decided
    // HERE, before a single PEXT instruction has any chance to run, not
    // at the end of this function after build_pext_table() had already
    // been called unconditionally on every BMI2-compiled build regardless
    // of what the running CPU actually supports — the bug that made a
    // default build (NIGHTWING_ENABLE_BMI2 defaults ON) execute an
    // illegal instruction at startup on any pre-Haswell x86 CPU, even
    // though the runtime detection machinery (support::cpu_has_bmi2())
    // already existed; it just wasn't consulted until too late to matter.
    // g_use_pext is ALSO the flag rook_attacks()/bishop_attacks() read at
    // every call below, so setting it here, once, up front, is both the
    // fix and the only place that ever needs to set it.
    g_use_pext = nightwing::support::cpu_has_bmi2();
#endif

    // Fixed seed: reproducible magics/tables across every run and platform.
    nightwing::support::Xorshift64Star rng(0x9E3779B97F4A7C15ULL);

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        g_rook_mask[sq] = relevant_occupancy_mask(sq, kRookDeltas);
        g_rook_bits[sq] = popcount(g_rook_mask[sq]);
        const SubsetData data = enumerate_subsets(sq, g_rook_mask[sq], kRookDeltas);
        g_rook_magic[sq] = find_magic_for_square(g_rook_mask[sq], data, rng, g_rook_table[sq]);
#if defined(NIGHTWING_ENABLE_BMI2)
        if (g_use_pext) {
            g_rook_pext_table[sq] = build_pext_table(g_rook_mask[sq], data);
        }
#endif
    }

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        g_bishop_mask[sq] = relevant_occupancy_mask(sq, kBishopDeltas);
        g_bishop_bits[sq] = popcount(g_bishop_mask[sq]);
        const SubsetData data = enumerate_subsets(sq, g_bishop_mask[sq], kBishopDeltas);
        g_bishop_magic[sq] = find_magic_for_square(g_bishop_mask[sq], data, rng, g_bishop_table[sq]);
#if defined(NIGHTWING_ENABLE_BMI2)
        if (g_use_pext) {
            g_bishop_pext_table[sq] = build_pext_table(g_bishop_mask[sq], data);
        }
#endif
    }

    g_initialized = true;
}

Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_rook_mask[sq];
#if defined(NIGHTWING_ENABLE_BMI2)
    if (g_use_pext) {
        return g_rook_pext_table[sq][pext_u64(relevant, g_rook_mask[sq])];
    }
#endif
    const std::uint64_t index = (relevant * g_rook_magic[sq]) >> (64 - g_rook_bits[sq]);
    return g_rook_table[sq][index];
}

Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_bishop_mask[sq];
#if defined(NIGHTWING_ENABLE_BMI2)
    if (g_use_pext) {
        return g_bishop_pext_table[sq][pext_u64(relevant, g_bishop_mask[sq])];
    }
#endif
    const std::uint64_t index = (relevant * g_bishop_magic[sq]) >> (64 - g_bishop_bits[sq]);
    return g_bishop_table[sq][index];
}

#if defined(NIGHTWING_ENABLE_BMI2)
Bitboard rook_attacks_pext_for_testing(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_rook_mask[sq];
    return g_rook_pext_table[sq][pext_u64(relevant, g_rook_mask[sq])];
}

Bitboard bishop_attacks_pext_for_testing(Square sq, Bitboard occupied) noexcept {
    const Bitboard relevant = occupied & g_bishop_mask[sq];
    return g_bishop_pext_table[sq][pext_u64(relevant, g_bishop_mask[sq])];
}
#endif

} // namespace nightwing::board
