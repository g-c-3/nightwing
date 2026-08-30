// tests/match_tests.cpp
//
// Unit tests for src/tuner/match.h/.cpp — the "strength comparison"
// half of ROADMAP.md Phase 5's final item (see match.h's own header
// comment for the full design), plus a couple of tests confirming the
// search-level eval::MaterialWeights threading this module depends on
// (search.h's search_fixed_depth()) actually works end to end, not just
// at the eval::evaluate()-call-site level tests/eval_tests.cpp already
// covers.
//
// Every MatchConfig used below uses a small `num_games`/shallow
// `search_depth`/short `max_plies` — same CI-runtime-conscious
// reasoning as tests/selfplay_tests.cpp's/tests/tune_tests.cpp's own
// header comments (each TEST_CASE is its own process, repeatedly
// calling search_fixed_depth()).

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "eval/psqt.h"
#include "search/search.h"
#include "tuner/match.h"

using namespace nightwing::board;
using namespace nightwing::eval;
using namespace nightwing::search;
using namespace nightwing::tuner;

namespace {

void init_all() {
    init_masks();
    init_magic_bitboards();
    init_zobrist_keys();
}

/// Small config shared by most cases below — real search-driven play,
/// shallow/short enough to run fast (empirically ~1 second for an
/// 8-game match at this exact configuration, confirmed directly against
/// this session's own build before writing these tests, docs/
/// DECISIONS.md).
MatchConfig small_config() {
    MatchConfig config;
    config.num_games = 8;
    config.search_depth = 2;
    config.random_opening_plies = 4;
    config.max_plies = 40;
    return config;
}

/// Returns a fully empty position (no pieces, given side to move) —
/// same helper pattern as tests/eval_tests.cpp's own (duplicated
/// locally rather than shared across test files, matching this
/// codebase's existing per-test-file convention for this exact helper).
/// A bare `Position pos;` default-construction alone is NOT safe to
/// hand-build a position from directly -- castling_rights/
/// en_passant_square aren't left in a safe state by the default
/// constructor on their own, which was confirmed the hard way while
/// writing this file's own search_fixed_depth() test below (docs/
/// DECISIONS.md, this file's introducing entry, has the full account of
/// the crash this caused before the fix).
Position empty_position(Color stm = Color::White) {
    Position pos;
    pos.side_to_move = stm;
    pos.castling_rights = 0;
    pos.en_passant_square = kNoEnPassantSquare;
    return pos;
}

} // namespace

TEST_CASE("MatchResult::score_a: standard cases", "[tuner][match]") {
    MatchResult all_wins{10, 0, 0, 10};
    REQUIRE(all_wins.score_a() == 1.0);

    MatchResult all_losses{0, 10, 0, 10};
    REQUIRE(all_losses.score_a() == 0.0);

    MatchResult all_draws{0, 0, 10, 10};
    REQUIRE(all_draws.score_a() == 0.5);

    MatchResult mixed{5, 3, 2, 10}; // 5 + 0.5*2 = 6 -> 0.6
    REQUIRE(mixed.score_a() == 0.6);
}

TEST_CASE("MatchResult::score_a: zero games played returns 0.5 rather than dividing by zero",
          "[tuner][match]") {
    MatchResult none{0, 0, 0, 0};
    REQUIRE(none.score_a() == 0.5);
}

TEST_CASE("MatchResult::elo_diff: an even match (score 0.5) has zero Elo difference",
          "[tuner][match]") {
    MatchResult even{5, 5, 0, 10};
    REQUIRE(even.elo_diff() == 0.0);
}

TEST_CASE("MatchResult::elo_diff: A winning more gives a positive difference, A winning less "
          "gives a negative one, and the two are symmetric",
          "[tuner][match]") {
    MatchResult a_ahead{7, 3, 0, 10};
    MatchResult b_ahead{3, 7, 0, 10};
    REQUIRE(a_ahead.elo_diff() > 0.0);
    REQUIRE(b_ahead.elo_diff() < 0.0);
    REQUIRE(std::abs(a_ahead.elo_diff() + b_ahead.elo_diff()) < 1e-9); // symmetric, within
                                                                        // floating-point tolerance
}

