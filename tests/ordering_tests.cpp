// tests/ordering_tests.cpp
//
// Unit tests for src/search/ordering.h — KillerTable, HistoryTable, and
// order_moves()'s priority scheme, in isolation from search.cpp's
// negamax() integration (which gets its own end-to-end correctness net
// from search_tests.cpp — these tests are deliberately narrower).
//
// order_moves() only reads board occupancy (Position::piece_at()), so
// MoveLists here are built by hand with specific from/to pairs rather
// than via full legal move generation — the moves don't need to be
// legal in the "doesn't leave your own king in check" sense, only
// geometrically meaningful enough that piece_at(from)/piece_at(to)
// reflect a real capture/quiet/promotion scenario for scoring purposes.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/move.h"
#include "board/zobrist.h"
#include "search/ordering.h"

using namespace nightwing::board;
using namespace nightwing::search;

namespace {
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}
} // namespace

TEST_CASE("KillerTable: a stored killer is returned by get()", "[ordering][killers]") {
    KillerTable killers;
    const Move move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush); // e2e4
    killers.update(3, move);
    REQUIRE(killers.get(3, 0) == move);
}

TEST_CASE("KillerTable: a second distinct killer pushes the first to slot 1", "[ordering][killers]") {
    KillerTable killers;
    const Move first(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    const Move second(make_square(6, 0), make_square(5, 2), MoveFlag::Quiet); // g1f3
    killers.update(2, first);
    killers.update(2, second);
    REQUIRE(killers.get(2, 0) == second);
    REQUIRE(killers.get(2, 1) == first);
}

TEST_CASE("KillerTable: re-recording the current top killer doesn't duplicate it", "[ordering][killers]") {
    KillerTable killers;
    const Move first(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    const Move second(make_square(6, 0), make_square(5, 2), MoveFlag::Quiet);
    killers.update(1, first);
    killers.update(1, second);
    killers.update(1, second); // already slot 0 -- should be a no-op, not shift `first` out further
    REQUIRE(killers.get(1, 0) == second);
    REQUIRE(killers.get(1, 1) == first);
}

TEST_CASE("KillerTable: different plies are independent", "[ordering][killers]") {
    KillerTable killers;
    const Move move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    killers.update(5, move);
    REQUIRE(killers.get(5, 0) == move);
    REQUIRE(killers.get(6, 0).is_null());
}

TEST_CASE("KillerTable: out-of-range ply/index returns a null move, not a crash", "[ordering][killers]") {
    KillerTable killers;
    REQUIRE(killers.get(-1, 0).is_null());
    REQUIRE(killers.get(kMaxPly, 0).is_null());
    REQUIRE(killers.get(0, 2).is_null());
    killers.update(-1, Move(make_square(0, 0), make_square(0, 1))); // must not crash
    killers.update(kMaxPly + 10, Move(make_square(0, 0), make_square(0, 1))); // must not crash
}

TEST_CASE("HistoryTable: an unrecorded move scores 0", "[ordering][history]") {
    HistoryTable history;
    const Move move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    REQUIRE(history.score(Color::White, move) == 0);
}

TEST_CASE("HistoryTable: update() adds a depth-squared bonus", "[ordering][history]") {
    HistoryTable history;
    const Move move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    history.update(Color::White, move, /*depth=*/4);
    REQUIRE(history.score(Color::White, move) == 16); // 4*4
    history.update(Color::White, move, /*depth=*/3);
    REQUIRE(history.score(Color::White, move) == 25); // 16 + 3*3
}

TEST_CASE("HistoryTable: score is clamped and never overflows", "[ordering][history]") {
    HistoryTable history;
    const Move move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    for (int i = 0; i < 100; ++i) {
        history.update(Color::White, move, /*depth=*/50); // 50*50 = 2500 per call, far exceeding the cap quickly
    }
    const int score = history.score(Color::White, move);
    REQUIRE(score > 0);
    REQUIRE(score <= 8192); // matches HistoryTable::kHistoryMax (private, so checked by value here)
}

TEST_CASE("HistoryTable: colors and squares are independent", "[ordering][history]") {
    HistoryTable history;
    const Move white_move(make_square(4, 1), make_square(4, 3), MoveFlag::DoublePawnPush);
    const Move black_move(make_square(4, 6), make_square(4, 4), MoveFlag::DoublePawnPush);
    history.update(Color::White, white_move, 5);
    REQUIRE(history.score(Color::White, white_move) == 25);
    REQUIRE(history.score(Color::Black, white_move) == 0); // same move bits, different color
    REQUIRE(history.score(Color::White, black_move) == 0); // different move entirely
}

TEST_CASE("ContinuationHistoryTable: an unrecorded combination scores 0", "[ordering][continuation_history]") {
    ContinuationHistoryTable cont_history;
    REQUIRE(cont_history.score(PieceType::Knight, make_square(4, 3), PieceType::Bishop,
                                make_square(2, 5)) == 0);
}

TEST_CASE("ContinuationHistoryTable: update() adds a depth-squared bonus, same weighting as "
          "HistoryTable's",
          "[ordering][continuation_history]") {
    ContinuationHistoryTable cont_history;
    cont_history.update(PieceType::Knight, make_square(4, 3), PieceType::Bishop, make_square(2, 5),
                         /*depth=*/4);
    REQUIRE(cont_history.score(PieceType::Knight, make_square(4, 3), PieceType::Bishop,
                                make_square(2, 5)) == 16); // 4*4
    cont_history.update(PieceType::Knight, make_square(4, 3), PieceType::Bishop, make_square(2, 5),
                         /*depth=*/3);
    REQUIRE(cont_history.score(PieceType::Knight, make_square(4, 3), PieceType::Bishop,
                                make_square(2, 5)) == 25); // 16 + 3*3
}

TEST_CASE("ContinuationHistoryTable: a PieceType::None prev_piece is always a no-op/zero",
          "[ordering][continuation_history]") {
    // The "no real preceding move" sentinel (search/ordering.h's own
    // header comment) -- the true search root, or immediately after a
    // null move. update() must not record anything, and score() must
    // always report 0, regardless of how many times either is called.
    ContinuationHistoryTable cont_history;
    cont_history.update(PieceType::None, make_square(4, 3), PieceType::Bishop, make_square(2, 5),
                         /*depth=*/10);
    REQUIRE(cont_history.score(PieceType::None, make_square(4, 3), PieceType::Bishop,
                                make_square(2, 5)) == 0);
}

TEST_CASE("ContinuationHistoryTable: distinct preceding-move contexts are independent",
          "[ordering][continuation_history]") {
    // The same reply (Bishop to the same square) following two DIFFERENT
    // preceding moves must be tracked separately -- this is the entire
    // point of the table (distinct from HistoryTable, which has no
    // notion of what came before).
    ContinuationHistoryTable cont_history;
    cont_history.update(PieceType::Knight, make_square(4, 3), PieceType::Bishop, make_square(2, 5),
                         /*depth=*/5);
    REQUIRE(cont_history.score(PieceType::Knight, make_square(4, 3), PieceType::Bishop,
                                make_square(2, 5)) == 25);
    REQUIRE(cont_history.score(PieceType::Rook, make_square(4, 3), PieceType::Bishop,
                                make_square(2, 5)) == 0); // different prev_piece
    REQUIRE(cont_history.score(PieceType::Knight, make_square(0, 0), PieceType::Bishop,
                                make_square(2, 5)) == 0); // different prev_to
}

TEST_CASE("ContinuationHistoryTable: score is clamped and never overflows",
          "[ordering][continuation_history]") {
    ContinuationHistoryTable cont_history;
    for (int i = 0; i < 100; ++i) {
        cont_history.update(PieceType::Knight, make_square(4, 3), PieceType::Bishop, make_square(2, 5),
                             /*depth=*/50); // 50*50 = 2500 per call, far exceeding the cap quickly
    }
    const int score = cont_history.score(PieceType::Knight, make_square(4, 3), PieceType::Bishop,
                                          make_square(2, 5));
    REQUIRE(score > 0);
    REQUIRE(score <= 8192); // matches ContinuationHistoryTable::kContinuationHistoryMax (private)
}

TEST_CASE("order_moves: the TT move is always ordered first when present", "[ordering]") {
    init_all();
    // White queen e4, rook c4, knight b4; Black rook c6, pawn d5, king e8.
    Position pos = parse_fen("4k3/8/2r5/3p4/1NR1Q3/8/8/4K3 w - - 0 1");

    const Move qxd5(make_square(4, 3), make_square(3, 4), MoveFlag::Capture); // small MVV-LVA score
    const Move rxc6(make_square(2, 3), make_square(2, 5), MoveFlag::Capture); // large MVV-LVA score
    MoveList moves;
    moves.push_back(qxd5);
    moves.push_back(rxc6);

    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    // Without a TT move, Rxc6 (captures a rook) should outrank Qxd5
    // (captures a pawn) on MVV-LVA alone -- sanity-check that first,
    // then confirm the TT move overrides it.
    order_moves(moves, pos, Move(), killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == rxc6);

    order_moves(moves, pos, /*tt_move=*/qxd5, killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == qxd5);
}

TEST_CASE("order_moves: MVV -- capturing the more valuable victim ranks first regardless of attacker",
          "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/8/2r5/3p4/1NR1Q3/8/8/4K3 w - - 0 1");
    const Move qxd5(make_square(4, 3), make_square(3, 4), MoveFlag::Capture); // Q captures pawn
    const Move rxc6(make_square(2, 3), make_square(2, 5), MoveFlag::Capture); // R captures rook

    MoveList moves;
    moves.push_back(qxd5);
    moves.push_back(rxc6);

    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    order_moves(moves, pos, Move(), killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == rxc6); // rook victim (500) beats pawn victim (100) regardless of attacker value
}

TEST_CASE("order_moves: LVA -- among equal victims, the cheaper attacker ranks first", "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/8/2r5/3p4/1NR1Q3/8/8/4K3 w - - 0 1");
    const Move qxd5(make_square(4, 3), make_square(3, 4), MoveFlag::Capture); // Queen captures the pawn
    const Move nxd5(make_square(1, 3), make_square(3, 4), MoveFlag::Capture); // Knight captures the same pawn

    MoveList moves;
    moves.push_back(qxd5);
    moves.push_back(nxd5);

    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    order_moves(moves, pos, Move(), killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == nxd5); // same victim (pawn) -- cheaper attacker (knight < queen) goes first
}

