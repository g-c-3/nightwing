// tests/endgame_tests.cpp
//
// Unit tests for src/eval/endgame.h's classify_endgame() (ROADMAP.md
// Phase 6's first item, "Material-signature classifier"). Positions
// are built directly via Position::place_piece(), matching every
// other eval/*_tests.cpp file's style (see e.g.
// material_imbalance_tests.cpp's own header comment for why). This
// file is the "dedicated endgame test suite" ARCHITECTURE.md's own
// Module Layout/Testing Policy sections already named in advance
// (`tests/endgame_tests.cpp`) — later Phase 6 items (K+P theory, rook
// endgame patterns, KPK/KRK/KBNK exact play, ...) are expected to add
// their own TEST_CASEs here as they land, alongside the classifier
// tests below.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "eval/endgame.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("classify_endgame: the starting position is None -- far too much material for any "
          "recognized bucket",
          "[eval][endgame]") {
    REQUIRE(classify_endgame(start_position()) == EndgameSignature::None);
}

TEST_CASE("classify_endgame: bare king vs. bare king is None -- not a recognized bucket here "
          "(insufficient-material draw detection is a separate, already-existing concern, "
          "ROADMAP.md Phase 6's own draw-detection-refinement item, not this classifier's job)",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    REQUIRE(classify_endgame(pos) == EndgameSignature::None);
}

TEST_CASE("classify_endgame: KPK -- a single White pawn, nothing else, is KPK", "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 1), Piece::WhitePawn);
    REQUIRE(classify_endgame(pos) == EndgameSignature::KPK);
}

TEST_CASE("classify_endgame: KPK -- a single BLACK pawn, nothing else, is also KPK (the bucket "
          "doesn't care which side the lone pawn belongs to)",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 6), Piece::BlackPawn);
    REQUIRE(classify_endgame(pos) == EndgameSignature::KPK);
}

TEST_CASE("classify_endgame: two pawns (one each side) is None, not KPK -- KPK is specifically "
          "exactly one pawn on the whole board",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 1), Piece::WhitePawn);
    pos.place_piece(make_square(3, 6), Piece::BlackPawn);
    REQUIRE(classify_endgame(pos) == EndgameSignature::None);
}

TEST_CASE("classify_endgame: KRK -- a single rook, no pawns, nothing else, is KRK",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    REQUIRE(classify_endgame(pos) == EndgameSignature::KRK);
}

TEST_CASE("classify_endgame: a rook plus a pawn is None, not KRK -- KRK requires zero pawns",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(4, 1), Piece::WhitePawn);
    REQUIRE(classify_endgame(pos) == EndgameSignature::None);
}

TEST_CASE("classify_endgame: KBNK -- White's bishop AND knight, no pawns, Black bare, is KBNK",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    REQUIRE(classify_endgame(pos) == EndgameSignature::KBNK);
}

TEST_CASE("classify_endgame: KBNK also recognized with BOTH minors on Black's side instead",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop);
    pos.place_piece(make_square(1, 7), Piece::BlackKnight);
    REQUIRE(classify_endgame(pos) == EndgameSignature::KBNK);
}

TEST_CASE("classify_endgame: a bishop on White's side and a knight on Black's side (split across "
          "sides) is KnightVsBishop, NOT KBNK -- this is the exact case this file's own header "
          "comment on classify_endgame() calls out as needing an explicit same-side check",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop);
    pos.place_piece(make_square(1, 7), Piece::BlackKnight);
    REQUIRE(classify_endgame(pos) == EndgameSignature::KnightVsBishop);
}

TEST_CASE("classify_endgame: KnightVsBishop also recognized with pawns on the board -- unlike "
          "KBNK, this bucket allows any pawn count (endgame.h's own doc comment: the whole point "
          "is weighing the tradeoff BY pawn structure)",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);
    pos.place_piece(make_square(2, 7), Piece::BlackBishop);
    pos.place_piece(make_square(0, 1), Piece::WhitePawn);
    pos.place_piece(make_square(0, 6), Piece::BlackPawn);
    pos.place_piece(make_square(3, 1), Piece::WhitePawn);
    pos.place_piece(make_square(3, 6), Piece::BlackPawn);
    REQUIRE(classify_endgame(pos) == EndgameSignature::KnightVsBishop);
}

TEST_CASE("classify_endgame: OppositeColoredBishops -- one bishop each, on opposite-colored "
          "squares, is recognized",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1 -- dark square
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8 -- light square
    REQUIRE(classify_endgame(pos) == EndgameSignature::OppositeColoredBishops);
}

TEST_CASE("classify_endgame: same-colored bishops (one each) is None, NOT "
          "OppositeColoredBishops -- endgame.h's own doc comment: no drawish tendency, and "
          "nothing in ROADMAP.md Phase 6 calls for handling this case specially",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1 -- dark square
    pos.place_piece(make_square(5, 7), Piece::BlackBishop); // f8 -- dark square (same color)
    REQUIRE(classify_endgame(pos) == EndgameSignature::None);
}

TEST_CASE("classify_endgame: OppositeColoredBishops still recognized with pawns on the board",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1 -- dark
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8 -- light
    pos.place_piece(make_square(0, 1), Piece::WhitePawn);
    pos.place_piece(make_square(0, 6), Piece::BlackPawn);
    REQUIRE(classify_endgame(pos) == EndgameSignature::OppositeColoredBishops);
}

TEST_CASE("classify_endgame: RookEndgame -- one rook each, plus pawns, is recognized",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(0, 7), Piece::BlackRook);
    pos.place_piece(make_square(3, 1), Piece::WhitePawn);
    pos.place_piece(make_square(3, 6), Piece::BlackPawn);
    REQUIRE(classify_endgame(pos) == EndgameSignature::RookEndgame);
}

TEST_CASE("classify_endgame: RookEndgame also recognized with zero pawns", "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(0, 7), Piece::BlackRook);
    REQUIRE(classify_endgame(pos) == EndgameSignature::RookEndgame);
}

TEST_CASE("classify_endgame: two rooks on the same side is None, not RookEndgame -- RookEndgame "
          "requires exactly one rook EACH side",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);
    pos.place_piece(make_square(7, 0), Piece::WhiteRook);
    pos.place_piece(make_square(0, 7), Piece::BlackRook);
    REQUIRE(classify_endgame(pos) == EndgameSignature::None);
}

TEST_CASE("classify_endgame: a queen anywhere on the board rules out every bucket, even "
          "otherwise-matching material (e.g. a KPK-shaped position plus a queen)",
          "[eval][endgame]") {
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(4, 1), Piece::WhitePawn);
    pos.place_piece(make_square(3, 0), Piece::WhiteQueen);
    REQUIRE(classify_endgame(pos) == EndgameSignature::None);
}
