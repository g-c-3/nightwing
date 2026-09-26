// tests/movegen_tests.cpp
//
// Unit tests for src/board/movegen.h — focused, feature-by-feature
// legality checks (pins, checks, castling, en passant, the king
// "step back along a check ray" edge case). Deliberately NOT a perft
// suite: per docs/SESSIONS.md, the depth-count perft suite is the next,
// separate roadmap item now that movegen exists, and belongs in its own
// perft_tests.cpp. Positions here are hand-built via Position::place_piece()
// rather than a FEN parser, since FEN parsing doesn't exist yet (planned
// alongside the Phase 2 UCI loop).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"

using namespace nightwing::board;

namespace {

/// Returns a fully empty position (no pieces, White to move) — Position's
/// default member initializers already zero every bitboard and fill the
/// mailbox with Piece::None, so this is just a named, documented default.
Position empty_position(Color stm) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

/// Counts moves in `moves` whose `from()` equals `from`.
int count_from(const MoveList& moves, Square from) {
    int n = 0;
    for (const Move& m : moves) {
        if (m.from() == from) ++n;
    }
    return n;
}

bool contains_move(const MoveList& moves, Square from, Square to) {
    for (const Move& m : moves) {
        if (m.from() == from && m.to() == to) return true;
    }
    return false;
}

bool contains_move_with_flag(const MoveList& moves, Square from, Square to, MoveFlag flag) {
    for (const Move& m : moves) {
        if (m.from() == from && m.to() == to && m.flag() == flag) return true;
    }
    return false;
}

} // namespace

TEST_CASE("start position has exactly 20 legal moves", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    Position pos = start_position();
    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(moves.size() == 20);
}

TEST_CASE("start position includes both knight moves and a double pawn push", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    Position pos = start_position();
    MoveList moves;
    generate_legal_moves(pos, moves);

    REQUIRE(contains_move(moves, make_square(1, 0), make_square(2, 2))); // Nb1-c3
    REQUIRE(contains_move(moves, make_square(1, 0), make_square(0, 2))); // Nb1-a3
    REQUIRE(contains_move_with_flag(moves, make_square(4, 1), make_square(4, 3),
                                     MoveFlag::DoublePawnPush)); // e2-e4
}

TEST_CASE("a piece pinned to its king has zero legal moves if it can't move along the pin ray",
          "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // White King e1, White Knight e2, Black Rook e8, e-file otherwise
    // clear. The knight is pinned along the e-file and can't stay on it
    // (knights don't move along ranks/files), so it must have no moves.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);   // e1
    pos.place_piece(make_square(4, 1), Piece::WhiteKnight); // e2
    pos.place_piece(make_square(4, 7), Piece::BlackRook);   // e8
    pos.place_piece(make_square(0, 7), Piece::BlackKing);   // a8, for a valid position

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(count_from(moves, make_square(4, 1)) == 0);
}

TEST_CASE("a pinned rook may still slide within the pin ray or capture the pinner", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // White King e1, White Rook e2, Black Rook e8. The rook is pinned but
    // can still move anywhere from e2 to e7, or capture on e8.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 1), Piece::WhiteRook); // e2
    pos.place_piece(make_square(4, 7), Piece::BlackRook); // e8
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(contains_move(moves, make_square(4, 1), make_square(4, 4)));  // Re2-e5
    REQUIRE(contains_move(moves, make_square(4, 1), make_square(4, 7)));  // Re2xe8
    REQUIRE(!contains_move(moves, make_square(4, 1), make_square(3, 1))); // Re2-d2 illegal
}

TEST_CASE("single check: non-king moves must capture the checker or block the ray", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // White King e1, White Rook a4 (off the e-file), Black Rook e8,
    // e-file clear between. King is in check; Ra4 can block on e4, but
    // any move that doesn't address the check must be excluded.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(0, 3), Piece::WhiteRook); // a4
    pos.place_piece(make_square(4, 7), Piece::BlackRook); // e8
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(contains_move(moves, make_square(0, 3), make_square(4, 3)));  // Ra4-e4 blocks
    REQUIRE(!contains_move(moves, make_square(0, 3), make_square(0, 4))); // Ra4-a5 doesn't help
}

TEST_CASE("king cannot step further back along the ray it's being checked on", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // White King e2, Black Rook e8, e-file clear between. e1 is still on
    // the checking file and must NOT appear as a legal king move — this
    // is exactly the case that requires removing the king from occupancy
    // before testing king-destination squares for attack.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 1), Piece::WhiteKing); // e2
    pos.place_piece(make_square(4, 7), Piece::BlackRook); // e8
    pos.place_piece(make_square(0, 7), Piece::BlackKing); // a8

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(!contains_move(moves, make_square(4, 1), make_square(4, 0))); // Ke2-e1 illegal
    REQUIRE(!contains_move(moves, make_square(4, 1), make_square(4, 2))); // Ke2-e3 illegal
    REQUIRE(contains_move(moves, make_square(4, 1), make_square(3, 0)));  // Ke2-d1 legal
}

