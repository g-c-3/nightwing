// src/board/fen.cpp
//
// See fen.h.

#include "board/fen.h"

#include <cctype>
#include <sstream>
#include <stdexcept>

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
        pos.en_passant_square = make_square(ep_str[0] - 'a', ep_str[1] - '1');
    }

    pos.halfmove_clock = static_cast<std::uint8_t>(halfmove);
    pos.fullmove_number = static_cast<std::uint16_t>(fullmove);

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
