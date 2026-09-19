#pragma once
// src/eval/eval.h
//
// Top-level static evaluation. Material + tapered piece-square tables
// (eval/psqt.h) plus pawn structure (eval/pawns.h: passed, isolated,
// doubled, backward, connected pawns — ROADMAP.md Phase 5's "Pawn
// structure" item, implemented ahead of Phase 5's other items
// specifically to give ROADMAP.md Phase 3's "Pawn hash table" item real
// values to cache — see docs/DECISIONS.md) plus mobility (eval/
// mobility.h — ROADMAP.md Phase 5's "Mobility eval" item) plus king
// safety (eval/king_safety.h — ROADMAP.md Phase 5's "King safety"
// item) plus bishop pair / rook-on-open-or-semi-open-file / rook-on-
// 7th-rank (eval/piece_bonuses.h — ROADMAP.md Phase 5's "Bishop pair,
// rook on open/semi-open file, rook on 7th rank" item) plus knight
// outposts (eval/knight_outposts.h — ROADMAP.md Phase 5's "Knight
// outposts" item) plus space evaluation (eval/space.h — ROADMAP.md
// Phase 5's "Space evaluation" item) plus threats evaluation (eval/
// threats.h — ROADMAP.md Phase 5's "Threats evaluation" item) plus king
// tropism (eval/king_tropism.h — ROADMAP.md Phase 5's "King tropism"
// item) plus trapped piece penalties (eval/trapped_pieces.h —
// ROADMAP.md Phase 5's "Trapped piece penalties" item) plus a tempo
// bonus (eval/tempo.h — ROADMAP.md Phase 5's "Tempo bonus" item) plus a
// material imbalance table (eval/material_imbalance.h — ROADMAP.md
// Phase 5's "Material imbalance table" item) plus an optional eval
// cache (eval/eval_cache.h — ROADMAP.md Phase 5's "Eval cache
// (optional performance optimization, separate from TT)" item),
// caching evaluate()'s own full result keyed on the full position
// rather than any one term, plus King+pawn endgame theory (eval/
// king_pawn_endgame.h — ROADMAP.md Phase 6's "King+pawn theory" item;
// the first Phase 6 term, applying only to positions eval/endgame.h's
// classify_endgame() recognizes as EndgameSignature::KPK, a no-op
// Score{} everywhere else) plus rook endgame theory (eval/
// rook_endgame.h — ROADMAP.md Phase 6's "Rook endgame patterns" item;
// the second Phase 6 term, applying only to EndgameSignature::
// RookEndgame positions — Tarrasch's Rule generally, Lucena/Philidor
// recognition further narrowed to the single-pawn textbook case; see
// rook_endgame.h's own header comment for why Vancura recognition is
// deliberately not included) plus minor piece endgame theory (eval/
// minor_piece_endgame.h — ROADMAP.md Phase 6's "Minor piece endgames"
// item; the third Phase 6 term, dispatching across THREE buckets —
// EndgameSignature::KBPK, ::OppositeColoredBishops, and
// ::KnightVsBishop — one per clause in that item's own wording) plus
// fortress pattern detection (eval/fortress.h — ROADMAP.md Phase 6's
// "Fortress pattern detection" item; the fourth Phase 6 term, and the
// first that deliberately does NOT consult classify_endgame() at all —
// see fortress.h's own header comment for why a general, cross-
// material-shape structural heuristic doesn't fit that classifier's
// bucket-based approach) plus KRK/KBNK basic-mate technique (eval/
// basic_mates.h — ROADMAP.md Phase 6's final item, "Hand-built base
// heuristics carried over"; the fifth Phase 6 term, covering that
// item's KRK/KBNK clauses specifically — see basic_mates.h's own
// header comment for where that item's KPK and insufficient-material
// clauses are actually handled instead).
//
// compute_phase() (below) was moved out of eval.cpp's anonymous
// namespace and declared here on 2026-08-29 specifically so it can be
// tested directly -- it previously computed the game phase backwards
// (full starting material mapped to phase 0, i.e. taper()'s eg term,
// rather than kMaxPhase/mg as every term's own mg/eg tapering
// direction was designed assuming); see docs/DECISIONS.md, 2026-08-29
// (2) for the full account of the bug, the fix, and why it went
// undetected until now.

#include "board/board.h"
#include "eval/eval_cache.h"
#include "eval/incremental.h"
#include "eval/mobility.h"
#include "eval/pawn_tt.h"
#include "eval/psqt.h"
#include "eval/space.h"

