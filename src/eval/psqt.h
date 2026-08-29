#pragma once
// src/eval/psqt.h
//
// Material values and piece-square tables (PSQT). Values are Tomasz
// Michniewski's "Simplified Evaluation Function" (published on the
// Chess Programming Wiki: https://www.chessprogramming.org/Simplified_Evaluation_Function
// and https://www.chessprogramming.org/Piece-Square_Tables) — a
// well-known, freely published baseline table set, used here verbatim
// as a starting point per DECISIONS.md, not presented as original.
// Chosen specifically because it's a standard, recognizable baseline
// that Phase 5's Texel tuner can later refine from real values instead
// of hand-guessed ones.
//
// See psqt.cpp's header comment for the source cross-checking done
// before transcribing these numbers, and DECISIONS.md for why only the
// king gets a distinct middlegame/endgame table pair (Michniewski's
// original doesn't taper the other five piece types).

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Material values (mg, eg), Tomasz Michniewski's "Simplified Evaluation
/// Function" baseline (this file's header comment) -- named constants
/// rather than literals inline in material_value()'s switch below,
/// matching every other eval module's own established convention
/// (`inline constexpr Score kXxxBonus/kXxxPenalty = {...}`, e.g. eval/
/// tempo.h's kTempoBonus, eval/pawns.h's kIsolatedPawnPenalty) --
/// ROADMAP.md Phase 5's "All terms as named tunable constants" item,
/// closing what turned out to be the one remaining gap: an audit of
/// every eval/*.cpp scoring line (docs/DECISIONS.md, this entry) found
/// these five were the only ones still written as raw literals directly
/// in a `return` statement rather than a named constant a future Texel/
/// SPSA tuner (this same ROADMAP phase's next unchecked item) can
/// enumerate and adjust the same uniform way it will every other term.
/// `eg` currently equals `mg` for every piece (Michniewski's baseline
/// doesn't taper material) -- kept as separate named mg/mg pairs rather
/// than a single scalar specifically so the tuner can later split them
/// independently, the same reason every other Score-typed constant in
/// this codebase already carries both fields even where the two
/// currently happen to match (e.g. eval/material_imbalance.h's
/// kKnightPairPerMissingPawn's own doc comment on this point).
inline constexpr Score kPawnValue = {100, 100};
inline constexpr Score kKnightValue = {320, 320};
inline constexpr Score kBishopValue = {330, 330};
inline constexpr Score kRookValue = {500, 500};
inline constexpr Score kQueenValue = {900, 900};

/// Material value (mg, eg) for one piece of `type`, color-agnostic
/// (the caller negates for Black — see eval.cpp). `eg` currently equals
/// `mg` for every type (Michniewski's baseline doesn't taper material);
/// King returns {0, 0} since king "material" isn't counted — its value
/// is expressed entirely through psqt_value()'s king table.
[[nodiscard]] constexpr Score material_value(board::PieceType type) noexcept {
    switch (type) {
        case board::PieceType::Pawn:
            return kPawnValue;
        case board::PieceType::Knight:
            return kKnightValue;
        case board::PieceType::Bishop:
            return kBishopValue;
        case board::PieceType::Rook:
            return kRookValue;
        case board::PieceType::Queen:
            return kQueenValue;
        case board::PieceType::King:
        case board::PieceType::None:
        default:
            return {0, 0};
    }
}

/// Piece-square table value (mg, eg) for `piece` (a specific color+type)
/// standing on `sq`. Positive always favors `piece`'s own color — the
/// caller (eval.cpp) adds this for White pieces and subtracts it for
/// Black, exactly as it does with material_value().
[[nodiscard]] Score psqt_value(board::Piece piece, board::Square sq) noexcept;

} // namespace nightwing::eval
