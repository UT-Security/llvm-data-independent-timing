#!/usr/bin/env python3
"""Per-workload DIT sensitivity inside the two winning interpreters.

The sweep gives one number per host. This splits it by workload to test WHY.
dit-headroom-needs-serial-chains predicts the cost tracks serial
load-to-address chains (object graphs, attribute lookup, GC tracing) and NOT
raw arithmetic, however slow that arithmetic is.

  CPython  richards, go     - object graph, method dispatch  -> predict HIGH
           float, nbody     - numeric over floats/lists      -> predict LOW
  Lua      binary_trees     - allocation + tree walk + GC     -> predict HIGH
           fannkuch         - integer permutation, flat array -> predict LOW
           spectralnorm     - float, flat arrays, parallel    -> predict ~ZERO

If cost tracks the pointer-chasing column rather than the runtime column, the
serial-chain requirement is confirmed in real interpreters.
"""
import csv
import os
import re
import subprocess
import sys
import time

SW = os.path.dirname(os.path.abspath(__file__))
PY = os.path.join(SW, "src", "cpython", "python.exe")
LUA = os.path.join(SW, "src", "lua-5.4.7", "src", "lua")
ARMS = [("null", os.path.join(SW, "bin", "dit_off.dylib")),
        ("dit", os.path.join(SW, "bin", "dit_on.dylib"))]

LUA_WORK = [("binary_trees", "17"), ("fannkuch", "10"), ("spectralnorm", "2000")]

reps = int(sys.argv[1]) if len(sys.argv) > 1 else 15
burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 2
out = os.path.join(SW, "out", "mechanism.csv")


def env_for(dylib):
    e = dict(os.environ)
    e.pop("DYLD_INSERT_LIBRARIES", None)
    if dylib:
        e["DYLD_INSERT_LIBRARIES"] = dylib
    return e


with open(out, "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["rep", "host", "bench", "arm", "seconds"])
    for r in range(reps + burnin):
        keep = r >= burnin
        # CPython: one process reports all four, timed internally.
        for arm, dylib in ARMS:
            p = subprocess.run([PY, "driver.py", "all"], cwd=os.path.join(SW, "py"),
                               env=env_for(dylib), capture_output=True, text=True)
            if p.returncode != 0:
                print("  !! cpython", arm, p.stderr[:160], flush=True)
                continue
            for line in p.stdout.split("\n"):
                m = re.match(r"PYBENCH (\S+) ([0-9.]+)", line)
                if m and keep:
                    w.writerow([r - burnin, "cpython", m.group(1), arm, m.group(2)])
        # Lua: one process per workload, timed externally.
        for bench, n in LUA_WORK:
            for arm, dylib in ARMS:
                t0 = time.perf_counter()
                p = subprocess.run([LUA, "bench2.lua", bench, n],
                                   cwd=os.path.join(SW, "lua"),
                                   env=env_for(dylib), capture_output=True, text=True)
                t1 = time.perf_counter()
                if p.returncode != 0:
                    print("  !! lua", bench, arm, p.stderr[:160], flush=True)
                    continue
                if keep:
                    w.writerow([r - burnin, "lua", bench, arm, f"{t1 - t0:.6f}"])
        fh.flush()
        print(f"rep {r - burnin if keep else 'burnin'} done", flush=True)
print("WROTE", out)
