#pragma once
// src/eval/threats.h
//
// Threats evaluation (ROADMAP.md Phase 5's "Threats evaluation
// (hanging/attacked pieces, pieces attacked by pawns)" item): two
// related but independent penalties applied to a side's own knights,
// bishops, rooks, and queens (pawns and kings are out of scope -- see
// "Why pawns and the king are excluded" below):
//
//   1. Attacked by an enemy pawn: a piece a pawn currently attacks is a
//      serious problem regardless of whether it's defended -- trading a
//      pawn for a minor or major piece is a big material win for the
//      attacker even after any recapture, so this penalty applies
//      unconditionally whenever the attack exists (CPW's own general
//      "Threats" discussion: https://www.chessprogramming.org/Threats
//      covers this as one of the most common and highest-value threat
//      patterns in practice).
//   2. Hanging: a piece attacked by ANY enemy piece (pawn, minor,
//      major, or king) that has NO own defender at all. A simplified,
//      boolean version of the concept -- see "Why a boolean
//      attacked/defended check instead of SEE" below for why this
//      doesn't attempt a full Static-Exchange-Evaluation-accurate
//      "would the exchange actually favor the attacker" judgment.
//   3. Overloaded (ROADMAP.md Priority Fixes, 2026-09-08, "Overloaded
//      pieces" -- the last of that section's 5 eval-feature gaps): a
//      piece that is the SOLE own defender of two or more own pieces
//      that are each themselves currently attacked by the enemy --
//      CPW's "Overloading" (https://www.chessprogramming.org/Overloading):
//      the defender can only actually recapture on one of those squares
//      if the enemy captures there, so the other one is effectively
//      undefended in practice despite superficially having a defender
//      on record. Built from the SAME per-piece attack bitboards the
//      other two checks already compute -- see overloaded_piece_value()'s
//      own doc comment (this file, below the constants) for the exact
//      "sole defender of 2+ attacked pieces" test.
//
// The three penalties are independent and stack when more than one
// applies to the same piece (an overloaded piece that's ALSO pawn-
// attacked or hanging in its own right is worse than any one condition
// alone, and is scored that way) -- the same "independent signals add"
// philosophy eval/piece_bonuses.h's own open-file + 7th-rank stacking
// and eval/space.h's own occupancy + attack stacking already establish
// for this codebase.
//
// From-scratch implementation here, no code copied, per
// ARCHITECTURE.md's Attribution Policy.
//
// Why pawns and the king are excluded: ROADMAP.md's own item wording
// ("hanging/attacked pieces, pieces attacked by pawns") is read here as
// referring to minor/major pieces specifically, matching how CPW's own
// "Threats" material and most engines' own Threats-style eval terms
// scope this concept -- a hanging PAWN is already substantially covered
// by eval/pawns.cpp's own isolated/backward/doubled-pawn terms and by
// material_value() itself the moment it's actually captured, and a
// "hanging king" isn't a coherent concept in eval at all (an attacked
// king is check, which search handles directly, never an eval-time
// judgment call). The overloaded-piece check (added later, same file)
// keeps this exact same minor/major-only scope on BOTH ends of the
// relationship for consistency: only a knight/bishop/rook/queen is ever
// evaluated as the overloaded DEFENDER, and only a knight/bishop/rook/
// queen is ever counted as one of the DEFENDED pieces whose attacked
// status matters -- a piece solely defending a pawn (or the king, which
// is never actually "defended" in the capturable sense at all) doesn't
// enter into this specific check, for the identical reasons already
// given for excluding pawns/king from the two checks above.
//
// Why a boolean attacked/defended check instead of SEE: search/see.h's
// static_exchange_evaluation() already exists and is more precise (it
// simulates the full capture/recapture sequence rather than a flat
// "any attacker vs. any defender" count), but it operates on a specific
// board::Move (a concrete hypothetical capture), and eval/ has no
// existing dependency on search/ anywhere in this codebase -- adding
// one here to evaluate a piece that isn't actually being captured this
// turn would be a new, and backwards, module dependency (search calls
// eval, not the reverse -- ARCHITECTURE.md's own module layering) for a
// refinement this first cut doesn't need. A simple union-of-attacks
// boolean check needs nothing beyond board/, matching every other
// eval/*.h term added this phase.
//
// Same "few hand-estimated constants" preference every other eval/*.h
// module in this phase already establishes -- every constant below is
// a first-draft hand estimate, not yet Texel-tuned, same caveat every
// other eval term added this phase already carries.

