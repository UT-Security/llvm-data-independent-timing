#!/usr/bin/env python3
"""Re-run Result 2 of docs/results/dit-switch-cyc-confirmation.md under gem5.

That result reported a def-minus-NOP term "straddling zero" and concluded that
switch-cyc=30 buys fewer INSTRUCTIONS rather than cheaper mode switches. It was
retracted 2026-08-30: build_sodium_arms.sh passed -taint-dit-nop-switches only to
the analysis stage, so every NOP arm was byte-identical to its non-NOP twin and
the term was two timings of the same binary.

The original is native Apple M5 and cannot be reproduced without FEAT_DIT
silicon. This is the same WORKLOAD (both libsodium composite lanes) on a
different INSTRUMENT, with arms that are actually NOPed:

    nodit  0 msr DIT     def0  520     def30  414     nop0  0     nop30  0

def-minus-NOP is now a real comparison: same placement, same instruction count,
same addresses, switches replaced by HINT #0 so no DIT executes.

Every arm also runs from an EQUAL-LENGTH path. gem5 SE mode writes the binary
path onto the initial process stack as argv[0], so its LENGTH shifts stack
alignment for the whole run: one byte-identical binary measured 287,318 /
285,068 / 284,936 cycles at a 1-, 36- and 18-char name, 0.84% from the file name
alone. Only the length matters, not the text. Here the `def0` and `nop0`
suffixes are 4 characters against 5 for `nodit`, `def30` and `nop30`, so
def0-vs-def30 and any *0-vs-nodit comparison were confounded; the NOP pairs
(nop0/def0, nop30/def30) happened to match and were not. `nodit` vs `always` is
one binary in two runtime modes and was never affected. See dit-gem5-rig-traps
 #5.
"""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G = pathlib.Path(os.environ.get("G5", pathlib.Path(__file__).resolve().parents[3] / "gem5-DIT"))
CONFIG = G / "configs/example/arm/fdp_neoverse_v2_binary.py"
BIN = pathlib.Path.home() / "Documents/dit-crossover/build/sodium_gem5"

# arm -> (binary suffix, runtime DIT mode). `always` is a runtime mode of the
# nodit binary, so blanket DIT is measured on the SAME codegen as the baseline.
ARMS = {"nodit": ("nodit", 0), "always": ("nodit", 1),
        "def0": ("def0", 0), "def30": ("def30", 0),
        "nop0": ("nop0", 0), "nop30": ("nop30", 0),
        # Callee-saved DIT ABI (docs/design/dit-abi.md), region placement.
        # abinop is its NOP control and is only meaningful since
        # "NOP-substitute both forms of the DIT write": before that the ABI's
        # unconditional `MSR DIT, Xt` exits survived substitution.
        "abi30": ("abi30", 0), "abinop": ("abinop", 0)}
CONFIGS = {
    "spec":   ["--eves", "--dmp", "--comp-simp"],
    "serdit": ["--eves", "--dmp", "--comp-simp", "--no-speculative-dit"],
}
STAT_RE = re.compile(r"^(\S+)\s+([-\d.eninf]+)")


def canon_path(src, *key):
    """Hard-link `src` to a fixed-WIDTH path so argv[0] length is constant."""
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


