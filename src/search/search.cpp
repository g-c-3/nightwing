// src/search/search.cpp

#include "search/search.h"

#include <cassert>

#include "board/movegen.h"
#include "eval/eval.h"

namespace nightwing::search {
namespace {

using board::Color;
using board::Move;
using board::MoveList;
using board::Position;
using board::UndoInfo;

/// Alpha/beta search bound, kept well below kMateScore's already-huge
/// magnitude so that negating it (`-alpha`, `-beta` on recursive calls,
/// per the negamax convention) never risks integer overflow — a fixed,
/// generous constant rather than std::numeric_limits<int>::max(), whose
/// negation is undefined behavior.
constexpr int kInfinity = 1'000'000;

/// Returns true if `pos.side_to_move`'s king is currently attacked —
/// used to distinguish checkmate from stalemate when
/// generate_legal_moves() returns no moves, since movegen itself
/// doesn't report that distinction directly (see board/movegen.h).
[[nodiscard]] bool in_check(const Position& pos) noexcept {
    const Color us = pos.side_to_move;
    const board::Square king_sq =
        board::bitscan_forward(pos.pieces(us, board::PieceType::King));
    return board::is_square_attacked(pos, king_sq, board::opposite(us));
}

/// Negamax alpha-beta search. Returns a score from `pos.side_to_move`'s
/// perspective (positive = good for the side to move), consistent with
/// eval::evaluate()'s White-perspective output being flipped for Black
/// at the depth-0 base case below.
///
/// `ply` is the number of plies searched so far from the root (0 at the
/// root's immediate children), used only to adjust mate scores so that
/// shorter mates are preferred: a mate delivered `ply` plies from the
/// root scores kMateScore - ply from the mated side's perspective (see
/// the `moves.empty()` branch), so the score shrinks — and therefore
/// looks less attractive to a side searching for the *fastest* mate, or
/// less bad to a side merely trying to survive as long as possible —
/// the deeper the forced mate lies. Standard CPW "Mate Scores"
/// convention; from-scratch implementation here.
int negamax(Position& pos, int depth, int alpha, int beta, int ply, std::uint64_t& nodes) {
    ++nodes;

    if (depth <= 0) {
        const int white_relative_score = eval::evaluate(pos);
        return pos.side_to_move == Color::White ? white_relative_score : -white_relative_score;
    }

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        return in_check(pos) ? -(kMateScore - ply) : kDrawScore;
    }

    int best = -kInfinity;
    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        UndoInfo undo;
        board::make_move(pos, move, undo);
        const int score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, nodes);
        board::unmake_move(pos, move, undo);

        if (score > best) {
            best = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break; // Beta cutoff: the opponent won't let us reach this line.
        }
    }
    return best;
}

} // namespace

SearchResult search_fixed_depth(Position& pos, int depth) {
    assert(depth >= 1 && "search_fixed_depth: depth must be at least 1");

    SearchResult result;

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        // Nothing to play: report the terminal score with a null
        // best_move (Move::is_null()) rather than an arbitrary one.
        result.score = in_check(pos) ? -kMateScore : kDrawScore;
        result.nodes = 1;
        return result;
    }

    int alpha = -kInfinity;
    const int beta = kInfinity;
    Move best_move = moves[0];

    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        UndoInfo undo;
        board::make_move(pos, move, undo);
        const int score = -negamax(pos, depth - 1, -beta, -alpha, 1, result.nodes);
        board::unmake_move(pos, move, undo);

        if (score > alpha) {
            alpha = score;
            best_move = move;
        }
    }

    result.best_move = best_move;
    result.score = alpha;
    return result;
}

} // namespace nightwing::search
