// tests/piece_bonuses_tests.cpp
//
// Unit tests for src/eval/piece_bonuses.h (ROADMAP.md Phase 5's
// "Bishop pair, rook on open/semi-open file, rook on 7th rank" item).
// Positions are built directly via Position::place_piece(), matching
// mobility_tests.cpp's/king_safety_tests.cpp's style, specifically so
// each test isolates piece_bonus_value() itself rather than relying on
// a real game position where every eval term interacts at once
// (eval_tests.cpp's job, not this file's).

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "eval/eval_cache.h"
#include "eval/piece_bonuses.h"
#include "eval/score.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Every Catch2 TEST_CASE below runs as its own separate process
/// invocation, so table-init state isn't shared across cases -- each
/// must initialize what it needs itself. Matches king_safety_tests.cpp's
/// convention. Only init_masks() is needed here (piece_bonus_value()
/// never touches a sliding-piece attack table -- see piece_bonuses.h's
/// own precondition comment), unlike king_safety_tests.cpp/
/// mobility_tests.cpp which also need init_magic_bitboards().
void init_all() {
    init_masks();
}

/// Returns a fully empty position (no pieces, given side to move) --
/// same helper pattern as mobility_tests.cpp/king_safety_tests.cpp.
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("piece_bonus_value: starting position is exactly balanced", "[eval][piece_bonuses]") {
    init_all();
    // Mirror-symmetric between colors: both sides have an intact bishop
    // pair (cancels), both a1/h1-style rooks sit behind their own
    // b/g-pawns on the a/h files (closed, no open/semi-open bonus) and
    // on rank 1/rank 8 respectively, neither of which is either side's
    // own relative 7th rank.
    REQUIRE(piece_bonus_value(start_position()) == Score{0, 0});
}

TEST_CASE("piece_bonus_value: bishop pair outscores a single bishop", "[eval][piece_bonuses]") {
    init_all();
    Position pair = empty_position();
    pair.place_piece(make_square(4, 0), Piece::WhiteKing);   // e1
    pair.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    pair.place_piece(make_square(5, 0), Piece::WhiteBishop); // f1
    pair.place_piece(make_square(4, 7), Piece::BlackKing);   // e8

    Position single = empty_position();
    single.place_piece(make_square(4, 0), Piece::WhiteKing);   // e1
    single.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    single.place_piece(make_square(4, 7), Piece::BlackKing);   // e8

    REQUIRE(piece_bonus_value(pair).mg > piece_bonus_value(single).mg);
    REQUIRE(piece_bonus_value(pair).eg > piece_bonus_value(single).eg);
}

TEST_CASE("piece_bonus_value: a rook on an open file outscores one on a semi-open file, "
          "which outscores one on a closed file",
          "[eval][piece_bonuses]") {
    init_all();
    // Rook fixed on a1 (file 0) in every config -- only the a-file's own
    // pawn occupancy varies, isolating the file-openness component.
    // Neither king is placed on a rook-relevant file/rank combination
    // that would introduce a 7th-rank bonus for this comparison.
    Position closed = empty_position();
    closed.place_piece(make_square(0, 0), Piece::WhiteRook); // a1
    closed.place_piece(make_square(0, 1), Piece::WhitePawn); // a2 -- own pawn
    closed.place_piece(make_square(4, 0), Piece::WhiteKing);
    closed.place_piece(make_square(4, 7), Piece::BlackKing);

    Position semi_open = empty_position();
    semi_open.place_piece(make_square(0, 0), Piece::WhiteRook); // a1
    semi_open.place_piece(make_square(0, 5), Piece::BlackPawn); // a6 -- enemy pawn only
    semi_open.place_piece(make_square(4, 0), Piece::WhiteKing);
    semi_open.place_piece(make_square(4, 7), Piece::BlackKing);

    Position fully_open = empty_position();
    fully_open.place_piece(make_square(0, 0), Piece::WhiteRook); // a1, no a-file pawn at all
    fully_open.place_piece(make_square(4, 0), Piece::WhiteKing);
    fully_open.place_piece(make_square(4, 7), Piece::BlackKing);

    const int closed_mg = piece_bonus_value(closed).mg;
    const int semi_open_mg = piece_bonus_value(semi_open).mg;
    const int fully_open_mg = piece_bonus_value(fully_open).mg;

    REQUIRE(fully_open_mg > semi_open_mg);
    REQUIRE(semi_open_mg > closed_mg);
}

