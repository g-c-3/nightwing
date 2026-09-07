// tests/contempt_tests.cpp
//
// Unit tests for the `contempt_cp` parameter added to
// search::search_fixed_depth()/search::search_iterative_deepening()
// (search/search.h) -- ROADMAP.md Phase 8's "Contempt / draw score
// adjustment (optional)" item. See search.cpp's own
// contempt_draw_score() doc comment (just above negamax()) for the
// full sign-convention derivation these tests exercise directly.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "search/search.h"

using namespace nightwing::board;
using namespace nightwing::search;

namespace {
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Identical to tests/search_tests.cpp's own play_move() helper --
/// deliberately duplicated rather than shared (this codebase's own
/// established per-test-file convention for small fixtures like this).
void play_move(Position& pos, Square from, Square to, std::vector<std::uint64_t>* history) {
    MoveList legal;
    generate_legal_moves(pos, legal);
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].from() == from && legal[i].to() == to) {
            history->push_back(pos.zobrist_hash);
            UndoInfo undo;
            make_move(pos, legal[i], undo);
            return;
        }
    }
    FAIL("play_move: no legal move found from the requested squares");
}
} // namespace

TEST_CASE("search_fixed_depth: Contempt 0 (default) leaves an already-stalemated position's "
          "score exactly at kDrawScore -- the pre-existing behavior",
          "[search][contempt]") {
    init_all();
    // Same classic K+Q vs K stalemate as tests/search_tests.cpp's own
    // "an already-stalemated position returns a null move and a draw
    // score" test -- Black to move, no legal moves, not in check.
    Position pos = parse_fen("7k/5Q2/7K/8/8/8/8/8 b - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("search_fixed_depth: a positive Contempt penalizes an immediate stalemate for "
          "whichever side is actually to move there",
          "[search][contempt]") {
    init_all();
    Position pos = parse_fen("7k/5Q2/7K/8/8/8/8/8 b - - 0 1"); // Black to move, stalemated.
    const SearchResult result =
        search_fixed_depth(pos, 2, /*game_history=*/{}, /*material_weights=*/nullptr,
                            /*num_threads=*/1, kDefaultTTSizeMB, /*contempt_cp=*/50);
    REQUIRE(result.score == kDrawScore - 50);
}

TEST_CASE("search_fixed_depth: a negative Contempt REWARDS an immediate stalemate for whichever "
          "side is actually to move there -- the exact mirror of the positive case",
          "[search][contempt]") {
    init_all();
    Position pos = parse_fen("7k/5Q2/7K/8/8/8/8/8 b - - 0 1"); // Black to move, stalemated.
    const SearchResult result =
        search_fixed_depth(pos, 2, /*game_history=*/{}, /*material_weights=*/nullptr,
                            /*num_threads=*/1, kDefaultTTSizeMB, /*contempt_cp=*/-50);
    REQUIRE(result.score == kDrawScore + 50);
}

TEST_CASE("search_fixed_depth: Contempt's sign is relative to whichever side is actually to "
          "move at the root, not tied to a fixed color -- the same positive value produces the "
          "same penalty whether White or Black is the one facing the stalemate",
          "[search][contempt]") {
    init_all();
    // The exact color-swapped mirror of the fixture above: White king
    // h8, Black queen f7, Black king h6, White to move -- geometrically
    // identical stalemate, opposite colors and side to move.
    Position pos = parse_fen("7K/5q2/7k/8/8/8/8/8 w - - 0 1");
    const SearchResult result =
        search_fixed_depth(pos, 2, /*game_history=*/{}, /*material_weights=*/nullptr,
                            /*num_threads=*/1, kDefaultTTSizeMB, /*contempt_cp=*/50);
    REQUIRE(result.score == kDrawScore - 50);
}

TEST_CASE("search_fixed_depth: Contempt correctly propagates several plies deep through a real "
          "search, not just an immediate terminal position -- and a material-scale contempt "
          "value never overturns a genuinely much better line",
          "[search][contempt]") {
    init_all();
    // Identical fixture to tests/search_tests.cpp's own "a position
    // that already repeated once in game_history is recognized..."
    // test: White, down a whole queen (material score around -900 on
    // any non-repeating line), can force an immediate draw by
    // repetition via Kb1-a1 -- that move's own doc comment there has
    // the full construction rationale.
    Position pos = parse_fen("1k6/8/8/8/7q/8/8/1K6 w - - 0 1");
    std::vector<std::uint64_t> history;

    play_move(pos, make_square(1, 0), make_square(0, 0), &history); // Kb1-a1
    play_move(pos, make_square(1, 7), make_square(0, 7), &history); // Kb8-a8
    play_move(pos, make_square(0, 0), make_square(1, 0), &history); // Ka1-b1
    play_move(pos, make_square(0, 7), make_square(1, 7), &history); // Ka8-b8
    REQUIRE(history.size() == 4);

    // Even at kMaxContemptCp-scale contempt (100, deliberately at the
    // TOP of the range this feature allows -- src/uci/uci.cpp's own
    // kMaxContemptCp doc comment), a queen's worth of material (~900)
    // vastly dominates it: White still finds and prefers the very same
    // repetition, and the reported score is now exactly the contempt-
    // penalized draw score (-100), not some blend or an entirely
    // different evaluation -- direct, exact evidence that the
    // contempt adjustment applied at the repetition several plies deep
    // survived unchanged all the way back up to the root's own
    // reported score, exactly as negamax()'s own doc comment on this
    // parameter (search.cpp) says it should.
    const SearchResult penalized =
        search_fixed_depth(pos, 2, history, /*material_weights=*/nullptr, /*num_threads=*/1,
                            kDefaultTTSizeMB, /*contempt_cp=*/100);
    REQUIRE(penalized.score == kDrawScore - 100);
    REQUIRE_FALSE(penalized.best_move.is_null());
    REQUIRE(penalized.best_move.from() == make_square(1, 0)); // b1
    REQUIRE(penalized.best_move.to() == make_square(0, 0));   // a1

    // The mirror case: negative contempt makes the very same forced
    // draw actively MORE attractive (still, obviously, the engine's
    // own choice either way here, since it's the only non-catastrophic
    // option) -- the reported score reflects the boost by exactly the
    // same magnitude.
    const SearchResult rewarded =
        search_fixed_depth(pos, 2, history, /*material_weights=*/nullptr, /*num_threads=*/1,
                            kDefaultTTSizeMB, /*contempt_cp=*/-100);
    REQUIRE(rewarded.score == kDrawScore + 100);
    REQUIRE_FALSE(rewarded.best_move.is_null());
    REQUIRE(rewarded.best_move.from() == make_square(1, 0)); // b1
    REQUIRE(rewarded.best_move.to() == make_square(0, 0));   // a1
}

TEST_CASE("search_iterative_deepening: Contempt 0 (default) is completely unaffected -- "
          "byte-for-byte identical SearchResult to never passing the parameter at all",
          "[search][contempt][id]") {
    init_all();
    Position pos1 = parse_fen("7k/5Q2/7K/8/8/8/8/8 b - - 0 1");
    Position pos2 = parse_fen("7k/5Q2/7K/8/8/8/8/8 b - - 0 1");

    const SearchResult without_param = search_iterative_deepening(pos1, /*max_depth=*/2);
    const SearchResult with_default_contempt =
        search_iterative_deepening(pos2, /*max_depth=*/2, /*time_limit_ms=*/0,
                                    /*game_history=*/{}, /*on_iteration=*/nullptr,
                                    /*material_weights=*/nullptr, /*num_threads=*/1,
                                    /*external_stop=*/nullptr, kDefaultTTSizeMB, /*multi_pv=*/1,
                                    /*soft_time_limit_ms=*/0, /*contempt_cp=*/0);

    REQUIRE(without_param.score == with_default_contempt.score);
    REQUIRE(without_param.best_move == with_default_contempt.best_move);
}
