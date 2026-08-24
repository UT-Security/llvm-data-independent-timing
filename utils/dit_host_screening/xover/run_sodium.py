#!/usr/bin/env python3
"""The libsodium public+secret composite, on Apple silicon.

TWO PUBLIC LANES, ONE SECRET LIBRARY. SQLite's query loop (c_P = 12.66% measured)
and Lua 5.4's binary-trees (screened +14.52%) - two different mechanisms, B-tree
descent versus bytecode dispatch - so c_P is not a single point.

THREE GRIDS:

  f      Sweep the secret fraction at FIXED region size. The crossover curve.

  R      Sweep the region size at FIXED secret fraction. This is the one
         libsodium makes honest: for an AEAD, R is just the message size, so it
         moves over three orders of magnitude without changing the algorithm or
         the call pattern. Because f = n*R/T, holding f fixed while R grows means
         cutting the call rate proportionally - the grid below does that, so the
         two axes really are separated rather than confounded.

  prim   Sweep the primitive: Poly1305 -> ChaCha20-Poly1305 -> Ed25519 ->
         Argon2id, ~0.2 us to ~10 ms. Every one is in the CIO-parity seed, so the
         pass can actually protect it.

MUST BE THE ONLY THING RUNNING - native wall-clock is contended. The pre-flight
gate enforces it: the lvp_chase probe must show const/perm ~ 0.26 with DIT off
(the predictor is live) and ~1.00 with DIT injected (DIT is really gating it).
"""
import argparse, json, math, os, pathlib, re, statistics, subprocess, sys, time
from collections import defaultdict

HOME = pathlib.Path.home()
BIN = HOME / "Documents/dit-crossover/build/sodium_native"
PROBE = HOME / "Documents/dit-crossover/build/native/dit_probe"
DIT_DYLIB = HOME / "Documents/dit-browser-bench/dit_on.dylib"

PRIM = {"auth": 0, "aead": 1, "sign": 2, "pwhash": 3, "gcm": 4}

# arm -> (binary suffix, runtime DIT mode). off/always/oracle/batch share ONE
# binary and differ only by argv, so no codegen differs between them.
# CONFIRMATION RUN (2026-08-24). The compiler defaults moved, so `def30` IS the
# shipped default and the only knob varied against it is -taint-dit-switch-cyc,
# at 0 and 30. off/always/oracle/batch share ONE binary and differ only by argv,
# so no codegen differs between them. nop0/nop30 are the alignment control (every
# `msr DIT` emitted as `HINT #0` at the same address): nop30-vs-nop0 is the pure
# LAYOUT delta, so def30-vs-def0 minus it is the switch delta - required, because
# the claim under test is about switch COUNT. off2 is the same binary as off, run
# last: off-vs-off2 is the drift check on the machine itself.
ARMS = [
    ("off",    "nodit",  0),
    ("always", "nodit",  1),
    ("oracle", "nodit",  2),
    ("batch",  "nodit",  3),
    ("def30",  "def30",  0),   # the shipped default
    ("def0",   "def0",   0),   # ... with corridor merging disabled
    ("nop30",  "nop30",  0),   # alignment control for def30
    ("nop0",   "nop0",   0),   # alignment control for def0
    ("off2",   "nodit",  0),
]

RX = re.compile(r"total_s=([\d.]+) secret_s=([\d.]+) secret_frac=([\d.]+)% "
                r"ops=(\d+) R_us=([\d.]+) toggles=(\d+)")
CK = re.compile(r"checksum (\d+)")

# (msgsize, period, nper) chosen so f stays ~5% while R moves 1280x.
# Derived from the measured R: 64 B = 0.24 us, 1 KB = 1.46, 16 KB = 20.5,
# 256 KB = 307.6, against ~0.37 s of public work.
R_GRID = [(64, 1, 14), (1024, 1, 2), (16384, 6, 1), (262144, 95, 1)]

# f sweep at fixed R (1 KB AEAD).
F_GRID = [(1024, 3000, 1), (1024, 1000, 1), (1024, 300, 1), (1024, 100, 1),
          (1024, 30, 1), (1024, 10, 1), (1024, 3, 1), (1024, 1, 1),
          (1024, 1, 4), (1024, 1, 16)]


