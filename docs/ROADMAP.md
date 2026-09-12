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
- [ ] Eval: pawn storms (enemy pawns advancing on the king's shelter,
      the aggressive complement to the existing defensive king-safety
      terms) and a connected-passed-pawns bonus (a mutually-defending
      passed pair worth more than two individually-scored passers).
      Five further concrete, low-risk gaps identified by cross-
      referencing a bucketed CPW/Stockfish-classical eval-feature
      review against `src/eval/` (docs/DECISIONS.md, 2026-09-08 (5)),
      each cheap to detect via existing attack-bitboard/pawn-structure
      machinery and, per that review's own framing, standard sub-checks
      within already-implemented top-level buckets rather than new
      buckets of their own:
    - [ ] Candidate passed pawns — a pawn not yet passed but positioned
          to become passed after a likely, forceable pawn trade.
    - [ ] Outside passed pawns — a passer on the side of the board away
          from the pawn majority; a specific, cheap-to-detect sub-case
          of the existing passed-pawn bucket.
    - [ ] Pawn islands — a simple count of contiguous same-color pawn
          groups; correlates well with structural weakness.
    - [ ] Back-rank weakness — a concrete, well-defined pattern (an open
          back rank with the king stuck on it).
    - [ ] Overloaded pieces — a piece defending two or more things it
          cannot actually defend if any one of them is taken/attacked;
          detectable via the same attack-bitboard machinery the existing
          Threats bucket already uses.
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

## NPS / Raw Speed (parallel track — not phase-gated, external review 2026-09-08)

Distinct axis from the Priority Fixes section above: that section is entirely about
searching *fewer nodes* for the same answer (pruning/reductions/ordering); this
section is about searching the nodes it does visit *faster* (raw nodes-per-second).
Both matter and are pursued independently — closing the gap with stronger classical
engines needs both, not one traded off against the other (docs/DECISIONS.md,
2026-09-08 (5)).

- [ ] **Profile first (do this before any of the items below):** run
      `perf record`/`perf report` (or `valgrind --tool=callgrind`) on a
      real search and confirm where the cycles actually go before
      committing to any rewrite below. `eval::evaluate()`'s own existing
      code comments already flag the from-scratch-every-node computation
      below as a deliberate, PROFILER-FREE trade-off ("no profiled hot
      path to justify the accumulator's extra bookkeeping yet") — the
      right next step is measurement, not proceeding on assumption alone,
      per this project's own already-stated instinct on the matter.
- [ ] Incremental evaluation (material + PSQT as a running accumulator,
      updated with a small delta on `make_move`/`unmake_move`, instead
      of recomputed from zero on every call to `eval::evaluate()`) — if
      profiling confirms this is in fact the dominant per-node cost (as
      it typically is in classical engines), this is the single biggest
      lever on this list.
- [ ] Lazy evaluation / early-exit on cheap terms: compute material +
      PSQT first inside `eval::evaluate()`; if that alone already clears
      alpha/beta by a comfortable margin, skip the remaining expensive
      terms (mobility, king safety, threats, space, pawn structure) and
      return early, rather than always computing every term regardless
      of whether the cheap ones already settled the question.
- [ ] Staged / lazy move generation: a `MovePicker`-style iterator that
      generates captures first and only generates quiet moves if the
      search gets past the captures without a cutoff, instead of always
      generating the entire legal move list upfront. A structural change
      (a real iterator type, not just a flat generate-then-sort list),
      not a small patch — scope accordingly when picked up.


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

## Phase 9 — Advanced / Stretch Goals (beyond great-engine baseline)
- [ ] NUMA-aware thread/memory allocation (large multi-socket hardware only)
- [ ] Distributed/cluster search (very advanced, likely out of practical scope)
- [ ] Self-generated small (3-4-5 man) endgame tablebases — DECIDED AGAINST (see DECISIONS.md, 2026-08-11): superseded by Phase 6's algorithmic endgame theory approach. Listed here only as a historical note; not planned.

---
**Phase 2 complete. Phase 3 complete** (its former "Pondering" item moved to Phase 7 — see above). **Phase 4 complete.** **The Priority Fixes section above is complete** (external code review, 2026-08-25 — both mid-search time checks and UCI `info` output done). **Phase 5 complete** (2026-08-31, Session 63 — see that phase's own final-item note above for the tuning-run history). **Phase 6 complete, including both of its lower-priority items** (2026-08-31, Session 69 for the core phase; Session 70 for the dedicated endgame test suite and the opening book — the material-signature classifier from Session 64, `eval::classify_endgame()`, ended up with seven buckets, four `eval/` consumers, and one `search/` consumer across Sessions 65–69; Fortress pattern detection stands as the one deliberately classifier-independent term. See Sessions 64–70's own docs/SESSIONS.md entries for the full build history of this phase). Phase 7 (Multithreading) complete (2026-09-03/04, Sessions 71–77 — 73 and 76 were both bugfixes, not new roadmap items — see their own entries): Lazy SMP implementation, Lock-free TT for concurrent access, Thread count UCI option, Pondering, and the strength-regression verification, all done — see that phase's own item notes above and docs/SESSIONS.md's Session 71/72/74/75/77 entries. (Session 78, immediately after, was a same-day CI-driven bugfix to Session 77's own test suite — see its own entry; no roadmap item touched.) **Phase 8 (Polish & Tournament Readiness) complete as of Session 92** — see that phase's own item notes above and docs/SESSIONS.md's Session 79–92 entries for the full build history (two items carry an honestly-flagged external-verification gap this sandbox cannot itself close — real-GUI pondering interop, and real fishtest/OpenBench interop for `bench`'s own output format — both noted at their own items; both optional items, Skill Level and Contempt, were completed anyway). Next up: the unlabeled Release Automation phase immediately below (CI release job, native binary publishing, wasm build) or Phase 9 — Advanced/Stretch Goals, at a future session's own discretion, since neither is part of the sequential phase order the way Phases 0–8 were.
