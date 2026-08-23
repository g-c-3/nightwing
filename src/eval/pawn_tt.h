#pragma once
// src/eval/pawn_tt.h
//
// Pawn hash table: caches eval::pawn_structure_value()'s result, keyed
// on a hash of pawn placement ONLY (board::compute_pawn_hash(),
// board/zobrist.h) rather than the full position -- pawn structure
// changes far less often move-to-move than the rest of the position
// (most moves don't touch a pawn at all), so many search nodes share an
// identical pawn hash even when their full Zobrist hash differs, making
// this a genuinely different, complementary cache to the main
// transposition table (search/tt.h), not a smaller copy of it. CPW
// "Pawn Hash Table" -- from-scratch implementation of this standard
// technique, no code copied.
//
// Deliberately much simpler than TranspositionTable (search/tt.h): no
// depth, no alpha-beta bound type, no mate-distance ply adjustment --
// pawn structure eval is a pure function of pawn placement alone, not
// of search depth or window, so a stored value is either exactly right
// for ANY future probe with the same key, or (a different key) not
// present at all. Single-entry-per-slot with unconditional replacement
// on collision (CPW's own "Pawn Hash Table" article notes this is
// standard for pawn tables specifically -- unlike the main TT, a wrong
// eviction here just means recomputing a cheap-ish value, not losing
// deep search results), rather than search/tt.h's 4-way bucket +
// age/depth-preferred replacement scheme.
//
// Lives in `eval`, not `search`, despite ROADMAP.md describing it as "a
// small separate TT" -- its entire purpose is caching an eval-layer
// value (pawn_structure_value(), eval/pawns.h), and eval already sits
// beneath search in this codebase's dependency direction (search
// includes eval headers, never the reverse); keeping this table in
// `eval` avoids search::search.h needing to reach back into `eval` for
// a type search.cpp itself will own an instance of anyway (see
// search.cpp's ownership of TranspositionTable/KillerTable/HistoryTable
// for the established pattern this follows).
//
// LIFETIME NOTE: same interim scoping as TranspositionTable (see
// search/tt.h's header comment) -- one fresh, private instance per
// top-level search_fixed_depth()/search_iterative_deepening() call,
// shared across that call's own depth iterations, not yet a persistent
// global. Belongs with the same eventual UCI `Hash` option work
// search/tt.h's note describes.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "eval/score.h"

namespace nightwing::eval {

/// Single pawn-hash-table entry: a pawn-only Zobrist key and the
/// pawn_structure_value() result stored for it. No attempt made to pack
/// this down to a cache-line-friendly size the way search::TTEntry is
/// (search/tt.h): this table is deliberately much smaller and simpler,
/// one entry per slot, and packing further would cost clarity for a
/// table that's already tiny relative to the main TT.
struct PawnEntry {
    /// 0 is the "empty slot" sentinel -- see search::TTEntry::key's
    /// identical reasoning in search/tt.h (a real pawn structure hashing
    /// to exactly 0 is astronomically unlikely with a well-seeded 64-bit
    /// PRNG, so a freshly-constructed/cleared table, all entries zeroed,
    /// is correctly "everything empty" with no separate per-entry flag
    /// needed).
    std::uint64_t key = 0;
    Score value;
};

/// A small, power-of-2-sized, single-entry-per-slot cache from pawn-only
/// Zobrist key (board::compute_pawn_hash()) to
/// eval::pawn_structure_value()'s result for that pawn structure. See
/// this file's header comment for the full rationale.
class PawnHashTable {
public:
    /// Constructs a table sized to fit within `size_kb` KILOBYTES --
    /// deliberately KB, not MB like TranspositionTable's `size_mb`: this
    /// table is meant to be a small fraction of the main TT's size, per
    /// CPW's own sizing guidance for pawn hash tables. Rounded DOWN to
    /// the largest power-of-2 entry count that fits (ARCHITECTURE.md:
    /// power-of-2 sizing for fast index masking, no modulo). `size_kb`
    /// too small for even one entry constructs a minimum 1-entry table
    /// rather than an unusable empty one.
    explicit PawnHashTable(std::size_t size_kb);

    /// Zeroes every entry. Not called automatically anywhere yet -- each
    /// top-level search call gets a fresh, already-zeroed table via its
    /// constructor instead (this file's header comment) -- exposed for
    /// tests and for whatever eventually implements UCI `ucinewgame`.
    void clear() noexcept;

    /// Looks up `key`. Returns {true, value} on a hit, {false, Score{}}
    /// on a miss (key not present, or a different key occupies its
    /// slot).
    [[nodiscard]] std::pair<bool, Score> probe(std::uint64_t key) const noexcept;

    /// Stores `value` for `key`, unconditionally overwriting whatever
    /// (if anything) previously occupied that slot -- see this file's
    /// header comment on why unconditional replacement is the right
    /// choice here, unlike search/tt.h's age/depth-preferred scheme. A
    /// `key` of exactly 0 (the empty-slot sentinel) is silently not
    /// stored -- astronomically unlikely for a real pawn structure (see
    /// PawnEntry::key's doc comment), but refusing it defensively means
    /// a hypothetical collision there can never be mistaken for a real
    /// stored entry on a later probe.
    void store(std::uint64_t key, const Score& value) noexcept;

    /// Number of entries in the table -- test/diagnostic convenience.
    [[nodiscard]] std::size_t num_entries() const noexcept { return entries_.size(); }

private:
    std::vector<PawnEntry> entries_;

    [[nodiscard]] std::size_t index_for(std::uint64_t key) const noexcept {
        return key & (entries_.size() - 1);
    }
};

} // namespace nightwing::eval
