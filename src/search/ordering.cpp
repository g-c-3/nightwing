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

/// Captures: kCaptureBase + mvv_lva_score(...) + capture_history.score(...).
/// mvv_lva_score's range is roughly [pawn_value*10 - queen_value,
/// queen_value*10 - 0] = [100*10-900, 900*10-0] = [100, 9000] given this
/// engine's current material values (eval/psqt.h); capture_history's own
/// range is [-8192, 8192] (CaptureHistoryTable::kCaptureHistoryMax,
/// ordering.h). Combined worst/best case is [1,000,000+100-8192,
/// 1,000,000+9000+8192] = [991,908, 1,017,192] -- comfortably clear of
/// both kPromotionBase (900,000) below and kTTMoveScore (2,000,000) above, so neither signal can ever push a capture's score
/// out of this band even at each one's own most extreme value
/// simultaneously.
constexpr int kCaptureBase = 1'000'000;

/// Non-capture promotions: kPromotionBase + promoted piece's value
/// (320..900 given current material values).
constexpr int kPromotionBase = 900'000;

constexpr int kKiller1Score = 800'000;
constexpr int kKiller2Score = 799'000;

/// Tie-break jitter magnitude (order_moves()'s own `tie_break_variant`
/// parameter, ordering.h -- that parameter's own doc comment has the
/// full rationale). +/-16 is comfortably inside HistoryTable's own
/// [-kHistoryMax, kHistoryMax] = [-8192, 8192] range (so it perturbs
/// quiet-move ties without ever itself becoming the dominant signal in
/// a quiet move's own score) and vastly smaller than the >700,000-point
/// gap between kHistoryMax and kKiller2Score above -- no jitter this
/// small can ever push a score across a band boundary.
constexpr int kTieBreakJitterRange = 16;

} // namespace

int mvv_lva_score(const Position& pos, Move move) noexcept {
    const PieceType attacker = board::piece_type_of(pos.piece_at(move.from()));
    const PieceType victim =
        move.is_en_passant() ? PieceType::Pawn : board::piece_type_of(pos.piece_at(move.to()));
    const int attacker_value = eval::material_value(attacker).mg;
    const int victim_value = eval::material_value(victim).mg;
    return victim_value * 10 - attacker_value;
}

