#pragma once
// src/tuner/spsa_search.h
//
// SPSA binding for SEARCH constants -- ROADMAP.md, "SPSA tuner" item, step
// (2), sub-step (2c); design in docs/DECISIONS.md 2026-10-09 and
// 2026-10-09 (4).
//
// Both engines are launched from ONE tuning binary (CMake option
// NIGHTWING_SEARCH_TUNING=ON, search/tunables.h) and differ only in the
// `setoption` values of theta+ and theta-. Games are played through
// UciMatchSession (uci_match.h), node-limited (`go nodes N`) rather than
// fixed-depth, so a pruning or reduction parameter is credited for the nodes
// it saves (DECISIONS 2026-10-09). A new session is started per SPSA
// iteration. Theta is rounded to integers when sent, so every perturbation
// size c is kept at 1 or more.
//
// The tunable table below mirrors search/tunables.h (names, defaults and
// ranges). That header exists only in the tuning build, so the table is
// duplicated here; a test compiled into the tuning build checks that the two
// agree. Any tuned value still requires SPRT gating against the untuned
// production build before being committed.
//
// Requires board::init_masks(), init_magic_bitboards() and
// init_zobrist_keys() to have been called (the match referee uses them).
//
// From-scratch; algorithm credit is in spsa.h.

#include <cstdint>
#include <string>
#include <vector>

#include "tuner/spsa.h"
#include "tuner/uci_match.h"

namespace nightwing::tuner {

/// One tunable search constant: UCI option name, default, bounds and the
/// initial perturbation size c used by default.
struct SearchTunableInfo {
    const char* name;
    double default_value;
    double min_value;
    double max_value;
    double default_c;
};

/// The seven tunable search constants, in the order of theta.
[[nodiscard]] const std::vector<SearchTunableInfo>& search_tunable_table();

/// Node budget per move used when a config leaves both `movetime_ms` and
/// `nodes` at 0 (untuned).
inline constexpr int kDefaultSearchMatchNodes = 20000;

/// Builds SpsaParams from the table. `c_scale` multiplies each default c
/// (the result is kept at 1 or more); `start_scale` multiplies each default
/// value to form the start (rounded, clamped to the bounds). 1.0 for both
/// gives the defaults.
[[nodiscard]] std::vector<SpsaParam> make_search_spsa_params(double c_scale, double start_scale);

/// Builds the engine spec for one side: `Threads` 1, `Hash` 16, one
/// `setoption` per parameter with its theta value rounded to an integer and
/// clamped to the parameter's bounds, and every parameter name listed in
/// `required_options`. Missing theta entries use the parameter's start.
[[nodiscard]] UciEngineSpec make_search_engine_spec(const std::string& name,
                                                    const std::string& command,
                                                    const std::vector<SpsaParam>& params,
                                                    const std::vector<double>& theta);

/// Plays `theta_a` against `theta_b` from one tuning binary, node-limited
/// per `match` (see kDefaultSearchMatchNodes), `match.num_games` games,
/// seeds `base_seed`, `base_seed + 1`, ... Returns the session result
/// (aborted/error set on failure).
[[nodiscard]] UciMatchResult play_search_match(const std::string& command,
                                               const std::vector<SpsaParam>& params,
                                               const std::vector<double>& theta_a,
                                               const std::vector<double>& theta_b,
                                               std::uint64_t base_seed, const UciMatchConfig& match);

/// Settings for run_spsa_search().
struct SpsaSearchConfig {
    SpsaConfig spsa;
    /// Per-iteration match; `num_games` is the games per SPSA iteration.
    UciMatchConfig match;
    /// Path of the tuning binary (built with NIGHTWING_SEARCH_TUNING=ON).
    std::string engine_command;
    /// Multiplier on each default perturbation size (kept at 1 or more).
    double c_scale = 1.0;
    /// Multiplier on each default start value; far from 1 is a deliberately
    /// wrong start used to check that the optimizer recovers.
    double start_scale = 1.0;
};

/// Runs SPSA over the seven search constants. Throws std::runtime_error when
/// a match aborts (engine missing, crashed, timed out, or without the tuned
/// options), so a broken setup cannot masquerade as a null result.
[[nodiscard]] SpsaResult run_spsa_search(const SpsaSearchConfig& config);

} // namespace nightwing::tuner
