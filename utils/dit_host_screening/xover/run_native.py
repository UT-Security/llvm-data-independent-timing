#!/usr/bin/env python3
"""The crossover sweep on Apple silicon.

MUST BE THE ONLY THING RUNNING. Native wall-clock is contended; a gem5 matrix or
a build stealing cores inflates arms unevenly. This was learned by launching a
Bitcoin Core measurement alongside 20 gem5 processes and having to quarantine the
data. gem5 during other work is fine; other work during native is not.

CONTROLS, every one of which caught a real defect on this project:
  * ARM ORDER ROTATED every rep. A fixed order penalises whichever arm runs last
    by ~1.3% - demonstrated with an instruction-identical arm.
  * `off2`, a second copy of the baseline arm, measures the noise floor on every
    run instead of assuming it.
  * `dit_probe` (the lvp_chase constant-vs-permuted chase) runs IN BAND every
    rep. It prints const/perm, so a HEALTHY reading is ~0.26 - the constant chase
    is the FAST one when the predictor works. Contended, it reads ~1.00. Without
    it, a table of 1.00x ratios is indistinguishable from a rig that is not
    measuring DIT at all.
  * Checksums must be identical across arms, or the arms are doing different work.
  * `off` is the ROUND-TRIP CONTROL binary, not the stock build. The 3-phase MIR
    round trip perturbs codegen by itself with zero msr DIT emitted, by between
    +0.06% and +2.65% depending on the binary. `plain` is carried only so that
    artifact can be reported rather than silently charged to DIT.
  * A DIT-on arm reading below 1.00x is an artifact, not a win: DIT can only
    remove optimizations.

Reported as median per rep, with the geometric mean across points - arithmetic
means over ratios depend on which arm is the baseline and are a documented
benchmarking flaw (van der Kouwe et al., EuroS&P'19, flaw B5).
"""
import argparse, itertools, json, os, pathlib, re, statistics, subprocess, sys, time

HOME = pathlib.Path.home()
BIN = HOME / "Documents/dit-crossover/build/native"
PROBE = HOME / "Documents/dit-crossover/build/native/dit_probe"

ARMS = [
    ("off",    "nodit",  0),
    ("always", "nodit",  1),
    ("oracle", "nodit",  2),
    ("hoist",  "hoist",  0),
    ("gated",  "gated",  0),
    ("hoist0", "hoist0", 0),
    ("off2",   "nodit",  0),
]

RX = re.compile(
    r"total_s=([\d.]+) secret_s=([\d.]+) verify_s=([\d.]+) secret_frac=([\d.]+)%"
    r" signs=(\d+) verifs=(\d+) toggles=(\d+)")
CK = re.compile(r"checksum (\d+)")

# (sigs, period): f from ~0.01% to ~55% with the public work identical.
F_GRID = [(1, 1200), (1, 300), (1, 100), (1, 30), (1, 10),
          (1, 3), (1, 1), (2, 1), (4, 1), (8, 1)]


def run(binname, mode, a, sigs, period, verifies):
    p = BIN / f"xover_{binname}"
    out = subprocess.run(
        [str(p), str(mode), str(a.rows), str(a.rounds), str(sigs),
         str(period), str(verifies)],
        capture_output=True, text=True)
    m = RX.search(out.stdout)
    c = CK.search(out.stdout)
    if not m:
        return None
    return {"total_s": float(m.group(1)), "secret_s": float(m.group(2)),
            "verify_s": float(m.group(3)), "f": float(m.group(4)),
            "signs": int(m.group(5)), "verifs": int(m.group(6)),
            "toggles": int(m.group(7)),
            "checksum": int(c.group(1)) if c else None}


DIT_DYLIB = pathlib.Path.home() / "Documents/dit-browser-bench/dit_on.dylib"


