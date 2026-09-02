// src/search/tt.cpp

#include "search/tt.h"

#include <algorithm>
#include <mutex>

#if defined(_MSC_VER) && !defined(__clang__)
#include <xmmintrin.h>
#endif

#include "search/search.h" // kMateThreshold -- see this file's header comment in tt.h

namespace nightwing::search {
namespace {

/// Largest power of 2 that is <= v (minimum 1). Used to size the table
/// to the largest power-of-2 bucket count that fits in the requested
/// byte budget (ARCHITECTURE.md: power-of-2 sizing for fast index
/// masking, no modulo).
[[nodiscard]] constexpr std::size_t round_down_power_of_2(std::size_t v) noexcept {
    if (v == 0) {
        return 1;
    }
    std::size_t p = 1;
    while (p * 2 <= v) {
        p *= 2;
    }
    return p;
}

/// Converts a current-node-relative score (as negamax() computes it,
/// `ply` plies from the search's root) into a storage-relative score
/// that stays meaningful regardless of which ply it's later probed
/// from. Mirror operation of adjust_mate_score_from_storage() below.
/// CPW "Score in TT": a stored mate score's magnitude is pushed toward
/// its ply-independent "intrinsic" value on the way in, then pulled
/// back down to whatever ply it's retrieved at on the way out --
/// otherwise the same stored value would misreport mate distance when
/// reused from a different distance-from-root than it was computed at.
[[nodiscard]] constexpr int adjust_mate_score_for_storage(int score, int ply) noexcept {
    if (score >= kMateThreshold) {
        return score + ply;
    }
    if (score <= -kMateThreshold) {
        return score - ply;
    }
    return score;
}

/// Inverse of adjust_mate_score_for_storage() -- see its comment.
[[nodiscard]] constexpr int adjust_mate_score_from_storage(int score, int ply) noexcept {
    if (score >= kMateThreshold) {
        return score - ply;
    }
    if (score <= -kMateThreshold) {
        return score + ply;
    }
    return score;
}

} // namespace

TranspositionTable::TranspositionTable(std::size_t size_mb) {
    const std::size_t total_bytes = size_mb * 1024ULL * 1024ULL;
    const std::size_t max_buckets = total_bytes / sizeof(TTBucket);
    buckets_.resize(round_down_power_of_2(max_buckets));
    // See tt.h's shard_locks_ comment: never more shards than buckets
    // (both powers of 2, so the min is too). std::vector<std::mutex>'s
    // sized constructor value-initializes each element in place -- no
    // per-element move/copy required, only vector's own internal buffer
    // is (trivially) transferred by this move-assignment.
    shard_locks_ = std::vector<std::mutex>(std::min(buckets_.size(), kMaxLockShards));
}

void TranspositionTable::clear() noexcept {
    // Not itself made concurrency-safe against a simultaneous probe()/
    // store() from another thread -- see tt.h's header comment: clear()
    // is a whole-table reset (UCI `ucinewgame`, tests), never called
    // while a Lazy SMP search is actually in flight on this table.
    for (TTBucket& bucket : buckets_) {
        for (TTEntry& entry : bucket.entries) {
            entry = TTEntry{};
        }
    }
    current_age_.store(0, std::memory_order_relaxed);
}

void TranspositionTable::new_search() noexcept {
    // 6 bits (see TTEntry::bound_and_age); relaxed load+store is fine
    // here -- see current_age_'s own doc comment in tt.h for why a
    // torn/stale read by a concurrent probe()/store() is harmless.
    const std::uint8_t next =
        static_cast<std::uint8_t>((current_age_.load(std::memory_order_relaxed) + 1) & 0x3F);
    current_age_.store(next, std::memory_order_relaxed);
}

void TranspositionTable::prefetch(std::uint64_t key) const noexcept {
    const void* addr = &bucket_for(key);
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr);
#elif defined(_MSC_VER)
    _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
#else
    (void)addr; // No prefetch intrinsic available on this toolchain -- a correctness no-op, not an error.
#endif
}

TTProbeResult TranspositionTable::probe(std::uint64_t key, int ply) const noexcept {
    // See tt.h's THREAD-SAFETY NOTE: this stripe lock is also taken by
    // store() for the same key, so a probe() never observes a
    // partially-written entry from a concurrent store().
    std::scoped_lock lock(lock_for(key));
    const TTBucket& bucket = bucket_for(key);
    for (const TTEntry& entry : bucket.entries) {
        if (entry.bound() != Bound::None && entry.key == key) {
            TTProbeResult result;
            result.hit = true;
            result.score = adjust_mate_score_from_storage(entry.score, ply);
            result.move = board::Move::from_raw(entry.move_raw);
            result.depth = entry.depth;
            result.bound = entry.bound();
            return result;
        }
    }
    return TTProbeResult{};
}

void TranspositionTable::store(std::uint64_t key, int depth, int score, Bound bound,
                                board::Move move, int ply) noexcept {
    // See tt.h's THREAD-SAFETY NOTE.
    std::scoped_lock lock(lock_for(key));
    TTBucket& bucket = bucket_for(key);
    // One load for this whole store() call, not re-read per candidate
    // entry below -- current_age_ changes at most once per depth
    // iteration (new_search()), so a single snapshot is exactly as
    // correct as re-loading it repeatedly, and avoids extra atomic
    // traffic while this stripe's lock is held.
    const std::uint8_t age_now = current_age_.load(std::memory_order_relaxed);

    for (TTEntry& entry : bucket.entries) {
        if (entry.bound() != Bound::None && entry.key == key) {
            // Same position already stored: refresh only if this store
            // is at least as deep, or the existing entry is simply
            // stale from an older generation -- otherwise leave the
            // existing, more valuable entry alone rather than
            // overwriting it with worse data.
            if (depth < entry.depth && entry.age() == age_now) {
                return;
            }
            write_entry(entry, key, depth, score, bound, move, ply, age_now);
            return;
        }
    }

    // No existing entry for this exact position: pick a slot to use --
    // an empty one, else the least valuable per age-then-depth (CPW
    // "Replacement Strategy").
    TTEntry* replace = &bucket.entries[0];
    for (TTEntry& entry : bucket.entries) {
        if (entry.bound() == Bound::None) {
            replace = &entry;
            break;
        }
        if (entry.age() != age_now && replace->age() == age_now) {
            replace = &entry; // any older-generation entry beats a current-generation one
        } else if (entry.age() == replace->age() && entry.depth < replace->depth) {
            replace = &entry; // among same-age candidates, evict the shallowest
        }
    }
    write_entry(*replace, key, depth, score, bound, move, ply, age_now);
}

void TranspositionTable::write_entry(TTEntry& e, std::uint64_t key, int depth, int score,
                                      Bound bound, board::Move move, int ply,
                                      std::uint8_t age_now) const noexcept {
    e.key = key;
    e.move_raw = move.raw();
    e.score = static_cast<std::int16_t>(adjust_mate_score_for_storage(score, ply));
    e.depth = static_cast<std::uint8_t>(depth < 0 ? 0 : depth);
    e.bound_and_age = static_cast<std::uint8_t>(
        (static_cast<std::uint8_t>(bound) & 0x3) | static_cast<std::uint8_t>(age_now << 2));
}

} // namespace nightwing::search