#include "board/board.h"
#include "eval/psqt.h" // round_to_int()
#include "eval/score.h"

namespace nightwing::eval {

/// Penalty for a knight/bishop/rook/queen currently attacked by an
/// enemy pawn, applied unconditionally (regardless of whether the
/// piece is otherwise defended -- see this file's header comment for
/// why). Rook/Queen penalties are larger than Knight/Bishop's, matching
/// the intuitive "the more valuable the attacked piece, the more
/// embarrassing and costly it is to have a mere pawn attacking it"
/// scaling. `mg` above `eg` throughout: a pawn threat forces an
/// immediate response (move the piece, defend it, or lose it) most
/// consequentially while there's more actively-contested play still
/// ahead for that tempo loss to cost something real; with fewer pieces
/// left on the board in the endgame, the same tempo hit has less to
/// disrupt.
inline constexpr Score kKnightAttackedByPawnPenalty = {-44, -30};
inline constexpr Score kBishopAttackedByPawnPenalty = {-44, -30};
inline constexpr Score kRookAttackedByPawnPenalty = {-50, -35};
inline constexpr Score kQueenAttackedByPawnPenalty = {-58, -40};

/// Penalty for a knight/bishop/rook/queen attacked by ANY enemy piece
/// (pawn, minor, major, or king) with no own defender anywhere (see
/// this file's header comment's "Why a boolean attacked/defended check
/// instead of SEE" for the exact, deliberately simplified qualification
/// test). Same value-scaling logic as the pawn-attack penalties above
/// (Rook/Queen larger than Knight/Bishop), and still `mg`-heavier for
/// the same underlying reason, though slightly less steeply than the
/// pawn-attack penalties: an outright hanging piece is a real,
/// convertible material loss whenever it's actually found and captured
/// -- a threat that stays somewhat relevant into the endgame too,
/// unlike the pawn-attack penalty's more purely tempo-based mg-leaning
/// rationale above.
inline constexpr Score kKnightHangingPenalty = {-30, -25};
inline constexpr Score kBishopHangingPenalty = {-30, -25};
inline constexpr Score kRookHangingPenalty = {-40, -30};
inline constexpr Score kQueenHangingPenalty = {-50, -35};

/// Penalty for a knight/bishop/rook/queen that is the SOLE own defender
/// of two or more own knights/bishops/rooks/queens each currently
/// attacked by the enemy (this file's header comment's "Overloaded"
/// entry has the concept; overloaded_piece_value()'s own doc comment,
/// below, has the exact test). Same Rook/Queen-larger-than-Knight/
/// Bishop value-scaling logic as the two penalty tables above, but
/// smaller in magnitude than either: unlike a pawn-attacked or hanging
/// piece, an overloaded piece isn't itself under any direct attack at
/// all -- the danger is a FUTURE tactic (the enemy capturing one of the
/// two defended pieces to exploit the divided duty), not a current,
/// already-realized threat, so this is scored as a real but smaller
/// structural weakness rather than with the same weight as an
/// immediate material threat. `mg` above `eg`, matching both tables
/// above, for the same underlying reason: exploiting an overload takes
/// active, contested play to actually convert, which matters most while
/// there's still a lot of that play ahead.
inline constexpr Score kKnightOverloadedPenalty = {-18, -8};
inline constexpr Score kBishopOverloadedPenalty = {-18, -8};
inline constexpr Score kRookOverloadedPenalty = {-24, -10};
inline constexpr Score kQueenOverloadedPenalty = {-30, -12};

/// Runtime-mutable counterpart to the 12 kXxxYyyPenalty constants above
/// -- the threats-term entry in the same "runtime-mutable
/// parameter-vector abstraction over eval's currently-constexpr named
/// constants" family as eval::MaterialWeights/PsqtWeights (psqt.h),
/// eval::MobilityWeights (mobility.h), and eval::SpaceWeights (space.h)
/// already establish (ROADMAP.md's Tier 0 tuner item, "PSQT and
/// beyond" -- Step 8b, the third "beyond" term, picked ahead of pawn
/// structure/king safety specifically because it shares mobility's own
/// plain-scalar shape -- no per-square or other indexed dimension --
/// despite having more fields than mobility's 8, unlike king_safety.h's
/// kPawnStormPenalty or pawns.h's several 8-entry passed-pawn-family
/// tables, both of which need array indexing this struct doesn't).
///
/// `constexpr`-constructible via default-member-initializers naming
/// the 12 kXxxYyyPenalty constants directly, the same MaterialWeights/
/// MobilityWeights/SpaceWeights pattern (not PsqtWeights' out-of-line
/// one) -- mechanical, not a judgment call, since every one of those 12
/// constants is an `inline constexpr` value declared right here in this
/// header, not hidden in threats.cpp's own anonymous namespace.
///
/// Field naming: `<piece>_<category>_<mg|eg>`, where `<category>` is
/// `pawn` (attacked-by-a-pawn), `hanging`, or `overloaded` -- matching
/// this file's own header comment's numbered list (1. pawn-attacked,
/// 2. hanging, 3. overloaded) and threats.cpp's own
/// pawn_threat_penalty()/hanging_penalty()/overloaded_penalty() helper
/// names, just spelled out per-piece-type rather than switched on
/// PieceType the way those helpers are.
struct ThreatsWeights {
    double knight_pawn_mg = kKnightAttackedByPawnPenalty.mg;
    double knight_pawn_eg = kKnightAttackedByPawnPenalty.eg;
    double bishop_pawn_mg = kBishopAttackedByPawnPenalty.mg;
    double bishop_pawn_eg = kBishopAttackedByPawnPenalty.eg;
    double rook_pawn_mg = kRookAttackedByPawnPenalty.mg;
    double rook_pawn_eg = kRookAttackedByPawnPenalty.eg;
    double queen_pawn_mg = kQueenAttackedByPawnPenalty.mg;
    double queen_pawn_eg = kQueenAttackedByPawnPenalty.eg;

