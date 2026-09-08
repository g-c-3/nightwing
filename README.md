# Nightwing

Nightwing is a from-scratch, classical (hand-crafted evaluation) UCI chess
engine written in C++20. It plays standard chess only — no variants.

**Deliberately, permanently NOT included:** NNUE, any neural network of any
kind, and Syzygy or any other external endgame tablebase. This is a hard
design constraint of the project, not a placeholder for later — Nightwing's
evaluation is entirely hand-crafted and tunable, and its endgame knowledge
is entirely algorithmic (opposition, key squares, Lucena/Philidor
recognition, and similar theory implemented as real geometric rules, not
lookup tables or a bitbase).

## Features

- **Board representation:** 64-bit bitboards, magic bitboards for sliding
  pieces (with a BMI2/PEXT fast path and a portable fallback), Zobrist
  hashing, fully legal move generation (pins, checks, castling, en
  passant, promotions).
- **Search:** Principal Variation Search (PVS) over iterative deepening,
  aspiration windows, a lock-free transposition table, and the standard
  set of modern pruning techniques and extensions — null-move pruning,
  late move reductions/pruning, futility pruning, razoring, ProbCut,
  history and continuation-history pruning, check and singular
  extensions. Lazy SMP for multithreading.
- **Evaluation:** hand-crafted (HCE) — material, piece-square tables,
  mobility, king safety, pawn structure, space, threats, king tropism,
  trapped pieces, tempo, and a material-imbalance table — every term a
  named, tunable constant. Algorithmic endgame knowledge on top: King+Pawn
  theory (rule of the square, key squares, opposition), rook-endgame
  patterns (Lucena, Philidor, Tarrasch's Rule), minor-piece-endgame and
  fortress detection, and basic mate technique (KRK, KBNK) — all
  structural rules, not tables.
- **Tuning and testing infrastructure:** a self-play data generator and a
  Texel/SPSA-style gradient-descent tuner (`nightwing_selfplay`/
  `nightwing_tune`), a head-to-head match harness (`nightwing_match`), and
  a proper SPRT (Sequential Probability Ratio Test) tool for validating
  whether a change is a real improvement rather than noise
  (`nightwing_sprt`) — see `docs/ARCHITECTURE.md` for how these fit
  together.
- **UCI protocol:** `Hash`, `Threads`, `MultiPV`, `Move Overhead`,
  `Ponder`, `Skill Level` (0–20, for practice/handicap play), and
  `Contempt` (draw-score adjustment) are all implemented, along with full
  pondering (`go ponder`/`ponderhit`), a small curated opening book, and a
  `bench` command for fishtest/OpenBench-style regression benchmarking.

## Building

Requirements: CMake 3.20+, and a C++20 compiler (GCC or Clang recommended
— see `docs/DECISIONS.md` for the current, narrower state of MSVC
support).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

This produces the UCI engine binary at `build/src/nightwing`. It speaks
the standard UCI protocol and works with any UCI-compatible GUI (Arena,
CuteChess, etc.), or can be driven directly over stdin/stdout.

To run the benchmark used for regression tracking:

```
build/src/nightwing bench
```

### Running the test suite

The Catch2-based test suite is built by default (`NIGHTWING_BUILD_TESTS`,
`ON` unless set otherwise) and run via CTest:

```
cmake -B build -DNIGHTWING_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GitHub Actions (`.github/workflows/ci.yml`) builds and tests every push
across Linux/macOS/Windows automatically; a person building locally
doesn't need to do anything beyond the two commands above.

A few other CMake options exist for special-purpose builds — Profile-
Guided Optimization (`NIGHTWING_PGO_PHASE`) and a portable non-BMI2
fallback (`NIGHTWING_ENABLE_BMI2=OFF`) — both documented in comments at
the top of the root `CMakeLists.txt`.

## Repository layout

```
nightwing/
├── docs/
│   ├── ROADMAP.md        — what's done, what's next, phase by phase
│   ├── ARCHITECTURE.md   — module layout, tech stack, testing policy
│   ├── DECISIONS.md      — every architectural decision, with rationale
│   └── SESSIONS.md       — a session-by-session development log
├── src/                  — engine source (board/, search/, eval/, uci/, tuner/, book/)
├── tests/                — the Catch2 test suite
└── .github/workflows/    — CI (build+test on every push; PGO/tuning/SPRT
                             pipelines available via manual dispatch)
```

`docs/` is this project's own memory across development sessions — every
non-trivial design decision is recorded there with its reasoning, not just
its outcome. Anyone picking up this codebase for the first time should
start with `docs/ARCHITECTURE.md` for the technical layout and
`docs/ROADMAP.md` for current status.

## Attribution

Nightwing is written from scratch — no code is copied from any other
engine. Where a well-known technique or idea (from Stockfish's classical
codebase, Ethereal, or the Chess Programming Wiki) informs an
implementation, the corresponding source file's own comments credit it
specifically, consistent with this project's own attribution policy
(`docs/ARCHITECTURE.md`).
