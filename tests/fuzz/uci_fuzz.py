#!/usr/bin/env python3
"""Short multi-threaded UCI fuzz driver for the real `nightwing` binary.

Purpose: CI runs this against sanitizer builds (ASan/UBSan and TSan, see
.github/workflows/ci.yml, job `sanitizer-fuzz`) so the engine as shipped is
exercised end to end -- background `go` threads, Lazy SMP helpers, ponder,
`stop`, `ucinewgame`, option changes -- in ways the unit tests only cover
piecemeal. It is a robustness check, not a strength or correctness test.

Behavior: for a fixed wall-clock budget it plays randomized games against
itself over the UCI protocol. Each iteration picks a random action: a
`go` with a random limit (depth, nodes, movetime, clock, infinite+stop,
ponder+ponderhit/stop), an interrupting command sent while a search is in
flight (isready, position, ucinewgame), or an option change while idle
(Threads, Hash, MultiPV, Skill Level, Contempt, OwnBook, Ponder, Move
Overhead). The engine's own bestmove is played onward, so positions stay
legal without a second move generator. It ends with a `quit` sent during
`go infinite` to race shutdown against the search threads.

Failure (exit status 1, with the seed and the last commands printed):
  - the engine dies, or does not answer within the timeout (hang/deadlock);
  - a `bestmove` is malformed;
  - stderr contains a sanitizer report (AddressSanitizer, UndefinedBehavior
    runtime error, ThreadSanitizer, LeakSanitizer);
  - the engine exits with a non-zero status after `quit`.

Reproduce a failure with the printed seed: `--seed N` (thread timing still
varies, so a race may not reproduce every time).

Usage: uci_fuzz.py --engine build/src/nightwing [--seconds 60] [--seed N]
                   [--max-threads 4]
"""

import argparse
import collections
import queue
import random
import re
import subprocess
import sys
import threading
import time

MOVE_RE = re.compile(r"^[a-h][1-8][a-h][1-8][qrbn]?$")
SAN_MARKERS = (
    "Sanitizer",
    "runtime error:",
    "ERROR: AddressSanitizer",
    "WARNING: ThreadSanitizer",
    "ERROR: LeakSanitizer",
)
# Start positions: the standard one plus a few with castling, en passant,
# promotion and sparse-endgame content.
START_FENS = [
    None,  # startpos
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "8/8/8/8/8/6k1/4q3/6K1 w - - 0 1",
    "7k/7P/8/6K1/4B3/8/8/8 w - - 0 1",
]
RESPONSE_TIMEOUT = 90.0  # seconds; generous for slow sanitizer builds


class FuzzFailure(Exception):
    """Raised when the engine misbehaves."""