namespace nightwing::eval {

/// Computes the current game phase in [0, kMaxPhase] (eval/score.h)
/// from remaining non-pawn material on the board: kMaxPhase means
/// "full starting non-pawn material still on the board, fully
/// middlegame" (taper() then selects each term's mg value in full);
/// 0 means "no non-pawn material left, fully endgame" (taper() then
/// selects each term's eg value in full). Exposed here (rather than
/// staying an eval.cpp implementation detail) so eval_tests.cpp can
/// pin this exact direction directly -- see this file's own header
/// comment for why that mattered.
[[nodiscard]] int compute_phase(const board::Position& pos) noexcept;

/// Evaluates `pos` and returns a centipawn score from White's
/// perspective: positive means White stands better, negative means
/// Black stands better, 0 is balanced. Search code (Phase 2's alpha-beta,
/// once it exists) is responsible for negating this for Black-to-move
/// nodes if it wants a side-to-move-relative score — evaluate() itself
/// always answers "how good is this position for White," which keeps
/// the function trivially testable independent of whose turn it is.
///
/// Material+PSQT is a full from-scratch 64-square recomputation each
/// call BY DEFAULT (eval/incremental.h's compute_material_psqt()) —
/// see `incremental_material_psqt` below for the opt-in accelerated
/// path search.cpp actually uses, once profiling (docs/DECISIONS.md,
/// 2026-09-16 (2)) confirmed this scan was worth avoiding. Every other
/// term (pawn structure aside, which has its own pawn_tt cache below)
/// is still recomputed fresh on every call regardless — this parameter
/// only ever short-circuits the material+PSQT portion specifically.
///
/// `pawn_tt`, if non-null, is probed/stored around the pawn_structure_value()
/// term specifically (eval/pawns.h) via board::compute_pawn_hash() (board/
/// zobrist.h) as the key — every OTHER term (material, PSQT) is still
/// recomputed fresh every call, since only pawn structure gets its own
/// cache (docs/DECISIONS.md, 2026-08-21 pawn hash table entry). Defaults
/// to nullptr, which just means "always recompute pawn structure" —
/// existing callers/tests are unaffected and still correct, just without
/// the cache's speedup.
///
/// `eval_cache`, if non-null, is probed FIRST, before any of the above:
/// on a hit (keyed on `pos.zobrist_hash`, the FULL position, already
/// incrementally maintained — no extra hash computation needed), this
/// function returns the cached result immediately, skipping every term
/// below entirely, including any `pawn_tt` probe. On a miss, the full
/// computation proceeds exactly as it would with `eval_cache == nullptr`,
/// and the final result is stored into `eval_cache` before returning —
/// see eval/eval_cache.h for the full rationale on why this is a
/// genuinely different, complementary cache to `pawn_tt` (full position
/// vs. pawn-structure-only key) and why a real hit rate exists despite
/// most individual terms changing on nearly every move. Defaults to
/// nullptr, meaning "no eval cache" — existing callers/tests are
/// unaffected and still correct, just without this cache's speedup.
///
/// `material_weights`, if non-null, is forwarded to the internal
/// material_value() call INSTEAD OF its own kPawnValue/.../kQueenValue
/// constants (eval/psqt.h's MaterialWeights, and material_value()'s own
/// doc comment on this parameter) — this is the entry point the not-
/// yet-fully-built gradient-descent tuner (ROADMAP.md Phase 5's Texel/
/// SPSA tuner item; src/tuner/tune.h) uses to compute evaluate() at a
/// candidate weight vector other than the compiled-in defaults, without
/// needing a second copy of this function. `pawn_tt` behaves completely
/// normally when `material_weights` is set (pawn structure doesn't
/// depend on material values at all), but `eval_cache` is DELIBERATELY
/// NEVER consulted at all — probed or stored — whenever
/// `material_weights != nullptr`, even if a real `eval_cache` pointer
/// is also passed: `eval_cache` is keyed purely on `pos.zobrist_hash`,
/// which says nothing about which weight vector produced whatever
/// result might already be cached under that key — honoring it here
/// would silently return a stale result computed under a DIFFERENT
/// weight vector, an eval tuner's worst-case failure mode (confidently
/// wrong tuned values with no obvious symptom; see docs/DECISIONS.md,
/// this parameter's introducing entry). Defaults to nullptr, meaning
/// "use the compiled-in constants" — every existing caller (all of
/// search/eval, every existing test) is entirely unaffected.
///
/// `psqt_weights`, if non-null, is forwarded to the internal
/// psqt_value() call INSTEAD OF its own kXxxMgTable/kXxxEgTable
/// constants (eval/psqt.h's PsqtWeights, and psqt_value()'s own doc
/// comment on this parameter) — the PSQT-side counterpart to
/// `material_weights` above, introduced this session (Tier 0 Step 4,
/// docs/DECISIONS.md, this parameter's own dated entry) specifically so
/// a future PSQT-aware tuning run (ROADMAP.md Tier 0's own multi-step
/// plan) can call evaluate() at a candidate PSQT weight vector the same
/// way `material_weights` already lets it do for material. Independent
/// of `material_weights` — either, both, or neither may be non-null on
/// any given call, e.g. tuning PSQT while material stays at its
/// compiled-in defaults, or vice versa. `eval_cache` is DELIBERATELY
/// NEVER consulted whenever `psqt_weights != nullptr` either, for
/// exactly the same staleness reason `material_weights` already
/// disables it (this doc comment's own paragraph above) — the cache
/// key says nothing about which PSQT weight vector produced a cached
/// result any more than it says which material weight vector did.
/// Defaults to nullptr, meaning "use the compiled-in constants" — every
/// existing caller is entirely unaffected.
///
/// `mobility_weights`, if non-null, is forwarded to the internal
/// mobility_value() call INSTEAD OF its own kKnightMobilityBonus/.../
/// kQueenMobilityBonus constants (eval/mobility.h's MobilityWeights, and
/// mobility_value()'s own doc comment on this parameter) — the
/// mobility-term counterpart to `material_weights`/`psqt_weights` above
/// (ROADMAP.md's Tier 0 tuner item, "PSQT and beyond" — this is the
/// first "beyond" term), so a future mobility-aware tuning run can call
/// evaluate() at a candidate mobility weight vector the same uniform
/// way the other two already allow. Independent of `material_weights`/
/// `psqt_weights` — any subset of the three may be non-null on a given
/// call. `eval_cache` is DELIBERATELY NEVER consulted whenever
/// `mobility_weights != nullptr` either, for the identical staleness
/// reason the other two already disable it. Defaults to nullptr,
/// meaning "use the compiled-in constants" — every existing caller is
/// entirely unaffected.
///
/// `space_weights`, if non-null, is forwarded to the internal
/// space_value() call INSTEAD OF its own kSpaceSquareBonus constant
/// (eval/space.h's SpaceWeights, and space_value()'s own doc comment on
/// this parameter) — the space-term counterpart to `material_weights`/
/// `psqt_weights`/`mobility_weights` above (ROADMAP.md's Tier 0 tuner
/// item, "PSQT and beyond" — the second "beyond" term, after
/// mobility), so a future space-aware tuning run can call evaluate() at
/// a candidate space weight vector the same uniform way the other
/// three already allow. Independent of `material_weights`/
/// `psqt_weights`/`mobility_weights` — any subset of the four may be
/// non-null on a given call. `eval_cache` is DELIBERATELY NEVER
/// consulted whenever `space_weights != nullptr` either, for the
/// identical staleness reason the other three already disable it.
/// Defaults to nullptr, meaning "use the compiled-in constant" — every
/// existing caller is entirely unaffected.
///
/// `incremental_material_psqt`, if non-null, is used INSTEAD OF running
/// compute_material_psqt()'s own 64-square scan (eval/incremental.h) —
/// the caller is asserting that `*incremental_material_psqt` already
/// equals exactly what that scan would compute for `pos` right now.
/// search.cpp is the only real caller: it maintains this value across
/// its own recursion by adding eval::material_psqt_delta() at each
/// move rather than rescanning the board at every node (docs/
/// DECISIONS.md, eval/incremental.h's introducing entry, has the full
/// design rationale, including why this lives in search's own call
/// frames rather than as a board::Position field). DELIBERATELY
/// IGNORED WHENEVER EITHER `material_weights` OR `psqt_weights` IS SET,
/// for the identical staleness reason `eval_cache` is already skipped
/// under either (this doc comment's own paragraphs above): an
/// accumulator maintained under the compiled-in constants says nothing
/// about what a tuner's candidate weight vector would have produced,
/// so honoring it there would silently return a wrong result computed
/// under the wrong weights. Defaults to nullptr, meaning "always run
/// the 64-square scan" — every existing caller (every test, the tuner,
/// any UCI debug tooling) is entirely unaffected.
///
/// `lazy_alpha_white`/`lazy_beta_white` (ROADMAP.md's NPS/Raw Speed
/// track, "Lazy evaluation / early-exit on cheap terms" item): an
/// optional alpha-beta window, in WHITE'S PERSPECTIVE, that this call's
/// result will only ever be compared against for a fail-high/fail-low
/// decision — never trusted as an exact score. Either both must be
/// non-null or both must be left at their nullptr default; passing only
/// one is a caller bug (there is no meaningful "half a window").
/// When set, material+PSQT (`score`, above) is tapered on its own,
/// BEFORE pawn structure or any of the "expensive" terms below are
/// computed at all, and compared against the window widened by
/// `kLazyEvalMargin` (eval.cpp) in each direction: if that cheap,
/// partial value already clears `*lazy_beta_white` (or falls short of
/// `*lazy_alpha_white`) by more than every remaining term could
/// plausibly swing the result, this function returns that cheap,
/// partial tapered value immediately — mobility, king safety, pawn
/// structure, threats, space, and every other still-unevaluated term
/// are never computed at all for this call. This is the classical
/// technique CPW calls "Lazy Evaluation" (https://www.chessprogramming.
/// org/Lazy_Evaluation) — a from-scratch implementation of that public,
/// well-documented idea, not copied code. The early-return value is a
/// deliberately approximate score (it omits every term besides
/// material+PSQT) — safe to use ONLY because the margin guarantees it's
/// still on the correct side of the caller's own window, exactly the
/// same accepted trade-off RFP/razoring/futility already make when they
/// prune based on a shallow, unverified static eval rather than a full
/// search. Because White's perspective is fixed regardless of who's
/// actually to move, a caller with a side-to-move-relative window
/// (search.cpp's/quiescence.cpp's negamax-style `alpha`/`beta`, always
/// true in this codebase today) must convert it exactly the way this
/// function's own callers already convert its RETURN value:
/// `lazy_alpha_white = (side_to_move == White) ? alpha : -beta;`
/// `lazy_beta_white  = (side_to_move == White) ? beta  : -alpha;`
/// — mirroring how negamax()'s own recursive calls already negate and
/// swap alpha/beta for the same reason. `eval_cache` is DELIBERATELY
/// NEVER consulted (probed or stored) whenever this window is set, for
/// the identical staleness reason `material_weights`/`psqt_weights`
/// already disable it above: a lazily-approximated result cached under
/// this position's plain zobrist key would be silently, wrongly
/// returned to a LATER, non-lazy caller wanting the exact full value.
/// `incremental_material_psqt` (below) is fully compatible and
/// orthogonal — it only changes how the cheap `score` this window is
/// checked against gets computed (a fresh scan vs. an already-known
/// value), not whether the window check itself applies. Defaults to
/// nullptr/nullptr, meaning "no lazy window — always compute every
/// term" — every existing caller (every test, the tuner, bench, and
/// every eval::evaluate() call site in search.cpp/quiescence.cpp except
/// quiescence.cpp's own stand-pat call, ROADMAP.md's own item scope for
/// this session) is entirely unaffected.
///
/// Precondition: board::init_masks() AND board::init_magic_bitboards()
/// have both been called. Before eval/mobility.h's mobility_value() term
/// existed, evaluate() only needed init_masks() (material/PSQT/pawn
/// structure never touch a sliding-piece attack table) — every other
/// caller in this codebase already calls both as part of the mandatory
/// startup sequence (ARCHITECTURE.md) regardless, since move generation
/// needs magic bitboards too, so this was never actually reachable as a
/// real bug, but a test calling evaluate() in isolation without both
/// would now silently read uninitialized attack tables.
[[nodiscard]] int evaluate(const board::Position& pos, PawnHashTable* pawn_tt = nullptr,
                            EvalCache* eval_cache = nullptr,
                            const MaterialWeights* material_weights = nullptr,
                            const PsqtWeights* psqt_weights = nullptr,
                            const MobilityWeights* mobility_weights = nullptr,
                            const SpaceWeights* space_weights = nullptr,
                            const Score* incremental_material_psqt = nullptr,
                            const int* lazy_alpha_white = nullptr,
                            const int* lazy_beta_white = nullptr) noexcept;

} // namespace nightwing::eval
