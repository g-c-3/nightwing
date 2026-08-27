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
- [ ] Space evaluation
- [ ] Threats evaluation (hanging/attacked pieces, pieces attacked by pawns)
- [ ] King tropism (piece proximity to enemy king in the attack)
- [ ] Trapped piece penalties
- [ ] Tempo bonus (small fixed bonus for side to move)
- [ ] Material imbalance table (e.g. bishop pair / knight pair value shifts with pawn count, per Stockfish-classic style)
- [ ] Eval cache (optional performance optimization, separate from TT)
- [ ] All terms as named tunable constants (per DECISIONS.md)
- [ ] Texel/SPSA tuner module (self-play data generation + gradient descent)
- [ ] Tuned weights committed, before/after strength comparison logged

## Phase 6 — Endgame Knowledge (algorithmic theory, no tablebases)

Goal: exact-feeling play in common endgames and graceful, generalizing play everywhere else — never a blind cliff the way tablebases have one past their piece-count ceiling. No self-generated bitbases (decision: algorithmic generalization only, see DECISIONS.md).

- [ ] Material-signature classifier: detect endgame material buckets at each node, route to specialized endgame reasoning when matched
- [ ] King+pawn theory: opposition, key squares, corresponding squares, the rule of the square, generalized to any K+P configuration (not case-tabulated)
- [ ] Rook endgame patterns: Lucena position recognition (winning technique), Philidor position recognition (drawing technique), Vancura position, rook behind passed pawn heuristic
- [ ] Minor piece endgames: wrong-bishop-corner draw detection, opposite-colored bishop fortress/drawish-tendency eval adjustment, knight vs. bishop endings weighted by pawn structure (open vs. closed)
- [ ] Fortress pattern detection (structural, not tabulated) — recognize blocked/closed positions where material advantage can't be converted
- [ ] Zugzwang-aware search shaping: bias search (e.g. reduce/skip null-move pruning) in positions flagged as zugzwang-prone by material signature, so the search doesn't miss zugzwang the way naive null-move can
- [ ] Hand-built base heuristics carried over: KPK, KRK, KBNK exact-play rules (algorithmic, not lookup-table), draw detection refinement (insufficient material)
- [ ] Dedicated endgame test suite: curated known-tricky K+P and rook-ending positions (canonical sources e.g. Fine's *Basic Chess Endings*) with known-correct results, run in CI to catch algorithmic-rule misjudgments that pure perft/search regression tests wouldn't surface. Kept as its own test file, separate from perft/search/eval regression tests (per Testing Policy in ARCHITECTURE.md)
- [ ] (Optional, low priority) small curated opening book

## Phase 7 — Multithreading
- [ ] Lazy SMP implementation
- [ ] Lock-free TT for concurrent access
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
**Phase 2 complete. Phase 3 complete** (its former "Pondering" item moved to Phase 7 — see above). **Phase 4 complete.** **The Priority Fixes section above is complete** (external code review, 2026-08-25 — both mid-search time checks and UCI `info` output done). **Current: Phase 5 — Eval Expansion & Tuning** — Mobility eval, King safety, Bishop pair/rook-on-open-or-semi-open-file/rook-on-7th-rank, and Knight outposts are all done; next task: Space evaluation.
