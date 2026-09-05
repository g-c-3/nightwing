// tests/bench_tests.cpp
//
// Regression bench (ROADMAP.md Phase 4's last item): a small, fixed set
// of positions searched to a fixed depth via search_fixed_depth(), with
// resulting node counts printed to stdout in a simple, greppable format.
// Position set and depth now live in src/bench_positions.h, shared with
// the actual UCI/CLI `bench` command (src/uci/uci.cpp's run_bench(),
// ROADMAP.md Phase 8) so the two can never silently drift apart — see
// that header's own comment for the split of responsibilities.
//
// Deliberately NOT asserting exact node counts as pass/fail, unlike
// perft (where the count is a mathematically fixed ground truth): a
// search's node count is EXPECTED to change whenever a pruning/
// ordering/extension technique changes, often on purpose (fewer nodes
// for the same correct answer is usually the whole point of adding one).
// Asserting a fixed value here would make this test fail on every
// intentional improvement — the opposite of what a regression bench is
// for. Instead, this test asserts only genuine invariants (the search
// completes, returns a LEGAL best move, and reports real search effort)
// and prints node counts for a human — or a future session reading the
// CI log — to record in docs/SESSIONS.md, per that file's own
// convention going forward: a note each time a search-affecting change
// lands, on whether nodes went up or down and by roughly how much, as a
// concrete signal on whether that change actually helped rather than
// just a documented-sounding rationale.
//
// Depth is deliberately FIXED (not time-limited) and MODEST: fixed depth
// means fixed, reproducible node counts across different machines/CI
// runners — the entire point of a diffable bench — since a wall-clock
// or time-limited search would be dominated by hardware/scheduling
// noise, not by engine changes. kBenchDepth (6) is deep enough that
// Phase 4's depth-gated techniques are genuinely exercised (several
// only apply from around depth 3 up — see search.cpp's own
// kFutilityMaxDepth/kRazorMaxDepth/kProbCutMinDepth/kSingularMinDepth
// range), while staying fast enough to run on every CI push across all
// three platforms' Debug/ASan+UBSan builds too, not just Release.
//
// The position set is kept small and deliberately varied in CHARACTER,
// not just count: an opening (startpos), a tactically-loaded middlegame
// (Kiwipete — chosen because it's already this project's own perft
// reference position, so there's no new position to independently
// verify), a quieter middlegame, and a forced-mate endgame (the same
// externally-verified mate-in-3 fixture reused across every Phase 4
// regression test in search_tests.cpp). A change that helps one phase
// of the game at another's expense shows up as an uneven shift across
// the printed rows, not hidden inside a single aggregate number.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "bench_positions.h"
#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "search/search.h"

using namespace nightwing::board;
using namespace nightwing::search;
using nightwing::bench::kBenchDepth;
using nightwing::bench::kBenchPositions;

namespace {

// Every Catch2 TEST_CASE below runs as its own separate process
// invocation (catch_discover_tests registers each one as an individual
// CTest test), so magic-bitboard/attack tables aren't shared across
// cases the way they'd be in a single long-lived process -- this case
// must initialize them itself, matching every other test file's own
// local init_all() (e.g. search_tests.cpp).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

} // namespace

TEST_CASE("regression bench: fixed positions searched to a fixed depth, node counts printed for "
          "docs/SESSIONS.md tracking",
          "[bench]") {
    init_all();

    std::uint64_t total_nodes = 0;
    for (const auto& bench_pos : kBenchPositions) {
        Position pos = parse_fen(bench_pos.fen);
        const SearchResult result = search_fixed_depth(pos, kBenchDepth);

        // Genuine invariants, not a node-count assertion (see this
        // file's header comment for why): a fixed-depth search from a
        // legal, non-terminal position must complete having found SOME
        // legal reply (a null best_move here would mean the search
        // thought there were no legal moves in a position that has
        // them -- an actual bug, not just an uninteresting bench
        // result) and must have reported real search effort.
        REQUIRE_FALSE(result.best_move.is_null());
        REQUIRE(result.nodes > 0);
        REQUIRE(result.depth_completed == kBenchDepth);

        total_nodes += result.nodes;

        // Greppable, stable format for a human -- or a future session
        // reading this from the CI log -- kept plain rather than
        // matching Catch2's own reporter output style, so it survives a
        // reporter-format change untouched.
        std::cout << "BENCH " << bench_pos.name << " depth=" << kBenchDepth
                   << " nodes=" << result.nodes << " score=" << result.score
                   << " best_move=" << result.best_move.to_uci() << '\n';
    }
    std::cout << "BENCH TOTAL depth=" << kBenchDepth << " nodes=" << total_nodes << '\n';
}
