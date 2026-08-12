# Nightwing — Decisions Log

Architectural decisions, newest first. Each entry: date, decision, rationale, alternatives considered.

---

## 2026-08-13 — Bulk-counting perft: a separate function, not a flag on `perft()`

**Decision:** `perft_bulk()` is a distinct function in perft.h/.cpp, not `perft(pos, depth, bool bulk)` or a template parameter on a single implementation.

**Rationale:** The two are used for different purposes that want to stay independently readable: `perft()` is the reference/correctness implementation (every node genuinely visited via make/unmake, so it's the one to trust if the two ever disagree), while `perft_bulk()` is the throughput baseline `src/bench.cpp` measures against. A boolean flag would make both purposes harder to reason about at each call site ("is this call site's `true` argument obviously about speed, or could someone mistake it for something else") for essentially zero code-sharing benefit — the two functions differ only in what happens at depth 1, so the "duplication" is two lines, not a maintenance burden. `tests/perft_tests.cpp` cross-checks them against each other directly (same node counts for every reference position/depth), which is a more direct and legible test than trying to test a flag's two branches through one shared function.

**Alternatives considered:**
- Single function with a runtime `bool bulk` parameter — rejected; no real code-sharing win, and the two call sites (correctness tests vs. bench) become slightly less self-documenting about which mode they're in.
- Single function templated on `bool Bulk` for zero-overhead dispatch — rejected as premature; perft/perft_bulk aren't on the actual search hot path (that's the eventual move generator inside alpha-beta, a separate concern), so there's no performance case for the extra complexity yet.

---



**Decision:** While building the perft suite, two real correctness bugs in `movegen.cpp` were found — not via review, but by writing a second, deliberately independent legal-move generator (pseudo-legal generation with no pin/check optimization at all, filtered by actually making each move and checking whether the mover's king ended up attacked) and bisecting the game tree for the first position where the fast and slow generators' move sets diverged. Both bugs are now fixed; the independent generator was scratch/debug-only tooling and isn't part of the committed codebase (see "Alternatives considered" for why it wasn't kept).

