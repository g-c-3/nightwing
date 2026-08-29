// src/eval/eval_cache.cpp
//
// See eval_cache.h.

#include "eval/eval_cache.h"

namespace nightwing::eval {
namespace {

/// Largest power of 2 that is <= v (minimum 1). Duplicated locally
/// rather than shared, matching search/tt.cpp's and eval/pawn_tt.cpp's
/// own identical choice to keep this a small, private, per-file helper
/// rather than a shared utility for one three-line function.
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

} // namespace

EvalCache::EvalCache(std::size_t size_kb) {
    const std::size_t total_bytes = size_kb * 1024ULL;
    const std::size_t max_entries = total_bytes / sizeof(EvalEntry);
    entries_.resize(round_down_power_of_2(max_entries));
}

void EvalCache::clear() noexcept {
    for (EvalEntry& entry : entries_) {
        entry = EvalEntry{};
    }
}

std::pair<bool, int> EvalCache::probe(std::uint64_t key) const noexcept {
    const EvalEntry& entry = entries_[index_for(key)];
    if (key != 0 && entry.key == key) {
        return {true, entry.value};
    }
    return {false, 0};
}

void EvalCache::store(std::uint64_t key, int value) noexcept {
    if (key == 0) {
        return;
    }
    EvalEntry& entry = entries_[index_for(key)];
    entry.key = key;
    entry.value = static_cast<std::int16_t>(value);
}

} // namespace nightwing::eval
