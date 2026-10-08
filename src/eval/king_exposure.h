#pragma once
// src/eval/king_exposure.h
//
// Restricted nonlinear feature interaction, candidate 4 (ROADMAP.md,
// "Restricted nonlinear feature interactions in the evaluator"): enemy
// king attackers scaled by king exposure. The approach follows the
// general CPW "King Safety" concept of attack weight multiplied by a
// shelter-quality factor (https://www.chessprogramming.org/King_Safety);
// the formula below is a from-scratch implementation, no code copied,
// per ARCHITECTURE.md's Attribution Policy.
//
// STATUS: standalone and UNWIRED. Nothing in eval::evaluate() calls
// king_exposure_value() yet. Wiring, the Texel fit and the `uci-match`
// SPRT are separate later steps (see DECISIONS.md).
//
// Per side, the term is:
//
//     penalty = attack_units * exposure * weight
//
// where
//   - `attack_units` is the same weighted count of enemy knights (1),
//     bishops (1), rooks (2) and queens (4) attacking at least one
//     square of the king zone (the king's square plus every square a
//     king there could move to) that king_safety.cpp already uses for
//     its flat, linear attacker-weighting component. The scan is
//     duplicated file-privately here rather than exported from
//     king_safety.cpp, so that completed, green code stays untouched;
//     a shared scan is a possible optimization at wiring time if
//     `bench` shows a per-node cost.
//   - `exposure` is the number of files among the king's own file and
//     its two neighbors (clipped at the board edge, so 0..3) on which
//     the king's side has NO pawn inside the 2-rank-deep shield zone
//     directly in front of the king. A pawn that has advanced beyond
//     that zone counts as missing. An intact shelter scores 0, so a
//     healthy castled king is never charged by this term regardless of
//     how many pieces eye it.
//
// Why this is a genuine interaction and not a re-weighting of existing
// terms: king_safety_value() already charges attack units linearly and
// charges open/semi-open files and missing shield pawns separately and
// additively. This term charges only the PRODUCT, i.e. attackers
// against a broken shelter, which no additive combination of the
// existing terms can express.
//
// Single weight pair, by design. Candidate 3's Texel fit failed because
// several near-collinear columns absorbed unrelated signal and came out
// with opposite signs (DECISIONS.md 2026-10-07 (8)). One scalar feature
// (units x exposure) with one (mg, eg) pair leaves no such freedom.
//
// MG-heavy and EG-light like every other king-safety constant: the
// default eg weight is 0, and eval::taper() fades the term out as
// pieces leave the board.

#include "board/board.h"
#include "eval/psqt.h" // round_to_int()
#include "eval/score.h"

namespace nightwing::eval {

/// Penalty per (attack unit x exposed shelter file) for the defending
/// side. First-draft hand estimate, not yet Texel-fitted. Worst case
/// (3 exposed files, roughly 12 attack units) is about -36 mg, the same
/// order of magnitude as kPawnStormPenalty's largest entry.
inline constexpr Score kExposureAttackPenalty = {-1, 0};

/// Runtime-mutable counterpart to kExposureAttackPenalty, in the same
/// "parameter-vector abstraction" family as KingSafetyWeights
/// (king_safety.h). Both fields are `double` so a tuner can move them
/// fractionally; see king_exposure_value() for the rounding rule.
struct KingExposureWeights {
    double attack_exposure_mg = kExposureAttackPenalty.mg;
    double attack_exposure_eg = kExposureAttackPenalty.eg;
};

/// Returns a KingExposureWeights matching kExposureAttackPenalty exactly.
[[nodiscard]] constexpr KingExposureWeights default_king_exposure_weights() noexcept {
    return KingExposureWeights{
        /*attack_exposure_mg=*/kExposureAttackPenalty.mg,
        /*attack_exposure_eg=*/kExposureAttackPenalty.eg,
    };
}

/// Returns the exposure level (0..3) of `side`'s king: the number of
/// files among the king's own file and its two neighbors (clipped at the
/// board edge) with no `side` pawn inside the 2-rank shield zone in
/// front of the king. Returns 0 if `side` has no king (defensive only;
/// never occurs in a legal position). Public so tests and a future
/// tuner can inspect the feature directly.
[[nodiscard]] int king_exposure_level(const board::Position& pos, board::Color side) noexcept;

/// Returns the White-relative Score of the attackers-x-exposure term for
/// both sides (positive favors White, matching every other eval/*.h
/// term). Each side contributes `-(units * exposure)` scaled by the
/// weight, so a broken shelter under attack lowers that side's score.
///
/// Rounding: with `weights == nullptr` the compiled-in Score is
/// multiplied by the integer product `units * exposure`. With a
/// non-null `weights` the product is multiplied by the double weight and
/// the result rounded once via round_to_int(), so fractional weights
/// stay meaningful. The two paths agree exactly for
/// default_king_exposure_weights().
///
/// Precondition: board::init_masks() and board::init_magic_bitboards()
/// have both been called.
[[nodiscard]] Score king_exposure_value(const board::Position& pos,
                                         const KingExposureWeights* weights = nullptr) noexcept;

} // namespace nightwing::eval
