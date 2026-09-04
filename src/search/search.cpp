// src/search/search.cpp

#include "search/search.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <new>
#include <span>
#include <thread>
#include <utility>

#include "board/movegen.h"
#include "eval/endgame.h"
#include "eval/eval.h"
#include "eval/eval_cache.h"
#include "eval/pawn_tt.h"
#include "search/ordering.h"
#include "search/quiescence.h"
#include "search/tt.h"

namespace nightwing::search {
namespace {

using board::Color;
using board::Move;
using board::MoveList;
using board::Position;
using board::UndoInfo;

/// Alpha/beta search bound, kept well below kMateScore's already-huge
/// magnitude so that negating it (`-alpha`, `-beta` on recursive calls,
/// per the negamax convention) never risks integer overflow — a fixed,
/// generous constant rather than std::numeric_limits<int>::max(), whose
/// negation is undefined behavior.
constexpr int kInfinity = 1'000'000;

/// Placeholder default pawn hash table size, same lifetime caveat as
/// kDefaultTTSizeMB just above -- KB, not MB, matching eval::PawnHashTable's
/// constructor (eval/pawn_tt.h's header comment on why this table is
/// sized much smaller than the main TT).
constexpr std::size_t kDefaultPawnTTSizeKB = 512;

/// Placeholder default eval cache size, same lifetime caveat as
/// kDefaultTTSizeMB/kDefaultPawnTTSizeKB just above -- KB, matching
/// eval::EvalCache's constructor (eval/eval_cache.h's header comment).
/// Sized larger than kDefaultPawnTTSizeKB deliberately: this cache is
/// keyed on the full position rather than pawn placement alone, so its
/// useful working set (however many distinct positions get evaluated
/// more than once within one search, via transpositions or the same
/// node's own razoring+futility double-evaluate() -- eval_cache.h's
/// header comment) isn't naturally bounded the tight way pawn structure
/// alone is; still deliberately far smaller than kDefaultTTSizeMB,
/// matching this cache's own "optional supplement, not the primary
/// cache" scope (ROADMAP.md's own "separate from TT" wording).
constexpr std::size_t kDefaultEvalCacheSizeKB = 2048;

/// Constructs a TranspositionTable sized to `requested_mb` megabytes,
/// falling back to progressively smaller sizes (halving each time) if
/// the allocation itself throws std::bad_alloc, rather than letting
/// that exception propagate and crash the engine -- a real, reachable
/// failure mode now that the UCI `Hash` option (ROADMAP.md Phase 8,
/// src/uci/uci.cpp's own kMinHashMB/kMaxHashMB) lets a GUI/script
/// request a table up to kMaxHashMB's own ceiling, which a genuinely
/// memory-constrained machine may not be able to satisfy even though the
/// value itself is within the option's advertised bounds -- an
/// in-range `setoption` value crashing the engine on a small machine
/// would be exactly the kind of surprising failure this project's
/// existing "malformed/out-of-range input degrades gracefully, never
/// crashes" convention (handle_setoption()'s own doc comment, uci.cpp)
/// already applies to every OTHER option; this is that same convention
/// extended to a value that's syntactically fine but too large for the
/// actual machine to satisfy, discovered directly by this session's own
/// test suite (a REQUIRE(...) fed a large, in-bounds Hash value
/// throwing std::bad_alloc uncaught, before this function existed).
/// Halves down to a hard floor of 1 MB before giving up and letting the
/// exception propagate after all -- TranspositionTable's own
/// constructor doc comment (tt.h) already guarantees a 1 MB request
/// yields a genuinely usable (if minimal) 1-bucket table rather than an
/// empty one, so 1 MB is a safe, meaningful place to stop trying
/// smaller sizes; a machine unable to allocate even that is too
/// memory-constrained to run this engine at all, a case worth letting
/// fail loudly rather than silently pretending to succeed.
///
/// IMPORTANT LIMITATION, discovered via real CI evidence (GitHub
/// Actions run 91820797115, after this function was first written):
/// this fallback ONLY helps when allocation failure actually surfaces
/// as a catchable std::bad_alloc, which is not guaranteed. Under ASan
/// (this project's Linux/macOS Debug builds -- CMakeLists.txt),
/// `allocator_may_return_null=0` is ASan's own default, so a failed
/// allocation calls ASan's own `Die()` and aborts the process directly
/// -- std::bad_alloc is never thrown, so this function's catch block
/// never runs. On a genuinely memory-constrained machine without ASan,
/// the OS's own out-of-memory killer can send an uncatchable SIGKILL
/// before malloc ever reports failure at all. Both were observed
/// directly on this project's own CI (Linux Debug: ASan's
/// "out-of-memory" abort; macOS Debug/Release: OS OOM-kill, 157 sec and
/// 32 sec in respectively) after `kMaxHashMB` was originally set to 64
/// GiB -- this function's fallback did NOT help in any of the three
/// failing cases; only lowering `kMaxHashMB` itself (src/uci/uci.cpp's
/// own doc comment on that constant has the full account) actually
/// fixed it. This function remains genuinely useful defense-in-depth
/// for the failure modes it CAN catch (e.g. a Release/non-ASan build
/// where malloc failure is reported normally), but is not, by itself,
/// a substitute for keeping `kMaxHashMB` realistic for the machines
/// this engine actually runs on.
[[nodiscard]] TranspositionTable make_transposition_table(std::size_t requested_mb) {
    std::size_t size_mb = requested_mb < 1 ? 1 : requested_mb;
    while (true) {
        try {
            return TranspositionTable(size_mb);
        } catch (const std::bad_alloc&) {
            if (size_mb <= 1) {
                throw; // Nothing smaller left to try -- let it propagate.
            }
            size_mb /= 2;
            if (size_mb < 1) {
                size_mb = 1;
            }
        }
    }
}

/// Starting half-width (centipawns) of the aspiration window
/// search_iterative_deepening() centers on the previous iteration's
/// score for depth >= 2 (CPW "Aspiration Windows"; see that function's
/// comments for the full scheme). Doubled on each fail-high/fail-low
/// retry. 25 centipawns is a common, reasonable starting point in
/// other engines' implementations of this technique -- not yet tuned
/// for Nightwing specifically (ROADMAP.md's Texel/SPSA tuner, once it
/// exists, targets eval terms first; a search constant like this one
/// is a plausible later tuning target, not a claim that 25 is already
/// verified optimal here).
constexpr int kAspirationInitialDelta = 25;

/// Internal Iterative Reduction (IIR) thresholds: a node with no
/// transposition-table entry at all has no move-ordering hint (see
/// negamax()'s header comment for the full technique). Only applied
/// beyond this minimum remaining depth (below it, the effect isn't
/// meaningful and isn't worth risking), reducing the effective depth
/// searched at that node by this many plies. Both are common, modest
/// starting values in other engines' IIR implementations -- not yet
/// tuned for Nightwing specifically, same caveat as
/// kAspirationInitialDelta above.
constexpr int kIIRMinDepth = 4;
constexpr int kIIRReduction = 1;

/// Null-move pruning (CPW "Null Move Pruning", negamax()'s NMP block
/// below) constants. Not yet tuned (ROADMAP.md Phase 5's Texel/SPSA
/// tuner is scoped for eval terms specifically; a future session
/// extending it, or a separate search-parameter tuning pass, may revise
/// these) -- a simple, well-established two-tier reduction scheme
/// common across many "classical" engines, chosen for this first-draft
/// implementation over a smoother adaptive formula (e.g. `3 + depth/6`)
/// specifically to keep the logic easy to hand-verify without a
/// compiler available.
constexpr int kNullMoveMinDepth = 3;        // don't bother below this remaining depth
constexpr int kNullMoveReduction = 2;       // R for depth < kNullMoveBigReductionDepth
constexpr int kNullMoveBigReductionDepth = 6;
constexpr int kNullMoveBigReduction = 3;    // R for depth >= kNullMoveBigReductionDepth

/// Zugzwang-aware null-move bias (ROADMAP.md Phase 6's "Zugzwang-aware
/// search shaping" item; see eval/endgame.h's own is_zugzwang_prone()
/// doc comment for which material signatures this applies to and why).
/// Subtracted from whichever of kNullMoveReduction/kNullMoveBigReduction
/// the NMP block below would otherwise use, at any node
/// eval::is_zugzwang_prone() flags -- giving the null-move probe MORE
/// remaining depth (a smaller R) there, so it searches closer to what a
/// real move's own child would see rather than trusting as shallow a
/// probe as usual, without disabling null-move pruning outright the way
/// the pre-existing `non_pawn_material` guard does for the stronger,
/// unsound-not-just-risky KPK case. Never taken below
/// kZugzwangMinReduction -- R must stay at least 1 for the null-move
/// probe's own depth formula (`depth - 1 - reduction`) to still mean
/// anything as a reduced-depth search rather than a full-depth one in
/// disguise. Not yet tuned, same caveat as kNullMoveReduction/
/// kNullMoveBigReduction above.
constexpr int kZugzwangReductionDecrease = 1;
constexpr int kZugzwangMinReduction = 1;

/// Late move reductions (CPW "Late Move Reductions", negamax()'s move
/// loop below) constants. Same "simple, hand-verifiable two-tier
/// scheme over a smoother formula" choice as the null-move-pruning
/// constants just above, and the same not-yet-tuned status.
constexpr int kLMRMinDepth = 3;        // don't bother below this remaining depth
constexpr int kLMRMinMoveIndex = 4;    // don't reduce the first few (already well-ordered) moves
constexpr int kLMRReduction = 1;       // R for depth < kLMRBigReductionDepth
constexpr int kLMRBigReductionDepth = 6;
constexpr int kLMRBigReduction = 2;    // R for depth >= kLMRBigReductionDepth

/// Late move pruning (LMP) / move-count based pruning (CPW "Move Count
/// Based Pruning", negamax()'s move loop below) constants. Only applies
/// at shallow remaining depth (kLMPMaxDepth) -- the technique's own
/// premise, "this many other candidates already failed to help, so one
/// more deep into an already-long tail almost certainly won't either,"
/// gets weaker the more plies of search remain to prove that wrong.
/// kLMPMoveCountLimits is a fixed lookup table (index 0 unused --
/// negamax() never reaches its move loop with depth <= 0, see the
/// quiescence delegation at the top of this function -- kept only so
/// kLMPMoveCountLimits[depth] reads directly, no off-by-one offset)
/// rather than a formula, matching this project's existing preference
/// (LMR/NMP's own two-tier constants above) for something exactly
/// hand-verifiable at each depth without a compiler available. Roughly
/// quadratic growth, a common shape for this technique in other
/// engines -- not yet tuned for Nightwing specifically, same caveat as
/// every other first-draft pruning constant in this file.
constexpr int kLMPMaxDepth = 8;
constexpr std::array<int, kLMPMaxDepth + 1> kLMPMoveCountLimits = {
    0, 5, 8, 13, 18, 25, 32, 41, 50,
};

/// History pruning (CPW "History Leaf Pruning" / move loop below)
/// constants: distinct from, and checked alongside, LMP -- LMP's skip
/// is purely a function of HOW MANY quiet moves have already been
/// tried (`quiets_tried`), while this is a function of a specific
/// move's own accumulated `HistoryTable` score (search/ordering.h),
/// regardless of its position in the list. `order_moves()` already
/// sorts quiet moves by descending history score, so in practice a
/// move failing this threshold tends to sit late in the quiet tail
/// anyway -- but this check can trigger independently of (and
/// potentially earlier than) LMP's own move-count threshold at very
/// shallow depth, when even an early quiet candidate's history score is
/// poor enough. kHistoryPruningThresholds is a fixed lookup table
/// (index 0 unused, same convention as kLMPMoveCountLimits above),
/// decreasing with depth (more aggressive right at the shallowest
/// depth, near-disabled by kHistoryPruningMaxDepth's own ceiling) --
/// same hand-verification-without-a-compiler reasoning as every other
/// pruning constant in this file, not yet tuned for Nightwing
/// specifically.
constexpr int kHistoryPruningMaxDepth = 3;
constexpr std::array<int, kHistoryPruningMaxDepth + 1> kHistoryPruningThresholds = {
    0, 300, 150, 50,
};

/// Futility pruning (CPW "Futility Pruning", negamax()'s move loop
/// below) constants. At shallow remaining depth, if the node's own
/// static evaluation (computed once, before the move loop -- the value
/// doesn't depend on which move is being considered) is already so far
/// below alpha that even a generous per-depth margin couldn't plausibly
/// close the gap, quiet, non-check-giving moves at this node are
/// skipped outright rather than searched -- CPW's own stated caveat
/// applies (`static_eval` is a rough proxy, not the move's real
/// post-move value, so this is a heuristic, not provably exact).
/// kFutilityMargins is a fixed lookup table (index 0 unused, same
/// reasoning as kLMPMoveCountLimits just above) rather than a formula,
/// for the same hand-verification-without-a-compiler reasoning as
/// every other pruning constant in this file. Deliberately narrow
/// (kFutilityMaxDepth = 3, CPW's own "frontier"/near-frontier range) --
/// the margin's job is bounding how much a quiet move could plausibly
/// swing the eval by remaining-depth plies of further search, and that
/// bound gets far less trustworthy the deeper the remaining search is.
/// Linear margin growth (100cp per remaining ply) rather than a
/// quadratic or exponential shape, matching the project's existing
/// preference for the simplest scheme that's still a real, documented
/// first draft -- not yet tuned for Nightwing specifically, same
/// caveat as every other constant in this file.
constexpr int kFutilityMaxDepth = 3;
constexpr std::array<int, kFutilityMaxDepth + 1> kFutilityMargins = {
    0, 100, 200, 300,
};

/// Razoring (CPW "Razoring", negamax()'s own node-level check right
/// after the NMP block below) constants. Similar shape to futility
/// pruning's margins above -- a fixed lookup table, index 0 unused, not
/// yet tuned -- but deliberately wider at every depth: razoring is a
/// more drastic decision than futility (it drops the ENTIRE node
/// straight into quiescence search instead of running the normal move
/// loop at all, not just skipping individual late quiet moves), so it
/// needs stronger evidence -- a bigger gap between static eval and
/// alpha -- before it's worth trusting. kRazorMaxDepth (3) matches
/// kFutilityMaxDepth's own shallow-only scope, for the same reason: a
/// static eval's margin is a much weaker signal about what deeper
/// search might still find the further from the leaves it's applied.
constexpr int kRazorMaxDepth = 3;
constexpr std::array<int, kRazorMaxDepth + 1> kRazorMargins = {
    0, 300, 400, 500,
};

/// ProbCut (CPW "ProbCut", negamax()'s own node-level check just before
/// the main move loop below) constants. Opposite end of the depth
/// spectrum from futility/razoring above: those apply near the leaves
/// (shallow remaining depth, looking for a reason to fail LOW early);
/// ProbCut applies at moderate-to-high remaining depth (kProbCutMinDepth
/// and up), looking for a reason to fail HIGH early -- a cheap, reduced
/// -depth verification search against a raised window
/// (`beta + kProbCutMargin`) that, if it also fails high, is taken as
/// strong evidence a full-depth search would too, without paying for
/// one. kProbCutReduction (4) is deliberately large relative to LMR's
/// own kLMRReduction/kLMRBigReduction (1-2) above -- ProbCut's
/// verification search only needs to be roughly right about "is this
/// position winning by at least a large, specific margin," not compute
/// an exact score, so a much shallower probe is an acceptable trade for
/// this technique specifically, the same way NMP's own R (this file's
/// kNullMoveReduction) is chosen independently of LMR's. Not yet tuned
/// for Nightwing specifically, same caveat as every other constant in
/// this file.
constexpr int kProbCutMinDepth = 5;
constexpr int kProbCutMargin = 200; // centipawns
constexpr int kProbCutReduction = 4;

/// Check extensions (CPW "Check Extensions", negamax()'s move loop
/// below) constant: the FIRST extension technique added to this file
/// (every earlier Phase 4 addition was a pruning/reduction technique
/// that removes search depth, never adds it; singular extensions below
/// are this file's second) -- when a move gives check, the child search
/// below it is granted one extra ply (`depth - 1 + kCheckExtensionPly`
/// instead of the usual `depth - 1`) rather than losing one, on the
/// premise that a forced check-response sequence is exactly the kind of
/// tactical line the horizon effect (this file's own quiescence()-
/// related header comments) is most likely to misjudge if cut off one
/// ply too early -- and a position that's just been put in check has,
/// structurally, far fewer legal replies than an ordinary position, so
/// the extra ply doesn't cost nearly as much branching as it would
/// anywhere else in the tree.
constexpr int kCheckExtensionPly = 1;

/// Singular extensions (CPW "Singular Extensions", negamax()'s move
/// loop below, evaluated only for the TT move specifically) constants.
/// Unlike check extensions (a cheap, purely local decision -- just look
/// at whether the move gives check), this technique pays for a real,
/// reduced-depth VERIFICATION search of every OTHER legal move at the
/// node, all under a narrow window built from the TT's own previously-
/// stored score, before deciding whether the TT move is "singular" --
/// so much better than every alternative that none of them can even
/// approach a score just below it. If none can, the TT move is
/// considered forced/critical enough to deserve an extra ply of its own
/// child search, on the premise that a position with only one genuinely
/// good move is exactly where a shallow search is most likely to
/// misjudge how forced the line actually is. Deliberately expensive
/// (an entire extra search per eligible node), so gated behind several
/// guards: `kSingularMinDepth` (8) -- shallow nodes can't afford to pay
/// for a verification search at all; `kSingularTTDepthMargin` (3) -- the
/// TT entry itself must be from a search deep enough to trust (`probe.
/// depth >= depth - kSingularTTDepthMargin`), or its stored score isn't
/// a reliable enough baseline to build a verification window from;
/// `kSingularMarginPerPly` (2) -- `singular_beta = probe.score -
/// kSingularMarginPerPly * depth`, a margin that widens with depth
/// (rather than a fixed lookup table the way most of this file's other
/// margins are -- depth here can range from kSingularMinDepth up
/// through however deep iterative deepening goes, too wide a range for
/// a hand-verifiable table the way kFutilityMargins/kRazorMargins could
/// afford with their own narrow, capped depth ranges); `kSingularDepth
/// Divisor` (2) -- the verification search itself runs at `(depth - 1)
/// / kSingularDepthDivisor`, roughly half the remaining depth, cheap
/// enough to be worth paying for while still deep enough to mean
/// something. `kSingularExtensionPly` (1) -- same size as check
/// extensions' own bonus; combined via `std::max()`, not summed, with
/// any check-extension bonus the same move might also separately
/// qualify for (this function's move loop), so a move never gets
/// double-extended for two different reasons at once.
constexpr int kSingularMinDepth = 8;
constexpr int kSingularTTDepthMargin = 3;
constexpr int kSingularMarginPerPly = 2;
constexpr int kSingularDepthDivisor = 2;
constexpr int kSingularExtensionPly = 1;

// Mid-search time-budget interruption (ROADMAP.md Priority Fix,
// promoted from the Phase 2 scope cut this comment used to describe,
// once its own documented revisit trigger -- real wtime/btime/movetime
// UCI parsing -- had been met for some time without the revisit
// happening; see docs/DECISIONS.md's 2026-08-13 and external-code-
// review entries). negamax()/quiescence() (search.h's SearchLimits,
// quiescence.cpp) now check a shared stop flag/deadline periodically,
// by node count, rather than search_iterative_deepening() (below)
// only ever checking *between* full-depth search calls the way it
// used to exclusively. Checked only every this many nodes -- a
// std::chrono::steady_clock::now() call is not free (ARCHITECTURE.md
// "Hot-Path Code Practices": measure, don't guess, but a syscall-
// backed clock read on every single node would be an obviously bad
// trade against a node's own, much cheaper, usual cost) -- must be a
// power of two so the check is a single bitwise AND, not a modulo.
constexpr std::uint64_t kTimeCheckNodeInterval = 2048;
constexpr std::uint64_t kTimeCheckNodeMask = kTimeCheckNodeInterval - 1;

/// Returns true if `pos.side_to_move`'s king is currently attacked —
/// used to distinguish checkmate from stalemate when
/// generate_legal_moves() returns no moves, since movegen itself
/// doesn't report that distinction directly (see board/movegen.h).
[[nodiscard]] bool in_check(const Position& pos) noexcept {
    const Color us = pos.side_to_move;
    const board::Square king_sq =
        board::bitscan_forward(pos.pieces(us, board::PieceType::King));
    return board::is_square_attacked(pos, king_sq, board::opposite(us));
}

/// Returns true if `pos` (whose Zobrist hash is `key`, passed separately
/// since every caller already has it on hand) should be scored as an
/// immediate draw for search purposes, checking two independent rules:
///
/// 50-move rule (CPW "50-move Rule"): `pos.halfmove_clock >= 100` (100
/// plies = 50 full moves since the last pawn move or capture) is treated
/// as an automatic draw, the near-universal engine convention — real
/// FIDE play requires a player to *claim* the draw, but essentially
/// every UCI engine scores this as an unconditional draw during search
/// rather than modeling the claim itself, and this implementation does
/// the same.
///
/// Repetition (CPW "Repetition Detection"): scored as a draw on a
/// position's SECOND occurrence within reach of this node, not
/// waiting for an actual third — standard engine practice, since once a
/// position has recurred once, a side can force a real threefold simply
/// by repeating it again, so the second occurrence is already
/// draw-equivalent information for search purposes. Only ever looks
/// back up to `pos.halfmove_clock` plies: a position from before the
/// most recent pawn move or capture can never recur (any recurrence
/// would itself have to cross that same irreversible move, which is
/// impossible), so this bounds the lookback naturally without needing
/// to track exactly how far `game_history`/`path` extend.
///
/// `game_history` is every ancestor position's hash strictly before the
/// search root (see search.h's header comment) — NOT including the
/// root. `path[0]` is the root's own hash (written once by
/// search_root(), see its comments) and `path[p]` for p in [1, ply) is
/// the hash negamax() itself recorded at every shallower ply of the
/// current root-to-here search path (see negamax()'s header comment) —
/// together, `game_history` followed by `path[0..ply]` form one
/// continuous, chronologically-ordered sequence ending at this node,
/// walked backward here without ever materializing it as a single
/// combined array.
/// Returns true if `sq` is a light square. Re-derived locally rather
/// than shared -- matches this codebase's established per-file
/// convention for this exact computation (eval/endgame.cpp's and
/// eval/minor_piece_endgame.cpp's own identical local helpers).
[[nodiscard]] constexpr bool is_light_square(board::Square sq) noexcept {
    return ((board::file_of(sq) + board::rank_of(sq)) % 2) == 1;
}

/// Returns true if neither side has enough material to force
/// checkmate against ANY defense, cooperative or not -- FIDE Article
/// 5.2.2's "dead position" concept, restricted to the standard,
/// conservative material-only subset every serious chess engine
/// checks (ROADMAP.md Phase 6's "draw detection refinement
/// (insufficient material)" item). Three cases, all provably dead
/// regardless of play:
/// 1. Bare king vs. bare king.
/// 2. King and a single minor piece (one knight OR one bishop) vs.
///    bare king, either side -- a lone minor piece can never deliver
///    checkmate together with its own king, period, regardless of
///    what (if anything) the opponent has.
/// 3. King and bishop vs. king and bishop, where BOTH bishops stand on
///    the SAME square color.
///
/// Deliberately narrower than it might first appear worth being:
/// king+minor vs. king+minor combinations OTHER than same-colored
/// bishops (knight vs. knight, bishop vs. knight, or bishop vs. bishop
/// on OPPOSITE colors) are NOT included here, even though neither
/// side's own lone minor can unilaterally force mate either. This is
/// deliberate, not an oversight: chess problem composers have
/// constructed legal (if wildly impractical) helpmates in exactly
/// those combinations, where the DEFENDING side's own piece
/// cooperates to trap its own king -- meaning a checkmate is reachable
/// by SOME legal move sequence, even though no side can ever FORCE
/// one. FIDE's own Article 5.2.2 requires NO sequence of legal moves,
/// cooperative or not, to reach checkmate -- same-colored bishops
/// genuinely satisfy that (no helpmate construction is possible: the
/// bishops can never contest the squares needed to trap a king when
/// confined to one color complex each), while every combination left
/// out above does not, strictly speaking, satisfy it. This matches the
/// same standard, conservative convention essentially every serious
/// chess engine and rules implementation already uses -- opposite-
/// colored bishops, knight-vs-knight, and bishop-vs-knight combinations
/// are correctly left to the 50-move rule (is_draw_by_rule()'s other
/// check, just below) rather than an incorrect blanket auto-draw here.
///
/// Deliberately does NOT attempt the fully general "no sequence of
/// legal moves can create a checkmate" test beyond these three
/// material-only cases -- that general test can also depend on
/// king/piece placement even with objectively adequate mating material
/// (e.g. certain fortress-adjacent shapes), a genuinely different and
/// much harder problem than this function's narrow, well-established
/// material-only scope.
[[nodiscard]] bool is_insufficient_material(const Position& pos) noexcept {
    using board::PieceType;
    // Any pawn, rook, or queen anywhere rules this out immediately --
    // a lone rook or queen already suffices to force mate by itself,
    // and a pawn can always eventually promote into one.
    if (pos.pieces(Color::White, PieceType::Pawn) != 0 ||
        pos.pieces(Color::Black, PieceType::Pawn) != 0 ||
        pos.pieces(Color::White, PieceType::Rook) != 0 ||
        pos.pieces(Color::Black, PieceType::Rook) != 0 ||
        pos.pieces(Color::White, PieceType::Queen) != 0 ||
        pos.pieces(Color::Black, PieceType::Queen) != 0) {
        return false;
    }

    const int white_knights = board::popcount(pos.pieces(Color::White, PieceType::Knight));
    const int black_knights = board::popcount(pos.pieces(Color::Black, PieceType::Knight));
    const int white_bishops = board::popcount(pos.pieces(Color::White, PieceType::Bishop));
    const int black_bishops = board::popcount(pos.pieces(Color::Black, PieceType::Bishop));
    const int total_minors = white_knights + black_knights + white_bishops + black_bishops;

    if (total_minors <= 1) {
        return true; // bare kings, or king + single minor vs. bare king
    }
    if (total_minors == 2 && white_bishops == 1 && black_bishops == 1) {
        const board::Square white_bishop_sq =
            board::bitscan_forward(pos.pieces(Color::White, PieceType::Bishop));
        const board::Square black_bishop_sq =
            board::bitscan_forward(pos.pieces(Color::Black, PieceType::Bishop));
        return is_light_square(white_bishop_sq) == is_light_square(black_bishop_sq);
    }
    return false;
}

[[nodiscard]] bool is_draw_by_rule(const Position& pos, std::uint64_t key, int ply,
                                    std::span<const std::uint64_t> game_history,
                                    const std::array<std::uint64_t, kMaxPly>& path) noexcept {
    if (pos.halfmove_clock >= 100) {
        return true;
    }
    if (is_insufficient_material(pos)) {
        return true;
    }
    if (ply >= kMaxPly) {
        // Comfortably beyond any depth this engine reaches in practice
        // today (search/ordering.h's kMaxPly header comment) -- skip
        // repetition tracking here rather than risk an out-of-bounds
        // `path` access below; the 50-move check above still applies
        // regardless.
        return false;
    }

    const int history_len = static_cast<int>(game_history.size());
    const int cur = history_len + ply; // this node's own index in the conceptual combined sequence
    const int lookback = pos.halfmove_clock;

    for (int steps = 1; steps <= lookback; ++steps) {
        const int idx = cur - steps;
        if (idx < 0) {
            break; // Walked past all available history -- nothing further to check.
        }
        const std::uint64_t candidate = idx >= history_len
                                             ? path[static_cast<std::size_t>(idx - history_len)]
                                             : game_history[static_cast<std::size_t>(idx)];
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

/// Repetition, 50-move-rule, AND insufficient-material detection (see
/// is_draw_by_rule() just above -- the third of these is
/// is_insufficient_material(), just above that) is checked immediately
/// after the node-count increment, before even mate distance pruning's
/// clamp — deliberately the very first thing this function does at a
/// real (depth >= 1) node. Two reasons for going first: it's among the
/// cheapest possible checks (no movegen, no TT probe -- the
/// insufficient-material check is a handful of popcount() calls, the
/// repetition walk is bounded by halfmove_clock), and — more
/// importantly — a position that's a draw by repetition was very
/// possibly reached through a *different* path last time the search
/// (or a previous iterative-deepening iteration) visited
/// this same Zobrist key, so any cached TT entry for it isn't safe to
/// trust here (the well-known "Graph History Interaction" problem: a TT
/// entry doesn't know which path reached it). Checking and returning
/// before the TT probe below means a draw-by-repetition/50-move score is
/// never looked up from a stale, wrong-context TT entry — and, since
/// this function returns immediately when it applies, such a score is
/// also never itself stored into the TT (the store happens at the
/// bottom of this function, which this early return skips entirely),
/// so a real, path-independent evaluation of this same key from a
/// different path is never at risk of being overwritten by a
/// path-dependent draw score either. `game_history`/`path` are passed
/// through unchanged to every recursive call below, and `path[ply]` is
/// written right after `key` is computed (see below) so a DEEPER node's
/// own is_draw_by_rule() call can see this node as part of its walk
/// back — this reuses `path` as a single flat, per-ply array the same
/// way search/ordering.h's KillerTable does (ARCHITECTURE.md "Memory &
/// Cache": no heap allocation for per-node search state), overwritten
/// as the search backtracks and descends a different line, which is
/// correct precisely because only the currently-active root-to-here
/// path is ever read backward from, never a stale sibling's leftover
/// entry at the same ply.
///
/// Check extensions (CPW "Check Extensions", the move loop below) are
/// the FIRST DEPTH-ADDING technique in this file -- every earlier
/// Phase 4 addition (LMP, futility, razoring, history pruning, ProbCut,
/// LMR itself) removes search depth, never adds it; singular extensions
/// just below are this file's second. A move that gives check gets its
/// own child search granted one extra ply (kCheckExtensionPly) rather
/// than losing the usual one, since a forced check-response sequence is
/// exactly the shape of tactical line the horizon effect is most likely
/// to misjudge if cut off one ply too early, and a position that's just
/// been put in check structurally has far fewer legal replies than an
/// ordinary one, so the extra ply costs much less branching than it
/// would elsewhere. Guarded by `ply + 1 < kMaxPly` -- not just a
/// niceness check, a genuine correctness requirement: `path` and the
/// killer/PV bookkeeping this function relies on are fixed-size arrays
/// sized by kMaxPly (search/ordering.h), so an unbounded chain of check
/// extensions pushing `ply` past that bound would be an out-of-bounds
/// write, not just a wasted search. `move_gives_check` (computed once
/// per move, right after make_move() -- shared by futility/LMP/history-
/// pruning's own checks above AND this) is what decides both the
/// extension here AND LMR's own eligibility just below: a checking move
/// is never LMR-reduced (previously an oversight -- LMR's own guard
/// excluded captures/promotions but not checks, even though a checking
/// move is exactly as tactical/forcing as either of those and CPW's own
/// LMR guidance excludes them for the identical reason NMP/LMR already
/// exclude captures) -- so extension and reduction are naturally
/// mutually exclusive per move under this design: a move is either
/// forcing enough to extend, or ordinary enough to consider reducing,
/// never plausibly both.
///
/// Singular extensions (CPW "Singular Extensions", also the move loop
/// below, but evaluated ONLY for the TT move -- see this file's
/// kSingular* constants for the full guard list and rationale) are this
/// file's second, more expensive depth-adding technique: when the TT's
/// own previously-stored score for this node's TT move indicates it
/// caused a beta cutoff before, and every OTHER legal move here fails a
/// cheap, reduced-depth verification search against a window built just
/// below that stored score, the TT move is judged "singular" (the only
/// genuinely good option at this node) and its own child search gets
/// the same one-ply bonus check extensions grant -- combined via
/// `std::max()` with any check-extension bonus, never summed, so a
/// checking TT move that's ALSO judged singular still only gets
/// extended once. Unlike check extensions (a free, purely local
/// look-at-the-move decision), this pays for a genuine extra search per
/// eligible node, which is exactly why it's gated behind kSingularMin
/// Depth and the TT-entry-freshness guard, not applied unconditionally
/// the way check extensions are.
///
/// Late move reductions (CPW "Late Move Reductions", the move loop
/// below) reduce depth for LATE, QUIET moves specifically -- ones far
/// enough into the already-ordered move list (order_moves(), search/
/// ordering.h) that they're unlikely to be the best move here, so a
/// cheap reduced-depth probe first is worth the risk of occasionally
/// needing a full-depth re-verification. This slots into the existing
/// PVS null-window structure as one MORE fallback step before it, not a
/// separate mechanism: reduced null-window probe -> (only if it beats
/// alpha) full-DEPTH null-window re-check -> (only if THAT still beats
/// alpha and stays below beta) the existing full-WINDOW re-search. A
/// move never gets reduced at all when in check (few, forced-feeling
/// replies -- not a good candidate for a shortcut), when IT ITSELF
/// gives check (see this file's check-extensions section just above --
/// it gets extended instead), when it's a capture or promotion
/// (tactical moves need full-depth verification, the same reasoning
/// NMP's own guards lean on), or when it's not late enough in the list
/// yet (kLMRMinMoveIndex) -- see this file's kLMR* constants for the
/// exact thresholds.
///
/// Late move pruning (CPW "Move Count Based Pruning", the move loop
/// below) goes one step further than LMR for a strict SUBSET of what
/// LMR would otherwise reduce: once enough quiet, non-check-giving
/// moves have already been tried at this node (this function's own
/// `quiets_tried` counter, not the raw move index -- LMP's premise is
/// specifically about how many quiet ALTERNATIVES have already failed
/// to help, not where a move happens to sit in a list that also
/// contains captures/promotions ahead of it) without any of them
/// raising alpha, the remaining quiet tail is skipped outright --
/// never even a reduced probe -- rather than searched at all. Only
/// applies at shallow remaining depth (kLMPMaxDepth) and never when
/// this node itself is in check (few, forced-feeling replies -- the
/// same reasoning LMR excludes in-check nodes for) or when alpha is
/// already in mate-score range (a position this close to a proven mate
/// needs every candidate reply actually checked, not skipped on a
/// move-count heuristic). "Gives check" is determined the same way
/// as everywhere else in this codebase without a dedicated move flag:
/// in_check(pos) read immediately after board::make_move() applies the
/// move, since side-to-move has already flipped to the opponent by
/// then -- a true result means this move gives check. See this file's
/// kLMP* constants for the exact per-depth thresholds.
///
/// History pruning (CPW "History Leaf Pruning", the move loop below) is
/// checked alongside LMP, using the same quiet/non-check-giving move
/// restriction, but a different signal: not how many quiet alternatives
/// have already been tried (LMP's `quiets_tried`), but this SPECIFIC
/// move's own accumulated `HistoryTable` score (search/ordering.h) --
/// skipped outright when that score is below a per-depth threshold, on
/// the premise that a quiet move which has rarely-if-ever caused a beta
/// cutoff elsewhere in this search is a poor bet to spend a full search
/// on this late in the game tree. Independent of LMP's own check (both
/// run, either can trigger the skip on its own) rather than folded into
/// one combined condition, since they measure genuinely different
/// things and either one failing is already sufficient reason to skip.
/// See this file's kHistoryPruning* constants for the exact per-depth
/// thresholds.
///
/// Futility pruning (CPW "Futility Pruning", the move loop below) is a
/// third, node-level check alongside LMP, evaluated once per node (not
/// once per move -- unlike LMP's `quiets_tried` counter, the underlying
/// condition, "is this node's static eval already too far below alpha
/// for a quiet move to plausibly close the gap," doesn't change as the
/// move loop progresses, only the move being considered does) using
/// `eval::evaluate()` computed before the move loop starts. Only
/// computed at all when it could actually matter (shallow remaining
/// depth, not in check, alpha not already in mate range) to avoid
/// paying eval's cost at nodes where futility could never apply anyway.
/// Same quiet/non-check-giving move restriction as LMP, for the same
/// reason (a capture or a check can swing the position's real value
/// well past a static margin's estimate). See this file's kFutility*
/// constants for the exact per-depth margins.
///
/// Razoring (CPW "Razoring", the node-level check right after the NMP
/// block below, before movegen) is a more drastic cousin of futility
/// pruning: instead of skipping individual late quiet moves inside the
/// move loop, it can skip the ENTIRE move loop for this node when the
/// static eval is so far below alpha (a wider margin than futility's
/// own, see kRazor* constants) that no move here plausibly recovers.
/// Rather than trusting that verdict outright, it drops into
/// quiescence search (which still correctly resolves the position's
/// own captures/checks/terminal status -- see quiescence()'s own
/// header comment) and only returns early if the quiescence result
/// ITSELF independently confirms the same conclusion (still <= alpha)
/// -- CPW's "razoring with verification," safer than trusting a wide
/// static-eval margin alone. If quiescence comes back ABOVE alpha, the
/// static eval's pessimism was wrong, and this falls through to the
/// normal move loop below rather than returning a bad, unverified
/// score. Self-contained (its own `in_check()`/`eval::evaluate()`
/// calls, not sharing state with futility pruning's own later,
/// separately-computed static eval) for the same reason NMP's own
/// block is self-contained -- it runs at a different point in this
/// function (before movegen even happens) than futility does (after
/// order_moves(), inside the move loop's own preamble).
///
/// ProbCut (CPW "ProbCut," the node-level check just before the main
/// move loop, right after order_moves() -- see kProbCut* constants
/// above) is razoring's mirror image: instead of looking for a reason
/// to fail LOW early at shallow depth, it looks for a reason to fail
/// HIGH early at moderate-to-high depth. It reuses the SAME already-
/// ordered `moves` list order_moves() just produced (no second movegen
/// or ordering pass) rather than being self-contained the way razoring
/// is -- razoring runs before movegen even happens, so it has nothing
/// to reuse yet, while ProbCut deliberately runs after, specifically so
/// its own verification search can walk captures/promotions in their
/// already-best-first MVV-LVA order rather than redo that work. For
/// each capture or promotion in `moves` (quiet moves are skipped with
/// `continue`, not `break` -- the TT move, which can be quiet, is
/// always tried first regardless of type, so a quiet move at index 0
/// doesn't mean everything after it is quiet too), a reduced-depth
/// search against a raised, null (`beta + kProbCutMargin`) window
/// checks whether this one move alone can already prove the position
/// is winning by at least that inflated margin; if it can, that's
/// taken as strong enough evidence a full-depth search of the whole
/// node would also fail high that this function returns immediately
/// (fail-soft: the verification score itself, not just `beta` --
/// mirroring NMP's own fail-soft return just below, including the same
/// mate-range clamp and for the identical reason: a raw score from a
/// REDUCED search shouldn't be trusted as an exact mate distance).
///
/// Null-move pruning (see the NMP block right after IIR, below) needs
/// one more piece of state IIR/IID's own logic never did: whether a
/// null move is even allowed at this node. `allow_null_move` defaults
/// to true for every ordinary call (every existing call site is
/// unaffected and doesn't need updating); the ONE place this function
/// ever passes false explicitly is its own recursive call inside the
/// NMP block, for the single child that call makes -- CPW's "no two
/// null moves in a row" rule (skipping straight past every real move
/// at a node by permitting the very next node to also just pass would
/// prove nothing about the actual position). This is deliberately NOT
/// a "was there a null move anywhere among this node's ancestors"
/// check -- only immediate adjacency matters.
///
/// Negamax alpha-beta search. Returns a score from `pos.side_to_move`'s
/// perspective (positive = good for the side to move). At depth <= 0
/// this hands off entirely to quiescence search (search/quiescence.h)
/// rather than returning a raw eval::evaluate() call directly — see
/// that module's header comment for why (the short version: avoiding
/// the horizon effect by resolving in-flight tactics before trusting a
/// static eval).
///
/// `ply` is the number of plies searched so far from the root (0 at the
/// root's immediate children), used only to adjust mate scores so that
/// shorter mates are preferred: a mate delivered `ply` plies from the
/// root scores kMateScore - ply from the mated side's perspective (see
/// the `moves.empty()` branch), so the score shrinks — and therefore
/// looks less attractive to a side searching for the *fastest* mate, or
/// less bad to a side merely trying to survive as long as possible —
/// the deeper the forced mate lies. Standard CPW "Mate Scores"
/// convention; from-scratch implementation here. `cont_history`,
/// `prev_piece`, and `prev_to` describe the move immediately preceding
/// this call (the move the caller just made to reach `pos`) -- threaded
/// through so this node's own order_moves() call and its own
/// killer/history-style update on a beta cutoff (this function's move
/// loop, below) can both consult/update continuation history (see
/// search/ordering.h's ContinuationHistoryTable for the full
/// rationale). `prev_piece` is `board::PieceType::None` when there
/// isn't a real preceding move to condition on -- immediately after a
/// null move (this function's own NMP block skips passing continuation
/// context to its null-move child for exactly that reason) -- which
/// makes continuation history contribute nothing at that node, same as
/// if the table were empty. `tt` (search/tt.h) is
/// also keyed and mate-distance-adjusted around this same `ply`
/// convention — see tt.cpp's adjustment functions.
///
/// Mate distance pruning (CPW "Mate Distance Pruning") is applied right
/// at the top of this function, before the TT probe or movegen: alpha/
/// beta are clamped to the best/worst scores actually reachable from
/// this node given `ply` (the same -(kMateScore - ply) formula the
/// `moves.empty()` branch below uses for "mated right here," and its
/// mirror kMateScore - ply - 1 for "deliver mate as fast as possible
/// from here"), and if that clamping alone collapses alpha >= beta, the
/// node returns immediately with no movegen/TT/ordering work at all.
/// This never changes what any node returns relative to plain alpha-
/// beta -- it only short-circuits nodes whose window was already
/// unreachable because some shallower ancestor has already found a
/// forced mate shorter than anything this node could possibly still
/// improve on.
///
/// Transposition table integration deliberately stays simple for this
/// first pass (see docs/DECISIONS.md, 2026-08-15 TT entry): a probe
/// that doesn't immediately resolve the window (an Exact hit, or a
/// Lower/Upper bound that already fails high/low against the CALLER's
/// actual alpha/beta) is discarded rather than used to narrow alpha/
/// beta for the search that follows. Narrowing from a non-cutoff
/// bound is a real, standard optimization, but it complicates how the
/// eventual store() at the bottom of this function should classify its
/// own Exact/Lower/Upper result (that classification needs to be
/// relative to whatever window was actually searched) — skipping it
/// keeps that classification unambiguous. TT interaction only happens
/// for depth >= 1 nodes: a depth <= 0 leaf's score is a pure
/// eval::evaluate() call that never consults alpha/beta at all (see
/// the branch immediately below), so there is nothing for the TT to
/// usefully cache there yet (no quiescence search exists as of this
/// commit — ROADMAP.md's separate "Quiescence search" item).
///
/// Move ordering (search/ordering.h) reorders the generated move list
/// before this function's own loop below, using the TT probe's move
/// (even one too shallow to trigger a cutoff above -- still a useful
/// hint), MVV-LVA for captures, promotion value, killer moves, and the
/// history heuristic. This is also what makes the "first move" comment
/// in the loop below actually meaningful now, rather than just "first
/// in move-generation order."
///
/// The PVS loop below no longer special-cases depth == 1 (it did, prior
/// to this session): that special case existed because a depth-1 move's
/// child used to be a depth-0 leaf resolved by a raw eval::evaluate()
/// call that never consulted alpha/beta at all, so a null-window probe
/// there genuinely could never prune anything -- just wasted work.
/// Now that depth <= 0 hands off to quiescence search (search/
/// quiescence.h) instead, which DOES meaningfully respond to whatever
/// window it's given (its own stand-pat comparison and cutoff logic),
/// that's no longer true -- a null-window probe at depth == 1 can
/// genuinely fail low and get skipped, same as at any deeper depth, so
/// it's folded into the general PVS branch below rather than kept as a
/// stale special case.
///
/// Internal Iterative Reduction (IIR): a node with no TT entry at all
/// (probe.hit == false) has no move-ordering hint to search efficiently
/// with -- CPW's modern alternative to Internal Iterative Deepening,
/// which spent extra search effort *at this same node* up front trying
/// to manufacture a move-ordering hint before the real search. IIR
/// instead just accepts a slightly shallower search here (reducing the
/// effective `depth` by kIIRReduction, only when the unreduced `depth`
/// is already at least kIIRMinDepth), trusting iterative deepening's
/// own outer loop to naturally revisit this node with a real TT-move
/// hint on a later, less-reduced iteration -- exactly why this is safe
/// in the way PVS/TT/aspiration windows were NOT: those were provably
/// exact (same best move/score as plain full-window alpha-beta,
/// regardless of how they were called); IIR is a genuine heuristic
/// approximation, like the pruning/reduction techniques ROADMAP.md's
/// remaining Phase 3/4 items add, whose safety net is iterative
/// deepening's own self-correction across iterations, not per-call
/// exactness -- a single search_fixed_depth() call with no shallower
/// iteration to have already populated the TT has no such correction
/// available, so it's a real (if generally small) accuracy/depth
/// trade at any one node, same as it is in every other engine that
/// uses this technique. `depth` is reassigned in place (not a separate
/// variable) so every downstream use in this function -- the move
/// loop's recursive calls and the TT store at the bottom -- correctly
/// reflects the depth actually searched, not the depth originally
/// requested.
///
/// `eval_cache` is forwarded to every eval::evaluate()/quiescence() call
/// this function and its own recursive calls make (razoring and
/// futility pruning below each call eval::evaluate() independently on
/// the SAME unchanged `pos` when both apply at one node -- a guaranteed
/// cache hit with no transposition needed at all; see eval/eval_cache.h
/// for the full rationale). A mandatory reference, same as `pawn_tt`
/// just above -- every negamax() call site in this file (recursive
/// calls and both top-level entry points) already threads a real
/// instance through, so there's no caller left needing a "no cache"
/// default the way evaluate()'s/quiescence()'s own optional pointer
/// parameters still support for callers outside this file (tests, etc).
///
/// `material_weights` is forwarded to every eval::evaluate()/
/// quiescence() call this function and its own recursive calls make
/// (the razoring/futility eval::evaluate() calls below, and the
/// depth<=0 quiescence() delegation) — see eval::evaluate()'s own doc
/// comment on this parameter, and search.h's search_fixed_depth()'s own
/// doc comment on why tuner::match (src/tuner/match.h) is this
/// parameter's real caller. A mandatory pointer (not defaulted) for the
/// identical reason `pawn_tt`/`eval_cache` aren't either at this
/// internal-call-site level — every negamax() call site in this file
/// already threads a real value through (nullptr is a perfectly valid
/// value to thread, meaning "no override," but it's threaded
/// EXPLICITLY at every site, not defaulted) — search.h's own public
/// search_fixed_depth()/search_iterative_deepening() are where this
/// parameter actually defaults to nullptr for external callers.
///
/// `limits`, if non-null, is the mid-search time-budget interruption
/// state (search.h's SearchLimits) this call and every recursive call
/// it makes -- the NMP/razoring/ProbCut/singular-extension probes
/// below, this function's own move loop, and every quiescence()
/// delegation -- share for one search_iterative_deepening() iteration.
/// See SearchLimits' own doc comment for the full contract. Defaults to
/// nullptr, meaning "no time budget": search_fixed_depth() and every
/// existing test/bench call are unaffected.
int negamax(Position& pos, int depth, int alpha, int beta, int ply, std::uint64_t& nodes,
            TranspositionTable& tt, KillerTable& killers, HistoryTable& history,
            ContinuationHistoryTable& cont_history, board::PieceType prev_piece,
            board::Square prev_to, std::span<const std::uint64_t> game_history,
            std::array<std::uint64_t, kMaxPly>& path, eval::PawnHashTable& pawn_tt,
            eval::EvalCache& eval_cache, const eval::MaterialWeights* material_weights,
            bool allow_null_move = true, SearchLimits* limits = nullptr) {
    // Mid-search time-budget interruption fast path (search.h's
    // SearchLimits doc comment has the full contract): checked before
    // anything else, including the depth <= 0 quiescence delegation
    // just below, so a node reached after the deadline was already
    // noticed elsewhere in the tree does zero further work of its own.
    if (limits != nullptr && limits->stopped) {
        return 0;
    }

    if (depth <= 0) {
        // Quiescence search (search/quiescence.h) rather than a raw
        // eval::evaluate() call: resolves in-flight capture/check
        // sequences right at the horizon instead of trusting a static
        // eval that might be mid-exchange (the "horizon effect," CPW
        // "Quiescence Search"). `nodes` is passed through and
        // incremented by quiescence()'s own counting, not incremented
        // again here first -- this negamax() call itself does no
        // "node work" of its own at depth <= 0, it's a pure delegation,
        // so counting it separately here would double-count the same
        // conceptual node. `limits` is threaded through too -- see
        // quiescence.h's own doc comment on why quiescence search
        // participates in the same interruption scheme.
        return quiescence(pos, alpha, beta, ply, nodes, /*include_checks=*/true, &pawn_tt,
                           &eval_cache, material_weights, limits);
    }

    // Periodic deadline/external-stop check (search.h's SearchLimits
    // doc comment, this file's kTimeCheckNodeInterval comment above):
    // checked after `nodes` is incremented so the shared counter's
    // periodicity is consistent regardless of how deep in the tree this
    // particular call happens to be. Two independent trigger conditions
    // -- a passed wall-clock `deadline` (the original mechanism), or a
    // Lazy SMP helper thread's `external_stop` flag having been raised
    // by the main thread (search.h's own doc comment on that field) --
    // either one sets `stopped` the same way; everything downstream of
    // `stopped` doesn't care which one fired.
    ++nodes;
    if (limits != nullptr && (nodes & kTimeCheckNodeMask) == 0) {
        const bool deadline_passed =
            limits->has_deadline && std::chrono::steady_clock::now() >= limits->deadline;
        const bool externally_stopped =
            limits->external_stop != nullptr &&
            limits->external_stop->load(std::memory_order_relaxed);
        if (deadline_passed || externally_stopped) {
            limits->stopped = true;
            return 0;
        }
    }

    const std::uint64_t key = pos.zobrist_hash;
    if (ply < kMaxPly) {
        // Record this node's own hash before any recursive call below
        // can need to see it (see negamax()'s header comment on why
        // `path` is written this way, mirroring KillerTable's per-ply
        // array reuse).
        path[static_cast<std::size_t>(ply)] = key;
    }
    if (is_draw_by_rule(pos, key, ply, game_history, path)) {
        // Not stored in the TT -- see negamax()'s header comment on why
        // this check deliberately runs before the TT probe below (the
        // Graph History Interaction problem).
        return kDrawScore;
    }

    // Mate distance pruning (CPW "Mate Distance Pruning"): regardless of
    // what the rest of this node finds, the best score achievable here
    // is capped at kMateScore - ply - 1 (delivering mate one ply deeper
    // than this node -- the fastest mate reachable from here), and the
    // worst score achievable is floored at -(kMateScore - ply) (getting
    // mated at this very node -- the identical formula the
    // `moves.empty()` branch below already uses). Clamping alpha/beta to
    // those bounds before doing any real work lets a window that's
    // already outside what's reachable from this node fail immediately:
    // if a shallower ancestor has already found a shorter forced mate
    // than anything achievable here, alpha/beta collapse and there's
    // nothing left to search for. This is a pure efficiency win, not a
    // change in what any node returns -- a node whose window wasn't
    // already unreachable in this way is completely unaffected, since
    // clamping alpha/beta to bounds that are still looser than the
    // caller's own window is a no-op.
    if (alpha < -(kMateScore - ply)) {
        alpha = -(kMateScore - ply);
    }
    if (beta > kMateScore - ply - 1) {
        beta = kMateScore - ply - 1;
    }
    if (alpha >= beta) {
        return alpha;
    }

    const int alpha_orig = alpha;
    const Color us = pos.side_to_move;
    tt.prefetch(key); // ARCHITECTURE.md: issued as early as possible, to overlap with movegen below.

    const TTProbeResult probe = tt.probe(key, ply);
    if (probe.hit && probe.depth >= depth) {
        if (probe.bound == Bound::Exact) {
            return probe.score;
        }
        if (probe.bound == Bound::Lower && probe.score >= beta) {
            return probe.score;
        }
        if (probe.bound == Bound::Upper && probe.score <= alpha) {
            return probe.score;
        }
        // Bound doesn't resolve this window: fall through to a normal
        // search using the untouched alpha/beta (see this function's
        // header comment on why this deliberately doesn't narrow them).
    }
    // Even a probe too shallow for a cutoff above is still worth using
    // as a move-ordering hint below -- a Move() (null) if there was no
    // hit at all, in which case order_moves() simply gives it no special
    // priority (see ordering.h).
    const Move tt_move = probe.hit ? probe.move : Move();

    // IIR (see this function's header comment): no TT entry at all for
    // this position, and deep enough that a shallower search here still
    // does something useful. Reassigning `depth` in place is
    // deliberate -- everything below (the move loop's recursive calls,
    // the TT store at the bottom) must reflect what was ACTUALLY
    // searched, not the depth this call was originally asked for.
    if (!probe.hit && depth >= kIIRMinDepth) {
        depth -= kIIRReduction;
    }

    // Null-move pruning (CPW "Null Move Pruning"): if we're not in
    // check and it's still our move even after "passing" (giving the
    // opponent a free tempo -- searched at reduced depth, since this is
    // just a cheap plausibility probe, not a real line worth full
    // depth), and that STILL isn't enough to bring the score down to
    // beta, then a real move (which can only do at least as well as
    // passing) would certainly also reach beta -- so this node can be
    // pruned outright without searching any of its real moves.
    //
    // Guards, each corresponding to a real unsoundness risk otherwise:
    // in check (board::make_null_move()'s own precondition -- a null
    // move can't escape check, not a legal chess outcome, and would
    // corrupt the search); two null moves in a row (`allow_null_move`,
    // see this function's header comment); too shallow (kNullMoveMinDepth
    // -- not worth the reduced re-search's own cost below a minimum);
    // zugzwang risk (CPW's own caveat -- skipped whenever the side to
    // move has no non-pawn, non-king material, the classic
    // king-and-pawn-endgame case where "passing" can be a strictly
    // WORSE option than any real move, making the technique unsound
    // there); and a mate-range beta (a reduced-depth null-move probe
    // stumbling onto what looks like a mate score isn't a trustworthy
    // claim of an actual forced mate at full depth). Beyond that hard
    // guard, a SOFTER zugzwang bias also applies just below (the `if
    // (eval::is_zugzwang_prone(...))` check right after `reduction` is
    // first computed) -- material signatures that are merely
    // zugzwang-PRONE rather than provably unsound for NMP altogether
    // (ROADMAP.md's own "Zugzwang-aware search shaping" item) get a
    // smaller reduction instead of being skipped outright.
    const bool non_pawn_material =
        (pos.pieces(us, board::PieceType::Knight) | pos.pieces(us, board::PieceType::Bishop) |
         pos.pieces(us, board::PieceType::Rook) | pos.pieces(us, board::PieceType::Queen)) != 0;
    if (allow_null_move && depth >= kNullMoveMinDepth && beta < kMateThreshold && non_pawn_material &&
        !in_check(pos)) {
        int reduction =
            depth >= kNullMoveBigReductionDepth ? kNullMoveBigReduction : kNullMoveReduction;
        // Zugzwang-aware bias (ROADMAP.md Phase 6's "Zugzwang-aware
        // search shaping" item; see this file's own kZugzwangReductionDecrease
        // doc comment above and eval/endgame.h's is_zugzwang_prone() for
        // the full rationale): a smaller R here for material signatures
        // flagged as zugzwang-prone, so the probe searches closer to
        // full depth instead of trusting as shallow a reduced-depth
        // check as usual in exactly the material shapes most likely to
        // make "passing" a misleadingly good-looking option.
        if (eval::is_zugzwang_prone(eval::classify_endgame(pos))) {
            reduction = std::max(reduction - kZugzwangReductionDecrease, kZugzwangMinReduction);
        }
        UndoInfo null_undo;
        board::make_null_move(pos, null_undo);
        const int null_score = -negamax(pos, depth - 1 - reduction, -beta, -beta + 1, ply + 1, nodes,
                                         tt, killers, history, cont_history,
                                         /*prev_piece=*/board::PieceType::None, /*prev_to=*/0,
                                         game_history, path, pawn_tt, eval_cache, material_weights,
                                         /*allow_null_move=*/false, limits);
        board::unmake_null_move(pos, null_undo);
        // A probe interrupted mid-search (limits->stopped) returns a
        // meaningless, truncated-subtree score (SearchLimits' own doc
        // comment) -- never trust it for a cutoff; fall through to the
        // normal move loop below (which will itself immediately bail
        // via the top-of-function check on its own next negamax() call).
        if ((limits == nullptr || !limits->stopped) && null_score >= beta) {
            // Don't hand back a mate-range score verbatim from a
            // reduced, unverified probe (CPW's own caution) -- clamp to
            // beta instead of trusting an unconfirmed "mate" claim.
            return null_score >= kMateThreshold ? beta : null_score;
        }
    }

    // Razoring (CPW "Razoring", this function's header comment): only
    // at shallow remaining depth, not in check, and only when this
    // node's own static eval is so far below alpha that even a wide
    // margin makes it implausible any move here recovers. Rather than
    // trusting that verdict outright and returning the static eval
    // directly, drop into quiescence search (which still correctly
    // resolves in-flight captures/checks and this position's own
    // terminal status) and only return early if THAT result
    // independently confirms the same conclusion.
    if (!in_check(pos) && depth <= kRazorMaxDepth && alpha < kMateThreshold) {
        const int white_relative = eval::evaluate(pos, &pawn_tt, &eval_cache, material_weights);
        const int razor_static_eval = us == Color::White ? white_relative : -white_relative;
        if (razor_static_eval + kRazorMargins[static_cast<std::size_t>(depth)] <= alpha) {
            const int razor_score = quiescence(pos, alpha, beta, ply, nodes,
                                                /*include_checks=*/true, &pawn_tt, &eval_cache,
                                                material_weights, limits);
            if ((limits == nullptr || !limits->stopped) && razor_score <= alpha) {
                return razor_score;
            }
            // Quiescence disagreed with the static eval's pessimism --
            // fall through to the normal move loop below rather than
            // trusting the unverified margin.
        }
    }

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        // Terminal position: not stored in the TT (see this function's
        // header comment) — movegen already paid the cost of detecting
        // this, and there's no move-loop result left to cache.
        return in_check(pos) ? -(kMateScore - ply) : kDrawScore;
    }

    order_moves(moves, pos, tt_move, killers, ply, history, cont_history, prev_piece, prev_to);

    // Computed once, reused by LMR's eligibility check below (moves
    // themselves don't change whether the position THEY'RE PLAYED FROM
    // was in check) rather than re-derived per move.
    const bool us_in_check = in_check(pos);

    // ProbCut (CPW "ProbCut", this function's header comment): only at
    // moderate-to-high remaining depth, not in check, and only when
    // beta itself isn't already mate-range (a raised, inflated window
    // built from a mate score would be meaningless -- same reasoning as
    // NMP's own beta guard just below in this file). Walks the SAME
    // already-ordered `moves` list order_moves() just produced, trying
    // only captures/promotions (quiet moves are `continue`d past, not a
    // `break`, since the TT move -- always tried first regardless of
    // type -- can itself be quiet without that meaning everything after
    // it is quiet too).
    if (!us_in_check && depth >= kProbCutMinDepth && beta < kMateThreshold) {
        const int probcut_beta = beta + kProbCutMargin;
        for (int i = 0; i < moves.size(); ++i) {
            const Move probcut_move = moves[i];
            if (!probcut_move.is_capture() && !probcut_move.is_promotion()) {
                continue;
            }
            const board::PieceType probcut_moved_piece =
                board::piece_type_of(pos.piece_at(probcut_move.from()));
            UndoInfo probcut_undo;
            board::make_move(pos, probcut_move, probcut_undo);
            const int probcut_score =
                -negamax(pos, depth - kProbCutReduction, -probcut_beta, -probcut_beta + 1, ply + 1,
                         nodes, tt, killers, history, cont_history, probcut_moved_piece,
                         probcut_move.to(), game_history, path, pawn_tt, eval_cache, material_weights,
                         /*allow_null_move=*/true, limits);
            board::unmake_move(pos, probcut_move, probcut_undo);
            if (limits != nullptr && limits->stopped) {
                // Truncated subtree (SearchLimits' own doc comment) --
                // stop trying further captures/promotions in this loop
                // and fall through, same reasoning as NMP's own guard
                // above.
                break;
            }
            if (probcut_score >= probcut_beta) {
                // This one move alone, even searched shallower than the
                // rest of this node will be, already proves the
                // position is winning by more than a normal beta cutoff
                // would need -- strong enough evidence that a full-depth
                // search of the whole node would also fail high that
                // it's not worth paying for one. Fail-soft (the
                // verification score itself, not just `beta`), clamped
                // the same way NMP's own null-move probe is just below:
                // a REDUCED search's score shouldn't be trusted as an
                // exact mate distance.
                return probcut_score >= kMateThreshold ? beta : probcut_score;
            }
        }
    }

    // Futility pruning's static eval (this function's header comment):
    // computed once per node, before the move loop, since the
    // condition it feeds doesn't depend on which move is being
    // considered -- only on the position BEFORE any of them are
    // played. Only computed when it could actually be used (shallow
    // remaining depth, not in check, alpha not already mate-range) to
    // avoid paying eval::evaluate()'s cost at nodes where futility
    // could never apply anyway.
    const bool futility_may_apply =
        !us_in_check && depth <= kFutilityMaxDepth && alpha < kMateThreshold;
    int static_eval = 0;
    if (futility_may_apply) {
        const int white_relative = eval::evaluate(pos, &pawn_tt, &eval_cache, material_weights);
        static_eval = us == Color::White ? white_relative : -white_relative;
    }
    const bool futility_prune_node =
        futility_may_apply &&
        static_eval + kFutilityMargins[static_cast<std::size_t>(depth)] <= alpha;

    int best = -kInfinity;
    Move best_move; // Move() default (null) unless overwritten below — every real position has >=1 move here.
    // Count of quiet (non-capture, non-promotion) moves already tried at
    // this node, incremented once per move below regardless of which
    // path that move took (searched, LMR-reduced, or LMP-skipped) --
    // late move pruning's own threshold check (kLMP* constants above,
    // this function's header comment) is keyed on this, not the raw
    // move index `i`, since LMP's premise is specifically about how many
    // quiet ALTERNATIVES have already failed to help, not where a move
    // sits in a list that also contains captures/promotions ahead of it.
    int quiets_tried = 0;
    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        const bool move_is_quiet = !move.is_capture() && !move.is_promotion();
        // The piece making this move, read BEFORE make_move() below
        // vacates its from-square -- threaded into every recursive
        // negamax() call as that child's own `prev_piece`/`prev_to` (see
        // this function's header comment on continuation history).
        // Computed uniformly for every move, including captures and
        // promotions, matching mvv_lva_score()'s own convention
        // (search/ordering.cpp) of reading the attacker's piece type off
        // the from-square the same way regardless of move type.
        const board::PieceType moved_piece = board::piece_type_of(pos.piece_at(move.from()));

        // Singular extensions (this function's header comment): only
        // evaluated for the TT move itself (order_moves() places it
        // first whenever `tt_move` is present and legal, so this is
        // scoped to `i == 0`), and using `pos` as it stands BEFORE
        // `move` is played -- the verification search below tries every
        // OTHER legal move from this same position, so it has to run
        // before this iteration's own make_move() changes it.
        int singular_extension = 0;
        if (i == 0 && probe.hit && move == tt_move && probe.bound == Bound::Lower &&
            depth >= kSingularMinDepth && probe.depth >= depth - kSingularTTDepthMargin &&
            probe.score > -kMateThreshold && probe.score < kMateThreshold) {
            const int singular_beta = probe.score - kSingularMarginPerPly * depth;
            const int singular_depth = (depth - 1) / kSingularDepthDivisor;
            bool any_alternative_matched = false;
            for (int j = 0; j < moves.size() && !any_alternative_matched; ++j) {
                if (moves[j] == tt_move) {
                    continue;
                }
                const Move alt_move = moves[j];
                const board::PieceType alt_moved_piece =
                    board::piece_type_of(pos.piece_at(alt_move.from()));
                UndoInfo alt_undo;
                board::make_move(pos, alt_move, alt_undo);
                const int alt_score =
                    -negamax(pos, singular_depth, -singular_beta, -singular_beta + 1, ply + 1, nodes,
                             tt, killers, history, cont_history, alt_moved_piece, alt_move.to(),
                             game_history, path, pawn_tt, eval_cache, material_weights,
                             /*allow_null_move=*/true, limits);
                board::unmake_move(pos, alt_move, alt_undo);
                if (limits != nullptr && limits->stopped) {
                    // Truncated subtree -- stop the verification loop
                    // rather than trying more alternatives against a
                    // score that can no longer be trusted (same
                    // reasoning as NMP's/ProbCut's own guards above);
                    // `singular_extension` simply stays 0 below, which
                    // is harmless either way since this whole node's
                    // result will be discarded (SearchLimits' own doc
                    // comment).
                    break;
                }
                if (alt_score >= singular_beta) {
                    // Some other move can already reach almost as high a
                    // score as the TT move's own previous cutoff --
                    // disproves singularity (the TT move isn't the ONLY
                    // good option here), so no extension.
                    any_alternative_matched = true;
                }
            }
            if (!any_alternative_matched) {
                singular_extension = kSingularExtensionPly;
            }
        }

        UndoInfo undo;
        board::make_move(pos, move, undo);

        // "Gives check" is computed once here, right after the move is
        // already applied -- no dedicated move flag exists in this
        // codebase for it (see this function's header comment) -- and
        // shared by every check below that needs it, including the
        // first move (i == 0): check extensions (this function's own
        // header comment) apply to ANY checking move, not just late
        // ones, so this can no longer be computed only inside the
        // i != 0 branch the way it used to be.
        const bool move_gives_check = in_check(pos);

        // Check extensions (this function's header comment): a checking
        // move's own child search gets one extra ply rather than losing
        // the usual one -- guarded by `ply + 1 < kMaxPly`, a genuine
        // correctness requirement (see the header comment) rather than
        // just a niceness check, since path/killer bookkeeping is
        // sized by kMaxPly. Combined with any singular-extension bonus
        // computed just above via std::max(), not summed (this
        // function's header comment) -- a move never gets extended
        // twice for two different reasons at once.
        const int check_extension = (move_gives_check && ply + 1 < kMaxPly) ? kCheckExtensionPly : 0;
        const int extension = std::max(check_extension, singular_extension);

        int score;
        if (i == 0) {
            // First move -- now genuinely the highest-priority candidate
            // per order_moves() above (TT move, else best-scoring
            // capture/promotion/killer/history move), not just "first in
            // move-generation order" as it was pre-ordering: search it
            // with the full alpha-beta window to establish a real score
            // to compare everything else against. `+ extension` applies
            // even to this first move -- a checking or singular move
            // deserves the extra ply regardless of its position in the
            // ordering.
            score = -negamax(pos, depth - 1 + extension, -beta, -alpha, ply + 1, nodes, tt, killers,
                              history, cont_history, moved_piece, move.to(), game_history, path,
                              pawn_tt, eval_cache, material_weights, /*allow_null_move=*/true, limits);
        } else {
            // Futility pruning (CPW "Futility Pruning", this function's
            // header comment): a node-level condition -- computed once,
            // before the move loop, since it doesn't depend on which
            // move is being tried -- checked first among this branch's
            // three cascading checks since, like LMP, it can skip the
            // move outright at zero search cost. Same quiet/non-check
            // restriction as LMP, for the same reason (a capture or
            // check can swing the real value well past a static
            // margin's estimate).
            if (futility_prune_node && move_is_quiet && !move_gives_check) {
                board::unmake_move(pos, move, undo);
                ++quiets_tried;
                continue;
            }

            // Late move pruning (CPW "Move Count Based Pruning", this
            // function's header comment): a strict subset of LMR's own
            // eligibility, checked first since it can skip the move
            // entirely -- no reduced probe, no PVS null-window search at
            // all.
            if (!us_in_check && move_is_quiet && !move_gives_check && depth <= kLMPMaxDepth &&
                quiets_tried >= kLMPMoveCountLimits[static_cast<std::size_t>(depth)] &&
                alpha > -kMateThreshold) {
                board::unmake_move(pos, move, undo);
                ++quiets_tried;
                continue;
            }

            // History pruning (CPW "History Leaf Pruning", this
            // function's header comment): independent of LMP's own
            // check just above -- a different signal (this move's own
            // HistoryTable score, not how many quiet alternatives have
            // already been tried), either sufficient on its own to skip
            // the move. Same not-in-check/quiet/non-check-giving/
            // mate-range guards as LMP, for the same reasons.
            if (!us_in_check && move_is_quiet && !move_gives_check &&
                depth <= kHistoryPruningMaxDepth && alpha > -kMateThreshold &&
                history.score(us, move) < kHistoryPruningThresholds[static_cast<std::size_t>(depth)]) {
                board::unmake_move(pos, move, undo);
                ++quiets_tried;
                continue;
            }

            // Late move reductions (CPW "Late Move Reductions", this
            // function's header comment): only quiet, non-check-evading,
            // non-check-GIVING (see this file's check-extensions header
            // comment -- a checking move is extended instead, never
            // reduced, as of this session) moves late enough in
            // order_moves()'s ranking get reduced -- see kLMR* constants
            // above for the exact thresholds.
            const bool eligible_for_lmr = !us_in_check && depth >= kLMRMinDepth &&
                                           i >= kLMRMinMoveIndex && !move.is_capture() &&
                                           !move.is_promotion() && !move_gives_check;
            const int reduction = eligible_for_lmr ? (depth >= kLMRBigReductionDepth
                                                            ? kLMRBigReduction
                                                            : kLMRReduction)
                                                     : 0;

            // PVS: probe every later move with a null (zero-width)
            // window first -- cheap, since it only needs to prove
            // "fails low against alpha" or "fails high," not compute an
            // exact score. Only if the probe suggests this move might
            // actually beat alpha (a fail-high on the null window that's
            // still below beta) is it worth paying for a full-window
            // re-search to get its real score. `+ extension - reduction`
            // are naturally mutually exclusive per move under this
            // session's design (see this function's header comment) --
            // see it for how that interacts with the two fallback steps
            // below.
            score = -negamax(pos, depth - 1 + extension - reduction, -alpha - 1, -alpha, ply + 1,
                              nodes, tt, killers, history, cont_history, moved_piece, move.to(),
                              game_history, path, pawn_tt, eval_cache, material_weights,
                              /*allow_null_move=*/true, limits);
            if ((limits == nullptr || !limits->stopped) && reduction > 0 && score > alpha) {
                // The reduced probe suggested this move might actually
                // be good -- not trustworthy on its own (a shallower
                // search can overstate a quiet move's value), so
                // re-verify at full depth (still a null window -- this
                // is still just a probe) before deciding whether the
                // full-window re-search below is warranted. `extension`
                // is always 0 here (reduction > 0 implies eligible_for_lmr
                // was true, which requires !move_gives_check, which is
                // the only thing that ever sets extension > 0).
                score = -negamax(pos, depth - 1 + extension, -alpha - 1, -alpha, ply + 1, nodes, tt,
                                  killers, history, cont_history, moved_piece, move.to(), game_history,
                                  path, pawn_tt, eval_cache, material_weights, /*allow_null_move=*/true,
                                  limits);
            }
            if ((limits == nullptr || !limits->stopped) && score > alpha && score < beta) {
                score = -negamax(pos, depth - 1 + extension, -beta, -alpha, ply + 1, nodes, tt,
                                  killers, history, cont_history, moved_piece, move.to(), game_history,
                                  path, pawn_tt, eval_cache, material_weights, /*allow_null_move=*/true,
                                  limits);
            }
        }

        board::unmake_move(pos, move, undo);

        if (limits != nullptr && limits->stopped) {
            // This move's own score came from a subtree truncated by
            // the deadline (SearchLimits' own doc comment) -- stop
            // trying further moves and don't let it update
            // best/best_move/alpha even transiently; this node's
            // result is discarded wholesale further up the call chain
            // regardless (search_iterative_deepening(), search.cpp),
            // so nothing downstream of `break` here matters beyond
            // unwinding quickly.
            break;
        }

        if (move_is_quiet) {
            ++quiets_tried;
        }

        if (score > best) {
            best = score;
            best_move = move;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            // Beta cutoff. Record it for future ordering (killers +
            // history + continuation history) only for quiet,
            // non-promotion moves -- captures and promotions already
            // order well via MVV-LVA/promotion value (see ordering.h's
            // header comment), so mixing them into the killer/history
            // scheme adds noise without adding information (CPW's
            // "Killer Heuristic"/"History Heuristic" are conventionally
            // quiet-move-only for the same reason).
            if (!move.is_capture() && !move.is_promotion()) {
                killers.update(ply, move);
                history.update(us, move, depth);
                // No-op if prev_piece is board::PieceType::None (no real
                // preceding move at this node -- see this function's
                // header comment and ContinuationHistoryTable's own).
                cont_history.update(prev_piece, prev_to, moved_piece, move.to(), depth);
            }
            break; // The opponent won't let us reach this line.
        }
    }

    // Classify against alpha_orig/beta (the window this call was
    // actually asked to resolve), not any narrowed value — see this
    // function's header comment. CPW "Node Types": every move tried and
    // none reached alpha_orig -> only an upper bound was proven; a
    // cutoff -> only a lower bound was proven; otherwise the search
    // ran to completion within the window and best is exact.
    const Bound bound_type = best <= alpha_orig ? Bound::Upper
                            : best >= beta       ? Bound::Lower
                                                  : Bound::Exact;
    // Skip the store entirely if this node's own move loop was cut
    // short by the deadline (SearchLimits' own doc comment): `best`/
    // `best_move` here may reflect a truncated subtree rather than a
    // genuine result for this depth, and caching that would pollute
    // the TT with a wrong entry that could mislead a later, real
    // search at this same position.
    if (limits == nullptr || !limits->stopped) {
        tt.store(key, depth, best, bound_type, best_move, ply);
    }

    return best;
}

/// Reconstructs a principal variation (SearchResult::pv, search.h) by
/// walking the transposition table from `pos`, starting with
/// `root_move` (search_root()'s own just-computed best move -- passed
/// explicitly rather than re-probed, since search_root() already knows
/// it without a redundant TT lookup) and then following each
/// subsequent position's own TT-stored move up to `max_plies`. Takes
/// `pos` BY VALUE deliberately: this function makes/unmakes moves on
/// its own private copy while walking, never touching the caller's
/// actual position (mirroring negamax()'s/search_root()'s own
/// unmodified-on-return contract, just via a copy instead of an
/// explicit unmake at the end).
///
/// Stops early, returning whatever was accumulated so far, on any of:
/// `root_move` (or a later walked move) not being legal in the
/// position it's about to be applied to (defensive -- a move sourced
/// from the TT should always be legal for the position it was stored
/// against, since it was only ever stored after being generated as a
/// legal move there, but a TT entry surviving into a DIFFERENT
/// position via a Zobrist collision, while astronomically unlikely
/// (tt.h's own header comment on the empty-slot sentinel), is exactly
/// the kind of thing this function should never trust blindly); a TT
/// miss (nothing more is known about that continuation); a stored
/// bound that isn't Bound::Exact (CPW "Node Types" -- only an Exact
/// entry's move is a genuine proven PV continuation, the same
/// distinction negamax()'s own TT-probe cutoff logic respects
/// elsewhere; a Lower/Upper-bound entry's move is still a reasonable
/// move, just not one this search actually proved best); a null move;
/// or the walked position repeating one already seen earlier in THIS
/// walk (a cycle would otherwise loop forever, and a "PV" that repeats
/// itself isn't a meaningful line to report either way).
std::vector<Move> extract_pv(Position pos, const TranspositionTable& tt, Move root_move,
                              int max_plies) {
    std::vector<Move> pv;
    if (root_move.is_null() || max_plies <= 0) {
        return pv;
    }

    std::vector<std::uint64_t> seen;
    seen.reserve(static_cast<std::size_t>(max_plies));
    Move move = root_move;

    for (int i = 0; i < max_plies; ++i) {
        MoveList legal;
        board::generate_legal_moves(pos, legal);
        if (!legal.contains(move)) {
            break;
        }

        seen.push_back(pos.zobrist_hash);
        UndoInfo undo;
        board::make_move(pos, move, undo);
        pv.push_back(move);

        bool repeated = false;
        for (const std::uint64_t h : seen) {
            if (h == pos.zobrist_hash) {
                repeated = true;
                break;
            }
        }
        if (repeated) {
            break;
        }

        const TTProbeResult probe = tt.probe(pos.zobrist_hash, 0);
        if (!probe.hit || probe.bound != Bound::Exact || probe.move.is_null()) {
            break;
        }
        move = probe.move;
    }

    return pv;
}

/// Core root-move-loop logic shared by both public entry points below.
/// Kept as a private, TT-taking function rather than exposed directly:
/// each public entry point is responsible for deciding *which*
/// TranspositionTable/KillerTable/HistoryTable instances to pass in
/// (see tt.h's header comment on the current per-call lifetime), not
/// this function. Deliberately does NOT apply Internal Iterative
/// Reduction (negamax()'s header comment) to its own `depth`: the root
/// is the exact position a caller asked to search to a specific depth
/// (via search_fixed_depth()'s parameter, or the current iteration of
/// search_iterative_deepening()'s loop) -- silently searching it any
/// shallower than requested would misreport SearchResult::depth_completed
/// and give a UCI caller a less-deep analysis than it asked for. IIR
/// only ever reduces *internal* nodes, reached through negamax()'s own
/// recursion below the root.
///
/// `aspiration_alpha`/`aspiration_beta` is the search window to use --
/// search_fixed_depth() always passes the full (-kInfinity, kInfinity)
/// window (no previous iteration's score to aspirate around);
/// search_iterative_deepening() narrows it for depth >= 2 (see its own
/// comments and docs/DECISIONS.md's aspiration-windows entry). Callers
/// must check the RETURNED result.score against the window they passed
/// in: if it's <= aspiration_alpha or >= aspiration_beta, the search
/// only proved a BOUND, not an exact score (CPW "Aspiration Windows" --
/// same fail-low/fail-high concept as negamax()'s own TT-bound
/// classification, just applied to the whole root call instead of one
/// node) -- best_move in that case is not to be trusted as final either
/// (see below), and the caller is expected to re-search with a wider
/// window rather than accept the result as-is.
/// `game_history`/`path`: threaded straight through to every negamax()
/// call below unchanged -- see negamax()'s header comment and
/// is_draw_by_rule() for what they're for. `path[0]` is set to this
/// call's own root_key right after it's computed, below, since the root
/// itself is never passed through negamax() (it has no ply of its own
/// in that sense) but its hash still needs to be part of the sequence a
/// deeper node's repetition check can walk back through.
/// `pawn_tt`: threaded straight through to every negamax() call below
/// unchanged, which threads it further into quiescence()/eval::evaluate()
/// -- see eval/eval.h's doc comment on evaluate()'s own `pawn_tt`
/// parameter for what it's for. `eval_cache`: threaded straight through
/// the same way -- see eval/eval_cache.h and negamax()'s own header
/// comment. `material_weights`: threaded straight through the same way
/// too -- see eval::evaluate()'s and negamax()'s own header comments on
/// this parameter, and search.h's search_fixed_depth()'s own doc
/// comment on why tuner::match is this parameter's real caller.
/// `cont_history`: threaded through to
/// every negamax() call below alongside killers/history (search/
/// ordering.h's ContinuationHistoryTable) -- but this function's OWN
/// order_moves() call (below) always passes board::PieceType::None for
/// `prev_piece`, not a real piece: the root has no preceding move of
/// its own to condition on (the game move that led to this exact
/// position isn't threaded into search_fixed_depth()/
/// search_iterative_deepening() today, only game_history's hashes are
/// -- a deliberate first-draft scope limit, not an oversight; revisit
/// if/when a real move, not just a hash, is threaded that far). Each
/// root MOVE's own piece/destination, by contrast, genuinely is known
/// (computed fresh per iteration of the loop below) and IS passed as
/// `prev_piece`/`prev_to` into that move's negamax() children, exactly
/// as negamax()'s own move loop does for its own children.
///
/// `limits`, if non-null, is the same mid-search time-budget
/// interruption state negamax() takes (search.h's SearchLimits) --
/// threaded into every negamax() call this function makes below.
/// Unlike negamax(), this function never checks `limits->stopped` on
/// entry itself (there is no shallower root to have set it before this
/// call begins) -- it only observes it after each move's own negamax()
/// call returns, at which point it stops trying further moves rather
/// than trusting a truncated-subtree score to update `alpha`/
/// `best_move`, and skips its own TT store. `result.depth_completed`
/// is left at `depth` regardless (this function's caller,
/// search_iterative_deepening(), is the one that actually decides
/// whether an interrupted iteration's SearchResult gets used at all --
/// see its own comments) -- callers other than
/// search_iterative_deepening() never pass a `limits` with
/// `has_deadline` set, so this never applies to search_fixed_depth().
SearchResult search_root(Position& pos, int depth, int aspiration_alpha, int aspiration_beta,
                          TranspositionTable& tt, KillerTable& killers, HistoryTable& history,
                          ContinuationHistoryTable& cont_history,
                          std::span<const std::uint64_t> game_history,
                          std::array<std::uint64_t, kMaxPly>& path, eval::PawnHashTable& pawn_tt,
                          eval::EvalCache& eval_cache, const eval::MaterialWeights* material_weights,
                          SearchLimits* limits = nullptr) {
    SearchResult result;

    MoveList moves;
    board::generate_legal_moves(pos, moves);

    if (moves.empty()) {
        // Nothing to play: report the terminal score with a null
        // best_move (Move::is_null()) rather than an arbitrary one.
        result.score = in_check(pos) ? -kMateScore : kDrawScore;
        result.nodes = 1;
        return result;
    }

    // Root-level TT interaction, added last session alongside move
    // ordering (deferred the session before specifically until ordering
    // existed to consume it -- see docs/DECISIONS.md, 2026-08-15 TT
    // entry, sub-decision 4). Probed only for an ordering hint below,
    // never for an early return/cutoff: the root always needs its FULL
    // legal move list reviewed to report a genuine best move (there's
    // no "skip searching, trust the cache" shortcut that would still
    // give a UCI-quality result), unlike internal negamax() nodes which
    // are free to trust a sufficiently-deep proven bound.
    const std::uint64_t root_key = pos.zobrist_hash;
    path[0] = root_key; // See this function's header comment.
    const TTProbeResult root_probe = tt.probe(root_key, 0);
    const Move tt_move = root_probe.hit ? root_probe.move : Move();

    order_moves(moves, pos, tt_move, killers, /*ply=*/0, history, cont_history,
                /*prev_piece=*/board::PieceType::None, /*prev_to=*/0);

    int alpha = aspiration_alpha;
    const int beta = aspiration_beta;
    Move best_move = moves[0];

    for (int i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        // Same rationale as negamax()'s own move loop (its header
        // comment): read before make_move() vacates the from-square,
        // threaded into this move's own negamax() children below as
        // their `prev_piece`/`prev_to`.
        const board::PieceType moved_piece = board::piece_type_of(pos.piece_at(move.from()));
        UndoInfo undo;
        board::make_move(pos, move, undo);

        // Check extensions (negamax()'s own header comment) apply
        // symmetrically at the root: a root move that gives check
        // deserves the same extra ply any other checking move in the
        // tree gets. `ply + 1 < kMaxPly` is always true here (ply is 0
        // at the root, `kMaxPly` far larger), but the guard is kept
        // identical to negamax()'s own for a single, consistently-
        // applied rule rather than a root-specific shortcut.
        const bool move_gives_check = in_check(pos);
        const int extension = (move_gives_check && 1 < kMaxPly) ? kCheckExtensionPly : 0;

        // Root-level PVS, mirroring negamax()'s move loop (see its
        // comments for the full rationale) with one deliberate
        // difference: first move -- now the ordering-selected best
        // candidate, not just move-generation order -- gets the full
        // [alpha, beta] window (the aspiration window when one is in
        // effect, not necessarily the full (-inf,+inf) range -- see
        // this function's header comment), and later moves at depth >= 2
        // get a null-window probe first, re-searched with the full
        // window only on a fail-high that's still below beta -- but
        // later moves specifically at depth == 1 still skip straight to
        // a full-window search here, unlike negamax()'s own loop (which
        // dropped this special case this session, now that quiescence
        // search genuinely responds to a narrow window -- see negamax()'s
        // header comment). The difference is deliberate, not a stale
        // leftover: a depth-1 REQUEST only ever reaches this branch via
        // search_fixed_depth(pos, 1) or iterative deepening's mandatory
        // first iteration, both of which always pass the full
        // (-inf, +inf) window in the first place (no previous score to
        // aspirate around yet -- search_iterative_deepening()'s own
        // comments), so `beta` here is always kInfinity when depth == 1
        // specifically, and a null-window probe against an
        // already-infinite beta has nothing to gain from skipping to.
        int score;
        if (i == 0 || depth == 1) {
            score = -negamax(pos, depth - 1 + extension, -beta, -alpha, 1, result.nodes, tt, killers,
                              history, cont_history, moved_piece, move.to(), game_history, path,
                              pawn_tt, eval_cache, material_weights, /*allow_null_move=*/true, limits);
        } else {
            score = -negamax(pos, depth - 1 + extension, -alpha - 1, -alpha, 1, result.nodes, tt,
                              killers, history, cont_history, moved_piece, move.to(), game_history,
                              path, pawn_tt, eval_cache, material_weights, /*allow_null_move=*/true,
                              limits);
            if ((limits == nullptr || !limits->stopped) && score > alpha && score < beta) {
                score = -negamax(pos, depth - 1 + extension, -beta, -alpha, 1, result.nodes, tt,
                                  killers, history, cont_history, moved_piece, move.to(), game_history,
                                  path, pawn_tt, eval_cache, material_weights, /*allow_null_move=*/true,
                                  limits);
            }
        }

        board::unmake_move(pos, move, undo);

        if (limits != nullptr && limits->stopped) {
            // This move's own score came from a subtree truncated by
            // the deadline (SearchLimits' own doc comment) -- stop
            // trying further root moves and don't let it update
            // alpha/best_move even transiently. The caller
            // (search_iterative_deepening()) discards this whole
            // iteration's SearchResult in favor of the previous one,
            // so best_move/alpha as they stood BEFORE this move is all
            // that matters from here, and even that is about to be
            // thrown away one level up.
            break;
        }

        if (score > alpha) {
            alpha = score;
            best_move = move;
        }
        if (alpha >= beta) {
            // Fail-high against the (possibly narrow, aspiration) window
            // -- with a full (-inf,+inf) window this can never trigger
            // (beta == kInfinity, no real score reaches it), so this is
            // a no-op for search_fixed_depth(). With a narrow aspiration
            // window it means this iteration's result is only a bound,
            // not exact -- searching the remaining moves under the same
            // too-narrow window can't produce a trustworthy result
            // either, so stop immediately rather than waste nodes; the
            // caller (search_iterative_deepening()) is expected to
            // detect this (result.score >= aspiration_beta) and re-
            // search with a wider window.
            break;
        }
    }

    // Whether `alpha` here is an exact score, or only a bound, depends
    // on how the loop above ended relative to the window passed in --
    // see this function's header comment for what the caller is
    // expected to check. Only store an Exact TT entry when the result
    // genuinely is one (CPW "Node Types" -- storing an unproven exact
    // bound as Exact would let a later probe trust a value that isn't
    // actually exact): a fail-high (the break above) only proves a
    // LOWER bound, and a fail-low (no move ever beat aspiration_alpha,
    // so `alpha` here still equals the original aspiration_alpha
    // exactly) only proves an UPPER bound -- both mirror negamax()'s own
    // Exact/Lower/Upper classification at the bottom of that function,
    // just evaluated over the whole root call instead of one node.
    const Bound bound_type = alpha <= aspiration_alpha ? Bound::Upper
                            : alpha >= aspiration_beta  ? Bound::Lower
                                                         : Bound::Exact;
    // Same reasoning as negamax()'s own guarded store: an interrupted
    // loop's `alpha`/`best_move` may reflect a truncated subtree from
    // whichever move was in flight when `stopped` became true, not a
    // genuine root result for this depth -- see this function's own
    // header comment on `limits`. The caller (search_iterative_
    // deepening()) discards the whole SearchResult in this case anyway;
    // this additionally keeps the TT itself clean.
    if (limits == nullptr || !limits->stopped) {
        tt.store(root_key, depth, alpha, bound_type, best_move, 0);
    }

    result.best_move = best_move;
    result.score = alpha;
    result.depth_completed = depth;
    // Skipped when interrupted -- see the guarded TT store just above
    // for the same reasoning: this whole SearchResult is about to be
    // discarded wholesale by search_iterative_deepening() in that case
    // (SearchLimits' own doc comment), so walking the TT for a PV
    // nobody will read is wasted work.
    if (limits == nullptr || !limits->stopped) {
        result.pv = extract_pv(pos, tt, best_move, depth);
    }
    return result;
}

} // namespace

namespace {
// Forward declaration only -- the real definition is further down this
// file (still the same anonymous namespace: unlike a named namespace,
// an anonymous `namespace { ... }` block refers to the SAME unique,
// internal-linkage namespace everywhere it's reopened within one
// translation unit, so this declaration and that later definition are
// one and the same entity, not two). Declared here, ahead of
// search_fixed_depth() below, purely so that function (which now also
// spawns Lazy SMP helpers -- see its own doc comment, search.h) can
// call it without needing to be physically relocated after
// search_iterative_deepening()'s own, much larger, block of helper
// machinery -- keeping search_fixed_depth() in its original place in
// the file, right after search_root(), was judged less disruptive to
// this file's existing structure than moving it.
void run_lazy_smp_helper(Position pos, int max_depth, TranspositionTable& tt,
                          std::span<const std::uint64_t> game_history,
                          const eval::MaterialWeights* material_weights,
                          const std::atomic<bool>& stop, std::uint64_t& nodes_out);
} // namespace

SearchResult search_fixed_depth(Position& pos, int depth, std::span<const std::uint64_t> game_history,
                                 const eval::MaterialWeights* material_weights, int num_threads,
                                 std::size_t hash_size_mb) {
    assert(depth >= 1 && "search_fixed_depth: depth must be at least 1");

    // Fresh, private tables for this one call (see tt.h's header
    // comment, which applies equally to KillerTable/HistoryTable/
    // ContinuationHistoryTable -- search/ordering.h). `hash_size_mb`
    // (defaulting to search.h's kDefaultTTSizeMB) is the UCI `Hash`
    // option's value (ROADMAP.md Phase 8) -- the table itself is still
    // constructed fresh per call, not yet a persistent global resized
    // in place (tt.h's own LIFETIME NOTE). make_transposition_table()
    // (above) falls back to a smaller size rather than crashing if
    // `hash_size_mb` is too large for this machine to actually satisfy.
    TranspositionTable tt = make_transposition_table(hash_size_mb);
    KillerTable killers;
    HistoryTable history;
    ContinuationHistoryTable cont_history;
    // Fresh, zero-initialized per-ply hash record for this one call (see
    // negamax()'s header comment and is_draw_by_rule()) -- a fixed-size
    // stack array, not heap-allocated (ARCHITECTURE.md "Memory & Cache"),
    // sized via search/ordering.h's existing kMaxPly rather than a new
    // constant.
    std::array<std::uint64_t, kMaxPly> path{};
    // Fresh, private pawn hash table for this one call (same lifetime
    // scoping/rationale as tt/killers/history just above -- see
    // eval/pawn_tt.h's header comment).
    eval::PawnHashTable pawn_tt(kDefaultPawnTTSizeKB);
    // Fresh, private eval cache for this one call, same lifetime
    // scoping/rationale as pawn_tt just above (see eval/eval_cache.h's
    // header comment).
    eval::EvalCache eval_cache(kDefaultEvalCacheSizeKB);

    // Lazy SMP (ROADMAP.md Phase 7's own last item, "Verify no strength
    // regression..." — search.h's own doc comment on this function's
    // `num_threads` parameter has the full contract): same spawn/join
    // pattern as search_iterative_deepening()'s own below, just wrapped
    // around this function's single fixed-depth search_root() call
    // instead of an outer iterative-deepening loop — there's no
    // "between iterations" to spawn helpers ahead of here, so they're
    // spawned immediately, before the one search_root() call below, and
    // told to stop as soon as it returns. `run_lazy_smp_helper()` itself
    // (this file, just above) has no knowledge of which of its two
    // callers (this function or search_iterative_deepening()) started
    // it — same helper function, same TT-sharing/heap-allocated-tables
    // contract, either way.
    const int effective_threads = num_threads < 1 ? 1 : num_threads;
    std::atomic<bool> smp_stop{false};
    std::vector<std::thread> helpers;
    std::vector<std::uint64_t> helper_nodes;
    if (effective_threads > 1) {
        const std::size_t n = static_cast<std::size_t>(effective_threads - 1);
        helpers.reserve(n);
        helper_nodes.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            Position helper_pos = pos; // synchronous copy on the calling thread -- same
                                        // ordering rationale as search_iterative_deepening()'s
                                        // own identical comment below.
            helpers.emplace_back(run_lazy_smp_helper, std::move(helper_pos), depth, std::ref(tt),
                                  game_history, material_weights, std::cref(smp_stop),
                                  std::ref(helper_nodes[i]));
        }
    }

