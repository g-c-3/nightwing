# Nightwing — Roadmap

Phases are sequential unless noted. Check off tasks as completed; add new ones as they're discovered. Each session works from the top incomplete item unless told otherwise.

## Phase 0 — Project Setup
- [ ] CMake project skeleton, C++20, builds empty `main.cpp`
- [ ] GitHub Actions CI: build matrix (Linux/macOS/Windows), runs `ctest`
- [ ] Catch2 integrated as test framework
- [ ] `docs/` seeded (this file, DECISIONS.md, SESSION_LOG.md, ARCHITECTURE.md)

## Phase 1 — Board Representation & Move Generation
- [ ] Bitboard primitives (set/clear/pop bit, popcount, bitscan)
- [ ] Magic bitboard generation for rook/bishop attacks
- [ ] Board state struct (piece bitboards, side to move, castling rights, en passant, halfmove clock)
- [ ] Zobrist hashing
- [ ] `init_masks() → init_magic_bitboards() → init_zobrist_keys()` startup sequence wired up
- [ ] Fully legal move generation (pins, checks, castling, en passant, promotions)
- [ ] Make/unmake move
- [ ] Perft test suite passing to standard reference depths (startpos, Kiwipete, etc.)

## Phase 2 — Minimal Search + Eval (get something playing)
- [ ] Material-only + PSQT eval (tapered mg/eg)
- [ ] Plain alpha-beta search, fixed depth
- [ ] Iterative deepening
- [ ] Basic UCI loop (`uci`, `isready`, `position`, `go depth N`, `stop`)
- [ ] Engine can play a full legal game against itself via UCI

## Phase 3 — Core Search Strengthening
- [ ] PVS (Principal Variation Search)
- [ ] Transposition table (Zobrist-keyed, depth/age replacement)
- [ ] Move ordering: TT move, MVV-LVA captures, killer moves, history heuristic
- [ ] Aspiration windows
- [ ] Quiescence search (captures + checks, with SEE pruning)

## Phase 4 — Pruning & Extensions
- [ ] Null-move pruning
- [ ] Late move reductions (LMR)
- [ ] Futility pruning
- [ ] Razoring
- [ ] Check extensions
- [ ] Singular extensions
- [ ] Regression bench: node-count/strength tracked in SESSION_LOG.md per change

## Phase 5 — Eval Expansion & Tuning
- [ ] Mobility eval
- [ ] King safety (pawn shield, open files near king, attacker weighting)
- [ ] Pawn structure (passed, isolated, doubled, backward, connected)
- [ ] Bishop pair, rook on open/semi-open file, other standard positional terms
- [ ] All terms as named tunable constants (per DECISIONS.md)
- [ ] Texel/SPSA tuner module (self-play data generation + gradient descent)
- [ ] Tuned weights committed, before/after strength comparison logged

## Phase 6 — Endgame Knowledge
- [ ] Hand-built endgame heuristics: KPK, KRK, KBNK, opposition, wrong-bishop-corner
- [ ] Draw detection refinement (insufficient material, fortress patterns where feasible)
- [ ] (Optional, low priority) small curated opening book

## Phase 7 — Multithreading
- [ ] Lazy SMP implementation
- [ ] Lock-free TT for concurrent access
- [ ] Thread count UCI option
- [ ] Verify no strength regression vs. single-threaded at equal single-thread depth

## Phase 8 — Polish & Tournament Readiness
- [ ] Full UCI option set (Hash size, Threads, MultiPV, etc.)
- [ ] Time management (search time allocation per move, increment handling)
- [ ] SPRT testing setup/process for validating future changes
- [ ] README, build instructions, engine info (name/author via `uci`)
- [ ] wasm build or GUI packaging (optional, revisit if wanted)

---
**Current phase: 0 — Project Setup.** Next task: CMake project skeleton.
