#pragma once
// src/board/board.h
//
// The board state struct (`Position`) and its supporting piece/color
// enums, plus make/unmake move. Holds exactly the state needed to fully
// describe a chess position: piece placement (bitboards + mailbox), side
// to move, castling rights, en passant target, and the two move
// counters. Deliberately minimal and dense — kept to a small number of
// cache lines (see the static_assert at the bottom of this file) since
// Position is read and copied constantly throughout search/movegen
// (ARCHITECTURE.md, "Memory & Cache").
//
// Legal move generation (movegen.h) is a separate module built on top of
// this one; it doesn't need make/unmake (see movegen.h's header comment
// for why: legality is resolved via pin/check masks instead), so there's
// no include-order dependency between the two beyond movegen depending
// on Position's read-only query surface.

#include <array>
#include <cstdint>
#include <string>

#include "board/bitboard.h"

namespace nightwing::board {

/// Forward declaration only — full definition is move.h. board.h can't
/// include move.h itself (move.h includes board.h, for Square/PieceType/
/// etc., so that direction would be circular); only board.cpp, which
/// implements make_move()/unmake_move(), needs the full Move definition.
class Move;

/// A side to move / piece color.
enum class Color : std::uint8_t {
    White = 0,
    Black = 1,
};

/// Returns the opposite color.
[[nodiscard]] constexpr Color opposite(Color c) noexcept {
    return c == Color::White ? Color::Black : Color::White;
}

/// Number of colors — used to size color-indexed arrays.
inline constexpr int kNumColors = 2;

/// A color-agnostic piece kind, used to index `Position::piece_bb[color]`.
/// `None` is a sentinel (e.g. "no piece captured") and is not a valid
/// index into piece-bitboard arrays — see kNumPieceTypes.
enum class PieceType : std::uint8_t {
    Pawn = 0,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
    None,
};

/// Number of real piece types (excludes the `None` sentinel) — used to
/// size piece-type-indexed arrays.
inline constexpr int kNumPieceTypes = 6;

/// A color+kind piece, used for the `Position::piece_on` mailbox where a
/// single-byte "what exactly is on this square" answer is needed without
/// consulting all twelve piece bitboards. `None` means the square is empty.
enum class Piece : std::uint8_t {
    WhitePawn = 0,
    WhiteKnight,
    WhiteBishop,
    WhiteRook,
    WhiteQueen,
    WhiteKing,
    BlackPawn,
    BlackKnight,
    BlackBishop,
    BlackRook,
    BlackQueen,
    BlackKing,
    None,
};

/// Combines a color and piece type into a mailbox `Piece` value.
/// Precondition: `type != PieceType::None`.
[[nodiscard]] constexpr Piece make_piece(Color c, PieceType type) noexcept {
    return static_cast<Piece>(static_cast<std::uint8_t>(c) * kNumPieceTypes +
                               static_cast<std::uint8_t>(type));
}

/// Returns the color-agnostic piece type of `p`. Precondition: `p != Piece::None`.
[[nodiscard]] constexpr PieceType piece_type_of(Piece p) noexcept {
    return static_cast<PieceType>(static_cast<std::uint8_t>(p) % kNumPieceTypes);
}

/// Returns the color of `p`. Precondition: `p != Piece::None`.
[[nodiscard]] constexpr Color color_of(Piece p) noexcept {
    return static_cast<Color>(static_cast<std::uint8_t>(p) / kNumPieceTypes);
}

/// Castling-rights bitmask flags (combine with `|`, test with `&`).
/// Deliberately plain `constexpr` values rather than an enum, so they
/// compose with ordinary bitwise operators without extra ceremony.
namespace castling {
inline constexpr std::uint8_t kWhiteKingside = 1u << 0;
inline constexpr std::uint8_t kWhiteQueenside = 1u << 1;
inline constexpr std::uint8_t kBlackKingside = 1u << 2;
inline constexpr std::uint8_t kBlackQueenside = 1u << 3;
inline constexpr std::uint8_t kAll =
    kWhiteKingside | kWhiteQueenside | kBlackKingside | kBlackQueenside;
} // namespace castling

/// Sentinel for "no en passant target square".
inline constexpr std::int8_t kNoEnPassantSquare = -1;

/// The full state of a chess position. No history, no make/unmake logic —
/// just the data. Piece placement is stored twice, deliberately: as
/// per-color-per-type bitboards (`piece_bb`, what movegen scans) and as a
/// per-square mailbox (`piece_on`, O(1) "what's on this square" — needed
/// constantly for captures, SEE, and printing without scanning twelve
/// bitboards). Keeping both in sync is the responsibility of whatever
/// mutates a Position (make/unmake move, a later roadmap item); nothing
/// in this file mutates a Position after construction.
///
/// Combined occupancy (both colors) is deliberately NOT stored — it's one
/// OR instruction away via occupied(), and storing it would be a second
/// piece of state that make/unmake would have to remember to keep in sync
/// for no real performance benefit.
struct Position {
    /// piece_bb[color][piece_type]: one bitboard per (color, piece type) pair.
    std::array<std::array<Bitboard, kNumPieceTypes>, kNumColors> piece_bb{};

