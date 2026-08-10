# Nightwing — Architecture

Standard chess engine, C++20, classical (non-NNUE, non-Syzygy) design philosophy: get everything else — search, pruning, tuned HCE, endgame heuristics — as strong as possible without a neural net or tablebase dependency.

## Tech Stack

| Area | Choice |
|---|---|
| Language | C++20 |
| Build | CMake (min 3.20) |
| CI | GitHub Actions — build + test on every push, all platforms via matrix (Linux/macOS/Windows) |
| Test framework | Catch2 (header-only, easy single-file include, good for perft-style parametrized tests) |
| Board representation | Bitboards (uint64_t), magic bitboards for sliding piece attacks (rook/bishop) |
| Move generation | Fully legal move gen (no pseudo-legal + filter) via pin/check masks, to keep search code simple |
| Search | PVS (Principal Variation Search) over alpha-beta, iterative deepening, aspiration windows |
| Pruning/extensions | Null-move pruning, late move reductions (LMR), futility pruning, razoring, check extensions, singular extensions (later phase) |
| Move ordering | TT move → captures (MVV-LVA + SEE) → killers → history heuristic → counter-moves |
| Eval | Hand-crafted eval (HCE): material, piece-square tables (tapered mg/eg), mobility, king safety, pawn structure (passed/isolated/doubled/backward), hand-built endgame heuristics |
| Eval tuning | Texel tuning (gradient descent on eval weights vs. game outcomes) — added once eval has enough terms (Phase 5) |
| Transposition table | Single global TT, power-of-2 sized, Zobrist hashing, age + depth replacement scheme; lock-free once multithreaded |
| Multithreading | Lazy SMP (Phase 7) — deferred until single-threaded search is perft/bench verified stable |
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
│   ├── king_safety.cpp/.h
│   └── endgame.cpp/.h          # KPK/KRK/etc. heuristics
├── uci/
│   └── uci.cpp/.h              # UCI protocol loop
└── main.cpp
tests/
├── perft_tests.cpp
├── eval_tests.cpp
└── search_tests.cpp
```

## Startup Sequence (mandatory order)

`init_masks() → init_magic_bitboards() → init_zobrist_keys()`

## Attribution Policy

Since this project deliberately avoids NNUE/tablebases and leans on classical technique, borrowed ideas or code (from Stockfish's pre-NNUE classical eval, Ethereal, the Chess Programming Wiki, etc.) must be credited inline in the source file's header comment, naming the source and what was adapted.

## Testing Policy

- Every movegen change must pass perft to known depth/node-count references (standard perft suite: startpos, Kiwipete, etc.)
- Every search change must pass existing search regression tests (no more than X% node count regression without justification — track in DECISIONS.md when this happens)
- `ctest` must be fully green before any file is considered committable
