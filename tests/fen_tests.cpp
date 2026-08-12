// tests/fen_tests.cpp
//
// Unit tests for src/board/fen.h — the parser/serializer itself, as
// distinct from perft_tests.cpp's use of it to build reference positions.

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "board/board.h"
#include "board/fen.h"

using namespace nightwing::board;

TEST_CASE("parse_fen: starting position matches start_position()", "[fen]") {
    Position parsed = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Position expected = start_position();
    REQUIRE(parsed.piece_bb == expected.piece_bb);
    REQUIRE(parsed.occupancy == expected.occupancy);
    REQUIRE(parsed.piece_on == expected.piece_on);
    REQUIRE(parsed.side_to_move == expected.side_to_move);
    REQUIRE(parsed.castling_rights == expected.castling_rights);
    REQUIRE(parsed.en_passant_square == expected.en_passant_square);
    REQUIRE(parsed.zobrist_hash == expected.zobrist_hash);
}

TEST_CASE("to_fen: starting position serializes to the canonical string", "[fen]") {
    REQUIRE(to_fen(start_position()) == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST_CASE("parse_fen -> to_fen round-trips for a variety of positions", "[fen]") {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
        "4k3/8/8/8/8/8/8/4K3 b - - 5 20",
    };
    for (const char* fen : fens) {
        Position pos = parse_fen(fen);
        REQUIRE(to_fen(pos) == std::string(fen));
    }
}

TEST_CASE("parse_fen: castling '-' and en passant '-' parse as no rights/no target", "[fen]") {
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    REQUIRE(pos.castling_rights == 0);
    REQUIRE(pos.en_passant_square == kNoEnPassantSquare);
}

TEST_CASE("parse_fen: en passant square parses to the correct square index", "[fen]") {
    Position pos = parse_fen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
    REQUIRE(pos.en_passant_square == make_square(3, 5)); // d6
}

TEST_CASE("parse_fen: halfmove/fullmove default to 0/1 when the fields are omitted", "[fen]") {
    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
    REQUIRE(pos.halfmove_clock == 0);
    REQUIRE(pos.fullmove_number == 1);
}

TEST_CASE("parse_fen: throws std::invalid_argument on malformed input", "[fen]") {
    REQUIRE_THROWS_AS(parse_fen(""), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("not a fen string"), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w KQkq - 0 1"),
                       std::invalid_argument); // 7 files on the back rank, not 8
    REQUIRE_THROWS_AS(parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNRX w KQkq - 0 1"),
                       std::invalid_argument); // unrecognized piece character
    REQUIRE_THROWS_AS(parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"),
                       std::invalid_argument); // side to move must be w/b
}
