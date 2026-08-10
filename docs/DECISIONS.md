# Nightwing — Decisions Log

Architectural decisions, newest first. Each entry: date, decision, rationale, alternatives considered.

---

## 2026-08-11 — Roadmap expanded to full "great classical engine" feature set, including pondering

**Decision:** ROADMAP.md expanded across Phases 3-9 to include the full feature set found in elite pre-NNUE engines (Stockfish 11, Ethereal, Komodo classical): pondering (search + protocol side), IIR, mate distance pruning, repetition/50-move handling in search, pawn hash table, LMP, history pruning, continuation history, ProbCut/multi-cut, delta pruning, knight outposts, space, threats, king tropism, trapped pieces, tempo bonus, material imbalance table, eval cache, `bench` command, skill level, contempt, and a new Phase 9 for advanced/stretch items (NUMA, distributed search, optional self-generated tablebases).

**Rationale:** Original roadmap covered the core skeleton of a strong engine but omitted several techniques that meaningfully separate elite classical engines from mid-tier ones — pondering being explicitly requested, the rest added so the roadmap actually matches the target ceiling discussed (competitive with Stockfish 11 / Ethereal-class strength, see 2026-08-11 Elo discussion in session).

**Alternatives considered:**
- Leave roadmap minimal and add features ad hoc as later "nice to have" sessions — rejected; the explicit goal is to match great engines, so the roadmap should reflect that from the start rather than being discovered piecemeal.

**Note:** This is an addition to phase task lists, not a change to founding constraints (no NNUE, no Syzygy still holds — see entries below). Self-generated tablebases remain explicitly optional/out-of-core-scope, now listed under Phase 9 for clarity rather than omitted.

---

## 2026-08-11 — No NNUE, no Syzygy tablebases (project constraint)

**Decision:** Nightwing will not use neural network evaluation (NNUE or any other net) or external endgame tablebases (Syzygy or otherwise), for the life of the project.

**Rationale:** Explicit project goal — a "dream project" built on classical technique: search, hand-crafted eval, and engineering craft, not learned weights or precomputed tables.

**Alternatives considered:** N/A — this is a foundational constraint, not a comparison decision.

---

## 2026-08-11 — Eval: incremental HCE with tunable terms, Texel tuner added later

**Decision:** Start eval with material + PSQT only. Add terms (mobility, king safety, pawn structure, etc.) incrementally as search/movegen stabilize. Every term is a named, tunable constant from the start. A Texel/SPSA tuner is added as its own module once the eval has enough terms to make tuning worthwhile (targeting Phase 5 of ROADMAP.md).

**Rationale:** Building a tuner before the eval has enough terms wastes effort tuning a near-empty eval. Waiting too long leaves hand-guessed weights in place longer than necessary. Keeping every term as a named constant from day one means the tuner can be dropped in later without refactoring eval code.

**Alternatives considered:**
- Full HCE designed up front, tuned offline before integration — rejected, delays getting a playable/testable engine.
- Tuner built alongside the very first eval terms — rejected, not enough parameters yet to meaningfully tune.

---

## 2026-08-11 — Search: single-threaded PVS first, Lazy SMP deferred

**Decision:** Build and fully verify single-threaded PVS/alpha-beta + iterative deepening + aspiration windows + standard pruning/extensions before adding any multithreading. Lazy SMP is a distinct later roadmap phase (Phase 7).

**Rationale:** Multithreaded search bugs (races, non-deterministic failures) are far harder to debug on top of an unproven single-threaded search. Getting single-threaded search perft/bench-verified and stable first isolates correctness issues from concurrency issues.

**Alternatives considered:**
- Lazy SMP from the start — rejected, too much simultaneous complexity for a from-scratch engine.

---

## 2026-08-11 — Endgame/opening knowledge: hand-built heuristics, no self-generated tablebases

**Decision:** Endgame knowledge comes from hand-built eval heuristics (KPK, KRK, opposition, wrong-bishop-corner, etc.), not tablebases. Opening book (if added) will be small and curated, added later and treated as optional, not a dependency.

**Rationale:** Hand-built heuristics give real strength cheaply and fit the "no tablebases" constraint. Generating our own tablebases is a multi-month project on its own (comparable in scope to building Syzygy) and out of scope here — if ever wanted, it's a separate future project, not a Nightwing dependency.

**Alternatives considered:**
- Self-generated small (3-4-5 man) tablebases — rejected for scope; revisit only as an explicit future side-project if desired.
