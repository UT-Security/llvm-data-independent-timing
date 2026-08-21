#!/usr/bin/env python3
"""Aggregate the MotionMark CPU-filter sweep.

Metric convention matches summarize.py: MotionMark's score is a sustained
complexity, so a SLOWER arm scores LOWER and overhead% = (ref/arm - 1)*100 is
positive when `arm` is slower.

Read the two tests as a matched pair, never in isolation. "CSS bouncing filter
circles" and "CSS bouncing circles" are the same page with and without &filter,
so `plain` is the control that says whether any DIT cost belongs to the FILTER
or merely to CSS compositing. Absolute scores are not comparable between them -
the filter test sustains ~25x less complexity - but the within-test DIT ratio is.

Caveat that must travel with every number here: Firefox is forced onto software
WebRender so the filter runs on the CPU at all. That is not a deployed
configuration.
"""
import collections, json, math, pathlib, statistics as st, sys


def ci95(xs):
    return 1.96 * st.stdev(xs) / math.sqrt(len(xs)) if len(xs) > 1 else float("nan")


def pct(ref, arm):
    return (ref / arm - 1.0) * 100.0 if arm else float("nan")


def main():
    work = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                        else pathlib.Path.home() / "Documents/dit-browser-bench")
    rows = [json.loads(l) for l in open(work / "results-mm-filter.jsonl") if l.strip()]
    canary = [json.loads(l) for l in open(work / "canary-mm-filter.jsonl") if l.strip()]
    good = [r for r in rows if r.get("ok")]
    bad = [r for r in rows if not r.get("ok")]

    print(f"runs: {len(good)} good, {len(bad)} failed")
    for r in bad[:8]:
        print(f"  FAILED {r['arm']} rep{r['rep']}: {r.get('error')}")

    print("\nGATES")
    lop = [c["dit_lands_on_perm"] for c in canary if "dit_lands_on_perm" in c]
    eff = [c["dit_effect"] for c in canary if "dit_effect" in c]
    if lop:
        out = sum(1 for x in lop if not (0.8 <= x <= 1.25))
        print(f"  dit_lands_on_perm : median {st.median(lop):.3f}, "
              f"{out}/{len(lop)} samples outside [0.8, 1.25]"
              + ("   *** investigate" if out else "   OK"))
    if eff:
        print(f"  canary dit_effect : median {st.median(eff):.2f}, min {min(eff):.2f}")

    # arm names are "<test>-<arm>"
    tests = sorted({r["arm"].rsplit("-", 1)[0] if r["arm"].count("-") == 1
                    else r["arm"].split("-", 1)[0] for r in good})
    for test in tests:
        sel = [r for r in good if r["arm"].startswith(test + "-")]
        if not sel:
            continue
        arms = sorted({r["arm"][len(test) + 1:] for r in sel})
        print(f"\n{'=' * 62}\nTEST: {test}")
        per_rep = {}
        for a in arms:
            xs = {r["rep"]: r["score"] for r in sel if r["arm"] == f"{test}-{a}"}
            per_rep[a] = xs
            vals = list(xs.values())
            cov = st.stdev(vals) / st.mean(vals) * 100 if len(vals) > 1 else float("nan")
            print(f"  {a:<9} n={len(vals):<3} median {st.median(vals):10.2f}  CoV {cov:5.2f}%")

        reps = sorted(set.intersection(*(set(v) for v in per_rep.values())))
        print(f"  -- paired over {len(reps)} reps (positive = arm is SLOWER) --")

        def paired(ref, arm, label):
            if ref not in per_rep or arm not in per_rep or not reps:
                return None
            d = sorted(pct(per_rep[ref][k], per_rep[arm][k]) for k in reps)
            n = len(d)
            q1, q3 = d[n // 4], d[(3 * n) // 4]
            print(f"    {label:<26} {st.mean(d):+6.2f}% +/- {ci95(d):.2f}  "
                  f"median {st.median(d):+6.2f}%  IQR [{q1:+.2f}, {q3:+.2f}]  "
                  f"{sum(1 for x in d if x > 0)}/{n} slower")
            return st.median(d)

        paired("base", "null", "harness floor")
        paired("null", "dit", "DIT, all processes")
        paired("null", "dit-gpu", "DIT, GPU process only")

    print(f"\n{'=' * 62}")
    print("Compare the two tests' 'DIT, all processes' rows against each other:")
    print("  filter > plain  => the filter itself is DIT-sensitive")
    print("  filter <= plain => the cost is CSS compositing, NOT the filter, and")
    print("                     a filter-targeted placement would recover nothing")


if __name__ == "__main__":
    main()
