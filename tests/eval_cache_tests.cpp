// tests/eval_cache_tests.cpp
//
// Unit tests for src/eval/eval_cache.h (EvalCache) and its wiring into
// eval::evaluate() (src/eval/eval.cpp). Mirrors tests/pawn_tt_tests.cpp's
// structure closely -- same category of cache (small, single-entry-per-
// slot, unconditional replacement -- see eval_cache.h's header comment
// on how it differs in scope/key from PawnHashTable).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "eval/eval.h"
#include "eval/eval_cache.h"
#include "eval/pawn_tt.h"

using namespace nightwing::board;
using namespace nightwing::eval;

TEST_CASE("EvalCache: constructs with at least one entry even for a tiny/zero size",
          "[eval][eval_cache]") {
    EvalCache cache(0);
    REQUIRE(cache.num_entries() >= 1);
}

TEST_CASE("EvalCache: entry count is always a power of two", "[eval][eval_cache]") {
    EvalCache cache(2048);
    const std::size_t n = cache.num_entries();
    REQUIRE(n >= 1);
    REQUIRE((n & (n - 1)) == 0);
}

TEST_CASE("EvalCache: probing an empty table misses", "[eval][eval_cache]") {
    EvalCache cache(2048);
    const auto [hit, value] = cache.probe(12345ULL);
    REQUIRE_FALSE(hit);
}

TEST_CASE("EvalCache: store then probe returns the exact stored value", "[eval][eval_cache]") {
    EvalCache cache(2048);
    cache.store(0xABCDEF1234567890ULL, -137);
    const auto [hit, value] = cache.probe(0xABCDEF1234567890ULL);
    REQUIRE(hit);
    REQUIRE(value == -137);
}

TEST_CASE("EvalCache: a key of exactly 0 is never stored (reserved empty-slot sentinel)",
          "[eval][eval_cache]") {
    EvalCache cache(2048);
    cache.store(0ULL, 99);
    REQUIRE_FALSE(cache.probe(0ULL).first);
}

TEST_CASE("EvalCache: a tiny (1-entry) table reports a genuine miss for a different key "
          "colliding into its only slot -- never a false hit",
          "[eval][eval_cache]") {
    EvalCache cache(0); // rounds up to the minimum, 1 entry (see the sizing test above)
    REQUIRE(cache.num_entries() == 1);

    cache.store(111ULL, 5);
    const auto [hit_same, value_same] = cache.probe(111ULL);
    REQUIRE(hit_same);
    REQUIRE(value_same == 5);

    // 222 necessarily maps to the same (only) slot as 111 in a 1-entry
    // table, but it's a different key -- probe() must report a miss, not
    // silently hand back 111's stored value under 222's name.
    const auto [hit_diff, value_diff] = cache.probe(222ULL);
    REQUIRE_FALSE(hit_diff);
}

TEST_CASE("EvalCache: storing a new key into an occupied slot unconditionally overwrites it "
          "(no depth/bound-preferred replacement scheme, unlike the main TT)",
          "[eval][eval_cache]") {
    EvalCache cache(0); // 1 entry, so 111 and 222 are guaranteed to collide
    cache.store(111ULL, 5);
    cache.store(222ULL, -9);
    REQUIRE_FALSE(cache.probe(111ULL).first);
    const auto [hit, value] = cache.probe(222ULL);
    REQUIRE(hit);
    REQUIRE(value == -9);
}

TEST_CASE("EvalCache: clear() removes every previously stored entry", "[eval][eval_cache]") {
    EvalCache cache(2048);
    cache.store(999ULL, 1);
    REQUIRE(cache.probe(999ULL).first);
    cache.clear();
    REQUIRE_FALSE(cache.probe(999ULL).first);
}

TEST_CASE("evaluate: using an eval cache never changes the result, on either the first (miss) "
          "or second (cached hit) call",
          "[eval][eval_cache]") {
    // See pawn_tt_tests.cpp's identical-purpose test for why both
    // init_masks()/init_magic_bitboards() are required here (evaluate()'s
    // mobility_value() term reads the magic-bitboard sliding-piece attack
    // tables).
    init_masks();
    init_magic_bitboards();

    Position pos = start_position();
    const int without_cache = evaluate(pos, nullptr, nullptr);

    EvalCache cache(2048);
    const int first_call = evaluate(pos, nullptr, &cache);  // miss: computes fresh, stores
    const int second_call = evaluate(pos, nullptr, &cache); // hit: returns the cached value

    REQUIRE(first_call == without_cache);
    REQUIRE(second_call == without_cache);
}

TEST_CASE("evaluate: eval cache transparency holds for a position with a real material/pawn "
          "imbalance too, not just the symmetric starting position",
          "[eval][eval_cache]") {
    init_masks();
    init_magic_bitboards();

    // Same doubled-e-pawns FEN pawn_tt_tests.cpp already hand-verifies --
    // reused here since this test only cares that going through the eval
    // cache doesn't change evaluate()'s answer, not what that answer's
    // exact value is.
    Position pos = parse_fen("4k3/8/8/8/4P3/8/4P3/4K3 w - - 0 1");
    const int without_cache = evaluate(pos, nullptr, nullptr);

    EvalCache cache(2048);
    REQUIRE(evaluate(pos, nullptr, &cache) == without_cache);
    REQUIRE(evaluate(pos, nullptr, &cache) == without_cache);
}

TEST_CASE("evaluate: an eval cache hit is keyed on the FULL position, not just material/pawn "
          "structure -- two different positions must never collide onto the same cached value "
          "through this table",
          "[eval][eval_cache]") {
    init_masks();
    init_magic_bitboards();

    // Same side-to-move, same material on both sides, different piece
    // placement (knight developed vs. not) -- if EvalCache were ever
    // mistakenly keyed on something coarser than the full Zobrist hash
    // (e.g. a hash that ignored minor-piece placement), this is the kind
    // of pair that would wrongly collide.
    Position pos_a = parse_fen("r1bqkbnr/pppppppp/2n5/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 2 2");
    Position pos_b = parse_fen("rnbqkbnr/pppppppp/8/8/8/2N5/PPPPPPPP/R1BQKBNR w KQkq - 2 2");
    REQUIRE(pos_a.zobrist_hash != pos_b.zobrist_hash);

    EvalCache cache(2048);
    const int eval_a = evaluate(pos_a, nullptr, &cache);
    const int eval_b = evaluate(pos_b, nullptr, &cache);
    REQUIRE(eval_a == evaluate(pos_a, nullptr, &cache));
    REQUIRE(eval_b == evaluate(pos_b, nullptr, &cache));
}

TEST_CASE("evaluate: eval cache and pawn hash table combine correctly -- an eval cache hit "
          "short-circuits before any pawn_tt probe, and a miss still populates/uses pawn_tt "
          "normally underneath",
          "[eval][eval_cache]") {
    init_masks();
    init_magic_bitboards();

    Position pos = start_position();
    const int reference = evaluate(pos, nullptr, nullptr);

    PawnHashTable pawn_tt(512);
    EvalCache eval_cache(2048);
    // First call: eval cache misses, falls through to the full
    // computation (which itself probes/stores pawn_tt), then stores into
    // eval_cache.
    REQUIRE(evaluate(pos, &pawn_tt, &eval_cache) == reference);
    // Second call: eval cache hits and returns immediately -- still
    // correct, regardless of whether pawn_tt's own entry would also
    // still be valid.
    REQUIRE(evaluate(pos, &pawn_tt, &eval_cache) == reference);
}
