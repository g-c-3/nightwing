// src/eval/king_pawn_endgame.cpp
//
// See king_pawn_endgame.h, where the four kXxx Score constants this
// file applies are declared (public, not local to this file --
// matching eval/mobility.h's/eval/king_safety.h's/eval/threats.h's/
// eval/king_tropism.h's/eval/trapped_pieces.h's/eval/tempo.h's own
// established convention, so tests and the eventual Texel tuner can
// both reach them by name). Every one of those four is a first-draft
// hand estimate, not yet Texel-tuned -- same caveat every other eval
// term in this codebase already carries (docs/DECISIONS.md, throughout
// Phase 5). Note this term is unusual among Phase 5/6 additions in one
// respect: a KPK position (by definition -- one pawn, two bare kings,
// nothing else) always has compute_phase(pos) == 0 (eval.h/eval.cpp --
// no non-pawn material exists to contribute to the phase count at
// all), so eval::taper() always selects each constant's `eg` half in
// full and never blends in `mg` at all. Every constant still sets `mg`
// equal to `eg` regardless -- not because `mg` ever matters for a real
// KPK position, but so a hand-built test position that calls taper()
// at some other phase (a plausible thing for a unit test to do, even
// though it can't arise from a genuine KPK position) still gets a
// sensible, non-surprising value rather than an unused `mg` field
// silently defaulting to 0 and looking like a bug.

#include "eval/king_pawn_endgame.h"

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
/// locally rather than shared with eval/pawns.cpp's own identical
/// helper -- matches this codebase's established per-file convention
/// for this exact computation (eval/pawns.cpp's own header comment on
/// relative_rank() makes no claim of being a shared utility, and no
/// other file currently imports it from there).
[[nodiscard]] constexpr int relative_rank(Color c, Square sq) noexcept {
    const int rank = board::rank_of(sq);
    return c == Color::White ? rank : 7 - rank;
}

/// Returns true if `a` and `b` are in DIRECT opposition: same file or
/// same rank, with exactly one square between them (CPW "Opposition" --
/// the simplest, most common form of this concept). Distant opposition
/// (three or five squares between, same file/rank/diagonal) and
/// diagonal opposition are deliberately not checked here -- see this
/// file's own header comment for why.
[[nodiscard]] constexpr bool has_direct_opposition(Square a, Square b) noexcept {
    const int file_diff = board::file_of(a) - board::file_of(b);
    const int rank_diff = board::rank_of(a) - board::rank_of(b);
    if (file_diff == 0) {
        return (rank_diff == 2 || rank_diff == -2);
    }
    if (rank_diff == 0) {
        return (file_diff == 2 || file_diff == -2);
    }
    return false;
}

} // namespace

