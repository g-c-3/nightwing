// src/search/tt.cpp

#include "search/tt.h"

#include "search/search.h" // kMateThreshold -- see this file's header comment in tt.h

#if defined(_MSC_VER) && !defined(__clang__)
#include <xmmintrin.h>
#endif

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

// --- TTEntry::data packing (see tt.h's TTEntry comment and this file's
// header comment's THREAD-SAFETY NOTE) ---
//
// Bits [ 0:16) move_raw   (board::Move::raw())
// Bits [16:32) score      (stored, mate-distance-adjusted; reinterpreted
//                          as std::uint16_t to pack, std::int16_t to
//                          unpack -- two's complement round-trip is
//                          well-defined by C++20's mandated two's-
//                          complement signed representation, P0907)
// Bits [32:40) depth      (std::uint8_t)
// Bits [40:42) bound      (Bound, 2 bits)
// Bits [42:48) age        (6 bits, see TranspositionTable::new_search())
// Bits [48:64) unused, always 0
//
// Kept as one contiguous 48-bit payload (rather than spread across
// several independently-packed bytes) purely so pack/unpack is a small,
// fixed set of shifts/masks -- no semantic significance to the exact
// bit boundaries beyond "big enough for each field's actual range".

[[nodiscard]] constexpr std::uint64_t pack_data(std::uint16_t move_raw, int score, int depth,
                                                 Bound bound, std::uint8_t age) noexcept {
    const auto score_bits = static_cast<std::uint16_t>(score);
    const auto depth_bits = static_cast<std::uint8_t>(depth < 0 ? 0 : depth);
    const auto bound_bits = static_cast<std::uint64_t>(bound) & 0x3ULL;
    const auto age_bits = static_cast<std::uint64_t>(age) & 0x3FULL;
    return static_cast<std::uint64_t>(move_raw) | (static_cast<std::uint64_t>(score_bits) << 16) |
           (static_cast<std::uint64_t>(depth_bits) << 32) | (bound_bits << 40) | (age_bits << 42);
}

[[nodiscard]] constexpr std::uint16_t unpack_move_raw(std::uint64_t data) noexcept {
    return static_cast<std::uint16_t>(data & 0xFFFFULL);
}
[[nodiscard]] constexpr int unpack_score(std::uint64_t data) noexcept {
    return static_cast<int>(static_cast<std::int16_t>((data >> 16) & 0xFFFFULL));
}
[[nodiscard]] constexpr int unpack_depth(std::uint64_t data) noexcept {
    return static_cast<int>((data >> 32) & 0xFFULL);
}
[[nodiscard]] constexpr Bound unpack_bound(std::uint64_t data) noexcept {
    return static_cast<Bound>((data >> 40) & 0x3ULL);
}
[[nodiscard]] constexpr std::uint8_t unpack_age(std::uint64_t data) noexcept {
    return static_cast<std::uint8_t>((data >> 42) & 0x3FULL);
}

} // namespace

TranspositionTable::TranspositionTable(std::size_t size_mb) {
    const std::size_t total_bytes = size_mb * 1024ULL * 1024ULL;
    const std::size_t max_buckets = total_bytes / sizeof(TTBucket);
    // NOTE: buckets_.resize(...) would need TTBucket to be move-
    // constructible (vector<T>::resize() growing from empty move-
    // constructs each new element from a default-constructed
    // temporary) -- TTBucket (via TTEntry's atomic members) isn't, so
    // this constructs the whole vector directly instead: vector's own
    // sized constructor value-initializes each element IN PLACE
    // (DefaultInsertable is enough, no move needed), and the
    // subsequent move-assignment into buckets_ only transfers the
    // vector's internal buffer pointer, not the individual elements.
    buckets_ = std::vector<TTBucket>(round_down_power_of_2(max_buckets));
}

void TranspositionTable::clear() noexcept {
    // Not itself made concurrency-safe against a simultaneous probe()/
    // store() from another thread -- see tt.h's header comment: clear()
    // is a whole-table reset (UCI `ucinewgame`, tests), never called
    // while a Lazy SMP search is actually in flight on this table.
    // Each word reset independently via .store(): TTEntry's atomic
    // members have no copy/move assignment (std::atomic disables both),
    // so whole-struct assignment (`entry = TTEntry{}`) isn't available
    // the way it was before the lock-free redesign -- see this file's
    // header comment's THREAD-SAFETY NOTE.
    for (TTBucket& bucket : buckets_) {
        for (TTEntry& entry : bucket.entries) {
            entry.key_xor_data.store(0, std::memory_order_relaxed);
            entry.data.store(0, std::memory_order_relaxed);
        }
    }
    current_age_.store(0, std::memory_order_relaxed);
}

