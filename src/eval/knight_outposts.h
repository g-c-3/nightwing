#pragma once
// src/eval/knight_outposts.h
//
// Knight outposts (ROADMAP.md Phase 5's "Knight outposts" item): a
// bonus for a knight standing on an "outpost" square -- the standard
// CPW "Outpost" concept (https://www.chessprogramming.org/Outposts):
// a square, in or near enemy territory, defended by a friendly pawn,
// that no enemy pawn can ever attack (neither adjacent file has an
// enemy pawn still able to advance far enough to challenge it). Such a
// knight can't be dislodged by a pawn and is a persistent thorn in the
// opponent's position. From-scratch implementation here, no code
// copied, per ARCHITECTURE.md's Attribution Policy.
//
// Only knights are scored -- unlike some engines' own broader "minor
// piece outpost" bonus that also credits bishops, this is scoped
// exactly to ROADMAP.md's own item name, matching this codebase's
// general practice of implementing exactly what's on the roadmap line
// rather than silently expanding scope; a bishop-outpost bonus, if ever
// wanted, is a separate future roadmap addition, not folded in here.
//
// Same "few hand-estimated constants, not a large tuned table"
// preference every other eval/*.h module in this phase already
// establishes -- kKnightOutpostBonus below is a first-draft hand
// estimate, not yet Texel-tuned, same caveat every other eval term
// added this phase already carries.
//
// Deliberately its own translation unit, same one-clearly-scoped-term-
// per-file organizational convention eval/mobility.h and eval/
// king_safety.h both already establish (unlike eval/piece_bonuses.h's
// own three-terms-in-one-file grouping, which was specifically because
// each of ITS three terms was individually too small to justify a
// dedicated file -- see docs/DECISIONS.md, 2026-08-27 (1). Knight
// outposts' own detection logic -- a defended-square check plus a
// no-enemy-pawn-can-ever-attack-it span check -- is comparable in size
// to Mobility's or King safety's own single term, so it gets the same
// standalone treatment those two already have).

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Flat bonus for each own knight standing on a qualifying outpost
/// square (see this file's header comment for the exact definition:
/// pawn-defended, never attackable by an enemy pawn, and within the
/// relative-rank window checked by knight_outposts.cpp's own
/// is_outpost_rank() -- the standard "outposts only matter in or near
/// enemy territory" restriction, since a knight parked on its own side
/// of the board isn't doing anything an outpost bonus should reward).
/// `mg` above `eg`: an outpost knight's real value is the strategic
/// grip it gives over squares and lines while more material remains on
/// the board for that grip to matter against -- other pieces to
/// support, enemy pieces it cramps, attacks it can anchor. This is
/// deliberately the opposite taper direction from eval/mobility.h's own
/// constants (which grow relatively more valuable as material thins):
/// mobility is about raw square-count reach, which genuinely increases
/// as the board opens up, while an outpost's value is about
/// battlefield leverage, which has fewer other pieces to leverage
/// against by the endgame -- closer in spirit to eval/king_safety.h's
/// own mg-heavy reasoning than to mobility's eg-heavy one, though for a
/// different underlying cause (leverage over the position, not
/// exposure/danger).
inline constexpr Score kKnightOutpostBonus = {18, 10};

/// Evaluates the knight-outpost bonus for BOTH sides and returns a
/// single White-relative Score (positive favors White, matching every
/// other eval/*.h term's sign convention in eval.cpp).
///
/// Precondition: board::init_masks() has been called (this function
/// uses board::pawn_attacks() and board::passed_pawn_mask(), both
/// masks.h functions). Like eval/piece_bonuses.h's piece_bonus_value()
/// and unlike eval/mobility.h's mobility_value()/eval/king_safety.h's
/// king_safety_value(), this function does NOT need
/// board::init_magic_bitboards() -- it never calls a sliding-piece
/// attack function.
[[nodiscard]] Score knight_outpost_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
