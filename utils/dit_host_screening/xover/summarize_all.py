#!/usr/bin/env python3
"""Cross-benchmark summary: secret fraction x switch model x IPC.

WHAT THIS ANSWERS. Every individual rig reports its own workload. This pulls all
of them into one frame with the three axes the evaluation is actually about:

  f      the fraction of dynamic work that is secret  (the knob)
  model  serializing vs renamed `MSR DIT`             (gem5 only - silicon has
         exactly one implementation, and it is the serializing one)
  IPC    instructions per cycle

WHY IPC IS THE RIGHT SECOND METRIC. Instruction counts are IDENTICAL across arms
and across machine configurations - that is an enforced gate, not a hope - so a
cycle difference IS an IPC difference and nothing else. Reporting IPC rather than
only a percentage makes the mechanism legible: DIT does not add work, it removes
optimizations, so it can only ever show up as IPC loss.

Native rows carry no IPC: those runs are wall-clock on Apple silicon with no
performance counters read (no sudo => no kperf). That is a real gap, not an
oversight to paper over.
"""
import argparse, collections, json, math, pathlib, statistics as st

H = pathlib.Path.home()
G5 = H / "Documents/dit-crossover/out/gem5"
NAT = H / "Documents/dit-crossover/out/native"
SQLC = H / "Documents/dit-browser-bench"


def load(p):
    p = pathlib.Path(p)
    return [json.loads(l) for l in open(p) if l.strip()] if p.exists() else []


