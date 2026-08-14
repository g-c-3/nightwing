# Nightwing — Decisions Log

Architectural decisions, newest first. Each entry: date, decision, rationale, alternatives considered.

---

## 2026-08-13 — UCI loop: move matching by to_uci() string, safe no-time-control depth fallback, synchronous `go`

**Decision:** `src/uci/uci.cpp`'s `run()` takes `std::istream&`/`std::ostream&` explicitly (not hardcoded `std::cin`/`std::cout`) so `tests/uci_tests.cpp` can drive it with string streams. `apply_uci_moves()` handles `position ... moves ...` by generating the real legal move list at each step and matching each UCI token (e.g. `"e7e8q"`) against `Move::to_uci()`, rather than hand-decoding the UCI move-string grammar (promotion suffix, etc.) independently — this way promotion/castling/en-passant flags are set exactly as `generate_legal_moves()` already produces them, with zero duplicated encoding logic. `go` always runs `search_iterative_deepening()` synchronously to completion before the loop reads its next input line — there is no background search thread, and `stop`/`go infinite` are accepted as tokens but have no real effect (see this decision's "deferred" note below). When neither an explicit `depth` nor any time control (`movetime`, or `wtime`/`btime`) is given at all, `go` falls back to a small fixed `kNoTimeControlDepth = 5`, not the much larger `kTimedSearchMaxDepth = 64` ceiling used when a real time budget is what's actually expected to stop the search.

**Rationale — the no-time-control fallback specifically:** This was caught before it shipped, not after: an early draft used `kTimedSearchMaxDepth` (64) as the depth for a bare `go` too, reasoning that `time_limit_ms = 0` (unlimited) would just mean "search as deep as it wants." That's wrong for Phase 2 specifically — there's no pruning, no move ordering, and no mid-search interruption yet (see the iterative-deepening time-check-granularity decision from the previous session), so `time_limit_ms = 0` with `max_depth = 64` means genuinely unbounded, hang-until-killed search on a bare `go`. Empirical timing on this sandbox's hardware (Release build, starting position, no time limit): depth 4 ≈ 9ms, depth 5 ≈ 127ms, depth 6 ≈ 1030ms — roughly an order of magnitude per additional ply, entirely consistent with an unpruned ~35-wide branching factor. Depth 5 was chosen as the fallback specifically because it's fast enough to stay well under a second even in a much slower Debug/ASan-instrumented build, while depth 6+ starts approaching a range where that safety margin gets uncomfortably thin. This matters in practice, not just in theory: a self-play script or a GUI's "just click go" button sending a bare `go` with no depth/time info is a completely ordinary UCI interaction, not an edge case.

**Rationale — synchronous `go`, no `stop`/`go infinite`:** Same scope cut as the previous session's iterative-deepening time-check granularity: true async search needs a background thread and a stop flag `negamax()` checks periodically, which is meaningfully more machinery than "basic UCI loop that a GUI or self-play script can drive" requires. The failure mode without it is a clean, honest one — `stop` genuinely has no effect (documented in `uci.cpp`'s header comment, not silently pretended to work), and `go infinite` degrades to the same small fixed-depth fallback as a bare `go`, rather than hanging forever waiting for a `stop` that this loop can't act on anyway (since it can't read `stop` from `in` until the in-progress synchronous `go` call returns). Revisit alongside real time-control-driven pondering, if/when that's prioritized — not tracked as a specific ROADMAP.md item yet.

**Verification — real self-play, not just unit tests:** Beyond the Catch2-equivalent sandbox harness (13 cases, ASan/UBSan, matching `uci_tests.cpp`'s cases), the actual compiled `nightwing` binary was driven through real stdin/stdout by a small script simulating a GUI/self-play controller: one run reached 60 plies (including a real pawn promotion, `b2a1q`, correctly generated and correctly re-parsed on the next `position ... moves ...` call) without concluding; a second run (shallower search, depth 2, weaker play as expected) reached a genuine checkmate at ply 15, which was independently re-verified outside the UCI layer entirely (replaying the same move list through `generate_legal_moves()`/`is_square_attacked()` directly, confirming zero legal moves and the side to move's king in check) rather than just trusting the engine's own `bestmove 0000` output. This is why ROADMAP.md's "engine can play a full legal game against itself via UCI" item is checked off in the same session as "basic UCI loop," rather than left for a separate session — the self-play validation is a natural, already-completed consequence of building and testing the loop itself, not separate unbuilt work.

