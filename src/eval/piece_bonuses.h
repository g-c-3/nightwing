#pragma once
// src/eval/piece_bonuses.h
//
// Bishop pair, rook on open/semi-open file, and rook on the 7th rank
// (ROADMAP.md Phase 5's next unchecked item, grouped together per
// Session 39's own next-start-point guidance: three simple, classic
// positional bonuses that share a file/single-module structural fit,
// the same way mobility.h and king_safety.h each grouped a handful of
// related additive components into one term). All three are standard
// CPW concepts:
//   - Bishop pair: https://www.chessprogramming.org/Bishop_Pair
//   - Rook on open/semi-open file: https://www.chessprogramming.org/Rook_on_Open_File
//   - Rook on the 7th rank: https://www.chessprogramming.org/Rook_on_Seventh_Rank
// From-scratch implementation here, no code copied, per
// ARCHITECTURE.md's Attribution Policy.
//
// Same "few hand-estimated constants, not a large tuned table"
// preference eval/pawns.h, eval/mobility.h, and eval/king_safety.h all
// already establish, ahead of ROADMAP.md Phase 5's eventual Texel
// tuner -- every constant below is a first-draft hand estimate, not
// yet Texel-tuned, same caveat every other eval term added this phase
// already carries.
//
// Deliberately its own translation unit, same one-clearly-scoped-term-
// per-file organizational convention this codebase already follows for
// every other eval/*.h module.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Flat bonus for having both bishops still on the board (CPW "Bishop
/// Pair"): two bishops together cover both square colors, a
/// combination a single bishop or a knight/bishop pair can't match.
/// `eg` above `mg`: the bishop pair's own coordination advantage grows
/// as the board opens up and fewer pieces block long diagonals --
/// mirroring mobility.h's own "eg at or above mg" reasoning (a bishop's
/// value is fundamentally a mobility/reach question) for the same
/// underlying cause: fewer pieces means more room for two long-range
/// pieces on opposite colors to matter more, not less.
inline constexpr Score kBishopPairBonus = {20, 40};

/// Bonus per rook on a fully open file (no pawn of EITHER color on that
/// file). The larger-magnitude of the two file bonuses, same ordering
/// logic as eval/king_safety.h's own kOpenFileNearKingPenalty (a
/// fully-open file is the more dangerous/more useful case in both
/// directions -- no pawn of any color to block the rook's own line of
/// attack). `mg` above `eg`: an open file's immediate value is
/// attacking chances against pieces and the enemy king while more of
/// them are still on the board to be attacked -- literally the mirror
/// image of why king_safety.cpp's own open-file penalty is itself
/// MG-heavy, seen from the attacking rook's side instead of the
/// defending king's side of the same file.
inline constexpr Score kRookOpenFileBonus = {24, 12};

/// Bonus per rook on a semi-open file (no OWN pawn, but an enemy pawn
/// still present) -- real, but smaller than kRookOpenFileBonus, same
/// reasoning as king_safety.h's own kSemiOpenFileNearKingPenalty: an
/// enemy pawn on the file is still an obstacle the rook has to get past
/// or trade off first before its pressure is completely unobstructed.
inline constexpr Score kRookSemiOpenFileBonus = {12, 6};

/// Bonus per rook on the 7th rank (relative to `c` -- rank 2 for
/// Black), the classic CPW "Rook on Seventh Rank" pattern: a rook there
/// attacks pawns still on their starting rank and helps confine the
/// enemy king to its back rank. Applied as a flat bonus whenever a rook
/// sits on that rank -- deliberately not narrowed to "only when the
/// enemy king is also on its own back rank" or "only when enemy pawns
/// are actually present to attack" (both real refinements several
/// engines make), matching this codebase's established preference for
/// the simpler first cut before a real tuner can show whether the added
/// condition is worth its complexity (see mobility.h's own "Why a flat
/// per-square bonus" reasoning for the parallel logic). `eg` above
/// `mg`: a 7th-rank rook's bite grows as material thins -- fewer
/// defenders to contest the rank or shield the king, the same general
/// "matters more with less on the board" pattern mobility.h's own
/// constants already follow.
inline constexpr Score kRookOnSeventhBonus = {10, 20};

/// Per-field `double` override of the four piece-bonus constants above,
/// the piece-bonus counterpart to eval/pawns.h's PawnsWeights,
/// eval/mobility.h's MobilityWeights and the other per-term Weights
/// types: a tuner (SPSA, tuner/spsa_eval.h's run_spsa_piece_bonus())
/// can play two different weight vectors in one process by passing a
/// `const PieceBonusWeights*` instead of relying on the compiled-in
/// `inline constexpr` values (DECISIONS.md 2026-11-03 (7)). Every field
/// defaults to the matching compiled-in constant, so a
/// default-constructed PieceBonusWeights reproduces piece_bonus_value()'s
/// built-in result exactly. Fields are `double` (so an optimizer can move
/// them continuously) and are rounded with round_to_int() when applied,
/// the same convention every other Weights type follows.
struct PieceBonusWeights {
    double bishop_pair_mg = kBishopPairBonus.mg;
    double bishop_pair_eg = kBishopPairBonus.eg;
    double rook_open_file_mg = kRookOpenFileBonus.mg;
    double rook_open_file_eg = kRookOpenFileBonus.eg;
    double rook_semi_open_file_mg = kRookSemiOpenFileBonus.mg;
    double rook_semi_open_file_eg = kRookSemiOpenFileBonus.eg;
    double rook_on_seventh_mg = kRookOnSeventhBonus.mg;
    double rook_on_seventh_eg = kRookOnSeventhBonus.eg;
};

/// Returns a PieceBonusWeights holding the four compiled-in constants
/// above (the same value a default-constructed PieceBonusWeights has).
[[nodiscard]] PieceBonusWeights default_piece_bonus_weights() noexcept;

/// Evaluates bishop pair + rook-on-open/semi-open-file + rook-on-7th-
/// rank for BOTH sides and returns a single White-relative Score
/// (positive favors White, matching every other eval/*.h term's sign
/// convention in eval.cpp).
///
/// Precondition: board::init_masks() has been called (this function
/// uses board::file_mask(), a masks.h function). Unlike mobility.h's
/// mobility_value() and king_safety.h's king_safety_value(), this
/// function does NOT need board::init_magic_bitboards() -- it never
/// calls a sliding-piece attack function, only checks static file/rank
/// membership.
///
/// `weights`, if non-null, replaces the four compiled-in constants with
/// its own (rounded) values for this call; nullptr (the default) uses the
/// compiled-in constants, so every existing caller is unaffected. The
/// default-weights path is bit-identical to the pre-override code.
[[nodiscard]] Score piece_bonus_value(const board::Position& pos,
                                      const PieceBonusWeights* weights = nullptr) noexcept;

} // namespace nightwing::eval
