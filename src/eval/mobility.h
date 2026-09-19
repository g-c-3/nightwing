#pragma once
// src/eval/mobility.h
//
// Mobility evaluation (ROADMAP.md Phase 5's "Mobility eval" item):
// rewards a piece for having more squares available to move to, the
// standard CPW "Mobility" concept
// (https://www.chessprogramming.org/Mobility) -- from-scratch
// implementation here, no code copied, per ARCHITECTURE.md's
// Attribution Policy. Knights, bishops, rooks, and queens are counted;
// pawns are already scored via eval/pawns.h and material_value()
// (eval/psqt.h), and the king is deliberately excluded -- king
// activity/safety is Phase 5's own separate, dedicated item (planned:
// eval/king_safety.h, not yet implemented), not folded into this
// generic mobility term.
//
// The specific per-square centipawn bonuses in mobility.cpp are
// first-draft hand estimates, not yet Texel-tuned (ROADMAP.md Phase 5's
// tuner item lands later and is expected to revise every constant in
// this file, same caveat as eval/pawns.h's own header comment) -- and
// deliberately a flat "per attacked square" bonus rather than a
// diminishing-returns lookup table indexed by mobility count (the more
// elaborate approach some engines use): matches this codebase's
// existing pawn-structure eval's own preference for a handful of simple
// additive constants over a larger tuned table as the right first cut
// before a real tuner exists -- see docs/DECISIONS.md.
//
// Deliberately its own translation unit, separate from eval.cpp, same
// organizational rationale as eval/pawns.h (one clearly-scoped eval term
// per file) even though mobility -- unlike pawn structure -- has no
// caching motivation of its own (piece mobility changes essentially
// every move, so there's no analogous "cache it, most positions don't
// change it" case the way eval/pawn_tt.h makes for pawn structure).

#include "board/board.h"
#include "eval/psqt.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Per-square mobility bonus for each piece type that counts toward this
/// term -- knight, bishop, rook, queen only (see this file's header
/// comment for why pawns/king are excluded). Larger magnitude for
/// pieces whose activity swings more decisively (a bishop or rook with
/// many open lines is a bigger deal than a knight with a few extra
/// hops; a queen's bonus is kept smallest of all despite it usually
/// having the most raw attacked squares, precisely BECAUSE it usually
/// has the most -- an unscaled per-square bonus at knight/bishop
/// magnitude would let queen mobility alone dominate this whole term).
/// `eg` at or above `mg` throughout: piece activity generally matters
/// at least as much, often more, once there are fewer pieces around to
/// block lines and contest outposts (the same general CPW "Mobility"
/// observation other engines' published weights reflect, not a value
/// copied from any of them).
inline constexpr Score kKnightMobilityBonus = {4, 4};
inline constexpr Score kBishopMobilityBonus = {5, 5};
inline constexpr Score kRookMobilityBonus = {2, 4};
inline constexpr Score kQueenMobilityBonus = {1, 2};

/// Runtime-mutable counterpart to the 4 kXxxMobilityBonus constants
/// above -- the mobility-term half of the "runtime-mutable
/// parameter-vector abstraction over eval's currently-constexpr named
/// constants" `eval::MaterialWeights` (psqt.h) and `PsqtWeights`
/// (psqt.h) already play for material and PSQT (ROADMAP.md's Tier 0
/// tuner item, "PSQT and beyond" -- this is the first "beyond" term).
/// Unlike PsqtWeights, this struct CAN be `constexpr`-constructible the
/// same way MaterialWeights already is: the values it mirrors
/// (kKnightMobilityBonus etc., above) are already visible right here in
/// this header, not hidden in mobility.cpp's own anonymous namespace
/// the way psqt.cpp's per-square tables are -- so this follows
/// MaterialWeights' pattern (default-member-initializers naming the
/// header constants directly, hand-kept-in-sync, not PsqtWeights'
/// (out-of-line default_psqt_weights(), all-zero default construction).
/// Every field is `mg`/`eg` for one piece type, no separate `knight_mg`/
/// `knight_eg` split needed beyond that -- only 4 piece types count
/// toward mobility (see this file's own header comment), so 8 fields
/// total, far fewer than PsqtWeights' 12 `std::array<double,64>` fields.
struct MobilityWeights {
    double knight_mg = kKnightMobilityBonus.mg;
    double knight_eg = kKnightMobilityBonus.eg;
    double bishop_mg = kBishopMobilityBonus.mg;
    double bishop_eg = kBishopMobilityBonus.eg;
    double rook_mg = kRookMobilityBonus.mg;
    double rook_eg = kRookMobilityBonus.eg;
    double queen_mg = kQueenMobilityBonus.mg;
    double queen_eg = kQueenMobilityBonus.eg;
};

