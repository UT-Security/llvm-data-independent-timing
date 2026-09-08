#!/usr/bin/env python3
"""Experiment 14 on gem5: turn filters.txt into balanced per-process slices.

THE SUITE IS SEGMENTED BY FILTER, NOT BY ROW, and that is not a preference. A row skipped
via M5_CALLS_LIST still runs its setup, and skipping one row breaks another: RSA verify
checks the signature its own signing row produced, and skipping RSA key-gen makes
ML-KEM-512 encaps fail outright. A filtered-out benchmark returns before doing anything,
so a filter is the only clean exclusion. A benchmark's internal dependencies always live
inside one filter, which is what makes the partitioning safe.

WHAT THIS DOES

  1. asks the NATIVE probe (built with -DBM_M5_NO_OPS, see patch_speed_m5.py) which rows
     each filter emits, and in what order -- the counts are indexed by emission order, so
     the order has to come from the tool rather than be assumed;
  2. picks a minimal cover, so a row is measured once rather than in several slices;
  3. sizes each row's call count from a native cost census, clamped to [1, MAX];
  4. packs the chosen filters into K slices, longest-cost-first, so they finish together.

The probe must be the same patch and the same -chunks/-threads the sweep will run with.

    gen_filter_slices.py --probe <bssl> --census census.json --out slices.json -k 14
"""
import argparse, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor


def row_key(r):
    size = r.get("bytesPerCall") or r.get("primeSizePerCall") or 0
    return f"{r['description']}" + (f" [{size} B]" if r.get("bytesPerCall")
                                    else (f" [{size}-bit]" if r.get("primeSizePerCall") else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", required=True, help="native BM_M5_NO_OPS bssl")
    ap.add_argument("--census", required=True, help="JSON from the UNPATCHED tool")
    ap.add_argument("--filters", default=os.path.join(os.path.dirname(__file__), "filters.txt"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--chunks", default="16,256,1350,8192,16384")
    ap.add_argument("--threads", default="1",
                    help="-threads for the tool; 1 keeps CRYPTO_refcount_inc on the "
                         "single-thread path the speed patch runs inline, because gem5 SE "
                         "on one CPU cannot create a thread")
    ap.add_argument("-k", "--partitions", type=int, default=14)
    ap.add_argument("--budget", type=int, default=200_000, help="target simulated cycles per row")
    ap.add_argument("--max-calls", type=int, default=2000)
    ap.add_argument("--cycles-per-ns", type=float, default=3.83)
    ap.add_argument("--warm", type=int, default=20)
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--cap-ns", type=float, default=0.0,
                    help="drop any FILTER that selects a row costing more than this natively. "
                         "Whole filters, never individual rows: skipping one row of a family "
                         "breaks another (RSA verify checks the signature its own signing row "
                         "produced), so the unit of exclusion has to be the family.")
    a = ap.parse_args()

    filters = [l.strip() for l in open(a.filters) if l.strip() and not l.startswith("#")]
    ns = {}
    for r in json.load(open(a.census)):
        n, us = r.get("numCalls"), r.get("microseconds")
        if n and us:
            ns[row_key(r)] = us * 1000.0 / n

    def probe(f):
        env = dict(os.environ, M5_FILTER=f, M5_CALLS="1", M5_WARM="1")
        try:
            p = subprocess.run([a.probe, "speed", "-json", "-threads", a.threads,
                                "-chunks", a.chunks], env=env, capture_output=True,
                               text=True, timeout=900)
            return f, [row_key(r) for r in json.loads(p.stdout)]
        except Exception:
            return f, []
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        fr = {f: rows for f, rows in ex.map(probe, filters) if rows}
    dead = [f for f in filters if f not in fr]
    if dead:
        print(f"note: {len(dead)} filter(s) emitted no rows: {', '.join(dead[:8])}"
              + (" ..." if len(dead) > 8 else ""))

    def calls_for(row):
        return max(1, min(a.max_calls, int(a.budget / max(1.0, ns.get(row, 0.0) * a.cycles_per_ns)) or 1))
    def cost(f):
        return sum(ns.get(r, 0.0) * a.cycles_per_ns * calls_for(r) for r in fr[f])

    if a.cap_ns:
        dropped = {f: max(ns.get(r, 0.0) for r in rows) for f, rows in fr.items()
                   if any(ns.get(r, 0.0) > a.cap_ns for r in rows)}
        for f in dropped:
            del fr[f]
        if dropped:
            print(f"cap {a.cap_ns/1e6:g} ms/op drops {len(dropped)} filter(s):")
            for f, worst in sorted(dropped.items(), key=lambda kv: -kv[1])[:10]:
                print(f"    {worst/1e6:>9,.0f} ms   {f}")
            if len(dropped) > 10:
                print(f"    ... and {len(dropped)-10} more")
        if not fr:
            sys.exit("the cap removed every filter")

    need = set().union(*fr.values())
    chosen = []
    while need:
        best = max(fr, key=lambda f: (len(set(fr[f]) & need), -cost(f)))
        gain = set(fr[best]) & need
        if not gain:
            break
        chosen.append(best)
        need -= gain

    bins, load = [[] for _ in range(a.partitions)], [0.0] * a.partitions
    for f in sorted(chosen, key=lambda f: -cost(f)):
        j = load.index(min(load))
        bins[j].append(f)
        load[j] += cost(f)

    slices = []
    for j, b in enumerate(bins):
        order = [r for f in b for r in fr[f]]
        slices.append(dict(label=f"g{j}", filter=",".join(b), chunks=a.chunks, warm=a.warm,
                           calls_list=",".join(str(calls_for(r)) for r in order),
                           rows=len(order), est_cycles=int(load[j])))
    json.dump(dict(chunks=a.chunks, filter="", budget=a.budget, cap_ns=0, min_ns=0,
                   cycles_per_ns=a.cycles_per_ns, screen=False, threads=a.threads,
                   census_rows=len(ns), kept=sum(s["rows"] for s in slices), slices=slices),
              open(a.out, "w"), indent=1)

    uniq = len(set().union(*[set(fr[f]) for f in chosen]))
    inst = sum(s["rows"] for s in slices)
    print(f"{len(fr)} filters emit rows; cover {len(chosen)} of them")
    print(f"{inst} row instances for {uniq} unique rows ({inst/uniq:.2f}x duplication)")
    print(f"{sum(load)/1e9:.3f} G simulated cycles per (arm, model); "
          f"long pole {max(load)/40000/3600:.2f} h")
    for j, b in enumerate(bins):
        print(f"  g{j:<2d} rows={slices[j]['rows']:>4d}  {load[j]/40000/3600:>5.2f} h  {','.join(b)[:56]}")
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
