#pragma once
// src/board/fen.h
//
// FEN (Forsyth-Edwards Notation) parsing and serialization. This is I/O-
// layer code — used to build test positions (perft reference positions
// are conventionally given as FEN) and, later, the UCI `position fen ...`
// command — not the search/movegen/eval hot path, so unlike those
// (ARCHITECTURE.md: "No exceptions ... in hot search/eval/movegen paths
// ... error handling in UCI/IO layers only"), parse_fen() uses exceptions
// for malformed input rather than a fallible return type.

#include <string>

#include "board/board.h"

namespace nightwing::board {

/// Parses a FEN string into a Position: piece placement, side to move,
/// castling rights, en passant target, halfmove clock, and fullmove
/// number. The halfmove clock and fullmove number fields are optional
/// (some FEN sources omit them) and default to 0 and 1 respectively when
/// absent, matching common practice. Sets zobrist_hash via
/// compute_hash() before returning, so the result is immediately usable
/// (init_zobrist_keys() is called internally, idempotently, same as
/// start_position()).
/// Throws std::invalid_argument if `fen` is malformed (wrong number of
/// ranks/files, unrecognized characters, missing required fields).
[[nodiscard]] Position parse_fen(const std::string& fen);

/// Serializes `pos` back to a FEN string. Round-trips with parse_fen()
/// for any Position parse_fen() could have produced (parse_fen(to_fen(p))
/// reproduces the same board state), used in tests to cross-check the
/// parser without hand-authoring a separate expected-string per case.
[[nodiscard]] std::string to_fen(const Position& pos);

} // namespace nightwing::board
