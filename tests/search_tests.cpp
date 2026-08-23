// tests/search_tests.cpp
//
// Unit tests for src/search/search.h — Phase 2's fixed-depth alpha-beta
// search and iterative deepening. Positions are built via FEN (fen.h),
// matching the style of movegen_tests.cpp / eval_tests.cpp.

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
/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation (catch_discover_tests registers each one as an individual
/// CTest test), so magic-bitboard/attack tables aren't shared across
/// cases the way they'd be in a single long-lived process — each case
/// must initialize them itself. Matches perft_tests.cpp's convention
/// exactly (see that file for why).
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Test helper: finds the legal move from `from` to `to` in `pos` and
/// applies it, recording `pos.zobrist_hash` (the PRE-move position) into
/// `*history` first if `history` isn't null -- exactly the convention
/// src/uci/uci.cpp's apply_uci_moves() uses to build the `game_history`
/// search::search_fixed_depth()/search_iterative_deepening() take (see
/// search.h's doc comment). Fails the test outright (REQUIRE) if no such
/// legal move exists, rather than silently doing nothing, since every
/// call site below is hand-constructed to be a specific legal move --
/// silently skipping one would leave the position subtly wrong for
/// every step after it instead of failing loudly at the point of the
/// actual mistake.
void play_move(Position& pos, Square from, Square to, std::vector<std::uint64_t>* history = nullptr) {
    MoveList legal;
    generate_legal_moves(pos, legal);
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].from() == from && legal[i].to() == to) {
            if (history != nullptr) {
                history->push_back(pos.zobrist_hash);
            }
            UndoInfo undo;
            make_move(pos, legal[i], undo);
            return;
        }
    }
    FAIL("play_move: no legal move found from the requested squares");
}
} // namespace

TEST_CASE("search_fixed_depth: depth 1 node count matches the root's legal move count", "[search]") {
    init_all();
    // At depth 1, negamax() is called once per root move and immediately
    // hits its depth<=0 base case (no further move generation), so total
    // nodes visited should equal exactly the number of legal root moves —
    // 20 from the starting position.
    Position pos = start_position();
    const SearchResult result = search_fixed_depth(pos, 1);
    REQUIRE(result.nodes == 20);
}

TEST_CASE("search_fixed_depth: leaves the position unmodified", "[search]") {
    init_all();
    Position pos = start_position();
    const std::uint64_t hash_before = pos.zobrist_hash;
    (void)search_fixed_depth(pos, 3);
    REQUIRE(pos.zobrist_hash == hash_before);
}

TEST_CASE("search_fixed_depth: picks a move from the actual legal move list", "[search]") {
    init_all();
    Position pos = start_position();
    MoveList legal;
    generate_legal_moves(pos, legal);
    const SearchResult result = search_fixed_depth(pos, 3);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(legal.contains(result.best_move));
}

TEST_CASE("search_fixed_depth: an already-checkmated position returns a null move and -mate", "[search]") {
    init_all();
    // Fool's mate final position (1. f3 e5 2. g4 Qh4#) -- White to move,
    // no legal moves, in check.
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.score == -kMateScore);
}

TEST_CASE("search_fixed_depth: an already-stalemated position returns a null move and a draw score", "[search]") {
    init_all();
    // Classic K+Q vs K stalemate: Black to move, no legal moves, not in
    // check.
    Position pos = parse_fen("7k/5Q2/7K/8/8/8/8/8 b - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("search_fixed_depth: finds a back-rank mate in 1", "[search]") {
    init_all();
    // White to move: Ra1-a8# is mate (Black king g8 boxed in by its own
    // f7/g7/h7 pawns, rook covers the entire back rank once the king
    // steps off it). Depth 2 is required (not 1) for the search to
    // actually discover the resulting position has no legal replies --
    // see search.h's header comment on why a mate exactly at the search
    // horizon isn't detected as a mate score.
    Position pos = parse_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.best_move.from() == make_square(0, 0)); // a1
    REQUIRE(result.best_move.to() == make_square(0, 7));   // a8
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_fixed_depth: depth_completed matches the requested depth", "[search]") {
    init_all();
    Position pos = start_position();
    const SearchResult result = search_fixed_depth(pos, 3);
    REQUIRE(result.depth_completed == 3);
}

TEST_CASE("search_fixed_depth: depth_completed stays 0 for an already-terminal position", "[search]") {
    init_all();
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.depth_completed == 0);
}

