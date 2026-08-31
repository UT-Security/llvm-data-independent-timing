#!/usr/bin/env python3
"""Coincurve / eth-account signing: does the mod-set gate change the verdict?

Prior result (dit-coincurve-timing.md, 40 reps): the prize over blanket DIT is
only 0.64% because ~19.5% of runtime is genuinely secret, and the pass was
+2.74% (clone) to +8.35% (hoist) WORSE than always-on. This run adds the gate.

Reports both the whole workload and the raw signing region, because the trap
that bit this workload three times was a placement that showed protection in one
and not the other (trap 8). If the two do not agree arithmetically, something is
covering the wrong code.
"""
import csv, os, statistics as st, sys

CC = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(CC, "signbench_gated.csv")
rows = list(csv.DictReader(open(path)))
arms = ["baseline", "baseline2", "null", "always", "oracle",
        "pass_hoist", "pass_clone", "pass_gated", "pass_clonegated"]

def series(arm, col="total_s"):
    return {int(r["rep"]): float(r[col]) for r in rows if r["arm"] == arm}

def paired(a, b, col="total_s"):
    A, B = series(a, col), series(b, col)
    reps = sorted(set(A) & set(B))
    if not reps: return None
    rat = sorted(B[r] / A[r] for r in reps)
    return ((st.median(rat) - 1) * 100, sum(1 for x in rat if x > 1), len(rat),
            (rat[int(len(rat)*.25)] - 1) * 100, (rat[int(len(rat)*.75)] - 1) * 100)

print(f"{'arm':<18}{'median s':>10}{'CoV':>8}{'vs baseline':>13}{'reps slower':>13}{'IQR':>18}")
for a in arms:
    s = series(a)
    if not s: continue
    v = list(s.values())
    cov = st.stdev(v) / st.mean(v) * 100 if len(v) > 1 else 0
    p = paired("baseline", a)
    cell = "-" if a == "baseline" else f"{p[0]:+.2f}%"
    reps = "-" if a == "baseline" else f"{p[1]}/{p[2]}"
    iqr = "-" if a == "baseline" else f"{p[3]:+.2f}..{p[4]:+.2f}"
    print(f"{a:<18}{st.median(v):>10.3f}{cov:>7.2f}%{cell:>13}{reps:>13}{iqr:>18}")

print()
print("VS ALWAYS-ON  (negative = the placement BEATS blanket DIT)")
for a in ("oracle", "pass_hoist", "pass_clone", "pass_gated", "pass_clonegated"):
    p = paired("always", a)
    if p: print(f"  {a:<18}{p[0]:+7.2f}%   {p[1]:>2}/{p[2]}   IQR {p[3]:+.2f}..{p[4]:+.2f}")

print()
print("RAW SIGNING REGION (the secret work itself)")
for a in arms:
    s = series(a, "raw_sign_s")
    if not s: continue
    p = paired("baseline", a, "raw_sign_s")
    cell = "-" if a == "baseline" else f"{p[0]:+.2f}%"
    print(f"  {a:<18}{st.median(s.values())*1000:>9.1f} ms{cell:>10}")

print()
sf = [float(r["secret_frac"]) for r in rows if r["arm"] == "baseline"]
print(f"secret fraction (baseline): {st.median(sf):.2f}%")
ck = {r["checksum"] for r in rows}
print(f"checksums: {'IDENTICAL' if len(ck) == 1 else 'DIFFER - ' + str(ck)}")
po = [float(r["probe_const_off"]) for r in rows if r["probe_const_off"]]
pn = [float(r["probe_const_on"]) for r in rows if r["probe_const_on"]]
print(f"in-band lvp_chase control: {st.median(pn)/st.median(po):.2f}x  (must be ~4x)")
nf = paired("baseline", "baseline2")
print(f"noise floor: {nf[0]:+.2f}%  ({nf[1]}/{nf[2]})")
h = paired("baseline", "null")
print(f"harness cost: {h[0]:+.2f}%  ({h[1]}/{h[2]})")
