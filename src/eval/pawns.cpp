// src/eval/pawns.cpp
//
// See pawns.h.

#include "eval/pawns.h"

#include <array>
#include <cstddef>

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
/// index kPassedPawnBonus (pawns.h) -- a real pawn's relative rank is
/// always in [1, 6] in any legal position (pawns never rest on rank 1
/// or 8).
[[nodiscard]] constexpr int relative_rank(Color c, Square sq) noexcept {
    const int rank = board::rank_of(sq);
    return c == Color::White ? rank : 7 - rank;
}

/// True when `sq` (a `c`-colored pawn) is a candidate passed pawn --
/// see pawns.h's own doc comment on pawn_structure_value() for the full
/// two-part test this implements. Only meaningful (and only ever
/// called) for a pawn that isn't already passed.
[[nodiscard]] bool is_candidate_passed_pawn(Color c, Square sq, Bitboard own_pawns,
                                             Bitboard enemy_pawns) noexcept {
    const int file = board::file_of(sq);
    const int my_rank = relative_rank(c, sq);

    // Part (a): a straight-ahead enemy pawn on this pawn's OWN file can
    // never be removed by a trade (pawns don't capture straight ahead),
    // so it rules out ever becoming passed via trades alone regardless
    // of what the adjacent files look like.
    {
        Bitboard scan = enemy_pawns & board::file_mask(file);
        while (scan != 0) {
            const Square s = board::pop_lsb(scan);
            if (relative_rank(c, s) > my_rank) {
                return false;
            }
        }
    }

    // Part (b): among the (up to two) adjacent files only, compare own
    // pawns at-or-behind this pawn's own rank (potential future
    // supporters/replacements, including this pawn itself) against
    // enemy pawns ahead of it (potential blockers/capturers).
    const Bitboard adjacent = board::adjacent_files_mask(file);

    int own_count = 0;
    {
        Bitboard scan = own_pawns & adjacent;
        while (scan != 0) {
            const Square s = board::pop_lsb(scan);
            if (relative_rank(c, s) <= my_rank) {
                ++own_count;
            }
        }
    }

    int enemy_count = 0;
    {
        Bitboard scan = enemy_pawns & adjacent;
        while (scan != 0) {
            const Square s = board::pop_lsb(scan);
            if (relative_rank(c, s) > my_rank) {
                ++enemy_count;
            }
        }
    }

    return own_count >= enemy_count;
}

/// True when `sq` is an outside passed pawn -- see pawns.h's own doc
/// comment on pawn_structure_value() for the full test. `other_pawns`
/// must be every pawn currently on the board, EITHER color, with `sq`
/// itself already excluded. Only meaningful (and only ever called) for
/// a pawn that IS already passed.
[[nodiscard]] bool is_outside_passed_pawn(Square sq, Bitboard other_pawns) noexcept {
    const int file = board::file_of(sq);

    // board::kNumFiles (8) is used as a sentinel here specifically
    // because it's strictly larger than any real file-distance on an
    // 8-file board (the largest possible is 7, between file 0 and file
    // 7) -- so "no other pawns found" and "found, but too close" can
    // never be confused with each other below.
    int min_file_distance = board::kNumFiles;
    for (int f = 0; f < board::kNumFiles; ++f) {
        if ((other_pawns & board::file_mask(f)) != 0) {
            const int distance = f > file ? f - file : file - f;
            if (distance < min_file_distance) {
                min_file_distance = distance;
            }
        }
    }

    if (min_file_distance == board::kNumFiles) {
        // No other pawn anywhere on the board -- nothing for this one
        // to be meaningfully "outside" of.
        return false;
    }

    return min_file_distance >= kOutsidePassedPawnMinFileGap;
}

} // namespace

