// tests/board_tests.cpp
//
// Unit tests for src/board/board.h — Piece/Color/PieceType encoding,
// Position's start_position() factory, and internal consistency between
// piece_bb and the piece_on mailbox.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"

using namespace nightwing::board;

TEST_CASE("make_piece / piece_type_of / color_of round-trip", "[board]") {
    REQUIRE(make_piece(Color::White, PieceType::Pawn) == Piece::WhitePawn);
    REQUIRE(make_piece(Color::Black, PieceType::King) == Piece::BlackKing);

    REQUIRE(piece_type_of(Piece::WhiteQueen) == PieceType::Queen);
    REQUIRE(piece_type_of(Piece::BlackKnight) == PieceType::Knight);

    REQUIRE(color_of(Piece::WhiteRook) == Color::White);
    REQUIRE(color_of(Piece::BlackBishop) == Color::Black);
}

TEST_CASE("opposite() flips color", "[board]") {
    REQUIRE(opposite(Color::White) == Color::Black);
    REQUIRE(opposite(Color::Black) == Color::White);
}

TEST_CASE("start_position has correct side to move and rights", "[board]") {
    Position pos = start_position();
    REQUIRE(pos.side_to_move == Color::White);
    REQUIRE(pos.castling_rights == castling::kAll);
    REQUIRE(pos.en_passant_square == kNoEnPassantSquare);
    REQUIRE(pos.halfmove_clock == 0);
    REQUIRE(pos.fullmove_number == 1);
}

TEST_CASE("start_position has 16 pawns, 8 per side", "[board]") {
    Position pos = start_position();
    REQUIRE(popcount(pos.pieces(Color::White, PieceType::Pawn)) == 8);
    REQUIRE(popcount(pos.pieces(Color::Black, PieceType::Pawn)) == 8);
}

TEST_CASE("start_position has correct piece counts per type", "[board]") {
    Position pos = start_position();
    for (Color c : {Color::White, Color::Black}) {
        REQUIRE(popcount(pos.pieces(c, PieceType::Pawn)) == 8);
        REQUIRE(popcount(pos.pieces(c, PieceType::Knight)) == 2);
        REQUIRE(popcount(pos.pieces(c, PieceType::Bishop)) == 2);
        REQUIRE(popcount(pos.pieces(c, PieceType::Rook)) == 2);
        REQUIRE(popcount(pos.pieces(c, PieceType::Queen)) == 1);
        REQUIRE(popcount(pos.pieces(c, PieceType::King)) == 1);
    }
}

TEST_CASE("start_position places specific pieces on specific squares", "[board]") {
    Position pos = start_position();
    REQUIRE(pos.piece_at(make_square(0, 0)) == Piece::WhiteRook);   // a1
    REQUIRE(pos.piece_at(make_square(4, 0)) == Piece::WhiteKing);   // e1
    REQUIRE(pos.piece_at(make_square(3, 0)) == Piece::WhiteQueen);  // d1
    REQUIRE(pos.piece_at(make_square(4, 7)) == Piece::BlackKing);   // e8
    REQUIRE(pos.piece_at(make_square(3, 7)) == Piece::BlackQueen);  // d8
    REQUIRE(pos.piece_at(make_square(0, 1)) == Piece::WhitePawn);   // a2
    REQUIRE(pos.piece_at(make_square(0, 6)) == Piece::BlackPawn);   // a7
}

TEST_CASE("start_position middle ranks and occupied() are empty/consistent", "[board]") {
    Position pos = start_position();
    for (int rank = 2; rank <= 5; ++rank) {
        for (int file = 0; file < kNumFiles; ++file) {
            REQUIRE(pos.is_empty(make_square(file, rank)));
        }
    }
    // 32 total pieces on a fresh board.
    REQUIRE(popcount(pos.occupied()) == 32);
}

TEST_CASE("piece_bb and piece_on mailbox agree on every square", "[board]") {
    Position pos = start_position();
    for (Square sq = 0; sq < kNumSquares; ++sq) {
        const Piece p = pos.piece_at(sq);
        if (p == Piece::None) {
            REQUIRE_FALSE(test_bit(pos.occupied(), sq));
            continue;
        }
        const Bitboard bb = pos.pieces(color_of(p), piece_type_of(p));
        REQUIRE(test_bit(bb, sq));
        REQUIRE(test_bit(pos.occupancy[static_cast<std::size_t>(color_of(p))], sq));
    }
}

TEST_CASE("occupancy[White] and occupancy[Black] don't overlap", "[board]") {
    Position pos = start_position();
    REQUIRE((pos.occupancy[static_cast<std::size_t>(Color::White)] &
             pos.occupancy[static_cast<std::size_t>(Color::Black)]) == kEmptyBitboard);
}

TEST_CASE("to_string renders a recognizable start position", "[board]") {
    Position pos = start_position();
    std::string s = to_string(pos);

    // 8 ranks * (8 chars + newline).
    REQUIRE(s.size() == 72);

    // Rank 8 (Black back rank) is the first row printed.
    REQUIRE(s.substr(0, 8) == "rnbqkbnr");
    // Rank 7 (Black pawns).
    REQUIRE(s.substr(9, 8) == "pppppppp");
    // Rank 2 (White pawns).
    REQUIRE(s.substr(54, 8) == "PPPPPPPP");
    // Rank 1 (White back rank), last row printed.
    REQUIRE(s.substr(63, 8) == "RNBQKBNR");
}
