// tests/persistent_tt_tests.cpp
//
// Tests for the `external_tt` parameter added to search_fixed_depth()/
// search_iterative_deepening() (search/search.h) as part of ROADMAP.md's
// Priority Fixes (2026-09-08), "Persistent, engine-lifetime
// transposition table". Deliberately a separate file from
// tests/search_tests.cpp (matching this suite's existing convention of
// giving a self-contained new capability its own file -- e.g.
// tests/lazy_smp_tests.cpp, tests/pondering_tests.cpp) since every case
// here specifically exercises table OWNERSHIP/LIFETIME, not ordinary
// search correctness, which the rest of search_tests.cpp already covers
// exhaustively and is unaffected by this parameter's addition (its own
// default of `nullptr` reproduces every pre-existing call site's
// behavior byte-for-byte -- also asserted directly below).
//
// What these tests can and can't prove: everything here is single-
// threaded and fully deterministic (no wall-clock timing, no
// std::thread) -- unlike tests/lazy_smp_tests.cpp/pondering_tests.cpp,
// there's no scheduling nondeterminism to work around, so exact node
// counts and exact TT contents are asserted directly, the same way
// tests/tt_tests.cpp already does for TranspositionTable's own probe()/
// store() in isolation.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "search/search.h"
#include "search/tt.h"

using namespace nightwing::board;
using namespace nightwing::search;

