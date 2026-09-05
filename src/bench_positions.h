#pragma once
// src/bench_positions.h
//
// Shared fixed-position bench set (ROADMAP.md Phase 8's "`bench`
// command" item, and the pre-existing internal regression bench it's
// now shared with — tests/bench_tests.cpp). Kept in exactly one place
// so the two can never drift apart into silently reporting different
// positions or depth for what's nominally "the same" bench:
//   - tests/bench_tests.cpp: this project's own internal regression
//     tracking, run as an ordinary Catch2 test case on every CI push,
//     asserting genuine invariants (not a fixed node count — see that
//     file's own header comment for why) and printing a greppable
//     summary for a human to compare across sessions in
//     docs/SESSIONS.md.
//   - src/uci/uci.cpp's run_bench() (invoked via the UCI `bench`
//     command, and via `./nightwing bench` on the command line —
//     src/main.cpp): the actual fishtest/OpenBench-style bench this
//     ROADMAP item asks for — external tooling runs the compiled
//     binary itself (not this project's own test suite) and greps its
//     output for a "Nodes searched" total to verify build correctness/
//     reproducibility across commits.
//
// Both consumers search every position here to the SAME kBenchDepth,
// single-threaded (search_fixed_depth()'s own num_threads default),
// with the default Hash size, so their two reported node-count totals
// are directly comparable — deliberately, since a future session may
// want to cross-check one against the other after a change.
//
// See tests/bench_tests.cpp's own header comment for the full
// rationale behind depth being fixed (not time-limited) and modest,
// and behind this specific four-position set's composition (an
// opening, a tactical middlegame, a quiet middlegame, a forced-mate
// endgame — chosen for variety of CHARACTER, not just count).

#include <array>

namespace nightwing::bench {

struct BenchPosition {
    const char* name;
    const char* fen;
};

/// Fixed search depth for every bench position — see this file's own
/// header comment for why fixed (not time-limited) and why 6
/// specifically (deep enough to exercise Phase 4's depth-gated pruning/
/// extension techniques, shallow enough to stay fast on every CI
/// platform/build-type combination).
inline constexpr int kBenchDepth = 6;

// See this file's own header comment for why these four specifically.
inline constexpr std::array<BenchPosition, 4> kBenchPositions = {{
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    {"quiet_middlegame", "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 0 1"},
    {"endgame_mate_in_3", "2k5/8/8/8/3Q4/8/6K1/R7 w - - 0 1"},
}};

} // namespace nightwing::bench
