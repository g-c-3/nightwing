// src/eval/minor_piece_endgame.cpp
//
// See minor_piece_endgame.h for the constants this file applies (all
// first-draft hand estimates, not yet Texel-tuned -- same caveat every
// other eval term in this codebase already carries) and for the
// overall scope. Three independent sub-patterns, one per
// EndgameSignature bucket, dispatched by minor_piece_endgame_value()
// at the bottom of this file:
//
// 1. Wrong bishop corner (EndgameSignature::KBPK): reuses the exact
//    rule-of-the-square technique eval/king_pawn_endgame.cpp's own KPK
//    term already established (re-derived locally here, per this
//    codebase's established per-file convention for this kind of
//    small geometric helper -- eval/pawns.cpp's and eval/
//    king_pawn_endgame.cpp's own local relative_rank() already set
//    this precedent) -- but checked against the DEFENDING king's
//    ability to reach the drawing CORNER rather than the attacking
//    pawn's own promotion square being caught. Deliberately narrowed
//    to the case where every one of the attacking side's pawns is on
//    a single rook file (a- or h-file) -- CPW's "Wrong Bishop"
//    fortress specifically depends on the defending king being able to
//    shuttle between the corner square and its only adjacent escape
//    square, which stops being true the moment a second, non-rook-file
//    pawn exists for the attacker to queen instead (at that point it's
//    simply a won bishop endgame, and ordinary material/PSQT eval
//    already gives the correct signal without this term's help).
//
// 2. Opposite-colored bishops (EndgameSignature::OppositeColoredBishops):
//    a flat per-pawn-difference discount against whichever side has
//    more pawns, NOT a flat bonus applied regardless of material
//    balance. A flat always-on bonus was considered and rejected: at
//    exact pawn parity there's no material lead for a "these endings
//    are drawish" adjustment to actually discount, and introducing a
//    nonzero score in an otherwise balanced position would be an
//    unjustified nudge with no classical technique behind it --
//    exactly the same reasoning eval/king_pawn_endgame.cpp's own
//    header comment already gives for declining to guess a sign in
//    its own genuinely-ambiguous fallthrough case.
//
// 3. Knight vs. bishop (EndgameSignature::KnightVsBishop): a simple,
//    cheap structural proxy for "how closed is this position" --
//    counting pawns (either color) whose immediate push square is
//    occupied by an opposing pawn (a direct, mutual blockade) --
//    rather than anything requiring full mobility/attack-table
//    analysis. This is a real simplification (a position can be
//    "closed" in ways this simple per-file check doesn't capture, e.g.
//    a pawn chain blocked several squares ahead rather than
//    immediately) but stays consistent with the same "first-draft hand
//    estimate over an elaborate but unverified formula" posture this
//    entire file and eval/rook_endgame.cpp/eval/king_pawn_endgame.cpp
//    already take throughout Phase 6.

#include "eval/minor_piece_endgame.h"

#include "eval/endgame.h"

