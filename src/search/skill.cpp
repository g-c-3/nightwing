// src/search/skill.cpp
//
// See skill.h.

#include "search/skill.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "search/search.h"

namespace nightwing::search {

int skill_search_multipv(int skill_level, int requested_multi_pv) noexcept {
    if (!is_skill_limited(skill_level)) {
        return requested_multi_pv;
    }
    return std::max(requested_multi_pv, kSkillSearchMultiPv);
}

board::Move pick_skill_move(const std::vector<SearchResult>& multipv_lines, int skill_level,
                             std::mt19937_64& rng) {
    if (multipv_lines.empty()) {
        return board::Move{}; // Defensive only -- see this function's own doc comment (skill.h).
    }
    if (!is_skill_limited(skill_level) || multipv_lines.size() < 2) {
        // No limiting requested, or nothing to choose between -- return
        // the engine's own best-known line untouched, and draw nothing
        // from `rng` at all (skill.h's own doc comment on why this
        // matters).
        return multipv_lines.front().best_move;
    }

    const int clamped_level = std::clamp(skill_level, kMinSkillLevel, kMaxSkillLevel);
    const double weakness = static_cast<double>(kMaxSkillLevel - clamped_level) /
                             static_cast<double>(kMaxSkillLevel - kMinSkillLevel);
    const int noise_amplitude = static_cast<int>(std::lround(weakness * kSkillNoiseCapCp));

    if (noise_amplitude <= 0) {
        // Only reachable if a future caller passes a `skill_level`
        // strictly less than kMaxSkillLevel that nonetheless rounds to
        // zero weakness -- not possible with today's integer 0-20
        // range, but handled explicitly rather than relying on the
        // distribution below tolerating a zero-width range.
        return multipv_lines.front().best_move;
    }

    std::uniform_int_distribution<int> noise(-noise_amplitude, noise_amplitude);

    int best_adjusted_score = std::numeric_limits<int>::min();
    board::Move chosen = multipv_lines.front().best_move;
    for (const SearchResult& line : multipv_lines) {
        const int adjusted_score = line.score + noise(rng);
        if (adjusted_score > best_adjusted_score) {
            best_adjusted_score = adjusted_score;
            chosen = line.best_move;
        }
    }
    return chosen;
}

} // namespace nightwing::search