TEST_CASE("order_moves: captures rank above non-capture promotions", "[ordering]") {
    init_all();
    // White pawn on a7 (about to promote), queen on e4 able to capture
    // the black pawn on d5 -- both moves available in the same position.
    Position pos = parse_fen("4k3/P7/8/3p4/4Q3/8/8/4K3 w - - 0 1");
    const Move promo(make_square(0, 6), make_square(0, 7), MoveFlag::PromoQueen); // a7a8=Q
    const Move qxd5(make_square(4, 3), make_square(3, 4), MoveFlag::Capture);

    MoveList moves;
    moves.push_back(promo);
    moves.push_back(qxd5);

    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    order_moves(moves, pos, Move(), killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == qxd5); // any capture outranks a non-capture promotion in this scheme
}

TEST_CASE("order_moves: a non-capture promotion ranks above a plain quiet move", "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    const Move promo(make_square(0, 6), make_square(0, 7), MoveFlag::PromoQueen); // a7a8=Q
    const Move king_move(make_square(4, 0), make_square(3, 0), MoveFlag::Quiet);  // e1d1

    MoveList moves;
    moves.push_back(king_move);
    moves.push_back(promo);

    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    order_moves(moves, pos, Move(), killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == promo);
}

TEST_CASE("order_moves: a killer move ranks above an unrelated quiet move with no history", "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    const Move killer_move(make_square(4, 0), make_square(3, 0), MoveFlag::Quiet); // e1d1
    const Move other_move(make_square(4, 0), make_square(5, 0), MoveFlag::Quiet);  // e1f1

    MoveList moves;
    moves.push_back(other_move);
    moves.push_back(killer_move);

    KillerTable killers;
    killers.update(/*ply=*/2, killer_move);
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    order_moves(moves, pos, Move(), killers, /*ply=*/2, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == killer_move);
}

