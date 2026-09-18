// tests/quiescence_tests.cpp
//
// Unit tests for src/search/quiescence.h, calling quiescence() directly
// (not through the full negamax() search) to isolate its own behavior:
// stand-pat, capture search, SEE pruning's actual effect on node count
// (not just "the search doesn't play a bad move," which alpha-beta
// alone would already guarantee -- these tests specifically check that
// a losing capture is skipped rather than searched and rejected),
// include_checks, in-check evasion handling, and terminal detection.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "eval/incremental.h" // compute_material_psqt() -- lazy-eval test's independently-derived expectation
#include "eval/score.h" // taper() -- lazy-eval test's independently-derived expectation
#include "search/quiescence.h"
#include "search/search.h"

using namespace nightwing::board;
using namespace nightwing::eval;
using namespace nightwing::search;

namespace {
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("quiescence: a quiet position with no captures returns exactly the stand-pat eval",
          "[quiescence]") {
    init_all();
    Position pos = start_position();
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/true);

    const int white_relative = evaluate(pos);
    const int expected = pos.side_to_move == Color::White ? white_relative : -white_relative;
    REQUIRE(score == expected);
    REQUIRE(nodes == 1); // just the top-level call -- nothing to search further
}

TEST_CASE("quiescence: SEE-pruning actually skips a losing capture rather than searching it",
          "[quiescence]") {
    init_all();
    // White queen d1 can capture a black pawn on d5 that's defended by a
    // black pawn on c6 -- a clearly losing capture (see the SEE tests,
    // see_tests.cpp, for the same position confirming SEE == -800).
    // This position also happens to have two quiet checking moves
    // available (Qh5+, Qe2+) -- irrelevant to what THIS test checks, but
    // real, so include_checks=false isolates pure capture-pruning
    // behavior specifically (a separate test below already covers
    // include_checks on its own). With SEE-pruning working, the losing
    // capture is skipped entirely: no recursive quiescence call happens
    // for it, so nodes stays at 1 (only the top-level call itself).
    Position pos = parse_fen("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1");
    std::uint64_t nodes = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/false);
    REQUIRE(nodes == 1);
}

TEST_CASE("quiescence: a genuinely good (non-pruned) capture is actually searched", "[quiescence]") {
    init_all();
    // Same shape as the pruning test above, but the pawn on d5 is
    // undefended this time -- a good capture (SEE > 0), so it must NOT
    // be pruned: at least one recursive call happens for it.
    // include_checks=false again isolates pure capture behavior (this
    // position also has quiet checks available, same as the pruning
    // test above) -- without it, nodes > 1 would hold even if Qxd5 were
    // wrongly pruned, since the checks alone would still push nodes up,
    // silently defeating what this test is actually meant to verify.
    Position pos = parse_fen("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1");
    std::uint64_t nodes = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/false);
    REQUIRE(nodes > 1);
}

TEST_CASE("quiescence: delta pruning skips a capture whose value can't plausibly close a much "
          "higher alpha, even though SEE alone would accept it",
          "[quiescence][delta]") {
    init_all();
    // Same undefended-pawn capture as the "genuinely good capture" test
    // above (SEE > 0, so SEE pruning alone would never skip it) --
    // deliberately isolating delta pruning as the reason for the skip
    // by passing a much higher alpha than a single pawn's value plus
    // this file's own kDeltaMargin could ever plausibly close, on a
    // position where the stand-pat baseline is already well below that
    // alpha too (White is up a queen for nothing, a large but still
    // bounded material edge). With delta pruning working, the capture
    // is skipped before ever reaching the recursive call that would
    // search it: nodes stays at 1 (only the top-level call).
    Position pos = parse_fen("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1");
    std::uint64_t nodes = 0;
    (void)quiescence(pos, /*alpha=*/5000, /*beta=*/1'000'000, 0, nodes, /*include_checks=*/false);
    REQUIRE(nodes == 1);
}

TEST_CASE("quiescence: delta pruning's own mate-range-alpha guard prevents it from wrongly "
          "skipping a good capture when alpha is already near a mate score",
          "[quiescence][delta]") {
    init_all();
    // Same good capture again, but with alpha pushed into mate-score
    // territory (kMateThreshold and up) -- without the guard on delta
    // pruning's own applicability (this file's quiescence_impl(), right
    // before the candidate loop), the per-move margin check below would
    // wrongly conclude EVERY ordinary capture fails to close a mate-
    // sized alpha and skip it outright, which would be a correctness
    // bug (silently reporting no good options exist when a real,
    // materially-winning capture does) rather than a missed
    // optimization. With the guard working, delta pruning doesn't apply
    // at all here, so the capture reaches SEE (which accepts it, SEE >
    // 0) and gets genuinely searched: nodes > 1.
    Position pos = parse_fen("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1");
    std::uint64_t nodes = 0;
    (void)quiescence(pos, /*alpha=*/kMateThreshold + 1, /*beta=*/1'000'000, 0, nodes,
                      /*include_checks=*/false);
    REQUIRE(nodes > 1);
}

