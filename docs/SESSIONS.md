# Nightwing — Sessions

Newest entry at top.

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