def geo(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def gem5_fsweep(path, title, zero=(0, 1), arms=("off", "always", "oracle", "hoist",
                                               "gated", "swcyc30", "hoist0", "nopctl")):
    """f-sweep table. f is measured by differencing the `off` arm against its own
    sigs=0 point - exact under gem5, no estimation."""
    rs = [r for r in load(path) if r.get("cycles")]
    if not rs:
        print(f"\n[{title}: no data at {path}]")
        return
    by = collections.defaultdict(dict)
    for r in rs:
        by[(r["cfg"], r["sigs"], r["period"])][r["arm"]] = r
    print("\n" + "=" * 118)
    print(title)
    print("=" * 118)
    for cfg, label in (("serdit", "SERIALIZING msr DIT (what ARM silicon does)"),
                       ("spec", "RENAMED msr DIT (a future implementation)")):
        pts = sorted({(s, p) for (c, s, p) in by if c == cfg},
                     key=lambda t: (t[0], -t[1]))
        if not pts:
            continue
        z = by.get((cfg,) + zero, {}).get("off")
        print(f"\n--- {label} ---")
        print(f"{'f_secret':>9}  " + "".join(f"{a:>10}" for a in arms) +
              "   |  IPC: " + "".join(f"{a:>8}" for a in arms))
        for pt in pts:
            d = by[(cfg,) + pt]
            if "off" not in d:
                continue
            b = d["off"]
            f = (b["cycles"] - z["cycles"]) / b["cycles"] * 100 if z else float("nan")
            row = f"{f:>8.2f}%  "
            for a in arms:
                row += f"{(d[a]['cycles']/b['cycles']-1)*100:>9.2f}%" if a in d else f"{'-':>10}"
            row += "   |       "
            for a in arms:
                row += f"{d[a]['insts']/d[a]['cycles']:>8.3f}" if a in d else f"{'-':>8}"
            print(row)


def gem5_lane_ipc(path, title, zero=(0, 1), top=None,
                  arms=("off", "always", "oracle", "hoist", "gated", "swcyc30")):
    """Separate the two lanes' IPC by differencing each arm against its OWN zero
    point. This is the decomposition the percentages hide: it shows whether DIT
    costs IPC in the PUBLIC lane, the SECRET lane, or both."""
    rs = [r for r in load(path) if r.get("cycles")]
    if not rs:
        return
    by = collections.defaultdict(dict)
    for r in rs:
        by[(r["cfg"], r["sigs"], r["period"])][r["arm"]] = r
    if top is None:
        cand = [(s, p) for (c, s, p) in by if c == "serdit"]
        top = max(cand, key=lambda t: t[0] / t[1])
    print("\n" + "=" * 118)
    print(title + f"   [public lane = the {zero} point; secret lane = {top} minus {zero}]")
    print("=" * 118)
    for cfg in ("serdit", "spec"):
        z, t = by.get((cfg,) + zero), by.get((cfg,) + top)
        if not z or not t:
            continue
        print(f"\n--- cfg={cfg} ---")
        print(f"{'arm':<9} {'public IPC':>11} {'vs off':>9}  |  {'secret IPC':>11} {'vs off':>9}")
        bp = z["off"]["insts"] / z["off"]["cycles"]
        bs = ((t["off"]["insts"] - z["off"]["insts"]) /
              (t["off"]["cycles"] - z["off"]["cycles"]))
        for a in arms:
            if a not in z or a not in t:
                continue
            pub = z[a]["insts"] / z[a]["cycles"]
            sec = ((t[a]["insts"] - z[a]["insts"]) / (t[a]["cycles"] - z[a]["cycles"]))
            print(f"{a:<9} {pub:>11.4f} {(pub/bp-1)*100:>8.2f}%  |  "
                  f"{sec:>11.4f} {(sec/bs-1)*100:>8.2f}%")


def sqlcipher():
    recs = []
    for f in ("gem5-sqlc/results.jsonl", "gem5-sqlc2/results.jsonl"):
        recs += [r for r in load(SQLC / f) if r.get("cycles")]
    if not recs:
        return
    by = collections.defaultdict(dict)
    for r in recs:
        by[(r["cfg"], r["cache"])][r["arm"]] = r
    print("\n" + "=" * 118)
    print("SQLCIPHER - SQLite B-tree public lane + AES-256-CBC/HMAC per page. "
          "Knob: PRAGMA cache_size (page decrypts per query).")
    print("=" * 118)
    for cfg, label in (("serdit", "SERIALIZING"), ("spec", "RENAMED")):
        print(f"\n--- {label} ---")
        print(f"{'cache':>6} {'dec/query':>10}  " +
              "".join(f"{a:>10}" for a in ("plain", "blanket", "hoist")) +
              "   |  IPC: " + "".join(f"{a:>9}" for a in ("nodit", "blanket", "hoist")))
        for c in sorted({k[1] for k in by if k[0] == cfg}):
            d = by[(cfg, c)]
            if "nodit" not in d:
                continue
            b = d["nodit"]
            row = f"{c:>6} {str(d['nodit'].get('dec_q')):>10}  "
            for a in ("plain", "blanket", "hoist"):
                row += f"{(d[a]['cycles']/b['cycles']-1)*100:>9.2f}%" if a in d else f"{'-':>10}"
            row += "   |       "
            for a in ("nodit", "blanket", "hoist"):
                row += f"{d[a]['insts']/d[a]['cycles']:>9.4f}" if a in d else f"{'-':>9}"
            print(row)


def native(name, key, arms, title, fkey="f"):
    rs = load(NAT / f"{name}.jsonl")
    if not rs:
        print(f"\n[{title}: no data]")
        return
    by = collections.defaultdict(lambda: collections.defaultdict(dict))
    for r in rs:
        by[tuple(r[k] for k in key)][r["arm"]][r["rep"]] = r
    print("\n" + "=" * 118)
    print(title + "   [Apple M5, wall clock, paired per rep, geometric mean. "
                  "Silicon serializes msr DIT; there is no renamed arm.]")
    print("=" * 118)
    print(f"{'point':<18} {'f_secret':>9} {'R_us':>8}  " +
          "".join(f"{a:>10}" for a in arms) + f"{'floor':>9}{'reps':>6}")
    for pt in sorted(by):
        d = by[pt]
        if "off" not in d:
            continue
        base = d["off"]
        f = st.median([v.get(fkey, 0) or 0 for v in base.values()]) if fkey else 0.0
        R = st.median([v.get("R_us", 0) or 0 for v in base.values()])
        row = f"{str(pt):<18} {f:>8.3f}% {R:>8.2f}  "
        for a in arms:
            if a not in d:
                row += f"{'-':>10}"
                continue
            rr = [d[a][k]["total_s"] / base[k]["total_s"] for k in base if k in d[a]]
            row += f"{(geo(rr)-1)*100:>9.2f}%" if rr else f"{'-':>10}"
        fl = [d["off2"][k]["total_s"] / base[k]["total_s"] for k in base if k in d.get("off2", {})]
        row += f"{(geo(fl)-1)*100:>8.2f}%" if fl else f"{'-':>9}"
        print(row + f"{len(base):>6}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    want = lambda k: (not a.only) or k in a.only.split(",")

    if want("sqlite_ecdsa"):
        gem5_fsweep(G5 / "fsweep_master/results.jsonl",
                    "BENCH 1  SQLite (public) + libsecp256k1 ECDSA (secret), gem5")
        gem5_lane_ipc(G5 / "fsweep_master/results.jsonl",
                      "BENCH 1  lane-separated IPC")
    if want("lua_ecdsa"):
        gem5_fsweep(G5 / "fsweep_lua/results.jsonl",
                    "BENCH 2  Lua 5.4 (public) + libsecp256k1 ECDSA (secret), gem5")
        gem5_lane_ipc(G5 / "fsweep_lua/results.jsonl",
                      "BENCH 2  lane-separated IPC")
    if want("sqlcipher"):
        sqlcipher()
    if want("phi"):
        gem5_fsweep(G5 / "vsweep_master/results.jsonl",
                    "BENCH 1b false-positive dial (ecdsa_verify lane), gem5",
                    zero=(1, 12))
    if want("R"):
        gem5_fsweep(G5 / "rsweep_master/results.jsonl",
                    "BENCH 1c region-size dial at fixed f, gem5", zero=(1, 1),
                    arms=("off", "always", "batch", "hoist", "swcyc30"))
    if want("native"):
        native("fsweep", ["sigs", "period"],
               ["always", "oracle", "hoist", "gated", "hoist0"],
               "BENCH 1 NATIVE  SQLite + libsecp256k1 ECDSA")
        native("sodium_lua_f", ["period"],
               ["always", "oracle", "batch", "hoist", "gated", "func", "nopctl"],
               "BENCH 3 NATIVE  Lua + libsodium AEAD")
        native("sodium_sqlite_f", ["period"],
               ["always", "oracle", "batch", "hoist", "gated", "func", "nopctl"],
               "BENCH 4 NATIVE  SQLite + libsodium AEAD")
        native("typeb", ["K"],
               ["always", "oracle", "hoist", "gated", "func", "nopctl"],
               "BENCH 5 NATIVE  type-B: public code computing ON secret data",
               fkey=None)


if __name__ == "__main__":
    main()
