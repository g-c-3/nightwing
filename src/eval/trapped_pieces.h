#pragma once
// src/eval/trapped_pieces.h
//
// Trapped piece penalties (ROADMAP.md Phase 5's "Trapped piece
// penalties" item): the standard CPW "Trapped Piece" concept
// (https://www.chessprogramming.org/Trapped_Pieces) -- a flat penalty
// for a side's own knight/bishop that has no safe square to move to at
// all. From-scratch implementation here, no code copied, per
// ARCHITECTURE.md's Attribution Policy.
//
// Why only knights and bishops, not rooks/queens: CPW's own "Trapped
// Piece" examples (a bishop shut in a corner by its own and the
// opponent's pawns, e.g. the classic Ruy Lopez Ba4-trapped-by-...b5
// pattern; a knight stranded on the rim) are specifically about minor
// pieces losing ALL mobility to a pawn structure that's closed around
// them. A rook or queen's much longer reach makes a genuine
// zero-safe-squares state vanishingly rare and not a distinct pattern
// worth its own hand-estimated constant at this stage -- matching this
// codebase's established "Knight outposts" precedent (docs/
// DECISIONS.md, 2026-08-27 (2)) of scoping a term to exactly the piece
// types its own name/CPW source describes, rather than silently
// broadening it.
//
// Definition -- "safe mobility": for each own knight/bishop, take its
// pseudo-mobility bitboard (attacks() masked to exclude own-occupied
// squares, the exact same building block eval/mobility.h's
// mobility_value() already computes for a different purpose) and
// additionally exclude every square attacked by an ENEMY PAWN (the
// reverse-pawn-attack trick eval/pawns.cpp's own "Connected" check
// establishes and eval/space.cpp/eval/threats.cpp already reuse). A
// piece is judged trapped when that final count is exactly zero -- no
// square it could reach is both unoccupied by its own side and safe
// from a pawn capture.
//
// Why only pawn attacks are excluded, not a full enemy attacks-by-side
// union (which eval/threats.cpp's own attacks_by_side() already
// computes for a related term): a piece with every reachable square
// covered by some OTHER enemy piece (not a pawn) is an ordinary,
// unremarkable state for an active piece in a contested middlegame --
// treating that as "trapped" would flag large numbers of perfectly
// normal, merely-contested pieces. A pawn-hemmed piece with nowhere
// safe to go at all is the specific, much rarer pattern CPW's own
// examples describe, and is what this term is scoped to.
//
// Why exactly zero rather than a low-but-nonzero threshold (e.g. "at
// most one safe square"): a piece with truly zero safe squares is
// categorically different from one that merely has few options -- it
// cannot move away from a future attack at all without either giving
// up the piece or accepting a capture, a real structural weakness
// rather than a matter of degree. A softer threshold would also need
// its own separately hand-tuned cutoff with no obvious "right" value
// before a real tuner exists; the zero-mobility case needs no such
// tuning to justify -- it's the plain, unambiguous CPW definition.
//
// Same "few hand-estimated constants, formula over a large tuned
// table" preference every other eval/*.h module in this phase already
// establishes -- both constants below are first-draft hand estimates,
// not yet Texel-tuned, same caveat every other eval term added this
// phase already carries.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Flat penalty for a knight/bishop with zero safe squares (see this
/// file's header comment for the exact definition). Bishop penalized
/// more than knight: a trapped bishop is the more classically costly
/// pattern (CPW's own leading example, and the one most likely to
/// actually be lost outright to a slow pawn advance -- a cornered
/// knight, while still a real problem, more often has at least a
/// tactical resource a bishop's fixed diagonal doesn't). `eg` above
/// `mg` for both: this term is the direct deficit-side mirror of
/// eval/mobility.h's own mobility bonus (whose header comment already
/// documents `eg` at or above `mg` because piece activity matters more
/// as material thins) -- a piece with zero mobility is a proportionally
/// larger fraction of a side's remaining force sitting completely idle
/// the fewer pieces are left on the board, and the opponent has more
/// time and a technically simpler task to eventually win it once fewer
/// other threats are competing for attention.
inline constexpr Score kKnightTrappedPenalty = {-40, -55};
inline constexpr Score kBishopTrappedPenalty = {-50, -65};

/// Evaluates the trapped-piece term for BOTH sides and returns a single
/// White-relative Score (positive favors White, matching every other
/// eval/*.h term's sign convention in eval.cpp).
///
/// Precondition: board::init_masks() AND board::init_magic_bitboards()
/// have both been called -- this function computes each knight's/
/// bishop's own attack bitboard (board::knight_attacks()/
/// board::bishop_attacks(), the latter needing magic bitboards) plus
/// board::pawn_attacks() for the reverse-pawn-attack safety check, the
/// same precondition eval/mobility.h's mobility_value() and eval/
/// threats.h's threats_value() already carry.
[[nodiscard]] Score trapped_piece_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