    // No previous iteration's score to aspirate around (see
    // search_iterative_deepening() below and docs/DECISIONS.md's
    // aspiration-windows entry) -- always the full window.
    SearchResult result = search_root(pos, depth, -kInfinity, kInfinity, tt, killers, history,
                                       cont_history, game_history, path, pawn_tt, eval_cache,
                                       material_weights);

    // Same stop/join/fold-in-node-counts pattern as
    // search_iterative_deepening()'s own below -- no-op (empty
    // `helpers`) when `effective_threads <= 1`, matching this whole
    // block's absence before this parameter existed.
    if (!helpers.empty()) {
        smp_stop.store(true, std::memory_order_relaxed);
        for (std::thread& helper : helpers) {
            helper.join();
        }
        std::uint64_t total_nodes = result.nodes;
        for (std::uint64_t n : helper_nodes) {
            total_nodes += n;
        }
        result.nodes = total_nodes;
    }

    return result;
}

namespace {

/// One Lazy SMP helper thread's own search loop (ROADMAP.md Phase 7,
/// search.h's search_iterative_deepening() doc comment has the full
/// contract). Runs entirely on the helper thread this is invoked on;
/// `pos` here is already that thread's own PRIVATE copy (moved in by
/// the caller -- see search_iterative_deepening() below for why the
/// copy itself must happen on the calling thread, before this function
/// starts running concurrently with anything else). `tt` is the one
/// object genuinely shared with the main thread and every other helper
/// (safe for exactly this -- search/tt.h's THREAD-SAFETY NOTE); every
/// other table here is this thread's own, private, and never touched by
/// any other thread. `stop`, when observed true, ends this helper's
/// loop at the next depth boundary AND (via SearchLimits::external_stop
/// below) mid-iteration, the same bounded-unwind way a deadline does
/// (search.h's SearchLimits doc comment). `nodes_out` receives this
/// helper's own total node count once it's done, for the caller to fold
/// into the overall SearchResult -- a plain (non-atomic) uint64_t is
/// fine here since each helper thread writes to its own, distinct
/// `nodes_out` slot (see the caller's per-helper storage below), never
/// one shared across threads.
void run_lazy_smp_helper(Position pos, int max_depth, TranspositionTable& tt,
                          std::span<const std::uint64_t> game_history,
                          const eval::MaterialWeights* material_weights,
                          const std::atomic<bool>& stop, std::uint64_t& nodes_out) {
    KillerTable killers;
    // Heap-allocated, not stack locals: HistoryTable and
    // ContinuationHistoryTable together are roughly 176KB
    // (HistoryTable: kNumColors*64*64 ints ~= 32KB; ContinuationHistoryTable:
    // kNumPieceTypes*64*kNumPieceTypes*64 ints ~= 144KB) -- fine on the
    // MAIN thread's own stack (this function's caller's counterparts to
    // these two tables, in search_iterative_deepening() below, stay
    // stack-allocated exactly as before), but a genuinely new OS thread
    // created via std::thread does NOT get the main thread's stack size
    // on every platform: POSIX only guarantees implementation-defined
    // sizing for a newly created thread absent an explicit request, and
    // in practice this is 512KB by default on macOS (vs 8MB on Linux, a
    // 16x difference) -- 176KB of fixed locals alone already eats over
    // a third of that budget before negamax()'s own recursive frames
    // are added on top, and did in fact overflow it in CI (macOS
    // Debug/Release specifically -- see docs/DECISIONS.md, 2026-09-03
    // (3)). Heap allocation sidesteps the platform's thread-stack-size
    // default entirely, rather than depending on it -- the more robust
    // fix, since any future search/eval feature that adds a few more KB
    // of per-node stack usage would otherwise silently reopen the exact
    // same margin-of-headroom problem.
    auto history = std::make_unique<HistoryTable>();
    auto cont_history = std::make_unique<ContinuationHistoryTable>();
    std::array<std::uint64_t, kMaxPly> path{};
    eval::PawnHashTable pawn_tt(kDefaultPawnTTSizeKB);
    eval::EvalCache eval_cache(kDefaultEvalCacheSizeKB);

    std::uint64_t total_nodes = 0;
    for (int depth = 1; depth <= max_depth; ++depth) {
        if (stop.load(std::memory_order_relaxed)) {
            break;
        }

        // Deliberately simpler than the main thread's own loop: always
        // the full (-inf, +inf) window (no aspiration windows -- Lazy
        // SMP helpers exist to widen/diversify what's been explored via
        // the shared TT, not to replicate the primary search's every
        // refinement; see docs/DECISIONS.md and search.h's own doc
        // comment on this function's caller). `tt.new_search()` is NOT
        // called here -- only the main thread ages the shared table
        // (once per ITS OWN depth iteration), matching the "one writer"
        // half of current_age_'s thread-safety story (search/tt.h).
        SearchLimits limits;
        limits.has_deadline = false;
        // const_cast: SearchLimits::external_stop is a non-const
        // pointer (negamax()/quiescence() only ever read through it),
        // but `stop` itself is held const here since this function
        // never writes it -- only the main thread does (see the
        // caller). Safe: no write ever happens through this pointer.
        limits.external_stop = const_cast<std::atomic<bool>*>(&stop);

        const SearchResult r = search_root(pos, depth, -kInfinity, kInfinity, tt, killers, *history,
                                            *cont_history, game_history, path, pawn_tt, eval_cache,
                                            material_weights, &limits);
        total_nodes += r.nodes;

        if (limits.stopped) {
            break;
        }
    }
    nodes_out = total_nodes;
}

} // namespace

