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

TEST_CASE("psqt_value: the 5 non-king piece types currently return equal mg/eg at every square "
          "-- Tier 0 Step 1 (docs/DECISIONS.md, 2026-09-15) split each into a genuine Mg/Eg table "
          "pair, but seeded the new Eg tables as exact Mg duplicates, not a real tuned split yet. "
          "This test locks in that current state deliberately: it is EXPECTED to start failing, "
          "piece by piece, once a later Tier 0 step lands real Texel-tuned Eg values -- at that "
          "point this test should be narrowed to just the pieces still untuned, not deleted "
          "outright, so it keeps guarding whichever pieces haven't been tuned yet",
          "[eval][psqt]") {
    const Piece white_pieces[] = {Piece::WhitePawn, Piece::WhiteKnight, Piece::WhiteBishop,
                                   Piece::WhiteRook, Piece::WhiteQueen};
    for (Piece piece : white_pieces) {
        for (int sq = 0; sq < 64; ++sq) {
            const Score s = psqt_value(piece, static_cast<Square>(sq));
            REQUIRE(s.mg == s.eg);
        }
    }
}

TEST_CASE("psqt_value: the new per-piece Eg tables are genuinely wired up, not left at zero or "
          "reading stale Mg-table memory -- spot-checks a known non-zero/non-trivial value from "
          "each of the 5 newly-split tables directly against psqt.cpp's own transcribed constants",
          "[eval][psqt]") {
    // Pawn: rank-7 White pawn (one step from promoting), a7 -- Michniewski's
    // table's own maximal advancement bonus (50), same value in both
    // Mg/Eg since they're still duplicates (see test above).
    const Score pawn_rank7 = psqt_value(Piece::WhitePawn, make_square(0, 6)); // a7
    REQUIRE(pawn_rank7.mg == 50);
    REQUIRE(pawn_rank7.eg == 50);

    // Knight: a1 corner, the table's own most-penalized square (-50).
    const Score knight_corner = psqt_value(Piece::WhiteKnight, make_square(0, 0)); // a1
    REQUIRE(knight_corner.mg == -50);
    REQUIRE(knight_corner.eg == -50);

    // Bishop: d4/e5-adjacent central square (d3), a positive developed-
    // bishop entry (10) rather than the table's back-rank/corner 0/-20s.
    const Score bishop_dev = psqt_value(Piece::WhiteBishop, make_square(3, 2)); // d3
    REQUIRE(bishop_dev.mg == 10);
    REQUIRE(bishop_dev.eg == 10);

    // Rook: 7th-rank bonus row (rank index 6), d7 -- the table's own
    // +10 7th-rank entries for the 6 non-edge files (edge files a/h get
    // +5 instead, a separate value on the same row).
    const Score rook_7th = psqt_value(Piece::WhiteRook, make_square(3, 6)); // d7
    REQUIRE(rook_7th.mg == 10);
    REQUIRE(rook_7th.eg == 10);

    // Queen: a1 corner, the table's own most-penalized entry (-20).
    const Score queen_corner = psqt_value(Piece::WhiteQueen, make_square(0, 0)); // a1
    REQUIRE(queen_corner.mg == -20);
    REQUIRE(queen_corner.eg == -20);
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

TEST_CASE("default_psqt_weights: matches psqt_value()'s default (weights == nullptr) path "
          "exactly, at every square, for every piece type -- Tier 0 Step 2 (docs/DECISIONS.md, "
          "this entry's own date)",
          "[eval][psqt][tuner]") {
    init_all();
    const PsqtWeights w = default_psqt_weights();
    const Piece white_pieces[] = {Piece::WhitePawn,  Piece::WhiteKnight, Piece::WhiteBishop,
                                   Piece::WhiteRook,  Piece::WhiteQueen,  Piece::WhiteKing};
    const Piece black_pieces[] = {Piece::BlackPawn,  Piece::BlackKnight, Piece::BlackBishop,
                                   Piece::BlackRook,  Piece::BlackQueen,  Piece::BlackKing};
    for (int sq = 0; sq < 64; ++sq) {
        for (Piece piece : white_pieces) {
            const Score without = psqt_value(piece, static_cast<Square>(sq));
            const Score with = psqt_value(piece, static_cast<Square>(sq), &w);
            REQUIRE(with.mg == without.mg);
            REQUIRE(with.eg == without.eg);
        }
        for (Piece piece : black_pieces) {
            const Score without = psqt_value(piece, static_cast<Square>(sq));
            const Score with = psqt_value(piece, static_cast<Square>(sq), &w);
            REQUIRE(with.mg == without.mg);
            REQUIRE(with.eg == without.eg);
        }
    }
}

TEST_CASE("psqt_value: a perturbed PsqtWeights field changes only that field's own square/piece, "
          "not any other -- confirms the weights-supplied path actually reads the specific array "
          "index it's supposed to, not some other entry or a shared scalar",
          "[eval][psqt][tuner]") {
    init_all();
    PsqtWeights w = default_psqt_weights();
    const Square d4 = make_square(3, 3);
    const Score baseline = psqt_value(Piece::WhiteKnight, d4, &w);
    w.knight_mg[d4] += 37.0;
    const Score perturbed = psqt_value(Piece::WhiteKnight, d4, &w);
    REQUIRE(perturbed.mg == baseline.mg + 37);
    REQUIRE(perturbed.eg == baseline.eg); // knight_eg untouched

    // A different square on the same table is untouched.
    const Score other_square = psqt_value(Piece::WhiteKnight, make_square(0, 0), &w);
    REQUIRE(other_square.mg == psqt_value(Piece::WhiteKnight, make_square(0, 0)).mg);

    // A different piece type entirely is untouched.
    const Score other_piece = psqt_value(Piece::WhitePawn, d4, &w);
    REQUIRE(other_piece.mg == psqt_value(Piece::WhitePawn, d4).mg);
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

TEST_CASE("evaluate: a PsqtWeights override changes evaluate()'s result exactly as expected -- "
          "Tier 0 Step 4 (docs/DECISIONS.md, this entry's own dated wiring) mirrors the "
          "MaterialWeights override test above for the new psqt_weights parameter",
          "[eval][tuner]") {
    init_all();
    // Fuller non-pawn material on both sides (mirrored, so material
    // itself stays balanced) specifically to push compute_phase() well
    // above the near-fully-endgame phase a truly bare "lone knight vs
    // bare king" position would have -- this test's own expected
    // magnitude below was cross-checked against this exact position via
    // a real compiled-library probe (not hand-derived from taper()'s
    // formula alone), since a lower-phase position would blend mostly
    // toward the (untouched) eg table instead of exercising the mg
    // change this test is actually about.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteKnight); // a1 -- the test square
    pos.place_piece(make_square(1, 0), Piece::WhiteKnight); // b1
    pos.place_piece(make_square(2, 0), Piece::WhiteBishop); // c1
    pos.place_piece(make_square(5, 0), Piece::WhiteBishop); // f1
    pos.place_piece(make_square(3, 0), Piece::WhiteQueen);  // d1
    pos.place_piece(make_square(7, 0), Piece::WhiteRook);   // h1
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(1, 7), Piece::BlackKnight); // b8
    pos.place_piece(make_square(6, 7), Piece::BlackKnight); // g8
    pos.place_piece(make_square(2, 7), Piece::BlackBishop); // c8
    pos.place_piece(make_square(5, 7), Piece::BlackBishop); // f8
    pos.place_piece(make_square(3, 7), Piece::BlackQueen);  // d8
    pos.place_piece(make_square(7, 7), Piece::BlackRook);   // h8

    const int default_eval = evaluate(pos, nullptr, nullptr, nullptr, nullptr);

    // Zero out the corner penalty for this one square/phase pair only --
    // every other PSQT entry (including knight_eg[a1]) stays at its
    // compiled-in default.
    PsqtWeights boosted_knight_corner = default_psqt_weights();
    boosted_knight_corner.knight_mg[make_square(0, 0)] = 0.0;
    const int boosted_eval = evaluate(pos, nullptr, nullptr, nullptr, &boosted_knight_corner);

    // Removing a -50 mg penalty should increase White's evaluated
    // advantage by a healthy fraction of 50 (this position's own real
    // phase, not kMaxPhase exactly, and mobility/etc. shift slightly
    // too since removing the corner penalty doesn't move the knight) --
    // not exactly 50. Threshold picked well below the actual measured
    // delta at this exact position/phase, not derived by hand alone.
    REQUIRE(boosted_eval > default_eval);
    REQUIRE(boosted_eval - default_eval >= 25);

    // material_weights stays independent: passing both a no-op material
    // override AND the same psqt override produces the identical result
    // to the psqt-only override above -- the two parameters don't
    // interact or clobber one another.
    const MaterialWeights unchanged_material = default_material_weights();
    const int both_eval =
        evaluate(pos, nullptr, nullptr, &unchanged_material, &boosted_knight_corner);
    REQUIRE(both_eval == boosted_eval);
}

TEST_CASE("evaluate: eval_cache is never consulted (probed or stored) when a PsqtWeights "
          "override is supplied, even if a real EvalCache pointer is also passed -- the "
          "psqt_weights counterpart to the MaterialWeights/eval_cache test above",
          "[eval][eval_cache][tuner]") {
    init_all();
    Position pos = start_position();

    EvalCache cache(2048);
    cache.store(pos.zobrist_hash, 12345); // same poisoning technique as the MaterialWeights test

    const PsqtWeights weights = default_psqt_weights();
    const int result = evaluate(pos, nullptr, &cache, nullptr, &weights);
    REQUIRE(result != 12345);
    REQUIRE(result == evaluate(pos, nullptr, nullptr, nullptr, &weights));

    const auto [hit, cached] = cache.probe(pos.zobrist_hash);
    REQUIRE(hit);
    REQUIRE(cached == 12345); // untouched, not overwritten with a fresh value either
}