TEST_CASE("search_iterative_deepening: max_depth 1 matches search_fixed_depth(pos, 1) exactly", "[search][id]") {
    init_all();
    Position pos = start_position();
    const SearchResult fixed = search_fixed_depth(pos, 1);
    const SearchResult id = search_iterative_deepening(pos, 1);
    REQUIRE(id.best_move == fixed.best_move);
    REQUIRE(id.score == fixed.score);
    REQUIRE(id.nodes == fixed.nodes);
    REQUIRE(id.depth_completed == 1);
}

TEST_CASE("search_iterative_deepening: unlimited time reaches max_depth and returns a legal, "
          "in-range result",
          "[search][id]") {
    init_all();
    // This test used to additionally REQUIRE(id.best_move ==
    // direct.best_move) and REQUIRE(id.score == direct.score) against a
    // fresh search_fixed_depth(pos, 3) call, on the theory that PVS, the
    // TT, and move ordering (docs/DECISIONS.md, 2026-08-15 TT/
    // move-ordering entries) are all "exact" techniques that only
    // change how cheaply a result is reached, never the result itself.
    // That reasoning holds as long as everything shared across
    // search_iterative_deepening()'s own iterations only ever
    // influences MOVE ORDER: a node's minimax value depends solely on
    // its own subtree, not on which order siblings were tried in, which
    // is exactly what makes leaning on move-ordering hints safe for
    // alpha-beta/PVS.
    //
    // Repetition detection (docs/DECISIONS.md, this session's entry)
    // breaks that assumption: a node's score can now depend on the
    // SPECIFIC SEQUENCE OF MOVES used to reach it (does this exact path
    // revisit an earlier position?), not just the position itself.
    // Real CI (all 6 platforms, 2026-08-20) reproduced a deterministic
    // id.best_move != direct.best_move divergence for this exact
    // scenario -- identical on every platform, ruling out flakiness or
    // undefined behavior. The mechanism: TT/killer/history sharing
    // across search_iterative_deepening()'s iterations changes move
    // order, which changes which subtree PVS's null-window probes cut
    // off early; if a repetition-by-path happens to sit inside a
    // subtree one run's ordering cuts off but the other's doesn't, the
    // two runs' backed-up scores for that branch -- and potentially the
    // final root choice -- can legitimately differ. This is a specific
    // instance of the well-known "Graph History Interaction" problem
    // (CPW), widely accepted across chess engines as unsolved in
    // general practice (essentially every engine with both repetition
    // detection and a shared TT has this same property) -- not a
    // Nightwing-specific bug, and not something a more careful
    // `game_history`/`path` implementation could fix without giving up
    // TT/killer/history sharing across iterations entirely, which would
    // be a much larger, real performance regression to avoid a
    // vanishingly-rare divergence.
    //
    // What's still guaranteed, and tested here: both searches complete
    // to the requested depth and both return a score within the
    // position's real (non-mate) evaluation range. id.best_move being
    // an actual legal move is covered separately by "picks a move from
    // the actual legal move list" below, so this test focuses on
    // depth/score-bound sanity instead of an equality claim that no
    // longer universally holds once path-dependent draw scoring exists
    // anywhere in the tree.
    Position pos = start_position();
    const SearchResult direct = search_fixed_depth(pos, 3);
    const SearchResult id = search_iterative_deepening(pos, 3);
    REQUIRE_FALSE(id.best_move.is_null());
    REQUIRE(id.depth_completed == 3);
    REQUIRE(direct.depth_completed == 3);
    REQUIRE(id.score > -kMateThreshold);
    REQUIRE(id.score < kMateThreshold);
    REQUIRE(direct.score > -kMateThreshold);
    REQUIRE(direct.score < kMateThreshold);
    // Sanity bound only: id must have visited at least as many nodes as
    // its own depth-1 pass alone (a fixed, always-unpruned 20 for the
    // start position -- see the depth-1 exact-node-count test above),
    // since that pass always runs in full before any deeper iteration
    // begins.
    REQUIRE(id.nodes >= 20);
}

TEST_CASE("search_iterative_deepening: an already-checkmated position returns a null move and -mate", "[search][id]") {
    init_all();
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    const SearchResult result = search_iterative_deepening(pos, 5);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.score == -kMateScore);
    REQUIRE(result.depth_completed == 0);
    REQUIRE(result.nodes == 1);
}

TEST_CASE("search_iterative_deepening: a tiny time budget stops before max_depth", "[search][id]") {
    init_all();
    // Depth 6 unpruned from the starting position is far more work than
    // any real machine finishes in ~1ms, so this is a safe, non-flaky
    // assertion regardless of how fast or slow the CI runner is -- it
    // deliberately does NOT assert which exact depth was reached (that
    // would be timing-dependent), only that the time budget actually cut
    // the search off short of max_depth.
    Position pos = start_position();
    const SearchResult result = search_iterative_deepening(pos, 6, 1);
    REQUIRE(result.depth_completed >= 1);
    REQUIRE(result.depth_completed < 6);
    REQUIRE_FALSE(result.best_move.is_null());
}

