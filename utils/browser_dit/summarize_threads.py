#!/usr/bin/env python3
"""Aggregate a thread-class attribution sweep (N arms, not the fixed three).

Metric convention is summarize.py's, unchanged: Speedometer's score is a rate,
so a SLOWER arm has a LOWER score and overhead% = (ref/arm - 1)*100 is positive
when `arm` is slower. See the dit-metric-sign-convention memory before quoting.

The question this answers: of the DIT cost that renderer-wide placement pays,
how much sits on thread classes an AOT clang pass could actually instrument?
Gecko's MainThread is C++ (plus JIT-generated code); StyleThread#N is Stylo,
which is Rust compiled by rustc's own LLVM and therefore out of reach.
"""
import collections, json, math, pathlib, statistics as st, sys


def load(p):
    return [json.loads(l) for l in open(p) if l.strip()]


def ci95(xs):
    n = len(xs)
    if n < 2:
        return float("nan")
    return 1.96 * st.stdev(xs) / math.sqrt(n)


def pct_overhead(ref, arm):
    return (ref / arm - 1.0) * 100.0 if arm else float("nan")


def main():
    work = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                        else pathlib.Path.home() / "Documents/dit-browser-bench")
    variant = sys.argv[2] if len(sys.argv) > 2 else "firefox-rend-threads"
    results = load(work / f"results-{variant}.jsonl")
    canary = load(work / f"canary-{variant}.jsonl")

    good = [r for r in results if r.get("ok")]
    bad = [r for r in results if not r.get("ok")]
    arms = sorted({r["arm"] for r in good})
    print(f"variant   : {variant}")
    print(f"runs      : {len(good)} good, {len(bad)} failed")
    for r in bad:
        print(f"  FAILED {r['arm']} rep{r['rep']}: {r.get('error')}")

    dupes = [k for k, c in collections.Counter(
        (r["arm"], r["rep"]) for r in good).items() if c > 1]
    if dupes:
        print(f"  *** {len(dupes)} duplicate (arm, rep) keys - two sweeps in one file")

    # ---- gates ----------------------------------------------------------
    print("\nGATES")
    eff = [c["dit_effect"] for c in canary[1:] if "dit_effect" in c]
    if eff:
        lo = min(eff)
        print(f"  canary dit_effect     : median {st.median(eff):.2f} over {len(eff)}"
              f" samples, min {lo:.2f}" + ("  *** LOW" if lo < 2.0 else ""))
    lop = [c["dit_lands_on_perm"] for c in canary[1:] if "dit_lands_on_perm" in c]
    if lop:
        print(f"  dit_lands_on_perm     : median {st.median(lop):.3f} "
              f"(want ~1.0: DIT fully disables the LVP)")

    # ---- per-arm --------------------------------------------------------
    print("\nPER-ARM SCORES (higher = faster)")
    stats = {}
    for arm in arms:
        xs = [r["score"] for r in good if r["arm"] == arm]
        if not xs:
            continue
        stats[arm] = xs
        cov = st.stdev(xs) / st.mean(xs) * 100 if len(xs) > 1 else float("nan")
        print(f"  {arm:<10} n={len(xs):<3} median {st.median(xs):8.3f}   CoV {cov:5.2f}%")

    # ---- paired ---------------------------------------------------------
    per_rep = {a: {r["rep"]: r["score"] for r in good if r["arm"] == a} for a in arms}
    reps = sorted(set.intersection(*(set(v) for v in per_rep.values())))
    print(f"\nPAIRED COMPARISONS over {len(reps)} reps "
          f"(positive % = that arm is SLOWER than the reference)")

    def paired(ref, arm, label):
        if ref not in per_rep or arm not in per_rep or not reps:
            return None
        d = sorted(pct_overhead(per_rep[ref][k], per_rep[arm][k]) for k in reps)
        n = len(d)
        q1, q3 = d[n // 4], d[(3 * n) // 4]
        slower = sum(1 for x in d if x > 0)
        print(f"  {label:<34} {st.mean(d):+6.2f}% +/- {ci95(d):.2f}   "
              f"median {st.median(d):+6.2f}%   IQR [{q1:+.2f}, {q3:+.2f}]   "
              f"{slower}/{n} slower")
        return st.median(d)

    noise = paired("base", "null", "harness floor (base -> null)")
    full = paired("null", "dit", "DIT, whole renderer (null -> dit)")
    main_t = paired("null", "dit-main", "DIT, MainThread only")
    style_t = paired("null", "dit-style", "DIT, StyleThread only")

    # ---- attribution ----------------------------------------------------
    print("\nATTRIBUTION")
    if full is None or not full:
        print("  no full-renderer arm to attribute against")
        return
    print(f"  whole-renderer DIT cost        : {full:+.2f}%")
    for label, part, reach in (("MainThread (C++ + JIT)", main_t, "clang pass CAN reach the C++"),
                               ("StyleThread (Stylo, Rust)", style_t, "clang pass CANNOT reach")):
        if part is None:
            continue
        share = part / full * 100 if full else float("nan")
        print(f"  {label:<31}: {part:+.2f}%  = {share:5.1f}% of the total   [{reach}]")
    if main_t is not None and style_t is not None:
        acc = main_t + style_t
        print(f"  sum of the two classes         : {acc:+.2f}%  "
              f"({acc / full * 100:5.1f}% of the total)")
        print(f"  unattributed remainder         : {full - acc:+.2f}%  "
              f"(other thread classes, and the pre-naming startup window)")
    if noise is not None:
        print(f"\n  noise floor (base->null)       : {noise:+.2f}%  "
              "- treat any arm inside this as indistinguishable from zero")


if __name__ == "__main__":
    main()
