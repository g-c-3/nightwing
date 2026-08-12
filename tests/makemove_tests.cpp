// tests/makemove_tests.cpp
//
// Unit tests for Position::make_move()/unmake_move() (board.h/.cpp).
// Every test checks two things: (1) the incrementally-updated
// zobrist_hash matches a from-scratch compute_hash() after make_move,
// per zobrist.h's stated cross-check intent, and (2) unmake_move()
// restores the position to byte-for-byte the same state it started in.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/move.h"
#include "board/zobrist.h"

using namespace nightwing::board;

namespace {

/// Full-field equality check. Position has no operator== (deliberately —
/// see board.h, it's a hot-path struct with no need for one outside
/// tests), so tests compare every field directly.
bool positions_equal(const Position& a, const Position& b) {
    if (a.piece_bb != b.piece_bb) return false;
    if (a.occupancy != b.occupancy) return false;
    if (a.piece_on != b.piece_on) return false;
    if (a.side_to_move != b.side_to_move) return false;
    if (a.castling_rights != b.castling_rights) return false;
    if (a.en_passant_square != b.en_passant_square) return false;
    if (a.halfmove_clock != b.halfmove_clock) return false;
    if (a.fullmove_number != b.fullmove_number) return false;
    if (a.zobrist_hash != b.zobrist_hash) return false;
    return true;
}

} // namespace

TEST_CASE("make_move then unmake_move exactly restores the start position", "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos = start_position();
    const Position original = pos;

    UndoInfo undo;
    const Move e2e4(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    make_move(pos, e2e4, undo);

    REQUIRE(pos.side_to_move == Color::Black);
    REQUIRE(pos.en_passant_square == make_square(4, 2)); // e3
    REQUIRE(pos.is_empty(make_square(4, 1)));
    REQUIRE(pos.piece_at(make_square(4, 3)) == Piece::WhitePawn);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));

    unmake_move(pos, e2e4, undo);
    REQUIRE(positions_equal(pos, original));
}

TEST_CASE("a short realistic opening sequence stays hash-consistent and fully unwinds", "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos = start_position();
    const Position original = pos;

    // 1. e4 e5 2. Nf3 Nc6 3. Bb5 (Ruy Lopez) — five ordinary legal moves,
    // hand-constructed (no movegen dependency needed for known-legal moves).
    const Move moves[5] = {
        Move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush), // e2e4
        Move(make_square(4, 6), make_square(4, 4), MoveFlag::DoublePawnPush), // e7e5
        Move(make_square(6, 0), make_square(5, 2), MoveFlag::Quiet),          // Ng1f3
        Move(make_square(1, 7), make_square(2, 5), MoveFlag::Quiet),          // Nb8c6
        Move(make_square(5, 0), make_square(1, 4), MoveFlag::Quiet),          // Bf1b5
    };
    UndoInfo undo[5];

    for (int i = 0; i < 5; ++i) {
        make_move(pos, moves[i], undo[i]);
        REQUIRE(pos.zobrist_hash == compute_hash(pos));
    }
    REQUIRE(pos.fullmove_number == 3);
    REQUIRE(pos.side_to_move == Color::Black);

    for (int i = 4; i >= 0; --i) {
        unmake_move(pos, moves[i], undo[i]);
    }
    REQUIRE(positions_equal(pos, original));
}

TEST_CASE("capture resets the halfmove clock and unmake restores the captured piece", "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    // White King a1, White Rook a3, Black Rook a6, Black King h8.
    // Ra3xa6 is a simple, unambiguous rook capture.
    Position pos;
    pos.side_to_move = Color::White;
    pos.castling_rights = 0;
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    pos.place_piece(make_square(0, 2), Piece::WhiteRook); // a3
    pos.place_piece(make_square(0, 5), Piece::BlackRook); // a6
    pos.place_piece(make_square(7, 7), Piece::BlackKing); // h8
    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);
    pos.halfmove_clock = 17;
    const Position original = pos;

    UndoInfo undo;
    const Move rxa6(make_square(0, 2), make_square(0, 5), MoveFlag::Capture);
    make_move(pos, rxa6, undo);

    REQUIRE(pos.halfmove_clock == 0);
    REQUIRE(pos.piece_at(make_square(0, 5)) == Piece::WhiteRook);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));

    unmake_move(pos, rxa6, undo);
    REQUIRE(positions_equal(pos, original));
}

