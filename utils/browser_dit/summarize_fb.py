#!/usr/bin/env python3
"""Aggregate the fixed-work filter sweep.

!! SIGN CONVENTION DIFFERS FROM summarize.py. Speedometer/MotionMark report a
RATE, so a slower arm scores LOWER and overhead is (ref/arm - 1). This page
reports TIME, so a slower arm reads HIGHER and overhead is (arm/ref - 1).
Getting this backwards flips every verdict - see the dit-metric-sign-convention
memory.
"""
import collections, json, math, pathlib, statistics as st, sys


def ci95(xs):
    return 1.96 * st.stdev(xs) / math.sqrt(len(xs)) if len(xs) > 1 else float("nan")


def pct_time(ref_ms, arm_ms):
    """Positive => arm took LONGER than ref."""
    return (arm_ms / ref_ms - 1.0) * 100.0 if ref_ms else float("nan")


def main():
    work = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                        else pathlib.Path.home() / "Documents/dit-browser-bench")
    rows = [json.loads(l) for l in open(work / "results-fb.jsonl") if l.strip()]
    canary = [json.loads(l) for l in open(work / "canary-fb.jsonl") if l.strip()]
    good = [r for r in rows if r.get("ok")]
    print(f"runs: {len(good)} good, {len(rows) - len(good)} failed\n")

    print("GATES")
    lin = [r["linearity"] for r in good if r.get("linearity")]
    off = sum(1 for x in lin if not (1.8 <= x <= 2.2))
    print(f"  linearity (2x iters -> 2x time): median {st.median(lin):.2f}, "
          f"{off}/{len(lin)} outside [1.8, 2.2]" + ("  *** " if off else "  OK"))
    lop = [c["dit_lands_on_perm"] for c in canary if "dit_lands_on_perm" in c]
    if lop:
        bad = sum(1 for x in lop if not (0.8 <= x <= 1.25))
        print(f"  canary dit_lands_on_perm: median {st.median(lop):.3f}, "
              f"{bad}/{len(lop)} outside band" + ("  *** " if bad else "  OK"))

    # checksum must be identical across ARMS within a (pattern, filter) cell:
    # that is the proof every arm computed the same pixels.
    cks = collections.defaultdict(set)
    for r in good:
        pattern, rest = r["pattern"], r["filter"]
        cks[(pattern, rest)].add(r["checksum"])
    bad_ck = {k: v for k, v in cks.items() if len(v) > 1}
    print(f"  checksums consistent within each (pattern, filter): "
          f"{'OK' if not bad_ck else '*** MISMATCH ' + str(bad_ck)}")

    cells = sorted({(r["pattern"], r["filter"]) for r in good})
    print(f"\n{'pattern':<8} {'filter':<16} {'base ms':>9} {'ms/iter':>9} "
          f"{'harness':>9} {'DIT cost':>16} {'slower':>7}")
    print("-" * 82)
    summary = {}
    for pattern, filt in cells:
        sel = [r for r in good if r["pattern"] == pattern and r["filter"] == filt]
        per = {}
        for arm in ("base", "null", "dit"):
            per[arm] = {r["rep"]: r["median_ms"] for r in sel
                        if r["arm"] == f"{pattern}-{filt}-{arm}"}
        reps = sorted(set.intersection(*(set(v) for v in per.values())))
        if not reps:
            continue
        harness = [pct_time(per["base"][k], per["null"][k]) for k in reps]
        dit = [pct_time(per["null"][k], per["dit"][k]) for k in reps]
        base_ms = st.median(list(per["base"].values()))
        mpi = base_ms / sel[0]["iters"]
        slower = sum(1 for x in dit if x > 0)
        summary[(pattern, filt)] = (st.median(dit), ci95(dit), slower, len(reps))
        print(f"{pattern:<8} {filt:<16} {base_ms:9.2f} {mpi:9.4f} "
              f"{st.median(harness):+8.2f}% {st.median(dit):+9.2f}% "
              f"+/-{ci95(dit):5.2f} {slower:>3}/{len(reps)}")

    print("\nREADING IT")
    print("  'none' is the control: any DIT cost there belongs to drawImage and")
    print("  readback, NOT to filter code. Subtract it before believing a filter row.")
    ctrl = {p: summary.get((p, "none"), (float('nan'),))[0] for p in ("noise", "smooth")}
    print()
    for pattern in ("noise", "smooth"):
        base = ctrl.get(pattern)
        for (p, f), (med, ci, sl, n) in sorted(summary.items()):
            if p != pattern or f == "none":
                continue
            net = med - base if base == base else float("nan")
            verdict = ("filter code is DIT-sensitive" if net > 0.5 and sl >= n * 0.8
                       else "no DIT sensitivity distinguishable from the control")
            print(f"  {p:<7} {f:<16} DIT {med:+.2f}%  minus control {base:+.2f}%"
                  f"  => net {net:+.2f}%   {verdict}")


if __name__ == "__main__":
    main()
