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

// ---------------------------------------------------------------------------
// Legality validation (2026-10-18, external review batch 1 (a)). Every
// rejected FEN below previously parsed and then crashed movegen or search.
// ---------------------------------------------------------------------------

TEST_CASE("parse_fen: rejects a position without exactly one king per side", "[fen][validation]") {
    REQUIRE_THROWS_AS(parse_fen("8/8/8/8/8/8/8/8 w - - 0 1"), std::invalid_argument);            // no kings
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/8 w - - 0 1"), std::invalid_argument);          // white king missing
    REQUIRE_THROWS_AS(parse_fen("8/8/8/8/8/8/8/4K3 w - - 0 1"), std::invalid_argument);          // black king missing
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/3KK3 w - - 0 1"), std::invalid_argument);       // two white kings
    REQUIRE_THROWS_AS(parse_fen("3kk3/8/8/8/8/8/8/4K3 w - - 0 1"), std::invalid_argument);       // two black kings
}

TEST_CASE("parse_fen: rejects adjacent kings", "[fen][validation]") {
    REQUIRE_THROWS_AS(parse_fen("8/8/8/3kK3/8/8/8/8 w - - 0 1"), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("8/8/8/8/8/8/3kK3/8 b - - 0 1"), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("8/8/8/4k3/3K4/8/8/8 w - - 0 1"), std::invalid_argument); // diagonal
}

TEST_CASE("parse_fen: rejects pawns on rank 1 or rank 8", "[fen][validation]") {
    REQUIRE_THROWS_AS(parse_fen("P3k3/8/8/8/8/8/8/4K3 w - - 0 1"), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/p3K3 w - - 0 1"), std::invalid_argument);
}

TEST_CASE("parse_fen: rejects a position where the side not to move is in check", "[fen][validation]") {
    // Black king e8 attacked by the rook on e7 with White to move: Black
    // would have had to leave its own king in check.
    REQUIRE_THROWS_AS(parse_fen("4k3/4R3/8/8/8/8/8/4K3 w - - 0 1"), std::invalid_argument);
    // Mirror: White king e1 attacked by a black rook, Black to move.
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/4r3/4K3 b - - 0 1"), std::invalid_argument);
    // Diagonal slider, knight and pawn checks are caught too.
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/1b6/8/8/4K3 b - - 0 1"), std::invalid_argument); // bishop b4 -> e1
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/3n4/8/4K3 b - - 0 1"), std::invalid_argument); // knight d3 -> e1
    REQUIRE_THROWS_AS(parse_fen("4k3/3P4/8/8/8/8/8/4K3 w - - 0 1"), std::invalid_argument); // pawn d7 -> e8
}

TEST_CASE("parse_fen: accepts the side to move being in check", "[fen][validation]") {
    // Check against the side to move is an ordinary position.
    REQUIRE_NOTHROW(parse_fen("4k3/4R3/8/8/8/8/8/4K3 b - - 0 1"));
    REQUIRE_NOTHROW(parse_fen("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1"));
}

TEST_CASE("parse_fen: rejects castling rights without the king and rook on their home squares",
          "[fen][validation]") {
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/4K3 w K - 0 1"), std::invalid_argument);  // no rook
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/4K3 w Q - 0 1"), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/4K3 w k - 0 1"), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/4K3 w q - 0 1"), std::invalid_argument);
    REQUIRE_THROWS_AS(parse_fen("4k2r/8/8/8/8/8/8/3K3R w KQkq - 0 1"), std::invalid_argument); // white king off e1
    REQUIRE_THROWS_AS(parse_fen("3k3r/8/8/8/8/8/8/4K2R w Kk - 0 1"), std::invalid_argument);  // black king off e8
    REQUIRE_THROWS_AS(parse_fen("4k3/8/8/8/8/8/8/4K2N w K - 0 1"), std::invalid_argument);    // wrong piece on h1
    REQUIRE_THROWS_AS(parse_fen("4k2r/8/8/8/8/8/8/4K2r w K - 0 1"), std::invalid_argument);   // enemy rook on h1
}

TEST_CASE("parse_fen: accepts castling rights that are backed by king and rook", "[fen][validation]") {
    REQUIRE_NOTHROW(parse_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    REQUIRE_NOTHROW(parse_fen("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1"));
    REQUIRE_NOTHROW(parse_fen("r3k3/8/8/8/8/8/8/4K3 b q - 0 1"));
}

TEST_CASE("parse_fen: validates the en passant square against the side to move", "[fen][validation]") {
    // Valid, capturable (existing fixture shape): White to move, Black just played d7-d5.
    REQUIRE_NOTHROW(parse_fen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"));
    // Valid but no pawn can capture: GUIs emit this after every double push, so it is accepted.
    REQUIRE_NOTHROW(parse_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"));

    // Wrong rank for the side to move (a rank-3 target with White to move).
    REQUIRE_THROWS_AS(parse_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e3 0 1"),
                       std::invalid_argument);
    // Target square occupied.
    REQUIRE_THROWS_AS(parse_fen("rnbqkbnr/ppp1pppp/3p4/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"),
                       std::invalid_argument);
    // No enemy pawn in front of the target square.
    REQUIRE_THROWS_AS(parse_fen("rnbqkbnr/ppp1pppp/8/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"),
                       std::invalid_argument);
    // The pawn's origin square (d7) is still occupied, so it cannot have just double-pushed.
    REQUIRE_THROWS_AS(parse_fen("rnbqkbnr/pppppppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"),
                       std::invalid_argument);
}

TEST_CASE("parse_fen: the validation message names the violated rule", "[fen][validation]") {
    try {
        (void)parse_fen("8/8/8/8/8/8/8/8 w - - 0 1");
        FAIL("expected std::invalid_argument");
    } catch (const std::invalid_argument& e) {
        REQUIRE(std::string(e.what()).find("exactly one king") != std::string::npos);
    }
}
