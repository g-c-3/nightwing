// tests/rook_files_tests.cpp
//
// Unit tests for src/eval/rook_files.h (candidate 3 of ROADMAP.md's
// "Restricted nonlinear feature interactions" item). Positions are built
// directly via Position::place_piece(), matching the other eval tests.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/rook_files.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

Position empty_position() {
    init_masks();
    Position pos;
    pos.side_to_move = Color::White;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

void add(Position& pos, Color c, PieceType t, int file, int rank) {
    pos.place_piece(make_square(file, rank), make_piece(c, t));
}

} // namespace

TEST_CASE("rook_files_value: no rooks scores zero", "[eval][rook_files]") {
    Position pos = empty_position();
    add(pos, Color::White, PieceType::King, 4, 0);
    add(pos, Color::Black, PieceType::King, 4, 7);
    REQUIRE(rook_files_value(pos) == Score{});
}

TEST_CASE("rook_files_value: one White rook on a pawnless board counts 8 open files",
          "[eval][rook_files]") {
    Position pos = empty_position();
    add(pos, Color::White, PieceType::King, 4, 0);
    add(pos, Color::Black, PieceType::King, 4, 7);
    add(pos, Color::White, PieceType::Rook, 0, 0);
    REQUIRE(rook_files_value(pos) == kRookOpenFilesBonus * 8);
}

TEST_CASE("rook_files_value: the rook's own file does not matter (distinct from "
          "piece_bonuses)", "[eval][rook_files]") {
    Position a = empty_position();
    add(a, Color::White, PieceType::King, 4, 0);
    add(a, Color::Black, PieceType::King, 4, 7);
    add(a, Color::White, PieceType::Rook, 0, 0);
    add(a, Color::White, PieceType::Pawn, 3, 1);
    add(a, Color::Black, PieceType::Pawn, 3, 6);

    Position b = empty_position();
    add(b, Color::White, PieceType::King, 4, 0);
    add(b, Color::Black, PieceType::King, 4, 7);
    add(b, Color::White, PieceType::Rook, 3, 0); // behind own pawn on the d-file
    add(b, Color::White, PieceType::Pawn, 3, 1);
    add(b, Color::Black, PieceType::Pawn, 3, 6);

    REQUIRE(rook_files_value(a) == rook_files_value(b));
    REQUIRE(rook_files_value(a) == kRookOpenFilesBonus * 7);
}

TEST_CASE("rook_files_value: semi-open files are side specific", "[eval][rook_files]") {
    Position pos = empty_position();
    add(pos, Color::White, PieceType::King, 4, 0);
    add(pos, Color::Black, PieceType::King, 4, 7);
    add(pos, Color::White, PieceType::Rook, 0, 0);
    add(pos, Color::Black, PieceType::Pawn, 3, 6); // d-file: semi-open for White only
    REQUIRE(rook_files_value(pos) == kRookOpenFilesBonus * 7 + kRookSemiOpenFilesBonus * 1);
}

TEST_CASE("rook_files_value: colour symmetry (mirrored position negates)", "[eval][rook_files]") {
    Position w = empty_position();
    add(w, Color::White, PieceType::King, 4, 0);
    add(w, Color::Black, PieceType::King, 4, 7);
    add(w, Color::White, PieceType::Rook, 0, 0);
    add(w, Color::Black, PieceType::Pawn, 3, 6);

    Position b = empty_position();
    add(b, Color::Black, PieceType::King, 4, 7);
    add(b, Color::White, PieceType::King, 4, 0);
    add(b, Color::Black, PieceType::Rook, 0, 7);
    add(b, Color::White, PieceType::Pawn, 3, 1);

    REQUIRE(rook_files_value(b) == -rook_files_value(w));
}

TEST_CASE("rook_files_value: rook count is capped at two", "[eval][rook_files]") {
    Position two = empty_position();
    add(two, Color::White, PieceType::King, 4, 0);
    add(two, Color::Black, PieceType::King, 4, 7);
    add(two, Color::White, PieceType::Rook, 0, 0);
    add(two, Color::White, PieceType::Rook, 7, 0);

    Position three = two;
    add(three, Color::White, PieceType::Rook, 1, 0);

    REQUIRE(rook_files_value(two) == kRookOpenFilesBonus * 16);
    REQUIRE(rook_files_value(three) == rook_files_value(two));
}

TEST_CASE("rook_files_value: default weights override matches compiled-in constants; "
          "a perturbed override changes the result", "[eval][rook_files]") {
    Position pos = empty_position();
    add(pos, Color::White, PieceType::King, 4, 0);
    add(pos, Color::Black, PieceType::King, 4, 7);
    add(pos, Color::White, PieceType::Rook, 0, 0);
    add(pos, Color::Black, PieceType::Pawn, 3, 6);

    const RookFilesWeights defaults = default_rook_files_weights();
    REQUIRE(rook_files_value(pos, &defaults) == rook_files_value(pos));

    RookFilesWeights boosted = defaults;
    boosted.open_mg += 10.0;
    REQUIRE(rook_files_value(pos, &boosted).mg == rook_files_value(pos).mg + 10 * 7);
}
