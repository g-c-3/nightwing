// src/eval/eval.cpp

#include "eval/eval.h"

#include "board/zobrist.h"
#include "eval/king_safety.h"
#include "eval/king_tropism.h"
#include "eval/knight_outposts.h"
#include "eval/mobility.h"
#include "eval/pawns.h"
#include "eval/piece_bonuses.h"
#include "eval/psqt.h"
#include "eval/space.h"
#include "eval/tempo.h"
#include "eval/threats.h"
#include "eval/trapped_pieces.h"
#include "eval/score.h"

namespace nightwing::eval {

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

int evaluate(const board::Position& pos, PawnHashTable* pawn_tt) noexcept {
    using board::Color;
    using board::Piece;
    using board::PieceType;
    using board::Square;

    Score score;

    for (Square sq = 0; sq < board::kNumSquares; ++sq) {
        const Piece piece = pos.piece_at(sq);
        if (piece == Piece::None) {
            continue;
        }

        const PieceType type = board::piece_type_of(piece);
        const Color color = board::color_of(piece);
        const Score term = material_value(type) + psqt_value(piece, sq);

        if (color == Color::White) {
            score += term;
        } else {
            score -= term;
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
    // threats (eval/threats.h), king tropism (eval/king_tropism.h), and
    // trapped piece penalties (eval/trapped_pieces.h) are NOT cached the
    // way pawn structure is: piece placement -- unlike pawn structure --
    // changes on essentially every move, so a position-keyed cache here
    // would see a near-100% miss rate and just add bookkeeping overhead
    // with no real hit-rate payoff, unlike the pawn hash table's
    // genuinely stable key. The tempo bonus (eval/tempo.h) was never a
    // caching candidate in the first place -- it's already a single
    // field lookup and branch, cheaper than a cache probe would be.
    return taper(score + pawn_score + mobility_value(pos) + king_safety_value(pos) +
                     piece_bonus_value(pos) + knight_outpost_value(pos) + space_value(pos) +
                     threats_value(pos) + king_tropism_value(pos) + trapped_piece_value(pos) +
                     tempo_value(pos),
                 compute_phase(pos));
}

} // namespace nightwing::eval
