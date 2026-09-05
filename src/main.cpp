// src/main.cpp
//
// Nightwing entry point. Runs the mandatory board-subsystem startup
// sequence (ARCHITECTURE.md "Startup Sequence"), then book::init_book()
// (src/book/book.h — ROADMAP.md's optional "small curated opening
// book" item), then hands off to the UCI loop (uci/uci.cpp) on
// stdin/stdout — this is what a GUI or a self-play script actually
// talks to. The Phase 0/1 startup-sequence smoke-test printout this
// replaced now lives in test_smoke.cpp's CPU-feature-detection test
// instead.
//
// `./nightwing bench` (ROADMAP.md Phase 8, "`bench` command"):
// recognized as a special first command-line argument, matching the
// convention several established engines already use for fishtest/
// OpenBench-style tooling, which typically invokes the compiled binary
// directly with this argument rather than driving it over UCI stdin/
// stdout (though uci::run()'s own `bench` command — uci.cpp — supports
// that path too, for tooling that does go through UCI). When present,
// runs uci::run_bench() and exits immediately WITHOUT starting the
// interactive UCI loop or initializing the opening book (irrelevant to
// what bench measures — run_bench() calls search_fixed_depth()
// directly, never touching book::book_move() at all).

#include <cstring>
#include <iostream>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "book/book.h"
#include "uci/uci.h"

int main(int argc, char** argv) {
    // Mandatory order per ARCHITECTURE.md: init_masks() ->
    // init_magic_bitboards() -> init_zobrist_keys(). Required before
    // uci::run()'s "position"/"go" commands, AND uci::run_bench()
    // (both exercise movegen/search) can touch movegen/search (see
    // board/movegen.h's precondition) — needed on both the `bench` and
    // the ordinary interactive paths below, so it happens unconditionally,
    // before either branch.
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        nightwing::uci::run_bench(std::cout);
        return 0;
    }

    // book::init_book() (src/book/book.h): replays the curated opening
    // book's own lines through real legal move generation, so it needs
    // the board subsystem above already initialized -- appended as one
    // more startup step specifically for this program (a test binary
    // that never calls it just gets book_move() == std::nullopt for
    // everything, always a safe fallback -- book.h's own doc comment).
    // Skipped entirely on the `bench` path above -- irrelevant to what
    // bench measures, and this project's own tests already exercise
    // init_book() independently (tests/book_tests.cpp).
    nightwing::book::init_book();

    nightwing::uci::run(std::cin, std::cout);

    return 0;
}
