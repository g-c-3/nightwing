#!/usr/bin/env python3
"""Clock-simulation self-play (Session 224): measures per-move time use against the soft/hard budget.
Usage: clock_sim.py <engine-binary> <base_ms> <inc_ms> <max_ply> <out.json>
Allocation mirrored from src/uci/time_budget.h (remaining/20 + inc, cap remaining/2, hard = 3x soft)."""
import subprocess, sys, time, json, statistics as st

BIN = sys.argv[1]
BASE_MS, INC_MS = int(sys.argv[2]), int(sys.argv[3])
MAXPLY = int(sys.argv[4])
OUT = sys.argv[5]

OPENINGS = [
 "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7",
 "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8",
 "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6",
 "c2c4 g8f6 b1c3 e7e6 e2e4 d7d5 e4e5 f6d7 c4d5 e6d5",
 "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8",
 "e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 b4c3",
]

def alloc(rem, inc, mtg=0):
    if rem <= 0: return 50, 50
    cap = rem // 2
    soft = min(rem // (mtg if mtg > 0 else 20) + inc, cap); soft = max(soft, 1)
    hard = min(soft * 3, cap); hard = max(hard, soft)
    return soft, hard

class Eng:
    def __init__(self):
        self.p = subprocess.Popen([BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.send("uci"); self.wait("uciok")
        self.send("setoption name OwnBook value false")
        self.send("isready"); self.wait("readyok")
    def send(self, s): self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def wait(self, tok):
        last = None
        while True:
            l = self.p.stdout.readline()
            if not l: raise RuntimeError("engine died")
            if l.startswith("info depth"): last = l
            if l.startswith(tok): return l.strip(), last
    def quit(self):
        try: self.send("quit"); self.p.wait(timeout=5)
        except Exception: self.p.kill()

records, games = [], []
for gi, op in enumerate(OPENINGS):
    for flip in (0, 1):
        engs = [Eng(), Eng()]
        for e in engs: e.send("ucinewgame"); e.send("isready"); e.wait("readyok")
        moves = op.split()
        clock = [BASE_MS, BASE_MS]  # white, black
        flag, ply, result = None, len(moves), "maxply"
        while ply < MAXPLY:
            side = ply % 2  # 0 = white to move
            e = engs[side ^ flip]
            e.send("position startpos moves " + " ".join(moves))
            soft, hard = alloc(clock[side], INC_MS)
            t0 = time.perf_counter()
            e.send(f"go wtime {clock[0]} btime {clock[1]} winc {INC_MS} binc {INC_MS}")
            bm, info = e.wait("bestmove")
            el = (time.perf_counter() - t0) * 1000
            mv = bm.split()[1]
            depth = None
            if info:
                parts = info.split()
                depth = int(parts[parts.index("depth") + 1])
            clock_before = clock[side]
            clock[side] = clock[side] - el + INC_MS
            records.append(dict(game=gi*2+flip, ply=ply, side=side, clock_before=clock_before,
                                soft=soft, hard=hard, elapsed=el, depth=depth))
            if clock[side] <= 0 and clock_before - el <= 0:
                flag = side; result = "flag"; break
            if mv in ("0000", "(none)"): result = "nomove"; break
            moves.append(mv); ply += 1
        games.append(dict(game=gi*2+flip, plies=ply, result=result, flag=flag, clocks=clock[:]))
        for e in engs: e.quit()
        print(f"game {gi*2+flip} done: {result}, plies {ply}, clocks {[round(c) for c in clock]}", flush=True)

json.dump(dict(records=records, games=games), open(OUT, "w"))
rat = [r["elapsed"] / r["soft"] for r in records if r["soft"] > 0]
hr  = [r["elapsed"] / r["hard"] for r in records if r["hard"] > 0]
rat_s = sorted(rat)
def pct(a, q): return a[min(len(a)-1, int(q*len(a)))]
print(f"\nTC {BASE_MS/1000:g}s+{INC_MS/1000:g}s  moves measured: {len(records)}")
print(f"time/soft  median {st.median(rat):.2f}  mean {st.mean(rat):.2f}  p90 {pct(rat_s,.9):.2f}  max {max(rat):.2f}")
print(f"share of moves using > soft: {sum(x>1.0 for x in rat)/len(rat):.0%};  >= 90% of hard: {sum(x>=.9 for x in hr)/len(hr):.0%};  > hard: {sum(x>1.0 for x in hr)/len(hr):.0%}")
print(f"overrun beyond hard (ms): max {max(r['elapsed']-r['hard'] for r in records):.0f}")
mins = [min(g['clocks']) for g in games]
print(f"flags: {sum(1 for g in games if g['result']=='flag')}/{len(games)}; lowest final clock (ms): {min(mins):.0f}")
