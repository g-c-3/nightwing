// src/board/fen.cpp
//
// See fen.h.

#include "board/fen.h"

#include <cctype>
#include <sstream>
#include <stdexcept>

#include "board/attacks.h"
#include "board/masks.h"
#include "board/movegen.h"
#include "board/zobrist.h"

namespace nightwing::board {

namespace {

Piece char_to_piece(char c) {
    switch (c) {
        case 'P': return Piece::WhitePawn;
        case 'N': return Piece::WhiteKnight;
        case 'B': return Piece::WhiteBishop;
        case 'R': return Piece::WhiteRook;
        case 'Q': return Piece::WhiteQueen;
        case 'K': return Piece::WhiteKing;
        case 'p': return Piece::BlackPawn;
        case 'n': return Piece::BlackKnight;
        case 'b': return Piece::BlackBishop;
        case 'r': return Piece::BlackRook;
        case 'q': return Piece::BlackQueen;
        case 'k': return Piece::BlackKing;
        default:
            throw std::invalid_argument(std::string("parse_fen: unrecognized piece character '") + c + "'");
    }
}

char piece_to_char(Piece p) {
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
        case Piece::None: return '.'; // unreachable: caller only calls this for occupied squares
    }
    return '.'; // unreachable for a valid Piece; keeps -Wreturn-type quiet
}


/// Rejects a parsed Position that no legal game could reach in a way that
/// would crash or corrupt move generation (added 2026-10-18, external
/// review batch 1 (a); docs/DECISIONS.md 2026-10-18 (7)). Checked, in
/// order: exactly one king per side; no pawn on rank 1 or 8; kings not
/// adjacent; the side NOT to move not in check; every castling right
/// backed by its king and rook on their home squares; and any en passant
/// square consistent with the side to move (correct rank, empty, enemy
/// pawn just in front of it, its origin square empty).
///
/// Deliberately NOT checked: that a pawn of the side to move can actually
/// capture onto the en passant square. FENs written by many GUIs and
/// libraries record the square after every double push, capturable or
/// not, and rejecting those would refuse ordinary input; movegen already
/// handles an uncapturable en passant square harmlessly.
///
/// Preconditions: pos's bitboards and mailbox are populated; init_masks()
/// and init_magic_bitboards() have run (parse_fen() calls both first).
/// Throws std::invalid_argument naming the first violated rule.
void validate_position(const Position& pos, const std::string& fen) {
    constexpr Bitboard kBackRanks = 0xFF000000000000FFULL; // rank 1 and rank 8

    for (const Color c : {Color::White, Color::Black}) {
        const int kings = popcount(pos.pieces(c, PieceType::King));
        if (kings != 1) {
            throw std::invalid_argument(
                std::string("parse_fen: ") + (c == Color::White ? "white" : "black") +
                " must have exactly one king (found " + std::to_string(kings) + "): " + fen);
        }
    }

    const Bitboard white_king_bb = pos.pieces(Color::White, PieceType::King);
    const Bitboard black_king_bb = pos.pieces(Color::Black, PieceType::King);
    const Square white_king = bitscan_forward(white_king_bb);
    const Square black_king = bitscan_forward(black_king_bb);

    const Bitboard all_pawns = pos.pieces(Color::White, PieceType::Pawn) |
                               pos.pieces(Color::Black, PieceType::Pawn);
    if ((all_pawns & kBackRanks) != 0) {
        throw std::invalid_argument("parse_fen: a pawn stands on rank 1 or rank 8: " + fen);
    }

    if ((king_attacks(white_king) & black_king_bb) != 0) {
        throw std::invalid_argument("parse_fen: the two kings are adjacent: " + fen);
    }

    // The side that just moved cannot have left its own king attacked by
    // the side to move... i.e. the side NOT to move must not be in check.
    const Color stm = pos.side_to_move;
    const Color other = opposite(stm);
    const Square other_king = (other == Color::White) ? white_king : black_king;
    if (is_square_attacked(pos, other_king, stm)) {
        throw std::invalid_argument(
            "parse_fen: the side not to move is in check (an unreachable position): " + fen);
    }

    struct CastlingRequirement {
        std::uint8_t flag;
        Color color;
        Square king_sq;
        Square rook_sq;
        const char* name;
    };
    const CastlingRequirement requirements[] = {
        {castling::kWhiteKingside, Color::White, make_square(4, 0), make_square(7, 0), "K"},
        {castling::kWhiteQueenside, Color::White, make_square(4, 0), make_square(0, 0), "Q"},
        {castling::kBlackKingside, Color::Black, make_square(4, 7), make_square(7, 7), "k"},
        {castling::kBlackQueenside, Color::Black, make_square(4, 7), make_square(0, 7), "q"},
    };
    for (const CastlingRequirement& req : requirements) {
        if ((pos.castling_rights & req.flag) == 0) continue;
        if (pos.piece_at(req.king_sq) != make_piece(req.color, PieceType::King) ||
            pos.piece_at(req.rook_sq) != make_piece(req.color, PieceType::Rook)) {
            throw std::invalid_argument(
                std::string("parse_fen: castling right '") + req.name +
                "' has no king and rook on their home squares: " + fen);
        }
    }

    if (pos.en_passant_square != kNoEnPassantSquare) {
        const Square ep = pos.en_passant_square;
        // White to move: Black just double-pushed, so the target is on
        // rank 6 (index 5), the pawn now on rank 5 (index 4), and the
        // pawn's origin on rank 7 (index 6). Mirrored for Black to move.
        const bool white_to_move = (stm == Color::White);
        const int expected_rank = white_to_move ? 5 : 2;
        const int pawn_rank = white_to_move ? 4 : 3;
        const int origin_rank = white_to_move ? 6 : 1;
        const Square pawn_sq = make_square(file_of(ep), pawn_rank);
        const Square origin_sq = make_square(file_of(ep), origin_rank);
        const Piece enemy_pawn = make_piece(other, PieceType::Pawn);
        if (rank_of(ep) != expected_rank || !pos.is_empty(ep) || !pos.is_empty(origin_sq) ||
            pos.piece_at(pawn_sq) != enemy_pawn) {
            throw std::invalid_argument(
                "parse_fen: en passant square is inconsistent with the position: " + fen);
        }
    }
}

} // namespace