def probe(dit=False):
    if not PROBE.exists():
        return None
    env = dict(os.environ)
    if dit:
        env["DYLD_INSERT_LIBRARIES"] = str(DIT_DYLIB)
    out = subprocess.run([str(PROBE), "2000000"], capture_output=True, text=True, env=env)
    m = re.search(r"const_ns_per_hop=([\d.]+) perm_ns_per_hop=([\d.]+) "
                  r"const_over_perm=([\d.]+)", out.stdout)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def preflight():
    """The probe prints const/perm, so HEALTHY is ~0.26 - the constant chase is
    the fast one when the predictor works. Gating on the reciprocal once threw
    away a whole sweep on a perfectly quiet machine."""
    if os.environ.get("XOVER_SKIP_PREFLIGHT") == "1":
        print("pre-flight: SKIPPED - harness test, NOT a measurement")
        return
    busy = subprocess.run(["ps", "ax"], capture_output=True, text=True).stdout
    bad = [w for w in ("gem5.opt", "gem5.fast", "ninja", "scons")
           if any(w in l and "grep" not in l for l in busy.splitlines())]
    if bad:
        sys.exit(f"pre-flight FAILED: {bad} running; native timing needs the machine alone")
    off = None
    for i in range(30):
        off = probe()
        if off and off[2] <= 0.35:
            break
        print(f"  probe {i+1}: const/perm={off[2] if off else '?'} - still ramping")
        time.sleep(15)
    if not off or off[2] > 0.35:
        sys.exit(f"pre-flight FAILED: const/perm={off[2] if off else '?'}, expected ~0.26")
    # Under DIT the constant chase must land ON the permuted line. Noise in this
    # ratio is ONE-DIRECTIONAL - it can only inflate, never deflate below 1.0 -
    # so the least-noisy of several samples is the right estimator, and a single
    # sample reading 1.078 rejected a demonstrably healthy machine.
    ons = [probe(dit=True) for _ in range(5)] if DIT_DYLIB.exists() else []
    ons = [o for o in ons if o]
    if ons:
        best = min(o[2] for o in ons)
        print(f"pre-flight: DIT off const/perm={off[2]:.4f}   "
              f"DIT on const/perm={best:.4f} (min of {len(ons)})")
        if not (0.95 <= best <= 1.10):
            sys.exit(f"pre-flight FAILED: DIT-on const/perm={best:.4f}, expected ~1.00 - "
                     f"DIT is not gating the predictor, so this rig is not measuring DIT")
    else:
        print(f"pre-flight: DIT off const/perm={off[2]:.4f}  (no injector)")


def run(lane, binname, mode, a, prim, msg, period, nper):
    p = BIN / f"xsod_{lane}_{binname}"
    if lane == "sqlite":
        cmd = [str(p), str(mode), str(prim), str(msg), str(period), str(nper),
               str(a.rows), str(a.rounds), str(a.ops), str(a.mem)]
    else:
        cmd = [str(p), str(BIN / "work_sodium.lua"), str(mode), str(prim), str(msg),
               str(period), str(nper), str(a.depth), str(a.ops), str(a.mem)]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    m, c = RX.search(out), CK.search(out)
    if not m:
        return None
    return {"total_s": float(m.group(1)), "secret_s": float(m.group(2)),
            "f": float(m.group(3)), "ops": int(m.group(4)),
            "R_us": float(m.group(5)), "toggles": int(m.group(6)),
            "checksum": int(c.group(1)) if c else None}


def geomean(xs):
    xs = [x for x in xs if x > 0]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


