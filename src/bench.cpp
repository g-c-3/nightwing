// src/bench.cpp
//
// Nightwing bench: a tiny standalone tool that runs perft_bulk() (see
// board/perft.h) against the standard startpos/Kiwipete reference
// positions at a depth that takes a few seconds, and reports nodes and
// nodes-per-second. This is a movegen throughput baseline, not a
// correctness test — perft_tests.cpp already covers correctness (and
// perft_bulk()'s equivalence with plain perft()) with hard-asserted
// reference node counts. NPS numbers here are informational: they vary
// by machine, so nothing in the test suite asserts a threshold against
// them. Run this by hand (or wire into CI as an informational step, not
// a pass/fail gate) whenever a movegen or make/unmake change might
// affect throughput, to sanity-check it didn't regress silently.
//
// Once real search exists (Phase 2+), this will grow into the engine's
// actual search-based bench (the conventional meaning of "bench" for a
// UCI engine — a fixed-depth/fixed-time search used to sanity-check
// build reproducibility across commits). For now, with no search yet,
// perft_bulk() throughput is the closest available proxy.

#include <cstdio>
#include <chrono>
#include <cstdint>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/perft.h"
#include "board/zobrist.h"

namespace {

void run_one(const char* label, const char* fen, int depth) {
    using nightwing::board::Position;
    using nightwing::board::parse_fen;
    using nightwing::board::perft_bulk;

    Position pos = parse_fen(fen);
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint64_t nodes = perft_bulk(pos, depth);
    const auto t1 = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    const double mnps = (seconds > 0.0) ? (static_cast<double>(nodes) / seconds / 1'000'000.0) : 0.0;

    std::printf("%-12s depth %d: %12llu nodes in %7.3fs  (%7.2f Mnps)\n", label, depth,
                static_cast<unsigned long long>(nodes), seconds, mnps);
}

} // namespace

int main() {
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    std::printf("Nightwing bench (perft_bulk throughput baseline)\n");
    run_one("startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6);
    run_one("kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5);

    return 0;
}
