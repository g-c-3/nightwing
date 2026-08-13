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

/// Material value (mg, eg) for one piece of `type`, color-agnostic
/// (the caller negates for Black — see eval.cpp). `eg` currently equals
/// `mg` for every type (Michniewski's baseline doesn't taper material);
/// King returns {0, 0} since king "material" isn't counted — its value
/// is expressed entirely through psqt_value()'s king table.
[[nodiscard]] constexpr Score material_value(board::PieceType type) noexcept {
    switch (type) {
        case board::PieceType::Pawn:
            return {100, 100};
        case board::PieceType::Knight:
            return {320, 320};
        case board::PieceType::Bishop:
            return {330, 330};
        case board::PieceType::Rook:
            return {500, 500};
        case board::PieceType::Queen:
            return {900, 900};
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
