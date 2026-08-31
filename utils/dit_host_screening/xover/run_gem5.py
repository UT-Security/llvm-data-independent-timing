#!/usr/bin/env python3
"""Crossover sweeps for the composite, under gem5.

THREE SWEEPS, one script.

  fsweep   Vary the secret fraction f with the public work held identical.
           Produces the crossover curve: selective minus blanket against f, and
           the f* where they cross.

  optsweep Vary HOW MANY optimizations DIT gates, at fixed f. This is the
           forward-looking experiment and gem5 is the only instrument that can
           run it: c_P is the always-on cost of the public lane, so as hardware
           gates more optimizations c_P rises, and f* = (c_P-phi)/(c_P-phi+tau)
           moves RIGHT. It measures the claim "always-on gets more expensive as
           machines get more aggressive" instead of asserting it.

  rsweep   Vary the work per DIT region at fixed f, via batch size and via the
           pass's own placement granularity. Blanket must be FLAT here - it
           never toggles - and if it is not, the knob is not doing what it says.

WHY GEM5 AT ALL: it answers two questions silicon cannot. (1) The renamed-switch
counterfactual - real ARM serialises `msr DIT`; the fork also models it as a
renamed CC-register write. (2) Exact coverage, via compSimplifier.ditSuppressed,
which is the only way to show a placement protects as much as the oracle rather
than merely running faster.

GATES, all exact because gem5 is deterministic:
  * simInsts IDENTICAL across machine configs for one binary+input. If it is not,
    the driver is perturbing itself (this failed the first time: the driver
    called clock_gettime, and gem5 SE returns SIMULATED time).
  * Unprotected arms (off, and every pass arm on a no-secret point) must report
    the ditSuppressed their placement implies; `off` must report exactly ZERO.
  * Checksums identical across arms.
"""
import argparse, hashlib, itertools, json, os, pathlib, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G = pathlib.Path.home() / "Documents/gem5-DIT"
GEM5 = G / "build/ARM/gem5.opt"
CONFIG = G / "configs/example/arm/fdp_neoverse_v2_binary.py"
# Default is the SQLite-public-lane composite. --bin selects another one
# (build/gem5lua = the Lua public lane); the arm names and the argv contract
# are identical across composites, which is what lets one driver run both.
BIN = pathlib.Path.home() / "Documents/dit-crossover/build/gem5"

# arm -> (binary suffix, runtime DIT mode)
ARMS = {
    "off":    ("nodit",  0),   # baseline: round-trip control, DIT never set
    "always": ("nodit",  1),   # blanket
    "oracle": ("nodit",  2),   # perfect placement, one region per signature
    "batch":  ("nodit",  3),   # perfect placement, one region per batch
    "hoist":  ("hoist",  0),   # the pass, loop-hoisted
    "gated":  ("gated",  0),   # the pass, + call-site mod-set gate
    "hoist0": ("hoist0", 0),   # the pass at its SHIPPED default (block-minimal)
    "plain":  ("plain",  0),   # stock -O2; plain->off IS the round-trip artifact
    "swcyc30":("swcyc30",0),   # region + corridor merging at 30 cyc/switch
    "nopctl": ("nopctl", 0),   # gated, with every MSR DIT emitted as HINT #0:
                               # the code-alignment control (Marinaro et al.)
}

CONFIGS = {
    # --- switch-model axis ---
    "spec":        ["--eves", "--dmp", "--comp-simp"],
    "serdit":      ["--eves", "--dmp", "--comp-simp", "--no-speculative-dit"],
    # --- optimization-count axis (the "future hardware" sweep) ---
    # Each step gates one more optimization behind DIT, so c_P rises monotonically.
    "opt0":        ["--value-predictor", "off"],
    "opt1":        ["--eves"],
    "opt2":        ["--eves", "--dmp"],
    "opt3":        ["--eves", "--dmp", "--comp-simp"],
}

STAT_RE = re.compile(r"^(\S+)\s+([-\d.eninf]+)")


def first_dump(path):
    """First Begin/End block only: that is the ROI. Parsing the last dump picks
    up gem5 teardown - trap 9, already made once on this project."""
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