def ci95(xs):
    return 1.96 * statistics.stdev(xs) / math.sqrt(len(xs)) if len(xs) > 1 else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lane", choices=["sqlite", "lua"], default="sqlite")
    ap.add_argument("--grid", choices=["f", "R", "prim", "deploy"], default="f")
    ap.add_argument("--prim", default="aead")
    ap.add_argument("--reps", type=int, default=20)
    ap.add_argument("--burnin", type=int, default=3)
    ap.add_argument("--rows", type=int, default=4000)
    ap.add_argument("--rounds", type=int, default=60)
    ap.add_argument("--depth", type=int, default=15)
    ap.add_argument("--ops", type=int, default=2)
    ap.add_argument("--mem", type=int, default=1 << 24)
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    prim = PRIM[a.prim]
    if a.grid == "f":
        pts = [(prim, m, p, n) for (m, p, n) in F_GRID]
    elif a.grid == "R":
        pts = [(prim, m, p, n) for (m, p, n) in R_GRID]
    elif a.grid == "deploy":
        # Message sizes taken from VERIFIED deployments, to pin R* against real
        # traffic rather than an interpolation:
        #   200/330 B  Discord voice frames (20 ms Opus; libsodium AEAD is
        #              officially REQUIRED of every Discord voice client)
        #   512 B      DNSCrypt query, padded to a 64-byte multiple
        #   1400 B     c-toxcore MAX_CRYPTO_PACKET_SIZE
        #   4 KiB      the chunk size in libsodium's own secretstream example
        #   64 KiB     rclone crypt block; the same figure age's STREAM uses
        # period is scaled with size so the secret fraction stays ~4-5% and the
        # region size is the only thing moving.
        pts = [(prim, 200, 1, 5), (prim, 330, 1, 3), (prim, 512, 1, 2),
               (prim, 1400, 1, 1), (prim, 4096, 2, 1), (prim, 65536, 24, 1)]
    else:
        # one point per primitive, call rate tuned so f lands in the same ballpark
        pts = [(PRIM["auth"], 1024, 1, 8), (PRIM["aead"], 1024, 1, 2),
               (PRIM["aead"], 16384, 6, 1), (PRIM["sign"], 1024, 6, 1),
               (PRIM["aead"], 262144, 95, 1), (PRIM["pwhash"], 32, 600, 1)]

    arms = [x for x in ARMS if (BIN / f"xsod_{a.lane}_{x[1]}").exists()]
    missing = {x[1] for x in ARMS} - {x[1] for x in arms}
    if missing:
        print(f"note: missing binaries for {sorted(missing)}")

    preflight()
    out = pathlib.Path(a.out or (HOME / f"Documents/dit-crossover/out/native/sodium_{a.lane}_{a.grid}.jsonl"))
    out.parent.mkdir(parents=True, exist_ok=True)
    recs, t0 = [], time.time()
    print(f"sodium {a.lane}/{a.grid}: {len(pts)} points x {len(arms)} arms x "
          f"{a.reps}+{a.burnin} reps", flush=True)

    for (pr, msg, period, nper) in pts:
        for r in range(-a.burnin, a.reps):
            order = arms[r % len(arms):] + arms[:r % len(arms)]     # ROTATE
            for arm, binname, mode in order:
                res = run(a.lane, binname, mode, a, pr, msg, period, nper)
                if res is None or r < 0:
                    continue
                res.update({"arm": arm, "rep": r, "prim": pr, "msgsize": msg,
                            "period": period, "nper": nper, "lane": a.lane})
                recs.append(res)
        sel = [x for x in recs if x["msgsize"] == msg and x["period"] == period
               and x["nper"] == nper and x["prim"] == pr]
        per = defaultdict(dict)
        for x in sel:
            per[(x["arm"], x["rep"])] = x["total_s"]
        base = {r: v for (arm, r), v in per.items() if arm == "off"}
        cells = {}
        for arm, _, _ in arms:
            rr = [per[(arm, r)] / base[r] for (aa, r) in per if aa == arm and r in base]
            if rr:
                cells[arm] = (100 * (geomean(rr) - 1), 100 * ci95(rr))
        f = statistics.median([x["f"] for x in sel if x["arm"] == "off"])
        R = statistics.median([x["R_us"] for x in sel if x["arm"] == "off"])
        line = "  ".join(f"{k}={v[0]:+.2f}" for k, v in cells.items() if k != "off")
        print(f"  msg={msg:<7} p={period:<5} n={nper:<3} f={f:6.3f}%  R={R:8.3f}us  {line}",
              flush=True)
        with open(out, "w") as fh:
            for x in recs:
                fh.write(json.dumps(x) + "\n")

    cks = defaultdict(set)
    for x in recs:
        cks[(x["prim"], x["msgsize"], x["period"], x["nper"])].add(x["checksum"])
    for k, v in cks.items():
        if len(v) > 1:
            print(f"!! GATE FAIL: checksums differ at {k}: {v}")
    print(f"{len(recs)} records in {(time.time()-t0)/60:.1f} min -> {out}")


if __name__ == "__main__":
    main()
