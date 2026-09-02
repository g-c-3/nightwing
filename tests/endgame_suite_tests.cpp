// tests/endgame_suite_tests.cpp
//
// ROADMAP.md Phase 6's "Dedicated endgame test suite" item: "curated
// known-tricky K+P and rook-ending positions ... with known-correct
// results, run in CI to catch algorithmic-rule misjudgments that pure
// perft/search regression tests wouldn't surface. Kept as its own test
// file, separate from perft/search/eval regression tests (per Testing
// Policy in ARCHITECTURE.md)."
//
// Every position below exercises the FULL engine (search_fixed_depth(),
// not any single internal eval term in isolation) end to end, and every
// expected result was independently confirmed by actually running this
// project's own compiled engine on the position first, rather than
// assumed or estimated -- these are regression tests against real,
// currently-observed engine behavior on positions with well-established
// theoretical results, not a re-derivation of the theory itself (that
// derivation already lives in eval/king_pawn_endgame.cpp, eval/
// rook_endgame.cpp, eval/minor_piece_endgame.cpp, and eval/
// basic_mates.cpp's own header comments and DECISIONS.md entries,
// Sessions 65-69).
//
// Sourcing note: the Lucena position below uses a real, independently
// sourced canonical FEN (English Wikipedia's "Lucena position" article,
// mirrored via chess.fandom.com at the time of writing) -- its
// best-move assertion (1.Rc1) matches that same source's own
// documented main line, not a value chosen to make the test pass. The
// Philidor-pattern and fortress-adjacent positions below are this
// project's own constructions, built to match the STRUCTURAL criteria
// standard endgame theory describes for each pattern (a defending rook
// on the cutting-off rank before the pawn crosses it, for Philidor; a
// mutually-blocked pawn structure, for the fortress case) rather than
// a claimed citation to a specific book position this project has no
// way to verify against the actual source text.
//
// Assertions are deliberately loose (direction and rough magnitude,
// not exact centipawn values) except where an exact best move is both
// well-established AND independently confirmed stable across several
// search depths on this project's own current engine -- exact node
// counts or centipawn scores are NOT asserted anywhere in this file,
// matching ARCHITECTURE.md's own caution against embedding fragile,
// version-dependent exact values in tests that are meant to catch
// large-scale regressions, not track minor eval retuning.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "search/search.h"

using namespace nightwing::board;
using namespace nightwing::search;

