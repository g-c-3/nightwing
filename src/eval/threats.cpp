// src/eval/threats.cpp
//
// See threats.h.

#include "eval/threats.h"

#include <array>
#include <cstddef>

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

/// Returns the union of every square a `c`-colored piece (of any type,
/// pawns and king included) currently attacks -- the same "attacks by
/// side" building block used to determine both which enemy pieces are
/// attacked and which own pieces are defended, computed once per side
/// per threats_value() call rather than recomputed per piece.
[[nodiscard]] Bitboard attacks_by_side(const Position& pos, Color c) noexcept {
    const Bitboard occupied = pos.occupied();
    Bitboard attacks = board::kEmptyBitboard;

    Bitboard pawns = pos.pieces(c, PieceType::Pawn);
    while (pawns != 0) {
        const Square sq = board::pop_lsb(pawns);
        attacks |= board::pawn_attacks(c, sq);
    }

    Bitboard knights = pos.pieces(c, PieceType::Knight);
    while (knights != 0) {
        const Square sq = board::pop_lsb(knights);
        attacks |= board::knight_attacks(sq);
    }

    Bitboard bishops = pos.pieces(c, PieceType::Bishop);
    while (bishops != 0) {
        const Square sq = board::pop_lsb(bishops);
        attacks |= board::bishop_attacks(sq, occupied);
    }

    Bitboard rooks = pos.pieces(c, PieceType::Rook);
    while (rooks != 0) {
        const Square sq = board::pop_lsb(rooks);
        attacks |= board::rook_attacks(sq, occupied);
    }

    Bitboard queens = pos.pieces(c, PieceType::Queen);
    while (queens != 0) {
        const Square sq = board::pop_lsb(queens);
        attacks |= board::queen_attacks(sq, occupied);
    }

    // Exactly one king per side in any real Position -- read
    // non-destructively via bitscan_forward(), same pattern
    // eval/king_safety.cpp's own king-square extraction already uses,
    // rather than pop_lsb() (which would require a mutable copy for a
    // single, already-known-present bit).
    const Bitboard king_bb = pos.pieces(c, PieceType::King);
    attacks |= board::king_attacks(board::bitscan_forward(king_bb));

    return attacks;
}

/// Returns the attacked-by-an-enemy-pawn penalty for a piece of `pt`
/// (only ever called with Knight/Bishop/Rook/Queen -- see threats.cpp's
/// own call site). `weights`, if non-null, is used instead of the
/// compiled-in kXxxAttackedByPawnPenalty constants -- see
/// threats_value()'s own doc comment (threats.h) on its `weights`
/// parameter for the full nullable-override rationale.
[[nodiscard]] Score pawn_threat_penalty(PieceType pt, const ThreatsWeights* weights) noexcept {
    switch (pt) {
        case PieceType::Knight:
            return weights == nullptr ? kKnightAttackedByPawnPenalty
                                       : Score{round_to_int(weights->knight_pawn_mg),
                                               round_to_int(weights->knight_pawn_eg)};
        case PieceType::Bishop:
            return weights == nullptr ? kBishopAttackedByPawnPenalty
                                       : Score{round_to_int(weights->bishop_pawn_mg),
                                               round_to_int(weights->bishop_pawn_eg)};
        case PieceType::Rook:
            return weights == nullptr ? kRookAttackedByPawnPenalty
                                       : Score{round_to_int(weights->rook_pawn_mg),
                                               round_to_int(weights->rook_pawn_eg)};
        case PieceType::Queen:
            return weights == nullptr ? kQueenAttackedByPawnPenalty
                                       : Score{round_to_int(weights->queen_pawn_mg),
                                               round_to_int(weights->queen_pawn_eg)};
        case PieceType::Pawn:
        case PieceType::King:
        case PieceType::None:
        default:
            return {0, 0};
    }
}

/// Returns the hanging-piece penalty for a piece of `pt` (only ever
/// called with Knight/Bishop/Rook/Queen -- see threats.cpp's own call
/// site). `weights`, if non-null, is used instead of the compiled-in
/// kXxxHangingPenalty constants -- same nullable-override rationale as
/// pawn_threat_penalty() above.
[[nodiscard]] Score hanging_penalty(PieceType pt, const ThreatsWeights* weights) noexcept {
    switch (pt) {
        case PieceType::Knight:
            return weights == nullptr
                       ? kKnightHangingPenalty
                       : Score{round_to_int(weights->knight_hanging_mg),
                               round_to_int(weights->knight_hanging_eg)};
        case PieceType::Bishop:
            return weights == nullptr
                       ? kBishopHangingPenalty
                       : Score{round_to_int(weights->bishop_hanging_mg),
                               round_to_int(weights->bishop_hanging_eg)};
        case PieceType::Rook:
            return weights == nullptr
                       ? kRookHangingPenalty
                       : Score{round_to_int(weights->rook_hanging_mg),
                               round_to_int(weights->rook_hanging_eg)};
        case PieceType::Queen:
            return weights == nullptr
                       ? kQueenHangingPenalty
                       : Score{round_to_int(weights->queen_hanging_mg),
                               round_to_int(weights->queen_hanging_eg)};
        case PieceType::Pawn:
        case PieceType::King:
        case PieceType::None:
        default:
            return {0, 0};
    }
}

