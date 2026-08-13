#pragma once
// src/eval/score.h
//
// A tapered (middlegame/endgame) evaluation score type and the game-phase
// machinery used to blend the two into a single centipawn value. This is
// the standard "Tapered Eval" technique described on the Chess
// Programming Wiki (https://www.chessprogramming.org/Tapered_Eval) —
// from-scratch implementation here, no code copied, only the general
// approach (per-piece phase weights summing to a fixed maximum, linear
// interpolation between mg/eg terms) borrowed and credited per
// ARCHITECTURE.md's Attribution Policy.

namespace nightwing::eval {

/// A pair of centipawn values for one evaluation term: `mg` applies in
/// the middlegame, `eg` in the endgame. The two are combined by taper()
/// once, at the very end of evaluate(), using the current game phase —
/// individual eval terms (material, PSQT, and later mobility/king
/// safety/pawn structure) stay in this unblended pair form so they can
/// simply add together term-by-term first.
struct Score {
    int mg = 0;
    int eg = 0;

    constexpr Score() noexcept = default;
    constexpr Score(int mg_value, int eg_value) noexcept : mg(mg_value), eg(eg_value) {}

    constexpr Score& operator+=(const Score& other) noexcept {
        mg += other.mg;
        eg += other.eg;
        return *this;
    }

    constexpr Score& operator-=(const Score& other) noexcept {
        mg -= other.mg;
        eg -= other.eg;
        return *this;
    }

    [[nodiscard]] constexpr Score operator+(const Score& other) const noexcept {
        return {mg + other.mg, eg + other.eg};
    }

    [[nodiscard]] constexpr Score operator-(const Score& other) const noexcept {
        return {mg - other.mg, eg - other.eg};
    }

    [[nodiscard]] constexpr Score operator-() const noexcept {
        return {-mg, -eg};
    }
};

/// Per-piece-type game-phase weights (standard CPW "Tapered Eval"
/// weighting: pawns don't count, minors are worth 1, a rook 2, a queen
/// 4). `kMaxPhase` is the sum across a full starting position's non-pawn
/// material for both sides (4 knights + 4 bishops + 4 rooks + 2 queens),
/// i.e. phase == kMaxPhase means "as middlegame-y as the position can
/// be" and phase == 0 means "no non-pawn material left, purely endgame."
inline constexpr int kKnightPhase = 1;
inline constexpr int kBishopPhase = 1;
inline constexpr int kRookPhase = 2;
inline constexpr int kQueenPhase = 4;
inline constexpr int kMaxPhase =
    kKnightPhase * 4 + kBishopPhase * 4 + kRookPhase * 4 + kQueenPhase * 2; // 24

/// Blends a tapered Score into a single centipawn value given a game
/// phase. `phase` is expected in [0, kMaxPhase] (computed by
/// eval.cpp's compute_phase()); values outside that range are clamped
/// defensively rather than trusted, since a future incremental phase
/// counter (see eval.h's header comment on incremental updates) is a
/// more plausible source of a stale/out-of-range value than today's
/// from-scratch-every-call computation is.
[[nodiscard]] constexpr int taper(const Score& s, int phase) noexcept {
    if (phase < 0) {
        phase = 0;
    } else if (phase > kMaxPhase) {
        phase = kMaxPhase;
    }
    return (s.mg * phase + s.eg * (kMaxPhase - phase)) / kMaxPhase;
}

} // namespace nightwing::eval