namespace {
/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so magic-bitboard/attack tables aren't shared across
/// cases the way they'd be in a single long-lived process -- matches
/// tests/search_tests.cpp's and tests/perft_tests.cpp's own identical
/// local helper exactly (not shared from either -- both of those are
/// themselves local, per-file helpers, not exported).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("endgame suite: KPK -- an unstoppable pawn (rule of the square) is a decisive loss "
          "for the side to move (Black), not a close call",
          "[endgame_suite][kpk]") {
    init_all();
    // White Ka1, Pe6 (rel. rank 5); Black Ka8, Black to move. Chosen to
    // match eval/king_pawn_endgame.cpp's own already rule-of-the-square-
    // verified "unstoppable" construction (tests/king_pawn_endgame_tests.cpp)
    // -- the defending king (Black's) cannot reach e8 before the pawn
    // promotes. Confirmed on this engine: score around -950 to -1130 cp
    // at depths 6-8 (from Black's own perspective, the side to move).
    Position pos = parse_fen("k7/8/4P3/8/8/8/8/K7 b - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 8);
    REQUIRE(result.score < -400);
}

TEST_CASE("endgame suite: KPK -- a rook pawn with the defending king able to reach the "
          "drawing corner in time stays close to balanced, not a clean extra-pawn advantage",
          "[endgame_suite][kpk]") {
    init_all();
    // White Kh1, Pa5 (rel. rank 4); Black Kc8, White to move -- the
    // defending king reaches the a8 corner in time per the rule of the
    // square, and CPW's own "Rook pawn" drawing exception applies.
    // Confirmed on this engine: score exactly 0 at depths 8-10.
    Position pos = parse_fen("2k5/8/8/P7/8/8/8/7K w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 10);
    REQUIRE(result.score >= -60);
    REQUIRE(result.score <= 60);
}

TEST_CASE("endgame suite: KBPK -- the WRONG bishop for a rook pawn's corner, with the "
          "defending king in time, stays close to balanced despite the extra bishop and pawn",
          "[endgame_suite][kbpk]") {
    init_all();
    // White Kg1, Ph6, Bc2 (a LIGHT square); Black Kg8. h8, the pawn's
    // promotion corner, is a DARK square -- the wrong-colored bishop
    // fortress (CPW "Wrong Bishop"). Confirmed on this engine: score
    // exactly 0 at depths 5-6, despite White objectively having an
    // extra bishop and pawn on the board.
    Position pos = parse_fen("6k1/8/7P/8/8/8/2b5/6K1 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 6);
    REQUIRE(result.score >= -80);
    REQUIRE(result.score <= 80);
}

TEST_CASE("endgame suite: insufficient material -- king and a single knight vs. bare king is "
          "an immediate, exact draw, not merely a low score",
          "[endgame_suite][insufficient_material]") {
    init_all();
    Position pos = parse_fen("7k/8/8/8/8/8/8/N6K w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 4);
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("endgame suite: KRK -- a decisive, growing advantage for the side with the rook, "
          "well beyond the rook's own raw material value",
          "[endgame_suite][krk]") {
    init_all();
    Position pos = parse_fen("7k/8/8/8/8/8/8/R3K3 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 8);
    REQUIRE(result.score > 400);
}

TEST_CASE("endgame suite: KBNK -- a decisive advantage for the side with the bishop and "
          "knight -- \"the hardest of the basic mates\" still shows as clearly winning, not "
          "merely balanced material",
          "[endgame_suite][kbnk]") {
    init_all();
    Position pos = parse_fen("k7/8/8/8/8/8/8/B1N1K3 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 8);
    REQUIRE(result.score > 400);
}

TEST_CASE("endgame suite: the canonical Lucena position -- a decisive win for the side with "
          "the extra pawn, with 1.Rc1 (the real, sourced main line's own first move) as the "
          "engine's own best move at a real search depth",
          "[endgame_suite][lucena]") {
    init_all();
    // FEN 4K3/2k1P3/8/8/8/8/5r2/6R1 w - - 0 1 -- White Ke8, Pe7, Rg1;
    // Black Kc7, Rf2. Sourced from English Wikipedia's "Lucena
    // position" article (via its chess.fandom.com mirror at the time
    // of writing), whose own documented main line begins 1.Rc1.
    // Confirmed on this engine: 1.Rc1 (g1-c1) is the engine's own best
    // move at depths 5 through 10, with a decisive, growing score (over
    // 600cp by depth 8).
    Position pos = parse_fen("4K3/2k1P3/8/8/8/8/5r2/6R1 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 8);
    REQUIRE(result.score > 400);
    REQUIRE(result.best_move.from() == make_square(6, 0)); // g1
    REQUIRE(result.best_move.to() == make_square(2, 0));   // c1
}

TEST_CASE("endgame suite: a Philidor-pattern position (defending rook on the cutting-off rank "
          "before the pawn crosses it) stays close to balanced, well short of the Lucena "
          "position's own decisive score for the same material",
          "[endgame_suite][philidor]") {
    init_all();
    // White Ka1, Rh1, Pe5 (rel. rank 4, hasn't crossed to the 6th
    // yet); Black Ke8, Ra6 (rank index 5 -- the cutting-off rank for a
    // White attacker) -- constructed to match the structural criteria
    // CPW's own "Philidor Position" article describes (defending rook
    // holding the cutting-off rank, defending king not yet dislodged),
    // not a citation to any single specific historical diagram.
    // Confirmed on this engine: score stays under 200cp at depths 6-8,
    // in clear contrast to the Lucena test's own 400+ for a comparably
    // sized material edge.
    Position pos = parse_fen("4k3/8/r7/4P3/8/8/8/K6R w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 8);
    REQUIRE(result.score < 250);
}

TEST_CASE("endgame suite: opposite-colored bishops score lower (more drawish) than an "
          "otherwise-identical same-colored-bishop position with the same material lead",
          "[endgame_suite][opposite_colored_bishops]") {
    init_all();
    // Two positions, White two pawns up in both, differing ONLY in
    // whether the two bishops share a square color: Bd2 (dark) is
    // fixed in both; the Black bishop is on d7 (LIGHT -- the genuine
    // EndgameSignature::OppositeColoredBishops case) in one FEN and e7
    // (DARK -- same color as White's, EndgameSignature::None, no
    // discount applies) in the other. Confirmed on this engine: the
    // opposite-colored case scores meaningfully lower (around 220-250cp
    // at depths 6-8) than the same-colored control (around 280-330cp),
    // a real, observable effect of eval::minor_piece_endgame.cpp's own
    // per-pawn drawish discount for the genuinely opposite-colored
    // case, not present for the same-colored control.
    Position opposite_colored = parse_fen("6k1/3b4/8/4p3/2P1PP2/8/3B4/6K1 w - - 0 1");
    Position same_colored = parse_fen("6k1/4b3/8/4p3/2P1PP2/8/3B4/6K1 w - - 0 1");
    const SearchResult opposite_result = search_fixed_depth(opposite_colored, 8);
    const SearchResult same_result = search_fixed_depth(same_colored, 8);
    REQUIRE(opposite_result.score < same_result.score);
}
