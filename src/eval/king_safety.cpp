// src/eval/king_safety.cpp
//
// See king_safety.h.

#include "eval/king_safety.h"

#include "board/attacks.h"
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
/// rank, 7 = the opposite back rank (where `c` promotes). Used only to
/// index kPawnStormPenalty (king_safety.h) -- a real pawn's relative
/// rank is always in [1, 6] in any legal position (pawns never rest on
/// rank 1 or 8). File-private and duplicated here rather than shared
/// via a board/ utility -- this project's own established convention
/// for this exact small computation (eval/pawns.cpp and several of the
/// endgame eval files each have their own identical copy; no shared
/// utility exists, and none is claimed here either).
[[nodiscard]] constexpr int relative_rank(Color c, Square sq) noexcept {
    const int rank = board::rank_of(sq);
    return c == Color::White ? rank : 7 - rank;
}

/// Per-attacking-piece-type weight for the attacker-weighting component
/// (king_safety.h's own header comment) — first-draft hand estimates,
/// loosely following each piece type's own relative material value
/// (eval/psqt.h/eval.cpp), not copied from any specific published
/// engine's own attack-unit table. A queen bearing down on the king
/// zone counts for far more than a knight doing the same, since a queen
/// alone can often generate mating threats a single knight cannot.
constexpr int kKnightAttackUnits = 1;
constexpr int kBishopAttackUnits = 1;
constexpr int kRookAttackUnits = 2;
constexpr int kQueenAttackUnits = 4;

/// Returns the pawn-shield zone for a king of color `c` at
/// (`king_file`, `king_rank`): every square on the king's own file and
/// its two neighbors (clipped at the board edge), on the two ranks
/// immediately in front of the king from `c`'s own perspective (toward
/// the enemy side — increasing rank for White, decreasing for Black).
/// Deliberately computed here with plain rank/file arithmetic rather
/// than added as a new board/masks.h function: unlike
/// passed_pawn_mask()/backward_support_mask() (genuinely reused by
/// multiple callers across ply/rank combinations during pawn structure
/// evaluation), this zone is only ever needed here, for exactly one
/// square per side per call — not worth promoting to a shared,
/// precomputed 64-entry table the way those two are.
[[nodiscard]] Bitboard shield_zone(Color c, int king_file, int king_rank) noexcept {
    Bitboard zone = board::kEmptyBitboard;
    const int direction = (c == Color::White) ? 1 : -1;

    for (int df = -1; df <= 1; ++df) {
        const int f = king_file + df;
        if (f < 0 || f >= board::kNumFiles) {
            continue;
        }
        for (int dr = 1; dr <= 2; ++dr) {
            const int r = king_rank + direction * dr;
            if (r < 0 || r >= board::kNumRanks) {
                continue;
            }
            board::set_bit(zone, board::make_square(f, r));
        }
    }

    return zone;
}

/// Returns the total weighted attack-unit count (see the per-piece-type
/// weights above) contributed by every one of `attacker`'s knights/
/// bishops/rooks/queens that attacks at least one square of `zone`.
/// Pawns and the king itself don't participate — same exclusion
/// rationale as eval/mobility.h's mobility_value() (this file's own
/// header comment doesn't repeat it; see mobility.h's).
[[nodiscard]] int attack_units_on(const Position& pos, Color attacker, Bitboard zone) noexcept {
    const Bitboard occupied = pos.occupied();
    int units = 0;

    Bitboard knights = pos.pieces(attacker, PieceType::Knight);
    while (knights != 0) {
        const Square sq = board::pop_lsb(knights);
        if ((board::knight_attacks(sq) & zone) != 0) {
            units += kKnightAttackUnits;
        }
    }

    Bitboard bishops = pos.pieces(attacker, PieceType::Bishop);
    while (bishops != 0) {
        const Square sq = board::pop_lsb(bishops);
        if ((board::bishop_attacks(sq, occupied) & zone) != 0) {
            units += kBishopAttackUnits;
        }
    }

    Bitboard rooks = pos.pieces(attacker, PieceType::Rook);
    while (rooks != 0) {
        const Square sq = board::pop_lsb(rooks);
        if ((board::rook_attacks(sq, occupied) & zone) != 0) {
            units += kRookAttackUnits;
        }
    }

    Bitboard queens = pos.pieces(attacker, PieceType::Queen);
    while (queens != 0) {
        const Square sq = board::pop_lsb(queens);
        if ((board::queen_attacks(sq, occupied) & zone) != 0) {
            units += kQueenAttackUnits;
        }
    }

    return units;
}

} // namespace

