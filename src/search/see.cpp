// src/search/see.cpp

#include "search/see.h"

#include <algorithm>
#include <array>

#include "board/attacks.h"
#include "board/masks.h"
#include "eval/psqt.h"

namespace nightwing::search {
namespace {

using board::Bitboard;
using board::Color;
using board::Piece;
using board::PieceType;
using board::Position;
using board::Square;

/// Piece values for SEE's own internal accounting -- reuses
/// eval::material_value() for Pawn..Queen (the same values move
/// ordering's MVV-LVA already uses, see ordering.cpp, so there's one
/// source of truth for "how much is a piece worth" across both), but
/// overrides King with a large sentinel rather than eval's {0,0}.
/// eval::material_value(King) is correctly 0 for EVALUATION purposes
/// (king "material" isn't real tradeable value -- see psqt.h's own
/// comment on this), but SEE needs the opposite signal: a king must
/// always be treated as by far the least desirable piece to risk in an
/// exchange, and if a hypothetical swap sequence ever reaches the point
/// of "capturing" a king, that must register as a catastrophic loss in
/// the gain array, not a free trade. kSeeKingValue is comfortably above
/// a queen (900) but well below kMateThreshold/kMateScore (search.h) to
/// stay a clearly distinct concept from a mate score.
constexpr int kSeeKingValue = 20'000;

[[nodiscard]] constexpr int see_piece_value(PieceType type) noexcept {
    return type == PieceType::King ? kSeeKingValue : eval::material_value(type).mg;
}

/// Attacker types in cheapest-first order -- SEE always continues an
/// exchange with the LEAST valuable available attacker, per the
/// technique's definition (CPW "Static Exchange Evaluation").
constexpr std::array<PieceType, 6> kAttackerOrder = {
    PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
    PieceType::Rook, PieceType::Queen,  PieceType::King,
};

/// Every square from which a piece of `by_color` currently attacks `sq`,
/// given board occupancy `occ` (NOT necessarily `pos.occupied()` --
/// static_exchange_evaluation() below mutates a local copy as it
/// simulates pieces being removed from the exchange, so sliding attacks
/// need to be recomputed against that evolving occupancy each step to
/// correctly reveal X-ray attackers behind a piece that's just been
/// "used"). Every term is masked by `occ` so a piece already removed
/// from the simulated board never counts as an attacker again.
[[nodiscard]] Bitboard attackers_to(const Position& pos, Square sq, Bitboard occ,
                                     Color by_color) noexcept {
    Bitboard attackers = board::kEmptyBitboard;
    attackers |= board::pawn_attacks(board::opposite(by_color), sq) &
                 pos.pieces(by_color, PieceType::Pawn) & occ;
    attackers |= board::knight_attacks(sq) & pos.pieces(by_color, PieceType::Knight) & occ;
    attackers |= board::king_attacks(sq) & pos.pieces(by_color, PieceType::King) & occ;
    const Bitboard diagonal_attackers = board::bishop_attacks(sq, occ);
    attackers |= diagonal_attackers &
                 (pos.pieces(by_color, PieceType::Bishop) | pos.pieces(by_color, PieceType::Queen)) & occ;
    const Bitboard straight_attackers = board::rook_attacks(sq, occ);
    attackers |= straight_attackers &
                 (pos.pieces(by_color, PieceType::Rook) | pos.pieces(by_color, PieceType::Queen)) & occ;
    return attackers;
}

} // namespace

int static_exchange_evaluation(const Position& pos, board::Move move) noexcept {
    const Square to = move.to();
    const Square from = move.from();

    const PieceType captured_type =
        move.is_en_passant() ? PieceType::Pawn : board::piece_type_of(pos.piece_at(to));

    const Piece moved_piece = pos.piece_at(from);
    PieceType attacker_type = board::piece_type_of(moved_piece);
    Color side = board::opposite(board::color_of(moved_piece)); // whose turn it is next in the simulation

    // Occupancy for the simulation: the real board, minus the piece that
    // just moved onto `to` (it's no longer available to attack `to` again
    // in this sequence -- it's the one sitting there being contested).
    Bitboard occ = pos.occupied();
    board::clear_bit(occ, from);
    if (move.is_en_passant()) {
        // The captured pawn sits one rank behind `to` (on `from`'s rank),
        // not on `to` itself -- the one capture type where the victim's
        // square and the destination square differ.
        board::clear_bit(occ, board::make_square(board::file_of(to), board::rank_of(from)));
    }

    // gain[d] is filled in going forward (each hypothetical capture in
    // the sequence), then resolved backward below -- CPW "SEE - The Swap
    // Algorithm". Sized well above any realistic exchange depth (at most
    // ~30 non-king pieces can ever participate, one each, before the
    // board runs out of material).
    std::array<int, 32> gain{};
    int depth = 0;
    gain[0] = see_piece_value(captured_type);

    for (;;) {
        const Bitboard attackers = attackers_to(pos, to, occ, side);
        if (!attackers) {
            break;
        }

        PieceType next_type = PieceType::None;
        Square next_sq = 0;
        for (PieceType candidate_type : kAttackerOrder) {
            const Bitboard candidates = attackers & pos.pieces(side, candidate_type);
            if (candidates) {
                next_type = candidate_type;
                next_sq = board::bitscan_forward(candidates);
                break;
            }
        }
        if (next_type == PieceType::None || depth + 1 >= static_cast<int>(gain.size())) {
            // The second condition is a defensive cap only -- see gain's
            // size comment above; in practice this is never reached.
            break;
        }

        ++depth;
        gain[static_cast<std::size_t>(depth)] =
            see_piece_value(attacker_type) - gain[static_cast<std::size_t>(depth - 1)];

        board::clear_bit(occ, next_sq);
        attacker_type = next_type;
        side = board::opposite(side);
    }

    // Backward resolution: at each hypothetical step, the side to move
    // chooses the better of "stop capturing here" (net 0 further change)
    // or "continue" (whatever the deeper step worked out to) -- exactly
    // mirroring a 1-ply negamax choice at every step of the sequence, but
    // over the small `gain` array instead of a real search tree.
    while (depth > 0) {
        gain[static_cast<std::size_t>(depth - 1)] =
            -std::max(-gain[static_cast<std::size_t>(depth - 1)], gain[static_cast<std::size_t>(depth)]);
        --depth;
    }
    return gain[0];
}

} // namespace nightwing::search
