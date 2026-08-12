#!/usr/bin/env python3
"""Summarize a -taint-dit-precision-report file.

The report has one line per instrumented function. This totals them and prints
the two numbers a placement policy trades against:

    precision = need / underdit    (higher = less public code under DIT)
    switches                       (each MSR DIT costs ~30 cyc when serializing)

Pass several reports to compare policies side by side:

    utils/taint_dit_precision.py region=r.txt function=f.txt
"""
import sys


def load(path):
    fns = {}
    with open(path) as fh:
        for line in fh:
            parts = line.split()
            if len(parts) < 2:
                continue
            name, kv = parts[0], {}
            for p in parts[1:]:
                if "=" not in p:
                    continue
                k, v = p.split("=", 1)
                kv[k] = float(v) if "." in v else int(v)
            # A function can appear twice if the report was appended across runs;
            # last write wins, matching the compiler's own behavior.
            fns[name] = kv
    return fns


def totals(fns):
    t = {}
    for kv in fns.values():
        for k in ("need", "underdit", "collateral", "total", "switches",
                  "wneed", "wunderdit", "wtotal"):
            t[k] = t.get(k, 0) + kv.get(k, 0)
    return t


def pct(n, d):
    return (100.0 * n / d) if d else 0.0


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    cols = []
    for a in args:
        label, _, path = a.partition("=")
        if not path:
            label, path = path or a, a
        cols.append((label or path, load(path)))

    w = 16
    print(f"{'metric':<22}" + "".join(f"{c[0]:>{w}}" for c in cols))
    print("-" * (22 + w * len(cols)))

    rows = [
        ("secret instructions", lambda t: f"{t['need']:,}"),
        ("under DIT", lambda t: f"{t['underdit']:,}"),
        ("collateral", lambda t: f"{t['collateral']:,}"),
        ("total instructions", lambda t: f"{t['total']:,}"),
        ("mode switches", lambda t: f"{t['switches']:,}"),
        ("precision %", lambda t: f"{pct(t['need'], t['underdit']):.1f}"),
        ("coverage %", lambda t: f"{pct(t['underdit'], t['total']):.1f}"),
        ("precision % (loop-wtd)",
         lambda t: f"{pct(t['wneed'], t['wunderdit']):.1f}"),
    ]
    tots = [totals(f) for _, f in cols]
    for label, fn in rows:
        print(f"{label:<22}" + "".join(f"{fn(t):>{w}}" for t in tots))

    if len(cols) > 1:
        print()
        print("precision is what to maximize; switches is what it costs. A policy that")
        print("wins on precision while multiplying switches loses on serializing-DIT")
        print("hardware -- see docs/results/dit-cost-model.md.")

    # Worst offenders: the functions dragging the total down.
    print()
    for (label, fns), t in zip(cols, tots):
        ranked = sorted(
            (kv for kv in fns.items() if kv[1].get("underdit")),
            key=lambda kv: kv[1]["collateral"], reverse=True)[:5]
        if not ranked:
            continue
        print(f"{label}: most collateral")
        for name, kv in ranked:
            print(f"    {name:<34} collateral={kv['collateral']:>6,} "
                  f"precision={pct(kv['need'], kv['underdit']):.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