Score king_pawn_endgame_value(const Position& pos) noexcept {
    if (classify_endgame(pos) != EndgameSignature::KPK) {
        return Score{};
    }

    // classify_endgame() already guarantees exactly one pawn total on
    // the board and nothing else besides the two kings -- find which
    // side it belongs to. Exactly one of these two popcounts is 1, the
    // other 0, by that same guarantee; no third case to handle.
    const Bitboard white_pawns = pos.pieces(Color::White, PieceType::Pawn);
    const Color attacker = (board::popcount(white_pawns) == 1) ? Color::White : Color::Black;
    const Color defender = board::opposite(attacker);
    const Square pawn_sq = board::bitscan_forward(pos.pieces(attacker, PieceType::Pawn));

    const Bitboard attacker_king_bb = pos.pieces(attacker, PieceType::King);
    const Bitboard defender_king_bb = pos.pieces(defender, PieceType::King);
    if (attacker_king_bb == 0 || defender_king_bb == 0) {
        // Defensive only -- every real, legally-reached position has
        // exactly one king per side (same guard eval/king_safety.cpp's
        // own king_safety_value() and eval/king_tropism.cpp's own
        // king_tropism_value() already apply, for the identical reason:
        // a hand-built test position could omit one, and
        // bitscan_forward() on an empty bitboard is undefined behavior
        // this function must never risk).
        return Score{};
    }
    const Square attacker_king_sq = board::bitscan_forward(attacker_king_bb);
    const Square defender_king_sq = board::bitscan_forward(defender_king_bb);

    const int pawn_file = board::file_of(pawn_sq);
    const bool rook_pawn = (pawn_file == 0 || pawn_file == 7);
    const int rel_rank = relative_rank(attacker, pawn_sq);

    // Rule of the Square: how many of the pawn's own pushes remain
    // before it promotes -- a pawn still on its own starting rank gets
    // its first-move double-step option folded in as one fewer move
    // needed (the standard adjustment CPW's own "Square Rule" article
    // describes -- "for a pawn on its starting square, draw the square
    // one rank smaller").
    int pawn_moves_to_promote = (7 - rel_rank) - (rel_rank == 1 ? 1 : 0);
    if (pawn_moves_to_promote < 0) {
        pawn_moves_to_promote = 0; // defensive: rel_rank == 7 can't occur for a real pawn
    }

    const Square promotion_sq =
        board::make_square(pawn_file, (attacker == Color::White) ? 7 : 0);
    const int king_moves_to_promotion_sq =
        board::chebyshev_distance(defender_king_sq, promotion_sq);

    // If it's the attacker's (pawn's) move, the pawn effectively gets a
    // head start over the defending king's own reply -- the standard
    // "if it's the pawn's move, the square shrinks by one rank" rule --
    // implemented here as one fewer defender move available before the
    // pawn would promote, rather than literally shrinking a square
    // (mathematically equivalent for a king moving optimally, and
    // simpler to reason about directly as move counts).
    int defender_moves_available = pawn_moves_to_promote - (pos.side_to_move == attacker ? 1 : 0);
    if (defender_moves_available < 0) {
        defender_moves_available = 0;
    }

    const bool king_catches = king_moves_to_promotion_sq <= defender_moves_available;

    Score adjustment;
    if (!king_catches) {
        adjustment = kUnstoppablePawnBonus;
    } else if (rook_pawn) {
        adjustment = kRookPawnDrawishPenalty;
    } else {
        // Key squares (CPW "Key Square"): three squares on the pawn's
        // own file and both adjacent files (safe to use file-1/file+1
        // unclipped here -- rook pawns, the only case where either
        // would go off-board, are already handled above). Standard
        // theory places them two ranks ahead of the pawn while it
        // hasn't yet crossed the board's midline (relative rank <= 4),
        // and one rank ahead once it has (relative rank >= 5) -- a
        // single formula covering the pawn's whole path, not a
        // per-rank lookup table, matching this item's own "not
        // case-tabulated" wording.
        const int key_rel_rank_offset = (rel_rank <= 4) ? 2 : 1;
        int key_rel_rank = rel_rank + key_rel_rank_offset;
        if (key_rel_rank > 7) {
            key_rel_rank = 7; // defensive clamp; not reachable for a real pawn short of promoting
        }
        const int key_abs_rank = (attacker == Color::White) ? key_rel_rank : (7 - key_rel_rank);

        bool attacker_holds_key_square = false;
        for (int df = -1; df <= 1; ++df) {
            const int f = pawn_file + df;
            if (f < 0 || f > 7) {
                continue;
            }
            if (board::make_square(f, key_abs_rank) == attacker_king_sq) {
                attacker_holds_key_square = true;
                break;
            }
        }

        if (attacker_holds_key_square) {
            adjustment = kKeySquareBonus;
        } else {
            // Opposition (CPW "Opposition"), direct form only: the
            // defending king "holds" the opposition exactly when it's
            // currently the ATTACKER's move (the attacker is the side
            // who must concede ground) and the two kings stand in
            // direct opposition right now. Combined with the defending
            // king actually standing in front of the pawn (same file,
            // between the pawn and its promotion square) -- without
            // both conditions together, opposition alone doesn't
            // establish the classical draw (a defending king with the
            // opposition but NOT in front of the pawn hasn't set up
            // the blockade the technique depends on).
            const bool defender_has_opposition =
                has_direct_opposition(attacker_king_sq, defender_king_sq) &&
                (pos.side_to_move == attacker);
            const bool defender_in_front =
                (board::file_of(defender_king_sq) == pawn_file) &&
                ((attacker == Color::White)
                     ? (board::rank_of(defender_king_sq) > board::rank_of(pawn_sq))
                     : (board::rank_of(defender_king_sq) < board::rank_of(pawn_sq)));

            if (defender_has_opposition && defender_in_front) {
                adjustment = kOppositionDrawishPenalty;
            }
            // Else: king catches, non-rook pawn, no key square held,
            // no established opposition-blockade -- genuinely
            // ambiguous by these three classical techniques alone.
            // Left as Score{} (no adjustment) deliberately, rather than
            // guessing a direction: a KPK subtree is at most a handful
            // of plies deep for real search to resolve on its own
            // (only two kings and one pawn can ever move), so an
            // unresolved case here safely falls through to whatever
            // plain material/PSQT terms already say, with search doing
            // the rest -- not a silent gap, just this term declining to
            // overreach past what these three specific techniques
            // actually decide.
        }
    }

    return (attacker == Color::White) ? adjustment : -adjustment;
}

} // namespace nightwing::eval