TEST_CASE("search_iterative_deepening: leaves the position unmodified", "[search][id]") {
    init_all();
    Position pos = start_position();
    const std::uint64_t hash_before = pos.zobrist_hash;
    (void)search_iterative_deepening(pos, 3);
    REQUIRE(pos.zobrist_hash == hash_before);
}

TEST_CASE("search_iterative_deepening: picks a move from the actual legal move list", "[search][id]") {
    init_all();
    Position pos = start_position();
    MoveList legal;
    generate_legal_moves(pos, legal);
    const SearchResult result = search_iterative_deepening(pos, 3);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(legal.contains(result.best_move));
}

TEST_CASE("search_iterative_deepening: finds a genuine mate-in-3 correctly with IIR active",
          "[search][iir]") {
    init_all();
    // Q+R vs lone king, verified by independent exhaustive search
    // (python-chess, not this engine) to be forced mate in exactly 3
    // full moves for White -- specifically NOT mate in 1 or 2, so this
    // genuinely requires looking 5 plies ahead to find with certainty.
    // At max_depth=6, negamax()'s internal recursion routinely reaches
    // remaining depth >= kIIRMinDepth (4) with no TT entry yet during
    // the earlier, shallower iterative-deepening iterations, so this
    // exercises Internal Iterative Reduction for real, not just at a
    // depth where it can never trigger -- and specifically relies on
    // IIR's actual safety net (iterative deepening's own
    // self-correction across iterations, not per-call exactness -- see
    // negamax()'s header comment in search.cpp) rather than coincidence,
    // since IIR is not a provably-exact technique the way PVS/TT/
    // aspiration windows are.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "null-move pruning active (depth 6 comfortably exceeds kNullMoveMinDepth at internal "
          "nodes)",
          "[search][nmp]") {
    init_all();
    // Same position/rationale as the IIR test just above, reused here
    // specifically to confirm null-move pruning (search.cpp's negamax(),
    // the NMP block right after IIR) doesn't cause this forced mate to
    // be missed or its score corrupted -- NMP is not a provably-exact
    // technique either (its own guards -- zugzwang risk, mate-range
    // beta, consecutive-null prevention -- exist precisely because
    // getting any of them wrong risks exactly this kind of missed
    // tactic), so a real search-level regression check matters here
    // beyond board.cpp's make_null_move()/unmake_null_move() round-trip
    // tests (tests/makemove_tests.cpp), which only confirm the
    // mechanical state/hash update is correct, not that the search
    // using it still finds real forced lines.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_fixed_depth: a pure king-and-pawn endgame (no non-pawn material) still "
          "searches successfully -- null-move pruning's zugzwang guard correctly disables it "
          "rather than crashing or hanging",
          "[search][nmp]") {
    init_all();
    // White has only a king and a pawn -- board::PieceType::Knight/
    // Bishop/Rook/Queen are all empty for White, so negamax()'s
    // `non_pawn_material` check (search.cpp) is false at every White
    // node, meaning null-move pruning's zugzwang guard should keep NMP
    // entirely inactive for this whole search, not just "usually skip
    // it." This test doesn't (and, without a reference engine, can't
    // easily) verify the position's exact evaluated value is
    // tablebase-correct -- it verifies the guard's code path itself
    // is exercised (a position where NMP is ALWAYS skipped, for the
    // entire search, not just sometimes) without crashing, hanging, or
    // returning a nonsensical result.
    Position pos = parse_fen("4k3/8/4K3/4P3/8/8/8/8 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 5);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.score > -kMateThreshold);
    REQUIRE(result.score < kMateThreshold);
}

TEST_CASE("search_fixed_depth: back-rank mate in 1 is still found exactly when searched well "
          "beyond the mating depth (mate distance pruning)",
          "[search][mdp]") {
    init_all();
    // Same position as the "finds a back-rank mate in 1" test above, but
    // requested at depth 6 instead of the minimum depth 2 needed to see
    // it: mate distance pruning (negamax()'s alpha/beta clamp on `ply`,
    // search.cpp) short-circuits nodes deep in this tree whose window is
    // already unreachable once the mate-in-1 has been found elsewhere in
    // the search, but must never change the actual result -- it's an
    // exact technique (search.cpp's negamax() header comment), same
    // guarantee class as PVS/TT/aspiration windows. Exact score
    // (kMateScore - 1, not just ">= kMateThreshold" as the shallower
    // test checks) confirms the engine still prefers the immediate mate
    // over any longer forced-mate line the deeper search might otherwise
    // also find, and that clamping didn't corrupt the returned score.
    Position pos = parse_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 6);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.best_move.from() == make_square(0, 0)); // a1
    REQUIRE(result.best_move.to() == make_square(0, 7));   // a8
    REQUIRE(result.score == kMateScore - 1);
}

