// tests/book_tests.cpp
//
// Unit tests for src/book/book.h (ROADMAP.md's optional "small curated
// opening book" item). Same style as the other test files in this
// suite: every scenario independently confirmed against the real,
// compiled implementation via a standalone driver before being written
// here as a permanent TEST_CASE.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "board/attacks.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "book/book.h"

using namespace nightwing::board;
using namespace nightwing::book;

namespace {

/// Applies the legal move matching `uci` (e.g. "e2e4") to `pos` and
/// returns the resulting position -- matches src/uci/uci.cpp's own
/// apply_uci_moves() convention (regenerate legal moves, match by
/// Move::to_uci() string) exactly, re-derived locally here per this
/// codebase's established per-test-file convention for this kind of
/// small helper (tests/search_tests.cpp's own apply_move() is the same
/// idea, also file-local, not shared). REQUIREs the move exists,
/// rather than silently doing nothing, since every call site below is
/// a hand-picked, known-legal move.
[[nodiscard]] Position apply(Position pos, const std::string& uci) {
    MoveList legal;
    generate_legal_moves(pos, legal);
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].to_uci() == uci) {
            UndoInfo undo;
            make_move(pos, legal[i], undo);
            return pos;
        }
    }
    FAIL("apply(): no legal move matches \"" << uci << "\" in this position");
    return pos; // unreachable -- FAIL() above throws
}

} // namespace

TEST_CASE("book_move: the start position has a book move after init_book()",
          "[book]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
    init_book();

    const std::optional<std::string> move = book_move(start_position());
    REQUIRE(move.has_value());
    // The Ruy Lopez line is listed first in book.cpp's own
    // curated_lines(), and init_book()'s own try_emplace-based
    // first-line-wins tie-break (book.cpp's own doc comment) means the
    // start position's book move is deterministically e2e4, not merely
    // "some legal first move" -- this exact value was independently
    // confirmed against the real compiled implementation before being
    // written here.
    REQUIRE(*move == "e2e4");
}

TEST_CASE("book_move: a real, multi-line-shared position (after 1.e4 e5 2.Nf3 Nc6, the "
          "starting point for both the Ruy Lopez and Italian lines in book.cpp) resolves to "
          "the first-listed line's own next move",
          "[book]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
    init_book();

    Position pos = start_position();
    pos = apply(pos, "e2e4");
    pos = apply(pos, "e7e5");
    pos = apply(pos, "g1f3");
    pos = apply(pos, "b8c6");

    const std::optional<std::string> move = book_move(pos);
    REQUIRE(move.has_value());
    // Ruy Lopez (f1b5) is listed before Italian Game (f1c4) in
    // book.cpp's curated_lines() -- confirms the tie-break is genuinely
    // "first line in the list," not e.g. "shortest line" or some other
    // rule that would also happen to produce a plausible-looking answer
    // here.
    REQUIRE(*move == "f1b5");
}

TEST_CASE("book_move: a position reached via a move no curated line contains is out of book",
          "[book]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
    init_book();

    Position pos = start_position();
    pos = apply(pos, "g1h3"); // Nh3 -- not the first move of any line in curated_lines()
    REQUIRE_FALSE(book_move(pos).has_value());
}

TEST_CASE("book_move: after following a curated line all the way to its own last recorded "
          "move, the resulting position is out of book (the book doesn't invent a continuation "
          "beyond what curated_lines() actually specifies)",
          "[book]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
    init_book();

    // The Petrov Defense line is exactly {"e2e4", "e7e5", "g1f3", "g8f6"}
    // -- four plies, nothing recorded for what comes after 3...Nf6.
    Position pos = start_position();
    pos = apply(pos, "e2e4");
    pos = apply(pos, "e7e5");
    pos = apply(pos, "g1f3");
    pos = apply(pos, "g8f6");
    REQUIRE_FALSE(book_move(pos).has_value());
}