TEST_CASE("piece_bonus_value: a rook on the 7th rank is rewarded", "[eval][piece_bonuses]") {
    init_all();
    Position on_seventh = empty_position();
    on_seventh.place_piece(make_square(0, 6), Piece::WhiteRook); // a7
    on_seventh.place_piece(make_square(4, 0), Piece::WhiteKing);
    on_seventh.place_piece(make_square(4, 7), Piece::BlackKing);
    on_seventh.place_piece(make_square(3, 6), Piece::BlackPawn); // d7 -- off the a-file, so the
                                                                  // a-file stays fully open in
                                                                  // both configs below, isolating
                                                                  // the 7th-rank component alone.

    Position off_seventh = empty_position();
    off_seventh.place_piece(make_square(0, 3), Piece::WhiteRook); // a4 -- same file, not 7th rank
    off_seventh.place_piece(make_square(4, 0), Piece::WhiteKing);
    off_seventh.place_piece(make_square(4, 7), Piece::BlackKing);
    off_seventh.place_piece(make_square(3, 6), Piece::BlackPawn); // d7

    REQUIRE(piece_bonus_value(on_seventh).mg > piece_bonus_value(off_seventh).mg);
    REQUIRE(piece_bonus_value(on_seventh).eg > piece_bonus_value(off_seventh).eg);
}

TEST_CASE("piece_bonus_value: open-file and 7th-rank bonuses stack additively for the same rook",
          "[eval][piece_bonuses]") {
    init_all();
    // A single rook that is BOTH on an open file AND on the 7th rank at
    // once should score the sum of both bonuses, not just the larger of
    // the two -- confirms the two components are independent additive
    // terms within the same rook's own contribution, not a max()-style
    // combination.
    Position both = empty_position();
    both.place_piece(make_square(0, 6), Piece::WhiteRook); // a7 -- open a-file, 7th rank
    both.place_piece(make_square(4, 0), Piece::WhiteKing);
    both.place_piece(make_square(4, 7), Piece::BlackKing);

    Position open_only = empty_position();
    open_only.place_piece(make_square(0, 3), Piece::WhiteRook); // a4 -- open a-file, not 7th rank
    open_only.place_piece(make_square(4, 0), Piece::WhiteKing);
    open_only.place_piece(make_square(4, 7), Piece::BlackKing);

    const Score both_score = piece_bonus_value(both);
    const Score open_only_score = piece_bonus_value(open_only);
    const Score expected = open_only_score + kRookOnSeventhBonus;

    REQUIRE(both_score == expected);
}

// ---------------------------------------------------------------------------
// PieceBonusWeights override (DECISIONS.md 2026-11-03 (7)).
// ---------------------------------------------------------------------------

namespace {

void init_full() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Positions exercising every bonus: bishop pairs, open / semi-open / closed
/// files, 7th-rank rooks for both colors, plus ordinary middlegames.
const char* const kOverrideFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "6k1/R5pp/8/8/8/8/5PPP/6K1 w - - 0 1",
    "6k1/5ppp/8/8/8/8/r5PP/6K1 b - - 0 1",
    "4k3/8/8/8/8/8/PPP2PPP/R3KB1R w KQ - 0 1",
    "2r3k1/5ppp/8/8/8/8/1B3PPP/1B4K1 w - - 0 1",
    "8/5pk1/4b3/8/3B4/8/5PK1/8 b - - 0 1",
};

} // namespace

TEST_CASE("piece_bonus_value: default PieceBonusWeights equals the compiled-in result",
          "[eval][piece_bonuses]") {
    init_full();
    const PieceBonusWeights defaults = default_piece_bonus_weights();
    for (const char* fen : kOverrideFens) {
        const Position pos = parse_fen(fen);
        REQUIRE(piece_bonus_value(pos, &defaults) == piece_bonus_value(pos));
    }
}

