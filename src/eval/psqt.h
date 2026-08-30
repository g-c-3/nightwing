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

/// Rounds `x` to the nearest int, half away from zero — used to convert
/// a MaterialWeights field (a tuner's in-progress `double`) back to the
/// plain int a Score actually stores. Written out as a plain expression
/// rather than calling std::lround()/std::round(): those aren't
/// guaranteed constexpr until C++23 (this codebase targets C++20 — see
/// ARCHITECTURE.md's tech stack), and material_value() below is
/// constexpr itself (its zero-`weights` default path is genuinely
/// compile-time-evaluable, and stays so this way — a non-constexpr
/// <cmath> call in the `weights != nullptr` branch would still compile
/// fine for RUNTIME calls, but there's no reason to take on that
/// dependency at all when three lines of plain arithmetic do the same
/// job).
[[nodiscard]] constexpr int round_to_int(double x) noexcept {
    return static_cast<int>(x >= 0.0 ? x + 0.5 : x - 0.5);
}

/// Runtime-mutable counterpart to kPawnValue/kKnightValue/.../kQueenValue
/// above — the first (and, as of this session, only) piece of the
/// "runtime-mutable parameter-vector abstraction over eval's currently-
/// constexpr named constants" a gradient-descent tuner needs (ROADMAP.md
/// Phase 5's Texel/SPSA tuner item; docs/DECISIONS.md, this struct's
/// introducing entry, has the full rationale for why material values are
/// this abstraction's first covered term rather than starting with
/// every eval term at once, and for the deliberate `double`, not `int`,
/// field type — a tuning run's own intermediate values, not the search/
/// eval hot path's, which still only ever reads the plain-int
/// kXxxValue constants above via material_value()'s zero-override
/// default path). Every field defaults to that same constant's current
/// value, via default_material_weights() below, so a freshly constructed
/// MaterialWeights (or one no caller has perturbed yet) behaves
/// identically to the constexpr defaults it starts from.
struct MaterialWeights {
    double pawn_mg = 100.0;
    double pawn_eg = 100.0;
    double knight_mg = 320.0;
    double knight_eg = 320.0;
    double bishop_mg = 330.0;
    double bishop_eg = 330.0;
    double rook_mg = 500.0;
    double rook_eg = 500.0;
    double queen_mg = 900.0;
    double queen_eg = 900.0;
};

/// Returns a MaterialWeights matching kPawnValue/kKnightValue/.../
/// kQueenValue exactly — the natural starting point for a tuning run
/// (tuner::tune(), src/tuner/tune.h), and the values every field above
/// is already separately, redundantly initialized to (kept in sync by
/// hand, not derived from this function, so that MaterialWeights{}'s
/// own default-member-initializers stay self-contained and don't
/// require calling a function just to default-construct one — this
/// function exists for callers that want that mapping made explicit/
/// named, and for tests confirming the two really do agree).
[[nodiscard]] constexpr MaterialWeights default_material_weights() noexcept {
    return MaterialWeights{
        /*pawn_mg=*/kPawnValue.mg,     /*pawn_eg=*/kPawnValue.eg,
        /*knight_mg=*/kKnightValue.mg, /*knight_eg=*/kKnightValue.eg,
        /*bishop_mg=*/kBishopValue.mg, /*bishop_eg=*/kBishopValue.eg,
        /*rook_mg=*/kRookValue.mg,     /*rook_eg=*/kRookValue.eg,
        /*queen_mg=*/kQueenValue.mg,   /*queen_eg=*/kQueenValue.eg,
    };
}

/// Material value (mg, eg) for one piece of `type`, color-agnostic
/// (the caller negates for Black — see eval.cpp). `eg` currently equals
/// `mg` for every type (Michniewski's baseline doesn't taper material);
/// King returns {0, 0} since king "material" isn't counted — its value
/// is expressed entirely through psqt_value()'s king table.
///
/// `weights`, if non-null, is used INSTEAD of the kPawnValue/.../
/// kQueenValue constants above — every existing call site (search/
/// see.cpp, search/ordering.cpp, search/quiescence.cpp, and eval.cpp's
/// own default-path call) omits it, so nothing outside the not-yet-
/// built gradient-descent tuner is affected by this parameter's mere
/// existence. King/None still always return {0, 0} regardless of
/// `weights` — a tuner has no material term to adjust for a piece
/// that was never counted as material in the first place.
[[nodiscard]] constexpr Score material_value(board::PieceType type,
                                              const MaterialWeights* weights = nullptr) noexcept {
    if (weights == nullptr) {
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
    switch (type) {
        case board::PieceType::Pawn:
            return {round_to_int(weights->pawn_mg), round_to_int(weights->pawn_eg)};
        case board::PieceType::Knight:
            return {round_to_int(weights->knight_mg), round_to_int(weights->knight_eg)};
        case board::PieceType::Bishop:
            return {round_to_int(weights->bishop_mg), round_to_int(weights->bishop_eg)};
        case board::PieceType::Rook:
            return {round_to_int(weights->rook_mg), round_to_int(weights->rook_eg)};
        case board::PieceType::Queen:
            return {round_to_int(weights->queen_mg), round_to_int(weights->queen_eg)};
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
