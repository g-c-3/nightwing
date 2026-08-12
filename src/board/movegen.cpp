// src/board/movegen.cpp
//
// See movegen.h for the algorithm outline and attribution note.

#include "board/movegen.h"

#include <array>

#include "board/attacks.h"
#include "board/masks.h"

namespace nightwing::board {

namespace {

/// Returns the bitboard of every `by_color` piece attacking `sq`, given
/// occupancy `occ`. The pawn term uses the standard "color-flip" trick:
/// the set of squares a `by_color` pawn attacks *from* `sq` is exactly
/// the set of squares from which a `by_color` pawn *would* attack `sq` —
/// so querying pawn_attacks(opposite(by_color), sq) and intersecting
/// with by_color's actual pawns gives exactly the attacking pawns.
[[nodiscard]] Bitboard attackers_to(const Position& pos, Square sq, Color by_color,
                                     Bitboard occ) noexcept {
    Bitboard attackers = kEmptyBitboard;
    attackers |= pawn_attacks(opposite(by_color), sq) & pos.pieces(by_color, PieceType::Pawn);
    attackers |= knight_attacks(sq) & pos.pieces(by_color, PieceType::Knight);
    attackers |= king_attacks(sq) & pos.pieces(by_color, PieceType::King);
    attackers |= bishop_attacks(sq, occ) &
                 (pos.pieces(by_color, PieceType::Bishop) | pos.pieces(by_color, PieceType::Queen));
    attackers |= rook_attacks(sq, occ) &
                 (pos.pieces(by_color, PieceType::Rook) | pos.pieces(by_color, PieceType::Queen));
    return attackers;
}

/// Returns the squares strictly between `a` and `b` if they lie on a
/// common rank, file, or diagonal; otherwise an empty bitboard. Standard
/// bitboard trick: attacking from `a` with the board occupied by nothing
/// but `b`, intersected with attacking from `b` with the board occupied
/// by nothing but `a`, leaves exactly the in-between squares on whichever
/// line (if any) connects them. Precondition: a != b, magics initialized.
[[nodiscard]] Bitboard between(Square a, Square b) noexcept {
    const Bitboard a_bb = square_bb(a);
    const Bitboard b_bb = square_bb(b);
    return (rook_attacks(a, b_bb) & rook_attacks(b, a_bb)) |
           (bishop_attacks(a, b_bb) & bishop_attacks(b, a_bb));
}

/// Finds pieces of `us` that are pinned to their king by an enemy slider,
/// and for each, the bitboard of squares it's still allowed to move to
/// (the king-through-pinner line, inclusive of the pinner's square) —
/// the "sniper" technique described in movegen.h's header comment.
void compute_pins(const Position& pos, Color us, Color them, Square king_sq, Bitboard occ,
                   Bitboard& pinned, std::array<Bitboard, kNumSquares>& pin_allowed) noexcept {
    pinned = kEmptyBitboard;
    const Bitboard enemy_occ = pos.occupancy[static_cast<std::size_t>(them)];
    const Bitboard own_occ = pos.occupancy[static_cast<std::size_t>(us)];

    Bitboard snipers = (bishop_attacks(king_sq, enemy_occ) &
                         (pos.pieces(them, PieceType::Bishop) | pos.pieces(them, PieceType::Queen))) |
                        (rook_attacks(king_sq, enemy_occ) &
                         (pos.pieces(them, PieceType::Rook) | pos.pieces(them, PieceType::Queen)));

    while (snipers) {
        const Square sniper_sq = pop_lsb(snipers);
        const Bitboard between_bb = between(king_sq, sniper_sq);
        const Bitboard blockers = between_bb & occ;
        if (popcount(blockers) == 1 && (blockers & own_occ) == blockers) {
            const Square pinned_sq = bitscan_forward(blockers);
            set_bit(pinned, pinned_sq);
            pin_allowed[static_cast<std::size_t>(pinned_sq)] = between_bb | square_bb(sniper_sq);
        }
    }
}

/// Appends a pawn move to `to`, expanding into all four promotion moves
/// if `to` is on the back rank.
void add_pawn_move(MoveList& moves, Square from, Square to, Bitboard promo_rank,
                    bool capture) noexcept {
    if (test_bit(promo_rank, to)) {
        if (capture) {
            moves.push_back(Move(from, to, MoveFlag::PromoCaptureQueen));
            moves.push_back(Move(from, to, MoveFlag::PromoCaptureRook));
            moves.push_back(Move(from, to, MoveFlag::PromoCaptureBishop));
            moves.push_back(Move(from, to, MoveFlag::PromoCaptureKnight));
        } else {
            moves.push_back(Move(from, to, MoveFlag::PromoQueen));
            moves.push_back(Move(from, to, MoveFlag::PromoRook));
            moves.push_back(Move(from, to, MoveFlag::PromoBishop));
            moves.push_back(Move(from, to, MoveFlag::PromoKnight));
        }
    } else {
        moves.push_back(Move(from, to, capture ? MoveFlag::Capture : MoveFlag::Quiet));
    }
}

void generate_pawn_moves(const Position& pos, Color us, Color them, Bitboard occ, Bitboard enemy,
                          Bitboard target_mask, Bitboard pinned,
                          const std::array<Bitboard, kNumSquares>& pin_allowed, Square king_sq,
                          MoveList& moves) {
    const bool white = (us == Color::White);
    const int push = white ? 8 : -8;
    const Bitboard promo_rank = rank_mask(white ? 7 : 0);
    const Bitboard start_rank = rank_mask(white ? 1 : 6);
    const Bitboard empty = ~occ;

    Bitboard pawns = pos.pieces(us, PieceType::Pawn);
    while (pawns) {
        const Square from = pop_lsb(pawns);
        const bool is_pinned = test_bit(pinned, from);
        const Bitboard allowed = is_pinned ? pin_allowed[static_cast<std::size_t>(from)] : kFullBitboard;

        // Single and double pushes. `from + push` and `+push` again both
        // stay in [0,64) for any real pawn (never on rank 1/8), so no
        // bounds check is needed beyond the empty-square test itself.
        const Square one = static_cast<Square>(from + push);
        if (test_bit(empty, one)) {
            if (test_bit(target_mask, one) && test_bit(allowed, one)) {
                add_pawn_move(moves, from, one, promo_rank, /*capture=*/false);
            }
            if (test_bit(start_rank, from)) {
                const Square two = static_cast<Square>(one + push);
                if (test_bit(empty, two) && test_bit(target_mask, two) && test_bit(allowed, two)) {
                    moves.push_back(Move(from, two, MoveFlag::DoublePawnPush));
                }
            }
        }

        // Captures.
        Bitboard cap_targets = pawn_attacks(us, from) & enemy;
        while (cap_targets) {
            const Square to = pop_lsb(cap_targets);
            if (!test_bit(target_mask, to)) continue;
            if (is_pinned && !test_bit(allowed, to)) continue;
            add_pawn_move(moves, from, to, promo_rank, /*capture=*/true);
        }

        // En passant — legality is resolved by direct occupancy
        // simulation rather than the pin/target masks above; see
        // movegen.h's header comment for why (the horizontal-pin edge
        // case that neither mask captures on its own).
        if (pos.en_passant_square != kNoEnPassantSquare) {
            const Square ep_sq = pos.en_passant_square;
            if (test_bit(pawn_attacks(us, from), ep_sq)) {
                const Square captured_sq = static_cast<Square>(ep_sq - push);
                Bitboard occ_after = occ;
                clear_bit(occ_after, from);
                clear_bit(occ_after, captured_sq);
                set_bit(occ_after, ep_sq);
                if (!is_square_attacked(pos, king_sq, them, occ_after)) {
                    moves.push_back(Move(from, ep_sq, MoveFlag::EnPassant));
                }
            }
        }
    }
}

/// Generates moves for a non-pawn, non-king piece type using `attack_fn`
/// (a callable taking (Square, Bitboard occ) and returning its attack
/// set — knight/king ignore the occupancy argument, sliders use it).
template <typename AttackFn>
void generate_piece_moves(const Position& pos, Color us, PieceType pt, Bitboard occ, Bitboard own,
                           Bitboard target_mask, Bitboard pinned,
                           const std::array<Bitboard, kNumSquares>& pin_allowed, MoveList& moves,
                           AttackFn attack_fn) {
    Bitboard bb = pos.pieces(us, pt);
    while (bb) {
        const Square from = pop_lsb(bb);
        Bitboard attacks = attack_fn(from, occ) & ~own & target_mask;
        if (test_bit(pinned, from)) {
            attacks &= pin_allowed[static_cast<std::size_t>(from)];
        }
        while (attacks) {
            const Square to = pop_lsb(attacks);
            moves.push_back(Move(from, to, pos.is_empty(to) ? MoveFlag::Quiet : MoveFlag::Capture));
        }
    }
}

void generate_king_moves(const Position& pos, Color them, Square king_sq, Bitboard occ,
                          Bitboard own, MoveList& moves) {
    // The king itself must not count as a blocker for its own destination
    // squares' attacked-check, or it would incorrectly appear safe to
    // step straight backward along a line it's currently being checked
    // on (the piece "checking" it would still be attacking through where
    // the king used to stand).
    const Bitboard occ_without_king = occ & ~square_bb(king_sq);

    Bitboard attacks = king_attacks(king_sq) & ~own;
    while (attacks) {
        const Square to = pop_lsb(attacks);
        if (is_square_attacked(pos, to, them, occ_without_king)) continue;
        moves.push_back(Move(king_sq, to, pos.is_empty(to) ? MoveFlag::Quiet : MoveFlag::Capture));
    }
}

void generate_castling_moves(const Position& pos, Color us, Color them, Square king_sq,
                              Bitboard occ, MoveList& moves) {
    // Not legal to castle out of check.
    if (is_square_attacked(pos, king_sq, them, occ)) return;

    const bool white = (us == Color::White);
    const std::uint8_t king_flag = white ? castling::kWhiteKingside : castling::kBlackKingside;
    const std::uint8_t queen_flag = white ? castling::kWhiteQueenside : castling::kBlackQueenside;
    const Bitboard empty = ~occ;

    if (pos.castling_rights & king_flag) {
        const Square f_sq = static_cast<Square>(king_sq + 1);
        const Square g_sq = static_cast<Square>(king_sq + 2);
        if (test_bit(empty, f_sq) && test_bit(empty, g_sq) &&
            !is_square_attacked(pos, f_sq, them, occ) && !is_square_attacked(pos, g_sq, them, occ)) {
            moves.push_back(Move(king_sq, g_sq, MoveFlag::KingCastle));
        }
    }
    if (pos.castling_rights & queen_flag) {
        const Square d_sq = static_cast<Square>(king_sq - 1);
        const Square c_sq = static_cast<Square>(king_sq - 2);
        const Square b_sq = static_cast<Square>(king_sq - 3);
        if (test_bit(empty, d_sq) && test_bit(empty, c_sq) && test_bit(empty, b_sq) &&
            !is_square_attacked(pos, d_sq, them, occ) && !is_square_attacked(pos, c_sq, them, occ)) {
            moves.push_back(Move(king_sq, c_sq, MoveFlag::QueenCastle));
        }
    }
}

} // namespace

bool is_square_attacked(const Position& pos, Square sq, Color by_color, Bitboard occ) noexcept {
    return attackers_to(pos, sq, by_color, occ) != kEmptyBitboard;
}

void generate_legal_moves(const Position& pos, MoveList& moves) {
    moves.clear();

    const Color us = pos.side_to_move;
    const Color them = opposite(us);
    const Bitboard occ = pos.occupied();
    const Bitboard own = pos.occupancy[static_cast<std::size_t>(us)];
    const Bitboard enemy = pos.occupancy[static_cast<std::size_t>(them)];
    const Square king_sq = bitscan_forward(pos.pieces(us, PieceType::King));

    const Bitboard checkers = attackers_to(pos, king_sq, them, occ);
    const int num_checkers = popcount(checkers);

    Bitboard target_mask;
    if (num_checkers == 0) {
        target_mask = kFullBitboard;
    } else if (num_checkers == 1) {
        const Square checker_sq = bitscan_forward(checkers);
        target_mask = square_bb(checker_sq) | between(king_sq, checker_sq);
    } else {
        target_mask = kEmptyBitboard; // double check: only king moves are legal
    }

    Bitboard pinned = kEmptyBitboard;
    std::array<Bitboard, kNumSquares> pin_allowed{};
    compute_pins(pos, us, them, king_sq, occ, pinned, pin_allowed);

    generate_king_moves(pos, them, king_sq, occ, own, moves);

    if (num_checkers < 2) {
        if (num_checkers == 0) {
            generate_castling_moves(pos, us, them, king_sq, occ, moves);
        }
        generate_pawn_moves(pos, us, them, occ, enemy, target_mask, pinned, pin_allowed, king_sq, moves);
        generate_piece_moves(pos, us, PieceType::Knight, occ, own, target_mask, pinned, pin_allowed,
                              moves, [](Square sq, Bitboard) { return knight_attacks(sq); });
        generate_piece_moves(pos, us, PieceType::Bishop, occ, own, target_mask, pinned, pin_allowed,
                              moves, [](Square sq, Bitboard o) { return bishop_attacks(sq, o); });
        generate_piece_moves(pos, us, PieceType::Rook, occ, own, target_mask, pinned, pin_allowed,
                              moves, [](Square sq, Bitboard o) { return rook_attacks(sq, o); });
        generate_piece_moves(pos, us, PieceType::Queen, occ, own, target_mask, pinned, pin_allowed,
                              moves, [](Square sq, Bitboard o) { return queen_attacks(sq, o); });
    }
}

} // namespace nightwing::board