def run_one(job):
    arm, cfg, sigs, period, verifies, outroot, a = job
    tag = f"{arm}__{cfg}__s{sigs}_p{period}_v{verifies}"
    d = outroot / tag
    d.mkdir(parents=True, exist_ok=True)
    if (d / "stats.txt").exists() and a.resume:
        s = first_dump(d / "stats.txt")
        if pick(s, "core.numCycles"):
            print(f"  [skip] {tag}", flush=True)
            return collect(d, arm, cfg, sigs, period, verifies, 0, 0.0)

    suffix, mode = ARMS[arm]
    binpath = canon_path(BIN / f"xover_{suffix}", arm, cfg, sigs, period, verifies)
    cmd = [str(GEM5), "-d", str(d), str(CONFIG)] + CONFIGS[cfg] + [
        "--binary", str(binpath),
        "--arguments", f"{mode} {a.rows} {a.rounds} {sigs} {period} {verifies}",
    ]
    t0 = time.time()
    with open(d / "run.log", "w") as log:
        rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT).returncode
    return collect(d, arm, cfg, sigs, period, verifies, rc, time.time() - t0)


def canon_path(src, *key):
    """Run every arm from an EQUAL-LENGTH path.

    gem5 SE mode writes the binary path onto the initial process stack as
    argv[0], so its LENGTH shifts stack alignment for the whole run. Measured
    with one byte-identical binary: 287,318 / 285,068 / 284,936 cycles at a 1-,
    36- and 18-char name, a 0.84% spread from the file name alone. Only the
    length matters, not the text: two 1-char names agreed to the cycle.

    Here `xover_nodit`, `_hoist`, `_gated`, `_plain` are all 11 characters but
    `_hoist0` and `_nopctl` are 12 and `_swcyc30` is 13, so the shipped-default
    arm, the NOP alignment control and the switch-cyc arm were each measured
    against a baseline at a different path length. Fixed-width hex slot keeps
    every path the same length while staying unique per parallel job; hard
    linked, so no disk or copy cost. See dit-gem5-rig-traps #5.
    """
    slot = hashlib.md5("/".join(map(str, key)).encode()).hexdigest()[:8]
    c = BIN / "armlink" / slot / "b"
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists():
        c.unlink()
    try:
        os.link(src, c)
    except OSError:
        shutil.copy2(src, c)
    return c


def collect(d, arm, cfg, sigs, period, verifies, rc, wall):
    rec = {"arm": arm, "cfg": cfg, "sigs": sigs, "period": period,
           "verifies": verifies, "rc": rc, "wall_s": round(wall, 1), "tag": d.name}
    sp = d / "stats.txt"
    if sp.exists():
        s = first_dump(sp)
        rec.update({
            "cycles": pick(s, "core.numCycles"),
            "insts": pick(s, "commitStats0.numInsts", "core.committedInsts"),
            "ditSuppressed": pick(s, "compSimplifier.ditSuppressed", "ditSuppressed"),
            "ditTaggedSet": pick(s, "valuePredictor.ditTaggedSet"),
            "vpPredictions": pick(s, "valuePredictor.predictions"),
        })
    log = d / "run.log"
    if log.exists():
        txt = log.read_text(errors="replace")
        m = re.search(r"checksum (\d+)", txt)
        rec["checksum"] = int(m.group(1)) if m else None
        m = re.search(r"signs=(\d+) verifs=(\d+) toggles=(\d+)", txt)
        if m:
            rec["signs"], rec["verifs"], rec["toggles"] = (int(m.group(i)) for i in (1, 2, 3))
    ok = "ok " if rc == 0 and rec.get("cycles") else "FAIL"
    print(f"  [{ok}] {rec['tag']:<38} cyc={rec.get('cycles')} "
          f"insts={rec.get('insts')} ditSupp={rec.get('ditSuppressed')} "
          f"tog={rec.get('toggles')} {rec['wall_s']}s", flush=True)
    return rec


# (sigs, period). The ROI is rounds*60 queries, so period must fit inside it.
# At the gem5 ROI size one signature is already ~1% of the run, so gem5 covers
# f ~ 1%..55% - which brackets the predicted f*. The sub-1% tail is covered
# natively instead, where a 100x longer ROI is affordable.
#
# (0, 1) is LOAD-BEARING, not a spare point: f is measured by differencing this
# arm's cycles against each point's, which is exact under gem5. It is also the
# SIGS=0 control - the secret lane does nothing and executes zero switches, so
# every arm must be instruction-identical to `off` here.
F_GRID = [(0, 1), (1, 120), (1, 40), (1, 12), (1, 4), (1, 1), (2, 1), (4, 1)]
R_GRID = [(1, 1), (2, 2), (4, 4), (8, 8), (16, 16), (32, 32)]   # f fixed, R scales
V_GRID = [0, 1, 2, 4, 8]                                        # the phi dial