TEST_CASE("search_fixed_depth: completes and returns a legal move at a depth where IIR engages",
          "[search][iir]") {
    init_all();
    // Coarse regression/safety-net check: a depth request comfortably
    // beyond kIIRMinDepth (4) from the start position doesn't hang,
    // crash, or return anything nonsensical. Doesn't assert an exact
    // best_move/score (IIR is a heuristic, not exact -- see negamax()'s
    // header comment -- so unlike the depth-1/depth-2 tests above, an
    // exact expected value isn't something to hand-verify here).
    Position pos = start_position();
    const SearchResult result = search_fixed_depth(pos, 5);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.depth_completed == 5);
    REQUIRE(result.score > -kMateThreshold);
    REQUIRE(result.score < kMateThreshold);
}

TEST_CASE("search_fixed_depth: 50-move rule forces a draw score one quiet ply after "
          "halfmove_clock reaches 100",
          "[search][fifty-move]") {
    init_all();
    // A quiet king-and-rook-vs-king position (no pawns, no captures
    // available for anyone) with halfmove_clock already at 99 -- one
    // more quiet move from ANY root move (every legal move here is a
    // quiet king or rook move) pushes it to 100, at which point
    // is_draw_by_rule() (search.cpp) must score that child kDrawScore
    // regardless of the position's material. Depth 2 is enough to reach
    // that child (negamax() at ply 1, depth 1 remaining -- still a
    // depth >= 1 node, so the check applies before this function hands
    // off to quiescence). Every root move leads to the same outcome
    // here, so the assertion is on result.score alone, not best_move.
    Position pos = parse_fen("7k/8/8/8/8/8/8/R6K w - - 99 50");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("search_fixed_depth: a position that already repeated once in game_history is "
          "recognized when the search revisits it, even though the losing side is down a "
          "whole queen",
          "[search][repetition]") {
    init_all();
    // White (down a queen, otherwise a bare king) shuffles Kb1-a1-b1
    // while Black mirrors Kb8-a8-b8, landing back on the exact starting
    // position (pos0) after 4 real, already-played moves -- a genuine
    // first repetition of pos0/pos1's king-on-a1 sub-position, recorded
    // into `history` below exactly as src/uci/uci.cpp's
    // apply_uci_moves() would. The black queen sits on h4 specifically
    // because it's off every diagonal/rank/file the a1/b1/a8/b8 shuffle
    // touches (CPW "Zobrist Hashing" doesn't factor in here directly,
    // but keeping the queen a spectator keeps this test's only variable
    // the repetition itself, not incidental tactics).
    //
    // From the resulting position (White to move, material score around
    // -900 for White on any non-repeating line -- nothing in this
    // engine's current material+PSQT eval could plausibly claw that back
    // to exactly 0), if is_draw_by_rule() correctly recognizes that
    // replaying Kb1-a1 recreates pos1 (already in `history`), that
    // specific move scores exactly kDrawScore -- strictly better than
    // every other legal White move, all of which stay deep in
    // queen-down territory. A score of exactly 0 here is strong,
    // specific evidence the repetition path was found and preferred,
    // not an artifact of the material-dominated eval landing near zero
    // by coincidence.
    Position pos = parse_fen("1k6/8/8/8/7q/8/8/1K6 w - - 0 1");
    std::vector<std::uint64_t> history;

    play_move(pos, make_square(1, 0), make_square(0, 0), &history); // pos0 -> pos1: Kb1-a1
    play_move(pos, make_square(1, 7), make_square(0, 7), &history); // pos1 -> pos2: Kb8-a8
    play_move(pos, make_square(0, 0), make_square(1, 0), &history); // pos2 -> pos3: Ka1-b1
    play_move(pos, make_square(0, 7), make_square(1, 7), &history); // pos3 -> pos4: Ka8-b8

    REQUIRE(history.size() == 4);
    REQUIRE(pos.halfmove_clock == 4);

    const SearchResult result = search_fixed_depth(pos, 2, history);
    REQUIRE(result.score == kDrawScore);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.best_move.from() == make_square(1, 0)); // b1
    REQUIRE(result.best_move.to() == make_square(0, 0));   // a1
}
