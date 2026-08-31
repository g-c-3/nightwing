// src/eval/rook_endgame.cpp
//
// See rook_endgame.h for the overall scope (including why Vancura
// position recognition is deliberately not attempted here) and
// docs/DECISIONS.md for the full rationale on that scope limit. Every
// constant this file applies is declared in rook_endgame.h (matching
// eval/king_pawn_endgame.h's own established convention, so tests and
// the eventual Texel tuner can both reach them by name) and is a
// first-draft hand estimate, not yet tuned -- same caveat every other
// eval term in this codebase already carries.
//
// Unlike eval/king_pawn_endgame.cpp's own KPK term, a RookEndgame
// position's game phase is NOT always 0 -- both rooks are real,
// phase-contributing material (eval.h/eval.cpp's compute_phase()), so
// eval::taper() genuinely blends `mg` and `eg` here rather than always
// selecting `eg` alone the way it does for a bare-kings KPK position.
// Every constant below still sets `mg` and `eg` to different, hand-
// picked values reflecting that a rook endgame's tactical/technique
// content (the actual bridge-building or cutting-off maneuver) matters
// more as the position simplifies further toward the ending proper --
// see each constant's own comment in rook_endgame.h.

#include "eval/rook_endgame.h"

#include "board/masks.h"
#include "eval/endgame.h"