TEST_CASE("MatchResult::elo_diff: a perfect score (all wins or all losses) returns a large but "
          "FINITE value, never NaN or infinity",
          "[tuner][match]") {
    MatchResult all_wins{10, 0, 0, 10};
    MatchResult all_losses{0, 10, 0, 10};
    const double win_elo = all_wins.elo_diff();
    const double loss_elo = all_losses.elo_diff();

    REQUIRE(std::isfinite(win_elo));
    REQUIRE(std::isfinite(loss_elo));
    REQUIRE(win_elo > 0.0);
    REQUIRE(loss_elo < 0.0);
}

TEST_CASE("play_match: games_played always equals wins_a + wins_b + draws",
          "[tuner][match]") {
    init_all();
    const MaterialWeights defaults = default_material_weights();
    const MatchResult result = play_match(defaults, defaults, 1, small_config());

    REQUIRE(result.games_played == small_config().num_games);
    REQUIRE(result.wins_a + result.wins_b + result.draws == result.games_played);
}

TEST_CASE("play_match: the same base_seed always reproduces an identical result",
          "[tuner][match]") {
    init_all();
    const MaterialWeights defaults = default_material_weights();
    MaterialWeights other = defaults;
    other.knight_mg -= 15.0;
    other.knight_eg -= 15.0;

    const MatchResult result_a = play_match(defaults, other, 42, small_config());
    const MatchResult result_b = play_match(defaults, other, 42, small_config());

    REQUIRE(result_a.wins_a == result_b.wins_a);
    REQUIRE(result_a.wins_b == result_b.wins_b);
    REQUIRE(result_a.draws == result_b.draws);
}

TEST_CASE("play_match: a weight vector with catastrophically undervalued minor/major pieces "
          "never wins against the compiled-in defaults",
          "[tuner][match]") {
    init_all();
    // Every non-pawn piece worth almost nothing (1 centipawn) to the
    // "B" side -- a deliberately extreme, easy-to-reason-about distortion
    // (not a subtle real tuning scenario) chosen specifically so a SMALL,
    // fast match still gives a reliable signal. Confirmed directly
    // against this session's own build before writing this assertion
    // (docs/DECISIONS.md, this file's introducing entry) across several
    // different configs/seeds: B won zero games in every trial. Note
    // this ISN'T as one-sided a beatdown as it might sound -- search/
    // see.cpp's SEE and search/ordering.cpp's MVV-LVA both call
    // eval::material_value() WITHOUT this override (a deliberate scope
    // decision, not an oversight -- match.h's own header comment), so
    // B's search still recognizes a genuinely bad capture via SEE even
    // though its own static eval undervalues the material outright --
    // which is why B still draws a lot rather than blundering pieces
    // away outright, and why this test only asserts "never wins," not
    // "loses decisively."
    MaterialWeights crippled = default_material_weights();
    crippled.knight_mg = crippled.knight_eg = 1.0;
    crippled.bishop_mg = crippled.bishop_eg = 1.0;
    crippled.rook_mg = crippled.rook_eg = 1.0;
    crippled.queen_mg = crippled.queen_eg = 1.0;
    const MaterialWeights defaults = default_material_weights();
    const MatchResult result = play_match(defaults, crippled, 1, small_config());

    REQUIRE(result.wins_b == 0);
    REQUIRE(result.score_a() >= 0.5);
}

TEST_CASE("search_fixed_depth: a material_weights override actually changes the returned "
          "score, confirming the override reaches all the way through search, not just a "
          "raw eval::evaluate() call",
          "[tuner][match]") {
    init_all();
    // White has an extra rook -- search_fixed_depth()'s own returned
    // score should reflect a much larger White advantage when the rook
    // is worth far more than normal, confirming material_weights
    // genuinely propagates through negamax()'s own razoring/futility
    // eval::evaluate() calls and quiescence()'s stand-pat, not merely
    // accepted as a parameter and ignored.
    Position pos = empty_position();
    pos.place_piece(make_square(4, 0), Piece::WhiteKing);
    pos.place_piece(make_square(4, 7), Piece::BlackKing);
    pos.place_piece(make_square(0, 0), Piece::WhiteRook);

    const SearchResult default_result = search_fixed_depth(pos, 3, {}, nullptr);

    MaterialWeights doubled_rook = default_material_weights();
    doubled_rook.rook_mg *= 2.0;
    doubled_rook.rook_eg *= 2.0;
    const SearchResult doubled_result = search_fixed_depth(pos, 3, {}, &doubled_rook);

    REQUIRE(doubled_result.score > default_result.score);
}
