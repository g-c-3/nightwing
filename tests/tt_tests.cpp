// tests/tt_tests.cpp
//
// Unit tests for src/search/tt.h — the transposition table's own
// correctness (store/probe round-trip, bound types, replacement scheme,
// mate-distance ply adjustment) in isolation from search.cpp's negamax()
// integration. negamax()'s own tests (search_tests.cpp) provide the
// end-to-end correctness net for the integration itself — these tests
// are deliberately narrower, exercising TranspositionTable's public API
// directly with synthetic keys (no real Zobrist hashes/positions needed
// for most of these — a TT doesn't care where a key came from).

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/move.h"
#include "search/tt.h"

using namespace nightwing::board;
using namespace nightwing::search;

TEST_CASE("TranspositionTable: probing an empty table is always a miss", "[tt]") {
    TranspositionTable tt(1);
    const TTProbeResult result = tt.probe(0x1234'5678'9abc'def0ULL, 0);
    REQUIRE_FALSE(result.hit);
}

TEST_CASE("TranspositionTable: store then probe round-trips score/move/depth/bound exactly", "[tt]") {
    TranspositionTable tt(1);
    const std::uint64_t key = 0xdead'beef'cafe'f00dULL;
    const Move move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush); // e2e4

    tt.store(key, /*depth=*/5, /*score=*/123, Bound::Exact, move, /*ply=*/2);
    const TTProbeResult result = tt.probe(key, /*ply=*/2);

    REQUIRE(result.hit);
    REQUIRE(result.score == 123);
    REQUIRE(result.move == move);
    REQUIRE(result.depth == 5);
    REQUIRE(result.bound == Bound::Exact);
}

TEST_CASE("TranspositionTable: a different key in the same bucket is still a miss", "[tt]") {
    // Deliberately tiny table (1 bucket = 4 entries) so both keys are
    // guaranteed to land in the same bucket regardless of their hash
    // bits, exercising the "not just any occupied slot, the matching
    // key" check in probe().
    TranspositionTable tt(0); // rounds up to a minimum 1-bucket table
    REQUIRE(tt.num_buckets() == 1);

    tt.store(111, 3, 50, Bound::Exact, Move(), 0);
    const TTProbeResult result = tt.probe(222, 0);
    REQUIRE_FALSE(result.hit);
}

TEST_CASE("TranspositionTable: Lower and Upper bounds round-trip their type", "[tt]") {
    TranspositionTable tt(1);
    tt.store(1, 4, 200, Bound::Lower, Move(), 0);
    tt.store(2, 4, -200, Bound::Upper, Move(), 0);

    REQUIRE(tt.probe(1, 0).bound == Bound::Lower);
    REQUIRE(tt.probe(2, 0).bound == Bound::Upper);
}

TEST_CASE("TranspositionTable: mate score stored at one ply reads back correctly at a different ply",
          "[tt]") {
    // A mate-for-the-side-to-move score found 2 plies into the search
    // (kMateScore - 2, per search.h's convention) is stored at ply=2.
    // Probing the SAME entry later from ply=5 (e.g. this exact position
    // reached via a different, longer move order/transposition) must
    // report the score re-relativized to ply=5 -- i.e. still "mate in
    // the same *absolute* number of plies from wherever this position
    // actually is," not the raw ply=2 number verbatim.
    TranspositionTable tt(1);
    constexpr int kMateScore = 32000; // mirrors search::kMateScore (search.h) -- not included here to
                                       // keep this test decoupled from search.cpp's module boundary.
    const int score_at_ply2 = kMateScore - 2;

    tt.store(42, 3, score_at_ply2, Bound::Exact, Move(), /*ply=*/2);

    const int score_probed_at_ply2 = tt.probe(42, /*ply=*/2).score;
    const int score_probed_at_ply5 = tt.probe(42, /*ply=*/5).score;

    REQUIRE(score_probed_at_ply2 == score_at_ply2);
    // Same stored (ply-independent) mate: probing from further from the
    // root reports a *smaller*-magnitude mate score (fewer plies until
    // mate, measured from wherever this new probe's root actually is) --
    // mirrors search.h's "shorter mates preferred" convention.
    REQUIRE(score_probed_at_ply5 < score_probed_at_ply2);
    REQUIRE(score_probed_at_ply5 == kMateScore - 5);
}

TEST_CASE("TranspositionTable: negative (losing) mate scores adjust the opposite direction", "[tt]") {
    TranspositionTable tt(1);
    constexpr int kMateScore = 32000;
    const int score_at_ply1 = -(kMateScore - 1);

    tt.store(7, 2, score_at_ply1, Bound::Exact, Move(), /*ply=*/1);

    const int score_probed_at_ply3 = tt.probe(7, /*ply=*/3).score;
    REQUIRE(score_probed_at_ply3 == -(kMateScore - 3));
}

TEST_CASE("TranspositionTable: replacement prefers an empty slot first", "[tt]") {
    TranspositionTable tt(0); // 1 bucket, 4 entries
    REQUIRE(tt.num_buckets() == 1);

    tt.store(1, 10, 0, Bound::Exact, Move(), 0); // fills 1 of 4 slots
    tt.store(2, 20, 0, Bound::Exact, Move(), 0); // an even deeper entry, still an empty slot available

    // Both should still be present -- the second store had an empty
    // slot to use and had no reason to evict the first.
    REQUIRE(tt.probe(1, 0).hit);
    REQUIRE(tt.probe(2, 0).hit);
}

