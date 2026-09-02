# Nightwing — Architecture

Standard chess engine, C++20, classical (non-NNUE, non-Syzygy) design philosophy: get everything else — search, pruning, tuned HCE, endgame heuristics — as strong as possible without a neural net or tablebase dependency.

## Tech Stack

| Area | Choice |
|---|---|
| Language | C++20 |
| Build | CMake (min 3.20) |
| CI | GitHub Actions — build + test on every push, all platforms via matrix (Linux/macOS/Windows) |
| Test framework | Catch2 (header-only, easy single-file include, good for perft-style parametrized tests) |
| Board representation | Bitboards (uint64_t). Sliding attacks via BMI2 PEXT bitboards on hardware that supports it (Haswell+/Zen3+), with magic bitboards as the portable fallback (compile-time or runtime CPU feature detection) |
| Move generation | Fully legal move gen (no pseudo-legal + filter) via pin/check masks, to keep search code simple |
| Search | PVS (Principal Variation Search) over alpha-beta, iterative deepening, aspiration windows |
| Pruning/extensions | Null-move pruning, late move reductions (LMR), futility pruning, razoring, check extensions, singular extensions (later phase) |
| Move ordering | TT move → captures (MVV-LVA + SEE) → killers → history heuristic → counter-moves |
| Eval | Hand-crafted eval (HCE): material, piece-square tables (tapered mg/eg), mobility, king safety, pawn structure (passed/isolated/doubled/backward), hand-built endgame heuristics |
| Eval tuning | Texel tuning (gradient descent on eval weights vs. game outcomes) — added once eval has enough terms (Phase 5) |
| Transposition table | Currently one private TT per top-level search call, not yet the eventual single persistent global (see `src/search/tt.h`'s LIFETIME NOTE — tied to the still-open UCI `Hash` option, ROADMAP.md Phase 8); power-of-2 sized, Zobrist hashing, age + depth replacement scheme; cache-line-aligned entries (16 bytes, 4 entries per 64-byte line), explicit prefetch on probe. Genuinely lock-free for concurrent Lazy SMP use as of Session 72 — CPW "Shared Hash Table" XOR-checksum technique, two atomic 64-bit words per entry, no locks (`src/search/tt.h`'s THREAD-SAFETY NOTE) |
| Multithreading | Lazy SMP (Phase 7) — helper threads sharing the TT (`search_iterative_deepening()`'s `num_threads` parameter, Session 71) and the TT's lock-free redesign (Session 72) are both landed; a UCI `Threads` option is still an open, separate Phase 7 item |
| Protocol | UCI (Universal Chess Interface) |
| No NNUE | Hard constraint — do not add |
| No tablebases | Hard constraint — do not add Syzygy or any external TB; hand-built endgame heuristics substitute |

## Module Layout (planned)

```
src/
├── board/
│   ├── bitboard.cpp/.h        # bitboard primitives, magic bitboards
│   ├── board.cpp/.h           # board state, make/unmake move
│   ├── movegen.cpp/.h         # legal move generation
│   └── zobrist.cpp/.h         # hashing
├── search/
│   ├── search.cpp/.h          # PVS, iterative deepening
│   ├── tt.cpp/.h               # transposition table
│   ├── ordering.cpp/.h        # move ordering, killers, history
│   └── pruning.cpp/.h         # null-move, LMR, futility, razoring
├── eval/
│   ├── eval.cpp/.h             # top-level eval, tapered eval
│   ├── psqt.cpp/.h             # piece-square tables
│   ├── pawns.cpp/.h            # pawn structure eval
│   ├── pawn_tt.cpp/.h          # pawn hash table (caches pawns.cpp results)
│   ├── mobility.cpp/.h         # mobility eval
│   ├── king_safety.cpp/.h
│   └── endgame.cpp/.h          # KPK/KRK/etc. heuristics
├── uci/
│   └── uci.cpp/.h              # UCI protocol loop
└── main.cpp
tests/
├── perft_tests.cpp
├── eval_tests.cpp
├── search_tests.cpp
└── endgame_tests.cpp     # curated known-result K+P/rook-ending positions, Phase 6
```

## Startup Sequence (mandatory order)

`init_masks() → init_magic_bitboards() → init_zobrist_keys()`

## Performance Engineering

Speed (nodes/sec) and search efficiency (useful nodes/sec — good pruning/ordering) are both first-class goals. Node count reduction from smart search beats raw NPS, but both compound, so neither is sacrificed for the other.

### Build & Compiler
- Release builds: `-O3`, LTO (link-time optimization) enabled
- Profile-Guided Optimization (PGO): generate profile via `bench`/self-play data, rebuild with `-fprofile-use` — added once the engine is feature-complete enough to have a stable hot path worth profiling (Phase 8)
- CPU feature detection at build/runtime: separate build targets (or runtime dispatch) for BMI2 (PEXT/PDEP), POPCNT, and a portable baseline — never assume BMI2 is present without a fallback
- No exceptions or RTTI in hot search/eval/movegen paths (`-fno-exceptions -fno-rtti` where practical) — error handling in UCI/IO layers only
- Avoid `std::vector` allocation in the search hot path — move lists and search stack use fixed-size arrays (max legal moves in a chess position is bounded, ~218) sized at compile time

### Hot-Path Code Practices
- Search, movegen, and eval functions marked for inlining where it helps (small helpers), profiled rather than guessed
- No virtual dispatch / polymorphism in search or eval — direct calls only, resolved at compile time via templates where behavior needs to vary (e.g., templated search function on node type: PV/non-PV, in-check/not)
- Branch-heavy logic (move ordering scores, pruning conditions) prefers lookup tables and branchless bit tricks over chained conditionals where it's a measurable win — not a blanket rule, verified per case
- Move generation is staged where it helps ordering + pruning cut nodes early: captures/promotions first, then killers, then quiets — avoids generating and scoring moves that get pruned anyway

### Incremental Updates
- Zobrist hash updated incrementally on make/unmake, never recomputed from scratch
- Material and PSQT eval terms updated incrementally on make/unmake (standard "eval on the fly" accumulator pattern), not recomputed from the full board each node
- Pawn structure eval reuses the pawn hash table (see Tech Stack) so unchanged pawn structure across nodes isn't re-evaluated

### Memory & Cache
- TT and pawn hash table sized as power-of-2 for fast index masking (no modulo)
- TT entries cache-line aligned; explicit prefetch of the TT entry for a position issued as early as possible in the search node (overlaps memory latency with move generation/ordering work)
- Search stack (per-ply state: killers, static eval, etc.) is a flat pre-allocated array indexed by ply, not heap-allocated per node

### Benchmarking Discipline
- `bench` command (Phase 8) is the standard fixed-position, fixed-depth benchmark — every performance-sensitive change is checked against it for both NPS and node count before merging
- Node-count regressions on `bench` require justification in DECISIONS.md (per Testing Policy below) — a "faster but searches worse" change is not automatically an improvement, and vice versa
- Multithreaded (Lazy SMP, Phase 7) scaling is validated against single-threaded `bench` node counts to catch thread contention or TT synchronization overhead early

## Attribution Policy

Since this project deliberately avoids NNUE/tablebases and leans on classical technique, borrowed ideas or code (from Stockfish's pre-NNUE classical eval, Ethereal, the Chess Programming Wiki, etc.) must be credited inline in the source file's header comment, naming the source and what was adapted.

## Testing Policy

- Every movegen change must pass perft to known depth/node-count references (standard perft suite: startpos, Kiwipete, etc.)
- Every search change must pass existing search regression tests (no more than X% node count regression without justification — track in DECISIONS.md when this happens)
- Every Phase 6 endgame-theory change must pass the dedicated endgame test suite (`endgame_tests.cpp`) — curated known-correct K+P and rook-ending positions, kept separate from perft/search/eval regression tests since it validates correctness of algorithmic judgment, not node counts or bulk legality
- `ctest` must be fully green before any file is considered committable