TEST_CASE("quiescence: include_checks controls whether a non-capture checking move is explored",
          "[quiescence]") {
    init_all();
    // White rook a1 can play Ra8+ (check, not a capture) -- nothing else
    // tactical is available (no captures exist at all on this board).
    Position pos = parse_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");

    std::uint64_t nodes_with_checks = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes_with_checks, /*include_checks=*/true);
    REQUIRE(nodes_with_checks > 1); // Ra8+ (and its evasion replies) get explored

    std::uint64_t nodes_without_checks = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes_without_checks, /*include_checks=*/false);
    REQUIRE(nodes_without_checks == 1); // no captures exist, and checks are ignored -- immediate stand-pat
}

TEST_CASE("quiescence: when in check, every legal evasion is tried, not just captures", "[quiescence]") {
    init_all();
    // Black king e8 in check from a white rook on e1 (open e-file) --
    // Black's only legal moves are king steps off the e-file/rank (no
    // blocker or capture available). None of Black's evasions are
    // captures, so if quiescence only ever considered captures while in
    // check, it would find zero candidates and (incorrectly) report
    // this position as if it had no legal response -- it must instead
    // find a real evasion and a real (non-mate, non-draw) score.
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4RK2 b - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/true);
    REQUIRE(nodes > 1); // at least one evasion was actually explored
    REQUIRE(score > -kMateScore); // not reported as checkmate -- a real evasion exists
}

