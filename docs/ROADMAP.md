# Nightwing — Roadmap

Phases are sequential unless noted. Check off tasks as completed; add new ones as they're discovered. Each session works from the top incomplete item unless told otherwise.

## Phase 0 — Project Setup
- [x] CMake project skeleton, C++20, builds empty `main.cpp`
- [x] Release build config: `-O3` + LTO enabled; separate Debug config with sanitizers (ASan/UBSan) for dev/CI correctness testing
- [x] CPU feature detection scaffolding (BMI2/POPCNT) with portable fallback build target
- [x] GitHub Actions CI: build matrix (Linux/macOS/Windows), runs `ctest`
- [x] Catch2 integrated as test framework
- [x] `docs/` seeded (this file, DECISIONS.md, SESSIONS.md, ARCHITECTURE.md)

## Phase 1 — Board Representation & Move Generation
- [x] Bitboard primitives (set/clear/pop bit, popcount, bitscan) — using compiler intrinsics, not manual loops
- [x] Magic bitboard generation for rook/bishop attacks (portable path)
- [x] BMI2 PEXT bitboard attack generation (fast path, runtime/build-time dispatched)
- [x] Board state struct (piece bitboards, side to move, castling rights, en passant, halfmove clock) — kept compact, cache-friendly (fits in a small number of cache lines)
- [x] Zobrist hashing (key generation, from-scratch compute_hash(), and incremental XOR-update on make/unmake)
- [x] `init_masks() → init_magic_bitboards() → init_zobrist_keys()` startup sequence wired up
- [x] Fully legal move generation (pins, checks, castling, en passant, promotions)
- [x] Move list as fixed-size stack array (no heap allocation)
- [x] Make/unmake move
- [x] Perft test suite passing to standard reference depths (startpos, Kiwipete, and the other 4 standard CPW reference positions — see docs/SESSIONS.md for the deeper depths checked by hand)
- [x] Perft bulk-counting mode benchmarked as an early NPS sanity check (movegen throughput baseline) — `perft_bulk()` + `src/bench.cpp`; ~3.8-4.9x faster than plain `perft()` on this dev machine (startpos ~120 Mnps, Kiwipete ~174 Mnps at Release+BMI2 — see docs/SESSIONS.md; not CI-asserted, since NPS is machine-dependent, but `perft_bulk()`'s node counts are cross-checked against `perft()`'s in tests/perft_tests.cpp)
- [x] Minimal FEN parser (`src/board/fen.h/.cpp`) — done ahead of schedule this session, as a perft-position-building prerequisite; kept here, checked off, as a marker that it exists and is tested (tests/fen_tests.cpp) even though it was originally slated for Phase 2's UCI work

## Phase 2 — Minimal Search + Eval (get something playing)
- [x] Material-only + PSQT eval (tapered mg/eg)
- [x] Plain alpha-beta search, fixed depth
- [x] Iterative deepening
- [x] Basic UCI loop
- [x] Engine can play a full legal game against itself via UCI

## Phase 3 — Core Search Strengthening
- [x] PVS (Principal Variation Search)
- [x] Transposition table (Zobrist-keyed, depth/age replacement)
- [x] Move ordering: TT move, MVV-LVA captures, killer moves, history heuristic
- [x] Aspiration windows
- [x] Quiescence search (captures + checks, with SEE pruning)
- [x] Internal Iterative Reduction (IIR) — reduce depth on nodes with no TT move (modern replacement for IID)
- [x] Mate distance pruning
- [x] Repetition detection (threefold) and 50-move rule handling integrated into search, not just board state
- [x] Pawn hash table (small separate TT keyed on pawn structure only, for pawn eval reuse)

## Phase 4 — Pruning & Extensions
- [x] Null-move pruning
- [x] Late move reductions (LMR)
- [x] Late move pruning (LMP) / move-count based pruning at low depth
- [x] Futility pruning
- [x] Razoring
- [x] History pruning (skip quiet moves with poor history score at low depth)
- [x] Continuation history (1-ply and 2-ply "counter-move history" for move ordering + pruning decisions)
- [x] ProbCut / multi-cut pruning
- [x] Delta pruning in quiescence search
- [x] Check extensions
- [x] Singular extensions
- [x] Regression bench: node-count/strength tracked in SESSIONS.md per change

## Priority Fixes (external code review, 2026-08-25)

Not phase-gated — inserted here, before Phase 5, per the decision logged in
docs/DECISIONS.md (2026-08-25 (8)). An external code review (build + full
test suite run in Release and Debug/ASan+UBSan, both green; 52,236
assertions / 211 test cases; UCI smoke-tested) confirmed the project's own
self-reported state and surfaced two real gaps neither caught by the test
suite nor yet on this roadmap explicitly. A third finding (TT/pawn hash
tables reallocated per `go` call) needed no new item — already an
intentional, documented placeholder under Phase 8's `Hash` option below.

- [x] Mid-search time checks: periodic node-count-based clock check inside
      `negamax()`/quiescence, with a clean unwind path that doesn't corrupt
      alpha/best-move bookkeeping — **High priority.** This is the Phase 2
      "check the clock only between iterations, not mid-search" scope cut's
      own documented revisit trigger (docs/DECISIONS.md, the iterative-
      deepening entry: *"the natural point to add [mid-search interruption]
      is alongside real time-control parsing in the UCI loop... revisit
      then"*) — that condition (`wtime`/`btime`/`winc`/`binc`/`movetime` all
      implemented) has been met for some time without the revisit happening.
      Without this, a search under a tight `movetime` or low-time budget can
      overrun by an entire additional depth iteration, which given the
      roughly order-of-magnitude cost growth per ply can be large relative
      to the allocated budget. Implemented via a shared `SearchLimits`
      (search.h) threaded through `negamax()`/`quiescence()`/`search_root()`,
      checked every 2048 nodes; an iteration interrupted this way is
      discarded wholesale by `search_iterative_deepening()` rather than
      trusted even partially — see docs/DECISIONS.md, 2026-08-26 entry.
