#pragma once
// src/eval/incremental.h
//
// Material+PSQT incremental-accumulator support (ROADMAP.md's NPS/Raw
// Speed track, "Incremental evaluation" item). Profiling (docs/
// DECISIONS.md, 2026-09-16 (2)) confirmed eval::evaluate()'s own
// from-scratch 64-square material+PSQT scan is a real, measurable
// per-node cost (~14% of profiled self-time), called from negamax(),
// quiescence_impl(), AND order_moves() on very nearly every node
// visited. This file provides the two building blocks search.cpp uses
// to avoid repeating that scan at every node: a from-scratch computer
// (the same loop eval.cpp's own evaluate() used to run inline, used
// once per search at the root) and a from-Move delta (used at every
// other node, one arithmetic step instead of a 64-square scan).
//
// Design decision (docs/DECISIONS.md, this file's introducing entry,
// resolving the open sub-decision left by 2026-09-16 (2)): the
// accumulator is deliberately NOT stored as a field on board::Position
// itself (Option 1 of that entry's three sketched options). Position
// already sits exactly at its documented ~3-cache-line static_assert
// ceiling with zero headroom, and — more importantly than the byte
// count — an accumulator field's correctness would only be guaranteed
// for Positions reached via a strict make_move()/unmake_move() stack
// discipline; every OTHER way a Position gets built in this codebase
// (board::Position::place_piece(), board::start_position(),
// board::fen::parse_fen(), and the many tests/tuner/book call sites
// that hand-construct a Position square-by-square) would leave such a
// field silently wrong (stuck at its zero default) with nothing to
// stop a caller from trusting it anyway. Keeping the accumulator
// entirely in search's own call frames instead (an eval::Score value
// threaded alongside `ply`/`nodes` through negamax()'s and
// quiescence()'s own recursion, exactly the way search.cpp already
// threads `prev_piece`/`prev_to` for continuation history) makes an
// invalid accumulator structurally unreachable: nothing outside
// search.cpp's own maintained recursion ever sees one at all. This
// mirrors the precedent already set by eval/eval_cache.h, eval/
// pawn_tt.h, and evaluate()'s own `material_weights`/`psqt_weights`
// parameters -- an optional, pointer-based acceleration with an
// always-correct, unconditional fallback (compute_material_psqt()
// below) rather than a new invariant baked permanently into Position
// that every future Position-mutating code path would have to
// remember to maintain correctly forever.

#include "board/board.h"
#include "board/move.h"
#include "eval/psqt.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Computes the material+PSQT contribution to evaluate()'s own score
/// from scratch: a full 64-square scan, summing material_value() +
/// psqt_value() for every occupied square (White's own pieces add,
/// Black's subtract -- the same White-perspective sign convention
/// evaluate() uses throughout, and the same one material_psqt_delta()
/// below preserves). This is the exact loop eval.cpp's evaluate() used
/// to run inline before this file existed; it's now the fallback path
/// evaluate() uses whenever no incremental accumulator is supplied (see
/// evaluate()'s own `incremental_material_psqt` parameter, eval.h), and
/// the function search.cpp calls exactly once, at the true search
/// root, to seed the accumulator it then maintains via
/// material_psqt_delta() for every descendant node.
///
/// `material_weights`/`psqt_weights` behave exactly as they do in
/// material_value()/psqt_value() themselves (and in evaluate()) --
/// non-null overrides the compiled-in constants, for the not-yet-fully-
/// built gradient-descent tuner. Defaults to nullptr for both, meaning
/// "use the compiled-in constants" -- every caller outside the tuner is
/// unaffected.
[[nodiscard]] Score compute_material_psqt(const board::Position& pos,
                                           const MaterialWeights* material_weights = nullptr,
                                           const PsqtWeights* psqt_weights = nullptr) noexcept;

/// Returns the exact change to compute_material_psqt()'s own value
/// caused by playing `move` for `mover`, given the piece that stood on
/// `move.from()` immediately BEFORE the move (`moved_piece_type` --
/// still a Pawn for a promotion, per board::Position::piece_at()'s own
/// pre-move reading, exactly like the `moved_piece` local variable
/// search.cpp's own move loop already computes for continuation-
/// history purposes) and the `undo` board::make_move() populated for
/// that same move (specifically, `undo.captured_piece`). Deliberately
/// takes no board::Position parameter at all -- every case below
/// (the mover's own square change, a captured piece's removal
/// including the en-passant-specific captured square, and castling's
/// secondary rook relocation) is fully determined by `mover`,
/// `moved_piece_type`, `move`, and `undo` alone, so this function
/// reads no board state and can be called either just before or just
/// after `board::make_move()` actually mutates `pos` -- search.cpp
/// calls it right after, alongside its own other post-make_move()
/// per-move bookkeeping (TT prefetch, `move_gives_check`), purely for
/// locality, not because the ordering matters here.
///
/// Only ever meaningfully used against the compiled-in material/PSQT
/// constants (search.cpp's own accelerated call sites never run under
/// a non-null `material_weights`/`psqt_weights` -- see eval.h's
/// `incremental_material_psqt` doc comment for why those two always
/// disable the incremental path entirely), so unlike
/// compute_material_psqt() above, this function deliberately does NOT
/// take its own weights-override parameters -- there is no real caller
/// that would ever pass anything but the defaults, and adding the
/// parameters anyway would just be dead surface to keep in sync.
[[nodiscard]] Score material_psqt_delta(board::Color mover, board::PieceType moved_piece_type,
                                         board::Move move, const board::UndoInfo& undo) noexcept;

} // namespace nightwing::eval
