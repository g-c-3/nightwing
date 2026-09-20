// tests/incremental_eval_tests.cpp
//
// Unit tests for src/eval/incremental.h (compute_material_psqt(),
// material_psqt_delta()) and eval::evaluate()'s own
// `incremental_material_psqt` parameter (src/eval/eval.h/eval.cpp).
// ROADMAP.md's NPS/Raw Speed track, "Incremental evaluation" item --
// docs/DECISIONS.md has the full design rationale for why the
// accumulator lives here (a pure function taking a Move + UndoInfo)
// rather than as a board::Position field.
//
// Each test's own pattern: compute compute_material_psqt() BEFORE a
// move, apply the move for real via board::make_move(), compute
// material_psqt_delta() for that exact move, and confirm
// `before + delta` equals a fresh compute_material_psqt() AFTER the
// move -- i.e. the delta is EXACTLY what a full rescan would have
// produced, for every case that changes the calculation differently:
// a quiet move, a regular capture, en passant, a quiet promotion, a
// capture-promotion, and all four castling sides.

#include <catch2/catch_test_macros.hpp>

#include "board/attacks.h"
#include "board/board.h"
#include "board/fen.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "eval/incremental.h"

using namespace nightwing::board;
using namespace nightwing::eval;

namespace {

/// Test-only helper: finds the legal move in `pos` whose UCI notation
/// (e.g. "e2e4", "e7e8q") matches `uci`. Fails the calling test via
/// REQUIRE if no such move exists, rather than returning a sentinel a
/// caller could silently misuse.
Move find_move(const Position& pos, const std::string& uci) {
    MoveList moves;
    generate_legal_moves(pos, moves);
    for (int i = 0; i < moves.size(); ++i) {
        const Move m = moves[i];
        std::string s;
        s.push_back(static_cast<char>('a' + file_of(m.from())));
        s.push_back(static_cast<char>('1' + rank_of(m.from())));
        s.push_back(static_cast<char>('a' + file_of(m.to())));
        s.push_back(static_cast<char>('1' + rank_of(m.to())));
        if (m.is_promotion()) {
            char c = 'q';
            switch (m.promotion_piece_type()) {
                case PieceType::Knight: c = 'n'; break;
                case PieceType::Bishop: c = 'b'; break;
                case PieceType::Rook: c = 'r'; break;
                default: c = 'q'; break;
            }
            s.push_back(c);
        }
        if (s == uci) {
            return m;
        }
    }
    FAIL("find_move: no legal move matches " << uci);
    return moves[0]; // unreachable -- FAIL() throws in Catch2's default configuration
}

/// Shared assertion body for every TEST_CASE below: applies `uci_move`
/// to a Position parsed from `fen`, and requires that
/// compute_material_psqt() BEFORE the move, plus material_psqt_delta()
/// for that move, equals compute_material_psqt() AFTER the move --
/// exactly. Also cross-checks eval::evaluate()'s own incremental path
/// against its default from-scratch path on the resulting position, so
/// a bug in evaluate()'s own use of `incremental_material_psqt`
/// (rather than in material_psqt_delta()/compute_material_psqt()
/// themselves) would be caught here too.
void check_delta(const char* fen, const char* uci_move) {
    // Idempotent (ARCHITECTURE.md's own "Startup Sequence" -- init_masks()/
    // init_magic_bitboards()/init_zobrist_keys() are all safe to call
    // repeatedly): needed here, explicitly, rather than relying on an
    // earlier test file in the binary having already called them, since
    // Catch2 test filtering (running this file's own tests in isolation)
    // would otherwise leave movegen/eval/FEN-parsing touching
    // uninitialized tables. Same pattern tests/eval_cache_tests.cpp's own
    // TEST_CASEs already use.
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos = parse_fen(fen);
    const Move move = find_move(pos, uci_move);
    const Score before = compute_material_psqt(pos);

    const Color mover = pos.side_to_move;
    const PieceType moved_type = piece_type_of(pos.piece_at(move.from()));

    UndoInfo undo;
    make_move(pos, move, undo);

    const Score delta = material_psqt_delta(mover, moved_type, move, undo);
    const Score predicted = before + delta;
    const Score actual = compute_material_psqt(pos);

    REQUIRE(predicted.mg == actual.mg);
    REQUIRE(predicted.eg == actual.eg);

    REQUIRE(evaluate(pos) == evaluate(pos, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &actual));

    unmake_move(pos, move, undo);
}

} // namespace

TEST_CASE("material_psqt_delta: quiet pawn push matches a full rescan",
          "[eval][incremental]") {
    check_delta("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4");
}

