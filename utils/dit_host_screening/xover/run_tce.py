#!/usr/bin/env python3
"""f-sweep for the SQLite TCE composite under gem5.

THE KNOB IS enc_cols AND IT MOVES ONLY f. Eight BLOB columns are stored on every
row at every point; enc_cols of them are produced by an AEAD call and the rest by
a same-sized fill, so the B-tree descends and splits identically and the two
indexes are maintained identically at every point on the curve. If the public
work moved with the knob, the crossover this produces would be meaningless.

GATES, all exact because gem5 is deterministic:
  * simInsts IDENTICAL across switch models for one binary+input. If not, the
    driver is perturbing itself - pass time_secret=0 so the host never calls
    clock_gettime inside the region (gem5 SE returns SIMULATED time).
  * `off` must report exactly ZERO ditSuppressed. A non-zero reading means the
    baseline is silently running someone else's placement.
  * Checksums identical across arms at one point. Different checksums mean the
    arms are not doing the same work and no timing comparison is valid.
"""
import argparse, json, pathlib, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G = pathlib.Path.home() / "Documents/gem5-DIT"
CONFIG = G / "configs/example/arm/fdp_neoverse_v2_binary.py"
BIN = pathlib.Path.home() / "Documents/dit-crossover/build/tce/gem5"

# arm -> (binary suffix, runtime DIT mode)
ARMS = {
    "off":    ("nodit", 0),   # THE BASELINE: round-trip control, DIT never set
    "always": ("nodit", 1),   # blanket
    "field":  ("nodit", 2),   # oracle, one region per encrypted field
    "row":    ("nodit", 3),   # oracle, one region per row
    "def30":  ("def30", 0),   # the pass at its shipped default
    "def0":   ("def0",  0),   # the pass with switch-cyc=0 (finest)
    "nop30":  ("nop30", 0),   # layout control for def30
    "nop0":   ("nop0",  0),   # layout control for def0
    # Tail calls suppressed at lowering (-disable-tail-calls), so an instrumented
    # function has an epilogue in which to emit its DIT clear. ntcbase is the
    # MATCHED round-trip control: same lowering, taint pass not run. Comparing
    # ntc30 against `off` would charge the tail-call codegen change to DIT.
    "ntcbase":("ntcbase",0),
    "ntc30":  ("ntc30", 0),
}

CONFIGS = {
    "spec":   ["--eves", "--dmp", "--comp-simp"],
    "serdit": ["--eves", "--dmp", "--comp-simp", "--no-speculative-dit"],
}

STAT_RE = re.compile(r"^(\S+)\s+([-\d.eninf]+)")


def first_dump(path):
    """First Begin/End block only - the ROI. Parsing the last dump picks up gem5
    teardown (trap 9, already made once on this project)."""
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
    arm, cfg, enc, a, outroot = job
    tag = f"{arm}__{cfg}__e{enc}"
    d = outroot / tag
    d.mkdir(parents=True, exist_ok=True)

    suffix, mode = ARMS[arm]
    binpath = BIN / f"xtce_{suffix}"
    if not binpath.exists():
        return {"arm": arm, "cfg": cfg, "enc": enc, "error": f"missing {binpath}"}

    argv = f"{mode} {a.field_bytes} {enc} {a.rows} {a.batch} {a.reads} 0"
    cmd = [str(G / "build/ARM" / a.gem5), f"--outdir={d}", str(CONFIG),
           "--binary", str(binpath), "--arguments", argv] + CONFIGS[cfg]

    t0 = time.time()
    if not ((d / "stats.txt").exists() and a.resume):
        p = subprocess.run(cmd, capture_output=True, text=True)
        (d / "run.log").write_text(p.stdout + p.stderr)
    wall = time.time() - t0

    log = (d / "run.log").read_text() if (d / "run.log").exists() else ""
    s = first_dump(d / "stats.txt") if (d / "stats.txt").exists() else {}

    m = re.search(r"WORK tce checksum (\d+)", log)
    r = {
        "arm": arm, "cfg": cfg, "enc": enc, "mode": mode, "wall_s": round(wall, 1),
        "checksum": int(m.group(1)) if m else None,
        "cycles": pick(s, "core.numCycles", "numCycles"),
        "insts": pick(s, "simInsts"),
        "dit_suppressed": pick(s, "ditSuppressed"),
        "dit_switches": pick(s, "ditSwitches", "numDitSwitches"),
    }
    hm = re.search(r"toggles=(\d+)", log)
    if hm:
        r["toggles"] = int(hm.group(1))
    if r["cycles"] is None:
        r["error"] = "no cycles - run did not complete"
    print(f"  [{'ok ' if r.get('error') is None else 'ERR'}] {tag} "
          f"cycles={r['cycles']} insts={r['insts']} ({wall:.0f}s)", flush=True)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--enc", default="0,1,2,4,6,8",
                    help="enc_cols grid - the f knob")
    ap.add_argument("--arms", default=",".join(ARMS))
    ap.add_argument("--configs", default="spec,serdit")
    ap.add_argument("--rows", type=int, default=4000)
    ap.add_argument("--batch", type=int, default=500)
    ap.add_argument("--reads", type=int, default=400)
    ap.add_argument("--field-bytes", type=int, default=128)
    ap.add_argument("--jobs", type=int, default=48)
    ap.add_argument("--gem5", default="gem5.fast")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--out", default=str(pathlib.Path.home() /
                    "Documents/dit-crossover/out/gem5/tce_fsweep"))
    a = ap.parse_args()

    outroot = pathlib.Path(a.out)
    outroot.mkdir(parents=True, exist_ok=True)
    encs = [int(x) for x in a.enc.split(",")]
    arms = a.arms.split(",")
    cfgs = a.configs.split(",")

    jobs = [(arm, cfg, enc, a, outroot)
            for enc in encs for cfg in cfgs for arm in arms]
    print(f"{len(jobs)} runs, {a.jobs} at a time, {a.gem5}", flush=True)

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(run_one, jobs))

    jl = outroot / "results.jsonl"
    with open(jl, "w") as fh:
        for r in results:
            fh.write(json.dumps(r) + "\n")
    print(f"\nwrote {jl}")

    # ---- gates -------------------------------------------------------------
    print("\n=== gates ===")
    fail = 0
    by = {}
    for r in results:
        by.setdefault((r["enc"], r["arm"]), {})[r["cfg"]] = r

    for (enc, arm), d in sorted(by.items()):
        if len(d) < 2:
            continue
        vals = {c: v.get("insts") for c, v in d.items()}
        if len(set(v for v in vals.values() if v is not None)) > 1:
            print(f"  FAIL simInsts differ across switch models: enc={enc} arm={arm} {vals}")
            fail += 1

    for r in results:
        if r["arm"] == "off" and r.get("dit_suppressed"):
            print(f"  FAIL off arm suppressed {r['dit_suppressed']} ops "
                  f"(enc={enc}) - contaminated baseline")
            fail += 1

    for enc in encs:
        cks = {r["arm"]: r["checksum"] for r in results
               if r["enc"] == enc and r["checksum"] is not None}
        if len(set(cks.values())) > 1:
            print(f"  FAIL checksums differ at enc={enc}: {cks}")
            fail += 1

    errs = [r for r in results if r.get("error")]
    if errs:
        print(f"  {len(errs)} runs did not complete")
        fail += len(errs)
    if not fail:
        print("  all gates pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
