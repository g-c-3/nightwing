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
// rather than any one term. The rest of Phase 5's terms land
// incrementally after this.
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
#include "eval/pawn_tt.h"

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
/// Currently a full from-scratch recomputation each call (scans all 64
/// squares) rather than an incremental accumulator updated on
/// make/unmake — ARCHITECTURE.md's "eval on the fly" accumulator
/// pattern is real but deliberately deferred; see DECISIONS.md for why
/// this is the right tradeoff for Phase 2's "get something playing"
/// goal specifically, and revisit once eval has enough terms and a
/// profiled hot path to justify the accumulator's extra bookkeeping.
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
                            EvalCache* eval_cache = nullptr) noexcept;

} // namespace nightwing::eval