def main():
    global BIN
    ap = argparse.ArgumentParser()
    ap.add_argument("--sweep", choices=["fsweep", "optsweep", "rsweep", "vsweep"],
                    default="fsweep")
    ap.add_argument("--arms", default="")
    ap.add_argument("--configs", default="")
    ap.add_argument("--rows", type=int, default=1500)
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--points", default="")
    ap.add_argument("--jobs", type=int, default=9)
    ap.add_argument("--out", default=str(pathlib.Path.home() / "Documents/dit-crossover/out/gem5/fsweep"))
    ap.add_argument("--resume", action="store_true", default=True)
    ap.add_argument("--bin", default=str(BIN),
                    help="directory holding the xover_<arm> binaries")
    a = ap.parse_args()
    BIN = pathlib.Path(a.bin)

    if a.sweep == "fsweep":
        arms = a.arms or "off,always,oracle,hoist,gated,swcyc30"
        cfgs = a.configs or "spec,serdit"
        pts = [(s, p, 0) for s, p in F_GRID]
    elif a.sweep == "optsweep":
        arms = a.arms or "off,always,oracle,gated,swcyc30"
        cfgs = a.configs or "opt0,opt1,opt2,opt3"
        pts = [(0, 1, 0), (1, 40, 0), (1, 4, 0)]   # zero-point + low-f + mid-f
    elif a.sweep == "rsweep":
        arms = a.arms or "off,always,batch,hoist,swcyc30"
        cfgs = a.configs or "serdit,spec"
        pts = [(s, p, 0) for s, p in R_GRID]
    else:  # vsweep - the false-positive dial
        # period=12 gives 10 trigger points in the ROI, so v scales the verify
        # share of the public lane from 0 to roughly two thirds. The secret lane
        # is held at one signature per trigger throughout, so f barely moves and
        # everything that changes is phi.
        arms = a.arms or "off,always,oracle,hoist,gated,nopctl"
        cfgs = a.configs or "serdit,spec"
        pts = [(1, 12, v) for v in V_GRID]

    if a.points:
        pts = [tuple(int(x) for x in t.split(":")) for t in a.points.split(",")]

    arms = arms.split(",")
    cfgs = cfgs.split(",")
    outroot = pathlib.Path(a.out)
    outroot.mkdir(parents=True, exist_ok=True)

    missing = [x for x in arms if not (BIN / f"xover_{ARMS[x][0]}").exists()]
    if missing:
        sys.exit(f"missing binaries for arms: {missing} (run build.sh gem5)")

    jobs = [(arm, cfg, s, p, v, outroot, a)
            for (s, p, v), cfg, arm in itertools.product(pts, cfgs, arms)]
    print(f"{a.sweep}: {len(jobs)} runs, jobs={a.jobs}, "
          f"rows={a.rows} rounds={a.rounds}", flush=True)

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        recs = list(ex.map(run_one, jobs))

    resfile = outroot / "results.jsonl"
    with open(resfile, "w") as fh:
        for r in recs:
            fh.write(json.dumps(r) + "\n")
    print(f"\n{len(recs)} runs in {(time.time()-t0)/60:.1f} min -> {resfile}")

    # ---- gates ----
    bad = [r for r in recs if r["rc"] != 0 or not r.get("cycles")]
    if bad:
        print(f"!! {len(bad)} FAILED runs")
    offs = [r for r in recs if r["arm"] == "off" and r.get("ditSuppressed")]
    if offs:
        print(f"!! GATE FAIL: `off` arm reports nonzero ditSuppressed on "
              f"{len(offs)} runs - the baseline is running some placement")
    # simInsts must match across configs for the same arm+point
    bykey = {}
    for r in recs:
        if r.get("insts"):
            bykey.setdefault((r["arm"], r["sigs"], r["period"], r["verifies"]),
                             {})[r["cfg"]] = r["insts"]
    for k, v in bykey.items():
        if len(set(v.values())) > 1:
            print(f"!! GATE FAIL: simInsts differs across configs for {k}: {v}")
    cks = {}
    for r in recs:
        if r.get("checksum") is not None:
            cks.setdefault((r["sigs"], r["period"], r["verifies"]),
                           set()).add(r["checksum"])
    for k, v in cks.items():
        if len(v) > 1:
            print(f"!! GATE FAIL: checksums differ at point {k}: {v}")


if __name__ == "__main__":
    main()