TEST_CASE("TranspositionTable: once a bucket is full, the shallowest same-generation entry is evicted",
          "[tt]") {
    TranspositionTable tt(0); // 1 bucket, 4 entries
    REQUIRE(tt.num_buckets() == 1);

    tt.store(1, /*depth=*/1, 0, Bound::Exact, Move(), 0); // shallowest -- should be the eviction target
    tt.store(2, /*depth=*/5, 0, Bound::Exact, Move(), 0);
    tt.store(3, /*depth=*/5, 0, Bound::Exact, Move(), 0);
    tt.store(4, /*depth=*/5, 0, Bound::Exact, Move(), 0); // bucket now full

    tt.store(5, /*depth=*/5, 0, Bound::Exact, Move(), 0); // needs a slot; none empty

    REQUIRE_FALSE(tt.probe(1, 0).hit); // evicted: it was the shallowest
    REQUIRE(tt.probe(2, 0).hit);
    REQUIRE(tt.probe(3, 0).hit);
    REQUIRE(tt.probe(4, 0).hit);
    REQUIRE(tt.probe(5, 0).hit);
}

TEST_CASE("TranspositionTable: new_search() lets a stale entry be evicted regardless of its depth",
          "[tt]") {
    TranspositionTable tt(0); // 1 bucket, 4 entries
    REQUIRE(tt.num_buckets() == 1);

    // A deep entry from generation 0 fills the whole bucket (repeated on
    // purpose -- doesn't matter which of the 4 slots it ends up in).
    tt.store(1, /*depth=*/9, 0, Bound::Exact, Move(), 0);
    tt.store(2, /*depth=*/9, 0, Bound::Exact, Move(), 0);
    tt.store(3, /*depth=*/9, 0, Bound::Exact, Move(), 0);
    tt.store(4, /*depth=*/9, 0, Bound::Exact, Move(), 0);

    tt.new_search(); // generation 1 begins -- every existing entry is now "stale"

    // A shallow generation-1 store should still be able to claim a slot
    // by evicting a stale (older-generation) deep entry -- age trumps
    // depth for staleness, per store()'s documented replacement order.
    tt.store(5, /*depth=*/1, 0, Bound::Exact, Move(), 0);

    int hits = 0;
    for (std::uint64_t key : {1ULL, 2ULL, 3ULL, 4ULL}) {
        if (tt.probe(key, 0).hit) {
            ++hits;
        }
    }
    REQUIRE(hits == 3); // exactly one of the four old entries was evicted
    REQUIRE(tt.probe(5, 0).hit);
}

TEST_CASE("TranspositionTable: re-storing the same key with a shallower depth doesn't overwrite",
          "[tt]") {
    TranspositionTable tt(1);
    const Move deep_move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    const Move shallow_move(make_square(6, 0), make_square(5, 2), MoveFlag::Quiet); // g1f3

    tt.store(99, /*depth=*/6, 111, Bound::Exact, deep_move, 0);
    tt.store(99, /*depth=*/2, 222, Bound::Exact, shallow_move, 0); // shallower -- should be ignored

    const TTProbeResult result = tt.probe(99, 0);
    REQUIRE(result.depth == 6);
    REQUIRE(result.score == 111);
    REQUIRE(result.move == deep_move);
}

TEST_CASE("TranspositionTable: re-storing the same key with an equal-or-deeper depth does overwrite",
          "[tt]") {
    TranspositionTable tt(1);
    const Move first_move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    const Move second_move(make_square(6, 0), make_square(5, 2), MoveFlag::Quiet);

    tt.store(99, /*depth=*/4, 111, Bound::Exact, first_move, 0);
    tt.store(99, /*depth=*/4, 222, Bound::Upper, second_move, 0); // equal depth -- should refresh

    const TTProbeResult result = tt.probe(99, 0);
    REQUIRE(result.score == 222);
    REQUIRE(result.move == second_move);
    REQUIRE(result.bound == Bound::Upper);
}

TEST_CASE("TranspositionTable: a stale same-key entry is refreshed even with a shallower depth", "[tt]") {
    TranspositionTable tt(1);
    tt.store(99, /*depth=*/6, 111, Bound::Exact, Move(), 0);
    tt.new_search();
    tt.store(99, /*depth=*/1, 222, Bound::Exact, Move(), 0); // shallower, but old entry is stale now

    const TTProbeResult result = tt.probe(99, 0);
    REQUIRE(result.depth == 1);
    REQUIRE(result.score == 222);
}

TEST_CASE("TranspositionTable: clear() empties every entry and resets age", "[tt]") {
    TranspositionTable tt(1);
    tt.store(1, 5, 0, Bound::Exact, Move(), 0);
    REQUIRE(tt.probe(1, 0).hit);

    tt.clear();
    REQUIRE_FALSE(tt.probe(1, 0).hit);
}

TEST_CASE("TranspositionTable: size rounds down to a power of 2 bucket count", "[tt]") {
    // sizeof(TTBucket) is 64 bytes (one cache line, private to tt.cpp),
    // so a 1 MB request has room for 1 MB / 64 B = 16384 buckets exactly
    // -- already a power of 2, so this specifically checks the
    // "already exact" case rather than only the rounding-down case
    // covered by the num_buckets()==1 tables in the tests above.
    TranspositionTable tt(1);
    REQUIRE(tt.num_buckets() == 16384);
}