namespace nightwing::eval {

namespace {

using board::Bitboard;
using board::Color;
using board::PieceType;
using board::Position;
using board::Square;

/// Relative rank of `sq` from `c`'s own perspective: 0 = `c`'s own back
/// rank, 7 = the opposite back rank (where `c` promotes). Re-derived
/// locally rather than shared -- matches this codebase's established
/// per-file convention for this exact computation (eval/pawns.cpp's
/// and eval/king_pawn_endgame.cpp's own identical local helpers).
[[nodiscard]] constexpr int relative_rank(Color c, Square sq) noexcept {
    const int rank = board::rank_of(sq);
    return c == Color::White ? rank : 7 - rank;
}

/// Returns true if a rook on `rook_sq` stands "behind" a pawn of color
/// `pawn_color` on `pawn_sq`, per Tarrasch's Rule: same file, and on
/// the side of the pawn AWAY from that pawn's own promotion square --
/// regardless of which side owns the rook. This single definition
/// covers both halves of the rule (support a friendly passed pawn from
/// behind; restrain an enemy passed pawn from behind) identically,
/// since "behind" is a property of the pawn's own direction of travel,
/// not of who the rook belongs to.
[[nodiscard]] constexpr bool is_rook_behind_pawn(Square rook_sq, Square pawn_sq,
                                                  Color pawn_color) noexcept {
    if (board::file_of(rook_sq) != board::file_of(pawn_sq)) {
        return false;
    }
    return (pawn_color == Color::White) ? (board::rank_of(rook_sq) < board::rank_of(pawn_sq))
                                         : (board::rank_of(rook_sq) > board::rank_of(pawn_sq));
}

/// Tarrasch's Rule (CPW "Rook Endings"): for each side's single rook,
/// a bonus for standing behind any passed pawn on the board (own or
/// enemy) -- applies across ANY pawn count, unlike the Lucena/Philidor
/// checks below, since it's a per-pawn geometric check rather than a
/// whole-position pattern match. Returns a White-relative Score.
[[nodiscard]] Score tarrasch_rule_value(const Position& pos) noexcept {
    Score score;

    for (const Color c : {Color::White, Color::Black}) {
        const Color them = board::opposite(c);
        const Bitboard rook_bb = pos.pieces(c, PieceType::Rook);
        if (rook_bb == 0) {
            // Defensive only -- EndgameSignature::RookEndgame guarantees
            // exactly one rook per side; a hand-built test position
            // could still omit one.
            continue;
        }
        const Square rook_sq = board::bitscan_forward(rook_bb);

        const Bitboard own_pawns = pos.pieces(c, PieceType::Pawn);
        const Bitboard enemy_pawns = pos.pieces(them, PieceType::Pawn);

        Score side_score;

        Bitboard bb = own_pawns;
        while (bb != 0) {
            const Square sq = board::pop_lsb(bb);
            const bool passed = (enemy_pawns & board::passed_pawn_mask(c, sq)) == 0;
            if (passed && is_rook_behind_pawn(rook_sq, sq, c)) {
                side_score += kRookBehindOwnPassedPawnBonus;
            }
        }

        bb = enemy_pawns;
        while (bb != 0) {
            const Square sq = board::pop_lsb(bb);
            const bool passed = (own_pawns & board::passed_pawn_mask(them, sq)) == 0;
            if (passed && is_rook_behind_pawn(rook_sq, sq, them)) {
                side_score += kRookBehindEnemyPassedPawnBonus;
            }
        }

        if (c == Color::White) {
            score += side_score;
        } else {
            score -= side_score;
        }
    }

    return score;
}

/// Lucena and Philidor recognition BOTH narrow down to the single-pawn
/// "textbook" rook + pawn vs. rook scenario (see rook_endgame.h's own
/// header comment for why) -- returns Score{} immediately if the board
/// doesn't have exactly one pawn total, or if neither pattern's own
/// geometric criteria match. Returns a value relative to the ATTACKER
/// (the side with the pawn) -- positive is good for the attacker,
/// negative is bad -- NOT yet White-relative; the caller applies the
/// final color sign-flip, matching eval/king_pawn_endgame.cpp's own
/// established convention for the identical situation.
[[nodiscard]] Score lucena_and_philidor_value(Color attacker, Square pawn_sq,
                                               Square attacker_king_sq, Square defender_king_sq,
                                               Square defender_rook_sq) noexcept {
    const int pawn_file = board::file_of(pawn_sq);
    const bool rook_pawn = (pawn_file == 0 || pawn_file == 7);
    const int rel_rank = relative_rank(attacker, pawn_sq);
    const Square promotion_sq =
        board::make_square(pawn_file, (attacker == Color::White) ? 7 : 0);

    // Lucena position (CPW "Lucena Position"): the pawn is one push
    // from promoting, the attacking king is already right beside the
    // promotion square (in position to help build the bridge), and the
    // defending king is too far away to interfere -- the classical
    // "known win" recognition. Deliberately excludes rook pawns, the
    // one file Lucena's own technique doesn't cleanly apply to (same
    // exclusion eval/king_pawn_endgame.cpp's own rule-of-the-square
    // handling makes for a different reason). The defending rook's
    // own exact square is deliberately NOT checked here -- the
    // king+pawn geometry alone is what actually distinguishes a "won"
    // position from a merely "probably favorable" one for this
    // simplified, static-eval-level recognition; requiring an exact
    // defending-rook square in addition would risk under-recognizing
    // real Lucena positions where the defending rook hasn't yet
    // settled onto its most typical square.
    if (!rook_pawn && rel_rank == 6 && board::chebyshev_distance(attacker_king_sq, promotion_sq) <= 1 &&
        board::chebyshev_distance(defender_king_sq, promotion_sq) >= 3) {
        return kLucenaWinBonus;
    }

    // Philidor position (CPW "Philidor Position"): the pawn hasn't yet
    // crossed onto the cutting-off rank, the defending king is already
    // blockading right at (or beside) the promotion square, and the
    // defending rook holds the rank that cuts the attacking king off
    // from crossing to help the pawn -- the classical drawing setup.
    // The cutting-off rank is the attacker's own relative rank 5 (the
    // rank directly behind where the pawn would need to reach rank 6
    // to force the defending rook to abandon it) -- rank index 5 (rank
    // 6) for White, rank index 2 (rank 3) for Black.
    const int cutoff_abs_rank = (attacker == Color::White) ? 5 : 2;
    if (rel_rank <= 4 && board::chebyshev_distance(defender_king_sq, promotion_sq) <= 1 &&
        board::rank_of(defender_rook_sq) == cutoff_abs_rank) {
        return kPhilidorDrawPenalty;
    }

    return Score{};
}

} // namespace

Score rook_endgame_value(const Position& pos) noexcept {
    if (classify_endgame(pos) != EndgameSignature::RookEndgame) {
        return Score{};
    }

    Score result = tarrasch_rule_value(pos);

    const int total_pawns = board::popcount(pos.pieces(Color::White, PieceType::Pawn)) +
                             board::popcount(pos.pieces(Color::Black, PieceType::Pawn));
    if (total_pawns != 1) {
        // Lucena/Philidor recognition is deliberately scoped to the
        // single-pawn textbook case only -- see this file's own header
        // comment and rook_endgame.h's for why. Tarrasch's Rule above
        // still applies regardless.
        return result;
    }

    const Bitboard white_pawns = pos.pieces(Color::White, PieceType::Pawn);
    const Color attacker = (board::popcount(white_pawns) == 1) ? Color::White : Color::Black;
    const Color defender = board::opposite(attacker);
    const Square pawn_sq = board::bitscan_forward(pos.pieces(attacker, PieceType::Pawn));

    const Bitboard attacker_king_bb = pos.pieces(attacker, PieceType::King);
    const Bitboard defender_king_bb = pos.pieces(defender, PieceType::King);
    const Bitboard defender_rook_bb = pos.pieces(defender, PieceType::Rook);
    if (attacker_king_bb == 0 || defender_king_bb == 0 || defender_rook_bb == 0) {
        // Defensive only -- same reasoning as king_pawn_endgame.cpp's
        // identical guard.
        return result;
    }
    const Square attacker_king_sq = board::bitscan_forward(attacker_king_bb);
    const Square defender_king_sq = board::bitscan_forward(defender_king_bb);
    const Square defender_rook_sq = board::bitscan_forward(defender_rook_bb);

    const Score pattern_adjustment = lucena_and_philidor_value(
        attacker, pawn_sq, attacker_king_sq, defender_king_sq, defender_rook_sq);
    result += (attacker == Color::White) ? pattern_adjustment : -pattern_adjustment;

    return result;
}

} // namespace nightwing::eval