TEST_CASE("en passant capture removes the correct pawn and unmake restores both pawns", "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    // White King a1, White pawn e5, Black pawn d5 (just double-pushed,
    // en passant target d6), Black King a8. White to move: exd6 e.p.
    Position pos;
    pos.side_to_move = Color::White;
    pos.castling_rights = 0;
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    pos.place_piece(make_square(4, 4), Piece::WhitePawn); // e5
    pos.place_piece(make_square(3, 4), Piece::BlackPawn); // d5
    pos.place_piece(make_square(7, 7), Piece::BlackKing); // h8
    pos.en_passant_square = make_square(3, 5);             // d6
    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);
    const Position original = pos;

    UndoInfo undo;
    const Move exd6(make_square(4, 4), make_square(3, 5), MoveFlag::EnPassant);
    make_move(pos, exd6, undo);

    REQUIRE(pos.piece_at(make_square(3, 5)) == Piece::WhitePawn); // capturing pawn landed on d6
    REQUIRE(pos.is_empty(make_square(4, 4)));                     // e5 vacated
    REQUIRE(pos.is_empty(make_square(3, 4)));                     // captured black pawn gone from d5
    REQUIRE(pos.en_passant_square == kNoEnPassantSquare);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));

    unmake_move(pos, exd6, undo);
    REQUIRE(positions_equal(pos, original));
}

TEST_CASE("promotion-with-capture places the promoted piece and unmake restores pawn + captured piece",
          "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    // White King a1, White pawn b7, Black Rook a8 (also White's promotion
    // corner target for a different test below — here just a capturable
    // piece), Black King h8. bxa8=Q.
    Position pos;
    pos.side_to_move = Color::White;
    pos.castling_rights = 0;
    pos.place_piece(make_square(0, 0), Piece::WhiteKing); // a1
    pos.place_piece(make_square(1, 6), Piece::WhitePawn); // b7
    pos.place_piece(make_square(0, 7), Piece::BlackRook); // a8
    pos.place_piece(make_square(7, 7), Piece::BlackKing); // h8
    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);
    const Position original = pos;

    UndoInfo undo;
    const Move bxa8q(make_square(1, 6), make_square(0, 7), MoveFlag::PromoCaptureQueen);
    make_move(pos, bxa8q, undo);

    REQUIRE(pos.piece_at(make_square(0, 7)) == Piece::WhiteQueen);
    REQUIRE(pos.is_empty(make_square(1, 6)));
    REQUIRE(pos.halfmove_clock == 0);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));

    unmake_move(pos, bxa8q, undo);
    REQUIRE(positions_equal(pos, original));
    REQUIRE(pos.piece_at(make_square(1, 6)) == Piece::WhitePawn);
    REQUIRE(pos.piece_at(make_square(0, 7)) == Piece::BlackRook);
}

TEST_CASE("kingside castling moves the rook too and unmake restores both pieces and rights",
          "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(7, 0), Piece::WhiteRook); // h1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.castling_rights = castling::kAll;
    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);
    const Position original = pos;

    UndoInfo undo;
    const Move o_o(make_square(4, 0), make_square(6, 0), MoveFlag::KingCastle);
    make_move(pos, o_o, undo);

    REQUIRE(pos.piece_at(make_square(6, 0)) == Piece::WhiteKing); // g1
    REQUIRE(pos.piece_at(make_square(5, 0)) == Piece::WhiteRook); // f1
    REQUIRE(pos.is_empty(make_square(4, 0)));
    REQUIRE(pos.is_empty(make_square(7, 0)));
    REQUIRE((pos.castling_rights & castling::kWhiteKingside) == 0);
    REQUIRE((pos.castling_rights & castling::kWhiteQueenside) == 0);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));

    unmake_move(pos, o_o, undo);
    REQUIRE(positions_equal(pos, original));
}

TEST_CASE("a rook move off its home square revokes only that side's castling right", "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(0, 0), Piece::WhiteRook); // a1
    pos.place_piece(make_square(7, 0), Piece::WhiteRook); // h1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.castling_rights = castling::kAll;
    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);

    UndoInfo undo;
    const Move ra1b1(make_square(0, 0), make_square(1, 0), MoveFlag::Quiet);
    make_move(pos, ra1b1, undo);

    REQUIRE((pos.castling_rights & castling::kWhiteQueenside) == 0);
    REQUIRE((pos.castling_rights & castling::kWhiteKingside) != 0);
    REQUIRE((pos.castling_rights & castling::kBlackKingside) != 0);
    REQUIRE((pos.castling_rights & castling::kBlackQueenside) != 0);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));
}

TEST_CASE("capturing an enemy rook on its home corner revokes that side's castling right",
          "[makemove]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos;
    pos.side_to_move = Color::White;
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);  // e1
    pos.place_piece(make_square(0, 1), Piece::WhiteQueen); // a2, attacks straight up the a-file
    pos.place_piece(make_square(0, 7), Piece::BlackRook);  // a8
    pos.place_piece(make_square(4, 7), Piece::BlackKing);  // e8
    pos.castling_rights = castling::kAll;
    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);

    UndoInfo undo;
    const Move qxa8(make_square(0, 1), make_square(0, 7), MoveFlag::Capture);
    make_move(pos, qxa8, undo);

    REQUIRE((pos.castling_rights & castling::kBlackQueenside) == 0);
    REQUIRE((pos.castling_rights & castling::kBlackKingside) != 0);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));
}
