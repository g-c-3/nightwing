#pragma once
// src/uci/time_budget.h
//
// Pure, side-effect-free time-budget arithmetic for `go wtime/btime/winc/
// binc/movestogo` and `Move Overhead`, split out of src/uci/uci.cpp's
// anonymous namespace (Session 222 review of time management) so each rule
// can be unit-tested directly (tests/time_budget_tests.cpp) instead of only
// through a full `go` round trip. Header-only: every function is `inline`
// and `noexcept`, with no dependency on Position, the search, or any I/O.
//
// The allocation scheme itself (remaining/divisor + increment, capped at
// half the remaining clock; hard cap = kHardBudgetMultiplier x soft) is
// unchanged from the code this was extracted from. Divisor/increment
// heuristics follow common practice described on the Chess Programming
// Wiki ("Time Management"); the soft/hard split and stability stop are
// Nightwing's own (docs/DECISIONS.md).

namespace nightwing::uci {

/// Result of allocate_time_ms(): a SOFT budget (the ordinary, "expected"
/// allocation for this move -- the early-stop threshold once the best move
/// has stabilized, search.h's `soft_time_limit_ms` doc comment) and a HARD
/// budget (an upper bound the search may extend into if the position turns
/// out to be genuinely unstable, but never exceed). `hard_ms >= soft_ms`
/// always.
struct TimeAllocation {
    int soft_ms = 0;
    int hard_ms = 0;
};

/// How much larger the hard cap is allowed to be than the soft budget
/// (ROADMAP.md Phase 8, "Time management" -- the best-move-stability-based
/// extension sub-item) when the position is unstable enough that the soft
/// early-stop in search_iterative_deepening() never triggers. 3x is a
/// common, reasonable starting point in other engines' time-management
/// schemes -- like kStabilityThreshold (search.cpp), not yet tuned for
/// Nightwing specifically.
inline constexpr int kHardBudgetMultiplier = 3;

/// Moves assumed to remain when the GUI gives no `movestogo`.
inline constexpr int kDefaultMovesToGo = 20;

/// Bounds of the share of the increment added to the soft budget, in
/// percent (the `IncrementPercent` UCI option, uci.cpp).
inline constexpr int kMinIncrementPercent = 0;
inline constexpr int kMaxIncrementPercent = 100;

/// Default of the `increment_percent` parameter of allocate_time_ms(): 100,
/// the original behaviour -- the whole increment is spent on top of
/// remaining/divisor. Kept at 100 so direct callers and the arithmetic unit
/// tests are unaffected; the engine's own default is kEngineIncrementPercent.
inline constexpr int kDefaultIncrementPercent = 100;

/// Default of the `IncrementPercent` UCI option, i.e. what the engine uses
/// unless a GUI sets the option (Session 226). 75 keeps a larger clock
/// reserve than 100: it scored +15.5 Elo (95% interval +6.6..+24.5, 4659
/// games, four runs, all positive) at 5 s + 0.05 s, and +14.3 (+1.9..+26.8,
/// 2329 games) at 10 s + 0.1 s, against 100 -- docs/SESSIONS.md, Session 226.
inline constexpr int kEngineIncrementPercent = 75;

/// Time allocation for `go wtime/btime/winc/binc` -- returns both a soft
/// (expected/normal) and a hard (maximum, extendable-into-if-unstable)
/// budget.
///
/// `movestogo`, when positive, replaces the fixed "assume roughly 20 moves
/// remain" guess with the GUI's exact count; `movestogo <= 0` (absent or
/// malformed) falls back to kDefaultMovesToGo.
///
/// Both budgets are capped at `remaining_ms / 2` (never claim more than
/// half of what's left, regardless of `movestogo` or the hard multiplier).
/// `remaining_ms <= 0` yields {50, 50}: as little as possible, but not zero.
///
/// @param remaining_ms  The side to move's own remaining clock, in ms.
/// @param increment_ms  The side to move's own increment per move, in ms.
/// @param movestogo     Moves until the next time control, or <= 0 if unknown.
/// @param increment_percent  Share of `increment_ms` added to the soft
///                      budget, clamped to [kMinIncrementPercent,
///                      kMaxIncrementPercent]; the default (100) adds all
///                      of it, exactly as before this parameter existed.
[[nodiscard]] inline TimeAllocation
allocate_time_ms(int remaining_ms, int increment_ms, int movestogo,
                 int increment_percent = kDefaultIncrementPercent) noexcept {
    if (remaining_ms <= 0) {
        return {50, 50};
    }
    const int divisor = movestogo > 0 ? movestogo : kDefaultMovesToGo;
    const int cap = remaining_ms / 2;

    // `long long` throughout: `remaining_ms / divisor + increment_ms` can
    // exceed INT_MAX for an extreme remaining clock plus a positive
    // increment (signed overflow, i.e. undefined behaviour, in `int`).
    if (increment_percent < kMinIncrementPercent) {
        increment_percent = kMinIncrementPercent;
    } else if (increment_percent > kMaxIncrementPercent) {
        increment_percent = kMaxIncrementPercent;
    }
    const long long increment_share = static_cast<long long>(increment_ms) * increment_percent / 100;
    long long soft_wide = static_cast<long long>(remaining_ms / divisor) + increment_share;
    if (soft_wide > cap) {
        soft_wide = cap;
    }
    if (soft_wide < 1) {
        soft_wide = 1;
    }
    const int soft = static_cast<int>(soft_wide);

    // `long long` for the multiply, for the same reason.
    long long hard = static_cast<long long>(soft) * kHardBudgetMultiplier;
    if (hard > cap) {
        hard = cap;
    }
    if (hard < soft) {
        hard = soft; // Never let the hard cap be smaller than the soft budget.
    }
    return {soft, static_cast<int>(hard)};
}

/// Picks the clock a `go` should budget from. Returns the side to move's own
/// remaining time when it was given (>= 0). When only the OPPONENT's clock
/// was given, returns that instead as the best available estimate (clocks in
/// a real game are normally equal or close), so a `go` carrying just one
/// clock is still time-bounded. Returns -1 when neither clock was given,
/// meaning "no clock-based time control" (`go infinite`, bare `go`).
///
/// Before Session 222 a missing own clock was treated as "no time control",
/// so `go btime 6000` with White to move searched unbounded until `stop`
/// (reproduced on the real engine) -- a flag-fall risk with a GUI that sends
/// only one clock.
///
/// @param wtime  White's clock from `go`, or a negative value if absent.
/// @param btime  Black's clock from `go`, or a negative value if absent.
/// @param white_to_move  True when White is the side to move.
[[nodiscard]] inline int select_own_clock_ms(int wtime, int btime, bool white_to_move) noexcept {
    const int own = white_to_move ? wtime : btime;
    if (own >= 0) {
        return own;
    }
    const int opponent = white_to_move ? btime : wtime;
    return opponent >= 0 ? opponent : -1;
}

/// Subtracts the `Move Overhead` safety margin from a hard (`time_limit_ms`)
/// and soft (`soft_time_limit_ms`) budget pair, in place. A value <= 0 means
/// "no budget of that kind" and is left untouched (nothing to trim a margin
/// off of). A positive value is reduced by `overhead_ms` and floored at 1 ms.
/// The soft budget is then clamped so `soft <= hard` still holds whenever
/// both are positive.
///
/// @param hard_ms      In/out: hard budget in ms (0 = none).
/// @param soft_ms      In/out: soft budget in ms (0 = none).
/// @param overhead_ms  Margin to subtract, in ms (>= 0).
inline void apply_move_overhead(int& hard_ms, int& soft_ms, int overhead_ms) noexcept {
    if (hard_ms > 0) {
        hard_ms -= overhead_ms;
        if (hard_ms < 1) {
            hard_ms = 1;
        }
    }
    if (soft_ms > 0) {
        soft_ms -= overhead_ms;
        if (soft_ms < 1) {
            soft_ms = 1;
        }
        // Overhead could push soft above hard after both were independently
        // floored at 1 -- keep soft <= hard true after overhead too.
        if (hard_ms > 0 && soft_ms > hard_ms) {
            soft_ms = hard_ms;
        }
    }
}

} // namespace nightwing::uci
