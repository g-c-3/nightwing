#pragma once
// src/uci/uci.h
//
// Minimal UCI (Universal Chess Interface) loop — Phase 2's fourth
// ROADMAP.md item. Reads commands from an input stream and writes
// responses to an output stream, per the UCI protocol. This is
// deliberately a "basic" loop: enough to be driven by a GUI or a
// self-play script (the very next ROADMAP.md item — "engine can play a
// full legal game against itself via UCI"), not the full protocol —
// see uci.cpp's header comment and DECISIONS.md for what's out of scope
// this phase (setoption/Hash/Threads, true asynchronous `go
// infinite`/`stop`, pondering) and why.

#include <iosfwd>

namespace nightwing::uci {

/// Runs the UCI loop: reads commands from `in` one line at a time,
/// writes responses to `out`, until a `quit` command or end-of-input.
/// Takes the streams explicitly (rather than always using std::cin/
/// std::cout) so tests/uci_tests.cpp can drive it with
/// std::istringstream/std::ostringstream instead of real stdin/stdout;
/// main.cpp calls this with std::cin/std::cout for the real engine.
///
/// Precondition: init_masks()/init_magic_bitboards()/init_zobrist_keys()
/// must already have been called — this loop's `position` and `go`
/// commands exercise movegen/search, both of which require it (see
/// board/movegen.h).
void run(std::istream& in, std::ostream& out);

/// Runs the fixed-position, fixed-depth bench (ROADMAP.md Phase 8,
/// "`bench` command" — src/bench_positions.h has the shared position
/// set/depth this shares with tests/bench_tests.cpp's own internal
/// regression tracking) and writes a fishtest/OpenBench-style summary
/// to `out`, ending with a "Nodes searched : N" line that external
/// tooling greps for to verify build correctness/reproducibility across
/// commits. Called from two places: run()'s own `bench` command
/// (typed at the interactive UCI prompt), and directly from main.cpp
/// when the engine is invoked as `./nightwing bench` on the command
/// line (the more common way fishtest/OpenBench actually invoke it —
/// see main.cpp's own comment) — both produce byte-for-byte identical
/// output for the same build, since both just call this one function.
///
/// Precondition: same as run() above — init_masks()/
/// init_magic_bitboards()/init_zobrist_keys() must already have been
/// called.
///
/// Deliberately single-threaded and Hash-default (search_fixed_depth()'s
/// own defaults, unconditionally — not configurable via this function's
/// own parameters): reproducibility across machines/commits is the
/// entire point of a bench command a testing harness diffs output
/// against, and both thread count and hash size can otherwise perturb
/// node counts (Lazy SMP's own inherent nondeterminism; a differently-
/// sized TT changes replacement-eviction timing) — see docs/DECISIONS.md
/// for the full reasoning and what's deliberately NOT configurable here
/// as a result.
void run_bench(std::ostream& out);

} // namespace nightwing::uci