namespace {

[[nodiscard]] int score_move(const Position& pos, Move move, Move tt_move,
                              const KillerTable& killers, int ply, const HistoryTable& history,
                              const ContinuationHistoryTable& cont_history,
                              const CaptureHistoryTable& capture_history, PieceType prev_piece,
                              board::Square prev_to) noexcept {
    if (move == tt_move) {
        return kTTMoveScore;
    }
    if (move.is_capture()) {
        // MVV-LVA (the base, dominant signal) plus capture history (a
        // finer-grained tiebreak within/around it -- this file's own
        // CaptureHistoryTable header comment): same "two independent
        // signals, summed" pattern as quiet moves' plain-history-plus-
        // continuation-history just below, applied to captures instead.
        // En passant's attacker/victim types mirror mvv_lva_score()'s
        // own special case just below (both read off the SAME move, so
        // they can never disagree with each other).
        const PieceType attacker = board::piece_type_of(pos.piece_at(move.from()));
        const PieceType victim =
            move.is_en_passant() ? PieceType::Pawn : board::piece_type_of(pos.piece_at(move.to()));
        return kCaptureBase + mvv_lva_score(pos, move) + capture_history.score(attacker, victim);
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
    // Plain history plus continuation history (this file's own header
    // comment): two independent signals about the same quiet move,
    // summed rather than picking one -- score() on either table is 0 if
    // it has nothing recorded (including, for cont_history, whenever
    // prev_piece is PieceType::None -- see ContinuationHistoryTable's
    // own header comment), so this degrades gracefully to plain history
    // alone wherever continuation context isn't available.
    const PieceType piece = board::piece_type_of(pos.piece_at(move.from()));
    return history.score(pos.side_to_move, move) +
           cont_history.score(prev_piece, prev_to, piece, move.to());
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

void HistoryTable::malus(Color color, Move move, int depth) noexcept {
    int& slot = table_[static_cast<std::size_t>(color)][static_cast<std::size_t>(move.from())]
                       [static_cast<std::size_t>(move.to())];
    slot -= depth * depth;
    if (slot < -kHistoryMax) {
        slot = -kHistoryMax;
    }
}

int HistoryTable::score(Color color, Move move) const noexcept {
    return table_[static_cast<std::size_t>(color)][static_cast<std::size_t>(move.from())]
                 [static_cast<std::size_t>(move.to())];
}

void ContinuationHistoryTable::update(PieceType prev_piece, board::Square prev_to, PieceType piece,
                                       board::Square to, int depth) noexcept {
    if (prev_piece == PieceType::None) {
        // No real preceding move to condition on -- see this class's
        // own header comment (ordering.h). Nothing to record.
        return;
    }
    int& slot = table_[static_cast<std::size_t>(prev_piece)][static_cast<std::size_t>(prev_to)]
                       [static_cast<std::size_t>(piece)][static_cast<std::size_t>(to)];
    slot += depth * depth;
    if (slot > kContinuationHistoryMax) {
        slot = kContinuationHistoryMax;
    }
}

void ContinuationHistoryTable::malus(PieceType prev_piece, board::Square prev_to, PieceType piece,
                                      board::Square to, int depth) noexcept {
    if (prev_piece == PieceType::None) {
        // See update()'s own comment just above.
        return;
    }
    int& slot = table_[static_cast<std::size_t>(prev_piece)][static_cast<std::size_t>(prev_to)]
                       [static_cast<std::size_t>(piece)][static_cast<std::size_t>(to)];
    slot -= depth * depth;
    if (slot < -kContinuationHistoryMax) {
        slot = -kContinuationHistoryMax;
    }
}

int ContinuationHistoryTable::score(PieceType prev_piece, board::Square prev_to, PieceType piece,
                                     board::Square to) const noexcept {
    if (prev_piece == PieceType::None) {
        return 0; // See this class's own header comment (ordering.h).
    }
    return table_[static_cast<std::size_t>(prev_piece)][static_cast<std::size_t>(prev_to)]
                 [static_cast<std::size_t>(piece)][static_cast<std::size_t>(to)];
}

void CaptureHistoryTable::update(PieceType attacker, PieceType victim, int depth) noexcept {
    int& slot = table_[static_cast<std::size_t>(attacker)][static_cast<std::size_t>(victim)];
    slot += depth * depth;
    if (slot > kCaptureHistoryMax) {
        slot = kCaptureHistoryMax;
    }
}

void CaptureHistoryTable::malus(PieceType attacker, PieceType victim, int depth) noexcept {
    int& slot = table_[static_cast<std::size_t>(attacker)][static_cast<std::size_t>(victim)];
    slot -= depth * depth;
    if (slot < -kCaptureHistoryMax) {
        slot = -kCaptureHistoryMax;
    }
}

int CaptureHistoryTable::score(PieceType attacker, PieceType victim) const noexcept {
    return table_[static_cast<std::size_t>(attacker)][static_cast<std::size_t>(victim)];
}

void CorrectionHistoryTable::update(Color us, std::uint64_t pawn_key, int error) noexcept {
    const int clamped_error = std::clamp(error, -kCorrectionMax, kCorrectionMax);
    int& slot = table_[static_cast<std::size_t>(us)][pawn_key & (kCorrectionTableSize - 1)];
    slot += (clamped_error - slot) / kCorrectionWeight;
}

int CorrectionHistoryTable::correction(Color us, std::uint64_t pawn_key) const noexcept {
    return table_[static_cast<std::size_t>(us)][pawn_key & (kCorrectionTableSize - 1)];
}

void order_moves(MoveList& moves, const Position& pos, Move tt_move, const KillerTable& killers,
                  int ply, const HistoryTable& history, const ContinuationHistoryTable& cont_history,
                  const CaptureHistoryTable& capture_history, PieceType prev_piece,
                  board::Square prev_to, int tie_break_variant) noexcept {
    struct ScoredMove {
        Move move;
        int score;
    };

    const int count = moves.size();
    std::array<ScoredMove, board::kMaxMoves> scored{};
    for (int i = 0; i < count; ++i) {
        int score = score_move(pos, moves[i], tt_move, killers, ply, history, cont_history,
                                capture_history, prev_piece, prev_to);
        if (tie_break_variant != 0) {
            // Cheap, deterministic per-(move, variant) jitter -- see
            // this function's own header comment (ordering.h) and
            // kTieBreakJitterRange's own comment (above) for why this
            // is safe against ever crossing a score-band boundary.
            // `move.raw()` (board/move.h) is this move's own packed
            // from/to/flag bits -- a cheap, always-available per-move
            // discriminator, multiplied by 2 well-known 32-bit
            // multiplicative-hash constants (Knuth's own, and a common
            // paired constant) then XORed together and reduced modulo
            // the jitter's own range -- not a cryptographic hash, just
            // enough bit-mixing that adjacent moves (which often have
            // adjacent `raw()` values, e.g. same piece stepping one
            // square further) don't jitter in obviously-correlated
            // ways.
            const std::uint32_t h = (static_cast<std::uint32_t>(moves[i].raw()) * 2654435761u) ^
                                     (static_cast<std::uint32_t>(tie_break_variant) * 40503u);
            score += static_cast<int>(h % (2 * kTieBreakJitterRange + 1)) - kTieBreakJitterRange;
        }
        scored[static_cast<std::size_t>(i)] = {moves[i], score};
    }

    // Stable so equal-scored moves (most commonly: untried quiets that
    // all still sit at history score 0, before any jitter) keep their
    // original move-generation order rather than an arbitrary one when
    // `tie_break_variant == 0` -- deterministic, reproducible search
    // behavior for the main thread. A nonzero variant makes genuine
    // ties rare in the first place (the jitter above almost always
    // breaks them), so stability matters less there, but costs nothing
    // to keep either way.
    std::stable_sort(scored.begin(), scored.begin() + count,
                      [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });

    for (int i = 0; i < count; ++i) {
        moves[i] = scored[static_cast<std::size_t>(i)].move;
    }
}

} // namespace nightwing::search
