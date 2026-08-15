#pragma once
// src/search/tt.h
//
// Transposition table: caches previously-searched positions' scores
// (with an alpha-beta bound type), best move, and search depth, keyed
// by Zobrist hash, so identical positions reached via different move
// orders (transpositions) don't get re-searched from scratch. Technique
// and replacement scheme per Chess Programming Wiki ("Transposition
// Table", its "Replacement Strategy" section, "Node Types" for the
// Exact/Lower/Upper bound convention, and "Score in TT" for the mate-
// distance ply adjustment below) -- from-scratch implementation of
// these public techniques, no code copied.
//
// Layout matches ARCHITECTURE.md's Transposition Table row: power-of-2
// sized, 16-byte entries, 4 entries per 64-byte bucket (cache line),
// age + depth replacement, explicit prefetch on probe.
//
// LIFETIME NOTE (see docs/DECISIONS.md, 2026-08-15 "Transposition table
// lifetime scoped per top-level search call, not a persistent global"):
// despite ARCHITECTURE.md's eventual "single global TT" target, for now
// each top-level search_fixed_depth()/search_iterative_deepening() call
// constructs its own fresh, private TranspositionTable rather than
// sharing one process-lifetime global instance. Making it a real
// persistent global belongs with the UCI `Hash` option and a
// `ucinewgame`-triggered clear (ROADMAP.md Phase 8) -- without that
// lifecycle management, a persistent global now would be untested and
// would make tests/search_tests.cpp's cross-call node-count/result
// comparisons silently depend on unrelated earlier calls within the
// same test process. The class itself is already built to the final
// spec (sizing, replacement scheme, cache layout) -- only its lifetime
// is the interim simplification.

#include <array>
#include <cstdint>
#include <vector>

#include "board/move.h"

namespace nightwing::search {

/// How a stored score relates to the position's true minimax value, per
/// the standard CPW "Node Types" convention: `Exact` is a fully-
/// resolved score (a PV node); `Lower` means the true score is at least
/// this value (a fail-high/beta-cutoff node -- only a lower bound was
/// proven); `Upper` means the true score is at most this value (a
/// fail-low node -- every move was tried and none reached alpha, so
/// only an upper bound was proven). `None` marks an empty slot.
enum class Bound : std::uint8_t {
    None = 0,
    Exact = 1,
    Lower = 2,
    Upper = 3,
};

/// Result of probe(). `hit == false` means the key wasn't found at all
/// (every other field is meaningless in that case). A `true` hit does
/// NOT by itself license a cutoff -- callers must still check
/// `depth >= <required remaining depth>` and apply `bound`'s meaning
/// against their own alpha/beta (see negamax()'s probe-handling code in
/// search.cpp for the exact, deliberately conservative pattern used).
/// `move` is meaningful on any hit, even one too shallow for a cutoff --
/// it's still a legal move worth trying first, though move ORDERING
/// isn't wired up to use this yet as of this commit (see ROADMAP.md's
/// separate "Move ordering" item).
struct TTProbeResult {
    bool hit = false;
    int score = 0;
    board::Move move;
    int depth = 0;
    Bound bound = Bound::None;
};

/// Single transposition-table entry -- deliberately exactly 16 bytes so
/// four fit in one 64-byte cache line (see TranspositionTable::TTBucket).
struct TTEntry {
    /// Full 64-bit Zobrist hash of the stored position. 0 is reserved as
    /// the "empty slot" sentinel -- a real position hashing to exactly 0
    /// is astronomically unlikely with a well-seeded 64-bit PRNG
    /// (standard practice; see CPW's discussion of TT key storage) -- so
    /// a freshly-constructed/cleared table (all entries zeroed) is
    /// correctly "everything empty" with no separate per-entry flag
    /// needed.
    std::uint64_t key = 0;

    /// Best/refutation move found at this entry, packed via
    /// board::Move::raw(). Default (0) is board::Move()'s null-move
    /// sentinel -- meaningful only alongside a non-zero key.
    std::uint16_t move_raw = 0;

    /// Score, already mate-distance-adjusted to be independent of which
    /// ply it was stored from (see tt.cpp's store()/probe() for the
    /// exact adjustment) so it's meaningful when later probed from a
    /// DIFFERENT ply than it was stored at. Fits comfortably in
    /// int16_t: kMateScore (32000) and all realistic eval magnitudes
    /// are well within [-32768, 32767].
    std::int16_t score = 0;

    /// Remaining search depth this entry was stored at -- a probe can
    /// only be trusted for a cutoff if this is >= the CURRENT node's
    /// required remaining depth (a shallower stored search doesn't
    /// prove enough about a deeper request).
    std::uint8_t depth = 0;

    /// Packed: low 2 bits = Bound, high 6 bits = age/generation (see
    /// TranspositionTable::new_search()). Packed into one byte
    /// specifically to keep this struct at exactly 16 bytes -- see the
    /// static_assert below.
    std::uint8_t bound_and_age = 0;

