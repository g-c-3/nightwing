#pragma once
// src/board/move.h
//
// A `Move` is a legal or pseudo-legal chess move packed into 16 bits: 6
// bits `from` square, 6 bits `to` square, 4 bits flag. The flag encoding
// (quiet / double push / castle / capture / en passant / four promotion
// kinds x plain-or-capture) follows the widely-documented convention on
// the Chess Programming Wiki, "Encoding Moves"
// (https://www.chessprogramming.org/Encoding_Moves) — this is a from-
// scratch implementation of that public scheme, no code copied.
//
// `MoveList` is a fixed-size stack array (max legal moves in any reachable
// chess position is bounded at 218), per ARCHITECTURE.md's "no heap
// allocation in the search hot path" standard.

#include <array>
#include <cassert>
#include <cstdint>
#include <string>

#include "board/bitboard.h"
#include "board/board.h"

namespace nightwing::board {

/// The 4-bit move-flag encoding. Values 6/7 are deliberately unused,
/// matching the standard CPW table (kept as gaps rather than repurposed,
/// so the promotion-detection bit test in Move::is_promotion() below —
/// "flag >= PromoKnight" — stays a single comparison).
enum class MoveFlag : std::uint8_t {
    Quiet = 0,
    DoublePawnPush = 1,
    KingCastle = 2,
    QueenCastle = 3,
    Capture = 4,
    EnPassant = 5,
    PromoKnight = 8,
    PromoBishop = 9,
    PromoRook = 10,
    PromoQueen = 11,
    PromoCaptureKnight = 12,
    PromoCaptureBishop = 13,
    PromoCaptureRook = 14,
    PromoCaptureQueen = 15,
};

/// A single move, packed into 16 bits (bits 0-5 = from, 6-11 = to, 12-15 =
/// flag). Deliberately trivially-copyable and tiny so move lists, search
/// stacks, and TT entries can hold many of these cheaply.
class Move {
public:
    constexpr Move() noexcept = default;

    constexpr Move(Square from, Square to, MoveFlag flag = MoveFlag::Quiet) noexcept
        : data_(static_cast<std::uint16_t>(
              (static_cast<std::uint16_t>(flag) << 12) |
              (static_cast<std::uint16_t>(to) << 6) |
              static_cast<std::uint16_t>(from))) {
        assert(from >= 0 && from < kNumSquares);
        assert(to >= 0 && to < kNumSquares);
    }

    [[nodiscard]] constexpr Square from() const noexcept {
        return static_cast<Square>(data_ & 0x3F);
    }

    [[nodiscard]] constexpr Square to() const noexcept {
        return static_cast<Square>((data_ >> 6) & 0x3F);
    }

    [[nodiscard]] constexpr MoveFlag flag() const noexcept {
        return static_cast<MoveFlag>((data_ >> 12) & 0xF);
    }

    /// Raw packed bits — for TT storage / equality / hashing convenience.
    [[nodiscard]] constexpr std::uint16_t raw() const noexcept { return data_; }

    [[nodiscard]] constexpr bool is_capture() const noexcept {
        switch (flag()) {
            case MoveFlag::Capture:
            case MoveFlag::EnPassant:
            case MoveFlag::PromoCaptureKnight:
            case MoveFlag::PromoCaptureBishop:
            case MoveFlag::PromoCaptureRook:
            case MoveFlag::PromoCaptureQueen:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] constexpr bool is_promotion() const noexcept {
        return static_cast<std::uint8_t>(flag()) >= static_cast<std::uint8_t>(MoveFlag::PromoKnight);
    }

    [[nodiscard]] constexpr bool is_castle() const noexcept {
        return flag() == MoveFlag::KingCastle || flag() == MoveFlag::QueenCastle;
    }

    [[nodiscard]] constexpr bool is_en_passant() const noexcept {
        return flag() == MoveFlag::EnPassant;
    }

    [[nodiscard]] constexpr bool is_double_pawn_push() const noexcept {
        return flag() == MoveFlag::DoublePawnPush;
    }

    /// Returns true for the default-constructed "no move" sentinel. Not a
    /// real move: from == to == a1 with a Quiet flag never occurs from
    /// actual move generation (from != to always), so it's safe as a
    /// null/none sentinel for TT slots, killer-move slots, etc.
    [[nodiscard]] constexpr bool is_null() const noexcept { return data_ == 0; }

    /// Returns the promoted-to piece type. Precondition: is_promotion().
    [[nodiscard]] constexpr PieceType promotion_piece_type() const noexcept {
        assert(is_promotion());
        switch (flag()) {
            case MoveFlag::PromoKnight:
            case MoveFlag::PromoCaptureKnight:
                return PieceType::Knight;
            case MoveFlag::PromoBishop:
            case MoveFlag::PromoCaptureBishop:
                return PieceType::Bishop;
            case MoveFlag::PromoRook:
            case MoveFlag::PromoCaptureRook:
                return PieceType::Rook;
            case MoveFlag::PromoQueen:
            case MoveFlag::PromoCaptureQueen:
            default:
                return PieceType::Queen;
        }
    }

    [[nodiscard]] constexpr bool operator==(const Move&) const noexcept = default;

    /// Returns UCI long algebraic notation (e.g. "e2e4", "e7e8q"). Debug/
    /// test/UCI-I-O tool only — not used in the search hot path.
    [[nodiscard]] std::string to_uci() const;

private:
    std::uint16_t data_ = 0;
};

/// Theoretical maximum number of legal moves in any reachable chess
/// position (the record position, R. Bruce 1968-style construction,
/// tops out at 218) — see https://www.chessprogramming.org/Chess_Position#The_Maximum_Number_of_Moves.
/// Used to size MoveList as a fixed stack array, per ARCHITECTURE.md
/// ("Avoid std::vector allocation in the search hot path").
inline constexpr int kMaxMoves = 218;

/// A fixed-capacity, stack-allocated list of moves. No heap allocation,
/// no exceptions — a plain bounded array with a running count.
class MoveList {
public:
    constexpr void push_back(Move m) noexcept {
        assert(count_ < kMaxMoves && "MoveList overflow - kMaxMoves is a hard ceiling");
        moves_[static_cast<std::size_t>(count_)] = m;
        ++count_;
    }

    constexpr void clear() noexcept { count_ = 0; }

    [[nodiscard]] constexpr int size() const noexcept { return count_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    [[nodiscard]] constexpr Move& operator[](int i) noexcept {
        assert(i >= 0 && i < count_);
        return moves_[static_cast<std::size_t>(i)];
    }
    [[nodiscard]] constexpr const Move& operator[](int i) const noexcept {
        assert(i >= 0 && i < count_);
        return moves_[static_cast<std::size_t>(i)];
    }

    [[nodiscard]] constexpr Move* begin() noexcept { return moves_.data(); }
    [[nodiscard]] constexpr Move* end() noexcept { return moves_.data() + count_; }
    [[nodiscard]] constexpr const Move* begin() const noexcept { return moves_.data(); }
    [[nodiscard]] constexpr const Move* end() const noexcept { return moves_.data() + count_; }

    /// Returns true if `m` is present in the list (linear scan — debug/
    /// test convenience, not used in the search hot path).
    [[nodiscard]] constexpr bool contains(Move m) const noexcept {
        for (int i = 0; i < count_; ++i) {
            if (moves_[static_cast<std::size_t>(i)] == m) return true;
        }
        return false;
    }

private:
    std::array<Move, kMaxMoves> moves_{};
    int count_ = 0;
};

} // namespace nightwing::board
