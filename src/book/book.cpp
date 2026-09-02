// src/book/book.cpp
//
// See book.h for the overall design. curated_lines() below is the
// entire book's actual content — a small, hand-picked set of
// well-established, sound main-line openings, each written as a plain
// sequence of UCI long-algebraic moves from the start position, easy
// to read and verify against real opening theory by inspection (no
// hand-computed hashes or binary format to audit). Every line was
// checked move-by-move against well-established opening theory before
// being included — this is deliberately NOT an attempt at deep or
// comprehensive coverage; ROADMAP.md's own "small curated" wording is
// taken literally.

#include "book/book.h"

#include <unordered_map>
#include <vector>

#include "board/movegen.h"

namespace nightwing::book {

namespace {

using board::Move;
using board::MoveList;
using board::Position;
using board::UndoInfo;

[[nodiscard]] std::unordered_map<std::uint64_t, std::string>& table() {
    static std::unordered_map<std::uint64_t, std::string> t;
    return t;
}

/// The book's actual content: a handful of well-known, sound main
/// lines for both colors, deliberately small (ROADMAP.md's own "small
/// curated" wording). Each inner vector is a sequence of UCI
/// long-algebraic moves from board::start_position() — comments name
/// the real opening/variation each line represents, for anyone
/// reviewing or extending this list later.
[[nodiscard]] const std::vector<std::vector<std::string>>& curated_lines() {
    static const std::vector<std::vector<std::string>> lines = {
        // Ruy Lopez (Spanish Opening)
        {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"},
        {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6"}, // Morphy Defense, Closed
        // Italian Game
        {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"},
        // Petrov (Russian) Defense
        {"e2e4", "e7e5", "g1f3", "g8f6"},
        // Sicilian Defense
        {"e2e4", "c7c5"},
        {"e2e4", "c7c5", "g1f3", "d7d6"}, // heading toward Najdorf/Scheveningen setups
        {"e2e4", "c7c5", "g1f3", "b8c6"}, // Open Sicilian, Nc6 systems
        // French Defense
        {"e2e4", "e7e6"},
        {"e2e4", "e7e6", "d2d4", "d7d5"},
        // Caro-Kann Defense
        {"e2e4", "c7c6"},
        // Queen's Gambit
        {"d2d4", "d7d5", "c2c4"},
        {"d2d4", "d7d5", "c2c4", "e7e6"}, // Queen's Gambit Declined
        {"d2d4", "d7d5", "c2c4", "c7c6"}, // Slav Defense
        // Indian Defenses
        {"d2d4", "g8f6", "c2c4", "e7e6"}, // heading toward Nimzo-Indian/Queen's Indian setups
        {"d2d4", "g8f6", "c2c4", "g7g6"}, // heading toward King's Indian/Grünfeld setups
        // English Opening
        {"c2c4"},
        {"c2c4", "e7e5"}, // Reversed Sicilian setup
    };
    return lines;
}

} // namespace

void init_book() {
    std::unordered_map<std::uint64_t, std::string>& t = table();
    t.clear();

    for (const std::vector<std::string>& line : curated_lines()) {
        Position pos = board::start_position();
        for (const std::string& uci_move : line) {
            MoveList legal;
            board::generate_legal_moves(pos, legal);

            bool matched = false;
            for (int i = 0; i < legal.size(); ++i) {
                if (legal[i].to_uci() == uci_move) {
                    // try_emplace, not operator[]: if an earlier line
                    // already recorded a different move for this exact
                    // position (a transposition between two curated
                    // lines), the FIRST line in curated_lines() to
                    // reach it wins -- a simple, deterministic,
                    // documented tie-break, not an arbitrary
                    // last-write-wins overwrite.
                    t.try_emplace(pos.zobrist_hash, uci_move);
                    UndoInfo undo;
                    board::make_move(pos, legal[i], undo);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                // A malformed line (a typo'd square, or a move that
                // isn't actually legal here) -- stop replaying this
                // one line only; every entry already recorded from its
                // earlier moves stays in the table, and every OTHER
                // line in curated_lines() is unaffected.
                break;
            }
        }
    }
}

std::optional<std::string> book_move(const Position& pos) noexcept {
    const std::unordered_map<std::uint64_t, std::string>& t = table();
    const auto it = t.find(pos.zobrist_hash);
    if (it == t.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace nightwing::book