def first_dump(path):
    stats, inside = {}, False
    for line in open(path):
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
    lane, arm, cfg, msg, a, outroot = job
    tag = f"{lane}__{arm}__{cfg}__m{msg}"
    d = outroot / tag
    d.mkdir(parents=True, exist_ok=True)
    suffix, mode = ARMS[arm]
    binpath = BIN / f"xsod_{lane}_{suffix}"
    if not binpath.exists():
        return {"lane": lane, "arm": arm, "cfg": cfg, "msg": msg,
                "error": f"missing {binpath}"}
    # Equal-length path for every arm (see the module docstring). Kept under BIN
    # so the lua lane's relative work_sodium.lua still resolves against cwd.
    binpath = canon_path(binpath, lane, arm, cfg, msg)

    # mode 0 everywhere: the DIT comes from the compiler, not from argv. The
    # oracle modes are not part of Result 2.
    if lane == "sqlite":
        argv = f"{mode} 1 {msg} {a.period} {a.nper} {a.rows} {a.rounds}"
    else:
        argv = f"work_sodium.lua {mode} 1 {msg} {a.period} {a.nper} {a.depth}"

    cmd = [str(G / "build/ARM" / a.gem5), f"--outdir={d}", str(CONFIG),
           "--binary", str(binpath), "--arguments", argv] + CONFIGS[cfg]
    t0 = time.time()
    if not ((d / "stats.txt").exists() and a.resume):
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(BIN))
        (d / "run.log").write_text(p.stdout + p.stderr)
    wall = time.time() - t0

    log = (d / "run.log").read_text() if (d / "run.log").exists() else ""
    s = first_dump(d / "stats.txt") if (d / "stats.txt").exists() else {}
    m = re.search(r"WORK \w+ checksum (\d+)", log)
    fm = re.search(r"secret_frac=([\d.]+)%", log)
    r = {"lane": lane, "arm": arm, "cfg": cfg, "msg": msg,
         "wall_s": round(wall, 1),
         "checksum": int(m.group(1)) if m else None,
         "secret_frac": float(fm.group(1)) if fm else None,
         "cycles": pick(s, "core.numCycles", "numCycles"),
         "insts": pick(s, "simInsts"),
         "dit_suppressed": pick(s, "ditSuppressed"),
         "serializing": pick(s, "rename.serializing")}
    if r["cycles"] is None:
        r["error"] = "no cycles - run did not complete"
    print(f"  [{'ok ' if not r.get('error') else 'ERR'}] {tag} "
          f"cyc={r['cycles']} f={r['secret_frac']} ({wall:.0f}s)", flush=True)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lanes", default="sqlite,lua")
    ap.add_argument("--msgs", default="200,512,1400")
    ap.add_argument("--arms", default=",".join(ARMS))
    ap.add_argument("--period", type=int, default=1)
    ap.add_argument("--configs", default="spec,serdit")
    ap.add_argument("--nper", type=int, default=8)
    ap.add_argument("--rows", type=int, default=4000)
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--depth", type=int, default=11)
    ap.add_argument("--jobs", type=int, default=60)
    ap.add_argument("--gem5", default="gem5.fast")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--out", default=str(pathlib.Path.home() /
                    "Documents/dit-crossover/out/gem5/sodium_result2"))
    a = ap.parse_args()

    outroot = pathlib.Path(a.out)
    outroot.mkdir(parents=True, exist_ok=True)
    jobs = [(lane, arm, cfg, int(msg), a, outroot)
            for lane in a.lanes.split(",")
            for msg in a.msgs.split(",")
            for cfg in a.configs.split(",")
            for arm in a.arms.split(",")]
    print(f"{len(jobs)} runs, {a.jobs} at a time, {a.gem5}", flush=True)
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(run_one, jobs))

    jl = outroot / "results.jsonl"
    with open(jl, "w") as fh:
        for r in results:
            fh.write(json.dumps(r) + "\n")
    print(f"\nwrote {jl}")

    print("\n=== gates ===")
    fail = 0
    for lane in a.lanes.split(","):
        for msg in [int(x) for x in a.msgs.split(",")]:
            cks = {r["arm"]: r["checksum"] for r in results
                   if r["lane"] == lane and r["msg"] == msg and r["checksum"]}
            if len(set(cks.values())) > 1:
                print(f"  FAIL checksums differ {lane} msg={msg}: {cks}")
                fail += 1
    for r in results:
        if r["arm"] in ("nodit", "nop0", "nop30") and r.get("dit_suppressed"):
            print(f"  FAIL {r['arm']} suppressed {r['dit_suppressed']} "
                  f"({r['lane']} m{r['msg']} {r['cfg']}) - DIT executed in a NOP arm")
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
