#!/usr/bin/env python3
"""The instruction-level interleaving regime, on Apple silicon.

K is the interleaving granularity: the Lua loop touches the secret string every
K-th iteration, so K = 1 means public and secret work alternate continuously
inside the interpreter and there is no call boundary at which to place a switch.

THE ORACLE IS THE INTERESTING ARM. With no boundary, the best a human can do is
bracket the whole secret-processing phase in one region - which is what Apple's
timingsafe_enable/restore and AWS-LC's caller-level hoisting actually do. So
`oracle` here wraps the entire script execution, and the question is whether any
compiler-placed alternative can beat it. The prediction is that none can, and
that this is a property of the regime rather than of this compiler.
"""
import argparse, json, math, os, pathlib, re, statistics, subprocess, sys, time
from collections import defaultdict

HOME = pathlib.Path.home()
BIN = HOME / "Documents/dit-crossover/build/luataint"
PROBE = HOME / "Documents/dit-crossover/build/native/dit_probe"
DIT_DYLIB = HOME / "Documents/dit-browser-bench/dit_on.dylib"

ARMS = [
    ("off",    "nodit",  0),
    ("always", "nodit",  1),   # DIT process-wide
    ("oracle", "nodit",  2),   # one region around the whole secret phase
    ("hoist",  "hoist",  0),
    ("gated",  "gated",  0),
    ("func",   "func",   0),
    ("nopctl", "nopctl", 0),
    ("off2",   "nodit",  0),
]

K_GRID = [1, 2, 4, 16, 64, 256, 1024, 4096]
RX = re.compile(r"total_s=([\d.]+)")
CK = re.compile(r"checksum (\d+) (\d+)")


def probe(dit=False):
    if not PROBE.exists():
        return None
    env = dict(os.environ)
    if dit:
        env["DYLD_INSERT_LIBRARIES"] = str(DIT_DYLIB)
    o = subprocess.run([str(PROBE), "2000000"], capture_output=True, text=True, env=env)
    m = re.search(r"const_ns_per_hop=([\d.]+) perm_ns_per_hop=([\d.]+) "
                  r"const_over_perm=([\d.]+)", o.stdout)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def preflight():
    if os.environ.get("XOVER_SKIP_PREFLIGHT") == "1":
        print("pre-flight: SKIPPED - harness test, NOT a measurement")
        return
    busy = subprocess.run(["ps", "ax"], capture_output=True, text=True).stdout
    bad = [w for w in ("gem5.opt", "gem5.fast", "ninja", "scons")
           if any(w in l and "grep" not in l for l in busy.splitlines())]
    if bad:
        sys.exit(f"pre-flight FAILED: {bad} running")
    off = None
    for i in range(30):
        off = probe()
        if off and off[2] <= 0.35:
            break
        time.sleep(15)
    if not off or off[2] > 0.35:
        sys.exit(f"pre-flight FAILED: const/perm={off[2] if off else '?'}, expected ~0.26")
    ons = [o for o in (probe(dit=True) for _ in range(5)) if o] if DIT_DYLIB.exists() else []
    best = min((o[2] for o in ons), default=None)
    print(f"pre-flight: DIT off const/perm={off[2]:.4f}" +
          (f"   DIT on const/perm={best:.4f} (min of {len(ons)})" if ons else ""))
    if best is not None and not (0.95 <= best <= 1.10):
        sys.exit(f"pre-flight FAILED: DIT-on const/perm={best:.4f}, expected ~1.00")


def geomean(xs):
    xs = [x for x in xs if x > 0]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


def ci95(xs):
    return 1.96 * statistics.stdev(xs) / math.sqrt(len(xs)) if len(xs) > 1 else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=20)
    ap.add_argument("--burnin", type=int, default=3)
    ap.add_argument("--n", type=int, default=20_000_000)
    ap.add_argument("--seclen", type=int, default=4096)
    ap.add_argument("--out", default=str(HOME / "Documents/dit-crossover/out/native/typeb.jsonl"))
    a = ap.parse_args()

    arms = [x for x in ARMS if (BIN / f"typeb_{x[1]}").exists()]
    preflight()
    out = pathlib.Path(a.out); out.parent.mkdir(parents=True, exist_ok=True)
    recs, t0 = [], time.time()
    print(f"typeb: {len(K_GRID)} K values x {len(arms)} arms x {a.reps}+{a.burnin} reps, "
          f"N={a.n}", flush=True)

    for K in K_GRID:
        for r in range(-a.burnin, a.reps):
            order = arms[r % len(arms):] + arms[:r % len(arms)]
            for arm, binname, mode in order:
                o = subprocess.run(
                    [str(BIN / f"typeb_{binname}"), str(BIN / "work_typeb.lua"),
                     str(mode), str(K), str(a.n), str(a.seclen)],
                    capture_output=True, text=True).stdout
                m, c = RX.search(o), CK.search(o)
                if not m or r < 0:
                    continue
                recs.append({"arm": arm, "rep": r, "K": K,
                             "total_s": float(m.group(1)),
                             "checksum": (c.group(1), c.group(2)) if c else None})
        sel = [x for x in recs if x["K"] == K]
        per = {(x["arm"], x["rep"]): x["total_s"] for x in sel}
        base = {r: v for (arm, r), v in per.items() if arm == "off"}
        cells = {}
        for arm, _, _ in arms:
            rr = [per[(arm, r)] / base[r] for (aa, r) in per if aa == arm and r in base]
            if rr:
                cells[arm] = (100 * (geomean(rr) - 1), 100 * ci95(rr))
        b = statistics.median([x["total_s"] for x in sel if x["arm"] == "off"])
        line = "  ".join(f"{k}={v[0]:+.2f}" for k, v in cells.items() if k != "off")
        print(f"  K={K:<6} base={b*1000:7.1f}ms  {line}", flush=True)
        with open(out, "w") as fh:
            for x in recs:
                fh.write(json.dumps(x) + "\n")

    cks = defaultdict(set)
    for x in recs:
        cks[x["K"]].add(x["checksum"])
    for k, v in cks.items():
        if len(v) > 1:
            print(f"!! GATE FAIL: checksums differ at K={k}: {v}")
    print(f"{len(recs)} records in {(time.time()-t0)/60:.1f} min -> {out}")


if __name__ == "__main__":
    main()
