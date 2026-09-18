#pragma once
// src/board/movegen.h
//
// Fully legal move generation: pins, checks, castling, en passant, and
// promotions are all resolved here, so search code can consume the
// output directly without a separate legality filter (per
// ARCHITECTURE.md: "fully legal move gen ... via pin/check masks, to
// keep search code simple").
//
// Algorithm outline (a from-scratch implementation of well-documented
// bitboard techniques — see the Chess Programming Wiki pages "Checks and
// Pinned Pieces", "Square Attacked By", and "Obstruction Difference" for
// the general public description of the "super-piece"/"sniper" approach
// used below; no code was copied from any engine):
//   1. Find checkers: attackers_to(king_sq) from the side to move's
//      perspective. Zero -> not in check. One -> single check, legal
//      non-king moves are restricted to capturing the checker or
//      blocking the ray between king and checker. Two or more -> only
//      king moves are legal.
//   2. Find pinned pieces: for each enemy slider that would attack the
//      king if own pieces were transparent ("sniper"), check the actual
//      occupancy between king and sniper. Exactly one blocker, and it's
//      our own piece -> that piece is pinned and may only move along the
//      king-sniper line.
//   3. King moves are checked against attackers_to() with the king
//      temporarily removed from occupancy (so a king can't "hide behind
//      itself" when stepping straight back along a checking ray).
//   4. En passant is legality-checked by full occupancy simulation
//      (removing both the moving and captured pawns, adding the mover on
//      the target square) rather than reasoned about via the pin/check
//      masks above, since it's the one move that can create a discovered
//      check not captured by either pin detection or the single-checker
//      block/capture mask (the classic "horizontal en passant pin").

#include "board/board.h"
#include "board/move.h"

namespace nightwing::board {

/// Selects which subset of the fully legal move set generate_legal_moves()
/// produces — the staged-generation split ROADMAP.md's "Staged / lazy
/// move generation" item asks for (CPW "Move Generation" /
/// "MoveGenerator"'s conventional "noisy moves first" split), so a
/// caller (search's future MovePicker) can generate captures, decide
/// whether the search even needs to look at quiets at this node, and
/// only then pay for the quiet-move generation pass.
///
/// The split follows the SAME "forcing/tactical vs. everything else"
/// line search/ordering.h's own move-ordering bands already draw
/// between captures+promotions and killers/quiets (see that file's
/// header comment, bands 2-3 vs. 5-7) — not a plain "MoveFlag::Capture
/// bit set or not" split. A quiet (non-capturing) promotion is
/// tactically forcing in exactly the way a capture is (it changes
/// material outright), so it belongs with `Captures`, not `Quiets`;
/// splitting any other way would let a search that stops after the
/// `Captures` stage (e.g. a capture-only quiescence search) silently
/// miss a legal quiet promotion.
enum class GenType {
    Captures,  ///< Every capture (incl. en passant, capture-promotions)
               ///< AND every promotion, capturing or not. No castling,
               ///< no ordinary quiet move.
    Quiets,    ///< Every remaining legal move: ordinary (non-promoting)
               ///< quiet moves, double pawn pushes, and castling. No
               ///< capture, no promotion of any kind.
    All,       ///< Every legal move — Captures and Quiets combined.
               ///< Identical output to calling both and concatenating,
               ///< just in one pass (this is generate_legal_moves()'s
               ///< pre-existing, unchanged behavior).
};

/// Generates the `gen_type` subset (default: every) fully legal move for
/// `pos.side_to_move` into `moves` (which is cleared first).
/// Preconditions: init_masks() and init_magic_bitboards() have both been
/// called (movegen uses knight/king/pawn attack tables and sliding-piece
/// attacks throughout).
///
/// `generate_legal_moves(pos, captures, GenType::Captures)` followed by
/// `generate_legal_moves(pos, quiets, GenType::Quiets)` produces exactly
/// the same set of moves, with no omissions and no duplicates, as one
/// `generate_legal_moves(pos, moves)` call (GenType::All) — verified by
/// tests/movegen_tests.cpp's staged-generation parity suite, which
/// recursively cross-checks this across every standard perft reference
/// position. All pin/check/legality machinery (compute_pins(),
/// attackers_to(), the single/double-check target mask) runs identically
/// regardless of `gen_type`; only which destination squares are kept for
/// each already-legal move differs.
void generate_legal_moves(const Position& pos, MoveList& moves, GenType gen_type = GenType::All);

/// Returns true if `sq` is attacked by any piece of `by_color`, given
/// board occupancy `occ`. Exposed (not just an internal helper) because
/// search will want it later for king-safety eval and similar queries,
/// and because castling/king-move legality both need to ask this
/// question with the king removed from occupancy for the latter — taking
/// `occ` explicitly rather than always reading `pos.occupied()` lets both
/// call sites reuse one implementation.
[[nodiscard]] bool is_square_attacked(const Position& pos, Square sq, Color by_color,
                                       Bitboard occ) noexcept;

/// Convenience overload using the position's actual current occupancy.
[[nodiscard]] inline bool is_square_attacked(const Position& pos, Square sq,
                                              Color by_color) noexcept {
    return is_square_attacked(pos, sq, by_color, pos.occupied());
}

} // namespace nightwing::board
