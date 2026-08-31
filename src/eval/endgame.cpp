// src/eval/endgame.cpp
//
// See endgame.h.

#include "eval/endgame.h"

namespace nightwing::eval {

namespace {

using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Returns true if `sq` is a light square (the standard chess board
/// coloring: a1 is dark, b1 is light, alternating). LERF mapping
/// (board/bitboard.h's own header comment: square = rank * 8 + file,
/// a1 = 0) means (file + rank) is even on every dark square and odd on
/// every light one -- checked directly against a1 (file 0, rank 0,
/// sum 0, dark, matching the real board) and h1 (file 7, rank 0, sum
/// 7, light, also matching). Local to this file: only
/// OppositeColoredBishops (endgame.h) needs a square's color right
/// now -- KBPK (endgame.h, added Session 66, the "future wrong bishop
/// corner signature" this comment used to say might need this function
/// "more broadly") turned out NOT to need it after all: classify_endgame()
/// deliberately classifies KBPK by piece counts alone, leaving the
/// actual bishop-vs-corner-color judgment to that bucket's later
/// consumer (see EndgameSignature::KBPK's own doc comment in
/// endgame.h for why).
[[nodiscard]] constexpr bool is_light_square(Square sq) noexcept {
    return ((board::file_of(sq) + board::rank_of(sq)) % 2) == 1;
}

/// One side's piece counts, gathered once and passed around by value
/// (four ints, cheap to copy) rather than re-querying `pos` for each
/// candidate signature check below -- keeps classify_endgame()'s own
/// body a sequence of readable comparisons instead of repeated
/// pos.pieces(...)/popcount() calls.
struct SideCounts {
    int pawns = 0;
    int knights = 0;
    int bishops = 0;
    int rooks = 0;
    int queens = 0;
};

[[nodiscard]] SideCounts count_side(const Position& pos, Color c) noexcept {
    return SideCounts{
        board::popcount(pos.pieces(c, PieceType::Pawn)),
        board::popcount(pos.pieces(c, PieceType::Knight)),
        board::popcount(pos.pieces(c, PieceType::Bishop)),
        board::popcount(pos.pieces(c, PieceType::Rook)),
        board::popcount(pos.pieces(c, PieceType::Queen)),
    };
}

} // namespace

EndgameSignature classify_endgame(const Position& pos) noexcept {
    const SideCounts w = count_side(pos, Color::White);
    const SideCounts b = count_side(pos, Color::Black);

    const int total_pawns = w.pawns + b.pawns;
    const int total_knights = w.knights + b.knights;
    const int total_bishops = w.bishops + b.bishops;
    const int total_rooks = w.rooks + b.rooks;
    const int total_queens = w.queens + b.queens;

    // KPK: exactly one pawn on the whole board, nothing else besides
    // the two kings. Which side the pawn belongs to doesn't need an
    // explicit check here -- with nothing else on the board at all,
    // "vs. bare king" is automatic.
    if (total_pawns == 1 && total_knights == 0 && total_bishops == 0 && total_rooks == 0 &&
        total_queens == 0) {
        return EndgameSignature::KPK;
    }

    // KRK: exactly one rook on the whole board, no pawns, nothing else
    // -- same "automatic vs. bare king" reasoning as KPK above.
    if (total_rooks == 1 && total_pawns == 0 && total_knights == 0 && total_bishops == 0 &&
        total_queens == 0) {
        return EndgameSignature::KRK;
    }

    // KBNK: one bishop and one knight, no pawns, no rooks/queens, AND
    // (unlike KPK/KRK above) both minors must belong to the SAME side
    // -- this file's own header comment explains why that check can't
    // be skipped here the way it safely was above.
    if (total_pawns == 0 && total_rooks == 0 && total_queens == 0) {
        const bool white_has_both = (w.bishops == 1 && w.knights == 1 && b.bishops == 0 &&
                                      b.knights == 0);
        const bool black_has_both = (b.bishops == 1 && b.knights == 1 && w.bishops == 0 &&
                                      w.knights == 0);
        if (white_has_both || black_has_both) {
            return EndgameSignature::KBNK;
        }

        // KnightVsBishop: one side has exactly the knight, the other
        // has exactly the bishop, neither has both (that's KBNK,
        // already handled above) or neither (that falls through to
        // OppositeColoredBishops/RookEndgame/None below, as
        // appropriate). Pawns are allowed here (unlike KBNK) -- this
        // signature's whole point (endgame.h's own doc comment) is
        // weighing the knight-vs-bishop tradeoff BY pawn structure,
        // so requiring zero pawns would make it useless for that.
    }
    if (total_rooks == 0 && total_queens == 0) {
        const bool white_knight_black_bishop =
            (w.knights == 1 && w.bishops == 0 && b.bishops == 1 && b.knights == 0);
        const bool white_bishop_black_knight =
            (w.bishops == 1 && w.knights == 0 && b.knights == 1 && b.bishops == 0);
        if (white_knight_black_bishop || white_bishop_black_knight) {
            return EndgameSignature::KnightVsBishop;
        }
    }

    // KBPK: one side has exactly one bishop and at least one pawn, the
    // other side is completely bare (checked explicitly below -- unlike
    // KPK/KRK above, "at least one pawn" doesn't by itself make "vs.
    // bare king" automatic, since the OTHER side could also have pawns
    // of its own). Checked before OppositeColoredBishops/RookEndgame
    // below since those both require pawns on potentially both sides,
    // a case this bucket's own bare-defender requirement already rules
    // out overlapping with.
    if (total_knights == 0 && total_rooks == 0 && total_queens == 0) {
        const bool white_side =
            (w.bishops == 1 && w.pawns >= 1 && b.bishops == 0 && b.pawns == 0);
        const bool black_side =
            (b.bishops == 1 && b.pawns >= 1 && w.bishops == 0 && w.pawns == 0);
        if (white_side || black_side) {
            return EndgameSignature::KBPK;
        }
    }

    // OppositeColoredBishops: each side has exactly one bishop, no
    // knights, no rooks/queens (any pawn count). Square-color check
    // last, since it's the only one of this function's checks that
    // isn't a plain integer comparison.
    if (w.bishops == 1 && b.bishops == 1 && total_knights == 0 && total_rooks == 0 &&
        total_queens == 0) {
        const Square white_bishop_sq = board::bitscan_forward(pos.pieces(Color::White, PieceType::Bishop));
        const Square black_bishop_sq = board::bitscan_forward(pos.pieces(Color::Black, PieceType::Bishop));
        if (is_light_square(white_bishop_sq) != is_light_square(black_bishop_sq)) {
            return EndgameSignature::OppositeColoredBishops;
        }
    }

    // RookEndgame: both sides have exactly one rook each, nothing else
    // besides pawns (any pawn count, including zero).
    if (w.rooks == 1 && b.rooks == 1 && total_knights == 0 && total_bishops == 0 &&
        total_queens == 0) {
        return EndgameSignature::RookEndgame;
    }

    return EndgameSignature::None;
}

} // namespace nightwing::eval
