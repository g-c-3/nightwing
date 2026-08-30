// src/tuner/selfplay.cpp
//
// See selfplay.h.

#include "tuner/selfplay.h"

#include <istream>
#include <iomanip>
#include <ostream>
#include <sstream>

#include "board/board.h"
#include "board/fen.h"
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

/// True if `pos.side_to_move` is currently in check — computed the same
/// way every existing eval/king_safety.cpp-style caller already does
/// (bitscan_forward() on that side's king bitboard, then
/// is_square_attacked() from the opposing side), duplicated locally
/// rather than shared since board/movegen.h doesn't expose an `in_check`
/// convenience wrapper of its own (only the lower-level
/// is_square_attacked() primitive) and this is this file's only use of
/// it.
[[nodiscard]] bool in_check(const Position& pos) noexcept {
    const board::Bitboard king_bb = pos.pieces(pos.side_to_move, PieceType::King);
    const board::Square king_sq = board::bitscan_forward(king_bb);
    return board::is_square_attacked(pos, king_sq, opposite(pos.side_to_move));
}

/// True if `pos` (with `move` about to be played from it) qualifies for
/// training-data sampling — see selfplay.h's header comment on "WHY
/// 'QUIET' POSITIONS" for the full rationale. Deliberately simple/cheap
/// (no quiescence run, no SEE call): not in check, and the move about
/// to be played isn't a capture.
[[nodiscard]] bool is_quiet_position(const Position& pos, const Move& move) noexcept {
    return !in_check(pos) && !move.is_capture();
}

/// Result of playing a game to its natural end (or `max_plies`) —
/// everything play_one_game() needs before it can go back and label
/// every position it collected along the way, since a game's result
/// isn't known until it's over.
struct PlayedGame {
    std::vector<std::string> quiet_fens;
    double result = 0.5;
    int ply_count = 0;
};

/// Plays one game move by move, sampling quiet positions as it goes
/// (selfplay.h's SelfPlayConfig), and determines the final result once
/// the game ends. Split out of play_one_game() as its own function
/// purely for readability — not part of this file's public interface.
[[nodiscard]] PlayedGame play_game_impl(std::uint64_t seed, const SelfPlayConfig& config) {
    support::Xorshift64Star rng(seed);
    Position pos = board::start_position();
    std::vector<std::uint64_t> repetition_history; // this game's own Zobrist history, oldest first

    PlayedGame played;

    for (int ply = 0; ply < config.max_plies; ++ply) {
        MoveList moves;
        board::generate_legal_moves(pos, moves);

        if (moves.size() == 0) {
            // Checkmate or stalemate — see this function's own comment
            // block below on why this is checked before the 50-move/
            // repetition draw conditions, not after: a position with no
            // legal moves is definitionally over regardless of the
            // halfmove clock or repetition count, and in_check() below
            // is only meaningful to call when there's a real question
            // of whose fault it is that the game just ended.
            played.ply_count = ply;
            if (in_check(pos)) {
                // The side to move is checkmated -- the OTHER side won.
                played.result = (pos.side_to_move == Color::White) ? 0.0 : 1.0;
            } else {
                played.result = 0.5; // stalemate
            }
            return played;
        }

        if (pos.halfmove_clock >= 100) {
            played.ply_count = ply;
            played.result = 0.5;
            return played;
        }

        // Threefold repetition: pos.zobrist_hash already encodes piece
        // placement, side to move, castling rights, and en passant
        // target (board/zobrist.cpp's own header comment) -- exactly
        // the FIDE repetition definition -- so counting occurrences of
        // the CURRENT position's hash within this game's own history is
        // sufficient, no separate position-equality check needed.
        int occurrences = 1; // the current position itself
        for (const std::uint64_t past_hash : repetition_history) {
            if (past_hash == pos.zobrist_hash) {
                ++occurrences;
            }
        }
        if (occurrences >= 3) {
            played.ply_count = ply;
            played.result = 0.5;
            return played;
        }

        const bool is_random_opening_ply = ply < config.random_opening_plies;
        Move move;
        if (is_random_opening_ply) {
            const std::size_t index = static_cast<std::size_t>(rng.next() % static_cast<std::uint64_t>(moves.size()));
            move = moves[static_cast<int>(index)];
        } else {
            const search::SearchResult result = search::search_fixed_depth(pos, config.search_depth);
            // A non-null best_move is guaranteed here: moves.size() > 0
            // was already confirmed above, and search_fixed_depth()
            // only returns a null best_move when the root position has
            // no legal moves at all (search/search.h's own
            // SearchResult::best_move doc comment).
            move = result.best_move;
        }

        if (!is_random_opening_ply && is_quiet_position(pos, move)) {
            played.quiet_fens.push_back(board::to_fen(pos));
        }

        repetition_history.push_back(pos.zobrist_hash);
        UndoInfo undo;
        board::make_move(pos, move, undo);
    }

    // Reached max_plies without any other end condition firing -- see
    // selfplay.h's header comment on why this is scored as a draw.
    played.ply_count = config.max_plies;
    played.result = 0.5;
    return played;
}

} // namespace

SelfPlayGame play_one_game(std::uint64_t seed, const SelfPlayConfig& config) {
    const PlayedGame played = play_game_impl(seed, config);

    SelfPlayGame game;
    game.result = played.result;
    game.ply_count = played.ply_count;
    game.positions.reserve(played.quiet_fens.size());
    for (const std::string& fen : played.quiet_fens) {
        game.positions.push_back(SelfPlayPosition{fen, played.result});
    }
    return game;
}

std::vector<SelfPlayGame> play_games(int num_games, std::uint64_t base_seed,
                                      const SelfPlayConfig& config) {
    std::vector<SelfPlayGame> games;
    games.reserve(static_cast<std::size_t>(num_games));
    for (int i = 0; i < num_games; ++i) {
        games.push_back(play_one_game(base_seed + static_cast<std::uint64_t>(i), config));
    }
    return games;
}

void write_training_data(const std::vector<SelfPlayGame>& games, std::ostream& out) {
    out << std::fixed << std::setprecision(1);
    for (const SelfPlayGame& game : games) {
        for (const SelfPlayPosition& position : game.positions) {
            out << position.fen << ';' << position.result << '\n';
        }
    }
}

std::vector<SelfPlayPosition> read_training_data(std::istream& in) {
    std::vector<SelfPlayPosition> positions;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t separator = line.rfind(';');
        if (separator == std::string::npos) {
            continue; // malformed line -- see this function's own doc comment
        }
        const std::string fen = line.substr(0, separator);
        const std::string result_text = line.substr(separator + 1);
        double result = 0.0;
        try {
            std::size_t chars_consumed = 0;
            result = std::stod(result_text, &chars_consumed);
            if (chars_consumed != result_text.size()) {
                continue; // trailing garbage after the number -- treat as malformed
            }
        } catch (const std::exception&) {
            continue; // non-numeric result field -- see this function's own doc comment
        }
        positions.push_back(SelfPlayPosition{fen, result});
    }
    return positions;
}

} // namespace nightwing::tuner
