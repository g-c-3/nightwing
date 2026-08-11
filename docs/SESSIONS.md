# Nightwing — Sessions

Newest entry at top.

---

## 2026-08-11 — Session 3: Phase 1 board representation complete (bitboards → Zobrist → startup wiring)

**What was built:** Full board-representation layer, verified locally end-to-end with a real C++20 toolchain (g++ 13, real BMI2 hardware) at every step rather than only reasoned about.

- `src/board/bitboard.h/.cpp` — `Bitboard`/`Square` types, set/clear/test/toggle bit, `popcount`, `bitscan_forward/reverse`, `pop_lsb` (all `constexpr`, intrinsic-backed via `<bit>`), file/rank/square coordinate helpers, ASCII debug printer.
- `src/board/attacks.h/.cpp` — magic bitboard rook/bishop/queen attack generation. Portable path: from-scratch sparse-random magic search (masks + attacks-on-the-fly + carry-rippler subset enumeration), no copied magic-number tables. BMI2 fast path: PEXT-indexed tables (no search needed), runtime-dispatched via `support::cpu_has_bmi2()` so a BMI2 binary still runs correctly on older hardware. Test-only hooks force the PEXT path for direct verification.
- `src/support/rng.h` — shared deterministic xorshift64* PRNG, extracted from `attacks.cpp` for reuse by `zobrist.cpp`.
- `src/board/board.h/.cpp` — `Position` struct (piece bitboards + mailbox + side to move + castling rights + en passant + move counters + zobrist hash), exactly 192 bytes (3 cache lines, asserted). `start_position()` factory, ASCII printer.
- `src/board/zobrist.h/.cpp` — key generation (piece-square, side-to-move, castling, en-passant-file) and from-scratch `compute_hash()`.
- `src/board/masks.h/.cpp` — knight/king/pawn attack tables, `file_mask()`/`rank_mask()`.
- `src/main.cpp` — wires the mandatory startup sequence (`init_masks()` → `init_magic_bitboards()` → `init_zobrist_keys()`), prints start-position hash + board as a smoke check.
- Test suite grew from 2 to 49 test cases (portable: 47, BMI2: 49) — `bitboard_tests.cpp`, `attacks_tests.cpp`, `board_tests.cpp`, `zobrist_tests.cpp`, `masks_tests.cpp`.

**Bugs fixed:**
- `src/CMakeLists.txt`: `NIGHTWING_ENABLE_BMI2` was being macro-defined even on ARM (latent bug — would've broken the macOS build again the moment BMI2-gated C++ code existed, since `<immintrin.h>`/`__builtin_cpu_supports` don't exist there). Fixed by gating the macro itself to x86/x86_64, not just the `-mbmi2` compile flags.
- `src/board/attacks.cpp` (portable/non-BMI2 build): `g_use_pext` was unused-variable-warning under `-Wextra` since it's only referenced inside `#if NIGHTWING_ENABLE_BMI2` blocks. Fixed by moving its declaration inside that same `#if`.
- `src/board/board.h`: `std::array<Piece, 64> piece_on{}` value-initialized every square to `Piece::WhitePawn` (enumerator `0`) instead of `Piece::None`, since `None` isn't 0 in the encoding — caught by `board_tests.cpp`'s mailbox/bitboard consistency check, which showed `start_position()` rendering 5 phantom ranks of white pawns. Fixed with an explicit fill via immediately-invoked lambda rather than relying on zero as an implicit sentinel.