TEST_CASE("en passant capture is illegal when it exposes a horizontal discovered check",
          "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // Black King a4, Black Pawn e4, White Pawn d4, White Queen h4, White
    // King g1. Black to move with en passant available on d3. Capturing
    // e4xd3 e.p. removes both the d4 and e4 pawns, leaving rank 4 as just
    // king-vs-queen with nothing between — illegal.
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(0, 3), Piece::BlackKing);  // a4
    pos.place_piece(make_square(4, 3), Piece::BlackPawn);  // e4
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);  // d4
    pos.place_piece(make_square(7, 3), Piece::WhiteQueen); // h4
    pos.place_piece(make_square(6, 0), Piece::WhiteKing);  // g1
    pos.en_passant_square = static_cast<std::int8_t>(make_square(3, 2)); // d3

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(!contains_move_with_flag(moves, make_square(4, 3), make_square(3, 2), MoveFlag::EnPassant));
}

TEST_CASE("en passant capture is legal when it doesn't expose a discovered check", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // Same idea, but with the black king off the rank entirely — nothing
    // discovered by removing the d4/e4 pawns, so the capture is legal.
    Position pos = empty_position(Color::Black);
    pos.place_piece(make_square(0, 7), Piece::BlackKing);  // a8
    pos.place_piece(make_square(4, 3), Piece::BlackPawn);  // e4
    pos.place_piece(make_square(3, 3), Piece::WhitePawn);  // d4
    pos.place_piece(make_square(7, 3), Piece::WhiteQueen); // h4
    pos.place_piece(make_square(6, 0), Piece::WhiteKing);  // g1
    pos.en_passant_square = static_cast<std::int8_t>(make_square(3, 2)); // d3

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(contains_move_with_flag(moves, make_square(4, 3), make_square(3, 2), MoveFlag::EnPassant));
}

TEST_CASE("castling is generated when rights held and squares clear/unattacked", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(0, 0), Piece::WhiteRook); // a1
    pos.place_piece(make_square(7, 0), Piece::WhiteRook); // h1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.castling_rights = castling::kAll;

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(contains_move_with_flag(moves, make_square(4, 0), make_square(6, 0), MoveFlag::KingCastle));
    REQUIRE(contains_move_with_flag(moves, make_square(4, 0), make_square(2, 0), MoveFlag::QueenCastle));
}

TEST_CASE("castling is not generated through an attacked square", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // Black rook on f8 attacks f1, the kingside transit square, so
    // kingside castling must be excluded; queenside is untouched.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(0, 0), Piece::WhiteRook); // a1
    pos.place_piece(make_square(7, 0), Piece::WhiteRook); // h1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(5, 7), Piece::BlackRook); // f8
    pos.castling_rights = castling::kAll;

    MoveList moves;
    generate_legal_moves(pos, moves);
    REQUIRE(!contains_move_with_flag(moves, make_square(4, 0), make_square(6, 0), MoveFlag::KingCastle));
    REQUIRE(contains_move_with_flag(moves, make_square(4, 0), make_square(2, 0), MoveFlag::QueenCastle));
}

TEST_CASE("double check allows only king moves", "[movegen]") {
    init_masks();
    init_magic_bitboards();
    // White King e1, attacked simultaneously by a rook on e8 (via the
    // e-file) and a bishop on b4 (via the b4-c3-d2-e1 diagonal). No White
    // piece other than the king may move.
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);   // e1
    pos.place_piece(make_square(4, 7), Piece::BlackRook);   // e8
    pos.place_piece(make_square(1, 3), Piece::BlackBishop); // b4
    pos.place_piece(make_square(0, 7), Piece::BlackKing);   // a8

    MoveList moves;
    generate_legal_moves(pos, moves);
    for (const Move& m : moves) {
        REQUIRE(m.from() == make_square(4, 0));
    }
    REQUIRE(moves.size() > 0);
}

