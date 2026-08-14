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

} // namespace nightwing::uci
