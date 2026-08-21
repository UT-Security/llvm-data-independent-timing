#!/usr/bin/env python3
"""Summarize the DIT screening sweep, with the gates from dit-measurement-traps."""
import csv
import os
import statistics as st
import sys

SW = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SW, "out", "sweep.csv")

rows = list(csv.DictReader(open(path)))
d = {}
ck = {}
probe = {}
for r in rows:
    d.setdefault((r["host"], r["arm"]), {})[int(r["rep"])] = float(r["seconds"])
    ck.setdefault((r["host"], r["arm"]), set()).add(r["stdout_tail"])
    if r["host"] == "probe" and r["probe_const"]:
        probe.setdefault(r["arm"], []).append(
            (float(r["probe_const"]), float(r["probe_perm"]), int(r["probe_dit"])))

hosts = []
for (h, a) in d:
    if h not in hosts:
        hosts.append(h)


def paired(h, a1, a2):
    x, y = d.get((h, a1), {}), d.get((h, a2), {})
    reps = sorted(set(x) & set(y))
    if not reps:
        return None
    pr = [y[i] / x[i] for i in reps]
    return pr


print("=" * 96)
print("GATES")
print("=" * 96)
for arm in ("base", "null", "dit"):
    if arm in probe:
        c = st.median(p[0] for p in probe[arm])
        pm = st.median(p[1] for p in probe[arm])
        bits = {p[2] for p in probe[arm]}
        print(f"  probe/{arm:5s} const={c:.4f} ns/hop  perm={pm:.4f}  const/perm={c/pm:.4f}  PSTATE.DIT={bits}")
if "null" in probe and "dit" in probe:
    cn = st.median(p[0] for p in probe["null"])
    cd = st.median(p[0] for p in probe["dit"])
    pd = st.median(p[1] for p in probe["dit"])
    print(f"\n  [1] positive control null->dit const chase : {cd/cn:.3f}x   (need ~4.0)  "
          f"{'PASS' if 3.5 < cd/cn < 4.5 else 'FAIL'}")
    print(f"  [2] robust gate const/perm under DIT      : {cd/pd:.4f}   (need .997-1.003)  "
          f"{'PASS' if 0.997 <= cd/pd <= 1.003 else 'FAIL'}")

print()
print("=" * 96)
print("ALWAYS-ON DIT COST BY HOST   (null -> dit, same binary, DIT set at process load)")
print("=" * 96)
print(f"{'host':9s} {'base s':>8s} {'null s':>8s} {'dit s':>8s} | {'harness':>9s} | "
      f"{'DIT COST':>9s} {'slower':>7s} {'CoV':>6s} {'out':>4s}")
results = []
for h in hosts:
    if h == "probe":
        continue
    b = d.get((h, "base"), {})
    n = d.get((h, "null"), {})
    t = d.get((h, "dit"), {})
    if not (b and n and t):
        continue
    pr_dit = paired(h, "null", "dit")
    pr_harn = paired(h, "base", "null")
    mb, mn, mt = st.median(b.values()), st.median(n.values()), st.median(t.values())
    cost = (st.median(pr_dit) - 1) * 100
    harn = (st.median(pr_harn) - 1) * 100
    slower = sum(1 for p in pr_dit if p > 1)
    cov = st.stdev(n.values()) / st.mean(n.values()) * 100 if len(n) > 1 else 0
    stable = len(ck[(h, "dit")] | ck[(h, "null")] | ck[(h, "base")]) == 1
    print(f"{h:9s} {mb:8.3f} {mn:8.3f} {mt:8.3f} | {harn:+8.2f}% | "
          f"{cost:+8.2f}% {slower:3d}/{len(pr_dit):<3d} {cov:5.2f}% {'ok' if stable else 'VARY':>4s}")
    results.append((h, cost, slower, len(pr_dit)))

print()
print("  [3] no DIT ratio below 1.00x:",
      "PASS" if all(c > 0 for _, c, _, _ in results) else
      "FAIL -> " + ", ".join(f"{h} {c:+.2f}%" for h, c, _, _ in results if c <= 0))
print()
print("RANKED (largest prize first):")
for h, c, s, n in sorted(results, key=lambda x: -x[1]):
    verdict = "LIVE" if c >= 1.0 else ("marginal" if c >= 0.5 else "dead")
    print(f"  {h:9s} {c:+6.2f}%   {s}/{n} reps slower   {verdict}")
print("\n  'out' column: does every arm produce identical stdout tail (correctness/stability)")