TEST_CASE("material_psqt_delta: quiet knight move matches a full rescan",
          "[eval][incremental]") {
    check_delta("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "g1f3");
}

TEST_CASE("material_psqt_delta: a regular capture matches a full rescan (material AND PSQT "
          "both change -- the captured piece disappears from its own square)",
          "[eval][incremental]") {
    check_delta("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "e4d5");
}

TEST_CASE("material_psqt_delta: en passant matches a full rescan -- the captured pawn is "
          "removed from a DIFFERENT square than move.to(), the one case where this matters",
          "[eval][incremental]") {
    check_delta("rnbqkbnr/1ppp1ppp/8/pP2p3/8/8/P1PPPPPP/RNBQKBNR w KQkq a6 0 3", "b5a6");
}

TEST_CASE("material_psqt_delta: a quiet promotion matches a full rescan (material changes -- "
          "a Pawn becomes a Queen -- with no capture involved)",
          "[eval][incremental]") {
    check_delta("8/P7/8/8/8/8/8/k6K w - - 0 1", "a7a8q");
}

TEST_CASE("material_psqt_delta: a capture-promotion matches a full rescan (both the "
          "promotion's own material change AND the captured piece's removal apply at once)",
          "[eval][incremental]") {
    check_delta("1n6/P7/8/8/8/8/8/k6K w - - 0 1", "a7b8q");
}

TEST_CASE("material_psqt_delta: underpromotion (to a knight, not a queen) matches a full "
          "rescan -- move.promotion_piece_type() must be honored, not just \"is this a "
          "promotion\"",
          "[eval][incremental]") {
    check_delta("8/P7/8/8/8/8/8/k6K w - - 0 1", "a7a8n");
}

TEST_CASE("material_psqt_delta: White kingside castling matches a full rescan -- the "
          "secondary rook relocation (h1->f1) must be accounted for, not just the king's move",
          "[eval][incremental]") {
    check_delta("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1");
}

TEST_CASE("material_psqt_delta: White queenside castling matches a full rescan (rook a1->d1)",
          "[eval][incremental]") {
    check_delta("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1");
}

TEST_CASE("material_psqt_delta: Black kingside castling matches a full rescan (rook h8->f8)",
          "[eval][incremental]") {
    check_delta("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8");
}

TEST_CASE("material_psqt_delta: Black queenside castling matches a full rescan (rook a8->d8)",
          "[eval][incremental]") {
    check_delta("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8");
}

TEST_CASE("material_psqt_delta: a chain of several moves in a row still matches a full "
          "rescan at every step -- confirms the delta composes correctly across positions, "
          "not just from a single fixed starting point",
          "[eval][incremental]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Score running = compute_material_psqt(pos);

    const char* moves[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"};
    for (const char* uci : moves) {
        const Move move = find_move(pos, uci);
        const Color mover = pos.side_to_move;
        const PieceType moved_type = piece_type_of(pos.piece_at(move.from()));

        UndoInfo undo;
        make_move(pos, move, undo);

        running += material_psqt_delta(mover, moved_type, move, undo);
        const Score actual = compute_material_psqt(pos);
        REQUIRE(running.mg == actual.mg);
        REQUIRE(running.eg == actual.eg);
    }
}

TEST_CASE("evaluate(): incremental_material_psqt is ignored (falls back to a full rescan) "
          "whenever material_weights is non-null -- an accumulator maintained under the "
          "compiled-in constants must never be trusted against a tuner's candidate weights",
          "[eval][incremental]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    MaterialWeights weights{}; // starts equal to the compiled-in constants (its own default
                                // member initializers) -- explicitly perturbed below so it's
                                // guaranteed to disagree with them.
    weights.pawn_mg = 1.0;
    weights.pawn_eg = 1.0;
    weights.queen_mg = 1.0;
    weights.queen_eg = 1.0;
    const Score wrong_accumulator{99999, 99999}; // deliberately absurd, to prove it's unused
    const int with_weights_ignoring_bad_accumulator =
        evaluate(pos, nullptr, nullptr, &weights, nullptr, nullptr, nullptr, nullptr, nullptr, &wrong_accumulator);
    const int with_weights_no_accumulator = evaluate(pos, nullptr, nullptr, &weights);
    REQUIRE(with_weights_ignoring_bad_accumulator == with_weights_no_accumulator);
}

TEST_CASE("evaluate(): a correct incremental_material_psqt produces the exact same result as "
          "the default from-scratch path, for a real position reached via make_move()",
          "[eval][incremental]") {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();

    Position pos = parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const Move move = find_move(pos, "d2d4");
    const Color mover = pos.side_to_move;
    const PieceType moved_type = piece_type_of(pos.piece_at(move.from()));
    const Score before = compute_material_psqt(pos);

    UndoInfo undo;
    make_move(pos, move, undo);
    const Score after = before + material_psqt_delta(mover, moved_type, move, undo);

    REQUIRE(evaluate(pos) == evaluate(pos, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &after));
}
