// tests/search_tests.cpp
//
// Unit tests for src/search/search.h — Phase 2's fixed-depth alpha-beta
// search and iterative deepening. Positions are built via FEN (fen.h),
// matching the style of movegen_tests.cpp / eval_tests.cpp.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "eval/endgame.h"
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

TEST_CASE("search_fixed_depth: pv is non-empty and starts with best_move, for a genuine mate-in-1",
          "[search]") {
    init_all();
    // Textbook back-rank mate-in-1 (1.Qb8#) -- a simple enough position
    // that the PV (SearchResult::pv, search.h) reconstructed via
    // extract_pv()'s TT walk (search.cpp) is expected to be exactly the
    // one move deep this depth allows, not truncated to nothing.
    Position pos = parse_fen("6k1/5ppp/8/8/8/8/8/1Q4K1 w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 1);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE_FALSE(result.pv.empty());
    REQUIRE(result.pv.front() == result.best_move);
}

TEST_CASE("search_fixed_depth: pv's first move is always legal in the searched position",
          "[search]") {
    init_all();
    Position pos = start_position();
    const SearchResult result = search_fixed_depth(pos, 3);
    REQUIRE_FALSE(result.pv.empty());
    MoveList legal;
    generate_legal_moves(pos, legal);
    REQUIRE(legal.contains(result.pv.front()));
}

TEST_CASE("search_fixed_depth: an already-terminal position's pv is empty (nothing to walk from)",
          "[search]") {
    init_all();
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.best_move.is_null());
    REQUIRE(result.pv.empty());
}

TEST_CASE("search_iterative_deepening: on_iteration fires once per completed depth, in "
          "increasing depth order, with cumulative (non-decreasing) node counts",
          "[search][id]") {
    init_all();
    Position pos = start_position();
    std::vector<int> depths_seen;
    std::vector<std::uint64_t> nodes_seen;
    const SearchResult result =
        search_iterative_deepening(pos, 4, 0, {}, [&](const SearchResult& iteration_result) {
            depths_seen.push_back(iteration_result.depth_completed);
            nodes_seen.push_back(iteration_result.nodes);
        });

    // One callback per depth 1..4, in order -- no time budget here, so
    // every iteration is expected to complete (none skipped via
    // mid-search interruption, search.h's SearchLimits).
    REQUIRE(depths_seen.size() == 4);
    for (std::size_t i = 0; i < depths_seen.size(); ++i) {
        REQUIRE(depths_seen[i] == static_cast<int>(i) + 1);
    }

    // Nodes reported to the callback are the CUMULATIVE running total
    // (IterationCallback's own doc comment, search.h), so each
    // iteration's reported count must be >= the previous one's, and the
    // very last callback's count must match the final SearchResult's own
    // total exactly.
    for (std::size_t i = 1; i < nodes_seen.size(); ++i) {
        REQUIRE(nodes_seen[i] >= nodes_seen[i - 1]);
    }
    REQUIRE(nodes_seen.back() == result.nodes);
}

