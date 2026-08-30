#pragma once
// src/tuner/match.h
//
// Head-to-head strength comparison between two eval::MaterialWeights
// vectors — the "strength comparison" half of ROADMAP.md Phase 5's
// final item, "Tuned weights committed, before/after strength
// comparison logged." Answers the question tuner::tune (tune.h) alone
// can't: does a tuned weight vector actually play BETTER chess than the
// one it started from, not just fit the training data's labels more
// closely (a lower Texel loss doesn't by itself guarantee a stronger
// engine — this is a real, separate thing worth measuring).
//
// WHAT THIS PLAYS: `weights_a` vs `weights_b`, `num_games` games,
// alternating which one plays White each game (this file's own header
// comment below on why) — reusing this codebase's existing
// search_fixed_depth() for both sides' actual move selection (now
// threading an optional per-search eval::MaterialWeights override all
// the way down through negamax()/quiescence() — search.h's own doc
// comment on search_fixed_depth()'s `material_weights` parameter,
// search/quiescence.h's own doc comment on its own — a capability that
// didn't exist before this session and had to be built as this file's
// own prerequisite), and the identical random-opening-plus-max-plies-
// safety-net game structure tuner::selfplay.h already established
// (checkmate/stalemate/50-move/threefold-repetition/max_plies, the same
// four end conditions, for the same reasons — selfplay.h's own header
// comment on why a max_plies cap exists given insufficient-material
// detection isn't built yet applies identically here).
//
// WHY THIS IS A SEPARATE MODULE FROM tuner::selfplay, NOT A SHARED/
// GENERALIZED GAME-PLAYING FUNCTION: selfplay.h's own game loop is
// scoped to a SINGLE weight vector (both sides use the same one) plus
// quiet-position SAMPLING for a training corpus — neither of those
// applies here (two DIFFERENT weight vectors, one per side, and no
// training data is produced, only a final win/draw/loss tally). The
// two loops share a similar shape (same four end conditions, same
// random-opening mechanism) but forcing them into one shared function
// with extra parameters to cover both cases would blur two genuinely
// different tools' purposes for a modest amount of code reuse — a
// deliberate choice, not an oversight (docs/DECISIONS.md, this file's
// introducing entry, has the full rationale).
//
// WHY COLOR ALTERNATES EVERY GAME RATHER THAN weights_a ALWAYS PLAYING
// WHITE: White's first-move advantage is real and would otherwise
// confound the comparison entirely -- a weight vector that happens to
// always play White in every game of a match could look stronger (or
// weaker) purely from that advantage, independent of whether its own
// eval values are actually better. Alternating colors game-to-game
// (this file's own play_match(), tune.cpp) means each weight vector
// gets an equal share of White/Black across the match, so the final
// tally reflects the weight vectors' own relative strength, not which
// one got the extra tempo more often.
//
// From-scratch implementation; no code copied from any existing match/
// tournament-manager tool.

#include <cstdint>
#include <vector>

#include "eval/psqt.h"

namespace nightwing::tuner {

/// Tunable knobs for a match run. Deliberately modest defaults, same
/// "run by hand/CI at whatever scale the caller actually wants"
/// philosophy as tuner::SelfPlayConfig (selfplay.h's own header
/// comment) — this struct's fields are NOT meant to be trusted at their
/// defaults for a real, statistically meaningful strength verdict; see
/// `num_games`'s own comment.
struct MatchConfig {
    /// Number of games to play. Should be even for exactly equal color
    /// alternation (play_match(), match.cpp, alternates starting from
    /// game 0 = weights_a plays White; an odd `num_games` just means the
    /// last game's own color assignment has no matching "other half" —
    /// not an error, just slightly less perfectly balanced). A SMALL
    /// number of games (this struct's own default) is adequate for
    /// confirming the match harness itself works end to end (this
    /// module's own tests, tests/match_tests.cpp) — a real "before/
    /// after strength comparison" (ROADMAP.md's own wording, the item
    /// this module exists to serve) needs far more games than this
    /// default for a statistically meaningful result; that real,
    /// large-scale run is separate follow-on work this module enables
    /// but doesn't itself perform.
    int num_games = 20;

