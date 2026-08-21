#!/usr/bin/env python3
"""Paired-rep analysis of the Bitcoin Core gated run.

Paired: every arm runs inside the same rep, arm order rotated per rep, so a
thermal or scheduler drift that moves one arm moves them all. The headline is
the per-rep ratio, not the ratio of medians - and `reps slower` counts how many
of the paired comparisons went the same way, which is the part that says whether
a sub-1% median is real.
"""
import csv, os, statistics as st, sys

SW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SW, "btc", "btc_gated.csv")
rows = list(csv.DictReader(open(path)))
benches = sorted({r["bench"] for r in rows})
arms = ["baseline", "baseline2", "null", "always", "pass_hoist", "pass_gated"]

def series(bench, arm):
    d = {}
    for r in rows:
        if r["bench"] == bench and r["arm"] == arm:
            d[int(r["rep"])] = float(r["ns_per_op"])
    return d

def paired(bench, a, b):
    """median per-rep ratio b/a, and how many reps had b slower than a"""
    A, B = series(bench, a), series(bench, b)
    reps = sorted(set(A) & set(B))
    if not reps:
        return None
    rat = [B[r] / A[r] for r in reps]
    slower = sum(1 for x in rat if x > 1.0)
    q = sorted(rat)
    lo = q[int(len(q) * .25)]; hi = q[int(len(q) * .75)]
    return (st.median(rat) - 1) * 100, slower, len(reps), (lo - 1) * 100, (hi - 1) * 100

print(f"{'benchmark':<42}" + "".join(f"{a:>13}" for a in arms[2:]))
print(f"{'  (median % vs baseline, paired per rep)':<42}")
for b in benches:
    base = series(b, "baseline")
    if not base: continue
    cells = []
    for a in arms[2:]:
        p = paired(b, "baseline", a)
        cells.append("-" if p is None else f"{p[0]:+.2f}%")
    cov = st.stdev(base.values()) / st.mean(base.values()) * 100 if len(base) > 1 else 0
    print(f"{b[:40]:<42}" + "".join(f"{c:>13}" for c in cells) + f"   [baseline CoV {cov:.2f}%]")

print()
print("HEAD TO HEAD vs always-on (negative = the pass BEATS blanket DIT)")
print(f"{'benchmark':<42}{'pass_hoist':>22}{'pass_gated':>22}")
for b in benches:
    out = []
    for a in ("pass_hoist", "pass_gated"):
        p = paired(b, "always", a)
        out.append("-" if p is None else f"{p[0]:+.2f}% ({p[1]}/{p[2]})")
    print(f"{b[:40]:<42}" + "".join(f"{c:>22}" for c in out))

print()
print("GATED vs HOIST (the flag's own effect)")
for b in benches:
    p = paired(b, "pass_hoist", "pass_gated")
    if p:
        print(f"  {b[:40]:<42}{p[0]:+.2f}%  ({p[1]}/{p[2]} slower, IQR {p[3]:+.2f}..{p[4]:+.2f})")

print()
nf = [paired(b, "baseline", "baseline2") for b in benches]
nf = [x for x in nf if x]
print(f"noise floor (baseline vs baseline2): median {st.median([x[0] for x in nf]):+.2f}%, "
      f"worst {max(abs(x[0]) for x in nf):.2f}%")
h = paired(benches[0], "baseline", "null")
print(f"harness cost (null arm, {benches[0]}): {h[0]:+.2f}%  ({h[1]}/{h[2]})")
po = [float(r["probe_off"]) for r in rows if r["probe_off"]]
pn = [float(r["probe_on"]) for r in rows if r["probe_on"]]
if po and pn:
    print(f"in-band lvp_chase control: {st.median(pn)/st.median(po):.2f}x  (must be ~4x)")
