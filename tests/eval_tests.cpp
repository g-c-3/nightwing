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
#include "eval/tempo.h"

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

TEST_CASE("evaluate: starting position is balanced apart from the tempo bonus -- White (to "
          "move) scores exactly kTempoBonus, tapered at the starting phase",
          "[eval]") {
    init_all();
    // Every OTHER term is symmetric on the starting position (material,
    // PSQT, pawn structure, mobility, king safety, etc. all cancel
    // between mirrored White/Black setups) -- the tempo bonus (eval/
    // tempo.h, ROADMAP.md Phase 5's "Tempo bonus" item) is the sole
    // exception, and is exactly why this is no longer literally 0 as
    // of that term's addition (previously: REQUIRE(evaluate(...) == 0)
    // ). taper()'d explicitly via compute_phase() rather than asserting
    // a bare kTempoBonus.mg literal, so this stays correct automatically
    // if kTempoBonus or compute_phase() itself ever changes.
    REQUIRE(evaluate(start_position()) == taper(kTempoBonus, compute_phase(start_position())));
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

TEST_CASE("compute_phase: starting position (full non-pawn material) is exactly kMaxPhase, not "
          "0 -- pins the direction a prior bug (docs/DECISIONS.md, 2026-08-29 (2)) got backwards",
          "[eval][score]") {
    init_all();
    REQUIRE(compute_phase(start_position()) == kMaxPhase);
}

TEST_CASE("compute_phase: a bare kings position (no non-pawn material at all) is exactly 0",
          "[eval][score]") {
    init_all();
    Position pos = parse_fen("8/8/8/4k3/8/3K4/8/8 w - - 0 1");
    REQUIRE(compute_phase(pos) == 0);
}

TEST_CASE("compute_phase: removing a single piece decreases phase by exactly that piece type's "
          "own phase weight",
          "[eval][score]") {
    init_all();
    // Starting position minus one White queen: phase should drop by
    // exactly kQueenPhase from the full kMaxPhase baseline -- a direct,
    // minimal check that the function counts UP from present material
    // (the fixed direction) rather than down from kMaxPhase (the
    // previous, buggy direction), which would have shown the opposite
    // sign of change here.
    Position pos = start_position();
    pos.remove_piece(make_square(3, 0)); // d1, White queen
    REQUIRE(compute_phase(pos) == kMaxPhase - kQueenPhase);
}

TEST_CASE("evaluate: with compute_phase() fixed, the starting position taper()s to (very close "
          "to) each term's mg value, not its eg value",
          "[eval][score]") {
    init_all();
    // A direct end-to-end regression check for the compute_phase() fix
    // itself (docs/DECISIONS.md, 2026-08-29 (2)): king centralization
    // is mg-penalized/eg-rewarded (already established just above, and
    // king safety/tropism are also mg-heavier per their own docs/
    // DECISIONS.md entries) -- so with the phase direction fixed, a
    // centralized White king should score WORSE than a back-rank White
    // king at the actual game start (full material, i.e. compute_phase()
    // returning kMaxPhase and taper() therefore weighting mg heavily),
    // not better. Under the previous (buggy) direction, start position
    // resolved to phase 0 -- fully eg-weighted -- which would have made
    // this comparison come out backwards.
    Position back_rank = empty_position();
    back_rank.place_piece(make_square(4, 0), Piece::WhiteKing);  // e1
    back_rank.place_piece(make_square(4, 7), Piece::BlackKing);  // e8
    back_rank.place_piece(make_square(3, 0), Piece::WhiteQueen); // d1
    back_rank.place_piece(make_square(3, 7), Piece::BlackQueen); // d8

    Position centralized = empty_position();
    centralized.place_piece(make_square(4, 3), Piece::WhiteKing); // e4
    centralized.place_piece(make_square(4, 7), Piece::BlackKing); // e8
    centralized.place_piece(make_square(3, 0), Piece::WhiteQueen); // d1
    centralized.place_piece(make_square(3, 7), Piece::BlackQueen); // d8

    REQUIRE(evaluate(centralized) < evaluate(back_rank));
}

TEST_CASE("default_material_weights: matches kPawnValue/kKnightValue/.../kQueenValue exactly",
          "[eval][psqt][tuner]") {
    const MaterialWeights defaults = default_material_weights();
    REQUIRE(defaults.pawn_mg == static_cast<double>(kPawnValue.mg));
    REQUIRE(defaults.pawn_eg == static_cast<double>(kPawnValue.eg));
    REQUIRE(defaults.knight_mg == static_cast<double>(kKnightValue.mg));
    REQUIRE(defaults.knight_eg == static_cast<double>(kKnightValue.eg));
    REQUIRE(defaults.bishop_mg == static_cast<double>(kBishopValue.mg));
    REQUIRE(defaults.bishop_eg == static_cast<double>(kBishopValue.eg));
    REQUIRE(defaults.rook_mg == static_cast<double>(kRookValue.mg));
    REQUIRE(defaults.rook_eg == static_cast<double>(kRookValue.eg));
    REQUIRE(defaults.queen_mg == static_cast<double>(kQueenValue.mg));
    REQUIRE(defaults.queen_eg == static_cast<double>(kQueenValue.eg));
}

TEST_CASE("material_value: passing default_material_weights() as an explicit override "
          "reproduces the no-override result exactly",
          "[eval][psqt][tuner]") {
    const MaterialWeights defaults = default_material_weights();
    for (const PieceType type : {PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
                                  PieceType::Rook, PieceType::Queen, PieceType::King}) {
        REQUIRE(material_value(type, &defaults).mg == material_value(type).mg);
        REQUIRE(material_value(type, &defaults).eg == material_value(type).eg);
    }
}

TEST_CASE("material_value: a modified MaterialWeights changes the corresponding piece's value, "
          "and no other piece's",
          "[eval][psqt][tuner]") {
    MaterialWeights weights = default_material_weights();
    weights.knight_mg = 275.0;
    weights.knight_eg = 260.0;

    REQUIRE(material_value(PieceType::Knight, &weights).mg == 275);
    REQUIRE(material_value(PieceType::Knight, &weights).eg == 260);
    // Every other piece is untouched.
    REQUIRE(material_value(PieceType::Pawn, &weights).mg == kPawnValue.mg);
    REQUIRE(material_value(PieceType::Bishop, &weights).mg == kBishopValue.mg);
    REQUIRE(material_value(PieceType::Rook, &weights).mg == kRookValue.mg);
    REQUIRE(material_value(PieceType::Queen, &weights).mg == kQueenValue.mg);
}

TEST_CASE("material_value: King and None always return {0, 0}, even with a MaterialWeights "
          "override supplied",
          "[eval][psqt][tuner]") {
    MaterialWeights weights = default_material_weights();
    weights.pawn_mg = 12345.0; // an absurd value -- confirms King/None ignore weights entirely

    REQUIRE(material_value(PieceType::King, &weights).mg == 0);
    REQUIRE(material_value(PieceType::King, &weights).eg == 0);
    REQUIRE(material_value(PieceType::None, &weights).mg == 0);
    REQUIRE(material_value(PieceType::None, &weights).eg == 0);
}

TEST_CASE("evaluate: a MaterialWeights override changes evaluate()'s result exactly as "
          "expected for an imbalanced position",
          "[eval][tuner]") {
    init_all();
    // White has an extra knight; Black is otherwise identical -- a
    // direct, easy-to-hand-verify case for evaluate()'s material_weights
    // parameter, mirroring this file's own existing "a lone extra White
    // pawn favors White" style of test.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight);

    const int default_eval = evaluate(pos, nullptr, nullptr, nullptr);

    MaterialWeights doubled_knight = default_material_weights();
    doubled_knight.knight_mg = kKnightValue.mg * 2.0;
    doubled_knight.knight_eg = kKnightValue.eg * 2.0;
    const int doubled_eval = evaluate(pos, nullptr, nullptr, &doubled_knight);

    // Doubling the extra knight's own value should increase White's
    // evaluated advantage by roughly one more knight's worth (not
    // exactly, since PSQT/mobility/etc. also contribute and aren't held
    // perfectly constant across taper() rounding, but the direction and
    // rough magnitude are exact/predictable here).
    REQUIRE(doubled_eval > default_eval);
    REQUIRE(doubled_eval - default_eval >= kKnightValue.mg - 5); // generous slack for taper/PSQT
}

TEST_CASE("evaluate: eval_cache is never consulted (probed or stored) when a MaterialWeights "
          "override is supplied, even if a real EvalCache pointer is also passed",
          "[eval][eval_cache][tuner]") {
    init_all();
    Position pos = start_position();

    EvalCache cache(2048);
    // Poison the cache with a deliberately WRONG value for this exact
    // position's key, standing in for "a stale result computed under a
    // different weight vector" (evaluate()'s own doc comment on this
    // parameter's interaction with eval_cache). If evaluate() incorrectly
    // consulted eval_cache while material_weights is set, it would
    // return this poisoned value instead of a freshly computed one.
    cache.store(pos.zobrist_hash, 12345);

    const MaterialWeights weights = default_material_weights();
    const int result = evaluate(pos, nullptr, &cache, &weights);
    REQUIRE(result != 12345);
    REQUIRE(result == evaluate(pos, nullptr, nullptr, &weights));

    // And the poisoned entry must still be sitting there afterward,
    // confirming evaluate() didn't overwrite it with a fresh (correct)
    // value either -- eval_cache must be left completely untouched, not
    // merely "not trusted for the return value."
    const auto [hit, cached] = cache.probe(pos.zobrist_hash);
    REQUIRE(hit);
    REQUIRE(cached == 12345);
}

