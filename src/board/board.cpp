// src/board/board.cpp
//
// See board.h. Implements the standard starting position factory, the
// debug pretty-printer, and make_move()/unmake_move() — everything
// performance-sensitive and trivial (occupied(), piece_at(),
// place_piece()/remove_piece()/move_piece()) is header-only/constexpr.

#include "board/board.h"

#include "board/move.h"
#include "board/zobrist.h"

namespace nightwing::board {

namespace {

/// Returns the corner square a rook must start on to hold `right`
/// (one of the castling::k* flags), for revoking rights when a piece
/// leaves (or is captured on) that square.
[[nodiscard]] constexpr Square corner_square_for(std::uint8_t right) noexcept {
    switch (right) {
        case castling::kWhiteQueenside: return make_square(0, 0); // a1
        case castling::kWhiteKingside: return make_square(7, 0);  // h1
        case castling::kBlackQueenside: return make_square(0, 7); // a8
        case castling::kBlackKingside: return make_square(7, 7);  // h8
        default: return 0; // unreachable for a valid single flag
    }
}

/// Returns the en passant capture square (where the captured pawn
/// actually sits) for an EnPassant-flagged move by `us` landing on `to`.
[[nodiscard]] constexpr Square en_passant_captured_square(Color us, Square to) noexcept {
    return static_cast<Square>(us == Color::White ? to - 8 : to + 8);
}

} // namespace

void make_move(Position& pos, const Move& move, UndoInfo& undo) noexcept {
    const Color us = pos.side_to_move;
    const Color them = opposite(us);
    const Square from = move.from();
    const Square to = move.to();
    const MoveFlag flag = move.flag();

    undo.castling_rights = pos.castling_rights;
    undo.en_passant_square = pos.en_passant_square;
    undo.halfmove_clock = pos.halfmove_clock;
    undo.zobrist_hash = pos.zobrist_hash;
    undo.captured_piece = Piece::None;

    std::uint64_t hash = pos.zobrist_hash;

    // En passant target square is only ever valid for the one ply right
    // after a double pawn push, so it always gets cleared here first —
    // re-set below only if this move is itself a double push.
    if (pos.en_passant_square != kNoEnPassantSquare) {
        hash ^= en_passant_file_key(file_of(pos.en_passant_square));
    }
    pos.en_passant_square = kNoEnPassantSquare;

    const Piece moving_piece = pos.piece_at(from);
    const PieceType moving_type = piece_type_of(moving_piece);
    const bool is_capture = move.is_capture();
    const bool is_pawn_move = (moving_type == PieceType::Pawn);

    const Square captured_sq =
        (flag == MoveFlag::EnPassant) ? en_passant_captured_square(us, to) : to;

    if (is_capture) {
        undo.captured_piece = pos.piece_at(captured_sq);
        hash ^= piece_square_key(undo.captured_piece, captured_sq);
        pos.remove_piece(captured_sq);
    }

    hash ^= piece_square_key(moving_piece, from);
    pos.remove_piece(from);

    const Piece placed_piece =
        move.is_promotion() ? make_piece(us, move.promotion_piece_type()) : moving_piece;
    pos.place_piece(to, placed_piece);
    hash ^= piece_square_key(placed_piece, to);

    if (flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle) {
        const int rank = (us == Color::White) ? 0 : 7;
        const Square rook_from =
            (flag == MoveFlag::KingCastle) ? make_square(7, rank) : make_square(0, rank);
        const Square rook_to =
            (flag == MoveFlag::KingCastle) ? make_square(5, rank) : make_square(3, rank);
        const Piece rook_piece = pos.piece_at(rook_from);
        hash ^= piece_square_key(rook_piece, rook_from);
        pos.remove_piece(rook_from);
        pos.place_piece(rook_to, rook_piece);
        hash ^= piece_square_key(rook_piece, rook_to);
    }

    if (flag == MoveFlag::DoublePawnPush) {
        pos.en_passant_square = static_cast<Square>((from + to) / 2);
        hash ^= en_passant_file_key(file_of(pos.en_passant_square));
    }

    // Castling rights only ever get revoked, never re-granted: the king
    // moving revokes both of that color's rights; a rook leaving (or
    // being captured on) one of the four corner squares revokes that
    // corner's right specifically. Checking by square rather than "is
    // this piece actually the original rook" is deliberate and safe —
    // once a corner's right is revoked it stays revoked, so a later,
    // unrelated piece passing through that square is a harmless no-op
    // against an already-cleared bit.
    std::uint8_t new_rights = pos.castling_rights;
    if (moving_type == PieceType::King) {
        new_rights &= (us == Color::White)
                          ? static_cast<std::uint8_t>(~(castling::kWhiteKingside | castling::kWhiteQueenside))
                          : static_cast<std::uint8_t>(~(castling::kBlackKingside | castling::kBlackQueenside));
    }
    for (std::uint8_t right :
         {castling::kWhiteKingside, castling::kWhiteQueenside, castling::kBlackKingside,
          castling::kBlackQueenside}) {
        if ((new_rights & right) && (from == corner_square_for(right) || to == corner_square_for(right))) {
            new_rights &= static_cast<std::uint8_t>(~right);
        }
    }
    if (new_rights != pos.castling_rights) {
        const std::uint8_t revoked = pos.castling_rights & static_cast<std::uint8_t>(~new_rights);
        for (std::uint8_t right :
             {castling::kWhiteKingside, castling::kWhiteQueenside, castling::kBlackKingside,
              castling::kBlackQueenside}) {
            if (revoked & right) hash ^= castling_right_key(right);
        }
        pos.castling_rights = new_rights;
    }

    pos.halfmove_clock = (is_pawn_move || is_capture) ? 0 : static_cast<std::uint8_t>(pos.halfmove_clock + 1);

    if (us == Color::Black) {
        ++pos.fullmove_number;
    }

    pos.side_to_move = them;
    hash ^= side_to_move_key(); // unconditional toggle; XOR is its own inverse

    pos.zobrist_hash = hash;
}

void unmake_move(Position& pos, const Move& move, const UndoInfo& undo) noexcept {
    const Color them = pos.side_to_move; // side to move now == side that just moved's opponent
    const Color us = opposite(them);
    const Square from = move.from();
    const Square to = move.to();
    const MoveFlag flag = move.flag();

    if (us == Color::Black) {
        --pos.fullmove_number;
    }

    pos.side_to_move = us;
    pos.castling_rights = undo.castling_rights;
    pos.en_passant_square = undo.en_passant_square;
    pos.halfmove_clock = undo.halfmove_clock;
    pos.zobrist_hash = undo.zobrist_hash;

    if (flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle) {
        const int rank = (us == Color::White) ? 0 : 7;
        const Square rook_from =
            (flag == MoveFlag::KingCastle) ? make_square(7, rank) : make_square(0, rank);
        const Square rook_to =
            (flag == MoveFlag::KingCastle) ? make_square(5, rank) : make_square(3, rank);
        pos.move_piece(rook_to, rook_from);
    }

    const Piece placed_piece = pos.remove_piece(to);
    const Piece original_piece = move.is_promotion() ? make_piece(us, PieceType::Pawn) : placed_piece;
    pos.place_piece(from, original_piece);

    if (undo.captured_piece != Piece::None) {
        const Square captured_sq =
            (flag == MoveFlag::EnPassant) ? en_passant_captured_square(us, to) : to;
        pos.place_piece(captured_sq, undo.captured_piece);
    }
}

void make_null_move(Position& pos, UndoInfo& undo) noexcept {
    undo.captured_piece = Piece::None;
    undo.castling_rights = pos.castling_rights; // unchanged by a null move; saved for symmetry only
    undo.en_passant_square = pos.en_passant_square;
    undo.halfmove_clock = pos.halfmove_clock;
    undo.zobrist_hash = pos.zobrist_hash;

    std::uint64_t hash = pos.zobrist_hash;

    // Same "clear en passant, XOR its file key out if one was set"
    // pattern make_move() uses above — a null move forfeits any
    // just-set en passant opportunity exactly like any other move that
    // doesn't take it.
    if (pos.en_passant_square != kNoEnPassantSquare) {
        hash ^= en_passant_file_key(file_of(pos.en_passant_square));
    }
    pos.en_passant_square = kNoEnPassantSquare;

    pos.halfmove_clock = static_cast<std::uint8_t>(pos.halfmove_clock + 1);

    pos.side_to_move = opposite(pos.side_to_move);
    hash ^= side_to_move_key(); // unconditional toggle; XOR is its own inverse

    pos.zobrist_hash = hash;
}

void unmake_null_move(Position& pos, const UndoInfo& undo) noexcept {
    pos.side_to_move = opposite(pos.side_to_move);
    pos.castling_rights = undo.castling_rights;
    pos.en_passant_square = undo.en_passant_square;
    pos.halfmove_clock = undo.halfmove_clock;
    pos.zobrist_hash = undo.zobrist_hash;
}

Position start_position() {
    Position pos;
    // Position's default member initializers already give White to move,
    // all castling rights, no en passant, halfmove_clock 0, fullmove 1 —
    // only piece placement needs to be filled in here.

    // Pawns: rank 2 (White) / rank 7 (Black).
    for (int file = 0; file < kNumFiles; ++file) {
        pos.place_piece(make_square(file, 1), Piece::WhitePawn);
        pos.place_piece(make_square(file, 6), Piece::BlackPawn);
    }

    // Back ranks: a/h rooks, b/g knights, c/f bishops, d queen, e king.
    struct BackRankPiece {
        int file;
        Piece white;
        Piece black;
    };
    static constexpr BackRankPiece kBackRank[8] = {
        {0, Piece::WhiteRook, Piece::BlackRook},
        {1, Piece::WhiteKnight, Piece::BlackKnight},
        {2, Piece::WhiteBishop, Piece::BlackBishop},
        {3, Piece::WhiteQueen, Piece::BlackQueen},
        {4, Piece::WhiteKing, Piece::BlackKing},
        {5, Piece::WhiteBishop, Piece::BlackBishop},
        {6, Piece::WhiteKnight, Piece::BlackKnight},
        {7, Piece::WhiteRook, Piece::BlackRook},
    };
    for (const auto& entry : kBackRank) {
        pos.place_piece(make_square(entry.file, 0), entry.white);
        pos.place_piece(make_square(entry.file, 7), entry.black);
    }

    // init_zobrist_keys() is idempotent, so calling it here rather than
    // requiring callers to remember to do so first guarantees this
    // factory always returns a Position with a correct, non-stale hash.
    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);

    return pos;
}

namespace {

/// Standard single-letter piece notation (uppercase White, lowercase
/// Black), matching FEN/PGN convention. Returns '.' for Piece::None.
char piece_letter(Piece p) {
    switch (p) {
        case Piece::WhitePawn: return 'P';
        case Piece::WhiteKnight: return 'N';
        case Piece::WhiteBishop: return 'B';
        case Piece::WhiteRook: return 'R';
        case Piece::WhiteQueen: return 'Q';
        case Piece::WhiteKing: return 'K';
        case Piece::BlackPawn: return 'p';
        case Piece::BlackKnight: return 'n';
        case Piece::BlackBishop: return 'b';
        case Piece::BlackRook: return 'r';
        case Piece::BlackQueen: return 'q';
        case Piece::BlackKing: return 'k';
        case Piece::None: return '.';
    }
    return '.'; // Unreachable for a valid Piece; keeps -Wreturn-type quiet.
}

} // namespace

std::string to_string(const Position& pos) {
    std::string out;
    out.reserve(kNumSquares + kNumRanks);

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < kNumFiles; ++file) {
            out += piece_letter(pos.piece_at(make_square(file, rank)));
        }
        out += '\n';
    }
    return out;
}

} // namespace nightwing::board
