#!/usr/bin/env python3
"""Summarize oracle vs always-on. Paired by rep, medians, gates."""
import csv
import os
import statistics as st
import sys

SEC = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(SEC), "out", "oracle.csv")
rows = list(csv.DictReader(open(path)))

d, meta, ck = {}, {}, {}
for r in rows:
    d.setdefault((r["host"], r["mode"]), {})[int(r["rep"])] = float(r["total_s"])
    meta[(r["host"], r["mode"])] = (float(r["secret_frac"]), int(r["signs"]), int(r["toggles"]))
    ck.setdefault(r["host"], set()).add(r["checksum"])

hosts = []
for (h, m) in d:
    if h not in hosts:
        hosts.append(h)


def pr(h, a, b):
    x, y = d[(h, a)], d[(h, b)]
    reps = sorted(set(x) & set(y))
    return [y[i] / x[i] for i in reps]


print("=" * 100)
print("ORACLE vs ALWAYS-ON   (one binary per host, DIT mode chosen at runtime)")
print("=" * 100)
print(f"{'host':9s} {'off s':>8s} {'always s':>9s} {'oracle s':>9s} | "
      f"{'always-on':>10s} {'oracle':>9s} | {'RECOVERED':>10s} {'o<a reps':>9s} | "
      f"{'NOISE':>7s} {'secret%':>8s} {'togg':>6s}")

for h in hosts:
    off, alw, orc = d[(h, "off")], d[(h, "always")], d[(h, "oracle")]
    a_cost = (st.median(pr(h, "off", "always")) - 1) * 100
    o_cost = (st.median(pr(h, "off", "oracle")) - 1) * 100
    beat = pr(h, "always", "oracle")
    nbeat = sum(1 for p in beat if p < 1)
    rec = (a_cost - o_cost) / a_cost * 100 if a_cost else 0
    frac, signs, togg = meta[(h, "oracle")]
    noise = (st.median(pr(h, "off", "off2")) - 1) * 100 if (h, "off2") in d else float("nan")
    print(f"{h:9s} {st.median(off.values()):8.3f} {st.median(alw.values()):9.3f} "
          f"{st.median(orc.values()):9.3f} | {a_cost:+9.2f}% {o_cost:+8.2f}% | "
          f"{rec:9.1f}% {nbeat:4d}/{len(beat):<4d} | {noise:+6.2f}% {frac:7.3f}% {togg:6d}")

print()
print("  NOISE = off vs a SECOND run of the identical off arm. That is the rig's floor;")
print("          any |oracle| below it is indistinguishable from zero.")
print("  always-on = off->always. oracle = off->oracle (residual cost after perfect placement).")
print("  RECOVERED = fraction of the always-on cost that oracle placement gives back.")
print()
for h in hosts:
    status = "PASS" if len(ck[h]) == 1 else "FAIL " + str(ck[h])
    print(f"  checksum identical across all 3 modes, {h:9s}: {status}")
print()
print("  Gate (trap 3): no arm faster than 'off' by more than noise ->",
      end=" ")
bad = [h for h in hosts if (st.median(pr(h, "off", "oracle")) - 1) * 100 < -0.5]
print("PASS" if not bad else "CHECK " + ",".join(bad))