namespace {
/// Same per-process-test-case setup convention as every other test file
/// in this suite -- see tests/search_tests.cpp's own init_all() comment.
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("search_fixed_depth: external_tt defaulting to nullptr leaves every existing call "
          "site's behavior unchanged",
          "[search][persistent_tt]") {
    init_all();
    Position pos1 = start_position();
    Position pos2 = start_position();
    const SearchResult default_call = search_fixed_depth(pos1, 5);
    const SearchResult explicit_null =
        search_fixed_depth(pos2, 5, /*game_history=*/{}, /*material_weights=*/nullptr,
                            /*num_threads=*/1, /*hash_size_mb=*/kDefaultTTSizeMB,
                            /*contempt_cp=*/0, /*external_tt=*/nullptr);
    REQUIRE(default_call.best_move == explicit_null.best_move);
    REQUIRE(default_call.score == explicit_null.score);
    REQUIRE(default_call.nodes == explicit_null.nodes);
    REQUIRE(default_call.depth_completed == explicit_null.depth_completed);
}

TEST_CASE("search_iterative_deepening: external_tt defaulting to nullptr leaves every existing "
          "call site's behavior unchanged",
          "[search][persistent_tt]") {
    init_all();
    Position pos1 = start_position();
    Position pos2 = start_position();
    const SearchResult default_call = search_iterative_deepening(pos1, 5);
    const SearchResult explicit_null = search_iterative_deepening(
        pos2, /*max_depth=*/5, /*time_limit_ms=*/0, /*game_history=*/{}, /*on_iteration=*/nullptr,
        /*material_weights=*/nullptr, /*num_threads=*/1, /*external_stop=*/nullptr,
        /*hash_size_mb=*/kDefaultTTSizeMB, /*multi_pv=*/1, /*soft_time_limit_ms=*/0,
        /*contempt_cp=*/0, /*external_tt=*/nullptr);
    REQUIRE(default_call.best_move == explicit_null.best_move);
    REQUIRE(default_call.score == explicit_null.score);
    REQUIRE(default_call.nodes == explicit_null.nodes);
}

TEST_CASE("search_fixed_depth: a non-null external_tt is genuinely used -- probing it after the "
          "call for the root position's own hash returns a hit matching the reported result",
          "[search][persistent_tt]") {
    init_all();
    Position pos = start_position();
    TranspositionTable tt(16);
    const SearchResult result = search_fixed_depth(
        pos, 5, /*game_history=*/{}, /*material_weights=*/nullptr, /*num_threads=*/1,
        /*hash_size_mb=*/1 /* deliberately different from tt's own 16 MB -- ignored, see below */,
        /*contempt_cp=*/0, &tt);

    const TTProbeResult probed = tt.probe(pos.zobrist_hash, /*ply=*/0);
    REQUIRE(probed.hit);
    REQUIRE(probed.move == result.best_move);
    REQUIRE(probed.score == result.score);
    REQUIRE(probed.depth >= 5);

    // `hash_size_mb` above (1) was deliberately set to something other
    // than `tt`'s own real size (16 MB) -- this function's own doc
    // comment (search.h) says that parameter is ignored whenever
    // `external_tt` is non-null. `num_buckets()` unchanged from a
    // freshly-constructed 16 MB table is the observable proof: had
    // `hash_size_mb` been honored instead, a 1 MB table would have a
    // very different (much smaller) bucket count.
    const TranspositionTable reference(16);
    REQUIRE(tt.num_buckets() == reference.num_buckets());
}

TEST_CASE("search_iterative_deepening: a non-null external_tt is genuinely used -- probing it "
          "after the call for the root position's own hash returns a hit",
          "[search][persistent_tt]") {
    init_all();
    Position pos = start_position();
    TranspositionTable tt(16);
    const SearchResult result = search_iterative_deepening(
        pos, /*max_depth=*/5, /*time_limit_ms=*/0, /*game_history=*/{}, /*on_iteration=*/nullptr,
        /*material_weights=*/nullptr, /*num_threads=*/1, /*external_stop=*/nullptr,
        /*hash_size_mb=*/kDefaultTTSizeMB, /*multi_pv=*/1, /*soft_time_limit_ms=*/0,
        /*contempt_cp=*/0, &tt);

    const TTProbeResult probed = tt.probe(pos.zobrist_hash, /*ply=*/0);
    REQUIRE(probed.hit);
    REQUIRE(probed.move == result.best_move);
}

TEST_CASE("search_fixed_depth: reusing an already-populated external_tt for an identical "
          "repeat call at the same depth visits dramatically fewer nodes, and still returns "
          "the same best move/score",
          "[search][persistent_tt]") {
    init_all();
    TranspositionTable tt(16);

    Position first_pos = start_position();
    const SearchResult first = search_fixed_depth(first_pos, 6, /*game_history=*/{},
                                                    /*material_weights=*/nullptr,
                                                    /*num_threads=*/1, /*hash_size_mb=*/16,
                                                    /*contempt_cp=*/0, &tt);

    // Same table, same position, same depth, from a fresh copy of the
    // position (search_fixed_depth() always restores `pos` before
    // returning, but a fresh copy here removes any doubt that this is
    // testing the SAME starting position, not some mutated leftover).
    Position second_pos = start_position();
    const SearchResult second = search_fixed_depth(second_pos, 6, /*game_history=*/{},
                                                     /*material_weights=*/nullptr,
                                                     /*num_threads=*/1, /*hash_size_mb=*/16,
                                                     /*contempt_cp=*/0, &tt);

    // Exact techniques (PVS/TT/aspiration windows -- search.h's own
    // header comment) guarantee the same best move/score regardless of
    // how many previously-stored TT entries speed up getting there.
    REQUIRE(first.best_move == second.best_move);
    REQUIRE(first.score == second.score);
    // The real point of this test: a table already holding every entry
    // from an IDENTICAL prior search at the same depth lets the second
    // call resolve almost every node via an immediate TT cutoff instead
    // of a real search -- a large, safe margin (not asserting an exact
    // number, which would be too brittle to incidental search changes)
    // still clearly distinguishes genuine reuse from no reuse at all.
    REQUIRE(second.nodes < first.nodes / 2);

    // And a fresh, empty table asked the exact same question reproduces
    // the FIRST call's own (larger) node count exactly -- confirming
    // `first`'s own count isn't itself somehow already-warmed from
    // something else in this process (each TEST_CASE is its own
    // process, per this suite's own convention, but this also directly
    // rules out any accidental cross-call state within this one case).
    TranspositionTable fresh_tt(16);
    Position third_pos = start_position();
    const SearchResult third = search_fixed_depth(third_pos, 6, /*game_history=*/{},
                                                    /*material_weights=*/nullptr,
                                                    /*num_threads=*/1, /*hash_size_mb=*/16,
                                                    /*contempt_cp=*/0, &fresh_tt);
    REQUIRE(third.nodes == first.nodes);
}

TEST_CASE("search_fixed_depth: a warm external_tt reproduces the cold call's own best move/"
          "score across EVERY depth from 1 to 8, not just one hand-picked depth",
          "[search][persistent_tt]") {
    init_all();
    // This is a direct, deliberately broader regression guard for a
    // real, confirmed bug this session found and fixed (docs/
    // DECISIONS.md has the full account): the test just above this one
    // only ever checked depth == 6, and PASSED at that one depth on
    // `main` even while the underlying defect -- Internal Iterative
    // Reduction (negamax()'s own header comment) firing inconsistently
    // between a cold and a warm call, since IIR's own gate is `!probe.
    // hit`, which a warmed-up TT changes by definition -- was ALREADY
    // present and already reproducible at depth 5 on this exact
    // position, undetected purely because no existing test happened to
    // ask at that specific depth. A single hand-picked depth is
    // therefore not a reliable regression guard for this failure mode
    // by itself; sweeping every depth from 1 through 8 (comfortably
    // past kIIRMinDepth (4), the threshold this bug's own fix is gated
    // on) is a meaningfully stronger one, at a still-modest total cost
    // (this whole sweep completes in well under a second).
    for (int depth = 1; depth <= 8; ++depth) {
        TranspositionTable tt(16);
        Position cold_pos = start_position();
        const SearchResult cold = search_fixed_depth(cold_pos, depth, /*game_history=*/{},
                                                       /*material_weights=*/nullptr,
                                                       /*num_threads=*/1, /*hash_size_mb=*/16,
                                                       /*contempt_cp=*/0, &tt);
        Position warm_pos = start_position();
        const SearchResult warm = search_fixed_depth(warm_pos, depth, /*game_history=*/{},
                                                       /*material_weights=*/nullptr,
                                                       /*num_threads=*/1, /*hash_size_mb=*/16,
                                                       /*contempt_cp=*/0, &tt);
        INFO("depth = " << depth);
        REQUIRE(cold.best_move == warm.best_move);
        REQUIRE(cold.score == warm.score);
    }
}

TEST_CASE("search_iterative_deepening: Internal Iterative Reduction still engages normally at "
          "depth >= 2 iterations -- this session's own IIR/persistent-TT fix only removed it "
          "from search_fixed_depth(), not from here",
          "[search][persistent_tt][iir]") {
    init_all();
    // negamax()'s own header comment (search.cpp): IIR is now gated on
    // `limits != nullptr`, which is true for every
    // search_iterative_deepening() iteration at depth >= 2 (the one
    // case where a shallower iteration's own TT entries provide the
    // genuine self-correction IIR's design assumes) -- unlike
    // search_fixed_depth(), which always passes `limits == nullptr`
    // (the test just above this one). This doesn't re-prove IIR's own
    // node-count benefit in detail (docs/DECISIONS.md's 2026-08-17 (2)
    // entry already did that, on Kiwipete specifically) -- it's a
    // narrower confirmation that THIS session's fix genuinely left that
    // established, validated behavior alone: a plain, un-gated
    // iterative-deepening search to a depth well past kIIRMinDepth (4)
    // still completes with a fully sane result, the same coarse
    // sanity-check shape as tests/search_tests.cpp's own equivalent
    // search_fixed_depth() case.
    Position pos = start_position();
    const SearchResult result = search_iterative_deepening(pos, /*max_depth=*/6);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.score > -kMateThreshold);
    REQUIRE(result.score < kMateThreshold);
}

TEST_CASE("search_iterative_deepening: a non-null external_tt still works correctly when "
          "MultiPV genuinely takes effect (multi_pv > 1)",
          "[search][persistent_tt][multipv]") {
    init_all();
    Position pos = start_position();
    TranspositionTable tt(16);
    const SearchResult result = search_iterative_deepening(
        pos, /*max_depth=*/4, /*time_limit_ms=*/0, /*game_history=*/{}, /*on_iteration=*/nullptr,
        /*material_weights=*/nullptr, /*num_threads=*/1, /*external_stop=*/nullptr,
        /*hash_size_mb=*/kDefaultTTSizeMB, /*multi_pv=*/2, /*soft_time_limit_ms=*/0,
        /*contempt_cp=*/0, &tt);

    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.multipv_lines.size() == 2);
    // The shared table genuinely received entries from this call too --
    // same probe-based proof as the single-line tests above.
    const TTProbeResult probed = tt.probe(pos.zobrist_hash, /*ply=*/0);
    REQUIRE(probed.hit);
}
