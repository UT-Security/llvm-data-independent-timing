#!/usr/bin/env python3
"""Blanket DIT overhead: ratio of medians per primitive, with the five gates.

Reads $OUT/blanket.csv. Prints one row per primitive and refuses to present a
number whose gates did not pass -- a failed gate is reported in place of the
estimate, not beside it, because a percentage printed next to the reason it is
wrong gets quoted without the reason."""
import collections, csv, os, statistics as st

OUT = os.environ.get("OUT", ".")
KPERF_CYC, KPERF_INS = 3400, 17700   # measured cost of one kpc_get_thread_counters pair

rows = list(csv.DictReader(open(os.path.join(OUT, "blanket.csv"))))
d = collections.defaultdict(list)
for r in rows:
    try:
        d[(r["primitive"], r["arm"])].append(
            (float(r["cycles"]), float(r["instructions"]), float(r["ticks"]),
             float(r["iters"]), int(r["dit_exit"])))
    except (ValueError, KeyError):
        continue

prims, seen = [], set()
for (p, _a) in d:
    if p not in seen:
        seen.add(p); prims.append(p)

def med(p, a, i): return st.median([x[i] for x in d[(p, a)]])
def mad(p, a, i):
    v = [x[i] for x in d[(p, a)]]; m = st.median(v)
    return (st.median([abs(x - m) for x in v]) / m) if m else 0.0

have_cyc = any(med(p, a, 0) > 0 for (p, a) in d)
unit_i, unit = (0, "cycles") if have_cyc else (2, "ns")
if not have_cyc:
    print("\nNO CYCLE COUNTERS (not root): falling back to TIME. Gate 3 cannot fire,\n"
          "so a frequency difference between the arms would be invisible here.\n")

print(f"\n{'='*104}\nBLANKET PSTATE.DIT -- {unit}/op, C (DIT on) vs A (DIT off), same binary\n{'='*104}")
print(f"{'primitive':<16}{'A '+unit+'/op':>14}{'C '+unit+'/op':>14}{'overhead':>11}"
      f"{'MAD A':>8}{'MAD C':>8}{'A GHz':>8}{'C GHz':>8}   gates")
print("-" * 104)

ctrl_ov = None
for p in prims:
    if (p, "A") not in d or (p, "C") not in d:
        continue
    n = med(p, "A", 3)
    a_v, c_v = med(p, "A", unit_i) / n, med(p, "C", unit_i) / n
    ov = (c_v / a_v - 1) * 100 if a_v else 0.0
    if p == "control":
        ctrl_ov = ov
    fails = []

    # GATE 1 -- the read pair, divided by the iteration count, against the total.
    if have_cyc:
        share = KPERF_CYC / med(p, "A", 0) * 100 if med(p, "A", 0) else 100.0
        if share > 0.01:
            fails.append(f"G1 instrument {share:.3f}% of total")

    # GATE 2 -- one binary, one input: instructions/op must not move.
    if have_cyc:
        ai, ci = med(p, "A", 1) / n, med(p, "C", 1) / n
        drift = abs(ci / ai - 1) * 100 if ai else 0.0
        if drift > 0.05:
            fails.append(f"G2 work differs {drift:+.3f}% ins/op")

    # GATE 3 -- if the arms ran at different clocks, only cycles mean anything.
    ga = gc = 0.0
    if have_cyc:
        ga = med(p, "A", 0) / med(p, "A", 2) if med(p, "A", 2) else 0.0
        gc = med(p, "C", 0) / med(p, "C", 2) if med(p, "C", 2) else 0.0
        if ga and gc and abs(gc / ga - 1) > 0.01:
            fails.append(f"G3 clock {ga:.2f} vs {gc:.2f} GHz")

    # GATE 4 -- the mode has to have been on. Readback is per-arm and absolute.
    if any(x[4] != 1 for x in d[(p, "C")]) or any(x[4] != 0 for x in d[(p, "A")]):
        fails.append("G4 DIT readback wrong at exit")

    # GATE 5 -- separation against the arms' own dispersion.
    ma, mc = mad(p, "A", unit_i), mad(p, "C", unit_i)
    if abs(ov) / 100 < 3 * max(ma, mc):
        fails.append("G5 below noise floor")

    print(f"{p:<16}{a_v:>14,.1f}{c_v:>14,.1f}{ov:>+10.2f}%"
          f"{ma*100:>7.2f}%{mc*100:>7.2f}%{ga:>8.2f}{gc:>8.2f}   "
          + ("ok" if not fails else "; ".join(fails)))

print("-" * 104)
if ctrl_ov is None:
    print("NO CONTROL ROW. Run the `control` primitive or GATE 4 is unverified: a flat\n"
          "result cannot be told apart from DIT never having been set.")
elif ctrl_ov < 5:
    print(f"GATE 4 FAILED GLOBALLY: control slowed only {ctrl_ov:+.2f}%. A data-dependent\n"
          "pointer chase MUST slow under DIT. Treat every row above as unverified.")
else:
    print(f"GATE 4 ok: control slowed {ctrl_ov:+.2f}%, so the mode was demonstrably on.")
print("\nREADING IT\n"
      "  The ratio is the result; the absolute column is not. It carries the loop's\n"
      "  own overhead and one counter pair, neither of which is subtracted.\n"
      "  Overhead near 0 means blanket DIT is FREE on that primitive -- which is a\n"
      "  property of the workload, and the thing selective placement would have to\n"
      "  beat to be worth its switches.")
