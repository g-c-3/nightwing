// src/board/zobrist.cpp
//
// See zobrist.h. Key layout:
//   - piece_square_keys[piece][square]: one key per (Piece, Square) pair
//     (12 pieces x 64 squares = 768 keys). Piece::None has no entry since
//     empty squares don't contribute to the hash.
//   - side_to_move_key: XORed in only when it's Black to move (a common
//     convention — hashing "White to move" as the absence of this key,
//     rather than giving White its own key, is equivalent and saves a key).
//   - castling_keys[4]: one per castling::k* flag, XORed in independently
//     when that right is held, rather than one key per of the 16 possible
//     rights bitmask values — simpler table, same effect since XOR of
//     independent keys is its own valid combination scheme.
//   - en_passant_file_keys[8]: one per file. XORed in whenever
//     en_passant_square is set, keyed by that square's file — this is the
//     classic/simplified approach (also used historically by engines like
//     Stockfish and Crafty): it doesn't check whether a pawn is actually
//     present to make the capture, only whether the target square is set.
//     A more precise scheme would omit the key when no capture is
//     actually available; the simplified version is what's implemented
//     here, matching common practice.

#include "board/zobrist.h"

#include <array>
#include <cstdint>

#include "support/rng.h"

namespace nightwing::board {

namespace {

constexpr int kNumPieces = 12; // Piece::WhitePawn..Piece::BlackKing (excludes None)

std::array<std::array<std::uint64_t, kNumSquares>, kNumPieces> g_piece_square_keys{};
std::uint64_t g_side_to_move_key = 0;
std::array<std::uint64_t, 4> g_castling_keys{};
std::array<std::uint64_t, kNumFiles> g_en_passant_file_keys{};
bool g_initialized = false;

} // namespace

void init_zobrist_keys() {
    if (g_initialized) {
        return;
    }

    // Fixed seed, distinct from attacks.cpp's magic-search seed: reproducible
    // keys across every run and platform, and deliberately not correlated
    // with the magic numbers (no shared state between the two generators).
    nightwing::support::Xorshift64Star rng(0x2545F4914F6CDD1DULL);

    for (auto& piece_keys : g_piece_square_keys) {
        for (auto& key : piece_keys) {
            key = rng.next();
        }
    }

    g_side_to_move_key = rng.next();

    for (auto& key : g_castling_keys) {
        key = rng.next();
    }

    for (auto& key : g_en_passant_file_keys) {
        key = rng.next();
    }

    g_initialized = true;
}

std::uint64_t compute_hash(const Position& pos) {
    std::uint64_t hash = 0;

    for (Square sq = 0; sq < kNumSquares; ++sq) {
        const Piece p = pos.piece_at(sq);
        if (p != Piece::None) {
            hash ^= g_piece_square_keys[static_cast<std::size_t>(p)][static_cast<std::size_t>(sq)];
        }
    }

    if (pos.side_to_move == Color::Black) {
        hash ^= g_side_to_move_key;
    }

    if (pos.castling_rights & castling::kWhiteKingside) hash ^= g_castling_keys[0];
    if (pos.castling_rights & castling::kWhiteQueenside) hash ^= g_castling_keys[1];
    if (pos.castling_rights & castling::kBlackKingside) hash ^= g_castling_keys[2];
    if (pos.castling_rights & castling::kBlackQueenside) hash ^= g_castling_keys[3];

    if (pos.en_passant_square != kNoEnPassantSquare) {
        const int file = file_of(pos.en_passant_square);
        hash ^= g_en_passant_file_keys[static_cast<std::size_t>(file)];
    }

    return hash;
}

std::uint64_t piece_square_key(Piece p, Square sq) noexcept {
    return g_piece_square_keys[static_cast<std::size_t>(p)][static_cast<std::size_t>(sq)];
}

std::uint64_t side_to_move_key() noexcept { return g_side_to_move_key; }

std::uint64_t castling_right_key(std::uint8_t right) noexcept {
    switch (right) {
        case castling::kWhiteKingside: return g_castling_keys[0];
        case castling::kWhiteQueenside: return g_castling_keys[1];
        case castling::kBlackKingside: return g_castling_keys[2];
        case castling::kBlackQueenside: return g_castling_keys[3];
        default: return 0; // precondition violated: not a single castling::k* flag
    }
}

std::uint64_t en_passant_file_key(int file) noexcept {
    return g_en_passant_file_keys[static_cast<std::size_t>(file)];
}

std::uint64_t compute_pawn_hash(const Position& pos) noexcept {
    std::uint64_t hash = 0;

    for (const Color c : {Color::White, Color::Black}) {
        Bitboard pawns = pos.pieces(c, PieceType::Pawn);
        while (pawns != 0) {
            const Square sq = pop_lsb(pawns);
            hash ^= g_piece_square_keys[static_cast<std::size_t>(make_piece(c, PieceType::Pawn))]
                                        [static_cast<std::size_t>(sq)];
        }
    }

    return hash;
}

} // namespace nightwing::board
