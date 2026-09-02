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

#include <iostream>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "book/book.h"
#include "uci/uci.h"

int main() {
    // Mandatory order per ARCHITECTURE.md: init_masks() ->
    // init_magic_bitboards() -> init_zobrist_keys(). Required before
    // uci::run()'s "position"/"go" commands can touch movegen/search
    // (see board/movegen.h's precondition).
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    // book::init_book() (src/book/book.h): replays the curated opening
    // book's own lines through real legal move generation, so it needs
    // the board subsystem above already initialized -- appended as one
    // more startup step specifically for this program (a test binary
    // that never calls it just gets book_move() == std::nullopt for
    // everything, always a safe fallback -- book.h's own doc comment).
    nightwing::book::init_book();

    nightwing::uci::run(std::cin, std::cout);

    return 0;
}
