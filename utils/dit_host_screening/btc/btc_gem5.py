#!/usr/bin/env python3
"""Bitcoin Core SignTransactionECDSA under gem5.

WHY RUN IT HERE TOO. Silicon says how much always-on DIT costs; it cannot say
what the pass would cost on hardware whose `msr dit` is not a pipeline barrier.
Apple silicon serializes the switch, so on SignTransactionECDSA -- a ~100%
secret workload where the pass has almost no public work to protect and can only
pay for the toggles it inserts -- the pass loses to blanket DIT. The open
question is whether that verdict is a property of the placement or a property of
the switch, and only gem5 can answer it, by running the identical binaries under
two switch models:

    spec     `msr dit` rename-resolved, both directions          (the proposal)
    serdit   `msr dit` as a barrier, --no-speculative-dit        (silicon today)

The difference between those two columns, on one binary, IS the serialization
cost. It is not inferred from a cycle budget.

Arms:
    base     btc_sign_base,  DIT_MODE=0   reference
    blanket  btc_sign_blanket,DIT_MODE=0  always-on: base plus a CONSTRUCTOR
                                          that sets DIT before main. The switch
                                          is never inside the measured function
    nodit    btc_sign_nodit, DIT_MODE=0   round-trip control (empty seed file)
    taint    btc_sign_taint, DIT_MODE=0   the shipped pass, 9 seeds

Two gates, both enforced rather than documented (each has failed before):
  1. simInsts identical across switch models for a given arm. If a config
     perturbs the instruction stream, the cycle comparison is void.
  2. ditSuppressed == 0 in every arm that should carry no DIT (base, nodit). A
     baseline silently running with protection looks exactly like a win.
  3. Every arm runs from an EQUAL-LENGTH path. gem5 SE mode writes the binary
     path onto the initial process stack as argv[0], so its LENGTH shifts stack
     alignment for the whole run. One byte-identical binary measured 287,318 /
     285,068 / 284,936 cycles at a 1-, 36- and 18-char name: 0.84% from the file
     name alone. Arms named btc_sign_base (13) and btc_sign_blanket (16) differ,
     so comparing them where they are built confounds every delta. See
     dit-gem5-rig-traps #5.
"""
import argparse, csv, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G = pathlib.Path.home() / "Documents/gem5-DIT"
GEM5 = G / "build/ARM/gem5.opt"
CONFIG = G / "configs/example/arm/fdp_neoverse_v2_binary.py"
BIN = G / "benchmarks/bitcoin/bin"

# bench -> arm -> binary. DIT_MODE is vestigial (kept at 0): the blanket arm
# gets its switch from a constructor, never from the driver.
BENCHES = {
    # ~100% secret. The pass has no public work to protect and can only pay for
    # its toggles, so this is where the switch model should matter most.
    "sign": {
        "base": "btc_sign_base", "blanket": "btc_sign_blanket",
        "nodit": "btc_sign_nodit", "taint": "btc_sign_taint",
    },
    # 0% secret, and the largest always-on DIT cost this project has measured on
    # production code (+13.3% on silicon). No pass arm: the question here is
    # only whether gem5 reproduces a prize that size at all.
    "coinsel": {
        "base": "btc_coinsel_base", "blanket": "btc_coinsel_blanket",
    },
}
# Arms that must show zero DIT suppression.
UNPROTECTED = ("base", "nodit")

CONFIGS = {
    "spec":   ["--eves", "--dmp", "--comp-simp"],
    "serdit": ["--eves", "--dmp", "--comp-simp", "--no-speculative-dit"],
    # Feature isolation: DIT can only cost what the mechanisms it disables are
    # worth, so run blanket-vs-base with one mechanism at a time. "none" is the
    # floor -- with nothing for DIT to suppress it must read ~0, and anything it
    # does read is layout, not DIT.
    "none":     [],
    "eves":     ["--eves"],
    "dmp":      ["--dmp"],
    "compsimp": ["--comp-simp"],
    "sip":      ["--sip"],
}

STAT_RE = re.compile(r"^(\S+)\s+([-\d.eninf]+)")