/// Returns the overloaded-piece penalty for a piece of `pt` (only ever
/// called with Knight/Bishop/Rook/Queen -- see threats.cpp's own call
/// site). `weights`, if non-null, is used instead of the compiled-in
/// kXxxOverloadedPenalty constants -- same nullable-override rationale
/// as pawn_threat_penalty()/hanging_penalty() above.
[[nodiscard]] Score overloaded_penalty(PieceType pt, const ThreatsWeights* weights) noexcept {
    switch (pt) {
        case PieceType::Knight:
            return weights == nullptr
                       ? kKnightOverloadedPenalty
                       : Score{round_to_int(weights->knight_overloaded_mg),
                               round_to_int(weights->knight_overloaded_eg)};
        case PieceType::Bishop:
            return weights == nullptr
                       ? kBishopOverloadedPenalty
                       : Score{round_to_int(weights->bishop_overloaded_mg),
                               round_to_int(weights->bishop_overloaded_eg)};
        case PieceType::Rook:
            return weights == nullptr
                       ? kRookOverloadedPenalty
                       : Score{round_to_int(weights->rook_overloaded_mg),
                               round_to_int(weights->rook_overloaded_eg)};
        case PieceType::Queen:
            return weights == nullptr
                       ? kQueenOverloadedPenalty
                       : Score{round_to_int(weights->queen_overloaded_mg),
                               round_to_int(weights->queen_overloaded_eg)};
        case PieceType::Pawn:
        case PieceType::King:
        case PieceType::None:
        default:
            return {0, 0};
    }
}

/// Generous fixed capacity for the per-side scratch array
/// overloaded_piece_value() below builds (one entry per own knight/
/// bishop/rook/queen currently on the board) -- a fixed-size stack
/// array rather than a heap-allocated one, matching this codebase's
/// existing "no heap allocation in per-node eval code" convention
/// (ARCHITECTURE.md, and e.g. search/ordering.h's own kMaxPly-sized
/// arrays). 16 comfortably covers every realistic game (at most 2
/// knights + 2 bishops + 2 rooks + 1 queen = 7 per side without any
/// promotion at all) with generous headroom left over for promoted
/// pieces from a hand-built test FEN; any piece beyond this count is
/// silently excluded from the overloaded-piece check specifically
/// (defensive, not a crash) rather than attempting an unbounded-size
/// allocation in eval code -- a scenario this constant's own margin
/// makes exceeding it, even deliberately, difficult to construct.
constexpr int kMaxOverloadScopedPieces = 16;

/// One own knight/bishop/rook/queen's square, type, and own individual
/// attack bitboard -- the per-piece detail overloaded_piece_value()'s
/// "sole defender" test needs that attacks_by_side()'s own UNION
/// bitboard (used by the pawn-attack/hanging checks above) doesn't
/// preserve: a union can tell you a square is defended by SOMETHING,
/// but not by exactly how many, or which, own pieces.
struct ScopedPiece {
    Square sq;
    PieceType pt;
    Bitboard attacks;
};

/// Collects every `c`-colored knight/bishop/rook/queen currently on the
/// board into `out`, each with its own individually-computed attack
/// bitboard, and returns how many were written (capped at
/// kMaxOverloadScopedPieces -- see that constant's own comment).
[[nodiscard]] int collect_scoped_pieces(
    const Position& pos, Color c,
    std::array<ScopedPiece, kMaxOverloadScopedPieces>& out) noexcept {
    const Bitboard occupied = pos.occupied();
    int count = 0;
    for (const PieceType pt :
         {PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen}) {
        Bitboard pieces = pos.pieces(c, pt);
        while (pieces != 0 && count < kMaxOverloadScopedPieces) {
            const Square sq = board::pop_lsb(pieces);
            Bitboard attacks = board::kEmptyBitboard;
            switch (pt) {
                case PieceType::Knight:
                    attacks = board::knight_attacks(sq);
                    break;
                case PieceType::Bishop:
                    attacks = board::bishop_attacks(sq, occupied);
                    break;
                case PieceType::Rook:
                    attacks = board::rook_attacks(sq, occupied);
                    break;
                case PieceType::Queen:
                    attacks = board::queen_attacks(sq, occupied);
                    break;
                default:
                    break; // unreachable: this loop only ever iterates N/B/R/Q
            }
            out[static_cast<std::size_t>(count)] = ScopedPiece{sq, pt, attacks};
            ++count;
        }
    }
    return count;
}

