// src/search/tunables.h
//
// Runtime-settable search constants for SPSA tuning (ROADMAP.md, "SPSA
// tuner", step (2), sub-step (2b); docs/DECISIONS.md 2026-10-09 and
// 2026-10-09 (3)).
//
// GATING. Everything below the `#if` exists only when the CMake option
// NIGHTWING_SEARCH_TUNING is ON (default OFF), which defines the
// NIGHTWING_SEARCH_TUNING macro on nightwing_lib. In the default build this
// header declares only `kSearchTuningBuild = false`, search.cpp keeps its
// anonymous-namespace `constexpr` constants unchanged, and no UCI option
// below exists -- the production binary and its `bench` node count are
// identical by construction. The tuning binary is never shipped.
//
// THREAD SAFETY. The values are plain ints, written only by `setoption`
// and read by search threads. A tuning driver sends `setoption` only while
// no search is running (between games), so no synchronisation is used.
// Changing an option during a search is unsupported in the tuning build.
//
// The idea of exposing search constants as UCI spin options for SPSA
// follows common practice in open engines (Stockfish-classic's SPSA
// tuning builds, CPW "SPSA"); this implementation is written from scratch.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace nightwing::search {

#if defined(NIGHTWING_SEARCH_TUNING)

/// True in the tuning build, false otherwise.
inline constexpr bool kSearchTuningBuild = true;

/// Current values of the tunable search constants. Defaults equal the
/// compiled-in constants of the production build.
struct SearchTunables {
    int null_move_reduction = 2;           ///< kNullMoveReduction
    int null_move_big_reduction = 3;       ///< kNullMoveBigReduction
    int probcut_margin = 200;              ///< kProbCutMargin (centipawns)
    int iir_min_depth = 4;                 ///< kIIRMinDepth
    int singular_margin_per_ply = 2;       ///< kSingularMarginPerPly
    int improving_futility_delta = 60;     ///< kImprovingFutilityMarginDelta
    int aspiration_initial_delta = 25;     ///< kAspirationInitialDelta
};

/// The single process-wide tunables instance read by search.cpp.
inline SearchTunables g_search_tunables{};

/// One UCI spin option backed by a SearchTunables field.
struct TunableSpec {
    std::string_view name;           ///< UCI option name (no spaces)
    int SearchTunables::*field;      ///< member written by setoption
    int default_value;               ///< advertised default
    int min_value;                   ///< advertised and enforced minimum
    int max_value;                   ///< advertised and enforced maximum
};

/// Table of every tunable option, in advertised order.
inline constexpr std::array<TunableSpec, 7> kSearchTunableSpecs = {{
    {"NullMoveReduction", &SearchTunables::null_move_reduction, 2, 1, 6},
    {"NullMoveBigReduction", &SearchTunables::null_move_big_reduction, 3, 1, 8},
    {"ProbCutMargin", &SearchTunables::probcut_margin, 200, 20, 600},
    {"IIRMinDepth", &SearchTunables::iir_min_depth, 4, 2, 12},
    {"SingularMarginPerPly", &SearchTunables::singular_margin_per_ply, 2, 1, 8},
    {"ImprovingFutilityDelta", &SearchTunables::improving_futility_delta, 60, 0, 200},
    {"AspirationInitialDelta", &SearchTunables::aspiration_initial_delta, 25, 5, 100},
}};

/// Sets the tunable named `name` to `value`, clamped to its [min, max].
/// Returns false (changing nothing) when no tunable has that name.
[[nodiscard]] inline bool set_search_tunable(std::string_view name, int value) noexcept {
    for (const TunableSpec& spec : kSearchTunableSpecs) {
        if (spec.name == name) {
            if (value < spec.min_value) {
                value = spec.min_value;
            } else if (value > spec.max_value) {
                value = spec.max_value;
            }
            g_search_tunables.*(spec.field) = value;
            return true;
        }
    }
    return false;
}

/// Restores every tunable to its default (used by tests).
inline void reset_search_tunables() noexcept { g_search_tunables = SearchTunables{}; }

#else

/// True in the tuning build, false otherwise.
inline constexpr bool kSearchTuningBuild = false;

#endif

} // namespace nightwing::search