    /// occupancy[color]: union of that color's piece_bb entries.
    std::array<Bitboard, kNumColors> occupancy{};

    /// Mailbox: piece_on[sq] is the piece on `sq`, or Piece::None if empty.
    /// Explicitly filled with Piece::None rather than left value-initialized:
    /// Piece::WhitePawn is enumerator 0, so a plain `{}` default would
    /// silently make every empty square read as a white pawn instead of
    /// empty — this immediately-invoked lambda avoids relying on zero as
    /// an implicit "empty" sentinel.
    std::array<Piece, kNumSquares> piece_on = [] {
        std::array<Piece, kNumSquares> arr{};
        arr.fill(Piece::None);
        return arr;
    }();

    Color side_to_move = Color::White;

    /// Bitwise OR of the `castling::k*` flags for rights still available.
    /// Note: a set flag means the right hasn't been permanently lost (king
    /// or that rook has never moved) — it does NOT mean the castle is
    /// legal right now (that also depends on check/attacked squares/
    /// blocking pieces, evaluated by move generation, not stored here).
    std::uint8_t castling_rights = castling::kAll;

    /// En passant target square (the square a capturing pawn would move
    /// to), or kNoEnPassantSquare if the previous move wasn't a two-square
    /// pawn push.
    std::int8_t en_passant_square = kNoEnPassantSquare;

    /// Plies since the last pawn move or capture (50-move rule counter;
    /// the rule triggers at 100 plies = 50 full moves).
    std::uint8_t halfmove_clock = 0;

    /// Full-move number, per FEN convention: starts at 1, increments after
    /// Black's move.
    std::uint16_t fullmove_number = 1;

    /// Zobrist hash of this position (see board/zobrist.h). Defaults to 0
    /// (not a valid hash for any real position with keys initialized —
    /// see compute_hash()'s precondition) until explicitly set; factories
    /// like start_position() set it correctly. Whatever mutates a Position
    /// (make/unmake move, a later roadmap item) is responsible for
    /// updating this incrementally rather than leaving it stale.
    std::uint64_t zobrist_hash = 0;

    /// Returns the union of both colors' occupancy — every occupied square.
    [[nodiscard]] constexpr Bitboard occupied() const noexcept {
        return occupancy[static_cast<std::size_t>(Color::White)] |
               occupancy[static_cast<std::size_t>(Color::Black)];
    }

    /// Returns the piece on `sq`, or Piece::None if empty.
    [[nodiscard]] constexpr Piece piece_at(Square sq) const noexcept {
        return piece_on[static_cast<std::size_t>(sq)];
    }

