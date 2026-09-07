#pragma once
// src/search/skill.h
//
// Skill Level / strength limiting (ROADMAP.md Phase 8, "Skill level /
// strength limiting (optional, for practice/handicap play)").
//
// WHAT THIS DOES, AND DELIBERATELY DOESN'T DO: this module never
// weakens the SEARCH itself — no reduced depth, no shortened time
// budget, no simplified eval. A limited-skill search still runs at
// exactly the same strength as an unlimited one, and every `info depth
// ... score ... pv ...` line a GUI sees during that search is genuine,
// full-strength analysis (src/uci/uci.cpp's emit_info(), completely
// unmodified by this feature). All this module does is change WHICH of
// several already-genuinely-computed candidate moves (search.h's own
// MultiPV machinery, ROADMAP.md Phase 8's own "MultiPV" sub-item)
// actually gets reported as `bestmove` — deliberately picking a
// plausible-but-not-always-optimal move more often as the configured
// skill level drops, rather than the engine's own best-known move every
// time. This mirrors the general SHAPE of Stockfish's own long-standing
// classical "Skill Level" UCI option (0-20, 20 = full strength/
// disabled) — search full-strength with several MultiPV lines, then
// perturb the FINAL move choice rather than the search — but the actual
// formula and constants below are this project's own from-scratch
// design, not copied from Stockfish's own (GPL-licensed) source: no
// code from that project was read or reused, only the general public
// concept, credited here per ARCHITECTURE.md's own Attribution Policy.
//
// No UCI_Elo/UCI_LimitStrength-style option is offered alongside this
// one (some engines, including Stockfish, expose both): mapping a
// target Elo rating onto an equivalent skill level would require this
// project's own actual playing strength at each level to have been
// independently measured against a real rating pool, which it hasn't
// (docs/DECISIONS.md's own established "don't overclaim strength that
// hasn't actually been measured" convention — see, e.g., the `bench`
// item's own honest "not verified against real fishtest/OpenBench
// infrastructure" caveat). The plain 0-20 `Skill Level` dial makes no
// claim about what any specific level corresponds to in real-world
// Elo terms — it is what it is: a knob from "full strength" (20) down
// to "considerably weaker" (0), for practice/handicap play.
//
// From-scratch implementation.

#include <cstdint>
#include <random>
#include <vector>

#include "board/move.h"

