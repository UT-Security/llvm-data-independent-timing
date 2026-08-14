#!/usr/bin/env python3
"""Aggregate a taint_browser_dit_bench.sh sweep into a readable verdict.

Speedometer scores are HIGHER IS BETTER (a scaled inverse of the geomean of
iteration times), so a slower arm has a LOWER score. Overhead is reported as the
time-domain figure, overhead% = (ref_score / arm_score - 1) * 100, which is what
FLOP's "4.5% on Speedometer 3.0" means. See utils/taint_dit_*.md and the
dit-metric-sign-convention memory before quoting any of this.
"""

import collections
import json
import math
import pathlib
import statistics as st
import sys


def load(p):
    if not p.exists():
        return []
    out = []
    for line in p.read_text().splitlines():
        line = line.strip()
        if line:
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return out


def ci95(xs):
    """Half-width of the 95% CI of the mean. t is approximated by 1.96 for
    n > 30 and a small-sample table below that."""
    n = len(xs)
    if n < 2:
        return float("nan")
    tbl = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36,
           9: 2.31, 10: 2.26, 12: 2.20, 15: 2.14, 20: 2.09, 25: 2.06, 30: 2.04}
    t = tbl.get(n, 1.96 if n > 30 else tbl[min(tbl, key=lambda k: abs(k - n))])
    return t * st.stdev(xs) / math.sqrt(n)


def pct_overhead(ref, arm):
    """Positive => arm is slower than ref."""
    return (ref / arm - 1.0) * 100.0 if arm else float("nan")


