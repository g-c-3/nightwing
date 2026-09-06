#pragma once
// src/tuner/sprt.h
//
// Sequential Probability Ratio Test (SPRT) for engine-strength change
// validation — ROADMAP.md Phase 8's "SPRT testing setup/process for
// validating future changes" item.
//
// WHAT PROBLEM THIS SOLVES THAT tuner::match (match.h) ALONE DOESN'T:
// tuner::play_match() plays a FIXED number of games and reports a
// point-estimate Elo difference (MatchResult::elo_diff()) with no
// principled stopping rule — too few games leaves a real small change
// indistinguishable from noise, too many wastes compute confirming
// something already clear after far fewer games. SPRT is the standard
// answer used by real engine-testing infrastructure (the Stockfish
// project's own Fishtest; OpenBench, used by many other open-source
// engines) — a sequential hypothesis test that samples games in
// batches and stops as soon as the evidence convincingly favors one of
// two Elo hypotheses, rather than at a pre-committed fixed sample size.
//
// THE TWO HYPOTHESES: H0 ("the change is no better than elo0" — the
// null hypothesis a patch is assumed not to have cleared until shown
// otherwise) and H1 ("the change is at least as good as elo1" — the
// hypothesis a patch's author actually hopes to demonstrate). elo0 <
// elo1 by convention — e.g. elo0=0, elo1=5 is a fairly standard "does
// this help at all" test; elo0=-5, elo1=0 tests "does this NOT
// regress strength" (a non-regression test, appropriate for a refactor
// that isn't expected to change strength at all, only code shape or
// speed).
//
// METHOD — GSPRT (the generalized SPRT used by Fishtest): computes the
// log-likelihood ratio (LLR) of the observed win/draw/loss counts
// under a normal approximation to the per-game score distribution,
// following the statistical method described by Michel Van den Bergh
// behind Fishtest's own SPRT implementation, as documented on the
// Chess Programming Wiki's "Sequential Probability Ratio Test" article
// (https://www.chessprogramming.org/Sequential_Probability_Ratio_Test).
// This project's own implementation is written from scratch from that
// public description — no code copied from Fishtest, cutechess, or any
// other existing testing framework (ARCHITECTURE.md's own Attribution
// Policy).
//
// From-scratch implementation.

namespace nightwing::tuner {

/// The two Elo hypotheses and the two error-rate bounds for a GSPRT
/// run. `elo0` and `elo1` are in the same "logistic Elo" units
/// MatchResult::elo_diff() (match.h) already reports — `elo0` should
/// be strictly less than `elo1`. `alpha` (false-positive rate: the
/// probability of accepting H1 when H0 is actually true) and `beta`
/// (false-negative rate: the probability of accepting H0 when H1 is
/// actually true) default to 0.05 each, the conventional values used
/// by Fishtest and most published SPRT-based engine testing.
struct SprtConfig {
    double elo0 = 0.0;
    double elo1 = 5.0;
    double alpha = 0.05;
    double beta = 0.05;
};

/// Which of the two hypotheses (if either) the evidence gathered so
/// far supports. `Continue` means neither bound has been crossed yet
/// — more games are needed before a decision can be made.
enum class SprtStatus {
    Continue,
    AcceptH0, ///< Evidence favors "no improvement" (elo0) — reject the change.
    AcceptH1, ///< Evidence favors "real improvement" (elo1) — accept the change.
};

/// Current state of a GSPRT run given cumulative win/draw/loss counts.
struct SprtState {
    double llr = 0.0;         ///< Current log-likelihood ratio.
    double lower_bound = 0.0; ///< ln(beta / (1 - alpha)) — LLR at or below this accepts H0.
    double upper_bound = 0.0; ///< ln((1 - beta) / alpha) — LLR at or above this accepts H1.
    SprtStatus status = SprtStatus::Continue;
};

/// Converts an Elo difference to the expected per-game score
/// (probability of scoring a "win" on the 0/0.5/1 scale) via the
/// standard logistic Elo model: 1 / (1 + 10^(-elo/400)) — the same
/// formula MatchResult::elo_diff() (match.h) inverts.
[[nodiscard]] double elo_to_score(double elo) noexcept;

/// Computes the current GSPRT state from cumulative win/draw/loss
/// counts under `config`'s two Elo hypotheses and two error bounds.
///
/// Uses the normal approximation to the log-likelihood ratio of the
/// observed mean per-game score under H0 (mean = elo_to_score(elo0))
/// vs. H1 (mean = elo_to_score(elo1)), with the sample variance of the
/// per-game score computed directly from wins/draws/losses — this is
/// the GSPRT method (see this file's own header comment for
/// attribution). Returns {llr=0, ..., Continue} for zero games (no
/// evidence yet) and treats a zero observed sample variance (e.g.
/// every game so far drawn) the same way, rather than dividing by
/// zero — both are honest "keep playing" answers, not a spurious
/// early decision.
[[nodiscard]] SprtState compute_sprt(int wins, int draws, int losses,
                                      const SprtConfig& config = {}) noexcept;

} // namespace nightwing::tuner
