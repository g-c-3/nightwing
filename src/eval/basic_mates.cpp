// src/eval/basic_mates.cpp
//
// See basic_mates.h for the overall scope. Two sub-terms, dispatched
// by basic_mate_value() at the bottom of this file, each returning a
// value relative to the ATTACKER (the side with the mating material)
// before the caller applies the final color sign-flip, matching every
// other Phase 6 eval term's own established convention:
//
// 1. KRK (EndgameSignature::KRK): a generic "push the defending king
//    to any edge" term plus a "bring the attacking king closer" term.
//    CPW's own "King and Rook vs King" article describes the technique
//    as confining the defending king to a shrinking box with the rook,
//    then bringing the attacking king in to finish -- these two terms
//    are exactly that, expressed as continuous formulas rather than a
//    staged, phase-detecting algorithm. No explicit "rook confines the
//    box" term is included -- ordinary search, given the king-position
//    incentives below, reliably finds the rook moves that create and
//    shrink the box on its own, the same way it finds tactics in any
//    other position; encoding rook placement directly as its own eval
//    term risked being both harder to get right and less necessary
//    than the two king-position terms already provide.
//
// 2. KBNK (EndgameSignature::KBNK): the same two generic terms as KRK,
//    plus the one genuinely KBNK-specific addition -- a term rewarding
//    the defending king's proximity to whichever pair of corners the
//    attacking side's BISHOP actually controls (CPW "King, Bishop and
//    Knight vs King" -- the reason this is "the hardest of the basic
//    mates" is precisely that only bishop-colored corners work; the
//    other two corners are safe for the defending king no matter how
//    close it gets pushed there otherwise).
//
// Every constant this file applies is declared in basic_mates.h
// (matching every other Phase 6 term's own convention) and is a
// first-draft hand estimate, not yet Texel-tuned, same caveat every
// other eval term in this codebase already carries.

#include "eval/basic_mates.h"

#include "eval/endgame.h"

namespace nightwing::eval {

namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Returns true if `sq` is a light square. Re-derived locally rather
/// than shared -- matches this codebase's established per-file
/// convention for this exact computation (eval/endgame.cpp's and
/// eval/minor_piece_endgame.cpp's own identical local helpers).
[[nodiscard]] constexpr bool is_light_square(Square sq) noexcept {
    return ((board::file_of(sq) + board::rank_of(sq)) % 2) == 1;
}

/// Generic "distance from the board's center" for `sq`: 2 at the
/// four center squares, 14 at any corner -- CPW's own standard metric
/// for "how close to the edge is this king," used identically by both
/// the KRK and KBNK sub-terms below.
[[nodiscard]] constexpr int edge_push_score(Square sq) noexcept {
    const int file_term = (2 * board::file_of(sq) - 7);
    const int rank_term = (2 * board::rank_of(sq) - 7);
    return (file_term < 0 ? -file_term : file_term) + (rank_term < 0 ? -rank_term : rank_term);
}

/// KRK (EndgameSignature::KRK) -- see this file's own header comment.
/// Returns a value relative to the attacker (the side with the rook).
[[nodiscard]] Score krk_value(Color attacker, Square attacker_king_sq,
                               Square defender_king_sq) noexcept {
    const int edge_term = edge_push_score(defender_king_sq);
    const int proximity_term = 7 - board::chebyshev_distance(attacker_king_sq, defender_king_sq);
    Score result;
    result += kKRKEdgePushWeight * edge_term;
    result += kKRKKingProximityWeight * proximity_term;
    return (attacker == Color::White) ? result : -result;
}

/// KBNK (EndgameSignature::KBNK) -- see this file's own header
/// comment. Returns a value relative to the attacker (the side with
/// the bishop and knight).
[[nodiscard]] Score kbnk_value(const Position& pos, Color attacker, Square attacker_king_sq,
                                Square defender_king_sq) noexcept {
    const Square bishop_sq = board::bitscan_forward(pos.pieces(attacker, PieceType::Bishop));
    const bool bishop_is_light = is_light_square(bishop_sq);

    // The two corners the bishop actually controls: a8/h1 if light,
    // a1/h8 if dark (board/board.h's make_square(file, rank), both
    // 0-indexed).
    const Square corner_a = bishop_is_light ? board::make_square(0, 7) : board::make_square(0, 0);
    const Square corner_b = bishop_is_light ? board::make_square(7, 0) : board::make_square(7, 7);
    const int dist_a = board::chebyshev_distance(defender_king_sq, corner_a);
    const int dist_b = board::chebyshev_distance(defender_king_sq, corner_b);
    const int dist_to_correct_corner = dist_a < dist_b ? dist_a : dist_b;
    const int corner_term = 7 - dist_to_correct_corner;

    const int edge_term = edge_push_score(defender_king_sq);
    const int proximity_term = 7 - board::chebyshev_distance(attacker_king_sq, defender_king_sq);

    Score result;
    result += kKBNKCornerColorWeight * corner_term;
    result += kKBNKEdgePushWeight * edge_term;
    result += kKBNKKingProximityWeight * proximity_term;
    return (attacker == Color::White) ? result : -result;
}

} // namespace

Score basic_mate_value(const Position& pos) noexcept {
    const EndgameSignature sig = classify_endgame(pos);
    if (sig != EndgameSignature::KRK && sig != EndgameSignature::KBNK) {
        return Score{};
    }

    const bool white_is_attacker = (sig == EndgameSignature::KRK)
                                        ? (pos.pieces(Color::White, PieceType::Rook) != 0)
                                        : (pos.pieces(Color::White, PieceType::Bishop) != 0);
    const Color attacker = white_is_attacker ? Color::White : Color::Black;
    const Color defender = board::opposite(attacker);

    const Bitboard attacker_king_bb = pos.pieces(attacker, PieceType::King);
    const Bitboard defender_king_bb = pos.pieces(defender, PieceType::King);
    if (attacker_king_bb == 0 || defender_king_bb == 0) {
        // Defensive only -- same reasoning as every other Phase 6
        // term's identical guard (a hand-built test position could
        // omit a king; a real, legally-reached position never does).
        return Score{};
    }
    const Square attacker_king_sq = board::bitscan_forward(attacker_king_bb);
    const Square defender_king_sq = board::bitscan_forward(defender_king_bb);

    if (sig == EndgameSignature::KRK) {
        return krk_value(attacker, attacker_king_sq, defender_king_sq);
    }
    return kbnk_value(pos, attacker, attacker_king_sq, defender_king_sq);
}

} // namespace nightwing::eval
