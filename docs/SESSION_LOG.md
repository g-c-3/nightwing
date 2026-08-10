# Nightwing — Session Log

Newest entry at top.

---

## 2026-08-11 — Session 1: Project Founding

**What was built:** Project scaffolding only — no code yet. Established the four-doc workflow (ROADMAP.md, DECISIONS.md, SESSION_LOG.md, ARCHITECTURE.md), repo structure, and Claude Project working instructions (mobile-only, full-file delivery, tiered doc reading, context management protocol).

**Bugs fixed:** N/A

**Decisions made:**
- No NNUE, no Syzygy tablebases — hard project constraint
- Eval: incremental HCE, tunable terms from day one, Texel tuner added in Phase 5
- Search: single-threaded PVS first, Lazy SMP deferred to Phase 7
- Endgame: hand-built heuristics, no self-generated tablebases (out of scope)

(Full rationale in DECISIONS.md)

**Next session start point:** Begin Phase 0 — create the CMake project skeleton (C++20, empty `main.cpp`, `CMakeLists.txt`) and the GitHub Actions CI workflow that runs a build + `ctest` matrix on push. Say "Go" to start.