def probe(dit=False):
    """The lvp_chase positive control.

    NOTE ON THE RATIO'S DIRECTION, which cost a whole failed sweep. The probe
    prints const_over_perm = const / perm. The constant-valued chase is the FAST
    one when the load-value predictor is working, so a HEALTHY reading is ~0.26,
    NOT ~4. The documented '~4x' is a different quantity (const with DIT on
    divided by const with DIT off). Gating on `ratio >= 3.5` rejects a perfectly
    good machine, which is exactly what happened on the first attempt.

    Returns (const_ns, perm_ns, const/perm)."""
    if not PROBE.exists():
        return None
    env = dict(os.environ)
    if dit:
        env["DYLD_INSERT_LIBRARIES"] = str(DIT_DYLIB)
    out = subprocess.run([str(PROBE), "2000000"], capture_output=True,
                         text=True, env=env)
    m = re.search(r"const_ns_per_hop=([\d.]+) perm_ns_per_hop=([\d.]+) "
                  r"const_over_perm=([\d.]+)", out.stdout)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def settle(max_tries=30):
    """Wait until the machine is actually HEALTHY, not merely stable.

    The first version waited for perm_ns to stop changing by more than 8% between
    probes and then declared victory. After a heavy gem5 load the times decay
    slowly, so two consecutive samples matched while still 6x above the quiet
    value, and it "settled" at perm=2.014 with const/perm=1.25 - the no-predictor
    signature. Stability is not health. Gate on the known-good signature itself
    (const/perm ~ 0.26, i.e. the constant chase is several times faster than the
    permuted one) and give the frequency ramp time to finish."""
    best = None
    for i in range(max_tries):
        p = probe()
        if p is None:
            return None
        if best is None or p[2] < best[2]:
            best = p
        if p[2] <= 0.35:
            print(f"  machine healthy after {i+1} probes "
                  f"(const={p[0]:.3f} perm={p[1]:.3f} const/perm={p[2]:.4f})")
            return p
        print(f"  probe {i+1}: const/perm={p[2]:.4f} perm={p[1]:.3f} - still ramping")
        time.sleep(15)
    return best


