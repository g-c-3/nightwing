#!/usr/bin/env python3
"""A/B speed comparison of two Nightwing engine binaries on `bench`.

Runs the UCI `bench <depth>` command of binary A and binary B alternately
(A, B, A, B, ...) so slow drift of the machine hits both equally, measures
each run's user CPU time, and reports the minimum, the median and the
median ratio B/A. Both binaries must report the same `Nodes searched` count
(it is a pure-speed comparison; a different count means different behaviour
and the script exits non-zero).

Usage: ab_bench.py <binary A> <binary B> [--runs 9] [--depth 10]

A median ratio below 1.0 means B is faster. Differences within about 3% are
inside typical shared-runner noise.
"""
import argparse
import resource
import subprocess
import sys


def run_once(binary, depth):
    """Returns (user CPU seconds, reported node count) for one bench run."""
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    proc = subprocess.run([binary], input=("bench %d\nquit\n" % depth).encode(),
                          capture_output=True, check=True)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    nodes = None
    for line in proc.stdout.decode().splitlines():
        if "Nodes searched" in line:
            nodes = line.split(":")[1].strip()
    if nodes is None:
        raise RuntimeError("%s printed no 'Nodes searched' line" % binary)
    return after.ru_utime - before.ru_utime, nodes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary_a")
    ap.add_argument("binary_b")
    ap.add_argument("--runs", type=int, default=9)
    ap.add_argument("--depth", type=int, default=10)
    args = ap.parse_args()

    times = {"A": [], "B": []}
    nodes = {}
    for _ in range(args.runs):
        for key, binary in (("A", args.binary_a), ("B", args.binary_b)):
            t, n = run_once(binary, args.depth)
            times[key].append(t)
            nodes.setdefault(key, n)
            if n != nodes[key]:
                print("binary %s changed its node count between runs" % key)
                return 1
    print("bench %d, %d alternating runs, user CPU seconds" % (args.depth, args.runs))
    med = {}
    for key, label in (("A", args.binary_a), ("B", args.binary_b)):
        s = sorted(times[key])
        med[key] = s[len(s) // 2]
        print("  %s %-40s min %.2f  median %.2f  nodes %s" %
              (key, label, s[0], med[key], nodes[key]))
    print("median ratio B/A: %.3f  (below 1.000 means B is faster)" % (med["B"] / med["A"]))
    if nodes["A"] != nodes["B"]:
        print("NODE COUNTS DIFFER between A and B: the binaries do not behave identically")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
