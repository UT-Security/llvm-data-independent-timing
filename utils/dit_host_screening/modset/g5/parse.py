#!/usr/bin/env python3
"""Parse the FIRST stats dump only (dit-measurement-traps trap 9: gem5 appends
every dump to stats.txt and the later ones cover the wrong region)."""
import re, sys, os

WANT = ["simInsts", "simSeconds", "system.cpu.numCycles", "board.processor.cores.core.numCycles",
        "compSimplifier.ditSuppressed", "valuePredictor.ditTaggedSet", "ditSwitches", "ditSet"]

def first_dump(path):
    out, started = {}, False
    with open(path) as f:
        for line in f:
            if line.startswith("---------- Begin"):
                if started: break
                started = True; continue
            if line.startswith("---------- End"):
                break
            m = re.match(r"^(\S+)\s+([-\d.eE+naninf]+)", line)
            if m:
                try: out[m.group(1)] = float(m.group(2))
                except ValueError: pass
    return out

def pick(d, needle):
    for k, v in d.items():
        if k.endswith(needle) or needle in k:
            return k, v
    return None, None

tags = sys.argv[1:]
base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
rows = []
for t in tags:
    p = os.path.join(base, t, "stats.txt")
    if not os.path.exists(p):
        rows.append((t, None)); continue
    rows.append((t, first_dump(p)))

hdr = ["simInsts", "numCycles", "ditSuppressed", "ditTaggedSet"]
print(f"{'arm':<14}" + "".join(f"{h:>18}" for h in hdr))
for t, d in rows:
    if d is None:
        print(f"{t:<14}{'(no stats)':>18}"); continue
    vals = []
    for h in hdr:
        _, v = pick(d, h)
        vals.append("-" if v is None else f"{v:,.0f}")
    print(f"{t:<14}" + "".join(f"{v:>18}" for v in vals))