def main():
    work = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    browser = sys.argv[2] if len(sys.argv) > 2 else "firefox"
    results = load(work / f"results-{browser}.jsonl")
    canary = load(work / f"canary-{browser}.jsonl")
    env = {}
    for name in (f"env-{browser}.json", "env.json"):
        if (work / name).exists():
            env = json.loads((work / name).read_text())
            break

    print(f"Speedometer 3.1 x always-on PSTATE.DIT  [{browser}]")
    print("=" * 66)
    print(f"machine   : {env.get('machine')}  (FEAT_DIT={env.get('feat_dit')})")
    print(f"browser   : {env.get('browser_version') or env.get('browser_bin') or browser}")
    print(f"speedomtr : {(env.get('speedometer_rev') or '')[:12]}")
    print(f"when      : {env.get('when')}")
    print()

    runs = [r for r in results if r.get("arm") in ("base", "null", "dit")]
    good = [r for r in runs if r.get("ok") and r.get("score")]
    bad = [r for r in runs if r not in good]
    print(f"runs: {len(runs)} total, {len(good)} good, {len(bad)} failed")
    for r in bad:
        print(f"  FAILED {r['arm']} rep{r['rep']}: {r.get('error')} "
              f"(timeout={r.get('timed_out')})")

    # Two sweeps appended into one file would collide on (arm, rep) and the
    # paired analysis would silently keep only the last of each. Refuse to
    # pretend that is one clean dataset.
    seen = collections.Counter((r["arm"], r["rep"]) for r in good)
    dupes = [k for k, n in seen.items() if n > 1]
    if dupes:
        print(f"  *** {len(dupes)} duplicate (arm, rep) keys - this file contains "
              f"more than one sweep. Results below are NOT trustworthy; "
              f"split the file or rerun.")
    print()

    # ---- validity gates -------------------------------------------------
    print("VALIDITY")
    problems = []

    dit_runs = [r for r in good if r["arm"] == "dit"]
    if dit_runs:
        nprocs = [r["dit"]["processes_loaded"] for r in dit_runs]
        ncontent = [r["dit"]["content_processes"] for r in dit_runs]
        allset = all(r["dit"]["all_main_dit_set"] for r in dit_runs)
        content = all(r["dit"]["content_dit_set"] for r in dit_runs)
        print(f"  dit arm processes/run : min {min(nprocs)}, max {max(nprocs)}")
        print(f"  content procs/run     : min {min(ncontent)}, max {max(ncontent)}")
        print(f"  main_dit set in all   : {allset}")
        print(f"  content proc covered  : {content}")
        if not allset:
            problems.append("some processes did not have PSTATE.DIT set")
        if not content:
            problems.append("the engine's content/renderer processes were not covered "
                            "- Speedometer runs there, so the measurement is void")
        # pthread_create coverage vs total threads: the gap is libdispatch.
        cov = []
        for r in dit_runs:
            for p in r["dit"]["detail"].values():
                tot, sta = p.get("threads_total"), p.get("threads_started")
                if tot and sta is not None:
                    # cumulative wrapped count vs live census; exited threads can
                    # push this past 1, so clamp.
                    cov.append(min(1.0, (sta + 1) / tot))
        if cov:
            print(f"  pthread-created thread coverage: "
                  f"mean {100*st.mean(cov):.0f}% (remainder is libdispatch, uncovered)")
            if st.mean(cov) < 0.8:
                problems.append(f"only {100*st.mean(cov):.0f}% of threads are "
                                "pthread_create-derived; DIT coverage is partial "
                                "and the measured cost is a LOWER BOUND")

    # The first canary of a sweep is measured on a cold machine and always reads
    # high; including it would report a fake thermal drift every time.
    perms = [c["perm_ns"] for c in canary if "perm_ns" in c][1:]
    if perms:
        # Drift means a systematic shift over the sweep, so measure it as
        # first-half median vs second-half median. max/min is a single-outlier
        # detector, not a drift detector: on chromium-rend one canary in 60 read
        # 0.910 against a 0.868 floor and that lone spike flagged an otherwise
        # perfectly flat sweep as thermally throttled.
        half = len(perms) // 2
        drift = 0.0
        if half:
            drift = abs(st.median(perms[half:]) / st.median(perms[:half]) - 1) * 100
        spikes = sum(1 for x in perms if x > 1.03 * min(perms))
        print(f"  canary perm_ns        : median {st.median(perms):.3f}, "
              f"min {min(perms):.3f}, max {max(perms):.3f}  "
              f"(half-to-half drift {drift:.1f}%, {spikes}/{len(perms)} spikes >3%)")
        if drift > 3.0:
            problems.append(f"canary drifted {drift:.1f}% first-half to second-half - "
                            "thermal throttling, treat arm differences below that "
                            "size as noise")
        elif spikes > 0.2 * len(perms):
            problems.append(f"{spikes}/{len(perms)} canary samples spiked >3% - the "
                            "machine was not quiescent")

    # Gate on the MEDIAN across canaries, not the minimum. Gating on min() means
    # one flaky sample in sixty voids an otherwise clean sweep, which is exactly
    # what happened on the first 20-rep run: 1/62 canaries read 1.37 and the
    # report declared the whole thing invalid while the browser data was fine.
    eff = [c["dit_effect"] for c in canary[1:] if "dit_effect" in c]
    if eff:
        low = [e for e in eff if e < 2.0]
        print(f"  canary dit_effect     : median {st.median(eff):.2f} over {len(eff)} "
              f"canaries, {len(low)} below 2.0 (>2 means DIT gates the LVP)")
        if st.median(eff) < 2.0:
            problems.append("canary MEDIAN shows DIT not gating the LVP - DIT is not "
                            "taking effect, the sweep is void")

        # The robust check: with DIT set the const chase should land on the perm
        # line (both are then plain L1 load-to-use). Measured 0.997-1.003 across
        # 25 processes. Unlike dit_effect this has no noisy denominator - both
        # terms are slow measurements that noise can only inflate.
        lop = [c["dit_lands_on_perm"] for c in canary[1:] if "dit_lands_on_perm" in c]
        if lop:
            print(f"  canary DIT-on vs perm : median {st.median(lop):.3f} "
                  f"[{min(lop):.3f}, {max(lop):.3f}]  (1.0 = LVP fully disabled)")
            if not 0.8 <= st.median(lop) <= 1.25:
                problems.append(f"with DIT set the const chase did not land on the perm "
                                f"line (ratio {st.median(lop):.2f}) - DIT is not fully "
                                "disabling the LVP")

    print("  " + ("no blocking problems" if not problems else "PROBLEMS:"))
    for p in problems:
        print(f"    - {p}")
    print()

    # ---- per-arm --------------------------------------------------------
    print("PER-ARM SCORES (higher is better)")
    stats = {}
    for arm in ("base", "null", "dit"):
        xs = [r["score"] for r in good if r["arm"] == arm]
        if not xs:
            continue
        stats[arm] = xs
        print(f"  {arm:<5} n={len(xs):<3} median {st.median(xs):8.3f}   "
              f"mean {st.mean(xs):8.3f} +/- {ci95(xs):.3f}   "
              f"min {min(xs):.3f} max {max(xs):.3f}")
    print()

    # ---- comparisons ----------------------------------------------------
    # Arms are interleaved within each rep, so pair by rep: that cancels drift
    # and any slow machine-state wander outright, and is far more powerful than
    # comparing two unpaired medians.
    per_rep = {a: {r["rep"]: r["score"] for r in good if r["arm"] == a}
               for a in ("base", "null", "dit")}
    reps = sorted(set.intersection(*(set(v) for v in per_rep.values()))
                  ) if all(per_rep.values()) else []

    print(f"PAIRED COMPARISONS over {len(reps)} reps (positive % = that arm is SLOWER)")

    def paired(ref, arm, label):
        if not reps:
            return None
        d = [pct_overhead(per_rep[ref][k], per_rep[arm][k]) for k in reps]
        n = len(d)
        m = st.mean(d)
        half = ci95(d)
        slower = sum(1 for x in d if x > 0)
        print(f"  {label:<28} {m:+6.2f}% +/- {half:.2f}   median {st.median(d):+.2f}%   "
              f"{slower}/{n} reps slower")
        return (m, half, slower, n)

    noise = paired("base", "null", "harness (base -> null)")
    dit_cost = paired("null", "dit", "DIT (null -> dit)")
    paired("base", "dit", "total (base -> dit)")
    print()

    print("UNPAIRED MEDIANS (cross-check)")

    def cmp(ref, arm, label):
        if ref not in stats or arm not in stats:
            return None
        a, b = st.median(stats[ref]), st.median(stats[arm])
        print(f"  {label:<28} {pct_overhead(a, b):+6.2f}%   (median {a:.2f} -> {b:.2f})")

    cmp("base", "null", "harness (base -> null)")
    cmp("null", "dit", "DIT (null -> dit)")
    print()

    print("VERDICT")
    if dit_cost is None:
        print("  not enough data")
    else:
        # Judge against the effect's own confidence interval and the measured
        # harness floor, NOT against a hard-coded percentage. An earlier version
        # required >2.0% absolute and so called Chromium's +1.80 +/- 0.16 with
        # 20/20 reps slower "inconclusive", which is plainly wrong: that CI is
        # [1.64, 1.96] and nowhere near either zero or the 0.30% floor.
        m, half, slower, n = dit_cost
        lo, hi = m - half, m + half
        floor = abs(noise[0]) if noise else 0.0
        print(f"  noise floor from base-vs-null : {floor:.2f}%")
        print(f"  always-on DIT on Speedometer  : {m:+.2f}%  95% CI [{lo:+.2f}, {hi:+.2f}]")
        print(f"  reps slower                   : {slower}/{n}"
              + ("  (sign test p < 1e-5)" if slower == n and n >= 17 else ""))
        print(f"  FLOP reference (Safari, S3.0) : +4.50%")
        if problems:
            print("  -> INVALID until the problems above are resolved")
        elif lo > max(floor, 0.5):
            print(f"  -> Always-on DIT costs a real {m:.2f}% here, cleanly separated from")
            print(f"     the harness floor. The fine-grained lead is ALIVE: that is the")
            print(f"     budget taint-driven placement would have to recover.")
        elif hi < 0.5:
            print("  -> Always-on DIT is ~free on this workload. The browser lead is DEAD")
            print("     for this engine. Record the negative and move on.")
        else:
            print("  -> Inconclusive: the CI overlaps the harness floor. Raise REPS.")


if __name__ == "__main__":
    main()
