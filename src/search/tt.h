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
//
// THREAD-SAFETY NOTE (see docs/DECISIONS.md, 2026-09-03 (2), and
// ROADMAP.md Phase 7's "Lock-free TT for concurrent access" item): one
// TranspositionTable instance is shared (by reference) across the main
// search thread and every Lazy SMP helper thread for the same
// search_iterative_deepening() call, so probe()/store() must be safe to
// call concurrently from multiple threads on the same instance -- with
// NO locking at all, per this item's own name. This is the classic CPW
// "Shared Hash Table" XOR-checksum technique (also the scheme real
// engines including Stockfish have shipped): each TTEntry (see below)
// holds its packed data in one atomic 64-bit word, and a SECOND atomic
// 64-bit word holding `key XOR data` instead of the raw key. A reader
// loads both words (in either order -- see probe()'s own comment for
// why the order doesn't matter for safety), XORs them back together,
// and compares the result against the position's real Zobrist key: if
// they match, the entry is trusted as genuine; if not, it's treated
// exactly like a miss. Why this is safe under concurrent writes: each
// individual 64-bit word is read/written as one indivisible atomic
// unit (std::atomic<std::uint64_t>, guaranteed lock-free on every
// platform this project targets -- see the static_assert below), so
// there is no way to observe a WORD torn mid-write, only a possible
// mismatch between the two words if a reader's two loads straddle a
// writer's two stores (old key_xor_data + new data, or vice versa). A
// straddled mismatch fails the XOR-equality check above with
// overwhelming probability (the same 64-bit-birthday-paradox margin
// this file already relies on for key==0 meaning "empty"), so it's
// reported as a safe miss, never as corrupt data used as if it were
// real. Two THREADS WRITING THE SAME ENTRY CONCURRENTLY is the one case
// this doesn't cleanly resolve: the final state can end up being one
// writer's data word paired with the other writer's key_xor_data word,
// which will never again XOR-match anyone's key -- that specific slot
// is effectively lost (a permanent miss) until the next store()
// overwrites it outright. This is the accepted, standard trade-off of
// this exact technique in the published literature and in real
// shipping engines: it never returns wrong data, it can only lose an
// entry early. `current_age_` is std::atomic for the same underlying
// reason (see its own comment): new_search() (main thread only) writes
// it while probe()/store() (every thread) read it.

#include <array>
#include <atomic>
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
///
/// Lock-free layout (see this file's header comment's THREAD-SAFETY
/// NOTE): rather than storing `key`/`move_raw`/`score`/`depth`/
/// `bound_and_age` as separate plain fields the way a single-threaded-
/// only design would, every field except the raw key is packed into
/// one atomic 64-bit `data` word, and the raw key itself is never
/// stored directly -- only `key XOR data`, in `key_xor_data`. This
/// isn't a cosmetic repacking: it's what lets probe()/store() detect a
/// torn concurrent read/write using only two independent atomic word
/// accesses, with no lock at all. See tt.cpp's pack_data()/unpack_*()
/// free functions for the exact bit layout, and probe()/store() for how
/// the two words are combined and validated.
struct TTEntry {
    /// `stored_key XOR data` (see this struct's own comment above and
    /// the header comment's THREAD-SAFETY NOTE) -- NOT the raw key by
    /// itself. 0 in both `key_xor_data` and `data` together (an
    /// untouched, cleared entry) reads back as an implicit key of 0,
    /// which is reserved as the "empty slot" sentinel exactly as it was
    /// before this XOR scheme existed -- a real position hashing to
    /// exactly 0 is astronomically unlikely with a well-seeded 64-bit
    /// PRNG (standard practice; CPW's discussion of TT key storage).
    std::atomic<std::uint64_t> key_xor_data{0};

    /// Every OTHER field (move, score, depth, bound+age), packed into
    /// one atomic 64-bit word -- see tt.cpp's pack_data()/unpack_*()
    /// for the exact bit layout. Read/written as a single atomic unit
    /// specifically so a concurrent reader/writer race can only ever
    /// produce a whole-word-old or whole-word-new value for THIS word,
    /// never a bit-level torn mixture of both.
    std::atomic<std::uint64_t> data{0};
};

static_assert(sizeof(TTEntry) == 16,
              "TTEntry must be exactly 16 bytes so 4 fit in one 64-byte "
              "cache line -- see ARCHITECTURE.md's Transposition Table row");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "TTEntry's lock-free XOR-checksum scheme (this file's header "
              "comment's THREAD-SAFETY NOTE) depends on std::atomic<std::uint64_t> "
              "itself being lock-free on every platform this project targets -- "
              "if this ever fires, a real per-word lock would silently sneak back "
              "in via libatomic on that platform, defeating the whole point.");

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
    /// tt.cpp's pack_data()) -- not a practical concern given how few
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
    /// Returns a result with `hit == false` if `key` isn't present --
    /// which, per this file's header comment's THREAD-SAFETY NOTE, also
    /// covers the rare case of a genuinely-stored entry that a
    /// concurrent store() was mid-write on: that's indistinguishable
    /// from a real miss by design, and always safe to treat as one.
    /// Lock-free: safe to call concurrently with any number of other
    /// probe()/store() calls, any thread, any key, with no blocking.
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
    /// depth-preferred). Lock-free: safe to call concurrently with any
    /// number of other probe()/store() calls -- see this file's header
    /// comment's THREAD-SAFETY NOTE for the one accepted edge case (two
    /// threads storing to the exact same slot at the exact same instant
    /// can leave that one slot as a guaranteed miss until the next
    /// store() to it; never wrong data, only an occasional early loss
    /// of one entry).
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
    // Atomic: written by new_search() (main search thread only), read by
    // every probe()/store() call (any thread) -- see this file's header
    // comment's THREAD-SAFETY NOTE. relaxed ordering throughout: age is
    // a replacement-scheme heuristic, not a correctness-load-bearing
    // value -- a helper thread briefly seeing the previous generation's
    // age mid-transition just makes that one replacement decision
    // slightly less optimal, never wrong in a way that corrupts data.
    std::atomic<std::uint8_t> current_age_{0};

    [[nodiscard]] TTBucket& bucket_for(std::uint64_t key) noexcept {
        return buckets_[key & (buckets_.size() - 1)];
    }
    [[nodiscard]] const TTBucket& bucket_for(std::uint64_t key) const noexcept {
        return buckets_[key & (buckets_.size() - 1)];
    }
};

} // namespace nightwing::search