TEST_CASE("piece_bonus_value: a changed weight moves the score by exactly that amount",
          "[eval][piece_bonuses]") {
    init_full();
    // White rook on a7 (open a-file AND 7th rank): +7 mg on the 7th-rank
    // bonus must raise mg by 7 and leave eg untouched.
    const Position pos = parse_fen("6k1/R5pp/8/8/8/8/5PPP/6K1 w - - 0 1");
    PieceBonusWeights w = default_piece_bonus_weights();
    const Score base = piece_bonus_value(pos, &w);
    w.rook_on_seventh_mg += 7.0;
    const Score changed = piece_bonus_value(pos, &w);
    REQUIRE(changed.mg == base.mg + 7);
    REQUIRE(changed.eg == base.eg);

    // Black bishop pair: raising the bishop-pair eg bonus lowers the
    // White-relative eg score by the same amount.
    const Position pair = parse_fen("4k3/8/8/8/8/8/8/2bbK3 w - - 0 1");
    PieceBonusWeights v = default_piece_bonus_weights();
    const Score before = piece_bonus_value(pair, &v);
    v.bishop_pair_eg += 5.0;
    const Score after = piece_bonus_value(pair, &v);
    REQUIRE(after.eg == before.eg - 5);
    REQUIRE(after.mg == before.mg);
}

TEST_CASE("evaluate: default piece-bonus override equals the compiled-in evaluation",
          "[eval][piece_bonuses]") {
    init_full();
    const PieceBonusWeights defaults = default_piece_bonus_weights();
    // Compare only positions the default-path shortcuts do not touch
    // (no insufficient material, no lone-minor scaling): there the override
    // must reproduce the default evaluation exactly.
    for (const char* fen : kOverrideFens) {
        const Position pos = parse_fen(fen);
        const int plain = evaluate(pos);
        const int overridden = evaluate(pos, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &defaults);
        INFO(fen);
        REQUIRE(overridden == plain);
    }
}

TEST_CASE("evaluate: a changed piece-bonus weight changes the tapered score by the expected amount",
          "[eval][piece_bonuses]") {
    init_full();
    // White has the bishop pair, Black does not. Adding +10 to BOTH halves
    // of the bishop-pair bonus adds 10 after tapering whatever the phase
    // (integer taper rounding allows at most 1 centipawn of slack).
    const Position pos = parse_fen("rn1qkbnr/ppp2ppp/8/3pp3/8/2N2N2/PPPPBPPP/R1BQK2R w KQkq - 0 1");
    PieceBonusWeights w = default_piece_bonus_weights();
    const int base = evaluate(pos, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr, nullptr, &w);
    w.bishop_pair_mg += 10.0;
    w.bishop_pair_eg += 10.0;
    const int changed = evaluate(pos, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr, nullptr, &w);
    REQUIRE(changed - base >= 9);
    REQUIRE(changed - base <= 11);
}

TEST_CASE("evaluate: a piece-bonus override bypasses the eval cache and the insufficient-material shortcut",
          "[eval][piece_bonuses]") {
    init_full();
    const Position pos = parse_fen("6k1/R5pp/8/8/8/8/5PPP/6K1 w - - 0 1");

    // Cache bypass, both directions: a default call fills the cache, an
    // override call must NOT return that cached value, and must not store
    // its own value for a later default call to pick up.
    EvalCache cache(1024);
    const int cached_default = evaluate(pos, nullptr, &cache);
    PieceBonusWeights w = default_piece_bonus_weights();
    w.rook_on_seventh_mg += 40.0;
    w.rook_on_seventh_eg += 40.0;
    const int with_override = evaluate(pos, nullptr, &cache, nullptr, nullptr, nullptr, nullptr, nullptr,
                                       nullptr, nullptr, nullptr, nullptr, nullptr, &w);
    REQUIRE(with_override != cached_default);
    REQUIRE(evaluate(pos, nullptr, &cache) == cached_default);

    // Insufficient-material shortcut: K+B vs K is 0 on the default path but
    // an override call evaluates it fully.
    const Position kbk = parse_fen("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1");
    REQUIRE(evaluate(kbk) == 0);
    const PieceBonusWeights defaults = default_piece_bonus_weights();
    REQUIRE(evaluate(kbk, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, &defaults) != 0);
}
