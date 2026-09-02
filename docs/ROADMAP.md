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
- [ ] Thread count UCI option
- [ ] Pondering — search side: handle `go ponder`, continue as real search on `ponderhit`, discard and restart on `stop`+actual move. Moved here from Phase 3 (2026-08-21): pondering needs real concurrent search (a background search thread, an async-checkable stop signal, and the UCI loop able to read `ponderhit`/`stop` while a search is in flight) — infrastructure this phase is building anyway for Lazy SMP. Building it ad-hoc in Phase 3, before that infrastructure exists, risked either a half-working implementation or throwaway work once this phase's real thread pool landed. See docs/DECISIONS.md for the full reasoning.
- [ ] Verify no strength regression vs. single-threaded at equal single-thread depth

## Phase 8 — Polish & Tournament Readiness
- [ ] Full UCI option set (Hash size, Threads, MultiPV, Ponder, Move Overhead, etc.) — note: `Hash` size specifically is also what makes the current per-`go`-call TT/pawn-hash reallocation (docs/DECISIONS.md, 2026-08-25 (8), an external code review finding) worth revisiting; no separate item needed, tracked here.
- [ ] Pondering — protocol side: `Ponder` UCI option exposed, verified working against GUIs that ponder (Arena, CuteChess, etc.)
- [ ] Time management (search time allocation per move, increment handling, best-move-stability-based extension) — this is the FULL allocation-strategy feature; the narrower, more urgent "does the search actually stop mid-iteration when the clock says to" gap is tracked separately, above Phase 5, as a Priority Fix (docs/DECISIONS.md, 2026-08-25 (8)) rather than waiting for this item's own scheduled phase.
- [ ] `bench` command — fixed-position node/time benchmark for fishtest/OpenBench-style regression testing
- [ ] Profile-Guided Optimization (PGO) build pipeline (generate profile via `bench`/self-play, rebuild optimized)
- [ ] TT prefetch verified to actually overlap memory latency with useful work (profiled, not assumed)
- [ ] SPRT testing setup/process for validating future changes
- [ ] Skill level / strength limiting (optional, for practice/handicap play)
- [ ] Contempt / draw score adjustment (optional)
- [ ] README, build instructions, engine info (name/author via `uci`)
- [ ] wasm build / GUI packaging — superseded by the "Release & Packaging Infrastructure" section below (2026-08-15); tracked there instead of here.

## Release & Packaging Infrastructure (parallel track — not phase-gated, pick up whenever)
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
**Phase 2 complete. Phase 3 complete** (its former "Pondering" item moved to Phase 7 — see above). **Phase 4 complete.** **The Priority Fixes section above is complete** (external code review, 2026-08-25 — both mid-search time checks and UCI `info` output done). **Phase 5 complete** (2026-08-31, Session 63 — see that phase's own final-item note above for the tuning-run history). **Phase 6 complete, including both of its lower-priority items** (2026-08-31, Session 69 for the core phase; Session 70 for the dedicated endgame test suite and the opening book — the material-signature classifier from Session 64, `eval::classify_endgame()`, ended up with seven buckets, four `eval/` consumers, and one `search/` consumer across Sessions 65–69; Fortress pattern detection stands as the one deliberately classifier-independent term. See Sessions 64–70's own docs/SESSIONS.md entries for the full build history of this phase). Phase 7 (Multithreading) underway: its first two items, Lazy SMP implementation and Lock-free TT for concurrent access, are complete as of Sessions 71–72 (2026-09-03) — see that phase's own item notes above and docs/SESSIONS.md's Session 71/72 entries. Next up: Phase 7's third item, Thread count UCI option.
