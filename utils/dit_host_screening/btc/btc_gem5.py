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
    base     btc_sign_base,  DIT_MODE=0   the baseline: built THROUGH the pass
                                          with an empty seed, so it carries
                                          everything -ftaint-harden changes
                                          besides DIT (the tail-call disable),
                                          as the silicon rig's baseline does
    blanket  btc_sign_blanket,DIT_MODE=0  always-on: base plus a CONSTRUCTOR
                                          that sets DIT before main. The switch
                                          is never inside the measured function
    taint    btc_sign_taint, DIT_MODE=0   the shipped pass, 9 seeds
    plain    btc_sign_plain, DIT_MODE=0   stock -O2, no pass: reference for what
                                          building through the pass costs
                                          (base vs plain), never a baseline

Two gates, both enforced rather than documented (each has failed before):
  1. simInsts identical across switch models for a given arm. If a config
     perturbs the instruction stream, the cycle comparison is void.
  2. ditSuppressed == 0 in every arm that should carry no DIT (base, plain). A
     baseline silently running with protection looks exactly like a win.
  3. Every arm runs from an EQUAL-LENGTH path. gem5 SE mode writes the binary
     path onto the initial process stack as argv[0], so its LENGTH shifts stack
     alignment for the whole run. One byte-identical binary measured 287,318 /
     285,068 / 284,936 cycles at a 1-, 36- and 18-char name: 0.84% from the file
     name alone. Arms named btc_sign_base (13) and btc_sign_blanket (16) differ,
     so comparing them where they are built confounds every delta. See
     dit-gem5-rig-traps #5.