1. **`between(a, b)` lacked an alignment guard.** It computed `(rook_attacks(a,{b}) & rook_attacks(b,{a})) | (bishop_attacks(a,{b}) & bishop_attacks(b,{a}))`, on the assumption that only one of the two terms could ever be nonzero. That assumption is false when `a` and `b` share neither a rank/file nor a diagonal: each ray call is then completely unobstructed (the lone "blocker" isn't on that ray at all), so each returns a full unobstructed cross or diagonal from its own square, and intersecting two unrelated full rays can spuriously share a square. Concretely, `between(e1, a5)`'s rook term produced `{a1, e5}` even though e1 and a5 share neither a rank nor a file — those two squares are an artifact of the two unobstructed crosses overlapping, not anything actually "between" e1 and a5. This silently under-detected pins (a diagonal pin through a pawn on d2 was missed entirely, since `between()`'s bogus extra squares polluted the blocker-counting logic in `compute_pins()`). Fixed by computing only the term for the alignment that actually holds (checking `file_of(a)==file_of(b) || rank_of(a)==rank_of(b)` for the rook term, and equal absolute file/rank deltas for the bishop term), returning an empty bitboard when neither holds.

2. **`is_square_attacked()`'s non-sliding-piece terms read the real `Position`'s piece bitboards regardless of the simulated `occ` passed in.** Sliding-piece attacks (`bishop_attacks(sq, occ)`, `rook_attacks(sq, occ)`) correctly respect a hypothetical occupancy for ray-blocking, but the pawn/knight/king terms (`pawn_attacks(...) & pos.pieces(by_color, Pawn)`, etc.) always read the actual board state. The original en-passant legality check simulated the capture by clearing bits in a local `Bitboard occ` copy (removing the mover and the captured pawn, setting the target square) and calling `is_square_attacked(pos, king_sq, them, occ_after)` — this correctly stopped sliding pieces from seeing through where the captured pawn had been, but the pawn-attacker term still found that same pawn via `pos.pieces(them, Pawn)`, since it was never actually removed from `pos`, only from the local occupancy copy. Net effect: a legal en passant capture could be wrongly rejected as exposing a discovered check from a pawn that the simulation was supposed to have removed. Fixed by simulating en passant via `make_move()` on a throwaway `Position` copy instead of hand-rolled occupancy bit-twiddling — this can't have the same class of bug, since `make_move()` updates every piece-type bitboard, the mailbox, and occupancy together, by construction.

**Rationale:** Both bugs are exactly the kind unit tests miss and perft reliably catches — narrow interactions between pieces several moves deep that no one would think to hand-write a test for. The bisection-against-an-independent-generator method is what made root-causing fast rather than guesswork: comparing move *sets* (not just counts) at each ply pinpointed the exact first divergent position in each case within a few tool calls, rather than needing to manually work through why a depth-4 node count was off by 30 or 66.

**Alternatives considered:**
- Keeping the naive cross-check generator as permanent test infrastructure (e.g. `tests/naive_movegen_reference.cpp`, run against perft positions on every CI build) — rejected for now; it would roughly double movegen-related CI time for a check that's valuable during development but not on every commit once the two bugs it caught are fixed and perft is green. Worth reconsidering if a future movegen change introduces a new subtle bug perft alone doesn't localize well.
- Fixing bug 2 by threading a "these specific squares don't count as occupied by `by_color`" exclusion set through `attackers_to()` instead of switching to a real `make_move()` simulation — rejected; more surface area for exactly the same class of bug to recur (any future non-sliding piece term added to `attackers_to()` would need to remember to respect the exclusion set too), whereas the `make_move()`-based simulation is correct by construction and gets more exercise (every other make/unmake user, i.e. the perft suite itself) than a special-cased exclusion mechanism would.

---



**Decision:** Three related choices made together while implementing `make_move()`/`unmake_move()` (board.h/.cpp):
1. `UndoInfo` stores the pre-move `zobrist_hash` verbatim, and `unmake_move()` restores it directly rather than reversing the incremental XOR steps make_move() applied. Still O(1), still "incremental" in the sense ARCHITECTURE.md means (no O(64) recompute), just restore-from-saved-value instead of undo-by-replaying-XORs.
2. Castling-rights revocation is computed by comparing the move's `from`/`to` squares against the four corner squares (a1/h1/a8/h8), not by checking whether the moving/captured piece is actually a rook. This is safe because rights only ever get revoked, never re-granted — once a corner's right is cleared, any later, unrelated piece occupying that square is a no-op against an already-clear bit.
3. `zobrist.h` gained four new public accessors (`piece_square_key()`, `side_to_move_key()`, `castling_right_key()`, `en_passant_file_key()`) so `board.cpp`'s incremental hash update can XOR against the same key tables `compute_hash()` uses, without board.cpp reaching into zobrist.cpp's previously-file-local key arrays.

**Rationale:** (1) Storing the previous hash is simpler and less error-prone than writing a second "undo the XOR sequence" code path that has to exactly mirror make_move()'s order of operations — any drift between the two would be a silent, hard-to-catch bug, whereas a saved value can't drift. This is the same pattern Stockfish's state stack uses. (2) The by-square check is the standard technique (used by essentially every classical engine) precisely because it avoids "was this actually the original rook" bookkeeping — a single square-based check handles rook moves, rook captures, and (via the king branch) king moves uniformly. (3) The alternative — duplicating key generation/storage in board.cpp, or making board.cpp befriend zobrist.cpp's internals — would violate the existing module boundary (zobrist.h/.cpp owns the keys; board.h/.cpp owns state mutation) for no benefit.

**Alternatives considered:**
- Reversing the XOR sequence on unmake instead of restoring a saved hash — rejected; more code, same asymptotic cost, and a second place for the hash-update logic to go subtly wrong.
- Tracking "does the moving/captured piece equal Rook and start on a corner" explicitly for castling-rights revocation — rejected; more conditions to get right for identical behavior, since the by-square check already handles every real case safely (see rationale above).
- Making the Zobrist key tables `extern`-visible globals instead of adding accessor functions — rejected; accessor functions keep the tables themselves private to zobrist.cpp (encapsulation) while still giving board.cpp exactly the per-key lookups it needs.

---



**Decision:** `generate_legal_moves()` produces fully legal moves directly (no separate pseudo-legal-then-filter pass), using three techniques: (1) a `target_mask` bitboard restricting non-king moves to check-resolving squares (capture the sole checker, or block the ray to it — computed once per call from a `checkers` bitboard); (2) a per-square "sniper" pin detection pass (enemy sliders that would reach the king if own pieces were transparent, checked against actual occupancy for exactly-one-own-piece-in-between) producing a `pin_allowed` ray per pinned piece; (3) en passant legality resolved by direct occupancy simulation (remove both pawns, add the mover on the target square, re-run the king-attacked check) rather than reasoned about via the pin/target masks, since it's the one move whose discovered-check risk (the "horizontal en passant pin") isn't captured by either mask on its own.

**Rationale:** Make/unmake move doesn't exist yet (next roadmap item), so a "generate pseudo-legal, then make each move and check if the king is attacked" filter — the simplest-to-reason-about approach — isn't available yet without generating that machinery first, out of order. The mask-based approach is also what ARCHITECTURE.md already committed to at the Phase 1 planning stage ("fully legal move gen ... via pin/check masks, to keep search code simple") and avoids the wasted work of generating and then discarding illegal moves. Simulating en passant specifically (rather than folding it into the pin-ray/target-mask reasoning) was chosen because that reasoning genuinely doesn't cover the horizontal-pin case without extra special-casing that the simulation gets for free and provably-correctly.

**Alternatives considered:**
- Pseudo-legal generation + make/unmake + king-attacked filter — rejected for now; cleanest long-term but requires make/unmake to exist first (next item), and would mean revisiting movegen's legality layer twice. Worth reconsidering once make/unmake lands, if the mask-based approach shows any correctness gaps beyond en passant.
- Extending the pin-ray logic to also cover the horizontal-en-passant case algebraically (checking if the two pawns being removed are both on the pin ray) — rejected; more special-case reasoning to get right and verify than a direct occupancy simulation, for a move type that's already rare enough that the simulation's cost is negligible.

---

## 2026-08-12 — Move encoding: 16-bit packed move (CPW "Encoding Moves" convention)

**Decision:** `Move` (src/board/move.h) packs from-square (6 bits), to-square (6 bits), and a 4-bit flag into a single `uint16_t`, using the flag-value table documented on the Chess Programming Wiki's "Encoding Moves" page (quiet/double-push/castle-kingside/castle-queenside/capture/en-passant/four promotion-piece values x plain-or-capture).

**Rationale:** A `uint16_t` move is cheap to copy, store in TT entries, killer-move slots, and history tables — all upcoming Phase 3/4 needs — without the padding or larger footprint a struct-of-fields (separate from/to/piece/captured/promotion members) would carry. The specific flag table is a widely-used, well-tested public convention, so adopting it (from-scratch implementation, no code copied) avoids reinventing a scheme with the same edge cases (e.g. distinguishing promotion-with-capture from promotion) that public engines have already shaken out.

**Alternatives considered:**
- Wider (32-bit) move encoding carrying the moved/captured piece type inline — rejected for now; those are one mailbox lookup away from a `Position` given from/to, and the extra bits aren't needed yet. Revisit only if profiling later shows the mailbox lookup is a hot-path cost worth trading memory for.
- A plain struct with named fields (from, to, piece, flags as separate members) — rejected; larger, and the packed encoding's bit-twiddling is fully encapsulated behind accessors (`from()`, `to()`, `flag()`, `is_capture()`, etc.), so callers never see raw bit manipulation.

---

## 2026-08-11 — Zobrist en passant hashing: simplified file-only scheme

**Decision:** `compute_hash()` XORs an en-passant-file key whenever `Position::en_passant_square` is set, based purely on that square's file — it does not check whether a pawn is actually present and legally able to make the capture.

**Rationale:** This is the classic/simplified scheme used historically by engines including early Stockfish and Crafty, and it's simple to implement and verify (a pure function of file, no board-scanning). The alternative (only hash the ep key when a capture is genuinely available) is more precise — two positions differing only in an ep square that no pawn can actually capture on would otherwise hash differently even though they're arguably "the same" for repetition purposes — but that precision requires checking adjacent squares for an enemy pawn of the right color, which is board-scanning logic that fits more naturally into move generation (which will already be computing exactly this) than into a hashing utility that should stay a pure function of the four state components (placement, side to move, castling, ep square).

**Alternatives considered:**
- Precise scheme (only hash ep key when a capture is actually available) — rejected for now; adds board-scanning coupling to what should be a simple utility. Revisit if/when this actually causes an observed repetition-detection correctness issue (Phase 3, "Repetition detection... integrated into search, not just board state") — the fix at that point would be to compute the check once during move generation and pass a "does this ep square matter" flag through, not to duplicate scanning logic inside compute_hash() itself.

---

## 2026-08-11 — Endgame strategy: algorithmic theory only, no self-generated exact bitbases

**Decision:** Phase 6 (Endgame Knowledge) is built entirely on algorithmic/generalizing endgame theory — opposition, key/corresponding squares, Lucena/Philidor recognition, fortress detection, zugzwang-aware search shaping, material-signature routing. No exact in-memory bitbases (KPK et al.) are generated, even though that option was technically compatible with the no-external-tablebase constraint (self-generated, no files). This closes out the brainstorm on a "Syzygy-beating" endgame system.

**Rationale:** The differentiator worth investing in is generalization: algorithmic theory plays correctly across *any* matching material configuration, including ones far beyond any tablebase's piece-count ceiling, whereas exact bitbases (even small, self-generated ones) only cover the specific configurations computed — a narrower, more tablebase-like approach that duplicates effort without adding the generalization advantage. Full commitment to the algorithmic approach keeps the codebase and testing surface focused on one coherent strategy rather than maintaining two overlapping endgame systems.

**Alternatives considered:**
- Small curated exact bitbases (KPK, KBNK, KRKP, ~4-man) alongside algorithmic theory — rejected; adds real engineering (retrograde generation, storage format, integration/testing) for coverage that algorithmic rules already handle adequately, without the generalization benefit that's the actual point of this approach.
- Pushing further to ~5-man bitbases — rejected for the same reason, more strongly (diminishing returns, more engineering, same lack of generalization).

---

## 2026-08-11 — Performance engineering formalized: BMI2 fast path, incremental updates, PGO, cache-conscious data layout

**Decision:** ARCHITECTURE.md now specifies concrete performance practices as project standards, not just aspirations: BMI2 PEXT bitboards with a magic-bitboard portable fallback; no heap allocation in the search hot path (fixed-size move lists/search stack); incremental Zobrist/material/PSQT updates on make/unmake instead of full recomputation; cache-line-aligned, prefetched TT entries; PGO build added once the hot path is stable (Phase 8); a `bench` command as the standard NPS/node-count regression check for every performance-sensitive change.

**Rationale:** "Smartest and fastest" requires both search efficiency (fewer, better-chosen nodes — covered by the search/pruning roadmap) and raw throughput (more nodes/sec at equal search quality). Naming concrete techniques now, before code exists, means the codebase is built cache-conscious and allocation-free from the start rather than retrofitted later, which is far more disruptive once search/eval code is in place.

**Alternatives considered:**
- Treat performance as a late-stage optimization pass after correctness/strength are done — rejected; data layout and allocation patterns (fixed-size arrays vs. heap, incremental vs. full eval recompute) are foundational choices that are expensive to change after the fact, unlike compiler flags or PGO which genuinely can be deferred (and are, to Phase 8).
- Full BMI2-only build (no fallback) — rejected; would break on older/non-BMI2 hardware (and some cloud CI runners), and a portable fallback is cheap to maintain alongside the fast path.

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
