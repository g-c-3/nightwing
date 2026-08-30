// src/tuner/match.cpp
//
// See match.h.

#include "tuner/match.h"

#include <cmath>

#include "board/board.h"
#include "board/move.h"
#include "board/movegen.h"
#include "search/search.h"
#include "support/rng.h"

namespace nightwing::tuner {
namespace {

using board::Color;
using board::Move;
using board::MoveList;
using board::opposite;
using board::PieceType;
using board::Position;
using board::UndoInfo;

/// Same helper as tuner/selfplay.cpp's own in_check() — duplicated
/// locally rather than shared for the same reason selfplay.cpp's own
/// copy gives (board/movegen.h exposes no in_check() convenience
/// wrapper of its own, only the lower-level is_square_attacked()
/// primitive), and this file has no dependency on selfplay.h at all
/// otherwise (this file's own header comment on why match.cpp and
/// selfplay.cpp are deliberately separate, not sharing a game loop).
[[nodiscard]] bool in_check(const Position& pos) noexcept {
    const board::Bitboard king_bb = pos.pieces(pos.side_to_move, PieceType::King);
    const board::Square king_sq = board::bitscan_forward(king_bb);
    return board::is_square_attacked(pos, king_sq, opposite(pos.side_to_move));
}

/// Plays one game with `weights_white` searching for White and
/// `weights_black` searching for Black, returning the result from
/// White's perspective (1.0 White won, 0.5 draw, 0.0 Black won) — same
/// convention as tuner::SelfPlayPosition::result (selfplay.h), and the
/// same four end conditions/max_plies-safety-net structure as
/// selfplay.cpp's own play_game_impl() (this file's header comment on
/// why the two aren't shared code, despite the structural similarity).
[[nodiscard]] double play_one_match_game(const eval::MaterialWeights& weights_white,
                                          const eval::MaterialWeights& weights_black,
                                          std::uint64_t seed, const MatchConfig& config) {
    support::Xorshift64Star rng(seed);
    Position pos = board::start_position();
    std::vector<std::uint64_t> repetition_history;

    for (int ply = 0; ply < config.max_plies; ++ply) {
        MoveList moves;
        board::generate_legal_moves(pos, moves);

        if (moves.size() == 0) {
            if (in_check(pos)) {
                return (pos.side_to_move == Color::White) ? 0.0 : 1.0;
            }
            return 0.5; // stalemate
        }

        if (pos.halfmove_clock >= 100) {
            return 0.5;
        }

        int occurrences = 1;
        for (const std::uint64_t past_hash : repetition_history) {
            if (past_hash == pos.zobrist_hash) {
                ++occurrences;
            }
        }
        if (occurrences >= 3) {
            return 0.5;
        }

        Move move;
        if (ply < config.random_opening_plies) {
            const std::size_t index =
                static_cast<std::size_t>(rng.next() % static_cast<std::uint64_t>(moves.size()));
            move = moves[static_cast<int>(index)];
        } else {
            const eval::MaterialWeights& weights_to_move =
                (pos.side_to_move == Color::White) ? weights_white : weights_black;
            const search::SearchResult result =
                search::search_fixed_depth(pos, config.search_depth, {}, &weights_to_move);
            // Guaranteed non-null: moves.size() > 0 was already
            // confirmed above (same reasoning as selfplay.cpp's own
            // play_game_impl()).
            move = result.best_move;
        }

        repetition_history.push_back(pos.zobrist_hash);
        UndoInfo undo;
        board::make_move(pos, move, undo);
    }

    return 0.5; // max_plies safety net (this file's/selfplay.h's header comment)
}

} // namespace

double MatchResult::score_a() const noexcept {
    if (games_played == 0) {
        return 0.5;
    }
    return (static_cast<double>(wins_a) + 0.5 * static_cast<double>(draws)) /
           static_cast<double>(games_played);
}

double MatchResult::elo_diff() const noexcept {
    // Clamp strictly inside (0, 1) before the log10 -- see this
    // function's own doc comment (match.h) on why the exact endpoints
    // are undefined and why a small clamp, not a special-cased
    // infinity, is the right way to keep this always returning a finite
    // number.
    constexpr double kEpsilon = 1e-4;
    double score = score_a();
    if (score < kEpsilon) {
        score = kEpsilon;
    } else if (score > 1.0 - kEpsilon) {
        score = 1.0 - kEpsilon;
    }
    return 400.0 * std::log10(score / (1.0 - score));
}

MatchResult play_match(const eval::MaterialWeights& weights_a, const eval::MaterialWeights& weights_b,
                        std::uint64_t base_seed, const MatchConfig& config) {
    MatchResult result;

    for (int i = 0; i < config.num_games; ++i) {
        // Alternate colors every game (this file's/match.h's header
        // comment on why) -- even game index: A plays White. Odd: B
        // plays White.
        const bool a_plays_white = (i % 2 == 0);
        const eval::MaterialWeights& weights_white = a_plays_white ? weights_a : weights_b;
        const eval::MaterialWeights& weights_black = a_plays_white ? weights_b : weights_a;

        const double result_white_perspective =
            play_one_match_game(weights_white, weights_black, base_seed + static_cast<std::uint64_t>(i),
                                 config);

        // Translate the game's White-perspective result into an A-vs-B
        // outcome, accounting for which side A actually played this
        // game.
        const double result_a_perspective =
            a_plays_white ? result_white_perspective : (1.0 - result_white_perspective);

        if (result_a_perspective == 1.0) {
            ++result.wins_a;
        } else if (result_a_perspective == 0.0) {
            ++result.wins_b;
        } else {
            ++result.draws;
        }
        ++result.games_played;
    }

    return result;
}

} // namespace nightwing::tuner
