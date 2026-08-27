// tests/mobility_tests.cpp
//
// Unit tests for src/eval/mobility.h (ROADMAP.md Phase 5's "Mobility
// eval" item). Positions are built directly via Position::place_piece(),
// matching pawns_tests.cpp's style, specifically so each test isolates
// mobility_value() itself rather than relying on a real game position
// where every eval term interacts at once (that end-to-end interaction
// is eval_tests.cpp's job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "eval/mobility.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so magic-bitboard/attack tables aren't shared across
/// cases -- each must initialize them itself. Matches search_tests.cpp's/
/// eval_tests.cpp's convention exactly (see either for why).
void init_all() {
    init_masks();
    init_magic_bitboards();
}

/// Returns a fully empty position (no pieces, given side to move) --
/// same helper pattern as pawns_tests.cpp/eval_tests.cpp.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("mobility_value: starting position is exactly balanced", "[eval][mobility]") {
    init_all();
    // The starting position is mirror-symmetric between colors (every
    // White piece has a same-type Black piece on the rank-flipped
    // square, and the occupied bitboard as a whole is symmetric under
    // that same flip) -- so every piece's own attacked-square count
    // matches its mirror's exactly, and the White-relative Score this
    // function returns must be precisely {0, 0}, not just "close to
    // balanced."
    REQUIRE(mobility_value(start_position()) == Score{0, 0});
}

TEST_CASE("mobility_value: a king-only position (no knights/bishops/rooks/queens) scores exactly "
          "zero",
          "[eval][mobility]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    // The king itself never contributes to this term (mobility.h's own
    // header comment on why it's excluded) -- with nothing else on the
    // board, every one of the four per-piece-type loops in
    // mobility_value() has nothing to iterate over at all.
    REQUIRE(mobility_value(pos) == Score{0, 0});
}

TEST_CASE("mobility_value: a knight in the center outscores an otherwise-identical knight in "
          "the corner",
          "[eval][mobility]") {
    init_all();
    Position center = empty_position();
    center.place_piece(make_square(0, 0), Piece::WhiteKing);  // a1, out of the knight's way
    center.place_piece(make_square(7, 7), Piece::BlackKing);  // h8
    center.place_piece(make_square(4, 4), Piece::WhiteKnight); // e5: up to 8 attacked squares

    Position corner = empty_position();
    corner.place_piece(make_square(0, 0), Piece::WhiteKing);
    corner.place_piece(make_square(7, 7), Piece::BlackKing);
    corner.place_piece(make_square(7, 0), Piece::WhiteKnight); // h1: exactly 2 attacked squares

    REQUIRE(mobility_value(center).mg > mobility_value(corner).mg);
    REQUIRE(mobility_value(center).eg > mobility_value(corner).eg);
}

TEST_CASE("mobility_value: an unobstructed rook outscores an otherwise-identical rook boxed in "
          "by its own pawns",
          "[eval][mobility]") {
    init_all();
    Position open = empty_position();
    open.place_piece(make_square(0, 0), Piece::WhiteKing);
    open.place_piece(make_square(7, 7), Piece::BlackKing);
    open.place_piece(make_square(3, 3), Piece::WhiteRook); // d4, fully open in all 4 directions

    Position boxed = empty_position();
    boxed.place_piece(make_square(0, 0), Piece::WhiteKing);
    boxed.place_piece(make_square(7, 7), Piece::BlackKing);
    boxed.place_piece(make_square(0, 3), Piece::WhiteRook); // a4
    boxed.place_piece(make_square(0, 4), Piece::WhitePawn); // a5 -- blocks north
    boxed.place_piece(make_square(0, 2), Piece::WhitePawn); // a3 -- blocks south
    boxed.place_piece(make_square(1, 3), Piece::WhitePawn); // b4 -- blocks east

    REQUIRE(mobility_value(open).mg > mobility_value(boxed).mg);
}

TEST_CASE("mobility_value: pseudo-mobility counts an attacked enemy-occupied square, not just "
          "empty ones",
          "[eval][mobility]") {
    init_all();
    // mobility.h's own header comment: this function's mobility
    // definition counts a square an ENEMY piece occupies too (it's a
    // legal capture, even though not a "free" move onto an empty
    // square) -- a slider's attacked-square bitboard already includes
    // the first blocking square along each ray regardless of which
    // color occupies it (mobility.cpp only masks out OWN-occupied
    // squares via `& ~own`), so an enemy piece sitting exactly at a
    // ray's natural edge-terminus changes nothing: the ray would have
    // stopped there anyway (the board edge), so both configurations
    // below are expected to score IDENTICALLY. (An enemy piece placed
    // mid-ray instead, e.g. one square short of the edge, would shorten
    // the ray by one square relative to the empty-board case -- that's
    // a real, correct difference, not a bug, but it would make a poor
    // "still counts an enemy-occupied square" test, since it conflates
    // "does the ray reach that square at all" with "is that square's
    // occupant's color irrelevant to whether it counts": using the
    // edge square isolates just the latter.)
    Position with_target = empty_position();
    with_target.place_piece(make_square(0, 0), Piece::WhiteKing);
    with_target.place_piece(make_square(7, 7), Piece::BlackKing);
    with_target.place_piece(make_square(3, 0), Piece::WhiteRook); // d1
    with_target.place_piece(make_square(3, 7), Piece::BlackPawn); // d8 -- the ray's own edge

    Position no_target = empty_position();
    no_target.place_piece(make_square(0, 0), Piece::WhiteKing);
    no_target.place_piece(make_square(7, 7), Piece::BlackKing);
    no_target.place_piece(make_square(3, 0), Piece::WhiteRook); // d1, same square, empty d8

    REQUIRE(mobility_value(with_target) == mobility_value(no_target));
}
