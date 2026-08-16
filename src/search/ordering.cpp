// src/search/ordering.cpp

#include "search/ordering.h"

#include <algorithm>

#include "eval/psqt.h" // eval::material_value() -- reused for MVV-LVA, see score_move() below

namespace nightwing::search {
namespace {

using board::Color;
using board::Move;
using board::MoveList;
using board::PieceType;
using board::Position;

/// Score bands, highest to lowest, deliberately spaced with generous
/// headroom so no lower band's maximum can ever reach into the band
/// above it (see each band's own comment for its actual achievable
/// range). `score_move()` below assigns exactly one of these (or a
/// HistoryTable score, which is capped below kKiller2Score) to every
/// move.
constexpr int kTTMoveScore = 2'000'000;

/// Captures: kCaptureBase + mvv_lva_score(...). mvv_lva_score's range
/// is roughly [pawn_value*10 - queen_value, queen_value*10 - 0] =
/// [100*10-900, 900*10-0] = [100, 9000] given this engine's current
/// material values (eval/psqt.h) -- comfortably within a band this wide.
constexpr int kCaptureBase = 1'000'000;

/// Non-capture promotions: kPromotionBase + promoted piece's value
/// (320..900 given current material values).
constexpr int kPromotionBase = 900'000;

constexpr int kKiller1Score = 800'000;
constexpr int kKiller2Score = 799'000;

/// MVV-LVA (Most Valuable Victim, Least Valuable Attacker): favors
/// capturing the most valuable piece with the least valuable attacker.
/// `move` must be a genuine capture (is_capture() == true) of `pos`,
/// which must not have had `move` applied yet (victim/attacker are read
/// directly off the board). En passant is special-cased since the
/// captured pawn isn't on `move.to()`, unlike every other capture type.
[[nodiscard]] int mvv_lva_score(const Position& pos, Move move) noexcept {
    const PieceType attacker = board::piece_type_of(pos.piece_at(move.from()));
    const PieceType victim =
        move.is_en_passant() ? PieceType::Pawn : board::piece_type_of(pos.piece_at(move.to()));
    const int attacker_value = eval::material_value(attacker).mg;
    const int victim_value = eval::material_value(victim).mg;
    return victim_value * 10 - attacker_value;
}

[[nodiscard]] int score_move(const Position& pos, Move move, Move tt_move,
                              const KillerTable& killers, int ply,
                              const HistoryTable& history) noexcept {
    if (move == tt_move) {
        return kTTMoveScore;
    }
    if (move.is_capture()) {
        return kCaptureBase + mvv_lva_score(pos, move);
    }
    if (move.is_promotion()) {
        return kPromotionBase + eval::material_value(move.promotion_piece_type()).mg;
    }
    if (move == killers.get(ply, 0)) {
        return kKiller1Score;
    }
    if (move == killers.get(ply, 1)) {
        return kKiller2Score;
    }
    return history.score(pos.side_to_move, move);
}

} // namespace

void KillerTable::update(int ply, Move move) noexcept {
    if (ply < 0 || ply >= kMaxPly) {
        return;
    }
    if (killers_[static_cast<std::size_t>(ply)][0] == move) {
        return; // already the top killer here -- avoid storing a duplicate
    }
    killers_[static_cast<std::size_t>(ply)][1] = killers_[static_cast<std::size_t>(ply)][0];
    killers_[static_cast<std::size_t>(ply)][0] = move;
}

Move KillerTable::get(int ply, int index) const noexcept {
    if (ply < 0 || ply >= kMaxPly || index < 0 || index > 1) {
        return Move();
    }
    return killers_[static_cast<std::size_t>(ply)][static_cast<std::size_t>(index)];
}

void HistoryTable::update(Color color, Move move, int depth) noexcept {
    int& slot = table_[static_cast<std::size_t>(color)][static_cast<std::size_t>(move.from())]
                       [static_cast<std::size_t>(move.to())];
    slot += depth * depth;
    if (slot > kHistoryMax) {
        slot = kHistoryMax;
    }
}

int HistoryTable::score(Color color, Move move) const noexcept {
    return table_[static_cast<std::size_t>(color)][static_cast<std::size_t>(move.from())]
                 [static_cast<std::size_t>(move.to())];
}

void order_moves(MoveList& moves, const Position& pos, Move tt_move, const KillerTable& killers,
                  int ply, const HistoryTable& history) noexcept {
    struct ScoredMove {
        Move move;
        int score;
    };

    const int count = moves.size();
    std::array<ScoredMove, board::kMaxMoves> scored{};
    for (int i = 0; i < count; ++i) {
        scored[static_cast<std::size_t>(i)] = {moves[i],
                                                score_move(pos, moves[i], tt_move, killers, ply, history)};
    }

    // Stable so equal-scored moves (most commonly: untried quiets that
    // all still sit at history score 0) keep their original move-
    // generation order rather than an arbitrary one -- deterministic,
    // reproducible search behavior.
    std::stable_sort(scored.begin(), scored.begin() + count,
                      [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });

    for (int i = 0; i < count; ++i) {
        moves[i] = scored[static_cast<std::size_t>(i)].move;
    }
}

} // namespace nightwing::search