**Alternatives considered:**
- Hand-parsing UCI move strings (`"e2e4"`, `"e7e8q"`) into `Move` objects directly, independent of `generate_legal_moves()` — rejected; this would duplicate `Move`'s own encoding rules (which flag bits mean what) in a second place that could silently drift out of sync with `move.h`/`move.cpp`, and would need its own legality checking to reject an illegal-but-well-formed token, which `generate_legal_moves()` already provides for free by construction.
- `kTimedSearchMaxDepth` (64) as the no-time-control fallback too — rejected after the empirical timing above showed it would hang the engine; see rationale.
- A background-thread `go`/`stop` implementation now, rather than deferring it — rejected as premature relative to Phase 2's "basic UCI loop, engine can play a full game" scope; the synchronous version is simpler, has no concurrency-bug surface to get wrong, and is sufficient for both stated goals. Revisit when pondering or truly time-pressured play (e.g. against a real clock in a GUI) makes the gap matter.

---

## 2026-08-13 — Iterative deepening: between-iteration time checks only, extended search.cpp in place

**Decision:** `search_iterative_deepening()` was added to the existing `src/search/search.{h,cpp}` (not a new `iterative_deepening.h/.cpp` file), and it implements time-budget stopping by checking a `std::chrono::steady_clock` deadline *between* successive `search_fixed_depth()` calls, not mid-search inside `negamax()`. Depth 1 always runs unconditionally before the first time check. `SearchResult` gained a `depth_completed` field (set by both `search_fixed_depth()` and `search_iterative_deepening()`) and its `nodes` field, for the iterative-deepening path, now means the *total* across every completed iteration rather than just the deepest one.

**Rationale:** Extending the existing file rather than adding a new one avoids either duplicating `negamax()`/`in_check()` or exposing them outside their current anonymous-namespace scope purely to share them across translation units — `search_iterative_deepening()` doesn't need them directly, it only calls the already-public `search_fixed_depth()` repeatedly, so there was no real reason to split files. Checking the clock only between iterations (not every N nodes inside `negamax()`) is a deliberate scope cut for Phase 2: true mid-search interruption is a real, standard technique, but it requires threading a stop condition through the recursion and unwinding without corrupting the alpha/best-move bookkeeping — meaningfully more machinery than "get something playing" needs before real UCI time controls (`go movetime`/`wtime`, the very next ROADMAP.md item) exist to actually exercise it. The failure mode without it is bounded and mild: at most one already-started iteration overruns the budget by however long that one depth takes, not an unbounded hang. Accumulating `nodes` across all completed iterations (rather than just reporting the deepest one) was chosen because that's the number that actually corresponds to wall-clock time spent — the right basis for NPS once `bench` exists (Phase 8), not merely the final iteration's count.

**Alternatives considered:**
- Mid-search interruption via a node-count-checked stop flag inside `negamax()` — rejected for now, for the reasons above; the natural point to add this is alongside real time-control parsing in the UCI loop, where a tight `movetime` budget would make the current between-iteration granularity's worst case (one full extra depth) actually matter. Revisit then.
- Reporting only the deepest iteration's node count (ignoring the "wasted" shallower iterations) — rejected; those nodes were real work that took real wall-clock time, and a `bench` command computing NPS from an undercounted total would report a misleadingly high number.
- A separate `src/search/iterative_deepening.h/.cpp` file — considered per the prior session's SESSIONS.md note flagging this as an open question; rejected once it became clear the function has no need to touch `negamax()`/`in_check()` directly, making a same-file extension simpler with no real separation-of-concerns benefit lost.

---

## 2026-08-13 — CMake: MSVC native ASan tried on Windows Debug, reverted same day — hung CI

**Decision:** Reverted. `CMakeLists.txt`'s MSVC Debug branch is back to skipping sanitizers entirely (the original behavior), not the `/fsanitize=address` override added earlier this session.