TEST_CASE("quiescence: detects checkmate with a correctly ply-adjusted score", "[quiescence]") {
    init_all();
    // Already-delivered back-rank mate: white rook a8, black king g8
    // boxed in by its own pawns, black to move with zero legal moves.
    Position pos = parse_fen("R5k1/5ppp/8/8/8/8/8/6K1 b - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, /*ply=*/3, nodes, true);
    REQUIRE(score == -(kMateScore - 3)); // mated at ply 3, from the mated side's perspective
}

TEST_CASE("quiescence: detects stalemate as a draw, not a loss", "[quiescence]") {
    init_all();
    // Black king h8, white queen g6 controls g7/g8/h7 without checking
    // h8 itself, white king a1 -- verified stalemate (0 legal moves, not
    // in check).
    Position pos = parse_fen("7k/8/6Q1/8/8/8/8/K7 b - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, true);
    REQUIRE(score == kDrawScore);
}

TEST_CASE("quiescence: a quiet (non-capturing) promotion is still explored, not silently "
          "stood-pat past (docs/DECISIONS.md has the full bug account)",
          "[quiescence][promotion]") {
    init_all();
    // White pawn a7 about to promote; no black piece anywhere for it to
    // capture, and the king (h4) is deliberately placed off the a8
    // rank/file/diagonal so that a7a8=Q does NOT incidentally give
    // check -- isolating this test to the promotion-specific fix rather
    // than letting `include_checks`'s own pre-existing check-detection
    // branch account for it by coincidence. `include_checks=false`
    // (deliberately, unlike most other tests in this file) is exactly
    // the condition that used to make this fail before `|| move.is_promotion()`
    // was added to quiescence_impl()'s candidate-loop condition: before
    // that fix, this move matched none of the loop's branches (not a
    // capture, not in check, and `include_checks` is off here so the
    // checking-move branch doesn't apply either), so `candidates` would
    // have stayed empty and quiescence would have silently stood pat on
    // the pre-promotion material -- nodes would have stayed at 1. With
    // the fix, the promotion is now an unconditional candidate (same as
    // a capture), so it's genuinely searched: nodes > 1.
    Position pos = parse_fen("8/P7/8/8/7k/8/8/4K3 w - - 0 1");
    std::uint64_t nodes = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/false);
    REQUIRE(nodes > 1);
}

TEST_CASE("quiescence: leaves the position completely unmodified", "[quiescence]") {
    init_all();
    Position pos = parse_fen("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1");
    const std::uint64_t hash_before = pos.zobrist_hash;
    std::uint64_t nodes = 0;
    (void)quiescence(pos, -1'000'000, 1'000'000, 0, nodes, true);
    REQUIRE(pos.zobrist_hash == hash_before);
}

TEST_CASE("quiescence: stand-pat's lazy-eval window (ROADMAP.md's NPS/Raw Speed track, \"Lazy "
          "evaluation / early-exit on cheap terms\" item) correctly fails high using only "
          "material+PSQT when the position is overwhelmingly good regardless of the remaining, "
          "unevaluated terms",
          "[quiescence][lazy_eval]") {
    init_all();
    // White is up a full queen on a busy, asymmetric board -- material+
    // PSQT alone clears a narrow beta well below a queen's own value, by
    // far more than eval::evaluate()'s own lazy-eval margin (eval.cpp),
    // regardless of whatever mobility/king safety/pawn structure/etc.
    // would otherwise contribute.
    Position pos = parse_fen("r1bqkbnr/pp1ppppp/2n5/2p5/4P3/5N2/PPPPQPPP/RNBQKB1R w KQkq - 2 3");
    std::uint64_t nodes = 0;
    const int beta = 100;
    const int score = quiescence(pos, -1'000'000, beta, 0, nodes, /*include_checks=*/true);

    // Independently derived expectation -- the exact same material+
    // PSQT-only tapered value eval_tests.cpp's own dedicated lazy-eval
    // unit tests confirm evaluate()'s early-exit path returns, computed
    // here from scratch rather than trusted from that other file.
    const int phase = compute_phase(pos);
    const int material_psqt_only = taper(compute_material_psqt(pos), phase);
    REQUIRE(score == material_psqt_only);
    REQUIRE(score >= beta); // the fail-high the stand-pat check itself relies on
    REQUIRE(nodes == 1); // stand-pat alone resolved this node -- no further search needed
}

// ---------------------------------------------------------------------
// Staged / lazy move generation, Step 2a (docs/ROADMAP.md, docs/
// DECISIONS.md 2026-09-18 (4)): quiescence_impl()'s `!us_in_check &&
// !include_checks` path now generates `GenType::Captures` only,
// falling back to `GenType::Quiets` solely to tell a genuinely
// terminal (stalemate) position apart from a merely-quiet one -- the
// one case the correctness argument in DECISIONS.md depends on but
// none of this file's existing tests actually exercised (the existing
// "quiet position"/"detects stalemate" tests above both pass
// include_checks=true, which always takes the GenType::All path and
// never touches the new fallback at all; the SEE/delta-pruning tests
// above pass include_checks=false but every one of their positions has
// at least one capture available, so GenType::Captures alone is always
// non-empty there and the Quiets fallback never triggers either). The
// two tests below are the first in this file to actually drive that
// fallback, on both sides of the ambiguity it exists to resolve.
// ---------------------------------------------------------------------

TEST_CASE("quiescence: a capture-free but non-terminal position (Quiets fallback finds legal "
          "moves) returns the real stand-pat eval, not a draw score",
          "[quiescence][staged]") {
    init_all();
    // White king e1, White rook a1, Black king e8, include_checks=false,
    // Black to move (well) -- actually White to move, deliberately: no
    // Black piece exists for White to capture, so GenType::Captures at
    // this node returns empty and the new Quiets fallback must fire.
    // The position is materially lopsided (White up a whole rook) and
    // very much NOT stalemate (both the king and the rook have plenty
    // of quiet moves) -- before Step 2a's fallback existed, this exact
    // shape (GenType::Captures empty, real legal moves elsewhere) is
    // precisely what could have been missed by a naive "captures empty
    // implies terminal" shortcut; this test exists to pin that down.
    Position pos = parse_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/false);

    const int white_relative = evaluate(pos);
    const int expected = pos.side_to_move == Color::White ? white_relative : -white_relative;
    REQUIRE(score == expected); // the real stand-pat eval...
    REQUIRE(score != kDrawScore); // ...specifically NOT the stalemate draw score
    REQUIRE(nodes == 1); // stand-pat alone resolved this node -- no candidates to search
}

TEST_CASE("quiescence: detects stalemate as a draw with include_checks=false too (Quiets "
          "fallback correctly confirms zero legal moves, not just zero captures)",
          "[quiescence][staged]") {
    init_all();
    // Same verified-stalemate position as the existing "detects
    // stalemate as a draw" test above (black king h8, white queen g6,
    // white king a1 -- 0 legal moves, not in check), but called with
    // include_checks=false this time specifically to exercise the new
    // GenType::Captures-then-Quiets-fallback path rather than the
    // GenType::All path the existing test above already covers. Both
    // stages must come back empty for this to correctly reach the
    // terminal branch rather than wrongly falling through to a stand-pat
    // return.
    Position pos = parse_fen("7k/8/6Q1/8/8/8/8/K7 b - - 0 1");
    std::uint64_t nodes = 0;
    const int score = quiescence(pos, -1'000'000, 1'000'000, 0, nodes, /*include_checks=*/false);
    REQUIRE(score == kDrawScore);
    REQUIRE(nodes == 1);
}
