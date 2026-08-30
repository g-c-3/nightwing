#pragma once
// src/tuner/selfplay.h
//
// Self-play game generation for eval tuning — the first of this
// module's two ROADMAP.md Phase 5 sub-parts ("Texel/SPSA tuner module
// (self-play data generation + gradient descent)"). This file is the
// "self-play data generation" half. The "gradient descent" half — the
// actual Texel Tuning Method loss/optimization loop over eval's named
// tunable constants (see docs/DECISIONS.md, the 2026-08-30 "All terms
// as named tunable constants" entry) — is a separate, deliberately NOT
// YET BUILT follow-up (docs/DECISIONS.md, this file's introducing
// entry, has the full rationale for why the two halves are split
// across sessions rather than attempted together).
//
// WHAT THIS PRODUCES: engine-vs-itself games, played by this engine's
// own search_fixed_depth() (search/search.h) for both sides, with a
// short random opening (support::Xorshift64Star-seeded uniform-random
// legal moves) at the start of each game for game-to-game diversity —
// without it, every game would play the identical, fully deterministic
// line search_fixed_depth() always finds from the empty starting
// position. From each game, a subset of "quiet" positions (this file's
// own header comment below on what that means and why) are sampled and
// labeled with the game's eventual result — a (FEN, result) pair per
// sampled position — the standard training-data shape for CPW's "Texel's
// Tuning Method" (https://www.chessprogramming.org/Texel%27s_Tuning_Method),
// which the not-yet-built gradient-descent half will consume once it
// exists. This is a from-scratch implementation of that publicly
// documented technique; no code was copied from Texel or any other
// engine/tuner.
//
// RESULT CONVENTION: every `result`/`SelfPlayGame::result`/
// `SelfPlayPosition::result` in this file is from WHITE's perspective —
// 1.0 (White won), 0.5 (draw), 0.0 (Black won) — matching Texel's
// Tuning Method's own convention (CPW, same page as above) and this
// codebase's own established "white_relative" convention for a raw
// evaluate() score (search.cpp's own repeated `white_relative =
// eval::evaluate(...)` pattern) — the same perspective a later gradient-
// descent loss function will compare a White-relative eval::evaluate()
// score against directly, with no per-side sign-flip needed at that
// comparison, only at the point (already handled by this file) where a
// per-ply search score needed converting to a final per-game result.
//
// WHY "QUIET" POSITIONS, NOT EVERY PLY: CPW's own Texel's Tuning Method
// page recommends sampling positions where the position itself, not an
// imminent tactic about to resolve, is representative of what a static
// eval::evaluate() call is meant to judge — a position where the side to
// move is in check, or about to make/receive a capture, has a raw
// static eval that can be wildly unrepresentative of the position's
// true value until quiescence search (search/quiescence.h) resolves the
// tactics sitting on top of it; training a tuner against noise like
// that would teach it to fit quiescence's job, not eval's. This file's
// filter (`is_quiet_position()`, `.cpp`) only samples a ply where the
// side to move is not in check AND the move about to be played from
// that position is not a capture — a simple, standard approximation
// (not a full "run quiescence and check nothing changes" check, which
// would be far more expensive to do at every candidate ply and isn't
// needed for a first version of this tool). Positions from the random-
// opening plies at the start of each game (`SelfPlayConfig::
// random_opening_plies`) are never sampled either, regardless of
// quietness — an opening move picked uniformly at random among ALL
// legal moves (rather than a real, played opening) is not representative
// of positions this engine would actually reach in real play, and
// training a tuner against them would bias it toward evaluating
// positions well that essentially never occur outside this generator.
//
// WHY GAME-TO-GAME DIVERSITY COMES ONLY FROM THE OPENING, NOT ONGOING
// RANDOMNESS THROUGHOUT: search_fixed_depth() is itself fully
// deterministic (docs/DECISIONS.md's own established position on this,
// e.g. the transposition-table/aspiration-window entries — same
// position, same depth, same move, every time), so once a game's random
// opening moves are fixed, every move after that follows the same
// deterministic line every time this game's seed is replayed — which is
// exactly the property that makes `play_one_game()` reproducible given
// a seed (this file's own tests rely on it), at the cost of every game
// with the same seed being identical to any other run of that seed.
// Diversity across the corpus comes entirely from using a different
// seed per game (`play_games()`, below) — not runtime randomness
// injected into the search itself, which would break reproducibility
// for a single trade of no real benefit (a large enough BATCH of
// differently-seeded games already gives a diverse corpus without it).
//
// WHY A max_plies SAFETY CAP EXISTS, IN ADDITION TO CHECKMATE/
// STALEMATE/50-MOVE/THREEFOLD-REPETITION DETECTION: this codebase does
// not yet have insufficient-material draw detection (ROADMAP.md Phase 6,
// "draw detection refinement (insufficient material)" — an explicitly
// NOT YET BUILT, later item) — without it, a genuinely drawn but
// materially unbalanced endgame (e.g. KBK, KNK) that neither side's
// search recognizes as drawn could otherwise never terminate through any
// of this file's other four end conditions. Reaching `max_plies` is
// scored as a draw (0.5) — the same conservative assumption CPW's own
// Texel's Tuning Method discussion makes for adjudicated/unterminated
// games, and a safe one here specifically because it only ever fires on
// positions this codebase's own search already treats as roughly
// balanced-or-drawish by then (a genuinely winning position converts
// well within `max_plies`'s default at the depths this file uses, or
// this default would need revisiting — see SelfPlayConfig's own
// comment).

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace nightwing::tuner {

/// One sampled quiet position from a self-play game, labeled with that
/// game's eventual result. `fen` is the position BEFORE the move that
/// was actually played from it (board::to_fen() output, board/fen.h) —
/// the position a later gradient-descent loss function will call
/// eval::evaluate() on, not the position after. `result` is that game's
/// final SelfPlayGame::result, copied onto every position sampled from
/// it (see this file's header comment on the White-perspective
/// convention).
struct SelfPlayPosition {
    std::string fen;
    double result = 0.5;
};

/// One complete self-play game: every quiet position sampled from it
/// (this file's header comment on the sampling filter), the game's
/// final result, and how many plies it actually ran before terminating.
struct SelfPlayGame {
    std::vector<SelfPlayPosition> positions;

    /// Final result, White's perspective: 1.0 White won, 0.5 draw, 0.0
    /// Black won — see this file's header comment. Copied onto every
    /// entry in `positions` too, for convenience (a caller iterating
    /// over ALL positions from ALL games via write_training_data(),
    /// below, doesn't need to track which SelfPlayGame each position
    /// came from separately).
    double result = 0.5;

    /// How many plies the game actually played before terminating —
    /// diagnostic/logging use (e.g. a caller reporting average game
    /// length across a batch), not consumed by anything else in this
    /// file.
    int ply_count = 0;
};

/// Tunable knobs for self-play game generation. Every field has a
/// deliberately MODEST default — this whole tool exists to be run
/// offline, by hand, potentially for many thousands of games to build a
/// real training corpus (not something CI's own per-push test suite
/// would ever want to do at production scale) — see this file's own
/// tests (tests/selfplay_tests.cpp) for the much smaller depth/
/// max_plies values used there instead, chosen for CI runtime, not
/// training-data quality.
struct SelfPlayConfig {
    /// Passed straight through to search_fixed_depth() (search/
    /// search.h) for every non-random-opening move of the game, both
    /// sides. Deliberately fixed depth, not search_iterative_deepening()
    /// with a time budget: fixed depth keeps generation reproducible
    /// and machine-speed-independent, the identical reasoning
    /// tests/bench_tests.cpp's own header comment already gives for
    /// using search_fixed_depth() over a time-limited search in a
    /// context where reproducibility matters more than search strength
    /// per move. Default (4) is intentionally shallow — CPW's own
    /// Texel's Tuning Method discussion notes fast/shallow self-play is
    /// standard practice for tuning-corpus generation (position
    /// diversity and corpus SIZE matter more than any one game's
    /// individual move quality, unlike a real competitive game), and a
    /// shallow depth keeps a large self-play run's overall runtime
    /// practical.
    int search_depth = 4;

    /// Number of plies at the start of each game played as a uniformly
    /// random legal move rather than search_fixed_depth()'s choice —
    /// this file's own header comment on why this is the game's only
    /// source of diversity. Default (8) is deliberately short relative
    /// to a typical game's length: long enough to meaningfully diverge
    /// different seeds' games from each other and from "the" single
    /// deterministic mainline, short enough that most of a game's real
    /// (sampled) content is still normal search-driven play, not random
    /// moves — the same "a handful of random plies, then real play"
    /// shape self-play/tuning pipelines conventionally use.
    int random_opening_plies = 8;

    /// Hard game-length cap, in plies — see this file's own header
    /// comment on why this exists (insufficient-material detection not
    /// yet built) and why treating it as a draw is safe at this file's
    /// shallow default `search_depth`. 200 plies (100 full moves) is
    /// comfortably beyond a typical decisive game's length at this
    /// depth while still bounding worst-case per-game runtime for a
    /// large batch.
    int max_plies = 200;
};

/// Plays one complete self-play game from the standard starting
/// position (board::start_position(), board/board.h) and returns it —
/// see this file's header comment for the full algorithm (random
/// opening, then search_fixed_depth() for both sides until one of
/// checkmate/stalemate/50-move/threefold-repetition/`max_plies` ends
/// it) and for why the SAME `seed` always reproduces the SAME game.
///
/// Precondition: init_masks()/init_magic_bitboards()/init_zobrist_keys()
/// have all been called (board::start_position()'s and
/// search_fixed_depth()'s own transitive preconditions).
[[nodiscard]] SelfPlayGame play_one_game(std::uint64_t seed,
                                          const SelfPlayConfig& config = {});

/// Plays `num_games` self-play games, seeded `base_seed`, `base_seed +
/// 1`, ..., `base_seed + num_games - 1` — consecutive seeds rather than
/// re-deriving a fresh seed per game from a single running generator,
/// so any individual game in a large batch can be reproduced in
/// isolation later (docs/DECISIONS.md, this file's introducing entry)
/// just by knowing its index and `base_seed`, without needing to replay
/// every earlier game in the batch first to reach the same generator
/// state.
[[nodiscard]] std::vector<SelfPlayGame> play_games(int num_games, std::uint64_t base_seed,
                                                     const SelfPlayConfig& config = {});

/// Writes every sampled position from every game in `games`, one per
/// line, as `<fen>;<result>` — semicolon-separated (not space-separated
/// like a raw FEN's own internal fields, to keep the two unambiguous to
/// re-split on read) — to `out`. `result` is formatted with enough
/// decimal precision to round-trip 0.0/0.5/1.0 exactly. This is the
/// training-data file format read_training_data() (below) and the not-
/// yet-built gradient-descent tuner both consume.
void write_training_data(const std::vector<SelfPlayGame>& games, std::ostream& out);

/// Reads a file written by write_training_data() back into a flat list
/// of (fen, result) pairs (SelfPlayGame's own game-grouping/ply_count
/// aren't preserved — a gradient-descent loss function has no use for
/// which positions came from the same game, only for each position's
/// own label). Malformed lines (missing the `;` separator, or a
/// non-numeric result field) are skipped, not treated as a hard error —
/// intended to tolerate a hand-trimmed or manually-edited training file,
/// the same tolerant-of-real-world-mess spirit as most engines' own
/// PGN/EPD file readers.
[[nodiscard]] std::vector<SelfPlayPosition> read_training_data(std::istream& in);

} // namespace nightwing::tuner