**What happened:** The `/fsanitize=address` + `/INCREMENTAL:NO` change built and linked cleanly on `windows-latest, Debug` (Configure: 26s, Build: 2m46s — no build/link failure), but the *first* test in the suite — `test_smoke.cpp`'s "CPU feature detection runs without crashing," a trivial, unrelated smoke test — hung and was killed by CTest's own per-test timeout (`***Timeout 1500.00 sec`, i.e. the full 25-minute default). One test alone consuming the entire 25-minute budget while 100 more remained made this an outright hang, not "slow but working" — the run was cancelled rather than waited out.

**Likely cause (unconfirmed):** `cpu_features.cpp` almost certainly uses `__cpuid`/CPUID-family intrinsics to detect BMI2/POPCNT support. MSVC's native ASan is documented to have rough edges around low-level/intrinsic code, and separately uses an on-demand-paging shadow-memory model (first-chance exceptions per page fault) that Microsoft's own docs note can behave differently under different debuggers/hosts — a plausible interaction with GitHub's `windows-latest` runner's virtualization layer. Not confirmed beyond this reasoning: no MSVC toolchain was available in the sandbox this was authored from, so this couldn't be reproduced or bisected locally the way the `search_tests.cpp` init-order bug was.

**Why reverting was the right call, not further tuning:** Without a Windows/MSVC environment to iterate in, further changes (e.g., excluding `cpu_features.cpp` from ASan instrumentation via a function-level `__declspec(no_sanitize_address)`, or trying a different ASan/runtime combination) would be unverified guesses shipped straight to CI, burning GitHub Actions minutes per attempt with no faster feedback loop than "push and wait 25+ minutes to find out." Reverting to the known-good, previously-green configuration and documenting the failure mode here is the correct move until either a Windows dev environment is available to iterate in properly, or this is revisited with more specific research into the ASan/CPUID interaction.

**Status:** MSVC Debug is back to zero sanitizer coverage — the coverage gap discussed in the original decision (see git history / prior session context) stands. Not re-attempting without a way to actually test the fix before pushing it.

---

## 2026-08-13 — Bugfix: search_tests.cpp missing per-process magic-bitboard init

**Cause:** `catch_discover_tests` (tests/CMakeLists.txt) registers every individual Catch2 `TEST_CASE` as its own separate CTest test, run as its own fresh process invocation of the `nightwing_tests` binary — not as one shared long-lived process. Every existing test file (`movegen_tests.cpp`, `perft_tests.cpp`, `attacks_tests.cpp`, `masks_tests.cpp`) accordingly calls `init_masks()`/`init_magic_bitboards()`/`init_zobrist_keys()` at the top of *each* `TEST_CASE` body, not once globally. `search_tests.cpp` (delivered last session) missed this convention entirely, so `g_rook_bits[]`/`g_bishop_bits[]` etc. were zero-initialized-but-never-populated in each fresh process, and `rook_attacks()`/`bishop_attacks()`'s `>> (64 - g_*_bits[sq])` shift became `>> 64` — undefined behavior, reliably a SEGV under both UBSan and plain release builds. Confirmed by reproducing the identical stack trace (`attacks.cpp:289` inside `bishop_attacks()`, called via `movegen.cpp`'s `attackers_to()`, called via `generate_legal_moves()`, called via `search_fixed_depth()`) in a standalone uninitialized-process harness before touching the fix.

**Fix:** Added a local `init_all()` helper to `search_tests.cpp` (calling all three init functions, matching `perft_tests.cpp`'s existing helper of the same name/shape exactly) and a call to it as the first line of every `TEST_CASE` in the file.

**Why correct:** Re-ran the same standalone harness with the three init calls added ahead of `search_fixed_depth()` and it no longer crashes (confirmed exit code 0, `nodes == 20` on the start position) — an exact before/after reproduction of the CI failure and its resolution, not just a plausible-looking patch. All 6 of `search_tests.cpp`'s cases were re-verified this way against the real board/movegen/eval code under ASan/UBSan (see SESSIONS.md).

**Process note:** This should have been caught before the previous session's delivery — the sandboxed verification harness used at the time (`check_search.cpp`) called the three init functions once at the top of `main()`, which hides exactly this class of bug (each `TEST_CASE` sharing process-wide state the way `main()` does, unlike CTest's actual per-test-case process model). Going forward, sandbox verification for new test files should mimic CTest's per-case isolation (e.g., a small harness invoked once per logical test case, each starting from a clean, uninitialized process) rather than a single `main()` that initializes once and runs every case in sequence — noted here so the same blind spot doesn't recur for `iterative_deepening`'s tests or later suites.