class Engine:
    """A UCI engine subprocess with non-blocking line reads."""

    def __init__(self, path):
        self.proc = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.lines = queue.Queue()
        self.err_chunks = []
        self.log = collections.deque(maxlen=60)
        # `bestmove` lines seen while waiting for some other prefix (a search
        # can finish before the readyok that follows it).
        self.pending_bestmoves = []
        threading.Thread(target=self._read_out, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()

    def _read_out(self):
        for line in self.proc.stdout:
            self.lines.put(line.rstrip("\n"))
        self.lines.put(None)  # EOF marker

    def _read_err(self):
        for line in self.proc.stderr:
            self.err_chunks.append(line)

    def stderr_text(self):
        return "".join(self.err_chunks)

    def check_stderr(self):
        text = self.stderr_text()
        if any(m in text for m in SAN_MARKERS):
            raise FuzzFailure("sanitizer report on stderr:\n" + text[-6000:])

    def send(self, cmd):
        self.log.append(">> " + cmd)
        try:
            self.proc.stdin.write(cmd + "\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            self.check_stderr()
            raise FuzzFailure("engine closed its input unexpectedly")

    def wait_for(self, prefix, timeout=RESPONSE_TIMEOUT):
        """Read lines until one starts with `prefix`; return that line."""
        deadline = time.time() + timeout
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                self.check_stderr()
                raise FuzzFailure("timeout (%.0fs) waiting for '%s'" % (timeout, prefix))
            try:
                line = self.lines.get(timeout=min(remaining, 1.0))
            except queue.Empty:
                self.check_stderr()
                continue
            if line is None:
                self.check_stderr()
                raise FuzzFailure("engine exited unexpectedly (rc=%s)" % self.proc.poll())
            self.log.append("<< " + line[:160])
            if line.startswith("bestmove") and prefix != "bestmove":
                self.pending_bestmoves.append(line)
            if line.startswith(prefix):
                return line

    def sync(self):
        """isready/readyok round trip; discards any other output."""
        self.send("isready")
        self.wait_for("readyok")

    def quit(self, timeout=60.0):
        self.send("quit")
        try:
            rc = self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            raise FuzzFailure("engine did not exit within %.0fs of quit" % timeout)
        time.sleep(0.2)  # let the stderr reader drain
        self.check_stderr()
        if rc != 0:
            raise FuzzFailure("engine exited with status %d after quit" % rc)


class Fuzzer:
    def __init__(self, engine, rng, max_threads):
        self.e = engine
        self.rng = rng
        self.max_threads = max_threads
        self.base = None  # None = startpos, else a FEN
        self.moves = []
        self.games = 0
        self.searches = 0

    def position_cmd(self):
        base = "startpos" if self.base is None else "fen " + self.base
        cmd = "position " + base
        if self.moves:
            cmd += " moves " + " ".join(self.moves)
        return cmd

    def new_game(self):
        self.base = self.rng.choice(START_FENS)
        self.moves = []
        self.games += 1
        if self.rng.random() < 0.5:
            self.e.send("ucinewgame")
            self.e.sync()

    def set_position(self):
        self.e.send(self.position_cmd())

    def random_option(self):
        r, e = self.rng, self.e
        name, value = r.choice([
            ("Threads", r.randint(1, self.max_threads)),
            ("Hash", r.choice([1, 2, 4, 16, 32])),
            ("MultiPV", r.randint(1, 3)),
            ("Skill Level", r.randint(0, 20)),
            ("Contempt", r.randint(-50, 50)),
            ("OwnBook", r.choice(["true", "false"])),
            ("Ponder", r.choice(["true", "false"])),
            ("Move Overhead", r.choice([0, 10, 50])),
        ])
        e.send("setoption name %s value %s" % (name, value))
        e.sync()

    def finish_search(self):
        """Wait for the bestmove, validate it, and play it on."""
        if self.e.pending_bestmoves:
            line = self.e.pending_bestmoves.pop(0)
        else:
            line = self.e.wait_for("bestmove")
        self.handle_bestmove(line)

    def handle_bestmove(self, line):
        self.searches += 1
        parts = line.split()
        move = parts[1] if len(parts) > 1 else ""
        if move in ("0000", "(none)"):
            self.new_game()  # game over (mate/stalemate)
            return
        if not MOVE_RE.match(move):
            raise FuzzFailure("malformed bestmove line: %r" % line)
        self.moves.append(move)
        if len(self.moves) > 120:
            self.new_game()

    def go_limited(self):
        r = self.rng
        limit = r.choice([
            "depth %d" % r.randint(1, 4),
            "nodes %d" % r.randint(200, 5000),
            "movetime %d" % r.randint(10, 150),
            "wtime %d btime %d winc 50 binc 50" % (r.randint(200, 2000), r.randint(200, 2000)),
        ])
        self.set_position()
        self.e.send("go " + limit)
        # Occasionally interrupt a search in flight with an out-of-protocol
        # command (the engine abandons the search, so resynchronize).
        if r.random() < 0.15:
            time.sleep(r.uniform(0, 0.05))
            kind = r.choice(["isready", "position", "ucinewgame", "stop"])
            if kind == "isready":
                self.e.pending_bestmoves.clear()
                self.e.send("isready")
                self.e.wait_for("readyok")
                self.finish_search()  # may already have arrived before readyok
            elif kind == "stop":
                self.e.send("stop")
                self.finish_search()
            else:
                if kind == "position":
                    self.new_game()
                    self.set_position()
                else:
                    self.e.send("ucinewgame")
                    self.moves = []
                self.e.sync()  # abandoned search may print no bestmove
                self.e.pending_bestmoves.clear()
        else:
            self.finish_search()

    def go_infinite_stop(self):
        self.set_position()
        self.e.send("go infinite")
        time.sleep(self.rng.uniform(0.01, 0.3))
        self.e.send("stop")
        self.finish_search()

    def go_ponder(self):
        r = self.rng
        self.set_position()
        self.e.send("go ponder wtime %d btime %d" % (r.randint(300, 3000), r.randint(300, 3000)))
        time.sleep(r.uniform(0.0, 0.2))
        if r.random() < 0.5:
            self.e.send("ponderhit")
        else:
            self.e.send("stop")
        self.finish_search()

    def step(self):
        r = self.rng.random()
        if r < 0.50:
            self.go_limited()
        elif r < 0.65:
            self.go_infinite_stop()
        elif r < 0.78:
            self.go_ponder()
        elif r < 0.90:
            self.random_option()
        else:
            self.new_game()

    def final_quit_race(self):
        """quit while a multi-threaded `go infinite` is running."""
        self.e.send("setoption name Threads value %d" % self.max_threads)
        self.e.sync()
        self.set_position()
        self.e.send("go infinite")
        time.sleep(self.rng.uniform(0.05, 0.3))
        self.e.quit()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--engine", required=True, help="path to the nightwing binary")
    ap.add_argument("--seconds", type=float, default=60.0, help="fuzz time budget")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--max-threads", type=int, default=4)
    args = ap.parse_args()

    seed = args.seed if args.seed is not None else random.randrange(1, 2**31)
    print("uci_fuzz: seed=%d seconds=%.0f max_threads=%d" % (seed, args.seconds, args.max_threads),
          flush=True)
    rng = random.Random(seed)
    engine = Engine(args.engine)
    fuzzer = Fuzzer(engine, rng, args.max_threads)
    try:
        engine.send("uci")
        engine.wait_for("uciok")
        engine.sync()
        fuzzer.new_game()
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            fuzzer.step()
            engine.check_stderr()
        fuzzer.final_quit_race()
    except FuzzFailure as exc:
        print("uci_fuzz: FAILED (seed=%d): %s" % (seed, exc), file=sys.stderr)
        print("--- last commands/responses ---", file=sys.stderr)
        print("\n".join(engine.log), file=sys.stderr)
        if engine.proc.poll() is None:
            engine.proc.kill()
        return 1
    print("uci_fuzz: OK (seed=%d, %d games, %d searches)" % (seed, fuzzer.games, fuzzer.searches))
    return 0


if __name__ == "__main__":
    sys.exit(main())