/// Returns a MobilityWeights matching kKnightMobilityBonus/.../
/// kQueenMobilityBonus exactly -- the natural starting point for a
/// mobility-aware tuning run (tuner::tune(), src/tuner/tune.h), and the
/// values every field above is already separately, redundantly
/// initialized to (kept in sync by hand, not derived from this
/// function, exactly matching default_material_weights()'s own doc
/// comment rationale, psqt.h, for why: MobilityWeights{}'s own
/// default-member-initializers stay self-contained and don't require
/// calling a function just to default-construct one -- this function
/// exists for callers that want that mapping made explicit/named, and
/// for tests confirming the two really do agree).
[[nodiscard]] constexpr MobilityWeights default_mobility_weights() noexcept {
    return MobilityWeights{
        /*knight_mg=*/kKnightMobilityBonus.mg, /*knight_eg=*/kKnightMobilityBonus.eg,
        /*bishop_mg=*/kBishopMobilityBonus.mg, /*bishop_eg=*/kBishopMobilityBonus.eg,
        /*rook_mg=*/kRookMobilityBonus.mg,     /*rook_eg=*/kRookMobilityBonus.eg,
        /*queen_mg=*/kQueenMobilityBonus.mg,   /*queen_eg=*/kQueenMobilityBonus.eg,
    };
}

/// Evaluates mobility for BOTH sides and returns a single White-relative
/// Score (positive favors White, matching
/// material_value()/psqt_value()/pawn_structure_value()'s sign
/// convention in eval.cpp/eval/pawns.h). For each knight/bishop/rook/
/// queen on the board, counts the squares it attacks that aren't
/// occupied by a piece of its OWN color (CPW's basic "pseudo-mobility"
/// definition -- a square occupied by an ENEMY piece still counts, even
/// though moving there would be a capture rather than free movement;
/// deliberately not narrowed to only empty squares, nor to
/// Stockfish-style "safe mobility" that also excludes squares attacked
/// by an enemy pawn -- a reasonable first-cut simplification, revisit
/// once the Texel tuner (ROADMAP.md Phase 5) can show whether a more
/// refined definition is worth its added complexity), multiplied by
/// that piece type's own per-square bonus above.
///
/// `weights`, if non-null, is used INSTEAD OF kKnightMobilityBonus/.../
/// kQueenMobilityBonus for this call only -- the same nullable-override
/// convention material_value()/psqt_value() (psqt.h) already establish,
/// threaded here for eval::evaluate()'s own `mobility_weights` parameter
/// (eval.h) so a future mobility-aware tuning run can probe candidate
/// mobility weight vectors the same uniform way it already can for
/// material and PSQT. Values are `double` (MobilityWeights' own field
/// type, matching MaterialWeights/PsqtWeights) but Score's own fields
/// are `int` (score.h): rounded via the same round_to_int() helper
/// psqt_value() already uses (psqt.h), reused here rather than
/// duplicated.
///
/// Precondition: board::init_masks() and board::init_magic_bitboards()
/// have both been called (this function uses board::knight_attacks()/
/// board::bishop_attacks()/board::rook_attacks()/board::queen_attacks(),
/// transitively requiring both).
[[nodiscard]] Score mobility_value(const board::Position& pos,
                                    const MobilityWeights* weights = nullptr) noexcept;

} // namespace nightwing::eval
