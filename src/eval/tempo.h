#pragma once
// src/eval/tempo.h
//
// Tempo bonus (ROADMAP.md Phase 5's "Tempo bonus" item): the standard
// CPW "Tempo" concept (https://www.chessprogramming.org/Tempo) -- a
// small flat bonus for whichever side has the move. From-scratch
// implementation here, no code copied, per ARCHITECTURE.md's
// Attribution Policy.
//
// Why this term exists at all: having the move is a real, if modest,
// advantage on its own -- it's an extra opportunity to create or answer
// a threat before the opponent gets another chance to. A static
// evaluation that only looks at material and piece placement and
// ignores whose turn it is systematically undervalues the side to
// move; CPW's own article notes this is part of why null-move pruning
// (search/*, Phase 4) works at all -- passing the move away and seeing
// if the position is still fine is only a meaningful test if having
// the move was worth something in the first place.
//
// Why a single flat constant pair rather than anything more elaborate
// (e.g. scaled by material, or by how many pieces are attacked): every
// other term added this phase has its own specific structural pattern
// to detect (a trapped piece, an outpost, a king's exposure); "whose
// turn is it" has no such pattern to detect at all -- it's already
// fully captured by `pos.side_to_move`, so there's nothing to compute
// beyond looking that field up and choosing a sign. Matches this
// codebase's established "start with the simplest defensible version,
// add complexity only once a specific gap is found" preference (most
// recently Trapped piece penalties' own "exactly zero, not a
// threshold" choice, docs/DECISIONS.md 2026-08-29 (1)) taken to its
// logical extreme for a term this structurally simple.
//
// Why `mg` is set above `eg` (the same tapering direction as eval/
// king_safety.h's own king-safety terms, the opposite direction from
// eval/mobility.h's own mobility bonus): initiative -- the practical
// value of getting to act first -- matters most while there are still
// many pieces and live threats on the board for that first move to
// exploit. In the endgame, having the move is a much more mixed
// blessing: zugzwang (CPW: https://www.chessprogramming.org/Zugzwang)
// is common enough there that being FORCED to move can actively hurt
// the side to move rather than help it (the entire reason null-move
// pruning is conventionally disabled in zugzwang-prone endgame
// positions, ARCHITECTURE.md/ROADMAP.md's Phase 4 search work). This
// term does not attempt to detect zugzwang specifically or flip sign
// in that case -- that is a search-level concern (verified-null-move /
// zugzwang detection, a separate, not-yet-reached ROADMAP.md item), not
// something this flat HCE term tries to encode -- but a smaller
// (still positive, never negative) `eg` value is the safe, standard
// way to reflect that the same "having the move" advantage is
// generally worth less, on average, once material is scarce.
//
// Same "few hand-estimated constants, formula over a large tuned
// table" preference every other eval/*.h module in this phase already
// establishes -- the constant below is a first-draft hand estimate,
// not yet Texel-tuned, same caveat every other eval term added this
// phase already carries.

#include "board/board.h"
#include "eval/score.h"

namespace nightwing::eval {

/// Flat bonus for the side to move. `mg` above `eg` for the reason
/// this file's header comment explains (initiative matters more with
/// more pieces/threats on the board; zugzwang makes the endgame case
/// more mixed).
inline constexpr Score kTempoBonus = {20, 10};

/// Evaluates the tempo term and returns a single White-relative Score
/// (positive favors White, matching every other eval/*.h term's sign
/// convention in eval.cpp): +kTempoBonus if White is to move,
/// -kTempoBonus if Black is to move.
///
/// Precondition: none. Unlike every other eval/*.h term added this
/// phase, this function touches only `pos.side_to_move` -- no attack
/// table of any kind is involved, so board::init_masks() and
/// board::init_magic_bitboards() do NOT need to have been called
/// before calling this specific function (though every other term
/// evaluate() also computes still needs both, so this makes no
/// practical difference to any real caller).
[[nodiscard]] Score tempo_value(const board::Position& pos) noexcept;

} // namespace nightwing::eval
