// tests/pawn_tt_tests.cpp
//
// Unit tests for src/eval/pawn_tt.h (PawnHashTable) and its wiring into
// eval::evaluate() (src/eval/eval.cpp).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "eval/pawn_tt.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

TEST_CASE("PawnHashTable: constructs with at least one entry even for a tiny/zero size",
          "[eval][pawn_tt]") {
    PawnHashTable tt(0);
    REQUIRE(tt.num_entries() >= 1);
}

TEST_CASE("PawnHashTable: entry count is always a power of two", "[eval][pawn_tt]") {
    PawnHashTable tt(512);
    const std::size_t n = tt.num_entries();
    REQUIRE(n >= 1);
    REQUIRE((n & (n - 1)) == 0);
}

TEST_CASE("PawnHashTable: probing an empty table misses", "[eval][pawn_tt]") {
    PawnHashTable tt(512);
    const auto [hit, value] = tt.probe(12345ULL);
    REQUIRE_FALSE(hit);
}

TEST_CASE("PawnHashTable: store then probe returns the exact stored value", "[eval][pawn_tt]") {
    PawnHashTable tt(512);
    const Score stored{17, -23};
    tt.store(0xABCDEF1234567890ULL, stored);
    const auto [hit, value] = tt.probe(0xABCDEF1234567890ULL);
    REQUIRE(hit);
    REQUIRE(value == stored);
}

TEST_CASE("PawnHashTable: a key of exactly 0 is never stored (reserved empty-slot sentinel)",
          "[eval][pawn_tt]") {
    PawnHashTable tt(512);
    tt.store(0ULL, Score{99, 99});
    REQUIRE_FALSE(tt.probe(0ULL).first);
}

TEST_CASE("PawnHashTable: a tiny (1-entry) table reports a genuine miss for a different key "
          "colliding into its only slot -- never a false hit",
          "[eval][pawn_tt]") {
    PawnHashTable tt(0); // rounds up to the minimum, 1 entry (see the sizing test above)
    REQUIRE(tt.num_entries() == 1);

    tt.store(111ULL, Score{5, 5});
    const auto [hit_same, value_same] = tt.probe(111ULL);
    REQUIRE(hit_same);
    REQUIRE(value_same == Score{5, 5});

    // 222 necessarily maps to the same (only) slot as 111 in a 1-entry
    // table, but it's a different key -- probe() must report a miss,
    // not silently hand back 111's stored value under 222's name.
    const auto [hit_diff, value_diff] = tt.probe(222ULL);
    REQUIRE_FALSE(hit_diff);
}

TEST_CASE("PawnHashTable: clear() removes every previously stored entry", "[eval][pawn_tt]") {
    PawnHashTable tt(512);
    tt.store(999ULL, Score{1, 1});
    REQUIRE(tt.probe(999ULL).first);
    tt.clear();
    REQUIRE_FALSE(tt.probe(999ULL).first);
}

TEST_CASE("evaluate: using a pawn hash table never changes the result, on either the first "
          "(miss) or second (cached hit) call",
          "[eval][pawn_tt]") {
    // init_magic_bitboards() is genuinely required as of eval/
    // mobility.h's mobility_value() term (docs/DECISIONS.md, ROADMAP.md
    // Phase 5's "Mobility eval" item): start_position() below has a full
    // complement of bishops/rooks/queens, and evaluate() now calls
    // board::bishop_attacks()/rook_attacks()/queen_attacks() for each of
    // them, which read the magic-bitboard tables this populates —
    // init_masks() alone was already required before this term existed
    // (pawn structure's own attack-table use) and remains required too.
    init_masks();
    init_magic_bitboards();

    Position pos = start_position();
    const int without_tt = evaluate(pos, nullptr);

    PawnHashTable tt(512);
    const int first_call = evaluate(pos, &tt);  // miss: computes fresh, stores
    const int second_call = evaluate(pos, &tt); // hit: returns the cached value

    REQUIRE(first_call == without_tt);
    REQUIRE(second_call == without_tt);
}

TEST_CASE("evaluate: pawn hash table transparency holds for a position with a real "
          "pawn-structure imbalance too, not just the symmetric starting position",
          "[eval][pawn_tt]") {
    // Not strictly required for THIS specific FEN (no bishops/rooks/
    // queens on it, so mobility_value()'s sliding-piece loops never
    // actually execute) -- included anyway for consistency with the
    // test just above and so this test stays correct if the position
    // is ever changed to include a slider, rather than relying on a
    // reader noticing it currently happens to be safe without it.
    init_masks();
    init_magic_bitboards();

    // White has doubled e-pawns (e2, e4), Black has none -- same shape
    // as tests/pawns_tests.cpp's doubled-pawn test, reused here since
    // it's already been hand-verified there; this test only cares that
    // going through the cache doesn't change evaluate()'s answer, not
    // what that answer's exact value is.
    Position pos = parse_fen("4k3/8/8/8/4P3/8/4P3/4K3 w - - 0 1");
    const int without_tt = evaluate(pos, nullptr);

    PawnHashTable tt(512);
    REQUIRE(evaluate(pos, &tt) == without_tt);
    REQUIRE(evaluate(pos, &tt) == without_tt);
}

TEST_CASE("evaluate: pawn_tt is never consulted (probed or stored) when a PawnsWeights "
          "override is supplied, even if a real PawnHashTable pointer is also passed -- the "
          "pawn_tt counterpart to eval_tests.cpp's own eval_cache-staleness tests for "
          "MaterialWeights/PsqtWeights/MobilityWeights/SpaceWeights/ThreatsWeights/"
          "KingSafetyWeights, applied to the OTHER cache evaluate() has (ROADMAP.md Tier 0 "
          "Step 8b, the fifth and final \"beyond PSQT\" term)",
          "[eval][pawn_tt][pawns][tuner]") {
    init_masks();
    init_magic_bitboards();

    // White has doubled e-pawns, same position as this file's own
    // "pawn hash table transparency" test above -- a real,
    // pawn-structure-sensitive position, not the symmetric (and
    // therefore pawn-structure-blind) starting position.
    Position pos = parse_fen("4k3/8/8/8/4P3/8/4P3/4K3 w - - 0 1");

    PawnHashTable tt(512);
    // Poison the table the same way eval_tests.cpp's own eval_cache
    // tests poison EvalCache: store an obviously-wrong Score under this
    // position's real pawn-structure key first, so a wrongly-consulted
    // hit is impossible to miss.
    const std::uint64_t pawn_key = compute_pawn_hash(pos);
    tt.store(pawn_key, Score{12345, 12345});

    const PawnsWeights weights = default_pawns_weights();
    const int result = evaluate(pos, &tt, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, &weights);
    // If pawn_tt had been wrongly consulted, the doubled-pawn penalty
    // component of the final score would instead reflect the poisoned
    // Score{12345, 12345} entry -- evaluate() would return a wildly
    // different (and, for any real position, essentially impossible)
    // value. Comparing against the equivalent call with pawn_tt =
    // nullptr confirms the poisoned entry played no role at all.
    REQUIRE(result == evaluate(pos, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                nullptr, nullptr, &weights));

    const auto [hit, cached] = tt.probe(pawn_key);
    REQUIRE(hit);
    REQUIRE(cached == Score{12345, 12345}); // untouched, not overwritten either
}
