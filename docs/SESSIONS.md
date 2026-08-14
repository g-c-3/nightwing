# Nightwing — Sessions

Newest entry at top.

---

## 2026-08-13 — Session 8: Eval + fixed-depth search + iterative deepening — Phase 2's first three items done

**What was built:** Phase 2's first three ROADMAP.md items in one session.

**Part 1 — material + tapered PSQT eval** (`src/eval/`, new module), built on Tomasz Michniewski's "Simplified Evaluation Function" (CPW) as the PSQT source — see DECISIONS.md for full attribution/rationale and a correction of two transcription bugs found in one cross-checked source along the way.

- `src/eval/score.h` — `Score` (plain `{mg, eg}` int pair, not packed), the CPW "Tapered Eval" game-phase weighting (`kKnightPhase`/`kBishopPhase`/`kRookPhase`/`kQueenPhase`, `kMaxPhase = 24`), and `taper()` (clamped, defensive against out-of-range phase).
- `src/eval/psqt.h/.cpp` — `material_value()` and `psqt_value()`. Tables are Michniewski's values (pawn/knight/bishop/rook/queen/king-mg/king-eg), LERF-indexed directly (his published array order turned out to already match our `a1=0..h8=63` convention — no reindexing needed), Black derived via a vertical mirror (`sq ^ 56`) rather than duplicated tables, since every mirrored table's rows are left-right palindromes. Knight and queen use one shared table for both colors, matching the source. Only the king has a real mg/eg split for now; the other five piece types reuse one table for both phases (per DECISIONS.md, deferred to Phase 5's tuner).
- `src/eval/eval.h/.cpp` — `evaluate()`: full board scan, White-perspective centipawn score, tapered by a from-scratch `compute_phase()` each call (not yet incrementally accumulated — see DECISIONS.md).
- `tests/eval_tests.cpp` — starting position balances to exactly 0 (mirror-symmetry check), lone extra pawn favors the right side, a lone extra queen dominates king/psqt noise, `taper()` boundary/clamp behavior, PSQT mirror-equality between White/Black on mirrored squares, king mg-vs-eg centralization direction, and a bare-kings sanity bound.

**Part 2 — plain alpha-beta search, fixed depth** (`src/search/`, new module).

- `src/search/search.h/.cpp` — `search_fixed_depth()`: negamax-form alpha-beta, calling `eval::evaluate()` (flipped to side-to-move-relative) at the depth-0 base case, `generate_legal_moves()` everywhere else. Checkmate/stalemate distinguished via a small `in_check()` helper (`is_square_attacked()` on the side-to-move's king square) when `generate_legal_moves()` returns empty. Mate scores decay by ply (`kMateScore - ply`) so shorter forced mates are preferred; alpha/beta use a fixed `kInfinity` sentinel rather than `numeric_limits<int>::max()` to sidestep negamax's negation-overflow trap on `INT_MIN`. Full details and alternatives considered in DECISIONS.md.
- `tests/search_tests.cpp` — depth-1 node count matches the root's legal-move count exactly (20 from the start position), search leaves the input position byte-for-byte unmodified (make/unmake paired correctly), the returned best move is always a member of the real legal move list, an already-checkmated position (fool's mate FEN) returns a null move and `-kMateScore`, an already-stalemated position (K+Q vs K FEN) returns a null move and a draw score, and a constructed back-rank mate-in-1 position is correctly found at depth 2 (not depth 1 — see search.h's header comment on why mate exactly at the search horizon isn't detected).

**Part 3 — iterative deepening**, extending `src/search/search.{h,cpp}` in place (no new file — see DECISIONS.md for why).

- `search_iterative_deepening()`: repeatedly calls `search_fixed_depth()` at depth 1, 2, 3, ... up to `max_depth`, with an optional `time_limit_ms` wall-clock budget checked between iterations (not mid-search — deferred, see DECISIONS.md). Depth 1 always completes unconditionally first, so there's always a legal move to return even under a near-zero time budget; an already-terminal root position (checkmate/stalemate) short-circuits after depth 1 instead of wastefully re-searching it at every depth.
- `SearchResult` gained `depth_completed` (set by both search functions); its `nodes` field now means "total across every completed iteration" for the iterative-deepening path specifically (the right basis for future NPS reporting), while staying "that one call's count" for `search_fixed_depth()`.
- `tests/search_tests.cpp` — new cases: `max_depth=1` matches `search_fixed_depth(pos,1)` exactly (move/score/nodes/depth_completed), unlimited-time `max_depth=3` matches a direct `search_fixed_depth(pos,3)` call's move/score exactly while accumulating strictly more total nodes, an already-checkmated position returns immediately with `depth_completed == 0` and `nodes == 1`, a 1ms time budget against `max_depth=6` reliably stops short of depth 6 without asserting which exact depth was reached (avoids CI-timing flakiness — see the test's comment), plus unmodified-position and legal-move-containment checks matching the existing fixed-depth tests' style. Also added two small `search_fixed_depth()`-specific cases confirming `depth_completed` is set correctly in both the normal and already-terminal paths.
- Verified locally (this session, ahead of a real CI run): `search.cpp` compiles at `-O3 -Wall -Wextra -Wpedantic` with zero warnings after the extension. All new and pre-existing `search_tests.cpp` assertions were re-run against the real `board`/`fen`/`movegen`/`eval` code (g++ 13, `-fsanitize=address,undefined`) via standalone harnesses mirroring every Catch2 case, confirming both the new iterative-deepening behavior and no regression in the existing fixed-depth tests — all passed (e.g. `search_iterative_deepening(pos, 3)`'s best move/score exactly matches `search_fixed_depth(pos, 3)` while node count is strictly higher; the 1ms-budget case stopped at depth 2 on this sandbox's hardware, satisfying the intentionally-loose `>= 1 && < 6` bound). As with Parts 1–2, this is a strong signal but not a substitute for the real `ctest` run — flagged as this session's test-risk item, consistent with the earlier ones. No `CMakeLists.txt` changes were needed for this part (only already-wired-in files were touched).