def first_dump(path):
    """First Begin/End block only: that is the ROI. Parsing the last dump picks
    up gem5 teardown instead -- a mistake already made on this rig."""
    stats, inside = {}, False
    with open(path) as fh:
        for line in fh:
            if line.startswith("---------- Begin Simulation Statistics"):
                if inside:
                    break
                inside = True
                continue
            if line.startswith("---------- End Simulation Statistics"):
                break
            if not inside:
                continue
            m = STAT_RE.match(line)
            if m:
                try:
                    stats[m.group(1)] = float(m.group(2))
                except ValueError:
                    pass
    return stats


def pick(s, *keys):
    for k in keys:
        for full, v in s.items():
            if full.endswith(k):
                return v
    return None


def canon_path(arm, cfg, binary):
    """An equal-length path for every arm (gate 3).

    Fixed-width hex slot, so the string length is identical for every arm and
    config while staying unique enough for the parallel jobs not to collide.
    Hard-linked beside the binaries so this costs no disk and no copy time;
    falls back to a copy if the link cannot be made.
    """
    slot = hashlib.md5(f"{arm}/{cfg}".encode()).hexdigest()[:8]
    c = BIN / "armlink" / slot / "b"
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists():
        c.unlink()
    try:
        os.link(BIN / binary, c)
    except OSError:
        shutil.copy2(BIN / binary, c)
    return c


