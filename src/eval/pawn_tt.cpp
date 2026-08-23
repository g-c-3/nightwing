// src/eval/pawn_tt.cpp
//
// See pawn_tt.h.

#include "eval/pawn_tt.h"

namespace nightwing::eval {
namespace {

/// Largest power of 2 that is <= v (minimum 1). Same pattern as
/// search/tt.cpp's round_down_power_of_2() -- duplicated locally rather
/// than shared, matching that file's own choice to keep this as a small,
/// private, per-file helper rather than a shared utility for one
/// three-line function.
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

PawnHashTable::PawnHashTable(std::size_t size_kb) {
    const std::size_t total_bytes = size_kb * 1024ULL;
    const std::size_t max_entries = total_bytes / sizeof(PawnEntry);
    entries_.resize(round_down_power_of_2(max_entries));
}

void PawnHashTable::clear() noexcept {
    for (PawnEntry& entry : entries_) {
        entry = PawnEntry{};
    }
}

std::pair<bool, Score> PawnHashTable::probe(std::uint64_t key) const noexcept {
    const PawnEntry& entry = entries_[index_for(key)];
    if (key != 0 && entry.key == key) {
        return {true, entry.value};
    }
    return {false, Score{}};
}

void PawnHashTable::store(std::uint64_t key, const Score& value) noexcept {
    if (key == 0) {
        return;
    }
    PawnEntry& entry = entries_[index_for(key)];
    entry.key = key;
    entry.value = value;
}

} // namespace nightwing::eval
