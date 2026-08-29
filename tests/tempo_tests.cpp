// tests/tempo_tests.cpp
//
// Unit tests for src/eval/tempo.h (ROADMAP.md Phase 5's "Tempo bonus"
// item). Unlike every other eval/*_tests.cpp file this phase, this
// term touches nothing but `pos.side_to_move` (see tempo.h's own
// precondition comment), so no init_masks()/init_magic_bitboards()
// call is needed anywhere in this file, and no piece placement is
// needed at all beyond what board::start_position()/an empty Position
// already provide.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/score.h"
#include "eval/tempo.h"

using namespace nightwing::board;
using namespace nightwing::eval;

TEST_CASE("tempo_value: White to move scores exactly +kTempoBonus", "[eval][tempo]") {
    Position pos = start_position();
    pos.side_to_move = Color::White;
    REQUIRE(tempo_value(pos) == kTempoBonus);
}

TEST_CASE("tempo_value: Black to move scores exactly -kTempoBonus", "[eval][tempo]") {
    Position pos = start_position();
    pos.side_to_move = Color::Black;
    REQUIRE(tempo_value(pos) == -kTempoBonus);
}

TEST_CASE("tempo_value: depends only on side_to_move, not on piece placement", "[eval][tempo]") {
    // A bare, otherwise-empty position (no pieces placed at all beyond
    // Position's own default state) scores identically to the full
    // starting position, given the same side to move -- confirming
    // this term genuinely ignores the board itself, exactly as
    // tempo.h's own header comment says it should.
    Position empty;
    empty.side_to_move = Color::White;
    Position full = start_position();
    full.side_to_move = Color::White;
    REQUIRE(tempo_value(empty) == tempo_value(full));
    REQUIRE(tempo_value(empty) == kTempoBonus);
}

TEST_CASE("tempo_value: flipping side_to_move on the same position exactly negates the score",
          "[eval][tempo]") {
    Position pos = start_position();
    pos.side_to_move = Color::White;
    const Score white_to_move = tempo_value(pos);
    pos.side_to_move = Color::Black;
    const Score black_to_move = tempo_value(pos);
    REQUIRE(white_to_move == -black_to_move);
}

TEST_CASE("tempo_value: kTempoBonus is a nonzero, mg-above-eg bonus (never negative in either "
          "phase)",
          "[eval][tempo]") {
    // Sanity-checks the constant itself against tempo.h's own
    // documented tapering direction (mg above eg, both positive) --
    // not a behavioral test of tempo_value() beyond what the tests
    // above already cover, but pins the constant's shape so a future
    // Texel-tuning pass that silently inverted it would be caught here
    // rather than only showing up as a confusing regression-bench
    // delta.
    REQUIRE(kTempoBonus.mg > 0);
    REQUIRE(kTempoBonus.eg > 0);
    REQUIRE(kTempoBonus.mg >= kTempoBonus.eg);
}