Score pawn_structure_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        const Color them = c == Color::White ? Color::Black : Color::White;
        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(them, PieceType::Pawn);

        // Per-file own-pawn counts, computed once up front: the doubled-
        // pawn check below needs "how many of my own pawns share this
        // file" for every pawn on it, and recomputing that via a fresh
        // mask-and-popcount per pawn would redo the same file's count
        // once for every pawn standing on it.
        std::array<int, board::kNumFiles> file_counts{};
        for (int f = 0; f < board::kNumFiles; ++f) {
            file_counts[static_cast<std::size_t>(f)] =
                board::popcount(own_pawns & board::file_mask(f));
        }

        // Every pawn currently on the board, either color -- computed
        // once up front so the outside-passed-pawn check below (which
        // needs "every OTHER pawn on the board" per candidate pawn)
        // doesn't have to re-OR the two bitboards on every iteration of
        // the main loop.
        const Bitboard total_pawns = own_pawns | enemy_pawns;

        // Precomputed once up front, same rationale as file_counts above:
        // the connected-passed-pawns bonus (kConnectedPassedPawnBonus,
        // pawns.h) needs to know, for a given pawn already known to be
        // passed, whether the SPECIFIC friendly pawn defending or
        // standing beside it is ALSO passed -- not just present. Scanning
        // all of `own_pawns` once here to build that bitboard is simpler
        // and no more expensive than re-deriving it per pawn inside the
        // main loop below (each check is itself a single mask-and-
        // compare, and there are at most 8 pawns per side).
        Bitboard passed_pawns_bb = board::kEmptyBitboard;
        {
            Bitboard scan = own_pawns;
            while (scan != 0) {
                const Square sq = board::pop_lsb(scan);
                if ((enemy_pawns & board::passed_pawn_mask(c, sq)) == 0) {
                    board::set_bit(passed_pawns_bb, sq);
                }
            }
        }

        Score side_score;
        Bitboard bb = own_pawns;
        while (bb != 0) {
            const Square sq = board::pop_lsb(bb);
            const int file = board::file_of(sq);

            const bool isolated = (own_pawns & board::adjacent_files_mask(file)) == 0;
            if (isolated) {
                side_score += kIsolatedPawnPenalty;
            }

            if (file_counts[static_cast<std::size_t>(file)] >= 2) {
                side_score += kDoubledPawnPenalty;
            }

            const bool passed = (enemy_pawns & board::passed_pawn_mask(c, sq)) == 0;
            if (passed) {
                side_score += kPassedPawnBonus[static_cast<std::size_t>(relative_rank(c, sq))];

                // Outside passed pawn (kOutsidePassedPawnBonus's own doc
                // comment, pawns.h, has the full rationale) -- an
                // additional bonus on top of the plain passed bonus
                // just above, for a passer sitting meaningfully
                // separated from every other pawn on the board.
                Bitboard other_pawns = total_pawns;
                board::clear_bit(other_pawns, sq);
                if (is_outside_passed_pawn(sq, other_pawns)) {
                    side_score += kOutsidePassedPawnBonus[static_cast<std::size_t>(
                        relative_rank(c, sq))];
                }
            }

            // Connected: defended by, or standing beside (phalanx), a
            // friendly pawn. The general reverse-pawn-attack trick is
            // "square X is attacked by a pawn of color K exactly when
            // pawn_attacks(!K, X) intersects K's pawns" (pawn attack
            // patterns are symmetric under the rank-reflection that
            // swaps "attacks from" and "is defended from"). Defended-by-
            // friendly-pawn is "is `sq` attacked by a pawn of color
            // `c`," so K=c and this needs pawn_attacks(them, sq) --
            // NOT pawn_attacks(c, sq), which instead gives the squares
            // an ENEMY pawn would need to stand on to attack `sq` (that
            // form is what the backward-pawn push-square check below
            // uses, where K=them is exactly what's wanted there).
            // Phalanx (same-rank neighbor) is checked separately since
            // pawn_attacks() only ever covers the two diagonal squares,
            // never the same-rank ones a phalanx partner stands on.
            const Bitboard defenders = board::pawn_attacks(them, sq) & own_pawns;
            const Bitboard phalanx_mask =
                board::adjacent_files_mask(file) & board::rank_mask(board::rank_of(sq));
            const Bitboard phalanx_partners = own_pawns & phalanx_mask;
            if (defenders != 0 || phalanx_partners != 0) {
                side_score += kConnectedPawnBonus;

                // Connected PASSED pawns (kConnectedPassedPawnBonus's own
                // doc comment, pawns.h, has the full rationale): on top
                // of the plain connected bonus just above, a passed pawn
                // whose specific defender/phalanx partner is ALSO passed
                // gets this additional bonus. Deliberately checked
                // against `passed_pawns_bb` (this pawn's own defenders/
                // phalanx partners intersected with the precomputed
                // passed-pawn set), not merely "is this pawn passed AND
                // is it connected to SOMETHING" -- the whole point is the
                // MUTUAL passed-pair relationship, not two unrelated
                // facts about the same pawn.
                if (passed &&
                    ((defenders & passed_pawns_bb) != 0 || (phalanx_partners & passed_pawns_bb) != 0)) {
                    side_score +=
                        kConnectedPassedPawnBonus[static_cast<std::size_t>(relative_rank(c, sq))];
                }
            }

            // Backward: only meaningful for a pawn that isn't already
            // passed -- a passed pawn has no enemy pawns anywhere ahead
            // of it (by definition), so its push square can never be
            // enemy-pawn-attacked in the first place, making the second
            // half of this test vacuous for it anyway; skipping passed
            // pawns here is purely an efficiency short-circuit, not a
            // behavior change.
            if (!passed) {
                const bool has_support = (own_pawns & board::backward_support_mask(c, sq)) != 0;
                if (!has_support) {
                    const int push_rank =
                        c == Color::White ? board::rank_of(sq) + 1 : board::rank_of(sq) - 1;
                    // A pawn one push from promoting has no "push
                    // square" left within the board to check -- and per
                    // board.h, a real position never has a pawn resting
                    // on the promotion rank anyway (it would already
                    // have promoted), so this is defensive only.
                    if (push_rank >= 0 && push_rank < board::kNumRanks) {
                        const Square push_sq = board::make_square(file, push_rank);
                        const bool push_attacked =
                            (board::pawn_attacks(c, push_sq) & enemy_pawns) != 0;
                        if (push_attacked) {
                            side_score += kBackwardPawnPenalty;
                        }
                    }
                }

                // Candidate passed pawn (kCandidatePassedPawnBonus's own
                // doc comment, pawns.h, has the full rationale): a
                // separate, independent check from backward-pawn above
                // -- a pawn can in principle be both (a not-yet-passed
                // pawn with no straight-ahead blocker but genuinely
                // unsupported right now still counts its own future
                // adjacent-file trade prospects).
                if (is_candidate_passed_pawn(c, sq, own_pawns, enemy_pawns)) {
                    side_score += kCandidatePassedPawnBonus[static_cast<std::size_t>(
                        relative_rank(c, sq))];
                }
            }
        }

        // Pawn islands (kPawnIslandPenalty's own doc comment, pawns.h,
        // has the full rationale): the ONLY per-SIDE, not per-pawn,
        // term in this function -- deliberately kept OUTSIDE the
        // per-pawn loop just above, since it depends on the shape of
        // the whole file_counts array at once, not any single pawn's
        // own square. A "no pawns at all" side (file_counts all zero)
        // correctly counts zero islands and so is charged nothing --
        // there's no structure to have a shape at all.
        {
            int islands = 0;
            bool prev_file_occupied = false;
            for (int f = 0; f < board::kNumFiles; ++f) {
                const bool file_occupied = file_counts[static_cast<std::size_t>(f)] > 0;
                if (file_occupied && !prev_file_occupied) {
                    ++islands;
                }
                prev_file_occupied = file_occupied;
            }
            if (islands >= 2) {
                side_score += kPawnIslandPenalty * (islands - 1);
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