Position parse_fen(const std::string& fen) {
    std::istringstream iss(fen);
    std::string placement;
    std::string stm;
    std::string castling_str;
    std::string ep_str;
    if (!(iss >> placement >> stm >> castling_str >> ep_str)) {
        throw std::invalid_argument("parse_fen: missing required field(s): " + fen);
    }

    // Halfmove clock and fullmove number are conventionally present but
    // some FEN sources omit them; default to 0 and 1 when absent rather
    // than treating that as an error.
    int halfmove = 0;
    int fullmove = 1;
    if (iss >> halfmove) {
        iss >> fullmove;
    }
    if (halfmove < 0) halfmove = 0;
    if (fullmove < 1) fullmove = 1;

    Position pos;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;

    int rank = 7;
    int file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != kNumFiles) {
                throw std::invalid_argument("parse_fen: a rank didn't sum to 8 files: " + fen);
            }
            --rank;
            file = 0;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            file += (c - '0');
            continue;
        }
        if (rank < 0 || file >= kNumFiles) {
            throw std::invalid_argument("parse_fen: piece placement overruns the board: " + fen);
        }
        pos.place_piece(make_square(file, rank), char_to_piece(c));
        ++file;
    }
    if (rank != 0 || file != kNumFiles) {
        throw std::invalid_argument("parse_fen: piece placement must cover exactly 8 ranks of 8 files: " + fen);
    }

    if (stm == "w") {
        pos.side_to_move = Color::White;
    } else if (stm == "b") {
        pos.side_to_move = Color::Black;
    } else {
        throw std::invalid_argument("parse_fen: side to move must be 'w' or 'b': " + fen);
    }

    if (castling_str != "-") {
        for (char c : castling_str) {
            switch (c) {
                case 'K': pos.castling_rights |= castling::kWhiteKingside; break;
                case 'Q': pos.castling_rights |= castling::kWhiteQueenside; break;
                case 'k': pos.castling_rights |= castling::kBlackKingside; break;
                case 'q': pos.castling_rights |= castling::kBlackQueenside; break;
                default:
                    throw std::invalid_argument(
                        std::string("parse_fen: unrecognized castling character '") + c + "': " + fen);
            }
        }
    }

    if (ep_str != "-") {
        if (ep_str.size() != 2 || ep_str[0] < 'a' || ep_str[0] > 'h' || ep_str[1] < '1' || ep_str[1] > '8') {
            throw std::invalid_argument("parse_fen: malformed en passant square: " + fen);
        }
        // See board.cpp's own identical fix (docs/DECISIONS.md,
        // 2026-09-26 (4)) for why this needs an explicit
        // `static_cast<std::int8_t>` around `make_square()`'s own
        // `Square` (== `int`) return value -- `en_passant_square`'s own
        // declared field type is `std::int8_t`, matching this same
        // file's own `halfmove_clock`/`fullmove_number` assignments
        // just below, which already cast explicitly for the identical
        // reason.
        pos.en_passant_square = static_cast<std::int8_t>(make_square(ep_str[0] - 'a', ep_str[1] - '1'));
    }

    pos.halfmove_clock = static_cast<std::uint8_t>(halfmove);
    pos.fullmove_number = static_cast<std::uint16_t>(fullmove);

    // validate_position() needs the attack tables (check detection); both
    // initializers are idempotent, same as init_zobrist_keys() below.
    init_masks();
    init_magic_bitboards();
    validate_position(pos, fen);

    init_zobrist_keys();
    pos.zobrist_hash = compute_hash(pos);

    return pos;
}

std::string to_fen(const Position& pos) {
    std::string out;

    for (int rank = 7; rank >= 0; --rank) {
        int empty_run = 0;
        for (int file = 0; file < kNumFiles; ++file) {
            const Piece p = pos.piece_at(make_square(file, rank));
            if (p == Piece::None) {
                ++empty_run;
                continue;
            }
            if (empty_run > 0) {
                out += std::to_string(empty_run);
                empty_run = 0;
            }
            out += piece_to_char(p);
        }
        if (empty_run > 0) out += std::to_string(empty_run);
        if (rank > 0) out += '/';
    }

    out += ' ';
    out += (pos.side_to_move == Color::White) ? 'w' : 'b';

    out += ' ';
    if (pos.castling_rights == 0) {
        out += '-';
    } else {
        if (pos.castling_rights & castling::kWhiteKingside) out += 'K';
        if (pos.castling_rights & castling::kWhiteQueenside) out += 'Q';
        if (pos.castling_rights & castling::kBlackKingside) out += 'k';
        if (pos.castling_rights & castling::kBlackQueenside) out += 'q';
    }

    out += ' ';
    if (pos.en_passant_square == kNoEnPassantSquare) {
        out += '-';
    } else {
        out += static_cast<char>('a' + file_of(pos.en_passant_square));
        out += static_cast<char>('1' + rank_of(pos.en_passant_square));
    }

    out += ' ';
    out += std::to_string(pos.halfmove_clock);
    out += ' ';
    out += std::to_string(pos.fullmove_number);

    return out;
}

} // namespace nightwing::board