    /// Passed straight through to search_fixed_depth() for every non-
    /// random-opening move of the game, both sides — same reasoning as
    /// tuner::SelfPlayConfig::search_depth (selfplay.h's own comment):
    /// fixed depth keeps a match reproducible and machine-speed-
    /// independent.
    int search_depth = 4;

    /// Number of plies at the start of each game played as a uniformly
    /// random legal move — same mechanism and rationale as
    /// tuner::SelfPlayConfig::random_opening_plies (selfplay.h's own
    /// header comment on "WHY GAME-TO-GAME DIVERSITY..."): without it,
    /// every game in the match would follow the identical deterministic
    /// line search_fixed_depth() always finds, and a "20-game match"
    /// would really just be one game played 20 times.
    int random_opening_plies = 8;

    /// Hard game-length cap, in plies — same rationale as
    /// tuner::SelfPlayConfig::max_plies (selfplay.h's own header
    /// comment on "WHY A max_plies SAFETY CAP EXISTS").
    int max_plies = 200;
};

/// Result of one play_match() call.
struct MatchResult {
    int wins_a = 0;
    int wins_b = 0;
    int draws = 0;

    /// wins_a + wins_b + draws — a convenience field (always derivable
    /// from the three above, kept explicit so callers/tests don't need
    /// to re-derive it every time) rather than a separate parameter.
    int games_played = 0;

    /// `weights_a`'s match score, on the standard 1-per-win/0.5-per-
    /// draw/0-per-loss scale, as a fraction of `games_played` — 0.0
    /// (lost every game) to 1.0 (won every game), 0.5 meaning an
    /// exactly even match. Returns 0.5 for `games_played == 0` (no
    /// evidence either way) rather than dividing by zero.
    [[nodiscard]] double score_a() const noexcept;

    /// A rough Elo difference estimate (`weights_a` minus `weights_b`)
    /// derived from score_a() via the standard logistic score-to-Elo
    /// formula (`400 * log10(score / (1 - score))`) — the same formula
    /// engine-testing tools conventionally use to turn a match score
    /// into a familiar Elo-difference figure. "Rough" because a small
    /// `num_games` (MatchConfig's own default, and any small smoke-test
    /// run) gives this estimate a very wide real confidence interval
    /// this function does not itself compute or report — a genuinely
    /// meaningful Elo estimate needs many more games than this module's
    /// own tests or a quick sanity run would ever use (MatchConfig's own
    /// `num_games` comment). score_a() of exactly 0.0 or 1.0 (won/lost
    /// every game) is clamped to just inside that range before the
    /// log10 (which is otherwise undefined at those exact endpoints) --
    /// even then, a 0% or 100% score from a small sample doesn't
    /// deserve a precise-looking number; the clamp exists so this
    /// function always returns SOME finite value rather than NaN/
    /// infinity, not to imply that value is trustworthy at face value.
    [[nodiscard]] double elo_diff() const noexcept;
};

/// Plays `config.num_games` games between `weights_a` and `weights_b`,
/// seeded `base_seed`, `base_seed + 1`, ... (same reproducibility
/// contract as tuner::play_games() — selfplay.h's own doc comment on
/// why consecutive, not re-derived, per-game seeds), alternating which
/// one plays White each game (this file's own header comment), and
/// returns the aggregate result.
///
/// Precondition: same as search_fixed_depth()'s own — init_masks()/
/// init_magic_bitboards() have been called.
[[nodiscard]] MatchResult play_match(const eval::MaterialWeights& weights_a,
                                      const eval::MaterialWeights& weights_b,
                                      std::uint64_t base_seed, const MatchConfig& config = {});

} // namespace nightwing::tuner
