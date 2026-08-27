// tests/knight_outposts_tests.cpp
//
// Unit tests for src/eval/knight_outposts.h (ROADMAP.md Phase 5's
// "Knight outposts" item). Positions are built directly via
// Position::place_piece(), matching mobility_tests.cpp's/
// king_safety_tests.cpp's/piece_bonuses_tests.cpp's style, specifically
// so each test isolates knight_outpost_value() itself rather than
// relying on a real game position where every eval term interacts at
// once (eval_tests.cpp's job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/masks.h"
#include "eval/knight_outposts.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so table-init state isn't shared across cases -- each
/// must initialize what it needs itself. Matches piece_bonuses_tests.cpp's
/// convention: only init_masks() is needed here (knight_outpost_value()
/// never touches a sliding-piece attack table -- see knight_outposts.h's
/// own precondition comment).
void init_all() {
    init_masks();
}

/// Returns a fully empty position (no pieces, given side to move) --
/// same helper pattern as every other eval/*_tests.cpp file.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("knight_outpost_value: starting position has no outposts", "[eval][knight_outposts]") {
    init_all();
    // Both sides' knights sit on their own back rank (relative rank 0),
    // well outside the outpost rank window (relative ranks 3..5) --
    // trivially zero regardless of pawn support or enemy pawn threats.
    REQUIRE(knight_outpost_value(start_position()) == Score{0, 0});
}

TEST_CASE("knight_outpost_value: a pawn-defended, unreachable knight on e5 scores higher than "
          "the same knight with no pawn support",
          "[eval][knight_outposts]") {
    init_all();
    // e5 (file 4, rank index 4) is relative rank 4 for White -- inside
    // the [3,5] outpost window. d4 (file 3, rank index 3) is one of the
    // two squares (the other is f4) from which a White pawn defends e5
    // (the reverse-pawn-attack trick knight_outposts.cpp itself uses).
    // No enemy pawns anywhere in either config, so the only difference
    // between the two positions is whether the knight is defended.
    Position defended = empty_position();
    defended.place_piece(make_square(4, 4), Piece::WhiteKnight); // e5
    defended.place_piece(make_square(3, 3), Piece::WhitePawn);   // d4
    defended.place_piece(make_square(4, 0), Piece::WhiteKing);
    defended.place_piece(make_square(4, 7), Piece::BlackKing);

    Position undefended = empty_position();
    undefended.place_piece(make_square(4, 4), Piece::WhiteKnight); // e5, no pawn support
    undefended.place_piece(make_square(4, 0), Piece::WhiteKing);
    undefended.place_piece(make_square(4, 7), Piece::BlackKing);

    REQUIRE(knight_outpost_value(defended) == Score{18, 10});
    REQUIRE(knight_outpost_value(undefended) == Score{0, 0});
}

TEST_CASE("knight_outpost_value: an enemy pawn able to eventually recapture on the outpost "
          "square disqualifies it",
          "[eval][knight_outposts]") {
    init_all();
    // Same defended e5 knight as the previous test, but with an extra
    // Black pawn on d6 (file 3, rank index 5) -- inside e5's own
    // enemy-threat span (on an adjacent file, strictly ahead of e5 from
    // White's own perspective) -- so despite being pawn-defended, e5 no
    // longer counts as a safe outpost: that Black pawn already attacks
    // e5 directly (it's exactly one rank ahead, the closest possible
    // case) and would recapture any knight sitting there.
    Position disqualified = empty_position();
    disqualified.place_piece(make_square(4, 4), Piece::WhiteKnight); // e5
    disqualified.place_piece(make_square(3, 3), Piece::WhitePawn);   // d4, still defends
    disqualified.place_piece(make_square(3, 5), Piece::BlackPawn);   // d6, threatens e5
    disqualified.place_piece(make_square(4, 0), Piece::WhiteKing);
    disqualified.place_piece(make_square(4, 7), Piece::BlackKing);

    REQUIRE(knight_outpost_value(disqualified) == Score{0, 0});
}

TEST_CASE("knight_outpost_value: a defended, unreachable knight outside the rank window scores "
          "nothing",
          "[eval][knight_outposts]") {
    init_all();
    // e3 (file 4, rank index 2) is relative rank 2 for White -- one
    // short of the [3,5] outpost window's lower bound. d2 (file 3, rank
    // index 1) defends it via the same reverse-pawn-attack relationship
    // d4 has to e5 in the earlier tests, and no enemy pawn is anywhere
    // near it -- isolating the rank-window restriction as the sole
    // reason this knight doesn't qualify.
    Position out_of_range = empty_position();
    out_of_range.place_piece(make_square(4, 2), Piece::WhiteKnight); // e3
    out_of_range.place_piece(make_square(3, 1), Piece::WhitePawn);   // d2
    out_of_range.place_piece(make_square(4, 0), Piece::WhiteKing);
    out_of_range.place_piece(make_square(4, 7), Piece::BlackKing);

    REQUIRE(knight_outpost_value(out_of_range) == Score{0, 0});
}

TEST_CASE("knight_outpost_value: a qualifying Black knight subtracts from the White-relative "
          "score",
          "[eval][knight_outposts]") {
    init_all();
    // d4 (file 3, rank index 3) is relative rank 4 for Black (7 - 3 =
    // 4), inside the [3,5] window. c5 (file 2, rank index 4) is one of
    // the two squares from which a Black pawn defends d4 (mirroring the
    // d4/e5 relationship the White-side tests above use). No White
    // pawns anywhere, so nothing disqualifies it.
    Position pos = empty_position();
    pos.place_piece(make_square(3, 3), Piece::BlackKnight); // d4
    pos.place_piece(make_square(2, 4), Piece::BlackPawn);   // c5
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);

    REQUIRE(knight_outpost_value(pos) == Score{-18, -10});
}