def preflight():
    """Refuse to start a native measurement the machine cannot support.

    XOVER_SKIP_PREFLIGHT=1 bypasses this. It exists ONLY to smoke-test the
    harness; data produced under it is not a measurement."""
    if os.environ.get("XOVER_SKIP_PREFLIGHT") == "1":
        print("pre-flight: SKIPPED - harness test, NOT a measurement")
        return None
    busy = subprocess.run(["ps", "ax"], capture_output=True, text=True).stdout
    offenders = [w for w in ("gem5.opt", "gem5.fast", "ninja", "scons")
                 if any(w in l and "grep" not in l for l in busy.splitlines())]
    if offenders:
        sys.exit(f"pre-flight FAILED: {offenders} still running. Native timing "
                 f"must have the machine to itself.")

    off = settle()
    if off is None:
        sys.exit("pre-flight FAILED: no control reading - build dit_probe first")
    on = probe(dit=True) if DIT_DYLIB.exists() else None

    print(f"pre-flight: DIT off const={off[0]:.3f} perm={off[1]:.3f} "
          f"const/perm={off[2]:.4f}")
    # [A] The load-value predictor must be live, i.e. the constant chase must be
    # much faster than the permuted one. Quiet machine reads ~0.26; with nine
    # gem5 processes running this read 1.0008, which is the contended signature.
    if off[2] > 0.40:
        sys.exit(f"pre-flight FAILED: const/perm = {off[2]:.4f}, expected ~0.26. "
                 f"The LVP is not visible - machine contended, or this landed on "
                 f"an E-core (the LVP is a P-core feature).")
    if on:
        ratio = on[0] / off[0]
        print(f"pre-flight: DIT on  const={on[0]:.3f} perm={on[1]:.3f} "
              f"const/perm={on[2]:.4f}   (const_dit/const_off = {ratio:.2f}x)")
        # [B] The ROBUST gate: with DIT set the constant chase must land ON the
        # permuted line. This is a ratio of two SLOW measurements, so noise can
        # only inflate it and cannot fake a pass (trap 6).
        if not (0.97 <= on[2] <= 1.07):
            sys.exit(f"pre-flight FAILED: with DIT set, const/perm = {on[2]:.4f}, "
                     f"expected ~1.00. DIT is not gating the predictor, so this "
                     f"rig is not measuring DIT at all.")
    else:
        print("pre-flight: WARNING - no DIT injector, ran gate [A] only")
    return off[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=30)
    ap.add_argument("--burnin", type=int, default=3)
    ap.add_argument("--rows", type=int, default=4000)
    ap.add_argument("--rounds", type=int, default=100)
    ap.add_argument("--points", default="")
    ap.add_argument("--verifies", type=int, default=0)
    ap.add_argument("--sweep", choices=["fsweep", "vsweep"], default="fsweep")
    ap.add_argument("--out", default=str(HOME / "Documents/dit-crossover/out/native/fsweep.jsonl"))
    a = ap.parse_args()

    if a.sweep == "vsweep":
        pts = [(1, 10, v) for v in (0, 1, 2, 4, 8)]
    else:
        pts = [(s, p, a.verifies) for s, p in F_GRID]
    if a.points:
        pts = [tuple(int(x) for x in t.split(":")) for t in a.points.split(",")]

    missing = [n for _, n, _ in ARMS if not (BIN / f"xover_{n}").exists()]
    if missing:
        sys.exit(f"missing binaries: {sorted(set(missing))}")

    preflight()

    outp = pathlib.Path(a.out)
    outp.parent.mkdir(parents=True, exist_ok=True)
    recs = []
    t0 = time.time()
    print(f"native {a.sweep}: {len(pts)} points x {len(ARMS)} arms x "
          f"{a.reps}+{a.burnin} reps, rows={a.rows} rounds={a.rounds}", flush=True)

    for (sigs, period, verifies) in pts:
        for r in range(-a.burnin, a.reps):
            pr = probe() if r >= 0 else None
            ctl = pr[2] if pr else None
            order = ARMS[r % len(ARMS):] + ARMS[:r % len(ARMS)]   # ROTATE
            for arm, binname, mode in order:
                res = run(binname, mode, a, sigs, period, verifies)
                if res is None or r < 0:
                    continue
                res.update({"arm": arm, "rep": r, "sigs": sigs,
                            "period": period, "verifies": verifies,
                            "control": ctl})
                recs.append(res)
        # progress line per point
        sel = [x for x in recs if x["sigs"] == sigs and x["period"] == period
               and x["verifies"] == verifies]
        med = {arm: statistics.median([x["total_s"] for x in sel if x["arm"] == arm])
               for arm, _, _ in ARMS if any(x["arm"] == arm for x in sel)}
        base = med.get("off")
        f = statistics.median([x["f"] for x in sel if x["arm"] == "off"]) if sel else 0
        line = "  ".join(f"{k}={100*(v/base-1):+.2f}%" for k, v in med.items() if k != "off")
        print(f"  s={sigs} p={period} v={verifies}  f={f:.3f}%  base={base*1000:.1f}ms  {line}",
              flush=True)
        with open(outp, "w") as fh:
            for x in recs:
                fh.write(json.dumps(x) + "\n")

    ctls = [x["control"] for x in recs if x.get("control")]
    if ctls:
        c = statistics.median(ctls)
        print(f"\nlvp_chase in-band control: const/perm = {c:.4f}  "
              f"{'OK (LVP live)' if c <= 0.40 else '!! GATE FAIL - LVP not visible, rig contended'}")
    cks = {}
    for x in recs:
        cks.setdefault((x["sigs"], x["period"], x["verifies"]), set()).add(x["checksum"])
    for k, v in cks.items():
        if len(v) > 1:
            print(f"!! GATE FAIL: checksums differ at {k}: {v}")
    print(f"{len(recs)} records in {(time.time()-t0)/60:.1f} min -> {outp}")


if __name__ == "__main__":
    main()