Score king_safety_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        const Bitboard king_bb = pos.pieces(c, PieceType::King);
        if (king_bb == 0) {
            // Defensive only -- every real, legally-reached position has
            // exactly one king per side (board/fen.h's own parser and
            // board/movegen.h's legality checking both already guarantee
            // this), but a hand-built test position (eval/mobility.h's
            // own tests build several) could omit one, and
            // bitscan_forward() on an empty bitboard is undefined
            // behavior this function must never risk.
            continue;
        }

        const Square king_sq = board::bitscan_forward(king_bb);
        const int king_file = board::file_of(king_sq);
        const int king_rank = board::rank_of(king_sq);
        const Color enemy = board::opposite(c);

        Score side_score;

        // Pawn shield.
        const Bitboard shield = shield_zone(c, king_file, king_rank);
        const int shield_pawns = board::popcount(pos.pieces(c, PieceType::Pawn) & shield);
        side_score += kShieldPawnBonus * shield_pawns;

        // Open/semi-open files among the king's own file and its two
        // neighbors.
        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(enemy, PieceType::Pawn);
        for (int df = -1; df <= 1; ++df) {
            const int f = king_file + df;
            if (f < 0 || f >= board::kNumFiles) {
                continue;
            }
            const Bitboard file_bb = board::file_mask(f);
            if ((own_pawns & file_bb) != 0) {
                continue; // Own pawn still on this file -- not open.
            }
            if ((enemy_pawns & file_bb) == 0) {
                side_score += kOpenFileNearKingPenalty;
            } else {
                side_score += kSemiOpenFileNearKingPenalty;
            }
        }

        // Pawn storms (king_safety.h's own header comment has the full
        // rationale): for the SAME 3 files just checked above, find the
        // most-advanced ENEMY pawn on each (if any) and penalize based
        // on how far it's advanced. Deliberately a SEPARATE loop over
        // the same file range, not folded into the open/semi-open loop
        // above -- that loop `continue`s immediately whenever an own
        // pawn is present on the file, but a storming enemy pawn matters
        // regardless of whether the king's own pawn is still there too.
        for (int df = -1; df <= 1; ++df) {
            const int f = king_file + df;
            if (f < 0 || f >= board::kNumFiles) {
                continue;
            }
            Bitboard storm_pawns = enemy_pawns & board::file_mask(f);
            if (storm_pawns == 0) {
                continue;
            }
            // Most advanced from the STORMING side's own perspective --
            // the highest enemy-relative rank among (possibly doubled)
            // enemy pawns on this file. Only the frontmost stormer
            // matters: it's the one that reaches the king's shelter
            // first and forces a resolution (a trade, a further
            // advance, or a block) before whatever pawn stands behind it
            // on the same file ever becomes relevant.
            int most_advanced_rank = 0;
            while (storm_pawns != 0) {
                const Square sq = board::pop_lsb(storm_pawns);
                const int r = relative_rank(enemy, sq);
                if (r > most_advanced_rank) {
                    most_advanced_rank = r;
                }
            }
            side_score += kPawnStormPenalty[static_cast<std::size_t>(most_advanced_rank)];
        }

        // Attacker weighting.
        const Bitboard king_zone = board::king_attacks(king_sq) | king_bb;
        const int units = attack_units_on(pos, enemy, king_zone);
        side_score += kAttackUnitPenalty * units;

        // Back-rank weakness (king_safety.h's own header comment, and
        // king_safety_value()'s own doc comment, have the full
        // rationale/exact test). Only meaningful when the king is on
        // its own back rank at all -- everywhere else, "no escape
        // square in front" isn't the same kind of weakness (there's a
        // whole extra rank behind it to retreat into instead).
        const int back_rank = (c == Color::White) ? 0 : (board::kNumRanks - 1);
        if (king_rank == back_rank) {
            const int direction = (c == Color::White) ? 1 : -1;
            const int front_rank = king_rank + direction;
            bool has_luft = false;
            for (int df = -1; df <= 1; ++df) {
                const int f = king_file + df;
                if (f < 0 || f >= board::kNumFiles) {
                    continue;
                }
                const Square front_sq = board::make_square(f, front_rank);
                if (!board::test_bit(own_pawns, front_sq)) {
                    has_luft = true;
                    break;
                }
            }
            if (!has_luft) {
                const bool major_piece_threat =
                    pos.pieces(enemy, PieceType::Rook) != 0 || pos.pieces(enemy, PieceType::Queen) != 0;
                if (major_piece_threat) {
                    side_score += kBackRankWeaknessPenalty;
                }
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