/// Adds this side's own overloaded-piece penalty (this file's header
/// comment's "Overloaded" entry; threats_value()'s own doc comment has
/// the exact test) into `side_score`. Two passes over the same `count`-
/// sized scratch array, O(count^2) total rather than the O(count^3) a
/// naive "for each candidate defender, for each target, recount every
/// possible defender" approach would cost: the first pass computes,
/// for every piece that's currently enemy-attacked, whether it has
/// EXACTLY one own defender and, if so, which -- `sole_defender_of[i]`
/// is that defender's own index into the same array, or -1 if piece i
/// isn't attacked at all or has 0 or 2+ defenders (either way, not
/// relevant to which single piece is carrying sole responsibility for
/// it). The second pass simply tallies, per candidate defender index,
/// how many targets named it as their one-and-only defender -- 2 or
/// more means that piece is overloaded.
void add_overloaded_penalty(const Position& pos, Color c, Bitboard enemy_attacks,
                             const ThreatsWeights* weights, Score& side_score) noexcept {
    std::array<ScopedPiece, kMaxOverloadScopedPieces> pieces{};
    const int count = collect_scoped_pieces(pos, c, pieces);

    std::array<int, kMaxOverloadScopedPieces> sole_defender_of{};
    for (int i = 0; i < count; ++i) {
        sole_defender_of[static_cast<std::size_t>(i)] = -1;
        if (!board::test_bit(enemy_attacks, pieces[static_cast<std::size_t>(i)].sq)) {
            continue;
        }
        int defender_count = 0;
        int last_defender = -1;
        for (int k = 0; k < count; ++k) {
            if (board::test_bit(pieces[static_cast<std::size_t>(k)].attacks,
                                 pieces[static_cast<std::size_t>(i)].sq)) {
                ++defender_count;
                last_defender = k;
            }
        }
        if (defender_count == 1) {
            sole_defender_of[static_cast<std::size_t>(i)] = last_defender;
        }
    }

    std::array<int, kMaxOverloadScopedPieces> overload_count{};
    for (int i = 0; i < count; ++i) {
        const int defender = sole_defender_of[static_cast<std::size_t>(i)];
        if (defender != -1) {
            ++overload_count[static_cast<std::size_t>(defender)];
        }
    }

    for (int k = 0; k < count; ++k) {
        if (overload_count[static_cast<std::size_t>(k)] >= 2) {
            side_score += overloaded_penalty(pieces[static_cast<std::size_t>(k)].pt, weights);
        }
    }
}

} // namespace

Score threats_value(const Position& pos, const ThreatsWeights* weights) noexcept {
    Score score;

    // Computed once per side, reused for every piece below rather than
    // recomputed per piece.
    const Bitboard white_attacks = attacks_by_side(pos, Color::White);
    const Bitboard black_attacks = attacks_by_side(pos, Color::Black);

    for (const Color c : {Color::White, Color::Black}) {
        const Color enemy = board::opposite(c);
        const Bitboard enemy_pawns = pos.pieces(enemy, PieceType::Pawn);
        const Bitboard& enemy_attacks = (c == Color::White) ? black_attacks : white_attacks;
        const Bitboard& own_attacks = (c == Color::White) ? white_attacks : black_attacks;

        Score side_score;

        for (const PieceType pt :
             {PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen}) {
            Bitboard pieces = pos.pieces(c, pt);
            while (pieces != 0) {
                const Square sq = board::pop_lsb(pieces);

                // Reverse-pawn-attack trick (eval/pawns.cpp's own
                // "Connected" check establishes the pattern, reused
                // identically in eval/space.cpp): `sq` is attacked by
                // an enemy pawn exactly when pawn_attacks(c, sq)
                // intersects the enemy's own pawns.
                const bool attacked_by_pawn = (board::pawn_attacks(c, sq) & enemy_pawns) != 0;
                if (attacked_by_pawn) {
                    side_score += pawn_threat_penalty(pt, weights);
                }

                const bool attacked = board::test_bit(enemy_attacks, sq);
                const bool defended = board::test_bit(own_attacks, sq);
                if (attacked && !defended) {
                    side_score += hanging_penalty(pt, weights);
                }
            }
        }

        add_overloaded_penalty(pos, c, enemy_attacks, weights, side_score);

        if (c == Color::White) {
            score += side_score;
        } else {
            score -= side_score;
        }
    }

    return score;
}

} // namespace nightwing::eval
