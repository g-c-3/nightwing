// src/eval/space.cpp
//
// See space.h.

#include "eval/space.h"

#include "board/bitboard.h"
#include "board/masks.h"

namespace nightwing::eval {
namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// The lowest/highest FILE (0=a..7=h) included in the space zone --
/// the c through f files, CPW "Space"'s own standard central-files
/// scope for this concept.
constexpr int kSpaceZoneMinFile = 2; // c
constexpr int kSpaceZoneMaxFile = 5; // f

/// The lowest/highest RELATIVE rank (0 = `c`'s own back rank) included
/// in the space zone -- relative ranks 1..3 (ranks 2 through 4 from
/// `c`'s own side), CPW "Space"'s own standard "just ahead of one's own
/// back ranks" scope.
constexpr int kSpaceZoneMinRelativeRank = 1;
constexpr int kSpaceZoneMaxRelativeRank = 3;

/// Returns `c`'s own space zone: every square on the c/d/e/f files
/// within the relative-rank window above, computed directly with plain
/// file/rank arithmetic rather than via a new masks.h function --
/// unlike passed_pawn_mask()/backward_support_mask() (masks.h,
/// genuinely reused across many (color, square) combinations during
/// pawn structure evaluation), this zone only ever depends on `c`
/// itself, not on a specific square, so it's computed once per side per
/// space_value() call rather than needing a 64-entry precomputed table
/// -- the same "not worth promoting to a shared table" reasoning
/// eval/king_safety.cpp's own shield_zone() already documents (2026-08-
/// 26 (6)) for an analogous single-purpose zone.
[[nodiscard]] Bitboard space_zone(Color c) noexcept {
    Bitboard zone = board::kEmptyBitboard;

    for (int file = kSpaceZoneMinFile; file <= kSpaceZoneMaxFile; ++file) {
        for (int rel = kSpaceZoneMinRelativeRank; rel <= kSpaceZoneMaxRelativeRank; ++rel) {
            const int rank = (c == Color::White) ? rel : (7 - rel);
            board::set_bit(zone, board::make_square(file, rank));
        }
    }

    return zone;
}

} // namespace

Score space_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        const Color enemy = board::opposite(c);
        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(enemy, PieceType::Pawn);

        int safe_squares = 0;
        Bitboard zone = space_zone(c);
        while (zone != 0) {
            const Square sq = board::pop_lsb(zone);

            if (board::test_bit(own_pawns, sq)) {
                continue; // Already scored via material/PSQT/pawn structure.
            }

            // Reverse-pawn-attack trick (eval/pawns.cpp's own
            // "Connected" check establishes the pattern): `sq` is
            // attacked by an enemy pawn exactly when
            // pawn_attacks(c, sq) intersects the enemy's own pawns.
            const bool attacked_by_enemy = (board::pawn_attacks(c, sq) & enemy_pawns) != 0;
            if (attacked_by_enemy) {
                continue;
            }

            ++safe_squares;
        }

        const Score side_score = kSpaceSquareBonus * safe_squares;

        if (c == Color::White) {
            score += side_score;
        } else {
            score -= side_score;
        }
    }

    return score;
}

} // namespace nightwing::eval