**Bugs fixed:** `search_tests.cpp` was missing the project's per-`TEST_CASE` `init_masks()`/`init_magic_bitboards()`/`init_zobrist_keys()` convention (every other test file calls these at the top of each `TEST_CASE`, since `catch_discover_tests` runs each one as its own process — see DECISIONS.md for the full cause/fix/verification). Caught via the real CI run (all 6 configs red, all 6 `search_tests.cpp` cases SEGV'ing in `attacks.cpp`'s magic-bitboard shift), reproduced exactly in a standalone uninitialized-process harness, fixed by adding a `perft_tests.cpp`-style `init_all()` helper call to each case, and re-verified both via the same harness (now clean) and the full search behavioral suite under ASan/UBSan. (Two transcription bugs were also caught and avoided in an *external source* consulted while transcribing PSQT values earlier this session — see DECISIONS.md; not a Nightwing bug.)

**Decisions made:** Logged in DECISIONS.md — (1) PSQT source (Michniewski's Simplified Evaluation Function, king-only tapered initially, mirror-derived Black tables, transcription cross-check); (2) `Score` as a plain struct rather than packed-int, and full-recompute `evaluate()` rather than an incremental make/unmake accumulator, both scoped as deliberate Phase 2 simplifications; (3) negamax-form search with a fixed `kInfinity` bound (not `numeric_limits::max()`, to avoid negation UB on `INT_MIN`) and ply-decaying mate scores, with fixed-depth-search's horizon-effect limitation (mate exactly at the search horizon isn't detected) explicitly documented rather than left as a surprise; (4) the `search_tests.cpp` init-convention bugfix above, plus a process note on why the sandbox harness missed it and how to avoid the same blind spot for future test files; (5) tried enabling MSVC's native ASan on `windows-latest, Debug` — **reverted the same session** after it hung a smoke test to CTest's 25-minute timeout in real CI, likely an interaction between MSVC ASan and `cpu_features.cpp`'s CPUID intrinsics; `CMakeLists.txt` is back to its original MSVC-skips-sanitizers state, and this isn't being re-attempted without a Windows environment to actually iterate in; (6) iterative deepening extends `search.cpp` in place rather than a new file, checks its time budget only between iterations (not mid-search, deferred to when real UCI time controls exist to exercise it), and accumulates `nodes` across all completed iterations for future NPS accuracy.

**Next session start point:** Phase 2 continues: basic UCI loop (`src/uci/uci.h/.cpp` or similar, new module — parses `uci`/`isready`/`position`/`go`/`quit` at minimum, wires `search_iterative_deepening()` to `go`, and is what `src/main.cpp` will actually run) — the next unchecked ROADMAP.md item, and the point where this session's `time_limit_ms` parameter on `search_iterative_deepening()` finally gets driven by something real (`go movetime N` / `go wtime`/`btime`). One thing still needs confirming from real CI before that work starts: push this session's `search.h`/`search.cpp`/`search_tests.cpp` changes and confirm all 6 configs are still green (no CMakeLists.txt changes were needed this time — the iterative-deepening addition only touched already-wired-in files). Say "Continue" or "Start" to proceed.

---

## 2026-08-13 — Session 7: Perft bulk-counting + NPS bench — Phase 1 complete

**What was built:** The last Phase 1 item. Phase 1 (Board Representation & Move Generation) is now fully checked off; next session starts Phase 2 (eval + search).

- `src/board/perft.h/.cpp` — added `perft_bulk()` alongside the existing `perft()`: the standard bulk-counting optimization (returns the legal move count directly at depth 1 instead of recursing to depth 0 for each leaf). Kept as a separate function rather than a flag on `perft()` — see DECISIONS.md.
- `src/bench.cpp` + a new `nightwing_bench` executable target (src/CMakeLists.txt) — runs `perft_bulk()` against startpos (depth 6) and Kiwipete (depth 5), printing nodes/time/Mnps. Informational only, not CI-asserted (NPS is machine-dependent) — correctness is what tests/perft_tests.cpp's new equivalence test covers instead.
- `tests/perft_tests.cpp` — added a cross-check test asserting `perft_bulk()` and `perft()` produce identical node counts across all six reference positions and their tested depths.
- Sample bench output on this dev machine (Release, BMI2 enabled): **startpos depth 6: 119,060,324 nodes in 0.995s (119.66 Mnps)**, **Kiwipete depth 5: 193,690,690 nodes in 1.115s (173.65 Mnps)** — roughly 3.8-4.9x faster than the equivalent plain `perft()` calls (3.75s/5.43s for the same node counts, from session 6's manual verification). A reasonable movegen throughput baseline to compare against once search-side move ordering and pruning start interacting with movegen call patterns in Phase 3+.
- Test suite grew from 84 to 85 (Release run, all green); Debug+ASan/UBSan build compiled with zero warnings, and the new bulk-vs-plain equivalence test ran clean under sanitizers.

**Bugs fixed:** None — new-function work built on an already-correct, perft-verified movegen/make-unmake layer.

**Decisions made:** Logged in DECISIONS.md — `perft_bulk()` as a separate function rather than a flag/template parameter on `perft()`.

**Next session start point:** Phase 2 begins: material-only + PSQT (piece-square table) evaluation, tapered between middlegame and endgame values. This is the first eval work, so it's a good point to also decide (and log in DECISIONS.md) the PSQT source/attribution — ARCHITECTURE.md and the system prompt both require crediting any borrowed values (e.g. if starting from a well-known public PSQT set like PeSTO's or Ethereal's as a baseline to tune from later, rather than hand-guessing values from scratch) rather than presenting them as original. Say "Continue" or "Start" to proceed.

---

## 2026-08-13 — Session 6: Perft suite, FEN parser, and two real movegen bugs found + fixed

**What was built:** The perft test suite (the last correctness gate before Phase 2's eval/search work), plus the FEN parser it needed, verified against the standard published reference node counts — and, in the process of getting there, two genuine movegen bugs were found and fixed (not hypothetical — perft mismatches don't lie). Full details on both bugs, root cause, and fix are in DECISIONS.md.

- `src/board/fen.h/.cpp` — `parse_fen()`/`to_fen()`. I/O-layer code (uses exceptions on malformed input, per ARCHITECTURE.md's exception policy), built ahead of its originally-planned Phase 2 slot because perft reference positions are conventionally given as FEN and hand-encoding Kiwipete and friends via `place_piece()` calls would have been slow and error-prone by comparison.
- `src/board/perft.h/.cpp` — plain (non-bulk-counting) `perft()`. Bulk counting is deliberately deferred to its own roadmap item — see perft.h's header comment and DECISIONS.md from session 4/5 for why non-bulk was the right scope here.
- `tests/fen_tests.cpp` — parser correctness: matches `start_position()`, round-trips through `to_fen()` for a handful of positions (including a couple of terse/edge-case FENs), en passant square parsing, halfmove/fullmove defaulting when those fields are omitted, and malformed-input error cases.
- `tests/perft_tests.cpp` — the six standard CPW reference positions (startpos through "Position 6"), each to a CI-friendly depth. All match the published node counts exactly. Deeper depths were checked by hand during development and matched too (not committed to CI, to keep runtime reasonable): **startpos to depth 6 = 119,060,324 nodes**, **Kiwipete to depth 5 = 193,690,690 nodes** — both correct, ~4-5s each in Release on this hardware. Worth promoting into the committed suite later if CI time budget allows.
- **Two bugs fixed in `src/board/movegen.cpp`** (see DECISIONS.md for full root-cause writeups): (1) `between()` was missing an alignment guard, causing it to spuriously "detect" squares between two pieces that shared neither a rank/file nor a diagonal — this silently missed a real diagonal pin. (2) `is_square_attacked()`'s pawn/knight/king terms always read the real `Position`'s piece bitboards regardless of a simulated occupancy passed in, so the original en-passant legality check (a hand-rolled occupancy edit) could still "see" a pawn that was supposed to have been simulated as captured — wrongly rejecting a legal en passant capture. Fixed by simulating en passant via a real `make_move()` on a scratch `Position` copy instead.
- Both bugs were found by writing an independent, deliberately naive second move generator (scratch/debug tooling, not committed — see DECISIONS.md's "Alternatives considered") and bisecting the game tree for the first position where it disagreed with `generate_legal_moves()`.
- Test suite grew from 70 to 84 (Release run, all green); Debug+ASan/UBSan build compiled with zero warnings, and all 7 perft tests specifically (the tests exercising both fixes most heavily, across hundreds of thousands to millions of nodes each) ran clean under sanitizers with zero errors.

**Bugs fixed:** The two above. Cause/fix/why-correct summary: (1) *cause* — `between()` assumed unaligned rook/bishop ray terms couldn't both produce false-positive overlaps; *fix* — added an explicit rank/file/diagonal alignment check before computing either term; *why correct* — verified via perft node counts matching reference exactly, plus the independent-generator bisection confirming no further move-set divergence at the positions checked. (2) *cause* — a hand-rolled occupancy-only simulation didn't update the real `Position`'s per-piece-type bitboards, which `is_square_attacked()`'s non-sliding terms read directly; *fix* — simulate via `make_move()` on a real (throwaway) `Position` copy instead; *why correct* — `make_move()` updates all piece-type bitboards, the mailbox, and occupancy together atomically, so there's no channel left for this class of mismatch, and it's the same function path the perft suite already exercises at scale.

**Decisions made:** Logged in DECISIONS.md — both bug root-causes/fixes, and the choice not to keep the independent cross-check generator as permanent CI infrastructure (kept as a debugging technique to reach for again if needed, not a standing test).

**Next session start point:** Perft bulk-counting mode — the last Phase 1 item. This adds a fast path that returns `moves.size()` at depth 1 instead of recursing to depth 0 (skipping the final ply's make/unmake), benchmarked against the plain `perft()` already in place as an early NPS (nodes-per-second) sanity check / movegen throughput baseline. Both should return identical counts for the same (position, depth) — that equality is itself a good test to add. After that, Phase 1 is complete and Phase 2 (material+PSQT eval, plain alpha-beta, iterative deepening, basic UCI loop) begins. Say "Continue" or "Start" to proceed.

---

## 2026-08-12 — Session 5: Make/unmake move, incremental Zobrist hashing

**What was built:** The last Phase 1 item before the perft suite, verified with real build+test runs (Release and Debug+ASan/UBSan) rather than only reasoned about — 78 total tests green (up from 62 last session), including 8 new make/unmake tests and cross-checks against `compute_hash()` after every make_move call.

- `src/board/board.h/.cpp` — `make_move()`/`unmake_move()`: applies/reverses a `Move` in place, handling normal moves, captures, en passant (removes the pawn on the actual captured square, not the destination), promotions (including promotion-with-capture), and castling (moves the rook too, in the same call). Updates castling rights (revoked by king moves or a piece leaving/being captured on a corner square — see DECISIONS.md), en passant target, halfmove clock (reset on pawn move or capture), fullmove number, and incrementally XOR-updates `zobrist_hash`. `UndoInfo` (new struct in board.h) carries the captured piece plus the pre-move castling rights/en passant/halfmove-clock/hash, restored verbatim on unmake. Also added `Position::remove_piece()` and `Position::move_piece()` low-level helpers alongside the existing `place_piece()`.
- `src/board/zobrist.h/.cpp` — four new public key accessors (`piece_square_key()`, `side_to_move_key()`, `castling_right_key()`, `en_passant_file_key()`) so board.cpp's incremental update can XOR against the same tables `compute_hash()` uses, without breaking the zobrist/board module boundary.
- `tests/makemove_tests.cpp` — 8 new tests: exact restoration of the start position through one make/unmake round-trip, a 5-move realistic opening sequence (1.e4 e5 2.Nf3 Nc6 3.Bb5) with a hash cross-check after every ply and full unwind back to the identical starting `Position`, capture halfmove-clock reset + restore, en passant capture + restore, promotion-with-capture + restore, castling (rook moves too) + rights update + restore, and both ways castling rights get revoked (rook moving off its corner, and an enemy rook being captured on its corner).
- Test suite grew from 62 to 70 (portable) test cases (Release run); Debug+ASan/UBSan run of all 8 new tests individually confirmed clean (no sanitizer errors).

**Bugs fixed:** None — new-file/new-function work, not a fix to existing code.

**Decisions made:** Logged in DECISIONS.md — three related choices: (1) unmake restores the saved pre-move hash verbatim rather than reversing the XOR sequence, (2) castling-rights revocation is by square (a1/h1/a8/h8) rather than by checking piece identity, (3) Zobrist keys are exposed from zobrist.h via small accessor functions rather than board.cpp reaching into zobrist.cpp's internals.

**Next session start point:** Perft test suite (`tests/perft_tests.cpp`) — the standard reference-depth node-count tests (startpos, Kiwipete, and the other common perft reference positions) that both movegen and make/unmake now exist to support. This is the natural point to also add a minimal FEN parser (`src/board/fen.h/.cpp`, not yet built) since perft reference positions are conventionally given as FEN strings — worth a quick DECISIONS.md note on whether to build that now as a small prerequisite or hand-encode the handful of reference positions via `place_piece()` the way this session's and last session's tests did. Say "Continue" or "Start" to proceed.

---

## 2026-08-12 — Session 4: Fully legal move generation + packed move encoding

**What was built:** The two remaining Phase 1 items short of make/unmake, verified with a real g++13/CMake/Catch2 build+ctest run (Release, Release+BMI2, and Debug+ASan/UBSan configurations) rather than only reasoned about.

- `src/board/move.h/.cpp` — `Move`: a `uint16_t`-packed move (6-bit from, 6-bit to, 4-bit flag), flag encoding per the standard CPW "Encoding Moves" table (quiet, double push, king/queen castle, capture, en passant, four promotion kinds x plain-or-capture). Accessors (`from()`, `to()`, `flag()`, `is_capture()`, `is_promotion()`, `promotion_piece_type()`, `is_castle()`, `is_en_passant()`, `is_double_pawn_push()`, `is_null()`) plus `to_uci()` for debug/UCI-I-O. `MoveList`: fixed-size stack array (`kMaxMoves` = 218, the theoretical maximum), no heap allocation, iterator support for range-for.
- `src/board/movegen.h/.cpp` — `generate_legal_moves()`: fully legal move generation with no separate pseudo-legal-then-filter pass. Checkers/pin detection via the "sniper" bitboard technique (enemy sliders that would reach the king through transparent own pieces, cross-checked against real occupancy for a single blocker); single check restricts non-king moves to a capture-or-block `target_mask`, double check allows only king moves; king moves are checked with the king itself removed from occupancy (avoids the "hide behind itself" bug when stepping back along a check ray); castling checks rights, empty/unattacked transit squares, and not-currently-in-check; en passant legality is resolved by direct occupancy simulation to correctly catch the horizontal-discovered-check edge case. `is_square_attacked()` exposed publicly for future king-safety eval reuse. See DECISIONS.md for the two logged technique decisions (move encoding, mask-based legality approach).
- `tests/movegen_tests.cpp` — 11 new focused unit tests (not a perft suite — that's next): start-position move count (20) and move-set spot checks, pinned-piece restriction (both the "pinned piece can't move at all" and "pinned piece can still slide within the pin ray" cases), single-check block/capture restriction, the king-step-back-along-check-ray regression case, en passant both illegal (horizontal discovered check) and legal (no discovery) cases, castling generated/blocked-by-attacked-square, and double check allowing only king moves.
- Test suite grew from 49 to 60 (portable) / 62 (BMI2) test cases.

**Bugs fixed:** None — this was new-file work, not a fix to existing code. One `-Wunused-parameter` warning caught locally before commit (`generate_king_moves` took an unused `us` parameter after refactoring) and fixed by dropping the parameter rather than shipping it.

**Decisions made:** Logged in DECISIONS.md — (1) 16-bit packed move encoding following the CPW convention, and (2) mask-based legality (pin/check masks + full-simulation for en passant) chosen over pseudo-legal-generation-then-make/unmake-filter, since make/unmake doesn't exist yet.

**Next session start point:** Make/unmake move, the last Phase 1 item before the perft test suite. This needs: incremental Zobrist hash updates on make/unmake (the "still pending" note from Session 3 — `compute_hash()` exists but incremental XOR-update doesn't yet), incremental handling of castling-rights changes (king/rook moves or rook captures revoking rights), en passant square set/clear, halfmove clock reset-on-pawn-move-or-capture, and a full unmake path (either a `Position` copy-based approach or explicit undo-info struct — worth a DECISIONS.md note on which, since ARCHITECTURE.md's "avoid heap allocation" standard argues for a fixed-size undo stack rather than a `Position` copy per ply once search is in the picture, but a straightforward copy is simplest to get right first and can be revisited before Phase 3's search loop actually needs the performance). Say "Continue" or "Start" to proceed. The perft test suite (`tests/perft_tests.cpp`) is the natural follow-on once make/unmake exists — it needs both movegen (done) and make/unmake to actually walk the tree.

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
