// tests/zobrist_tests.cpp
//
// Unit tests for src/board/zobrist.h — key generation determinism/
// idempotency, hash sensitivity to every component (piece placement,
// side to move, castling rights, en passant), and that start_position()
// wires up a correct, non-zero hash automatically.

#include <catch2/catch_test_macros.hpp>

#include "board/board.h"
#include "board/zobrist.h"

using namespace nightwing::board;

TEST_CASE("init_zobrist_keys is idempotent and deterministic", "[zobrist]") {
    init_zobrist_keys();
    Position pos = start_position();
    const std::uint64_t hash_before = compute_hash(pos);

    init_zobrist_keys(); // second call must be a no-op, not regenerate keys
    const std::uint64_t hash_after = compute_hash(pos);

    REQUIRE(hash_before == hash_after);
}

TEST_CASE("start_position() sets a non-zero, correct zobrist_hash", "[zobrist]") {
    init_zobrist_keys();
    Position pos = start_position();
    REQUIRE(pos.zobrist_hash != 0);
    REQUIRE(pos.zobrist_hash == compute_hash(pos));
}

TEST_CASE("hash changes when a piece is removed", "[zobrist]") {
    init_zobrist_keys();
    Position pos = start_position();
    const std::uint64_t before = compute_hash(pos);

    // Directly mutate placement (no make/unmake yet) to isolate the hash's
    // sensitivity to piece placement specifically.
    clear_bit(pos.piece_bb[static_cast<std::size_t>(Color::White)]
                          [static_cast<std::size_t>(PieceType::Pawn)],
              make_square(0, 1)); // remove White pawn from a2
    clear_bit(pos.occupancy[static_cast<std::size_t>(Color::White)], make_square(0, 1));
    pos.piece_on[static_cast<std::size_t>(make_square(0, 1))] = Piece::None;

    REQUIRE(compute_hash(pos) != before);
}

TEST_CASE("hash changes with side to move", "[zobrist]") {
    init_zobrist_keys();
    Position pos = start_position();
    const std::uint64_t white_to_move_hash = compute_hash(pos);

    pos.side_to_move = Color::Black;
    const std::uint64_t black_to_move_hash = compute_hash(pos);

    REQUIRE(white_to_move_hash != black_to_move_hash);

    // Flipping back must reproduce the original hash exactly (XOR is its
    // own inverse) - a basic sanity check on the incremental-update
    // property this will be relied on for once make/unmake exists.
    pos.side_to_move = Color::White;
    REQUIRE(compute_hash(pos) == white_to_move_hash);
}

TEST_CASE("hash changes independently for each castling right", "[zobrist]") {
    init_zobrist_keys();
    Position base = start_position();
    const std::uint64_t base_hash = compute_hash(base);

    for (std::uint8_t right : {castling::kWhiteKingside, castling::kWhiteQueenside,
                                castling::kBlackKingside, castling::kBlackQueenside}) {
        Position pos = base;
        pos.castling_rights &= static_cast<std::uint8_t>(~right); // revoke just this one
        REQUIRE(compute_hash(pos) != base_hash);
    }
}

TEST_CASE("hash changes with en passant target square", "[zobrist]") {
    init_zobrist_keys();
    Position pos = start_position();
    const std::uint64_t no_ep_hash = compute_hash(pos);

    pos.en_passant_square = static_cast<std::int8_t>(make_square(4, 2)); // arbitrary target square, e3
    const std::uint64_t with_ep_hash = compute_hash(pos);

    REQUIRE(with_ep_hash != no_ep_hash);

    pos.en_passant_square = kNoEnPassantSquare;
    REQUIRE(compute_hash(pos) == no_ep_hash);
}

TEST_CASE("en passant hash depends only on file, not rank", "[zobrist]") {
    // Matches the documented simplified scheme (zobrist.cpp header comment):
    // keyed by file only, regardless of which rank the target square is on.
    init_zobrist_keys();
    Position pos_rank3 = start_position();
    pos_rank3.en_passant_square = static_cast<std::int8_t>(make_square(4, 2)); // e3

    Position pos_rank6 = start_position();
    pos_rank6.en_passant_square = static_cast<std::int8_t>(make_square(4, 5)); // e6

    REQUIRE(compute_hash(pos_rank3) == compute_hash(pos_rank6));
}

TEST_CASE("two independently-built identical positions hash the same", "[zobrist]") {
    init_zobrist_keys();
    Position a = start_position();
    Position b = start_position();
    REQUIRE(compute_hash(a) == compute_hash(b));
    REQUIRE(a.zobrist_hash == b.zobrist_hash);
}

TEST_CASE("minor and non-pawn hashes ignore pawns and side to move, and see the right pieces",
          "[zobrist][correction_keys]") {
    init_zobrist_keys();
    Position pos = start_position();
    const std::uint64_t minor = compute_minor_hash(pos);
    const std::uint64_t non_pawn = compute_non_pawn_hash(pos);
    REQUIRE(minor != 0);
    REQUIRE(non_pawn != 0);
    REQUIRE(minor != non_pawn); // rooks, queens and kings are in non_pawn only
    REQUIRE(minor != compute_pawn_hash(pos));

    // Removing a pawn changes the pawn hash but neither the minor nor the non-pawn hash.
    Position no_pawn = pos;
    no_pawn.remove_piece(static_cast<Square>(8)); // a2 pawn
    REQUIRE(compute_minor_hash(no_pawn) == minor);
    REQUIRE(compute_non_pawn_hash(no_pawn) == non_pawn);
    REQUIRE(compute_pawn_hash(no_pawn) != compute_pawn_hash(pos));

    // Removing a knight changes both the minor and the non-pawn hash.
    Position no_knight = pos;
    no_knight.remove_piece(static_cast<Square>(1)); // b1 knight
    REQUIRE(compute_minor_hash(no_knight) != minor);
    REQUIRE(compute_non_pawn_hash(no_knight) != non_pawn);

    // Removing a rook changes the non-pawn hash but not the minor hash.
    Position no_rook = pos;
    no_rook.remove_piece(static_cast<Square>(0)); // a1 rook
    REQUIRE(compute_minor_hash(no_rook) == minor);
    REQUIRE(compute_non_pawn_hash(no_rook) != non_pawn);

    // Side to move is not part of either key.
    Position flipped = pos;
    flipped.side_to_move = Color::Black;
    REQUIRE(compute_minor_hash(flipped) == minor);
    REQUIRE(compute_non_pawn_hash(flipped) == non_pawn);
}
