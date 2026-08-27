// src/eval/knight_outposts.cpp
//
// See knight_outposts.h.

#include "eval/knight_outposts.h"

#include "board/bitboard.h"
#include "board/masks.h"

namespace nightwing::eval {
namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Relative rank of `sq` from `c`'s own perspective: 0 = `c`'s own back
/// rank, 7 = the opposite back rank (where `c` promotes). Same helper,
/// duplicated locally rather than shared, as eval/pawns.cpp's own
/// file-private relative_rank() -- a small enough computation that this
/// codebase's established convention (see eval/king_safety.cpp's own
/// ad-hoc shield_zone(), docs/DECISIONS.md 2026-08-26 (6)) is to keep
/// it local to whichever file needs it rather than promote it to a
/// shared header for two current callers.
[[nodiscard]] constexpr int relative_rank(Color c, Square sq) noexcept {
    const int rank = board::rank_of(sq);
    return c == Color::White ? rank : 7 - rank;
}

/// Returns true if relative rank `relative_rank(c, sq)` falls within the
/// window where a knight outpost is considered meaningful -- ranks 4
/// through 6 from `c`'s own perspective (relative rank 3..5, 0-indexed),
/// the standard "in or near enemy territory" restriction CPW's own
/// "Outposts" article describes: a knight parked on its own side of the
/// board isn't exerting the kind of forward pressure an outpost bonus
/// is meant to reward, even if it happens to be technically
/// pawn-defended and technically unreachable by an enemy pawn.
[[nodiscard]] constexpr bool is_outpost_rank(Color c, Square sq) noexcept {
    const int rel = relative_rank(c, sq);
    return rel >= 3 && rel <= 5;
}

/// Returns true if a knight of color `c` standing on `sq` would qualify
/// as being on an outpost square: defended by an own pawn (the standard
/// reverse-pawn-attack trick, same as eval/pawns.cpp's own "Connected"
/// check -- "square X is attacked by a pawn of color K exactly when
/// pawn_attacks(!K, X) intersects K's pawns"), never attackable by an
/// enemy pawn (no enemy pawn on either adjacent file still able to
/// advance far enough to reach `sq` -- reusing board::passed_pawn_mask()
/// intersected with board::adjacent_files_mask() rather than adding a
/// new masks.h function: passed_pawn_mask(c, sq) already returns every
/// square strictly ahead of `sq` from `c`'s own perspective across
/// `sq`'s file and both neighbors -- exactly the span an enemy pawn
/// would have to currently occupy to still be capable of one day
/// capturing onto `sq`; ANDing with adjacent_files_mask() narrows that
/// down to the two files that actually matter for a pawn CAPTURE
/// specifically, excluding `sq`'s own file, which a pawn can block but
/// never capture on), and within the outpost rank window above.
[[nodiscard]] bool is_outpost_square(const Position& pos, Color c, Square sq) noexcept {
    if (!is_outpost_rank(c, sq)) {
        return false;
    }

    const Color them = board::opposite(c);
    const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
    const Bitboard enemy_pawns = pos.pieces(them, PieceType::Pawn);

    const bool defended = (board::pawn_attacks(them, sq) & own_pawns) != 0;
    if (!defended) {
        return false;
    }

    const Bitboard enemy_threat_span =
        board::passed_pawn_mask(c, sq) & board::adjacent_files_mask(board::file_of(sq));
    return (enemy_pawns & enemy_threat_span) == 0;
}

} // namespace

Score knight_outpost_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        Score side_score;

        Bitboard knights = pos.pieces(c, PieceType::Knight);
        while (knights != 0) {
            const Square sq = board::pop_lsb(knights);
            if (is_outpost_square(pos, c, sq)) {
                side_score += kKnightOutpostBonus;
            }
        }

        if (c == Color::White) {
            score += side_score;
        } else {
            score -= side_score;
        }
    }

    return score;
}

} // namespace nightwing::eval