namespace nightwing::eval {

namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Relative rank of `sq` from `c`'s own perspective: 0 = `c`'s own back
/// rank, 7 = the opposite back rank (where `c` promotes). Re-derived
/// locally rather than shared -- matches this codebase's established
/// per-file convention for this exact computation (eval/pawns.cpp's
/// and eval/king_pawn_endgame.cpp's own identical local helpers).
[[nodiscard]] constexpr int relative_rank(Color c, Square sq) noexcept {
    const int rank = board::rank_of(sq);
    return c == Color::White ? rank : 7 - rank;
}

/// Returns true if `sq` is a light square. Re-derived locally rather
/// than shared with eval/endgame.cpp's own identical (and, there,
/// anonymous-namespace-private) helper -- see this file's own header
/// comment and endgame.cpp's own updated comment on why KBPK
/// deliberately doesn't need to share this computation with
/// classify_endgame() itself, even though both files independently
/// need it.
[[nodiscard]] constexpr bool is_light_square(Square sq) noexcept {
    return ((board::file_of(sq) + board::rank_of(sq)) % 2) == 1;
}

/// Wrong bishop corner (EndgameSignature::KBPK) -- see this file's own
/// header comment for the full recognition criteria. Returns a value
/// relative to the ATTACKER (the side with the bishop and pawn(s)) --
/// the caller applies the final color sign-flip, matching eval/
/// king_pawn_endgame.cpp's and eval/rook_endgame.cpp's own established
/// convention.
[[nodiscard]] Score wrong_bishop_corner_value(const Position& pos, Color attacker) noexcept {
    const Bitboard attacker_pawns = pos.pieces(attacker, PieceType::Pawn);

    // Every attacking pawn must be on the SAME file, and that file
    // must be a rook file -- both checked by inspecting the first
    // pawn found and then confirming every other pawn shares its file.
    const Square first_pawn_sq = board::bitscan_forward(attacker_pawns);
    const int pawn_file = board::file_of(first_pawn_sq);
    if (pawn_file != 0 && pawn_file != 7) {
        return Score{};
    }
    Bitboard remaining = attacker_pawns;
    int most_advanced_rel_rank = relative_rank(attacker, first_pawn_sq);
    while (remaining != 0) {
        const Square sq = board::pop_lsb(remaining);
        if (board::file_of(sq) != pawn_file) {
            return Score{}; // a second, non-rook-file pawn -- not this fortress pattern
        }
        const int rr = relative_rank(attacker, sq);
        if (rr > most_advanced_rel_rank) {
            most_advanced_rel_rank = rr;
        }
    }

    const Square corner_sq = board::make_square(pawn_file, (attacker == Color::White) ? 7 : 0);
    const Square bishop_sq = board::bitscan_forward(pos.pieces(attacker, PieceType::Bishop));
    if (is_light_square(bishop_sq) == is_light_square(corner_sq)) {
        return Score{}; // the RIGHT bishop for this corner -- an ordinary win, no adjustment
    }

    // Rule of the square, applied to the MOST ADVANCED attacking pawn
    // and the drawing corner rather than the pawn's own promotion
    // square -- identical technique to eval/king_pawn_endgame.cpp's
    // own KPK term (re-derived locally, not shared -- see this file's
    // own header comment).
    int pawn_moves_to_promote =
        (7 - most_advanced_rel_rank) - (most_advanced_rel_rank == 1 ? 1 : 0);
    if (pawn_moves_to_promote < 0) {
        pawn_moves_to_promote = 0;
    }
    const Color defender = board::opposite(attacker);
    const Square defender_king_sq = board::bitscan_forward(pos.pieces(defender, PieceType::King));
    const int king_moves_to_corner = board::chebyshev_distance(defender_king_sq, corner_sq);
    int defender_moves_available =
        pawn_moves_to_promote - (pos.side_to_move == attacker ? 1 : 0);
    if (defender_moves_available < 0) {
        defender_moves_available = 0;
    }

    if (king_moves_to_corner <= defender_moves_available) {
        return kWrongBishopCornerDrawPenalty;
    }
    // Defending king can't reach the corner in time regardless of
    // which bishop it is -- an ordinary win, no adjustment.
    return Score{};
}

/// Opposite-colored bishops (EndgameSignature::OppositeColoredBishops)
/// -- see this file's own header comment for why this is proportional
/// to the pawn-count difference only, never a flat always-on bonus.
/// Returns a White-relative Score directly (unlike the other two
/// sub-patterns in this file, there's no single "attacker" side here
/// to be relative to -- either side could hold the larger pawn count).
[[nodiscard]] Score opposite_colored_bishops_value(const Position& pos) noexcept {
    const int white_pawns = board::popcount(pos.pieces(Color::White, PieceType::Pawn));
    const int black_pawns = board::popcount(pos.pieces(Color::Black, PieceType::Pawn));
    const int diff = white_pawns - black_pawns;
    if (diff == 0) {
        return Score{};
    }
    const Score penalty = kOCBDrawishPenaltyPerExtraPawn * (diff > 0 ? diff : -diff);
    // `penalty` is a negative-magnitude Score -- apply it against
    // whichever side has the pawn-count lead (diff > 0 means White
    // leads), matching eval/king_pawn_endgame.cpp's and eval/
    // rook_endgame.cpp's own "adjustment already encodes good/bad for
    // the relevant side, caller picks the sign" convention.
    return (diff > 0) ? penalty : -penalty;
}

/// Knight vs. bishop (EndgameSignature::KnightVsBishop) -- see this
/// file's own header comment for the blocked-pawn closedness proxy.
/// Returns a White-relative Score directly, for the same reason
/// opposite_colored_bishops_value() above does.
[[nodiscard]] Score knight_vs_bishop_value(const Position& pos) noexcept {
    const Bitboard white_pawns = pos.pieces(Color::White, PieceType::Pawn);
    const Bitboard black_pawns = pos.pieces(Color::Black, PieceType::Pawn);
    const int total_pawns = board::popcount(white_pawns) + board::popcount(black_pawns);

    int blocked_pawns = 0;
    for (const Color c : {Color::White, Color::Black}) {
        const Color them = board::opposite(c);
        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(them, PieceType::Pawn);
        Bitboard bb = own_pawns;
        while (bb != 0) {
            const Square sq = board::pop_lsb(bb);
            const int push_rank =
                (c == Color::White) ? board::rank_of(sq) + 1 : board::rank_of(sq) - 1;
            // A pawn one push from promoting has no push square left on
            // the board -- defensive only, a real position never has a
            // pawn resting on the promotion rank anyway.
            if (push_rank < 0 || push_rank >= board::kNumRanks) {
                continue;
            }
            const Square push_sq = board::make_square(board::file_of(sq), push_rank);
            if ((enemy_pawns & board::square_bb(push_sq)) != 0) {
                ++blocked_pawns;
            }
        }
    }
    const int open_pawns = total_pawns - blocked_pawns;

    const bool white_has_knight = pos.pieces(Color::White, PieceType::Knight) != 0;
    const Color knight_side = white_has_knight ? Color::White : Color::Black;
    const Color bishop_side = board::opposite(knight_side);

    const Score knight_bonus = kKnightClosedPositionBonusPerBlockedPawn * blocked_pawns;
    const Score bishop_bonus = kBishopOpenPositionBonusPerOpenPawn * open_pawns;

    Score result;
    result += (knight_side == Color::White) ? knight_bonus : -knight_bonus;
    result += (bishop_side == Color::White) ? bishop_bonus : -bishop_bonus;
    return result;
}

} // namespace

Score minor_piece_endgame_value(const Position& pos) noexcept {
    switch (classify_endgame(pos)) {
        case EndgameSignature::KBPK: {
            const bool white_side = pos.pieces(Color::White, PieceType::Bishop) != 0;
            const Color attacker = white_side ? Color::White : Color::Black;
            const Score adjustment = wrong_bishop_corner_value(pos, attacker);
            return (attacker == Color::White) ? adjustment : -adjustment;
        }
        case EndgameSignature::OppositeColoredBishops:
            return opposite_colored_bishops_value(pos);
        case EndgameSignature::KnightVsBishop:
            return knight_vs_bishop_value(pos);
        case EndgameSignature::None:
        case EndgameSignature::KPK:
        case EndgameSignature::KRK:
        case EndgameSignature::KBNK:
        case EndgameSignature::RookEndgame:
        default:
            return Score{};
    }
}

} // namespace nightwing::eval