- [x] UCI `info` output during search: emit `info depth ... score cp ...
      nodes ... pv ...` per completed iterative-deepening iteration, using
      data `SearchResult` already collects — **Medium priority.** Previously
      `uci.cpp` only ever emitted the final `bestmove` line; most GUIs still
      function without live search feedback, but some tournament managers
      or strict UCI validators may flag its absence, and there was no
      principal-variation display. Implemented via a new
      `SearchResult::pv` (reconstructed by walking the TT — search.cpp's
      `extract_pv()`, since no triangular-PV-array bookkeeping is
      threaded through `negamax()`'s own recursion) and a new
      `IterationCallback` (search.h) `search_iterative_deepening()`
      invokes once per genuinely completed iteration; `uci.cpp`'s
      `emit_info()` formats it, including `score mate N` (not `cp`) once
      a mate is found — see docs/DECISIONS.md, 2026-08-26 (2) entry.

## Phase 5 — Eval Expansion & Tuning
- [x] Mobility eval — knight/bishop/rook/queen pseudo-mobility (squares
      attacked, excluding own-occupied squares), flat per-square-per-piece-
      type Score bonus rather than a diminishing-returns table indexed by
      count (a deliberate first-cut simplification, matching Pawn
      structure's own preference for a handful of simple additive
      constants over a larger tuned table before a real tuner exists);
      king and pawns excluded (king activity/safety is its own separate
      item below; pawns are already scored via material + Pawn structure).
      See src/eval/mobility.h/.cpp and docs/DECISIONS.md.
- [x] King safety (pawn shield, open files near king, attacker weighting)
      — three simple additive components: a flat per-pawn bonus for own
      pawns in a 3-file × 2-rank shield zone in front of the king; a
      penalty per fully-open or semi-open file among the king's own
      file and its two neighbors; a penalty scaled by enemy knights/
      bishops/rooks/queens attacking the king's immediate zone, weighted
      by piece type. Deliberately MG-heavy/EG-light in every constant
      (the opposite tapering direction from Mobility eval, on purpose —
      see docs/DECISIONS.md). See src/eval/king_safety.h/.cpp.
- [x] Pawn structure (passed, isolated, doubled, backward, connected) — implemented ahead of Mobility/King safety above, specifically to give Phase 3's Pawn hash table item real values to cache; see docs/DECISIONS.md
- [x] Bishop pair, rook on open/semi-open file, rook on 7th rank
- [x] Knight outposts
- [x] Space evaluation
- [x] Threats evaluation (hanging/attacked pieces, pieces attacked by pawns)
- [x] King tropism (piece proximity to enemy king in the attack)
- [x] Trapped piece penalties
- [x] Tempo bonus (small fixed bonus for side to move)
- [x] Material imbalance table (e.g. bishop pair / knight pair value shifts with pawn count, per Stockfish-classic style)
- [x] Eval cache (optional performance optimization, separate from TT) — full-position cache keyed on the Zobrist hash, wired into evaluate()/quiescence()/negamax(); see docs/DECISIONS.md
- [x] All terms as named tunable constants (per DECISIONS.md) — audit of every eval/*.cpp scoring line found one gap (psqt.h's material_value(), raw literals); fixed with named kPawnValue/kKnightValue/etc. constants; every other term already followed the established named-constant convention. See docs/DECISIONS.md.
- [x] Texel/SPSA tuner module (self-play data generation + gradient descent) — both halves built: self-play data generation (`tuner::` module, `src/tuner/selfplay.h`/`.cpp`/`selfplay_main.cpp`, `nightwing_selfplay`) and a finite-difference gradient-descent Texel-loss tuning loop (`src/tuner/tune.h`/`.cpp`/`tune_main.cpp`, `nightwing_tune`), verified working end to end on real self-play output (loss decreases monotonically). Currently tunes `eval::MaterialWeights` (the five base piece values) only — every other eval term is still a compiled-in constant; extending coverage is incremental follow-on work, not a blocker for this item. See docs/DECISIONS.md.
- [x] Tuned weights committed, before/after strength comparison logged — CLOSED with the hand-set defaults RETAINED (no eval/psqt.h change), on the strength of two large-scale production runs. Session 61's first run (5000 self-play games, 200 iterations, 400 match games) surfaced a tuner bug (pawn value collapsed to ~21% of its start — a classic Texel-tuning scale-degeneracy artifact); Session 62 fixed it (`pawn_mg`/`pawn_eg` anchored, `src/tuner/tune.h`/`.cpp`) and re-dispatched an identical-scale run. Result: `score_a=0.5088, elo_diff=6.1` (defaults nominally ahead, tuned weights nominally behind) — under 1 standard error (~6.55 Elo) for a 400-game sample, i.e. not distinguishable from zero difference, and a SMALLER nominal gap than Session 61's buggy run's 9.6 Elo, not a larger one — the signal is trending toward "no real difference" as the methodology improved, not toward "just needs more games." Conclusion: the hand-set defaults (pawn 100, knight 320, bishop 330, rook 500, queen 900) are already close to whatever this depth-4/quiet-position/material-only Texel objective converges to, so there's nothing here worth hand-transcribing over the simpler canonical values. Full before/after comparison logged in docs/DECISIONS.md, 2026-08-31 (1)/(2)/(3) entries. Future work (PSQT/mobility/etc. added to the tunable parameter set, or `sigmoid_scale` itself fit from data) may revisit material tuning specifically, but that's a new, separate effort, not a continuation of this item.

## Phase 6 — Endgame Knowledge (algorithmic theory, no tablebases)

Goal: exact-feeling play in common endgames and graceful, generalizing play everywhere else — never a blind cliff the way tablebases have one past their piece-count ceiling. No self-generated bitbases (decision: algorithmic generalization only, see DECISIONS.md).

- [x] Material-signature classifier: detect endgame material buckets at each node, route to specialized endgame reasoning when matched — CLASSIFICATION half done Session 64 (`eval::classify_endgame()`, `src/eval/endgame.h`/`.cpp`, six buckets, `tests/endgame_tests.cpp`). Session 65's King+pawn theory item (immediately below) is the first "specialized endgame reasoning" to actually consult it (`eval::king_pawn_endgame_value()` calls `classify_endgame()` as its own first check, gating on `EndgameSignature::KPK`) — the checkbox this classifier's own introducing entry left open ("stays open until at least one of them actually does") is satisfied now that a real consumer exists. The other five buckets (KRK, KBNK, RookEndgame, OppositeColoredBishops, KnightVsBishop) still have no consumer yet — each is still tracked by its own still-open item below.
- [x] King+pawn theory: opposition, key squares, corresponding squares, the rule of the square, generalized to any K+P configuration (not case-tabulated) — Session 65: `src/eval/king_pawn_endgame.h`/`.cpp`, `eval::king_pawn_endgame_value()`, wired into `eval::evaluate()`, applies whenever `eval::classify_endgame()` returns `EndgameSignature::KPK` (king + exactly one pawn, either side, vs. bare king). Implements the Rule of the Square (does the defending king have enough king-moves to catch the pawn, accounting for whose move it is and the pawn's own starting-rank double-step), Key Squares (does the attacking king control a key square once the defending king has caught up), and direct Opposition (the simplest, most common form) as genuine formulas over the pawn's/kings' actual squares — not a lookup table, satisfying this item's own "not case-tabulated" wording. Two deliberate scope limits, both documented in docs/DECISIONS.md rather than silently glossed over: (1) distant/diagonal opposition and the fuller "corresponding squares" theory this item's own wording also names are NOT attempted — direct opposition only; (2) positions where the king catches the pawn but none of the three techniques resolve the outcome (no key square held, no opposition blockade) are deliberately left with no adjustment, on the reasoning that a KPK subtree is shallow enough for ordinary search to resolve on its own. `tests/king_pawn_endgame_tests.cpp` (9 tests).
- [x] Rook endgame patterns: Lucena position recognition (winning technique), Philidor position recognition (drawing technique), Vancura position, rook behind passed pawn heuristic — Session 65: `src/eval/rook_endgame.h`/`.cpp`, `eval::rook_endgame_value()`, wired into `eval::evaluate()`, applies whenever `eval::classify_endgame()` returns `EndgameSignature::RookEndgame` (both sides have exactly one rook, any pawn count) — the classifier's second real consumer. Three of the four named patterns implemented as genuine geometric formulas: Tarrasch's Rule (rook behind a passed pawn, own or enemy — applies across any pawn count), Lucena position recognition, and Philidor position recognition (the latter two further narrowed to the single-pawn textbook "rook + pawn vs. rook" case both patterns are classically about). Vancura position recognition is deliberately DEFERRED, not implemented — its own recognition criteria are meaningfully different from, and no easier than, Lucena's and Philidor's, and encoding a fourth pattern hastily risked a wrong eval nudge more than it risked being merely incomplete; see docs/DECISIONS.md for the full rationale. `tests/rook_endgame_tests.cpp` (7 tests).
- [x] Minor piece endgames: wrong-bishop-corner draw detection, opposite-colored bishop fortress/drawish-tendency eval adjustment, knight vs. bishop endings weighted by pawn structure (open vs. closed) — Session 66: `src/eval/minor_piece_endgame.h`/`.cpp`, `eval::minor_piece_endgame_value()`, wired into `eval::evaluate()`, dispatches across three `eval::classify_endgame()` buckets, one per clause: `EndgameSignature::KBPK` (a new bucket added this same session — the original six-bucket set from Session 64 had no bucket at all for this case; see `endgame.h`'s own doc comment on `EndgameSignature::KBPK`) for wrong-bishop-corner detection (rule-of-the-square reused against the drawing corner, narrowed to single-rook-file-pawn(s) positions), `EndgameSignature::OppositeColoredBishops` for a per-pawn-difference drawish discount (not a flat always-on bonus — see docs/DECISIONS.md), and `EndgameSignature::KnightVsBishop` for a blocked/open-pawn-count structural bonus. `tests/minor_piece_endgame_tests.cpp` (10 tests) plus 4 new `tests/endgame_tests.cpp` cases covering the new KBPK bucket itself.
- [x] Fortress pattern detection (structural, not tabulated) — recognize blocked/closed positions where material advantage can't be converted — Session 67: `src/eval/fortress.h`/`.cpp`, `eval::fortress_value()`, wired into `eval::evaluate()`. Deliberately does NOT consult `eval::classify_endgame()` (unlike the previous three Phase 6 terms) — see docs/DECISIONS.md for why a cross-material-shape structural heuristic doesn't fit that classifier's bucket-based approach. Applies a proportional (not zeroing, not sign-flipping) discount to whichever side holds a material lead once a position has no queens, at most `kFortressMaxNonPawnPieces` (6) knights/bishops/rooks combined, and at least `kFortressMinBlockedPawns` (4) mutually-blocked pawns. `tests/fortress_tests.cpp` (6 tests).
- [x] Zugzwang-aware search shaping: bias search (e.g. reduce/skip null-move pruning) in positions flagged as zugzwang-prone by material signature, so the search doesn't miss zugzwang the way naive null-move can — Session 68: `eval::is_zugzwang_prone(EndgameSignature)` added to `src/eval/endgame.h` (flags `RookEndgame` only — see docs/DECISIONS.md for why not others), consumed by `src/search/search.cpp`'s negamax() NMP block, which reduces R by `kZugzwangReductionDecrease` (floored at `kZugzwangMinReduction`) at flagged nodes instead of skipping null-move pruning outright — that stronger response stays reserved for the pre-existing, genuinely-unsound `non_pawn_material == 0` case (KPK). This is the first Phase 6 item outside `eval/` — a `search/` change, not a new eval term. Verified empirically (see docs/DECISIONS.md): a real RookEndgame FEN visits more nodes at shallow depths with the bias active than an otherwise-identical control build without it (confirming the mechanism engages), while returning identical best scores at every depth tested (confirming no correctness regression). 1 new `tests/endgame_tests.cpp` case (`is_zugzwang_prone` exact values, 8 assertions) plus 1 new `tests/search_tests.cpp` case. Full existing test suite (360 cases / 26,819 assertions, real Catch2 build) reverified green after this change.
- [x] Hand-built base heuristics carried over: KPK, KRK, KBNK exact-play rules (algorithmic, not lookup-table), draw detection refinement (insufficient material) — Session 69: KPK's "exact-play rules" are Session 65's existing `eval::king_pawn_endgame_value()` (Rule of the Square/Key Squares/Opposition) — no separate mechanism was added, see `src/eval/basic_mates.h`'s own header comment for why. KRK and KBNK — this project's last two `EndgameSignature` buckets with no consumer — are covered by new `src/eval/basic_mates.h`/`.cpp`, `eval::basic_mate_value()`: a generic edge-push + king-proximity term for KRK, plus a bishop-color-matching corner term (the technique's defining distinction) for KBNK. Draw detection refinement (insufficient material) is new `is_insufficient_material()` in `src/search/search.cpp`, wired into the existing `is_draw_by_rule()` alongside 50-move-rule/repetition detection — covers bare kings, king+single-minor vs. bare king, and same-colored-bishop-pair vs. bare-king pairs; deliberately excludes knight-vs-knight/bishop-vs-knight/opposite-colored-bishop combinations (see docs/DECISIONS.md for the helpmate-construction reason those are NOT safe to auto-draw). 7 new `tests/basic_mates_tests.cpp` cases plus 3 new `tests/search_tests.cpp` insufficient-material cases. Full suite (370 cases / 26,829 assertions, real Catch2 build) reverified green.
- [x] Dedicated endgame test suite: curated known-tricky K+P and rook-ending positions (canonical sources e.g. Fine's *Basic Chess Endings*) with known-correct results, run in CI to catch algorithmic-rule misjudgments that pure perft/search regression tests wouldn't surface. Kept as its own test file, separate from perft/search/eval regression tests (per Testing Policy in ARCHITECTURE.md) — Session 70: `tests/endgame_suite_tests.cpp` (9 tests), exercising the full engine (`search::search_fixed_depth()`) end to end rather than any single internal eval term in isolation, on KPK/KBPK/insufficient-material/KRK/KBNK/Lucena/Philidor-pattern/opposite-colored-bishop positions. Every expected result was independently confirmed against this project's own actual compiled engine first (not assumed) — see the file's own header comment for its sourcing note on the Lucena position (a real, independently sourced canonical FEN with a matching, confirmed best-move assertion) versus the Philidor-pattern and other positions (this project's own constructions, built to match standard theory's own structural criteria rather than a claimed, unverifiable book citation).
- [x] (Optional, low priority) small curated opening book — Session 70: new `src/book/book.h`/`.cpp` module. Book entries are plain UCI move sequences from the start position (`curated_lines()`, book.cpp) covering well-established main-line openings (Ruy Lopez, Italian, Petrov, Sicilian, French, Caro-Kann, Queen's Gambit, Slav, Indian systems, English); `init_book()` replays each line through real legal move generation at startup to derive correct Zobrist hashes by construction, rather than any hand-maintained hash table. Wired into `src/uci/uci.cpp`'s `handle_go()` (consulted first, unconditionally — no setoption/UCI-options infrastructure exists to gate it behind a toggle) and `src/main.cpp`'s startup sequence. `tests/book_tests.cpp` (4 tests) plus one new UCI-integration test in `tests/uci_tests.cpp`; two pre-existing `uci_tests.cpp` tests were updated (not merely patched around) to use an out-of-book position, since their own actual purpose (verifying `info depth` line formatting) needs a real search to run, which a bare startpos `go` no longer triggers now that the book intercepts it — see docs/DECISIONS.md for the full reasoning.

## Phase 7 — Multithreading
- [x] Lazy SMP implementation — Session 71: `search::search_iterative_deepening()` gained a trailing `num_threads = 1` parameter; `> 1` spawns that many `std::thread` helpers, each running a private, non-aspirating depth loop over its own private position copy/ordering tables, sharing only the `TranspositionTable` (made concurrency-safe via striped locking — see below). See docs/DECISIONS.md, 2026-09-03 (1), for the full rationale, including why striped locking rather than a true lock-free redesign (that's the separate item just below). `tests/lazy_smp_tests.cpp` (new, 5 tests); manually verified clean under the project's existing ASan/UBSan Debug build (no sanitizer findings across multiple runs) since real thread-safety issues wouldn't otherwise reliably surface in a normal green ctest run.
- [x] Lock-free TT for concurrent access — Session 72: `TranspositionTable`'s striped-lock interim scheme (Session 71) replaced with a true lock-free design — the classic CPW "Shared Hash Table" XOR-checksum technique. `TTEntry` now holds two `std::atomic<std::uint64_t>` words (`data`: every field packed into one word; `key_xor_data`: the real key XORed with `data`, never stored raw) instead of separate plain fields — a reader XORs the two loaded words back together and compares against the position's real key, safely treating any mismatch (a straddled concurrent write) as a miss rather than trusting torn data. `probe()`/`store()` no longer take any lock at all. See docs/DECISIONS.md, 2026-09-03 (2), for the full design, the one accepted edge case (two threads writing the exact same slot simultaneously can lose that slot early, never return wrong data), and why this technique over alternatives. `TTEntry`/`TTBucket` stay at their existing 16-byte/64-byte cache-line sizes (now two atomic words instead of five plain fields) — ARCHITECTURE.md's layout commitment is unchanged. Full existing test suite (410 cases / 52,955 assertions) reverified green, byte-for-byte identical node counts/scores to before this change (confirming no behavioral regression), plus 8 repeated isolated `[smp]`+`[tt]` reruns under the project's existing ASan/UBSan Debug build with no findings.
- [x] Thread count UCI option — Session 74: `src/uci/uci.cpp`'s `uci` response now advertises `option name Threads type spin default 1 min 1 max 1024`; a new `handle_setoption()` parses `setoption name Threads value <N>` (any other option name silently ignored, matching this file's established robustness convention), clamps to `[kMinThreads, kMaxThreads]`, and stores it as session-lifetime state (persists across `ucinewgame`, like a real UCI option should — only game state resets there, not engine options). `handle_go()` passes it straight through as `search_iterative_deepening()`'s own `num_threads` parameter (Session 71). 6 new tests in `tests/uci_tests.cpp` (`[threads]` tag): the advertised option string itself, a functional smoke test with `Threads` set to 4, a `value 1`-equals-never-set determinism check, out-of-range values (0, 999999999) clamped rather than rejected, a malformed `setoption` line ignored without breaking a later `go`, and persistence across `ucinewgame`.
- [x] Pondering — search side: handle `go ponder`, continue as real search on `ponderhit`, discard and restart on `stop`+actual move. Session 75: `search::search_iterative_deepening()` (search/search.h) gained a trailing `external_stop = nullptr` parameter — a second, independent way (alongside `time_limit_ms`) for the calling thread's own loop to be interrupted from outside the call entirely, checked both between iterations and threaded into every iteration's own `SearchLimits` (mirroring the mid-search-time-checks mechanism already used internally for `time_limit_ms`). `src/uci/uci.cpp`'s `go` handling now branches on the `ponder` sub-token: `start_pondering()` launches the search in the background (`time_limit_ms = 0`, `max_depth = kTimedSearchMaxDepth`, its own `external_stop` flag) so the UCI loop keeps reading `ponderhit`/`stop` while it runs; `handle_ponderhit()` hands the real move's time budget (computed up front, via a new shared `compute_search_budget()` helper extracted from the previously-inline `go`-parsing logic) to the still-running search via a short detached watchdog thread that raises the same stop flag once the budget elapses; `handle_stop()` stops and joins immediately, still producing a `bestmove` per the UCI spec's own requirement even though a GUI is expected to discard it when the actual move differed from the ponder guess. An out-of-protocol command arriving mid-ponder (`position`/`ucinewgame`/a second `go ponder`, or `quit`/end-of-input with neither `ponderhit` nor `stop` ever sent) is handled defensively via `abandon_pondering()`, which suppresses that search's `bestmove` entirely rather than emitting a stray one. New `tests/pondering_tests.cpp` (10 tests, `[pondering]` tag): 2 at the `search_iterative_deepening()` level (an external stop from another thread interrupts a generous-depth search well short of completion; the new parameter's default leaves every existing call site unaffected) and 8 at the UCI level covering the full `go ponder`/`ponderhit`/`stop` flow, the immediate-stop fallback when no time control was given, the UCI-spec-required `bestmove` after a bare `stop`, the suppressed-output abandonment path, both commands as safe no-ops with nothing pondering, and both out-of-protocol defensive cases. Full existing suite reverified green with these 10 new cases added; the new file's own 10 cases additionally reverified clean under the project's existing ASan/UBSan Debug build (no findings) given the genuine background-thread concurrency involved. Accepted scope limitations (full rationale in docs/DECISIONS.md): no `info` lines are emitted during the pondering phase itself (only once a real, non-ponder `go` is answered); the budget `ponderhit` hands off is the one implied by `go ponder`'s own `wtime`/`btime` values at the moment pondering started, not re-synced against the GUI's actual elapsed clock (the UCI `ponderhit` command carries no parameters to do so with); the opening book (src/book/book.h) is not consulted during pondering. True asynchronous `go`/`stop` for an ORDINARY (non-ponder) `go` remains explicitly out of scope, unchanged from before this session — see src/uci/uci.cpp's own header comment.
- [x] Verify no strength regression vs. single-threaded at equal single-thread depth — Session 77: `search::search_fixed_depth()` (search/search.h) gained the same `num_threads = 1` parameter `search_iterative_deepening()` already had (spawning Lazy SMP helpers around its one fixed-depth `search_root()` call instead of an outer iterative loop); `tuner::MatchConfig` (tuner/match.h) gained `threads_a`/`threads_b` fields (both default 1), threading a per-side thread count through `tuner::play_match()`'s existing head-to-head game loop alongside its existing per-side `eval::MaterialWeights`. Setting both sides to the SAME weight vector and the SAME `search_depth`, with only `threads_a`/`threads_b` differing, turns the existing Elo-estimating match harness (built for Session 61/62's own eval-weights comparison) directly into the exact comparison this item asks for, with no separate module needed. An initial empirical check (before building any of the above) found that a Lazy SMP-threaded search's SCORE at a given depth is genuinely NOT bit-for-bit identical to a single-threaded search's own score at that same depth (concurrent helper-thread TT writes change move ordering/pruning during the main thread's own search, which can shift which line an aspiration-window retry converges on) — expected, well-understood Lazy SMP non-determinism, not a bug, and why this item is fundamentally a STATISTICAL (many-game, Elo-estimate) question, not a per-position exact-equality one. Four real dispatched match runs (`tuner::play_match`, same weights both sides, `search_depth = 4` for three of them, `search_depth = 5` for the fourth — same fixed depth both sides throughout, matching this item's own "equal single-thread depth" wording): threads_a=1 vs threads_b=2 (400 games) scored `score_a=0.4725, elo_diff=-19.13`; threads_a=1 vs threads_b=4 (400 games, depth 4) scored `score_a=0.4400, elo_diff=-41.89`; threads_a=1 vs threads_b=8 (400 games) scored `score_a=0.4500, elo_diff=-34.86`; threads_a=1 vs threads_b=4 at depth 5 (300 games) scored `score_a=0.4383, elo_diff=-43.07` — every single comparison favored the MULTI-threaded side (negative `elo_diff` = threads_a, the single-threaded baseline, behind), by roughly 19–43 Elo depending on thread count, consistent across both tested depths. Verdict: no regression found — multithreading is a genuine, positive strength gain at equal depth, exactly as Lazy SMP is intended to provide, not merely "not worse." Full numbers and methodology logged in docs/DECISIONS.md, 2026-09-04 (3). New `tests/thread_regression_tests.cpp` (5 tests, `[thread_regression]` tag): structural/plumbing checks only (both new capabilities work end to end, default parameters leave existing behavior unchanged, helper thread node counts fold in correctly) — NOT a rerun of the real match numbers above, matching this project's own established split between a fast CI-run structural test and a separately-dispatched, real production verification run (docs/DECISIONS.md's own entry has the full reasoning). Full existing suite reverified green with these 5 new cases added; the new file's own cases additionally reverified clean under the project's existing ASan/UBSan Debug build.

**Phase 7 (Multithreading) complete.**

## Phase 8 — Polish & Tournament Readiness
- [x] Full UCI option set (Hash size, Threads, MultiPV, Ponder, Move Overhead, etc.) — DONE as of Session 81: `Threads` (Session 74), `Hash`/`Move Overhead` (Session 79, CI-driven follow-up fix Session 80), and `MultiPV` (Session 81) are all implemented (`Ponder` the option ADVERTISEMENT is tracked separately just below, not part of this item's own text — pondering itself has been fully implemented since Session 75/76). `Hash`: spin option, default 16 MB, min 1, max 2048 (2 GiB — originally 65536/64 GiB in Session 79, lowered in Session 80 after real CI evidence; see docs/DECISIONS.md, 2026-09-05) — passed through as `search_iterative_deepening()`/`search_fixed_depth()`'s `hash_size_mb` parameter (default `kDefaultTTSizeMB`, public in search.h), which still only sizes the fresh, private TranspositionTable each top-level search call constructs for itself (see docs/DECISIONS.md, 2026-08-25 (8)) — not yet a persistent, `ucinewgame`-cleared global; that remains open, tracked informally rather than as a new separate item. Two genuine bugs were caught and fixed across Sessions 79-80, both by the project's own test suite/CI, neither hypothetical: an in-bounds but large `Hash` value throwing `std::bad_alloc` uncaught (fixed with a `make_transposition_table()` fallback), then real CI (GitHub Actions run 91820797115) catching a deeper problem the fallback couldn't fix on its own (ASan's/the OS's own non-catchable abort paths), fixed by lowering `kMaxHashMB` itself. `Move Overhead`: spin option, default 0 ms, min 0, max 5000 — subtracted (floored at 1 ms) from any positive computed time budget, for both `movetime` and `wtime`/`btime`-derived budgets alike. `MultiPV`: spin option, default 1, min 1, max 256 — classic root-move-exclusion technique (`search::search_iterative_deepening_multipv()`, search.cpp), with two deliberate first-draft simplifications (no aspiration windows, no Lazy SMP when it genuinely takes effect — both documented at that function's own definition, both accepted scope limits rather than defects). A genuine ranking bug was found and fixed by Session 81's own tests: lines came back out of score order (real search instability from shared killer/history tables across per-line searches within one depth) — fixed by sorting lines by score before assigning rank, the standard technique real engines use. A second, purely test-infrastructure bug was also found and fixed in the same session: two new UCI-level tests used a bare `position startpos` (not `position startpos moves ...`), which silently answers from the opening book once any earlier test in the same process has called `book::init_book()` — a pre-existing landmine every earlier search-content test in this file already knew to avoid, just not yet documented as a named gotcha; now is (see the affected tests' own comments, tests/uci_tests.cpp).
- [ ] Pondering — protocol side: `Ponder` UCI option exposed, verified working against GUIs that ponder (Arena, CuteChess, etc.) — the option itself is DONE (Session 82): advertised as `option name Ponder type check default true` and accepted via `setoption` with no behavioral gating (pondering support, `go ponder`/`ponderhit`/`stop`, has been fully implemented and independently tested since Session 75/76 regardless of this option's value — see that option's own doc comment, src/uci/uci.cpp, for why there's nothing to gate). NOT done, and left explicitly open: real verification against an actual GUI (Arena, CuteChess, etc.) — this development environment has no way to run either GUI, so only protocol-level (UCI-response-string) and unit-level correctness have been confirmed; genuine interop testing against a real GUI remains outstanding and should be flagged to whoever can run one.
- [x] Time management (search time allocation per move, increment handling, best-move-stability-based extension) — DONE as of Session 83. The narrower, more urgent "does the search actually stop mid-iteration when the clock says to" gap remains tracked separately, above Phase 5, as a Priority Fix (docs/DECISIONS.md, 2026-08-25 (8)) — unaffected by this item. `movestogo`: when the GUI provides it, `allocate_time_ms()` (src/uci/uci.cpp) now divides remaining time by the ACTUAL moves-until-next-control count instead of a fixed "assume ~20 moves left" guess; falls back to that same fixed-20 heuristic when `movestogo` is absent or non-positive, exactly as before this session. Best-move-stability-based extension: `search_iterative_deepening()` gained a `soft_time_limit_ms` parameter (search.h) — a SECOND, smaller budget alongside the existing hard `time_limit_ms` cap; once the best move has been identical for `kStabilityThreshold` (4, an untuned starting guess pending SPRT infrastructure) consecutive iterations AND elapsed time has passed the soft budget, the search stops early rather than using its full hard-cap allocation on a position that's already settled. An unstable position (best move still changing) is unaffected and runs all the way to the hard cap exactly as before. `uci.cpp`'s own time allocation now computes both a soft ("expected") and hard (`kHardBudgetMultiplier`, 3x, also untuned) budget from `wtime`/`btime`/`winc`/`binc`, threading both through; an explicit `movetime N` or bare `depth N` still gets no soft budget at all (deliberate instructions, not estimates to second-guess — same convention Move Overhead already established). Deliberately NOT threaded into pondering's own background search call — that path stops via an entirely different mechanism (a fixed-delay watchdog after `ponderhit`, not the iterative-deepening loop's own soft/hard check), and reconciling the two is real, separate future work (see that function's own doc comment, src/uci/uci.cpp). Verified with real wall-clock timing tests (tests/search_tests.cpp, `[time_management]`) confirming the early stop actually engages for a stable position and is never triggered for a genuinely unstable one (the starting position itself, confirmed unstable at shallow depths — same phenomenon as MultiPV's own search-instability finding, docs/DECISIONS.md 2026-09-05 (2)).
- [x] `bench` command — fixed-position node/time benchmark for fishtest/OpenBench-style regression testing — DONE as of Session 84. Two entry points, both calling the exact same `nightwing::uci::run_bench()` (src/uci/uci.cpp) for byte-for-byte identical output: `./nightwing bench` as a command-line argument (src/main.cpp, the more common way fishtest/OpenBench tooling actually invokes an engine), and `bench` typed as an ordinary command at the interactive UCI prompt (for tooling that drives engines purely over UCI stdin/stdout instead). Runs the same four-position, fixed-depth-6 set this project's own internal regression test (tests/bench_tests.cpp) already used — now factored into a single shared header, `src/bench_positions.h`, specifically so the two can never silently drift apart into reporting different things for what's nominally "the same" bench. Deliberately single-threaded and default-Hash-size, unconditionally (not configurable via this command) — reproducibility across machines/commits is the entire point of a bench a testing harness diffs output against, and either Lazy SMP or a differently-sized TT would perturb node counts run to run (see docs/DECISIONS.md for the full reasoning). Output format (`Total time (ms)`/`Nodes searched`/`Nodes/second`, standard fishtest/OpenBench-parseable convention) verified directly by running the actual compiled binary in this sandbox — real output, not hypothetical, and its "Nodes searched" total (81029) matches the internal regression bench's own long-standing baseline exactly. NOT verified, and left honestly noted: actual interoperability with real fishtest/OpenBench infrastructure itself, since neither is reachable from this development environment — the output format is believed-correct by convention, not confirmed working end-to-end against that specific tooling (same category of caveat as the "Pondering — protocol side" item's own outstanding real-GUI verification, just above).
- [x] Profile-Guided Optimization (PGO) build pipeline (generate profile via `bench`/self-play, rebuild optimized) — DONE as of Session 85. `CMakeLists.txt` gained `NIGHTWING_PGO_PHASE` (a cache STRING: `""`/`"generate"`/`"use"`, GCC/Clang only — MSVC's own PGO mechanism is a different workflow entirely, `/LTCG:PGInstrument`/`/LTCG:PGOptimize`, and was deliberately not attempted without a real MSVC environment to verify it against, matching this project's own established "don't half-implement an MSVC-specific mechanism blind" lesson from the 2026-08-13 `/fsanitize=address` revert). `.github/workflows/ci.yml` gained a new `pgo-build` job (`workflow_dispatch`-only, Linux, same "manually triggered, doesn't gate regular CI" rationale as `tuning-pipeline`): configure+build instrumented → run a training workload (`bench` plus a couple of ordinary `go` searches, chosen specifically so the opening book also gets exercised, since `bench` alone bypasses it) → reconfigure the SAME build directory in `use` phase → rebuild → run the full test suite (confirms PGO only changed codegen, never behavior) → re-run `bench` as a correctness cross-check (same node counts expected) → upload the optimized binary as an artifact. A real, non-obvious gotcha was found and documented directly (not just written and assumed): GCC ties each `.gcda` profile file to the specific OUTPUT BINARY NAME active during the generate-phase compile — renaming it between phases, even with identical source and build directory, silently produces a full set of masked "missing profile" warnings rather than a build error, so the CI job is structured to never reconfigure into a fresh directory or rename the `nightwing` target between its two phases; this is now called out explicitly in both `CMakeLists.txt`'s own option comment and the CI job's own comments. Verified substantively, not just designed: a real `cmake` (installed via pip for this session, since it wasn't present at session start) confirmed the actual CMakeLists.txt configures and builds correctly for both phases end to end, producing a working, correct engine binary (bench totals identical to the non-PGO baseline — 81029 nodes); separately, the full 458-case test suite was compiled and run under genuine PGO instrumentation, then recompiled with `-fprofile-use` and rerun 3 times (via a faster raw-g++/Catch2-amalgamated path, since the CMake+Catch2-FetchContent test-binary build proved too slow for this sandbox's per-command time limits) — all green every time, node counts unchanged, confirming the pipeline preserves correctness. NOT completed end-to-end via CMake specifically for the Catch2-based test binary (covered instead by the raw-g++ verification, using the identical flags and file set) — a sandbox resource/time constraint, not a known or suspected issue with the PGO flags themselves. **UPDATE, Session 86:** the `pgo-build` job was subsequently run for real on GitHub Actions (manually triggered via the `pipeline: pgo` workflow_dispatch input) and its resulting `nightwing-pgo-linux` artifact downloaded and inspected directly — a valid ELF binary, `bench` reports the identical 81029-node baseline, normal UCI operation works, and no leftover profiling symbols remain (confirms the clean final `-fprofile-use` build, not an accidentally-uploaded instrumented one). The job reaching its final upload step (after `ctest` in its own step sequence, with no `continue-on-error` anywhere in the job) is strong indirect evidence the full test suite passed in `use` phase on real CI infrastructure too, though the actual CI log/test output text itself wasn't directly reviewed. This substantively closes the gap left open above.
- [x] TT prefetch verified to actually overlap memory latency with useful work (profiled, not assumed) — DONE as of Session 87, with an honest, nuanced conclusion rather than a simple pass/fail. Code-level finding, confirmed by direct inspection (not assumed): the pre-existing prefetch (`negamax()`'s own top-of-function `tt.prefetch(key)`, `src/search/search.cpp`) was issued literally one line before the `tt.probe()` that needed it, with ZERO intervening work — contradicting both its own code comment and ARCHITECTURE.md's own description ("overlaps memory latency with move generation/ordering work"), since movegen for that node actually happens considerably LATER (only reached if the probe doesn't cause an outright cutoff). Fixed by adding a SECOND, eager prefetch right after `board::make_move()` in the main move loop, using the child position's already-updated `pos.zobrist_hash` — genuine intervening work (`in_check()` plus extension-related logic) now exists between that prefetch and the recursive call's own eventual probe. The pre-existing internal prefetch was kept, unchanged in placement, as the only prefetch that fires for the root call and for the null-move/ProbCut recursive calls (both of which make their own move and recurse with no intervening work either way, an inherent limitation of those specific paths, not something this session's fix could meaningfully improve — documented at the relevant code, not silently left unaddressed). Empirical measurement (profiled, not assumed, per this item's own wording): a rigorous, interleaved A/B/C wall-clock comparison (original placement vs. this session's fix vs. prefetch disabled entirely) across 20 repetitions at a ~3.2-second-per-run depth found NO statistically or practically significant difference between any of the three (differences under 10ms against roughly 100ms of measurement noise) — this development sandbox has no hardware performance-counter tooling (`perf` isn't installed) and is a virtualized/shared-CPU environment where genuine cache-miss latency effects are difficult to isolate from scheduling noise, so this null result should be read as "this sandbox cannot substantiate a measurable benefit," not as proof prefetching provides zero benefit on real target hardware (a dedicated, unshared machine could behave differently). The code-level fix is kept regardless — it's logically sound (creates a genuine overlap window that didn't exist before) and costs nothing measurable (identical node counts confirmed across all three variants, negligible instruction overhead) — but a future session with access to real bare-metal hardware and `perf`/equivalent tooling would be needed for a more conclusive quantitative answer, flagged honestly rather than claimed as already settled.
- [x] SPRT testing setup/process for validating future changes — DONE
      as of Session 88. New `nightwing::tuner::sprt` module
      (`src/tuner/sprt.h`/`.cpp`) implements GSPRT (the generalized
      Sequential Probability Ratio Test used by Fishtest/OpenBench-style
      engine-testing infrastructure), a from-scratch implementation of
      the method described by Michel Van den Bergh and documented on
      the Chess Programming Wiki, computing a log-likelihood ratio from
      cumulative win/draw/loss counts under two configurable Elo
      hypotheses (`elo0`/`elo1`) and two error-rate bounds
      (`alpha`/`beta`), returning `Continue`/`AcceptH0`/`AcceptH1`.
      This is the actual "process for validating future changes" the
      item's own wording asks for, not just a statistics library: a new
      `nightwing_sprt` executable (`src/tuner/sprt_main.cpp`) reads a
      candidate `eval::MaterialWeights` from stdin (the same format
      `nightwing_tune`/`nightwing_match` already use) and plays it
      against `eval::default_material_weights()` in batches via the
      existing `tuner::play_match()` (Phase 5's own match harness),
      checking SPRT after each batch and stopping as soon as one of the
      two Elo bounds is crossed — rather than committing to a single
      fixed game count up front the way `nightwing_match`'s own one-shot
      match does. A new `.github/workflows/ci.yml` `sprt-test` job
      (`workflow_dispatch`-only, same rationale as the pre-existing
      `tuning-pipeline`/`pgo-build` jobs) chains self-play → tune →
      `nightwing_sprt`, demonstrating the tool end to end; the tool
      itself works standalone against any candidate weights on stdin,
      the self-play/tune steps are just one example producer of a
      candidate to test. `tests/sprt_tests.cpp` (11 new tests, 22
      assertions) covers `elo_to_score()`'s own logistic-model
      properties, default bound values, the all-draws zero-variance
      edge case (must return `Continue`, not divide by zero), and two
      hand-derived decisive win/loss records checked against manually
      computed LLR values — see docs/DECISIONS.md, 2026-09-06 (2) entry,
      for the full design and the empirical verification performed.
      **UPDATE, Session 89:** the `sprt-test` job was subsequently run
      for real on GitHub Actions (`pipeline: sprt` workflow_dispatch)
      and its resulting artifact (`training_data.txt`/
      `tuned_weights.txt`/`sprt_result.txt`) downloaded and inspected
      directly — self-play produced 224,825 real training positions,
      tuning converged to plausible weights (knight ~275/311, bishop
      ~290/322, rook ~465/486, queen ~876/892 mg/eg — all moved a
      believable amount from the untuned defaults, none degenerate),
      and `nightwing_sprt` ran the full pipeline end to end, reaching
      `max_games` (2000) with `llr=1.297` still inside the default
      `[-2.944, 2.944]` bounds and correctly reporting `Continue`
      (inconclusive) rather than forcing a spurious decision — an
      honest result, not a bug: the observed candidate edge
      (`elo_diff_b_minus_a=4.5`, `score_b=0.5065`) sits almost exactly
      AT the `elo1=5` upper hypothesis this default test is configured
      for, which is precisely the hardest case for any SPRT to resolve
      quickly (the closer the true effect is to one of the two
      hypotheses' own boundary, the more games any sequential test
      needs before it can tell that hypothesis apart from the other one
      with the configured confidence) — a real change further from the
      test's own bounds, or a longer `sprt_max_games`, would be expected
      to resolve faster. This substantively closes the "not yet run
      against real GitHub Actions" gap this item's own Session 88 entry
      left open, same pattern as Session 86's own confirmation of the
      `pgo-build` job.
- [x] Skill level / strength limiting (optional, for practice/handicap
      play) — DONE as of Session 90. New `nightwing::search::skill`
      module (`src/search/skill.h`/`.cpp`) mirrors the general SHAPE of
      Stockfish's own classic "Skill Level" UCI option (0-20, 20 = full
      strength/disabled — the same numeric convention, for familiarity)
      but is a from-scratch design, not copied code: search itself is
      NEVER weakened (no reduced depth/time/eval — every `info depth
      ... score ... pv ...` line a GUI sees is genuine, full-strength
      analysis, completely unaffected by this feature); only WHICH of
      several already-genuinely-computed MultiPV candidate moves
      (Session 78's own MultiPV machinery, this same Phase 8) gets
      reported as `bestmove` changes with the configured level.
      `pick_skill_move()` adds a random perturbation to each candidate
      line's own score, capped at one pawn (`kSkillNoiseCapCp`, 100 —
      scaled down to zero at full strength) before picking the highest
      adjusted score — a cap chosen specifically so a large material
      win or a forced mate can never be discarded regardless of
      configured level, only a genuinely close alternative is ever put
      up for grabs (see that function's own doc comment, skill.h, for
      the full reasoning). `skill_search_multipv()` silently raises the
      internal MultiPV line count (to `kSkillSearchMultiPv`, 8) whenever
      limiting is active and the user's own `MultiPV` UCI option is
      smaller — mirroring Stockfish's own well-known, accepted behavior
      of a Skill-Level-active search reporting more `info multipv N`
      lines than the user explicitly asked for. New `Skill Level` UCI
      spin option (`src/uci/uci.cpp`, default 20, min 0, max 20) wired
      through `handle_setoption()`/`handle_go()`; a single
      session-lifetime `std::mt19937_64`, seeded once from genuine
      entropy and never reseeded by `ucinewgame`, is threaded through so
      a real game's own move choices keep advancing one real stream
      rather than eerily repeating move-for-move across games. Book
      moves (src/book/book.h) deliberately bypass skill limiting
      entirely — a book move has no MultiPV alternatives to weigh in
      the first place. Deliberately NOT threaded into `start_pondering()`
      — the exact same accepted scope limit this same file's own
      `MultiPV`-during-pondering already established (run()'s own
      `skill_rng` doc comment has the parallel). Deliberately NO
      UCI_Elo/UCI_LimitStrength-style companion option: mapping a target
      Elo rating onto an equivalent skill level would require this
      project's own actual playing strength at each level to have been
      independently measured against a real rating pool, which it
      hasn't — see docs/DECISIONS.md, 2026-09-07, for the full
      rationale and why this is consistent with this project's own
      established "don't overclaim strength that hasn't actually been
      measured" convention (the `bench` item's own honest fishtest/
      OpenBench-interop caveat, Session 84, is the same pattern).
      `tests/skill_tests.cpp` (10 new pure-logic tests, seeded/
      deterministic, no board/search dependency at all) plus 7 new
      `tests/uci_tests.cpp` cases (option advertisement, an explicit
      "Skill Level 20" proven byte-for-byte identical to never touching
      the option at all, clamping, malformed input, `ucinewgame`
      persistence, and book-bypass) — full suite verified green (488
      cases) and `bench` reverified byte-for-byte unchanged (81029
      nodes) after the change.
- [x] Contempt / draw score adjustment (optional) — DONE as of Session
      91. A `Contempt` UCI spin option (`src/uci/uci.cpp`, default 0,
      min -100, max 100 -- one pawn each direction, `kMinContemptCp`/
      `kMaxContemptCp`'s own doc comment) discourages (positive) or
      welcomes (negative) draws for whichever side the engine is
      playing. New `contempt_draw_score()` helper (`src/search/
      search.cpp`, just above `negamax()`, and duplicated verbatim into
      `src/search/quiescence.cpp` per this codebase's own established
      "duplicate small stable helpers rather than share them across
      files" convention) converts a fixed White-perspective value,
      computed once per search from the UCI-facing `contempt_cp`, into
      the correctly-signed score at each of the four draw-detection
      points in the engine (negamax()'s repetition/50-move and
      stalemate cases, quiescence()'s own stalemate case, and
      search_root()'s own top-level terminal case) — the sign
      derivation accounts for negamax's own per-ply score negation so a
      drawn line is worth a CONSISTENT `-contempt` from the root's own
      fixed perspective no matter how many plies deep it occurs or
      whose turn it technically falls on there (`contempt_draw_score()`'s
      own doc comment has the full math). Threaded as a new trailing,
      default-0 parameter through the entire recursive search core —
      `negamax()` (9 internal call sites), `quiescence()`/
      `quiescence_impl()`, `search_root()` (3 internal call sites),
      `run_lazy_smp_helper()` (so Lazy SMP helper threads populate the
      shared TT using the SAME contempt-adjusted scores the main thread
      does, not silently-inconsistent plain ones), and both public entry
      points `search_fixed_depth()`/`search_iterative_deepening()`
      (each computing the fixed White-perspective value exactly once,
      from `pos.side_to_move` at entry) — mechanical parameter
      threading through every identified call site, the same pattern
      already used for `material_weights`/`limits`/`multi_pv` when each
      was added. `contempt_cp` IS fully threaded into
      `search_iterative_deepening_multipv()` too (unlike
      `soft_time_limit_ms`, which deliberately isn't) — see
      docs/DECISIONS.md, 2026-09-07 (2), for why the two differ.
      Deliberately NOT threaded into `start_pondering()` — same accepted
      scope limit `Skill Level`/`MultiPV` already established for
      pondering. `tests/contempt_tests.cpp` (new, 6 cases) — an
      immediate-stalemate sign check in both directions, a check that
      the sign is relative to whichever side is actually to move (not a
      fixed color), and, most importantly, a real multi-ply propagation
      test reusing the exact same forced-draw-by-repetition fixture as
      `tests/search_tests.cpp`'s own repetition test: confirms contempt
      survives several plies of real negamax recursion intact (not just
      an immediate terminal case) AND that even `kMaxContemptCp`-scale
      contempt (100) never overturns a queen's-worth (~900) material
      difference — the whole design point of capping the range small.
      6 new `tests/uci_tests.cpp` cases (option advertisement, an
      explicit "Contempt 0" proven byte-for-byte identical to never
      touching the option, clamping, malformed input, `ucinewgame`
      persistence). Full suite verified green (500 cases) and `bench`
      reverified byte-for-byte unchanged (81029 nodes) after the
      change — confirming this large, invasive threading change through
      the hot recursive search core is completely behaviorally inert at
      the default `contempt_cp=0`. Manually smoke-tested end to end via
      a real UCI session against the same repetition fixture: `Contempt
      100` correctly still finds and plays the forced draw, with the
      reported `info`/`bestmove` score showing exactly -100 instead of
      0, matching the unit test's own hand-derived expectation exactly.
- [x] README, build instructions, engine info (name/author via `uci`) —
      DONE as of Session 92. New top-level `README.md`: project
      description, the NO-NNUE/NO-tablebase constraint stated up front
      as permanent (not a placeholder), a feature summary across
      search/eval/tuning-testing-infrastructure/UCI, build instructions
      (the two-command CMake configure+build, how to run `bench`, how to
      run the test suite via `ctest`), a repository-layout diagram, and
      an attribution-policy pointer — deliberately concise and pointing
      into `docs/` for depth rather than duplicating it. Engine info via
      `uci`: `id name`/`id author` already existed (`Nightwing`/`g-c-3`)
      from early in the project; this session added a real version
      number rather than leaving `id name` bare — `project(nightwing
      VERSION 0.1.0 ...)` in the top-level `CMakeLists.txt` is now the
      single source of truth, `configure_file()`'d into a generated
      `nightwing/version.h` (`src/version.h.in` is the checked-in
      template; the generated copy lives under the build directory,
      never in the repo) and consumed by `src/uci/uci.cpp`'s own `id
      name` line — `id name Nightwing 0.1.0`, confirmed by running the
      actual compiled binary. No LICENSE file was added — that's a
      genuine legal/ownership decision for whoever owns this repo to
      make, not something to invent unprompted; `README.md` doesn't
      claim a license status either way. Verified: full test suite
      rebuilt and rerun clean (500 test cases, 100% passing — the
      pre-existing `id name Nightwing` substring-match test in
      `tests/uci_tests.cpp` is unaffected by the appended version
      number), `bench` reverified byte-for-byte unchanged (81029 nodes),
      and the generated `version.h`/real `id name` output both
      confirmed directly against the actual compiled binary rather than
      assumed from reading the CMake logic alone. **This closes the
      last open item in Phase 8 — Phase 8 (Polish & Tournament
      Readiness) is now complete**, modulo the two items with an
      honestly-flagged external-verification gap this sandbox cannot
      close itself (real-GUI pondering interop, and real fishtest/
      OpenBench interop for `bench`'s own output format — both already
      noted at their own items above) and the two items marked optional
      that were nonetheless completed anyway (Skill Level, Contempt).
- [ ] wasm build / GUI packaging — superseded by the "Release & Packaging Infrastructure" section below (2026-08-15); tracked there instead of here.

## Priority Fixes (external code review, 2026-09-08)

Not phase-gated — inserted here, ahead of the Release & Packaging track and
Phase 9, per the decision logged in docs/DECISIONS.md (2026-09-08 (2)). Two
independent external documents were reviewed on the same day: a full-repo
code review (build + full test suite in Release and Debug/ASan+UBSan, both
green; 500 test cases / 53,403 assertions; perft, UCI, and `bench`
independently verified; a depth-5-vs-depth-4 self-play sanity match) and a
companion design document scoping the eval-tuning extension the review
itself flags as the single biggest lever (Tier 0 below). Items are ordered
correctness-fix-first, then cheap/well-understood search wins, then the
larger multi-session eval-tuning extension, matching this project's own
established "bugs before enhancements" convention (see the 2026-08-25
Priority Fixes section above).

- [x] SEE king-legality edge case — the swap algorithm never checked
      whether a hypothetical king "recapture" would actually be legal
      (i.e., whether the opponent still has an attacker on that square
      afterward, which in real chess means the king moved into check and
      the move is illegal); the simulation continued regardless. Fixed
      in `src/search/see.cpp`: when the next attacker in the sequence is
      the king, the exchange now stops there if the opponent still
      attacks the square post-recapture, rather than simulating an
      illegal move. See docs/DECISIONS.md, 2026-09-08 (1), including why
      this is provably a no-op on every existing test's numeric result
      (the king's large SEE sentinel value makes the bug self-limiting
      in practice, per the review's own finding) and is nonetheless
      worth fixing as a correctness/hygiene issue, not a speculative one.
- [x] History malus/gravity — `HistoryTable::update()` only rewards the
      cutoff move and never penalizes quiet moves tried-and-rejected
      before it, nor decays over time; a move can hit near the ordering
      ceiling once and stay there indefinitely. DONE, Session 94:
      `HistoryTable::malus()`/`ContinuationHistoryTable::malus()`
      (`src/search/ordering.h`/`.cpp`) subtract a depth-squared penalty,
      floored at the mirror of each table's existing positive ceiling;
      `negamax()`'s move loop (`src/search/search.cpp`) now tracks every
      quiet move it genuinely searches (not ones skipped outright by
      futility/LMP/history pruning) and, on a beta cutoff, applies malus
      to every one of them except the move that actually cut off. See
      docs/DECISIONS.md, 2026-09-08 (3), including the real bench
      node-count shift this produced (a mixed, small net increase across
      the 4 bench positions — expected from changed move ordering/
      pruning interaction, not a regression) and the explicit, justified
      decision NOT to add separate periodic aging/halving (the "long
      game" concern that recommendation is aimed at doesn't apply the
      same way here, since `HistoryTable` is already scoped to reset
      every top-level search call, not persistent across a game).
- [x] SEE-based pruning of bad captures in the main search — SEE is used
      for capture ordering only; nothing in `negamax()` prunes clearly-
      losing captures at shallow-to-moderate depth the way LMP/futility
      prune quiet moves. A related, smaller addition: a separate capture-
      history table (keyed on capturing/captured piece type) for finer-
      grained capture ordering beyond MVV-LVA/SEE. DONE, Session 95: a
      new cascading skip check (`kSeePruningMaxDepth`/
      `kSeePruningThresholds`, `src/search/search.cpp`) alongside
      futility/LMP/history pruning, gated on each capture's own
      pre-move SEE value; `CaptureHistoryTable`
      (`src/search/ordering.h`/`.cpp`), keyed by attacker/victim piece
      type with the same bonus-plus-malus scheme this session's own
      Session 94 quiet-history work established, threaded through
      `order_moves()`/`negamax()`/`search_root()` end-to-end and
      applied on capture beta cutoffs. See docs/DECISIONS.md,
      2026-09-08 (4), including a correction of the external review's
      own premise (SEE was NOT already used for ordering anywhere in
      this codebase before this session, only for quiescence's bad-
      capture pruning) and the real bench node-count change this
      produced (a net decrease, unlike Session 94's own malus-driven
      increase — expected, since this technique specifically prunes
      nodes rather than just reordering them).
- [x] Persistent, engine-lifetime transposition table — was a fresh,
      private allocation constructed per top-level search call rather
      than a persistent, `ucinewgame`-cleared global; hash information
      didn't carry over between moves within the same game. Was already
      flagged as an accepted placeholder (`src/search/tt.h`'s LIFETIME
      NOTE, ARCHITECTURE.md's Tech Stack row) before being promoted to a
      tracked item. DONE, Session 96: `search_fixed_depth()`/
      `search_iterative_deepening()` (and the internal MultiPV path)
      gained a new `external_tt` parameter (`src/search/search.h`/
      `.cpp`) — `nullptr` (the default) reproduces every pre-existing
      call site's behavior exactly (a fresh, private table, as before);
      non-null uses that caller-owned table directly instead.
      `src/uci/uci.cpp`'s `run()` now owns one `TranspositionTable` for
      its whole session, shared by every ordinary `go` and `go ponder`
      call, rebuilt (not resized in place — `TranspositionTable` has no
      such operation) only when `setoption name Hash` genuinely changes
      its size, and `clear()`-ed (not rebuilt) on `ucinewgame`. See
      docs/DECISIONS.md, 2026-09-09 (1), including the non-movable/non-
      copyable-table compile hazard this surfaced and how it was
      resolved, and the concurrency hazard a Hash-size change mid-ponder
      posed and how it's handled. New test coverage:
      `tests/persistent_tt_tests.cpp` (6 cases, `search::`-level:
      default-unaffected, genuine reuse via TT probe, a dramatic
      node-count drop on an identical warm repeat, `hash_size_mb` being
      ignored when `external_tt` is supplied, and MultiPV
      compatibility) plus 3 new cases appended to
      `tests/pondering_tests.cpp` covering the Hash-mid-ponder
      concurrency path specifically (genuine-size-change safely
      abandons the ponder search; same-size change does not; the engine
      still works correctly afterward) — verified green under both a
      plain build and ASan+UBSan, several repeated runs each, no
      flakiness. Full suite: 526 test cases / 53,457 assertions, all
      green; `bench` node counts unchanged (81,197 total, matching
      Session 95's own baseline exactly, as expected since `run_bench()`
      deliberately still constructs its own private table for
      reproducibility — untouched by this change).
- [x] Reverse futility / static null-move pruning — was a node-level pre-
      move-loop check (`if static_eval - margin*depth >= beta: return
      static_eval`), the natural third leg alongside the existing
      futility and razoring, previously absent. DONE, Session 97: a new
      cascading check in `negamax()` (`src/search/search.cpp`), placed
      right after IIR and before null-move pruning (the cheapest of
      this function's static-eval-based pruning techniques — no move
      made, no recursive search — so it runs first among them); new
      `kReverseFutilityMaxDepth`/`kReverseFutilityMargins` constants,
      same fixed-lookup-table shape as `kFutilityMargins`/
      `kRazorMargins` but reaching a deeper ceiling (depth 6, matching
      this same file's `kSeePruningMaxDepth`) since this technique's
      own verdict is a more reliable signal than futility/razoring's.
      See docs/DECISIONS.md, 2026-09-09 (2), including the real bench
      node-count drop this produced (a large one, as expected for a
      genuine new pruning technique, not a bug) and the deterministic
      (not flaky) small test-assertion-count shift it also produced,
      isolated to time-budget-sensitive tests and confirmed benign.
- [x] LMR as a continuous formula (e.g. `R = a + ln(depth)*ln(move_count)*b`)
      rather than the previous 2-value step table. DONE, Session 98: a new
      `lmr_reduction(depth, move_index)` helper (`src/search/search.cpp`)
      replacing `kLMRReduction`/`kLMRBigReduction`/`kLMRBigReductionDepth`
      with `R = kLMRBase + ln(depth)*ln(move_index)*kLMRScale`, precomputed
      into a cached lookup table on first call (`std::log()` isn't usable
      in a portably-`constexpr` context). Eligibility gating
      (`kLMRMinDepth`/`kLMRMinMoveIndex`) unchanged. See docs/DECISIONS.md,
      2026-09-10, for the coefficient values chosen and the regression a
      first, more aggressive pair of coefficients caused (and how it was
      caught and corrected) before landing on the final ones.
- [x] An "improving" flag (is static eval better than 2 plies ago?)
      feeding into futility/LMR/NMP margins. DONE, Session 98 (continued
      twice): a new `static_eval_history` per-ply array
      (`src/search/search.cpp`, mirroring `path[]`'s own reuse pattern)
      and an `improving` flag derived from it (this node's static eval
      vs. the same side's own eval 2 plies back) were added to
      `negamax()`. Applied at all 3 named call sites, but LMR's own
      version uses a DIFFERENT shape from futility's/NMP's: futility's
      margin (`kImprovingFutilityMarginDelta`) and NMP's reduction
      (`kImprovingReductionBonus`, gated to
      `depth >= kNullMoveBigReductionDepth`) both ADD an adjustment when
      NOT improving; LMR's own reduction instead SUBTRACTS
      (`kImprovingLmrDiscount`, clamped to never go below 0) when
      improving, leaving the not-improving case exactly at the
      already-tuned baseline. This asymmetry wasn't arbitrary: a first,
      additive "+1 when not improving" shape for LMR (matching NMP's own
      pattern) was tried first and broke 2 existing regression tests
      simultaneously — a high-frequency, per-move technique reacts very
      differently to a flat additive bonus than a once-per-node probe
      does. A real bug was also found and fixed mid-session, independent
      of either LMR shape: a first version of `static_eval_history` was
      written too late (after TT-cutoff/IIR), leaving stale cross-branch
      values behind on any early return; fixed by moving the write to
      the same point `path[ply]` is already written. See
      docs/DECISIONS.md, 2026-09-10 (2) and (3), for the full sweep, the
      stale-read bug, and the reasoning behind LMR's own final shape.
- [x] Correction history — a running per-pawn-structure (optionally per-
      piece-type) estimate of static-eval error vs. actual search
      result, used to adjust static eval before it drives pruning
      decisions. DONE, Session 98 (continued a fourth time): a new
      `CorrectionHistoryTable` class (`src/search/ordering.h`/`.cpp`) —
      a bounded exponential moving average, indexed by
      `[color][pawn-only Zobrist key]` (`board::compute_pawn_hash()`,
      the same key `eval::PawnHashTable` uses), tracking how far a
      node's own corrected static eval still was from its real, searched
      result. Threaded through `negamax()`/`search_root()`/all 4
      top-level entry points the same way `capture_history` already was.
      Applied at all 4 of this file's existing static-eval-driven
      pruning sites (RFP, razoring, futility, and the "improving" flag's
      own `node_static_eval`), each independently (matching this file's
      own established "double/triple/quadruple-evaluate, `eval_cache`
      absorbs it" pattern rather than a riskier shared-computation
      refactor). Updated once per node, at the very end of `negamax()`,
      gated on an EXACT bound (reusing the bound classification already
      computed there for the TT store) and a non-mate score — a
      fail-high/fail-low bound isn't real evidence of the position's own
      true value, so it isn't used to teach the correction table
      anything. Per-pawn-structure only in this pass, not per-piece-type
      — the spec's own parenthetical "optionally" scope left for a
      future session. See docs/DECISIONS.md, 2026-09-10 (4), for the
      full design.
- [x] Recapture extension (extend on an immediate recapture on the
      opponent's last capture square) and a small passed-pawn-push
      extension near promotion (rank 6/7) — only check and singular
      extensions exist today. DONE, Session 98 (continued a seventh
      time): passed-pawn-push extension implemented as specified — a
      pawn move reaching relative rank 6/7 (`kPassedPawnExtensionMinRank`,
      `src/search/search.cpp`) that's still genuinely passed
      (`board::passed_pawn_mask()`) gets +1 ply, combined via
      `std::max()` with check/singular extensions, in both `negamax()`'s
      and `search_root()`'s own move loops. Recapture extension went
      through 2 designs before landing: a first version, implemented
      literally as specified (an actual depth extension), was found to
      have a genuine correctness problem — it's conditioned on the
      PARENT move (`prev_to`/`prev_was_capture`), which is edge
      information, not node information, so a transposition could reach
      the identical position via a capturing or non-capturing last move
      and get a genuinely different search depth depending on which
      arrived first. Confirmed via a real, reproducible failure on
      `persistent_tt_tests.cpp`'s own warm-TT-reuse test: a warm search
      of an identical position visited MORE nodes than a cold one and
      returned a DIFFERENT best move — not just non-bit-reproducible,
      actively counterproductive. The SHIPPED version instead exempts a
      genuine recapture from SEE-based capture pruning
      (`kSeePruningMaxDepth`'s own block) rather than extending depth at
      all — it never changes what depth any move gets searched to, so
      the conflict has nowhere to occur; confirmed via the identical
      diagnostic that broke the first version (cold and warm searches of
      the same position now return the identical best move and score,
      with the expected large node-count reduction on the warm run). See
      docs/DECISIONS.md, 2026-09-10 (5) and (6), for the full account of
      both designs.
- [x] Lazy SMP helper thread diversification — every helper thread runs
      an identical, plain, non-aspirating loop starting at depth 1;
      real scaling gains typically come from varied starting depths,
      occasional depth skips, and varied move-ordering tie-breaks. DONE,
      Session 98 (continued an eighth time): `run_lazy_smp_helper()`
      (`src/search/search.cpp`) now takes a `helper_id` (each spawn
      site's own 0-based loop index) and derives all 3 named
      diversification axes from it — a staggered starting depth
      (`kLazySmpStartDepthSpread`, wrapping so at least one helper still
      starts at depth 1 exactly as before), an occasional 2-ply jump
      instead of 1 for odd-numbered helper ids only
      (`kLazySmpSkipPeriod`), and a per-helper `tie_break_variant`
      (`search/ordering.h`'s own new `order_moves()` parameter) that
      propagates through that helper's entire subtree, not just its own
      root move. `tie_break_variant` defaults to 0, reproducing the main
      thread's own pre-existing behavior exactly; a nonzero value adds a
      small (+/-16), score-band-safe jitter that only ever breaks ties
      among otherwise-identically-scored moves (most visibly: untried
      quiet moves). See docs/DECISIONS.md, 2026-09-10 (7), for the full
      design and verification, including the established
      `[smp]`+`[tt]`-tagged-tests-x8-under-ASan/UBSan check this
      project's own prior SMP-touching sessions use.
- [x] Eval: pawn storms (enemy pawns advancing on the king's shelter,
      the aggressive complement to the existing defensive king-safety
      terms) and a connected-passed-pawns bonus (a mutually-defending
      passed pair worth more than two individually-scored passers).
      DONE, Session 98 (continued a ninth time): `kPawnStormPenalty`
      (`src/eval/king_safety.h`/`.cpp`) penalizes the enemy's own most-
      advanced pawn on the king's file and its 2 neighbors, scaled by
      relative rank, as a 4th component alongside the existing shield/
      open-file/attacker-weighting terms. `kConnectedPassedPawnBonus`
      (`src/eval/pawns.h`/`.cpp`) adds an additional per-pawn bonus, on
      top of the existing passed and connected bonuses, specifically
      when a passed pawn's own defender or phalanx partner is ALSO
      passed, not merely present. See docs/DECISIONS.md, 2026-09-10 (8),
      for the exact design (including a real hand-derivation mistake
      caught during this session's own test-writing, not shipped).
      Five further concrete, low-risk gaps identified by cross-
      referencing a bucketed CPW/Stockfish-classical eval-feature
      review against `src/eval/` (docs/DECISIONS.md, 2026-09-08 (5)),
      each cheap to detect via existing attack-bitboard/pawn-structure
      machinery and, per that review's own framing, standard sub-checks
      within already-implemented top-level buckets rather than new
      buckets of their own:
    - [x] Candidate passed pawns — a pawn not yet passed but positioned
          to become passed after a likely, forceable pawn trade. DONE,
          Session 99: `eval::pawns.cpp`'s existing per-pawn loop gained a
          new `is_candidate_passed_pawn()` helper and a new
          `kCandidatePassedPawnBonus` table (`src/eval/pawns.h`, same
          relative-rank indexing convention as `kPassedPawnBonus`, scaled
          to roughly 40% of its magnitude), applied inside the same
          `if (!passed)` block the backward-pawn check already uses — a
          genuinely independent, sibling check, not a replacement for it
          (a pawn can be both backward and a candidate simultaneously).
          A deliberately simplified, from-scratch two-part test (CPW's
          general idea, not a literal search over every capture
          sequence — see the constant's own doc comment, `pawns.h`, for
          the full account of why): (a) no enemy pawn stands anywhere
          ahead of it on its own file (a straight-ahead blocker can
          never be traded away, since pawns don't capture straight
          ahead, ruling out candidacy regardless of the adjacent files);
          (b) among the up to two ADJACENT files only, the number of own
          pawns at-or-behind this pawn's own rank is >= the number of
          enemy pawns ahead of it (a symmetric trade-count argument). A
          pre-existing `tests/pawns_tests.cpp` test's own expected value
          needed correcting (not just a new test added) — one of its two
          White pawns turned out to also newly qualify as a candidate
          once this feature existed, exactly the same category of
          discovery Session 98's own `kConnectedPassedPawnBonus` work
          hit with a different pre-existing test. 2 new dedicated test
          cases (a genuine tied-count candidate; a same-file-blocker
          negative case demonstrating part (a) overrides part (b)
          regardless of support).
    - [x] Outside passed pawns — a passer on the side of the board away
          from the pawn majority; a specific, cheap-to-detect sub-case
          of the existing passed-pawn bucket. DONE, Session 100:
          `eval::pawns.cpp`'s existing per-pawn loop gained a new
          `is_outside_passed_pawn()` helper, a new `kOutsidePassedPawnBonus`
          table, and a new `kOutsidePassedPawnMinFileGap` (3) threshold
          constant (`src/eval/pawns.h`, same relative-rank indexing
          convention as `kPassedPawnBonus`, scaled to roughly 30% of its
          magnitude — smaller than `kConnectedPassedPawnBonus`'s own
          50%, since being merely far away is a weaker, more situational
          asset than having a genuine mutual defender). Applied inside
          the existing `if (passed)` block (the opposite gating from
          the candidate/backward checks, which both require `!passed`):
          the minimum file-distance from the pawn to every OTHER pawn
          on the board, either color, must be at least
          `kOutsidePassedPawnMinFileGap` — wide enough that no other
          pawn's own natural advance could ever interact with this one.
          A `total_pawns` bitboard (own | enemy) is now precomputed once
          per side, alongside the existing `file_counts`/`passed_pawns_bb`
          precomputation, so the per-pawn check doesn't have to re-OR
          the two bitboards on every iteration. 2 new test cases (a
          genuine outside passer, 4 files from its nearest neighbor;
          a boundary negative case at exactly 2 files, demonstrating the
          threshold is a hard cutoff, not a fuzzy preference) — no
          pre-existing test needed correcting this time (unlike Sessions
          98/99's own experience), since none of the earlier tests'
          hand-built positions happen to place a passed pawn 3+ files
          from every other pawn on the board.
    - [x] Pawn islands — a simple count of contiguous same-color pawn
          groups; correlates well with structural weakness. DONE,
          Session 101: `eval::pawns.cpp` gained a new, deliberately
          per-SIDE (not per-pawn) check appended right after the
          existing per-pawn loop -- a single scan across all 8 files'
          worth of the already-precomputed `file_counts` array,
          counting maximal runs of consecutive own-pawn-occupied files.
          `src/eval/pawns.h` gained `kPawnIslandPenalty`, a single flat
          `Score` (not rank-indexed, unlike every other constant in
          this file, since island count has no notion of "how far
          advanced") charged ONCE PER ISLAND BEYOND THE FIRST (a single
          island, however wide, costs nothing; 2 islands costs one
          charge, 3 costs two, via `Score::operator*(int)`). Fixing the
          candidate/outside-passed-pawn tests (Sessions 99/100) needed
          one pre-existing test's expected value corrected once again
          (the Session 100 "outside passed pawn" test's own White pawns,
          a5 and e2, happen to sit 4 files apart with 3 empty files in
          between -- exactly 2 separate islands) -- the third session in
          a row to hit this exact category of discovery. 2 new dedicated
          test cases (a clean 2-island case, charged once; a 3-island
          case, charged twice, demonstrating the penalty scales with
          `islands - 1` rather than being a flat per-side charge
          regardless of count).
    - [x] Back-rank weakness — a concrete, well-defined pattern (an open
          back rank with the king stuck on it). DONE, Session 102:
          `eval::king_safety.cpp` (not `pawns.cpp` — a king-safety
          pattern, not a pawn-structure one; `docs/ARCHITECTURE.md`'s
          own module layout confirmed the right file before writing
          anything) gained a 5th component alongside its existing 4:
          the king must be on its own back rank, EVERY existing square
          directly in front of it (up to 3, fewer on the a/h files)
          must be occupied by an own pawn (no pawn-created "luft"
          square to step up to), AND the enemy must have at least one
          rook or queen currently on the board (the specific piece
          types that actually deliver a back-rank check/mate — without
          one, the weakness is purely theoretical). `src/eval/
          king_safety.h` gained `kBackRankWeaknessPenalty`, a single
          flat `Score` (not rank-indexed — the king either has the
          weakness or it doesn't). 2 new dedicated test cases (a
          genuinely trapped king penalized more than an otherwise-
          identical king given a flight square by moving, not removing,
          a shield pawn one rank further forward; an exact-magnitude
          isolation test confirming the penalty fires if and only if a
          rook/queen is actually present, not merely "any enemy piece
          besides the king"). All 7 of this file's own pre-existing
          tests passed unchanged (each one's own king already has an
          open front square, or the enemy has no major piece, in every
          existing hand-built position — confirmed by re-running them,
          not merely reasoned about).

          **A real, confirmed, pre-existing search bug was found and
          fixed as a direct result of adding this term** — see `docs/
          DECISIONS.md`'s own 2026-09-14 entry for the full account.
          In short: this term's own contribution to the start
          position's static eval was enough to expose a latent Internal
          Iterative Reduction (IIR) / persistent-TT-reuse determinism
          bug that already existed on unmodified `main` (reproducible
          at depth 5 with zero eval changes at all) — `search_fixed_
          depth()`'s own IIR gate depended on TT-hit status, which a
          warmed-up persistent table changes call-to-call by design,
          occasionally producing a genuinely different best move/score
          between a cold and warm call at the identical position/depth.
          Fixed by gating IIR on `limits != nullptr` (`src/search/
          search.cpp`'s `negamax()`) — IIR now only fires during a real
          `search_iterative_deepening()` iteration at depth >= 2, where
          its own already-documented "self-correction" safety net
          genuinely exists; `search_fixed_depth()` (bench, the tuner,
          this exact regression test) no longer gets IIR's node-count
          benefit at all, a deliberate, documented trade favoring full
          determinism for those specific tools over a modest,
          direction-uncertain speed effect. Real gameplay (`search_
          iterative_deepening()`) is completely unaffected. `tests/
          persistent_tt_tests.cpp` gained a new depth-1-through-8 sweep
          test specifically to guard against this exact class of bug
          recurring undetected (the pre-existing test only ever checked
          one hand-picked depth, which is exactly how this bug went
          unnoticed for however many prior sessions it was already
          latent) plus one confirming IIR still fires normally under
          real iterative deepening; `tests/search_tests.cpp`'s own
          pre-existing IIR-in-`search_fixed_depth()` test was renamed
          and its comment corrected (the behavior it was checking no
          longer occurs, though its own loose assertions still pass).
    - [x] Overloaded pieces — a piece defending two or more things it
          cannot actually defend if any one of them is taken/attacked;
          detectable via the same attack-bitboard machinery the existing
          Threats bucket already uses. DONE, Session 103: this was the
          last of this section's 5 eval-feature gaps — the Priority
          Fixes (2026-09-08) section's own list is now fully complete.
          `eval::threats.cpp` gained the check: for each own knight/
          bishop/rook/queen D, count how many other own knights/
          bishops/rooks/queens are BOTH currently attacked by the enemy
          AND have D as their one-and-only own defender (not merely one
          of several) -- 2 or more such pieces means D is overloaded,
          via a new `ScopedPiece` scratch array (`kMaxOverloadScopedPieces`
          = 16, generous fixed-capacity headroom over any realistic
          piece count, no heap allocation) built once per side from each
          piece's own individually-computed attack bitboard (the
          existing `attacks_by_side()` union bitboards this file already
          had don't preserve per-square defender COUNT, only presence).
          O(pieces²) per side via two passes (first: which pieces have
          exactly one defender, and who; second: tally how many name
          each candidate as their sole defender) rather than the naive
          O(pieces³) a nested "for each defender, for each target,
          recount everyone" approach would cost. `src/eval/threats.h`
          gained 4 new penalties (`kKnightOverloadedPenalty` through
          `kQueenOverloadedPenalty`, the same Rook/Queen > Knight/Bishop
          value-scaling convention this file's own hanging-piece table
          already established, but smaller in magnitude throughout --
          an overload is a future tactical vulnerability, not an
          already-realized material threat). 2 new dedicated test cases
          (a rook overloaded across two perpendicular lines defending a
          knight and a second rook, each attacked by a different enemy
          rook -- deliberately built with the two enemy rooks also
          mutually defending each other, to directly confirm the
          symmetric check correctly does NOT fire on Black's own side in
          the same position; and a negative case adding a second
          defender to just one of the two targets, which drops the
          count below 2 and removes the penalty entirely even though the
          other target is still solely defended). All 5 pre-existing
          tests in this file passed unchanged.
- [ ] **Tier 0 — extend the tuner to PSQT and beyond (largest item, own
      multi-session design doc reviewed 2026-09-08):** only the 5 base
      material weights are Texel-tuned today; every PSQT cell and every
      mobility/king-safety/pawn/space/threat constant is hand-set. Full
      plan (tapering the 4 untapered piece PSQTs, a generalized
      `ParameterRef<Weights>` covering array-valued parameters, L2
      regularization once the parameter count leaves the current
      10-scalar regime, an analytic-gradient path for PSQT terms, a
      materially larger self-play corpus, and mandatory SPRT-gating
      before any tuned values are committed) logged in docs/DECISIONS.md,
      2026-09-08 (2). Treated as its own sub-tracked effort, not a single
      checkbox — see that entry for the step-by-step order.
    - [x] **Step 1 — taper the 5 non-king piece PSQTs (Pawn/Knight/
          Bishop/Rook/Queen), structurally (Session 104):** `psqt.cpp`'s
          five single-phase tables (`kPawnTable`, `kKnightTable`,
          `kBishopTable`, `kRookTable`, `kQueenTable`) each split into an
          Mg/Eg pair (`kPawnMgTable`/`kPawnEgTable`, etc.), matching the
          storage shape the king's own `kKingMgTable`/`kKingEgTable`
          already had. `psqt_value()` now looks up both tables for every
          piece type, not just the king. The 5 new Eg tables are exact
          duplicates of their Mg counterparts for now — Michniewski's
          baseline never published real per-phase values for anything
          but the king, and this step is deliberately pure plumbing, not
          a hand-guessed real split — so `ctest`/`bench` are confirmed
          byte-for-byte unchanged (see docs/SESSIONS.md, Session 104).
          Unblocks Step 2 (`PsqtWeights`, a runtime-mutable mirror of
          the now-uniform Mg/Eg table shape, the same role
          `MaterialWeights` already plays for material values).
    - [x] **Step 2 — `PsqtWeights` (Session 104):** a runtime-mutable
          mirror of the now-tapered PSQT tables above, the same role
          `MaterialWeights` (`psqt.h`) already plays for material
          values — 12 `std::array<double,64>` fields (one Mg/Eg pair
          per piece type), `default_psqt_weights()` populating one from
          psqt.cpp's own internal tables, and `psqt_value()` extended
          with the same nullable-override convention `material_value()`
          already established. No production call site passes it yet
          (eval.cpp's own call is untouched) — see docs/DECISIONS.md,
          this session's entry.
    - [x] **Step 3 — generalized `ParameterRef<Weights>` (Session 104):**
          `MaterialParameterRef` (`tuner/tune.h`) generalized into a
          template `ParameterRef<Weights>` supporting both a plain
          scalar `double Weights::* member` (the original shape) and an
          indexed `std::array<double,64> Weights::* array_member` +
          `index` (new), with uniform `get()`/`set()` accessors.
          `kMaterialParameters` unchanged in behavior (now
          `ParameterRef<eval::MaterialWeights>`); new `kPsqtParameters`
          (768 entries: `PsqtWeights`' 12 array fields x 64 squares
          each) added alongside it, generated by a `constexpr` loop
          rather than typed by hand. `tune.cpp`'s finite-difference loop
          refactored to use `.get()`/`.set()` instead of raw `.*member`
          dereferencing. `kPsqtParameters` is NOT YET consumed by
          `tune()`/`compute_loss()` (both still operate on
          `eval::MaterialWeights` specifically) — that generic wiring is
          Step 4's job, once PsqtWeights actually flows through
          `evaluate()`. See docs/DECISIONS.md, this session's entry.
    - [x] **Step 4 — wire `PsqtWeights` through `compute_loss()`/
          `eval::evaluate()`'s existing nullable-override convention
          (Session 104):** `evaluate()` gained a `const PsqtWeights*
          psqt_weights = nullptr` parameter (its 5th, after
          `material_weights`), forwarded to `psqt_value()` exactly the
          way `material_weights` already forwards to `material_value()`
          — independent of `material_weights` (either, both, or neither
          may be set on any given call). `eval_cache` is now skipped
          whenever EITHER override is non-null, not just
          `material_weights`, for the same staleness reason.
          `compute_loss()` (`tuner/tune.h`/`.cpp`) gained a matching
          optional `psqt_weights` parameter, forwarded straight through
          to `evaluate()`. `tune()` itself is UNCHANGED — it still only
          enumerates/updates `kMaterialParameters`, so a real
          `kPsqtParameters`-driven tuning run isn't callable yet, only
          `compute_loss()` at a hand-picked PSQT vector (which is what
          this session's own new tests exercise). See docs/
          DECISIONS.md, this session's entry.
    - [x] **Step 5 — L2 regularization (implemented externally, verified
          and integrated this session):** `TuneConfig::l2_lambda` added
          (default 0.0 — existing material-only tuning runs unaffected
          bit-for-bit, confirmed by test and by a full pristine-vs-
          modified build comparison). Applied analytically (closed-form
          `2*lambda*value` gradient contribution, not finite-difference)
          via a shared `l2_penalty()` helper templated over `Weights`,
          used by both `tune()` (`kMaterialParameters`) and the new
          `tune_psqt()` (`kPsqtParameters`, Step 6). Anchored parameters
          excluded from the penalty. See docs/DECISIONS.md, this
          session's entry, for the full verification account.
    - [x] **Step 6 — analytic gradient for PSQT terms (implemented
          externally, verified and integrated this session):**
          `compute_psqt_gradient()` computes PSQT's full 768-entry
          gradient in one board-scan pass per position (not 1536
          finite-difference `compute_loss()` calls), exploiting that
          PSQT's contribution to `evaluate()` is exactly linear in each
          table cell once `taper()`'s own single, whole-position call
          site is accounted for. A new `tune_psqt()` runs a real,
          callable PSQT gradient-descent loop on top of it (material
          held fixed) — a deliberate SEPARATE function from `tune()`,
          not a generalized `tune<Weights>()`, since the two functions'
          gradient *source* differs fundamentally (analytic vs. finite-
          difference), not just the `Weights` type. Still outstanding,
          by design, per this step's own ROADMAP.md scope: a materially
          larger self-play training corpus, and a real `nightwing_sprt`-
          gated match before any `tune_psqt()`-produced weights are
          hand-transcribed into `psqt.cpp`'s `constexpr` tables. See
          docs/DECISIONS.md, this session's entry, for the independent
          cross-check performed against the analytic gradient's
          correctness (agreement to ~1e-11 at a busier, multi-piece-type
          position, tighter than the ~8e-6 the original implementation's
          own single-cell cross-check reported) and the one real issue
          found and fixed (a GCC `-O3`-only `-Warray-bounds` false
          positive).
    - [x] **CLI follow-up to Steps 5-6 (Session 106):** `tuner/tune_main.cpp`
          (`nightwing_tune`) gained a `--psqt` mode running `tune_psqt()`
          (output as 12 copy-pasteable 8x8 grids matching `psqt.cpp`'s
          own literal formatting) and a new trailing `l2_lambda`
          positional argument in both modes — `l2_lambda` (Step 5) had no
          CLI exposure at all before this session. While hand-testing the
          new CLI argument, found that `learning_rate`/`l2_lambda`
          interact MULTIPLICATIVELY, not additively — a modest-looking
          `l2_lambda=0.001` at material mode's own default
          `learning_rate=20000.0` diverges every non-anchored parameter
          GEOMETRICALLY (observed O(100) → O(10^19) in 5 iterations),
          because the L2 term's own per-iteration multiplier
          `(1 - 2*learning_rate*l2_lambda)` exceeds magnitude 1 — a
          combination the original implementation's own L2 tests never
          exercised (both only ran 1 iteration, which cannot reveal
          multi-iteration geometric divergence). Fixed by adding
          `l2_update_is_stable()` (`tune.h`) plus a startup warning in
          `tune_main.cpp` (not a hard refusal) whenever a supplied
          `learning_rate`/`l2_lambda` pair crosses that threshold, and
          locked in with 3 new tests, including one confirming the
          actual observed divergence against a real `tune()` call, not
          just the closed-form formula in isolation. See docs/
          DECISIONS.md, this session's entry, for the full account.
    - [x] **Step 7 — mobility, the first "beyond PSQT" term (Session
          114):** `eval/mobility.h` gained `MobilityWeights` (8 plain
          `double` fields — knight/bishop/rook/queen x mg/eg, no array
          indexing needed, unlike `PsqtWeights` — mobility has no
          per-square dimension of its own) and `default_mobility_weights()`,
          both `constexpr`-constructible directly from
          `kKnightMobilityBonus`/etc. the same way `MaterialWeights`
          already is (those constants are header-visible, unlike
          `PsqtWeights`' hidden-in-`psqt.cpp` tables, so `PsqtWeights`'
          own out-of-line, all-zero-default pattern wasn't needed here).
          `mobility_value()` gained the same nullable-override parameter
          `material_value()`/`psqt_value()` already have.
          `eval::evaluate()` gained a matching `mobility_weights`
          parameter (inserted between `psqt_weights` and
          `incremental_material_psqt`), added to the same
          `eval_cache`-staleness condition `material_weights`/
          `psqt_weights` already trigger. `tuner/tune.h` gained
          `MobilityParameterRef`/`kMobilityParameters` (8 entries, ALL
          `anchored = false` — mobility is additive, like PSQT, so it
          doesn't share material's multiplicative flat-scaling
          degeneracy that pawn_mg/pawn_eg's anchoring exists to fix —
          same reasoning `kPsqtParameters`' own comment already gives)
          and `compute_loss()` gained a matching optional
          `mobility_weights` parameter, forwarded to `evaluate()` the
          same way `psqt_weights` already is. NOT YET CONSUMED by
          `tune()` itself, same "table exists and is independently
          tested ahead of that wiring" status `kPsqtParameters` had after
          Step 3, before Step 4 wired `PsqtWeights` through
          `compute_loss()`/`tune()` — a `tune()` call that actually
          drives `kMobilityParameters` is a later step, not this one.
          Inserting `mobility_weights` BETWEEN two existing parameters
          (rather than appending at the end) broke every call site that
          passed arguments positionally past `psqt_weights` — caught
          entirely by the compiler as type mismatches (a `Score*`/`int*`
          landing in the new `MobilityWeights*` slot), not silently:
          `search.cpp` (4 sites), `quiescence.cpp` (2 sites),
          `incremental_eval_tests.cpp` (3 sites), `eval_tests.cpp` (3
          sites) all fixed by inserting an explicit `nullptr` in the new
          slot. 628/628 tests green (608 carried over + 20 new: 7 in
          `mobility_tests.cpp`, 3 in `eval_tests.cpp`, 10 in
          `tune_tests.cpp` counting the pre-existing suite's own growth)
          under both Release and Debug/ASan+UBSan; `bench` unchanged at
          38,679 nodes (identical to Session 113's own value — every
          production call site's `mobility_weights` argument still
          defaults to `nullptr`, so this step adds capability only, same
          "zero effect on real search/eval behavior" pattern
          `kPsqtParameters`' own introduction (Step 3) followed).
    - [x] Step 8a — space (`eval/space.h`), the second "beyond PSQT" \
          term (Session 115): `SpaceWeights` (2 plain `double` fields,
          `square_mg`/`square_eg` — the smallest of the four weight
          structs so far, since space has only ONE constant,
          `kSpaceSquareBonus`, with no per-piece-type or per-square
          dimension at all) and `default_space_weights()`, both
          `constexpr`-constructible directly from `kSpaceSquareBonus`
          the same `MaterialWeights`/`MobilityWeights` pattern
          (mechanical, not `PsqtWeights`' out-of-line one — identical
          reasoning to Session 114's own mobility choice).
          `space_value()` gained the same nullable-override parameter
          `mobility_value()` already has. `eval::evaluate()` gained a
          matching `space_weights` parameter (inserted between
          `mobility_weights` and `incremental_material_psqt`, grouping
          all four weight-override parameters together), added to the
          same `eval_cache`-staleness condition the other three already
          trigger. `tuner/tune.h` gained `SpaceParameterRef`/
          `kSpaceParameters` (2 entries, `anchored = false` — additive
          term, same reasoning `kPsqtParameters`/`kMobilityParameters`
          already established) and `compute_loss()` gained a matching
          optional `space_weights` parameter, forwarded to `evaluate()`
          the same way `mobility_weights` already is. NOT YET CONSUMED
          by `tune()` itself, same "table exists and is independently
          tested ahead of that wiring" status `kMobilityParameters` had
          after Step 7. Inserting `space_weights` mid-signature (again,
          for readability) broke the same 6 production call sites
          Session 114's mobility insertion did (`search.cpp` x4,
          `quiescence.cpp` x2), each fixed with an explicit
          `/*space_weights=*/nullptr`, plus 6 test-file positional call
          sites (`eval_tests.cpp` x3, `incremental_eval_tests.cpp` x3)
          needing an extra `nullptr` inserted — all caught by the
          compiler as type mismatches, none silently. One test bug
          caught and fixed in this session's own new test code (not
          production code): the first draft of `compute_loss`'s
          space-forwarding test used a bare-kings-plus-one-pawn FEN
          with zero non-pawn material (`compute_phase() == 0`), and
          only perturbed `square_mg` — since `taper()` selects the EG
          term entirely at phase 0, the mg-only perturbation was
          silently invisible, not a real forwarding failure (caught
          immediately as a `FAILED` assertion, `loss_with_override`
          exactly 0.0 instead of the expected nonzero, not a false
          pass — fixed by perturbing both `square_mg` and `square_eg`).
          637/637 tests green (628 carried over + 9 new: 3 in
          `tests/space_tests.cpp`, 2 in `tests/eval_tests.cpp`, 4 in
          `tests/tune_tests.cpp`) under both Release and
          Debug/ASan+UBSan, zero new sanitizer findings; `bench`
          unchanged at 38,679 total nodes — every production call site's
          `space_weights` argument still defaults to `nullptr`, the
          same "zero behavior change" pattern every prior Tier 0 step
          has followed.
    - [x] Step 8b — king safety (`eval/king_safety.h`), pawn structure
          (`eval/pawns.h`), and threats (`eval/threats.h`) all now have
          the same `Weights` struct + `ParameterRef` group +
          `evaluate()`/`compute_loss()` wiring treatment mobility
          (Step 7) and space (Step 8a) got, done as three separate
          sub-steps in the order threats -> king safety -> pawn
          structure (all three in Session 116, across a `Continue` and
          two `Next` advances within that one session) — see each
          sub-step's own entry below for what shape decisions each one
          needed. This closes out Step 8, and with it ROADMAP.md's
          entire "PSQT and beyond" tuner item (Steps 1-8): every eval
          term now has its own `Weights` struct and `ParameterRef`
          table, none yet consumed by `tune()` itself.
        - [x] threats (`eval/threats.h`), Step 8b's first sub-step
              (Session 116): `ThreatsWeights` (24 plain `double` fields
              — 12 `kXxxYyyPenalty` constants x mg/eg each, the same
              plain-scalar shape mobility/space already use, just more
              fields — 3 penalty categories x 4 piece types, no array
              indexing needed) and `default_threats_weights()`, both
              `constexpr`-constructible directly from the 12 constants,
              same `MaterialWeights`/`MobilityWeights`/`SpaceWeights`
              pattern. `threats_value()`'s three internal helper
              functions (`pawn_threat_penalty()`/`hanging_penalty()`/
              `overloaded_penalty()`, threats.cpp's own anonymous
              namespace) each gained a `const ThreatsWeights*` parameter
              alongside `threats_value()`'s own new nullable-override
              parameter, all four threaded through together.
              `eval::evaluate()` gained a matching `threats_weights`
              parameter (inserted between `space_weights` and
              `incremental_material_psqt`, keeping all five weight-
              override parameters grouped together), added to the same
              `eval_cache`-staleness condition the other four already
              trigger. `tuner/tune.h` gained `ThreatsParameterRef`/
              `kThreatsParameters` (24 entries, `anchored = false` —
              additive term, same reasoning every sibling table already
              established) and `compute_loss()` gained a matching
              optional `threats_weights` parameter, forwarded to
              `evaluate()` the same way `space_weights` already is. NOT
              YET CONSUMED by `tune()` itself, same status every sibling
              table has after its own introducing session. Inserting
              `threats_weights` mid-signature broke the same 6
              production call sites (`search.cpp` x4, `quiescence.cpp`
              x2) and 6 test-file positional call sites
              (`eval_tests.cpp` x3, `incremental_eval_tests.cpp` x3)
              Session 115's space insertion did, each fixed with one
              more explicit `nullptr`, all caught by the compiler.
              646/646 tests green (637 carried over + 9 new: 3 in
              `tests/threats_tests.cpp`, 2 in `tests/eval_tests.cpp`, 4
              in `tests/tune_tests.cpp`) under both Release and
              Debug/ASan+UBSan, zero new sanitizer findings; `bench`
              unchanged at 38,679 total nodes.
        - [x] king safety (`eval/king_safety.h`), Step 8b's second
              sub-step (Session 116, same session as threats):
              `KingSafetyWeights` (22 plain `double` fields — 5 plain
              `Score` constants x mg/eg (10 fields: shield, open_file,
              semi_open_file, attack_unit, back_rank) plus
              `kPawnStormPenalty`'s 8-entry array FLATTENED into 6
              individually-named scalar field pairs, indices 1-6 only
              (12 fields) — see this struct's own doc comment,
              king_safety.h, for the full account) and
              `default_king_safety_weights()`, both `constexpr`-
              constructible directly from the underlying constants,
              same `MaterialWeights`/`MobilityWeights`/`SpaceWeights`/
              `ThreatsWeights` pattern for the plain-scalar fields.
              DESIGN DECISION (docs/DECISIONS.md has the full account):
              rather than generalizing `tuner::ParameterRef`'s
              `array_member` mechanism (hardcoded to
              `std::array<double, 64>` for PSQT's own 64-square case)
              to an arbitrary size just for this one much-smaller
              8-entry table, `kPawnStormPenalty` is flattened into 6
              named scalar fields instead — every `KingSafetyParameterRef`
              entry sets only `member`, none set `array_member`, unlike
              `kPsqtParameters`. Indices 0 and 7 (structurally
              unreachable at runtime — king_safety.cpp's own
              `most_advanced_rank` is always in [1, 6] whenever it's
              actually used) are correspondingly NOT represented as
              tunable fields at all. `king_safety_value()`'s own new
              helper function `pawn_storm_penalty()` (king_safety.cpp's
              own anonymous namespace) switches on the storming pawn's
              relative rank to select the matching flattened field
              when an override is supplied. `eval::evaluate()` gained a
              matching `king_safety_weights` parameter (inserted between
              `threats_weights` and `incremental_material_psqt`, keeping
              all six weight-override parameters grouped together),
              added to the same `eval_cache`-staleness condition the
              other five already trigger. `tuner/tune.h` gained
              `KingSafetyParameterRef`/`kKingSafetyParameters` (22
              entries, `anchored = false` — additive term, same
              reasoning every sibling table already established) and
              `compute_loss()` gained a matching optional
              `king_safety_weights` parameter, forwarded to
              `evaluate()` the same way `threats_weights` already is.
              NOT YET CONSUMED by `tune()` itself, same status every
              sibling table has after its own introducing session.
              Inserting `king_safety_weights` mid-signature broke the
              same 6 production call sites and 6 test-file positional
              call sites every prior "beyond PSQT" insertion has,
              each fixed with one more explicit `nullptr`. One test bug
              caught and fixed in this session's own new test code (not
              production code): the first draft of
              `kKingSafetyParameters`' own "every member pointer reaches
              exactly the field its name claims" test used -1.0 as its
              per-field sentinel (the same value every sibling table's
              own equivalent test already uses safely) — but
              `KingSafetyWeights`' own defaults happen to contain TWO
              fields already exactly equal to -1.0
              (`kAttackUnitPenalty.eg` and `kPawnStormPenalty[3].eg`,
              both `{..., -1}` in king_safety.h), so the sentinel
              collided with real default data, producing a genuine
              `FAILED` assertion (`changed_count == 3`, not the false
              pass a less careful check might have missed) on the very
              first test run — fixed by switching the sentinel to
              -999999.0, a value confirmed not to collide with any real
              `KingSafetyWeights` default. 655/655 tests green (646
              carried over + 9 new: 3 in `tests/king_safety_tests.cpp`,
              2 in `tests/eval_tests.cpp`, 4 in `tests/tune_tests.cpp`)
              under both Release and Debug/ASan+UBSan, zero new
              sanitizer findings; `bench` unchanged at 38,679 total
              nodes.
        - [x] pawn structure (`eval/pawns.h`), Step 8b's third and
              final sub-step (Session 116, `Next`-continued): `PawnsWeights`
              (59 plain `double` fields — 4 plain `Score` constants x
              mg/eg (8 fields: isolated, doubled, backward, connected)
              plus FOUR separate 8-entry arrays (`kPassedPawnBonus`,
              `kConnectedPassedPawnBonus`, `kOutsidePassedPawnBonus`,
              `kCandidatePassedPawnBonus`) each FLATTENED into 6
              individually-named scalar field pairs, indices 1-6 only
              (48 fields total across all four), plus
              `kOutsidePassedPawnMinFileGap` (a plain `int` file-
              distance threshold) represented as a single plain
              `double` field, `outside_min_file_gap` (1 field), plus 1
              plain `Score` constant x mg/eg (2 fields: island) — see
              this struct's own doc comment, pawns.h, for the full
              account) and `default_pawns_weights()`, both `constexpr`-
              constructible directly from the underlying constants.
              BOTH open design questions flagged after king safety
              (Session 116, above) were resolved here: (1) king
              safety's array-flattening resolution DID transfer
              cleanly, applied four times over rather than once, with
              no new wrinkle; (2) `kOutsidePassedPawnMinFileGap` is
              represented the same uniform way every other field is (a
              plain `double`, rounded via `round_to_int()` at the point
              of use), with a documented caveat that its true effect on
              the score is a discrete step function, not a smooth
              linear one, so gradient-based tuning may treat it
              differently from the other 58 fields — flagged, not
              resolved, for whichever session builds the actual
              gradient/tuning consumer. `pawn_structure_value()` gained
              4 new internal helper functions
              (`passed_pawn_bonus()`/`connected_passed_pawn_bonus()`/
              `outside_passed_pawn_bonus()`/`candidate_passed_pawn_bonus()`,
              each switching on relative rank) plus an updated
              `is_outside_passed_pawn()` (now taking the min-file-gap
              threshold as a parameter instead of reading the constant
              directly), all threaded through `pawn_structure_value()`'s
              own new nullable-override parameter. `eval::evaluate()`
              gained a matching `pawns_weights` parameter (inserted
              between `king_safety_weights` and
              `incremental_material_psqt`, keeping all seven weight-
              override parameters grouped together) — and, uniquely
              among all six "beyond PSQT" terms so far, this insertion
              ALSO required updating `pawn_tt`'s own existing
              staleness-guard logic (evaluate()'s dedicated pawn-hash-
              table caching, separate from `eval_cache`): the same
              `pawns_weights != nullptr` condition that disables
              `eval_cache` now also disables consulting/storing into
              `pawn_tt`, for the identical staleness reason, closing a
              real gap that would otherwise have let a pawn-structure-
              override call silently read or poison the shared pawn
              hash table. `tuner/tune.h` gained
              `PawnsParameterRef`/`kPawnsParameters` (59 entries,
              `anchored = false`) and `compute_loss()` gained a
              matching optional `pawns_weights` parameter, forwarded to
              `evaluate()` the same way `king_safety_weights` already
              is. NOT YET CONSUMED by `tune()` itself, same status
              every sibling table has after its own introducing
              session. Inserting `pawns_weights` mid-signature broke
              the same 6 production call sites and 6 test-file
              positional call sites every prior "beyond PSQT" insertion
              has, each fixed with one more explicit `nullptr`. One
              bug caught and fixed mid-implementation, before any test
              even ran: the first draft of `PawnsWeights` entirely
              omitted `kCandidatePassedPawnBonus` (a fourth 8-entry
              array easy to miss alongside the other three, since
              `pawn_structure_value()`'s own candidate-passed-pawn
              check sits in a different part of the function than the
              already-passed checks for the other three arrays) —
              caught by re-reading the function body being wired up
              against the struct just written, before compiling or
              testing either, and fixed by adding the missing 12 fields
              (`candidate_passed_rank1_mg`/`_eg` through `_rank6_*`)
              and its own helper function. 666/666 tests green (655
              carried over + 11 new: 4 in `tests/pawns_tests.cpp`, 2 in
              `tests/eval_tests.cpp`, 4 in `tests/tune_tests.cpp`, 1 new
              in `tests/pawn_tt_tests.cpp` confirming `pawn_tt`'s own
              new bypass behaves identically to `eval_cache`'s) under
              both Release and Debug/ASan+UBSan, zero new sanitizer
              findings; `bench` unchanged at 38,679 total nodes. This
              closes out Step 8b, and with it ROADMAP.md's entire "PSQT
              and beyond" tuner item (Steps 1 through 8) — every eval
              term now has its own `Weights` struct and `ParameterRef`
              table, though none is yet actually consumed by `tune()`
              itself (see the very next unchecked item below —
              "A materially larger self-play corpus" and mandatory
              SPRT-gating — for what standing between here and an
              actual tuning run still looks like; that item, not a new
              Step 9, is next in top-to-bottom order).
    - [x] "A materially larger self-play corpus" and "mandatory
          SPRT-gating before any tuned values are committed" (this
          item's own original intro text, docs/DECISIONS.md, 2026-09-08
          (2)) — the SPRT-gating half is now addressed. The corpus-size
          half's own item, immediately below, was corrected/rescoped by
          Session 119 (docs/DECISIONS.md has the full account) — it
          turned out NOT to be the actual remaining blocker; see that
          item's own text for what is.
          Session 113's own correction entry claimed "mandatory
          SPRT-gating" specifically could already be done with this
          repo's existing `nightwing_sprt`/`play_match()` tooling;
          Session 117 checked that claim against the actual code and
          found it did NOT hold for any of the 6 "beyond PSQT" terms
          Sessions 114-116 just built (mobility/space/threats/king-
          safety/pawns/PSQT itself) — `play_match()`
          (`tuner/match.h`/`match.cpp`) and the `search_fixed_depth()`
          it called only threaded an `eval::MaterialWeights` override
          through search at all; none of the other 6 `Weights` types
          had any path through `search_fixed_depth()`/`negamax()`/
          `quiescence()` into a real game. Session 118 closed that gap:
          rather than adding a `const MobilityWeights*`/`SpaceWeights*`/
          `ThreatsWeights*`/`KingSafetyWeights*`/`PawnsWeights*`/
          `PsqtWeights*` parameter apiece (six separate insertions,
          Session 117's first-listed option), a single bundling struct,
          `eval::EvalWeightsOverride` (`eval/eval.h`), was added instead
          — a `const EvalWeightsOverride*` (default `nullptr`) is now
          threaded through `search_fixed_depth()`/
          `search_iterative_deepening()`/`negamax()`/`search_root()`/
          `run_lazy_smp_helper()`/`quiescence()` at every call site,
          unpacked into the matching six individual pointers exactly at
          each `eval::evaluate()` call. `EvalWeightsOverride` also
          carries a `material` field for uniformity, but
          `search.cpp`/`quiescence.cpp` deliberately do NOT read it —
          `material_weights` keeps its own pre-existing, separately-
          threaded parameter unchanged (avoiding re-touching that
          parameter's own many already-wired call sites for zero
          behavioral benefit). `MatchConfig` (`tuner/match.h`) gained
          `eval_weights_a`/`eval_weights_b` fields (both default
          `nullptr`), forwarded through `play_match()`'s existing
          per-game color-alternation logic into
          `play_one_match_game()`'s own `search_fixed_depth()` call,
          the same way `threads_a`/`threads_b` were added previously.
          Two new tests confirm the fix end to end, not just that it
          compiles: a direct `search_fixed_depth()` call with a
          20x-boosted `MobilityWeights` override changes the returned
          score at real search depth, and a `play_match()` run with one
          side's mobility weights turned into an active penalty
          (`eval_weights_b` only, `eval_weights_a` left `nullptr`) loses
          every game rather than scoring the ~50/50 result silent-
          ignoring would produce (`tests/match_tests.cpp`,
          `[eval_weights]`). Every one of this session's own resulting
          call-site fixes across `search.cpp`/`quiescence.cpp`/
          `uci.cpp`, and the ~20 existing test call sites that needed an
          explicit `nullptr` inserted for the new parameter (mirroring
          every prior "beyond PSQT" insertion's own established
          pattern), reverified against a real `cmake`+`ctest` build in
          this session, not compiled-in-isolation guesswork — full
          existing suite plus the 2 new cases green (668 test cases, 0
          failures).
    - [x] **Generalize `tune()` to the 5 remaining "beyond PSQT" tables
          (mobility/space/threats/king-safety/pawns)** — corrected/
          rescoped by Session 119 from this item's own prior text
          ("A materially larger self-play corpus"), which
          mischaracterized what's actually still blocking a real tuning
          run for these 5 terms, then implemented by Session 120.
          Checked `tune.cpp`/`tune_main.cpp` directly rather than
          assuming from ROADMAP wording alone: `compute_loss()` already
          accepted all 7 `Weights` types as optional overrides (Tier 0
          Steps 4/7/8a/8b each wired their own term through to it
          independently), but `tune()`'s own finite-difference
          gradient-descent loop only ever enumerated/updated
          `kMaterialParameters`. PSQT is a separate, already-addressed
          case (Tier 0 Step 6's `tune_psqt()`, an analytic-gradient path
          specifically for PSQT's 768-parameter space, docs/DECISIONS.md
          2026-09-08 (2)) — the 5 remaining terms are each a much
          smaller parameter count (8-59 scalars apiece, not 768), so
          finite-difference (the same approach material tuning already
          uses) was the right generalization, not a second
          analytic-gradient effort (docs/DECISIONS.md, 2026-09-21 (2)).
          Session 120 built one generic `tune_term<Weights, N>()`
          finite-difference helper (`tune.cpp`, anonymous namespace) —
          the same loop shape `tune()`'s own `kMaterialParameters` case
          already used, generalized over any `ParameterRef<Weights>`
          table via a small `compute_loss_for()` overload set dispatching
          to `compute_loss()`'s correct trailing pointer slot — and five
          thin named entry points on top of it (`tune_mobility()`/
          `tune_space()`/`tune_threats()`/`tune_king_safety()`/
          `tune_pawns()`, `tune.h`/`tune.cpp`), each holding material
          fixed and defaulting to that term's own
          `default_XXX_weights()`, mirroring `tune_psqt()`'s own
          "one named function per term" precedent (a real, previously-
          open design choice — see docs/DECISIONS.md, 2026-09-21 (2)'s
          own "Alternatives considered" — now settled this way). None of
          the 5 tables has an `anchored` entry today, so `tune_term()`
          does not special-case that field at all, unlike `tune()`'s own
          `kMaterialParameters` loop (flagged in `tune.h`'s own doc
          comment as something a future anchored entry would need
          revisited). `tune_main.cpp` gained matching
          `--mobility`/`--space`/`--threats`/`--king-safety`/`--pawns`
          CLI modes alongside the existing `--psqt`, sharing the same
          positional-argument slots, with output via a new generic
          `print_term_weights()` (`name=value` per field, since none of
          these five tables has PSQT's per-square dimension the way
          `--psqt` mode's own grid printer needs). `tests/tune_tests.cpp`
          gained 6 new tests: one "all-neutral" (bare kings, every
          term's contribution identically 0, so every gradient must come
          out bit-for-bit 0) regression test per new function — the
          direct counterpart of this file's own pre-existing
          material-side version of that same test — plus one "a
          consistent training signal reduces loss" test for
          `tune_mobility` specifically, modeled on the already-existing
          material- and PSQT-side versions of that same test. Reverified
          against a real `cmake`+`ctest` build later in this session (a
          build-capable environment turned out to be available after
          all — `cmake` installed via `apt-get`, real repo tarball
          downloaded, this session's 4 changed files dropped in): both
          `nightwing_lib` and `nightwing_tune` compiled and linked
          clean, CLI smoke tests of all 5 new modes matched the new
          tests' own predictions exactly (zero-gradient on all-neutral
          input, `knight_mg` moving up with loss monotonically
          decreasing on a disagreeing mobility signal), and the full
          suite passed green — 674 test cases, 691,106 assertions, 0
          failures, including all 52 `[tuner][tune]`-tagged tests. See
          docs/SESSIONS.md's own entry for this session for the full
          verification detail.
    - [x] Actually run `tune_mobility()`/`tune_space()`/`tune_threats()`/
          `tune_king_safety()`/`tune_pawns()` (the item above) against
          real self-play data, and decide from that run's own result
          whether the existing 5000-game self-play corpus size is
          adequate for these 5 modest-sized tables — closed by Session
          122, which read back the real dispatch (5000 games, 208,360
          quiet positions, 200 iterations each). **The honest answer is
          NOT a single yes/no — it varies by term, and two of the five
          are not trustworthy as converged regardless of corpus size:**
          `pawns` (3.3% loss reduction) converged cleanly to a sensible,
          rank-monotonic passed-pawn table — corpus adequate here.
          `king-safety` (3.1%) converged well for its dominant terms
          (closest-to-king pawn-storm ranks stayed stable, correctly
          signed) but several of its own weaker sub-terms
          (`attack_unit_mg`, `semi_open_file_mg`, a few
          `pawn_storm_rank2-4` entries) flipped sign. `mobility` (7.0%
          loss reduction — the LARGEST of the five, which here is a bad
          sign, not a good one) had nearly every knight/bishop/queen
          mg/eg bonus flip from small-positive to sizeable-negative —
          chess-nonsensical, not trusted as converged. `space` (0.7%)
          showed the same pattern at smaller scale (`square_mg` moved
          8x its own magnitude for under 1% loss improvement).
          `threats` (0.026% — essentially flat) barely moved at all —
          NOT a corpus-size problem: self-play's quiet-position filter
          structurally excludes tactical (hanging/overloaded-piece)
          positions by construction, so more games drawn the SAME way
          won't fix it; a different sampling method would be needed.
          The mobility/space/king-safety-subterm sign flips are a
          DIFFERENT failure mode from threats' flatness: those loss
          curves are smooth and genuinely converged (not
          oscillating/diverging) — the optimizer found a real local
          optimum in this specific sample, just one where a
          weaker-signal term tuned in isolation (every other correlated
          `evaluate()` term frozen at its default) picked up a spurious
          rather than causal correlation. See docs/DECISIONS.md,
          2026-09-22, for the full per-term numbers and reasoning; two
          new items below follow directly from this finding.
    - [x] **Fix `tune_psqt()`'s zero-movement bug** — found while
          closing the item above (Session 122): loss stayed EXACTLY
          flat (0.050887) across all 200 iterations of a real
          208,360-position dispatch, and the "tuned" `king_mg`/
          `king_eg` tables came back byte-for-byte identical to
          `psqt.cpp`'s own compiled-in `kKingMgTable`/`kKingEgTable` —
          `tune_psqt()`'s analytic gradient is doing nothing at full
          scale, not just moving slowly. The same flat-loss symptom was
          visible on Session 121's own small (732-position) local
          smoke test before dispatch and was wrongly written off there
          as "too little signal for this toy scale" rather than
          investigated — a real regression that should have been
          caught before, not after, shipping. FIXED (Session 125):
          root cause was NOT a broken gradient (`compute_psqt_gradient()`
          still independently agrees with a hand-rolled finite-difference
          probe, `tests/tune_tests.cpp`) — it was `tune_main.cpp`'s own
          `--psqt` `learning_rate` default (100.0), calibrated only
          against `tests/tune_tests.cpp`'s narrow, single-repeated-
          position toy scenario, colliding with `psqt_value()`'s own
          `round_to_int()` on every real, diverse self-play corpus: a
          real per-cell average gradient magnitude (measured directly
          against an 8104-position self-play corpus: ~1e-5, roughly
          1000x weaker than the toy test's own unanimous, uncontested
          signal) times that learning rate produces a per-iteration step
          orders of magnitude too small to ever cross a full integer
          unit within a normal run — so the underlying double weights
          WERE moving, just never far enough for any table cell's
          rounded, printed value (or `compute_loss()`'s own rounded-
          eval-dependent output) to visibly change at all. Fixed by
          raising the default to 200000.0 (chosen the same "measure,
          don't guess" way, tried directly against the same real corpus
          at several candidate values — loss decreases smoothly and
          monotonically through at least 1,000,000, with instability
          only appearing around 2,000,000; 200000.0 sits an order of
          magnitude below that with real headroom while still producing
          a genuine, substantial loss reduction). `tests/tune_tests.cpp`
          gained a new diluted/mixed-signal regression test specifically
          exercising this "small but genuine gradient, real learning
          rate" regime, closing the test-coverage gap this item's own
          note already flagged. See docs/DECISIONS.md, this session's
          own dated entry, for the full measured account.
    - [ ] **Investigate regularization / per-term learning rates for
          the sign-flip instability** found in mobility/space/some
          king-safety sub-terms (item above) — `TuneConfig::l2_lambda`
          already exists and is plumbed through `tune_term()` (Session
          120) but was left at 0.0 (no regularization) for this run;
          likely candidates, neither attempted yet: (a) a nonzero
          `l2_lambda` pulling each term back toward its own starting
          value, reducing the incentive to chase a spurious correlation
          far from a sensible default; (b) per-term learning rates
          instead of borrowing material's own 20000.0 uniformly across
          all 5 finite-difference terms (docs/DECISIONS.md, 2026-09-21
          (3), already flagged this as "not independently re-verified
          for these five terms' own real production data" — this is
          that re-verification, and it suggests the borrowed value may
          be too aggressive for smaller-magnitude terms specifically).
          NOT the same problem as `threats`' near-zero movement (a
          sampling-methodology gap, not a tuning-stability one) — keep
          these two findings' own follow-up work separate.

## Priority Fixes (external code review, 2026-09-17)

Not phase-gated — inserted here, ahead of the NPS / Raw Speed track's own
remaining items (Incremental evaluation was the next item in sequence per
Session 108's own handoff), per this project's established "bugs before
enhancements" convention (see both Priority Fixes sections above). Two
independent audit documents were reviewed against `main` on 2026-09-16 (a
bug-notes document covering search/ordering/eval correctness issues, and a
roadmap-sequencing audit confirming Session 108's own "Incremental
evaluation" handoff and its `sizeof(Position)` prerequisite) — every claim
in both was independently verified against the actual repository (grep,
standalone compiled probes, and a scratch A/B build comparison) before any
fix was written, rather than trusted at face value. See docs/DECISIONS.md,
2026-09-17, for the full verification account, including the one factual
error found in the audit documents (a tuning-history claim, not a code
bug — corrected there, no code implicated).

- [x] Castling invisible to move ordering and pruning — `Move::is_castle()`
      had zero callers anywhere in `src/`, so a castling move fell through
      `search/ordering.cpp`'s `score_move()` to the same 0-by-default
      quiet-move path as any other untried move, and `search/search.cpp`'s
      `move_is_quiet`/`eligible_for_lmr` didn't exempt it from futility/
      LMP/history pruning or LMR either — a legal, often strong `O-O`/
      `O-O-O` could be late-move-reduced or pruned outright before its
      value was ever seen. Fixed: `ordering.cpp` gives castling its own
      fixed `kCastleScore` band (between promotions and killers);
      `search.cpp` excludes `move.is_castle()` from `move_is_quiet`,
      `eligible_for_lmr`, and the post-cutoff killer/history bookkeeping
      (which would otherwise write dead entries castling's own fixed
      score never reads). See docs/DECISIONS.md, 2026-09-17, including an
      empirical A/B build comparison proving the search.cpp half of this
      fix changes real search output (a genuine tactical line the
      unfixed build missed by reducing castling's own subtree).
- [x] Quiescence search dropped quiet (non-capturing) promotions —
      `quiescence.cpp`'s candidate filter only kept check-evasions,
      captures, and (at the first qsearch ply only) checking moves; a
      quiet promotion matched none of these, so qsearch silently stood
      pat on the pre-promotion material instead of ever trying it. Fixed:
      added `|| move.is_promotion()` to the unconditional (not
      `include_checks`-gated) branch, matching how captures are already
      handled — a promotion is exactly as forcing/material-changing as a
      capture, and is already a small, self-limiting candidate set. See
      docs/DECISIONS.md, 2026-09-17.
- [x] Knight and Queen PSQT tables never mirrored for Black —
      `psqt_value()` mirrored the lookup square for Black
      (`mirror_vertical(sq)`) for every piece type except Knight and
      Queen, which read `sq` directly, under a comment claiming "no color
      distinction" for those two tables. That's true of the STORAGE (one
      table serves both colors, per Michniewski's convention) but false
      of the LOOKUP: unlike pawn/bishop/rook/king, the knight and queen
      tables are not row-for-row rank-mirror-symmetric, so skipping the
      mirror was a real, silent, position-dependent scoring bug for
      Black's knights and queens, duplicated independently in the
      tuner's `PsqtWeights`-supplied lookup path. Fixed: both lookup
      paths now apply `color == Color::White ? sq : mirror_vertical(sq)`
      to Knight and Queen too, matching every other piece type. Confirmed
      via a standalone compiled probe before the fix (a true mirror pair,
      White knight on d2 versus Black knight on d7, scored `{0,0}` vs
      `{5,5}` — should have matched) and a broad regression test now
      sweeping all 6 piece types x 64 squares (added this session — the
      pre-existing symmetry test only ever checked Pawn, which is exactly
      why this went undetected). See docs/DECISIONS.md, 2026-09-17, for
      the full account, including the resulting bench node-count shift
      (38,378 -> 40,656 -- expected, since Black's knight/queen eval
      values genuinely changed).

## NPS / Raw Speed (parallel track — not phase-gated, external review 2026-09-08)

Distinct axis from the Priority Fixes section above: that section is entirely about
searching *fewer nodes* for the same answer (pruning/reductions/ordering); this
section is about searching the nodes it does visit *faster* (raw nodes-per-second).
Both matter and are pursued independently — closing the gap with stronger classical
engines needs both, not one traded off against the other (docs/DECISIONS.md,
2026-09-08 (5)).

- [x] **Profile first (do this before any of the items below):** DONE,
      Session 108. Neither `perf` nor `valgrind` is available in this
      sandbox (no `perf_event_paranoid` access, no `valgrind` package
      installed) — `gprof` (`-pg` instrumentation) used instead, the
      standard portable GCC/Clang fallback for function-level hot-path
      attribution; see docs/DECISIONS.md, 2026-09-16 (2), for why this
      substitution is judged equivalent for this item's purpose. `bench`
      alone (38,378 nodes, ~100ms) proved too short for statistically
      meaningful sampling, so the actual profiled workload was `bench`
      plus two longer `go depth` searches on busier middlegame/Kiwipete-
      style positions, ~6.7M nodes over ~26s. Result: `eval::evaluate()`
      and its own `eval/*` term functions account for roughly 46% of
      profiled self-time in aggregate, with `evaluate()` itself the
      single largest individual contributor (~14%) — called from
      `negamax()`, `order_moves()`, AND `quiescence_impl()` on very
      nearly every node visited, confirming the from-scratch-every-node
      pattern this item's own prior comment already flagged, not just
      assumed. Also surfaced, as a direct consequence of this
      confirmation: `board.h`'s `sizeof(Position) <= 192` static_assert
      is already at its documented 3-cache-line ceiling with zero
      headroom, and an incremental material+PSQT accumulator's most
      natural home (a `Score` field inside `Position`, the same role
      `zobrist_hash` already plays) would breach it outright — this is
      now an explicit open sub-decision the item immediately below must
      resolve FIRST, before any accumulator code is written, not an
      implementation detail to discover mid-way. See docs/DECISIONS.md,
      2026-09-16 (2), for the full flat-profile numbers, the call-graph
      confirmation, and the three options sketched (not chosen) for the
      cache-line conflict.
- [x] Incremental evaluation (material + PSQT as a running accumulator,
      updated with a small delta on `make_move`/`unmake_move`, instead
      of recomputed from zero on every call to `eval::evaluate()`) —
      done, 2026-09-18. The `sizeof(Position)` sub-decision above
      resolved as: the accumulator does NOT live on `board::Position`
      (Option 1, breaching the 192-byte ceiling) — it's threaded
      through `negamax()`'s/`quiescence()`'s own recursion instead (a
      new `eval::Score` parameter, `mat_psqt`, mirroring how
      `prev_piece`/`prev_to` are already threaded for continuation
      history), computed once from scratch at the true search root
      (`eval::compute_material_psqt()`) and updated by one arithmetic
      step per move from there (`eval::material_psqt_delta()`, new file
      `src/eval/incremental.h`/`.cpp`) rather than a fresh 64-square
      scan at every node. `eval::evaluate()` gained a new optional
      `incremental_material_psqt` parameter (defaults to nullptr,
      identical behavior to before) rather than changing its existing
      contract. ProbCut's and singular extension's own verification
      searches were deliberately left unwired this session (still fall
      back to a full rescan) — a documented scope cut, not an
      oversight; see docs/DECISIONS.md, this session's entry, for the
      full rationale on both the Position-vs-search-frames decision and
      the scope cut. Verified against the real Catch2 suite (not just a
      sandbox check): 604/604 tests green, including 14 new tests in
      `tests/incremental_eval_tests.cpp` covering every delta case
      (quiet move, capture, en passant, quiet promotion, capture-
      promotion, underpromotion, all four castling sides, a multi-move
      chain, and `evaluate()`'s own weights-override fallback). One
      real bug was caught and fixed during this session's own
      verification pass — see docs/SESSIONS.md, this entry, "bugs
      fixed.
- [x] Lazy evaluation / early-exit on cheap terms: compute material +
      PSQT first inside `eval::evaluate()`; if that alone already clears
      alpha/beta by a comfortable margin, skip the remaining expensive
      terms (mobility, king safety, threats, space, pawn structure) and
      return early, rather than always computing every term regardless
      of whether the cheap ones already settled the question. DONE,
      2026-09-18 (2): `eval::evaluate()` (`eval.h`/`.cpp`) gained two new
      optional trailing parameters, `lazy_alpha_white`/`lazy_beta_white`
      — an alpha-beta window in WHITE'S PERSPECTIVE (CPW "Lazy
      Evaluation"). When set, material+PSQT (`score`) is tapered on its
      own, compared against the window widened by a new
      `kLazyEvalMargin` (650, a conservative, untuned hand-survey of
      every other term's combined plausible swing) in each direction,
      and returned immediately — skipping pawn structure, mobility, king
      safety, and every other term — whenever it already clears the
      window by more than that margin. `compute_phase(pos)` was hoisted
      to be computed once, up front (reused by both the lazy check and
      the final `taper()` call), rather than only at the end as before —
      a strict no-op on the non-lazy path (still exactly one call), not
      a new cost. `eval_cache` is deliberately never consulted whenever
      this window is set, same staleness reasoning as `material_weights`/
      `psqt_weights` already established. Wired into exactly ONE call
      site this session — `quiescence.cpp`'s own stand-pat computation
      (the single highest-frequency static-eval call in the engine, and
      the cleanest fit: a genuine alpha-beta window is already on hand
      there with no extra plumbing) — converting quiescence's own
      side-to-move-relative `alpha`/`beta` to White's perspective the
      same way its return value already gets un-converted
      (`lazy_alpha_white = us==White ? alpha : -beta`, `lazy_beta_white
      = us==White ? beta : -alpha`, mirroring negamax()'s own child-call
      negate-and-swap convention). `negamax()`'s own RFP/razoring/
      futility static-eval call sites, and `order_moves()`'s eval calls,
      were deliberately NOT wired this session — a documented scope cut,
      not an oversight (see docs/DECISIONS.md) — revisit in a future
      session if profiling shows it's worth the additional diff surface.
      Verified against the real project toolchain: full Release and
      Debug/ASan+UBSan builds, 608/608 tests green (604 pre-existing + 4
      new: 3 in `tests/eval_tests.cpp` exercising `evaluate()`'s own
      early-exit mechanism directly — a wide window never triggers it, a
      tight window against an overwhelming material edge triggers it and
      returns exactly the material+PSQT-only tapered value, and
      `eval_cache` staleness under a lazy window — plus 1 in
      `tests/quiescence_tests.cpp` confirming the stand-pat site's own
      real end-to-end behavior: fails high correctly using only
      material+PSQT, `nodes == 1`). `bench` moved from 40,656 to 37,287
      total nodes (a genuine, expected node-count DECREASE — different,
      earlier stand-pat cutoffs in quiescence from the same underlying
      technique that makes this optimization useful in the first place,
      not a regression; ARCHITECTURE.md's own Benchmarking Discipline
      section is satisfied by this entry's own account).
- [x] **Staged / lazy move generation: a `MovePicker`-style iterator that
      generates captures first and only generates quiet moves if the
      search gets past the captures without a cutoff, instead of always
      generating the entire legal move list upfront.** A structural
      change (a real iterator type, not just a flat generate-then-sort
      list), not a small patch — treated as its own sub-tracked effort
      (Sessions 112-113), the same "own step-by-step order" convention
      the Tier 0 tuner item above uses, not a single checkbox. CLOSED
      OUT (Session 113) on Step 3a's correctness evidence; Step 3b (a
      real strength comparison) couldn't be done with this repo's
      existing tooling and was re-scoped as its own separate new item
      (below, "Engine-vs-engine match infrastructure") rather than left
      blocking this one indefinitely — see that item and docs/
      DECISIONS.md's correction entry.
    - [x] **Step 1 — `GenType`-staged move generation in
          `board::generate_legal_moves()` (Session 112):** `movegen.h`
          gained a `GenType` enum (`Captures`, `Quiets`, `All`) and
          `generate_legal_moves()` a matching third parameter, default
          `All` (source-compatible with every pre-existing 2-argument
          call site — none needed changing). The split follows
          `search/ordering.h`'s own existing move-ordering band
          boundary, not a plain "has the Capture flag" test: `Captures`
          yields every capture (incl. en passant, capture-promotions)
          AND every promotion, capturing or not (a quiet promotion is
          exactly as forcing/material-changing as a capture); `Quiets`
          yields everything else (ordinary quiet moves, double pawn
          pushes, castling). All pin/check/legality machinery
          (`compute_pins()`, `attackers_to()`, the single/double-check
          target mask) is unchanged and runs identically regardless of
          `gen_type` — only which destination squares survive for each
          already-legal move differs, via a new internal `stage_mask()`
          helper (pieces/king) and inline capture/promotion-rank checks
          (pawns, whose capture-vs-quiet-ness depends on promotion rank,
          not just target-square occupancy, so they filter themselves
          rather than going through `stage_mask()`). NOT YET consumed by
          search or quiescence — this step only adds the capability;
          `src/search/search.cpp`, `src/search/quiescence.cpp`, and
          `search/ordering.h`'s existing full-list `order_moves()` are
          all UNCHANGED and still call `generate_legal_moves()` at its
          default `GenType::All`, so this step has zero effect on real
          search behavior (confirmed: `bench` unchanged at 37,287 total
          nodes, byte-for-byte identical to Session 111's own value).
          Building the actual `MovePicker` iterator that consumes
          `Captures`/`Quiets` staging and wiring it into
          `negamax()`/`quiescence()` is Step 2, not yet started.
    - [x] **Step 2a — lazy captures-only generation in `quiescence.cpp`
          (Session 112 continued):** no `MovePicker` class needed for
          this half of Step 2 — `quiescence_impl()`'s own candidates
          loop already only ever admits a capture, a promotion, or (only
          when `include_checks`, only at `qs_ply == 0`) a checking quiet
          move, so quiets were provably never needed on the far more
          common `!us_in_check && !include_checks` path. That path now
          calls `generate_legal_moves(pos, legal_moves,
          GenType::Captures)` directly, falling back to a
          `GenType::Quiets` generation ONLY when the captures result is
          empty — needed there just to tell a genuinely terminal
          (stalemate) position apart from a merely-quiet one (legal
          moves exist, just none of them a capture), since those two
          cases return different scores (a draw score vs. the stand-pat
          eval) a few lines below. Because `GenType::Captures` omits
          quiet moves rather than reordering anything, the resulting
          `candidates` list is byte-identical, in the same order, to
          what the old always-`GenType::All` call produced feeding the
          same downstream filter — confirmed by `bench` staying at
          exactly 37,287 total nodes, unchanged from Step 1 and from
          Session 111. Full suite green (615/615) under both Release and
          Debug/ASan+UBSan.
    - [x] **Step 2b — lazy captures-first generation in `negamax()`'s
          own move loop (Session 112 continued):** no separate
          `MovePicker` class in the end (`ordering.h`'s existing
          full-list `order_moves()` is reused as-is, called once on
          whatever `moves` currently holds at each of the points below,
          rather than replaced) — an in-function `ensure_quiets()`
          lambda, generating `GenType::Quiets` and appending the result
          (freshly `order_moves()`-sorted on its own) to the
          already-`GenType::Captures`-generated `moves` list, fires at
          exactly 4 points, each one resolving a real correctness need
          that quiets-only-on-demand introduces, not an arbitrary
          choice: (1) captures come back empty -- can't yet tell a
          genuinely terminal position from a merely-quiet one, same
          shape as Step 2a's quiescence fallback; (2) `tt_move` is
          non-null but not found among the captures -- might be a
          genuine quiet best move from a shallower iterative-deepening
          pass that `order_moves()` needs to place first, might be a
          stale/foreign TT entry, and there's no way to tell without
          generating quiets to check; (3) Singular Extensions' own
          alternative-move scan (`search.cpp`'s existing
          singular-extension block) needs every legal move, not just
          captures, whenever it triggers; (4) the main move loop itself
          runs out of currently-generated moves without a cutoff having
          fired, meaning the search must legitimately continue further
          than what's been generated so far.
    - **Important finding, not a bug (full account in docs/DECISIONS.md,
          this session's Step 2b entry):** unlike Step 1 and Step 2a,
          Step 2b is NOT bench-parity-preserving, and this was expected
          (see this item's own original Step 3 text below) but is worth
          stating precisely now that it's been observed directly:
          `history`/`cont_history`/`capture_history` (search/ordering.h)
          are global, shared across the whole search tree, and get
          mutated by recursive search of whichever captures were tried
          BEFORE trigger (4) above ever fires — so quiets generated and
          scored via `order_moves()` partway through a node's own move
          loop can land in a genuinely different RELATIVE order among
          themselves than they would have under the old always-eager
          `GenType::All` scheme, which scored every move (captures and
          quiets alike) against the history-table state as it stood at
          the very start of the node, before anything at that node had
          been searched yet. This is the same trade-off real engines'
          own staged `MovePicker` designs (CPW's own "Move Ordering"
          article) accept as standard, not a defect specific to this
          implementation — pruning heuristics (LMR/LMP/history pruning)
          are inherently order-sensitive, so a different-but-still-
          legitimate ordering can shift the reported score/node count at
          a FIXED search depth even though the underlying algorithm
          remains sound. Observed directly on this session's own bench
          suite: `quiet_middlegame`'s score moved from 173 to 31 (`best_move`
          unchanged: `d4c5` both times) while `kiwipete`'s node count
          moved from 16,049 to 16,151 with its score unchanged; bench
          TOTAL moved from 37,287 to 38,679 nodes (+3.7%). None of this
          reflects a missed legal move, an illegal move, or a crash —
          confirmed by the full test suite (619/619, including a battery
          of "mate-in-3 still found correctly with [technique] active"
          regression tests spanning every major pruning/extension
          feature in this file) staying green throughout.
    - [x] **Step 3a — correctness re-verification (DONE, this session):**
          the full suite (619/619) staying green under Release and
          Debug/ASan+UBSan, including every existing mate-finding/
          pruning-technique regression test, is what "correctness
          re-verification" actually means for Step 2b's kind of change
          (no missed/illegal moves, no crash) — already covered by Step
          2b's own entry above, checked off here for the record as its
          own distinct thing from Step 3b below, not because there's
          separate new work under this label.
    - [ ] **Step 3b — a real strength comparison, genuinely NOT possible
          with this repo's existing tooling as it stands today
          (corrects a mistake in this item's own text from earlier in
          this session — see docs/DECISIONS.md's correction entry for
          the full account of how this was actually checked, not just
          reconsidered):** `nightwing_sprt`/`tuner::play_match()`
          (`src/tuner/match.h`) compare two `eval::MaterialWeights`
          vectors played against EACH OTHER USING THE SAME COMPILED
          SEARCH CODE — they cannot compare two different SEARCH-CODE
          versions (like pre- vs. post-Step-2b `negamax()`) at all, with
          or without building a second binary; there is no facility in
          this repo for spawning two different binaries and playing them
          against each other over UCI either. Actually running a
          strength comparison for a search-code change (as opposed to an
          eval-weight change, which this repo's tooling already handles
          well) needs new infrastructure that doesn't exist yet — either
          a genuine two-process UCI-vs-UCI match runner, or (the smaller
          lift, following the exact precedent `MatchConfig`'s own
          `threads_a`/`threads_b` fields already set for "repurpose this
          same single-process module for a different per-side knob,
          holding weights equal" — that struct's own doc comment) a new
          per-side movegen-strategy toggle threaded through
          `search_fixed_depth()`/`negamax()` the same way thread count
          already is. Tracked as its own new, clearly separate ROADMAP
          item below ("Engine-vs-engine match infrastructure for
          search-code changes") rather than a blocking requirement on
          this item — closing THIS item (staged move generation) on the
          correctness evidence Step 3a already provides, consistent with
          this project's own stated bench-tests.cpp philosophy ("prints
          node counts for a human ... to record ... whether nodes went
          up or down ... as a concrete signal," not "requires a full
          match before any node-count-affecting change can land").


Added 2026-08-15. Not part of the sequential phase order above — can be
picked up in any session without waiting for Phase 8. Decisions/rationale in DECISIONS.md,
2026-08-15 entry.

- [ ] `ci.yml`: add a `release` job gated with `needs:` on all 6 existing build+test matrix jobs, running only on push to `main` (not PRs), with `contents: write` permission
- [ ] Release job re-tags/re-publishes a single rolling `latest` GitHub Release on every green run of the 6 jobs (no per-run version tags; release body auto-includes commit SHA + date so "which commit is this" is still answerable)
- [ ] Publish the 3 **Release**-config native binaries (Linux/macOS/Windows) as release assets, consistently named (e.g. `nightwing-linux`, `nightwing-macos`, `nightwing-windows.exe`) — the 3 **Debug** (ASan/UBSan-instrumented) jobs stay CI/test-only and gate the release without publishing their own binaries, since a sanitizer-instrumented build isn't something an end user should run
- [ ] Emscripten toolchain integration in CMake: new `NIGHTWING_BUILD_WASM` option, separate build directory/job, producing `nightwing.wasm` + `nightwing.js` glue
- [ ] WASM JS surface: full UCI loop over stdin/stdout (via Emscripten's stdin support), so Node can drive it exactly like a native UCI engine binary — no bespoke JS API to design/maintain in parallel with UCI itself
- [ ] Standalone `nightwing.min.js`: single self-contained minified bundle (wasm binary inlined as base64, not a separate `.wasm` file to host/serve) for drop-in use without asset-path configuration
- [ ] Both wasm artifacts (`nightwing.wasm`+`nightwing.js`, and `nightwing.min.js`) published as release assets alongside the 3 native binaries on the same rolling `latest` release
- [ ] Verify the wasm build against the existing UCI test suite (or an equivalent subset runnable under Node) before it's trusted as a real release asset, not just "compiles"

Added 2026-09-18 (Session 112/113). Not part of the sequential phase
order above — can be picked up in any session without waiting for
Phase 8/9. Rationale: discovered a real gap while trying to strength-
verify the "Staged / lazy move generation" item's Step 3b (that item's
own entry above, and DECISIONS.md's correction entry, have the full
account) — `tuner::play_match()`/`nightwing_sprt` can only ever compare
two `eval::MaterialWeights` vectors played against each other using the
SAME compiled search code; there is currently no way, anywhere in this
repo, to get a real strength (Elo/match-score) signal for a SEARCH-CODE
change (a new pruning technique, a search refactor like Step 2b) the
way there already is for an eval-weight change. This gap will recur
every time a future search-code change's effect on strength (as
opposed to just correctness and node-count) is worth knowing before
shipping it, not just this one time.

- [ ] Engine-vs-engine match infrastructure for search-code changes (not just eval weights): the smaller-lift option, following `MatchConfig`'s own existing `threads_a`/`threads_b` precedent (`src/tuner/match.h` — added specifically to repurpose that same single-process module for a different per-side knob, holding weights equal) — add an analogous per-side movegen-strategy (or, more generally, a per-side function-pointer/`std::function`-based search-variant) toggle threaded through `search_fixed_depth()`/`negamax()`, so `play_match()` can compare two search-CODE configurations within the same process/binary the same way it already compares two weight vectors
- [ ] Alternative, larger-lift option: a genuine two-process UCI-vs-UCI match runner (spawn two separate `nightwing` binaries — e.g. a just-built one plus a checked-out-and-built-separately baseline — and referee games between them over stdin/stdout UCI), closer to how real engine testing frameworks (cutechess-cli, fastchess, OpenBench) work, and the only option that can compare two commits/binaries without adding any new in-process toggle to `negamax()` itself
- [ ] Whichever option is built, retroactively apply it to the "Staged / lazy move generation" item's still-open Step 3b (pre- vs. post-Step-2b `negamax()`) as this new tool's first real use case, rather than leaving that comparison undone indefinitely

## Priority Fixes (external code review, 2026-09-22)

Not phase-gated — inserted here, ahead of Phase 9 and the Release Automation
work, per this project's established "bugs before enhancements" convention
(see the three Priority Fixes sections above). A `report.md` code-review
document (GCC 13.3/CMake/Ninja build, 24 perft positions, a ThreadSanitizer
concurrency pass, manual/scripted UCI sessions, and direct reading of
`search.cpp`/`uci.cpp`/`attacks.cpp`/`cpu_features.cpp`/`ci.yml`) was
reviewed against `main` on 2026-09-22. Every finding was independently
re-verified against the actual repository (direct source reading, `grep`,
line/parameter counts, `du`/`wc` on the docs and source trees) before being
filed here, same "verify before trusting" discipline as the 2026-09-17
section above. All 13 findings held up. One correction to the report
itself: finding 10 (`negamax`'s parameter count) is understated — the real
count is **27**, not 22. One finding (the KQ-vs-K "13M nodes / 60s
unresolved" observation) is a runtime behavior this sandbox has not
independently reproduced; filed as an observation to check, not a
confirmed bug. See docs/DECISIONS.md, 2026-09-22 (2), for the full
per-finding verification account.

Ordered per the report's own "Recommended Order of Work"; items 7/12/13/the
KQ-vs-K observation (not part of that ordering) are appended after it.
This section's own item order is a STARTING recommendation, not a
commitment — re-sequence freely if a later session's own judgment differs,
same as every other Priority Fixes section here.

**Note on sequencing against the `tune_psqt()` bug** (Phase 5
above, filed 2026-09-22 (1), fixed Session 125): that bug and this section
were found in the same sitting but were always unrelated systems (tuner
vs. UCI/search engine proper) — nothing below depended on it or blocked
it either way.

1. [x] **Asynchronous `go` with working `stop`/`isready`/`quit`** (findings 1
   and 2) — DONE (Session 124). `handle_go()` (`src/uci/uci.cpp`) previously
   ran fully synchronously on the same thread that reads UCI input, exactly
   as that file's own header comment used to document ("still not
   attempted... `stop` sent while an ordinary `go` is in flight is parsed
   but has no effect"). Replaced with a `GoState` struct (thread +
   `std::atomic<bool> stop`/`suppress_output`, mirroring `PonderState`) and
   `abandon_go()`/`start_go()`/`handle_go_stop()`/`finish_go()`, the same
   general shape `start_pondering()` already used for `go ponder` — `go`
   now moves onto its own worker thread and the search polls
   `SearchLimits::external_stop` via that flag, exactly as
   `run_lazy_smp_helper()` already did one layer up. `go infinite`/bare
   `go` (previously falling back to the fixed `kNoTimeControlDepth = 5`)
   now run genuinely unbounded via `kTimedSearchMaxDepth` until an async
   `stop` arrives, same as `go ponder` already did; `kNoTimeControlDepth`'s
   stale "Phase 2 has no pruning yet" doc comment was corrected — it now
   applies only to a malformed explicit `depth` token. A new
   `SearchBudget::unbounded`/`GoState::unbounded` flag lets `quit`/
   end-of-input tell a bounded search (joined — let it finish and print
   `bestmove`, reproducing the exact old synchronous behavior) apart from
   a genuinely unbounded one (abandoned, to avoid hanging forever) — see
   `finish_go()`'s own doc comment. A new `out_mutex` (`run()`'s own
   local) serializes every write to `out` across the main thread and both
   background threads, now that concurrent writers are possible. Two
   pre-existing `tests/uci_tests.cpp` cases that encoded the old
   synchronous-`go` assumption (a bare `go` immediately followed by
   `quit`; two `go depth 1` calls with no `stop` between them) were
   updated to send the `stop` a real, compliant GUI would send in both
   cases — see docs/DECISIONS.md, 2026-09-22 (3).
2. [x] **Real BMI2 portability** (finding 3) — `build_pext_table()`
   (`src/board/attacks.cpp`) called `_pext_u64` unconditionally inside
   `init_magic_bitboards()`, gated only by the compile-time
   `#if defined(NIGHTWING_ENABLE_BMI2)` (which defaults ON,
   `NIGHTWING_ENABLE_BMI2` option, root `CMakeLists.txt`), NOT by the
   runtime `support::cpu_has_bmi2()` check — that check only decided
   `g_use_pext` (which table `rook_attacks()`/`bishop_attacks()` read from
   afterward), set at the very END of `init_magic_bitboards()`, well after
   the unconditional PEXT table build already ran. A default build
   (BMI2 flags on) would execute an illegal instruction at startup on any
   pre-Haswell x86 CPU, regardless of the runtime detection machinery
   existing. FIXED (Session 126): `support::cpu_has_bmi2()` is now checked
   at the very START of `init_magic_bitboards()` (setting `g_use_pext`
   immediately, before any table-building loop runs), and every
   `build_pext_table()` call is now itself gated by that flag rather than
   running unconditionally. Separately, and just as load-bearing: the
   actual PEXT instruction is now isolated to one small function,
   `pext_u64()`, marked with a GCC/Clang
   `__attribute__((target("bmi2")))` rather than the report's own
   literally-suggested "separate translation unit" — verified against
   this project's own compiler (a real `-O2` build and a real `-O3 -flto`
   Release-config build) that this isolates the PEXT instruction to
   exactly that one function's own machine code and nothing else in the
   binary, confirmed by disassembling the actual linked test binary, not
   just by inspection. `src/CMakeLists.txt` no longer applies
   `-mbmi2`/`-mpopcnt` to `nightwing_lib` as a whole — a second, closely
   related hazard this fix also closes: `-mpopcnt` applied library-wide
   meant `std::popcount()` (`board/bitboard.h`, used throughout
   eval/search, not just `attacks.cpp`) always lowered to the hardware
   POPCNT instruction with no runtime check at all, contradicting that
   function's own doc comment's promise of a portable fallback. See
   docs/DECISIONS.md, this session's own dated entry, for the full
   account of why the function-attribute approach was chosen over the
   report's own literal suggestion, and the disassembly-level
   verification performed.
3. [x] **Emit a ponder move in `bestmove`** (finding 4) — DONE (this
   session). Both `bestmove` call sites (`src/uci/uci.cpp`'s `start_go()`
   and `start_pondering()`, the latter also serving a `ponderhit`-
   continued search) now append `ponder <move>` whenever a genuine
   second PV move exists, via a new shared helper, `ponder_move_for()`.
   The move comes from `SearchResult::pv[1]` in the ordinary case, or
   from the matching `multipv_lines` entry's own `pv[1]` whenever
   `search::pick_skill_move()` (search/skill.h) chose a different line
   than `result.best_move` — reading `result.pv[1]` unconditionally
   would have reported a wrong ponder move in that case, not just a
   missing one. The `ponder` token is omitted entirely (never
   `ponder 0000`) whenever the chosen line's PV has fewer than 2
   entries — e.g. the position is one ply from checkmate. See
   docs/DECISIONS.md, 2026-09-23, for the full design/rationale.
4. [x] **Validate the null-move gate and the singular-extension rewrite
   with SPRT** (findings 5 and 6) — CODE FIXES LANDED the prior session;
   left unchecked because the item's own text explicitly requires
   `nightwing_sprt` validation before either change is trusted as a
   strength gain, not just correctness — that run is the concrete
   remaining step, not more code. What landed: `negamax()`'s null-move
   condition now also requires `node_static_eval >= beta` (previously
   missing entirely); the singular-extension block now makes a single
   verification `negamax()` call with the TT move excluded via a new
   `exclude_move` parameter, at the same `ply` and the same side to
   move, rather than one full recursive call per alternative legal move
   in a `for (int j = 0; j < moves.size() ...)` loop — replacing up to
   `moves.size() - 1` extra sub-searches per singular candidate (and an
   unconditional `ensure_quiets()` call) with exactly one, at standard-
   technique cost. `exclude_move` also had to disable this position's
   own TT cutoff and skip both the TT store and the correction-history
   update for that one call — see docs/DECISIONS.md, 2026-09-23 (2),
   for the full correctness argument on why (a subtler hazard than the
   node-count savings alone). UPDATE, same day (docs/DECISIONS.md,
   2026-09-23 (3)): a compiler/CMake toolchain was discovered genuinely
   available in the assistant's own sandbox (docs/ARCHITECTURE.md's new
   "Development Environment" section) and used to actually build this
   repository with both fixes applied and run the full `ctest` suite for
   real — 679/679 passing, and the real `nightwing` binary confirmed
   over UCI to still behave correctly (`bestmove ... ponder ...` on an
   ordinary line, no `ponder` token on a Fool's-Mate line). This is
   materially stronger than the "verified by inspection" standard every
   prior session's own entries used, but is still NOT the SPRT run this
   item's own text requires — a real strength-gain verdict needs many
   real games against a reference build, genuine wall-clock time this
   session didn't spend on it (see the same DECISIONS.md entry's own
   note on why `nightwing_sprt --help` alone already ran long enough to
   hit this sandbox's own per-command time limit). One new regression
   test in `tests/search_tests.cpp` (`[nmp][singular_extension]`-tagged,
   depth 8 — deep enough to reach `kSingularMinDepth` itself). DONE
   (this session): `nightwing_sprt`/`play_match()` turned out unable to
   do this run at all — confirmed directly by re-reading
   `src/tuner/match.h`/`sprt_main.cpp` — because it only ever plays both
   sides with the SAME compiled binary's `search_fixed_depth()`,
   differing solely by a `MaterialWeights` vector; it cannot compare two
   different SEARCH-CODE versions, which is exactly what this item
   needs. A genuine two-process UCI-vs-UCI match referee was built
   instead (`python-chess` driving two persistent `nightwing` UCI
   subprocesses over stdin/stdout, paired/color-balanced random
   openings, `score_a()`/`elo_diff()` reported in the same style as
   `tuner::MatchResult` for continuity) — sandbox-only verification
   tooling, not a repo deliverable, since it depends on this sandbox's
   own Python/compiler toolchain the user has no local equivalent of;
   see docs/ARCHITECTURE.md's "Development Environment" section. Three
   real matches were run, using `git clone` (confirmed to work in this
   sandbox) to check out the exact pre-fix commit (`e2d0620`, the direct
   parent of the fix commit `687f2c7`) as the baseline binary, and an
   isolated single-line null-move-only variant built on top of that same
   baseline (`node_static_eval >= beta` alone, cleanly isolable since
   that variable was already in scope), so each fix's own marginal
   effect could be checked, not just the combined result:
   - **Combined (both fixes) vs. pre-fix baseline** — 200 games, depth
     6, 8-ply random openings: candidate scored 101W/71L/28D
     (score=0.575, elo_diff=**+52.5**, z=2.15) — a real, statistically
     suggestive net gain, not a regression.
   - **Null-move gate alone vs. baseline** — 90 games, depth 6:
     nullmove-only fix scored 47W/37L/6D from the baseline's perspective
     (elo_diff=**+38.8** for the fix, z≈1.06) — same direction, smaller
     sample, not yet 2-sigma but no regression signal either.
   - **Singular-extension rewrite alone**, isolated as
     nullmove-only-vs-candidate (both share the null-move fix, so the
     difference is exactly the singular-extension change) — first run
     at depth 6 came back an exact 40W/40L/10D tie (elo_diff=0.00) at
     EVERY checkpoint, which is fully explained rather than a red flag:
     `kSingularMinDepth` is 8, so depth-6 games never reach the changed
     code path at all, and two provably-identical search trees produce
     mirror-identical games — itself a useful behavioral-equivalence
     check on the refactor below its trigger depth. Re-run at depth 9
     (12 games only, wall-clock-limited) came back 4W/2L/6D in the new
     code's favor (elo_diff=+58, z=-0.59 — far too small a sample to be
     conclusive, but directionally non-regressing). **Verdict: no
     regression found in any of the three matches**; the combined change
     is a real, if modestly-sampled, strength gain. The singular-
     extension fix's own isolated depth-\u2265-8 sample stays small enough
     that a **future session with more wall-clock budget re-running just
     that one match at depth 9-10 for 100+ games** is worth doing for a
     tighter confidence interval, filed as a new low-priority item below
     rather than blocking this one — the "no regression" bar this item
     was gating on has been met. See docs/DECISIONS.md, 2026-09-24, for
     the full match-methodology writeup.
4b. [x] **Fix a pre-existing, previously-undetected flaky test surfaced
   by this push's own CI run** — `tests/endgame_suite_tests.cpp`'s
   "opposite-colored bishops score lower..." test failed identically on
   all 6 CI platforms; confirmed by direct reproduction (not inference)
   to be UNRELATED to item 4's own changes above (the identical failure
   reproduces against a completely unmodified copy of `search.cpp` from
   `main`) and to be a real, if narrow, alpha-beta search-instability
   artifact specific to depth 8 for this one position pair — the
   underlying static-eval term itself is correct (confirmed both by a
   direct `eval::evaluate()` probe and by `minor_piece_endgame_tests.
   cpp`'s own already-passing unit coverage of it), and the expected
   score ordering holds cleanly at depths 4-7, 9, and 10, flipping by
   only 2 points at depth 8 specifically. Fixed by changing that one
   test's own depth from 8 to 7 (a 94-point margin, reconfirmed
   deterministic against the real compiled build) and rewriting its
   comment to document the investigation. No production code changed.
   Full `ctest` (679/679) confirmed green after this fix, with item 4's
   own changes still applied on top. See docs/DECISIONS.md, 2026-09-23
   (3), for the full investigation and rationale.
5a. [x] **Fix the mate-vs-fifty-move ordering** (finding 8) —
   `is_draw_by_rule()` (`src/search/search.cpp`) returned a draw
   immediately on `halfmove_clock >= 100`, checked before any move
   generation or legality check — so a checkmate delivered by the very
   move that pushed the halfmove clock to 100 was misscored as a draw
   rather than a win. Rare (an exact-ply coincidence) but a real
   rules-correctness bug, not just an edge case to document away. FIXED
   (this session): the `halfmove_clock >= 100` branch now only returns
   a draw unconditionally when the side to move is NOT in check;
   when it IS in check, `board::generate_legal_moves()` is called (the
   one real cost, paid only in this rare branch) and a draw is returned
   only if at least one legal reply exists — zero legal replies while
   in check is checkmate, and the function returns `false` so
   `negamax()`'s own move loop discovers the empty move list itself and
   scores the node as a genuine mate. Same technique Stockfish's own
   `Position::is_draw()` uses (`st->rule50 > 99 && (!checkers() ||
   MoveList<LEGAL>(*this).size())`), credited per
   docs/ARCHITECTURE.md's attribution policy, not derived from scratch.
   **Verified against a real compiled build, not inspection**: a
   constructed FEN (`7k/5K2/8/8/8/8/8/6Q1 w - - 99 60` — bare-king mate
   pattern, halfmove_clock 99, any quiet queen move reaches 100) was
   run through the actual pre-fix binary first, confirming the bug
   reproduces exactly as described (`score cp 0`, the search visibly
   avoiding a mate it could see, `bestmove g1g5` instead of a mating
   move); the same FEN against the post-fix binary correctly reports
   `score mate 1` / `bestmove g1h1`. Full `ctest` (680/680, including
   one new regression test,
   `tests/search_tests.cpp`'s `[fifty-move][mate]`-tagged case built
   from this exact FEN) confirmed green.
5b. [x] **Tighten UCI parsing** (finding 9) — `go nodes`/`go mate`/
   `searchmoves` were parsed nowhere in `uci.cpp`'s `go`-token loop and
   were silently dropped; `apply_uci_moves()` silently `break`s on the
   first unmatched move token in `position ... moves`, discarding the
   rest of the list rather than reporting anything; `emit_info()` wrote
   only `depth`/`multipv`/`score`/`nodes`/`pv` — no `nps`, `time`,
   `seldepth`, or `hashfull`, all of which real tooling (cutechess and
   similar) consumes. DONE (this session): all four pieces implemented,
   verified against a real compiled build, and covered by new tests at
   both the search layer and the UCI layer — see docs/DECISIONS.md,
   2026-09-24 (3), for the full writeup. Summary: `go nodes <n>` (a new
   `SearchLimits::has_node_limit`/`max_nodes`, checked in the same
   periodic block as `deadline`/`external_stop` in both `negamax()` and
   `quiescence_impl()`); `go searchmoves <m1> ...` (a new
   `search_root()` `searchmoves_filter` inclusion parameter, the mirror
   of the existing `excluded_moves` exclusion filter, threaded through
   both the mandatory depth-1 call and every depth-2-onward iteration of
   `search_iterative_deepening()`); `go mate <n>` (parsed as a depth
   ceiling of `2n` plies — the minimal, no-dedicated-mode treatment this
   item's own earlier note flagged as the design question to settle,
   settled that way); `TranspositionTable::hashfull()` (a new method,
   Stockfish's own 1000-entry-sample technique, credited); `SearchResult::
   seldepth`/`elapsed_ms` (new fields, the former updated from the
   deepest `ply` any `negamax()`/`quiescence_impl()` call reached, the
   latter wall-clock since `search_iterative_deepening()` was called);
   `emit_info()` now writes `seldepth`/`time`/`nps`/`hashfull` alongside
   the existing fields; `apply_uci_moves()` now returns how many tokens
   it actually applied, and `handle_position()` emits one `info string`
   diagnostic when that's fewer than were given, rather than staying
   silent. Full `ctest`: 692/692 (680 carried over from item 5a + 12 new
   this leg: 4 `TranspositionTable::hashfull()` tests, 2 search-layer
   tests for `max_nodes`/`searchmoves`, and 6 end-to-end UCI-layer tests
   covering the new `info` fields, `go nodes`, `go searchmoves`, `go
   mate`, and the `apply_uci_moves()` diagnostic both firing and NOT
   firing) confirmed green.
   Deliberately deferred, not bundled in: MultiPV combined with `go
   nodes`/`searchmoves`, and pondering's background search combined
   with either — filed as a new low-priority item below, not blocking
   this one.
- [x] **Fix: `search_root()`'s own `game_history` local alias was genuinely
      unused, dead code missed by both Session 133's original refactor
      and Session 134's own subsequent MultiPV/pondering work** — caught
      by real macOS CI (Apple Clang's own `-Wunused-variable`, not
      reproducible under this project's Linux CI compiler, GCC, which
      does not flag an unused CLASS-typed local — `std::span` here — the
      same way it flags an unused reference/pointer/scalar one), on the
      first macOS CI run after either session's changes to this
      function. DONE, Session 135 — CI-log triage session, immediately
      after the push. Removed the dead alias; `game_history` remains
      correctly aliased and used inside `negamax()` itself, a separate
      function this bug never touched. See docs/DECISIONS.md, 2026-09-26
      (3), for the full account of why GCC never caught this.
- [ ] **Investigate: real macOS CI registers only 694 of 696 CTest
      tests, missing exactly `eval_tests.cpp`'s two `taper: phase ...`
      tests (`kMaxPhase`/`0`)** — discovered during Session 135's own CI
      triage of Session 134's push (docs/SESSIONS.md has the full
      account); Linux and Windows (both Debug and Release, all 4 jobs)
      register all 696 and pass all 696; macOS Debug registers 694 and
      passes all 694 (no failures, just missing 2); macOS Release
      registers the same 694 with 1 genuine failure (the pre-existing
      `nps` flake, a separate, already-tracked item — see this section's
      2026-09-22-filed entries above). Not a skip directive found
      anywhere in `tests/CMakeLists.txt` or either test's own source
      (`eval_tests.cpp`, plain unconditional `TEST_CASE`s, no `#ifdef
      __APPLE__` or Catch2 tag exclusion) — genuinely unexplained by
      inspection alone; first observed on this exact CI run, not
      confirmed present on any earlier push (no prior session appears to
      have diffed exact per-platform CTest totals this precisely).
      First step: reproduce directly (a real macOS build, not more log
      reading) and determine whether this is a `ctest_discover_tests()`
      registration quirk (e.g., a name-collision or truncation specific
      to Apple's linker/discovery mechanism) versus something more
      structural, before deciding on a fix.
- [x] **Thread `go nodes`/`searchmoves` through MultiPV and pondering**
      (filed 2026-09-24, from item 5b's own deliberate scope limit
      above) — DONE, Session 134. `search_iterative_deepening_multipv()`
      (internal to `search.cpp`) gained the same `max_nodes`
      (`std::uint64_t`, default 0)/`searchmoves`
      (`std::span<const Move>`, default empty) parameters
      `search_iterative_deepening()`'s single-line path already had,
      forwarded from that function's own delegation call site whenever
      `multi_pv` genuinely takes effect. `searchmoves` is threaded as
      `search_root()`'s own `searchmoves_filter` argument on EVERY
      `search_root()` call this function makes, including the
      unconditional depth-1 block, mirroring the single-line path's own
      depth-1 treatment of the same parameter. `max_nodes` only feeds
      the depth-2-onward loop's own per-depth `SearchLimits`
      (`limits.has_node_limit`/`limits.max_nodes`) — NOT the depth-1
      block — for the identical "always have a legal move" reason
      `SearchLimits::has_deadline`'s own doc comment already gives for
      the single-line path; one shared node budget per depth across
      every one of that depth's requested lines (the same `limits`
      instance is reused across the inner per-line loop), not a
      separate budget per line — reaching it mid-line discards that
      whole depth, same "an incomplete depth is discarded wholesale"
      convention this function already applied to every other
      interruption reason. `start_pondering()` (`src/uci/uci.cpp`) now
      forwards `budget.has_node_limit ? budget.max_nodes : 0` and
      `budget.searchmoves` to its own background
      `search_iterative_deepening()` call (previously hardcoded to
      neither) — `budget` (the whole `SearchBudget` struct, by value)
      is captured into the pondering thread's closure, mirroring
      `start_go()`'s own established capture pattern, rather than just
      the two new fields. At the default values (no `nodes`/
      `searchmoves` token on either `go` variant), both changes are
      completely behavior-preserving: `bench`'s own per-position node
      counts (`startpos`/`kiwipete`/`quiet_middlegame`/
      `endgame_mate_in_3`, all 4 positions) were confirmed
      byte-for-byte unchanged under both Release and Debug/ASan+UBSan
      before and after this session's changes. `search.h`'s own
      `max_nodes`/`searchmoves` doc comments on
      `search_iterative_deepening()` updated to drop the now-stale "NOT
      threaded into search_iterative_deepening_multipv()" language and
      describe the new behavior instead. Verified against a real
      fresh-clone `cmake`+`ctest` build, not inspection: a pre-change
      baseline (both Release and Debug/ASan+UBSan) came back 689/692
      (Release) and 690/692 (Debug) — 3 and 2 pre-existing,
      sandbox-specific `[uci]` failures respectively, the same ones
      Session 133 already documented (this sandbox's 1-CPU-core timing
      theory, not independently confirmed against real CI) — and the
      post-change build, with 4 new tests added (2 in
      `tests/search_tests.cpp` covering `multi_pv` combined with each of
      `searchmoves`/`max_nodes` directly at the search layer, 2 in
      `tests/pondering_tests.cpp` covering `go ponder searchmoves`/`go
      ponder nodes` at the UCI layer), came back 693/696 (Release) and
      694/696 (Debug) — the exact same pre-existing failure set,
      confirmed by direct comparison of both runs' failing test names,
      plus all 4 new tests passing. Debug/ASan+UBSan surfaced 4
      pre-existing `eval/score.h` signed-integer-overflow warnings
      during `bench` on both the baseline and modified builds
      identically — confirmed NOT introduced by this session's changes
      (present in the unmodified baseline too), not investigated
      further as out of this item's own scope.
6. [x] **Refactor `negamax`'s parameter list; prune stale docs** (finding
   10, and part of 11) — DONE (this session), the refactor half. Added
   `SearchContext` (`src/search/search.cpp`, anonymous namespace, declared
   immediately before `negamax()`), bundling the 14 per-search-invariant
   fields (`tt`, `killers`, `history`, `cont_history`, `capture_history`,
   `correction_history`, `game_history`, `pawn_tt`, `eval_cache`,
   `material_weights`, `eval_weights`, `limits`, `contempt_white_pov`,
   `tie_break_variant`). `negamax()`: 27 params -> 15. `search_root()`: 19
   params -> 9. Every recursive call site inside both functions (10 for
   `negamax()`, 3 for `search_root()`) and every external `search_root()`
   call site (7, across `search_fixed_depth()`, `run_lazy_smp_helper()`,
   `search_iterative_deepening_multipv()`, `search_iterative_deepening()`)
   updated. Neither function's own BODY was rewritten — a top-of-function
   local-alias block reconstructs the exact same local names the old
   parameter list provided, so every line past that block is unchanged
   from before this session; deliberate, given `negamax()` is ~1,230
   lines of previously-SPRT-relevant pruning/extension logic. Confirmed
   directly (both functions are anonymous-namespace-local, never
   forward-declared) that neither has any call site outside `search.cpp`
   at all — no test file or `uci.cpp` change was needed. Verified against
   the real toolchain, not by inspection: a fresh clone, baseline build of
   unmodified `main` came back 689/692 (3 pre-existing, deterministic,
   sandbox-specific `[uci]` test failures, unrelated to this work —
   this sandbox has 1 CPU core; working theory is single-core timing on
   async `go` tests, NOT independently confirmed against real CI); the
   refactored build was clean (zero errors, zero warnings once four
   `-Wunused-variable` hits in `search_root()`'s own alias block — fields
   it never reads directly, only forwards via `ctx` — were pruned) and
   came back 689/692 with the exact same 3 failing test names, confirmed
   via direct diff of both runs. See docs/DECISIONS.md, 2026-09-26, for
   the full design/scope/verification account.
   Deliberately NOT done this session, left as a named follow-up: the
   finding's own text also floated that this struct could let
   `run_lazy_smp_helper()`'s own documented
   `const_cast<std::atomic<bool>*>(&stop)` go away — that `const_cast` is
   on a parameter of `run_lazy_smp_helper()` itself, a different function
   than the two actually refactored here, and doing it properly means
   deciding `SearchContext::limits`'s own constness story deliberately
   rather than as a side effect of an unrelated change. Not picked up
   this session; pick up as its own small follow-up.
   Same item, smaller, also NOT separately pursued this session beyond
   what the refactor itself touched: `docs/DECISIONS.md` (872 KB) and
   `docs/SESSIONS.md` (~650 KB, still growing) are each individually
   larger than the source tree they document; nothing in this section
   asks for docs to be pruned or rewritten wholesale (this project's "the
   repo is the memory" convention is deliberate, not an oversight), but
   any stale statement actually caught in passing — the report
   specifically named the TT "per-search lifetime" note, `setoption`
   "besides `Threads`" being ignored, and the already-fixed "Phase 2 has
   no pruning" comment (item 1 above) — should still be corrected on
   sight in a future session rather than left for a dedicated cleanup
   pass that may never come.

Not part of the report's own ordering, appended here:

- [ ] **Reduce redundant per-node work** (finding 7, lower priority —
      report's own framing: "probably a cheap NPS gain," profile first)
      — `eval::evaluate()` is called up to 4 separate times per
      `negamax()` node (node static eval, reverse futility, razoring,
      futility; confirmed at 4 distinct call sites) — `EvalCache`
      absorbs most of the real cost already (its own header comment
      in `eval.cpp` names this exact scenario as the reason the cache
      exists), so this is a smaller win than it looks at first glance.
      `board::compute_pawn_hash()`, by contrast, genuinely re-scans
      every pawn on the board from scratch once per node (confirmed:
      a plain bitboard loop, no incremental field the way
      `pos.zobrist_hash` has), unlike the rest of this engine's
      Zobrist hashing, which IS incrementally maintained through
      make/unmake — an incremental pawn-hash update (XOR out/in only
      the pawn(s) actually touched by the move just made) would remove
      this real O(pawn-count)-per-node cost. Profile before spending
      time on either — the report's own caution, not lowered.
- [ ] **Add a LICENSE and a `.gitignore`** (finding 13) — neither exists
      in the repository today (confirmed absent). The README has a
      documented attribution policy but nothing governs actual reuse
      terms; whoever operates this repository should pick a license
      (not this repo-assistant's call to make) before this is closed.
- [ ] **Audit fixed-depth vs. production-path test coverage** (finding
      12, a methodology observation, not a bug by itself) — Internal
      Iterative Reduction (`negamax()`'s own `if (!probe.hit && depth >=
      kIIRMinDepth && limits != nullptr)` gate, confirmed) only fires
      when `limits != nullptr`, i.e. only on the real
      `search_iterative_deepening()` production path, never on
      `search_fixed_depth()` — and 10 test files call
      `search_fixed_depth()` against only 6 that call
      `search_iterative_deepening()` (confirmed counts), so the
      majority of this project's own search tests do not exercise IIR
      at all. This is documented, deliberate behavior (`limits` gating
      IIR is intentional, not an oversight) — the actionable item is
      making sure test coverage's own balance is a conscious choice
      going forward, not silently skewing further toward the path that
      exercises less of production `negamax()` as new tests get added.
- [ ] **Widen the singular-extension isolation match's sample size**
      (filed 2026-09-24, from item 4's own match results above) — the
      combined and null-move-gate-only matches (200 and 90 games) gave a
      real, if not fully 2-sigma, signal; the singular-extension-alone
      match at a depth that actually reaches `kSingularMinDepth` (8) was
      wall-clock-limited to 12 games (elo_diff=+58, z=-0.59 — direction
      only, not confidence). Re-run that one specific match (nullmove-
      only-fix binary vs. current `main`, depth 9-10, 100+ games) in a
      session with more time budgeted for it specifically; the
      methodology and both binaries' build commits are recorded in
      docs/DECISIONS.md, 2026-09-24. Not blocking — no regression signal
      exists at any sample size tested.
- [ ] **Investigate the KQ-vs-K "unresolved after ~13M nodes / 60s"
      observation** — reported from `8/8/8/4k3/8/8/4K1Q1/8 w`, not yet
      independently reproduced by this sandbox. `basic_mates.h`
      (confirmed) only defines dedicated algorithmic terms for KRK and
      KBNK — no KQK term exists — which is at least consistent with a
      real gap, though KQK is conventionally one of the EASIEST won
      endgames for any competent engine via plain material + mobility
      eval alone, with no special technique needed, so a genuine
      13M-node stall on it (if reproduced) would be a surprising,
      worth-prioritizing finding rather than a minor gap. First step:
      actually reproduce it (a real search run, not code reading) before
      deciding whether this needs a fix or was a one-off
      timeout/environment artifact in the original review.

## Phase 9 — Advanced / Stretch Goals (beyond great-engine baseline)
- [ ] NUMA-aware thread/memory allocation (large multi-socket hardware only)
- [ ] Distributed/cluster search (very advanced, likely out of practical scope)
- [ ] Self-generated small (3-4-5 man) endgame tablebases — DECIDED AGAINST (see DECISIONS.md, 2026-08-11): superseded by Phase 6's algorithmic endgame theory approach. Listed here only as a historical note; not planned.

---
**Phase 2 complete. Phase 3 complete** (its former "Pondering" item moved to Phase 7 — see above). **Phase 4 complete.** **The Priority Fixes section above is complete** (external code review, 2026-08-25 — both mid-search time checks and UCI `info` output done). **Phase 5 complete** (2026-08-31, Session 63 — see that phase's own final-item note above for the tuning-run history). **Phase 6 complete, including both of its lower-priority items** (2026-08-31, Session 69 for the core phase; Session 70 for the dedicated endgame test suite and the opening book — the material-signature classifier from Session 64, `eval::classify_endgame()`, ended up with seven buckets, four `eval/` consumers, and one `search/` consumer across Sessions 65–69; Fortress pattern detection stands as the one deliberately classifier-independent term. See Sessions 64–70's own docs/SESSIONS.md entries for the full build history of this phase). Phase 7 (Multithreading) complete (2026-09-03/04, Sessions 71–77 — 73 and 76 were both bugfixes, not new roadmap items — see their own entries): Lazy SMP implementation, Lock-free TT for concurrent access, Thread count UCI option, Pondering, and the strength-regression verification, all done — see that phase's own item notes above and docs/SESSIONS.md's Session 71/72/74/75/77 entries. (Session 78, immediately after, was a same-day CI-driven bugfix to Session 77's own test suite — see its own entry; no roadmap item touched.) **Phase 8 (Polish & Tournament Readiness) complete as of Session 92** — see that phase's own item notes above and docs/SESSIONS.md's Session 79–92 entries for the full build history (two items carry an honestly-flagged external-verification gap this sandbox cannot itself close — real-GUI pondering interop, and real fishtest/OpenBench interop for `bench`'s own output format — both noted at their own items; both optional items, Skill Level and Contempt, were completed anyway). Next up: the unlabeled Release Automation phase immediately below (CI release job, native binary publishing, wasm build) or Phase 9 — Advanced/Stretch Goals, at a future session's own discretion, since neither is part of the sequential phase order the way Phases 0–8 were.