    /// Returns true if `sq` has no piece on it.
    [[nodiscard]] constexpr bool is_empty(Square sq) const noexcept {
        return piece_at(sq) == Piece::None;
    }

    /// Returns the bitboard for a specific (color, piece type) pair.
    [[nodiscard]] constexpr Bitboard pieces(Color c, PieceType type) const noexcept {
        return piece_bb[static_cast<std::size_t>(c)][static_cast<std::size_t>(type)];
    }

    /// Places `piece` on `sq`, updating both piece_bb and the mailbox.
    /// Precondition: `sq` is currently empty (this is a placement helper
    /// for building positions from scratch, e.g. start_position() below —
    /// not a move-making primitive; make/unmake move, which must also
    /// handle removing whatever was previously on the destination square,
    /// is a separate later roadmap item).
    constexpr void place_piece(Square sq, Piece piece) noexcept {
        set_bit(piece_bb[static_cast<std::size_t>(color_of(piece))]
                        [static_cast<std::size_t>(piece_type_of(piece))],
                sq);
        set_bit(occupancy[static_cast<std::size_t>(color_of(piece))], sq);
        piece_on[static_cast<std::size_t>(sq)] = piece;
    }

    /// Removes whatever piece is on `sq`, updating both piece_bb and the
    /// mailbox, and returns the piece that was removed. Precondition:
    /// `sq` is occupied.
    constexpr Piece remove_piece(Square sq) noexcept {
        const Piece p = piece_on[static_cast<std::size_t>(sq)];
        clear_bit(piece_bb[static_cast<std::size_t>(color_of(p))]
                          [static_cast<std::size_t>(piece_type_of(p))],
                  sq);
        clear_bit(occupancy[static_cast<std::size_t>(color_of(p))], sq);
        piece_on[static_cast<std::size_t>(sq)] = Piece::None;
        return p;
    }