"""
import argparse, csv, hashlib, json, os, pathlib, re, shutil, statistics as st
import subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G = pathlib.Path(os.environ.get("G5", str(pathlib.Path(__file__).resolve().parents[3] / "gem5-DIT")))
# gem5.fast for measurement; gem5.opt only when a run needs --debug-flags or
# the asserts, which NDEBUG/TRACING_ON=0 compile out of .fast. GEM5_BIN switches.
GEM5 = pathlib.Path(os.environ.get("GEM5_BIN", G / "build/ARM/gem5.fast"))
CONFIG = G / "configs/example/arm/fdp_neoverse_v2_binary.py"
BIN = G / "benchmarks/bitcoin/bin"

# bench -> arm -> binary. DIT_MODE is vestigial (kept at 0): the blanket arm
# gets its switch from a constructor, never from the driver.
BENCHES = {
    # ~100% secret. The pass has no public work to protect and can only pay for
    # its toggles, so this is where the switch model should matter most.
    "sign": {
        "base": "btc_sign_base", "blanket": "btc_sign_blanket",
        "taint": "btc_sign_taint", "plain": "btc_sign_plain",
    },
    # 0% secret, and the largest always-on DIT cost this project has measured on
    # production code (+13.3% on silicon). No pass arm: the question here is
    # only whether gem5 reproduces a prize that size at all.
    "coinsel": {
        "base": "btc_coinsel_base", "blanket": "btc_coinsel_blanket",
    },
}
# Arms that must show zero DIT suppression.
UNPROTECTED = ("base", "plain")

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


def canon_path(arm, cfg, binary, outroot, offset=0):
    """An equal-length path for every arm (gate 3), at a chosen offset.

    Fixed-width hex slot, so the string length is identical for every arm and
    config while staying unique enough for the parallel jobs not to collide.
    Hard-linked beside the binaries so this costs no disk and no copy time;
    falls back to a copy if the link cannot be made.

    The slot hashes the run directory and the offset as well as arm/cfg. Keyed
    on arm/cfg alone, two drivers running at once (coinsel beside coinsel4, or
    sign beside either) re-linked each other's slots, and a sign run could load
    the coinsel binary with the sign arguments - which is what happened on
    2026-09-03. Length stays 8 hex either way.

    The file name is `b` repeated offset+1 times, so consecutive offsets differ
    by exactly one byte of argv[0] and every arm at one offset shares a length.
    gem5 SE writes argv[0] onto the initial stack, so its length moves every
    stack address for the whole run (dit-gem5-rig-traps #5); a sub-2% delta is
    only quotable as a median over several offsets, with its spread.
    """
    slot = hashlib.md5(f"{outroot}/{arm}/{cfg}/o{offset}".encode()).hexdigest()[:8]
    c = BIN / "armlink" / slot / ("b" * (offset + 1))
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists():
        c.unlink()
    try:
        os.link(BIN / binary, c)
    except OSError:
        shutil.copy2(BIN / binary, c)
    return c


def done(d):
    sp, lg = d / "stats.txt", d / "run.log"
    return (sp.exists() and sp.stat().st_size > 0 and lg.exists()
            and "checksum=" in lg.read_text(errors="replace"))


def run_one(job):
    arm, cfg, off, outroot, a = job
    binary = BENCHES[a.bench][arm]
    d = outroot / f"o{off}" / f"{arm}__{cfg}"
    # The run's own configuration travels with its numbers: the driver's
    # defaults (50/2/10) are ~500x the work of the committed coinsel runs
    # (1/1/1), and a CSV that omits them cannot say which it holds.
    rec = {"arm": arm, "cfg": cfg, "offset": off, "wall_s": "",
           "iter": a.iter, "warmup": a.warmup, "targets": a.targets}
    if a.resume and done(d):
        rec["rc"] = 0
    else:
        d.mkdir(parents=True, exist_ok=True)
        cmd = [str(GEM5), "-d", str(d), str(CONFIG)] + CONFIGS[cfg] + [
            "--binary", str(canon_path(arm, cfg, binary, outroot, off)),
            "--arguments", f"{a.iter} {a.warmup} {a.targets}".strip(),
        ]
        t0 = time.time()
        with open(d / "run.log", "w") as log:
            rec["rc"] = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT).returncode
        rec["wall_s"] = round(time.time() - t0, 1)
    sp = d / "stats.txt"
    if rec["rc"] == 0 and sp.exists():
        s = first_dump(sp)
        rec.update({
            "cycles": pick(s, "core.numCycles", "numCycles"),
            # gem5's own IPC divides committed numInsts by numCycles, which is
            # not simInsts/cycles (numInsts counts a few hundred more), so both
            # are recorded and IPC is gem5's, not derived.
            "numInsts": pick(s, "core.commitStats0.numInsts", "commitStats0.numInsts"),
            "ipc": pick(s, "core.ipc"),
            "simInsts": pick(s, "simInsts"),
            "ditSuppressed": pick(s, "ditSuppressed") or 0.0,
            # Committed PSTATE.DIT writes. The model names this commit.ditWrites;
            # the earlier "ditSwitches" key matched nothing and read 0 on every arm.
            "ditSwitches": pick(s, "commit.ditWrites", "ditSwitches") or 0.0,
            "commitNonSpecStalls": pick(s, "commitNonSpecStalls") or 0.0,
        })
    # The driver prints its checksum; arms that disagree did different work.
    log_txt = (d / "run.log").read_text(errors="replace") if (d / "run.log").exists() else ""
    m = re.search(r"checksum=(\d+)", log_txt)
    rec["checksum"] = m.group(1) if m else ""
    print(f"  o{off} {arm:<9} {cfg:<7} rc={rec['rc']} cycles={rec.get('cycles')} "
          f"ipc={rec.get('ipc')} ck={rec['checksum']} {rec['wall_s']}s", flush=True)
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
    # argv[0] lengths per arm; every number reported is the median over them.
    ap.add_argument("--offsets", type=int, default=1)
    ap.add_argument("--resume", action="store_true",
                    help="skip runs whose stats.txt and checksum already exist")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--out", default=str(pathlib.Path.home() /
                                         "Documents/dit-browser-bench/gem5-btc"))
    ap.add_argument("--tag", default="")
    a = ap.parse_args()
    if not a.arms:
        a.arms = ",".join(BENCHES[a.bench])
    arms, cfgs, offs = a.arms.split(","), a.configs.split(","), list(range(a.offsets))

    for p in (GEM5, CONFIG, BIN):
        if not p.exists():
            sys.exit(f"missing: {p}")

    outroot = pathlib.Path(a.out) / (a.tag or a.bench)
    outroot.mkdir(parents=True, exist_ok=True)
    jobs = [(arm, cfg, off, outroot, a) for off in offs for arm in arms for cfg in cfgs]
    print(f"gem5 Bitcoin [{a.bench}]: {len(jobs)} runs ({a.offsets} offset(s)), {a.jobs} parallel")
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        recs = list(ex.map(run_one, jobs))

    csv_path = outroot / f"btc_gem5_{a.bench}.csv"
    cols = ["arm", "cfg", "offset", "rc", "iter", "warmup", "targets", "cycles",
            "numInsts", "simInsts", "ipc", "ditSuppressed", "ditSwitches",
            "commitNonSpecStalls", "checksum", "wall_s"]
    with open(csv_path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in recs:
            w.writerow(r)
    print("WROTE", csv_path)

    # ---- gates, per offset ----------------------------------------------
    ok = True
    by = {(r["arm"], r["cfg"], r["offset"]): r for r in recs}
    failed = [r for r in recs if r["rc"] != 0 or not r.get("cycles")]
    if failed:
        ok = False
        print(f"\nGATE FAIL: {len(failed)} run(s) without a result: "
              f"{[(r['arm'], r['cfg'], r['offset']) for r in failed]}")

    print("\n--- gate 1: simInsts identical across switch models ---")
    for arm in arms:
        for off in offs:
            vals = {c: by[(arm, c, off)].get("simInsts") for c in cfgs if (arm, c, off) in by}
            uniq = set(v for v in vals.values() if v is not None)
            status = "OK" if len(uniq) <= 1 else "FAIL"
            if len(uniq) > 1:
                ok = False
            print(f"  o{off} {arm:<9} {status:<5} {vals}")

    print("\n--- gate 2: ditSuppressed == 0 in unprotected arms ---")
    for arm in [x for x in UNPROTECTED if x in arms]:
        for c in cfgs:
            for off in offs:
                if (arm, c, off) not in by:
                    continue
                v = by[(arm, c, off)].get("ditSuppressed")
                status = "OK" if not v else "FAIL"
                if v:
                    ok = False
                print(f"  o{off} {arm:<9} {c:<7} {status:<5} ditSuppressed={v}")

    # Gate 3 compares cycles ACROSS configs, which is only meaningful when the
    # configs differ solely in the DIT switch model. Mechanism-isolation configs
    # (none/eves/dmp/...) legitimately change base cycles, so the gate would
    # misfire; restrict it to the switch-model pair.
    SWITCH_MODEL_CFGS = {"spec", "serdit"}
    gate3_cfgs = [c for c in cfgs if c in SWITCH_MODEL_CFGS]
    if len(gate3_cfgs) < 2:
        print("\n--- gate 3: skipped (needs both spec and serdit; configs vary "
              "mechanisms, not the switch model) ---")
    else:
        print("\n--- gate 3: switch model is a no-op on arms with no DIT switch ---")
    # base and plain contain zero `msr DIT`, so --no-speculative-dit rewrites
    # nothing in them and their cycle counts MUST be identical across models.
    # A nonzero delta here means the knob under test is reaching the controls,
    # and it bounds nothing until it is fixed. This gate caught exactly that:
    # a runtime blanket switch inside main() moved `nodit` by 0.549%.
        for arm in [x for x in UNPROTECTED if x in arms]:
            for off in offs:
                cs = [by[(arm, c, off)].get("cycles") for c in gate3_cfgs if (arm, c, off) in by]
                cs = [x for x in cs if x]
                if len(cs) < 2:
                    continue
                delta = (max(cs) / min(cs) - 1) * 100
                status = "OK" if delta == 0.0 else "FAIL"
                if delta != 0.0:
                    ok = False
                print(f"  o{off} {arm:<9} {status:<5} delta={delta:+.4f}%  cycles={cs}")

    print("\n--- checksums (must all match) ---")
    cks = {r["checksum"] for r in recs if r["checksum"]}
    print(f"  {cks}  {'OK' if len(cks) <= 1 else 'FAIL: arms did different work'}")
    if len(cks) > 1:
        ok = False

    # ---- result: medians over offsets, with the spread ----------------------
    def vals(arm, c, key="cycles"):
        return [by[(arm, c, o)][key] for o in offs
                if (arm, c, o) in by and by[(arm, c, o)].get(key)]

    def med(arm, c, key="cycles"):
        v = vals(arm, c, key)
        return st.median(v) if v else None

    def spread(arm, c):
        v = vals(arm, c)
        return (max(v) / min(v) - 1) * 100 if len(v) > 1 else 0.0

    print(f"\n--- cycles vs base and IPC, median over {a.offsets} offset(s); "
          f"spread = max/min - 1 over offsets ---")
    hdr = f"{'arm':<9}" + "".join(f"{c:>34}" for c in cfgs) + f"{'spread':>9}"
    print(hdr)
    for arm in arms:
        line = f"{arm:<9}"
        for c in cfgs:
            r, b = med(arm, c), med("base", c)
            if r and b:
                pct = (r / b - 1) * 100
                ipc = med(arm, c, "ipc")
                line += f"{r:>12,.0f} {pct:>+7.2f}% ipc={ipc:>8.4f}" if ipc else f"{r:>12,.0f} {pct:>+7.2f}% ipc={'-':>8}"
            else:
                line += f"{'-':>34}"
        line += f"{max(spread(arm, c) for c in cfgs):>8.2f}%"
        print(line)

    print("\nGATES", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