SearchResult search_iterative_deepening(Position& pos, int max_depth, int time_limit_ms,
                                         std::span<const std::uint64_t> game_history,
                                         IterationCallback on_iteration,
                                         const eval::MaterialWeights* material_weights,
                                         int num_threads, std::atomic<bool>* external_stop,
                                         std::size_t hash_size_mb) {
    assert(max_depth >= 1 && "search_iterative_deepening: max_depth must be at least 1");

    const auto start_time = std::chrono::steady_clock::now();

    // Tables shared across every iteration of THIS call (see tt.h's
    // header comment) -- this cross-iteration sharing is where the
    // ordering/TT machinery's real value comes from right now, since
    // search_fixed_depth() on its own always starts from empty tables.
    // Unlike the TT (aged via new_search() each iteration), killers,
    // history, and continuation history are NOT reset between
    // iterations on purpose: a killer or a historically-good quiet move
    // (plain or continuation) from a shallower iteration is still a
    // reasonable ordering bet for the next, deeper one, and letting them
    // persist is exactly how real engines use iterative deepening to
    // make each successive iteration cheaper.
    TranspositionTable tt = make_transposition_table(hash_size_mb);
    KillerTable killers;
    // Heap-allocated, not stack locals -- see run_lazy_smp_helper()'s
    // own identical pattern and comment just above in this file for the
    // full rationale (docs/DECISIONS.md, 2026-09-03 (3)): together
    // roughly 176KB, which fits comfortably on the CALLING thread's own
    // stack when that's an ordinary main thread (this function's
    // original, only caller), but does NOT fit macOS's fixed 512KB
    // default new-thread stack size. That distinction stopped being
    // hypothetical for THIS function specifically as of Session 75
    // (docs/SESSIONS.md/DECISIONS.md, 2026-09-04): `src/uci/uci.cpp`'s
    // pondering support (`start_pondering()`) now calls this function
    // directly from a freshly spawned `std::thread`, not just via
    // run_lazy_smp_helper()'s own already-guarded path -- confirmed by
    // real macOS CI (`Bus error`/ASan `BUS on unknown address`,
    // isolated to exactly the new pondering tests) after this function
    // still had these two tables as plain stack locals. Heap allocation
    // makes this function safe to call from ANY thread regardless of
    // its stack size, present or future callers alike, rather than
    // fixing only the one call site that happened to surface the bug.
    auto history = std::make_unique<HistoryTable>();
    auto cont_history = std::make_unique<ContinuationHistoryTable>();
    // Shared across every iteration of this call too, same rationale as
    // tt/killers/history just above (see negamax()'s header comment and
    // is_draw_by_rule()) -- a later, deeper iteration re-deriving the
    // same repetition information from scratch would be wasted work,
    // and more importantly wouldn't change anything: `path` is
    // overwritten ply-by-ply as each iteration's own root-to-here search
    // proceeds, the same reuse pattern as within a single negamax() call.
    std::array<std::uint64_t, kMaxPly> path{};
    // Shared across every iteration too -- see eval/pawn_tt.h's header
    // comment for the same lifetime rationale as tt/killers/history.
    eval::PawnHashTable pawn_tt(kDefaultPawnTTSizeKB);
    // Shared across every iteration too, same rationale as pawn_tt just
    // above (see eval/eval_cache.h's header comment).
    eval::EvalCache eval_cache(kDefaultEvalCacheSizeKB);

    // Depth 1 always runs unconditionally, before any time check, so
    // there's always a legal best_move to fall back on (see search.h's
    // header comment). Full window: there's no previous iteration yet
    // to aspirate around (see the depth-2-onward loop below).
    tt.new_search();
    SearchResult result = search_root(pos, 1, -kInfinity, kInfinity, tt, killers, *history,
                                       *cont_history, game_history, path, pawn_tt, eval_cache,
                                       material_weights);
    std::uint64_t total_nodes = result.nodes;

    // Position already over (checkmate/stalemate at the root): every
    // deeper iteration would just regenerate the same empty move list
    // and return the same terminal result, so stop immediately instead
    // of wastefully repeating it. No callback fired -- IterationCallback's
    // own doc comment (search.h): nothing meaningful to report for a
    // terminal position.
    if (result.best_move.is_null()) {
        return result;
    }

    // `result.nodes` already equals `total_nodes` at this exact point
    // (depth 1 is the very first iteration), so no cumulative-total
    // rewrite is needed before this first callback the way the
    // depth-2-onward loop below needs one -- see that loop's own
    // comment.
    if (on_iteration) {
        on_iteration(result);
    }

    // Lazy SMP (ROADMAP.md Phase 7, search.h's own doc comment on this
    // function's `num_threads` parameter has the full contract): spawn
    // helper threads now, once depth 1 has proven there's real work
    // (the early return above), and before the calling thread's own
    // depth-2-onward loop starts mutating `pos` again via search_root()
    // -- each helper's private Position COPY is taken here, on the
    // calling thread, synchronously, one at a time, each one strictly
    // before that helper's thread is even constructed (let alone
    // running) and strictly before the NEXT helper's own copy is taken
    // -- so every copy of `pos` happens while `pos` is guaranteed
    // quiescent (search_root() always restores it before returning),
    // with no concurrent mutation from either the calling thread (which
    // hasn't reached its own next search_root() call yet) or any other
    // helper (which only ever touches its OWN private copy, never
    // `pos` itself). Constructing the copy inside the helper thread's
    // own function body instead would race against the calling
    // thread's very next search_root() call, which is exactly the kind
    // of data race this ordering avoids.
    const int effective_threads = num_threads < 1 ? 1 : num_threads;
    std::atomic<bool> smp_stop{false};
    std::vector<std::thread> helpers;
    std::vector<std::uint64_t> helper_nodes;
    if (effective_threads > 1) {
        const std::size_t n = static_cast<std::size_t>(effective_threads - 1);
        helpers.reserve(n);
        helper_nodes.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            Position helper_pos = pos; // synchronous copy on the calling thread -- see above
            helpers.emplace_back(run_lazy_smp_helper, std::move(helper_pos), max_depth,
                                  std::ref(tt), game_history, material_weights, std::cref(smp_stop),
                                  std::ref(helper_nodes[i]));
        }
    }

    for (int depth = 2; depth <= max_depth; ++depth) {
        if (time_limit_ms > 0) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start_time)
                                         .count();
            if (elapsed_ms >= time_limit_ms) {
                break;
            }
        }
        // `external_stop` (this function's own doc comment, search.h) —
        // the between-iteration counterpart to the mid-iteration check
        // threaded into `limits` below. Checked independently of
        // `time_limit_ms`, since a pondering call (the canonical caller)
        // passes `time_limit_ms == 0` (no deadline at all) and relies on
        // this flag alone.
        if (external_stop != nullptr && external_stop->load(std::memory_order_relaxed)) {
            break;
        }

        tt.new_search();
        SearchResult next;

        // Mid-search time-budget interruption (ROADMAP.md Priority Fix,
        // search.h's SearchLimits): one instance per depth iteration,
        // sharing the SAME deadline (and `stopped` flag) across every
        // search_root() call this iteration makes below, including any
        // aspiration-window fail-high/fail-low retries -- all of them
        // are still spending this one iteration's time budget, not
        // separate ones. Never constructed for depth == 1 above (that
        // call passes no `limits` at all, preserving the "always have a
        // legal move" guarantee -- see search.h's own doc comment).
        SearchLimits limits;
        limits.has_deadline = time_limit_ms > 0;
        if (limits.has_deadline) {
            limits.deadline = start_time + std::chrono::milliseconds(time_limit_ms);
        }
        // `external_stop`, when the caller provided one (this function's
        // own doc comment, search.h) — independent of `has_deadline`
        // above; negamax()/quiescence() already check both conditions
        // unconditionally once a non-null `limits` is threaded through
        // (search.h's SearchLimits doc comment), so this alone is enough
        // to make a mid-iteration external stop request work, with no
        // further change needed in negamax()/quiescence() themselves.
        // `nullptr` when the caller didn't pass one (every existing
        // caller before this parameter existed) — behaviorally identical
        // to before.
        limits.external_stop = external_stop;

        // Aspiration windows (CPW "Aspiration Windows"): the previous
        // iteration's score is usually a good estimate of this
        // iteration's score too (positions rarely swing wildly between
        // one ply of search depth and the next), so start this
        // iteration with a narrow window centered on it instead of the
        // full (-inf,+inf) range -- a narrower window means more beta
        // cutoffs happen sooner throughout the tree, at the cost of an
        // occasional fail-high/fail-low needing a wider re-search when
        // the estimate turns out wrong. Skipped when the previous
        // score is already in mate-score territory (see kMateThreshold,
        // search.h): mate scores are a different regime entirely, far
        // outside any small centipawn-sized window, so aspirating
        // around one would just guarantee a wasted first attempt for no
        // benefit -- go straight to the full window instead.
        if (result.score > -kMateThreshold && result.score < kMateThreshold) {
            int delta = kAspirationInitialDelta;
            int window_alpha = result.score - delta;
            int window_beta = result.score + delta;
            if (window_alpha < -kInfinity) {
                window_alpha = -kInfinity;
            }
            if (window_beta > kInfinity) {
                window_beta = kInfinity;
            }

            for (;;) {
                next = search_root(pos, depth, window_alpha, window_beta, tt, killers, *history,
                                    *cont_history, game_history, path, pawn_tt, eval_cache,
                                    material_weights, &limits);

                if (limits.stopped) {
                    // Interrupted mid-retry -- see the post-loop
                    // handling below; the retry loop itself has nothing
                    // trustworthy left to check `next.score` against
                    // (SearchLimits' own doc comment), so stop
                    // immediately rather than attempting another widen.
                    break;
                }

                if (next.score <= window_alpha && window_alpha > -kInfinity) {
                    // Fail low: the true score is <= window_alpha, exact
                    // value unknown -- search_root()'s own best_move for
                    // this attempt isn't trustworthy either (see its
                    // header comment), so this attempt's result is
                    // discarded entirely by the next loop iteration.
                    // Widen downward and retry.
                    delta *= 2;
                    window_alpha = result.score - delta;
                    if (window_alpha < -kInfinity) {
                        window_alpha = -kInfinity;
                    }
                } else if (next.score >= window_beta && window_beta < kInfinity) {
                    // Fail high: symmetric case, widen upward and retry.
                    delta *= 2;
                    window_beta = result.score + delta;
                    if (window_beta > kInfinity) {
                        window_beta = kInfinity;
                    }
                } else {
                    // Either the score landed strictly inside the window
                    // (an exact result -- CPW "Aspiration Windows"), or
                    // the window has already widened out to the full
                    // (-inf,+inf) range, which is unconditionally exact
                    // by construction (same as search_fixed_depth()'s
                    // own always-full-window search) -- either way,
                    // done.
                    break;
                }
            }
        } else {
            next = search_root(pos, depth, -kInfinity, kInfinity, tt, killers, *history, *cont_history,
                                game_history, path, pawn_tt, eval_cache, material_weights, &limits);
        }

        // `next.nodes` reflects real work done regardless of whether
        // this iteration was interrupted -- every node visited, even
        // ones whose result got discarded by a poisoned-score guard
        // (search.cpp's negamax()/search_root() own comments), took
        // real wall-clock time, so it's counted the same way a fully
        // completed iteration's nodes are (see search.h's own doc
        // comment on SearchResult::nodes).
        total_nodes += next.nodes;

        if (limits.stopped) {
            // This iteration's own SearchResult was truncated mid-
            // search -- not trustworthy even as a bound, unlike a
            // genuine aspiration fail-high/fail-low (SearchLimits' own
            // doc comment) -- discard it wholesale and keep the
            // previous, fully-completed iteration's `result` as the
            // final answer instead. No deeper iteration would fare any
            // better (the same, already-exhausted deadline applies to
            // it too), so stop the outer loop entirely rather than
            // trying depth + 1.
            break;
        }

        result = next;

        // Report CUMULATIVE nodes to the callback (matching the
        // conventional meaning of a UCI `info nodes` line -- total
        // work for the whole `go` command so far, not just this one
        // iteration -- see IterationCallback's own doc comment,
        // search.h), overwriting `next`'s own per-iteration-only count
        // that search_root() reported. `result.nodes` picks this up
        // too, ahead of the final rewrite below, so it's already
        // correct even if this turns out to be the last iteration.
        result.nodes = total_nodes;
        if (on_iteration) {
            on_iteration(result);
        }
    }

    // Lazy SMP: the calling thread's own loop above is done (whether by
    // reaching max_depth, running out of time, or being interrupted) --
    // tell every helper thread to stop at its own next check (the same
    // periodic check negamax()/quiescence() already do for `deadline`,
    // now also checking `smp_stop` via SearchLimits::external_stop —
    // search.h's own doc comment), join them, and fold each one's own
    // node count into the total this call reports. No-op (empty
    // `helpers`) when `effective_threads <= 1`, matching this whole
    // block's absence before this parameter existed.
    if (!helpers.empty()) {
        smp_stop.store(true, std::memory_order_relaxed);
        for (std::thread& helper : helpers) {
            helper.join();
        }
        for (std::uint64_t n : helper_nodes) {
            total_nodes += n;
        }
    }

    result.nodes = total_nodes;
    return result;
}

} // namespace nightwing::search