    [[nodiscard]] constexpr Bound bound() const noexcept {
        return static_cast<Bound>(bound_and_age & 0x3);
    }
    [[nodiscard]] constexpr std::uint8_t age() const noexcept {
        return static_cast<std::uint8_t>(bound_and_age >> 2);
    }
};

static_assert(sizeof(TTEntry) == 16,
              "TTEntry must be exactly 16 bytes so 4 fit in one 64-byte "
              "cache line -- see ARCHITECTURE.md's Transposition Table row");

/// Transposition table: a power-of-2-sized array of 4-entry buckets (one
/// 64-byte cache line each), indexed by the low bits of a position's
/// Zobrist hash. See this file's header comment for the current
/// per-call lifetime scoping (not yet a persistent global).
class TranspositionTable {
public:
    /// Constructs a table sized to fit within `size_mb` megabytes,
    /// rounded DOWN to the largest power-of-2 bucket count that fits
    /// (ARCHITECTURE.md: "power-of-2 sized ... for fast index masking,
    /// no modulo"). `size_mb` too small for even one bucket constructs
    /// a minimum 1-bucket table (4 entries) rather than an unusable
    /// empty one.
    explicit TranspositionTable(std::size_t size_mb);

    /// Zeroes every entry and resets the age counter to 0. Not called
    /// automatically anywhere yet -- each top-level search call gets a
    /// fresh, already-zeroed table via its constructor instead (this
    /// file's header comment) -- exposed for tests and for whatever
    /// eventually implements UCI `ucinewgame`.
    void clear() noexcept;

    /// Marks the start of a new search generation: subsequent store()
    /// calls tag entries with the new age, and the replacement scheme
    /// (see store()) treats any entry from an older age as safe to
    /// overwrite unconditionally, regardless of its depth.
    /// search_iterative_deepening() calls this once per depth iteration
    /// (search.cpp), so the age+depth replacement scheme is genuinely
    /// exercised even though the table itself is currently short-lived
    /// (this file's header comment). Age wraps at 64 (6 bits, see
    /// TTEntry::bound_and_age) -- not a practical concern given how few
    /// generations a single top-level call produces today, but noted
    /// here for whoever eventually makes the table persistent across
    /// many real games' worth of searches.
    void new_search() noexcept;

    /// Issues a hardware prefetch for the bucket `key` maps to.
    /// ARCHITECTURE.md: "explicit prefetch of the TT entry for a
    /// position issued as early as possible in the search node" --
    /// callers should call this immediately after computing/knowing a
    /// node's Zobrist hash, before other per-node work, so the memory
    /// fetch overlaps with move generation/ordering instead of stalling
    /// probe(). A no-op (not an error) on toolchains without a prefetch
    /// intrinsic available.
    void prefetch(std::uint64_t key) const noexcept;

    /// Looks up `key`. `ply` translates a stored (storage-relative) mate
    /// score back to be relative to the CURRENT node -- see tt.cpp for
    /// the exact adjustment and why it's necessary (CPW "Score in TT").
    /// Returns a result with `hit == false` if `key` isn't present.
    [[nodiscard]] TTProbeResult probe(std::uint64_t key, int ply) const noexcept;

    /// Stores a search result for `key`. `score` is relative to the
    /// CURRENT node exactly as negamax() computes it -- store() itself
    /// performs the mate-distance conversion (the mirror of probe()'s
    /// adjustment) so callers never need to think about it. Replacement
    /// within `key`'s 4-entry bucket: an existing entry for the same
    /// key is refreshed only if this store is at least as deep (or the
    /// existing one is from an older generation); otherwise, among
    /// *other* positions' entries, prefers an empty slot, then an entry
    /// from an older search generation, then the entry with the
    /// smallest stored depth (CPW "Replacement Strategy": age-then-
    /// depth-preferred).
    void store(std::uint64_t key, int depth, int score, Bound bound,
               board::Move move, int ply) noexcept;

    /// Number of 4-entry buckets in the table -- test/diagnostic
    /// convenience (e.g. constructing a deliberately tiny table to
    /// exercise the replacement scheme deterministically).
    [[nodiscard]] std::size_t num_buckets() const noexcept { return buckets_.size(); }

private:
    struct alignas(64) TTBucket {
        std::array<TTEntry, 4> entries{};
    };
    static_assert(sizeof(TTBucket) == 64,
                  "TTBucket must be exactly one 64-byte cache line "
                  "(4x 16-byte TTEntry) -- see ARCHITECTURE.md");

    std::vector<TTBucket> buckets_;
    std::uint8_t current_age_ = 0;

    [[nodiscard]] TTBucket& bucket_for(std::uint64_t key) noexcept {
        return buckets_[key & (buckets_.size() - 1)];
    }
    [[nodiscard]] const TTBucket& bucket_for(std::uint64_t key) const noexcept {
        return buckets_[key & (buckets_.size() - 1)];
    }

    /// Fills `e` with a new stored result, applying the mate-distance
    /// storage adjustment and tagging it with the table's current age.
    void write_entry(TTEntry& e, std::uint64_t key, int depth, int score,
                      Bound bound, board::Move move, int ply) const noexcept;
};

} // namespace nightwing::search
