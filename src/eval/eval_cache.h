#pragma once
// src/eval/eval_cache.h
//
// Eval cache: caches eval::evaluate()'s full, final (already-tapered)
// centipawn result, keyed on a position's FULL Zobrist hash
// (board::Position::zobrist_hash, already incrementally maintained on
// every make_move()/unmake_move() -- no separate hash computation
// needed, unlike eval::PawnHashTable's board::compute_pawn_hash(),
// eval/pawn_tt.h). ROADMAP.md Phase 5, "Eval cache (optional
// performance optimization, separate from TT)".
//
// WHY THIS IS A DIFFERENT, GENUINELY USEFUL CACHE, NOT A DUPLICATE OF
// THE MAIN TT (search/tt.h) OR THE PAWN HASH TABLE (eval/pawn_tt.h):
// search::TranspositionTable caches SEARCH results (a score with an
// alpha-beta bound type, valid only relative to the depth/window it was
// stored at) -- it does not store a position's raw static eval at all.
// eval::PawnHashTable caches only the pawn-structure TERM, keyed on
// pawn placement alone. This cache stores evaluate()'s complete,
// final, side-agnostic (White-relative) return value -- material,
// PSQT, pawn structure, mobility, king safety, every other term, and
// the tapering itself -- for the exact, fully-placed position the full
// Zobrist hash identifies. A hit means "evaluate() was already computed
// for this EXACT position" -- skipping every one of its sub-terms'
// work entirely, not just the pawn-structure portion.
//
// WHY A HIT RATE EXISTS AT ALL, GIVEN eval.cpp's OWN "piece placement
// changes essentially every move" REASONING FOR NOT CACHING INDIVIDUAL
// VOLATILE TERMS (mobility, king safety, etc. -- see eval.cpp's
// evaluate() comment): that reasoning is about caching a SUB-term
// keyed on something coarser than the full position (which would see
// a near-100% miss rate, since almost any term OTHER than pawn
// structure changes on almost every move). This cache is keyed on the
// FULL position instead, so its hit rate isn't about how often piece
// placement repeats move-to-move (rarely) -- it's about how often the
// exact same full position is evaluated more than once, which happens
// for two concrete, unrelated reasons already present in this
// codebase's own search: (1) TRANSPOSITIONS -- different move orders
// reaching the identical position, the same phenomenon the main TT
// exists to exploit, and (2) the SAME node's static eval being
// computed MORE THAN ONCE within a single negamax() call -- as of this
// session, razoring and futility pruning (search.cpp) each call
// eval::evaluate() independently on the SAME unchanged `pos` when both
// of their own conditions apply at a node, which is a guaranteed,
// zero-transposition-required hit on its own. See docs/DECISIONS.md
// for the full rationale and this session's measurements.
//
// Modeled directly on eval::PawnHashTable (eval/pawn_tt.h): single-
// entry-per-slot, unconditional replacement on collision -- a cached
// value is either exactly right for any future probe with the same
// key (evaluate() is a pure function of `pos`), or (a different key)
// simply not present, with no notion of "depth" or "bound type" to
// prefer the way the main TT's replacement scheme has. A wrong
// eviction here just costs a full evaluate() recomputation, never a
// correctness issue.
//
// Lives in `eval`, not `search`, for the identical dependency-direction
// reasoning as eval::PawnHashTable (pawn_tt.h's own header comment) --
// this table's entire purpose is caching evaluate()'s own output.
//
// LIFETIME NOTE: same interim per-top-level-call scoping as
// TranspositionTable/PawnHashTable (search/tt.h's header comment) --
// one fresh, private instance per search_fixed_depth()/
// search_iterative_deepening() call, not yet a persistent global.
// Belongs with the same eventual UCI `Hash` option work those files'
// own notes describe.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace nightwing::eval {

/// Single eval-cache entry: a full Zobrist key and the White-relative
/// centipawn evaluate() result stored for it. `value` is int16_t, not
/// int -- comfortably sufficient (see search::TTEntry::score's
/// identical reasoning, search/tt.h): evaluate() never returns a mate
/// score (that's exclusively negamax()'s/quiescence()'s domain, layered
/// on top of a raw static eval), and realistic material+eval-term
/// magnitudes are well within [-32768, 32767]. Kept unpacked/uncompressed
/// otherwise, matching PawnEntry's own "this table is deliberately much
/// smaller and simpler than the main TT" choice (eval/pawn_tt.h).
struct EvalEntry {
    /// 0 is the "empty slot" sentinel -- identical reasoning to
    /// search::TTEntry::key/eval::PawnEntry::key (a real position
    /// hashing to exactly 0 is astronomically unlikely with a
    /// well-seeded 64-bit PRNG), so a freshly-constructed/cleared table
    /// is correctly "everything empty" with no separate per-entry flag.
    std::uint64_t key = 0;
    std::int16_t value = 0;
};

/// A small, power-of-2-sized, single-entry-per-slot cache from a
/// position's full Zobrist key (board::Position::zobrist_hash) to
/// eval::evaluate()'s full result for that exact position. See this
/// file's header comment for the full rationale.
class EvalCache {
public:
    /// Constructs a table sized to fit within `size_kb` KILOBYTES --
    /// KB, not MB like TranspositionTable's `size_mb`, matching
    /// PawnHashTable's own sizing convention (eval/pawn_tt.h): this
    /// cache is meant to be a small, optional supplement, not sized
    /// like the main search TT. Rounded DOWN to the largest power-of-2
    /// entry count that fits (ARCHITECTURE.md: power-of-2 sizing for
    /// fast index masking, no modulo). `size_kb` too small for even one
    /// entry constructs a minimum 1-entry table rather than an unusable
    /// empty one.
    explicit EvalCache(std::size_t size_kb);

    /// Zeroes every entry. Not called automatically anywhere yet -- each
    /// top-level search call gets a fresh, already-zeroed table via its
    /// constructor instead (this file's header comment) -- exposed for
    /// tests and for whatever eventually implements UCI `ucinewgame`.
    void clear() noexcept;

    /// Looks up `key`. Returns {true, value} on a hit, {false, 0} on a
    /// miss (key not present, or a different key occupies its slot).
    [[nodiscard]] std::pair<bool, int> probe(std::uint64_t key) const noexcept;

    /// Stores `value` for `key`, unconditionally overwriting whatever
    /// (if anything) previously occupied that slot -- see this file's
    /// header comment on why unconditional replacement is the right
    /// choice here, unlike search/tt.h's age/depth-preferred scheme. A
    /// `key` of exactly 0 (the empty-slot sentinel) is silently not
    /// stored, mirroring PawnHashTable::store()'s identical guard.
    void store(std::uint64_t key, int value) noexcept;

    /// Number of entries in the table -- test/diagnostic convenience.
    [[nodiscard]] std::size_t num_entries() const noexcept { return entries_.size(); }

private:
    std::vector<EvalEntry> entries_;

    [[nodiscard]] std::size_t index_for(std::uint64_t key) const noexcept {
        return key & (entries_.size() - 1);
    }
};

} // namespace nightwing::eval