**Decisions made:** Zobrist en passant hashing uses the simplified file-only scheme (XORs the target square's file key whenever `en_passant_square` is set, regardless of whether a capturing pawn is actually present) — logged in DECISIONS.md with rationale/alternatives.

**Next session start point:** Begin the biggest remaining Phase 1 item — fully legal move generation (pins, checks, castling, en passant, promotions) in a new `src/board/movegen.h/.cpp`, with a fixed-size stack-array move list (no heap allocation, per ARCHITECTURE.md). This will need pin/check detection helpers built on the existing `rook_attacks()`/`bishop_attacks()`/`knight_attacks()`/`king_attacks()`/`pawn_attacks()` primitives. Say "Continue" or "Start" to proceed. Perft test suite (`tests/perft_tests.cpp`) is the natural follow-on once movegen exists — don't build it standalone before movegen is in place.

---

## 2026-08-11 — Session 2: Phase 0 complete — build/CI/test skeleton

**What was built:** Full Phase 0 skeleton. Root `CMakeLists.txt` (C++20, Release `-O3`+LTO via `check_ipo_supported`, Debug ASan/UBSan on non-MSVC, `NIGHTWING_ENABLE_BMI2`/`NIGHTWING_BUILD_TESTS` options). `src/CMakeLists.txt` builds `nightwing_lib` (static) + `nightwing` executable; BMI2/POPCNT compile flags gated behind the build option so `-DNIGHTWING_ENABLE_BMI2=OFF` gives a portable fallback build. `src/support/cpu_features.{h,cpp}` — runtime BMI2/POPCNT detection (MSVC `__cpuid`/`__cpuidex` path, GCC/Clang `__builtin_cpu_supports` path, safe unknown-compiler fallback), cached, idempotent. `src/main.cpp` — empty-engine skeleton that runs detection and prints the feature summary. `tests/CMakeLists.txt` — Catch2 v3.7.1 via `FetchContent`, `catch_discover_tests` wired to `ctest`. `tests/test_smoke.cpp` — pipeline smoke test. `.github/workflows/ci.yml` — Linux/macOS/Windows × Release/Debug matrix, configure → build → `ctest`.

**Bugs fixed:** CI run showed `macos-latest` (Debug + Release) failing to compile: `-mbmi2`/`-mpopcnt` and `__builtin_cpu_supports` are x86-only, but `macos-latest` runners are now Apple Silicon (arm64) and the compiler rejected both outright. Fixed by gating the BMI2 compile flags (`src/CMakeLists.txt`, `CMAKE_SYSTEM_PROCESSOR` check) and the `__builtin_cpu_supports` code path (`src/support/cpu_features.cpp`, `__x86_64__`/`__i386__` check) behind an x86 architecture check, correctly reporting BMI2/POPCNT absent on ARM. Linux and Windows (all configs) passed on the first run.

**Decisions made:** None new — implementation of decisions already logged in DECISIONS.md/ARCHITECTURE.md (Catch2, BMI2 fast-path + portable fallback, Release/Debug split).

**Next session start point:** Begin Phase 1 — bitboard primitives (`src/board/bitboard.h/.cpp`): set/clear/pop bit, popcount, bitscan, all via compiler intrinsics (no manual bit-loops), plus unit tests in a new `tests/bitboard_tests.cpp`. Say "Continue" or "Start" to proceed.

---

## 2026-08-11 — Session 1: Project Founding

**What was built:** Project scaffolding only — no code yet. Established the four-doc workflow (ROADMAP.md, DECISIONS.md, SESSIONS.md, ARCHITECTURE.md), repo structure, and Claude Project working instructions (mobile-only, full-file delivery, tiered doc reading, context management protocol).

**Bugs fixed:** N/A

**Decisions made:**
- No NNUE, no Syzygy tablebases — hard project constraint
- Eval: incremental HCE, tunable terms from day one, Texel tuner added in Phase 5
- Search: single-threaded PVS first, Lazy SMP deferred to Phase 7
- Endgame: hand-built heuristics, no self-generated tablebases (out of scope)

(Full rationale in DECISIONS.md)

**Next session start point:** Begin Phase 0 — create the CMake project skeleton (C++20, empty `main.cpp`, `CMakeLists.txt`) and the GitHub Actions CI workflow that runs a build + `ctest` matrix on push. Say "Go" to start.
