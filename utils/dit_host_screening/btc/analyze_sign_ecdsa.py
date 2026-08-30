#!/usr/bin/env python3
"""Paired-rep analysis of the SignTransactionECDSA run.

Paired: every arm runs inside the same rep and arm order rotates per rep, so a
thermal or scheduler drift moves all arms together. The headline is the median
per-rep RATIO, not the ratio of medians, and `n/N` counts how many of the paired
comparisons went the same way -- which is what decides whether a sub-1% median
means anything.

The decomposition that matters here, because it is what a gem5 comparison stands
on:

    pass - passnop   the switches themselves (MSR DIT vs HINT #0 at the same
                     addresses, identical placement, identical layout)
    passnop - base   everything else the pass did: extra instructions, register
                     pressure, changed code layout

If pass-passnop is most of the pass's cost, a machine with a cheaper switch
recovers it. If passnop-base is, no switch model can.
"""
import csv, os, statistics as st, sys

HERE = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "sign_ecdsa.csv")
rows = list(csv.DictReader(open(path)))
if not rows:
    sys.exit("no rows")
bench = rows[0]["bench"]
present = {r["arm"] for r in rows}


def series(arm):
    return {int(r["rep"]): float(r["ns_per_op"]) for r in rows if r["arm"] == arm}


def paired(a, b):
    """median per-rep ratio b/a as a percent, reps where b was slower, IQR."""
    A, B = series(a), series(b)
    reps = sorted(set(A) & set(B))
    if not reps:
        return None
    rat = sorted(B[r] / A[r] for r in reps)
    slower = sum(1 for x in rat if x > 1.0)
    lo, hi = rat[int(len(rat) * .25)], rat[int(len(rat) * .75)]
    return (st.median(rat) - 1) * 100, slower, len(reps), (lo - 1) * 100, (hi - 1) * 100


def line(label, p, note=""):
    if p is None:
        print(f"  {label:<34}   -")
        return
    print(f"  {label:<34}{p[0]:>+8.2f}%   {p[1]:>2}/{p[2]} slower   "
          f"IQR {p[3]:+.2f}..{p[4]:+.2f}   {note}")


base = series("baseline")
cov = st.stdev(base.values()) / st.mean(base.values()) * 100 if len(base) > 1 else 0
print(f"{bench}   baseline median {st.median(base.values()):,.0f} ns/op   "
      f"CoV {cov:.2f}%   reps {len(base)}\n")

print("VS BASELINE (build-nodit-v2, round-trip control)")
for arm, note in (("null", "harness only, DIT never written"),
                  ("always", "blanket DIT"),
                  ("pass", "the shipped pass"),
                  ("passnop", "pass placement, switches are NOPs"),
                  ("baseline2", "NOISE FLOOR")):
    if arm in present:
        line(arm, paired("baseline", arm), note)

print("\nHEAD TO HEAD  (negative = the pass BEATS blanket DIT)")
if {"always", "pass"} <= present:
    line("pass vs always", paired("always", "pass"))

print("\nDECOMPOSITION  (what the pass's cost is made of)")
if {"pass", "passnop"} <= present:
    line("switches themselves", paired("passnop", "pass"),
         "pass - passnop")
    line("instructions + layout", paired("baseline", "passnop"),
         "passnop - baseline")
else:
    print("  (no passnop arm: cannot attribute the pass's cost)")

print("\nCONTROLS")
nf = paired("baseline", "baseline2")
if nf:
    print(f"  noise floor                       {nf[0]:+.2f}%   "
          f"(any effect smaller than this is not real)")
po = [float(r["probe_off"]) for r in rows if r["probe_off"]]
pn = [float(r["probe_on"]) for r in rows if r["probe_on"]]
if po and pn:
    ratio = st.median(pn) / st.median(po)
    print(f"  in-band lvp_chase                 {ratio:.2f}x   "
          f"{'PASS' if ratio > 3.0 else 'FAIL - DIT stopped taking effect'} (must be ~4x)")
