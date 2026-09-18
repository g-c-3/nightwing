// src/eval/eval.cpp

#include "eval/eval.h"

#include "board/zobrist.h"
#include "eval/basic_mates.h"
#include "eval/fortress.h"
#include "eval/king_pawn_endgame.h"
#include "eval/king_safety.h"
#include "eval/king_tropism.h"
#include "eval/knight_outposts.h"
#include "eval/material_imbalance.h"
#include "eval/minor_piece_endgame.h"
#include "eval/mobility.h"
#include "eval/pawns.h"
#include "eval/piece_bonuses.h"
#include "eval/psqt.h"
#include "eval/rook_endgame.h"
#include "eval/space.h"
#include "eval/tempo.h"
#include "eval/threats.h"
#include "eval/trapped_pieces.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Lazy evaluation margin (ROADMAP.md's NPS/Raw Speed track, "Lazy
/// evaluation / early-exit on cheap terms" item; eval.h's own doc
/// comment on evaluate()'s `lazy_alpha_white`/`lazy_beta_white`
/// parameters has the full mechanism). A conservative, hand-surveyed
/// upper bound on how far every term BESIDES material+PSQT (pawn
/// structure, mobility, king safety, the bishop-pair/rook-file/7th-rank
/// bonuses, knight outposts, space, threats, king tropism, trapped
/// pieces, tempo, material imbalance, and the four Phase 6 endgame
/// terms, combined) could plausibly move the final tapered score in
/// EITHER direction, surveyed from those terms' own constant tables
/// across eval/*.h — no single realistic position stacks every term's
/// own maximum simultaneously, so this is deliberately generous rather
/// than a tight sum of literal per-term maxima. Like every other
/// pruning-margin constant in this codebase (kFutilityMargins,
/// kRazorMargins, kReverseFutilityMargins — search/search.cpp), this is
/// an untuned starting value, not yet SPRT-validated against an
/// alternative; a real Texel/SPSA-style pass over search margins
/// (rather than just eval weights) would need to sweep this too, same
/// caveat as every sibling margin.
constexpr int kLazyEvalMargin = 650;

/// Computes the current game phase in [0, kMaxPhase] from remaining
/// non-pawn material on the board — the standard CPW "Tapered Eval"
/// technique (see score.h's header comment for the citation); this is
/// a from-scratch implementation of that general approach, not copied
/// code. kMaxPhase (full starting non-pawn material) means "fully
/// middlegame," 0 means "fully endgame" — matching taper()'s own
/// tested convention (score.h/eval_tests.cpp: phase==kMaxPhase selects
/// the mg term exactly, phase==0 selects the eg term exactly), so
/// compute_phase(start_position()) must equal kMaxPhase, not 0. Not in
/// an anonymous namespace (unlike this file's previous state) and
/// declared in eval.h specifically so eval_tests.cpp can pin this
/// exact direction directly, per the bug this same lack of direct
/// testability let through undetected — see docs/DECISIONS.md,
/// 2026-08-29 (2) for the full account.
[[nodiscard]] int compute_phase(const board::Position& pos) noexcept {
    using board::Color;
    using board::PieceType;

    int phase = 0;
    for (Color c : {Color::White, Color::Black}) {
        phase += board::popcount(pos.pieces(c, PieceType::Knight)) * kKnightPhase;
        phase += board::popcount(pos.pieces(c, PieceType::Bishop)) * kBishopPhase;
        phase += board::popcount(pos.pieces(c, PieceType::Rook)) * kRookPhase;
        phase += board::popcount(pos.pieces(c, PieceType::Queen)) * kQueenPhase;
    }
    // Defensive only: a legal position can't exceed starting non-pawn
    // material, but promotions (once search reaches positions with
    // extra queens on the board) could in principle push this above
    // kMaxPhase without this clamp -- an upper-bound clamp now that
    // this function counts UP from 0 as present material, the opposite
    // clamp direction from this function's previous (buggy, subtracted-
    // from-kMaxPhase) form, which could only ever have gone negative,
    // never over kMaxPhase.
    return phase > kMaxPhase ? kMaxPhase : phase;
}