    /// Moves whatever piece is on `from` to `to`, updating both piece_bb
    /// and the mailbox. Precondition: `from` is occupied, `to` is empty
    /// (this is a relocation helper, not a capture — callers that need to
    /// overwrite an occupied `to` square must remove_piece() it first;
    /// make_move() in board.cpp does this explicitly for captures rather
    /// than folding it in here, so this stays a simple, precondition-safe
    /// primitive).
    constexpr void move_piece(Square from, Square to) noexcept {
        const Piece p = remove_piece(from);
        place_piece(to, p);
    }
};

/// Saved state needed to exactly reverse one make_move() call. Move
/// itself already encodes from/to/flag/promotion, so the only extra
/// state make_move() needs to hand back is whatever it would otherwise
/// have destroyed or couldn't re-derive: the captured piece (if any) and
/// the pre-move "history" fields that don't follow mechanically from the
/// move alone (castling rights, en passant target, halfmove clock, and
/// the Zobrist hash — restored verbatim on unmake rather than re-derived
/// by reversing the incremental XOR steps, which is simpler and equally
/// O(1), the same pattern used by e.g. Stockfish's state stack).
///
/// Intended usage is a fixed-size stack of these indexed by search ply
/// (ARCHITECTURE.md "Memory & Cache": no heap allocation in the search
/// hot path) — callers own that storage; UndoInfo itself is just a plain
/// value type.
struct UndoInfo {
    Piece captured_piece = Piece::None; // Piece::None if the move wasn't a capture
    std::uint8_t castling_rights = 0;   // pre-move value
    std::int8_t en_passant_square = kNoEnPassantSquare; // pre-move value
    std::uint8_t halfmove_clock = 0;    // pre-move value
    std::uint64_t zobrist_hash = 0;     // pre-move value
};

/// Applies `move` (assumed pseudo-legal/legal — this function does not
/// re-validate legality) to `pos` in place: moves the piece, resolves
/// captures (including en passant), promotions, and castling rook
/// movement, updates castling rights/en passant target/halfmove/fullmove
/// counters, and incrementally updates `pos.zobrist_hash` (never a full
/// recompute — ARCHITECTURE.md "Incremental Updates"). Saves everything
/// needed to reverse the move into `undo`. Precondition:
/// init_zobrist_keys() has been called.
void make_move(Position& pos, const Move& move, UndoInfo& undo) noexcept;

/// Exactly reverses the most recent make_move(pos, move, undo) call.
/// Precondition: `move` and `undo` are the same arguments/output from
/// the matching make_move() call, and no other move has been made on
/// `pos` since (this is a strict stack discipline — make/unmake calls
/// must nest, like the search tree they're driving).
void unmake_move(Position& pos, const Move& move, const UndoInfo& undo) noexcept;

/// Applies a "null move" to `pos` (CPW "Null Move Pruning", search/
/// search.cpp): side to move flips, any en passant target is cleared
/// (a null move means "the side to move does nothing," so any
/// just-set en passant capture opportunity is forfeit exactly as it
/// would be after any other real move that doesn't take it),
/// halfmove_clock increments (a null move is not a pawn move or
/// capture), and pos.zobrist_hash is updated incrementally (side-to-move
/// key XORed, en passant file key XORed out if one was set) — never a
/// full recompute, the same discipline make_move() follows. No piece
/// moves, and fullmove_number is deliberately left unchanged — a null
/// move is a search-tree-only artifact, not a real ply in the game's
/// own move numbering, and nothing reads fullmove_number during search
/// (it exists only for FEN output). Saves the pre-move en_passant_square,
/// halfmove_clock, and zobrist_hash into `undo` for unmake_null_move()
/// to reverse; `undo.captured_piece`/`undo.castling_rights` are left at
/// UndoInfo's defaults, unused — a null move can't capture anything or
/// change castling rights, so there's nothing there to save or restore.
/// Precondition: init_zobrist_keys() has been called, and (CPW's own
/// "Null Move Pruning" caveat) `pos.side_to_move` is not currently in
/// check — passing a null move in check would let the search "escape"
/// check by doing nothing, which is not a legal chess outcome and would
/// corrupt the search; the caller (negamax(), search.cpp) is responsible
/// for checking this before calling.
void make_null_move(Position& pos, UndoInfo& undo) noexcept;

/// Exactly reverses the most recent make_null_move(pos, undo) call. Same
/// strict stack-discipline precondition as unmake_move().
void unmake_null_move(Position& pos, const UndoInfo& undo) noexcept;

/// Returns the standard chess starting position: full initial piece
/// placement, White to move, all castling rights available, no en
/// passant target, halfmove clock 0, fullmove number 1.
[[nodiscard]] Position start_position();

/// Returns an 8x8 ASCII-art rendering of `pos` (rank 8 on top, per
/// convention seen in bitboard::to_string()), one piece-letter or '.' per
/// square (uppercase = White, lowercase = Black, standard PGN letters,
/// pawns as 'P'/'p'). Debug/test tool only — not used in the hot path.
[[nodiscard]] std::string to_string(const Position& pos);

// A Position currently comes to exactly three 64-byte cache lines
// (measured: sizeof(Position) == 192 on a typical x86-64/ARM64 LP64
// target with default alignment — 12 piece bitboards + 2 occupancy
// bitboards = 112 bytes, 64-byte mailbox, a handful of state bytes, plus
// the 8-byte zobrist_hash). This is exactly at the assert's current
// ceiling — the next addition needs either a deliberate look at trimming
// something (ARCHITECTURE.md "Memory & Cache") or a deliberate, documented
// decision to raise the ceiling; this assert exists to make that choice
// visible rather than let it happen by accident.
static_assert(sizeof(Position) <= 192,
              "Position has grown beyond ~3 cache lines - if intentional, "
              "update this assert and note the tradeoff in DECISIONS.md");

} // namespace nightwing::board