void TranspositionTable::new_search() noexcept {
    // 6 bits (see pack_data()'s age field above); relaxed load+store is
    // fine here -- see current_age_'s own doc comment in tt.h for why a
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
    const TTBucket& bucket = bucket_for(key);
    for (const TTEntry& entry : bucket.entries) {
        // Load order between the two words doesn't matter for safety
        // (this file's header comment's THREAD-SAFETY NOTE): whichever
        // order, a straddled concurrent write produces a combination
        // that fails the XOR-equality check below with overwhelming
        // probability, and is then correctly treated as a miss rather
        // than trusted.
        const std::uint64_t data = entry.data.load(std::memory_order_relaxed);
        const std::uint64_t key_xor_data = entry.key_xor_data.load(std::memory_order_relaxed);
        if ((key_xor_data ^ data) != key) {
            continue;
        }
        // key == 0 is the reserved "empty slot" sentinel (tt.h's
        // TTEntry comment) -- an entry that validates against key == 0
        // is either a genuinely never-written slot (data == 0 too, so
        // this branch is skipped below since bound() reads None either
        // way) or, astronomically unlikely, a real position that
        // actually hashes to 0; either way, treating it as a miss here
        // (rather than a hit with a meaningless None bound) matches
        // every other empty-slot check in this file.
        if (key == 0) {
            continue;
        }
        TTProbeResult result;
        result.hit = true;
        result.score = adjust_mate_score_from_storage(unpack_score(data), ply);
        result.move = board::Move::from_raw(unpack_move_raw(data));
        result.depth = unpack_depth(data);
        result.bound = unpack_bound(data);
        return result;
    }
    return TTProbeResult{};
}

void TranspositionTable::store(std::uint64_t key, int depth, int score, Bound bound,
                                board::Move move, int ply) noexcept {
    TTBucket& bucket = bucket_for(key);
    // One load for this whole store() call, not re-read per candidate
    // entry below -- current_age_ changes at most once per depth
    // iteration (new_search()), so a single snapshot is exactly as
    // correct as re-loading it repeatedly, and avoids extra atomic
    // traffic.
    const std::uint8_t age_now = current_age_.load(std::memory_order_relaxed);
    const std::uint64_t new_data =
        pack_data(move.raw(), adjust_mate_score_for_storage(score, ply), depth, bound, age_now);
    const std::uint64_t new_key_xor_data = key ^ new_data;

    // Write order (data first, then key_xor_data): a convention, not a
    // safety requirement -- see this file's header comment's THREAD-
    // SAFETY NOTE for why EITHER order is safe against a concurrent
    // reader (any straddled read fails the XOR-equality check and is
    // reported as a miss).
    auto write = [new_data, new_key_xor_data](TTEntry& e) noexcept {
        e.data.store(new_data, std::memory_order_relaxed);
        e.key_xor_data.store(new_key_xor_data, std::memory_order_relaxed);
    };

    for (TTEntry& entry : bucket.entries) {
        const std::uint64_t data = entry.data.load(std::memory_order_relaxed);
        const std::uint64_t key_xor_data = entry.key_xor_data.load(std::memory_order_relaxed);
        if ((key_xor_data ^ data) == key && key != 0) {
            // Same position already stored: refresh only if this store
            // is at least as deep, or the existing entry is simply
            // stale from an older generation -- otherwise leave the
            // existing, more valuable entry alone rather than
            // overwriting it with worse data.
            if (depth < unpack_depth(data) && unpack_age(data) == age_now) {
                return;
            }
            write(entry);
            return;
        }
    }

    // No existing entry for this exact position: pick a slot to use --
    // an empty one, else the least valuable per age-then-depth (CPW
    // "Replacement Strategy"). Values read from each candidate's own
    // `data` word are trusted for ranking purposes even without
    // validating them against any particular key -- this matches the
    // pre-lock-free version's own behavior (it never needed to know
    // WHICH position an "other" bucket entry belonged to, only its own
    // age/depth), and a rare torn/mismatched read here (this file's
    // header comment's THREAD-SAFETY NOTE) just makes this one
    // replacement decision slightly less optimal, never wrong in a way
    // that corrupts data -- the actual write below is always a clean,
    // correctly-paired new (data, key_xor_data).
    TTEntry* replace = &bucket.entries[0];
    std::uint8_t replace_age = unpack_age(replace->data.load(std::memory_order_relaxed));
    int replace_depth = unpack_depth(replace->data.load(std::memory_order_relaxed));
    for (TTEntry& entry : bucket.entries) {
        const std::uint64_t data = entry.data.load(std::memory_order_relaxed);
        if (unpack_bound(data) == Bound::None) {
            replace = &entry;
            break;
        }
        const std::uint8_t entry_age = unpack_age(data);
        const int entry_depth = unpack_depth(data);
        if (entry_age != age_now && replace_age == age_now) {
            replace = &entry; // any older-generation entry beats a current-generation one
            replace_age = entry_age;
            replace_depth = entry_depth;
        } else if (entry_age == replace_age && entry_depth < replace_depth) {
            replace = &entry; // among same-age candidates, evict the shallowest
            replace_age = entry_age;
            replace_depth = entry_depth;
        }
    }
    write(*replace);
}

} // namespace nightwing::search
