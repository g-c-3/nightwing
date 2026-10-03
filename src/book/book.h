#pragma once
// src/book/book.h
//
// A small, curated opening book (ROADMAP.md Phase 6's optional,
// low-priority item: "small curated opening book"). Book entries are
// well-established, sound main-line opening theory (Ruy Lopez, Italian
// Game, Sicilian, French, Caro-Kann, Queen's Gambit, English, etc.) —
// this is NOT an attempt at comprehensive opening coverage, extensive
// sideline/trap knowledge, or anything Texel-tuned or learned; it
// exists purely so the engine doesn't have to re-derive well-known,
// uncontroversial opening moves via search every single game, the same
// practical reasoning essentially every UCI-compatible engine's own
// optional book support is built on.
//
// UNLIKE this project's own Syzygy/tablebase prohibition (a hard
// constraint — no exception, ever, for any reason), an opening book is
// explicitly NOT in that category: it doesn't replace the engine's own
// judgment anywhere except a handful of well-known opening moves, and
// falls through to ordinary search the moment the game leaves book —
// see ROADMAP.md's own framing of the two as clearly different things,
// and the person's own question this session that this file's header
// comment is partly answering.
//
// DESIGN: book entries are stored as plain UCI long-algebraic move
// sequences from the start position (curated_lines(), book.cpp) —
// deliberately NOT hand-computed Zobrist hashes, which would be
// error-prone and unverifiable by inspection. init_book() (below)
// replays each line through real legal move generation once at
// startup, deriving the actual Zobrist hash of every intermediate
// position the same way any other part of this engine would compute
// it, and only THEN builds the hash-keyed lookup table book_move()
// actually queries. This guarantees every book hash is correct by
// construction — it can never silently drift from what the rest of
// the engine would compute for the same position, the way a
// hand-maintained table of raw hash values could.
//
// Disabling the book: book_move() itself is unconditional — it answers
// for any known book position and has no on/off state of its own. The
// engine's `OwnBook` UCI option (a standard `check` option, default
// `true`) is applied by the CALLER: src/uci/uci.cpp's start_go() only
// consults book_move() when that option is on, so
// `setoption name OwnBook value false` makes every `go` run a real
// search even on a book position (see docs/DECISIONS.md, 2026-10-02 (2),
// which supersedes the earlier 2026-09-03 (4) decision to defer such a
// toggle, back when no options infrastructure existed).

#include <cstdint>
#include <optional>
#include <string>

#include "board/board.h"

namespace nightwing::book {

/// Builds the internal position -> move lookup table by replaying
/// every curated opening line (book.cpp's own curated_lines()) from
/// board::start_position(), using real legal move generation to derive
/// each intermediate position's actual Zobrist hash. MUST be called
/// exactly once, after board::init_masks() -> board::
/// init_magic_bitboards() -> board::init_zobrist_keys() (this
/// function's own move replay needs working legal move generation,
/// which needs magic bitboards) and before any book_move() call —
/// mirrors ARCHITECTURE.md's own mandatory board-subsystem startup
/// order, with this as one more, final step appended to it
/// specifically for programs (src/main.cpp) that want book support;
/// tests that don't call this function simply get book_move() ==
/// std::nullopt for everything, which is always a safe, correct
/// fallback (falls straight through to ordinary search).
void init_book();

/// Returns a book move for `pos`, as a UCI long-algebraic string (e.g.
/// "e2e4") exactly as board::Move::to_uci() would format it — matching
/// src/uci/uci.cpp's own apply_uci_moves() convention for how a UCI
/// move string gets turned back into a real, legal board::Move (regen
/// the legal move list, match by to_uci() string) — or std::nullopt if
/// `pos` isn't a known book position, including if init_book() was
/// never called (an empty table is a safe, correct "no book" state,
/// not a precondition violation).
[[nodiscard]] std::optional<std::string> book_move(const board::Position& pos) noexcept;

} // namespace nightwing::book