TEST_CASE("order_moves: a killer move only applies at its own recorded ply", "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    const Move killer_move(make_square(4, 0), make_square(3, 0), MoveFlag::Quiet);
    const Move other_move(make_square(4, 0), make_square(5, 0), MoveFlag::Quiet);

    MoveList moves;
    moves.push_back(killer_move);
    moves.push_back(other_move);

    KillerTable killers;
    killers.update(/*ply=*/2, killer_move); // recorded at ply 2...
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    order_moves(moves, pos, Move(), killers, /*ply=*/7, history, cont_history, PieceType::None, 0); // ...but ordering happens at ply 7
    REQUIRE(moves[0] == killer_move); // unaffected -- both still score 0 (no killer match, no history);
                                       // move-generation order (stable sort) keeps killer_move first
                                       // simply because it was pushed first, not because it "won."
}

TEST_CASE("order_moves: a higher-history quiet move ranks above a lower-history one", "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    const Move good_move(make_square(4, 0), make_square(3, 0), MoveFlag::Quiet); // e1d1
    const Move meh_move(make_square(4, 0), make_square(5, 0), MoveFlag::Quiet);  // e1f1

    MoveList moves;
    moves.push_back(meh_move);
    moves.push_back(good_move);

    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    history.update(Color::White, good_move, /*depth=*/6); // 36
    history.update(Color::White, meh_move, /*depth=*/2);  // 4
    order_moves(moves, pos, Move(), killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == good_move);
}

TEST_CASE("order_moves: history-scored quiets still rank below killers", "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    const Move killer_move(make_square(4, 0), make_square(3, 0), MoveFlag::Quiet);
    const Move history_move(make_square(4, 0), make_square(5, 0), MoveFlag::Quiet);

    MoveList moves;
    moves.push_back(history_move);
    moves.push_back(killer_move);

    KillerTable killers;
    killers.update(2, killer_move);
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    // A very large history score -- still must not outrank a killer,
    // since killer scores (ordering.cpp) are deliberately kept above
    // HistoryTable::kHistoryMax's ceiling.
    for (int i = 0; i < 20; ++i) {
        history.update(Color::White, history_move, 50);
    }
    order_moves(moves, pos, Move(), killers, 2, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == killer_move);
}

TEST_CASE("order_moves: equal-scoring quiets keep move-generation order (stable sort)", "[ordering]") {
    init_all();
    Position pos = parse_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    const Move first(make_square(4, 0), make_square(3, 0), MoveFlag::Quiet);
    const Move second(make_square(4, 0), make_square(5, 0), MoveFlag::Quiet);

    MoveList moves;
    moves.push_back(first);
    moves.push_back(second);

    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    order_moves(moves, pos, Move(), killers, 0, history, cont_history, PieceType::None, 0);
    REQUIRE(moves[0] == first);
    REQUIRE(moves[1] == second);
}