    double knight_hanging_mg = kKnightHangingPenalty.mg;
    double knight_hanging_eg = kKnightHangingPenalty.eg;
    double bishop_hanging_mg = kBishopHangingPenalty.mg;
    double bishop_hanging_eg = kBishopHangingPenalty.eg;
    double rook_hanging_mg = kRookHangingPenalty.mg;
    double rook_hanging_eg = kRookHangingPenalty.eg;
    double queen_hanging_mg = kQueenHangingPenalty.mg;
    double queen_hanging_eg = kQueenHangingPenalty.eg;

    double knight_overloaded_mg = kKnightOverloadedPenalty.mg;
    double knight_overloaded_eg = kKnightOverloadedPenalty.eg;
    double bishop_overloaded_mg = kBishopOverloadedPenalty.mg;
    double bishop_overloaded_eg = kBishopOverloadedPenalty.eg;
    double rook_overloaded_mg = kRookOverloadedPenalty.mg;
    double rook_overloaded_eg = kRookOverloadedPenalty.eg;
    double queen_overloaded_mg = kQueenOverloadedPenalty.mg;
    double queen_overloaded_eg = kQueenOverloadedPenalty.eg;
};

/// Returns a ThreatsWeights matching all 12 kXxxYyyPenalty constants
/// exactly -- the natural starting point for a threats-aware tuning run
/// (tuner::tune()-style, src/tuner/tune.h), and the values every field
/// above is already separately, redundantly initialized to (kept in
/// sync by hand, matching default_material_weights()'s/
/// default_mobility_weights()'s/default_space_weights()'s own doc
/// comment rationale for why: ThreatsWeights{}'s own default-member-
/// initializers stay self-contained and don't require calling a
/// function just to default-construct one).
[[nodiscard]] constexpr ThreatsWeights default_threats_weights() noexcept {
    return ThreatsWeights{
        /*knight_pawn_mg=*/kKnightAttackedByPawnPenalty.mg,
        /*knight_pawn_eg=*/kKnightAttackedByPawnPenalty.eg,
        /*bishop_pawn_mg=*/kBishopAttackedByPawnPenalty.mg,
        /*bishop_pawn_eg=*/kBishopAttackedByPawnPenalty.eg,
        /*rook_pawn_mg=*/kRookAttackedByPawnPenalty.mg,
        /*rook_pawn_eg=*/kRookAttackedByPawnPenalty.eg,
        /*queen_pawn_mg=*/kQueenAttackedByPawnPenalty.mg,
        /*queen_pawn_eg=*/kQueenAttackedByPawnPenalty.eg,

        /*knight_hanging_mg=*/kKnightHangingPenalty.mg,
        /*knight_hanging_eg=*/kKnightHangingPenalty.eg,
        /*bishop_hanging_mg=*/kBishopHangingPenalty.mg,
        /*bishop_hanging_eg=*/kBishopHangingPenalty.eg,
        /*rook_hanging_mg=*/kRookHangingPenalty.mg,
        /*rook_hanging_eg=*/kRookHangingPenalty.eg,
        /*queen_hanging_mg=*/kQueenHangingPenalty.mg,
        /*queen_hanging_eg=*/kQueenHangingPenalty.eg,

        /*knight_overloaded_mg=*/kKnightOverloadedPenalty.mg,
        /*knight_overloaded_eg=*/kKnightOverloadedPenalty.eg,
        /*bishop_overloaded_mg=*/kBishopOverloadedPenalty.mg,
        /*bishop_overloaded_eg=*/kBishopOverloadedPenalty.eg,
        /*rook_overloaded_mg=*/kRookOverloadedPenalty.mg,
        /*rook_overloaded_eg=*/kRookOverloadedPenalty.eg,
        /*queen_overloaded_mg=*/kQueenOverloadedPenalty.mg,
        /*queen_overloaded_eg=*/kQueenOverloadedPenalty.eg,
    };
}

/// Evaluates the threats term for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp).
///
/// Overloaded-piece test (this file's header comment's "Overloaded"
/// entry, used for kKnightOverloadedPenalty/kBishopOverloadedPenalty/
/// kRookOverloadedPenalty/kQueenOverloadedPenalty above): for each own
/// knight/bishop/rook/queen D, count how many other own knights/
/// bishops/rooks/queens T satisfy BOTH (a) T is currently attacked by
/// the enemy, AND (b) D is the ONLY own piece whose own individual
/// attack bitboard covers T's square (not merely one of several
/// defenders -- see threats.cpp's own implementation comment for
/// exactly how "only" is checked, since threats_value()'s own existing
/// attacks_by_side() union bitboards don't by themselves distinguish
/// "one defender" from "several"). D is overloaded, and penalized once
/// (not once per over-defended piece -- see this function's own
/// implementation comment for why), whenever that count reaches 2 or
/// more. Deliberately does NOT attempt to determine whether the enemy
/// could actually WIN material by exploiting the overload (that would
/// need a real capture-sequence evaluation -- search/see.h's own
/// static_exchange_evaluation(), which this file's header comment's "Why
/// a boolean attacked/defended check instead of SEE" section already
/// explains eval/ deliberately doesn't depend on) -- purely a structural
/// "this piece has more defensive duties than it can actually fulfill"
/// signal, same deliberately-simplified spirit as the hanging-piece
/// check just above it.
///
/// Precondition: board::init_masks() AND board::init_magic_bitboards()
/// have both been called -- unlike eval/piece_bonuses.h's
/// piece_bonus_value(), eval/knight_outposts.h's
/// knight_outpost_value(), and eval/space.h's space_value() (none of
/// which need magic bitboards), this function computes full attack
/// bitboards for every piece type on the board, including sliding
/// pieces, to determine which squares are attacked/defended -- the same
/// precondition eval/mobility.h's mobility_value() and eval/
/// king_safety.h's king_safety_value() already carry.
///
/// `weights`, if non-null, is used INSTEAD OF the 12 kXxxYyyPenalty
/// constants above for this call only -- the same nullable-override
/// convention material_value()/psqt_value()/mobility_value()/
/// space_value() already establish, threaded here for
/// eval::evaluate()'s own `threats_weights` parameter (eval.h) so a
/// future threats-aware tuning run can probe candidate threats weight
/// vectors the same uniform way it already can for the other four
/// terms. `weights`' own fields are `double` (ThreatsWeights' field
/// type); Score's own fields are `int` (score.h), so each override is
/// rounded via the same round_to_int() helper psqt_value()/
/// mobility_value()/space_value() already use, reused here rather than
/// duplicated.
[[nodiscard]] Score threats_value(const board::Position& pos,
                                   const ThreatsWeights* weights = nullptr) noexcept;

} // namespace nightwing::eval
