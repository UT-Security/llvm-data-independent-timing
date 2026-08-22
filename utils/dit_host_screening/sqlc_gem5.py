#!/usr/bin/env python3
"""The SQLCipher secret-fraction sweep, under gem5.

WHY RUN IT HERE TOO. Silicon answers "how much does always-on cost"; gem5 answers
two things silicon cannot:
  1. THE RENAMED-SWITCH COUNTERFACTUAL. Real ARM serialises `msr DIT`. The fork
     also models it as a renamed CC-register write, which is what a future
     implementation could do. On silicon the pass loses this workload by ~25 points
     of toggle overhead; if that collapses under a renamed switch, the verdict is
     a property of today's hardware rather than of the technique.
  2. EXACT COVERAGE. `compSimplifier.ditSuppressed` counts optimizations actually
     withheld because DIT was set - the only way to prove a placement protects as
     much as the oracle rather than merely running faster.

TWO GATES, BOTH EXACT AND FREE, BOTH OF WHICH FAILED THE FIRST TIME THEY WERE
USED ON THIS PROJECT:
  * simInsts must be IDENTICAL across switch models for the same binary+input.
    The instruction stream cannot depend on the machine model; if it does, the
    driver is perturbing itself.
  * The unprotected arms (plain, nodit) must report ZERO ditSuppressed. A nonzero
    count means the "baseline" is silently running some placement.

EACH RUN GETS ITS OWN COPY OF THE DATABASE. Runs execute in parallel and SQLite
opens the file read-write, so a shared file would let concurrent runs corrupt
each other - a failure that would look like noise, not like an error.
"""
import argparse, json, os, pathlib, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G = pathlib.Path.home() / "Documents/gem5-DIT"
GEM5 = G / "build/ARM/gem5.opt"
CONFIG = G / "configs/example/arm/fdp_neoverse_v2_binary.py"
BIN = G / "benchmarks/sqlcipher/bin"
ARMS = {"plain": "sqlcipher_query", "blanket": "q_blanket",
        "nodit": "q_nodit", "hoist": "q_hoist"}
CONFIGS = {
    # the fork's default: MSR DIT as a renamed CC-register write
    "spec":   ["--eves", "--dmp", "--comp-simp"],
    # what ARM silicon does today: MSR DIT as a pipeline barrier
    "serdit": ["--eves", "--dmp", "--comp-simp", "--no-speculative-dit"],
}
STAT_RE = re.compile(r"^(\S+)\s+([-\d.eninf]+)")


def first_dump(path):
    """First Begin/End block only: that is the ROI. Parsing the last dump instead
    picks up gem5 teardown - a mistake already made once on this benchmark."""
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
    arm, cfg, cache, outroot, a = job
    d = outroot / f"{arm}__{cfg}__c{cache}"
    d.mkdir(parents=True, exist_ok=True)
    db = d / "q.db"
    if not db.exists():
        shutil.copy(a.db, db)
    cmd = [str(GEM5), "-d", str(d), str(CONFIG)] + CONFIGS[cfg] + [
        "--env", f"CACHE_SIZE={cache}",
        "--env", f"KDF_ITER={a.kdf}",
        # MUST be a URI selecting the unix-none VFS. A bare path makes SQLite
        # use the default VFS, which takes fcntl locks; gem5's syscall emulation
        # cannot service those and the open fails with SQLITE_PERM ("journal
        # failed: 3"). The driver's own default has the same URI for this reason.
        "--env", f"QDB=file:{db}?vfs=unix-none",
        "--binary", str(BIN / ARMS[arm]),
        "--arguments",
        f"--rows {a.rows} --queries {a.queries} --payload {a.payload} "
        f"--warmup {a.warmup} --reuse",
    ]
    t0 = time.time()
    with open(d / "run.log", "w") as log:
        rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT).returncode
    rec = {"arm": arm, "cfg": cfg, "cache": cache, "rc": rc,
           "wall_s": round(time.time() - t0, 1)}
    sp = d / "stats.txt"
    if rc == 0 and sp.exists():
        s = first_dump(sp)
        rec.update({
            "cycles": pick(s, "core.numCycles"),
            "insts": pick(s, "commitStats0.numInsts", "core.committedInsts"),
            "ditSuppressed": pick(s, "compSimplifier.ditSuppressed", "ditSuppressed"),
            "ditTaggedSet": pick(s, "valuePredictor.ditTaggedSet"),
        })
        txt = (d / "run.log").read_text(errors="replace")
        m = re.search(r"decrypts_per_query=([\d.]+)", txt)
        rec["dec_q"] = float(m.group(1)) if m else None
        m = re.search(r"checksum=(\d+)", txt)
        rec["checksum"] = int(m.group(1)) if m else None
    print(f"  [{'ok ' if rc == 0 else 'FAIL'}] {arm:<8} {cfg:<7} cache={cache:<5} "
          f"cycles={rec.get('cycles')} insts={rec.get('insts')} "
          f"ditSupp={rec.get('ditSuppressed')} {rec['wall_s']}s", flush=True)
    return rec


def main():
    # Run from a directory that cannot be rewritten underneath us. gem5's
    # Process.py calls os.getcwd() at import time, so if the launch directory is
    # deleted mid-sweep every subsequent run dies with FileNotFoundError before
    # simulating anything. That happened here: a `git rebase` in the checkout
    # this script lives in removed and recreated its own directory, and 26 of 30
    # runs failed at import while the 4 already-started ones completed fine.
    os.chdir(pathlib.Path.home())

    ap = argparse.ArgumentParser()
    ap.add_argument("--caches", default="16,1024,1792,1920,2048")
    ap.add_argument("--arms", default="plain,blanket,nodit,hoist")
    ap.add_argument("--configs", default="spec,serdit")
    ap.add_argument("--rows", type=int, default=2000)
    ap.add_argument("--queries", type=int, default=200)
    ap.add_argument("--payload", type=int, default=3500)
    ap.add_argument("--warmup", type=int, default=50)
    ap.add_argument("--kdf", type=int, default=4000)
    ap.add_argument("--jobs", type=int, default=5)
    ap.add_argument("--db", default=str(pathlib.Path.home() /
                                        "Documents/dit-browser-bench/sqlcdb/q_r2000_p3500.db"))
    ap.add_argument("--out", default=str(pathlib.Path.home() /
                                         "Documents/dit-browser-bench/gem5-sqlc"))
    a = ap.parse_args()
    a.db = pathlib.Path(a.db)

    for arm in a.arms.split(","):
        if not (BIN / ARMS[arm]).exists():
            sys.exit(f"missing cross binary: {BIN / ARMS[arm]}")
    if not a.db.exists():
        sys.exit(f"missing database: {a.db}")

    outroot = pathlib.Path(a.out)
    outroot.mkdir(parents=True, exist_ok=True)
    jobs = [(arm, cfg, int(c), outroot, a)
            for c in a.caches.split(",")
            for arm in a.arms.split(",")
            for cfg in a.configs.split(",")]
    print(f"gem5 SQLCipher sweep: {len(jobs)} runs, {a.jobs} parallel")
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        recs = list(ex.map(run_one, jobs))

    res = outroot / "results.jsonl"
    with open(res, "w") as fh:
        for r in recs:
            fh.write(json.dumps(r) + "\n")
    print("\nwrote", res)


if __name__ == "__main__":
    main()