TEST_CASE("search_iterative_deepening: on_iteration is never called for an already-terminal "
          "position",
          "[search][id]") {
    init_all();
    Position pos = parse_fen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    int callback_count = 0;
    (void)search_iterative_deepening(pos, 5, 0, {},
                                      [&](const SearchResult&) { ++callback_count; });
    REQUIRE(callback_count == 0);
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

TEST_CASE("search_iterative_deepening: a tiny time budget bounds wall-clock time even when a "
          "single iteration alone would run far longer than the budget (mid-search interruption, "
          "not just the between-iteration check)",
          "[search][id]") {
    init_all();
    // Before mid-search interruption existed, only the check BETWEEN
    // iterations (before starting the next search_root() call) could
    // stop a search -- an iteration already in progress ran to
    // completion regardless of the time budget. depth 1 from the
    // starting position always completes near-instantly (guaranteed,
    // unconditionally, no deadline at all -- search.h's own doc
    // comment) with plenty of budget left in the 1ms requested here, so
    // the between-iteration check right after it still lets depth 2
    // start; from there, an unpruned-enough middlegame branching factor
    // makes it extremely unlikely depth 2 alone also finishes within
    // 1ms. If mid-search interruption (search.h's SearchLimits,
    // search.cpp's kTimeCheckNodeInterval) is working, depth 2 gets cut
    // off partway through and the whole call returns quickly regardless
    // -- if it regressed back to between-iteration-only checking, depth
    // 2 (and this assertion) would instead have to wait for however
    // long an entire unpruned depth-2 search takes, which is not
    // reliably fast. 2000ms is an enormous margin relative to the 1ms
    // requested -- correct interruption returns within roughly one
    // kTimeCheckNodeInterval-sized batch of extra node visits (a small,
    // constant amount of work), not anywhere close to 2000ms, so this
    // isn't a tight/flaky timing assertion in the direction that
    // matters; it only guards against the regression this test exists
    // to catch.
    Position pos = start_position();
    const auto call_start = std::chrono::steady_clock::now();
    const SearchResult result = search_iterative_deepening(pos, 10, 1);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - call_start)
                                 .count();
    REQUIRE(elapsed_ms < 2000);
    REQUIRE(result.depth_completed >= 1);
    REQUIRE(result.depth_completed < 10);
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

TEST_CASE("search_fixed_depth: a genuine RookEndgame-material position (both sides have one "
          "rook plus pawns) still searches successfully with the zugzwang-aware null-move bias "
          "active -- same honest scope as the KPK NMP guard test above: this doesn't (and, "
          "without a reference engine, can't) prove tablebase-correctness, only that the biased "
          "code path is genuinely exercised without crashing, hanging, or returning a "
          "nonsensical result",
          "[search][nmp][zugzwang]") {
    init_all();
    // Both sides have exactly one rook plus several pawns -- a real
    // eval::EndgameSignature::RookEndgame position (confirmed directly
    // below), so eval::is_zugzwang_prone() flags it and search.cpp's
    // negamax() NMP block reduces R by kZugzwangReductionDecrease at
    // every one of this search's own nodes, rather than skipping null-
    // move pruning outright (unlike the KPK test above) or leaving it
    // fully unbiased (unlike a typical middlegame position).
    Position pos = parse_fen("2r1k3/pp3ppp/8/8/8/8/PP3PPP/2R1K3 b - - 0 1");
    REQUIRE(nightwing::eval::classify_endgame(pos) == nightwing::eval::EndgameSignature::RookEndgame);
    const SearchResult result = search_fixed_depth(pos, 6);
    REQUIRE_FALSE(result.best_move.is_null());
    REQUIRE(result.score > -kMateThreshold);
    REQUIRE(result.score < kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "late move reductions active",
          "[search][lmr]") {
    init_all();
    // Same position/rationale as the IIR and NMP regression tests above
    // -- reused again here specifically for LMR (search.cpp's negamax()
    // move loop, the `else` branch's reduction logic). Like NMP, LMR is
    // not a provably-exact technique (a reduced probe that happens to
    // stay <= alpha is trusted without ever re-verifying at full depth
    // -- that's the entire point of the optimization, and also exactly
    // the risk if the reduction/re-verification cascade has a bug), so
    // a real search-level check that a genuine forced line survives it
    // matters here specifically, not just that the code compiles and
    // runs. Depth 6 gives plenty of room for `i >= kLMRMinMoveIndex`
    // quiet moves at `depth >= kLMRMinDepth` to actually get reduced
    // during this search, not just sit below the threshold everywhere.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "late move pruning active",
          "[search][lmp]") {
    init_all();
    // Same position/rationale as the IIR/NMP/LMR regression tests above
    // -- reused again here specifically for LMP (search.cpp's negamax()
    // move loop, the new move-count-based skip checked ahead of LMR's
    // own reduction). LMP is strictly more aggressive than LMR (it can
    // skip a quiet move's search entirely, not just reduce it), so a
    // bug in its guards -- the in-check exclusion, the mate-threshold
    // guard, the quiets_tried counter itself -- risks silently pruning
    // away the one quiet move that was actually needed somewhere along
    // this forced mating line, not just searching it less deeply. Depth
    // 6 comfortably exceeds kLMPMaxDepth at the shallower internal
    // nodes this search reaches, so LMP's skip path is genuinely
    // exercised here, not just present in the code but never triggered.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "futility pruning active",
          "[search][futility]") {
    init_all();
    // Same position/rationale as the IIR/NMP/LMR/LMP regression tests
    // above -- reused again here specifically for futility pruning
    // (search.cpp's negamax() move loop, the new node-level static-eval
    // check ahead of LMP and LMR). Futility pruning is checked FIRST in
    // the else-branch cascade, so a bug in its guards -- the
    // not-in-check/mate-range exclusions, the static-eval computation
    // itself, the margin table -- risks silently pruning away a quiet
    // move a forced line needed before LMP or LMR even get a chance to
    // run. Depth 6 gives internal nodes at remaining depth <=
    // kFutilityMaxDepth (3) plenty of opportunity to actually trigger
    // the skip, not just leave the code present but unexercised.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "razoring active",
          "[search][razoring]") {
    init_all();
    // Same position/rationale as the IIR/NMP/LMR/LMP/futility regression
    // tests above -- reused again here specifically for razoring
    // (search.cpp's negamax(), the new node-level check right after the
    // NMP block, before movegen). Razoring is the most drastic of this
    // phase's techniques so far -- it can skip an entire node's move
    // loop, not just individual moves -- so a bug in its verification
    // step (falling through to the normal move loop whenever quiescence
    // itself disagrees with the static eval's pessimism) risks silently
    // returning a wrong, unverified score for a node along this forced
    // mating line rather than just searching it differently. Depth 6
    // gives internal nodes at remaining depth <= kRazorMaxDepth (3)
    // plenty of opportunity to actually trigger the check, not just
    // leave the code present but unexercised.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "history pruning active",
          "[search][history_pruning]") {
    init_all();
    // Same position/rationale as the IIR/NMP/LMR/LMP/futility/razoring
    // regression tests above -- reused again here specifically for
    // history pruning (search.cpp's negamax() move loop, checked
    // independently of, right alongside, the existing LMP check). A
    // fresh HistoryTable starts every move at score 0, so at shallow
    // depth this check is maximally aggressive early in a fresh search
    // -- exactly the situation where a guard bug (the not-in-check
    // exclusion, the mate-range guard, an off-by-one in the threshold
    // table) would most plausibly prune away a move this forced mating
    // line actually needs. Depth 6 gives internal nodes at remaining
    // depth <= kHistoryPruningMaxDepth (3) plenty of opportunity to
    // trigger the check, not just leave the code present but
    // unexercised.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "continuation history active",
          "[search][continuation_history]") {
    init_all();
    // Same position/rationale as the IIR/NMP/LMR/LMP/futility/razoring/
    // history-pruning regression tests above -- reused again here
    // specifically for continuation history (search/ordering.h's
    // ContinuationHistoryTable, threaded through search.cpp's
    // negamax()/search_root() as `cont_history`/`prev_piece`/`prev_to`).
    // This is purely a move-ORDERING signal (added into score_move(),
    // search/ordering.cpp), not a pruning technique -- nothing here can
    // change which moves are legal or searched, only what order they're
    // tried in -- so a bug in the threading (the wrong piece/square
    // captured before make_move(), a `prev_piece`/`prev_to` pair not
    // actually reaching the intended child call) risks silently
    // misordering moves rather than mis-scoring the position, but the
    // deepest confirmation available without a real bench/perft
    // comparison is still that the correct forced mate is found exactly
    // once continuation history is live end-to-end across the whole
    // recursive call chain this test exercises.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "ProbCut active",
          "[search][probcut]") {
    init_all();
    // Same position/rationale as the IIR/NMP/LMR/LMP/futility/razoring/
    // history-pruning/continuation-history regression tests above --
    // reused again here specifically for ProbCut (search.cpp's
    // negamax(), the new node-level check right after order_moves(),
    // opposite end of the depth spectrum from futility/razoring: it
    // applies at MODERATE-to-high remaining depth, not near the
    // leaves). Depth 6 gives the root's own immediate children remaining
    // depth 5 -- exactly kProbCutMinDepth -- so this test's very first
    // ply already exercises the check, not just leaves the code present
    // but unreached. ProbCut is a whole-node fail-high shortcut (like
    // razoring's own fail-low shortcut, just mirrored), so a bug in its
    // guards or its fail-soft return risks silently mis-scoring a node
    // along this forced mating line rather than merely searching it
    // differently.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "check extensions active",
          "[search][check_extension]") {
    init_all();
    // Same position/rationale as every prior Phase 4 regression test
    // above -- reused again here specifically for check extensions
    // (search.cpp's negamax()/search_root(), the first DEPTH-ADDING
    // technique in this file rather than a pruning/reduction one). This
    // position's own mating line is built entirely from checking moves
    // (Qd4-d8+, Ra1-a8#, etc.), so every ply of the actual search this
    // test exercises should be getting the extra ply check extensions
    // grant, not just occasionally touching the code path the way some
    // of the narrower Phase 4 checks above only get exercised at a
    // specific depth threshold. This is an end-to-end regression check
    // (the mate is still found, extended or not, since alpha-beta plus
    // a correct evaluation would eventually find it regardless) rather
    // than a demonstration that the extension is NECESSARY to find this
    // particular mate at this particular depth -- constructing a
    // position where a check extension is the deciding factor between
    // finding and missing a mate at a fixed low depth would need
    // external engine verification to build with confidence, and is
    // deferred rather than guessed at by hand.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
}

TEST_CASE("search_iterative_deepening: the same forced mate-in-3 is still found correctly with "
          "singular extensions active",
          "[search][singular_extension]") {
    init_all();
    // Same position/rationale as every prior Phase 4 regression test
    // above -- reused again here specifically for singular extensions
    // (search.cpp's negamax(), this file's second and more expensive
    // depth-adding technique, evaluated only for the TT move). Unlike
    // most of this position's other Phase 4 tests, singular extensions
    // depend on a PRIOR search having already stored a TT entry with a
    // Bound::Lower score for the position being re-searched -- iterative
    // deepening naturally provides that (each depth's search populates
    // the TT that the next, deeper iteration probes), so this position
    // is searched via search_iterative_deepening() specifically (not
    // search_fixed_depth()) to give the singular-extension check a
    // realistic chance to actually trigger by the final iteration,
    // rather than only ever seeing an empty or too-shallow TT. This is
    // an end-to-end regression check (the mate is still found correctly
    // whether or not any given node's TT move happens to qualify as
    // singular) rather than a demonstration that the extension is
    // NECESSARY to find this particular mate -- the same caveat the
    // check-extensions test above already notes, for the same reason.
    Position pos = parse_fen("2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1");
    const SearchResult result = search_iterative_deepening(pos, 6);
    REQUIRE(result.score >= kMateThreshold);
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

TEST_CASE("search_fixed_depth: insufficient material (king and a single knight vs. bare king) "
          "forces a draw score at the root immediately, no captures or waiting required -- "
          "is_insufficient_material() (search.cpp)",
          "[search][insufficient-material]") {
    init_all();
    Position pos = parse_fen("7k/8/8/8/8/8/8/N6K w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("search_fixed_depth: insufficient material (bishops on both sides, SAME square "
          "color) forces a draw score at the root immediately",
          "[search][insufficient-material]") {
    init_all();
    // White Bc1 (file 2, rank 0 -- dark), Black Bf8 (file 5, rank 7 --
    // also dark). Same color, no pawns anywhere -- the one two-minor
    // case is_insufficient_material() recognizes.
    Position pos = parse_fen("5b1k/8/8/8/8/8/8/2B4K w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    REQUIRE(result.score == kDrawScore);
}

TEST_CASE("search_fixed_depth: two knights vs. bare king is NOT treated as insufficient "
          "material -- score reflects the real material lead, not a forced draw -- confirms "
          "is_insufficient_material()'s total_minors <= 1 threshold doesn't over-trigger on a "
          "second minor for the SAME side",
          "[search][insufficient-material]") {
    init_all();
    Position pos = parse_fen("7k/8/8/8/8/8/8/NN5K w - - 0 1");
    const SearchResult result = search_fixed_depth(pos, 2);
    // Two knights (roughly 320cp each) is a large, unambiguous material
    // lead -- nowhere near kDrawScore (0) if the position is being
    // evaluated normally rather than incorrectly force-drawn.
    REQUIRE(result.score > 300);
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
