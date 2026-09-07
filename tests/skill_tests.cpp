// tests/skill_tests.cpp
//
// Unit tests for src/search/skill.h/.cpp -- ROADMAP.md Phase 8's
// "Skill level / strength limiting (optional, for practice/handicap
// play)" item (see skill.h's own header comment for the full design
// and attribution). Pure-logic tests over hand-built search::
// SearchResult vectors and a caller-supplied, explicitly-seeded
// std::mt19937_64 -- fully deterministic and reproducible, no
// dependency on board/search machinery actually running a real search
// at all (skill.h's own pick_skill_move()/skill_search_multipv() never
// touch board::Position or run any search themselves).

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <vector>

#include "board/move.h"
#include "search/search.h"
#include "search/skill.h"

using namespace nightwing::search;
using nightwing::board::Move;
using nightwing::board::MoveFlag;

namespace {

/// Builds a minimal SearchResult carrying just `score` and `best_move`
/// -- every field this module's own functions actually read. `square`
/// distinguishes each line's own Move so a test can tell which one was
/// ultimately picked (board::Square is a plain int, board/bitboard.h --
/// no real board position or legality is involved at all, matching
/// this file's own header comment on why these tests need none).
SearchResult make_line(int score, int square) {
    SearchResult result;
    result.score = score;
    result.best_move = Move(square, square + 1, MoveFlag::Quiet);
    return result;
}

} // namespace

TEST_CASE("skill_search_multipv: at kMaxSkillLevel (no limiting), the requested MultiPV count is "
          "returned completely unchanged, for every requested count",
          "[search][skill]") {
    REQUIRE(skill_search_multipv(kMaxSkillLevel, 1) == 1);
    REQUIRE(skill_search_multipv(kMaxSkillLevel, 5) == 5);
    REQUIRE(skill_search_multipv(kMaxSkillLevel, 100) == 100);
}

TEST_CASE("skill_search_multipv: below kMaxSkillLevel, returns max(requested, kSkillSearchMultiPv)",
          "[search][skill]") {
    REQUIRE(skill_search_multipv(10, 1) == kSkillSearchMultiPv);
    REQUIRE(skill_search_multipv(0, 1) == kSkillSearchMultiPv);
    // Already-larger request is never reduced.
    REQUIRE(skill_search_multipv(0, kSkillSearchMultiPv + 5) == kSkillSearchMultiPv + 5);
}

TEST_CASE("skill_search_multipv: an out-of-range skill_level is still handled consistently with "
          "is_skill_limited()'s own documented rule, not specially rejected",
          "[search][skill]") {
    // -5 < kMaxSkillLevel -> is_skill_limited() true -> limiting applies.
    REQUIRE(skill_search_multipv(-5, 1) == kSkillSearchMultiPv);
    // 999 is NOT < kMaxSkillLevel -> is_skill_limited() false -> unchanged.
    REQUIRE(skill_search_multipv(999, 1) == 1);
}

TEST_CASE("is_skill_limited: true below kMaxSkillLevel, false at or above it", "[search][skill]") {
    REQUIRE(is_skill_limited(0));
    REQUIRE(is_skill_limited(19));
    REQUIRE_FALSE(is_skill_limited(20));
    REQUIRE_FALSE(is_skill_limited(21));
}

TEST_CASE("pick_skill_move: at kMaxSkillLevel, always returns the best (index 0) line's move and "
          "draws NOTHING from the caller's rng",
          "[search][skill]") {
    const std::vector<SearchResult> lines{make_line(50, 0), make_line(40, 10), make_line(-100, 20)};

    std::mt19937_64 rng_used(12345);
    std::mt19937_64 rng_untouched(12345); // Identical seed, never passed to pick_skill_move().

    const Move chosen = pick_skill_move(lines, kMaxSkillLevel, rng_used);
    REQUIRE(chosen == lines.front().best_move);

    // If pick_skill_move() drew anything at all from rng_used, its
    // state would now differ from rng_untouched's -- draw one value
    // from each and confirm they still agree, proving zero consumption
    // (skill.h's own doc comment on this exact guarantee).
    REQUIRE(rng_used() == rng_untouched());
}

TEST_CASE("pick_skill_move: with fewer than 2 lines, returns the single line's move regardless "
          "of skill_level, and draws nothing from rng",
          "[search][skill]") {
    const std::vector<SearchResult> one_line{make_line(0, 0)};

    std::mt19937_64 rng_used(999);
    std::mt19937_64 rng_untouched(999);

    const Move chosen = pick_skill_move(one_line, /*skill_level=*/0, rng_used);
    REQUIRE(chosen == one_line.front().best_move);
    REQUIRE(rng_used() == rng_untouched());
}

TEST_CASE("pick_skill_move: an empty line list returns a null move defensively, without crashing",
          "[search][skill]") {
    const std::vector<SearchResult> no_lines;
    std::mt19937_64 rng(1);
    const Move chosen = pick_skill_move(no_lines, /*skill_level=*/0, rng);
    REQUIRE(chosen.is_null());
}

TEST_CASE("pick_skill_move: a gap far beyond kSkillNoiseCapCp (a near-mate line vs. ordinary "
          "alternatives) is NEVER overturned, even at the weakest skill level, across many "
          "fixed-seed trials",
          "[search][skill]") {
    const std::vector<SearchResult> lines{make_line(kMateScore - 500, 0), make_line(30, 10),
                                           make_line(-40, 20)};
    std::mt19937_64 rng(42);
    for (int trial = 0; trial < 200; ++trial) {
        const Move chosen = pick_skill_move(lines, kMinSkillLevel, rng);
        REQUIRE(chosen == lines.front().best_move);
    }
}

TEST_CASE("pick_skill_move: two genuinely close lines (well within kSkillNoiseCapCp) both get "
          "chosen at least once across many fixed-seed trials at the weakest skill level -- "
          "confirms real variability, not silent no-op behavior",
          "[search][skill]") {
    const std::vector<SearchResult> lines{make_line(10, 0), make_line(0, 10)};
    std::mt19937_64 rng(7);

    bool saw_first = false;
    bool saw_second = false;
    for (int trial = 0; trial < 200 && !(saw_first && saw_second); ++trial) {
        const Move chosen = pick_skill_move(lines, kMinSkillLevel, rng);
        if (chosen == lines[0].best_move) {
            saw_first = true;
        } else if (chosen == lines[1].best_move) {
            saw_second = true;
        }
    }
    REQUIRE(saw_first);
    REQUIRE(saw_second);
}

TEST_CASE("pick_skill_move: intermediate skill levels are strictly less likely than kMinSkillLevel "
          "to move away from the best line, for the same close pair of lines and the same seed "
          "stream",
          "[search][skill]") {
    // Same two close lines as the previous test; count how often the
    // WORSE (index 1) line gets picked at the weakest setting vs. a
    // middling one, using freshly, identically-seeded generators so the
    // two counts are directly comparable draw-for-draw.
    const std::vector<SearchResult> lines{make_line(10, 0), make_line(0, 10)};

    auto count_second_picked = [&](int skill_level) {
        std::mt19937_64 rng(2024);
        int count = 0;
        for (int trial = 0; trial < 500; ++trial) {
            if (pick_skill_move(lines, skill_level, rng) == lines[1].best_move) {
                ++count;
            }
        }
        return count;
    };

    const int weakest_count = count_second_picked(kMinSkillLevel);
    const int middling_count = count_second_picked(10);
    REQUIRE(weakest_count > middling_count);
}
