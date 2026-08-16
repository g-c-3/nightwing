// src/search/quiescence.cpp

#include "search/quiescence.h"

#include <algorithm>

#include "board/movegen.h"
#include "eval/eval.h"
#include "search/ordering.h" // mvv_lva_score() -- reused here for a simple capture-only ordering pass
#include "search/search.h" // kMateScore, kDrawScore
#include "search/see.h"

namespace nightwing::search {
namespace {

using board::Color;
using board::Move;
using board::MoveList;
using board::Position;
using board::UndoInfo;

/// Mirrors search.cpp's own kInfinity of the same value -- a private,
/// module-local constant rather than a shared one; see this file's
/// header comment on why quiescence.cpp/see.cpp/ordering.cpp each own
/// their own small, stable constants like this rather than centralizing
/// them (consistent with how e.g. every test file already owns its own
/// tiny init_all() helper in this codebase, rather than a shared test
/// utility for something this small).
constexpr int kInfinity = 1'000'000;

/// Defensive cap on how many plies deep quiescence's own recursion can
/// go beyond where it was entered, regardless of how the position
/// evolves. Captures alone can never need this (they strictly deplete
/// the board's remaining material each ply, so a pure capture sequence
/// is already bounded at roughly 30 plies by the total non-king piece
/// count) -- this exists specifically to bound the "in check -> full
/// evasion search" path (see quiescence()'s header comment), which
/// isn't self-limiting the same way: a contrived repeating-check
/// pattern could otherwise recurse far deeper than intended, especially
/// since Nightwing doesn't yet have repetition detection wired into
/// search to recognize and cut such a pattern off on its own. 32 plies
/// is comfortably beyond any realistic capture/check-resolution
/// sequence -- this is a rare-pathological-case safety net, not
/// expected to be hit in normal play.
constexpr int kMaxQuiescencePly = 32;

/// Returns true if `pos.side_to_move`'s king is currently attacked.
/// Deliberately duplicated from search.cpp's own private helper of the
/// same behavior (rather than shared) -- small, stable, and this
/// module's only real dependency on it; see this file's header comment.
[[nodiscard]] bool in_check(const Position& pos) noexcept {
    const Color us = pos.side_to_move;
    const board::Square king_sq =
        board::bitscan_forward(pos.pieces(us, board::PieceType::King));
    return board::is_square_attacked(pos, king_sq, board::opposite(us));
}

/// Returns true if making `move` would leave the opponent (whoever is to
/// move after it) in check. Implemented by actually making and unmaking
/// the move rather than a dedicated "does this move give check without
/// playing it" bitboard detector -- a real, standard optimization some
/// engines have, but more machinery than this first quiescence-search
/// pass needs: this is only ever called for non-capture moves, and only
/// at the very first quiescence ply (see quiescence()'s header comment
/// on include_checks), so the cost is bounded and paid rarely, not in
/// quiescence's own hot inner loop.
[[nodiscard]] bool gives_check(Position& pos, Move move) noexcept {
    UndoInfo undo;
    board::make_move(pos, move, undo);
    const bool result = in_check(pos);
    board::unmake_move(pos, move, undo);
    return result;
}

/// Simple capture-only descending-MVV-LVA sort for quiescence's own
/// candidate list -- deliberately not the fuller order_moves() scheme
/// (search/ordering.h): quiescence doesn't thread a TT/KillerTable/
/// HistoryTable through (see this file's header comment on scope), and
/// non-capture check-evasions/checking moves have no meaningful MVV-LVA
/// score anyway (mvv_lva_score() assumes a genuine capture), so they're
/// simply left after every capture, in whatever order movegen produced
/// them, via this being a stable sort.
void order_captures_first(MoveList& moves, const Position& pos) noexcept {
    struct ScoredMove {
        Move move;
        int score;
    };
    const int count = moves.size();
    std::array<ScoredMove, board::kMaxMoves> scored{};
    for (int i = 0; i < count; ++i) {
        const Move m = moves[i];
        scored[static_cast<std::size_t>(i)] = {m, m.is_capture() ? mvv_lva_score(pos, m) : -kInfinity};
    }
    std::stable_sort(scored.begin(), scored.begin() + count,
                      [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
    for (int i = 0; i < count; ++i) {
        moves[i] = scored[static_cast<std::size_t>(i)].move;
    }
}

int quiescence_impl(Position& pos, int alpha, int beta, int ply, std::uint64_t& nodes,
                     bool include_checks, int qs_ply) noexcept {
    ++nodes;

    const bool us_in_check = in_check(pos);

    MoveList legal_moves;
    board::generate_legal_moves(pos, legal_moves);

    if (legal_moves.empty()) {
        // Genuinely terminal (checkmate/stalemate), not just "no noisy
        // moves left" -- matches negamax()'s own terminal handling
        // exactly, including the same ply-adjusted mate score.
        return us_in_check ? -(kMateScore - ply) : kDrawScore;
    }

    if (qs_ply >= kMaxQuiescencePly) {
        // Safety net (see kMaxQuiescencePly's comment) -- return a
        // best-effort static evaluation rather than recursing further,
        // even if `us_in_check` (there's no better cheap alternative at
        // this depth, and this branch is not expected to be reached in
        // normal play).
        const int white_relative = eval::evaluate(pos);
        return pos.side_to_move == Color::White ? white_relative : -white_relative;
    }

    int best;
    if (!us_in_check) {
        // Stand-pat: "doing nothing further" is always an available
        // option when not in check (CPW "Quiescence Search") -- if the
        // position is already good enough to beat beta with no more
        // moves played, or better than anything found so far, that's a
        // real, legitimate baseline score, not a placeholder.
        const int white_relative = eval::evaluate(pos);
        best = pos.side_to_move == Color::White ? white_relative : -white_relative;
        if (best >= beta) {
            return best;
        }
        if (best > alpha) {
            alpha = best;
        }
    } else {
        // In check: there is no stand-pat option -- silence isn't legal
        // when the king is under attack, so `best` starts pessimistic
        // and can only be established by actually trying an evasion
        // below (every one of `legal_moves` becomes a candidate in that
        // case, not just captures -- see the loop below).
        best = -kInfinity;
    }

    MoveList candidates;
    for (int i = 0; i < legal_moves.size(); ++i) {
        const Move move = legal_moves[i];
        if (us_in_check || move.is_capture()) {
            candidates.push_back(move);
        } else if (include_checks && gives_check(pos, move)) {
            candidates.push_back(move);
        }
    }

    if (candidates.empty()) {
        // Not in check (see above -- that branch always fills
        // `candidates`), and no captures or, at the first ply, checking
        // moves either: the position is already quiet, so the stand-pat
        // baseline computed above is the final answer.
        return best;
    }

    order_captures_first(candidates, pos);

    for (int i = 0; i < candidates.size(); ++i) {
        const Move move = candidates[i];

        // SEE pruning (ROADMAP.md: "with SEE pruning"): skip captures
        // that lose material even after all reasonable recaptures --
        // but never when in check (every evasion must be tried
        // regardless of material cost; skipping the only legal escape
        // because it "looks bad" materially would be a correctness bug,
        // not an optimization) and never for a non-capture checking
        // move (SEE only evaluates capture sequences).
        if (!us_in_check && move.is_capture() && static_exchange_evaluation(pos, move) < 0) {
            continue;
        }

        UndoInfo undo;
        board::make_move(pos, move, undo);
        const int score =
            -quiescence_impl(pos, -beta, -alpha, ply + 1, nodes, /*include_checks=*/false, qs_ply + 1);
        board::unmake_move(pos, move, undo);

        if (score > best) {
            best = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    return best;
}

} // namespace

int quiescence(Position& pos, int alpha, int beta, int ply, std::uint64_t& nodes,
               bool include_checks) noexcept {
    return quiescence_impl(pos, alpha, beta, ply, nodes, include_checks, /*qs_ply=*/0);
}

} // namespace nightwing::search