// ---------------------------------------------------------------------
// Staged-generation parity suite (ROADMAP.md "Staged / lazy move
// generation", Step 1: GenType::Captures/Quiets/All in movegen.h/.cpp).
//
// Rather than spot-checking a handful of hand-picked positions, this
// recursively walks the same six standard CPW perft reference positions
// (tests/perft_tests.cpp) to a modest depth, and at EVERY node along the
// way confirms that generate_legal_moves(Captures) plus
// generate_legal_moves(Quiets) is exactly generate_legal_moves(All) — no
// move omitted, no move duplicated between the two stages, and each
// move lands in the stage its own is_capture()/is_promotion() flags say
// it belongs in. This is deliberately shaped like perft (a full-tree
// walk, not a single-ply check) since the staged split's riskiest bugs
// -- a promotion inside a pin, an en passant capture that resolves a
// check, a quiet king move available only in a specific check/pin
// combination -- are exactly the positions perft's own reference suite
// was built to stress in the first place, several plies deep, not just
// at the root.
// ---------------------------------------------------------------------

namespace {

/// Returns true if every move in `list` is present (by raw() equality)
/// in `other`, and vice versa is checked separately by the caller via
/// combined_matches_all() below — this is a one-directional subset
/// check, kept small and reused both ways.
bool is_subset(const MoveList& list, const MoveList& other) {
    for (const Move& m : list) {
        if (!other.contains(m)) return false;
    }
    return true;
}

/// Recursively verifies staged-generation parity at `pos` and every
/// position reachable within `depth` plies. Fails the current Catch2
/// test (via REQUIRE) at the first divergence found, same as perft
/// would fail at the first wrong node count, rather than continuing to
/// accumulate more failures past the first real bug.
void verify_staged_parity(const Position& pos, int depth) {
    MoveList all_moves;
    MoveList captures;
    MoveList quiets;
    generate_legal_moves(pos, all_moves, GenType::All);
    generate_legal_moves(pos, captures, GenType::Captures);
    generate_legal_moves(pos, quiets, GenType::Quiets);

    // Every Captures move is a real capture or a promotion (of any
    // kind); every Quiets move is neither -- exactly the split
    // movegen.h's GenType doc comment specifies.
    for (const Move& m : captures) {
        REQUIRE((m.is_capture() || m.is_promotion()));
    }
    for (const Move& m : quiets) {
        REQUIRE_FALSE(m.is_capture());
        REQUIRE_FALSE(m.is_promotion());
    }

    // No move counted twice, none dropped: sizes must add up, and each
    // stage's list must be a subset of the combined All list (which,
    // combined with the size check, proves the two stages partition All
    // exactly -- same logic perft's own bulk-vs-plain cross-check uses).
    REQUIRE(captures.size() + quiets.size() == all_moves.size());
    REQUIRE(is_subset(captures, all_moves));
    REQUIRE(is_subset(quiets, all_moves));

    if (depth <= 1) return;

    for (const Move& m : all_moves) {
        Position child = pos;
        UndoInfo undo;
        make_move(child, m, undo);
        verify_staged_parity(child, depth - 1);
    }
}

} // namespace

TEST_CASE("staged generation parity: startpos", "[movegen][staged]") {
    init_masks();
    init_magic_bitboards();
    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    verify_staged_parity(pos, 3);
}

TEST_CASE("staged generation parity: Kiwipete (captures/checks/castling/promotions)",
          "[movegen][staged]") {
    init_masks();
    init_magic_bitboards();
    Position pos = parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    verify_staged_parity(pos, 3);
}

TEST_CASE("staged generation parity: position 3 (en passant/pins-heavy endgame)",
          "[movegen][staged]") {
    init_masks();
    init_magic_bitboards();
    Position pos = parse_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    verify_staged_parity(pos, 4);
}

TEST_CASE("staged generation parity: position 4 (asymmetric castling/promotion stress)",
          "[movegen][staged]") {
    init_masks();
    init_magic_bitboards();
    Position pos = parse_fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    verify_staged_parity(pos, 3);
}

TEST_CASE("staged generation parity: position 5 (sharp middlegame)", "[movegen][staged]") {
    init_masks();
    init_magic_bitboards();
    Position pos = parse_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
    verify_staged_parity(pos, 3);
}

TEST_CASE("staged generation parity: position 6 (complex late-middlegame)", "[movegen][staged]") {
    init_masks();
    init_magic_bitboards();
    Position pos = parse_fen("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
    verify_staged_parity(pos, 3);
}

TEST_CASE("staged generation parity: double check position (king-only legal moves)",
          "[movegen][staged]") {
    init_masks();
    init_magic_bitboards();
    Position pos = empty_position(Color::White);
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);   // e1
    pos.place_piece(make_square(4, 7), Piece::BlackRook);   // e8
    pos.place_piece(make_square(1, 3), Piece::BlackBishop); // b4
    pos.place_piece(make_square(0, 7), Piece::BlackKing);   // a8
    verify_staged_parity(pos, 2);
}