int evaluate(const board::Position& pos, PawnHashTable* pawn_tt, EvalCache* eval_cache,
             const MaterialWeights* material_weights, const PsqtWeights* psqt_weights,
             const Score* incremental_material_psqt, const int* lazy_alpha_white,
             const int* lazy_beta_white) noexcept {
    // Eval cache (eval/eval_cache.h): probed first, keyed on the FULL
    // position (pos.zobrist_hash, already incrementally maintained --
    // no extra hash computation needed, unlike pawn_tt's own
    // board::compute_pawn_hash() below). A hit means this exact
    // position's evaluate() result was already computed -- return it
    // immediately, skipping every term below (including any pawn_tt
    // probe) entirely. See eval_cache.h's header comment for why a real
    // hit rate exists here (transpositions, and the same node's static
    // eval sometimes being requested more than once within a single
    // negamax() call -- search.cpp's razoring/futility pruning).
    //
    // DELIBERATELY SKIPPED WHENEVER EITHER material_weights OR
    // psqt_weights IS SET (this function's own doc comment on both
    // parameters has the full rationale, repeated for each): eval_cache's
    // key says nothing about which weight vector(s) produced a cached
    // result, so honoring it under a different-than-default weight
    // vector could silently return a stale result from a different
    // vector entirely. ALSO deliberately skipped whenever a lazy window
    // (`lazy_alpha_white`/`lazy_beta_white`, this function's own doc
    // comment) is set -- a lazily-approximated early return omits every
    // term besides material+PSQT, and caching it under this position's
    // plain zobrist key would silently hand that approximation to a
    // LATER, non-lazy caller wanting the real, full result.
    const bool lazy_eval_requested = (lazy_alpha_white != nullptr) && (lazy_beta_white != nullptr);
    const bool eval_cache_usable = (eval_cache != nullptr) && (material_weights == nullptr) &&
                                    (psqt_weights == nullptr) && !lazy_eval_requested;
    if (eval_cache_usable) {
        const auto [hit, cached] = eval_cache->probe(pos.zobrist_hash);
        if (hit) {
            return cached;
        }
    }

    // Material+PSQT (eval/incremental.h): the accelerated path is only
    // trusted when neither weights-override parameter is set, for the
    // identical staleness reason eval_cache is skipped under either
    // (this function's own `incremental_material_psqt` doc comment,
    // eval.h, has the full rationale) -- an accumulator maintained
    // under the compiled-in constants says nothing about a tuner's
    // candidate weight vector.
    const bool incremental_usable = (incremental_material_psqt != nullptr) &&
                                     (material_weights == nullptr) && (psqt_weights == nullptr);
    const Score score = incremental_usable
                             ? *incremental_material_psqt
                             : compute_material_psqt(pos, material_weights, psqt_weights);

    // Phase (CPW "Tapered Eval") is computed once here, up front, and
    // reused both by the lazy-window check immediately below and by
    // this function's own final taper() call at the bottom -- a single
    // compute_phase() call either way (this function called it only
    // once before this change too, just at the end instead of here),
    // not a new redundant computation on the non-lazy path.
    const int phase = compute_phase(pos);

    // Lazy evaluation / early-exit on cheap terms (ROADMAP.md's NPS/Raw
    // Speed track; this function's own doc comment on
    // `lazy_alpha_white`/`lazy_beta_white`, eval.h, has the full
    // mechanism and citation). `score` (material+PSQT only, computed
    // above) is tapered on its own and compared against the caller's
    // window widened by kLazyEvalMargin in each direction -- if it
    // already clears the window by more than every remaining term could
    // plausibly swing it, every term below (pawn structure, mobility,
    // king safety, threats, space, ...) is skipped entirely and this
    // partial, approximate value is returned directly.
    if (lazy_eval_requested) {
        const int lazy_score = taper(score, phase);
        if (lazy_score - kLazyEvalMargin > *lazy_beta_white ||
            lazy_score + kLazyEvalMargin < *lazy_alpha_white) {
            return lazy_score;
        }
    }

    Score pawn_score;
    if (pawn_tt == nullptr) {
        pawn_score = pawn_structure_value(pos);
    } else {
        const std::uint64_t pawn_key = board::compute_pawn_hash(pos);
        const auto [hit, cached] = pawn_tt->probe(pawn_key);
        if (hit) {
            pawn_score = cached;
        } else {
            pawn_score = pawn_structure_value(pos);
            pawn_tt->store(pawn_key, pawn_score);
        }
    }

    // Mobility (eval/mobility.h), king safety (eval/king_safety.h), the
    // bishop-pair/rook-file/rook-7th-rank bonuses (eval/piece_bonuses.h),
    // knight outposts (eval/knight_outposts.h), space (eval/space.h),
    // threats (eval/threats.h), king tropism (eval/king_tropism.h),
    // trapped piece penalties (eval/trapped_pieces.h), the material
    // imbalance table (eval/material_imbalance.h), King+pawn endgame
    // theory (eval/king_pawn_endgame.h), rook endgame theory (eval/
    // rook_endgame.h), minor piece endgame theory (eval/
    // minor_piece_endgame.h), fortress detection (eval/fortress.h), and
    // KRK/KBNK basic-mate technique (eval/basic_mates.h) are NOT cached
    // the way pawn structure is: piece placement -- unlike pawn
    // structure -- changes on essentially every move, so a
    // position-keyed cache here would see a near-100% miss rate and
    // just add bookkeeping overhead with no real hit-rate payoff,
    // unlike the pawn hash table's genuinely stable key. The tempo
    // bonus (eval/tempo.h) was never a caching candidate in the first
    // place -- it's already a single field lookup and branch, cheaper
    // than a cache probe would be.
    //
    // king_pawn_endgame_value(), rook_endgame_value(),
    // minor_piece_endgame_value(), AND basic_mate_value() each run
    // classify_endgame() (eval/endgame.h) as their own first, internal
    // check on every single call, including every position that is
    // nowhere near any of the endgames they cover -- four now-redundant
    // calls to the same classifier on every node, not just one, a real,
    // deliberately-accepted per-node cost (a handful of popcount() calls
    // over bitboards this function's own material loop above already
    // touched, not reused between any of the four) rather than an
    // optimization this phase took on. fortress_value() deliberately
    // does NOT call classify_endgame() at all (see eval/fortress.h's
    // own header comment for why) -- it pays its own separate,
    // comparable per-node cost (its own popcount()/bitboard scan) that
    // isn't shared with the other four either. Phase 6 is explicitly
    // the "algorithmic endgame theory" phase, not a performance-tuning
    // one (ARCHITECTURE.md's own Benchmarking Discipline section is a
    // later, Phase 8 concern); revisit if a real bench run shows any of
    // this is a measurable hot-path cost worth short-circuiting (e.g.
    // computing classify_endgame() once here and passing the result to
    // each of its four consumers, and/or gating fortress_value()
    // itself behind a cheap early-out check computed once and shared)
    // -- this becomes more attractive, not less, with each further
    // Phase 6 item that adds its own consumer.
    const int result = taper(score + pawn_score + mobility_value(pos) + king_safety_value(pos) +
                                  piece_bonus_value(pos) + knight_outpost_value(pos) +
                                  space_value(pos) + threats_value(pos) + king_tropism_value(pos) +
                                  trapped_piece_value(pos) + tempo_value(pos) +
                                  material_imbalance_value(pos) + king_pawn_endgame_value(pos) +
                                  rook_endgame_value(pos) + minor_piece_endgame_value(pos) +
                                  fortress_value(pos) + basic_mate_value(pos),
                              phase);

    if (eval_cache_usable) {
        eval_cache->store(pos.zobrist_hash, result);
    }
    return result;
}

} // namespace nightwing::eval