namespace nightwing::search {

struct SearchResult; // search.h — forward-declared to avoid a circular include
                      // (search.h doesn't need to know skill.h exists at all).

/// The `Skill Level` UCI option's own range. `kMaxSkillLevel` (20,
/// matching Stockfish's own classic convention for this exact number,
/// a deliberate choice for familiarity to anyone who has used another
/// engine's own Skill Level option before) means "no limiting at all —
/// full strength," and is this project's own default (src/uci/uci.cpp)
/// — every session before this feature existed effectively always ran
/// at this value, so nothing changes for anyone who never touches this
/// option. `kMinSkillLevel` (0) is the weakest configurable setting.
inline constexpr int kMinSkillLevel = 0;
inline constexpr int kMaxSkillLevel = 20;

/// How many simultaneous MultiPV lines a skill-limited search asks for
/// internally (skill_search_multipv() below), when the caller's own
/// requested MultiPV count (the ordinary `MultiPV` UCI option,
/// independent of this feature) is smaller. Deliberately a fixed
/// compile-time constant rather than itself scaling with skill level —
/// more lines than this would let pick_skill_move() (below) consider
/// choices the noise amplitude at any configured level would already
/// be very unlikely to ever prefer over the top handful anyway (see
/// that function's own comment on why the noise amplitude is capped at
/// kSkillNoiseCapCp), for real added search cost (search.cpp's own
/// MultiPV implementation re-searches the root once per requested line
/// with prior lines' own best moves excluded).
inline constexpr int kSkillSearchMultiPv = 8;

/// The maximum centipawn magnitude of the random perturbation
/// pick_skill_move() (below) ever adds to a candidate line's own score
/// before comparing lines against each other — one pawn's worth
/// (matching this project's own eval::MaterialWeights::pawn_mg default,
/// eval/psqt.h), at the weakest configured skill level (kMinSkillLevel).
/// This is what keeps even the weakest setting from ever discarding a
/// forced mate or a large, clear material win in favor of a badly worse
/// line — a genuinely weak human player still usually recognizes an
/// unmissable tactic even while playing an otherwise much weaker game
/// overall, and this cap reproduces that same shape: only choices that
/// are ALREADY close to each other in evaluation (within about a pawn)
/// become genuinely up for grabs, regardless of skill level.
inline constexpr int kSkillNoiseCapCp = 100;

/// True whenever `skill_level` (any value — not assumed pre-clamped)
/// asks for any limiting at all, i.e. is strictly below
/// kMaxSkillLevel. A value below kMinSkillLevel or above kMaxSkillLevel
/// is still handled correctly by both this function and
/// pick_skill_move()/skill_search_multipv() below (both clamp
/// internally) — this function's own boolean result for such an
/// out-of-range value is still exactly what its own doc comment says:
/// "does this ask for limiting."
[[nodiscard]] constexpr bool is_skill_limited(int skill_level) noexcept {
    return skill_level < kMaxSkillLevel;
}

/// How many MultiPV lines a `go` call should actually request from
/// search::search_iterative_deepening() given the currently configured
/// `skill_level` and the caller's own, independently configured
/// `requested_multi_pv` (the ordinary `MultiPV` UCI option's current
/// value — this feature never reduces it, only potentially raises it
/// for the search call itself). Returns `requested_multi_pv` UNCHANGED
/// whenever `skill_level` doesn't ask for any limiting at all
/// (is_skill_limited() above is false) — a caller at the default skill
/// level sees zero behavioral difference from before this feature
/// existed, including zero difference in how many lines get computed
/// or reported. When limiting IS active, returns
/// `max(requested_multi_pv, kSkillSearchMultiPv)` — enough lines for
/// pick_skill_move() (below) to have a genuine choice, while never
/// reporting FEWER `info multipv N` lines to the GUI than the user's
/// own `MultiPV` setting already asked for.
[[nodiscard]] int skill_search_multipv(int skill_level, int requested_multi_pv) noexcept;

/// Picks which of `multipv_lines`' own already-fully-searched candidate
/// moves to actually report as `bestmove`, given the configured
/// `skill_level`. `multipv_lines` is expected to be ranked best-first
/// (search::SearchResult::multipv_lines' own documented convention,
/// search.h) — index 0 is the engine's own single best-known move.
/// `rng` is the caller-owned source of randomness (src/uci/uci.cpp owns
/// one real generator, seeded once per session from a genuine entropy
/// source, and reuses it move after move — the same "a real generator
/// advances across real moves in a real game, a test injects its own
/// fixed-seed generator for reproducibility" split this project's own
/// tuner::selfplay/tuner::match modules already use for their own
/// seeding, src/tuner/selfplay.h/match.h).
///
/// At `skill_level >= kMaxSkillLevel` (no limiting requested) OR when
/// `multipv_lines` has fewer than 2 entries (nothing to choose between
/// regardless of skill level), this function returns
/// `multipv_lines.front().best_move` WITHOUT drawing anything from
/// `rng` at all — not merely "usually returns the same move," but
/// literally zero calls into `rng`, so a caller at the default skill
/// level (or facing a position with only one genuinely distinct
/// MultiPV line — an only-legal-move position, most commonly) sees
/// precisely zero behavioral change, in either its move choice or its
/// RNG's own subsequent state, from before this feature existed.
///
/// Otherwise, computes a "weakness" fraction in [0, 1] from how far
/// `skill_level` (clamped into [kMinSkillLevel, kMaxSkillLevel]) sits
/// below kMaxSkillLevel, scales kSkillNoiseCapCp by it to get this
/// call's own noise amplitude, and picks whichever line maximizes
/// (that line's own score + an independent uniform random integer in
/// [-amplitude, +amplitude]) — see kSkillNoiseCapCp's own doc comment
/// above for why a large amplitude still can't flip a genuinely
/// lopsided choice (a forced mate vs. anything else, a large material
/// win vs. a much worse alternative), only a genuinely close one.
/// Returns `multipv_lines.front().best_move` if, degenerately,
/// `multipv_lines` is empty (defensive only — every real caller already
/// guarantees at least one entry whenever it calls this function at
/// all).
[[nodiscard]] board::Move pick_skill_move(const std::vector<SearchResult>& multipv_lines,
                                           int skill_level, std::mt19937_64& rng);

} // namespace nightwing::search