def run_one(job):
    arm, cfg, outroot, a = job
    binary = BENCHES[a.bench][arm]
    d = outroot / f"{arm}__{cfg}"
    d.mkdir(parents=True, exist_ok=True)
    cmd = [str(GEM5), "-d", str(d), str(CONFIG)] + CONFIGS[cfg] + [
        "--binary", str(canon_path(arm, cfg, binary)),
        "--arguments", f"{a.iter} {a.warmup} {a.targets}".strip(),
    ]
    t0 = time.time()
    with open(d / "run.log", "w") as log:
        rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT).returncode
    rec = {"arm": arm, "cfg": cfg, "rc": rc, "wall_s": round(time.time() - t0, 1)}
    sp = d / "stats.txt"
    if rc == 0 and sp.exists():
        s = first_dump(sp)
        rec.update({
            "cycles": pick(s, "core.numCycles", "numCycles"),
            "simInsts": pick(s, "simInsts"),
            "ditSuppressed": pick(s, "ditSuppressed") or 0.0,
            "ditSwitches": pick(s, "ditSwitches") or 0.0,
            "commitNonSpecStalls": pick(s, "commitNonSpecStalls") or 0.0,
        })
    # The driver prints its checksum; arms that disagree did different work.
    log_txt = (d / "run.log").read_text(errors="replace") if (d / "run.log").exists() else ""
    m = re.search(r"checksum=(\d+)", log_txt)
    rec["checksum"] = m.group(1) if m else ""
    print(f"  {arm:<9} {cfg:<7} rc={rc} cycles={rec.get('cycles')} "
          f"ck={rec['checksum']} {rec['wall_s']}s", flush=True)
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", default="sign", choices=sorted(BENCHES))
    ap.add_argument("--arms", default="")
    ap.add_argument("--configs", default="spec,serdit")
    ap.add_argument("--iter", type=int, default=50)
    ap.add_argument("--warmup", type=int, default=2)
    # coinsel only: selections per pass. The pool stays at the benchmark's 400
    # coins; fewer targets just shortens the run.
    ap.add_argument("--targets", type=int, default=10)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--out", default=str(pathlib.Path.home() /
                                         "Documents/dit-browser-bench/gem5-btc"))
    ap.add_argument("--tag", default="")
    a = ap.parse_args()
    if not a.arms:
        a.arms = ",".join(BENCHES[a.bench])

    for p in (GEM5, CONFIG, BIN):
        if not p.exists():
            sys.exit(f"missing: {p}")

    outroot = pathlib.Path(a.out) / (a.tag or a.bench)
    outroot.mkdir(parents=True, exist_ok=True)
    jobs = [(arm, cfg, outroot, a)
            for arm in a.arms.split(",") for cfg in a.configs.split(",")]
    print(f"gem5 Bitcoin [{a.bench}]: {len(jobs)} runs, {a.jobs} parallel")
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        recs = list(ex.map(run_one, jobs))

    csv_path = outroot / f"btc_gem5_{a.bench}.csv"
    cols = ["arm", "cfg", "rc", "cycles", "simInsts", "ditSuppressed",
            "ditSwitches", "commitNonSpecStalls", "checksum", "wall_s"]
    with open(csv_path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in recs:
            w.writerow(r)
    print("WROTE", csv_path)

    # ---- gates ----------------------------------------------------------
    ok = True
    by = {(r["arm"], r["cfg"]): r for r in recs}
    failed = [r for r in recs if r["rc"] != 0]
    if failed:
        ok = False
        print(f"\nGATE FAIL: {len(failed)} run(s) returned nonzero: "
              f"{[(r['arm'], r['cfg']) for r in failed]}")

    print("\n--- gate 1: simInsts identical across switch models ---")
    for arm in a.arms.split(","):
        vals = {c: by[(arm, c)].get("simInsts") for c in a.configs.split(",")
                if (arm, c) in by}
        uniq = set(v for v in vals.values() if v is not None)
        status = "OK" if len(uniq) <= 1 else "FAIL"
        if len(uniq) > 1:
            ok = False
        print(f"  {arm:<9} {status:<5} {vals}")

    print("\n--- gate 2: ditSuppressed == 0 in unprotected arms ---")
    for arm in [x for x in UNPROTECTED if x in a.arms.split(",")]:
        for c in a.configs.split(","):
            if (arm, c) not in by:
                continue
            v = by[(arm, c)].get("ditSuppressed")
            status = "OK" if not v else "FAIL"
            if v:
                ok = False
            print(f"  {arm:<9} {c:<7} {status:<5} ditSuppressed={v}")

    # Gate 3 compares cycles ACROSS configs, which is only meaningful when the
    # configs differ solely in the DIT switch model. Mechanism-isolation configs
    # (none/eves/dmp/...) legitimately change base cycles, so the gate would
    # misfire; restrict it to the switch-model pair.
    SWITCH_MODEL_CFGS = {"spec", "serdit"}
    gate3_cfgs = [c for c in a.configs.split(",") if c in SWITCH_MODEL_CFGS]
    if len(gate3_cfgs) < 2:
        print("\n--- gate 3: skipped (needs both spec and serdit; configs vary "
              "mechanisms, not the switch model) ---")
    else:
        print("\n--- gate 3: switch model is a no-op on arms with no DIT switch ---")
    # base and nodit contain zero `msr DIT`, so --no-speculative-dit rewrites
    # nothing in them and their cycle counts MUST be identical across models.
    # A nonzero delta here means the knob under test is reaching the controls,
    # and it bounds nothing until it is fixed. This gate caught exactly that:
    # a runtime blanket switch inside main() moved `nodit` by 0.549%.
        for arm in [x for x in UNPROTECTED if x in a.arms.split(",")]:
            cs = [by[(arm, c)].get("cycles") for c in gate3_cfgs if (arm, c) in by]
            cs = [x for x in cs if x]
            if len(cs) < 2:
                continue
            delta = (max(cs) / min(cs) - 1) * 100
            status = "OK" if delta == 0.0 else "FAIL"
            if delta != 0.0:
                ok = False
            print(f"  {arm:<9} {status:<5} delta={delta:+.4f}%  cycles={cs}")

    print("\n--- checksums (must all match) ---")
    cks = {r["checksum"] for r in recs if r["checksum"]}
    print(f"  {cks}  {'OK' if len(cks) <= 1 else 'FAIL: arms did different work'}")
    if len(cks) > 1:
        ok = False

    # ---- result ---------------------------------------------------------
    print("\n--- cycles vs base, per switch model ---")
    hdr = f"{'arm':<9}" + "".join(f"{c:>22}" for c in a.configs.split(","))
    print(hdr)
    for arm in a.arms.split(","):
        line = f"{arm:<9}"
        for c in a.configs.split(","):
            r, b = by.get((arm, c)), by.get(("base", c))
            if r and b and r.get("cycles") and b.get("cycles"):
                pct = (r["cycles"] / b["cycles"] - 1) * 100
                line += f"{r['cycles']:>12,.0f} {pct:>+7.2f}%"
            else:
                line += f"{'-':>22}"
        print(line)

    print("\nGATES", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
