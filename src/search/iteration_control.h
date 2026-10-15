#pragma once
// src/search/iteration_control.h
//
// Pure decision rule for "should iterative deepening start another
// iteration?" under a soft/hard time budget (Session 224). Split out of
// search_iterative_deepening() (search.cpp) so the rule can be unit-tested
// without running a search (tests/iteration_control_tests.cpp).
//
// Background: a clock-simulation self-play measurement (Session 224) showed
// the previous rule -- stop only when the best move had been stable for 4
// iterations AND elapsed >= soft, checked only at iteration start -- almost
// never fired: 99% of moves used more than the soft budget and the median
// move used the full hard budget (3x soft) at a 20 s sudden-death control.
// An iteration begun just below the soft budget typically needs longer than
// the remaining slack, so it ran to the hard cap and was then discarded
// wholesale (search.cpp: a truncated iteration's result is not used).
//
// The rule below (a) ends the search once soft*fraction of the budget is
// spent, with a smaller fraction when the best move is stable, and (b)
// refuses to start an iteration predicted to overrun the hard cap, since such
// an iteration would be discarded anyway. Predicting the next iteration's
// cost from the previous one follows common practice in other engines'
// time management (CPW "Time Management"); the specific constants are
// Nightwing's own starting values, not yet tuned.

namespace nightwing::search {

/// Fraction of the soft budget, as a numerator/denominator pair, after which
/// a STABLE best move (unchanged for kStabilityThreshold iterations) ends the
/// search.
inline constexpr int kStableSoftNum = 3;
inline constexpr int kStableSoftDen = 5;

/// Fraction of the soft budget, as a numerator/denominator pair, after which
/// an UNSTABLE best move ends the search. Between the two fractions an
/// unstable position keeps searching, which is the "extend when unstable"
/// intent of the soft/hard split.
inline constexpr int kUnstableSoftNum = 1;
inline constexpr int kUnstableSoftDen = 1;

/// Assumed growth of an iteration's cost relative to the previous iteration
/// (time of depth d+1 ~= kIterationGrowth x time of depth d). 3 is the upper
/// end of the typical 1.5x to 3x range, chosen because a refused iteration
/// costs nothing while an interrupted one is discarded. Measured in the
/// Session 224 clock simulation (20 s sudden death, 12 games): growth 2 gave
/// p90 time/soft 3.00 and a lowest final clock of 208 ms; growth 3 gave 2.80
/// and 327 ms. Not yet validated for playing strength.
inline constexpr long long kIterationGrowth = 3;

/// Decides whether search_iterative_deepening() should start another depth.
///
/// Returns true ("go on") when `soft_ms <= 0`, i.e. there is no soft budget:
/// callers without one (fixed depth, `movetime`, infinite) behave exactly as
/// before this rule existed.
///
/// Otherwise returns false when either:
///  - the predicted end of the next iteration (`elapsed_ms + kIterationGrowth
///    * last_iteration_ms`) would pass `hard_ms` (when `hard_ms > 0`); or
///  - `elapsed_ms` has reached the stable or unstable fraction of `soft_ms`,
///    chosen by `stable`.
///
/// @param elapsed_ms         Time since the search started, in ms.
/// @param last_iteration_ms  Duration of the most recently completed
///                           iteration, in ms (0 if unknown).
/// @param soft_ms            Soft budget in ms (<= 0 means none).
/// @param hard_ms            Hard budget in ms (<= 0 means none).
/// @param stable             True when the best move has been unchanged for
///                           kStabilityThreshold consecutive iterations.
[[nodiscard]] inline bool should_start_next_iteration(long long elapsed_ms,
                                                       long long last_iteration_ms, int soft_ms,
                                                       int hard_ms, bool stable) noexcept {
    if (soft_ms <= 0) {
        return true;
    }
    if (hard_ms > 0 && elapsed_ms + kIterationGrowth * last_iteration_ms > hard_ms) {
        return false;
    }
    const long long num = stable ? kStableSoftNum : kUnstableSoftNum;
    const long long den = stable ? kStableSoftDen : kUnstableSoftDen;
    // elapsed >= soft * num / den, in integers.
    return elapsed_ms * den < static_cast<long long>(soft_ms) * num;
}

} // namespace nightwing::search
