#pragma once
// src/eval/threats.h
//
// Threats evaluation (ROADMAP.md Phase 5's "Threats evaluation
// (hanging/attacked pieces, pieces attacked by pawns)" item): two
// related but independent penalties applied to a side's own knights,
// bishops, rooks, and queens (pawns and kings are out of scope -- see
// "Why pawns and the king are excluded" below):
//
//   1. Attacked by an enemy pawn: a piece a pawn currently attacks is a
//      serious problem regardless of whether it's defended -- trading a
//      pawn for a minor or major piece is a big material win for the
//      attacker even after any recapture, so this penalty applies
//      unconditionally whenever the attack exists (CPW's own general
//      "Threats" discussion: https://www.chessprogramming.org/Threats
//      covers this as one of the most common and highest-value threat
//      patterns in practice).
//   2. Hanging: a piece attacked by ANY enemy piece (pawn, minor,
//      major, or king) that has NO own defender at all. A simplified,
//      boolean version of the concept -- see "Why a boolean
//      attacked/defended check instead of SEE" below for why this
//      doesn't attempt a full Static-Exchange-Evaluation-accurate
//      "would the exchange actually favor the attacker" judgment.
//
// The two penalties are independent and stack when both apply to the
// same piece (a pawn-attacked AND undefended piece is worse than
// either condition alone, and is scored that way) -- the same
// "independent signals add" philosophy eval/piece_bonuses.h's own
// open-file + 7th-rank stacking and eval/space.h's own
// occupancy + attack stacking already establish for this codebase.
//
// From-scratch implementation here, no code copied, per
// ARCHITECTURE.md's Attribution Policy.
//
// Why pawns and the king are excluded: ROADMAP.md's own item wording
// ("hanging/attacked pieces, pieces attacked by pawns") is read here as
// referring to minor/major pieces specifically, matching how CPW's own
// "Threats" material and most engines' own Threats-style eval terms
// scope this concept -- a hanging PAWN is already substantially covered
// by eval/pawns.cpp's own isolated/backward/doubled-pawn terms and by
// material_value() itself the moment it's actually captured, and a
// "hanging king" isn't a coherent concept in eval at all (an attacked
// king is check, which search handles directly, never an eval-time
// judgment call).
//
// Why a boolean attacked/defended check instead of SEE: search/see.h's
// static_exchange_evaluation() already exists and is more precise (it
// simulates the full capture/recapture sequence rather than a flat
// "any attacker vs. any defender" count), but it operates on a specific
// board::Move (a concrete hypothetical capture), and eval/ has no
// existing dependency on search/ anywhere in this codebase -- adding
// one here to evaluate a piece that isn't actually being captured this
// turn would be a new, and backwards, module dependency (search calls
// eval, not the reverse -- ARCHITECTURE.md's own module layering) for a
// refinement this first cut doesn't need. A simple union-of-attacks
// boolean check needs nothing beyond board/, matching every other
// eval/*.h term added this phase.
//
// Same "few hand-estimated constants" preference every other eval/*.h
// module in this phase already establishes -- every constant below is
// a first-draft hand estimate, not yet Texel-tuned, same caveat every
// other eval term added this phase already carries.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Penalty for a knight/bishop/rook/queen currently attacked by an
/// enemy pawn, applied unconditionally (regardless of whether the
/// piece is otherwise defended -- see this file's header comment for
/// why). Rook/Queen penalties are larger than Knight/Bishop's, matching
/// the intuitive "the more valuable the attacked piece, the more
/// embarrassing and costly it is to have a mere pawn attacking it"
/// scaling. `mg` above `eg` throughout: a pawn threat forces an
/// immediate response (move the piece, defend it, or lose it) most
/// consequentially while there's more actively-contested play still
/// ahead for that tempo loss to cost something real; with fewer pieces
/// left on the board in the endgame, the same tempo hit has less to
/// disrupt.
inline constexpr Score kKnightAttackedByPawnPenalty = {-44, -30};
inline constexpr Score kBishopAttackedByPawnPenalty = {-44, -30};
inline constexpr Score kRookAttackedByPawnPenalty = {-50, -35};
inline constexpr Score kQueenAttackedByPawnPenalty = {-58, -40};

/// Penalty for a knight/bishop/rook/queen attacked by ANY enemy piece
/// (pawn, minor, major, or king) with no own defender anywhere (see
/// this file's header comment's "Why a boolean attacked/defended check
/// instead of SEE" for the exact, deliberately simplified qualification
/// test). Same value-scaling logic as the pawn-attack penalties above
/// (Rook/Queen larger than Knight/Bishop), and still `mg`-heavier for
/// the same underlying reason, though slightly less steeply than the
/// pawn-attack penalties: an outright hanging piece is a real,
/// convertible material loss whenever it's actually found and captured
/// -- a threat that stays somewhat relevant into the endgame too,
/// unlike the pawn-attack penalty's more purely tempo-based mg-leaning
/// rationale above.
inline constexpr Score kKnightHangingPenalty = {-30, -25};
inline constexpr Score kBishopHangingPenalty = {-30, -25};
inline constexpr Score kRookHangingPenalty = {-40, -30};
inline constexpr Score kQueenHangingPenalty = {-50, -35};

/// Evaluates the threats term for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp).
///
/// Precondition: board::init_masks() AND board::init_magic_bitboards()
/// have both been called -- unlike eval/piece_bonuses.h's
/// piece_bonus_value(), eval/knight_outposts.h's
/// knight_outpost_value(), and eval/space.h's space_value() (none of
/// which need magic bitboards), this function computes full attack
/// bitboards for every piece type on the board, including sliding
/// pieces, to determine which squares are attacked/defended -- the same
/// precondition eval/mobility.h's mobility_value() and eval/
/// king_safety.h's king_safety_value() already carry.
[[nodiscard]] Score threats_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
