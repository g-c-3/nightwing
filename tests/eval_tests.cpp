// tests/eval_tests.cpp
//
// Unit tests for src/eval/{eval,psqt,score}.h — material + tapered
// piece-square tables + mobility (eval/mobility.h — see
// mobility_tests.cpp for that term's own dedicated, isolated tests;
// this file only exercises it indirectly through evaluate() end to
// end). Positions are built via FEN (fen.h) or Position::place_piece()
// directly, matching the style of movegen_tests.cpp / fen_tests.cpp.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "eval/psqt.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation (catch_discover_tests registers each one as an individual
/// CTest test), so magic-bitboard/attack tables aren't shared across
/// cases the way they'd be in a single long-lived process — each case
/// must initialize them itself. Matches search_tests.cpp's/
/// perft_tests.cpp's convention exactly (see either for why).
///
/// Genuinely required here as of eval/mobility.h's mobility_value()
/// term (docs/DECISIONS.md, ROADMAP.md Phase 5's "Mobility eval" item):
/// evaluate() now calls board::bishop_attacks()/rook_attacks()/
/// queen_attacks() for every bishop/rook/queen on the board, which read
/// the magic-bitboard tables init_magic_bitboards() populates — before
/// that term existed, this file's tests never needed anything beyond
/// what Position::place_piece()/parse_fen()/start_position() alone
/// provide, since material/PSQT/pawn-structure evaluation never touches
/// a sliding-piece attack table.
void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Returns a fully empty position (no pieces, given side to move) —
/// same helper pattern as movegen_tests.cpp.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("evaluate: starting position is exactly balanced", "[eval]") {
    init_all();
    REQUIRE(evaluate(start_position()) == 0);
}

TEST_CASE("evaluate: a lone extra White pawn favors White", "[eval]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(4, 3), Piece::WhitePawn); // e4
    REQUIRE(evaluate(pos) > 0);
}

TEST_CASE("evaluate: a lone extra Black pawn favors Black", "[eval]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing); // e1
    pos.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    pos.place_piece(make_square(4, 4), Piece::BlackPawn); // e5
    REQUIRE(evaluate(pos) < 0);
}

TEST_CASE("evaluate: material dominates a full extra queen", "[eval]") {
    init_all();
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(3, 3), Piece::WhiteQueen); // d4
    // Queen material alone is 900; even the largest possible king/psqt/
    // mobility swing (a few tens of centipawns) can't erase that, so
    // this bound is a safe correctness check, not a tuned expectation.
    REQUIRE(evaluate(pos) > 800);
}

TEST_CASE("taper: phase kMaxPhase selects the mg term exactly", "[eval][score]") {
    const Score s{100, 50};
    REQUIRE(taper(s, kMaxPhase) == 100);
}

TEST_CASE("taper: phase 0 selects the eg term exactly", "[eval][score]") {
    const Score s{100, 50};
    REQUIRE(taper(s, 0) == 50);
}

TEST_CASE("taper: out-of-range phase is clamped rather than trusted", "[eval][score]") {
    const Score s{100, 50};
    REQUIRE(taper(s, -5) == taper(s, 0));
    REQUIRE(taper(s, kMaxPhase + 5) == taper(s, kMaxPhase));
}

TEST_CASE("psqt_value: White and Black get equal terms on mirrored squares", "[eval][psqt]") {
    const Square e2 = make_square(4, 1);
    const Square e7 = make_square(4, 6);
    const Score white_term = psqt_value(Piece::WhitePawn, e2);
    const Score black_term = psqt_value(Piece::BlackPawn, e7);
    REQUIRE(white_term.mg == black_term.mg);
    REQUIRE(white_term.eg == black_term.eg);
}

TEST_CASE("psqt_value: king centralization is mg-penalized and eg-rewarded", "[eval][psqt]") {
    const Square e1 = make_square(4, 0); // back rank
    const Square e4 = make_square(4, 3); // center
    const Score back_rank = psqt_value(Piece::WhiteKing, e1);
    const Score center = psqt_value(Piece::WhiteKing, e4);
    REQUIRE(center.mg < back_rank.mg); // centralizing early is discouraged
    REQUIRE(center.eg > back_rank.eg); // centralizing late is encouraged
}

TEST_CASE("evaluate: a bare kings position stays within a small bound", "[eval]") {
    init_all();
    // Not a mirrored-squares symmetry case (the two kings aren't on
    // mirrored squares here) -- just a sanity bound: with material equal
    // (0) and only king psqt terms in play (no other piece exists to
    // contribute a mobility term either), the score can't be large.
    Position pos = parse_fen("8/8/8/4k3/8/3K4/8/8 w - - 0 1");
    const int score = evaluate(pos);
    REQUIRE(score > -100);
    REQUIRE(score < 100);
}
