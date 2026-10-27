#!/usr/bin/env python3
"""Measures Lazy SMP thread scaling of the Nightwing UCI engine.

For each thread count the engine searches a fixed set of positions to a fixed
depth (`go depth N`) and the script records wall-clock time, nodes searched
and the engine's own reported nodes per second. Time-to-depth is the standard
scaling measure (CPW "Parallel Search": speedup is the 1-thread time to a
given depth divided by the N-thread time to the same depth); nodes per second
shows raw parallel throughput separately from search overhead.

Usage: thread_scaling.py <engine> [--threads 1,2,4] [--depth 14] [--hash 128]

Prints one table and a summary; exits non-zero only if the engine misbehaves
(no `bestmove`, timeout). It does not assert on the numbers: scaling depends
on the runner's real core count and neighbours, so it is a report, not a test.
"""
import argparse
import os
import subprocess
import sys
import time

# Startpos, Kiwipete, a quiet middlegame and a rook endgame: different branching
# factors and tree shapes, so one outlier position cannot define the result.
POSITIONS = [
    ("startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
    ("kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
    ("middlegame", "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 9"),
    ("rook endgame", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"),
]


def read_until(proc, prefix, timeout_s):
    """Reads engine output lines until one starts with `prefix`; returns them all."""
    lines = []
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError("engine closed its output before '%s'" % prefix)
        line = line.rstrip("\n")
        lines.append(line)
        if line.startswith(prefix):
            return lines
    raise RuntimeError("timed out waiting for '%s'" % prefix)


def last_info(lines):
    """Returns (depth, nodes, nps) from the last `info depth` line, or Nones."""
    best = (None, None, None)
    for line in lines:
        if not line.startswith("info depth"):
            continue
        tok = line.split()
        vals = {}
        for key in ("depth", "nodes", "nps"):
            if key in tok:
                try:
                    vals[key] = int(tok[tok.index(key) + 1])
                except (IndexError, ValueError):
                    pass
        if "depth" in vals:
            best = (vals.get("depth"), vals.get("nodes"), vals.get("nps"))
    return best


def run_threads(engine, threads, depth, hash_mb, per_search_timeout):
    """Searches every position at `threads` threads; returns per-position rows."""
    proc = subprocess.Popen([engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def send(cmd):
        proc.stdin.write(cmd + "\n")
        proc.stdin.flush()

    try:
        send("uci")
        read_until(proc, "uciok", 10)
        send("setoption name Threads value %d" % threads)
        send("setoption name Hash value %d" % hash_mb)
        send("setoption name OwnBook value false")
        send("isready")
        read_until(proc, "readyok", 10)
        rows = []
        for name, fen in POSITIONS:
            send("ucinewgame")
            send("isready")
            read_until(proc, "readyok", 10)
            send("position fen " + fen)
            start = time.monotonic()
            send("go depth %d" % depth)
            lines = read_until(proc, "bestmove", per_search_timeout)
            elapsed = time.monotonic() - start
            d, nodes, nps = last_info(lines)
            rows.append((name, elapsed, d, nodes, nps))
        send("quit")
        return rows
    finally:
        try:
            proc.kill()
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("--threads", default="1,2,4")
    ap.add_argument("--depth", type=int, default=14)
    ap.add_argument("--hash", type=int, default=128)
    ap.add_argument("--timeout", type=int, default=600, help="seconds per search")
    args = ap.parse_args()
    counts = [int(x) for x in args.threads.split(",")]

    print("logical CPUs reported by the OS: %s" % os.cpu_count())
    print("depth %d, hash %d MB, positions: %s" % (args.depth, args.hash,
                                                   ", ".join(n for n, _ in POSITIONS)))
    totals = {}
    for t in counts:
        rows = run_threads(args.engine, t, args.depth, args.hash, args.timeout)
        print("\nThreads = %d" % t)
        print("  %-14s %9s %6s %14s %12s" % ("position", "time s", "depth", "nodes", "nps"))
        for name, el, d, nodes, nps in rows:
            print("  %-14s %9.2f %6s %14s %12s" % (name, el, d, nodes, nps))
        total_time = sum(r[1] for r in rows)
        total_nodes = sum(r[3] or 0 for r in rows)
        totals[t] = (total_time, total_nodes)
        print("  total time %.2f s, total nodes %d, aggregate nodes/s %.0f" %
              (total_time, total_nodes, total_nodes / total_time if total_time else 0))

    base_t = counts[0]
    base_time, base_nodes = totals[base_t]
    print("\nSummary (relative to %d thread%s):" % (base_t, "" if base_t == 1 else "s"))
    print("  %-8s %12s %14s %14s" % ("threads", "time-to-depth", "speedup", "main-thr n/s"))
    for t in counts:
        tm, nd = totals[t]
        speed = base_time / tm if tm else 0
        nps_ratio = (nd / tm) / (base_nodes / base_time) if tm and base_time and base_nodes else 0
        print("  %-8d %11.2fs %13.2fx %13.2fx" % (t, tm, speed, nps_ratio))
    print("\nNote: only the time-to-depth speedup is a reliable scaling figure. The node and")
    print("nodes-per-second columns come from the engine's last per-iteration `info` line,")
    print("which can exclude helper-thread nodes (they are folded in after the search),")
    print("so they are not total throughput at more than one thread.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