---

## 2026-08-13 — Search: negamax form, fixed `kInfinity` bound (not `numeric_limits::max()`), mate score decays by ply

**Decision:** `src/search/search.cpp`'s `negamax()` is negamax-form alpha-beta (each recursive call flips sign and swaps/negates alpha-beta, rather than separate maximizing/minimizing branches), matching `eval::evaluate()`'s existing White-relative convention by flipping it to side-to-move-relative only at the depth-0 base case. Alpha/beta bounds use a fixed `kInfinity = 1'000'000` sentinel rather than `std::numeric_limits<int>::min()`/`max()`, specifically because negamax's `-beta`/`-alpha` calls would invoke undefined behavior negating `INT_MIN`. Checkmate is scored `kMateScore - ply` (not a flat `kMateScore`) so that mate-in-1 outscores mate-in-3 — the search will therefore prefer the fastest forced mate available rather than stopping at the first one found regardless of length.

**Rationale:** Negamax form was chosen over explicit min/max branches because it halves the code that needs to independently stay correct (one recursive function instead of two near-duplicates), and it's what nearly every classical engine's alpha-beta actually looks like — no exotic choice here, just the standard shape (CPW "Negamax"). The `kInfinity` sentinel is a small, deliberate departure from "use the type's real max" specifically to route around the negation-overflow trap; `1'000'000` is comfortably larger than any plausible score (mate scores top out at 32000, eval() in the low thousands even with extra queens) with a lot of headroom for `alpha`/`beta` window narrowing later (Phase 3's aspiration windows) without approaching the sentinel.

**Alternatives considered:**
- Flat `kMateScore` for every mate regardless of ply — rejected; a search that can't tell a mate-in-1 from a mate-in-5 apart will happily pick the slower one (or, worse, keep re-finding "a mate exists" without converging toward the fastest one as depth increases), which is a correctness-adjacent bug even at fixed low depth, not just a cosmetic UCI-info-line concern.
- `std::numeric_limits<int>::min()/max()` for alpha/beta — rejected outright; `-std::numeric_limits<int>::min()` is undefined behavior (no positive representation of `INT_MIN` exists in two's complement `int`), and this is exactly the kind of UB that only shows up in an edge case (root node before any bound has narrowed) an ASan/UBSan run would rather catch in dev than in a rare CI-runner-specific miscompile.
- Detecting mate at the search horizon (i.e., having the depth-0 base case check for zero legal moves too) — rejected as unnecessary scope for this task; it would mean generating moves at *every* leaf (defeating the purpose of the depth-0 cutoff existing at all) just to special-case a position most leaves will never be. This is the standard, well-understood "horizon effect" limitation of fixed-depth search (a mate exactly one ply beyond the search horizon reads as a merely-good static eval, not as mate) — search_tests.cpp's mate-in-1 test therefore deliberately searches to depth 2, not depth 1, to actually exercise mate detection; documented in search.h/search.cpp so it isn't mistaken for a bug later.

---

## 2026-08-13 — PSQT source: Michniewski's Simplified Evaluation Function, king-only tapered initially

**Decision:** `src/eval/psqt.cpp`'s material values and piece-square tables are Tomasz Michniewski's "Simplified Evaluation Function," published on the Chess Programming Wiki (https://www.chessprogramming.org/Simplified_Evaluation_Function), used as-is as a starting baseline per ARCHITECTURE.md's Attribution Policy — full credit in psqt.cpp's header comment, not presented as original. Only the king gets a genuinely distinct middlegame/endgame table pair (Michniewski's own design); the other five piece types reuse one table for both `mg` and `eg` in `Score` until Phase 5's Texel tuner has enough terms to learn real per-phase splits instead of hand-guessing them. Black's values are derived from White's tables via a vertical mirror (`sq ^ 56`) rather than a second stored table, since every relevant row (pawn/bishop/rook/king) is left-right symmetric; knight and queen use one identical table for both colors, matching Michniewski's original (he doesn't differentiate those two by color either).

**Rationale:** A well-known, freely published baseline (rather than hand-guessed values) is exactly what SESSIONS.md's session-6→7 handoff flagged as the right call for the first eval pass — it gives correctly-shaped positional knowledge (pawn advancement, knight centralization, king safety vs. centralization by phase, etc.) from day one, and gives Phase 5's tuner real starting weights to refine rather than noise. Michniewski's set (rather than PeSTO's more heavily fine-tuned values) was chosen specifically because it's small, round-numbered, and easy to transcribe and cross-check correctly by hand — accuracy of the literal numbers matters here (a chess engine silently playing on subtly wrong constants is a hard-to-notice bug class), and PeSTO's fuller 768-entry tuned table set is a better target for the *tuner* to reproduce/exceed later than for hand-transcription now. During transcription, two independent published sources (the CPW page's own inline excerpts, and a from-scratch C# port by Adam Berent) were cross-checked against a third (an unrelated public GitHub Python port), which turned up two sign-flip typos in that third source's king mg/eg tables (the a1-file corner entries on rank 1/rank 8 shown as positive instead of negative); the corrected values are what's actually in `psqt.cpp`, with the cross-check documented in its header comment.

**Alternatives considered:**
- PeSTO's tables — rejected for now; more values (768 vs. essentially six 64-entry tables plus one extra king table), tuned via gradient descent rather than a small number of principled round numbers, so higher transcription-error risk for a first pass with no tuner yet in place to catch a wrong constant. Worth reconsidering as a Phase 5 tuner *starting point* if Michniewski's baseline turns out to converge slowly.
- Tapering every piece type immediately (inventing separate mg/eg values for pawn/knight/bishop/rook/queen now) — rejected; Michniewski's source doesn't provide these, so any values beyond his king mg/eg pair would be hand-guessed, which is exactly what "start from a well-known public PSQT set" was meant to avoid. The `Score`/`taper()` infrastructure (score.h) is fully general and already blends all six piece types correctly; only the *data* for five of them is currently mg==eg, and that's a data update (Phase 5), not an infrastructure change.

---

## 2026-08-13 — Eval: plain `{mg, eg}` Score struct, full recompute per call (not incremental) for Phase 2

**Decision:** `src/eval/score.h`'s `Score` is a plain `struct { int mg; int eg; }` with ordinary arithmetic operators, not a single packed 32-bit integer (the classic Stockfish `make_score`/`mg_value`/`eg_value` bit-packing trick). `evaluate()` (`src/eval/eval.cpp`) scans all 64 squares and recomputes material+PSQT from scratch on every call; it does not use an incremental accumulator updated inside `make_move()`/`unmake_move()`.

**Rationale:** Phase 2's stated goal is "get something playing" — correctness and a working UCI loop, not peak throughput. A plain two-`int` struct is trivially readable and impossible to get the sign-extension/rounding details of packed encoding wrong on, which matters more right now than the (real, but not yet relevant) memory/perf win packing would give once eval and search are both hot paths. Likewise, full recomputation is the correct scope for a five-term eval (12 piece-bitboard scans, negligible cost) — ARCHITECTURE.md's "eval on the fly" accumulator pattern is a real commitment, but wiring it into `make_move()`/`unmake_move()` now, before Phase 2's alpha-beta search even exists to call `evaluate()` from a hot loop, would be optimizing code whose call frequency and profile aren't known yet.

**Alternatives considered:**
- Packed `int32_t` Score (Stockfish-style) — rejected for now; real technique, correctly attributable to Stockfish/CPW's "Tapered Eval" page if adopted, but premature before profiling shows eval-term addition is a measurable cost next to search-tree size. Revisit alongside the incremental-accumulator work once search exists and `bench` (Phase 8, or informally earlier) can measure it.
- Incremental material/PSQT accumulator wired into make_move()/unmake_move() now — rejected for Phase 2 specifically; correctly identified in ARCHITECTURE.md as the long-term design, but doing it before alpha-beta search exists means guessing at the access pattern it needs to optimize for. Revisit once Phase 2's search loop is in place and `evaluate()` is actually being called at real node-count volume — this is a performance pass, not a correctness requirement, so deferring it doesn't block "engine can play a full legal game via UCI."

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
